#!/usr/bin/env python3
"""Regression checks for the FreeDV-only receiver audio gate."""

from __future__ import annotations

import gzip
import pathlib
import re
import subprocess
import tempfile
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
        self.assertIn(b"FreeDV v0.1.30", source)
        self.assertIn(b"Built with ", source)
        self.assertIn(b"https://freedv.org/", source)
        self.assertIn('#define FREEDV_RELEASE "0.1.30"', SERVER.read_text(encoding="utf-8"))

    def test_help_modal_is_enabled_and_covers_every_mode(self) -> None:
        source = (WEB / "FreeDV.js").read_text(encoding="utf-8")
        help_callback = re.search(
            r"function FreeDV_help\(show\)(.*?)\n\}", source, re.DOTALL
        )
        self.assertIsNotNone(help_callback)
        self.assertIn("confirmation_show_scrolling_content", help_callback.group(1))
        self.assertIn("return true;", help_callback.group(1))
        for mode in ("1600", "700C", "700D", "700E", "2400A", "2400B", "800XA", "RADEV1"):
            self.assertIn(mode, help_callback.group(1))
        self.assertIn("calling_frequencies.slice(1)", help_callback.group(1))
        self.assertIn("join('<br>')", help_callback.group(1))
        self.assertIn("FARGAN", help_callback.group(1))
        self.assertIn("8 kHz modem audio", help_callback.group(1))
        self.assertIn("16 kHz decoded speech", help_callback.group(1))
        self.assertIn(
            "https://github.com/smegoff/kiwisdr-freedv-extension",
            help_callback.group(1),
        )

    def test_receiver_sideband_and_mode_filter_profiles(self) -> None:
        source = (WEB / "FreeDV.js").read_text(encoding="utf-8")
        self.assertIn("if (freq_kHz >= 5250 && freq_kHz < 5450) return 'usb'", source)
        self.assertIn("return (freq_kHz < 10000)? 'lsb':'usb'", source)
        self.assertIn("if (ext_get_mode() != p.sideband) ext_set_mode(p.sideband)", source)
        self.assertIn("ext_set_passband(p.low, p.high)", source)
        for mode, width in {
            "1600": 1125,
            "700C": 1500,
            "700D": 1000,
            "700E": 1500,
            "800XA": 2000,
            "RADEV1": 1500,
        }.items():
            self.assertRegex(source, rf"'{mode}':\s*{width}")
        self.assertIn("p.nominal_hz = 5000", source)
        self.assertIn("p.high = 5700", source)
        self.assertIn("analog FM audio (integration only)", source)
        self.assertIn("freedv.saved_passband = ext_get_passband()", source)
        self.assertIn(
            "ext_set_passband(freedv.saved_passband.low, freedv.saved_passband.high)",
            source,
        )

    def test_noise_filter_is_temporarily_disabled_and_restored(self) -> None:
        source = (WEB / "FreeDV.js").read_text(encoding="utf-8")
        force = re.search(
            r"function freedv_force_noise_filter_off\(\)(.*?)\n\}", source, re.DOTALL
        )
        restore = re.search(
            r"function freedv_restore_noise_filter\(\)(.*?)\n\}", source, re.DOTALL
        )
        blur = re.search(r"function FreeDV_blur\(\)(.*?)\n\}", source, re.DOTALL)
        self.assertIsNotNone(force)
        self.assertIsNotNone(restore)
        self.assertIsNotNone(blur)
        for value in ("noise_filter.algo", "noise_filter.denoise", "noise_filter.autonotch"):
            self.assertIn(value, force.group(1))
            self.assertIn(value, restore.group(1))
        self.assertIn("stored_algo: kiwi_storeRead('last_nr_algo')", force.group(1))
        self.assertIn("snd_send('SET nr algo='+ noise_filter.NR_OFF)", force.group(1))
        self.assertIn("w3_select_value('nr_algo', noise_filter.NR_OFF, { all:1 })", force.group(1))
        self.assertIn("snd_send('SET nr algo='+ noise_filter.algo)", restore.group(1))
        self.assertIn("w3_select_value('nr_algo', noise_filter.algo, { all:1 })", restore.group(1))
        self.assertIn("noise_filter_send(noise_filter.NR_DENOISE)", restore.group(1))
        self.assertIn("noise_filter_send(noise_filter.NR_AUTONOTCH)", restore.group(1))
        self.assertIn("freedv_restore_noise_filter()", blur.group(1))
        self.assertNotIn("noise_blank", force.group(1) + restore.group(1))

    def test_automatic_filter_tightens_once_and_has_manual_overrides(self) -> None:
        source = (WEB / "FreeDV.js").read_text(encoding="utf-8")
        self.assertIn("filter_modes: ['Auto (lock on sync)', 'Tight', 'Normal', 'Wide']", source)
        self.assertIn("filter_keys: ['auto', 'tight', 'normal', 'wide']", source)
        self.assertIn("if (key == 'tight') return 50", source)
        self.assertIn("if (key == 'wide') return 350", source)
        self.assertIn("if (key == 'auto' && freedv.filter_locked) return 50", source)
        self.assertIn("return 200", source)
        lock = re.search(
            r"function freedv_filter_sync\(synced\)(.*?)\n\}", source, re.DOTALL
        )
        self.assertIsNotNone(lock)
        self.assertIn("freedv_filter_key() != 'auto'", lock.group(1))
        self.assertIn("freedv.filter_locked", lock.group(1))
        self.assertIn("freedv.filter_locked = true", lock.group(1))
        self.assertIn("freedv_filter_sync(status.sync)", source)
        self.assertIn("function freedv_filter_cb(path, index, first)", source)
        self.assertIn("freedv.filter_locked = false", source)
        self.assertIn("Automatic filter mode", source)
        self.assertIn("The noise blanker is not changed", source)

    def test_common_calling_frequency_selector(self) -> None:
        source = (WEB / "FreeDV.js").read_text(encoding="utf-8")
        entries = (
            ("160 m - 1.870 MHz LSB", "1870", "lsb"),
            ("80 m - 3.625 MHz LSB", "3625", "lsb"),
            ("80 m - 3.643 MHz LSB", "3643", "lsb"),
            ("80 m - 3.693 MHz LSB", "3693", "lsb"),
            ("80 m - 3.697 MHz LSB", "3697", "lsb"),
            ("80 m - 3.803 MHz LSB", "3803", "lsb"),
            ("60 m - 5.4035 MHz USB", "5403.5", "usb"),
            ("60 m - 5.3685 MHz USB", "5368.5", "usb"),
            ("40 m - 7.177 MHz LSB", "7177", "lsb"),
            ("40 m - 7.197 MHz LSB", "7197", "lsb"),
            ("20 m - 14.236 MHz USB (most common)", "14236", "usb"),
            ("20 m - 14.240 MHz USB", "14240", "usb"),
            ("17 m - 18.118 MHz USB", "18118", "usb"),
            ("15 m - 21.313 MHz USB", "21313", "usb"),
            ("12 m - 24.933 MHz USB", "24933", "usb"),
            ("10 m - 28.330 MHz USB", "28330", "usb"),
            ("10 m - 28.720 MHz USB", "28720", "usb"),
            ("10 GHz QO-100 - 10489.640 MHz USB", "10489640", "usb"),
        )
        for label, khz, sideband in entries:
            self.assertIn(
                f"{{ label: '{label}', kHz: {khz}, sideband: '{sideband}' }}",
                source,
            )
        self.assertIn("function freedv_calling_frequency_cb(path, index, first)", source)
        self.assertIn("entry.kHz < range.lo_kHz || entry.kHz > range.hi_kHz", source)
        self.assertIn("entry.kHz - range.offset_kHz", source)
        self.assertIn("ext_tune(entry.kHz - range.offset_kHz, entry.sideband, ext_zoom.CUR)", source)
        self.assertIn("QO-100 requires a suitable downconverter/transverter", source)

    def test_radev1_requires_server_and_admin_feature_gate(self) -> None:
        server = SERVER.read_text(encoding="utf-8")
        browser = (WEB / "FreeDV.js").read_text(encoding="utf-8")
        self.assertIn('cfg_default_bool("freedv.rade_enabled", false', server)
        self.assertIn('strcmp(mode, "RADEV1") != 0 || cfg_true("freedv.rade_enabled")', server)
        self.assertIn('"EXT rade_enabled=%d reporter_enabled=%d ready', server)
        self.assertIn("legacy_modes: ['1600', '700C', '700D', '700E', '2400A', '2400B', '800XA']", browser)
        self.assertIn("if (freedv.rade_enabled) freedv.modes.push('RADEV1')", browser)
        self.assertIn("mode: 'RADEV1'", browser)
        self.assertIn("if (freedv.modes.indexOf(freedv.mode) < 0) freedv.mode = '700D'", browser)
        self.assertIn("'freedv.rade_enabled'", browser)

    def test_panel_uses_native_kiwi_type_and_spaced_sections(self) -> None:
        browser = (WEB / "FreeDV.js").read_text(encoding="utf-8")
        css = (WEB / "FreeDV.css").read_text(encoding="utf-8")
        for section in (
            "id-freedv-intro",
            "id-freedv-actions",
            "id-freedv-calling",
            "id-freedv-radio-info",
            "id-freedv-status",
            "id-freedv-footer",
        ):
            self.assertIn(section, browser)
            self.assertIn(f".{section}", css)
        self.assertNotIn("font-family", css)
        self.assertIn("line-height:1.25", css)
        self.assertIn("border-top:1px solid #777", css)

    def test_reference_mode_exercises_external_decoder_and_never_reports(self) -> None:
        server = SERVER.read_text(encoding="utf-8")
        browser = (WEB / "FreeDV.js").read_text(encoding="utf-8")
        self.assertIn('DIR_CFG "/samples/FreeDV.test.au"', server)
        self.assertIn("ext_register_receive_real_samps(freedv_test_audio", server)
        self.assertIn('"SET freedv_test=%d mode=%15s"', server)
        self.assertIn('e->test? "true":"false"', server)
        self.assertIn('\\"test_ready\\":%s', server)
        self.assertIn("bool test_job_seen;", server)
        self.assertIn("arm_test_after_response", server)
        self.assertIn("test_ready? \"true\":\"false\"", server)
        self.assertIn("job_e->test_sample = test_signal.samples;", server)
        self.assertIn("job_e->test_job_seen = true;", server)
        self.assertIn("job_.test && !job_.test_ready", DECODER.read_text(encoding="utf-8"))
        waiting = re.search(
            r"if \(job_\.test && !job_\.test_ready\) \{(.*?)\n    \}",
            DECODER.read_text(encoding="utf-8"),
            re.DOTALL,
        )
        self.assertIsNotNone(waiting)
        self.assertIn("send_status({});", waiting.group(1))
        self.assertIn("std::chrono::milliseconds(250)", waiting.group(1))
        self.assertIn("maybe_poll();", waiting.group(1))
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
        self.assertIn("freedv.test_arm_timer = setTimeout", browser)
        self.assertIn("within 15 seconds", browser)
        self.assertIn("freedv_test_mark_armed()", browser)
        self.assertIn("status.state == 'running'", browser)
        self.assertIn("freedv.test_progress_timer = setTimeout", browser)
        self.assertIn("did not start within 5 seconds", browser)
        self.assertIn("freedv_clear_test_timers()", browser)
        self.assertIn("reporter_enabled=%d", server)
        self.assertIn("enabled (idle)", browser)
        self.assertIn("enabled (test excluded)", browser)
        self.assertIn("freedv_update_reporter_state(status.reporter)", browser)
        self.assertIn('{"decoded_frames", job_decoded_frames_}', DECODER.read_text(encoding="utf-8"))

    def test_health_and_watchdog_follow_the_decoder_main_loop(self) -> None:
        decoder = DECODER.read_text(encoding="utf-8")
        self.assertIn("main_loop_heartbeat", decoder)
        self.assertIn("main_loop_age_seconds", decoder)
        self.assertIn("main loop stalled for", decoder)
        self.assertIn("_exit(4);", decoder)
        self.assertIn("http::status::service_unavailable", decoder)
        self.assertIn("freedv_status_updates_total", decoder)

    def test_authenticated_camper_routes_freedv_status_directly(self) -> None:
        patch = PATCH.with_name("0004-freedv-direct-status-relay.patch").read_text(
            encoding="utf-8"
        )
        server = SERVER.read_text(encoding="utf-8")
        self.assertIn(
            "if (freedv_receive_cmds(CMD_NO_KEY, cmd, camped_rx)) continue;",
            patch,
        )
        self.assertIn("FreeDV owns rev_txt", patch)
        self.assertIn("freedv_status_diag", server)
        self.assertIn('"stale-generation"', server)
        self.assertIn('"accepted"', server)

    def test_public_docs_use_portable_decoder_guest_terminology(self) -> None:
        public_paths = [ROOT / "README.md", *(ROOT / "docs").glob("*.md")]
        public_text = "\n".join(path.read_text(encoding="utf-8") for path in public_paths)
        self.assertNotIn("CT 112", public_text)
        self.assertNotIn("set-ct-radev1", public_text)
        self.assertIn("**decoder guest**", public_text)
        self.assertIn("## Release publication gate", public_text)
        self.assertFalse((ROOT / "deploy" / "112.fw").exists())
        self.assertTrue((ROOT / "deploy" / "freedv-decoder.fw.example").exists())
        self.assertTrue((ROOT / "tools" / "soak-decoder-guest.sh").exists())

    def test_patch_applies_to_pinned_upstream_when_available(self) -> None:
        upstream = ROOT / "upstream-kiwisdr"
        if not (upstream / ".git").exists():
            self.skipTest("ignored upstream KiwiSDR checkout is not present")
        commit = "c40ecb471dced33689e335689f8ffd35a54f47fa"
        with tempfile.TemporaryDirectory() as directory:
            worktree = pathlib.Path(directory) / "kiwi-v1902"
            subprocess.run(
                ["git", "-C", str(upstream), "worktree", "add", "--detach", str(worktree), commit],
                check=True,
                capture_output=True,
            )
            try:
                for patch in (
                    ROOT / "kiwi-overlay" / "patches" / "0001-publish-freedv.patch",
                    ROOT / "kiwi-overlay" / "patches" / "0002-freedv-monitor-control.patch",
                    ROOT / "kiwi-overlay" / "patches" / "0004-freedv-direct-status-relay.patch",
                    PATCH,
                ):
                    subprocess.run(
                        ["git", "-C", str(worktree), "apply", "--check", str(patch)],
                        check=True,
                    )
            finally:
                subprocess.run(
                    ["git", "-C", str(upstream), "worktree", "remove", "--force", str(worktree)],
                    check=True,
                    capture_output=True,
                )


if __name__ == "__main__":
    unittest.main()
