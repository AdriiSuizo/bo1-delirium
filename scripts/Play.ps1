param([switch]$TwoPlayers,[string]$Map='zombie_theater')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$env:BO1_SDK_DATA=Join-Path $root 'local/data'
$env:BO1_SDK_GAME=Join-Path $root 'local/game360'
$worker=Join-Path $root 'target/release/delirium_worker.exe'
& $worker --verify
if($LASTEXITCODE){throw 'Official SDK preflight failed. Physics migration is blocked; see the SDK error above. No game was launched.'}
if($TwoPlayers){& (Join-Path $PSScriptRoot 'Two-Monitors.ps1') -Start -Map $Map;return}
$run=Join-Path $root 'local/bo1/build/Release'
$art=Join-Path $env:BO1_SDK_DATA 'character.xrt'
$cfg=@"
exec euphoria_play.cfg
set bo1_headless 0
set bo1_testclient 0
set bo1_autoquit 0
set bo1_mod_euphoria_rd 1
set bo1_mod_euphoria_rd_dll nm108_native_bridge.dll
set bo1_mod_euphoria_rd_art "$art"
set gpad_enabled 1
devmap $Map
"@
[IO.File]::WriteAllText((Join-Path $run 'main/sdk_play.cfg'),$cfg)
$args="+set bo1_zombies 1 +set fs_b `"$run`" +set fs_h . +set fs_game mods/euphoria +set fs_mods euphoria +set logfile 1 +set net_ip 127.0.0.1 +exec sdk_play.cfg"
Start-Process -FilePath (Join-Path $run 'BO1Zombies.exe') -ArgumentList $args -WorkingDirectory $run
