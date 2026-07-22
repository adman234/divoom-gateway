#include "divoom_gateway.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cstring>

#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome {
namespace divoom_gateway {

static const char *const TAG = "divoom_gateway";

// packet-framing chunk size for the animation-frame splitting below - a
// protocol-level constant (ties to the Divoom "set animation frame" command
// size), unrelated to CONFIG_LWIP_TCP_MSS which only bounds one raw TCP read
static const size_t FRAME_CHUNK_DEFAULT = 1096;
static const size_t FRAME_CHUNK_ANIMATION[] = {210, 213, 215};

DivoomGatewayComponent *DivoomGatewayComponent::instance_ = nullptr;

DivoomGatewayComponent::DivoomGatewayComponent() { instance_ = this; }

void DivoomGatewayComponent::setup() {
  this->serial_bt_.begin(App.get_name().c_str(), true);
  this->serial_bt_.setTimeout(1000);
  this->serial_bt_.register_callback(&DivoomGatewayComponent::spp_event_trampoline_);

  this->start_tcp_server_();

  this->tcp_parse_queue_ = xQueueCreate(3, sizeof(DataPacket *));
  if (this->tcp_parse_queue_ == nullptr) {
    ESP_LOGE(TAG, "failed to create TCP parse queue");
    this->mark_failed();
    return;
  }

  BaseType_t task_result = xTaskCreatePinnedToCore(&DivoomGatewayComponent::tcp_parse_task_trampoline_,
                                                    "DivoomTcpParse", 5120, this, 1,
                                                    &this->tcp_parse_task_handle_, 1);
  if (task_result != pdPASS) {
    ESP_LOGE(TAG, "failed to start TCP parse task, restarting");
    ESP.restart();
  }
}

void DivoomGatewayComponent::loop() {
  // Bluetooth inquiry and WiFi scanning share one radio: back off while our
  // AP/captive portal is up, mirroring the standalone firmware's coexistence
  // workaround, otherwise WiFi scans during provisioning come up empty.
  bool ap_active = (WiFi.getMode() & WIFI_MODE_AP) != 0;
  if (ap_active) return;

  if (millis() - this->bt_discover_timer_ > 15000) {
    this->bt_discover_timer_ = millis();

    BaseType_t task_result = xTaskCreatePinnedToCore(&DivoomGatewayComponent::bt_task_trampoline_, "DivoomBtScan",
                                                      2048, this, 1, &this->bt_task_handle_, 1);
    if (task_result != pdPASS) {
      ESP_LOGE(TAG, "failed to start Bluetooth scan task, restarting");
      ESP.restart();
    }
  }
}

void DivoomGatewayComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Divoom Gateway:");
  ESP_LOGCONFIG(TAG, "  TCP port: %u", this->tcp_port_);
  ESP_LOGCONFIG(TAG, "  Bluetooth name filter: %s", this->bluetooth_filter_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG, "  Bluetooth PIN: %s", this->pin_.empty() ? "(none)" : "(set)");
}

// --- Bluetooth Classic (SPP) ---
// Ported from the standalone firmware's hardware/bluetoothctl.cpp +
// output/bluetooth.cpp.

void DivoomGatewayComponent::bt_task_trampoline_(void *arg) {
  static_cast<DivoomGatewayComponent *>(arg)->bt_task_();
  vTaskDelete(nullptr);
}

void DivoomGatewayComponent::bt_task_() {
  if (this->serial_bt_.connected(5000)) {
    this->bt_connected_ = true;
    this->bt_connecting_ = false;
  } else {
    this->bt_connected_ = false;
    this->bt_connecting_ = false;
    this->bt_discover_(7500);
  }
}

void DivoomGatewayComponent::bt_discover_(int timeout_ms) {
  BTScanResults *devices = this->serial_bt_.discover(timeout_ms);
  if (devices == nullptr) {
    // matches the standalone firmware: some esp32-arduino Bluedroid versions
    // fail to re-arm discovery after a connection without a full restart
    ESP_LOGW(TAG, "Bluetooth discovery returned no results, restarting");
    ESP.restart();
    return;
  }

  this->discovered_.clear();
  for (int i = 0; i < devices->getCount(); i++) {
    BTAdvertisedDevice *device = devices->getDevice(i);

    bool supported = device->haveName();
    std::string name = device->haveName() ? device->getName() : "Unknown";
    if (name.find("Aurabox") == std::string::npos && name.find("AuraBox") == std::string::npos &&
        name.find("Timebox") == std::string::npos && name.find("TimeBox") == std::string::npos &&
        name.find("Ditoo") == std::string::npos && name.find("Pixoo") == std::string::npos &&
        name.find("Timoo") == std::string::npos && name.find("Tivoo") == std::string::npos &&
        name.find("Divoom") == std::string::npos)
      supported = false;
    if (this->bluetooth_filter_ && !supported) continue;

    if (supported) {
      MDNS.addServiceTxt("_divoom_esp32", "_tcp", "device_mac", device->getAddress().toString().c_str());
      MDNS.addServiceTxt("_divoom_esp32", "_tcp", "device_name", name.c_str());
    }

    if (this->discovered_.size() < BT_DISCOVERED_MAX) {
      this->discovered_.push_back({std::string(device->getAddress().toString().c_str()), name});
    }

    this->advertise_(device->getAddress().getNative(), name, supported);
    vTaskDelay(pdMS_TO_TICKS(25));
  }

  this->serial_bt_.discoverClear();
}

bool DivoomGatewayComponent::bt_connect_(const uint8_t address[6], uint16_t channel) {
  if (this->bt_connected_) this->bt_disconnect_();
  if (!this->pin_.empty()) this->serial_bt_.setPin(this->pin_.c_str());
  delay(10);

  esp_bd_addr_t bytes;
  memcpy(bytes, address, sizeof(bytes));
  BTAddress bt_address(bytes);

  this->bt_connecting_ = true;
  this->bt_connected_ = this->serial_bt_.connect(bt_address, channel);
  return this->bt_connected_;
}

bool DivoomGatewayComponent::bt_disconnect_() {
  this->bt_connected_ = false;
  this->bt_connecting_ = false;
  return this->serial_bt_.disconnect();
}

int DivoomGatewayComponent::bt_send_(const uint8_t *buffer, size_t size) {
  if (!this->bt_connected_ && !this->bt_connecting_) return -1;
  if (!this->bt_connected_ && this->bt_connecting_) return 0;
  return this->serial_bt_.write(buffer, size);
}

void DivoomGatewayComponent::on_spp_event_(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  switch (event) {
    case ESP_SPP_OPEN_EVT:
      this->bt_connected_ = true;
      this->bt_connecting_ = false;
      break;
    case ESP_SPP_CLOSE_EVT:
      this->bt_connected_ = false;
      this->bt_connecting_ = false;
      break;
    case ESP_SPP_DATA_IND_EVT: {
      uint8_t buffer[64];
      size_t available;
      while ((available = this->serial_bt_.available()) > 0) {
        size_t size = this->serial_bt_.readBytes(buffer, std::min(available, sizeof(buffer)));
        this->backward_(buffer, size);
      }
      break;
    }
    default:
      break;
  }
}

void DivoomGatewayComponent::spp_event_trampoline_(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if (instance_ != nullptr) instance_->on_spp_event_(event, param);
}

// --- TCP passthrough ---
// Ported from the standalone firmware's input/tcp.cpp. Wire protocol is
// unchanged: 0x69+MAC+port = connect, 0x96+MAC = disconnect, 0x01...0x02 =
// raw Divoom payload to relay to/from the Bluetooth device.

void DivoomGatewayComponent::start_tcp_server_() {
  this->tcp_server_ = new AsyncServer(this->tcp_port_);
  this->tcp_server_->onClient(&DivoomGatewayComponent::tcp_client_connect_trampoline_, this);
  this->tcp_server_->begin();
}

void DivoomGatewayComponent::tcp_client_connect_trampoline_(void *arg, AsyncClient *client) {
  static_cast<DivoomGatewayComponent *>(arg)->on_tcp_client_(client);
}

void DivoomGatewayComponent::on_tcp_client_(AsyncClient *client) {
  client->onData(&DivoomGatewayComponent::tcp_client_data_trampoline_, this);
  client->onDisconnect(&DivoomGatewayComponent::tcp_client_disconnect_trampoline_, this);
  client->onTimeout(&DivoomGatewayComponent::tcp_client_timeout_trampoline_, this);
  client->onError(&DivoomGatewayComponent::tcp_client_error_trampoline_, this);

  int8_t index = -1;
  for (size_t i = 0; i < TCP_MAX_CLIENTS; i++) {
    if (this->tcp_clients_[i] != nullptr) continue;
    index = i;
    break;
  }

  if (index < 0) {
    this->tcp_clear_clients_();
    index = 0;
  }
  this->tcp_clients_[index] = client;
}

void DivoomGatewayComponent::tcp_clear_clients_() {
  for (size_t i = 0; i < TCP_MAX_CLIENTS; i++) {
    if (this->tcp_clients_[i] == nullptr) continue;
    AsyncClient *client = this->tcp_clients_[i];
    this->tcp_clients_[i] = nullptr;
    client->abort();
    delete client;
  }
}

void DivoomGatewayComponent::tcp_client_data_trampoline_(void *arg, AsyncClient *client, void *data, size_t size) {
  auto *self = static_cast<DivoomGatewayComponent *>(arg);

  auto *packet = static_cast<DataPacket *>(malloc(sizeof(DataPacket)));
  if (packet == nullptr) {
    ESP_LOGE(TAG, "out of memory handling TCP data, restarting");
    ESP.restart();
    return;
  }

  packet->size = std::min(size, sizeof(packet->data));
  memcpy(packet->data, data, packet->size);

  if (xQueueSend(self->tcp_parse_queue_, &packet, pdMS_TO_TICKS(25)) != pdPASS) {
    free(packet);
  }
}

void DivoomGatewayComponent::tcp_client_disconnect_trampoline_(void *arg, AsyncClient *client) {
  auto *self = static_cast<DivoomGatewayComponent *>(arg);
  for (size_t i = 0; i < TCP_MAX_CLIENTS; i++) {
    if (self->tcp_clients_[i] != client) continue;
    self->tcp_clients_[i] = nullptr;
    client->close();
    delete client;
    break;
  }
}

void DivoomGatewayComponent::tcp_client_error_trampoline_(void *arg, AsyncClient *client, int8_t error) {
  auto *self = static_cast<DivoomGatewayComponent *>(arg);
  for (size_t i = 0; i < TCP_MAX_CLIENTS; i++) {
    if (self->tcp_clients_[i] != client) continue;
    self->tcp_clients_[i] = nullptr;
    client->abort();
    delete client;
    break;
  }
}

void DivoomGatewayComponent::tcp_client_timeout_trampoline_(void *arg, AsyncClient *client, uint32_t time) {
  auto *self = static_cast<DivoomGatewayComponent *>(arg);
  for (size_t i = 0; i < TCP_MAX_CLIENTS; i++) {
    if (self->tcp_clients_[i] != client) continue;
    self->tcp_clients_[i] = nullptr;
    client->abort();
    delete client;
    break;
  }
}

void DivoomGatewayComponent::tcp_parse_task_trampoline_(void *arg) {
  auto *self = static_cast<DivoomGatewayComponent *>(arg);
  esp_task_wdt_add(nullptr);

  size_t previous_size = 0;
  uint8_t previous_buffer[FRAME_CHUNK_DEFAULT] = {0x00};

  for (;;) {
    DataPacket *packet;
    if (xQueueReceive(self->tcp_parse_queue_, &packet, pdMS_TO_TICKS(25)) == pdPASS) {
      size_t off = 0;
      size_t max = FRAME_CHUNK_DEFAULT;
      size_t len = previous_size + packet->size;
      uint8_t *packet_buffer = packet->data;

      // detect an animation-frame stream and switch to its smaller chunk
      // size, so every frame is handed to tcp_parse_ as its own message
      for (size_t chunk : FRAME_CHUNK_ANIMATION) {
        uint8_t pos = chunk - 1;
        uint8_t *start_buffer = previous_size > 0 ? previous_buffer : packet_buffer;
        uint8_t *end_buffer = previous_size > chunk ? previous_buffer : packet_buffer;
        uint8_t end_position = previous_size > chunk ? pos : pos - previous_size;
        if (start_buffer[0] == 0x01 && start_buffer[3] == 0x49 && end_buffer[end_position] == 0x02) max = chunk;
      }

      while (len > 0) {
        uint8_t this_buffer[FRAME_CHUNK_DEFAULT] = {0x00};
        size_t use = len > max ? max : len;
        if (previous_size > 0) memcpy(this_buffer, previous_buffer, previous_size);
        memcpy(this_buffer + previous_size, packet_buffer, use - previous_size);

        self->tcp_parse_(this_buffer, use);

        off += use - previous_size;
        packet_buffer += use - previous_size;
        len = packet->size - off;
        previous_size = 0;

        // carry an incomplete trailing message over to the next packet
        // instead of forwarding it early, if more data is already queued
        if (len > 0 && len <= max && packet_buffer[len - 1] != 0x02) {
          previous_size = len;
          memcpy(previous_buffer, packet_buffer, len);
          break;
        }
      }

      free(packet);
    }

    esp_task_wdt_reset();
    vTaskDelay(1);
  }
}

void DivoomGatewayComponent::tcp_parse_(const uint8_t *buffer, size_t size) {
  // connect statement: 0x69 + 6-byte MAC [+ port]
  if (buffer[0] == 0x69 && size >= 7 && size <= 8) {
    uint16_t port = size > 7 ? buffer[7] : 1;
    this->bt_connect_(buffer + 1, port);
  }

  // disconnect statement: 0x96 + 6-byte MAC
  if (buffer[0] == 0x96 && size == 7) {
    this->bt_disconnect_();
  }

  // raw Divoom payload, framed between 0x01 and 0x02
  if (buffer[0] == 0x01 && buffer[size - 1] == 0x02) {
    int result = this->bt_send_(buffer, size);
    if (result == 0) {  // still connecting
      const uint8_t data[1] = {0x69};
      this->backward_(data, 1);
    } else if (result == -1) {  // no connection
      const uint8_t data[1] = {0x96};
      this->backward_(data, 1);
    }
  }
}

void DivoomGatewayComponent::tcp_write_(const uint8_t *buffer, size_t size) {
  for (size_t i = 0; i < TCP_MAX_CLIENTS; i++) {
    AsyncClient *client = this->tcp_clients_[i];
    if (client == nullptr || !client->connected()) continue;

    if (client->space() > size && client->canSend()) {
      client->add(reinterpret_cast<const char *>(buffer), size);
      client->send();
    }
  }
}

void DivoomGatewayComponent::backward_(const uint8_t *buffer, size_t size) { this->tcp_write_(buffer, size); }

void DivoomGatewayComponent::advertise_(const uint8_t address[6], const std::string &name, bool supported) {
  if (!supported) return;

  size_t length = 6 + name.size() + 2;
  std::vector<uint8_t> buffer(length);
  size_t index = 0;
  buffer[index++] = 0x00;
  for (size_t i = 0; i < 6; i++) buffer[index++] = address[i];
  buffer[index++] = static_cast<uint8_t>(name.size());
  memcpy(&buffer[index], name.data(), name.size());

  this->tcp_write_(buffer.data(), buffer.size());
}

}  // namespace divoom_gateway
}  // namespace esphome
