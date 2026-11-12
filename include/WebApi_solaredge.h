// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <ESPAsyncWebServer.h>
#include <TaskSchedulerDeclarations.h>

class WebApiSolarEdgeClass {
public:
    void init(AsyncWebServer& server, Scheduler& scheduler);

private:
    void onStatus(AsyncWebServerRequest* request);
    void onConfigGet(AsyncWebServerRequest* request);
    void onConfigPost(AsyncWebServerRequest* request);
    void onHistoryPage(AsyncWebServerRequest* request);
    void onHistoryGet(AsyncWebServerRequest* request);
    void onHistoryReset(AsyncWebServerRequest* request);
    void onPowerGet(AsyncWebServerRequest* request);
};
