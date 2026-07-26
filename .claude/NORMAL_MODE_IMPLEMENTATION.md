# Implementation brief: CEH, Watchdog, Normal Mode, MQTT

This is the implementation brief for the spec added to `doc/specification.md` covering the Central Error Handler, Watchdog, Normal Mode (both the Fingerprint-Module and Modes-level sections), and the MQTT function-code publish mechanism. That spec was written in a dedicated planning pass (no code changed at the time) — this file exists so the *next* pass, which actually implements it, doesn't have to re-derive the reasoning behind each decision from the spec's bullet points alone.

Read `CLAUDE.md` (repo root) first for general project context if you haven't already.

## Scope

Four things need real implementation, currently all missing or stubbed:
1. **CEH** (`src/ceh/`) — currently shows one LED blink + fixed 3s delay + reboot, and never touches NVS. Needs a full rewrite: repeating blink code, NVS error logging (with dedup), and a non-fatal logging path.
2. **Watchdog** (`src/wdt/`) — currently a complete no-op stub (every function just returns `RC_SUCCESS`). Needs implementing from scratch as a cooperative software monitor, plus the scheduled-reboot feature, plus ESP-IDF's hardware TWDT as a fallback.
3. **Normal-Mode scan loop + boot sequence** (`src/app.c`'s `scanner_task`, WiFi/MQTT connect sequencing) — the scan loop exists today but uses a different (less reliable) detection approach than Setup-Mode ended up needing; the boot LED sequence and runtime disconnect handling don't exist yet.
4. **MQTT client + publish** (`src/mqtt/`) — currently drains `g_scan_queue` and drops every event. Needs a real client (connect, heartbeat, last-will) plus the function-code-based publish with placeholder substitution.

## Key design decisions and why

**Scan loop: idle-semaphore + active-`io_WaitFingerPresent()` hybrid (SWS-FPM202).** The existing `g_fp_sense_sem` (given by a GPIO interrupt) stays, but only as a wake-from-idle signal — it has no debounce and shouldn't be trusted as "a finger is present," which is exactly the class of bug that caused most of the pain during Setup-Mode's hardware debugging. Once woken, the task should switch to `io_WaitFingerPresent()` (io.c) for actual detection. This keeps CPU usage near zero while genuinely idle (no busy-polling for hours between scans) while still getting the reliable, edge-requiring detection everywhere it matters.

**"Awaiting finger" is skipped on the very first post-wake attempt.** The wake itself already means a finger just made contact — showing a breathing "awaiting finger" LED and then switching to a scan result a few milliseconds later would look like a visible glitch for something that, in fact, already happened. Only show "awaiting finger" *after* a scan/result, while polling for a possible next scan in the same session.

**Watchdog is dual-layered (SWS-WDT001).** Primary: a cooperative software monitor task that checks each registered task's last `wdt_Reset()` time and calls `ceh_Fatal()` itself on a stall — this gets a watchdog trip the same graceful LED-blink + NVS-log + reboot treatment as every other fatal condition. Secondary/fallback: ESP-IDF's real hardware TWDT, enabled in parallel, purely to catch the case where the software monitor task itself freezes (no graceful sequence there, just a guarantee the device doesn't hang forever).

**CEH fatal/non-fatal split, and dedup (SWS-CEH002/003).** Not every CEH-worthy condition should reboot the device — a dropped WiFi connection that's actively retrying should be logged (so it's visible later) without interrupting operation. Only escalate to `ceh_Fatal()` (blink code + reboot) once retry has genuinely failed. The NVS error FIFO is only 10 slots — without dedup, one persistent condition (e.g. "no WiFi" logged on every retry attempt) would flood it and evict real history. Rule: don't push a new entry if the same error condition is still the most recent one; only a new/different condition (or the same one recurring *after* a clear) gets a fresh entry.

**CEH_ERR → blink-code mapping (SWS-CEH005)** — there are only 5 blink-count LED states (`ERROR_1`..`ERROR_5`). Two are already used (`CEH_ERR_RESOURCE`→1, `CEH_ERR_WIFI_BOOT`→2). The three new Normal-Mode error types fit the remaining three exactly: `CEH_ERR_WIFI_RUNTIME`→3, `CEH_ERR_MQTT_RUNTIME`→4, `CEH_ERR_WATCHDOG`→5.

**MQTT publish has no fixed JSON shape (SWS-MQT04).** Earlier design sketches (see `mqtt.h`'s original doc comment, now stale) assumed a fixed JSON payload. That's superseded: the admin writes their own topic and message strings per function code (already stored in NVS per `SWS-NVS002`), with placeholders (`{uuid}`, `{finger_id}`, `{function_code}`, `{score}`, `{name}`) substituted in wherever they appear, in both the topic and the message. Plain find-and-replace is sufficient — no templating engine needed. A no-match scan publishes nothing (no function code exists to select a template from).

## Files/functions to touch

- `src/ceh/ceh.c` / `ceh.h`: rewrite `ceh_Fatal()` for the repeat-blink-then-reboot timing (2s pause, up to 30s total — see `SWS-CEH004`); add a non-fatal logging entry point (e.g. `ceh_LogError()`); add `CEH_ERR_WIFI_RUNTIME`, `CEH_ERR_MQTT_RUNTIME`, `CEH_ERR_WATCHDOG` to `ceh_err_t`; wire in `nvs_ErrorPush()` with the dedup rule above; make sure a log line (`ESP_LOGE`) always prints even on the very-early-boot path where no LED callback is registered yet.
- `src/wdt/wdt.c` / `wdt.h`: currently a total no-op — implement the cooperative monitor task, per-task last-reset tracking, the scheduled-reboot check (`nvs_GeneralGetRebootMinutes()`, already read/written by the webpage but never acted on — a plain `esp_restart()`, not routed through CEH), and enable ESP-IDF's hardware TWDT as the fallback layer.
- `src/app.c`: `scanner_task` — the user wants this moved into `fps.c` (fits `SWS-FPM001`'s existing description: "the fps wrapper... contains the scanner task"); rewrite its detection logic per `SWS-FPM202`. Also wire up the Normal-Mode boot LED sequence (`SWS-MOD201`/`202`) — WiFi connect (Diagnostic State 1) → MQTT connect (Diagnostic State 2) → "op success" → scan loop.
- `src/wifi/wifi.c`: `WIFI_EVENT_STA_DISCONNECTED` currently just calls `esp_wifi_connect()` again immediately with no escalation. Needs the 2-minute-then-fatal behavior from `SWS-MOD203` for a *runtime* disconnect (this is distinct from the existing boot-time AP-fallback timer, which stays as-is).
- `src/mqtt/mqtt.c` / `mqtt.h`: implement the actual client (connect, heartbeat per `SWS-MQT03`, last-will at connect time) and the `SWS-MQT04` publish-with-placeholders logic. `mqtt_scan_event_t` (in `mqtt.h`) will need `finger_id` and `function_code` added — `scanner_task`/its replacement already resolves `uuid`/`name`, just needs to also carry these two through.
- `src/nvs/nvs_app.h`: `nvs_ErrorPush()`/`nvs_ErrorGet()`/`nvs_ErrorGetCount()`/`nvs_ErrorClear()` already exist and are fully implemented in `nvs_app.c` — just never called from anywhere yet.
- `src/webpage/`: new "View Errors" screen under System Settings (`SWS-WP102`) — list `nvs_ErrorGetCount()`/`nvs_ErrorGet()` entries, similar in shape to the existing Fingerprint Library list screen.

## Already solved — don't re-litigate

`io_WaitFingerPresent()` in `src/io/io.c` is a hard-won, carefully-debounced, edge-requiring primitive that took a long real-hardware debugging process to get right (see `CLAUDE.md`'s Hardware Notes section for the short version). Reuse it as-is for the Normal-Mode scan loop. Do not add a "wait for lift" function, do not change the sense-pin polarity/edge/debounce constants without new hardware evidence, and do not assume the sensor's touch line behaves like a simple continuous level — it doesn't.
