# Software Specification for Esp32FingerprintScanner

## NVS
- [X] SWS-NVS001  The device uses ESP32s NVS. NVS is used to store setup-,
                  user- and debug-information. There are functions to read and 
                  write those informations from and to NVS.
- [X] SWS-NVS002  Setup-information structure:
                  - General
                    - Scheduled Reboot Time in minutes
                    - Start in Setup mode
                  - Wifi-Information:
                    - Credentials:
                      - SSID
                      - Password
                  - Mqtt-Information:
                    - Credentials:
                      - Broker-IP
                      - Username
                      - Password
                    - Heartbeat:
                      - Enabled
                      - Topic & message
                      - Interval in seconds
                    - Last-Will:
                      - Enabled
                      - Topic & message
                    - Function-Codes:
                      - 31 (1..31) Slots (Function-Codes/FCs) each storing a
                        topic and a message.
- [X] SWS-NVS003  User-information structure:
                  - Up to 127 UUIDs (Range 1..127) with Username.
                    UUID 0 marks empty slots.
- [X] SWS-NVS004  Error-information structure:
                  - 10 Slots of Error information (FIFO) where each slots contains
                    an error message.
- [X] SWS-NVS005  Error-code occurrence counters: alongside the error FIFO
                  (SWS-NVS004), NVS also tracks, per CEH error code
                  (ceh_err_t — see Central Error Handler), a running count
                  of how many times that code has been raised. This counts
                  every raw occurrence (every ceh_Fatal()/ceh_NonFatal()
                  call for that code), independent of the FIFO's own
                  SWS-CEH002 deduplication — a condition that stays ongoing
                  for a long time and only ever produces one FIFO entry
                  still increments its counter on every occurrence. All
                  counters are reset together with the FIFO by
                  nvs_ErrorClear() — "since last clear" applies to both.


## Wifi
- [X] SWS-WIF001  The device uses wifi. It operates in two different modes client,
                  and AP. Which mode the device operates in, is decicded at 
                  system startup.
- [X] SWS-WIF002  AP mode: In Access Point mode, a Wifi with SSID
                  "Esp32FingerprintScanner", and password "esp32setup" is setup. 
- [X] SWS-WIF003  Client mode: In Client mode, the devices connects to a passed
                  ssid and password. If the connection failed, the blocking init
                  function returns with an error.
                  NOTE: implemented as an event-driven state machine (wifi_Task +
                  WIFI_EVENT/IP_EVENT handlers + a 30s one-shot timer) rather than
                  a literal blocking call, matching ESP-IDF's async WiFi driver.
                  On timeout the device falls back to AP mode and reboots via
                  ceh_Fatal(CEH_ERR_WIFI_BOOT) after a configurable fallback
                  window to retry STA.
- [X] SWS-WIF004  There will be a system event triggered, if the connection to
                  the AP fails during device operation.


## Mqtt
- [X] SWS-MQT01   The device is an mqtt client. On init a cfg structure is passed,
                  with, a broker ip, username, password, last-will-topic and message,
                  heartbeat topic, message and interval.
                  The blocking init function returns with an error, if connection
                  to the broker failed.
- [X] SWS-MQT02   There will be a system event triggered, if the connection to
                  the brokter fails during device operation.
- [X] SWS-MQT03   The heartbeat is send automatically with the configured interval.
- [X] SWS-MQT04   Scan-result publishing: when the fingerprint module identifies
                  a match during Normal-Mode scanning (SWS-FPM202), the matched
                  fingerprint's function code selects one of the 31 NVS-configured
                  (topic, message) pairs (SWS-NVS002). Both the topic and the
                  message are published as-is except for the following
                  placeholders, substituted with the matched fingerprint's
                  actual values before publishing:
                    - {uuid}          - the matched user's UUID
                    - {finger_id}     - which finger was matched (0-9)
                    - {function_code} - the function code that selected this
                                        (topic, message) pair
                    - {score}         - the match confidence score
                    - {name}          - the matched user's display name
                                        (resolved from NVS)
                  There is no predetermined JSON structure — the admin writes
                  the exact topic and payload they want (plain text, JSON, or
                  anything else) via the webpage's MQTT Setup screen, with
                  placeholders substituted in wherever used. A scan with no
                  match produces no publish at all (there is no function code
                  to select a (topic, message) pair from).
- [X] SWS-MQT05   There is currently no need for supporting topic subscription


## Fingerprint Module
- [X] SWS-FPM001  The device uses the finger_print_module. The fps wrapper need
                  to serve two parts: it contains the scanner task, which is 
                  run when the device is in normal mode but also functions that
                  are used when in setup mode.
                  NOTE: the setup-mode-facing part is the bounded-poll
                  fps_EnrollStep()/fps_EnrollCommitAndTag()/fps_ScanStep()
                  helpers in fps.c, driven by the webpage module.
- [X] SWS-FPM002  NVS and the modules meta functions, store fingerprint
                  informations. UUIDs are mapped to usernames in NVS.
                  Fingerprint ids, are mapped to uuids in the modules
                  meta data.
- [X] SWS-FPM003  The module has an led for feedback and showing of general
                  devices states.

### Setup Mode
- [X] SWS-FPM101  In Setup mode the fingerprint module is controlled externally,
                  the scanner task is not active.
- [X] SWS-FPM102  Enrollment capture procedure (used by SWS-WP104.3/SWS-WP201,
                  each of the two scans making up one template):
                  - Before instructing the module to capture, the fp-sense
                    pin is evaluated (polled, debounced — see below) to
                    confirm a finger is actually present, rather than
                    blindly issuing the capture command and relying on it
                    to fail fast.
                  - Once both scans for a template have succeeded, the
                    module is instructed to generate and store the template
                    (and it is then tagged with uuid/finger-id/function-code
                    metadata).
                  - There is no separate "lift" phase/request. Confirmed
                    independently on real hardware (via the sensor vendor's
                    own PC test tool, not just this firmware): performing a
                    scan leaves the sensor unable to reliably report a lift
                    afterward — it can still detect a genuinely fresh touch,
                    just not the absence of one. A dedicated lift-wait
                    placed after a scan would therefore be trivially (and
                    meaninglessly) satisfied, or stuck, regardless of what
                    the user's finger is actually doing. Instead,
                    io_WaitFingerPresent() (io.c) requires a genuine edge —
                    a confirmed "not present" baseline, then a fresh
                    transition to "present" — for every capture, including
                    the very first of a session. That edge requirement is
                    what actually enforces "the user must lift and place
                    again" between scans; the web interface just needs to
                    tell them to.
                  - The fp-sense pin is active LOW, wired directly to the
                    R503's own touch output (see io.c). An external
                    transistor buffer stage was tried at one point based on
                    disturbances observed while the pin was actively
                    polled, but those were later traced to a since-removed
                    busy-wait polling loop and a floating-pin artifact
                    specific to the wire being disconnected for testing —
                    not to the direct connection itself — so the buffer was
                    removed once the real causes were fixed.
                  - LED feedback (see SWS-FPM003): "awaiting finger"
                    (breathing blue) while polling for a finger to place.
                    Once presence is confirmed, the LED switches to
                    "scanning" (fast flash) for the actual capture attempt,
                    held for at least SCANNING_DISPLAY_MIN_MS (webpage.c)
                    even if the real capture finishes faster, so there is a
                    clear, visible confirmation that a scan happened rather
                    than an instant jump from "awaiting finger" to the
                    result. On a successful non-final scan, the LED then
                    briefly shows "op success" for a fixed RESULT_DISPLAY_MS
                    (webpage.c) to confirm the capture, then goes straight
                    back to "awaiting finger" for the next capture — no
                    lift-wait in between. On the final scan, once its
                    template is stored, the LED shows "op success" as the
                    terminal indicator. A capture presence-wait elapsing
                    without a finger being placed is routine, expected
                    behaviour, not an error — no LED change, no abort; the
                    web interface simply keeps polling (see SWS-FPM103 for
                    how the user actually stops a stalled attempt).
                  - The web interface (SWS-WP003) shows enrollment progress
                    via a dedicated progress indicator (current template,
                    and a filled/current/pending dot per scan, six total)
                    plus a single, clearly distinguished message area for
                    the current instruction — "place your finger on the
                    sensor" for the very first capture of a session, "lift
                    your finger and place it again" for every capture after
                    that. This message only changes on real state
                    transitions (new capture, success, error) — the routine
                    "still waiting" retries are conveyed only via a
                    continuously-animated waiting indicator, not by
                    rewriting the instruction text on every retry.
- [X] SWS-FPM103  Enrollment abort and cleanup: a capture presence timeout
                  is routine and never aborts by itself (see SWS-FPM102) —
                  the user's escape hatch out of a stalled capture wait is
                  the wizard's Cancel control, not an automatic timeout. So
                  an enrollment attempt ends before completion on: a
                  genuine capture/commit failure, or the user explicitly
                  cancelling via the Cancel control (present for the
                  duration of the wizard). In either case, the whole
                  enrollment attempt (all templates for the finger being
                  added, not just the current one) is aborted — the web
                  interface is told this is a terminal failure/cancellation
                  with a specific reason, and any templates already
                  committed to the sensor's fingerprint library during that
                  same attempt are deleted, so an aborted or failed
                  enrollment never leaves a partial (fewer than 3
                  templates) fingerprint behind. Since a cancel request and
                  an in-flight capture request may overlap, cancellation
                  takes effect as soon as the in-flight request completes —
                  not necessarily instantaneously.
- [X] SWS-FPM104  General scan-result LED persistence (applies to any
                  single-scan identification attempt, e.g. Reset Device's
                  admin verification below; Normal-Mode scanning still
                  needs its own equivalent update once specified — see
                  Normal Mode TBD):
                  - "awaiting finger" (breathing blue) while waiting for a
                    finger to be presented.
                  - Once a scan attempt produces a definitive result, the
                    result LED ("scan success" solid, or "scan failed") is
                    shown for a fixed RESULT_DISPLAY_MS (webpage.c) — not
                    swapped out the instant a result is known, and not
                    waiting for the finger to be lifted first (see
                    SWS-FPM102 for why lift-detection isn't used at all).
                  - After that, "awaiting finger" resumes for the next
                    attempt (e.g. Reset Device looping back after a wrong
                    finger). The next attempt's own presence-wait requires
                    a genuinely fresh touch (see SWS-FPM102), which is what
                    actually enforces "lift and place again" between tries.
                  - If waiting for a finger to be presented at all times
                    out (nobody scans anything), the LED reverts to the
                    mode's baseline/idle indicator (Diagnostic State 0 in
                    Setup-Mode; off in Normal-Mode) rather than continuing
                    to show "awaiting finger" indefinitely, and the
                    operation is treated as aborted, not silently retried.
                  - The web interface mirrors this with the same
                    explicit-step presentation as the enrollment wizard
                    (SWS-FPM102): distinct "place finger" / "scan failed,
                    try again" / "recognized" messages rather than a single
                    box whose text silently changes.
                  - Applied to SWS-WP102's Reset Device flow as a single
                    request per attempt (mirroring SWS-FPM102's single
                    capture per request): wait for a finger to be
                    presented, allow a brief settle delay for the finger to
                    seat before the first capture attempt, attempt the 1:N
                    match and hold the result LED, then either perform the
                    actual erase (admin matched) or go back to "awaiting
                    finger" so the frontend loops back for another attempt.

### Normal Mode
- [X] SWS-FPM201  In Normal mode, the fingerprint module is used for scanning
                  only — no enrollment, no library management (Setup-Mode-only,
                  see SWS-WP104.3).
- [X] SWS-FPM202  Scan loop:
                  - While idle, the task blocks on the existing
                    interrupt-driven g_fp_sense_sem, using effectively no CPU
                    time. This semaphore only wakes the task on a fresh touch
                    event — it is never trusted as a "finger is present"
                    signal by itself.
                  - On wake, the task attempts the scan directly — it does
                    NOT show "awaiting finger" first. The wake itself already
                    means a finger just made contact, so showing "awaiting
                    finger" (implying "not yet detected") and then switching
                    to a scan a few milliseconds later would just be a
                    visible LED glitch for an event that has, in fact,
                    already happened.
                  - The scan result LED is shown per the general pattern
                    already specified in SWS-FPM104.
                  - A successful match (regardless of whether the resulting
                    MQTT publish actually happened or was skipped, e.g. an
                    unconfigured function code — SWS-MQT04/SWS-FPM203) is a
                    terminal event for the session: once its result display
                    is done, the LED reverts straight to the Normal-Mode
                    baseline (off) and the task goes back to blocking on
                    g_fp_sense_sem (idle). There is no follow-up polling
                    wait after a match — nothing more to gain from waiting
                    around for another scan once the touch has already been
                    identified.
                  - A no-match or scan error, on the other hand, is not
                    necessarily the end of the attempt (contact may have
                    been marginal, or it may genuinely be the wrong
                    finger and the user wants to retry immediately): once
                    that result display is done, "awaiting finger"
                    (breathing blue) resumes and the task actively polls via
                    io_WaitFingerPresent() (io.c) — the same debounced,
                    edge-requiring primitive Setup-Mode uses (SWS-FPM102) —
                    in case the same session involves a retry. If
                    io_WaitFingerPresent() times out after 10s with no
                    further finger placed, the LED reverts to the
                    Normal-Mode baseline (off) and the task goes back to
                    blocking on g_fp_sense_sem (idle) until the next fresh
                    touch wakes it.
- [X] SWS-FPM203  On a successful match, the matched fingerprint's metadata
                  (uuid, finger_id, function_code), the resolved display
                  name, and the match score are used to publish an MQTT
                  message (SWS-MQT04). On no match, nothing is published.
- [X] SWS-FPM204  Admin/master fingerprint override: if the fingerprint
                  matched during Normal-Mode scanning (SWS-FPM202) is the
                  admin/master fingerprint (SWS-WP201: finger_id 15), it is
                  not treated as a regular scan result — no MQTT message is
                  published (SWS-FPM203/SWS-MQT04 do not apply to it).
                  Instead, after a brief LED confirmation ("op success",
                  held for the same fixed display duration as any other
                  scan result), the device sets the setup-enter-flag
                  (SWS-NVS002, read by SWS-MOD002) and reboots via a plain,
                  unconditional esp_restart() — not routed through the
                  Central Error Handler, since this isn't an error
                  condition (mirrors SWS-WDT002's scheduled reboot). This
                  gives an admin a way to re-enter Setup-Mode (e.g. to
                  reconfigure WiFi/MQTT) without physical access to the
                  device's boot process, by presenting the same master
                  fingerprint used for First-Run-Mode enrollment
                  (SWS-WP201) and Setup-Mode's Reset Device admin
                  verification (SWS-FPM104).


## Webpage Interface
- [X] SWS-WP001   The user sets the device up via the webpage.
- [X] SWS-WP002   The Webpage Can be started in two modes: First-Run-Mode and
                  Normal-Setup-Mode. Which mode is started, is passed to the
                  init function.
- [X] SWS-WP003   The webpage interface should look nicely and should use
                  javascript for nice look and feel. The webpage is mostly
                  accessed by mobile devices.

### Normal-Setup-Mode
- [X] SWS-WP101    In normal setup mode, a nicely formatted menu is shown.
                   The follwing Specification entries describe the menu entries.
- [X] SWS-WP102    System Settings with subentries. Subentries read the
                   configuration from nvs if the settings change, the new values
                   are stored within nvs.
                    - Sheduled Reboot (in minutes) display current and change, 
                      0 for off.
                    - Wifi Setup: see in (SWS-NVS02)
                    - Mqtt Setup: see in (SWS-NVS02)
                    - Reset Device, queries the user for the admin-fingerprint,
                      if scanned, resets nvs and the fpm.
                    - View Errors: shows the NVS error FIFO entries
                      (SWS-NVS004 / SWS-CEH002) and, per error code, how
                      many times it has occurred since the last clear
                      (SWS-NVS005), with a Clear control that empties both
                      (nvs_ErrorClear()).
                   NOTE: was previously marked [X] as a whole while the "View
                   Errors" subentry had never actually been built (no backend
                   endpoint, no webpage menu item) — flipped back to [ ]
                   until that subentry is implemented and verified on real
                   hardware too, per this repo's checkbox convention (every
                   subentry, not just most of them).
- [X] SWS-WP103    User Settings with subentries
- [X] SWS-WP103.1  List users: Get user names with uuid from nvs.
- [X] SWS-WP103.2  Edit user: Querries for uuid, changes name, stores changes
- [X] SWS-WP103.3  Delete user: Querries for uuid, delelts uuid from nvs, delets
                   fingerprints in fpm.
- [X] SWS-WP104    Fingerprint Library with subentries.
- [X] SWS-WP104.1  List: with the information from nvs and the list function from
                   the fpm, a nice formatted table where the infromation is shown.
                   NOTE: the admin/master fingerprint (SWS-WP201) is never
                   included in this list — it isn't a regular user's
                   fingerprint, and it can't be deleted through this
                   screen anyway (see SWS-WP104.2), so showing it would
                   only invite a confusing "delete" attempt that has to
                   be refused.
- [X] SWS-WP104.2  Delete: queries for user, lists all fingerprints for the user,
                   shows them with relative ids. Queries for id to delete.
                   Deletes fingerprint in fpm.
- [X] SWS-WP104.3  Add: Queries for uuid, queries for finger-id (0 is pinky left,
                   4 is thumb left, 5 is pinky right and so on). Queries for
                   function code. After the information is gathered, finger is
                   scanned with the fpm. After a scuessfull store, the finger is
                   enrolled a second and then a third time (so each finger is
                   stored with three templates within the fingerprint-library).
- [X] SWS-WP105    Exit - which triggers the device to reboot.

### First-Run-Mode
- [X] SWS-WP201    In First Run Mode, the user is greeted and asked to start
                   by enrolling the admin-fingerprint. see SWS-WP104.3 for
                   the enrolling process. The admin UUID is 0, the master
                   finger-id is 15, the master function code is 0.
- [X] SWS-WP201    After successfull enrollment, the device is rebooted, the
                   setup-enter-flag is set before.


## Watchdog
- [X] SWS-WDT001  Cooperative software watchdog: each long-running task
                  registers itself (wdt_RegisterTask()) and periodically
                  resets its own entry (wdt_Reset()) from within its normal
                  loop. A dedicated monitor task periodically checks every
                  registered task's time since its last reset; if any task
                  exceeds its allowed interval, ceh_Fatal() is called with a
                  dedicated watchdog error code (SWS-CEH005) — the same
                  LED-blink-then-reboot behavior as any other fatal error.
                  This is a cooperative check, not ESP-IDF's hardware/task
                  watchdog panicking directly, so every fatal condition in
                  this project behaves consistently — at the cost of not
                  catching a task frozen solidly enough to never reach its
                  own watchdog check-in. As an absolute fallback for exactly
                  that case, ESP-IDF's hardware task watchdog (TWDT) is also
                  enabled in parallel, in case the software monitor task
                  itself freezes too — it won't have the graceful blink/log
                  sequence, but it guarantees the device doesn't hang forever.
- [X] SWS-WDT002  Scheduled reboot: the watchdog module also owns the
                  scheduled-reboot feature (SWS-NVS002's "Scheduled Reboot
                  Time in minutes", 0 = disabled) — a plain, unconditional
                  esp_restart() once the configured interval has elapsed
                  since boot. Not routed through the Central Error Handler,
                  since it isn't an error condition.


## Central Error Handler
- [X] SWS-CEH001  The Central Error Handler (CEH) is called whenever a
                  condition needs to be surfaced and/or recorded: a WiFi or
                  MQTT connection that could not be (re-)established, a
                  resource allocation failure, or a watchdog trip (Watchdog).
- [X] SWS-CEH002  Every CEH call — fatal or not — records an entry in the
                  NVS error FIFO (SWS-NVS004, nvs_ErrorPush()), so the
                  history of what went wrong is visible later via the
                  webpage (SWS-WP102's new "View Errors" entry). Deduplicated:
                  if the same error condition is still ongoing (e.g. WiFi
                  still down on every retry attempt), it does not get a new
                  FIFO entry each time — only the first occurrence is
                  recorded, until either the condition clears or a different
                  error occurs. Otherwise a single persistent condition would
                  flood the 10-slot FIFO and push out genuinely distinct,
                  older history.
- [X] SWS-CEH003  Non-fatal errors: logged via SWS-CEH002 only — no LED
                  change, no reboot. Used when a condition is recoverable and
                  a retry is already in progress (e.g. a dropped WiFi or MQTT
                  connection while the device is still trying to reconnect —
                  see SWS-MOD203).
- [X] SWS-CEH004  Fatal errors (ceh_Fatal()): in addition to SWS-CEH002, the
                  sensor LED shows the error's blink code (SWS-FPM003 / the
                  existing ERROR_1..5 LED states), then goes off for a 2s
                  pause, then shows the same blink code again — repeating for
                  up to 30s total — after which the device reboots
                  (esp_restart()). If no LED callback is registered yet (very
                  early boot, before the sensor is initialised), the device
                  reboots immediately without attempting to show anything on
                  the LED — but a log message (ESP_LOGE) is still printed
                  first either way, so there's always at least a serial-console
                  trail even when nothing can be shown on the LED.
- [X] SWS-CEH005  Fatal error code -> blink code mapping:
                  - CEH_ERR_RESOURCE     -> ERROR_1 (existing)
                  - CEH_ERR_WIFI_BOOT    -> ERROR_2 (existing)
                  - CEH_ERR_WIFI_RUNTIME -> ERROR_3 (new, SWS-MOD203)
                  - CEH_ERR_MQTT_RUNTIME -> ERROR_4 (new, SWS-MOD203)
                  - CEH_ERR_WATCHDOG     -> ERROR_5 (new, SWS-WDT001)
                  CEH_ERR_FPM_INIT and CEH_ERR_NVS_INIT occur before the
                  sensor LED is available, so they reboot immediately per
                  SWS-CEH004 without a blink code.


## Modes
- [X] SWS-MOD001  The device can operate in two Modes: Normal-Mode and Setup-Mode.
- [X] SWS-MOD002  Setup mode is entered when:
                  - NVS was never used before, so contains invalid data.
                  - Wihtin NVS the setup-enter-flag was previously set
                  - In NVS, no Wifi or mqtt credentails were setup.
                  NOTE: "NVS never used before" and "no wifi/mqtt credentials"
                  collapse into one check — both read back empty with the
                  existing typed accessors, so app_mode_Decide() (mode.c) is
                  a single OR over setup_flag / empty ssid / empty broker.
- [X] SWS-MOD003  If non of the conditions for setup mode are fulfilled,
                  normal-mode is entered.
- [X] SWS-MOD004  Boot-time admin-finger override: after fps_Init()
                  succeeds (sensor + LED available) but before WiFi is
                  initialised, if app_mode_Decide() (SWS-MOD002/003) chose
                  Normal-Mode, the device does a single, non-blocking check
                  for a finger already resting on the sensor — a raw
                  presence read (io_FpSensePresent()), not the debounced,
                  edge-requiring io_WaitFingerPresent(): there is no
                  "not-present" baseline to compare against this early in
                  boot, and the finger may already have been resting there
                  through several prior failed boot attempts. If a finger
                  is present, one identification scan is attempted; if it
                  matches the admin/master fingerprint (SWS-WP201:
                  finger_id 15), Normal-Mode's boot sequence (SWS-MOD201/
                  202) is skipped and the device instead proceeds as if
                  Setup-Mode had been decided (AP wifi, webpage interface).
                  No finger present, or no match: normal boot continues
                  unaffected — this adds no delay in the common case of
                  nobody touching the sensor.
                  This exists as an escape hatch out of a WiFi/MQTT
                  connect-failure boot loop (SWS-MOD202 / SWS-CEH004): if
                  the configured broker/credentials are wrong (not merely
                  unset — an unset broker already routes to Setup-Mode via
                  SWS-MOD002), the device would otherwise keep rebooting
                  indefinitely with no way to reach the webpage and fix the
                  configuration. Complements SWS-FPM204, which covers the
                  same admin-finger-to-Setup-Mode behaviour once the device
                  has already reached Normal-Mode's scan loop; this entry
                  covers the boot-time window before that loop — and
                  before WiFi/MQTT — is even reached.

### Setup Mode
- [X] SWS-MOD101  Entering Setup mode resets the setup-enter-flag in NVS
- [X] SWS-MOD102  Within Setup mode, there is no "normal" fingerprint scanning
                  operation. There is also no mqtt communiction
- [X] SWS-MOD103  Wifi is setup in AP mode.
- [X] SWS-MOD104  In setup mode, the Webpage Interface is active.
- [X] SWS-MOD105  If there is no Admin-Fingerprint configured, the Webpage is
                  started in First-Run-Mode, otherwise in Normal-Setup-Mode.
- [X] SWS-MOD106  Three-stage diagnostic LED sequence during Setup mode
                  boot-up:
                  - Diagnostic State 1 (flashing) from the moment Setup
                    mode is entered until the wifi AP and the webpage
                    interface are both up and actually serving requests.
                  - Diagnostic State 2 (breathing) once the AP/webpage are
                    ready but no client has connected to the AP yet.
                  - Diagnostic State 0 (solid) once a client has actually
                    been assigned an IP address by the AP's DHCP server
                    (IP_EVENT_ASSIGNED_IP_TO_CLIENT) — this is the user's
                    confirmation that they are correctly connected and
                    setup is under way.
                  Diagnostic State 0 remains the baseline/idle indicator
                  for the rest of Setup mode (enrollment and scan
                  operations temporarily show their own LED states and
                  restore Diagnostic State 0 afterward).

### Normal Mode
- [X] SWS-MOD201  On entering Normal mode, the device connects to the WiFi
                  network and MQTT broker configured in NVS (both are
                  guaranteed non-empty — otherwise SWS-MOD002 would have
                  routed to Setup mode instead).
- [X] SWS-MOD202  Boot LED sequence:
                  - Diagnostic State 1 (flashing) while connecting to WiFi.
                  - Diagnostic State 2 (breathing) while connecting to the
                    MQTT broker (once WiFi is up).
                  - "op success" once both are connected, then the device
                    proceeds to normal scanning operation (SWS-FPM202).
- [X] SWS-MOD203  If the WiFi or MQTT connection is lost during operation
                  (not during the initial boot sequence in SWS-MOD202): a
                  non-fatal CEH entry is recorded (SWS-CEH003) and the device
                  attempts to reconnect for up to 2 minutes. If reconnection
                  succeeds within that window, operation resumes normally. If
                  it does not, ceh_Fatal() is called (SWS-CEH004) with the
                  matching runtime-connection-lost error code (SWS-CEH005),
                  rebooting the device after showing the blink code.
- [X] SWS-MOD204  While connected and idle, the device sends MQTT heartbeats
                  per the configured interval (if enabled, SWS-NVS002) and
                  otherwise waits for the user to place a finger on the
                  sensor (SWS-FPM202).