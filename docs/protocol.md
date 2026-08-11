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

Decoder 0.1.26 may append a semicolon-separated list of integer frequencies
currently advertised through FreeDV Reporter. When present, the signed value is
`2|<unix-seconds>|<nonce>|<frequencies>` and the entire list is covered by the
HMAC. The Kiwi validates every value, retains at most 48 entries for 120 seconds,
and exposes only those frequency numbers to the extension. Callsigns, locators,
messages, Socket.IO session identifiers and listener identities are not relayed.
The older four-field protocol-v2 poll remains valid so the Kiwi candidate can be
deployed before the decoder candidate.

A valid poll receives URL-encoded JSON:

```text
MSG freedv_job=<encoded-json>
```

Running jobs contain `protocol`, `generation`, `running`, `rx_chan`, `mode`,
`input_rate`, `frequency_hz`, `test`, `test_ready`, and disabled-by-default
Reporter station fields.
Only a higher generation changes the decoder state. Older jobs are discarded and a
same-generation conflict is rejected. There is no job queue.

For a bundled test, the Kiwi selects a deterministic waveform that matches the
test job: RADEV1 uses the bundled RADE waveform and all legacy selector choices
use the established 700D reference. The first authenticated job response has
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
Decoder 0.1.24 queues the modem's larger decoded-speech bursts for no more than
500 ms and emits at most one receiver-sized packet for each incoming SND
packet. Kiwi extension 0.1.32 likewise consumes at most one returned packet per
normal sound cadence, discarding stale excess rather than draining a network
burst into the browser. The Kiwi relays those bytes through the receiver's
ordinary SND stream. From
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

The test waveform is armed only after the authenticated camper-readiness
handshake. It then advances inside the same real-sample callback used by the
selected receiver, reports percentage progress, and stops at the end of the
finite recording. Test jobs carry `test=true`, so the Reporter sidecar excludes
them.

The daemon has no accumulated user-space audio work queue: one SND message is
processed synchronously. A sequence gap, generation/mode/frequency change,
reconnect, backend delay over 500 ms, or invalid frame resets the backend and
discards stale state.

## Local health surface

The daemon binds `/healthz` and `/metrics` to `127.0.0.1:8074`. Metrics cover
Kiwi/camper state, authenticated polls, sessions, SND/decoded frames, drops,
reconnects, generation, sync, decoder CPU time, status updates, main-loop age
and Reporter state. Decoder 0.1.26 also exports the age of the last authenticated
job response and the number of stale-control recoveries. `/healthz` returns
HTTP 503 when the Kiwi control loop is disconnected, when its main loop is
stale, or when no authenticated poll response has arrived for more than ten
seconds. The camper reconnects a stale control socket using its bounded
backoff. The in-process watchdog exits after 15 seconds without main-loop
progress, allowing systemd to replace a fully wedged process; the
systemd service watchdog remains a second failure boundary. Reporter events use
UDP loopback port 8075. The periodic event repeats the administrator-owned
Reporter opt-in identity so the sidecar can recover after an independent
restart. The sidecar reports `online` only after FreeDV Reporter sends
`connection_successful`; the lower-level Socket.IO connect event is not an
acceptance signal. An active unsynchronized session publishes its tuned
frequency immediately, while a synchronized session takes precedence if more
than one session is supported in the future.

## Read-only diagnostics surface

Decoder service 0.1.26 includes a separate read-only management surface on
TCP 8076. It does not change protocol v2, create a second Kiwi connection or
accept decoder jobs. `/api/v1/status`, `/api/v1/history`,
`/api/v1/capture.wav` and WebSocket `/api/v1/stream` are intentionally open
to sources admitted by the management firewall; the daemon has no dashboard
login or control endpoints. The WAV endpoint returns only the bounded,
in-memory latest modem-audio window and never exposes a continuous recording.

The WebSocket carries version 1 `FDWF` binary frames: a 16-byte header with
flags, input sample rate, 512-bin count and sequence, followed by unsigned
-120..0 dBFS bins. Visualization samples come from a bounded non-blocking tap
after Kiwi sound decoding. An overloaded dashboard drops its own samples and
cannot back-pressure the modem. See [dashboard.md](dashboard.md) for the exact
frame layout and security boundary.
