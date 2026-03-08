"""Test 3: End-to-end print simulation with weight deduction"""

from .base import BaseTestScenario


class PrintE2ETest(BaseTestScenario):
    """
    Full print simulation: configure device → start print → finish → verify deduction.

    Steps:
     1. read_config → save original config
     2. write_config(prusa_link_url, prusa_link_api_key, poll_interval_ms, automation_mode)
     3. Ensure formatted spool is present (reuse existing if already on reader)
     4. update_spool(id, grams_remaining=1000), wait 3s, verify 1000g
     5. MockPrusalinkState.set_printing(job_id=42, filament_g=9.18, download_ref)
     6. Wait 15s (3× poll interval for detection)
     7. MockPrusalinkState.set_finished()
     8. Wait 20s (poll + deferred fetch + NFC write)
     9. MockPrusalinkState.set_idle()
    10. list_spools → verify grams_remaining ≈ 991 (1000 - 9.18, ±2g tolerance)
    11. Restore original config (in finally block)
    """

    def run(self):
        original_config = None
        spool_id = None

        try:
            # Step 1: Read and save original config
            self._emit_step("Save Config", "running", "Reading current device configuration")
            original_config = self._ble_read_config()
            self._emit_step("Save Config", "passed",
                          f"Saved: {original_config.get('prusa_link_url', 'N/A')}")

            # Step 2: Get server URL from browser and configure device
            self._emit_step("Configure Device", "running", "Applying mock PrusaLink settings")
            server_url = self._get_server_url()
            self._ble_write_config(
                prusa_link_url=server_url,
                prusa_link_api_key=self.mock_state.api_key,
                poll_interval_ms=5000,
                automation_mode=0
            )
            self._emit_step("Configure Device", "passed", f"Device now polling {server_url}")

            # Step 3: Ensure formatted spool is present
            spool_id = self._ensure_tag_formatted()

            # Step 4: Set initial weight to 1000g
            self._emit_step("Set Initial Weight", "running", "Writing 1000g to tag")
            self._ble_update_spool(spool_id, grams_remaining=1000)

            # Wait for MQTT update (falls back to BLE polling if MQTT unavailable)
            state = self._wait_for_mqtt_remaining_weight(1000, max_wait_sec=30)

            # Verify with BLE (belt-and-suspenders during migration)
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "Spool disappeared")
            actual = current.get("grams_remaining")
            self._assert(actual == 1000, f"Expected 1000g, got {actual}g")
            self._emit_step("Set Initial Weight", "passed", "Confirmed: 1000g")

            # Step 5: Start mock print job
            self._emit_step("Start Print", "running", "Mock printer starting job")
            self.mock_state.set_printing(
                job_id=42,
                filament_g=9.18,
                download_ref="/usb/SAMPLE~1.BGC"
            )
            self._emit_step("Start Print", "passed", "Job 42 active, 9.18g filament")

            # Step 6: Wait for device to detect print (no tag state change, use fixed wait)
            self._wait_seconds(15, "Waiting for device to detect print (3× poll interval)")

            # Step 7: Finish print
            self._emit_step("Finish Print", "running", "Mock printer completing job")
            self.mock_state.set_finished()
            self._emit_step("Finish Print", "passed", "Job 42 finished at 100%")

            # Step 8: Wait for device to process completion and update tag
            # Use MQTT to wait for weight update
            expected_final = int(1000 - 9.18)  # ~991g
            try:
                state = self._wait_for_mqtt_remaining_weight(expected_final, tolerance=2, max_wait_sec=30)
                self.orchestrator.push_sse_event("info", {
                    "message": f"Weight update detected via MQTT: {state.get('remaining_g')}g"
                })
            except TimeoutError:
                # Fall back to fixed wait if MQTT times out
                self._wait_seconds(10, "MQTT timeout - waiting additional 10s for deduction")

            # Step 9: Return mock to idle
            self._emit_step("Reset Printer", "running", "Mock printer returning to idle")
            self.mock_state.set_idle()
            self._emit_step("Reset Printer", "passed")

            # Step 10: Verify weight deduction
            self._emit_step("Verify Deduction", "running", "Reading final tag weight")
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "Spool disappeared")

            final_weight = current.get("grams_remaining")
            expected = 1000 - 9.18  # 990.82, device casts to int = 990 or 991
            self._assert_approx(final_weight, expected, 2, "grams_remaining")

            self._emit_step("Verify Deduction", "passed",
                          f"Final weight: {final_weight}g (expected ~{int(expected)}g)")

            self.result = "passed"

        except Exception as e:
            self.error = str(e)
            self.result = "failed"
            # Don't emit step failure here - let finally block restore config

        finally:
            # Step 11: Restore original config
            if original_config:
                try:
                    self._emit_step("Restore Config", "running", "Restoring original settings")
                    self._ble_write_config(
                        prusa_link_url=original_config.get("prusa_link_url", ""),
                        prusa_link_api_key=original_config.get("prusa_link_api_key", ""),
                        poll_interval_ms=original_config.get("poll_interval_ms", 30000),
                        automation_mode=original_config.get("automation_mode", 0)
                    )
                    self._emit_step("Restore Config", "passed",
                                  f"Restored: {original_config.get('prusa_link_url', 'N/A')}")
                except Exception as restore_error:
                    self.orchestrator.push_sse_event("warning", {
                        "message": f"Failed to restore config: {restore_error}"
                    })

            # Cleanup MQTT
            self._cleanup_mqtt()

            # Reset mock state
            self.mock_state.set_idle()
