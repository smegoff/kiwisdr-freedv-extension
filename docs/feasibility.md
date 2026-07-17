# FreeDV on KiwiSDR: feasibility and architecture

## Decision

External decoding is the production design. The target KiwiSDR 2 runs firmware
1.901 on a single-core AM335x with about 483 MB RAM and already carries the RF,
waterfall, receiver and networking workload. The reference decoder guest
currently provides 4 vCPU and 2 GB
RAM, and libcodec2 opened every required legacy mode there. Native decoding
remains an adapter boundary only.

John's current KiwiSDR source also supports the BeagleBone AI-64. Its two
2 GHz Cortex-A72 cores and 4 GB RAM make a local companion decoder plausible,
so this repository now includes an ARM64 build, loopback activation, resource
limits, live soak and rollback path. No physical AI-64 has been available for
execution, so this is an implemented experimental target rather than a tested
replacement for the decoder guest. See [ai64-local-decoder.md](ai64-local-decoder.md).

The current guest is an unprivileged Proxmox LXC. A full VM uses the same
architecture and service package when stronger kernel isolation is preferred.
See [external-decoder-vm.md](external-decoder-vm.md) for the engineering reasons,
VM/LXC choice and deployment procedure.

RADEV1 now uses the official portable C implementation at the reviewed,
V1-only pin `peterbmarks/radae_nopy@6e6fff3fc0546363693b60b52f463e08c71117e6`
and FARGAN/Opus pin `940d4e5af64351ca8ba8390df3f555484c567fbb`.
It is disabled by default in the repository and requires matching decoder and Kiwi
administrator feature flags.

## Architecture

```text
public browser
  | standard Kiwi extension control + standard SND audio
  v
Kiwi receiver channel (firmware 1.901)
  ^  normal camper SND packets       | rev_bin PCM + rev_txt status
  |                                  v
private decoder guest outbound Kiwi monitor connection
  -> resampler -> Codec2 or RADEV1 backend -> resampler
  -> localhost health/metrics
  -> disabled RX-only Reporter sidecar
```

This is the same transport family used by John's monitor/TDoA support. It
avoids the failed design in which the Kiwi attempted to run a new Mongoose
WebSocket client and avoids a second browser audio implementation. Camping
does not consume an additional receiver.

## Measured external headroom

Earlier decoder-guest measurements processed eight simultaneous 700D test clients for 30
seconds with zero sequence drops. Mean, p95 and maximum request times were
1.48, 2.72 and 43.51 ms. Production is nevertheless capped at one session
until live RF/audio validation is complete. The decoder 0.1.19 Release build exercises
every legacy mode with assertions enabled and includes the exact-token camper
control regression test. It is now the reference production decoder binary and passed a real
browser Help, Test, normal Start and Stop cycle with zero dropped frames or
authentication failures, followed by a 41-sample service soak. A forced process
hang was recovered automatically in about 33 seconds. Adding CPU cores did not
address the earlier wait state because its cause was a blocked synchronous
WebSocket loop, not decoder headroom.

The deterministic Test path also had a control race rather than a compute
limit. Early `rev_txt` status could arrive before Kiwi completed its MON-to-SND
camper transition, leaving both sides waiting at zero percent. v0.1.19 repeats
status while waiting, and Kiwi v0.1.21 uses the decoder's authenticated second
job poll after the camper acknowledgement as the authoritative readiness
fallback. Three consecutive browser tests armed within 2.4 seconds and passed.

The RADEV1 reference waveform represented about 11.9 seconds of modem audio.
The reference decoder guest synchronized, produced 181,600 speech samples and completed the decode
in 0.253-0.256 seconds: real-time factor about 0.0215. An eight-worker,
20-repetition stress test completed with per-worker real-time factor 0.0855
and peak container memory 385,695,744 bytes. This is ample external-compute
headroom; production remains capped at one session for predictable Kiwi audio
and Reporter behavior.

## Native decoder gate

Native libcodec2 may be enabled only if every mode has a real-time factor no
greater than 0.50, sustained Kiwi CPU remains below 80%, p95 decoder block
time is below half the represented audio duration, memory is bounded, and a
30-minute full receiver/waterfall load produces no underrun, sequence,
watchdog or thermal fault. The current hardware has not passed that gate.

The AI-64 option does not weaken this gate and does not link RADEV1 into
`kiwid`. The separate local daemon must first pass its offline RTF gate and
then a 30-minute shared-host test with average system CPU no greater than 80%,
peak CPU no greater than 95%, temperature no greater than 85 C, and no Kiwi
audio, decoder-drop, restart or watchdog event. External decoding remains the
reference production design until those results exist.
