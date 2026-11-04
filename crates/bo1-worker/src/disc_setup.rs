use delirium_data::{disc::Disc, disc_kv as kv, xex};
use crate::{disc_motion, recovery::Motions};
use std::{
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    path::{Path, PathBuf},
};

pub fn load(cache: &Path) -> Result<(), String> {
    let file = File::open(cache).map_err(|e| format!("{}: {e}", cache.display()))?;
    let mut data = Vec::with_capacity(kv::CACHE_BYTES);
    file.take((kv::CACHE_BYTES + 1) as u64).read_to_end(&mut data).map_err(|e| e.to_string())?;
    if !kv::restore(&data) {
        return Err(format!("Invalid or outdated Delirium cache {}", cache.display()));
    }
    Ok(())
}

pub fn prepare(game: &Path, cache: &Path) -> Result<(), String> {
    let dir = cache.parent().ok_or("Missing cache directory")?;
    let getup = dir.join("original-getup.motion");
    let locomotion = dir.join("original-locomotion.motion");
    let gta_header = File::open(&getup).and_then(|file| {
        let mut header = [0; 8];
        (&file).read_exact(&mut header)?;
        Ok(header == *b"NMMOTN03")
    }).unwrap_or(false);
    if gta_header && Motions::load(&getup).is_ok() && load(cache).is_ok() {
        println!("Delirium tuning and original GTA IV recovery cache ready.");
        return Ok(());
    }

    let mut images: Vec<PathBuf> = fs::read_dir(game).map_err(|e| e.to_string())?
        .filter_map(Result::ok)
        .map(|entry| entry.path())
        .filter(|path| path.extension().is_some_and(|ext| ext.eq_ignore_ascii_case("iso")) && path.is_file())
        .collect();
    images.sort();
    if images.is_empty() {
        return Err(format!("Place your GTA IV Xbox 360 ISO beside BlackOps.exe in {}", game.display()));
    }

    for path in images {
        match import(&path) {
            Ok((tuning, rise, idle)) => {
                write_cache(&locomotion, &idle)?;
                write_cache(&getup, &rise)?;
                Motions::load(&getup)?;
                write_cache(cache, &tuning)?;
                println!("Delirium tuning and original GTA IV recovery prepared from {}. You can now remove the ISO.", path.display());
                return Ok(());
            }
            Err(error) => eprintln!("{}: {error}", path.display()),
        }
    }
    Err("No compatible GTA IV Xbox 360 disc found. Use the base game disc.".into())
}

fn import(path: &Path) -> Result<(Vec<u8>, Vec<u8>, Vec<u8>), String> {
    let disc = Disc::open(path)?;
    let length = disc.len("default.xex").filter(|&n| n > 4 && n <= 32 * 1024 * 1024)
        .ok_or("No valid default.xex in the disc root")?;
    let xex = disc.read_at("default.xex", 0, length as usize)?;
    if !xex.starts_with(b"XEX2") {
        return Err("default.xex is not an Xbox 360 executable".into());
    }
    let image = xex::XexImage::parse_with_key(&xex, &xex::fetched_key()?)?.image;
    // Unlock only after successful extraction. An earlier ISO with missing
    // resources must not leave its tuning loaded for the next candidate.
    let (rise, idle) = disc_motion::extract(&disc, &image)?;
    if !kv::unlock(&image) {
        return Err("Delirium 0.6.0 does not recognize this disc program".into());
    }
    let tuning = kv::snapshot().ok_or("Tuning import produced no data")?;
    Ok((tuning, rise, idle))
}

fn write_cache(cache: &Path, data: &[u8]) -> Result<(), String> {
    let dir = cache.parent().ok_or("Missing cache directory")?;
    fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    let temporary = cache.with_extension(format!("{}.tmp", std::process::id()));
    let mut output = OpenOptions::new().create_new(true).write(true).open(&temporary).map_err(|e| e.to_string())?;
    let result = output.write_all(data).and_then(|_| output.sync_all());
    drop(output);
    if let Err(error) = result.and_then(|_| fs::rename(&temporary, cache)) {
        let _ = fs::remove_file(&temporary);
        return Err(format!("{}: {error}", cache.display()));
    }
    Ok(())
}
