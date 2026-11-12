// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   GET  /fusion/update               "Aktualizacja z GitHuba" page (admin)
 *   GET  /fusion/tuya                 Tuya cloud page (admin)
 *   GET  /api/fusion/update           version + newest release (read-only)
 *   POST /api/fusion/update/check     look for a new release (admin)
 *   POST /api/fusion/update/install   download, verify and install (admin)
 *   POST /api/fusion/update/config    data={"repo":"user/repo","prefix":"fusion-v","auto":true} (admin)
 *   GET  /api/fusion/tuya             config + status, never the secret (admin)
 *   POST /api/fusion/tuya             data={"enabled":true,"region":"eu","device_id":"..","secret":"..","interval":60} (admin)
 *   POST /api/fusion/tuya/test        send a report now (admin)
 */
#include "WebApi_fusion.h"
#include "FusionPages.h"
#include "FusionTuya.h"
#include "FusionUpdate.h"
#include "WebApi.h"
#include "WebApi_errors.h"
#include <AsyncJson.h>

#define BIND(f) static_cast<ArRequestHandlerFunction>(std::bind(&WebApiFusionClass::f, this, std::placeholders::_1))

void WebApiFusionClass::init(AsyncWebServer& server, Scheduler&)
{
    server.on("/fusion/update", HTTP_GET, BIND(onUpdatePage));
    server.on("/fusion/tuya", HTTP_GET, BIND(onTuyaPage));
    server.on("/api/fusion/update", HTTP_GET, BIND(onUpdateGet));
    server.on("/api/fusion/update/check", HTTP_POST, BIND(onUpdateCheck));
    server.on("/api/fusion/update/install", HTTP_POST, BIND(onUpdateInstall));
    server.on("/api/fusion/update/config", HTTP_POST, BIND(onUpdateConfig));
    server.on("/api/fusion/tuya/test", HTTP_POST, BIND(onTuyaTest));
    server.on("/api/fusion/tuya", HTTP_GET, BIND(onTuyaGet));
    server.on("/api/fusion/tuya", HTTP_POST, BIND(onTuyaPost));
}

static void reply(AsyncWebServerRequest* request, AsyncJsonResponse* response, bool ok, const char* okMsg, const char* errMsg)
{
    auto& r = response->getRoot();
    r["type"] = ok ? "success" : "warning";
    r["message"] = ok ? okMsg : errMsg;
    r["code"] = ok ? WebApiError::GenericSuccess : WebApiError::GenericWriteFailed;
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiFusionClass::onUpdatePage(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) return;
    request->send(200, "text/html; charset=utf-8", UPDATE_PAGE_HTML);
}

void WebApiFusionClass::onTuyaPage(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) return;
    request->send(200, "text/html; charset=utf-8", TUYA_PAGE_HTML);
}

void WebApiFusionClass::onUpdateGet(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) return;
    AsyncJsonResponse* response = new AsyncJsonResponse();
    FusionUpdate.toJson(response->getRoot());
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiFusionClass::onUpdateCheck(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) return;
    reply(request, new AsyncJsonResponse(), FusionUpdate.requestCheck(), "Checking", "Busy");
}

void WebApiFusionClass::onUpdateInstall(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) return;
    const bool ok = FusionUpdate.updateAvailable() && FusionUpdate.requestInstall();
    reply(request, new AsyncJsonResponse(), ok, "Installing", "No update or busy");
}

void WebApiFusionClass::onUpdateConfig(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) return;
    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonDocument root;
    if (!WebApi.parseRequestData(request, response, root)) return;
    FusionUpdateConfig c = FusionUpdate.getConfig();
    if (root["repo"].is<const char*>()) c.repo = root["repo"].as<String>();
    if (root["prefix"].is<const char*>()) c.prefix = root["prefix"].as<String>();
    c.autoCheck = root["auto"] | c.autoCheck;
    reply(request, response, FusionUpdate.setConfig(c), "Settings saved!", "Invalid repository");
}

void WebApiFusionClass::onTuyaGet(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) return;
    AsyncJsonResponse* response = new AsyncJsonResponse();
    FusionTuya.statusToJson(response->getRoot());
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiFusionClass::onTuyaPost(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) return;
    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonDocument root;
    if (!WebApi.parseRequestData(request, response, root)) return;
    FusionTuyaConfig c = FusionTuya.getConfig();
    c.enabled = root["enabled"] | c.enabled;
    if (root["region"].is<const char*>()) c.region = root["region"].as<String>();
    if (root["device_id"].is<const char*>()) c.deviceId = root["device_id"].as<String>();
    const String sec = root["secret"] | "";
    const bool keep = sec.isEmpty();
    if (!keep) c.secret = sec;
    c.interval = root["interval"] | c.interval;
    reply(request, response, FusionTuya.setConfig(c, keep), "Settings saved!", "Failed to save settings!");
}

void WebApiFusionClass::onTuyaTest(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) return;
    FusionTuya.reportNow();
    reply(request, new AsyncJsonResponse(), true, "Queued", "");
}
