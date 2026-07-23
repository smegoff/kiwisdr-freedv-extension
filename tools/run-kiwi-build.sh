#!/usr/bin/env bash
set -euo pipefail
kiwi=${1:-/root/KiwiSDR}
cd "$kiwi"
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
    # web/Makefile expands FILES_EMBED_JS_F2 while Make parses the file. If
    # file_optim does not already exist that expansion silently becomes empty
    # and the rule produces a zero-byte browser application. Build the helper
    # in a separate Make invocation so it exists before web/Makefile is parsed.
    make ../build/tools/file_optim > /root/freedv-build.log 2>&1
    rc=$?
    if [[ $rc -ne 0 ]]; then echo "$rc" > /root/freedv-build.exit; exit; fi
    make web/kiwisdr.min.js >> /root/freedv-build.log 2>&1
    rc=$?
    if [[ $rc -ne 0 ]]; then echo "$rc" > /root/freedv-build.exit; exit; fi
    js_bytes=$(stat -c %s web/kiwisdr.min.js 2>/dev/null || echo 0)
    gz_bytes=$(stat -c %s web/kiwisdr.min.js.gz 2>/dev/null || echo 0)
    if (( js_bytes < 100000 || gz_bytes < 10000 )) ||
       ! gzip -t web/kiwisdr.min.js.gz ||
       ! gzip -cd web/kiwisdr.min.js.gz | cmp -s web/kiwisdr.min.js -; then
      echo "browser application bundle validation failed: js=$js_bytes gzip=$gz_bytes" \
        >> /root/freedv-build.log
      echo 3 > /root/freedv-build.exit
      exit
    fi
  fi
  make "$target" >> /root/freedv-build.log 2>&1
  echo $? > /root/freedv-build.exit
) </dev/null >/dev/null 2>&1 &
echo $!
