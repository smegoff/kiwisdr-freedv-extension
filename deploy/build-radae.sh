#!/usr/bin/env bash
set -euo pipefail

# FreeDV-maintained portable C implementation. Production selects V1 only;
# the presence of experimental V2 code in this pinned library does not expose
# a V2 mode through the Kiwi extension.
pin=a36161bce0fb37daf3f4602344b095f6817dddb1
opus_pin=940d4e5af64351ca8ba8390df3f555484c567fbb
src=/opt/rade_c
build=$src/build-kiwi
include_root=/usr/local/include/freedv-rade
library_root=/usr/local/lib/freedv-rade

[[ $EUID -eq 0 ]] || { echo "run as root" >&2; exit 2; }
apt-get -o Acquire::ForceIPv4=true update
DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::ForceIPv4=true install -y --no-install-recommends \
  build-essential ca-certificates cmake git autoconf automake libtool pkg-config patch unzip

if [[ ! -d $src/.git ]]; then
  git clone https://github.com/freedv/rade_c.git "$src"
fi
git -C "$src" fetch origin "$pin"
git -C "$src" checkout --detach "$pin"
git -C "$src" reset --hard "$pin"

rm -rf "$build"
cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" --target rade_rx_wav rade_tx_wav --parallel 2

opus_src=$(find "$build" -type d -path '*/build_opus-prefix/src/build_opus' -print -quit)
[[ -n $opus_src && -f $opus_src/.libs/libopus.a ]] || {
  echo "pinned FARGAN/Opus archive was not produced" >&2
  exit 1
}
grep -Fq "$opus_pin" "$src/cmake/BuildOpus.cmake" || {
  echo "unexpected FARGAN/Opus revision" >&2
  exit 1
}

install -d -m 0755 "$include_root/rade" "$include_root/opus" "$library_root" \
  /usr/local/share/freedv-rade
find "$src/src" -maxdepth 1 -type f -name '*.h' -exec install -m 0644 {} "$include_root/rade/" \;
for directory in dnn celt include; do
  rm -rf "$include_root/opus/$directory"
  install -d -m 0755 "$include_root/opus/$directory"
  find "$opus_src/$directory" -maxdepth 1 -type f -name '*.h' \
    -exec install -m 0644 {} "$include_root/opus/$directory/" \;
done
find "$opus_src" -maxdepth 1 -type f -name '*.h' -exec install -m 0644 {} "$include_root/opus/" \;

install -m 0644 "$opus_src/.libs/libopus.a" "$library_root/libopus-fargan.a"
install -m 0755 "$build/src/rade_rx_wav" /usr/local/bin/rade_rx_wav
install -m 0755 "$build/src/rade_tx_wav" /usr/local/bin/rade_tx_wav
install -m 0644 "$src/input_sample.wav" /usr/local/share/freedv-rade/input_sample.wav
install -m 0644 "$src/FDV_offair.wav" /usr/local/share/freedv-rade/FDV_offair.wav

rade_library=$(find "$build/src" -maxdepth 1 -type f -name 'librade.so.*' -print | sort | tail -1)
[[ -n $rade_library ]] || { echo "librade was not produced" >&2; exit 1; }
install -m 0755 "$rade_library" /usr/local/lib/librade.so.0.1
ln -sfn librade.so.0.1 /usr/local/lib/librade.so
ldconfig

printf '%s\n' "$pin" > /usr/local/share/freedv-rade/rade_c.commit
printf '%s\n' "$opus_pin" > /usr/local/share/freedv-rade/opus-fargan.commit
/usr/local/bin/rade_rx_wav --help >/dev/null
/usr/local/bin/rade_tx_wav --help >/dev/null
echo "FreeDV rade_c library installed at $pin; production remains V1-only and feature-gated"
