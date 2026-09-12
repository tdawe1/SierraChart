#pragma once
// TA_DrawingIDs.h — Stable LineNumber ranges for all sc.UseTool managed drawings.
// Never reuse a range across modules. Gaps are intentional for future expansion.

// ── Boundary Forecast: Trend Cloud ───────────────────────────────────────────
// 20 bars × 2 boundaries (base at +0..19, top at +20..39)
#define FC_TC_BASE_ID       1000
#define FC_TC_END_ID        1039

// ── Boundary Forecast: Super Channel ─────────────────────────────────────────
// 20 bars × 2 boundaries (top at +0..19, bot at +20..39)
#define FC_SC_BASE_ID       1040
#define FC_SC_END_ID        1079

// ── PRISM Signal Labels (B / S bubbles) ──────────────────────────────────────
// 500 labels max (matching Pine's max_labels_count=500) — reserved for the
// text-label layer (not yet implemented; arrows render via subgraphs)
#define SIG_LABEL_BASE_ID   1100
#define SIG_LABEL_END_ID    1599

// ── Info Panel Text Grid ──────────────────────────────────────────────────────
// 25 rows × 2 columns = 50 cell drawings (2000 + row*2 + col)
#define INFO_PANEL_BASE_ID  2000
#define INFO_PANEL_END_ID   2059

// ── Watermark ─────────────────────────────────────────────────────────────────
#define WATERMARK_ID        2060

// ── Boundary Forecast fill rectangles (linefill approximation) ───────────────
// 20 per system
#define FC_TC_FILL_BASE_ID  2100
#define FC_TC_FILL_END_ID   2119
#define FC_SC_FILL_BASE_ID  2130
#define FC_SC_FILL_END_ID   2149

// ── Cleanup helpers ───────────────────────────────────────────────────────────
// Use sc.DeleteACSChartDrawing(sc.ChartNumber, TOOL_DELETE_CHARTDRAWING, id) to
// delete a single drawing by LineNumber; iterate for ranges.
