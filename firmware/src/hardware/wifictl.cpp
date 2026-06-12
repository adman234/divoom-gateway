
#include "wifictl.h"

#include "util.h"
#include "hardware/settings.h"
#include "input/base.h"
#include "output/base.h"

WifiHandler::WifiHandler() {
    timer = millis();
}

/**
 * setup functionality
*/
void WifiHandler::setup(void) {
    WiFi.disconnect(true);
    WiFi.persistent(true);
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
    WiFi.setHostname(Settings::hostname);
    WiFi.onEvent(scanned, WiFiEvent_t::ARDUINO_EVENT_WIFI_SCAN_DONE);
    WiFi.onEvent(connected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(disconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    if (Settings::hasWifi()) {
        WiFi.scanNetworks(true, false, false, 500);
    } else {
        // nothing configured yet: open the captive portal right away
        startAccessPoint();
    }
}

/**
 * loop functionality
*/
void WifiHandler::loop(void) {
    if (isApMode) {
        dnsServer.processNextRequest();
    }

    if (getElapsed(timer) > 10000) {
        timer = millis();

        check(false);
    }
}

/**
 * checks connection and scanning state and keeps background tasks up
*/
bool WifiHandler::check(bool fast) {
    if (fast) return isConnected;

    if (WiFi.status() == WL_CONNECTED)
    {
        isConnected = true;
        retryCount = 0;
        return true;
    }
    else
    {
        isConnected = false;
        if (!Settings::hasWifi()) {
            if (!isApMode) startAccessPoint();
            return false;
        }

        // after too many retries, fall back to the captive portal but
        // keep scanning in the background, in case the network comes back
        if (retryCount >= WIFI_RETRY && !isApMode) startAccessPoint();

        int8_t wifiStatus = WiFi.status();
        int8_t scanStatus = WiFi.scanComplete();
        if ((wifiStatus == WL_IDLE_STATUS ||
             wifiStatus == WL_NO_SSID_AVAIL ||
             wifiStatus == WL_CONNECT_FAILED ||
             wifiStatus == WL_DISCONNECTED) && scanStatus != -1) {
            if (!isApMode || getElapsed(apTimer) > 30000) {
                apTimer = millis();
                WiFi.scanNetworks(true, false, false, 2500);
                retryCount++;
            }
        }

        return WiFi.status() == WL_CONNECTED;
    }
}

/**
 * connects to one of the scanned wifi networks
*/
void WifiHandler::connect(void) {
    if (WiFi.status() == WL_CONNECTED) return;

    int16_t count = WiFi.scanComplete();
    for (uint8_t i = 0; i < count && i < 99; i++) {
        auto ssid = WiFi.SSID(i);
        if (ssid == NULL || ssid.length() == 0) continue;

        if (ssid == Settings::wifiSsid1) { WiFi.begin(Settings::wifiSsid1, Settings::wifiPass1); break; }
        if (ssid == Settings::wifiSsid2) { WiFi.begin(Settings::wifiSsid2, Settings::wifiPass2); break; }
    }

    WiFi.scanDelete();
}

/**
 * applies new credentials immediately (after Improv or web configuration)
*/
void WifiHandler::reconnect(void) {
    retryCount = 0;
    if (strlen(Settings::wifiSsid1) > 0) {
        WiFi.begin(Settings::wifiSsid1, Settings::wifiPass1);
    } else if (strlen(Settings::wifiSsid2) > 0) {
        WiFi.begin(Settings::wifiSsid2, Settings::wifiPass2);
    }
}

/**
 * whether the fallback access point is currently active
*/
bool WifiHandler::isAccessPoint(void) {
    return isApMode;
}

/**
 * starts the fallback access point including the captive portal DNS
*/
void WifiHandler::startAccessPoint(void) {
    if (isApMode) return;
    isApMode = true;
    apTimer = millis();

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(Settings::hostname, strlen(AP_PASSWORD) > 0 ? AP_PASSWORD : NULL);

    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", WiFi.softAPIP());

    Serial.print("AP: ");
    WiFi.softAPIP().printTo(Serial);
    Serial.println("");
}

/**
 * stops the fallback access point
*/
void WifiHandler::stopAccessPoint(void) {
    if (!isApMode) return;
    isApMode = false;

    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
}

/**
 * callback for when the scan finished
*/
void WifiHandler::scanned(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (info.wifi_scan_done.number > 0) connect();
}

/**
 * callback for when we connected to a network
*/
void WifiHandler::connected(WiFiEvent_t event, WiFiEventInfo_t info) {
    isConnected = true;
    retryCount = 0;

    stopAccessPoint();

    WiFi.setTxPower(WIFI_POWER_15dBm);
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
    WiFi.setHostname(Settings::hostname);
    WiFi.persistent(true);

    MDNS.begin(Settings::hostname);
    MDNS.addService("_divoom_esp32", "_tcp", TCP_PORT);
    MDNS.addService("_http", "_tcp", 80);

    Serial.print("IP: ");
    WiFi.localIP().printTo(Serial);
    Serial.println("");
}

/**
 * callback for when we disconnected from a network
*/
void WifiHandler::disconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    isConnected = false;
}
