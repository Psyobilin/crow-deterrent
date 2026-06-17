#include "Arduino.h"
#include "DFRobotDFPlayerMini.h"
#include "HardwareSerial.h"
#include "WiFiManager.h"
#include "ESPmDNS.h"
#include "WebServer.h"
#include "Preferences.h"
#include "Update.h"
#include "time.h"
#include "ota.h"

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

// Gedrosseltes Sichern des Gesamtzählers (schont den Flash-Speicher)
bool          statsDirty         = false;
unsigned long lastStatsSave      = 0;
const unsigned long STATS_SAVE_INTERVAL = 300000UL; // 5 Minuten

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
:root{
  --bg:#0e0f1a;--card:#181a2e;--deep:#0b0c16;--line:rgba(255,255,255,.07);
  --accent:#6366f1;--accent-soft:rgba(99,102,241,.16);
  --green:#34d399;--red:#f87171;--amber:#fbbf24;
  --text:#e8eaf2;--muted:#9499b7;--shadow:0 6px 20px rgba(0,0,0,.35);
}
html{-webkit-text-size-adjust:100%}
body{
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,system-ui,sans-serif;
  background:radial-gradient(1200px 600px at 50% -10%,#1b1d36 0%,var(--bg) 58%) fixed;
  color:var(--text);min-height:100vh;max-width:480px;margin:0 auto;
  padding:max(18px,env(safe-area-inset-top)) max(16px,env(safe-area-inset-right)) max(26px,env(safe-area-inset-bottom)) max(16px,env(safe-area-inset-left));
  -webkit-font-smoothing:antialiased;text-rendering:optimizeLegibility;
}
h1{font-size:24px;font-weight:800;text-align:center;letter-spacing:-.02em;margin-bottom:2px}
h2{color:var(--muted);text-align:center;font-size:13px;font-weight:500;margin-bottom:18px}
.card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:16px;margin:10px 0;box-shadow:var(--shadow)}
.lbl{color:var(--muted);font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:.07em;margin-bottom:8px}
.val{font-size:24px;font-weight:700;transition:color .3s}
.val.sm{font-size:16px;font-weight:600}
.srow{display:flex;align-items:center;gap:10px;justify-content:center;margin-bottom:10px}
.dot{width:13px;height:13px;border-radius:50%;flex-shrink:0;transition:background .3s}
.dot.on{background:var(--green);animation:glow 1.8s ease-in-out infinite}
.dot.off{background:var(--red)}
.stxt{font-size:22px;font-weight:800;letter-spacing:.01em;transition:color .3s}
.clk{text-align:center;color:var(--muted);font-size:13px;margin-top:2px}
@keyframes glow{
  0%,100%{box-shadow:0 0 0 0 rgba(52,211,153,.6)}
  50%{box-shadow:0 0 0 8px rgba(52,211,153,0)}
}
.brbg{background:var(--deep);border-radius:999px;height:8px;margin:12px 0 8px;overflow:hidden}
.brfl{height:100%;border-radius:999px;background:var(--amber);transition:width .8s linear,background .5s}
.brfl.ok{background:var(--green)}
.btn{display:block;width:100%;min-height:50px;padding:13px 16px;border:none;border-radius:12px;font-size:15px;font-weight:600;color:#fff;background:var(--accent);cursor:pointer;transition:transform .1s,filter .15s;-webkit-tap-highlight-color:transparent}
.btn:active{transform:scale(.98);filter:brightness(.92)}
.blue{background:var(--accent);color:#fff}
.green{background:#059669;color:#fff}
.red{background:#dc2626;color:#fff}
.purple{background:var(--accent-soft);color:#c3c7f5;border:1px solid var(--line)}
.orange{background:transparent;color:var(--red);border:1px solid rgba(248,113,113,.4)}
input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:6px;border-radius:999px;background:var(--deep);margin:14px 0;cursor:pointer}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:26px;height:26px;border-radius:50%;background:var(--accent);border:3px solid rgba(255,255,255,.15);box-shadow:0 2px 6px rgba(0,0,0,.4);cursor:pointer}
input[type=range]::-moz-range-thumb{width:26px;height:26px;border:none;border-radius:50%;background:var(--accent);cursor:pointer}
.row{display:flex;gap:8px;align-items:center;margin-top:6px}
input[type=number],input[type=text]{flex:1;min-height:46px;padding:10px 12px;border-radius:10px;border:1px solid var(--line);font-size:16px;font-weight:600;background:var(--deep);color:var(--text);text-align:center}
input[type=number]:focus,input[type=text]:focus{outline:none;border-color:var(--accent)}
.row .btn{flex:0 0 auto;width:auto;padding:10px 18px;min-height:46px}
.g2{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.cdr{display:flex;justify-content:space-between;align-items:baseline}
.cdh{font-size:13px;font-weight:600;color:var(--amber)}
.hint{font-size:12px;color:var(--muted);margin-top:8px;line-height:1.5}
@keyframes flash{0%{background:#1c2a4d}100%{background:var(--card)}}
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
  <div class="cdr">
    <div class="lbl">Firmware</div>
    <span class="cdh" id="fwv">–</span>
  </div>
  <div class="hint" id="otas" style="margin-top:2px">–</div>
  <button class="btn blue" id="cub" onclick="checkUpd()" style="margin-top:10px">Jetzt auf Updates prüfen</button>
  <a href="/update"><button class="btn purple" style="margin-top:8px">Firmware manuell hochladen</button></a>
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
  document.getElementById('nmsg').textContent='';
  var on=d.active;
  document.getElementById('dot').className='dot '+(on?'on':'off');
  document.getElementById('stxt').textContent=on?'AKTIV':'INAKTIV';
  document.getElementById('stxt').style.color=on?'#34d399':'#f87171';
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
    cv.style.color='#34d399';
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
  document.getElementById('fwv').textContent=d.firmwareVersion;
  document.getElementById('otas').textContent='Status: '+d.otaStatus;
}

function tick(){
  if(!ce)return;
  var r=Math.max(0,Math.ceil((ce-Date.now())/1000));
  var el=document.getElementById('cdv');
  el.textContent=r+'s';
  el.style.color=r>10?'#f87171':'#fbbf24';
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

function checkUpd(){
  var b=document.getElementById('cub');
  b.disabled=true;b.textContent='Prüfe...';
  fetch('/checkupdate').then(function(){
    setTimeout(function(){b.disabled=false;b.textContent='Jetzt auf Updates prüfen';},4000);
  });
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
:root{--bg:#0e0f1a;--card:#181a2e;--deep:#0b0c16;--line:rgba(255,255,255,.07);--accent:#6366f1;--green:#34d399;--text:#e8eaf2;--muted:#9499b7;--shadow:0 6px 20px rgba(0,0,0,.35)}
html{-webkit-text-size-adjust:100%}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,system-ui,sans-serif;background:radial-gradient(1200px 600px at 50% -10%,#1b1d36 0%,var(--bg) 58%) fixed;color:var(--text);min-height:100vh;max-width:480px;margin:0 auto;padding:max(18px,env(safe-area-inset-top)) max(16px,env(safe-area-inset-right)) max(26px,env(safe-area-inset-bottom)) max(16px,env(safe-area-inset-left));-webkit-font-smoothing:antialiased}
h1{font-size:23px;font-weight:800;text-align:center;letter-spacing:-.02em;margin-bottom:2px}
h2{color:var(--muted);text-align:center;font-size:13px;font-weight:500;margin-bottom:18px}
.card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:18px;margin:10px 0;box-shadow:var(--shadow);text-align:center}
.lbl{color:var(--muted);font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:.07em;margin-bottom:10px;text-align:left}
.btn{display:block;width:100%;min-height:50px;padding:13px 16px;border:none;border-radius:12px;font-size:15px;font-weight:600;color:#fff;background:var(--accent);cursor:pointer;transition:transform .1s,filter .15s;-webkit-tap-highlight-color:transparent}
.btn:active{transform:scale(.98);filter:brightness(.92)}
.btn:disabled{opacity:.45;cursor:default}
.blue{background:var(--accent);color:#fff}
.gray{background:#2a2c44;color:var(--muted)}
input[type=file]{width:100%;padding:12px;background:var(--deep);border:1px solid var(--line);border-radius:10px;color:var(--text);margin-bottom:12px;font-size:14px}
.brbg{background:var(--deep);border-radius:999px;height:12px;margin:14px 0;overflow:hidden;display:none}
.brfl{height:100%;background:var(--accent);border-radius:999px;width:0%;transition:width .2s,background .4s}
.msg{font-size:14px;color:var(--muted);margin-top:10px;min-height:20px}
a{color:var(--accent);text-decoration:none;font-size:14px;font-weight:600}
.hint{font-size:12px;color:var(--muted);margin-top:12px;text-align:left;line-height:1.6}
code{background:var(--deep);padding:2px 6px;border-radius:6px}
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
      document.getElementById('bar').style.background='#34d399';
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
  json.reserve(400); // einmal Speicher reservieren statt ~15x neu zu allokieren
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
  json += "\"firmwareVersion\":\"" + otaCurrentVersion() + "\",";
  json += "\"otaStatus\":\""       + otaStatus() + "\",";
  json += "\"time\":\""            + getTime() + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handleSetName() {
  if (server.hasArg("n")) {
    String name = server.arg("n");
    name.trim();
    // Anführungszeichen/Backslash würden das Status-JSON zerstören → ablehnen
    if (name.length() > 0 && name.length() < 32 &&
        name.indexOf('"') < 0 && name.indexOf('\\') < 0) {
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

void handleCheckUpdate() {
  server.send(200, "text/plain", "OK");
  otaCheckNow();
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
    preferences.putInt("volume", VOLUME);
    Serial.print("Lautstärke geändert auf: ");
    Serial.println(VOLUME);
  }
  server.send(200, "text/plain", "OK");
}

void handleCooldown() {
  if (server.hasArg("c")) {
    COOLDOWN_SEC = server.arg("c").toInt();
    preferences.putInt("cooldown", COOLDOWN_SEC);
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
  SOUND_COUNT  = preferences.getInt("soundCount", SOUND_COUNT);
  VOLUME       = preferences.getInt("volume", VOLUME);
  COOLDOWN_SEC = preferences.getInt("cooldown", COOLDOWN_SEC);
  totalCount   = preferences.getInt("totalCount", 0);
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
  server.on("/checkupdate", handleCheckUpdate);
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
  otaTick();
  unsigned long now = millis();

  int today = getDay();
  if (today != lastAlarmDay && lastAlarmDay != -1) {
    todayCount = 0;
  }

  // Gesamtzähler höchstens alle 5 Minuten in den Flash schreiben
  if (statsDirty && now - lastStatsSave >= STATS_SAVE_INTERVAL) {
    preferences.putInt("totalCount", totalCount);
    statsDirty    = false;
    lastStatsSave = now;
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
      dfPlayer.playMp3Folder(sound);
      lastSound = sound;

      lastAlarmTime = getTime();
      lastAlarmDay  = today;
      totalCount++;
      todayCount++;
      statsDirty = true;

      Serial.print("Spiele sound");
      Serial.print(sound);
      Serial.println(".mp3");

      lastTriggerTime = now;
      cooldownActive  = true;
    }
  }

  delay(50);
}
