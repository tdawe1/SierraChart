#!/usr/bin/env python3
"""sc.py — SierraChart staging-repo study tooling (stdlib only).

One CLI for the study lifecycle in this repo (/home/user/SierraChart):

  new       scaffold a study from the template (refuses name/file collisions;
            --register also adds the STUDIES.md row + index.html card)
  catalog   register a study in STUDIES.md + studies/index.html
  install   deploy sources to the Sierra build folder ($SC_ACS_SOURCE or
            the local Wine ACS_Source) for Remote Build
  check     static review: duplicate scsf_ exports, missing SCDLLName,
            multi-instance globals, unescaped webhook JSON, dead helpers,
            STUDIES.md coverage, vendor drift vs upstream checkouts
  maintain  repo housekeeping audit: strays, docs drift, git hygiene
            (--rm-junk deletes browser _files/ + [objectObject] accidents)
  sync      copy vendor reference files from upstream checkouts (or --check)
  data      validate/list TimeSlotValue-style CSV datasets; bars validates
            OHLC/signal exports against the backtester data contract
  strategies  list backtester strategies + params files
  optimize  build a sweep grid from --param ranges, run the sweep, write
            the leaderboard (engine stays upstream)
  backtest  thin passthrough to the upstream headless backtester
            (engine lives in SierraChartStudies/backtest/bt.py — never
            reimplemented here)

Examples:
  python3 tools/sc.py check
  python3 tools/sc.py new "My Signal" --file MySignal.cpp --study MySignal --register --desc "..."
  python3 tools/sc.py install --file MySignal.cpp --dry-run
  python3 tools/sc.py sync --check
  python3 tools/sc.py data validate "EdgeFul Indicators/ES_FiveMinStats.csv"
  python3 tools/sc.py data bars /tmp/export.csv
  python3 tools/sc.py strategies
  python3 tools/sc.py optimize --data X.csv --base params.replay.json --param stop_ticks=8,12 --out runs/opt1
  python3 tools/sc.py backtest demo --out /tmp/demo-report.html
  python3 tools/sc.py backtest run --data X.csv --params params.replay.json --out out/x
"""

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = Path(__file__).resolve().parent
TEMPLATE = TOOLS / "study_template.cpp"
STUDIES_MD = ROOT / "STUDIES.md"
SOURCES_MD = ROOT / "studies" / "SOURCES.md"
VENDOR = ROOT / "studies" / "vendor"

NATIVE_DIRS = [ROOT, ROOT / "EdgeFul Indicators", ROOT / "zbyte"]
UPSTREAM_BT = Path(os.environ.get("SC_BT",
                   "/home/user/SierraChartStudies/backtest/bt.py"))
BT_DIR = UPSTREAM_BT.parent


def acs_source():
    """Live Sierra build folder: $SC_ACS_SOURCE or the local Wine install."""
    env = os.environ.get("SC_ACS_SOURCE")
    return Path(env) if env else Path.home() / ".wine/drive_c/SierraChart/ACS_Source"

# vendor name -> (upstream root, extra mapping for renamed files)
VENDOR_MAP = {
    "sierrachart-studies": (
        Path("/home/user/SierraChartStudies"),
        {"BacktestExporter.cpp": "backtest/exporter/BacktestExporter.cpp"},
    ),
    "frozentundra": (Path("/home/user/sierrachart"), {}),
    "jm-jo-nq100": (Path("/home/user/Sierra-Chart---DLLs/src"), {}),
}
GLOBAL_RE = re.compile(
    r"^\s*(?:static\s+)?(?:std::map(?:<[^;]*>)?|std::vector(?:<[^;]*>)?|std::string|int|double|float|bool|SCString)\s+g_\w+"
)
# Same-export collisions already documented in STUDIES.md as pick-one-per-build.
# New duplicates outside this set are hard errors.
KNOWN_COLLISIONS = {
    "Delta_Intensity",
    "InsideBarScanner",
    "RollingZScoreChannel",
    "SCOFAbsorptionDetector",
}
SCSF_RE = re.compile(r"SCSFExport\s+scsf_(\w+)")
SCDLL_RE = re.compile(r"SCDLLName\s*\(")
NEAREQUAL_RE = re.compile(r"return\s+abs\s*\(\s*value1\s*-\s*value2\s*\)")
SCALE3_RE = re.compile(r"\(\s*3\s*\*\s*percent\s*\)")
WEBHOOK_RE = re.compile(r"\{\\\"content\\\":\\\"")
LIVE_RE = re.compile(r"SendOrdersToTradeService|BuyEntry\s*\(|SellEntry\s*\(")
HTTP_RE = re.compile(r"MakeHTTPPOSTRequest|MakeHTTPRequest")
INPUT_RE = re.compile(r"SCInputRef\s+(\w+)\s*=\s*sc\.Input\[(\d+)\]")
ALERT_RE = re.compile(r"(?:AlertWithMessage|SetAlert)\s*\(\s*(\d+)")
SECRET_URL_RE = re.compile(r"discord\.com/api/webhooks/\d+/\S+|hooks\.slack\.com/\S+")


def strip_cpp_comments(text):
    """Remove /* */ + // comments (string/char aware; keeps newlines)."""
    out, i, n = [], 0, len(text or "")
    NORMAL, LINE, BLOCK, STR, CHR = range(5)
    state = NORMAL
    while i < n:
        c = text[i]
        d = text[i + 1] if i + 1 < n else ""
        if state == NORMAL:
            if c == "/" and d == "/":
                state = LINE
                i += 2
            elif c == "/" and d == "*":
                state = BLOCK
                i += 2
            elif c == '"':
                state = STR
                out.append(c)
                i += 1
            elif c == "'":
                state = CHR
                out.append(c)
                i += 1
            else:
                out.append(c)
                i += 1
        elif state == LINE:
            if c == "\n":
                state = NORMAL
                out.append(c)
            i += 1
        elif state == BLOCK:
            if c == "*" and d == "/":
                state = NORMAL
                i += 2
            else:
                if c == "\n":
                    out.append(c)
                i += 1
        elif state == STR:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
            else:
                if c == '"':
                    state = NORMAL
                i += 1
        else:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
            else:
                if c == "'":
                    state = NORMAL
                i += 1
    return "".join(out)


def study_exports(text):
    """scsf_ names actually compiled (commented-out examples excluded)."""
    return SCSF_RE.findall(strip_cpp_comments(text))


def native_sources():
    files = []
    for d in NATIVE_DIRS:
        if d.is_dir():
            files.extend(sorted(d.glob("*.cpp")))
    return files


def read_text(p):
    try:
        return Path(p).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None


def read_bytes(p):
    try:
        return Path(p).read_bytes()
    except OSError:
        return None

def cmd_check(args):
    alerts = {}  # alert id -> [files]; SC alert IDs are chart-global
    errors, warnings, infos = [], [], []

    exports = {}  # name -> [files]
    for f in native_sources():
        text = read_text(f) or ""
        names = study_exports(text)
        for m in names:
            exports.setdefault(m, []).append(f)
        if names and not SCDLL_RE.search(text):
            errors.append(f"{rel(f)}: missing SCDLLName (study will not load)")
    for name, files in sorted(exports.items()):
        if len(files) > 1:
            msg = ("duplicate scsf_%s: %s (never build together)"
                   % (name, ", ".join(rel(f) for f in files)))
            if name in KNOWN_COLLISIONS:
                warnings.append(msg + " — known pick-one variant, documented in STUDIES.md")
            else:
                errors.append(msg)
    studies_text = read_text(STUDIES_MD) or ""
    index_text = read_text(ROOT / "studies" / "index.html") or ""
    for f in native_sources():
        if f.name not in studies_text:
            errors.append(f"{rel(f)}: not listed in STUDIES.md")
        if f.name not in index_text:
            warnings.append(f"{rel(f)}: not in studies/index.html catalog")

    for f in native_sources():
        lines = (read_text(f) or "").splitlines()
        for i, line in enumerate(lines, 1):
            if GLOBAL_RE.match(line):
                warnings.append(
                    f"{rel(f)}:{i}: file-scope mutable global "
                    f"({line.strip()[:60]}…) — breaks with 2+ chart instances; "
                    "use sc.GetPersistent* instead"
                )
            if NEAREQUAL_RE.search(line) or SCALE3_RE.search(line):
                warnings.append(
                    f"{rel(f)}:{i}: IsNearEqual ignores candle size "
                    "(abs diff vs 3*percent) — likely meant fabs + "
                    "PercentOfCandleLength"
                )
        text = "\n".join(lines)
        if WEBHOOK_RE.search(text) and "escape" not in text.lower():
            warnings.append(
                f"{rel(f)}: Discord JSON built by string concat with no "
                "escaping — quotes/newlines in the message corrupt the payload"
            )
        if LIVE_RE.search(text):
            infos.append(f"{rel(f)}: touches live orders (audit before enabling)")
        for seg in re.split(r"SCSFExport\s+scsf_\w+", strip_cpp_comments(text)):
            seen_inputs = {}  # slot -> variable name, scoped per study
            for m in INPUT_RE.finditer(seg):
                var, slot = m.group(1), int(m.group(2))
                if slot in seen_inputs and seen_inputs[slot] != var:
                    errors.append(
                        f"{rel(f)}: sc.Input[{slot}] bound to both "
                        f"{seen_inputs[slot]} and {var} in one study — one "
                        "setting silently overwrites the other; move one to "
                        "a free slot"
                    )
                else:
                    seen_inputs.setdefault(slot, var)
        for m in ALERT_RE.finditer(strip_cpp_comments(text)):
            alerts.setdefault(int(m.group(1)), []).append(f)
    for aid, files in sorted(alerts.items()):
        uniq = sorted(set(rel(f) for f in files))
        if len(uniq) > 1:
            # Pick-one variants are never built together, so a shared ID
            # between them cannot cross-trigger — same rationale as the
            # duplicate-export downgrade above.
            fset = set(files)
            pick_one = any(
                name in KNOWN_COLLISIONS
                and fset <= set(flist)
                for name, flist in exports.items()
            )
            msg = (f"alert {aid} shared by {', '.join(uniq)} — SC alert IDs "
                   "are chart-global; enabling alerts on both studies "
                   "cross-triggers")
            (infos if pick_one else warnings).append(
                msg + (" (pick-one variants, never built together)" if pick_one else ""))
    for preset in sorted(ROOT.glob("*.Cht")) + sorted(ROOT.glob("*.StdyCollct")):
        blob = read_bytes(preset)
        if blob and SECRET_URL_RE.search(blob.decode("utf-8", errors="ignore")):
            warnings.append(
                f"{preset.name}: embedded webhook URL — chartbooks store input "
                "strings in plaintext; keep this file private or it leaks the secret"
            )
    for d in check_vendor_drift():
        warnings.append(d)
    for e in errors:
        print(f"ERROR: {e}")
    for w in warnings:
        print(f"WARN:  {w}")
    for i in infos:
        print(f"INFO:  {i}")
    print(f"\ncheck: {len(errors)} error(s), {len(warnings)} warning(s), "
          f"{len(infos)} note(s), {len(exports)} study export(s)")
    if errors or (args.strict and warnings):
        return 1
    return 0


def rel(p):
    try:
        return str(Path(p).relative_to(ROOT))
    except ValueError:
        return str(p)


def vendor_pairs():
    """Yield (vendor_path, upstream_path) for every managed reference file."""
    pairs = []
    pairs.extend(
        (VENDOR / "sierrachart-studies" / n, up / src)
        for n, src in {
            "Orion.cpp": "Orion.cpp",
            "orion_core.h": "orion_core.h",
            "FlipperStudies.cpp": "FlipperStudies.cpp",
            "SatyPivotRibbon.cpp": "SatyPivotRibbon.cpp",
            "DiscordAlerts.cpp": "DiscordAlerts.cpp",
            "InitialBalanceStatistics.cpp": "InitialBalanceStatistics.cpp",
            "PropRiskOverlay.cpp": "PropRiskOverlay.cpp",
            "BacktestExporter.cpp": "backtest/exporter/BacktestExporter.cpp",
        }.items()
        for up in [VENDOR_MAP["sierrachart-studies"][0]]
    )
    for vend_name, (up, _) in VENDOR_MAP.items():
        if vend_name == "sierrachart-studies":
            continue
        vdir = VENDOR / vend_name
        if not up.is_dir():
            continue
        for src in sorted(up.glob("*.cpp")):
            pairs.append((vdir / src.name, src))
        helpers = up / "helpers.h"
        if helpers.exists():
            pairs.append((vdir / "helpers.h", helpers))
    return pairs


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()[:12]


def check_vendor_drift():
    notes = []
    for vend, up in vendor_pairs():
        if not up.exists():
            notes.append(f"vendor drift: upstream missing {up} (skipped)")
        elif not vend.exists():
            notes.append(f"vendor drift: {rel(vend)} missing — run sync")
        elif sha(vend) != sha(up):
            notes.append(f"vendor drift: {rel(vend)} differs from upstream — run sync")
    return notes


def cmd_sync(args):
    pairs = vendor_pairs()
    if args.check:
        notes = check_vendor_drift()
        if notes:
            print("\n".join(notes))
            return 1
        print(f"vendor in sync ({len(pairs)} files)")
        return 0
    copied, skipped = 0, 0
    for vend, up in pairs:
        if not up.exists():
            print(f"SKIP  {rel(up)} (upstream missing)")
            skipped += 1
            continue
        vend.parent.mkdir(parents=True, exist_ok=True)
        if vend.exists() and sha(vend) == sha(up):
            skipped += 1
            continue
        shutil.copy2(up, vend)
        print(f"COPY  {rel(up)} -> {rel(vend)}")
        copied += 1
    if SOURCES_MD.exists() and copied:
        text = SOURCES_MD.read_text(encoding="utf-8")
        new_text = re.sub(r"synced \d{4}-\d{2}-\d{2}", f"synced {date.today():%Y-%m-%d}", text)
        if new_text != text:
            SOURCES_MD.write_text(new_text, encoding="utf-8")
            print(f"TOUCH {rel(SOURCES_MD)} (sync date)")
    print(f"\nsync: {copied} copied, {skipped} up to date")
    return 0


def resolve_native(name):
    """Find a native source by basename or repo-relative path."""
    p = Path(name)
    cand = (ROOT / p) if not p.is_absolute() else p
    if cand.is_file():
        return cand
    for f in native_sources():
        if f.name == p.name:
            return f
    return None


def cmd_install(args):
    dest = Path(args.to) if args.to else acs_source()
    if args.all:
        files = native_sources()
    else:
        if not args.file:
            print("error: give --file and/or --all", file=sys.stderr)
            return 2
        files = []
        for name in args.file:
            f = resolve_native(name)
            if f is None:
                print(f"error: no native source matching {name!r}", file=sys.stderr)
                return 1
            files.append(f)
    manifest = manifest_for(files)
    missing = manifest_missing(manifest)
    if missing:
        for src, m in missing:
            print(f"error: {rel(src)}: missing header {m}", file=sys.stderr)
        return 1
    copies, errors = stage_copies(manifest)
    if errors:
        for e in errors:
            print(f"error: {e}", file=sys.stderr)
        return 1
    if args.dry_run:
        print(f"would deploy {len(copies)} file(s) to {dest}:")
        for repo_path, _ in copies:
            print(f"  {rel(repo_path)}")
        return 0
    if args.to:
        dest.mkdir(parents=True, exist_ok=True)
    if not dest.is_dir():
        print(f"error: build folder not found: {dest} "
              "(set SC_ACS_SOURCE or use --to)", file=sys.stderr)
        return 1
    for repo_path, dest_rel in copies:
        target = dest / dest_rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(repo_path, target)
        print(f"COPY  {rel(repo_path)} -> {target}")
    fams = sorted({m for f in files for m in study_exports(read_text(f) or "")
                   if m in KNOWN_COLLISIONS})
    for m in fams:
        print(f"NOTE:  scsf_{m} is a pick-one-per-build family — select only one "
              "of its files in Remote Build")
    print(f"\ninstall: {len(copies)} file(s) to {dest}")
    print("next: Sierra Chart → Analysis → Build Custom Studies DLL (Remote Build)")
    return 0


INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)
STR_RUN_RE = re.compile(rb'[\x20-\x7e]{4,}')
MINGW = "x86_64-w64-mingw32-g++"


def study_deps(src):
    """Quoted-include closure for one study.

    Returns (headers, missing): headers are repo files the study transitively
    needs beside sierrachart.h; missing are 'file -> include' strings for
    headers that do not exist (Remote Build fails on these).
    """
    headers, missing, seen, stack = [], [], set(), [Path(src)]
    while stack:
        cur = stack.pop()
        if cur in seen:
            continue
        seen.add(cur)
        try:
            text = cur.read_text(encoding="utf-8", errors="replace")
        except OSError:
            missing.append(f"{cur.name} (unreadable)")
            continue
        for inc in INCLUDE_RE.findall(text):
            if inc == "sierrachart.h":
                continue
            cand = Path(os.path.normpath(cur.parent / inc))
            if cand.is_file():
                if cand not in seen:
                    headers.append(cand)
                    stack.append(cand)
            else:
                missing.append(f"{cur.name} -> {inc}")
    return headers, missing


def manifest_for(files):
    """[(src, headers, missing)] for the given native sources."""
    return [(f, *study_deps(f)) for f in files]


def build_manifest():
    """[(src, headers, missing)] for every native source."""
    return manifest_for(native_sources())


def stage_copies(manifest):
    """Map a manifest onto a flat stage layout: [(repo_path, dest_rel)].

    Sources land at the stage root (Remote Build compiles the flat folder);
    headers keep their path relative to their study's dir (e.g. zbyte's
    include/ + modules/). Returns (copies, errors).
    """
    copies, errors, claimed = [], [], {}

    def claim(repo_path, dest_rel):
        prev = claimed.get(dest_rel)
        if prev is not None and prev != repo_path:
            errors.append(f"stage collision: {rel(prev)} and {rel(repo_path)} "
                          f"both map to {dest_rel}")
            return
        claimed[dest_rel] = repo_path
        if (repo_path, dest_rel) not in copies:
            copies.append((repo_path, dest_rel))

    for src, headers, missing in manifest:
        if missing:
            continue
        claim(src, Path(src.name))
        for h in headers:
            try:
                dest_rel = h.relative_to(src.parent)
            except ValueError:
                errors.append(f"{rel(src)}: header {rel(h)} escapes the study dir")
                continue
            claim(h, dest_rel)
    return copies, errors


def data_dir():
    """Live Sierra Data folder: $SC_DATA_DIR or beside the build folder."""
    env = os.environ.get("SC_DATA_DIR")
    return Path(env) if env else acs_source().parent / "Data"


def dll_strings(dll):
    """ASCII runs in a binary (export + study names survive as plain text)."""
    try:
        blob = Path(dll).read_bytes()
    except OSError:
        return set()
    return set(m.group(0).decode("ascii") for m in STR_RUN_RE.finditer(blob))


def manifest_missing(manifest):
    return [(src, m) for src, _, missing in manifest for m in missing]


def build_plan():
    manifest = build_manifest()
    bad = 0
    for src, headers, missing in manifest:
        if missing:
            print(f"MISSING  {rel(src)}")
            for m in missing:
                print(f"           {m}")
            bad += 1
        else:
            print(f"ok       {rel(src)} ({len(headers)} header(s))")
    _, errors = stage_copies(manifest)
    for e in errors:
        print(f"ERROR: {e}")
        bad += 1
    fams = sorted({m for src, _, _ in manifest
                   for m in study_exports(read_text(src) or "")
                   if m in KNOWN_COLLISIONS})
    for m in fams:
        print(f"NOTE:  scsf_{m} is a pick-one family — build every file, "
              "but load only one of its DLLs at a time")
    n = len(manifest)
    print(f"\nbuild plan: {n} {'study' if n == 1 else 'studies'}, "
          f"{'BLOCKED' if bad else 'all deps resolve'}")
    return 1 if bad else 0


def build_stage(to, dry_run):
    dest = Path(to) if to else acs_source()
    manifest = build_manifest()
    missing = manifest_missing(manifest)
    if missing:
        for src, m in missing:
            print(f"ERROR: {rel(src)}: missing header {m}", file=sys.stderr)
        return 1
    copies, errors = stage_copies(manifest)
    if errors:
        for e in errors:
            print(f"ERROR: {e}", file=sys.stderr)
        return 1
    if dry_run:
        print(f"would stage {len(copies)} file(s) to {dest}:")
        for repo_path, dest_rel in copies:
            print(f"  {rel(repo_path)} -> {dest_rel}")
        return 0
    if to:
        dest.mkdir(parents=True, exist_ok=True)
    if not dest.is_dir():
        print(f"error: build folder not found: {dest} "
              "(set SC_ACS_SOURCE or use --to)", file=sys.stderr)
        return 1
    for repo_path, dest_rel in copies:
        target = dest / dest_rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(repo_path, target)
        print(f"COPY  {rel(repo_path)} -> {target}")
    print(f"\nstage: {len(copies)} file(s) to {dest}")
    print("next: select every staged .cpp in Remote Build (one trip, one DLL each)")
    return 0


def build_local(include):
    cc = shutil.which(MINGW)
    if cc is None:
        print(f"error: {MINGW} not found — no local syntax check available",
              file=sys.stderr)
        return 1
    inc = Path(include) if include else acs_source()
    if not (inc / "sierrachart.h").is_file():
        print(f"error: sierrachart.h not found in {inc}", file=sys.stderr)
        return 1
    fails = 0
    files = native_sources()
    for src in files:
        p = subprocess.run([cc, "-fsyntax-only", "-std=c++17", f"-I{inc}", str(src)],
                           capture_output=True, text=True, timeout=300)
        if p.returncode != 0:
            fails += 1
            print(f"FAIL  {rel(src)}")
            for line in p.stderr.splitlines()[:8]:
                print(f"        {line}")
        else:
            print(f"ok    {rel(src)}")
    print(f"\nlocal: {len(files) - fails}/{len(files)} file(s) pass")
    return 1 if fails else 0


def build_dll(to, force):
    cc = shutil.which(MINGW)
    if cc is None:
        print(f"error: {MINGW} not found — cannot build locally",
              file=sys.stderr)
        return 1
    inc = acs_source()
    if not (inc / "sierrachart.h").is_file():
        print(f"error: sierrachart.h not found in {inc}", file=sys.stderr)
        return 1
    ddir = Path(to) if to else data_dir()
    ddir.mkdir(parents=True, exist_ok=True)
    manifest = build_manifest()
    missing = manifest_missing(manifest)
    if missing:
        for src, m in missing:
            print(f"ERROR: {rel(src)}: missing header {m}", file=sys.stderr)
        return 1
    built, skipped, fails = 0, 0, 0
    for src, headers, _ in manifest:
        dll = ddir / f"{src.stem}_64.dll"
        inputs = [src] + headers
        if (not force and dll.is_file()
                and all(dll.stat().st_mtime >= p.stat().st_mtime for p in inputs)):
            print(f"skip    {rel(src)} (up to date)")
            skipped += 1
            continue
        tmp = dll.with_suffix(".tmp.dll")
        p = subprocess.run(
            [cc, "-shared", "-O2", "-std=c++17", "-static",
             f"-I{inc}", str(src), "-o", str(tmp)],
            capture_output=True, text=True, timeout=600)
        if p.returncode != 0:
            print(f"FAIL  {rel(src)}")
            for line in p.stderr.splitlines()[:8]:
                print(f"        {line}")
            tmp.unlink(missing_ok=True)
            fails += 1
            continue
        names = dll_strings(tmp)
        absent = [e for e in study_exports(read_text(src) or "")
                  if f"scsf_{e}" not in names]
        if absent:
            print(f"FAIL  {rel(src)}: no export "
                  f"{', '.join('scsf_' + e for e in absent)}")
            tmp.unlink(missing_ok=True)
            fails += 1
            continue
        os.replace(tmp, dll)
        print(f"ok      {rel(src)} -> {dll.name}")
        built += 1
    print(f"\ndll: {built} built, {skipped} up to date, {fails} failed")
    return 1 if fails else 0


def build_verify(data_dir_arg):
    ddir = Path(data_dir_arg) if data_dir_arg else data_dir()
    bad = 0
    for src in native_sources():
        exports = study_exports(read_text(src) or "")
        if not exports:
            print(f"SKIP  {rel(src)} (no scsf_ exports)")
            continue
        dll = ddir / f"{src.stem}_64.dll"
        if not dll.is_file():
            print(f"MISSING  {rel(src)}: {dll.name} not in {ddir}")
            bad += 1
            continue
        problems = []
        if dll.stat().st_mtime < src.stat().st_mtime:
            problems.append("stale (source is newer — rebuild)")
        names = dll_strings(dll)
        absent = [e for e in exports if f"scsf_{e}" not in names]
        if absent:
            problems.append(f"exports absent: {', '.join('scsf_' + e for e in absent)}")
        if problems:
            print(f"STALE   {rel(src)}: {'; '.join(problems)}")
            bad += 1
        else:
            print(f"ok      {rel(src)} ({dll.name})")
    print(f"\nverify: {'all studies available' if not bad else f'{bad} ' + ('study needs' if bad == 1 else 'studies need') + ' a build'}")
    return 1 if bad else 0


def cmd_build(args):
    if args.action == "plan":
        return build_plan()
    if args.action == "stage":
        return build_stage(args.to, args.dry_run)
    if args.action == "local":
        return build_local(args.include)
    if args.action == "dll":
        return build_dll(args.to, args.force)
    if args.action == "verify":
        return build_verify(args.data_dir)
    print(f"error: unknown build action {args.action!r}", file=sys.stderr)
    return 2


GRAPHNAME_RE = re.compile(r'sc\.GraphName\s*=\s*"([^"]+)"')
# native dir key -> (STUDIES.md section header, index.html group title)
CATALOG_SECTIONS = {
    ".": ("## 1.", "House studies — repo root"),
    "EdgeFul Indicators": ("## 2.", "EdgeFul Indicators/"),
    "zbyte": ("## 3.", "zbyte/"),
}
STATUS_WORDS = {"Active", "Source-only", "Legacy", "Variant",
                "Helper/Test", "Binary-only", "Reference"}


def js_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", " ")


def do_catalog_add(src, desc, status):
    """Append STUDIES.md row(s) + index.html card for a native source."""
    if not status.split() or status.split()[0] not in STATUS_WORDS:
        print(f"error: status {status!r} must start with one of "
              f"{sorted(STATUS_WORDS)}", file=sys.stderr)
        return 2
    if "]}" in desc:
        print("error: description must not contain ']}'", file=sys.stderr)
        return 2
    text = read_text(src)
    if text is None:
        print(f"error: cannot read {src}", file=sys.stderr)
        return 1
    try:
        key = str(src.parent.relative_to(ROOT))
    except ValueError:
        print(f"error: {src} is outside the repo", file=sys.stderr)
        return 2
    if key == "":
        key = "."
    if key not in CATALOG_SECTIONS:
        print(f"error: {src} is not a native study dir "
              "(root, EdgeFul Indicators, zbyte)", file=sys.stderr)
        return 2
    section, group = CATALOG_SECTIONS[key]
    studies_text = read_text(STUDIES_MD) or ""
    if src.name in studies_text:
        print(f"error: {src.name} is already registered in STUDIES.md",
              file=sys.stderr)
        return 1
    exports = study_exports(text)
    graphs = GRAPHNAME_RE.findall(text)
    names = exports or ["—"]
    rows = []
    for i, exp in enumerate(names):
        chart = graphs[i] if i < len(graphs) else exp
        study = f"`{exp}` / {chart}" if exp != "—" else "—"
        rows.append(f"| `{src.name}` | {study} | {desc} | {status} |")
    lines = studies_text.splitlines()
    head = next((n for n, ln in enumerate(lines) if ln.startswith(section)), None)
    if head is None:
        print(f"error: section {section} not found in STUDIES.md", file=sys.stderr)
        return 1
    nxt = next((n for n in range(head + 1, len(lines)) if lines[n].startswith("## ")),
               len(lines))
    last = next((n for n in range(nxt - 1, head, -1) if lines[n].startswith("| `")), None)
    if last is None:
        print(f"error: no table rows under {section} in STUDIES.md", file=sys.stderr)
        return 1
    # Validate the index.html slot BEFORE mutating either file: a failed
    # add must leave no row-without-card behind (and must stay retryable).
    index_path = ROOT / "studies" / "index.html"
    index_text = read_text(index_path) or ""
    marker = '{g:"' + group + '"'
    gi = index_text.find(marker)
    if gi < 0:
        print(f"error: group {group!r} not found in studies/index.html",
              file=sys.stderr)
        return 1
    end = index_text.find("]}", gi)
    if end < 0:
        print("error: malformed DATA group in studies/index.html", file=sys.stderr)
        return 1
    lines[last + 1:last + 1] = rows
    STUDIES_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")
    title = f"{src.name} — {graphs[0]}" if graphs else src.name
    scsfs = ", ".join(f"scsf_{e}" for e in exports) if exports else "—"
    card = (f',\n["{js_escape(title)}","{js_escape(scsfs)}",'
            f'"{js_escape(desc)}","{js_escape(status)}"]')
    index_path.write_text(index_text[:end] + card + index_text[end:],
                           encoding="utf-8")
    print(f"registered {src.name} ({len(rows)} STUDIES.md row(s) + 1 catalog card)")
    return 0


def cmd_catalog(args):
    src = resolve_native(args.file)
    if src is None:
        print(f"error: no native source matching {args.file!r}", file=sys.stderr)
        return 1
    return do_catalog_add(src, args.desc, args.status)


VALID_IDENT = re.compile(r"^[A-Za-z_]\w*$")


def cmd_new(args):
    if not VALID_IDENT.match(args.study):
        print(f"error: study name '{args.study}' is not a C identifier", file=sys.stderr)
        return 2
    dest = (ROOT / args.dir / args.file).resolve()
    try:
        dest.relative_to(ROOT)
    except ValueError:
        print("error: destination must stay inside the repo", file=sys.stderr)
        return 2
    if dest.exists() and not args.force:
        print(f"error: {rel(dest)} exists (use --force to overwrite)", file=sys.stderr)
        return 1
    for f in native_sources():
        if f"scsf_{args.study}" in (read_text(f) or ""):
            print(f"error: scsf_{args.study} already exists in {rel(f)}", file=sys.stderr)
            return 1
    tpl = TEMPLATE.read_text(encoding="utf-8")
    out = tpl.replace("MyStudy", args.study).replace("My Study", args.name)
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(out, encoding="utf-8")
    print(f"wrote {rel(dest)} (scsf_{args.study})")
    if getattr(args, "register", False):
        if not getattr(args, "desc", ""):
            print("error: --register needs --desc", file=sys.stderr)
            return 2
        rc = do_catalog_add(dest, args.desc, args.status)
        if rc:
            return rc
        print("next: python3 tools/sc.py check")
        print("      copy into ACS_Source (tools/sc.py install) and Remote Build")
        return 0
    print("next: python3 tools/sc.py check")
    print("      add a STUDIES.md row + studies/index.html card")
    print("      copy into ACS_Source and Remote Build")
    return 0


TIME_RE = re.compile(r"^([01]\d|2[0-3]):([0-5]\d)$")


def validate_slot_csv(path):
    """Validate a TimeSlotValue CSV. Returns (rows, errors)."""
    rows, errors, seen = 0, [], set()
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        for i, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) != 2:
                errors.append(f"{path}:{i}: want 'HH:MM,value', got {line!r}")
                continue
            t, v = parts
            if not TIME_RE.match(t):
                # allow HHMM numeric fallback (study parses it too),
                # normalized so 0900 and 09:00 collide as duplicates
                if t.isdigit() and len(t) == 4 and TIME_RE.match(t[:2] + ":" + t[2:]):
                    t = t[:2] + ":" + t[2:]
                else:
                    errors.append(f"{path}:{i}: bad time {t!r}")
                    continue
            try:
                int(v)
            except ValueError:
                errors.append(f"{path}:{i}: bad value {v!r}")
                continue
            if t in seen:
                errors.append(f"{path}:{i}: duplicate slot {t}")
                continue
            seen.add(t)
            rows += 1
    return rows, errors


BAR_ALIASES = {
    "datetime": "stamp", "date_time": "stamp", "time": "stamp",
    "date": "stamp", "timestamp": "stamp",
    "o": "open", "open": "open",
    "h": "high", "high": "high",
    "l": "low", "low": "low",
    "c": "close", "close": "close", "last": "close",
}
BAR_DT_FORMATS = ("%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M",
                  "%Y/%m/%d %H:%M:%S", "%Y/%m/%d %H:%M", "%Y-%m-%d")
SIGNAL_COLS = {"signallong", "signal_long", "signalshort", "signal_short",
                "setuplong", "setup_long", "setupshort", "setup_short"}


def parse_bar_stamp(s):
    s = s.strip()
    if s.isdigit():
        return s  # epoch — accepted, ordering checked lexicographically downstream
    for fmt in BAR_DT_FORMATS:
        try:
            from datetime import datetime
            datetime.strptime(s, fmt)
            return s
        except ValueError:
            continue
    return None


def validate_bars_csv(path):
    """Validate an OHLC/signal export against the backtester data contract.
    Returns (n_bars, n_signals, first, last, errors)."""
    errors = []
    try:
        f = open(path, newline="", encoding="utf-8", errors="replace")
    except OSError as e:
        return 0, 0, "", "", [f"{path}: cannot open ({e})"]
    with f:
        reader = csv.DictReader(f)
        if reader.fieldnames is None:
            return 0, 0, "", "", [f"{path}: empty file (no header)"]
        norm = { (c or "").strip().lower(): c for c in reader.fieldnames }
        canon = {alias: norm[a] for a, alias in BAR_ALIASES.items() if a in norm}
        # canon maps role -> file column; keep first column per role
        roles = {}
        for role, col in canon.items():
            roles.setdefault(role, col)
        missing = [r for r in ("stamp", "open", "high", "low", "close")
                   if r not in roles]
        if missing:
            return 0, 0, "", "", [
                f"{path}: missing columns: {', '.join(missing)} "
                f"(have: {', '.join(reader.fieldnames)})"]
        sig_cols = [norm[c] for c in SIGNAL_COLS if c in norm]
        n_bars = n_signals = 0
        first = last = ""
        for i, row in enumerate(reader, 2):
            try:
                stamp_raw = (row[roles["stamp"]] or "").strip()
                o = float(row[roles["open"]] or "nan")
                h = float(row[roles["high"]] or "nan")
                l = float(row[roles["low"]] or "nan")
                c = float(row[roles["close"]] or "nan")
            except (ValueError, TypeError):
                if len(errors) < 20:
                    errors.append(f"{path}:{i}: non-numeric OHLC")
                continue
            if parse_bar_stamp(stamp_raw) is None:
                if len(errors) < 20:
                    errors.append(f"{path}:{i}: bad DateTime {stamp_raw!r}")
                continue
            if not (h >= l and l - 1e-9 <= o <= h + 1e-9
                    and l - 1e-9 <= c <= h + 1e-9):
                if len(errors) < 20:
                    errors.append(f"{path}:{i}: OHLC range violation "
                                  f"O={o} H={h} L={l} C={c}")
                continue
            n_bars += 1
            if not first:
                first = stamp_raw
            last = stamp_raw
            for sc in sig_cols:
                try:
                    if float(row[sc] or 0) != 0:
                        n_signals += 1
                        break
                except (ValueError, TypeError):
                    pass
    return n_bars, n_signals, first, last, errors


def cmd_data(args):
    if args.action == "list":
        found = sorted(ROOT.glob("**/*.csv"))
        for p in found:
            if ".git" in p.parts:
                continue
            print(rel(p))
        print(f"\n{len(found)} csv file(s)")
        return 0
    if args.action == "bars":
        if not args.files:
            print("error: data bars needs explicit CSV file(s) "
                  "(slot CSVs use data validate)", file=sys.stderr)
            return 2
        total_bars, total_errs = 0, 0
        for name in args.files:
            t = Path(name)
            n, sig, first, last, errs = validate_bars_csv(t)
            total_bars += n
            total_errs += len(errs)
            for e in errs[:20]:
                print(f"ERROR: {e}")
            if len(errs) > 20:
                print(f"ERROR: ... plus {len(errs) - 20} more in {t}")
            if not errs:
                span = f"{first} → {last}" if first else "empty"
                print(f"OK    {t} ({n} bars, {sig} signal bars, {span})")
        print(f"\ndata bars: {total_bars} bar(s), {total_errs} error(s)")
        return 1 if total_errs else 0
    # validate

    targets = [Path(a) for a in args.files] if args.files else sorted(
        p for p in ROOT.glob("**/*.csv") if ".git" not in p.parts
    )
    total_rows, total_errs = 0, 0
    for t in targets:
        if not t.exists():
            print(f"ERROR: {t} not found", file=sys.stderr)
            total_errs += 1
            continue
        rows, errs = validate_slot_csv(t)
        total_rows += rows
        total_errs += len(errs)
        for e in errs:
            print(f"ERROR: {e}")
        if not errs:
            print(f"OK    {t} ({rows} slots)")
    print(f"\ndata validate: {total_rows} slot(s), {total_errs} error(s)")
    return 1 if total_errs else 0


MAX_COMBOS = 200


def parse_grid_val(text):
    """Parse 'a,b,c' into [int|float|bool|str]."""
    vals = []
    for tok in text.split(","):
        t = tok.strip()
        if t.lower() == "true":
            vals.append(True)
        elif t.lower() == "false":
            vals.append(False)
        else:
            try:
                vals.append(int(t))
            except ValueError:
                try:
                    vals.append(float(t))
                except ValueError:
                    vals.append(t)
    return vals


def bt_call(*argv):
    cmd = [sys.executable, str(UPSTREAM_BT)] + list(argv)
    print("$ " + " ".join(cmd))
    return subprocess.call(cmd, cwd=BT_DIR)


def cmd_optimize(args):
    if not UPSTREAM_BT.exists():
        print(f"error: upstream backtester not found at {UPSTREAM_BT}", file=sys.stderr)
        return 1
    try:
        base = json.loads(Path(args.base).read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        print(f"error: cannot load base params {args.base} ({e})", file=sys.stderr)
        return 1
    grid = {}
    for spec in args.param:
        if "=" not in spec:
            print(f"error: --param must be NAME=v1,v2 (got {spec!r})", file=sys.stderr)
            return 2
        k, v = spec.split("=", 1)
        vals = parse_grid_val(v)
        if not vals:
            print(f"error: empty grid for {k!r}", file=sys.stderr)
            return 2
        grid[k.strip()] = vals
    if not grid:
        print("error: give at least one --param NAME=v1,v2", file=sys.stderr)
        return 2
    combos = 1
    for vals in grid.values():
        combos *= len(vals)
    if combos > MAX_COMBOS:
        print(f"error: {combos} combos exceeds the {MAX_COMBOS} cap "
              "(narrow the grid)", file=sys.stderr)
        return 1
    outdir = Path(args.out)
    outdir.mkdir(parents=True, exist_ok=True)
    sweep = dict(base)
    sweep["grid"] = grid
    sweep["_comment"] = (f"OPTIMIZE: grid over {sorted(grid)} "
                         f"({combos} combos) from {args.base}")
    spath = outdir / "sweep.json"
    spath.write_text(json.dumps(sweep, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {spath} ({combos} combos)")
    if args.dry_run:
        return 0
    data = str(Path(args.data).resolve())
    rc = bt_call("sweep", "--data", data, "--params", str(spath.resolve()),
                 "--out", str((outdir / "sweep").resolve()),
                 "--tag-prefix", args.tag)
    if rc:
        return rc
    dataset = args.dataset or args.data
    cmd = ["compare", "--dataset", dataset, "--out", str((outdir / "leaderboard.html").resolve())]
    if args.study:
        cmd += ["--study", args.study]
    rc = bt_call(*cmd)
    if rc:
        return rc
    print(f"\noptimize: sweep + leaderboard in {outdir}")
    print("next: confirm winners out-of-sample, e.g.")
    print(f"  python3 tools/sc.py backtest run --data {args.data} "
          f"--params <winner>.json --split frac:0.7 --out {args.out}/confirm")
    return 0


def cmd_confirm(args):
    """IS + OOS-split + walkforward confirmation (ORB15 discipline)."""
    if not UPSTREAM_BT.exists():
        print(f"error: upstream backtester not found at {UPSTREAM_BT}", file=sys.stderr)
        return 1
    data = str(Path(args.data).resolve())
    params = str(Path(args.params).resolve())
    outdir = Path(args.out)
    tag = args.tag or "confirm"
    steps = [
        ["run", "--data", data, "--params", params,
         "--out", str((outdir / "is").resolve()), "--tag", tag],
        ["run", "--data", data, "--params", params,
         "--out", str((outdir / "oos").resolve()), "--tag", tag + "-oos",
         "--split", args.split],
    ]
    if not args.skip_walkforward:
        wf = ["walkforward", "--data", data, "--params", params,
              "--out", str((outdir / "wf").resolve()), "--tag", tag + "-wf"]
        for flag in ("--train", "--test", "--step", "--embargo-days"):
            val = getattr(args, flag.lstrip("-").replace("-", "_"), "")
            if val:
                wf += [flag, val]
        steps.append(wf)
    for cmd in steps:
        rc = bt_call(*cmd)
        if rc:
            return rc
    tail = "IS + OOS(%s)" % args.split
    if not args.skip_walkforward:
        tail += " + walkforward"
    print(f"\nconfirm: {tail} in {outdir}")
    print(f"next: pick the OOS run id (tag {tag}-oos) and gate shipping with")
    print("  python3 tools/sc.py backtest promote --run <id>")
    return 0


def cmd_strategies(args):
    spath = BT_DIR / "strategies.py"
    if not spath.exists():
        print(f"error: upstream backtester not found at {BT_DIR}", file=sys.stderr)
        return 1
    sys.path.insert(0, str(BT_DIR))
    try:
        import strategies as upstream_strategies
        items = sorted(upstream_strategies.STRATEGIES.items())
        found = [(n, ((fn.__doc__ or "").strip().splitlines() or [""])[0])
                 for n, fn in items]
    except Exception:
        found = [(n, "") for n in
                 re.findall(r'"(\w+)"\s*:\s*\w+', spath.read_text(encoding="utf-8"))]
    finally:
        try:
            sys.path.remove(str(BT_DIR))
        except ValueError:
            pass
    for n, doc in found:
        print(f"{n}" + (f" — {doc}" if doc else ""))
    params = sorted(BT_DIR.glob("params.*.json"))
    if params:
        print("\nparams files:")
        for p in params:
            print(f"  {p.name}")
    print("\nrun one: python3 tools/sc.py backtest run --data <csv> "
          "--params <params.json> --out <dir>")
    return 0


def cmd_backtest(args):
    if not UPSTREAM_BT.exists():
        print(f"error: upstream backtester not found at {UPSTREAM_BT}", file=sys.stderr)
        return 1
    cmd = [sys.executable, str(UPSTREAM_BT)] + args.rest
    print("$ " + " ".join(cmd))
    return subprocess.call(cmd, cwd=UPSTREAM_BT.parent)


HARNESS_VERBS = ("ADD", "RESOLVE", "SETINT", "SETFLOAT", "SETSTRING",
                 "WIRE", "VERIFY", "RECALC", "REMOVE", "WHOAMI")
HARNESS_JOB_NAME = "bt_harness_job.txt"


def harness_paths():
    """(job_path, result_path) in the live Sierra Data dir."""
    job = data_dir() / HARNESS_JOB_NAME
    return job, job.with_name(job.name + ".result")


def build_harness_job(cmds):
    """Join + validate harness commands. Raises ValueError on bad verbs."""
    lines = []
    for c in cmds:
        s = c.strip()
        if not s or s.startswith("#"):
            continue
        verb = s.split(None, 1)[0].upper()
        if verb not in HARNESS_VERBS:
            raise ValueError(f"unknown harness verb {verb!r} (want one of "
                             f"{', '.join(HARNESS_VERBS)})")
        lines.append(s)
    if not lines:
        raise ValueError("no harness commands given")
    return "\n".join(lines) + "\n"


def harness_result_ok(text):
    """True when every result line is OK (and at least one exists)."""
    rows = [l for l in text.splitlines() if l.strip()]
    return bool(rows) and all(l.startswith("OK ") for l in rows)


def run_harness_job(cmds, timeout):
    """Write a job file, wait for the .result. Returns (rc, text)."""
    job, result = harness_paths()
    body = build_harness_job(cmds)
    if job.exists():
        return 1, (f"stale job file {job} — harness busy or crashed; "
                   f"remove it manually"), False
    if result.exists():
        result.unlink()
    job.write_text(body, encoding="utf-8")
    print(f"job -> {job} ({len(cmds)} command(s)); waiting up to "
          f"{timeout}s for Sierra (Backtest Harness must be on a chart)")
    deadline = time.time() + timeout
    while time.time() < deadline:
        if result.exists():
            text = result.read_text(encoding="utf-8")
            print(text, end="" if text.endswith("\n") else "\n")
            ok = harness_result_ok(text)
            print(f"result: {'OK' if ok else 'ERRORS'}")
            return (0 if ok else 1), text, True
        time.sleep(1)
    return 1, (f"no result after {timeout}s — is Sierra running with "
               f"the Backtest Harness study on a chart?"), False


def cmd_harness(args):
    if args.capture:
        return cmd_harness_capture(args)
    try:
        rc, text, from_result = run_harness_job(args.cmd, args.timeout)
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    if rc != 0 and not from_result:
        print(f"error: {text}", file=sys.stderr)
    return rc


ROUTING_PATH = ROOT / "studies" / "harness-routing.json"


def load_routing():
    return json.loads(ROUTING_PATH.read_text(encoding="utf-8"))


def harness_winpath(local_csv):
    """Data-dir CSV -> C:\\ path as Sierra sees it. Raises ValueError."""
    p = Path(local_csv)
    try:
        rel = p.resolve().relative_to(data_dir().resolve())
    except ValueError:
        raise ValueError(f"--out must be inside the Sierra Data dir "
                         f"({data_dir()})")
    return "C:\\SierraChart\\Data\\" + "\\".join(rel.parts)


RESOLVE_RE = re.compile(r"^OK RESOLVE (\S+) id=(\d+)\s*$")
VERIFY_RE = re.compile(r"^OK VERIFY (\S+)\[(\d+)\] <- id=(\d+) sg(\d+)")


def parse_resolve_ids(text):
    """Short name -> study id for every OK RESOLVE line."""
    ids = {}
    for line in text.splitlines():
        m = RESOLVE_RE.match(line.strip())
        if m:
            ids[m.group(1)] = int(m.group(2))
    return ids


def check_wire(text, cap, long_sg, short_sg):
    """Refuse a wrong-study export: VERIFY must show BTE wired to the
    resolved capture study's subgraphs. Returns an error string or None."""
    ids = parse_resolve_ids(text)
    capid = ids.get(cap)
    if capid is None:
        return f"capture study {cap!r} did not resolve"
    seen = {}
    for line in text.splitlines():
        m = VERIFY_RE.match(line.strip())
        if m and m.group(1) == "BTE":
            seen[int(m.group(2))] = (int(m.group(3)), int(m.group(4)))
    want = {1: (capid, long_sg), 2: (capid, short_sg)}
    if seen != want:
        return (f"wiring mismatch: BTE reads {seen}, want {want} "
                f"— refusing a wrong-study export")
    return None


def compose_capture(entry, name, win_csv):
    """Idempotent capture flow under one stable short name per study: a
    repeat capture reuses the chart instance instead of stacking a
    duplicate (whose default inputs can silently export zeros)."""
    probe = ["WHOAMI", f"RESOLVE {name}", "RESOLVE BTE"]
    wire = [f"RESOLVE {name}", "RESOLVE BTE",
            f"SETSTRING BTE 0 {win_csv}",
            f"WIRE BTE 1 {name} {entry['longSg']}",
            f"WIRE BTE 2 {name} {entry['shortSg']}",
            "VERIFY BTE 1", "VERIFY BTE 2", "RECALC"]
    return probe, wire


def cmd_harness_capture(args):
    try:
        routing = load_routing()
        entry = routing["studies"][args.capture]
    except (OSError, ValueError, KeyError):
        print(f"error: unknown study {args.capture!r} "
              f"(see studies/harness-routing.json)", file=sys.stderr)
        return 2
    if entry.get("class") != "TRIGGER" or not isinstance(
            entry.get("longSg"), int) or not isinstance(
            entry.get("shortSg"), int):
        print(f"error: {args.capture!r} is not a wired TRIGGER study "
              f"(audit its signal subgraphs first)", file=sys.stderr)
        return 2
    try:
        win_csv = harness_winpath(args.out)
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    out_csv = Path(args.out)
    name = args.capture
    probe, wire = compose_capture(entry, name, win_csv)
    try:
        _, probe_text, live = run_harness_job(probe, args.timeout)
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    if not live:
        print(f"error: {probe_text}", file=sys.stderr)
        return 1
    have = parse_resolve_ids(probe_text)
    adds = []
    if name not in have:
        adds.append(f"ADD {entry['dll']}.{entry['scsf']} AS {name}")
    if "BTE" not in have:
        adds.append("ADD BacktestExporter_64.scsf_BacktestExporter AS BTE")
    if adds:
        try:
            rc, _, live = run_harness_job(adds + ["RECALC"], args.timeout)
        except ValueError as e:
            print(f"error: {e}", file=sys.stderr)
            return 2
        if not live or rc != 0:
            print("error: study ADD failed", file=sys.stderr)
            return 1
        print(f"settle {args.settle}s for study registration...")
        time.sleep(args.settle)
    try:
        rc2, wire_text, live = run_harness_job(wire, args.timeout)
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    if not live or rc2 != 0:
        print(f"error: wire job failed:\n{wire_text}", file=sys.stderr)
        return 1
    err = check_wire(wire_text, name, entry["longSg"], entry["shortSg"])
    if err:
        print(f"error: {err}", file=sys.stderr)
        return 1
    print(f"settle {args.settle}s for export write...")
    time.sleep(args.settle)
    if not out_csv.exists():
        print(f"error: no CSV at {out_csv} after recalc", file=sys.stderr)
        return 1
    # Quiescence: a heavy chart can still be writing when the settle
    # elapses (a partial read once reported 11 signals from a file that
    # settled at 3534). Wait for the size/mtime to stop moving, bounded.
    try:
        last_stat = (out_csv.stat().st_size, out_csv.stat().st_mtime_ns)
    except OSError:
        last_stat = None
    waited = 0
    while waited < args.timeout:
        time.sleep(5)
        waited += 5
        try:
            cur = (out_csv.stat().st_size, out_csv.stat().st_mtime_ns)
        except OSError:
            continue
        if cur == last_stat:
            break
        last_stat = cur
    else:
        print("warning: export file still moving after "
              f"{args.settle + args.timeout}s; validating anyway")
    n, sig, first, last, errs = validate_bars_csv(out_csv)
    for e in errs[:10]:
        print(f"ERROR: {e}")
    if errs:
        return 1
    print(f"OK {out_csv} ({n} bars, {sig} signal bars, {first} -> {last})")
    if sig == 0:
        print("note: zero signals is a verdict (silent study/window), "
              "not a failure — wiring above proves the exporter read "
              "the intended study")
    return 0


COMMIT_RE = re.compile(r"^[a-z][a-z0-9_/-]*: \S")
JUNK_FILE_RES = (re.compile(r"\[objectObject\]"),)


def root_junk():
    """Unambiguous tool accidents at the repo root: browser page exports
    (*_files/) and bad download names (*[objectObject]*)."""
    found = [p for p in sorted(ROOT.glob("*_files")) if p.is_dir()]
    try:
        entries = sorted(ROOT.iterdir())
    except OSError:
        return found
    found += [p for p in entries
              if p.is_file() and any(r.search(p.name) for r in JUNK_FILE_RES)]
    return found


def cli_commands():
    """Every invokable subcommand/action (e.g. 'build dll'), introspected
    from the parser so the docs check cannot drift from the code."""
    cmds = []
    top = build_parser()._subparsers._group_actions[0].choices
    for name, sp in sorted(top.items()):
        cmds.append(name)
        for act in sp._actions:
            if isinstance(act, argparse._SubParsersAction):
                cmds += [f"{name} {sub}" for sub in sorted(act.choices)]
            elif act.dest == "action" and getattr(act, "choices", None):
                cmds += [f"{name} {sub}" for sub in sorted(act.choices)]
    return cmds


def git_hygiene():
    """(status lines, HEAD subject); (None, None) when git is unavailable."""
    if not (ROOT / ".git").is_dir():
        return None, None
    try:
        st = subprocess.run(["git", "status", "--short"], cwd=ROOT,
                            capture_output=True, text=True, timeout=15)
        hd = subprocess.run(["git", "log", "-1", "--format=%s"], cwd=ROOT,
                            capture_output=True, text=True, timeout=15)
    except (OSError, subprocess.SubprocessError):
        return None, None
    if st.returncode or hd.returncode:
        return None, None
    return st.stdout.splitlines(), hd.stdout.strip()


def cmd_maintain(args):
    errors, warnings, infos = [], [], []

    if getattr(args, "rm_junk", False) and not getattr(args, "check", False):
        for p in root_junk():
            try:
                if p.is_dir():
                    shutil.rmtree(p)
                else:
                    p.unlink()
                print(f"REMOVED: {rel(p)}")
            except OSError as e:
                errors.append(f"{rel(p)}: could not remove ({e})")

    for p in root_junk():
        warnings.append(
            f"{rel(p)}: tool accident — delete with maintain --rm-junk")
    for p in sorted(ROOT.glob("*.bak*")):
        warnings.append(
            f"{rel(p)}: backup file — archive outside the repo or drop it")
    for p in sorted(ROOT.glob("*.cht")):
        warnings.append(
            f"{p.name}: lowercase preset — Sierra writes .Cht; rename "
            "for consistency")
    for p in sorted(ROOT.rglob("*.md")):
        if VENDOR in p.parents or not p.is_file():
            continue
        if not (read_text(p) or "").strip():
            warnings.append(
                f"{rel(p)}: empty placeholder doc — fill it in or delete it")

    studies_text = read_text(STUDIES_MD) or ""
    body = studies_text
    start = studies_text.find("## 7.")
    end = studies_text.find("## Alert ID registry")
    if start != -1 and end != -1 and start < end:
        body = studies_text[start:end]
    for tok in sorted(set(re.findall(r"`([^`]+)`", body))):
        tok = tok.strip()
        if not tok or "…" in tok:
            continue
        suf = tok.rsplit(".", 1)[-1] if "." in tok else ""
        if suf not in ("Cht", "cht", "StdyCollct", "dll", "cpp", "h", "csv"):
            continue
        if any(c in tok for c in "*?[]"):
            if not list(ROOT.glob("**/" + tok)):
                errors.append(
                    f"STUDIES.md names `{tok}` but nothing in the repo matches it")
        elif next(ROOT.rglob(tok), None) is None:
            errors.append(
                f"STUDIES.md names `{tok}` but no such file is in the repo")

    bins = sorted(ROOT.rglob("*_64.dll")) + sorted(ROOT.rglob("*.Cht")) + \
        sorted(ROOT.rglob("*.cht")) + sorted(ROOT.rglob("*.StdyCollct"))
    for p in bins:
        if ".bak" in p.name or VENDOR in p.parents:
            continue
        short = p.name.split("_64")[0] if p.suffix == ".dll" else p.name
        if p.name not in studies_text and short not in studies_text:
            warnings.append(
                f"{rel(p)}: on-disk artifact not referenced in STUDIES.md §7/§8")

    readme = read_text(TOOLS / "README.md") or ""
    for cmd in cli_commands():
        if f"`{cmd}`" not in readme:
            errors.append(
                f"tools/README.md does not mention `{cmd}` — docs drifted "
                "from the CLI")

    status, head = git_hygiene()
    if status:
        shown = ", ".join(s.strip() for s in status[:6])
        more = f" (+{len(status) - 6} more)" if len(status) > 6 else ""
        infos.append(f"{len(status)} uncommitted path(s): {shown}{more}")
    if head and not COMMIT_RE.match(head):
        infos.append(
            f"HEAD subject {head!r} breaks the `<area>: <summary>` "
            "convention (see MAINTENANCE.md)")

    for e in errors:
        print(f"ERROR: {e}")
    for w in warnings:
        print(f"WARN:  {w}")
    for i in infos:
        print(f"INFO:  {i}")
    print(f"\nmaintain: {len(errors)} error(s), {len(warnings)} warning(s), "
          f"{len(infos)} note(s)")
    if errors:
        return 1
    return 0

def build_parser():
    p = argparse.ArgumentParser(description="SierraChart study tooling")
    sub = p.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("check", help="static review of native studies")
    c.add_argument("--strict", action="store_true",
                   help="warnings also fail the run")
    c.set_defaults(fn=cmd_check)

    n = sub.add_parser("new", help="scaffold a study from the template")
    n.add_argument("name", help='display name, e.g. "My Signal"')
    n.add_argument("--file", required=True, help="e.g. MySignal.cpp")
    n.add_argument("--study", required=True,
                   help="scsf_ name without prefix, e.g. MySignal")
    n.add_argument("--dir", default=".", help="subdir of repo root (default .)")
    n.add_argument("--force", action="store_true")
    n.add_argument("--register", action="store_true",
                   help="also add STUDIES.md row + index.html card (needs --desc)")
    n.add_argument("--desc", default="",
                   help="one-line description for --register")
    n.add_argument("--status", default="Source-only",
                   help="catalog status for --register (default Source-only)")
    n.set_defaults(fn=cmd_new)

    ins = sub.add_parser("install", help="deploy sources to the Sierra build folder")
    ins.add_argument("--file", action="append", default=[],
                     help="native source to deploy (repeatable; basename or path)")
    ins.add_argument("--all", action="store_true",
                     help="deploy every native source")
    ins.add_argument("--to",
                     help="destination dir (default $SC_ACS_SOURCE or Wine ACS_Source)")
    ins.add_argument("--dry-run", action="store_true",
                     help="list what would deploy without copying")
    ins.set_defaults(fn=cmd_install)

    cat = sub.add_parser("catalog", help="register a study in STUDIES.md + index.html")
    cat_sub = cat.add_subparsers(dest="action", required=True)
    add = cat_sub.add_parser("add", help="append STUDIES.md row(s) + index.html card")
    add.add_argument("--file", required=True, help="native source to register")
    add.add_argument("--desc", required=True, help="one-line description")
    add.add_argument("--status", default="Source-only")
    add.set_defaults(fn=cmd_catalog)

    s = sub.add_parser("sync", help="sync vendor copies from upstream checkouts")
    s.add_argument("--check", action="store_true",
                   help="report drift without copying")
    s.set_defaults(fn=cmd_sync)

    d = sub.add_parser("data", help="CSV dataset management")
    d.add_argument("action", choices=["validate", "list", "bars"])
    d.add_argument("files", nargs="*")
    d.set_defaults(fn=cmd_data)

    o = sub.add_parser("optimize", help="grid sweep + leaderboard via the backtester")
    o.add_argument("--data", required=True, help="bars/signal CSV")
    o.add_argument("--base", required=True, help="base params JSON (grid overrides it)")
    o.add_argument("--param", action="append", default=[],
                   help="grid axis NAME=v1,v2 (repeatable)")
    o.add_argument("--out", required=True, help="output dir (sweep.json + leaderboard)")
    o.add_argument("--tag", default="opt", help="sweep run tag prefix")
    o.add_argument("--dataset", default="",
                   help="compare filter (default: --data value)")
    o.add_argument("--study", default="",
                   help="compare filter to one strategy")
    o.add_argument("--dry-run", action="store_true",
                   help="write sweep.json only, run nothing")
    o.set_defaults(fn=cmd_optimize)

    cf = sub.add_parser("confirm", help="IS + OOS-split + walkforward confirmation")
    cf.add_argument("--data", required=True, help="bars/signal CSV")
    cf.add_argument("--params", required=True, help="winner params JSON")
    cf.add_argument("--out", required=True, help="output dir (is/ + oos/ + wf/)")
    cf.add_argument("--tag", default="confirm", help="run tag prefix")
    cf.add_argument("--split", default="frac:0.7",
                    help="OOS split: frac:0.7 or 'YYYY-MM-DD[ HH:MM]'")
    cf.add_argument("--train", default="", help="walkforward train window")
    cf.add_argument("--test", default="", help="walkforward test window")
    cf.add_argument("--step", default="", help="walkforward step")
    cf.add_argument("--embargo-days", default="",
                    help="walkforward embargo per test window")
    cf.add_argument("--skip-walkforward", action="store_true",
                    help="IS + split only, no walkforward")
    cf.set_defaults(fn=cmd_confirm)

    st = sub.add_parser("strategies", help="list backtester strategies + params files")
    st.set_defaults(fn=cmd_strategies)

    b = sub.add_parser("backtest",
                       help="run the upstream headless backtester (passthrough)")
    b.add_argument("rest", nargs=argparse.REMAINDER,
                   help="args forwarded to backtest/bt.py, e.g. run --data …")
    # allow `backtest --help` to reach bt.py
    b.set_defaults(fn=cmd_backtest)

    h = sub.add_parser("harness",
                       help="run a Backtest Harness job on the live Sierra chart")
    h.add_argument("--cmd", action="append", default=[],
                   help="harness command line, e.g. --cmd 'ADD MeanReversionOU_64.scsf_MeanReversionOU AS MROU' (repeatable)")
    h.add_argument("--timeout", type=int, default=60,
                   help="seconds to wait for the .result file (default 60)")
    h.add_argument("--capture", default="",
                   help="capture a routed TRIGGER study end-to-end (name in studies/harness-routing.json)")
    h.add_argument("--out", default="",
                   help="local CSV path under the Sierra Data dir (with --capture)")
    h.add_argument("--settle", type=int, default=25,
                   help="seconds between capture phases for registration/export (default 25)")
    h.set_defaults(fn=cmd_harness)

    bd = sub.add_parser("build", help="compile pipeline: deps, stage, local check, verify")
    bd_sub = bd.add_subparsers(dest="action", required=True)
    bd_sub.add_parser("plan", help="dep closure for every study (fails on missing headers)").set_defaults(fn=cmd_build)
    stg = bd_sub.add_parser("stage", help="copy studies + headers to the Sierra build folder")
    stg.add_argument("--to", help="destination dir (default $SC_ACS_SOURCE or Wine ACS_Source)")
    stg.add_argument("--dry-run", action="store_true", help="list what would stage without copying")
    stg.set_defaults(fn=cmd_build)
    loc = bd_sub.add_parser("local", help="local mingw syntax check over every study")
    loc.add_argument("--include", help="dir with sierrachart.h (default $SC_ACS_SOURCE or Wine ACS_Source)")
    loc.set_defaults(fn=cmd_build)
    dll = bd_sub.add_parser("dll", help="compile every study to Data DLLs with local mingw")
    dll.add_argument("--to", help="destination dir (default $SC_DATA_DIR or Data beside ACS_Source)")
    dll.add_argument("--force", action="store_true", help="rebuild even when the DLL is newer than its sources")
    dll.set_defaults(fn=cmd_build)
    ver = bd_sub.add_parser("verify", help="every study has a fresh DLL exporting its studies")
    ver.add_argument("--data-dir", help="Sierra Data dir (default $SC_DATA_DIR or beside ACS_Source)")
    ver.set_defaults(fn=cmd_build)
    m = sub.add_parser("maintain", help="repo housekeeping audit: strays, docs drift, git hygiene")
    m.add_argument("--check", action="store_true",
                   help="report findings without changing anything (default)")
    m.add_argument("--rm-junk", action="store_true",
                   help="delete browser _files/ dirs + [objectObject] accidents, then audit")
    m.set_defaults(fn=cmd_maintain)
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    if args.cmd == "backtest" and args.rest and args.rest[0] == "--":
        args.rest = args.rest[1:]
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
