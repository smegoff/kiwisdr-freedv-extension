# FreeDV on KiwiSDR: feasibility and architecture

## Decision

External decoding is the production design. The target KiwiSDR 2 runs firmware
1.902 on a single-core AM335x with about 483 MB RAM and already carries the RF,
waterfall, receiver and networking workload. The reference decoder guest
currently provides 4 vCPU and 2 GB
RAM, and libcodec2 opened every required legacy mode there. Native decoding
remains an adapter boundary only.

The live reference deployment is **not** an AI-64. It is a standard AM335x
KiwiSDR with all FreeDV modem work offloaded to the Proxmox decoder guest.
John's source also supports the BeagleBone AI-64, so this repository retains
an ARM64 build, loopback activation, resource limits, live-soak and rollback
path for a separate future experiment. No physical AI-64 has been available
for execution, and none of that path is enabled on the reference system. See
[ai64-local-decoder.md](ai64-local-decoder.md).

The current guest is an unprivileged Proxmox LXC. A full VM uses the same
architecture and service package when stronger kernel isolation is preferred.
See [external-decoder-vm.md](external-decoder-vm.md) for the engineering reasons,
VM/LXC choice and deployment procedure.

RADEV1 now uses the official portable C implementation at the reviewed,
V1-only pin `freedv/rade_c@a36161bce0fb37daf3f4602344b095f6817dddb1`
and FARGAN/Opus pin `940d4e5af64351ca8ba8390df3f555484c567fbb`.
It is disabled by default in the repository and requires matching decoder and Kiwi
administrator feature flags.

## Architecture

```text
public browser
  | standard Kiwi extension control + standard SND audio
  v
Kiwi receiver channel (firmware 1.902)
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
camper transition, leaving both sides waiting at zero percent. Kiwi extension
v0.1.31 uses the decoder's authenticated second job poll after the camper
acknowledgement as the authoritative readiness signal.

Decoder v0.1.22 exposed a second transport fault: Beast could read the initial
Kiwi monitor frame into its own WebSocket buffer during the handshake, making
a later native-socket `poll()` wait forever even though the frame was already
available. Decoder v0.1.23 consumes the short authentication/monitor bootstrap
before entering its 100 ms socket-polling loop. The live bundled 700D Test then
advanced from 0 to 100 percent, synchronized, returned decoded audio and
passed. A deliberate service restart recovered the same active browser session.
The subsequent 41-sample active soak passed with zero auth failures, drops,
reconnects or crash restarts.

The generated RADEV1 reference waveform synchronized, produced 150,880 speech
samples and completed with a real-time factor of about 0.021 on the decoder
guest. The official `freedv/rade_c` `FDV_offair.wav` recording also
synchronized after resampling its 48 kHz mono PCM to the decoder's 8 kHz modem
input, with a real-time factor of about 0.021. An eight-worker,
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
