#include "pi_remote.h"
#include "pi_reply.h"
#include "remote_menu.h"
#include "core/mykeyboard.h"
#include "core/wifi/wifi_common.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>

// Same API and default address as the user's existing ESP32/Pi remote.
// This is the Pi address, not the TV address. No direct TV pairing is needed here.
static String piHost = "192.168.8.77";
static bool validHost(const String &value) {
    IPAddress ip;
    return ip.fromString(value) && ip[0] > 0 && ip[0] < 224 && ip[0] != 127;
}
static void configurePi() {
    String value = keyboard(piHost, 15, "Raspberry Pi IPv4");
    if (value == "\x1B" || value.isEmpty()) return;
    if (!validHost(value)) { displayError("Invalid IPv4 address", true); return; }
    Preferences p;
    if (!p.begin("pi-remote", false)) { displayError("Settings unavailable", true); return; }
    bool ok = p.putString("host", value) == value.length() && p.getString("host") == value;
    p.end();
    if (ok) { piHost = value; displaySuccess("Pi address saved", true); }
    else displayError("Pi address save failed", true);
}

// Bound memory even for chunked responses or a server sending no Content-Length.
class PiResponse : public Stream {
public:
    static constexpr size_t limit = 32768;
    String body;
    bool overflow = false;
    size_t write(uint8_t value) override { return write(&value, 1); }
    size_t write(const uint8_t *data, size_t length) override {
        if (length > limit - body.length()) { overflow = true; return 0; }
        return body.concat(reinterpret_cast<const char *>(data), length) ? length : 0;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};
static void loadPiHost() {
    Preferences p;
    if (p.begin("pi-remote", true)) {
        String saved = p.getString("host", piHost); p.end();
        if (validHost(saved)) piHost = saved;
    }
}
static bool requestPi(const String &route, String &body, bool post = false) {
    loadPiHost();
    if (!WiFi.isConnected()) {
        displayInfo("WLAN: gespeicherte Verbindung wird gesucht...");
        if (!wifiConnecttoKnownNet()) wifiConnectMenu(WIFI_STA);
    }
    if (!WiFi.isConnected()) return false;
    displayInfo("Pi: " + route + "\nWaiting for server...");
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(1500);
    http.setTimeout(15000);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(client, "http://" + piHost + ":5050" + route)) {
        displayError("Cannot start Pi request", true); return false;
    }
    // Never retry automatically: volume/mute and app launches are not idempotent.
    int code = post ? http.POST("") : http.GET();
    if (code < 200 || code >= 300) {
        http.end();
        displayError(code == 404 ? "Pi route missing: " + route : "Pi HTTP " + String(code), true);
        return false;
    }
    PiResponse response;
    int received = http.getSize() > int(PiResponse::limit) ? -1 : http.writeToStream(&response);
    http.end();
    if (received < 0 || response.overflow) { displayError("Pi reply incomplete / too large", true); return false; }
    body = response.body;
    JsonDocument doc;
    if (!body.isEmpty() && !deserializeJson(doc, body) && doc["ok"].is<bool>() && !doc["ok"].as<bool>()) {
        displayError("Pi reported command failure", true); return false;
    }
    return true;
}
static void command(const char *route) {
    String body;
    if (requestPi(route, body)) displaySuccess("Pi accepted command", true);
}
static bool extendedCommand(const char *key) {
    String body;
    if (!requestPi("/remote/capabilities", body)) return false;
    JsonDocument doc;
    if (deserializeJson(doc, body) || doc["version"].as<int>() != 1 || !doc["keys"].is<JsonArray>()) {
        displayError("Pi-Erweiterung nicht passend", true); return false;
    }
    bool supported = false;
    for (JsonVariant value : doc["keys"].as<JsonArray>()) if (value.as<String>() == key) supported = true;
    if (!supported) { displayError("Pi unterstuetzt Taste nicht", true); return false; }
    if (!requestPi(String("/remote/key/") + key, body, true)) return false;
    doc.clear();
    if (deserializeJson(doc, body) || !doc["ok"].is<bool>() || !doc["ok"].as<bool>()) {
        displayError("Pi bestaetigt Befehl nicht", true); return false;
    }
    return true;
}
bool piPowerOff() { return extendedCommand("off"); }
static void navigationMenu() {
    std::vector<Option> opts;
    const char *keys[] = {"up", "down", "left", "right", "ok", "off"};
    const char *labels[] = {"Hoch", "Runter", "Links", "Rechts", "OK", "TV ausschalten"};
    for (size_t i = 0; i < 6; ++i) {
        const char *key = keys[i];
        opts.push_back({labels[i], [key]() { if (extendedCommand(key)) displaySuccess("Pi-Befehl gesendet", true); }});
    }
    remoteMenu(opts, "Pi-Erweiterung erforderlich");
}
static void channels() {
    String body;
    if (!requestPi("/channels", body)) return;
    JsonDocument doc;
    if (deserializeJson(doc, body) || !PiReply::validChannels(doc.as<JsonVariantConst>())) {
        displayError("Invalid Pi channel list", true); return;
    }
    std::vector<Option> opts;
    for (JsonObject channel : doc.as<JsonArray>()) {
        String id = channel["id"].as<String>(), name = channel["name"].as<String>();
        opts.push_back({name, [id]() { command(("/tv/" + id).c_str()); }});
    }
    remoteMenu(opts, "Pi TV channels");
}
void piRemoteMenu() {
    Preferences p;
    if (p.begin("pi-remote", true)) {
        String saved = p.getString("host", piHost); p.end();
        if (validHost(saved)) piHost = saved;
    }
    std::vector<Option> opts = {
        {"Navigation / AUS (Pi-Addon)", navigationMenu},
        {"Pi address / setup", configurePi},
        {"Show Pi address", []() { displayInfo(piHost + ":5050\nRaspberry Pi, not TV IP", true); }},
        {"TV channels", channels},
        {"Joyn", []() { command("/joyn"); }},
        {"Pro7", []() { command("/pro7"); }},
        {"ORF1", []() { command("/orf1"); }},
        {"ORF2", []() { command("/orf2"); }},
        {"Jellyfin", []() { command("/jellyfin"); }},
        {"Home", []() { command("/home"); }},
        {"Back", []() { command("/back"); }},
        {"Volume +", []() { command("/volup"); }},
        {"Volume -", []() { command("/voldown"); }},
        {"Mute", []() { command("/mute"); }},
    };
    remoteMenu(opts, "Xiaomi TV via Raspberry Pi");
}
