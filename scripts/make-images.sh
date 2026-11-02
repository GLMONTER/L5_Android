#!/bin/bash
# Turn the AOSP build output into images that Purism's u-boot can boot and that the flash
# scripts write to the eMMC.
#
# Purism u-boot has no abootimg/bcb/avb, so instead of boot/vendor_boot/init_boot partitions
# a small ext4 "bootfs" partition (partition 1, legacy-bootable) holds Image + DTB + one
# combined ramdisk + boot.scr; u-boot's distro boot finds boot.scr there.
#
# Output in $IMAGES: gpt-primary.img gpt-backup.img bootfs.img super.img SHA256SUMS
#                    (+ gpt-layout.txt, cmdline.txt, boot.cmd for reference)
source "$(dirname "$0")/common.sh"

PRODUCT=$AOSP/out/target/product/librem5
UNPACK=$AOSP/system/tools/mkbootimg/unpack_bootimg.py
DTB=imx8mq-librem5-r4.dtb
SELINUX_MODE=${SELINUX_MODE:-permissive}
for f in vendor_boot.img boot.img super.img kernel; do
    [ -f "$PRODUCT/$f" ] || die "$PRODUCT/$f missing: build Android first (scripts/build.sh)"
done
[ -f "$FLASH_INITRAMFS" ] || "$REPO/scripts/build-flash-initramfs.sh"
for t in mkimage mke2fs sgdisk; do command -v $t >/dev/null || die "missing host tool: $t"; done

rm -rf "$IMAGES"; mkdir -p "$IMAGES/unpack" "$IMAGES/bootfs"
O=$IMAGES

# --- 1. ramdisk: vendor ramdisk fragment(s) first, then the generic ramdisk ---
python3 "$UNPACK" --boot_img "$PRODUCT/vendor_boot.img" --out "$O/unpack/vendor_boot" > "$O/unpack/vendor_boot.txt"
python3 "$UNPACK" --boot_img "$PRODUCT/boot.img" --out "$O/unpack/boot" > "$O/unpack/boot.txt"
if [ -f "$PRODUCT/init_boot.img" ]; then
    python3 "$UNPACK" --boot_img "$PRODUCT/init_boot.img" --out "$O/unpack/init_boot" > "$O/unpack/init_boot.txt"
    GENERIC=$O/unpack/init_boot/ramdisk
else
    GENERIC=$O/unpack/boot/ramdisk
fi
mapfile -t VENDOR_RDS < <(ls "$O"/unpack/vendor_boot/vendor_ramdisk* 2>/dev/null | sort)
[ ${#VENDOR_RDS[@]} -gt 0 ] || die "no vendor ramdisk found in vendor_boot.img"
# Concatenated compressed cpio archives are fine for the kernel's initramfs unpacker.
cat "${VENDOR_RDS[@]}" "$GENERIC" > "$O/ramdisk.img"

# --- 2. kernel command line: vendor_boot cmdline + what a bootloader would add ---
# No bootconfig in this kernel: androidboot.* go on the cmdline (init falls back to it).
VCMD=$(sed -n 's/^vendor command line args: //p' "$O/unpack/vendor_boot.txt")
BCMD=$(sed -n 's/^command line args: //p' "$O/unpack/boot.txt")
EXTRA="androidboot.slot_suffix=_a androidboot.force_normal_boot=1 androidboot.verifiedbootstate=orange"
EXTRA+=" androidboot.selinux=$SELINUX_MODE androidboot.serialno=librem5 printk.devkmsg=on panic=0"
CMDLINE=$(echo "$BCMD $VCMD $EXTRA" | tr ' ' '\n' | awk 'NF && !seen[$0]++' | tr '\n' ' ' | sed 's/ $//')
echo "$CMDLINE" > "$O/cmdline.txt"

# --- 3. bootfs: Image, DTB, ramdisk, flash initramfs, boot.scr ---
cp "$PRODUCT/kernel" "$O/bootfs/Image"
cp "$REPO/librem5-kernel/dtb/$DTB" "$O/bootfs/$DTB"
cp "$O/ramdisk.img" "$O/bootfs/ramdisk.img"
# Flash mode: if /flashmode exists on bootfs, boot the flash initramfs (USB network + telnet on
# $PHONE_IP) instead of Android, so images can be rewritten without download mode (scripts/flash.sh).
cp "$FLASH_INITRAMFS" "$O/bootfs/flash-initramfs.img"
# Load addresses avoid reserved RAM: M4 0x40000000-0x41000000 and 0x80000000+8M, ramoops 0x41000000.
cat > "$O/boot.cmd" <<EOF
echo "Librem 5 Android (bootfs on mmc \${devnum}:\${distro_bootpart})"
if test -e \${devtype} \${devnum}:\${distro_bootpart} /flashmode; then
  echo "flashmode: booting the flash initramfs"
  setenv bootargs "console=ttymxc0,115200 console=tty0 loglevel=8 panic=0 rdinit=/init"
  load \${devtype} \${devnum}:\${distro_bootpart} 0x50000000 /Image
  load \${devtype} \${devnum}:\${distro_bootpart} 0x58000000 /$DTB
  load \${devtype} \${devnum}:\${distro_bootpart} 0x59000000 /flash-initramfs.img
  booti 0x50000000 0x59000000:\${filesize} 0x58000000
fi
setenv bootargs "$CMDLINE"
load \${devtype} \${devnum}:\${distro_bootpart} 0x50000000 /Image
load \${devtype} \${devnum}:\${distro_bootpart} 0x58000000 /$DTB
load \${devtype} \${devnum}:\${distro_bootpart} 0x59000000 /ramdisk.img
booti 0x50000000 0x59000000:\${filesize} 0x58000000
EOF
mkimage -A arm64 -T script -C none -n "Librem 5 Android" -d "$O/boot.cmd" "$O/bootfs/boot.scr" >/dev/null
# Fixed UUID/hash seed/timestamps so identical inputs give an identical bootfs.img.
E2FSPROGS_FAKE_TIME=1700000000 mke2fs -q -t ext4 -L bootfs -U 4c35626f-6f74-4673-0000-000000000001 \
    -E hash_seed=4c35626f-6f74-4673-0000-000000000002 -d "$O/bootfs" "$O/bootfs.img" 256M

# --- 4. super (dynamic partitions; GloDroid config builds super.img) ---
cp "$PRODUCT/super.img" "$O/super.img"

# --- 5. GPT for the eMMC user area ---
# Sector positions are fixed and used by phone-flash.sh (MiB: bootfs 1, misc 257, metadata 258,
# super 274, userdata 6418). frp is the second half of a 1 MiB slot after misc.
DISK=$O/disk-layout.img
truncate -s $((EMMC_SECTORS * 512)) "$DISK"
sgdisk -Z "$DISK" >/dev/null 2>&1 || true
G=4c35616e-6472-6f69-6400  # fixed GUID prefix -> reproducible tables
sgdisk -a 2048 -U $G-000000000000 \
  -n 1:2048:526335      -c 1:bootfs   -A 1:set:2 -u 1:$G-000000000001 \
  -n 2:526336:527359    -c 2:misc                -u 2:$G-000000000002 \
  -n 3:528384:561151    -c 3:metadata            -u 3:$G-000000000003 \
  -n 4:561152:13144063  -c 4:super               -u 4:$G-000000000004 \
  -n 5:13144064:0       -c 5:userdata            -u 5:$G-000000000005 \
  "$DISK" >/dev/null
sgdisk -a 8 -n 6:527360:528383 -c 6:frp -u 6:$G-000000000006 "$DISK" >/dev/null
sgdisk -p "$DISK" > "$O/gpt-layout.txt"
dd if="$DISK" of="$O/gpt-primary.img" bs=512 count=34 status=none
dd if="$DISK" of="$O/gpt-backup.img" bs=512 skip=$((EMMC_SECTORS - 33)) count=33 status=none
rm -f "$DISK"

cp "$REPO/scripts/phone-flash.sh" "$O/"
(cd "$O" && sha256sum gpt-primary.img gpt-backup.img bootfs.img super.img > SHA256SUMS)
log "images in $O (SELinux $SELINUX_MODE)"
cat "$O/SHA256SUMS"
