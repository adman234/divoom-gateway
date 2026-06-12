from typing import Final
from homeassistant.const import Platform

# notify is set up via discovery (legacy platform), the rest are real entity platforms
PLATFORMS = [Platform.NOTIFY]
ENTITY_PLATFORMS = [Platform.LIGHT, Platform.SELECT, Platform.NUMBER, Platform.BUTTON]

DOMAIN: Final = "divoom"

CONF_DEVICE_TYPE: Final = 'device_type'
CONF_MEDIA_DIR: Final = 'media_directory'
CONF_MEDIA_DIR_DEFAULT: Final = "pixelart"
CONF_ESCAPE_PAYLOAD: Final = 'escape_payload'

# per device type feature flags, used to decide which entities get created
DEVICE_CAPABILITIES: Final = {
    "aurabox":     {"audio": False, "keyboard": False, "lyrics": False},
    "backpack":    {"audio": False, "keyboard": False, "lyrics": False},
    "ditoo":       {"audio": True,  "keyboard": True,  "lyrics": True},
    "ditoomic":    {"audio": True,  "keyboard": True,  "lyrics": True},
    "pixoo":       {"audio": False, "keyboard": False, "lyrics": False},
    "pixoomax":    {"audio": False, "keyboard": False, "lyrics": False},
    "timebox":     {"audio": True,  "keyboard": False, "lyrics": False},
    "timeboxmini": {"audio": True,  "keyboard": False, "lyrics": False},
    "timoo":       {"audio": True,  "keyboard": False, "lyrics": True},
    "tivoo":       {"audio": True,  "keyboard": False, "lyrics": True},
}

DEVICE_MODEL_NAMES: Final = {
    "aurabox": "Aurabox",
    "backpack": "Backpack",
    "ditoo": "Ditoo",
    "ditoomic": "Ditoo Mic",
    "pixoo": "Pixoo",
    "pixoomax": "Pixoo Max",
    "timebox": "Timebox",
    "timeboxmini": "Timebox Mini",
    "timoo": "Timoo",
    "tivoo": "Tivoo",
}

# channels selectable through the channel select entity
CHANNEL_CLOCK: Final = "clock"
CHANNEL_LIGHT: Final = "light"
CHANNEL_EFFECTS: Final = "effects"
CHANNEL_VISUALIZATION: Final = "visualization"
CHANNEL_DESIGN: Final = "design"
CHANNEL_LYRICS: Final = "lyrics"

# clock styles selectable through the clock style select entity
CLOCK_STYLES: Final = {
    "Fullscreen": 0,
    "Rainbow": 1,
    "Boxed": 2,
    "Analog square": 3,
    "Fullscreen negative": 4,
    "Analog round": 5,
    "Widescreen": 6,
}
