# Overview:
ESP32 (2MB flash) Arduino project built with PlatformIO.
PN5180 NFC (ISO15693) — reads/writes OpenPrintTag spec
16x2 I2C LCD — user feedback
BLE config via docs/index.html

# Guidlines:
Be concise in responses
Do NOT attempt to build — ask user to build
Consider thread safety for all changes
Add new files to source inventory (one-line)
Run tests after changes: ./scripts/run_all_tests.sh

# Architecture summary:
main.cpp: Initializes all managers, starts FreeRTOS tasks
ApplicationManager: Central state machine + message bus, receives events (print start, spool scan, etc.) via queue and coordinates responses.
NFC Stack: NFCManager -> HardwareNFCConnection -> PN5180 driver -> ISO15693 Tag -> openprinttag_lib (CBOR encode/decode)
Printer Polling: PrinterManager task polls -> PrusaLinkAPIStrategy::update() -> HTTP GET to /api/v1/status & /api/v1/job
Spoolman Sync: ApplicationManager triggers sync -> SpoolmanManager queues request -> SpoolmanManager task -> HTTP requests to /api/v1/... endpoints.
Home Assistant: ApplicationManager publishes events -> HomeAssistantManager queues -> MQTT publish. HA commands arrive via MQTT subscribe -> AppMessage queue -> ApplicationManager handles.
Configuration: Web Browser <-> BLE <-> BluetoothManager <-> ConfigurationManager & NFCManager

# Source Inventory
OpenPrintTag Library
lib/openprinttag/cbor.h / cbor_native.c — Minimal CBOR implementation for native tests
lib/openprinttag/openprinttag_lib.c / .h — Encode/decode filament data (CBOR, NDEF)
lib/openprinttag/openprinttag_pn532.h — HAL adapter for PN532

PN5180 Driver
lib/PN5180/Debug.cpp / .h — Hex/debug helpers
lib/PN5180/PN5180.cpp / .h — Core driver, SPI + register control
lib/PN5180/PN5180ISO15693.cpp / .h — ISO15693 protocol implementation

Application Core
src/main.cpp — Entry point, task startup
src/ApplicationManager.cpp / .h — Central state machine + event queue
src/ConfigurationManager.cpp / .h — Device config (WiFi, API keys, NVS)

NFC
src/NFCManager.cpp / .h — Scan/read/write task
src/HardwareNFCConnection.cpp / .h — PN5180 hardware adapter
src/NFCConnectionI.h — NFC hardware interface
src/NFCTypes.h — Detected spool state structs
src/NFCWriteTypes.h — Write queue types/enums

Printer Integration
src/PrinterManager.cpp / .h — Polls printer, emits job events
src/IPrinterLinkStrategy.h — Printer API interface
src/PrusaLinkAPIStrategy.cpp / .h — PrusaLink REST implementation
src/StubPrinterLinkStrategy.cpp / .h — Test stub

Spool Sync
src/SpoolmanManager.cpp / .h — Spoolman API sync + queue worker

Home Assistant
src/HomeAssistantManager.cpp / .h — MQTT client task, publish/subscribe, HA discovery

UI / UX
src/LCDManager.cpp / .h — I2C LCD task + status updates
src/LCDDisplayLogic.h — Shared LCD message merge/timing rules
src/BluetoothManager.cpp / .h — BLE services + connections
docs/index.html — BLE config UI (device setup + spool editing)

Utilities
src/BgcodeParser.cpp / .h — Extracts "filament used [g]" from .bgcode

Tests
OpenPrintTag
test/test_openprinttag.c — CBOR + NDEF unit tests (mock HAL)

Native Fakes / Stubs
test/native/FakeLCDManager.h — In-memory LCD
test/native/StubApplicationManager.h — Message capture stub
test/native/StubNFCConnection.h — Simulated NFC tags
test/native/NativePlatform.cpp — Stub Serial

Native Tests
test/native/test_app_flow.cpp — App state transitions
test/native/test_bgcode_parser.cpp — Parser validation
test/native/test_lcd_manager.cpp — LCD message merge timing behavior
test/native/test_nfc_read.cpp — NFC read behavior
test/native/TestableApplicationManager.h — Queue bypass harness
test/native/TestNFCManager.h — Write queue tracker
test/native/test_helpers.h — Factories + assertions

Integration Tests
test/integration/ha.cpp — Native standalone MQTT/HA connectivity + discovery/state publisher
test/integration/Makefile — Build/run helper for local HA integration probe

Hardware-in-the-Loop Tests
test/integration/http_server.py — Test orchestrator + mock PrusaLink API + SSE server
test/integration/mock_prusalink.py — MockPrusalinkState (controls mock API responses)
test/integration/scenarios/base.py — BaseTestScenario with BLE bridge helpers
test/integration/scenarios/test_format_spool.py — Format spool test
test/integration/scenarios/test_set_filament.py — Set filament weight test
test/integration/scenarios/test_print_e2e.py — End-to-end print simulation test
test/integration/site/index.html — Web Bluetooth test runner UI
