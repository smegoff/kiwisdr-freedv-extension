#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then echo "run as root" >&2; exit 2; fi
src=${1:-/opt/kiwi-freedv}

apt-get -o Acquire::ForceIPv4=true update
DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::ForceIPv4=true install -y --no-install-recommends \
  build-essential cmake pkg-config libboost-system-dev nlohmann-json3-dev \
  libcodec2-dev libsamplerate0-dev libfftw3-dev libssl-dev python3-venv ca-certificates avahi-daemon

id freedv >/dev/null 2>&1 || useradd --system --home /nonexistent --shell /usr/sbin/nologin freedv
cmake -S "$src/decoder" -B "$src/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$src/build" --parallel
cmake --install "$src/build" --prefix /usr/local

install -d -m 0750 -o root -g freedv /etc/freedv-decoder
install -d -m 0755 /usr/local/share/freedv-dashboard/0.1.20
install -m 0644 "$src/dashboard/index.html" "$src/dashboard/app.js" \
  "$src/dashboard/styles.css" /usr/local/share/freedv-dashboard/0.1.20/
ln -sfn 0.1.20 /usr/local/share/freedv-dashboard/current
if [[ ! -f /etc/freedv-decoder/dashboard.token ]]; then
  umask 0077
  openssl rand -hex 32 > /etc/freedv-decoder/dashboard.token
  chown root:freedv /etc/freedv-decoder/dashboard.token
  chmod 0640 /etc/freedv-decoder/dashboard.token
fi
install -d -m 0755 /opt/freedv-reporter
install -m 0755 "$src/reporter/reporter.py" /opt/freedv-reporter/reporter.py
python3 -m venv /opt/freedv-reporter/venv
/opt/freedv-reporter/venv/bin/pip install --no-cache-dir -r "$src/reporter/requirements.txt"
/opt/freedv-reporter/venv/bin/python -c 'import aiohttp, socketio'

install -m 0644 "$src/deploy/freedv-decoder.service" /etc/systemd/system/
install -m 0644 "$src/deploy/freedv-reporter.service" /etc/systemd/system/
[[ -f /etc/freedv-decoder/decoder.env ]] || install -m 0640 -o root -g freedv "$src/deploy/decoder.env.example" /etc/freedv-decoder/decoder.env
[[ -f /etc/freedv-decoder/reporter.env ]] || install -m 0640 -o root -g freedv "$src/deploy/reporter.env.example" /etc/freedv-decoder/reporter.env
systemctl daemon-reload
systemctl enable avahi-daemon.service freedv-decoder.service freedv-reporter.service

echo "Edit /etc/freedv-decoder/*.env, run ctest, then start the services."
echo "Dashboard access key: /etc/freedv-decoder/dashboard.token (retrieve as root)."
