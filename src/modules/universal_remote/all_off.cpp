#include "all_off.h"
#include "remote_menu.h"
#include "nanoleaf_remote.h"
#include "pi_remote.h"
#include "modules/ir/ir_favorites.h"
#include <Preferences.h>

static uint8_t targets() {
    Preferences p;
    if (!p.begin("remote-off", true)) return 0;
    uint8_t value = p.getUChar("targets", 0); p.end();
    return value <= 7 ? value : 0;
}
static void toggleTarget(uint8_t bit) {
    uint8_t value = targets() ^ bit;
    Preferences p;
    if (!p.begin("remote-off", false)) { displayError("Speicher nicht verfuegbar", true); return; }
    bool ok = p.putUChar("targets", value) == 1 && p.getUChar("targets", 255) == value;
    p.end();
    if (!ok) displayError("Auswahl nicht gespeichert", true);
}
static void runAllOff() {
    uint8_t selected = targets();
    if (!selected) { displayInfo("Zuerst eigene Geraete auswaehlen", true); return; }
    String result;
    if (selected & 1) result += nanoleafPowerOff() ? "Nanoleaf: API OK\n" : "Nanoleaf: Fehler\n";
    if (check(EscPress) || returnToMenu) return;
    if (selected & 2) result += piPowerOff() ? "TV/Pi: Befehl OK\n" : "TV/Pi: Fehler\n";
    if (check(EscPress) || returnToMenu) return;
    if (selected & 4) result += "IR AUS gesendet: " + String(sendIrOffFavorites()) + "\n";
    displayInfo(result + "IR ohne Rueckmeldung", true);
}
void allOffMenu() {
    int index = 0;
    while (!returnToMenu) {
        uint8_t selected = targets();
        std::vector<Option> opts = {
            {"Alles aus starten", runAllOff},
            {String(selected & 1 ? "[x] " : "[ ] ") + "Nanoleaf", []() { toggleTarget(1); }},
            {String(selected & 2 ? "[x] " : "[ ] ") + "Xiaomi via Pi-Addon", []() { toggleTarget(2); }},
            {String(selected & 4 ? "[x] " : "[ ] ") + "Gelernte IR-AUS-Tasten", []() { toggleTarget(4); }},
            {"IR-AUS-Tasten einrichten", irFavoritesMenu},
        };
        index = loopOptions(opts, MENU_TYPE_SUBMENU, "Alles aus: meine Geraete", index);
        if (index < 0) break;
    }
}
