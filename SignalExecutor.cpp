#include "sierrachart.h"

SCDLLName("Signal Executor DLL")

// SignalExecutor: trade any indicator's trigger subgraphs with real orders,
// so the built-in replay/bar backtester and live auto-trading run the same
// logic the headless backtester replays via signal_replay.
//
// Wiring (same inputs as BacktestExporter, so certification is exact):
//   [Signal] Long/Short study subgraph, twice (long leg, short leg).
//     Point at any trigger output -- e.g. Orion "Trigger Long/Short".
//     A nonzero subgraph value on a CLOSED bar = 1 entry signal.
//   Trade >> Auto Trading Enabled (Global + Chart) must be on; replay or
//   bar backtest then fills the market orders. "Send Live" stays No until
//   a replay run matches the headless trades.csv within tolerance.
//
// Semantics mirror the headless engine defaults: closed-bar signals only
// (sc.GetBarHasClosedStatus), one position (Max Position), flatten on the
// opposite signal (no auto-reverse), optional attached stop/target in
// ticks. Known delta vs headless: replay fills the entry at ~bar close,
// headless fills at next open -- live trading matches headless here.
// Markers: Subgraph 0/1 flag bars where a long/short entry fired (wire
// them into the exporter to diff SC fills against headless trades.csv).
//
// Inputs are append-only once shipped -- never insert or reorder.

SCSFExport scsf_SignalExecutor(SCStudyInterfaceRef sc)
{
    SCInputRef InEnabled = sc.Input[0];
    SCInputRef InLongSrc = sc.Input[1];
    SCInputRef InShortSrc = sc.Input[2];
    SCInputRef InAllowLong = sc.Input[3];
    SCInputRef InAllowShort = sc.Input[4];
    SCInputRef InExitOpposite = sc.Input[5];
    SCInputRef InStopTicks = sc.Input[6];
    SCInputRef InTargetTicks = sc.Input[7];
    SCInputRef InQuantity = sc.Input[8];
    SCInputRef InMaxPosition = sc.Input[9];
    SCInputRef InSendLive = sc.Input[10];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Signal Executor";
        sc.StudyDescription = "Market entries from indicator trigger subgraphs (closed-bar), opposite-signal exit, attached stop/target. Sim default.";
        sc.GraphRegion = 0;
        sc.AutoLoop = 1;
        sc.FreeDLL = 0;

        InEnabled.Name = "Enabled";
        InEnabled.SetYesNo(1);

        InLongSrc.Name = "[Signal] Long study subgraph";
        InLongSrc.SetStudySubgraphValues(0, 0);

        InShortSrc.Name = "[Signal] Short study subgraph";
        InShortSrc.SetStudySubgraphValues(0, 0);

        InAllowLong.Name = "Allow Long Entries";
        InAllowLong.SetYesNo(1);

        InAllowShort.Name = "Allow Short Entries";
        InAllowShort.SetYesNo(1);

        InExitOpposite.Name = "Exit On Opposite Signal";
        InExitOpposite.SetYesNo(1);

        InStopTicks.Name = "Attached Stop (ticks, 0=off)";
        InStopTicks.SetInt(0);
        InStopTicks.SetIntLimits(0, 1000);

        InTargetTicks.Name = "Attached Target (ticks, 0=off)";
        InTargetTicks.SetInt(0);
        InTargetTicks.SetIntLimits(0, 1000);

        InQuantity.Name = "Order Quantity";
        InQuantity.SetInt(1);
        InQuantity.SetIntLimits(1, 100);

        InMaxPosition.Name = "Max Position";
        InMaxPosition.SetInt(1);
        InMaxPosition.SetIntLimits(1, 100);

        InSendLive.Name = "Send Live To Broker (No = sim/replay only)";
        InSendLive.SetYesNo(0);
        sc.Subgraph[0].Name = "Long Entry";
        sc.Subgraph[0].DrawStyle = DRAWSTYLE_POINT;
        sc.Subgraph[0].LineWidth = 4;
        sc.Subgraph[1].Name = "Short Entry";
        sc.Subgraph[1].DrawStyle = DRAWSTYLE_POINT;
        sc.Subgraph[1].LineWidth = 4;
        return;
    }

    sc.SendOrdersToTradeService = InSendLive.GetYesNo() != 0;
    sc.MaximumPositionAllowed = InMaxPosition.GetInt();
    sc.AllowMultipleEntriesInSameDirection = false;
    sc.SupportReversals = true;

    if (!InEnabled.GetYesNo())
        return;

    const int i = sc.Index;
    if (i < 1 || i != sc.ArraySize - 1)
        return; // latest bar only: orders act on live/formed data, never history
    if (sc.GetBarHasClosedStatus(i) != BHCS_BAR_HAS_CLOSED)
        return; // closed-bar semantics = headless signal_replay

    int& lastActionBar = sc.GetPersistentInt(0);
    if (lastActionBar == i)
        return; // one action per bar

    SCFloatArray longArr;
    SCFloatArray shortArr;
    const bool haveLong = InLongSrc.GetStudyID() != 0 &&
        sc.GetStudyArrayUsingID(InLongSrc.GetStudyID(), InLongSrc.GetSubgraphIndex(), longArr) != 0;
    const bool haveShort = InShortSrc.GetStudyID() != 0 &&
        sc.GetStudyArrayUsingID(InShortSrc.GetStudyID(), InShortSrc.GetSubgraphIndex(), shortArr) != 0;
    const bool longSig = haveLong && longArr[i] != 0.0f;
    const bool shortSig = haveShort && shortArr[i] != 0.0f;
    if (!longSig && !shortSig)
        return;

    s_SCPositionData pos;
    sc.GetTradePosition(pos);

    const int stopTicks = InStopTicks.GetInt();
    const int targetTicks = InTargetTicks.GetInt();
    const double tick = sc.TickSize > 0.0 ? sc.TickSize : 0.25;
    const double ref = sc.Close[i];

    // Flatten on the opposite signal first (exit_on_opposite, no reverse).
    if (InExitOpposite.GetYesNo() && pos.PositionQuantity != 0)
    {
        const int qty = abs(pos.PositionQuantity);
        const bool isLong = pos.PositionQuantity > 0;
        if ((isLong && shortSig) || (!isLong && longSig))
        {
            s_SCNewOrder exitOrder{};
            exitOrder.OrderQuantity = qty;
            exitOrder.OrderType = SCT_ORDERTYPE_MARKET;
            exitOrder.TimeInForce = SCT_TIF_GOOD_TILL_CANCELED;
            const int r = static_cast<int>(isLong ? sc.SellEntry(exitOrder) : sc.BuyEntry(exitOrder));
            SCString msg;
            msg.Format("SignalExecutor: flatten %d @ bar %d (%d)", qty, i, r);
            sc.AddMessageToLog(msg.GetChars(), 0);
            lastActionBar = i;
            return;
        }
    }

    if (pos.PositionQuantity != 0)
        return; // one position at a time = headless engine

    // Long wins ties, exactly like signal_replay (if-first); the allow
    // gates below then match the engine entry gate, so a disallowed side
    // skips the bar instead of trading (never flips to the other side).
    const bool allowLong = InAllowLong.GetYesNo() != 0;
    const bool allowShort = InAllowShort.GetYesNo() != 0;
    int dir = 0;
    if (longSig)
        dir = 1;
    else if (shortSig)
        dir = -1;
    if ((dir > 0 && !allowLong) || (dir < 0 && !allowShort))
        return;
    s_SCNewOrder order{};
    order.OrderQuantity = InQuantity.GetInt();
    order.OrderType = SCT_ORDERTYPE_MARKET;
    order.TimeInForce = SCT_TIF_GOOD_TILL_CANCELED;
    if (targetTicks > 0)
    {
        order.AttachedOrderTarget1Type = SCT_ORDERTYPE_LIMIT;
        order.Target1Price = ref + dir * targetTicks * tick;
    }
    if (stopTicks > 0)
    {
        order.AttachedOrderStop1Type = SCT_ORDERTYPE_STOP;
        order.Stop1Price = ref - dir * stopTicks * tick;
    }
    const int r = static_cast<int>(dir > 0 ? sc.BuyEntry(order) : sc.SellEntry(order));
    SCString msg;
    msg.Format("SignalExecutor: %s %d @ bar %d (%d)", dir > 0 ? "BUY" : "SELL",
        order.OrderQuantity, i, r);
    sc.AddMessageToLog(msg.GetChars(), 0);
    sc.Subgraph[dir > 0 ? 0 : 1][i] = 1.0f;
    lastActionBar = i;
}
