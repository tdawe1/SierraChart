#pragma once
// mod_CandleColoring.h — Section 5B: Candle Coloring
// 6 modes: MA Ribbon, Trend Regime, Dual Confirmation, Adaptive Impulse,
//          Heikin Ashi, Trend Cloud Base.
// Returns a COLORREF to apply to chart candles via barcolor.

#include "../include/TA_Subgraphs.h"
#include "../include/TA_Inputs.h"
#include "../include/TA_Algorithms.h"
#include "../include/TA_ColorThemes.h"
#include "../include/TA_State.h"
#include "mod_TrendRegimeGate.h"

enum CandleColorMode {
    CCM_MA_RIBBON = 0,
    CCM_TREND_REGIME,
    CCM_DUAL_CONFIRMATION,
    CCM_ADAPTIVE_IMPULSE,
    CCM_HEIKIN_ASHI,
    CCM_TREND_CLOUD_BASE
};

inline COLORREF compute_CandleColor(
    SCStudyInterfaceRef sc,
    TAState& tas,
    const ThemeProfile& theme,
    CandleColorMode mode,
    bool ribbon_bull,
    bool tc_base_bull,
    int bull_votes,
    int bear_votes,
    double cv_ha_c, double cv_ha_o,   // for HA mode
    double cma1, double cma2)         // for Adaptive Impulse MACD
{
    int idx = sc.Index;

    switch (mode) {
    case CCM_MA_RIBBON:
        return ribbon_bull ? theme.bull : theme.bear;

    case CCM_TREND_REGIME: {
        // Pine transparencies 25/15 = 75%/85% opacity
        int net = bull_votes - bear_votes;
        if (net >= 2)  return theme.bull;
        if (net == 1)  return theme_color_dim(theme.bull, 75, theme.bg);
        if (net == -1) return theme_color_dim(theme.bear, 75, theme.bg);
        if (net <= -2) return theme.bear;
        return theme_color_dim(theme.neutral, 85, theme.bg);
    }

    case CCM_DUAL_CONFIRMATION:
        if (ribbon_bull && tc_base_bull)   return theme.bull;
        if (!ribbon_bull && !tc_base_bull) return theme.bear;
        return theme_color_dim(theme.neutral, 85, theme.bg);

    case CCM_ADAPTIVE_IMPULSE: {
        // Adaptive Impulse: KAMA(close,13) slope + ALMA MACD histogram.
        // Pine f_kama_vb: crawls from the zero seed at minimum rate until the
        // 13-bar window fills, then adaptive — identical to the TC KAMAs.
        double& r_ai_kama = tas.working.ai_kama;
        {
            const int AI_LEN = 13;
            double nef = 0.0;
            if (idx >= AI_LEN) {
                double nsignal = std::abs((double)sc.Close[idx] - (double)sc.Close[idx - AI_LEN]);
                double nnoise  = 0.0;
                for (int i = 0; i < AI_LEN; i++)
                    nnoise += std::abs((double)sc.Close[idx - i] - (double)sc.Close[idx - i - 1]);
                nef = (nnoise != 0.0) ? nsignal / nnoise : 0.0;
            }
            double nsm = nef * (0.666 - 0.0645) + 0.0645;
            r_ai_kama = r_ai_kama + nsm * nsm * ((double)sc.Close[idx] - r_ai_kama);
        }
        double kama_prev = (idx > 0) ? (double)sc.Subgraph[SG_AI_KAMA_PREV][idx - 1] : r_ai_kama;
        sc.Subgraph[SG_AI_KAMA_PREV][idx] = (float)r_ai_kama;
        bool kama_bull = (r_ai_kama > kama_prev);

        // ALMA MACD histogram: ALMA(12) - ALMA(26), signal = ALMA(9) of macd
        int b12 = std::min(12, idx + 1), b26 = std::min(26, idx + 1);
        float* buf12 = ta_scratch(tas.scratch_b, b12);
        float* buf26 = ta_scratch(tas.scratch_c, b26);
        for (int i = 0; i < b12; i++) buf12[i] = sc.Close[idx - i];
        for (int i = 0; i < b26; i++) buf26[i] = sc.Close[idx - i];
        double fast12 = f_alma(buf12, b12, 0.85, 6.0);
        double slow26 = f_alma(buf26, b26, 0.85, 6.0);
        double macd_val = fast12 - slow26;

        sc.Subgraph[SG_AI_MACD][idx] = (float)macd_val;

        // Signal: ALMA(9) of macd
        int mblen = std::min(9, idx + 1);
        float* mbuf = ta_scratch(tas.scratch_d, mblen);
        for (int i = 0; i < mblen; i++) mbuf[i] = sc.Subgraph[SG_AI_MACD][idx - i];
        double sig9 = f_alma(mbuf, mblen, 0.85, 6.0);
        double hist  = macd_val - sig9;

        bool hist_bull = (idx > 0) ? (hist > (double)sc.Subgraph[SG_AI_HIST][idx - 1]) : true;
        sc.Subgraph[SG_AI_HIST][idx] = (float)hist;

        if (kama_bull && hist_bull)   return theme.bull;
        if (!kama_bull && !hist_bull) return theme.bear;
        return theme_color_dim(theme.neutral, 85, theme.bg);
    }

    case CCM_HEIKIN_ASHI:
        return (cv_ha_c >= cv_ha_o) ? theme.bull : theme.bear;

    case CCM_TREND_CLOUD_BASE:
        return tc_base_bull ? theme.bull : theme.bear;

    default:
        return theme_color_dim(theme.neutral, 85, theme.bg);
    }
}
