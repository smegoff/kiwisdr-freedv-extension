#!/usr/bin/env bash
set -euo pipefail

samples=${1:-41}
interval=${2:-15}
expected_release=${3:-baseline-1.901}
max_users=${4:-8}
start_epoch=$(date +%s)

for ((sample = 1; sample <= samples; sample++)); do
  target_epoch=$((start_epoch + (sample - 1) * interval))
  now_epoch=$(date +%s)
  if (( now_epoch < target_epoch )); then sleep $((target_epoch - now_epoch)); fi
  timestamp=$(date -u +%FT%TZ)
  active=$(readlink /root/freedv-releases/active)
  service=$(systemctl is-active kiwid.service)
  status_page=$(wget -q -T 5 -O - http://127.0.0.1:8073/status)
  root_page=$(wget -q -T 5 -O - http://127.0.0.1:8073/)
  status=$(sed -n 's/^status=//p' <<< "$status_page")
  firmware=$(sed -n 's/^sw_version=//p' <<< "$status_page")
  users=$(sed -n 's/^users=//p' <<< "$status_page")
  root_html=0
  grep -qi '<html' <<< "$root_page" && root_html=1
  wrappers=$(pgrep -fc '^(bash|/bin/bash) /root/.*/(deploy|rollback)-kiwi-release[.]sh' || true)
  critical=$(journalctl -u kiwid.service --since "@$start_epoch" --no-pager |
    grep -Eci 'watchdog|segfault|audio.*sequence|fatal|assert|authentication.*error' || true)

  printf 'KIWI sample=%d time=%s active=%s service=%s status=%s firmware=%s users=%s root=%d wrappers=%d critical=%d\n' \
    "$sample" "$timestamp" "$active" "$service" "$status" "$firmware" "$users" \
    "$root_html" "$wrappers" "$critical"

  [[ $users =~ ^[0-9]+$ ]]
  [[ $active == "$expected_release" && $service == active && $status == active &&
     $firmware == KiwiSDR_v1.901 && $users -le $max_users && $root_html == 1 &&
     $wrappers == 0 && $critical == 0 ]]
done
