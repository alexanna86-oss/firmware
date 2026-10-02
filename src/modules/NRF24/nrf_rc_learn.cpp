#include "nrf_rc_learn.h"
#include "rc_profile.h"
#include "modules/NRF24/nrf_common.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "modules/universal_remote/remote_menu.h"
#include <Preferences.h>

using namespace RcProfile;
static Packet config;

template <typename T> static bool loadRecord(const char *key, T &record) {
    Preferences p;
    if (!p.begin("nrf-rc-learn", true)) return false;
    bool ok = p.getBytesLength(key) == sizeof(T) && p.getBytes(key, &record, sizeof(T)) == sizeof(T);
    p.end();
    return ok && valid(record);
}
template <typename T> static bool saveRecord(const char *key, T &record) {
    seal(record);
    Preferences p;
    if (!p.begin("nrf-rc-learn", false)) return false;
    bool ok = p.putBytes(key, &record, sizeof(T)) == sizeof(T);
    p.end();
    T verify;
    return ok && loadRecord(key, verify) && memcmp(&record, &verify, sizeof(T)) == 0;
}
static bool startRadio() {
    if (bruceConfigPins.NRF24_bus.checkConflict(GPIO_NUM_NC) || !nrf_start(NRF_MODE_SPI)) {
        displayError("Check NRF24 module/pins", true);
        return false;
    }
    NRFradio.stopListening();
    NRFradio.setAutoAck(false);
    NRFradio.disableAckPayload();
    NRFradio.disableDynamicPayloads();
    NRFradio.flush_rx();
    NRFradio.flush_tx();
    for (uint8_t pipe = 0; pipe < 6; ++pipe) NRFradio.closeReadingPipe(pipe);
    return true;
}
static void stopRadio() { NRFradio.stopListening(); NRFradio.powerDown(); }
static void showScan(const Scan &s) {
    uint8_t channels[126];
    rank(s, channels);
    if (!s.hits[channels[0]]) { displayInfo("Scan saved: no RPD activity", true); return; }
    std::vector<Option> opts;
    for (uint8_t ch : channels) {
        String label = "CH " + String(ch) + ": " + String(s.hits[ch]) + "/" + String(s.samples);
        uint16_t hits = s.hits[ch], samples = s.samples;
        opts.push_back({label.c_str(), [ch, hits, samples]() {
            displayInfo(String(2400 + ch) + " MHz\nRPD " + String(hits) + "/" + String(samples) +
                        "\nActivity only; no decoding", true);
        }});
    }
    remoteMenu(opts, "Activity: strongest first");
}
void nrf_rc_learn() {
    if (!startRadio()) return;
    NRFradio.disableCRC();
    NRFradio.setDataRate(RF24_1MBPS);
    Scan s;
    constexpr uint8_t passes = 48, samplesPerPass = 3;
    displayInfo("Activity scan: CH 0-125\nESC cancels without saving");
    for (uint8_t pass = 0; pass < passes; ++pass) {
        for (uint8_t ch = 0; ch < 126; ++ch) {
            if (check(EscPress)) { stopRadio(); return; }
            NRFradio.setChannel(ch);
            for (uint8_t sample = 0; sample < samplesPerPass; ++sample) {
                NRFradio.startListening();
                delayMicroseconds(500);
                // Read the RPD latch after RX ends, before a new RX clears it.
                NRFradio.stopListening();
                if (NRFradio.testRPD()) ++s.hits[ch];
            }
            if ((ch & 7) == 0) delay(1);
        }
        s.samples += samplesPerPass;
        if ((pass & 3) == 0) displayInfo("Activity scan " + String(pass + 1) + "/48");
    }
    stopRadio();
    if (!saveRecord("scanV1", s)) { displayError("Scan save failed", true); return; }
    showScan(s);
}
static bool applyProfile(const Packet &p) {
    if (!settingsValid(p)) return false;
    NRFradio.setChannel(p.channel);
    if (!NRFradio.setDataRate(static_cast<rf24_datarate_e>(p.rate))) return false;
    NRFradio.setAddressWidth(p.addressSize);
    if (p.width) NRFradio.setPayloadSize(p.width); else NRFradio.enableDynamicPayloads();
    if (!p.crc) NRFradio.disableCRC();
    else NRFradio.setCRCLength(p.crc == 1 ? RF24_CRC_8 : RF24_CRC_16);
    return true;
}
static String addressText(const Packet &p) {
    String s;
    for (uint8_t i = 0; i < p.addressSize; ++i) {
        char hex[3]; snprintf(hex, sizeof(hex), "%02X", p.address[i]); s += hex;
    }
    return s;
}
static void showPacket(const Packet &p) {
    displayInfo("CH " + String(p.channel) + " " + (p.rate == 2 ? "250K" : p.rate == 1 ? "2M" : "1M") +
                "\nAddr " + addressText(p) + "\nWidth " + String(p.width) + " CRC " + String(p.crc * 8) +
                "\nPackets " + String(p.count), true);
}
static void capture() {
    displayInfo("Known own fixed-code devices\nAddress/rate/CRC must match\nNot a universal sniffer", true);
    if (!startRadio()) return;
    Packet p = config;
    p.count = 0;
    memset(p.lengths, 0, sizeof(p.lengths));
    memset(p.data, 0, sizeof(p.data));
    if (!applyProfile(p)) { stopRadio(); displayError("Unsupported radio profile", true); return; }
    NRFradio.openReadingPipe(1, p.address);
    NRFradio.startListening();
    displayInfo("Press device button\nListening 10 sec; ESC cancels");
    uint32_t start = millis();
    while (millis() - start < 10000 && p.count < 8) {
        if (check(EscPress)) { stopRadio(); return; }
        if (NRFradio.available()) {
            uint8_t n = p.width ? p.width : NRFradio.getDynamicPayloadSize();
            if (!n || n > 32) { NRFradio.flush_rx(); continue; }
            NRFradio.read(p.data[p.count], n);
            p.lengths[p.count++] = n;
        }
        delay(1);
    }
    stopRadio();
    if (!p.count) { displayWarning("No matching packets\nPrevious capture kept", true); return; }
    if (!saveRecord("packetV1", p)) { displayError("Packet save failed", true); return; }
    displaySuccess("Compatible packets saved", true);
    showPacket(p);
}
static void replay() {
    Packet p;
    if (!loadRecord("packetV1", p)) { displayWarning("No valid saved packet", true); return; }
    if (!startRadio()) return;
    if (!applyProfile(p)) { stopRadio(); displayError("Unsupported radio profile", true); return; }
    NRFradio.openWritingPipe(p.address);
    uint8_t sent = 0;
    for (uint8_t i = 0; i < p.count && !check(EscPress); ++i) {
        if (NRFradio.write(p.data[i], p.lengths[i])) ++sent;
        delay(12);
    }
    stopRadio();
    displayInfo("Transmitted " + String(sent) + "/" + String(p.count) + "\nNo receiver acknowledgement", true);
}
static void editNumber(uint8_t &value, uint8_t maximum, const char *title) {
    String s = num_keyboard(String(value), 3, title);
    if (s.isEmpty()) return;
    for (size_t i = 0; i < s.length(); ++i) if (!isDigit(s[i])) { displayError("Invalid number", true); return; }
    int n = s.toInt();
    if (n > maximum) { displayError("Out of range", true); return; }
    value = n;
}
void nrf_rc_capture() {
    Packet saved;
    if (loadRecord("packetV1", saved)) config = saved;
    std::vector<Option> opts = {
        {"Show profile", []() { showPacket(config); }},
        {"Channel 0-125", []() { editNumber(config.channel, 125, "Channel 0-125"); }},
        {"Address (hex bytes)", []() {
            String s = hex_keyboard(addressText(config), 10, "Address: 6/8/10 hex digits");
            if (s.isEmpty()) return;
            if (s.length() != 6 && s.length() != 8 && s.length() != 10) {
                displayError("Address must be 3-5 bytes", true); return;
            }
            for (size_t i = 0; i < s.length(); ++i) if (!isxdigit(static_cast<unsigned char>(s[i]))) {
                displayError("Invalid hex address", true); return;
            }
            config.addressSize = s.length() / 2;
            for (uint8_t i = 0; i < config.addressSize; ++i)
                config.address[i] = strtoul(s.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
        }},
        {"Rate 250K", []() { config.rate = 2; }},
        {"Rate 1M", []() { config.rate = 0; }},
        {"Rate 2M", []() { config.rate = 1; }},
        {"Payload 1-32 (0=dynamic)", []() { editNumber(config.width, 32, "Payload 0-32"); }},
        {"CRC 16 bit", []() { config.crc = 2; }},
        {"CRC 8 bit", []() { config.crc = 1; }},
        {"CRC off", []() { config.crc = 0; }},
        {"Capture + Save", capture},
    };
    remoteMenu(opts, "Known fixed-code profile");
}
static void showSavedScan() {
    Scan s;
    if (loadRecord("scanV1", s)) { showScan(s); return; }
    // Old maps remain readable. Never trust the old unbounded activeCount/activeCh.
    Preferences p;
    if (p.begin("nrf-rc-learn", true)) {
        bool ok = p.getBytesLength("scanHits") == sizeof(s.hits) &&
                  p.getBytes("scanHits", s.hits, sizeof(s.hits)) == sizeof(s.hits);
        p.end();
        s.samples = 144;
        seal(s);
        if (ok && valid(s)) { showScan(s); return; }
    }
    displayWarning("No valid saved activity scan", true);
}
void nrf_rc_saved() {
    std::vector<Option> opts = {
        {"Saved activity (ranked)", showSavedScan},
        {"Saved packet profile", []() {
            Packet p;
            if (loadRecord("packetV1", p)) showPacket(p);
            else displayWarning("No valid saved packet", true);
        }},
        {"Replay own fixed-code", replay},
    };
    remoteMenu(opts, "NRF24 Saved RC");
}
