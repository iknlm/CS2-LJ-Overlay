# CS2 LJ Overlay v1.2 - Windows x64

**IKNLM is the primary author, creator and owner.**
Source code: **MIT License © 2026 IKNLM**. [Authors](AUTHORS.md) | [Russian documentation](README_RU.md).
Claude, ChatGPT and Codex are credited exclusively as development assistants.

Standalone Windows x64 WinAPI/GDI HUD for CS2 CYBERSHOKE jump statistics.
Reads `game/csgo/console.log`; no DLL injection or game process modification.
One HUD window (`LJOverlayWnd`), aligned right by default, with Modern and CS 1.6 themes.

## DOWNLOAD FOR WINDOWS

1. Download [LJOverlay_v1.2_win64.zip](https://github.com/iknlm/CS2-LJ-Overlay/releases/download/v1.2/LJOverlay_v1.2_win64.zip).
2. Extract the archive.
3. Run `LJOverlay_new.exe`.
4. Configure the `console.log` path if needed.

**No compiler, MSYS2, Python or developer tools are required for normal use.**
Keep `sounds/` next to the EXE. Add `-condebug` to CS2 launch options, restart
CS2 and use windowed/borderless mode. If needed, set the log path in INS -> 9 -> 10.
Use the ZIP for a first installation. The separate EXE can update an existing
installation but does not include the optional external sounds.

[Release v1.2](https://github.com/iknlm/CS2-LJ-Overlay/releases/tag/v1.2) |
[Separate EXE](https://github.com/iknlm/CS2-LJ-Overlay/releases/download/v1.2/LJOverlay_new.exe) |
[Checksums](https://github.com/iknlm/CS2-LJ-Overlay/releases/download/v1.2/SHA256SUMS.txt)

## Controls

| Key | Action |
| --- | --- |
| INS | Toggle inline HUD settings |
| F6 / F7 | Manually mark the **latest** jump GOOD / BAD |
| F8 | Restore automatic evaluation of the latest jump |
| F9 | Lock / click through |
| Home | Hide/show |
| Ctrl+End | Quit |
| PgUp / PgDn, wheel | Scroll the current HUD section |

Mark an older attempt through INS -> History. Number keys are handled only
while settings are open and focused. Clearing/replacing session history
requires confirmation. Drag the header while settings are open, including with F9 enabled.

## v1.2 features

- Full log import from byte zero, then live appends; shared reading, rotation,
  truncation and replay handling without guessing duplicates from distance alone.
- Detailed metrics, per-strafe tables and key/mouse timelines.
- Select an older attempt by stable ID, inspect/copy its analysis, annotate its
  direction, and compare two attempts. Global marks still target the latest jump.
- Player names in the selected attempt header, history column and analysis;
  Unicode, long nicknames and `[VNL]`/`[CKZ]`/`[KZT]` headers supported.
  Names are bound to their own record, never taken from the last other player.
  Anonymous summaries are displayed as an unknown player.
- `lj_history.dat` persists attempts, names, IDs, manual marks and metrics across
  restarts, including when the old console.log has rotated or disappeared.
- Session CSV includes the full player name and `attempt_id`.

## Success thresholds and goals

INS -> 11. Select jump type and mode; settings are independent for
**LJ/BH/MBH/WJ/LAJ/JB x VNL/CKZ/KZT**.

Success may use Impressive..Wrecker or a custom distance, including values
above Wrecker. A separate goal may target the next rank, the mode/type-specific
Wrecker threshold, a custom distance, or a manually entered WR value.
Manual WR values are explicitly labeled and are not represented as official world records.

Use +/-0.1u or type a value; Enter applies, Esc cancels, Ctrl+A clears the draft,
Ctrl+V pastes. Exact decimal calculations keep 256.0 - 255.9 equal to 0.1u.
Reached goals are shown as reached. Near warnings apply only when
`0 < remaining <= 0.50u`. CYBERSHOKE ranks and rank tables are unchanged.
Changing a success threshold reevaluates automatic history/current CSV;
manual marks take priority. Changing only a goal leaves results and counters unchanged.

INI sections such as `[jump_VNL_LJ]` contain `success_mode`, `success_rank`,
`minimum`, `goal_mode=0..3`, `target`, `wr_manual`.
`[overlay]` stores theme, scale, opacity, block visibility, history rows,
position, lock, source path and optional `player` filter.

## Sounds and attribution

Sounds are off on a fresh install. INS -> 10 configures them. Playback is enabled
only in the CS 1.6 theme; Modern keeps preferences but mutes playback.
`[sound]`: `enabled=0`, `master=80`, `menu=60`, `awards=100`.

Two announcer sounds are by Antti Saari / TripleSnail, CC BY 3.0.
Nine procedural effects are CC0 substitutes, not Valve samples.
The MIT source license does not replace third-party asset licenses.
See [SOUND_CREDITS.txt](SOUND_CREDITS.txt) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Build and test

Install GCC in the MSYS2 UCRT64 terminal:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc
```

Run `BUILD_EXE.bat`. It writes `LJOverlay_new.exe` and `BUILD_LOG.txt`.
The complete manual command is:

```sh
g++ -std=c++20 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ lj_overlay.cpp -o LJOverlay_new.exe -lgdi32 -luser32 -lshell32 -ladvapi32 -lwinmm
```

Run `powershell -NoProfile -ExecutionPolicy Bypass -File .\RUN_TESTS.ps1`.
See [TESTING.md](TESTING.md), [VERIFICATION_RU.txt](VERIFICATION_RU.txt),
[CHANGELOG.md](CHANGELOG.md), and [screenshots](screenshots/README.md).
GDI screenshots use synthetic fixtures. LOG ERROR on some screenshots means
that the fixture did not provide a live game log.

Physical interaction with active CS2, in-game visibility and speaker audibility
still require manual checks. Unknown modes/types are not inferred. Identical
unstamped blocks at different log positions cannot reliably be distinguished
from separate equal attempts and are not merged by distance alone.

Download the Windows player ZIP from Release v1.2. The v1.1 release is retained.
