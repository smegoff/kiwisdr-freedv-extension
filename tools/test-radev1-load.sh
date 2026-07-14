#!/usr/bin/env bash
set -euo pipefail

candidate=${1:-/opt/kiwi-freedv-v0-1-15}
workers=${2:-8}
repetitions=${3:-20}
test_bin=$candidate/build/freedv-rade-reference-test
reference=$candidate/build/radev1-reference.wav

[[ -x $test_bin && -f $reference ]] || {
  echo "RADEv1 reference candidate is incomplete" >&2
  exit 2
}
[[ $workers =~ ^[1-9][0-9]*$ && $repetitions =~ ^[1-9][0-9]*$ ]] || {
  echo "workers and repetitions must be positive integers" >&2
  exit 2
}

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
: > "$scratch/running"
(
  peak=0
  while [[ -e $scratch/running ]]; do
    if [[ -r /sys/fs/cgroup/memory.current ]]; then
      read -r current < /sys/fs/cgroup/memory.current
      (( current > peak )) && peak=$current
    fi
    sleep 0.1
  done
  printf '%s\n' "$peak" > "$scratch/peak-memory"
) &
sampler=$!

start_ns=$(date +%s%N)
pids=()
for worker in $(seq 1 "$workers"); do
  (
    for _ in $(seq 1 "$repetitions"); do
      "$test_bin" "$reference" >/dev/null 2>>"$scratch/worker-$worker.log"
    done
  ) &
  pids+=("$!")
done

failed=0
for pid in "${pids[@]}"; do
  wait "$pid" || failed=1
done
end_ns=$(date +%s%N)
rm -f "$scratch/running"
wait "$sampler"

if (( failed )); then
  tail -80 "$scratch"/worker-*.log >&2
  echo "RADEv1 concurrent load test failed" >&2
  exit 1
fi

elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
reference_bytes=$(stat -c %s "$reference")
signal_ms=$(( (reference_bytes - 44) * 1000 / 2 / 8000 ))
per_worker_rtf=$(awk -v elapsed="$elapsed_ms" -v signal="$signal_ms" -v reps="$repetitions" \
  'BEGIN { printf "%.4f", elapsed / (signal * reps) }')
peak_bytes=$(cat "$scratch/peak-memory")
printf 'RADEV1 load: workers=%d repetitions=%d elapsed_ms=%d per_worker_rtf=%s peak_container_bytes=%s\n' \
  "$workers" "$repetitions" "$elapsed_ms" "$per_worker_rtf" "$peak_bytes"
awk -v rtf="$per_worker_rtf" 'BEGIN { exit !(rtf <= 0.50) }' || {
  echo "RADEv1 load test exceeded the 0.50 real-time headroom gate" >&2
  exit 1
}
