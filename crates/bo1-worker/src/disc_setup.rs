use crate::{disc_motion, recovery::Motions};
use delirium_data::{disc::Disc, disc_kv as kv, xex};
use std::{
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    path::{Path, PathBuf},
};

/// Exit codes of `--prepare-disc`, read by the installer to explain the cause.
pub const EXIT_NO_IMAGE: i32 = 31;
pub const EXIT_KEY_UNAVAILABLE: i32 = 32;
pub const EXIT_UNSUPPORTED_DISC: i32 = 33;
pub const EXIT_OTHER: i32 = 30;

#[derive(Debug)]
pub struct SetupError {
    pub code: i32,
    pub message: String,
}

impl From<String> for SetupError {
    fn from(message: String) -> Self {
        Self { code: EXIT_OTHER, message }
    }
}

impl From<&str> for SetupError {
    fn from(message: &str) -> Self {
        message.to_string().into()
    }
}

pub fn load(cache: &Path) -> Result<(), String> {
    let file = File::open(cache).map_err(|e| format!("{}: {e}", cache.display()))?;
    let mut data = Vec::with_capacity(kv::CACHE_BYTES);
    file.take((kv::CACHE_BYTES + 1) as u64)
        .read_to_end(&mut data)
        .map_err(|e| e.to_string())?;
    if !kv::restore(&data) {
        return Err(format!(
            "Invalid or outdated Delirium cache {}",
            cache.display()
        ));
    }
    Ok(())
}

fn images(source: &Path) -> Result<Vec<PathBuf>, String> {
    if source.is_file() {
        return Ok(vec![source.to_path_buf()]);
    }
    let mut result: Vec<_> = fs::read_dir(source)
        .map_err(|e| e.to_string())?
        .filter_map(Result::ok)
        .map(|entry| entry.path())
        .filter(|path| path.is_file() && path.extension().is_some_and(|ext| ext.eq_ignore_ascii_case("iso")))
        .collect();
    result.sort();
    Ok(result)
}

pub fn prepare(game: &Path, cache: &Path) -> Result<(), SetupError> {
    let dir = cache.parent().ok_or("Missing cache directory")?;
    let getup = dir.join("original-getup.motion");
    let locomotion = dir.join("original-locomotion.motion");
    let gta_header = File::open(&getup)
        .and_then(|file| {
            let mut header = [0; 8];
            (&file).read_exact(&mut header)?;
            Ok(header == *b"NMMOTN03")
        })
        .unwrap_or(false);
    let marker = dir.join("gta4-development-v1.ready");
    if gta_header
        && profile_digest(cache)
            .ok()
            .is_some_and(|digest| fs::read(&marker).is_ok_and(|stored| stored == digest))
        && Motions::load(&getup).is_ok()
        && load(cache).is_ok()
    {
        println!("Delirium development character and recovery cache ready.");
        return Ok(());
    }

    let images = images(game)?;
    if images.is_empty() {
        return Err(SetupError {
            code: EXIT_NO_IMAGE,
            message: format!("Place your GTA IV Xbox 360 ISO beside BlackOps.exe in {}", game.display()),
        });
    }
    xex::fetched_key().map_err(|message| SetupError { code: EXIT_KEY_UNAVAILABLE, message })?;

    let mut last = String::new();
    for path in images {
        match import(&path) {
            Ok((tuning, resources)) => {
                match fs::remove_file(&marker) {
                    Ok(()) => {}
                    Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
                    Err(error) => return Err(error.to_string().into()),
                }
                write_cache(&locomotion, &resources.locomotion)?;
                write_cache(&getup, &resources.recovery)?;
                write_cache(&dir.join("original-fred.xrt"), &resources.character)?;
                write_cache(&dir.join("fred-build.txt"), &resources.build)?;
                write_cache(&dir.join("original-taskparams.txt"), &resources.tasks)?;
                Motions::load(&getup)?;
                write_cache(cache, &tuning)?;
                write_cache(&marker, &profile_digest(cache)?)?;
                println!(
                    "Delirium development character and recovery prepared from {}. You can now remove the ISO.",
                    path.display()
                );
                return Ok(());
            }
            Err(error) => {
                eprintln!("{}: {error}", path.display());
                last = format!("{}: {error}", path.display());
            }
        }
    }
    Err(SetupError {
        code: EXIT_UNSUPPORTED_DISC,
        message: format!("No compatible GTA IV Xbox 360 disc found. Use the base game disc. Last result: {last}"),
    })
}

fn import(path: &Path) -> Result<(Vec<u8>, disc_motion::DiscResources), String> {
    let disc = Disc::open(path)?;
    let length = disc
        .len("default.xex")
        .filter(|&n| n > 4 && n <= 32 * 1024 * 1024)
        .ok_or("No valid default.xex in the disc root")?;
    let xex = disc.read_at("default.xex", 0, length as usize)?;
    if !xex.starts_with(b"XEX2") {
        return Err("default.xex is not an Xbox 360 executable".into());
    }
    let image = xex::XexImage::parse_with_key(&xex, &xex::fetched_key()?)?.image;
    if !kv::supports(&image) {
        return Err("Delirium does not recognize this base-game disc program".into());
    }
    // Unlock only after successful extraction. An earlier ISO with missing
    // resources must not leave its tuning loaded for the next candidate.
    let resources = disc_motion::extract(&disc, &image)?;
    if !kv::unlock(&image) {
        return Err("Delirium 0.6.0 does not recognize this disc program".into());
    }
    let tuning = kv::snapshot().ok_or("Tuning import produced no data")?;
    Ok((tuning, resources))
}

fn profile_digest(cache: &Path) -> Result<Vec<u8>, String> {
    let dir = cache.parent().ok_or("Missing cache directory")?;
    let mut hash = blake3::Hasher::new();
    hash.update(&fs::read(cache).map_err(|e| e.to_string())?);
    for name in [
        "original-getup.motion",
        "original-locomotion.motion",
        "original-fred.xrt",
        "fred-build.txt",
        "original-taskparams.txt",
    ] {
        let data = fs::read(dir.join(name)).map_err(|e| e.to_string())?;
        if data.is_empty() || data.len() > 16 * 1024 * 1024 {
            return Err(format!("Invalid development cache file {name}"));
        }
        hash.update(name.as_bytes());
        hash.update(&(data.len() as u64).to_le_bytes());
        hash.update(&data);
    }
    Ok(hash.finalize().as_bytes().to_vec())
}

fn write_cache(cache: &Path, data: &[u8]) -> Result<(), String> {
    let dir = cache.parent().ok_or("Missing cache directory")?;
    fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    let temporary = cache.with_extension(format!("{}.tmp", std::process::id()));
    let mut output = OpenOptions::new()
        .create_new(true)
        .write(true)
        .open(&temporary)
        .map_err(|e| e.to_string())?;
    let result = output.write_all(data).and_then(|_| output.sync_all());
    drop(output);
    if let Err(error) = result.and_then(|_| fs::rename(&temporary, cache)) {
        let _ = fs::remove_file(&temporary);
        return Err(format!("{}: {error}", cache.display()));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn selected_iso_can_stay_outside_the_game_directory() {
        let root = std::env::temp_dir().join(format!("bo1-selected-disc-{}", std::process::id()));
        fs::create_dir(&root).unwrap();
        let selected = root.join("Disc.ISO");
        fs::write(&selected, b"selection fixture, not a game resource").unwrap();
        fs::write(root.join("notes.txt"), b"ignored").unwrap();
        assert_eq!(images(&selected).unwrap(), vec![selected.clone()]);
        assert_eq!(images(&root).unwrap(), vec![selected.clone()]);
        assert!(images(&root.join("missing")).is_err());
        fs::remove_file(selected).unwrap();
        fs::remove_file(root.join("notes.txt")).unwrap();
        fs::remove_dir(root).unwrap();
    }

    #[test]
    fn missing_iso_reports_its_own_installer_exit_code() {
        let root = std::env::temp_dir().join(format!("bo1-no-disc-{}", std::process::id()));
        fs::create_dir_all(&root).unwrap();
        fs::write(root.join("notes.txt"), b"not a disc").unwrap();
        let error = prepare(&root, &root.join("cache").join("gta4-xbox360-v060.kv")).unwrap_err();
        assert_eq!(error.code, EXIT_NO_IMAGE);
        assert_eq!(SetupError::from("other").code, EXIT_OTHER);
        fs::remove_file(root.join("notes.txt")).unwrap();
        fs::remove_dir(root).unwrap();
    }

    #[test]
    fn changed_or_incomplete_development_data_requires_preparation() {
        let root = std::env::temp_dir().join(format!("bo1-profile-{}", std::process::id()));
        fs::create_dir_all(&root).unwrap();
        let cache = root.join("gta4-xbox360-v060.kv");
        let names = [
            "gta4-xbox360-v060.kv",
            "original-getup.motion",
            "original-locomotion.motion",
            "original-fred.xrt",
            "fred-build.txt",
            "original-taskparams.txt",
        ];
        for name in names {
            fs::write(root.join(name), name.as_bytes()).unwrap();
        }
        let original = profile_digest(&cache).unwrap();
        fs::write(
            root.join("original-taskparams.txt"),
            b"changed task parameters",
        )
        .unwrap();
        assert_ne!(profile_digest(&cache).unwrap(), original);
        fs::remove_file(root.join("original-fred.xrt")).unwrap();
        assert!(profile_digest(&cache).is_err());
        for name in names {
            let _ = fs::remove_file(root.join(name));
        }
        fs::remove_dir(root).unwrap();
    }
}
