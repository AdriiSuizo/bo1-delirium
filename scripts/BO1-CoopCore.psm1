function Get-CoopMaps([string]$Base) {
    $names=[ordered]@{
        zombie_theater='Kino der Toten';zombie_pentagon='Five';zombie_cosmodrome='Ascension'
        zombie_coast='Call of the Dead';zombie_temple='Shangri-La';zombie_moon='Moon'
        zombie_cod5_prototype='Nacht der Untoten';zombie_cod5_asylum='Verrückt'
        zombie_cod5_sumpf='Shi No Numa';zombie_cod5_factory='Der Riese'
    }
    $files=@{}; $zone=Join-Path $Base 'zone'
    if(Test-Path -LiteralPath $zone){Get-ChildItem -LiteralPath $zone -File -Recurse -Filter '*.ff' | ForEach-Object {$files[$_.BaseName]=$true}}
    # Steam can supply the map fastfiles even when the corresponding DLC
    # image archives are not installed. Check real external images too.
    $required=@{
        zombie_cosmodrome='images/zombie_floodacc_n.iwi'
        zombie_coast='images/~~-gp_zom_lighthouse_door_s-r~6bed6018.iwi'
        zombie_temple='images/~~-gzombie_torch_s-rgb&$white-l.iwi'
        zombie_moon='images/a51_dec_dirtedge_01_n.iwi'
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $images=New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    $main=Join-Path $Base 'main'
    if(Test-Path -LiteralPath $main){foreach($archive in Get-ChildItem -LiteralPath $main -Filter '*.iwd'){
        $zip=$null
        try{$zip=[System.IO.Compression.ZipFile]::OpenRead($archive.FullName);foreach($entry in $zip.Entries){if($required.Values -contains $entry.FullName){[void]$images.Add($entry.FullName)}}}
        catch {} finally{if($zip){$zip.Dispose()}}
    }}
    foreach($id in $names.Keys){if($files.ContainsKey($id) -and $files.ContainsKey($id+'_patch')){
        $missing=if($required.ContainsKey($id) -and !$images.Contains($required[$id])){$required[$id]}else{''}
        [pscustomobject]@{Id=$id;Name=$names[$id];Available=(!$missing);MissingResource=$missing}
    }}
}
function New-CoopLobbyState([string]$Map) {
    [pscustomobject]@{Map=$Map;Ready=@($false,$false);Connected=@($false,$false);Launching=$false;Cancelled=$false}
}
function Set-CoopLobbyMap($State,[string]$Map) {
    if($State.Launching){return}
    if($State.Map -ne $Map){$State.Map=$Map;$State.Ready=@($false,$false)}
}
function Set-CoopLobbyConnected($State,[int]$Player,[bool]$Connected) {
    if($Player -lt 0 -or $Player -gt 1){throw 'Jugador no válido.'}
    $State.Connected[$Player]=$Connected
    if(!$Connected){$State.Ready[$Player]=$false}
}
function Set-CoopLobbyReady($State,[int]$Player,[bool]$Ready) {
    if($Player -lt 0 -or $Player -gt 1){throw 'Jugador no válido.'}
    if(!$State.Launching){$State.Ready[$Player]=$Ready -and $State.Connected[$Player] -and !$State.Cancelled}
}
function Test-CoopLobbyReady($State) {
    !$State.Cancelled -and !$State.Launching -and $State.Ready[0] -and $State.Ready[1] -and $State.Connected[0] -and $State.Connected[1]
}
function Find-CoopPortPair([int]$First=29360,[int]$Last=29450) {
    for($port=$First;$port -le $Last;$port+=2){
        $sockets=@()
        try {
            foreach($number in @($port,($port+1))){
                $socket=New-Object System.Net.Sockets.UdpClient;$sockets+=,$socket
                $socket.Client.ExclusiveAddressUse=$true
                $socket.Client.Bind((New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Loopback,$number)))
            }
            return $port
        } catch {} finally {foreach($socket in $sockets){$socket.Dispose()}}
    }
    throw 'No hay dos puertos locales libres para la partida.'
}
function Get-CoopPlayerConfig([int]$Role,[int]$Pad,[int]$X,[int]$Y,[string]$Map,[int]$Port) {
    if($Role -notin @(1,2) -or $Pad -lt 0 -or $Pad -gt 3 -or $Map -notmatch '^zombie_[a-z0-9_]+$' -or $Port -lt 1024 -or $Port -gt 65534){throw 'Configuración de partida no válida.'}
    $tail=if($Role -eq 1){"set party_maxplayers 2`nset sv_maxclients 2`nset sv_connectTimeout 240`nset onlinegame 0`nset systemlink 1`ndevmap $Map"}else{"set bo1_mod_euphoria_rd 0`nset onlinegame 0`nset systemlink 1`nconnect 127.0.0.1:$Port"}
    @"
exec euphoria_play.cfg
set bo1_mod_sharedconfig 0
set bo1_headless 0
set bo1_testclient 0
set bo1_autoquit 0
set euphoria_selftest 0
set name "Jugador$Role"
set gpad_enabled 1
set gpad_port $Pad
set gpad_background 1
set vid_xpos $X
set vid_ypos $Y
set r_autopriority 0
set com_maxfps 150
set r_vsync 0
set rate 25000
set snaps 20
updategamerprofile
$tail
"@
}
Export-ModuleMember -Function *-Coop*
