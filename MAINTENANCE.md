# Maintenance workflow

Recurring housekeeping for directory, commit log, and docs. The
mechanical checks run via `python3 tools/sc.py maintain --check`
(audit only); `--rm-junk` additionally deletes unambiguous tool
accidents. Everything else below is human judgment.

## Directory — keep it sorted

- Native sources live at root / `EdgeFul Indicators/` / `zbyte/`;
  vendor copies under `studies/vendor/` (edit upstream, then `sync`).
  Nothing else accumulates at root.
- Junk classes `maintain` flags (WARN): browser page exports
  (`*_files/`), bad download names (`*[objectObject]*`), `*.bak*`
  backups, lowercase `*.cht` (Sierra writes `.Cht`), empty
  placeholder `*.md`.
- Backups (`.bak*`) are never committed and never auto-deleted —
  archive outside the repo or drop them.
- Every `*_64.dll`, `.Cht`, `.StdyCollct` on disk must be referenced
  in `STUDIES.md` §7/§8, and every artifact `STUDIES.md` names there
  must exist on disk.

## Commit log

- Format: `<area>: <imperative summary>` — areas: `studies`,
  `edgeful`, `zbyte`, `vendor`, `tools`, `docs`, `chartbooks`, `dll`,
  `chore`. One concern per commit; a study ships with its catalog
  row + card in the same commit.
- History before 2026-09-12 is upload-era noise (`Add files via
  upload`, `.`); leave it alone, keep the convention from here.
- `CHANGELOG.md` (Keep a Changelog, date sections): add entries
  under `Unreleased` with each user-facing change.

## Docs

- New/changed study → `new --register` or `catalog add`
  (`STUDIES.md` + `index.html` together, same commit); vendor
  change → `sync` (bumps `studies/SOURCES.md` sync date).
- `tools/README.md` must name every CLI subcommand/action —
  `maintain --check` enforces this as a hard error on drift.
- CLI shape changes → update the `AGENTS.md` command block in the
  same commit.
- Official automation/backtesting reference: `docs/Automation-Backtesting.md` —
  consult it before touching order-sending code or designing backtests.

## Loop (before each commit, weekly at minimum)

```
python3 tools/sc.py maintain --check
python3 tools/sc.py check
python3 -m unittest discover -s tools/tests -v
```
