import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE

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

    cg.add_library("BluetoothSerial", None)
    cg.add_library("esphome/AsyncTCP-esphome", "^2.1.4")
