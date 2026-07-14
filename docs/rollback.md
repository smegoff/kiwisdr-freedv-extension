# Backup and rollback runbook

## Preferred full-recovery gate

1. Insert a spare microSD into the powered Kiwi.
2. Open Admin > Backup and select **Click to write backup SD card**.
3. Wait for successful finalization, shut down, and remove the card before the
   next boot. Label it with firmware `1.901` and the UTC date.
4. Run `tools/backup-kiwi.ps1` with `KIWI_PASSWORD` set in the process
   environment. Verify the archive checksum and copy the result to Proxmox
   backup storage.
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

For the current v0.1.5 deployment, CT 112 has snapshot
`pre-freedv-v0-1-5`. The prior production decoder binary and service files are
also retained root-only under
`/root/freedv-rollbacks/pre-v0-1-5-20260713T1823Z`. If only the decoder upgrade
fails, restore that binary atomically and restart `freedv-decoder.service`. If
the Kiwi candidate fails, first run
`tools/rollback-kiwi-release.sh baseline-1.901`; CT v0.1.5 may remain connected
and idle while the stock Kiwi ignores its authenticated polling command.

The older `pre-tdoa-v0-1-3` snapshot preserves the former transport-reset
boundary. Do not restore the obsolete port-8074 ingress design into the live
environment unless both Kiwi and CT are intentionally returned to that older,
protocol-compatible state.

## Full recovery

Power off the Kiwi, insert the labelled backup card, power on, and allow the
automatic eMMC reflash to finish. Power off, remove the card, and boot normally.
Confirm firmware 1.901, configuration hashes, receiver operation and admin
access before returning the receiver to service.
