# Software Specification for Esp32FingerprintScanner

## NVS
- [X] SWS-NVS01   The device uses ESP32s NVS. NVS is used to store setup-,
                  user- and debug-information. There are functions to read and 
                  write those informations from and to NVS.
- [X] SWS-NVS02   Setup-information structure:
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
- [X] SWS-NVS03   User-information structure:
                  - Up to 127 UUIDs (Range 1..127) with Username.
                    UUID 0 marks empty slots.
- [X] SWS-NVS04   Error-information structure:
                  - 10 Slots of Error information (FIFO) where each slots contains
                    an error message.


## Wifi
- [ ] SWS-WIF01   The device uses wifi. It operates in two different modes client,
                  and AP. Which mode the device operates in, is decicded at 
                  system startup.
- [ ] SWS-WIF02   AP mode: In Access Point mode, a Wifi with SSID
                  "Esp32FingerprintScanner", and password "esp32" is setup. 
- [ ] SWS-WIF03   Client mode: In Client mode, the devices connects to a passed
                  ssid and password. If the connection failed, the blocking init
                  function returns with an error.
- [ ] SWS-WIF04   There will be a system event triggered, if the connection to
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
- [ ] SWS-FPM01   The device uses the finger_print_module
TODO: Spezifizieren wie sich led verhält, algmeins wie fingerprints im modul 
abgelegt werden, wie funktioniert hinzufügen, scannen löschen etc.


## Webpage Interface
- [ ] SWS-WP001   The user sets the device up via the webpage.
- [ ] SWS-WP002   The Webpage Can be started in two modes: First-Run-Mode and
                  Normal-Setup-Mode. Which mode is started, is passed to the
                  init function.
- [ ] SWS-WP003   The webpage interface should look nicely and should use
                  javascript for nice look and feel. The webpage is mostly
                  accessed by mobile devices.

### Normal-Setup-Mode
- [ ] SWS-WP101    In normal setup mode, a nicely formatted menu is shown.
                   The follwing Specification entries describe the menu entries.
- [ ] SWS-WP102    System Settings with subentries. Subentries read the
                   configuration from nvs if the settings change, the new values
                   are stored within nvs.
                    - Sheduled Reboot (in minutes) display current and change, 
                      0 for off.
                    - Wifi Setup: see in (SWS-NVS02)
                    - Mqtt Setup: see in (SWS-NVS02)
                    - Reset Device, queries the user for the admin-fingerprint,
                      if scanned, resets nvs and the fpm.
- [ ] SWS-WP103    User Settings with subentries
- [ ] SWS-WP103.1  List users: Get user names with uuid from nvs.
- [ ] SWS-WP103.2  Edit user: Querries for uuid, changes name, stores changes
- [ ] SWS-WP103.3  Delete user: Querries for uuid, delelts uuid from nvs, delets
                   fingerprints in fpm.
- [ ] SWS-WP104    Fingerprint Library with subentries.
- [ ] SWS-WP104.1  List: with the information from nvs and the list function from
                   the fpm, a nice formatted table where the infromation is shown.
- [ ] SWS-WP104.2  Delete: queries for user, lists all fingerprints for the user,
                   shows them with relative ids. Queries for id to delete.
                   Deletes fingerprint in fpm.
- [ ] SWS-WP104.3  Add: Queries for uuid, queries for finger-id (0 is pinky left,
                   4 is thumb left, 5 is pinky right and so on). Queries for
                   function code. After the information is gathered, finger is
                   scanned with the fpm. After a scuessfull store, the finger is
                   enrolled a second and then a third time (so each finger is
                   stored with three templates within the fingerprint-library).
- [ ] SWS-WP105    Exit - which triggers the device to reboot.

### First-Run-Mode
- [ ] SWS-WP201    In First Run Mode, the user is greeted and asked to start
                   by enrolling the admin-fingerprint. see SWS-WP104.3 for
                   the enrolling process. The admin UUID is 0, the master
                   finger-id is 15, the master function code is 0.
- [ ] SWS-WP201    After successfull enrollment, the device is rebooted, the
                   setup-enter-flag is set before.


## Watchdog
TODO: TBD


## Central Error Handler
TODO: TBD


## Modes
- [ ] SWS-MOD101  The device can operate in two Modes: Normal-Mode and Setup-Mode.
- [ ] SWS-MOD102  Setup mode is entered when:
                  - NVS was never used before, so contains invalid data.
                  - Wihtin NVS the setup-enter-flag was previously set
                  - In NVS, no Wifi or mqtt credentails were setup.
- [ ] SWS-MOD103  If non of the conditions for setup mode are fulfilled,
                  normal-mode is entered.

### Setup Mode
- [ ] SWS-MOD204  Entering Setup mode resets the setup-enter-flag in NVS
- [ ] SWS-MOD205  Within Setup mode, there is no "normal" fingerprint scanning
                  operation. There is also no mqtt communiction
- [ ] SWS-MOD306  Wifi is setup in AP mode.
- [ ] SWS-MOD307  In setup mode, the Webpage Interface is active.
- [ ] SWS-MOD308  If there is no Admin-Fingerprint configured, the Webpage is
                  started in First-Run-Mode, otherwise in Normal-Setup-Mode.
- [ ] SWS-MOD309  During Setup mode, the Diagnostic State 0 is shown on the 
                  fpm led.

### Normal Mode
TBD