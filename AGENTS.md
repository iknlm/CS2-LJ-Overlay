# LJOverlay / CS2 CYBERSHOKE VNL — instructions for Codex

This project is a standalone native WinAPI/GDI overlay. Source: `lj_overlay.cpp`.
Please communicate with the user in Russian. Focus on complete, compilable changes.

## Protect existing features

- Read `lj_overlay.cpp` and `README_RU.txt` before changing code.
- Do not inject a DLL into CS2 or modify the game process.
- Keep the `-condebug`/`game/csgo/console.log` Tailer and CYBERSHOKE parser behavior.
- Preserve F6 (last jump good), F7 (bad), F8 (clear), F9 (lock), Home (hide), Ctrl+End (quit), and default INS (menu).
- Preserve `jump_summary`, per-strafe tables, key/mouse timeline, session CSV, scoring and color thresholds.
- Keep the HUD on the right by default so it doesn't cover KZ MENU on the left.
- Use exactly one HUD window (`LJOverlayWnd`). Settings and older-jump selection live inside its `WndProc`; do not recreate a separate LJ CONTROL/MenuWndProc window. Modern/CS 1.6 themes apply to the entire HUD.
- Do not replace the custom layout with an unrelated UI framework unless user explicitly asks.

## Build and test

- Windows build: run `BUILD_EXE.bat` in this folder. Output: `LJOverlay_new.exe`.
- Manual command on MSYS2 UCRT64: `g++ -std=c++20 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ lj_overlay.cpp -o LJOverlay_new.exe -lgdi32 -luser32 -lshell32 -ladvapi32`
- Parser-only test: `g++ -std=c++20 -O2 -DLJ_TEST lj_overlay.cpp -o lj_test` and `lj_test sample_jump_console.log`.
- Before claiming a successful Windows executable, actually run the Windows compiler and verify the produced EXE.
- Prefer touching only relevant functions, test any modified compile targets and report limitations.

## Interaction semantics

- Closed menu must not consume number keys. Number keys work only while the inline HUD settings are open and focused.
- Global marks via F6/F7/F8 always affect the most recent jump. Marks through the menu History page affect the chosen older jump.
- Request confirmation before destructive operations such as clearing the session/re-reading logs.
- Keep `style`, `scale`, `show_details`, visibility toggles, opacity, and history row count in `lj_overlay.ini`.

FIX2 build: Use BUILD_EXE.bat and inspect BUILD_LOG.txt. Do not assume a DLL filename/version. See docs/CODEX_TASK.txt for the user goal. Windows compiler execution and current results are recorded in BUILD_LOG.txt and VERIFICATION_RU.txt.



Publication copy: IKNLM is the primary author, creator and owner. Source license is MIT (LICENSE); AI tools are development assistants only (AUTHORS.md). Run RUN_TESTS.ps1 for current tests; tests/legacy/verification_test.cpp is historical v1.1 coverage and is not in the v1.2 suite. No GitHub uploads, repository creation or release changes without a new explicit request.
