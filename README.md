# Esp32FingerprintScanner

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-blue)](https://github.com/espressif/esp-idf)
[![Platform](https://img.shields.io/badge/platform-ESP32-orange)](#hardware)

> **Work in progress.** This is a personal hobby project, developed and tested on my own hardware. It is functional and (per the checklist below) verified end-to-end on real hardware, but it hasn't been hardened, security-reviewed, or run for long enough to call it "production ready." Use it, fork it, learn from it — just don't bolt it onto your front door and walk away without keeping an eye on it for a while first.

An ESP32-based fingerprint door/access scanner. Enroll fingerprints through a phone-friendly web UI, then scan a finger to publish an MQTT message — wire that up to a door strike, a smart-home automation, a doorbell chime, whatever you like.

## What it does

The device boots into one of two modes:

- **Setup-Mode** — the ESP32 runs its own WiFi access point and serves a small embedded web app (no separate phone app, no cloud account) for configuring WiFi/MQTT credentials, managing users, and enrolling fingerprints. Entered automatically the first time (or whenever WiFi/MQTT aren't configured yet), or on demand — see [Re-entering Setup-Mode](#re-entering-setup-mode) below.
- **Normal-Mode** — the device joins your WiFi network and MQTT broker, then just waits. Scan a finger, and if it matches, it publishes a fully custom MQTT message you defined yourself (topic *and* payload — plain text, JSON, whatever your automation expects).

The sensor's own LED is the device's only user-facing status indicator (no display, no buzzer) — it breathes while waiting, flashes while scanning, and shows a distinct color/pattern for success, failure, and error conditions.

## Features

- **No app, no cloud.** Setup happens entirely over the device's own WiFi hotspot and a web page — nothing to install, nothing phoning home.
- **Fully custom MQTT payloads.** Each enrolled fingerprint carries a *function code* (1–31) that selects one of 31 independently configurable (topic, message) pairs. Write whatever topic and payload your automation platform expects, with placeholders filled in from the actual scan:

  | Placeholder | Meaning |
  |---|---|
  | `{uuid}` | Matched user's ID |
  | `{name}` | Matched user's display name |
  | `{finger_id}` | Which finger was scanned (0–9) |
  | `{function_code}` | The function code that selected this topic/message |
  | `{score}` | Match confidence score |

  A non-matching scan publishes nothing at all.
- **Admin fingerprint as a master key.** One designated fingerprint (enrolled during first-time setup) can always drop the device back into Setup-Mode — during normal scanning, *and* even at boot (held down while power-cycling), as a recovery path if WiFi/MQTT are misconfigured and the device would otherwise loop.
- **MQTT heartbeat & last-will**, both fully configurable, so your automation platform knows the device is alive (or isn't).
- **Self-healing connectivity.** A dropped WiFi or MQTT connection is retried automatically for up to 2 minutes before the device gives up and reboots to try again from scratch.
- **On-device error log.** The last 10 error conditions, plus a running per-error-code occurrence count, viewable from the web UI — useful when the device is somewhere you can't easily plug a USB cable into.
- **Cooperative + hardware watchdog**, scheduled reboots, and a central error handler with a consistent LED blink-code language for every fatal condition — so failures are visible, not silent.

## Hardware

- An ESP32 dev board (this project targets the plain `esp32` chip target, 2MB flash, single-app partition — see `sdkconfig`).
- An R503 optical/capacitive fingerprint sensor module (UART-based; cheap and common on the usual electronics marketplaces).
- Wiring: sensor UART on ESP32 UART2 (see `src/uart/uart.c` for the exact TX/RX pins), and the sensor's touch-detect output wired directly to a GPIO interrupt pin (see `src/io/io.c` for the exact pin and important electrical notes — **read the comments there before changing anything wiring-related**; getting the touch-sense behavior right took a lot of real-hardware debugging).
- `hardware/doorbell/` has a FreeCAD frontplate design if you want to mount this as a doorbell.

## Getting started

### Prerequisites

- [ESP-IDF v6.0.1](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html)
- An R503 fingerprint sensor wired up as described above
- Network access for the ESP-IDF Component Manager to fetch a couple of managed dependencies (cJSON, esp-mqtt) on first build

### Build & flash

```bash
git clone --recurse-submodules https://github.com/TomOdw/Esp32FingerprintScanner.git
cd Esp32FingerprintScanner
idf.py build
idf.py -p <YOUR_SERIAL_PORT> flash monitor
```

(If you already cloned without `--recurse-submodules`, run `git submodule update --init --recursive` first — the fingerprint sensor driver lives in its own repository, [`FingerPrintModule`](https://github.com/TomOdw/FingerPrintModule).)

### First boot

1. On first boot (no configuration yet), the device starts its own WiFi access point:
   **SSID:** `Esp32FingerprintScanner` · **Password:** `esp32setup`
2. Connect to it with your phone and open `http://192.168.4.1` in a browser.
3. You'll be asked to enroll the **admin fingerprint** first — this is your master key (see below), so pick a finger you'll remember.
4. Once enrolled, the device reboots into the normal Setup-Mode menu, where you configure:
   - WiFi (SSID/password)
   - MQTT (broker, credentials, heartbeat, last-will, and your 31 function-code topic/message pairs)
   - Users and their fingerprints
5. Hit **Exit** to reboot into Normal-Mode and start scanning.

### Re-entering Setup-Mode

Configured something wrong and can't reach the web UI anymore? Present the admin fingerprint:

- **During normal scanning** — the device drops straight back into Setup-Mode instead of publishing a scan result.
- **At boot** — hold the admin finger on the sensor while power-cycling the device; if WiFi/MQTT are misconfigured badly enough to loop-reboot, this breaks the cycle without needing serial/USB access.

## Project status

This project is developed spec-first: [`doc/specification.md`](doc/specification.md) is the authoritative source of truth for intended behavior (numbered `SWS-XXX` requirements), and [`doc/roadmap.md`](doc/roadmap.md) tracks milestones. In the spec, a checked box (`[X]`) means a requirement is **implemented and verified on real hardware** — not just coded up and assumed to work. Unchecked (`[ ]`) means it's specified but not yet verified that way. If you're evaluating whether a particular behavior is solid, that file is the place to look, not just the code.

## License

[MIT](LICENSE) © Tom Christ
