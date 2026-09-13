#include "sierrachart.h"
#include <map>
#include <string>
#include <cstdio>
#include <cstring>
#include <ctime>

SCDLLName("Backtest Harness DLL")

// BacktestHarness: file-driven study deployer. A headless driver writes a
// job file; the harness executes it on its chart and writes a result file.
// After this ONE study is on a chart (bootstrap), no GUI is needed per
// study: add/configure/wire/recalculate/remove entirely from disk.
//
// Job protocol (one command per line, `#` comments + blanks ignored):
//   ADD <DllBase>.<scsf> AS <short>   e.g. ADD MeanReversionOU_64.scsf_MeanReversionOU AS MROU
//   RESOLVE <short>                   map a short name to an existing study ID
//   SETINT <short> <idx> <int>
//   SETFLOAT <short> <idx> <float>
//   SETSTRING <short> <idx> <text...>
//   WIRE <short> <idx> <srcShort> <subgraphIdx>
//   VERIFY <short> <idx>          read back a wired input (study,subgraph)
//   READINT/READFLOAT/READSTR <short> <idx>   read back a scalar input value
//   USECHART <n>                  retarget all verbs to chart n (0 = own chart)
//   SCAN                          list chart numbers with symbols (find NQ/ES/GC)
//   RECALC
//
// Inputs are append-only once shipped -- never insert or reorder.

namespace
{
int resolveId(SCStudyInterfaceRef sc, int chart, const std::string& name,
              std::map<std::string, int>& ids, SCString& err)
{
    auto it = ids.find(name);
    if (it != ids.end())
        return it->second;
    const int id = sc.GetStudyIDByName(chart, name.c_str(), 1);
    if (id == 0)
        err.Format("unknown study '%s'", name.c_str());
    else
        ids[name] = id;
    return id;
}

void resultLine(FILE* f, bool ok, const SCString& msg)
{
    fprintf(f, "%s %s\n", ok ? "OK" : "ERR", msg.GetChars());
}
} // namespace

SCSFExport scsf_BacktestHarness(SCStudyInterfaceRef sc)
{
    SCInputRef InEnabled = sc.Input[0];
    SCInputRef InJobFile = sc.Input[1];
    SCInputRef InPollSec = sc.Input[2];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Backtest Harness";
        sc.StudyDescription = "File-driven deployer: ADD/SET/WIRE/RECALC/REMOVE studies on this chart from a job file.";
        sc.GraphRegion = 0;
        sc.AutoLoop = 1;
        sc.FreeDLL = 0;
        sc.UpdateAlways = 1; // poll for job files without waiting for new bars

        InEnabled.Name = "Enabled";
        InEnabled.SetYesNo(1);

        InJobFile.Name = "Job file path";
        InJobFile.SetString("C:\\SierraChart\\Data\\bt_harness_job.txt");

        InPollSec.Name = "Poll interval (seconds)";
        InPollSec.SetInt(2);
        InPollSec.SetIntLimits(1, 60);
        sc.Subgraph[0].Name = "Poll Heartbeat";
        sc.Subgraph[0].DrawStyle = DRAWSTYLE_POINT;
        sc.Subgraph[0].LineWidth = 3;
        return;
    }

    if (!InEnabled.GetYesNo())
        return;
    sc.UpdateAlways = 1; // re-assert: SetDefaults-time flags may not stick
    int& lastPollSec = sc.GetPersistentInt(0);
    const int nowSec = static_cast<int>(time(nullptr));
    if (nowSec - lastPollSec < InPollSec.GetInt())
        return;
    lastPollSec = nowSec;
    const char* jobPath = InJobFile.GetString();
    FILE* jf = fopen(jobPath, "r");
    if (jf == nullptr)
        return;

    char line[2048];
    std::string content;
    while (fgets(line, sizeof(line), jf) != nullptr)
        content += line;
    fclose(jf);
    remove(jobPath); // claim: exactly-once processing
    if (content.empty())
        return;

    SCString resultPath;
    resultPath.Format("%s.result", jobPath);
    FILE* rf = fopen(resultPath.GetChars(), "w");
    if (rf == nullptr)
    {
        SCString msg;
        msg.Format("BacktestHarness: cannot open %s", resultPath.GetChars());
        sc.AddMessageToLog(msg.GetChars(), 1);
        return;
    }

    const int ownChart = sc.ChartNumber;
    int& chartOverride = sc.GetPersistentInt(1); // USECHART target (0 = own chart)
    const int chart = (chartOverride > 0) ? chartOverride : ownChart;
     // NOTE: every verb below uses `chart`, so USECHART retargets all of them.
    std::map<std::string, int> ids;
    int okCount = 0, errCount = 0;

    size_t pos = 0;
    while (pos < content.size())
    {
        size_t end = content.find('\n', pos);
        std::string raw = content.substr(pos, end == std::string::npos ? end : end - pos);
        pos = (end == std::string::npos) ? content.size() : end + 1;
        while (!raw.empty() && (raw.back() == '\r' || raw.back() == ' ' || raw.back() == '\t'))
            raw.pop_back();
        size_t start = raw.find_first_not_of(" \t");
        if (start == std::string::npos || raw[start] == '#')
            continue;
        raw = raw.substr(start);

        char verb[16] = {0};
        int used = 0;
        if (sscanf(raw.c_str(), "%15s%n", verb, &used) != 1)
            continue;
        const char* rest = raw.c_str() + used;
        SCString out;
        bool ok = false;

        if (strcmp(verb, "ADD") == 0)
        {
            char dllFn[128] = {0}, shortName[64] = {0}, as[8] = {0};
            if (sscanf(rest, "%127s %7s %63s", dllFn, as, shortName) == 3 && strcmp(as, "AS") == 0)
            {
                n_ACSIL::s_AddStudy add;
                add.ChartNumber = chart;
                add.StudyID = 0;
                add.CustomStudyFileAndFunctionName = dllFn;
                add.ShortName = shortName;
                const int rc = sc.AddStudyToChart(add);
                if (rc != 0)
                {
                    const int id = sc.GetStudyIDByName(chart, shortName, 1);
                    if (id != 0)
                        ids[shortName] = id;
                    out.Format("ADD %s accepted%s", shortName,
                               id != 0 ? "" : " (id pending: RESOLVE after recalc)");
                    ok = true;
                }
                else
                    out.Format("ADD %s failed (check DLL.scsf name)", dllFn);
            }
            else
                out = "ADD syntax: ADD <DllBase>.<scsf> AS <short>";
        }
        else if (strcmp(verb, "RESOLVE") == 0)
        {
            char shortName[64] = {0};
            if (sscanf(rest, "%63s", shortName) == 1)
            {
                ids.erase(shortName); // never trust the map: re-query live
                SCString err;
                const int id = resolveId(sc, chart, shortName, ids, err);
                if (id != 0)
                {
                    out.Format("RESOLVE %s id=%d", shortName, id);
                    ok = true;
                }
                else
                    out = err;
            }
            else
                out = "RESOLVE syntax: RESOLVE <short>";
        }
        else if (strcmp(verb, "SETINT") == 0 || strcmp(verb, "SETFLOAT") == 0)
        {
            char shortName[64] = {0};
            int idx = 0;
            double val = 0;
            if (sscanf(rest, "%63s %d %lf", shortName, &idx, &val) == 3)
            {
                SCString err;
                const int id = resolveId(sc, chart, shortName, ids, err);
                if (id != 0)
                {
                    if (strcmp(verb, "SETINT") == 0)
                        sc.SetChartStudyInputInt(chart, id, idx, static_cast<int>(val));
                    else
                        sc.SetChartStudyInputFloat(chart, id, idx, val);
                    out.Format("%s %s[%d] done", verb, shortName, idx);
                    ok = true;
                }
                else
                    out = err;
            }
            else
                out.Format("%s syntax: %s <short> <idx> <value>", verb, verb);
        }
        else if (strcmp(verb, "SETSTRING") == 0)
        {
            char shortName[64] = {0};
            int idx = 0, n = 0;
            if (sscanf(rest, "%63s %d %n", shortName, &idx, &n) >= 2)
            {
                SCString err;
                const int id = resolveId(sc, chart, shortName, ids, err);
                if (id != 0)
                {
                    const char* val = rest + n;
                    while (*val == ' ' || *val == '\t')
                        ++val;
                    sc.SetChartStudyInputString(chart, id, idx, val);
                    out.Format("SETSTRING %s[%d] done", shortName, idx);
                    ok = true;
                }
                else
                    out = err;
            }
            else
                out = "SETSTRING syntax: SETSTRING <short> <idx> <text>";
        }
        else if (strcmp(verb, "WIRE") == 0)
        {
            char shortName[64] = {0}, src[64] = {0};
            int idx = 0, sg = 0;
            if (sscanf(rest, "%63s %d %63s %d", shortName, &idx, src, &sg) == 4)
            {
                SCString err;
                const int id = resolveId(sc, chart, shortName, ids, err);
                const int srcId = (id != 0) ? resolveId(sc, chart, src, ids, err) : 0;
                if (id != 0 && srcId != 0)
                {
                    s_ChartStudySubgraphValues ref;
                    ref.ChartNumber = chart;
                    ref.StudyID = srcId;
                    ref.SubgraphIndex = sg;
                    sc.SetChartStudyInputChartStudySubgraphValues(chart, id, idx, ref);
                    out.Format("WIRE %s[%d] <- %s.sg%d done", shortName, idx, src, sg);
                    ok = true;
                }
                else
                    out = err;
            }
            else
                out = "WIRE syntax: WIRE <short> <idx> <srcShort> <subgraphIdx>";
        }
        else if (strcmp(verb, "WHOAMI") == 0)
        {
            SCString tsym = (chart != ownChart) ? sc.GetChartSymbol(chart) : sc.Symbol;
            out.Format("chart=%d symbol=%s bars=%d target=%d tsym=%s", ownChart,
                       sc.Symbol.GetChars(), sc.ArraySize, chart, tsym.GetChars());
            ok = true;
        }
        else if (strcmp(verb, "RECALC") == 0)
        {
            sc.RecalculateChart(chart);
            out = "RECALC requested";
            ok = true;
        }
        else if (strcmp(verb, "VERIFY") == 0)
        {
            char shortName[64] = {0};
            int idx = 0;
            if (sscanf(rest, "%63s %d", shortName, &idx) == 2)
            {
                SCString err;
                const int id = resolveId(sc, chart, shortName, ids, err);
                if (id != 0)
                {
                    s_ChartStudySubgraphValues ref{};
                    ref.ChartNumber = 0;
                    ref.StudyID = 0;
                    ref.SubgraphIndex = -1;
                    if (sc.GetChartStudyInputChartStudySubgraphValues(chart, id, idx, ref) == 0)
                    {
                        out.Format("VERIFY %s[%d]: read failed (bad input index?)", shortName, idx);
                    }
                    else
                    {
                        out.Format("VERIFY %s[%d] <- id=%d sg%d (chart %d)",
                                   shortName, idx, ref.StudyID, ref.SubgraphIndex,
                                   ref.ChartNumber);
                        ok = true;
                    }
                }
                else
                    out = err;
            }
            else
                out = "VERIFY syntax: VERIFY <short> <idx>";
        }
        else if (strcmp(verb, "REMOVE") == 0)
        {
            char shortName[64] = {0};
            if (sscanf(rest, "%63s", shortName) == 1)
            {
                SCString err;
                const int id = resolveId(sc, chart, shortName, ids, err);
                if (id != 0)
                {
                    sc.RemoveStudyFromChart(chart, id);
                    ids.erase(shortName);
                    out.Format("REMOVE %s done", shortName);
                    ok = true;
                }
                else
                    out = err;
            }
            else
                out = "REMOVE syntax: REMOVE <short>";
        }
        else if (strcmp(verb, "USECHART") == 0)
        {
            int n = -1;
            if (sscanf(rest, "%d", &n) == 1 && n >= 0 && n <= 256)
            {
                chartOverride = n; // takes effect on the NEXT job (chart is bound above)
                out.Format("USECHART target=%d (applies next job)", n);
                ok = true;
            }
            else
                out = "USECHART syntax: USECHART <chart#> (0 = own chart)";
        }
        else if (strcmp(verb, "SCAN") == 0)
        {
            int found = 0;
            SCString list;
            for (int n = 1; n <= 200; ++n)
            {
                SCString sym = sc.GetChartSymbol(n);
                if (sym.GetLength() > 0)
                {
                    SCString item;
                    item.Format(" [%d]=%s", n, sym.GetChars());
                    list += item;
                    ++found;
                }
            }
            out.Format("SCAN %d charts:%s", found, list.GetChars());
            ok = true;
        }
        else if (strcmp(verb, "READINT") == 0 || strcmp(verb, "READFLOAT") == 0 || strcmp(verb, "READSTR") == 0)
        {
            char shortName[64] = {0};
            int idx = 0;
            if (sscanf(rest, "%63s %d", shortName, &idx) == 2)
            {
                SCString err;
                const int id = resolveId(sc, chart, shortName, ids, err);
                if (id != 0)
                {
                    if (strcmp(verb, "READINT") == 0)
                    {
                        int v = 0;
                        if (sc.GetChartStudyInputInt(chart, id, idx, v)) { out.Format("READINT %s[%d] = %d", shortName, idx, v); ok = true; }
                        else out.Format("READINT %s[%d]: read failed", shortName, idx);
                    }
                    else if (strcmp(verb, "READFLOAT") == 0)
                    {
                        double v = 0.0;
                        if (sc.GetChartStudyInputFloat(chart, id, idx, v)) { out.Format("READFLOAT %s[%d] = %f", shortName, idx, v); ok = true; }
                        else out.Format("READFLOAT %s[%d]: read failed", shortName, idx);
                    }
                    else
                    {
                        SCString v;
                        if (sc.GetChartStudyInputString(chart, id, idx, v)) { out.Format("READSTR %s[%d] = %s", shortName, idx, v.GetChars()); ok = true; }
                        else out.Format("READSTR %s[%d]: read failed", shortName, idx);
                    }
                }
                else
                    out = err;
            }
            else
                out = "READ syntax: READINT|READFLOAT|READSTR <short> <idx>";
        }
        else
            out.Format("unknown verb '%s'", verb);

        resultLine(rf, ok, out);
        if (ok)
            ++okCount;
        else
            ++errCount;
    }

    fclose(rf);
    SCString done;
    done.Format("BacktestHarness: job done (%d ok, %d err) -> %s", okCount, errCount, resultPath.GetChars());
    sc.AddMessageToLog(done.GetChars(), errCount > 0 ? 1 : 0);
}
