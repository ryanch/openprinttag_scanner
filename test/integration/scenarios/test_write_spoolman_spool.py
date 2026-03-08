"""Write Spoolman spool integration test"""

import json
from scenarios.base import BaseTestScenario


class WriteSpoolmanSpoolTest(BaseTestScenario):
    """
    Test Spoolman integration: get_spoolman_spool and write_spoolman_spool (Mode B).

    Tests:
    1. get_spoolman_spool: Fetch spool data from Spoolman API
    2. write_spoolman_spool Mode B: Write with all 5 fields passed directly

    Test flow:
    1. Configure device with Spoolman URL
    2. Setup mock Spoolman data (vendor, filament, spool)
    3. Test get_spoolman_spool command to verify API fetch works
    4. Prepare NFC tag (format if blank)
    5. Write spool data using Mode B (all fields passed directly)
    6. Verify tag contents match written data
    7. Test weight decrement to verify normal operation
    """

    def run(self):
        try:
            self._emit_step("Test Start", "running", "Starting write_spoolman_spool (Mode B) test")

            # Step 1: Save original config
            self._emit_step("Save Config", "running", "Reading device configuration")
            original_config = self._ble_read_config()
            self._emit_step("Save Config", "passed", "Original config saved")

            # Step 2: Configure Spoolman URL
            self._emit_step("Configure Spoolman", "running", "Getting server URL")
            server_url = self._get_server_url()
            self._emit_step("Configure Spoolman", "running", f"Writing Spoolman URL: {server_url}")

            config_to_write = original_config.copy()
            config_to_write["spoolman_url"] = server_url
            self._ble_write_config(**config_to_write)
            self._emit_step("Configure Spoolman", "passed", "Spoolman URL configured")

            # Step 3: Setup mock Spoolman data
            self._emit_step("Setup Mock Data", "running", "Creating vendor, filament, and spool")
            self._setup_mock_spoolman_data()
            self._emit_step("Setup Mock Data", "passed", "Created test spool in Spoolman API")

            # Step 4: Test get_spoolman_spool command
            self._emit_step("Test Get Spool", "running", "Fetching spool 1 from Spoolman API")
            spool_data = self._ble_get_spoolman_spool(1)
            self._assert(spool_data is not None, "Failed to get spool data from Spoolman")
            self._assert(spool_data.get("material") == "PLA",
                        f"Expected material=PLA, got {spool_data.get('material')}")
            self._assert(spool_data.get("vendor_name") == "TestVendor",
                        f"Expected vendor_name=TestVendor, got {spool_data.get('vendor_name')}")
            self._emit_step("Test Get Spool", "passed",
                          f"Successfully fetched: {spool_data.get('material')} by {spool_data.get('vendor_name')}")

            # Step 5: Ensure tag is freshly formatted with clean state
            spool_id = self._ensure_tag_formatted(step_name="Prepare Tag")

            # Step 6: Write spool using Mode B (direct data - all fields)
            self._emit_step("Write Mode B", "running", "Writing spool using Mode B (all 5 fields)")
            self._ble_write_spoolman_spool_mode_b(
                spoolman_id=42,
                type="PETG",
                color="#3498DB",
                manufacturer="TestVendor",
                initial_weight=750,
                grams_remaining=600
            )
            state = self._wait_for_mqtt_remaining_weight(600, tolerance=5, max_wait_sec=30)
            self._emit_step("Write Mode B", "passed", "Mode B write command completed")

            # Step 7: Verify tag contents
            self._emit_step("Verify Write", "running", "Reading tag to verify Mode B write")
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "No spool detected after Mode B write")

            # Verify all fields
            self._assert(current.get("type") == "PETG",
                        f"Expected type=PETG, got {current.get('type')}")
            self._assert(current.get("manufacturer") == "TestVendor",
                        f"Expected manufacturer=TestVendor, got {current.get('manufacturer')}")

            # Verify color (case-insensitive, with #)
            actual_color = current.get("color", "").upper()
            expected_color = "#3498DB"
            self._assert(actual_color == expected_color,
                        f"Expected color={expected_color}, got {actual_color}")

            # Verify weight
            self._assert_approx(current.get("grams_remaining", 0), 600, 5, "grams_remaining")

            # Verify spoolman_id
            self._assert(current.get("spoolman_id") == 42,
                        f"Expected spoolman_id=42, got {current.get('spoolman_id')}")

            self._emit_step("Verify Write", "passed",
                          f"Verified: PETG, TestVendor, #3498DB, 600g, spoolman_id=42")

            # Step 8: Decrement weight (test normal operation after Mode B write)
            self._emit_step("Test Decrement", "running", "Decrementing weight to 500g")
            spool_id = current["id"]
            self._ble_update_spool(spool_id, grams_remaining=500)
            state = self._wait_for_mqtt_remaining_weight(500, tolerance=5, max_wait_sec=30)

            # Belt-and-suspenders verify
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert_approx(current.get("grams_remaining", 0), 500, 5, "grams_remaining after decrement")
            self._emit_step("Test Decrement", "passed", "Weight updated to 500g")

            # Success
            self.result = "passed"
            self._emit_step("Test Complete", "passed", "All steps passed")

        except AssertionError as e:
            self.result = "failed"
            self.error = str(e)
            self._emit_step("Test Failed", "failed", str(e))
        except Exception as e:
            self.result = "failed"
            self.error = str(e)
            self._emit_step("Test Error", "failed", str(e))
        finally:
            # Restore config
            try:
                self._emit_step("Restore Config", "running", "Restoring original configuration")
                self._ble_write_config(**original_config)
                self._emit_step("Restore Config", "passed", "Config restored")
            except Exception as e:
                self._emit_step("Restore Config", "failed", f"Failed to restore config: {e}")

    def _setup_mock_spoolman_data(self):
        """Create vendor, filament, and spool in mock Spoolman API"""
        vendor = self.mock_spoolman.create_vendor("TestVendor")
        filament = self.mock_spoolman.create_filament(
            vendor_id=vendor["id"],
            material="PLA",
            density=1.24,
            diameter=1.75,
            weight=1000,
            color_hex="#FF5733"  # Coral red
        )
        self.mock_spoolman.create_spool(
            filament_id=filament["id"],
            remaining_weight=800,
            initial_weight=1000,
            extra={}
        )

    def _ble_get_spoolman_spool(self, spoolman_id):
        """Get spool details from Spoolman via BLE"""
        cmd = {"command": "get_spoolman_spool", "spoolman_id": spoolman_id}
        data = self._ble_command(cmd, read_data=True)
        return json.loads(data) if data else None

    def _ble_write_spoolman_spool_mode_b(self, spoolman_id, type, color,
                                          manufacturer, initial_weight, grams_remaining):
        """Write spool using Mode B (direct data - all 5 fields)"""
        cmd = {
            "command": "write_spoolman_spool",
            "spoolman_id": spoolman_id,
            "type": type,
            "color": color,
            "manufacturer": manufacturer,
            "initial_weight": initial_weight,
            "grams_remaining": grams_remaining
        }
        self._ble_command(cmd, read_data=False)
