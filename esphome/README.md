# Divoom Gateway - ESPHome external component (experimental)

This is an alternative to the standalone firmware in [`firmware/`](../firmware) built as an
[ESPHome external component](https://esphome.io/components/external_components.html) instead
of a bespoke PlatformIO/Arduino project. It replaces WiFi management, OTA, logging, the web UI
and WiFi provisioning with ESPHome's own components, and keeps only the part ESPHome can't do
for you: talking Bluetooth Classic SPP to the Divoom device and relaying it over TCP.

**Status: experimental, not yet flashed/tested on real hardware.** The component compiles
against ESPHome's schema, but the Bluetooth Classic / WiFi radio coexistence behavior (the
class of bug that led to the `WiFi.setSleep(false)` boot-loop fix in `firmware/`) can only be
found by testing on a real board. Expect to flash it, see what breaks, and iterate.

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

- An original dual-core **ESP32** (not S2/S3/C3/C6 - those don't have Bluetooth Classic)
- `esp32: framework: type: arduino` - `BluetoothSerial` is Arduino-only, not available under
  esp-idf

## Building and flashing

```bash
pip install esphome
cp secrets.yaml.example secrets.yaml   # then fill in real values
esphome run divoom-gateway.yaml
```

First flash needs a USB cable. After that, `ota:` is enabled, so subsequent
`esphome run divoom-gateway.yaml` / `esphome upload` calls can go over WiFi. Initial WiFi
provisioning (if you don't want to hardcode credentials in `secrets.yaml` on first boot) works
the same way as ESP Web Tools did with the standalone firmware, via `improv_serial:`.

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
  plain heap `malloc` is used, since the base `esp32dev` board this targets has no PSRAM.
- Verification here (no network access to install `esphome` in this sandbox) has been limited
  to Python/YAML syntax checks, a manual declared-vs-defined method cross-check, and
  brace-balance checks - not a real `esphome compile`. Real builds so far have caught three
  things those checks couldn't (see Troubleshooting below) - all fixed, but this is still
  unflashed, so treat it as still likely to have another rough edge or two.

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
  bt(...)"**: unused built-in ESP-IDF components are now excluded by default too, and nothing
  else in a typical config pulls in the Bluetooth Classic (`bt`) component. Fixed by calling
  `esphome.components.esp32.include_builtin_idf_component("bt")` in `to_code()` (wrapped in a
  try/except `ImportError` since this function doesn't exist on older ESPHome versions that
  don't need it).
- **`fatal error: IPv6Address.h` from `AsyncTCP-esphome`**: this is what led to dropping AsyncTCP
  entirely in favor of plain BSD sockets (see Known deviations above) - pull latest if you're on
  an older copy of this branch.
- **Build cache serving a stale copy of this branch**: ESPHome's git-sourced
  `external_components:` default to a 1-day refresh. `divoom-gateway.yaml` already sets
  `refresh: 0s` to always re-fetch; if a fix still doesn't seem to take effect, clean build files
  (ESPHome dashboard's three-dot menu, or delete the `.esphome` folder) before rebuilding.
