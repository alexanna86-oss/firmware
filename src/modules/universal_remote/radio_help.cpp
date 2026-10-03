#include "radio_help.h"
#include "remote_menu.h"
#include "modules/NRF24/nrf_rc_learn.h"
#include "modules/rf/rf_utils.h"
#include "modules/rf/rf_scan.h"
#include "modules/rf/rf_spectrum.h"
#include "modules/rf/record.h"

static void ccReception(float frequency) {
    if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) {
        displayError("In RF Config CC1101 waehlen", true); return;
    }
    displayInfo("CC1101 Empfangstest\nSender 30 Sek. betaetigen\nNur Pegel, keine Decodierung", true);
    if (!initRfModule("rx", frequency)) { deinitRfModule(); return; }
    int peak = -150, minimum = 0;
    uint32_t start = millis(), lastDisplay = 0;
    delay(10);
    while (millis() - start < 30000 && !check(EscPress) && !returnToMenu) {
        int level = ELECHOUSE_cc1101.getRssi();
        peak = max(peak, level); minimum = min(minimum, level);
        if (millis() - lastDisplay >= 150) {
            displayInfo(String(frequency, 2) + " MHz\nJetzt " + String(level) + " dBm" +
                        "\nMaximum " + String(peak) + " dBm\nSender druecken; ESC beendet");
            lastDisplay = millis();
        }
        delay(10);
    }
    deinitRfModule();
    displayInfo("CC1101 ansprechbar\n" + String(frequency, 2) + " MHz: " + String(minimum) +
                " bis " + String(peak) + " dBm\nPegel ist kein Geraetenachweis", true);
}
static void ccMenu() {
    remoteMenu({
        {"433.92 MHz Empfang testen", []() { ccReception(433.92f); }},
        {"868.35 MHz Empfang testen", []() { ccReception(868.35f); }},
        {"315 MHz Empfang testen", []() { ccReception(315.0f); }},
        {"915 MHz Empfang testen", []() { ccReception(915.0f); }},
        {"Signal suchen / speichern", []() { RFScan(); }},
        {"Spektrum anzeigen", rf_CC1101_rssi},
        {"RAW aufnehmen", rf_raw_record},
        {"Was empfange ich hier?", []() {
            displayInfo("CC1101: Sub-GHz-Funk\nKein WLAN / Bluetooth\nKeine 2.4-GHz-RC-Autos\nFrequenz muss passen", true);
        }},
    }, "CC1101 / Sub-GHz");
}
static void nrfMenu() {
    remoteMenu({
        {"Modul und Pins testen", nrf_rc_diagnostics},
        {"RC SAFE: Signal suchen", nrf_rc_auto_find},
        {"RC SAFE: Taste messen", nrf_rc_auto_learn},
        {"RC SAFE: Gespeicherte Tasten", nrf_rc_auto_saved},
        {"Funkaktivitaet suchen", nrf_rc_learn},
        {"Laenger suchen", nrf_rc_long_scan},
        {"Gespeicherte Messung / Pakete", nrf_rc_saved},
        {"Bekanntes Geraet manuell", nrf_rc_capture},
        {"Wenn AUTO nichts findet", []() {
            displayInfo("AUTO trennt drei Faelle:\nkein 2.4-GHz Signal / nur Funkaktivitaet /\nESB-Paket dekodiert. Hopping, Pairing,\nVerschluesselung oder anderes Protokoll\nsind nicht universell lernbar.", true);
        }},
    }, "nRF24 / RC SAFE");
}
void remoteRadioMenu() {
    remoteMenu({{"nRF24: 2.4 GHz / RC", nrfMenu}, {"CC1101: Sub-GHz", ccMenu}}, "Funk: Modul waehlen");
}
