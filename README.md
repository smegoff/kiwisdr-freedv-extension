# KiwiSDR FreeDV extension

This project adds receive-only FreeDV decoding to a KiwiSDR without running
Codec2 on the Kiwi's single-core AM335x processor. It follows John Seamons'
existing TDoA/camper model: a private Debian service connects outbound to the
Kiwi, camps on the selected receiver's normal sound stream, decodes it, and
returns audio through the Kiwi's `rev_bin` path. Status returns through
`rev_txt`. Browsers use the standard Kiwi audio stream and never connect to the
decoder host.

While a FreeDV job is running, the extension owns the receiver audio path:
ordinary SSB/static audio is replaced with silence until decoded FreeDV PCM is
available, including during decoder gaps or outages. Stop or Close restores the
normal Kiwi audio stream immediately.

The production milestone targets FreeDV 1600, 700C, 700D, 700E, 2400A,
2400B and 800XA through libcodec2. All seven are present in the selector,
protocol allow-list and decoder backend. This is not yet a claim that all
seven have passed live-RF acceptance: 700D has passed the end-to-end transport
test, live speech tests remain outstanding, and 2400A/2400B still need their
48 kHz and VHF/FM receive paths completed. See [docs/modes.md](docs/modes.md)
for the waveforms, intended use and exact readiness of each mode.

RADEV1 is implemented with the official portable C library and FARGAN speech
synthesizer. It is independently gated by the decoder environment and Kiwi
administrator, and is hidden from the selector until both the reviewed build
and the site opt-in are ready. FreeDV Reporter is an optional, RX-only station
presence. There is one global FreeDV session.

The tested reference installation runs Kiwi extension `0.1.21` with decoder
service `0.1.19`. Here and throughout the documentation, **decoder guest** means the
private Debian VM or unprivileged LXC that performs FreeDV decoding; its local
hypervisor ID is not part of the architecture. A real browser verified the
operator-facing features:

- The panel visibly acknowledges the FreeDV project and links to
  [freedv.org](https://freedv.org/).
- Help opens a mode guide covering all seven selectable modes.
- The extension follows the amateur voice convention automatically: LSB on
  160, 80 and 40 metres, the 60-metre USB exception, and USB at 10 MHz and above.
  It also selects a mode-specific receiver filter from the documented FreeDV
  occupied bandwidth with tuning headroom on both edges.
- A calling-frequency selector tunes 18 common FreeDV activity frequencies,
  applies the listed sideband, and supports QO-100 when the Kiwi has a suitable
  downconverter/transverter frequency offset configured.
- Test decodes John's bundled 700D recording end to end and reached
  `100% / test passed` with returned Codec2 audio and zero dropped frames.
- Test readiness uses the authenticated control channel after the camper
  acknowledgement, avoiding a `Test: 0%` deadlock if early returned status is
  lost during the monitor-to-sound transition. A 15-second UI timeout reports
  a clear failure if the reference signal still cannot arm.
- A normal 700D session reached the external Codec2 backend and published the
  RX-only station on FreeDV Reporter at the receiver's tuned frequency. The
  sidecar also recovered its listing after a forced process restart; Stop
  returned it to disabled and removed the presence.
- A running, unsynchronized session owns the Kiwi audio path and receives zero
  PCM instead of analogue/static audio. Stop or Close restores normal audio.
- RADEV1 appeared only after the admin gate was enabled, started through the
  normal Kiwi camper path with backend `rade-v1`, brought Reporter online, and
  stopped with the camper/session and Reporter presence fully removed.
- Decoder health now follows the real Kiwi control-loop heartbeat. A wedged
  WebSocket can no longer look healthy just because a separate watchdog thread
  is alive; the process exits and systemd reconnects it automatically.

The browser tests were followed by independent 41-sample Kiwi and decoder-guest
stability soaks at 15-second intervals. All 82 checks passed. A deliberate
decoder hang was also recovered automatically in about 33 seconds before the
soak.

## Safety model

- Kiwi firmware remains 1.901 and every candidate is an atomic versioned
  release with automatic service, `/status`, and root-HTML rollback checks.
- `baseline-1.901` and streamed configuration archives are retained.
- The reference decoder guest was snapshotted as `pre-reporter-v0-1-18` before
  the current backend upgrade; the earlier RADEV1 and v0.1.16 snapshots are
  also retained.
- The shared 256-bit secret exists only in root-readable environment files.
- The decoder guest makes the only decoder connection. There is no browser-facing or
  public decoder listener.
- A physical eMMC recovery still requires a supported backup microSD card.

See [docs/protocol.md](docs/protocol.md) for the transport,
[docs/installation.md](docs/installation.md) for a safety-gated installation,
[docs/external-decoder-vm.md](docs/external-decoder-vm.md) for why the decoder
runs on external compute and how to create a Proxmox VM or LXC,
[docs/deployment-status.md](docs/deployment-status.md) for live state, and
[docs/rollback.md](docs/rollback.md) for recovery.

## Installation overview

Installation has two independently reversible parts:

1. Install the C++ decoder and optional Reporter sidecar on a private Debian 12
   LXC or VM.
2. Apply the pinned overlay to John Seamons' KiwiSDR source, build a production
   `kiwid.bin`, and activate it through the atomic rollback gate.

Both systems use the same generated 256-bit secret. The decoder initiates the
only connection, outbound to the Kiwi's normal port; no decoder service is
published to browsers or the Internet. Back up the Kiwi and snapshot the
decoder guest before activation.

The complete commands, configuration fields, verification checks and rollback
procedure are in [docs/installation.md](docs/installation.md). Do not run the
site-specific Proxmox or backup scripts unchanged on another installation;
review their node, storage, address, MAC and SSH host-key defaults first.

The current production decoder is an unprivileged Proxmox LXC. A full VM is an
architecturally supported deployment choice and provides stronger kernel
isolation at the cost of modest additional memory and disk overhead; current
live acceptance evidence is for the LXC path. The comparison and VM creation
procedure are in
[docs/external-decoder-vm.md](docs/external-decoder-vm.md).

## Release publication gate

A deployment is not complete until its matching source, tests and public
documentation are committed and pushed to GitHub. The tracked branch must be
clean and synchronized with its upstream branch, and the open pull request must
record the deployed version, validation and rollback point. Secrets, private
configuration archives and operational logs remain in ignored storage and are
never published.

## Repository layout

- `decoder/` - C++17 Kiwi camper client, resampler and Codec2/RADE adapters
- `reporter/` - isolated RX-only Reporter sidecar
- `kiwi-overlay/` - FreeDV server/client overlay pinned to Kiwi commit
  `417e2c8add196e879b8cc4eb4a488b35b4bf0df7`
- `deploy/` - decoder-guest provisioning, firewall, environment and systemd definitions
- `tools/` - backup, overlay, build, atomic deployment and rollback tooling
- `docs/` - architecture, protocol, deployment evidence and recovery runbooks
