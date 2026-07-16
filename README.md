# KiwiSDR FreeDV Extension

Receive-only [FreeDV](https://freedv.org/) decoding for KiwiSDR, with the modem
workload offloaded to a private Debian VM or unprivileged LXC.

The project follows the KiwiSDR TDoA/camper design. In this documentation,
**decoder guest** means the private Debian VM or unprivileged LXC that performs
FreeDV decoding. It connects outbound to the Kiwi, camps on the selected
receiver's sound stream, decodes FreeDV, and returns speech through Kiwi's
standard audio path. Browsers never connect directly to the decoder guest.

> [!IMPORTANT]
> This project is beta software built against KiwiSDR firmware 1.901 and
> upstream commit `417e2c8add196e879b8cc4eb4a488b35b4bf0df7`. Back up the
> Kiwi and create a decoder-guest rollback point before installation.

## Features

- FreeDV extension available in the ordinary KiwiSDR extension menu.
- External Codec2 decoding keeps modem CPU load off the Kiwi's AM335x.
- Decoded speech returns through the normal Kiwi audio stream.
- Analogue noise and static remain silent while FreeDV is running but not
  synchronized; Stop or Close restores normal receiver audio.
- Automatic amateur sideband selection, including USB on 60 metres.
- Mode-specific receiver filters derived from documented FreeDV bandwidths.
- Selector for 18 common FreeDV calling frequencies from 160 metres to QO-100.
- Built-in deterministic 700D test using the bundled Kiwi reference recording.
- Optional RX-only [FreeDV Reporter](https://qso.freedv.org/) presence.
- Optional, independently gated RADEV1 decoder.
- Help panel covering the available modes and controls.
- Authenticated Kiwi-to-decoder control, bounded audio queues, health metrics,
  watchdog recovery, atomic Kiwi releases, and rollback tooling.

## Project status

| Component | Tested version | Status |
| --- | --- | --- |
| Kiwi extension | 0.1.24 | Deployed and browser-tested on KiwiSDR 1.901 |
| Decoder service | 0.1.19 | Deployed on Debian 12 and stability-soak tested |
| Legacy transport | Protocol v2 | One receive session; outbound camper connection |
| FreeDV Reporter | RX-only | Opt-in; presence, restart recovery and removal tested |
| RADEV1 | Experimental | Implemented and feature-gated; reference audio decoded |

The bundled 700D test has passed end to end with returned audio and zero
dropped frames. Live-RF speech acceptance is still pending mode by mode. See
[Mode support](docs/modes.md) and [Deployment status](docs/deployment-status.md)
for the exact evidence and remaining gaps.

## Architecture

```text
Browser                   KiwiSDR                    Decoder guest
   |                         |                             |
   |-- extension control --->|                             |
   |                         |<-- outbound monitor WS -----|
   |                         |--- post-detector audio ---->|
   |                         |<-- decoded PCM / status ----|
   |<--- standard audio -----|                             |-- Codec2
   |                         |                             |-- RADEV1 (optional)
   |                         |                             `-- Reporter (optional)
```

The Kiwi remains the only receiver endpoint. The decoder service does not
publish a browser-facing WebSocket or require public port forwarding. Its
health and metrics endpoint is intended to remain local to the decoder guest.

External decoding is intentional: the Kiwi's single-core processor already
handles RF processing, receiver channels, waterfalls, audio and networking.
See [Why run an external decoder guest?](docs/external-decoder-vm.md) for the
resource, isolation and VM-versus-LXC trade-offs.

## Supported modes

| Mode | Typical use | Current integration status |
| --- | --- | --- |
| 1600 | Early FreeDV HF waveform | Codec2 backend and Kiwi SSB path implemented |
| 700C | Fast synchronization on stronger HF signals | Codec2 backend and Kiwi SSB path implemented |
| 700D | Weak-signal HF | End-to-end bundled reference test passed |
| 700E | Faster fading with lower latency than 700D | Codec2 backend and Kiwi SSB path implemented |
| 800XA | 4FSK through SSB | Codec2 backend and Kiwi SSB path implemented |
| 2400A | Wide VHF/UHF SDR channel | Selectable; 48 kHz modem path still required |
| 2400B | Audio through an analogue FM radio | Selectable; 48 kHz and FM receive paths still required |
| RADEV1 | Neural HF speech | Experimental, disabled by default |

“Implemented” means the selector, authenticated protocol and decoder backend
recognize the mode. It does not imply live-RF validation. The detailed mode
guide covers bandwidths, filters, approximate SNR thresholds, modem behavior
and selection advice: [docs/modes.md](docs/modes.md).

## Requirements

- KiwiSDR 2 running firmware 1.901 for the tested reference build.
- A supported Kiwi backup microSD card for full physical recovery.
- Private Debian 12 VM or unprivileged LXC reachable from the Kiwi LAN.
- Recommended decoder allocation: 2 vCPU, 2 GB RAM and 16 GB disk.
- Root or equivalent administrative access to the Kiwi and decoder guest.
- A unique 256-bit shared secret stored only in root-readable environment
  files.
- A KiwiSDR source checkout at the pinned upstream commit.

The reference deployment uses Proxmox, but the decoder is an ordinary Debian
service and is not tied to a specific hypervisor or guest ID.

## Installation

Clone the project on the administration workstation:

```bash
git clone https://github.com/smegoff/kiwisdr-freedv-extension.git
cd kiwisdr-freedv-extension
```

Installation has two independently reversible parts:

1. Provision the Debian decoder guest and install the C++ decoder service.
2. Apply the pinned overlay to KiwiSDR source, build the production
   `kiwid.bin`, and activate it as a versioned release.

Do not copy example addresses, VM IDs, storage names or credentials into a
production installation without review. Follow the complete safety-gated
procedure in [docs/installation.md](docs/installation.md). It covers:

- Kiwi configuration and physical backup;
- VM/LXC creation and firewall policy;
- decoder dependencies, build and systemd installation;
- shared-secret generation and configuration;
- overlay application and production Kiwi build;
- browser acceptance, health checks and stability soak; and
- automatic and manual rollback.

## Using the extension

1. Open a KiwiSDR receiver and choose **FreeDV** from the extension menu.
2. Select the transmitted FreeDV mode.
3. Tune manually or choose a common calling frequency.
4. Press **Start**.
5. Watch the state, backend, synchronization, SNR, frequency offset,
   callsign/text, dropped-frame and Reporter fields.
6. Press **Stop** or close the panel to restore the previous receiver mode,
   passband and normal audio.

The **Test** button runs a bundled 700D recording through the same Kiwi camper,
decoder and returned-audio path used for live reception. A passing test proves
the transport and Codec2 pipeline are working; it does not test the antenna,
RF signal level or every FreeDV mode.

## FreeDV Reporter

Reporter is disabled by default and operates strictly as the Kiwi owner's
RX-only station identity. It never publishes a public listener's browser name,
IP address or identity.

To enable it, configure a valid station callsign and Maidenhead locator in
Kiwi Admin, enable Reporter, and start a normal FreeDV session. The panel shows
`enabled (idle)` while no session is active, `enabled (test excluded)` during
the local reference Test, and `connecting` then `online` during a normal Start.
The decoder
guest's Reporter sidecar connects outbound to `qso.freedv.org`; no inbound
firewall rule is required. Full setup and troubleshooting are in
[Optional FreeDV Reporter](docs/installation.md#8-optional-freedv-reporter).

## RADEV1

RADEV1 uses the pinned portable RADE C implementation and FARGAN speech
synthesizer. It is disabled by default and requires both gates:

1. `FREEDV_ENABLE_RADE=1` on the decoder guest; and
2. **Enable RADEV1** in Kiwi Admin.

Enable it only after the RADEV1 reference and load tests pass on the target
decoder guest. RADEV1 live-RF speech validation remains pending.

## Testing

Run the repository regression tests from the project root:

```bash
python -m unittest discover -s tests -v
```

Decoder builds also provide CTest-based backend, framing, resampling and
reference-audio tests. Deployment is accepted only after a real-browser test
and independent Kiwi/decoder stability soaks. The reference deployment passed
41 checks per host at 15-second intervals with no critical errors.

## Safety and rollback

- Kiwi candidates are versioned and activated atomically.
- Deployment health gates verify the service, `/status` and receiver HTML.
- The stock `baseline-1.901` release and the immediately previous candidate are
  retained.
- Decoder snapshots are short-term rollback points, not backups. Superseded
  release snapshots are pruned after acceptance and soak testing.
- Shared secrets and configuration archives are excluded from Git.
- Physical eMMC recovery still requires a supported Kiwi backup microSD card.

Read [docs/rollback.md](docs/rollback.md) before deploying a candidate.

## Release publication gate

A deployment is not complete until its matching source, tests and public
documentation are committed and pushed to GitHub. The tracked branch must be
synchronized with its upstream branch, and the open pull request must record
the deployed versions, validation evidence and rollback point. Secrets,
private configuration archives and operational logs remain in ignored storage
and are never published.

## Documentation

- [Installation](docs/installation.md)
- [FreeDV mode support](docs/modes.md)
- [External decoder VM/LXC](docs/external-decoder-vm.md)
- [Camper/control protocol](docs/protocol.md)
- [Backup and rollback](docs/rollback.md)
- [Feasibility and headroom](docs/feasibility.md)
- [Reference deployment status](docs/deployment-status.md)

## Repository layout

| Path | Purpose |
| --- | --- |
| `decoder/` | C++17 Kiwi camper client, resampler and Codec2/RADE backends |
| `reporter/` | Isolated RX-only FreeDV Reporter sidecar |
| `kiwi-overlay/` | Kiwi server/client overlay and reproducible patches |
| `deploy/` | Decoder environment examples, firewall and systemd definitions |
| `tools/` | Backup, build, deployment, test, soak and rollback helpers |
| `docs/` | Architecture, installation, operation and validation records |

## Security

Do not commit Kiwi, Proxmox or decoder credentials. Use process environment
variables and root-readable configuration files as documented. The shared
secret authenticates decoder control polls and must be unique for each
installation.

If you find a security issue, avoid publishing credentials, private station
configuration or exploitable details in a public issue. Contact the repository
owner privately first.

## Contributing

Issues and pull requests are welcome. Please include the Kiwi firmware version,
decoder release, FreeDV mode, relevant health output and reproducible test
steps. Remove callsigns, addresses, secrets and other private station data from
logs before posting them.

## Acknowledgements

This project builds on:

- [KiwiSDR](https://github.com/jks-prv/KiwiSDR) by John Seamons;
- [FreeDV](https://freedv.org/) and
  [Codec2](https://github.com/drowe67/codec2) by David Rowe and contributors;
- [FreeDV Reporter](https://qso.freedv.org/); and
- the portable RADE work in
  [peterbmarks/radae_nopy](https://github.com/peterbmarks/radae_nopy).

FreeDV, Codec2, KiwiSDR and the upstream projects remain independent projects;
this repository provides an integration framework for receive-only use.
