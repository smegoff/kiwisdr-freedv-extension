#!/usr/bin/env bash
set -euo pipefail

src=${1:-/opt/kiwi-freedv}
kiwi_secret=${KIWI_FREEDV_SECRET_FILE:-/root/decoder.env}
validation_root=${KIWI_FREEDV_VALIDATION_ROOT:-/var/lib/freedv-decoder/ai64-validation}

die() { echo "AI-64 install: $*" >&2; exit 2; }
[[ $EUID -eq 0 ]] || die "run as root"
[[ -f $src/deploy/install-decoder.sh && -f $src/deploy/build-radae.sh ]] ||
  die "repository checkout not found: $src"

model=$(tr -d '\0' </proc/device-tree/model 2>/dev/null || true)
[[ $model == *"BeagleBone AI-64"* ]] || die "unsupported board: ${model:-unknown}"
[[ $(uname -m) == aarch64 ]] || die "a 64-bit ARM operating system is required"
cpus=$(getconf _NPROCESSORS_ONLN)
(( cpus >= 2 )) || die "at least two online CPU cores are required"
mem_kib=$(awk '/^MemTotal:/ {print $2}' /proc/meminfo)
(( mem_kib >= 3145728 )) || die "at least 3 GiB of visible RAM is required"
free_kib=$(df -Pk "$src" | awk 'NR == 2 {print $4}')
(( free_kib >= 3145728 )) || die "at least 3 GiB of free storage is required for the pinned build"
[[ -f $kiwi_secret ]] || die "missing root-only Kiwi secret: $kiwi_secret"
secret=$(awk -F= '$1 == "FREEDV_SHARED_SECRET" {print substr($0, index($0, "=") + 1)}' \
  "$kiwi_secret" | tail -n 1)
[[ $secret =~ ^[[:xdigit:]]{64}$ ]] || die "Kiwi secret must contain exactly 64 hexadecimal characters"

echo "AI-64 preflight passed: $model, $cpus cores, $((mem_kib / 1024)) MiB RAM"
echo "Building the pinned portable RADEV1 library for ARM64. This may take several minutes."
"$src/deploy/build-radae.sh"
"$src/deploy/install-decoder.sh" "$src"

install -d -m 0755 /etc/systemd/system/freedv-decoder.service.d
install -m 0644 "$src/deploy/freedv-decoder-ai64.conf" \
  /etc/systemd/system/freedv-decoder.service.d/ai64-local.conf

decoder_env=/etc/freedv-decoder/decoder.env
temporary=$(mktemp "${decoder_env}.tmp.XXXXXX")
trap 'rm -f "$temporary"' EXIT
found_secret=0
found_host=0
found_fps=0
found_rade=0
while IFS= read -r line || [[ -n $line ]]; do
  case $line in
    FREEDV_SHARED_SECRET=*) printf 'FREEDV_SHARED_SECRET=%s\n' "$secret"; found_secret=1 ;;
    FREEDV_KIWI_HOST=*) printf 'FREEDV_KIWI_HOST=127.0.0.1\n'; found_host=1 ;;
    FREEDV_DASHBOARD_BIND=*) printf 'FREEDV_DASHBOARD_BIND=127.0.0.1\n' ;;
    FREEDV_DASHBOARD_WATERFALL_FPS=*) printf 'FREEDV_DASHBOARD_WATERFALL_FPS=5\n'; found_fps=1 ;;
    FREEDV_ENABLE_RADE=*) printf 'FREEDV_ENABLE_RADE=0\n'; found_rade=1 ;;
    *) printf '%s\n' "$line" ;;
  esac
done < "$decoder_env" > "$temporary"
(( found_secret )) || printf 'FREEDV_SHARED_SECRET=%s\n' "$secret" >> "$temporary"
(( found_host )) || printf 'FREEDV_KIWI_HOST=127.0.0.1\n' >> "$temporary"
(( found_fps )) || printf 'FREEDV_DASHBOARD_WATERFALL_FPS=5\n' >> "$temporary"
(( found_rade )) || printf 'FREEDV_ENABLE_RADE=0\n' >> "$temporary"
install -m 0640 -o root -g freedv "$temporary" "$decoder_env"
rm -f "$temporary"
trap - EXIT

systemctl daemon-reload
systemctl stop freedv-decoder.service freedv-reporter.service 2>/dev/null || true

ctest --test-dir "$src/build" --output-on-failure
install -d -m 0750 -o root -g freedv "$validation_root"
/usr/local/bin/rade_modulate_wav -v 0 /usr/local/share/freedv-rade/voice.wav \
  "$src/build/radev1-reference.wav"
"$src/build/freedv-rade-reference-test" "$src/build/radev1-reference.wav" \
  | tee "$validation_root/reference.txt"
"$src/tools/test-radev1-load.sh" "$src" 1 20 | tee "$validation_root/load.txt"

stamp=$(date -u +%Y%m%dT%H%M%SZ)
{
  printf 'installed_utc=%s\n' "$stamp"
  printf 'model=%s\n' "$model"
  printf 'architecture=%s\n' "$(uname -m)"
  printf 'online_cpus=%s\n' "$cpus"
  printf 'memory_kib=%s\n' "$mem_kib"
  printf 'rade_pin=%s\n' "$(cat /usr/local/share/freedv-rade/radae_nopy.commit)"
  printf 'fargan_pin=%s\n' "$(cat /usr/local/share/freedv-rade/opus-fargan.commit)"
  sha256sum /usr/local/bin/freedv-decoder
} > "$validation_root/install-manifest.txt"
touch "$validation_root/offline-pass"
chown -R root:freedv "$validation_root"
chmod 0750 "$validation_root"
chmod 0640 "$validation_root"/*

echo "Offline ARM64 build and one-worker RADEV1 headroom gate passed."
echo "RADEV1 remains disabled and Kiwi configuration is unchanged."
echo "Next: $src/tools/activate-ai64-local.sh $src"
