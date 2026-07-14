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
`input_rate`, `frequency_hz`, and disabled-by-default Reporter station fields.
Only a higher generation changes the CT state. Older jobs are discarded and a
same-generation conflict is rejected. There is no job queue.

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
substring inside `audio_camp=`. This exact-token rule is covered by the v0.1.5
protocol regression test.

libcodec2 input is resampled to 8 kHz. Decoded speech is resampled to the Kiwi
audio rate and sent as a binary WebSocket message beginning `SET rev_bin=`.
The Kiwi relays those bytes through the receiver's ordinary SND stream.

Status is sent as:

```text
SET rev_txt=<generation>,<url-encoded-json>
```

It includes backend, state, sync, SNR, frequency offset, decoded callsign/text,
dropped frames, Reporter state, and an error field. The Kiwi checks generation
and encoded framing before forwarding it to the extension. A five-second
status timeout restores normal receiver audio.

The daemon has no accumulated user-space audio work queue: one SND message is
processed synchronously. A sequence gap, generation/mode/frequency change,
reconnect, backend delay over 500 ms, or invalid frame resets the backend and
discards stale state.

## Local health surface

The daemon binds `/healthz` and `/metrics` to `127.0.0.1:8074`. Metrics cover
Kiwi/camper state, authenticated polls, sessions, SND/decoded frames, drops,
reconnects, generation, sync, decoder CPU time and Reporter state. systemd
uses a 30-second service watchdog. Reporter events use UDP loopback port 8075.
