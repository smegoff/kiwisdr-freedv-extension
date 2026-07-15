#!/usr/bin/env bash
set -euo pipefail

enabled=${1:-0}
config=${2:-/etc/freedv-decoder/decoder.env}
[[ $EUID -eq 0 ]] || { echo "run as root" >&2; exit 2; }
[[ $enabled == 0 || $enabled == 1 ]] || { echo "enabled must be 0 or 1" >&2; exit 2; }
[[ -f $config ]] || { echo "decoder environment not found" >&2; exit 2; }

stamp=$(date -u +%Y%m%dT%H%M%SZ)
backup="$config.pre-radev1-$stamp"
temporary=$(mktemp "${config}.tmp.XXXXXX")
trap 'rm -f "$temporary"' EXIT
cp -a "$config" "$backup"
awk -v enabled="$enabled" '
  BEGIN { found=0 }
  /^FREEDV_ENABLE_RADE=/ { print "FREEDV_ENABLE_RADE=" enabled; found=1; next }
  { print }
  END { if (!found) print "FREEDV_ENABLE_RADE=" enabled }
' "$config" > "$temporary"
chmod --reference="$config" "$temporary"
chown --reference="$config" "$temporary"
mv -f "$temporary" "$config"
trap - EXIT

systemctl restart freedv-decoder.service
for _ in $(seq 1 30); do
  health=$(wget -qO- http://127.0.0.1:8074/healthz 2>/dev/null || true)
  if [[ $health == *'"release":"0.1.18"'* && $health == *'"kiwi_connected":true'* &&
        $(systemctl is-active freedv-decoder.service) == active ]]; then
    printf 'rade_enabled=%s backup=%s\n' "$enabled" "$backup"
    exit 0
  fi
  sleep 1
done

echo "decoder failed health gate; restoring previous environment" >&2
cp -a "$backup" "$config"
systemctl restart freedv-decoder.service
exit 4
