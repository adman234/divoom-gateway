
#include "settings.h"

/**
 * setup functionality
*/
void Settings::setup(void) {
    preferences.begin("divoom", false);
    load();
}

/**
 * loads all settings from NVS, falling back to the compile-time defaults
*/
void Settings::load(void) {
    String value;

    value = preferences.getString("hostname", WIFI_NAME);
    strlcpy(hostname, value.c_str(), sizeof(hostname));

    value = preferences.getString("wifiSsid1", WIFISSID1);
    strlcpy(wifiSsid1, value.c_str(), sizeof(wifiSsid1));
    value = preferences.getString("wifiPass1", WIFIPASS1);
    strlcpy(wifiPass1, value.c_str(), sizeof(wifiPass1));

    value = preferences.getString("wifiSsid2", WIFISSID2);
    strlcpy(wifiSsid2, value.c_str(), sizeof(wifiSsid2));
    value = preferences.getString("wifiPass2", WIFIPASS2);
    strlcpy(wifiPass2, value.c_str(), sizeof(wifiPass2));

    value = preferences.getString("mqttHost", MQTT_HOST);
    strlcpy(mqttHost, value.c_str(), sizeof(mqttHost));
    mqttPort = preferences.getUShort("mqttPort", MQTT_PORT);
    value = preferences.getString("mqttUser", MQTT_USER);
    strlcpy(mqttUser, value.c_str(), sizeof(mqttUser));
    value = preferences.getString("mqttPass", MQTT_PASS);
    strlcpy(mqttPass, value.c_str(), sizeof(mqttPass));
    value = preferences.getString("mqttPrefix", MQTT_PREFIX);
    strlcpy(mqttPrefix, value.c_str(), sizeof(mqttPrefix));

    btFilter = preferences.getBool("btFilter", BLUETOOTH_FILTER);
}

/**
 * persists all settings into NVS
*/
void Settings::save(void) {
    preferences.putString("hostname", hostname);
    preferences.putString("wifiSsid1", wifiSsid1);
    preferences.putString("wifiPass1", wifiPass1);
    preferences.putString("wifiSsid2", wifiSsid2);
    preferences.putString("wifiPass2", wifiPass2);
    preferences.putString("mqttHost", mqttHost);
    preferences.putUShort("mqttPort", mqttPort);
    preferences.putString("mqttUser", mqttUser);
    preferences.putString("mqttPass", mqttPass);
    preferences.putString("mqttPrefix", mqttPrefix);
    preferences.putBool("btFilter", btFilter);
}

/**
 * clears all persisted settings (factory reset)
*/
void Settings::reset(void) {
    preferences.clear();
    load();
}

/**
 * checks if any wifi network is configured
*/
bool Settings::hasWifi(void) {
    return strlen(wifiSsid1) > 0 || strlen(wifiSsid2) > 0;
}

/**
 * checks if a MQTT broker is configured
*/
bool Settings::hasMqtt(void) {
    return strlen(mqttHost) > 0;
}

/**
 * stores wifi credentials into the first slot (used by Improv provisioning)
*/
void Settings::setWifi(const char* ssid, const char* pass) {
    strlcpy(wifiSsid1, ssid, sizeof(wifiSsid1));
    strlcpy(wifiPass1, pass, sizeof(wifiPass1));
    save();
}
