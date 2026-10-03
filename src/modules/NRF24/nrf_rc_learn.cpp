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
static void forceTEmbedNrfPins() {
#if defined(T_EMBED_1101)
    auto &bus = bruceConfigPins.NRF24_bus;
    if (bus.sck != (gpio_num_t)NRF24_SCK_PIN || bus.miso != (gpio_num_t)NRF24_MISO_PIN ||
        bus.mosi != (gpio_num_t)NRF24_MOSI_PIN || bus.cs != (gpio_num_t)NRF24_SS_PIN ||
        bus.io0 != (gpio_num_t)NRF24_CE_PIN) {
        bus = BruceConfigPins::SPIPins(
            (gpio_num_t)NRF24_SCK_PIN, (gpio_num_t)NRF24_MISO_PIN, (gpio_num_t)NRF24_MOSI_PIN,
            (gpio_num_t)NRF24_SS_PIN, (gpio_num_t)NRF24_CE_PIN
        );
        Serial.println("[NRF24] T-Embed bus restored to board pins");
    }
#endif
}

static bool startRadio() {
    forceTEmbedNrfPins();
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
void nrf_rc_diagnostics() {
    auto &bus = bruceConfigPins.NRF24_bus;
    displayInfo("nRF24 Pins\nCE " + String(bus.io0) + " CS " + String(bus.cs) +
                "\nSCK " + String(bus.sck) + " MISO " + String(bus.miso) +
                " MOSI " + String(bus.mosi), true);
    if (!startRadio()) {
#if defined(T_EMBED_1101)
        displayInfo("T-Embed nRF24 nicht erreichbar\nBoard-Pins: CE43 CS44\nSPI: SCK11 MISO10 MOSI9", true);
#else
        displayInfo("nRF24 nicht erreichbar\n3.3V, GND und SPI pruefen\nCC1101 ist kein nRF24", true);
#endif
        return;
    }
    bool ok = NRFradio.isChipConnected();
    for (uint8_t channel : {0, 62, 125}) {
        NRFradio.setChannel(channel);
        ok = ok && NRFradio.getChannel() == channel;
    }
    bool plus = NRFradio.isPVariant();
    stopRadio();
    displayInfo(ok ? String("SPI-Kanaltest OK\n") + (plus ? "nRF24L01+ erkannt" : "nRF24 erkannt") +
                      "\nKein Test der Antenne!" : "SPI-Test fehlgeschlagen\nPins / Stromversorgung pruefen", true);
}
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

static void configurePromiscuous(rf24_datarate_e rate) {
    NRFradio.stopListening();
    NRFradio.setAutoAck(false);
    NRFradio.disableCRC();
    NRFradio.disableAckPayload();
    NRFradio.disableDynamicPayloads();
    NRFradio.setAddressWidth(2);
    NRFradio.setPayloadSize(32);
    NRFradio.setRetries(0, 0);
    NRFradio.flush_rx();
    NRFradio.flush_tx();
    NRFradio.setDataRate(rate);
    const uint8_t noiseAddress[][2] = {
        {0x55, 0x55}, {0xAA, 0xAA}, {0xA0, 0xAA},
        {0xAB, 0xAA}, {0xAC, 0xAA}, {0xAD, 0xAA}
    };
    for (uint8_t i = 0; i < 6; ++i) NRFradio.openReadingPipe(i, noiseAddress[i]);
}

static void scanActivity(uint16_t passes) {
    displayInfo("RC-Taste gedrueckt halten\nKanaele 0-125\nRPD + Rohpaket-Erkennung", true);
    delay(150);
    if (!startRadio()) return;
    configurePromiscuous(RF24_1MBPS);
    Scan s;
    constexpr uint8_t samplesPerPass = 3;
    for (uint16_t pass = 0; pass < passes; ++pass) {
        for (uint8_t ch = 0; ch < 126; ++ch) {
            if (check(EscPress)) { stopRadio(); return; }
            NRFradio.setChannel(ch);
            for (uint8_t sample = 0; sample < samplesPerPass; ++sample) {
                NRFradio.startListening();
                delayMicroseconds(800);
                bool hit = NRFradio.testRPD();
                if (NRFradio.available()) {
                    uint8_t raw[32];
                    NRFradio.read(raw, sizeof(raw));
                    hit = true;
                }
                NRFradio.stopListening();
                if (hit) ++s.hits[ch];
            }
            if ((ch & 7) == 0) delay(1);
        }
        s.samples += samplesPerPass;
        if ((pass & 3) == 0) displayInfo("Funk suchen " + String(pass + 1) + "/" + String(passes) +
                                       "\nOriginal-RC betaetigen\nESC bricht ab");
    }
    stopRadio();
    if (!saveRecord("scanV1", s)) { displayError("Scan save failed", true); return; }
    showScan(s);
}
void nrf_rc_learn() { scanActivity(48); }
void nrf_rc_long_scan() { scanActivity(192); }
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

static const char *AUTO_SLOT_NAMES[8] = {
    "Vorwaerts", "Rueckwaerts", "Links", "Rechts", "Turbo", "Licht", "Taste 7", "Taste 8"
};

static uint16_t rcCrcUpdate(uint16_t crc, uint8_t byte, uint8_t bits) {
    crc ^= ((uint16_t)byte << 8);
    while (bits--) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    return crc;
}

static uint8_t rateCode(rf24_datarate_e rate) {
    if (rate == RF24_2MBPS) return 1;
    if (rate == RF24_250KBPS) return 2;
    return 0;
}

static bool decodeEsbRaw(
    const uint8_t *rawBuf, uint8_t size, uint8_t channel, rf24_datarate_e rate, Packet &out
) {
    if (size < 10) return false;
    uint8_t buf[37];
    if (size > sizeof(buf)) size = sizeof(buf);
    memcpy(buf, rawBuf, size);

    for (uint8_t offset = 0; offset < 2; ++offset) {
        if (offset) {
            memcpy(buf, rawBuf, size);
            for (int x = size - 1; x >= 0; --x) {
                buf[x] = x ? (uint8_t)((buf[x - 1] << 7) | (buf[x] >> 1)) : (uint8_t)(buf[x] >> 1);
            }
        }

        uint8_t payloadLength = buf[5] >> 2;
        if (!payloadLength || payloadLength > 32 || payloadLength > size - 9) continue;

        uint16_t crcGiven = ((uint16_t)buf[6 + payloadLength] << 9) |
                            ((uint16_t)buf[7 + payloadLength] << 1);
        crcGiven = (uint16_t)((crcGiven << 8) | (crcGiven >> 8));
        if (buf[8 + payloadLength] & 0x80) crcGiven |= 0x0100;

        uint16_t crcCalc = 0xFFFF;
        for (uint8_t x = 0; x < 6 + payloadLength; ++x) crcCalc = rcCrcUpdate(crcCalc, buf[x], 8);
        crcCalc = rcCrcUpdate(crcCalc, buf[6 + payloadLength] & 0x80, 1);
        crcCalc = (uint16_t)((crcCalc << 8) | (crcCalc >> 8));
        if (crcCalc != crcGiven) continue;

        Packet p;
        p.channel = channel;
        p.rate = rateCode(rate);
        p.addressSize = 5;
        memcpy(p.address, buf, 5);
        p.width = payloadLength;
        p.crc = 2;
        p.count = 1;
        p.lengths[0] = payloadLength;
        for (uint8_t x = 0; x < payloadLength; ++x)
            p.data[0][x] = (uint8_t)(((buf[6 + x] << 1) & 0xFF) | (buf[7 + x] >> 7));
        out = p;
        return true;
    }
    return false;
}

struct AutoCandidate {
    Packet packet;
    uint16_t frames = 0;
};

static bool sameAutoCandidate(const AutoCandidate &a, const Packet &b) {
    return a.packet.channel == b.channel && a.packet.rate == b.rate &&
           a.packet.addressSize == b.addressSize && a.packet.width == b.width &&
           memcmp(a.packet.address, b.address, b.addressSize) == 0;
}

static void addAutoCandidate(AutoCandidate (&items)[8], uint8_t &used, const Packet &p) {
    uint8_t idx = used;
    for (uint8_t i = 0; i < used; ++i) {
        if (sameAutoCandidate(items[i], p)) { idx = i; break; }
    }
    if (idx == used) {
        if (used >= 8) return;
        items[idx].packet = p;
        items[idx].frames = 0;
        ++used;
    }
    AutoCandidate &c = items[idx];
    ++c.frames;
    if (c.packet.count < 8) {
        uint8_t pos = c.packet.count;
        c.packet.lengths[pos] = p.lengths[0];
        memcpy(c.packet.data[pos], p.data[0], p.lengths[0]);
        c.packet.count = pos + 1;
    }
}

static bool autoDiscoverProfile(Packet &best) {
    displayInfo("RC AUTO FIND\nOriginal-Taste halten\nOK druecken, dann RC-Taste halten", true);
    delay(200);
    if (!startRadio()) return false;

    AutoCandidate candidates[8] = {};
    uint8_t used = 0;
    uint32_t rawFrames = 0, rpdSeen = 0;
    const rf24_datarate_e rates[] = {RF24_250KBPS, RF24_1MBPS, RF24_2MBPS};

    vTaskDelay(pdMS_TO_TICKS(1));
    uint32_t start = millis(), lastUi = 0;
    while (millis() - start < 12000) {
        for (auto rate : rates) {
            configurePromiscuous(rate);
            for (uint8_t ch = 0; ch < 126; ++ch) {
                if (check(EscPress)) { stopRadio(); return false; }
                NRFradio.setChannel(ch);
                NRFradio.startListening();
                delayMicroseconds(1000);
                if (NRFradio.testRPD()) ++rpdSeen;
                uint8_t drained = 0;
                while (NRFradio.available() && drained < 4) {
                    uint8_t raw[32];
                    NRFradio.read(raw, sizeof(raw));
                    ++drained;
                    ++rawFrames;
                    Packet p;
                    if (decodeEsbRaw(raw, sizeof(raw), ch, rate, p)) addAutoCandidate(candidates, used, p);
                }
                NRFradio.stopListening();

                // The T-Embed display and nRF24 share the SPI wiring. Yield often so
                // the ESP32-S3 watchdog and UI task keep running during the long sweep.
                if ((ch & 7) == 0) vTaskDelay(pdMS_TO_TICKS(1));

                if (millis() - lastUi > 750) {
                    displayInfo("AUTO FIND\nRate " + String(rateCode(rate) == 2 ? "250K" : rateCode(rate) == 1 ? "2M" : "1M") +
                                " CH " + String(ch) + "\nRaw " + String(rawFrames) +
                                " Decode " + String(used));
                    lastUi = millis();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    stopRadio();

    if (!used) {
        if (rawFrames)
            displayWarning("Rohpakete gesehen, aber\nkein gueltiges ESB CRC16\nAnderes/geschuetztes Protokoll?", true);
        else if (rpdSeen)
            displayWarning("2.4-GHz Aktivitaet gefunden,\naber kein lesbares nRF24-ESB Paket\nTaste naeher am Geraet testen", true);
        else
            displayWarning("nRF24 antwortet, aber\nkein 2.4-GHz Signal erkannt\nRC direkt daneben betaetigen", true);
        return false;
    }

    uint8_t bestIdx = 0;
    for (uint8_t i = 1; i < used; ++i)
        if (candidates[i].frames > candidates[bestIdx].frames) bestIdx = i;
    best = candidates[bestIdx].packet;
    if (!saveRecord("autoV1", best)) {
        displayError("AUTO profile save failed", true);
        return false;
    }
    displaySuccess("RC-Profil automatisch gefunden\nFrames " + String(candidates[bestIdx].frames), true);
    showPacket(best);
    return true;
}

static String slotKey(uint8_t slot) { return "btn" + String(slot); }


static const char *safeRateName(uint8_t rate) {
    return rate == 2 ? "250K" : rate == 1 ? "2M" : "1M";
}

static void saveSafeTune(uint8_t slot, uint8_t channel, uint8_t rate, uint16_t hits) {
    Preferences p;
    if (!p.begin("nrf-rc-learn", false)) return;
    String base = "safe" + String(slot);
    p.putUChar((base + "c").c_str(), channel);
    p.putUChar((base + "r").c_str(), rate);
    p.putUShort((base + "h").c_str(), hits);
    p.end();
}

static bool loadSafeTune(uint8_t slot, uint8_t &channel, uint8_t &rate, uint16_t &hits) {
    Preferences p;
    if (!p.begin("nrf-rc-learn", true)) return false;
    String base = "safe" + String(slot);
    bool ok = p.isKey((base + "c").c_str()) && p.isKey((base + "r").c_str());
    if (ok) {
        channel = p.getUChar((base + "c").c_str(), 0);
        rate = p.getUChar((base + "r").c_str(), 0);
        hits = p.getUShort((base + "h").c_str(), 0);
    }
    p.end();
    return ok && channel <= 125 && rate <= 2;
}

static bool safeFindSignal(uint8_t slot, bool saveSlot) {
    displayInfo(
        String(saveSlot ? AUTO_SLOT_NAMES[slot] : "RC SAFE SIGNAL") +
        "\nOriginal-RC direkt daneben halten\nTaste gedrueckt halten + OK",
        true
    );
    delay(250);

    if (!startRadio()) return false;

    static uint16_t hits[126];
    memset(hits, 0, sizeof(hits));

    // RPD is a pure energy detector. Do not try to infer data rate here:
    // a single stable 1 Mbps RX configuration avoids unnecessary radio
    // reconfiguration and gives each RF channel a much longer dwell time.
    NRFradio.setAutoAck(false);
    NRFradio.disableAckPayload();
    NRFradio.disableDynamicPayloads();
    NRFradio.setAddressWidth(5);
    NRFradio.setPayloadSize(32);
    NRFradio.setDataRate(RF24_1MBPS);

    constexpr uint8_t passes = 64;
    constexpr uint16_t dwellUs = 1000;

    // Do not draw to TFT and do not poll buttons while nRF24 owns the
    // shared SPI wiring. This scan is deliberately boring but robust.
    for (uint8_t pass = 0; pass < passes; ++pass) {
        for (uint8_t ch = 0; ch < 126; ++ch) {
            NRFradio.setChannel(ch);
            NRFradio.startListening();
            delayMicroseconds(dwellUs);
            bool hit = NRFradio.testRPD();
            NRFradio.stopListening();
            if (hit && hits[ch] < 0xFFFF) ++hits[ch];
            if ((ch & 7) == 0) vTaskDelay(pdMS_TO_TICKS(1));
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    stopRadio();
    delay(150);

    uint8_t topCh[5] = {0, 0, 0, 0, 0};
    uint16_t topHits[5] = {0, 0, 0, 0, 0};
    for (uint8_t ch = 0; ch < 126; ++ch) {
        uint16_t h = hits[ch];
        if (!h) continue;
        for (uint8_t pos = 0; pos < 5; ++pos) {
            if (h > topHits[pos]) {
                for (int8_t move = 4; move > (int8_t)pos; --move) {
                    topHits[move] = topHits[move - 1];
                    topCh[move] = topCh[move - 1];
                }
                topHits[pos] = h;
                topCh[pos] = ch;
                break;
            }
        }
    }

    if (!topHits[0]) {
        displayWarning(
            "Kein Signal ueber RPD-Schwelle\nRC direkt am LILYGO testen\nAuto einschalten + RC-Taste halten",
            true
        );
        return false;
    }

    // Rate is intentionally not claimed by this energy-only scan.
    if (saveSlot) saveSafeTune(slot, topCh[0], 0, topHits[0]);

    String found = String(saveSlot ? AUTO_SLOT_NAMES[slot] : "RC SAFE") +
                   "\nStaerkster Kanal: CH " + String(topCh[0]) +
                   " (" + String(2400 + topCh[0]) + " MHz)" +
                   "\nTreffer " + String(topHits[0]) + "/" + String(passes);
    if (topHits[1]) found += "\n#2 CH " + String(topCh[1]) + " " + String(topHits[1]);
    if (topHits[2]) found += "  #3 CH " + String(topCh[2]) + " " + String(topHits[2]);

    displaySuccess(found, true);
    return true;
}

static bool captureAutoSlot(uint8_t slot, const Packet &profile) {
    if (!startRadio()) return false;
    Packet p = profile;
    p.count = 0;
    memset(p.lengths, 0, sizeof(p.lengths));
    memset(p.data, 0, sizeof(p.data));
    if (!applyProfile(p)) { stopRadio(); displayError("AUTO profile unsupported", true); return false; }
    NRFradio.openReadingPipe(1, p.address);
    NRFradio.startListening();
    displayInfo(String(AUTO_SLOT_NAMES[slot]) + "\nOriginal-Taste jetzt halten\n6 Sekunden Aufnahme");
    uint32_t start = millis();
    while (millis() - start < 6000 && p.count < 8) {
        if (check(EscPress)) { stopRadio(); return false; }
        if (NRFradio.available()) {
            uint8_t n = p.width ? p.width : NRFradio.getDynamicPayloadSize();
            if (!n || n > 32) { NRFradio.flush_rx(); continue; }
            NRFradio.read(p.data[p.count], n);
            p.lengths[p.count++] = n;
        }
        delay(1);
    }
    stopRadio();
    if (!p.count) return false;
    String key = slotKey(slot);
    if (!saveRecord(key.c_str(), p)) { displayError("Taste speichern fehlgeschlagen", true); return false; }
    displaySuccess(String(AUTO_SLOT_NAMES[slot]) + "\ngespeichert: " + String(p.count) + " Frames", true);
    return true;
}

static void learnAutoSlot(uint8_t slot) {
    // SAFE mode: first identify a stable channel/rate without using the
    // experimental 2-byte promiscuous decoder that caused resets/blanking
    // on the user's T-Embed CC1101 Plus.
    safeFindSignal(slot, true);
}

static void replayAutoSlot(uint8_t slot) {
    Packet p;
    String key = slotKey(slot);
    if (!loadRecord(key.c_str(), p)) {
        uint8_t ch = 0, rate = 0;
        uint16_t hits = 0;
        if (loadSafeTune(slot, ch, rate, hits)) {
            displayInfo(
                String(AUTO_SLOT_NAMES[slot]) + "\nSAFE Profil: CH " + String(ch) +
                " / " + safeRateName(rate) +
                "\nNoch keine TX-Pakete decodiert",
                true
            );
        } else {
            displayWarning(String(AUTO_SLOT_NAMES[slot]) + "\nnoch nicht gelernt", true);
        }
        return;
    }
    if (!startRadio()) return;
    if (!applyProfile(p)) { stopRadio(); displayError("Gespeichertes Profil ungueltig", true); return; }
    NRFradio.openWritingPipe(p.address);
    uint16_t sent = 0;
    for (uint8_t repeat = 0; repeat < 4 && !check(EscPress); ++repeat) {
        for (uint8_t i = 0; i < p.count && !check(EscPress); ++i) {
            if (NRFradio.write(p.data[i], p.lengths[i])) ++sent;
            delay(12);
        }
    }
    stopRadio();
    displayInfo(String(AUTO_SLOT_NAMES[slot]) + "\nTX Frames " + String(sent) +
                "\nKeine Wirkung = Pairing/Hopping/Counter", true);
}

void nrf_rc_auto_find() {
    safeFindSignal(0, false);
}

void nrf_rc_auto_learn() {
    remoteMenu({
        {"Vorwaerts lernen", []() { learnAutoSlot(0); }},
        {"Rueckwaerts lernen", []() { learnAutoSlot(1); }},
        {"Links lernen", []() { learnAutoSlot(2); }},
        {"Rechts lernen", []() { learnAutoSlot(3); }},
        {"Turbo lernen", []() { learnAutoSlot(4); }},
        {"Licht lernen", []() { learnAutoSlot(5); }},
        {"Taste 7 lernen", []() { learnAutoSlot(6); }},
        {"Taste 8 lernen", []() { learnAutoSlot(7); }},
    }, "RC SAFE LEARN");
}

void nrf_rc_auto_saved() {
    remoteMenu({
        {"Vorwaerts senden", []() { replayAutoSlot(0); }},
        {"Rueckwaerts senden", []() { replayAutoSlot(1); }},
        {"Links senden", []() { replayAutoSlot(2); }},
        {"Rechts senden", []() { replayAutoSlot(3); }},
        {"Turbo senden", []() { replayAutoSlot(4); }},
        {"Licht senden", []() { replayAutoSlot(5); }},
        {"Taste 7 senden", []() { replayAutoSlot(6); }},
        {"Taste 8 senden", []() { replayAutoSlot(7); }},
        {"AUTO Profil anzeigen", []() {
            Packet p;
            if (loadRecord("autoV1", p)) showPacket(p);
            else displayWarning("Noch kein AUTO Profil", true);
        }},
    }, "Gelernte RC-Tasten");
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
    if (!loadRecord("packetV1", p)) {
        displayWarning("No packet captured\nActivity scan is not a command\nUse Known Device + Capture", true);
        return;
    }
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
