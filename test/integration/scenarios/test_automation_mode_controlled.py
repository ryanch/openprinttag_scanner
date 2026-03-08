"""Test: Home Assistant Controlled Mode"""

from .base import BaseTestScenario


class AutomationModeControlledTest(BaseTestScenario):
    """
    Test automation_mode=1 (CONTROLLED_BY_HOME_ASSISTANT).

    Steps:
    1. Set automation_mode=1
    2. Ensure formatted spool is present
    3. Set initial weight to 1000g
    4. Run print to completion (9.18g usage)
    5. Verify: device does NOT auto-deduct weight
    6. Wait 10 seconds
    7. Change color of filament
    8. Confirm weight stays the same when read back (still 1000g)
    """

    def run(self):
        original_config = None
        spool_id = None

        try:
            # Step 1: Save and configure device with automation_mode=1
            self._emit_step("Save Config", "running", "Reading current device configuration")
            original_config = self._ble_read_config()
            self._emit_step("Save Config", "passed")

            self._emit_step("Configure Device", "running",
                          "Setting automation_mode=1 (CONTROLLED_BY_HOME_ASSISTANT)")
            server_url = self._get_server_url()
            self._ble_write_config(
                prusa_link_url=server_url,
                prusa_link_api_key=self.mock_state.api_key,
                poll_interval_ms=5000,
                automation_mode=1  # CONTROLLED_BY_HOME_ASSISTANT
            )
            self._emit_step("Configure Device", "passed",
                          "Device in HA-controlled mode (no auto-deduction)")

            # Step 2: Ensure formatted spool is present
            spool_id = self._ensure_tag_formatted()

            # Step 3: Set initial weight to 1000g
            self._emit_step("Set Initial Weight", "running", "Writing 1000g to tag")
            self._ble_update_spool(spool_id, grams_remaining=1000)
            state = self._wait_for_mqtt_remaining_weight(1000, max_wait_sec=30)

            # Belt-and-suspenders verify
            current = self._get_current_spool()
            self._assert(current is not None, "Spool disappeared")
            self._assert(current.get("grams_remaining") == 1000, "Failed to set 1000g")
            self._emit_step("Set Initial Weight", "passed", "Confirmed: 1000g")

            # Step 4: Run print to completion
            self._emit_step("Start Print", "running", "Mock printer starting job (9.18g)")
            self.mock_state.set_printing(
                job_id=200,
                filament_g=9.18,
                download_ref="/usb/SAMPLE~1.BGC"
            )
            self._emit_step("Start Print", "passed", "Job 200 active")

            self._wait_seconds(15, "Waiting for device to detect print")

            self._emit_step("Finish Print", "running", "Mock printer completing job")
            self.mock_state.set_finished()
            self._emit_step("Finish Print", "passed", "Job 200 finished at 100%")

            # Step 5: Wait and verify NO auto-deduction occurred
            self._wait_seconds(20, "Waiting (device should NOT auto-deduct in mode 1)")
            self.mock_state.set_idle()

            self._emit_step("Verify No Auto-Deduct", "running", "Reading tag weight")
            current = self._get_current_spool()
            self._assert(current is not None, "Spool disappeared")

            weight_after_print = current.get("grams_remaining")
            self._assert(weight_after_print == 1000,
                        f"Weight should still be 1000g (no auto-deduct), but got {weight_after_print}g")
            self._emit_step("Verify No Auto-Deduct", "passed",
                          f"Correctly skipped auto-deduction: {weight_after_print}g (unchanged)")

            # Step 6: Wait 10 seconds
            self._wait_seconds(10, "Waiting 10 seconds before color change")

            # Step 7: Change color of filament (no weight change, keep fixed wait)
            self._emit_step("Change Color", "running", "Updating color to #FF00FF")
            self._ble_update_spool(spool_id, color="#FF00FF")
            self._wait_seconds(5, 'Waiting for NFC write')
            self._emit_step("Change Color", "passed", "Color updated to #FF00FF")

            # Step 8: Confirm weight unchanged
            self._emit_step("Verify Weight Unchanged", "running", "Reading tag weight again")
            current = self._get_current_spool()
            self._assert(current is not None, "Spool disappeared")

            final_weight = current.get("grams_remaining")
            final_color = current.get("color")

            self._assert(final_weight == 1000,
                        f"Weight should still be 1000g, but got {final_weight}g")
            self._assert(final_color == "#FF00FF",
                        f"Color should be #FF00FF, but got {final_color}")

            self._emit_step("Verify Weight Unchanged", "passed",
                          f"Weight unchanged at 1000g after color update (mode 1 confirmed)")

            self.result = "passed"

        except Exception as e:
            self.error = str(e)
            self.result = "failed"

        finally:
            # Restore config (including automation_mode)
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
                                  f"Restored automation_mode={original_config.get('automation_mode', 0)}")
                except Exception as restore_error:
                    self.orchestrator.push_sse_event("warning", {
                        "message": f"Failed to restore config: {restore_error}"
                    })

            # Reset mock state
            self.mock_state.set_idle()
