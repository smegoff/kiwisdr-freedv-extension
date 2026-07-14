# KiwiSDR FreeDV extension

This project adds receive-only FreeDV decoding to a KiwiSDR without running
Codec2 on the Kiwi's single-core AM335x processor. It follows John Seamons'
existing TDoA/camper model: a private Debian service connects outbound to the
Kiwi, camps on the selected receiver's normal sound stream, decodes it, and
returns audio through the Kiwi's `rev_bin` path. Status returns through
`rev_txt`. Browsers use the standard Kiwi audio stream and never connect to the
decoder host.

The production milestone targets FreeDV 1600, 700C, 700D, 700E, 2400A,
2400B and 800XA through libcodec2. All seven are present in the selector,
protocol allow-list and decoder backend. This is not yet a claim that all
seven have passed live-RF acceptance: 700D has passed the end-to-end transport
test, live speech tests remain outstanding, and 2400A/2400B still need their
48 kHz and VHF/FM receive paths completed. See [docs/modes.md](docs/modes.md)
for the waveforms, intended use and exact readiness of each mode.

RADE remains disabled. FreeDV Reporter is implemented as an optional, RX-only
station presence. There is one global FreeDV session.

The live Kiwi and CT decoder are on `freedv-v0-1-5`. A real browser
`Start -> Stop -> Start -> Close` cycle passed on the same receiver channel,
including correct uncamping and re-camping, followed by 41-sample Kiwi and CT
stability soaks. Reporter settings have since been enabled by the owner, but
the live sidecar still requires deployment of the repository's asynchronous
Socket.IO dependency correction before its first controlled online test.

## Safety model

- Kiwi firmware remains 1.901 and every candidate is an atomic versioned
  release with automatic service, `/status`, and root-HTML rollback checks.
- `baseline-1.901` and streamed configuration archives are retained.
- CT 112 was snapshotted as `pre-freedv-v0-1-5` before the decoder upgrade.
- The shared 256-bit secret exists only in root-readable environment files.
- CT 112 makes the only decoder connection. There is no browser-facing or
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

## Repository layout

- `decoder/` - C++17 Kiwi camper client, resampler and Codec2/RADE adapters
- `reporter/` - isolated RX-only Reporter sidecar
- `kiwi-overlay/` - FreeDV server/client overlay pinned to Kiwi commit
  `417e2c8add196e879b8cc4eb4a488b35b4bf0df7`
- `deploy/` - CT firewall, environment and systemd definitions
- `tools/` - backup, overlay, build, atomic deployment and rollback tooling
- `docs/` - architecture, protocol, deployment evidence and recovery runbooks
