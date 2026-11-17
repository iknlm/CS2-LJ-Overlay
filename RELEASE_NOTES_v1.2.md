# CS2 LJ Overlay v1.2 - Windows x64

**Primary author, creator and owner: IKNLM. MIT © 2026 IKNLM.**
Claude, ChatGPT and Codex are credited exclusively as development assistants.

## DOWNLOAD FOR WINDOWS

1. Download [LJOverlay_v1.2_win64.zip](https://github.com/iknlm/CS2-LJ-Overlay/releases/download/v1.2/LJOverlay_v1.2_win64.zip).
2. Extract the archive.
3. Run `LJOverlay_new.exe`.
4. Configure the `console.log` path if needed.

**A compiler is not required for normal use.**
Add `-condebug` to CS2 launch options, restart CS2 and use windowed/borderless mode.
Keep the bundled `sounds/` folder next to the EXE.
The separate EXE is suitable for updates; download the ZIP for a first installation.

## Changes since v1.1

- Full console.log import from byte zero and live reading with rotation/replay handling.
- Select and analyze older attempts, annotate directions and compare jumps.
- Custom success distance and an independent goal for each jump type/mode.
- Exact decimal remaining distance, Near warnings and reached-goal states.
- Player names in the selected header, history and analysis; stable IDs and
  session restoration across restarts, including when the log has rotated.
- Modern and CS 1.6 themes, existing ranks/metrics, manual marks and optional sound retained.

Player names are bound to their own records; anonymous records remain unknown.
Manual WR targets are explicitly labeled and are not official world records.
Preserve your own INI, history.dat and CSV folder when updating.

## Assets and verification

- `LJOverlay_v1.2_win64.zip`: complete player package.
- `LJOverlay_new.exe`: standalone executable, with external sounds optional.
- `SHA256SUMS.txt`: SHA256 for the ZIP and EXE; the ZIP also includes an internal manifest.

Windows x64 compilation, the current regression suite, actual EXE startup with
compiler directories excluded from PATH, log import/live appends, INI persistence,
both themes and resource packaging were checked before release publication.
Required PE imports are Windows system libraries; compiler runtimes are statically linked.
A separate compiler-free VM, physical CS2 interaction and speaker audibility
were not verified. Automated audio tests exercise PCM assets and PlaySoundW.
See the source repository verification reports for the exact scope.

Sounds have separate CC BY 3.0 / CC0 licenses; retain SOUND_CREDITS.txt and licenses/.
No DLL injection or game process modification. Not affiliated with Valve or CYBERSHOKE.
The existing v1.1 release and tag are retained.
