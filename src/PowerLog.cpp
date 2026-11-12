// SPDX-License-Identifier: GPL-2.0-or-later
#include "PowerLog.h"
#include "Datastore.h"
#include "SolarEdge.h"
#include <LittleFS.h>
#include <time.h>

#undef TAG
static const char* TAG = "powerlog";
static const char* FILE_NAME = "/power.bin";
static const char* TMP_NAME = "/power.tmp";
static const uint32_t MAGIC = 0x50574C31; // "PWL1"

PowerLogClass PowerLog;

void PowerDay::clear()
{
    date[0] = 0;
    for (int i = 0; i < POWERLOG_SLOTS; i++) hoy[i] = se[i] = POWERLOG_NONE;
}

PowerLogClass::PowerLogClass()
    : _loopTask(10 * TASK_SECOND, TASK_FOREVER, std::bind(&PowerLogClass::loop, this))
{
    _today.clear();
    _yesterday.clear();
}

void PowerLogClass::init(Scheduler& scheduler)
{
    load();
    scheduler.addTask(_loopTask);
    _loopTask.enable();
}

static uint16_t clampW(float w)
{
    if (w < 0) w = 0;
    if (w > 65000) w = 65000;
    return (uint16_t)lroundf(w);
}

void PowerLogClass::closeSlot()
{
    if (_slot < 0 || _n == 0) return;
    _today.hoy[_slot] = clampW(_sumHoy / _n);
    _today.se[_slot] = clampW(_sumSe / _n);
    _dirty = true;
}

void PowerLogClass::loop()
{
    struct tm t;
    if (!getLocalTime(&t, 5) || t.tm_year < (2024 - 1900)) return;
    char date[11];
    strftime(date, sizeof(date), "%Y-%m-%d", &t);
    const int slot = (t.tm_hour * 60 + t.tm_min) / POWERLOG_STEP_MIN;

    const float hoyW = Datastore.getTotalAcPowerEnabled();
    const SolarEdgeData d = SolarEdge.getData();
    const float seW = d.valid ? d.acPower : 0;

    std::lock_guard<std::mutex> lock(_mutex);
    if (strcmp(_today.date, date) != 0) {
        if (_today.date[0]) {
            closeSlot();
            _yesterday = _today;
        }
        _today.clear();
        strlcpy(_today.date, date, sizeof(_today.date));
        _slot = -1;
        _n = 0;
        _dirty = true;
        _lastSave = 0; // save right after midnight
    }
    if (slot != _slot) {
        closeSlot();
        _slot = slot;
        _sumHoy = _sumSe = 0;
        _n = 0;
    }
    _sumHoy += hoyW;
    _sumSe += seW;
    _n++;
    // live value of the running slot
    _today.hoy[_slot] = clampW(_sumHoy / _n);
    _today.se[_slot] = clampW(_sumSe / _n);

    if (_dirty && (_lastSave == 0 || millis() - _lastSave > 10 * 60 * 1000UL)) {
        if (save()) {
            _dirty = false;
            _lastSave = millis();
        }
    }
}

bool PowerLogClass::save()
{
    File f = LittleFS.open(TMP_NAME, "w");
    if (!f) return false;
    bool ok = f.write((const uint8_t*)&MAGIC, 4) == 4;
    ok &= f.write((const uint8_t*)&_today, sizeof(PowerDay)) == sizeof(PowerDay);
    ok &= f.write((const uint8_t*)&_yesterday, sizeof(PowerDay)) == sizeof(PowerDay);
    f.close();
    if (!ok) {
        LittleFS.remove(TMP_NAME);
        return false;
    }
    LittleFS.remove(FILE_NAME);
    return LittleFS.rename(TMP_NAME, FILE_NAME);
}

bool PowerLogClass::load()
{
    File f = LittleFS.open(FILE_NAME, "r");
    if (!f) return false;
    uint32_t magic = 0;
    PowerDay* a = new PowerDay();
    PowerDay* b = new PowerDay();
    bool ok = f.read((uint8_t*)&magic, 4) == 4 && magic == MAGIC
        && f.read((uint8_t*)a, sizeof(PowerDay)) == sizeof(PowerDay)
        && f.read((uint8_t*)b, sizeof(PowerDay)) == sizeof(PowerDay);
    f.close();
    if (ok) {
        a->date[10] = 0;
        b->date[10] = 0;
        std::lock_guard<std::mutex> lock(_mutex);
        _today = *a; // if the date is old, loop() moves it to "yesterday"
        _yesterday = *b;
        ESP_LOGI(TAG, "loaded %s / %s", _today.date, _yesterday.date);
    }
    delete a;
    delete b;
    return ok;
}

void PowerLogClass::toJson(JsonObject root, uint8_t which)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const PowerDay& d = which ? _yesterday : _today;
    root["date"] = d.date;
    root["step_min"] = POWERLOG_STEP_MIN;
    if (!which) root["slot"] = _slot;
    auto h = root["hoy"].to<JsonArray>();
    auto s = root["se"].to<JsonArray>();
    for (int i = 0; i < POWERLOG_SLOTS; i++) {
        if (d.hoy[i] == POWERLOG_NONE) h.add(nullptr); else h.add(d.hoy[i]);
        if (d.se[i] == POWERLOG_NONE) s.add(nullptr); else s.add(d.se[i]);
    }
}

void PowerLogClass::getToday(uint16_t* hoy, uint16_t* se, size_t n, uint16_t& currentSlot)
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (size_t i = 0; i < n && i < POWERLOG_SLOTS; i++) {
        hoy[i] = _today.hoy[i];
        se[i] = _today.se[i];
    }
    currentSlot = _slot < 0 ? 0 : _slot;
}

void PowerLogClass::reset()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _today.clear();
    _yesterday.clear();
    _slot = -1;
    _n = 0;
    LittleFS.remove(FILE_NAME);
}
