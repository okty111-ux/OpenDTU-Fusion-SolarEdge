// SPDX-License-Identifier: GPL-2.0-or-later
#include "SdLogger.h"
#include "Datastore.h"
#include "EnergyHistory.h"
#include "HoymilesChannels.h"
#include "SolarEdge.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <cmath>
#include <time.h>

#undef TAG
static const char* TAG = "sdlog";

#define SD_CFG_FILE "/sdlog.json"

#if SD_CSV_DECIMAL_COMMA
static const char SEP = ';';
#else
static const char SEP = ',';
#endif

SdLoggerClass SdLogger;

SdLoggerClass::SdLoggerClass()
    : _loopTask(5 * TASK_SECOND, TASK_FOREVER, std::bind(&SdLoggerClass::loop, this))
{
}

void SdLoggerClass::init(Scheduler& scheduler)
{
    loadConfig();

    // SDMMC host (1-bit) via GPIO matrix: does not use any SPI host,
    // so NRF24, CMT2300A and W5500 (PoE) all keep working.
    if (SD_PIN_D3 >= 0) { // otherwise DAT3/CS is pulled up by a resistor
        pinMode(SD_PIN_D3, OUTPUT);
        digitalWrite(SD_PIN_D3, HIGH); // DAT3/CS high = SD mode, not SPI mode
    }
    pinMode(SD_PIN_CMD, INPUT_PULLUP);
    pinMode(SD_PIN_D0, INPUT_PULLUP);
    if (!SD_MMC.setPins(SD_PIN_CLK, SD_PIN_CMD, SD_PIN_D0)) {
        _reason = "SDMMC pin setup failed";
        ESP_LOGW(TAG, "%s", _reason.c_str());
        return;
    }
    _busOk = true;

    mount();
    scheduler.addTask(_loopTask);
    _loopTask.enable();
}

bool SdLoggerClass::mount()
{
    std::lock_guard<std::mutex> lock(_fsMutex);
    _lastMountTry = millis();
    if (!SD_MMC.begin("/sd", true, false, SD_MMC_FREQ_KHZ, 5)) {
        SD_MMC.end();
        _mounted = false;
        _reason = "no card / card not readable (FAT32/exFAT)";
        return false;
    }
    if (SD_MMC.cardType() == CARD_NONE) {
        SD_MMC.end();
        _mounted = false;
        _reason = "no card";
        return false;
    }
    if (!SD_MMC.exists(SD_LOG_ROOT)) {
        SD_MMC.mkdir(SD_LOG_ROOT);
    }
    _mounted = true;
    _reason = "";
    ESP_LOGI(TAG, "SD mounted, %llu MB", SD_MMC.cardSize() / (1024ULL * 1024ULL));
    return true;
}

void SdLoggerClass::unmount(const char* reason)
{
    SD_MMC.end();
    _mounted = false;
    _reason = reason;
    ESP_LOGW(TAG, "SD unmounted: %s", reason);
}

// ------------------------------------------------------------- config
void SdLoggerClass::loadConfig()
{
    File f = LittleFS.open(SD_CFG_FILE, "r", false);
    if (!f) return;
    JsonDocument doc;
    if (!deserializeJson(doc, f)) {
        _enabled = doc["enabled"] | true;
        _intervalMin = doc["interval"] | SD_DEFAULT_INTERVAL_MIN;
        strlcpy(_lastSummary, doc["last_summary"] | "", sizeof(_lastSummary));
    }
    f.close();
}

bool SdLoggerClass::saveConfig()
{
    JsonDocument doc;
    doc["enabled"] = _enabled;
    doc["interval"] = _intervalMin;
    doc["last_summary"] = _lastSummary;
    File f = LittleFS.open(SD_CFG_FILE, "w");
    if (!f) return false;
    const bool ok = serializeJson(doc, f) > 0;
    f.close();
    return ok;
}

bool SdLoggerClass::setConfig(bool enabled, uint16_t intervalMin)
{
    if (intervalMin < 1) intervalMin = 1;
    if (intervalMin > 60) intervalMin = 60;
    _enabled = enabled;
    _intervalMin = intervalMin;
    _lastSlot = -1;
    return saveConfig();
}

SdStatus SdLoggerClass::getStatus()
{
    SdStatus s;
    s.compiled = _busOk;
    s.mounted = _mounted;
    s.reason = _reason;
    s.enabled = _enabled;
    s.intervalMin = _intervalMin;
    s.lastWrite = _lastWrite;
    s.writeErrors = _writeErrors;
    if (_mounted) {
        std::lock_guard<std::mutex> lock(_fsMutex);
        switch (SD_MMC.cardType()) {
        case CARD_MMC: s.cardType = "MMC"; break;
        case CARD_SD: s.cardType = "SD"; break;
        case CARD_SDHC: s.cardType = "SDHC/SDXC"; break;
        default: s.cardType = "?";
        }
        s.totalBytes = SD_MMC.totalBytes();
        s.usedBytes = SD_MMC.usedBytes();
    }
    return s;
}

bool SdLoggerClass::isSafePath(const String& path)
{
    return path.startsWith(SD_LOG_ROOT) && path.indexOf("..") < 0 && path.indexOf("//") < 0;
}

// ---------------------------------------------------------------- write
String SdLoggerClass::num(float v, uint8_t digits)
{
    if (std::isnan(v) || v < -1e8) return "";
    String s(v, (unsigned int)digits);
#if SD_CSV_DECIMAL_COMMA
    s.replace('.', ',');
#endif
    return s;
}

bool SdLoggerClass::appendLine(const String& path, const String& header, const String& line)
{
    std::lock_guard<std::mutex> lock(_fsMutex);
    const int slash = path.lastIndexOf('/');
    if (slash > 0) {
        const String dir = path.substring(0, slash);
        if (!SD_MMC.exists(dir)) SD_MMC.mkdir(dir);
    }
    const bool isNew = !SD_MMC.exists(path);
    File f = SD_MMC.open(path, FILE_APPEND);
    if (!f) return false;
    bool ok = true;
    if (isNew) {
        f.write(0xEF); f.write(0xBB); f.write(0xBF); // UTF-8 BOM -> Excel shows Polish chars
        ok &= f.print(header) > 0;
        ok &= f.print("\r\n") > 0;
    }
    ok &= f.print(line) > 0;
    ok &= f.print("\r\n") > 0;
    f.close();
    return ok;
}

void SdLoggerClass::writeSample(const struct tm& t)
{
    char date[11], ts[20], path[40];
    strftime(date, sizeof(date), "%Y-%m-%d", &t);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", &t);
    snprintf(path, sizeof(path), SD_LOG_ROOT "/%.4s/%s.csv", date, date);

    const SolarEdgeData d = SolarEdge.getData();
    const EnergyRecord today = EnergyHistory.getToday();
    const Counters c = EnergyHistory.getLastCounters();
    const float hoyW = Datastore.getTotalAcPowerEnabled();
    const float hoyDcW = Datastore.getTotalDcPowerEnabled();
    const float seW = d.valid ? d.acPower : 0;

    String h;
    const char* cols[] = { "czas", "hoymiles_W", "hoymiles_DC_W", "solaredge_W", "razem_W", "siec_W",
        "hoymiles_dzis_kWh", "solaredge_dzis_kWh", "razem_dzis_kWh", "eksport_dzis_kWh", "import_dzis_kWh",
        "hoymiles_licznik_kWh", "solaredge_licznik_kWh", "solaredge_status", "solaredge_temp_C",
        "solaredge_DC_V", "napiecie_L1_V", "napiecie_L2_V", "napiecie_L3_V", "czestotliwosc_Hz" };
    for (size_t i = 0; i < sizeof(cols) / sizeof(cols[0]); i++) {
        if (i) h += SEP;
        h += cols[i];
    }

    const auto mppt = HoymilesChannels.read();
    for (auto& m : mppt) {
        char pre[24];
        snprintf(pre, sizeof(pre), "%cinw%u_mppt%u_", SEP, m.invIndex, m.channel);
        h += pre; h += "U_V";
        h += pre; h += "I_A";
        h += pre; h += "P_W";
        h += pre; h += "dzis_Wh";
        for (uint8_t k = 0; k < m.panels.size(); k++) {
            char pp[40];
            snprintf(pp, sizeof(pp), "%cinw%u_mppt%u_panel%u_P_W_szac", SEP, m.invIndex, m.channel, k + 1);
            h += pp;
        }
    }

    String l;
    l.reserve(200 + mppt.size() * 30);
    l += ts; l += SEP;
    l += num(hoyW, 1); l += SEP;
    l += num(hoyDcW, 1); l += SEP;
    l += d.valid ? num(seW, 1) : ""; l += SEP;
    l += num(hoyW + seW, 1); l += SEP;
    l += d.meterValid ? num(d.meterPower, 1) : ""; l += SEP;
    l += num(today.hoy, 3); l += SEP;
    l += num(today.se, 3); l += SEP;
    l += num(today.hoy + today.se, 3); l += SEP;
    l += today.exp >= 0 ? num(today.exp, 3) : ""; l += SEP;
    l += today.imp >= 0 ? num(today.imp, 3) : ""; l += SEP;
    l += c.hoy >= 0 ? num(c.hoy, 3) : ""; l += SEP;
    l += c.se >= 0 ? num(c.se, 3) : ""; l += SEP;
    l += d.valid ? SolarEdgeClass::statusToString(d.status) : "offline"; l += SEP;
    l += d.valid ? num(d.heatsinkTemp, 1) : ""; l += SEP;
    l += d.valid ? num(d.dcVoltage, 1) : ""; l += SEP;
    l += d.valid ? num(d.acVoltageAN, 1) : ""; l += SEP;
    l += d.valid ? num(d.acVoltageBN, 1) : ""; l += SEP;
    l += d.valid ? num(d.acVoltageCN, 1) : ""; l += SEP;
    l += d.valid ? num(d.acFrequency, 2) : "";
    for (auto& m : mppt) {
        l += SEP; l += m.reachable ? num(m.u, 1) : "";
        l += SEP; l += m.reachable ? num(m.i, 2) : "";
        l += SEP; l += m.reachable ? num(m.p, 1) : "";
        l += SEP; l += num(m.yieldDay, 0);
        for (auto& e : m.panels) { l += SEP; l += m.reachable ? num(e.p, 1) : ""; }
    }

    if (appendLine(path, h, l)) {
        _lastWrite = ts;
    } else {
        _writeErrors++;
        unmount("write failed (card removed?)");
    }
}

void SdLoggerClass::writeDaySummary(const EnergyRecord& r)
{
    String h = String("dzien") + SEP + "hoymiles_kWh" + SEP + "solaredge_kWh" + SEP + "razem_kWh" + SEP
        + "eksport_kWh" + SEP + "import_kWh" + SEP + "hoymiles_max_W" + SEP + "solaredge_max_W";
    String l = String(r.key) + SEP + num(r.hoy, 3) + SEP + num(r.se, 3) + SEP + num(r.hoy + r.se, 3) + SEP
        + (r.exp >= 0 ? num(r.exp, 3) : "") + SEP + (r.imp >= 0 ? num(r.imp, 3) : "") + SEP
        + num(r.hoyPeak, 0) + SEP + num(r.sePeak, 0);
    if (!appendLine(SD_LOG_ROOT "/dni.csv", h, l)) {
        _writeErrors++;
        unmount("write failed (card removed?)");
        return;
    }
    strlcpy(_lastSummary, r.key, sizeof(_lastSummary));
    saveConfig();
}

void SdLoggerClass::loop()
{
    if (!_busOk || !_enabled) return;

    if (!_mounted) {
        if (millis() - _lastMountTry > 30000) mount(); // hot-plug: retry every 30 s
        return;
    }

    struct tm t;
    if (!getLocalTime(&t, 5) || t.tm_year < (2024 - 1900)) return;

    // daily summary: every day closed by EnergyHistory (also after reboot / card swap)
    EnergyRecord closed;
    if (EnergyHistory.getLastClosedDay(closed) && strcmp(closed.key, _lastSummary) > 0) {
        writeDaySummary(closed);
        if (!_mounted) return;
    }

    const int slot = (t.tm_hour * 60 + t.tm_min) / _intervalMin;
    if (slot != _lastSlot) {
        _lastSlot = slot;
        writeSample(t);
    }
}
