// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * "Aktualizacja z GitHuba" - firmware update straight from GitHub releases.
 *
 * Releases of the configured repository are tagged "<prefix><N>" (default
 * "fusion-v<N>").  The device lists the releases through the GitHub API, picks
 * the highest N, downloads asset "fusion-<pioenv>.bin", checks its SHA-256
 * against SHA256SUMS.txt from the same release and writes it to the inactive
 * OTA partition.  Only after a successful verification the boot partition is
 * switched and the device restarts.
 */
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <TaskSchedulerDeclarations.h>
#include <mutex>

struct FusionUpdateConfig {
    String repo;
    String prefix;
    bool autoCheck = true;
};

enum class FuState : uint8_t { Idle, Checking, UpToDate, Available, Downloading, Verifying, Done, Error };

class FusionUpdateClass {
public:
    void init(Scheduler& scheduler);
    void toJson(JsonVariant root);
    FusionUpdateConfig getConfig();
    bool setConfig(const FusionUpdateConfig& c);
    bool requestCheck();   // async
    bool requestInstall(); // async, requires a found release
    bool updateAvailable();

private:
    void loop();
    static void taskEntry(void* arg);
    void doCheck();
    void doInstall();
    bool fetchSha(const String& url, String& sha);
    void setState(FuState s, const String& msg = "");
    void loadConfig();

    std::mutex _mutex;
    FusionUpdateConfig _cfg;
    FuState _state = FuState::Idle;
    String _msg;
    // newest release found
    int _latest = 0;
    String _tag, _name, _date, _notes, _binUrl, _shaUrl, _pageUrl;
    uint32_t _binSize = 0;
    uint32_t _done = 0, _total = 0;
    time_t _lastCheck = 0;
    uint32_t _bootMs = 0, _lastCheckMs = 0;
    bool _checkedOnce = false;
    volatile bool _busy = false;
    uint8_t _job = 0; // 1 = check, 2 = install
    Task _loopTask;
};

extern FusionUpdateClass FusionUpdate;
