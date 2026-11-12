// SPDX-License-Identifier: GPL-2.0-or-later
#include "EnergyHistory.h"
#include "Configuration.h"
#include "Datastore.h"
#include "HoymilesChannels.h"
#include "MqttSettings.h"
#include "SolarEdge.h"
#include "SunPosition.h"
#include <LittleFS.h>
#include <time.h>

#undef TAG
static const char* TAG = "history";

#define HISTORY_FILE "/history.json"
#define HISTORY_TMP "/history.tmp"

EnergyHistoryClass EnergyHistory;

EnergyHistoryClass::EnergyHistoryClass()
    : _loopTask(10 * TASK_SECOND, TASK_FOREVER, std::bind(&EnergyHistoryClass::loop, this))
    , _mqttTask(30 * TASK_SECOND, TASK_FOREVER, std::bind(&EnergyHistoryClass::mqttPublish, this))
{
}

void EnergyHistoryClass::init(Scheduler& scheduler)
{
    _loaded = load();
    scheduler.addTask(_loopTask);
    _loopTask.enable();
    scheduler.addTask(_mqttTask);
    _mqttTask.enable();
    ESP_LOGI(TAG, "History %s (%u days, %u months, %u years)", _loaded ? "loaded" : "empty",
        _days.size(), _months.size(), _years.size());
}

float EnergyHistoryClass::delta(float now, float start)
{
    if (now < 0 || start < 0 || now < start) {
        return now < 0 || start < 0 ? -1 : 0;
    }
    return now - start;
}

// ------------------------------------------------------------------ live
void EnergyHistoryClass::readLiveCounters(Counters& c, float& hoyW, float& seW)
{
    hoyW = Datastore.getTotalAcPowerEnabled();
    // Only trust Hoymiles total when every enabled inverter answered,
    // otherwise (night, reboot) the sum may be 0 or incomplete.
    const float hoyTotal = Datastore.getTotalAcYieldTotalEnabled();
    c.hoy = (Datastore.getIsAllEnabledReachable() && hoyTotal > 0) ? hoyTotal : -1;

    const SolarEdgeData d = SolarEdge.getData();
    seW = d.valid ? d.acPower : 0;
    c.se = (d.valid && d.energyTotal > 0) ? d.energyTotal / 1000.0f : -1;
    c.exp = (d.valid && d.meterValid) ? d.meterExported / 1000.0f : -1;
    c.imp = (d.valid && d.meterValid) ? d.meterImported / 1000.0f : -1;
}

void EnergyHistoryClass::loop()
{
    struct tm t;
    if (!getLocalTime(&t, 5) || t.tm_year < (2024 - 1900)) {
        return; // no valid time yet (NTP not synced)
    }
    char today[11];
    strftime(today, sizeof(today), "%Y-%m-%d", &t);

    Counters now;
    float hoyW, seW;
    readLiveCounters(now, hoyW, seW);

    std::lock_guard<std::mutex> lock(_mutex);

    if (_date[0] == 0) {
        strlcpy(_date, today, sizeof(_date));
        _dirty = true;
    } else if (strcmp(_date, today) != 0) {
        rollover(today);
    }

    // update last-known counters (monotonic) and set missing day-start values
    auto upd = [&](float& last, float& start, float v) {
        if (v < 0) return;
        if (last < 0 || v > last) {
            last = v;
            _dirty = true;
        }
        if (start < 0) {
            start = v;
            _dirty = true;
        }
    };
    upd(_last.hoy, _start.hoy, now.hoy);
    upd(_last.se, _start.se, now.se);
    upd(_last.exp, _start.exp, now.exp);
    upd(_last.imp, _start.imp, now.imp);

    if (hoyW > _hoyPeak) _hoyPeak = hoyW;
    if (seW > _sePeak) _sePeak = seW;

    if (hoyW + seW > HISTORY_PRODUCING_THRESHOLD_W) {
        char hm[6];
        strftime(hm, sizeof(hm), "%H:%M", &t);
        if (_firstProd[0] == 0) strlcpy(_firstProd, hm, sizeof(_firstProd));
        strlcpy(_lastProd, hm, sizeof(_lastProd));
        _dirty = true;
    }

    if (_dirty && millis() - _lastSaveMs > HISTORY_SAVE_INTERVAL_MIN * 60000UL) {
        save();
    }
}

EnergyRecord EnergyHistoryClass::todayRecord() const
{
    EnergyRecord r;
    strlcpy(r.key, _date, sizeof(r.key));
    const float h = delta(_last.hoy, _start.hoy);
    const float s = delta(_last.se, _start.se);
    r.hoy = h < 0 ? 0 : h;
    r.se = s < 0 ? 0 : s;
    r.exp = delta(_last.exp, _start.exp);
    r.imp = delta(_last.imp, _start.imp);
    r.hoyPeak = _hoyPeak;
    r.sePeak = _sePeak;
    return r;
}

void EnergyHistoryClass::addTo(std::deque<EnergyRecord>& list, const char* key, const EnergyRecord& day, size_t max)
{
    if (list.empty() || strcmp(list.back().key, key) != 0) {
        EnergyRecord n;
        strlcpy(n.key, key, sizeof(n.key));
        list.push_back(n);
        while (list.size() > max) list.pop_front();
    }
    EnergyRecord& r = list.back();
    r.hoy += day.hoy;
    r.se += day.se;
    if (day.exp >= 0) r.exp = (r.exp < 0 ? 0 : r.exp) + day.exp;
    if (day.imp >= 0) r.imp = (r.imp < 0 ? 0 : r.imp) + day.imp;
    if (day.hoyPeak > r.hoyPeak) r.hoyPeak = day.hoyPeak;
    if (day.sePeak > r.sePeak) r.sePeak = day.sePeak;
}

// close the current day, start a new one (mutex held)
void EnergyHistoryClass::rollover(const char* newDate)
{
    const EnergyRecord day = todayRecord();
    ESP_LOGI(TAG, "Day %s closed: Hoymiles %.2f kWh, SolarEdge %.2f kWh", day.key, day.hoy, day.se);

    // days list
    if (_days.empty() || strcmp(_days.back().key, day.key) != 0) {
        _days.push_back(day);
        while (_days.size() > HISTORY_MAX_DAYS) _days.pop_front();
    }
    char month[8], year[5];
    strlcpy(month, day.key, sizeof(month));
    strlcpy(year, day.key, sizeof(year));
    addTo(_months, month, day, HISTORY_MAX_MONTHS);
    addTo(_years, year, day, HISTORY_MAX_YEARS);

    // New day starts at the last known counters. If the DTU was switched off,
    // production during the outage is counted today instead of being lost.
    strlcpy(_date, newDate, sizeof(_date));
    _start = _last;
    _hoyPeak = _sePeak = 0;
    _firstProd[0] = _lastProd[0] = 0;

    save();
}

// ------------------------------------------------------------- storage
static void recToJson(JsonObject o, const EnergyRecord& r, bool peaks)
{
    o["k"] = r.key;
    o["h"] = serialized(String(r.hoy, 3));
    o["s"] = serialized(String(r.se, 3));
    if (r.exp >= 0) o["e"] = serialized(String(r.exp, 3));
    if (r.imp >= 0) o["i"] = serialized(String(r.imp, 3));
    if (peaks) {
        o["hp"] = (int)r.hoyPeak;
        o["sp"] = (int)r.sePeak;
    }
}

static EnergyRecord recFromJson(JsonObjectConst o)
{
    EnergyRecord r;
    strlcpy(r.key, o["k"] | "", sizeof(r.key));
    r.hoy = o["h"] | 0.0f;
    r.se = o["s"] | 0.0f;
    r.exp = o["e"] | -1.0f;
    r.imp = o["i"] | -1.0f;
    r.hoyPeak = o["hp"] | 0.0f;
    r.sePeak = o["sp"] | 0.0f;
    return r;
}

bool EnergyHistoryClass::save()
{
    JsonDocument doc;
    doc["v"] = 1;
    auto cur = doc["cur"].to<JsonObject>();
    cur["date"] = _date;
    auto putC = [](JsonObject o, const Counters& c) {
        o["h"] = serialized(String(c.hoy, 3));
        o["s"] = serialized(String(c.se, 3));
        o["e"] = serialized(String(c.exp, 3));
        o["i"] = serialized(String(c.imp, 3));
    };
    putC(cur["start"].to<JsonObject>(), _start);
    putC(cur["last"].to<JsonObject>(), _last);
    cur["hp"] = (int)_hoyPeak;
    cur["sp"] = (int)_sePeak;
    cur["fp"] = _firstProd;
    cur["lp"] = _lastProd;

    for (auto& r : _days) recToJson(doc["days"].add<JsonObject>(), r, true);
    for (auto& r : _months) recToJson(doc["months"].add<JsonObject>(), r, false);
    for (auto& r : _years) recToJson(doc["years"].add<JsonObject>(), r, false);

    // write to temp file first, then rename -> no corrupted history on power loss
    File f = LittleFS.open(HISTORY_TMP, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", HISTORY_TMP);
        return false;
    }
    const size_t n = serializeJson(doc, f);
    f.close();
    if (n == 0) {
        LittleFS.remove(HISTORY_TMP);
        return false;
    }
    LittleFS.remove(HISTORY_FILE);
    const bool ok = LittleFS.rename(HISTORY_TMP, HISTORY_FILE);
    _lastSaveMs = millis();
    _dirty = false;
    ESP_LOGD(TAG, "History saved (%u bytes)", n);
    return ok;
}

bool EnergyHistoryClass::load()
{
    const char* file = LittleFS.exists(HISTORY_FILE) ? HISTORY_FILE : (LittleFS.exists(HISTORY_TMP) ? HISTORY_TMP : nullptr);
    if (!file) return false;
    File f = LittleFS.open(file, "r", false);
    if (!f) return false;
    JsonDocument doc;
    const auto err = deserializeJson(doc, f);
    f.close();
    if (err) {
        ESP_LOGW(TAG, "History file invalid: %s", err.c_str());
        return false;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    JsonObjectConst cur = doc["cur"];
    strlcpy(_date, cur["date"] | "", sizeof(_date));
    auto getC = [](JsonObjectConst o, Counters& c) {
        c.hoy = o["h"] | -1.0f;
        c.se = o["s"] | -1.0f;
        c.exp = o["e"] | -1.0f;
        c.imp = o["i"] | -1.0f;
    };
    getC(cur["start"], _start);
    getC(cur["last"], _last);
    _hoyPeak = cur["hp"] | 0.0f;
    _sePeak = cur["sp"] | 0.0f;
    strlcpy(_firstProd, cur["fp"] | "", sizeof(_firstProd));
    strlcpy(_lastProd, cur["lp"] | "", sizeof(_lastProd));
    for (JsonObjectConst o : doc["days"].as<JsonArrayConst>()) _days.push_back(recFromJson(o));
    for (JsonObjectConst o : doc["months"].as<JsonArrayConst>()) _months.push_back(recFromJson(o));
    for (JsonObjectConst o : doc["years"].as<JsonArrayConst>()) _years.push_back(recFromJson(o));
    return true;
}

void EnergyHistoryClass::reset()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _days.clear();
    _months.clear();
    _years.clear();
    _start = _last;
    _hoyPeak = _sePeak = 0;
    _firstProd[0] = _lastProd[0] = 0;
    save();
}

// ------------------------------------------------------------------ API
void EnergyHistoryClass::toJson(JsonVariant root)
{
    struct tm t;
    const bool timeValid = getLocalTime(&t, 5) && t.tm_year >= (2024 - 1900);
    char buf[20];
    if (timeValid) {
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &t);
        root["now"] = buf;
    }
    root["time_valid"] = timeValid;

    struct tm s;
    if (SunPosition.sunriseTime(&s)) {
        strftime(buf, sizeof(buf), "%H:%M", &s);
        root["sunrise"] = buf;
    }
    if (SunPosition.sunsetTime(&s)) {
        strftime(buf, sizeof(buf), "%H:%M", &s);
        root["sunset"] = buf;
    }
    root["is_day"] = SunPosition.isDayPeriod();

    // live
    Counters c;
    float hoyW, seW;
    readLiveCounters(c, hoyW, seW);
    const SolarEdgeData d = SolarEdge.getData();
    auto live = root["live"].to<JsonObject>();
    live["hoy_w"] = hoyW;
    live["se_w"] = seW;
    live["total_w"] = hoyW + seW;
    live["hoy_reachable"] = Datastore.getIsAtLeastOneReachable();
    live["hoy_producing"] = Datastore.getIsAtLeastOneProducing();
    live["se_valid"] = d.valid;
    live["se_status"] = d.status;
    live["se_status_text"] = SolarEdgeClass::statusToString(d.status);
    if (d.meterValid) live["grid_w"] = d.meterPower;
    HoymilesChannels.toJson(root["mppt"].to<JsonArray>());

    std::lock_guard<std::mutex> lock(_mutex);
    const EnergyRecord today = todayRecord();
    auto td = root["today"].to<JsonObject>();
    recToJson(td, today, true);
    td["first"] = _firstProd;
    td["last"] = _lastProd;

    for (auto& r : _days) recToJson(root["days"].add<JsonObject>(), r, true);

    // months/years including today's running values
    auto withToday = [&](const std::deque<EnergyRecord>& src, const char* key, size_t max, const char* name) {
        std::deque<EnergyRecord> tmp = src;
        if (_date[0]) addTo(tmp, key, today, max);
        for (auto& r : tmp) recToJson(root[name].add<JsonObject>(), r, false);
    };
    char month[8], year[5];
    strlcpy(month, _date, sizeof(month));
    strlcpy(year, _date, sizeof(year));
    withToday(_months, month, HISTORY_MAX_MONTHS, "months");
    withToday(_years, year, HISTORY_MAX_YEARS, "years");

    auto lt = root["lifetime"].to<JsonObject>();
    if (_last.hoy >= 0) lt["h"] = _last.hoy;
    if (_last.se >= 0) lt["s"] = _last.se;
    if (_last.exp >= 0) lt["e"] = _last.exp;
    if (_last.imp >= 0) lt["i"] = _last.imp;

    root["save_interval_min"] = HISTORY_SAVE_INTERVAL_MIN;
}

void EnergyHistoryClass::mqttPublish()
{
    _mqttTask.setInterval(Configuration.get().Mqtt.PublishInterval * TASK_SECOND);
    if (!MqttSettings.getConnected() || _date[0] == 0) return;

    EnergyRecord t;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        t = todayRecord();
    }
    MqttSettings.publish("fusion/day/hoymiles", String(t.hoy, 3));
    MqttSettings.publish("fusion/day/solaredge", String(t.se, 3));
    MqttSettings.publish("fusion/day/total", String(t.hoy + t.se, 3));
    if (t.exp >= 0) MqttSettings.publish("fusion/day/exported", String(t.exp, 3));
    if (t.imp >= 0) MqttSettings.publish("fusion/day/imported", String(t.imp, 3));
}

EnergyRecord EnergyHistoryClass::getToday()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return todayRecord();
}

Counters EnergyHistoryClass::getLastCounters()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _last;
}

bool EnergyHistoryClass::getDay(const char* key, EnergyRecord& out)
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto it = _days.rbegin(); it != _days.rend(); ++it) {
        if (strcmp(it->key, key) == 0) {
            out = *it;
            return true;
        }
    }
    return false;
}

bool EnergyHistoryClass::getLastClosedDay(EnergyRecord& out)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_days.empty()) return false;
    out = _days.back();
    return true;
}

EnergySummary EnergyHistoryClass::getSummary()
{
    EnergySummary s;
    std::lock_guard<std::mutex> lock(_mutex);
    s.lifetime = _last;
    if (!_date[0]) return s;
    s.valid = true;
    s.today = todayRecord();
    if (!_days.empty()) {
        s.yesterday = _days.back();
        s.hasYesterday = true;
    }
    char month[8], year[5];
    strlcpy(month, _date, sizeof(month));
    strlcpy(year, _date, sizeof(year));
    auto pick = [&](const std::deque<EnergyRecord>& src, const char* key, EnergyRecord& out) {
        if (!src.empty() && strcmp(src.back().key, key) == 0) out = src.back();
        else strlcpy(out.key, key, sizeof(out.key));
        out.hoy += s.today.hoy;
        out.se += s.today.se;
        if (s.today.exp >= 0) out.exp = (out.exp < 0 ? 0 : out.exp) + s.today.exp;
        if (s.today.imp >= 0) out.imp = (out.imp < 0 ? 0 : out.imp) + s.today.imp;
    };
    pick(_months, month, s.month);
    pick(_years, year, s.year);
    return s;
}
