# Backup and rollback runbook

## Preferred full-recovery gate

The guided installer repeats this risk notice and refuses to continue until
the operator records either the preferred physical-recovery gate or an
explicit reduced-recovery acknowledgement. See
[Guided one-shot installer](one-shot-installer.md).

1. Insert a spare microSD into the powered Kiwi.
2. Open Admin > Backup and select **Click to write backup SD card**.
3. Wait for successful finalization, shut down, and remove the card before the
   next boot. Label it with firmware `1.902` and the UTC date.
4. Run `tools/backup-kiwi.ps1` with `KIWI_PASSWORD` set in the process
   environment. Verify the archive checksum and copy the result to Proxmox
   independent backup storage.
5. Do not treat the receiver as having physical disaster recovery until both
   artifacts exist.

## Current reduced gate (no spare microSD available)

The owner accepted a source/config deployment before a backup microSD was
available. The verified configuration archive is stored under the ignored
`backups/` workspace directory and the original 1.902 binary is retained at
`/root/freedv-releases/baseline-1.902` on the Kiwi. The earlier 1.901 baseline
is retained as historical recovery evidence but is not the automatic rollback
target for a 1.902 candidate. This protects against a
bad extension binary or configuration change, but it does **not** protect
against eMMC, bootloader or hardware failure. Create and remove a supported
backup card at the first opportunity.

## Software rollback

The overlay installer stores replaced extension directories beneath
`/root/freedv-rollbacks/<release-id>`. `tools/deploy-kiwi-release.sh` installs
the candidate as a versioned release and changes an atomic `active` link. It
checks `kiwid.service`, `/status` and embedded root HTML; a failed check
automatically restores the previous link and restarts the service. The explicit
rollback applies the same checks. Run
`tools/rollback-kiwi-release.sh baseline-1.902` for an explicit software
rollback. The decoder LXC is snapshotted before upgrades and can be rolled back
independently.

For the reference deployment, the Kiwi release is `freedv-v0-1-29`, the
immediate Kiwi rollback is the stock `baseline-1.902`, the decoder guest runs `0.1.21`,
and the latest pre-upgrade guest snapshot is
`pre-dashboard-openwebrx-palettes`, taken immediately before the OpenWebRX
palette asset upgrade. The retained
architectural checkpoint is `pre-radev1-v0-1-15`, and `clean-debian12` is the
clean operating-system baseline. Superseded per-release snapshots were removed
after v0.1.21 passed its browser tests and stability soak. If only
RADEV1 fails, first run `tools/set-decoder-radev1.sh 0`, turn **RADEV1 off** in
Admin > Extensions > FreeDV and retest the legacy modes. If the decoder
upgrade itself fails, restore the decoder-guest snapshot or retained previous
binary and restart `freedv-decoder.service`. If the Kiwi candidate fails, run
`tools/rollback-kiwi-release.sh baseline-1.902` for the stock firmware behavior.

After either rollback, require decoder `/healthz` to show `status=ok`, zero
sessions
and Reporter disabled. On the Kiwi require firmware 1.902, stock root HTML and
`/status`, then run the stability soak before returning it to service.

The obsolete `pre-tdoa-v0-1-3` snapshot was removed during retention cleanup.
The old port-8074 ingress design remains unsupported and must not be restored
into the live environment.

For an experimental BeagleBone AI-64 local decoder, use the dedicated
`tools/rollback-ai64-local.sh` command and the exact `activate-*` backup created
by `tools/activate-ai64-local.sh`. It stops the local services, restores both
Kiwi and decoder configuration, restarts Kiwi and verifies root HTML plus
`/status`. Retain the external decoder guest until the AI-64 has passed the
offline benchmark, 30-minute shared-host soak and synchronized live-speech
test. See [ai64-local-decoder.md](ai64-local-decoder.md) for the complete gate.

## Full recovery

Power off the Kiwi, insert the labelled backup card, power on, and allow the
automatic eMMC reflash to finish. Power off, remove the card, and boot normally.
Confirm firmware 1.902, configuration hashes, receiver operation and admin
access before returning the receiver to service.
