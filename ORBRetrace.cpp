#include "sierrachart.h"

SCDLLName("ORBRetrace")

// ORBRetrace - intraday opening-range breakout-retracement system for
// index futures (NQ-first). Port of dws-data/nas-orb-backtester rules:
// All session minutes below are CHART-local (BarTime.GetHour/Minute, no ET
// conversion); defaults assume an ET chart — on a Central-TZ chart 09:30
// reads as 10:30 ET. Keep the chart timezone aligned with these inputs.
// Opening range 09:30-09:45 ET; a 1m bar CLOSE beyond the OR extreme plus
// a tick threshold confirms the breakout (no wick entries); entries arm
// on the breakout bar and fill only when a LATER bar retraces to touch a
// volume-profile level (VAH/POC/VAL) computed from the OR bars, entering
// for continuation beyond the extreme.
// Stop at the OR extreme; target a measured move beyond the extreme
// (extreme + (extreme - entry level)): the source specifies "beyond the
// extreme" without a formula, so this is an interpretation, flagged here.
// EOD force-flat and an entry cutoff complete the session.
// Value area is the standard 70% expansion from the POC over OR-bar
// volume buckets (one bucket per tick; days with >4096 buckets halt).
// Signal-only, closed-bar only: stop/target/EOD exits all ride the
// opposite signal, so assessment runs with engine stops/targets OFF
// (fixed-tick stops cannot match a per-day OR stop; run fixed stops as
// a second pass to price control). Intraday bars only.
// Assessment recipe (EXACT): Buy->SignalLong, Sell->SignalShort, stops /
// targets / max_hold OFF, allow_long=true, allow_short=true (load-bearing
// both ways: entries need their direction allowed, exits need the other),
// exit_on_opposite=true, reverse_on_opposite=false. Two-sided induction:
// no Buy is emitted while long and no Sell while short, so with exits on
// opposite signals the engine can neither open a naked position nor
// reverse without a fresh entry signal. Regime: trend (continuation).
namespace
{

// Minutes since midnight for a bar time (chart-local; NOT converted to ET).
inline int BarMinutes(SCDateTime BarTime)
{
    return BarTime.GetHour() * 60 + BarTime.GetMinute();
}

// 70% value area over BucketVol[0..NB-1]: POC = argmax, then expand one
// bucket at a time toward the heavier side until >= 70% of total volume.
inline void ValueArea70(const double *BucketVol, int NB,
                        int &PocBucket, int &VahBucket, int &ValBucket)
{
    PocBucket = 0;
    double total = 0.0;
    for (int b = 0; b < NB; ++b)
    {
        total += BucketVol[b];
        if (BucketVol[b] > BucketVol[PocBucket])
            PocBucket = b;
    }
    int up = PocBucket, dn = PocBucket;
    double inArea = BucketVol[PocBucket];
    const double need = total * 0.70;
    while (inArea < need && (up + 1 < NB || dn - 1 >= 0))
    {
        const double upVol = (up + 1 < NB) ? BucketVol[up + 1] : -1.0;
        const double dnVol = (dn - 1 >= 0) ? BucketVol[dn - 1] : -1.0;
        if (upVol >= dnVol)
        {
            ++up;
            inArea += BucketVol[up];
        }
        else
        {
            --dn;
            inArea += BucketVol[dn];
        }
    }
    VahBucket = up;
    ValBucket = dn;
}

} // namespace

SCSFExport scsf_ORBRetrace(SCStudyInterfaceRef sc)
{
    SCInputRef InEnabled = sc.Input[0];
    SCInputRef InORStartMin = sc.Input[1];
    SCInputRef InOREndMin = sc.Input[2];
    SCInputRef InBreakoutTicks = sc.Input[3];
    SCInputRef InVPLevel = sc.Input[4];
    SCInputRef InEntryCutoffMin = sc.Input[5];
    SCInputRef InEODFlatMin = sc.Input[6];
    SCInputRef InUseShorts = sc.Input[7];

    SCSubgraphRef SgBuy = sc.Subgraph[0];
    SCSubgraphRef SgSell = sc.Subgraph[1];
    SCSubgraphRef SgORHigh = sc.Subgraph[2];
    SCSubgraphRef SgORLow = sc.Subgraph[3];
    SCSubgraphRef SgVPOC = sc.Subgraph[4];
    SCSubgraphRef SgVAH = sc.Subgraph[5];
    SCSubgraphRef SgVAL = sc.Subgraph[6];
    SCSubgraphRef SgHalt = sc.Subgraph[7];

    // Per-day state. pPhase: 0 collect, 1 armed, 2 wait-long, 3 wait-short,
    // 4 in-long, 5 in-short, 6 done. pPos mirrors 4/5 as +1/-1.
    int &pDay = sc.GetPersistentInt(0);
    int &pPhase = sc.GetPersistentInt(1);
    int &pPos = sc.GetPersistentInt(2);
    int &pORStartIdx = sc.GetPersistentInt(3);
    int &pOREndIdx = sc.GetPersistentInt(4);
    double &pORHigh = sc.GetPersistentDouble(5);
    double &pORLow = sc.GetPersistentDouble(6);
    double &pStop = sc.GetPersistentDouble(7);
    double &pTarget = sc.GetPersistentDouble(8);

    if (sc.SetDefaults)
    {
        sc.GraphName = "ORB Retrace (NQ)";
        sc.GraphRegion = 0;
        sc.AutoLoop = 1;

        InEnabled.Name = "Enabled";
        InEnabled.SetYesNo(1);

        InORStartMin.Name = "OR Start (min since midnight, chart time)";
        InORStartMin.SetInt(570);
        InORStartMin.SetIntLimits(0, 1439);

        InOREndMin.Name = "OR End (min since midnight, chart time)";
        InOREndMin.SetInt(585);
        InOREndMin.SetIntLimits(1, 1440);

        InBreakoutTicks.Name = "Breakout Threshold (ticks)";
        InBreakoutTicks.SetInt(8);
        InBreakoutTicks.SetIntLimits(0, 500);

        InVPLevel.Name = "Entry Level: 0=POC, 1=VAH, 2=VAL";
        InVPLevel.SetInt(0);
        InVPLevel.SetIntLimits(0, 2);

        InEntryCutoffMin.Name = "Entry Cutoff (min since midnight, chart time)";
        InEntryCutoffMin.SetInt(720);
        InEntryCutoffMin.SetIntLimits(0, 1439);

        InEODFlatMin.Name = "EOD Force-Flat (min since midnight, chart time)";
        InEODFlatMin.SetInt(955);
        InEODFlatMin.SetIntLimits(0, 1439);

        InUseShorts.Name = "Take Short Side";
        InUseShorts.SetYesNo(1);

        SgBuy.Name = "ORB Long Entry";
        SgBuy.DrawStyle = DRAWSTYLE_ARROW_UP;
        SgBuy.PrimaryColor = RGB(0, 255, 0);
        SgBuy.LineWidth = 2;
        SgBuy.DrawZeros = false;

        SgSell.Name = "ORB Short Entry / Exit";
        SgSell.DrawStyle = DRAWSTYLE_ARROW_DOWN;
        SgSell.PrimaryColor = RGB(255, 0, 0);
        SgSell.LineWidth = 2;
        SgSell.DrawZeros = false;

        SgORHigh.Name = "OR High";
        SgORHigh.DrawStyle = DRAWSTYLE_LINE;
        SgORHigh.PrimaryColor = RGB(0, 180, 255);
        SgORHigh.LineWidth = 1;
        SgORHigh.DrawZeros = false;

        SgORLow.Name = "OR Low";
        SgORLow.DrawStyle = DRAWSTYLE_LINE;
        SgORLow.PrimaryColor = RGB(255, 180, 0);
        SgORLow.LineWidth = 1;
        SgORLow.DrawZeros = false;

        SgVPOC.Name = "OR POC";
        SgVPOC.DrawStyle = DRAWSTYLE_DASH;
        SgVPOC.PrimaryColor = RGB(255, 255, 255);
        SgVPOC.LineWidth = 1;
        SgVPOC.DrawZeros = false;

        SgVAH.Name = "OR VAH";
        SgVAH.DrawStyle = DRAWSTYLE_DASH;
        SgVAH.PrimaryColor = RGB(150, 150, 150);
        SgVAH.LineWidth = 1;
        SgVAH.DrawZeros = false;

        SgVAL.Name = "OR VAL";
        SgVAL.DrawStyle = DRAWSTYLE_DASH;
        SgVAL.PrimaryColor = RGB(150, 150, 150);
        SgVAL.LineWidth = 1;
        SgVAL.DrawZeros = false;

        SgHalt.Name = "Halt Flag (1 = no entries)";
        SgHalt.DrawStyle = DRAWSTYLE_IGNORE;
        SgHalt.DrawZeros = false;

        pDay = -1;
        pPhase = 0;
        pPos = 0;
        pORStartIdx = -1;
        pOREndIdx = -1;
        return;
    }

    const int i = sc.Index;
    SgBuy[i] = 0.0f;
    SgSell[i] = 0.0f;
    SgORHigh[i] = 0.0f;
    SgORLow[i] = 0.0f;
    SgVPOC[i] = 0.0f;
    SgVAH[i] = 0.0f;
    SgVAL[i] = 0.0f;
    SgHalt[i] = 1.0f;
    if (!InEnabled.GetYesNo())
        return;
    if (i == 0)
    {
        // Full recalculations replay from bar 0 with stale persistents;
        // restart the day machine so replay matches a fresh compute.
        pDay = -1;
        pPhase = 0;
        pPos = 0;
        pORStartIdx = -1;
        pOREndIdx = -1;
    }
    if (sc.GetBarHasClosedStatus() != BHCS_BAR_HAS_CLOSED)
        return;

    const int orStart = InORStartMin.GetInt();
    const int orEnd = InOREndMin.GetInt();
    const int cutoff = InEntryCutoffMin.GetInt();
    const int eodFlat = InEODFlatMin.GetInt();
    if (orEnd <= orStart)
    {
        // Misconfigured window: halt loudly on the last bar only.
        if (i == sc.ArraySize - 1)
            sc.AddMessageToLog("ORBRetrace: OR End must be after OR Start.", 1);
        return;
    }

    const int day = sc.BaseDateTimeIn[i].GetDate();
    const int mins = BarMinutes(sc.BaseDateTimeIn[i]);
    if (day != pDay)
    {
        // New session: force-flat a stale position first (EOD bar missing
        // or weekend gap), then restart the day machine.
        if (pPos == 1)
            SgSell[i] = sc.High[i] + sc.TickSize;
        else if (pPos == -1)
            SgBuy[i] = sc.Low[i] - sc.TickSize;
        pDay = day;
        pPhase = 0;
        pPos = 0;
        pORStartIdx = -1;
        pOREndIdx = -1;
        pORHigh = 0.0;
        pORLow = 0.0;
    }

    const double tick = (sc.TickSize > 0.0) ? sc.TickSize : 0.25;

    if (mins >= orStart && mins < orEnd)
    {
        // Inside the window: accumulate the range.
        if (pORStartIdx < 0)
        {
            pORStartIdx = i;
            pORHigh = (double)sc.High[i];
            pORLow = (double)sc.Low[i];
        }
        else
        {
            if ((double)sc.High[i] > pORHigh)
                pORHigh = (double)sc.High[i];
            if ((double)sc.Low[i] < pORLow)
                pORLow = (double)sc.Low[i];
        }
        pOREndIdx = i;
        return;
    }

    if (pORStartIdx < 0 || pOREndIdx < pORStartIdx)
        return; // no OR printed yet today (holiday / late start)

    SgORHigh[i] = (float)pORHigh;
    SgORLow[i] = (float)pORLow;

    // Volume profile over the OR bars: one bucket per tick.
    const int nORBars = pOREndIdx - pORStartIdx + 1;
    if (nORBars < 2)
        return; // too thin to profile; halt the day
    const int nBuckets = (int)((pORHigh - pORLow) / tick) + 1;
    const int kMaxBuckets = 4096;
    if (nBuckets < 1 || nBuckets > kMaxBuckets)
        return; // degenerate or runaway range; halt the day
    double bucketVol[kMaxBuckets];
    for (int b = 0; b < nBuckets; ++b)
        bucketVol[b] = 0.0;
    for (int k = pORStartIdx; k <= pOREndIdx; ++k)
    {
        const double v = (double)sc.Volume[k] / (double)nORBars;
        // Spread the bar's volume across the buckets it spans so wide
        // bars do not pile everything on the close bucket.
        int lo = (int)(((double)sc.Low[k] - pORLow) / tick);
        int hi = (int)(((double)sc.High[k] - pORLow) / tick);
        if (lo < 0)
            lo = 0;
        if (hi >= nBuckets)
            hi = nBuckets - 1;
        for (int b = lo; b <= hi; ++b)
            bucketVol[b] += v / (double)(hi - lo + 1);
    }
    int pocB = 0, vahB = 0, valB = 0;
    ValueArea70(bucketVol, nBuckets, pocB, vahB, valB);
    const double poc = pORLow + (pocB + 0.5) * tick;
    // Bucket centers can overshoot the range by half a tick at the edge
    // buckets; clamp so levels (and the measured-move target) stay sane.
    double vah = pORLow + (vahB + 0.5) * tick;
    double val = pORLow + (valB + 0.5) * tick;
    if (vah > pORHigh)
        vah = pORHigh;
    if (val < pORLow)
        val = pORLow;
    SgVPOC[i] = (float)poc;
    SgVAH[i] = (float)vah;
    SgVAL[i] = (float)val;

    const int lvlSel = InVPLevel.GetInt();
    const double entryLevel = (lvlSel == 1) ? vah : (lvlSel == 2) ? val : poc;

    // Day machine. Transitions only on closed bars; exits take priority
    // over entries and at most one signal fires per bar.
    const double thr = (double)InBreakoutTicks.GetInt() * tick;
    const double close = (double)sc.Close[i];
    const double high = (double)sc.High[i];
    const double low = (double)sc.Low[i];

    if (pPos == 1)
    {
        SgHalt[i] = 0.0f;
        if (low <= pStop || high >= pTarget || mins >= eodFlat)
        {
            pPos = 0;
            pPhase = 6;
            SgSell[i] = high + tick;
            if (sc.IsNewBar(i))
                sc.AlertWithMessage(206, "ORBRetrace EXIT");
        }
        return;
    }
    if (pPos == -1)
    {
        SgHalt[i] = 0.0f;
        if (high >= pStop || low <= pTarget || mins >= eodFlat)
        {
            pPos = 0;
            pPhase = 6;
            SgBuy[i] = low - tick;
            if (sc.IsNewBar(i))
                sc.AlertWithMessage(205, "ORBRetrace EXIT");
        }
        return;
    }

    if (pPhase == 6 || mins >= eodFlat)
    {
        pPhase = 6;
        return; // done for the day
    }

    if (pPhase == 0)
    {
        // First bar at/after the window: arm on a close-confirmed break.
        pPhase = 1;
    }
    if (pPhase == 1)
    {
        SgHalt[i] = 0.0f;
        const bool brkLong = close > pORHigh + thr;
        const bool brkShort = close < pORLow - thr;
        // brkLong && brkShort is unsatisfiable (pORHigh >= pORLow,
        // thr >= 0), so no mid-range tiebreak is needed.
        if (brkLong)
            pPhase = 2;
        else if (brkShort)
            pPhase = 3;
        // Armed this bar or still waiting: retrace entries start next bar,
        // so a breakout bar's own low can never fill as a "retrace".
        return;
    }

    // Wait for the retrace touch, then enter for continuation.
    if (mins >= cutoff)
        return; // entry window over; Halt stays 1
    SgHalt[i] = 0.0f;
    if (pPhase == 2)
    {
        if (low <= entryLevel)
        {
            pPos = 1;
            pPhase = 4;
            pStop = pORLow;
            pTarget = pORHigh + (pORHigh - entryLevel);
            SgBuy[i] = low - tick;
            if (sc.IsNewBar(i))
                sc.AlertWithMessage(205, "ORBRetrace BUY");
        }
    }
    else if (pPhase == 3)
    {
        if (!InUseShorts.GetYesNo())
        {
            pPhase = 6;
            return;
        }
        if (high >= entryLevel)
        {
            pPos = -1;
            pPhase = 5;
            pStop = pORHigh;
            pTarget = pORLow - (entryLevel - pORLow);
            SgSell[i] = high + tick;
            if (sc.IsNewBar(i))
                sc.AlertWithMessage(206, "ORBRetrace SELL");
        }
    }
}
