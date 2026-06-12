"""Select platform for switching the Divoom display channel."""
from __future__ import annotations

import logging

from homeassistant.components.select import SelectEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import (
    CHANNEL_CLOCK,
    CHANNEL_DESIGN,
    CHANNEL_EFFECTS,
    CHANNEL_LIGHT,
    CHANNEL_LYRICS,
    CHANNEL_VISUALIZATION,
    DEVICE_CAPABILITIES,
    DOMAIN,
)
from .entity import DivoomEntity
from .hub import DivoomHub

_LOGGER = logging.getLogger(__package__)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up the Divoom channel select from a config entry."""
    hub: DivoomHub = hass.data[DOMAIN]["hubs"][entry.entry_id]
    async_add_entities([DivoomChannelSelect(hub)])


class DivoomChannelSelect(DivoomEntity, SelectEntity):
    """Switches between the channels of a Divoom device. Optimistic state."""

    _attr_name = "Channel"
    _attr_assumed_state = True
    _attr_icon = "mdi:television-guide"

    def __init__(self, hub: DivoomHub) -> None:
        super().__init__(hub)
        self._attr_unique_id = f"{hub.mac}-channel"

        capabilities = DEVICE_CAPABILITIES.get(hub.device_type, {})
        options = [CHANNEL_CLOCK, CHANNEL_LIGHT, CHANNEL_EFFECTS, CHANNEL_VISUALIZATION, CHANNEL_DESIGN]
        if capabilities.get("lyrics"):
            options.append(CHANNEL_LYRICS)
        self._attr_options = options
        self._attr_current_option = None

    async def async_select_option(self, option: str) -> None:
        if option == CHANNEL_CLOCK:
            await self._hub.async_execute("show_clock")
        elif option == CHANNEL_LIGHT:
            await self._hub.async_execute("show_light", color=None, brightness=100, power=True)
        elif option == CHANNEL_EFFECTS:
            await self._hub.async_execute("show_effects", 0)
        elif option == CHANNEL_VISUALIZATION:
            await self._hub.async_execute("show_visualization", 0, None, None)
        elif option == CHANNEL_DESIGN:
            await self._hub.async_execute("show_design")
        elif option == CHANNEL_LYRICS:
            await self._hub.async_execute("show_lyrics")

        self._attr_current_option = option
        self.async_write_ha_state()
