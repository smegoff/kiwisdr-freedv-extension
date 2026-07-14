# FreeDV on KiwiSDR: feasibility and architecture

## Decision

External decoding is the production design. The target KiwiSDR 2 runs firmware
1.901 on a single-core AM335x with about 483 MB RAM and already carries the RF,
waterfall, receiver and networking workload. CT 112 provides 2 vCPU and 2 GB
RAM, and libcodec2 opened every required legacy mode there. Native decoding
remains an adapter boundary only.

RADE is pinned as tools-only at `peterbmarks/radae_decoder` commit
`73016461252822002a256b2813432bc6e2d6f87a`. It remains disabled because the
upstream project describes it as experimental and not fully reviewed.

## Architecture

```text
public browser
  | standard Kiwi extension control + standard SND audio
  v
Kiwi receiver channel (firmware 1.901)
  ^  normal camper SND packets       | rev_bin PCM + rev_txt status
  |                                  v
CT 112 outbound Kiwi monitor connection
  -> resampler -> Codec2 backend -> resampler
  -> localhost health/metrics
  -> disabled RX-only Reporter sidecar
```

This is the same transport family used by John's monitor/TDoA support. It
avoids the failed design in which the Kiwi attempted to run a new Mongoose
WebSocket client and avoids a second browser audio implementation. Camping
does not consume an additional receiver.

## Measured external headroom

Earlier CT measurements processed eight simultaneous 700D test clients for 30
seconds with zero sequence drops. Mean, p95 and maximum request times were
1.48, 2.72 and 43.51 ms. Production is nevertheless capped at one session
until live RF/audio validation is complete. The v0.1.5 Release build exercises
every legacy mode with assertions enabled and includes the exact-token camper
control regression test. It is now the production CT binary and passed a real
browser `Start -> Stop -> Start -> Close` cycle with zero dropped frames or
authentication failures, followed by a 41-sample service soak.

## Native decoder gate

Native libcodec2 may be enabled only if every mode has a real-time factor no
greater than 0.50, sustained Kiwi CPU remains below 80%, p95 decoder block
time is below half the represented audio duration, memory is bounded, and a
30-minute full receiver/waterfall load produces no underrun, sequence,
watchdog or thermal fault. The current hardware has not passed that gate.
