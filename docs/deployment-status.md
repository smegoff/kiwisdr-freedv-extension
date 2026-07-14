# Deployment status

Last verified: 2026-07-14 23:21 UTC (2026-07-15 11:21 NZST)

## Live state

- KiwiSDR 2 firmware: 1.901
- active Kiwi release: `freedv-v0-1-15`
- active Kiwi SHA-256:
  `e643a52ab3aace3b379795879b64492a020f0deaf3d7acb9f64bd1024ae05b15`
- Kiwi BuildID: `009496758bc8cf97698ea0b8800e6061b8ada785`
- immediate Kiwi rollback: `freedv-v0-1-14`
- retained stock baseline SHA-256:
  `ceaadaac5edb4165ef7331a1884651919798602bbc5881bc0c736ed0cf4b21b0`
- CT 112 decoder release: `0.1.15`
- CT decoder SHA-256:
  `1d8a39afad2300f3393e74a1322c12af24eeee12c813c5adcb6980945111fa30`
- Reporter sidecar SHA-256:
  `43169d7fd1ae2b1a4cbeab207c673fcbfc58b6308ee7c0cf9afaae960d22d726`
- CT snapshot: `pre-radev1-v0-1-15`
- RADEV1: compiled and enabled by matching CT/Kiwi gates
- normal idle state: Kiwi connected, not camped, zero sessions, Reporter
  disabled

FreeDV remains visible to ordinary users without developer mode. Only FreeDV
was removed from `extint.excl_devl`; the other developer extensions remain
hidden. RADEV1 is separately hidden from the mode selector whenever its Kiwi
administrator flag is off.

## Backup and rollback gate

The pre-deployment streamed archive is
`backups/kiwi-config-20260714T224649Z/kiwi.config.tgz`. It contains 39 entries
and has SHA-256
`4061be1f13c47eeedadbaf4c72b13dc7de7cf2364b269cfa6ce5a70a41d09ec6`.
The stock baseline checksum, root page, `/status`, two zero-listener readings,
CT snapshot and shared-secret hash match all passed before activation. The
secret itself was never displayed or copied into the repository.

Software and configuration rollback are available. Physical eMMC recovery is
still unavailable until a supported Kiwi backup microSD card is created.

## RADEV1 build evidence

- RADE C pin:
  `peterbmarks/radae_nopy@6e6fff3fc0546363693b60b52f463e08c71117e6`
- FARGAN/Opus pin:
  `940d4e5af64351ca8ba8390df3f555484c567fbb`
- decoder source archive SHA-256 used for the build:
  `dd2abca9f08d04bb46c29d49060376099dbe0f136b3b3ee916b185a92320d08d`
- Kiwi overlay archive SHA-256:
  `c3d03392b18482ee5d7062653f3ce0554cdad9fa4897705c0042e36fc87d54aa`
- RADEV1 reference test executable SHA-256:
  `0b1b33467c441fe4d191374461adf66136b56b0d3441dbab55382a5ed33a11cc`
- generated reference waveform SHA-256:
  `41352c3d6d7c97966a1f8646420aac83f8594b8ecc35c81d632f66b9ed6dc578`

The pinned reference synchronized, produced 181,600 samples of 16 kHz speech
and decoded at real-time factor 0.0215. The queue-overflow/reset and
between-frame status persistence checks also passed. An eight-worker,
20-repetition stress run passed the 0.50 headroom gate at per-worker real-time
factor 0.0855 with peak container memory 385,695,744 bytes.

After acceptance, the obsolete 667 MB `/opt/radae_decoder` tree, old
`webrx_rade_decode` tool and legacy commit marker were removed. They remain
recoverable from snapshot `pre-radev1-v0-1-15`; the live service uses only the
official `radae_nopy` installation.

## Browser and transport acceptance

A real browser confirmed:

1. FreeDV v0.1.15 appears in the normal extension menu.
2. With the admin flag off, the selector contains only the seven legacy modes.
3. Help opens and includes the RADEV1 bandwidth, sample-rate, sync-gate and
   enablement guidance.
4. After enabling both gates, RADEV1 appears in the selector.
5. A normal RADEV1 session reached `running / rade-v1 / sync no`, CT moved to
   one camper/session, Reporter reached online and dropped frames remained 0.
6. With no synchronized RADE signal, no decoded frames were returned; the
   existing Kiwi return-audio gate therefore supplied silence instead of
   analogue/static audio.
7. Stop returned the browser to `stopped`, CT to zero/un-camped and Reporter to
   disabled. A following 700D bundled test reached `100% / test passed` with
   Reporter disabled.

Live on-air RADEV1 speech remains to be validated when a suitable transmission
is available; the codec itself has passed the generated reference waveform.

## Stability evidence

From 2026-07-14 23:11:47 UTC through 23:21:47 UTC:

- Kiwi: 41/41 samples passed at 15-second intervals. Every sample showed
  v0.1.15 active, firmware 1.901, healthy service/status/root HTML, no
  deployment wrappers and zero critical journal matches.
- CT: 41/41 samples passed. Every sample showed both services active, decoder
  0.1.15 healthy, Kiwi connected, zero sessions, un-camped and Reporter
  disabled.
- Final `NRestarts` was zero for both `kiwid.service` and
  `freedv-decoder.service`.

The ignored local evidence directory is
`backups/radev1-v0-1-15-20260714T232147Z/`.

## Follow-up

Validate live RADEV1 and legacy speech mode by mode when suitable transmissions
are available. 2400A/2400B still require the wider 48 kHz and VHF/FM receive
paths described in [modes.md](modes.md). Obtain a supported Kiwi backup microSD
and rotate the Kiwi/Proxmox credentials disclosed during development.
