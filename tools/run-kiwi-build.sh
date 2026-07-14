#!/usr/bin/env bash
set -euo pipefail
cd /root/KiwiSDR
rm -f /root/freedv-build.log /root/freedv-build.exit
target=${KIWI_BUILD_TARGET:-../build/kiwid.bin}
if [[ $target == ../build/kiwid.bin ]]; then
  gzip --fast --keep --force web/extensions/FreeDV/FreeDV.min.js
  gzip --fast --keep --force web/extensions/FreeDV/FreeDV.min.css
  rm -f web/kiwisdr.min.js web/kiwisdr.min.js.gz \
    ../build/gen/edata_embed.cpp ../build/obj/edata_embed.o \
    ../build/kiwid.bin
fi
(
  set +e
  if [[ $target == ../build/kiwid.bin ]]; then
    make web/kiwisdr.min.js > /root/freedv-build.log 2>&1
    rc=$?
    if [[ $rc -ne 0 ]]; then echo "$rc" > /root/freedv-build.exit; exit; fi
  fi
  make "$target" >> /root/freedv-build.log 2>&1
  echo $? > /root/freedv-build.exit
) </dev/null >/dev/null 2>&1 &
echo $!
