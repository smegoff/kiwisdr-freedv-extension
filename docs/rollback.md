# Backup and rollback runbook

## Preferred full-recovery gate

1. Insert a spare microSD into the powered Kiwi.
2. Open Admin > Backup and select **Click to write backup SD card**.
3. Wait for successful finalization, shut down, and remove the card before the
   next boot. Label it with firmware `1.901` and the UTC date.
4. Run `tools/backup-kiwi.ps1` with `KIWI_PASSWORD` set in the process
   environment. Verify the archive checksum and copy the result to Proxmox
   independent backup storage.
5. Do not treat the receiver as having physical disaster recovery until both
   artifacts exist.

## Current reduced gate (no spare microSD available)

The owner accepted a source/config deployment before a backup microSD was
available. The verified configuration archive is stored under the ignored
`backups/` workspace directory and the original 1.901 binaries are retained at
`/root/freedv-releases/baseline-1.901` on the Kiwi. This protects against a
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
`tools/rollback-kiwi-release.sh baseline-1.901` for an explicit software
rollback. The decoder LXC is snapshotted before upgrades and can be rolled back
independently.

For the reference deployment, the Kiwi release is `freedv-v0-1-16`, the
decoder guest runs `0.1.16`, and the latest pre-upgrade guest snapshot is
`pre-freedv-v0-1-16`. Additional earlier snapshots, including
`pre-radev1-v0-1-15`, remain available. If only
RADEV1 fails, first run `tools/set-decoder-radev1.sh 0`, turn **RADEV1 off** in
Admin > Extensions > FreeDV and retest the legacy modes. If the decoder
upgrade itself fails, restore the decoder-guest snapshot or retained previous
binary
and restart `freedv-decoder.service`. If the Kiwi candidate fails, use the
immediate `freedv-v0-1-15` release or run
`tools/rollback-kiwi-release.sh baseline-1.901` for the stock firmware behavior.

After either rollback, require decoder `/healthz` to show `status=ok`, zero
sessions
and Reporter disabled. On the Kiwi require firmware 1.901, stock root HTML and
`/status`, then run the stability soak before returning it to service.

The older `pre-tdoa-v0-1-3` snapshot preserves the former transport-reset
boundary. Do not restore the obsolete port-8074 ingress design into the live
environment unless both Kiwi and decoder guest are intentionally returned to
that older, protocol-compatible state.

## Full recovery

Power off the Kiwi, insert the labelled backup card, power on, and allow the
automatic eMMC reflash to finish. Power off, remove the card, and boot normally.
Confirm firmware 1.901, configuration hashes, receiver operation and admin
access before returning the receiver to service.
