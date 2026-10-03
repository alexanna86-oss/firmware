#include "ota_update.h"
#include "core/display.h"
#include "core/wifi/wifi_common.h"
#include "core/utils.h"
#include <WebServer.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

void universalRemoteOta() {
    if (!esp_ota_get_next_update_partition(nullptr)) {
        displayError("No OTA slot: USB factory flash needed", true);
        return;
    }
    if (!WiFi.isConnected()) {
        displayInfo("WLAN: gespeicherte Verbindung wird gesucht...");
        if (!wifiConnecttoKnownNet() && !wifiConnectMenu(WIFI_STA)) {
            displayError("WiFi connection failed", true);
            return;
        }
    }

    WebServer otaServer(8080);
    bool restartPending = false;
    bool uploadStarted = false, uploadComplete = false, uploadFailed = false;
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
            bool ok = uploadStarted && uploadComplete && !uploadFailed && !Update.hasError();
            otaServer.send(ok ? 200 : 500, "text/plain", ok ? "Update OK - device restarting" : "Update failed");
            if (ok) restartPending = true;
            uploadStarted = uploadComplete = false;
        },
        [&]() {
            HTTPUpload &upload = otaServer.upload();
            if (upload.status == UPLOAD_FILE_START) {
                uploadStarted = true;
                uploadComplete = false;
                uploadFailed = !Update.begin(UPDATE_SIZE_UNKNOWN);
                if (uploadFailed) Update.printError(Serial);
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (!uploadStarted || uploadFailed) return;
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    uploadFailed = true;
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                if (uploadStarted && !uploadFailed && upload.totalSize > 0) {
                    uploadComplete = Update.end(true);
                    if (!uploadComplete) { uploadFailed = true; Update.printError(Serial); }
                } else { uploadFailed = true; Update.abort(); }
            } else if (upload.status == UPLOAD_FILE_ABORTED) {
                uploadFailed = true;
                uploadComplete = false;
                Update.abort();
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
    if (Update.isRunning()) Update.abort();
}
