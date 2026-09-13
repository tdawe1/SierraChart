#include "sierrachart.h"
#include <cmath>

SCDLLName("StatArbPairs")

// StatArbPairs — two-leg cointegration-style spread mean reversion.
//
// Public-baseline design (Engle-Granger two-step / Johansen-lite practice):
//   1. Hedge ratio beta from rolling OLS of leg1 on leg2 over HedgeLookback.
//   2. Spread s = leg1 - beta * leg2; z-scored over ZLookback.
//   3. Enter the spread (long = long leg1 / short leg2) at |z| >= EntryZ,
//      exit at |z| <= ExitZ, opposite-signal flip, or half-life time-stop.
// Health gates (what separates this from a raw price-difference chart):
//   - rolling correlation >= MinCorrelation (cointegration-break proxy);
//   - spread half-life from an AR(1) fit inside [HLMin, HLMax];
//   - spread-stdev floor in leg1 ticks (dead-spread filter).
// Data: leg1 is this chart's close. Leg2 is another chart's base-data close,
// mapped bar-by-bar with GetContainingIndexForDateTimeIndex (same bar period
// and session on both charts required). Set Leg2 Chart Number to 0 to disable.
// GraphRegion 1: this study draws the z-oscillator, not prices. Signal-only:
// no order calls. Exits need both legs; size leg2 qty from the Beta subgraph.

namespace
{
const double kLn2 = 0.6931471805599453;

inline bool InSession(int t, int start, int end, bool enabled)
{
    if (!enabled)
        return true;
    if (start == end)
        return true;
    if (start < end)
        return t >= start && t <= end;
    return t >= start || t <= end;
}
}  // namespace

SCSFExport scsf_StatArbPairs(SCStudyInterfaceRef sc)
{
    SCInputRef InEnabled = sc.Input[0];
    SCInputRef InLeg2Chart = sc.Input[1];
    SCInputRef InHedgeLookback = sc.Input[2];
    SCInputRef InZLookback = sc.Input[3];
    SCInputRef InEntryZ = sc.Input[4];
    SCInputRef InExitZ = sc.Input[5];
    SCInputRef InMinCorr = sc.Input[6];
    SCInputRef InMinStdevTicks = sc.Input[7];
    SCInputRef InHLMin = sc.Input[8];
    SCInputRef InHLMax = sc.Input[9];
    SCInputRef InHLMult = sc.Input[10];
    SCInputRef InMaxHoldBars = sc.Input[11];
    SCInputRef InUseSession = sc.Input[12];
    SCInputRef InSessionStart = sc.Input[13];
    SCInputRef InSessionEnd = sc.Input[14];
    SCInputRef InAllowLong = sc.Input[15];
    SCInputRef InAllowShort = sc.Input[16];

    SCSubgraphRef SgBuy = sc.Subgraph[0];
    SCSubgraphRef SgSell = sc.Subgraph[1];
    SCSubgraphRef SgZ = sc.Subgraph[2];
    SCSubgraphRef SgUpper = sc.Subgraph[3];
    SCSubgraphRef SgLower = sc.Subgraph[4];
    SCSubgraphRef SgBeta = sc.Subgraph[5];
    SCSubgraphRef SgHalt = sc.Subgraph[6];

    int& Pos = sc.GetPersistentInt(0);
    int& EntryBar = sc.GetPersistentInt(1);
    int& HoldLimit = sc.GetPersistentInt(2);

    if (sc.SetDefaults)
    {
        sc.GraphName = "Stat Arb Pairs";
        sc.StudyDescription = "Pairs spread mean reversion: rolling OLS hedge ratio, z-scored spread, correlation + half-life health gates. Leg2 from another chart. Signal-only.";
        sc.GraphRegion = 1;
        sc.AutoLoop = 1;
        sc.FreeDLL = 0;

        InEnabled.Name = "Enabled";
        InEnabled.SetYesNo(1);

        InLeg2Chart.Name = "Leg2 Chart Number (0 = disabled)";
        InLeg2Chart.SetChartNumber(0);

        InHedgeLookback.Name = "Hedge Lookback Bars";
        InHedgeLookback.SetInt(60);
        InHedgeLookback.SetIntLimits(10, 500);

        InZLookback.Name = "Spread Z Lookback Bars";
        InZLookback.SetInt(60);
        InZLookback.SetIntLimits(10, 500);

        InEntryZ.Name = "Entry Z";
        InEntryZ.SetFloat(2.0f);
        InEntryZ.SetFloatLimits(0.5f, 5.0f);

        InExitZ.Name = "Exit Z";
        InExitZ.SetFloat(0.5f);
        InExitZ.SetFloatLimits(0.0f, 2.0f);

        InMinCorr.Name = "Min Rolling Correlation";
        InMinCorr.SetFloat(0.7f);
        InMinCorr.SetFloatLimits(0.0f, 1.0f);

        InMinStdevTicks.Name = "Min Spread Stdev (leg1 ticks)";
        InMinStdevTicks.SetFloat(2.0f);
        InMinStdevTicks.SetFloatLimits(0.0f, 100.0f);

        InHLMin.Name = "Half-Life Min Bars";
        InHLMin.SetInt(2);
        InHLMin.SetIntLimits(1, 1000);

        InHLMax.Name = "Half-Life Max Bars";
        InHLMax.SetInt(200);
        InHLMax.SetIntLimits(2, 2000);

        InHLMult.Name = "Auto Hold = HalfLife x";
        InHLMult.SetFloat(2.0f);
        InHLMult.SetFloatLimits(0.5f, 10.0f);

        InMaxHoldBars.Name = "Max Hold Bars (0 = auto from half-life)";
        InMaxHoldBars.SetInt(0);
        InMaxHoldBars.SetIntLimits(0, 2000);

        InUseSession.Name = "Use Session Filter";
        InUseSession.SetYesNo(0);

        InSessionStart.Name = "Session Start (chart time)";
        InSessionStart.SetTime(HMS_TIME(8, 30, 0));

        InSessionEnd.Name = "Session End (chart time)";
        InSessionEnd.SetTime(HMS_TIME(15, 0, 0));

        InAllowLong.Name = "Allow Long Spread (long leg1 / short leg2)";
        InAllowLong.SetYesNo(1);

        InAllowShort.Name = "Allow Short Spread (short leg1 / long leg2)";
        InAllowShort.SetYesNo(1);

        SgBuy.Name = "Buy Spread (flag)";
        SgBuy.DrawStyle = DRAWSTYLE_POINT;
        SgBuy.PrimaryColor = RGB(0, 255, 0);
        SgBuy.LineWidth = 5;
        SgBuy.DrawZeros = false;

        SgSell.Name = "Sell Spread (flag)";
        SgSell.DrawStyle = DRAWSTYLE_POINT;
        SgSell.PrimaryColor = RGB(255, 0, 0);
        SgSell.LineWidth = 5;
        SgSell.DrawZeros = false;

        SgZ.Name = "Spread Z";
        SgZ.DrawStyle = DRAWSTYLE_LINE;
        SgZ.PrimaryColor = RGB(0, 255, 255);
        SgZ.LineWidth = 2;
        SgZ.DrawZeros = true;

        SgUpper.Name = "Entry +Z";
        SgUpper.DrawStyle = DRAWSTYLE_LINE;
        SgUpper.PrimaryColor = RGB(120, 120, 200);
        SgUpper.LineWidth = 1;
        SgUpper.DrawZeros = false;

        SgLower.Name = "Entry -Z";
        SgLower.DrawStyle = DRAWSTYLE_LINE;
        SgLower.PrimaryColor = RGB(120, 120, 200);
        SgLower.LineWidth = 1;
        SgLower.DrawZeros = false;

        SgBeta.Name = "Hedge Beta (hidden)";
        SgBeta.DrawStyle = DRAWSTYLE_IGNORE;
        SgBeta.DrawZeros = false;

        SgHalt.Name = "Halt Flag (1 = entries halted)";
        SgHalt.DrawStyle = DRAWSTYLE_IGNORE;
        SgHalt.DrawZeros = false;

        Pos = 0;
        EntryBar = -1;
        HoldLimit = 0;
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
        HoldLimit = 0;
    }

    const int leg2Chart = InLeg2Chart.GetChartNumber();
    if (leg2Chart <= 0 || leg2Chart == sc.ChartNumber)
    {
        SgHalt[i] = 1.0f;
        if (i == sc.ArraySize - 1)
        {
            SCString msg;
            msg.Format("StatArbPairs: set Leg2 Chart Number to the other leg's chart (got %d).", leg2Chart);
            sc.AddMessageToLog(msg, 1);
        }
        return;
    }

    int hedgeLen = InHedgeLookback.GetInt();
    int zLen = InZLookback.GetInt();
    // Clamp at read time: harness SETINT bypasses the editor limits, and the
    // stack cache below holds 500 entries (SetDefaults caps both at 500).
    if (hedgeLen < 10) hedgeLen = 10; if (hedgeLen > 500) hedgeLen = 500;
    if (zLen < 10) zLen = 10; if (zLen > 500) zLen = 500;
    const int need = hedgeLen > zLen ? hedgeLen : zLen;
    if (i < need - 1)
    {
        SgHalt[i] = 1.0f;
        SgZ[i] = 0.0f;
        return;
    }
    if (sc.GetBarHasClosedStatus() != BHCS_BAR_HAS_CLOSED)
        return;

    SCGraphData leg2Data;
    sc.GetChartBaseData(leg2Chart, leg2Data);

    // One aligned leg2-close cache over [i-need+1, i]; every consumer below
    // (OLS, spread, half-life) reads the cache, so a mapping miss stalls
    // this bar instead of indexing out of bounds. Stack cache: both
    // lookbacks cap at 500 in SetDefaults, so need <= 500 always fits.
    const int cStart = i - need + 1;
    double leg2c[500];
    for (int k = cStart; k <= i; ++k)
    {
        const int j = sc.GetContainingIndexForDateTimeIndex(leg2Chart, k);
        if (j < 0 || j >= leg2Data.GetArraySize())
        {
            SgHalt[i] = 1.0f;
            return;
        }
        leg2c[k - cStart] = (double)leg2Data[SC_CLOSE][j];
    }

    // OLS beta over the hedge window: the hedge window is the tail of the
    // cache (offset = need - hedgeLen), so leg1 bar winStart+k pairs with
    // cache[offset+k].
    const int winStart = i - hedgeLen + 1;
    const int off = need - hedgeLen;
    double sx = 0.0, sy = 0.0;
    for (int k = 0; k < hedgeLen; ++k)
    {
        sx += leg2c[off + k];
        sy += (double)sc.Close[winStart + k];
    }
    const double mx = sx / (double)hedgeLen;
    const double my = sy / (double)hedgeLen;
    double sxx = 0.0, sxy = 0.0, syy = 0.0;
    for (int k = 0; k < hedgeLen; ++k)
    {
        const double dx = leg2c[off + k] - mx;
        const double dy = (double)sc.Close[winStart + k] - my;
        sxx += dx * dx;
        sxy += dx * dy;
        syy += dy * dy;
    }
    if (sxx <= 0.0 || syy <= 0.0)
    {
        SgHalt[i] = 1.0f;
        return;
    }
    const double beta = sxy / sxx;
    const double intercept = my - beta * mx;
    const double corr = sxy / sqrt(sxx * syy);
    SgBeta[i] = (float)beta;

    // Spread z over the Z window (spread in leg1 price units). The Z window
    // is the tail of the cache (offset = need - zLen); no chart mapping here.
    const int zoff = need - zLen;
    double ssum = 0.0;
    for (int k = 0; k < zLen; ++k)
        ssum += (double)sc.Close[i - zLen + 1 + k] - (beta * leg2c[zoff + k] + intercept);
    const double smean = ssum / (double)zLen;
    double ssq = 0.0;
    for (int k = 0; k < zLen; ++k)
    {
        const double s = (double)sc.Close[i - zLen + 1 + k] - (beta * leg2c[zoff + k] + intercept);
        const double d = s - smean;
        ssq += d * d;
    }
    const double ssd = zLen > 1 ? sqrt(ssq / (double)(zLen - 1)) : 0.0;
    if (ssd <= 0.0)
    {
        SgHalt[i] = 1.0f;
        return;
    }

    const double spreadNow = (double)sc.Close[i] - (beta * leg2c[need - 1] + intercept);
    const double z = (spreadNow - smean) / ssd;

    SgZ[i] = (float)z;
    SgUpper[i] = InEntryZ.GetFloat();
    SgLower[i] = -InEntryZ.GetFloat();

    // Spread half-life: AR(1) on the demeaned spread series. NOTE: short-window
    // OLS underestimates HL, so the [HLMin, HLMax] gate is a regime filter
    // (rejects unit-root/trend), not a precision estimate. Keep HLMin loose.
    double hxx = 0.0, hxy = 0.0;
    for (int k = 1; k < zLen; ++k)
    {
        const double prev = ((double)sc.Close[i - zLen + k] - (beta * leg2c[zoff + k - 1] + intercept)) - smean;
        const double cur = ((double)sc.Close[i - zLen + 1 + k] - (beta * leg2c[zoff + k] + intercept)) - smean;
        hxx += prev * prev;
        hxy += prev * (cur - prev);
    }
    double hl = -1.0;
    if (hxx > 0.0)
    {
        const double lambda = hxy / hxx;
        if (lambda < 0.0)
            hl = -kLn2 / lambda;
    }

    const double entryZ = (double)InEntryZ.GetFloat();
    const double exitZ = (double)InExitZ.GetFloat();
    const double sdFloor = (double)InMinStdevTicks.GetFloat() * (double)sc.TickSize;
    const int hlMin = InHLMin.GetInt();
    const int hlMax = InHLMax.GetInt();
    const bool healthy = corr >= (double)InMinCorr.GetFloat()
        && ssd >= sdFloor
        && hl > 0.0 && hl >= (double)hlMin && hl <= (double)hlMax;
    SgHalt[i] = healthy ? 0.0f : 1.0f;

    const int barTime = sc.BaseDateTimeIn[i].GetTimeInSeconds();
    const bool sessionOk = InSession(barTime, InSessionStart.GetTime(), InSessionEnd.GetTime(),
        InUseSession.GetYesNo() != 0);

    if (Pos != 0)
    {
        const bool meanTouched = (Pos > 0 && z >= -exitZ) || (Pos < 0 && z <= exitZ);
        const bool oppositeSignal = (Pos > 0 && z >= entryZ) || (Pos < 0 && z <= -entryZ);
        const bool timedOut = HoldLimit > 0 && (i - EntryBar) >= HoldLimit;
        if (meanTouched || timedOut || oppositeSignal)
        {
            Pos = 0;
            EntryBar = -1;
            HoldLimit = 0;
            if (!oppositeSignal)
                return;
        }
        else
        {
            return;
        }
    }

    if (!healthy || !sessionOk)
        return;

    const bool longOk = InAllowLong.GetYesNo() != 0;
    const bool shortOk = InAllowShort.GetYesNo() != 0;

    int hold = InMaxHoldBars.GetInt();
    if (hold <= 0)
    {
        hold = (int)ceil(hl * (double)InHLMult.GetFloat());
        if (hold < hlMin)
            hold = hlMin;
        if (hold > hlMax * 2)
            hold = hlMax * 2;
    }

    if (longOk && z <= -entryZ)
    {
        Pos = 1;
        EntryBar = i;
        HoldLimit = hold;
        SgBuy[i] = (float)z;
        if (sc.IsNewBar(i))
            sc.AlertWithMessage(199, "StatArbPairs BUY spread");
    }
    else if (shortOk && z >= entryZ)
    {
        Pos = -1;
        EntryBar = i;
        HoldLimit = hold;
        SgSell[i] = (float)z;
        if (sc.IsNewBar(i))
            sc.AlertWithMessage(200, "StatArbPairs SELL spread");
    }
}
