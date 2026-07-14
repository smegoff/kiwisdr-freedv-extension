#!/usr/bin/env bash
set -euo pipefail

samples=${1:-41}
interval=${2:-15}
evidence_log=${3:-}
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
  ct_state=$(pct status 112 | awk '{print $2}')
  services=$(pct exec 112 -- systemctl is-active freedv-decoder.service freedv-reporter.service |
    tr '\n' ',' | sed 's/,$//')
  health=$(pct exec 112 -- wget -q -T 5 -O - http://127.0.0.1:8074/healthz)
  private_port=$(pct exec 112 -- ss -lnt | grep -c '127[.]0[.]0[.]1:8074' || true)
  critical=$(pct exec 112 -- journalctl -u freedv-decoder.service --since "@$start_epoch" --no-pager |
    grep -Eci 'conflicting job|authentication rejected|watchdog|segfault|fatal|assert' || true)

  printf 'CT sample=%d time=%s state=%s services=%s private_port=%d critical=%d health=%s\n' \
    "$sample" "$timestamp" "$ct_state" "$services" "$private_port" "$critical" "$health"

  [[ $ct_state == running && $services == active,active && $private_port == 1 &&
     $critical == 0 && $health == *'"status":"ok"'* && $health == *'"sessions":0'* &&
     $health == *'"kiwi_connected":true'* && $health == *'"camper_connected":false'* &&
     $health == *'"reporter":"disabled"'* ]]
done
