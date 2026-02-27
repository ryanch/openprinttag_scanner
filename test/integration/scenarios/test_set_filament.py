"""Test 2: Set and verify filament weight"""

from .base import BaseTestScenario


class SetFilamentTest(BaseTestScenario):
    """
    Test updating spool filament weight.

    Steps:
    1. Ask user to place formatted spool
    2. list_spools → get spool_id, confirm not blank
    3. update_spool(id, grams_remaining=1000)
    4. Wait 3s, list_spools → verify grams_remaining=1000
    5. update_spool(id, grams_remaining=967)
    6. Wait 3s, list_spools → verify grams_remaining=967
    """

    def run(self):
        try:
            # Step 1: Request user to place formatted spool
            self._emit_step("Place Spool", "running", "Waiting for formatted spool")
            self._request_user_action("Please place a formatted spool on the NFC reader")
            self._emit_step("Place Spool", "passed")

            # Step 2: List spools and verify not blank
            self._emit_step("Detect Spool", "running", "Reading spool ID")
            spools = self._ble_list_spools()
            self._assert("current" in spools, "list_spools response missing 'current' field")
            current = spools.get("current")
            self._assert(current is not None, "No spools detected")

            spool_id = current["id"]
            self._assert(current.get("blank") != True,
                        "Spool is blank - please format it first")
            self._emit_step("Detect Spool", "passed", f"Detected formatted spool: {spool_id}")

            # Step 3: Set grams_remaining to 1000
            self._emit_step("Set 1000g", "running", "Writing 1000g to tag")
            self._ble_update_spool(spool_id, grams_remaining=1000)
            self._emit_step("Set 1000g", "passed")

            # Step 4: Wait and verify 1000g
            self._wait_seconds(3, "Waiting for NFC write")
            self._emit_step("Verify 1000g", "running", "Reading back tag data")
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "Spool disappeared")
            actual = current.get("grams_remaining")
            self._assert(actual == 1000, f"Expected 1000g, got {actual}g")
            self._emit_step("Verify 1000g", "passed", f"Confirmed: {actual}g")

            # Step 5: Set grams_remaining to 967
            self._emit_step("Set 967g", "running", "Writing 967g to tag")
            self._ble_update_spool(spool_id, grams_remaining=967)
            self._emit_step("Set 967g", "passed")

            # Step 6: Wait and verify 967g
            self._wait_seconds(3, "Waiting for NFC write")
            self._emit_step("Verify 967g", "running", "Reading back tag data")
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "Spool disappeared")
            actual = current.get("grams_remaining")
            self._assert(actual == 967, f"Expected 967g, got {actual}g")
            self._emit_step("Verify 967g", "passed", f"Confirmed: {actual}g")

            self.result = "passed"

        except Exception as e:
            self.error = str(e)
            self.result = "failed"
            if hasattr(self, '_current_step'):
                self._emit_step(self._current_step, "failed", str(e))
