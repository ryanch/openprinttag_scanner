"""Test 2: Set and verify filament weight"""

from .base import BaseTestScenario


class SetFilamentTest(BaseTestScenario):
    """
    Test updating spool filament weight.

    Steps:
    1. Ensure formatted spool is present (reuse existing if already on reader)
    2. update_spool(id, grams_remaining=1000)
    3. Wait 3s, list_spools → verify grams_remaining=1000
    4. update_spool(id, grams_remaining=967)
    5. Wait 3s, list_spools → verify grams_remaining=967
    """

    def run(self):
        try:
            # Step 1: Ensure formatted spool is present
            current = self._ensure_spool_present(
                "Please place a formatted spool on the NFC reader",
                require_formatted=True
            )
            spool_id = current["id"]

            # Step 2: Set grams_remaining to 1000
            self._emit_step("Set 1000g", "running", "Writing 1000g to tag")
            self._ble_update_spool(spool_id, grams_remaining=1000)
            self._emit_step("Set 1000g", "passed")

            # Step 3: Wait and verify 1000g
            self._wait_seconds(3, "Waiting for NFC write")
            self._emit_step("Verify 1000g", "running", "Reading back tag data")
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "Spool disappeared")
            actual = current.get("grams_remaining")
            self._assert(actual == 1000, f"Expected 1000g, got {actual}g")
            self._emit_step("Verify 1000g", "passed", f"Confirmed: {actual}g")

            # Step 4: Set grams_remaining to 967
            self._emit_step("Set 967g", "running", "Writing 967g to tag")
            self._ble_update_spool(spool_id, grams_remaining=967)
            self._emit_step("Set 967g", "passed")

            # Step 5: Wait and verify 967g
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
