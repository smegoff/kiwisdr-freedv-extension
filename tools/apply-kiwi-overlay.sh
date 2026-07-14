#!/usr/bin/env bash
set -euo pipefail

src=${1:-$(cd "$(dirname "$0")/.." && pwd)}
kiwi=${2:-/root/KiwiSDR}
upstream_commit=417e2c8add196e879b8cc4eb4a488b35b4bf0df7
[[ -f "$kiwi/Makefile" ]] || { echo "Kiwi source tree not found: $kiwi" >&2; exit 2; }
[[ $(git -C "$kiwi" rev-parse HEAD) == "$upstream_commit" ]] || {
  echo "Kiwi source is not pinned to $upstream_commit" >&2; exit 2;
}

release=$(date -u +%Y%m%dT%H%M%SZ)
backup="/root/freedv-rollbacks/$release"
install -d -m 0700 "$backup"
for path in extensions/FreeDV web/extensions/FreeDV rx/rx_monitor.cpp \
  web/extensions/ext.js web/extensions/ext.min.js; do
  if [[ -e "$kiwi/$path" ]]; then
    mkdir -p "$backup/$(dirname "$path")"
    cp -a "$kiwi/$path" "$backup/$path"
  fi
done

mkdir -p "$kiwi/extensions/FreeDV" "$kiwi/web/extensions/FreeDV"
cp -a "$src/kiwi-overlay/extensions/FreeDV/." "$kiwi/extensions/FreeDV/"
cp -a "$src/kiwi-overlay/web/extensions/FreeDV/." "$kiwi/web/extensions/FreeDV/"
chmod 0644 "$kiwi/extensions/FreeDV/freedv.cpp" "$kiwi/extensions/FreeDV/freedv.h" \
  "$kiwi/web/extensions/FreeDV/FreeDV.js" "$kiwi/web/extensions/FreeDV/FreeDV.css" \
  "$kiwi/web/extensions/FreeDV/FreeDV.min.js" "$kiwi/web/extensions/FreeDV/FreeDV.min.css" \
  "$kiwi/web/extensions/FreeDV/FreeDV.min.js.gz" "$kiwi/web/extensions/FreeDV/FreeDV.min.css.gz"

source_before="excl_devl: [ 'devl', 'FreeDV', 'digi_modes', 's4285', 'prefs' ],"
source_after="excl_devl: [ 'devl', 'digi_modes', 's4285', 'prefs' ],"
if grep -Fq "$source_before" "$kiwi/web/extensions/ext.js"; then
  patch -d "$kiwi" -p1 --batch --forward < \
    "$src/kiwi-overlay/patches/0001-publish-freedv.patch"
elif ! grep -Fq "$source_after" "$kiwi/web/extensions/ext.js"; then
  echo "unexpected ext.js developer exclusion list" >&2; exit 3
fi

python3 - "$kiwi/web/extensions/ext.min.js" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
before = "excl_devl:['devl','FreeDV','digi_modes','s4285','prefs']"
after = "excl_devl:['devl','digi_modes','s4285','prefs']"
text = path.read_text(encoding="utf-8")
if text.count(before) == 1 and after not in text:
    path.write_text(text.replace(before, after), encoding="utf-8")
elif text.count(after) != 1 or before in text:
    raise SystemExit("unexpected ext.min.js developer exclusion list")
PY

grep -Fq "$source_after" "$kiwi/web/extensions/ext.js"
! grep -Fq "$source_before" "$kiwi/web/extensions/ext.js"
grep -Fq "excl_devl:['devl','digi_modes','s4285','prefs']" \
  "$kiwi/web/extensions/ext.min.js"
! grep -Fq "excl_devl:['devl','FreeDV','digi_modes','s4285','prefs']" \
  "$kiwi/web/extensions/ext.min.js"

monitor_marker='freedv_monitor_poll(conn_mon, &cmd[16]);'
if ! grep -Fq "$monitor_marker" "$kiwi/rx/rx_monitor.cpp"; then
  patch -d "$kiwi" -p1 --batch --forward < \
    "$src/kiwi-overlay/patches/0002-freedv-monitor-control.patch"
fi
[[ $(grep -Fc "$monitor_marker" "$kiwi/rx/rx_monitor.cpp") == 1 ]] || {
  echo "unexpected FreeDV monitor command integration" >&2; exit 3
}
printf '%s\n' "$release" > "$backup/release-id"
echo "$backup"
