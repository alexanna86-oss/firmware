#include "nanoleaf_remote.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/wifi/wifi_common.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>

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
        {"Power ON", []() { nlPut("state", "{\"on\":{\"value\":true}}"); }},
        {"Power OFF", []() { nlPut("state", "{\"on\":{\"value\":false}}"); }},
        {"Brightness 25%", []() { setBrightness(25); }},
        {"Brightness 50%", []() { setBrightness(50); }},
        {"Brightness 100%", []() { setBrightness(100); }},
        {"Scene / Effect", setEffect},
        {"Setup IP + Token", setupNanoleaf},
    };
    loopOptions(opts, MENU_TYPE_SUBMENU, "Nanoleaf");
}
