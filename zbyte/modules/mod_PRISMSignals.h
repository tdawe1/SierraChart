#pragma once
// mod_PRISMSignals.h — Section 7+7B: PRISM Signal Engine
//
// Faithful port of Pine TA9 §7B (lines 989-1216). Pipeline per bar:
//   1. 4th-degree polynomial regression on close (length = prism_len)
//   2. Dual SuperTrend on poly value (Alpha/Sigma rails, SMA-of-TR ATR)
//   3. Direction flip of BOTH rails → raw bull/bear signal
//   4. Structure Lock  — raw signal against an ADVERSE ribbon (against AND the
//      gap widening) holds up to 4 bars; fires ONLY on an actual ALMA
//      crossover inside the window, otherwise silently cancels (Pine 1021-1040)
//   5. Deduplication   — no repeat same-direction signals (prism_last_sig)
//   6. Bar Quality     — signal bar must close in the signal direction with
//      body/range >= min; otherwise hold up to `bar_qual_wait` bars for a
//      qualifying bar, else silently cancel (Pine 1050-1070)
//   7. Quality Gate + Regime Gate — chop (ER14 low AND flat fast-KAMA slope)
//      or an opposing regime DOWNGRADES the signal to a marginal (MQ) dot —
//      it is never dropped (Pine 1083-1097)
//   8. Elevation — an MQ dot upgrades back to a full signal if ANY of:
//      c1 counter-trend + quadratic-fit vertex forecasts a TC reversal just
//         ahead (fit refreshed every 4 bars, coefficients held between)
//      c2 with-trend: k50 slope outrunning base slope, close beyond base
//      c3 with-trend: quality bar bouncing within 0.5 ATR of the cloud edge
//      c4 with-trend: cumulative volume delta rank >= 95 in signal direction
//      (Pine 1099-1199)
//
// History reads: ribbon cma1/cma2 from SG_ALMA1/SG_ALMA2 .Arrays[0] (written
// every bar by mod_MAribbon regardless of display); tc_base/tc_k50 history
// from their subgraphs; fast KAMA(20) history in SG_PRISM_POLY.Arrays[0].

#include "../include/TA_Subgraphs.h"
#include "../include/TA_Inputs.h"
#include "../include/TA_Algorithms.h"
#include "../include/TA_ColorThemes.h"
#include "../include/TA_State.h"

static const int    SL_MAX_BARS          = 4;    // Pine: bar_index + 4 (hardcoded)
static const int    PRISM_ER_LEN         = 14;   // Pine prism_er_len
static const int    PRISM_FKAMA_LEN      = 20;   // Pine prism_fast_kama_len
static const int    PRISM_FKAMA_SLOPE_LB = 5;    // Pine prism_fkama_slope_len
static const int    ELEV_LOOKBACK        = 25;   // Pine _elev_lookback
static const int    ELEV_REFIT_EVERY     = 4;    // Pine: bar_index % 4 == 0
static const double ELEV_CVD_RANK_MIN    = 95.0; // Pine elev criterion 4 (hardcoded)
static const double ELEV_TC_BOUNCE_ATR   = 0.5;  // Pine elev criterion 3

struct PRISMResult {
    double poly_val;             // 4th-degree poly evaluated at current bar
    double st1_line, st2_line;   // Alpha and Sigma rail lines
    int    st1_dir,  st2_dir;    // -1=bull, 1=bear
    int    cur_dir;              // last agreed dual-rail direction (Pine prism_cur_dir)
    double er;                   // efficiency ratio ER(14)
    bool   quality_ok;           // NOT in chop (info panel)
    bool   regime_ok;            // not downgraded by regime gate (info panel)
    bool   sig_bull, sig_bear;   // passed the gate chain (Pine prism_would_*)
    bool   full_bull, full_bear; // full arrows (Pine prism_is_bull/bear)
    bool   mq_bull,  mq_bear;    // marginal dots shown (Pine _mq_*_only)
    // [v9.3 V1] fire-time features — already computed as locals, now exposed
    // for the outcome logger and the hidden-array stash (zero behavior change)
    double fk_norm;              // normalized fast-KAMA slope
    double bq_ratio;             // signal-bar body/range ratio
    double er_rank;              // ER percentile rank (REGIME-4; 0 when off)
    bool   chop_fired;           // chop verdict of the fired direction
    int    sig_path;             // 0 immediate, 1 SL release, 2 BQ release
    int    elev_mask;            // bit0..3 = e1..e4 of the fired direction
};

// Compute SMA of True Range over 'period' bars ending at sc.Index
// (Pine prism_calcST: ta.sma(PRISM_TR, period) — deliberately NOT ta.atr)
static double prism_atr_sma(SCStudyInterfaceRef sc, int period)
{
    int idx = sc.Index;
    double sum = 0.0;
    int n = 0;
    for (int i = 0; i < period && i <= idx; i++, n++) {
        double hl = (double)(sc.High[idx - i] - sc.Low[idx - i]);
        double hc = (i < idx) ? std::abs((double)sc.High[idx - i] - sc.Close[idx - i - 1]) : hl;
        double lc = (i < idx) ? std::abs((double)sc.Low[idx - i]  - sc.Close[idx - i - 1]) : hl;
        sum += std::max({hl, hc, lc});
    }
    return (n > 0) ? sum / n : 0.0;
}

inline PRISMResult compute_PRISMSignals(
    SCStudyInterfaceRef sc,
    TAState& tas,
    double atr14,             // Wilder ATR(14) from caller (Pine ATR14)
    int    prism_len,         // effective poly lookback (ao_eff_len or base_len)
    int    st1_period,        // rail ATR SMA periods (after adaptive/NS scaling)
    int    st2_period,
    float  alpha_factor,      // Alpha rail factor (0.2)
    float  sigma_factor,      // Sigma rail factor (0.5)
    double cv_h, double cv_l, // display-candle high/low (for elevation c3)
    bool   use_custom_candles,// Indicator Candles enabled (Pine cv_use_custom)
    double cvd_rank,          // from CandlesResult (elevation c4)
    double cvd_net,
    double tc_base_now,       // from TrendCloudResult
    double tc_k50,
    double tc_cloud_top,
    bool   suppress_buy,      // from TrendRegimeGate (false when TRG disabled)
    bool   suppress_sell,
    float  er_thresh,         // IN_PRISM_ER_THRESH (0.2)
    float  kama_slope_min,    // IN_PRISM_KAMA_SLOPE_MIN (0.03)
    float  bar_qual_min,      // IN_PRISM_BAR_QUAL_MIN (0.30)
    int    bar_qual_wait,     // IN_PRISM_BAR_QUAL_WAIT (3, Pine hardcodes 3)
    bool   struct_lock_enable,// IN_PRISM_STRUCT_LOCK
    bool   bq_enable,         // IN_PRISM_BQ_ENABLE
    bool   mq_enable,         // IN_PRISM_MQ_ENABLE (Quality Gate)
    bool   prism_enable,
    bool   strict_mode,       // [v9.3 E2] IN_PRISM_GATE_STRICT
    int    flip_guard_bars,   // [v9.3 E3] IN_PRISM_FLIP_GUARD (0 = off)
    bool   er_adapt,          // [v9.3 REGIME-4] IN_ER_ADAPT_ENABLE
    int    er_adapt_lb,       // IN_ER_ADAPT_LOOKBACK
    float  er_adapt_pct)      // IN_ER_ADAPT_PCT
{
    PRISMResult out = {};
    out.er = 1.0;
    out.quality_ok = true;
    out.regime_ok  = true;
    out.cur_dir    = tas.working.prism_last_dir;

    int idx = sc.Index;
    TARecurrence& st = tas.working;
    // Pine computes the whole engine unconditionally — eff_prism_enable gates
    // only prism_would_* (TA9:1073-1076); `prism_enable` is applied below.
    if (idx == 0) {
        // Write neutral values to hidden subgraphs
        sc.Subgraph[SG_PRISM_POLY][idx]     = (float)sc.Close[idx];
        sc.Subgraph[SG_PRISM_ST1_LINE][idx] = (float)sc.Close[idx];
        sc.Subgraph[SG_PRISM_ST2_LINE][idx] = (float)sc.Close[idx];
        // Pine f_kama_vb steps at bar 0 too: nAMA = 0.0645^2 * close[0]
        const double nsm = 0.0645;
        st.prism_fkama = st.prism_fkama + nsm * nsm * ((double)sc.Close[idx] - st.prism_fkama);
        sc.Subgraph[SG_PRISM_POLY].Arrays[0][idx] = (float)st.prism_fkama;
        return out;
    }

    // ── 1. Polynomial Regression ──────────────────────────────────────────────
    // Sanity cap only — the per-instance scratch grows on demand
    int buflen = std::min(std::min(prism_len, idx + 1), 5000);
    int actual_len = buflen;
    float* cbuf = ta_scratch(tas.scratch_a, actual_len);
    for (int i = 0; i < actual_len; i++) cbuf[i] = sc.Close[idx - i];

    double coeffs[5] = {};
    double poly_val = (double)sc.Close[idx];
    if (actual_len >= 5 && poly_regression(cbuf, actual_len, 4, coeffs))
        poly_val = eval_poly(1.0, coeffs, 4);

    out.poly_val = poly_val;
    sc.Subgraph[SG_PRISM_POLY][idx] = (float)poly_val;

    // ── 2. Dual SuperTrend on poly ────────────────────────────────────────────
    // Pine na-gating: the poly is na until its window fills (bar prism_len-1)
    // and ta.sma(PRISM_TR, p) is na until p TRs with a prior close exist
    // (bar p). While anything is na Pine's rails hold dir=1 and cannot flip —
    // stepping them early would fire raw signals Pine never produces and
    // pollute prism_last_sig / hold state past warm-up.
    bool rails_valid = (idx >= prism_len - 1)
                    && (idx >= st1_period) && (idx >= st2_period);

    double st1_line = poly_val, st2_line = poly_val;
    int    d1 = 1, d2 = 1;
    bool   raw_bull = false, raw_bear = false;

    if (rails_valid) {
        // Rails deliberately use an SMA of TR (Pine's own choice), never ta.atr
        double atr1 = prism_atr_sma(sc, st1_period);
        double atr2 = prism_atr_sma(sc, st2_period);

        calc_supertrend(poly_val, atr1, alpha_factor, st.prism_st1, st1_line, d1);
        calc_supertrend(poly_val, atr2, sigma_factor, st.prism_st2, st2_line, d2);

        // ── 3. Direction agreement + flip → raw signal (Pine 996-1008) ───────
        bool now_bull = (d1 == -1 && d2 == -1);
        bool now_bear = (d1 ==  1 && d2 ==  1);
        bool was_bull = (st.prism_prev_d1 == -1 && st.prism_prev_d2 == -1);
        bool was_bear = (st.prism_prev_d1 ==  1 && st.prism_prev_d2 ==  1);

        raw_bull = now_bull && !was_bull;
        raw_bear = now_bear && !was_bear;

        if (now_bull)      st.prism_last_dir = -1;
        else if (now_bear) st.prism_last_dir = 1;
    }
    // During warm-up prev dirs are pinned at 1 (Pine: var stDir = 1), so the
    // first valid bar (dir 1/1, prev 1/1) produces no phantom flip
    st.prism_prev_d1 = d1;
    st.prism_prev_d2 = d2;
    out.cur_dir = st.prism_last_dir;

    out.st1_line = st1_line; out.st1_dir = d1;
    out.st2_line = st2_line; out.st2_dir = d2;
    sc.Subgraph[SG_PRISM_ST1_LINE][idx] = (float)st1_line;
    sc.Subgraph[SG_PRISM_ST2_LINE][idx] = (float)st2_line;

    // ── 4. Bar Quality — DIRECTIONAL (Pine 1010-1013) ────────────────────────
    double bq_range = std::max((double)(sc.High[idx] - sc.Low[idx]), 1e-10);
    double bq_ratio = std::abs((double)(sc.Close[idx] - sc.Open[idx])) / bq_range;
    bool bq_bull_ok = (sc.Close[idx] >= sc.Open[idx]) && (bq_ratio >= (double)bar_qual_min);
    bool bq_bear_ok = (sc.Close[idx] <= sc.Open[idx]) && (bq_ratio >= (double)bar_qual_min);

    // ── 5. Structure Lock (Pine 1015-1040) ───────────────────────────────────
    // Ribbon values + history from the always-written hidden arrays
    double cma1      = (double)sc.Subgraph[SG_ALMA1].Arrays[0][idx];
    double cma2      = (double)sc.Subgraph[SG_ALMA2].Arrays[0][idx];
    double cma1_prev = (idx > 0) ? (double)sc.Subgraph[SG_ALMA1].Arrays[0][idx - 1] : cma1;
    double cma2_prev = (idx > 0) ? (double)sc.Subgraph[SG_ALMA2].Arrays[0][idx - 1] : cma2;

    bool cma_crossover  = (idx > 0) && (cma1 > cma2) && (cma1_prev <= cma2_prev);
    bool cma_crossunder = (idx > 0) && (cma1 < cma2) && (cma1_prev >= cma2_prev);

    // Adverse = ribbon against the signal AND the gap still widening
    bool adverse_bull = struct_lock_enable && (cma1 < cma2)
                     && ((cma2 - cma1) > (cma2_prev - cma1_prev));
    bool adverse_bear = struct_lock_enable && (cma1 > cma2)
                     && ((cma1 - cma2) > (cma1_prev - cma2_prev));

    // Pine statement order matters: opposite raw cancels, then own raw re-arms
    if (raw_bull) st.sl_bear_hold_until = -1;
    if (raw_bear) st.sl_bull_hold_until = -1;
    if (raw_bull) st.sl_bull_hold_until = adverse_bull ? idx + SL_MAX_BARS : -1;
    if (raw_bear) st.sl_bear_hold_until = adverse_bear ? idx + SL_MAX_BARS : -1;

    bool bull_in_hold  = (st.sl_bull_hold_until > 0) && (idx <= st.sl_bull_hold_until);
    bool bear_in_hold  = (st.sl_bear_hold_until > 0) && (idx <= st.sl_bear_hold_until);
    bool bull_released = bull_in_hold && cma_crossover;    // fire ONLY on crossover
    bool bear_released = bear_in_hold && cma_crossunder;   // expiry = silent cancel
    // [v9.3 E2] Strict mode re-validates delayed releases: the rails must
    // STILL agree with the signal on the release bar (a single-rail flip back
    // produces no raw counter-signal, so it never cancels the hold), and the
    // release bar itself must be a quality bar in the signal direction.
    if (strict_mode) {
        bull_released = bull_released && (d1 == -1 && d2 == -1) && bq_bull_ok;
        bear_released = bear_released && (d1 ==  1 && d2 ==  1) && bq_bear_ok;
    }

    if (bull_released) st.sl_bull_hold_until = -1;
    if (bear_released) st.sl_bear_hold_until = -1;

    // ── 6. Dedup + immediate signals (Pine 1042-1048) ────────────────────────
    // prism_last_sig is read BEFORE this bar's raw signals update it
    bool imm_bull = raw_bull && !adverse_bull && (st.prism_last_sig != 1)
                 && (!bq_enable || bq_bull_ok);
    bool imm_bear = raw_bear && !adverse_bear && (st.prism_last_sig != -1)
                 && (!bq_enable || bq_bear_ok);

    // ── Bar Quality hold (Pine 1050-1070) ────────────────────────────────────
    if (raw_bear) st.bq_bull_hold_until = -1;
    if (raw_bull) st.bq_bear_hold_until = -1;
    if (raw_bull && !adverse_bull && (st.prism_last_sig != 1)  && bq_enable && !bq_bull_ok)
        st.bq_bull_hold_until = idx + bar_qual_wait;
    if (raw_bear && !adverse_bear && (st.prism_last_sig != -1) && bq_enable && !bq_bear_ok)
        st.bq_bear_hold_until = idx + bar_qual_wait;

    bool bq_bull_in_hold  = (st.bq_bull_hold_until > 0) && (idx <= st.bq_bull_hold_until);
    bool bq_bear_in_hold  = (st.bq_bear_hold_until > 0) && (idx <= st.bq_bear_hold_until);
    bool bq_bull_released = bq_bull_in_hold && bq_bull_ok;   // expiry = silent cancel
    bool bq_bear_released = bq_bear_in_hold && bq_bear_ok;
    // [v9.3 E2] Strict mode: a Bar-Quality release also requires the rails to
    // still agree with the signal direction on the release bar
    if (strict_mode) {
        bq_bull_released = bq_bull_released && (d1 == -1 && d2 == -1);
        bq_bear_released = bq_bear_released && (d1 ==  1 && d2 ==  1);
    }

    if (bq_bull_released) st.bq_bull_hold_until = -1;
    if (bq_bear_released) st.bq_bear_hold_until = -1;

    // ── Pre-Quality-Gate result (Pine 1075-1081) ─────────────────────────────
    bool would_bull = prism_enable && (imm_bull || bull_released || bq_bull_released);
    bool would_bear = prism_enable && (imm_bear || bear_released || bq_bear_released);

    if (raw_bull) st.prism_last_sig = 1;
    if (raw_bear) st.prism_last_sig = -1;

    out.sig_bull = would_bull;
    out.sig_bear = would_bear;

    // ── 7. Quality Gate: chop detection (Pine 1083-1093) ─────────────────────
    // ER over 14 bars (NOT the poly lookback)
    bool er_valid = (idx >= PRISM_ER_LEN);
    double er = 0.0;
    if (er_valid) {
        double noise = 0.0;
        for (int i = 0; i < PRISM_ER_LEN; i++)
            noise += std::abs((double)sc.Close[idx - i] - (double)sc.Close[idx - i - 1]);
        er = (noise > 0.0)
           ? std::abs((double)sc.Close[idx] - (double)sc.Close[idx - PRISM_ER_LEN]) / noise
           : 0.0;
    }
    out.er = er;

    // Fast KAMA(20) — Pine f_kama_vb steps EVERY bar from the zero seed; the
    // na efficiency ratio takes the false branch (0) until the window fills,
    // so it crawls from 0 at the minimum rate like the Trend Cloud KAMAs
    {
        double nef = 0.0;
        if (idx >= PRISM_FKAMA_LEN) {
            double nsignal = std::abs((double)sc.Close[idx] - (double)sc.Close[idx - PRISM_FKAMA_LEN]);
            double nnoise  = 0.0;
            for (int i = 0; i < PRISM_FKAMA_LEN; i++)
                nnoise += std::abs((double)sc.Close[idx - i] - (double)sc.Close[idx - i - 1]);
            nef = (nnoise != 0.0) ? nsignal / nnoise : 0.0;
        }
        double nsm = nef * (0.666 - 0.0645) + 0.0645;
        st.prism_fkama = st.prism_fkama + nsm * nsm * ((double)sc.Close[idx] - st.prism_fkama);
    }
    sc.Subgraph[SG_PRISM_POLY].Arrays[0][idx] = (float)st.prism_fkama;

    // fkama[5] exists from bar 5 (values near 0 during the crawl, same as Pine)
    bool fk_valid = (idx >= PRISM_FKAMA_SLOPE_LB);
    double fk_norm = 0.0;
    if (fk_valid && atr14 > 0.0) {
        double fk_prev = (double)sc.Subgraph[SG_PRISM_POLY].Arrays[0][idx - PRISM_FKAMA_SLOPE_LB];
        fk_norm = (st.prism_fkama - fk_prev) / atr14;
    }

    // [v9.3 REGIME-4] Adaptive ER percentile: rank this bar's ER against its
    // own recent history (SG_RAW_ER holds the per-bar ER series). Strictly
    // tightening — the OR only ADDS chop detections below the absolute floor.
    double er_rank = 0.0;
    bool   er_rank_chop = false;
    if (er_adapt && er_valid && idx > PRISM_ER_LEN + 10) {
        int avail = std::min(er_adapt_lb, idx - 1);
        int below = 0;
        for (int i = 1; i <= avail; i++)
            if ((double)sc.Subgraph[SG_RAW_ER][idx - i] > er) below++;
        er_rank = 100.0 * (double)(avail - below) / (double)avail;
        er_rank_chop = (er_rank < (double)er_adapt_pct);
    }
    out.er_rank = er_rank;

    // [v9.3 E2] Strict mode closes the AND-hole: baseline requires low ER AND
    // a flat fast-KAMA slope, so a transient +0.03-ATR slope micro-swing
    // defeats chop detection in dead-flat ER; strict ORs the two conditions.
    double er_term_b = (er < (double)er_thresh) || er_rank_chop;
    bool chop_bull = mq_enable && er_valid && fk_valid
                  && (strict_mode ? (er_term_b || (fk_norm <  (double)kama_slope_min))
                                  : (er_term_b && (fk_norm <  (double)kama_slope_min)));
    bool chop_bear = mq_enable && er_valid && fk_valid
                  && (strict_mode ? (er_term_b || (fk_norm > -(double)kama_slope_min))
                                  : (er_term_b && (fk_norm > -(double)kama_slope_min)));

    // ── Quality Gate + Regime Gate DOWNGRADE to marginal (Pine 1095-1097) ────
    bool is_mq_bull = would_bull && (chop_bull || suppress_buy);
    bool is_mq_bear = would_bear && (chop_bear || suppress_sell);

    out.quality_ok = !(chop_bull || chop_bear);
    out.regime_ok  = !((would_bull && suppress_buy) || (would_bear && suppress_sell));

    // ── 8. Elevation criteria (Pine 1099-1199) ───────────────────────────────
    double tc_base_prev = (idx > 0) ? (double)sc.Subgraph[SG_TC_BASE][idx - 1] : tc_base_now;
    bool ct_bull = (tc_base_now < tc_base_prev) && (cma1 < cma2);   // counter-trend context
    bool ct_bear = (tc_base_now > tc_base_prev) && (cma1 > cma2);

    // Quadratic fit of tc_base over 25 bars, refit every 4 bars, coeffs held
    if (idx % ELEV_REFIT_EVERY == 0 && idx >= ELEV_LOOKBACK - 1) {
        float* qbuf = ta_scratch(tas.scratch_b, ELEV_LOOKBACK);
        for (int i = 0; i < ELEV_LOOKBACK; i++)
            qbuf[i] = sc.Subgraph[SG_TC_BASE][idx - i];
        double qc[5] = {};
        if (poly_regression(qbuf, ELEV_LOOKBACK, 2, qc)) {
            st.elev_c0 = qc[0];
            st.elev_c1 = qc[1];
            st.elev_c2 = qc[2];
        }
    }

    // Vertex of the held parabola; reversal forecast if it sits just ahead
    // (x in [1.0, 1.0 + 8/(lookback-1)] where x=1.0 is the current bar)
    bool   vx_valid = (std::abs(st.elev_c2) > 1e-10);
    double vx       = vx_valid ? (-st.elev_c1 / (2.0 * st.elev_c2)) : 0.0;
    double fc_far   = 1.0 + 8.0 / (double)(ELEV_LOOKBACK - 1);
    bool elev_tc_rev_bull = vx_valid && (st.elev_c2 > 0.0) && (vx >= 1.0) && (vx <= fc_far);
    bool elev_tc_rev_bear = vx_valid && (st.elev_c2 < 0.0) && (vx >= 1.0) && (vx <= fc_far);

    // c1: counter-trend signal + TC reversal forecast
    bool e1_bull = ct_bull && elev_tc_rev_bull;
    bool e1_bear = ct_bear && elev_tc_rev_bear;

    // c2: strong with-trend alignment (k50 outrunning base)
    double k50_prev = (idx > 0) ? (double)sc.Subgraph[SG_TC_K50][idx - 1] : tc_k50;
    bool e2_bull = !ct_bull
                && (tc_base_now > tc_base_prev)
                && ((tc_k50 - k50_prev) > (tc_base_now - tc_base_prev))
                && ((double)sc.Close[idx] > tc_base_now);
    bool e2_bear = !ct_bear
                && (tc_base_now < tc_base_prev)
                && ((tc_k50 - k50_prev) < (tc_base_now - tc_base_prev))
                && ((double)sc.Close[idx] < tc_base_now);

    // c3: with-trend TC bounce on a quality bar (bull vs base, bear vs cloud top)
    double elev_low  = use_custom_candles ? cv_l : (double)sc.Low[idx];
    double elev_high = use_custom_candles ? cv_h : (double)sc.High[idx];
    bool e3_bull = !ct_bull && (tc_base_now > tc_base_prev) && bq_bull_ok
                && (std::abs(elev_low - tc_base_now) <= atr14 * ELEV_TC_BOUNCE_ATR);
    bool e3_bear = !ct_bear && (tc_base_now < tc_base_prev) && bq_bear_ok
                && (std::abs(elev_high - tc_cloud_top) <= atr14 * ELEV_TC_BOUNCE_ATR);

    // c4: very strong cumulative CVD in the signal direction
    bool e4_bull = !ct_bull && (cvd_rank >= ELEV_CVD_RANK_MIN) && (cvd_net > 0.0);
    bool e4_bear = !ct_bear && (cvd_rank >= ELEV_CVD_RANK_MIN) && (cvd_net < 0.0);

    // [v9.3 E2] Strict mode requires a 2-criteria quorum to re-promote a
    // downgraded signal — a single weak criterion (a 3-bar-stale quad-fit
    // vertex, or the delta rank alone) can no longer rescue a chop signal.
    int elev_n_bull = (int)e1_bull + (int)e2_bull + (int)e3_bull + (int)e4_bull;
    int elev_n_bear = (int)e1_bear + (int)e2_bear + (int)e3_bear + (int)e4_bear;
    bool elev_bull = is_mq_bull && (strict_mode ? (elev_n_bull >= 2) : (elev_n_bull >= 1));
    bool elev_bear = is_mq_bear && (strict_mode ? (elev_n_bear >= 2) : (elev_n_bear >= 1));

    // ── Final signals (Pine 1201-1211) ───────────────────────────────────────
    out.full_bull = (would_bull && !is_mq_bull) || elev_bull;
    out.full_bear = (would_bear && !is_mq_bear) || elev_bear;
    out.mq_bull   = is_mq_bull && !elev_bull;   // dot shown only if not elevated
    out.mq_bear   = is_mq_bear && !elev_bear;

    // [v9.3 E3] Opposite-signal cooldown — applied AFTER elevation so nothing
    // can re-promote around it. An immediate counter-flip inside the guard
    // window prints as a marginal dot instead of a full arrow (never dropped).
    if (flip_guard_bars > 0) {
        if (out.full_bull && st.last_full_dir == -1
            && (idx - st.last_full_bar) <= flip_guard_bars) {
            out.full_bull = false; out.mq_bull = true;
        }
        if (out.full_bear && st.last_full_dir == 1
            && (idx - st.last_full_bar) <= flip_guard_bars) {
            out.full_bear = false; out.mq_bear = true;
        }
    }
    // State updates from the POST-guard flags: a demoted counter-flip does not
    // re-arm the window. (Order-flow demotions in section 7C happen after this
    // and leave the armed state in place — strictly more conservative.)
    if (out.full_bull)      { st.last_full_bar = idx; st.last_full_dir = 1;  }
    else if (out.full_bear) { st.last_full_bar = idx; st.last_full_dir = -1; }

    // [v9.3 V1] Expose the fire-time gate context (locals until now)
    out.fk_norm  = fk_norm;
    out.bq_ratio = bq_ratio;
    bool fired_bull = out.full_bull || out.mq_bull;
    out.chop_fired = fired_bull ? chop_bull : chop_bear;
    out.sig_path = fired_bull ? (imm_bull ? 0 : (bull_released ? 1 : 2))
                              : (imm_bear ? 0 : (bear_released ? 1 : 2));
    out.elev_mask = fired_bull
        ? ((int)e1_bull | ((int)e2_bull << 1) | ((int)e3_bull << 2) | ((int)e4_bull << 3))
        : ((int)e1_bear | ((int)e2_bear << 1) | ((int)e3_bear << 2) | ((int)e4_bear << 3));

    return out;
}
