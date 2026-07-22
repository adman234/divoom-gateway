import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE

try:
    # only present on ESPHome versions that exclude unused built-in ESP-IDF
    # components by default (2026.2.0+); older versions don't need this call
    from esphome.components.esp32 import include_builtin_idf_component
except ImportError:
    include_builtin_idf_component = None

CODEOWNERS = ["@adman234"]
DEPENDENCIES = ["wifi", "mdns"]

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
    # - the TCP relay is plain BSD/lwIP sockets, see divoom_gateway.cpp.)
    cg.add_library("WiFi", None)
    cg.add_library("ESPmDNS", None)
    cg.add_library("BluetoothSerial", None)

    # same 2026.2.0 change also excludes unused built-in ESP-IDF components
    # by default; Bluetooth Classic (esp_spp_api.h) lives in the "bt" IDF
    # component, which nothing else pulls in unless something asks for it
    if include_builtin_idf_component is not None:
        include_builtin_idf_component("bt")
