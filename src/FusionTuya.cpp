// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * TuyaLink MQTT (https://developer.tuya.com/en/docs/iot/MQTT-protocol?id=Kb65nphxrj8f1)
 *   host     m1.tuyaeu.com:8883 (TLS)
 *   clientId tuyalink_<deviceId>
 *   username <deviceId>|signMethod=hmacSha256,timestamp=<s>,secureMode=1,accessType=1
 *   password hex(HMAC-SHA256(deviceSecret,
 *            "deviceId=<id>,timestamp=<s>,secureMode=1,accessType=1"))
 *   publish  tylink/<deviceId>/thing/property/report
 *
 * Properties (TuyaLink thing model, all type "value", scale 0, read-only):
 *   pv_power          W   total production (Hoymiles + SolarEdge)
 *   hoymiles_power    W
 *   solaredge_power   W
 *   grid_power        W   + export / - import (SolarEdge meter)
 *   energy_today      Wh  production today
 *   energy_month      kWh production this month
 *   energy_total      kWh lifetime production
 */
#include "FusionTuya.h"
#include "Datastore.h"
#include "EnergyHistory.h"
#include <esp_log.h>
#include "SolarEdge.h"
#include <LittleFS.h>
#include <mbedtls/md.h>
#include <mqtt_client.h>

extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

#undef TAG
static const char* TAG = "tuya";

FusionTuyaClass FusionTuya;

#define TUYA_CFG_FILE "/fusion_tuya.json"

String FusionTuyaClass::host(const String& r)
{
    if (r == "us") return "m1.tuyaus.com";
    if (r == "us-e") return "m1-ueaz.tuyaus.com";
    if (r == "eu-w") return "m1-weaz.tuyaeu.com";
    if (r == "cn") return "m1.tuyacn.com";
    if (r == "in") return "m1.tuyain.com";
    return "m1.tuyaeu.com";
}

static String hmacHex(const String& key, const String& msg)
{
    uint8_t out[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t*)key.c_str(), key.length());
    mbedtls_md_hmac_update(&ctx, (const uint8_t*)msg.c_str(), msg.length());
    mbedtls_md_hmac_finish(&ctx, out);
    mbedtls_md_free(&ctx);
    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", out[i]);
    return String(hex);
}

static void mqttHandler(void* arg, esp_event_base_t, int32_t id, void* data)
{
    static_cast<FusionTuyaClass*>(arg)->onEvent(id, data);
}

void FusionTuyaClass::load()
{
    File f = LittleFS.open(TUYA_CFG_FILE, "r");
    if (!f) return;
    JsonDocument d;
    if (!deserializeJson(d, f)) {
        _cfg.enabled = d["enabled"] | false;
        _cfg.region = d["region"] | "eu";
        _cfg.deviceId = d["device_id"] | "";
        _cfg.secret = d["secret"] | "";
        _cfg.interval = constrain((int)(d["interval"] | 60), 10, 3600);
    }
    f.close();
}

void FusionTuyaClass::init(Scheduler& scheduler)
{
    load();
    scheduler.addTask(_loopTask);
    _loopTask.setCallback(std::bind(&FusionTuyaClass::loop, this));
    _loopTask.setIterations(TASK_FOREVER);
    _loopTask.setInterval(TASK_SECOND);
    _loopTask.enable();
}

FusionTuyaConfig FusionTuyaClass::getConfig()
{
    std::lock_guard<std::mutex> l(_mutex);
    return _cfg;
}

bool FusionTuyaClass::setConfig(const FusionTuyaConfig& in, bool keepSecret)
{
    FusionTuyaConfig c = in;
    c.deviceId.trim();
    c.secret.trim();
    c.interval = constrain((int)c.interval, 10, 3600);
    {
        std::lock_guard<std::mutex> l(_mutex);
        if (keepSecret) c.secret = _cfg.secret;
        _cfg = c;
        _restart = true;
        _lastErr = "";
    }
    File f = LittleFS.open(TUYA_CFG_FILE, "w");
    if (!f) return false;
    JsonDocument d;
    d["enabled"] = c.enabled;
    d["region"] = c.region;
    d["device_id"] = c.deviceId;
    d["secret"] = c.secret;
    d["interval"] = c.interval;
    serializeJson(d, f);
    f.close();
    return true;
}

void FusionTuyaClass::disconnect()
{
    if (!_client) return;
    auto c = (esp_mqtt_client_handle_t)_client;
    _client = nullptr;
    _connected = false;
    esp_mqtt_client_stop(c);
    esp_mqtt_client_destroy(c);
}

void FusionTuyaClass::connect()
{
    const FusionTuyaConfig c = getConfig();
    const time_t ts = time(nullptr);
    const String tss = String((uint32_t)ts);
    static String uri, cid, user, pass; // must outlive esp_mqtt_client_init
    uri = "mqtts://" + host(c.region) + ":8883";
    cid = "tuyalink_" + c.deviceId;
    user = c.deviceId + "|signMethod=hmacSha256,timestamp=" + tss + ",secureMode=1,accessType=1";
    pass = hmacHex(c.secret, "deviceId=" + c.deviceId + ",timestamp=" + tss + ",secureMode=1,accessType=1");
    _topicReport = "tylink/" + c.deviceId + "/thing/property/report";
    _topicResp = "tylink/" + c.deviceId + "/thing/property/report_response";

    esp_mqtt_client_config_t m = {};
    m.uri = uri.c_str();
    m.client_id = cid.c_str();
    m.username = user.c_str();
    m.password = pass.c_str();
    m.keepalive = 60;
    m.disable_auto_reconnect = true; // fresh signature on every connect
    m.crt_bundle_attach = esp_crt_bundle_attach;
    m.network_timeout_ms = 15000;
    m.buffer_size = 1536;
    m.task_stack = 6144;
    auto h = esp_mqtt_client_init(&m);
    if (!h) {
        _lastErr = "brak pamięci dla klienta MQTT";
        return;
    }
    esp_mqtt_client_register_event(h, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqttHandler, this);
    _client = h;
    esp_mqtt_client_start(h);
    ESP_LOGI(TAG, "Tuya: connecting to %s as %s", uri.c_str(), cid.c_str());
}

void FusionTuyaClass::onEvent(int32_t id, void* data)
{
    auto ev = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        _connected = true;
        _retry = 5000;
        esp_mqtt_client_subscribe(ev->client, _topicResp.c_str(), 1);
        {
            std::lock_guard<std::mutex> l(_mutex);
            _lastErr = "";
        }
        _forceReport = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        _connected = false;
        break;
    case MQTT_EVENT_ERROR: {
        std::lock_guard<std::mutex> l(_mutex);
        if (ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            const int rc = ev->error_handle->connect_return_code;
            _lastErr = rc == 4 || rc == 5 ? "Tuya odrzuciła logowanie (sprawdź Device ID, Device Secret i region)"
                                          : "Tuya odrzuciła połączenie, kod " + String(rc);
        } else {
            _lastErr = "błąd połączenia TLS/TCP z " + host(_cfg.region) + " (esp_err 0x" + String(ev->error_handle->esp_tls_last_esp_err, HEX) + ")";
        }
        _connected = false;
        break;
    }
    case MQTT_EVENT_DATA: {
        JsonDocument d;
        if (!deserializeJson(d, ev->data, ev->data_len)) {
            std::lock_guard<std::mutex> l(_mutex);
            _lastCode = d["code"] | 0;
            if (_lastCode == 0) {
                _acked++;
                time(&_lastOk);
            }
        }
        break;
    }
    default:
        break;
    }
}

void FusionTuyaClass::loop()
{
    FusionTuyaConfig c;
    bool restart;
    {
        std::lock_guard<std::mutex> l(_mutex);
        c = _cfg;
        restart = _restart;
        _restart = false;
    }
    if (restart) disconnect();
    const bool ready = c.enabled && c.deviceId.length() > 4 && c.secret.length() > 4;
    if (!ready) {
        disconnect();
        return;
    }
    if (time(nullptr) < 1700000000) {
        std::lock_guard<std::mutex> l(_mutex);
        _lastErr = "czekam na czas z NTP";
        return;
    }
    const uint32_t now = millis();
    if (!_connected) {
        if (_client && now - _lastTry < 20000) return; // still connecting
        if (now - _lastTry < _retry) return;
        disconnect();
        _lastTry = now;
        _retry = min<uint32_t>(_retry * 2, 300000);
        connect();
        return;
    }
    if (_forceReport || now - _lastReport >= c.interval * 1000UL) {
        _forceReport = false;
        _lastReport = now;
        report();
    }
}

void FusionTuyaClass::report()
{
    if (!_client) return;
    const SolarEdgeData se = SolarEdge.getData();
    const float hoyW = Datastore.getIsAtLeastOneReachable() ? Datastore.getTotalAcPowerEnabled() : 0;
    const float seW = se.valid ? se.acPower : 0;
    const EnergySummary s = EnergyHistory.getSummary();

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    const uint64_t ms = (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;

    JsonDocument d;
    d["msgId"] = String(_sent + 1) + "-" + String((uint32_t)tv.tv_sec);
    d["time"] = ms;
    d["sys"]["ack"] = 1;
    JsonObject data = d["data"].to<JsonObject>();
    auto put = [&](const char* k, long v) {
        JsonObject o = data[k].to<JsonObject>();
        o["value"] = v;
        o["time"] = ms;
    };
    put("pv_power", lroundf(hoyW + seW));
    put("hoymiles_power", lroundf(hoyW));
    put("solaredge_power", lroundf(seW));
    if (se.valid && se.meterValid) put("grid_power", lroundf(se.meterPower));
    if (s.valid) {
        put("energy_today", lroundf((max(0.0f, s.today.hoy) + max(0.0f, s.today.se)) * 1000.0f));
        put("energy_month", lroundf(max(0.0f, s.month.hoy) + max(0.0f, s.month.se)));
        const float lt = max(0.0f, s.lifetime.hoy) + max(0.0f, s.lifetime.se);
        if (lt > 0) put("energy_total", lroundf(lt));
    }
    String out;
    serializeJson(d, out);
    const int r = esp_mqtt_client_enqueue((esp_mqtt_client_handle_t)_client, _topicReport.c_str(), out.c_str(), out.length(), 1, 0, true);
    std::lock_guard<std::mutex> l(_mutex);
    if (r >= 0) {
        _sent++;
        _lastPayload = out;
    } else {
        _lastErr = "nie udało się wysłać raportu";
    }
}

void FusionTuyaClass::statusToJson(JsonVariant root)
{
    std::lock_guard<std::mutex> l(_mutex);
    root["enabled"] = _cfg.enabled;
    root["region"] = _cfg.region;
    root["host"] = host(_cfg.region);
    root["device_id"] = _cfg.deviceId;
    root["has_secret"] = _cfg.secret.length() > 0;
    root["interval"] = _cfg.interval;
    root["connected"] = (bool)_connected;
    root["sent"] = _sent;
    root["acked"] = _acked;
    root["last_code"] = _lastCode;
    root["last_ok"] = (uint32_t)_lastOk;
    root["error"] = _lastErr;
    root["payload"] = _lastPayload;
}
