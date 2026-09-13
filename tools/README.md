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
`harness` drives the live chart without GUI clicks: `BacktestHarness.cpp`
(once on a chart) executes a job file — ADD/SET/WIRE/RECALC/REMOVE studies —
and `harness` waits for the `.result` file. Routing (DLL/scsf/subgraphs per
study) lives in `studies/harness-routing.json`:

```
python3 tools/sc.py harness --cmd "ADD MeanReversionOU_64.scsf_MeanReversionOU AS MROU" --cmd "RECALC"
```

`confirm` runs the ORB15 confirmation discipline in one shot — in-sample
`run`, out-of-sample `run --split`, then `walkforward` (fail-fast), and
points at `promote --run` for the shipping gate:

```
python3 tools/sc.py confirm --data X.csv --params winner.json --out runs/confirm1
python3 tools/sc.py confirm --data X.csv --params winner.json --out runs/confirm1 --split 2024-01-01 --train 252 --test 63 --step 21
python3 tools/sc.py confirm --data X.csv --params winner.json --out runs/confirm1 --skip-walkforward   # IS + split only
```

Risk profiles (`tools/profiles/risk-{conservative,balanced,growth}.json`,
1%/2%/3% risk with scaled daily-loss and drawdown halts) plug into any
step as the params file, e.g.
`--params tools/profiles/risk-conservative.json`. Copy one and flip
`engine.regime` per strategy (they ship as `trend` for ORBRetrace).

## Automated loop: chart signals to verdict with no clicks

`BacktestHarness.cpp` (on one chart per setup) executes job files from
`harness`, so studies are added, wired, recalculated and exported without
GUI work. Study routing (DLL/scsf/trigger subgraphs, 71 studies classified)
lives in `studies/harness-routing.json`. Proven live: MeanReversionOU on
YMU 10-min captured 2045 bars / 48 signals end-to-end.

```
python3 tools/sc.py harness --cmd "WHOAMI"   # worker? chart/symbol/bars
# one-liner per routed TRIGGER study (two jobs + validation):
python3 tools/sc.py harness --capture MeanReversionOU --out ~/.wine/drive_c/SierraChart/Data/bt_mrou.csv
python3 tools/sc.py backtest run --data ~/.wine/drive_c/SierraChart/Data/bt_mrou.csv --params params.replay.json --split frac:0.7 --out runs/mrou
```

Session recipe (Sierra runs on a virtual display here): launch, and if jobs
sit unclaimed past one bar, force one recalculation (Chart menu) to
kick-start polling — new studies only calculate after a recalc/new bar.
`VERIFY <short> <idx>` reads back live wiring; never trust an ADD return
as an ID (it is a status — always RESOLVE). Keep exactly one harness per
setup (two poll the same job file). Save the book after deploying so the
harness persists across restarts. First poll after add/session start needs
a recalc (or one new bar); steady-state claims land in seconds.
Study IDs recycle after REMOVE: always re-VERIFY every wired input after
removing a study (a dangling reference silently follows the next study
to reuse the id). Retunes need REMOVE + ADD + SET + RECALC (recalcs on
a live instance carry stale persistents).

New TRIGGER study onboarding: audit its Buy/Sell subgraph slots, fill
`longSg`/`shortSg` in the routing table, `harness --capture`, confirm
signal bars > 0, then the standard `run`/`sweep`/`split`/`walkforward`/
`promote` chain. ORDER studies go through `SignalExecutor.cpp` + the
built-in replay backtest; DISPLAY/UTIL/BLOCKED classes have no backtest
path by construction (see routing notes).

Bulk data (months, not weeks): `bt.py scid` converts tick files to bar
CSVs — pass the per-generation price divisor (legacy hundredths files:
100; current SYM-YYYYMM points files: 1) and `--tz-offset -4` for
chart-joins, then overlap-join a chart export before trusting a
conversion (exact OHLC + volumes or it didn't happen). Long-window
study signals without GUI chart surgery: port the study's entry math
as a strategy adapter (`mrou_ou` pattern) and gate it on a bit-for-bit
match against exported markers (48/48) before sweeping bulk data.

## Maintenance (directory, commit log, docs)

```
python3 tools/sc.py maintain --check   # audit only: strays, docs drift, git hygiene
python3 tools/sc.py maintain --rm-junk # also delete browser _files/ + [objectObject] accidents
```

Flags root strays (junk, `*.bak*` backups, lowercase `*.cht`, empty
`*.md` stubs), errors on `STUDIES.md` §7/§8 artifact refs that resolve
to nothing and on `tools/README.md` missing any CLI subcommand/action,
and notes uncommitted paths + non-`<area>: <summary>` HEAD subjects.
Full workflow (commit format, changelog policy, doc duties) in
`MAINTENANCE.md`. Loop before each commit:
`maintain --check` → `check` → `unittest`.

## Tests

```
python3 -m unittest discover -s tools/tests -v
```
