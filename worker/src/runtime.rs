// BO1 orchestration through the unmodified official Delirium Physics v0.5.0 SDK.
// Engine types are used for exported build metadata and math; all simulation,
// contacts, messages, impulses and severing go through the public nm_* API.
use crate::{
    protocol::*,
    recovery::{Motions, Recovery},
};
use delirium::{prelude::*, ragdoll::Build};
use delirium_data::rng::Rand;
use delirium_physics::*;
use std::{ffi::CString, path::PathBuf, ptr, time::Instant};
pub const DT: f32 = 1.0 / 60.0;
fn c(s: &str) -> CString {
    CString::new(s).expect("NUL-free internal string")
}
pub fn check(n: i32) -> Result<i32, String> {
    if n < 0 {
        let mut e = [0i8; 512];
        unsafe {
            nm_last_error(e.as_mut_ptr(), 512);
        };
        Err(unsafe { std::ffi::CStr::from_ptr(e.as_ptr()) }
            .to_string_lossy()
            .into())
    } else {
        Ok(n)
    }
}
fn required<T>(p: *mut T) -> Result<*mut T, String> {
    if p.is_null() {
        check(-1)?;
        unreachable!()
    } else {
        Ok(p)
    }
}
fn v(a: [f32; 4]) -> Vec3 {
    Vec3::new(a[0], a[1], a[2])
}
fn tf(m: &[f32; 16]) -> Transform {
    Transform::from_translation(Vec3::new(m[12], m[13], m[14])).with_rotation(
        Quat::from_mat3(&Mat3::from_cols(
            Vec3::new(m[0], m[1], m[2]),
            Vec3::new(m[4], m[5], m[6]),
            Vec3::new(m[8], m[9], m[10]),
        ))
        .normalize(),
    )
}
fn matrix(t: Transform) -> [f32; 16] {
    let q = Mat3::from_quat(t.rotation);
    [
        q.x_axis.x,
        q.x_axis.y,
        q.x_axis.z,
        0.,
        q.y_axis.x,
        q.y_axis.y,
        q.y_axis.z,
        0.,
        q.z_axis.x,
        q.z_axis.y,
        q.z_axis.z,
        0.,
        t.translation.x,
        t.translation.y,
        t.translation.z,
        0.,
    ]
}
pub fn ground(w: *const NmWorld, p: Vec3, len: f32) -> Option<f32> {
    let mut d = 0.;
    let mut n = [0.; 3];
    (unsafe {
        nm_world_raycast(
            w,
            p.to_array().as_ptr(),
            [0., 0., -1.].as_ptr(),
            len,
            &mut d,
            n.as_mut_ptr(),
        )
    } > 0)
        .then_some(p.z - d)
}
// All pointers below are confined to the worker's single simulation thread.
pub struct Body {
    pub ptr: *mut NmAgent,
    masses: Vec<f32>,
    moments: Vec<Vec3>,
}
impl Drop for Body {
    fn drop(&mut self) {
        unsafe { nm_agent_destroy(self.ptr) }
    }
}
impl Body {
    fn new(build: *const NmBuild, meta: &Build) -> Result<Self, String> {
        let a = required(unsafe { nm_agent_create(build, [0.; 3].as_ptr(), 0., 0.31) })?;
        Ok(Self {
            ptr: a,
            masses: meta.parts.iter().map(|p| p.mass).collect(),
            moments: meta.parts.iter().map(|p| p.inertia).collect(),
        })
    }
    pub fn frames(&self) -> std::vec::IntoIter<Transform> {
        let n = unsafe { nm_agent_part_count(self.ptr) } as usize;
        let mut out = vec![0.; n * 7];
        unsafe {
            nm_agent_get_part_transforms(self.ptr, out.as_mut_ptr(), n as i32);
        };
        out.chunks(7)
            .map(|a| {
                Transform::from_translation(Vec3::new(a[0], a[1], a[2]))
                    .with_rotation(Quat::from_xyzw(a[3], a[4], a[5], a[6]))
            })
            .collect::<Vec<_>>()
            .into_iter()
    }
    pub fn vel(&self, k: usize) -> (Vec3, Vec3) {
        let (mut v, mut w) = ([0.; 3], [0.; 3]);
        unsafe {
            nm_agent_get_part_velocity(self.ptr, k as i32, v.as_mut_ptr(), w.as_mut_ptr());
        }
        (Vec3::from_array(v), Vec3::from_array(w))
    }
    pub fn centre(&self, k: usize) -> Vec3 {
        self.frames().nth(k).unwrap().translation
    }
    pub fn com(&self) -> (Vec3, Vec3) {
        let (mut p, mut v) = ([0.; 3], [0.; 3]);
        unsafe {
            nm_agent_get_com(self.ptr, p.as_mut_ptr(), v.as_mut_ptr());
        }
        (Vec3::from_array(p), Vec3::from_array(v))
    }
    pub fn part_mass(&self, k: usize) -> f32 {
        self.masses[k]
    }
    pub fn inertia(&self, k: usize, axis: Vec3) -> f32 {
        let q = self.frames().nth(k).unwrap().rotation;
        let a = q.inverse() * axis;
        self.moments[k].max(Vec3::splat(0.01)).dot(a * a)
    }
    pub fn supported(&self) -> bool {
        (0..self.masses.len())
            .any(|k| unsafe { nm_agent_get_part_contact(self.ptr, k as i32) } & 0x28 == 0x20)
    }
    pub fn force(&mut self, k: usize, at: Vec3, force: Vec3) {
        unsafe {
            nm_agent_apply_impulse(
                self.ptr,
                k as i32,
                at.to_array().as_ptr(),
                (force * DT).to_array().as_ptr(),
            );
        }
    }
    pub fn message(
        &mut self,
        w: *const NmWorld,
        name: &str,
        params: &[(&str, delirium::msg::Value)],
    ) -> Result<(), String> {
        unsafe {
            check(nm_msg_begin(self.ptr, c(name).as_ptr()))?;
            for (name, value) in params {
                let name = c(name);
                use delirium::msg::Value::*;
                check(match value {
                    Float(v) => nm_msg_set_float(self.ptr, name.as_ptr(), *v),
                    Int(v) => nm_msg_set_int(self.ptr, name.as_ptr(), *v),
                    Bool(v) => nm_msg_set_bool(self.ptr, name.as_ptr(), *v as i32),
                    Vec3(v) => nm_msg_set_vec3(self.ptr, name.as_ptr(), v.to_array().as_ptr()),
                    Str(v) => nm_msg_set_string(self.ptr, name.as_ptr(), c(v).as_ptr()),
                })?;
            }
            check(nm_msg_send(self.ptr, w))?;
        }
        Ok(())
    }
    pub fn start_pose(&mut self, w: *const NmWorld) {
        use delirium::msg::Value::*;
        self.message(w, "stopAllBehaviours", &[]).unwrap();
        self.message(
            w,
            "setStiffness",
            &[("bodyStiffness", Float(12.)), ("damping", Float(1.))],
        )
        .unwrap();
    }
    pub fn pose(&mut self, w: *const NmWorld, pose: &[Transform]) {
        let data: Vec<f32> = pose
            .iter()
            .flat_map(|t| {
                [
                    t.translation.x,
                    t.translation.y,
                    t.translation.z,
                    t.rotation.x,
                    t.rotation.y,
                    t.rotation.z,
                    t.rotation.w,
                ]
            })
            .collect();
        unsafe {
            nm_agent_set_incoming_transforms(self.ptr, data.as_ptr(), pose.len() as i32);
        };
        self.message(w, "activePose", &[]).unwrap();
    }
    fn initial(&mut self, i: &Initial) -> Result<(), String> {
        for k in 0..self.masses.len() {
            let t = tf(&i.pose[k]);
            if !t.translation.is_finite()
                || !t.rotation.is_finite()
                || !v(i.linear[k]).is_finite()
                || !v(i.angular[k]).is_finite()
            {
                return Err("nonfinite initial pose".into());
            }
            check(unsafe {
                nm_agent_set_part_state(
                    self.ptr,
                    k as i32,
                    t.translation.to_array().as_ptr(),
                    t.rotation.to_array().as_ptr(),
                    i.linear[k].as_ptr(),
                    i.angular[k].as_ptr(),
                )
            })?;
        }
        Ok(())
    }
    fn balance(&mut self, w: *const NmWorld) -> Result<(), String> {
        check(unsafe { nm_agent_task_balance(self.ptr, w, c("*").as_ptr(), ptr::null()) })?;
        Ok(())
    }
}
struct Actor {
    body: Body,
    active: bool,
    target: Target,
    recovery: Recovery,
    detached: u32,
    quiet: f32,
    wound: f32,
    gravity: f32,
}
pub struct Runtime {
    world: *mut NmWorld,
    build: *mut NmBuild,
    meta: Build,
    rest: Vec<Transform>,
    actors: Vec<Option<Actor>>,
    fragments: Vec<Option<Body>>,
    motions: Motions,
    rand: Rand,
    frame: u32,
    stats: (f64, f64, u32),
}
impl Drop for Runtime {
    fn drop(&mut self) {
        self.actors.clear();
        self.fragments.clear();
        unsafe {
            nm_world_destroy(self.world);
            nm_build_destroy(self.build);
        }
    }
}
impl Runtime {
    pub fn load(floor: bool) -> Result<Self, String> {
        let root = std::env::var_os("BO1_SDK_DATA")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../local/data"));
        // Deliberately no search/fallback to another GTA IV installation.
        let gta =
            r"C:\Program Files (x86)\Steam\steamapps\content\app_12210\depot_12211\GTAIV\GTAIV.exe";
        check(unsafe { nm_unlock(c(gta).as_ptr()) })?;
        check(unsafe {
            nm_init(
                c(root
                    .join("character.xrt")
                    .to_str()
                    .ok_or("data path encoding")?)
                .as_ptr(),
            )
        })?;
        check(unsafe {
            nm_load_task_params(c(root.join("taskparams.txt").to_str().unwrap()).as_ptr())
        })?;
        nm_set_headshot_kills(0); // BO1 alone decides damage, health and lethality.
        let text = std::fs::read_to_string(root.join("build.txt")).map_err(|e| e.to_string())?;
        let (meta, _) = Build::undump(&text);
        if meta.parts.len() != 21 {
            return Err("SDK build must contain 21 parts".into());
        }
        let build = required(unsafe { nm_build_from_text(c(&text).as_ptr()) })?;
        let world = required(nm_world_create())?;
        let mut calibration = Body::new(build, &meta)?;
        let cw = nm_world_create();
        check(unsafe { nm_world_add_floor(cw, 0., 0., 0., 4.) })?;
        calibration.balance(cw)?;
        for _ in 0..180 {
            check(unsafe { nm_agent_step(calibration.ptr, cw, DT) })?;
        }
        if unsafe { nm_agent_is_balancing(calibration.ptr) } == 0 {
            return Err("official SDK neutral calibration failed".into());
        }
        let rest: Vec<_> = calibration.frames().collect();
        unsafe { nm_world_destroy(cw) };
        let motions = Motions::load(&root.join("getup.motion"))?;
        let mut r = Self {
            world,
            build,
            meta,
            rest,
            actors: (0..ACTORS).map(|_| None).collect(),
            fragments: (0..FRAGMENTS).map(|_| None).collect(),
            motions,
            rand: Rand::new(41277),
            frame: 0,
            stats: (0., 0., 0),
        };
        if floor {
            check(unsafe { nm_world_add_floor(world, 0., 0., 0., 100.) })?;
        }
        r.create(0, None)?;
        println!(
            "SDK v0.5.0 b0138418f2b51d38038ff7c773df10f252b599d2 initialized from approved GTA IV PC 1.0.8; 60 Hz fixed solver"
        );
        Ok(r)
    }
    fn create(&mut self, id: usize, pose: Option<&Initial>) -> Result<(), String> {
        if id >= ACTORS || self.actors[id].is_some() {
            return Err("actor occupied".into());
        }
        let mut body = Body::new(self.build, &self.meta)?;
        let mut initial: Initial = unsafe { std::mem::zeroed() };
        for (k, t) in self.rest.iter().enumerate() {
            initial.pose[k] = matrix(*t)
        }
        body.initial(pose.unwrap_or(&initial))?;
        body.balance(self.world)?;
        self.actors[id] = Some(Actor {
            body,
            active: true,
            target: Target::default(),
            recovery: Recovery::default(),
            detached: 0,
            quiet: 0.,
            wound: 0.,
            gravity: 1.,
        });
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
                        if a.target_mask & (1 << i) != 0 {
                            if let Some(actor) = &mut self.actors[i] {
                                actor.target = a.targets[i]
                            }
                        }
                    }
                    for h in a.hits.iter().take(r.hit_count as usize) {
                        self.hit(h)?
                    }
                    for _ in 0..r.steps {
                        self.step()?
                    }
                }
                2 => return Ok(6),
                3 => {
                    let m = r.data.mesh;
                    if m.vertices < 3 || m.vertices > 128 || m.triangles == 0 || m.triangles > 256 {
                        return Ok(3);
                    }
                    let t = tf(&m.transform);
                    let points: Vec<f32> = m
                        .points
                        .iter()
                        .take(m.vertices as usize)
                        .flat_map(|p| (t.translation + t.rotation * v(*p)).to_array())
                        .collect();
                    let indices: Vec<u32> = m
                        .indices
                        .iter()
                        .take(m.triangles as usize)
                        .flat_map(|a| a.map(u32::from))
                        .collect();
                    if points.iter().any(|v| !v.is_finite())
                        || indices.iter().any(|i| *i >= m.vertices)
                    {
                        return Ok(3);
                    }
                    check(nm_world_add_mesh(
                        self.world,
                        points.as_ptr(),
                        m.vertices as i32,
                        indices.as_ptr(),
                        m.triangles as i32,
                        ptr::null(),
                    ))?;
                }
                5 => self.create(id, Some(&r.data.initial))?,
                6 => {
                    if id >= ACTORS {
                        return Ok(3);
                    }
                    self.actors[id] = None;
                }
                11 => {
                    if id >= FRAGMENTS {
                        return Ok(3);
                    }
                    let f = r.data.fragment;
                    let mut meta = self.meta.clone();
                    meta.parts.truncate(1);
                    meta.parts[0].parent = None;
                    meta.parts[0].mass = f.mass.max(0.01);
                    meta.parts[0].boxed = Some(v(f.dimensions) * 0.5);
                    meta.parts[0].half = 0.;
                    meta.parts[0].radius = v(f.dimensions).min_element() * 0.5;
                    meta.parts[0].inertia = Vec3::ZERO;
                    let t = tf(&f.pose);
                    let b = required(nm_build_from_text(c(&meta.dump(&[t])).as_ptr()))?;
                    let mut body = Body::new(b, &meta)?;
                    nm_build_destroy(b);
                    check(nm_agent_set_part_state(
                        body.ptr,
                        0,
                        t.translation.to_array().as_ptr(),
                        t.rotation.to_array().as_ptr(),
                        f.linear.as_ptr(),
                        f.angular.as_ptr(),
                    ))?;
                    body.message(self.world, "stopAllBehaviours", &[])?;
                    nm_agent_set_dead(body.ptr, 1);
                    self.fragments[id] = Some(body);
                }
                12 => {
                    if id >= FRAGMENTS {
                        return Ok(3);
                    }
                    self.fragments[id] = None;
                }
                13 => {
                    if let Some(b) = self.fragments.get_mut(id).and_then(Option::as_mut) {
                        let h = r.data.advance.hits[0];
                        check(nm_agent_apply_impulse(
                            b.ptr,
                            0,
                            h.point.as_ptr(),
                            h.impulse.as_ptr(),
                        ))?;
                    } else {
                        return Ok(3);
                    }
                }
                _ => {
                    let Some(a) = self.actors.get_mut(id).and_then(Option::as_mut) else {
                        return Ok(3);
                    };
                    match r.command {
                        4 => a.body.initial(&r.data.initial)?,
                        7 => {}
                        8 => a.target = r.data.target,
                        9 => {
                            let active = r.data.active != 0;
                            if active != a.active {
                                a.recovery.interrupt();
                                a.body.message(self.world, "stopAllBehaviours", &[])?;
                                check(nm_agent_set_dead(a.body.ptr, (!active) as i32))?;
                                if active {
                                    a.body.balance(self.world)?
                                } else {
                                    check(nm_agent_task_relax(a.body.ptr, 0., 0.1))?;
                                }
                                a.active = active;
                            }
                        }
                        10 => {
                            let mask = r.data.active;
                            for k in 1..21 {
                                if mask & (1 << k) != 0
                                    && nm_agent_is_severed(a.body.ptr, k as i32) == 0
                                {
                                    let p = a.body.centre(k);
                                    let result = nm_agent_sever(
                                        a.body.ptr,
                                        self.world,
                                        k as i32,
                                        p.to_array().as_ptr(),
                                        [0.; 3].as_ptr(),
                                    );
                                    if result < 0 && nm_agent_is_severed(a.body.ptr, k as i32) == 0
                                    {
                                        return Ok(3);
                                    }
                                }
                            }
                            a.detached = mask;
                            a.recovery.interrupt();
                        }
                        14 => {
                            let bytes = &r.data.bytes;
                            let end = bytes.iter().position(|b| *b == 0).unwrap_or(bytes.len());
                            let line =
                                std::str::from_utf8(&bytes[..end]).map_err(|e| e.to_string())?;
                            let mut words = line.split_whitespace();
                            let name = words.next().ok_or("empty message")?;
                            let mut params = Vec::new();
                            for p in words {
                                let Some((key, val)) = p.split_once('=') else {
                                    return Ok(3);
                                };
                                use delirium::msg::Value::*;
                                let value = if val == "true" || val == "false" {
                                    Bool(val == "true")
                                } else if val.contains(',') {
                                    let a: Vec<f32> =
                                        val.split(',').filter_map(|s| s.parse().ok()).collect();
                                    if a.len() != 3 {
                                        return Ok(3);
                                    }
                                    Vec3(delirium::prelude::Vec3::new(a[0], a[1], a[2]))
                                } else if let Ok(v) = val.parse::<i32>() {
                                    Int(v)
                                } else if let Ok(v) = val.parse::<f32>() {
                                    Float(v)
                                } else {
                                    Str(val.into())
                                };
                                params.push((key, value));
                            }
                            if a.body.message(self.world, name, &params).is_err() {
                                return Ok(3);
                            }
                        }
                        15 => a.gravity = (r.data.active as f32 / 1000.).clamp(0.01, 4.),
                        _ => return Ok(3),
                    }
                }
            }
        }
        Ok(2)
    }
    fn hit(&mut self, h: &Hit) -> Result<(), String> {
        let Some(a) = self
            .actors
            .get_mut(h.actor as usize)
            .and_then(Option::as_mut)
        else {
            return Err("hit actor missing".into());
        };
        let mut part = (h.part as usize).min(20);
        while a.detached & (1 << part) != 0 {
            part = self.meta.parts[part].parent.unwrap_or(0);
            if part == 0 {
                break;
            }
        }
        let t = a.body.frames().nth(part).unwrap();
        let at = if h.point[3] == 1. {
            t.translation + t.rotation * v(h.point)
        } else {
            v(h.point)
        };
        let impulse = v(h.impulse);
        if !at.is_finite() || !impulse.is_finite() {
            return Err("nonfinite hit".into());
        }
        a.quiet = 0.;
        a.wound += impulse.length() / 15.;
        a.recovery.interrupt();
        if a.active {
            if h.normal[3] as u32 == 8 {
                check(unsafe {
                    nm_agent_apply_impulse(
                        a.body.ptr,
                        part as i32,
                        at.to_array().as_ptr(),
                        impulse.to_array().as_ptr(),
                    )
                })?;
            } else {
                let dir = impulse.normalize_or(Vec3::Y);
                check(unsafe {
                    nm_agent_task_shot_force(
                        a.body.ptr,
                        self.world,
                        c("*").as_ptr(),
                        part as i32,
                        at.to_array().as_ptr(),
                        dir.to_array().as_ptr(),
                        100.,
                        impulse.length().max(0.1),
                    )
                })?;
            }
        } else {
            check(unsafe {
                nm_agent_apply_impulse(
                    a.body.ptr,
                    part as i32,
                    at.to_array().as_ptr(),
                    impulse.to_array().as_ptr(),
                )
            })?;
        }
        Ok(())
    }
    fn step(&mut self) -> Result<(), String> {
        let timer = Instant::now();
        for a in self.actors.iter_mut().flatten() {
            a.quiet += DT;
            a.wound *= (-DT / 2.).exp();
            if a.active {
                let done = a.recovery.step(
                    &mut a.body,
                    &self.meta,
                    self.world,
                    &self.motions,
                    &self.rest,
                    &mut self.rand,
                    a.quiet > 0.8 && a.detached & 0x7e == 0,
                    None,
                );
                if done {
                    a.body.message(self.world, "stopAllBehaviours", &[])?;
                    a.body.balance(self.world)?;
                }
            }
            if (a.gravity - 1.).abs() > 0.001 {
                for k in 0..21 {
                    if a.detached & (1 << k) == 0 {
                        a.body.force(
                            k,
                            a.body.centre(k),
                            Vec3::Z * (9.81 * (1. - a.gravity) * a.body.part_mass(k)),
                        )
                    }
                }
            }
            check(unsafe { nm_agent_step(a.body.ptr, self.world, DT) })?;
        }
        for b in self.fragments.iter_mut().flatten() {
            check(unsafe { nm_agent_step(b.ptr, self.world, DT) })?;
        }
        self.frame += 1;
        let ms = timer.elapsed().as_secs_f64() * 1000.;
        self.stats.0 += ms;
        self.stats.1 = self.stats.1.max(ms);
        self.stats.2 += 1;
        if self.stats.2 == 600 {
            println!(
                "STATS frame={} actors={} fixed_hz=60 avg_ms={:.3} max_ms={:.3}",
                self.frame,
                self.actors.iter().flatten().count(),
                self.stats.0 / 600.,
                self.stats.1
            );
            self.stats = (0., 0., 0)
        }
        Ok(())
    }
    pub fn export(&self, x: &mut Exchange, focus: usize) -> Result<(), String> {
        unsafe {
            x.response = std::mem::zeroed();
            x.response.status = 2;
            x.response.frame = self.frame;
            x.response.parts = 21;
            x.response.active_controllers = 0x80000000;
            for (k, p) in self.meta.parts.iter().enumerate() {
                x.rig.parent[k] = p.parent.map_or(-1, |i| i as i32);
                let t = self.rest[k];
                let anchor = t.translation + t.rotation * p.anchor;
                x.rig.anchor[k] = [anchor.x, anchor.y, anchor.z, 0.];
                x.rig.shapes[k] = Shape {
                    orientation: p.offset.rotation.to_array(),
                    offset: [0.; 4],
                    size: p
                        .boxed
                        .map_or([0.; 4], |s| [2. * s.x, 2. * s.y, 2. * s.z, 0.]),
                    length: 2. * p.half,
                    radius: p.radius,
                    boxed: p.boxed.is_some() as u32,
                    mass: p.mass,
                };
            }
            for i in 0..ACTORS {
                x.actors[i] = std::mem::zeroed();
                let Some(a) = &self.actors[i] else { continue };
                let s = &mut x.actors[i];
                s.present = 1;
                s.active_controllers = 0x80000000 | a.active as u32;
                s.recovery_active = a.recovery.rise.is_some() as u32;
                s.recovery_completed = a.recovery.completed;
                s.recovery_interrupted = a.recovery.interrupted;
                s.recovery_time = a.recovery.time();
                s.detached_mask = a.detached;
                for (k, t) in a.body.frames().enumerate() {
                    s.poses[k] = matrix(t);
                    let contact = nm_agent_get_part_contact(a.body.ptr, k as i32);
                    if contact & 0x28 == 0x20 {
                        s.support_mask |= 1 << k;
                    }
                }
                s.support_mask &= !a.detached;
                if a.active
                    && nm_agent_is_balancing(a.body.ptr) != 0
                    && nm_agent_has_fallen(a.body.ptr) == 0
                    && s.recovery_active == 0
                {
                    s.support_mask |= 1 << 27;
                }
                let (p, v) = a.body.com();
                s.com = [p.x, p.y, p.z, 0.];
                s.velocity = [v.x, v.y, v.z, 0.];
                s.foot_speed = a.body.vel(3).0.length().max(a.body.vel(6).0.length());
                s.trunk_angular_speed = a.body.vel(0).1.length();
            }
            for i in 0..FRAGMENTS {
                x.fragments[i] = std::mem::zeroed();
                if let Some(b) = &self.fragments[i] {
                    x.fragments[i].present = 1;
                    x.fragments[i].pose = matrix(b.frames().next().unwrap());
                }
            }
            if let Some(s) = x.actors.get(focus).filter(|s| s.present != 0) {
                x.response.poses = s.poses;
                x.response.com = s.com;
                x.response.velocity = s.velocity;
                x.response.recovery_active = s.recovery_active;
                x.response.recovery_completed = s.recovery_completed;
                x.response.recovery_interrupted = s.recovery_interrupted;
                x.response.recovery_time = s.recovery_time;
            } else {
                for (k, t) in self.rest.iter().enumerate() {
                    x.response.poses[k] = matrix(*t)
                }
            }
        }
        Ok(())
    }
}
pub fn verify() -> Result<(), String> {
    let mut r = Runtime::load(true)?;
    let mut worst = 0f64;
    for _ in 0..180 {
        let t = Instant::now();
        r.step()?;
        worst = worst.max(t.elapsed().as_secs_f64() * 1000.);
    }
    let b = &r.actors[0].as_ref().unwrap().body;
    println!(
        "SDK_VERIFY standing={} com={:?} worst_ms={worst:.3}",
        unsafe { nm_agent_is_balancing(b.ptr) },
        b.com().0
    );
    if unsafe { nm_agent_is_balancing(b.ptr) } == 0 {
        return Err("not balanced".into());
    }
    Ok(())
}
