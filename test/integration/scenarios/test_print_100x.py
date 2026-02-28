"""
Test scenario: Run end-to-end print cycles for a configurable count.
"""
import time
from .base import BaseTestScenario

PRINT_ITERATIONS = 500


class Test100xPrint(BaseTestScenario):
    """
    Endurance test: Run many print cycles to verify stability and accuracy.

    Validates:
    - Consistent weight deduction across many prints
    - No memory leaks or crashes
    - NFC write reliability
    - Mock API state management
    """

    id = "print_100x"
    name = f"{PRINT_ITERATIONS}x Print Endurance Test"
    description = (
        f"Run {PRINT_ITERATIONS} consecutive print cycles "
        "(9.18g each) to verify system stability"
    )

    ITERATIONS = PRINT_ITERATIONS
    RESET_INTERVAL = 100
    FILAMENT_PER_PRINT = 9.18  # grams, from sample.bgcode
    INITIAL_WEIGHT = 1000.0
    TOLERANCE = 2.0  # grams tolerance per check

    def run(self):
        """
        Run many print cycles:
        1. Set spool to 1000g
        2. For each iteration:
           - Start print (9.18g)
           - Wait for completion
           - Verify weight decreased correctly
        3. Report final statistics
        """
        original_config = None
        stats = {
            'total': self.ITERATIONS,
            'passed': 0,
            'failed': 0,
            'errors': [],
            'weight_drift': []  # Track actual vs expected weight over time
        }

        try:
            # Step 1: Get server URL from browser
            self._emit_step("server_url", "running", "Requesting server URL from browser")
            server_url = self._get_server_url()
            self._emit_step("server_url", "passed", f"Got server URL: {server_url}")

            # Step 2: Save original config
            self._emit_step("save_config", "running", "Saving original device config")
            original_config = self._ble_read_config()
            self._emit_step("save_config", "passed", "Original config saved")
            # Step 3: Apply mock config
            self._emit_step("mock_config", "running", "Applying mock PrusaLink config")
            self._ble_write_config(
                prusa_link_url=server_url,
                prusa_link_api_key="test-api-key",
                poll_interval_ms=5000,
                automation_mode=0
            )
            self._wait_seconds(2, "config apply")
            self._emit_step("mock_config", "passed", "Mock config applied")

            # Step 4: Ensure spool present
            self._emit_step("spool_check", "running", "Checking for spool")
            spools = self._ble_list_spools()
            current = spools.get("current")

            if not current or current.get("blank"):
                self._request_user_action("Place a formatted spool on the scanner")
                spools = self._ble_list_spools()
                current = spools.get("current")

            self._assert(current and not current.get("blank"), "No valid spool detected")
            spool_id = current["id"]
            self._emit_step("spool_check", "passed", f"Spool detected: {spool_id}")

            # Step 5: Reset spool to initial weight
            self._emit_step("reset_weight", "running", f"Setting spool to {self.INITIAL_WEIGHT}g")
            self._ble_update_spool(spool_id, grams_remaining=int(self.INITIAL_WEIGHT))
            self._wait_seconds(3, "NFC write")
            spools = self._ble_list_spools()
            actual = spools["current"]["grams_remaining"]
            self._assert_approx(actual, self.INITIAL_WEIGHT, self.TOLERANCE, "initial weight")
            self._emit_step("reset_weight", "passed", f"Weight set to {actual}g")

            # Step 6: Run configured print cycles
            expected_weight = self.INITIAL_WEIGHT

            for i in range(1, self.ITERATIONS + 1):
                iteration_start = time.time()

                self._emit_step(f"iteration_{i}", "running",
                              f"Iteration {i}/{self.ITERATIONS} - Starting print")

                try:
                    # Start print
                    job_id = 1000 + i
                    self.mock_state.set_printing(
                        job_id=job_id,
                        filament_g=self.FILAMENT_PER_PRINT,
                        download_ref="/usb/SAMPLE~1.BGC"
                    )

                    # Wait for device to detect print
                    self._wait_seconds(15, f"print {i} detection (3× poll interval)")

                    # Complete print
                    self.mock_state.set_finished()

                    # Wait for deferred fetch + NFC write
                    self._wait_seconds(20, f"print {i} completion + NFC write")

                    # Reset to idle
                    self.mock_state.set_idle()

                    # Verify weight
                    spools = self._ble_list_spools()
                    actual_weight = spools["current"]["grams_remaining"]
                    expected_weight -= self.FILAMENT_PER_PRINT

                    drift = actual_weight - expected_weight
                    stats['weight_drift'].append({
                        'iteration': i,
                        'expected': round(expected_weight, 2),
                        'actual': actual_weight,
                        'drift': round(drift, 2)
                    })

                    # Allow tolerance to accumulate slightly over iterations
                    cumulative_tolerance = self.TOLERANCE + (i * 0.1)  # +0.1g per iteration

                    if abs(drift) > cumulative_tolerance:
                        error_msg = f"Weight drift too large: expected {expected_weight:.2f}g, got {actual_weight}g (drift: {drift:.2f}g)"
                        stats['errors'].append(f"Iteration {i}: {error_msg}")
                        stats['failed'] += 1
                        self._emit_step(f"iteration_{i}", "failed", error_msg)
                    else:
                        stats['passed'] += 1
                        iteration_time = time.time() - iteration_start
                        self._emit_step(f"iteration_{i}", "passed",
                                      f"Weight: {actual_weight}g (drift: {drift:+.2f}g, time: {iteration_time:.1f}s)")

                    # Progress update every 10 iterations
                    if i % 10 == 0:
                        avg_drift = sum(d['drift'] for d in stats['weight_drift'][-10:]) / 10
                        self._emit_progress(
                            f"Progress: {i}/{self.ITERATIONS} ({stats['passed']} passed, {stats['failed']} failed, "
                            f"avg drift last 10: {avg_drift:+.2f}g)"
                        )

                    # Reset spool weight every 100 iterations to avoid negative expected values
                    if i % self.RESET_INTERVAL == 0:
                        self._emit_step(
                            f"reset_weight_{i}",
                            "running",
                            f"Iteration {i}: resetting spool to {self.INITIAL_WEIGHT}g"
                        )
                        self._ble_update_spool(
                            spool_id,
                            grams_remaining=int(self.INITIAL_WEIGHT)
                        )
                        self._wait_seconds(3, f"NFC write reset at iteration {i}")
                        spools = self._ble_list_spools()
                        reset_weight = spools["current"]["grams_remaining"]
                        self._assert_approx(
                            reset_weight,
                            self.INITIAL_WEIGHT,
                            self.TOLERANCE,
                            f"reset weight at iteration {i}"
                        )
                        expected_weight = self.INITIAL_WEIGHT
                        self._emit_step(
                            f"reset_weight_{i}",
                            "passed",
                            f"Iteration {i}: weight reset to {reset_weight}g"
                        )

                except Exception as e:
                    stats['failed'] += 1
                    error_msg = f"Exception: {str(e)}"
                    stats['errors'].append(f"Iteration {i}: {error_msg}")
                    self._emit_step(f"iteration_{i}", "failed", error_msg)

                    # Reset mock state on error
                    self.mock_state.set_idle()

                    # Optional: stop on first failure
                    # raise

            # Step 7: Final statistics
            self._emit_step("statistics", "running", "Calculating final statistics")

            if stats['weight_drift']:
                total_drift = stats['weight_drift'][-1]['drift']
                avg_drift = sum(d['drift'] for d in stats['weight_drift']) / len(stats['weight_drift'])
                max_drift = max(abs(d['drift']) for d in stats['weight_drift'])
            else:
                total_drift = avg_drift = max_drift = 0

            final_weight = spools["current"]["grams_remaining"]

            summary = (
                f"{self.ITERATIONS}x Print Test Complete\n"
                f"━━━━━━━━━━━━━━━━━━━━━━━━\n"
                f"Passed:        {stats['passed']}/{stats['total']}\n"
                f"Failed:        {stats['failed']}/{stats['total']}\n"
                f"Final weight:  {final_weight}g\n"
                f"Expected:      {expected_weight:.2f}g\n"
                f"Total drift:   {total_drift:+.2f}g\n"
                f"Avg drift:     {avg_drift:+.2f}g\n"
                f"Max drift:     {max_drift:.2f}g\n"
            )

            if stats['errors']:
                summary += f"\nErrors ({len(stats['errors'])}):\n"
                for err in stats['errors'][:5]:  # Show first 5 errors
                    summary += f"  • {err}\n"
                if len(stats['errors']) > 5:
                    summary += f"  ... and {len(stats['errors']) - 5} more\n"

            if stats['failed'] == 0:
                self._emit_step("statistics", "passed", summary)
            else:
                self._emit_step("statistics", "failed", summary)
                raise AssertionError(f"{stats['failed']} iterations failed")

            self.result = "passed"

        except Exception as e:
            self.error = str(e)
            self.result = "failed"

        finally:
            # Step 8: Restore original config
            if original_config:
                try:
                    self._emit_step("restore_config", "running", "Restoring original config")
                    self._ble_write_config(
                        prusa_link_url=original_config.get("prusa_link_url", ""),
                        prusa_link_api_key=original_config.get("prusa_link_api_key", ""),
                        poll_interval_ms=original_config.get("poll_interval_ms", 30000),
                        automation_mode=original_config.get("automation_mode", 0)
                    )
                    self._wait_seconds(2, "config restore")
                    self._emit_step("restore_config", "passed", "Original config restored")
                except Exception as restore_error:
                    self.orchestrator.push_sse_event("warning", {
                        "message": f"Failed to restore config: {restore_error}"
                    })

            # Reset mock state
            self.mock_state.set_idle()

    def _emit_progress(self, message: str):
        """Emit a progress update (non-blocking info message)"""
        self.orchestrator.push_sse_event("info", {"message": message})
