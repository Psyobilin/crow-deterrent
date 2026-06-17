# UI-Verfeinerung Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Das dunkle Dashboard und die Update-Seite optisch verfeinern (premium, mobil-first, Indigo-Akzent) — ohne jede funktionale Änderung.

**Architecture:** Rein gestalterische Überarbeitung der beiden HTML-Strings `DASHBOARD_HTML` und `UPDATE_HTML` in `src/main.cpp`. Nur `<style>`-Blöcke und statische HTML-Struktur/Klassen werden überarbeitet; sämtliche Element-IDs, `onclick`-Handler, das JavaScript und alle vom JS zur Laufzeit gesetzten CSS-Klassen bleiben funktional identisch.

**Tech Stack:** Inline-HTML/CSS (kein externes Asset, Offline-Auslieferung vom ESP32), System-Font-Stack.

## Global Constraints

- **Offline & eigenständig:** alles inline; keine externen Schriften, CDNs, Bibliotheken. Favicon-Daten-SVG bleibt.
- **Keine Verhaltens-/Endpoint-Änderung:** `/status, /toggle, /volume, /cooldown, /soundcount, /setname, /checkupdate, /update, /resetwifi` unverändert; das gesamte JavaScript bleibt funktional gleich.
- **Akzent:** Indigo `#6366f1` (Bedienung). Bedeutungsfarben: Grün = aktiv/bereit, Rot = inaktiv/Alarm, Amber/Gelb = Cooldown.
- **Typografie:** System-Font-Stack `-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif`. Keine Web-Fonts.
- **Mobil-first:** Buttons ≥ 48px hoch, dickere Slider-Griffe, `env(safe-area-inset-*)`, `max-width:480px` zentriert.
- **Diese Element-IDs MÜSSEN erhalten bleiben** (das JS spricht sie an):
  - Dashboard: `dn, dot, stxt, clk, tbtn, cdh, cdv, cdm, bar, cds, vv, vls, sc, nn, nmsg, tc1, tc2, t1, t2, la, ls, fwv, otas, cub`
  - Update-Seite: `dn, file, upbtn, barbg, bar, msg`
- **Diese vom JS gesetzten CSS-Klassen MÜSSEN im neuen Stil weiter wirken:**
  - `dot` + `on`/`off` (Status-Punkt inkl. Glow), `btn` + `red`/`green` (Toggle), `brfl` + `ok` (Cooldown-Balken „bereit"), `blink` (Statistik-Flash), `gray`/`blue` (Update-Button-Zustände), `barbg` display-Umschaltung.
- **Diese `onclick`/Funktionsaufrufe bleiben unverändert:** `act('/toggle')`, `act('/volume?v='+...)`, `act('/cooldown?c='+...)`, `act('/soundcount?c='+...)`, `setName()`, `checkUpd()`, `rwifi()`, `doUpload()`.
- **Verifikation:** kein Unit-Test-Framework. Prüf-Schritt je Task = erfolgreiches `pio run -e lolin_s2_mini`. Sicht-Test im Browser/am Handy in der Abschluss-Verifikation.
- **Flash-Budget:** aktuell ~82%; CSS-Text klein, aber im Blick behalten.

---

## File Structure

| Datei | Verantwortung | Aktion |
|---|---|---|
| `src/main.cpp` → `DASHBOARD_HTML` | Restyle der Hauptseite (Struktur/CSS), IDs/Klassen/Handler erhalten | Modify |
| `src/main.cpp` → `UPDATE_HTML` | Restyle der Update-Seite passend zum Dashboard | Modify |

Beide Strings liegen in derselben Datei; getrennte Tasks, weil ein Reviewer die Dashboard-Überarbeitung unabhängig von der Update-Seite annehmen/ablehnen kann.

---

### Task 1: Dashboard-Seite (`DASHBOARD_HTML`) verfeinern

**Files:**
- Modify: `src/main.cpp` (String `DASHBOARD_HTML`, derzeit ca. Zeilen 87–313)

**Interfaces:**
- Consumes: nichts.
- Produces: optisch überarbeitetes Dashboard; unveränderte ID-/Klassen-/Handler-Oberfläche für das bestehende JS.

- [ ] **Step 1: Bestandsaufnahme der Funktions-Anker**

Lies `DASHBOARD_HTML` und das zugehörige `<script>` vollständig. Notiere jede `id`, jede vom JS gesetzte Klasse und jeden `onclick`. Diese Liste ist dein Vertrag (siehe Global Constraints). Nichts davon darf beim Restyle verloren gehen oder umbenannt werden.

- [ ] **Step 2: `<style>`-Block neu gestalten**

Ersetze ausschließlich den Inhalt des `<style>`-Blocks und passe nur dort nötige Klassennamen/Struktur an, wo es das Design verlangt — unter strikter Wahrung der oben gelisteten JS-relevanten Klassen. Setze die Design-Vorgaben der Spec um:
- Indigo `#6366f1` als Primär-/Bedien-Akzent (Buttons `.blue`→Indigo, Slider `accent-color`, Links).
- Tiefer dunkler Hintergrund; Karten mit 1px-Kante (`rgba(255,255,255,.06)`) + weichem Schatten.
- System-Font-Stack; Labels klein + leicht gesperrt (`letter-spacing`) in Versalien, Werte groß/fett.
- Bedeutungsfarben beibehalten: `.green/.dot.on/.brfl.ok` grün, `.red/.dot.off` rot, Cooldown-Akzent amber.
- Buttons mind. 48px Höhe; Slider-Thumb vergrößern (`::-webkit-slider-thumb`).
- `body{padding:env(safe-area-inset-...)}` bzw. äquivalente Safe-Area-Behandlung; `max-width:480px` zentriert.
- Button-Hierarchie vereinheitlichen: Primär = Indigo gefüllt; Sekundär = gedämpfte Fläche/Umriss; `.orange` (WLAN-Reset) = Rot/„gefährlich"; Toggle nutzt weiter `.red`/`.green`.

- [ ] **Step 3: HTML-Struktur behutsam anpassen**

Falls für die neue Optik nötig (z. B. Wrapper, Icon-Spans, geänderte Button-Klassen), passe die statische Struktur an — aber:
- KEINE `id` entfernen/umbenennen aus der Vertragsliste.
- KEINEN `onclick`/Funktionsaufruf ändern.
- Den `<script>`-Block NICHT in seinem Verhalten ändern (höchstens unveränderte ID-Zugriffe bleiben gültig).

- [ ] **Step 4: Build prüfen**

Run: `pio run -e lolin_s2_mini`
Expected: `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "UI: Dashboard verfeinert (Dark, mobil-first, Indigo-Akzent)"
```

---

### Task 2: Update-Seite (`UPDATE_HTML`) angleichen

**Files:**
- Modify: `src/main.cpp` (String `UPDATE_HTML`, derzeit ca. Zeilen 316–404)

**Interfaces:**
- Consumes: das Farb-/Typo-System aus Task 1 (gleiche Werte verwenden für ein einheitliches Bild).
- Produces: optisch zum Dashboard passende Update-Seite; unveränderte IDs/Handler.

- [ ] **Step 1: Funktions-Anker notieren**

Lies `UPDATE_HTML` samt `<script>`. Erhalte: IDs `dn, file, upbtn, barbg, bar, msg`; Klassen `btn/blue/gray`, `brfl`, `barbg`-Display-Logik; Aufruf `doUpload()` und der `fetch('/status')`-Block.

- [ ] **Step 2: `<style>`-Block an das Dashboard angleichen**

Übernimm dieselbe Farbwelt, denselben Font-Stack, Karten-Look (Kante+Schatten), Button-Stil (Indigo-Primär, ≥48px) und Safe-Area-Behandlung wie in Task 1. `.blue` (Upload-Button) = Indigo; `.gray` (deaktiviert) gedämpft; `.brfl` (Fortschritt) Indigo/Amber. „Zurück zum Dashboard"-Link im Akzent.

- [ ] **Step 3: HTML behutsam anpassen**

Nur optische Struktur; IDs und `doUpload()`/`onclick` unverändert lassen.

- [ ] **Step 4: Build prüfen**

Run: `pio run -e lolin_s2_mini`
Expected: `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "UI: Update-Seite an neues Dashboard-Design angeglichen"
```

---

### Task 3: Sicht-Verifikation (Browser/Handy)

**Files:**
- keine Code-Änderung (reine Prüfung)

**Interfaces:**
- Consumes: Ergebnis aus Tasks 1–2.

- [ ] **Step 1: Funktionsanker-Gegencheck**

Vergleiche die finale `src/main.cpp` gegen die Vertragslisten aus den Global Constraints: jede gelistete `id` und JS-Klasse ist noch vorhanden (z. B. per Suche). Erwartung: alle vorhanden.

- [ ] **Step 2: Sicht- und Funktionstest am Gerät/Handy**

Nach Aufspielen (USB oder OTA) das Dashboard am Handy öffnen und prüfen:
1. Optik: dunkel, Indigo-Akzent, klare Hierarchie, Tiefe (Kanten/Schatten), saubere Tap-Flächen, nichts unter Notch/Home-Leiste.
2. Funktion unverändert: Live-Status (Punkt + AKTIV/INAKTIV), Toggle wechselt Farbe, Lautstärke-/Cooldown-Slider wirken, Cooldown-Countdown + Balken laufen, Soundanzahl speichern, Gerätename ändern (inkl. Verschwinden der „startet neu"-Meldung), Statistik-Flash bei neuem Alarm, Firmware-Version + OTA-Status, „Jetzt prüfen", „Firmware manuell hochladen" → Update-Seite passt optisch und der Upload funktioniert, WLAN-Reset.

Erst wenn Optik UND alle Funktionen stimmen, gilt die UI-Überarbeitung als fertig.

---

## Self-Review

**Spec-Abdeckung:**
- Farbsystem/Indigo-Akzent → Task 1 Step 2, Task 2 Step 2. ✅
- Typografie (System-Font, Hierarchie) → Task 1 Step 2. ✅
- Layout & Touch (48px, Slider-Thumb, Safe-Area, einspaltig) → Task 1 Step 2. ✅
- Buttons vereinheitlichen → Task 1 Step 2. ✅
- Update-Seite angleichen → Task 2. ✅
- Harte Rahmenbedingungen (IDs/Klassen/Handler/Offline) → Global Constraints + Step-1-Verträge in Tasks 1/2 + Task 3 Step 1. ✅
- Build- & Sicht-Verifikation → Steps „Build prüfen" + Task 3. ✅

**Platzhalter:** Keine inhaltlichen TBDs. Die konkrete CSS-Ausgestaltung ist bewusst gestalterischer Spielraum innerhalb fester Vorgaben (Akzent, Font, Touch-Maße, erhaltene Klassen) — das ist Design-Arbeit, kein abzutippender Code.

**Konsistenz:** Die Vertragslisten (IDs/Klassen/Handler) sind in Global Constraints, Task 1 Step 1, Task 2 Step 1 und Task 3 Step 1 identisch referenziert.
