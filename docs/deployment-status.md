# Deployment status

Last verified: 2026-07-16 21:00 UTC (2026-07-17 09:00 NZST)

This page records the project's reference installation. Hypervisor guest IDs,
hostnames and LAN addresses are site-local operational details, not product
requirements; the portable documentation calls this system the **decoder
guest**.

## Live state

- KiwiSDR 2 firmware: 1.901
- active Kiwi release: `freedv-v0-1-26`
- active Kiwi SHA-256:
  `e8b5cf7eaf5000d8fe1c20100ec9acb5f17341943d34599358197eb23f8536bc`
- Kiwi BuildID: `e9df12a4cb46bf672a0ae6d00b33afcad35bfaed`
- immediate Kiwi rollback: `freedv-v0-1-25`
- retained stock baseline SHA-256:
  `ceaadaac5edb4165ef7331a1884651919798602bbc5881bc0c736ed0cf4b21b0`
- decoder-guest release: `0.1.21`
- decoder binary SHA-256:
  `7875127ae3fdf5a4a2c19bcbf90d44808c09c49780f6121f22e428042efee639`
- Reporter sidecar SHA-256:
  `1b4263a0b19c99044e7e8f5391641b740cc0febfa31c25d2ec9ff1a9b86568c5`
- Reporter client: `KiwiSDR-FreeDV/0.1.28`
- decoder-guest snapshot: `pre-dashboard-lan-v0-1-21`
- RADEV1: compiled and enabled by matching decoder/Kiwi gates
- normal idle state: Kiwi connected, not camped, zero sessions; decoder health
  reports the Reporter sidecar disabled while the opted-in extension panel shows
  `enabled (idle)` and no station presence is published

## Decoder dashboard v0.1.21

Decoder v0.1.21 removes the dashboard token, login form, session cookies and
authentication endpoints. Any host that can reach TCP 8076 can open the
read-only dashboard and its status, history and waterfall APIs directly. The
security boundary is therefore the Proxmox firewall: the reference policy
permits TCP 8076 only from `192.168.10.0/24`. Health and metrics on TCP 8074
remain bound to loopback, and no public forwarding is configured.

The upgrade retained the existing atomic binary and asset rollback behavior.
The live decoder environment no longer contains the legacy dashboard-token
setting, and `POST /api/v1/login` returns HTTP 404. A real browser loaded the
dashboard without a login or sign-out control and established the waterfall
WebSocket immediately. The published token-free dashboard screenshot has
SHA-256
`08d8945dc35cbf47d2bc4440e604a04da49a155747074a235174a0327ce29ffc`.

A real Kiwi browser session started 700D and reached `running`, backend
`codec2` and Reporter `online`. The dashboard simultaneously showed one
session and one client. A three-sample active soak advanced the waterfall from
567 to 855 frames with zero visualization drops and daemon RSS from 10,484 to
10,548 KiB. Stop returned the extension to `stopped`, Reporter to
`enabled (idle)`, and the decoder to zero sessions. A following three-sample
idle check held at zero sessions, one dashboard client and a stationary
waterfall sequence. All C++ tests and all 17 Python tests passed.

Extended RADEV1 no-signal testing separately exposed an existing decoder
main-loop watchdog after roughly nine minutes. The isolated dashboard worker,
ring buffer and WebSocket path remained responsive and drop-free before the
watchdog recovery. RADEV1 remains experimental; this is recorded as a backend
follow-up and is not presented as a dashboard failure.

## v0.1.26 panel spacing and RADEV1 default

The receiver panel continues to inherit Kiwi's native typography (`DejaVu
Sans`, with Verdana, Geneva and generic sans-serif fallbacks). Its content is
now grouped into intro, action, tuning, receiver, decoder-status and footer
sections. The panel adds modest 8-12 px section gaps, 3 px status-row spacing,
12 x 14 px internal padding and a subtle one-pixel divider above decoder
status. No independent font or global Kiwi style is introduced.

Kiwi's `w3_*` helpers emit `id-*` style tokens as classes. v0.1.26 therefore
uses class selectors so both the new layout and the extension's existing aqua
status-value rules actually apply. RADEV1 is the initial mode when its Admin
feature gate is on; a Kiwi with RADEV1 disabled automatically falls back to
700D. The fixed 700D reference Test remains unchanged.

A real browser verified v0.1.26 in the ordinary extension menu, RADEV1 selected
by default, the native font stack, all computed spacing values and the rendered
status divider. A RADEV1 session reached backend `rade-v1`, Reporter `online`
and zero dropped frames. Stop returned Reporter to `enabled (idle)` and the
decoder guest to zero sessions and no camper. Fourteen Kiwi-overlay tests and
15 Reporter tests pass. The production binary SHA-256 and BuildID are recorded
above; v0.1.25 is retained as the immediate atomic rollback.

The post-deployment soak passed 41/41 samples. Every sample reported active
v0.1.26, firmware 1.901, healthy service/status/root HTML, zero users, zero
deployment wrappers and zero critical log matches. The pre-change streamed
configuration archive contains 39 entries and has SHA-256
`019d115f294d086a537b2b37f6b0a007928ebf69a0b13a5c79876ffcbbb01252`.
Ignored evidence is stored under
`backups/freedv-v0-1-26-deployment-20260716T085800Z/`.

## Reporter v0.1.28 RX codec publication

The RX-only Reporter sidecar now publishes the selected FreeDV codec as an
empty-callsign `rx_report`. Frequency changes use a Socket.IO acknowledgement
before the codec report, preventing the Reporter server's frequency-change RX
reset from racing and clearing the mode. A ten-second activity refresh handles
the server's current behavior of not replaying the last RX report to web
viewers who open the page after the decoder session starts. No `tx_report` is
sent.

The stale-session safety timeout is 15 seconds. Decoder status is normally
delivered every 250 ms with incoming Kiwi audio; the longer grace period avoids
dropping Reporter presence during a brief sound-stream gap while still cleaning
up a lost local Stop event promptly.

A real browser started a normal 700D session and verified the public ZL1SXG row
as `Receive Only`, client `KiwiSDR-FreeDV/0.1.28`, RX Mode `700D`, RX callsign
`--` and TX Mode `N/A`. The same RX Mode appeared after reloading the Reporter
page mid-session and waiting for the refresh. Stop removed the public presence.
Fifteen Reporter unit tests cover identity, RX-only authentication, session
precedence, restart recovery, stale-state expiry, codec payload and publication
ordering; all 13 Kiwi-overlay regression tests also pass.

The final 41-sample, ten-minute decoder-guest soak passed while a real listener
used FreeDV for most of the window. Active samples consistently showed one
session, camper connected and Reporter online; the listener's Stop transitioned
cleanly to zero sessions, no camper and Reporter disabled. Both services stayed
active, health remained `ok`, the health listener remained loopback-only and no
critical decoder or Reporter log matches occurred. A final v0.1.28 browser test
reconfirmed immediate and late-viewer RX Mode `700D`, then removed the public
presence on Stop.

## v0.1.24 returned-status relay and Reporter cleanup

The decoder was healthy, camped and processing audio while the browser remained
at `waiting-for-decoder`. The failure was isolated to the Kiwi's `rev_txt`
handoff: the generic receiver callback could be replaced during the
monitor-to-camper transition, so valid decoder status was silently lost.
v0.1.24 routes the authenticated camper's FreeDV `rev_txt` directly to the
FreeDV handler, retains generation and payload validation, and records one
bounded diagnostic per outcome and generation without logging status payloads
or secrets.

A real browser reproduced the old failure on v0.1.23, then verified v0.1.24.
RADEV1 reached `running`, backend `rade-v1` and Reporter `online` in under three
seconds with one decoder session and zero dropped frames. Stop returned the
decoder to zero sessions and no camper. The bundled 700D Test progressed from
6% to 81% and completed at `100% / test passed`; Reporter correctly showed
`enabled (test excluded)` during the test. A normal 700D session reached
backend `codec2` and Reporter `online`.

The public Reporter page listed ZL1SXG at 7.0200 MHz as `Receive Only` with
client `KiwiSDR-FreeDV/0.1.24`, then removed the row after Stop. The Reporter
sidecar now expires session state after three seconds without decoder events,
so a lost local UDP stop datagram cannot leave a stale public presence. The
stale-session behavior and the existing identity, RX-only, precedence,
reconnect and duplicate-suppression behavior are covered by 12 Reporter unit
tests; 13 Kiwi-overlay regression checks also pass.

The pre-change streamed archive is
`backups/kiwi-config-20260716T054648Z/kiwi.config.tgz`. It contains 39 entries
and has SHA-256
`01123957d3d9e2c5d561ea44db914f7f36d22da9d059e2f617e1416e62236e9d`.
The source rollback copy is `/root/freedv-rollbacks/20260716T055500Z`.

The post-deployment idle soak passed 41/41 Kiwi samples and 41/41
decoder-guest samples. Every Kiwi sample reported active v0.1.24, firmware
1.901, healthy service/status/root HTML, zero listeners, zero deployment
wrappers and zero critical log matches. Every decoder sample reported both
services active, private loopback health only, Kiwi connected, no camper, zero
sessions, Reporter disabled and zero critical matches. The ignored evidence is
stored under `backups/freedv-v0-1-24-deployment-20260716T062850Z/`.

FreeDV remains visible to ordinary users without developer mode. Only FreeDV
was removed from `extint.excl_devl`; the other developer extensions remain
hidden. RADEV1 is separately hidden from the mode selector whenever its Kiwi
administrator flag is off.

## v0.1.23 Help guide

The built-in Help modal now explains the experimental RADEV1 receive path,
including the external decoder guest, portable RADE implementation, FARGAN
speech synthesis, 8 kHz modem input, 16 kHz decoded speech and the separate
decoder/Kiwi feature gates. The Calling frequencies section is generated from
the same data as the extension selector and lists all 18 common entries from
160 metres through QO-100. The modal also links directly to the public GitHub
repository for installation, architecture, mode and rollback documentation.

A real browser loaded FreeDV from the normal extension menu, confirmed version
0.1.23, opened the scrolling Help modal and observed the complete RADEV1 text,
all calling-frequency entries and the correct public repository URL. The
decoder service was unchanged for this Help-only release, so its retained
snapshot set was left unchanged. The post-deployment idle soak passed 41/41
Kiwi samples and 41/41 decoder-guest samples with no service failures, critical
log matches, sessions, campers or orphan deployment wrappers.

## Kiwi storage cleanup

The Kiwi eMMC cleanup on 2026-07-16 reduced filesystem use from 94% to 80%,
increasing available space from 226 MiB to 683 MiB. Before deletion, the active
v0.1.23 binary, immediate v0.1.22 rollback and stock 1.901 baseline were checked
against their recorded SHA-256 values. The same checks passed after cleanup.

The cleanup removed superseded v0.1.20 and v0.1.21 release binaries, obsolete
FreeDV staging and transfer directories, two sparse core files and a temporary
configuration archive whose SHA-256 exactly matched the verified local backup.
It retained the current release, immediate rollback, stock baseline, Kiwi
source tree, build tree, configuration, rollback archive and deployment
evidence.

Persistent system journal use was reduced from 352 MiB to 64 MiB and capped at
64 MiB with 256 MiB reserved free. `/tmp` fell from 113 MiB to 112 KiB. Five
post-cleanup receiver samples verified the service, root page, Admin page,
`/status`, firmware version and zero-listener state. Decoder health remained
OK with zero sessions and no camper. The ignored operational manifest is stored
under `backups/kiwi-storage-cleanup-20260716T020214Z/`.

## v0.1.22 Test and Reporter state reliability

The browser Test timeout is now split into two observable stages. The first
watchdog waits for the authenticated decoder camper to report `running`; that
state marks the deterministic reference signal as armed. A separate five-second
watchdog then checks that Kiwi reference-audio progress actually begins. This
prevents a late but healthy camper acknowledgement from producing the previous
generic "did not arm" message, while still detecting a stalled Kiwi audio path.

Reporter state now distinguishes configuration from presence. An opted-in but
stopped extension shows `enabled (idle)`, Test shows
`enabled (test excluded)`, and normal Start progresses through `connecting` to
`online`. A real browser observed the v0.1.22 Test reach running in 0.5 seconds,
begin reference progress within one second, and finish at `100% / test passed`
with no timeout. A normal 700D Start reached Reporter `online` in 1.5 seconds.
The public FreeDV Reporter page showed the configured station as `Receive Only`,
client `KiwiSDR-FreeDV/0.1.22`, at 7.0200 MHz, and removed the row immediately
after Stop. The panel then returned to `enabled (idle)`.

The post-deployment idle soak passed 41/41 Kiwi samples and 41/41 decoder-guest
samples. Kiwi firmware 1.901, root HTML, `/status`, both services, the private
health listener and outbound Kiwi connection remained healthy; users, sessions,
campers, wrappers and critical log matches remained zero.

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

The current pre-deployment streamed archive is
`backups/kiwi-config-20260716T005715Z/kiwi.config.tgz`. It contains 39 entries
and has SHA-256
`3011117b81693c752c813bfaf55dec361ebdaceff17b21d86e8c0f701841c499`.
The stock baseline checksum, root page, `/status`, two zero-listener readings,
decoder snapshot and shared-secret hash match all passed before activation. The
secret itself was never displayed or copied into the repository.

Software and configuration rollback are available. Physical eMMC recovery is
still unavailable until a supported Kiwi backup microSD card is created.

The v0.1.19 pre-change streamed archive is
`backups/kiwi-config-20260715T161826Z/kiwi.config.tgz`. It contains 39 entries
and has SHA-256
`a96ffe3d796905001748cc4ed46ceadc48a33c7da6bac419fb0cca91463dc9f9`.
The deployment preserved v0.1.16 as the immediate atomic rollback and created
source rollback copy `/root/freedv-rollbacks/20260715T162237Z` before applying
the overlay.

The v0.1.20 pre-change streamed archive is
`backups/kiwi-config-20260715T184611Z/kiwi.config.tgz`. It contains 39 entries
and has SHA-256
`e023f564a7192d4628f733dc8255b57ecf2d355da815cb09c5d52e45fe704483`.
The deployment preserved v0.1.19 as the immediate atomic rollback and created
source rollback copy `/root/freedv-rollbacks/20260715T184843Z` before applying
the overlay. The candidate passed the pinned-source, ordinary-menu,
optimized-asset, embedded-data and production-ARM checks before activation.

The v0.1.21 pre-change streamed archive is
`backups/kiwi-config-20260715T193403Z/kiwi.config.tgz`. It contains 39 entries
and has SHA-256
`52e68b629928b2f5801a6d4c4571c0b713b765bd313750bac4dd5f070c357dee`.
The deployment preserved v0.1.20 as the immediate atomic rollback and created
source rollback copy `/root/freedv-rollbacks/20260715T193603Z` before applying
the overlay. Decoder snapshot `pre-test-race-v0-1-19` was taken before the
v0.1.19 service upgrade.

The v0.1.22 pre-change streamed archive is
`backups/kiwi-config-20260715T235938Z/kiwi.config.tgz`. It contains 39 entries
and has SHA-256
`e71c72ef44ce379675e6ec18cd992a322720bce33299796e5b065ae9db7c8a33`.
The deployment preserved v0.1.21 as the immediate atomic rollback and created
source rollback copy `/root/freedv-rollbacks/20260716T000329Z`. Decoder snapshot
`pre-reporter-v0-1-22` was taken before the Reporter-only sidecar upgrade.

## v0.1.21 deterministic Test handshake

The reported failure was reproduced in a real browser: the panel remained at
`Test: 0% / State: testing`, the decoder camped and received SND packets, but
the reference signal never armed and decoded frames remained zero. Repeating
the decoder's running status alone did not resolve it; 233 status updates were
sent while the test remained stuck. This proved the failure was a routing race,
not insufficient decoder CPU.

The first `rev_txt` running status could arrive while Kiwi was still changing
the connection from MON to SND, before returned-status routing was ready. The
final handshake now uses the already authenticated job-control path: the first
response advertises `test_ready=false`; after the decoder processes the camper
acknowledgement and polls again, Kiwi queues `test_ready=true` and then starts
the reference recording. Decoder v0.1.19 also repeats running status while
waiting, and the v0.1.21 panel fails clearly after 15 seconds if arming does not
begin instead of waiting indefinitely.

Three consecutive real-browser tests then armed in 1.3-2.4 seconds and each
reached `100% / test passed`, with 85 decoded frames in total, zero dropped
frames and Reporter disabled. Each completion returned the decoder to zero
sessions and no camper.

After acceptance, inactive Kiwi releases v0.1.13-v0.1.16 and v0.1.19 were
archived locally as
`backups/retired-kiwi-releases-before-v0-1-21-cleanup.tgz` (SHA-256
`ce231054086c5d96541da9a4570bd5c9a4f96b513cc65b220759846494950bef`)
and removed from eMMC. The stock baseline, active v0.1.21 and immediate v0.1.20
rollback remain on the Kiwi; free space increased from 147 MB to 284 MB.

## Decoder snapshot retention

After v0.1.19 passed browser acceptance and the 41-sample soak, 17 superseded
decoder-guest snapshots were removed. The guest now retains only:

- `clean-debian12` - clean operating-system baseline;
- `pre-radev1-v0-1-15` - architectural checkpoint before RADEV1; and
- `pre-dashboard-lan-v0-1-21` - immediate rollback for the active token-free
  dashboard and decoder service.

The cleanup did not stop or modify the active guest. Post-cleanup checks showed
both services active, decoder v0.1.19 healthy and connected to the Kiwi, zero
sessions and Reporter disabled. After v0.1.22 acceptance, the superseded
`pre-test-race-v0-1-19` snapshot was also removed. After v0.1.24 acceptance,
`pre-reporter-v0-1-22` was superseded by `pre-reporter-v0-1-24` and removed.
After Reporter v0.1.28 acceptance, the v0.1.24-v0.1.27 Reporter snapshots were
superseded by `pre-reporter-v0-1-28` and removed.
After decoder v0.1.21 acceptance, `pre-dashboard-v0-1-20` was superseded by
`pre-dashboard-lan-v0-1-21` and removed. The Reporter sidecar itself was
unchanged and remains covered by the new immediate rollback snapshot.
Future decoder deployments use the dry-run-first
`tools/prune-decoder-snapshots.ps1` helper after acceptance and soak testing.

## v0.1.20 common calling frequencies

The extension panel now provides 18 common FreeDV activity frequencies from
160 metres through QO-100. Each entry tunes the displayed RF frequency and
applies its listed sideband. The automatic profile also handles the 60-metre
USB exception rather than applying the general below-10-MHz LSB rule there.

A real browser verified:

1. The normal extension menu exposed FreeDV v0.1.20 and the selector contained
   all 18 supplied entries.
2. Selecting 5.4035 MHz tuned the receiver to 60 metres USB with the 700D
   800-2,200 Hz filter.
3. Selecting 7.177 MHz tuned the receiver to 40 metres LSB with the 700D
   -2,200 to -800 Hz filter.
4. On this normal 0-30 MHz Kiwi, selecting 10489.640 MHz QO-100 showed the
   downconverter/transverter guidance, reset the selector and left the
   existing 7.177 MHz tune unchanged.
5. Selecting the most-common 14.236 MHz entry tuned USB and Help documented
   the list, 60-metre exception, QO-100 offset requirement and Start behavior.
6. The bundled 700D test reached `100% / test passed`, returned decoded audio,
   reported zero dropped frames and kept Reporter disabled. Closing the
   extension restored normal USB receiver mode and returned the decoder to
   zero sessions with no camper.

## v0.1.19 sideband and filter profiles

The extension now follows the amateur HF voice convention automatically:
below 10 MHz it selects LSB, covering 40 metres and the lower-frequency bands;
at 10 MHz and above it selects USB. A retune or manual receiver-mode change
reapplies the correct sideband while FreeDV is open. Closing the extension
restores the receiver mode and passband that were active before it opened.

Each HF mode uses its upstream occupied bandwidth centred at 1,500 Hz, plus
200 Hz of tuning/acquisition headroom on each edge. The panel displays the
active sideband and signed filter limits. A real browser verified:

1. At 7.020 MHz, 700D selected LSB with a -2,200 to -800 Hz filter.
2. At 14.236 MHz, 700D selected USB with an 800 to 2,200 Hz filter.
3. At 14.236 MHz, selecting 1600 changed the filter to 725-2,275 Hz and
   selecting 700E changed it to 550-2,450 Hz.
4. Help documented the 10 MHz sideband boundary, occupied-bandwidth profiles,
   filter headroom and the 2400A/2400B integration limitation.
5. The bundled 700D test reached `100% / test passed`; the decoder showed one
   camper/session, returned 24 decoded frames, zero dropped frames and
   Reporter disabled. Completion and Close returned it to zero sessions,
   un-camped and Reporter disabled.
6. Closing at 14.236 MHz restored the pre-extension USB mode.

The v0.1.19 Kiwi build passed the pinned-source, extension-registration,
ordinary-menu, optimized-asset and production-ARM candidate checks before the
atomic restart. No automatic rollback occurred.

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

## v0.1.16 browser and transport acceptance

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

From 2026-07-15 19:48:41 UTC through 19:58:32 UTC, the Test-race fix completed
fresh, parallel 41-sample soaks at 15-second intervals:

- Kiwi: 41/41 samples passed with v0.1.21 active, firmware 1.901, healthy
  service/status/root HTML, no deployment wrappers, zero critical matches and
  zero `kiwid.service` restarts.
- Decoder guest: 41/41 samples passed with decoder v0.1.19 connected and
  healthy, both services active, zero sessions, no camper, Reporter disabled
  and zero critical matches. Its two restart-count entries occurred during
  the preceding Kiwi restart/reconnection window and did not increase during
  the accepted browser tests or soak.
- Kiwi soak log SHA-256:
  `e4dd569758479d1c60c470a90a04738b629a0f202038529d852bbcfe32b56672`.
- Decoder soak log SHA-256:
  `a4569705d3e1ab42b679c230b2d18a4e78dc77f4efcc6a86b720409390b74382`.
- Ignored evidence directory:
  `backups/freedv-test-race-v0-1-21-20260715T194813Z/`.

From 2026-07-15 19:07:20 UTC through 19:17:10 UTC, v0.1.20 completed fresh,
parallel 41-sample soaks at 15-second intervals:

- Kiwi: 41/41 samples passed with v0.1.20 active, firmware 1.901, healthy
  service/status/root HTML, no deployment wrappers, zero critical journal
  matches and zero `kiwid.service` restarts after activation.
- Decoder guest: 41/41 samples passed with both services active, decoder
  v0.1.18 healthy and connected, zero sessions, no camper, Reporter disabled
  and zero critical matches.
- Kiwi soak log SHA-256:
  `64673d926afbc4df3ead355a77fa8d539176d69feee02f047708ab9ac937479c`.
- Decoder soak log SHA-256:
  `a3a594e3413ef753c0ae10a0965dc1acf445bc0a2758d7c7619b127e870bf937`.
- Ignored evidence directory:
  `backups/freedv-v0-1-20-20260715T190653Z/`.

From 2026-07-15 17:36:35 UTC through 17:46:35 UTC, v0.1.19 completed fresh,
parallel 41-sample soaks at 15-second intervals:

- Kiwi: 41/41 samples passed with v0.1.19 active, firmware 1.901, healthy
  service/status/root HTML, no deployment wrappers, zero critical journal
  matches and zero `kiwid.service` restarts after activation.
- Decoder guest: 41/41 samples passed with both services active, decoder
  v0.1.18 healthy and connected, zero sessions, no camper, Reporter disabled
  and zero critical matches.
- Kiwi soak log SHA-256:
  `44f5fa5adb2ab3ac39ba2ba52e2dac37cf91ca68c317241fb434c5432efc340f`.
- Decoder soak log SHA-256:
  `7f2ab1da13d1f04ad5e75da0bcfc420bb231ee4f9f46818d4a4a4863e50c5ff3`.
- Ignored evidence directory:
  `backups/freedv-v0-1-19-20260715T173601Z/`.

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
