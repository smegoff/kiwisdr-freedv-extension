# Guided one-shot installer

The guided installer runs on the Kiwi and coordinates the decoder, Kiwi source
build, configuration backup, candidate verification, atomic activation and
automatic software rollback. It supports:

- a private Debian 11 or Debian 12 VM/LXC decoder; or
- a decoder running locally on a BeagleBone AI-64 Kiwi host.

The local option is not supported on the original BeagleBone Green or Black.
Those boards must use the external decoder option.

> [!CAUTION]
> This script builds and replaces the executable used by a live KiwiSDR. A
> power failure, storage failure, incompatible source tree or interrupted
> operation can leave the receiver unavailable. Atomic software rollback
> cannot repair damaged eMMC, a bootloader failure or hardware. A supported,
> verified bootable backup microSD is the only full physical recovery path.
> Decoder snapshots and configuration archives are additional rollback aids,
> not substitutes for the recovery card.

Read [Backup and rollback](rollback.md) before running the installer. Never
pipe a downloaded script directly into a root shell. Clone the repository,
review the checked-out script, and run that local copy.

## What the installer does not do

The installer does not create a VM or LXC, alter Proxmox, configure a router,
open a firewall, update Kiwi firmware, enable FreeDV Reporter or prove live RF
speech quality. For external mode, create the private decoder guest first and
take a snapshot or independent backup. See
[External decoder guest](external-decoder-vm.md).

It also refuses to perform an in-place decoder software upgrade. A fresh guest
may use the install option; a guest that already has `freedv-decoder` uses the
configuration-only option. Existing decoder releases are upgraded through the
separate atomic decoder release procedure.

## Validated operating systems

The script discovers the operating system on the Kiwi and, in external mode,
on the decoder guest. It does not ask the operator to choose an OS profile.

| Host | Accepted OS | Dependency behavior |
| --- | --- | --- |
| KiwiSDR 2 / BeagleBone host | Debian 11 or Debian 12, Kiwi firmware 1.901 | Builds the pinned Kiwi source and deploys only the verified production binary |
| External decoder VM/LXC | Debian 11 or Debian 12, 64-bit | Uses the distribution Codec2 when its API is complete; otherwise builds the pinned Codec2 source |
| Local AI-64 decoder | Debian 11 or Debian 12, AArch64 | Applies the same Codec2 capability check and ARM64 RADEV1 build |

Debian 11 ships
[`libcodec2-dev` 0.9.2-4](https://packages.debian.org/bullseye/libcodec2-dev),
which does not expose every API needed by this integration. On Debian 11 the
decoder installer therefore normally builds the pinned Codec2 1.2.0 source
after checking the installed headers and `pkg-config` version. Debian 12
normally uses its packaged Codec2 when all
required modes, including 700E and reliable text, are present. The decision is
capability-based so a backport or locally installed compatible package is not
needlessly replaced.

Any other distribution or Debian major version is refused before source or
live configuration is changed.

## Prepare the hosts

On the Kiwi, obtain the two pinned source trees:

```bash
git clone https://github.com/smegoff/kiwisdr-freedv-extension.git /root/kiwi-freedv
git clone https://github.com/jks-prv/KiwiSDR.git /root/KiwiSDR
git -C /root/KiwiSDR checkout --detach 417e2c8add196e879b8cc4eb4a488b35b4bf0df7
```

For an external decoder:

1. Create a private Debian 11 or 12 VM or unprivileged LXC with at least two
   vCPU, 2 GB RAM and 16 GB storage.
2. Give it a stable private IPv4 address and accurate system time.
3. Permit outbound TCP 8073 from the decoder to the Kiwi. Permit SSH only from
   the management LAN. Do not add public port forwarding.
4. Install the operator's SSH public key for `root`, connect manually once,
   verify the host fingerprint, and keep strict host-key checking enabled.
5. Take a current snapshot or independent guest backup.

The script requires `root@host` for the remote preparation because it installs
systemd units and root-owned configuration. It never accepts a password on its
command line and never disables SSH host-key verification.

## Inspect the plan without changing anything

Run the interactive dry run first:

```bash
cd /root/kiwi-freedv
sudo ./tools/install-freedv.py --dry-run
```

The prompts select local AI-64 or external decoder, addresses, fresh install
or configuration-only operation, recovery status and optional experimental
RADEV1. RADEV1 and Reporter are disabled by default. A dry run does not inspect
or change either host; it prints the resolved plan only.

## Run the guided installation

```bash
cd /root/kiwi-freedv
sudo ./tools/install-freedv.py
```

The installer displays the full risk notice and requires the exact phrase
`I ACCEPT THE KIWI DEPLOYMENT RISK`, followed by `APPLY`. These acknowledgements
cannot make an unsafe recovery plan safe; they are there to stop accidental
execution.

The guarded sequence is:

1. detect Debian versions, hardware and resources;
2. require healthy KiwiSDR 1.901 and the pinned Kiwi source commit;
3. refuse another deployment wrapper and require two zero-listener readings;
4. create and structurally verify a root-only Kiwi recovery archive;
5. copy that archive to independent storage or the snapshotted decoder guest;
6. prepare the stopped decoder and retain its previous configuration state;
7. apply the idempotent overlay, configure a root-only shared secret, and
   build the production embedded `kiwid.bin`;
8. verify the binary, embedded extension and source commit;
9. require two more zero-listener readings;
10. activate a unique versioned release through the Kiwi health gate;
11. start the decoder and require Kiwi and decoder health samples.

Reporter remains disabled. RADEV1 is enabled only if explicitly selected and
its offline reference/load tests pass.

## Reduced recovery

The preferred choice is a verified Kiwi backup microSD. If no card exists, the
installer can continue only after the operator explicitly accepts reduced
recovery.

- External mode streams the fresh Kiwi recovery archive to root-only storage
  on the independently protected decoder guest.
- Local AI-64 mode has no independent guest, so `--backup-copy` is mandatory
  and must name remote storage in `user@host:/absolute/path` form.

Reduced recovery can restore configuration and a previous working executable.
It cannot recover failed eMMC or a damaged bootloader. Obtain and verify a
supported backup microSD as soon as possible.

## Non-interactive example

Use non-interactive mode only after the interactive dry run and host-key
verification. Every important assumption must be explicit:

```bash
sudo ./tools/install-freedv.py \
  --mode remote \
  --decoder-ip 192.0.2.40 \
  --kiwi-ip 192.0.2.30 \
  --decoder-ssh root@192.0.2.40 \
  --skip-remote-install \
  --decoder-rollback-ready \
  --backup-sd-present \
  --legacy-only \
  --acknowledge-risk \
  --yes
```

The documentation addresses are reserved examples. Replace them with private
site addresses. `--skip-remote-install` means that an existing decoder package
will only be reconfigured. Use `--install-remote-decoder` only on a fresh guest.

## Automatic failure handling

Before changing Kiwi configuration, the installer preserves `kiwi.json`, the
active release, the live production binary, service definition and root-only
decoder secret. A failed build is never deployed. A failed activation restores
the previous atomic Kiwi release. A later failure restores `kiwi.json`,
restarts Kiwi and waits for firmware 1.901 health.

For an existing external decoder, configuration-only preparation records the
old package/configuration and previous service state. If the Kiwi phase fails,
the installer restores that state. Fresh external or local application files
are removed after a failed coordinated installation; downloaded build
dependencies may remain because removing system packages automatically would
be a greater recovery risk. The confirmed VM/LXC snapshot remains the final
decoder rollback point.

Automatic recovery can itself fail, for example after power or storage loss.
The script reports that condition prominently and stops. Do not retry blindly;
use the archive, the retained release, the decoder snapshot or the physical
recovery card according to [rollback.md](rollback.md).

## Acceptance after the script passes

`INSTALLATION PREPARATION PASSED` is not final production acceptance. In a real
browser:

1. verify FreeDV appears and the panel renders;
2. run the bundled 700D Test and confirm returned speech and zero dropped
   frames;
3. run a normal legacy and, if enabled, RADEV1 session;
4. stop and close the extension and verify normal receiver audio is restored;
5. verify Reporter is still disabled; and
6. complete the documented ten-minute stability soak (30 minutes for a local
   AI-64 deployment).

Only after acceptance should superseded decoder snapshots be pruned. Retain a
clean OS baseline, the immediate rollback point and any still-useful
architectural checkpoint; record local snapshot names in private deployment
evidence rather than portable public documentation.
