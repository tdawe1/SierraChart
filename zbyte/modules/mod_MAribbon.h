#pragma once
// mod_MAribbon.h — Section 3: Moving Average Ribbon
// Computes two ALMA lines (offset 0.85 and 0.77) and ribbon fill color.
// Call after reading inputs; writes to SG_ALMA1, SG_ALMA2 and the
// SG_RIBBON_FILL_TOP/BOT fill pair.

#include "../include/TA_Subgraphs.h"
#include "../include/TA_Inputs.h"
#include "../include/TA_Algorithms.h"
#include "../include/TA_ColorThemes.h"
#include "../include/TA_State.h"

struct MAribbonResult {
    double cma1;
    double cma2;
    bool   ribbon_bull;   // cma1 > cma2
    COLORREF line1_color;
    COLORREF line2_color;
    COLORREF fill_color;
};

// Pine computes the ribbon series unconditionally — the enable input only
// gates the PLOTS. PRISM Structure Lock and the ribbon-crossover alerts need
// cma1/cma2 (and their history) even when the ribbon is hidden, so this is
// always called; `display` gates the visible subgraphs. History for [1]-style
// reads lives in hidden extra arrays: SG_ALMA1.Arrays[0] / SG_ALMA2.Arrays[0].
inline MAribbonResult compute_MAribbon(
    SCStudyInterfaceRef sc,
    TAState& tas,
    const ThemeProfile& theme,
    int length,
    double offset1,   // Pine hardcodes 0.85
    double offset2,   // Pine hardcodes 0.77
    double sigma,     // Pine hardcodes 6.0
    bool dir_color,
    bool bicolor,
    bool display)
{
    MAribbonResult out = {};
    int idx = sc.Index;

    // Build float buffer for ALMA (most recent first)
    int avail = std::min(idx + 1, length);
    if (avail < 2) {
        out.cma1 = (double)sc.Close[idx];
        out.cma2 = (double)sc.Close[idx];
        out.ribbon_bull = true;
        out.line1_color = theme.bull;
        out.line2_color = theme.bull;
        out.fill_color  = theme.neutral;
        sc.Subgraph[SG_ALMA1].Arrays[0][idx] = (float)out.cma1;
        sc.Subgraph[SG_ALMA2].Arrays[0][idx] = (float)out.cma2;
        if (display) {
            sc.Subgraph[SG_ALMA1][idx] = (float)out.cma1;
            sc.Subgraph[SG_ALMA2][idx] = (float)out.cma2;
        } else {
            sc.Subgraph[SG_ALMA1][idx] = 0.0f;
            sc.Subgraph[SG_ALMA2][idx] = 0.0f;
            sc.Subgraph[SG_RIBBON_FILL_TOP][idx] = 0.0f;
            sc.Subgraph[SG_RIBBON_FILL_BOT][idx] = 0.0f;
        }
        return out;
    }

    // Fill buffer: buf[0] = most recent close
    // ACSIL: sc.Close[idx - i] for i=0..len-1
    int buflen = std::min(length, idx + 1);
    float* buf = ta_scratch(tas.scratch_a, buflen);
    for (int i = 0; i < buflen; i++) buf[i] = sc.Close[idx - i];

    out.cma1 = f_alma(buf, buflen, offset1, sigma);
    out.cma2 = f_alma(buf, buflen, offset2, sigma);

    // Hidden history (always) — consumed by PRISM + alerts
    sc.Subgraph[SG_ALMA1].Arrays[0][idx] = (float)out.cma1;
    sc.Subgraph[SG_ALMA2].Arrays[0][idx] = (float)out.cma2;

    out.ribbon_bull = (out.cma1 > out.cma2);

    COLORREF dir_col = out.ribbon_bull ? theme.bull : theme.bear;
    out.line1_color  = dir_color ? dir_col : theme.bull;
    out.line2_color  = dir_color ? dir_col : (bicolor ? theme.bear : theme.bull);
    out.fill_color   = out.ribbon_bull ? theme.bull : theme.bear;

    if (display) {
        sc.Subgraph[SG_ALMA1][idx] = (float)out.cma1;
        sc.Subgraph[SG_ALMA2][idx] = (float)out.cma2;
        sc.Subgraph[SG_ALMA1].DataColor[idx] = out.line1_color;
        sc.Subgraph[SG_ALMA2].DataColor[idx] = out.line2_color;

        // Fill between the two lines (adjacent TRANSPARENT_FILL_TOP/BOTTOM
        // pair); Pine fills at 80 transparency = 20% opacity. With the study
        // fill transparency at 80, theme_fill_dim passes the raw color through
        // and Sierra does the real alpha blend over the bars underneath.
        COLORREF fill_blend = theme_fill_dim(out.fill_color, 20, theme.bg);
        sc.Subgraph[SG_RIBBON_FILL_TOP][idx] = (float)out.cma1;
        sc.Subgraph[SG_RIBBON_FILL_BOT][idx] = (float)out.cma2;
        sc.Subgraph[SG_RIBBON_FILL_TOP].DataColor[idx] = fill_blend;
        sc.Subgraph[SG_RIBBON_FILL_BOT].DataColor[idx] = fill_blend;
    } else {
        // Hidden: zeros are skipped by DrawZeros=0
        sc.Subgraph[SG_ALMA1][idx] = 0.0f;
        sc.Subgraph[SG_ALMA2][idx] = 0.0f;
        sc.Subgraph[SG_RIBBON_FILL_TOP][idx] = 0.0f;
        sc.Subgraph[SG_RIBBON_FILL_BOT][idx] = 0.0f;
    }

    return out;
}
