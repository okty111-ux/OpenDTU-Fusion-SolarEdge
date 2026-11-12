// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SolarEdge SunSpec Modbus reader (TCP or RTU/RS485) for OpenDTU (Fusion)
 */
#pragma once

#include <Arduino.h>
#include <TaskSchedulerDeclarations.h>
#include <mutex>

#ifndef SOLAREDGE_DEFAULT_HOST
#define SOLAREDGE_DEFAULT_HOST ""          // e.g. "192.168.1.50"
#endif
#ifndef SOLAREDGE_DEFAULT_PORT
#define SOLAREDGE_DEFAULT_PORT 1502        // SolarEdge default (SetApp), sometimes 502
#endif
#ifndef SOLAREDGE_DEFAULT_UNIT
#define SOLAREDGE_DEFAULT_UNIT 1
#endif
#ifndef SOLAREDGE_DEFAULT_INTERVAL
#define SOLAREDGE_DEFAULT_INTERVAL 10      // seconds
#endif
#ifndef SOLAREDGE_DEFAULT_METER
#define SOLAREDGE_DEFAULT_METER false      // read Meter 1 (SE energy meter)
#endif

// Fusion v2 RS485 transceiver (hardwired, see OpenDTUFusionDocs):
//   GPIO16 = R (receiver out -> ESP RX), GPIO45 = D (driver in <- ESP TX)
//   GPIO46 = DE (driver enable, active high), GPIO15 = RE_N (receiver enable, active low)
#ifndef SOLAREDGE_RS485_RX
#define SOLAREDGE_RS485_RX 16
#endif
#ifndef SOLAREDGE_RS485_TX
#define SOLAREDGE_RS485_TX 45
#endif
#ifndef SOLAREDGE_RS485_DE
#define SOLAREDGE_RS485_DE 46
#endif
#ifndef SOLAREDGE_RS485_RE_N
#define SOLAREDGE_RS485_RE_N 15
#endif

enum SolarEdgeTransport : uint8_t {
    SE_TRANSPORT_TCP = 0, // Modbus TCP over LAN/WiFi
    SE_TRANSPORT_RTU = 1, // Modbus RTU over RS485
};

struct SolarEdgeConfig {
    uint8_t transport = SE_TRANSPORT_TCP;
    uint32_t baud = 115200; // SolarEdge default, 9600..115200
    uint8_t parity = 0; // 0 = 8N1, 1 = 8E1, 2 = 8O1
    String host = SOLAREDGE_DEFAULT_HOST;
    uint16_t port = SOLAREDGE_DEFAULT_PORT;
    uint8_t unitId = SOLAREDGE_DEFAULT_UNIT;
    uint16_t interval = SOLAREDGE_DEFAULT_INTERVAL;
    bool readMeter = SOLAREDGE_DEFAULT_METER;
    bool enabled = true;
};

struct SolarEdgeData {
    bool valid = false;
    uint32_t lastUpdateMs = 0;
    uint32_t errors = 0;
    String lastError;

    // common block
    String manufacturer, model, version, serial;

    // inverter (model 101/102/103)
    uint16_t sunspecDid = 0;
    float acCurrent = 0, acCurrentA = 0, acCurrentB = 0, acCurrentC = 0;
    float acVoltageAN = 0, acVoltageBN = 0, acVoltageCN = 0;
    float acPower = 0;       // W
    float acFrequency = 0;   // Hz
    float acVA = 0, acVAR = 0, acPF = 0;
    float energyTotal = 0;   // Wh lifetime
    float dcCurrent = 0, dcVoltage = 0, dcPower = 0;
    float heatsinkTemp = 0;  // °C
    uint16_t status = 0;
    uint16_t vendorStatus = 0;

    // meter 1 (optional)
    bool meterValid = false;
    float meterPower = 0;    // W, SolarEdge: + = export to grid, - = import
    float meterPowerA = 0, meterPowerB = 0, meterPowerC = 0;
    float meterExported = 0; // Wh
    float meterImported = 0; // Wh
};

class SolarEdgeClass {
public:
    SolarEdgeClass();
    void init(Scheduler& scheduler);

    SolarEdgeData getData();
    SolarEdgeConfig getConfig();
    bool setConfig(const SolarEdgeConfig& cfg);

    static const char* statusToString(uint16_t status);

private:
    static void taskEntry(void* arg);
    void pollLoop();
    bool pollOnce(SolarEdgeData& d, const SolarEdgeConfig& cfg);
    bool readRegisters(const SolarEdgeConfig& cfg, uint16_t addr, uint16_t count, uint16_t* out);
    bool readRegistersTcp(const SolarEdgeConfig& cfg, uint16_t addr, uint16_t count, uint16_t* out);
    bool readRegistersRtu(const SolarEdgeConfig& cfg, uint16_t addr, uint16_t count, uint16_t* out);
    void rs485Begin(const SolarEdgeConfig& cfg);
    void rs485End();
    bool _rs485Open = false;
    String _linkError;
    uint32_t _rs485Baud = 0;
    uint8_t _rs485Parity = 0xFF;

    void loadConfig();
    bool saveConfig();
    void mqttPublish();

    std::mutex _mutex;
    SolarEdgeConfig _cfg;
    SolarEdgeData _data;
    uint16_t _transactionId = 0;
    volatile bool _reconnect = false;

    Task _mqttTask;
};

extern SolarEdgeClass SolarEdge;
