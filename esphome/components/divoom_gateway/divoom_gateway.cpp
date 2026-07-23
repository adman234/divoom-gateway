#include "divoom_gateway.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <esp_bt.h>
#include <esp_bt_main.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/components/wifi/wifi_component.h"

namespace esphome {
namespace divoom_gateway {

// The Arduino WiFi.status() API isn't reliably kept in sync with ESPHome's
// own wifi: component under the Arduino-as-ESP-IDF-component build mode - it
// reported disconnected indefinitely even while wifi:'s own dump_config
// showed "Connected: YES". Ask ESPHome's wifi component directly instead.
static bool wifi_is_connected() {
  return wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_connected();
}

static const char *const TAG = "divoom_gateway";

// packet-framing chunk size for the animation-frame splitting below - a
// protocol-level constant (ties to the Divoom "set animation frame" command
// size), unrelated to CONFIG_LWIP_TCP_MSS which only bounds one raw TCP read
static const size_t FRAME_CHUNK_DEFAULT = 1096;
static const size_t FRAME_CHUNK_ANIMATION[] = {210, 213, 215};

DivoomGatewayComponent *DivoomGatewayComponent::instance_ = nullptr;

DivoomGatewayComponent::DivoomGatewayComponent() {
  instance_ = this;
  for (auto &fd : this->tcp_client_fds_) fd = -1;
}

void DivoomGatewayComponent::setup() {
  this->setup_ran_ = true;
  ESP_LOGI(TAG, "setup() starting");

  // disableBLE=true: we only ever enabled Classic-BT sdkconfig options
  // (CONFIG_BT_CLASSIC_ENABLED/CONFIG_BT_SPP_ENABLED, no BLE ones), but the
  // default (false) requests BTDM (Classic+BLE dual mode) - a likely
  // contributor to esp_bt_gap_start_discovery() failing instantly every
  // time (see bt_discover_'s 0ms-return diagnostic).
  if (!this->serial_bt_.begin(App.get_name().c_str(), true, true)) {
    ESP_LOGE(TAG, "BluetoothSerial.begin() failed - Bluetooth Classic will not work");
  }
  this->serial_bt_.setTimeout(1000);
  this->serial_bt_.register_callback(&DivoomGatewayComponent::spp_event_trampoline_);

  // create the parse queue/task before the TCP listener starts accepting, so
  // a client that connects immediately never races an as-yet-nonexistent queue
  ESP_LOGI(TAG, "free heap before TCP queue alloc: %u bytes", ESP.getFreeHeap());
  this->tcp_parse_queue_ = xQueueCreate(3, sizeof(DataPacket *));
  if (this->tcp_parse_queue_ == nullptr) {
    ESP_LOGE(TAG, "failed to create TCP parse queue (free heap %u bytes) - component will not run (loop() disabled)",
             ESP.getFreeHeap());
    this->mark_failed();
    return;
  }

  BaseType_t task_result = xTaskCreatePinnedToCore(&DivoomGatewayComponent::tcp_parse_task_trampoline_,
                                                    "DivoomTcpParse", 5120, this, 1,
                                                    &this->tcp_parse_task_handle_, 1);
  if (task_result != pdPASS) {
    ESP_LOGE(TAG, "failed to start TCP parse task, restarting");
    ESP.restart();
    return;
  }

  this->start_tcp_server_();
  ESP_LOGI(TAG, "setup() finished (failed = %s)", this->is_failed() ? "YES" : "NO");
}

void DivoomGatewayComponent::loop() {
  // unmissable heartbeat, throttled to the same 15s cadence as the scan
  // trigger below - confirms loop() is even being called and what gate it's
  // hitting, regardless of how deep any actual Bluetooth call gets
  bool due = millis() - this->bt_discover_timer_ > 15000;
  static uint32_t last_heartbeat = 0;
  if (millis() - last_heartbeat > 15000) {
    last_heartbeat = millis();
    ESP_LOGI(TAG, "loop() heartbeat: wifi_connected=%s bt_connected=%s bt_connecting=%s scan_due=%s",
             wifi_is_connected() ? "YES" : "NO", this->bt_connected_ ? "YES" : "NO",
             this->bt_connecting_ ? "YES" : "NO", due ? "YES" : "NO");
  }

  // Bluetooth inquiry and WiFi scanning share one radio: back off until WiFi
  // has actually joined a network, mirroring the standalone firmware's
  // coexistence workaround (there, checking "is our AP up" was equivalent,
  // since that firmware's own WiFi handler tore the AP down on connect - but
  // ESPHome's ap:/captive_portal: fallback commonly stays up in parallel
  // (WIFI_MODE_APSTA) even after joining, so that check would never clear
  // here and Bluetooth discovery would never run at all). Uses ESPHome's own
  // wifi: component state, not Arduino's WiFi.status(), which was observed
  // stuck reporting disconnected indefinitely under this build mode.
  if (!wifi_is_connected()) return;

  if (due) {
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
  ESP_LOGCONFIG(TAG, "  DIAGNOSTIC BUILD MARKER 2026-07-22c: setup() ran = %s, component failed = %s",
                this->setup_ran_ ? "YES" : "NO", this->is_failed() ? "YES" : "NO");
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
  ESP_LOGD(TAG, "starting Bluetooth discovery scan (requested timeout %d ms)", timeout_ms);
  // read-only, direct from the ESP-IDF Bluetooth stack: does it actually
  // think it's enabled at this point? (controller status: 0=idle 1=inited
  // 2=enabled; bluedroid status: 0=uninitialized 1=initialized 2=enabled -
  // esp_bt_gap_start_discovery() needs both at their "enabled" value)
  ESP_LOGI(TAG, "bt controller_status=%d bluedroid_status=%d", static_cast<int>(esp_bt_controller_get_status()),
           static_cast<int>(esp_bluedroid_get_status()));
  uint32_t scan_start = millis();
  BTScanResults *devices = this->serial_bt_.discover(timeout_ms);
  uint32_t elapsed = millis() - scan_start;
  if (devices == nullptr) {
    // matches the standalone firmware: some esp32-arduino Bluedroid versions
    // fail to re-arm discovery after a connection without a full restart
    ESP_LOGW(TAG, "Bluetooth discovery returned no results after %u ms, restarting", elapsed);
    ESP.restart();
    return;
  }
  if (elapsed < static_cast<uint32_t>(timeout_ms) / 2) {
    // a real inquiry scan should take close to the full requested duration;
    // returning much sooner usually means the scan bailed out/failed rather
    // than actually finding nothing
    ESP_LOGW(TAG, "discover() returned after only %u ms (requested %d ms) - scan likely failed rather than "
                  "finding nothing",
             elapsed, timeout_ms);
  }

  this->discovered_.clear();
  ESP_LOGD(TAG, "raw scan results: %d device(s)", devices->getCount());
  for (int i = 0; i < devices->getCount(); i++) {
    BTAdvertisedDevice *device = devices->getDevice(i);

    bool supported = device->haveName();
    std::string name = device->haveName() ? device->getName() : "Unknown";
    ESP_LOGD(TAG, "  [%d] %s (%s)", i, device->getAddress().toString().c_str(), name.c_str());
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

    this->advertise_(*device->getAddress().getNative(), name, supported);
    vTaskDelay(pdMS_TO_TICKS(25));
  }

  this->serial_bt_.discoverClear();
  ESP_LOGD(TAG, "Bluetooth discovery scan finished in %u ms: %d device(s) seen, %u kept", elapsed,
           devices->getCount(), static_cast<unsigned>(this->discovered_.size()));
}

bool DivoomGatewayComponent::bt_connect_(const uint8_t address[6], uint16_t channel) {
  if (this->bt_connected_) this->bt_disconnect_();
  if (!this->pin_.empty())
    this->serial_bt_.setPin(this->pin_.c_str(), static_cast<uint8_t>(this->pin_.size()));
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
// Ported from the standalone firmware's input/tcp.cpp, but on plain BSD/lwIP
// sockets instead of AsyncTCP: ESPHome's own web_server_base component no
// longer uses AsyncTCP on ESP32 either (it switched to a native ESP-IDF
// socket implementation), and the AsyncTCP-esphome library's expectations
// around Arduino-as-an-esp-idf-component headers (IPv6Address.h) don't line
// up cleanly there anymore. Wire protocol is unchanged: 0x69+MAC+port =
// connect, 0x96+MAC = disconnect, 0x01...0x02 = raw Divoom payload to relay
// to/from the Bluetooth device.

void DivoomGatewayComponent::start_tcp_server_() {
  this->tcp_listen_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (this->tcp_listen_fd_ < 0) {
    ESP_LOGE(TAG, "failed to create TCP listen socket (errno %d)", errno);
    this->mark_failed();
    return;
  }

  int reuse = 1;
  setsockopt(this->tcp_listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(this->tcp_port_);

  if (bind(this->tcp_listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    ESP_LOGE(TAG, "failed to bind TCP port %u (errno %d)", this->tcp_port_, errno);
    this->mark_failed();
    return;
  }
  if (listen(this->tcp_listen_fd_, TCP_MAX_CLIENTS) != 0) {
    ESP_LOGE(TAG, "failed to listen on TCP port %u (errno %d)", this->tcp_port_, errno);
    this->mark_failed();
    return;
  }

  int flags = fcntl(this->tcp_listen_fd_, F_GETFL, 0);
  fcntl(this->tcp_listen_fd_, F_SETFL, flags | O_NONBLOCK);

  BaseType_t task_result = xTaskCreatePinnedToCore(&DivoomGatewayComponent::tcp_socket_task_trampoline_,
                                                    "DivoomTcpSocket", 5120, this, 1,
                                                    &this->tcp_socket_task_handle_, 1);
  if (task_result != pdPASS) {
    ESP_LOGE(TAG, "failed to start TCP socket task, restarting");
    ESP.restart();
  }
}

void DivoomGatewayComponent::tcp_socket_task_trampoline_(void *arg) {
  static_cast<DivoomGatewayComponent *>(arg)->tcp_socket_task_();
}

void DivoomGatewayComponent::tcp_socket_task_() {
  esp_task_wdt_add(nullptr);

  for (;;) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(this->tcp_listen_fd_, &read_fds);
    int max_fd = this->tcp_listen_fd_;

    for (size_t i = 0; i < TCP_MAX_CLIENTS; i++) {
      if (this->tcp_client_fds_[i] < 0) continue;
      FD_SET(this->tcp_client_fds_[i], &read_fds);
      max_fd = std::max(max_fd, this->tcp_client_fds_[i]);
    }

    timeval timeout{.tv_sec = 0, .tv_usec = 100000};
    int ready = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);

    if (ready > 0) {
      if (FD_ISSET(this->tcp_listen_fd_, &read_fds)) this->tcp_accept_client_();

      for (size_t i = 0; i < TCP_MAX_CLIENTS; i++) {
        if (this->tcp_client_fds_[i] < 0) continue;
        if (FD_ISSET(this->tcp_client_fds_[i], &read_fds)) this->tcp_handle_client_readable_(i);
      }
    }

    esp_task_wdt_reset();
  }
}

void DivoomGatewayComponent::tcp_accept_client_() {
  int fd = accept(this->tcp_listen_fd_, nullptr, nullptr);
  if (fd < 0) return;

  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  int index = -1;
  for (size_t i = 0; i < TCP_MAX_CLIENTS; i++) {
    if (this->tcp_client_fds_[i] >= 0) continue;
    index = i;
    break;
  }

  if (index < 0) {
    // all slots full: evict the oldest connection to make room, matching
    // the standalone firmware's fallback behavior
    this->tcp_close_client_(0);
    index = 0;
  }

  this->tcp_client_fds_[index] = fd;
}

void DivoomGatewayComponent::tcp_close_client_(size_t index) {
  if (this->tcp_client_fds_[index] < 0) return;
  close(this->tcp_client_fds_[index]);
  this->tcp_client_fds_[index] = -1;
}

void DivoomGatewayComponent::tcp_handle_client_readable_(size_t index) {
  auto *packet = static_cast<DataPacket *>(malloc(sizeof(DataPacket)));
  if (packet == nullptr) {
    ESP_LOGE(TAG, "out of memory handling TCP data, restarting");
    ESP.restart();
    return;
  }

  ssize_t received = recv(this->tcp_client_fds_[index], packet->data, sizeof(packet->data), 0);
  if (received <= 0) {
    free(packet);
    if (received == 0 || (errno != EWOULDBLOCK && errno != EAGAIN)) this->tcp_close_client_(index);
    return;
  }

  packet->size = static_cast<size_t>(received);
  if (xQueueSend(this->tcp_parse_queue_, &packet, pdMS_TO_TICKS(25)) != pdPASS) {
    free(packet);
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
    int fd = this->tcp_client_fds_[i];
    if (fd < 0) continue;

    size_t sent = 0;
    int retries = 0;
    // best-effort with a bounded retry: a slow/stalled client shouldn't be
    // able to block the whole gateway (this is called straight from the
    // Bluetooth SPP receive callback), but a handful of short retries covers
    // the common case of a momentarily full send buffer
    while (sent < size && retries < 20) {
      ssize_t result = send(fd, buffer + sent, size - sent, 0);
      if (result > 0) {
        sent += static_cast<size_t>(result);
        continue;
      }
      if (result < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
        retries++;
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
      this->tcp_close_client_(i);
      break;
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
