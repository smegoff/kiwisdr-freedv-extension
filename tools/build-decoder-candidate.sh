#!/usr/bin/env bash
set -euo pipefail

release=${1:-0.1.13}
archive=${2:-/root/freedv-v0-1-13-decoder.tgz}
candidate="/opt/kiwi-freedv-v${release//./-}"

[[ $EUID -eq 0 ]] || { echo "run as root" >&2; exit 2; }
[[ -f $archive ]] || { echo "candidate archive not found: $archive" >&2; exit 2; }
[[ ! -e $candidate ]] || { echo "candidate directory already exists: $candidate" >&2; exit 2; }

install -d -m 0755 "$candidate"
tar -xzf "$archive" -C "$candidate"
cmake -S "$candidate/decoder" -B "$candidate/build" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$candidate/build" --parallel 2
ctest --test-dir "$candidate/build" --output-on-failure
sha256sum "$candidate/build/freedv-decoder" "$candidate/build/freedv-reference-test"
printf '%s\n' "$candidate"
