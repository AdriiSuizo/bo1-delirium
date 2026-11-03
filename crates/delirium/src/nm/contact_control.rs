use super::*;

impl Agent {
    pub fn set_pursuit_velocity(&mut self, velocity: Vec3) {
        self.pursuit_velocity = if velocity.is_finite() {
            velocity.with_z(0.)
        } else {
            Vec3::ZERO
        };
    }
    /// Native limb IK -> joint motors. The goals are measured support points;
    /// no body transform, velocity, pin or animation frame is imposed.
    pub fn contact_motor_pose(
        &mut self,
        build: &Build,
        reference: &[Transform],
        feet: [Vec3; 2],
        hands: Option<[Vec3; 2]>,
        stiffness: f32,
    ) {
        let Some(original) = self.ch.as_ref() else {
            return;
        };
        let frames: Vec<_> = self.doll.frames().collect();
        let mut ch = original.clone();
        let rows = crate::nmrs::incoming_rows(build.nm, reference);
        for j in 0..ch.eff.len() {
            ch.eff[j].desired = ch.incoming_angle(j, &rows);
        }
        let snapshot = ch.clone();
        let body = crate::nmdbm::Body {
            ch: &snapshot,
            build,
            doll: &self.doll,
            frames: &frames,
        };
        for side in 0..2 {
            let limb = crate::nmshot::limb(side + 2);
            crate::nmdbm::limb_ik(
                &body,
                &mut ch,
                limb.e[1],
                limb.e[2],
                limb.p[0],
                limb.p[3],
                feet[side],
                0.,
                0.,
                1. - 2. * side as f32,
                -1.,
                Vec3::ZERO,
                None,
            );
            let mut foot_pose = frames.clone();
            foot_pose[limb.p[3]].rotation = reference[limb.p[3]].rotation;
            let ankle_rows = crate::nmrs::incoming_rows(build.nm, &foot_pose);
            ch.eff[limb.e[3]].desired = ch.incoming_angle(limb.e[3], &ankle_rows);
            if let Some(hands) = hands {
                let limb = crate::nmshot::limb(side);
                crate::nmdbm::limb_ik(
                    &body,
                    &mut ch,
                    limb.e[1],
                    limb.e[2],
                    limb.p[0],
                    limb.p[3],
                    hands[side],
                    0.,
                    0.,
                    1. - 2. * side as f32,
                    1.,
                    Vec3::ZERO,
                    None,
                );
            }
        }
        if std::env::var_os("BO1_DELIRIUM_IK_DEBUG").is_some() {
            for i in 0..2 {
                let l = crate::nmshot::limb(i + 2);
                eprintln!(
                    "IK side={i} root={:?} foot={:?} wanted={:?} angle={:?} target={:?}",
                    body.part(l.p[0]).m.d,
                    body.part(l.p[3]).m.d,
                    feet[i],
                    snapshot.angle(l.e[2], &frames),
                    ch.eff[l.e[2]].desired
                );
            }
        }
        for (j, e) in ch.eff.iter().enumerate() {
            let limits = ch.limits(j);
            let desired = std::array::from_fn(|axis| {
                e.desired[axis].clamp(limits[2 * axis], limits[2 * axis + 1])
            });
            let response = if j == 2 || j == 5 {
                10.
            } else if j == 1 || j == 4 {
                4.
            } else if j == 0 || j == 3 {
                3.
            } else {
                2.
            };
            self.eff_override.push((
                j,
                desired,
                response,
                stiffness * stiffness,
                2. * stiffness,
                Some(1.),
            ));
        }
        self.incoming = reference.to_vec();
    }
}
