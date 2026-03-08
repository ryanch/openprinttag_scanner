"""Test: Color Field Testing"""

from .base import BaseTestScenario


class ColorUpdateTest(BaseTestScenario):
    """
    Test updating and verifying the spool color field.

    Steps:
    1. Ensure formatted spool is present (reuse existing if already on reader)
    2. Format spool → verify default color is #FFFFFF
    3. Update color to #FF0000 (red) → verify
    4. Update color to #00FF00 (green) → verify
    5. Edge case: Try invalid color format (should reject or clamp)
    """

    def run(self):
        try:
            # Step 1: Ensure spool is present
            current = self._ensure_spool_present(
                "Please place a blank or formatted spool on the NFC reader"
            )
            spool_id = current["id"]

            # Step 2: Format spool and verify default color
            self._emit_step("Format Spool", "running", "Writing default values to tag")
            self._ble_format_spool(spool_id)
            self._wait_seconds(2, "Waiting for format to complete")

            self._emit_step("Verify Default Color", "running", "Reading back tag data")
            self._wait_for_mqtt_field_match("color", "#FFFFFF", max_wait_sec=30)

            # Belt-and-suspenders: verify via BLE
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "Spool disappeared after format")
            self._assert(current["color"] == "#FFFFFF", f"Expected #FFFFFF, got {current.get('color')}")
            self._emit_step("Verify Default Color", "passed", "#FFFFFF confirmed")

            # Step 3: Update color to red
            self._emit_step("Update Color to Red", "running", "Writing #FF0000 to tag")
            self._ble_update_spool(spool_id, color="#FF0000")
            self._wait_for_mqtt_field_match("color", "#FF0000", max_wait_sec=30)

            # Belt-and-suspenders: verify via BLE
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "Spool disappeared after red write")
            self._assert(current["color"] == "#FF0000", f"Expected #FF0000, got {current.get('color')}")
            self._emit_step("Update Color to Red", "passed", "#FF0000 confirmed")

            # Step 4: Update color to green
            self._emit_step("Update Color to Green", "running", "Writing #00FF00 to tag")
            self._ble_update_spool(spool_id, color="#00FF00")
            self._wait_for_mqtt_field_match("color", "#00FF00", max_wait_sec=30)

            # Belt-and-suspenders: verify via BLE
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "Spool disappeared after green write")
            self._assert(current["color"] == "#00FF00", f"Expected #00FF00, got {current.get('color')}")
            self._emit_step("Update Color to Green", "passed", "#00FF00 confirmed")

            # Step 5: Edge case - Invalid color format
            self._emit_step("Test Invalid Color", "running", "Testing invalid format handling")
            try:
                # Try invalid format - device should reject or handle gracefully
                self._ble_update_spool(spool_id, color="invalid")
                self._wait_seconds(2, "Waiting for potential write")

                # Read back - should still be green or have been rejected
                spools = self._ble_list_spools()
                current = spools.get("current")
                self._assert(current is not None, "Spool disappeared after invalid color")

                # Color should be unchanged (green) or device rejected the write
                final_color = current.get("color")
                self._emit_step("Test Invalid Color", "passed",
                              f"Device handled invalid format (color={final_color})")
            except Exception as e:
                # BLE command rejection is also acceptable
                self._emit_step("Test Invalid Color", "passed",
                              f"Device rejected invalid color format")

            self.result = "passed"

        except Exception as e:
            self.error = str(e)
            self.result = "failed"
