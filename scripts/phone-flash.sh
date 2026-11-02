#!/bin/sh
# Runs ON THE PHONE inside the flash initramfs (BusyBox). Fetches images from the host over USB
# networking and writes them to the eMMC user area (/dev/mmcblk0). Never touches mmcblk0boot0/1
# (u-boot). Offsets match make-images.sh.
#
#   phone-flash.sh update   GPT + bootfs + super; keeps userdata (default)
#   phone-flash.sh full     first install: also zeroes misc/frp/metadata and the start of
#                           userdata, so Android formats them on first boot (wipes all data)
set -e
MODE=${1:-update}
D=/dev/mmcblk0
U=${URL:-http://10.66.0.2:8000}
N=$(cat /sys/block/mmcblk0/size)
echo "start $(date) mode=$MODE sectors=$N"
[ "$N" = 61071360 ] || echo "WARNING: unexpected eMMC size $N (images were made for 61071360)"

if [ "$MODE" = full ]; then
    echo "wipe misc/frp/metadata, userdata head"
    dd if=/dev/zero of=$D bs=1M seek=257 count=17     # misc + frp + metadata (257..273 MiB)
    dd if=/dev/zero of=$D bs=1M seek=6418 count=64    # userdata superblocks
else
    echo "frp"
    dd if=/dev/zero of=$D bs=512 seek=527360 count=1024
fi
echo "gpt";    wget -q -O - $U/gpt-primary.img | dd of=$D bs=512 seek=0
               wget -q -O - $U/gpt-backup.img  | dd of=$D bs=512 seek=$((N - 33))
echo "bootfs"; wget -q -O - $U/bootfs.img | dd of=$D bs=1M seek=1
echo "super";  wget -q -O - $U/super.img  | dd of=$D bs=1M seek=274
sync

echo "verify"
wget -q -O /tmp/SHA256SUMS $U/SHA256SUMS
SUPER_MB=$(( $(wget -q -S --spider $U/super.img 2>&1 | sed -n 's/.*Content-Length: *//p' | tr -d '\r') / 1048576 ))
ok=1
check() {  # name actual
    want=$(grep " $1\$" /tmp/SHA256SUMS | cut -d' ' -f1)
    if [ "$2" = "$want" ]; then echo "  OK   $1"; else echo "  BAD  $1 ($2 != $want)"; ok=0; fi
}
check gpt-primary.img "$(dd if=$D bs=512 count=34 2>/dev/null | sha256sum | cut -d' ' -f1)"
check gpt-backup.img  "$(dd if=$D bs=512 skip=$((N - 33)) count=33 2>/dev/null | sha256sum | cut -d' ' -f1)"
check bootfs.img      "$(dd if=$D bs=1M skip=1 count=256 2>/dev/null | sha256sum | cut -d' ' -f1)"
check super.img       "$(dd if=$D bs=1M skip=274 count=$SUPER_MB 2>/dev/null | sha256sum | cut -d' ' -f1)"
blockdev --rereadpt $D 2>/dev/null || true
[ $ok = 1 ] && echo "DONE $(date)" || echo "FAILED verification $(date)"
