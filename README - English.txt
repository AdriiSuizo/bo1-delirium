BO1 DELIRIUM | 0.3.0 Beta 16
ENGLISH GUIDE
Guía en español: LEEME - Espanol.txt

WHAT IT DOES
Active body physics for BO1 zombies: localized impacts, balance, falls and
recovery, integrated with BO1 pursuit. This beta improves languages, naming
and installation; it retains beta 11 physics. It is not a full migration
to the newest upstream Delirium SDK.

REQUIREMENTS
- Steam Call of Duty: Black Ops, build 7.0.152 CL 966072.
- Plutonium T5 installed and configured for that BO1 folder.
  Official installation: https://plutonium.pw/docs/install/
- Your own Xbox 360 BASE GTA IV ISO dump. PC copies and Episodes from
  Liberty City are unsupported. Verified profiles: 4A53F9F6 and 6AC07221.
- Windows 10/11 x64. Tested on Windows 10 with Plutonium r5369.

INSTALL
1. Close BO1 and Plutonium. Extract the ENTIRE RAR into any folder.
2. Open Instalar - Install BO1 Delirium.exe and select Español or English.
3. Setup finds BO1 in Steam. Select BlackOps.exe if prompted.
4. Select your ISO if requested. Leave it wherever you store it: you do not
   need to copy it into BO1. Initial preparation may require Internet access.
5. Wait for the installation-complete message. Initial preparation briefly
   opens Kino to read your BO1 skeleton and ragdoll; allow around three
   minutes. That preparation instance closes when finished.
6. Open Jugar - Play BO1 (LAN) from your Desktop, select BO1 Delirium in
   Mods, then start Zombies Solo. Start with Kino to check your installation.

The shortcut starts the official Plutonium client in LAN mode. There is no
per-mod CMD you need to keep open. You can also use the official launcher
in LAN mode. Online mode does not load this native plugin.

AFTER INSTALLATION
You can delete the folder where you extracted the RAR. The ISO is no longer
needed to play after preparation completes. Keep the installed BO1Delirium
folder inside BO1 and your local cache: the mod still uses those.
BlackOps.exe remains unchanged. No original game resources ship in the package.

TEST CONTROLS | CONTROLLER D-PAD OR KEYBOARD ARROWS
They are off by default. To enable them, add the line test_controls=1 under
[Delirium] in %LOCALAPPDATA%\BO1PlutoniumDelirium\installation.ini.
Reinstalling or updating keeps that choice.
Down: toggle god mode and add 100,000 points on every press. Requests round 20
      when below it; does not rewind higher rounds.
Up: kill living zombies and pause round spawning. Press again to resume.
Left: spawn five pursuing zombies without resuming a paused round.
Right: toggle zombie pursuit.
In-game notices follow BO1's language: Spanish or English.

UPDATE
Close BO1 and Plutonium, extract the newer package and run its installer.
Compatible caches are reused; unrelated files are preserved.
Find the version in VERSION.txt and executable file Properties.
The BO1 Delirium version is separate from the upstream physics engine version.

UNINSTALL
Inside your Steam BO1 folder, open Desinstalar - Uninstall BO1 Delirium.exe
with the game closed. It removes the plugin, script and Mods registration.
Saves, other mods, your ISO and local cache are kept for reinstallation.
Package files remain; you can delete them afterwards if no longer needed.
The LAN shortcut is a general BO1 shortcut and remains useful without this mod.

COMPATIBILITY
Verified mode: SINGLE-PLAYER Zombies in LAN. Articulated bodies have no custom
network replication: installing the mod on every PC does not establish co-op
compatibility. The Mods identifier remains mods/bo1delirium.
Previous startup checks cover installed Kino, Five, Ascension, Call of the
Dead, Shangri-La, Moon, Nacht, Verrückt, Shi No Numa and Der Riese.
Startup checks do not establish full-match stability on every map. This beta
rechecks loading and controls in Kino. Dogs retain their native behavior.

TROUBLESHOOTING
- Missing installer: fully extract the RAR before opening it.
- Mod appears but physics are absent: use LAN and select the mod.
- Missing resources: rerun setup. Do not download another player's cache.
- "The key ... could not be downloaded": the first preparation needs Internet
  and curl.exe (included in Windows 10 and 11). disc-setup.log explains the
  offline alternative.
- "The ISO is not a supported base disc": use a dump of base GTA IV for
  Xbox 360 (profiles 4A53F9F6 or 6AC07221). disc-setup.log shows the reason.
- Plutonium updates can change supported runtime signatures.
- Logs: %LOCALAPPDATA%\BO1PlutoniumDelirium
  disc-setup.log: ISO preparation.
  plugin-loader.log and logs\native-runtime.log: plugin and adapter.
  installation.ini: installed location and language.
Include version, map, reproduction steps and relevant logs in bug reports.
Never attach ISOs, caches, keys or original game files.

Credits: CREDITOS - CREDITS.txt
Version history: NOVEDADES - CHANGELOG.txt
Source and build instructions: BO1Delirium\SOURCE.zip (BUILD.txt).
