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
- [ ] SWS-MQT01   The device is an mqtt client. On init a cfg structure is passed,
                  with, a broker ip, username, password, last-will-topic and message,
                  heartbeat topic, message and interval.
                  The blocking init function returns with an error, if connection
                  to the broker failed.
- [ ] SWS-MQT02   There will be a system event triggered, if the connection to
                  the brokter fails during device operation.
- [ ] SWS-MQT03   The heartbeat is send automatically with the configured interval.
- [ ] SWS-MQT04   Otherwise, there is a publish function to publish a message to
                  a topic.
- [ ] SWS-MQT05   There is currently no need for supporting topic subscription


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
                  - The fp-sense pin is active HIGH via an external
                    transistor buffer stage between the R503's own (active
                    LOW) touch output and the GPIO (see io.c) — wiring the
                    sensor's output directly into the GPIO caused real,
                    oscilloscope-confirmed disturbances specifically while
                    that pin was being actively polled; the buffer fixed
                    that specific issue (separate from the lift-detection
                    limitation above, which is a sensor firmware
                    characteristic, not a wiring issue).
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
TODO: TBD


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
TODO: TBD


## Central Error Handler
TODO: TBD


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

### Setup Mode
- [X] SWS-MOD104  Entering Setup mode resets the setup-enter-flag in NVS
- [X] SWS-MOD105  Within Setup mode, there is no "normal" fingerprint scanning
                  operation. There is also no mqtt communiction
- [X] SWS-MOD106  Wifi is setup in AP mode.
- [X] SWS-MOD107  In setup mode, the Webpage Interface is active.
- [X] SWS-MOD108  If there is no Admin-Fingerprint configured, the Webpage is
                  started in First-Run-Mode, otherwise in Normal-Setup-Mode.
- [X] SWS-MOD109  Three-stage diagnostic LED sequence during Setup mode
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
TBD