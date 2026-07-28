# Roadmap for Software Development of Esp32FingerprintScanner

- [X] Finish the relevant parts of the specifictaion: Fingerprint Module Normal Mode
- [X] Implement NVS
- [X] Implement Webpage Interface
- [X] Setup the basic software structure, supporting two modes.
- [X] Implement Setup mode.
- [X] Test Setup mode so far. (needs real hardware — see doc/specification.md's
      Webpage Interface verification checklist)
- [X] Implement Webpage.
- [X] Implement First-Run-mode. (as part of the Webpage Interface, not a
      separate CLI — the roadmap's "CLI" wording predates this decision)
- [X] Specify ceh, implement, test
- [X] Specify watchdog, implement, test
- [X] Specify Normal-Mode
- [X] Implement Mqtt, CEH, WDT
- [X] Implement Normal-Mode
- [X] Test the implementation
- [ ] Investigate MQTT/WIFI Runtime Errors
- [ ] OTA Update
- [ ] Solider curcuit board with sockets for esp & r503 connector
- [ ] Run a longer test with this board
- [ ] Design mounting behind the doorbell faceplate for curcuit board and 
      doorbell
- [ ] Fieldtest