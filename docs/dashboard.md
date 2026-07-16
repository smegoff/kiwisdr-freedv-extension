# Decoder diagnostics dashboard

Decoder service 0.1.21 includes a small read-only web dashboard for diagnosing
the external FreeDV decoder. It is intended for a trusted management LAN and
is not a public KiwiSDR feature.

![Live 700D decoder diagnostics](images/dashboard-live.png)

## What it shows

The dashboard taps the selected receiver channel immediately after Kiwi sound
packet decoding. It therefore shows post-detector audio from zero hertz to
half the decoder input sample rate. It is not an RF/IQ display and does not
replace the Kiwi wideband waterfall.

The main view provides:

- Waterfall, spectrum or split display with Cividis, Viridis, greyscale and
  the OpenWebRX Turbo, Classic/teejeez and HA7ILM palettes. The OpenWebRX
  choices reproduce its current default and two retained historical schemes,
  as defined in
  [OpenWebRX's waterfall source](https://github.com/jketterl/openwebrx/blob/640c5b0b3e19b10d823a7ee703f2a683ec848eac/owrx/waterfall.py).
- Configurable dBFS floor, ceiling, averaging and five- or ten-frame refresh.
- The nominal modem passband and 1500 Hz centre-frequency overlay.
- Current session, backend, sync, SNR, frequency offset, callsign/text and
  Reporter state.
- One-, five- or ten-minute SNR and frequency-offset history.
- Codec2 bits, errors, packet counts, resyncs, clock offset, timing delta and
  sync metric where libcodec2 supplies them.
- Service counters for input, decoded and dropped frames, reconnects, control
  authentication and decoder CPU time.

Unavailable RADEV1 or mode-specific diagnostics are displayed as **Not
available**. The service returns JSON `null`; it does not invent substitute
values. The terminology follows the
[FreeDV GUI statistics window](https://github.com/drowe67/freedv-gui/blob/master/USER_MANUAL.md#stats-window),
and the overlay follows the [FreeDV signal specifications](https://freedv.org/freedv-specification/).

## Access and network boundary

Open:

```text
http://freedv-decoder.local:8076/
```

Use the decoder guest's private address if local name resolution is not
configured. There is no dashboard login or access key. Any host that can reach
TCP 8076 can view the read-only diagnostics, so the network firewall is the
access-control boundary.

Port 8076 must be permitted only from the administration CIDR. Do not add a
public port-forward. Port 8074 remains loopback-only for `/healthz` and
`/metrics`. The dashboard deliberately sends no CORS header and loads no CDN,
external font, analytics or other third-party resource.

> No application authentication is provided. Plain HTTP is acceptable only on
> a trusted, private management LAN. Use a
> private reverse proxy with TLS if the management network is not trusted; do
> not expose the daemon directly to the internet.

## Display behavior

Display preferences are stored only in the current browser's local storage.
**Pause** stops rendering without affecting the decoder; **Clear** clears the
local canvases. The client stops drawing while its tab is hidden and caps
device-pixel scaling at 2 to limit CPU and memory use.

The dashboard is strictly observational. It cannot tune the Kiwi, select a
mode, start or stop a session, restart services, operate Reporter chat, log a
QSO or record audio.

## Data and performance isolation

The modem thread performs only a bounded, non-blocking write to a
single-producer ring. A low-priority worker creates at most ten 1024-point Hann
FFTs per second. If the dashboard path falls behind, diagnostic samples are
dropped and `spectrum_drops` increments; modem input is never queued behind
the visualization.

Each binary WebSocket message begins with this 16-byte little-endian header,
followed by 512 unsigned bins mapping -120 to 0 dBFS:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII `FDWF` |
| 4 | 1 | Protocol version (`1`) |
| 5 | 1 | Flags: bit 0 session active, bit 1 synchronized |
| 6 | 2 | Bin count (`512`) |
| 8 | 4 | Input sample rate in hertz |
| 12 | 4 | Sequence number |
| 16 | 512 | Unsigned spectral bins |

## HTTP API

The management listener provides:

- `GET /api/v1/status`
- `GET /api/v1/history`
- WebSocket `/api/v1/stream`

Status, history and WebSocket requests are available to any source admitted by
the management firewall. Unknown paths, including traversal attempts, are
rejected. APIs never return the decoder-control secret, Reporter credentials or
Kiwi password.

## Troubleshooting

If the page does not load, verify the daemon, listener and firewall from the
decoder guest:

```bash
systemctl is-active freedv-decoder.service
ss -ltn | grep ':8076'
wget -qO- http://127.0.0.1:8076/ | head
journalctl -u freedv-decoder.service --since '-15 min' --no-pager
```

If the page loads but remains idle, start FreeDV from an ordinary Kiwi browser
session. A late dashboard connection is supported. If waterfall frames do not
advance, check `/api/v1/status` for `sessions`, `snd_frames_total`,
`waterfall_frames` and `spectrum_drops`.

Release acceptance can use the bundled 41-sample, ten-minute check from the
decoder guest. Pass `1` as the final argument for an active session or `0` for
idle cleanup:

```bash
sudo ./tools/soak-dashboard.sh 41 15 0.1.21 1
```
