# Repository Guidelines

## Project Overview

- ACSIL custom-study staging repo: native sources (`*.cpp`, `EdgeFul Indicators/`, `zbyte/`) are canonical; `studies/vendor/` copies are references — edit upstream, then `sync`.
- `tools/sc.py` (stdlib-only Python) is the lifecycle CLI: scaffold → `check` → `install`/`build` → `data` → `backtest`/`optimize`.
- Signal flow: chart → `BacktestExporter.cpp` → CSV → upstream headless backtester (never reimplemented here; `backtest` is a passthrough).
- Single sources of truth: the upstream backtester (`$SC_BT`, default `/home/user/SierraChartStudies/backtest/bt.py`) is the only engine; `STUDIES.md` + `studies/index.html` are the only catalog.
- Committed `*_64.dll`, `*.Cht`, `*.StdyCollct` are artifacts only — handled via `install`/`verify`, no binary-management or release-note process.

## Architecture & Data Flow

- Each study file is a standalone translation unit exporting `SCSFExport scsf_<Name>(SCStudyInterfaceRef sc)`.
- Lifecycle: `sc.SetDefaults` (define `GraphName`, `AutoLoop`, inputs) vs per-bar path using `sc.Index` (current bar) and `sc.ArraySize` (bar count).
- Per-instance state via `sc.GetPersistentInt/Fast/Float/SCString` — never file-scope mutable `g_` globals (breaks with 2+ chart instances).
- Chartbooks (`.Cht`) and study collections (`.StdyCollct`) store inputs in plaintext — never commit secrets in them.
- Pipeline: signal export → `data bars` validation → `backtest run|sweep|compare` → `promote` gate for shipping winners.

## Key Directories

| Path | Contents |
|---|---|
| Root `*.cpp` | House studies, incl. `TraderOracle.cpp`, `TORobots.cpp`, `Renko_GOAT.cpp`, `godtrades.cpp` |
| `EdgeFul Indicators/` | Session toolkit + `ES/NQ/YM_FiveMinStats.csv` slot tables |
| `zbyte/` | Ported indicators + `modules/` companion headers |
| `studies/vendor/*/` | Reference copies: `sierrachart-studies/`, `frozentundra/`, `jm-jo-nq100/` (see `studies/SOURCES.md`) |
| `tools/` + `tools/tests/` | Lifecycle CLI + stdlib unittest suite |
| `*_64.dll`, `*.Cht`, `*.StdyCollct` | Committed build artifacts and chart configs |

## Development Commands

- All commands run from the repo root; Python is stdlib-only (no pip, no build).
- `new` refuses overwrites and duplicate `scsf_` names; `--register` adds the `STUDIES.md` row + catalog card (else `check` fails until `catalog add`).

```
python3 tools/sc.py new "My Signal" --file MySignal.cpp --study MySignal --register --desc "..."
python3 tools/sc.py check
python3 tools/sc.py install --file MySignal.cpp --dry-run
python3 tools/sc.py build plan && python3 tools/sc.py build local
python3 tools/sc.py build dll && python3 tools/sc.py build verify
python3 tools/sc.py build stage --dry-run
python3 tools/sc.py sync --check
python3 tools/sc.py maintain --check
python3 tools/sc.py data validate
python3 tools/sc.py data bars /tmp/bt_export.csv
python3 tools/sc.py backtest demo --out /tmp/demo-report.html
python3 -m unittest discover -s tools/tests -v
```

## Code Conventions & Common Patterns

- One `scsf_` export per collision family — run `check` before any Remote Build; the four known pick-one families (`Delta_Intensity`, `InsideBarScanner`, `RollingZScoreChannel`, `SCOFAbsorptionDetector`, flagged ⚠ in `STUDIES.md`) warn, any new duplicate is a hard error.
- Every `.cpp` needs `SCDLLName("...")` or the study will not load.
- Convert `SCString` → `const char*` only via `.GetChars()`; never rely on implicit conversion.
- Input indices (`sc.Input[N]`) are append-only once shipped — never insert or reorder.
- Keep HTTP (`MakeHTTPPOSTRequest`/`MakeHTTPRequest`) and order calls (`BuyEntry`/`SellEntry`/`SendOrdersToTradeService`) off the hot path; throttle with `sc.Index` guards.
- Use the shared alert-ID registry (`AlertWithMessage`/`SetAlert` literals are chart-global); never embed `discord.com/api/webhooks` / `hooks.slack.com` secrets — chartbooks leak them.
- `TimeSlotValue` CSVs use the `HH:MM,value` contract: blank/`#` lines ignored, duplicates rejected.
- Edit vendor files upstream, then `sync` — never edit `studies/vendor/` directly.

## Important Files

| File | Why it matters |
|---|---|
| `tools/sc.py` | Lifecycle CLI contract; source of every Development Commands example |
| `tools/study_template.cpp` | Canonical `SCSFExport`/`SCDLLName`/persistent-state/input-index rules |
| `STUDIES.md` | Agent-facing study index, status values, ⚠ pick-one collisions, alert registry |
| `studies/SOURCES.md` | Vendor-copy manifest: origins, contents, sync dates |
| `studies/index.html` | Human-browsable catalog (mirrors `STUDIES.md`) |
| `tools/README.md` | Lifecycle workflows: `new`/`check`/`install`/`build`/`sync`/`data`/`backtest`/`maintain` |
| `MAINTENANCE.md`, `CHANGELOG.md` | Housekeeping workflow + changelog |
| `docs/Automation-Backtesting.md` | Distilled official automation/backtesting reference + links |
| `SierraChart.code-workspace` | Workspace folders (this repo + upstream `SierraChartStudies`) |

## Runtime/Tooling Preferences

- `python3`, stdlib-only: no pip installs, no build step for tooling.
- Local DLL compile is MinGW linking UCRT into `Data/*_64.dll`, matching Remote Build output with no server round-trip; override the target with `--to`.
- Env knobs: `$SC_ACS_SOURCE` (deploy target), `$SC_BT` (upstream backtester); defaults are the Wine install `~/.wine/drive_c/SierraChart/ACS_Source` and `/home/user/SierraChartStudies/backtest/bt.py`.
- Restart Sierra Chart after a build — it scans `Data/` once at startup — then re-run `verify`.
- `.vscode/` gcc/gdb settings are IntelliSense-only; they are not the DLL toolchain.

## Testing & QA

- `python3 -m unittest discover -s tools/tests -v` — stdlib-only suite in `tools/tests/test_sc.py` (currently 40 tests, all passing).
- `python3 tools/sc.py check` is the catalog/collision gate: new duplicate export, missing `SCDLLName`, or double-bound `sc.Input[N]` = error; the four known pick-one families = warnings (clean tree today: 0 errors, 4 warnings).
- `data validate` for `TimeSlotValue` slot CSVs; `data bars` for OHLC/signal exports (required columns, timestamp parsing, `High ≥ Low`) before burning backtest cycles.
- `build plan` fails on missing headers; `build verify` checks each DLL exists, is newer than its source, and exports every `scsf_` name.
- No coverage target is stated anywhere in-repo — none is enforced.
