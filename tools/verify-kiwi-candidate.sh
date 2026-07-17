#!/usr/bin/env bash
set -euo pipefail

release=${1:-}
kiwi=${2:-/root/KiwiSDR}
candidate=${3:-/root/build/kiwid.bin}
build=$(dirname "$candidate")
expected_commit=417e2c8add196e879b8cc4eb4a488b35b4bf0df7
version=$(sed -n 's/^#define FREEDV_RELEASE "\([0-9.]*\)"/\1/p' \
  "$kiwi/extensions/FreeDV/freedv.cpp")
[[ $version =~ ^[0-9]+([.][0-9]+)+$ ]] || {
  echo "unable to determine FreeDV extension version" >&2; exit 3;
}

[[ -n $release ]] || {
  echo "usage: $0 <release> [kiwi-source] [candidate-binary]" >&2
  exit 2
}
[[ -f $candidate ]] || { echo "candidate not found: $candidate" >&2; exit 2; }
[[ $(cat /root/freedv-build.exit) == 0 ]] || {
  echo "production build did not finish successfully" >&2
  exit 3
}
[[ $(git -C "$kiwi" rev-parse HEAD) == "$expected_commit" ]] || {
  echo "unexpected Kiwi source commit" >&2
  exit 3
}

grep -Fq "excl_devl: [ 'devl', 'digi_modes', 's4285', 'prefs' ]," \
  "$kiwi/web/extensions/ext.js"
grep -Fq "excl_devl:['devl','digi_modes','s4285','prefs']" \
  "$kiwi/web/extensions/ext.min.js"
! grep -Fq "excl_devl:['devl','FreeDV'" "$kiwi/web/extensions/ext.min.js"
grep -Fq 'FreeDV_main();' "$build/gen/ext_init.cpp"
grep -aFq FreeDV_main "$candidate"
zgrep -Fq "FreeDV v$version" "$kiwi/web/extensions/FreeDV/FreeDV.min.js.gz"
zgrep -Fq 'freq_kHz < 10000' "$kiwi/web/extensions/FreeDV/FreeDV.min.js.gz"
[[ ! -e /root/freedv-releases/$release ]] || {
  echo "release already exists: $release" >&2
  exit 3
}

sha256sum "$candidate"
file "$candidate"
echo "candidate_verified=$release"
