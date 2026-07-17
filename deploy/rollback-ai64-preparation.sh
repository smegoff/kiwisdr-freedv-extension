#!/usr/bin/env bash
set -euo pipefail

marker=${1:-/var/lib/freedv-decoder/one-shot-local-ready}
die() { echo "AI-64 preparation cleanup: $*" >&2; exit 2; }

[[ $EUID -eq 0 ]] || die "run as root"
[[ -f $marker ]] || die "preparation marker is missing"
grep -qx 'fresh_install=1' "$marker" || die "marker does not describe a fresh installation"

systemctl stop freedv-decoder.service freedv-reporter.service 2>/dev/null || true
systemctl disable freedv-decoder.service freedv-reporter.service 2>/dev/null || true
rm -f /usr/local/bin/freedv-decoder \
  /etc/systemd/system/freedv-decoder.service \
  /etc/systemd/system/freedv-reporter.service
rm -rf /etc/systemd/system/freedv-decoder.service.d \
  /etc/freedv-decoder /opt/freedv-reporter /usr/local/share/freedv-dashboard \
  /var/lib/freedv-decoder/ai64-validation
rm -f "$marker"
systemctl daemon-reload
echo "fresh local decoder application files removed; build dependencies were retained"
