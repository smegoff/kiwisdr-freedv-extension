# Public FreeDV signal monitor

Decoder service 0.1.37 includes an optional spectator page for internet users.
It is a separate surface from the management diagnostics dashboard and is
disabled by default for new installations.

## Security boundary

The decoder creates the waterfall once and can publish the same versioned FFT
frames to two independent listeners:

| Surface | Default listener | Intended audience | Available data |
| --- | --- | --- | --- |
| Management diagnostics | `0.0.0.0:8076`, management firewall only | Station administrator | Full status, counters, history and bounded WAV capture |
| Public signal monitor | `127.0.0.1:8077`, disabled | Internet spectators through HTTPS | Waterfall, spectrum, mode, frequency, input rate, test/live state, sync, SNR and frequency offset |

The public API constructs a new allowlisted object. It does not filter the
management JSON after serialization. It excludes callsign/text, Reporter
state, backend details, authentication counters, queue information, decoder
timing, internal addresses and the WAV capture. `/api/v1/capture.wav` returns
HTTP 404 on the public listener. There are no tuning, session, service or
decoder-control methods.

The listener enforces `FREEDV_PUBLIC_DASHBOARD_MAX_CLIENTS` for WebSockets.
New installations default to 16. HTTP request-rate and per-address connection
limits belong at the HTTPS edge because a loopback decoder sees the reverse
proxy, not the original internet address.

## Enable the loopback listener

Set these values in `/etc/freedv-decoder/decoder.env`:

```dotenv
FREEDV_PUBLIC_DASHBOARD_ENABLED=1
FREEDV_PUBLIC_DASHBOARD_BIND=127.0.0.1
FREEDV_PUBLIC_DASHBOARD_PORT=8077
FREEDV_PUBLIC_DASHBOARD_ASSET_DIR=/usr/local/share/freedv-public-dashboard/current
FREEDV_PUBLIC_DASHBOARD_MAX_CLIENTS=16
```

Restart the decoder and test from inside the decoder guest:

```bash
sudo systemctl restart freedv-decoder.service
wget -qO- http://127.0.0.1:8077/api/v1/status
wget -S -O- http://127.0.0.1:8077/api/v1/capture.wav
ss -lnt | grep 8077
```

The status response should contain only `version`, `release`,
`kiwi_connected` and `session`. The capture request must return 404 and `ss`
must show `127.0.0.1:8077`, never `0.0.0.0:8077` or `[::]:8077`.

## Publish through HTTPS

Internet publication needs a hostname controlled by the Kiwi owner, a valid
TLS certificate and one of these ingress paths:

1. A router forwards TCP 80/443 to an HTTPS reverse proxy in the decoder guest.
2. An authenticated outbound tunnel publishes the HTTPS hostname without an
   inbound router rule.
3. An existing trusted reverse proxy reaches a private listener configured
   specifically for that proxy and protected by the hypervisor firewall.

The same-guest Nginx approach keeps port 8077 loopback-only. Install Nginx and
an ACME client, copy
`deploy/nginx-freedv-public.conf.example` to `/etc/nginx/conf.d/`, replace the
example hostname and certificate paths, validate with `nginx -t`, and reload
Nginx. The template provides HTTPS redirection, WebSocket forwarding, request
rate limiting, four concurrent connections per source address, a 1 KiB request
body ceiling and GET-only access.

Do not publish port 8076 and do not add an internet firewall rule for 8077.
John's KiwiSDR reverse proxy forwards the Kiwi receiver and extension
connections; it does not route this decoder-guest listener. A separate public
hostname or tunnel endpoint is required.

## Acceptance checks

Before publishing DNS or a router rule:

- verify the management dashboard still exposes its expected diagnostics only
  from the management LAN;
- verify the public status and history contain only their documented fields;
- require 404 for the capture, unknown paths and control-style requests;
- verify the public WebSocket viewer ceiling;
- open the HTTPS endpoint in a browser and confirm the waterfall reconnects;
- run a reference test and a live session while watching decoder drops, CPU,
  memory and critical logs; and
- confirm the public hostname cannot reach ports 8074, 8076, SSH or any other
  decoder-guest service.

The reference deployment has completed the loopback listener and browser
acceptance. Internet ingress remains pending a hostname and HTTPS routing
choice.
