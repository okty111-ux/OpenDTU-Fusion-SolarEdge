// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SolarEdge SunSpec Modbus-TCP reader for OpenDTU (Fusion)
 *
 * Register map (0-based Modbus addresses, SolarEdge "SunSpec Implementation
 * Technical Note"):  common block 40000, inverter model 40069, meter 1 at 40121/40188.
 */
#include "SolarEdge.h"
#include "Configuration.h"
#include "Datastore.h"
#include "MqttSettings.h"
#include "NetworkSettings.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <HardwareSerial.h>
#if __has_include(<NetworkClient.h>)
#include <NetworkClient.h> // Arduino-ESP32 3.x (WiFi + Ethernet)
#else
#include <WiFiClient.h> // Arduino-ESP32 2.x (lwIP sockets, also works over W5500)
using NetworkClient = WiFiClient;
#endif
#include <cmath>

#undef TAG
static const char* TAG = "solaredge";

#define SOLAREDGE_CFG_FILE "/solaredge.json"

SolarEdgeClass SolarEdge;

static NetworkClient seClient;
static HardwareSerial seSerial(1); // UART1, free on Fusion

SolarEdgeClass::SolarEdgeClass()
    : _mqttTask(TASK_SECOND * 10, TASK_FOREVER, std::bind(&SolarEdgeClass::mqttPublish, this))
{
}

void SolarEdgeClass::init(Scheduler& scheduler)
{
    loadConfig();

    scheduler.addTask(_mqttTask);
    _mqttTask.enable();

    // Modbus polling runs in its own FreeRTOS task (core 0) so that a slow
    // TCP connection can never block the Hoymiles radio loop.
    xTaskCreatePinnedToCore(taskEntry, "solaredge", 6144, this, 1, nullptr, 0);
    ESP_LOGI(TAG, "SolarEdge reader started (%s, host '%s:%u', unit %u)",
        _cfg.transport == SE_TRANSPORT_RTU ? "RS485" : "TCP", _cfg.host.c_str(), _cfg.port, _cfg.unitId);
}

// ---------------------------------------------------------------- config
void SolarEdgeClass::loadConfig()
{
    File f = LittleFS.open(SOLAREDGE_CFG_FILE, "r", false);
    if (!f) {
        ESP_LOGI(TAG, "No %s, using build defaults", SOLAREDGE_CFG_FILE);
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, f)) {
        ESP_LOGW(TAG, "Invalid %s", SOLAREDGE_CFG_FILE);
        f.close();
        return;
    }
    f.close();
    std::lock_guard<std::mutex> lock(_mutex);
    _cfg.enabled = doc["enabled"] | true;
    _cfg.transport = doc["transport"] | (uint8_t)SE_TRANSPORT_TCP;
    _cfg.baud = doc["baud"] | 115200UL;
    _cfg.parity = doc["parity"] | 0;
    _cfg.host = doc["host"] | SOLAREDGE_DEFAULT_HOST;
    _cfg.port = doc["port"] | SOLAREDGE_DEFAULT_PORT;
    _cfg.unitId = doc["unit_id"] | SOLAREDGE_DEFAULT_UNIT;
    _cfg.interval = doc["interval"] | SOLAREDGE_DEFAULT_INTERVAL;
    _cfg.readMeter = doc["read_meter"] | SOLAREDGE_DEFAULT_METER;
}

bool SolarEdgeClass::saveConfig()
{
    JsonDocument doc;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        doc["enabled"] = _cfg.enabled;
        doc["transport"] = _cfg.transport;
        doc["baud"] = _cfg.baud;
        doc["parity"] = _cfg.parity;
        doc["host"] = _cfg.host;
        doc["port"] = _cfg.port;
        doc["unit_id"] = _cfg.unitId;
        doc["interval"] = _cfg.interval;
        doc["read_meter"] = _cfg.readMeter;
    }
    File f = LittleFS.open(SOLAREDGE_CFG_FILE, "w");
    if (!f) {
        return false;
    }
    const bool ok = serializeJson(doc, f) > 0;
    f.close();
    return ok;
}

SolarEdgeConfig SolarEdgeClass::getConfig()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _cfg;
}

bool SolarEdgeClass::setConfig(const SolarEdgeConfig& cfg)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _cfg = cfg;
        if (_cfg.interval < 2) {
            _cfg.interval = 2;
        }
    }
    _reconnect = true;
    return saveConfig();
}

SolarEdgeData SolarEdgeClass::getData()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _data;
}

// ---------------------------------------------------------------- modbus
bool SolarEdgeClass::readRegisters(const SolarEdgeConfig& cfg, uint16_t addr, uint16_t count, uint16_t* out)
{
    if (cfg.transport == SE_TRANSPORT_RTU) {
        return readRegistersRtu(cfg, addr, count, out);
    }
    return readRegistersTcp(cfg, addr, count, out);
}

// ---------------------------------------------------------------- RS485 / Modbus RTU
static uint16_t modbusCrc(const uint8_t* p, size_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
    }
    return crc;
}

static inline void rs485Tx(bool tx)
{
    // half duplex: DE and /RE switched together
    digitalWrite(SOLAREDGE_RS485_DE, tx ? HIGH : LOW);
    digitalWrite(SOLAREDGE_RS485_RE_N, tx ? HIGH : LOW);
}

void SolarEdgeClass::rs485Begin(const SolarEdgeConfig& cfg)
{
    if (_rs485Open && _rs485Baud == cfg.baud && _rs485Parity == cfg.parity) {
        return;
    }
    rs485End();
    pinMode(SOLAREDGE_RS485_DE, OUTPUT);
    pinMode(SOLAREDGE_RS485_RE_N, OUTPUT);
    rs485Tx(false);
    const uint32_t mode = cfg.parity == 1 ? SERIAL_8E1 : cfg.parity == 2 ? SERIAL_8O1 : SERIAL_8N1;
    seSerial.setRxBufferSize(512);
    seSerial.begin(cfg.baud, mode, SOLAREDGE_RS485_RX, SOLAREDGE_RS485_TX);
    _rs485Open = true;
    _rs485Baud = cfg.baud;
    _rs485Parity = cfg.parity;
    ESP_LOGI(TAG, "RS485 open: %lu baud, parity %u", (unsigned long)cfg.baud, cfg.parity);
}

void SolarEdgeClass::rs485End()
{
    if (_rs485Open) {
        seSerial.end();
        _rs485Open = false;
    }
}

bool SolarEdgeClass::readRegistersRtu(const SolarEdgeConfig& cfg, uint16_t addr, uint16_t count, uint16_t* out)
{
    if (count == 0 || count > 125) {
        return false;
    }
    rs485Begin(cfg);
    _linkError = "";

    // Modbus RTU needs >= 3.5 char silent time between frames
    const uint32_t charUs = 11000000UL / (cfg.baud ? cfg.baud : 9600);
    const uint32_t gapUs = max<uint32_t>(charUs * 4, 1750);
    delayMicroseconds(gapUs);
    while (seSerial.available()) {
        seSerial.read();
    }

    uint8_t req[8] = {
        cfg.unitId, 0x03,
        (uint8_t)(addr >> 8), (uint8_t)addr,
        (uint8_t)(count >> 8), (uint8_t)count,
        0, 0
    };
    const uint16_t crc = modbusCrc(req, 6);
    req[6] = crc & 0xFF;
    req[7] = crc >> 8;

    rs485Tx(true);
    seSerial.write(req, sizeof(req));
    seSerial.flush(); // wait until the last stop bit has left the UART
    rs485Tx(false);

    // response: unit, fc, bytecount, data..., crcLo, crcHi  (exception: unit, fc|0x80, code, crc)
    uint8_t resp[5 + 250];
    const size_t want = 5 + count * 2;
    size_t got = 0;
    uint32_t start = millis();
    uint32_t last = start;
    while (got < want) {
        const int a = seSerial.available();
        if (a > 0) {
            got += seSerial.read(resp + got, want - got);
            last = millis();
            if (got >= 5 && (resp[1] & 0x80)) {
                break; // exception frame is 5 bytes
            }
            continue;
        }
        const uint32_t now = millis();
        if (got == 0 && now - start > 1000) {
            _linkError = "RS485: brak odpowiedzi (adres, predkosc, A/B?)";
            return false;
        }
        if (got > 0 && now - last > 50) {
            break; // inter-frame timeout
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    if (got >= 5 && resp[0] == cfg.unitId && (resp[1] & 0x80)) {
        if (modbusCrc(resp, 3) == (uint16_t)(resp[3] | (resp[4] << 8))) {
            ESP_LOGW(TAG, "Modbus exception %u at register %u", resp[2], addr);
        }
        return false;
    }
    if (got != want || resp[0] != cfg.unitId || resp[1] != 0x03 || resp[2] != count * 2) {
        _linkError = "RS485: niepelna lub obca ramka";
        return false;
    }
    if (modbusCrc(resp, want - 2) != (uint16_t)(resp[want - 2] | (resp[want - 1] << 8))) {
        _linkError = "RS485: blad CRC";
        return false;
    }
    for (uint16_t i = 0; i < count; i++) {
        out[i] = (resp[3 + i * 2] << 8) | resp[4 + i * 2];
    }
    return true;
}

bool SolarEdgeClass::readRegistersTcp(const SolarEdgeConfig& cfg, uint16_t addr, uint16_t count, uint16_t* out)
{
    if (count == 0 || count > 125) {
        return false;
    }

    if (!seClient.connected()) {
        seClient.stop();
        seClient.setTimeout(3000);
        if (!seClient.connect(cfg.host.c_str(), cfg.port, 3000)) {
            return false;
        }
        seClient.setNoDelay(true);
    }

    while (seClient.available()) { // drop stale bytes
        seClient.read();
    }

    const uint16_t tid = ++_transactionId;
    uint8_t req[12] = {
        (uint8_t)(tid >> 8), (uint8_t)tid, // transaction id
        0, 0, // protocol id
        0, 6, // length
        cfg.unitId,
        0x03, // read holding registers
        (uint8_t)(addr >> 8), (uint8_t)addr,
        (uint8_t)(count >> 8), (uint8_t)count
    };
    if (seClient.write(req, sizeof(req)) != sizeof(req)) {
        seClient.stop();
        return false;
    }

    auto readExact = [](uint8_t* buf, size_t len, uint32_t timeoutMs) -> bool {
        size_t got = 0;
        const uint32_t start = millis();
        while (got < len) {
            if (millis() - start > timeoutMs) {
                return false;
            }
            const int avail = seClient.available();
            if (avail > 0) {
                got += seClient.read(buf + got, len - got);
            } else {
                if (!seClient.connected()) {
                    return false;
                }
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        }
        return true;
    };

    uint8_t hdr[9];
    if (!readExact(hdr, sizeof(hdr), 3000)) {
        seClient.stop();
        return false;
    }
    const uint16_t rtid = (hdr[0] << 8) | hdr[1];
    if (rtid != tid) {
        seClient.stop();
        return false;
    }
    if (hdr[7] & 0x80) { // modbus exception, hdr[8] = exception code
        ESP_LOGW(TAG, "Modbus exception %u at register %u", hdr[8], addr);
        return false;
    }
    const uint8_t byteCount = hdr[8];
    if (byteCount != count * 2) {
        seClient.stop();
        return false;
    }
    uint8_t data[250];
    if (!readExact(data, byteCount, 3000)) {
        seClient.stop();
        return false;
    }
    for (uint16_t i = 0; i < count; i++) {
        out[i] = (data[i * 2] << 8) | data[i * 2 + 1];
    }
    return true;
}

static inline float scaled(uint16_t raw, uint16_t sf, bool isSigned = true)
{
    const int16_t s = static_cast<int16_t>(sf);
    if (s == -32768) { // "not implemented"
        return NAN;
    }
    const float v = isSigned ? static_cast<int16_t>(raw) : raw;
    return v * powf(10.0f, s);
}

static String regsToString(const uint16_t* r, size_t n)
{
    String s;
    for (size_t i = 0; i < n; i++) {
        const char a = r[i] >> 8, b = r[i] & 0xFF;
        if (a) s += a;
        if (b) s += b;
    }
    s.trim();
    return s;
}

bool SolarEdgeClass::pollOnce(SolarEdgeData& d, const SolarEdgeConfig& cfg)
{
    uint16_t r[125];

    // Common block (once): 40000.. SunS id, manufacturer, model, version, serial
    if (d.serial.isEmpty()) {
        if (!readRegisters(cfg, 40000, 69, r)) {
            d.lastError = "common block read failed";
            return false;
        }
        if (r[0] != 0x5375 || r[1] != 0x6e53) { // "SunS"
            d.lastError = "no SunSpec signature";
            return false;
        }
        d.manufacturer = regsToString(&r[4], 16);
        d.model = regsToString(&r[20], 16);
        d.version = regsToString(&r[44], 8);
        d.serial = regsToString(&r[52], 16);
    }

    // Inverter model: 40069 .. 40108 (40 registers)
    if (!readRegisters(cfg, 40069, 40, r)) {
        d.lastError = "inverter block read failed";
        return false;
    }
    auto R = [&](uint16_t a) { return r[a - 40069]; };
    d.sunspecDid = R(40069);
    const uint16_t sfI = R(40075);
    d.acCurrent = scaled(R(40071), sfI, false);
    d.acCurrentA = scaled(R(40072), sfI, false);
    d.acCurrentB = scaled(R(40073), sfI, false);
    d.acCurrentC = scaled(R(40074), sfI, false);
    const uint16_t sfV = R(40082);
    d.acVoltageAN = scaled(R(40079), sfV, false);
    d.acVoltageBN = scaled(R(40080), sfV, false);
    d.acVoltageCN = scaled(R(40081), sfV, false);
    d.acPower = scaled(R(40083), R(40084));
    d.acFrequency = scaled(R(40085), R(40086), false);
    d.acVA = scaled(R(40087), R(40088));
    d.acVAR = scaled(R(40089), R(40090));
    d.acPF = scaled(R(40091), R(40092));
    const uint32_t wh = (static_cast<uint32_t>(R(40093)) << 16) | R(40094);
    d.energyTotal = wh * powf(10.0f, static_cast<int16_t>(R(40095)));
    d.dcCurrent = scaled(R(40096), R(40097), false);
    d.dcVoltage = scaled(R(40098), R(40099), false);
    d.dcPower = scaled(R(40100), R(40101));
    d.heatsinkTemp = scaled(R(40103), R(40106));
    d.status = R(40107);
    d.vendorStatus = R(40108);

    // Meter 1: model 201-204 at 40188 .. 40242 (55 registers)
    d.meterValid = false;
    if (cfg.readMeter && readRegisters(cfg, 40188, 55, r)) {
        auto M = [&](uint16_t a) { return r[a - 40188]; };
        const uint16_t did = M(40188);
        if (did >= 201 && did <= 204) {
            const uint16_t sfW = M(40210);
            d.meterPower = scaled(M(40206), sfW);
            d.meterPowerA = scaled(M(40207), sfW);
            d.meterPowerB = scaled(M(40208), sfW);
            d.meterPowerC = scaled(M(40209), sfW);
            const float sfE = powf(10.0f, static_cast<int16_t>(M(40242)));
            d.meterExported = ((static_cast<uint32_t>(M(40226)) << 16) | M(40227)) * sfE;
            d.meterImported = ((static_cast<uint32_t>(M(40234)) << 16) | M(40235)) * sfE;
            d.meterValid = true;
        }
    }

    d.lastError = "";
    return true;
}

void SolarEdgeClass::taskEntry(void* arg)
{
    static_cast<SolarEdgeClass*>(arg)->pollLoop();
}

void SolarEdgeClass::pollLoop()
{
    for (;;) {
        const SolarEdgeConfig cfg = getConfig();

        if (_reconnect) {
            _reconnect = false;
            seClient.stop();
            rs485End();
            std::lock_guard<std::mutex> lock(_mutex);
            _data = SolarEdgeData();
        }

        const bool linkOk = cfg.transport == SE_TRANSPORT_RTU
            ? true
            : (!cfg.host.isEmpty() && NetworkSettings.isConnected());
        if (cfg.enabled && linkOk) {
            SolarEdgeData d = getData();
            const bool ok = pollOnce(d, cfg);
            if (ok) {
                d.valid = true;
                d.lastUpdateMs = millis();
            } else {
                d.errors++;
                // mark data stale after 3 intervals without success
                if (millis() - d.lastUpdateMs > cfg.interval * 3000UL) {
                    d.valid = false;
                }
                if (cfg.transport == SE_TRANSPORT_RTU && !_linkError.isEmpty()) {
                    d.lastError += " - " + _linkError;
                }
                ESP_LOGW(TAG, "Poll failed: %s", d.lastError.c_str());
            }
            std::lock_guard<std::mutex> lock(_mutex);
            _data = d;
        }

        vTaskDelay(pdMS_TO_TICKS(cfg.interval * 1000UL));
    }
}

// ---------------------------------------------------------------- mqtt
void SolarEdgeClass::mqttPublish()
{
    _mqttTask.setInterval(Configuration.get().Mqtt.PublishInterval * TASK_SECOND);

    if (!MqttSettings.getConnected()) {
        return;
    }
    const SolarEdgeData d = getData();

    MqttSettings.publish("solaredge/is_valid", String(d.valid));
    if (d.valid) {
        MqttSettings.publish("solaredge/serial", d.serial);
        MqttSettings.publish("solaredge/model", d.model);
        MqttSettings.publish("solaredge/status", String(d.status));
        MqttSettings.publish("solaredge/status_text", statusToString(d.status));
        MqttSettings.publish("solaredge/ac/power", String(d.acPower, 1));
        MqttSettings.publish("solaredge/ac/current", String(d.acCurrent, 2));
        MqttSettings.publish("solaredge/ac/voltage_l1", String(d.acVoltageAN, 1));
        MqttSettings.publish("solaredge/ac/voltage_l2", String(d.acVoltageBN, 1));
        MqttSettings.publish("solaredge/ac/voltage_l3", String(d.acVoltageCN, 1));
        MqttSettings.publish("solaredge/ac/frequency", String(d.acFrequency, 2));
        MqttSettings.publish("solaredge/ac/power_factor", String(d.acPF, 2));
        MqttSettings.publish("solaredge/ac/yieldtotal", String(d.energyTotal / 1000.0f, 3)); // kWh
        MqttSettings.publish("solaredge/dc/power", String(d.dcPower, 1));
        MqttSettings.publish("solaredge/dc/voltage", String(d.dcVoltage, 1));
        MqttSettings.publish("solaredge/dc/current", String(d.dcCurrent, 2));
        MqttSettings.publish("solaredge/temperature", String(d.heatsinkTemp, 1));
        if (d.meterValid) {
            MqttSettings.publish("solaredge/meter/power", String(d.meterPower, 1));
            MqttSettings.publish("solaredge/meter/exported", String(d.meterExported / 1000.0f, 3));
            MqttSettings.publish("solaredge/meter/imported", String(d.meterImported / 1000.0f, 3));
        }
    }

    // Combined production Hoymiles (all enabled inverters) + SolarEdge
    const float hoyPower = Datastore.getTotalAcPowerEnabled();
    const float hoyYield = Datastore.getTotalAcYieldTotalEnabled(); // kWh
    const float sePower = d.valid ? d.acPower : 0.0f;
    MqttSettings.publish("fusion/ac/power", String(hoyPower + sePower, 1));
    MqttSettings.publish("fusion/ac/power_hoymiles", String(hoyPower, 1));
    MqttSettings.publish("fusion/ac/power_solaredge", String(sePower, 1));
    if (d.valid) {
        MqttSettings.publish("fusion/ac/yieldtotal", String(hoyYield + d.energyTotal / 1000.0f, 3));
    }
}

const char* SolarEdgeClass::statusToString(uint16_t status)
{
    switch (status) {
    case 1: return "Off";
    case 2: return "Sleeping";
    case 3: return "Starting";
    case 4: return "Producing (MPPT)";
    case 5: return "Throttled";
    case 6: return "Shutting down";
    case 7: return "Fault";
    case 8: return "Standby";
    default: return "Unknown";
    }
}
