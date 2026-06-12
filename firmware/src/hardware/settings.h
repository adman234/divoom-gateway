
#ifndef _SETTINGS_H
    #define _SETTINGS_H

    #include "config.h"

    #include "Arduino.h"
    #include "Preferences.h"

    class Settings {
        public:
            static void setup(void);
            static void save(void);
            static void reset(void);

            static bool hasWifi(void);
            static bool hasMqtt(void);
            static void setWifi(const char* ssid, const char* pass);

            // values are kept in static buffers, so libraries that keep
            // pointers (e.g. AsyncMqttClient) stay valid for the whole runtime
            inline static char hostname[33];
            inline static char wifiSsid1[33];
            inline static char wifiPass1[65];
            inline static char wifiSsid2[33];
            inline static char wifiPass2[65];
            inline static char mqttHost[65];
            inline static uint16_t mqttPort;
            inline static char mqttUser[33];
            inline static char mqttPass[65];
            inline static char mqttPrefix[33];
            inline static bool btFilter;

        private:
            inline static Preferences preferences;
            static void load(void);
    };
#endif
