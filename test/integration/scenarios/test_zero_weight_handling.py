"""Test: Zero Weight Boundary Handling"""

from .base import BaseTestScenario


class ZeroWeightHandlingTest(BaseTestScenario):
    """
    Test behavior when spool reaches 0g.

    Steps:
    1. Configure device with mock PrusaLink
    2. Ensure formatted spool is present
    3. Set spool to 5g remaining
    4. Run print that uses 10g
    5. Verify: weight clamps to 0g (doesn't go negative)
    6. Verify: Spoolman sync handles 0g correctly (if enabled)
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
            self._emit_step("Configure Device", "passed")

            # Step 2: Ensure formatted spool is present
            spool_id = self._ensure_tag_formatted()

            # Step 3: Set spool to 5g (low weight)
            self._emit_step("Set Low Weight", "running", "Writing 5g to tag")
            self._ble_update_spool(spool_id, grams_remaining=5)
            state = self._wait_for_mqtt_remaining_weight(5, max_wait_sec=30)

            # Belt-and-suspenders verify
            current = self._get_current_spool()
            self._assert(current is not None, "Spool disappeared")
            self._assert(current.get("grams_remaining") == 5, "Failed to set 5g")
            self._emit_step("Set Low Weight", "passed", "Confirmed: 5g")

            # Step 4: Start print that uses 10g (more than available)
            self._emit_step("Start Print", "running", "Mock printer starting job (10g usage)")
            self.mock_state.set_printing(
                job_id=88,
                filament_g=10.0,  # More than the 5g available
                download_ref="/usb/SAMPLE~1.BGC"
            )
            self._emit_step("Start Print", "passed", "Job 88 active, 10g filament")

            # Wait for device to detect print
            self._wait_seconds(15, "Waiting for device to detect print")

            # Finish print
            self._emit_step("Finish Print", "running", "Mock printer completing job")
            self.mock_state.set_finished()
            self._emit_step("Finish Print", "passed", "Job 88 finished at 100%")

            # Wait for device to process and update tag
            self._wait_seconds(20, "Waiting for device to fetch metadata and update tag")
            self.mock_state.set_idle()

            # Step 5: Verify weight clamped to 0g (not negative)
            self._emit_step("Verify Zero Clamp", "running", "Reading final tag weight")

            current = self._get_current_spool()
            self._assert(current is not None, "Spool disappeared after print")

            final_weight = current.get("grams_remaining")
            self._assert(final_weight == 0,
                        f"Expected weight to clamp at 0g, got {final_weight}g")

            self._emit_step("Verify Zero Clamp", "passed",
                          f"Weight correctly clamped at 0g (5g - 10g = 0g, not negative)")

            # Step 6: Verify tag is still readable and valid
            self._emit_step("Verify Tag Integrity", "running", "Confirming tag is still valid")
            self._assert(current.get("id") == spool_id, "Spool ID changed")
            self._assert(current.get("blank") != True, "Tag marked as blank after zero weight")
            self._assert(current.get("type") is not None, "Filament type lost")
            self._assert(current.get("color") is not None, "Color lost")
            self._emit_step("Verify Tag Integrity", "passed",
                          "Tag metadata intact at 0g")

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
