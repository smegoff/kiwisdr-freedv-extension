import unittest
from reporter.reporter import CALLSIGN, GRID, ReporterState


class ReporterTests(unittest.TestCase):
    def test_identity_validation(self):
        self.assertTrue(CALLSIGN.fullmatch("ZL2ABC"))
        self.assertTrue(CALLSIGN.fullmatch("ZL2ABC/P"))
        self.assertFalse(CALLSIGN.fullmatch("listener@example.com"))
        self.assertTrue(GRID.fullmatch("RF80"))
        self.assertTrue(GRID.fullmatch("RF80AA"))

    def test_latest_synced_session_wins(self):
        state = ReporterState()
        state.update({"type": "status", "session_id": 1, "sync": True, "frequency": 7177000})
        state.update({"type": "status", "session_id": 2, "sync": True, "frequency": 14236000})
        self.assertEqual(state.selected()["session_id"], 2)
        state.update({"type": "stop", "session_id": 2})
        self.assertEqual(state.selected()["session_id"], 1)

    def test_duplicate_suppression(self):
        state = ReporterState()
        self.assertTrue(state.reportable("ZL2ABC", "700D", 7177000))
        self.assertFalse(state.reportable("ZL2ABC", "700D", 7177000))

    def test_authenticated_kiwi_station_config_replaces_defaults(self):
        state = ReporterState()
        state.update({"type": "start", "session_id": 1, "sync": False,
                      "enabled": True, "station_callsign": "zl2abc",
                      "grid_square": "rf80aa", "message": "Kiwi RX"})
        self.assertEqual(state.config["callsign"], "ZL2ABC")
        self.assertEqual(state.config["grid_square"], "RF80AA")
        self.assertNotIn("listener", state.config)


if __name__ == "__main__":
    unittest.main()
