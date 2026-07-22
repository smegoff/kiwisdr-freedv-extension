#!/usr/bin/env bash
set -euo pipefail

root=/root/freedv-releases
target=${1:-baseline-1.902}
[[ $EUID -eq 0 ]] || { echo "run as root" >&2; exit 2; }
[[ -x $root/$target/kiwid ]] || { echo "unknown release: $target" >&2; exit 2; }
ln -sfn "$target" "$root/.active-new"
mv -Tf "$root/.active-new" "$root/active"
systemctl restart kiwid.service
page=
ui_bytes=0
for _ in $(seq 1 90); do
  if systemctl is-active --quiet kiwid.service && \
     wget -q -T 5 -O /dev/null http://127.0.0.1:8073/status; then
    page=$(wget -q -T 5 -O - http://127.0.0.1:8073/) || page=
    ui_bytes=$(wget -q -T 5 -O - http://127.0.0.1:8073/kiwisdr.min.js | wc -c) || ui_bytes=0
    if grep -qi '<html' <<< "$page" && (( ui_bytes >= 100000 )); then
      echo "active=$target"; exit 0
    fi
  fi
  sleep 1
done
echo "rollback target failed health check" >&2
exit 5
