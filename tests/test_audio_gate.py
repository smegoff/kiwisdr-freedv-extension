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
        self.assertIn(b"FreeDV v0.1.14", source)

    def test_help_modal_is_enabled_and_covers_every_mode(self) -> None:
        source = (WEB / "FreeDV.js").read_text(encoding="utf-8")
        help_callback = re.search(
            r"function FreeDV_help\(show\)(.*?)\n\}", source, re.DOTALL
        )
        self.assertIsNotNone(help_callback)
        self.assertIn("confirmation_show_scrolling_content", help_callback.group(1))
        self.assertIn("return true;", help_callback.group(1))
        for mode in ("1600", "700C", "700D", "700E", "2400A", "2400B", "800XA"):
            self.assertIn(mode, help_callback.group(1))

    def test_reference_mode_exercises_external_decoder_and_never_reports(self) -> None:
        server = SERVER.read_text(encoding="utf-8")
        browser = (WEB / "FreeDV.js").read_text(encoding="utf-8")
        self.assertIn('DIR_CFG "/samples/FreeDV.test.au"', server)
        self.assertIn("ext_register_receive_real_samps(freedv_test_audio", server)
        self.assertIn('"SET freedv_test=%d mode=%15s"', server)
        self.assertIn('e->test? "true":"false"', server)
        self.assertIn('cfg_true("freedv.reporter_enabled") && !e->test', server)
        self.assertIn("freedv_status_running(end + 1)", server)
        self.assertIn("e->test_sample = NULL;", server)
        self.assertIn("u2_t value = (u2_t) *e->test_sample;", server)
        self.assertIn("e->test_sample++;", server)
        self.assertIn("FLIP16(value)", server)
        self.assertNotIn("FLIP16(*e->test_sample++)", server)
        self.assertIn("if (progress_sent) return;", server)
        self.assertIn("e->input_rate = (u4_t) (ext_update_get_sample_rateHz(rx_chan) + 0.5);", server)
        self.assertIn("e->input_rate, (unsigned long long) e->frequency_hz", server)
        self.assertNotIn("(u4_t) ext_update_get_sample_rateHz(active_rx)", server)
        self.assertIn('"EXT test_pct=100 test_done"', server)
        self.assertIn("function freedv_test_cb()", browser)
        self.assertIn("freedv.mode = '700D'", browser)
        self.assertIn("freedv.test_synced && freedv.test_audio", browser)
        self.assertIn("+status.decoded_frames > 0", browser)
        self.assertIn("last_test_result", browser)
        self.assertIn("w3_innerHTML('id-freedv-reporter', 'disabled')", browser)
        self.assertIn("freedv.running? (status.reporter || 'disabled') : 'disabled'", browser)
        self.assertIn('{"decoded_frames", job_decoded_frames_}', DECODER.read_text(encoding="utf-8"))

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
