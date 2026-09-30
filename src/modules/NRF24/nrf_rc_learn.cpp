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

    displayInfo("RC Learn+: press RC button during scan");
    NRFradio.setAutoAck(false);
    NRFradio.disableCRC();
    NRFradio.setDataRate(RF24_1MBPS);

    uint16_t hits[126] = {0};
    const uint8_t passes = 64;
    for (uint8_t pass = 0; pass < passes && !check(EscPress); ++pass) {
        for (uint8_t ch = 0; ch < 126; ++ch) {
            NRFradio.setChannel(ch);
            NRFradio.startListening();
            delayMicroseconds(220);
            NRFradio.stopListening();
            if (NRFradio.testRPD()) hits[ch]++;
        }
    }

    uint8_t best = 0;
    for (uint8_t ch = 1; ch < 126; ++ch) if (hits[ch] > hits[best]) best = ch;

    // Persist the complete activity map, not only the strongest channel.
    // This allows later inspection of every active channel without rescanning.
    Preferences scanPrefs;
    scanPrefs.begin("nrf-rc-learn", false);
    scanPrefs.putBytes("scanHits", hits, sizeof(hits));
    uint8_t activeChannels[126] = {0};
    uint8_t activeCount = 0;
    for (uint8_t ch = 0; ch < 126; ++ch) {
        if (hits[ch] > 0) activeChannels[activeCount++] = ch;
    }
    scanPrefs.putUChar("activeCount", activeCount);
    scanPrefs.putBytes("activeCh", activeChannels, activeCount);
    scanPrefs.putUChar("bestChannel", best);
    scanPrefs.end();

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

    saveProfile(foundChannel, foundRate, packets, count, width);
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


static void showSavedChannels() {
    Preferences p;
    p.begin("nrf-rc-learn", true);
    uint8_t n = p.getUChar("activeCount", 0);
    uint8_t channels[126] = {0};
    if (n) p.getBytes("activeCh", channels, n);
    p.end();
    if (!n) {
        displayWarning("No saved channels", true);
        return;
    }
    String msg = "Saved channels:\n";
    uint8_t shown = n > 18 ? 18 : n;
    for (uint8_t i = 0; i < shown; ++i) {
        msg += String(channels[i]);
        if (i + 1 < shown) msg += ", ";
    }
    if (n > shown) msg += "\n+" + String(n - shown) + " more";
    displayInfo(msg, true);
}

static void replaySavedPacket() {
    Preferences p;
    p.begin("nrf-rc-learn", true);
    uint8_t channel = p.getUChar("channel", 255);
    uint8_t rateRaw = p.getUChar("rate", (uint8_t)RF24_1MBPS);
    uint8_t width = p.getUChar("width", 0);
    uint8_t count = p.getUChar("count", 0);
    uint8_t packets[8 * 32] = {0};
    size_t bytes = (size_t)count * width;
    if (bytes > sizeof(packets)) bytes = sizeof(packets);
    if (bytes) p.getBytes("packets", packets, bytes);
    p.end();

    if (channel > 125 || !count || !width || width > 32) {
        displayWarning("No compatible saved packet", true);
        return;
    }
    if (!nrf_start(NRF_MODE_SPI)) {
        displayError("NRF24 not found", true);
        return;
    }

    // Replay is intentionally limited to the learner's own fixed address/profile.
    // It does not attempt to defeat pairing, hopping, rolling codes or other protections.
    static const uint8_t learnAddr[5] = {'R','C','L','R','N'};
    NRFradio.stopListening();
    NRFradio.setChannel(channel);
    NRFradio.setDataRate((rf24_datarate_e)rateRaw);
    NRFradio.setAutoAck(false);
    NRFradio.disableCRC();
    NRFradio.setAddressWidth(5);
    NRFradio.setPayloadSize(width);
    NRFradio.openWritingPipe(learnAddr);

    uint8_t sent = 0;
    for (uint8_t i = 0; i < count; ++i) {
        if (NRFradio.write(packets + ((size_t)i * width), width)) sent++;
        delay(12);
    }
    NRFradio.powerDown();
    displaySuccess("Replay test: " + String(sent) + "/" + String(count) + " sent", true);
}

void nrf_rc_saved() {
    std::vector<Option> opts = {
        {"Show saved channels", showSavedChannels},
        {"Test Replay", replaySavedPacket},
    };
    loopOptions(opts, MENU_TYPE_SUBMENU, "NRF24 Saved RC");
}
