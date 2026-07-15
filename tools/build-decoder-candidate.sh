#!/usr/bin/env bash
set -euo pipefail

release=${1:-0.1.18}
archive=${2:-/root/freedv-v0-1-18-decoder.tgz}
candidate="/opt/kiwi-freedv-v${release//./-}"
rade_pin=6e6fff3fc0546363693b60b52f463e08c71117e6
opus_pin=940d4e5af64351ca8ba8390df3f555484c567fbb

[[ $EUID -eq 0 ]] || { echo "run as root" >&2; exit 2; }
[[ -f $archive ]] || { echo "candidate archive not found: $archive" >&2; exit 2; }
[[ ! -e $candidate ]] || { echo "candidate directory already exists: $candidate" >&2; exit 2; }
[[ $(cat /usr/local/share/freedv-rade/radae_nopy.commit 2>/dev/null) == "$rade_pin" &&
   $(cat /usr/local/share/freedv-rade/opus-fargan.commit 2>/dev/null) == "$opus_pin" ]] || {
  echo "official RADEv1 dependency pins are missing or unexpected" >&2
  exit 3
}

install -d -m 0755 "$candidate"
tar -xzf "$archive" -C "$candidate"
cmake -S "$candidate/decoder" -B "$candidate/build" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$candidate/build" --parallel 2
ctest --test-dir "$candidate/build" --output-on-failure
[[ -x "$candidate/build/freedv-rade-reference-test" ]] || {
  echo "RADEv1 adapter was not compiled; run deploy/build-radae.sh first" >&2
  exit 3
}
/usr/local/bin/rade_modulate_wav -v 0 \
  /usr/local/share/freedv-rade/voice.wav "$candidate/build/radev1-reference.wav"
"$candidate/build/freedv-rade-reference-test" "$candidate/build/radev1-reference.wav" \
  | tee "$candidate/build/radev1-reference-result.txt"
sha256sum "$candidate/build/freedv-decoder" "$candidate/build/freedv-reference-test" \
  "$candidate/build/freedv-rade-reference-test" "$candidate/build/radev1-reference.wav"
printf '%s\n' "$candidate"
