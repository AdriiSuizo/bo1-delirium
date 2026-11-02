// SPDX-License-Identifier: GPL-3.0-only
mod import360;
use delirium::prelude::*;
use delirium_data::{archive_key::ArchiveKey, rpf};
use delirium_physics::*;
use std::{
    ffi::CString,
    io::{Read, Write},
    path::Path,
};
fn error(n: i32) -> Result<(), String> {
    if n < 0 {
        let mut b = [0i8; 1024];
        unsafe {
            nm_last_error(b.as_mut_ptr(), 1024);
        };
        Err(unsafe { std::ffi::CStr::from_ptr(b.as_ptr()) }
            .to_string_lossy()
            .into())
    } else {
        Ok(())
    }
}
fn archive(game: &Path, name: &str, path: &str, key: &ArchiveKey) -> Result<Vec<u8>, String> {
    let d = std::fs::read(game.join(name)).map_err(|e| e.to_string())?;
    let toc = rpf::read_toc(&d, key)?;
    let e = toc
        .iter()
        .find(|e| e.path.eq_ignore_ascii_case(path))
        .ok_or(format!("missing {path}"))?;
    rpf::read(&d, e)
}
fn motions(out: &Path, name: &str, clips: &import360::Clips, ids: &[usize]) -> Result<(), String> {
    let mut f =
        std::io::BufWriter::new(std::fs::File::create(out.join(name)).map_err(|e| e.to_string())?);
    let mut data = Vec::new();
    data.extend_from_slice(b"NMMOTN03");
    data.extend_from_slice(&21u32.to_le_bytes());
    data.extend_from_slice(&(ids.len() as u32).to_le_bytes());
    let idle = clips.place(&clips.pose(import360::IDLE, 0.), Transform::IDENTITY);
    for t in &idle {
        for q in t.rotation.to_array() {
            data.extend_from_slice(&q.to_le_bytes())
        }
    }
    for &i in ids {
        let name = if i >= 2 && i <= 5 {
            clips.clip_names[i].split('/').last().unwrap()
        } else {
            &clips.clip_names[i]
        };
        data.extend_from_slice(&(name.len() as u32).to_le_bytes());
        data.extend_from_slice(name.as_bytes());
        let duration = clips.duration(i);
        let count = (duration * 60.).ceil() as u32 + 1;
        data.extend_from_slice(&count.to_le_bytes());
        data.extend_from_slice(&duration.to_le_bytes());
        for k in 0..count {
            let frames = clips.place(
                &clips.pose(i, k as f32 / (count - 1) as f32),
                Transform::IDENTITY,
            );
            for v in (frames[0].translation - idle[0].translation).to_array() {
                data.extend_from_slice(&v.to_le_bytes())
            }
            for t in frames {
                for q in t.rotation.to_array() {
                    data.extend_from_slice(&q.to_le_bytes())
                }
            }
        }
    }
    f.write_all(&data).map_err(|e| e.to_string())?;
    Ok(())
}
fn run() -> Result<(), String> {
    let args: Vec<_> = std::env::args().collect();
    let game = Path::new(args.get(1).ok_or("usage: sdk_import360 GAME_COPY OUTPUT")?);
    let out = Path::new(args.get(2).ok_or("output missing")?);
    let exe = game.join("default.xex");
    error(unsafe {
        nm_unlock(
            CString::new(exe.to_str().ok_or("path encoding")?)
                .unwrap()
                .as_ptr(),
        )
    })?;
    delirium_data::LE.store(false, std::sync::atomic::Ordering::Relaxed);
    let program = delirium_data::xex::program(&exe)?;
    let mut head = vec![0; 0x810];
    std::fs::File::open(game.join("xbox360.rpf"))
        .and_then(|mut f| f.read_exact(&mut head))
        .map_err(|e| e.to_string())?;
    let (_, bytes) =
        ArchiveKey::find_by_trial(&program, &head).ok_or("Xbox archive key missing")?;
    let key = ArchiveKey::from_bytes(&bytes);
    let xrt = archive(game, "xbox360.rpf", "data/naturalmotion/fred.xrt", &key)?;
    error(unsafe { nm_init_memory(xrt.as_ptr(), xrt.len() as i32) })?;
    let mut build = import360::homer(
        game.to_str().unwrap(),
        "m_y_pmanhat_01",
        delirium::nmrs::HOMER,
    )?;
    let clips = import360::Clips::load(game.to_str().unwrap(), &build)?;
    let frames = clips.stand_up(&mut build);
    if build.parts.len() != 21 {
        return Err("incomplete rig".into());
    }
    std::fs::create_dir_all(out).map_err(|e| e.to_string())?;
    std::fs::write(out.join("character.xrt"), xrt).map_err(|e| e.to_string())?;
    std::fs::write(out.join("build.txt"), build.dump(&frames)).map_err(|e| e.to_string())?;
    std::fs::write(
        out.join("taskparams.txt"),
        archive(game, "common.rpf", "data/taskparams.txt", &key)?,
    )
    .map_err(|e| e.to_string())?;
    std::fs::write(
        out.join("weaponinfo.xml"),
        archive(game, "common.rpf", "data/weaponinfo.xml", &key)?,
    )
    .map_err(|e| e.to_string())?;
    let ids: Vec<_> = clips
        .clip_names
        .iter()
        .enumerate()
        .filter_map(|(i, n)| n.starts_with("get_up").then_some(i))
        .collect();
    motions(out, "getup.motion", &clips, &ids)?;
    motions(out, "locomotion.motion", &clips, &[2, 3, 4, 5])?;
    std::fs::write(
        out.join("game-source.txt"),
        format!("Xbox360\ndefault.xex\n{}\n", exe.display()),
    )
    .map_err(|e| e.to_string())?;
    println!(
        "Official SDK Xbox 360 import complete: 21 parts, {} get-up clips, 4 locomotion clips. All resources stay local.",
        ids.len()
    );
    Ok(())
}
fn main() {
    if let Err(e) = run() {
        eprintln!("Xbox 360 import failed: {e}");
        std::process::exit(30)
    }
}
