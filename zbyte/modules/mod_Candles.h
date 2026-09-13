#pragma once
// mod_Candles.h — Section 5: Indicator Candles
// Implements FasterHA (volume-weighted Kalman), R-Squared Adaptive, LinReg Heikin Ashi,
// LinReg Candles, and CVD border highlighting.

#include "../include/TA_Subgraphs.h"
#include "../include/TA_Inputs.h"
#include "../include/TA_Algorithms.h"
#include "../include/TA_ColorThemes.h"
#include "../include/TA_State.h"

// Candle type string indices (match IN_CANDLE_TYPE SetCustomInputStrings order)
enum CandleType { CT_REGULAR = 0, CT_HEIKIN_ASHI, CT_R2_ADAPTIVE, CT_LINREG_HA, CT_LINREG };

// FHA constants (Pine hardcodes)
static const double FHA_RESPONSIVENESS = 0.7;
static const double FHA_VOL_INFLUENCE  = 0.5;
static const double FHA_MAX_VOL_MULT   = 3.0;
static const double FHA_MEAS_N         = 0.1;
static const double FHA_KG             = 0.7;

// R2 Adaptive blend range
static const double CV_R2_LO  = 0.3;
static const double CV_R2_HI  = 0.8;
static const double CV_BLD_MIN = 0.20;
static const double CV_BLD_MAX = 0.80;

// CVD lookback
static const int CVD_LOOKBACK = 50;

struct CandlesResult {
    double cv_o, cv_h, cv_l, cv_c;
    double cvd_rank;
    double cvd_net;
    bool   cvd_strong;    // cvd_rank >= strong threshold
    bool   delta_is_real; // [v9.3] this bar's delta came from bid/ask volume
    double cvd_window;    // [v9.3 OF-3] windowed L-bar cumulative delta
    COLORREF body_color;
    COLORREF border_color;
};

inline CandlesResult compute_Candles(
    SCStudyInterfaceRef sc,
    TAState& tas,
    const ThemeProfile& theme,
    CandleType candle_type,
    int lr_length,
    double cvd_strong_thresh,  // percentile, e.g. 95.0
    bool cvd_enable,
    bool use_custom,
    bool use_true_delta,       // [v9.3 OF-1] real bid/ask aggressor volume
    int  div_lookback)         // [v9.3 OF-3] cumulative-delta window (bars)
{
    CandlesResult out = {};
    int idx = sc.Index;
    TARecurrence& st = tas.working;

    double o = (double)sc.Open[idx];
    double h = (double)sc.High[idx];
    double l = (double)sc.Low[idx];
    double c = (double)sc.Close[idx];
    double vol = (double)sc.Volume[idx];

    // ── FHA Kalman state (commit/rollback recurrence fields) ─────────────────
    // Defaults (vol_err=1, pvp_err=1, pvp_var=1, rest 0) are re-established by
    // the full-recalc reset in TrendArchitect.cpp before idx 0 is computed.
    double& r_fha_vol_est = st.fha_vol_est;
    double& r_fha_vol_err = st.fha_vol_err;
    double& r_fha_pvp_est = st.fha_pvp_est;
    double& r_fha_pvp_err = st.fha_pvp_err;
    double& r_fha_pvp_var = st.fha_pvp_var;
    double& r_cv_ha_o     = st.cv_ha_o;
    double& r_cv_lrha_o   = st.cv_lrha_o;

    // Kalman filter for volume estimate
    double fha_bar_pvp = (c - o) * vol;
    double vk_vol = (r_fha_vol_err + FHA_MEAS_N) / (r_fha_vol_err + FHA_MEAS_N + FHA_MEAS_N);
    double new_vol_est = (r_fha_vol_est == 0.0 && idx == 0) ? vol
                       : r_fha_vol_est + vk_vol * (vol - r_fha_vol_est);
    double new_vol_err = (1.0 - vk_vol) * (r_fha_vol_err + FHA_MEAS_N);

    double vk_pvp = (r_fha_pvp_err + FHA_MEAS_N) / (r_fha_pvp_err + FHA_MEAS_N + FHA_MEAS_N);
    double new_pvp_est = (r_fha_pvp_est == 0.0 && idx == 0) ? fha_bar_pvp
                       : r_fha_pvp_est + vk_pvp * (fha_bar_pvp - r_fha_pvp_est);
    double new_pvp_err = (1.0 - vk_pvp) * (r_fha_pvp_err + FHA_MEAS_N);
    // Pine (TA9:270-272): var seeds at 1.0 and takes the recurrent branch even
    // on bar 0 — the squared-error re-seed happens ONLY if var falls <= 0
    double new_pvp_var = (r_fha_pvp_var <= 0.0)
                       ? (fha_bar_pvp - new_pvp_est) * (fha_bar_pvp - new_pvp_est)
                       : r_fha_pvp_var + FHA_KG * ((fha_bar_pvp - new_pvp_est) * (fha_bar_pvp - new_pvp_est) - r_fha_pvp_var);

    double pvp_std  = std::sqrt(std::max(new_pvp_var, 0.0001));
    double vol_rat  = std::min(vol / std::max(new_vol_est, 1.0), FHA_MAX_VOL_MULT);
    double pvp_norm = (pvp_std > 0.0) ? ((fha_bar_pvp - new_pvp_est) / pvp_std) : 0.0;
    double comb_vf  = std::sqrt(vol_rat) * std::max(0.5, std::min(1.5, 1.0 + pvp_norm * 0.2));
    double vf       = 1.0 + (comb_vf - 1.0) * FHA_VOL_INFLUENCE;

    double cw  = 1.0 + FHA_RESPONSIVENESS * 2.0;
    double ow_ = 1.0 + FHA_RESPONSIVENESS * 0.5;
    double hw_ = 1.0 + FHA_RESPONSIVENESS * 0.3;
    double tot = (ow_ + hw_ * 2.0 + cw) * vf;
    double spd = std::min(FHA_RESPONSIVENESS * (1.0 + FHA_RESPONSIVENESS * 0.5) * std::sqrt(vf), 1.0);

    // FHA OHLC
    double cv_ha_c_raw = (o * ow_ + h * hw_ + l * hw_ + c * cw) * vf / tot;
    double ha_trad_o   = (r_cv_ha_o == 0.0 && idx == 0)
                       ? (o + c) / 2.0
                       : (r_cv_ha_o + (idx > 0 ? (double)sc.Subgraph[SG_FHA_CLOSE_PREV][idx - 1] : cv_ha_c_raw)) / 2.0;
    double new_cv_ha_o = (r_cv_ha_o == 0.0 && idx == 0)
                       ? (o * ow_ + c * cw) / ((ow_ + cw) * vf)
                       : r_cv_ha_o + spd * (ha_trad_o - r_cv_ha_o);
    double cv_ha_c = cv_ha_c_raw;
    double cv_ha_h = std::max({h, new_cv_ha_o, cv_ha_c});
    double cv_ha_l = std::min({l, new_cv_ha_o, cv_ha_c});

    // Store FHA close for next bar's open calculation
    sc.Subgraph[SG_FHA_CLOSE_PREV][idx] = (float)cv_ha_c;

    // LinReg OHLC
    double cv_lr_o = 0.0, cv_lr_h = 0.0, cv_lr_l = 0.0, cv_lr_c = 0.0;
    if (idx >= lr_length - 1) {
        // Build buffers for linreg (per-instance scratch — all four live at once)
        int buflen = std::min(lr_length, idx + 1);
        float* buf_o = ta_scratch(tas.scratch_a, buflen);
        float* buf_h = ta_scratch(tas.scratch_b, buflen);
        float* buf_l = ta_scratch(tas.scratch_c, buflen);
        float* buf_c = ta_scratch(tas.scratch_d, buflen);
        for (int i = 0; i < buflen; i++) {
            buf_o[i] = sc.Open[idx - i];
            buf_h[i] = sc.High[idx - i];
            buf_l[i] = sc.Low[idx - i];
            buf_c[i] = sc.Close[idx - i];
        }
        // LinReg endpoint value: fit y=a+bx over [0..len-1], return value at x=len-1
        // Using ta.linreg(src, len, 0) = forecast at current bar
        auto linreg_val = [](const float* buf, int len) -> double {
            if (len < 2) return (double)buf[0];
            double sx = 0, sy = 0, sxx = 0, sxy = 0;
            for (int i = 0; i < len; i++) {
                sx += i; sy += buf[len-1-i];
                sxx += (double)i * i; sxy += (double)i * buf[len-1-i];
            }
            double n = len;
            double denom = n * sxx - sx * sx;
            if (std::abs(denom) < 1e-10) return (double)buf[0];
            double b = (n * sxy - sx * sy) / denom;
            double a = (sy - b * sx) / n;
            return a + b * (double)(len - 1);
        };
        cv_lr_o = linreg_val(buf_o, buflen);
        cv_lr_h = linreg_val(buf_h, buflen);
        cv_lr_l = linreg_val(buf_l, buflen);
        cv_lr_c = linreg_val(buf_c, buflen);
    } else {
        cv_lr_o = o; cv_lr_h = h; cv_lr_l = l; cv_lr_c = c;
    }
    // Pine never mutates cv_lr_* (TA9:296-299): the hi/lo clamp applies only
    // to the 'LinReg Candles' output copies (cv_lrc_*, TA9:311-313) — the
    // R2-Adaptive blend and LinReg-HA consume the RAW linreg values
    double cv_lrc_h = std::max(cv_lr_h, std::max(cv_lr_o, cv_lr_c));
    double cv_lrc_l = std::min(cv_lr_l, std::min(cv_lr_o, cv_lr_c));

    // R2-Adaptive OHLC
    double cv_r2 = 0.0;
    if (idx >= lr_length - 1) {
        // Correlation(close, bar_index, lr_length)
        // (scratch_a is free again — the LinReg buffers above are consumed)
        int buflen = std::min(lr_length, idx + 1);
        float* buf_c2 = ta_scratch(tas.scratch_a, buflen);
        for (int i = 0; i < buflen; i++) buf_c2[i] = sc.Close[idx - i];
        double mc = 0, mx = 0;
        for (int i = 0; i < buflen; i++) { mc += buf_c2[i]; mx += (buflen-1-i); }
        mc /= buflen; mx /= buflen;
        double num = 0, dc = 0, dx = 0;
        for (int i = 0; i < buflen; i++) {
            double dci = buf_c2[i] - mc, dxi = (double)(buflen-1-i) - mx;
            num += dci * dxi; dc += dci * dci; dx += dxi * dxi;
        }
        double corr = (dc > 0 && dx > 0) ? (num / std::sqrt(dc * dx)) : 0.0;
        cv_r2 = corr * corr;
    }

    double r2_norm = std::max(0.0, std::min(1.0, (cv_r2 - CV_R2_LO) / (CV_R2_HI - CV_R2_LO)));
    double blend   = CV_BLD_MIN + r2_norm * (CV_BLD_MAX - CV_BLD_MIN);
    double cv_ra_o = o * (1.0 - blend) + cv_lr_o * blend;
    double cv_ra_h = h * (1.0 - blend) + cv_lr_h * blend;
    double cv_ra_l = l * (1.0 - blend) + cv_lr_l * blend;
    double cv_ra_c = c * (1.0 - blend) + cv_lr_c * blend;
    cv_ra_h = std::max(cv_ra_h, std::max(cv_ra_o, cv_ra_c));
    cv_ra_l = std::min(cv_ra_l, std::min(cv_ra_o, cv_ra_c));

    // LinReg HA open
    double lrha_c_raw = (cv_lr_o * ow_ + cv_lr_h * hw_ + cv_lr_l * hw_ + cv_lr_c * cw) * vf / tot;
    double lrha_trad_o = (r_cv_lrha_o == 0.0 && idx == 0)
                       ? (cv_lr_o + cv_lr_c) / 2.0
                       : (r_cv_lrha_o + (idx > 0 ? (double)sc.Subgraph[SG_LRHA_CLOSE_PREV][idx-1] : lrha_c_raw)) / 2.0;
    double new_lrha_o  = (r_cv_lrha_o == 0.0 && idx == 0)
                       ? (cv_lr_o * ow_ + cv_lr_c * cw) / ((ow_ + cw) * vf)
                       : r_cv_lrha_o + spd * (lrha_trad_o - r_cv_lrha_o);
    double cv_lrha_c = lrha_c_raw;
    double cv_lrha_h = std::max({cv_lr_h, new_lrha_o, cv_lrha_c});
    double cv_lrha_l = std::min({cv_lr_l, new_lrha_o, cv_lrha_c});

    sc.Subgraph[SG_LRHA_CLOSE_PREV][idx] = (float)cv_lrha_c;

    // Update persistent state
    r_fha_vol_est = new_vol_est; r_fha_vol_err = new_vol_err;
    r_fha_pvp_est = new_pvp_est; r_fha_pvp_err = new_pvp_err;
    r_fha_pvp_var = new_pvp_var;
    r_cv_ha_o   = new_cv_ha_o;
    r_cv_lrha_o = new_lrha_o;

    // Select candle type
    switch (candle_type) {
    case CT_HEIKIN_ASHI:  out.cv_o = new_cv_ha_o; out.cv_h = cv_ha_h; out.cv_l = cv_ha_l; out.cv_c = cv_ha_c; break;
    case CT_R2_ADAPTIVE:  out.cv_o = cv_ra_o; out.cv_h = cv_ra_h; out.cv_l = cv_ra_l; out.cv_c = cv_ra_c; break;
    case CT_LINREG_HA:    out.cv_o = new_lrha_o; out.cv_h = cv_lrha_h; out.cv_l = cv_lrha_l; out.cv_c = cv_lrha_c; break;
    case CT_LINREG:       out.cv_o = cv_lr_o; out.cv_h = cv_lrc_h; out.cv_l = cv_lrc_l; out.cv_c = cv_lr_c; break;
    default:              out.cv_o = o; out.cv_h = h; out.cv_l = l; out.cv_c = c; break;
    }

    // ── CVD Border ────────────────────────────────────────────────────────────
    // [v9.3 OF-1] Real bid/ask aggressor delta when the chart carries it —
    // sc.AskVolume = trades at ask or higher, sc.BidVolume = at bid or lower.
    // Per-bar fallback to the Pine geometric proxy where the split is absent
    // (backfilled segments); the sign convention and downstream units
    // (cvd_net sign, cvd_agg = |net|/vol*100, percent rank) are identical.
    double ba_ask = (double)sc.AskVolume[idx];
    double ba_bid = (double)sc.BidVolume[idx];
    bool   ba_ok  = use_true_delta && (ba_ask + ba_bid) > 0.0;
    out.delta_is_real = ba_ok;

    double tw    = h - std::max(o, c);
    double bw    = std::min(o, c) - l;
    double body  = std::abs(c - o);
    double denom = std::max(tw + bw + body, 1e-10);
    double base  = 0.5 * (tw + bw) / denom;
    double extra = body / denom;

    double cvd_up  = ba_ok ? ba_ask : vol * std::max(base + (o <= c ? extra : 0.0), 0.5);
    double cvd_dn  = ba_ok ? ba_bid : vol * std::max(base + (o >  c ? extra : 0.0), 0.5);
    out.cvd_net    = cvd_up - cvd_dn;
    double cvd_agg = (vol > 0.0) ? (std::abs(out.cvd_net) / vol * 100.0) : 0.0;

    // [v9.3] Per-bar delta history for the order-flow gates: signed delta in
    // Arrays[1], real-data flag in Arrays[2], windowed cumulative delta (the
    // change in session CVD over div_lookback bars — stateless, so tick-
    // idempotent and full-recalc deterministic) in Arrays[3].
    sc.Subgraph[SG_CVD_STORE].Arrays[1][idx] = (float)out.cvd_net;
    sc.Subgraph[SG_CVD_STORE].Arrays[2][idx] = ba_ok ? 1.0f : 0.0f;
    {
        double cw_sum = 0.0;
        int L = std::max(div_lookback, 1);
        for (int i = 0; i < L && i <= idx; i++)
            cw_sum += (double)sc.Subgraph[SG_CVD_STORE].Arrays[1][idx - i];
        out.cvd_window = cw_sum;
        sc.Subgraph[SG_CVD_STORE].Arrays[3][idx] = (float)cw_sum;
    }

    // Store cvd_agg for percent rank
    sc.Subgraph[SG_CVD_STORE][idx] = (float)cvd_agg;

    // Percent rank of cvd_agg over CVD_LOOKBACK bars. Pine ta.percentrank is
    // na until the full window exists (na comparisons are false downstream) —
    // partial windows here produced phantom 100% ranks on early bars.
    out.cvd_rank = 0.0;
    if (idx >= CVD_LOOKBACK) {
        int count = 0;
        for (int i = 1; i <= CVD_LOOKBACK; i++) {
            // Pine ta.percentrank counts values less than OR EQUAL — ties are
            // structural here (dojis give agg==0.0, marubozus agg==50.0)
            if ((double)sc.Subgraph[SG_CVD_STORE][idx - i] <= cvd_agg) count++;
        }
        out.cvd_rank = 100.0 * (double)count / (double)CVD_LOOKBACK;
    }

    // Rank history for the info panel's 3-bar Delta Strength average
    sc.Subgraph[SG_CVD_STORE].Arrays[0][idx] = (float)out.cvd_rank;

    out.cvd_strong = (cvd_enable && out.cvd_rank >= cvd_strong_thresh);

    // Candle colors (Pine: strong-delta bodies fade to 55 transp = 45% opacity)
    bool bull_candle = (out.cv_c >= out.cv_o);
    COLORREF bull_c = out.cvd_strong ? theme_color_dim(theme.bull, 45, theme.bg) : theme.bull;
    COLORREF bear_c = out.cvd_strong ? theme_color_dim(theme.bear, 45, theme.bg) : theme.bear;
    out.body_color   = bull_candle ? bull_c : bear_c;
    out.border_color = (cvd_enable && out.cvd_strong) ? theme.hilight
                     : (use_custom ? out.body_color : RGB(0, 0, 0));

    // Write to candle subgraphs (paired wick + body draw styles).
    // Sierra has no candle border, so the Pine "Delta Border Highlight" is
    // rendered on the wick: strong-delta bars get the theme highlight color
    // on High/Low while the body is dimmed (matching Pine's faded body).
    if (use_custom) {
        sc.Subgraph[SG_CANDLE_OPEN][idx]  = (float)out.cv_o;
        sc.Subgraph[SG_CANDLE_HIGH][idx]  = (float)out.cv_h;
        sc.Subgraph[SG_CANDLE_LOW][idx]   = (float)out.cv_l;
        sc.Subgraph[SG_CANDLE_CLOSE][idx] = (float)out.cv_c;
        sc.Subgraph[SG_CANDLE_OPEN].DataColor[idx]  = out.body_color;
        sc.Subgraph[SG_CANDLE_CLOSE].DataColor[idx] = out.body_color;
        sc.Subgraph[SG_CANDLE_HIGH].DataColor[idx]  = out.border_color;
        sc.Subgraph[SG_CANDLE_LOW].DataColor[idx]   = out.border_color;
    }

    return out;
}
