# LILYGO T-Embed CC1101 Universal Remote

Target: `pio run -e lilygo-t-embed-cc1101`, branch `universal-remote`.
The existing board source filter includes the remote modules and main-menu entry.
IR, CC1101, Wi-Fi and the other existing Bruce modules remain included.

## Installation and OTA migration

The previous `custom_16Mb.csv` has only a factory application and cannot perform
normal Arduino OTA updates. This target now uses two 0x470000-byte application
slots and OTA metadata at 0xFEE000. PlatformIO's initial OTA image is placed at
that address, not its default 0xE000 inside the existing NVS partition.

**Before the first installation, back up settings and files from the device.**
Flash `firmware.factory.bin` by USB at address **0x0000** for ESP32-S3, 16 MB flash.
The filesystem moves and becomes smaller to make room for the second app slot;
the full factory image also overwrites the NVS area with padding. Restore your
backup afterwards, or re-enter Wi-Fi/Nanoleaf settings. Do not migrate by sending
only `firmware.bin` to a device that still has the old partition table.

For subsequent updates with this layout, use Universal Remote > OTA Update,
open the displayed local URL on port 8080, and upload **firmware.bin**. The factory
image is only for USB flashing. Empty/failed/aborted uploads do not trigger a reboot.
The OTA server runs only while this screen is open; ESC closes it. It retains the
existing local-network access model without HTTP authentication.

## Nanoleaf

- Auto Find queries `_nanoleafapi._tcp`, presents the discovered controllers,
  saves the selected IPv4 address, advertised port and device ID.
- If mDNS finds nothing (or cannot start), probe port 16021 on the actual IPv4
  subnet. Networks larger than 1024 addresses are limited to the local /22
  intersection. ESC cancels; manual IPv4 entry remains available.
- An open TCP port is displayed as a **candidate**, not confirmed Nanoleaf identity.
- Auto Pair + Save requires the controller's physical pairing mode. Hold its power
  button for 5–7 seconds, then press OK when the LED flashes. Pairing retries for
  up to 30 seconds with bounded HTTP timeouts. A valid JSON token is required.
- Settings are saved together and read back. Legacy `ip`/`token` settings can still
  be read when no new record exists. Selecting another controller clears the old
  token; a matching mDNS ID preserves it across an IP change. Token text is masked.
- On/off, brightness presets, RGB colours, warm/cool white and named scenes remain.
  This integration uses the panels-style local HTTP API; it is not a Matter/Thread controller.

## nRF24

An external nRF24L01+ and correctly configured SPI/CE/CS pins are required. The
T-Embed's built-in CC1101 is a different radio. Keep the existing wiring/pin setup.

- **Activity Scan** samples every channel 0–125, 144 times each, with 500 us RX
  dwell. The RPD indicator is an above-threshold activity count, not RSSI in dBm
  or evidence of a decodable remote. Results sort by activity, including channel
  125. Saved RC reloads the complete map without needing the radio.
- A complete versioned record with checksum is written and verified. Cancelled
  scans leave the previous record intact. A silent scan is still a valid saved
  result. Legacy complete `scanHits` maps remain readable.
- **Known Device** is separate. Configure channel, 250K/1M/2M rate, 3–5 byte receive
  address, CRC and static payload length (1–32) or dynamic mode (0). Hex bytes are
  supplied in RF24 buffer order, least-significant address byte first. The initial
  `52434C524E` value is only an example and must match your own device.
- Capture stores up to eight matching packets in ten seconds, with actual lengths
  and the full profile. Cancel/no reception preserves the previous capture.
  Saved RC displays that profile and replays those payloads with 12 ms spacing.
  Transmission count is not acknowledgement or proof that a receiver acted.
- Unknown addresses/protocols are not decoded automatically. This does not bypass
  pairing, hopping, encryption or rolling codes. Timing-dependent devices may
  require a dedicated protocol implementation. Old captures made with the former
  hard-coded profile must be captured again; their incomplete metadata is not replayed.

## Xiaomi TV through the existing Raspberry Pi

`Universal Remote > Xiaomi TV (Pi WiFi)` uses the same GET API as the user's
existing ESP32 remote. Default Pi address: `192.168.8.77`, port `5050`.
`Pi address / setup` stores a different IPv4 address with write/readback checking.
This is the **Pi address**, not the TV address. Connect the T-Embed to the same LAN.
The Pi retains its existing connection/authorization to the Xiaomi TV.

Supported existing routes: `/channels`, `/tv/<id>`, `/joyn`, `/pro7`, `/orf1`,
`/orf2`, `/jellyfin`, `/home`, `/back`, `/volup`, `/voldown`, `/mute`.
The channel menu uses the real server list (array of `id`/`name` objects), with
bounded response size (32 KiB), at most 192 entries and validated unique IDs.
HTTP errors and explicit JSON `ok:false` are reported. Requests have connection
and read timeouts and are never retried automatically. A successful HTTP reply
means the server accepted the request, not proof of a visible TV reaction.

The existing server was reached during development: `/` and `/channels` returned
HTTP 200; OPTIONS advertised GET for all routes above without issuing TV commands.
No new Pi installation is included or required for those existing routes.
There is no verified Pi power-off route in the available source, and no universal
Bluetooth power-off implementation. Neither is advertised as working.

## Infrared learning and power codes

`Alle TVs: Power` is a direct start in both Universal Remote and Infrared. It
automatically sends the existing EU and NA TV power databases once, without
learning or region selection. Identical carrier/timing sequences are skipped
using a 64-bit fingerprint. Back/ESC cancels between codes or during the 205 ms
inter-code pause. Long timings are split without losing mark/space polarity.
This path uses only the established TV lists, not the mixed fan/projector/menu
entries in the additional universal list. It provides no receiver feedback and
cannot guarantee that every brand reacts or that the final state is on or off:
many TV codes toggle power, and different matching codes may toggle a TV again.

The Universal Remote menu now links to IR learning, saved IR remotes and the
existing TV-B-Gone power-code selection. These codes cannot cover every TV and
some power codes toggle power instead of guaranteeing off.

`IR RAW Learn` preserves raw mode even when a protocol is recognized. Select
30/33/36/38/40/56 kHz according to your remote; the receiver cannot measure the
original carrier. Captured timings and the selected carrier are saved together
and used for replay. Normal decoded learning remains available. Overflowed,
truncated captures are rejected instead of being saved as complete signals.
RAW mode does not learn Bluetooth commands or overcome receiver frequency limits.

## Validation

The Actions workflow compiles the actual target, runs C++ record corruption/bounds
and sorting regressions with sanitizers, then verifies partition bounds, both OTA
slots, initial OTA metadata, ESP32-S3 image type and the factory image's embedded
bootloader/table/app. SHA-256 hashes and commit ID are in `build-manifest.json`.
No hardware tests are claimed: radio reception/replay, Nanoleaf discovery/pairing,
physical controls and a complete OTA/reboot cycle must be checked on the device.

References: [Nanoleaf pairing](https://support.nanoleaf.me/hc/en-us/articles/41108368751892-API-Authentication-Security),
[Nanoleaf OpenAPI](https://nanoleaf.atlassian.net/wiki/spaces/NOAD1/pages/2789310530),
[RF24 API](https://rf24.readthedocs.io/en/v1.4.11/classRF24.html).
