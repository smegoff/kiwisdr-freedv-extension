# TDoA-style decoder protocol v2

## Connection direction

The private decoder guest opens the standard Kiwi `SND?camp` WebSocket. The
browser and Kiwi do not open a connection to the decoder guest. After normal
Kiwi authentication, the Kiwi responds with `MSG monitor` and the decoder
client may poll or camp.

## Authenticated job polling

The decoder client sends:

```text
SET freedv_poll=2,<unix-seconds>,<16-hex-nonce>,<hmac-sha256>
```

The signed value is `2|<unix-seconds>|<nonce>`. The Kiwi accepts only the
configured decoder-guest IPv4 address on the local LAN, timestamps within 30 seconds,
previously unseen nonces, protocol 2, and a constant-time matching HMAC. The
secret is loaded from `/root/decoder.env`; it is not stored in `kiwi.json` or
sent to a browser.

A valid poll receives URL-encoded JSON:

```text
MSG freedv_job=<encoded-json>
```

Running jobs contain `protocol`, `generation`, `running`, `rx_chan`, `mode`,
`input_rate`, `frequency_hz`, `test`, `test_ready`, and disabled-by-default
Reporter station fields.
Only a higher generation changes the decoder state. Older jobs are discarded and a
same-generation conflict is rejected. There is no job queue.

For a bundled test, the first authenticated job response has
`test_ready=false`. The decoder requests the camper and can issue its next poll
only after processing Kiwi's camper acknowledgement. Kiwi therefore treats
that authenticated second poll as authoritative readiness, queues a response
for the same generation with `test_ready=true`, then arms John's reference
sample. Returned running status remains an additional fast-path signal and the
decoder repeats it while waiting. The service resets its decoder and
resamplers at the readiness transition and ignores live SND audio beforehand.
This avoids both receiver noise as the first test frame and a deadlock when an
early `rev_txt` message arrives before returned-status routing is established.

## Audio and status

For a running job, the decoder sends `SET MON_CAMP=<rx_chan>`. This subscribes to the
selected receiver's normal post-detector sound packets without allocating
another receiver. SND sequence, flags, byte order and optional IMA ADPCM are
validated. The browser extension temporarily requests uncompressed audio, as
John's reverse-audio path expects linear PCM, and restores the prior setting on
Stop or Close.

On Stop or Close, the decoder sends `SET MON_CAMP=-1`; Kiwi acknowledges the audio
disconnect with `MSG audio_camp=1,0`. Message fields are matched only at the
start of a whitespace-delimited token, so the `camp=` parser must not match the
substring inside `audio_camp=`. This exact-token rule is covered by the 0.1.16
protocol regression test.

libcodec2 input is resampled to 8 kHz. Decoded speech is resampled to the Kiwi
audio rate and sent as a binary WebSocket message beginning `SET rev_bin=`.
The Kiwi relays those bytes through the receiver's ordinary SND stream. From
the moment a FreeDV job starts, that stream carries silence until synchronized
decoded PCM is available. The decoder does not return PCM while its modem is
unsynchronized. The stream also carries silence between returned packets and
during a decoder outage, so normal SSB noise cannot leak through while FreeDV
is running. Stop or Close atomically discards queued decoded PCM and restores
the ordinary receiver stream.

Status is sent as:

```text
SET rev_txt=<generation>,<url-encoded-json>
```

It includes backend, state, sync, SNR, frequency offset, decoded callsign/text,
dropped frames, Reporter state, and an error field. The Kiwi checks generation,
decodes the monitor transport once, and uses the standard encoded extension
message helper for the browser relay. A five-second
status timeout marks the decoder offline but retains the silent FreeDV audio
gate; only Stop or Close restores normal receiver audio.

The daemon has no accumulated user-space audio work queue: one SND message is
processed synchronously. A sequence gap, generation/mode/frequency change,
reconnect, backend delay over 500 ms, or invalid frame resets the backend and
discards stale state.

## Local health surface

The daemon binds `/healthz` and `/metrics` to `127.0.0.1:8074`. Metrics cover
Kiwi/camper state, authenticated polls, sessions, SND/decoded frames, drops,
reconnects, generation, sync, decoder CPU time, status updates, main-loop age
and Reporter state. `/healthz` returns HTTP 503 when the Kiwi control loop is
disconnected or stale. The in-process watchdog exits after 15 seconds without
control-loop progress, allowing systemd to replace a wedged process; the
systemd service watchdog remains a second failure boundary. Reporter events use
UDP loopback port 8075. The periodic event repeats the administrator-owned
Reporter opt-in identity so the sidecar can recover after an independent
restart. The sidecar reports `online` only after FreeDV Reporter sends
`connection_successful`; the lower-level Socket.IO connect event is not an
acceptance signal. An active unsynchronized session publishes its tuned
frequency immediately, while a synchronized session takes precedence if more
than one session is supported in the future.

## Read-only diagnostics surface

Decoder service 0.1.21 adds a separate read-only management surface on
TCP 8076. It does not change protocol v2, create a second Kiwi connection or
accept decoder jobs. `/api/v1/status`, `/api/v1/history` and WebSocket
`/api/v1/stream` are intentionally open to sources admitted by the management
firewall; the daemon has no dashboard login or control endpoints.

The WebSocket carries version 1 `FDWF` binary frames: a 16-byte header with
flags, input sample rate, 512-bin count and sequence, followed by unsigned
-120..0 dBFS bins. Visualization samples come from a bounded non-blocking tap
after Kiwi sound decoding. An overloaded dashboard drops its own samples and
cannot back-pressure the modem. See [dashboard.md](dashboard.md) for the exact
frame layout and security boundary.
