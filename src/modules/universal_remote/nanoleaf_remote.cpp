#include "nanoleaf_remote.h"
#include "nanoleaf_pairing.h"
#include "remote_menu.h"
#include "core/mykeyboard.h"
#include "core/wifi/wifi_common.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

static String nlIp, nlToken, nlId, nlBackup;
static uint16_t nlPort = 16021;
static bool validIp(const String &s) {
    IPAddress ip;
    return ip.fromString(s) && ip[0] > 0 && ip[0] < 224 && ip[0] != 127 && ip != IPAddress(255,255,255,255);
}
static bool validToken(const String &s) {
    if (s.length() < 1 || s.length() > 128) return false;
    for (size_t i = 0; i < s.length(); ++i) if (!isalnum(static_cast<unsigned char>(s[i])) && s[i] != '-' && s[i] != '_') return false;
    return true;
}
static void loadNanoleaf() {
    Preferences p;
    nlIp = nlToken = nlId = nlBackup = ""; nlPort = 16021;
    if (!p.begin("nanoleaf", true)) return;
    String data = p.getString("configV1", "");
    if (!data.isEmpty()) {
        JsonDocument doc;
        if (!deserializeJson(doc, data) && doc["version"].as<int>() == 1) {
            nlIp = doc["ip"].as<String>(); nlToken = doc["token"].as<String>();
            nlId = doc["id"].as<String>(); nlPort = doc["port"] | 16021;
            nlBackup = doc["pairedToken"].as<String>();
        }
    } else {
        nlIp = p.getString("ip", ""); nlToken = p.getString("token", "");
    }
    p.end();
    if (!validIp(nlIp) || !nlPort) { nlIp = nlToken = nlId = ""; nlPort = 16021; }
    if (!nlToken.isEmpty() && !validToken(nlToken)) nlToken = "";
    if (!validToken(nlBackup)) nlBackup = "";
}
static bool saveNanoleaf(bool showError = true) {
    if (validToken(nlToken)) nlBackup = nlToken;
    JsonDocument doc;
    doc["version"] = 1; doc["ip"] = nlIp; doc["token"] = nlToken; doc["id"] = nlId; doc["port"] = nlPort;
    doc["pairedToken"] = nlBackup;
    String data; serializeJson(doc, data);
    Preferences p;
    if (!p.begin("nanoleaf", false)) { if (showError) displayError("Cannot open settings", true); return false; }
    bool ok = p.putString("configV1", data) == data.length();
    p.end();
    if (ok) {
        ok = p.begin("nanoleaf", true);
        if (ok) { ok = p.getString("configV1", "") == data; p.end(); }
    }
    if (!ok) { loadNanoleaf(); if (showError) displayError("Nanoleaf save failed", true); }
    return ok;
}
static bool ensureWifi() {
    if (WiFi.isConnected()) return true;
    displayInfo("WLAN: gespeicherte Verbindung wird gesucht...");
    if (wifiConnecttoKnownNet() && WiFi.isConnected()) return true;
    wifiConnectMenu(WIFI_STA);
    return WiFi.isConnected();
}
static String baseUrl() { return "http://" + nlIp + ":" + String(nlPort) + "/api/v1/"; }
static bool beginHttp(HTTPClient &http, const String &url) {
    http.setConnectTimeout(1000); http.setTimeout(1500);
    return http.begin(url);
}
// Read-only authentication check; bound both known-length and chunked responses.
class NanoleafResponse : public Stream {
public:
    String body;
    size_t write(uint8_t v) override { return write(&v, 1); }
    size_t write(const uint8_t *data, size_t len) override {
        if (len > 4096 - body.length()) return 0;
        return body.concat(reinterpret_cast<const char *>(data), len) ? len : 0;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};
static bool acceptsToken(const String &ip, uint16_t port, const String &token) {
    if (!validIp(ip) || !port || !validToken(token) || !WiFi.isConnected()) return false;
    HTTPClient http;
    if (!beginHttp(http, "http://" + ip + ":" + String(port) + "/api/v1/" + token + "/state")) return false;
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    int code = http.GET();
    NanoleafResponse response;
    int received = code == 200 && http.getSize() <= 4096 ? http.writeToStream(&response) : -1;
    http.end();
    JsonDocument doc;
    return received >= 0 && !deserializeJson(doc, response.body) && doc["on"]["value"].is<bool>();
}
// Never discard the last paired key while selecting an unpaired LAN candidate.
static void selectAddress(const String &ip, uint16_t port, const String &id) {
    bool same = ip == nlIp && port == nlPort && (id.isEmpty() || nlId.isEmpty() || id == nlId);
    String token = nanoleafSelectToken(nlToken, nlBackup, same, [&](const String &key) {
        return acceptsToken(ip, port, key);
    });
    nlIp = ip; nlPort = port; nlId = id; nlToken = token;
}
String nanoleafStatus() {
    loadNanoleaf();
    return "IP " + nlIp + ":" + String(nlPort) + " Token " + (nlToken.isEmpty() ? "missing" : "saved");
}
bool nanoleafRestore(const String &data) {
    if (data.length() > 1024) return false;
    JsonDocument doc;
    if (deserializeJson(doc, data) || doc["version"].as<int>() != 1) return false;
    String ip = doc["ip"].as<String>(), token = doc["token"].as<String>();
    int port = doc["port"] | 16021;
    if (port < 1 || port > 65535 || !acceptsToken(ip, port, token)) return false;
    loadNanoleaf();
    nlIp = ip; nlPort = port; nlToken = token; nlId = "";
    return saveNanoleaf(false);
}
static bool nlPut(const String &endpoint, const String &json) {
    loadNanoleaf();
    if (nlIp.isEmpty() || nlToken.isEmpty()) { displayWarning("Use Auto Find / Auto Pair", true); return false; }
    if (!ensureWifi()) return false;
    HTTPClient http;
    if (!beginHttp(http, baseUrl() + nlToken + "/" + endpoint)) return false;
    http.addHeader("Content-Type", "application/json");
    int code = http.PUT(json); http.end();
    if (code >= 200 && code < 300) { displaySuccess("Nanoleaf OK"); return true; }
    displayError(code == 401 || code == 403 ? "Token rejected: pair again" : "Nanoleaf HTTP " + String(code), true);
    return false;
}
struct Candidate { String ip, id, label; uint16_t port; };
static bool chooseCandidate(const std::vector<Candidate> &found, const char *title) {
    if (found.empty()) return false;
    int chosen = -1;
    std::vector<Option> opts;
    for (size_t i = 0; i < found.size(); ++i)
        opts.push_back({found[i].label, [&, i]() { chosen = i; }});
    opts.push_back({"Cancel", []() {}});
    loopOptions(opts, MENU_TYPE_SUBMENU, title);
    if (chosen < 0) return false;
    const Candidate &c = found[chosen];
    selectAddress(c.ip, c.port, c.id);
    if (!saveNanoleaf()) return false;
    displaySuccess(nlToken.isEmpty() ? "IP saved; pairing needed" : "Connection saved + paired", true);
    return true;
}
static uint32_t ipNumber(const IPAddress &ip) {
    return (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) | (uint32_t(ip[2]) << 8) | ip[3];
}
static IPAddress numberIp(uint32_t ip) { return IPAddress(ip >> 24, ip >> 16, ip >> 8, ip); }
static bool discoverNanoleaf() {
    loadNanoleaf();
    if (!ensureWifi()) return false;
    displayInfo("Searching Nanoleaf mDNS...");
    std::vector<Candidate> found;
    // Reuse a running responder. A failed mDNS start must not prevent the LAN fallback.
    char hostname[MDNS_NAME_BUF_LEN] = {};
    bool mdnsReady = mdns_hostname_get(hostname) == ESP_OK || MDNS.begin("bruce-nanoleaf");
    int n = mdnsReady ? MDNS.queryService("nanoleafapi", "tcp") : 0;
    for (int i = 0; i < n && found.size() < 16; ++i) {
        String ip = MDNS.address(i).toString();
        if (!validIp(ip) || !MDNS.port(i)) continue;
        String id = MDNS.txt(i, "id");
        found.push_back({ip, id, MDNS.hostname(i) + " " + ip, MDNS.port(i)});
    }
    if (!found.empty()) return chooseCandidate(found, "Select Nanoleaf");
    if (check(EscPress)) return false;
    // Scan the actual subnet. Large networks are bounded to the local /22 intersection.
    uint32_t local = ipNumber(WiFi.localIP()), mask = ipNumber(WiFi.subnetMask());
    if (!mask || mask == 0xffffffffu) { displayWarning("Use manual IP on this network", true); return false; }
    uint32_t first = (local & mask) + 1, last = (local | ~mask) - 1;
    bool bounded = last >= first && last - first > 1022;
    if (bounded) { first = max(first, (local & 0xfffffc00u) + 1); last = min(last, (local | 1023u) - 1); }
    displayInfo(bounded ? "LAN: local /22 only\nESC cancels" : "LAN discovery\nESC cancels");
    for (uint32_t host = first; host <= last && found.size() < 16; ++host) {
        if (check(EscPress) || !WiFi.isConnected()) return false;
        if (host == local) continue;
        WiFiClient client;
        if (client.connect(numberIp(host), 16021, 80)) {
            String ip = numberIp(host).toString();
            // An open port is only a candidate, not positive device identification.
            found.push_back({ip, "", "Candidate " + ip, 16021});
        }
        client.stop();
        if ((host & 15) == 0) displayInfo("LAN " + String(host - first + 1) + "/" + String(last - first + 1));
        delay(1);
    }
    if (found.empty()) { displayWarning("No API candidate\nTry manual IP", true); return false; }
    return chooseCandidate(found, "Port 16021 candidates");
}
static void pairNanoleaf() {
    loadNanoleaf();
    if (nlIp.isEmpty() && !discoverNanoleaf()) return;
    if (!ensureWifi()) return;
    displayInfo("Hold Nanoleaf power 5-7 sec\nPress OK when LED flashes\nESC cancels");
    delay(500);
    while (!check(SelPress)) { if (check(EscPress)) return; delay(20); }
    uint32_t start = millis();
    int code = 0;
    while (millis() - start < 30000 && WiFi.isConnected()) {
        if (check(EscPress)) return;
        HTTPClient http;
        if (!beginHttp(http, baseUrl() + "new")) break;
        http.addHeader("Content-Type", "application/json");
        code = http.POST("{}");
        String body = http.getString(); http.end();
        if (code >= 200 && code < 300) {
            JsonDocument doc;
            if (deserializeJson(doc, body) || !doc["auth_token"].is<String>() ||
                !validToken(doc["auth_token"].as<String>())) {
                displayError("Invalid pairing response", true); return;
            }
            nlToken = doc["auth_token"].as<String>();
            if (saveNanoleaf()) displaySuccess("Nanoleaf paired + saved", true);
            return;
        }
        if (code > 0 && code != 401 && code != 403) break;
        displayInfo("Waiting for pairing mode\nESC cancels");
        for (int i = 0; i < 10; ++i) { if (check(EscPress)) return; delay(100); }
    }
    displayError("Pair failed HTTP " + String(code) + "\nCheck IP / pairing mode", true);
}
static void setupNanoleaf() {
    loadNanoleaf();
    String ip = keyboard(nlIp, 15, "Nanoleaf IPv4:");
    if (ip.isEmpty() || ip == "\x1B") return;
    if (!validIp(ip)) { displayError("Invalid IPv4 address", true); return; }
    bool same = ip == nlIp;
    String token = keyboard(same ? nlToken : "", 128, "Token (empty = keep key):", true);
    if (token == "\x1B") return;
    if (!token.isEmpty() && !validToken(token)) { displayError("Invalid token", true); return; }
    if (ip != nlIp && !ensureWifi()) return;
    selectAddress(ip, 16021, same ? nlId : "");
    if (!token.isEmpty()) nlToken = token;
    if (saveNanoleaf()) displaySuccess("Nanoleaf saved", true);
}
static void setState(const char *key, int value) {
    JsonDocument doc; doc[key]["value"] = value;
    String body; serializeJson(doc, body); nlPut("state", body);
}
static void setColor(int hue) {
    nlPut("state", "{\"hue\":{\"value\":" + String(hue) + "},\"sat\":{\"value\":100}}");
}
static void setEffect() {
    String effect = keyboard("", 80, "Nanoleaf scene/effect:");
    if (effect.isEmpty()) return;
    JsonDocument doc; doc["select"] = effect;
    String body; serializeJson(doc, body); nlPut("effects", body);
}
void nanoleafMenu() {
    loadNanoleaf();
    std::vector<Option> opts = {
        {"Auto Find", []() { discoverNanoleaf(); }},
        {"Auto Pair + Save", pairNanoleaf},
        {"Saved connection", []() { displayInfo(nanoleafStatus(), true); }},
        {"Power ON", []() { nlPut("state", "{\"on\":{\"value\":true}}"); }},
        {"Power OFF", []() { nlPut("state", "{\"on\":{\"value\":false}}"); }},
        {"Brightness 25%", []() { setState("brightness", 25); }},
        {"Brightness 50%", []() { setState("brightness", 50); }},
        {"Brightness 100%", []() { setState("brightness", 100); }},
        {"Red", []() { setColor(0); }},
        {"Green", []() { setColor(120); }},
        {"Blue", []() { setColor(240); }},
        {"Warm White", []() { setState("ct", 2700); }},
        {"Cool White", []() { setState("ct", 6500); }},
        {"Scene / Effect", setEffect},
        {"Setup IP + Token", setupNanoleaf},
    };
    remoteMenu(opts, "Nanoleaf");
}
bool nanoleafPowerOff() { return nlPut("state", "{\"on\":{\"value\":false}}"); }
