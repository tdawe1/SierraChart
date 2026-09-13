#pragma once
// mod_RangeRegimeGate.h — [v9.3 REGIME-2, ENHANCEMENT — no Pine equivalent]
// Non-directional range/chop regime detector: 2-of-3 vote over
//   1. Choppiness Index (chop_len)      — chop  > chop_thresh  → range vote
//   2. Wilder ADX (adx_len)             — adx   < adx_floor    → range vote
//   3. ATR(14) percentile (atrp_lb)     — %tile < atrp_floor   → range vote
// When range_active, the caller ORs it into BOTH TRG suppress flags feeding
// compute_PRISMSignals — signals in a detected range print as marginal dots
// via the existing downgrade path (never dropped; elevation can still rescue).
//
// ADX recurrence lives in TARecurrence (commit/rollback + full-recalc reset);
// CHOP and the ATR percentile are windowed re-computations — stateless and
// tick-idempotent. ATR14 history is stored in SG_RRG_STORE by the caller-
// supplied atr14 (written here, read over the percentile window).

#include "../include/TA_Subgraphs.h"
#include "../include/TA_Inputs.h"
#include "../include/TA_Algorithms.h"
#include "../include/TA_State.h"

struct RangeRegimeResult {
    bool   range_active;   // >= 2 votes → suppress full arrows
    int    votes;
    double chop;           // Choppiness Index value
    double adx;            // Wilder ADX value
    double atr_pctl;       // ATR14 percentile rank (0-100)
};

inline RangeRegimeResult compute_RangeRegimeGate(
    SCStudyInterfaceRef sc,
    TAState& tas,
    double atr14,
    bool   enabled,
    int    chop_len,      // IN_RRG_CHOP_LEN (14)
    double chop_thresh,   // IN_RRG_CHOP_THRESH (61.8)
    int    adx_len,       // IN_RRG_ADX_LEN (14)
    double adx_floor,     // IN_RRG_ADX_FLOOR (20.0)
    int    atrp_lb,       // IN_RRG_ATRP_LOOKBACK (200)
    double atrp_floor)    // IN_RRG_ATRP_FLOOR (25.0)
{
    RangeRegimeResult out = {};
    int idx = sc.Index;
    TARecurrence& st = tas.working;

    // ATR14 history for the percentile vote — written unconditionally so the
    // window is populated even while the gate is toggled off
    sc.Subgraph[SG_RRG_STORE][idx] = (float)atr14;

    // ── Wilder ADX — steps every bar (state must stay warm across toggles) ──
    // +DM/-DM/TR Wilder RMAs then RMA of DX, ta_wilder_atr_step-style warm-up
    if (idx >= 1) {
        double up_move = (double)sc.High[idx] - (double)sc.High[idx - 1];
        double dn_move = (double)sc.Low[idx - 1] - (double)sc.Low[idx];
        double dmp = (up_move > dn_move && up_move > 0.0) ? up_move : 0.0;
        double dmn = (dn_move > up_move && dn_move > 0.0) ? dn_move : 0.0;
        double tr  = ta_true_range(sc, idx);
        int n = (adx_len < 1) ? 1 : adx_len;

        if (idx < n) {
            // warm-up: running means
            st.rrg_dmp = (st.rrg_dmp * (double)(idx - 1) + dmp) / (double)idx;
            st.rrg_dmn = (st.rrg_dmn * (double)(idx - 1) + dmn) / (double)idx;
            st.rrg_tr  = (st.rrg_tr  * (double)(idx - 1) + tr)  / (double)idx;
        } else {
            st.rrg_dmp = (st.rrg_dmp * (double)(n - 1) + dmp) / (double)n;
            st.rrg_dmn = (st.rrg_dmn * (double)(n - 1) + dmn) / (double)n;
            st.rrg_tr  = (st.rrg_tr  * (double)(n - 1) + tr)  / (double)n;
        }
        double dip = (st.rrg_tr > 1e-10) ? 100.0 * st.rrg_dmp / st.rrg_tr : 0.0;
        double din = (st.rrg_tr > 1e-10) ? 100.0 * st.rrg_dmn / st.rrg_tr : 0.0;
        double dx  = (dip + din > 1e-10) ? 100.0 * std::abs(dip - din) / (dip + din) : 0.0;
        st.rrg_adx = (idx < 2 * n)
                   ? (st.rrg_adx * (double)(idx - 1) + dx) / (double)idx
                   : (st.rrg_adx * (double)(n - 1) + dx) / (double)n;
    }
    out.adx = st.rrg_adx;

    if (!enabled) return out;   // state stays warm; no votes while off

    // ── Vote 1: Choppiness Index ─────────────────────────────────────────────
    int cl = (chop_len < 2) ? 2 : chop_len;
    bool chop_vote = false;
    if (idx >= cl) {
        double tr_sum = 0.0, hh = -1e18, ll = 1e18;
        for (int i = 0; i < cl; i++) {
            tr_sum += ta_true_range(sc, idx - i);
            if ((double)sc.High[idx - i] > hh) hh = (double)sc.High[idx - i];
            if ((double)sc.Low[idx - i]  < ll) ll = (double)sc.Low[idx - i];
        }
        out.chop = (hh > ll && tr_sum > 0.0)
                 ? 100.0 * std::log10(tr_sum / (hh - ll)) / std::log10((double)cl)
                 : 100.0;   // degenerate flat window = maximal chop
        chop_vote = (out.chop > chop_thresh);
    }

    // ── Vote 2: ADX floor ────────────────────────────────────────────────────
    bool adx_vote = (idx >= 2 * ((adx_len < 1) ? 1 : adx_len)) && (out.adx < adx_floor);

    // ── Vote 3: ATR percentile ───────────────────────────────────────────────
    bool atrp_vote = false;
    if (idx >= 20) {
        int avail = std::min(atrp_lb, idx);
        int below = 0;
        for (int i = 1; i <= avail; i++)
            if ((double)sc.Subgraph[SG_RRG_STORE][idx - i] <= (float)atr14) below++;
        out.atr_pctl = 100.0 * (double)below / (double)avail;
        atrp_vote = (out.atr_pctl < atrp_floor);
    }

    out.votes = (int)chop_vote + (int)adx_vote + (int)atrp_vote;
    out.range_active = (out.votes >= 2);

    // Hidden history for the outcome logger / external reference
    sc.Subgraph[SG_RAW_CCO].Arrays[1][idx] = (float)out.votes;
    sc.Subgraph[SG_RAW_CCO].Arrays[2][idx] = (float)out.chop;
    sc.Subgraph[SG_RAW_CCO].Arrays[3][idx] = (float)out.adx;
    sc.Subgraph[SG_RAW_CCO].Arrays[4][idx] = (float)out.atr_pctl;

    return out;
}
