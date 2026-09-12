#pragma once
#include <cmath>
#include <algorithm>
#include <deque>

// ── KAMA (Kaufman Adaptive Moving Average) ────────────────────────────────────
// Pine f_kama_vb (non-standard fast=0.666, slow=0.0645) is implemented inline
// at each call site (TC fan / tc_base / k100 / PRISM fkama / AI kama) with
// Pine's exact crawl-from-zero warm-up — see the note in mod_TrendCloud.h.

// ── ALMA (Arnaud Legoux Moving Average) ───────────────────────────────────────
// buf[0] = most recent bar, buf[len-1] = oldest
// offset ∈ [0,1], sigma > 0
inline double f_alma(const float* buf, int len, double offset, double sigma)
{
    if (len <= 0 || !buf) return 0.0;
    double m = offset * (double)(len - 1);
    double s = (double)len / sigma;   // Pine ta.alma: s = windowsize/sigma
    double wsum = 0.0, wval = 0.0;
    for (int i = 0; i < len; i++) {
        double diff = (double)(len - 1 - i) - m;  // i=0 → most recent = x=1 end
        double w = std::exp(-0.5 * (diff / s) * (diff / s));
        wsum += w;
        wval += w * (double)buf[i];
    }
    return (wsum > 0.0) ? (wval / wsum) : (double)buf[0];
}

// ── Polynomial Regression (degree 2, 3, or 4) ────────────────────────────────
// buf[0] = most recent bar, buf[len-1] = oldest (like Pine's src[i])
// Pine: i=0 → x=0 (oldest), i=len-1 → x=1 (newest)
// So buf[len-1-i] is the y-value at x = i/(len-1)
// Returns true on success; coeffs[0..degree] filled
// Evaluate at x=1.0 for current-bar value: sum(coeffs[k] for k=0..D)
inline bool poly_regression(const float* buf, int len, int degree, double* coeffs)
{
    if (len < degree + 1 || !buf || !coeffs) return false;
    for (int i = 0; i <= degree; i++) coeffs[i] = 0.0;

    int D = degree;
    int dim = D + 1;
    double A[5][6] = {};          // max 5x6 for D=4
    double lenM1 = (double)(len - 1);
    if (lenM1 <= 0.0) return false;

    for (int i = 0; i < len; i++) {
        double x = (double)i / lenM1;
        double y = (double)buf[len - 1 - i];  // oldest at i=0
        double xp[9];
        xp[0] = 1.0;
        for (int j = 1; j <= 2 * D; j++) xp[j] = xp[j-1] * x;
        for (int r = 0; r < dim; r++) {
            for (int c = 0; c < dim; c++) A[r][c] += xp[r + c];
            A[r][dim] += y * xp[r];
        }
    }

    // Partial-pivot Gaussian elimination
    for (int col = 0; col < dim - 1; col++) {
        int pivRow = col;
        double pivMax = std::abs(A[col][col]);
        for (int row = col + 1; row < dim; row++) {
            double v = std::abs(A[row][col]);
            if (v > pivMax) { pivMax = v; pivRow = row; }
        }
        if (pivRow != col)
            for (int k = 0; k <= dim; k++) std::swap(A[col][k], A[pivRow][k]);
        double piv = A[col][col];
        if (std::abs(piv) > 1e-10)
            for (int row = col + 1; row < dim; row++) {
                double f = A[row][col] / piv;
                for (int k = col; k <= dim; k++) A[row][k] -= f * A[col][k];
            }
    }

    // Back-substitution
    for (int ii = 0; ii < dim; ii++) {
        int r = dim - 1 - ii;
        double val = A[r][dim];
        for (int c = r + 1; c < dim; c++) val -= A[r][c] * coeffs[c];
        double diag = A[r][r];
        coeffs[r] = (std::abs(diag) > 1e-10) ? (val / diag) : 0.0;
    }
    return true;
}

// Evaluate degree-D polynomial at x
inline double eval_poly(double x, const double* coeffs, int degree)
{
    // Horner's method
    double val = coeffs[degree];
    for (int i = degree - 1; i >= 0; i--) val = val * x + coeffs[i];
    return val;
}

// ── Hurst Exponent (Rescaled Range) ──────────────────────────────────────────
// buf[0]=most recent, buf[len-1]=oldest
// Returns H clamped to [0,1]; 0.5 on flat series
inline double f_hurst_rs(const float* buf, int len)
{
    if (len < 4) return 0.5;
    double mean = 0.0;
    for (int i = 0; i < len; i++) mean += (double)buf[i];
    mean /= (double)len;

    double cumDev = 0.0, maxCum = -1e18, minCum = 1e18;
    double sumSq  = 0.0;
    // Pine iterates i=0 to len-1 where i=0 is most recent (buf[i] order)
    for (int i = 0; i < len; i++) {
        double diff = (double)buf[i] - mean;
        cumDev += diff;
        maxCum  = std::max(maxCum, cumDev);
        minCum  = std::min(minCum, cumDev);
        sumSq  += diff * diff;
    }
    double R  = maxCum - minCum;
    double S  = std::sqrt(sumSq / (double)len);
    double RS = (S > 1e-10) ? (R / S) : 0.0;
    double H  = (RS > 0.0) ? (std::log(RS) / std::log((double)len)) : 0.5;
    return std::max(0.0, std::min(1.0, H));
}

// ── Percent Rank ─────────────────────────────────────────────────────────────
// Counts how many of buf[1..lookback] are < buf[0], as percentage
// buf[0]=current, buf[1..lookback]=history
inline double f_percent_rank(const float* buf, int buf_avail, int lookback)
{
    if (buf_avail < 2) return 0.0;
    int avail = std::min(lookback, buf_avail - 1);
    if (avail <= 0) return 0.0;
    double cur = (double)buf[0];
    int count = 0;
    for (int i = 1; i <= avail; i++) {
        if ((double)buf[i] < cur) count++;
    }
    return 100.0 * (double)count / (double)lookback;
}

// ── Quantize ──────────────────────────────────────────────────────────────────
inline int f_quantize(double val, int step)
{
    int s = std::max(step, 1);
    int q = (int)std::round(val / (double)s) * s;
    return std::max(s, q);
}

// ── Rolling SMA ───────────────────────────────────────────────────────────────
// buf[0]=most recent, buf[len-1]=oldest
inline double f_sma(const float* buf, int len)
{
    if (len <= 0) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < len; i++) sum += (double)buf[i];
    return sum / (double)len;
}

// ── EMA (running, uses persistent prev_ema) ───────────────────────────────────
inline double f_ema_step(double src, double prev_ema, int period)
{
    double k = 2.0 / (double)(period + 1);
    return prev_ema + k * (src - prev_ema);
}

// ── Convex-Weighted Blend (Auto-Optimizer) ────────────────────────────────────
// weights are quadratic (position^2), normalized
// scores is a pointer to a deque<int>
inline double f_convex_weight_blend(const std::deque<int>& scores)
{
    int n = (int)scores.size();
    if (n == 0) return 0.0;
    double wsum = 0.0, wscore = 0.0;
    for (int i = 0; i < n; i++) {
        double r  = (double)(i + 1) / (double)n;
        double w  = r * r;
        wscore   += w * (double)scores[i];
        wsum     += w;
    }
    return (wsum > 0.0) ? (wscore / wsum) : 0.0;
}

// Sharpness-cubed version used by AO final blend:
// _ao_w_x = pow(avg_x, 3) * count_x
inline double f_ao_score_weight(double avg_score, int count)
{
    double c = std::pow(avg_score, 3.0) * (double)count;
    return (c > 0.0) ? c : 0.0;
}

// ── SuperTrend State ──────────────────────────────────────────────────────────
struct STState {
    double ratchetedUpper = 0.0;
    double ratchetedLower = 0.0;
    double prevSrc        = 0.0;
    double line           = 0.0;
    int    dir            = 1;
    bool   initialized    = false;

    void reset() { *this = STState{}; }

    void write_to_doubles(double* d5) const {
        d5[0] = ratchetedUpper;
        d5[1] = ratchetedLower;
        d5[2] = prevSrc;
        d5[3] = line;
        d5[4] = (double)dir;
    }
    void read_from_doubles(const double* d5, bool was_initialized) {
        if (!was_initialized) { reset(); return; }
        ratchetedUpper = d5[0];
        ratchetedLower = d5[1];
        prevSrc        = d5[2];
        line           = d5[3];
        dir            = (int)d5[4];
        initialized    = true;
    }
};

inline void calc_supertrend(
    double src, double atr_sma, double factor,
    STState& state, double& out_line, int& out_dir)
{
    double raw_upper = src + factor * atr_sma;
    double raw_lower = src - factor * atr_sma;

    if (!state.initialized) {
        out_dir  = 1;
        out_line = raw_upper;
        state.ratchetedUpper = raw_upper;
        state.ratchetedLower = raw_lower;
        state.prevSrc        = src;
        state.line           = out_line;
        state.dir            = out_dir;
        state.initialized    = true;
        return;
    }

    double prevUpper = state.ratchetedUpper;
    double prevLower = state.ratchetedLower;
    double prevSrc_  = state.prevSrc;
    double prevLine  = state.line;

    // Ratchet: upper only decreases, lower only increases
    double upper = (raw_upper < prevUpper || prevSrc_ > prevUpper) ? raw_upper : prevUpper;
    double lower = (raw_lower > prevLower || prevSrc_ < prevLower) ? raw_lower : prevLower;

    // Direction
    int new_dir;
    if (prevLine == prevUpper)          // was bearish (on upper)
        new_dir = (src > upper) ? -1 : 1;
    else                                // was bullish (on lower)
        new_dir = (src < lower) ?  1 : -1;

    out_line = (new_dir == -1) ? lower : upper;
    out_dir  = new_dir;

    state.ratchetedUpper = upper;
    state.ratchetedLower = lower;
    state.prevSrc        = src;
    state.line           = out_line;
    state.dir            = new_dir;
}
