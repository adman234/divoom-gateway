#pragma once

#include <ESPmDNS.h>
#include <BluetoothSerial.h>
#include <AsyncTCP.h>
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
// worth of payload, since that's the largest single AsyncTCP onData() call
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
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

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

  // --- TCP passthrough ---
  void start_tcp_server_();
  void on_tcp_client_(AsyncClient *client);
  void tcp_write_(const uint8_t *buffer, size_t size);
  void tcp_parse_(const uint8_t *buffer, size_t size);
  static void tcp_client_connect_trampoline_(void *arg, AsyncClient *client);
  static void tcp_client_data_trampoline_(void *arg, AsyncClient *client, void *data, size_t size);
  static void tcp_client_disconnect_trampoline_(void *arg, AsyncClient *client);
  static void tcp_client_error_trampoline_(void *arg, AsyncClient *client, int8_t error);
  static void tcp_client_timeout_trampoline_(void *arg, AsyncClient *client, uint32_t time);
  static void tcp_parse_task_trampoline_(void *arg);
  void tcp_clear_clients_();

  // relays a byte buffer to every connected TCP client (data coming back
  // from the Bluetooth device)
  void backward_(const uint8_t *buffer, size_t size);
  // publishes/refreshes the discovered-device zeroconf TXT records that
  // custom_components/divoom's config_flow listens for
  void advertise_(const uint8_t address[6], const std::string &name, bool supported);

  uint16_t tcp_port_{7777};
  bool bluetooth_filter_{true};
  std::string pin_{};

  BluetoothSerial serial_bt_;
  bool bt_connected_{false};
  bool bt_connecting_{false};
  uint32_t bt_discover_timer_{0};
  TaskHandle_t bt_task_handle_{nullptr};
  std::vector<DiscoveredDevice> discovered_;

  AsyncServer *tcp_server_{nullptr};
  AsyncClient *tcp_clients_[TCP_MAX_CLIENTS]{};
  QueueHandle_t tcp_parse_queue_{nullptr};
  TaskHandle_t tcp_parse_task_handle_{nullptr};

  static DivoomGatewayComponent *instance_;
};

}  // namespace divoom_gateway
}  // namespace esphome
