#pragma once
// mod_BoundaryForecast.h — Section 8: Boundary Forecast (Pine TA9:1220-1300,
// drawing block 1552-1618)
//
// Projection math (exactly Pine):
//   TC bands — 'Slope Extension': two-point slope (x - x[lb-1]) / (lb-1)
//              'Regression':      LSMA slope = linreg(x,lb,0) - linreg(x,lb,1)
//              projected linearly from the CURRENT value either way
//   SC bands — 'Slope Extension': two-point slope, linear projection
//              'Regression':      cubic fit over lb bars, refit every 3 bars
//              with held coefficients (rollback state), evaluated at
//              x = 1 + n/(lb-1)
// Fades (Pine transparencies -> opacity toward chart bg):
//   TC base line 15..70 -> 85..30%, TC top 35..80 -> 65..20%,
//   TC fill 75..95 -> 25..5%; SC lines 15..70 -> 85..30%, SC fill 88..97 -> 12..3%
// Styles: TC base solid w2, TC top dotted w1 (NEUT); SC dashed w2 (BULL top /
// BEAR bot); fills approximated with rectangle highlights (NEUT).
//
// Drawings are created ONLY on the last bar; the SC cubic refit runs EVERY bar
// (Pine's bar_index % 3 throttle with held var coefficients).

#include "../include/TA_Subgraphs.h"
#include "../include/TA_Inputs.h"
#include "../include/TA_DrawingIDs.h"
#include "../include/TA_Algorithms.h"
#include "../include/TA_ColorThemes.h"
#include "../include/TA_State.h"

// Maximum forecast bars supported (matches LineNumber range allocation)
static const int FC_MAX_BARS = 20;

// ── Helper: draw one forecast segment ────────────────────────────────────────
static void fc_draw_segment(
    SCStudyInterfaceRef sc,
    int   line_id,
    SCDateTime beginDT,
    SCDateTime endDT,
    double begin_val,
    double end_val,
    COLORREF color,
    int    width,
    int    line_style)   // LINESTYLE_SOLID / _DOT / _DASH
{
    s_UseTool t;
    t.ChartNumber   = sc.ChartNumber;
    t.DrawingType   = DRAWING_LINE;
    t.Region        = sc.GraphRegion;
    t.LineNumber    = line_id;
    t.BeginDateTime = beginDT;
    t.EndDateTime   = endDT;
    t.BeginValue    = (float)begin_val;
    t.EndValue      = (float)end_val;
    t.Color         = color;
    t.LineWidth     = width;
    t.LineStyle     = (SubgraphLineStyles)line_style;
    t.AddMethod     = UTAM_ADD_OR_ADJUST;
    sc.UseTool(t);
}

// ── Helper: fill approximation — one rectangle per segment ──────────────────
// The Pine linefill fade is carried ENTIRELY by the rectangle's per-drawing
// TransparencyLevel (color stays the raw theme color) — pre-blending the color
// AND setting a transparency would apply the fade twice.
static void fc_draw_fill(
    SCStudyInterfaceRef sc,
    int   rect_id,
    SCDateTime beginDT,
    SCDateTime endDT,
    double top_a, double top_b,
    double bot_a, double bot_b,
    COLORREF color,
    int    transparency)   // Pine transparency 0..100
{
    if (transparency < 0) transparency = 0;
    if (transparency > 100) transparency = 100;
    s_UseTool t;
    t.ChartNumber   = sc.ChartNumber;
    t.DrawingType   = DRAWING_RECTANGLEHIGHLIGHT;
    t.Region        = sc.GraphRegion;
    t.LineNumber    = rect_id;
    t.BeginDateTime = beginDT;
    t.EndDateTime   = endDT;
    t.BeginValue    = (float)((top_a + top_b) / 2.0);
    t.EndValue      = (float)((bot_a + bot_b) / 2.0);
    t.Color         = color;
    t.SecondaryColor = color;
    t.LineWidth     = 0;
    t.TransparencyLevel = (short)transparency;
    t.AddMethod     = UTAM_ADD_OR_ADJUST;
    sc.UseTool(t);
}

// ── Helper: delete every forecast drawing ────────────────────────────────────
static void fc_delete_all(SCStudyInterfaceRef sc)
{
    for (int id = FC_TC_BASE_ID; id <= FC_TC_END_ID; ++id)
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, id);
    for (int id = FC_SC_BASE_ID; id <= FC_SC_END_ID; ++id)
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, id);
    for (int id = FC_TC_FILL_BASE_ID; id <= FC_TC_FILL_END_ID; ++id)
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, id);
    for (int id = FC_SC_FILL_BASE_ID; id <= FC_SC_FILL_END_ID; ++id)
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, id);
}

// ── LSMA endpoint (Pine ta.linreg(src, len, offset)) over subgraph history ──
// Fits y = a + b*x over the last `len` values (x = 0 oldest .. len-1 newest)
// and evaluates at x = len - 1 - offset.
static double fc_linreg_at(SCStudyInterfaceRef sc, int sg_idx, int idx, int len, int offset)
{
    int blen = std::min(len, idx + 1);
    if (blen < 2) return (double)sc.Subgraph[sg_idx][idx];
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    for (int i = 0; i < blen; i++) {
        double x = (double)i;                                    // oldest = 0
        double y = (double)sc.Subgraph[sg_idx][idx - (blen - 1 - i)];
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    double n = (double)blen;
    double denom = n * sxx - sx * sx;
    double b = (std::abs(denom) > 1e-10) ? ((n * sxy - sx * sy) / denom) : 0.0;
    double a = (sy - b * sx) / n;
    return a + b * (double)(blen - 1 - offset);
}

// ── Main entry — called EVERY bar (drawings only materialize on the last) ───
inline void compute_BoundaryForecast(
    SCStudyInterfaceRef sc,
    TAState& tas,
    const ThemeProfile& theme,
    bool   enabled,
    int    fc_bars,          // IN_FC_BARS (1..20)
    int    reg_len,          // IN_FC_REG_LEN (10..50)
    bool   use_regression,   // IN_FC_MODE index 0 = 'Regression'
    bool   show_tc,
    bool   show_sc,
    double tc_base,
    double tc_cloud_top)
{
    int idx = sc.Index;
    TARecurrence& st = tas.working;

    reg_len = std::max(2, std::min(reg_len, 250));
    fc_bars = std::max(1, std::min(fc_bars, FC_MAX_BARS));

    // ── SC cubic refit — every bar at bar_index % 3 == 0 with held coeffs ───
    // (Pine TA9:1295-1299; the fit itself is Pine f_cubicFit == degree-3
    // poly_regression with x normalized 0..1)
    if (idx % 3 == 0 && idx >= reg_len - 1) {
        float* fbuf = ta_scratch(tas.scratch_a, reg_len);
        double qc[5] = {};
        for (int i = 0; i < reg_len; i++) fbuf[i] = sc.Subgraph[SG_SC_TOP][idx - i];
        if (poly_regression(fbuf, reg_len, 3, qc))
            for (int k = 0; k < 4; k++) st.fc_sc_ct[k] = qc[k];
        for (int i = 0; i < reg_len; i++) fbuf[i] = sc.Subgraph[SG_SC_BOT][idx - i];
        if (poly_regression(fbuf, reg_len, 3, qc))
            for (int k = 0; k < 4; k++) st.fc_sc_cb[k] = qc[k];
    }

    // Purge stale drawings when disabled (input change → full recalc → idx 0)
    if (!enabled) {
        if (idx == 0) fc_delete_all(sc);
        return;
    }

    // Drawings only on the last bar
    if (idx != sc.ArraySize - 1) return;

    // Bar period for future timestamps (fractional days). [v9.3] Prefer the
    // median-based per-chart estimate — the single last-bar duration is noisy
    // on tick/volume/range bars and sc.SecondsPerBar is deprecated (0 there).
    double barDT = sc.BaseDateTimeIn[idx].GetAsDouble();
    double period = (tas.est_sec_per_bar > 0.0)
        ? tas.est_sec_per_bar / 86400.0
        : (idx > 0 ? (barDT - sc.BaseDateTimeIn[idx - 1].GetAsDouble()) : 60.0 / 86400.0);

    double lenM1 = (double)(reg_len - 1);

    // ── Build projections [0..fc_bars], [0] = current value ─────────────────
    double tc_b_proj[FC_MAX_BARS + 1] = {};
    double tc_t_proj[FC_MAX_BARS + 1] = {};
    double sc_t_proj[FC_MAX_BARS + 1] = {};
    double sc_b_proj[FC_MAX_BARS + 1] = {};

    if (show_tc) {
        double b_hist = (idx >= reg_len - 1)
            ? (double)sc.Subgraph[SG_TC_BASE][idx - (reg_len - 1)] : tc_base;
        double t_hist = (idx >= reg_len - 1)
            ? (double)sc.Subgraph[SG_TC_TOP][idx - (reg_len - 1)] : tc_cloud_top;
        double b_slope, t_slope;
        if (use_regression) {
            // Pine: linreg(x, lb, 0) - linreg(x, lb, 1) == fitted LSMA slope
            b_slope = fc_linreg_at(sc, SG_TC_BASE, idx, reg_len, 0)
                    - fc_linreg_at(sc, SG_TC_BASE, idx, reg_len, 1);
            t_slope = fc_linreg_at(sc, SG_TC_TOP, idx, reg_len, 0)
                    - fc_linreg_at(sc, SG_TC_TOP, idx, reg_len, 1);
        } else {
            b_slope = (tc_base      - b_hist) / lenM1;
            t_slope = (tc_cloud_top - t_hist) / lenM1;
        }
        for (int n = 0; n <= fc_bars; n++) {
            tc_b_proj[n] = tc_base      + b_slope * (double)n;
            tc_t_proj[n] = tc_cloud_top + t_slope * (double)n;
        }
    }

    if (show_sc) {
        double sc_top_now = (double)sc.Subgraph[SG_SC_TOP][idx];
        double sc_bot_now = (double)sc.Subgraph[SG_SC_BOT][idx];
        if (use_regression) {
            // Cubic held coefficients evaluated beyond the window
            sc_t_proj[0] = sc_top_now;
            sc_b_proj[0] = sc_bot_now;
            for (int n = 1; n <= fc_bars; n++) {
                double x = 1.0 + (double)n / lenM1;
                sc_t_proj[n] = eval_poly(x, st.fc_sc_ct, 3);
                sc_b_proj[n] = eval_poly(x, st.fc_sc_cb, 3);
            }
        } else {
            double t_hist = (idx >= reg_len - 1)
                ? (double)sc.Subgraph[SG_SC_TOP][idx - (reg_len - 1)] : sc_top_now;
            double b_hist = (idx >= reg_len - 1)
                ? (double)sc.Subgraph[SG_SC_BOT][idx - (reg_len - 1)] : sc_bot_now;
            double t_slope = (sc_top_now - t_hist) / lenM1;
            double b_slope = (sc_bot_now - b_hist) / lenM1;
            for (int n = 0; n <= fc_bars; n++) {
                sc_t_proj[n] = sc_top_now + t_slope * (double)n;
                sc_b_proj[n] = sc_bot_now + b_slope * (double)n;
            }
        }
    }

    // ── Draw segments with Pine's fade ramps ─────────────────────────────────
    for (int n = 1; n <= fc_bars; n++) {
        double t_frac = (fc_bars > 1) ? ((double)(n - 1) / (double)(fc_bars - 1)) : 0.0;
        SCDateTime t0(barDT + period * (double)(n - 1));
        SCDateTime t1(barDT + period * (double)n);
        int i = n - 1;

        if (show_tc) {
            int base_op = (int)std::lround(85.0 - t_frac * 55.0);  // transp 15->70
            int top_op  = (int)std::lround(65.0 - t_frac * 45.0);  // transp 35->80
            int fill_tr = (int)std::lround(75.0 + t_frac * 20.0);  // Pine transp 75->95
            fc_draw_segment(sc, FC_TC_BASE_ID + i, t0, t1,
                tc_b_proj[n - 1], tc_b_proj[n],
                theme_color_dim(theme.neutral, base_op, theme.bg), 2, LINESTYLE_SOLID);
            fc_draw_segment(sc, FC_TC_BASE_ID + 20 + i, t0, t1,
                tc_t_proj[n - 1], tc_t_proj[n],
                theme_color_dim(theme.neutral, top_op, theme.bg), 1, LINESTYLE_DOT);
            fc_draw_fill(sc, FC_TC_FILL_BASE_ID + i, t0, t1,
                tc_t_proj[n - 1], tc_t_proj[n], tc_b_proj[n - 1], tc_b_proj[n],
                theme.neutral, fill_tr);
        }

        if (show_sc) {
            int line_op = (int)std::lround(85.0 - t_frac * 55.0);  // transp 15->70
            int fill_tr = (int)std::lround(88.0 + t_frac * 9.0);   // Pine transp 88->97
            // Width 1 keeps the dash visible — Sierra renders non-solid line
            // styles as solid whenever LineWidth > 1 (deviation: Pine uses w2)
            fc_draw_segment(sc, FC_SC_BASE_ID + i, t0, t1,
                sc_t_proj[n - 1], sc_t_proj[n],
                theme_color_dim(theme.bull, line_op, theme.bg), 1, LINESTYLE_DASH);
            fc_draw_segment(sc, FC_SC_BASE_ID + 20 + i, t0, t1,
                sc_b_proj[n - 1], sc_b_proj[n],
                theme_color_dim(theme.bear, line_op, theme.bg), 1, LINESTYLE_DASH);
            fc_draw_fill(sc, FC_SC_FILL_BASE_ID + i, t0, t1,
                sc_t_proj[n - 1], sc_t_proj[n], sc_b_proj[n - 1], sc_b_proj[n],
                theme.neutral, fill_tr);
        }
    }

    // Remove leftovers when the horizon or toggles shrink
    for (int i = fc_bars; i < FC_MAX_BARS; i++) {
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, FC_TC_BASE_ID + i);
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, FC_TC_BASE_ID + 20 + i);
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, FC_TC_FILL_BASE_ID + i);
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, FC_SC_BASE_ID + i);
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, FC_SC_BASE_ID + 20 + i);
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, FC_SC_FILL_BASE_ID + i);
    }
    if (!show_tc)
        for (int i = 0; i < FC_MAX_BARS; i++) {
            sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, FC_TC_BASE_ID + i);
            sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, FC_TC_BASE_ID + 20 + i);
            sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, FC_TC_FILL_BASE_ID + i);
        }
    if (!show_sc)
        for (int i = 0; i < FC_MAX_BARS; i++) {
            sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, FC_SC_BASE_ID + i);
            sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, FC_SC_BASE_ID + 20 + i);
            sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, FC_SC_FILL_BASE_ID + i);
        }
}
