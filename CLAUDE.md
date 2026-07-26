# Esp32FingerprintScanner

An ESP32-based fingerprint door/access scanner. It boots into one of two modes:

- **Setup-Mode**: the device runs its own WiFi access point and serves an embedded webpage (HTML/CSS/JS baked into the firmware binary) for configuration, user management, and fingerprint enrollment. Entered when WiFi/MQTT credentials aren't configured yet, or when explicitly requested (SWS-MOD002).
- **Normal-Mode**: the device joins the configured WiFi network and MQTT broker, then waits for fingerprints to scan, publishing match results over MQTT.

The authoritative source of truth for intended behavior is `doc/specification.md` (numbered `SWS-XXX` requirements) and `doc/roadmap.md` (milestone tracking) — read those before making assumptions about what a module should do.

## Spec file convention

In `doc/specification.md`, `[X]` means a requirement is **implemented and verified on real hardware**, not just decided or coded. `[ ]` means it's specified but not yet built/verified. Don't flip a checkbox to `[X]` just because code exists for it — the checkbox tracks verified behavior, and much of this project's history has been in-code assumptions that turned out wrong until tested on actual hardware.

## Module map

- `src/nvs/` — typed NVS accessors (`nvs_app.h`/`.c`) for all persisted config: WiFi/MQTT credentials, per-function-code MQTT (topic, message) pairs, users, general settings, and a 10-slot error FIFO.
- `src/wifi/` — WiFi state machine (AP or STA, decided by the caller, not by this module).
- `src/mqtt/` — MQTT client task (currently a skeleton; see `.claude/NORMAL_MODE_IMPLEMENTATION.md` if that's the active task).
- `src/fps/` — application-layer wrapper around the fingerprint sensor. Wraps `sub/finger_print_module` (a git submodule), which itself wraps the R503 sensor's UART protocol. Owns the sensor mutex, LED state, and bounded-poll helpers used by both Setup-Mode (HTTP-driven enrollment/scan) and Normal-Mode (the scan loop).
- `src/io/` — GPIO, most notably the fingerprint sensor's touch/sense pin (`io_WaitFingerPresent()`). See "Hardware notes" below before touching this.
- `src/webpage/` — Setup-Mode's HTTP server (`esp_http_server` + cJSON) and its embedded frontend (`assets/index.html`, `app.js`, `style.css`), built into the firmware via `EMBED_TXTFILES`.
- `src/mode/` — boot-mode decision logic (Setup vs Normal).
- `src/ceh/` — Central Error Handler: fatal-error LED blink codes + reboot, and (per the in-progress Normal-Mode spec) NVS error logging.
- `src/wdt/` — watchdog (currently a no-op stub; see the Normal-Mode spec for its intended design).
- `src/timer/` — one-shot timer helper used throughout for delayed reboots/LED restores.
- `src/app.c` — boot sequence and task creation.

## Conventions

- Functions return `RC_t` (`RC_SUCCESS`, `RC_ERROR`, `RC_TIMEOUT`, `RC_NO_MATCH`, etc. — see `basictypes.h`) rather than raw booleans or errno-style codes.
- Parameters are prefixed `i_` (input) / `o_` (output) by convention.
- The fingerprint sensor's LED (`fpm_led_state_t`, set via `fps_SetLed()`) is the device's *only* user-facing status indicator — no display, no other output. Expect heavy use of it for feedback in both modes. `fps_SetLed()` is idempotent (caches the last-set state) specifically so callers can call it unconditionally on every loop iteration/request without restarting the sensor's own LED animation.
- Module lifecycle is typically `_Init()` once at boot, then either a dedicated FreeRTOS task, or (for Setup-Mode's HTTP-driven flows) small bounded-poll helper functions called synchronously from request handlers.

## Hardware notes — read before touching `io.c` or fingerprint scan timing

The fingerprint sensor's touch/sense line went through extensive real-hardware debugging (oscilloscope-verified, cross-checked against the sensor vendor's own PC test tool). Key conclusions, already reflected in code comments in `io.c`:

- The sense pin is **active LOW**, wired directly to the sensor's own touch output (no buffer stage). An external transistor buffer was tried at one point based on disturbances observed while the pin was actively polled, but those were later traced to a since-removed busy-wait polling loop and a floating-pin artifact specific to the wire being disconnected for testing — not to the direct connection itself — so the buffer was removed once the real causes were fixed.
- `io_WaitFingerPresent()` requires a genuine **edge** (confirmed "not present" baseline, then a fresh transition to "present"), not just a level read. A scan/capture operation can leave the pin reading "present" for a while afterward even with no finger there, so a level check alone can't tell a stale reading from a real touch.
- There is **no** "wait for lift" primitive, and there shouldn't be one. This was tried multiple times and reverted: the sensor was proven (independently, via the vendor's own PC tool) to be unable to reliably report a lift after a scan — it can only detect a fresh touch. Anything needing "the user lifted and placed again" relies on `io_WaitFingerPresent()`'s edge requirement for the *next* touch, not on detecting the lift itself.
- Don't re-diagnose sense-pin electrical behavior from first principles — read the comments in `io.c` first. Getting here took a long back-and-forth of wrong theories (assumed sensor firmware quirks, WiFi interference, CPU busy-wait noise, floating-pin artifacts) before the real causes (direct GPIO wiring, and separately, an MQTT/UART read timeout too short for a 1:N search response) were found.

## Workflow notes

- The user builds and flashes the firmware themselves and pastes back serial output/errors — don't invoke `idf.py build`/`flash`/`monitor` directly.
- ESP-IDF v6.0.1, single-app partition table, 2MB flash, no SPIFFS.
