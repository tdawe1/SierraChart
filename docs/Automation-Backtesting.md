# Sierra Chart reference: automating + backtesting

Distilled from the official docs (read 2026-09-12; links are canonical —
re-check them before relying on details, Sierra rewrites pages in place).
This file orients; the linked pages rule on anything disputed.

## Canonical pages

Automation:

- Automated Trading From an Advanced Custom Study —
  <https://www.sierrachart.com/index.php?page=doc/ACSILTrading.html>
- Auto Trade Management (`sc.SendOrdersToTradeService`, sim vs live) —
  <https://www.sierrachart.com/index.php?page=doc/AutoTradeManagment.php>
- Enabling/Disabling Automated Trading —
  <https://www.sierrachart.com/index.php?page=doc/EnablingDisablingAutomatedTrading.php>
- Example ACSIL Trading Systems —
  <https://www.sierrachart.com/index.php?page=doc/ACSIL_ExampleTradingSystems.html>
  (in-app copies: Analysis → Studies → Add Custom Study → Sierra Chart
  Custom Studies and Examples → Trading Example:*)

Backtesting:

- Auto Trade System Back Testing —
  <https://www.sierrachart.com/index.php?page=doc/Backtesting.php>
- Replaying Charts —
  <https://www.sierrachart.com/index.php?page=doc/ReplayChart.html>
- Trade Simulation (fills, bid/ask accuracy) —
  <https://www.sierrachart.com/index.php?page=doc/TradeSimulation.php>
- Trade Activity Log → Trade Statistics / Trades (results) —
  <https://www.sierrachart.com/index.php?page=doc/TradeActivityLog.php>

## Automating (ACSIL)

- Managed model: `sc.BuyEntry` / `sc.BuyExit` / `sc.SellEntry` /
  `sc.SellExit` examine position + working orders for the chart's symbol
  and account and only send when conditions are met. Nothing fires unless
  **Trade → Auto Trading Enabled - Global AND - Chart** are both on.
- Orders are **ignored on historical bars and during historical download**
  (return `SCT_SKIPPED_FULL_RECALC` /
  `SCT_SKIPPED_DOWNLOADING_HISTORICAL_DATA`). Only live updates and
  replay-added data trigger order processing — a replay starts with a
  recalc, so pre-replay bars never trade. Arrows on old bars are not fills.
- `sc.AllowOnlyOneTradePerBar` defaults to 1: one order per Order Action
  type per bar (excess returns `SCT_SKIPPED_ONLY_ONE_TRADE_PER_BAR`).
  Keep it unless backtests prove you need more.
- Sim vs live is two switches: **Trade → Trade Simulation Mode On** and
  `sc.SendOrdersToTradeService`. The docs recommend an input gating the
  latter (`sc.SendOrdersToTradeService = SendOrdersToService.GetYesNo();`)
  — this repo's convention (order sending defaults OFF, e.g. `TORobots`)
  follows that exactly. `sc.GlobalTradeSimulationIsOn` mirrors sim mode;
  sim positions are prefixed `[Sim]`.
- For bar-close logic use `sc.GetBarHasClosedStatus() ==
  BHCS_BAR_HAS_CLOSED` (with bar-based backtests this pins fills to the
  bar open); intrabar eval frequency in live follows the Chart Update
  Interval (not every tick — don't go below 50–100 ms).
- Inspect: `sc.GetOrderByIndex/ByOrderID`, `sc.GetOrderFillEntry/ArraySize`
  (reverse-iterate fills to reconstruct the current position),
  `sc.GetTradePosition*`, `sc.GetTradeStatisticsForSymbolV2`;
  flatten via `sc.FlattenAndCancelAllOrders` / `sc.FlattenPosition`.
  Ignored actions log their reason to **Trade → Trade Service Log**
  (plus Message Log with error-handling code) — check there first, using
  the `SCT_SKIPPED_*` / `SCTRADING_*` constants to decode returns.
- Multi-chart systems: enable Controlled Order Chart Updating, replay all
  referenced charts, and set overlay Data Copy Mode to earliest-value for
  non-time bars — otherwise referenced charts read finalized bars.

## Backtesting in Sierra

| | Bar Based | Replay (auto / manual) |
|---|---|---|
| Source | Loaded bar OHLC | Intraday file records (tick–1 min) |
| Charts | Historical + intraday | Intraday only |
| Speed / precision | Fast, coarse | Slow, precise |
| Command | Trade → Auto Trade System Bar Based Back Test | Trade → Auto Trade System Replay Back Test, or Replay Chart control panel |

- Bar-based: enable auto trading (global+chart), **File → Disconnect**,
  lower Chart Update Interval, run; studies calculate 4× per bar
  (O, then H/L ordered by bar direction, then C); fills land on or within
  1 tick of OHLC with the bar-start timestamp; only Fill entries hit the
  Trade Activity Log. Bar Processing Increment default 250, Esc interrupts.
- Replay auto clears the symbol's sim data and replays from the first bar
  (single chart, Skip Empty Records, Accurate Trading System Back Test
  Mode, speed 100000). Manual mode (slower, observable, required for
  multi-chart): Replay Chart control panel → Accurate mode → Single or All
  Charts in Chartbook → disable Use Start Date-Time → scroll back, Play.
  Multi-chart synchronized step default 60 s ≈ 25% of average bar length.
- Results: **Trade → Trade Activity Log → Trade Statistics** on the
  `[Sim]` symbol with the chart's trade account; **Trades** tab for the
  trade-by-trade log; **Trade → Show Order Fills** to see fills on chart
  (fill markers, not signal arrows, are ground truth). Separate runs by
  switching Trade Account first (fills get unique ms stamps).
- Consistency: same method + same data ⇒ identical bid/ask handling and
  identical results — drift means your code is nondeterministic (verify
  against a Sierra example system). Replay outranks bar-based on accuracy;
  actual historical bid/ask requires tick-by-tick storage **and** a replay
  backtest. Continuous-contract charts backtest fine (trades print under
  the current symbol at expired-contract prices). Replay never equals live.
- Performance, in order: ACSIL (not spreadsheet), bar-based + small Chart
  Update Interval, strip studies to isolate hogs, avoid Calculate At Every
  Tick/Trade, disable queue-position tracking, split multi-year histories
  across charts/accounts.

## How this repo maps onto it

- House signal studies are **signal-only** (no order calls) by design:
  ideas are validated offline via chart → `BacktestExporter.cpp` → CSV →
  `backtest run|sweep|compare` (`tools/sc.py`), which is fast,
  scriptable, and version-controlled — Sierra replay is the independent
  cross-check, not the iteration loop.
- `TORobots.cpp` (GoldBug) is the live path: order sending defaults OFF,
  max-loss/profit halt enforced — graduate it sim-first per Going from
  Simulation to Live, then confirm with a Sierra replay backtest before
  any real account touches it.
