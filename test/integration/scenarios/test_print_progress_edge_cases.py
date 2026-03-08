"""Test: Print Progress Edge Cases"""

from .base import BaseTestScenario


class PrintProgressEdgeCasesTest(BaseTestScenario):
    """
    Test unusual progress values.

    Steps:
    1. Configure device
    2. Test A: Print at 0% → cancel (verify 0g deduction)
    3. Test B: Print at 100% → finish (verify full deduction)
    4. Test C: Print at 100% → stays 100% for 30s → finish (verify doesn't double-count)
    """

    def run(self):
        original_config = None
        spool_id = None

        try:
            # Setup
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

            # Ensure spool
            spool_id = self._ensure_tag_formatted()

            # TEST A: Cancel at 0% progress
            self._emit_step("Test A: Cancel at 0%", "running", "Setting weight to 1000g")
            self._ble_update_spool(spool_id, grams_remaining=1000)
            state = self._wait_for_mqtt_remaining_weight(1000, max_wait_sec=30)

            self._emit_step("Test A: Cancel at 0%", "running", "Starting print at 0%")
            self.mock_state.set_printing(
                job_id=100,
                filament_g=20.0,
                download_ref="/usb/SAMPLE~1.BGC",
                progress_percent=0.0  # 0% progress
            )
            self._wait_seconds(10, "Waiting for device to detect print at 0%")

            # Cancel immediately
            self.mock_state.set_canceled()
            self._emit_step("Test A: Cancel at 0%", "running", "Canceled at 0%")
            self._wait_seconds(15, "Waiting for device to process cancellation")
            self.mock_state.set_idle()

            # Verify no deduction (or minimal deduction)
            current = self._get_current_spool()
            self._assert(current is not None, "Spool disappeared")
            weight_after_cancel = current.get("grams_remaining")
            self._assert_approx(weight_after_cancel, 1000, 2, "weight after 0% cancel")
            self._emit_step("Test A: Cancel at 0%", "passed",
                          f"No deduction at 0% cancel: {weight_after_cancel}g")

            # TEST B: Finish at 100%
            self._emit_step("Test B: Finish at 100%", "running", "Setting weight to 900g")
            self._ble_update_spool(spool_id, grams_remaining=900)
            state = self._wait_for_mqtt_remaining_weight(900, max_wait_sec=30)

            self._emit_step("Test B: Finish at 100%", "running", "Starting print")
            self.mock_state.set_printing(
                job_id=101,
                filament_g=15.0,
                download_ref="/usb/SAMPLE~1.BGC",
                progress_percent=0.0
            )
            self._wait_seconds(10, "Waiting for device to detect print")

            # Jump to 100% and finish
            self.mock_state.set_printing(
                job_id=101,
                filament_g=15.0,
                download_ref="/usb/SAMPLE~1.BGC",
                progress_percent=100.0  # 100% progress
            )
            self.mock_state.set_finished()
            self._emit_step("Test B: Finish at 100%", "running", "Finished at 100%")
            self._wait_seconds(20, "Waiting for device to process completion")
            self.mock_state.set_idle()

            # Verify full deduction
            current = self._get_current_spool()
            self._assert(current is not None, "Spool disappeared")
            expected_b = 900 - 15  # 885g
            weight_after_100 = current.get("grams_remaining")
            self._assert_approx(weight_after_100, expected_b, 2, "weight after 100% finish")
            self._emit_step("Test B: Finish at 100%", "passed",
                          f"Full deduction: {weight_after_100}g (expected ~{int(expected_b)}g)")

            # TEST C: Stay at 100% for 30s before finishing (no double-count)
            self._emit_step("Test C: 100% Dwell", "running", "Setting weight to 800g")
            self._ble_update_spool(spool_id, grams_remaining=800)
            state = self._wait_for_mqtt_remaining_weight(800, max_wait_sec=30)

            self._emit_step("Test C: 100% Dwell", "running", "Starting print")
            self.mock_state.set_printing(
                job_id=102,
                filament_g=12.0,
                download_ref="/usb/SAMPLE~1.BGC",
                progress_percent=0.0
            )
            self._wait_seconds(10, "Waiting for device to detect print")

            # Set to 100% and hold
            self.mock_state.set_printing(
                job_id=102,
                filament_g=12.0,
                download_ref="/usb/SAMPLE~1.BGC",
                progress_percent=100.0
            )
            self._emit_step("Test C: 100% Dwell", "running", "Holding at 100% for 30s")
            self._wait_seconds(30, "Device polling while at 100% (should not double-count)")

            # Now finish
            self.mock_state.set_finished()
            self._wait_seconds(20, "Waiting for device to process completion")
            self.mock_state.set_idle()

            # Verify only deducted once
            current = self._get_current_spool()
            self._assert(current is not None, "Spool disappeared")
            expected_c = 800 - 12  # 788g
            weight_after_dwell = current.get("grams_remaining")
            self._assert_approx(weight_after_dwell, expected_c, 2, "weight after 100% dwell")
            self._emit_step("Test C: 100% Dwell", "passed",
                          f"No double-count: {weight_after_dwell}g (expected ~{int(expected_c)}g)")

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
