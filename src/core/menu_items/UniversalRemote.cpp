#include "UniversalRemote.h"
#include "core/display.h"
#include "core/utils.h"
#include "modules/universal_remote/nanoleaf_remote.h"
#include "modules/universal_remote/ota_update.h"
#include "modules/NRF24/nrf_rc_learn.h"

void UniversalRemoteMenu::optionsMenu() {
    options.clear();
    options.push_back({"Nanoleaf", nanoleafMenu});
    options.push_back({"NRF24 RC Learn+", nrf_rc_learn});
    options.push_back({"OTA Update", universalRemoteOta});
    options.push_back({"Xiaomi TV / Joyn", []() {
        displayInfo("TV/Joyn module is next", true);
    }});
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Universal Remote");
}

void UniversalRemoteMenu::drawIcon(float scale) {
    clearIconArea();
    int w = 38 * scale;
    int h = 64 * scale;
    int x = iconCenterX - w / 2;
    int y = iconCenterY - h / 2;
    tft.drawRoundRect(x, y, w, h, 6 * scale, bruceConfig.priColor);
    tft.fillCircle(iconCenterX, y + 14 * scale, 4 * scale, bruceConfig.priColor);
    tft.fillCircle(iconCenterX, y + 30 * scale, 3 * scale, bruceConfig.priColor);
    tft.fillCircle(iconCenterX, y + 44 * scale, 3 * scale, bruceConfig.priColor);
}
