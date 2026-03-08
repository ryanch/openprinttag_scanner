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

    def run(self):
        try:
            # Step 1: Ensure tag is freshly formatted with clean state
            spool_id = self._ensure_tag_formatted()

            # Step 2: Set baseline profile
            self._emit_step("Set Baseline", "running", "Writing type=PLA, manufacturer=Unknown")
            self._ble_update_spool(spool_id, type="PLA", manufacturer="Unknown")
            baseline = self._wait_for_mqtt_multi_field_match({
                "material_type": "PLA",
                "manufacturer": "Unknown"
            }, max_wait_sec=30)

            # Belt-and-suspenders: verify via BLE
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "Spool disappeared after baseline write")
            self._emit_step(
                "Set Baseline",
                "passed",
                f"Baseline confirmed: type={current.get('type')}, manufacturer={current.get('manufacturer')}"
            )

            # Step 3: Update filament profile
            self._emit_step("Set Profile", "running", "Writing type=PETG, manufacturer=Prusament")
            self._ble_update_spool(spool_id, type="PETG", manufacturer="Prusament")
            self._emit_step("Set Profile", "passed")

            # Step 4: Verify updated values (type/manufacturer writes are queued separately)
            self._emit_step("Verify Profile", "running", "Reading back spool data")
            self._wait_for_mqtt_multi_field_match({
                "material_type": "PETG",
                "manufacturer": "Prusament"
            }, max_wait_sec=30)

            # Belt-and-suspenders: verify via BLE
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "Spool disappeared after profile write")
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
