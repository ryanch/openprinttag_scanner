"""Test 5: Change filament type and manufacturer"""

from .base import BaseTestScenario


class SetFilamentProfileTest(BaseTestScenario):
    """
    Test updating spool filament profile fields.

    Steps:
    1. Ask user to place formatted spool
    2. list_spools -> get spool_id, confirm not blank
    3. Normalize to baseline profile (PLA + Unknown)
    4. update_spool(id, type=PETG, manufacturer=Prusament)
    5. Poll until both fields match expected values
    """

    def _wait_for_profile(self, expected_type, expected_manufacturer, max_attempts=12):
        for attempt in range(1, max_attempts + 1):
            spools = self._ble_list_spools()
            current = spools.get("current")
            if current is not None:
                actual_type = current.get("type")
                actual_manufacturer = current.get("manufacturer")
                if actual_type == expected_type and actual_manufacturer == expected_manufacturer:
                    return current
            self._wait_seconds(1, f"Waiting for profile write ({attempt}/{max_attempts})")

        raise AssertionError(
            f"Timed out waiting for type={expected_type}, manufacturer={expected_manufacturer}"
        )

    def run(self):
        try:
            # Step 1: Request user to place formatted spool
            self._emit_step("Place Spool", "running", "Waiting for formatted spool")
            self._request_user_action("Please place a formatted spool on the NFC reader")
            self._emit_step("Place Spool", "passed")

            # Step 2: Detect spool
            self._emit_step("Detect Spool", "running", "Reading spool data")
            spools = self._ble_list_spools()
            self._assert("current" in spools, "list_spools response missing 'current' field")
            current = spools.get("current")
            self._assert(current is not None, "No spool detected")
            self._assert(current.get("blank") != True, "Spool is blank - please format it first")
            spool_id = current["id"]
            self._emit_step("Detect Spool", "passed", f"Detected spool: {spool_id}")

            # Step 3: Normalize starting values to avoid dependence on previous tests
            self._emit_step("Set Baseline", "running", "Writing type=PLA, manufacturer=Unknown")
            self._ble_update_spool(spool_id, type="PLA", manufacturer="Unknown")
            baseline = self._wait_for_profile("PLA", "Unknown")
            self._emit_step(
                "Set Baseline",
                "passed",
                f"Baseline confirmed: type={baseline.get('type')}, manufacturer={baseline.get('manufacturer')}"
            )

            # Step 4: Update filament profile
            self._emit_step("Set Profile", "running", "Writing type=PETG, manufacturer=Prusament")
            self._ble_update_spool(spool_id, type="PETG", manufacturer="Prusament")
            self._emit_step("Set Profile", "passed")

            # Step 5: Verify updated values (type/manufacturer writes are queued separately)
            self._emit_step("Verify Profile", "running", "Reading back spool data")
            current = self._wait_for_profile("PETG", "Prusament")
            actual_type = current.get("type")
            actual_manufacturer = current.get("manufacturer")
            self._emit_step(
                "Verify Profile",
                "passed",
                f"Confirmed: type={actual_type}, manufacturer={actual_manufacturer}"
            )

            self.result = "passed"

        except Exception as e:
            self.error = str(e)
            self.result = "failed"
