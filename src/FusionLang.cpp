// SPDX-License-Identifier: GPL-2.0-or-later
#include "FusionLang.h"
#include "LangPackPl.h"
#include <LittleFS.h>
#include <esp_log.h>

#undef TAG
static const char* TAG = "fusionlang";
static const char* LANG_FILE = "/pl.lang.json";

void FusionLang::install()
{
    if (LittleFS.exists(LANG_FILE)) {
        File f = LittleFS.open(LANG_FILE, "r");
        char head[128] = { 0 };
        size_t n = f ? f.readBytes(head, sizeof(head) - 1) : 0;
        size_t sz = f ? f.size() : 0;
        if (f) {
            f.close();
        }
        if (n > 0 && strstr(head, "\"rev\":\"" FUSION_LANG_PL_REV "\"") && sz == strlen_P(FUSION_LANG_PL)) {
            return; // aktualna
        }
    }

    const size_t len = strlen_P(FUSION_LANG_PL);
    if (LittleFS.totalBytes() - LittleFS.usedBytes() < len + 8192) {
        ESP_LOGW(TAG, "Za malo miejsca na paczke jezykowa");
        return;
    }
    File f = LittleFS.open(LANG_FILE, "w");
    if (!f) {
        ESP_LOGE(TAG, "Nie mozna zapisac %s", LANG_FILE);
        return;
    }
    size_t w = 0;
    const size_t chunk = 1024;
    while (w < len) {
        size_t c = (len - w) < chunk ? (len - w) : chunk;
        const size_t k = f.write(reinterpret_cast<const uint8_t*>(FUSION_LANG_PL) + w, c);
        if (k == 0) {
            break;
        }
        w += k;
    }
    f.close();
    ESP_LOGI(TAG, "Zainstalowano polska paczke jezykowa (%u B)", static_cast<unsigned>(w));
}
