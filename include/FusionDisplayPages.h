// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Page rendering for the SSD1322 256x64 OLED (U8g2 C API only, no Arduino
 * dependencies -> the same code is compiled natively for preview images).
 */
#pragma once
#include <stdint.h>
#include "clib/u8g2.h"

#define FD_MAX_MPPT 8
#define FD_CURVE_SLOTS 288 // 5 min slots per day
#define FD_CURVE_NONE 0xFFFF

enum FdPage : uint8_t {
    FD_PAGE_POWER = 0, // total production now + today
    FD_PAGE_CHART,     // today's power curve (5 min averages)
    FD_PAGE_GRID,      // SolarEdge meter: export/import
    FD_PAGE_MPPT,      // Hoymiles MPPT U/I/P/today
    FD_PAGE_PANELS,    // per-panel estimate
    FD_PAGE_SOLAREDGE, // SolarEdge inverter details
    FD_PAGE_HISTORY,   // yesterday / month / year / lifetime
    FD_PAGE_STATUS,    // IP, network, SD, inverters
    FD_PAGE_COUNT
};

struct FdPanel {
    char name[12];
    uint16_t wp;
    float p;
};

struct FdMppt {
    uint8_t inv, ch;
    bool reachable;
    float u, i, p, yd; // V, A, W, Wh
    uint8_t panelCount;
    FdPanel panel[2];
};

struct FdData {
    char clock[6];      // "14:35"
    char date[11];      // "12.11.2026"
    bool timeValid;

    float hoyW, seW;
    bool hoyReachable, seConfigured, seValid;
    float todayHoy, todaySe;        // kWh, <0 unknown
    float peakW;

    bool meterValid;
    float gridW;                    // + export, - import
    float todayExp, todayImp;       // kWh, <0 unknown

    uint8_t mpptCount;
    bool multiInverter;
    FdMppt mppt[FD_MAX_MPPT];

    char seStatus[16];
    float seTemp, seTotalKWh, seDcV;
    char seModel[20];

    bool hasYesterday;
    float yHoy, ySe, mHoy, mSe, yrHoy, yrSe, ltHoy, ltSe; // kWh, <0 unknown

    // power curve of today (W per 5 min slot, FD_CURVE_NONE = no record)
    uint16_t curveHoy[FD_CURVE_SLOTS];
    uint16_t curveSe[FD_CURVE_SLOTS];
    uint16_t curveSlot; // current slot

    char ip[16];
    char net[24];       // "Wi-Fi -61 dBm" / "Ethernet"
    char sd[32];        // "29,7 GB, wolne 28,1 GB" / "brak karty"
    uint8_t invOnline, invTotal;
    uint32_t uptimeS;
};

#ifdef __cplusplus
extern "C++" {
#endif
// true if the page has data to show (others are skipped while switching)
bool fdPageAvailable(const FdData& d, uint8_t page);
// draws page into the u8g2 buffer (caller clears/sends)
void fdRenderPage(u8g2_t* u, const FdData& d, uint8_t page, uint8_t pageNo, uint8_t pageCnt, bool autoRotate);
// centered message box drawn on top (button feedback)
void fdRenderOverlay(u8g2_t* u, const char* line1, const char* line2);
// night / no-data clock screen
const char* fdPageTitle(uint8_t page);
#ifdef __cplusplus
}
#endif
