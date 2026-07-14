# Deployment status

Last verified: 2026-07-14 22:11 UTC (2026-07-15 10:11 NZST)

## Live state

- KiwiSDR 2 firmware: 1.901
- active Kiwi release: `freedv-v0-1-14`
- active Kiwi SHA-256:
  `189cba9f72354b540a9068a3ad5b0d5d76368726c229f64da509159e9771f46e`
- candidate BuildID: `4e850e24c1b5c7d1b747cbd8c164cef056d74c5f`
- retained baseline SHA-256:
  `ceaadaac5edb4165ef7331a1884651919798602bbc5881bc0c736ed0cf4b21b0`
- CT 112 decoder release: `0.1.13`
- CT decoder SHA-256:
  `b2ae885554de8427c639fff564a6cfd724936649f45795efa4fecb669430895f`
- Reporter sidecar SHA-256:
  `0d728b3dea2bd22dc2796b5c43435a57665fec7cf5689b347f90d0fe196c9985`
- CT health endpoint: loopback-only `127.0.0.1:8074`
- idle state: Kiwi connected, not camped, zero sessions, Reporter disabled
- latest decoder snapshot: `pre-freedv-v0-1-13`
- RADE: disabled

FreeDV is visible to ordinary users without developer mode. Only `FreeDV` was
removed from `extint.excl_devl`; `devl`, `digi_modes`, `s4285` and `prefs`
remain excluded.

## Backup and rollback gate

The latest verified streamed configuration archive is
`backups/kiwi-config-20260714T202358Z/kiwi.config.tgz`. It contains 39 entries
and has SHA-256
`2ed53a30b93b14fa8758871ac7e77518201ac380d8ee0b3b5b885194c9c8c3e3`.
The baseline checksum, receiver root page, `/status`, zero-listener readings,
CT snapshot and secret-hash match were verified before activation. The secret
itself was not displayed or copied into the repository.

The stock `baseline-1.901` release and atomic rollback tooling remain intact.
Physical eMMC disaster recovery is still unavailable until a supported backup
microSD card is created.

After acceptance, obsolete inactive Kiwi candidates v0.1.3, v0.1.4, v0.1.5,
v0.1.7, v0.1.8, v0.1.9 and v0.1.12 were checksummed into
`/root/freedv-releases/retired-SHA256SUMS-20260714` and removed. The baseline,
immediate previous v0.1.13 and active v0.1.14 remain. Removing those copies and
the duplicate build binary increased free eMMC space from 68 MB to 287 MB.

## v0.1.14 build and activation

- source base: Kiwi commit `417e2c8add196e879b8cc4eb4a488b35b4bf0df7`
- overlay archive SHA-256:
  `904c45f7a6e7d94cf12a8a5c6a1018f2f803fb78bf072a77931ad593797fb22b`
- production Kiwi binary SHA-256:
  `189cba9f72354b540a9068a3ad5b0d5d76368726c229f64da509159e9771f46e`
- activation result: `active=freedv-v0-1-14 previous=baseline-1.901`

The production build regenerated optimized JavaScript, gzip assets,
`edata_embed` and `kiwid.bin`. Static verification confirmed a 32-bit ARM
production ELF, exported `FreeDV_main()`, the embedded FreeDV asset and the
v0.1.14 release marker. The activation wrapper verified the service, `/status`
and root HTML during the normal Kiwi initialization interval.

## Browser and decoder acceptance

A real browser completed the following checks against the live receiver:

1. FreeDV appeared in the normal extension menu and rendered all seven legacy
   modes plus Start, Test, status, Reporter and Help controls.
2. Help was enabled and opened a mode guide covering every listed mode,
   listening behavior and Test mode.
3. Test used John's bundled 700D sample and reached `100% / test passed` with
   backend `codec2`, returned decoded PCM, zero drops and Reporter disabled.
4. A normal no-signal 700D session reached `running / codec2 / sync no`; CT
   showed one camper/session and Reporter reached `online`.
5. The unsynchronized session exercised the audio gate: the standard analogue
   stream was replaced with zero PCM while no decoded PCM was available.
6. Stop changed the panel to `stopped`, Reporter to `disabled`, and CT to zero
   sessions/un-camped. Close reset the extension selector and restored the
   normal receiver stream.
7. Receiver root and Admin HTML remained available.

Reporter is therefore enabled and working. Its idle display is intentionally
`disabled`: it publishes one RX-only station presence only while a normal
FreeDV decode session is active. Test sessions never report.

## Stability evidence

From 2026-07-14 22:01:33 UTC through 22:11:33 UTC:

- Kiwi: 41/41 samples passed at 15-second intervals, exit 0. Every sample
  showed v0.1.14 active, firmware 1.901, healthy service/status/root HTML,
  zero users, no deployment wrappers and zero critical journal matches.
- CT: 41/41 samples passed at 15-second intervals. Every sample showed decoder
  0.1.13 healthy, Kiwi connected, zero sessions, un-camped and Reporter
  disabled.

The relevant journals had no watchdog, crash, assertion, audio-sequence or
authentication failures during acceptance.

## Follow-up

Live RF speech remains to be validated mode by mode when suitable FreeDV
transmissions are available. 2400A/2400B still require the wider 48 kHz and
VHF/FM receive paths described in [modes.md](modes.md). Obtain a supported Kiwi
backup microSD and rotate the Kiwi/Proxmox credentials disclosed during
development.
