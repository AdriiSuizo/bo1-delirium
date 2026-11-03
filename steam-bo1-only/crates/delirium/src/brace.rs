use crate::msg::Value;
use crate::prelude::*;
use crate::shottask::Msg;
use crate::Vd;
use delirium_data::rng::Rand;
use delirium_data::taskparams::TaskParams;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Set {
    pub f: [f32; 9],
    pub gun: [i32; 4],
    pub arm: [f32; 7],
    pub flags: [bool; 2],
}

#[allow(non_snake_case)]
#[inline]
fn F() -> [(&'static str, f32); 9] { [("stayUprightStiffness", delirium_data::kv!(2)), ("braceDistance", delirium_data::kv!(3)), ("braceReachAbsorbtionTime", delirium_data::kv!(4)),
    ("braceTargetPredictionTime", delirium_data::kv!(5)), ("braceGrabStrength", delirium_data::kv!(6)), ("braceGrabDistance", 2.0), ("braceGrabReachAngle", delirium_data::kv!(7)),
    ("braceGrabHoldTimerL", 0.5), ("braceGrabHoldTimerH", delirium_data::kv!(8))] }
#[allow(non_snake_case)]
#[inline]
fn GUN() -> [(&'static str, f32); 4] { [("braceMinTimeBeforeGunThreaten", 0.0), ("braceMaxTimeBeforeGunThreaten", delirium_data::kv!(9)),
    ("braceMinTimeBetweenFireGun", delirium_data::kv!(10)), ("braceMaxTimeBetweenFireGun", delirium_data::kv!(11))] }
#[allow(non_snake_case)]
#[inline]
fn ARM() -> [(&'static str, f32); 7] { [("bracePointArmTwistL", 0.0), ("bracePointArmTwistH", delirium_data::kv!(12)), ("bracePointArmStraightnessL", delirium_data::kv!(13)),
    ("bracePointArmStraightnessH", delirium_data::kv!(14)), ("braceAimAngleDPThreshold", delirium_data::kv!(15)), ("braceLegStiffness", delirium_data::kv!(16)), ("balanceStepClampScale", 1.0)] }

impl Set {
    pub fn read(tp: &TaskParams, set: &str) -> Set {
        let f = |(n, or)| tp.get("NMBrace", set, n, or);
        Set { f: F().map(f), gun: GUN().map(|n| f(n) as i32), arm: ARM().map(f),
              flags: [f(("braceGrabDontLetGo", 0.0)) != 0.0, f(("braceUseGunThreatening", 1.0)) != 0.0] }
    }
}

pub fn start(set: &Set, rand: &mut Rand, few_steps: bool) -> Vec<Msg> {
    use Value::{Bool as B, Float as Fl};
    let f = &set.f;
    let mut b = vec![("start", B(true))];
    b.extend([(1, "braceDistance"), (2, "reachAbsorbtionTime"), (3, "targetPredictionTime")].map(|(i, k)| (k, Fl(f[i]))));
    b.push(("grabDontLetGo", B(set.flags[0])));
    b.extend([(4, "grabStrength"), (5, "grabDistance"), (6, "grabReachAngle")].map(|(i, k)| (k, Fl(f[i]))));
    b.extend([("grabHoldTimer", Fl(rand.float_in_range(f[7], f[8]))), ("legStiffness", Fl(set.arm[5]))]);
    let mut m = vec![("braceForImpact", b)];
    if few_steps {
        m.push(("configureBalance", vec![("stepClampScale", Fl(delirium_data::kv!(17)))]));
    } else {
        m.push(("stayUpright", vec![("start", B(true)), ("stiffness", Fl(f[0]))]));
        m.push(("configureBalance", vec![("stepClampScale", Fl(set.arm[6]))]));
    }
    rand.next();
    m
}

pub fn control(m: [Vec3; 4], hit: Option<Vec3>, place: Vec3, look: Vec3, inst: i32) -> Vec<Msg> {
    let local = |w: Vec3| {
        let d = w - m[3];
        Vec3::new(m[0].vd(d), m[1].vd(d), m[2].vd(d))
    };
    let pos = hit.unwrap_or(place.with_z(place.z - delirium_data::kv!(17)));
    vec![("braceForImpact", vec![("pos", Value::Vec3(local(pos))), ("look", Value::Vec3(local(look))), ("instanceIndex", Value::Int(inst))])]
}

