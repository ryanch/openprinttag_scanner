"""Test: Write real OpenPrintTag binary and verify weight deduction."""

import base64
from pathlib import Path

from .base import BaseTestScenario


class RealTagBinaryWriteTest(BaseTestScenario):
    """
    Test raw binary write flow with a known-good OpenPrintTag payload.

    Steps:
    1. Ensure a spool/tag is present.
    2. Load test/res/openprinttag_PETG_Jet_Black.bin and write via write_raw_tag.
    3. Verify type=PETG and grams_remaining=1050.
    4. Remove 100g (set grams_remaining=950) and verify.
    """

    def run(self):
        try:
            # Step 1: Ensure spool is present
            current = self._ensure_spool_present(
                "Please place a tag on the NFC reader"
            )
            spool_id = current["id"]

            # Step 2: Load and write raw binary image
            self._emit_step("Write Real Binary", "running", "Loading .bin and writing to tag")
            bin_path = Path(__file__).resolve().parents[2] / "res" / "openprinttag_PETG_Jet_Black.bin"
            self._assert(bin_path.exists(), f"Missing test resource: {bin_path}")
            raw_bytes = bin_path.read_bytes()
            payload_b64 = base64.b64encode(raw_bytes).decode("ascii")
            self._ble_command(
                {"command": "write_raw_tag", "id": spool_id, "data": payload_b64},
                read_data=False
            )
            self._emit_step("Write Real Binary", "passed", f"Wrote {len(raw_bytes)} bytes")

            # Step 3: Verify PETG at 1050g
            self._emit_step("Verify PETG 1050g", "running", "Reading back tag data")
            self._wait_for_mqtt_multi_field_match({
                "material_type": "PETG",
                "remaining_g": 1050
            }, max_wait_sec=30)

            # Belt-and-suspenders verify
            spools = self._ble_list_spools()
            assert spools["current"]["material_type"] == "PETG"
            assert spools["current"]["grams_remaining"] == 1050
            self._emit_step("Verify PETG 1050g", "passed", "type=PETG, grams=1050")

            # Step 4: Remove 100g and verify 950g
            self._emit_step("Remove 100g", "running", "Writing grams_remaining=950")
            self._ble_update_spool(spool_id, grams_remaining=950)
            self._emit_step("Remove 100g", "passed")

            self._emit_step("Verify 950g", "running", "Reading back tag data")
            self._wait_for_mqtt_multi_field_match({
                "material_type": "PETG",
                "remaining_g": 950
            }, max_wait_sec=30)

            # Belt-and-suspenders verify
            spools = self._ble_list_spools()
            assert spools["current"]["material_type"] == "PETG"
            assert spools["current"]["grams_remaining"] == 950
            self._emit_step("Verify 950g", "passed", "type=PETG, grams=950")

            self.result = "passed"

        except Exception as e:
            self.error = str(e)
            self.result = "failed"
            if hasattr(self, "_current_step"):
                self._emit_step(self._current_step, "failed", str(e))
