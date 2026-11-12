// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * REST API for the SolarEdge Modbus-TCP reader
 *   GET  /api/solaredge/status   live values
 *   GET  /api/solaredge/config   current settings
 *   POST /api/solaredge/config   data={"host":"192.168.1.50","port":1502,...}
 *   GET  /history                history page (tables)
 *   GET  /api/history            stored daily/monthly/yearly energy
 *   POST /api/history/reset      clear stored history (admin)
 */
#include "WebApi_solaredge.h"
#include "Datastore.h"
#include "EnergyHistory.h"
#include "PowerLog.h"
#include "HistoryPage.h"
#include "SolarEdge.h"
#include "WebApi.h"
#include "WebApi_errors.h"
#include <AsyncJson.h>

void WebApiSolarEdgeClass::init(AsyncWebServer& server, Scheduler& scheduler)
{
    using std::placeholders::_1;
    server.on("/api/solaredge/status", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSolarEdgeClass::onStatus, this, _1)));
    server.on("/api/solaredge/config", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSolarEdgeClass::onConfigGet, this, _1)));
    server.on("/history", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSolarEdgeClass::onHistoryPage, this, _1)));
    server.on("/api/history/power", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSolarEdgeClass::onPowerGet, this, _1)));
    server.on("/api/history/reset", HTTP_POST, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSolarEdgeClass::onHistoryReset, this, _1)));
    server.on("/api/history", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSolarEdgeClass::onHistoryGet, this, _1)));
    server.on("/api/solaredge/config", HTTP_POST, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSolarEdgeClass::onConfigPost, this, _1)));
}

void WebApiSolarEdgeClass::onStatus(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) {
        return;
    }
    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    const SolarEdgeData d = SolarEdge.getData();

    root["valid"] = d.valid;
    root["age_ms"] = d.lastUpdateMs ? millis() - d.lastUpdateMs : 0;
    root["errors"] = d.errors;
    root["last_error"] = d.lastError;
    root["manufacturer"] = d.manufacturer;
    root["model"] = d.model;
    root["version"] = d.version;
    root["serial"] = d.serial;
    root["sunspec_did"] = d.sunspecDid;
    root["status"] = d.status;
    root["status_text"] = SolarEdgeClass::statusToString(d.status);

    auto ac = root["ac"].to<JsonObject>();
    ac["power"] = d.acPower;
    ac["current"] = d.acCurrent;
    ac["current_l1"] = d.acCurrentA;
    ac["current_l2"] = d.acCurrentB;
    ac["current_l3"] = d.acCurrentC;
    ac["voltage_l1"] = d.acVoltageAN;
    ac["voltage_l2"] = d.acVoltageBN;
    ac["voltage_l3"] = d.acVoltageCN;
    ac["frequency"] = d.acFrequency;
    ac["apparent_power"] = d.acVA;
    ac["reactive_power"] = d.acVAR;
    ac["power_factor"] = d.acPF;
    ac["yield_total_kwh"] = d.energyTotal / 1000.0f;

    auto dc = root["dc"].to<JsonObject>();
    dc["power"] = d.dcPower;
    dc["voltage"] = d.dcVoltage;
    dc["current"] = d.dcCurrent;
    root["temperature"] = d.heatsinkTemp;

    if (d.meterValid) {
        auto m = root["meter"].to<JsonObject>();
        m["power"] = d.meterPower;
        m["power_l1"] = d.meterPowerA;
        m["power_l2"] = d.meterPowerB;
        m["power_l3"] = d.meterPowerC;
        m["exported_kwh"] = d.meterExported / 1000.0f;
        m["imported_kwh"] = d.meterImported / 1000.0f;
    }

    auto total = root["fusion"].to<JsonObject>();
    const float hoy = Datastore.getTotalAcPowerEnabled();
    total["power_hoymiles"] = hoy;
    total["power_solaredge"] = d.valid ? d.acPower : 0.0f;
    total["power_total"] = hoy + (d.valid ? d.acPower : 0.0f);

    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiSolarEdgeClass::onConfigGet(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) {
        return;
    }
    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    const SolarEdgeConfig c = SolarEdge.getConfig();
    root["enabled"] = c.enabled;
    root["transport"] = c.transport == SE_TRANSPORT_RTU ? "rtu" : "tcp";
    root["baud"] = c.baud;
    root["parity"] = c.parity;
    root["host"] = c.host;
    root["port"] = c.port;
    root["unit_id"] = c.unitId;
    root["interval"] = c.interval;
    root["read_meter"] = c.readMeter;
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiSolarEdgeClass::onConfigPost(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) {
        return;
    }
    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonDocument root;
    if (!WebApi.parseRequestData(request, response, root)) {
        return;
    }
    auto& retMsg = response->getRoot();

    SolarEdgeConfig c = SolarEdge.getConfig();
    if (root["enabled"].is<bool>()) c.enabled = root["enabled"].as<bool>();
    if (root["transport"].is<String>()) c.transport = root["transport"].as<String>() == "rtu" ? SE_TRANSPORT_RTU : SE_TRANSPORT_TCP;
    if (root["baud"].is<uint32_t>()) c.baud = root["baud"].as<uint32_t>();
    if (root["parity"].is<uint8_t>()) c.parity = root["parity"].as<uint8_t>();
    if (root["host"].is<String>()) c.host = root["host"].as<String>();
    if (root["port"].is<uint16_t>()) c.port = root["port"].as<uint16_t>();
    if (root["unit_id"].is<uint8_t>()) c.unitId = root["unit_id"].as<uint8_t>();
    if (root["interval"].is<uint16_t>()) c.interval = root["interval"].as<uint16_t>();
    if (root["read_meter"].is<bool>()) c.readMeter = root["read_meter"].as<bool>();

    if (c.baud < 1200 || c.baud > 230400 || c.parity > 2 || c.unitId == 0 || c.unitId > 247) {
        retMsg["message"] = "Invalid RS485 settings!";
        retMsg["code"] = WebApiError::GenericValueMissing;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }
    if (c.host.length() > 64 || (c.transport == SE_TRANSPORT_TCP && c.port == 0)) {
        retMsg["message"] = "Invalid host or port!";
        retMsg["code"] = WebApiError::GenericValueMissing;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }

    const bool ok = SolarEdge.setConfig(c);
    retMsg["type"] = ok ? "success" : "warning";
    retMsg["message"] = ok ? "Settings saved!" : "Failed to save settings!";
    retMsg["code"] = ok ? WebApiError::GenericSuccess : WebApiError::GenericWriteFailed;
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiSolarEdgeClass::onHistoryPage(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) {
        return;
    }
    request->send(200, "text/html; charset=utf-8", HISTORY_PAGE_HTML);
}

void WebApiSolarEdgeClass::onHistoryGet(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) {
        return;
    }
    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto root = response->getRoot();
    EnergyHistory.toJson(root);
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiSolarEdgeClass::onHistoryReset(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) {
        return;
    }
    EnergyHistory.reset();
    PowerLog.reset();
    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& retMsg = response->getRoot();
    retMsg["type"] = "success";
    retMsg["message"] = "History cleared";
    retMsg["code"] = WebApiError::GenericSuccess;
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

// GET /api/history/power?day=0 (today) | 1 (yesterday)
void WebApiSolarEdgeClass::onPowerGet(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) {
        return;
    }
    const uint8_t which = request->hasParam("day") && request->getParam("day")->value() == "1" ? 1 : 0;
    AsyncJsonResponse* response = new AsyncJsonResponse();
    PowerLog.toJson(response->getRoot().as<JsonObject>(), which);
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}
