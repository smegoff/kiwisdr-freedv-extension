# Deployment status

Last verified: 2026-07-13 19:08 UTC (2026-07-14 07:08 NZST)

## Live state

- KiwiSDR 2 firmware: 1.901
- active release: `freedv-v0-1-5`
- active Kiwi SHA-256:
  `ef9d4493e2825975c80bade442273461d68e80143f0688ef1012f94778fa7684`
- candidate BuildID: `b787410e9462ed5d63008d27816df1d2d935b202`
- retained baseline SHA-256:
  `ceaadaac5edb4165ef7331a1884651919798602bbc5881bc0c736ed0cf4b21b0`
- `kiwid.service`: active, `NRestarts=0`
- CT 112: production v0.1.5 outbound camper daemon and Reporter sidecar active
- production CT decoder SHA-256:
  `83293769a9d16e1865f0c4a98c0580c66b2bfb130b9ecf69d993b3fee76e9721`
- CT health/metrics: loopback-only `127.0.0.1:8074`
- CT state: connected idle monitor, not camped, zero sessions, `NRestarts=0`
- Reporter: owner-enabled after acceptance, currently `error` pending the
  asynchronous Socket.IO dependency deployment; RADE: disabled
- current CT snapshot: `pre-freedv-v0-1-5`

FreeDV is visible to ordinary users without developer mode. Only `FreeDV` was
removed from `extint.excl_devl`; `devl`, `digi_modes`, `s4285` and `prefs`
remain excluded.

## Backup and rollback gate

The fresh streamed pre-deployment configuration archive is
`backups/kiwi-config-20260713T182027Z/kiwi.config.tgz`. It contains 39 entries
and its verified SHA-256 is
`94f5a016a03bbf4ce542ed32c54ccd05c740c1d399b35a4f74308ad2680ae5be`.
The manifest, source commit, baseline checksum, root HTML, `/status`, matching
Kiwi/CT secret hashes and Reporter/RADE disabled state were verified before
activation. The secret itself was not displayed or copied into project files.

The stock `baseline-1.901` release and atomic rollback tooling remain intact.
CT 112 was snapshotted as `pre-freedv-v0-1-5`, and the prior decoder binary is
also retained root-only at
`/root/freedv-rollbacks/pre-v0-1-5-20260713T1823Z/freedv-decoder`.

Physical eMMC disaster recovery remains unavailable until a supported backup
microSD is created.

## v0.1.5 build and deployment

- source base: Kiwi commit `417e2c8add196e879b8cc4eb4a488b35b4bf0df7`
- release: `freedv-v0-1-5`
- Kiwi production binary SHA-256:
  `ef9d4493e2825975c80bade442273461d68e80143f0688ef1012f94778fa7684`
- CT production binary SHA-256:
  `83293769a9d16e1865f0c4a98c0580c66b2bfb130b9ecf69d993b3fee76e9721`
- deployment result: `active=freedv-v0-1-5 previous=baseline-1.901`

The production build regenerated `kiwisdr.min.js`, its gzip package,
`edata_embed` and `kiwid.bin`. Static verification confirmed a 32-bit ARM
production binary, registered/exported `FreeDV_main()`, embedded FreeDV JS/CSS,
the v0.1.5 release marker and the authenticated poll plus `rev_bin`/`rev_txt`
transport paths. The candidate is distinct from v0.1.4 and the stock baseline.

## Browser and decoder acceptance

A real browser verified that the normal extension menu contains FreeDV and the
panel renders all seven legacy modes, Start/Stop, backend, sync, SNR, offset,
callsign/text, dropped frames, Reporter state and the Reporter link.

The required 700D `Start -> Stop -> Start -> Close` sequence passed on the same
receiver channel:

1. First Start reached `State: running`, backend `codec2`. CT moved to one
   camper/session; SND and decoded counters reached 591 and 157.
2. Stop returned CT to `sessions=0, camper_connected=false`. Counters reached
   786 and 212. This proves `audio_camp=1,0` no longer false-matches `camp=`.
3. Second Start re-camped the same receiver and counters advanced to 1148 and
   308.
4. Closing FreeDV while running returned CT to zero sessions and uncamped;
   final counters were 1370 SND and 372 decoded frames.

Across the sequence, dropped frames and authentication failures remained zero,
reconnects remained at three, Reporter remained disabled, and the browser mute
DOM state after Stop and Close matched its pre-test state. The root receiver
and admin endpoint both loaded; the admin endpoint correctly reported that an
existing admin session already held the single-admin slot.

## Stability evidence

- CT: 41/41 samples passed at 15-second intervals, exit 0. Every sample showed
  CT 112 running, both services active, loopback-only port 8074, connected but
  uncamped, zero sessions, Reporter disabled and zero critical journal hits.
- Kiwi: 41/41 replacement samples passed at 15-second intervals, exit 0. Every
  sample showed v0.1.5 active, healthy service/status/root HTML, firmware 1.901,
  no deployment wrappers and zero critical journal hits.
- An initial Kiwi isolation run was preserved after it stopped at sample 26
  solely because a public receiver listener joined (`users=1`). All technical
  fields on that sample were healthy. The replacement soak records and permits
  the supported listener range instead of treating ordinary use as a fault.
- Kiwi soak SHA-256:
  `1f67c89260e9eaf731285a5df8cc80392af993bf0661a9d2294f7e65591292df`
- CT soak SHA-256:
  `fa78c35132b7be1fba3902580fba17d645ca254b6e39f0d0b64ad742f6877433`
- Kiwi journal SHA-256:
  `02c3e3c807cbd6eb210fbbfeafa433c1dfd2db8f74e66024bac75c441a9b18b6`
- CT journal SHA-256:
  `8e3a0f59bf1376b09be444b2963ad4df2f68a24419eb536b482e69afd0c3ad3a`

The collected candidate journals have zero matches for watchdog, crash,
assertion, audio-sequence, authentication or conflicting-job faults. Evidence
is stored under the ignored
`backups/evidence-v0-1-5-20260713T1908Z/` directory.

## Follow-up

The next validation step is live RF speech in each legacy mode when suitable
FreeDV transmissions are available. The owner has enabled Reporter with the
Kiwi station identity; the repository now corrects its Python dependency to
`python-socketio[asyncio_client]`, but this has not yet been deployed to CT 112.
RADE remains disabled. Create a supported Kiwi backup microSD and rotate the
Kiwi/Proxmox passwords disclosed during development.
