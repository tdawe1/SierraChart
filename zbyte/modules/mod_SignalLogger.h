#pragma once
// mod_SignalLogger.h — [v9.3 V2/V4, ENHANCEMENT — no Pine equivalent]
// Logs every CONFIRMED (closed-bar) PRISM signal to CSV once resolved, so the
// gate thresholds can be validated against actual outcomes on the trader's
// own charts (execution/analyze_signals.py consumes these files).
//
// Design invariants:
//  - CLOSED bars only: signal state is read from the marker subgraphs at
//    bars < the live bar, where the last tick's write is final. Repaint-proof
//    by construction; identical rows whether display is Live or Bar Close.
//  - LIVE sessions only: gated like the alert block (!IsFullRecalculation &&
//    !DownloadingHistoricalData). The watermark initializes to the LAST bar's
//    timestamp on the first gated call, so attach-time history is never
//    backfilled — no cross-session duplicate rows.
//  - Resolution mirrors the Auto-Optimizer's ATR tiers (0.75/1.5/2.5 from the
//    AO inputs) over a FIXED K-bar window (AO's next-signal censoring would
//    make MFE/MAE non-comparable across rows), tracking BOTH MFE and MAE.
//  - One file append per RESOLVED signal (minutes apart at worst) via the
//    ACSIL file API: sc.OpenFile(FILE_MODE_OPEN_TO_APPEND)/WriteFile/CloseFile.
//    First error latches log_io_disabled — no error-loop spam.

#include <cstdio>
#include <cstring>
#include <cctype>
#include "../include/TA_Subgraphs.h"
#include "../include/TA_Inputs.h"
#include "../include/TA_State.h"

static const int SIGLOG_SCHEMA_VER = 1;

// Append one line to the per-chart CSV; returns false on I/O failure.
inline bool siglog_append(SCStudyInterfaceRef sc, const char* line)
{
    SCString path;
    SCString sym = sc.Symbol;
    // sanitize symbol for a filename
    char clean[64] = {};
    int n = 0;
    for (int i = 0; sym[i] != '\0' && n < 60; i++) {
        char c = sym[i];
        clean[n++] = (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    }
    path.Format("%s\\TA_signals_%s_ch%d.csv", sc.DataFilesFolder().GetChars(),
                clean, sc.ChartNumber);

    int handle = 0;
    if (!sc.OpenFile(path, n_ACSIL::FILE_MODE_OPEN_TO_APPEND, handle))
        return false;
    unsigned int written = 0;
    bool ok = sc.WriteFile(handle, line, (int)strlen(line), &written) != 0;
    sc.CloseFile(handle);
    return ok;
}

inline void siglog_write_header_if_new(SCStudyInterfaceRef sc, TAState& tas)
{
    // FILE_MODE_OPEN_TO_APPEND creates the file if missing; emit the header
    // exactly once per session — duplicate headers are dropped by the
    // analyzer, so a simple per-session latch (watermark < 0) is enough.
    static const char* HDR =
        "schema_ver,fire_time_raw,fire_time,symbol,bar_period,direction,class,"
        "sig_path,elev_mask,entry,atr,mfe_atr,mae_atr,tier,bars_held,flicker,"
        "er,er_rank,fk_norm,bq_ratio,cvd_rank,cvd_agree,delta_real,chop,"
        "trg_bull_votes,trg_bear_votes,suppressed,hurst,kama_align,sc_cco,"
        "ao_eff_len,rrg_votes,est_sec_per_bar\n";
    if (!siglog_append(sc, HDR))
        tas.log_io_disabled = true;
}

// Called once per NEW live bar (bar_advanced), after all modules computed.
// Enqueues confirmed signals from the just-closed bar(s) past the watermark,
// updates pending MFE/MAE with the just-closed bar, resolves aged entries.
inline void compute_SignalLogger(
    SCStudyInterfaceRef sc,
    TAState& tas,
    bool   enabled,
    bool   include_mq,
    int    horizon_bars,      // 0 = follow IN_AO_MAX_BARS
    float  tier1, float tier2, float tier3,   // AO ATR tiers
    double est_sec_per_bar)
{
    if (!enabled || tas.log_io_disabled) return;
    if (sc.IsFullRecalculation || sc.DownloadingHistoricalData) return;

    int live = sc.ArraySize - 1;
    if (live < 1) return;

    // First gated call: initialize the watermark to the last CLOSED bar so
    // pre-attach history is never enqueued (verified defect fix — a -1 init
    // would backfill the whole chart every session).
    if (tas.log_watermark_time < 0.0) {
        tas.log_watermark_time = sc.BaseDateTimeIn[live - 1].GetAsDouble();
        siglog_write_header_if_new(sc, tas);
        return;
    }

    int K = (horizon_bars > 0) ? horizon_bars : 7;

    // ── 1. Update + resolve pending with each newly closed bar ──────────────
    // Walk forward over the newly closed bars (usually exactly one)
    int first_new = live;   // first bar index with time > watermark
    while (first_new >= 1
        && sc.BaseDateTimeIn[first_new - 1].GetAsDouble() > tas.log_watermark_time)
        first_new--;

    for (int b = first_new; b < live; b++) {
        // 1a. progress existing pendings with bar b's range
        for (size_t i = 0; i < tas.log_pending.size(); i++) {
            SigLogPending& p = tas.log_pending[i];
            if (p.bars_held < 0) continue;               // resolved, awaiting erase
            if (sc.BaseDateTimeIn[b].GetAsDouble() <= p.fire_time) continue;
            double fav = (p.dir == 1) ? ((double)sc.High[b] - p.entry)
                                      : (p.entry - (double)sc.Low[b]);
            double adv = (p.dir == 1) ? (p.entry - (double)sc.Low[b])
                                      : ((double)sc.High[b] - p.entry);
            if (p.atr > 0.0) {
                p.mfe = std::max(p.mfe, fav / p.atr);
                p.mae = std::max(p.mae, adv / p.atr);
            }
            if      (p.mfe >= (double)tier3) p.tier = 3;
            else if (p.mfe >= (double)tier2) p.tier = std::max(p.tier, 2);
            else if (p.mfe >= (double)tier1) p.tier = std::max(p.tier, 1);
            p.bars_held++;
            if (p.tier >= 3 || p.bars_held >= K) {
                // resolve → write row
                char line[1024];
                SCString ts = sc.DateTimeToString(SCDateTime(p.fire_time),
                                                  FLAG_DT_COMPLETE_DATETIME);
                snprintf(line, sizeof(line),
                    "%d,%.8f,%s,%s,%s,%d,%s,%d,%d,%.6f,%.6f,%.4f,%.4f,%d,%d,%d,"
                    "%.4f,%.1f,%.4f,%.3f,%.1f,%d,%d,%d,%d,%d,%d,%.3f,%.1f,%.1f,%d,%d,%.1f\n",
                    SIGLOG_SCHEMA_VER, p.fire_time, ts.GetChars(),
                    sc.Symbol.GetChars(), tas.log_period_str.GetChars(),
                    p.dir, p.marginal ? "MQ" : "FULL",
                    p.f_sig_path, p.f_elev_mask,
                    p.entry, p.atr, p.mfe, p.mae, p.tier, p.bars_held, p.flicker,
                    p.f_er, p.f_er_rank, p.f_fk_norm, p.f_bq_ratio,
                    p.f_cvd_rank, p.f_cvd_agree, p.f_delta_real, p.f_chop,
                    p.f_trg_bull_votes, p.f_trg_bear_votes, p.f_suppressed,
                    p.f_hurst, p.f_kama_align * 100.0, p.f_sc_cco,
                    p.f_ao_eff_len, p.f_rrg_votes, est_sec_per_bar);
                if (!siglog_append(sc, line)) { tas.log_io_disabled = true; return; }
                p.bars_held = -1;   // mark for erase
            }
        }
        // erase resolved
        for (int i = (int)tas.log_pending.size() - 1; i >= 0; i--)
            if (tas.log_pending[i].bars_held < 0)
                tas.log_pending.erase(tas.log_pending.begin() + i);

        // 1b. enqueue new confirmed signals from bar b (marker subgraphs hold
        // the bar's FINAL state once it is no longer the live bar)
        bool fb = sc.Subgraph[SG_PRISM_BULL][b]    != 0.0f;
        bool fr = sc.Subgraph[SG_PRISM_BEAR][b]    != 0.0f;
        bool mb = sc.Subgraph[SG_PRISM_MQ_BULL][b] != 0.0f;
        bool mr = sc.Subgraph[SG_PRISM_MQ_BEAR][b] != 0.0f;
        if ((fb || fr || (include_mq && (mb || mr))) && tas.log_pending.size() < 64) {
            SigLogPending p;
            p.fire_time = sc.BaseDateTimeIn[b].GetAsDouble();
            p.dir       = (fb || mb) ? 1 : -1;
            p.marginal  = !(fb || fr);
            p.entry     = (double)sc.Close[b];
            p.atr       = (double)sc.Subgraph[SG_RRG_STORE][b];   // ATR14 history
            // flicker snapshot for this bar (V4; -1 = not observed live)
            p.flicker   = (tas.fin_flicker_time == p.fire_time)
                        ? tas.fin_flicker_count : -1;
            // fire-time feature vector from the hidden stashes
            p.f_er         = (double)sc.Subgraph[SG_RAW_ER][b];
            p.f_er_rank    = (double)sc.Subgraph[SG_RAW_CCO].Arrays[0][b];
            p.f_fk_norm    = (double)sc.Subgraph[SG_RAW_ER].Arrays[0][b];
            p.f_bq_ratio   = (double)sc.Subgraph[SG_RAW_ER].Arrays[1][b];
            p.f_chop       = (int)sc.Subgraph[SG_RAW_ER].Arrays[2][b];
            p.f_sig_path   = (int)sc.Subgraph[SG_RAW_ER].Arrays[3][b];
            p.f_elev_mask  = (int)sc.Subgraph[SG_RAW_ER].Arrays[4][b];
            p.f_cvd_rank   = (double)sc.Subgraph[SG_CVD_STORE].Arrays[0][b];
            double dnet    = (double)sc.Subgraph[SG_CVD_STORE].Arrays[1][b];
            p.f_cvd_agree  = ((p.dir == 1 && dnet > 0.0) || (p.dir == -1 && dnet < 0.0)) ? 1 : 0;
            p.f_delta_real = (int)sc.Subgraph[SG_CVD_STORE].Arrays[2][b];
            p.f_suppressed = (int)(sc.Subgraph[SG_RAW_KAMA_ALIGN].Arrays[0][b]
                                 + sc.Subgraph[SG_RAW_KAMA_ALIGN].Arrays[1][b] > 0.5f);
            p.f_kama_align = (double)sc.Subgraph[SG_RAW_KAMA_ALIGN][b] / 100.0;
            p.f_ao_eff_len = (int)sc.Subgraph[SG_RAW_AO_EFF_LEN][b];
            p.f_rrg_votes  = (int)sc.Subgraph[SG_RAW_CCO].Arrays[1][b];
            p.f_sc_cco     = (double)sc.Subgraph[SG_SC_CCO][b];
            // per-bar TRG history (written by the main study every bar)
            p.f_trg_bull_votes = (int)sc.Subgraph[SG_RAW_KAMA_ALIGN].Arrays[2][b];
            p.f_trg_bear_votes = (int)sc.Subgraph[SG_RAW_KAMA_ALIGN].Arrays[3][b];
            p.f_hurst          = (double)sc.Subgraph[SG_RAW_KAMA_ALIGN].Arrays[4][b];
            tas.log_pending.push_back(p);
        }
    }

    tas.log_watermark_time = sc.BaseDateTimeIn[live - 1].GetAsDouble();
}
