# studies/SOURCES.md — vendor copy manifest

Reference copies of external study sources, synced 2026-09-13.
The upstream repo is always the source of truth: edit there, then re-copy
the changed file(s) here and update the sync date below.

| Vendor dir | Upstream | Sync date | Contents |
|---|---|---|---|
| `vendor/sierrachart-studies/` | `tdawe1/SierraChartStudies` (`/home/user/SierraChartStudies`) | 2026-09-13 | `Orion.cpp`, `orion_core.h`, `FlipperStudies.cpp`, `SatyPivotRibbon.cpp`, `DiscordAlerts.cpp`, `InitialBalanceStatistics.cpp`, `PropRiskOverlay.cpp` (untracked upstream worktree file), `BacktestExporter.cpp` (from `backtest/exporter/`) |
| `vendor/frozentundra/` | `FrozenTundraTrader/sierrachart` (`/home/user/sierrachart`) | 2026-09-13 | 15× `.cpp` + `helpers.h` (needed by `JIGSAW_Export.cpp`) |
| `vendor/jm-jo-nq100/` | `JM-JO/Sierra-Chart---DLLs` (`src/`, `/home/user/Sierra-Chart---DLLs`) | 2026-09-13 | 19× French NQ100 `.cpp` |

Not copied (deliberately): upstream `AllStudies.cpp` is a generated bundle —
regenerate with `python3 bundle.py` instead of archiving a stale copy.
Upstream `orion_core_test.cpp` is a C++ unit test, not a study.

Notes:

- `Orion.cpp` used to live at this repo root (added `93cdc92`, removed
  `4f3acb0`). Do not re-add it at root; the vendor copy is the reference.
- `PropRiskOverlay.cpp` and `BacktestExporter.cpp` are untracked in their
  home repos (active work); re-sync after they are committed upstream.
- Native (non-vendor) sources — repo-root `*.cpp`, `EdgeFul Indicators/`,
  `zbyte/` — are canonical here, no manifest needed.
- After re-syncing, update `STUDIES.md` + `studies/index.html` if the
  study list or descriptions changed.
