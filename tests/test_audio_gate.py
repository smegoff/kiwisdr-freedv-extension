#!/usr/bin/env python3
"""Regression checks for the FreeDV-only receiver audio gate."""

from __future__ import annotations

import gzip
import pathlib
import re
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
PATCH = ROOT / "kiwi-overlay" / "patches" / "0003-silence-return-audio.patch"
SERVER = ROOT / "kiwi-overlay" / "extensions" / "FreeDV" / "freedv.cpp"
DECODER = ROOT / "decoder" / "src" / "main.cpp"
WEB = ROOT / "kiwi-overlay" / "web" / "extensions" / "FreeDV"


class AudioGateTest(unittest.TestCase):
    def test_running_job_replaces_receiver_pcm_with_silence(self) -> None:
        patch = PATCH.read_text(encoding="utf-8")
        self.assertIn("if (!wf->want_rtn_snd)", patch)
        self.assertIn("memset(((char *) &s->out_pkt_real) + header_bytes, 0,", patch)
        self.assertIn("app_to_web(c, (char *) &s->out_pkt_real, bytes);", patch)
        self.assertIn("sent_return_audio = true;", patch)

    def test_timeout_keeps_gate_but_stop_releases_it(self) -> None:
        server = SERVER.read_text(encoding="utf-8")
        poll = re.search(
            r"static void freedv_poll\(int rx_chan\)(.*?)\n\}", server, re.DOTALL
        )
        stop = re.search(
            r"static void freedv_stop\(int rx_chan\)(.*?)\n\}", server, re.DOTALL
        )
        self.assertIsNotNone(poll)
        self.assertIsNotNone(stop)
        self.assertIn("freedv_set_return_audio(rx_chan, true);", poll.group(1))
        self.assertNotIn("freedv_set_return_audio(rx_chan, false);", poll.group(1))
        self.assertIn("freedv_set_return_audio(rx_chan, false);", stop.group(1))

    def test_decoder_only_returns_synchronized_pcm(self) -> None:
        decoder = DECODER.read_text(encoding="utf-8")
        self.assertIn(
            "if (decoded.status.synced && !decoded.pcm.empty())", decoder
        )
        self.assertIn("else if (!decoded.status.synced)", decoder)

    def test_packaged_browser_asset_matches_source(self) -> None:
        source = (WEB / "FreeDV.min.js").read_bytes()
        packaged = gzip.decompress((WEB / "FreeDV.min.js.gz").read_bytes())
        self.assertEqual(source, packaged)
        self.assertIn(b"FreeDV v0.1.6", source)

    def test_patch_applies_to_pinned_upstream_when_available(self) -> None:
        upstream = ROOT / "upstream-kiwisdr"
        if not (upstream / ".git").exists():
            self.skipTest("ignored upstream KiwiSDR checkout is not present")
        subprocess.run(
            ["git", "-C", str(upstream), "apply", "--check", str(PATCH)],
            check=True,
        )


if __name__ == "__main__":
    unittest.main()
