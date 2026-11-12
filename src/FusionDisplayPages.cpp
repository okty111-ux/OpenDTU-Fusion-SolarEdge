// SPDX-License-Identifier: GPL-2.0-or-later
#include "FusionDisplayPages.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define W 256
#define FONT_SMALL u8g2_font_helvR08_te
#define FONT_SMALLB u8g2_font_helvB08_te
#define FONT_ROW u8g2_font_6x12_te
#define FONT_MID u8g2_font_helvB12_te
#define FONT_BIG u8g2_font_helvB24_te

static const char* const TITLES[FD_PAGE_COUNT] = {
    "Produkcja", "Wykres mocy - dziś", "Sieć", "Hoymiles MPPT", "Panele (szacunek)", "SolarEdge", "Historia", "Status"
};

const char* fdPageTitle(uint8_t page) { return page < FD_PAGE_COUNT ? TITLES[page] : ""; }

// ---------- number formatting (decimal comma) ----------
static void fnum(char* b, size_t n, float v, int dec)
{
    snprintf(b, n, "%.*f", dec, v);
    for (char* p = b; *p; p++) if (*p == '.') *p = ',';
}
static void fpow(char* b, size_t n, float w)
{
    if (fabsf(w) >= 1000) {
        char t[16];
        fnum(t, sizeof(t), w / 1000.0f, fabsf(w) >= 10000 ? 1 : 2);
        snprintf(b, n, "%s kW", t);
    } else {
        snprintf(b, n, "%.0f W", w);
    }
}
static void fkwh(char* b, size_t n, float k)
{
    if (k < 0) { snprintf(b, n, "-"); return; }
    char t[16];
    if (k >= 1000) { fnum(t, sizeof(t), k / 1000.0f, 2); snprintf(b, n, "%s MWh", t); return; }
    fnum(t, sizeof(t), k, k >= 100 ? 1 : 2);
    snprintf(b, n, "%s kWh", t);
}
static float sum2(float a, float b)
{
    if (a < 0 && b < 0) return -1;
    return (a < 0 ? 0 : a) + (b < 0 ? 0 : b);
}

// ---------- drawing helpers ----------
static void txt(u8g2_t* u, int x, int y, const char* s) { u8g2_DrawUTF8(u, x, y, s); }
static void rtxt(u8g2_t* u, int xr, int y, const char* s) { u8g2_DrawUTF8(u, xr - u8g2_GetUTF8Width(u, s), y, s); }
static void ctxt(u8g2_t* u, int xc, int y, const char* s) { u8g2_DrawUTF8(u, xc - u8g2_GetUTF8Width(u, s) / 2, y, s); }

static void header(u8g2_t* u, const FdData& d, uint8_t page, uint8_t no, uint8_t cnt, bool autoRotate)
{
    u8g2_SetFont(u, FONT_SMALLB);
    txt(u, 0, 8, TITLES[page]);
    u8g2_SetFont(u, FONT_SMALL);
    char b[24];
    snprintf(b, sizeof(b), "%u/%u", no, cnt);
    const int pw = u8g2_GetUTF8Width(u, b);
    rtxt(u, W, 8, b);
    if (!autoRotate) { // small "pause" marker = auto switching off
        u8g2_DrawBox(u, W - pw - 9, 2, 2, 6);
        u8g2_DrawBox(u, W - pw - 5, 2, 2, 6);
    }
    if (d.timeValid) rtxt(u, W - pw - (autoRotate ? 8 : 16), 8, d.clock);
    u8g2_DrawHLine(u, 0, 11, W);
}

// 4 content rows for FONT_ROW
static const int ROW_Y[4] = { 23, 35, 47, 59 };

static void kvRow(u8g2_t* u, int x, int xr, int y, const char* k, const char* v)
{
    txt(u, x, y, k);
    rtxt(u, xr, y, v);
}

// ---------- pages ----------
static void pagePower(u8g2_t* u, const FdData& d)
{
    char b[32], c[32];
    const float tot = d.hoyW + d.seW;
    const bool producing = tot >= 1.0f;
    u8g2_SetFont(u, FONT_BIG);
    fpow(b, sizeof(b), tot);
    txt(u, 0, 44, b);
    u8g2_SetFont(u, FONT_ROW);
    fkwh(c, sizeof(c), sum2(d.todayHoy, d.todaySe));
    snprintf(b, sizeof(b), "Dziś %s", c);
    txt(u, 0, 60, b);
    if (!producing) {
        u8g2_SetFont(u, FONT_SMALL);
        rtxt(u, 134, 24, "brak produkcji");
        u8g2_SetFont(u, FONT_ROW);
    }

    const int x = 150;
    u8g2_DrawVLine(u, x - 8, 15, 48);
    fpow(b, sizeof(b), d.hoyW);
    kvRow(u, x, W, ROW_Y[0], "Hoymiles", d.hoyReachable ? b : "offline");
    fpow(b, sizeof(b), d.seW);
    kvRow(u, x, W, ROW_Y[1], "SolarEdge", !d.seConfigured ? "-" : (d.seValid ? b : "offline"));
    fkwh(b, sizeof(b), d.todayHoy);
    kvRow(u, x, W, ROW_Y[2], "dziś H", b);
    fkwh(b, sizeof(b), d.todaySe);
    kvRow(u, x, W, ROW_Y[3], "dziś SE", b);
}

static void pageGrid(u8g2_t* u, const FdData& d)
{
    char b[32];
    const bool exp = d.gridW >= 0;
    u8g2_SetFont(u, FONT_SMALLB);
    txt(u, 0, 24, exp ? "EKSPORT DO SIECI" : "POBÓR Z SIECI");
    rtxt(u, 134, 24, "teraz");
    u8g2_SetFont(u, FONT_BIG);
    fpow(b, sizeof(b), fabsf(d.gridW));
    txt(u, 0, 56, b);

    const int x = 150;
    u8g2_DrawVLine(u, x - 8, 15, 48);
    u8g2_SetFont(u, FONT_ROW);
    fkwh(b, sizeof(b), d.todayExp);
    kvRow(u, x, W, ROW_Y[0], "eksport", b);
    fkwh(b, sizeof(b), d.todayImp);
    kvRow(u, x, W, ROW_Y[1], "pobór", b);
    const float prod = sum2(d.todayHoy, d.todaySe);
    if (prod > 0 && d.todayExp >= 0) {
        float self = prod - d.todayExp;
        if (self < 0) self = 0;
        fkwh(b, sizeof(b), self);
        kvRow(u, x, W, ROW_Y[2], "zużyte", b);
        snprintf(b, sizeof(b), "%.0f %%", 100.0f * self / prod);
        kvRow(u, x, W, ROW_Y[3], "udział", b);
    }
}

static void pageMppt(u8g2_t* u, const FdData& d)
{
    char b[32];
    u8g2_SetFont(u, FONT_ROW);
    const uint8_t n = d.mpptCount > 4 ? 4 : d.mpptCount; // 4 rows per screen
    for (uint8_t k = 0; k < n; k++) {
        const FdMppt& m = d.mppt[k];
        const int y = ROW_Y[k];
        if (d.multiInverter) snprintf(b, sizeof(b), "%u.%u", m.inv, m.ch);
        else snprintf(b, sizeof(b), "MPPT%u", m.ch);
        txt(u, 0, y, b);
        if (m.reachable) {
            char t[16];
            fnum(t, sizeof(t), m.u, 1); snprintf(b, sizeof(b), "%s V", t); rtxt(u, 92, y, b);
            fnum(t, sizeof(t), m.i, 2); snprintf(b, sizeof(b), "%s A", t); rtxt(u, 140, y, b);
            snprintf(b, sizeof(b), "%.0f W", m.p); rtxt(u, 190, y, b);
        } else {
            rtxt(u, 160, y, "brak odczytu");
        }
        fkwh(b, sizeof(b), m.yd / 1000.0f);
        rtxt(u, W, y, b);
    }
}

static void pagePanels(u8g2_t* u, const FdData& d)
{
    char b[32];
    u8g2_SetFont(u, FONT_ROW);
    const uint8_t n = d.mpptCount > 4 ? 4 : d.mpptCount;
    u8g2_DrawVLine(u, 140, 15, 48);
    for (uint8_t k = 0; k < n; k++) {
        const FdMppt& m = d.mppt[k];
        const int y = ROW_Y[k];
        snprintf(b, sizeof(b), "%u", m.ch);
        u8g2_SetFont(u, FONT_SMALLB);
        txt(u, 0, y, b);
        u8g2_SetFont(u, FONT_ROW);
        if (m.panelCount == 0) {
            txt(u, 12, y, "brak danych paneli");
            continue;
        }
        for (uint8_t j = 0; j < m.panelCount && j < 2; j++) {
            const FdPanel& p = m.panel[j];
            const int x0 = j ? 148 : 12, xr = j ? W : 134;
            char nm[12];
            strncpy(nm, p.name, sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0;
            if (strlen(nm) > 8) nm[8] = 0;
            txt(u, x0, y, nm);
            if (m.reachable) snprintf(b, sizeof(b), "~%.0f W", p.p);
            else snprintf(b, sizeof(b), "-");
            rtxt(u, xr, y, b);
        }
    }
}

static void pageSolarEdge(u8g2_t* u, const FdData& d)
{
    char b[32];
    u8g2_SetFont(u, FONT_ROW);
    if (!d.seValid) {
        txt(u, 0, ROW_Y[0], "Brak połączenia Modbus");
        txt(u, 0, ROW_Y[1], "Sprawdź IP i port w ustawieniach");
        return;
    }
    const int m = 128;
    fpow(b, sizeof(b), d.seW);            kvRow(u, 0, m - 8, ROW_Y[0], "Moc AC", b);
    fkwh(b, sizeof(b), d.todaySe);        kvRow(u, 0, m - 8, ROW_Y[1], "Dziś", b);
    fkwh(b, sizeof(b), d.seTotalKWh);     kvRow(u, 0, m - 8, ROW_Y[2], "Łącznie", b);
    txt(u, 0, ROW_Y[3], d.seModel);
    u8g2_DrawVLine(u, m, 15, 48);
    kvRow(u, m + 8, W, ROW_Y[0], "Stan", d.seStatus);
    char t[12];
    fnum(t, sizeof(t), d.seTemp, 0); snprintf(b, sizeof(b), "%s °C", t);
    kvRow(u, m + 8, W, ROW_Y[1], "Radiator", b);
    fnum(t, sizeof(t), d.seDcV, 0); snprintf(b, sizeof(b), "%s V", t);
    kvRow(u, m + 8, W, ROW_Y[2], "DC", b);
}

static void histRow(u8g2_t* u, int y, const char* label, float h, float s)
{
    char b[32], bh[16], bs[16];
    u8g2_SetFont(u, FONT_ROW);
    txt(u, 0, y, label);
    fkwh(b, sizeof(b), sum2(h, s));
    rtxt(u, 150, y, b);
    u8g2_SetFont(u, FONT_SMALL);
    fnum(bh, sizeof(bh), h < 0 ? 0 : h, h >= 100 ? 0 : 1);
    fnum(bs, sizeof(bs), s < 0 ? 0 : s, s >= 100 ? 0 : 1);
    snprintf(b, sizeof(b), "H %s · SE %s", bh, bs);
    rtxt(u, W, y, b);
}

static void pageHistory(u8g2_t* u, const FdData& d)
{
    histRow(u, ROW_Y[0], "Wczoraj", d.hasYesterday ? d.yHoy : -1, d.hasYesterday ? d.ySe : -1);
    histRow(u, ROW_Y[1], "Ten miesiąc", d.mHoy, d.mSe);
    histRow(u, ROW_Y[2], "Ten rok", d.yrHoy, d.yrSe);
    histRow(u, ROW_Y[3], "Łącznie", d.ltHoy, d.ltSe);
}

static void pageStatus(u8g2_t* u, const FdData& d)
{
    char b[40];
    u8g2_SetFont(u, FONT_ROW);
    kvRow(u, 0, W, ROW_Y[0], "IP", d.ip[0] ? d.ip : "brak");
    kvRow(u, 0, W, ROW_Y[1], "Sieć", d.net);
    kvRow(u, 0, W, ROW_Y[2], "Karta SD", d.sd);
    const uint32_t dd = d.uptimeS / 86400, hh = (d.uptimeS / 3600) % 24, mm = (d.uptimeS / 60) % 60;
    if (dd) snprintf(b, sizeof(b), "%u/%u online · %u d %u h", d.invOnline, d.invTotal, (unsigned)dd, (unsigned)hh);
    else snprintf(b, sizeof(b), "%u/%u online · %u h %u min", d.invOnline, d.invTotal, (unsigned)hh, (unsigned)mm);
    kvRow(u, 0, W, ROW_Y[3], "Inwertery", b);
}


// ---------- power curve ----------
static void hatchPix(u8g2_t* u, int x, int y0, int y1)
{
    // SolarEdge share: 50 % dither, so the two sources are distinguishable on a mono OLED
    for (int y = y0; y <= y1; y++) if (((x + y) & 1) == 0) u8g2_DrawPixel(u, x, y);
}

static void pageChart(u8g2_t* u, const FdData& d)
{
    const int X0 = 54, X1 = W - 1, TOP = 15, BOT = 53; // plot area
    const int PW = X1 - X0 + 1;
    const int per = 12; // slots per hour
    int first = -1, last = -1, peakI = -1;
    uint32_t peak = 0;
    float wh = 0;
    for (int i = 0; i < FD_CURVE_SLOTS; i++) {
        const uint16_t a = d.curveHoy[i], s = d.curveSe[i];
        if (a == FD_CURVE_NONE && s == FD_CURVE_NONE) continue;
        const uint32_t t = (a == FD_CURVE_NONE ? 0 : a) + (s == FD_CURVE_NONE ? 0 : s);
        if (t > 0) { if (first < 0) first = i; last = i; }
        if (t > peak) { peak = t; peakI = i; }
        wh += t * 5.0f / 60.0f;
    }
    // time window: data +-1 h, at least 8 h, default 06-20
    int h0 = 6, h1 = 20;
    if (first >= 0) {
        h0 = first / per - 1; if (h0 < 0) h0 = 0;
        h1 = (last + per) / per + 1; if (h1 > 24) h1 = 24;
        if (h1 - h0 < 8) { int c = (h0 + h1) / 2; h0 = c - 4; if (h0 < 0) h0 = 0; h1 = h0 + 8; if (h1 > 24) { h1 = 24; h0 = 16; } }
    }
    const int s0 = h0 * per, sN = (h1 - h0) * per;

    // scale: 1 / 1,5 / 2 / 2,5 / 3 / 4 / 5 / 6 / 8 / 10 x 10^n
    float ymax = 1000;
    if (peak > 0) {
        const float r = peak * 1.05f;
        float e = powf(10.0f, floorf(log10f(r)));
        static const float F[] = { 1, 1.5f, 2, 2.5f, 3, 4, 5, 6, 8, 10 };
        for (float f : F) if (f * e >= r) { ymax = f * e; break; }
    }

    // left column
    char b[24], c[24];
    u8g2_SetFont(u, FONT_SMALL);
    txt(u, 0, 22, "Szczyt");
    u8g2_SetFont(u, FONT_SMALLB);
    fpow(b, sizeof(b), (float)peak);
    txt(u, 0, 32, peak ? b : "-");
    u8g2_SetFont(u, FONT_SMALL);
    if (peakI >= 0 && peak) { snprintf(b, sizeof(b), "o %02d:%02d", peakI / 12, (peakI % 12) * 5); txt(u, 0, 42, b); }
    fkwh(c, sizeof(c), (d.todayHoy >= 0 || d.todaySe >= 0) ? sum2(d.todayHoy, d.todaySe) : wh / 1000.0f);
    txt(u, 0, 53, c);

    // grid: dotted top / half lines, scale label right
    for (int x = X0; x <= X1; x += 3) { u8g2_DrawPixel(u, x, TOP); u8g2_DrawPixel(u, x, (TOP + BOT) / 2); }
    if (ymax >= 1000) {
        const float k = ymax / 1000.0f;
        if (fabsf(k - roundf(k)) < 0.01f) snprintf(b, sizeof(b), "%d kW", (int)roundf(k));
        else { snprintf(b, sizeof(b), "%.1f kW", k); for (char* p = b; *p; p++) if (*p == '.') *p = ','; }
    } else snprintf(b, sizeof(b), "%d W", (int)ymax);
    {
        const int bw = u8g2_GetUTF8Width(u, b);
        u8g2_SetDrawColor(u, 0); u8g2_DrawBox(u, X1 - bw - 1, TOP - 1, bw + 2, 9); u8g2_SetDrawColor(u, 1);
        txt(u, X1 - bw, TOP + 7, b);
    }

    // curve (column by column; hoy = solid, SolarEdge = dither on top, outline solid)
    int prevTop = -1;
    for (int x = 0; x < PW; x++) {
        const int sa = s0 + x * sN / PW;
        int se_ = s0 + (x + 1) * sN / PW; if (se_ <= sa) se_ = sa + 1;
        if (sa >= FD_CURVE_SLOTS) break;
        if (sa > (int)d.curveSlot) { prevTop = -1; continue; } // future
        uint32_t ha = 0, sa2 = 0; int n = 0;
        for (int i = sa; i < se_ && i < FD_CURVE_SLOTS; i++) {
            if (d.curveHoy[i] == FD_CURVE_NONE && d.curveSe[i] == FD_CURVE_NONE) continue;
            ha += d.curveHoy[i] == FD_CURVE_NONE ? 0 : d.curveHoy[i];
            sa2 += d.curveSe[i] == FD_CURVE_NONE ? 0 : d.curveSe[i];
            n++;
        }
        if (!n) { prevTop = -1; continue; }
        const float hv = (float)ha / n, sv = (float)sa2 / n;
        int yh = BOT - (int)lroundf((BOT - TOP) * fminf(hv, ymax) / ymax);
        int yt = BOT - (int)lroundf((BOT - TOP) * fminf(hv + sv, ymax) / ymax);
        const int px = X0 + x;
        if (yh < BOT) u8g2_DrawVLine(u, px, yh, BOT - yh);
        if (yt < yh) hatchPix(u, px, yt, yh - 1);
        // outline of total, connected to previous column
        if (prevTop >= 0) {
            const int a = prevTop < yt ? prevTop : yt, z = prevTop < yt ? yt : prevTop;
            u8g2_DrawVLine(u, px, a, z - a + 1);
        } else u8g2_DrawPixel(u, px, yt);
        prevTop = yt;
    }

    // axis + hour labels
    u8g2_DrawHLine(u, X0, BOT, PW);
    const int span = h1 - h0, every = span > 12 ? 3 : 2;
    for (int hh = h0; hh <= h1; hh++) {
        const int x = X0 + (hh - h0) * (PW - 1) / span;
        u8g2_DrawVLine(u, x, BOT, 2);
        if (hh % every == 0 && hh < h1) {
            snprintf(b, sizeof(b), "%d", hh);
            const int bw = u8g2_GetUTF8Width(u, b);
            int lx = x - bw / 2; if (lx < X0) lx = X0;
            txt(u, lx, 63, b);
        }
    }
    // "now" marker
    if (d.timeValid && (int)d.curveSlot >= s0 && (int)d.curveSlot <= s0 + sN) {
        const int x = X0 + ((int)d.curveSlot - s0) * PW / sN;
        for (int y = TOP + 9; y < BOT; y += 2) u8g2_DrawPixel(u, x, y);
    }
    if (first < 0) {
        u8g2_SetDrawColor(u, 0); u8g2_DrawBox(u, X0 + 40, 26, PW - 80, 14); u8g2_SetDrawColor(u, 1);
        ctxt(u, X0 + PW / 2, 36, d.timeValid ? "Dziś brak produkcji" : "Brak czasu (NTP)");
    }
}

bool fdPageAvailable(const FdData& d, uint8_t page)
{
    switch (page) {
    case FD_PAGE_GRID: return d.meterValid;
    case FD_PAGE_MPPT: return d.mpptCount > 0;
    case FD_PAGE_PANELS:
        for (uint8_t k = 0; k < d.mpptCount; k++) if (d.mppt[k].panelCount > 1) return true;
        return false;
    case FD_PAGE_SOLAREDGE: return d.seConfigured;
    case FD_PAGE_CHART: return d.timeValid;
    default: return page < FD_PAGE_COUNT;
    }
}

void fdRenderPage(u8g2_t* u, const FdData& d, uint8_t page, uint8_t no, uint8_t cnt, bool autoRotate)
{
    u8g2_SetFontMode(u, 1);
    u8g2_SetDrawColor(u, 1);
    header(u, d, page, no, cnt, autoRotate);
    switch (page) {
    case FD_PAGE_POWER: pagePower(u, d); break;
    case FD_PAGE_CHART: pageChart(u, d); break;
    case FD_PAGE_GRID: pageGrid(u, d); break;
    case FD_PAGE_MPPT: pageMppt(u, d); break;
    case FD_PAGE_PANELS: pagePanels(u, d); break;
    case FD_PAGE_SOLAREDGE: pageSolarEdge(u, d); break;
    case FD_PAGE_HISTORY: pageHistory(u, d); break;
    case FD_PAGE_STATUS: pageStatus(u, d); break;
    }
}

void fdRenderOverlay(u8g2_t* u, const char* l1, const char* l2)
{
    // clear content area below the header, then a centered message
    u8g2_SetDrawColor(u, 0);
    u8g2_DrawBox(u, 0, 12, W, 52);
    u8g2_SetDrawColor(u, 1);
    u8g2_SetFont(u, FONT_MID);
    ctxt(u, W / 2, l2 ? 34 : 42, l1);
    if (l2) {
        u8g2_SetFont(u, FONT_ROW);
        ctxt(u, W / 2, 54, l2);
    }
}
