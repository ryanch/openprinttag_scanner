"""Test 6: Verify recently seen spools after swapping A -> B"""

from .base import BaseTestScenario


class RecentSpoolsTest(BaseTestScenario):
    """
    Verify recently seen spool tracking when swapping to a different tag.

    Steps:
    1. Ensure spool A is present (reuse existing if already on reader)
    2. Ask user to swap to a different spool B
    3. Verify current spool is B
    4. Verify recent list contains spool A
    """

    def run(self):
        try:
            # Step 1: Ensure spool A is present
            current_a = self._ensure_spool_present(
                "Please place spool A on the NFC reader"
            )
            spool_a_id = current_a.get("id")
            self._assert(spool_a_id is not None, "Spool A missing id")

            # Step 2: Ask user to swap to spool B
            self._emit_step("Place Spool B", "running", "Waiting for a different spool")
            self._request_user_action(
                "Please remove spool A and place a different spool B on the NFC reader"
            )
            current_b = self._wait_for_mqtt_spool_update(
                condition=lambda s: s.get("uid") != spool_a_id and s.get("present") == True,
                max_wait_sec=30,
                reason="Waiting for different spool"
            )

            # Belt-and-suspenders: verify via BLE
            spools = self._ble_list_spools()
            current_b = spools.get("current")
            self._assert(current_b is not None, "No spool detected via BLE after swap")
            spool_b_id = current_b.get("id")
            self._assert(spool_b_id is not None, "Spool B missing id")
            self._assert(spool_b_id != spool_a_id, "Spool B must be different from spool A")
            self._emit_step("Place Spool B", "passed", f"Detected: {spool_b_id}")

            # Step 3 + 4: Verify current and recent lists
            self._emit_step("Verify Recent Spools", "running", "Reading current + recent spools")
            spools = self._ble_list_spools()
            self._assert("current" in spools, "list_spools response missing 'current' field")
            current = spools.get("current")
            self._assert(current is not None, "No current spool after swap")
            self._assert(current.get("id") == spool_b_id, "Current spool does not match spool B")

            recent = spools.get("recent")
            self._assert(isinstance(recent, list), "list_spools response missing 'recent' list")
            recent_ids = [item.get("id") for item in recent if isinstance(item, dict)]
            self._assert(
                spool_a_id in recent_ids,
                f"Spool A ({spool_a_id}) not found in recently seen spools"
            )
            self._emit_step(
                "Verify Recent Spools",
                "passed",
                f"Current={spool_b_id}, recent contains spool A ({spool_a_id})"
            )

            self.result = "passed"

        except Exception as e:
            self.error = str(e)
            self.result = "failed"
