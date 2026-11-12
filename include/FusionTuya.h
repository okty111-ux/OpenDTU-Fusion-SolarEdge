// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Sends production data to the Tuya cloud (TuyaLink MQTT, Tuya Developer
 * Platform).  The device is created on the platform as a TuyaLink product
 * with the properties listed in FusionTuya.cpp; deviceId + deviceSecret of
 * the registered device are entered on /fusion/tuya.
 */
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <TaskSchedulerDeclarations.h>
#include <mutex>

struct FusionTuyaConfig {
    bool enabled = false;
    String region = "eu"; // eu, eu-w, us, us-e, cn, in
    String deviceId;
    String secret;
    uint16_t interval = 60; // s
};

class FusionTuyaClass {
public:
    void init(Scheduler& scheduler);
    FusionTuyaConfig getConfig();
    bool setConfig(const FusionTuyaConfig& c, bool keepSecret);
    void statusToJson(JsonVariant root);
    void reportNow() { _forceReport = true; }

    // called from the MQTT task
    void onEvent(int32_t id, void* data);

private:
    void loop();
    void connect();
    void disconnect();
    void report();
    void load();
    static String host(const String& region);

    std::mutex _mutex;
    FusionTuyaConfig _cfg;
    void* _client = nullptr;
    volatile bool _connected = false;
    volatile bool _forceReport = false;
    bool _restart = false;
    uint32_t _lastTry = 0, _lastReport = 0, _retry = 5000;
    uint32_t _sent = 0, _acked = 0;
    int _lastCode = -1;
    String _lastErr, _lastPayload;
    time_t _lastOk = 0;
    String _topicReport, _topicResp;
    Task _loopTask;
};

extern FusionTuyaClass FusionTuya;
