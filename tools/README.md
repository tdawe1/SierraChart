# tools/ — study lifecycle automation

Stdlib-only Python. No pip, no build. Run from the repo root.

## Add a study

```
python3 tools/sc.py new "My Signal" --file MySignal.cpp --study MySignal --register --desc "..." --status Source-only
python3 tools/sc.py check        # duplicate-export + catalog gate
# copy to ACS_Source → Remote Build (see Install)
```

`new` refuses to overwrite and refuses a `scsf_` name that already exists
anywhere in the native sources, so the classic "two files, same export, one
broken DLL" failure cannot be scaffolded. `--register` appends the
STUDIES.md row(s) and the `studies/index.html` card in the same step
(section/group follow the file's directory); without it, `check` fails
until you run `catalog add` below.

```
python3 tools/sc.py catalog add --file MySignal.cpp --desc "..." --status Source-only
```

## Update a study

Edit the canonical file (root `*.cpp`, `EdgeFul Indicators/`, `zbyte/`),
then:

```
python3 tools/sc.py check        # fails on new duplicate exports / missing SCDLLName /
                                 # missing STUDIES.md row
python3 tools/sc.py data validate   # if you touched a stats CSV
```

`check` passes with warnings on this repo's four *known* pick-one-per-build
collisions (`Delta_Intensity`, `InsideBarScanner`, `RollingZScoreChannel`,
`SCOFAbsorptionDetector` — all flagged ⚠ in STUDIES.md). Any *new* duplicate
export is a hard error.

What `check` looks for (all grounded in this repo's actual bugs):

| Signal | Origin |
|---|---|
| duplicate `scsf_` across files | `Delta_Intensity` ×2, `InsideBarScanner` ×2, `RollingZScoreChannel` ×2, `SCOFAbsorptionDetector` ×3 |
| same `sc.Input[N]` bound to two names in one study (error) | `TORobots.cpp` `Input[8]` was both Max Loss and Max Profit |
| shared alert IDs across files (`AlertWithMessage`/`SetAlert` literals) | renumbered 2026-09-12 into the STUDIES.md registry; `check` still guards |
| webhook URLs embedded in `.Cht`/`.StdyCollct` | chartbooks store inputs in plaintext — keep private |

## Install (deploy to Sierra for Remote Build)

```
python3 tools/sc.py install --file MySignal.cpp --dry-run   # preview
python3 tools/sc.py install --file MySignal.cpp              # copy to ACS_Source
python3 tools/sc.py install --all --to /tmp/acs-test         # override target
```

Destination is `$SC_ACS_SOURCE`, else the local Wine install
`~/.wine/drive_c/SierraChart/ACS_Source`. Pick-one-per-build families in the
deployed set are re-flagged at copy time — the collision can never slip
through silently.

## Build (compile pipeline — every study available in SC)

```
python3 tools/sc.py build plan     # dep closure per study (fails on missing headers)
python3 tools/sc.py build local    # local mingw syntax check (seconds, not minutes)
python3 tools/sc.py build dll      # compile every study to Data/*_64.dll locally
python3 tools/sc.py build verify   # every study has a fresh DLL exporting its studies
```

`dll` builds with the local MinGW compiler straight into Sierra's `Data/`
dir (override with `--to`; `--force` rebuilds everything). The toolchain
links UCRT — the same C runtime as Sierra Chart — and exports are plain
`extern "C" __cdecl`, so the DLLs load exactly like Remote Build output
with no server round-trip. After a build, restart Sierra Chart (it scans
`Data/` once at startup), then re-run `verify`.

Prefer the vendor (Remote Build) path instead? `stage` flattens sources to
the build root and merges companion headers under their study-relative
dirs (`zbyte/include/`, `zbyte/modules/`), so quoted includes resolve both
locally and on the build server:
```
python3 tools/sc.py build stage --dry-run   # preview the flat Remote Build layout
python3 tools/sc.py build stage              # copy studies + headers to ACS_Source
# then in Sierra Chart: Analysis → Build Custom Studies DLL → select every
# staged .cpp in ONE Remote Build (one trip, one _64.dll per study)
```
`install` uses the same layout for `--file` / `--all`. `verify` checks each
`Data/<Base>_64.dll` exists, is newer than its source, and exports every
`scsf_` name (pick-one families build together fine — just load one DLL
of the family at a time).

## Vendor updates (reference copies — edit upstream, never here)

```
python3 tools/sc.py sync --check   # drift report (hash compare, no copies)
python3 tools/sc.py sync           # copy + bump studies/SOURCES.md sync date
```

## Data management

```
python3 tools/sc.py data list
python3 tools/sc.py data validate "EdgeFul Indicators/ES_FiveMinStats.csv"
python3 tools/sc.py data validate   # all CSVs in repo
```
Validates the `HH:MM,value` contract (blank/`#` lines ignored, `HHMM`
fallback accepted, duplicates rejected) that `TimeSlotValue.cpp` parses.

Bar/signal exports (chart → `BacktestExporter.cpp` → CSV) use a different
contract — validate those with `bars` before burning backtest cycles:

```
python3 tools/sc.py data bars /tmp/bt_export.csv
```

Checks required `DateTime/Open/High/Low/Close` columns (case-insensitive,
upstream aliases accepted), timestamp parsing, numeric OHLC, `High ≥ Low`
with open/close inside the range, and reports bar count, signal-bar count,
and stamp span.

## Backtest different strategies / optimize parameters

The engine lives upstream (`~/SierraChartStudies/backtest/`, stdlib-only)
and is **not** reimplemented here — `backtest` is a passthrough so there is
exactly one engine to keep honest. Discover what to run first:

```
python3 tools/sc.py strategies   # strategy names + params files upstream
```

```
python3 tools/sc.py backtest demo --out /tmp/demo-report.html
python3 tools/sc.py backtest run --data ES-aug.csv --params params.replay.json --out out/aug-base
python3 tools/sc.py backtest sweep --data ES-aug.csv --params params.sweep.json
python3 tools/sc.py backtest compare --dataset ES-aug --out report-aug.html
```

`optimize` removes the hand-edited sweep JSON step: it builds the grid from
`--param NAME=v1,v2` ranges over a base params file (200-combo cap, same as
the engine's fail-fast), runs the sweep, and writes the leaderboard:

```
python3 tools/sc.py optimize --data X.csv --base params.replay.json --param stop_ticks=8,12,16 --param target_ticks=16,24 --out runs/opt1
python3 tools/sc.py optimize --data X.csv --base params.replay.json --param stop_ticks=8,12 --out runs/opt1 --dry-run   # sweep.json only
```

Loop: export signals from the chart with `BacktestExporter.cpp` (vendor copy
in `studies/vendor/sierrachart-studies/`), `run` the base, `sweep` the grid,
confirm winners with `run --split` / `walkforward`, gate shipping with
`promote --run <id>`. Full contract in `backtest/README.md` upstream.

## Tests

```
python3 -m unittest discover -s tools/tests -v
```
