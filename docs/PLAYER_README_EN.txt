CS2 LJ Overlay v1.2 - Windows x64
Primary author, creator and owner: IKNLM.
MIT © 2026 IKNLM. Claude, ChatGPT and Codex: development assistance only.

DOWNLOAD FOR WINDOWS
1. Download LJOverlay_v1.2_win64.zip.
2. Extract the archive into a writable folder.
3. Run LJOverlay_new.exe.
4. Configure the console.log path if needed.

No compiler, MSYS2, Python or developer tools are required for normal use.
Keep sounds/ next to the EXE. Do not run the program from inside the ZIP.

CS2 SETUP
Add -condebug to the Steam launch options for CS2 and restart the game.
Use windowed/borderless mode. The expected log is game/csgo/console.log.
If auto-detection fails: INS -> 9 -> 10, enter/paste the log path.
The overlay is standalone WinAPI/GDI; it does not inject into or modify CS2.

CONTROLS
INS: inline settings. F6/F7: GOOD/BAD latest jump. F8: restore automatic.
F9: lock/click through. Home: hide/show. Ctrl+End: quit.
Mark older attempts through INS -> History; global marks target the latest.
Choose Modern or CS 1.6 in settings. Sound is OFF by default.
Sound plays only in CS 1.6; INS -> 10 controls sound and volume.
INS -> 11 configures per-type/mode success thresholds and independent goals.
Manual WR values are not official world records.

CONFIGURATION AND UPDATES
Settings are generated automatically in lj_overlay.ini on first launch.
Optionally copy lj_overlay.ini.example to lj_overlay.ini before starting.
The example has no personal log path or player name.
Keep your own lj_overlay.ini, lj_history.dat and lj_jumps/ when updating.
Close the overlay before replacing its EXE and bundled resources.
A separately downloaded EXE can update an existing installation, but it
does not contain the external sounds/ files; use the ZIP for a first install.
Missing sound resources are skipped; the HUD continues working.

LICENSES
The application source uses MIT © 2026 IKNLM.
Godlike/Ownage: Antti Saari / TripleSnail, CC BY 3.0; other effects CC0.
Keep LICENSE, SOUND_CREDITS.txt, THIRD_PARTY_NOTICES.md and licenses/ when sharing.
This project is not affiliated with Valve or CYBERSHOKE.

VERIFICATION LIMITS
The x64 EXE is statically linked to GCC/libstdc++ runtimes and uses Windows
system DLLs. It was tested with compiler paths removed from PATH.
A compiler-free virtual machine and every Windows version were not tested.
Sound API calls and PCM assets were checked; speaker audibility and physical
interaction with active CS2 require a manual check on your system.
