#!/bin/bash
# Fetch the Broadcom BCM43752 Wi-Fi / BCM4362 Bluetooth firmware into librem5-firmware/brcm/.
# It is non-free and not redistributed here; it comes from Purism's public firmware repo (the
# files burned into every Librem 5's SPI flash), at a pinned commit with checksums verified.
source "$(dirname "$0")/common.sh"
FILES="brcmfmac43752-sdio.bin brcmfmac43752-sdio.txt brcmfmac43752-sdio.clm_blob BCM4362A2.hcd"
# Submodule of https://source.puri.sm/Librem5/fw/firmware-librem5-nonfree (the SPI flash squashfs).
FW_REPO=${FW_REPO:-https://source.puri.sm/Librem5/firmware-brcm43752-nonfree.git}
FW_COMMIT=4fff6ff76fa69b5b3d9d234e8df953ccbbf39fa9
FW_SHA256="
f892de1047ac91dfed071a0fd2beb1ea783d6d98599ac53477cae8799f1f05ef  brcmfmac43752-sdio.bin
2d2723101fe9c66c853ddb1e2d715851ba100a4390f8ac72fc84dd35736cc66f  brcmfmac43752-sdio.txt
5143146e1923f87f7aab8df043abcf89a657fa9fdc3b22a38806399730d9a97a  brcmfmac43752-sdio.clm_blob
18901a5bef1d418b6895e92d0afae36234f4160b237465dfca3d75e9844e93ef  BCM4362A2.hcd
"
DEST=$REPO/librem5-firmware/brcm; mkdir -p "$DEST"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT

log "fetching firmware from $FW_REPO @ ${FW_COMMIT:0:12}"
git -C "$T" init -q fw && git -C "$T/fw" fetch -q --depth 1 "$FW_REPO" "$FW_COMMIT" &&
    git -C "$T/fw" checkout -q FETCH_HEAD || die "could not fetch $FW_REPO"
for f in $FILES; do install -m 644 "$T/fw/firmware/brcm/$f" "$DEST/$f"; done
(cd "$DEST" && echo "$FW_SHA256" | grep -v '^$' | sha256sum -c --quiet) ||
    die "firmware checksums differ from the pinned ones"
log "firmware installed in $DEST"
