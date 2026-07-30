#!/usr/bin/env bash
set -euo pipefail

input=${1:-/usr/local/share/freedv-rade/input_sample.wav}
output=${2:-$(cd "$(dirname "$0")/.." && pwd)/kiwi-overlay/samples/FreeDV.rade.au}

command -v rade_tx_wav >/dev/null || {
  echo "rade_tx_wav is required; run deploy/build-radae.sh first" >&2
  exit 2
}
command -v sox >/dev/null || {
  echo "sox is required to package the Kiwi 12 kHz AU sample" >&2
  exit 2
}
[[ -f $input ]] || { echo "input speech WAV not found: $input" >&2; exit 2; }

temporary=$(mktemp --suffix=.wav)
trap 'rm -f "$temporary"' EXIT

rade_tx_wav -v 0 "$input" "$temporary"
install -d -m 0755 "$(dirname "$output")"
sox "$temporary" -t au -e signed-integer -b 16 -c 1 -r 12000 "$output"

file "$output"
sha256sum "$output"
