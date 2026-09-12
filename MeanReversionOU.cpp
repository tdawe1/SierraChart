#include "sierrachart.h"
#include <cmath>

SCDLLName("MeanReversionOU")

// MeanReversionOU — single-asset Ornstein-Uhlenbeck / z-score mean reversion.
//
// Public-baseline design (Engle-Granger/OU literature, AlgoDrill/Quantt/OpenAlgo):
//   z = (close - rolling mean) / rolling stdev over Lookback bars.
//   Enter long at z <= -EntryZ, short at z >= +EntryZ (closed bars only).
//   Exit at |z| <= ExitZ, on the opposite entry signal (flip), or after
//   MaxHoldBars (0 = auto: ceil(HalfLife * HLMult), clamped to [HLMin, HLMax]).
// Health gates (the parts naive Bollinger fades skip):
//   - stdev floor (MinStdevTicks * TickSize): no signals on flat lines.
//   - half-life from an AR(1) fit (Δy on lagged demeaned y) must satisfy
//     lambda < 0 and HL in [HLMin, HLMax]; otherwise entries halt.
//   - optional session filter (chart time, overnight-aware).
// Signal-only: no order calls. Wire Buy/Sell subgraphs into BacktestExporter
// Signal inputs and assess with `signal_replay` (regime: mean-reversion).

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

inline void RollingMeanStdev(SCFloatArrayRef Data, int End, int Length, double& Mean, double& Stdev)
{
    Mean = 0.0;
    Stdev = 0.0;
    if (Length < 2 || End < Length - 1)
        return;
    double sum = 0.0;
    for (int k = End - Length + 1; k <= End; ++k)
        sum += (double)Data[k];
    Mean = sum / (double)Length;
    double sq = 0.0;
    for (int k = End - Length + 1; k <= End; ++k)
    {
        const double d = (double)Data[k] - Mean;
        sq += d * d;
    }
    Stdev = sqrt(sq / (double)(Length - 1));
}

// AR(1) half-life of Data over [End-Length+1, End], demeaned by Mean.
// Returns -1 when undefined (lambda >= 0 or degenerate window).
// NOTE: short-window OLS underestimates HL (biased toward fast reversion),
// so the [HLMin, HLMax] gate is a regime filter (rejects unit-root/trend),
// not a precision estimate. Keep HLMin loose; size holds off HL*HLMult.
inline double HalfLifeBars(SCFloatArrayRef Data, int End, int Length, double Mean)
{
    if (Length < 10 || End < Length - 1)
        return -1.0;
    double sxx = 0.0, sxy = 0.0;
    for (int k = End - Length + 2; k <= End; ++k)
    {
        const double x = (double)Data[k - 1] - Mean;
        const double y = (double)Data[k] - (double)Data[k - 1];
        sxx += x * x;
        sxy += x * y;
    }
    if (sxx <= 0.0)
        return -1.0;
    const double lambda = sxy / sxx;
    if (lambda >= 0.0)
        return -1.0;
    return -kLn2 / lambda;
}
}  // namespace

SCSFExport scsf_MeanReversionOU(SCStudyInterfaceRef sc)
{
    SCInputRef InEnabled = sc.Input[0];
    SCInputRef InLookback = sc.Input[1];
    SCInputRef InEntryZ = sc.Input[2];
    SCInputRef InExitZ = sc.Input[3];
    SCInputRef InMinStdevTicks = sc.Input[4];
    SCInputRef InMaxHoldBars = sc.Input[5];
    SCInputRef InHLMult = sc.Input[6];
    SCInputRef InHLMin = sc.Input[7];
    SCInputRef InHLMax = sc.Input[8];
    SCInputRef InUseSession = sc.Input[9];
    SCInputRef InSessionStart = sc.Input[10];
    SCInputRef InSessionEnd = sc.Input[11];
    SCInputRef InAllowLong = sc.Input[12];
    SCInputRef InAllowShort = sc.Input[13];

    SCSubgraphRef SgBuy = sc.Subgraph[0];
    SCSubgraphRef SgSell = sc.Subgraph[1];
    SCSubgraphRef SgUpper = sc.Subgraph[2];
    SCSubgraphRef SgLower = sc.Subgraph[3];
    SCSubgraphRef SgBasis = sc.Subgraph[4];
    SCSubgraphRef SgZ = sc.Subgraph[5];
    SCSubgraphRef SgHalt = sc.Subgraph[6];

    int& Pos = sc.GetPersistentInt(0);
    int& EntryBar = sc.GetPersistentInt(1);
    int& HoldLimit = sc.GetPersistentInt(2);

    if (sc.SetDefaults)
    {
        sc.GraphName = "Mean Reversion OU";
        sc.StudyDescription = "OU/z-score mean reversion: enter at |z|>=EntryZ, exit at |z|<=ExitZ, half-life time-stop and stdev-floor health gates. Signal-only.";
        sc.GraphRegion = 0;
        sc.AutoLoop = 1;
        sc.FreeDLL = 0;

        InEnabled.Name = "Enabled";
        InEnabled.SetYesNo(1);

        InLookback.Name = "Lookback Bars";
        InLookback.SetInt(60);
        InLookback.SetIntLimits(10, 500);

        InEntryZ.Name = "Entry Z";
        InEntryZ.SetFloat(2.0f);
        InEntryZ.SetFloatLimits(0.5f, 5.0f);

        InExitZ.Name = "Exit Z";
        InExitZ.SetFloat(0.5f);
        InExitZ.SetFloatLimits(0.0f, 2.0f);

        InMinStdevTicks.Name = "Min Stdev (ticks)";
        InMinStdevTicks.SetFloat(2.0f);
        InMinStdevTicks.SetFloatLimits(0.0f, 100.0f);

        InMaxHoldBars.Name = "Max Hold Bars (0 = auto from half-life)";
        InMaxHoldBars.SetInt(0);
        InMaxHoldBars.SetIntLimits(0, 2000);

        InHLMult.Name = "Auto Hold = HalfLife x";
        InHLMult.SetFloat(2.0f);
        InHLMult.SetFloatLimits(0.5f, 10.0f);

        InHLMin.Name = "Half-Life Min Bars";
        InHLMin.SetInt(2);
        InHLMin.SetIntLimits(1, 1000);

        InHLMax.Name = "Half-Life Max Bars";
        InHLMax.SetInt(200);
        InHLMax.SetIntLimits(2, 2000);

        InUseSession.Name = "Use Session Filter";
        InUseSession.SetYesNo(0);

        InSessionStart.Name = "Session Start (chart time)";
        InSessionStart.SetTime(HMS_TIME(8, 30, 0));

        InSessionEnd.Name = "Session End (chart time)";
        InSessionEnd.SetTime(HMS_TIME(15, 0, 0));

        InAllowLong.Name = "Allow Long";
        InAllowLong.SetYesNo(1);

        InAllowShort.Name = "Allow Short";
        InAllowShort.SetYesNo(1);

        SgBuy.Name = "Buy";
        SgBuy.DrawStyle = DRAWSTYLE_POINT_ON_LOW;
        SgBuy.PrimaryColor = RGB(0, 255, 0);
        SgBuy.LineWidth = 5;
        SgBuy.DrawZeros = false;

        SgSell.Name = "Sell";
        SgSell.DrawStyle = DRAWSTYLE_POINT_ON_HIGH;
        SgSell.PrimaryColor = RGB(255, 0, 0);
        SgSell.LineWidth = 5;
        SgSell.DrawZeros = false;

        SgUpper.Name = "Upper Band";
        SgUpper.DrawStyle = DRAWSTYLE_LINE;
        SgUpper.PrimaryColor = RGB(120, 120, 200);
        SgUpper.LineWidth = 1;
        SgUpper.DrawZeros = false;

        SgLower.Name = "Lower Band";
        SgLower.DrawStyle = DRAWSTYLE_LINE;
        SgLower.PrimaryColor = RGB(120, 120, 200);
        SgLower.LineWidth = 1;
        SgLower.DrawZeros = false;

        SgBasis.Name = "Basis (rolling mean)";
        SgBasis.DrawStyle = DRAWSTYLE_LINE;
        SgBasis.PrimaryColor = RGB(200, 200, 200);
        SgBasis.LineWidth = 1;
        SgBasis.DrawZeros = false;

        SgZ.Name = "Z-Score (hidden, for exporter)";
        SgZ.DrawStyle = DRAWSTYLE_IGNORE;
        SgZ.DrawZeros = false;

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

    const int Lookback = InLookback.GetInt();
    if (i < Lookback - 1)
    {
        SgHalt[i] = 1.0f;
        SgZ[i] = 0.0f;
        return;
    }
    if (sc.GetBarHasClosedStatus() != BHCS_BAR_HAS_CLOSED)
        return;

    double mean = 0.0, sd = 0.0;
    RollingMeanStdev(sc.Close, i, Lookback, mean, sd);

    const double entryZ = (double)InEntryZ.GetFloat();
    const double exitZ = (double)InExitZ.GetFloat();
    const double sdFloor = (double)InMinStdevTicks.GetFloat() * (double)sc.TickSize;
    const double hl = HalfLifeBars(sc.Close, i, Lookback, mean);

    const int hlMin = InHLMin.GetInt();
    const int hlMax = InHLMax.GetInt();
    const bool hlOk = hl > 0.0 && hl >= (double)hlMin && hl <= (double)hlMax;
    const bool sdOk = sd >= sdFloor && sd > 0.0;
    const bool healthy = hlOk && sdOk;

    SgHalt[i] = healthy ? 0.0f : 1.0f;

    if (sd > 0.0)
    {
        SgBasis[i] = (float)mean;
        SgUpper[i] = (float)(mean + entryZ * sd);
        SgLower[i] = (float)(mean - entryZ * sd);
        SgZ[i] = (float)(((double)sc.Close[i] - mean) / sd);
    }
    else
    {
        SgBasis[i] = (float)mean;
        SgUpper[i] = (float)mean;
        SgLower[i] = (float)mean;
        SgZ[i] = 0.0f;
        return;
    }

    const double z = ((double)sc.Close[i] - mean) / sd;
    const int barTime = sc.BaseDateTimeIn[i].GetTimeInSeconds();
    const bool sessionOk = InSession(barTime, InSessionStart.GetTime(), InSessionEnd.GetTime(),
        InUseSession.GetYesNo() != 0);

    // Open-position management: exits always evaluated, even when halted.
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

    // Flat: entries need health + session + direction enabled.
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
        SgBuy[i] = sc.Low[i] - sc.TickSize;
        if (sc.IsNewBar(i))
            sc.AlertWithMessage(197, "MeanReversionOU BUY");
    }
    else if (shortOk && z >= entryZ)
    {
        Pos = -1;
        EntryBar = i;
        HoldLimit = hold;
        SgSell[i] = sc.High[i] + sc.TickSize;
        if (sc.IsNewBar(i))
            sc.AlertWithMessage(198, "MeanReversionOU SELL");
    }
}
