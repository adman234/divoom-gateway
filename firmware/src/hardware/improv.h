
#ifndef _IMPROV_H
    #define _IMPROV_H

    #include "config.h"

    #include "Arduino.h"

    /**
     * Implements the Improv WiFi serial protocol (https://www.improv-wifi.com/serial/),
     * so the device can be provisioned directly from the browser via ESP Web Tools.
    */
    class ImprovHandler {
        public:
            static void loop(void);

            // feeds a single byte from the serial stream into the improv state machine.
            // returns 0 if the byte is not part of an improv frame (treat as text input),
            // 1 if the byte was consumed by an improv frame and
            // 2 if the byte just completed the "IMPROV" header (the previous 5 bytes
            //   leaked into the text buffer and should be dropped by the caller).
            static int feed(uint8_t data);

        private:
            inline static uint8_t buffer[300];
            inline static size_t position;
            inline static bool provisioning;
            inline static unsigned long provisionTimer;

            static void handle(void);
            static void handleRpc(const uint8_t *payload, size_t size);
            static void sendState(uint8_t state);
            static void sendError(uint8_t error);
            static void sendRpcResult(uint8_t command, const char* strings[], size_t count);
            static void sendPacket(uint8_t type, const uint8_t *payload, size_t size);
            static uint8_t currentState(void);
    };
#endif
