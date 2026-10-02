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
#include "modules/ir/ir_favorites.h"
#include "modules/universal_remote/all_off.h"
#include "modules/universal_remote/radio_help.h"

static void tvMenu() {
    remoteMenu({
        {"Alle TV-Powercodes senden", startAllTvPower},
        {"TV-Code finden und merken", findTvPower},
        {"Meine IR-Tasten", irFavoritesMenu},
        {"Xiaomi per WLAN / Pi", piRemoteMenu},
        {"Weitere IR-Funktionen", []() {
            remoteMenu({{"Fernbedienung anlernen", []() { IrRead(); }}, {"RAW anlernen", irRawLearnMenu},
                        {"IR-Dateien oeffnen", otherIRcodes}, {"TV-Codes nach Region", StartTvBGone}}, "IR erweitert");
        }},
    }, "Fernseher");
}

void UniversalRemoteMenu::optionsMenu() {
    std::vector<Option> options = {
        {"Favoriten / Meine IR-Tasten", irFavoritesMenu},
        {"Alles aus", allOffMenu},
        {"Fernseher", tvMenu},
        {"Licht / Nanoleaf", nanoleafMenu},
        {"Funk / RC", remoteRadioMenu},
        {"Firmware aktualisieren", universalRemoteOta},
    };
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
