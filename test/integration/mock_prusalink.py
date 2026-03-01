"""Mock PrusaLink API state controller for integration tests"""

class MockPrusalinkState:
    """Mutable state object controlling mock API responses"""

    def __init__(self):
        self.api_key = "test-api-key"
        self._status = "IDLE"
        self._job_id = None
        self._filament_g = 0.0
        self._download_ref = None
        self._progress = 0.0
        self._error_mode = False  # When True, return 500 errors

    def set_idle(self):
        """No job, printer idle"""
        self._status = "IDLE"
        self._job_id = None
        self._filament_g = 0.0
        self._download_ref = None
        self._progress = 0.0

    def set_printing(self, job_id, filament_g, download_ref, progress_percent=50.0):
        """Active job"""
        self._status = "PRINTING"
        self._job_id = job_id
        self._filament_g = filament_g
        self._download_ref = download_ref
        self._progress = float(progress_percent)

    def set_finished(self):
        """Job complete, 100% progress"""
        self._status = "FINISHED"
        self._progress = 100.0

    def set_canceled(self):
        """Job canceled"""
        self._status = "CANCELED"

    def set_error_mode(self, enabled):
        """Enable/disable API error responses (500 errors)"""
        self._error_mode = enabled

    def is_error_mode(self):
        """Check if error mode is enabled"""
        return self._error_mode

    def get_status_response(self):
        """For GET /api/v1/status"""
        response = {
            "printer": {
                "state": self._status,
                "temp_bed": 60.0,
                "temp_nozzle": 210.0
            }
        }

        if self._job_id is not None:
            response["job"] = {
                "id": self._job_id,
                "progress": self._progress,
                "time_remaining": 1200 if self._status == "PRINTING" else 0,
                "time_printing": 600
            }

        return response

    def get_job_response(self):
        """For GET /api/v1/job and /api/v1/job/{id}"""
        if self._job_id is None:
            return None

        response = {
            "id": self._job_id,
            "state": self._status,
            "progress": self._progress,
            "file": {
                "refs": {
                    "download": self._download_ref or "/usb/sample.bgcode"
                },
                "name": "sample.bgcode",
                "display_name": "sample.bgcode",
                "path": self._download_ref or "/usb/sample.bgcode",
                "size": 12345,
                "m_timestamp": 1640000000
            }
        }

        # Include filament metadata for finished jobs
        if self._status == "FINISHED" and self._filament_g > 0:
            response["file"]["meta"] = {
                "filament used [g]": self._filament_g
            }

        return response
