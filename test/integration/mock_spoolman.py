"""
Mock Spoolman API state controller for integration tests.
"""

class MockSpoolmanState:
    def __init__(self):
        self.vendors = {}
        self.filaments = {}
        self.spools = {}
        self.next_vendor_id = 1
        self.next_filament_id = 1
        self.next_spool_id = 1

    def reset(self):
        """Reset all state to empty."""
        self.vendors = {}
        self.filaments = {}
        self.spools = {}
        self.next_vendor_id = 1
        self.next_filament_id = 1
        self.next_spool_id = 1

    def _normalize_uuid(self, uuid_str):
        """Normalize UUID by removing quotes for comparison."""
        if uuid_str is None:
            return None
        return uuid_str.strip('"').strip()

    def get_vendors(self, name_filter=None):
        """Get vendors, optionally filtered by name."""
        result = list(self.vendors.values())
        if name_filter:
            result = [v for v in result if v["name"] == name_filter]
        return result

    def create_vendor(self, name):
        """Create a new vendor."""
        vendor_id = self.next_vendor_id
        self.next_vendor_id += 1
        vendor = {
            "id": vendor_id,
            "name": name
        }
        self.vendors[vendor_id] = vendor
        return vendor

    def get_filaments(self, vendor_id=None, material=None):
        """Get filaments, optionally filtered by vendor_id and/or material."""
        result = list(self.filaments.values())
        if vendor_id is not None:
            result = [f for f in result if f["vendor_id"] == vendor_id]
        if material is not None:
            result = [f for f in result if f["material"] == material]
        return result

    def create_filament(self, vendor_id, material, density, diameter, weight, color_hex):
        """Create a new filament."""
        if vendor_id not in self.vendors:
            return None

        filament_id = self.next_filament_id
        self.next_filament_id += 1
        filament = {
            "id": filament_id,
            "vendor_id": vendor_id,
            "material": material,
            "density": density,
            "diameter": diameter,
            "weight": weight,
            "color_hex": color_hex
        }
        self.filaments[filament_id] = filament
        return filament

    def get_spools(self, filament_id=None, openprinttag_uuid=None):
        """Get spools, optionally filtered by filament_id or UUID in extra field."""
        result = list(self.spools.values())
        if filament_id is not None:
            result = [s for s in result if s["filament_id"] == filament_id]
        if openprinttag_uuid is not None:
            normalized_search = self._normalize_uuid(openprinttag_uuid)
            result = [
                s for s in result
                if s.get("extra", {}).get("openprinttag_uuid") and
                   self._normalize_uuid(s["extra"]["openprinttag_uuid"]) == normalized_search
            ]
        return result

    def get_spool_by_id(self, spool_id):
        """Get a single spool by ID."""
        return self.spools.get(spool_id)

    def create_spool(self, filament_id, remaining_weight, initial_weight, extra=None):
        """Create a new spool."""
        if filament_id not in self.filaments:
            return None

        spool_id = self.next_spool_id
        self.next_spool_id += 1
        spool = {
            "id": spool_id,
            "filament_id": filament_id,
            "remaining_weight": remaining_weight,
            "initial_weight": initial_weight,
            "extra": extra or {}
        }
        self.spools[spool_id] = spool
        return spool

    def update_spool(self, spool_id, remaining_weight=None, filament_id=None):
        """Update an existing spool."""
        if spool_id not in self.spools:
            return None

        spool = self.spools[spool_id]
        if remaining_weight is not None:
            spool["remaining_weight"] = remaining_weight
        if filament_id is not None:
            if filament_id not in self.filaments:
                return None
            spool["filament_id"] = filament_id

        return spool
