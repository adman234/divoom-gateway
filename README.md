# Divoom Gateway

**Control Divoom pixel-art devices (Pixoo, Ditoo, Timebox, Tivoo, …) from Home Assistant — as real devices with entities and actions, over Bluetooth or through a web-flashable ESP32 gateway.**

This repo combines and extends two projects by [@d03n3rfr1tz3](https://github.com/d03n3rfr1tz3):
[hass-divoom](https://github.com/d03n3rfr1tz3/hass-divoom) (the Home Assistant integration) and
[esp32-divoom](https://github.com/d03n3rfr1tz3/esp32-divoom) (the ESP32 Bluetooth-Classic proxy firmware).

| Part | Where | What's new here |
| --- | --- | --- |
| **HA integration** | [`custom_components/divoom`](custom_components/divoom) | Full device with entities (light, channel select, brightness/volume sliders, buttons) + 23 device-targeted actions. Notify service still works. |
| **ESP32 firmware** | [`firmware/`](firmware) | Flash from your browser, WiFi setup via Improv (no code editing), on-device web UI for configuration, OTA updates, captive portal fallback. |
| **Web flasher** | [`flasher/`](flasher) | ESP Web Tools installer page, deployed to GitHub Pages by CI. |

## Why an ESP32 gateway?

Divoom devices use **Bluetooth Classic (SPP)** — not BLE. Home Assistant's ESPHome Bluetooth proxies
are BLE-only and **cannot** talk to Divoom devices, and many HA servers don't have (working) Bluetooth
Classic at all. The gateway firmware turns a ~$5 ESP32 dev board into a WiFi→Bluetooth Classic bridge that
the integration talks to over TCP. Note: only the **original ESP32** chip has Bluetooth Classic — the
S2/S3/C3 variants don't work.

If your Home Assistant host has working Bluetooth Classic, you can skip the gateway entirely and connect
directly.

## Quick start

### 1. Flash the gateway (skip if using direct Bluetooth)

1. Open the **[web installer](https://REPO_OWNER.github.io/REPO_NAME/)** in Chrome or Edge
2. Plug your ESP32 in via USB and click **Install**
3. Enter your WiFi credentials when prompted (Improv)
4. Done — the gateway announces itself on your network via mDNS/zeroconf

The gateway's own web page (`http://divoom-gateway.local`) shows live status (WiFi, Bluetooth, MQTT, heap)
and lets you change WiFi, hostname, MQTT and Bluetooth settings, install firmware updates over the air,
restart, or factory-reset. If it can't reach your WiFi it opens an access point
(`Divoom-Gateway` / password `divoom1234`) with a captive setup portal.

<details>
<summary>Building/flashing manually with PlatformIO instead</summary>

The firmware is a PlatformIO project in [`firmware/`](firmware). Open the folder in VS Code with the
PlatformIO extension and hit Upload, or:

```
cd firmware
pio run -e esp32dev -t upload
```

Runtime configuration lives in NVS (set via web UI/Improv), so you no longer need a `config_local.h` —
but it still works for pre-seeding defaults.
</details>

### 2. Install the Home Assistant integration

[![Open your Home Assistant instance and open a repository inside the Home Assistant Community Store.](https://my.home-assistant.io/badges/hacs_repository.svg)](https://my.home-assistant.io/redirect/hacs_repository/?owner=REPO_OWNER&repository=REPO_NAME&category=integration)

Install via HACS (or copy `custom_components/divoom` into your config folder), restart Home Assistant,
then add the **Divoom** integration. If the gateway already discovered your Divoom device, it shows up
automatically via zeroconf — otherwise add it manually and pick your device from the scanned list.

Your Divoom device then appears as a **device** with:

| Entity | Description |
| --- | --- |
| Light | The light channel — on/off, brightness, RGB color |
| Channel (select) | Switch between clock / light / effects / visualization / design / lyrics |
| Brightness (number) | Display brightness 0–100% |
| Volume (number) | Speaker volume (audio devices: Ditoo, Tivoo, Timoo, Timebox) |
| Sync time (button) | Set the device clock to current time |
| Keyboard buttons | Toggle / next / previous keyboard LED effect (Ditoo) |

States are *optimistic*: the Divoom protocol is mostly write-only, so Home Assistant assumes commands
succeeded.

### 3. Use actions in automations

All device functions are exposed as actions (`Developer tools → Actions → search "Divoom"`), targeted
at the device — with proper UI fields:

`show_image`, `show_text`, `show_clock`, `show_effects`, `show_visualization`, `show_design`,
`show_lyrics`, `show_scoreboard`, `show_timer`, `show_countdown`, `show_noise`, `show_radio`,
`show_sleep`, `show_equalizer`, `play_game`, `game_control`, `set_keyboard`, `set_playstate`,
`set_datetime`, `set_weather`, `set_alarm`, `set_memorial`, `send_raw`

```yaml
# Example: show a GIF when the doorbell rings
action: divoom.show_image
target:
  device_id: abc123...
data:
  file: doorbell.gif
```

```yaml
# Example: scoreboard
action: divoom.show_scoreboard
target:
  device_id: abc123...
data:
  player1: 2
  player2: 1
```

GIFs go into a `pixelart/` folder in your HA config directory and must exactly match your screen size
(16×16 for Pixoo/Ditoo, 32×32 for Pixoo Max), non-interlaced, with a global color palette.
See [Troubleshooting](#troubleshooting).

<details>
<summary>Legacy notify service (still supported)</summary>

The classic `notify.divoom_*` service keeps working exactly as documented in the
[original README](https://github.com/d03n3rfr1tz3/hass-divoom#usage), including `configuration.yaml`
setups. Config-entry devices share one connection between the notify service and the entities.
</details>

## Supported devices

`aurabox`, `backpack`, `ditoo`, `ditoomic`, `pixoo`, `pixoomax`, `timebox`, `timeboxmini`, `timoo`, `tivoo`

Bluetooth port is usually `1`; audio devices (Ditoo, Timoo, Tivoo) often use `2`, Timebox Mini uses `4`.
The config flow pre-fills the right one based on the device name.

## Gateway protocols

Besides TCP (port 7777, used by the integration), the gateway also accepts commands via **Serial** and
**MQTT** — useful standalone, without Home Assistant. See the
[firmware README](firmware/README.md) for the `CONNECT` / `SEND` / `MODE …` command reference
(brightness, clock, light, effects, games, alarms, radio, and more).

## Troubleshooting

**Cannot connect** — Make sure your phone's Divoom app isn't currently connected (most devices allow only
one connection). If it connects but drops on the first command, you picked the wrong port — try `2`
(audio devices) or `4` (Timebox Mini).

**GIF doesn't display** — The image must exactly match the screen size (16×16/32×32), be non-interlaced
and use a global color palette. GIMP: export with the animation box checked and interlace unchecked, resize
with no interpolation. Details in [this comment](https://github.com/d03n3rfr1tz3/hass-divoom/issues/19#issuecomment-1982059358).

**Web installer can't find the port** — Use a USB *data* cable, install the CP210x/CH340 driver for your
board, and use Chrome or Edge (Web Serial isn't available in Firefox/Safari).

## Credits

All protocol reverse-engineering credit goes to [@d03n3rfr1tz3](https://github.com/d03n3rfr1tz3) and the
projects referenced in the [original credits](https://github.com/d03n3rfr1tz3/hass-divoom#credits):
[node-divoom-timebox-evo](https://github.com/RomRider/node-divoom-timebox-evo/),
[fhem-Divoom](https://github.com/mumpitzstuff/fhem-Divoom),
[timebox](https://github.com/ScR4tCh/timebox/), and the
[Divoom protocol docs](https://docin.divoom-gz.com/web/#/5/146).
