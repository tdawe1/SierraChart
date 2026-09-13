# Changelog

Notable changes to this repo, newest first. Format follows Keep a
Changelog; sections are calendar dates (`Unreleased` on top).

## [Unreleased]

### Added

- `tools/sc.py maintain --check`: housekeeping audit (root strays,
  empty doc stubs, `STUDIES.md` artifact refs, README/CLI parity, git
  hygiene); `--rm-junk` deletes browser `_files/` + `[objectObject]`
  accidents. Workflow defined in `MAINTENANCE.md`.

### Changed

- Lowercase presets renamed to Sierra's `.Cht` spelling (`TO Main.Cht`,
  `Professor RV Lines.Cht`), so the `check` secret-scan covers them.
- Vendor re-sync (`studies/vendor/sierrachart-studies/`):
  `BacktestExporter.cpp` 17-column schema (new `Symbol` column), per-bar
  bid/ask from `BaseData[SC_BID_PRICE/SC_ASK_PRICE]` (needs
  `MaintainAdditionalChartDataArrays`), fail-closed header check on
  incremental update (missing/mismatched header logs and requires
  Recalculate instead of writing mixed-schema rows); `PropRiskOverlay.cpp`
  `AutoLoop` 0→1, clock-guard `now_sec >= last_update`, fail-closed
  live-balance halt math, edge-triggered peak reset; `Orion.cpp`
  stale-`triggered_bar` reset on recalc-detect, `have_fields`
  live-balance fallback.
- `TORobots.cpp` GoldBug input migration: aggressive slot 6 removed,
  `MaxProfit` moved 8→22, `SendOrders` default flip applies to new
  instances only — re-check inputs on existing chart instances.

### Fixed

- `STUDIES.md` §8 preset list dropped `FrozenTundra_Footprint_*.cht` +
  `frozen tundra footprint.Cht` (files not in repo; re-add with the
  files if they resurface).

### Removed

- StrategyQuant export leftovers superseded by `MondayDipBuy.cpp`:
  `AlgoWizard - 1789213832080.[objectObject]` and
  `AlgoCloud Shared Strategy_files/`; empty `zbyte/readme.md` and
  `EdgeFul Indicators/readme.md` placeholders.

## [2026-09-12]

- Consolidation (`29acefb`): `StatArbPairs`, `MeanReversionOU`,
  `GoldenCrossRegime` studies; `tools/sc.py` lifecycle CLI + `zbyte`
  headers; studies index/catalog refresh; DLL + chartbook updates.
- Earlier history is upload-era (`Add files via upload`, `.`); see
  `git log`.
