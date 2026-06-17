# Automatisches OTA-Update aus GitHub — Design

**Datum:** 2026-06-17
**Repo:** `Psyobilin/crow-deterrent` (öffentlich)
**Board:** LOLIN S2 Mini (ESP32-S2), PlatformIO / Arduino

## Ziel

Die Geräte stehen an schwer erreichbaren Standorten und laufen dauerhaft. Wenn
neuer Programmcode veröffentlicht wird, sollen sich die Geräte die neue Firmware
**selbstständig aus GitHub** holen und flashen — ohne dass jemand vor Ort sein
oder die Datei manuell hochladen muss.

## Überblick des Workflows

```
Code ändern  →  git tag vX.Y.Z  →  git push --tags
                                        │
                                        ▼
                         GitHub Action baut firmware.bin
                                        │
                                        ▼
                    GitHub-Release mit firmware.bin als Asset
                                        │
                                        ▼
        Gerät prüft wöchentlich  →  neuer Tag?  →  lädt & flasht  →  Neustart
```

Der Benutzer editiert **keinen** Versions-Code. Es genügt, einen Git-Tag zu
setzen und zu pushen.

## Komponenten

### 1. Versionierung über Build-Flag

Die eingebaute Firmware-Version wird **zur Build-Zeit** aus dem Git-Tag gesetzt,
damit eingebaute Version und Release-Tag immer übereinstimmen.

- In `platformio.ini` ein Default-Flag für lokale Builds:
  `build_flags = -DFIRMWARE_VERSION='"dev"'`
- Die GitHub Action überschreibt das Flag mit dem Tagnamen
  (`${GITHUB_REF_NAME}`, z.B. `v1.2.0`).
- Im Code abgesichert:
  ```cpp
  #ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION "dev"
  #endif
  ```

### 2. GitHub Action (`.github/workflows/release.yml`)

- **Trigger:** Push eines Tags der Form `v*`.
- **Schritte:**
  1. Repo auschecken.
  2. Python + PlatformIO einrichten.
  3. Build mit injiziertem Versions-Flag:
     `platformio run` mit `PLATFORMIO_BUILD_FLAGS=-DFIRMWARE_VERSION='"${GITHUB_REF_NAME}"'`.
  4. Release anlegen und `.pio/build/lolin_s2_mini/firmware.bin` anhängen
     (z.B. via `softprops/action-gh-release`).

### 3. OTA-Modul auf dem Gerät (`src/ota.h` / `src/ota.cpp`)

Eigenes, klar abgegrenztes Modul. Schnittstelle zu `main.cpp`:

| Funktion            | Zweck                                                         |
|---------------------|--------------------------------------------------------------|
| `otaTick()`         | In `loop()` aufgerufen; verwaltet das wöchentliche Timing und den Boot-Check, ohne zu blockieren. |
| `otaCheckNow()`     | Manueller Anstoß (vom Dashboard-Knopf); führt sofort eine Prüfung+Update durch. |
| `otaCurrentVersion()` | Liefert `FIRMWARE_VERSION` für die Statusanzeige.          |
| `otaStatus()`       | Kurzer Textstatus (z.B. „aktuell", „Update läuft", „Fehler") für das Dashboard. |

**Ablauf einer Prüfung:**

1. HTTPS-GET auf `https://api.github.com/repos/Psyobilin/crow-deterrent/releases/latest`
   mit Header `User-Agent: crow-deterrent` (von GitHub verlangt).
2. JSON parsen (ArduinoJson mit Filter, um nur `tag_name` und die `.bin`-Asset-URL
   aus `assets[].browser_download_url` zu extrahieren — die Antwort ist groß).
3. Wenn `tag_name` ≠ `FIRMWARE_VERSION`: Update durchführen.
4. `httpUpdate.update(client, binUrl)` mit `WiFiClientSecure` und `setInsecure()`.
   Redirects aktivieren (`httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS)`),
   da GitHub-Asset-Downloads auf eine CDN-Domain umgeleitet werden.
5. Bei Erfolg startet das Gerät automatisch neu (übernimmt `httpUpdate`).

**Sicherheit:** `setInsecure()` — keine Zertifikatsprüfung. Bewusst gewählt, weil
Asset-Downloads über wechselnde CDN-Domains laufen und Cert-Pinning dadurch
fragil wäre. Restrisiko (manipulierte Firmware durch Angreifer im selben Netz)
ist für dieses Gerät vertretbar.

**Versionsvergleich:** einfache Ungleichheit (`tag_name != FIRMWARE_VERSION`).
Ein abweichender Tag löst ein Update aus — kein semantisches „neuer als".

### 4. Zeitsteuerung

- Nicht-blockierender Timer über `millis()`.
- Prüfung **alle 2 Tage**. `unsigned long` reicht problemlos (Überlauf erst nach
  ~49 Tagen; wird durch das Zurücksetzen alle 2 Tage nie erreicht).
- Zusätzlich **eine Prüfung ~60 s nach dem Boot** (deckt seltene Neustarts /
  Stromausfälle ab, ohne den Start zu verzögern).

### 5. Dashboard-Ergänzungen (`main.cpp`)

- Anzeige der **aktuellen Firmware-Version** (aus `/status`-JSON, neues Feld
  `firmwareVersion`).
- Anzeige des **OTA-Status** (neues Feld `otaStatus`).
- Neuer Knopf **„Jetzt auf Updates prüfen"** → ruft neuen Endpoint
  `/checkupdate` auf, der `otaCheckNow()` anstößt.
- Die bestehende manuelle Upload-OTA (`/update`) **bleibt unverändert** als
  Fallback erhalten.

### 6. Abhängigkeiten

- Neu in `lib_deps`: `bblanchon/ArduinoJson` (für robustes Parsen der GitHub-API-Antwort).
- `HTTPUpdate`, `WiFiClientSecure`, `HTTPClient` sind Teil des ESP32-Arduino-Cores
  (keine zusätzliche Abhängigkeit).

## Bewusst NICHT enthalten (YAGNI)

- Kein Rollback-/Fallback-Mechanismus bei kaputter Firmware.
- Keine Beta-/Release-Kanäle.
- Keine Signatur-/Hash-Prüfung der Firmware.
- Kein Token/Authentifizierung (Repo ist öffentlich).

## Erfolgskriterien

1. Ein Push von `git tag vX.Y.Z` erzeugt automatisch ein GitHub-Release mit
   angehängter `firmware.bin`.
2. Das Dashboard zeigt die aktuelle Firmware-Version korrekt an.
3. Der Knopf „Jetzt prüfen" löst bei vorhandenem neuem Release ein Update aus;
   das Gerät flasht und startet mit der neuen Version neu.
4. Ohne neues Release passiert beim Prüfen nichts (kein unnötiges Flashen).
5. Die Prüfung alle 2 Tage läuft automatisch, ohne den normalen Betrieb
   (PIR-Erkennung, Sound, Dashboard) zu blockieren.
