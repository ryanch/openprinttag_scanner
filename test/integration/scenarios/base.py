"""Base test scenario class with BLE bridge helpers"""

import time
import json
import threading
import queue
from abc import ABC, abstractmethod
from pathlib import Path

try:
    import paho.mqtt.client as mqtt
    MQTT_AVAILABLE = True
except ImportError:
    MQTT_AVAILABLE = False
    mqtt = None


class MQTTTagStateListener:
    """
    MQTT listener for tag state updates.
    Subscribes to openprinttag/{device_id}/tag/state and buffers messages.
    Thread-safe for use in test scenarios.
    """

    def __init__(self, broker_host, broker_port, device_id, username="", password=""):
        self.broker_host = broker_host
        self.broker_port = broker_port
        self.device_id = device_id
        self.username = username
        self.password = password

        self.topic = f"openprinttag/{device_id}/tag/state"
        self.client = None
        self.latest_state = None
        self.state_lock = threading.Lock()
        self.message_queue = queue.Queue(maxsize=100)
        self.connected_event = threading.Event()
        self.connection_failed = False
        self.connection_error = None

    def connect(self, timeout=5):
        """
        Connect to MQTT broker (non-blocking).
        Returns True if connected within timeout, False otherwise.
        """
        if not MQTT_AVAILABLE:
            self.connection_failed = True
            self.connection_error = "paho-mqtt not installed"
            return False

        if not self.device_id:
            self.connection_failed = True
            self.connection_error = "device_id not configured"
            return False

        try:
            client_id = f"test_{self.device_id}_{int(time.time())}"
            print(f"  MQTT: Creating client (id={client_id})")
            self.client = mqtt.Client(client_id=client_id)

            if self.username and self.password:
                print(f"  MQTT: Setting credentials (username={self.username})")
                self.client.username_pw_set(self.username, self.password)

            self.client.on_connect = self._on_connect
            self.client.on_message = self._on_message
            self.client.on_disconnect = self._on_disconnect

            print(f"  MQTT: Connecting to {self.broker_host}:{self.broker_port}...")
            self.client.connect(self.broker_host, self.broker_port, keepalive=60)
            self.client.loop_start()

            # Wait for connection with timeout
            print(f"  MQTT: Waiting for connection (timeout={timeout}s)...")
            if self.connected_event.wait(timeout=timeout):
                return True
            else:
                self.connection_failed = True
                self.connection_error = f"Connection timeout after {timeout}s - broker may be down or unreachable"
                return False

        except Exception as e:
            self.connection_failed = True
            self.connection_error = f"{type(e).__name__}: {str(e)}"
            print(f"  MQTT: Exception during connect: {self.connection_error}")
            return False

    def _on_connect(self, client, userdata, flags, rc):
        """MQTT connection callback (runs in paho thread)"""
        rc_codes = {
            0: "Connection successful",
            1: "Connection refused - incorrect protocol version",
            2: "Connection refused - invalid client identifier",
            3: "Connection refused - server unavailable",
            4: "Connection refused - bad username or password",
            5: "Connection refused - not authorized"
        }

        if rc == 0:
            print(f"  MQTT: Connected successfully (rc=0)")
            print(f"  MQTT: Subscribing to {self.topic}...")
            client.subscribe(self.topic)
            self.connected_event.set()
        else:
            error_msg = rc_codes.get(rc, f"Unknown error code {rc}")
            self.connection_failed = True
            self.connection_error = error_msg
            print(f"  MQTT: Connection failed - {error_msg}")

    def _on_message(self, client, userdata, msg):
        """MQTT message callback (runs in paho thread)"""
        try:
            payload = json.loads(msg.payload.decode('utf-8'))

            # Thread-safe update of latest state
            with self.state_lock:
                self.latest_state = payload

            # Add to message queue (non-blocking, drop oldest if full)
            try:
                self.message_queue.put_nowait(payload)
            except queue.Full:
                # Drop oldest message and add new one
                try:
                    self.message_queue.get_nowait()
                    self.message_queue.put_nowait(payload)
                except queue.Empty:
                    pass

        except Exception as e:
            # Silently ignore malformed messages
            pass

    def _on_disconnect(self, client, userdata, rc):
        """MQTT disconnect callback (runs in paho thread)"""
        self.connected_event.clear()

    def get_latest_state(self):
        """Thread-safe access to latest tag state (dict or None)"""
        with self.state_lock:
            return self.latest_state.copy() if self.latest_state else None

    def wait_for_condition(self, condition, timeout=30, poll_interval=1):
        """
        Wait for condition to be met on tag state.

        Args:
            condition: Callable(state_dict) -> bool, or None for any update
            timeout: Maximum wait time in seconds
            poll_interval: Check interval in seconds

        Returns:
            dict: Tag state when condition met

        Raises:
            TimeoutError: If timeout exceeded
        """
        start_time = time.time()

        while time.time() - start_time < timeout:
            state = self.get_latest_state()

            if state is not None:
                if condition is None or condition(state):
                    return state

            time.sleep(poll_interval)

        raise TimeoutError(f"MQTT condition not met within {timeout}s")

    def disconnect(self):
        """Disconnect from MQTT broker"""
        if self.client:
            self.client.loop_stop()
            self.client.disconnect()
            self.client = None


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
        self.mqtt_listener = None
        self._setup_mqtt()

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

    def _load_mqtt_config(self):
        """Load MQTT configuration from mqtt_config.json"""
        config_path = Path(__file__).parent.parent / "mqtt_config.json"

        defaults = {
            "broker_host": "localhost",
            "broker_port": 1883,
            "device_id": "",
            "username": "",
            "password": ""
        }

        if not config_path.exists():
            return defaults

        try:
            with open(config_path, 'r') as f:
                config = json.load(f)
                # Merge with defaults
                defaults.update(config)
                return defaults
        except Exception as e:
            print(f"Warning: Failed to load mqtt_config.json: {e}")
            return defaults

    def _setup_mqtt(self):
        """
        Initialize MQTT listener (non-blocking, non-fatal).
        Falls back to BLE polling if MQTT unavailable.
        """
        print("=" * 60)
        print("MQTT Setup: Initializing MQTT listener...")

        if not MQTT_AVAILABLE:
            print("Warning: paho-mqtt not installed - MQTT event waiting disabled")
            print("  Install with: pip install -r test/integration/requirements.txt")
            print("=" * 60)
            return

        config = self._load_mqtt_config()
        print(f"MQTT Setup: Loaded config - broker={config['broker_host']}:{config['broker_port']}, device_id={config['device_id']}")

        if not config["device_id"]:
            print("Warning: mqtt_config.json missing device_id - MQTT event waiting disabled")
            print("  Find device_id via BLE config, HA entities, or serial output")
            print("=" * 60)
            return

        print(f"MQTT Setup: Creating listener for device {config['device_id']}...")
        self.mqtt_listener = MQTTTagStateListener(
            broker_host=config["broker_host"],
            broker_port=config["broker_port"],
            device_id=config["device_id"],
            username=config.get("username", ""),
            password=config.get("password", "")
        )

        print(f"MQTT Setup: Attempting connection to {config['broker_host']}:{config['broker_port']} (timeout=5s)...")
        if self.mqtt_listener.connect(timeout=5):
            print(f"✓ MQTT: Connected to {config['broker_host']}:{config['broker_port']}")
            print(f"✓ MQTT: Subscribed to openprinttag/{config['device_id']}/tag/state")
            print("=" * 60)
        else:
            print(f"✗ MQTT connection failed: {self.mqtt_listener.connection_error}")
            print("  Tests will fall back to BLE polling")
            print("=" * 60)
            self.mqtt_listener = None

    def _cleanup_mqtt(self):
        """Disconnect MQTT listener"""
        if self.mqtt_listener:
            self.mqtt_listener.disconnect()
            self.mqtt_listener = None

    def _wait_for_mqtt_spool_update(self, max_wait_sec=30, poll_interval_sec=1,
                                     condition=None, reason=""):
        """
        Wait for MQTT tag state update with optional condition.
        Falls back to BLE polling if MQTT unavailable.

        Args:
            max_wait_sec: Maximum wait time (default: 30)
            poll_interval_sec: Check interval (default: 1)
            condition: Optional callable(state_dict) -> bool
            reason: Description for progress event

        Returns:
            dict: Latest tag state

        Raises:
            TimeoutError: If max_wait_sec exceeded
        """
        # Fall back to BLE if MQTT not available
        if not self.mqtt_listener:
            print(f"Using BLE polling (MQTT not available)")
            return self._wait_for_spool_update_ble(
                max_wait_sec, poll_interval_sec, condition, reason
            )

        # Use MQTT event-driven wait
        print(f"Using MQTT event-driven wait (timeout={max_wait_sec}s)")
        if reason:
            self.orchestrator.push_sse_event("waiting", {
                "seconds": max_wait_sec,
                "reason": reason
            })

        try:
            state = self.mqtt_listener.wait_for_condition(
                condition=condition,
                timeout=max_wait_sec,
                poll_interval=poll_interval_sec
            )
            return state

        except TimeoutError:
            # Timeout - fall back to BLE for final check
            print(f"Warning: MQTT timeout after {max_wait_sec}s, checking via BLE")
            return self._wait_for_spool_update_ble(
                max_wait_sec=5,
                poll_interval_sec=1,
                condition=condition,
                reason="Final BLE check after MQTT timeout"
            )

    def _wait_for_spool_update_ble(self, max_wait_sec, poll_interval_sec,
                                    condition, reason):
        """
        Fallback: Poll via BLE list_spools (existing behavior).

        Args:
            max_wait_sec: Maximum wait time
            poll_interval_sec: Check interval
            condition: Optional callable(spool_dict) -> bool
            reason: Description for progress event

        Returns:
            dict: Current spool state

        Raises:
            TimeoutError: If max_wait_sec exceeded
        """
        if reason:
            self.orchestrator.push_sse_event("waiting", {
                "seconds": max_wait_sec,
                "reason": reason
            })

        start_time = time.time()

        while time.time() - start_time < max_wait_sec:
            current = self._get_current_spool()

            if current is not None:
                if condition is None or condition(current):
                    return current

            time.sleep(poll_interval_sec)

        raise TimeoutError(f"BLE polling timeout after {max_wait_sec}s")

    def _wait_for_mqtt_tag_present(self, max_wait_sec=30):
        """Wait for tag to be present on reader"""
        return self._wait_for_mqtt_spool_update(
            max_wait_sec=max_wait_sec,
            condition=lambda s: s.get("present") == True,
            reason="Waiting for tag to be placed on reader"
        )

    def _wait_for_mqtt_tag_absent(self, max_wait_sec=30):
        """Wait for tag to be removed from reader"""
        return self._wait_for_mqtt_spool_update(
            max_wait_sec=max_wait_sec,
            condition=lambda s: s.get("present") == False,
            reason="Waiting for tag to be removed from reader"
        )

    def _wait_for_mqtt_remaining_weight(self, expected_g, tolerance=2, max_wait_sec=30):
        """
        Wait for tag remaining weight to match expected value.

        Args:
            expected_g: Expected remaining weight in grams
            tolerance: Acceptable deviation in grams (default: 2)
            max_wait_sec: Maximum wait time

        Returns:
            dict: Tag state when weight matches
        """
        def weight_matches(state):
            if not state.get("present"):
                return False
            remaining = state.get("remaining_g")
            if remaining is None:
                return False
            return abs(remaining - expected_g) <= tolerance

        return self._wait_for_mqtt_spool_update(
            max_wait_sec=max_wait_sec,
            condition=weight_matches,
            reason=f"Waiting for remaining weight to reach {expected_g}g"
        )

    def _wait_for_mqtt_field_match(self, field_name, expected_value, max_wait_sec=30):
        """
        Wait for any single tag field to match expected value via MQTT.
        Falls back to BLE polling if MQTT unavailable.

        Args:
            field_name: Field name in MQTT state (e.g., "color", "material_type")
            expected_value: Expected field value
            max_wait_sec: Maximum wait time

        Returns:
            dict: Tag state when field matches
        """
        return self._wait_for_mqtt_spool_update(
            max_wait_sec=max_wait_sec,
            condition=lambda s: s.get(field_name) == expected_value,
            reason=f"Waiting for {field_name}={expected_value}"
        )

    def _wait_for_mqtt_multi_field_match(self, field_dict, max_wait_sec=30):
        """
        Wait for multiple tag fields to match expected values simultaneously via MQTT.
        Falls back to BLE polling if MQTT unavailable.

        Args:
            field_dict: Dictionary of field_name -> expected_value
            max_wait_sec: Maximum wait time

        Returns:
            dict: Tag state when all fields match

        Example:
            state = self._wait_for_mqtt_multi_field_match({
                "material_type": "PETG",
                "manufacturer": "Prusament"
            })
        """
        def all_match(state):
            return all(state.get(k) == v for k, v in field_dict.items())

        fields_str = ", ".join(f"{k}={v}" for k, v in field_dict.items())
        return self._wait_for_mqtt_spool_update(
            max_wait_sec=max_wait_sec,
            condition=all_match,
            reason=f"Waiting for {fields_str}"
        )
