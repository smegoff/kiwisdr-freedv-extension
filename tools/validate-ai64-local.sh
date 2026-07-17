#!/usr/bin/env bash
set -euo pipefail

duration=${1:-1800}
interval=${KIWI_FREEDV_SOAK_INTERVAL:-5}
validation_root=${KIWI_FREEDV_VALIDATION_ROOT:-/var/lib/freedv-decoder/ai64-validation}

die() { echo "AI-64 validation: $*" >&2; exit 2; }
[[ $EUID -eq 0 ]] || die "run as root"
[[ $duration =~ ^[0-9]+$ && $duration -ge 60 && $duration -le 7200 ]] ||
  die "duration must be between 60 and 7200 seconds"
model=$(tr -d '\0' </proc/device-tree/model 2>/dev/null || true)
[[ $model == *"BeagleBone AI-64"* && $(uname -m) == aarch64 ]] ||
  die "this validation is only for a 64-bit BeagleBone AI-64"
[[ -f $validation_root/offline-pass ]] || die "offline validation has not passed"
[[ $(systemctl is-active kiwid.service) == active ]] || die "kiwid.service is not active"
[[ $(systemctl is-active freedv-decoder.service) == active ]] || die "decoder service is not active"

metrics() { wget -qO- http://127.0.0.1:8074/metrics; }
metric() { awk -v name="$1" '$1 == name {print $2; exit}'; }
initial_metrics=$(metrics)
[[ $(printf '%s\n' "$initial_metrics" | metric freedv_sessions) == 1 ]] ||
  die "start exactly one RADEV1 session before running the soak"
python3 - <<'PY' || die "the active decoder session is not RADEV1"
import json
import urllib.request

with urllib.request.urlopen("http://127.0.0.1:8076/api/v1/status", timeout=3) as response:
    status = json.load(response)
session = status.get("session", {})
raise SystemExit(0 if session.get("active") and session.get("mode") == "RADEV1" else 1)
PY

install -d -m 0750 -o root -g freedv "$validation_root"
stamp=$(date -u +%Y%m%dT%H%M%SZ)
log="$validation_root/live-$stamp.log"
exec > >(tee -a "$log") 2>&1

read_cpu() {
  awk '/^cpu / { idle=$5+$6; total=0; for (i=2; i<=NF; i++) total+=$i; print total, idle; exit }' /proc/stat
}
max_temperature() {
  local maximum=0 value
  for path in /sys/class/thermal/thermal_zone*/temp; do
    [[ -r $path ]] || continue
    read -r value < "$path"
    [[ $value =~ ^[0-9]+$ ]] || continue
    (( value > maximum )) && maximum=$value
  done
  printf '%s\n' "$maximum"
}

initial_drops=$(printf '%s\n' "$initial_metrics" | metric freedv_dropped_frames_total)
initial_reconnects=$(printf '%s\n' "$initial_metrics" | metric freedv_reconnects_total)
initial_malformed=$(printf '%s\n' "$initial_metrics" | metric freedv_malformed_jobs_total)
initial_restarts=$(systemctl show freedv-decoder.service -p NRestarts --value)
start_epoch=$(date +%s)
start_journal=$(date -u +'%Y-%m-%d %H:%M:%S UTC')
read -r previous_total previous_idle < <(read_cpu)
cpu_sum=0
cpu_samples=0
cpu_peak=0
temperature_peak=0
sample=0

echo "AI-64 live RADEV1 soak: duration=${duration}s interval=${interval}s"
while (( $(date +%s) - start_epoch < duration )); do
  sleep "$interval"
  sample=$((sample + 1))
  read -r total idle < <(read_cpu)
  total_delta=$((total - previous_total))
  idle_delta=$((idle - previous_idle))
  previous_total=$total
  previous_idle=$idle
  (( total_delta > 0 )) || die "unable to sample CPU usage"
  cpu=$((100 * (total_delta - idle_delta) / total_delta))
  cpu_sum=$((cpu_sum + cpu))
  cpu_samples=$((cpu_samples + 1))
  (( cpu > cpu_peak )) && cpu_peak=$cpu

  temperature=$(max_temperature)
  (( temperature > temperature_peak )) && temperature_peak=$temperature
  current_metrics=$(metrics)
  sessions=$(printf '%s\n' "$current_metrics" | metric freedv_sessions)
  drops=$(printf '%s\n' "$current_metrics" | metric freedv_dropped_frames_total)
  reconnects=$(printf '%s\n' "$current_metrics" | metric freedv_reconnects_total)
  malformed=$(printf '%s\n' "$current_metrics" | metric freedv_malformed_jobs_total)
  health=$(wget -qO- http://127.0.0.1:8074/healthz)
  status=$(wget -qO- http://127.0.0.1:8073/status)
  root=$(wget -qO- http://127.0.0.1:8073/)
  restarts=$(systemctl show freedv-decoder.service -p NRestarts --value)

  [[ $sessions == 1 ]] || die "RADEV1 session stopped during validation"
  [[ $drops == "$initial_drops" && $reconnects == "$initial_reconnects" &&
     $malformed == "$initial_malformed" ]] || die "decoder counters increased"
  [[ $restarts == "$initial_restarts" ]] || die "decoder service restarted"
  [[ $health == *'"status":"ok"'* && $health == *'"kiwi_connected":true'* ]] ||
    die "decoder health failed"
  [[ $status == *"status=active"* && $root == *"KiwiSDR"* ]] || die "Kiwi health failed"
  (( temperature > 0 )) || die "no readable thermal sensor was found"
  (( temperature <= 85000 )) || die "temperature exceeded 85 C"

  printf 'sample=%d cpu=%d%% temperature=%.1fC sessions=%s drops=%s reconnects=%s\n' \
    "$sample" "$cpu" "$(awk -v t="$temperature" 'BEGIN {print t/1000}')" \
    "$sessions" "$drops" "$reconnects"
done

cpu_average=$((cpu_sum / cpu_samples))
(( cpu_average <= 80 )) || die "average CPU exceeded 80 percent"
(( cpu_peak <= 95 )) || die "peak CPU exceeded 95 percent"
if journalctl -u kiwid.service -u freedv-decoder.service --since "$start_journal" --no-pager |
   grep -Eiq 'watchdog|main loop stalled|segfault|audio.*sequence|fatal|failed with result'; then
  die "critical journal event occurred during validation"
fi

printf 'PASS samples=%d average_cpu=%d%% peak_cpu=%d%% peak_temperature=%.1fC\n' \
  "$sample" "$cpu_average" "$cpu_peak" \
  "$(awk -v t="$temperature_peak" 'BEGIN {print t/1000}')"
touch "$validation_root/live-pass"
chown root:freedv "$log" "$validation_root/live-pass"
chmod 0640 "$log" "$validation_root/live-pass"
