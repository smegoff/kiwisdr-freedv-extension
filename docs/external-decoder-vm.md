# External decoder guest: why and how

The FreeDV modem runs on a private Debian guest instead of on the KiwiSDR. In
this documentation, **guest** means either a full Proxmox QEMU/KVM virtual
machine or an unprivileged Proxmox LXC container.

The reference installation uses an unprivileged LXC named `freedv-decoder`.
Its numeric Proxmox guest ID is site-local and intentionally omitted from the
portable documentation. It is not a full VM. A full VM is architecturally compatible
and runs the same decoder and Reporter services, but it has not yet completed
this project's live acceptance and soak tests.

## Why decoding is external

The target KiwiSDR 2 has a single-core AM335x processor and about 483 MB RAM.
That processor already handles RF sample processing, receiver channels,
waterfalls, audio, networking, the web interface and watchdog-sensitive
real-time work. FreeDV decoding adds modem synchronization, FEC, Codec2,
resampling and metadata processing whose workload varies as a signal acquires
and loses synchronization.

Running that work in a separate guest provides five practical advantages:

1. **Receiver stability.** Decoder CPU spikes cannot starve the Kiwi audio,
   waterfall or network tasks. If the guest stops, the extension falls back to
   the Kiwi service remains available; the extension keeps the receiver silent
   until the decoder returns or the user presses Stop/Close, which then restores
   normal receiver audio.
2. **Known headroom.** The recommended starting allocation is 2 vCPU and 2 GB
   RAM. The reference guest currently has 4 vCPU, although testing confirmed
   that the earlier wait state was a blocked control loop rather than CPU
   exhaustion. Production remains capped at one session until every mode
   completes live-RF acceptance.
3. **Dependency isolation.** libcodec2, libsamplerate, OpenSSL, Boost and the
   Python Socket.IO Reporter client can be updated on Debian without expanding
   the Kiwi firmware image or its runtime dependency set.
4. **Independent rollback.** The guest can be snapshotted, upgraded and rolled
   back without changing the Kiwi. Kiwi releases retain their own atomic
   `baseline-1.901` rollback path.
5. **Neural-codec capacity.** RADEV1 and later multi-session testing can use
   x86 CPU and memory without changing the Kiwi hardware.

Native decoding is therefore disabled until it meets the headroom gate in
[feasibility.md](feasibility.md): every mode at real-time factor 0.50 or lower,
sustained Kiwi CPU below 80%, bounded memory, and no audio, watchdog or thermal
faults during a 30-minute loaded test.

## What the guest does

The browser never connects to the decoder guest. The guest initiates a standard
outbound Kiwi monitor connection, authenticates a FreeDV control poll, and camps
on the receiver channel selected by the extension.

```text
browser                 KiwiSDR                       decoder guest
   | extension control     |                                |
   |---------------------->|                                |
   |                       |<---- outbound monitor WS ------|
   |                       |----- normal SND audio -------->|
   |                       |<---- rev_bin decoded PCM ------|
   |<-- normal Kiwi audio--|<---- rev_txt status -----------|
   |                       |                                |-- Codec2
   |                       |                                |-- health/metrics
   |                       |                                `-- RX-only Reporter
```

The Kiwi remains the only public endpoint. The guest does not expose a decoder
WebSocket, and its health endpoint binds to `127.0.0.1:8074`. Reporter, when the
owner explicitly enables it, makes a separate outbound HTTPS/Socket.IO
connection to `qso.freedv.org`.

## Full VM or LXC?

| Consideration | Full QEMU/KVM VM | Unprivileged LXC |
| --- | --- | --- |
| Isolation | Separate guest kernel; stronger boundary | Shares the Proxmox kernel but uses namespaces and an unprivileged mapping |
| Overhead | Modestly higher RAM, disk and boot overhead | Very low overhead and fast startup |
| Portability | Conventional VM image; straightforward migration to another hypervisor | Best kept on a compatible Proxmox/Linux host |
| Kernel control | Guest owns its kernel and kernel update schedule | Uses the Proxmox host kernel |
| Hardware requirement | Requires CPU virtualization support | No hardware virtualization requirement beyond the host |
| Project status | Architecturally compatible; live acceptance pending | Current tested reference path |

Choose a **full VM** when isolation and portability matter more than the small
resource saving, when unrelated users administer the Proxmox host, or when
future experimental decoder components will be tested in the guest. Choose an
**unprivileged LXC** for the lightest private single-purpose appliance and the
deployment path already exercised by this project.

Do not use a privileged LXC for this service. The decoder does not need device
passthrough, nesting, Docker or host filesystem access.

## Resource sizing

The production starting point for either guest type is:

| Resource | Allocation | Reason |
| --- | ---: | --- |
| OS | Debian 12, 64-bit, minimal | Matches the tested packages and systemd units |
| CPU | 2 vCPU | Leaves one decoder session ample margin without pinning host cores |
| Memory | 2 GB fixed | Covers the C++ daemon, Reporter sidecar, build tools and package updates |
| Disk | 16 GB | Allows source, Release builds, journals and one previous package copy |
| Swap | Disabled | Avoids latency stalls; investigate memory pressure instead of swapping audio work |
| NIC | VirtIO on a private bridged LAN | Low overhead and direct reachability to the Kiwi |
| Autostart | Enabled | Restores decoding automatically after Proxmox maintenance |

The tested RADEV1 reference decode ran at real-time factor about 0.0215 on the
reference decoder guest. Eight concurrent stress workers remained below the 0.50 gate at 0.0855
per worker, with peak container memory about 386 MB. The 2 vCPU/2 GB allocation
therefore has substantial headroom for the one-session production limit; it is
not a justification for increasing that limit without end-to-end testing.

For a full VM, the Proxmox default CPU type is portable across unlike cluster
nodes. CPU type `host` can expose more native features, but may restrict live
migration. The decoder does not require `host`; use it only when the VM will
remain on compatible hardware and measurements show a benefit.

Do not increase the one-session production limit merely because the VM has
spare CPU. Multi-session capacity also depends on Kiwi audio behavior, latency,
Reporter rate and end-to-end recovery testing.

## Create a full Proxmox VM

These steps use the Proxmox web interface. Field names can vary slightly by
Proxmox VE release.

### 1. Prepare the installation media

1. Download the current Debian 12 amd64 netinst ISO from Debian.
2. Upload it to a Proxmox storage that permits ISO images.
3. Choose a unique VMID and hostname such as `freedv-decoder`.
4. Decide which private bridge or VLAN reaches the Kiwi. In the original
   installation this is `vmbr0`, but that value is site-specific.

### 2. Run **Create VM**

Use these starting values in the wizard:

- **General:** unique VMID and name `freedv-decoder`.
- **OS:** the Debian 12 netinst ISO and Linux guest type.
- **System:** VirtIO SCSI single controller and QEMU guest agent enabled. UEFI
  is optional; the decoder has no UEFI-specific requirement.
- **Disk:** one 16 GB SCSI disk. Enable discard when the selected thin storage
  supports it.
- **CPU:** one socket, two cores. Keep the portable default CPU type unless the
  `host` trade-off above is intentional.
- **Memory:** 2048 MB. Fixed memory is simplest for predictable audio latency.
- **Network:** VirtIO adapter on the private bridge, Proxmox firewall enabled,
  and a generated locally administered MAC address.

Finish the wizard, but review all values before starting the VM. Record the
VMID, MAC address, storage and bridge in the installation manifest.

### 3. Install minimal Debian

1. Start the VM and open its console.
2. Install Debian 12 without a desktop environment. Select **SSH server** and
   **standard system utilities**.
3. Use the full 16 GB virtual disk. Do not create a swap partition, or disable
   swap after installation.
4. Configure a normal administrative account and SSH keys. Disable direct
   password-based root SSH when normal site policy permits.
5. Reboot, remove the ISO from the virtual CD drive, and verify time sync.

Accurate time is required: the Kiwi rejects authenticated control polls more
than 30 seconds away from its clock.

Install the guest agent and basic tools:

```bash
sudo apt-get update
sudo apt-get full-upgrade -y
sudo apt-get install -y qemu-guest-agent git curl ca-certificates
sudo systemctl enable --now qemu-guest-agent.service
timedatectl status
```

In the Proxmox VM **Options**, confirm **QEMU Guest Agent** and **Start at boot**
are enabled. Restart the VM once and verify Proxmox reports its guest IP.

### 4. Give the VM a stable private address

Create a router-managed DHCP reservation for the recorded MAC, or configure a
static address using the site's normal network management. The address must not
change because the Kiwi checks the decoder connection's source address.

Use the numeric Kiwi address in `FREEDV_KIWI_HOST`. The decoder systemd sandbox
denies arbitrary network destinations, so relying on mDNS or an external DNS
resolver adds permissions that are unnecessary for this appliance.

### 5. Apply network policy

Keep the VM on the private LAN and do not create a public port-forward. Permit:

- VM to Kiwi TCP 8073 for the outbound monitor/camper connection.
- VM to local DNS and NTP only if those services are needed by the chosen
  address and time configuration.
- VM outbound TCP 80/443 for Debian updates, and TCP 443 for FreeDV Reporter
  when it is enabled.
- Management SSH to the VM only from the administration LAN.

No LAN client needs access to decoder port 8074; health and metrics are
loopback-only. The installed `freedv-decoder.service` also contains systemd
`IPAddressAllow` rules. If the Kiwi does not use the repository's development
address, add its actual address with a systemd drop-in before starting the
daemon:

```ini
# sudo systemctl edit freedv-decoder.service
[Service]
IPAddressAllow=<kiwi-private-address>/32
```

Then run `sudo systemctl daemon-reload`. Keep `IPAddressDeny=any`; the explicit
allow-list is an additional containment layer, not a substitute for the
Proxmox firewall.

### 6. Snapshot the clean guest

Install pending Debian updates, stop the VM, and take a snapshot named along
the lines of `clean-debian12-before-freedv`. A stopped snapshot provides the
clearest filesystem-consistent rollback point.

Continue with **Install the decoder service** in
[installation.md](installation.md#4-install-the-decoder-service). The same
commands, environment files and shared secret apply to a VM and an LXC.

## Create the lighter LXC alternative

For the tested LXC path, review and run `deploy/provision-lxc.ps1` as described
in [installation.md](installation.md#3-create-the-decoder-guest). It creates an
unprivileged Debian 12 container with 2 cores, 2 GB RAM, 16 GB disk, no swap,
nesting disabled, autostart and the Proxmox firewall enabled.

The script contains installation-specific API, node, template, storage, bridge,
VMID and MAC defaults. Change those values before using it on another Proxmox
system. Reserve the MAC before starting the container.

## Operate and upgrade the guest

After installing the decoder, verify its local health and service sandbox:

```bash
systemctl --no-pager --full status freedv-decoder.service freedv-reporter.service
curl --fail http://127.0.0.1:8074/healthz
curl --fail http://127.0.0.1:8074/metrics
journalctl -u freedv-decoder.service --since '-15 min' --no-pager
```

Normal idle state is connected to the Kiwi monitor, not camped, with zero
sessions. Starting FreeDV in a browser should move sessions to one and advance
the SND counters; stopping or closing it should return sessions to zero.

Before each guest service upgrade:

1. Confirm no FreeDV session is active.
2. Record current health, package revision and configuration hashes without
   displaying the shared secret.
3. Stop the decoder services and take a Proxmox snapshot.
4. Install and test the new package.
5. Restore the snapshot if service, camper or browser acceptance fails.

A snapshot is a short-term rollback point, not a backup. Retain scheduled
Proxmox backups on separate storage and keep the previous decoder package or
binary until the new version completes its stability soak.

After the soak passes, prune superseded pre-release snapshots. Keep the clean
operating-system baseline, the immediate pre-upgrade rollback and only an
intentional architectural checkpoint that still has recovery value. The
dry-run-first helper accepts either a Proxmox API token or password through
process environment variables and never stores credentials:

```powershell
$env:PVE_API_TOKEN_ID = '<user@realm!token-name>'
$env:PVE_API_TOKEN_SECRET = '<token-secret>'
./tools/prune-decoder-snapshots.ps1 `
  -Api 'https://proxmox.example:8006/api2/json' `
  -Node '<node>' -Vmid <guest-id> `
  -KeepSnapshots clean-debian12,pre-radev1-v0-1-15,pre-current-release

# Review the printed removal list, then repeat the same command with -Apply.
```

Snapshot pruning is part of completing a decoder deployment, but must happen
only after browser acceptance and the stability soak. Record retained and
removed names in ignored deployment evidence. Never remove the current
rollback snapshot just to save space, and do not treat snapshots as a
replacement for scheduled backups.

## Official Proxmox references

- [Proxmox VE Administration Guide](https://pve.proxmox.com/pve-docs/pve-admin-guide.pdf)
- [Proxmox container toolkit documentation](https://pve.proxmox.com/pve-docs/pct.1.html)
- [Proxmox network configuration](https://pve.proxmox.com/wiki/Network_Configuration)
- [QEMU guest agent guidance](https://pve.proxmox.com/wiki/Qemu-guest-agent)
