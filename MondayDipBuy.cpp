#include "sierrachart.h"

SCDLLName("MondayDipBuy")

// MondayDipBuy — long-only Monday dip-buy mean-reversion system. Port of
// the StrategyQuant "AlgoWizard" long rule, kept faithful to its logic:
//   Entry (flat, closed bar): bar day is Monday, close < DipFactor * open
//     (SQ uses 0.97: a -3% intraday washout), close above the prior-bar
//     slow SMA (long trend intact), and the slow SMA itself rising
//     (sma[cur] > sma[prev]).
//   Exit (holding, closed bar): close > close 2 bars ago (oversold bounce
//     realized) or bars-held >= ExitBars (SQ uses 10: time stop).
// Position Score = 1 - ROC(RocLen) evaluated one bar back (SQ scores on
// ROC[1]): ranks candidates deepest-dip-first for multi-symbol use; hidden
// subgraph for offline replication. Rationale: meta-analytic Monday/weekend
// weakness makes Monday the highest-base-rate dip day; the SMA200 gate
// keeps dips within uptrends (buys weakness, not downtrends). Signal-only:
// no order calls.
// NOTE: the exit branch is skipped on the entry bar, so a Monday washout
// that also closes above close[2] opens a position SQ's every-tick rule
// might close the same bar. Rare, defensible, noted for SQ re-export diffs.
// Assessment recipe (EXACT): Buy->SignalLong, Sell->SignalShort, stops /
// targets / max_hold OFF (the 10-bar timeout arrives as a Sell signal),
// allow_long=true, allow_short=true (load-bearing for exits),
// exit_on_opposite=true, reverse_on_opposite=false. Regime: mean-reversion.

namespace
{
// SMA of Data over [End-Length+1, End]. Caller guarantees End >= Length-1.
inline double DipSMA(SCFloatArrayRef Data, int End, int Length)
{
    double sum = 0.0;
    for (int k = End - Length + 1; k <= End; ++k)
        sum += (double)Data[k];
    return sum / (double)Length;
}

// ROC in percent over Length bars ending at End: needs End >= Length.
inline double DipROCPct(SCFloatArrayRef Data, int End, int Length)
{
    const double prev = (double)Data[End - Length];
    if (prev == 0.0)
        return 0.0;
    return ((double)Data[End] / prev - 1.0) * 100.0;
}
}  // namespace

SCSFExport scsf_MondayDipBuy(SCStudyInterfaceRef sc)
{
    SCInputRef InEnabled = sc.Input[0];
    SCInputRef InTrendLen = sc.Input[1];
    SCInputRef InDipFactor = sc.Input[2];
    SCInputRef InExitBars = sc.Input[3];
    SCInputRef InRocLen = sc.Input[4];

    SCSubgraphRef SgBuy = sc.Subgraph[0];
    SCSubgraphRef SgSell = sc.Subgraph[1];
    SCSubgraphRef SgTrend = sc.Subgraph[2];
    SCSubgraphRef SgScore = sc.Subgraph[3];
    SCSubgraphRef SgHalt = sc.Subgraph[4];

    int& Pos = sc.GetPersistentInt(0);
    int& EntryBar = sc.GetPersistentInt(1);

    if (sc.SetDefaults)
    {
        sc.GraphName = "Monday Dip Buy";
        sc.StudyDescription = "Long-only Monday dip-buy: -3% washout day above a rising slow SMA, bounce-or-time exits, 1-ROC position score. Signal-only.";
        sc.GraphRegion = 0;
        sc.AutoLoop = 1;
        sc.FreeDLL = 0;

        InEnabled.Name = "Enabled";
        InEnabled.SetYesNo(1);

        InTrendLen.Name = "Trend SMA Bars";
        InTrendLen.SetInt(200);
        InTrendLen.SetIntLimits(10, 500);

        InDipFactor.Name = "Dip Factor (close < X * open)";
        InDipFactor.SetFloat(0.97f);
        InDipFactor.SetFloatLimits(0.5f, 1.0f);

        InExitBars.Name = "Exit After Bars (time stop)";
        InExitBars.SetInt(10);
        InExitBars.SetIntLimits(1, 500);

        InRocLen.Name = "Position Score ROC Bars";
        InRocLen.SetInt(5);
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

        SgTrend.Name = "Trend SMA";
        SgTrend.DrawStyle = DRAWSTYLE_LINE;
        SgTrend.PrimaryColor = RGB(200, 200, 200);
        SgTrend.LineWidth = 1;
        SgTrend.DrawZeros = false;

        SgScore.Name = "Position Score 1-ROC (hidden, for ranking)";
        SgScore.DrawStyle = DRAWSTYLE_IGNORE;
        SgScore.DrawZeros = false;

        SgHalt.Name = "Halt Flag (1 = warmup)";
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

    const int trendLen = InTrendLen.GetInt();
    // Trend SMA at the prior bar needs i >= trendLen; the bounce exit
    // needs i >= 2; the back-dated score needs i >= rocLen + 1.
    const int rocLen = InRocLen.GetInt();
    int need = trendLen;
    if (rocLen + 1 > need)
        need = rocLen + 1;
    if (i < need)
    {
        SgHalt[i] = 1.0f;
        SgScore[i] = 0.0f;
        return;
    }
    if (sc.GetBarHasClosedStatus() != BHCS_BAR_HAS_CLOSED)
    {
        SgTrend[i] = 0.0f;
        SgScore[i] = 0.0f;
        SgHalt[i] = 1.0f;
        return;
    }
    SgHalt[i] = 0.0f;

    const double smaCur = DipSMA(sc.Close, i, trendLen);
    const double smaPrev = DipSMA(sc.Close, i - 1, trendLen);
    SgTrend[i] = (float)smaCur;
    SgScore[i] = (float)(1.0 - DipROCPct(sc.Close, i - 1, rocLen));

    if (Pos != 0)
    {
        const bool bounced = i >= 2 && (double)sc.Close[i] > (double)sc.Close[i - 2];
        const bool timedOut = (i - EntryBar) >= InExitBars.GetInt();
        if (bounced || timedOut)
        {
            Pos = 0;
            EntryBar = -1;
            SgSell[i] = sc.High[i] + sc.TickSize;
            if (sc.IsNewBar(i))
                sc.AlertWithMessage(204, "MondayDipBuy EXIT");
        }
        return;
    }

    const bool isMonday = sc.BaseDateTimeIn[i].GetDayOfWeek() == MONDAY;
    const double dipFactor = (double)InDipFactor.GetFloat();
    if (isMonday
        && (double)sc.Close[i] < dipFactor * (double)sc.Open[i]
        && (double)sc.Close[i] > smaPrev
        && smaCur > smaPrev)
    {
        Pos = 1;
        EntryBar = i;
        SgBuy[i] = sc.Low[i] - sc.TickSize;
        if (sc.IsNewBar(i))
            sc.AlertWithMessage(203, "MondayDipBuy BUY");
    }
}
