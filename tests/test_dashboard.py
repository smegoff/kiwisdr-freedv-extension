from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DashboardAssetTest(unittest.TestCase):
    def test_frontend_is_dependency_free_and_neutral(self):
        html = (ROOT / "dashboard" / "index.html").read_text(encoding="utf-8")
        css = (ROOT / "dashboard" / "styles.css").read_text(encoding="utf-8")
        js = (ROOT / "dashboard" / "app.js").read_text(encoding="utf-8")
        combined = "\n".join((html, css, js)).lower()
        self.assertNotIn("http://", combined)
        self.assertNotIn("https://", combined)
        self.assertNotIn("@import", combined)
        self.assertNotIn("gradient(", combined)
        self.assertNotIn("cdn", combined)
        self.assertIn("system-ui", css)
        self.assertIn('palette:"cividis"', js)
        self.assertIn('value="openwebrx-turbo"', html)
        self.assertIn('value="openwebrx-classic"', html)
        self.assertIn('value="openwebrx-ha7ilm"', html)
        self.assertIn('palettes["openwebrx-turbo"]', js)
        self.assertIn('"openwebrx-classic"', js)
        self.assertIn('"openwebrx-ha7ilm"', js)
        self.assertIn("length:256", js)
        self.assertIn('document.addEventListener("visibilitychange"', js)
        self.assertIn("Math.min(devicePixelRatio||1,2)", js)

    def test_openwebrx_palettes_match_built_in_schemes(self):
        js = (ROOT / "dashboard" / "app.js").read_text(encoding="utf-8")
        turbo = re.search(r'openWebRxTurboHex = "([0-9a-f]+)"', js)
        self.assertIsNotNone(turbo)
        self.assertEqual(len(turbo.group(1)), 256 * 6)
        self.assertTrue(turbo.group(1).startswith("30123b311542"))
        self.assertTrue(turbo.group(1).endswith("8106027e05027a0402"))
        self.assertIn('"openwebrx-classic":[[0,[0,0,0]]', js)
        self.assertIn('[1,[255,255,255]]]', js)
        self.assertIn('"openwebrx-ha7ilm":[[0,[0,0,0]]', js)
        self.assertIn('[1,[178,0,0]]]', js)

    def test_installer_and_upgrade_rely_on_management_firewall(self):
        installer = (ROOT / "deploy" / "install-decoder.sh").read_text(encoding="utf-8")
        environment = (ROOT / "deploy" / "decoder.env.example").read_text(encoding="utf-8")
        upgrade = (ROOT / "tools" / "deploy-decoder-release.sh").read_text(encoding="utf-8")
        firewall = (ROOT / "deploy" / "freedv-decoder.fw.example").read_text(encoding="utf-8")
        self.assertIn("libfftw3-dev", installer)
        self.assertNotIn("dashboard.token", installer)
        self.assertNotIn("FREEDV_DASHBOARD_TOKEN_FILE", environment)
        self.assertIn("FREEDV_DASHBOARD_PORT=8076", environment)
        self.assertIn("dashboard-assets", upgrade)
        self.assertIn("-dport 8076", firewall)

    def test_dashboard_api_is_lan_open_and_hardened(self):
        source = (ROOT / "decoder" / "src" / "dashboard.cpp").read_text(encoding="utf-8")
        self.assertIn("/api/v1/status", source)
        self.assertIn("/api/v1/history", source)
        self.assertIn("/api/v1/stream", source)
        self.assertNotIn("/api/v1/login", source)
        self.assertNotIn("/api/v1/logout", source)
        self.assertNotIn("http::status::unauthorized", source)
        self.assertIn('"Content-Security-Policy"', source)
        self.assertNotIn("Access-Control-Allow-Origin", source)


if __name__ == "__main__":
    unittest.main()
