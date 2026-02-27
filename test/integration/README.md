# Hardware-in-the-Loop Integration Tests

Real ESP32 device testing against mock services over network.

## Architecture

```
[Browser/HTML]              [Python Server]            [ESP32 Device]
    |                              |                          |
    |--- SSE subscribe ---------->|                          |
    |<-- event: run BLE cmd ------|                          |
    |--- BLE write/read ---------------------------------------->|
    |<-- BLE response -------------------------------------------|
    |--- POST /api/tests/ble-result ->|                       |
    |                              |<--- GET /api/v1/status --|
    |                              |--- mock JSON response -->|
```

## Quick Start

1. **Start the server:**
   ```bash
   cd test/integration
   python3 http_server.py --port 8080
   ```

2. **Open test UI in Chrome:**
   - Navigate to `http://localhost:8080/`
   - **IMPORTANT:** Must use `localhost` or `127.0.0.1` (NOT the LAN IP address)
   - Web Bluetooth API requires HTTPS or localhost for security
   - Chrome or Edge required (Firefox doesn't support Web Bluetooth)

   **Note:** The browser accesses via `localhost`, but the test automatically configures your ESP32 device with the server's LAN IP (e.g., `http://192.168.1.100:8080`) so the device can reach the mock PrusaLink API over the network.

3. **Connect to device:**
   - Click "Connect BLE" and select your ESP32 device
   - Click "Save Current Settings" to backup config

4. **Run tests:**
   - Click individual "Run" buttons or "Run All Tests"
   - Follow physical prompts (place spool when asked)
   - Tests will restore config automatically when complete

## Test Scenarios

### Test 1: Format Spool
- Place blank/formatted spool on reader
- Formats tag with defaults (PLA, 1000g, white)
- Verifies all default values

### Test 2: Set Filament Weight
- Place formatted spool on reader
- Updates weight to 1000g, verifies
- Updates weight to 967g, verifies

### Test 5: Set Filament Type + Manufacturer
- Place formatted spool on reader
- Verifies initial values are PLA and Unknown (or empty manufacturer)
- Updates type to PETG and manufacturer to Prusament
- Verifies both values persisted on tag

### Test 3: End-to-End Print
- Configures device to poll mock PrusaLink
- Sets spool to 1000g
- Simulates print job consuming 9.18g filament
- Verifies final weight is ~991g (±2g tolerance)
- Automatically restores original config

### Test 4: End-to-End Test - 30% Print
- Configures device to poll mock PrusaLink
- Sets spool to 1000g
- Simulates a print canceled at 30% progress
- Verifies only ~30% of filament is deducted from the spool
- Automatically restores original config

## Components

- **http_server.py** — HTTP server, test orchestrator, mock PrusaLink API
- **mock_prusalink.py** — Mutable state controlling mock API responses
- **scenarios/base.py** — Base class with BLE bridge helpers
- **scenarios/test_*.py** — Individual test scenarios
- **site/index.html** — Web Bluetooth UI, SSE client, BLE bridge

## Mock PrusaLink API

The server provides these endpoints for the ESP32:
- `GET /api/v1/status` — Printer status with job info
- `GET /api/v1/job` — Current/finished job metadata
- `GET /api/v1/job/{id}` — Specific job metadata (deferred fetch)
- `GET /usb/*` — Serves sample.bgcode with Range header support

API key validation: `X-Api-Key: test-api-key`

## Requirements

- Python 3.7+
- Chrome browser (Web Bluetooth)
- ESP32 device running OpenPrintTag firmware
- Formatted NFC tag for testing

## Notes

- WiFi credentials are never sent during config changes — device stays connected
- Tests use partial config updates (only change PrusaLink settings)
- Original config is automatically restored after Test 3 and Test 4
- Sample bgcode file must exist at `test/res/sample.bgcode` (455KB)
