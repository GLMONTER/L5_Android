#!/bin/bash
# Refresh kernel prebuilts from the out-of-tree kernel build.
# Usage: device/purism/librem5-kernel/update.sh [kernel-out-dir]
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
KOUT=${1:-$HERE/../../../../out/kernel}
KSRC=$(readlink -f "$KOUT/source")
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

cp "$KOUT/arch/arm64/boot/Image" "$HERE/Image"
mkdir -p "$HERE/dtb"
cp "$KOUT/arch/arm64/boot/dts/freescale/imx8mq-librem5-r4.dtb" "$HERE/dtb/"

make -s -C "$KSRC" O="$KOUT" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
     INSTALL_MOD_PATH="$STAGE" INSTALL_MOD_STRIP=1 modules_install >/dev/null
rm -rf "$HERE/modules"; mkdir -p "$HERE/modules"
find "$STAGE/lib/modules" -name '*.ko' -exec cp {} "$HERE/modules/" \;

# Load list: module names (as lsmod shows them) -> .ko file names.
: > "$HERE/modules.load"
while read -r m; do
  [ -z "$m" ] && continue
  f=""
  for cand in "$m.ko" "${m//_/-}.ko"; do [ -f "$HERE/modules/$cand" ] && f=$cand && break; done
  if [ -z "$f" ]; then echo "WARN: no .ko for $m" >&2; continue; fi
  echo "$f" >> "$HERE/modules.load"
done < "$HERE/modules.load.names"
echo "kernel: $(strings "$HERE/Image" | grep -m1 'Linux version' | cut -d' ' -f3)"
echo "modules: $(ls "$HERE/modules" | wc -l) files, $(wc -l < "$HERE/modules.load") in load list"
