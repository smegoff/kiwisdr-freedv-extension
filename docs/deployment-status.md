# Deployment status

Last verified: 2026-07-15 15:33 UTC (2026-07-16 03:33 NZST)

This page records the project's reference installation. Hypervisor guest IDs,
hostnames and LAN addresses are site-local operational details, not product
requirements; the portable documentation calls this system the **decoder
guest**.

## Live state

- KiwiSDR 2 firmware: 1.901
- active Kiwi release: `freedv-v0-1-16`
- active Kiwi SHA-256:
  `ada125f19794834cd5e6422c5b18b3d47140fc3ba882efe4192acd998a66686b`
- Kiwi BuildID: `2715fb01040bf6547df6069181a2a19d8bcedc7c`
- immediate Kiwi rollback: `freedv-v0-1-15`
- retained stock baseline SHA-256:
  `ceaadaac5edb4165ef7331a1884651919798602bbc5881bc0c736ed0cf4b21b0`
- decoder-guest release: `0.1.18`
- decoder binary SHA-256:
  `1eee6cc920ee36b9fca3bec6eeb87d6ee0a602866e02c74d30ba2328d7acb4bd`
- Reporter sidecar SHA-256:
  `ce0a3245a135c88a6f47342a06e6c8167fe957f4b507fd84fdea1dfac9e78328`
- decoder-guest snapshot: `pre-reporter-v0-1-18`
- RADEV1: compiled and enabled by matching decoder/Kiwi gates
- normal idle state: Kiwi connected, not camped, zero sessions, Reporter
  disabled

FreeDV remains visible to ordinary users without developer mode. Only FreeDV
was removed from `extint.excl_devl`; the other developer extensions remain
hidden. RADEV1 is separately hidden from the mode selector whenever its Kiwi
administrator flag is off.

## v0.1.18 Reporter correctness and recovery

The earlier sidecar treated the generic Socket.IO connection callback as proof
that FreeDV Reporter had accepted the station. The upstream server uses
`always_connect`, so transport connection can occur before authentication and
application registration finish. v0.1.18 waits for the server's explicit
`connection_successful` event; a rejection, disconnect or timeout now becomes
an error instead of a false `online` state.

Frequency publication no longer waits for modem sync. The current receiver
frequency is sent as soon as a normal FreeDV session starts; a synchronized
session still wins selection if multiple sessions are later enabled. Periodic
private status events now repeat the administrator-owned Reporter settings so
an independently restarted sidecar reconstructs the session and reconnects.
No listener name, browser identity or listener address is included.

A real browser and the public Reporter page confirmed the configured RX-only
station with decoder version `KiwiSDR-FreeDV/0.1.18`, its locator/message and
`7.0200 MHz`. Restarting `freedv-reporter.service` while the decoder remained
active restored `online` and the same listing within five seconds. Stop changed
the panel and health endpoint to `disabled` and removed the public row.

## Backup and rollback gate

The pre-deployment streamed archive is
`backups/kiwi-config-20260715T102557Z/kiwi.config.tgz`. It contains 39 entries
and has SHA-256
`310218b686b6afc15abe51fcc1ab96d27128243584e09bcbf5b0004c41f8c175`.
The stock baseline checksum, root page, `/status`, two zero-listener readings,
decoder snapshot and shared-secret hash match all passed before activation. The
secret itself was never displayed or copied into the repository.

Software and configuration rollback are available. Physical eMMC recovery is
still unavailable until a supported Kiwi backup microSD card is created.

## v0.1.16 reliability changes

The v0.1.15 decoder could remain connected but stop making progress when its
synchronous Kiwi WebSocket read blocked after a lost response. Its independent
watchdog thread continued notifying systemd, so the service and old health
endpoint incorrectly remained green. Additional CPU cores could not resolve
that blocked control path.

v0.1.16 ties `/healthz`, metrics and watchdog notification to a monotonic
heartbeat updated by the real Kiwi control loop. A loop older than 15 seconds
is degraded and causes the process to exit for systemd recovery. A deliberate
`SIGSTOP` test replaced the frozen decoder and restored a connected, idle
service in approximately 32.5 seconds. The resulting intentional restart count
is one and did not increase during the following soak.

The Kiwi now installs its reverse-status callback defensively on Setup, Start
and Test, and relays decoded `rev_txt` through the standard encoded extension
helper. A `test_ready` handshake prevents the decoder guest from consuming live receiver noise
before the bundled recording is armed. Reference transport tests passed all 12
byte-order/rate/first-packet combinations, and the standalone 700D and RADEV1
reference decoders both passed.

## RADEV1 build evidence

- RADE C pin:
  `peterbmarks/radae_nopy@6e6fff3fc0546363693b60b52f463e08c71117e6`
- FARGAN/Opus pin:
  `940d4e5af64351ca8ba8390df3f555484c567fbb`
- decoder source archive SHA-256 used for the v0.1.16 build:
  `74787486ed812c7783a3c35bb15e2662a7931a9b0dc00cdb419cda3298e194ae`
- Kiwi overlay archive SHA-256:
  `10672107cd2d061f3b52b375549fed68b86ba9185fdd94f89f66ba60ee1188ba`
- RADEV1 reference test executable SHA-256:
  `0b1b33467c441fe4d191374461adf66136b56b0d3441dbab55382a5ed33a11cc`
- generated reference waveform SHA-256:
  `41352c3d6d7c97966a1f8646420aac83f8594b8ecc35c81d632f66b9ed6dc578`

The pinned reference synchronized, produced 181,600 samples of 16 kHz speech
and decoded at real-time factor 0.0216. The queue-overflow/reset and
between-frame status persistence checks also passed. An eight-worker,
20-repetition stress run passed the 0.50 headroom gate at per-worker real-time
factor 0.0855 with peak container memory 385,695,744 bytes.

After acceptance, the obsolete 667 MB `/opt/radae_decoder` tree, old
`webrx_rade_decode` tool and legacy commit marker were removed. They remain
recoverable from snapshot `pre-radev1-v0-1-15`; the live service uses only the
official `radae_nopy` installation.

## Browser and transport acceptance

A real browser confirmed:

1. FreeDV v0.1.16 appears in the normal extension menu and visibly credits and
   links to the FreeDV project, Codec2 and RADE.
2. With the admin flag off, the selector contains only the seven legacy modes.
3. Help opens and includes the RADEV1 bandwidth, sample-rate, sync-gate and
   enablement guidance.
4. After enabling both gates, RADEV1 appears in the selector.
5. A normal RADEV1 session reached `running / rade-v1 / sync no`; the decoder moved to
   one camper/session, Reporter reached online and dropped frames remained 0.
6. With no synchronized RADE signal, no decoded frames were returned; the
   Kiwi return-audio gate therefore supplied silence instead of
   analogue/static audio.
7. Stop returned the browser to `stopped`, the decoder to zero/un-camped and Reporter to
   disabled. A following deterministic 700D bundled test reached
   `100% / test passed`, with zero drops and Reporter disabled.

Live on-air RADEV1 speech remains to be validated when a suitable transmission
is available; the codec itself has passed the generated reference waveform.

## Stability evidence

From 2026-07-15 11:32:56 UTC through 11:43:05 UTC:

- Kiwi: 41/41 samples passed at 15-second intervals. Every sample showed
  v0.1.16 active, firmware 1.901, healthy service/status/root HTML, no
  deployment wrappers and zero critical journal matches.
- Decoder guest: 41/41 samples passed. Every sample showed both services active, decoder
  0.1.16 healthy, Kiwi connected, zero sessions, un-camped and Reporter
  disabled.
- Final `NRestarts` was zero for `kiwid.service`. Decoder `NRestarts` remained
  one from the intentional pre-soak hang test and did not increase during the
  41 samples.

The ignored local evidence directory is
`backups/freedv-v0-1-16-20260715T114305Z/`.

Reporter v0.1.18 also completed a separate active-session soak from
2026-07-15 15:23:42 UTC through 15:33:42 UTC. All 41 samples required and
observed one camper/session, Reporter `online`, decoder release `0.1.18`, a
fresh Kiwi heartbeat, both services active and zero critical matches. The
ignored evidence log contains 41 records and has SHA-256
`c37486deb8015f5aa8c1c5c7e0d9b1e62746248134365da2f31184de704c50ac`.
After the soak, Stop returned to zero sessions/un-camped/disabled; both service
restart counters were zero and the error-priority journal was empty.

## Follow-up

Validate live RADEV1 and legacy speech mode by mode when suitable transmissions
are available. 2400A/2400B still require the wider 48 kHz and VHF/FM receive
paths described in [modes.md](modes.md). Obtain a supported Kiwi backup microSD
and rotate the Kiwi/Proxmox credentials disclosed during development.
