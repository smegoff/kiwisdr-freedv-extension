#!/usr/bin/env bash
set -euo pipefail

# Debian 11 ships Codec2 0.9.2, which predates the complete API used by this
# decoder. Keep the fallback pinned and architecture-neutral for arm64/amd64.
pin=310777b1c6f1af0bc7c72f5b32f80f6fd9136962
src=/opt/codec2-kiwi
build=$src/build-kiwi

[[ $EUID -eq 0 ]] || { echo "run as root" >&2; exit 2; }
apt-get -o Acquire::ForceIPv4=true update
DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::ForceIPv4=true install -y --no-install-recommends \
  build-essential ca-certificates cmake git pkg-config

if [[ ! -d $src/.git ]]; then
  git clone https://github.com/drowe67/codec2.git "$src"
fi
git -C "$src" fetch origin "$pin"
git -C "$src" checkout --detach "$pin"
git -C "$src" reset --hard "$pin"
rm -rf "$build"
cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON -DUNITTEST=OFF -DLPCNET=OFF
cmake --build "$build" --parallel "$(getconf _NPROCESSORS_ONLN)"
cmake --install "$build" --prefix /usr/local
ldconfig

export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/lib64/pkgconfig:${PKG_CONFIG_PATH:-}
pkg-config --atleast-version=1.2.0 codec2
grep -q 'FREEDV_MODE_700E' /usr/local/include/codec2/freedv_api.h
[[ -f /usr/local/include/codec2/reliable_text.h ]]
printf '%s\n' "$pin" > /usr/local/share/codec2-kiwi.commit
echo "Pinned Codec2 installed for Debian 11 compatibility at $pin"
