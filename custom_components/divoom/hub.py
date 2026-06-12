"""Shared connection hub for a Divoom device."""
import logging
import threading

from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError

_LOGGER = logging.getLogger(__package__)


def create_device(device_type, host, mac, port, escape_payload, logger=None):
    """Create the protocol implementation for the given device type."""
    if logger is None:
        logger = _LOGGER

    if device_type == 'aurabox':
        from .devices.aurabox import Aurabox
        return Aurabox(host=host, mac=mac, port=port, escapePayload=escape_payload, logger=logger)
    if device_type == 'backpack':
        from .devices.backpack import Backpack
        return Backpack(host=host, mac=mac, port=port, escapePayload=escape_payload, logger=logger)
    if device_type == 'ditoo':
        from .devices.ditoo import Ditoo
        return Ditoo(host=host, mac=mac, port=port, escapePayload=escape_payload, logger=logger)
    if device_type == 'ditoomic':
        from .devices.ditoomic import DitooMic
        return DitooMic(host=host, mac=mac, port=port, escapePayload=escape_payload, logger=logger)
    if device_type == 'pixoo':
        from .devices.pixoo import Pixoo
        return Pixoo(host=host, mac=mac, port=port, escapePayload=escape_payload, logger=logger)
    if device_type == 'pixoomax':
        from .devices.pixoomax import PixooMax
        return PixooMax(host=host, mac=mac, port=port, escapePayload=escape_payload, logger=logger)
    if device_type == 'timebox':
        from .devices.timebox import Timebox
        return Timebox(host=host, mac=mac, port=port, escapePayload=escape_payload, logger=logger)
    if device_type == 'timeboxmini':
        from .devices.timeboxmini import TimeboxMini
        return TimeboxMini(host=host, mac=mac, port=port, escapePayload=escape_payload, logger=logger)
    if device_type == 'timoo':
        from .devices.timoo import Timoo
        return Timoo(host=host, mac=mac, port=port, escapePayload=escape_payload, logger=logger)
    if device_type == 'tivoo':
        from .devices.tivoo import Tivoo
        return Tivoo(host=host, mac=mac, port=port, escapePayload=escape_payload, logger=logger)

    return None


class DivoomHub:
    """Owns the (single) connection to a Divoom device and serializes access to it.

    The underlying protocol classes use blocking sockets, so every call goes
    through the executor and is guarded by a lock. Entities, services and the
    legacy notify platform all share the same hub, so there is never more than
    one Bluetooth/TCP connection per device.
    """

    def __init__(self, hass: HomeAssistant, device_type, host, mac, port, escape_payload,
                 name=None, media_directory=None, font_directory=None):
        self.hass = hass
        self.name = name
        self.mac = mac
        self.host = host
        self.device_type = device_type
        self.media_directory = media_directory
        self.font_directory = font_directory
        self.device = create_device(device_type, host, mac, port, escape_payload)
        self._lock = threading.RLock()

    def execute(self, command, *args, **kwargs):
        """Run a device command while holding the connection lock. Blocking."""
        with self._lock:
            try:
                return self._execute(command, *args, **kwargs)
            except OSError as error:
                # the connection may be stale (e.g. the gateway restarted):
                # tear it down and retry once before giving up
                _LOGGER.warning("Divoom: connection to %s failed (%s), reconnecting", self.mac, error)
                try:
                    self.device.disconnect()
                    return self._execute(command, *args, **kwargs)
                except OSError as retry_error:
                    target = self.host or self.mac
                    raise HomeAssistantError(
                        f"Could not reach the Divoom device via {target}: {retry_error}"
                    ) from retry_error

    def _execute(self, command, *args, **kwargs):
        skip_ping = command in ("send_gamecontrol", "send_raw", "send_command")
        self.device.reconnect(skipPing=skip_ping)
        return getattr(self.device, command)(*args, **kwargs)

    async def async_execute(self, command, *args, **kwargs):
        """Run a device command from the event loop."""
        return await self.hass.async_add_executor_job(
            lambda: self.execute(command, *args, **kwargs)
        )

    def connect(self):
        with self._lock:
            self.device.connect()

    def disconnect(self):
        with self._lock:
            self.device.disconnect()
