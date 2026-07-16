#!/usr/bin/env bash
set -euo pipefail

candidate=${1:-/opt/kiwi-freedv-v0-1-20}
release=${2:-v0.1.20}
health_release=${release#v}
decoder_health_release=${3:-$health_release}
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
if [[ -d /usr/local/share/freedv-dashboard ]]; then
  cp -a /usr/local/share/freedv-dashboard "$rollback/dashboard-assets"
fi
/opt/freedv-reporter/venv/bin/pip freeze > "$rollback/reporter-requirements.txt"

# Prepare all dependencies and files before interrupting the running decoder.
/opt/freedv-reporter/venv/bin/pip install --no-cache-dir \
  -r "$candidate/reporter/requirements.txt"
/opt/freedv-reporter/venv/bin/python -c 'import aiohttp, socketio'
install -m 0755 "$candidate/build/freedv-decoder" /usr/local/bin/freedv-decoder.new
install -m 0755 "$candidate/reporter/reporter.py" /opt/freedv-reporter/reporter.py.new
install -m 0644 "$candidate/deploy/freedv-decoder.service" /etc/systemd/system/freedv-decoder.service.new
install -m 0644 "$candidate/deploy/freedv-reporter.service" /etc/systemd/system/freedv-reporter.service.new
dashboard_root=/usr/local/share/freedv-dashboard
dashboard_release=$dashboard_root/$decoder_health_release
install -d -m 0755 "$dashboard_release.new"
install -m 0644 "$candidate/dashboard/index.html" "$candidate/dashboard/app.js" \
  "$candidate/dashboard/styles.css" "$dashboard_release.new/"
rm -rf "$dashboard_release"
mv -T "$dashboard_release.new" "$dashboard_release"
ln -sfn "$decoder_health_release" "$dashboard_root/.current-new"
mv -Tf "$dashboard_root/.current-new" "$dashboard_root/current"

if [[ ! -f /etc/freedv-decoder/dashboard.token ]]; then
  umask 0077
  openssl rand -hex 32 > /etc/freedv-decoder/dashboard.token
  chown root:freedv /etc/freedv-decoder/dashboard.token
  chmod 0640 /etc/freedv-decoder/dashboard.token
fi
decoder_env=/etc/freedv-decoder/decoder.env
ensure_env() { grep -q "^$1=" "$decoder_env" || printf '%s=%s\n' "$1" "$2" >> "$decoder_env"; }
ensure_env FREEDV_DASHBOARD_ENABLED 1
ensure_env FREEDV_DASHBOARD_BIND 0.0.0.0
ensure_env FREEDV_DASHBOARD_PORT 8076
ensure_env FREEDV_DASHBOARD_TOKEN_FILE /etc/freedv-decoder/dashboard.token
ensure_env FREEDV_DASHBOARD_ASSET_DIR /usr/local/share/freedv-dashboard/current
ensure_env FREEDV_DASHBOARD_HISTORY_SECONDS 600
ensure_env FREEDV_DASHBOARD_WATERFALL_FPS 10

mv -Tf /usr/local/bin/freedv-decoder.new /usr/local/bin/freedv-decoder
mv -Tf /opt/freedv-reporter/reporter.py.new /opt/freedv-reporter/reporter.py
mv -Tf /etc/systemd/system/freedv-decoder.service.new /etc/systemd/system/freedv-decoder.service
mv -Tf /etc/systemd/system/freedv-reporter.service.new /etc/systemd/system/freedv-reporter.service
systemctl daemon-reload

healthy=0
dashboard_required=$(awk -F= '$1 == "FREEDV_DASHBOARD_ENABLED" {print $2}' "$decoder_env" | tail -n 1)
if systemctl restart freedv-reporter.service freedv-decoder.service; then
  for _ in $(seq 1 30); do
    health=$(/usr/bin/wget -qO- http://127.0.0.1:8074/healthz 2>/dev/null || true)
    dashboard_ok=1
    if [[ $dashboard_required == 1 ]]; then
      dashboard_page=$(/usr/bin/wget -qO- http://127.0.0.1:8076/ 2>/dev/null || true)
      [[ $dashboard_page == *'FreeDV Decoder Diagnostics'* ]] || dashboard_ok=0
    fi
    if [[ $health == *"\"release\":\"$decoder_health_release\""* && $health == *'"kiwi_connected":true'* &&
          $(systemctl is-active freedv-decoder.service) == active &&
          $(systemctl is-active freedv-reporter.service) == active && $dashboard_ok == 1 ]]; then
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
  rm -rf /etc/freedv-decoder
  cp -a "$rollback/config" /etc/freedv-decoder
  rm -rf /usr/local/share/freedv-dashboard
  if [[ -d $rollback/dashboard-assets ]]; then
    cp -a "$rollback/dashboard-assets" /usr/local/share/freedv-dashboard
  fi
  systemctl daemon-reload
  systemctl restart freedv-reporter.service freedv-decoder.service
  exit 4
fi

sha256sum /usr/local/bin/freedv-decoder /opt/freedv-reporter/reporter.py
printf 'active=%s rollback=%s\n' "$release" "$rollback"
