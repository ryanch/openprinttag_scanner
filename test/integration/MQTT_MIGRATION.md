# MQTT Event-Driven Test Infrastructure

## Overview

The integration test infrastructure now supports MQTT-driven waits that respond immediately to device state changes, replacing fixed `_wait_seconds()` delays. This makes tests faster and more reliable.

## Setup

### 1. Create Python Virtual Environment

```bash
# From project root
cd test/integration
python3 -m venv venv
```

### 2. Install Dependencies

```bash
# Activate virtual environment
source venv/bin/activate  # On Linux/Mac
# OR
venv\Scripts\activate     # On Windows

# Install requirements
pip install -r requirements.txt
```

This installs `paho-mqtt>=1.6.1`.

**Note:** You need to activate the virtual environment (`source venv/bin/activate`) each time you open a new terminal to run tests.

### 3. Configure MQTT

Edit `test/integration/mqtt_config.json`:

```json
{
  "broker_host": "localhost",
  "broker_port": 1883,
  "device_id": "aabbcc",
  "username": "",
  "password": ""
}
```

**Finding your device_id:**
- It's the last 6 hex characters of the ESP32 MAC address
- Check BLE device config via web interface
- Look for HA entity names (e.g., `sensor.openprinttag_aabbcc_tag_state`)
- Check serial output: `HomeAssistantManager: Initialized (device_id=...)`

### 4. Verify MQTT Broker

Ensure your MQTT broker (e.g., Mosquitto) is running:

```bash
# Check broker status
systemctl status mosquitto

# Subscribe to test topic (replace aabbcc with your device_id)
mosquitto_sub -h localhost -t 'openprinttag/aabbcc/tag/state' -v
```

## How It Works

### Device Side
The ESP32 publishes to `openprinttag/{device_id}/tag/state` on every tag state change:
- Tag placed/removed
- Weight updated
- Material type changed
- Any field modification

Messages are retained (latest state always available to new subscribers).

### Test Side
Tests subscribe to the tag state topic and wait for specific conditions:

```python
# Wait for specific weight
state = self._wait_for_mqtt_remaining_weight(1000, max_wait_sec=30)

# Wait for tag present
state = self._wait_for_mqtt_tag_present(max_wait_sec=30)

# Wait for custom condition
state = self._wait_for_mqtt_spool_update(
    max_wait_sec=30,
    condition=lambda s: s.get("material_type") == "PETG"
)
```

### Fallback Behavior
If MQTT is unavailable (not installed, wrong config, broker down), tests automatically fall back to BLE polling. You'll see warnings like:

```
Warning: paho-mqtt not installed - MQTT event waiting disabled
Warning: MQTT connection failed: Connection refused
```

Tests continue normally using the original BLE polling method.

## Migration Status

### ✅ Migration Complete (16 Files)

**Wave 0 (Initial Implementation - 3 files):**
- ✅ `test_format_spool.py` - Format defaults
- ✅ `test_set_filament.py` - Weight updates
- ✅ `test_print_e2e.py` - End-to-end print

**Wave 1 (Tier 1 - Fixed Waits Only - 6 files):**
- ✅ `test_zero_weight_handling.py` - Weight boundary conditions
- ✅ `test_automation_mode_controlled.py` - HA-controlled mode (no auto-deduction)
- ✅ `test_job_disappeared_deduction.py` - Job disappeared (204) bgcode fallback
- ✅ `test_write_spoolman_spool.py` - Spoolman write modes A & B
- ✅ `test_print_progress_edge_cases.py` - Print progress edge cases (0%, 100%, dwell)
- ✅ `test_printer_api_errors.py` - PrusaLink API error resilience

**Wave 2 (Tier 2 - Simple Polling - 6 files):**
- ✅ `test_set_filament_profile.py` - Multi-field matcher (material_type + manufacturer)
- ✅ `test_color_update.py` - Single field matcher (color)
- ✅ `test_print_30_percent.py` - Canceled print at 30%
- ✅ `test_recent_spools.py` - Spool swap A/B with recently seen history
- ✅ `test_spoolman_sync.py` - Spoolman sync verification
- ✅ `test_spool_swap_during_print.py` - Mid-print spool swap edge case

**Wave 3 (Tier 3 - Compound Conditions - 1 file):**
- ✅ `test_real_tag.py` - Raw binary write + compound condition (type AND weight)

### Migration Statistics

**Custom polling methods eliminated:** 9 total
- Wave 0: 0 (used inline waits)
- Wave 1: 0 (used inline fixed waits)
- Wave 2: 8 methods (`_wait_for_profile`, `_wait_for_color`, `_wait_for_different_spool` ×3, `_wait_for_specific_spool`, etc.)
- Wave 3: 1 method (`_wait_for_type_and_weight`)

**MQTT helper methods added to base.py:**
- `_wait_for_mqtt_field_match()` - Single field matcher
- `_wait_for_mqtt_multi_field_match()` - Multi-field matcher
- `_wait_for_mqtt_spool_update()` - Generic condition with lambda (existing)
- `_wait_for_mqtt_remaining_weight()` - Weight-specific matcher (existing)
- `_wait_for_mqtt_tag_present()` - Tag present matcher (existing)

**Performance improvements:**
- Weight update waits: 5-15s → 1-2s (10-13s saved per update)
- Multi-field waits: 12-15s → 2-3s (9-12s saved per verification)
- Single field waits: 10s → 1-2s (8-9s saved per verification)
- Estimated full test suite speedup: **3-4 minutes faster**

### Migration Patterns Used

**Pattern 1: Weight updates (9 instances)**
```python
# Before: Fixed wait
self._ble_update_spool(spool_id, grams_remaining=1000)
self._wait_seconds(5, 'Waiting for NFC write')

# After: MQTT event-driven + belt-and-suspenders
self._ble_update_spool(spool_id, grams_remaining=1000)
state = self._wait_for_mqtt_remaining_weight(1000, max_wait_sec=30)
spools = self._ble_list_spools()  # Verify
```

**Pattern 2: Single field matcher (3 instances - color)**
```python
# Before: Custom polling method
def _wait_for_color(self, spool_id, expected_color, max_attempts=10):
    for attempt in range(max_attempts):
        spools = self._ble_list_spools()
        if spools["current"]["color"] == expected_color:
            return spools["current"]
        time.sleep(1)

# After: MQTT helper + belt-and-suspenders
self._wait_for_mqtt_field_match("color", "#FF0000", max_wait_sec=30)
spools = self._ble_list_spools()  # Verify
```

**Pattern 3: Multi-field matcher (4 instances - type + weight, type + manufacturer)**
```python
# Before: Custom polling method
def _wait_for_profile(self, expected_type, expected_manufacturer, max_attempts=12):
    for attempt in range(max_attempts):
        spools = self._ble_list_spools()
        if (spools["current"]["material_type"] == expected_type and
            spools["current"]["manufacturer"] == expected_manufacturer):
            return spools["current"]
        time.sleep(1)

# After: MQTT helper + belt-and-suspenders
self._wait_for_mqtt_multi_field_match({
    "material_type": "PETG",
    "manufacturer": "Prusament"
}, max_wait_sec=30)
spools = self._ble_list_spools()  # Verify
```

**Pattern 4: Generic condition with lambda (5 instances - spool swaps)**
```python
# Before: Custom polling method
def _wait_for_different_spool(self, exclude_id, max_attempts=15):
    for attempt in range(max_attempts):
        spools = self._ble_list_spools()
        if spools["current"]["uid"] != exclude_id:
            return spools["current"]
        time.sleep(2)

# After: MQTT helper with lambda + belt-and-suspenders
self._wait_for_mqtt_spool_update(
    condition=lambda s: s.get("uid") != spool_a_id and s.get("present") == True,
    max_wait_sec=30,
    reason="Waiting for different spool"
)
spools = self._ble_list_spools()  # Verify
```

### Preserved Fixed Waits (By Category)

**Print Detection (10 instances):**
- Device polling to detect print start - requires external timing, no immediate tag update
- Kept as `_wait_seconds(15, "Waiting for device to detect print")`

**Async HTTP/Metadata Fetch (8 instances):**
- Bgcode file fetches, grace poll periods - external I/O timing
- Kept as `_wait_seconds(20, "Waiting for device to fetch metadata...")`

**Test Requirements (5 instances):**
- Dwell tests (30s at 100%)
- Mode 1 no-auto-deduct verification
- Pre-color-change timing
- Kept as test-specific timing requirements

**State Processing (7 instances):**
- Print cancellation, completion, error recovery - state machine transitions
- Kept as `_wait_seconds(20, "Waiting for device to process...")`

**External Service Sync (2 instances):**
- Spoolman API sync latency
- Kept as `_wait_seconds(10, "Waiting for Spoolman sync")`

**NFC Write Settle (3 instances):**
- Format completion, raw binary write settle time
- Kept as `_wait_seconds(2-5, "Waiting for format/write to complete")`

**Total preserved fixed waits: ~35 instances** (all intentional, not candidates for MQTT migration)

## Migration Patterns

### Pattern 1: Weight Updates

**Before:**
```python
self._ble_update_spool(spool_id, grams_remaining=1000)
self._wait_seconds(15, 'Waiting for NFC write')
spools = self._ble_list_spools()
actual = spools["current"]["grams_remaining"]
assert actual == 1000
```

**After:**
```python
self._ble_update_spool(spool_id, grams_remaining=1000)
state = self._wait_for_mqtt_remaining_weight(1000, max_wait_sec=30)

# Belt-and-suspenders BLE verification (optional during migration)
spools = self._ble_list_spools()
actual = spools["current"]["grams_remaining"]
assert actual == 1000
```

### Pattern 2: Format Defaults

**Before:**
```python
self._ble_format_spool(spool_id)
for attempt in range(12):
    spools = self._ble_list_spools()
    if spools["current"]["type"] == "PLA":
        break
    self._wait_seconds(1, f"Waiting ({attempt}/12)")
```

**After:**
```python
self._ble_format_spool(spool_id)
state = self._wait_for_mqtt_spool_update(
    condition=lambda s: s.get("material_type") == "PLA" and s.get("remaining_g") == 1000,
    reason="Waiting for formatted defaults"
)
```

### Pattern 3: Tag Present/Absent

**Before:**
```python
self._wait_seconds(5, "Waiting for tag detection")
spools = self._ble_list_spools()
assert spools.get("current") is not None
```

**After:**
```python
state = self._wait_for_mqtt_tag_present(max_wait_sec=30)
```

## Available Helper Methods

### Core Wait Method
```python
_wait_for_mqtt_spool_update(max_wait_sec=30, poll_interval_sec=1,
                            condition=None, reason="")
```
- Falls back to BLE polling if MQTT unavailable
- `condition`: callable that receives state dict, returns bool
- Returns: latest tag state dict

### Convenience Methods
```python
_wait_for_mqtt_tag_present(max_wait_sec=30)
_wait_for_mqtt_tag_absent(max_wait_sec=30)
_wait_for_mqtt_remaining_weight(expected_g, tolerance=2, max_wait_sec=30)
_wait_for_mqtt_field_match(field_name, expected_value, max_wait_sec=30)
_wait_for_mqtt_multi_field_match(field_dict, max_wait_sec=30)
```

**Examples:**
```python
# Wait for single field
state = self._wait_for_mqtt_field_match("color", "#FF0000", max_wait_sec=30)

# Wait for multiple fields simultaneously
state = self._wait_for_mqtt_multi_field_match({
    "material_type": "PETG",
    "manufacturer": "Prusament"
}, max_wait_sec=30)

# Wait for spool swap (custom condition)
state = self._wait_for_mqtt_spool_update(
    condition=lambda s: s.get("uid") != old_spool_id and s.get("present") == True,
    max_wait_sec=30,
    reason="Waiting for different spool"
)
```

### MQTT State Message Format
```json
{
  "uid": "E0040118...",
  "present": true,
  "tag_data_valid": true,
  "material_type": "PLA",
  "color": "#FF5733",
  "remaining_g": 967,
  "initial_weight_g": 1000,
  "spoolman_id": "42",
  "blank": false
}
```

## Testing

### Run Single Test
```bash
cd test/integration
python http_server.py
# Open http://localhost:8080/site/index.html
# Run desired test
```

### Run All Tests
```bash
./scripts/run_all_tests.sh
```

### Monitor MQTT Traffic
```bash
# Replace aabbcc with your device_id
mosquitto_sub -h localhost -t 'openprinttag/aabbcc/#' -v
```

## Design Decisions

**Why per-test MQTT connection?**
- Simpler lifecycle (connect in `__init__`, disconnect in `finally`)
- Test isolation (connection failures don't cascade)
- No shared state or mutex complexity

**Why fallback to BLE?**
- Works without MQTT broker
- Smooth migration path
- Reduces test fragility

**Why 1-second polling within MQTT wait?**
- Matches user preference
- Provides progress feedback
- Early timeout detection

**Why retain flag?**
- Device publishes with `retain=true` (see HomeAssistantManager)
- New subscribers get latest state immediately
- Helps with test startup sync

## Thread Safety

- MQTT callbacks run in paho-mqtt's network loop thread
- `threading.Lock()` protects `latest_state` access
- `queue.Queue()` provides thread-safe message buffering
- Test thread calls `get_latest_state()` and `wait_for_condition()`

## Troubleshooting

**"paho-mqtt not installed"**
```bash
cd test/integration
source venv/bin/activate
pip install -r requirements.txt
```

**"mqtt_config.json missing device_id"**
- Edit `test/integration/mqtt_config.json`
- Set `device_id` to last 6 hex chars of MAC
- Find via BLE config or serial output

**"MQTT connection failed: Connection refused"**
```bash
# Start MQTT broker
sudo systemctl start mosquitto
```

**Tests still use BLE polling**
- Check console for MQTT warnings
- Verify `device_id` is correct
- Verify broker is running: `mosquitto_sub -h localhost -t '#' -v`
- Check MQTT traffic: `mosquitto_sub -h localhost -t 'openprinttag/+/tag/state' -v`

**MQTT timeouts despite device being online**
- Verify topic name: `openprinttag/{device_id}/tag/state`
- Check device is publishing: monitor with `mosquitto_sub`
- Verify device has correct MQTT config (broker host/port)
- Check HomeAssistantManager initialization in serial output

## Performance Impact

**Before MQTT (fixed waits):**
- Format test: ~12 seconds (12× 1s polls)
- Weight update: ~15 seconds per update
- Print E2E: ~40+ seconds of waiting

**After MQTT (event-driven):**
- Format test: ~1-3 seconds (immediate response)
- Weight update: ~1-2 seconds per update
- Print E2E: ~15-20 seconds (only print detection wait remains)

**Speedup:** 3-5x faster for NFC write verification.
