"""Test: Job disappeared (204) triggers bgcode fallback for filament deduction"""

from .base import BaseTestScenario


class JobDisappearedDeductionTest(BaseTestScenario):
    """
    When PrusaLink job API returns 204 (job gone), device should fall back to
    bgcode file fetch for filament data instead of reporting 0g.

    Steps:
     1. read_config -> save original config
     2. write_config(prusa_link_url, prusa_link_api_key, poll_interval_ms=5000, automation_mode=0)
     3. Ensure formatted spool is present
     4. update_spool(id, grams_remaining=1000), verify 1000g
     5. set_printing(job_id=42, filament_g=9.18, download_ref="/usb/SAMPLE~1.BGC", progress_percent=98.0)
     6. Wait 15s for device to detect print
     7. set_idle() - job disappears, API returns 204
     8. Wait 40s - grace polls + deferred bgcode fetch + NFC write
     9. Verify grams_remaining ~ 991g (1000 - 9.18, +/-2g)
    10. Restore original config
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

            # Step 2: Configure device to point at mock server
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

            # Belt-and-suspenders verify
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "Spool disappeared")
            actual = current.get("grams_remaining")
            self._assert(actual == 1000, f"Expected 1000g, got {actual}g")
            self._emit_step("Set Initial Weight", "passed", "Confirmed: 1000g")

            # Step 5: Start mock print at 98% progress
            self._emit_step("Start Print", "running", "Mock printer starting job at 98%")
            self.mock_state.set_printing(
                job_id=42,
                filament_g=9.18,
                download_ref="/usb/SAMPLE~1.BGC",
                progress_percent=98.0
            )
            self._emit_step("Start Print", "passed", "Job 42 active at 98%, 9.18g filament")

            # Step 6: Wait for device to detect print
            self._wait_seconds(15, "Waiting for device to detect print (3x poll interval)")

            # Step 7: Job disappears (set_idle returns None from job API -> 204)
            self._emit_step("Job Disappears", "running", "Mock printer job vanishes (204)")
            self.mock_state.set_idle()
            self._emit_step("Job Disappears", "passed", "Job API now returns 204")

            # Step 8: Wait for grace polls + deferred bgcode fetch + NFC write
            self._wait_seconds(40, "Waiting for grace polls + bgcode fetch + NFC write")

            # Step 9: Verify weight deduction
            self._emit_step("Verify Deduction", "running", "Reading final tag weight")
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "Spool disappeared after job end")

            final_weight = current.get("grams_remaining")
            # 98% >= 95% threshold, so progress rounds to 100%
            # Deduction: 100% * 9.18g = 9.18g, final = 1000 - 9.18 = 990.82
            expected = 1000 - 9.18
            self._assert_approx(final_weight, expected, 2, "grams_remaining")

            self._emit_step("Verify Deduction", "passed",
                          f"Final weight: {final_weight}g (expected ~{int(expected)}g)")

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
                    self._emit_step("Restore Config", "passed",
                                  f"Restored: {original_config.get('prusa_link_url', 'N/A')}")
                except Exception as restore_error:
                    self.orchestrator.push_sse_event("warning", {
                        "message": f"Failed to restore config: {restore_error}"
                    })

            # Reset mock state
            self.mock_state.set_idle()
