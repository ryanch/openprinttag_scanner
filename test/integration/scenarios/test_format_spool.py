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

    def _wait_for_formatted_defaults(self, spool_id, max_attempts=12):
        for attempt in range(1, max_attempts + 1):
            spools = self._ble_list_spools()
            current = spools.get("current")
            if current is not None and current.get("id") == spool_id:
                if (
                    current.get("blank") != True
                    and current.get("type") == "PLA"
                    and current.get("grams_remaining") == 1000
                    and current.get("color") == "#FFFFFF"
                ):
                    return current
            self._wait_seconds(1, f"Waiting for formatted defaults ({attempt}/{max_attempts})")

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
            current = self._wait_for_formatted_defaults(spool_id)
            self._assert(current["id"] == spool_id, "Spool ID changed after format")

            self._emit_step("Verify Defaults", "passed",
                          f"PLA, 1000g, #FFFFFF confirmed")

            self.result = "passed"

        except Exception as e:
            self.error = str(e)
            self.result = "failed"
            if hasattr(self, '_current_step'):
                self._emit_step(self._current_step, "failed", str(e))
