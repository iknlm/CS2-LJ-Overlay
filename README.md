# CS2 LJ Overlay — v1.1 Community Edition

**Created and maintained by [IKNLM](https://github.com/iknlm).**  
Development assistance: **Claude (Anthropic), ChatGPT (OpenAI), Codex (OpenAI)**.

A lightweight **Windows x64** WinAPI/GDI HUD for CS2KZ / CYBERSHOKE jump stats. It reads CS2's `console.log` when the game is launched with `-condebug`; it does **not** inject into the game.

## Features

- Jump distance, strafes, sync, Pre/Max Speed, JumpEdge, DeadAir, Deviation, and key/mouse timeline.
- VNL and CKZ/KZT jump rank evaluation, with CYBERSHOKE rank colors.
- Configurable minimum successful rank: Impressive, Perfect, Godlike, Ownage or Wrecker (default: Perfect).
- Manual results: **F6** successful, **F7** unsuccessful, **F8** return to automatic evaluation.
- Session counts, attempt history, comparison of successes and failures, CSV export.
- Two themes for the **same HUD**: Modern and classic CS 1.6 / GoldSrc. Inline settings via **INS**; no separate menu window.
- Optional CS 1.6-style sounds and announcers, **disabled by default** on new installations.

## Quick start

1. Download `LJOverlay_v1.1_CommunityEdition_win64_MIT.zip` from **Releases** and extract the whole archive to a writable folder.
2. In Steam, set CS2 launch options to `-condebug`. Use borderless/windowed mode for the external overlay.
3. Start `LJOverlay_new.exe`. Keep its `sounds/` folder next to it.
4. Press **INS** to open settings and change theme, size, opacity, visible panels, minimum successful rank or sounds.

Additional shortcuts: **F9** lock/unlock overlay; **Home** hide/show; **Ctrl+End** quit.

For setup and detailed usage, see [README_RU.txt](README_RU.txt) or [README_EN.txt](README_EN.txt).

## Building from source

Install **MSYS2 UCRT64 GCC** with a C++20 compiler, then run `BUILD_EXE.bat` from the repository root. The resulting `LJOverlay_new.exe` is the Windows build. Parser and Windows GUI tests are in `verification_test.cpp` and related test files.

## Credits and license

- **Original author and project maintainer: IKNLM**.
- Development acknowledgments: **Claude**, **ChatGPT**, **Codex**. See [AUTHORS.md](AUTHORS.md).
- Application source code: **MIT License** — © 2026 IKNLM. See [LICENSE](LICENSE). You may reuse and modify it provided the copyright and license notice are retained.
- Sounds: separately licensed under CC BY 3.0 / CC0, as specified in [SOUND_CREDITS.txt](SOUND_CREDITS.txt). This includes attribution obligations for the CC BY files.
- Not affiliated with Valve, Counter-Strike, or CYBERSHOKE. Game names and marks belong to their owners.

**Testing note:** Automated Windows build and regression tests have been reported successful; real input/audio behavior in an active CS2 session must be checked on the target machine.
