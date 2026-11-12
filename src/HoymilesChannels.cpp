// SPDX-License-Identifier: GPL-2.0-or-later
#include "HoymilesChannels.h"
#include "Configuration.h"
#include <Hoymiles.h>
#include <LittleFS.h>

#undef TAG
static const char* TAG = "mppt";
static const char* CFG_FILE = "/mppt.json";

HoymilesChannelsClass HoymilesChannels;

uint8_t MpptCfg::count() const
{
    uint8_t n = 0;
    for (auto& p : panel) if (p.wp > 0) n++;
    return n;
}

void HoymilesChannelsClass::init() { load(); }

void HoymilesChannelsClass::load()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _cfg.clear();
    File f = LittleFS.open(CFG_FILE, "r");
    if (!f) { _cfg.to<JsonObject>(); return; }
    if (deserializeJson(_cfg, f)) { ESP_LOGW(TAG, "bad %s", CFG_FILE); _cfg.to<JsonObject>(); }
    f.close();
}

bool HoymilesChannelsClass::save()
{
    File f = LittleFS.open("/mppt.tmp", "w");
    if (!f) return false;
    const bool ok = serializeJson(_cfg, f) > 0;
    f.close();
    if (!ok) return false;
    LittleFS.remove(CFG_FILE);
    return LittleFS.rename("/mppt.tmp", CFG_FILE);
}

MpptCfg HoymilesChannelsClass::getCfg(const String& serial, uint8_t ch)
{
    MpptCfg c;
    std::lock_guard<std::mutex> lock(_mutex);
    JsonVariantConst e = _cfg[serial][ch];
    if (e.isNull()) return c;
    c.series = e["series"] | true;
    JsonArrayConst ps = e["panels"];
    for (uint8_t k = 0; k < MPPT_MAX_PANELS && k < ps.size(); k++) {
        c.panel[k].name = ps[k]["name"] | "";
        c.panel[k].wp = ps[k]["wp"] | 0;
        c.panel[k].vmp = ps[k]["vmp"] | 0.0f;
    }
    return c;
}

static String serialStr(uint64_t s)
{
    char b[17];
    snprintf(b, sizeof(b), "%0x%08x", (uint32_t)(s >> 32), (uint32_t)(s & 0xFFFFFFFF));
    return String(b);
}

std::vector<MpptReading> HoymilesChannelsClass::read()
{
    std::vector<MpptReading> out;
    for (uint8_t i = 0; i < Hoymiles.getNumInverters(); i++) {
        auto inv = Hoymiles.getInverterByPos(i);
        if (!inv) continue;
        auto cfg = Configuration.getInverterConfig(inv->serial());
        auto stats = inv->Statistics();
        const String ser = serialStr(inv->serial());
        for (auto& c : stats->getChannelsByType(TYPE_DC)) {
            MpptReading r;
            r.inverter = inv->name();
            r.serial = ser;
            r.invIndex = i + 1;
            r.channel = static_cast<uint8_t>(c) + 1;
            r.name = cfg ? String(cfg->channel[c].Name) : String();
            r.reachable = inv->isReachable();
            r.u = stats->getChannelFieldValue(TYPE_DC, c, FLD_UDC);
            r.i = stats->getChannelFieldValue(TYPE_DC, c, FLD_IDC);
            r.p = stats->getChannelFieldValue(TYPE_DC, c, FLD_PDC);
            r.yieldDay = stats->getChannelFieldValue(TYPE_DC, c, FLD_YD);
            r.yieldTotal = stats->getChannelFieldValue(TYPE_DC, c, FLD_YT);

            const MpptCfg mc = getCfg(ser, static_cast<uint8_t>(c));
            r.series = mc.series;
            const uint8_t n = mc.count();
            if (n > 0) {
                bool allVmp = true;
                float sumWp = 0, sumVmp = 0;
                for (auto& p : mc.panel) {
                    if (!p.wp) continue;
                    sumWp += p.wp;
                    sumVmp += p.vmp;
                    if (p.vmp <= 0) allVmp = false;
                }
                const bool useVmp = mc.series && allVmp && sumVmp > 0;
                uint8_t k = 0;
                for (auto& p : mc.panel) {
                    k++;
                    if (!p.wp) continue;
                    const float w = n == 1 ? 1.0f : (useVmp ? p.vmp / sumVmp : p.wp / sumWp);
                    PanelEstimate e;
                    e.name = p.name.length() ? p.name : String("Panel ") + k;
                    e.wp = p.wp;
                    e.p = r.p * w;
                    e.yieldDay = r.yieldDay * w;
                    if (mc.series) { e.u = r.u * w; e.i = r.i; }
                    else { e.u = r.u; e.i = r.i * w; }
                    r.panels.push_back(e);
                }
            }
            out.push_back(r);
        }
    }
    return out;
}

void HoymilesChannelsClass::toJson(JsonArray arr)
{
    for (auto& r : read()) {
        auto o = arr.add<JsonObject>();
        o["inv"] = r.inverter;
        o["serial"] = r.serial;
        o["inv_idx"] = r.invIndex;
        o["ch"] = r.channel;
        o["name"] = r.name;
        o["reachable"] = r.reachable;
        o["u"] = r.u;
        o["i"] = r.i;
        o["p"] = r.p;
        o["yd"] = r.yieldDay;
        o["yt"] = r.yieldTotal;
        o["series"] = r.series;
        auto ps = o["panels"].to<JsonArray>();
        for (auto& e : r.panels) {
            auto po = ps.add<JsonObject>();
            po["name"] = e.name;
            po["wp"] = e.wp;
            po["u"] = e.u;
            po["i"] = e.i;
            po["p"] = e.p;
            po["yd"] = e.yieldDay;
        }
    }
}

// GET: { "inverters":[{serial,name,channels:[{ch,name,series,panels:[{name,wp,vmp}]}]}] }
void HoymilesChannelsClass::configToJson(JsonObject root)
{
    auto invs = root["inverters"].to<JsonArray>();
    for (uint8_t i = 0; i < Hoymiles.getNumInverters(); i++) {
        auto inv = Hoymiles.getInverterByPos(i);
        if (!inv) continue;
        auto icfg = Configuration.getInverterConfig(inv->serial());
        const String ser = serialStr(inv->serial());
        auto io = invs.add<JsonObject>();
        io["serial"] = ser;
        io["name"] = inv->name();
        io["type"] = inv->typeName();
        auto chs = io["channels"].to<JsonArray>();
        for (auto& c : inv->Statistics()->getChannelsByType(TYPE_DC)) {
            const MpptCfg mc = getCfg(ser, static_cast<uint8_t>(c));
            auto co = chs.add<JsonObject>();
            co["ch"] = static_cast<uint8_t>(c) + 1;
            co["name"] = icfg ? String(icfg->channel[c].Name) : String();
            co["series"] = mc.series;
            auto ps = co["panels"].to<JsonArray>();
            for (auto& p : mc.panel) {
                auto po = ps.add<JsonObject>();
                po["name"] = p.name;
                po["wp"] = p.wp;
                po["vmp"] = p.vmp;
            }
        }
    }
}

// POST: { "<serial>": [ {series, panels:[{name,wp,vmp},{...}]}, ... ] }
bool HoymilesChannelsClass::configFromJson(JsonObjectConst root)
{
    std::lock_guard<std::mutex> lock(_mutex);
    JsonDocument n;
    auto no = n.to<JsonObject>();
    for (JsonPairConst kv : root) {
        JsonArrayConst chs = kv.value();
        if (chs.isNull() || chs.size() > 8) return false;
        auto a = no[kv.key()].to<JsonArray>();
        for (JsonVariantConst c : chs) {
            auto co = a.add<JsonObject>();
            co["series"] = c["series"] | true;
            auto ps = co["panels"].to<JsonArray>();
            JsonArrayConst ip = c["panels"];
            for (uint8_t k = 0; k < MPPT_MAX_PANELS; k++) {
                auto po = ps.add<JsonObject>();
                String nm = ip[k]["name"] | "";
                if (nm.length() > 24) nm = nm.substring(0, 24);
                po["name"] = nm;
                po["wp"] = constrain((int)(ip[k]["wp"] | 0), 0, 2000);
                po["vmp"] = constrain((float)(ip[k]["vmp"] | 0.0f), 0.0f, 140.0f);
            }
        }
    }
    _cfg = n;
    return save();
}
