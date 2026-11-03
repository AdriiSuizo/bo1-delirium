use delirium::{collision::Collision, nm::Agent, prelude::*, ragdoll::Build};
#[derive(Default)]
pub struct Gait {
    heading: Option<Vec3>,
    ascent_slope: f32,
    pub passing_side: f32,
}
impl Gait {
    pub fn reset(&mut self) {
        self.heading = None;
        self.ascent_slope = 0.;
        self.passing_side = 0.;
    }
    /// Pursuit intent and supported body assistance. Delirium chooses and
    /// articulates the feet; there is no phase, gait clip or animated pose.
    pub fn step(
        &mut self,
        a: &mut Agent,
        build: &Build,
        world: &Collision,
        rest: &[Transform],
        desired: Vec3,
        facing: Vec3,
        gravity: f32,
        reacting: bool,
    ) -> Result<(), String> {
        let positions = [a.doll.centre(3), a.doll.centre(6)];
        let (_, velocity) = a.doll.com();
        let hip = a.doll.root();
        let root_rotation = a.doll.frames().next().unwrap().rotation * rest[0].rotation.inverse();
        if (root_rotation * Vec3::Z).z < 0.4 || hip.z - positions[0].z.min(positions[1].z) < 0.55 {
            a.set_pursuit_velocity(Vec3::ZERO);
            return Ok(());
        }
        let contacts = [3, 6].map(|k| a.doll.touch[k] & 0x28 == 0x20);
        let current_forward = (root_rotation * Vec3::NEG_Y)
            .with_z(0.)
            .normalize_or(Vec3::NEG_Y);
        let wanted = if reacting { current_forward } else { facing.with_z(0.).normalize_or(current_forward) };
        let heading = self.heading.get_or_insert(current_forward);
        let remaining = heading.cross(wanted).z.atan2(heading.dot(wanted));
        *heading = Quat::from_rotation_z(remaining.clamp(-2. / 60., 2. / 60.)) * *heading;
        let offset = current_forward
            .cross(*heading)
            .z
            .atan2(current_forward.dot(*heading))
            .clamp(-0.6, 0.6);
        let forward = Quat::from_rotation_z(offset) * current_forward;
        // Turn through the planted support before accelerating along a new
        // direction. A sharply redirected navigation target is not an impulse.
        let alignment = current_forward.dot(wanted).max(0.);
        let mut desired = desired * (0.35 + 0.65 * alignment * alignment);
        let terrain_here = world.ground(hip + Vec3::Z * 0.15, 2.);
        let terrain_ahead = world.ground(hip + forward * 0.4 + Vec3::Z * 0.15, 2.);
        let climb = terrain_here.zip(terrain_ahead).map_or(0., |(here,ahead)| (ahead-here).max(0.));
        let farther = world.ground(hip + forward * 0.8 + Vec3::Z * 0.15, 2.);
        let slope = terrain_here.zip(farther).map_or(0., |(here,ahead)| ((ahead-here)/0.8).max(0.));
        // Treads alternate level ground and risers. Do not accelerate back to
        // a sprint each time the short probe briefly sees a level tread.
        self.ascent_slope = (climb / 0.4).max(slope).max(self.ascent_slope * (-1.0f32/18.).exp());
        if !reacting {
            // Ascending costs vertical work and a longer swing. Match travel
            // intent to the terrain before driving the torso into the riser.
            let speed = desired.length();
            let stair_weight = (self.ascent_slope / 0.2).clamp(0., 1.);
            if speed > 1.8 {
                desired *= 1. - stair_weight * (1. - 1.8 / speed);
            }
            desired /= 1. + self.ascent_slope * 3.;
        }
        // A wound suspends navigation thrust, not the load-bearing muscles.
        // Leave native shot balance/step limits in charge during the reaction.
        a.set_pursuit_velocity(if reacting { Vec3::ZERO } else { desired });
        let g = delirium::ragdoll::GRAVITY() * gravity;
        let acceleration = if reacting { Vec3::ZERO } else {
            ((desired - velocity.with_z(0.)) * 6.)
                .clamp_length_max((1.5 + 4.5 * alignment) * gravity.sqrt())
        };
        let yaw =
            Quat::from_rotation_z(Vec3::NEG_Y.cross(forward).z.atan2(Vec3::NEG_Y.dot(forward)));
        let lean = (acceleration.dot(forward) / g.max(1.))
            .atan()
            .clamp(-0.45, 0.45);
        let turn = yaw * Quat::from_rotation_x(lean);
        let mut reference = rest.to_vec();
        for (i, t) in reference.iter_mut().enumerate() {
            t.rotation = turn * rest[i].rotation;
        }
        let mut ground = world
            .ground(hip + Vec3::Z * 0.15, 2.)
            .unwrap_or(hip.z - rest[0].translation.z);
        let supports: Vec<_> = (0..2)
            .filter(|&i| contacts[i])
            .map(|i| positions[i].z - rest[if i == 0 { 3 } else { 6 }].translation.z)
            .collect();
        if !supports.is_empty() {
            ground = supports.iter().sum::<f32>() / (supports.len() as f32);
        }
        if !reacting && climb > 0.02 && climb < 0.4 {
            if let Some(ahead) = terrain_ahead { ground = ground.max(ahead); }
        }
        reference[0].translation = Vec3::new(hip.x, hip.y, ground + rest[0].translation.z);
        for k in 1..21 {
            if let Some(p) = build.parts[k].parent {
                reference[k].translation = reference[p].translation
                    + reference[p].rotation * build.parts[k].anchor_parent
                    - reference[k].rotation * build.parts[k].anchor;
            }
        }
        if contacts.iter().any(|x| *x) {
            let mass = a.doll.mass();
            let max_torque = mass * delirium::ragdoll::GRAVITY() * 0.4;
            for k in [0, 10] {
                // Keep the loaded pelvis supported, but leave the chest to
                // native wound/spine motors. Stabilizing both here would
                // counteract the very upper-body reaction the shot requested.
                if reacting && k == 10 { continue; }
                let at = a.doll.centre(k);
                let current = a.doll.frames().nth(k).unwrap();
                let mut error = reference[k].rotation * current.rotation.inverse();
                if error.w < 0. {
                    error = -error;
                }
                let alpha = error.to_scaled_axis() * 49. - a.doll.vel(k).1 * 14.;
                let axis = alpha.normalize_or(Vec3::X);
                let inertia: f32 = (0..21)
                    .map(|p| {
                        a.doll.inertia(p, axis)
                            + a.doll.part_mass(p)
                                * (a.doll.centre(p) - at).cross(axis).length_squared()
                    })
                    .sum();
                let torque = (alpha * inertia * 0.5).clamp_length_max(max_torque);
                let lever = if axis.z.abs() < 0.8 {
                    axis.cross(Vec3::Z)
                } else {
                    axis.cross(Vec3::X)
                }
                .normalize_or(Vec3::X)
                    * 0.2;
                let force = torque.cross(lever) / (2. * lever.length_squared());
                a.frame_forces.push((k, at + lever, force));
                a.frame_forces.push((k, at - lever, -force));
                let lift = ((reference[k].translation.z - at.z) * 36. - a.doll.vel(k).0.z * 12.
                    + g)
                    .clamp(-g, 2. * g);
                let drive = (acceleration + Vec3::Z * lift) * (mass * 0.5);
                a.frame_forces.push((k, at, drive));
                let count = contacts.iter().filter(|x| **x).count() as f32;
                for (i, &loaded) in contacts.iter().enumerate() {
                    if loaded {
                        let foot = if i == 0 { 3 } else { 6 };
                        a.frame_forces.push((
                            foot,
                            a.doll.centre(foot),
                            -Vec3::Z * drive.z / count,
                        ));
                    }
                }
            }
        }
        Ok(())
    }
}
