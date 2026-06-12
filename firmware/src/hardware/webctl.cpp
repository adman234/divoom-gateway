
#include "webctl.h"

#include "Update.h"

#include "util.h"
#include "hardware/bluetoothctl.h"
#include "hardware/settings.h"
#include "hardware/wifictl.h"
#include "input/mqtt.h"

static const char indexHtml[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Divoom Gateway</title>
<link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='%23111418'/><rect x='3' y='3' width='4' height='4' fill='%238ab4f8'/><rect x='9' y='3' width='4' height='4' fill='%2381c995'/><rect x='3' y='9' width='4' height='4' fill='%23fdd663'/><rect x='9' y='9' width='4' height='4' fill='%23f28b82'/></svg>">
<style>
:root{color-scheme:dark}
*{box-sizing:border-box}
body{font-family:system-ui,sans-serif;background:#111418;color:#e8eaed;margin:0;padding:16px;display:flex;justify-content:center}
main{width:100%;max-width:560px}
h1{font-size:1.4em;margin:8px 0 16px}
h1 small{color:#9aa0a6;font-weight:normal;font-size:.6em;margin-left:8px}
section{background:#1c2026;border-radius:12px;padding:16px;margin-bottom:16px}
h2{font-size:1em;margin:0 0 12px;color:#8ab4f8}
label{display:block;font-size:.85em;color:#9aa0a6;margin:10px 0 4px}
input,select{width:100%;padding:8px 10px;border-radius:8px;border:1px solid #3c4043;background:#111418;color:#e8eaed;font-size:.95em}
input:focus{outline:none;border-color:#8ab4f8}
button{background:#8ab4f8;color:#111418;border:none;border-radius:8px;padding:10px 16px;font-size:.95em;font-weight:600;cursor:pointer;margin-top:14px}
button.danger{background:#f28b82}
button.secondary{background:#3c4043;color:#e8eaed}
.row{display:flex;gap:12px}
.row>div{flex:1}
.kv{display:flex;justify-content:space-between;padding:4px 0;font-size:.9em;border-bottom:1px solid #2a2f36}
.kv:last-child{border-bottom:none}
.kv span:first-child{color:#9aa0a6}
.ok{color:#81c995}.bad{color:#f28b82}
.note{font-size:.8em;color:#9aa0a6;margin-top:8px}
.warn{background:#2a2218;border-left:4px solid #fdd663;padding:8px 12px;border-radius:8px;font-size:.8em;margin-top:10px}
#toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%);background:#8ab4f8;color:#111418;padding:10px 20px;border-radius:8px;font-weight:600;display:none}
</style>
</head>
<body>
<main>
<h1>Divoom Gateway<small id="version"></small></h1>

<section>
<h2>Status</h2>
<div class="kv"><span>WiFi</span><span id="s-wifi"></span></div>
<div class="kv"><span>IP address</span><span id="s-ip"></span></div>
<div class="kv"><span>Bluetooth</span><span id="s-bt"></span></div>
<div class="kv"><span>Divoom devices found</span><span id="s-devices"></span></div>
<div class="kv"><span>MQTT</span><span id="s-mqtt"></span></div>
<div class="kv"><span>Free heap</span><span id="s-heap"></span></div>
<div class="kv"><span>Uptime</span><span id="s-uptime"></span></div>
<p class="note">The Bluetooth connection to a Divoom device is commanded by Home Assistant (or TCP/MQTT/Serial clients) &mdash; the gateway connects on demand. Discovery runs every ~15 seconds.</p>
</section>

<form id="config">
<section>
<h2>WiFi</h2>
<label>Hostname</label><input name="hostname" maxlength="32">
<div class="row">
<div><label>SSID</label><input name="wifiSsid1" maxlength="32"></div>
<div><label>Password</label><input name="wifiPass1" type="password" maxlength="64" placeholder="(unchanged)"></div>
</div>
<div class="row">
<div><label>SSID 2 (optional)</label><input name="wifiSsid2" maxlength="32"></div>
<div><label>Password 2</label><input name="wifiPass2" type="password" maxlength="64" placeholder="(unchanged)"></div>
</div>
</section>

<section>
<h2>MQTT (optional)</h2>
<div class="row">
<div><label>Host</label><input name="mqttHost" maxlength="64"></div>
<div><label>Port</label><input name="mqttPort" type="number" min="1" max="65535"></div>
</div>
<div class="row">
<div><label>Username</label><input name="mqttUser" maxlength="32"></div>
<div><label>Password</label><input name="mqttPass" type="password" maxlength="64" placeholder="(unchanged)"></div>
</div>
<label>Topic prefix</label><input name="mqttPrefix" maxlength="32">
</section>

<section>
<h2>Bluetooth</h2>
<label>Discovery filter</label>
<select name="btFilter">
<option value="1">Only announce Divoom devices</option>
<option value="0">Announce all discovered devices</option>
</select>
<button type="submit">Save &amp; Restart</button>
</section>
</form>

<section>
<h2>Firmware update</h2>
<form id="ota" method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin">
<button type="submit">Upload &amp; Install</button>
</form>
<p class="note">Upload a firmware .bin file. The device restarts automatically when done.</p>
<p class="warn">&#9888;&#65039; Untested feature. If an over-the-air update fails or the device misbehaves afterwards, re-flash via USB at the web installer.</p>
</section>

<section>
<h2>Maintenance</h2>
<button class="secondary" onclick="post('/api/restart')">Restart</button>
<button class="danger" onclick="if(confirm('Erase all settings?'))post('/api/reset')">Factory reset</button>
<p class="warn">&#9888;&#65039; Factory reset is untested. It erases all settings (incl. WiFi) and the gateway reopens its setup access point. If it does not come back, re-flash via USB.</p>
</section>
</main>
<div id="toast"></div>
<script>
const $=id=>document.getElementById(id);
function toast(m){const t=$('toast');t.textContent=m;t.style.display='block';setTimeout(()=>t.style.display='none',4000)}
function post(u){fetch(u,{method:'POST'}).then(()=>toast('OK, restarting...'))}
function fmtUp(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+'d ':'')+h+'h '+m+'m'}
function refresh(){fetch('/api/status').then(r=>r.json()).then(j=>{
$('version').textContent='v'+j.version;
$('s-wifi').innerHTML=j.wifi.connected?'<span class="ok">'+j.wifi.ssid+' ('+j.wifi.rssi+' dBm)</span>':(j.wifi.ap?'<span class="bad">access point mode</span>':'<span class="bad">disconnected</span>');
$('s-ip').textContent=j.wifi.ip;
$('s-bt').innerHTML=j.bluetooth.connected?'<span class="ok">connected</span>':'<span>not connected</span>';
$('s-devices').textContent=j.bluetooth.devices.length?j.bluetooth.devices.map(d=>d.name+' ('+d.mac+')').join(', '):'none yet';
$('s-mqtt').innerHTML=j.mqtt.configured?(j.mqtt.connected?'<span class="ok">connected</span>':'<span class="bad">disconnected</span>'):'<span>not configured</span>';
$('s-heap').textContent=Math.round(j.heap/1024)+' KB';
$('s-uptime').textContent=fmtUp(j.uptime);
}).catch(()=>{})}
fetch('/api/config').then(r=>r.json()).then(j=>{
const f=document.forms.config;
for(const k in j){if(f[k]!==undefined)f[k].value=j[k]}
});
document.forms.config.onsubmit=e=>{
e.preventDefault();
fetch('/api/config',{method:'POST',body:new URLSearchParams(new FormData(e.target))})
.then(()=>toast('Saved. Restarting...'));
};
document.forms.ota.onsubmit=e=>{
e.preventDefault();
const f=new FormData(e.target);
if(!f.get('firmware')||!f.get('firmware').name)return toast('Choose a .bin file first');
toast('Uploading...');
fetch('/update',{method:'POST',body:f}).then(r=>r.text()).then(t=>toast(t)).catch(()=>toast('Upload failed'));
};
refresh();setInterval(refresh,5000);
</script>
</body>
</html>)html";

/**
 * setup functionality
*/
void WebHandler::setup(void) {
    server = new AsyncWebServer(80);

    server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        // explicit length from flash: the chunked variants proved unreliable on real hardware
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", (const uint8_t*)indexHtml, sizeof(indexHtml) - 1);
        request->send(response);
    });

    server->on("/api/status", HTTP_GET, handleStatus);
    server->on("/api/config", HTTP_GET, handleConfigGet);
    server->on("/api/config", HTTP_POST, handleConfigPost);
    server->on("/api/restart", HTTP_POST, handleRestart);
    server->on("/api/reset", HTTP_POST, handleReset);

    server->on("/update", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            bool success = !Update.hasError();
            AsyncWebServerResponse *response = request->beginResponse(success ? 200 : 500, "text/plain", success ? "Update successful. Restarting..." : "Update failed.");
            response->addHeader("Connection", "close");
            request->send(response);
            if (success) scheduleRestart();
        },
        handleUpdate);

    server->onNotFound(handleNotFound);
    server->begin();
}

/**
 * loop functionality. executes a scheduled restart
*/
void WebHandler::loop(void) {
    if (restartPending && getElapsed(restartTimer) > 1000) {
        // shut bluetooth down first: a soft restart with an active BT Classic
        // controller can leave the radio wedged until a power cycle
        BluetoothHandler::stop();
        delay(100);
        ESP.restart();
    }
}

/**
 * GET /api/status
*/
void WebHandler::handleStatus(AsyncWebServerRequest *request) {
    bool wifiConnected = WiFi.status() == WL_CONNECTED;

    String json = "{";
    json += "\"name\":\"" FIRMWARE_NAME "\",";
    json += "\"version\":\"" FIRMWARE_VERSION "\",";
    json += "\"wifi\":{";
    json += "\"connected\":" + String(wifiConnected ? "true" : "false") + ",";
    json += "\"ap\":" + String(WifiHandler::isAccessPoint() ? "true" : "false") + ",";
    json += "\"ssid\":\"" + jsonEscape(wifiConnected ? WiFi.SSID().c_str() : "") + "\",";
    json += "\"rssi\":" + String(wifiConnected ? WiFi.RSSI() : 0) + ",";
    json += "\"ip\":\"" + (WifiHandler::isAccessPoint() ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\"";
    json += "},";
    json += "\"bluetooth\":{\"connected\":" + String(BluetoothHandler::check() ? "true" : "false") + ",\"devices\":[";
    for (size_t i = 0; i < BluetoothHandler::discoveredCount && i < BT_DISCOVERED_MAX; i++) {
        if (i > 0) json += ",";
        json += "{\"mac\":\"" + jsonEscape(BluetoothHandler::discovered[i].mac) + "\",";
        json += "\"name\":\"" + jsonEscape(BluetoothHandler::discovered[i].name) + "\"}";
    }
    json += "]},";
    json += "\"mqtt\":{";
    json += "\"configured\":" + String(Settings::hasMqtt() ? "true" : "false") + ",";
    json += "\"connected\":" + String(MqttInput::check() ? "true" : "false");
    json += "},";
    json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"uptime\":" + String(millis() / 1000);
    json += "}";

    request->send(200, "application/json", json);
}

/**
 * GET /api/config (passwords are never sent back)
*/
void WebHandler::handleConfigGet(AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"hostname\":\"" + jsonEscape(Settings::hostname) + "\",";
    json += "\"wifiSsid1\":\"" + jsonEscape(Settings::wifiSsid1) + "\",";
    json += "\"wifiSsid2\":\"" + jsonEscape(Settings::wifiSsid2) + "\",";
    json += "\"mqttHost\":\"" + jsonEscape(Settings::mqttHost) + "\",";
    json += "\"mqttPort\":" + String(Settings::mqttPort) + ",";
    json += "\"mqttUser\":\"" + jsonEscape(Settings::mqttUser) + "\",";
    json += "\"mqttPrefix\":\"" + jsonEscape(Settings::mqttPrefix) + "\",";
    json += "\"btFilter\":\"" + String(Settings::btFilter ? 1 : 0) + "\"";
    json += "}";

    request->send(200, "application/json", json);
}

/**
 * POST /api/config (empty passwords keep their current value)
*/
void WebHandler::handleConfigPost(AsyncWebServerRequest *request) {
    if (request->hasParam("hostname", true)) {
        String value = request->getParam("hostname", true)->value();
        if (value.length() > 0) strlcpy(Settings::hostname, value.c_str(), sizeof(Settings::hostname));
    }

    if (request->hasParam("wifiSsid1", true))
        strlcpy(Settings::wifiSsid1, request->getParam("wifiSsid1", true)->value().c_str(), sizeof(Settings::wifiSsid1));
    if (request->hasParam("wifiPass1", true) && request->getParam("wifiPass1", true)->value().length() > 0)
        strlcpy(Settings::wifiPass1, request->getParam("wifiPass1", true)->value().c_str(), sizeof(Settings::wifiPass1));

    if (request->hasParam("wifiSsid2", true))
        strlcpy(Settings::wifiSsid2, request->getParam("wifiSsid2", true)->value().c_str(), sizeof(Settings::wifiSsid2));
    if (request->hasParam("wifiPass2", true) && request->getParam("wifiPass2", true)->value().length() > 0)
        strlcpy(Settings::wifiPass2, request->getParam("wifiPass2", true)->value().c_str(), sizeof(Settings::wifiPass2));

    if (request->hasParam("mqttHost", true))
        strlcpy(Settings::mqttHost, request->getParam("mqttHost", true)->value().c_str(), sizeof(Settings::mqttHost));
    if (request->hasParam("mqttPort", true))
        Settings::mqttPort = request->getParam("mqttPort", true)->value().toInt();
    if (request->hasParam("mqttUser", true))
        strlcpy(Settings::mqttUser, request->getParam("mqttUser", true)->value().c_str(), sizeof(Settings::mqttUser));
    if (request->hasParam("mqttPass", true) && request->getParam("mqttPass", true)->value().length() > 0)
        strlcpy(Settings::mqttPass, request->getParam("mqttPass", true)->value().c_str(), sizeof(Settings::mqttPass));
    if (request->hasParam("mqttPrefix", true)) {
        String value = request->getParam("mqttPrefix", true)->value();
        if (value.length() > 0) strlcpy(Settings::mqttPrefix, value.c_str(), sizeof(Settings::mqttPrefix));
    }

    if (request->hasParam("btFilter", true))
        Settings::btFilter = request->getParam("btFilter", true)->value() == "1";

    Settings::save();
    request->send(200, "text/plain", "Saved. Restarting...");
    scheduleRestart();
}

/**
 * POST /api/restart
*/
void WebHandler::handleRestart(AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Restarting...");
    scheduleRestart();
}

/**
 * POST /api/reset
*/
void WebHandler::handleReset(AsyncWebServerRequest *request) {
    Settings::reset();
    request->send(200, "text/plain", "Settings erased. Restarting...");
    scheduleRestart();
}

/**
 * POST /update (firmware upload via OTA)
*/
void WebHandler::handleUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t size, bool final) {
    if (index == 0) {
        Update.begin(UPDATE_SIZE_UNKNOWN);
    }

    if (size > 0 && !Update.hasError()) {
        Update.write(data, size);
    }

    if (final && !Update.hasError()) {
        Update.end(true);
    }
}

/**
 * fallback handler. redirects to the config page while in captive portal mode
*/
void WebHandler::handleNotFound(AsyncWebServerRequest *request) {
    if (WifiHandler::isAccessPoint()) {
        AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
        response->addHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
        request->send(response);
    } else {
        request->send(404, "text/plain", "Not found");
    }
}

/**
 * schedules a restart shortly after the current response went out
*/
void WebHandler::scheduleRestart(void) {
    restartPending = true;
    restartTimer = millis();
}

/**
 * minimal JSON string escaping
*/
String WebHandler::jsonEscape(const char* value) {
    String result = "";
    for (size_t i = 0; value[i] != '\0'; i++) {
        char c = value[i];
        if (c == '"' || c == '\\') result += '\\';
        if (c < 0x20) continue;
        result += c;
    }
    return result;
}
