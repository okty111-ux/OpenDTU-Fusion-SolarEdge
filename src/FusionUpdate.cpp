// SPDX-License-Identifier: GPL-2.0-or-later
#include "FusionUpdate.h"
#include "FusionVersion.h"
#include <esp_log.h>
#include "RestartHelper.h"
#include <LittleFS.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

#undef TAG
static const char* TAG = "fusionupd";

FusionUpdateClass FusionUpdate;

#define FU_CFG_FILE "/fusion_update.json"
#define FU_FIRST_CHECK_MS (2UL * 60UL * 1000UL)
#define FU_CHECK_EVERY_MS (24UL * 3600UL * 1000UL)
#define FU_UA "OpenDTU-Fusion/" STR(FUSION_VERSION)
#define STR_(x) #x
#define STR(x) STR_(x)

static const char* stateName(FuState s)
{
    switch (s) {
    case FuState::Checking: return "checking";
    case FuState::UpToDate: return "uptodate";
    case FuState::Available: return "available";
    case FuState::Downloading: return "downloading";
    case FuState::Verifying: return "verifying";
    case FuState::Done: return "done";
    case FuState::Error: return "error";
    default: return "idle";
    }
}

// ---------- small esp_http_client helpers (TLS via the IDF certificate bundle)
static esp_http_client_handle_t openUrl(const String& url, int& status, int64_t& len, const char* accept)
{
    String cur = url;
    for (int hop = 0; hop < 6; hop++) {
        esp_http_client_config_t cfg = {};
        cfg.url = cur.c_str();
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.timeout_ms = 15000;
        cfg.buffer_size = 2048;
        cfg.buffer_size_tx = 1024;
        cfg.disable_auto_redirect = true;
        cfg.user_agent = "OpenDTU-Fusion";
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        if (!c) return nullptr;
        if (accept) esp_http_client_set_header(c, "Accept", accept);
        if (esp_http_client_open(c, 0) != ESP_OK) {
            esp_http_client_cleanup(c);
            status = -1;
            return nullptr;
        }
        len = esp_http_client_fetch_headers(c);
        status = esp_http_client_get_status_code(c);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            char* loc = nullptr;
            esp_http_client_get_header(c, "Location", &loc);
            String next = loc ? String(loc) : String();
            esp_http_client_close(c);
            esp_http_client_cleanup(c);
            if (next.isEmpty()) {
                status = -2;
                return nullptr;
            }
            cur = next;
            continue;
        }
        return c;
    }
    status = -3;
    return nullptr;
}

static void closeUrl(esp_http_client_handle_t c)
{
    if (!c) return;
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
}

// Stream adapter so ArduinoJson can parse the response without buffering it
struct HttpStream : public Stream {
    esp_http_client_handle_t c;
    uint8_t buf[512];
    int pos = 0, len = 0;
    bool eof = false;
    explicit HttpStream(esp_http_client_handle_t h) : c(h) {}
    bool fill()
    {
        if (pos < len) return true;
        if (eof) return false;
        len = esp_http_client_read(c, (char*)buf, sizeof(buf));
        pos = 0;
        if (len <= 0) {
            eof = true;
            len = 0;
            return false;
        }
        return true;
    }
    int available() override { return fill() ? len - pos : 0; }
    int read() override { return fill() ? buf[pos++] : -1; }
    int peek() override { return fill() ? buf[pos] : -1; }
    size_t readBytes(char* b, size_t n) override
    {
        size_t got = 0;
        while (got < n && fill()) {
            size_t k = min((size_t)(len - pos), n - got);
            memcpy(b + got, buf + pos, k);
            pos += k;
            got += k;
        }
        return got;
    }
    size_t write(uint8_t) override { return 0; }
    void flush() override {}
};

// ---------------------------------------------------------------- config
void FusionUpdateClass::loadConfig()
{
    _cfg.repo = FUSION_UPDATE_REPO;
    _cfg.prefix = FUSION_TAG_PREFIX;
    _cfg.autoCheck = true;
    File f = LittleFS.open(FU_CFG_FILE, "r");
    if (!f) return;
    JsonDocument d;
    if (!deserializeJson(d, f)) {
        if (d["repo"].is<const char*>() && strlen(d["repo"]) > 2) _cfg.repo = d["repo"].as<String>();
        if (d["prefix"].is<const char*>()) _cfg.prefix = d["prefix"].as<String>();
        _cfg.autoCheck = d["auto"] | true;
    }
    f.close();
}

FusionUpdateConfig FusionUpdateClass::getConfig()
{
    std::lock_guard<std::mutex> l(_mutex);
    return _cfg;
}

bool FusionUpdateClass::setConfig(const FusionUpdateConfig& c)
{
    FusionUpdateConfig n = c;
    n.repo.trim();
    n.prefix.trim();
    if (n.repo.startsWith("https://github.com/")) n.repo = n.repo.substring(19);
    while (n.repo.endsWith("/")) n.repo.remove(n.repo.length() - 1);
    if (n.repo.endsWith(".git")) n.repo.remove(n.repo.length() - 4);
    if (n.repo.indexOf('/') < 1 || n.repo.length() > 100 || n.prefix.length() > 24) return false;
    {
        std::lock_guard<std::mutex> l(_mutex);
        _cfg = n;
    }
    File f = LittleFS.open(FU_CFG_FILE, "w");
    if (!f) return false;
    JsonDocument d;
    d["repo"] = n.repo;
    d["prefix"] = n.prefix;
    d["auto"] = n.autoCheck;
    serializeJson(d, f);
    f.close();
    return true;
}

// ---------------------------------------------------------------- lifecycle
void FusionUpdateClass::init(Scheduler& scheduler)
{
    loadConfig();
    _bootMs = millis();
    scheduler.addTask(_loopTask);
    _loopTask.setCallback(std::bind(&FusionUpdateClass::loop, this));
    _loopTask.setIterations(TASK_FOREVER);
    _loopTask.setInterval(10 * TASK_SECOND);
    _loopTask.enable();
    ESP_LOGI(TAG, "FusionUpdate: version %d, repo %s, asset %s", FUSION_VERSION, _cfg.repo.c_str(), FUSION_ASSET_NAME);
}

void FusionUpdateClass::loop()
{
    if (_busy || !_cfg.autoCheck) return;
    const uint32_t now = millis();
    const bool due = !_checkedOnce ? (now - _bootMs > FU_FIRST_CHECK_MS) : (now - _lastCheckMs > FU_CHECK_EVERY_MS);
    if (due) requestCheck();
}

bool FusionUpdateClass::requestCheck()
{
    if (_busy) return false;
    _busy = true;
    _job = 1;
    _checkedOnce = true;
    _lastCheckMs = millis();
    setState(FuState::Checking, "");
    if (xTaskCreatePinnedToCore(taskEntry, "fusion_upd", 10240, this, 1, nullptr, 0) != pdPASS) {
        _busy = false;
        setState(FuState::Error, "brak pamięci na zadanie");
        return false;
    }
    return true;
}

bool FusionUpdateClass::requestInstall()
{
    if (_busy) return false;
    {
        std::lock_guard<std::mutex> l(_mutex);
        if (_binUrl.isEmpty() || _latest <= 0) return false;
    }
    _busy = true;
    _job = 2;
    setState(FuState::Downloading, "");
    if (xTaskCreatePinnedToCore(taskEntry, "fusion_upd", 10240, this, 1, nullptr, 0) != pdPASS) {
        _busy = false;
        setState(FuState::Error, "brak pamięci na zadanie");
        return false;
    }
    return true;
}

bool FusionUpdateClass::updateAvailable()
{
    std::lock_guard<std::mutex> l(_mutex);
    return _latest > FUSION_VERSION && !_binUrl.isEmpty();
}

void FusionUpdateClass::taskEntry(void* arg)
{
    auto* self = static_cast<FusionUpdateClass*>(arg);
    if (self->_job == 1) self->doCheck();
    else if (self->_job == 2) self->doInstall();
    self->_busy = false;
    vTaskDelete(nullptr);
}

void FusionUpdateClass::setState(FuState s, const String& msg)
{
    std::lock_guard<std::mutex> l(_mutex);
    _state = s;
    _msg = msg;
}

// ---------------------------------------------------------------- check
void FusionUpdateClass::doCheck()
{
    const FusionUpdateConfig cfg = getConfig();
    const String url = "https://api.github.com/repos/" + cfg.repo + "/releases?per_page=20";
    int status = 0;
    int64_t len = 0;
    esp_http_client_handle_t c = openUrl(url, status, len, "application/vnd.github+json");
    if (!c) {
        setState(FuState::Error, "brak połączenia z GitHub (sprawdź internet i DNS)");
        return;
    }
    if (status != 200) {
        closeUrl(c);
        setState(FuState::Error, status == 404 ? "nie znaleziono repozytorium " + cfg.repo + " (musi być publiczne)"
                                               : (status == 403 ? String("limit zapytań GitHub, spróbuj za godzinę") : "GitHub odpowiedział kodem " + String(status)));
        return;
    }

    JsonDocument filter;
    JsonObject fr = filter.add<JsonObject>();
    fr["tag_name"] = true;
    fr["name"] = true;
    fr["body"] = true;
    fr["draft"] = true;
    fr["published_at"] = true;
    fr["html_url"] = true;
    JsonObject fa = fr["assets"].add<JsonObject>();
    fa["name"] = true;
    fa["size"] = true;
    fa["browser_download_url"] = true;

    JsonDocument doc;
    HttpStream st(c);
    const DeserializationError err = deserializeJson(doc, st, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(12));
    closeUrl(c);
    if (err) {
        setState(FuState::Error, String("błąd odczytu listy wydań: ") + err.c_str());
        return;
    }

    int best = 0;
    JsonObject rel;
    for (JsonObject r : doc.as<JsonArray>()) {
        if (r["draft"] | false) continue;
        const String tag = r["tag_name"] | "";
        if (!tag.startsWith(cfg.prefix)) continue;
        const int v = tag.substring(cfg.prefix.length()).toInt();
        if (v > best) {
            // must contain the firmware for this board
            bool has = false;
            for (JsonObject a : r["assets"].as<JsonArray>())
                if (strcmp(a["name"] | "", FUSION_ASSET_NAME) == 0) has = true;
            if (!has) continue;
            best = v;
            rel = r;
        }
    }

    std::lock_guard<std::mutex> l(_mutex);
    time(&_lastCheck);
    if (best == 0) {
        _latest = 0;
        _binUrl = "";
        _state = FuState::Error;
        _msg = "brak wydań " + cfg.prefix + "N z plikiem " FUSION_ASSET_NAME;
        return;
    }
    _latest = best;
    _tag = rel["tag_name"].as<String>();
    _name = rel["name"] | _tag.c_str();
    _date = String(rel["published_at"] | "").substring(0, 10);
    _notes = rel["body"] | "";
    if (_notes.length() > 3000) _notes = _notes.substring(0, 3000) + "…";
    _pageUrl = rel["html_url"] | "";
    _binUrl = "";
    _shaUrl = "";
    _binSize = 0;
    for (JsonObject a : rel["assets"].as<JsonArray>()) {
        const char* n = a["name"] | "";
        if (strcmp(n, FUSION_ASSET_NAME) == 0) {
            _binUrl = a["browser_download_url"].as<String>();
            _binSize = a["size"] | 0;
        } else if (strcmp(n, "SHA256SUMS.txt") == 0) {
            _shaUrl = a["browser_download_url"].as<String>();
        }
    }
    _state = best > FUSION_VERSION ? FuState::Available : FuState::UpToDate;
    _msg = "";
    ESP_LOGI(TAG, "FusionUpdate: newest %s (%d), running %d", _tag.c_str(), best, FUSION_VERSION);
}

bool FusionUpdateClass::fetchSha(const String& url, String& sha)
{
    int status = 0;
    int64_t len = 0;
    esp_http_client_handle_t c = openUrl(url, status, len, nullptr);
    if (!c) return false;
    if (status != 200) {
        closeUrl(c);
        return false;
    }
    String txt;
    char b[256];
    int n;
    while ((n = esp_http_client_read(c, b, sizeof(b))) > 0 && txt.length() < 8192) txt.concat(b, n);
    closeUrl(c);
    // lines: "<64 hex>  <file name>"
    int p = 0;
    while (p < (int)txt.length()) {
        int e = txt.indexOf('\n', p);
        if (e < 0) e = txt.length();
        String line = txt.substring(p, e);
        line.trim();
        if (line.length() > 66 && line.endsWith(FUSION_ASSET_NAME)) {
            String name = line.substring(64);
            name.trim();
            if (name.startsWith("*")) name.remove(0, 1);
            if (name == FUSION_ASSET_NAME) {
                sha = line.substring(0, 64);
                sha.toLowerCase();
                return true;
            }
        }
        p = e + 1;
    }
    return false;
}

// ---------------------------------------------------------------- install
void FusionUpdateClass::doInstall()
{
    String binUrl, shaUrl;
    uint32_t size;
    {
        std::lock_guard<std::mutex> l(_mutex);
        binUrl = _binUrl;
        shaUrl = _shaUrl;
        size = _binSize;
        _done = 0;
        _total = size;
    }
    String want;
    if (shaUrl.isEmpty() || !fetchSha(shaUrl, want)) {
        setState(FuState::Error, "brak sumy kontrolnej SHA256SUMS.txt w wydaniu - aktualizacja przerwana");
        return;
    }

    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    if (!part) {
        setState(FuState::Error, "brak partycji OTA");
        return;
    }
    int status = 0;
    int64_t len = 0;
    esp_http_client_handle_t c = openUrl(binUrl, status, len, "application/octet-stream");
    if (!c || status != 200) {
        closeUrl(c);
        setState(FuState::Error, "nie można pobrać pliku (kod " + String(status) + ")");
        return;
    }
    if (len > 0) size = (uint32_t)len;
    if (size > part->size) {
        closeUrl(c);
        setState(FuState::Error, "plik większy niż partycja OTA");
        return;
    }
    {
        std::lock_guard<std::mutex> l(_mutex);
        _total = size;
    }
    esp_ota_handle_t ota = 0;
    if (esp_ota_begin(part, size ? size : OTA_SIZE_UNKNOWN, &ota) != ESP_OK) {
        closeUrl(c);
        setState(FuState::Error, "esp_ota_begin nie powiódł się");
        return;
    }
    mbedtls_sha256_context sh;
    mbedtls_sha256_init(&sh);
    mbedtls_sha256_starts(&sh, 0);
    uint8_t* buf = (uint8_t*)malloc(4096);
    bool ok = buf != nullptr;
    uint32_t got = 0;
    while (ok) {
        const int n = esp_http_client_read(c, (char*)buf, 4096);
        if (n < 0) {
            ok = false;
            break;
        }
        if (n == 0) break;
        mbedtls_sha256_update(&sh, buf, n);
        if (esp_ota_write(ota, buf, n) != ESP_OK) {
            ok = false;
            break;
        }
        got += n;
        std::lock_guard<std::mutex> l(_mutex);
        _done = got;
    }
    free(buf);
    closeUrl(c);
    uint8_t dig[32];
    mbedtls_sha256_finish(&sh, dig);
    mbedtls_sha256_free(&sh);
    if (!ok || got == 0 || (size && got != size)) {
        esp_ota_abort(ota);
        setState(FuState::Error, "przerwane pobieranie (" + String(got) + " z " + String(size) + " B)");
        return;
    }
    setState(FuState::Verifying, "");
    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", dig[i]);
    if (want != hex) {
        esp_ota_abort(ota);
        setState(FuState::Error, "suma SHA-256 nie zgadza się - plik odrzucony, obecny firmware bez zmian");
        return;
    }
    if (esp_ota_end(ota) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {
        setState(FuState::Error, "zapis/weryfikacja obrazu nie powiodła się");
        return;
    }
    setState(FuState::Done, "zainstalowano, ponowne uruchamianie");
    ESP_LOGI(TAG, "FusionUpdate: installed, restarting");
    vTaskDelay(pdMS_TO_TICKS(1500));
    RestartHelper.triggerRestart();
}

// ---------------------------------------------------------------- json
void FusionUpdateClass::toJson(JsonVariant root)
{
    std::lock_guard<std::mutex> l(_mutex);
    root["version"] = FUSION_VERSION;
    root["tag"] = String(_cfg.prefix) + FUSION_VERSION;
    root["env"] = PIOENV;
    root["asset"] = FUSION_ASSET_NAME;
    root["built"] = __DATE__ " " __TIME__;
    root["repo"] = _cfg.repo;
    root["prefix"] = _cfg.prefix;
    root["auto"] = _cfg.autoCheck;
    root["state"] = stateName(_state);
    root["message"] = _msg;
    root["busy"] = (bool)_busy;
    root["last_check"] = (uint32_t)_lastCheck;
    root["available"] = _latest > FUSION_VERSION && !_binUrl.isEmpty();
    root["done"] = _done;
    root["total"] = _total;
    if (_latest > 0) {
        JsonObject r = root["latest"].to<JsonObject>();
        r["version"] = _latest;
        r["tag"] = _tag;
        r["name"] = _name;
        r["date"] = _date;
        r["notes"] = _notes;
        r["url"] = _pageUrl;
        r["size"] = _binSize;
        r["has_sha"] = !_shaUrl.isEmpty();
    }
}
