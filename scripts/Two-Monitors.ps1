param([switch]$Solo,[switch]$Start,[switch]$ValidateOnly,[switch]$PrepareOnly,[switch]$ListMaps,[switch]$SelfTest,[int]$Monitor1=0,[int]$Monitor2=1,[int]$Pad1=0,[int]$Pad2=1,[string]$Map='zombie_theater')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$repo=Split-Path -Parent $PSScriptRoot
$base=Join-Path $repo 'local/bo1/build/Release'
Import-Module (Join-Path $PSScriptRoot 'BO1-CoopCore.psm1') -Force
$catalog=@(Get-CoopMaps $base)
$maps=@($catalog | Where-Object {$_.Available})
if($ListMaps){$catalog | Format-Table Id,Name,Available,MissingResource -AutoSize | Out-String -Width 200 | Write-Output;exit 0}
$script:lobby=$null
function Set-LaunchStatus([string]$Text){
    if($script:lobby){foreach($label in $script:lobby.Progress){$label.Text=$Text};[System.Windows.Forms.Application]::DoEvents()}
}
function Test-LaunchCancelled {if($script:lobby -and $script:lobby.State.Cancelled){throw 'Preparación cancelada. Las partidas que ya se hayan abierto siguen en sus pantallas.'}}
$screens=@([System.Windows.Forms.Screen]::AllScreens | Sort-Object @{Expression={if($_.Bounds.Width -eq 1920 -and $_.Bounds.Height -eq 1080){0}else{1}}},@{Expression={$_.Bounds.X}})
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class BO1LocalPads {
 [DllImport("xinput1_3.dll")] static extern uint XInputGetState(uint slot,IntPtr state);
 [DllImport("xinput1_3.dll")] static extern uint XInputGetCapabilities(uint slot,uint flags,IntPtr caps);
 public static bool Present(uint slot){IntPtr p=Marshal.AllocHGlobal(64);try{return XInputGetCapabilities(slot,0,p)==0;}finally{Marshal.FreeHGlobal(p);}}
 public static int Buttons(uint slot){IntPtr p=Marshal.AllocHGlobal(32);try{return XInputGetState(slot,p)==0?(ushort)Marshal.ReadInt16(p,4):-1;}finally{Marshal.FreeHGlobal(p);}}
 [DllImport("kernel32.dll",CharSet=CharSet.Ansi)] static extern IntPtr OpenFileMapping(uint access,bool inherit,string name);
 [DllImport("kernel32.dll")] static extern IntPtr MapViewOfFile(IntPtr handle,uint access,uint high,uint low,UIntPtr bytes);
 [DllImport("kernel32.dll")] static extern bool UnmapViewOfFile(IntPtr view);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
 public static bool Ready(string token,int pid,string map){uint key=2166136261;foreach(char c in map){key^=(byte)c;key=unchecked(key*16777619);}var h=OpenFileMapping(4,false,"Local\\BO1EuphoriaCoop-"+token);if(h==IntPtr.Zero)return false;var p=MapViewOfFile(h,4,0,0,(UIntPtr)16);try{return p!=IntPtr.Zero&&Marshal.ReadInt32(p)==0x32434d4e&&Marshal.ReadInt32(p,4)==4&&Marshal.ReadInt32(p,8)==pid&&unchecked((uint)Marshal.ReadInt32(p,12))==key;}finally{if(p!=IntPtr.Zero)UnmapViewOfFile(p);CloseHandle(h);}}
}
'@
$pads=@(0..3 | Where-Object {[BO1LocalPads]::Present($_)})
function Prepare-Player([string]$directory){
    New-Item -ItemType Directory -Path $directory -Force|Out-Null
    $running=@(Get-Process BO1Zombies -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq (Join-Path $directory 'BO1Zombies.exe')})
    if($running.Count){throw 'Ya hay una partida de dos monitores abierta. Ciérrala antes de iniciar otra.'}
    foreach($file in Get-ChildItem -LiteralPath $base -File | Where-Object {$_.Extension -in @('.exe','.dll','.txt','.bmp')}){
        $target=Join-Path $directory $file.Name
        if(!(Test-Path -LiteralPath $target) -or (Get-Item -LiteralPath $target).LastWriteTimeUtc -ne $file.LastWriteTimeUtc){Copy-Item -LiteralPath $file.FullName -Destination $target}
    }
    New-Item -ItemType Directory -Path (Join-Path $directory 'main') -Force|Out-Null
    foreach($relative in @('zone','main/video')){
        $source=Join-Path $base $relative;$target=Join-Path $directory $relative
        if((Test-Path -LiteralPath $source) -and !(Test-Path -LiteralPath $target)){New-Item -ItemType Junction -Path $target -Target $source|Out-Null}
    }
    $source=Join-Path $base 'mods/euphoria';$dest=Join-Path $directory 'mods/euphoria'
    foreach($file in Get-ChildItem -LiteralPath $source -File -Recurse | Where-Object {$_.Extension -in @('.gsc','.csc','.cfg','.csv','.menu','.iwd')}){
        $target=Join-Path $dest $file.FullName.Substring($source.Length+1)
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force|Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
    }
    $profiles=Join-Path $directory 'players'
    if(!(Test-Path -LiteralPath $profiles)){
        $sourceProfiles=Join-Path $base 'players'
        if(Test-Path -LiteralPath $sourceProfiles){Copy-Item -LiteralPath $sourceProfiles -Destination $profiles -Recurse}
    }
}
function Start-LocalCoop {
    if($screens.Count -lt 2){throw 'Conecta y activa los dos monitores en Windows.'}
    if($pads.Count -lt 2){throw 'Conecta los dos mandos Xbox antes de iniciar.'}
    if($Monitor1 -eq $Monitor2 -or $Monitor1 -lt 0 -or $Monitor2 -lt 0 -or $Monitor1 -ge $screens.Count -or $Monitor2 -ge $screens.Count){throw 'Selecciona dos monitores diferentes.'}
    if($Pad1 -eq $Pad2 -or $Pad1 -notin $pads -or $Pad2 -notin $pads){throw 'Selecciona dos mandos conectados y diferentes.'}
    $selected=@($maps | Where-Object {$_.Id -eq $Map})
    if($selected.Count -ne 1){throw 'El mapa no está instalado o no es un mapa de Zombies compatible con esta sala.'}
    $connectedScreens=@([System.Windows.Forms.Screen]::AllScreens | ForEach-Object {$_.DeviceName})
    foreach($index in @($Monitor1,$Monitor2)){if($screens[$index].DeviceName -notin $connectedScreens){throw 'Se ha desconectado uno de los monitores. Vuelve a preparar la sala.'}}
    foreach($pad in @($Pad1,$Pad2)){if(![BO1LocalPads]::Present($pad)){throw 'Se ha desconectado un mando Xbox. Vuelve a prepararlo.'}}
    foreach($required in @('BO1Zombies.exe','delirium_worker.exe','nm108_native_bridge.dll')){if(!(Test-Path -LiteralPath (Join-Path $base $required))){throw "Falta $required; compila el proyecto primero."}}
    $env:BO1_SDK_GAME=Join-Path $repo 'local/game360'
    if(!(Test-Path (Join-Path $env:BO1_SDK_GAME 'default.xex'))){throw 'Missing local Xbox 360 copy; run Initialize-360.ps1'}
    $env:NM108_GETUP='1'
    $env:BO1_PHYSICS_BACKEND='delirium'
    $env:BO1_DELIRIUM_CONTINUOUS='0'
    $env:BO1_SDK_DATA=Join-Path $repo 'local/data'
    $env:NM108_TASK_PARAMS=Join-Path $env:BO1_SDK_DATA 'taskparams.txt'
    foreach($cache in @($env:NM108_TASK_PARAMS)){if(!(Test-Path -LiteralPath $cache)){throw 'Falta una caché local de físicas GTA IV.'}}
    if($ValidateOnly){
        "Mapa: $($selected[0].Name) ($Map)"
        for($i=0;$i -lt 2;$i++){$monitor=@($Monitor1,$Monitor2)[$i];$pad=@($Pad1,$Pad2)[$i];$b=$screens[$monitor].Bounds;"Jugador $($i+1): $($screens[$monitor].DeviceName) $($b.Width)x$($b.Height) [$($b.X),$($b.Y)], Xbox $($pad+1)"}
        return
    }
    & (Join-Path $base 'delirium_worker.exe') --verify
    if($LASTEXITCODE){throw 'Official SDK preflight failed; no game launched'}
    $session=[Guid]::NewGuid().ToString('N');$root=Join-Path $repo 'local/bo1/build/local-coop'
    $dirs=@((Join-Path $root 'jugador1'),(Join-Path $root 'jugador2'))
    Set-LaunchStatus 'Preparando los archivos de los dos jugadores…'
    foreach($dir in $dirs){Prepare-Player $dir}
    $serverPort=Find-CoopPortPair
    $guestReady=New-Object System.Threading.EventWaitHandle($false,[System.Threading.EventResetMode]::ManualReset,"Local\BO1EuphoriaCoop-$session-guest-ready")
    $hostProcess=$null;$guestProcess=$null
    try {
    for($i=0;$i -lt 2;$i++){
        $role=$i+1;$pad=@($Pad1,$Pad2)[$i];$monitor=$screens[@($Monitor1,$Monitor2)[$i]];$b=$monitor.Bounds
        $cfg=Get-CoopPlayerConfig -Role $role -Pad $pad -X ($b.X+8) -Y ($b.Y+8) -Map $Map -Port $serverPort
        $sdkCfg='exec euphoria_play.cfg'+[Environment]::NewLine+'set bo1_mod_euphoria_rd_dll nm108_native_bridge.dll'+[Environment]::NewLine+'set bo1_mod_euphoria_rd_art "'+(Join-Path $env:BO1_SDK_DATA 'character.xrt')+'"'
        $cfg=$cfg.Replace('exec euphoria_play.cfg',$sdkCfg)
        [System.IO.File]::WriteAllText((Join-Path $dirs[$i] 'main/local_coop.cfg'),$cfg,(New-Object System.Text.UTF8Encoding($false)))
        $port=$serverPort+$i
        $arguments="+set bo1_zombies 1 +set fs_b `"$base`" +set fs_h . +set fs_game mods/euphoria +set fs_mods euphoria +set net_ip 127.0.0.1 +set net_port $port +set logfile 1 +set bo1_localcoop_role $role +set bo1_localcoop_session $session +set gpad_port $pad +set gpad_background 1 +set r_fullscreen 0 +set r_borderless 1 +set vid_xpos $($b.X+8) +set vid_ypos $($b.Y+8) +exec local_coop.cfg"
        if($arguments.Length -gt 1020){throw 'La ruta del proyecto hace demasiado larga la orden de inicio.'}
        if($PrepareOnly){"Preparado jugador $role, mando $($pad+1), $($b.Width)x$($b.Height), argumentos $($arguments.Length) caracteres";continue}
        Test-LaunchCancelled
        Set-LaunchStatus $(if($i -eq 0){"Cargando $($selected[0].Name) y sus físicas…"}else{'Conectando al jugador 2…'})
        Test-LaunchCancelled
        $process=Start-Process -FilePath (Join-Path $dirs[$i] 'BO1Zombies.exe') -ArgumentList $arguments -WorkingDirectory $dirs[$i] -PassThru -WindowStyle Normal
        if($i -eq 0){
            $hostProcess=$process;$deadline=[DateTime]::UtcNow.AddSeconds(180)
            while(![BO1LocalPads]::Ready($session,$process.Id,$Map)){
                Test-LaunchCancelled
                if($process.HasExited){throw 'El anfitrión no ha podido iniciar. Revisa la instalación local.'}
                if([DateTime]::UtcNow -gt $deadline){throw 'La primera vista sigue cargando. Espera a que termine antes de iniciar otra partida.'}
                [System.Windows.Forms.Application]::DoEvents();Start-Sleep -Milliseconds 100
            }
        }else{$guestProcess=$process}
    }
    if(!$PrepareOnly){
        $deadline=[DateTime]::UtcNow.AddSeconds(180)
        while(!$guestReady.WaitOne(0)){
            Test-LaunchCancelled
            if($hostProcess.HasExited -or $guestProcess.HasExited){throw 'Uno de los juegos se ha cerrado antes de completar la conexión. Revisa su registro en build/local-coop.'}
            if([DateTime]::UtcNow -gt $deadline){throw 'No se ha confirmado la entrada del jugador 2. Los juegos abiertos siguen en sus pantallas; ciérralos antes de volver a iniciar.'}
            [System.Windows.Forms.Application]::DoEvents();Start-Sleep -Milliseconds 100
        }
        Set-LaunchStatus 'Los dos jugadores están dentro. ¡Buena partida!'
    }
    }finally{$guestReady.Dispose()}
}
function Update-CoopRoom {
    $room=$script:lobby;if(!$room -or $room.Closing){return}
    $index=0;for($i=0;$i -lt $maps.Count;$i++){if($maps[$i].Id -eq $room.State.Map){$index=$i;break}}
    $room.Syncing=$true
    for($i=0;$i -lt 2;$i++){
        $room.Maps[$i].SelectedIndex=$index
        $room.Maps[$i].Enabled=$i -eq 0 -and !$room.State.Launching
        $room.ReadyButtons[$i].Text=if($room.State.Ready[$i]){'No estoy listo'}else{'Estoy listo (A)'}
        $room.ReadyButtons[$i].Enabled=$room.State.Connected[$i] -and $room.ScreensValid -and !$room.State.Launching
        $lines=@();for($p=0;$p -lt 2;$p++){$state=if(!$room.State.Connected[$p]){'Mando desconectado'}elseif($room.State.Ready[$p]){'LISTO'}else{'Esperando confirmación'};$lines+="Jugador $($p+1): $state"}
        $room.Players[$i].Text=$lines -join "`n"
        if(!$room.State.Launching){$room.Progress[$i].Text=if(!$room.ScreensValid){'Falta uno de los monitores. Vuelve a la preparación.'}else{'La partida empieza cuando los dos estén listos.'}}
    }
    $room.Syncing=$false
}
function Close-CoopRoom([bool]$Success,$ClosingWindow=$null){
    $room=$script:lobby;if(!$room -or $room.Closing){return};$room.Closing=$true
    $room.Timer.Stop();$room.Timer.Dispose()
    foreach($window in $room.Forms){if($window -ne $ClosingWindow){$window.Close();$window.Dispose()}}
    $script:lobby=$null
    if($room.Root){if($Success){$room.Root.Close()}else{$room.Root.Show()}}
}
function Start-CoopRoomGame {
    $room=$script:lobby
    if(!$room -or !$room.ScreensValid -or !(Test-CoopLobbyReady $room.State)){return}
    if($room.Testing){$room.LaunchRequested=$true;return}
    $room.State.Launching=$true;$room.Timer.Stop();Update-CoopRoom
    $script:Map=$room.State.Map
    try {Start-LocalCoop;Close-CoopRoom $true}
    catch {[void][System.Windows.Forms.MessageBox]::Show($room.Forms[0],$_.Exception.Message,'BO1 Zombies');Close-CoopRoom $false}
}
function Toggle-CoopRoomReady([int]$Player){
    $room=$script:lobby;if(!$room -or $room.State.Launching){return}
    Set-CoopLobbyReady $room.State $Player (!$room.State.Ready[$Player]);Update-CoopRoom;Start-CoopRoomGame
}
function Invoke-CoopRoomButtons([int]$Player,[int]$Buttons){
    $room=$script:lobby;if(!$room -or $room.State.Launching){return}
    $last=$room.LastButtons[$Player];$room.LastButtons[$Player]=$Buttons
    Set-CoopLobbyConnected $room.State $Player ($Buttons -ge 0)
    if($Buttons -lt 0 -or $last -lt 0){return}
    $pressed=$Buttons -band (-bnot $last)
    if($pressed -band 0x2000){
        if($room.State.Ready[$Player]){Set-CoopLobbyReady $room.State $Player $false}
        else{$room.State.Cancelled=$true;Close-CoopRoom $false};return
    }
    if($Player -eq 0 -and ($pressed -band 12)){
        $step=if($pressed -band 8){1}else{-1};$index=$room.Maps[0].SelectedIndex
        $index=($index+$step+$maps.Count)%$maps.Count;Set-CoopLobbyMap $room.State $maps[$index].Id;Update-CoopRoom
    }
    if($pressed -band 0x1000){Toggle-CoopRoomReady $Player}
}
function Open-CoopRoom($Root,[bool]$Testing=$false){
    $state=New-CoopLobbyState $Map
    $script:lobby=[pscustomobject]@{State=$state;Root=$Root;Forms=@();Maps=@();Players=@();Progress=@();ReadyButtons=@();LastButtons=@(-1,-1);Syncing=$false;Closing=$false;ScreensValid=$true;Testing=$Testing;LaunchRequested=$false;Timer=(New-Object System.Windows.Forms.Timer)}
    $room=$script:lobby
    for($i=0;$i -lt 2;$i++){
        $player=$i;$pad=@($Pad1,$Pad2)[$i];$screen=$screens[@($Monitor1,$Monitor2)[$i]];$b=$screen.WorkingArea
        $window=New-Object System.Windows.Forms.Form;$window.Text="BO1 Zombies · Jugador $($i+1)";$window.ClientSize=New-Object System.Drawing.Size(720,430)
        $window.StartPosition='Manual';$window.Location=New-Object System.Drawing.Point(($b.X+[Math]::Max(0,($b.Width-$window.Width)/2)),($b.Y+[Math]::Max(0,($b.Height-$window.Height)/2)))
        $window.FormBorderStyle='FixedDialog';$window.MaximizeBox=$false;$window.TopMost=$true;$window.BackColor=[System.Drawing.Color]::FromArgb(20,26,32);$window.ForeColor=[System.Drawing.Color]::WhiteSmoke;$window.Font=New-Object System.Drawing.Font('Segoe UI',12)
        $title=New-Object System.Windows.Forms.Label;$title.Text="SALA ZOMBIES · JUGADOR $($i+1)";$title.SetBounds(28,22,660,38);$title.Font=New-Object System.Drawing.Font('Segoe UI',18,[System.Drawing.FontStyle]::Bold);$window.Controls.Add($title)
        $caption=New-Object System.Windows.Forms.Label;$caption.Text="Mando Xbox $($pad+1) · Ronda 10 · Puntos normales";$caption.SetBounds(30,66,650,28);$window.Controls.Add($caption)
        $mapBox=New-Object System.Windows.Forms.ComboBox;$mapBox.DropDownStyle='DropDownList';$mapBox.SetBounds(30,110,660,36)
        foreach($entry in $maps){[void]$mapBox.Items.Add($entry.Name)};$window.Controls.Add($mapBox)
        $players=New-Object System.Windows.Forms.Label;$players.SetBounds(30,165,650,72);$window.Controls.Add($players)
        $progress=New-Object System.Windows.Forms.Label;$progress.SetBounds(30,245,650,58);$window.Controls.Add($progress)
        $legend=New-Object System.Windows.Forms.Label;$legend.Text=if($i -eq 0){'← → Elegir mapa · A Listo · B Volver / quitar listo'}else{'A Listo · B Volver / quitar listo · El jugador 1 elige el mapa'};$legend.SetBounds(30,307,660,32);$legend.Font=New-Object System.Drawing.Font('Segoe UI',10);$window.Controls.Add($legend)
        $ready=New-Object System.Windows.Forms.Button;$ready.SetBounds(400,355,285,45);$ready.ForeColor=[System.Drawing.Color]::Black;$window.Controls.Add($ready)
        $back=New-Object System.Windows.Forms.Button;$back.Text='Volver';$back.SetBounds(30,355,140,45);$back.ForeColor=[System.Drawing.Color]::Black;$window.Controls.Add($back)
        # A dynamic-module closure cannot resolve this script's room functions.
        # Keep the player on the control and run in the original script scope.
        $ready.Tag=$player
        $ready.Add_Click({param($sender,$eventArgs) Toggle-CoopRoomReady ([int]$sender.Tag)})
        $back.Add_Click({if($script:lobby.State.Launching){$script:lobby.State.Cancelled=$true}else{Close-CoopRoom $false}})
        $window.Add_FormClosing({param($sender,$eventArgs) if($script:lobby -and !$script:lobby.Closing){if($script:lobby.State.Launching){$eventArgs.Cancel=$true;$script:lobby.State.Cancelled=$true}else{Close-CoopRoom $false $sender}}})
        if($i -eq 0){$mapBox.Add_SelectedIndexChanged({if($script:lobby -and !$script:lobby.Syncing -and $script:lobby.Maps[0].SelectedIndex -ge 0){Set-CoopLobbyMap $script:lobby.State $maps[$script:lobby.Maps[0].SelectedIndex].Id;Update-CoopRoom}})}
        $room.Forms+=,$window;$room.Maps+=,$mapBox;$room.Players+=,$players;$room.Progress+=,$progress;$room.ReadyButtons+=,$ready
    }
    Update-CoopRoom
    $room.Timer.Interval=100
    $room.Timer.Add_Tick({
        $room=$script:lobby;if(!$room -or $room.State.Launching){return}
        $devices=@([System.Windows.Forms.Screen]::AllScreens | ForEach-Object {$_.DeviceName});$room.ScreensValid=$true
        foreach($index in @($Monitor1,$Monitor2)){if($screens[$index].DeviceName -notin $devices){$room.ScreensValid=$false;$room.State.Ready=@($false,$false)}}
        for($p=0;$p -lt 2;$p++){Invoke-CoopRoomButtons $p ([BO1LocalPads]::Buttons(@($Pad1,$Pad2)[$p]));if(!$script:lobby){return}}
        Update-CoopRoom;Start-CoopRoomGame
    })
    if(!$Testing){if($Root){$Root.Hide()};foreach($window in $room.Forms){$window.Show()};$room.Timer.Start()}
}
try {
    if($Solo){
        $form=New-Object System.Windows.Forms.Form;$form.Text='BO1 Zombies - elegir mapa';$form.ClientSize=New-Object System.Drawing.Size(620,250);$form.StartPosition='CenterScreen';$form.FormBorderStyle='FixedDialog';$form.MaximizeBox=$false;$form.Font=New-Object System.Drawing.Font('Segoe UI',11)
        $label=New-Object System.Windows.Forms.Label;$label.Text='Un jugador · Ronda 10 · Puntos normales';$label.SetBounds(20,20,580,30);$form.Controls.Add($label)
        $choice=New-Object System.Windows.Forms.ComboBox;$choice.DropDownStyle='DropDownList';$choice.SetBounds(20,65,580,32);foreach($entry in $maps){[void]$choice.Items.Add($entry.Name)};if($maps.Count){$choice.SelectedIndex=0};$form.Controls.Add($choice)
        $note=New-Object System.Windows.Forms.Label;$note.SetBounds(20,110,580,65);$note.Text='Físicas activas en el mapa seleccionado.';$missing=@($catalog | Where-Object {!$_.Available} | ForEach-Object {$_.Name});if($missing.Count){$note.Text+="`nFaltan recursos DLC: "+($missing -join ', ')};$form.Controls.Add($note)
        $play=New-Object System.Windows.Forms.Button;$play.Text='Jugar';$play.SetBounds(430,190,170,40);$play.Enabled=$maps.Count -gt 0;$form.Controls.Add($play);$form.AcceptButton=$play
        $play.Add_Click({
            try{
                if($choice.SelectedIndex -lt 0){return};$selected=$maps[$choice.SelectedIndex].Id
                & (Join-Path $PSScriptRoot 'Play.ps1') -Map $selected
                $form.Close()
            }catch{[void][System.Windows.Forms.MessageBox]::Show($_.Exception.Message,'BO1 Zombies')}
        })
        [System.Windows.Forms.Application]::Run($form);exit 0
    }
    if($SelfTest){
        $token='test-'+[Guid]::NewGuid().ToString('N');$memory=$null;$access=$null
        try{
            if([BO1LocalPads]::Ready($token,$PID,'zombie_theater')){throw 'Sesión inexistente marcada lista.'}
            $memory=[System.IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew("Local\BO1EuphoriaCoop-$token",16)
            $access=$memory.CreateViewAccessor();$access.Write(0,[int]0x32434d4e);$access.Write(4,[int]4);$access.Write(8,[int]$PID);$access.Write(12,[uint32]4217694873)
            if(![BO1LocalPads]::Ready($token,$PID,'zombie_theater')){throw 'No se reconoce el anfitrión listo.'}
            if([BO1LocalPads]::Ready($token,($PID+1),'zombie_theater') -or [BO1LocalPads]::Ready($token,$PID,'zombie_moon')){throw 'Se acepta otro proceso o mapa.'}
            $access.Write(4,[int]3);if([BO1LocalPads]::Ready($token,$PID,'zombie_theater')){throw 'Se acepta una versión antigua de físicas compartidas.'}
            $access.Write(4,[int]4);$access.Write(12,[uint32]0);if([BO1LocalPads]::Ready($token,$PID,'zombie_theater')){throw 'Se inicia mientras el mundo no está listo.'}
        }finally{if($access){$access.Dispose()};if($memory){$memory.Dispose()}}
        if($maps.Count -lt 2){throw 'Se necesitan dos mapas instalados para esta prueba.'}
        Open-CoopRoom $null $true
        $room=$script:lobby
        if($room.Maps[0].Items.Count -ne $maps.Count -or $room.Maps[1].Enabled){throw 'Selector de sala incorrecto.'}
        Invoke-CoopRoomButtons 0 0;Invoke-CoopRoomButtons 1 0
        $click=[System.Windows.Forms.Button].GetMethod('OnClick',[System.Reflection.BindingFlags]'Instance,NonPublic')
        $click.Invoke($room.ReadyButtons[0],@([System.EventArgs]::Empty))
        if(!$room.State.Ready[0] -or $room.LaunchRequested){throw 'Inicio con un solo jugador.'}
        $room.Maps[0].SelectedIndex=($room.Maps[0].SelectedIndex+1)%$maps.Count
        if($room.State.Ready[0] -or $room.Maps[0].SelectedIndex -ne $room.Maps[1].SelectedIndex){throw 'El mapa no se sincroniza.'}
        Invoke-CoopRoomButtons 1 -1
        if($room.State.Ready[1] -or $room.ReadyButtons[1].Enabled){Update-CoopRoom;if($room.ReadyButtons[1].Enabled){throw 'Mando desconectado habilitado.'}}
        Invoke-CoopRoomButtons 1 4096
        if($room.State.Ready[1]){throw 'Reconectar con A pulsado confirma la sala.'}
        Invoke-CoopRoomButtons 1 0;Invoke-CoopRoomButtons 1 4096
        $click.Invoke($room.ReadyButtons[0],@([System.EventArgs]::Empty))
        if(!$room.LaunchRequested){throw 'Los dos jugadores listos no inician.'}
        Close-CoopRoom $false
        'PASS sala en dos monitores, selección sincronizada, dos confirmaciones, desconexión y reconexión del mando, y verificación del proceso, mapa y versión del mundo compartido';exit 0
    }
    if($Start -or $ValidateOnly -or $PrepareOnly){Start-LocalCoop;exit 0}
    $form=New-Object System.Windows.Forms.Form
    $form.Text='BO1 Zombies - preparar dos jugadores';$form.ClientSize=New-Object System.Drawing.Size(720,395);$form.StartPosition='CenterScreen';$form.FormBorderStyle='FixedDialog';$form.MaximizeBox=$false
    $form.Font=New-Object System.Drawing.Font('Segoe UI',10)
    $title=New-Object System.Windows.Forms.Label;$title.Text='Dos jugadores en la misma partida, cada uno en su monitor.';$title.Location=New-Object System.Drawing.Point(20,15);$title.Size=New-Object System.Drawing.Size(620,28);$form.Controls.Add($title)
    $monitorBoxes=@();$padBoxes=@()
    for($i=0;$i -lt 2;$i++){
        $y=55+$i*70
        $label=New-Object System.Windows.Forms.Label;$label.Text="Jugador $($i+1)";$label.Location=New-Object System.Drawing.Point(20,$y);$label.Size=New-Object System.Drawing.Size(100,24);$form.Controls.Add($label)
        $screenBox=New-Object System.Windows.Forms.ComboBox;$screenBox.DropDownStyle='DropDownList';$screenBox.Location=New-Object System.Drawing.Point(120,$y);$screenBox.Size=New-Object System.Drawing.Size(380,27)
        foreach($screen in $screens){$b=$screen.Bounds;[void]$screenBox.Items.Add("$($screen.DeviceName) - $($b.Width)x$($b.Height) - X=$($b.X)")}
        if($i -lt $screens.Count){$screenBox.SelectedIndex=$i};$form.Controls.Add($screenBox);$monitorBoxes+=,$screenBox
        $padBox=New-Object System.Windows.Forms.ComboBox;$padBox.DropDownStyle='DropDownList';$padBox.Location=New-Object System.Drawing.Point(515,$y);$padBox.Size=New-Object System.Drawing.Size(175,27)
        foreach($slot in $pads){[void]$padBox.Items.Add("Mando Xbox $($slot+1)")};if($i -lt $pads.Count){$padBox.SelectedIndex=$i};$form.Controls.Add($padBox);$padBoxes+=,$padBox
    }
    $mapLabel=New-Object System.Windows.Forms.Label;$mapLabel.Text='Mapa';$mapLabel.SetBounds(20,200,95,28);$form.Controls.Add($mapLabel)
    $mapChoice=New-Object System.Windows.Forms.ComboBox;$mapChoice.DropDownStyle='DropDownList';$mapChoice.SetBounds(120,200,570,30)
    foreach($entry in $maps){[void]$mapChoice.Items.Add($entry.Name)}
    for($i=0;$i -lt $maps.Count;$i++){if($maps[$i].Id -eq $Map){$mapChoice.SelectedIndex=$i}}
    if($mapChoice.SelectedIndex -lt 0 -and $maps.Count){$mapChoice.SelectedIndex=0};$form.Controls.Add($mapChoice)
    $note=New-Object System.Windows.Forms.Label;$note.Text='Una sala en cada monitor. Confirmad con A en cada mando para entrar juntos.'
    $missing=@($catalog | Where-Object {!$_.Available} | ForEach-Object {$_.Name})
    if($missing.Count){$note.Text+="`nFaltan recursos DLC: "+($missing -join ', ')}
    $note.SetBounds(20,250,670,74);$form.Controls.Add($note)
    $play=New-Object System.Windows.Forms.Button;$play.Text='Abrir sala';$play.SetBounds(515,335,175,38);$play.Enabled=$maps.Count -gt 0;$form.Controls.Add($play);$form.AcceptButton=$play
    $refresh=New-Object System.Windows.Forms.Button;$refresh.Text='Actualizar dispositivos';$refresh.SetBounds(300,335,195,38);$form.Controls.Add($refresh)
    $refresh.Add_Click({
        $script:screens=@([System.Windows.Forms.Screen]::AllScreens | Sort-Object @{Expression={if($_.Bounds.Width -eq 1920 -and $_.Bounds.Height -eq 1080){0}else{1}}},@{Expression={$_.Bounds.X}})
        $script:pads=@(0..3 | Where-Object {[BO1LocalPads]::Present($_)})
        for($i=0;$i -lt 2;$i++){
            $screenBox=$monitorBoxes[$i];$screenBox.Items.Clear();foreach($screen in $screens){$b=$screen.Bounds;[void]$screenBox.Items.Add("$($screen.DeviceName) - $($b.Width)x$($b.Height) - X=$($b.X)")};if($i -lt $screens.Count){$screenBox.SelectedIndex=$i}
            $padBox=$padBoxes[$i];$padBox.Items.Clear();foreach($slot in $pads){[void]$padBox.Items.Add("Mando Xbox $($slot+1)")};if($i -lt $pads.Count){$padBox.SelectedIndex=$i}
        }
    })
    $play.Add_Click({
        try{
            if($monitorBoxes[0].SelectedIndex -lt 0 -or $monitorBoxes[1].SelectedIndex -lt 0 -or $padBoxes[0].SelectedIndex -lt 0 -or $padBoxes[1].SelectedIndex -lt 0){throw 'Selecciona un monitor y un mando para cada jugador.'}
            $script:Monitor1=$monitorBoxes[0].SelectedIndex;$script:Monitor2=$monitorBoxes[1].SelectedIndex;$script:Pad1=$pads[$padBoxes[0].SelectedIndex];$script:Pad2=$pads[$padBoxes[1].SelectedIndex];$script:Map=$maps[$mapChoice.SelectedIndex].Id
            if($Monitor1 -eq $Monitor2 -or $Pad1 -eq $Pad2){throw 'Cada jugador necesita un monitor y un mando diferentes.'}
            Open-CoopRoom $form
        }
        catch{[void][System.Windows.Forms.MessageBox]::Show($_.Exception.Message,'BO1 Zombies');$play.Enabled=$true}
    })
    [System.Windows.Forms.Application]::Run($form)
} catch {
    if($ValidateOnly -or $PrepareOnly -or $Start -or $SelfTest){Write-Error $_;exit 1}
    [void][System.Windows.Forms.MessageBox]::Show($_.Exception.Message,'BO1 Zombies');exit 1
}
