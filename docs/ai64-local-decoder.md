# BeagleBone AI-64 local RADEV1 decoder

This optional deployment runs the existing FreeDV decoder service on the same
BeagleBone AI-64 as KiwiSDR. It is intended for a Kiwi cape already operating
on John's supported 64-bit AI-64 platform. The project has no AI-64 hardware on
which to execute the procedure yet, so this path is **implemented and
automatically checked, but not hardware-validated**.

John's current KiwiSDR source explicitly lists BBAI-64 support and includes its
device trees, SPI path and flasher tooling. The board provides two 2 GHz
Cortex-A72 cores, 4 GB RAM and 16 GB eMMC. See the
[KiwiSDR repository](https://github.com/jks-prv/KiwiSDR) and
[AI-64 specifications](https://docs.beagleboard.org/boards/beaglebone/ai-64/01-introduction.html).

## Architecture

The local option deliberately does not compile RADEV1 into `kiwid`:

```text
receiver browser
      |
      v
kiwid on TCP 8073 <---- loopback camper ----> freedv-decoder
      ^                                      Codec2 / RADEV1
      |                                      Reporter sidecar
      `----------- returned PCM ------------ local diagnostics
```

The Kiwi extension and decoder continue to use protocol v2, HMAC
authentication, bounded queues, `rev_bin` returned speech and `rev_txt`
status. Only the decoder address changes to `127.0.0.1`. Keeping two systemd
services retains watchdog recovery and makes it possible to stop or roll back
the modem without replacing the Kiwi executable.

## Preconditions

- BeagleBone AI-64 with the Kiwi cape already working under John's supported
  64-bit ARM image and a compatible Kiwi source build.
- Debian 11 or Debian 12. The installer detects the release and uses a pinned
  Codec2 source build when the distribution package lacks the complete API.
- This repository's FreeDV extension deployed on that Kiwi.
- At least two online CPU cores, 3 GiB visible RAM and 3 GiB free storage.
- Root-only `/root/decoder.env` containing the existing 64-hex-character
  `FREEDV_SHARED_SECRET`.
- A verified Kiwi configuration backup and a separate bootable microSD before
  modifying the AI-64 eMMC.
- Stable 5 V supply rated above 3 A and active cooling. The AI-64 is physically
  larger than the standard BeagleBone Green and will generally require a
  different enclosure.

Do not run these scripts on the current BeagleBone Green/Black. The installer
checks the device-tree model, `aarch64`, CPU count, memory and free storage and
refuses other hardware.

## 1. Install without activation

The recommended coordinated path is the
[guided one-shot installer](one-shot-installer.md), selecting **local AI-64**.
It adds Kiwi backup, source-build, deployment and automatic recovery gates
around the component steps below. The manual component path remains useful for
offline hardware validation.

Clone the public repository on the AI-64 and run:

```bash
sudo install -d /opt/kiwi-freedv
sudo git clone https://github.com/smegoff/kiwisdr-freedv-extension.git \
  /opt/kiwi-freedv
sudo /opt/kiwi-freedv/deploy/install-ai64-local.sh /opt/kiwi-freedv
```

The installer:

1. verifies the AI-64 hardware and resource floor;
2. builds the pinned portable RADEV1 and FARGAN sources for ARM64;
3. builds and installs the decoder, Reporter and dashboard;
4. copies the existing Kiwi HMAC secret without displaying it;
5. configures the camper for `127.0.0.1`;
6. binds the dashboard to loopback at five frames per second;
7. installs a systemd resource override;
8. runs CTest, the RADEV1 reference decoder and a one-worker, twenty-repeat
   real-time-factor test; and
9. leaves both services stopped and `FREEDV_ENABLE_RADE=0`.

The installed override gives Kiwi scheduling preference and limits the decoder
to 140% CPU, 1 GiB soft memory pressure and 1.5 GiB maximum memory. A failure
of the offline real-time-factor 0.50 gate prevents the `offline-pass` marker
from being created.

Results are root-readable beneath
`/var/lib/freedv-decoder/ai64-validation/`. They contain hardware and binary
hashes, not the shared secret.

## 2. Controlled activation

Activation changes Kiwi configuration and restarts it once. It requires two
zero-listener readings ten seconds apart, creates a timestamped backup and
restores automatically if either Kiwi or decoder health fails:

```bash
sudo /opt/kiwi-freedv/tools/activate-ai64-local.sh /opt/kiwi-freedv
```

The command sets Kiwi's authorized decoder source and the camper destination
to `127.0.0.1`, enables the two RADEV1 feature gates, keeps Reporter opt-in
disabled and starts the local services. It does not delete or modify an
external decoder guest.

The dashboard initially listens only on `127.0.0.1:8076`. To expose it to the
management LAN, bind it to the AI-64's fixed private address and add a host or
upstream firewall rule limited to the management CIDR. Never port-forward TCP
8076 from the internet.

## 3. Hardware acceptance

Open a normal Kiwi receiver, select FreeDV and start exactly one RADEV1
session. Leave that session running while executing:

```bash
sudo /opt/kiwi-freedv/tools/validate-ai64-local.sh 1800
```

The 30-minute gate polls every five seconds and fails on:

- average system CPU above 80% or a CPU peak above 95%;
- any temperature above 85 C or a missing thermal sensor;
- decoder drops, reconnects, malformed jobs or service restarts;
- loss of the one RADEV1 session, Kiwi page, `/status` or decoder health; or
- watchdog, stalled-loop, segmentation, audio-sequence or fatal journal events.

Successful completion records `live-pass` and a local log. A no-signal RADEV1
session exercises receive processing and resource behavior, but synchronized
on-air speech remains a separate acceptance requirement.

## Rollback and external fallback

Activation records its backup path in
`/var/lib/freedv-decoder/ai64-validation/last-activation-backup`. Roll back with
that exact directory after listeners disconnect:

```bash
sudo /opt/kiwi-freedv/tools/rollback-ai64-local.sh \
  /root/freedv-ai64-backups/activate-<UTC-timestamp>
```

The rollback command accepts only verified `activate-*` directories beneath
the dedicated backup root, stops the local decoder, restores `kiwi.json` and
the decoder environment, restarts Kiwi and verifies receiver HTML and
`/status`.

To return to an external decoder after the AI-64 has passed testing, stop and
disable the local decoder services, configure the external decoder's private
source address with `configure-kiwi-freedv.py`, and restore the matching secret
and feature gates. Keep the external decoder guest intact until the AI-64 has
passed both the 30-minute soak and live synchronized speech acceptance.

## Current validation status

| Item | Status |
| --- | --- |
| AI-64 support in upstream KiwiSDR | Present |
| Portable ARM64 build path | Implemented |
| Board/resource preflight | Implemented |
| Offline RADEV1 RTF gate | Implemented |
| Zero-listener activation and automatic rollback | Implemented |
| Thirty-minute CPU/thermal/reliability gate | Implemented |
| Executed on physical AI-64 | Pending hardware |
| Live synchronized RADEV1 speech on AI-64 | Pending hardware and signal |
