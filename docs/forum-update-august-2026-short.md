# FreeDV extension update — August 2026

Since my last update at the end of July, the KiwiSDR FreeDV project has moved
to extension **v0.1.38** and external decoder **v0.1.26**. This is running on a
normal AM335x KiwiSDR with all Codec2/RADE processing offloaded to a private
Debian guest under Proxmox. It is not an AI-64 installation.

Main changes:

- Fixed broken/stuttering speech caused by decoded modem frames returning to
  the Kiwi in bursts. PCM is now paced to the normal Kiwi audio cadence using a
  bounded queue, with stale audio discarded instead of accumulated.
- Improved camper authentication, startup and recovery. The decoder now detects
  a connected-but-stale control socket and reconnects automatically.
- Added deterministic **Test 700D** and **Test RADE** signals. These exercise
  the real Kiwi sound channel, external decoder and standard returned-audio
  path—not browser-only sample playback.
- Added experimental **RADEV1** using the portable `freedv/rade_c` C
  implementation, not the original Python decoder. RADE remains feature-gated
  and runs on the external guest.
- Tested both references as an ordinary listener through John's public
  `proxy.kiwisdr.com` service. Public browsers still connect only to the Kiwi;
  the private decoder and management services are not exposed.
- Improved RX-only FreeDV Reporter support. Normal sessions publish the
  selected codec as RX Mode, while reference tests are excluded.
- Added a manual frequency field, automatic amateur LSB/USB selection, common
  FreeDV calling frequencies and currently advertised Reporter frequencies.
  Live entries are marked **[Reporter live]**.
- Changed the recommended receiver path to flat 300–3000 Hz. Opening FreeDV
  temporarily disables the Kiwi noise filter/denoiser/autonotch and restores
  the listener's settings on close. The noise blanker is unchanged.
- Reworked panel spacing and expanded Help for modes, filters, calling
  frequencies, tests, Reporter and RADE.
- Added a lightweight read-only LAN diagnostics dashboard with audio-band
  waterfall/spectrum, SNR and offset history, session state and modem counters.
  Its link is shown only to local Kiwi users, not public proxy listeners.
- Strengthened firmware build checks, streamed configuration backups, atomic
  release activation and automatic rollback.

The clean 700D and RADEV1 references now pass end to end with sync, continuous
returned PCM and zero dropped frames. Live-RF validation is still being recorded
mode by mode. RADEV1 remains experimental, and 2400A/B need the appropriate
48 kHz VHF/FM receive path.

FreeDV v0.1.38 running Test 700D:

![FreeDV extension](https://raw.githubusercontent.com/smegoff/kiwisdr-freedv-extension/main/docs/images/freedv-extension-v0.1.38.png)

Synchronized external-decoder dashboard:

![Decoder diagnostics](https://raw.githubusercontent.com/smegoff/kiwisdr-freedv-extension/main/docs/images/decoder-dashboard-v0.1.26.png)

Code, installation notes and full test details:
https://github.com/smegoff/kiwisdr-freedv-extension
