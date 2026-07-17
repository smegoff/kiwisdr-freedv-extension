#!/usr/bin/env bash
set -euo pipefail

output_root=${1:-/root/freedv-installer-backups}
stamp=${2:-$(date -u +%Y%m%dT%H%M%SZ)}
out="$output_root/$stamp"
stage="$out/stage"

[[ $EUID -eq 0 ]] || { echo "run as root" >&2; exit 2; }
[[ -d /root/kiwi.config ]] || { echo "Kiwi configuration is missing" >&2; exit 2; }
[[ ! -e $out ]] || { echo "backup already exists: $out" >&2; exit 2; }
install -d -m 0700 "$stage/root" "$stage/usr/local/bin" "$stage/etc/systemd/system"
cp -a /root/kiwi.config "$stage/root/kiwi.config"
[[ ! -f /root/decoder.env ]] || cp -a /root/decoder.env "$stage/root/decoder.env"
cp -aL /usr/local/bin/kiwid "$stage/usr/local/bin/kiwid"
[[ ! -f /etc/systemd/system/kiwid.service ]] ||
  cp -a /etc/systemd/system/kiwid.service "$stage/etc/systemd/system/kiwid.service"

status=$(wget -q -T 5 -O - http://127.0.0.1:8073/status)
active=unversioned
[[ ! -L /root/freedv-releases/active ]] || active=$(readlink /root/freedv-releases/active)
source_commit=unavailable
[[ ! -d /root/KiwiSDR/.git ]] || source_commit=$(git -C /root/KiwiSDR rev-parse HEAD)
host_fingerprint=unavailable
if [[ -f /etc/ssh/ssh_host_ed25519_key.pub ]]; then
  host_fingerprint=$(ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub | awk '{print $2}')
fi

tar -C "$stage" -czf "$out/kiwi-recovery.tgz" .
entries=$(tar -tzf "$out/kiwi-recovery.tgz" | wc -l)
(( entries >= 5 )) || { echo "backup structural validation failed" >&2; exit 3; }
sha256sum "$out/kiwi-recovery.tgz" > "$out/SHA256SUMS"
{
  printf 'captured_utc=%s\n' "$stamp"
  printf 'active_release=%s\n' "$active"
  printf 'kiwi_source_commit=%s\n' "$source_commit"
  printf 'ssh_host_fingerprint=%s\n' "$host_fingerprint"
  printf 'archive_entries=%s\n' "$entries"
  printf '%s\n' "$status" | grep -E '^(status|sw_version|sdr_hw|users)=' || true
  sha256sum "$stage/usr/local/bin/kiwid"
  printf 'physical_recovery=requires a supported backup microSD\n'
} > "$out/manifest.txt"

# Loose root-only copies allow the orchestrator to restore configuration
# without unpacking an archive after a failed candidate.
cp -a "$stage/root/kiwi.config/kiwi.json" "$out/kiwi.json"
[[ ! -f $stage/root/decoder.env ]] || cp -a "$stage/root/decoder.env" "$out/decoder.env"
chmod 0600 "$out/kiwi.json" "$out/manifest.txt" "$out/SHA256SUMS" "$out/kiwi-recovery.tgz"
[[ ! -f $out/decoder.env ]] || chmod 0600 "$out/decoder.env"
rm -rf "$stage"
printf '%s\n' "$out"
