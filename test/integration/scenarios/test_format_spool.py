"""Test 1: Format spool and verify defaults"""

from .base import BaseTestScenario


class FormatSpoolTest(BaseTestScenario):
    """
    Test spool formatting and default value initialization.

    Steps:
    1. Ensure spool is present (reuse existing if already on reader)
    2. format_spool(spool_id)
    3. Poll until formatted defaults appear
    4. Verify: type=PLA, grams_remaining=1000, color=#FFFFFF, blank=false
    """

    def _wait_for_formatted_defaults(self, spool_id, max_wait_sec=30):
        """Wait for formatted defaults via MQTT (falls back to BLE polling)"""
        def is_formatted_with_defaults(state):
            if not state.get("present"):
                return False
            return (
                state.get("blank") != True
                and state.get("material_type") == "PLA"
                and state.get("remaining_g") == 1000
                and state.get("color") == "#FFFFFF"
            )

        try:
            state = self._wait_for_mqtt_spool_update(
                max_wait_sec=max_wait_sec,
                condition=is_formatted_with_defaults,
                reason="Waiting for formatted defaults (PLA, 1000g, #FFFFFF)"
            )
            return state
        except TimeoutError:
            raise AssertionError("Timed out waiting for formatted defaults (PLA, 1000g, #FFFFFF)")

    def run(self):
        try:
            # Step 1: Ensure spool is present
            current = self._ensure_spool_present(
                "Please place a blank or formatted spool on the NFC reader"
            )
            spool_id = current["id"]

            # Step 2: Format spool
            self._emit_step("Format Spool", "running", "Writing default values to tag")
            self._ble_format_spool(spool_id)
            self._emit_step("Format Spool", "passed")

            # Step 3 + 4: Poll and verify defaults
            self._emit_step("Verify Defaults", "running", "Reading back tag data")
            state = self._wait_for_formatted_defaults(spool_id)

            # Verify with BLE (belt-and-suspenders during migration)
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "Spool disappeared")
            self._assert(current["id"] == spool_id, "Spool ID changed after format")

            self._emit_step("Verify Defaults", "passed",
                          f"PLA, 1000g, #FFFFFF confirmed")

            self.result = "passed"

        except Exception as e:
            self.error = str(e)
            self.result = "failed"
            if hasattr(self, '_current_step'):
                self._emit_step(self._current_step, "failed", str(e))

        finally:
            # Cleanup MQTT
            self._cleanup_mqtt()
