#pragma once
#include "sierrachart.h"
// windows.h defines min/max macros that break std::min/std::max
#undef min
#undef max

struct ThemeProfile {
    COLORREF bull;
    COLORREF bear;
    COLORREF neutral;
    COLORREF hilight;
    COLORREF sig_bull;
    COLORREF sig_bear;
    COLORREF sig_text;
    COLORREF bg;       // blend target for faked transparency (set from bg_mode)
};

// Helper: build COLORREF from 0xRRGGBB hex value
// Sierra Chart uses BGR ordering in COLORREF on Windows, so convert:
// Pine #RRGGBB → Windows COLORREF = RGB(RR, GG, BB) = 0x00BBGGRR
// We use the RGB() macro which does the correct conversion.
#define TA_RGB(hex) RGB(((hex)>>16)&0xFF, ((hex)>>8)&0xFF, (hex)&0xFF)

inline ThemeProfile get_theme(int theme_index, bool is_dark)
{
    ThemeProfile t;
    // theme_index: dark themes 0-15, light themes 0-15 (passed separately via is_dark)

    if (is_dark) {
        switch (theme_index) {
        case 0:  // Modern
            t.bull=TA_RGB(0x00FFFF); t.bear=TA_RGB(0xFF0000); t.neutral=TA_RGB(0x888888); t.hilight=TA_RGB(0xFFFFFF);
            t.sig_bull=TA_RGB(0xFFD700); t.sig_bear=TA_RGB(0xFFFFFF); t.sig_text=TA_RGB(0x000000); break;
        case 1:  // Terminal
            t.bull=TA_RGB(0x00FF00); t.bear=TA_RGB(0xFF6000); t.neutral=TA_RGB(0x707070); t.hilight=TA_RGB(0xFFFFFF);
            t.sig_bull=TA_RGB(0xFFFFFF); t.sig_bear=TA_RGB(0xFF5500); t.sig_text=TA_RGB(0x000000); break;
        case 2:  // Cyberpunk
            t.bull=TA_RGB(0x00FFFF); t.bear=TA_RGB(0xFF00FF); t.neutral=TA_RGB(0x9932CC); t.hilight=TA_RGB(0xFFFF00);
            t.sig_bull=TA_RGB(0xFFFF00); t.sig_bear=TA_RGB(0xFF00CC); t.sig_text=TA_RGB(0x000000); break;
        case 3:  // Neon Noir
            t.bull=TA_RGB(0xFF1DDF); t.bear=TA_RGB(0x00C8FF); t.neutral=TA_RGB(0x6A0DAD); t.hilight=TA_RGB(0xFFFF00);
            t.sig_bull=TA_RGB(0xFFFF00); t.sig_bear=TA_RGB(0xFF6EC7); t.sig_text=TA_RGB(0x000000); break;
        case 4:  // Phosphor
            t.bull=TA_RGB(0xFFB000); t.bear=TA_RGB(0xCC2200); t.neutral=TA_RGB(0x7A5000); t.hilight=TA_RGB(0xFFFFFF);
            t.sig_bull=TA_RGB(0xFFFFFF); t.sig_bear=TA_RGB(0xFF6600); t.sig_text=TA_RGB(0x000000); break;
        case 5:  // Fire & Ice
            t.bull=TA_RGB(0xC8F0FF); t.bear=TA_RGB(0xFF4500); t.neutral=TA_RGB(0x6699AA); t.hilight=TA_RGB(0xFFD700);
            t.sig_bull=TA_RGB(0xFFD700); t.sig_bear=TA_RGB(0xFF6347); t.sig_text=TA_RGB(0x000000); break;
        case 6:  // Slate
            t.bull=TA_RGB(0x00CED1); t.bear=TA_RGB(0xE8735A); t.neutral=TA_RGB(0x708090); t.hilight=TA_RGB(0xF5F5DC);
            t.sig_bull=TA_RGB(0xF5DEB3); t.sig_bear=TA_RGB(0xDC143C); t.sig_text=TA_RGB(0x000000); break;
        case 7:  // Blood & Greed
            t.bull=TA_RGB(0x00FF41); t.bear=TA_RGB(0xCC0000); t.neutral=TA_RGB(0x555555); t.hilight=TA_RGB(0xFFD700);
            t.sig_bull=TA_RGB(0xFFD700); t.sig_bear=TA_RGB(0xFF6600); t.sig_text=TA_RGB(0x000000); break;
        case 8:  // Gold Standard
            t.bull=TA_RGB(0xFFD700); t.bear=TA_RGB(0xB0B0B0); t.neutral=TA_RGB(0xCD7F32); t.hilight=TA_RGB(0xFFFFFF);
            t.sig_bull=TA_RGB(0x1E90FF); t.sig_bear=TA_RGB(0xFF1111); t.sig_text=TA_RGB(0x000000); break;
        case 9:  // Ultraviolet
            t.bull=TA_RGB(0xBF00FF); t.bear=TA_RGB(0x6600AA); t.neutral=TA_RGB(0x5C0080); t.hilight=TA_RGB(0x00FF00);
            t.sig_bull=TA_RGB(0x00FF00); t.sig_bear=TA_RGB(0xFF1493); t.sig_text=TA_RGB(0x000000); break;
        case 10: // Infrared
            t.bull=TA_RGB(0xFFFFFF); t.bear=TA_RGB(0xCC0000); t.neutral=TA_RGB(0x882200); t.hilight=TA_RGB(0xFF6600);
            t.sig_bull=TA_RGB(0xFF6600); t.sig_bear=TA_RGB(0xFF2222); t.sig_text=TA_RGB(0x000000); break;
        case 11: // Toxic
            t.bull=TA_RGB(0xCCFF00); t.bear=TA_RGB(0x33AA00); t.neutral=TA_RGB(0x1A5500); t.hilight=TA_RGB(0xFFFFFF);
            t.sig_bull=TA_RGB(0xFFFFFF); t.sig_bear=TA_RGB(0x00FF66); t.sig_text=TA_RGB(0x000000); break;
        case 12: // Crimson Tide
            t.bull=TA_RGB(0xFF2222); t.bear=TA_RGB(0xC0C0C0); t.neutral=TA_RGB(0x808080); t.hilight=TA_RGB(0xFFD700);
            t.sig_bull=TA_RGB(0xFF8800); t.sig_bear=TA_RGB(0xFF66FF); t.sig_text=TA_RGB(0x000000); break;
        case 13: // Vaporwave
            t.bull=TA_RGB(0xFF6B9D); t.bear=TA_RGB(0x9B30FF); t.neutral=TA_RGB(0x6622AA); t.hilight=TA_RGB(0x00FFFF);
            t.sig_bull=TA_RGB(0x00FFFF); t.sig_bear=TA_RGB(0xFF00FF); t.sig_text=TA_RGB(0x000000); break;
        case 14: // Matrix
            t.bull=TA_RGB(0x00FF41); t.bear=TA_RGB(0x007A1F); t.neutral=TA_RGB(0x003D0F); t.hilight=TA_RGB(0xFFFFFF);
            t.sig_bull=TA_RGB(0xAAFFBB); t.sig_bear=TA_RGB(0xFFFFFF); t.sig_text=TA_RGB(0x000000); break;
        case 15: // Arctic
        default:
            t.bull=TA_RGB(0xA8D8FF); t.bear=TA_RGB(0x4488CC); t.neutral=TA_RGB(0x3A5A8A); t.hilight=TA_RGB(0xF0F8FF);
            t.sig_bull=TA_RGB(0xF0F8FF); t.sig_bear=TA_RGB(0x00BFFF); t.sig_text=TA_RGB(0x000000); break;
        }
    } else {
        switch (theme_index) {
        case 0:  // Classic
            t.bull=TA_RGB(0x2B58BF); t.bear=TA_RGB(0xD42020); t.neutral=TA_RGB(0x707070); t.hilight=TA_RGB(0x2A2A2A);
            t.sig_bull=TA_RGB(0x0F8C50); t.sig_bear=TA_RGB(0xA04810); t.sig_text=TA_RGB(0xFFFFFF); break;
        case 1:  // Woodland
            t.bull=TA_RGB(0x2E8A2E); t.bear=TA_RGB(0x991515); t.neutral=TA_RGB(0x7A5A35); t.hilight=TA_RGB(0x3E2010);
            t.sig_bull=TA_RGB(0x1080A0); t.sig_bear=TA_RGB(0x8A208A); t.sig_text=TA_RGB(0xFFFFFF); break;
        case 2:  // Solar
            t.bull=TA_RGB(0x009999); t.bear=TA_RGB(0xAA20AA); t.neutral=TA_RGB(0x7A55A0); t.hilight=TA_RGB(0xA07515);
            t.sig_bull=TA_RGB(0xA07515); t.sig_bear=TA_RGB(0x108870); t.sig_text=TA_RGB(0xFFFFFF); break;
        case 3:  // Twilight
            t.bull=TA_RGB(0xAA1570); t.bear=TA_RGB(0x1060AA); t.neutral=TA_RGB(0x6A3590); t.hilight=TA_RGB(0x807010);
            t.sig_bull=TA_RGB(0x807010); t.sig_bear=TA_RGB(0x8A1050); t.sig_text=TA_RGB(0xFFFFFF); break;
        case 4:  // Parchment
            t.bull=TA_RGB(0xA06015); t.bear=TA_RGB(0xA03515); t.neutral=TA_RGB(0x7A5515); t.hilight=TA_RGB(0x3A2010);
            t.sig_bull=TA_RGB(0x3A2010); t.sig_bear=TA_RGB(0xA04F15); t.sig_text=TA_RGB(0xFFFFFF); break;
        case 5:  // Shoreline
            t.bull=TA_RGB(0x106595); t.bear=TA_RGB(0xA04515); t.neutral=TA_RGB(0x406578); t.hilight=TA_RGB(0x807010);
            t.sig_bull=TA_RGB(0x807010); t.sig_bear=TA_RGB(0x8A3515); t.sig_text=TA_RGB(0xFFFFFF); break;
        case 6:  // Graphite
            t.bull=TA_RGB(0x109595); t.bear=TA_RGB(0xA05540); t.neutral=TA_RGB(0x5A7080); t.hilight=TA_RGB(0x2A2A2A);
            t.sig_bull=TA_RGB(0x6A5515); t.sig_bear=TA_RGB(0xAA2020); t.sig_text=TA_RGB(0xFFFFFF); break;
        case 7:  // Harvest
            t.bull=TA_RGB(0x409915); t.bear=TA_RGB(0x991818); t.neutral=TA_RGB(0x505050); t.hilight=TA_RGB(0x807010);
            t.sig_bull=TA_RGB(0x807010); t.sig_bear=TA_RGB(0x906515); t.sig_text=TA_RGB(0xFFFFFF); break;
        case 8:  // Guilded
            t.bull=TA_RGB(0xA08015); t.bear=TA_RGB(0x585858); t.neutral=TA_RGB(0x7A6A30); t.hilight=TA_RGB(0x151560);
            t.sig_bull=TA_RGB(0x2060CC); t.sig_bear=TA_RGB(0xAA2020); t.sig_text=TA_RGB(0xFFFFFF); break;
        case 9:  // Amethyst
            t.bull=TA_RGB(0x7020BB); t.bear=TA_RGB(0x4A1599); t.neutral=TA_RGB(0x451568); t.hilight=TA_RGB(0x108A10);
            t.sig_bull=TA_RGB(0x108A10); t.sig_bear=TA_RGB(0xAA2080); t.sig_text=TA_RGB(0xFFFFFF); break;
        case 10: // Forge
            t.bull=TA_RGB(0x484848); t.bear=TA_RGB(0xBB2020); t.neutral=TA_RGB(0x7A3A15); t.hilight=TA_RGB(0xA05015);
            t.sig_bull=TA_RGB(0xA05015); t.sig_bear=TA_RGB(0xCC2525); t.sig_text=TA_RGB(0xFFFFFF); break;
        case 11: // Briar
            t.bull=TA_RGB(0x6A9515); t.bear=TA_RGB(0x2D8820); t.neutral=TA_RGB(0x304A10); t.hilight=TA_RGB(0x2A2A2A);
            t.sig_bull=TA_RGB(0x2A2A2A); t.sig_bear=TA_RGB(0x108855); t.sig_text=TA_RGB(0xFFFFFF); break;
        case 12: // Scarlet
            t.bull=TA_RGB(0xBB2525); t.bear=TA_RGB(0x585858); t.neutral=TA_RGB(0x707070); t.hilight=TA_RGB(0x807010);
            t.sig_bull=TA_RGB(0xA06015); t.sig_bear=TA_RGB(0x8A208A); t.sig_text=TA_RGB(0xFFFFFF); break;
        case 13: // Dusk
            t.bull=TA_RGB(0xAA3570); t.bear=TA_RGB(0x5A30BB); t.neutral=TA_RGB(0x5A2590); t.hilight=TA_RGB(0x109090);
            t.sig_bull=TA_RGB(0x109090); t.sig_bear=TA_RGB(0x992099); t.sig_text=TA_RGB(0xFFFFFF); break;
        case 14: // Fern
            t.bull=TA_RGB(0x2D9915); t.bear=TA_RGB(0x106625); t.neutral=TA_RGB(0x1A4A10); t.hilight=TA_RGB(0x2A2A2A);
            t.sig_bull=TA_RGB(0x408A50); t.sig_bear=TA_RGB(0x2A2A2A); t.sig_text=TA_RGB(0xFFFFFF); break;
        case 15: // Nordic
        default:
            t.bull=TA_RGB(0x2E60A0); t.bear=TA_RGB(0x2E5099); t.neutral=TA_RGB(0x406080); t.hilight=TA_RGB(0x1A3060);
            t.sig_bull=TA_RGB(0x1A3060); t.sig_bear=TA_RGB(0x1080BB); t.sig_text=TA_RGB(0xFFFFFF); break;
        }
    }
    return t;
}

// Emulates Pine color.new(c, transparency) by pre-blending toward the chart
// background (ACSIL COLORREFs carry no alpha). alpha_pct is OPACITY percent
// (Pine transparency T -> alpha_pct = 100 - T). Blending toward black inverted
// every fade on light backgrounds — always pass the theme's bg.
inline COLORREF theme_color_dim(COLORREF c, int alpha_pct, COLORREF bg)
{
    if (alpha_pct < 0) alpha_pct = 0;
    if (alpha_pct > 100) alpha_pct = 100;
    int r = (GetRValue(bg) * (100 - alpha_pct) + GetRValue(c) * alpha_pct) / 100;
    int g = (GetGValue(bg) * (100 - alpha_pct) + GetGValue(c) * alpha_pct) / 100;
    int b = (GetBValue(bg) * (100 - alpha_pct) + GetBValue(c) * alpha_pct) / 100;
    return RGB(r, g, b);
}

// ── Fill transparency (TC cloud / SC channel / ribbon TRANSPARENT_FILL pairs) ─
// The study's "Transparency Level for Fill Styles" is set to this value (0 =
// opaque, 100 = invisible) so the fills genuinely alpha-blend over whatever is
// underneath — native price bars, indicator candles of a lower layer, and
// other studies stay visible through them. Pre-blending alone (v9.0 pinned the
// level to 0) made every fill an OPAQUE blanket that hid the chart's own
// candles and any study drawn below us.
//
// Sierra composites: on_screen = DataColor*(100-T)/100 + underneath*T/100.
// A fill that wants Pine opacity A% therefore pre-scales its color toward the
// background by k = A*100/(100-T):
//   underneath == bg  → exact Pine composite (same as before the change)
//   underneath != bg  → the covered content shows through at (100-T)%+
// k clamps at 100, so A caps at (100-T)% — with T=80 the strong-trend Trend
// Cloud renders at 20% opacity instead of Pine's 45% ceiling; the glow stack
// and lines still convey strength. The ribbon fill (A=20) lands exactly.
static const int TA_FILL_TRANSPARENCY = 80;

inline COLORREF theme_fill_dim(COLORREF c, int alpha_pct, COLORREF bg)
{
    // theme_color_dim clamps the scaled opacity to [0,100]
    return theme_color_dim(c, alpha_pct * 100 / (100 - TA_FILL_TRANSPARENCY), bg);
}

// Dark theme names (index matches get_theme dark branch)
static const char* DARK_THEME_NAMES[] = {
    "Modern", "Terminal", "Cyberpunk", "Neon Noir", "Phosphor",
    "Fire & Ice", "Slate", "Blood & Greed", "Gold Standard", "Ultraviolet",
    "Infrared", "Toxic", "Crimson Tide", "Vaporwave", "Matrix", "Arctic"
};

// Light theme names (index matches get_theme light branch)
static const char* LIGHT_THEME_NAMES[] = {
    "Classic", "Woodland", "Solar", "Twilight", "Parchment",
    "Shoreline", "Graphite", "Harvest", "Guilded", "Amethyst",
    "Forge", "Briar", "Scarlet", "Dusk", "Fern", "Nordic"
};
