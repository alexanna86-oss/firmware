#include "ir_favorites.h"
#include "ir_favorite_record.h"
#include "TV-B-Gone.h"
#include "ir_utils.h"
#include "core/mykeyboard.h"
#include "core/settings.h"
#include "modules/universal_remote/remote_menu.h"
#include <IRrecv.h>
#include <IRutils.h>
#include <Preferences.h>

using IrFavorite::Record;
static uint16_t carrier = 38000;
static String slotKey(int slot) { return "slot" + String(slot); }
static bool load(int slot, Record &r) {
    Preferences p;
    if (!p.begin("ir-favorites", true)) return false;
    String key = slotKey(slot);
    bool ok = p.getBytesLength(key.c_str()) == sizeof(r) && p.getBytes(key.c_str(), &r, sizeof(r)) == sizeof(r);
    p.end();
    return ok && IrFavorite::valid(r);
}
bool saveIrFavorite(const uint16_t *raw, size_t count, uint16_t hz, const char *suggestion, bool offOnly) {
    if (!raw || !count || count > IrFavorite::MaxTimings) {
        displayError("IR-Signal zu lang", true); return false;
    }
    Record r;
    r.frequency = hz; r.count = count; r.offOnly = offOnly;
    memcpy(r.timings, raw, count * sizeof(uint16_t));
    String name = keyboard(suggestion, 31, "Name der Taste");
    if (name.isEmpty() || name == "\x1B") return false;
    strlcpy(r.name, name.c_str(), sizeof(r.name));
    r.checksum = IrFavorite::hash(r);
    if (!IrFavorite::valid(r)) { displayError("Ungueltiges IR-Signal", true); return false; }
    int selected = -1;
    std::vector<Option> opts;
    for (int i = 0; i < 8; ++i) {
        Record old;
        opts.push_back({String(i + 1) + ": " + (load(i, old) ? String(old.name) + " ersetzen" : "frei"),
                        [&, i]() { selected = i; }});
    }
    opts.push_back({"Abbrechen", []() {}});
    loopOptions(opts, MENU_TYPE_SUBMENU, "Favorit speichern");
    if (selected < 0) return false;
    Preferences p;
    if (!p.begin("ir-favorites", false)) { displayError("Speicher nicht verfuegbar", true); return false; }
    bool ok = p.putBytes(slotKey(selected).c_str(), &r, sizeof(r)) == sizeof(r);
    p.end();
    Record verify;
    ok = ok && load(selected, verify) && !memcmp(&verify, &r, sizeof(r));
    if (ok) displaySuccess("Favorit gespeichert", true);
    else displayError("Speichern fehlgeschlagen", true);
    return ok;
}
static void send(const Record &r) {
    checkIrTxPin();
#ifdef USE_BOOST
    PPM.enableOTG();
#endif
    setup_ir_pin(bruceConfigPins.irTx, OUTPUT);
    IRsend sender(bruceConfigPins.irTx);
    sender.begin();
    sender.sendRaw(r.timings, r.count, r.frequency);
    digitalWrite(bruceConfigPins.irTx, LOW);
#ifdef USE_BOOST
    PPM.disableOTG();
#endif
}
int sendIrOffFavorites() {
    int sent = 0;
    for (int i = 0; i < 8; ++i) {
        if (check(EscPress) || returnToMenu) break;
        Record r;
        if (!load(i, r) || !r.offOnly) continue;
        displayInfo("IR AUS: " + String(r.name));
        send(r); ++sent; delay(250);
    }
    return sent;
}
static void learn(bool offOnly) {
    if (offOnly) displayInfo("Nur echte AUS-Taste lernen!\nNicht Power-Umschalter.\nWird bei Alles aus gesendet.", true);
    setup_ir_pin(bruceConfigPins.irRx, INPUT);
    IRrecv receiver(bruceConfigPins.irRx, IrFavorite::MaxTimings, 50);
    receiver.enableIRIn();
    decode_results result;
    displayInfo("Originaltaste druecken\n30 Sek.; ESC bricht ab\nTraeger " + String(carrier / 1000) + " kHz");
    uint32_t start = millis();
    bool captured = false;
    while (millis() - start < 30000 && !check(EscPress)) {
        if (receiver.decode(&result)) { captured = true; break; }
        delay(5);
    }
    receiver.disableIRIn();
    if (!captured) { displayInfo("Keine Taste gespeichert", true); return; }
    if (result.overflow) { displayError("Signal unvollstaendig", true); return; }
    uint16_t count = getCorrectedRawLength(&result);
    uint16_t *raw = resultToRawArray(&result);
    if (raw) saveIrFavorite(raw, count, carrier, offOnly ? "TV AUS" : "TV Taste", offOnly);
    else displayError("Nicht genug Speicher", true);
    delete[] raw;
}
void irFavoritesMenu() {
    int index = 0;
    while (!returnToMenu) {
    std::vector<Option> opts;
    for (int i = 0; i < 8; ++i) {
        Record r;
        if (load(i, r)) opts.push_back({String(r.name) + (r.offOnly ? " [AUS]" : ""), [i]() {
            Record current;
            if (load(i, current)) { send(current); displayInfo("Gesendet: " + String(current.name), true); }
        }});
    }
    opts.push_back({"Taste anlernen", []() { learn(false); }});
    opts.push_back({"Nur AUS anlernen", []() { learn(true); }});
    opts.push_back({"IR-Frequenz einstellen", []() {
        std::vector<Option> frequencies;
        for (uint16_t khz : {38, 36, 40, 56, 30, 33})
            frequencies.push_back({String(khz) + " kHz", [khz]() { carrier = khz * 1000; }});
        loopOptions(frequencies, MENU_TYPE_SUBMENU, "Meist 38 kHz; nicht messbar");
    }});
    opts.push_back({"Favorit loeschen", []() {
        std::vector<Option> slots;
        for (int i = 0; i < 8; ++i) {
            Record r;
            if (load(i, r)) slots.push_back({r.name, [i]() {
                Preferences p;
                bool ok = p.begin("ir-favorites", false) && p.remove(slotKey(i).c_str()); p.end();
                if (ok) displaySuccess("Geloescht", true); else displayError("Loeschen fehlgeschlagen", true);
            }});
        }
        if (!slots.empty()) loopOptions(slots, MENU_TYPE_SUBMENU, "Loeschen");
    }});
    index = loopOptions(opts, MENU_TYPE_SUBMENU, "Meine IR-Tasten", index);
    if (index < 0) break;
    }
}
