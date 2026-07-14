#!/usr/bin/env bash
set -euo pipefail

candidate=${1:-/opt/kiwi-freedv-v0-1-15}
release=${2:-v0.1.15}
health_release=${release#v}
stamp=$(date -u +%Y%m%dT%H%M%SZ)
rollback="/root/freedv-rollbacks/pre-${release}-${stamp}"

[[ $EUID -eq 0 ]] || { echo "run as root" >&2; exit 2; }
[[ -x $candidate/build/freedv-decoder ]] || { echo "decoder candidate missing" >&2; exit 2; }
[[ -f $candidate/reporter/reporter.py ]] || { echo "Reporter candidate missing" >&2; exit 2; }
[[ $(systemctl is-active freedv-decoder.service) == active ]] || exit 3
[[ $(systemctl is-active freedv-reporter.service) == active ]] || exit 3
[[ $(/usr/bin/wget -qO- http://127.0.0.1:8074/metrics | awk '/^freedv_sessions / {print $2}') == 0 ]] || {
  echo "a FreeDV session is active" >&2; exit 3;
}

install -d -m 0700 "$rollback"
cp -a /usr/local/bin/freedv-decoder "$rollback/freedv-decoder"
cp -a /opt/freedv-reporter/reporter.py "$rollback/reporter.py"
cp -a /etc/systemd/system/freedv-decoder.service "$rollback/freedv-decoder.service"
cp -a /etc/systemd/system/freedv-reporter.service "$rollback/freedv-reporter.service"
cp -a /etc/freedv-decoder "$rollback/config"
/opt/freedv-reporter/venv/bin/pip freeze > "$rollback/reporter-requirements.txt"

# Prepare all dependencies and files before interrupting the running decoder.
/opt/freedv-reporter/venv/bin/pip install --no-cache-dir \
  -r "$candidate/reporter/requirements.txt"
/opt/freedv-reporter/venv/bin/python -c 'import aiohttp, socketio'
install -m 0755 "$candidate/build/freedv-decoder" /usr/local/bin/freedv-decoder.new
install -m 0755 "$candidate/reporter/reporter.py" /opt/freedv-reporter/reporter.py.new
install -m 0644 "$candidate/deploy/freedv-decoder.service" /etc/systemd/system/freedv-decoder.service.new
install -m 0644 "$candidate/deploy/freedv-reporter.service" /etc/systemd/system/freedv-reporter.service.new

mv -Tf /usr/local/bin/freedv-decoder.new /usr/local/bin/freedv-decoder
mv -Tf /opt/freedv-reporter/reporter.py.new /opt/freedv-reporter/reporter.py
mv -Tf /etc/systemd/system/freedv-decoder.service.new /etc/systemd/system/freedv-decoder.service
mv -Tf /etc/systemd/system/freedv-reporter.service.new /etc/systemd/system/freedv-reporter.service
systemctl daemon-reload

healthy=0
if systemctl restart freedv-reporter.service freedv-decoder.service; then
  for _ in $(seq 1 30); do
    health=$(/usr/bin/wget -qO- http://127.0.0.1:8074/healthz 2>/dev/null || true)
    if [[ $health == *"\"release\":\"$health_release\""* && $health == *'"kiwi_connected":true'* &&
          $(systemctl is-active freedv-decoder.service) == active &&
          $(systemctl is-active freedv-reporter.service) == active ]]; then
      healthy=1
      break
    fi
    sleep 1
  done
fi

if [[ $healthy -ne 1 ]]; then
  echo "decoder health check failed; restoring previous files" >&2
  install -m 0755 "$rollback/freedv-decoder" /usr/local/bin/freedv-decoder
  install -m 0755 "$rollback/reporter.py" /opt/freedv-reporter/reporter.py
  install -m 0644 "$rollback/freedv-decoder.service" /etc/systemd/system/freedv-decoder.service
  install -m 0644 "$rollback/freedv-reporter.service" /etc/systemd/system/freedv-reporter.service
  systemctl daemon-reload
  systemctl restart freedv-reporter.service freedv-decoder.service
  exit 4
fi

sha256sum /usr/local/bin/freedv-decoder /opt/freedv-reporter/reporter.py
printf 'active=%s rollback=%s\n' "$release" "$rollback"
