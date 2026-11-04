use crate::protocol::*;
use crate::recovery::{Motions, Recovery};
use delirium::{
    collision::Collision,
    msg::{self, Value},
    nm::{self, Agent},
    prelude::*,
    ragdoll::{Build, Group, Ragdoll},
    shottask::{self, ShotTask},
};
use delirium_data::{
    rng::Rand,
    taskparams::{self, TaskParams},
};
use std::{
    path::PathBuf,
    time::{SystemTime, UNIX_EPOCH},
};
const DT: f32 = 1.0 / 60.0;
// A hand brushing a neighbour is contact, not evidence that the neighbour
// carries this actor's weight. Feet always count; other supports matter while
// down, crawling or rising, when no planted foot already carries the body.
fn body_carries_weight(part: usize, s: &ActorState) -> bool {
    if part == 3 || part == 6 { return true; }
    let feet = (1 << 3) | (1 << 6);
    if s.support_mask & feet != 0 { return false; }
    let missing_legs = s.detached_mask & ((1 << 2) | (1 << 3) | (1 << 5) | (1 << 6)) != 0;
    let foot_z = s.poses[3][14].min(s.poses[6][14]);
    s.recovery_active != 0 || missing_legs || s.com[2] - foot_z < 0.55
}
#[cfg(test)]
mod support_tests {
    use super::*;
    #[test]
    fn hand_contact_is_not_a_passenger_but_feet_and_recovery_support_are() {
        let mut s: ActorState = unsafe { std::mem::zeroed() };
        s.com[2] = 1.0;
        s.support_mask = 1 << 3;
        assert!(!body_carries_weight(16, &s));
        assert!(!body_carries_weight(10, &s));
        assert!(body_carries_weight(6, &s));
        s.support_mask = 0;
        assert!(!body_carries_weight(16, &s));
        s.recovery_active = 1;
        assert!(body_carries_weight(16, &s));
        s.recovery_active = 0;
        s.detached_mask = 1 << 3;
        assert!(body_carries_weight(1, &s));
        s.detached_mask = 0; s.com[2] = 0.3;
        assert!(body_carries_weight(10, &s));
    }
}
// Hit kind for a melee shove (BO1 knife/bayonet); 8 contact, 9 blast.
const MELEE: u32 = 10;
// BO1 brush faces may span an entire room. The original NM triangle contact
// path is conditioned for the much smaller faces in RAGE collision meshes.
// Bisect long edges without changing the surface or discarding map detail.
fn add_mesh(world: &mut Collision, points: &[[f32; 3]], triangles: &[[u32; 3]]) {
    let mut vertices = Vec::new();
    let mut faces = Vec::new();
    let mut pending: Vec<[Vec3; 3]> = triangles
        .iter()
        .map(|t| t.map(|i| Vec3::from_array(points[i as usize])))
        .collect();
    while let Some(t) = pending.pop() {
        let lengths = [
            (t[1] - t[0]).length_squared(),
            (t[2] - t[1]).length_squared(),
            (t[0] - t[2]).length_squared(),
        ];
        let edge = (0..3)
            .max_by(|&a, &b| lengths[a].total_cmp(&lengths[b]))
            .unwrap();
        if lengths[edge] > split2() {
            let a = edge;
            let b = (edge + 1) % 3;
            let c = (edge + 2) % 3;
            let m = (t[a] + t[b]) * 0.5;
            pending.push([t[a], m, t[c]]);
            pending.push([m, t[b], t[c]]);
        } else {
            let base = vertices.len() as u32;
            vertices.extend(t.map(|v| v.to_array()));
            faces.push([base, base + 1, base + 2]);
        }
    }
    world.add(&vertices, &faces, &vec![0; faces.len()]);
}
// Longest triangle edge kept unsplit, squared (BO1_DELIRIUM_SPLIT metres, for
// tests). 8 m keeps --yaw-large and the real Kino pile stable with half the
// triangles 4 m made (Kino 123k instead of 230k; 16 m failed the Kino pile).
fn split2() -> f32 {
    static SPLIT: std::sync::OnceLock<f32> = std::sync::OnceLock::new();
    *SPLIT.get_or_init(|| {
        let m: f32 = std::env::var("BO1_DELIRIUM_SPLIT").ok().and_then(|v| v.parse().ok()).unwrap_or(8.0);
        m * m
    })
}
fn v(p: [f32; 4]) -> Vec3 {
    Vec3::new(p[0], p[1], p[2])
}
fn tf(m: &[f32; 16]) -> Transform {
    let matrix = Mat3::from_cols(
        Vec3::new(m[0], m[1], m[2]),
        Vec3::new(m[4], m[5], m[6]),
        Vec3::new(m[8], m[9], m[10]),
    );
    Transform::from_translation(Vec3::new(m[12], m[13], m[14]))
        .with_rotation(Quat::from_mat3(&matrix).normalize())
}
fn matrix(t: Transform) -> [f32; 16] {
    let q = Mat3::from_quat(t.rotation);
    [
        q.x_axis.x,
        q.x_axis.y,
        q.x_axis.z,
        0.0,
        q.y_axis.x,
        q.y_axis.y,
        q.y_axis.z,
        0.0,
        q.z_axis.x,
        q.z_axis.y,
        q.z_axis.z,
        0.0,
        t.translation.x,
        t.translation.y,
        t.translation.z,
        0.0,
    ]
}
fn descending_steps(world: &Collision, from: Vec3, direction: Vec3) -> bool {
    let d = direction.with_z(0.0).normalize_or_zero();
    if d.length_squared() < 0.5 {
        return false;
    }
    let (mut last, mut drops, mut flats, mut descent) = (None::<f32>, 0, 0, 0.0);
    for i in 0..9 {
        let at = from + d * ((i as f32 - 2.0) * 0.2) + Vec3::Z * 0.1;
        let Some(z) = world.ground(at, 2.5) else {
            return false;
        };
        if let Some(prior) = last {
            let step = prior - z;
            if !(-0.035..=0.32).contains(&step) {
                return false;
            }
            if step > 0.035 {
                drops += 1;
                descent += step;
            } else if step.abs() < 0.012 {
                flats += 1;
            }
        }
        last = Some(z);
    }
    drops >= 2 && flats >= 2 && descent > 0.14
}
fn parse_line(line: &str) -> Result<(String, Vec<(String, Value)>), String> {
    let mut words = line.split_whitespace();
    let name = words.next().ok_or("empty message")?;
    let message = msg::find(name).ok_or_else(|| format!("unknown Delirium message {name}"))?;
    let mut out = Vec::new();
    for word in words {
        let (key, value) = word.split_once('=').ok_or("parameter needs =")?;
        let kind = if key == "start" {
            msg::Kind::Bool
        } else {
            message
                .params
                .iter()
                .find(|p| p.name == key)
                .ok_or_else(|| format!("unknown {name}.{key}"))?
                .kind
        };
        let val = match kind {
            msg::Kind::Float => {
                let f: f32 = value.parse().map_err(|_| "invalid float")?;
                if !f.is_finite() {
                    return Err("non-finite float".into());
                }
                Value::Float(f)
            }
            msg::Kind::Int => Value::Int(value.parse().map_err(|_| "invalid integer")?),
            msg::Kind::Bool => Value::Bool(match value {
                "true" | "1" => true,
                "false" | "0" => false,
                _ => return Err("invalid boolean".into()),
            }),
            msg::Kind::Str => Value::Str(value.trim_matches('"').into()),
            msg::Kind::Vec3 => {
                let v: Vec<f32> = value
                    .split(',')
                    .map(|n| n.parse().map_err(|_| "invalid vector"))
                    .collect::<Result<_, _>>()?;
                if v.iter().any(|f| !f.is_finite()) || v.len() != 3 {
                    return Err("three vector components required".into());
                }
                Value::Vec3(Vec3::new(v[0], v[1], v[2]))
            }
        };
        out.push((key.into(), val));
    }
    Ok((name.into(), out))
}
fn root() -> Result<PathBuf, String> {
    std::env::var_os("BO1_DELIRIUM_CACHE_DIR").map(PathBuf::from)
        .filter(|p| p.join("bo1-character.json").is_file())
        .ok_or_else(|| "Resource cache missing; launch PrepareResources first".into())
}

pub struct Actor {
    agent: Agent,
    build: Build,
    active: bool,
    target: Target,
    gait: Option<GaitReference>,
    shot: Option<ShotTask>,
    blast: Option<nm::Explosion>,
    born: u32,
    last_hit: u32,
    blast_start: u32,
    detached: u32,
    feedback: [u32; 6],
    wander: delirium::playerctl::Wander,
    recovery: Recovery,
    attacking: bool,
    airborne: f32,
    highfall: bool,
    pushes: Vec<TimedPush>,
    // Recent bullet wounds (legs count double), decaying 0.1 per second.
    wound: f32,
    // Fraction of Earth's gravity (Moon's low gravity: 136/800).
    gravity: f32,
    // Frames a corpse has lain nearly still (see the corpse freeze in step).
    still: u32,
    // A still corpse: left out of the solver, a static obstacle to the others.
    frozen: bool,
}
struct TimedPush {
    part: usize,
    local: Vec3,
    force: Vec3,
    left: f32,
}
impl Actor {
    fn context(&self) -> nm::Hit {
        let mut hit = nm::Hit::ragdoll(&self.agent);
        hit.health = if self.active { 160.0 } else { 0.0 };
        hit.dying = !self.active;
        hit.running = hit.vel.with_z(0.0).length() > 2.0;
        hit.moving = self.target.enabled != 0 && self.target.position[3] > 0.1;
        hit.attacker_head =
            (self.target.enabled != 0).then(|| v(self.target.position) + Vec3::Z * 0.65);
        hit
    }
    fn send(
        &mut self,
        world: &Collision,
        name: &str,
        params: &[(&str, Value)],
    ) -> Result<usize, String> {
        let mut state = std::mem::take(&mut self.agent.msgs);
        let out = msg::send(
            &mut self.agent,
            &self.build,
            world,
            &mut state,
            name,
            params,
        )
        .map_err(|e| format!("{name}: {e:?}"));
        self.agent.msgs = state;
        out
    }
    fn apply(&mut self, world: &Collision, m: &[shottask::Msg]) {
        let mut st = std::mem::take(&mut self.agent.msgs);
        shottask::apply(&mut self.agent, &self.build, world, &mut st, m);
        self.agent.msgs = st;
    }
    fn balance(&mut self, world: &Collision, tp: &TaskParams) {
        nm::task_balance(&mut self.agent, &self.build, world, tp, "*", None, false);
    }
    fn restore_injury(&mut self, world: &Collision) -> Result<(), String> {
        self.agent.doll.removed_parts = self.detached;
        for (bits, mask) in [(0x1e000, "ul"), (0x1e0000, "ur"), (0xe, "ll"), (0x70, "lr")] {
            if self.detached & bits != 0 {
                self.send(
                    world,
                    "injury",
                    &[
                        ("mask", Value::Str(mask.into())),
                        ("amount", Value::Float(1.0)),
                    ],
                )?;
            }
        }
        Ok(())
    }
}
pub struct Runtime {
    world: Collision,
    build: Build,
    rest: Vec<Transform>,
    bounds: [Quat; 21],
    actors: Vec<Option<Actor>>,
    fragments: Vec<Option<Actor>>,
    // Body-to-body contacts of the last step (see contacts_between_bodies).
    body_contacts: Vec<(usize, usize, usize, usize, Vec3)>,
    contact_group: Group,
    tp: TaskParams,
    rand: Rand,
    frame: u32,
    births: u32,
    motions: Motions,
    // Step timing for the periodic STATS line in the worker log.
    step_ms: f64,
    step_max: f64,
    group_ms: f64,
}
impl Runtime {
    pub fn load(floor: bool) -> Result<Self, String> {
        let root = root()?;
        let character=crate::character::Character::parse(
            &std::fs::read_to_string(root.join("bo1-character.json")).map_err(|e|e.to_string())?)?;
        let (joints,parts,masses,pairs)=character.asset();
        if joints.len()!=20 || parts.len()!=21 || masses.iter().any(|m| !m.is_finite() || *m<=0.) {
            return Err("invalid BO1 physical character".into());
        }
        delirium::nmrs::CUSTOM[0].store(true,std::sync::atomic::Ordering::Relaxed);
        delirium::nmrs::set(0,joints,parts,masses,pairs);
        // These custom frames are explicitly defined in the host's frame;
        // they need no Fred fragment link correction or logged GTA NM_LINKS.
        let _=delirium::nmdbm::DERIVED_LINKS[0].set(vec![Quat::IDENTITY;PARTS]);
        let (mut build,mut rest)=character.build().ok_or("invalid BO1 character build")?;
        let low=build.parts.iter().zip(&rest).map(|(p,t)| {
            let r=Mat3::from_quat(t.rotation);
            let extent=p.boxed.map_or(r.y_axis.z.abs()*p.half+p.radius,
                |e| r.x_axis.z.abs()*e.x+r.y_axis.z.abs()*e.y+r.z_axis.z.abs()*e.z);
            t.translation.z-extent
        }).fold(f32::INFINITY,f32::min);
        for t in &mut rest {t.translation.z-=low;}
        let tp=TaskParams::default();
        println!("BO1 custom character: {} parts, {:.3} kg; built-in task parameters",build.parts.len(),build.parts.iter().map(|p|p.mass).sum::<f32>());
        // A model's exported idle is not a solved physical stance. Calibrate the
        // reference in the actual original articulation before mapping BO1 bones
        // or using it as the terminal posture of a host get-up.
        let mut calibration = Collision::default();
        calibration.add_floor(0.0, 0.0, 0.0, 3.0, 0);
        let mut standing = Agent::new(
            &build,
            Ragdoll::new(&build, &rest, Vec3::ZERO),
            Quat::from_rotation_z(std::f32::consts::PI),
            0.3,
        );
        nm::task_balance(&mut standing, &build, &calibration, &tp, "*", None, false);
        for f in 0..180 {
            if std::env::var_os("BO1_PROFILE_DEBUG").is_some() && (f%30==0 || f<3) {
                let pose:Vec<_>=standing.doll.frames().collect();
                eprintln!("CAL f={f} root={:?} feet={:?}/{:?} balance={} fallen={} contacts={:?}",standing.doll.root(),pose[3].translation,pose[6].translation,standing.balancing(),standing.fallen,standing.doll.touch);
                if f<3 {let ch=standing.character().unwrap();for j in 0..6 {eprintln!("CALJOINT {j} actual={:?} desired={:?} limits={:?}",ch.angle(j,&pose),ch.eff[j].desired,ch.joints[j].limits);}}
            }
            standing.step(&build, &calibration, DT, &[], &[]);
        }
        if !standing.balancing() {
            return Err(format!("BO1 articulation did not balance: root {:?}, com {:?}, contacts {:?}",standing.doll.root(),standing.doll.com(),standing.doll.touch));
        }
        let calibrated: Vec<Transform> = standing.doll.frames().collect();
        if std::env::var_os("BO1_DELIRIUM_REST_AUDIT").is_some() {
            for (k, (a, b)) in rest.iter().zip(&calibrated).enumerate() {
                let d = b.rotation * a.rotation.inverse();
                let (axis, angle) = d.to_axis_angle();
                println!(
                    "REST part={k:2} {:<14} angle={:6.1}deg axis=({:5.2},{:5.2},{:5.2}) shift=({:5.2},{:5.2},{:5.2})",
                    build.parts[k].name,
                    angle.to_degrees(),
                    axis.x, axis.y, axis.z,
                    (b.translation - a.translation).x,
                    (b.translation - a.translation).y,
                    (b.translation - a.translation).z
                );
            }
        }
        rest = calibrated;
        let session_seed = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_err(|e| e.to_string())?
            .as_nanos() as u32
            ^ std::process::id();
        // Explicit opt-in for reproducible private physics regression tests.
        let seed = std::env::var("BO1_DELIRIUM_TEST_SEED")
            .ok()
            .and_then(|value| value.parse::<u32>().ok())
            .unwrap_or(session_seed);
        let motions = Motions::load(
            &std::env::var_os("NM108_RECOVERY_CACHE")
                .map(PathBuf::from)
                .unwrap_or_else(|| {
                    root.join("bo1-getup.motion")
                }),
        )?;
        let original = Ragdoll::new(&build, &rest, Vec3::ZERO);
        let bounds = std::array::from_fn(|k| {
            (rest[k].rotation.inverse() * original.collision_rotation(k)).normalize()
        });
        let mut engine = Self {
            world: Collision::default(),
            build,
            rest,
            bounds,
            actors: (0..ACTORS).map(|_| None).collect(),
            fragments: (0..FRAGMENTS).map(|_| None).collect(),
            body_contacts: Vec::new(),
            contact_group: Group::default(),
            tp,
            rand: Rand::new(seed),
            frame: 0,
            births: 0,
            motions,
            step_ms: 0.0,
            step_max: 0.0,
            group_ms: 0.0,
        };
        if floor {
            engine.world.add_floor(0.0, 0.0, 0.0, 100.0, 0);
        }
        engine.create(0, None)?;
        println!(
            "Loaded all Delirium controllers: {} core + {} additional messages, seed={seed}",
            msg::MESSAGES.len(),
            delirium::extras::messages().count()
        );
        Ok(engine)
    }
    fn create(&mut self, id: usize, pose: Option<&Initial>) -> Result<(), String> {
        self.contact_group.forget(id);
        if id >= ACTORS || self.actors[id].is_some() {
            return Err("occupied/invalid actor".into());
        }
        self.births = self.births.wrapping_add(1);
        let frames = pose.map_or_else(|| self.rest.clone(), |p| p.pose.iter().map(tf).collect());
        let facing = frames[0].rotation * self.rest[0].rotation.inverse();
        let mut agent = Agent::new(
            &self.build,
            Ragdoll::new(&self.build, &frames, Vec3::ZERO),
            facing,
            self.rand.next() as f32 / 32768.0,
        );
        // BO1 may hand over in a running/wounded pose. It is an initial state,
        // not this character's neutral stance or its hip/ankle calibration.

        agent.set_rng((
            (self.rand.next() << 16) ^ self.rand.next() ^ self.births.wrapping_mul(0x9e3779b9),
            (self.rand.next() << 16) ^ self.rand.next(),
        ));
        if agent.character().is_none() {
            return Err("Delirium original articulation was not constructed".into());
        }
        if let Some(p) = pose {
            for k in 0..PARTS {
                agent.doll.set_body(
                    k,
                    frames[k].translation,
                    frames[k].rotation,
                    v(p.linear[k]),
                    v(p.angular[k]),
                );
            }
        }
        let actor = Actor {
            agent,
            build: self.build.clone(),
            active: false,
            target: Target::default(),
            gait: None,
            shot: None,
            blast: None,
            born: self.frame,
            last_hit: 0,
            blast_start: 0,
            detached: 0,
            feedback: [0; 6],
            wander: Default::default(),
            recovery: Default::default(),
            attacking: false,
            airborne: 0.0,
            highfall: false,
            pushes: Vec::new(),
            wound: 0.0,
            gravity: 1.0,
            still: 0,
            frozen: false,

        };
        self.actors[id] = Some(actor);
        Ok(())
    }
    pub fn command(&mut self, r: &Request) -> Result<u32, String> {
        unsafe {
            let id = r.actor as usize;
            match r.command {
                1 => {
                    if r.steps > 8 || r.hit_count > 32 {
                        return Ok(3);
                    }
                    let a = r.data.advance;
                    for i in 0..ACTORS {
                        if let Some(actor) = &mut self.actors[i] {
                            actor.gait = (a.gait[i].weight != 0).then_some(a.gait[i]);
                        }
                        if a.target_mask & (1 << i) != 0 {
                            if let Some(actor) = &mut self.actors[i] {
                                actor.target = a.targets[i];
                            }
                        }
                    }
                    for h in a.hits.iter().take(r.hit_count as usize) {
                        self.hit(h)?;
                    }
                    for _ in 0..r.steps {
                        self.step()?;
                    }
                }
                2 => return Ok(6),
                3 => {
                    let m = &r.data.mesh;
                    if m.vertices < 3 || m.vertices > 128 || m.triangles == 0 || m.triangles > 256 {
                        return Ok(3);
                    }
                    let trans = tf(&m.transform);
                    let points: Vec<[f32; 3]> = m
                        .points
                        .iter()
                        .take(m.vertices as usize)
                        .map(|p| (trans.translation + trans.rotation * v(*p)).to_array())
                        .collect();
                    let triangles: Vec<[u32; 3]> = m
                        .indices
                        .iter()
                        .take(m.triangles as usize)
                        .map(|t| t.map(u32::from))
                        .collect();
                    if triangles.iter().flatten().any(|&i| i >= m.vertices)
                        || points.iter().flatten().any(|x| !x.is_finite())
                    {
                        return Ok(3);
                    }
                    add_mesh(&mut self.world, &points, &triangles);
                }
                4 => {
                    let p = r.data.initial;
                    let Some(a) = self.actors.get_mut(id).and_then(Option::as_mut) else {
                        return Ok(3);
                    };
                    a.frozen = false;
                    for k in 0..PARTS {
                        let t = tf(&p.pose[k]);
                        a.agent.doll.set_body(
                            k,
                            t.translation,
                            t.rotation,
                            v(p.linear[k]),
                            v(p.angular[k]),
                        );
                    }
                }
                5 => {
                    if id >= ACTORS || self.actors[id].is_some() {
                        return Ok(3);
                    }
                    self.create(id, Some(&r.data.initial))?;
                }
                6 => {
                    if id >= ACTORS {
                        return Ok(3);
                    }
                    self.actors[id] = None;
                }
                7 => {
                    if self.actors.get(id).and_then(Option::as_ref).is_none() {
                        return Ok(3);
                    }
                }
                8 => {
                    let Some(a) = self.actors.get_mut(id).and_then(Option::as_mut) else {
                        return Ok(3);
                    };
                    a.target = r.data.target;
                }
                9 => {
                    let Some(a) = self.actors.get_mut(id).and_then(Option::as_mut) else {
                        return Ok(3);
                    };
                    a.frozen = false;
                    let was_active = a.active;
                    a.active = r.data.active != 0;
                    a.agent.doll.allow_sleep(!a.active);
                    if !a.active {
                        // A corpse's relaxed muscles keep it turning slightly:
                        // let the solver's own sleep test accept that.
                        a.agent.doll.sleep_tol = [0.15 * 0.15, 0.6 * 0.6, 0.5];
                    }
                    if a.active {
                        a.balance(&self.world, &self.tp);
                    } else {
                        a.recovery.interrupt();
                        if was_active && a.shot.is_some() && a.detached & (1 << 12) == 0 {
                            let context = a.context();
                            let mut shot = a.shot.take().unwrap();
                            shot.started = None;
                            let messages = shot
                                .update(&mut self.rand, &self.tp, &context, self.frame * 1000 / 60)
                                .msgs;
                            a.apply(&self.world, &messages);
                            a.shot = Some(shot);
                            a.last_hit = self.frame;
                        } else {
                            a.agent.stop_all_behaviours();
                            nm::task_relax(&mut a.agent, 0.0, 0.1);
                            a.shot = None;
                        }
                        a.blast = None;
                    }
                }
                10 => {
                    let Some(a) = self.actors.get_mut(id).and_then(Option::as_mut) else {
                        return Ok(3);
                    };
                    a.frozen = false;
                    a.detached = r.data.active;
                    a.agent.doll.removed_parts = a.detached;
                    a.recovery.interrupt();
                    for (bits, mask) in
                        [(0x1e000, "ul"), (0x1e0000, "ur"), (0xe, "ll"), (0x70, "lr")]
                    {
                        if a.detached & bits != 0 {
                            a.send(
                                &self.world,
                                "injury",
                                &[
                                    ("mask", Value::Str(mask.into())),
                                    ("amount", Value::Float(1.0)),
                                ],
                            )?;
                        }
                    }
                }
                11 => {
                    if id >= FRAGMENTS {
                        return Ok(3);
                    }
                    self.fragment(id, &r.data.fragment)?;
                }
                12 => {
                    if id >= FRAGMENTS {
                        return Ok(3);
                    }
                    self.fragments[id] = None;
                }
                13 => {
                    let Some(a) = self.fragments.get_mut(id).and_then(Option::as_mut) else {
                        return Ok(3);
                    };
                    let h = r.data.advance.hits[0];
                    a.frozen = false;
                    a.still = 0;
                    a.last_hit = self.frame;
                    a.agent.doll.impulse(0, v(h.point), v(h.impulse));
                }
                15 => {
                    let Some(a) = self.actors.get_mut(id).and_then(Option::as_mut) else {
                        return Ok(3);
                    };
                    a.gravity = (r.data.active as f32 / 1000.0).clamp(0.0, 1.0);
                    a.agent.doll.wake();
                }
                14 => {
                    let Some(a) = self.actors.get_mut(id).and_then(Option::as_mut) else {
                        return Ok(3);
                    };
                    let bytes = &r.data.bytes;
                    let line = std::str::from_utf8(bytes.split(|&b| b == 0).next().unwrap_or(&[]))
                        .map_err(|e| e.to_string())?;
                    let Ok((name, params)) = parse_line(line) else {
                        return Ok(3);
                    };
                    let refs: Vec<(&str, Value)> = params
                        .iter()
                        .map(|(n, v)| (n.as_str(), v.clone()))
                        .collect();
                    if let Err(e) = a.send(&self.world, &name, &refs) {
                        eprintln!("message rejected: {e}");
                        return Ok(3);
                    }
                }
                _ => return Ok(3),
            }
            Ok(2)
        }
    }
    fn fragment(&mut self, id: usize, f: &Fragment) -> Result<(), String> {
        self.contact_group.forget(ACTORS+id);
        let mut part = self.build.parts[0].clone();
        part.name = "Fragment".into();
        part.parent = None;
        part.mass = f.mass.max(0.01);
        part.radius = v(f.dimensions).min_element().max(0.01) * 0.5;
        part.half = 0.0;
        part.boxed = Some(v(f.dimensions) * 0.5);
        part.anchor = Vec3::ZERO;
        part.anchor_parent = Vec3::ZERO;
        let build = Build {
            parts: vec![part],
            nm: 0,
        };
        let t = tf(&f.pose);
        let mut agent = Agent::new(
            &build,
            Ragdoll::new(&build, &[t], v(f.linear)),
            Quat::IDENTITY,
            self.rand.next() as f32 / 32768.0,
        );
        agent
            .doll
            .set_body(0, t.translation, t.rotation, v(f.linear), v(f.angular));
        agent.body_relax(100.0, 0.1);
        agent.doll.allow_sleep(true);
        self.fragments[id] = Some(Actor {
            agent,
            build,
            active: false,
            target: Target::default(),
            gait: None,
            shot: None,
            blast: None,
            born: self.frame,
            last_hit: 0,
            blast_start: 0,
            detached: 0,
            feedback: [0; 6],
            wander: Default::default(),
            recovery: Default::default(),
            attacking: false,
            airborne: 0.0,
            highfall: false,
            pushes: Vec::new(),
            wound: 0.0,
            gravity: 1.0,
            still: 0,
            frozen: false,

        });
        Ok(())
    }
    fn hit(&mut self, h: &Hit) -> Result<(), String> {
        let Some(a) = self
            .actors
            .get_mut(h.actor as usize)
            .and_then(Option::as_mut)
        else {
            return Err("hit on absent actor".into());
        };
        let p = h.part as usize;
        if p >= PARTS {
            return Err("hit part out of range".into());
        }
        a.frozen = false;
        a.still = 0;
        let kind = h.normal[3] as u32;
        if a.active && a.recovery.rise.is_some() && (kind != 8 || v(h.impulse).length() > 12.0) {
            // Resume dynamics from the observed animated state, retaining every limb's
            // velocity. A fresh task must not inherit the pre-get-up fallen latch.
            let poses: Vec<_> = a.agent.doll.frames().collect();
            let velocities: Vec<_> = (0..PARTS).map(|k| a.agent.doll.vel(k)).collect();
            let facing = poses[0].rotation * self.rest[0].rotation.inverse();
            a.agent = Agent::new(
                &a.build,
                Ragdoll::new(&a.build, &poses, Vec3::ZERO),
                facing,
                self.rand.next() as f32 / 32768.0,
            );
            a.agent.set_rng((
                (self.rand.next() << 16) ^ self.rand.next(),
                (self.rand.next() << 16) ^ self.rand.next(),
            ));
            for k in 0..PARTS {
                a.agent.doll.set_body(
                    k,
                    poses[k].translation,
                    poses[k].rotation,
                    velocities[k].0,
                    velocities[k].1,
                );
            }
            a.restore_injury(&self.world)?;
        }
        let at = if h.point[3] == 1.0 {
            let t = a.agent.doll.frames().nth(p).unwrap();
            t.translation + t.rotation * v(h.point)
        } else {
            v(h.point)
        };
        // Keep each projectile's momentum and duration. Applying the entire
        // 0.1-second weapon push in one solver tick exaggerated limb velocities.
        let duration = h.impulse[3];
        if duration > DT && duration <= 0.3 {
            let t = a.agent.doll.frames().nth(p).unwrap();
            a.pushes.push(TimedPush {
                part: p,
                local: t.rotation.inverse() * (at - t.translation),
                force: v(h.impulse) / duration,
                left: duration,
            });
        } else {
            a.agent.doll.impulse(p, at, v(h.impulse));
        }
        if !a.active {
            return Ok(());
        }
        let now = self.frame * 1000 / 60;
        if kind != 8 || v(h.impulse).length() > 12.0 {
            a.recovery.interrupt();
            a.attacking = false;
            a.highfall = false;
        }
        if kind == MELEE {
            // 8226B618, as the reference viewer's melee dispatcher: DEFAULT
            // Balance when the original RNG draw exceeds .7, otherwise MELEE
            // Flinch. Zombies use the active melee set, not passive civilians.
            a.blast = None;
            a.shot = None;
            let context = a.context();
            let attacker = context.attacker_head.unwrap_or(at - v(h.impulse).normalize_or(Vec3::Y));
            if self.rand.float_in_range(0.0, 1.0) > 0.7 {
                nm::task_balance(&mut a.agent, &a.build, &self.world, &self.tp, "*", Some(attacker), false);
            } else {
                nm::task_flinch(&mut a.agent, &a.build, &self.world, &self.tp, &mut self.rand, "MELEE", attacker);
            }
            a.last_hit = self.frame;
        } else if kind == 9 {
            if a.blast.is_none() || a.blast_start != self.frame {
                a.blast = Some(nm::Explosion::start(
                    &mut a.agent,
                    &a.build,
                    &self.world,
                    &self.tp,
                    &mut self.rand,
                ));
                a.blast_start = self.frame;
                a.shot = None;
            }
        } else if kind != 8 {
            a.blast = None;
            a.wound += if (1..=6).contains(&p) { 2.0 } else { 1.0 };
            let context = a.context();
            let dir = v(h.bullet_velocity);
            let dir = if dir.length() > 0.1 {
                dir
            } else {
                v(h.impulse).normalize_or(Vec3::Y) * 400.0
            };
            let normal = -dir.normalize_or(Vec3::Y);
            let set = match kind {
                4 => "POWERFUL_WEAPON",
                5 => "SNIPER_RIFLE",
                6 | 7 => "SHOTGUN",
                _ => "*",
            };
            let messages = match &mut a.shot {
                Some(task) => task.next_hit(
                    &mut self.rand,
                    &self.tp,
                    &context,
                    now,
                    set,
                    p,
                    at,
                    dir,
                    normal,
                ),
                None => {
                    let mut t = ShotTask::new(&context, set, p, at, dir, normal);
                    let out = t.update(&mut self.rand, &self.tp, &context, now).msgs;
                    a.shot = Some(t);
                    out
                }
            };
            a.apply(&self.world, &messages);
            a.last_hit = self.frame;
        }
        Ok(())
    }
    fn step(&mut self) -> Result<(), String> {
        let started = std::time::Instant::now();
        let result = self.step_inner();
        let ms = started.elapsed().as_secs_f64() * 1000.0;
        self.step_ms += ms;
        self.step_max = self.step_max.max(ms);
        if self.frame % 600 == 0 && self.frame > 0 {
            // One line per 10 game seconds: what the solver carries and its cost.
            let (mut alive, mut dead, mut dead_awake, mut frag, mut frag_awake) = (0, 0, 0, 0, 0);
            for a in self.actors.iter().flatten() {
                if a.active { alive += 1 } else { dead += 1; dead_awake += (a.agent.doll.awake() && !a.frozen) as u32 }
            }
            for a in self.fragments.iter().flatten() {
                frag += 1;
                frag_awake += (a.agent.doll.awake() && !a.frozen) as u32;
            }
            println!(
                "STATS frame={} alive={alive} dead={dead} dead_awake={dead_awake} fragments={frag} fragments_awake={frag_awake} world={} step_avg_ms={:.2} solver_avg_ms={:.2} step_max_ms={:.2}",
                self.frame, self.world.len(), self.step_ms / 600.0, self.group_ms / 600.0, self.step_max
            );
            self.group_ms = 0.0;
            use std::io::Write;
            let _ = std::io::stdout().flush();
            self.step_ms = 0.0;
            self.step_max = 0.0;
        }
        result
    }
    fn step_inner(&mut self) -> Result<(), String> {
        for a in self
            .actors
            .iter_mut()
            .chain(self.fragments.iter_mut())
            .flatten()
        {
            a.agent.frame_forces.clear();
        }
        let now = self.frame * 1000 / 60;
        let corpse_support: Vec<Option<f32>> = self
            .actors
            .iter()
            .map(|a| {
                a.as_ref().and_then(|a| {
                    [3, 6]
                        .iter()
                        .filter_map(|&k| a.agent.doll.hit[k])
                        .filter(|h| {
                            h.2 >= 0
                                && h.1.z > 0.55
                                && h.0.z < a.agent.doll.root().z - 0.3
                                && self
                                    .actors
                                    .get(h.2 as usize)
                                    .and_then(Option::as_ref)
                                    .is_some_and(|other| !other.active)
                        })
                        .map(|h| h.0.z)
                        .max_by(f32::total_cmp)
                })
            })
            .collect();
        if self.frame % 300 == 299 && std::env::var_os("BO1_DELIRIUM_DIAGNOSTICS").is_some() {
            if let Some(a) = &self.actors[0] {
                if let Some(ch) = a.agent.character() {
                    let frames: Vec<_> = a.agent.doll.frames().collect();
                    for j in [0, 1, 6, 7, 8, 9] {
                        let e = &ch.eff[j];
                        eprintln!(
                            "MOTOR f={} j={j} desired={:?} actual={:?} stiffness={} strength={} damping={} injury={}",
                            self.frame,
                            e.desired,
                            ch.angle(j, &frames),
                            e.muscle_stiffness,
                            e.strength,
                            e.damping,
                            e.injury
                        );
                    }
                }
            }
        }
        for (actor_id, slot) in self.actors.iter_mut().chain(self.fragments.iter_mut()).enumerate() {
            let Some(a) = slot else { continue };
            if !a.active {
                // Relaxed NM muscles keep a lying corpse turning at 0.03-0.5
                // rad/s, above the solver's own sleep limit (0.07): corpses
                // never slept and each cost as much as a living zombie
                // (--corpse: 12 corpses 6.2 ms a step, 15 s after death). A
                // dead body still for half a second is put to sleep; a hit or
                // a moving body against it wakes it again.
                let d = &a.agent.doll;
                let quiet = self.frame.wrapping_sub(a.last_hit) > 90
                    && d.vel(0).0.length() < 0.15
                    && d.vel(0).1.length() < 0.6
                    && (0..a.build.parts.len()).all(|k| a.detached & (1 << k) != 0 || d.vel(k).0.length() < 0.3);
                a.still = if quiet { a.still.saturating_add(1) } else { 0 };
                if a.still >= 30 {
                    // Retire the dead character's residual muscle and upright
                    // drives. Keep the passive body in the shared contact group
                    // so a new impact can wake it with its actual mass.
                    if a.still == 30 {
                        a.agent.stop_all_behaviours();
                        a.agent.set_stiffness(0.0, 0.0);
                        a.agent.stay_upright(false, 0.0, 0.0);
                    }
                    a.agent.doll.rest_passive();
                    a.frozen = !a.agent.doll.awake();
                }
                if let Some(mut shot) = a.shot.take() {
                    if self.frame - a.last_hit < 180 {
                        let context = a.context();
                        let update = shot.update(&mut self.rand, &self.tp, &context, now);
                        a.apply(&self.world, &update.msgs);
                        if !update.done {
                            a.shot = Some(shot);
                        }
                    } else {
                        a.agent.stop_all_behaviours();
                        nm::task_relax(&mut a.agent, 0.0, 0.1);
                    }
                }
                continue;
            }
            let supported = a.agent.doll.touch.iter().any(|t| t & 0x28 == 0x20);
            a.airborne = if supported { 0.0 } else { a.airborne + DT };
            if a.airborne > 0.2
                && !a.highfall
                && a.blast.is_none()
                && a.agent.doll.vel(0).0.z < -2.0
                && self.world.ground(a.agent.doll.root(), 1.5).is_none()
            {
                nm::task_high_fall(&mut a.agent, &a.build, &self.world, &self.tp, "*", false);
                a.highfall = true;
                a.shot = None;
            }
            a.wound = (a.wound - 0.1 * DT).max(0.0);
            a.recovery.hurt = a.wound >= 3.0;
            let rising = a.recovery.rise.is_some();
            let completed = a.recovery.completed;
            let eligible = self.frame - a.born > 90
                && self.frame - a.last_hit > 60
                && a.blast.is_none()
                && a.detached & 0x7e == 0;
            if a.recovery.step(
                &mut a.agent,
                &a.build,
                &self.world,
                &self.motions,
                &self.rest,
                &mut self.rand,
                eligible,
                corpse_support[actor_id],
            ) {
                if a.recovery.completed > completed {
                    let poses: Vec<_> = a.agent.doll.frames().collect();
                    let velocities: Vec<_> = (0..PARTS).map(|k| a.agent.doll.vel(k)).collect();
                    let rng = (
                        (self.rand.next() << 16) ^ self.rand.next(),
                        (self.rand.next() << 16) ^ self.rand.next(),
                    );
                    // A fall/get-up may change heading. Keeping the pre-fall facing here
                    // bakes that heading into pelvis_up and gives the fresh balancer a
                    // different neutral frame from an otherwise identical newly born actor.
                    let facing = poses[0].rotation * self.rest[0].rotation.inverse();
                    a.agent = Agent::new(
                        &a.build,
                        Ragdoll::new(&a.build, &poses, Vec3::ZERO),
                        facing,
                        self.rand.next() as f32 / 32768.0,
                    );
                    a.agent.set_rng(rng);
                    for k in 0..PARTS {
                        a.agent.doll.set_body(k, poses[k].translation, poses[k].rotation, velocities[k].0, velocities[k].1);
                    }
                    a.agent.doll.removed_parts = a.detached;
                    a.attacking = false;
                    a.highfall = false;
                    a.balance(&self.world, &self.tp);
                    a.restore_injury(&self.world)?;
                } else {
                    a.balance(&self.world, &self.tp);
                }
            }
            if a.recovery.rise.is_some() {
                if !rising {
                    a.shot = None;
                    a.highfall = false;
                    a.attacking = false;
                }
                continue;
            }
            // Every task retains its own clock, RNG draws and state between bullets.
            if let Some(mut shot) = a.shot.take() {
                let hit = a.context();
                let update = shot.update(&mut self.rand, &self.tp, &hit, now);
                a.apply(&self.world, &update.msgs);
                if !update.done {
                    a.shot = Some(shot);
                }
            }
            if let Some(mut blast) = a.blast.take() {
                let vz = a.agent.doll.vel(0).0.z;
                let near = self.world.ground(a.agent.doll.root(), 2.0).is_some();
                blast.control(
                    &mut a.agent,
                    &a.build,
                    &self.world,
                    &mut self.rand,
                    DT,
                    vz,
                    near,
                );
                let supported = a.agent.doll.touch.iter().any(|x| x & 0x28 == 0x20);
                let age = (self.frame - a.blast_start) as f32 * DT;
                if !(supported
                    && ((age > 2.0 && a.agent.doll.com().1.length() < 0.2)
                        || (age > 10.0 && a.agent.doll.com().1.length() < 2.0)))
                {
                    a.blast = Some(blast);
                } else {
                    a.balance(&self.world, &self.tp);
                }
            }
            // Start by supplying the BO1 leg cycle as a partial motor target.
            // No transform/velocity is imposed: the native balance controller,
            // support contacts and joint limits still decide the actual pose.
            a.agent.set_locomotion_drive(None).map_err(str::to_owned)?;
            if let Some(reference) = a.gait.filter(|_| a.shot.is_none() && a.blast.is_none()
                && !a.agent.fallen && a.agent.balancing() && a.detached & 0x7e == 0) {
                let mut poses: Vec<_> = a.agent.doll.frames().collect();
                let mut valid = true;
                for (pose, packed) in poses.iter_mut().zip(reference.rotation) {
                    let q = Quat::from_array(packed.map(|v| v as f32 / 32767.0));
                    if !q.is_finite() || q.length_squared() < 0.9 { valid = false; break; }
                    pose.rotation = q.normalize();
                }
                if valid {
                    let rows = delirium::nmrs::incoming_rows(a.build.nm, &poses);
                    let weight = reference.weight as f32 / 65535.0;
                    if let Some(ch) = a.agent.character() {
                        let targets = std::array::from_fn(|i| {
                            let target = ch.incoming_angle(i, &rows);
                            std::array::from_fn(|axis| ch.eff[i].desired[axis] + (target[axis]-ch.eff[i].desired[axis])*weight)
                        });
                        let drive = nm::LocomotionDrive { targets,
                            stance: [reference.stance & 1 != 0, reference.stance & 2 != 0],
                            weight, gain_weight: 0.0,
                            leg_stiffness: [delirium_data::kv!(25); 2], response: [1.0; 2] };
                        a.agent.set_locomotion_drive(Some(drive)).map_err(str::to_owned)?;
                    }
                }
            }
            if a.target.enabled != 0 && a.blast.is_none() {
                let pos = a.agent.doll.root();
                let delta = (v(a.target.position) - pos).with_z(0.0);
                let speed = a.agent.doll.com().1.with_z(0.0).length();

                if delta.length() > 0.6 && a.agent.balancing() {
                    let dir = delta.normalize();
                    let desired = a.target.position[3].clamp(0.0, 6.0);
                    let stick = ((desired - speed).clamp(0.0, 2.5) / 2.5 * 127.0) as i32;
                    if stick > 0 {
                        let m = delirium::playerctl::control(
                            &mut a.agent,
                            (0, -stick),
                            (-dir.x).atan2(dir.y),
                            pos,
                            &mut a.wander,
                            &mut self.rand,
                        );
                        a.apply(&self.world, &m);
                    }
                }
            }
            let attack = a.target.reach > 0.05
                && a.agent.balancing()
                && self.frame - a.last_hit > 90
                && a.detached & ((1 << 16) | (1 << 20)) != ((1 << 16) | (1 << 20));
            if attack {
                let mut params = vec![
                    ("target", Value::Vec3(v(a.target.reach_position))),
                    ("useLeftArm", Value::Bool(a.detached & (1 << 20) != 0)),
                    ("armStiffness", Value::Float(7.0)),
                    (
                        "armStraightness",
                        Value::Float(a.target.reach.clamp(0.0, 0.9)),
                    ),
                ];
                if !a.attacking {
                    params.push(("start", Value::Bool(true)));
                }
                a.send(&self.world, "pointArm", &params)?;
                a.attacking = true;
            } else if a.attacking {
                a.send(&self.world, "pointArm", &[("start", Value::Bool(false))])?;
                a.attacking = false;
            }
            for command in std::mem::take(&mut a.agent.extras.outbox) {
                if command.starts_with('#') {
                    continue;
                }
                let (name, params) = parse_line(&command)?;
                let refs: Vec<(&str, Value)> = params
                    .iter()
                    .map(|(n, v)| (n.as_str(), v.clone()))
                    .collect();
                a.send(&self.world, &name, &refs)?;
            }
        }
        // Recovery references drive joint motors. The same world, self and
        // neighbour contacts must run during recovery as during a fall.
        for a in self.actors.iter_mut().flatten() {
            for push in &mut a.pushes {
                a.agent.doll.wake();
                let t = a.agent.doll.frames().nth(push.part).unwrap();
                let fraction = push.left.min(DT) / DT;
                a.agent.frame_forces.push((
                    push.part,
                    t.translation + t.rotation * push.local,
                    push.force * fraction,
                ));
                push.left -= DT;
            }
            a.pushes.retain(|push| push.left > 0.000001);
            // Low gravity as a constant lift on every part: the solver's own
            // gravity stays Earth's for all the other bodies.
            if a.gravity < 0.999 && a.agent.doll.awake() {
                for k in 0..PARTS {
                    if a.detached & (1 << k) == 0 {
                        let lift = Vec3::Z * a.agent.doll.part_mass(k) * delirium::ragdoll::GRAVITY() * (1.0 - a.gravity);
                        a.agent.frame_forces.push((k, a.agent.doll.centre(k), lift));
                    }
                }
            }
        }
        // Shared Delirium solve: the two articulated masses participate in the
        // same contact rows. The native manifold also supplies support, rather
        // than inferring it from proxy capsules after independent solves.
        let solver = std::time::Instant::now();
        let mut members: Vec<_> = self.actors.iter_mut().enumerate()
            .chain(self.fragments.iter_mut().enumerate().map(|(i,a)|(ACTORS+i,a)))
            .filter_map(|(id,a)| a.as_mut().map(|a| (&mut a.agent,id,&a.build,&[][..],&[][..])))
            .collect();
        Agent::step_group(&mut members, &self.world, DT, &mut self.contact_group);
        drop(members);
        self.body_contacts = self.contact_group.contacts().collect();
        for a in self.actors.iter_mut().chain(self.fragments.iter_mut()).flatten() {
            // A contact wake starts a fresh rest window. Otherwise an old
            // quiet counter would cancel a small but real push next frame.
            if a.frozen && a.agent.doll.awake() { a.still = 0; }
            a.frozen = !a.active && !a.agent.doll.awake();
        }
        self.group_ms += solver.elapsed().as_secs_f64() * 1000.0;
        for a in self.actors.iter_mut().flatten() {
            if a.recovery.rise.is_some() {
                a.agent.feedback.clear();
                continue;
            }
            let feedback = a.agent.feedback.clone();
            for f in feedback {
                let slot = f.slot as usize / 4;
                if slot < 6 {
                    a.feedback[slot] += 1;
                }
                if f.name == "balance" && f.slot == 8 && a.active {
                    nm::task_failed(&mut a.agent, &self.world, nm::Task::Shot);
                    let dir = if a.agent.doll.com().1.with_z(0.0).length() > 0.2 {
                        a.agent.doll.com().1
                    } else {
                        v(a.target.position) - a.agent.doll.root()
                    };
                    if descending_steps(&self.world, a.agent.doll.root(), dir) {
                        if std::env::var_os("BO1_DELIRIUM_STAIRS_DEBUG").is_some() {
                            eprintln!("STAIRSDBG rollDownStairs");
                        }
                        a.agent
                            .roll_down_stairs(a.agent.msgs.roll_down_stairs.clone());
                    }
                }
            }
            if a.agent
                .doll
                .frames()
                .any(|t| !t.translation.is_finite() || !t.rotation.is_finite())
            {
                return Err("non-finite Delirium body".into());
            }
        }
        self.frame = self.frame.wrapping_add(1);
        Ok(())
    }
    pub fn export(&self, x: &mut Exchange, focus: usize) -> Result<(), String> {
        x.response = unsafe { std::mem::zeroed() };
        x.response.status = 2;
        x.response.frame = self.frame;
        x.response.parts = PARTS as u32;
        x.rig = unsafe { std::mem::zeroed() };
        for (k, p) in self.build.parts.iter().enumerate() {
            let t = self.rest[k];
            x.rig.parent[k] = p.parent.map_or(-1, |i| i as i32);
            let pivot = t.translation + t.rotation * p.anchor;
            x.rig.anchor[k] = [pivot.x, pivot.y, pivot.z, 0.0];
            x.rig.shapes[k] = Shape {
                orientation: self.bounds[k].to_array(),
                offset: [0.0; 4],
                size: p
                    .boxed
                    .map_or([0.0; 4], |s| [2.0 * s.x, 2.0 * s.y, 2.0 * s.z, 0.0]),
                length: p.half * 2.0,
                radius: p.radius,
                boxed: p.boxed.is_some() as u32,
                mass: delirium::nmrs::MASSES[self.build.nm]
                    .get()
                    .and_then(|m| m.get(k))
                    .copied()
                    .unwrap_or(p.mass),
            };
        }
        for i in 0..ACTORS {
            x.actors[i] = unsafe { std::mem::zeroed() };
            let Some(a) = &self.actors[i] else {
                continue;
            };
            let s = &mut x.actors[i];
            s.present = 1;
            s.active_controllers = if a.active {
                0x80000001 + a.shot.is_some() as u32 + a.blast.is_some() as u32
            } else {
                0
            };
            s.recovery_active = a.recovery.rise.is_some() as u32;
            s.recovery_completed = a.recovery.completed;
            s.recovery_interrupted = a.recovery.interrupted;
            s.recovery_time = a.recovery.time();
            s.feedback = a.feedback;
            s.detached_mask = a.detached;
            for (out, t) in s.poses.iter_mut().zip(a.agent.doll.frames()) {
                *out = matrix(t);
            }
            let (p, vel) = a.agent.doll.com();
            s.com = [p.x, p.y, p.z, 0.0];
            s.velocity = [vel.x, vel.y, vel.z, 0.0];
            for (k, t) in a.agent.doll.touch.iter().enumerate() {
                if t & 0x28 == 0x20 {
                    s.support_mask |= 1 << k;
                }
            }
            s.support_mask &= !a.detached;
            s.foot_speed = [3, 6]
                .iter()
                .map(|&k| a.agent.doll.vel(k).0.length())
                .fold(0.0, f32::max);
            s.trunk_angular_speed = a.agent.doll.vel(0).1.length();
        }
        for &(a, pa, b, pb, n) in &self.body_contacts {
            if a < ACTORS && b < ACTORS && x.actors[a].present != 0 && x.actors[b].present != 0 {
                x.actors[a].support_mask |= BODY_CONTACT;
                x.actors[b].support_mask |= BODY_CONTACT;
                let carries_a = body_carries_weight(pa, &x.actors[a]);
                let carries_b = body_carries_weight(pb, &x.actors[b]);
                if n.z > 0.55 && carries_a {
                    x.actors[a].support_mask |= BODY_SUPPORT | (1 << pa);
                    if self.actors[a].as_ref().is_some_and(|a| a.active) {
                        x.actors[b].support_mask |= 1 << 29;
                    }
                }
                if n.z < -0.55 && carries_b {
                    x.actors[b].support_mask |= BODY_SUPPORT | (1 << pb);
                    if self.actors[b].as_ref().is_some_and(|a| a.active) {
                        x.actors[a].support_mask |= 1 << 29;
                    }
                }
            }
        }
        let selected = if focus < ACTORS && self.actors[focus].is_some() {
            focus
        } else {
            0
        };
        if self.actors[selected].is_some() {
            let a = &x.actors[selected];
            x.response.poses = a.poses;
            x.response.active_controllers = a.active_controllers;
            x.response.com = a.com;
            x.response.velocity = a.velocity;
            x.response.feedback = a.feedback;
            x.response.recovery_active = a.recovery_active;
            x.response.recovery_completed = a.recovery_completed;
            x.response.recovery_interrupted = a.recovery_interrupted;
            x.response.recovery_time = a.recovery_time;
        } else {
            for (m, t) in x.response.poses.iter_mut().zip(&self.rest) {
                *m = matrix(*t);
            }
        }
        x.response.active_controllers |= 0x80000000;
        if std::env::var_os("BO1_DELIRIUM_CONTINUOUS").is_some_and(|v| v == "1") {
            x.response.active_controllers |= 0x40000000;
        }
        for i in 0..FRAGMENTS {
            x.fragments[i] = unsafe { std::mem::zeroed() };
            if let Some(a) = &self.fragments[i] {
                x.fragments[i].present = 1;
                x.fragments[i].pose = matrix(a.agent.doll.frames().next().unwrap());
            }
        }
        Ok(())
    }
}
pub fn selftest() -> Result<(), String> {
    let mut r = Runtime::load(true)?;
    r.actors[0].as_mut().unwrap().active = true;
    r.actors[0].as_mut().unwrap().balance(&r.world, &r.tp);
    for _ in 0..180 {
        r.step()?;
    }
    println!(
        "SELFTEST pelvis={:?} balancing={} mass={}",
        r.actors[0].as_ref().unwrap().agent.doll.root(),
        r.actors[0].as_ref().unwrap().agent.balancing(),
        r.actors[0].as_ref().unwrap().agent.doll.mass()
    );
    Ok(())
}
pub fn recover_test() -> Result<(), String> {
    let mut r = Runtime::load(true)?;
    let a = r.actors[0].as_mut().unwrap();
    let yaw = Quat::from_rotation_x(1.5);
    let root = a.agent.doll.root();
    let poses: Vec<_> = a.agent.doll.frames().collect();
    let rows = delirium::nmrs::incoming_rows(a.build.nm, &poses);
    let ch = a.agent.character().unwrap();
    for j in 0..20 {
        println!(
            "AXIS {j} actual={:?} incoming={:?}",
            ch.angle(j, &poses),
            ch.incoming_angle(j, &rows)
        );
    }
    for (k, t) in poses.iter().enumerate() {
        a.agent.doll.set_body(
            k,
            Vec3::new(0.0, 0.0, 0.4) + yaw * (t.translation - root),
            yaw * t.rotation,
            Vec3::ZERO,
            Vec3::ZERO,
        );
    }
    a.active = true;
    a.balance(&r.world, &r.tp);
    let (mut tracking_samples, mut root_error) = (0u32, 0.0f32);
    let (mut knee_low, mut knee_high) = ([f32::INFINITY; 2], [f32::NEG_INFINITY; 2]);
    let (mut target_low, mut target_high) = ([f32::INFINITY; 2], [f32::NEG_INFINITY; 2]);
    for f in 0..1500 {
        r.step()?;
        let body = r.actors[0].as_ref().unwrap();
        if body.recovery.rise.as_ref().is_some_and(|rise| rise.age > 0.25) {
            let frames: Vec<_> = body.agent.doll.frames().collect();
            let target = body.agent.incoming[0].rotation;
            root_error += 2.0 * target.dot(frames[0].rotation).abs().clamp(0.0,1.0).acos();
            tracking_samples += 1;
            for (side, joint) in [1,4].into_iter().enumerate() {
                let bend = body.agent.character().unwrap().angle(joint, &frames)[0];
                knee_low[side] = knee_low[side].min(bend);
                knee_high[side] = knee_high[side].max(bend);
                let target = body.agent.character().unwrap().incoming_angle(joint,
                    &delirium::nmrs::incoming_rows(body.build.nm, &body.agent.incoming))[0];
                target_low[side] = target_low[side].min(target);
                target_high[side] = target_high[side].max(target);
            }
        }
        if f % 30 == 29 {
            let a = r.actors[0].as_ref().unwrap();
            let p: Vec<_> = a.agent.doll.frames().collect();
            println!(
                "RISE f={f} root={:?} head_z={:.3} feet_z={:.3},{:.3} contact={:x?} balance={} rising={} time={:.3} done={} aborted={}",
                a.agent.doll.root(),
                p[12].translation.z,
                p[3].translation.z,
                p[6].translation.z,
                a.agent.doll.touch,
                a.agent.balancing(),
                a.recovery.rise.is_some(),
                a.recovery.time(),
                a.recovery.completed,
                a.recovery.interrupted
            );
        }
    }
    let a = r.actors[0].as_ref().unwrap();
    let error = root_error / tracking_samples.max(1) as f32;
    let excursion = [knee_high[0]-knee_low[0],knee_high[1]-knee_low[1]];
    println!("RECOVERY MOTION samples={tracking_samples} mean_root_error={error:.4}rad knee_excursion={excursion:?}");
    println!("RECOVERY KNEES actual={knee_low:?}..{knee_high:?} target={target_low:?}..{target_high:?} limits={:?}/{:?}", a.agent.character().unwrap().limits(1),a.agent.character().unwrap().limits(4));
    if tracking_samples < 30 || error > 0.6 || excursion.iter().any(|&x| !x.is_finite() || x < 0.3) {
        return Err("recovery must track the root turn and articulate both knees, not just lift a rigid pose".into());
    }
    if a.recovery.completed == 0 {
        return Err("muscle get-up failed".into());
    }
    Ok(())
}
// Twelve zombies knocked down at once (face up and face down): every one must
// get up and keep balance, and the crowd must not rise with one clip and rate.
pub fn variety_test() -> Result<(), String> {
    let mut r = Runtime::load(true)?;
    for i in 0..12 {
        if i != 0 {
            r.create(i, None)?;
        }
        let a = r.actors[i].as_mut().unwrap();
        let tilt = Quat::from_rotation_z(i as f32 * 0.5)
            * Quat::from_rotation_x(if i % 2 == 0 { 1.5 } else { -1.5 });
        let root = a.agent.doll.root();
        let poses: Vec<_> = a.agent.doll.frames().collect();
        let at = Vec3::new((i % 4) as f32 * 2.5, (i / 4) as f32 * 2.5, 0.4);
        for (k, t) in poses.iter().enumerate() {
            a.agent.doll.set_body(k, at + tilt * (t.translation - root), tilt * t.rotation, Vec3::ZERO, Vec3::ZERO);
        }
        a.wound = if i % 3 == 0 { 4.0 } else { 0.0 };
        a.active = true;
        a.balance(&r.world, &r.tp);
    }
    let mut first: Vec<Option<(usize, crate::recovery::Variant, bool, f32, u32)>> = vec![None; 12];
    for f in 0..1800u32 {
        r.step()?;
        for i in 0..12 {
            let a = r.actors[i].as_ref().unwrap();
            if first[i].is_none() {
                if let Some((c, v, b, rate)) = a.recovery.current(&r.motions) {
                    first[i] = Some((c, v, b, rate, f));
                }
            }
        }
    }
    let mut pass = 0;
    for i in 0..12 {
        let a = r.actors[i].as_ref().unwrap();
        let p: Vec<_> = a.agent.doll.frames().collect();
        let height = p[12].translation.z - (p[3].translation.z + p[6].translation.z) * 0.5;
        let ok = a.recovery.completed > 0 && a.agent.balancing() && height > 1.25;
        println!("VARIETY i={i} first={:?} completed={} balance={} height={height:.3}", first[i], a.recovery.completed, a.agent.balancing());
        pass += ok as usize;
    }
    let mut clips: Vec<usize> = first.iter().flatten().map(|x| x.0).collect();
    clips.sort();
    clips.dedup();
    let mut starts: Vec<u32> = first.iter().flatten().map(|x| x.4).collect();
    starts.sort();
    starts.dedup();
    let rates_differ = first.iter().flatten().map(|x| (x.3 * 1000.0) as i32).collect::<std::collections::BTreeSet<_>>().len() > 6;
    println!("VARIETY recovered={pass}/12 distinct_clips={} distinct_start_frames={} rates_differ={rates_differ}", clips.len(), starts.len());
    if pass != 12 || clips.len() < 3 || starts.len() < 6 || !rates_differ {
        return Err("get-up variety/stability failed".into());
    }
    Ok(())
}
// Pose dump for offline inspection: a standing chaser shot in the chest and
// the arm, then left alone. Writes every part's centre, rotation and capsule
// half axis per frame to the file given as `path`.
pub fn dump_test(path: &str, rest_only: bool) -> Result<(), String> {
    use std::io::Write;
    let mut r = Runtime::load(true)?;
    let mut out = std::fs::File::create(path).map_err(|e| e.to_string())?;
    {
        let a = r.actors[0].as_mut().unwrap();
        a.active = true;
        a.balance(&r.world, &r.tp);
        a.target = Target { enabled: 1, position: [0.0, 6.0, 1.0, 1.5], ..Default::default() };
    }
    let write = |out: &mut std::fs::File, f: u32, a: &Actor, frames: &[Transform]| {
        for (k, t) in frames.iter().enumerate() {
            let p = &a.build.parts[k];
            let axis = if let Some(h) = p.boxed {
                let i = if h.x >= h.y && h.x >= h.z { 0 } else if h.y >= h.z { 1 } else { 2 };
                let mut v = Vec3::ZERO;
                v[i] = h[i];
                t.rotation * v
            } else {
                t.rotation * Vec3::Y * (p.half + p.radius)
            };
            // Third triple: this part's joint with its parent, as the part holds it.
            let x = t.rotation * p.anchor;
            let _ = writeln!(out, "{f},{k},{},{},{},{},{},{},{},{},{},{}", t.translation.x, t.translation.y, t.translation.z, axis.x, axis.y, axis.z, x.x, x.y, x.z, p.parent.map_or(-1, |q| q as i32));
        }
    };
    if rest_only {
        let a = r.actors[0].as_ref().unwrap();
        write(&mut out, 0, a, &r.rest);
        return Ok(());
    }
    for f in 0..360u32 {
        if f == 60 || f == 75 || f == 90 {
            let a = r.actors[0].as_ref().unwrap();
            let part = if f == 75 { 15 } else { 10 };
            let at = a.agent.doll.frames().nth(part).unwrap().translation;
            r.hit(&Hit { part: part as u32, actor: 0, point: [at.x, at.y, at.z, 0.0], impulse: [0.0, -8.0, 0.0, 0.1], bullet_velocity: [0.0, -450.0, 0.0, 0.0], normal: [0.0, 0.0, 0.0, 2.0] })?;
        }
        r.step()?;
        let a = r.actors[0].as_ref().unwrap();
        let frames: Vec<Transform> = a.agent.doll.frames().collect();
        write(&mut out, f, a, &frames);
    }
    Ok(())
}
// A knife shove (kind MELEE, 30 N s like the Hero's push) on standing zombies
// facing the player: each must be driven back, stay articulated, and end up
// standing again (balanced or after a get-up).
pub fn melee_test() -> Result<(), String> {
    let mut r = Runtime::load(true)?;
    let n = 6;
    for i in 0..n {
        if i != 0 {
            r.create(i, None)?;
        }
        let a = r.actors[i].as_mut().unwrap();
        let poses: Vec<_> = a.agent.doll.frames().collect();
        for (k, t) in poses.iter().enumerate() {
            a.agent.doll.set_body(k, t.translation + Vec3::new(i as f32 * 3.0, 0.0, 0.0), t.rotation, Vec3::ZERO, Vec3::ZERO);
        }
        a.active = true;
        a.balance(&r.world, &r.tp);
    }
    for _ in 0..90 {
        r.step()?;
    }
    let mut start = vec![Vec3::ZERO; n];
    let mut push_dir = vec![Vec3::ZERO; n];
    for i in 0..n {
        let a = r.actors[i].as_mut().unwrap();
        let root = a.agent.doll.root();
        // The player stands 1 m away, on a different side for each zombie.
        let side = Quat::from_rotation_z(i as f32 * 1.047) * Vec3::Y;
        let player = root.with_z(0.0) + side;
        a.target = Target { enabled: 1, position: [player.x, player.y, 1.0, 0.0], ..Default::default() };
        let chest = a.agent.doll.frames().nth(10).unwrap().translation;
        let j = -side * (75.0 * 1.5);
        start[i] = root;
        push_dir[i] = -side;
        r.hit(&Hit { part: 10, actor: i as u32, point: [chest.x, chest.y, chest.z, 0.0], impulse: [j.x, j.y, j.z, 0.15], bullet_velocity: [0.0; 4], normal: [0.0, 0.0, 0.0, MELEE as f32] })?;
    }
    let mut moved = vec![0.0f32; n];
    let mut max_joint = 0.0f32;
    for f in 0..600 {
        r.step()?;
        for i in 0..n {
            let a = r.actors[i].as_ref().unwrap();
            if f == 60 {
                moved[i] = (a.agent.doll.root() - start[i]).dot(push_dir[i]);
            }
            let poses: Vec<_> = a.agent.doll.frames().collect();
            for (k, p) in a.build.parts.iter().enumerate().skip(1) {
                if let Some(parent) = p.parent {
                    let left = poses[parent].translation + poses[parent].rotation * p.anchor_parent;
                    let right = poses[k].translation + poses[k].rotation * p.anchor;
                    max_joint = max_joint.max(left.distance(right));
                }
            }
        }
    }
    let mut pass = 0;
    for i in 0..n {
        let a = r.actors[i].as_ref().unwrap();
        let p: Vec<_> = a.agent.doll.frames().collect();
        let height = p[12].translation.z - (p[3].translation.z + p[6].translation.z) * 0.5;
        let standing = height > 1.25 && a.agent.balancing();
        println!("MELEE i={i} pushed_back_1s={:.2}m recovered={} standing={standing} height={height:.2}", moved[i], a.recovery.completed);
        pass += (moved[i] > 0.25 && standing) as usize;
    }
    println!("MELEE pass={pass}/{n} max_joint={max_joint:.5}");
    if pass != n || max_joint > 0.12 {
        return Err("melee shove failed".into());
    }
    Ok(())
}
// Solver cost of a crowd: `n` zombies on a grid `spacing` metres apart, all
// pressing towards the same player. Prints the mean step time per second.
pub fn crowd_test(n: usize, spacing: f32) -> Result<(), String> {
    let mut r = Runtime::load(true)?;
    let side = (n as f32).sqrt().ceil() as usize;
    for i in 0..n {
        if i != 0 {
            r.create(i, None)?;
        }
        let a = r.actors[i].as_mut().unwrap();
        let poses: Vec<_> = a.agent.doll.frames().collect();
        let at = Vec3::new((i % side) as f32 * spacing, (i / side) as f32 * spacing, 0.0);
        for (k, t) in poses.iter().enumerate() {
            a.agent.doll.set_body(k, t.translation + at, t.rotation, Vec3::ZERO, Vec3::ZERO);
        }
        a.active = true;
        a.balance(&r.world, &r.tp);
        let c = (side as f32 - 1.0) * spacing * 0.5;
        a.target = Target { enabled: 1, position: [c, c + 0.5, 1.0, 1.5], ..Default::default() };
    }
    let mut worst = 0.0f64;
    for second in 0..10 {
        let t = std::time::Instant::now();
        for _ in 0..60 {
            r.step()?;
        }
        let ms = t.elapsed().as_secs_f64() * 1000.0 / 60.0;
        worst = worst.max(ms);
        let touching = r.body_contacts.len();
        println!("CROWD n={n} spacing={spacing} second={second} step_ms={ms:.2} body_contacts={touching}");
    }
    println!("CROWD n={n} spacing={spacing} worst_step_ms={worst:.2}");
    Ok(())
}
// Moon gravity (command 15): a limp body dropped from 3 m lands later under
// 136/800 of Earth's gravity than under Earth's, and a living zombie on the
// moon still stands, takes a shot and stands again.
pub fn moon_test() -> Result<(), String> {
    let mut r = Runtime::load(true)?;
    for i in 0..3 {
        if i != 0 {
            r.create(i, None)?;
        }
        let a = r.actors[i].as_mut().unwrap();
        let poses: Vec<_> = a.agent.doll.frames().collect();
        let lift = if i < 2 { 3.0 } else { 0.0 };
        for (k, t) in poses.iter().enumerate() {
            a.agent.doll.set_body(k, t.translation + Vec3::new(i as f32 * 4.0, 0.0, lift), t.rotation, Vec3::ZERO, Vec3::ZERO);
        }
        a.gravity = if i == 0 { 1.0 } else { 136.0 / 800.0 };
        a.active = i == 2;
        a.agent.doll.allow_sleep(i != 2);
        if i == 2 {
            a.balance(&r.world, &r.tp);
        } else {
            a.agent.stop_all_behaviours();
            nm::task_relax(&mut a.agent, 0.0, 0.1);
        }
    }
    let mut landed = [None::<u32>; 2];
    for f in 0..900u32 {
        if f == 240 {
            let a = r.actors[2].as_ref().unwrap();
            let at = a.agent.doll.frames().nth(10).unwrap().translation;
            r.hit(&Hit { part: 10, actor: 2, point: [at.x, at.y, at.z, 0.0], impulse: [0.0, -8.0, 0.0, 0.1], bullet_velocity: [0.0, -450.0, 0.0, 0.0], normal: [0.0, 0.0, 0.0, 2.0] })?;
        }
        r.step()?;
        for i in 0..2 {
            let z = r.actors[i].as_ref().unwrap().agent.doll.root().z;
            if landed[i].is_none() && z < 0.35 {
                landed[i] = Some(f);
            }
        }
    }
    let a = r.actors[2].as_ref().unwrap();
    let p: Vec<_> = a.agent.doll.frames().collect();
    let height = p[12].translation.z - (p[3].translation.z + p[6].translation.z) * 0.5;
    let standing = height > 1.25 && a.agent.balancing();
    let (earth, moon) = (landed[0].map(|f| f as f32 / 60.0), landed[1].map(|f| f as f32 / 60.0));
    println!("MOON earth_fall_s={earth:?} moon_fall_s={moon:?} moon_zombie_standing={standing} height={height:.2}");
    match (earth, moon) {
        (Some(e), Some(m)) if m > e * 1.8 && standing => Ok(()),
        _ => Err("moon gravity test failed".into()),
    }
}
// A grenade in a horde: `n` zombies enter physics in the same frame (as BO1
// hands them over) and every part takes the blast push (kind 9, 0.08 s), as
// CG_EuphoriaRd_Explosion sends it. Prints creation cost and step costs.
pub fn blast_crowd_test(n: usize) -> Result<(), String> {
    let mut r = Runtime::load(true)?;
    r.actors[0] = None;
    let side = (n as f32).sqrt().ceil() as usize;
    let mut initial: Initial = unsafe { std::mem::zeroed() };
    let created = std::time::Instant::now();
    for i in 0..n {
        let at = Vec3::new((i % side) as f32 * 0.7, (i / side) as f32 * 0.7, 0.0);
        for k in 0..PARTS {
            let t = r.rest[k];
            initial.pose[k] = matrix(Transform::from_translation(t.translation + at).with_rotation(t.rotation));
        }
        r.create(i, Some(&initial))?;
        let a = r.actors[i].as_mut().unwrap();
        a.active = true;
        a.agent.doll.allow_sleep(false);
        a.balance(&r.world, &r.tp);
    }
    let create_ms = created.elapsed().as_secs_f64() * 1000.0;
    let centre = Vec3::new((side as f32 - 1.0) * 0.35, (side as f32 - 1.0) * 0.35, 0.0);
    let hits = std::time::Instant::now();
    for i in 0..n {
        let a = r.actors[i].as_ref().unwrap();
        let root = a.agent.doll.root();
        let masses: Vec<f32> = (0..PARTS).map(|k| a.agent.doll.part_mass(k)).collect();
        let dv = ((root - centre).with_z(0.0).normalize_or(Vec3::X) + Vec3::Z * 0.6).normalize() * 6.0;
        for k in 0..PARTS {
            let m = masses[k];
            let j = (dv * m).clamp_length_max(127.0);
            r.hit(&Hit { part: k as u32, actor: i as u32, point: [0.0, 0.0, 0.0, 1.0], impulse: [j.x, j.y, j.z, 0.08], bullet_velocity: [0.0; 4], normal: [0.0, 0.0, 0.0, 9.0] })?;
        }
    }
    let hit_ms = hits.elapsed().as_secs_f64() * 1000.0;
    println!("BLAST n={n} create_ms={create_ms:.1} hits_ms={hit_ms:.1}");
    let mut worst = 0.0f64;
    for second in 0..6 {
        let mut total = 0.0f64;
        for _ in 0..60 {
            let t = std::time::Instant::now();
            r.step()?;
            let ms = t.elapsed().as_secs_f64() * 1000.0;
            total += ms;
            worst = worst.max(ms);
        }
        let up = r.actors.iter().flatten().filter(|a| a.agent.balancing()).count();
        let speed = r.actors.iter().flatten().map(|a| a.agent.doll.com().1.length()).fold(0.0f32, f32::max);
        println!("BLAST n={n} second={second} step_avg_ms={:.2} balancing={up} fastest_body_mps={speed:.1}", total / 60.0);
    }
    println!("BLAST n={n} worst_step_ms={worst:.2}");
    Ok(())
}
// Frontal shots: zombies facing the shooter (who stands 3 m in front) take
// one pistol bullet travelling straight at them. After 0.25 s and 0.6 s:
// how far the pelvis went back (along the bullet) and sideways, and how much
// the body turned. Repeated per target over several seeds.
fn facing_of(a: &Actor) -> Vec3 {
    let f: Vec<Transform> = a.agent.doll.frames().collect();
    let pivot = |k: usize| f[k].translation + f[k].rotation * a.build.parts[k].anchor;
    let left = (pivot(1) - pivot(4)).with_z(0.0).normalize_or(Vec3::X);
    left.cross(Vec3::Z).normalize_or(Vec3::Y)
}
pub fn front_test(speed: f32) -> Result<(), String> {
    let targets: [(&str, usize, f32); 4] = [("chest", 10, 0.0), ("chest_left", 10, 0.12), ("chest_right", 10, -0.12), ("head", 12, 0.0)];
    for (name, part, side) in targets {
        let (mut back, mut lateral, mut turn) = (0.0f32, 0.0f32, 0.0f32);
        let (mut lat_max, mut turn_max) = (0.0f32, 0.0f32);
        let runs = 6;
        for run in 0..runs {
            let mut r = Runtime::load(true)?;
            r.rand = Rand::new(0x1234 + run * 977);
            {
                let a = r.actors[0].as_mut().unwrap();
                a.active = true;
                a.balance(&r.world, &r.tp);
            }
            for _ in 0..90 {
                r.step()?;
            }
            let (fwd, root0, at) = {
                let a = r.actors[0].as_mut().unwrap();
                let fwd = facing_of(a);
                let root = a.agent.doll.root();
                // BO1's bridge sets the target 10 m along the zombie's path; on
                // a curved path that is to the side of the real shooter.
                let player = if std::env::var_os("FRONT_PATH_SIDE").is_some() {
                    root.with_z(0.0) + Vec3::Z.cross(fwd) * 10.0
                } else {
                    root.with_z(0.0) + fwd * 3.0
                };
                a.target = Target { enabled: 1, position: [player.x, player.y, 1.0, 0.0], ..Default::default() };
                let left = Vec3::Z.cross(fwd);
                let at = a.agent.doll.frames().nth(part).unwrap().translation + left * side;
                (fwd, root, at)
            };
            let dir = -fwd;
            // A zombie coming at the shooter enters physics with its run.
            if speed > 0.0 {
                let a = r.actors[0].as_mut().unwrap();
                let poses: Vec<_> = a.agent.doll.frames().collect();
                for (k, t) in poses.iter().enumerate() {
                    a.agent.doll.set_body(k, t.translation, t.rotation, fwd * speed, Vec3::ZERO);
                }
            }
            let j = dir * 8.0;
            r.hit(&Hit { part: part as u32, actor: 0, point: [at.x, at.y, at.z, 0.0], impulse: [j.x, j.y, j.z, 0.12], bullet_velocity: [dir.x * 450.0, dir.y * 450.0, dir.z * 450.0, 0.0], normal: [0.0, 0.0, 0.0, 1.0] })?;
            for f in 0..36 {
                r.step()?;
                let a = r.actors[0].as_ref().unwrap();
                let d = a.agent.doll.root() - root0;
                let left = Vec3::Z.cross(fwd);
                let now = facing_of(a);
                let yaw = now.cross(fwd).z.atan2(now.dot(fwd)).to_degrees().abs();
                lat_max = lat_max.max(d.dot(left).abs());
                turn_max = turn_max.max(yaw);
                if f == 35 {
                    back += d.dot(dir);
                    lateral += d.dot(left).abs();
                    turn += yaw;
                }
            }
        }
        let n = runs as f32;
        println!("FRONT speed={speed} {name:<12} back_0.6s={:.3}m side_0.6s={:.3}m side_max={lat_max:.3}m turn_0.6s={:.1}deg turn_max={turn_max:.1}deg", back / n, lateral / n, turn / n);
    }
    Ok(())
}
// Corpses: zombies shot dead on a flat floor, on a 25 degree ramp and in a
// pile; how long until each corpse stops costing solver time (not awake),
// and the step cost while corpses lie there.
pub fn corpse_test() -> Result<(), String> {
    let mut r = Runtime::load(true)?;
    for i in 0..28 {
        r.fragment(i, &Fragment {
            pose: matrix(Transform::from_translation(Vec3::new(-20.0 + (i % 7) as f32, -10.0 + (i / 7) as f32, 1.0))),
            dimensions: [0.25, 0.15, 0.1, 0.0], linear: [0.0;4], angular: [0.0;4], mass: 2.0,
        })?;
    }
    let rise = 25f32.to_radians().tan();
    add_mesh(&mut r.world, &[[20.0, -5.0, 0.0], [30.0, -5.0, 10.0 * rise], [30.0, 5.0, 10.0 * rise], [20.0, 5.0, 0.0]], &[[0, 1, 2], [0, 2, 3]]);
    let n = 12;
    for i in 0..n {
        if i != 0 {
            r.create(i, None)?;
        }
        let a = r.actors[i].as_mut().unwrap();
        let poses: Vec<_> = a.agent.doll.frames().collect();
        // 0..3 flat floor, 4..7 on the ramp, 8..11 piled on one spot.
        let at = match i {
            0..4 => Vec3::new(i as f32 * 3.0, 0.0, 0.0),
            4..8 => Vec3::new(23.0 + (i - 4) as f32 * 1.6, (i % 2) as f32 * 2.0, (3.0 + (i - 4) as f32 * 1.6) * rise),
            _ => Vec3::new(-6.0 + (i - 8) as f32 * 0.3, 6.0, (i - 8) as f32 * 0.5),
        };
        for (k, t) in poses.iter().enumerate() {
            a.agent.doll.set_body(k, t.translation + at, t.rotation, Vec3::ZERO, Vec3::ZERO);
        }
        a.active = true;
        a.balance(&r.world, &r.tp);
    }
    for _ in 0..30 {
        r.step()?;
    }
    for i in 0..n {
        let at = r.actors[i].as_ref().unwrap().agent.doll.frames().nth(10).unwrap().translation;
        r.hit(&Hit { part: 10, actor: i as u32, point: [at.x, at.y, at.z, 0.0], impulse: [0.0, -20.0, 0.0, 0.0], bullet_velocity: [0.0, -450.0, 0.0, 0.0], normal: [0.0, 0.0, 0.0, 2.0] })?;
        let mut req: Request = unsafe { std::mem::zeroed() };
        req.command = 9;
        req.actor = i as u32;
        req.data.active = 0;
        r.command(&req)?;
    }
    let mut asleep_at = vec![None::<u32>; n];
    let mut late_ms = 0.0f64;
    for f in 0..900u32 {
        let t = std::time::Instant::now();
        r.step()?;
        if f >= 600 {
            late_ms += t.elapsed().as_secs_f64() * 1000.0;
        }
        for i in 0..n {
            let a = r.actors[i].as_ref().unwrap();
            let awake = a.agent.doll.awake() && !a.frozen;
            if awake {
                asleep_at[i] = None;
            } else if asleep_at[i].is_none() {
                asleep_at[i] = Some(f);
            }
        }
    }
    let mut still_awake = 0;
    for i in 0..n {
        let d = &r.actors[i].as_ref().unwrap().agent.doll;
        let fastest = (0..PARTS).map(|k| d.vel(k).0.length()).fold(0.0f32, f32::max);
        println!("CORPSE i={i} place={} asleep_after_s={:?} root_v={:.3} root_w={:.3} fastest_part={fastest:.3} tol={:?} mode={}", ["flat", "ramp", "pile"][i / 4], asleep_at[i].map(|f| f as f32 / 60.0), d.vel(0).0.length(), d.vel(0).1.length(), d.sleep_tol, d.mode);
        // A corpse still sliding down the ramp is real motion, not a cost leak.
        still_awake += (asleep_at[i].is_none() && fastest < 0.3) as usize;
    }
    println!("CORPSE awake_after_15s={still_awake}/{n} step_ms_last_5s={:.2}", late_ms / 300.0);
    if still_awake > 0 {
        return Err("corpses still cost solver time 15 s after death".into());
    }
    let sleeping = r.fragments.iter().flatten().filter(|a| a.frozen).count();
    println!("DEBRIS sleeping={sleeping}/28");
    if sleeping != 28 { return Err("settled fragments remain in solver".into()); }
    let start = r.fragments[0].as_ref().unwrap().agent.doll.centre(0);
    let mut req: Request = unsafe { std::mem::zeroed() };
    req.command=13; req.actor=0;
    unsafe { req.data.advance.hits[0] = Hit { actor:0, part:0, point:[start.x,start.y,start.z,0.0], impulse:[2.0,0.0,1.0,0.0], bullet_velocity:[0.0;4], normal:[0.0;4] }; }
    r.command(&req)?;
    if r.fragments[0].as_ref().unwrap().frozen { return Err("hit did not wake frozen fragment".into()); }
    for _ in 0..12 { r.step()?; }
    let travel = r.fragments[0].as_ref().unwrap().agent.doll.centre(0).distance(start);
    println!("DEBRIS hit_wake_travel={travel:.4}");
    if travel < 0.01 { return Err("frozen fragment did not move after hit".into()); }
    // Broad-phase proximity alone must not wake a resting fragment. Actual
    // contact with another moving fragment must wake it, including IDs >= ACTORS.
    let target = r.fragments[1].as_ref().unwrap().agent.doll.centre(0);
    {
        let moving=r.fragments[0].as_mut().unwrap();
        moving.frozen=false;moving.still=0;
        moving.agent.doll.set_body(0,target-Vec3::X*0.20,Quat::IDENTITY,Vec3::X,Vec3::ZERO);
        moving.agent.doll.wake();
    }
    r.step()?;
    if r.fragments[1].as_ref().unwrap().frozen { return Err("fragment contact did not wake neighbour".into()); }
    for _ in 0..12 { r.step()?; }
    let displacement = r.fragments[1].as_ref().unwrap().agent.doll.centre(0) - target;
    if displacement.x <= 0.01 { return Err(format!("contact wake lost transferred momentum: {displacement:?}")); }
    println!("DEBRIS contact_wake=PASS transferred_travel={:.4}", displacement.x);
    Ok(())
}
// A solid box as triangles (for test scenes).
fn add_box(world: &mut Collision, lo: Vec3, hi: Vec3) {
    let c = |i: usize| Vec3::new(if i & 1 == 0 { lo.x } else { hi.x }, if i & 2 == 0 { lo.y } else { hi.y }, if i & 4 == 0 { lo.z } else { hi.z }).to_array();
    let points: Vec<[f32; 3]> = (0..8).map(c).collect();
    let faces = [[0, 2, 1], [1, 2, 3], [4, 5, 6], [5, 7, 6], [0, 1, 4], [1, 5, 4], [2, 6, 3], [3, 6, 7], [0, 4, 2], [2, 4, 6], [1, 3, 5], [3, 7, 5]];
    add_mesh(world, &points, &faces);
}
// Kino-like stairs: a zombie halfway up a ten-step flight, facing the player
// at the top, takes one round in the chest from above (down the stairs).
// Counts how many lose their footing and go down (pelvis 0.6 m lower and
// lying) rather than stepping back down balanced.
pub fn stairs_shot_test(weapon_j: f32, runs: u32) -> Result<(), String> {
    let (mut fell, mut rolled_far) = (0, 0);
    for run in 0..runs {
        let mut r = Runtime::load(true)?;
        r.rand = Rand::new(0x5151 + run * 7919);
        // Top landing at -Y (the zombie's facing), steps descending towards +Y.
        add_box(&mut r.world, Vec3::new(-2.0, -6.0, 0.0), Vec3::new(2.0, 0.0, 1.6));
        for i in 0..10 {
            let top = 1.6 - (i + 1) as f32 * 0.16;
            add_box(&mut r.world, Vec3::new(-2.0, i as f32 * 0.3, 0.0), Vec3::new(2.0, (i + 1) as f32 * 0.3, top));
        }
        let start = {
            let a = r.actors[0].as_mut().unwrap();
            let poses: Vec<_> = a.agent.doll.frames().collect();
            let at = Vec3::new(0.0, 1.35, 1.6 - 5.0 * 0.16 + 0.01);
            for (k, t) in poses.iter().enumerate() {
                a.agent.doll.set_body(k, t.translation + at, t.rotation, Vec3::ZERO, Vec3::ZERO);
            }
            a.active = true;
            a.balance(&r.world, &r.tp);
            a.target = Target { enabled: 1, position: [0.0, -3.0, 1.6, 0.0], ..Default::default() };
            a.agent.doll.root()
        };
        for _ in 0..45 {
            r.step()?;
        }
        let at = r.actors[0].as_ref().unwrap().agent.doll.frames().nth(10).unwrap().translation;
        let dir = Vec3::new(0.0, 1.0, -0.35).normalize();
        let j = dir * weapon_j;
        r.hit(&Hit { part: 10, actor: 0, point: [at.x, at.y, at.z, 0.0], impulse: [j.x, j.y, j.z, 0.12], bullet_velocity: [dir.x * 450.0, dir.y * 450.0, dir.z * 450.0, 0.0], normal: [0.0, 0.0, 0.0, if weapon_j > 20.0 { 6.0 } else { 1.0 }] })?;
        let mut lowest = start.z;
        let mut down = false;
        for _ in 0..240 {
            r.step()?;
            let a = r.actors[0].as_ref().unwrap();
            let p: Vec<_> = a.agent.doll.frames().collect();
            lowest = lowest.min(p[0].translation.z);
            down |= p[12].translation.z - (p[3].translation.z + p[6].translation.z) * 0.5 < 0.7;
        }
        let end = r.actors[0].as_ref().unwrap().agent.doll.root();
        let went = down && lowest < start.z - 0.6;
        println!("STAIRS run={run} fell={went} lying_seen={down} pelvis_drop={:.2}m slid_down={:.2}m", start.z - lowest, end.y - start.y);
        fell += went as u32;
        rolled_far += (end.y - start.y > 1.2) as u32;
    }
    println!("STAIRS weapon_j={weapon_j} fell_down={fell}/{runs} went_1.2m+_down={rolled_far}/{runs}");
    Ok(())
}
pub fn message_test() -> Result<(), String> {
    let mut r = Runtime::load(true)?;
    let mut count = 0;
    for m in msg::MESSAGES.iter().chain(delirium::extras::messages()) {
        r.actors[0] = None;
        r.create(0, None)?;
        let a = r.actors[0].as_mut().unwrap();
        a.active = true;
        a.balance(&r.world, &r.tp);
        let line = if m.name == "injury" {
            "injury part=15 amount=0.2".to_string()
        } else {
            format!("{} start=true", m.name)
        };
        let (name, params) = parse_line(&line)?;
        let refs: Vec<(&str, Value)> = params
            .iter()
            .map(|(n, v)| (n.as_str(), v.clone()))
            .collect();
        a.send(&r.world, &name, &refs)?;
        for _ in 0..12 {
            r.step()?;
        }
        count += 1;
        println!("MESSAGE {name} PASS");
    }
    if count != 55 {
        return Err("incomplete message catalogue".into());
    }
    println!("PASS all 55 Delirium messages executed in original articulation");
    Ok(())
}
pub fn yaw_test(large: bool) -> Result<(), String> {
    let mut r = Runtime::load(!large)?;
    if large {
        add_mesh(
            &mut r.world,
            &[
                [-102., -102., 0.],
                [102., -102., 0.],
                [102., 102., 0.],
                [-102., 102., 0.],
            ],
            &[[0, 1, 2], [0, 2, 3]],
        );
    }
    for i in 0..8 {
        r.actors[i] = None;
        let yaw = Quat::from_rotation_z(i as f32 * std::f32::consts::FRAC_PI_4);
        let mut p: Initial = unsafe { std::mem::zeroed() };
        for k in 0..21 {
            let t = r.rest[k];
            p.pose[k] = matrix(
                Transform::from_translation(
                    yaw * (t.translation - r.rest[0].translation)
                        + r.rest[0].translation
                        + Vec3::new(i as f32 * 4.0 + 0.317, 0.619, 0.0),
                )
                .with_rotation(yaw * t.rotation),
            );
        }
        r.create(i, Some(&p))?;
        let a = r.actors[i].as_mut().unwrap();
        a.active = true;
        a.balance(&r.world, &r.tp);
    }
    for _ in 0..600 {
        r.step()?;
    }
    let mut pass = 0;
    for i in 0..8 {
        let a = r.actors[i].as_ref().unwrap();
        let p: Vec<_> = a.agent.doll.frames().collect();
        let height = p[12].translation.z - (p[3].translation.z + p[6].translation.z) * 0.5;
        println!(
            "YAW i={i} height={height:.3} balance={} recovered={} v={:.3}",
            a.agent.balancing(),
            a.recovery.completed,
            a.agent.doll.com().1.length()
        );
        pass += (height > 1.25 && a.agent.balancing()) as usize;
    }
    if pass != 8 {
        return Err(format!("yaw stability {pass}/8"));
    }
    Ok(())
}
pub fn verify() -> Result<(), String> {
    let mut r = Runtime::load(true)?;
    r.rand = Rand::new(0x49ea6145);
    let start = std::time::Instant::now();
    let mut max_joint = 0.0f32;
    let mut min_z = f32::INFINITY;
    for i in 0..12 {
        if i != 0 {
            r.create(i, None)?;
        }
        let a = r.actors[i].as_mut().unwrap();
        let poses: Vec<_> = a.agent.doll.frames().collect();
        for (k, t) in poses.iter().enumerate() {
            a.agent.doll.set_body(
                k,
                t.translation + Vec3::new((i % 4) as f32 * 1.8, (i / 4) as f32 * 2.0, 0.0),
                t.rotation,
                Vec3::ZERO,
                Vec3::ZERO,
            );
        }
        a.active = true;
        a.balance(&r.world, &r.tp);
        a.target = Target {
            enabled: 1,
            position: [1.0, 20.0, 1.0, 3.5],
            reach_position: [0.0, 0.0, 0.0, 1.0],
            ..Default::default()
        };
    }
    let mut shots = 0;
    let mut recoveries = 0;
    let mut interruptions = 0;
    for frame in 0..1800 {
        if (180..780).contains(&frame) && frame % 5 == 0 {
            for i in 0..12 {
                let p = [0, 2, 5, 10, 12, 15, 19][(frame / 5 + i) % 7];
                let at = r.actors[i]
                    .as_ref()
                    .unwrap()
                    .agent
                    .doll
                    .frames()
                    .nth(p)
                    .unwrap()
                    .translation;
                let dir = Vec3::new(((i % 3) as f32 - 1.0) * 0.35, -1.0, 0.04).normalize();
                let j = if frame % 25 == 0 { 28.0 } else { 6.0 };
                r.hit(&Hit {
                    part: p as u32,
                    actor: i as u32,
                    point: [at.x, at.y, at.z, 0.0],
                    impulse: [dir.x * j, dir.y * j, dir.z * j, 0.0],
                    bullet_velocity: [dir.x * 450.0, dir.y * 450.0, dir.z * 450.0, 0.0],
                    normal: [0.0, 0.0, 0.0, if frame % 25 == 0 { 6.0 } else { 2.0 }],
                })?;
                shots += 1;
            }
        }
        if frame == 400 {
            let t = Transform::from_translation(Vec3::new(0.0, 2.0, 2.0));
            r.fragment(
                0,
                &Fragment {
                    pose: matrix(t),
                    dimensions: [0.25, 0.15, 0.1, 0.0],
                    linear: [1.0, 0.0, 0.0, 0.0],
                    angular: [0.1, 0.0, 0.0, 0.0],
                    mass: 2.0,
                },
            )?;
        }
        r.step()?;
        for a in r.actors.iter().flatten() {
            let poses: Vec<_> = a.agent.doll.frames().collect();
            min_z = min_z.min(poses[0].translation.z);
            for (k, p) in a.build.parts.iter().enumerate().skip(1) {
                if let Some(parent) = p.parent {
                    let left = poses[parent].translation + poses[parent].rotation * p.anchor_parent;
                    let right = poses[k].translation + poses[k].rotation * p.anchor;
                    max_joint = max_joint.max(left.distance(right));
                }
            }
            if poses[0].translation.z < -0.25 {
                return Err(format!(
                    "floor penetration frame={frame} pelvis={:?}",
                    poses[0].translation
                ));
            }
        }
        if frame % 300 == 299 {
            let a = r.actors[0].as_ref().unwrap();
            println!(
                "VERIFY frame={frame} pelvis={:?} speed={:.3} balance={} rising={} recovered={} interrupted={} joint_max={max_joint:.5}",
                a.agent.doll.root(),
                a.agent.doll.com().1.length(),
                a.agent.balancing(),
                a.recovery.rise.is_some(),
                a.recovery.completed,
                a.recovery.interrupted
            );
        }
    }
    for a in r.actors.iter().flatten() {
        recoveries += a.recovery.completed;
        interruptions += a.recovery.interrupted;
    }
    let roots: Vec<_> = r
        .actors
        .iter()
        .flatten()
        .map(|a| a.agent.doll.root())
        .collect();
    let distinct = roots.windows(2).all(|p| p[0].distance(p[1]) > 0.02);
    println!(
        "VERIFY total actors=12 frames=1800 shots={shots} recoveries={recoveries} interrupted={interruptions} min_pelvis={min_z:.4} max_joint={max_joint:.6} distinct={distinct} wall_seconds={:.3}",
        start.elapsed().as_secs_f64()
    );
    if max_joint > 0.12 || !distinct {
        return Err("joint/crowd invariant failed".into());
    }
    Ok(())
}
