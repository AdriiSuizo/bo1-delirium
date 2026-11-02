# BO1 Zombies — official Delirium SDK 0.5.0

The single-player integration now runs with the user's **Xbox 360 GTA IV disc data**. The official SDK accepts its `default.xex`; the earlier PC 1.0.8 fingerprint blocker no longer applies to this setup.

The SDK is an unmodified Cargo Git dependency pinned to [v0.5.0, b0138418f2b51d38038ff7c773df10f252b599d2](https://github.com/PVCPC/delirium-physics/tree/b0138418f2b51d38038ff7c773df10f252b599d2). BO1 stays x86. Its numeric adapter talks to an owned x64 worker calling the public `nm_*` SDK API. Historical `nm108` filenames are retained for ABI compatibility. No original GTA runtime loader or old custom physics engine is used as a fallback.

## Play single-player

On this machine, the independent game, Steam resources and Xbox exports are already prepared. Double-click **Play-SinglePlayer.cmd**, or run:

```powershell
.\scripts\Play.ps1
# Another installed BO1 map:
.\scripts\Play.ps1 -Map zombie_pentagon
```

The launcher verifies SDK initialization before starting the game. Its short config file avoids BO1's command-line length limit. Single-player is the current testing priority; multiplayer/controller validation is deferred.

## Rebuild or prepare another checkout

Windows, Rust stable (2024 edition), CMake, Visual Studio 2022 x86 C++ tools and the June 2010 DirectX SDK are required.

```powershell
# Ordinary independent copies from local read-only references. Never run a
# build in an existing project or share its checkout/build directories.
.\scripts\Initialize-Local.ps1 -Source 'C:\path\to\your\bo1-euphoria'
.\scripts\Initialize-360.ps1 -GameDir 'C:\path\to\your\extracted\Xbox360\GTAIV'
.\scripts\Build.ps1
.\local\bo1\setup.ps1   # Reads the legitimate BO1 Steam installation; writes only this project.
.\scripts\Play.ps1
```

The Xbox input directory needs `default.xex`, `common.rpf`, `xbox360.rpf`, `xbox360/anim/anim.img` and `xbox360/models/cdimages/componentpeds.img`. Setup copies these into ignored `local/game360/`, verifies the executable through the official SDK, and uses the upstream archive/build/animation import routines to export locally:

- `local/data/character.xrt`, `build.txt`, `taskparams.txt`, `weaponinfo.xml`;
- `local/data/getup.motion`: seven ground get-up variants;
- `local/data/locomotion.motion`: four reference clips, including the matching idle reference.

No validation check is bypassed. This machine's accepted Xbox executable SHA-256 is `586F29CDF8C6C06115EC007D2AA3EE4F39C0C22A5A508209138A6DF0EFA0AC37`. Setup can record resource hashes in ignored `local/xbox360-provenance.json`. `BO1_SDK_GAME` and `BO1_SDK_DATA` select these local directories; the launcher sets them.

The local BO1 engine tree and libraries stay under ignored `local/bo1/`. Only authored integration source is committed. The baseline mod adapter was copied from the pre-shotgun backup whose game executable matches the accepted SHA-256 `2591EF423813109961D70FDD55817E74486B4CE1DE415381658B52193125F4F6`. Unverified shotgun worker/bridge edits were not adopted.

## Runtime behavior

Protocol v19 queues and batches up to 32 ordered lifecycle commands, preventing one blocking exchange per activated zombie. Readers use completed private snapshots. Geometry and lifecycle changes finish before each simulation barrier; a persistent pool steps distinct SDK agents against the SDK's synchronized world cache. Tackle tasks that retain another agent's raw pointer are not exposed. The solver remains at 1/60 second; contact settings and simulation time are not reduced to hide overload.

The host retains BO1 walking, running, attacks, wound resistance and health/damage ownership. Per-limb takeover velocities enter the SDK. Physical impulses remain separate from SDK shot-task projectile speed, with duration-based pushes distributed across fixed steps. Ground get-up references drive muscles through SDK messages and bounded impulses; they do not assign recovery body transforms. External impulses and nearby moving bodies wake resting corpses despite the SDK's dead-body resting shortcut. Weapon response data is parsed from the local Xbox export at world initialization.

## Validation on 2026-11-02

- Independent BO1 x86, adapter x86 and official-SDK-worker x64 Release builds pass. Host tests pass for ordered cross-process batches, pending snapshot isolation, weak/consecutive/lethal wound budgeting and torque-preserving pellet merging.
- Official Xbox unlock and fresh 21-part import pass. Four standalone prone/side recovery cases complete, including the retry path.
- A single-player Kino test at round 20 observed genuine ground contact, recovery and **430.64 game units of pursuit over ten seconds**, with zero fallen-state samples during pursuit. Earlier serial-worker runs exposed inconsistent recovery; the final parallel-worker run passed. This is one measured case, not an all-map guarantee.
- Corpse resting and external wake pass: 0.114 m measured displacement after the test impulse. SDK arm severing and fragment lifetime checks pass.
- A packed 20-actor fixture with eight shotgun pellets per actor reacts in the first host frame. Measured host cost: 0.06 ms average, 3.23 ms worst; 207 solver frames complete with zero accumulated debt during the run. The grenade fixture also maintained solver progress. These are bounded fixtures, not universal FPS claims.
- The first Kino shotgun/stairs probe ended with 22 survivors, 19 advancing or in attack range, and none still in a physical response. The repeated-hit probe ended with 22 survivors, 16 advancing or in attack range, and none still in a physical response. Six did not meet the pursuit-distance check; congested stair navigation is not considered resolved.

The repeated-hit probe recorded a worst combat-window frame of 145.1 ms at 1280x720 on a private desktop. Remaining renderer/server frame spikes need further work; the reported 0.5-second hitch was not reproduced in this run. This does not establish a normal-play FPS target.

Pending coverage: longer single-player soaks, more maps, visual skin/contact inspection, repeated ground-to-run transitions in congested stairs, and broader lethal/gib scenarios. No original GTA IV/Euphoria equivalence or universal frame-rate target is claimed. Two-player and controller/monitor testing is deferred at the user's request.

```powershell
.\scripts\Build.ps1 -SkipGame
.\target\release\delirium_worker.exe --verify
.\target\release\delirium_worker.exe --recover
.\target\release\delirium_worker.exe --lifecycle
# SDK crowd fixture (set BO1_SDK_GAME / BO1_SDK_DATA first):
Copy-Item target/release/delirium_worker.exe local/adapter-build/Release/
.\local\adapter-build\Release\crowd_sdk.exe 20 .45 shotgun
```

All proprietary executables, archives, models, textures, sounds, ART/XRT resources, exported values and animation caches stay under ignored `local/`. The private repository contains only permitted source, licenses and build/test instructions. Preserve the upstream GPL-3.0-only license and attribution in `NOTICE`.
