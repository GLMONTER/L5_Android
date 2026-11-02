# Helpers shared by flash-first.sh and flash.sh. Sourced after common.sh.

PSH() { python3 "$REPO/scripts/l5sh.py" "$@"; }

wait_flash_initramfs() {
    log "waiting for the flash initramfs on $PHONE_IP"
    for _ in $(seq 1 60); do
        ping -c1 -W1 "$PHONE_IP" >/dev/null 2>&1 && return 0
        sleep 2
    done
    die "phone did not appear on $PHONE_IP (USB network: host needs $HOST_IP/24 on the cdc_ncm interface)"
}

# Serve $IMAGES over HTTP, run phone-flash.sh <mode> on the phone, wait, verify, reboot.
flash_via_initramfs() {
    local mode=$1
    [ -f "$IMAGES/SHA256SUMS" ] || die "no images in $IMAGES: run scripts/build.sh (or make-images.sh)"
    python3 -m http.server 8000 --bind "$HOST_IP" --directory "$IMAGES" >/dev/null 2>&1 &
    local srv=$!
    trap 'kill $srv 2>/dev/null' EXIT
    sleep 1
    PSH "grep mmcblk /proc/mounts; wget -q -O /tmp/phone-flash.sh http://$HOST_IP:8000/phone-flash.sh && \
         setsid sh /tmp/phone-flash.sh $mode > /tmp/flash.log 2>&1 < /dev/null & sleep 1; echo started" >/dev/null
    log "writing images ($mode) - about 3 minutes"
    until PSH 'cat /tmp/flash.log' 2>/dev/null | grep -qE '^(DONE|FAILED)|rror|No space'; do sleep 10; done
    PSH 'cat /tmp/flash.log' | grep -E '^  (OK|BAD)|DONE|FAILED|rror'
    PSH 'cat /tmp/flash.log' | grep -q '^DONE' || die "flashing failed; the phone stays in flash mode (rerun the script)"
    log "rebooting into Android"
    PSH 'sync; (sleep 1; reboot -f) >/dev/null 2>&1 & echo ok' 2 >/dev/null || true
    kill $srv 2>/dev/null; trap - EXIT
}

wait_android() {
    log "waiting for Android (adb + boot_completed)"
    timeout 300 "$ADB" wait-for-device || die "no adb after 5 minutes"
    for _ in $(seq 1 60); do
        [ "$("$ADB" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ] && { log "Android booted"; return 0; }
        sleep 3
    done
    die "Android did not finish booting"
}

# Download mode -> u-boot, kernel and flash initramfs in RAM over USB (uuu); eMMC untouched.
ram_boot_flash_initramfs() {
    command -v "$UUU" >/dev/null || die "uuu not found (scripts/host-setup.sh installs it)"
    local K=$REPO/librem5-kernel/Image DTB=$REPO/librem5-kernel/dtb/imx8mq-librem5-r4.dtb T
    [ -f "$K" ] && [ -f "$DTB" ] || die "kernel prebuilts missing: run scripts/build-kernel.sh"
    [ -f "$FLASH_INITRAMFS" ] || "$REPO/scripts/build-flash-initramfs.sh"
    lsusb | grep -q '1fc9:012b' || die "phone not in download mode (NXP 1fc9:012b not on USB)"
    log "u-boot to RAM"
    (cd "$REPO/boot/uuu" && "$UUU" 1-uboot-to-ram.lst) || die "uuu failed loading u-boot"
    sleep 2

    # u-boot's fastboot always downloads to 0x43000000; copy each file to a safe address.
    # Reserved RAM to avoid: 0x40000000-0x41000000 (M4), 0x41000000+1M (ramoops), 0x80000000+8M (M4).
    hex() { printf '0x%x' "$(stat -c %s "$1")"; }
    # uuu resolves -f paths relative to the .lst file, so stage everything in one directory.
    T=$(mktemp -d)
    ln -s "$K" "$T/Image"; ln -s "$DTB" "$T/board.dtb"; ln -s "$FLASH_INITRAMFS" "$T/initramfs.cpio.gz"
    cat > "$T/boot.lst" <<LST
uuu_version 1.0.1
FB: download -f Image
FB: ucmd cp.b 0x43000000 0x50000000 $(hex "$K")
FB: download -f board.dtb
FB: ucmd cp.b 0x43000000 0x58000000 $(hex "$DTB")
FB: download -f initramfs.cpio.gz
FB: ucmd cp.b 0x43000000 0x59000000 $(hex "$FLASH_INITRAMFS")
FB: ucmd itest.l *0x50000038 == 0x644d5241
FB: ucmd itest.l *0x58000000 == 0xedfe0dd0
FB: ucmd itest.w *0x59000000 == 0x8b1f
FB: ucmd setenv bootargs console=ttymxc0,115200 console=tty0 loglevel=8 panic=0 rdinit=/init
FB: acmd booti 0x50000000 0x59000000:$(hex "$FLASH_INITRAMFS") 0x58000000
FB: Done
LST
    log "booting the flash initramfs from RAM"
    "$UUU" "$T/boot.lst" || { rm -rf "$T"; die "uuu failed booting the flash initramfs"; }
    rm -rf "$T"
    wait_flash_initramfs
}
