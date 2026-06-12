
#include "webctl.h"

#include "Update.h"

#include "util.h"
#include "hardware/bluetoothctl.h"
#include "hardware/settings.h"
#include "hardware/webpage.h"
#include "hardware/wifictl.h"
#include "input/mqtt.h"

/**
 * setup functionality
*/
void WebHandler::setup(void) {
    server = new AsyncWebServer(80);

    server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        // gzipped page from flash: a single small allocation, which keeps the
        // response intact even when the heap is under pressure
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", WEBPAGE_GZ, WEBPAGE_GZ_LEN);
        response->addHeader("Content-Encoding", "gzip");
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
