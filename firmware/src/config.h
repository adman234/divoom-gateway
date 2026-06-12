#ifndef _CONFIG_H
    #define _CONFIG_H

    #ifndef LED_BUILTIN
        #define LED_BUILTIN  0
    #endif

    /* Firmware Information */
    #define FIRMWARE_NAME    "Divoom Gateway"
    #define FIRMWARE_VERSION "2.0.4"

    /* Bluetooth Configuration */
    #define BLUETOOTH_NAME   "Divoom-Gateway"
    #define BLUETOOTH_FILTER true

    /* WiFi Configuration */
    #define WIFI_NAME        "Divoom-Gateway"
    #define WIFI_RETRY       5

    /* The defines below are only DEFAULTS now. The actual values are stored
       in NVS (flash) and can be changed at runtime through the web interface
       or Improv provisioning. A config_local.h still works for pre-seeding. */

    #define WIFISSID1        ""
    #define WIFIPASS1        ""

    #define WIFISSID2        ""
    #define WIFIPASS2        ""

    /* Access Point Configuration (captive portal fallback) */
    #define AP_PASSWORD      "divoom1234"

    /* TCP Configuration */
    #define TCP_PORT         7777
    #define TCP_MAX          3

    /* MQTT Configuration */
    #define MQTT_HOST        ""
    #define MQTT_PORT        1883
    #define MQTT_USER        ""
    #define MQTT_PASS        ""
    #define MQTT_PREFIX      "divoom"
#endif

#if __has_include("config_local.h")
# include "config_local.h"
#endif
