#include "sierrachart.h"

SCDLLName("GoldenCrossRegime")

// GoldenCrossRegime — long-only SMA golden-cross trend system with an
// index regime filter. Port of the StrategyQuant "AlgoCloud Stockpicker"
// long rule, hardened per public moving-average-crossover evidence:
//   Entry (flat, closed bar): SMAfast crossed above SMAslow on this bar
//     (fast[prev] <= slow[prev] && fast[cur] > slow[cur]), close <
//     MaxExtension * slow (don't chase; SQ uses 1.05), and the index leg
//     closes above its own slow SMA (bull regime; 200DMA timing cuts
//     drawdown but whipsaws — the extension guard and fresh-cross-only
//     rule are the whipsaw offsets).
//   Exit (holding, closed bar): death cross (fast crosses below slow).
// Position Score = 1 - ROC(RocLen): SQ ranks candidates weakest-first;
// exposed on a hidden subgraph so multi-symbol ranking can be replicated
// offline (the single-position backtester cannot rank; it takes the series
// as given). Signal-only: no order calls.
// Assessment recipe (EXACT): Buy->SignalLong, Sell->SignalShort, stops /
// targets / max_hold OFF, allow_long=true, allow_short=true,
// reverse_on_opposite=false. With no stop-outs the study and engine
// positions agree inductively (same entries, death cross the only exit),
// so no flat-state Sell can open a short. Add stops in a second run to
// price risk control. Regime: trend.
namespace
{
inline double SMAOver(SCFloatArrayRef Data, int End, int Length)
{
    double sum = 0.0;
    for (int k = End - Length + 1; k <= End; ++k)
        sum += (double)Data[k];
    return sum / (double)Length;
}

// ROC in percent over Length bars ending at End: needs End >= Length.
inline double ROCPctOver(SCFloatArrayRef Data, int End, int Length)
{
    const double prev = (double)Data[End - Length];
    if (prev == 0.0)
        return 0.0;
    return ((double)Data[End] / prev - 1.0) * 100.0;
}
}  // namespace

SCSFExport scsf_GoldenCrossRegime(SCStudyInterfaceRef sc)
{
    SCInputRef InEnabled = sc.Input[0];
    SCInputRef InFastLen = sc.Input[1];
    SCInputRef InSlowLen = sc.Input[2];
    SCInputRef InMaxExtension = sc.Input[3];
    SCInputRef InIndexChart = sc.Input[4];
    SCInputRef InIndexSlowLen = sc.Input[5];
    SCInputRef InRocLen = sc.Input[6];

    SCSubgraphRef SgBuy = sc.Subgraph[0];
    SCSubgraphRef SgSell = sc.Subgraph[1];
    SCSubgraphRef SgFast = sc.Subgraph[2];
    SCSubgraphRef SgSlow = sc.Subgraph[3];
    SCSubgraphRef SgScore = sc.Subgraph[4];
    SCSubgraphRef SgHalt = sc.Subgraph[5];

    int& Pos = sc.GetPersistentInt(0);
    int& EntryBar = sc.GetPersistentInt(1);

    if (sc.SetDefaults)
    {
        sc.GraphName = "Golden Cross Regime";
        sc.StudyDescription = "Long-only SMA golden-cross entries (fresh cross + extension guard + index bull-regime filter), death-cross exits, 1-ROC position score. Signal-only.";
        sc.GraphRegion = 0;
        sc.AutoLoop = 1;
        sc.FreeDLL = 0;

        InEnabled.Name = "Enabled";
        InEnabled.SetYesNo(1);

        InFastLen.Name = "Fast SMA Bars";
        InFastLen.SetInt(50);
        InFastLen.SetIntLimits(2, 500);

        InSlowLen.Name = "Slow SMA Bars";
        InSlowLen.SetInt(200);
        InSlowLen.SetIntLimits(3, 500);

        InMaxExtension.Name = "Max Extension vs Slow SMA (close < X * slow)";
        InMaxExtension.SetFloat(1.05f);
        InMaxExtension.SetFloatLimits(1.0f, 2.0f);

        InIndexChart.Name = "Index Chart Number (0 = disabled/halt)";
        InIndexChart.SetChartNumber(0);

        InIndexSlowLen.Name = "Index Slow SMA Bars";
        InIndexSlowLen.SetInt(200);
        InIndexSlowLen.SetIntLimits(3, 500);

        InRocLen.Name = "Position Score ROC Bars";
        InRocLen.SetInt(20);
        InRocLen.SetIntLimits(2, 200);

        SgBuy.Name = "Buy";
        SgBuy.DrawStyle = DRAWSTYLE_POINT_ON_LOW;
        SgBuy.PrimaryColor = RGB(0, 255, 0);
        SgBuy.LineWidth = 5;
        SgBuy.DrawZeros = false;

        SgSell.Name = "Sell (exit)";
        SgSell.DrawStyle = DRAWSTYLE_POINT_ON_HIGH;
        SgSell.PrimaryColor = RGB(255, 128, 0);
        SgSell.LineWidth = 5;
        SgSell.DrawZeros = false;

        SgFast.Name = "Fast SMA";
        SgFast.DrawStyle = DRAWSTYLE_LINE;
        SgFast.PrimaryColor = RGB(0, 255, 0);
        SgFast.LineWidth = 1;
        SgFast.DrawZeros = false;

        SgSlow.Name = "Slow SMA";
        SgSlow.DrawStyle = DRAWSTYLE_LINE;
        SgSlow.PrimaryColor = RGB(255, 0, 0);
        SgSlow.LineWidth = 2;
        SgSlow.DrawZeros = false;

        SgScore.Name = "Position Score 1-ROC (hidden, for ranking)";
        SgScore.DrawStyle = DRAWSTYLE_IGNORE;
        SgScore.DrawZeros = false;

        SgHalt.Name = "Halt Flag (1 = entries halted)";
        SgHalt.DrawStyle = DRAWSTYLE_IGNORE;
        SgHalt.DrawZeros = false;

        Pos = 0;
        EntryBar = -1;
        return;
    }

    if (!InEnabled.GetYesNo())
        return;

    const int i = sc.Index;
    SgBuy[i] = 0.0f;
    SgSell[i] = 0.0f;
    if (i == 0)
    {
        // Full recalculations replay from bar 0 with stale persistents;
        // restart the state machine so replay matches a fresh compute.
        Pos = 0;
        EntryBar = -1;
    }

    const int fastLen = InFastLen.GetInt();
    const int slowLen = InSlowLen.GetInt();
    const int idxLen = InIndexSlowLen.GetInt();
    const int need = slowLen > fastLen ? slowLen : fastLen;
    // Cross detection needs prior-bar SMAs: first computable bar is i == need.
    if (i < need)
    {
        SgHalt[i] = 1.0f;
        return;
    }
    if (sc.GetBarHasClosedStatus() != BHCS_BAR_HAS_CLOSED)
        return;

    const int idxChart = InIndexChart.GetChartNumber();
    if (idxChart <= 0 || idxChart == sc.ChartNumber)
    {
        SgHalt[i] = 1.0f;
        if (i == sc.ArraySize - 1)
        {
            SCString msg;
            msg.Format("GoldenCrossRegime: set Index Chart Number to the index chart (got %d).", idxChart);
            sc.AddMessageToLog(msg, 1);
        }
        return;
    }

    // Aligned index-close cache over [i-idxLen+1, i]; any mapping miss
    // stalls this bar (stale = halt, never a signal on partial data).
    // Stack cache: IndexSlowLen caps at 500 in SetDefaults, so this fits.
    SCGraphData idxData;
    sc.GetChartBaseData(idxChart, idxData);
    const int cStart = i - idxLen + 1;
    double idxc[500];
    for (int k = cStart; k <= i; ++k)
    {
        const int j = sc.GetContainingIndexForDateTimeIndex(idxChart, k);
        if (j < 0 || j >= idxData.GetArraySize())
        {
            SgHalt[i] = 1.0f;
            return;
        }
        idxc[k - cStart] = (double)idxData[SC_CLOSE][j];
    }
    double idxSum = 0.0;
    for (int k = 0; k < idxLen; ++k)
        idxSum += idxc[k];
    const double idxSMA = idxSum / (double)idxLen;
    const bool regimeOk = idxc[idxLen - 1] > idxSMA;
    SgHalt[i] = regimeOk ? 0.0f : 1.0f;

    const double fastCur = SMAOver(sc.Close, i, fastLen);
    const double fastPrev = SMAOver(sc.Close, i - 1, fastLen);
    const double slowCur = SMAOver(sc.Close, i, slowLen);
    const double slowPrev = SMAOver(sc.Close, i - 1, slowLen);

    SgFast[i] = (float)fastCur;
    SgSlow[i] = (float)slowCur;

    const int rocLen = InRocLen.GetInt();
    SgScore[i] = (i >= rocLen) ? (float)(1.0 - ROCPctOver(sc.Close, i, rocLen)) : 0.0f;

    const bool golden = fastPrev <= slowPrev && fastCur > slowCur;
    const bool death = fastPrev >= slowPrev && fastCur < slowCur;

    if (Pos != 0)
    {
        if (death)
        {
            Pos = 0;
            EntryBar = -1;
            SgSell[i] = sc.High[i] + sc.TickSize;
            if (sc.IsNewBar(i))
                sc.AlertWithMessage(202, "GoldenCrossRegime EXIT");
        }
        return;
    }

    if (!regimeOk)
        return;

    const double maxExt = (double)InMaxExtension.GetFloat();
    if (golden && (double)sc.Close[i] < maxExt * slowCur)
    {
        Pos = 1;
        EntryBar = i;
        SgBuy[i] = sc.Low[i] - sc.TickSize;
        if (sc.IsNewBar(i))
            sc.AlertWithMessage(201, "GoldenCrossRegime BUY");
    }
}
