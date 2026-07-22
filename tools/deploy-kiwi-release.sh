#!/usr/bin/env bash
set -euo pipefail

candidate=${1:-/root/build}
release=${2:-freedv-$(date -u +%Y%m%dT%H%M%SZ)}
root=/root/freedv-releases
baseline=$root/baseline-1.902

health_wait() {
  local page
  for _ in $(seq 1 90); do
    if systemctl is-active --quiet kiwid.service && \
       wget -q -T 5 -O /dev/null http://127.0.0.1:8073/status; then
      page=$(wget -q -T 5 -O - http://127.0.0.1:8073/) || page=
      if grep -qi '<html' <<< "$page"; then
        return 0
      fi
    fi
    sleep 1
  done
  return 1
}

[[ $EUID -eq 0 ]] || { echo "run as root" >&2; exit 2; }
[[ -f $candidate/kiwid.bin ]] || {
  echo "candidate must contain the production embedded kiwid.bin" >&2; exit 2;
}
systemctl is-active --quiet kiwid.service || { echo "baseline Kiwi service is not healthy" >&2; exit 3; }

install -d -m 0700 "$root" "$baseline" "$root/$release"
if [[ ! -f $baseline/kiwid ]]; then
  cp -aL /usr/local/bin/kiwid "$baseline/kiwid"
  sha256sum "$baseline/kiwid" > "$baseline/SHA256SUMS"
fi
install -m 0755 "$candidate/kiwid.bin" "$root/$release/kiwid"
sha256sum "$root/$release/kiwid" > "$root/$release/SHA256SUMS"

# An official Kiwi update replaces /usr/local/bin/kiwid directly but can leave
# our old active-release link behind. Only trust that link when its binary is
# byte-for-byte identical to the executable currently serving users.
previous=baseline-1.902
if [[ -L $root/active ]]; then
  claimed=$(readlink "$root/active")
  if [[ -f $root/$claimed/kiwid ]]; then
    claimed_sha=$(sha256sum "$root/$claimed/kiwid" | awk '{print $1}')
    live_sha=$(sha256sum /usr/local/bin/kiwid | awk '{print $1}')
    if [[ $claimed_sha == "$live_sha" ]]; then
      previous=$claimed
    else
      echo "official update replaced the prior custom binary; rollback target is baseline-1.902" >&2
    fi
  else
    echo "official update replaced the prior custom binary; rollback target is baseline-1.902" >&2
  fi
fi
ln -sfn "$previous" "$root/active"
ln -sfn "$root/active/kiwid" /usr/local/bin/.kiwid-freedv
mv -Tf /usr/local/bin/.kiwid-freedv /usr/local/bin/kiwid

ln -sfn "$release" "$root/.active-new"
mv -Tf "$root/.active-new" "$root/active"
healthy=0
if systemctl restart kiwid.service && health_wait; then healthy=1; fi
if [[ $healthy -ne 1 ]]; then
  echo "health check failed; restoring $previous" >&2
  ln -sfn "$previous" "$root/.active-new"
  mv -Tf "$root/.active-new" "$root/active"
  if ! systemctl restart kiwid.service || ! health_wait; then
    echo "automatic rollback also failed" >&2; exit 5;
  fi
  exit 4
fi

printf 'active=%s previous=%s\n' "$release" "$previous"
