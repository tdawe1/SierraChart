#pragma once
// TA_Inputs.h — sc.Input index constants for TrendArchitect
//
// ⚠ INDICES MUST BE CONTIGUOUS (0..N, no gaps). Observed 2026-07-03: Sierra's
// Study Settings input list TRUNCATES at a long run of unnamed indices —
// with the old sparse layout (14 unnamed slots at 26-39) the dialog ended at
// "Dynamic Direction Color" and everything from "Candle Type" onward was
// invisible, even though it was in the DLL. Single-slot gaps displayed fine,
// which masked the problem. Repacked contiguously 0-89 (v9.3.1).
// ⚠ HARD LIMIT: sc.Input[] has SC_INPUTS_AVAILABLE = 128 elements (valid
// indices 0-127). Writing higher indices is out-of-bounds UB.
// ⚠ Renumbering invalidates stored per-instance input values — users MUST
// remove and re-add the study after installing a build that changes indices.

// ── Section 0: Mode + Visual Components (displayed In:1-In:9) ────────────────
#define IN_SIM_MODE             0   // string: "Simple" | "Advanced"
#define IN_BG_MODE              1   // string: "Dark Background" | "Light Background"
#define IN_DARK_THEME           2   // string: one of 16 dark theme names
#define IN_LIGHT_THEME          3   // string: one of 16 light theme names
#define IN_INFO_ENABLE          4   // bool
#define IN_INFO_LOCATION        5   // string: Top Left | Middle Left | Bottom Left | Middle Right | Bottom Right
#define IN_PRISM_SIG_OFFSET     6   // float [0.0, 2.0], default 0.9 (Pine maxval 2.0)
#define IN_GLOW_TC_ENABLE       7   // bool, default true
#define IN_PRISM_SIG_SIZE       8   // string: "Small" | "Normal", default Small

// ── Main tool enables (In:10-In:18) ──────────────────────────────────────────
#define IN_CMA_ENABLE           9   // bool MA Ribbon
#define IN_SC_ENABLE            10  // bool Super Channel
#define IN_ALT_ENABLE           11  // bool Indicator Candles
#define IN_TC_ENABLE            12  // bool Trend Cloud
#define IN_PRISM_ENABLE         13  // bool PRISM Signals
#define IN_FORECAST_ENABLE      14  // bool Boundary Forecast
#define IN_TRG_ENABLE           15  // bool Trend Regime Gate
#define IN_AO_ENABLE            16  // bool Auto-Optimizer
#define IN_CANDLE_COL_ENABLE    17  // bool Candle Coloring

// ── MA Ribbon (In:19-In:24) ──────────────────────────────────────────────────
#define IN_CMA_LENGTH           18  // int, default 20
#define IN_CMA_OFFSET1          19  // float, default 0.85
#define IN_CMA_OFFSET2          20  // float, default 0.77
#define IN_CMA_SIGMA            21  // float, default 6.0
#define IN_CMA_BICOLOR          22  // bool "Bi-Color Ribbon Lines", default false
#define IN_CMA_DIR_COLOR        23  // bool "Dynamic Direction Color", default true

// (Super Channel has no tuning inputs — Pine hardcodes every SC parameter)

// ── Candle Types (In:25-In:28) ───────────────────────────────────────────────
#define IN_CANDLE_TYPE          24  // string: Regular | Heikin Ashi | R-Squared Adaptive | LinReg Heikin Ashi | LinReg Candles
#define IN_CVD_ENABLE           25  // bool CVD border highlighting
#define IN_CVD_THRESHOLD        26  // float percentile threshold, default 95.0
#define IN_LINREG_LEN           27  // int LinReg length, default 10

// ── Candle Coloring (In:29) ──────────────────────────────────────────────────
#define IN_CANDLE_COL_MODE      28  // string: MA Ribbon | Trend Regime | Dual Confirmation | Adaptive Impulse | Heikin Ashi | Trend Cloud Base

// ── Trend Cloud (In:30-In:31; KAMA base/multiplier are Pine hardcodes) ───────
#define IN_TC_BODY_ENABLE       29  // bool "Enable Trend Cloud Body" (fill), default true
#define IN_TC_SLOPE_COLOR       30  // bool "Color Base by Trend Direction", default true

// ── Trend Regime Gate (In:32-In:37) ──────────────────────────────────────────
#define IN_TRG_VOTES_REQ        31  // int votes required (1–3), default 2
#define IN_TRG_KAMA_THRESH      32  // float KAMA alignment threshold %, default 62
#define IN_TRG_HURST_THRESH     33  // float Hurst threshold, default 50
#define IN_TRG_HURST_LEN        34  // int Hurst lookback, default 100
#define IN_TRG_HURST_FREQ       35  // int Hurst update frequency (bars), default 5  [ENHANCEMENT]
#define IN_TRG_ACCEL_SMOOTH     36  // int acceleration EMA smoothing, default 3

// ── PRISM Signals (In:38-In:50) ──────────────────────────────────────────────
#define IN_PRISM_STRUCT_LOCK    37  // bool "Structure Lock", default true
#define IN_PRISM_BQ_ENABLE      38  // bool "Bar Quality", default true
#define IN_PRISM_MQ_ENABLE      39  // bool "Quality Gate" (marginal dots), default true
#define IN_PRISM_ADAPTIVE       40  // bool "PRISM Adaptive" timeframe scaling, default true
#define IN_PRISM_BASE_LEN       41  // int polynomial lookback base, default 40
#define IN_PRISM_ALPHA_FACTOR   42  // float Alpha Rail factor, default 0.2
#define IN_PRISM_SIGMA_FACTOR   43  // float Sigma Rail factor, default 0.5
#define IN_PRISM_BAR_QUAL_MIN   44  // float body/range min ratio, default 0.30
#define IN_PRISM_BAR_QUAL_WAIT  45  // int max wait bars for quality, default 3
#define IN_PRISM_ER_THRESH      46  // float ER threshold for Quality Gate, default 0.2
#define IN_PRISM_KAMA_SLOPE_MIN 47  // float KAMA slope min for Quality Gate, default 0.03
#define IN_PRISM_ALPHA_PERIOD   48  // int Alpha Rail ATR period, default 10
#define IN_PRISM_SIGMA_PERIOD   49  // int Sigma Rail ATR period, default 20

// ── Auto-Optimizer (In:51-In:57; AO base comes from PRISM Base Lookback) ─────
#define IN_AO_SPREAD_PCT        50  // float spread percentage, default 50
#define IN_AO_NOISE_SUPPRESS    51  // bool "Noise Suppression" (PRISM NS), default false
#define IN_AO_TIER1             52  // float Tier 1 ATR — Weak Win, default 0.75
#define IN_AO_TIER2             53  // float Tier 2 ATR — Solid Win, default 1.5
#define IN_AO_TIER3             54  // float Tier 3 ATR — Strong Win, default 2.5
#define IN_AO_MAX_BARS          55  // int Max Bars to Resolve, default 7
#define IN_AO_LOOKBACK          56  // int Signal Lookback, default 20

// ── Boundary Forecast (In:58-In:62) ──────────────────────────────────────────
#define IN_FC_BARS              57  // int forecast horizon bars (1–20), default 10
#define IN_FC_REG_LEN           58  // int regression lookback, default 25
#define IN_FC_MODE              59  // string: "Regression" | "Slope Extension", default Slope Extension
#define IN_FC_SHOW_SC           60  // bool "Show Projected SC Bands", default true
#define IN_FC_SHOW_TC           61  // bool "Show Projected TC Bands", default true

// ── Section 9: v9.3 Signal Enhancements (In:63-In:90) [no Pine equivalent] ───
// Everything here is layered ON TOP of the TV-parity baseline. Defaults marked
// (parity) leave behavior byte-identical to the Pine source when unchanged.
#define IN_SIG_CONFIRM_MODE     62  // string: "Live (TradingView parity)" | "Bar Close (confirmed)", default Live (parity)
#define IN_OF_TRUE_DELTA        63  // bool: real bid/ask aggressor volume replaces the geometric CVD proxy (auto-fallback), default Yes
#define IN_OF_AGREE_GATE        64  // bool: full arrows need signal-side real delta within the window, default Yes
#define IN_OF_AGREE_BARS        65  // int 1-5: delta agreement window (bars), default 2
#define IN_OF_DIV_VETO          66  // bool: windowed cumulative-delta divergence demotes full arrows, default No
#define IN_OF_DIV_LOOKBACK      67  // int 10-50: divergence window (bars), default 20
#define IN_OF_ABSORB_VETO       68  // bool: intrabar delta-retracement (absorption) demotes full arrows, default No
#define IN_OF_ABSORB_RETRACE    69  // float 0.30-0.90: absorption retrace fraction, default 0.60
#define IN_PRISM_GATE_STRICT    70  // bool: chop OR-mode + release re-validation + elevation quorum(2), default No (parity)
#define IN_PRISM_FLIP_GUARD     71  // int 0-20: opposite-signal cooldown bars (0 = off, parity), default 0
#define IN_ADAPT_TICK_EST       72  // bool: estimate bar seconds on tick/volume/range charts for PRISM Adaptive, default Yes
#define IN_ER_ADAPT_ENABLE      73  // bool: adaptive ER percentile tightening of the Quality Gate, default No (parity)
#define IN_ER_ADAPT_LOOKBACK    74  // int 50-1000: ER percentile lookback, default 250
#define IN_ER_ADAPT_PCT         75  // float 5-95: ER rank below this percentile = chop, default 40
#define IN_TRG_CAL_ENABLE       76  // bool: calibrated Hurst threshold + accel deadband votes, default No (parity)
#define IN_TRG_HURST_CAL        77  // float 0.50-0.95: calibrated Hurst vote threshold, default 0.72
#define IN_TRG_ACCEL_DB         78  // float 0-0.05: accel vote deadband, default 0.005
#define IN_RRG_ENABLE           79  // bool: Range Regime Gate (CHOP+ADX+ATR%tile 2-of-3), default No
#define IN_RRG_CHOP_LEN         80  // int: Choppiness Index length, default 14
#define IN_RRG_CHOP_THRESH      81  // float: CHOP above this = range vote, default 61.8
#define IN_RRG_ADX_LEN          82  // int: Wilder ADX length, default 14
#define IN_RRG_ADX_FLOOR        83  // float: ADX below this = range vote, default 20.0
#define IN_RRG_ATRP_LOOKBACK    84  // int: ATR percentile lookback, default 200
#define IN_RRG_ATRP_FLOOR       85  // float: ATR %tile below this = range vote, default 25.0
#define IN_AO_STOP_ATR          86  // float: AO adverse-excursion stop in ATR (0 = off, parity), default 0
#define IN_SIGLOG_ENABLE        87  // bool: log resolved signals to CSV, default Yes
#define IN_SIGLOG_HORIZON       88  // int: resolution window bars (0 = follow AO Max Bars), default 0
#define IN_SIGLOG_INCLUDE_MQ    89  // bool: log marginal dots too (control group), default Yes

// ── Persistent state ──────────────────────────────────────────────────────────
// All scalar recurrence state (EMAs, Kalman filters, KAMA k100, SuperTrend
// ratchets, gate/pending ints, AO score deques) lives in the TAState struct —
// see include/TA_State.h — reached through a single persistent pointer
// (PERSIST_P_TA_STATE). Individual GetPersistentInt/Float/Double slots are no
// longer used; do not add new ones — extend TARecurrence instead so the state
// participates in commit/rollback and full-recalc resets automatically.
