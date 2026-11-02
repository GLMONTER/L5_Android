#!/bin/bash
# Update an installed Librem 5 Android over adb, no buttons needed:
#   adb root -> set /flashmode on bootfs -> reboot -> u-boot boots the flash initramfs ->
#   images written from $IMAGES -> reboot into Android. Keeps userdata.
# If Android does not boot far enough for adb, use scripts/flash-first.sh --update instead.
source "$(dirname "$0")/common.sh"
source "$REPO/scripts/flash-lib.sh"

timeout 20 "$ADB" wait-for-device || die "phone not on adb (asleep? wake it, or use flash-first.sh --update)"
"$ADB" root >/dev/null; sleep 2; "$ADB" wait-for-device
log "setting flash mode and rebooting"
"$ADB" shell 'mkdir -p /data/local/tmp/bfs && mount -t ext4 /dev/block/by-name/bootfs /data/local/tmp/bfs &&
              touch /data/local/tmp/bfs/flashmode && sync && umount /data/local/tmp/bfs' || die "could not set flash mode"
"$ADB" reboot
wait_flash_initramfs
flash_via_initramfs update   # writing bootfs.img also removes /flashmode
wait_android
