

/*
 Vogelhaus - ESP32-C6 port (synchronous WebServer + Web OTA + ArduinoOTA)
 1.5_Vogelhaus_FinalVer._4_ESP32C6_09.05.26_IP10.20.1.22_MissionProject_mitSleep_mitPowerConsumption_opimized_withMOSFET_mitWebSeiteInactive_mitNachtruhe_mitOTA
 _Nun Abends dauerhaft an, kein Server Sleep mehr_Optimierung an bleiben abends, wenn Licht angeht_Fading beim Einschalten von Helligkeit und Farbe
 _ESP Uhrzeit im Webbrowser, Sommer und WinterZeitumstellung passiert automatisch, Täglich wird maximale Reading erfasst und Abends entsprechend angepasst.
 
 Mit Neopixel WS2812
 IP: 10.20.1.22
 Lichtfarben: 233,32,169
 Einstellung Schwellwert für Licht start Stop in Zeile 221.            Aktuell 2400
 Einstellung Schwellwert für ESP Sleep oder nicht. Zeile: 826 UND 938. Aktuell 2900
 Einstellung Schwellwert Nachtruhe Zeile 936.                          Aktuell 23:30
 
 LDR PIN: 2
 RGB LED Stuerungs PIN: 22
 MOSFET Steuerungs PIN: 23
 WAKE UP PIN: 1
 
 Last up date: 19.07.2026  // Thresholds geändet into Automatik-Berechnungn
 Version: V8
 ESP BoardVersion: 3.3.2 / 3.3.8
 IDE Version: 1.8.13 / (old Arduino IDE)

 Fehlercodes:
 Unexpected response from device: '944' -> Funktioniert trotzdem

*/




#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include "time.h"

// For Times: Automatische Sommer-/Winterzeit für Deutschland (Mitteleuropa)
const char* ntpServer = "pool.ntp.org";
const char* TZ_INFO = "CET-1CEST,M3.5.0,M10.5.0/3"; // Regelt UTC+1/+2 automatisch

// ====== WLAN ======
const char* WIFI_SSID = "Mission Project";
const char* WIFI_PASS = "RS956aury24!";

// ====== Pins / LEDs ======
#define LDR_PIN 2
#define NEOPIXEL_PIN 22  
#define NUM_LEDS 4
#define MOSFET_PIN 23

//Wake Up PIN while in Deep Sleep:
#define BUTTON_PIN 1
#define SLEEP_INTERVAL_US (900ULL * 1000000ULL) // Alle 15 Minuten den LDR checken
#define INACTIVITY_TIMEOUT 1200000UL // 20 Minuten

//Helpers for Sleep
unsigned long lastActivity = 0;
bool forcedAwake = false;
unsigned long lastWifiTry = 0;
unsigned long lastSerial = 0;
#define WIFI_TRY_INTERVAL 30000 

Adafruit_NeoPixel strip(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);
Preferences prefs;

// ====== State ======
int targetR = 233, targetG = 32, targetB = 169;
int manualBrightness = 200;
bool ledsOn = true;
bool autoMode = true;

// DYNAMISCHE KALIBRIERUNG
int maxLdrToday = 2500; // Start-Basiswert
bool resetDoneToday = false; 
unsigned long lastPrefsSave = 0; 
int lastSavedMaxLdr = 2500; // Hilfsvariable gegen Flash-Spam

// Aktuelle Werte für das Fading (Ist)
float currentStepR = 0;
float currentStepG = 0;
float currentStepB = 0;
float currentStepBrightness = 0;

// Forward declarations
void handleRoot();
void handleStatus();
void handleSetColor();
void handleSetBright();
void handleLED();
void handleMode();
void handleReset();
void handleOTAGet();
void handleOTAPost();
void handleOTAUpload();

// ====== Helpers: prefs ======
void savePrefs() {
  prefs.putInt("r", targetR);    
  prefs.putInt("g", targetG);    
  prefs.putInt("b", targetB);    
  prefs.putInt("br", manualBrightness);
  prefs.putBool("on", ledsOn);
  prefs.putBool("auto", autoMode);
  prefs.putInt("maxLdr", maxLdrToday);
  lastSavedMaxLdr = maxLdrToday; // Synchronisieren
}

void loadPrefs() {
  if (prefs.isKey("r")) targetR = prefs.getInt("r", targetR);
  if (prefs.isKey("g")) targetG = prefs.getInt("g", targetG);
  if (prefs.isKey("b")) targetB = prefs.getInt("b", targetB);
  if (prefs.isKey("br")) manualBrightness = prefs.getInt("br", manualBrightness);
  if (prefs.isKey("on")) ledsOn = prefs.getBool("on", ledsOn);
  if (prefs.isKey("auto")) autoMode = prefs.getBool("auto", autoMode);
  if (prefs.isKey("maxLdr")) {
    maxLdrToday = prefs.getInt("maxLdr", maxLdrToday);
    lastSavedMaxLdr = maxLdrToday;
  }
}

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastWifiTry < WIFI_TRY_INTERVAL) return;
  lastWifiTry = now;
  Serial.println("WiFi trying to reconnect...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void goToSleep() {
  strip.clear(); 
  strip.show();
  delay(10);
  digitalWrite(MOSFET_PIN, LOW);
  Serial.println("Deep Sleep...");
  esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_US);
  delay(200);
  esp_deep_sleep_start();
}

void checkWakeupReason() {
  esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
  if (reason == ESP_SLEEP_WAKEUP_EXT1 || reason == ESP_SLEEP_WAKEUP_EXT0 || reason == ESP_SLEEP_WAKEUP_GPIO) {
    Serial.println("Aufgewacht durch Taster");
    forcedAwake = true;
  } else if (reason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Aufgewacht durch Timer");
    forcedAwake = false; 
  } else {
    Serial.println("Normaler Start");
    forcedAwake = true;
  }
}

int calcAutoBrightness(int ldrValue) {
  ldrValue = constrain(ldrValue, 0, maxLdrToday);
  int ledMax = constrain(manualBrightness, 0, 255);
  int led = map(ldrValue, 0, maxLdrToday, ledMax, 0);
  return constrain(led, 0, ledMax);
}

const char index_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset='utf-8'>
  <meta name='viewport' content='width=device-width,initial-scale=1.0'>
  <title>Vogelhaus Lichtsteuerung</title>
  <style>
  :root{ --bg:#0b0b12; --white:#ffffff; --accent:#0ff; --active-green:#00ff00; --auto-bg:#40E0D0; --manual-bg:#FF00FF; --reset-bg:#00E5E5; }
  html,body{height:100%;margin:0;background:var(--bg);color:var(--white);font-family:Constantia, serif;}
  .container{max-width:920px;margin:0 auto;padding:14px;text-align:center;}
  .header{display:flex;align-items:center;justify-content:center;gap:8px;}
  h2{color:#0ff;margin:8px 0;}
  .gauges{display:flex;justify-content:center;gap:36px;flex-wrap:wrap;margin:14px 0;}
  .gauge{width:160px;height:84px;position:relative;display:flex;flex-direction:column;align-items:center;}
  .gauge svg{width:100%;height:100%;}
  .needle{stroke:var(--accent);stroke-width:4;transform-origin:80px 80px;transition:transform 0.4s ease-out;}
  .tacholabel{font-size:16px;margin-top:10px;color:var(--white);}
  .tachovalue{color:var(--accent);font-weight:bold;margin-left:6px;}
  #colorPreview{width:40px;height:40px;margin-top:3px;border-radius:6px;border:2px solid #fff;margin-left:auto;margin-right:auto;display:block;}
  .rgbLabel{font-size:14px;margin-top:6px;color:var(--white);}
  .rgbCode{color:var(--accent);font-weight:bold;margin-top:4px;}
  .colorRow{margin-top:10px;display:flex;justify-content:center;gap:8px;align-items:center;flex-direction:column;}
  input[type="color"]{width:40px;height:40px;border:none;border-radius:6px;cursor:pointer;}
  .colorApply{margin-top:6px;padding:8px 14px;border-radius:8px;border:none;background:#f5f5f5;color:#0074D9;font-weight:bold;cursor:pointer;}
  .sliderRow{margin-top:10px;display:flex;flex-direction:column;align-items:center;}
  input[type="range"]{width:300px;height:14px;border-radius:10px;background:linear-gradient(90deg,#0f0,#ff0,#f00);outline:none;border:none;}
  input[type="range"]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;border-radius:50%;background:#00E5E5;box-shadow:0 0 10px rgba(0,229,229,0.6);cursor:pointer;border:2px solid rgba(255,255,255,0.15);}
  input[type="range"]::-moz-range-thumb{width:20px;height:20px;border-radius:50%;background:#00E5E5;border:none;box-shadow:0 0 8px rgba(0,229,229,0.5);}
  .sliderApply{margin-top:8px;padding:8px 14px;border-radius:8px;border:none;background:#f5f5f5;color:#0074D9;font-weight:bold;cursor:pointer;}
  .panelRow{display:flex;gap:10px;justify-content:center;margin-top:5px;}
  .panel{padding:6px;border-radius:8px;display:flex;align-items:center;justify-content:center;}
  .panelAuto{background:var(--auto-bg);}
  .panelManual{background:var(--manual-bg);}
  .panel.active{box-shadow:0 0 10px rgba(0,255,0,0.14);outline:3px solid var(--active-green);border-radius:10px;}
  .btns{display:flex;gap:10px;justify-content:center;margin-top:6px;}
  .btn{padding:8px 14px;border-radius:8px;border:2px solid transparent;background:#222;color:#fff;cursor:pointer;}
  .btn.active{box-shadow:0 0 8px rgba(0,255,0,0.14);border-color:var(--active-green);}
  .resetRow{margin-top:5px;}
  .resetPanel{border-radius:8px;display:inline-block;padding:6px;border:2px solid var(--reset-bg);background:transparent;transition:all 0.18s ease;}
  .resetPanel a{color:var(--reset-bg);text-decoration:none;}
  .resetPanel button{background:transparent;border:none;color:var(--reset-bg);font-weight:bold;padding:8px 12px;border-radius:6px;cursor:pointer;}
  .resetPanel:hover{background:var(--reset-bg);}
  .resetPanel:hover button{color:#000;}
  hr{border:0;border-top:1px solid #222;margin:12px 0;width:90%;}
  @media (max-width:700px){ input[type="range"]{width:200px;} .gauge{width:140px;height:74px;} #colorPreview{width:36px;height:36px;} }
  </style>
</head>
<body>
  <div class="container">
    <div class="header"><h2>🐦 Vogelhaus Lichtsteuerung</h2></div>
    <div id="modeLine" class="modeLine">Modus: <b id="modeVal">--</b></div>
    <div id="timeLine" class="timeLine">Uhrzeit ESP: <b id="timeVal">--:--</b></div>  
    <div class="gauges">
      <div class="gauge">
        <svg viewBox="0 0 160 80">
          <defs><linearGradient id="gLdr" x1="0" x2="1"><stop offset="0%" stop-color="green"/><stop offset="50%" stop-color="yellow"/><stop offset="100%" stop-color="red"/></linearGradient></defs>
          <path d="M10,80 A70,70 0 0,1 150,80" fill="none" stroke="url(#gLdr)" stroke-width="10"/>
          <line id="ldrNeedle" x1="80" y1="80" x2="80" y2="10" class="needle"/>
        </svg>
        <div class="tacholabel">Lichteinfall <span id="ldrVal" class="tachovalue">0</span></div>
      </div>
      <div class="gauge">
        <svg viewBox="0 0 160 80">
          <defs><linearGradient id="gBri" x1="0" x2="1"><stop offset="0%" stop-color="#00f"/><stop offset="50%" stop-color="#0ff"/><stop offset="100%" stop-color="#fff"/></linearGradient></defs>
          <path d="M10,80 A70,70 0 0,1 150,80" fill="none" stroke="url(#gBri)" stroke-width="10"/>
          <line id="brightNeedle" x1="80" y1="80" x2="80" y2="10" class="needle"/>
        </svg>
        <div class="tacholabel">Helligkeit LED <span id="brightVal" class="tachovalue">0</span></div>
      </div>
    </div>
    <div id="colorPreview"></div>
    <div class="rgbLabel">Aktuelle Lichtfarbe</div>
    <div id="rgbCode" class="rgbCode">RGB(0,0,0)</div>
    <hr>
    <div class="colorRow">
      <form id="colorForm" style="display:flex;flex-direction:column;align-items:center;">
        <label style="margin-bottom:6px;color:#fff;">Farbauswahl</label>
        <input type="color" id="colorPicker" value="#ff0000">
        <input type="hidden" id="hidR">
        <input type="hidden" id="hidG">
        <input type="hidden" id="hidB">
        <button type="button" id="applyColor" class="colorApply">Apply</button>
      </form>
    </div>
    <div class="sliderRow">
      <form id="brightForm" style="display:flex;flex-direction:column;align-items:center;">
        <label style="margin-bottom:6px;color:#fff;">Manuelle Helligkeit</label>
        <input type="range" id="br" min="0" max="255" value="200">
        <button type="button" id="applyBright" class="sliderApply">Apply</button>
      </form>
    </div>
    <div class="btns">
      <a href="/LED=ON"><button id="btnOn" class="btn" style="background:#0a0;color:#000;">AN</button></a>
      <a href="/LED=OFF"><button id="btnOff" class="btn" style="background:#a00;color:#fff;">AUS</button></a>
    </div>
    <div class="panelRow">
      <div id="panelAuto" class="panel panelAuto"><a href="/MODE=AUTO" style="text-decoration:none;"><button id="btnAuto" class="btn">Automatik</button></a></div>
      <div id="panelMan" class="panel panelManual"><a href="/MODE=MANUAL" style="text-decoration:none;"><button id="btnMan" class="btn">Manuell</button></a></div>
    </div>
    <div class="resetRow">
      <div class="resetPanel"><a href="/RESET"><button id="btnReset">🔄 Reset</button></a></div>
    </div>
  </div>
<script>
let lockUpdate = false;
async function updateStatus(){
  if(lockUpdate) return;
  try {
    const r = await fetch('/STATUS');
    if(!r.ok) return;
    const d = await r.json();
    document.getElementById('timeVal').innerText = d.time;
    document.getElementById('modeVal').innerText = d.mode;
    document.getElementById('ldrVal').innerText = d.ldr;
    document.getElementById('brightVal').innerText = d.brightness;
    const ldrA = (d.ldr / 4095 * 180) - 90;
    const briA = (d.brightness / 255 * 180) - 90;
    document.getElementById('ldrNeedle').style.transform = 'rotate('+ldrA+'deg)';
    document.getElementById('brightNeedle').style.transform = 'rotate('+briA+'deg)';
    const rgb = 'rgb('+d.r+','+d.g+','+d.b+')';
    document.getElementById('colorPreview').style.backgroundColor = rgb;
    document.getElementById('rgbCode').innerText = rgb;
    document.getElementById('br').value = d.brightness;
    document.getElementById('panelAuto').classList.toggle('active', d.mode === "Auto");
    document.getElementById('panelMan').classList.toggle('active', d.mode === "Manuell");
    document.getElementById('btnOn').classList.toggle('active', d.on);
    document.getElementById('btnOff').classList.toggle('active', !d.on);
  } catch(e){}
}
setInterval(updateStatus,1000);
updateStatus();
const picker = document.getElementById('colorPicker');
const hidR = document.getElementById('hidR');
const hidG = document.getElementById('hidG');
const hidB = document.getElementById('hidB');
picker.addEventListener('input', ()=>{
  const c = picker.value;
  hidR.value = parseInt(c.substr(1,2),16);
  hidG.value = parseInt(c.substr(3,2),16);
  hidB.value = parseInt(c.substr(5,2),16);
  document.getElementById('colorPreview').style.backgroundColor = c;
});
(function initColor(){
  const c = picker.value;
  hidR.value = parseInt(c.substr(1,2),16);
  hidG.value = parseInt(c.substr(3,2),16);
  hidB.value = parseInt(c.substr(5,2),16);
})();
document.getElementById('applyColor').addEventListener('click', async ()=>{
  const R = hidR.value; const G = hidG.value; const B = hidB.value;
  lockUpdate = true;
  try {
    await fetch(`/SETCOLOR?R=${R}&G=${G}&B=${B}`);
    setTimeout(()=>{ lockUpdate = false; updateStatus(); }, 300);
  } catch(e){ lockUpdate = false; }
});
const slider = document.getElementById('br');
slider.addEventListener('input', ()=>{ lockUpdate = true; });
document.getElementById('applyBright').addEventListener('click', async ()=>{
  const br = slider.value;
  try {
    await fetch(`/SETBRIGHT?br=${br}`);
    setTimeout(()=>{ lockUpdate = false; updateStatus(); }, 300);
  } catch(e){ lockUpdate = false; }
});
</script>
</body>
</html>
)rawliteral";

void handleStatus() {
  struct tm ti;
  char timeStr[10] = "00:00";
  if (getLocalTime(&ti)) {
    sprintf(timeStr, "%02d:%02d", ti.tm_hour, ti.tm_min);
  }

  int ldrNow = analogRead(LDR_PIN);
  int brightNow = autoMode ? calcAutoBrightness(ldrNow) : manualBrightness;
  
  String json = "{";
  json += "\"mode\":\"";
  json += (autoMode ? "Auto" : "Manuell");
  json += "\",";
  json += "\"time\":\"" + String(timeStr) + "\",";
  json += "\"ldr\":" + String(ldrNow) + ",";
  json += "\"brightness\":" + String(brightNow) + ",";
  json += "\"r\":" + String(targetR) + ",";
  json += "\"g\":" + String(targetG) + ",";
  json += "\"b\":" + String(targetB) + ",";
  json += "\"on\":" + String(ledsOn ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

void handleSetColor() {
  lastActivity = millis();
  if (server.hasArg("R")) targetR = server.arg("R").toInt();
  if (server.hasArg("G")) targetG = server.arg("G").toInt();
  if (server.hasArg("B")) targetB = server.arg("B").toInt();
  savePrefs();
  server.send(200, "text/plain", "OK");
}

void handleSetBright() {
  lastActivity = millis();  
  if (server.hasArg("br")) {
    manualBrightness = constrain(server.arg("br").toInt(), 0, 255);
    savePrefs();
  }
  server.send(200, "text/plain", "OK");
}

void handleLED() {
  lastActivity = millis();
  String uri = server.uri();
  if (uri.indexOf("ON") != -1) ledsOn = true;
  else ledsOn = false;
  savePrefs();
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void handleMode() {
  lastActivity = millis();
  String uri = server.uri();
  if (uri.indexOf("AUTO") != -1) autoMode = true;
  else autoMode = false;
  savePrefs();
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void handleReset() {
  const char* resetHtml = R"rawliteral(
    <!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>
    <title>Neustart</title><style>body{background:#111;color:#fff;font-family:Constantia;text-align:center;padding-top:40px;}
    .spinner{width:64px;height:64px;border-radius:50%;border:8px solid #333;border-top:8px solid #0ff;margin:20px auto;animation:spin 1s linear infinite;}
    @keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}</style></head>
    <body><h2>🔄 Neustart wird durchgeführt...</h2><div class="spinner"></div><p>Bitte warten — die Seite springt automatisch zurück.</p>
    <script>setTimeout(()=>{location.href='/'},8000);</script></body></html>
  )rawliteral";
  server.send(200, "text/html", resetHtml);
  savePrefs();
  delay(300);
  ESP.restart();
}

void handleRoot() {
  lastActivity = millis();
  server.send_P(200, "text/html", index_html);
}

const char ota_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>
<title>Firmware Upload</title>
<style>
body{background:#111;color:#fff;font-family:Constantia;text-align:center;padding:20px;}
.card{background:#0b0b12;padding:18px;border-radius:10px;border:1px solid #222;display:inline-block;}
input[type=file]{margin:12px;}
button{padding:8px 14px;border-radius:6px;border:none;background:#0ff;color:#000;font-weight:bold;cursor:pointer;}
</style>
</head><body>
<div class="card">
<h3>Firmware Upload (OTA)</h3>
<form method="POST" action="/OTA" enctype="multipart/form-data">
  <input type="file" name="firmware" accept=".bin"><br>
  <button type="submit">Upload & Install</button>
</form>
<p style="font-size:12px;color:#888;margin-top:8px;">Nach erfolgreichem Upload wird das Gerät automatisch neu gestartet.</p>
</div>
</body></html>
)rawliteral";

void handleOTAGet() {
  server.send_P(200, "text/html", ota_html);
}

void handleOTAPost() {
  if (Update.hasError()) {
    server.send(500, "text/plain", "Update failed");
  } else {
    server.send(200, "text/plain", "Upload abgeschlossen. Neustart...");
  }
}

void handleOTAUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("OTA Upload Start: %s\n", upload.filename.c_str());
    size_t contentLen = upload.totalSize;
    if (!Update.begin(contentLen)) {
      size_t maxSketchSpace = ESP.getFreeSketchSpace();
      Update.begin(maxSketchSpace);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    Update.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    Update.end(true);
    delay(200);
    ESP.restart();
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, LOW); 

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  checkWakeupReason();
  lastActivity = millis();
  strip.begin();
  strip.show();

  prefs.begin("vogel", false);
  loadPrefs();

  currentStepR = targetR;
  currentStepG = targetG;
  currentStepB = targetB;
  currentStepBrightness = 0; 

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setSleep(false);
  
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
    delay(300);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ mit WLAN verbunden");
  } else {
    delay(5000);
    ESP.restart();
  }

  // Korrektur 1: Echte automatische Zeitzone für Deutschland setzen
  configTzTime(TZ_INFO, ntpServer);
  
  // Pufferzeit, bis NTP-Zeit geladen ist
  struct tm ti;
  int retry = 0;
  while (!getLocalTime(&ti) && retry < 10) {
    delay(200);
    retry++;
  }

  // Korrektur 3: Mitternachtsreset im Setup sichern
  if (getLocalTime(&ti)) {
    if (ti.tm_hour >= 0 && ti.tm_hour < 4) { 
      maxLdrToday = 1500; // Niedrigerer Nachtwert, um Dämmerungsüberschreiben zu verhindern
      savePrefs();
      resetDoneToday = true; 
    }
  }

  int ldrNow = analogRead(LDR_PIN);
  
  // Nur kalibrieren, wenn wir NICHT im nächtlichen Reset-Fenster sind
  if (!resetDoneToday && ldrNow > maxLdrToday && ldrNow <= 4095) {
    maxLdrToday = ldrNow;
    savePrefs();
  }

  // Deep-Sleep-Check am Tag
  if (!forcedAwake && ldrNow > (maxLdrToday - 200)) { 
    Serial.println("Tag erkannt → zurück in Sleep");
    goToSleep();
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/STATUS", HTTP_GET, handleStatus);
  server.on("/SETCOLOR", HTTP_GET, handleSetColor);
  server.on("/SETBRIGHT", HTTP_GET, handleSetBright);
  server.on("/LED=ON", HTTP_GET, handleLED);
  server.on("/LED=OFF", HTTP_GET, handleLED);
  server.on("/MODE=AUTO", HTTP_GET, handleMode);
  server.on("/MODE=MANUAL", HTTP_GET, handleMode);
  server.on("/RESET", HTTP_GET, handleReset);

  server.on("/OTA", HTTP_GET, handleOTAGet);
  server.on("/OTA", HTTP_POST, handleOTAPost, handleOTAUpload);
  server.begin();

  ArduinoOTA.setHostname("Vogelhaus");
  ArduinoOTA.begin();

  Serial.println("System bereit. Warte 5 Sekunden Sicherheitszeit...");
  delay(5000);
}

void loop() {
  if (forcedAwake && millis() > 600000) forcedAwake = false;    // Zeit zum Wachbleiben nach Wake-up Taster -> hier 10 Minuten

  ensureWiFi();
  ArduinoOTA.handle();
  server.handleClient();

  int ldr = analogRead(LDR_PIN);

  // ====== DYNAMISCHE KALIBRIERUNG (Flash-schonend korrigiert) ======
  if (ldr > maxLdrToday && ldr <= 4095) {
    maxLdrToday = ldr; // RAM-Wert sofort anpassen für die Licht-Berechnung
  }

  // Nur im Flash speichern, wenn der neue Wert signifikant höher ist ODER 5 Min um sind
  if (maxLdrToday > lastSavedMaxLdr) {
    if (millis() - lastPrefsSave >= 300000) {
      lastPrefsSave = millis();
      savePrefs();
      Serial.printf("✨ Neues Tagesmaximum im Flash gesichert: %d\n", maxLdrToday);
    }
  }

  // ====== DER MITTERNACHTS-RESET ======
  struct tm ti;
  if (getLocalTime(&ti)) {
    if (ti.tm_hour == 0 && ti.tm_min == 0) {
      if (!resetDoneToday) {
        maxLdrToday = 1500; 
        savePrefs();
        resetDoneToday = true;
        Serial.println("🔄 Kalibrierung für neuen Tag zurückgesetzt.");
      }
    } else {
      resetDoneToday = false; 
    }
  }

  // 1. Werte berechnen
  int currentTargetB = autoMode ? calcAutoBrightness(ldr) : manualBrightness;
  int finalTargetBrightness = ledsOn ? currentTargetB : 0;

  // 2. Serieller Monitor
  if (millis() - lastSerial >= 2000) {
    lastSerial = millis();
    Serial.printf("LDR:%d | Max-Heute:%d | Ziel-Bri:%d\n", ldr, maxLdrToday, finalTargetBrightness);
  }

  // 3. MOSFET Steuerung
  if (finalTargetBrightness > 0 && digitalRead(MOSFET_PIN) == LOW) {
    digitalWrite(MOSFET_PIN, HIGH);
    delay(10); 
  }

  // 4. Fading
  if (abs(currentStepBrightness - (float)finalTargetBrightness) > 0.1 ||  
      abs(currentStepR - (float)targetR) > 0.1) {
    
    currentStepBrightness += ((float)finalTargetBrightness - currentStepBrightness) * 0.05;
    currentStepR += ((float)targetR - currentStepR) * 0.05;
    currentStepG += ((float)targetG - currentStepG) * 0.05;
    currentStepB += ((float)targetB - currentStepB) * 0.05;
    
    strip.setBrightness((int)currentStepBrightness);
    strip.fill(strip.Color((int)currentStepR, (int)currentStepG, (int)currentStepB));
    strip.show();
  }

  // 5. MOSFET Aus-Logik
  if (finalTargetBrightness == 0 && currentStepBrightness < 1.0) {
    currentStepBrightness = 0;
    strip.clear();
    strip.show();
    digitalWrite(MOSFET_PIN, LOW);
  }

  // 6. Zeitgesteuerter Deep Sleep
  if (!forcedAwake) {
    if (getLocalTime(&ti)) {
      bool isNight = (ti.tm_hour == 23 && ti.tm_min >= 30) || (ti.tm_hour < 5);
      bool isDaySleep = (ti.tm_hour >= 8 && ti.tm_hour < 20 && ldr > (maxLdrToday - 200));
      
      if (isNight || isDaySleep) {
        savePrefs(); // Letzter Stand vor dem Einschlafen sichern
        goToSleep();
      }
    }
  }

  delay(15);
}
