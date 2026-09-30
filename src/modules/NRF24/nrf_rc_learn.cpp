#include "nrf_rc_learn.h"
#include "modules/NRF24/nrf_common.h"
#include "core/display.h"
#include "core/utils.h"
#include <Preferences.h>

static void saveProfile(uint8_t channel, rf24_datarate_e rate, const uint8_t *packets, uint8_t count, uint8_t width) {
    Preferences prefs;
    prefs.begin("nrf-rc-learn", false);
    prefs.putUChar("channel", channel);
    prefs.putUChar("rate", (uint8_t)rate);
    prefs.putUChar("width", width);
    prefs.putUChar("count", count);
    if (count && width) prefs.putBytes("packets", packets, (size_t)count * width);
    prefs.end();
}

static bool captureOn(uint8_t channel, rf24_datarate_e rate, uint8_t *packets, uint8_t &count, uint8_t &width) {
    // Generic nRF24 cannot promiscuously decode arbitrary 2.4 GHz traffic.
    // This logger is deliberately limited to compatible nRF24-style packets
    // and a known receive address. It does not bypass hopping/pairing/rolling codes.
    static const uint8_t learnAddr[5] = {'R','C','L','R','N'};

    NRFradio.stopListening();
    NRFradio.setChannel(channel);
    NRFradio.setDataRate(rate);
    NRFradio.setAutoAck(false);
    NRFradio.disableCRC();
    NRFradio.setAddressWidth(5);
    NRFradio.setPayloadSize(32);
    NRFradio.openReadingPipe(1, learnAddr);
    NRFradio.startListening();

    count = 0;
    width = 32;
    const uint32_t until = millis() + 5000;
    while ((int32_t)(until - millis()) > 0 && !check(EscPress)) {
        if (NRFradio.available()) {
            if (count < 8) {
                NRFradio.read(packets + ((size_t)count * width), width);
                count++;
            } else {
                uint8_t dump[32];
                NRFradio.read(dump, sizeof(dump));
            }
        }
        delay(1);
    }
    NRFradio.stopListening();
    return count > 0;
}

void nrf_rc_learn() {
    if (!nrf_start(NRF_MODE_SPI)) {
        displayError("NRF24 not found", true);
        return;
    }

    displayInfo("RC Learn: activity scan...");
    NRFradio.setAutoAck(false);
    NRFradio.disableCRC();
    NRFradio.setDataRate(RF24_1MBPS);

    uint16_t hits[126] = {0};
    const uint8_t passes = 32;
    for (uint8_t pass = 0; pass < passes && !check(EscPress); ++pass) {
        for (uint8_t ch = 0; ch < 126; ++ch) {
            NRFradio.setChannel(ch);
            NRFradio.startListening();
            delayMicroseconds(180);
            NRFradio.stopListening();
            if (NRFradio.testRPD()) hits[ch]++;
        }
    }

    uint8_t best = 0;
    for (uint8_t ch = 1; ch < 126; ++ch) if (hits[ch] > hits[best]) best = ch;

    String scanMsg = "CH " + String(best) + " / " + String(2400 + best) + "MHz\nPress RC button";
    displayInfo(scanMsg);

    const rf24_datarate_e rates[] = {RF24_250KBPS, RF24_1MBPS, RF24_2MBPS};
    const char *rateNames[] = {"250K", "1M", "2M"};
    uint8_t packets[8 * 32] = {0};
    uint8_t count = 0, width = 0;
    rf24_datarate_e foundRate = RF24_1MBPS;

    // Try the strongest activity channel at all nRF24 data rates.
    // Packet capture only succeeds for traffic compatible with the configured address.
    for (uint8_t i = 0; i < 3 && count == 0; ++i) {
        displayInfo("Listen CH " + String(best) + " " + rateNames[i] + "...");
        uint8_t n = 0, w = 0;
        if (captureOn(best, rates[i], packets, n, w)) {
            count = n;
            width = w;
            foundRate = rates[i];
        }
    }

    saveProfile(best, foundRate, packets, count, width);
    NRFradio.powerDown();

    if (!count) {
        displaySuccess(
            "Activity learned: CH " + String(best) + " (" + String(2400 + best) +
            "MHz)\nNo compatible packets decoded", true
        );
        return;
    }

    String msg = "Learned CH " + String(best) + "\nPackets: " + String(count) + " / " +
                 (foundRate == RF24_250KBPS ? "250K" : foundRate == RF24_2MBPS ? "2M" : "1M");
    displaySuccess(msg, true);
}
