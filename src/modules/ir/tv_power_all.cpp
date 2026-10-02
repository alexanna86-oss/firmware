#include "TV-B-Gone.h"
#include "tv_power_decode.h"
#include "ir_utils.h"
#include "core/display.h"
#include <algorithm>

void startAllTvPower() {
    if (!init_ir_tx_mutex()) { displayError("IR busy", true); return; }
    checkIrTxPin();
    if (returnToMenu) return;
#ifdef USE_BOOST
    PPM.enableOTG();
#endif
    setup_ir_pin(bruceConfigPins.irTx, OUTPUT);
    IRsend sender(bruceConfigPins.irTx);
    sender.begin();
    // Only the established TV databases: no fan/projector/menu/app commands.
    std::vector<const IrCode *> codes;
    codes.insert(codes.end(), EUpowerCodes, EUpowerCodes + num_EUcodes);
    codes.insert(codes.end(), NApowerCodes, NApowerCodes + num_NAcodes);
    std::vector<uint64_t> sent;
    bool cancelled = false;
    check(SelPress); // consume the menu selection, never require a second start
    for (size_t i = 0; i < codes.size(); ++i) {
        if (check(EscPress) || returnToMenu) { cancelled = true; break; }
        auto raw = TvPower::decode(*codes[i]);
        if (raw.empty()) continue;
        uint64_t key = TvPower::fingerprint(codes[i]->timer_val, raw);
        if (std::find(sent.begin(), sent.end(), key) != sent.end()) continue;
        displayInfo("Alle TVs: Power\nCode " + String(i + 1) + "/" + String(codes.size()) +
                    "\nAuf TVs richten; ESC stoppt");
        lock_ir_tx();
        sender.sendRaw(raw.data(), raw.size(), codes[i]->timer_val);
        unlock_ir_tx();
        sent.push_back(key);
        // Allow the TV to process the frame and keep Back responsive between codes.
        uint32_t start = millis();
        while (millis() - start < 205) {
            if (check(EscPress) || returnToMenu) { cancelled = true; break; }
            delay(5);
        }
        if (cancelled) break;
    }
    digitalWrite(bruceConfigPins.irTx, LED_OFF);
#ifdef USE_BOOST
    PPM.disableOTG();
#endif
    displayInfo((cancelled ? "Gestoppt: " : "Fertig: ") + String(sent.size()) +
                " TV-Codes\nPower kann ein/aus umschalten\nKeine TV-Rueckmeldung", true);
}
