// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   GET  /sd                       microSD file browser page
 *   GET  /fusion/settings          inverter communication settings page
 *   GET  /api/sd/status
 *   GET  /api/sd/list?dir=/fusion/2026
 *   GET  /api/sd/download?path=/fusion/2026/2026-11-12.csv
 *   POST /api/sd/delete            data={"path":"..."}           (admin)
 *   POST /api/sd/config            data={"enabled":true,"interval":5} (admin)
 *   GET  /api/fusion/display       display settings (admin)
 *   POST /api/fusion/display       data={"enabled":true,"auto_sec":10,"sleep_min":10,"contrast":160,"flip":false,"pages":127} (admin)
 *   POST /api/fusion/display/next  switch page (admin)
 *   GET  /api/mppt/config          panels per MPPT (admin)
 *   POST /api/mppt/config          data={"<serial>":[{"series":true,"panels":[{"name":"","wp":450,"vmp":34.1},...]}]} (admin)
 */
#include "WebApi_sd.h"
#include "FusionPages.h"
#include "FusionDisplay.h"
#include "HoymilesChannels.h"
#include "SdLogger.h"
#include "WebApi.h"
#include "WebApi_errors.h"
#include <AsyncJson.h>
#include <SD_MMC.h>
#include <algorithm>
#include <vector>

void WebApiSdClass::init(AsyncWebServer& server, Scheduler& scheduler)
{
    using std::placeholders::_1;
    server.on("/sd", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSdClass::onPage, this, _1)));
    server.on("/fusion/settings", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSdClass::onSettingsPage, this, _1)));
    server.on("/api/sd/status", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSdClass::onStatus, this, _1)));
    server.on("/api/sd/list", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSdClass::onList, this, _1)));
    server.on("/api/sd/download", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSdClass::onDownload, this, _1)));
    server.on("/api/sd/delete", HTTP_POST, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSdClass::onDelete, this, _1)));
    server.on("/api/sd/config", HTTP_POST, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSdClass::onConfigPost, this, _1)));
    server.on("/api/mppt/config", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSdClass::onMpptGet, this, _1)));
    server.on("/api/mppt/config", HTTP_POST, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSdClass::onMpptPost, this, _1)));
    server.on("/api/fusion/display", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSdClass::onDisplayGet, this, _1)));
    server.on("/api/fusion/display", HTTP_POST, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSdClass::onDisplayPost, this, _1)));
    server.on("/api/fusion/display/next", HTTP_POST, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSdClass::onDisplayNext, this, _1)));
}

void WebApiSdClass::onPage(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) return;
    request->send(200, "text/html; charset=utf-8", SD_PAGE_HTML);
}

void WebApiSdClass::onSettingsPage(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) return; // admin only
    request->send(200, "text/html; charset=utf-8", SETTINGS_PAGE_HTML);
}

void WebApiSdClass::onStatus(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) return;
    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    const SdStatus s = SdLogger.getStatus();
    root["available"] = s.compiled;
    root["mounted"] = s.mounted;
    root["reason"] = s.reason;
    root["enabled"] = s.enabled;
    root["interval"] = s.intervalMin;
    root["card_type"] = s.cardType;
    root["total_mb"] = (uint32_t)(s.totalBytes / (1024ULL * 1024ULL));
    root["used_mb"] = (uint32_t)(s.usedBytes / (1024ULL * 1024ULL));
    root["last_write"] = s.lastWrite;
    root["write_errors"] = s.writeErrors;
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiSdClass::onList(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) return;
    String dir = request->hasParam("dir") ? request->getParam("dir")->value() : String(SD_LOG_ROOT);
    while (dir.length() > 1 && dir.endsWith("/")) dir.remove(dir.length() - 1);

    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    root["dir"] = dir;

    if (!SdLogger.isMounted() || !SdLoggerClass::isSafePath(dir)) {
        root["error"] = SdLogger.isMounted() ? "invalid path" : "card not mounted";
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }

    struct Entry {
        String name;
        bool isDir;
        size_t size;
    };
    std::vector<Entry> entries;
    {
        std::lock_guard<std::mutex> lock(SdLogger.fsMutex());
        File d = SD_MMC.open(dir);
        if (d && d.isDirectory()) {
            File f = d.openNextFile();
            while (f && entries.size() < 400) {
                String n = f.name();
                const int slash = n.lastIndexOf('/');
                if (slash >= 0) n = n.substring(slash + 1);
                entries.push_back({ n, f.isDirectory(), f.size() });
                f.close();
                f = d.openNextFile();
            }
            d.close();
        }
    }
    // directories first, newest (reverse alphabetical = newest date) first
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.isDir != b.isDir) return a.isDir;
        return a.name > b.name;
    });
    auto arr = root["entries"].to<JsonArray>();
    for (auto& e : entries) {
        auto o = arr.add<JsonObject>();
        o["name"] = e.name;
        o["dir"] = e.isDir;
        o["size"] = e.size;
    }
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiSdClass::onDownload(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) return;
    if (!request->hasParam("path")) {
        request->send(400, "text/plain", "missing path");
        return;
    }
    const String path = request->getParam("path")->value();
    if (!SdLogger.isMounted() || !SdLoggerClass::isSafePath(path)) {
        request->send(404, "text/plain", "not found");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(SdLogger.fsMutex());
        if (!SD_MMC.exists(path)) {
            request->send(404, "text/plain", "not found");
            return;
        }
    }
    // download=true -> Content-Disposition: attachment
    request->send(SD_MMC, path, "text/csv; charset=utf-8", true);
}

void WebApiSdClass::onDelete(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) return;
    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonDocument root;
    if (!WebApi.parseRequestData(request, response, root)) return;
    auto& retMsg = response->getRoot();
    const String path = root["path"] | "";
    bool ok = false;
    if (SdLogger.isMounted() && SdLoggerClass::isSafePath(path) && path != SD_LOG_ROOT) {
        std::lock_guard<std::mutex> lock(SdLogger.fsMutex());
        ok = SD_MMC.remove(path) || SD_MMC.rmdir(path);
    }
    retMsg["type"] = ok ? "success" : "warning";
    retMsg["message"] = ok ? "Deleted" : "Delete failed (directories must be empty)";
    retMsg["code"] = ok ? WebApiError::GenericSuccess : WebApiError::GenericWriteFailed;
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiSdClass::onConfigPost(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) return;
    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonDocument root;
    if (!WebApi.parseRequestData(request, response, root)) return;
    auto& retMsg = response->getRoot();
    const SdStatus s = SdLogger.getStatus();
    const bool ok = SdLogger.setConfig(root["enabled"] | s.enabled, root["interval"] | s.intervalMin);
    retMsg["type"] = ok ? "success" : "warning";
    retMsg["message"] = ok ? "Settings saved!" : "Failed to save settings!";
    retMsg["code"] = ok ? WebApiError::GenericSuccess : WebApiError::GenericWriteFailed;
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiSdClass::onMpptGet(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) return;
    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto root = response->getRoot().as<JsonObject>();
    HoymilesChannels.configToJson(root);
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiSdClass::onMpptPost(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) return;
    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonDocument root;
    if (!WebApi.parseRequestData(request, response, root)) return;
    auto& retMsg = response->getRoot();
    if (!root.is<JsonObject>() || !HoymilesChannels.configFromJson(root.as<JsonObjectConst>())) {
        retMsg["type"] = "warning";
        retMsg["message"] = "Invalid MPPT config";
    } else {
        retMsg["type"] = "success";
        retMsg["message"] = "MPPT config saved";
    }
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiSdClass::onDisplayGet(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) return;
    AsyncJsonResponse* response = new AsyncJsonResponse();
    FusionDisplay.toJson(response->getRoot().as<JsonObject>());
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiSdClass::onDisplayPost(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) return;
    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonDocument root;
    if (!WebApi.parseRequestData(request, response, root)) return;
    FusionDisplayConfig c = FusionDisplay.getConfig();
    c.enabled = root["enabled"] | c.enabled;
    c.autoSec = constrain((int)(root["auto_sec"] | (int)c.autoSec), 0, 600);
    c.sleepMin = constrain((int)(root["sleep_min"] | (int)c.sleepMin), 0, 1440);
    c.contrast = constrain((int)(root["contrast"] | (int)c.contrast), 0, 255);
    c.flip = root["flip"] | c.flip;
    c.pages = (uint8_t)(root["pages"] | (int)c.pages);
    const bool ok = FusionDisplay.setConfig(c);
    auto& retMsg = response->getRoot();
    retMsg["type"] = ok ? "success" : "warning";
    retMsg["message"] = ok ? "Settings saved!" : "Failed to save settings!";
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiSdClass::onDisplayNext(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) return;
    FusionDisplay.nextPage();
    AsyncJsonResponse* response = new AsyncJsonResponse();
    response->getRoot()["type"] = "success";
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}
