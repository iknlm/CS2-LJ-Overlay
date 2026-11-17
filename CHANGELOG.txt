# Changelog

## v1.2 (2026-11-17)

- Full byte-zero console.log import and live shared reading with rotation,
  truncation, partial UTF-8 lines, source selection and replay protection.
- Stable attempt selection, analysis, manual direction annotations and comparison.
- Custom decimal success thresholds and independent goals per jump type/mode;
  exact remaining-distance calculations, reached states and Near warnings.
- Player names in the header/history/analysis, Unicode-safe compact display,
  bracketed mode headers, mixed-player/type/mode identity handling.
- Persistent current-session snapshot with names, IDs, marks and import keys;
  CSV now includes attempt_id alongside the existing player-name column.

### Publication preparation (2026-11-17)

- Selected the newest tested source by code, hashes, dates and build evidence,
  rather than using the older v1.1 CommunityEdition/release snapshots.
- Source code licensed MIT © 2026 IKNLM as instructed by the project owner;
  development assistants credited separately from primary authorship.
- Preserved CC BY 3.0/CC0 asset notices and compiler-runtime license texts.
- Added current RU/EN documentation, a test runner, provenance and readiness reports.
- Updated the build workflow for all headers/resources and winmm linkage.
- Restricted the copied shutdown helper to executables inside this copy's folder.
- Excluded personal settings/history/logs, old binaries/packages and test debris.
- The publication request now authorizes main and a separate v1.2 release, while preserving v1.1.

## v1.1 Community Edition - historical

- User-selectable minimum success rank with automatic history/CSV reevaluation.
- Modern/CS 1.6 themes, inline settings, manual marks and sound preferences.
- Sounds disabled by default, with separate menu/award volumes.

Existing published v1.1 artifacts are not changed by this preparation.

Publication-copy test adaptation: `analysis_ui_test.cpp` assigns IDs to its
direct Parser fixture before recording its baseline CSV, matching the v1.2
production log pump. The assertions are retained; see `reports/TEST_ADAPTATIONS.diff`.

Windows player distribution: ready EXE, sounds, clean INI example, RU/EN player instructions and per-asset SHA256. No compiler required for normal use.
