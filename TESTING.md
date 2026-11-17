# Build and test

Run `BUILD_EXE.bat` with an installed MSYS2 UCRT64 GCC toolchain.
The script sets the compiler DLL search path, compiles the complete GUI with
`-lwinmm`, and writes `LJOverlay_new.exe` and `BUILD_LOG.txt`.
`STOP_OLD_OVERLAY.ps1` stops only known overlay processes whose executable
is in this copy's directory. It does not stop the original project's process.

Run current tests in PowerShell:

```powershell
.\RUN_TESTS.ps1
# If MSYS2 is installed elsewhere:
.\RUN_TESTS.ps1 -Compiler 'X:\msys64\ucrt64\bin\g++.exe'
```

The runner copies sources and sounds into a fresh `build/tests_*` folder,
then builds and runs 10 current test targets plus the parser-only fixture.
Test logs, INI files, CSV files, screenshots and EXEs remain under ignored
`build/`. Summary evidence is written to `reports/TEST_RESULTS.txt`.

The tests cover decimal goals, rank thresholds, player identity, persistent
history, full log import/rotation/replay, detailed analysis, comparison,
WinAPI/GDI rendering in both themes, sound settings and PlaySoundW behavior.
Synthetic sample names and logs are test fixtures, not a personal session.

`tests/legacy/verification_test.cpp` is preserved as historical v1.1 coverage.
Its global-threshold and menu assertions predate v1.2 and it is not a current
v1.2 acceptance target. The current `goals_test.cpp`, `players_test.cpp`,
`review_gui_test.cpp` and other tests replace that acceptance coverage.
It can be compiled for inspection with `-I .` from the repository root;
passing the legacy assertions on the v1.2 interface is not claimed.

`smoke_players_v12.ps1`, `smoke_goals_v12.ps1`, `smoke_console_v12.ps1` and
`smoke_v12.ps1` start an actual production EXE. Close another running overlay
before using them: the application intentionally uses a single-instance mutex.
The general test runner does not launch the production application.

Latest local evidence: `BUILD_LOG.txt`, `reports/TEST_RESULTS.txt`,
`reports/PACKAGE_CHECKS.txt`, and `VERIFICATION_RU.txt`.
Earlier evidence is explicitly separated under `reports/provenance/`.
The GitHub Actions workflow builds/tests and uploads an artifact; it does
not create repositories or publish GitHub Releases.

Publication-copy test adaptation: `analysis_ui_test.cpp` assigns IDs to its
direct Parser fixture before recording its baseline CSV, matching the v1.2
production log pump. The assertions are retained; see `reports/TEST_ADAPTATIONS.diff`.


Release v1.2 publication is now explicitly authorized. The release workflow
checks the exact locally tested source/resource hashes, rebuilds on Windows x64,
runs parser/model checks and the actual EXE smoke test, packages a clean ZIP,
verifies uploaded draft assets before publication, and verifies public downloads.
The local full 10-program suite (including actual PlaySoundW) is recorded in
reports/TEST_RESULTS.txt. CI repeats portable model/runtime checks without
claiming to provide an audio endpoint. A failure stops publication.
The prior general build workflow remains separate and does not publish releases.
