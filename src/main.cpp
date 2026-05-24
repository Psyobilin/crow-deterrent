#include "Arduino.h"
#include "DFRobotDFPlayerMini.h"
#include "HardwareSerial.h"
#include "WiFiManager.h"
#include "ESPmDNS.h"
#include "WebServer.h"
#include "Preferences.h"
#include "Update.h"
#include "time.h"

// ── Gerätebezeichnung (wird aus NVS geladen, überlebt OTA) ─
char deviceName[32] = "Kraehe";

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

// ── Dashboard HTML ────────────────────────────────────────
static const char DASHBOARD_HTML[] = R"CROW(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<link rel='icon' href='data:image/svg+xml,<svg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220 0 100 100%22><text y=%22.9em%22 font-size=%2290%22>🐦</text></svg>'>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Krähen Abwehr</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{--green:#2ecc71;--red:#e74c3c;--blue:#3498db;--orange:#e67e22;--purple:#8e44ad;--bg:#1a1a2e;--card:#16213e;--deep:#0f3460}
body{font-family:Arial,sans-serif;background:var(--bg);color:#eee;padding:16px;max-width:480px;margin:0 auto}
h1{color:#e94560;text-align:center;font-size:26px;margin-bottom:4px}
h2{color:#aaa;text-align:center;font-size:14px;margin-bottom:16px}
.card{background:var(--card);border-radius:12px;padding:16px;margin:8px 0}
.lbl{color:#aaa;font-size:13px;margin-bottom:6px}
.val{font-size:22px;font-weight:bold;transition:color 0.3s}
.val.sm{font-size:16px}
.srow{display:flex;align-items:center;gap:10px;justify-content:center;margin-bottom:8px}
.dot{width:14px;height:14px;border-radius:50%;flex-shrink:0;transition:background 0.3s}
.dot.on{background:var(--green);animation:glow 1.8s ease-in-out infinite}
.dot.off{background:var(--red)}
.stxt{font-size:22px;font-weight:bold;transition:color 0.3s}
.clk{text-align:center;color:#888;font-size:13px;margin-top:4px}
@keyframes glow{
  0%,100%{box-shadow:0 0 0 0 rgba(46,204,113,.7)}
  50%{box-shadow:0 0 0 9px rgba(46,204,113,0)}
}
.brbg{background:var(--deep);border-radius:6px;height:8px;margin:10px 0 6px;overflow:hidden}
.brfl{height:100%;border-radius:6px;background:var(--blue);transition:width .8s linear,background .5s}
.brfl.ok{background:var(--green)}
.btn{display:block;width:100%;padding:13px;border:none;border-radius:10px;font-size:15px;font-weight:bold;cursor:pointer;transition:opacity .15s,transform .1s}
.btn:active{opacity:.7;transform:scale(.98)}
.red{background:var(--red);color:#fff}
.green{background:var(--green);color:#fff}
.blue{background:var(--blue);color:#fff}
.orange{background:var(--orange);color:#fff}
.purple{background:var(--purple);color:#fff}
input[type=range]{width:100%;margin:6px 0;accent-color:var(--blue);cursor:pointer}
.row{display:flex;gap:8px;align-items:center;margin-top:8px}
input[type=number],input[type=text]{flex:1;padding:10px;border-radius:8px;border:none;font-size:15px;font-weight:bold;background:var(--deep);color:#eee;text-align:center}
input[type=number]{font-size:17px}
.row .btn{flex:0 0 auto;width:auto;padding:10px 18px}
.g2{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.cdr{display:flex;justify-content:space-between;align-items:baseline}
.cdh{font-size:13px;color:#e67e22}
.hint{font-size:12px;color:#888;margin-top:6px}
@keyframes flash{0%{background:#1a4a2e}100%{background:var(--card)}}
.blink{animation:flash .9s ease-out}
a{text-decoration:none}
</style>
</head>
<body>
<h1>🐦‍⬛ Krähen Abwehr</h1>
<h2 id="dn">...</h2>

<div class="card">
  <div class="srow">
    <div class="dot" id="dot"></div>
    <div class="stxt" id="stxt">–</div>
  </div>
  <div class="clk" id="clk">–</div>
  <button class="btn" id="tbtn" onclick="act('/toggle')" style="margin-top:12px">–</button>
</div>

<div class="card">
  <div class="cdr">
    <div class="lbl">Cooldown</div>
    <span class="cdh" id="cdh"></span>
  </div>
  <div class="cdr" style="margin-top:4px">
    <div class="val" id="cdv">–</div>
    <div class="val sm" id="cdm" style="color:#aaa">–</div>
  </div>
  <div class="brbg"><div class="brfl" id="bar" style="width:100%"></div></div>
  <input type="range" min="10" max="300" step="10" id="cds"
    oninput="cdi(this.value)" onchange="act('/cooldown?c='+this.value)">
</div>

<div class="card">
  <div class="lbl">Lautstärke</div>
  <div class="val" id="vv">–</div>
  <input type="range" min="0" max="100" step="5" id="vls"
    oninput="vli(this.value)" onchange="act('/volume?v='+this.value)">
</div>

<div class="card">
  <div class="lbl">Anzahl MP3-Dateien</div>
  <div class="row">
    <input type="number" id="sc" min="1" max="999">
    <button class="btn blue" onclick="act('/soundcount?c='+document.getElementById('sc').value)">Speichern</button>
  </div>
</div>

<div class="card">
  <div class="lbl">Gerätename</div>
  <div class="row">
    <input type="text" id="nn" maxlength="31" placeholder="z.B. Kraehe-3">
    <button class="btn blue" onclick="setName()">Speichern</button>
  </div>
  <div class="hint" id="nmsg"></div>
</div>

<div class="g2">
  <div class="card" id="tc1"><div class="lbl">Heute</div><div class="val" id="t1">–</div></div>
  <div class="card" id="tc2"><div class="lbl">Gesamt</div><div class="val" id="t2">–</div></div>
</div>

<div class="card"><div class="lbl">Letzter Alarm</div><div class="val sm" id="la">–</div></div>
<div class="card"><div class="lbl">Letzter Sound</div><div class="val sm" id="ls">–</div></div>

<div class="card">
  <a href="/update"><button class="btn purple">Firmware Update (OTA)</button></a>
</div>

<div class="card">
  <button class="btn orange" onclick="rwifi()">WLAN zurücksetzen</button>
</div>

<script>
var busy=0,ce=null,ct=null,pv=null;

function poll(){
  fetch('/status').then(function(r){return r.json();}).then(draw).catch(function(){});
}

function draw(d){
  document.getElementById('dn').textContent=d.deviceName;
  var nn=document.getElementById('nn');
  if(document.activeElement!==nn) nn.value=d.deviceName;
  var on=d.active;
  document.getElementById('dot').className='dot '+(on?'on':'off');
  document.getElementById('stxt').textContent=on?'AKTIV':'INAKTIV';
  document.getElementById('stxt').style.color=on?'#2ecc71':'#e74c3c';
  var tb=document.getElementById('tbtn');
  tb.textContent=on?'System DEAKTIVIEREN':'System AKTIVIEREN';
  tb.className='btn '+(on?'red':'green');
  document.getElementById('clk').textContent=d.time;
  if(!busy){
    document.getElementById('vls').value=d.volume;
    document.getElementById('cds').value=d.cooldownSec;
  }
  document.getElementById('vv').textContent=d.volume+'%';
  document.getElementById('cdm').textContent='max '+d.cooldownSec+'s';
  document.getElementById('sc').value=d.soundCount;
  var bar=document.getElementById('bar');
  if(d.cooldownActive){
    ce=Date.now()+d.cooldownRemaining*1000;
    if(!ct)ct=setInterval(tick,200);
    tick();
    bar.className='brfl';
    document.getElementById('cdh').textContent='aktiv';
  }else{
    ce=null;
    if(ct){clearInterval(ct);ct=null;}
    var cv=document.getElementById('cdv');
    cv.textContent='Bereit';
    cv.style.color='#2ecc71';
    bar.style.width='100%';
    bar.className='brfl ok';
    document.getElementById('cdh').textContent='';
  }
  if(pv!==null&&d.totalCount>pv){fl('tc1');fl('tc2');}
  pv=d.totalCount;
  document.getElementById('t1').textContent=d.todayCount;
  document.getElementById('t2').textContent=d.totalCount;
  document.getElementById('la').textContent=d.lastAlarm;
  document.getElementById('ls').textContent=d.lastSound>0?String(d.lastSound).padStart(4,'0')+'.mp3':'–';
}

function tick(){
  if(!ce)return;
  var r=Math.max(0,Math.ceil((ce-Date.now())/1000));
  var el=document.getElementById('cdv');
  el.textContent=r+'s';
  el.style.color=r>10?'#e74c3c':'#e67e22';
  var cs=parseInt(document.getElementById('cds').value)||60;
  document.getElementById('bar').style.width=Math.min(100,r/cs*100)+'%';
}

function fl(id){
  var e=document.getElementById(id);
  e.classList.remove('blink');
  void e.offsetWidth;
  e.classList.add('blink');
}

function vli(v){busy=1;document.getElementById('vv').textContent=v+'%';}
function cdi(v){busy=1;document.getElementById('cdm').textContent='max '+v+'s';}

async function act(u){await fetch(u);busy=0;poll();}

async function setName(){
  var n=document.getElementById('nn').value.trim();
  if(!n)return;
  document.getElementById('nmsg').textContent='Speichern – Gerät startet neu...';
  fetch('/setname?n='+encodeURIComponent(n));
}

function rwifi(){
  if(confirm('WLAN-Einstellungen wirklich zurücksetzen?\nDas Gerät startet neu und öffnet einen Hotspot zur Neukonfiguration.'))
    fetch('/resetwifi');
}

poll();
setInterval(poll,3000);
</script>
</body>
</html>
)CROW";

// ── OTA Update HTML ───────────────────────────────────────
static const char UPDATE_HTML[] = R"OTA(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Firmware Update</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{--green:#2ecc71;--red:#e74c3c;--blue:#3498db;--bg:#1a1a2e;--card:#16213e;--deep:#0f3460}
body{font-family:Arial,sans-serif;background:var(--bg);color:#eee;padding:16px;max-width:480px;margin:0 auto}
h1{color:#e94560;text-align:center;font-size:24px;margin-bottom:4px}
h2{color:#aaa;text-align:center;font-size:13px;margin-bottom:20px}
.card{background:var(--card);border-radius:12px;padding:16px;margin:8px 0;text-align:center}
.lbl{color:#aaa;font-size:13px;margin-bottom:10px;text-align:left}
.btn{display:block;width:100%;padding:13px;border:none;border-radius:10px;font-size:15px;font-weight:bold;cursor:pointer;transition:opacity .15s}
.btn:disabled{opacity:.4;cursor:default}
.blue{background:var(--blue);color:#fff}
.gray{background:#444;color:#fff}
input[type=file]{width:100%;padding:10px;background:var(--deep);border:none;border-radius:8px;color:#eee;margin-bottom:12px;font-size:14px}
.brbg{background:var(--deep);border-radius:6px;height:12px;margin:12px 0;overflow:hidden;display:none}
.brfl{height:100%;background:var(--blue);border-radius:6px;width:0%;transition:width .2s,background .4s}
.msg{font-size:14px;color:#aaa;margin-top:8px;min-height:20px}
a{color:var(--blue);text-decoration:none;font-size:14px}
.hint{font-size:12px;color:#666;margin-top:10px;text-align:left;line-height:1.6}
</style>
</head>
<body>
<h1>🐦‍⬛ Firmware Update</h1>
<h2 id="dn">...</h2>

<div class="card">
  <div class="lbl">Firmware-Datei (.bin)</div>
  <input type="file" id="file" accept=".bin">
  <button class="btn blue" id="upbtn" onclick="doUpload()">Hochladen &amp; Flashen</button>
  <div class="brbg" id="barbg"><div class="brfl" id="bar"></div></div>
  <div class="msg" id="msg"></div>
  <div class="hint">
    PlatformIO Build-Ausgabe:<br>
    <code>.pio/build/lolin_s2_mini/firmware.bin</code>
  </div>
</div>

<div class="card">
  <a href="/">← Zurück zum Dashboard</a>
</div>

<script>
fetch('/status').then(function(r){return r.json();}).then(function(d){
  document.getElementById('dn').textContent=d.deviceName;
});

function doUpload(){
  var f=document.getElementById('file').files[0];
  if(!f){document.getElementById('msg').textContent='Bitte eine .bin-Datei auswählen.';return;}
  var fd=new FormData();
  fd.append('firmware',f,f.name);
  var xhr=new XMLHttpRequest();
  xhr.open('POST','/update');
  document.getElementById('barbg').style.display='block';
  document.getElementById('upbtn').disabled=true;
  document.getElementById('upbtn').className='btn gray';
  xhr.upload.onprogress=function(e){
    if(e.lengthComputable){
      var pct=Math.round(e.loaded/e.total*100);
      document.getElementById('bar').style.width=pct+'%';
      document.getElementById('msg').textContent='Hochladen... '+pct+'%';
    }
  };
  xhr.onload=function(){
    if(xhr.responseText==='OK'){
      document.getElementById('bar').style.width='100%';
      document.getElementById('bar').style.background='#2ecc71';
      document.getElementById('msg').textContent='✓ Erfolgreich – Gerät startet neu...';
    }else{
      document.getElementById('msg').textContent='✗ Fehler beim Flashen. Nochmal versuchen.';
      document.getElementById('upbtn').disabled=false;
      document.getElementById('upbtn').className='btn blue';
    }
  };
  xhr.onerror=function(){
    document.getElementById('msg').textContent='Verbindungsfehler.';
  };
  xhr.send(fd);
}
</script>
</body>
</html>
)OTA";

// ── Webserver Handler ─────────────────────────────────────
void handleRoot() {
  server.send(200, "text/html", DASHBOARD_HTML);
}

void handleStatus() {
  unsigned long now = millis();
  int remaining = 0;
  if (cooldownActive) {
    unsigned long elapsed = (now - lastTriggerTime) / 1000;
    remaining = max(0, (int)COOLDOWN_SEC - (int)elapsed);
  }

  String json = "{";
  json += "\"deviceName\":\"" + String(deviceName) + "\",";
  json += "\"active\":"            + String(systemActive   ? "true" : "false") + ",";
  json += "\"cooldownActive\":"    + String(cooldownActive ? "true" : "false") + ",";
  json += "\"cooldownRemaining\":" + String(remaining) + ",";
  json += "\"cooldownSec\":"       + String(COOLDOWN_SEC) + ",";
  json += "\"volume\":"            + String(map(VOLUME, 0, 30, 0, 100)) + ",";
  json += "\"soundCount\":"        + String(SOUND_COUNT) + ",";
  json += "\"lastAlarm\":\""       + lastAlarmTime + "\",";
  json += "\"todayCount\":"        + String(todayCount) + ",";
  json += "\"totalCount\":"        + String(totalCount) + ",";
  json += "\"lastSound\":"         + String(lastSound) + ",";
  json += "\"time\":\""            + getTime() + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handleSetName() {
  if (server.hasArg("n")) {
    String name = server.arg("n");
    name.trim();
    if (name.length() > 0 && name.length() < 32) {
      name.toCharArray(deviceName, sizeof(deviceName));
      preferences.putString("deviceName", name);
      Serial.print("Gerätename gespeichert: ");
      Serial.println(deviceName);
      server.send(200, "text/plain", "OK");
      delay(500);
      ESP.restart();
      return;
    }
  }
  server.send(400, "text/plain", "Ungültiger Name");
}

void handleUpdatePage() {
  server.send(200, "text/html", UPDATE_HTML);
}

void handleUpdateDone() {
  bool ok = !Update.hasError();
  server.send(200, "text/plain", ok ? "OK" : "FEHLER");
  delay(1000);
  ESP.restart();
}

void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("OTA Start: %s\n", upload.filename.c_str());
    Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    Update.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    Update.end(true);
    Serial.printf("OTA Ende: %u Bytes\n", upload.totalSize);
  }
}

void handleResetWifi() {
  WiFiManager wm;
  wm.resetSettings();
  server.send(200, "text/plain", "Restarting...");
  delay(500);
  ESP.restart();
}

void handleToggle() {
  systemActive = !systemActive;
  server.send(200, "text/plain", "OK");
}

void handleVolume() {
  if (server.hasArg("v")) {
    int pct = server.arg("v").toInt();
    VOLUME = map(pct, 0, 100, 0, 30);
    dfPlayer.volume(VOLUME);
    Serial.print("Lautstärke geändert auf: ");
    Serial.println(VOLUME);
  }
  server.send(200, "text/plain", "OK");
}

void handleCooldown() {
  if (server.hasArg("c")) {
    COOLDOWN_SEC = server.arg("c").toInt();
    Serial.print("Cooldown geändert auf: ");
    Serial.print(COOLDOWN_SEC);
    Serial.println(" Sekunden");
  }
  server.send(200, "text/plain", "OK");
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
  server.send(200, "text/plain", "OK");
}

// ── Setup ─────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);

  preferences.begin("kraehe", false);
  SOUND_COUNT = preferences.getInt("soundCount", SOUND_COUNT);
  String savedName = preferences.getString("deviceName", "Kraehe");
  savedName.toCharArray(deviceName, sizeof(deviceName));
  Serial.print("Gerätename: ");
  Serial.println(deviceName);

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

  server.on("/",           handleRoot);
  server.on("/status",     handleStatus);
  server.on("/toggle",     handleToggle);
  server.on("/volume",     handleVolume);
  server.on("/cooldown",   handleCooldown);
  server.on("/soundcount", handleSoundCount);
  server.on("/setname",    handleSetName);
  server.on("/update",     HTTP_GET,  handleUpdatePage);
  server.on("/update",     HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/resetwifi",  handleResetWifi);
  server.begin();

  dfSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  if (!dfPlayer.begin(dfSerial)) {
    Serial.println("DFPlayer nicht gefunden! Webserver läuft trotzdem.");
  }
  dfPlayer.volume(VOLUME);
  delay(2000);
  randomSeed(analogRead(0));

  Serial.println("Krähen-Abwehr bereit!");
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
