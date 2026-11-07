"""Assemble the BO1 Delirium installation package and its RAR archive.

Run from the source root after the BUILD.txt commands, on a clean commit:

    python tools/package_release.py --out ..\\..\\dist --licenses <previous package>\\BO1Delirium\\licenses

The license folder holds third-party license texts (Rust toolchain and crate
licenses); reuse the previous package's folder while Cargo.lock is unchanged.
The script never adds game resources, ISOs, caches or keys.
"""
import argparse
import getpass
import hashlib
import json
import shutil
import socket
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAR = Path(r"C:\Program Files\WinRAR\Rar.exe")

PAYLOAD = {
    "native-build/Release/BO1SteamDelirium.exe": "BO1Delirium/BO1PlutoniumDelirium.exe",
    "native-build/Release/DeliriumBO1.dll": "BO1Delirium/DeliriumBO1.dll",
    "native-build/Release/BO1DeliriumPlugin.dll": "BO1Delirium/runtime/BO1DeliriumPlugin.dll",
    "native-build/Release/nm108_native_bridge.dll": "BO1Delirium/runtime/nm108_native_bridge.dll",
    "native-build/Release/winmm.dll": "BO1Delirium/runtime/winmm.dll",
    "target/release/bo1-delirium-worker.exe": "BO1Delirium/runtime/delirium_worker.exe",
    "capture-build/Release/CaptureBO1.exe": "BO1Delirium/tools/CaptureBO1.exe",
    "capture-build/Release/BO1CharacterCapture.dll": "BO1Delirium/tools/BO1CharacterCapture.dll",
    "native-build/Release/BO1DeliriumSetup.exe": "Instalar - Install BO1 Delirium.exe",
    "native-build/Release/BO1DeliriumUninstall.exe": "Desinstalar - Uninstall BO1 Delirium.exe",
    "LEEME - Espanol.txt": "LEEME - Espanol.txt",
    "README - English.txt": "README - English.txt",
    "CREDITOS - CREDITS.txt": "CREDITOS - CREDITS.txt",
    "NOVEDADES - CHANGELOG.txt": "NOVEDADES - CHANGELOG.txt",
    "VERSION.txt": "VERSION.txt",
}


def run(*args, **kw):
    return subprocess.run(args, cwd=ROOT, check=True, capture_output=True, **kw).stdout


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def personal_data(stage):
    """Files naming the build machine's user or host, inside archives too."""
    words = [getpass.getuser(), socket.gethostname()]
    needles = [w.encode(e) for w in words for e in ("utf-8", "utf-16-le")]
    found = []

    def check(name, data):
        if any(n in data for n in needles):
            found.append(name)

    for path in stage.rglob("*"):
        if path.is_file():
            data = path.read_bytes()
            check(str(path.relative_to(stage)), data)
            if path.suffix.lower() in (".zip", ".iwd"):
                with zipfile.ZipFile(path) as z:
                    for member in z.namelist():
                        check(f"{path.name}!{member}", z.read(member))
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--licenses", required=True, type=Path)
    ap.add_argument("--rust-tests", type=int, required=True, help="number of passing Rust release tests")
    ap.add_argument("--physics-changed", action="store_true", help="this release changes body behaviour")
    args = ap.parse_args()

    if run("git", "status", "--porcelain").strip():
        sys.exit("Commit or set aside changes first: SOURCE.zip must match the exact commit.")
    commit = run("git", "rev-parse", "HEAD").decode().strip()
    version = (ROOT / "VERSION.txt").read_text().strip()
    display = version.replace("-beta.", " Beta ")
    name = "BO1-Delirium-" + display.replace(" ", "-")
    stage = args.out.resolve() / name
    if stage.exists():
        sys.exit(f"{stage} already exists")

    for source, target in PAYLOAD.items():
        destination = stage / target
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / source, destination)

    pyi = args.out.resolve() / f"{name}-pyinstaller"
    run(sys.executable, "-m", "PyInstaller", "--onefile", "--console", "--name", "PrepareBO1",
        "--distpath", str(pyi / "dist"), "--workpath", str(pyi / "build"), "--specpath", str(pyi),
        str(ROOT / "tools/prepare_bo1.py"))
    shutil.copy2(pyi / "dist/PrepareBO1.exe", stage / "BO1Delirium/tools/PrepareBO1.exe")
    shutil.rmtree(pyi)

    (stage / "BO1Delirium/scripts").mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(stage / "BO1Delirium/scripts/bo1delirium_controls.iwd", "w", zipfile.ZIP_DEFLATED) as z:
        z.write(ROOT / "scripts/maps/delirium/controls.gsc", "maps/delirium/controls.gsc")
    (stage / "BO1Delirium/SOURCE.zip").write_bytes(run("git", "archive", "--format=zip", commit))
    shutil.copytree(args.licenses, stage / "BO1Delirium/licenses")

    leaks = personal_data(stage)
    if leaks:
        sys.exit("Personal data in the package, build with the BUILD.txt RUSTFLAGS: " + ", ".join(leaks))

    files = {}
    for path in sorted(p for p in stage.rglob("*") if p.is_file()):
        files[path.relative_to(stage).as_posix()] = {"bytes": path.stat().st_size, "sha256": sha256(path)}
    manifest = {
        "version": version,
        "display_version": display,
        "languages": ["es", "en"],
        "platform": "Plutonium T5 r5369 LAN Zombies",
        "source": commit,
        "delirium_core": "BO1 development runtime",
        "disc_cache": "0.6.0",
        "gta_iv_xbox360_media_ids": ["4A53F9F6", "6AC07221"],
        "checks": {
            "rust_release_tests": args.rust_tests,
            "native_regression_suites": 2,
            "setup_languages": ["es", "en"],
            "uninstall_languages": ["es", "en"],
            "game_mode": "Kino single-player LAN",
            "physics_changed": args.physics_changed,
        },
        "files": files,
    }
    (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    archive = args.out.resolve() / f"{name}.rar"
    subprocess.run([str(RAR), "a", "-r", "-ep1", "-m5", "-idq", str(archive), str(stage / "*")], check=True)
    (args.out.resolve() / f"{name}.rar.sha256").write_text(f"{sha256(archive)}  {archive.name}\n")
    print(archive)


if __name__ == "__main__":
    main()
