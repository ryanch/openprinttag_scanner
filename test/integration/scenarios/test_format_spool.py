"""Test 1: Format spool and verify defaults"""

from .base import BaseTestScenario


class FormatSpoolTest(BaseTestScenario):
    """
    Test spool formatting and default value initialization.

    Steps:
    1. Ask user to place spool
    2. list_spools → get spool_id, confirm tag detected
    3. format_spool(spool_id)
    4. Wait 3s for NFC write
    5. list_spools → verify: type=PLA, grams_remaining=1000, color=#FFFFFF, blank=false
    """

    def run(self):
        try:
            # Step 1: Request user to place spool
            self._emit_step("Place Spool", "running", "Waiting for user to place spool on reader")
            self._request_user_action("Please place a blank or formatted spool on the NFC reader")
            self._emit_step("Place Spool", "passed")

            # Step 2: List spools to get spool_id
            self._emit_step("Detect Spool", "running", "Reading spool ID from NFC tag")
            spools = self._ble_list_spools()
            self._assert("current" in spools, "list_spools response missing 'current' field")
            current = spools.get("current")
            self._assert(current is not None, "No spool currently detected")
            spool_id = current["id"]
            self._emit_step("Detect Spool", "passed", f"Detected spool: {spool_id}")

            # Step 3: Format spool
            self._emit_step("Format Spool", "running", "Writing default values to tag")
            self._ble_format_spool(spool_id)
            self._emit_step("Format Spool", "passed")

            # Step 4: Wait for NFC write
            self._wait_seconds(3, "Waiting for NFC write to complete")

            # Step 5: Verify default values
            self._emit_step("Verify Defaults", "running", "Reading back tag data")
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "Spool disappeared after format")

            self._assert(current["id"] == spool_id, "Spool ID changed after format")
            self._assert(current.get("blank") != True, "Spool should not be blank after format")
            self._assert(current.get("type") == "PLA", f"Expected type=PLA, got {current.get('type')}")
            self._assert(current.get("grams_remaining") == 1000,
                        f"Expected grams_remaining=1000, got {current.get('grams_remaining')}")
            self._assert(current.get("color") == "#FFFFFF",
                        f"Expected color=#FFFFFF, got {current.get('color')}")

            self._emit_step("Verify Defaults", "passed",
                          f"PLA, 1000g, #FFFFFF confirmed")

            self.result = "passed"

        except Exception as e:
            self.error = str(e)
            self.result = "failed"
            if hasattr(self, '_current_step'):
                self._emit_step(self._current_step, "failed", str(e))
