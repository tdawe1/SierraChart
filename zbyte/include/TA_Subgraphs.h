#pragma once
// TA_Subgraphs.h — sc.Subgraph index constants for TrendArchitect
//
// LAYOUT v9.2 (2026-07-03): one-time +2 renumber to make room for the
// native-bar mask at the BOTTOM of the z-order. Sierra draws subgraphs in
// ASCENDING index order (higher index = drawn on top), so the indices encode
// Pine's plot z-order exactly:
//   bar mask (bottom) → glow → TC fill → TC lines → SC fill → SC lines →
//   ribbon fill → ribbon lines → candles → signal markers (top). Text/tool
//   drawings (labels, info panel, forecast) always render above subgraphs.
//
// ⚠ Existing chart instances MUST be re-added (or Set Defaults pressed) after
// installing this build — Sierra keys per-instance subgraph settings by index
// and does not reapply SetDefaults on DLL replacement. The idx==0 runtime
// block re-enforces every layout-critical DrawStyle/width/DrawZeros so the
// chart renders correctly even before that is done (names/colors in the
// Subgraphs list stay stale until re-add).
// After this build the indices are FROZEN again — add new subgraphs at the
// END only.
//
// Pairing rules: FILL_RECTANGLE_TOP pairs with FILL_RECTANGLE_BOTTOM on the
// next index; TRANSPARENT_FILL_TOP pairs with TRANSPARENT_FILL_BOTTOM on the
// next index; BAR_TOP pairs with BAR_BOTTOM on the next index (wick);
// CANDLESTICK_BODY_OPEN pairs with CANDLESTICK_BODY_CLOSE on the next (body).

// ── Layer 0: Native-bar mask ──────────────────────────────────────────────────
// Per-bar FILL_RECTANGLE pair in the theme background color spanning the raw
// bar's High..Low. Active only while the study draws its own candles
// (Indicator Candles or Candle Coloring on): it blanks the chart's native
// bars so the synthetic candles render clean — the intentional replacement
// for the accidental blanking the v9.0 opaque fills provided. Bottom of the
// z-order: glow, fills, lines and candles all draw on top of it.
#define SG_BAR_MASK_TOP     0
#define SG_BAR_MASK_BOT     1

// ── Layer 1a: TC base glow stack (Pine: 4 plots, widths 10/20/30/45,
//    transparencies 80/90/95/98 — widest/faintest drawn first) ──────────────
#define SG_TC_GLOW4         2   // widest, faintest
#define SG_TC_GLOW3         3
#define SG_TC_GLOW2         4
#define SG_TC_GLOW1         5   // narrowest, strongest

// ── Layer 1b: Trend Cloud ─────────────────────────────────────────────────────
// Pine declares tc_p_base BEFORE tc_p_top (TA9:1317-1322), so the TOP line
// draws over the base — base at the lower index here.
#define SG_TC_FILL_TOP      6   // fill pair (cloud top boundary)
#define SG_TC_FILL_BOT      7   // fill pair (base boundary)
#define SG_TC_BASE          8   // DRAWSTYLE_LINE width 3
#define SG_TC_TOP           9   // DRAWSTYLE_LINE width 2 (Pine: solid, NEUT 20% transp)

// ── Layer 2: Super Channel ────────────────────────────────────────────────────
#define SG_SC_FILL_TOP      10
#define SG_SC_FILL_BOT      11
#define SG_SC_TOP           12  // DRAWSTYLE_LINE width 2
#define SG_SC_BOT           13  // DRAWSTYLE_LINE width 2

// ── Layer 3: MA Ribbon ────────────────────────────────────────────────────────
#define SG_RIBBON_FILL_TOP  14
#define SG_RIBBON_FILL_BOT  15
#define SG_ALMA1            16  // DRAWSTYLE_LINE width 2 (Arrays[0] = always-on history)
#define SG_ALMA2            17  // DRAWSTYLE_LINE width 2 (Arrays[0] = always-on history)

// ── Layer 4: Custom Candles ───────────────────────────────────────────────────
#define SG_CANDLE_HIGH      18  // DRAWSTYLE_BAR_TOP    — wick top
#define SG_CANDLE_LOW       19  // DRAWSTYLE_BAR_BOTTOM — wick bottom
#define SG_CANDLE_OPEN      20  // DRAWSTYLE_CANDLESTICK_BODY_OPEN
#define SG_CANDLE_CLOSE     21  // DRAWSTYLE_CANDLESTICK_BODY_CLOSE
#define SG_CVD_BORDER       22  // reserved overlay (hidden)

// ── Layer 5: PRISM signal markers (top) ──────────────────────────────────────
#define SG_PRISM_BULL       23  // DRAWSTYLE_ARROW_UP
#define SG_PRISM_BEAR       24  // DRAWSTYLE_ARROW_DOWN
#define SG_PRISM_MQ_BULL    25  // DRAWSTYLE_POINT
#define SG_PRISM_MQ_BEAR    26  // DRAWSTYLE_POINT

// ── Raw internal values (hidden, for external study reference) [ENHANCEMENT] ─
#define SG_RAW_ER           27  // Efficiency Ratio
#define SG_RAW_CCO          28  // CCO oscillator
#define SG_RAW_KAMA_ALIGN   29  // KAMA alignment % (Arrays[0]/[1] = suppress-flag history)
#define SG_RAW_AO_EFF_LEN   30  // AO effective lookback

// ── KAMA fan reference outputs (hidden) [ENHANCEMENT] ────────────────────────
#define SG_TC_K10           31
#define SG_TC_K25           32
#define SG_TC_K50           33
#define SG_TC_K75           34

// ── Hidden storage subgraphs (DRAWSTYLE_IGNORE, DrawZeros=1) ─────────────────
// TrendCloud KAMA fan: STORE_A main=k05 Arrays[0..4]=k10..k30;
//                      STORE_B main=k35 Arrays[0..4]=k40..k60;
//                      STORE_C main=k65 Arrays[0..4]=k70..k90
//                      (k100 lives in TARecurrence::tc_k100)
#define SG_KAMA_STORE_A     35
#define SG_KAMA_STORE_B     36
#define SG_KAMA_STORE_C     37
// STORE_D: main = StochRSI-K history (info panel); Arrays[0]=SC cco_bot_raw,
//          Arrays[1..2]=KC raw, Arrays[3..4]=BB raw
#define SG_KAMA_STORE_D     38
#define SG_OHLC4_STORE      39  // ohlc4 history for the KAMA fan ER windows
#define SG_FHA_CLOSE_PREV   40  // FHA raw HA close history
#define SG_LRHA_CLOSE_PREV  41  // LinReg-HA raw close history
#define SG_CVD_STORE        42  // main = cvd_agg history; Arrays[0] = cvd_rank history
#define SG_SC_CCO           43  // main = sc_cco; Arrays[0]=RSI, [1]=stoch raw, [2]=CCI, [3]=raw CCO, [4]=cco_top_raw
#define SG_AI_KAMA_PREV     44  // Adaptive Impulse KAMA history
#define SG_AI_MACD          45  // Adaptive Impulse ALMA-MACD history
#define SG_AI_HIST          46  // Adaptive Impulse histogram history
#define SG_PRISM_ST1_LINE   47  // PRISM Alpha rail line (info panel)
#define SG_PRISM_ST2_LINE   48  // PRISM Sigma rail line (info panel)
#define SG_PRISM_POLY       49  // main = poly value; Arrays[0] = fast-KAMA(20) history
#define SG_RRG_STORE        50  // main = ATR14 history (RRG percentile) [v9.3]

// ── v9.3 extra-Arrays slot allocation (12 Arrays per subgraph; keep this map
//    current — slots are shared by convention, collisions corrupt silently) ──
//   SG_RAW_ER.Arrays[0]  = fk_norm (normalized fast-KAMA slope)
//   SG_RAW_ER.Arrays[1]  = bq_ratio (signal-bar body/range)
//   SG_RAW_ER.Arrays[2]  = chop flag of the fired direction (0/1)
//   SG_RAW_ER.Arrays[3]  = sig_path (0 immediate, 1 SL release, 2 BQ release)
//   SG_RAW_ER.Arrays[4]  = elev_mask (bit0..3 = e1..e4)
//   SG_RAW_CCO.Arrays[0] = ER percentile rank (REGIME-4)
//   SG_RAW_CCO.Arrays[1] = RRG votes, [2] = CHOP, [3] = ADX, [4] = ATR %tile
//   SG_RAW_KAMA_ALIGN.Arrays[2] = TRG bull votes, [3] = bear votes, [4] = Hurst
//   (baseline: SG_RAW_KAMA_ALIGN.Arrays[0..1] = suppress-flag history)
//   SG_CVD_STORE.Arrays[1] = signed per-bar delta (real or proxy)
//   SG_CVD_STORE.Arrays[2] = delta-is-real flag (0/1)
//   SG_CVD_STORE.Arrays[3] = windowed L-bar cumulative delta (OF-3)
//   (baseline: SG_CVD_STORE.Arrays[0] = cvd_rank history — unchanged)

// ── Total ─────────────────────────────────────────────────────────────────────
// Highest index used: 50 (ACSIL supports up to 60 subgraphs)
#define SG_FIRST_HIDDEN_STORE  35
#define SG_LAST_HIDDEN_STORE   50
