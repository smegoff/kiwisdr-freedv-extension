#!/usr/bin/env bash
set -euo pipefail

marker=${1:-/var/lib/freedv-decoder/one-shot-ready}

die() { echo "remote decoder rollback: $*" >&2; exit 2; }
[[ $EUID -eq 0 ]] || die "run as root"
[[ -f $marker ]] || die "preparation marker is missing"

backup=$(awk -F= '$1 == "backup" {print substr($0, index($0, "=") + 1)}' "$marker")
decoder_was_active=$(awk -F= '$1 == "decoder_was_active" {print $2}' "$marker")
reporter_was_active=$(awk -F= '$1 == "reporter_was_active" {print $2}' "$marker")
decoder_existed=$(awk -F= '$1 == "decoder_existed" {print $2}' "$marker")
[[ $backup =~ ^/root/freedv-one-shot-backups/[0-9]{8}T[0-9]{6}Z$ ]] ||
  die "unsafe backup path in marker"
[[ $decoder_was_active =~ ^[01]$ && $reporter_was_active =~ ^[01]$ && $decoder_existed =~ ^[01]$ ]] ||
  die "invalid service state in marker"

systemctl stop freedv-decoder.service freedv-reporter.service 2>/dev/null || true
if [[ -f $backup/pre-install.tgz ]]; then
  tar -tzf "$backup/pre-install.tgz" >/dev/null
  tar -C / -xzf "$backup/pre-install.tgz"
elif (( decoder_existed == 0 )); then
  systemctl disable freedv-decoder.service freedv-reporter.service 2>/dev/null || true
  rm -f /usr/local/bin/freedv-decoder \
    /etc/systemd/system/freedv-decoder.service \
    /etc/systemd/system/freedv-reporter.service
  rm -rf /etc/freedv-decoder /opt/freedv-reporter /usr/local/share/freedv-dashboard
else
  die "previous decoder existed but its package backup is missing"
fi

systemctl daemon-reload
(( decoder_was_active == 0 )) || systemctl start freedv-decoder.service
(( reporter_was_active == 0 )) || systemctl start freedv-reporter.service
rm -f "$marker"
echo "remote decoder preparation rolled back from $backup"
