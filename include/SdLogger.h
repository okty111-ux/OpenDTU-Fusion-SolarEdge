// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * microSD CSV logger for OpenDTU Fusion v2.
 *
 * Wiring, SDMMC 1-bit mode, 3.3 V card socket (SPI display header is used
 * by the SSD1322 display):
 *   CLK  GPIO14 (display header MISO pin)
 *   CMD  GPIO2  (I2C header SDA)
 *   D0   GPIO1  (I2C header SCL)
 *   D3/CS of the card: 10k pull-up to 3V3; 10k pull-ups on CMD and D0 too.
 * Uses the SDMMC peripheral, not SPI -> works with NRF24 + CMT + W5500.
 *
 * Files:
 *   /fusion/YYYY/YYYY-MM-DD.csv   one row every N minutes
 *   /fusion/dni.csv               one row per finished day
 */
#pragma once

#include <Arduino.h>
#include <TaskSchedulerDeclarations.h>
#include <mutex>

#ifndef SD_PIN_CLK
#define SD_PIN_CLK 14
#endif
#ifndef SD_PIN_CMD
#define SD_PIN_CMD 2
#endif
#ifndef SD_PIN_D0
#define SD_PIN_D0 1
#endif
#ifndef SD_PIN_D3
#define SD_PIN_D3 -1
#endif
#ifndef SD_MMC_FREQ_KHZ
#define SD_MMC_FREQ_KHZ 20000
#endif
#ifndef SD_DEFAULT_INTERVAL_MIN
#define SD_DEFAULT_INTERVAL_MIN 5
#endif
// 1 = Polish Excel friendly: ';' separator and ',' decimal mark
#ifndef SD_CSV_DECIMAL_COMMA
#define SD_CSV_DECIMAL_COMMA 1
#endif

#define SD_LOG_ROOT "/fusion"

struct SdStatus {
    bool compiled = false;
    bool mounted = false;
    String reason;
    String cardType;
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    uint16_t intervalMin = SD_DEFAULT_INTERVAL_MIN;
    bool enabled = true;
    String lastWrite;
    uint32_t writeErrors = 0;
};

class SdLoggerClass {
public:
    SdLoggerClass();
    void init(Scheduler& scheduler);

    SdStatus getStatus();
    bool isMounted() const { return _mounted; }
    bool setConfig(bool enabled, uint16_t intervalMin);
    std::mutex& fsMutex() { return _fsMutex; }

    static bool isSafePath(const String& path);

private:
    void loop();
    bool mount();
    void unmount(const char* reason);
    bool appendLine(const String& path, const String& header, const String& line);
    void writeSample(const struct tm& t);
    void writeDaySummary(const struct EnergyRecord& r);
    void loadConfig();
    bool saveConfig();

    static String num(float v, uint8_t digits);

    Task _loopTask;
    std::mutex _fsMutex;
    bool _busOk = false;
    volatile bool _mounted = false;
    String _reason = "not initialised";
    bool _enabled = true;
    uint16_t _intervalMin = SD_DEFAULT_INTERVAL_MIN;
    int _lastSlot = -1;
    char _lastSummary[11] = { 0 }; // last day written to dni.csv (persisted)
    uint32_t _lastMountTry = 0;
    String _lastWrite;
    uint32_t _writeErrors = 0;
};

extern SdLoggerClass SdLogger;
