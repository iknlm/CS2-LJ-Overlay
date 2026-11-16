LJ Overlay v1.1 — Community Edition
Standalone Windows x64 WinAPI/GDI HUD for CS2 CYBERSHOKE jump statistics.
Reads game/csgo/console.log; no DLL injection or game process modification.

Extract the entire release into a writable folder and run LJOverlay_new.exe.
Add -condebug to CS2 launch options. Use windowed/borderless mode.
INI is created next to the EXE. Set [overlay] log= to the full console.log path
if automatic detection fails. Optional player= filters by player name.
To update, close the overlay and replace EXE and sounds. KEEP your own
lj_overlay.ini and lj_jumps. No personal INI or CSV is shipped.

INS: inline settings in the single HUD. Modern / CS 1.6 themes affect the whole
HUD. Right side by default. Drag header with settings open. Number keys are
handled only with settings open and focused.
F6/F7: GOOD/BAD latest jump. F8: restore automatic result. F9: lock.
Home: hide/show. Ctrl+End: quit. History marks affect the selected older jump.
Session clearing and log reread require confirmation. CSV exports: lj_jumps/.

Success from (item 11): Impressive / Perfect (default) / Godlike / Ownage /
Wrecker. Selected rank and higher are successful. Changes update current session
results and CSV, preserving manual marks and original statistics. Archived CSV
is unchanged. Unknown mode/type has no automatic result. Rank colors and distance
tables are unchanged. [overlay] success_rank stores the selection; missing or
invalid values fall back to Perfect.

Sounds (item 10): OFF on a fresh installation, [sound] enabled=0.
Existing explicit enabled=1 is preserved. Missing enabled means OFF.
Playback only in CS 1.6. Modern mutes but preserves preferences.
[sound] master=80, menu=60, awards=100; each accepts 0..100.
Effective volume = master * category / 100. System volume is unchanged.
Menu changes save immediately. Restart after manual INI changes.
Keep sounds/ next to EXE. Files: 16-bit mono/stereo PCM WAV.
Missing/invalid assets are skipped; restart after replacing WAV files.
Imports, repeated updates and success-threshold changes do not replay awards.
Credits and asset licenses: SOUND_CREDITS.txt and licenses/.

Build with BUILD_EXE.bat using MSYS2 UCRT64 GCC. Output: LJOverlay_new.exe.
Source package includes sound_system.h and STOP_OLD_OVERLAY.ps1.
Verification: VERIFICATION_RU.txt. Physical in-game interaction and audible
speaker output require manual checks on your machine.
Original author, creator and owner: IKNLM (github.com/iknlm).
Application source code is licensed under MIT: LICENSE (c) 2026 IKNLM.
AI development acknowledgments: Claude, ChatGPT, Codex (AUTHORS.md).
Sounds are governed by separate asset licenses (SOUND_CREDITS.txt, licenses/).

AUTHORSHIP AND LICENSE
Original project creator and copyright holder: IKNLM. Application code: MIT License.
Credit the included copyright and license notice when redistributing copies.
AI-assisted development: Claude (Anthropic), ChatGPT (OpenAI), Codex (OpenAI).
These assistants are not described as joint project owners. Sound licenses are separate.
