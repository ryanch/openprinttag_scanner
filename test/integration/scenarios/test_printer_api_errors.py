"""Test: PrusaLink API Error Handling"""

from .base import BaseTestScenario


class PrinterAPIErrorsTest(BaseTestScenario):
    """
    Test resilience to printer API failures.

    Steps:
    1. Configure device to poll mock printer
    2. Mock returns 404/500 errors
    3. Verify: device continues polling, doesn't crash
    4. Mock returns valid response
    5. Verify: device recovers and tracks print normally
    """

    def run(self):
        original_config = None
        spool_id = None

        try:
            # Step 1: Save and configure device
            self._emit_step("Save Config", "running", "Reading current device configuration")
            original_config = self._ble_read_config()
            self._emit_step("Save Config", "passed")

            self._emit_step("Configure Device", "running", "Applying mock PrusaLink settings")
            server_url = self._get_server_url()
            self._ble_write_config(
                prusa_link_url=server_url,
                prusa_link_api_key=self.mock_state.api_key,
                poll_interval_ms=5000,
                automation_mode=0
            )
            self._emit_step("Configure Device", "passed", f"Device polling {server_url}")

            # Step 2: Set mock to return errors
            self._emit_step("Inject API Errors", "running", "Mock will return 500 errors")
            self.mock_state.set_error_mode(True)
            self._emit_step("Inject API Errors", "passed", "Error mode enabled")

            # Wait for device to poll and encounter errors
            self._wait_seconds(15, "Device polling (should encounter errors)")

            # Step 3: Verify device hasn't crashed (can still respond to BLE)
            self._emit_step("Verify Device Health", "running", "Testing BLE connectivity")
            try:
                config = self._ble_read_config()
                self._assert(config is not None, "BLE communication failed")
                self._emit_step("Verify Device Health", "passed",
                              "Device still responsive after API errors")
            except Exception as e:
                raise AssertionError(f"Device unresponsive after API errors: {e}")

            # Step 4: Restore normal API responses
            self._emit_step("Restore API", "running", "Mock returning to normal operation")
            self.mock_state.set_error_mode(False)
            self.mock_state.set_idle()
            self._emit_step("Restore API", "passed", "API now returning valid responses")

            # Step 5: Verify device recovers and can track a print normally
            # Ensure spool is present
            spool_id = self._ensure_tag_formatted()

            # Set known weight
            self._emit_step("Set Weight", "running", "Writing 1000g to tag")
            self._ble_update_spool(spool_id, grams_remaining=1000)
            state = self._wait_for_mqtt_remaining_weight(1000, max_wait_sec=30)

            # Belt-and-suspenders verify
            current = self._get_current_spool()
            self._assert(current.get("grams_remaining") == 1000, "Failed to set 1000g")
            self._emit_step("Set Weight", "passed", "Confirmed: 1000g")

            # Start and complete a print
            self._emit_step("Test Recovery Print", "running", "Starting print job after recovery")
            self.mock_state.set_printing(
                job_id=77,
                filament_g=8.5,
                download_ref="/usb/SAMPLE~1.BGC"
            )
            self._wait_seconds(15, "Waiting for device to detect print")

            self.mock_state.set_finished()
            self._emit_step("Test Recovery Print", "passed", "Print detected and completed")

            self._wait_seconds(20, "Waiting for device to process completion")
            self.mock_state.set_idle()

            # Verify deduction worked
            self._emit_step("Verify Recovery", "running", "Checking if deduction worked")
            current = self._get_current_spool()
            self._assert(current is not None, "Spool disappeared")

            final_weight = current.get("grams_remaining")
            expected = 1000 - 8.5  # 991.5
            self._assert_approx(final_weight, expected, 2, "grams_remaining")

            self._emit_step("Verify Recovery", "passed",
                          f"Device fully recovered: {final_weight}g (expected ~{int(expected)}g)")

            self.result = "passed"

        except Exception as e:
            self.error = str(e)
            self.result = "failed"

        finally:
            # Ensure error mode is disabled
            self.mock_state.set_error_mode(False)

            # Restore config
            if original_config:
                try:
                    self._emit_step("Restore Config", "running", "Restoring original settings")
                    self._ble_write_config(
                        prusa_link_url=original_config.get("prusa_link_url", ""),
                        prusa_link_api_key=original_config.get("prusa_link_api_key", ""),
                        poll_interval_ms=original_config.get("poll_interval_ms", 30000),
                        automation_mode=original_config.get("automation_mode", 0)
                    )
                    self._emit_step("Restore Config", "passed")
                except Exception as restore_error:
                    self.orchestrator.push_sse_event("warning", {
                        "message": f"Failed to restore config: {restore_error}"
                    })

            # Reset mock state
            self.mock_state.set_idle()
