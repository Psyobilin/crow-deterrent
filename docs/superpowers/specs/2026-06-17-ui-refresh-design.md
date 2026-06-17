# UI-Verfeinerung Dashboard — Design

**Datum:** 2026-06-17
**Datei:** `src/main.cpp` (die beiden HTML-Strings `DASHBOARD_HTML` und `UPDATE_HTML`)

## Ziel

Das bestehende dunkle Dashboard optisch verfeinern — premium, ruhig, **mobil-first** (die Seite wird fast ausschließlich am Handy aufgerufen). Rein optische Überarbeitung: **keine** Funktions-, Verhaltens- oder Endpoint-Änderung.

## Richtung (vom User bestätigt)

- Dunkles Theme bleibt, wird poliert.
- **Ruhiger, edler Akzent:** Indigo (~`#6366f1`) als Marken-/Bedien-Akzent — bewusst nicht Türkis/Emerald, da der „AKTIV"-Status bereits grün ist und klar getrennt bleiben soll.
- Mobil-first: große Tap-Flächen, gute Lesbarkeit, Daumen-Reichweite.

## Gestaltung

### Farbsystem
- **Hintergrund:** tiefes, ruhiges Dunkelblau-Schwarz.
- **Karten:** leicht abgesetzte Fläche mit zarter 1px-Kante (rgba) + weichem Schatten für Tiefe.
- **Akzent (Bedienung):** Indigo `#6366f1` für primäre Buttons, Slider, Links.
- **Bedeutungsfarben:** Grün = aktiv/bereit · Rot = inaktiv/Alarm · Amber/Gelb = Cooldown läuft.
- **Text:** heller Grauton primär, gedämpfter Grauton für Labels.

### Typografie
- System-Font-Stack (`-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif`) statt Arial — native, scharfe Darstellung am Handy, keine Web-Fonts (Offline-Betrieb).
- Hierarchie: kleine, dezent gesperrte Labels in Großbuchstaben über großen, fetten Werten.

### Layout & Touch
- Einspaltig, großzügigere Abstände, einheitlicher Eck-Radius.
- Buttons mind. ~48px hoch; dickere Slider-Griffe (Touch-freundlich).
- Safe-Area-Insets (`env(safe-area-inset-*)`), damit Inhalt nicht unter Notch/Home-Leiste rutscht.
- Status-Karte oben als „Held": Status-Punkt + AKTIV/INAKTIV + Uhrzeit, klar und ruhig.

### Buttons vereinheitlichen
Statt konkurrierender Farben (rot/grün/blau/orange/lila):
- **Primär** = Indigo (gefüllt)
- **Sekundär** = dezent (gedämpfte Fläche / Umriss)
- **Gefährlich** (WLAN-Reset) = Rot
- **An/Aus-Toggle** = grün/rot je nach Zustand

### Zweite Seite
`UPDATE_HTML` (Firmware-Update `/update`) wird optisch mitgezogen, damit beide Seiten aus einem Guss sind.

## Harte Rahmenbedingungen (nicht brechen)

1. **Eigenständig & offline:** alles inline im `<style>`/`<script>`; keine externen Schriften, CDNs oder Bibliotheken. Das Favicon-Daten-SVG bleibt.
2. **Funktion unangetastet:** alle HTTP-Endpoints (`/status`, `/toggle`, `/volume`, `/cooldown`, `/soundcount`, `/setname`, `/checkupdate`, `/update`, `/resetwifi`) und das gesamte JavaScript-Verhalten bleiben gleich.
3. **Alle Element-IDs bleiben erhalten** (das JS spricht sie an):
   - Dashboard: `dn, dot, stxt, clk, tbtn, cdh, cdv, cdm, bar, cds, vv, vls, sc, nn, nmsg, tc1, tc2, t1, t2, la, ls, fwv, otas, cub`
   - Update-Seite: `dn, file, upbtn, barbg, bar, msg`
4. **Vom JS zur Laufzeit gesetzte CSS-Klassen müssen im neuen Stil weiter wirken:**
   - `dot` + `on`/`off` (Status-Punkt inkl. Glow-Animation)
   - `btn` + `red`/`green` (Toggle-Button wechselt die Klasse)
   - `brfl` + `ok` (Cooldown-Fortschrittsbalken, „bereit"-Zustand)
   - `blink` (Flash-Animation der Statistik-Kacheln bei neuem Alarm)
   - sichtbar/unsichtbar-Logik der Update-Fortschrittsanzeige (`barbg` display-Umschaltung)
5. **Alle `onclick`-Aufrufe und `id`-Referenzen** in der bestehenden Struktur bleiben funktional identisch (`act(...)`, `setName()`, `checkUpd()`, `rwifi()`, `doUpload()`).
6. **Flash-Budget:** aktuell ~82% belegt; CSS-Text ist klein, im Blick behalten, aber unkritisch.

## Erfolgskriterien

1. Dashboard sieht spürbar hochwertiger/ruhiger aus (Indigo-Akzent, System-Font, klare Hierarchie, Tiefe durch Kanten/Schatten).
2. Am Handy: bequeme Tap-Flächen, nichts unter Notch/Home-Leiste, gut lesbar.
3. **Alle bestehenden Funktionen arbeiten unverändert** — Live-Updates, Slider, Toggle, Rename, Cooldown-Countdown, Statistik-Flash, OTA-Knopf, Update-Seite.
4. Update-Seite passt optisch zum Dashboard.
5. Build `pio run` erfolgreich; Seite wird offline vom Gerät ausgeliefert (keine externen Ressourcen).
