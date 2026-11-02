#!/bin/bash
# Build the Librem 5 kernel (Purism tree, pinned commit + kernel/patches + config fragment) and
# install Image, DTB and modules into device/purism/librem5-kernel for the AOSP build.
source "$(dirname "$0")/common.sh"
# shellcheck disable=SC1091
source "$REPO/kernel/kernel.pin"
export ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE:-aarch64-linux-gnu-}
command -v "${CROSS_COMPILE}gcc" >/dev/null || die "cross compiler ${CROSS_COMPILE}gcc not found (apt install gcc-aarch64-linux-gnu)"

if [ ! -d "$KSRC/.git" ]; then
    log "cloning $KERNEL_REPO ($KERNEL_BRANCH)"
    mkdir -p "$(dirname "$KSRC")"
    git clone --branch "$KERNEL_BRANCH" --single-branch --shallow-since=2026-01-01 "$KERNEL_REPO" "$KSRC" ||
        git clone --branch "$KERNEL_BRANCH" --single-branch "$KERNEL_REPO" "$KSRC"
fi

# Patch state: base commit + kernel/patches, applied on a local "librem5" branch.
mapfile -t subjects < <(for p in "$REPO"/kernel/patches/*.patch; do patch_subject "$p"; done)
have=$(git -C "$KSRC" log --format=%s -n 30)
missing=0
for s in "${subjects[@]}"; do grep -qxF -- "$s" <<<"$have" || missing=1; done
if [ $missing = 1 ]; then
    log "applying kernel patches on $KERNEL_COMMIT"
    git -C "$KSRC" cat-file -e "$KERNEL_COMMIT" 2>/dev/null || git -C "$KSRC" fetch origin "$KERNEL_COMMIT"
    git -C "$KSRC" checkout -q -B librem5 "$KERNEL_COMMIT"
    git -C "$KSRC" -c user.name="librem5-android" -c user.email="build@localhost" \
        am -q --3way "$REPO"/kernel/patches/*.patch
else
    log "kernel patches already applied"
fi

log "config: PureOS base + kernel/android-librem5.config"
mkdir -p "$KOUT"
(cd "$KSRC" && scripts/kconfig/merge_config.sh -m -O "$KOUT" \
    "$REPO/$KERNEL_DEFCONFIG_BASE" "$REPO/kernel/android-librem5.config" >/dev/null)
make -s -C "$KSRC" O="$KOUT" olddefconfig
bad=0
while IFS= read -r l; do grep -qxF "$l" "$KOUT/.config" || { echo "  not applied: $l"; bad=1; }; done \
    < <(grep -E '^CONFIG_' "$REPO/kernel/android-librem5.config")
[ $bad = 0 ] || die "some config fragment options did not apply (missing dependencies?)"

log "building kernel (-j$JOBS)"
make -C "$KSRC" O="$KOUT" -j"$JOBS" Image.gz modules freescale/imx8mq-librem5-r4.dtb > "$KOUT/build.log" 2>&1 ||
    { tail -30 "$KOUT/build.log"; die "kernel build failed (see $KOUT/build.log)"; }

log "installing prebuilts into device/purism/librem5-kernel"
"$REPO/librem5-kernel/update.sh" "$KOUT"
