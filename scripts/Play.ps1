param([switch]$TwoPlayers,[string]$Map='zombie_theater')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$env:BO1_SDK_DATA=Join-Path $root 'local/data'
$worker=Join-Path $root 'target/release/delirium_worker.exe'
& $worker --verify
if($LASTEXITCODE){throw 'Official SDK preflight failed. Physics migration is blocked; see the SDK error above. No game was launched.'}
if($TwoPlayers){& (Join-Path $PSScriptRoot 'Two-Monitors.ps1') -Start -Map $Map;return}
$run=Join-Path $root 'local/bo1/build/Release'
$art='C:\Program Files (x86)\Steam\steamapps\content\app_12210\depot_12211\GTAIV\pc\data\naturalmotion\Fred.art'
$args="+set bo1_zombies 1 +set fs_b `"$run`" +set fs_h `"$run`" +set logfile 2 +set net_ip 127.0.0.1 +set r_fullscreen 0 +set fs_game mods/euphoria +set fs_mods euphoria +set euphoria_physics 1 +set euphoria_level 0 +set bo1_mod_euphoria_rd 1 +set bo1_mod_euphoria_rd_dll nm108_native_bridge.dll +set bo1_mod_euphoria_rd_art `"$art`" +set gpad_enabled 1 +devmap $Map"
Start-Process -FilePath (Join-Path $run 'BO1Zombies.exe') -ArgumentList $args -WorkingDirectory $run
