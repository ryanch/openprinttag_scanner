"""Test: Mid-Print Spool Swap"""

from .base import BaseTestScenario


class SpoolSwapDuringPrintTest(BaseTestScenario):
    """
    Critical edge case: what happens if user swaps spool while print is active?

    Steps:
    1. Configure device with mock PrusaLink
    2. Ensure formatted spool A is present
    3. Set spool A to 1000g
    4. Start print job (9.18g filament)
    5. Wait for device to detect print
    6. Mid-print: Ask user to swap to spool B
    7. Finish print
    8. Verify: no auto-deduction occurs (spool identity is ambiguous after swap)
    """

    def run(self):
        original_config = None

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
            self._emit_step("Configure Device", "passed")

            # Step 2: Ensure formatted spool A is present
            spool_a = self._ensure_spool_present(
                "Please place formatted spool A on the NFC reader",
                step_name="Place Spool A",
                require_formatted=True
            )
            spool_a_id = spool_a["id"]

            # Step 3: Set spool A to 1000g
            self._emit_step("Set Spool A Weight", "running", "Writing 1000g to tag")
            self._ble_update_spool(spool_a_id, grams_remaining=1000)
            self._wait_seconds(5, 'Waiting for NFC write')

            current = self._get_current_spool()
            self._assert(current is not None, "Spool A disappeared")
            self._assert(current.get("grams_remaining") == 1000, "Failed to set 1000g")
            self._emit_step("Set Spool A Weight", "passed", "Confirmed: 1000g")

            # Step 4: Start print
            self._emit_step("Start Print", "running", "Mock printer starting job")
            self.mock_state.set_printing(
                job_id=99,
                filament_g=9.18,
                download_ref="/usb/SAMPLE~1.BGC"
            )
            self._emit_step("Start Print", "passed", "Job 99 active")

            # Step 5: Wait for device to detect print
            self._wait_seconds(15, "Waiting for device to detect print")

            # Step 6: Mid-print spool swap
            self._emit_step("Swap to Spool B", "running", "Waiting for user to swap spools")
            self._request_user_action(
                "WHILE PRINT IS RUNNING: Remove spool A and place a different formatted spool B on the reader"
            )

            self._wait_for_mqtt_spool_update(
                condition=lambda s: s.get("uid") != spool_a_id and s.get("present") == True,
                max_wait_sec=30,
                reason="Waiting for different spool"
            )

            # Belt-and-suspenders: verify via BLE
            spools = self._ble_list_spools()
            spool_b = spools.get("current")
            self._assert(spool_b is not None, "No spool detected via BLE after swap")
            spool_b_id = spool_b.get("id")
            self._assert(spool_b_id is not None, "Spool B missing id")
            self._assert(spool_b_id != spool_a_id, "Spool B must be different from spool A")

            # Set spool B to a known baseline so we can verify no deduction
            self._ble_update_spool(spool_b_id, grams_remaining=500)
            self._wait_seconds(3, "Setting spool B to 500g")
            self._emit_step("Swap to Spool B", "passed", f"Detected: {spool_b_id} at 500g")

            # Step 7: Finish print
            self._emit_step("Finish Print", "running", "Mock printer completing job")
            self.mock_state.set_finished()
            self._emit_step("Finish Print", "passed", "Job 99 finished at 100%")

            # Step 8: Wait for device to process and update tag
            self._wait_seconds(20, "Waiting for device to fetch metadata and update tag")
            self.mock_state.set_idle()

            # Step 9: Verify no deduction was applied after mid-print spool swap
            self._emit_step("Verify No Deduction", "running", "Reading final tag weight")

            current = self._get_current_spool()
            self._assert(current is not None, "No spool detected after print")

            current_id = current.get("id")
            final_weight = current.get("grams_remaining")

            # Verify it's spool B
            self._assert(current_id == spool_b_id,
                        f"Expected spool B ({spool_b_id}), but found {current_id}")

            # Spool changed during print, so application should not update any spool weight.
            self._assert(final_weight == 500,
                        f"Expected spool B weight to remain 500g, but got {final_weight}g")

            self._emit_step("Verify No Deduction", "passed",
                          f"No deduction applied after swap: spool B remains {final_weight}g")

            self.result = "passed"

        except Exception as e:
            self.error = str(e)
            self.result = "failed"

        finally:
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
