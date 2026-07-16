import asyncio
import unittest
from reporter.reporter import (
    CALLSIGN,
    CLIENT_VERSION,
    GRID,
    ReporterState,
    build_auth,
    connect_and_wait_for_acceptance,
)


class FakeSocketIO:
    def __init__(self, outcome="accepted"):
        self.connected = False
        self.handlers = {}
        self.outcome = outcome

    def on(self, name):
        def register(handler):
            self.handlers[name] = handler
            return handler
        return register

    def event(self, handler):
        self.handlers[handler.__name__] = handler
        return handler

    async def connect(self, _url, auth, wait_timeout):
        self.connected = True
        self.auth = auth
        self.wait_timeout = wait_timeout
        if self.outcome == "accepted":
            await self.handlers["connection_successful"]()
        elif self.outcome == "rejected":
            await self.handlers["disconnect"]("server disconnect")

    async def disconnect(self):
        self.connected = False
        if "disconnect" in self.handlers:
            await self.handlers["disconnect"]("client disconnect")


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

    def test_unsynced_session_publishes_tuned_frequency(self):
        state = ReporterState()
        state.update({"type": "start", "session_id": 1, "sync": False,
                      "frequency": 7177000, "mode": "700D"})
        self.assertEqual(state.selected()["frequency"], 7177000)

    def test_synced_session_takes_precedence_over_newer_unsynced_session(self):
        state = ReporterState()
        state.update({"type": "status", "session_id": 1, "sync": True,
                      "frequency": 7177000})
        state.update({"type": "status", "session_id": 2, "sync": False,
                      "frequency": 14236000})
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

    def test_periodic_status_reconstructs_config_after_sidecar_restart(self):
        state = ReporterState()
        state.update({"type": "status", "session_id": 1, "sync": False,
                      "frequency": 7177000, "mode": "700D", "enabled": True,
                      "station_callsign": "zl2abc", "grid_square": "rf80aa",
                      "message": "Kiwi RX"})
        self.assertTrue(state.config["enabled"])
        self.assertEqual(state.config["callsign"], "ZL2ABC")
        self.assertEqual(state.selected()["frequency"], 7177000)

    def test_reporter_auth_is_strictly_receive_only(self):
        auth = build_auth({"callsign": "ZL2ABC", "grid_square": "RF80AA"})
        self.assertEqual(auth["role"], "report_wo")
        self.assertTrue(auth["rx_only"])
        self.assertEqual(auth["protocol_version"], 2)
        self.assertNotIn("password", auth)
        self.assertNotIn("listener", auth)
        self.assertEqual(auth["version"], CLIENT_VERSION)
        self.assertEqual(CLIENT_VERSION, "KiwiSDR-FreeDV/0.1.22")


class ReporterConnectionTests(unittest.IsolatedAsyncioTestCase):
    async def test_online_requires_application_acceptance(self):
        sio = FakeSocketIO("accepted")
        auth = build_auth({"callsign": "ZL2ABC", "grid_square": "RF80AA"})
        await connect_and_wait_for_acceptance(sio, "https://reporter.invalid", auth, timeout=0.1)
        self.assertTrue(sio.connected)

    async def test_server_disconnect_before_acceptance_is_an_error(self):
        sio = FakeSocketIO("rejected")
        auth = build_auth({"callsign": "ZL2ABC", "grid_square": "RF80AA"})
        with self.assertRaisesRegex(ConnectionError, "before acceptance"):
            await connect_and_wait_for_acceptance(sio, "https://reporter.invalid", auth, timeout=0.1)
        self.assertFalse(sio.connected)

    async def test_missing_ack_times_out_and_disconnects(self):
        sio = FakeSocketIO("timeout")
        auth = build_auth({"callsign": "ZL2ABC", "grid_square": "RF80AA"})
        with self.assertRaises(asyncio.TimeoutError):
            await connect_and_wait_for_acceptance(sio, "https://reporter.invalid", auth, timeout=0.01)
        self.assertFalse(sio.connected)


if __name__ == "__main__":
    unittest.main()
