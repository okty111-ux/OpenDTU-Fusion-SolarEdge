// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <ESPAsyncWebServer.h>
#include <TaskSchedulerDeclarations.h>

class WebApiFusionClass {
public:
    void init(AsyncWebServer& server, Scheduler& scheduler);

private:
    void onUpdatePage(AsyncWebServerRequest* request);
    void onTuyaPage(AsyncWebServerRequest* request);
    void onUpdateGet(AsyncWebServerRequest* request);
    void onUpdateCheck(AsyncWebServerRequest* request);
    void onUpdateInstall(AsyncWebServerRequest* request);
    void onUpdateConfig(AsyncWebServerRequest* request);
    void onTuyaGet(AsyncWebServerRequest* request);
    void onTuyaPost(AsyncWebServerRequest* request);
    void onTuyaTest(AsyncWebServerRequest* request);
};
