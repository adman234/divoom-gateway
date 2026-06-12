
#include "improv.h"

#include "util.h"
#include "hardware/settings.h"
#include "hardware/wifictl.h"

// improv protocol constants
#define IMPROV_VERSION          0x01

#define TYPE_CURRENT_STATE      0x01
#define TYPE_ERROR_STATE        0x02
#define TYPE_RPC_COMMAND        0x03
#define TYPE_RPC_RESULT         0x04

#define STATE_READY             0x02
#define STATE_PROVISIONING      0x03
#define STATE_PROVISIONED       0x04

#define ERROR_NONE              0x00
#define ERROR_INVALID_RPC       0x01
#define ERROR_UNKNOWN_RPC       0x02
#define ERROR_UNABLE_TO_CONNECT 0x03
#define ERROR_UNKNOWN           0xFF

#define RPC_WIFI_SETTINGS       0x01
#define RPC_CURRENT_STATE       0x02
#define RPC_DEVICE_INFO         0x03
#define RPC_WIFI_NETWORKS       0x04

static const char improvHeader[] = "IMPROV";

/**
 * loop functionality. finishes or aborts a pending provisioning attempt
*/
void ImprovHandler::loop(void) {
    if (!provisioning) return;

    if (WiFi.status() == WL_CONNECTED) {
        provisioning = false;

        char url[64];
        snprintf(url, sizeof(url), "http://%s/", WiFi.localIP().toString().c_str());
        const char* strings[] = { url };

        sendState(STATE_PROVISIONED);
        sendRpcResult(RPC_WIFI_SETTINGS, strings, 1);
        return;
    }

    if (getElapsed(provisionTimer) > 30000) {
        provisioning = false;
        sendError(ERROR_UNABLE_TO_CONNECT);
        sendState(STATE_READY);
    }
}

/**
 * feeds a single byte into the improv frame state machine
*/
int ImprovHandler::feed(uint8_t data) {
    if (position < 6) {
        if (data == improvHeader[position]) {
            buffer[position++] = data;
            return position == 6 ? 2 : 0;
        }

        position = 0;
        if (data == improvHeader[0]) buffer[position++] = data;
        return 0;
    }

    buffer[position++] = data;

    // frame layout: [0..5] header, [6] version, [7] type, [8] length, [9..] data, [last] checksum
    if (position == 7 && buffer[6] != IMPROV_VERSION) {
        position = 0;
        return 1;
    }

    if (position >= 10) {
        size_t total = 9 + buffer[8] + 1;
        if (position >= total) {
            handle();
            position = 0;
        }
    }

    if (position >= sizeof(buffer)) position = 0;
    return 1;
}

/**
 * handles a complete improv frame
*/
void ImprovHandler::handle(void) {
    uint8_t type = buffer[7];
    uint8_t length = buffer[8];

    uint8_t checksum = 0x00;
    for (size_t i = 0; i < 9 + length; i++) checksum += buffer[i];
    if (checksum != buffer[9 + length]) {
        sendError(ERROR_INVALID_RPC);
        return;
    }

    if (type == TYPE_RPC_COMMAND) {
        handleRpc(buffer + 9, length);
    }
}

/**
 * handles an improv RPC command
*/
void ImprovHandler::handleRpc(const uint8_t *payload, size_t size) {
    if (size < 2) {
        sendError(ERROR_INVALID_RPC);
        return;
    }

    uint8_t command = payload[0];
    switch (command) {
        case RPC_WIFI_SETTINGS: {
            uint8_t ssidLength = payload[2];
            if (3 + ssidLength + 1 > size) {
                sendError(ERROR_INVALID_RPC);
                return;
            }
            uint8_t passLength = payload[3 + ssidLength];

            char ssid[33] = { 0 };
            char pass[65] = { 0 };
            memcpy(ssid, payload + 3, min((size_t)ssidLength, sizeof(ssid) - 1));
            memcpy(pass, payload + 4 + ssidLength, min((size_t)passLength, sizeof(pass) - 1));

            Settings::setWifi(ssid, pass);

            provisioning = true;
            provisionTimer = millis();
            sendState(STATE_PROVISIONING);
            WifiHandler::reconnect();
            break;
        }

        case RPC_CURRENT_STATE: {
            sendState(currentState());
            if (currentState() == STATE_PROVISIONED) {
                char url[64];
                snprintf(url, sizeof(url), "http://%s/", WiFi.localIP().toString().c_str());
                const char* strings[] = { url };
                sendRpcResult(RPC_CURRENT_STATE, strings, 1);
            }
            break;
        }

        case RPC_DEVICE_INFO: {
            const char* strings[] = { FIRMWARE_NAME, FIRMWARE_VERSION, "ESP32", Settings::hostname };
            sendRpcResult(RPC_DEVICE_INFO, strings, 4);
            break;
        }

        case RPC_WIFI_NETWORKS: {
            int16_t count = WiFi.scanComplete();
            if (count <= 0) {
                count = WiFi.scanNetworks(false, false, false, 500);
            }

            for (int16_t i = 0; i < count; i++) {
                auto ssid = WiFi.SSID(i);
                if (ssid == NULL || ssid.length() == 0) continue;

                char rssi[8];
                snprintf(rssi, sizeof(rssi), "%d", WiFi.RSSI(i));
                const char* strings[] = { ssid.c_str(), rssi, WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES" };
                sendRpcResult(RPC_WIFI_NETWORKS, strings, 3);
            }

            sendRpcResult(RPC_WIFI_NETWORKS, nullptr, 0);
            break;
        }

        default:
            sendError(ERROR_UNKNOWN_RPC);
            break;
    }
}

/**
 * determines the current improv state
*/
uint8_t ImprovHandler::currentState(void) {
    if (provisioning) return STATE_PROVISIONING;
    if (WiFi.status() == WL_CONNECTED) return STATE_PROVISIONED;
    return STATE_READY;
}

/**
 * sends an improv state packet
*/
void ImprovHandler::sendState(uint8_t state) {
    sendPacket(TYPE_CURRENT_STATE, &state, 1);
}

/**
 * sends an improv error packet
*/
void ImprovHandler::sendError(uint8_t error) {
    sendPacket(TYPE_ERROR_STATE, &error, 1);
}

/**
 * sends an improv RPC result packet containing the given strings
*/
void ImprovHandler::sendRpcResult(uint8_t command, const char* strings[], size_t count) {
    uint8_t payload[256];
    size_t index = 0;

    payload[index++] = command;
    payload[index++] = 0x00; // placeholder for data length

    for (size_t i = 0; i < count; i++) {
        size_t length = strlen(strings[i]);
        if (index + length + 1 >= sizeof(payload)) break;

        payload[index++] = length;
        memcpy(payload + index, strings[i], length);
        index += length;
    }

    payload[1] = index - 2;
    sendPacket(TYPE_RPC_RESULT, payload, index);
}

/**
 * sends a raw improv packet incl. header, version and checksum
*/
void ImprovHandler::sendPacket(uint8_t type, const uint8_t *payload, size_t size) {
    uint8_t packet[300];
    size_t index = 0;

    memcpy(packet, improvHeader, 6);
    index += 6;
    packet[index++] = IMPROV_VERSION;
    packet[index++] = type;
    packet[index++] = size;
    memcpy(packet + index, payload, size);
    index += size;

    uint8_t checksum = 0x00;
    for (size_t i = 0; i < index; i++) checksum += packet[i];
    packet[index++] = checksum;

    Serial.write(packet, index);
    Serial.println("");
}
