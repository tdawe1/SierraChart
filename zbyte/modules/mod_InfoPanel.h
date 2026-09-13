#pragma once
// mod_InfoPanel.h — Info Panel + Watermark (Pine TA9:1394-1546, 1620-1625)
//
// Faithful port of Pine's 2-column x 25-row table: five section headers with
// background bars, label column + right-aligned value column, per-cell value
// colors. Rendered as a grid of DRAWING_TEXT tools (one per cell, LineNumbers
// INFO_PANEL_BASE_ID + row*2 + col), positioned with relative coordinates:
// BeginDateTime = bar column from the left edge of the VISIBLE window,
// BeginValue = percent of region height (UseRelativeVerticalValues).
// Redrawn only on the last bar; purged on toggle-off, full recalc and study
// removal from TrendArchitect.cpp.

#include <cstdio>   // snprintf — do not rely on sierrachart.h pulling it in
#include <cstring>
#include "../include/TA_DrawingIDs.h"
#include "../include/TA_ColorThemes.h"

// Data bag aggregated by TrendArchitect.cpp on the last bar
struct InfoPanelData {
    // ── Trend State ──────────────────────────────────────────────────────────
    bool   bias_bull;            // prism_cur_dir == -1
    bool   st_bull;              // cma1 > cma2
    bool   mt_bull;              // tc_k20 rising
    bool   lt_bull;              // tc_base rising

    // ── Momentum & Quality ───────────────────────────────────────────────────
    double delta_avg;            // 3-bar average of cvd_rank
    double srsi_k, srsi_k_prev;  // StochRSI-K now / previous bar
    double sc_cco;               // CCO oscillator (0..100)
    double er;                   // PRISM ER(14)

    // ── Trend Regime ─────────────────────────────────────────────────────────
    bool   suppress_sell, suppress_buy;
    double hurst;
    double ka_bull_pct, ka_bear_pct;
    double ka_thresh;            // effective alignment threshold (0..1)
    double accel_smooth;
    bool   tc_base_rising;

    // ── PRISM Adaptive ───────────────────────────────────────────────────────
    bool   adaptive_on, ns_on;
    int    prism_len, prism_base_input;      // effective vs input base length
    int    st1_per, st2_per, st1_per_input;  // effective rails vs input
    double er_smo;                           // smoothed ER (NS)

    // ── Auto-Optimizer ───────────────────────────────────────────────────────
    bool   ao_enabled, ao_active;            // active = weights resolved
    int    ao_len_s, ao_len_m, ao_len_l, ao_eff_len;
    double ao_avg_s, ao_avg_m, ao_avg_l;
    double ao_w_s, ao_w_m, ao_w_l;           // blend weights (score color pick)
    int    ao_sz_s, ao_sz_m, ao_sz_l;
    int    ao_lookback;
};

static void info_delete_all(SCStudyInterfaceRef sc)
{
    for (int id = INFO_PANEL_BASE_ID; id <= INFO_PANEL_END_ID; id++)
        sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, id);
}

// One grid cell as a text tool. bg_used=0 draws text on a filled cell.
static void info_cell(
    SCStudyInterfaceRef sc,
    int row, int col,
    double vpos_pct, int bar_col,
    const char* text, COLORREF text_color,
    bool right_align,
    bool solid_bg, COLORREF bg_color)
{
    s_UseTool t;
    t.ChartNumber   = sc.ChartNumber;
    t.DrawingType   = DRAWING_TEXT;
    t.Region        = sc.GraphRegion;
    t.LineNumber    = INFO_PANEL_BASE_ID + row * 2 + col;
    t.BeginDateTime = bar_col;
    t.BeginValue    = (float)vpos_pct;
    t.UseRelativeVerticalValues = 1;
    t.Text          = text;
    t.Color         = text_color;
    t.FontSize      = 8;
    t.FontBold      = 0;
    t.FontFace      = "Consolas";
    t.TextAlignment = (right_align ? DT_RIGHT : DT_LEFT) | DT_TOP;
    if (solid_bg) {
        t.FontBackColor = bg_color;
        t.TransparentLabelBackground = 0;
    } else {
        t.TransparentLabelBackground = 1;
    }
    t.AddMethod = UTAM_ADD_OR_ADJUST;
    sc.UseTool(t);
}

inline void compute_InfoPanel(
    SCStudyInterfaceRef sc,
    const ThemeProfile& theme,
    const InfoPanelData& d,
    int location_mode)   // 0=TL, 1=ML, 2=BL, 3=MR, 4=BR (matches IN_INFO_LOCATION)
{
    // ── Pine's derived state strings and colors (TA9:1400-1431) ─────────────
    bool mom_rising = d.srsi_k > d.srsi_k_prev;
    const char* mom_state =
        (d.srsi_k >= 80.0 && mom_rising)  ? "Rising & OB"  :
        (d.srsi_k <= 20.0 && !mom_rising) ? "Falling & OS" :
        mom_rising                        ? "Rising"       : "Falling";

    const char* vol_state =
        d.sc_cco >= 75.0 ? "Overbought"      :
        d.sc_cco >= 60.0 ? "Near Overbought" :
        d.sc_cco >  40.0 ? "Normal Range"    :
        d.sc_cco >  25.0 ? "Near Oversold"   : "Oversold";

    const char* er_state =
        d.er >= 0.60 ? "Strong Trend" :
        d.er >= 0.45 ? "Trending"     :
        d.er >= 0.30 ? "Mixed"        :
        d.er >= 0.15 ? "Noisy"        : "Very Noisy";

    // Panel palette: the panel sits on its own dark backdrop like Pine's table
    // (black at 22% opacity over the chart), so text blends toward that backdrop
    COLORREF cell_bg  = theme_color_dim(RGB(0, 0, 0), 22, theme.bg);
    COLORREF hdr_bg   = theme_color_dim(theme.neutral, 18, theme.bg);
    COLORREF lbl      = theme_color_dim(RGB(255, 255, 255), 50, cell_bg);
    COLORREF hdr_text = theme_color_dim(RGB(255, 255, 255), 95, hdr_bg);
    COLORREF bull_v   = theme.bull;
    COLORREF bear_v   = theme.bear;
    COLORREF neut_v   = theme_color_dim(RGB(255, 255, 255), 80, cell_bg);
    COLORREF hi_v     = theme.hilight;
    COLORREF dim_bull = theme_color_dim(theme.bull, 60, cell_bg);
    COLORREF dim_bear = theme_color_dim(theme.bear, 60, cell_bg);

    COLORREF mom_col =
        (std::strcmp(mom_state, "Rising & OB") == 0)  ? bear_v :
        (std::strcmp(mom_state, "Falling & OS") == 0) ? bull_v :
        mom_rising ? bull_v : bear_v;
    COLORREF vol_col =
        d.sc_cco >= 75.0 ? bear_v : d.sc_cco >= 60.0 ? dim_bear :
        d.sc_cco >  40.0 ? neut_v : d.sc_cco >  25.0 ? dim_bull : bull_v;
    COLORREF er_col =
        d.er >= 0.60 ? hi_v   : d.er >= 0.45 ? bull_v :
        d.er >= 0.30 ? neut_v : d.er >= 0.15 ? dim_bear : bear_v;
    COLORREF delta_col =
        d.delta_avg >= 80.0 ? hi_v : d.delta_avg >= 60.0 ? bull_v :
        d.delta_avg >= 40.0 ? neut_v : dim_bear;

    // Trend Regime (TA9:1455-1473)
    const char* trg_regime = d.suppress_sell ? "Bull Regime"
                           : d.suppress_buy  ? "Bear Regime" : "No Regime";
    COLORREF trg_regime_col = d.suppress_sell ? bull_v
                            : d.suppress_buy  ? bear_v : neut_v;

    const char* hurst_str = d.hurst > 0.55 ? "Trending"
                          : d.hurst < 0.45 ? "Mean Reverting" : "Mixed";
    COLORREF hurst_col = d.hurst > 0.55 ? hi_v : d.hurst < 0.45 ? bear_v : neut_v;

    double ka_pct = std::max(d.ka_bull_pct, d.ka_bear_pct);
    const char* ka_str = ka_pct >= 0.78 ? "High" : ka_pct >= 0.56 ? "Moderate" : "Low";
    COLORREF ka_col = (ka_pct >= d.ka_thresh) ? hi_v : neut_v;

    const char* accel_str = d.accel_smooth >  0.001 ? "Accelerating"
                          : d.accel_smooth < -0.001 ? "Decelerating" : "Steady";
    COLORREF accel_col =
        (d.accel_smooth >  0.001 && d.tc_base_rising)  ? bull_v :
        (d.accel_smooth < -0.001 && !d.tc_base_rising) ? bear_v :
        d.accel_smooth >  0.001 ? dim_bull :
        d.accel_smooth < -0.001 ? dim_bear : neut_v;

    // PRISM Adaptive (TA9:1487-1500)
    const char* adapt_mode = (d.adaptive_on && d.ns_on) ? "Adaptive + NS"
                           : d.adaptive_on ? "Adaptive"
                           : d.ns_on ? "Noise Supp" : "Fixed";
    COLORREF adapt_mode_col = (d.adaptive_on || d.ns_on) ? bull_v : neut_v;
    COLORREF adapt_len_col = d.prism_len > d.prism_base_input ? hi_v
                           : d.prism_len == d.prism_base_input ? neut_v : dim_bull;
    COLORREF adapt_rails_col = d.st1_per > d.st1_per_input ? hi_v : neut_v;
    COLORREF adapt_er_col = d.er_smo >= 0.5 ? bull_v : d.er_smo >= 0.2 ? neut_v : bear_v;

    // Auto-Optimizer (TA9:1516-1543)
    const char* ao_status = d.ao_enabled ? (d.ao_active ? "Active" : "Warming Up") : "Disabled";
    COLORREF ao_status_col = d.ao_enabled ? (d.ao_active ? bull_v : dim_bull) : neut_v;
    COLORREF ao_scores_col = (d.ao_w_l >= d.ao_w_m && d.ao_w_l >= d.ao_w_s) ? hi_v
                           : (d.ao_w_s >= d.ao_w_m) ? dim_bull : neut_v;
    int ao_adj = d.ao_eff_len - d.ao_len_m;
    COLORREF ao_adj_col = ao_adj > 0 ? hi_v : ao_adj < 0 ? dim_bull : neut_v;
    COLORREF ao_sigs_col = (std::min(d.ao_sz_s, std::min(d.ao_sz_m, d.ao_sz_l)) >= d.ao_lookback)
                         ? bull_v : dim_bull;

    // ── Geometry ─────────────────────────────────────────────────────────────
    // ACSIL relative horizontal positioning: with a small integer (1..150)
    // assigned to BeginDateTime, Sierra anchors the drawing to the VISIBLE
    // window on a fixed 1-150 scale (1 = left edge, 150 = right edge) —
    // zoom- and scroll-independent, exactly like Pine's screen-anchored table.
    bool right_side = (location_mode >= 3);
    int col_lbl = right_side ? 124 : 2;
    int col_val = right_side ? 147 : 25;

    double top_pct;
    switch (location_mode) {
        case 0: default: top_pct = 98.0; break;  // Top Left
        case 1:          top_pct = 79.0; break;  // Middle Left
        case 2:          top_pct = 62.0; break;  // Bottom Left
        case 3:          top_pct = 79.0; break;  // Middle Right
        case 4:          top_pct = 62.0; break;  // Bottom Right
    }
    const double vstep = 2.3;   // percent of region height per row

    char buf[64];
    auto rowy = [&](int row) { return top_pct - vstep * (double)row; };

    auto hdr = [&](int row, const char* title) {
        info_cell(sc, row, 0, rowy(row), col_lbl, title, hdr_text, false, true, hdr_bg);
        info_cell(sc, row, 1, rowy(row), col_val, " ",   hdr_text, true,  true, hdr_bg);
    };
    auto kv = [&](int row, const char* label, const char* value, COLORREF vcol) {
        info_cell(sc, row, 0, rowy(row), col_lbl, label, lbl,  false, true, cell_bg);
        info_cell(sc, row, 1, rowy(row), col_val, value, vcol, true,  true, cell_bg);
    };

    // ── TREND STATE ──────────────────────────────────────────────────────────
    hdr(0, " TREND STATE");
    kv(1, " Immediate Bias", d.bias_bull ? "Bullish" : "Bearish", d.bias_bull ? bull_v : bear_v);
    kv(2, " Short Term",     d.st_bull   ? "Bullish" : "Bearish", d.st_bull   ? bull_v : bear_v);
    kv(3, " Medium Term",    d.mt_bull   ? "Bullish" : "Bearish", d.mt_bull   ? bull_v : bear_v);
    kv(4, " Long Term",      d.lt_bull   ? "Bullish" : "Bearish", d.lt_bull   ? bull_v : bear_v);

    // ── MOMENTUM & QUALITY ───────────────────────────────────────────────────
    hdr(5, " MOMENTUM & QUALITY");
    snprintf(buf, sizeof(buf), "%.0f%%", d.delta_avg);
    kv(6, " Delta Strength",   buf,       delta_col);
    kv(7, " Momentum State",   mom_state, mom_col);
    kv(8, " Volatility Range", vol_state, vol_col);
    kv(9, " Trend Efficiency", er_state,  er_col);
    // row 10: spacer (Pine leaves it empty)

    // ── TREND REGIME ─────────────────────────────────────────────────────────
    hdr(11, " TREND REGIME");
    kv(12, " Regime State",     trg_regime, trg_regime_col);
    kv(13, " Trend Alignment",  ka_str,     ka_col);
    kv(14, " Market Character", hurst_str,  hurst_col);
    kv(15, " Trend Momentum",   accel_str,  accel_col);

    // ── PRISM ADAPTIVE ───────────────────────────────────────────────────────
    hdr(16, " PRISM ADAPTIVE");
    kv(17, " Mode", adapt_mode, adapt_mode_col);
    snprintf(buf, sizeof(buf), "%d", d.prism_len);
    kv(18, " Eff. Lookback", buf, adapt_len_col);
    snprintf(buf, sizeof(buf), "%d / %d", d.st1_per, d.st2_per);
    kv(19, " Eff. Rails (a/s)", buf, adapt_rails_col);

    // ── AUTO-OPTIMIZER ───────────────────────────────────────────────────────
    hdr(20, " AUTO-OPTIMIZER");
    kv(21, " Status", ao_status, ao_status_col);
    snprintf(buf, sizeof(buf), "%d / %d / %d", d.ao_len_s, d.ao_len_m, d.ao_len_l);
    kv(22, " Test Lengths", buf, neut_v);
    snprintf(buf, sizeof(buf), "%.2f / %.2f / %.2f", d.ao_avg_s, d.ao_avg_m, d.ao_avg_l);
    kv(23, " Scores (S/M/L)", buf, ao_scores_col);
    // Pine prints '0' without a sign (str.tostring), '+N' only when positive
    snprintf(buf, sizeof(buf), "%s%d", (ao_adj > 0 ? "+" : ""), ao_adj);
    kv(24, " Length Adjustment", buf, ao_adj_col);
}

// ── Watermark (Pine TA9:1620-1625: 1x1 table, top right, HILIGHT 55 transp) ──
inline void compute_Watermark(SCStudyInterfaceRef sc, const ThemeProfile& theme)
{
    s_UseTool t;
    t.ChartNumber   = sc.ChartNumber;
    t.DrawingType   = DRAWING_TEXT;
    t.Region        = sc.GraphRegion;
    t.LineNumber    = WATERMARK_ID;
    t.BeginDateTime = 149;   // fixed 1-150 relative scale, flush right
    t.BeginValue    = 99.0f;
    t.UseRelativeVerticalValues = 1;
    t.Text          = "Trend Architect v9.1";
    t.Color         = theme_color_dim(theme.hilight, 45, theme.bg);
    t.FontSize      = 10;
    t.FontBold      = 0;
    t.TextAlignment = DT_RIGHT | DT_TOP;
    t.TransparentLabelBackground = 1;
    t.AddMethod     = UTAM_ADD_OR_ADJUST;
    sc.UseTool(t);
}
