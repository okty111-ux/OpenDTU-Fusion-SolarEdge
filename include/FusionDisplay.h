// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SSD1322 256x64 OLED with several pages, switched with a push button.
 *
 * Wiring (Fusion v2 SPI display header, display in 4-wire SPI mode):
 *   DIN/SDA GPIO9, SCLK GPIO10, CS GPIO11, DC GPIO12, RES GPIO13, VCC 3V3, GND
 * Button: on-board BOOT button (GPIO0) or an external push button GPIO0 -> GND.
 *   short press  = next page (and wake the screen)
 *   long press   = automatic page switching on/off
 *
 * Both hardware SPI hosts of the ESP32-S3 are taken by NRF24/CMT2300A/W5500,
 * therefore the display is driven by a fast register based software SPI.
 * Config: LittleFS /display.json
 */
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <TaskSchedulerDeclarations.h>
#include <mutex>

struct FdData;

#ifndef FD_PIN_MOSI
#define FD_PIN_MOSI 9
#endif
#ifndef FD_PIN_SCK
#define FD_PIN_SCK 10
#endif
#ifndef FD_PIN_CS
#define FD_PIN_CS 11
#endif
#ifndef FD_PIN_DC
#define FD_PIN_DC 12
#endif
#ifndef FD_PIN_RST
#define FD_PIN_RST 13
#endif
#ifndef FD_PIN_BUTTON
#define FD_PIN_BUTTON 0
#endif

struct FusionDisplayConfig {
    bool enabled = false;
    uint16_t autoSec = 10;  // 0 = no automatic switching
    uint16_t sleepMin = 10; // screen off at night after N min without button, 0 = never
    uint8_t contrast = 160; // 0..255
    bool flip = false;      // rotate 180°
    uint8_t pages = 0xFF;   // bit mask of enabled pages
};

class FusionDisplayClass {
public:
    FusionDisplayClass();
    void init(Scheduler& scheduler);
    FusionDisplayConfig getConfig();
    bool setConfig(const FusionDisplayConfig& c);
    void toJson(JsonObject root);
    void nextPage(); // also from the web UI

private:
    void loop();
    void begin();
    void draw();
    void collect(FdData& d);
    bool producing();
    void loadConfig();
    bool saveConfig();
    uint8_t nextAvailable(const FdData& d, uint8_t from);

    std::mutex _mutex;
    FusionDisplayConfig _cfg;
    Task _loopTask;
    void* _u8g2 = nullptr; // u8g2_t*
    bool _started = false;
    bool _sleeping = false;
    bool _autoOn = true;
    uint8_t _page = 0;
    uint32_t _lastDraw = 0, _lastSwitch = 0, _lastActivity = 0;
    uint32_t _overlayUntil = 0;
    String _ov1, _ov2;
    bool _btnDown = false, _btnLongDone = false;
    uint32_t _btnSince = 0, _btnChange = 0;
    volatile bool _webNext = false;
};

extern FusionDisplayClass FusionDisplay;
