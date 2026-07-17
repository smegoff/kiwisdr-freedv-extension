#!/usr/bin/env bash
set -euo pipefail

src=${1:-/opt/kiwi-freedv}
config=${KIWI_CONFIG:-/root/kiwi.config/kiwi.json}
kiwi_secret=${KIWI_FREEDV_SECRET_FILE:-/root/decoder.env}
decoder_env=${FREEDV_DECODER_ENV:-/etc/freedv-decoder/decoder.env}
validation_root=${KIWI_FREEDV_VALIDATION_ROOT:-/var/lib/freedv-decoder/ai64-validation}
backup_root=${KIWI_FREEDV_BACKUP_ROOT:-/root/freedv-ai64-backups}

die() { echo "AI-64 activation: $*" >&2; exit 2; }
[[ $EUID -eq 0 ]] || die "run as root"
model=$(tr -d '\0' </proc/device-tree/model 2>/dev/null || true)
[[ $model == *"BeagleBone AI-64"* && $(uname -m) == aarch64 ]] ||
  die "this activation is only for a 64-bit BeagleBone AI-64"
[[ -f $validation_root/offline-pass ]] || die "offline ARM64 validation has not passed"
[[ -f $config && -f $kiwi_secret && -f $decoder_env ]] || die "required configuration is missing"
[[ -x $src/tools/configure-kiwi-freedv.py ]] || die "configuration helper is missing"

secret_hash() {
  awk -F= '$1 == "FREEDV_SHARED_SECRET" {print substr($0, index($0, "=") + 1)}' "$1" |
    tail -n 1 | sha256sum | awk '{print $1}'
}
[[ $(secret_hash "$kiwi_secret") == $(secret_hash "$decoder_env") ]] ||
  die "Kiwi and local decoder secrets do not match"

users() {
  wget -qO- http://127.0.0.1:8073/status 2>/dev/null |
    awk -F= '$1 == "users" {print $2; exit}'
}
[[ $(systemctl is-active kiwid.service) == active ]] || die "kiwid.service is not active"
[[ $(users) == 0 ]] || die "Kiwi listeners are active"
sleep 10
[[ $(users) == 0 ]] || die "Kiwi listeners appeared during the safety gate"

stamp=$(date -u +%Y%m%dT%H%M%SZ)
backup="$backup_root/activate-$stamp"
install -d -m 0700 "$backup"
cp -a "$config" "$backup/kiwi.json"
cp -a "$decoder_env" "$backup/decoder.env"
printf '%s\n' "$backup" > "$validation_root/last-activation-backup"

restore() {
  echo "AI-64 activation failed; restoring configuration" >&2
  systemctl stop freedv-decoder.service freedv-reporter.service 2>/dev/null || true
  cp -a "$backup/kiwi.json" "$config"
  cp -a "$backup/decoder.env" "$decoder_env"
  systemctl restart kiwid.service
}
trap restore ERR

temporary=$(mktemp "${decoder_env}.tmp.XXXXXX")
awk '
  BEGIN { found=0 }
  /^FREEDV_ENABLE_RADE=/ { print "FREEDV_ENABLE_RADE=1"; found=1; next }
  { print }
  END { if (!found) print "FREEDV_ENABLE_RADE=1" }
' "$decoder_env" > "$temporary"
chmod --reference="$decoder_env" "$temporary"
chown --reference="$decoder_env" "$temporary"
mv -f "$temporary" "$decoder_env"

python3 "$src/tools/configure-kiwi-freedv.py" 127.0.0.1 --enable-rade --config "$config"
systemctl restart kiwid.service
healthy=0
for _ in $(seq 1 90); do
  root=$(wget -qO- http://127.0.0.1:8073/ 2>/dev/null || true)
  status=$(wget -qO- http://127.0.0.1:8073/status 2>/dev/null || true)
  if [[ $(systemctl is-active kiwid.service) == active &&
        $root == *"KiwiSDR"* && $status == *"status=active"* ]]; then
    healthy=1
    break
  fi
  sleep 1
done
(( healthy )) || false

systemctl start freedv-decoder.service freedv-reporter.service
decoder_ok=0
for _ in $(seq 1 60); do
  health=$(wget -qO- http://127.0.0.1:8074/healthz 2>/dev/null || true)
  if [[ $(systemctl is-active freedv-decoder.service) == active &&
        $health == *'"status":"ok"'* && $health == *'"kiwi_connected":true'* ]]; then
    decoder_ok=1
    break
  fi
  sleep 1
done
(( decoder_ok )) || false
trap - ERR

echo "AI-64 local decoder activated; backup=$backup"
echo "Start one RADEV1 receiver session, then run:"
echo "  $src/tools/validate-ai64-local.sh 1800"
