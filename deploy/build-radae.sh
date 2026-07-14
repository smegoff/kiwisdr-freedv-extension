#!/usr/bin/env bash
set -euo pipefail

pin=73016461252822002a256b2813432bc6e2d6f87a
src=/opt/radae_decoder
build=$src/build-kiwi
export PATH="$(cd "$(dirname "$0")" && pwd):$PATH"

[[ $EUID -eq 0 ]] || { echo "run as root" >&2; exit 2; }
apt-get -o Acquire::ForceIPv4=true update
DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::ForceIPv4=true install -y --no-install-recommends \
  git autoconf automake libtool pkg-config libasound2-dev

if [[ ! -d $src/.git ]]; then
  git clone https://github.com/peterbmarks/radae_decoder.git "$src"
fi
git -C "$src" fetch --depth 1 origin "$pin"
git -C "$src" checkout --detach "$pin"
cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI=OFF -DAUDIO_BACKEND=ALSA -DAVX=OFF
cmake --build "$build" --target webrx_rade_decode --parallel 2
install -m 0755 "$build/tools/webrx_rade_decode" /usr/local/bin/webrx_rade_decode
printf '%s\n' "$pin" > /usr/local/share/radae_decoder.commit
/usr/local/bin/webrx_rade_decode --help >/dev/null
echo "RADE tools-only build installed at pinned commit $pin (production feature flag remains disabled)"
