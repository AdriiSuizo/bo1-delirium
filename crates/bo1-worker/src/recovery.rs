// The game's local get-up clips are motor targets, never body transforms.
// Contacts, joint limits and impacts remain active throughout recovery.
use delirium::{
    collision::Collision,
    nm::{self, Agent},
    prelude::*,
    ragdoll::Build,
};
use delirium_data::rng::Rand;
use std::io::{Cursor, Read};
struct Frame {
    pelvis: Vec3,
    q: [Quat; 21],
}
struct Clip {
    duration: f32,
    frames: Vec<Frame>,
    back: bool,
    variant: Variant,
}
// The original get_up dictionaries: fast/normal/slow from the front and the
// back, injured from the front only. Each zombie draws its own variant.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Variant {
    Fast,
    Normal,
    Slow,
    Injured,
}
pub struct Motions {
    rest: [Quat; 21],
    clips: Vec<Clip>,
}
pub struct Rise {
    clip: usize,
    // The original task's playback rate (get_up_rate: 0.8 to 1.2).
    rate: f32,
    start: f32,
    time: f32,
    yaw: Quat,
    origin: Vec3,
    initial: Vec<Transform>,
    previous: Vec<Transform>,
    pub age: f32,
}
#[derive(Default)]
pub struct Recovery {
    pub rise: Option<Rise>,
    pub completed: u32,
    pub interrupted: u32,
    wait: f32,
    down: f32,
    ready: f32,
    // How long this zombie stays down before rising, drawn per fall.
    lie: Option<f32>,
    /// Set by the host: wounded zombies favour the injured/slow get-ups.
    pub hurt: bool,
}
fn u32r(c: &mut Cursor<Vec<u8>>) -> Result<u32, String> {
    let mut b = [0; 4];
    c.read_exact(&mut b).map_err(|e| e.to_string())?;
    Ok(u32::from_le_bytes(b))
}
fn fl(c: &mut Cursor<Vec<u8>>) -> Result<f32, String> {
    let f = f32::from_bits(u32r(c)?);
    if !f.is_finite() {
        return Err("invalid recovery value".into());
    }
    Ok(f)
}
fn quat(c: &mut Cursor<Vec<u8>>) -> Result<Quat, String> {
    Ok(Quat::from_xyzw(fl(c)?, fl(c)?, fl(c)?, fl(c)?).normalize())
}
impl Motions {
    pub fn load(path: &std::path::Path) -> Result<Self, String> {
        let mut c = Cursor::new(std::fs::read(path).map_err(|e| e.to_string())?);
        let mut head = [0; 8];
        c.read_exact(&mut head).map_err(|e| e.to_string())?;
        let bo1 = &head == b"BO1MOT01";
        if (!bo1 && &head != b"NMMOTN03") || u32r(&mut c)? != 21 {
            return Err("recovery cache header".into());
        }
        let n = u32r(&mut c)?;
        if !(2..=32).contains(&n) {
            return Err("recovery clip count".into());
        }
        let mut rest = [Quat::IDENTITY; 21];
        for q in &mut rest {
            *q = quat(&mut c)?;
        }
        let mut clips = Vec::new();
        for _ in 0..n {
            let len = u32r(&mut c)?;
            if len == 0 || len > 255 {
                return Err("recovery clip name".into());
            }
            let mut b = vec![0; len as usize];
            c.read_exact(&mut b).map_err(|e| e.to_string())?;
            let name = String::from_utf8(b).map_err(|e| e.to_string())?;
            let count = u32r(&mut c)?;
            if !(2..=1024).contains(&count) {
                return Err("recovery frame count".into());
            }
            let duration = fl(&mut c)?;
            let mut frames = Vec::new();
            for _ in 0..count {
                let pelvis = Vec3::new(fl(&mut c)?, fl(&mut c)?, fl(&mut c)?);
                let mut q = [Quat::IDENTITY; 21];
                for r in &mut q {
                    *r = quat(&mut c)?;
                }
                frames.push(Frame { pelvis, q });
            }
            let back = name.starts_with("get_up_back/");
            if !back && !name.starts_with("get_up/") {
                continue;
            }
            let variant = if name.ends_with("_fast") {
                Variant::Fast
            } else if name.ends_with("_slow") {
                Variant::Slow
            } else if name.ends_with("_injured") {
                Variant::Injured
            } else if name.ends_with("_normal") {
                Variant::Normal
            } else {
                continue;
            };
            clips.push(Clip { duration, frames, back, variant });
        }
        if [false, true]
            .iter()
            .any(|&b| !clips.iter().any(|c| c.back == b && c.variant == Variant::Normal))
        {
            return Err("both face-up and face-down recovery targets required".into());
        }
        // The target build is an idle pose, not a bind pose. Use the matching
        // source idle as reference; applying the bind-to-idle bend twice folded
        // the spine and knees even at the end of a successful muscle recovery.
        if !bo1 {
        let mut c = Cursor::new(
            std::fs::read(path.with_file_name("original-locomotion.motion"))
                .map_err(|e| e.to_string())?,
        );
        let mut header = [0; 352];
        c.read_exact(&mut header).map_err(|e| e.to_string())?;
        if &header[..8] != b"NMMOTN03" {
            return Err("locomotion reference header".into());
        }
        let n = u32r(&mut c)?;
        if n != 4 {
            return Err("idle reference name length".into());
        }
        let mut name = [0; 4];
        c.read_exact(&mut name).map_err(|e| e.to_string())?;
        if &name != b"idle" {
            return Err("first reference must be idle".into());
        }
        let _frames = u32r(&mut c)?;
        let _duration = fl(&mut c)?;
        for _ in 0..3 {
            fl(&mut c)?;
        }
        for q in &mut rest {
            *q = quat(&mut c)?;
        }
        }
        println!(
            "Delirium host recovery: {} local original clips, interruptible on impact",
            clips.len()
        );
        Ok(Self { rest, clips })
    }
    fn sample(
        &self,
        clip: usize,
        time: f32,
        rest: &[Transform],
        build: &Build,
        yaw: Quat,
        origin: Vec3,
    ) -> Vec<Transform> {
        let c = &self.clips[clip];
        let f = (time / c.duration).clamp(0.0, 1.0) * (c.frames.len() - 1) as f32;
        let i = f as usize;
        let j = (i + 1).min(c.frames.len() - 1);
        let alpha = f - i as f32;
        let mut out = Vec::with_capacity(21);
        // lsc4-support exports animation bones in the source skeleton's frame,
        // but builds Fred after a -90-degree world-Z turn. Conjugate animation
        // deltas into that same frame before applying the body offsets. Without
        // this, knee flexion becomes off-axis twist that the hinge rejects.
        let basis = Quat::from_rotation_z(-std::f32::consts::FRAC_PI_2);
        for k in 0..21 {
            let q = basis * c.frames[i].q[k].slerp(c.frames[j].q[k], alpha)
                * self.rest[k].inverse()
                * basis.inverse()
                * rest[k].rotation;
            out.push(Transform::from_rotation((yaw * q).normalize()));
        }
        out[0].translation = origin + yaw * basis * c.frames[i].pelvis.lerp(c.frames[j].pelvis, alpha);
        for k in 1..21 {
            if let Some(p) = build.parts[k].parent {
                let pivot = out[p].translation + out[p].rotation * build.parts[k].anchor_parent;
                out[k].translation = pivot - out[k].rotation * build.parts[k].anchor;
            }
        }
        out
    }
    fn pick(&self, a: &Agent, build: &Build, rest: &[Transform], rng: &mut Rand, hurt: bool) -> Rise {
        let current: Vec<Transform> = a.doll.frames().collect();
        // Per-zombie variety: weighted variant, then the original rate draw.
        // The front/back pose match below still chooses the matching side.
        let roll = rng.next() % 100;
        let variant = match (hurt, roll) {
            (true, 0..45) => Variant::Injured,
            (true, 45..80) => Variant::Slow,
            (true, _) => Variant::Normal,
            (false, 0..30) => Variant::Fast,
            (false, 30..75) => Variant::Normal,
            (false, 75..90) => Variant::Slow,
            (false, _) => Variant::Injured,
        };
        let rate = delirium_data::getup::get_up_rate(rng.next());
        let has = |back: bool| self.clips.iter().any(|c| c.back == back && c.variant == variant);
        // There is no injured get-up from the back: a hurt zombie lying face
        // up takes the slow one (played a little slower) rather than normal.
        let fallback = if variant == Variant::Injured { Variant::Slow } else { Variant::Normal };
        let resolved = |back: bool| if has(back) { variant }
            else if self.clips.iter().any(|c| c.back == back && c.variant == fallback) { fallback }
            else { Variant::Normal };
        let allowed = |c: &Clip| c.variant == resolved(c.back);
        let mut best = (f32::INFINITY, 0, 0.0, Quat::IDENTITY);
        let trunk = current[10].translation - current[0].translation;
        let partial = trunk.normalize_or(Vec3::Z).z > 0.45
            && current[12].translation.z
                - (current[3].translation.z + current[6].translation.z) * 0.5
                > 0.75;
        for (c, clip) in self.clips.iter().enumerate().filter(|(_, c)| allowed(c)) {
            let count = if partial {
                (clip.frames.len() as f32 * 0.7) as usize
            } else {
                1
            };
            for i in (0..count).step_by(3) {
                let time = clip.duration * i as f32 / (clip.frames.len() - 1) as f32;
                let candidate = self.sample(c, time, rest, build, Quat::IDENTITY, Vec3::ZERO);
                let (mut aa, mut bb, mut ab, mut weights) = (0.0, 0.0, 0.0, 0.0);
                for (k, w) in [
                    (0, 1.0),
                    (7, 1.0),
                    (10, 3.0),
                    (1, 0.25),
                    (2, 0.25),
                    (4, 0.25),
                    (5, 0.25),
                ] {
                    let q = candidate[k].rotation;
                    let a = current[k].rotation.dot(q);
                    let b = current[k]
                        .rotation
                        .dot(Quat::from_xyzw(0.0, 0.0, 1.0, 0.0) * q);
                    aa += w * a * a;
                    bb += w * b * b;
                    ab += w * a * b;
                    weights += w;
                }
                let angle = (2.0f32 * ab).atan2(aa - bb);
                let score =
                    weights - 0.5 * (aa + bb + ((aa - bb) * (aa - bb) + 4.0 * ab * ab).sqrt());
                if score < best.0 {
                    best = (score, c, time, Quat::from_rotation_z(angle));
                }
            }
        }
        // The cache's pelvis is relative to its standing origin (negative while
        // lying), not a height above ground. Preserve the observed root at entry.
        let first = self.sample(best.1, best.2, rest, build, best.3, Vec3::ZERO);
        let origin = current[0].translation - first[0].translation;
        let rate = if variant == Variant::Injured && self.clips[best.1].variant == Variant::Slow { rate * 0.85 } else { rate };
        Rise {
            clip: best.1,
            rate,
            start: best.2,
            time: best.2,
            yaw: best.3,
            origin,
            previous: current.clone(),
            initial: current,
            age: 0.0,
        }
    }
}
// Pose references guide a supported recovery; the dynamic body can lag behind
// them, collide, lose support or be interrupted before returning to balance.
impl Recovery {
    pub fn interrupt(&mut self) {
        if self.rise.take().is_some() {
            self.interrupted += 1;
        }
        self.wait = 0.65;
        self.down = 0.0;
        self.ready = 0.0;
        self.lie = None;
    }
    /// The running get-up's clip name index, variant and rate (diagnostics).
    pub fn current(&self, motions: &Motions) -> Option<(usize, Variant, bool, f32)> {
        self.rise.as_ref().map(|r| {
            let c = &motions.clips[r.clip];
            (r.clip, c.variant, c.back, r.rate)
        })
    }
    pub fn time(&self) -> f32 {
        self.rise.as_ref().map_or(0.0, |r| r.time - r.start)
    }
    pub fn step(
        &mut self,
        a: &mut Agent,
        build: &Build,
        world: &Collision,
        motions: &Motions,
        rest: &[Transform],
        rng: &mut Rand,
        eligible: bool,
        corpse_support: Option<f32>,
    ) -> bool {
        let dt = 1.0 / 60.0;
        self.wait = (self.wait - dt).max(0.0);
        let frames: Vec<Transform> = a.doll.frames().collect();
        if frames.len() != 21 {
            return false;
        }
        let feet = frames[3].translation.z.min(frames[6].translation.z);
        let trunk = frames[10].translation - frames[0].translation;
        let up = frames[12].translation.z - feet > 1.25 && trunk.normalize_or(Vec3::Z).z > 0.8;
        let supported = a.doll.touch.iter().any(|t| t & 0x28 == 0x20);
        if self.rise.is_none() {
            self.down = if eligible && !up && supported && a.doll.com().1.length() < 0.8 {
                self.down + dt
            } else {
                0.0
            };
            // A crowd knocked down together must not stand up in unison.
            let lie = *self
                .lie
                .get_or_insert_with(|| 0.6 + (rng.next() % 1000) as f32 * 0.0011);
            if self.wait == 0.0 && self.down > lie {
                if world.ground(frames[0].translation + Vec3::Z * 0.2, 2.0).is_some() {
                    self.rise = Some(motions.pick(a, build, rest, rng, self.hurt));
                    nm::task_pose(a, build, world, rng, 11.0, 14.0);
                    self.down = 0.0;
                    self.lie = None;
                }
            }
        }
        let Some(r) = &mut self.rise else {
            return false;
        };
        r.age += dt;
        r.time += dt * r.rate;
        let mut pose = motions.sample(r.clip, r.time, rest, build, r.yaw, r.origin);
        let blend = (r.age / 0.25).clamp(0.0, 1.0);
        let standing = ((r.time / motions.clips[r.clip].duration - 0.6) / 0.4).clamp(0.0, 1.0);
        let standing = standing * standing * (3.0 - 2.0 * standing);
        for k in 0..21 {
            pose[k].rotation = r.initial[k].rotation.slerp(
                pose[k].rotation.slerp(r.yaw * rest[k].rotation, standing),
                blend,
            );
        }
        pose[0].translation = r.initial[0].translation.lerp(pose[0].translation, blend);
        for k in 1..21 {
            if let Some(p) = build.parts[k].parent {
                pose[k].translation = pose[p].translation
                    + pose[p].rotation * build.parts[k].anchor_parent
                    - pose[k].rotation * build.parts[k].anchor;
            }
        }
        // Fit the terminal soles to the same world/corpse supports as the solver.
        // Do not move to another floor: query from the observed pelvis downward.
        if standing > 0.0 {
            if let Some(mut ground) = world.ground(frames[0].translation + Vec3::Z * 0.2, 2.0) {
                if let Some(z) = corpse_support {
                    ground = ground.max(z);
                }
                let shift = (ground + rest[0].translation.z - pose[0].translation.z) * standing;
                for t in &mut pose {
                    t.translation.z += shift;
                }
            }
        }
        if supported {
            // Support assistance is a critically damped pelvis/trunk drive,
            // expressed as forces. It cannot relocate a limb through contacts,
            // and disappears when the body loses all support. Joint motors
            // follow the reference while the solver enforces articulation.
            let mass: f32 = (0..21).map(|k| a.doll.part_mass(k)).sum();
            // Joint targets only specify relative rotations. Without a root
            // orientation drive, lifting the pelvis can stand the whole rigid
            // shape upright without following the get-up's roll/crouch motion.
            // A bounded force couple supplies that missing absolute rotation;
            // it changes no transforms and remains opposed by real contacts.
            let shortest = |mut q: Quat| {
                if q.w < 0.0 { q = -q; }
                q.to_scaled_axis()
            };
            let error = shortest(pose[0].rotation * frames[0].rotation.inverse());
            let desired_w = shortest(pose[0].rotation * r.previous[0].rotation.inverse()) / dt;
            let angular_acceleration = error * 36.0 + (desired_w - a.doll.vel(0).1) * 12.0;
            let axis = angular_acceleration.normalize_or(Vec3::X);
            let centre = a.doll.centre(0);
            let inertia: f32 = (0..21).map(|k| {
                let arm = a.doll.centre(k) - centre;
                a.doll.inertia(k, axis) + a.doll.part_mass(k) * arm.cross(axis).length_squared()
            }).sum();
            // A fixed mass*g*0.25 cap could not follow BO1's roll-to-support
            // get-ups. Bound the absolute drive by the loaded character's
            // actual hip/spine actuator budgets instead of inventing another
            // weapon-independent or clip-specific lift multiplier.
            let torque_budget = delirium::nmrs::ASSET[build.nm].get()
                .map(|joints| [0,3,6,7,8,9].iter()
                    .map(|&i| joints[i].max_torque[0].max(joints[i].max_torque[1]))
                    .sum::<f32>()).unwrap_or(0.0);
            let torque = (angular_acceleration * inertia).clamp_length_max(torque_budget);
            let lever = if axis.z.abs() < 0.8 { axis.cross(Vec3::Z) } else { axis.cross(Vec3::X) }
                .normalize_or(Vec3::X) * 0.2;
            let force = torque.cross(lever) / (2.0 * lever.length_squared());
            a.frame_forces.push((0, centre + lever, force));
            a.frame_forces.push((0, centre - lever, -force));
            for k in [0, 10] {
                let desired_velocity = (pose[k].translation-r.previous[k].translation)/dt;
                let acceleration = ((pose[k].translation-frames[k].translation)*36.0
                    +(desired_velocity-a.doll.vel(k).0)*12.0+Vec3::Z*delirium::ragdoll::GRAVITY())
                    .clamp_length_max(3.0*delirium::ragdoll::GRAVITY());
                a.frame_forces.push((k,a.doll.centre(k),acceleration*(mass*0.5)));
            }
        }
        r.previous.clone_from(&pose);
        nm::task_pose_control(a, pose);
        if r.time > motions.clips[r.clip].duration + 0.35 && up && supported {
            self.rise = None;
            self.completed += 1;
            self.wait = 0.8;
            self.ready = 0.0;
            return true;
        }
        if r.age > motions.clips[r.clip].duration / r.rate + 4.0 {
            self.interrupt();
            return true;
        }
        false
    }
}
