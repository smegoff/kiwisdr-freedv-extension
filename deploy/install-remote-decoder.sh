#!/usr/bin/env bash
set -euo pipefail

src=${1:-/opt/kiwi-freedv}
secret_file=${2:-/root/freedv-one-shot-secret.env}
kiwi_ip=${3:-}
action=${4:-install}
enable_rade=${5:-0}
stamp=$(date -u +%Y%m%dT%H%M%SZ)
backup="/root/freedv-one-shot-backups/$stamp"

die() { echo "remote decoder install: $*" >&2; exit 2; }
[[ $EUID -eq 0 ]] || die "run as root"
[[ $action == install || $action == configure ]] || die "action must be install or configure"
[[ $enable_rade == 0 || $enable_rade == 1 ]] || die "RADEV1 flag must be zero or one"
[[ -f $src/deploy/install-decoder.sh && -f $src/deploy/build-radae.sh ]] ||
  die "repository checkout not found: $src"
[[ -f $secret_file ]] || die "secret transfer file is missing"
secret=$(awk -F= '$1 == "FREEDV_SHARED_SECRET" {print substr($0, index($0, "=") + 1)}' \
  "$secret_file" | tail -n 1)
[[ $secret =~ ^[[:xdigit:]]{64}$ ]] || die "shared secret is invalid"
python3 - "$kiwi_ip" <<'PY'
import ipaddress
import sys
ipaddress.IPv4Address(sys.argv[1])
PY
[[ -r /etc/debian_version ]] || die "the remote decoder must run Debian"
debian_major=$(cut -d. -f1 /etc/debian_version)
[[ $debian_major == 11 || $debian_major == 12 ]] ||
  die "only Debian 11 and Debian 12 are validated"
arch=$(uname -m)
[[ $arch == x86_64 || $arch == aarch64 ]] || die "a 64-bit x86_64 or aarch64 operating system is required"
(( $(getconf _NPROCESSORS_ONLN) >= 2 )) || die "at least two CPU cores are required"
(( $(awk '/^MemTotal:/ {print $2}' /proc/meminfo) >= 1572864 )) || die "at least 1.5 GiB RAM is required"
if [[ $action == install ]]; then
  free_kib=$(df -Pk "$src" | awk 'NR == 2 {print $4}')
  (( free_kib >= 3145728 )) || die "at least 3 GiB free storage is required for a fresh decoder build"
fi

decoder_existed=0
[[ ! -x /usr/local/bin/freedv-decoder ]] || decoder_existed=1
if [[ $action == install && $decoder_existed == 1 ]]; then
  die "fresh-install mode refuses to overwrite an existing decoder; use configure-only or the atomic upgrade procedure"
fi
if [[ $action == configure && $decoder_existed == 0 ]]; then
  die "configure-only requested but no decoder installation exists"
fi
if [[ $action == configure && $enable_rade == 1 ]]; then
  [[ -f /usr/local/lib/librade.so.0.1 &&
     -f /usr/local/share/freedv-rade/radae_nopy.commit &&
     -f /usr/local/share/freedv-rade/opus-fargan.commit ]] ||
    die "RADEV1 was requested but the pinned portable backend is not installed"
fi

decoder_was_active=0
reporter_was_active=0
if systemctl is-active --quiet freedv-decoder.service 2>/dev/null; then
  decoder_was_active=1
  sessions=$(wget -qO- http://127.0.0.1:8074/metrics 2>/dev/null |
    awk '$1 == "freedv_sessions" {print $2; exit}')
  [[ ${sessions:-0} == 0 ]] || die "a decoder session is active"
fi
systemctl is-active --quiet freedv-reporter.service 2>/dev/null && reporter_was_active=1 || true

install -d -m 0700 "$backup"
backup_paths=()
for path in usr/local/bin/freedv-decoder etc/freedv-decoder \
  etc/systemd/system/freedv-decoder.service etc/systemd/system/freedv-reporter.service \
  opt/freedv-reporter usr/local/share/freedv-dashboard; do
  [[ ! -e /$path ]] || backup_paths+=("$path")
done
if (( ${#backup_paths[@]} )); then
  tar -C / -czf "$backup/pre-install.tgz" "${backup_paths[@]}"
fi

restore() {
  echo "remote decoder preparation failed; restoring previous package" >&2
  [[ -z ${temporary:-} ]] || rm -f "$temporary"
  [[ -z ${reporter_temporary:-} ]] || rm -f "$reporter_temporary"
  systemctl stop freedv-decoder.service freedv-reporter.service 2>/dev/null || true
  if [[ -f $backup/pre-install.tgz ]]; then
    tar -C / -xzf "$backup/pre-install.tgz"
  elif (( decoder_existed == 0 )); then
    systemctl disable freedv-decoder.service freedv-reporter.service 2>/dev/null || true
    rm -f /usr/local/bin/freedv-decoder \
      /etc/systemd/system/freedv-decoder.service \
      /etc/systemd/system/freedv-reporter.service
    rm -rf /etc/freedv-decoder /opt/freedv-reporter /usr/local/share/freedv-dashboard
  fi
  systemctl daemon-reload 2>/dev/null || true
  (( decoder_was_active == 0 )) || systemctl start freedv-decoder.service 2>/dev/null || true
  (( reporter_was_active == 0 )) || systemctl start freedv-reporter.service 2>/dev/null || true
  rm -f "$secret_file"
}
trap restore ERR

if [[ $action == install ]]; then
  "$src/deploy/build-radae.sh"
  "$src/deploy/install-decoder.sh" "$src"
  ctest --test-dir "$src/build" --output-on-failure
  /usr/local/bin/rade_modulate_wav -v 0 /usr/local/share/freedv-rade/voice.wav \
    "$src/build/radev1-reference.wav"
  "$src/build/freedv-rade-reference-test" "$src/build/radev1-reference.wav" \
    > "$backup/radev1-reference.txt"
  "$src/tools/test-radev1-load.sh" "$src" 1 20 > "$backup/radev1-load.txt"
fi

decoder_env=/etc/freedv-decoder/decoder.env
temporary=$(mktemp "${decoder_env}.tmp.XXXXXX")
trap 'rm -f "$temporary" "$secret_file"' EXIT
found_secret=0; found_host=0; found_rade=0
while IFS= read -r line || [[ -n $line ]]; do
  case $line in
    FREEDV_SHARED_SECRET=*) printf 'FREEDV_SHARED_SECRET=%s\n' "$secret"; found_secret=1 ;;
    FREEDV_KIWI_HOST=*) printf 'FREEDV_KIWI_HOST=%s\n' "$kiwi_ip"; found_host=1 ;;
    FREEDV_ENABLE_RADE=*) printf 'FREEDV_ENABLE_RADE=%s\n' "$enable_rade"; found_rade=1 ;;
    *) printf '%s\n' "$line" ;;
  esac
done < "$decoder_env" > "$temporary"
(( found_secret )) || printf 'FREEDV_SHARED_SECRET=%s\n' "$secret" >> "$temporary"
(( found_host )) || printf 'FREEDV_KIWI_HOST=%s\n' "$kiwi_ip" >> "$temporary"
(( found_rade )) || printf 'FREEDV_ENABLE_RADE=%s\n' "$enable_rade" >> "$temporary"
install -m 0640 -o root -g freedv "$temporary" "$decoder_env"
rm -f "$temporary" "$secret_file"
trap - EXIT

reporter_env=/etc/freedv-decoder/reporter.env
reporter_temporary=$(mktemp "${reporter_env}.tmp.XXXXXX")
awk '
  BEGIN { found=0 }
  /^FREEDV_REPORTER_ENABLED=/ { print "FREEDV_REPORTER_ENABLED=0"; found=1; next }
  { print }
  END { if (!found) print "FREEDV_REPORTER_ENABLED=0" }
' "$reporter_env" > "$reporter_temporary"
install -m 0640 -o root -g freedv "$reporter_temporary" "$reporter_env"
rm -f "$reporter_temporary"
systemctl daemon-reload
systemctl stop freedv-decoder.service freedv-reporter.service 2>/dev/null || true
install -d -m 0750 -o root -g freedv /var/lib/freedv-decoder
{
  printf 'ready_utc=%s\n' "$stamp"
  printf 'backup=%s\n' "$backup"
  printf 'decoder_was_active=%s\n' "$decoder_was_active"
  printf 'reporter_was_active=%s\n' "$reporter_was_active"
  printf 'decoder_existed=%s\n' "$decoder_existed"
} > /var/lib/freedv-decoder/one-shot-ready
chmod 0640 /var/lib/freedv-decoder/one-shot-ready
trap - ERR
echo "remote decoder prepared and stopped; backup=$backup"
