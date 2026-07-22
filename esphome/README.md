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
  `input/tcp.cpp`)
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

- Fixed an inverted condition in the "TCP client slot full" fallback (`clear()` in
  `input/tcp.cpp` looped over `nullptr` slots and skipped real ones - it now correctly evicts
  existing clients when all `TCP_MAX_CLIENTS` slots are in use).
- Fixed a stack buffer overflow: the original's Bluetooth receive loop passed the SPP driver's
  reported `available` byte count straight to `readBytes()` into a fixed 64-byte stack buffer
  with no clamp. The port clamps to the buffer size. Same fix applied to the TCP-receive path
  (clamped to the packet struct's buffer size before the `memcpy`).
- No PSRAM-aware allocation for TCP receive buffers (`MALLOC`/`ps_malloc` in the original) -
  plain heap `malloc` is used, since the base `esp32dev` board this targets has no PSRAM.
- Not yet validated with `esphome compile` - this sandbox has no network access to install the
  `esphome` PyPI package, so verification so far is: Python/YAML syntax checks, manual
  declared-vs-defined method cross-check, and brace-balance checks. Run `esphome compile
  divoom-gateway.yaml` yourself before flashing to catch anything a real ESPHome/PlatformIO
  toolchain would catch that I couldn't (ESPHome internal API names can drift between
  versions - `WiFi.getMode()`/`App.get_name()`/etc. are the most likely spots to need a
  small fixup).
