#include "UniversalRemote.h"
#include "core/display.h"
#include "core/utils.h"
#include "modules/universal_remote/nanoleaf_remote.h"
#include "modules/universal_remote/ota_update.h"
#include "modules/NRF24/nrf_rc_learn.h"
#include "modules/universal_remote/remote_menu.h"
#include "modules/universal_remote/pi_remote.h"
#include "modules/ir/ir_read.h"
#include "modules/ir/custom_ir.h"
#include "modules/ir/TV-B-Gone.h"

void UniversalRemoteMenu::optionsMenu() {
    std::vector<Option> options;
    options.push_back({"Nanoleaf", nanoleafMenu});
    options.push_back({"Xiaomi TV (Pi WiFi)", piRemoteMenu});
    options.push_back({"IR Learn", []() { IrRead(); }});
    options.push_back({"IR RAW Learn", irRawLearnMenu});
    options.push_back({"Saved IR remotes", otherIRcodes});
    options.push_back({"IR TV power codes", StartTvBGone});
    options.push_back({"NRF24 Activity Scan", nrf_rc_learn});
    options.push_back({"NRF24 Known Device", nrf_rc_capture});
    options.push_back({"NRF24 Saved RC", nrf_rc_saved});
    options.push_back({"OTA Update", universalRemoteOta});
    remoteMenu(options, "Universal Remote");
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
