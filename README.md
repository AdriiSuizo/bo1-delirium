# BO1 Delirium SDK 0.5.0 migration

**Incomplete migration: builds pass; the required GTA IV PC 1.0.8 executable is rejected by the official SDK. No playable SDK build is certified.**

This is a separate, source-only integration project. The unmodified official SDK is a Cargo Git dependency pinned to [v0.5.0, b0138418f2b51d38038ff7c773df10f252b599d2](https://github.com/PVCPC/delirium-physics/tree/b0138418f2b51d38038ff7c773df10f252b599d2). BO1 remains x86; the owned x64 worker calls the public SDK `nm_*` API. No older custom simulation is used as an SDK fallback. Historical `nm108` names remain for the BO1 adapter ABI.

## Current blocker

`delirium_worker --verify` exits 30 at `nm_unlock`, before character initialization or simulation. The only permitted local input is:

```
C:\Program Files (x86)\Steam\steamapps\content\app_12210\depot_12211\GTAIV\GTAIV.exe
```

Its size is 15,628,696 bytes; SHA-256 is `3D90E7C516FA450CA002E5031E62C0F66B404590F33E6CC9793B0DA4FFFBFD0F`. The SDK's PC `kv.map` expects a different fingerprint and 17,425,752 bytes. `crates/delirium-data/src/kv.rs` checks both length and BLAKE3; this project preserves that check. A verified official map for this installation is needed to continue. Do not substitute another installation, disable the check, supply guessed constants or restore the old custom worker.

## Local layout and build

Windows, Rust stable (2024 edition), CMake, Visual Studio 2022 C++ x86 tools and June 2010 DirectX SDK are required. Commands run from this repository:

```powershell
# For a fresh checkout: ordinary independent copies, no shared checkout or links.
.\scripts\Initialize-Local.ps1 -Source 'C:\path\to\your\bo1-euphoria'
.\scripts\Build.ps1
# Copy BO1 resources from the legitimate Steam install to this project's run directory.
.\local\bo1\setup.ps1
# Runs SDK preflight first; currently refuses to launch because of the blocker.
.\scripts\Play.ps1
.\scripts\Play.ps1 -TwoPlayers
```

`local/bo1/` contains the independent decompiled engine source, libraries, build output and local Steam resources. It is excluded from Git. `overlay/cg_euphoria_rd.cpp` is the authored BO1 mod adapter selected from the pre-shotgun backup that carried the validated game executable (baseline SHA-256 `2591EF423813109961D70FDD55817E74486B4CE1DE415381658B52193125F4F6`). Unverified shotgun worker/bridge changes were not adopted. The built independent game SHA-256 is `EFA525D90B7CB926A55AA91E2175568ABCD9E4FDCBA3B3852A38B077B9AC985E`.

The worker expects official exports in `local/data/`: `character.xrt`, `build.txt`, `taskparams.txt`, plus the local get-up motor-reference cache `getup.motion`. `BO1_SDK_DATA` selects that directory. It is intentionally empty: reference caches with unverified export provenance were not promoted to official SDK inputs. See the official [start guide](https://github.com/PVCPC/delirium-physics/blob/v0.5.0/docs/start-here.md) and [integration reference](https://github.com/PVCPC/delirium-physics/blob/v0.5.0/docs/modding.md). Export and recovery-cache provenance must be validated after the executable support issue is resolved.

## Implementation and validation

The adapter retains host movement/attack code, per-limb initial velocity, interpolation, wound budgeting and original health/damage ownership. Ordered protocol v19 batches up to 32 lifecycle commands per asynchronous exchange. Creation/mode/death/gib requests queue instead of making per-actor solver waits; readers keep a private completed snapshot. The worker keeps a fixed 1/60-second step and uses SDK messages/impulses for recovery motor references rather than assigning body transforms during get-up. These are implemented paths, **not validated gameplay results**.

Completed on 2026-11-02:

- Independent BO1 x86 Release build; x86 DLL and x64 SDK-worker Release builds.
- Host tests: 32-command cross-process batch ordering, pending snapshot isolation and lifetime closure; weak/consecutive wound accumulation and lethal trigger; parallel impulse merging with torque preservation and divergent-pellet rejection; basic recovery refusal guard.
- Measured IPC fixture enqueue: 0.015 ms on this run. This is not a game-frame or physics performance measurement.
- SDK preflight: **failed**, rejected the approved executable. No game was launched through the migration launcher.

Still required: SDK export/provenance checks, genuine ground get-up to BO1 full-speed pursuit, weak/consecutive/lethal reactions in-game, shotgun crowds/Kino stairs, physical momentum exchange, wakeable sleeping corpses, dismemberment, renderer cadence/frame spikes and two-player/controller/monitor consistency. The official SDK's body contact and recovery behavior, the metadata rig conversion, corpse wake/sleep, fragments, gravity and all recovery tuning remain unverified in this adapter. The copied dual-monitor launcher is retained but has not been exercised with this SDK.

```powershell
.\scripts\Build.ps1 -SkipGame
ctest --test-dir local/adapter-build -C Release --output-on-failure
.\target\release\delirium_worker.exe --verify  # currently expected to fail; do not mark as PASS
python scripts/audit_source.py                 # audit the exact staged source before upload
```

No all-map stability, frame-rate target, shotgun-hitch fix or original GTA IV/Euphoria equivalence is claimed.

## Source and local resources

The repository includes authored integration source and GPL notices, not the decompiled engine tree, game executable, proprietary binaries, ART/XRT data, animation caches, models, textures, audio, resource archives or research dumps. Keep all such inputs under ignored `local/`. Never upload existing game repository history. Preserve the SDK's GPL-3.0-only license and attribution; see `NOTICE`.
