"""Base test scenario class with BLE bridge helpers"""

import time
import json
from abc import ABC, abstractmethod


class BaseTestScenario(ABC):
    """
    Base class for test scenarios.
    Each test runs in a background thread and communicates with the browser via SSE.
    """

    def __init__(self, orchestrator, mock_state, mock_spoolman=None):
        self.orchestrator = orchestrator
        self.mock_state = mock_state
        self.mock_spoolman = mock_spoolman
        self.result = None
        self.error = None

    @abstractmethod
    def run(self):
        """Main test logic - implemented by subclasses"""
        pass

    def _emit_step(self, name, status, detail=""):
        """Emit step update event"""
        self.orchestrator.push_sse_event("step_update", {
            "step": name,
            "status": status,
            "detail": detail
        })

    def _ble_command(self, cmd, read_data=False):
        """
        Execute a BLE command via the browser bridge.
        Blocks until browser posts result to /api/tests/ble-result
        """
        self.orchestrator.push_sse_event("ble_command", {
            "command": cmd,
            "read_data": read_data
        })
        result = self.orchestrator.wait_for_ble_result(timeout=30)
        if result.get("error"):
            raise Exception(f"BLE command failed: {result['error']}")
        return result.get("data")

    def _ble_read_config(self):
        """Read device configuration via BLE"""
        cmd = {"command": "read_config"}
        data = self._ble_command(cmd, read_data=True)
        if not data:
            raise Exception("No data returned from read_config")
        return json.loads(data)

    def _ble_write_config(self, **fields):
        """Write configuration fields via BLE (partial update)"""
        cmd = {"command": "write_config"}
        cmd.update(fields)
        self._ble_command(cmd, read_data=False)

    def _ble_list_spools(self):
        """List spools via BLE"""
        cmd = {"command": "list_spools"}
        data = self._ble_command(cmd, read_data=True)
        if not data:
            raise Exception("No data returned from list_spools")
        return json.loads(data)

    def _ble_format_spool(self, spool_id):
        """Format spool via BLE"""
        cmd = {"command": "format_spool", "id": spool_id}
        self._ble_command(cmd, read_data=False)

    def _ble_update_spool(self, spool_id, **fields):
        """Update spool fields via BLE"""
        cmd = {"command": "update_spool", "id": spool_id}
        cmd.update(fields)
        max_attempts = 4
        for attempt in range(max_attempts):
            try:
                self._ble_command(cmd, read_data=False)
                return
            except Exception as e:
                msg = str(e)
                is_busy = "Busy" in msg or "Device busy" in msg
                if not is_busy or attempt == max_attempts - 1:
                    raise
                self._wait_seconds(1, f"Retrying update_spool after busy ({attempt + 1}/{max_attempts - 1})")

    def _get_current_spool(self):
        """Return currently detected spool dict or None."""
        spools = self._ble_list_spools()
        self._assert("current" in spools, "list_spools response missing 'current' field")
        return spools.get("current")

    def _ensure_spool_present(self, prompt_message, step_name="Place Spool", require_formatted=False):
        """
        Ensure a spool is present.
        If one is already detected, skip user prompt and reuse it.
        """
        self._emit_step(step_name, "running", "Checking for spool already on reader")
        current = self._get_current_spool()
        used_existing = current is not None

        if current is None:
            self._emit_step(step_name, "running", "Waiting for user to place spool on reader")
            self._request_user_action(prompt_message)
            current = self._get_current_spool()
            self._assert(current is not None, "No spool detected after user confirmation")

        if require_formatted:
            self._assert(current.get("blank") != True, "Spool is blank - please format it first")

        spool_id = current.get("id", "unknown")
        source_detail = "already present" if used_existing else "placed by user"
        self._emit_step(step_name, "passed", f"Detected: {spool_id} ({source_detail})")
        return current

    def _ensure_tag_formatted(self, step_name="Format Tag"):
        """
        Ensure tag is freshly formatted with clean state.
        This prevents test interdependencies from corrupted tag data.
        """
        self._emit_step(step_name, "running", "Checking for NFC tag")
        current = self._get_current_spool()

        if current is None:
            self._emit_step(step_name, "running", "Waiting for user to place tag on reader")
            self._request_user_action("Place NFC tag on reader")
            current = self._get_current_spool()
            self._assert(current is not None, "No tag detected after user confirmation")

        spool_id = current["id"]
        self._emit_step(step_name, "running", f"Formatting tag {spool_id} for clean state")
        self._ble_format_spool(spool_id)
        self._wait_seconds(5, "Waiting for format to complete")
        self._emit_step(step_name, "passed", f"Tag {spool_id} formatted successfully")
        return spool_id

    def _request_user_action(self, msg):
        """Ask user to perform a physical action, wait for confirmation"""
        self.orchestrator.push_sse_event("user_action_required", {
            "message": msg
        })
        result = self.orchestrator.wait_for_user_action(timeout=120)
        if not result:
            raise Exception("User action timeout")

    def _wait_seconds(self, n, reason):
        """Sleep with progress SSE event"""
        self.orchestrator.push_sse_event("waiting", {
            "seconds": n,
            "reason": reason
        })
        time.sleep(n)

    def _assert(self, condition, msg):
        """Assert with helpful error message"""
        if not condition:
            raise AssertionError(msg)

    def _assert_approx(self, actual, expected, tolerance, name):
        """Assert approximate equality"""
        if abs(actual - expected) > tolerance:
            raise AssertionError(
                f"{name}: expected {expected} ± {tolerance}, got {actual}"
            )

    def _get_server_url(self):
        """Request server URL from browser"""
        self.orchestrator.push_sse_event("request_server_url", {})
        result = self.orchestrator.wait_for_server_url(timeout=10)
        if not result:
            raise Exception("Failed to get server URL from browser")
        return result
