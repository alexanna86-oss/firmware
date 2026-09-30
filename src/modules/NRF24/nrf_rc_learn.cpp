#include "nrf_rc_learn.h"
#include "modules/NRF24/nrf_common.h"
#include "core/display.h"
#include <Preferences.h>

void nrf_rc_learn() {
    if (!nrf_start(NRF_MODE_SPI)) {
        displayError("NRF24 not found", true);
        return;
    }

    displayInfo("RC Learn: scanning 2.4GHz activity...");
    NRFradio.setAutoAck(false);
    NRFradio.disableCRC();
    NRFradio.setDataRate(RF24_1MBPS);

    uint16_t hits[126] = {0};
    const uint8_t passes = 24;

    for (uint8_t pass = 0; pass < passes; ++pass) {
        for (uint8_t ch = 0; ch < 126; ++ch) {
            NRFradio.setChannel(ch);
            NRFradio.startListening();
            delayMicroseconds(160);
            NRFradio.stopListening();
            if (NRFradio.testRPD()) hits[ch]++;
        }
    }

    uint8_t best = 0;
    for (uint8_t ch = 1; ch < 126; ++ch) {
        if (hits[ch] > hits[best]) best = ch;
    }

    NRFradio.stopListening();
    NRFradio.powerDown();

    Preferences prefs;
    prefs.begin("nrf-rc-learn", false);
    prefs.putUChar("channel", best);
    prefs.putUShort("hits", hits[best]);
    prefs.end();

    String msg = "Strongest CH " + String(best) + " (" + String(2400 + best) + " MHz), hits " +
                 String(hits[best]) + "/" + String(passes);
    displaySuccess(msg, true);
}
