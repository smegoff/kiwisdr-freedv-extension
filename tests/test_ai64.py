import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class Ai64LocalDecoderTest(unittest.TestCase):
    def test_loopback_configuration_enables_rade_without_exposing_secret(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "kiwi.json"
            config.write_text(
                json.dumps(
                    {
                        "freedv": {
                            "shared_secret": "must-be-removed",
                            "decoder_host": "old.example",
                            "rade_enabled": False,
                        }
                    }
                ),
                encoding="utf-8",
            )
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools" / "configure-kiwi-freedv.py"),
                    "127.0.0.1",
                    "--enable-rade",
                    "--config",
                    str(config),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            result = json.loads(config.read_text(encoding="utf-8"))
            freedv = result["freedv"]
            self.assertEqual(freedv["decoder_ip"], "127.0.0.1")
            self.assertTrue(freedv["rade_enabled"])
            self.assertFalse(freedv["reporter_enabled"])
            self.assertNotIn("shared_secret", freedv)
            self.assertNotIn("decoder_host", freedv)
            self.assertEqual(len(list(Path(directory).glob("kiwi.json.pre-freedv-*"))), 1)

    def test_installer_has_hardware_and_offline_headroom_gates(self):
        installer = (ROOT / "deploy" / "install-ai64-local.sh").read_text(encoding="utf-8")
        self.assertIn('BeagleBone AI-64', installer)
        self.assertIn('$(uname -m) == aarch64', installer)
        self.assertIn("cpus >= 2", installer)
        self.assertIn("mem_kib >= 3145728", installer)
        self.assertIn("free_kib >= 3145728", installer)
        self.assertIn('FREEDV_KIWI_HOST=127.0.0.1', installer)
        self.assertIn('FREEDV_DASHBOARD_BIND=127.0.0.1', installer)
        self.assertIn('FREEDV_DASHBOARD_WATERFALL_FPS=5', installer)
        self.assertIn('FREEDV_ENABLE_RADE=0', installer)
        self.assertIn('test-radev1-load.sh" "$src" 1 20', installer)
        self.assertIn('offline-pass', installer)

    def test_activation_is_zero_listener_gated_and_self_rolling_back(self):
        activation = (ROOT / "tools" / "activate-ai64-local.sh").read_text(encoding="utf-8")
        self.assertGreaterEqual(activation.count('$(users) == 0'), 2)
        self.assertIn('secret_hash', activation)
        self.assertIn('cp -a "$config" "$backup/kiwi.json"', activation)
        self.assertIn('trap restore ERR', activation)
        self.assertIn('configure-kiwi-freedv.py" 127.0.0.1 --enable-rade', activation)
        self.assertIn('"status":"ok"', activation)
        self.assertIn('"kiwi_connected":true', activation)

    def test_live_soak_enforces_resource_and_reliability_gates(self):
        soak = (ROOT / "tools" / "validate-ai64-local.sh").read_text(encoding="utf-8")
        self.assertIn('duration -ge 60', soak)
        self.assertIn('session.get("mode") == "RADEV1"', soak)
        self.assertIn('average CPU exceeded 80 percent', soak)
        self.assertIn('peak CPU exceeded 95 percent', soak)
        self.assertIn('temperature exceeded 85 C', soak)
        self.assertIn('freedv_dropped_frames_total', soak)
        self.assertIn('freedv_reconnects_total', soak)
        self.assertIn('NRestarts', soak)
        self.assertIn('audio.*sequence', soak)
        self.assertIn('live-pass', soak)

    def test_rollback_restricts_backup_path_and_health_checks_kiwi(self):
        rollback = (ROOT / "tools" / "rollback-ai64-local.sh").read_text(encoding="utf-8")
        self.assertIn('"$backup_root"/activate-*', rollback)
        self.assertIn('refusing backup outside', rollback)
        self.assertIn('Kiwi listeners are active', rollback)
        self.assertIn('systemctl stop freedv-decoder.service freedv-reporter.service', rollback)
        self.assertIn('status=active', rollback)

    def test_ai64_service_override_reserves_kiwi_headroom(self):
        override = (ROOT / "deploy" / "freedv-decoder-ai64.conf").read_text(encoding="utf-8")
        self.assertIn("CPUQuota=140%", override)
        self.assertIn("CPUWeight=20", override)
        self.assertIn("MemoryHigh=1024M", override)
        self.assertIn("MemoryMax=1536M", override)
        self.assertIn("IOSchedulingClass=idle", override)

    def test_shell_scripts_parse(self):
        bash = shutil.which("bash")
        if not bash:
            git_bash = Path("C:/Program Files/Git/bin/bash.exe")
            if git_bash.exists():
                bash = str(git_bash)
        if not bash:
            self.skipTest("bash is not installed on this workstation")
        for relative in (
            "deploy/install-ai64-local.sh",
            "tools/activate-ai64-local.sh",
            "tools/validate-ai64-local.sh",
            "tools/rollback-ai64-local.sh",
        ):
            subprocess.run([bash, "-n", str(ROOT / relative)], check=True)


if __name__ == "__main__":
    unittest.main()
