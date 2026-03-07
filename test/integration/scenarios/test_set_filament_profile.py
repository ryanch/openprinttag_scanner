"""Test 5: Change filament type and manufacturer"""

from .base import BaseTestScenario


class SetFilamentProfileTest(BaseTestScenario):
    """
    Test updating spool filament profile fields.

    Steps:
    1. Ensure formatted spool is present (reuse existing if already on reader)
    2. Normalize to baseline profile (PLA + Unknown)
    3. update_spool(id, type=PETG, manufacturer=Prusament)
    4. Poll until both fields match expected values
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
            # Step 1: Ensure tag is freshly formatted with clean state
            spool_id = self._ensure_tag_formatted()

            # Step 2: Set baseline profile
            self._emit_step("Set Baseline", "running", "Writing type=PLA, manufacturer=Unknown")
            self._ble_update_spool(spool_id, type="PLA", manufacturer="Unknown")
            baseline = self._wait_for_profile("PLA", "Unknown")
            self._emit_step(
                "Set Baseline",
                "passed",
                f"Baseline confirmed: type={baseline.get('type')}, manufacturer={baseline.get('manufacturer')}"
            )

            # Step 3: Update filament profile
            self._emit_step("Set Profile", "running", "Writing type=PETG, manufacturer=Prusament")
            self._ble_update_spool(spool_id, type="PETG", manufacturer="Prusament")
            self._emit_step("Set Profile", "passed")

            # Step 4: Verify updated values (type/manufacturer writes are queued separately)
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
