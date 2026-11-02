param([Parameter(Mandatory=$true)][string]$Source)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$sourceRoot=(Resolve-Path -LiteralPath $Source).Path
$dest=Join-Path $root 'local/bo1'
if(Test-Path -LiteralPath $dest){throw 'local/bo1 already exists; refusing to overwrite an independent project'}
if($sourceRoot.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)){throw 'Source must be an external read-only reference'}
New-Item -ItemType Directory -Path $dest -Force|Out-Null
foreach($name in @('src','tl','DemonWare','mods','data','tools','tests')){
  $src=Join-Path $sourceRoot $name
  if(Get-ChildItem -LiteralPath $src -Recurse -Force | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}){throw "Reference $name contains a reparse point; ordinary copies required"}
  Copy-Item -LiteralPath $src -Destination (Join-Path $dest $name) -Recurse
}
foreach($name in @('CMakeLists.txt','cmake_files.cmake','setup.ps1')){Copy-Item -LiteralPath (Join-Path $sourceRoot $name) -Destination (Join-Path $dest $name)}
# Apply only authored overlay files. The decompiled engine tree never enters Git.
Copy-Item -LiteralPath (Join-Path $root 'overlay/cg_euphoria_rd.cpp') -Destination (Join-Path $dest 'src/cgame_mp/cg_euphoria_rd.cpp') -Force
Write-Output "Independent local copy ready: $dest"
