"""Tests for tools/sc.py (stdlib only). Run: python3 -m unittest discover -s tools/tests -v"""

import contextlib
import io
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))
import sc  # noqa: E402


def write(p, text):
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding="utf-8")


CPP_A = '#include "sierrachart.h"\nSCDLLName("A")\nSCSFExport scsf_Alpha(SCStudyInterfaceRef sc) {}\n'
CPP_B = '#include "sierrachart.h"\nSCDLLName("B")\nSCSFExport scsf_Beta(SCStudyInterfaceRef sc) {}\n'


class CheckTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.orig = (sc.ROOT, sc.NATIVE_DIRS, sc.STUDIES_MD, sc.VENDOR)
        sc.ROOT = self.tmp
        sc.NATIVE_DIRS = [self.tmp]
        sc.STUDIES_MD = self.tmp / "STUDIES.md"
        sc.VENDOR = self.tmp / "studies" / "vendor"
        (self.tmp / "studies").mkdir()

    def tearDown(self):
        sc.ROOT, sc.NATIVE_DIRS, sc.STUDIES_MD, sc.VENDOR = self.orig

    def test_duplicate_export_is_error(self):
        write(self.tmp / "A.cpp", CPP_A)
        write(self.tmp / "B.cpp", CPP_A.replace("Alpha", "Alpha").replace('"A"', '"B"'))
        write(self.tmp / "STUDIES.md", "A.cpp\nB.cpp\n")
        rc = sc.cmd_check(type("A", (), {"strict": False})())
        self.assertEqual(rc, 1)

    def test_missing_dllname_is_error(self):
        write(self.tmp / "A.cpp", 'SCSFExport scsf_Alpha(SCStudyInterfaceRef sc) {}\n')
        write(self.tmp / "STUDIES.md", "A.cpp\n")
        rc = sc.cmd_check(type("A", (), {"strict": False})())
        self.assertEqual(rc, 1)

    def test_clean_tree_passes(self):
        write(self.tmp / "A.cpp", CPP_A)
        write(self.tmp / "B.cpp", CPP_B)
        write(self.tmp / "STUDIES.md", "A.cpp\nB.cpp\n")
        rc = sc.cmd_check(type("A", (), {"strict": False})())
        self.assertEqual(rc, 0)

    def test_global_warns_not_fails(self):
        write(self.tmp / "A.cpp", CPP_A + "std::map<int,int> g_SlotValues;\n")
        write(self.tmp / "STUDIES.md", "A.cpp\n")
        rc = sc.cmd_check(type("A", (), {"strict": False})())
        self.assertEqual(rc, 0)
        rc = sc.cmd_check(type("A", (), {"strict": True})())
        self.assertEqual(rc, 1)

    def test_webhook_escape_suppresses_warning(self):
        escaped = ('#include "sierrachart.h"\nSCDLLName("W1")\n'
                   'static std::string EscapeDiscordJson(const std::string& s) { return s; }\n'
                   'SCSFExport scsf_W1(SCStudyInterfaceRef sc) {}\n'
                   '// payload "{\\"content\\":\\""\n')
        raw = ('#include "sierrachart.h"\nSCDLLName("W2")\n'
               'SCSFExport scsf_W2(SCStudyInterfaceRef sc) {}\n'
               '// payload "{\\"content\\":\\""\n')
        write(self.tmp / "W1.cpp", escaped)
        write(self.tmp / "W2.cpp", raw)
        write(self.tmp / "STUDIES.md", "W1.cpp\nW2.cpp\n")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = sc.cmd_check(type("A", (), {"strict": False})())
        out = buf.getvalue()
        self.assertEqual(rc, 0)
        self.assertNotIn("W1.cpp: Discord JSON", out)
        self.assertIn("W2.cpp: Discord JSON", out)

    def test_commented_export_ignored(self):
        write(self.tmp / "A.cpp", CPP_A)
        write(self.tmp / "B.cpp", CPP_B + "/*\nSCSFExport scsf_Alpha(SCStudyInterfaceRef sc) {}\n*/\n")
        write(self.tmp / "STUDIES.md", "A.cpp\nB.cpp\n")
        rc = sc.cmd_check(type("A", (), {"strict": False})())
        self.assertEqual(rc, 0)


    def test_input_slot_dup_is_error(self):
        dup = ('#include "sierrachart.h"\nSCDLLName("D")\n'
               'SCSFExport scsf_Dup(SCStudyInterfaceRef sc) {\n'
               'SCInputRef Input_MaxLoss = sc.Input[8];\n'
               'SCInputRef Input_MaxProfit = sc.Input[8];\n}\n')
        write(self.tmp / "D.cpp", dup)
        write(self.tmp / "STUDIES.md", "D.cpp\n")
        rc = sc.cmd_check(type("A", (), {"strict": False})())
        self.assertEqual(rc, 1)

    def test_input_slot_same_name_rebind_ok(self):
        ok = ('#include "sierrachart.h"\nSCDLLName("O")\n'
              'SCSFExport scsf_Ok(SCStudyInterfaceRef sc) {\n'
              'SCInputRef Input_X = sc.Input[3];\n}\n'
              'SCSFExport scsf_Ok2(SCStudyInterfaceRef sc) {\n'
              'SCInputRef Input_X = sc.Input[3];\n}\n')
        write(self.tmp / "O.cpp", ok)
        write(self.tmp / "STUDIES.md", "O.cpp\n")
        rc = sc.cmd_check(type("A", (), {"strict": False})())
        self.assertEqual(rc, 0)

    def test_alert_collision_warns(self):
        a = CPP_A + 'void f(SCStudyInterfaceRef sc) { sc.AlertWithMessage(199, "x"); }\n'
        b = CPP_B + 'void g(SCStudyInterfaceRef sc) { sc.AlertWithMessage(199, "y"); }\n'
        write(self.tmp / "A.cpp", a)
        write(self.tmp / "B.cpp", b)
        write(self.tmp / "STUDIES.md", "A.cpp\nB.cpp\n")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = sc.cmd_check(type("A", (), {"strict": False})())
        self.assertEqual(rc, 0)
        self.assertIn("alert 199 shared by", buf.getvalue())
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = sc.cmd_check(type("A", (), {"strict": True})())
        self.assertEqual(rc, 1)

    def test_secret_in_chartbook_warns(self):
        write(self.tmp / "A.cpp", CPP_A)
        write(self.tmp / "STUDIES.md", "A.cpp\n")
        (self.tmp / "X.Cht").write_bytes(
            b"study\x00https://discord.com/api/webhooks/123/abc\x00tail")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = sc.cmd_check(type("A", (), {"strict": False})())
        self.assertEqual(rc, 0)
        self.assertIn("X.Cht", buf.getvalue())

    def test_alert_share_between_pick_one_variants_is_info(self):
        # scsf_Delta_Intensity is a KNOWN_COLLISIONS pair: never built
        # together, so a shared alert ID cannot cross-trigger.
        a = ('#include "sierrachart.h"\nSCDLLName("P1")\n'
             'SCSFExport scsf_Delta_Intensity(SCStudyInterfaceRef sc) {\n'
             ' sc.SetAlert(5, "x"); }\n')
        b = ('#include "sierrachart.h"\nSCDLLName("P2")\n'
             'SCSFExport scsf_Delta_Intensity(SCStudyInterfaceRef sc) {\n'
             ' sc.SetAlert(5, "y"); }\n')
        write(self.tmp / "P1.cpp", a)
        write(self.tmp / "P2.cpp", b)
        write(self.tmp / "STUDIES.md", "P1.cpp\nP2.cpp\n")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = sc.cmd_check(type("A", (), {"strict": False})())
        out = buf.getvalue()
        self.assertEqual(rc, 0)
        self.assertNotIn("WARN:  alert 5", out)
        self.assertIn("pick-one variants", out)

class BuildTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.orig = (sc.ROOT, sc.NATIVE_DIRS, sc.STUDIES_MD, sc.VENDOR)
        sc.ROOT = self.tmp
        sc.NATIVE_DIRS = [self.tmp]
        sc.STUDIES_MD = self.tmp / "STUDIES.md"
        sc.VENDOR = self.tmp / "studies" / "vendor"
        (self.tmp / "studies").mkdir()

    def tearDown(self):
        sc.ROOT, sc.NATIVE_DIRS, sc.STUDIES_MD, sc.VENDOR = self.orig

    def bargs(self, action, **kw):
        d = {"action": action, "to": None, "dry_run": False,
             "include": None, "data_dir": None, "force": False}
        d.update(kw)
        return type("A", (), d)()

    def test_plan_flags_missing_header(self):
        write(self.tmp / "A.cpp", CPP_A + '#include "nope/missing.h"\n')
        write(self.tmp / "STUDIES.md", "A.cpp\n")
        rc = sc.cmd_build(self.bargs("plan"))
        self.assertEqual(rc, 1)

    def test_plan_clean_passes(self):
        write(self.tmp / "A.cpp", CPP_A)
        write(self.tmp / "STUDIES.md", "A.cpp\n")
        rc = sc.cmd_build(self.bargs("plan"))
        self.assertEqual(rc, 0)

    def test_stage_layout(self):
        write(self.tmp / "S.cpp",
              CPP_A.replace("scsf_Alpha", "scsf_Sierra")
              + '#include "inc/h.h"\n')
        write(self.tmp / "inc" / "h.h", "// helper\n")
        write(self.tmp / "STUDIES.md", "S.cpp\n")
        dest = self.tmp / "acs"
        rc = sc.cmd_build(self.bargs("stage", to=str(dest)))
        self.assertEqual(rc, 0)
        self.assertTrue((dest / "S.cpp").exists())
        self.assertTrue((dest / "inc" / "h.h").exists())

    def test_stage_refuses_missing_header(self):
        write(self.tmp / "A.cpp", CPP_A + '#include "nope/missing.h"\n')
        write(self.tmp / "STUDIES.md", "A.cpp\n")
        dest = self.tmp / "acs"
        rc = sc.cmd_build(self.bargs("stage", to=str(dest)))
        self.assertEqual(rc, 1)
        self.assertFalse(dest.exists())

    def test_verify(self):
        write(self.tmp / "A.cpp", CPP_A)
        write(self.tmp / "STUDIES.md", "A.cpp\n")
        ddir = self.tmp / "data"
        ddir.mkdir()
        self.assertEqual(sc.cmd_build(self.bargs("verify", data_dir=str(ddir))), 1)
        dll = ddir / "A_64.dll"
        dll.write_bytes(b"\x00MZ-fake\x00scsf_Alpha\x00tail")
        src_mtime = (self.tmp / "A.cpp").stat().st_mtime
        os.utime(dll, (src_mtime + 10, src_mtime + 10))
        self.assertEqual(sc.cmd_build(self.bargs("verify", data_dir=str(ddir))), 0)
        os.utime(self.tmp / "A.cpp", (src_mtime + 99, src_mtime + 99))
        self.assertEqual(sc.cmd_build(self.bargs("verify", data_dir=str(ddir))), 1)

    @unittest.skipUnless(shutil.which("x86_64-w64-mingw32-g++"), "no local mingw")
    def test_dll_builds_and_verifies(self):
        write(self.tmp / "A.cpp", CPP_A)
        write(self.tmp / "STUDIES.md", "A.cpp\n")
        ddir = self.tmp / "data"
        self.assertEqual(sc.cmd_build(self.bargs("dll", to=str(ddir))), 0)
        self.assertTrue((ddir / "A_64.dll").exists())
        self.assertEqual(sc.cmd_build(self.bargs("verify", data_dir=str(ddir))), 0)
        self.assertEqual(sc.cmd_build(self.bargs("dll", to=str(ddir))), 0)
        self.assertEqual(sc.cmd_build(self.bargs("dll", to=str(ddir), force=True)), 0)


class NewTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.orig = (sc.ROOT, sc.NATIVE_DIRS, sc.TEMPLATE)
        sc.ROOT = self.tmp
        sc.NATIVE_DIRS = [self.tmp]

    def tearDown(self):
        sc.ROOT, sc.NATIVE_DIRS, sc.TEMPLATE = self.orig

    def args(self, **kw):
        d = {"name": "My Signal", "file": "MySignal.cpp", "study": "MySignal",
             "dir": ".", "force": False}
        d.update(kw)
        return type("A", (), d)()

    def test_scaffold_substitutes(self):
        rc = sc.cmd_new(self.args())
        self.assertEqual(rc, 0)
        text = (self.tmp / "MySignal.cpp").read_text()
        self.assertIn("scsf_MySignal", text)
        self.assertIn("My Signal", text)

    def test_refuses_duplicate_study(self):
        write(self.tmp / "Old.cpp", CPP_A.replace("Alpha", "MySignal"))
        rc = sc.cmd_new(self.args())
        self.assertNotEqual(rc, 0)

    def test_refuses_overwrite(self):
        write(self.tmp / "MySignal.cpp", "x")
        rc = sc.cmd_new(self.args())
        self.assertNotEqual(rc, 0)

    def test_rejects_bad_ident(self):
        rc = sc.cmd_new(self.args(study="has-dash"))
        self.assertEqual(rc, 2)


class DataTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def test_valid_csv(self):
        p = self.tmp / "s.csv"
        p.write_text("# c\n09:30,22\n0935,12\n", encoding="utf-8")
        rows, errs = sc.validate_slot_csv(p)
        self.assertEqual((rows, errs), (2, []))

    def test_bad_rows_reported(self):
        p = self.tmp / "s.csv"
        p.write_text("09:30,22\n25:00,1\n09:30,9\nnope\n", encoding="utf-8")
        rows, errs = sc.validate_slot_csv(p)
        self.assertEqual(rows, 1)
        self.assertEqual(len(errs), 3)


STUDIES_FIXTURE = """# t
## 1. Root
| File | Study | What | Status |
|---|---|---|---|
| `A.cpp` | `Alpha` / A | d | Source-only |
## 2. Edge
| File | Study | What |
|---|---|---|
| `B.cpp` | `Beta` / B | d |
## 3. Zed
| File | Study | What |
|---|---|---|
| `C.cpp` | `Gamma` / C | d |
"""

INDEX_FIXTURE = """<script>
const DATA=[
{g:"House studies — repo root",n:"x",items:[
["A.cpp","scsf_A","desc A.","Source-only"]]},
{g:"EdgeFul Indicators/",n:"y",items:[
["B.cpp","scsf_B","desc B.","Source-only"]]},
{g:"zbyte/",n:"z",items:[
["C.cpp","scsf_C","desc C.","Source-only"]]}];
</script>
"""

NOVA_CPP = ('#include "sierrachart.h"\nSCDLLName("Nova DLL")\n'
            'SCSFExport scsf_Nova(SCStudyInterfaceRef sc) {\n'
            '    if (sc.SetDefaults) { sc.GraphName = "Nova"; return; }\n}\n')


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.orig = (sc.ROOT, sc.NATIVE_DIRS)
        sc.ROOT = self.tmp
        sc.NATIVE_DIRS = [self.tmp]
        self.dest = self.tmp / "acs"
        self.dest.mkdir()

    def tearDown(self):
        sc.ROOT, sc.NATIVE_DIRS = self.orig

    def args(self, **kw):
        d = {"file": [], "all": False, "to": str(self.dest), "dry_run": False}
        d.update(kw)
        return type("A", (), d)()

    def test_deploys_file(self):
        write(self.tmp / "A.cpp", CPP_A)
        rc = sc.cmd_install(self.args(file=["A.cpp"]))
        self.assertEqual(rc, 0)
        self.assertTrue((self.dest / "A.cpp").exists())

    def test_dry_run_copies_nothing(self):
        write(self.tmp / "A.cpp", CPP_A)
        rc = sc.cmd_install(self.args(file=["A.cpp"], dry_run=True))
        self.assertEqual(rc, 0)
        self.assertFalse((self.dest / "A.cpp").exists())

    def test_missing_file_errors(self):
        rc = sc.cmd_install(self.args(file=["Nope.cpp"]))
        self.assertNotEqual(rc, 0)

    def test_to_creates_dir(self):
        write(self.tmp / "A.cpp", CPP_A)
        target = self.tmp / "fresh" / "acs"
        rc = sc.cmd_install(self.args(file=["A.cpp"], to=str(target)))
        self.assertEqual(rc, 0)
        self.assertTrue((target / "A.cpp").exists())

    def test_missing_default_dest_errors(self):
        write(self.tmp / "A.cpp", CPP_A)
        orig = sc.acs_source
        sc.acs_source = lambda: self.tmp / "no-such-dir"
        try:
            rc = sc.cmd_install(self.args(file=["A.cpp"], to=None))
        finally:
            sc.acs_source = orig
        self.assertNotEqual(rc, 0)


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.orig = (sc.ROOT, sc.NATIVE_DIRS, sc.STUDIES_MD)
        sc.ROOT = self.tmp
        sc.NATIVE_DIRS = [self.tmp]
        sc.STUDIES_MD = self.tmp / "STUDIES.md"
        write(sc.STUDIES_MD, STUDIES_FIXTURE)
        write(self.tmp / "studies" / "index.html", INDEX_FIXTURE)

    def tearDown(self):
        sc.ROOT, sc.NATIVE_DIRS, sc.STUDIES_MD = self.orig

    def test_add_registers(self):
        src = self.tmp / "Nova.cpp"
        write(src, NOVA_CPP)
        rc = sc.do_catalog_add(src, "Nova signals.", "Source-only")
        self.assertEqual(rc, 0)
        studies = sc.STUDIES_MD.read_text()
        self.assertIn("| `Nova.cpp` | `Nova` / Nova | Nova signals. | Source-only |",
                      studies)
        index = (self.tmp / "studies" / "index.html").read_text()
        self.assertIn('"Nova.cpp — Nova","scsf_Nova","Nova signals.","Source-only"',
                      index)

    def test_double_register_errors(self):
        src = self.tmp / "Nova.cpp"
        write(src, NOVA_CPP)
        self.assertEqual(sc.do_catalog_add(src, "Nova signals.", "Source-only"), 0)
        self.assertNotEqual(sc.do_catalog_add(src, "Nova signals.", "Source-only"), 0)

    def test_bad_status_errors(self):
        src = self.tmp / "Nova.cpp"
        write(src, NOVA_CPP)
        self.assertEqual(sc.do_catalog_add(src, "Nova signals.", "Bogus"), 2)


class OptimizeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def test_parse_grid_val(self):
        self.assertEqual(sc.parse_grid_val("8,12"), [8, 12])
        self.assertEqual(sc.parse_grid_val("a,true,1.5"), ["a", True, 1.5])

    def test_dry_run_writes_sweep(self):
        import json
        base = self.tmp / "base.json"
        base.write_text(json.dumps({"strategy": "signal_replay",
                                    "strategy_params": {},
                                    "engine": {"qty": 1}}))
        out = self.tmp / "opt"
        args = type("A", (), {"data": "x.csv", "base": str(base),
                              "param": ["stop_ticks=8,12", "target_ticks=16,24"],
                              "out": str(out), "tag": "t", "dataset": "",
                              "study": "", "dry_run": True})()
        rc = sc.cmd_optimize(args)
        self.assertEqual(rc, 0)
        sweep = json.loads((out / "sweep.json").read_text())
        self.assertEqual(sweep["grid"],
                         {"stop_ticks": [8, 12], "target_ticks": [16, 24]})
        self.assertEqual(sweep["engine"], {"qty": 1})

    def test_combo_cap(self):
        import json
        base = self.tmp / "base.json"
        base.write_text(json.dumps({"strategy": "signal_replay"}))
        big = ",".join(str(i) for i in range(201))
        args = type("A", (), {"data": "x.csv", "base": str(base),
                              "param": [f"stop_ticks={big}"],
                              "out": str(self.tmp / "opt"), "tag": "t",
                              "dataset": "", "study": "",
                              "dry_run": True})()
        self.assertNotEqual(sc.cmd_optimize(args), 0)

    def test_full_flow_calls_sweep_then_compare(self):
        import json
        stub = self.tmp / "stub_bt.py"
        log = self.tmp / "calls.log"
        stub.write_text(
            "import sys\n"
            f"open({str(log)!r}, 'a').write(' '.join(sys.argv[1:]) + chr(10))\n")
        stub.chmod(0o755)
        base = self.tmp / "base.json"
        base.write_text(json.dumps({"strategy": "signal_replay"}))
        out = self.tmp / "opt"
        args = type("A", (), {"data": "x.csv", "base": str(base),
                              "param": ["stop_ticks=8,12"],
                              "out": str(out), "tag": "t", "dataset": "",
                              "study": "", "dry_run": False})()
        orig_bt, orig_dir = sc.UPSTREAM_BT, sc.BT_DIR
        sc.UPSTREAM_BT, sc.BT_DIR = stub, self.tmp
        try:
            rc = sc.cmd_optimize(args)
        finally:
            sc.UPSTREAM_BT, sc.BT_DIR = orig_bt, orig_dir
        self.assertEqual(rc, 0)
        calls = log.read_text().splitlines()
        self.assertEqual(len(calls), 2)
        self.assertTrue(calls[0].startswith("sweep --data "))
        self.assertIn("--params ", calls[0])
        self.assertTrue(calls[1].startswith("compare --dataset "))
        self.assertIn("leaderboard.html", calls[1])


class BarsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def test_valid_bars(self):
        p = self.tmp / "b.csv"
        p.write_text("DateTime,Open,High,Low,Close,SignalLong\n"
                     "2026-08-03 09:30,100,101,99.5,100.5,1\n"
                     "2026-08-03 09:35,100.5,102,100,101.5,0\n")
        n, sig, first, last, errs = sc.validate_bars_csv(p)
        self.assertEqual(errs, [])
        self.assertEqual((n, sig), (2, 1))
        self.assertEqual((first, last),
                         ("2026-08-03 09:30", "2026-08-03 09:35"))

    def test_bad_range(self):
        p = self.tmp / "b.csv"
        p.write_text("DateTime,Open,High,Low,Close\n"
                     "2026-08-03 09:30,100,99,101,100\n")
        n, sig, first, last, errs = sc.validate_bars_csv(p)
        self.assertEqual(n, 0)
        self.assertEqual(len(errs), 1)

    def test_missing_cols(self):
        p = self.tmp / "b.csv"
        p.write_text("DateTime,Open,High,Low\n2026-08-03 09:30,100,101,99\n")
        n, sig, first, last, errs = sc.validate_bars_csv(p)
        self.assertEqual(n, 0)
        self.assertTrue(any("missing columns" in e for e in errs))

    def test_bad_time(self):
        p = self.tmp / "b.csv"
        p.write_text("DateTime,Open,High,Low,Close\nnot-a-time,100,101,99,100\n")
        n, sig, first, last, errs = sc.validate_bars_csv(p)
        self.assertEqual(n, 0)
        self.assertEqual(len(errs), 1)


class StrategiesTests(unittest.TestCase):
    @unittest.skipIf(not (sc.BT_DIR / "strategies.py").exists(),
                     "upstream backtester checkout missing")
    def test_lists_strategies(self):
        import io
        from contextlib import redirect_stdout
        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = sc.cmd_strategies(type("A", (), {})())
        self.assertEqual(rc, 0)
        self.assertIn("signal_replay", buf.getvalue())


class MaintainTests(unittest.TestCase):
    README_FIXTURE = (
        "`check` `new` `install` `catalog` `sync` `data` `optimize` `confirm` "
        "`strategies` `backtest` `harness` `build` `maintain`\n"
        "`catalog add`\n`build plan`\n`build stage`\n`build local`\n`build dll`\n"
        "`build verify`\n`data validate`\n`data list`\n`data bars`\n"
    )
    STUDIES_FIXTURE = (
        "# STUDIES.md — fixture\n\n"
        "## 7. Binary-only DLLs (no source in this repo — do not treat as buildable)\n\n"
        "`Foo_64.dll` is vendored.\n\n"
        "## 8. Chart presets (not studies, but wired to studies above)\n\n"
        "`Bar.Cht` and `Baz.StdyCollct`.\n\n"
        "## Alert ID registry (chart-global — never reuse an ID across studies)\n"
    )

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.orig = (sc.ROOT, sc.NATIVE_DIRS, sc.STUDIES_MD, sc.VENDOR, sc.TOOLS)
        sc.ROOT = self.tmp
        sc.NATIVE_DIRS = [self.tmp]
        sc.STUDIES_MD = self.tmp / "STUDIES.md"
        sc.VENDOR = self.tmp / "studies" / "vendor"
        sc.TOOLS = self.tmp / "tools"
        (self.tmp / "studies").mkdir()
        (self.tmp / "tools").mkdir()
        write(sc.STUDIES_MD, self.STUDIES_FIXTURE)
        write(sc.TOOLS / "README.md", self.README_FIXTURE)
        write(self.tmp / "Foo_64.dll", "dll")
        write(self.tmp / "Bar.Cht", "cht")
        write(self.tmp / "Baz.StdyCollct", "collct")

    def tearDown(self):
        sc.ROOT, sc.NATIVE_DIRS, sc.STUDIES_MD, sc.VENDOR, sc.TOOLS = self.orig

    def margs(self, **kw):
        d = {"check": False, "rm_junk": False}
        d.update(kw)
        return type("A", (), d)()

    def run_maintain(self, **kw):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = sc.cmd_maintain(self.margs(**kw))
        return rc, buf.getvalue()

    def test_clean_tree_passes(self):
        rc, out = self.run_maintain(check=True)
        self.assertEqual(rc, 0)
        self.assertIn("0 error(s), 0 warning(s)", out)
        self.assertNotIn("uncommitted", out)  # no .git in fixture

    def test_junk_flagged_then_removed(self):
        (self.tmp / "Page_files").mkdir()
        write(self.tmp / "Page_files" / "x.js", "junk")
        write(self.tmp / "dl [objectObject]", "junk")
        rc, out = self.run_maintain(check=True)
        self.assertEqual(rc, 0)
        self.assertIn("Page_files", out)
        self.assertIn("[objectObject]", out)
        rc, out = self.run_maintain(rm_junk=True)
        self.assertEqual(rc, 0)
        self.assertIn("REMOVED", out)
        self.assertFalse((self.tmp / "Page_files").exists())
        self.assertFalse((self.tmp / "dl [objectObject]").exists())
        rc, out = self.run_maintain(check=True)
        self.assertNotIn("Page_files", out)

    def test_backup_and_lowercase_preset_warn(self):
        write(self.tmp / "A.Cht.bak-1", "bak")
        write(self.tmp / "b.cht", "cht")
        rc, out = self.run_maintain(check=True)
        self.assertEqual(rc, 0)
        self.assertIn("A.Cht.bak-1", out)
        self.assertIn("b.cht", out)

    def test_empty_stub_warns(self):
        write(self.tmp / "notes.md", "\n")
        rc, out = self.run_maintain(check=True)
        self.assertEqual(rc, 0)
        self.assertIn("notes.md", out)

    def test_readme_drift_is_error(self):
        write(sc.TOOLS / "README.md",
              self.README_FIXTURE.replace("strategies", ""))
        rc, out = self.run_maintain(check=True)
        self.assertEqual(rc, 1)
        self.assertIn("ERROR: tools/README.md does not mention `strategies`", out)

    def test_missing_study_ref_is_error(self):
        write(sc.STUDIES_MD,
              self.STUDIES_FIXTURE.replace("`Bar.Cht`", "`Nope.Cht`"))
        rc, out = self.run_maintain(check=True)
        self.assertEqual(rc, 1)
        self.assertIn("`Nope.Cht`", out)

    def test_unmatched_glob_ref_is_error(self):
        write(sc.STUDIES_MD,
              self.STUDIES_FIXTURE.replace("`Bar.Cht`", "`Ghost_*.Cht`"))
        rc, out = self.run_maintain(check=True)
        self.assertEqual(rc, 1)
        self.assertIn("`Ghost_*.Cht`", out)

    def test_glob_ref_matches_file(self):
        write(sc.STUDIES_MD,
              self.STUDIES_FIXTURE.replace(
                  "`Bar.Cht` and `Baz.StdyCollct`",
                  "`Bar.Cht` and `Baz*.StdyCollct`"))
        rc, out = self.run_maintain(check=True)
        self.assertEqual(rc, 0)
        self.assertIn("0 error(s)", out)

    def test_unreferenced_dll_warns(self):
        write(self.tmp / "Extra_64.dll", "dll")
        rc, out = self.run_maintain(check=True)
        self.assertEqual(rc, 0)
        self.assertIn("Extra_64.dll", out)

class ConfirmTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.orig_bt = sc.bt_call
        self.calls = []
        def fake(*argv):
            self.calls.append(list(argv))
            return 0
        sc.bt_call = fake

    def tearDown(self):
        sc.bt_call = self.orig_bt

    def cargs(self, **kw):
        d = {"data": "d.csv", "params": "p.json", "out": str(self.tmp / "c"),
             "tag": "t", "split": "frac:0.7", "train": "", "test": "",
             "step": "", "embargo_days": "", "skip_walkforward": False}
        d.update(kw)
        return type("A", (), d)()

    def test_sequence_is_run_split_walkforward(self):
        rc = sc.cmd_confirm(self.cargs())
        self.assertEqual(rc, 0)
        self.assertEqual([c[0] for c in self.calls],
                         ["run", "run", "walkforward"])
        self.assertNotIn("--split", self.calls[0])
        self.assertIn("--split", self.calls[1])
        self.assertEqual(self.calls[1][self.calls[1].index("--split") + 1],
                         "frac:0.7")

    def test_fail_fast_stops_sequence(self):
        calls = self.calls
        def bad(*argv):
            calls.append(list(argv))
            return 1 if len(calls) == 2 else 0
        sc.bt_call = bad
        rc = sc.cmd_confirm(self.cargs())
        self.assertNotEqual(rc, 0)
        self.assertEqual(len(calls), 2)

    def test_skip_walkforward_runs_split_only(self):
        rc = sc.cmd_confirm(self.cargs(skip_walkforward=True))
        self.assertEqual(rc, 0)
        self.assertEqual([c[0] for c in self.calls], ["run", "run"])

    def test_missing_upstream_fails_without_calling(self):
        orig = sc.UPSTREAM_BT
        sc.UPSTREAM_BT = self.tmp / "nope-bt.py"
        try:
            rc = sc.cmd_confirm(self.cargs())
        finally:
            sc.UPSTREAM_BT = orig
        self.assertNotEqual(rc, 0)
        self.assertEqual(self.calls, [])


class ProfilesTests(unittest.TestCase):
    NAMES = ("risk-conservative", "risk-balanced", "risk-growth")

    def test_profiles_valid_graded_and_halted(self):
        import json
        profs = {}
        for name in self.NAMES:
            p = sc.TOOLS / "profiles" / (name + ".json")
            profs[name] = json.loads(p.read_text(encoding="utf-8"))
        risks = [profs[n]["engine"]["risk_pct"] for n in self.NAMES]
        self.assertEqual(risks, [1.0, 2.0, 3.0])
        for prof in profs.values():
            eng = prof["engine"]
            self.assertGreater(eng["daily_loss_limit"], 0)
            self.assertGreater(eng["max_drawdown_limit"], 0)
            self.assertGreater(eng["max_qty"], 0)
            self.assertIn(eng["regime"], ("mean-reversion", "trend", "breakout"))
            self.assertLess(eng["daily_loss_limit"], eng["max_drawdown_limit"])
            self.assertLess(eng["max_drawdown_limit"], eng["profit_target"])
        dailies = [profs[n]["engine"]["daily_loss_limit"] for n in self.NAMES]
        draws = [profs[n]["engine"]["max_drawdown_limit"] for n in self.NAMES]
        targets = [profs[n]["engine"]["profit_target"] for n in self.NAMES]
        qtys = [profs[n]["engine"]["max_qty"] for n in self.NAMES]
        self.assertEqual([dailies, draws, targets, qtys],
                         [sorted(dailies), sorted(draws), sorted(targets), sorted(qtys)])

class HarnessTests(unittest.TestCase):
    def test_build_joins_and_skips_comments(self):
        body = sc.build_harness_job(["# setup", "", "ADD Foo_64.scsf_Foo AS F",
                                     "SETINT F 3 1", "RECALC"])
        self.assertEqual(body, "ADD Foo_64.scsf_Foo AS F\nSETINT F 3 1\nRECALC\n")

    def test_build_rejects_unknown_verb(self):
        with self.assertRaises(ValueError):
            sc.build_harness_job(["LAUNCH Foo"])

    def test_build_rejects_empty(self):
        with self.assertRaises(ValueError):
            sc.build_harness_job(["# nothing here"])

    def test_result_ok_needs_all_ok(self):
        self.assertTrue(sc.harness_result_ok("OK ADD F id=7\nOK RECALC requested\n"))
        self.assertFalse(sc.harness_result_ok("OK ADD F id=7\nERR WIRE bad\n"))
        self.assertFalse(sc.harness_result_ok(""))

    def test_compose_capture_probe_and_wire(self):
        entry = {"dll": "Foo_64", "scsf": "scsf_Foo", "longSg": 0, "shortSg": 1}
        probe, wire = sc.compose_capture(entry, "MeanReversionOU",
                                         "C:\\SierraChart\\Data\\x.csv")
        self.assertIn("RESOLVE MeanReversionOU", probe)
        self.assertIn("RESOLVE BTE", probe)
        self.assertNotIn("ADD Foo_64.scsf_Foo AS MeanReversionOU", probe)
        self.assertIn("WIRE BTE 1 MeanReversionOU 0", wire)
        self.assertIn("WIRE BTE 2 MeanReversionOU 1", wire)
        self.assertIn("VERIFY BTE 1", wire)
        self.assertIn("VERIFY BTE 2", wire)
        self.assertEqual(wire[-1], "RECALC")

    def test_parse_resolve_ids(self):
        text = ("OK RESOLVE MeanReversionOU id=3\n"
                "ERR unknown study 'BTE'\n"
                "OK chart=13 symbol=YMU26-CBOT bars=2046\n")
        self.assertEqual(sc.parse_resolve_ids(text), {"MeanReversionOU": 3})

    def test_check_wire_accepts_match(self):
        text = ("OK RESOLVE MeanReversionOU id=3\n"
                "OK WIRE BTE[1] <- MeanReversionOU.sg0 done\n"
                "OK VERIFY BTE[1] <- id=3 sg0 (chart 0)\n"
                "OK VERIFY BTE[2] <- id=3 sg1 (chart 0)\n")
        self.assertIsNone(sc.check_wire(text, "MeanReversionOU", 0, 1))

    def test_check_wire_refuses_wrong_study(self):
        text = ("OK RESOLVE MeanReversionOU id=3\n"
                "OK VERIFY BTE[1] <- id=6 sg0 (chart 0)\n"
                "OK VERIFY BTE[2] <- id=6 sg1 (chart 0)\n")
        err = sc.check_wire(text, "MeanReversionOU", 0, 1)
        self.assertIn("wiring mismatch", err)

    def test_check_wire_refuses_missing_resolve(self):
        text = "OK VERIFY BTE[1] <- id=6 sg0 (chart 0)\n"
        self.assertIn("did not resolve",
                      sc.check_wire(text, "MeanReversionOU", 0, 1))

    def test_winpath_rejects_outside_data(self):
        with self.assertRaises(ValueError):
            sc.harness_winpath("/tmp/elsewhere.csv")


if __name__ == "__main__":
    unittest.main()
