// SPDX-License-Identifier: GPL-2.0-or-later
#include "FusionDisplay.h"
#include "PowerLog.h"
#include "Datastore.h"
#include "EnergyHistory.h"
#include "FusionDisplayPages.h"
#include "HoymilesChannels.h"
#include "SdLogger.h"
#include "SolarEdge.h"
#include <Hoymiles.h>
#include <LittleFS.h>
#include <NetworkSettings.h>
#include <WiFi.h>
#include <soc/gpio_struct.h>
#include <esp_timer.h>
#include <time.h>

#undef TAG
static const char* TAG = "fdisplay";
static const char* CFG_FILE = "/display.json";
static const uint32_t LONG_PRESS_MS = 1000;
static const uint32_t DEBOUNCE_MS = 30;

FusionDisplayClass FusionDisplay;
static u8g2_t s_u8g2;

// ---------------------------------------------------------------- fast SW SPI
// SPI mode 0, MSB first. Pins 0..31 only (GPIO9..13 on the display header).
static inline void IRAM_ATTR pinHi(uint8_t p) { GPIO.out_w1ts = (1UL << p); }
static inline void IRAM_ATTR pinLo(uint8_t p) { GPIO.out_w1tc = (1UL << p); }

static uint8_t fd_byte_cb(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr)
{
    switch (msg) {
    case U8X8_MSG_BYTE_SEND: {
        const uint8_t* data = static_cast<const uint8_t*>(arg_ptr);
        while (arg_int--) {
            uint8_t b = *data++;
            for (uint8_t i = 0; i < 8; i++) {
                if (b & 0x80) pinHi(FD_PIN_MOSI); else pinLo(FD_PIN_MOSI);
                b <<= 1;
                __asm__ __volatile__("nop; nop; nop; nop;");
                pinHi(FD_PIN_SCK); // SSD1322 latches on rising edge
                __asm__ __volatile__("nop; nop; nop; nop;");
                pinLo(FD_PIN_SCK);
            }
        }
        break;
    }
    case U8X8_MSG_BYTE_INIT:
        pinLo(FD_PIN_SCK);
        u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);
        break;
    case U8X8_MSG_BYTE_SET_DC:
        u8x8_gpio_SetDC(u8x8, arg_int);
        break;
    case U8X8_MSG_BYTE_START_TRANSFER:
        u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_enable_level);
        u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_DELAY_NANO, u8x8->display_info->post_chip_enable_wait_ns, NULL);
        break;
    case U8X8_MSG_BYTE_END_TRANSFER:
        u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_DELAY_NANO, u8x8->display_info->pre_chip_disable_wait_ns, NULL);
        u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);
        break;
    default:
        return 0;
    }
    return 1;
}

static uint8_t fd_gpio_cb(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr)
{
    switch (msg) {
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        for (uint8_t p : { FD_PIN_MOSI, FD_PIN_SCK, FD_PIN_CS, FD_PIN_DC, FD_PIN_RST }) {
            pinMode(p, OUTPUT);
            digitalWrite(p, p == FD_PIN_CS || p == FD_PIN_RST ? HIGH : LOW);
        }
        break;
    case U8X8_MSG_DELAY_NANO: delayMicroseconds(arg_int == 0 ? 0 : 1); break;
    case U8X8_MSG_DELAY_100NANO: delayMicroseconds(1); break;
    case U8X8_MSG_DELAY_10MICRO: delayMicroseconds(10 * arg_int); break;
    case U8X8_MSG_DELAY_MILLI: delay(arg_int); break;
    case U8X8_MSG_GPIO_CS: digitalWrite(FD_PIN_CS, arg_int); break;
    case U8X8_MSG_GPIO_DC: digitalWrite(FD_PIN_DC, arg_int); break;
    case U8X8_MSG_GPIO_RESET: digitalWrite(FD_PIN_RST, arg_int); break;
    default: return 0;
    }
    return 1;
}

// ---------------------------------------------------------------- class
FusionDisplayClass::FusionDisplayClass()
    : _loopTask(20 * TASK_MILLISECOND, TASK_FOREVER, std::bind(&FusionDisplayClass::loop, this))
{
}

void FusionDisplayClass::init(Scheduler& scheduler)
{
    loadConfig();
    pinMode(FD_PIN_BUTTON, INPUT_PULLUP);
    if (_cfg.enabled) begin();
    _lastActivity = _lastSwitch = millis();
    scheduler.addTask(_loopTask);
    _loopTask.enable();
}

void FusionDisplayClass::begin()
{
    if (_started) {
        u8g2_SetPowerSave(&s_u8g2, 0);
        return;
    }
    u8g2_Setup_ssd1322_nhd_256x64_f(&s_u8g2, _cfg.flip ? U8G2_R2 : U8G2_R0, fd_byte_cb, fd_gpio_cb);
    u8g2_InitDisplay(&s_u8g2);
    u8g2_SetPowerSave(&s_u8g2, 0);
    u8g2_SetContrast(&s_u8g2, _cfg.contrast);
    _u8g2 = &s_u8g2;
    _started = true;
    _sleeping = false;
    ESP_LOGI(TAG, "SSD1322 started");
}

FusionDisplayConfig FusionDisplayClass::getConfig()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _cfg;
}

bool FusionDisplayClass::setConfig(const FusionDisplayConfig& c)
{
    bool ok;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _cfg = c;
        _cfg.contrast = c.contrast;
        if (_cfg.pages == 0) _cfg.pages = 1;
        ok = saveConfig();
    }
    if (_cfg.enabled) {
        begin();
        u8g2_SetContrast(&s_u8g2, _cfg.contrast);
        u8g2_SetDisplayRotation(&s_u8g2, _cfg.flip ? U8G2_R2 : U8G2_R0);
        _lastActivity = millis();
        _sleeping = false;
        _lastDraw = 0;
    } else if (_started) {
        u8g2_ClearDisplay(&s_u8g2);
        u8g2_SetPowerSave(&s_u8g2, 1);
    }
    return ok;
}

void FusionDisplayClass::toJson(JsonObject root)
{
    const FusionDisplayConfig c = getConfig();
    root["enabled"] = c.enabled;
    root["auto_sec"] = c.autoSec;
    root["sleep_min"] = c.sleepMin;
    root["contrast"] = c.contrast;
    root["flip"] = c.flip;
    auto pages = root["pages"].to<JsonArray>();
    for (uint8_t p = 0; p < FD_PAGE_COUNT; p++) {
        auto o = pages.add<JsonObject>();
        o["id"] = p;
        o["name"] = fdPageTitle(p);
        o["on"] = (c.pages >> p) & 1;
    }
    root["page"] = _page;
    root["sleeping"] = _sleeping;
    root["auto_on"] = _autoOn;
}

void FusionDisplayClass::nextPage() { _webNext = true; }

void FusionDisplayClass::loadConfig()
{
    File f = LittleFS.open(CFG_FILE, "r");
    if (!f) return;
    JsonDocument doc;
    if (!deserializeJson(doc, f)) {
        _cfg.enabled = doc["enabled"] | false;
        _cfg.autoSec = doc["auto_sec"] | 10;
        _cfg.sleepMin = doc["sleep_min"] | 10;
        _cfg.contrast = doc["contrast"] | 160;
        _cfg.flip = doc["flip"] | false;
        _cfg.pages = doc["pages"] | 0xFF;
        if ((doc["pv"] | 1) < 2) { // v1 had no chart page (bit 1): shift bits up, enable chart
            const uint8_t m = _cfg.pages;
            _cfg.pages = (m & 1) | 0x02 | ((m & 0x7E) << 1);
        }
    }
    f.close();
}

bool FusionDisplayClass::saveConfig()
{
    JsonDocument doc;
    doc["enabled"] = _cfg.enabled;
    doc["auto_sec"] = _cfg.autoSec;
    doc["sleep_min"] = _cfg.sleepMin;
    doc["contrast"] = _cfg.contrast;
    doc["flip"] = _cfg.flip;
    doc["pages"] = _cfg.pages;
    doc["pv"] = 2;
    File f = LittleFS.open(CFG_FILE, "w");
    if (!f) return false;
    const bool ok = serializeJson(doc, f) > 0;
    f.close();
    return ok;
}

bool FusionDisplayClass::producing()
{
    const SolarEdgeData se = SolarEdge.getData();
    return Datastore.getIsAtLeastOneProducing() || (se.valid && se.acPower > 1.0f);
}

uint8_t FusionDisplayClass::nextAvailable(const FdData& d, uint8_t from)
{
    for (uint8_t k = 1; k <= FD_PAGE_COUNT; k++) {
        const uint8_t p = (from + k) % FD_PAGE_COUNT;
        if (((_cfg.pages >> p) & 1) && fdPageAvailable(d, p)) return p;
    }
    return FD_PAGE_POWER;
}

void FusionDisplayClass::loop()
{
    if (!_cfg.enabled || !_started) return;
    const uint32_t now = millis();

    // ---- button (active low, debounced)
    bool shortPress = false, longPress = false;
    const bool down = digitalRead(FD_PIN_BUTTON) == LOW;
    if (down != _btnDown && now - _btnChange > DEBOUNCE_MS) {
        _btnDown = down;
        _btnChange = now;
        if (down) {
            _btnSince = now;
            _btnLongDone = false;
        } else if (!_btnLongDone) {
            shortPress = true;
        }
    }
    if (_btnDown && !_btnLongDone && now - _btnSince >= LONG_PRESS_MS) {
        _btnLongDone = true;
        longPress = true;
    }
    if (_webNext) {
        _webNext = false;
        shortPress = true;
    }

    bool redraw = now - _lastDraw >= 1000;
    if (shortPress || longPress) {
        _lastActivity = now;
        if (_sleeping) {
            _sleeping = false;
            u8g2_SetPowerSave(&s_u8g2, 0);
            shortPress = false; // first press only wakes the screen
        }
        redraw = true;
    }

    // ---- night sleep
    if (!_sleeping && _cfg.sleepMin > 0 && !producing() && now - _lastActivity > _cfg.sleepMin * 60000UL) {
        _sleeping = true;
        u8g2_SetPowerSave(&s_u8g2, 1);
    }
    if (_sleeping) {
        if (producing()) { // morning: production started
            _sleeping = false;
            _lastActivity = now;
            u8g2_SetPowerSave(&s_u8g2, 0);
        } else {
            return;
        }
    }
    if (!redraw) return;

    static FdData d;
    collect(d);
    if (!(((_cfg.pages >> _page) & 1) && fdPageAvailable(d, _page))) _page = nextAvailable(d, _page);

    if (shortPress) {
        _page = nextAvailable(d, _page);
        _lastSwitch = now;
    }
    if (longPress) {
        _autoOn = !_autoOn;
        _lastSwitch = now;
        _ov1 = "Automatyczne przełączanie";
        _ov2 = _autoOn ? (String("włączone · co ") + _cfg.autoSec + " s") : String("wyłączone · krótko = następna");
        _overlayUntil = now + 2000;
    }
    if (_autoOn && _cfg.autoSec > 0 && now - _lastSwitch >= _cfg.autoSec * 1000UL) {
        _page = nextAvailable(d, _page);
        _lastSwitch = now;
    }

    uint8_t cnt = 0, no = 0;
    for (uint8_t p = 0; p < FD_PAGE_COUNT; p++) {
        if (((_cfg.pages >> p) & 1) && fdPageAvailable(d, p)) {
            cnt++;
            if (p <= _page) no++;
        }
    }

    u8g2_ClearBuffer(&s_u8g2);
    fdRenderPage(&s_u8g2, d, _page, no ? no : 1, cnt ? cnt : 1, _autoOn && _cfg.autoSec > 0);
    if (now < _overlayUntil) fdRenderOverlay(&s_u8g2, _ov1.c_str(), _ov2.length() ? _ov2.c_str() : nullptr);
    u8g2_SendBuffer(&s_u8g2);
    _lastDraw = now;
}

static void copyStr(char* dst, size_t n, const String& s) { strlcpy(dst, s.c_str(), n); }

void FusionDisplayClass::collect(FdData& d)
{
    memset(&d, 0, sizeof(d));
    struct tm t;
    d.timeValid = getLocalTime(&t, 0) && t.tm_year >= (2024 - 1900);
    if (d.timeValid) {
        strftime(d.clock, sizeof(d.clock), "%H:%M", &t);
        strftime(d.date, sizeof(d.date), "%d.%m.%Y", &t);
    }

    PowerLog.getToday(d.curveHoy, d.curveSe, FD_CURVE_SLOTS, d.curveSlot);

    d.hoyW = Datastore.getTotalAcPowerEnabled();
    d.hoyReachable = Datastore.getIsAtLeastOneReachable();
    const SolarEdgeData se = SolarEdge.getData();
    const SolarEdgeConfig sc = SolarEdge.getConfig();
    d.seConfigured = sc.enabled && (sc.transport == SE_TRANSPORT_RTU || sc.host.length() > 0);
    d.seValid = se.valid;
    d.seW = se.valid ? se.acPower : 0;
    d.meterValid = se.valid && se.meterValid;
    d.gridW = se.meterPower;
    copyStr(d.seStatus, sizeof(d.seStatus), SolarEdgeClass::statusToString(se.status));
    d.seTemp = se.heatsinkTemp;
    d.seTotalKWh = se.energyTotal / 1000.0f;
    d.seDcV = se.dcVoltage;
    copyStr(d.seModel, sizeof(d.seModel), se.model);

    const EnergySummary s = EnergyHistory.getSummary();
    d.todayHoy = s.valid ? s.today.hoy : -1;
    d.todaySe = s.valid && d.seConfigured ? s.today.se : -1;
    d.todayExp = s.valid ? s.today.exp : -1;
    d.todayImp = s.valid ? s.today.imp : -1;
    d.hasYesterday = s.hasYesterday;
    d.yHoy = s.yesterday.hoy; d.ySe = s.yesterday.se;
    d.mHoy = s.valid ? s.month.hoy : -1; d.mSe = s.valid ? s.month.se : -1;
    d.yrHoy = s.valid ? s.year.hoy : -1; d.yrSe = s.valid ? s.year.se : -1;
    d.ltHoy = s.lifetime.hoy; d.ltSe = s.lifetime.se;

    const auto mppt = HoymilesChannels.read();
    uint8_t maxInv = 0;
    for (auto& m : mppt) {
        if (d.mpptCount >= FD_MAX_MPPT) break;
        FdMppt& o = d.mppt[d.mpptCount++];
        o.inv = m.invIndex; o.ch = m.channel; o.reachable = m.reachable;
        o.u = m.u; o.i = m.i; o.p = m.p; o.yd = m.yieldDay;
        if (m.invIndex > maxInv) maxInv = m.invIndex;
        o.panelCount = m.panels.size() > 2 ? 2 : m.panels.size();
        for (uint8_t k = 0; k < o.panelCount; k++) {
            copyStr(o.panel[k].name, sizeof(o.panel[k].name), m.panels[k].name);
            o.panel[k].wp = m.panels[k].wp;
            o.panel[k].p = m.panels[k].p;
        }
    }
    d.multiInverter = maxInv > 1;

    copyStr(d.ip, sizeof(d.ip), NetworkSettings.localIP().toString());
    if (NetworkSettings.NetworkMode() == network_mode::Ethernet) strlcpy(d.net, "Ethernet (PoE)", sizeof(d.net));
    else if (NetworkSettings.isConnected()) snprintf(d.net, sizeof(d.net), "Wi-Fi %d dBm", WiFi.RSSI());
    else strlcpy(d.net, "brak połączenia", sizeof(d.net));

    const SdStatus sd = SdLogger.getStatus();
    if (sd.mounted) {
        char a[12], b[12];
        snprintf(a, sizeof(a), "%.1f", sd.totalBytes / 1073741824.0);
        snprintf(b, sizeof(b), "%.1f", (sd.totalBytes - sd.usedBytes) / 1073741824.0);
        for (char* p = a; *p; p++) if (*p == '.') *p = ',';
        for (char* p = b; *p; p++) if (*p == '.') *p = ',';
        snprintf(d.sd, sizeof(d.sd), "%s GB · wolne %s GB", a, b);
    } else {
        strlcpy(d.sd, sd.enabled ? "brak karty" : "wyłączona", sizeof(d.sd));
    }

    d.invTotal = Hoymiles.getNumInverters();
    for (uint8_t i = 0; i < d.invTotal; i++) {
        auto inv = Hoymiles.getInverterByPos(i);
        if (inv && inv->isReachable()) d.invOnline++;
    }
    d.uptimeS = (uint32_t)(esp_timer_get_time() / 1000000ULL);
}
