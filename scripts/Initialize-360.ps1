param([Parameter(Mandatory=$true)][string]$GameDir)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$source=(Resolve-Path -LiteralPath $GameDir).Path
$copy=Join-Path $root 'local/game360'
if(!(Test-Path -LiteralPath $copy)){
    if(Get-ChildItem -LiteralPath $source -Recurse -Force | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}){throw 'Xbox reference contains reparse points; ordinary copies required'}
    Copy-Item -LiteralPath $source -Destination $copy -Recurse
}
foreach($f in @('default.xex','common.rpf','xbox360.rpf','xbox360/anim/anim.img','xbox360/models/cdimages/componentpeds.img')){
    if(!(Test-Path -LiteralPath (Join-Path $copy $f))){throw "Missing Xbox 360 resource: $f"}
}
$env:BO1_SDK_GAME=$copy
$env:BO1_SDK_DATA=Join-Path $root 'local/data'
Push-Location $root
try {
    cargo build --locked --release --bin sdk_import360
    if($LASTEXITCODE){throw 'Xbox importer build failed'}
    & (Join-Path $root 'target/release/sdk_import360.exe') $copy $env:BO1_SDK_DATA
    if($LASTEXITCODE){throw 'Official SDK Xbox 360 import failed'}
    $manifest=@(Get-ChildItem -LiteralPath $copy -Recurse -File | ForEach-Object { @{File=$_.FullName.Substring($copy.Length+1);SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash} })
    $manifest | ConvertTo-Json | Set-Content (Join-Path $root 'local/xbox360-provenance.json')
}finally{Pop-Location}
