"""Spoolman sync integration test"""

import time
from scenarios.base import BaseTestScenario


class SpoolmanSyncTest(BaseTestScenario):
    """
    Test Spoolman synchronization across spool swaps:
    1. Configure Spoolman URL
    2. Place spool A, update weight, verify sync
    3. Swap to spool B, update weight, verify sync
    4. Swap back to A, verify spoolman_id unchanged
    """

    def run(self):
        try:
            self._emit_step("Test Start", "running", "Starting Spoolman sync test")
            initial_spool_count = len(self.mock_spoolman.spools)

            # Step 1: Save original config
            self._emit_step("Save Config", "running", "Reading device configuration")
            original_config = self._ble_read_config()
            self._emit_step("Save Config", "passed", "Original config saved")

            # Step 2: Configure Spoolman URL
            self._emit_step("Configure Spoolman", "running", "Getting server URL")
            server_url = self._get_server_url()
            self._emit_step("Configure Spoolman", "running", f"Writing Spoolman URL: {server_url}")

            config_to_write = original_config.copy()
            config_to_write["spoolman_url"] = server_url
            self._ble_write_config(**config_to_write)
            self._emit_step("Configure Spoolman", "passed", "Spoolman URL configured")

            # Step 3: Spool A setup
            self._emit_step("Setup Spool A", "running", "Ensuring spool A is present")
            spool_a = self._ensure_spool_present("Place spool A on reader", require_formatted=True)
            spool_a_id = spool_a["id"]
            self._emit_step("Setup Spool A", "running", f"Updating spool A ({spool_a_id}) weight to 944g")
            self._ble_update_spool(spool_a_id, grams_remaining=944)
            self._wait_seconds(10, "Waiting for Spoolman sync")
            self._emit_step("Setup Spool A", "passed", f"Spool A updated: {spool_a_id}")

            # Step 4: Verify spool A synced
            self._emit_step("Verify Spool A Sync", "running", "Checking Spoolman ID for spool A")
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "No current spool detected")
            self._assert(current.get("id") == spool_a_id, f"Expected spool A ({spool_a_id}), got {current.get('id')}")
            self._assert(current.get("grams_remaining") == 944, f"Expected 944g, got {current.get('grams_remaining')}g")

            spoolman_id_a = current.get("spoolman_id")
            self._assert(spoolman_id_a is not None, "Spoolman ID missing for spool A")
            self._assert(spoolman_id_a > 0, f"Invalid spoolman_id for spool A: {spoolman_id_a}")

            # Check mock state growth from baseline
            mock_spool_count = len(self.mock_spoolman.spools)
            self._assert(
                mock_spool_count == initial_spool_count + 1,
                f"Expected Spoolman spool count to grow by 1 (baseline={initial_spool_count}), found {mock_spool_count}"
            )

            # Verify no duplicate UUIDs
            duplicate_check = self._check_for_duplicate_uuids(spool_a_id)
            self._assert(duplicate_check["count"] == 1,
                        f"Found {duplicate_check['count']} Spoolman entries for spool A (expected 1): {duplicate_check['ids']}")

            self._emit_step("Verify Spool A Sync", "passed", f"Spoolman ID: {spoolman_id_a} (1 entry verified)")

            # Step 5: Swap to spool B
            self._emit_step("Swap to Spool B", "running", "Waiting for spool B")
            self._request_user_action("Remove spool A, place spool B on reader")
            self._wait_for_mqtt_spool_update(
                condition=lambda s: s.get("uid") != spool_a_id and s.get("present") == True,
                max_wait_sec=30,
                reason="Waiting for different spool"
            )

            # Belt-and-suspenders: verify via BLE
            spools = self._ble_list_spools()
            spool_b = spools.get("current")
            self._assert(spool_b is not None, "No spool detected via BLE after swap")
            spool_b_id = spool_b["id"]

            # Format if blank
            if spool_b.get("blank"):
                self._emit_step("Swap to Spool B", "running", f"Formatting blank spool B ({spool_b_id})")
                self._ble_format_spool(spool_b_id)
                self._wait_seconds(5, 'Waiting for NFC write')

            self._emit_step("Swap to Spool B", "passed", f"Detected spool B: {spool_b_id}")

            # Step 6: Spool B setup
            self._emit_step("Setup Spool B", "running", f"Updating spool B ({spool_b_id}) weight to 123g")
            self._ble_update_spool(spool_b_id, grams_remaining=123)
            self._wait_seconds(10, "Waiting for Spoolman sync")
            self._emit_step("Setup Spool B", "passed", f"Spool B updated: {spool_b_id}")

            # Step 7: Verify spool B synced
            self._emit_step("Verify Spool B Sync", "running", "Checking Spoolman ID for spool B")
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "No current spool detected")
            self._assert(current.get("id") == spool_b_id, f"Expected spool B ({spool_b_id}), got {current.get('id')}")
            self._assert(current.get("grams_remaining") == 123, f"Expected 123g, got {current.get('grams_remaining')}g")

            spoolman_id_b = current.get("spoolman_id")
            self._assert(spoolman_id_b is not None, "Spoolman ID missing for spool B")
            self._assert(spoolman_id_b > 0, f"Invalid spoolman_id for spool B: {spoolman_id_b}")
            self._assert(spoolman_id_b != spoolman_id_a, f"Spool B has same spoolman_id as A: {spoolman_id_b}")

            # Check mock state growth from baseline
            mock_spool_count = len(self.mock_spoolman.spools)
            self._assert(
                mock_spool_count == initial_spool_count + 2,
                f"Expected Spoolman spool count to grow by 2 (baseline={initial_spool_count}), found {mock_spool_count}"
            )

            # Verify no duplicate UUIDs for spool B
            duplicate_check_b = self._check_for_duplicate_uuids(spool_b_id)
            self._assert(duplicate_check_b["count"] == 1,
                        f"Found {duplicate_check_b['count']} Spoolman entries for spool B (expected 1): {duplicate_check_b['ids']}")

            # Verify spool A still has only 1 entry
            duplicate_check_a = self._check_for_duplicate_uuids(spool_a_id)
            self._assert(duplicate_check_a["count"] == 1,
                        f"Found {duplicate_check_a['count']} Spoolman entries for spool A (expected 1): {duplicate_check_a['ids']}")

            self._emit_step("Verify Spool B Sync", "passed", f"Spoolman ID: {spoolman_id_b} (2 total entries, no duplicates)")

            # Step 8: Swap back to spool A
            self._emit_step("Swap Back to A", "running", "Waiting for spool A to return")
            self._request_user_action("Remove spool B, place spool A back on reader")
            self._wait_for_mqtt_spool_update(
                condition=lambda s: s.get("uid") == spool_a_id and s.get("present") == True,
                max_wait_sec=30,
                reason=f"Waiting for spool A ({spool_a_id})"
            )

            # Belt-and-suspenders: verify via BLE
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "No spool detected via BLE")
            self._assert(current.get("id") == spool_a_id, f"Expected spool A, got {current.get('id')}")
            self._emit_step("Swap Back to A", "passed", f"Detected spool A again: {spool_a_id}")

            # Step 9: Verify spool A ID unchanged
            self._emit_step("Verify A ID Unchanged", "running", "Checking Spoolman ID for spool A")
            spools = self._ble_list_spools()
            current = spools.get("current")
            self._assert(current is not None, "No current spool detected")
            self._assert(current.get("id") == spool_a_id, f"Expected spool A ({spool_a_id}), got {current.get('id')}")

            final_spoolman_id_a = current.get("spoolman_id")
            self._assert(final_spoolman_id_a == spoolman_id_a,
                        f"Spoolman ID changed! Was {spoolman_id_a}, now {final_spoolman_id_a}")

            # Final check: still only +2 from baseline, no duplicates
            mock_spool_count = len(self.mock_spoolman.spools)
            self._assert(
                mock_spool_count == initial_spool_count + 2,
                f"Expected Spoolman spool count to remain baseline+2 ({initial_spool_count + 2}), found {mock_spool_count}"
            )

            duplicate_check_final = self._check_for_duplicate_uuids(spool_a_id)
            self._assert(duplicate_check_final["count"] == 1,
                        f"Found {duplicate_check_final['count']} Spoolman entries for spool A (expected 1): {duplicate_check_final['ids']}")

            self._emit_step("Verify A ID Unchanged", "passed", f"Spoolman ID still: {final_spoolman_id_a} (no duplicates)")

            # Success
            self.result = "passed"
            self._emit_step("Test Complete", "passed", "All steps passed")

        except AssertionError as e:
            self.result = "failed"
            self.error = str(e)
            self._emit_step("Test Failed", "failed", str(e))
        except Exception as e:
            self.result = "failed"
            self.error = str(e)
            self._emit_step("Test Error", "failed", str(e))
        finally:
            # Step 10: Restore config
            try:
                self._emit_step("Restore Config", "running", "Restoring original configuration")
                self._ble_write_config(**original_config)
                self._emit_step("Restore Config", "passed", "Config restored")
            except Exception as e:
                self._emit_step("Restore Config", "failed", f"Failed to restore config: {e}")

    def _check_for_duplicate_uuids(self, spool_uid):
        """
        Check mock Spoolman state for duplicate entries with the same openprinttag_uuid.
        Returns dict with 'count' (number of entries) and 'ids' (list of spoolman IDs).
        """
        matching_ids = []
        normalized_uid = self.mock_spoolman._normalize_uuid(f'"{spool_uid}"')  # Add quotes like device does

        for spool_id, spool in self.mock_spoolman.spools.items():
            stored_uuid = spool.get("extra", {}).get("openprinttag_uuid")
            if stored_uuid:
                normalized_stored = self.mock_spoolman._normalize_uuid(stored_uuid)
                if normalized_stored == normalized_uid:
                    matching_ids.append(spool_id)

        return {
            "count": len(matching_ids),
            "ids": matching_ids
        }

