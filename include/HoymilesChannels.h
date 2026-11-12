// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Per-MPPT (DC channel) readings of all Hoymiles inverters + per-panel estimate.
 *
 * MIT-5000-8T: 4 MPPT, 1 input per MPPT, typically 2 modules in SERIES on one
 * input. The inverter only measures the whole string (U, I, P), so per-panel
 * values are estimated from the panel data entered by the user:
 *   series:   I_panel = I_string,  U_k = U * w_k,  P_k = P * w_k
 *   parallel: U_panel = U_string,  I_k = I * w_k,  P_k = P * w_k
 * where w_k = Vmp_k / sum(Vmp) (series, if Vmp of all panels given) or
 *       w_k = Wp_k  / sum(Wp)  (otherwise).
 * Config: LittleFS /mppt.json
 */
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <mutex>
#include <vector>

#define MPPT_MAX_PANELS 2

struct PanelCfg {
    String name;
    uint16_t wp = 0;   // rated power, 0 = not used
    float vmp = 0;     // optional, V
};

struct MpptCfg {
    bool series = true;
    PanelCfg panel[MPPT_MAX_PANELS];
    uint8_t count() const;
};

struct PanelEstimate {
    String name;
    uint16_t wp;
    float u, i, p, yieldDay; // V, A, W, Wh
};

struct MpptReading {
    String inverter;
    String serial;
    uint8_t invIndex;  // 1-based
    uint8_t channel;   // 1-based MPPT number
    String name;       // channel name from OpenDTU settings
    bool reachable;
    float u, i, p;     // V, A, W
    float yieldDay;    // Wh
    float yieldTotal;  // kWh
    bool series;
    std::vector<PanelEstimate> panels;
};

class HoymilesChannelsClass {
public:
    void init();
    std::vector<MpptReading> read();
    void toJson(JsonArray arr);
    void configToJson(JsonObject root);
    bool configFromJson(JsonObjectConst root);

private:
    MpptCfg getCfg(const String& serial, uint8_t ch);
    void load();
    bool save();
    std::mutex _mutex;
    JsonDocument _cfg; // { "<serial>": [ {series, panels:[{name,wp,vmp}]} x channels ] }
};

extern HoymilesChannelsClass HoymilesChannels;
