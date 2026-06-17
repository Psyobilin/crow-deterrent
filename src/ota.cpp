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
    // 404 = das Repo hat (noch) kein Release – kein echter Fehler.
    statusText = (code == 404) ? "Noch kein Release"
                               : "Fehler (HTTP " + String(code) + ")";
    http.end();
    return false;
  }

  // Nur die benötigten Felder aus der (großen) Antwort filtern.
  JsonDocument filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;

  JsonDocument doc;
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
  bootChecked = true; // verhindert eine redundante Boot-Prüfung danach
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
