# Installation

This guide installs the receive-only FreeDV framework as two components: a
private Debian 12 decoder guest and a versioned KiwiSDR firmware overlay. It is
written for the current `0.1.5` source and KiwiSDR upstream commit
`417e2c8add196e879b8cc4eb4a488b35b4bf0df7`.

The supplied automation contains defaults for the development installation.
Review and replace every address, VMID, storage name, MAC address, template and
SSH host key before using it elsewhere.

## 1. Prerequisites

- A KiwiSDR running firmware 1.901 with 12 kHz receiver audio channels.
- Root SSH and Admin access to the Kiwi.
- A private Debian 12 LXC or VM with at least 2 vCPU, 2 GB RAM and 16 GB disk.
- The decoder guest must be able to connect outbound to the Kiwi TCP port
  (normally 8073). No inbound decoder port or public port-forward is needed.
- A supported Kiwi backup microSD, a verified streamed configuration archive,
  and a snapshot or backup of the decoder guest are strongly recommended.
- A clean checkout of this repository on both the decoder guest and the Kiwi,
  or a secure way to copy the checkout to each host.

The software rollback protects against a bad extension binary or configuration
change. It does not recover failed eMMC, a damaged bootloader or hardware. See
[rollback.md](rollback.md) before changing the receiver.

## 2. Back up before installation

Use **Admin > Backup > Click to write backup SD card** and remove and label the
completed card after the Kiwi is powered down. From the management workstation,
review the address and pinned SSH host key in `tools/backup-kiwi.ps1`, then run:

```powershell
$secure = Read-Host 'Kiwi root password' -AsSecureString
$env:KIWI_PASSWORD = [Net.NetworkCredential]::new('', $secure).Password
try {
    .\tools\backup-kiwi.ps1 -Kiwi '<kiwi-address>' -Proxmox '<proxmox-address>'
} finally {
    Remove-Item Env:KIWI_PASSWORD -ErrorAction SilentlyContinue
}
```

Confirm that the resulting ignored `backups/kiwi-config-*/` directory contains
`kiwi.config.tgz`, `manifest.json` and a matching SHA-256 file. Structurally
inspect the archive with `tar -tzf` before continuing. Snapshot the decoder
guest immediately before installing or upgrading its service.

Do not build or restart the Kiwi while it has active listeners. Check
`users=0` twice, at least ten seconds apart, before CPU-intensive build work and
again before candidate activation.

## 3. Create the decoder guest

Choose either a full Debian 12 VM or an unprivileged Debian 12 LXC. A VM gives
stronger kernel isolation and is a good default when the Proxmox host has ample
resources. An LXC has less overhead and is the form used by the current tested
deployment. The reasons, trade-offs, resource sizing, Proxmox VM wizard steps,
network rules and operating procedure are in
[external-decoder-vm.md](external-decoder-vm.md).

For the original Proxmox LXC layout, `deploy/provision-lxc.ps1` creates an
unprivileged, autostarting guest. The script deliberately requires the Proxmox
password through a temporary environment variable:

```powershell
$secure = Read-Host 'Proxmox root password' -AsSecureString
$env:PVE_PASSWORD = [Net.NetworkCredential]::new('', $secure).Password
try {
    .\deploy\provision-lxc.ps1 -Api 'https://<proxmox>:8006/api2/json' `
        -Node '<node>' -Vmid 112
} finally {
    Remove-Item Env:PVE_PASSWORD -ErrorAction SilentlyContinue
}
```

Before running it, inspect the Debian template, storage pool, bridge, fixed MAC
and TLS policy in the script. Reserve the resulting MAC in DHCP, start the
guest and make sure its address remains stable. A local DNS or mDNS name is
optional; a fixed private address is simpler for the Kiwi source-address check.

If you create a full VM instead, complete the Debian and QEMU guest-agent setup
in [external-decoder-vm.md](external-decoder-vm.md), then continue with the
same decoder installation below. The application, secret, camper protocol and
acceptance tests are identical for a VM and an LXC.

## 4. Install the decoder service

Clone the repository using a deploy key or copy a verified checkout to the
guest. Do not embed a GitHub token in the clone URL or shell history.

```bash
sudo apt-get update
sudo apt-get install -y git curl
sudo install -d -o "$USER" -g "$USER" /opt/kiwi-freedv
git clone git@github.com:smegoff/kiwisdr-freedv-extension.git /opt/kiwi-freedv
sudo /opt/kiwi-freedv/deploy/install-decoder.sh /opt/kiwi-freedv
```

The installer adds the Debian build dependencies, builds the C++17 daemon in
Release mode, installs the Reporter Python environment, creates the unprivileged
`freedv` service account and installs both systemd units. It enables the units
but deliberately does not start them until configuration and tests pass.

Generate one 256-bit shared secret. Store the same 64 hexadecimal characters on
the guest and Kiwi, but never commit, paste into an issue, or print the value in
logs:

```bash
openssl rand -hex 32
sudoedit /etc/freedv-decoder/decoder.env
```

Set these decoder values:

```dotenv
FREEDV_SHARED_SECRET=<64-hex-character-secret>
FREEDV_KIWI_HOST=<kiwi-private-address>
FREEDV_KIWI_PORT=8073
FREEDV_KIWI_PASSWORD=<optional-Kiwi-receiver-password>
FREEDV_HEALTH_PORT=8074
FREEDV_ENABLE_RADE=0
```

`FREEDV_KIWI_PASSWORD` is the ordinary receiver password used by the Kiwi Web
connection, not the Kiwi root or Admin password. Leave it empty when the
receiver connection does not require one. Preserve the file as
`root:freedv 0640`.

Leave `/etc/freedv-decoder/reporter.env` disabled for initial acceptance:

```dotenv
FREEDV_REPORTER_ENABLED=0
FREEDV_REPORTER_CALLSIGN=
FREEDV_REPORTER_GRID=
FREEDV_REPORTER_MESSAGE=
FREEDV_REPORTER_URL=https://qso.freedv.org
```

Build-time smoke tests confirm that all advertised libcodec2 mode identifiers
can be opened and reset:

```bash
ctest --test-dir /opt/kiwi-freedv/build --output-on-failure
sudo systemctl start freedv-decoder.service freedv-reporter.service
sudo systemctl --no-pager --full status freedv-decoder.service freedv-reporter.service
curl --fail http://127.0.0.1:8074/healthz
curl --fail http://127.0.0.1:8074/metrics
```

The health endpoint intentionally binds to loopback. The decoder should show a
Kiwi monitor connection but zero sessions until a browser starts FreeDV.

## 5. Prepare and build the Kiwi overlay

On the Kiwi, clone John Seamons' source at the exact supported commit and place
this repository at `/root/kiwi-freedv`:

```bash
git clone https://github.com/jks-prv/KiwiSDR.git /root/KiwiSDR
git -C /root/KiwiSDR checkout 417e2c8add196e879b8cc4eb4a488b35b4bf0df7
git clone git@github.com:smegoff/kiwisdr-freedv-extension.git /root/kiwi-freedv
```

Create the root-only Kiwi secret without putting its value in command history:

```bash
install -m 0600 /dev/null /root/decoder.env
read -rsp 'FreeDV shared secret: ' FREEDV_SECRET; echo
printf 'FREEDV_SHARED_SECRET=%s\n' "$FREEDV_SECRET" > /root/decoder.env
unset FREEDV_SECRET
```

Apply the overlay and build the production embedded binary:

```bash
/root/kiwi-freedv/tools/apply-kiwi-overlay.sh /root/kiwi-freedv /root/KiwiSDR
/root/kiwi-freedv/tools/run-kiwi-build.sh
```

The build runs in the background and records output in
`/root/freedv-build.log`. Wait for `/root/freedv-build.exit`, require its value
to be `0`, then record the candidate hash:

```bash
while test ! -f /root/freedv-build.exit; do sleep 5; done
test "$(cat /root/freedv-build.exit)" = 0
file /root/build/kiwid.bin
sha256sum /root/build/kiwid.bin
```

Do not deploy the development `kiwi.bin`. The build helper forces regeneration
of optimized web assets, their gzip packages, embedded data and the production
`kiwid.bin`.

## 6. Configure and activate the Kiwi candidate

In **Admin > Extensions > FreeDV**, set **Decoder LAN address** to the decoder
guest's private source address. This must match the address from which the
outbound camper connection reaches the Kiwi. Leave Reporter off for the first
test.

Reconfirm the configuration archive, current baseline health, two zero-listener
readings and the decoder guest snapshot. Activate with a unique release label:

```bash
/root/kiwi-freedv/tools/deploy-kiwi-release.sh /root/build \
    freedv-v0-1-5-$(date -u +%Y%m%dT%H%M%SZ)
```

The deployment script captures the current production executable as
`baseline-1.901`, switches an atomic active link, restarts `kiwid.service`, and
checks the service, `/status` and root HTML for up to 90 seconds. A failed
candidate check automatically restores the previous release.

## 7. Acceptance test

1. Load the receiver in a current Chrome, Firefox or Edge browser and verify
   that **FreeDV** appears in the normal extension menu.
2. Open it, choose **700D**, and select **Start**. On no signal, sync may remain
   `no`, but state must reach `running` and backend must read `codec2`.
3. On the decoder guest, require `freedv_sessions 1`, an active camper and
   increasing SND counters in `http://127.0.0.1:8074/metrics`.
4. Stop and close the extension. Require sessions and camper state to return to
   zero, and confirm the browser's normal receiver audio state is restored.
5. Confirm the Kiwi root receiver and Admin pages still load and that Reporter
   remains disabled.

Inspect both journals for authentication, watchdog, sequence or crash errors:

```bash
# Run on the decoder guest.
journalctl -u freedv-decoder.service -u freedv-reporter.service --since '-15 min' --no-pager

# Run separately on the Kiwi.
journalctl -u kiwid.service --since '-15 min' --no-pager
```

After this no-signal test, validate actual decoded speech with recordings or
known transmissions. The per-mode RF readiness and limitations are recorded in
[modes.md](modes.md).

## 8. Optional FreeDV Reporter

Only enable Reporter after decoding is stable. In **Admin > Extensions >
FreeDV**, enter the station owner's valid callsign and four- or six-character
Maidenhead locator, optionally add a public message, then switch Reporter on.
The Kiwi job overrides the disabled sidecar defaults for that receiving
session. It never uses a public listener's identity.

Start a FreeDV session and check the panel plus:

```bash
cat /tmp/freedv-reporter-state
journalctl -u freedv-reporter.service --since '-10 min' --no-pager
```

Expected states are `connecting` followed by `online`. The Reporter disconnects
when no FreeDV session is active. If it reports `error`, turn the Kiwi setting
off and diagnose the sidecar without interrupting decoding.

## 9. Rollback

To restore the stock Kiwi release explicitly:

```bash
/root/kiwi-freedv/tools/rollback-kiwi-release.sh baseline-1.901
```

Verify root HTML and `/status` after rollback. Restore the pre-install decoder
snapshot independently if its service upgrade failed. Do not restore an older
decoder transport while leaving a protocol-incompatible Kiwi candidate active.
The full physical recovery procedure is in [rollback.md](rollback.md).
