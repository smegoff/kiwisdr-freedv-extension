#!/usr/bin/env bash
set -euo pipefail

backup=${1:-}
config=${KIWI_CONFIG:-/root/kiwi.config/kiwi.json}
decoder_env=${FREEDV_DECODER_ENV:-/etc/freedv-decoder/decoder.env}
backup_root=$(readlink -f "${KIWI_FREEDV_BACKUP_ROOT:-/root/freedv-ai64-backups}")

[[ $EUID -eq 0 ]] || { echo "run as root" >&2; exit 2; }
[[ -n $backup ]] || { echo "usage: $0 /root/freedv-ai64-backups/activate-<UTC>" >&2; exit 2; }
resolved=$(readlink -f "$backup")
case $resolved in
  "$backup_root"/activate-*) ;;
  *) echo "refusing backup outside $backup_root" >&2; exit 2 ;;
esac
[[ -f $resolved/kiwi.json && -f $resolved/decoder.env ]] || {
  echo "rollback files are incomplete" >&2; exit 2;
}

users=$(wget -qO- http://127.0.0.1:8073/status 2>/dev/null |
  awk -F= '$1 == "users" {print $2; exit}')
[[ $users == 0 ]] || { echo "Kiwi listeners are active" >&2; exit 3; }

systemctl stop freedv-decoder.service freedv-reporter.service 2>/dev/null || true
cp -a "$resolved/kiwi.json" "$config"
cp -a "$resolved/decoder.env" "$decoder_env"
systemctl restart kiwid.service

for _ in $(seq 1 90); do
  root=$(wget -qO- http://127.0.0.1:8073/ 2>/dev/null || true)
  status=$(wget -qO- http://127.0.0.1:8073/status 2>/dev/null || true)
  if [[ $(systemctl is-active kiwid.service) == active &&
        $root == *"KiwiSDR"* && $status == *"status=active"* ]]; then
    echo "AI-64 local decoder configuration rolled back from $resolved"
    exit 0
  fi
  sleep 1
done
echo "Kiwi failed its rollback health gate" >&2
exit 4
