# GitHub Auto-OTA Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Die Geräte prüfen selbstständig alle 2 Tage GitHub auf eine neue Firmware-Version, laden sie herunter und flashen sie — ohne Vor-Ort-Eingriff.

**Architecture:** Eine GitHub Action baut bei jedem Versions-Tag (`v*`) die `firmware.bin` und hängt sie an ein GitHub-Release. Ein neues OTA-Modul auf dem Gerät fragt periodisch die GitHub-Releases-API ab, vergleicht den Release-Tag mit der eingebauten Firmware-Version und flasht bei Abweichung per `httpUpdate` über HTTPS. Das Modul ist von `main.cpp` getrennt und über eine kleine Schnittstelle angebunden.

**Tech Stack:** PlatformIO / Arduino, ESP32-S2 (LOLIN S2 Mini), ESP32-Arduino-Core (`HTTPClient`, `WiFiClientSecure`, `HTTPUpdate`), ArduinoJson.

## Global Constraints

- Board / Build-Environment: `lolin_s2_mini` (siehe `platformio.ini`).
- Repo (öffentlich): `Psyobilin/crow-deterrent`. Releases-API:
  `https://api.github.com/repos/Psyobilin/crow-deterrent/releases/latest`.
- GitHub verlangt bei API-Anfragen einen `User-Agent`-Header: `crow-deterrent`.
- HTTPS ohne Zertifikatsprüfung: `WiFiClientSecure::setInsecure()`.
- Firmware-Version kommt als Build-Flag `FIRMWARE_VERSION` (C-String inkl. Anführungszeichen). Default ohne Flag: `"dev"` (über `#ifndef`-Guard im Code).
- Versionsvergleich: einfache Ungleichheit (`tag_name != FIRMWARE_VERSION`).
- Prüf-Intervall: alle 2 Tage (`172800000` ms) + eine Prüfung ~60 s nach Boot.
- Der Gerätename liegt in NVS (`Preferences`, Namespace `kraehe`, Key `deviceName`) und darf von OTA NICHT berührt werden — OTA flasht nur die App-Partition. Keine namensbezogene Logik im OTA-Modul.
- Bestehende manuelle Upload-OTA (`/update`) bleibt unverändert erhalten.
- **Verifikation:** Es gibt kein Unit-Test-Framework. Prüf-Schritt nach jeder Code-Aufgabe ist erfolgreiches Kompilieren (`pio run -e lolin_s2_mini`). Abschluss-Aufgabe ist eine manuelle Prüfung am echten Gerät.

---

## File Structure

| Datei | Verantwortung | Aktion |
|---|---|---|
| `platformio.ini` | ArduinoJson als Abhängigkeit ergänzen | Modify |
| `.github/workflows/release.yml` | Firmware bei Tag-Push bauen + Release erstellen | Create |
| `src/ota.h` | Öffentliche Schnittstelle des OTA-Moduls | Create |
| `src/ota.cpp` | OTA-Logik: GitHub abfragen, vergleichen, flashen, Timing | Create |
| `src/main.cpp` | OTA-Modul einbinden, Timing-Tick, `/checkupdate`-Endpoint, Status-JSON, Dashboard-UI | Modify |
| `Readme.md` | Neuen automatischen Update-Workflow dokumentieren | Modify |

---

### Task 1: Abhängigkeit ArduinoJson ergänzen

**Files:**
- Modify: `platformio.ini:17-20`

**Interfaces:**
- Consumes: nichts.
- Produces: ArduinoJson-Bibliothek verfügbar für `src/ota.cpp`.

- [ ] **Step 1: ArduinoJson zu `lib_deps` hinzufügen**

Ersetze den `lib_deps`-Block in `platformio.ini`:

```ini
lib_deps =
    DFRobotDFPlayerMini
    tzapu/WiFiManager
    bblanchon/ArduinoJson
```

- [ ] **Step 2: Build prüfen (lädt die neue Bibliothek)**

Run: `pio run -e lolin_s2_mini`
Expected: Build erfolgreich (`SUCCESS`). PlatformIO lädt ArduinoJson beim ersten Build automatisch herunter.

- [ ] **Step 3: Commit**

```bash
git add platformio.ini
git commit -m "Build: ArduinoJson für OTA-JSON-Parsing ergänzen"
```

---

### Task 2: GitHub Action für automatischen Release-Build

**Files:**
- Create: `.github/workflows/release.yml`

**Interfaces:**
- Consumes: nichts (eigenständige CI).
- Produces: Bei Push eines Tags `v*` ein GitHub-Release mit angehängter `firmware.bin`, gebaut mit `-DFIRMWARE_VERSION='"<tag>"'`.

- [ ] **Step 1: Workflow-Datei anlegen**

Erstelle `.github/workflows/release.yml` mit exakt diesem Inhalt:

```yaml
name: Build and Release Firmware

on:
  push:
    tags:
      - 'v*'

permissions:
  contents: write

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.x'

      - name: Install PlatformIO
        run: pip install platformio

      - name: Build firmware
        env:
          PLATFORMIO_BUILD_FLAGS: -DFIRMWARE_VERSION='"${{ github.ref_name }}"'
        run: pio run -e lolin_s2_mini

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          files: .pio/build/lolin_s2_mini/firmware.bin
```

- [ ] **Step 2: YAML-Syntax lokal prüfen**

Run: `python -c "import yaml,sys; yaml.safe_load(open('.github/workflows/release.yml')); print('YAML OK')"`
Expected: `YAML OK`

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "CI: Firmware bei Versions-Tag bauen und Release anlegen"
```

> **Hinweis:** Die vollständige Wirkung (Release entsteht) lässt sich erst nach dem ersten echten Tag-Push prüfen — das passiert in der Abschluss-Aufgabe (Task 6).

---

### Task 3: OTA-Modul Schnittstelle (`ota.h`)

**Files:**
- Create: `src/ota.h`

**Interfaces:**
- Consumes: nichts.
- Produces (von `main.cpp` und `ota.cpp` genutzt):
  - `void otaTick();` — in `loop()` aufgerufen, verwaltet Timing nicht-blockierend.
  - `void otaCheckNow();` — sofortige Prüfung + ggf. Update (Dashboard-Knopf).
  - `String otaCurrentVersion();` — liefert `FIRMWARE_VERSION`.
  - `String otaStatus();` — kurzer Textstatus fürs Dashboard.
  - Makro `FIRMWARE_VERSION` (Default `"dev"`, falls nicht per Build-Flag gesetzt).

- [ ] **Step 1: Header anlegen**

Erstelle `src/ota.h` mit exakt diesem Inhalt:

```cpp
#ifndef OTA_H
#define OTA_H

#include <Arduino.h>

// Wird zur Build-Zeit per -DFIRMWARE_VERSION gesetzt (GitHub Action).
// Lokale Builds ohne Flag laufen als "dev".
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

// Muss regelmäßig in loop() aufgerufen werden. Nicht-blockierend, außer im
// Moment einer tatsächlichen Prüfung/eines Updates.
void otaTick();

// Stößt sofort eine Prüfung + ggf. Update an (Dashboard-Knopf).
void otaCheckNow();

// Aktuelle eingebaute Firmware-Version (für die Statusanzeige).
String otaCurrentVersion();

// Kurzer Textstatus fürs Dashboard ("Bereit", "Prüfe...", "Aktuell",
// "Update läuft...", "Fehler").
String otaStatus();

#endif
```

- [ ] **Step 2: Build prüfen (Header wird noch nicht eingebunden, muss aber valide sein)**

Run: `pio run -e lolin_s2_mini`
Expected: Build erfolgreich (Header allein ändert nichts am Build, solange er nicht inkludiert ist — dient hier als Syntaxkontrolle für die nächste Aufgabe).

- [ ] **Step 3: Commit**

```bash
git add src/ota.h
git commit -m "OTA: Schnittstelle des OTA-Moduls (ota.h)"
```

---

### Task 4: OTA-Logik (`ota.cpp`)

**Files:**
- Create: `src/ota.cpp`

**Interfaces:**
- Consumes: `ota.h` (alle Deklarationen), `FIRMWARE_VERSION`.
- Produces: Implementierung von `otaTick`, `otaCheckNow`, `otaCurrentVersion`, `otaStatus`.

- [ ] **Step 1: Implementierung anlegen**

Erstelle `src/ota.cpp` mit exakt diesem Inhalt:

```cpp
#include "ota.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

// ── Konfiguration ─────────────────────────────────────────
static const char* RELEASES_URL =
  "https://api.github.com/repos/Psyobilin/crow-deterrent/releases/latest";
static const char* USER_AGENT = "crow-deterrent";

static const unsigned long CHECK_INTERVAL_MS = 172800000UL; // 2 Tage
static const unsigned long BOOT_CHECK_DELAY_MS = 60000UL;   // 60 s nach Boot

// ── Interner Zustand ──────────────────────────────────────
static unsigned long lastCheck = 0;
static bool bootChecked = false;
static String statusText = "Bereit";

String otaCurrentVersion() {
  return String(FIRMWARE_VERSION);
}

String otaStatus() {
  return statusText;
}

// Holt das neueste Release. Schreibt Tag in latestTag und die .bin-URL in
// binUrl. Gibt true zurück, wenn beides gefunden wurde.
static bool fetchLatestRelease(String& latestTag, String& binUrl) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, RELEASES_URL)) {
    statusText = "Fehler";
    return false;
  }
  http.addHeader("User-Agent", USER_AGENT);

  int code = http.GET();
  if (code != 200) {
    statusText = "Fehler (HTTP " + String(code) + ")";
    http.end();
    return false;
  }

  // Nur die benötigten Felder aus der (großen) Antwort filtern.
  StaticJsonDocument<256> filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(
    doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();

  if (err) {
    statusText = "Fehler (JSON)";
    return false;
  }

  latestTag = doc["tag_name"].as<String>();
  binUrl = "";
  for (JsonObject asset : doc["assets"].as<JsonArray>()) {
    String name = asset["name"].as<String>();
    if (name.endsWith(".bin")) {
      binUrl = asset["browser_download_url"].as<String>();
      break;
    }
  }
  return latestTag.length() > 0 && binUrl.length() > 0;
}

// Lädt die Firmware und flasht sie. Kehrt nur bei Misserfolg zurück
// (bei Erfolg startet das Gerät neu).
static void performUpdate(const String& binUrl) {
  statusText = "Update läuft...";

  WiFiClientSecure client;
  client.setInsecure();

  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(true);

  t_httpUpdate_return ret = httpUpdate.update(client, binUrl);
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      statusText = "Fehler: " + httpUpdate.getLastErrorString();
      break;
    case HTTP_UPDATE_NO_UPDATES:
      statusText = "Aktuell";
      break;
    case HTTP_UPDATE_OK:
      statusText = "OK"; // wird durch Neustart praktisch nie sichtbar
      break;
  }
}

static void runCheck() {
  if (WiFi.status() != WL_CONNECTED) {
    statusText = "Kein WLAN";
    return;
  }
  statusText = "Prüfe...";

  String latestTag, binUrl;
  if (!fetchLatestRelease(latestTag, binUrl)) {
    // statusText wurde in fetchLatestRelease gesetzt
    return;
  }

  if (latestTag == String(FIRMWARE_VERSION)) {
    statusText = "Aktuell";
    return;
  }

  performUpdate(binUrl);
}

void otaCheckNow() {
  runCheck();
  lastCheck = millis();
}

void otaTick() {
  if (WiFi.status() != WL_CONNECTED) return;

  unsigned long now = millis();

  // Eine Prüfung kurz nach dem Boot.
  if (!bootChecked && now >= BOOT_CHECK_DELAY_MS) {
    bootChecked = true;
    runCheck();
    lastCheck = now;
    return;
  }

  // Danach alle 2 Tage.
  if (bootChecked && (now - lastCheck >= CHECK_INTERVAL_MS)) {
    runCheck();
    lastCheck = now;
  }
}
```

- [ ] **Step 2: Build prüfen (Modul kompiliert eigenständig)**

Run: `pio run -e lolin_s2_mini`
Expected: Build erfolgreich. Das Modul ist noch nicht in `main.cpp` eingebunden, muss aber fehlerfrei kompilieren.

- [ ] **Step 3: Commit**

```bash
git add src/ota.cpp
git commit -m "OTA: GitHub-Release-Prüfung und httpUpdate-Logik"
```

---

### Task 5: Integration in `main.cpp` + Dashboard

**Files:**
- Modify: `src/main.cpp` (Include, Status-JSON, neuer Endpoint, Routen-Registrierung, `loop()`-Tick, Dashboard-HTML/JS)

**Interfaces:**
- Consumes: `ota.h` (`otaTick`, `otaCheckNow`, `otaCurrentVersion`, `otaStatus`).
- Produces: HTTP-Endpoint `GET /checkupdate`; Status-JSON-Felder `firmwareVersion` und `otaStatus`; Dashboard zeigt Version + Status + „Jetzt prüfen"-Knopf.

- [ ] **Step 1: OTA-Header einbinden**

In `src/main.cpp` nach den bestehenden Includes (nach Zeile 9, `#include "time.h"`) ergänzen:

```cpp
#include "ota.h"
```

- [ ] **Step 2: Status-JSON um zwei Felder erweitern**

In `handleStatus()` die `lastSound`-Zeile (aktuell `main.cpp:412`) so erweitern, dass danach die zwei neuen Felder folgen. Ersetze:

```cpp
  json += "\"lastSound\":"         + String(lastSound) + ",";
  json += "\"time\":\""            + getTime() + "\"";
```

durch:

```cpp
  json += "\"lastSound\":"         + String(lastSound) + ",";
  json += "\"firmwareVersion\":\"" + otaCurrentVersion() + "\",";
  json += "\"otaStatus\":\""       + otaStatus() + "\",";
  json += "\"time\":\""            + getTime() + "\"";
```

- [ ] **Step 3: Neuen Handler für die manuelle Prüfung ergänzen**

In `src/main.cpp` direkt vor `void handleResetWifi()` (aktuell `main.cpp:461`) einfügen:

```cpp
void handleCheckUpdate() {
  server.send(200, "text/plain", "OK");
  otaCheckNow();
}
```

- [ ] **Step 4: Route registrieren**

In `setup()` nach der Zeile `server.on("/resetwifi", handleResetWifi);` (aktuell `main.cpp:540`) einfügen:

```cpp
  server.on("/checkupdate", handleCheckUpdate);
```

- [ ] **Step 5: `otaTick()` in die Schleife einhängen**

In `loop()` direkt nach `server.handleClient();` (aktuell `main.cpp:556`) einfügen:

```cpp
  otaTick();
```

- [ ] **Step 6: Dashboard — Versions-/Status-Karte und Knopf ergänzen**

Im `DASHBOARD_HTML` den bestehenden OTA-Block ersetzen. Ersetze (aktuell `main.cpp:197-199`):

```html
<div class="card">
  <a href="/update"><button class="btn purple">Firmware Update (OTA)</button></a>
</div>
```

durch:

```html
<div class="card">
  <div class="cdr">
    <div class="lbl">Firmware</div>
    <span class="cdh" id="fwv">–</span>
  </div>
  <div class="hint" id="otas" style="margin-top:2px">–</div>
  <button class="btn blue" id="cub" onclick="checkUpd()" style="margin-top:10px">Jetzt auf Updates prüfen</button>
  <a href="/update"><button class="btn purple" style="margin-top:8px">Firmware manuell hochladen</button></a>
</div>
```

- [ ] **Step 7: Dashboard — JS für Version/Status und Knopf ergänzen**

Im `DASHBOARD_HTML`-Script in der Funktion `draw(d)` am Ende (direkt vor der schließenden `}` von `draw`, aktuell vor `main.cpp:254`) einfügen:

```javascript
  document.getElementById('fwv').textContent=d.firmwareVersion;
  document.getElementById('otas').textContent='Status: '+d.otaStatus;
```

Und die neue Funktion `checkUpd()` direkt vor `function rwifi(){` (aktuell `main.cpp:285`) einfügen:

```javascript
function checkUpd(){
  var b=document.getElementById('cub');
  b.disabled=true;b.textContent='Prüfe...';
  fetch('/checkupdate').then(function(){
    setTimeout(function(){b.disabled=false;b.textContent='Jetzt auf Updates prüfen';},4000);
  });
}
```

- [ ] **Step 8: Build prüfen**

Run: `pio run -e lolin_s2_mini`
Expected: Build erfolgreich.

- [ ] **Step 9: Commit**

```bash
git add src/main.cpp
git commit -m "OTA: Auto-Update einbinden, Dashboard mit Version und Prüf-Knopf"
```

---

### Task 6: README dokumentieren + Verifikation am echten Gerät

**Files:**
- Modify: `Readme.md`

**Interfaces:**
- Consumes: das fertige System aus Tasks 1–5 und die CI aus Task 2.
- Produces: aktualisierte Doku; bestätigtes End-to-End-Verhalten am Gerät.

- [ ] **Step 1: README — Feature-Liste ergänzen**

In `Readme.md` die Feature-Zeile zur OTA (aktuell `Readme.md:22`):

```markdown
- OTA firmware update directly via browser (no USB required after first flash)
```

ersetzen durch:

```markdown
- Manual OTA firmware update via browser (upload .bin)
- Automatic OTA update from GitHub Releases — devices self-update every 2 days
```

- [ ] **Step 2: README — OTA-Abschnitt neu fassen**

In `Readme.md` den Abschnitt `## OTA Firmware Update` (aktuell `Readme.md:126-135`) ersetzen durch:

```markdown
## Firmware Updates

### Automatic OTA from GitHub (recommended)

Devices check GitHub for a new release every 2 days (and ~60 s after boot)
and flash themselves automatically. To publish a new version:

1. Commit your code changes.
2. Tag and push:
   ```bash
   git tag v1.2.0
   git push origin v1.2.0
   ```
3. A GitHub Action builds `firmware.bin` and creates a release automatically.
4. Within 2 days every device downloads and installs it. No site visit needed.

The dashboard shows the installed firmware version and an **"Jetzt auf
Updates prüfen"** button to trigger a check immediately (useful for testing).

The device name and all settings live in flash (NVS) and are **not** touched
by OTA — each device keeps its own name (`Kraehe-1`, `Kraehe-3`, …). The
firmware binary is identical for all devices and contains no name.

### Manual OTA (fallback)

1. Build in PlatformIO → generates `.pio/build/lolin_s2_mini/firmware.bin`
2. Open `kraehe-x.local/update` in the browser
3. Select the `.bin` file → **Hochladen & Flashen**
4. The device flashes itself and reboots — Gerätename and all settings are preserved
```

- [ ] **Step 3: Commit der Doku**

```bash
git add Readme.md
git commit -m "Doku: automatisches GitHub-OTA-Update beschreiben"
```

- [ ] **Step 4: End-to-End-Verifikation am echten Gerät**

Manuell durchführen und Ergebnis festhalten:

1. **Aktuelle Firmware per USB flashen** (lokaler Build, Version zeigt `dev`).
   Run: `pio run -e lolin_s2_mini -t upload`
   Dashboard öffnen → unter „Firmware" muss `dev` stehen.
2. **Gerätename setzen** (falls noch nicht) — z.B. `Kraehe-test` — im Dashboard.
3. **Release auf GitHub erzeugen:**
   ```bash
   git tag v0.0.1
   git push origin v0.0.1
   ```
   Auf GitHub unter „Actions" prüfen, dass der Build durchläuft und unter
   „Releases" ein Release `v0.0.1` mit `firmware.bin` erscheint.
4. **Update auslösen:** im Dashboard „Jetzt auf Updates prüfen" drücken.
   Erwartung: Status wechselt zu „Update läuft…", Gerät startet neu, danach
   zeigt „Firmware" `v0.0.1`.
5. **Name-Erhalt bestätigen:** Nach dem Update muss der Gerätename weiterhin
   `Kraehe-test` sein (nicht zurückgesetzt). ✅ Kernanforderung.
6. **„Kein neues Release"-Fall:** Erneut „Jetzt prüfen" drücken.
   Erwartung: Status „Aktuell", kein Flashen, kein Neustart.

Erst wenn Punkt 5 bestätigt ist, gilt das Feature als fertig.

---

## Self-Review

**Spec-Abdeckung:**
- Versionierung über Build-Flag → Task 2 (Action injiziert) + Task 3 (`#ifndef`-Guard). ✅
- GitHub Action → Task 2. ✅
- OTA-Modul (Schnittstelle + Logik) → Tasks 3, 4. ✅
- Zeitsteuerung (alle 2 Tage + Boot-Check) → Task 4 (`otaTick`). ✅
- Dashboard (Version, Status, „Jetzt prüfen", manuelle OTA bleibt) → Task 5. ✅
- ArduinoJson-Abhängigkeit → Task 1. ✅
- Namens-Erhalt über NVS → keine namensbezogene Logik im OTA-Modul (Constraint) + ausdrückliche Geräte-Verifikation Task 6, Step 4.5. ✅
- `setInsecure`, Redirects, User-Agent, Ungleichheits-Vergleich → Task 4. ✅

**Typ-/Namens-Konsistenz:** `otaTick`, `otaCheckNow`, `otaCurrentVersion`, `otaStatus` identisch in `ota.h` (Task 3), `ota.cpp` (Task 4) und `main.cpp` (Task 5). JSON-Felder `firmwareVersion`/`otaStatus` in Task 5 Step 2 erzeugt und in Step 7 gelesen — konsistent. DOM-IDs `fwv`, `otas`, `cub` in Step 6 angelegt und in Step 7 verwendet — konsistent.

**Platzhalter:** keine. Jeder Code-Schritt enthält vollständigen Code; jeder Build-Schritt einen konkreten Befehl mit Erwartung.
