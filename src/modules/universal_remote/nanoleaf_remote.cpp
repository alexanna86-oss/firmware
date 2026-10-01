#include "nanoleaf_remote.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/wifi/wifi_common.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPmDNS.h>

static String nlIp;
static String nlToken;

static void loadNanoleaf() {
    Preferences p;
    p.begin("nanoleaf", true);
    nlIp = p.getString("ip", "");
    nlToken = p.getString("token", "");
    p.end();
}

static void saveNanoleaf() {
    Preferences p;
    p.begin("nanoleaf", false);
    p.putString("ip", nlIp);
    p.putString("token", nlToken);
    p.end();
}

static bool ensureWifi() {
    if (WiFi.isConnected()) return true;
    wifiConnectMenu(WIFI_STA);
    return WiFi.isConnected();
}

static bool nlPut(const String &endpoint, const String &json) {
    loadNanoleaf();
    if (nlIp.isEmpty() || nlToken.isEmpty()) {
        displayWarning("Set Nanoleaf IP/token first", true);
        return false;
    }
    if (!ensureWifi()) {
        displayError("WiFi not connected", true);
        return false;
    }

    HTTPClient http;
    String url = "http://" + nlIp + ":16021/api/v1/" + nlToken + "/" + endpoint;
    if (!http.begin(url)) {
        displayError("Nanoleaf HTTP init failed", true);
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    int code = http.PUT(json);
    http.end();

    if (code >= 200 && code < 300) {
        displaySuccess("Nanoleaf OK");
        return true;
    }
    displayError("Nanoleaf HTTP " + String(code), true);
    return false;
}


static bool discoverNanoleaf() {
    if (!ensureWifi()) {
        displayError("WiFi not connected", true);
        return false;
    }
    displayInfo("Searching Nanoleaf...");
    if (!MDNS.begin("bruce-nanoleaf")) {
        displayError("mDNS start failed", true);
        return false;
    }
    int n = MDNS.queryService("nanoleafapi", "tcp");
    if (n <= 0) {
        displayWarning("No Nanoleaf found", true);
        return false;
    }
    nlIp = MDNS.IP(0).toString();
    saveNanoleaf();
    displaySuccess("Found: " + nlIp, true);
    return true;
}

static void pairNanoleaf() {
    loadNanoleaf();
    if (nlIp.isEmpty() && !discoverNanoleaf()) return;
    if (!ensureWifi()) return;

    displayInfo("Hold Nanoleaf power 5-7 sec\nthen press OK");
    while (!check(SelPress)) {
        if (check(EscPress)) return;
        delay(20);
    }

    HTTPClient http;
    String url = "http://" + nlIp + ":16021/api/v1/new";
    if (!http.begin(url)) {
        displayError("Nanoleaf HTTP init failed", true);
        return;
    }
    http.addHeader("Content-Type", "application/json");
    int code = http.POST("{}");
    String body = http.getString();
    http.end();
    if (code < 200 || code >= 300) {
        displayError("Pair failed HTTP " + String(code), true);
        return;
    }

    int key = body.indexOf("\"auth_token\"");
    int colon = key >= 0 ? body.indexOf(':', key) : -1;
    int q1 = colon >= 0 ? body.indexOf('"', colon) : -1;
    int q2 = q1 >= 0 ? body.indexOf('"', q1 + 1) : -1;
    if (q1 < 0 || q2 <= q1) {
        displayError("Token not found", true);
        return;
    }
    nlToken = body.substring(q1 + 1, q2);
    saveNanoleaf();
    displaySuccess("Nanoleaf paired + saved", true);
}

static void setupNanoleaf() {
    loadNanoleaf();
    String ip = keyboard(nlIp, 64, "Nanoleaf IP:");
    if (!ip.isEmpty()) nlIp = ip;
    String token = keyboard(nlToken, 128, "Nanoleaf API token:", true);
    if (!token.isEmpty()) nlToken = token;
    saveNanoleaf();
    displaySuccess("Nanoleaf saved", true);
}

static void setBrightness(int value) {
    nlPut("state", "{\"brightness\":{\"value\":" + String(value) + "}}");
}

static void setHue(int value) {
    nlPut("state", "{\"hue\":{\"value\":" + String(value) + "}}");
}

static void setSaturation(int value) {
    nlPut("state", "{\"sat\":{\"value\":" + String(value) + "}}");
}

static void setColorTemperature(int value) {
    nlPut("state", "{\"ct\":{\"value\":" + String(value) + "}}");
}

static void setEffect() {
    String effect = keyboard("", 80, "Nanoleaf scene/effect:");
    if (effect.isEmpty()) return;
    effect.replace("\\", "\\\\");
    effect.replace("\"", "\\\"");
    nlPut("effects", "{\"select\":\"" + effect + "\"}");
}

void nanoleafMenu() {
    loadNanoleaf();
    std::vector<Option> opts = {
        {"Auto Find", []() { discoverNanoleaf(); }},
        {"Auto Pair + Save", pairNanoleaf},
        {"Power ON", []() { nlPut("state", "{\"on\":{\"value\":true}}"); }},
        {"Power OFF", []() { nlPut("state", "{\"on\":{\"value\":false}}"); }},
        {"Brightness 25%", []() { setBrightness(25); }},
        {"Brightness 50%", []() { setBrightness(50); }},
        {"Brightness 100%", []() { setBrightness(100); }},
        {"Red", []() { setHue(0); setSaturation(100); }},
        {"Green", []() { setHue(120); setSaturation(100); }},
        {"Blue", []() { setHue(240); setSaturation(100); }},
        {"Warm White", []() { setSaturation(0); setColorTemperature(4000); }},
        {"Cool White", []() { setSaturation(0); setColorTemperature(6500); }},
        {"Scene / Effect", setEffect},
        {"Setup IP + Token", setupNanoleaf},
    };
    loopOptions(opts, MENU_TYPE_SUBMENU, "Nanoleaf");
}
