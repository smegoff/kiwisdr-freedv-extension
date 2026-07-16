#!/usr/bin/env bash
set -euo pipefail

samples=${1:-41}
interval=${2:-15}
expected_release=${3:-0.1.21}
expected_sessions=${4:-0}
start_epoch=$(date +%s)

for ((sample = 1; sample <= samples; sample++)); do
  target_epoch=$((start_epoch + (sample - 1) * interval))
  now_epoch=$(date +%s)
  if (( now_epoch < target_epoch )); then sleep $((target_epoch - now_epoch)); fi
  health=$(wget -qO- http://127.0.0.1:8074/healthz)
  status=$(wget -qO- http://127.0.0.1:8076/api/v1/status)
  history=$(wget -qO- http://127.0.0.1:8076/api/v1/history)
  history_count=$(printf '%s' "$history" |
    python3 -c 'import json, sys; print(len(json.load(sys.stdin)))')
  unset history
  service=$(systemctl is-active freedv-decoder.service)
  rss_kb=$(ps -o rss= -C freedv-decoder | awk '{sum += $1} END {print sum + 0}')
  cpu=$(ps -o pcpu= -C freedv-decoder | awk '{sum += $1} END {print sum + 0}')
  critical=$(journalctl -u freedv-decoder.service --since "@$start_epoch" --no-pager |
    grep -Eci 'watchdog|segfault|audio.*sequence|fatal|assert|authentication.*error' || true)

  python3 - "$sample" "$health" "$status" "$history_count" "$service" \
    "$rss_kb" "$cpu" "$critical" "$expected_release" "$expected_sessions" <<'PY'
import json
import sys

sample, health_raw, status_raw, history_count, service, rss, cpu, critical, release, sessions = sys.argv[1:]
health = json.loads(health_raw)
status = json.loads(status_raw)
sessions = int(sessions)
assert service == "active"
assert health["status"] == "ok" and health["release"] == release
assert status["release"] == release and status["kiwi_connected"] is True
assert int(status["sessions"]) == sessions
assert int(status["dropped_frames_total"]) == 0
assert int(critical) == 0
assert int(history_count) <= 600
dashboard = status["dashboard"]
if sessions:
    assert int(dashboard["clients"]) >= 1
    assert int(dashboard["waterfall_frames"]) > 0
print(
    f"DASHBOARD sample={sample} release={release} sessions={sessions} "
    f"clients={dashboard['clients']} wf={dashboard['waterfall_frames']} "
    f"viz_drops={dashboard['spectrum_drops']} history={history_count} "
    f"rss_kb={rss} cpu={cpu} critical={critical}"
)
PY
done
