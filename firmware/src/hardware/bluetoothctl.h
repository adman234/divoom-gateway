
#ifndef _BLUETOOTH_H
    #define _BLUETOOTH_H

    #include "config.h"
    
    #include "Arduino.h"
    #include "BluetoothSerial.h"
    #include "ESPmDNS.h"
    #include "esp_task_wdt.h"

    #if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
    #error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
    #endif

    #if !defined(CONFIG_BT_SPP_ENABLED)
    #error Serial Bluetooth not available or not enabled. It is only available for the ESP32 chip.
    #endif

    typedef struct {
        char mac[18];
        char name[33];
    } bt_device_t;

    #define BT_DISCOVERED_MAX 8

    class BluetoothHandler {
        public:
            BluetoothHandler();
            static void setup(void);
            static void loop(void);
            static void stop(void);
            static bool check(void);

            static bool connect(BTAddress address, uint16_t channel, const char *pin);
            static bool connect(BTAddress address, uint16_t channel);
            static bool disconnect(void);

            static size_t send(const uint8_t *buffer, size_t size);

            // last discovery results, for the web interface
            inline static bt_device_t discovered[BT_DISCOVERED_MAX];
            inline static size_t discoveredCount;

        private:
            inline static bool isConnected;
            inline static bool isConnecting;
            inline static unsigned long timer;
            inline static BluetoothSerial serialBT;
            inline static TaskHandle_t discoverHandle;

            static void discover(int timeout);
            static void task(void *parameter);
            static void event(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);
    };
#endif