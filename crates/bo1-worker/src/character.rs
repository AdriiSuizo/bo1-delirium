//! Host character JSON: BO1-derived geometry, joints, masses and motor frames.
use delirium::prelude::*;
use delirium::ragdoll::Build;
use delirium_data::nm_asset;
use delirium::ragdoll::{Limit, PartDef};
use serde::{Deserialize, Serialize};

pub const FORMAT: &str = "nm-character-1";

#[derive(Serialize, Deserialize)]
pub struct Character {
    pub format: String,
    pub joints: Vec<Joint>,
    pub parts: Vec<Part>,
    pub self_collision: Vec<[usize; 2]>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub build: Option<Vec<BuildPart>>,
}

/// One joint, in the model's frame at the zero pose (the model faces +Y, its left at -X).
#[derive(Serialize, Deserialize)]
pub struct Joint {
    pub name: String,
    pub index: u32,
    /// 3 degrees of freedom (lean1, lean2, twist), else a hinge (knee, elbow).
    pub three: bool,
    /// The pivot, against the root part's origin.
    pub pos: [f32; 3],
    /// The twist axis (a hinge's axis) and the lean1 axis (unused by a hinge).
    pub twist_axis: [f32; 3],
    pub lean_axis: [f32; 3],
    /// lean1, lean2, twist: (min, max) each; a hinge's in the first.
    pub limits: [[f32; 2]; 3],
    /// lean1, lean2, twist reversed (the right side's mirror).
    pub flip: [bool; 3],
    pub muscle_stiffness: f32,
    pub damping: f32,
    pub strength: f32,
    /// The most torque: lean and twist (a hinge's in the first).
    pub max_torque: [f32; 2],
}

/// One part at the zero pose: its frame's rows (x, y along the bone, z) and position, and its mass.
#[derive(Serialize, Deserialize)]
pub struct Part {
    pub name: String,
    pub frame: [[f32; 3]; 4],
    pub mass: f32,
}

/// A position and a rotation (quaternion x, y, z, w).
#[derive(Serialize, Deserialize)]
pub struct Frame {
    pub pos: [f32; 3],
    pub rot: [f32; 4],
}

#[derive(Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum JointLimit {
    Cone { swing: f32, twist: f32 },
    Hinge { axis: [f32; 3], lo: f32, hi: f32, twist: f32 },
    /// The joint's frame in the parent and child part (z the twist), lean half ranges about its y and x, twist's.
    Bone { parent_frame: [f32; 4], child_frame: [f32; 4], lean: [f32; 2], twist: f32 },
    BoneHinge { parent_frame: [f32; 4], child_frame: [f32; 4], lo: f32, hi: f32 },
    Locked,
}

/// One part of the ragdoll build: its bone, parent part, mass, capsule (radius, half segment) or box, and its joint.
#[derive(Serialize, Deserialize)]
pub struct BuildPart {
    pub name: String,
    pub parent: Option<usize>,
    pub mass: f32,
    pub radius: f32,
    pub half: f32,
    /// The shape's frame in its bone's.
    pub offset: Frame,
    /// The parent's frame at the bind pose, in this part's.
    pub rest: [f32; 4],
    /// The joint, in the parent's frame and in this part's.
    pub anchor_parent: [f32; 3],
    pub anchor: [f32; 3],
    pub limit: JointLimit,
    /// Pelvis or leg (a stagger keeps it on the animation); upper arm or forearm (a fall reaches out with it).
    pub lower: bool,
    pub reach: bool,
    /// The shape's rotation in the model at the bind pose.
    pub bind: [f32; 4],
    /// A box's half extents instead of the capsule.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub boxed: Option<[f32; 3]>,
    /// Inertia about the shape's axes; zeros: worked out from the shape.
    pub inertia: [f32; 3],
    /// Where the part stands at the start.
    pub frame: Frame,
}

fn q(v: [f32; 4]) -> Quat {
    Quat::from_array(v)
}

impl Character {
    pub fn parse(t: &str) -> Result<Character, String> {
        let c: Character = serde_json::from_str(t).map_err(|e| e.to_string())?;
        if c.format != FORMAT {
            return Err(format!("format {:?}, not {FORMAT:?}", c.format));
        }
        if c.joints.len() != 20 || c.parts.len() != 21 || c.build.as_ref().is_some_and(|b| b.len() != 21) {
            return Err("needs 21 parts and 20 joints".into());
        }
        if c.self_collision.iter().flatten().any(|&k| k >= 21) {
            return Err("self_collision: no such part".into());
        }
        Ok(c)
    }

    /// The asset's pieces, as `nmrs::set` takes them.
    pub fn asset(&self) -> (Vec<nm_asset::Joint>, Vec<[[f32; 3]; 4]>, Vec<f32>, Vec<(usize, usize)>) {
        let mut joints: Vec<nm_asset::Joint> = self.joints.iter().map(|j| nm_asset::Joint {
            name: j.name.clone(), index: j.index, three: j.three, pos: j.pos, twist_axis: j.twist_axis, lean_axis: j.lean_axis,
            limits: j.limits.map(|[a, b]| (a, b)), flip: j.flip, muscle_stiffness: j.muscle_stiffness, damping: j.damping,
            strength: j.strength, max_torque: j.max_torque,
        }).collect();
        joints.sort_by_key(|j| j.index);
        (joints, self.parts.iter().map(|p| p.frame).collect(), self.parts.iter().map(|p| p.mass).collect(),
         self.self_collision.iter().map(|&[a, b]| (a, b)).collect())
    }

    /// The build and its frames (laid out on the loaded asset, as `nm_build_from_text`), if the file has one.
    pub fn build(&self) -> Option<(Build, Vec<Transform>)> {
        let parts = self.build.as_ref()?;
        let tf = |f: &Frame| Transform::from_translation(Vec3::from_array(f.pos)).with_rotation(q(f.rot));
        let mut frames: Vec<Transform> = parts.iter().map(|p| tf(&p.frame)).collect();
        let parts = parts.iter().enumerate().map(|(i, p)| PartDef {
            bone: i, parent: p.parent, mass: p.mass, radius: p.radius, half: p.half, offset: tf(&p.offset), rest: q(p.rest),
            anchor_parent: Vec3::from_array(p.anchor_parent), anchor: Vec3::from_array(p.anchor),
            limit: match p.limit {
                JointLimit::Cone { swing, twist } => Limit::Cone { swing, twist },
                JointLimit::Hinge { axis, lo, hi, twist } => Limit::Hinge { axis: Vec3::from_array(axis), lo, hi, twist },
                JointLimit::Bone { parent_frame, child_frame, lean, twist } => Limit::Bone { pf: q(parent_frame), cf: q(child_frame), lean, twist },
                JointLimit::BoneHinge { parent_frame, child_frame, lo, hi } => Limit::BoneHinge { pf: q(parent_frame), cf: q(child_frame), lo, hi },
                JointLimit::Locked => Limit::Locked,
            },
            lower: p.lower, reach: p.reach, name: p.name.clone(), bind: q(p.bind), boxed: p.boxed.map(Vec3::from_array),
            inertia: Vec3::from_array(p.inertia), size: 1.0, heft: 1.0, grown: 1.0,
        }).collect();
        let mut build = Build { parts, nm: delirium::nmrs::HOMER };
        delirium::nmrs::asset_layout(&mut build, &mut frames);
        if let Some(asset) = delirium::nmrs::ASSET[build.nm].get() {
            let defs=delirium::nmrs::joints(&build,asset)?;
            for (part,j) in build.parts.iter_mut().skip(1).zip(defs) {
                part.limit = if j.three {
                    delirium::ragdoll::Limit::Bone { pf:j.rage_parent,cf:j.rage_child,
                        lean:[(j.limits[0].1-j.limits[0].0)*0.5,(j.limits[1].1-j.limits[1].0)*0.5],
                        twist:(j.limits[2].1-j.limits[2].0)*0.5 }
                }else{delirium::ragdoll::Limit::BoneHinge{pf:j.rage_parent,cf:j.rage_child,lo:j.limits[0].0,hi:j.limits[0].1}};
            }
        }
        Some((build, frames))
    }
}
