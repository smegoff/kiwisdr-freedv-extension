#!/usr/bin/env bash
set -euo pipefail

vmid=${1:-}
samples=${2:-41}
interval=${3:-15}
evidence_log=${4:-}
expected_sessions=${5:-0}
expected_camper=${6:-false}
expected_reporter=${7:-disabled}
expected_release=${8:-}
[[ $vmid =~ ^[1-9][0-9]*$ ]] || {
  echo "usage: $0 <proxmox-vmid> [samples] [interval-seconds] [evidence-log] [sessions] [camper] [reporter] [release]" >&2
  exit 2
}
[[ $expected_sessions =~ ^[0-9]+$ ]] || { echo "sessions must be numeric" >&2; exit 2; }
[[ $expected_camper == true || $expected_camper == false ]] || {
  echo "camper must be true or false" >&2; exit 2;
}
if [[ -n $evidence_log ]]; then
  [[ ! -e $evidence_log ]] || { echo "evidence log already exists: $evidence_log" >&2; exit 2; }
  exec > >(tee "$evidence_log") 2>&1
fi
start_epoch=$(date +%s)

for ((sample = 1; sample <= samples; sample++)); do
  target_epoch=$((start_epoch + (sample - 1) * interval))
  now_epoch=$(date +%s)
  if (( now_epoch < target_epoch )); then sleep $((target_epoch - now_epoch)); fi
  timestamp=$(date -u +%FT%TZ)
  guest_state=$(pct status "$vmid" | awk '{print $2}')
  services=$(pct exec "$vmid" -- systemctl is-active freedv-decoder.service freedv-reporter.service |
    tr '\n' ',' | sed 's/,$//')
  health=$(pct exec "$vmid" -- wget -q -T 5 -O - http://127.0.0.1:8074/healthz)
  private_port=$(pct exec "$vmid" -- ss -lnt | grep -c '127[.]0[.]0[.]1:8074' || true)
  critical=$(pct exec "$vmid" -- journalctl -u freedv-decoder.service --since "@$start_epoch" --no-pager |
    grep -Eci 'conflicting job|authentication rejected|watchdog|segfault|fatal|assert' || true)

  printf 'DECODER sample=%d time=%s state=%s services=%s private_port=%d critical=%d health=%s\n' \
    "$sample" "$timestamp" "$guest_state" "$services" "$private_port" "$critical" "$health"

  [[ $guest_state == running && $services == active,active && $private_port == 1 &&
     $critical == 0 && $health == *'"status":"ok"'* &&
     $health == *"\"sessions\":$expected_sessions"* &&
     $health == *'"kiwi_connected":true'* &&
     $health == *"\"camper_connected\":$expected_camper"* &&
     $health == *"\"reporter\":\"$expected_reporter\""* ]]
  if [[ -n $expected_release ]]; then
    [[ $health == *"\"release\":\"$expected_release\""* ]]
  fi
done
