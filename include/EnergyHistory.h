// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Persistent energy history (daily / monthly / yearly) for
 * Hoymiles (all enabled inverters) + SolarEdge + SolarEdge meter.
 *
 * Daily energy is computed from the lifetime counters stored inside the
 * inverters (Hoymiles YieldTotal, SolarEdge AC lifetime energy, meter
 * import/export).  Only small "day start" snapshots and finished records
 * are written to LittleFS (/history.json), so a power loss never loses
 * more than the production since the last save (default 10 min) and even
 * that is counted on the next day instead of being dropped.
 */
#pragma once

#include <ArduinoJson.h>
#include <TaskSchedulerDeclarations.h>
#include <deque>
#include <mutex>

#ifndef HISTORY_SAVE_INTERVAL_MIN
#define HISTORY_SAVE_INTERVAL_MIN 10
#endif
#ifndef HISTORY_MAX_DAYS
#define HISTORY_MAX_DAYS 62
#endif
#ifndef HISTORY_MAX_MONTHS
#define HISTORY_MAX_MONTHS 36
#endif
#ifndef HISTORY_MAX_YEARS
#define HISTORY_MAX_YEARS 25
#endif
#ifndef HISTORY_PRODUCING_THRESHOLD_W
#define HISTORY_PRODUCING_THRESHOLD_W 5.0f
#endif

// one aggregated period (day, month or year); energies in kWh, -1 = unknown
struct EnergyRecord {
    char key[11] = { 0 }; // "YYYY-MM-DD", "YYYY-MM" or "YYYY"
    float hoy = 0;        // Hoymiles production
    float se = 0;         // SolarEdge production
    float exp = -1;       // meter export to grid
    float imp = -1;       // meter import from grid
    float hoyPeak = 0;    // W (days only)
    float sePeak = 0;     // W (days only)
};

// lifetime counter set
struct Counters {
    float hoy = -1; // kWh, -1 = unknown
    float se = -1;
    float exp = -1;
    float imp = -1;
};

struct EnergySummary {
    bool valid = false;  // date known
    EnergyRecord today, yesterday, month, year;
    bool hasYesterday = false;
    Counters lifetime;
};
class EnergyHistoryClass {
public:
    EnergyHistoryClass();
    void init(Scheduler& scheduler);

    void toJson(JsonVariant root);
    EnergyRecord getToday();
    Counters getLastCounters();
    bool getDay(const char* key, EnergyRecord& out);
    bool getLastClosedDay(EnergyRecord& out);
    EnergySummary getSummary();
    void reset();

private:
    void loop();
    void mqttPublish();
    void readLiveCounters(Counters& c, float& hoyW, float& seW);
    void rollover(const char* newDate);
    EnergyRecord todayRecord() const;
    static void addTo(std::deque<EnergyRecord>& list, const char* key, const EnergyRecord& day, size_t max);
    static float delta(float now, float start);

    bool load();
    bool save();

    std::mutex _mutex;

    // current day state
    char _date[11] = { 0 };
    Counters _start; // counters at beginning of the day
    Counters _last;  // last valid counters seen
    float _hoyPeak = 0, _sePeak = 0;
    char _firstProd[6] = { 0 }, _lastProd[6] = { 0 };

    std::deque<EnergyRecord> _days, _months, _years;

    uint32_t _lastSaveMs = 0;
    bool _dirty = false;
    bool _loaded = false;

    Task _loopTask;
    Task _mqttTask;
};

extern EnergyHistoryClass EnergyHistory;
