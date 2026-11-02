#!/bin/bash
# Install (or recover) Android on a Librem 5 from download mode. Nothing is read from the eMMC:
# u-boot, kernel and the flash initramfs are loaded into RAM over USB with uuu, then the images
# from $IMAGES are written. Never touches mmcblk0boot0/1, so download mode keeps working.
#
#   scripts/flash-first.sh            first install: wipes the eMMC user area (PureOS, all data)
#   scripts/flash-first.sh --update   recovery: rewrite system/boot, keep userdata
#
# Download mode: power off, hold Volume-Up while plugging in USB (the phone shows nothing).
source "$(dirname "$0")/common.sh"
source "$REPO/scripts/flash-lib.sh"

MODE=full
[ "${1:-}" = --update ] && MODE=update
[ -f "$IMAGES/SHA256SUMS" ] || die "no images in $IMAGES: run scripts/build.sh first"

if [ $MODE = full ]; then
    cat "$IMAGES/gpt-layout.txt"
    read -r -p "This ERASES everything on the eMMC user area (PureOS and all data). Type WIPE: " ans
    [ "$ans" = WIPE ] || exit 1
fi

ram_boot_flash_initramfs
flash_via_initramfs $MODE
wait_android
[ $MODE = full ] && log "first boot formats /data; the setup wizard follows"
exit 0
