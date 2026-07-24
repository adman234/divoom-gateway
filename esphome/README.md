# Divoom Gateway - ESPHome external component

This is an alternative to the standalone firmware in [`firmware/`](../firmware) built as an
[ESPHome external component](https://esphome.io/components/external_components.html) instead
of a bespoke PlatformIO/Arduino project. It replaces network management, OTA, logging, the web
UI and (on the original WiFi target) WiFi provisioning with ESPHome's own components, and keeps
only the part ESPHome can't do for you: talking Bluetooth Classic SPP to the Divoom device and
relaying it over TCP.

**Status: confirmed working on real hardware** (Bluetooth Classic discovery/connect, verified
against a Divoom Ditoo) **on the original WiFi-based `esp32dev` target.** This branch has since
been retargeted to Ethernet+PoE hardware (see below) to eliminate WiFi/Bluetooth radio
coexistence entirely - that retarget compiles but is not yet flash-tested on the actual Ethernet
board.

## Why Ethernet instead of WiFi

The WiFi build worked, but WiFi and Bluetooth Classic share one radio on the ESP32, and getting
them to coexist took real tuning: a Bluetooth discovery scan running too often/too long
noticeably increased the odds of TCP connections to the gateway (including the ESPHome API
itself) intermittently failing to establish. That's fixable on WiFi (scan-interval tuning got it
to a reasonably low duty cycle), but a wired board sidesteps the problem entirely - Ethernet
doesn't touch the radio Bluetooth Classic uses at all. This branch now targets an
**Olimex ESP32-POE / ESP32-POE-ISO** (Ethernet + PoE, built around the same original ESP32 chip
this whole project depends on for Bluetooth Classic - see Requirements). The Bluetooth-discovery
scan interval was reverted back to the standalone firmware's original 15s cadence accordingly,
since there's no coexistence cost to worry about anymore.

Boards that only add Ethernet over SPI (e.g. a XIAO ESP32-S3 + W5500 adapter) are **not** an
alternative here, however tempting for their small size/cost: the S3 (like S2/C3/C6) has no
Bluetooth Classic radio at all, only BLE - a hardware limitation no firmware change can work
around.

## What this does and doesn't include

Ported from `firmware/src/`, wire-compatible with `custom_components/divoom` as-is - **no
changes needed on the Home Assistant side**, just point the integration's host/port at this
device instead of the standalone firmware:

- Bluetooth Classic (SPP) discovery and connect/disconnect (from `hardware/bluetoothctl.cpp`)
- Raw TCP relay on port 7777, including the animation-frame packet splitting (from
  `input/tcp.cpp`) - implemented on plain BSD/lwIP sockets rather than AsyncTCP, see Known
  deviations below
- Zeroconf (`_divoom_esp32._tcp`) TXT record publishing for discovered devices, so Home
  Assistant's `divoom` integration can still auto-discover this gateway

**Not included** (out of scope for this first pass - the existing Python integration doesn't
need them):

- The MQTT/Serial `CONNECT`/`DISCONNECT`/`MODE`/`SEND` text-command layer and the on-device
  Divoom protocol reimplementation (`divoom/divoom.cpp`, `input/mqtt.cpp`, `input/serial.cpp`)
- The custom web status page/config API and OTA-upload endpoint (`hardware/webctl.cpp`) - use
  ESPHome's built-in `web_server:` dashboard instead
- Runtime-changeable WiFi/MQTT/hostname/PIN settings (`hardware/settings.cpp`) - these are now
  YAML + `secrets.yaml`; changing them means editing YAML and re-uploading (OTA, no physical
  access needed)

## Requirements

- **Olimex ESP32-POE-ISO** (default in `divoom-gateway.yaml`) or **ESP32-POE** (non-isolated;
  change `esp32: board:` to `esp32-poe`) - both use the original dual-core ESP32
  (ESP32-WROOM-32(E)/WROVER), which is required for Bluetooth Classic. Not S2/S3/C3/C6, and not
  any board whose Ethernet is bolted on over SPI (W5500) rather than built around this chip.
- Check which module variant is actually on your board: the WROVER variant of these Olimex
  boards uses **GPIO0** for the Ethernet clock pin instead of **GPIO17** - the `ethernet:` block
  in `divoom-gateway.yaml` defaults to GPIO17 (WROOM); change it if your link doesn't come up.
- `esp32: framework: type: arduino` - `BluetoothSerial` is Arduino-only, not available under
  esp-idf

## Building and flashing

```bash
pip install esphome
cp secrets.yaml.example secrets.yaml   # then fill in real values
esphome run divoom-gateway.yaml
```

First flash needs a USB cable. After that, `ota:` is enabled, so subsequent
`esphome run divoom-gateway.yaml` / `esphome upload` calls can go over the network. There's no
WiFi credential to provision on this Ethernet target - just plug in the cable and it gets an
address via DHCP (use `ethernet: manual_ip:` in the YAML if you want a static one instead).

## Known deviations from `firmware/`

- **TCP relay is plain BSD/lwIP sockets, not AsyncTCP.** The standalone firmware (and this
  component's first draft) used `AsyncServer`/`AsyncClient` from `AsyncTCP-esphome`. That hit a
  `fatal error: IPv6Address.h: No such file or directory` under current ESPHome (ESP32 Arduino
  now builds as an ESP-IDF component, and `AsyncTCP-esphome`'s expectations about which
  arduino-esp32 headers are available don't line up there anymore) - and it turns out ESPHome's
  own `web_server_base` component stopped using AsyncTCP on ESP32 for the same reason, switching
  to a native ESP-IDF socket implementation instead. This component now does the same: one
  FreeRTOS task runs a `select()` loop over the listening socket and up to `TCP_MAX_CLIENTS`
  client sockets, feeding the same packet-parsing pipeline the AsyncTCP version used. Wire
  protocol and client-eviction behavior are unchanged; writes are best-effort with a bounded
  retry (~100ms) rather than blocking indefinitely on a stalled client.
- Fixed an inverted condition in the "TCP client slot full" fallback (`clear()` in
  `input/tcp.cpp` looped over `nullptr` slots and skipped real ones - it now correctly evicts
  the oldest connection when all `TCP_MAX_CLIENTS` slots are in use).
- Fixed a stack buffer overflow: the original's Bluetooth receive loop passed the SPP driver's
  reported `available` byte count straight to `readBytes()` into a fixed 64-byte stack buffer
  with no clamp. The port clamps to the buffer size. Same fix applied to the TCP-receive path
  (clamped to the packet struct's buffer size before the `memcpy`).
- No PSRAM-aware allocation for TCP receive buffers (`MALLOC`/`ps_malloc` in the original) -
  plain heap `malloc` is used. The Olimex ESP32-POE-ISO's WROOM module has no PSRAM either
  (the WROVER variant does, but nothing here takes advantage of it).
- The WiFi target went through many real build/flash cycles to reach a working state (see
  Troubleshooting) - each of those fixes carries over here since they're all in shared code
  (Bluetooth Classic init, the TCP relay, sdkconfig). The Ethernet-specific change itself
  (dropping `wifi:`/`ap:`/`captive_portal:`/`improv_serial:` for `ethernet:`, and the
  `network_is_connected()` rename) has only been verified by static checks in this sandbox (no
  network access here to run `esphome compile`), not a real build - expect at least one round of
  Ethernet-specific fixes once it's actually flashed.

## Troubleshooting

Two ESPHome 2026.2.0 changes bit this component on the first few real build attempts - both are
now handled automatically in `__init__.py`, described here in case you're on an older copy of
this branch or hit a variant of either:

- **`fatal error: ESPmDNS.h`/`WiFi.h`/other `arduino-esp32/libraries/*` header**: ESP32 Arduino
  builds now compile Arduino as an ESP-IDF component with all Arduino libraries disabled by
  default. Fixed by `cg.add_library("Name", None)` calls in `to_code()` for `WiFi`, `ESPmDNS`,
  and `BluetoothSerial`. If you hit this on a header not covered by those three, add the
  matching `cg.add_library(...)` call yourself.
- **`fatal error: esp_spp_api.h`/`esp_bt.h`/`esp_gap_bt_api.h`: "Missing ... found in component(s)
  bt(...)"**: the `bt` IDF component isn't in ESPHome's excluded-by-default list at all (that
  was a wrong diagnosis on an earlier version of this file - `include_builtin_idf_component("bt")`
  was a no-op, since "bt" was never excluded). The real gate is the ESP-IDF Kconfig option
  `CONFIG_BT_ENABLED`: ESPHome only turns it on when a BLE component (`esp32_ble`,
  `bluetooth_proxy`, etc.) asks for it, and nothing does here since this component talks to
  `BluetoothSerial` directly. Fixed by calling `esphome.components.esp32.add_idf_sdkconfig_option`
  in `to_code()` for `CONFIG_BT_ENABLED`, `CONFIG_BT_BLUEDROID_ENABLED`, `CONFIG_BT_CLASSIC_ENABLED`,
  and `CONFIG_BT_SPP_ENABLED` (confirmed against ESP-IDF 5.5's actual `components/bt/host/
  bluedroid/Kconfig.in` dependency chain, not just inferred).
- **`fatal error: IPv6Address.h` from `AsyncTCP-esphome`**: this is what led to dropping AsyncTCP
  entirely in favor of plain BSD sockets (see Known deviations above) - pull latest if you're on
  an older copy of this branch.
- **Build cache serving a stale copy of this branch**: ESPHome's git-sourced
  `external_components:` default to a 1-day refresh. `divoom-gateway.yaml` already sets
  `refresh: 0s` to always re-fetch; if a fix still doesn't seem to take effect, clean build files
  (ESPHome dashboard's three-dot menu, or delete the `.esphome` folder) before rebuilding.
