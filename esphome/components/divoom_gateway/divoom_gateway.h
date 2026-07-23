#pragma once

#include <ESPmDNS.h>
#include <BluetoothSerial.h>
#include <esp_spp_api.h>

#include <string>
#include <vector>

#include "esphome/core/component.h"

namespace esphome {
namespace divoom_gateway {

static const size_t TCP_MAX_CLIENTS = 3;
static const size_t BT_DISCOVERED_MAX = 8;

struct DiscoveredDevice {
  std::string mac;
  std::string name;
};

// sized like the upstream firmware's data_packet_t (util.h): one TCP MSS
// worth of payload, since that's the largest single recv() call we do
struct DataPacket {
  uint8_t data[CONFIG_LWIP_TCP_MSS];
  size_t size;
};

// Ports the raw Bluetooth-Classic <-> TCP relay from the standalone
// divoom-gateway firmware (hardware/bluetoothctl.cpp + output/bluetooth.cpp +
// input/tcp.cpp) into a single ESPHome external component. Wire protocol on
// the TCP side is unchanged, so the existing Home Assistant `divoom` custom
// integration talks to this exactly like it does to the standalone firmware:
// 0x69 + 6-byte MAC + port = connect, 0x96 + 6-byte MAC = disconnect,
// 0x01 ... 0x02 = raw Divoom payload to relay to/from the Bluetooth device.
class DivoomGatewayComponent : public Component {
 public:
  DivoomGatewayComponent();

  void setup() override;
  void loop() override;
  void dump_config() override;
  // controller_status/bluedroid_status were both observed stuck at 0 (never
  // initialized) despite begin() reporting success - the standalone
  // firmware's own main.cpp initializes Bluetooth *before* WiFi
  // (BluetoothHandler::setup() then WifiHandler::setup()), but this
  // component was running at AFTER_WIFI, the opposite order. ESP32's WiFi
  // and Bluetooth Classic share one radio, and BT controller init failing
  // silently when it runs after WiFi's already up is consistent with what
  // was observed. setup_priority::BLUETOOTH runs before WIFI, matching the
  // original ordering.
  float get_setup_priority() const override { return setup_priority::BLUETOOTH; }

  void set_tcp_port(uint16_t port) { this->tcp_port_ = port; }
  void set_bluetooth_filter(bool filter) { this->bluetooth_filter_ = filter; }
  void set_pin(const std::string &pin) { this->pin_ = pin; }

  // exposed so a lambda sensor/text_sensor in YAML can report discovery
  // status, e.g. `lambda: return id(gateway).get_bluetooth_connected();`
  bool get_bluetooth_connected() const { return this->bt_connected_; }
  const std::vector<DiscoveredDevice> &get_discovered_devices() const { return this->discovered_; }

 protected:
  // --- Bluetooth Classic (SPP) ---
  void bt_task_();
  void bt_discover_(int timeout_ms);
  bool bt_connect_(const uint8_t address[6], uint16_t channel);
  bool bt_disconnect_();
  // returns bytes written, 0 if still connecting, -1 if not connected
  int bt_send_(const uint8_t *buffer, size_t size);
  void on_spp_event_(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);
  static void spp_event_trampoline_(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);
  static void bt_task_trampoline_(void *arg);

  // --- TCP passthrough (plain BSD/lwIP sockets - see README for why this
  // isn't built on AsyncTCP like the standalone firmware was) ---
  void start_tcp_server_();
  static void tcp_socket_task_trampoline_(void *arg);
  void tcp_socket_task_();
  void tcp_accept_client_();
  void tcp_close_client_(size_t index);
  void tcp_handle_client_readable_(size_t index);
  void tcp_write_(const uint8_t *buffer, size_t size);
  void tcp_parse_(const uint8_t *buffer, size_t size);
  static void tcp_parse_task_trampoline_(void *arg);

  // relays a byte buffer to every connected TCP client (data coming back
  // from the Bluetooth device)
  void backward_(const uint8_t *buffer, size_t size);
  // publishes/refreshes the discovered-device zeroconf TXT records that
  // custom_components/divoom's config_flow listens for
  void advertise_(const uint8_t address[6], const std::string &name, bool supported);

  uint16_t tcp_port_{7777};
  bool bluetooth_filter_{true};
  std::string pin_{};
  // set as the very first statement in setup(); read back in dump_config(),
  // which has proven reliably visible in every log capture so far, to settle
  // for certain whether setup() ever actually runs
  bool setup_ran_{false};

  BluetoothSerial serial_bt_;
  bool bt_connected_{false};
  bool bt_connecting_{false};
  uint32_t bt_discover_timer_{0};
  TaskHandle_t bt_task_handle_{nullptr};
  std::vector<DiscoveredDevice> discovered_;

  int tcp_listen_fd_{-1};
  int tcp_client_fds_[TCP_MAX_CLIENTS];
  TaskHandle_t tcp_socket_task_handle_{nullptr};
  QueueHandle_t tcp_parse_queue_{nullptr};
  TaskHandle_t tcp_parse_task_handle_{nullptr};

  static DivoomGatewayComponent *instance_;
};

}  // namespace divoom_gateway
}  // namespace esphome
