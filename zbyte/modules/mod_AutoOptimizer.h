#pragma once
// mod_AutoOptimizer.h — Section 7A: Auto-Optimizer
// Tests 3 lookback lengths simultaneously; scores each via ATR-tiered metrics;
// blends with convex weighting to produce ao_eff_len.
// Uses 6 SuperTrend instances (3 lengths × 2 rails).
// Uses 3 std::deque<int> ring buffers for score history.

#include "../include/TA_Subgraphs.h"
#include "../include/TA_Inputs.h"
#include "../include/TA_Algorithms.h"
#include "../include/TA_State.h"
#include "mod_TrendCloud.h"   // for KAMA_PERIODS reference only

struct AOResult {
    int    ao_len_s, ao_len_m, ao_len_l;
    int    ao_eff_len;
    double avg_s, avg_m, avg_l;
    double w_s, w_m, w_l;
    int    sz_s, sz_m, sz_l;
    bool   is_warming_up;
};

// Helper: run poly + dual ST for one length, return (bull_signal, bear_signal, d1, d2)
// All recurrence state (SuperTrend ratchets, previous directions) lives in the
// commit/rollback TARecurrence — re-ticks of the live bar roll back cleanly.
static void ao_pipe(
    SCStudyInterfaceRef sc,
    TARecurrence& st,
    double poly_val,
    int alpha_period, int sigma_period,
    float st1_factor, float st2_factor,
    int st_inst_base,  // index into st.ao_st[] pairs (0, 2, 4 for s/m/l)
    bool& out_sig_bull, bool& out_sig_bear,
    int& out_d1, int& out_d2)
{
    STState& st1 = st.ao_st[st_inst_base];
    STState& st2 = st.ao_st[st_inst_base + 1];

    // ATR SMA for ST (PRISM_TR over alpha/sigma periods) — Pine deliberately
    // uses an SMA of TR for the rails, NOT ta.atr; keep it.
    int idx = sc.Index;
    auto get_prism_tr = [&](int i) -> double {
        if (i <= 0) return (double)(sc.High[i] - sc.Low[i]);
        return std::max({(double)(sc.High[i] - sc.Low[i]),
                         std::abs((double)sc.High[i] - sc.Close[i-1]),
                         std::abs((double)sc.Low[i]  - sc.Close[i-1])});
    };

    double atr_alpha = 0.0, atr_sigma = 0.0;
    for (int i = 0; i < alpha_period && i <= idx; i++) atr_alpha += get_prism_tr(idx - i);
    atr_alpha /= std::min(alpha_period, idx + 1);
    for (int i = 0; i < sigma_period && i <= idx; i++) atr_sigma += get_prism_tr(idx - i);
    atr_sigma /= std::min(sigma_period, idx + 1);

    double out_line1 = 0.0, out_line2 = 0.0;
    int d1 = 0, d2 = 0;
    calc_supertrend(poly_val, atr_alpha, st1_factor, st1, out_line1, d1);
    calc_supertrend(poly_val, atr_sigma, st2_factor, st2, out_line2, d2);

    out_d1 = d1; out_d2 = d2;

    int& r_prev_d1 = st.ao_prev_d[st_inst_base];
    int& r_prev_d2 = st.ao_prev_d[st_inst_base + 1];

    bool now_bull = (d1 == -1 && d2 == -1);
    bool now_bear = (d1 ==  1 && d2 ==  1);
    bool was_bull = (r_prev_d1 == -1 && r_prev_d2 == -1);
    bool was_bear = (r_prev_d1 ==  1 && r_prev_d2 ==  1);

    out_sig_bull = now_bull && !was_bull;
    out_sig_bear = now_bear && !was_bear;

    r_prev_d1 = d1;
    r_prev_d2 = d2;
}

inline AOResult compute_AutoOptimizer(
    SCStudyInterfaceRef sc,
    TAState& tas,
    double atr14,
    int prism_base_len,   // _prism_len after adaptive/NS scaling
    int prism_st1_period,
    int prism_st2_period,
    float st1_factor,
    float st2_factor,
    float ao_spread_pct,
    float ao_tier1,
    float ao_tier2,
    float ao_tier3,
    int   ao_max_bars,
    int   ao_lookback,
    bool  enabled,
    float ao_stop_atr)   // [v9.3 E5] adverse stop in ATR units, 0 = off (parity)
{
    AOResult out = {};
    int idx = sc.Index;
    TARecurrence& st = tas.working;

    // Score history lives directly in the commit/rollback state: full-recalc
    // reset and live-bar rollback (no duplicate pushes per tick) come for free.
    std::deque<int>& scores_s = st.ao_scores[0];
    std::deque<int>& scores_m = st.ao_scores[1];
    std::deque<int>& scores_l = st.ao_scores[2];

    // Compute three test lengths
    double spread_frac = ao_spread_pct / 100.0;
    out.ao_len_s = f_quantize((double)prism_base_len * (1.0 - spread_frac), 5);
    out.ao_len_m = prism_base_len;
    out.ao_len_l = f_quantize((double)prism_base_len * (1.0 + spread_frac), 5);

    if (!enabled || idx == 0) {
        // idx 0: nothing to regress and the SuperTrends must not seed on a
        // degenerate value — return neutral, exactly like PRISM's early-out
        out.ao_eff_len = out.ao_len_m;
        return out;
    }

    // Compute poly for each length and run ST pipeline.
    // Scratch grows to the largest test length — the old static float[512]
    // overflowed (silent memory corruption) when PRISM Adaptive scaled the
    // base length up on sub-2-minute charts with a wide spread.
    double coeffs_s[5]={}, coeffs_m[5]={}, coeffs_l[5]={};
    // Defensive max — a mis-set negative spread would make len_s the largest
    float* cbuf = ta_scratch(tas.scratch_a,
        std::max(std::max(out.ao_len_s, out.ao_len_m), out.ao_len_l));
    auto get_buf = [&](int len) {
        for (int i = 0; i < len && i <= idx; i++) cbuf[i] = sc.Close[idx - i];
    };

    // Fall back to price when the regression can't run yet (mirrors PRISM) so
    // warm-up bars don't step the SuperTrends with a garbage 0.0 level
    double poly_s = (double)sc.Close[idx];
    double poly_m = (double)sc.Close[idx];
    double poly_l = (double)sc.Close[idx];

    get_buf(out.ao_len_s);
    if (poly_regression(cbuf, std::min(out.ao_len_s, idx+1), 4, coeffs_s))
        poly_s = eval_poly(1.0, coeffs_s, 4);

    get_buf(out.ao_len_m);
    if (poly_regression(cbuf, std::min(out.ao_len_m, idx+1), 4, coeffs_m))
        poly_m = eval_poly(1.0, coeffs_m, 4);

    get_buf(out.ao_len_l);
    if (poly_regression(cbuf, std::min(out.ao_len_l, idx+1), 4, coeffs_l))
        poly_l = eval_poly(1.0, coeffs_l, 4);

    bool sig_bull_s=false, sig_bear_s=false;
    bool sig_bull_m=false, sig_bear_m=false;
    bool sig_bull_l=false, sig_bear_l=false;
    int d1_s=1, d2_s=1, d1_m=1, d2_m=1, d1_l=1, d2_l=1;

    // Pine na-gating (same as the PRISM rails): a pipeline's poly is na until
    // its window fills and the rail ATR is na until `period` bars — stepping
    // the SuperTrends early would inject phantom flips into the score history
    bool atr_ok = (idx >= prism_st1_period) && (idx >= prism_st2_period);
    auto run_pipe = [&](int len, double poly, int base,
                        bool& sb, bool& sbr, int& a, int& b) {
        if (atr_ok && idx >= len - 1) {
            ao_pipe(sc, st, poly, prism_st1_period, prism_st2_period,
                    st1_factor, st2_factor, base, sb, sbr, a, b);
        } else {
            st.ao_prev_d[base]     = 1;   // pin like Pine's var stDir = 1
            st.ao_prev_d[base + 1] = 1;
            sb = false; sbr = false; a = 1; b = 1;
        }
    };
    run_pipe(out.ao_len_s, poly_s, 0, sig_bull_s, sig_bear_s, d1_s, d2_s);
    run_pipe(out.ao_len_m, poly_m, 2, sig_bull_m, sig_bear_m, d1_m, d2_m);
    run_pipe(out.ao_len_l, poly_l, 4, sig_bull_l, sig_bear_l, d1_l, d2_l);

    bool sig_any_s = sig_bull_s || sig_bear_s;
    bool sig_any_m = sig_bull_m || sig_bear_m;
    bool sig_any_l = sig_bull_l || sig_bear_l;

    // Pending signal state (per-length)
    auto process_pending = [&](
        bool sig_any, bool sig_bull,
        AOPendingState& p,
        std::deque<int>& scores) {

        bool active = (p.price != 0.0);

        if (active) {
            double fav = (p.dir == 1) ? (sc.High[idx] - p.price) : (p.price - sc.Low[idx]);
            if      (fav >= p.atr * ao_tier3) p.best = 3;
            else if (fav >= p.atr * ao_tier2) p.best = std::max(p.best, 2);
            else if (fav >= p.atr * ao_tier1) p.best = std::max(p.best, 1);

            // [v9.3 E5] Adverse-excursion tracking: with a stop configured, a
            // signal that draws down stop_atr ATRs before reaching Tier 1 is
            // finalized as a loss (0) immediately — MFE-only scoring counted
            // "won 1.5 ATR after a 3-ATR drawdown" as a clean win. 0.0 = off
            // (parity: the branch never runs).
            double adv = (p.dir == 1) ? (p.price - sc.Low[idx]) : (sc.High[idx] - p.price);
            p.worst = std::max(p.worst, adv);
            bool stopped = (ao_stop_atr > 0.0f)
                        && (p.worst >= p.atr * (double)ao_stop_atr)
                        && (p.best < 1);

            bool expire = stopped || (p.best >= 3) || ((idx - p.bar) >= ao_max_bars) || sig_any;
            if (expire) {
                scores.push_back(stopped ? 0 : p.best);
                while ((int)scores.size() > ao_lookback) scores.pop_front();
                p.price = 0.0;
                active = false;
            }
        }
        if (sig_any && !active) {
            p.price = (double)sc.Close[idx];
            p.dir   = sig_bull ? 1 : -1;
            p.bar   = idx;
            p.best  = 0;
            p.worst = 0.0;
            p.atr   = atr14;
        }
    };

    process_pending(sig_any_s, sig_bull_s, st.ao_pend[0], scores_s);
    process_pending(sig_any_m, sig_bull_m, st.ao_pend[1], scores_m);
    process_pending(sig_any_l, sig_bull_l, st.ao_pend[2], scores_l);

    // Weighted blend
    out.sz_s = (int)scores_s.size();
    out.sz_m = (int)scores_m.size();
    out.sz_l = (int)scores_l.size();

    out.avg_s = f_convex_weight_blend(scores_s);
    out.avg_m = f_convex_weight_blend(scores_m);
    out.avg_l = f_convex_weight_blend(scores_l);

    out.w_s = f_ao_score_weight(out.avg_s, out.sz_s);
    out.w_m = f_ao_score_weight(out.avg_m, out.sz_m);
    out.w_l = f_ao_score_weight(out.avg_l, out.sz_l);

    double w_total = out.w_s + out.w_m + out.w_l;
    out.is_warming_up = (w_total <= 0.0);

    if (w_total > 0.0) {
        double blended = (out.ao_len_s * out.w_s + out.ao_len_m * out.w_m + out.ao_len_l * out.w_l) / w_total;
        out.ao_eff_len = (int)std::round(blended);
    } else {
        out.ao_eff_len = out.ao_len_m;
    }

    return out;
}
