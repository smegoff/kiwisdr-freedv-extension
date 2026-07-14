#!/usr/bin/env bash
set -euo pipefail

# Measure decoder throughput using a generated, deterministic FreeDV stream.
# Run from a Codec2 build directory or set CODEC2_BUILD to one.

build_dir="${CODEC2_BUILD:-$PWD}"
seconds="${SECONDS_PER_MODE:-120}"
modes="${MODES:-1600 700C 700D 700E 2400A 2400B 800XA}"
speech="${SPEECH_RAW:-}"

tx="$build_dir/src/freedv_tx"
rx="$build_dir/src/freedv_rx"

if [[ ! -x "$tx" || ! -x "$rx" ]]; then
  echo "freedv_tx/freedv_rx not found below: $build_dir" >&2
  echo "Build codec2, then set CODEC2_BUILD to its build directory." >&2
  exit 2
fi

if [[ -z "$speech" ]]; then
  for candidate in "$build_dir/../raw/ve9qrp.raw" "$build_dir/../raw/hts1a.raw"; do
    if [[ -f "$candidate" ]]; then speech="$candidate"; break; fi
  done
fi

if [[ ! -f "$speech" ]]; then
  echo "Set SPEECH_RAW to a signed 16-bit, 8 kHz mono speech file." >&2
  exit 2
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

printf 'mode,audio_seconds,wall_seconds,realtime_factor\n'
for mode in $modes; do
  input="$tmp/$mode.raw"
  # Repeat source speech, then retain the requested duration at 8 kHz x 2 bytes.
  while [[ ! -f "$input" || $(wc -c < "$input") -lt $((seconds * 16000)) ]]; do
    cat "$speech" >> "$input"
  done
  truncate -s $((seconds * 16000)) "$input"

  modem="$tmp/$mode.modem"
  "$tx" "$mode" "$input" "$modem"

  start_ns="$(date +%s%N)"
  "$rx" "$mode" "$modem" /dev/null
  end_ns="$(date +%s%N)"

  wall="$(awk -v a="$start_ns" -v b="$end_ns" 'BEGIN { printf "%.6f", (b-a)/1e9 }')"
  rtf="$(awk -v w="$wall" -v s="$seconds" 'BEGIN { printf "%.4f", w/s }')"
  printf '%s,%s,%s,%s\n' "$mode" "$seconds" "$wall" "$rtf"
done

echo "Pass criterion: every worst-case-load realtime_factor <= 0.50" >&2
