#include "sierrachart.h"

#include <cstdio>
#include <cstring>

SCDLLName("Prop Risk Overlay")

// PropRiskOverlay: live prop guardrails drawn on the chart for manual
// trading. Copy this file into ACS_Source and Remote Build it (self-
// contained, no extra headers).
//
// Reads the account exactly like the broker sees it:
//   opening equity (account value, or manual override) + daily closed P/L
//   (Trade Statistics, needs "Maintain Trade Statistics and Trades Data"
//   enabled on the chart) + open position P/L.
// Draws, top-left:
//   day P/L vs daily loss limit | trailing room | profit-target progress
//   size calculator (contracts for your stop at your risk %) + session
//   gate (TRADE / STANDBY by wall clock on the last bar)
//   HALT lines when the daily or trailing limit is breached.
//
// What it does NOT do: consistency needs multi-day history and lives in
// the backtester (bt.py reports best-day share PASS/FAIL). This overlay
// governs today: size, session, daily halt, trailing halt, target.
static const int kDrawDay = 202609101;
static const int kDrawSize = 202609102;
static const int kDrawHaltDay = 202609103;
static const int kDrawHaltTrail = 202609104;

static bool ParseHM(const char* s, int& hh, int& mm) {
    hh = 0;
    mm = 0;
    if (s == nullptr || s[0] == '\0')
        return false;
    return sscanf(s, "%d:%d", &hh, &mm) == 2 && hh >= 0 && hh < 24 && mm >= 0 && mm < 60;
}

static bool InWindow(const char* window, int hh, int mm) {
    if (window == nullptr || window[0] == '\0')
        return false;
    char buf[32];
    strncpy(buf, window, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* dash = strchr(buf, '-');
    if (dash == nullptr)
        return false;
    *dash = '\0';
    int h0, m0, h1, m1;
    if (!ParseHM(buf, h0, m0) || !ParseHM(dash + 1, h1, m1))
        return false;
    const int t = hh * 60 + mm;
    const int a = h0 * 60 + m0;
    const int b = h1 * 60 + m1;
    if (a <= b)
        return t >= a && t <= b;
    return t >= a || t <= b;
}

SCSFExport scsf_PropRiskOverlay(SCStudyInterfaceRef sc) {
    SCInputRef InUseManual = sc.Input[0];
    SCInputRef InOpening = sc.Input[1];
    SCInputRef InRiskPct = sc.Input[2];
    SCInputRef InStopTicks = sc.Input[3];
    SCInputRef InTickValue = sc.Input[4];
    SCInputRef InFee = sc.Input[5];
    SCInputRef InMaxContracts = sc.Input[6];
    SCInputRef InDailyLimit = sc.Input[7];
    SCInputRef InTrailLimit = sc.Input[8];
    SCInputRef InTarget = sc.Input[9];
    SCInputRef InWindow1 = sc.Input[10];
    SCInputRef InWindow2 = sc.Input[11];
    SCInputRef InResetPeak = sc.Input[12];
    SCInputRef InShowSize = sc.Input[13];
    SCInputRef InRefreshSec = sc.Input[14];
    SCSubgraphRef Text = sc.Subgraph[0];

    if (sc.SetDefaults) {
        sc.GraphName = "Prop Risk Overlay";
        sc.GraphRegion = 0;
        sc.AutoLoop = 0;
        sc.UpdateAlways = 1;
        sc.MaintainTradeStatisticsAndTradesData = 1;

        InUseManual.Name = "Use manual opening equity";
        InUseManual.SetYesNo(0);
        InOpening.Name = "Manual opening equity";
        InOpening.SetFloat(50000);
        InRiskPct.Name = "Risk per trade %";
        InRiskPct.SetFloat(1.0f);
        InStopTicks.Name = "Stop size (ticks) for calculator";
        InStopTicks.SetInt(12);
        InTickValue.Name = "Tick value $/tick/contract (ES 12.50, NQ 5.00)";
        InTickValue.SetFloat(12.5f);
        InFee.Name = "Fee per side";
        InFee.SetFloat(2.10f);
        InMaxContracts.Name = "Max contracts (prop cap)";
        InMaxContracts.SetInt(10);
        InDailyLimit.Name = "Daily loss limit $ (0=off)";
        InDailyLimit.SetFloat(1000);
        InTrailLimit.Name = "Trailing drawdown limit $ (0=off)";
        InTrailLimit.SetFloat(2000);
        InTarget.Name = "Profit target $ (0=off)";
        InTarget.SetFloat(3000);
        InWindow1.Name = "Trade window 1 (HH:MM-HH:MM, empty=off)";
        InWindow1.SetString("08:30-11:00");
        InWindow2.Name = "Trade window 2 (HH:MM-HH:MM, empty=off)";
        InWindow2.SetString("13:30-15:00");
        InResetPeak.Name = "Reset trailing peak to live now";
        InResetPeak.SetYesNo(0);
        InShowSize.Name = "Show size/session line";
        InShowSize.SetYesNo(1);
        InRefreshSec.Name = "Refresh seconds";
        InRefreshSec.SetInt(5);

        Text.Name = "Text (color + font size)";
        Text.PrimaryColor = RGB(229, 231, 235);
        Text.LineWidth = 14;
        Text.DrawStyle = DRAWSTYLE_IGNORE;
        return;
    }

    if (sc.Index != sc.ArraySize - 1)
        return;
    int& last_update = sc.GetPersistentInt(0);
    float& peak = sc.GetPersistentFloat(0);
    const int now_sec = sc.CurrentSystemDateTime.GetTimeInSeconds();
    const int refresh = InRefreshSec.GetInt() < 1 ? 1 : InRefreshSec.GetInt();
    if (last_update != 0 && now_sec - last_update < refresh)
        return;
    last_update = now_sec;

    SCString account = sc.SelectedTradeAccount;
    double opening = static_cast<double>(InOpening.GetFloat());
    if (InUseManual.GetYesNo() == 0) {
        n_ACSIL::s_TradeAccountDataFields fields;
        if (sc.GetTradeAccountData(fields, account) != 0 && fields.m_AccountValue != 0)
            opening = fields.m_AccountValue;
    }
    double daily_closed = 0;
    n_ACSIL::s_TradeStatistics stats;
    if (sc.GetTradeStatisticsForSymbolV2(n_ACSIL::STATS_TYPE_DAILY_ALL_TRADES, stats) != 0)
        daily_closed = stats.ClosedTradesProfitLoss;
    double open_pl = 0;
    s_SCPositionData pos;
    if (sc.GetTradePosition(pos) == 1)
        open_pl = pos.OpenProfitLoss;
    const double live = opening + daily_closed + open_pl;
    const double day_pl = daily_closed + open_pl;

    if (InResetPeak.GetYesNo() != 0 || peak == 0 || live > peak)
        peak = static_cast<float>(live);

    const double daily_limit = InDailyLimit.GetFloat();
    const double trail_limit = InTrailLimit.GetFloat();
    const double target = InTarget.GetFloat();
    const bool halt_day = daily_limit > 0 && day_pl <= -daily_limit;
    const bool halt_trail = trail_limit > 0 && (peak - live) >= trail_limit;

    SCString dt = sc.DateTimeToString(sc.BaseDateTimeIn[sc.ArraySize - 1], FLAG_DT_COMPLETE_DATETIME);
    int hh = -1;
    int mm = -1;
    if (dt.GetLength() >= 16)
        sscanf(dt.GetChars() + 11, "%d:%d", &hh, &mm);
    bool in_session = false;
    const SCString w1 = InWindow1.GetString();
    const SCString w2 = InWindow2.GetString();
    if (hh >= 0) {
        in_session = InWindow(w1.GetChars(), hh, mm)
            || InWindow(w2.GetChars(), hh, mm);
        if (w1.GetChars()[0] == '\0' && w2.GetChars()[0] == '\0')
            in_session = true;
    }

    int size = 1;
    const double per_contract = InStopTicks.GetInt() * InTickValue.GetFloat()
        + 2 * InFee.GetFloat();
    if (per_contract > 0 && InRiskPct.GetFloat() > 0)
        size = (int)(opening * InRiskPct.GetFloat() / 100.0 / per_contract);
    if (size < 1)
        size = 1;
    if (size > InMaxContracts.GetInt() && InMaxContracts.GetInt() > 0)
        size = InMaxContracts.GetInt();

    auto draw = [&](int line_number, int vpos, const SCString& line_text, COLORREF color) {
        s_UseTool tool;
        tool.Clear();
        tool.ChartNumber = sc.ChartNumber;
        tool.DrawingType = DRAWING_TEXT;
        tool.LineNumber = line_number;
        tool.AddMethod = UTAM_ADD_OR_ADJUST;
        tool.Region = sc.GraphRegion;
        tool.BeginDateTime = 1;
        tool.BeginValue = vpos;
        tool.UseRelativeVerticalValues = 1;
        tool.Color = color;
        tool.FontSize = Text.LineWidth > 0 ? Text.LineWidth : 14;
        tool.FontBold = 1;
        tool.Text = line_text;
        tool.AddAsUserDrawnDrawing = 0;
        tool.DrawUnderneathMainGraph = 0;
        sc.UseTool(tool);
    };
    const COLORREF base = Text.PrimaryColor;
    const COLORREF red = RGB(248, 113, 113);
    const COLORREF green = RGB(52, 211, 153);

    SCString line1;
    if (target > 0)
        line1.Format("PROP day %+.0f / -%.0f | trail room %.0f | tgt %.0f%%",
            day_pl, daily_limit, trail_limit - (peak - live),
            (live - opening) / target * 100.0);
    else
        line1.Format("PROP day %+.0f / -%.0f | trail room %.0f",
            day_pl, daily_limit, trail_limit - (peak - live));
    draw(kDrawDay, 12, line1, halt_day || halt_trail ? red : base);

    if (InShowSize.GetYesNo() != 0) {
        SCString line2;
        line2.Format("size %d @ %dt stop | %s %s",
            size, InStopTicks.GetInt(),
            dt, in_session ? "TRADE" : "STANDBY");
        draw(kDrawSize, 18, line2, in_session ? green : base);
    } else {
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, kDrawSize);
    }
    if (halt_day) {
        SCString halt;
        halt.Format("HALT - daily limit (%.0f)", day_pl);
        draw(kDrawHaltDay, 30, halt, red);
    } else {
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, kDrawHaltDay);
    }
    if (halt_trail) {
        SCString halt;
        halt.Format("HALT - trailing (peak %.0f live %.0f)", (double)peak, live);
        draw(kDrawHaltTrail, 36, halt, red);
    } else {
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, kDrawHaltTrail);
    }
}
