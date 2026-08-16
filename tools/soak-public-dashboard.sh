#!/usr/bin/env bash
set -euo pipefail

samples=${1:-41}
interval=${2:-15}
expected_release=${3:-0.1.37}
start_epoch=$(date +%s)

for ((sample = 1; sample <= samples; sample++)); do
  target_epoch=$((start_epoch + (sample - 1) * interval))
  now_epoch=$(date +%s)
  if (( now_epoch < target_epoch )); then sleep $((target_epoch - now_epoch)); fi
  health=$(wget -qO- http://127.0.0.1:8074/healthz)
  management=$(wget -qO- http://127.0.0.1:8076/api/v1/status)
  public=$(wget -qO- http://127.0.0.1:8077/api/v1/status)
  history=$(wget -qO- http://127.0.0.1:8077/api/v1/history)
  capture_code=$({ wget -q -S -O /dev/null http://127.0.0.1:8077/api/v1/capture.wav 2>&1 || true; } |
    awk '/HTTP\// {code=$2} END {print code}')
  listeners=$(ss -lnt)
  critical=$(journalctl -u freedv-decoder.service --since "@$start_epoch" --no-pager |
    grep -Eci 'watchdog|segfault|audio.*sequence|fatal|assert|authentication.*error' || true)
  rss_kb=$(ps -o rss= -C freedv-decoder | awk '{sum += $1} END {print sum + 0}')
  cpu=$(ps -o pcpu= -C freedv-decoder | awk '{sum += $1} END {print sum + 0}')

  python3 - "$sample" "$health" "$management" "$public" "$history" \
    "$capture_code" "$listeners" "$critical" "$rss_kb" "$cpu" "$expected_release" <<'PY'
import json
import sys

sample, health_raw, management_raw, public_raw, history_raw, capture, listeners, critical, rss, cpu, release = sys.argv[1:]
health = json.loads(health_raw)
management = json.loads(management_raw)
public = json.loads(public_raw)
history = json.loads(history_raw)
assert health["status"] == "ok" and health["release"] == release
assert management["release"] == release and management["kiwi_connected"] is True
assert management["dashboard"]["public_enabled"] is True
assert int(management["dropped_frames_total"]) == 0
assert set(public) == {"version", "release", "kiwi_connected", "session"}
assert public["version"] == 1 and public["release"] == release
assert set(public["session"]) <= {"active", "mode", "frequency_hz", "input_rate", "test", "sync", "snr_db", "frequency_offset_hz"}
for point in history:
    assert set(point) <= {"timestamp_ms", "sync", "snr_db", "frequency_offset_hz"}
assert len(history) <= 600
assert capture == "404"
assert "127.0.0.1:8077" in listeners
assert "0.0.0.0:8077" not in listeners and "[::]:8077" not in listeners
assert int(critical) == 0
print(f"PUBLIC sample={sample} release={release} public_clients={management['dashboard']['public_clients']} history={len(history)} rss_kb={rss} cpu={cpu} critical={critical}")
PY
done
