# TDoA-style decoder protocol v2

## Connection direction

CT 112 opens the standard Kiwi `SND?camp` WebSocket. The browser and Kiwi do
not open any connection to CT 112. After normal Kiwi authentication, the Kiwi
responds with `MSG monitor` and the CT client may poll or camp.

## Authenticated job polling

The CT sends:

```text
SET freedv_poll=2,<unix-seconds>,<16-hex-nonce>,<hmac-sha256>
```

The signed value is `2|<unix-seconds>|<nonce>`. The Kiwi accepts only the
configured CT IPv4 address on the local LAN, timestamps within 30 seconds,
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
Only a higher generation changes the CT state. Older jobs are discarded and a
same-generation conflict is rejected. There is no job queue.

For a bundled test, CT first camps and sends authoritative running status while
`test_ready=false`. The Kiwi then arms John's reference sample and returns the
same generation with `test_ready=true`. CT resets its decoder and resamplers at
that transition and ignores live SND audio beforehand. This prevents receiver
noise or a stale packet from becoming the first test frame.

## Audio and status

For a running job, CT sends `SET MON_CAMP=<rx_chan>`. This subscribes to the
selected receiver's normal post-detector sound packets without allocating
another receiver. SND sequence, flags, byte order and optional IMA ADPCM are
validated. The browser extension temporarily requests uncompressed audio, as
John's reverse-audio path expects linear PCM, and restores the prior setting on
Stop or Close.

On Stop or Close, CT sends `SET MON_CAMP=-1`; Kiwi acknowledges the audio
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
UDP loopback port 8075.
