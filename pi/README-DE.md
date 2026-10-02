# Erweiterung des vorhandenen Raspberry-Pi-TV-Servers

Diese Datei ersetzt deinen laufenden Server nicht. Joyn, Senderlisten und die
bisherigen Routen bleiben dort bestehen. Die Firmware fragt zuerst
`GET /remote/capabilities` ab; ohne diese Erweiterung sind die neuen Navigationstasten
und TV-AUS nicht verfuegbar. Alle bisherigen TV-Funktionen funktionieren weiterhin.

1. Bestehende Serverdatei sichern. `remote_routes.py` neben sie kopieren.
2. Nach `app = Flask(...)`, aber vor `app.run(...)` bzw. vor dem WSGI-Start ergaenzen:

```python
from remote_routes import register_remote
register_remote(app, tv_serial="BESTEHENDE_TV_ADRESSE:ADB_PORT")
```

Die bereits funktionierende ADB-Adresse deines Xiaomi verwenden, **nicht** die
Pi-Adresse. Alternativ nutzt `register_remote(app)` die vorhandene Umgebungsvariable
`JOYN_TV_ADB`. Ein vorhandenes anders benanntes Flask-Objekt entsprechend einsetzen.
Der Pi-Dienst muss mit dem bereits am TV autorisierten ADB-Benutzer laufen.

3. Den bestehenden Dienst neu starten; keinen zweiten Server auf Port 5050 starten.
4. Ohne den Fernseher zu schalten pruefen:

```sh
curl --fail http://127.0.0.1:5050/remote/capabilities
```

Erwartet: `version: 1` und `keys` mit up/down/left/right/ok/off.
Die Firmware sendet ausgewaehlte Tasten per POST an `/remote/key/<taste>`.
`off` verwendet Android KEYCODE_SLEEP (223): Standby, kein Power-Umschalter und
kein Trennen vom Strom. Die Wirkung am konkreten Xiaomi muss getestet werden.
Ein positiver HTTP-Status bestaetigt den ADB-Befehl, nicht das Fernsehbild.

Keine automatische Wiederholung von Tasten. Offline/fehlende Autorisierung ergibt
503, fehlgeschlagene Ausfuehrung 502, Timeout 504 und gleichzeitig laufender Befehl
409. Die Erweiterung uebernimmt das bestehende lokale Heimnetz-Zugriffsmodell.

Rueckbau: Registrierungszeilen entfernen und bestehenden Dienst neu starten.
Quelle fuer Sleep: https://developer.android.com/reference/android/view/KeyEvent#KEYCODE_SLEEP
