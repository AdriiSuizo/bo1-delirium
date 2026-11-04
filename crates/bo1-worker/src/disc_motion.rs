//! GTA IV Xbox 360 recovery references. Adapted from the project's local
//! lsc4-support exporter. The source skeleton and samples come from the disc.
use delirium::prelude::{Quat, Vec3};
use delirium_data::{
    anim, archive_key::ArchiveKey, disc::Disc, img, model, resource, rpf, skeleton,
};

const ANIM_IMG: &str = "xbox360/anim/anim.img";
const PARTS: [&str; 21] = [
    "Char_Pelvis",
    "Char_L_Thigh",
    "Char_L_Calf",
    "Char_L_Foot",
    "Char_R_Thigh",
    "Char_R_Calf",
    "Char_R_Foot",
    "Char_Spine",
    "Char_Spine1",
    "Char_Spine2",
    "Char_Spine3",
    "Char_Neck",
    "Char_Head",
    "Char_L_Clavicle",
    "Char_L_UpperArm",
    "Char_L_Forearm",
    "Char_L_Hand",
    "Char_R_Clavicle",
    "Char_R_UpperArm",
    "Char_R_Forearm",
    "Char_R_Hand",
];

fn archive_file(bytes: &[u8], key: &ArchiveKey, name: &str) -> Result<Vec<u8>, String> {
    let entries = rpf::read_toc(bytes, key)?;
    let entry = entries
        .iter()
        .find(|entry| entry.path.eq_ignore_ascii_case(name))
        .ok_or_else(|| format!("Missing RPF entry {name}"))?;
    rpf::read(bytes, entry)
}

fn dictionary(
    disc: &Disc,
    entries: &[img::Entry],
    name: &str,
) -> Result<Vec<anim::Animation>, String> {
    let entry = entries
        .iter()
        .find(|entry| entry.name.eq_ignore_ascii_case(name))
        .ok_or_else(|| format!("Missing animation dictionary {name}"))?;
    let size = usize::from(entry.used_blocks) * 2048;
    let bytes = disc.read_at(ANIM_IMG, entry.offset, size)?;
    if size == 0 || bytes.len() != size {
        return Err(format!("Truncated animation dictionary {name}"));
    }
    anim::read_dictionary(&resource::parse(&bytes)?)
}

type Pose = Vec<(Vec3, Quat)>;

fn world_pose(bones: &[skeleton::Bone], local: &[(Vec3, Quat)]) -> Pose {
    let mut world: Pose = Vec::with_capacity(local.len());
    for (bone, &(position, rotation)) in bones.iter().zip(local) {
        let (parent, basis) = bone
            .parent
            .map_or((Vec3::ZERO, Quat::IDENTITY), |index| world[index]);
        world.push((parent + basis * position, (basis * rotation).normalize()));
    }
    world
}

struct Exporter {
    bones: Vec<skeleton::Bone>,
    local: Pose,
    indices: Vec<usize>,
}

impl Exporter {
    fn encode(&self, clips: &[(&str, &anim::Animation)]) -> Result<Vec<u8>, String> {
        fn floats(output: &mut Vec<u8>, values: impl IntoIterator<Item = f32>) {
            for value in values {
                output.extend_from_slice(&value.to_le_bytes());
            }
        }
        let mut output = b"NMMOTN03".to_vec();
        output.extend_from_slice(&(PARTS.len() as u32).to_le_bytes());
        output.extend_from_slice(&(clips.len() as u32).to_le_bytes());
        let bind = world_pose(&self.bones, &self.local);
        for &index in &self.indices {
            floats(&mut output, bind[index].1.to_array());
        }
        for &(name, animation) in clips {
            if !(2..=1024).contains(&animation.frames)
                || !animation.duration.is_finite()
                || animation.duration <= 0.0
            {
                return Err(format!("Invalid animation {name}"));
            }
            output.extend_from_slice(&(name.len() as u32).to_le_bytes());
            output.extend_from_slice(name.as_bytes());
            output.extend_from_slice(&(animation.frames as u32).to_le_bytes());
            output.extend_from_slice(&animation.duration.to_le_bytes());
            for frame in 0..animation.frames {
                let mut local = self.local.clone();
                local[0] = (Vec3::ZERO, Quat::IDENTITY);
                for track in &animation.tracks {
                    let values = track.sample(frame);
                    let bone = self
                        .bones
                        .iter()
                        .position(|bone| bone.id == track.bone)
                        .filter(|&index| index != 0);
                    match (track.kind, bone, track.bone, values.as_slice()) {
                        (anim::BONE_ROTATION, Some(index), _, [x, y, z, w]) => {
                            local[index].1 = Quat::from_xyzw(*x, *y, *z, *w).normalize()
                        }
                        (anim::BONE_TRANSLATION, Some(index), _, [x, y, z]) => {
                            local[index].0 = Vec3::new(*x, *y, *z)
                        }
                        (anim::BONE_ROTATION, None, 0, [x, y, z, w]) => {
                            local[0].1 = Quat::from_xyzw(*x, *y, *z, *w).normalize()
                        }
                        (anim::BONE_TRANSLATION | anim::MOVER_TRANSLATION, None, 0, [x, y, z]) => {
                            local[0].0 += Vec3::new(*x, *y, *z)
                        }
                        _ => {}
                    }
                }
                let world = world_pose(&self.bones, &local);
                if world.iter().any(|(p, q)| !p.is_finite() || !q.is_finite()) {
                    return Err(format!("Invalid pose in {name}, frame {frame}"));
                }
                floats(&mut output, world[self.indices[0]].0.to_array());
                for &index in &self.indices {
                    floats(&mut output, world[index].1.to_array());
                }
            }
            println!("GTA IV motion {name}: {} frames", animation.frames);
        }
        Ok(output)
    }
}

pub fn extract(disc: &Disc, executable: &[u8]) -> Result<(Vec<u8>, Vec<u8>), String> {
    let size = disc
        .len("xbox360.rpf")
        .filter(|&size| size <= 256 * 1024 * 1024)
        .ok_or("Missing or oversized xbox360.rpf")?;
    let platform = disc.read_at("xbox360.rpf", 0, size as usize)?;
    // Resolve the table key from the user's executable and encrypted TOC.
    let (key, _) = ArchiveKey::find_by_trial(executable, &platform)
        .ok_or("Could not resolve the GTA IV archive key")?;
    let archive = archive_file(&platform, &key, "models/cdimages/playerped.rpf")?;
    let player = resource::parse(&archive_file(&archive, &key, "player.xft")?)?;
    let bones = skeleton::read(&player, model::fragment_root(&player)?)?;
    let indices = PARTS
        .iter()
        .map(|name| {
            bones
                .iter()
                .position(|bone| bone.name == *name)
                .ok_or_else(|| format!("Missing source bone {name}"))
        })
        .collect::<Result<Vec<_>, _>>()?;
    let local = bones
        .iter()
        .map(|bone| {
            (
                Vec3::from(bone.translation),
                Quat::from_array(bone.rotation).normalize(),
            )
        })
        .collect();
    let exporter = Exporter {
        bones,
        local,
        indices,
    };
    let header = disc.read_at(ANIM_IMG, 0, 20)?;
    let len = img::table_len(&header, Some(&key))
        .filter(|&len| len <= 16 * 1024 * 1024)
        .ok_or("Invalid anim.img table size")?;
    let entries = img::read_table(&disc.read_at(ANIM_IMG, 0, len)?, Some(&key))?;
    let front = dictionary(disc, &entries, "get_up.xad")?;
    let back = dictionary(disc, &entries, "get_up_back.xad")?;
    let walk = dictionary(disc, &entries, "move_player.xad")?;
    let named = front
        .iter()
        .filter(|animation| animation.name.starts_with("get_up"))
        .map(|animation| (format!("get_up/{}", animation.name), animation))
        .chain(
            back.iter()
                .filter(|animation| animation.name.starts_with("get_up"))
                .map(|animation| (format!("get_up_back/{}", animation.name), animation)),
        )
        .collect::<Vec<_>>();
    let get_up = named
        .iter()
        .map(|(name, animation)| (name.as_str(), *animation))
        .collect::<Vec<_>>();
    let locomotion = ["idle", "walk", "run", "sprint"]
        .into_iter()
        .map(|name| {
            walk.iter()
                .find(|animation| animation.name == name)
                .map(|animation| (name, animation))
                .ok_or_else(|| format!("Missing GTA IV locomotion reference {name}"))
        })
        .collect::<Result<Vec<_>, _>>()?;
    Ok((exporter.encode(&get_up)?, exporter.encode(&locomotion)?))
}
