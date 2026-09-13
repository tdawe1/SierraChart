#pragma once
// TA_State.h — Per-instance study state with Pine `var` commit/rollback semantics.
//
// Why this exists: with sc.AutoLoop=1 the study function runs on EVERY tick of
// the live bar. Pine `var` recurrences (EMAs, Kalman filters, KAMA, SuperTrend
// ratchets, pending-signal gates, score deques) are evaluated once per bar
// close in Pine; naively re-stepping them per tick makes live-session output
// diverge from a historical recalculation of the same bars.
//
// The fix: keep ALL scalar recurrence state in one TARecurrence struct with two
// copies — `committed` (state as of the last CLOSED bar) and `working` (state
// for the bar being computed). On the first call for a new bar the previous
// working copy is committed; on every re-tick of the same bar the working copy
// is rolled back to committed before recomputing (exactly Pine's var rollback).
// A full recalculation (sc.Index == 0) resets both copies — no exceptions.
//
// This struct also owns the per-instance scratch buffers that replace the
// function-local `static float buf[...]` arrays the modules used to share
// across chart instances (data race + fixed-size overflow risk).
//
// Everything here is doubles: Pine floats are 64-bit, and float32 subgraph
// round-trips visibly degrade long KAMA/EMA recursions.

#include <vector>
#include <deque>
#include "TA_Algorithms.h"   // STState

// Persistent-pointer slot for the TAState instance (do not reuse for anything else)
#define PERSIST_P_TA_STATE  10

// ── EMA with Pine ta.ema seeding ─────────────────────────────────────────────
// Pine ta.ema(src, len) seeds with SRC on the first non-na bar and steps with
// k = 2/(len+1) from then on. (The SMA-seeded variant is ta.rma, not ta.ema.)
struct SeededEma {
    double value = 0.0;
    int    count = 0;

    double step(double src, int len) {
        if (len < 1) len = 1;
        count++;
        if (count == 1) {
            value = src;
        } else {
            double k = 2.0 / ((double)len + 1.0);
            value = value + k * (src - value);
        }
        return value;
    }
};

// ── Auto-Optimizer pending-signal state (one per test length) ────────────────
struct AOPendingState {
    double price = 0.0;   // 0.0 = no pending signal (sentinel, matches prior code)
    double atr   = 0.0;   // ATR(14) snapshot at signal bar
    int    dir   = 0;     // 1 = bull, -1 = bear
    int    bar   = 0;     // bar index the signal fired on
    int    best  = 0;     // best ATR tier reached (0..3)
    double worst = 0.0;   // worst adverse excursion in price units [v9.3 E5]
};

// ── Signal-outcome logger pending entry [v9.3] ───────────────────────────────
// One confirmed (closed-bar) signal awaiting resolution. Feature values are
// snapshots of the CLOSED signal bar — repaint-proof by construction.
struct SigLogPending {
    double fire_time = 0.0;      // SCDateTime of the signal bar (as double)
    int    dir       = 0;        // 1 = bull, -1 = bear
    bool   marginal  = false;    // MQ dot (control group) vs full arrow
    double entry     = 0.0;      // signal-bar close (AO parity)
    double atr       = 0.0;      // ATR(14) at the signal bar
    double mfe       = 0.0;      // max favorable excursion, ATR units
    double mae       = 0.0;      // max adverse excursion, ATR units
    int    tier      = 0;        // best AO tier reached (0..3)
    int    bars_held = 0;
    int    flicker   = 0;        // intrabar signal-state changes observed live
    // fire-time feature vector (from the V1 stash + module results)
    double f_er = 0.0, f_er_rank = 0.0, f_fk_norm = 0.0, f_bq_ratio = 0.0;
    double f_cvd_rank = 0.0; int f_cvd_agree = 0; int f_delta_real = 0;
    int    f_chop = 0, f_sig_path = 0, f_elev_mask = 0;
    int    f_trg_bull_votes = 0, f_trg_bear_votes = 0, f_suppressed = 0;
    double f_hurst = 0.0, f_kama_align = 0.0, f_sc_cco = 0.0;
    int    f_ao_eff_len = 0, f_rrg_votes = 0;
};

// ── All Pine-var recurrence state, committed once per closed bar ─────────────
struct TARecurrence {
    // Global ATR(14) — Wilder RMA, matching Pine ta.atr(14) (line 171 of TA9)
    double atr14 = 0.0;

    // Super Channel: Wilder RSI(14) running averages (Pine ta.rsi, line 201),
    // Keltner EMA(20) (ta.ema, line 215), Keltner ATR(10) (ta.atr, line 216)
    double    rsi_avg_gain = 0.0;   // holds the running SUM during warm-up (idx < 14)
    double    rsi_avg_loss = 0.0;
    SeededEma kc_ema;
    double    kc_atr10 = 0.0;

    // PRISM Adaptive noise suppression: EMA(10) of ER(14) (ta.ema, line 686)
    SeededEma ns_er_ema;

    // Candles: FHA Kalman estimators + recursive HA opens
    double fha_vol_est = 0.0, fha_vol_err = 1.0;
    double fha_pvp_est = 0.0, fha_pvp_err = 1.0, fha_pvp_var = 1.0;
    double cv_ha_o = 0.0, cv_lrha_o = 0.0;

    // Trend Cloud: KAMA fan k100 on ohlc4 (the 18 shorter levels live in
    // subgraph arrays, which are naturally tick-idempotent via [idx-1] reads)
    double tc_k100 = 0.0;

    // Trend Regime Gate: throttled Hurst + acceleration EMA (ta.ema, line 533)
    double    hurst = 0.5;
    SeededEma accel_ema;

    // Candle Coloring: Adaptive Impulse KAMA(13)
    double ai_kama = 0.0;

    // PRISM signal engine: dual SuperTrend + gate chain (Pine TA9 §7B vars)
    STState prism_st1, prism_st2;
    int prism_prev_d1 = 0, prism_prev_d2 = 0;  // st dirs [1] (previous bar)
    int prism_last_dir = 1;                    // last agreed dual-rail direction
    int prism_last_sig = 0;                    // dedup: last raw signal direction
    int sl_bull_hold_until = -1;               // Structure Lock holds (bar index)
    int sl_bear_hold_until = -1;
    int bq_bull_hold_until = -1;               // Bar Quality holds (bar index)
    int bq_bear_hold_until = -1;
    double prism_fkama = 0.0;                  // fast KAMA(20); Pine seeds from 0
    double elev_c0 = 0.0, elev_c1 = 0.0, elev_c2 = 0.0;  // held quad-fit coeffs

    // Auto-Optimizer: 6 SuperTrends (3 lengths x 2 rails), previous rail
    // directions, pending signals and score history per length (s/m/l)
    STState         ao_st[6];
    int             ao_prev_d[6] = {0, 0, 0, 0, 0, 0};
    AOPendingState  ao_pend[3];
    std::deque<int> ao_scores[3];

    // Boundary Forecast: SC cubic-fit coefficients, refit every 3 bars with
    // held values between (Pine TA9:1290-1299 var + bar_index % 3 throttle)
    double fc_sc_ct[4] = {0.0, 0.0, 0.0, 0.0};   // sc_top cubic c0..c3
    double fc_sc_cb[4] = {0.0, 0.0, 0.0, 0.0};   // sc_bot cubic c0..c3

    // ── v9.3 enhancements ────────────────────────────────────────────────────
    // Bar-Close confirmation snapshot: the bar's FINAL signal state + marker
    // anchor prices. Written every tick of the live bar; read back from the
    // `committed` copy on the next bar to draw confirmed markers at idx-1 and
    // fire confirmed alerts. Riding TARecurrence makes it rollback-safe and
    // full-recalc deterministic for free.
    bool  sigc_full_bull = false, sigc_full_bear = false;
    bool  sigc_mq_bull   = false, sigc_mq_bear   = false;
    float sigc_bull_y = 0.0f, sigc_bear_y = 0.0f;
    float sigc_mq_bull_y = 0.0f, sigc_mq_bear_y = 0.0f;

    // Opposite-signal cooldown (flip guard): last FULL arrow's bar and
    // direction (1 = bull, -1 = bear). Bar indices are safe here because the
    // state resets on every full recalculation where indices could shift.
    int last_full_bar = -1000000;
    int last_full_dir = 0;

    // Range Regime Gate: Wilder ADX recurrence (smoothed +DM / -DM / TR and
    // the RMA of DX), warm-up counted like ta_wilder_atr_step
    double rrg_dmp = 0.0, rrg_dmn = 0.0, rrg_tr = 0.0, rrg_adx = 0.0;
};

// ── The per-instance state object (sc.GetPersistentPointer) ─────────────────
struct TAState {
    TARecurrence committed;    // state as of the last closed bar
    TARecurrence working;      // state for the bar currently being computed
    int last_index = -1;       // last sc.Index processed

    // Once-per-bar alert latch (indexed by alert number), keyed by the bar's
    // TIMESTAMP (SCDateTime as double) — bar indices shift when Days-to-Load
    // trims or reloads the data, timestamps don't. Deliberately NOT part of
    // TARecurrence: rolling it back would re-fire alerts on every tick.
    double alert_fired_time[16];

    // Per-instance scratch buffers (replace shared function-local statics).
    // Reused freely across modules within one study call; scratch_a..d may be
    // live simultaneously inside a single module (mod_Candles uses four).
    std::vector<float> scratch_a, scratch_b, scratch_c, scratch_d;

    // ── v9.3 per-instance flags/estimates (NOT rollback state) ───────────────
    // Chart has real bid/ask aggressor volume (scanned once per full recalc)
    bool of_data_present = false;
    // Effective seconds per bar: authoritative for time charts, median of
    // recent bar durations for tick/volume/range charts (frozen per recalc)
    double est_sec_per_bar = 0.0;

    // Intrabar signal flicker telemetry: counts live-bar signal-state changes.
    // Deliberately outside TARecurrence (per-tick observation must not roll
    // back). `fin_*` is the snapshot of the just-CLOSED bar, taken BEFORE the
    // counters reset for the new bar, so the logger (which runs later in the
    // same pass) reads the finished bar's count — never the wiped one.
    double        flicker_bar_time  = -1.0;
    unsigned char flicker_prev_pack = 0;
    int           flicker_count     = 0;
    double        fin_flicker_time  = -1.0;   // -1 = no live-observed bar yet
    int           fin_flicker_count = -1;     // -1 sentinel = not observed live

    // Signal-outcome logger: pending signals awaiting resolution, plus the
    // live-only watermark (initialized to the last bar's timestamp on the
    // first gated call so attach-time history is never backfilled — rows are
    // live-session signals only, no cross-session duplicates).
    std::vector<SigLogPending> log_pending;
    double log_watermark_time = -1.0;
    bool   log_io_disabled    = false;   // latched on first file error
    SCString log_period_str;             // e.g. "500T" / "60s" (set at idx==0)

    TAState() { for (int i = 0; i < 16; i++) alert_fired_time[i] = -1.0; }
};

// Grow-on-demand scratch access — never overflows, unlike the old statics.
inline float* ta_scratch(std::vector<float>& v, int n)
{
    if (n < 1) n = 1;
    if ((int)v.size() < n) v.resize((size_t)n);
    return v.data();
}

inline TAState* ta_get_state(SCStudyInterfaceRef sc)
{
    void*& vp = sc.GetPersistentPointer(PERSIST_P_TA_STATE);
    if (vp == nullptr) vp = new TAState();
    return static_cast<TAState*>(vp);
}

inline void ta_free_state(SCStudyInterfaceRef sc)
{
    void*& vp = sc.GetPersistentPointer(PERSIST_P_TA_STATE);
    delete static_cast<TAState*>(vp);
    vp = nullptr;
}

// ── True Range + Wilder ATR (Pine ta.atr) ────────────────────────────────────
inline double ta_true_range(SCStudyInterfaceRef sc, int j)
{
    double hl = (double)(sc.High[j] - sc.Low[j]);
    if (j <= 0) return hl;
    double hc = std::abs((double)sc.High[j] - sc.Close[j - 1]);
    double lc = std::abs((double)sc.Low[j]  - sc.Close[j - 1]);
    return std::max(hl, std::max(hc, lc));
}

// Pine ta.atr(n) = RMA(TR, n): na until n TRs exist, then seeds with their SMA
// and steps as (prev*(n-1) + tr)/n. Warm-up returns the running mean of the
// TRs so far (Pine shows na). `state` is the recurrence field for this period.
inline double ta_wilder_atr_step(SCStudyInterfaceRef sc, int idx, int n, double& state)
{
    if (n < 1) n = 1;
    double tr = ta_true_range(sc, idx);
    if (idx < n - 1) {
        state = (state * (double)idx + tr) / (double)(idx + 1);
    } else if (idx == n - 1) {
        double sum = 0.0;
        for (int j = 0; j < n; j++) sum += ta_true_range(sc, idx - j);
        state = sum / (double)n;
    } else {
        state = (state * (double)(n - 1) + tr) / (double)n;
    }
    return (state > 0.0) ? state : 0.0001;
}
