import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE

try:
    from esphome.components.esp32 import add_idf_sdkconfig_option
except ImportError:
    add_idf_sdkconfig_option = None

CODEOWNERS = ["@adman234"]
DEPENDENCIES = ["ethernet", "mdns"]

divoom_gateway_ns = cg.esphome_ns.namespace("divoom_gateway")
DivoomGatewayComponent = divoom_gateway_ns.class_("DivoomGatewayComponent", cg.Component)

CONF_TCP_PORT = "tcp_port"
CONF_BLUETOOTH_FILTER = "bluetooth_filter"
CONF_PIN = "pin"


def _require_esp32_arduino(config):
    if not CORE.is_esp32:
        raise cv.Invalid(
            "divoom_gateway requires an ESP32 - Bluetooth Classic (SPP) is not "
            "available on ESP32-S2/S3/C3/C6 or other chips."
        )
    if not CORE.using_arduino:
        raise cv.Invalid(
            "divoom_gateway requires the Arduino framework (esp32: framework: type: arduino) "
            "- BluetoothSerial is not available under esp-idf."
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(DivoomGatewayComponent),
            cv.Optional(CONF_TCP_PORT, default=7777): cv.port,
            cv.Optional(CONF_BLUETOOTH_FILTER, default=True): cv.boolean,
            cv.Optional(CONF_PIN): cv.string_strict,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _require_esp32_arduino,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_tcp_port(config[CONF_TCP_PORT]))
    cg.add(var.set_bluetooth_filter(config[CONF_BLUETOOTH_FILTER]))
    if CONF_PIN in config:
        cg.add(var.set_pin(config[CONF_PIN]))

    # ESPHome 2026.2+ disables Arduino libraries by default on ESP32 Arduino
    # builds (arduino-as-an-esp-idf-component); anything we #include that
    # lives under arduino-esp32/libraries/* has to be re-enabled explicitly,
    # or its headers aren't on the include path. (No AsyncTCP dependency here
    # - the TCP relay is plain BSD/lwIP sockets, see divoom_gateway.cpp. No
    # Arduino WiFi library either - network status is queried through
    # ESPHome's own ethernet: component directly, not an Arduino network class.)
    cg.add_library("ESPmDNS", None)
    cg.add_library("BluetoothSerial", None)

    # The "bt" IDF component itself isn't excluded by ESPHome by default (it's
    # not in DEFAULT_EXCLUDED_IDF_COMPONENTS), but its contents - including
    # esp_spp_api.h - are compiled in only when Bluetooth is actually enabled
    # via Kconfig. ESPHome only sets CONFIG_BT_ENABLED when a BLE component
    # (esp32_ble, bluetooth_proxy, etc.) asks for it; nothing does that here
    # since we talk to BluetoothSerial directly, so set the sdkconfig options
    # arduino-esp32's BluetoothSerial itself requires (see the #error guards
    # in the standalone firmware's hardware/bluetoothctl.h for the same list).
    if add_idf_sdkconfig_option is not None:
        add_idf_sdkconfig_option("CONFIG_BT_ENABLED", True)
        add_idf_sdkconfig_option("CONFIG_BT_BLUEDROID_ENABLED", True)
        add_idf_sdkconfig_option("CONFIG_BLUEDROID_ENABLED", True)
        add_idf_sdkconfig_option("CONFIG_BT_CLASSIC_ENABLED", True)
        add_idf_sdkconfig_option("CONFIG_BT_SPP_ENABLED", True)

        # Separate, lower-level Kconfig layer: the BT *controller's* own mode
        # selection (as opposed to the Bluedroid *host*-level flags above).
        # This is a Kconfig "choice" - its default is BLE_ONLY, which is
        # incompatible with requesting ESP_BT_MODE_CLASSIC_BT at runtime and
        # was confirmed (via a diagnostic build) to make
        # esp_bt_controller_enable() fail with ESP_ERR_INVALID_ARG. Select
        # BR/EDR (Classic) only, matching disableBLE=true in begin().
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_MODE_BLE_ONLY", False)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_MODE_BTDM", False)
