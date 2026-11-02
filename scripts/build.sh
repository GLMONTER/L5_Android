#!/bin/bash
# One-command build: kernel -> Android (AOSP 17, librem5-cp2a-userdebug) -> flashable images.
#   scripts/build.sh                 everything
#   SKIP_KERNEL=1 scripts/build.sh   reuse the installed kernel prebuilts
#   SELINUX_MODE=permissive scripts/build.sh   debugging (default: enforcing)
source "$(dirname "$0")/common.sh"
[ -f "$REPO/librem5-firmware/brcm/BCM4362A2.hcd" ] || "$REPO/scripts/extract-firmware.sh"
"$REPO/scripts/apply-patches.sh"
[ "${SKIP_KERNEL:-0}" = 1 ] || "$REPO/scripts/build-kernel.sh"
log "building Android (-j$JOBS)"
( aosp_env && m -j"$JOBS" ) > "$WORK/out/android-build.log" 2>&1 ||
    { grep -E 'FAILED:|error:' "$WORK/out/android-build.log" | head -20; die "Android build failed (see $WORK/out/android-build.log)"; }
"$REPO/scripts/make-images.sh"
log "done. Flash with scripts/flash.sh (installed phone) or scripts/flash-first.sh (download mode)"
