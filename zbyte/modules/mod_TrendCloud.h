#pragma once
// mod_TrendCloud.h — Section 6: Trend Cloud
// Computes 19-level KAMA fan on ohlc4, tc_base (KAMA on close), tc_cloud_top.
// KAMA fan: k05, k10, k15, k20, k25, k30, k35, k40, k45, k50, k55, k60, k65, k70, k75, k80, k85, k90, k100
// tc_base = f_kama_vb(close, 100)

#include "../include/TA_Subgraphs.h"
#include "../include/TA_Inputs.h"
#include "../include/TA_Algorithms.h"
#include "../include/TA_ColorThemes.h"
#include "../include/TA_State.h"

static const int   TC_BASE_LEN = 100;
static const double TC_MULT    = 10.0;

// 19 KAMA fan levels (periods)
static const int KAMA_PERIODS[19] = {5,10,15,20,25,30,35,40,45,50,55,60,65,70,75,80,85,90,100};

struct TrendCloudResult {
    double tc_kama[19];   // fan values k05..k100 (index 0..18)
    double tc_base;       // KAMA(close, 100) — separate from fan (which uses ohlc4)
    double tc_cloud_top;
    double tc_strength;   // |top - base| / atr14
    double tc_k20;        // for medium-term bias
    double tc_k50;        // for elevation criteria
    bool   tc_bull;       // cloud_top > base
};

// Compute KAMA for one period, using persistent double for state
// buf[0]=current, buf[len]=len-bars-ago
// Since we use sc.Subgraph arrays for KAMA storage (per-bar persistence),
// we reference previous bar's value via sc.Subgraph[sg_idx][sc.Index-1]
// Pine f_kama_vb (TA9:385-393) warm-up semantics: `var float nAMA = 0.0` steps
// EVERY bar from the zero seed; while the lookback window is unfilled the na
// efficiency-ratio condition takes the false branch (nefratio = 0), so the
// KAMA crawls up from 0 at the minimum rate 0.0645^2 per bar until bar
// `period`, then turns adaptive. The old 0.5-blend pre-warm converged in ~20
// bars — visually nicer but numerically different from TradingView.
inline double kama_step(
    double src, int period, int idx,
    SCStudyInterfaceRef sc, int sg_idx, int arr_idx)
{
    double prev;
    if (arr_idx < 0)
        prev = (idx > 0) ? (double)sc.Subgraph[sg_idx][idx - 1] : 0.0;
    else
        prev = (idx > 0) ? (double)sc.Subgraph[sg_idx].Arrays[arr_idx][idx - 1] : 0.0;

    // ohlc4 history lives in SG_OHLC4_STORE
    double nefratio = 0.0;
    if (idx >= period) {
        double nsignal = std::abs(src - (double)sc.Subgraph[SG_OHLC4_STORE][idx - period]);
        double nnoise  = 0.0;
        for (int i = 0; i < period; i++) {
            double a = (double)sc.Subgraph[SG_OHLC4_STORE][idx - i];
            double b = (double)sc.Subgraph[SG_OHLC4_STORE][idx - i - 1];
            nnoise += std::abs(a - b);
        }
        nefratio = (nnoise != 0.0) ? nsignal / nnoise : 0.0;
    }
    double nsmooth = nefratio * (0.666 - 0.0645) + 0.0645;
    return prev + nsmooth * nsmooth * (src - prev);
}

// KAMA step for close-based tc_base (same Pine crawl-from-zero semantics)
inline double kama_step_close(double src, int period, int idx, SCStudyInterfaceRef sc)
{
    double prev = (idx > 0) ? (double)sc.Subgraph[SG_TC_BASE][idx - 1] : 0.0;

    double nefratio = 0.0;
    if (idx >= period) {
        double nsignal = std::abs(src - (double)sc.Close[idx - period]);
        double nnoise  = 0.0;
        for (int i = 0; i < period; i++)
            nnoise += std::abs((double)sc.Close[idx - i] - (double)sc.Close[idx - i - 1]);
        nefratio = (nnoise != 0.0) ? nsignal / nnoise : 0.0;
    }
    double nsmooth = nefratio * (0.666 - 0.0645) + 0.0645;
    return prev + nsmooth * nsmooth * (src - prev);
}

// Always called (Pine computes the cloud unconditionally — TA9:395-420; the
// enable/body inputs gate only the plots). Visibility is handled in
// TrendArchitect.cpp by switching the visible subgraphs' DrawStyle at the
// start of each full recalculation, so every value here is written for real:
// PRISM elevation, the Trend Regime Gate, candle coloring and the Boundary
// Forecast all read SG_TC_BASE / SG_TC_K50 / SG_TC_TOP history.
inline TrendCloudResult compute_TrendCloud(
    SCStudyInterfaceRef sc,
    TAState& tas,
    double atr14,
    const ThemeProfile& theme,
    bool slope_color)
{
    TrendCloudResult out = {};
    int idx = sc.Index;
    TARecurrence& st = tas.working;

    double ohlc4 = ((double)sc.Open[idx] + sc.High[idx] + sc.Low[idx] + sc.Close[idx]) / 4.0;
    sc.Subgraph[SG_OHLC4_STORE][idx] = (float)ohlc4;

    // KAMA fan mapping: sg_idx, arr_idx (-1 = use .Data[])
    // SG_KAMA_STORE_A: main=k05, A[0]=k10, A[1]=k15, A[2]=k20, A[3]=k25, A[4]=k30
    // SG_KAMA_STORE_B: main=k35, A[0]=k40, A[1]=k45, A[2]=k50, A[3]=k55, A[4]=k60
    // SG_KAMA_STORE_C: main=k65, A[0]=k70, A[1]=k75, A[2]=k80, A[3]=k85, A[4]=k90

    struct KamaDest { int sg; int arr; };
    static const KamaDest KAMA_DEST[19] = {
        {SG_KAMA_STORE_A, -1}, {SG_KAMA_STORE_A, 0}, {SG_KAMA_STORE_A, 1},
        {SG_KAMA_STORE_A, 2},  {SG_KAMA_STORE_A, 3}, {SG_KAMA_STORE_A, 4},
        {SG_KAMA_STORE_B, -1}, {SG_KAMA_STORE_B, 0}, {SG_KAMA_STORE_B, 1},
        {SG_KAMA_STORE_B, 2},  {SG_KAMA_STORE_B, 3}, {SG_KAMA_STORE_B, 4},
        {SG_KAMA_STORE_C, -1}, {SG_KAMA_STORE_C, 0}, {SG_KAMA_STORE_C, 1},
        {SG_KAMA_STORE_C, 2},  {SG_KAMA_STORE_C, 3}, {SG_KAMA_STORE_C, 4},
    };
    // Note: period 100 is index 18, but we store in SG_KAMA_STORE_C.Arrays[4]=k90 at index 17, k100 at 18
    // Remapping: index 18 = period 100 → SG_KAMA_STORE_C.Arrays[4]  (that's k90 currently)
    // Let me recount: 0=k05,1=k10,...,17=k90,18=k100
    // SG_KAMA_STORE_C: main=k65(12), A[0]=k70(13), A[1]=k75(14), A[2]=k80(15), A[3]=k85(16), A[4]=k90(17)
    // k100(18) has no storage in STORE_C - add SG_KAMA_STORE_D.Arrays[0] for k100 (STORE_D main is free for SC use)
    // Actually SG_KAMA_STORE_D.Arrays[0..4] are used by SuperChannel. Let me use Arrays[1..4] for that
    // and use a completely separate subgraph for k100.
    // Simplest: use SG_TC_K10..SG_TC_K75 for the visible enhancement subgraphs AND for k100 storage,
    // but those are visible enhancement subgraphs. Let's add one more hidden subgraph for k100.
    // I'll just compute k100 using tc_base storage approach below.

    // Compute all 19 KAMA fan values
    for (int i = 0; i < 18; i++) {  // k05..k90 (indices 0..17)
        int period = KAMA_PERIODS[i];
        double kv  = kama_step(ohlc4, period, idx, sc, KAMA_DEST[i].sg, KAMA_DEST[i].arr);
        out.tc_kama[i] = kv;
        if (KAMA_DEST[i].arr < 0)
            sc.Subgraph[KAMA_DEST[i].sg][idx] = (float)kv;
        else
            sc.Subgraph[KAMA_DEST[i].sg].Arrays[KAMA_DEST[i].arr][idx] = (float)kv;
    }

    // k100 on ohlc4 (index 18) — a scalar recurrence, held in the commit/rollback
    // state (doubles). Same Pine crawl-from-zero warm-up as the fan levels.
    double& r_k100 = st.tc_k100;
    {
        const int period = 100;
        double nefratio = 0.0;
        if (idx >= period) {
            double nsignal = std::abs(ohlc4 - (double)sc.Subgraph[SG_OHLC4_STORE][idx - period]);
            double nnoise  = 0.0;
            for (int i = 0; i < period; i++)
                nnoise += std::abs((double)sc.Subgraph[SG_OHLC4_STORE][idx - i] - (double)sc.Subgraph[SG_OHLC4_STORE][idx - i - 1]);
            nefratio = (nnoise != 0.0) ? nsignal / nnoise : 0.0;
        }
        double nsmooth = nefratio * (0.666 - 0.0645) + 0.0645;
        r_k100 = r_k100 + nsmooth * nsmooth * (ohlc4 - r_k100);
    }
    out.tc_kama[18] = r_k100;

    // Export to enhancement visible subgraphs
    sc.Subgraph[SG_TC_K10][idx] = (float)out.tc_kama[1];
    sc.Subgraph[SG_TC_K25][idx] = (float)out.tc_kama[4];
    sc.Subgraph[SG_TC_K50][idx] = (float)out.tc_kama[9];
    sc.Subgraph[SG_TC_K75][idx] = (float)out.tc_kama[14];

    // tc_base = KAMA(close, 100)
    out.tc_base = kama_step_close((double)sc.Close[idx], TC_BASE_LEN, idx, sc);
    sc.Subgraph[SG_TC_BASE][idx] = (float)out.tc_base;

    // Total distance calculation (18 pairs)
    double tc_total_dist = 0.0;
    for (int i = 0; i < 18; i++) {
        double ka = out.tc_kama[i];
        double kb = out.tc_kama[i + 1];
        if (std::abs(kb) > 1e-10) tc_total_dist += (ka - kb) / kb;  // Pine divides unguarded; |..| keeps negative-price symbols working
    }
    double tc_avg_dist = tc_total_dist / 18.0;
    out.tc_cloud_top   = out.tc_base * (1.0 + tc_avg_dist * TC_MULT);

    out.tc_strength = (atr14 > 0.0) ? std::abs(out.tc_cloud_top - out.tc_base) / atr14 : 0.0;
    out.tc_bull     = (out.tc_cloud_top > out.tc_base);
    out.tc_k20      = out.tc_kama[3];
    out.tc_k50      = out.tc_kama[9];

    sc.Subgraph[SG_TC_TOP][idx] = (float)out.tc_cloud_top;

    // Colors
    double tc_transp = std::max(55.0, std::min(90.0, 90.0 - out.tc_strength / 2.0 * 35.0));
    int alpha_pct = (int)(100.0 - tc_transp);  // convert transparency to opaqueness
    // theme_fill_dim: real fill transparency — candles/studies show through the
    // cloud (opacity >20% clamps; see TA_FILL_TRANSPARENCY in TA_ColorThemes.h)
    COLORREF fill_c = out.tc_bull
        ? theme_fill_dim(theme.bull, alpha_pct, theme.bg)
        : theme_fill_dim(theme.bear, alpha_pct, theme.bg);

    COLORREF base_col;
    if (slope_color) {
        double prev_base = (idx > 0) ? (double)sc.Subgraph[SG_TC_BASE][idx - 1] : out.tc_base;
        base_col = (out.tc_base > prev_base) ? theme.bull : theme.bear;
    } else {
        base_col = theme_color_dim(theme.neutral, 80, theme.bg);
    }

    sc.Subgraph[SG_TC_BASE].DataColor[idx]  = base_col;
    sc.Subgraph[SG_TC_TOP].DataColor[idx]   = theme_color_dim(theme.neutral, 80, theme.bg);

    // Glow stack — Pine: 4 plots, widths 10/20/30/45, transparencies 80/90/95/98
    // (widest/faintest at the lowest subgraph index so it draws underneath)
    static const int GLOW_SG[4]      = {SG_TC_GLOW1, SG_TC_GLOW2, SG_TC_GLOW3, SG_TC_GLOW4};
    static const int GLOW_OPACITY[4] = {20, 10, 5, 2};
    for (int g = 0; g < 4; g++) {
        sc.Subgraph[GLOW_SG[g]][idx] = (float)out.tc_base;
        sc.Subgraph[GLOW_SG[g]].DataColor[idx] = theme_color_dim(base_col, GLOW_OPACITY[g], theme.bg);
    }

    // Cloud fill between base and top (adjacent TRANSPARENT_FILL pair).
    // Real values are ALWAYS written — hiding the body zeroed SG_TC_TOP
    // before, which fed the Boundary Forecast regression garbage. Display is
    // gated via DrawStyle in TrendArchitect.cpp instead.
    sc.Subgraph[SG_TC_FILL_TOP][idx] = (float)out.tc_cloud_top;
    sc.Subgraph[SG_TC_FILL_BOT][idx] = (float)out.tc_base;
    sc.Subgraph[SG_TC_FILL_TOP].DataColor[idx] = fill_c;
    sc.Subgraph[SG_TC_FILL_BOT].DataColor[idx] = fill_c;

    return out;
}
