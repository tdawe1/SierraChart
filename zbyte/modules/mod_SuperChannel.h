#pragma once
// mod_SuperChannel.h — Section 4: Super Channel
// Hybrid channel: CCO (50%) + Keltner (40%) + Bollinger (10%).
// CCO = weighted blend of StochRSI-K, MFI, and CCI PercentRank.

#include "../include/TA_Subgraphs.h"
#include "../include/TA_Inputs.h"
#include "../include/TA_Algorithms.h"
#include "../include/TA_ColorThemes.h"
#include "../include/TA_State.h"

// Constants matching Pine hardcodes
static const int   SC_SMOOTHING     = 5;
static const int   SC_MFI_LEN       = 14;
static const int   SC_CCI_LEN       = 20;
static const int   SC_PCTRANK_LB    = 100;
static const int   SC_CONSENSUS_SMO = 3;
static const int   SC_KC_EMA_LEN    = 20;
static const int   SC_KC_ATR_LEN    = 10;
static const float SC_KC_MULT       = 2.0f;
static const int   SC_BB_LEN        = 20;
static const float SC_BB_MULT       = 2.0f;
static const float SC_CCO_RATIO     = 0.50f;  // 1 - 0.40 - 0.10
static const float SC_KELTNER_RATIO = 0.40f;
static const float SC_BB_RATIO      = 0.10f;
static const float SC_ATR_DIST      = 6.0f;
static const float SC_CCO_LO        = 15.0f;
static const float SC_CCO_HI        = 85.0f;
static const float SC_HI_THRESH     = 75.0f;
static const float SC_LO_THRESH     = 25.0f;

struct SuperChannelResult {
    double sc_top;
    double sc_bot;
    double sc_cco;       // raw CCO (0-100) for alerts and info panel
    bool   is_overbought;
    bool   is_oversold;
    COLORREF top_color;
    COLORREF bot_color;
};

inline SuperChannelResult compute_SuperChannel(
    SCStudyInterfaceRef sc,
    TAState& tas,
    double atr14,
    const ThemeProfile& theme)
{
    SuperChannelResult out = {};
    int idx = sc.Index;
    TARecurrence& st = tas.working;

    // ── StochRSI K ───────────────────────────────────────────────────────────
    // RSI(close, 14) then Stoch(rsi, 14) then SMA(stoch_raw, 3)

    // Wilder RSI(14), matching Pine ta.rsi: RMA-smoothed gains/losses seeded
    // with the SMA of the first 14 changes (Pine shows na before bar 14; we
    // return a provisional value from the partial sums instead).
    double rsi14 = 50.0;
    if (idx >= 1) {
        double ch = (double)sc.Close[idx] - (double)sc.Close[idx - 1];
        double u = std::max(ch, 0.0);
        double d = std::max(-ch, 0.0);
        if (idx < 14) {
            // warm-up: rsi_avg_gain/loss hold running SUMS of the changes
            st.rsi_avg_gain += u;
            st.rsi_avg_loss += d;
            double g = st.rsi_avg_gain / (double)idx;
            double l = st.rsi_avg_loss / (double)idx;
            rsi14 = (l > 0.0) ? (100.0 - 100.0 / (1.0 + g / l)) : (g > 0.0 ? 100.0 : 50.0);
        } else {
            if (idx == 14) {
                // seed: SMA of the first 14 changes (sums collected bars 1..13, + this bar)
                st.rsi_avg_gain = (st.rsi_avg_gain + u) / 14.0;
                st.rsi_avg_loss = (st.rsi_avg_loss + d) / 14.0;
            } else {
                st.rsi_avg_gain = (st.rsi_avg_gain * 13.0 + u) / 14.0;
                st.rsi_avg_loss = (st.rsi_avg_loss * 13.0 + d) / 14.0;
            }
            rsi14 = (st.rsi_avg_loss > 0.0)
                ? (100.0 - 100.0 / (1.0 + st.rsi_avg_gain / st.rsi_avg_loss))
                : (st.rsi_avg_gain > 0.0 ? 100.0 : 50.0);
        }
    }

    // Stoch of RSI: need rsi history — store in a hidden subgraph (SG_SC_CCO extra Arrays[0])
    sc.Subgraph[SG_SC_CCO].Arrays[0][idx] = (float)rsi14;

    double srsi_raw = 50.0;
    int stoch_len = 14;
    if (idx >= stoch_len) {
        double hi = -1e18, lo = 1e18;
        for (int i = 0; i < stoch_len; i++) {
            float v = sc.Subgraph[SG_SC_CCO].Arrays[0][idx - i];
            if (v > hi) hi = v;
            if (v < lo) lo = v;
        }
        srsi_raw = (hi > lo) ? (100.0 * (rsi14 - lo) / (hi - lo)) : 50.0;
    }

    // SMA of stoch_raw, period 3
    double srsi_k = srsi_raw;
    if (idx >= 2) {
        sc.Subgraph[SG_SC_CCO].Arrays[1][idx] = (float)srsi_raw;
        double s = 0.0;
        for (int i = 0; i < 3 && i <= idx; i++) s += (double)sc.Subgraph[SG_SC_CCO].Arrays[1][idx - i];
        srsi_k = s / std::min(3, idx + 1);
    } else {
        sc.Subgraph[SG_SC_CCO].Arrays[1][idx] = (float)srsi_raw;
    }

    // StochRSI-K history (info panel "Momentum State" needs SRSI_K[1])
    sc.Subgraph[SG_KAMA_STORE_D][idx] = (float)srsi_k;

    // ── MFI (Money Flow Index, len=14) ───────────────────────────────────────
    // hlc3 = (H+L+C)/3
    double mfi = 50.0;
    if (idx >= SC_MFI_LEN) {
        double pos_flow = 0.0, neg_flow = 0.0;
        double cur_tp = ((double)sc.High[idx] + sc.Low[idx] + sc.Close[idx]) / 3.0;
        for (int i = 1; i <= SC_MFI_LEN; i++) {
            double tp = ((double)sc.High[idx-i+1] + sc.Low[idx-i+1] + sc.Close[idx-i+1]) / 3.0;
            double tp_prev = ((double)sc.High[idx-i] + sc.Low[idx-i] + sc.Close[idx-i]) / 3.0;
            double vol = (double)sc.Volume[idx-i+1];
            double mf  = tp * vol;
            if (tp > tp_prev) pos_flow += mf;
            else if (tp < tp_prev) neg_flow += mf;
        }
        double unused = cur_tp;
        mfi = (neg_flow > 0.0) ? (100.0 - 100.0 / (1.0 + pos_flow / neg_flow))
                               : (pos_flow > 0.0 ? 100.0 : 50.0);
    }

    // ── CCI PercentRank ───────────────────────────────────────────────────────
    // CCI = (close - SMA20) / (0.015 * MeanDev) over 20 bars
    double cci_val = 0.0;
    if (idx >= SC_CCI_LEN - 1) {
        double sma = 0.0;
        for (int i = 0; i < SC_CCI_LEN; i++) sma += (double)sc.Close[idx - i];
        sma /= SC_CCI_LEN;
        double md = 0.0;
        for (int i = 0; i < SC_CCI_LEN; i++) md += std::abs((double)sc.Close[idx - i] - sma);
        md /= SC_CCI_LEN;
        cci_val = (md > 1e-10) ? ((sc.Close[idx] - sma) / (0.015 * md)) : 0.0;
    }

    // Store CCI for percent rank
    sc.Subgraph[SG_SC_CCO].Arrays[2][idx] = (float)cci_val;

    // Percent rank of CCI over 100 bars, counting <= like Pine ta.percentrank.
    // Divide by the ACTUAL window during warm-up (a partial count over the
    // full lookback understated the rank). Note: Pine's sc_cci_pc is na until
    // idx >= 119 (CCI first valid at 19 + 100-bar window) — bars below that
    // are an intentional na-avoidance approximation.
    double cci_pc = 50.0;
    if (idx >= 1) {
        int lb = std::min(SC_PCTRANK_LB, idx);
        int count = 0;
        for (int i = 1; i <= lb; i++) {
            if ((double)sc.Subgraph[SG_SC_CCO].Arrays[2][idx - i] <= cci_val) count++;
        }
        cci_pc = 100.0 * (double)count / (double)lb;
    }

    // ── CCO Composite ────────────────────────────────────────────────────────
    double sc_raw_cco = (srsi_k * 0.8 + mfi * 0.9 + cci_pc * 1.2) / (0.8 + 0.9 + 1.2);

    // SMA of sc_raw_cco over 3 bars
    sc.Subgraph[SG_SC_CCO].Arrays[3][idx] = (float)sc_raw_cco;
    double sc_cco = 0.0;
    for (int i = 0; i < SC_CONSENSUS_SMO && i <= idx; i++)
        sc_cco += (double)sc.Subgraph[SG_SC_CCO].Arrays[3][idx - i];
    sc_cco /= std::min(SC_CONSENSUS_SMO, idx + 1);

    sc.Subgraph[SG_SC_CCO][idx] = (float)sc_cco;

    // CCO normalized [0,1]
    double sc_norm_cco = std::max(0.0, std::min(1.0, (sc_cco - SC_CCO_LO) / (SC_CCO_HI - SC_CCO_LO)));

    // ── CCO bands ─────────────────────────────────────────────────────────────
    // SMA(hlc3 ± ATR_DIST * (1 - norm) * ATR14, SMOOTHING)
    double hlc3 = ((double)sc.High[idx] + sc.Low[idx] + sc.Close[idx]) / 3.0;
    double cco_top_raw = hlc3 + SC_ATR_DIST * (1.0 - sc_norm_cco) * atr14;
    double cco_bot_raw = hlc3 - SC_ATR_DIST * sc_norm_cco            * atr14;

    // Store raw CCO bands for SMA smoothing
    sc.Subgraph[SG_SC_CCO].Arrays[4][idx] = (float)cco_top_raw;
    // cco_bot_raw lives in SG_KAMA_STORE_D.Arrays[0] (dedicated hidden storage)
    sc.Subgraph[SG_KAMA_STORE_D].Arrays[0][idx] = (float)cco_bot_raw;

    double sc_top_cco = 0.0, sc_bot_cco = 0.0;
    for (int i = 0; i < SC_SMOOTHING && i <= idx; i++) {
        sc_top_cco += (double)sc.Subgraph[SG_SC_CCO].Arrays[4][idx - i];
        sc_bot_cco += (double)sc.Subgraph[SG_KAMA_STORE_D].Arrays[0][idx - i];
    }
    sc_top_cco /= std::min(SC_SMOOTHING, idx + 1);
    sc_bot_cco /= std::min(SC_SMOOTHING, idx + 1);

    // ── Keltner Channel ──────────────────────────────────────────────────────
    // EMA(close, 20) ± 2.0 * ATR(10)
    // Pine: ta.ema(close, 20) is src-seeded; ta.atr(10) is Wilder RMA (TA9:215-216)
    double kc_mid = st.kc_ema.step((double)sc.Close[idx], SC_KC_EMA_LEN);
    double kc_atr = ta_wilder_atr_step(sc, idx, SC_KC_ATR_LEN, st.kc_atr10);

    double kc_top_raw = kc_mid + SC_KC_MULT * kc_atr;
    double kc_bot_raw = kc_mid - SC_KC_MULT * kc_atr;

    // Store for SMA
    sc.Subgraph[SG_KAMA_STORE_D].Arrays[1][idx] = (float)kc_top_raw;
    sc.Subgraph[SG_KAMA_STORE_D].Arrays[2][idx] = (float)kc_bot_raw;

    double sc_top_kc = 0.0, sc_bot_kc = 0.0;
    for (int i = 0; i < SC_SMOOTHING && i <= idx; i++) {
        sc_top_kc += (double)sc.Subgraph[SG_KAMA_STORE_D].Arrays[1][idx - i];
        sc_bot_kc += (double)sc.Subgraph[SG_KAMA_STORE_D].Arrays[2][idx - i];
    }
    sc_top_kc /= std::min(SC_SMOOTHING, idx + 1);
    sc_bot_kc /= std::min(SC_SMOOTHING, idx + 1);

    // ── Bollinger Bands ───────────────────────────────────────────────────────
    // SMA(close,20) ± 2.0 * stdev(close,20)
    double bb_sma = 0.0;
    for (int i = 0; i < SC_BB_LEN && i <= idx; i++) bb_sma += (double)sc.Close[idx - i];
    bb_sma /= std::min(SC_BB_LEN, idx + 1);

    double bb_var = 0.0;
    for (int i = 0; i < SC_BB_LEN && i <= idx; i++) {
        double d = (double)sc.Close[idx - i] - bb_sma;
        bb_var += d * d;
    }
    double bb_dev = std::sqrt(bb_var / std::min(SC_BB_LEN, idx + 1));

    double bb_top_raw = bb_sma + SC_BB_MULT * bb_dev;
    double bb_bot_raw = bb_sma - SC_BB_MULT * bb_dev;

    sc.Subgraph[SG_KAMA_STORE_D].Arrays[3][idx] = (float)bb_top_raw;
    sc.Subgraph[SG_KAMA_STORE_D].Arrays[4][idx] = (float)bb_bot_raw;

    double sc_top_bb = 0.0, sc_bot_bb = 0.0;
    for (int i = 0; i < SC_SMOOTHING && i <= idx; i++) {
        sc_top_bb += (double)sc.Subgraph[SG_KAMA_STORE_D].Arrays[3][idx - i];
        sc_bot_bb += (double)sc.Subgraph[SG_KAMA_STORE_D].Arrays[4][idx - i];
    }
    sc_top_bb /= std::min(SC_SMOOTHING, idx + 1);
    sc_bot_bb /= std::min(SC_SMOOTHING, idx + 1);

    // ── Weighted Combination ──────────────────────────────────────────────────
    double sc_top = sc_top_cco * SC_CCO_RATIO + sc_top_kc * SC_KELTNER_RATIO + sc_top_bb * SC_BB_RATIO;
    double sc_bot = sc_bot_cco * SC_CCO_RATIO + sc_bot_kc * SC_KELTNER_RATIO + sc_bot_bb * SC_BB_RATIO;

    sc.Subgraph[SG_SC_TOP][idx] = (float)sc_top;
    sc.Subgraph[SG_SC_BOT][idx] = (float)sc_bot;

    out.sc_top = sc_top;
    out.sc_bot = sc_bot;
    out.sc_cco = sc_cco;
    out.is_overbought = (sc_cco >= SC_HI_THRESH);
    out.is_oversold   = (sc_cco <= SC_LO_THRESH);
    out.top_color = out.is_overbought ? theme.hilight : theme.bull;
    out.bot_color = out.is_oversold   ? theme.hilight : theme.bear;

    sc.Subgraph[SG_SC_TOP].DataColor[idx] = out.top_color;
    sc.Subgraph[SG_SC_BOT].DataColor[idx] = out.bot_color;

    // Channel fill (adjacent TRANSPARENT_FILL pair); Pine: NEUT at 95 transp.
    // theme_fill_dim + the study fill transparency keep the channel see-through
    // — the price bars and anything drawn below the study stay visible.
    sc.Subgraph[SG_SC_FILL_TOP][idx] = (float)sc_top;
    sc.Subgraph[SG_SC_FILL_BOT][idx] = (float)sc_bot;
    sc.Subgraph[SG_SC_FILL_TOP].DataColor[idx] = theme_fill_dim(theme.neutral, 5, theme.bg);
    sc.Subgraph[SG_SC_FILL_BOT].DataColor[idx] = theme_fill_dim(theme.neutral, 5, theme.bg);

    return out;
}
