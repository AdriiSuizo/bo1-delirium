// SPDX-License-Identifier: GPL-3.0-only
// Adapted from PVCPC/delirium-physics v0.5.0, delirium-room/src/getup.rs.
// Only local Xbox 360 archive/build/animation import; no game data is embedded.
use delirium::prelude::*;
use delirium::ragdoll::Build;
use delirium_data::{anim, archive_key::ArchiveKey, img, model, resource, rpf, skeleton};
use std::{
    io::{Read, Seek, SeekFrom},
    path::Path,
};
pub const IDLE: usize = 2;
fn plat(s: &str) -> String {
    s.into()
}
fn key(dir: &Path) -> Result<ArchiveKey, String> {
    let xex = delirium_data::xex::program(&dir.join("default.xex"))?;
    let mut head = vec![0; 0x810];
    std::fs::File::open(dir.join("xbox360.rpf"))
        .and_then(|mut f| f.read_exact(&mut head))
        .map_err(|e| e.to_string())?;
    ArchiveKey::find_by_trial(&xex, &head)
        .map(|(_, k)| ArchiveKey::from_bytes(&k))
        .ok_or("no archive key in Xbox 360 executable".into())
}
fn img_open(path: &Path, key: &ArchiveKey) -> Result<(Vec<img::Entry>, std::fs::File), String> {
    let path = &std::path::PathBuf::from(plat(&path.to_string_lossy()));
    let name = path
        .file_name()
        .map_or(String::new(), |n| n.to_string_lossy().into_owned());
    let mut f = std::fs::File::open(path).map_err(|e| format!("{name}: {e}"))?;
    let mut head = vec![];
    (&mut f)
        .take(delirium_data::kv!(1 int) << delirium_data::kv!(127 int))
        .read_to_end(&mut head)
        .map_err(|e| e.to_string())?;
    Ok((img::read_table(&head, Some(key))?, f))
}

fn img_read(
    table: &[img::Entry],
    f: &mut std::fs::File,
    name: &str,
) -> Result<resource::Resource, String> {
    let name = &plat(name);
    let e = table
        .iter()
        .find(|e| e.name.eq_ignore_ascii_case(name))
        .ok_or(format!("{name} not found"))?;
    let mut raw = vec![];
    f.seek(SeekFrom::Start(e.offset))
        .map_err(|e| e.to_string())?;
    f.take(e.used_blocks as u64 * delirium_data::kv!(708 int))
        .read_to_end(&mut raw)
        .map_err(|e| e.to_string())?;
    resource::parse(&raw)
}

static LINKS: [std::sync::OnceLock<Vec<Quat>>; 2] = [const { std::sync::OnceLock::new() }; 2];

pub fn links(nm: usize) -> &'static [Quat] {
    LINKS[nm].get().map_or(&[], |l| l.as_slice())
}

pub fn homer(game: &str, model: &str, nm: usize) -> Result<Build, String> {
    let b = homer_links(game, model, nm)?;
    let _ = LINKS[nm].set(b.links.clone());
    Ok(b)
}

fn homer_links(game: &str, model: &str, nm: usize) -> Result<Build, String> {
    let b = homer_build(
        game,
        model,
        nm,
        LINKS[nm]
            .get()
            .cloned()
            .unwrap_or_else(|| delirium::nmdbm::asset_links(nm)),
    )?;
    if !b.links.is_empty() {
        return Ok(b);
    }
    let clips = Clips::load(game, &b)?;
    let idle: Vec<Quat> = clips
        .place(
            &clips.pose(IDLE, 0.0),
            Transform::from_rotation(Quat::from_rotation_z(-std::f32::consts::FRAC_PI_2)),
        )
        .iter()
        .map(|f| f.rotation)
        .collect();
    match delirium::nmrs::derive_links(&b, &idle) {
        Some(l) => homer_build(game, model, nm, l),
        None => Ok(b),
    }
}

fn homer_build(game: &str, model: &str, nm: usize, links: Vec<Quat>) -> Result<Build, String> {
    let dir = Path::new(game);
    let (table, mut f) = img_open(
        &dir.join("xbox360/models/cdimages/componentpeds.img"),
        &key(dir)?,
    )?;
    let frag = img_read(&table, &mut f, &format!("{model}.xft"))?;
    let bones = skeleton::read(&frag, model::fragment_root(&frag)?)?;
    let parts = delirium_data::ragdoll::read(&frag)?;
    let parents: Vec<Option<usize>> = bones.iter().map(|b| b.parent).collect();
    let bind: Vec<(Vec3, Quat)> = bones
        .iter()
        .map(|b| {
            (
                Vec3::from(b.translation),
                Quat::from_array(b.rotation).normalize(),
            )
        })
        .collect();
    delirium::ragdoll::Rig::linked(&parts, &parents, &bind, &bones, nm, links)
        .map(|r| r.build)
        .ok_or(format!("{model}.xft: no ragdoll"))
}

pub struct Clips {
    parents: Vec<Option<usize>>,
    bind: Vec<(Vec3, Quat)>,
    clips: Vec<(anim::Animation, Vec<Option<usize>>)>,
    parts: Vec<(usize, Transform)>,
    pub clip_names: Vec<String>,
}
impl Clips {
    pub fn load(game: &str, build: &Build) -> Result<Self, String> {
        let dir = Path::new(game);
        let key = key(dir)?;
        let platform = std::fs::read(dir.join("xbox360.rpf")).map_err(|e| e.to_string())?;
        let toc = rpf::read_toc(&platform, &key)?;
        let nested = toc
            .iter()
            .find(|e| e.path.eq_ignore_ascii_case("models/cdimages/playerped.rpf"))
            .ok_or("playerped.rpf missing")?;
        let peds = rpf::read(&platform, nested)?;
        let inner = rpf::read_toc(&peds, &key)?;
        let e = inner
            .iter()
            .find(|e| e.path.eq_ignore_ascii_case("player.xft"))
            .ok_or("player.xft missing")?;
        let frag = resource::parse(&rpf::read(&peds, e)?)?;
        let bones = skeleton::read(&frag, model::fragment_root(&frag)?)?;
        let (table, mut f) = img_open(&dir.join("xbox360/anim/anim.img"), &key)?;
        let mut clips = Vec::new();
        let mut names = Vec::new();
        for (dict, names_wanted) in [
            ("get_up", vec!["get_up_fast"]),
            ("get_up_back", vec!["get_up_fast"]),
            ("move_player", vec!["idle", "walk", "run", "sprint"]),
        ] {
            let all = anim::read_dictionary(&img_read(&table, &mut f, &format!("{dict}.xad"))?)?;
            for name in names_wanted {
                let a = all
                    .iter()
                    .find(|a| a.name.eq_ignore_ascii_case(name))
                    .ok_or(format!("no {name} in {dict}"))?
                    .clone();
                let map = a
                    .tracks
                    .iter()
                    .map(|t| {
                        bones
                            .iter()
                            .position(|b| b.id == t.bone)
                            .filter(|&b| b != 0)
                    })
                    .collect();
                clips.push((a, map));
                names.push(format!("{dict}/{name}"));
            }
        }
        for dict in ["get_up", "get_up_back"] {
            for a in anim::read_dictionary(&img_read(&table, &mut f, &format!("{dict}.xad"))?)? {
                let name = format!("{dict}/{}", a.name);
                if !a.name.starts_with("get_up_") || names.contains(&name) {
                    continue;
                }
                let map = a
                    .tracks
                    .iter()
                    .map(|t| {
                        bones
                            .iter()
                            .position(|b| b.id == t.bone)
                            .filter(|&b| b != 0)
                    })
                    .collect();
                clips.push((a, map));
                names.push(name);
            }
        }
        let parts = build
            .parts
            .iter()
            .map(|p| {
                bones
                    .iter()
                    .position(|b| b.name == p.name)
                    .map(|b| (b, p.offset))
                    .ok_or(format!("no bone {}", p.name))
            })
            .collect::<Result<_, _>>()?;
        Ok(Self {
            parents: bones.iter().map(|b| b.parent).collect(),
            bind: bones
                .iter()
                .map(|b| {
                    (
                        Vec3::from(b.translation),
                        Quat::from_array(b.rotation).normalize(),
                    )
                })
                .collect(),
            clips,
            parts,
            clip_names: names,
        })
    }
    pub fn pose(&self, c: usize, phase: f32) -> Vec<(Vec3, Quat)> {
        let (a, map) = &self.clips[c];
        let mut bind = self.bind.clone();
        bind[0] = (Vec3::ZERO, Quat::IDENTITY);
        let mut out = delirium::ragdoll::sample(&bind, a, map, phase);
        out[0] = delirium::ragdoll::root(a, phase);
        out
    }

    pub fn duration(&self, c: usize) -> f32 {
        self.clips[c].0.duration
    }
    pub fn place(&self, pose: &[(Vec3, Quat)], body: Transform) -> Vec<Transform> {
        let bones = delirium::ragdoll::pose_frames(&self.parents, pose, body);
        self.parts
            .iter()
            .map(|&(b, o)| {
                let (bt, br) = bones[b];
                Transform::from_translation(bt + br * o.translation).with_rotation(br * o.rotation)
            })
            .collect()
    }
    pub fn stand_up(&self, build: &mut Build) -> Vec<Transform> {
        let mut frames = self.place(
            &self.pose(IDLE, 0.0),
            Transform::from_rotation(Quat::from_rotation_z(-std::f32::consts::FRAC_PI_2)),
        );
        for (p, f) in build.parts.iter_mut().zip(&frames) {
            p.bind = f.rotation;
        }
        delirium::nmrs::asset_layout(build, &mut frames);
        frames
    }
}
