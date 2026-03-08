"""Test 4: End-to-end canceled print at 30% progress"""

from .base import BaseTestScenario


class Print30PercentE2ETest(BaseTestScenario):
    """
    Canceled print simulation:
    - Start print with known total filament
    - Cancel/disappear at 30%
    - Verify only 30% of filament is deducted
    """

    def run(self):
        original_config = None

        try:
            # Step 1: Read and save original config
            self._emit_step("Save Config", "running", "Reading current device configuration")
            original_config = self._ble_read_config()
            self._emit_step(
                "Save Config",
                "passed",
                f"Saved: {original_config.get('prusa_link_url', 'N/A')}"
            )

            # Step 2: Configure device to use mock PrusaLink
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
            state = self._wait_for_mqtt_remaining_weight(1000, max_wait_sec=30)

            # Belt-and-suspenders: verify via BLE
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "Spool disappeared")
            actual = current.get("grams_remaining")
            self._assert(actual == 1000, f"Expected 1000g, got {actual}g")
            self._emit_step("Set Initial Weight", "passed", "Confirmed: 1000g")

            # Step 5: Start mock print job at 30% progress
            self._emit_step("Start Print", "running", "Mock printer starting job at 30%")
            self.mock_state.set_printing(
                job_id=43,
                filament_g=9.18,
                download_ref="/usb/SAMPLE~1.BGC",
                progress_percent=30.0
            )
            self._emit_step("Start Print", "passed", "Job 43 active at 30.0%")

            # Step 6: Wait for device to detect/track the job
            self._wait_seconds(15, "Waiting for device to detect print (3x poll interval)")

            # Step 7: Simulate cancellation by removing active job from API
            self._emit_step("Cancel Print", "running", "Mock printer canceled at 30%")
            self.mock_state.set_idle()
            self._emit_step("Cancel Print", "passed", "Job disappeared at 30%")

            # Step 8: Wait for device to process canceled job and update tag
            self._wait_seconds(20, "Waiting for device to process cancel and update tag")

            # Step 9: Verify partial deduction (30% of total)
            self._emit_step("Verify Deduction", "running", "Reading final tag weight")
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "Spool disappeared")

            final_weight = current.get("grams_remaining")
            expected_used = 9.18 * 0.30
            expected_remaining = 1000 - expected_used
            self._assert_approx(final_weight, expected_remaining, 2, "grams_remaining")
            self._emit_step(
                "Verify Deduction",
                "passed",
                f"Final weight: {final_weight}g (expected ~{int(expected_remaining)}g)"
            )

            self.result = "passed"

        except Exception as e:
            self.error = str(e)
            self.result = "failed"

        finally:
            # Restore original config
            if original_config:
                try:
                    self._emit_step("Restore Config", "running", "Restoring original settings")
                    self._ble_write_config(
                        prusa_link_url=original_config.get("prusa_link_url", ""),
                        prusa_link_api_key=original_config.get("prusa_link_api_key", ""),
                        poll_interval_ms=original_config.get("poll_interval_ms", 30000),
                        automation_mode=original_config.get("automation_mode", 0)
                    )
                    self._emit_step(
                        "Restore Config",
                        "passed",
                        f"Restored: {original_config.get('prusa_link_url', 'N/A')}"
                    )
                except Exception as restore_error:
                    self.orchestrator.push_sse_event("warning", {
                        "message": f"Failed to restore config: {restore_error}"
                    })

            # Reset mock state
            self.mock_state.set_idle()
