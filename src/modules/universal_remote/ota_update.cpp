#include "ota_update.h"
#include "core/display.h"
#include "core/wifi/wifi_common.h"
#include "core/utils.h"
#include <WebServer.h>
#include <Update.h>
#include <WiFi.h>

void universalRemoteOta() {
    if (!WiFi.isConnected()) {
        if (!wifiConnectMenu(WIFI_STA)) {
            displayError("WiFi connection failed", true);
            return;
        }
    }

    WebServer otaServer(8080);
    bool restartPending = false;
    const char *page =
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Bruce Universal Remote OTA</title></head><body>"
        "<h2>Bruce Universal Remote OTA</h2>"
        "<p>Select <b>firmware.bin</b> from the new build.</p>"
        "<form method='POST' action='/update' enctype='multipart/form-data'>"
        "<input type='file' name='firmware' accept='.bin' required>"
        "<button type='submit'>Update</button></form></body></html>";

    otaServer.on("/", HTTP_GET, [&]() { otaServer.send(200, "text/html", page); });
    otaServer.on("/update", HTTP_POST,
        [&]() {
            bool ok = !Update.hasError();
            otaServer.send(ok ? 200 : 500, "text/plain", ok ? "Update OK - device restarting" : "Update failed");
            if (ok) restartPending = true;
        },
        [&]() {
            HTTPUpload &upload = otaServer.upload();
            if (upload.status == UPLOAD_FILE_START) {
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
            } else if (upload.status == UPLOAD_FILE_END) {
                if (!Update.end(true)) Update.printError(Serial);
            }
        });

    otaServer.begin();
    drawMainBorderWithTitle("Universal OTA", true);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FP);
    tft.setCursor(10, 55);
    tft.print("Open browser:");
    tft.setCursor(10, 75);
    tft.print("http://" + WiFi.localIP().toString() + ":8080");
    tft.setCursor(10, 100);
    tft.print("Upload firmware.bin");
    tft.setCursor(10, tftHeight - 30);
    tft.print("ESC = exit");

    while (!check(EscPress)) {
        otaServer.handleClient();
        if (restartPending) {
            delay(800);
            ESP.restart();
        }
        delay(2);
    }
    otaServer.stop();
}
