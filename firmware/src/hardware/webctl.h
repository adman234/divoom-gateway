
#ifndef _WEB_H
    #define _WEB_H

    #include "config.h"

    #include "Arduino.h"
    #include "ESPAsyncWebServer.h"

    class WebHandler {
        public:
            static void setup(void);
            static void loop(void);

        private:
            inline static AsyncWebServer* server;
            inline static unsigned long restartTimer;
            inline static bool restartPending;

            static void handleStatus(AsyncWebServerRequest *request);
            static void handleConfigGet(AsyncWebServerRequest *request);
            static void handleConfigPost(AsyncWebServerRequest *request);
            static void handleRestart(AsyncWebServerRequest *request);
            static void handleReset(AsyncWebServerRequest *request);
            static void handleUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t size, bool final);
            static void handleNotFound(AsyncWebServerRequest *request);

            static void scheduleRestart(void);
            static String jsonEscape(const char* value);
    };
#endif
