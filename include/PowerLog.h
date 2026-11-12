// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Power curve for charts: average Hoymiles / SolarEdge AC power per 5 minutes
 * for today and yesterday (2 x 288 slots). Survives power loss: LittleFS
 * /power.bin, written every 10 minutes and at midnight (tmp file + rename).
 */
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <TaskSchedulerDeclarations.h>
#include <mutex>

#define POWERLOG_STEP_MIN 5
#define POWERLOG_SLOTS (24 * 60 / POWERLOG_STEP_MIN)
#define POWERLOG_NONE 0xFFFF

struct PowerDay {
    char date[11] = { 0 };
    uint16_t hoy[POWERLOG_SLOTS];
    uint16_t se[POWERLOG_SLOTS];
    void clear();
};

class PowerLogClass {
public:
    PowerLogClass();
    void init(Scheduler& scheduler);
    // which: 0 = today, 1 = yesterday
    void toJson(JsonObject root, uint8_t which);
    // copies today's curve in W, POWERLOG_NONE = no data
    void getToday(uint16_t* hoy, uint16_t* se, size_t n, uint16_t& currentSlot);
    void reset();

private:
    void loop();
    bool load();
    bool save();
    void closeSlot();

    std::mutex _mutex;
    PowerDay _today, _yesterday;
    int _slot = -1;
    float _sumHoy = 0, _sumSe = 0;
    uint16_t _n = 0;
    uint32_t _lastSave = 0;
    bool _dirty = false;
    Task _loopTask;
};

extern PowerLogClass PowerLog;
