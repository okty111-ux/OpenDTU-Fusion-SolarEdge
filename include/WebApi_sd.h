// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <ESPAsyncWebServer.h>
#include <TaskSchedulerDeclarations.h>

class WebApiSdClass {
public:
    void init(AsyncWebServer& server, Scheduler& scheduler);

private:
    void onPage(AsyncWebServerRequest* request);
    void onSettingsPage(AsyncWebServerRequest* request);
    void onStatus(AsyncWebServerRequest* request);
    void onList(AsyncWebServerRequest* request);
    void onDownload(AsyncWebServerRequest* request);
    void onDelete(AsyncWebServerRequest* request);
    void onConfigPost(AsyncWebServerRequest* request);
    void onMpptGet(AsyncWebServerRequest* request);
    void onMpptPost(AsyncWebServerRequest* request);
    void onDisplayGet(AsyncWebServerRequest* request);
    void onDisplayPost(AsyncWebServerRequest* request);
    void onDisplayNext(AsyncWebServerRequest* request);
};
