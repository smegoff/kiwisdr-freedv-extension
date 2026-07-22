import ast
from pathlib import Path
import shutil
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
INSTALLER = ROOT / "tools" / "install-freedv.py"


class OneShotInstallerTest(unittest.TestCase):
    def run_installer(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(INSTALLER), *arguments],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_remote_dry_run_requires_explicit_safety_inputs(self):
        result = self.run_installer(
            "--mode", "remote",
            "--decoder-ip", "192.0.2.10",
            "--kiwi-ip", "192.0.2.20",
            "--decoder-ssh", "root@decoder.example",
            "--skip-remote-install",
            "--decoder-rollback-ready",
            "--backup-sd-present",
            "--acknowledge-risk",
            "--yes",
            "--dry-run",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("RISK NOTICE", result.stdout)
        self.assertIn("decoder mode: remote", result.stdout)
        self.assertIn("DRY RUN", result.stdout)
        self.assertIn("Reporter: disabled", result.stdout)
        self.assertIn("RADEV1: disabled", result.stdout)

    def test_local_reduced_recovery_requires_independent_copy(self):
        result = self.run_installer(
            "--mode", "local",
            "--accept-reduced-recovery",
            "--acknowledge-risk",
            "--yes",
            "--dry-run",
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("requires --backup-copy", result.stderr)

    def test_noninteractive_run_requires_risk_acknowledgement(self):
        result = self.run_installer(
            "--mode", "local",
            "--backup-sd-present",
            "--yes",
            "--dry-run",
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("--acknowledge-risk is required", result.stderr)

    def test_installer_parses_as_python_39(self):
        ast.parse(INSTALLER.read_text(encoding="utf-8"), feature_version=(3, 9))

    def test_os_detection_and_debian_11_codec2_fallback_are_explicit(self):
        installer = INSTALLER.read_text(encoding="utf-8")
        decoder_install = (ROOT / "deploy" / "install-decoder.sh").read_text(encoding="utf-8")
        codec2_build = (ROOT / "deploy" / "build-codec2.sh").read_text(encoding="utf-8")
        remote_install = (ROOT / "deploy" / "install-remote-decoder.sh").read_text(encoding="utf-8")
        self.assertIn('values.get("VERSION_ID") not in {"11", "12"}', installer)
        self.assertIn("Detected Kiwi operating system", installer)
        self.assertIn("decoder clock differs", installer)
        self.assertIn("pkg-config --atleast-version=1.0.5 codec2", decoder_install)
        self.assertIn("expected on Debian 11", decoder_install)
        self.assertIn("310777b1c6f1af0bc7c72f5b32f80f6fd9136962", codec2_build)
        self.assertIn("FREEDV_MODE_700E", codec2_build)
        self.assertIn("reliable_text.h", codec2_build)
        self.assertIn("debian_major == 11", remote_install)
        self.assertIn("debian_major == 12", remote_install)
        self.assertIn("x86_64", remote_install)
        self.assertIn("aarch64", remote_install)
        self.assertIn("free_kib >= 3145728", remote_install)

    def test_guardrails_cover_backup_zero_users_and_both_rollbacks(self):
        installer = INSTALLER.read_text(encoding="utf-8")
        backup = (ROOT / "tools" / "backup-kiwi-on-device.sh").read_text(encoding="utf-8")
        remote_rollback = (ROOT / "deploy" / "rollback-remote-decoder.sh").read_text(encoding="utf-8")
        self.assertGreaterEqual(installer.count("zero_listener_gate()"), 2)
        self.assertIn("backup-kiwi-on-device.sh", installer)
        self.assertIn("rollback-kiwi-release.sh", installer)
        self.assertIn("wait_kiwi_health()", installer)
        self.assertIn("rollback_prepared_decoder", installer)
        self.assertIn("commit_prepared_decoder", installer)
        self.assertIn("kiwi-recovery.tgz", backup)
        self.assertIn("tar -tzf", backup)
        self.assertIn("unsafe backup path in marker", remote_rollback)
        self.assertIn("pre-install.tgz", remote_rollback)

    def test_reporter_is_forced_off_during_initial_acceptance(self):
        for relative in ("deploy/install-remote-decoder.sh", "deploy/install-ai64-local.sh"):
            content = (ROOT / relative).read_text(encoding="utf-8")
            self.assertIn("FREEDV_REPORTER_ENABLED=0", content)

    def test_unique_release_labels_use_embedded_extension_version(self):
        verifier = (ROOT / "tools" / "verify-kiwi-candidate.sh").read_text(encoding="utf-8")
        self.assertIn("FREEDV_RELEASE", verifier)
        self.assertNotIn("echo \"$release\" | sed", verifier)

    def test_official_update_cannot_leave_a_stale_rollback_target(self):
        deployment = (ROOT / "tools" / "deploy-kiwi-release.sh").read_text(encoding="utf-8")
        self.assertIn("official Kiwi update replaces /usr/local/bin/kiwid", deployment)
        self.assertIn("byte-for-byte identical", deployment)
        self.assertIn("previous=baseline-1.902", deployment)
        self.assertIn("sha256sum /usr/local/bin/kiwid", deployment)

    def test_new_shell_scripts_parse(self):
        bash = shutil.which("bash")
        if not bash:
            git_bash = Path("C:/Program Files/Git/bin/bash.exe")
            if git_bash.exists():
                bash = str(git_bash)
        if not bash:
            self.skipTest("bash is not installed on this workstation")
        for relative in (
            "deploy/build-codec2.sh",
            "deploy/install-remote-decoder.sh",
            "deploy/rollback-remote-decoder.sh",
            "deploy/install-ai64-local.sh",
            "deploy/rollback-ai64-preparation.sh",
            "tools/backup-kiwi-on-device.sh",
            "tools/deploy-kiwi-release.sh",
            "tools/run-kiwi-build.sh",
            "tools/verify-kiwi-candidate.sh",
        ):
            subprocess.run([bash, "-n", str(ROOT / relative)], check=True)


if __name__ == "__main__":
    unittest.main()
