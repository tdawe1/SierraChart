#pragma once
// mod_TrendRegimeGate.h — Section 6B: Trend Regime Gate
// Three independent voting measures:
//   1. KAMA fan alignment percentage
//   2. Hurst RS exponent (throttled every 5 bars)
//   3. TC base slope acceleration (EMA-3 of 2nd derivative)
// 2-of-3 voting to suppress counter-trend signals.

#include "../include/TA_Subgraphs.h"
#include "../include/TA_Inputs.h"
#include "../include/TA_Algorithms.h"
#include "../include/TA_State.h"
#include "mod_TrendCloud.h"

struct TrendRegimeResult {
    bool   suppress_sell;  // bull regime: suppress sells
    bool   suppress_buy;   // bear regime: suppress buys
    int    bull_votes;
    int    bear_votes;
    double ka_bull_pct;
    double ka_bear_pct;
    double hurst;
    double accel_smooth;
};

inline TrendRegimeResult compute_TrendRegimeGate(
    SCStudyInterfaceRef sc,
    TAState& tas,
    const TrendCloudResult& tc,
    double atr14,
    bool enabled,
    double ka_thresh,          // 0.62 default
    int hurst_len,             // 100 default
    double hurst_thresh,       // 0.50 default
    int hurst_freq,            // 5 default (update every N bars)
    int accel_smooth_period,   // 3 default
    int votes_required,        // 2 default
    bool cal_mode,             // [v9.3 REGIME-3] IN_TRG_CAL_ENABLE
    double hurst_cal_thresh,   // IN_TRG_HURST_CAL (0.72)
    double accel_deadband)     // IN_TRG_ACCEL_DB (0.005)
{
    TrendRegimeResult out = {};
    int idx = sc.Index;
    TARecurrence& st = tas.working;
    // Pine computes all three measures + votes unconditionally; `enabled`
    // gates only the suppress flags (TA9:550-551) — applied at the bottom.

    // ── Measure 1: KAMA Fan Alignment ────────────────────────────────────────
    int bull_count = 0, bear_count = 0;
    for (int i = 0; i < 18; i++) {
        if (tc.tc_kama[i] > tc.tc_kama[i + 1]) bull_count++;
        else if (tc.tc_kama[i] < tc.tc_kama[i + 1]) bear_count++;
    }
    out.ka_bull_pct = (double)bull_count / 18.0;
    out.ka_bear_pct = (double)bear_count / 18.0;

    // ── Measure 2: Hurst RS (throttled) ──────────────────────────────────────
    double& r_hurst = st.hurst;
    if (idx == 0) r_hurst = 0.5;

    int hf = std::max(1, hurst_freq);
    if (idx % hf == 0 && idx >= hurst_len - 1) {
        // Build close buffer
        int buflen = std::min(hurst_len, idx + 1);
        float* hbuf = ta_scratch(tas.scratch_a, buflen);
        for (int i = 0; i < buflen; i++) hbuf[i] = sc.Close[idx - i];
        r_hurst = f_hurst_rs(hbuf, buflen);
    }
    out.hurst = r_hurst;

    // ── Measure 3: TC Base Slope Acceleration ────────────────────────────────
    double tc_base_prev = (idx > 0) ? (double)sc.Subgraph[SG_TC_BASE][idx - 1] : tc.tc_base;
    double tc_base_pp   = (idx > 1) ? (double)sc.Subgraph[SG_TC_BASE][idx - 2] : tc_base_prev;

    double vel      = (atr14 > 0.0) ? ((tc.tc_base - tc_base_prev) / atr14) : 0.0;
    double vel_prev = (atr14 > 0.0) ? ((tc_base_prev - tc_base_pp) / atr14) : 0.0;
    double accel    = vel - vel_prev;

    // EMA of acceleration (Pine ta.ema, src-seeded — TA9:533)
    double r_accel_ema = st.accel_ema.step(accel, accel_smooth_period);
    out.accel_smooth = r_accel_ema;

    // ── Voting ────────────────────────────────────────────────────────────────
    // [v9.3 REGIME-3] Calibrated mode: the RS-Hurst estimator on real futures
    // closes rarely dips below 0.5 (replicated in simulation), so with the
    // parity threshold the Hurst vote fires on nearly EVERY bar — inflating
    // both counts and degenerating 2-of-3 into 1-of-2. A calibrated 0.72
    // threshold restores a real vote. The accel deadband stops the sign-flap
    // vote from micro-oscillations of a flat cloud base.
    double h_thr = cal_mode ? hurst_cal_thresh : hurst_thresh;
    double a_db  = cal_mode ? accel_deadband   : 0.0;
    int bull_v = 0, bear_v = 0;
    if (out.ka_bull_pct >= ka_thresh) bull_v++;
    if (out.ka_bear_pct >= ka_thresh) bear_v++;
    if (out.hurst > h_thr) { bull_v++; bear_v++; }  // hurst just adds to both
    if (r_accel_ema >  a_db && tc.tc_base > tc_base_prev) bull_v++;
    if (r_accel_ema < -a_db && tc.tc_base < tc_base_prev) bear_v++;

    out.bull_votes = bull_v;
    out.bear_votes = bear_v;
    out.suppress_sell = enabled && (bull_v >= votes_required);
    out.suppress_buy  = enabled && (bear_v >= votes_required);

    return out;
}
