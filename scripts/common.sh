# Shared paths for the Librem 5 Android build scripts. Sourced, not executed.
#
# Layout (WORK is any directory you choose; defaults to the parent of the AOSP tree):
#   $WORK/aosp                 AOSP 17 checkout (this repo is checked out at aosp/device/purism)
#   $WORK/src/linux-librem5    Purism kernel source (pinned in kernel/kernel.pin)
#   $WORK/out/kernel           kernel build output
#   $WORK/out/images           flashable images (gpt, bootfs, super)
#   $WORK/out/flash-initramfs  tiny BusyBox initramfs used for flashing
#   $WORK/hostpy               Python venv with pycparser (Mesa build dependency)

set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

if [ -z "${AOSP:-}" ]; then
    if [ -f "$REPO/../../build/envsetup.sh" ]; then
        AOSP=$(cd "$REPO/../.." && pwd)
    elif [ -n "${WORK:-}" ] && [ -f "$WORK/aosp/build/envsetup.sh" ]; then
        AOSP=$WORK/aosp
    fi
fi
WORK=${WORK:-$(cd "${AOSP:?AOSP tree not found: run scripts/setup.sh <workdir> first, or set AOSP=}/.." && pwd)}

KSRC=${KSRC:-$WORK/src/linux-librem5}
KOUT=${KOUT:-$WORK/out/kernel}
IMAGES=${IMAGES:-$WORK/out/images}
FLASH_INITRAMFS=${FLASH_INITRAMFS:-$WORK/out/flash-initramfs/initramfs.cpio.gz}
HOSTPY=${HOSTPY:-$WORK/hostpy}
LUNCH_TARGET=${LUNCH_TARGET:-librem5-cp2a-userdebug}
JOBS=${JOBS:-$(( $(nproc) * 3 / 4 ))}

# eMMC user-area size of the Librem 5 in 512-byte sectors (/sys/block/mmcblk0/size).
EMMC_SECTORS=${EMMC_SECTORS:-61071360}

# USB networking with the flash initramfs (host side is set up by NetworkManager/udev).
PHONE_IP=10.66.0.1
HOST_IP=10.66.0.2

ADB=${ADB:-$(command -v adb || echo "$HOME/bin/adb")}
UUU=${UUU:-uuu}

log() { printf '\033[1;34m== %s\033[0m\n' "$*"; }
die() { printf '\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

aosp_env() {
    # shellcheck disable=SC1091
    export PATH="$HOSTPY/bin:$PATH"
    cd "$AOSP"
    set +u
    source build/envsetup.sh >/dev/null
    lunch "$LUNCH_TARGET" >/dev/null
    set -u
}

# Subject of a format-patch file, unfolded and without "[PATCH n/m]" (same as the commit subject).
patch_subject() { git mailinfo /dev/null /dev/null < "$1" | sed -n 's/^Subject: //p'; }
