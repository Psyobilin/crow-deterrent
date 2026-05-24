#include "Arduino.h"
#include "DFRobotDFPlayerMini.h"
#include "HardwareSerial.h"
#include "WiFiManager.h"
#include "ESPmDNS.h"
#include "WebServer.h"
#include "Preferences.h"
#include "time.h"

// ── Gerätebezeichnung ─────────────────────────────────────
const char* deviceName = "Kraehe-2";

// ── Pin Konfiguration ──────────────────────────────────────
#define PIR_PIN     7
#define DFPLAYER_RX 18
#define DFPLAYER_TX 17

// ── Einstellungen ──────────────────────────────────────────
int        SOUND_COUNT    = 86;
const int  NO_REPEAT_LAST = 5;
int        COOLDOWN_SEC   = 60;
int        VOLUME         = 25;

// ── Interne Variablen ──────────────────────────────────────
HardwareSerial dfSerial(1);
DFRobotDFPlayerMini dfPlayer;
WebServer server(80);
Preferences preferences;

int  lastPlayed[5]        = {-1, -1, -1, -1, -1};
unsigned long lastTriggerTime = 0;
bool cooldownActive        = false;
bool systemActive          = true;
int  totalCount            = 0;
int  todayCount            = 0;
int  lastSound             = 0;
String lastAlarmTime       = "Noch kein Alarm";
int  lastAlarmDay          = -1;

// ── NTP Zeit ──────────────────────────────────────────────
const char* ntpServer      = "pool.ntp.org";
const long  gmtOffset      = 3600;
const int   daylightOffset = 3600;

String getTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Keine Zeit";
  char buf[30];
  strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", &timeinfo);
  return String(buf);
}

int getDay() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return -1;
  return timeinfo.tm_mday;
}

// ── Hilfsfunktionen ───────────────────────────────────────
bool wasRecentlyPlayed(int soundNum) {
  for (int i = 0; i < NO_REPEAT_LAST; i++) {
    if (lastPlayed[i] == soundNum) return true;
  }
  return false;
}

int pickRandomSound() {
  int pick;
  int attempts = 0;
  do {
    pick = random(1, SOUND_COUNT + 1);
    attempts++;
  } while (wasRecentlyPlayed(pick) && attempts < 20);
  return pick;
}

void addToHistory(int soundNum) {
  for (int i = NO_REPEAT_LAST - 1; i > 0; i--) {
    lastPlayed[i] = lastPlayed[i - 1];
  }
  lastPlayed[0] = soundNum;
}

// ── Webserver Dashboard ───────────────────────────────────
void handleRoot() {
  String state    = systemActive ? "AKTIV" : "INAKTIV";
  String stateCol = systemActive ? "#2ecc71" : "#e74c3c";
  String btnText  = systemActive ? "System DEAKTIVIEREN" : "System AKTIVIEREN";
  String btnCol   = systemActive ? "#e74c3c" : "#2ecc71";
  int    volPct   = map(VOLUME, 0, 30, 0, 100);

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<link rel='icon' href='data:image/svg+xml,<svg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220 0 100 100%22><text y=%22.9em%22 font-size=%2290%22>🐦‍⬛</text></svg>'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='10'>";
  html += "<title>Krähen Abwehr</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:20px;}";
  html += "h1{color:#e94560;text-align:center;}";
  html += "h2{color:#aaa;text-align:center;font-size:16px;margin-top:-10px;}";
  html += ".card{background:#16213e;border-radius:12px;padding:20px;margin:10px 0;}";
  html += ".label{color:#aaa;font-size:14px;}";
  html += ".value{font-size:24px;font-weight:bold;margin-top:5px;}";
  html += ".status{text-align:center;font-size:28px;font-weight:bold;padding:15px;border-radius:12px;}";
  html += ".btn{display:block;width:100%;padding:15px;border:none;border-radius:12px;font-size:18px;font-weight:bold;cursor:pointer;margin-top:15px;}";
  html += "input[type=range]{width:100%;margin:10px 0;}";
  html += "input[type=number]{width:100%;padding:10px;border-radius:8px;border:none;font-size:18px;font-weight:bold;background:#0f3460;color:#eee;text-align:center;box-sizing:border-box;}";
  html += ".row{display:flex;gap:10px;align-items:center;margin-top:10px;}";
  html += ".row input[type=number]{flex:1;}";
  html += ".row .btn{flex:0 0 auto;width:auto;padding:10px 20px;margin-top:0;}";
  html += "</style></head><body>";
  html += "<h1>🐦‍⬛ Krähen Abwehr</h1>";
  html += "<h2>" + String(deviceName) + "</h2>";

  // Status & Toggle
  html += "<div class='card'>";
  html += "<div class='status' style='background:" + stateCol + ";'>" + state + "</div>";
  html += "<button class='btn' style='background:" + btnCol + ";color:white;' onclick=\"location.href='/toggle'\">" + btnText + "</button>";
  html += "</div>";

  // Lautstärke Slider
  html += "<div class='card'>";
  html += "<div class='label'>Lautstärke</div>";
  html += "<div class='value' id='volVal'>" + String(volPct) + "%</div>";
  html += "<input type='range' min='0' max='100' step='5' value='" + String(volPct) + "' ";
  html += "oninput=\"document.getElementById('volVal').innerText=this.value+'%'\" ";
  html += "onchange=\"location.href='/volume?v='+this.value\">";
  html += "</div>";

  // Cooldown Slider
  html += "<div class='card'>";
  html += "<div class='label'>Cooldown</div>";
  html += "<div class='value' id='cdVal'>" + String(COOLDOWN_SEC) + " Sek</div>";
  html += "<input type='range' min='10' max='300' step='10' value='" + String(COOLDOWN_SEC) + "' ";
  html += "oninput=\"document.getElementById('cdVal').innerText=this.value+' Sek'\" ";
  html += "onchange=\"location.href='/cooldown?c='+this.value\">";
  html += "</div>";

  // Soundanzahl
  html += "<div class='card'>";
  html += "<div class='label'>Anzahl MP3-Dateien</div>";
  html += "<div class='row'>";
  html += "<input type='number' id='scVal' min='1' max='999' value='" + String(SOUND_COUNT) + "'>";
  html += "<button class='btn' style='background:#3498db;color:white;' onclick=\"location.href='/soundcount?c='+document.getElementById('scVal').value\">Speichern</button>";
  html += "</div>";
  html += "</div>";

  // Letzter Alarm
  html += "<div class='card'>";
  html += "<div class='label'>Letzter Alarm</div>";
  html += "<div class='value' style='font-size:18px;'>" + lastAlarmTime + "</div>";
  html += "</div>";

  // Auslösungen heute
  html += "<div class='card'>";
  html += "<div class='label'>Auslösungen heute</div>";
  html += "<div class='value'>" + String(todayCount) + "</div>";
  html += "</div>";

  // Auslösungen gesamt
  html += "<div class='card'>";
  html += "<div class='label'>Auslösungen gesamt</div>";
  html += "<div class='value'>" + String(totalCount) + "</div>";
  html += "</div>";

  // Letzter Sound
  html += "<div class='card'>";
  html += "<div class='label'>Letzter Sound</div>";
  html += "<div class='value'>sound" + String(lastSound) + ".mp3</div>";
  html += "</div>";

  // Uhrzeit
  html += "<div class='card'>";
  html += "<div class='label'>Uhrzeit</div>";
  html += "<div class='value' style='font-size:18px;'>" + getTime() + "</div>";
  html += "</div>";

  // WLAN zurücksetzen
  html += "<div class='card'>";
  html += "<button class='btn' style='background:#e67e22;color:white;' onclick=\"if(confirm('WLAN-Einstellungen wirklich zurücksetzen? Das Gerät startet neu und öffnet einen Hotspot zur Neukonfiguration.')) location.href='/resetwifi'\">WLAN zurücksetzen</button>";
  html += "</div>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleResetWifi() {
  WiFiManager wm;
  wm.resetSettings();
  server.sendHeader("Location", "/");
  server.send(303);
  delay(500);
  ESP.restart();
}

void handleToggle() {
  systemActive = !systemActive;
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleVolume() {
  if (server.hasArg("v")) {
    int pct = server.arg("v").toInt();
    VOLUME = map(pct, 0, 100, 0, 30);
    dfPlayer.volume(VOLUME);
    Serial.print("Lautstärke geändert auf: ");
    Serial.println(VOLUME);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleCooldown() {
  if (server.hasArg("c")) {
    COOLDOWN_SEC = server.arg("c").toInt();
    Serial.print("Cooldown geändert auf: ");
    Serial.print(COOLDOWN_SEC);
    Serial.println(" Sekunden");
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSoundCount() {
  if (server.hasArg("c")) {
    int count = server.arg("c").toInt();
    if (count > 0) {
      SOUND_COUNT = count;
      preferences.putInt("soundCount", SOUND_COUNT);
      Serial.print("Soundanzahl geändert auf: ");
      Serial.println(SOUND_COUNT);
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

// ── Setup ─────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);

  preferences.begin("kraehe", false);
  SOUND_COUNT = preferences.getInt("soundCount", SOUND_COUNT);
  Serial.print("Soundanzahl geladen: ");
  Serial.println(SOUND_COUNT);

  WiFi.setHostname(deviceName);
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  if (wm.autoConnect(deviceName)) {
    Serial.println("Verbunden! IP: " + WiFi.localIP().toString());
    MDNS.begin(deviceName);
    configTime(gmtOffset, daylightOffset, ntpServer);
  } else {
    Serial.println("Kein WLAN – läuft im Offline-Modus.");
  }

  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.on("/volume", handleVolume);
  server.on("/cooldown", handleCooldown);
  server.on("/soundcount", handleSoundCount);
  server.on("/resetwifi", handleResetWifi);
  server.begin();

  dfSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  if (!dfPlayer.begin(dfSerial)) {
    Serial.println("DFPlayer nicht gefunden! Webserver läuft trotzdem.");
  }
  dfPlayer.volume(VOLUME);
  delay(2000);
  randomSeed(analogRead(0));

  Serial.println("Specht-Abwehr bereit!");
}

// ── Loop ──────────────────────────────────────────────────
void loop() {
  server.handleClient();
  unsigned long now = millis();

  int today = getDay();
  if (today != lastAlarmDay && lastAlarmDay != -1) {
    todayCount = 0;
  }

  if (cooldownActive && (now - lastTriggerTime >= (unsigned long)COOLDOWN_SEC * 1000)) {
    cooldownActive = false;
    Serial.println("Cooldown beendet – bereit.");
  }

  if (systemActive && !cooldownActive) {
    if (digitalRead(PIR_PIN) == HIGH) {
      Serial.println("Bewegung erkannt!");

      int sound = pickRandomSound();
      addToHistory(sound);
      dfPlayer.playFolder(1, sound);
      lastSound = sound;

      lastAlarmTime = getTime();
      lastAlarmDay  = today;
      totalCount++;
      todayCount++;

      Serial.print("Spiele sound");
      Serial.print(sound);
      Serial.println(".mp3");

      lastTriggerTime = now;
      cooldownActive  = true;
    }
  }

  delay(200);
}
