#!/bin/bash
# Build the tiny flash initramfs: static BusyBox from a pinned Debian package + boot/flash-initramfs/init.
source "$(dirname "$0")/common.sh"
BUSYBOX_DEB_URL=${BUSYBOX_DEB_URL:-https://deb.debian.org/debian/pool/main/b/busybox/busybox-static_1.38.0-3+b1_arm64.deb}
BUSYBOX_DEB_SHA256=968d1aa8f579fa1ac59c26afa365454369e13cf29848e6400b50028fed0ffda0

D=$(dirname "$FLASH_INITRAMFS"); mkdir -p "$D"
DEB=$D/${BUSYBOX_DEB_URL##*/}
[ -f "$DEB" ] || curl -fsSL -o "$DEB" "$BUSYBOX_DEB_URL" || die "download failed: $BUSYBOX_DEB_URL (set BUSYBOX_DEB_URL to a mirror or snapshot.debian.org)"
echo "$BUSYBOX_DEB_SHA256  $DEB" | sha256sum -c --quiet || die "checksum mismatch for $DEB"

ROOT=$D/root; rm -rf "$ROOT" "$D/deb"; mkdir -p "$ROOT"/{bin,sbin,dev,proc,sys,tmp,etc,usr/bin,usr/sbin} "$D/deb"
dpkg-deb -x "$DEB" "$D/deb"
cp "$D/deb/usr/bin/busybox" "$ROOT/bin/busybox"
install -m 755 "$REPO/boot/flash-initramfs/init" "$ROOT/init"
(cd "$ROOT" && find . -print0 | LC_ALL=C sort -z | cpio --null -o -H newc --owner=0:0 --quiet --reproducible) \
    | gzip -9 -n > "$FLASH_INITRAMFS"
log "flash initramfs: $FLASH_INITRAMFS ($(stat -c %s "$FLASH_INITRAMFS") bytes)"
