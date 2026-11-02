param([switch]$SkipGame)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
  cargo build --locked --release
  if($LASTEXITCODE){throw 'SDK worker build failed'}
  cmake -S host -B local/adapter-build -A Win32
  if($LASTEXITCODE){throw 'Adapter configure failed'}
  cmake --build local/adapter-build --config Release
  if($LASTEXITCODE){throw 'Adapter build failed'}
  ctest --test-dir local/adapter-build -C Release --output-on-failure
  if($LASTEXITCODE){throw 'Host tests failed'}
  if(!$SkipGame){
    if(!(Test-Path local/bo1/CMakeLists.txt)){throw 'Run Initialize-Local.ps1 with your local BO1 source first'}
    cmake -S local/bo1 -B local/bo1/build -A Win32
    if($LASTEXITCODE){throw 'Game configure failed'}
    cmake --build local/bo1/build --config Release --parallel 8
    if($LASTEXITCODE){throw 'Game build failed'}
    $run=Join-Path $root 'local/bo1/build/Release'
    Copy-Item local/adapter-build/Release/nm108_native_bridge.dll $run -Force
    Copy-Item local/adapter-build/Release/nm108_native_bridge.dll (Join-Path $run 'ragdoll_ffi.dll') -Force
    Copy-Item target/release/delirium_worker.exe $run -Force
  }
} finally {Pop-Location}
