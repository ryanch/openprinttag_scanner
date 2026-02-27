#!/usr/bin/env python3
"""
Hardware-in-the-loop integration test server.
Serves test UI, mock PrusaLink API, and orchestrates test scenarios via SSE.
"""

import os
import sys
import json
import time
import socket
import threading
import queue
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
from pathlib import Path

# Add scenarios to path
sys.path.insert(0, str(Path(__file__).parent))

from mock_prusalink import MockPrusalinkState
from scenarios.test_format_spool import FormatSpoolTest
from scenarios.test_set_filament import SetFilamentTest
from scenarios.test_print_e2e import PrintE2ETest


class TestOrchestrator:
    """Manages test lifecycle, SSE clients, and BLE result bridge"""

    def __init__(self):
        self.mock_state = MockPrusalinkState()
        self.tests = {
            "format_spool": {
                "id": "format_spool",
                "name": "Format Spool",
                "description": "Format NFC tag and verify default values",
                "class": FormatSpoolTest
            },
            "set_filament": {
                "id": "set_filament",
                "name": "Set Filament Weight",
                "description": "Update and verify filament weight",
                "class": SetFilamentTest
            },
            "print_e2e": {
                "id": "print_e2e",
                "name": "End-to-End Print",
                "description": "Full print simulation with weight deduction",
                "class": PrintE2ETest
            }
        }

        self.current_test = None
        self.current_test_thread = None
        self.sse_clients = []
        self.sse_lock = threading.Lock()

        # Queues for BLE bridge and user actions
        self.ble_result_queue = queue.Queue()
        self.user_action_queue = queue.Queue()
        self.server_url_queue = queue.Queue()

    def get_test_list(self, server_url):
        """Return test list with server_url injected"""
        return {
            "tests": [
                {
                    "id": t["id"],
                    "name": t["name"],
                    "description": t["description"]
                }
                for t in self.tests.values()
            ],
            "server_url": server_url
        }

    def start_test(self, test_id):
        """Start a test scenario in background thread"""
        if self.current_test_thread and self.current_test_thread.is_alive():
            return {"error": "Test already running"}

        test_info = self.tests.get(test_id)
        if not test_info:
            return {"error": f"Unknown test: {test_id}"}

        # Clear queues
        self._clear_queue(self.ble_result_queue)
        self._clear_queue(self.user_action_queue)
        self._clear_queue(self.server_url_queue)

        # Create test instance
        self.current_test = test_info["class"](self, self.mock_state)

        # Start in background
        self.current_test_thread = threading.Thread(
            target=self._run_test_wrapper,
            daemon=True
        )
        self.current_test_thread.start()

        return {"status": "started"}

    def _run_test_wrapper(self):
        """Wrapper to catch exceptions and emit final event"""
        try:
            self.current_test.run()
        except Exception as e:
            self.current_test.result = "failed"
            self.current_test.error = str(e)
        finally:
            self.push_sse_event("test_complete", {
                "result": self.current_test.result or "failed",
                "error": self.current_test.error
            })

    def get_status(self):
        """Get current test status"""
        if not self.current_test:
            return {"status": "idle"}

        is_running = self.current_test_thread and self.current_test_thread.is_alive()
        return {
            "status": "running" if is_running else "completed",
            "result": self.current_test.result,
            "error": self.current_test.error
        }

    def add_sse_client(self, queue_obj):
        """Register SSE client"""
        with self.sse_lock:
            self.sse_clients.append(queue_obj)

    def remove_sse_client(self, queue_obj):
        """Unregister SSE client"""
        with self.sse_lock:
            if queue_obj in self.sse_clients:
                self.sse_clients.remove(queue_obj)

    def push_sse_event(self, event_type, data):
        """Push event to all SSE clients"""
        message = f"event: {event_type}\ndata: {json.dumps(data)}\n\n"
        with self.sse_lock:
            dead_clients = []
            for client_queue in self.sse_clients[:]:  # Copy list to avoid modification during iteration
                try:
                    client_queue.put_nowait(message)
                except queue.Full:
                    # Client queue full, skip this message
                    pass
                except Exception:
                    # Client is dead, mark for removal
                    dead_clients.append(client_queue)

            # Remove dead clients
            for dead in dead_clients:
                if dead in self.sse_clients:
                    self.sse_clients.remove(dead)

    def submit_ble_result(self, result):
        """Browser posts BLE command result"""
        self.ble_result_queue.put(result)

    def wait_for_ble_result(self, timeout=30):
        """Block until BLE result arrives"""
        try:
            return self.ble_result_queue.get(timeout=timeout)
        except queue.Empty:
            return {"error": "BLE result timeout"}

    def submit_user_action(self):
        """User confirmed action"""
        self.user_action_queue.put(True)

    def wait_for_user_action(self, timeout=120):
        """Block until user confirms"""
        try:
            return self.user_action_queue.get(timeout=timeout)
        except queue.Empty:
            return False

    def submit_server_url(self, url):
        """Browser posts server URL"""
        self.server_url_queue.put(url)

    def wait_for_server_url(self, timeout=10):
        """Block until server URL arrives"""
        try:
            return self.server_url_queue.get(timeout=timeout)
        except queue.Empty:
            return None

    @staticmethod
    def _clear_queue(q):
        """Clear all items from queue"""
        while not q.empty():
            try:
                q.get_nowait()
            except queue.Empty:
                break


class IntegrationTestHandler(BaseHTTPRequestHandler):
    """HTTP request handler for test server"""

    orchestrator = None  # Set by main()

    def log_message(self, format, *args):
        """Log HTTP requests to stderr"""
        import sys
        sys.stderr.write("%s - - [%s] %s\n" %
                         (self.address_string(),
                          self.log_date_time_string(),
                          format%args))

    def do_GET(self):
        """Handle GET requests"""
        parsed = urlparse(self.path)
        path = parsed.path

        # Static files
        if path == "/" or path == "/site/index.html":
            self._serve_static("site/index.html")
        elif path.startswith("/site/"):
            self._serve_static(path[1:])  # Remove leading /

        # Test API
        elif path == "/api/tests":
            server_url = self._get_server_url()
            test_list = self.orchestrator.get_test_list(server_url)
            self._send_json(test_list)

        elif path == "/api/tests/status":
            status = self.orchestrator.get_status()
            self._send_json(status)

        elif path == "/api/tests/events":
            self._handle_sse()

        # Mock PrusaLink API
        elif path == "/api/v1/status":
            if not self._check_api_key():
                return
            response = self.orchestrator.mock_state.get_status_response()
            self._send_json(response)

        elif path == "/api/v1/job":
            if not self._check_api_key():
                return
            response = self.orchestrator.mock_state.get_job_response()
            self._send_json(response)

        elif path.startswith("/api/v1/job/"):
            if not self._check_api_key():
                return
            response = self.orchestrator.mock_state.get_job_response()
            self._send_json(response)

        elif path.startswith("/usb/"):
            self._serve_bgcode(path)

        else:
            self.send_error(404, "Not Found")

    def do_POST(self):
        """Handle POST requests"""
        parsed = urlparse(self.path)
        path = parsed.path

        # Read request body
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length) if content_length > 0 else b""
        data = json.loads(body) if body else {}

        if path.startswith("/api/tests/") and path.endswith("/start"):
            test_id = path.split("/")[-2]
            result = self.orchestrator.start_test(test_id)
            self._send_json(result)

        elif path == "/api/tests/ble-result":
            self.orchestrator.submit_ble_result(data)
            self._send_json({"status": "ok"})

        elif path == "/api/tests/user-action":
            action = data.get("action")
            if action == "user_confirmed":
                self.orchestrator.submit_user_action()
            elif action == "server_url":
                self.orchestrator.submit_server_url(data.get("server_url"))
            self._send_json({"status": "ok"})

        else:
            self.send_error(404, "Not Found")

    def _serve_static(self, file_path):
        """Serve static file from test/integration/"""
        base_dir = Path(__file__).parent
        full_path = base_dir / file_path

        if not full_path.exists():
            self.send_error(404, "File not found")
            return

        try:
            content_type = "text/html" if file_path.endswith(".html") else "application/octet-stream"
            with open(full_path, "rb") as f:
                content = f.read()

            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(content)))
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            self.wfile.write(content)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            # Client disconnected - normal, just return
            pass
        except Exception as e:
            import sys
            print(f"Error serving static file {file_path}: {e}", file=sys.stderr)

    def _serve_bgcode(self, path):
        """Serve bgcode file with Range header support"""
        # Serve sample.bgcode from test/res/
        base_dir = Path(__file__).parent.parent / "res"
        bgcode_path = base_dir / "sample.bgcode"

        if not bgcode_path.exists():
            self.send_error(404, "Bgcode file not found")
            return

        file_size = bgcode_path.stat().st_size

        # Check for Range header
        range_header = self.headers.get("Range")
        if range_header:
            # Parse Range: bytes=0-8191
            range_match = range_header.replace("bytes=", "").split("-")
            start = int(range_match[0])
            end = int(range_match[1]) if range_match[1] else file_size - 1

            with open(bgcode_path, "rb") as f:
                f.seek(start)
                content = f.read(end - start + 1)

            self.send_response(206)  # Partial Content
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(content)))
            self.send_header("Content-Range", f"bytes {start}-{end}/{file_size}")
            self.end_headers()
            self.wfile.write(content)
        else:
            # Full file
            with open(bgcode_path, "rb") as f:
                content = f.read()

            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(file_size))
            self.end_headers()
            self.wfile.write(content)

    def _handle_sse(self):
        """Handle SSE connection"""
        try:
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
        except Exception as e:
            # Connection already broken before we could send headers
            return

        # Create queue for this client
        client_queue = queue.Queue(maxsize=100)  # Prevent memory issues
        self.orchestrator.add_sse_client(client_queue)

        try:
            while True:
                # Block until message or timeout
                try:
                    message = client_queue.get(timeout=30)
                    self.wfile.write(message.encode("utf-8"))
                    self.wfile.flush()
                except queue.Empty:
                    # Send keepalive
                    try:
                        self.wfile.write(b": keepalive\n\n")
                        self.wfile.flush()
                    except (BrokenPipeError, ConnectionResetError):
                        # Client disconnected
                        break
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            # Client disconnected - this is normal
            pass
        except Exception as e:
            # Unexpected error - log it but don't crash
            import sys
            print(f"SSE error: {e}", file=sys.stderr)
        finally:
            self.orchestrator.remove_sse_client(client_queue)

    def _check_api_key(self):
        """Verify X-Api-Key header"""
        api_key = self.headers.get("X-Api-Key")
        if api_key != self.orchestrator.mock_state.api_key:
            self.send_response(401)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"error": "Invalid API key"}).encode("utf-8"))
            return False
        return True

    def _send_json(self, data):
        """Send JSON response"""
        body = json.dumps(data).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _get_server_url(self):
        """Build server URL using LAN IP (for device config)"""
        # Always use LAN IP, not Host header
        # Device needs actual network address, not localhost
        lan_ip = get_lan_ip()
        port = self.server.server_port
        return f"http://{lan_ip}:{port}"


def get_lan_ip():
    """Get LAN IP address"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


def main():
    import argparse

    parser = argparse.ArgumentParser(description="Integration test server")
    parser.add_argument("--port", type=int, default=8080, help="Server port")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose logging")
    args = parser.parse_args()

    # Create orchestrator
    orchestrator = TestOrchestrator()
    IntegrationTestHandler.orchestrator = orchestrator

    # Start server
    server = ThreadingHTTPServer(("0.0.0.0", args.port), IntegrationTestHandler)
    server.daemon_threads = True  # Allow threads to terminate when main exits
    lan_ip = get_lan_ip()

    print(f"Integration test server running on:")
    print(f"  http://localhost:{args.port}/")
    print(f"  http://{lan_ip}:{args.port}/")
    print()
    print("Open the URL in Chrome to run tests.")
    print("Press Ctrl+C to stop.")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
        server.shutdown()


if __name__ == "__main__":
    main()
