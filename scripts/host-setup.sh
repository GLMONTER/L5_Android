#!/bin/bash
# One-time host setup (Ubuntu/Debian x86-64) for building and flashing Android on the Librem 5.
# Run as your normal user; uses sudo for packages, swap, udev and NetworkManager.
#   - build dependencies (AOSP, kernel, images) + uuu
#   - repo, adb and fastboot in ~/bin
#   - swap so RAM+swap >= 96 GB (Soong analysis of this tree peaks around 60 GB)
#   - udev rules: NXP download mode, u-boot fastboot, adb
#   - NetworkManager profile: 10.66.0.2/24 on the phone's USB network (flash initramfs = 10.66.0.1)
set -euo pipefail
BIN=$HOME/bin; mkdir -p "$BIN"

echo "== packages"
sudo apt-get update
sudo apt-get install -y \
    git git-lfs curl python3 python3-venv python-is-python3 ccache rsync zip unzip \
    flex bison libc6-dev-i386 x11proto-core-dev lib32z1-dev libgl1-mesa-dev libxml2-utils xsltproc \
    libncurses-dev libssl-dev libelf-dev dwarves bc lz4 \
    gcc-aarch64-linux-gnu u-boot-tools device-tree-compiler \
    gdisk e2fsprogs cpio squashfs-tools dpkg-dev android-sdk-libsparse-utils \
    uuu

echo "== repo, adb, fastboot -> $BIN"
if [ ! -x "$BIN/repo" ]; then
    curl -fsSL https://storage.googleapis.com/git-repo-downloads/repo -o "$BIN/repo"
    chmod +x "$BIN/repo"
fi
if [ ! -x "$BIN/adb" ]; then
    T=$(mktemp -d)
    curl -fsSL https://dl.google.com/android/repository/platform-tools-latest-linux.zip -o "$T/pt.zip"
    unzip -q "$T/pt.zip" -d "$T"
    install -m 755 "$T/platform-tools/adb" "$T/platform-tools/fastboot" "$BIN/"
    rm -rf "$T"
fi
case ":$PATH:" in *":$BIN:"*) ;; *) echo "   add $BIN to PATH (e.g. in ~/.profile)";; esac

echo "== swap"
ram_gb=$(( $(awk '/MemTotal/ {print $2}' /proc/meminfo) / 1024 / 1024 ))
swap_gb=$(( $(awk '/SwapTotal/ {print $2}' /proc/meminfo) / 1024 / 1024 ))
need=$(( 96 - ram_gb ))
if [ "$swap_gb" -lt "$need" ]; then
    echo "   RAM ${ram_gb} GB + swap ${swap_gb} GB is below 96 GB; creating /swap.img (${need} GB)"
    if swapon --show=NAME --noheadings | grep -qx /swap.img; then sudo swapoff /swap.img; fi
    sudo fallocate -l "${need}G" /swap.img
    sudo chmod 600 /swap.img
    sudo mkswap /swap.img >/dev/null
    sudo swapon /swap.img
    grep -q '^/swap.img' /etc/fstab || echo '/swap.img none swap sw 0 0' | sudo tee -a /etc/fstab >/dev/null
else
    echo "   RAM ${ram_gb} GB + swap ${swap_gb} GB: ok"
fi

echo "== udev rules"
sudo tee /etc/udev/rules.d/51-librem5-android.rules >/dev/null <<'EOF'
# i.MX8MQ boot ROM serial download (Volume-Up + USB) and u-boot SDP/fastboot -- uuu
SUBSYSTEM=="usb", ATTRS{idVendor}=="1fc9", MODE="0660", GROUP="plugdev", TAG+="uaccess"
SUBSYSTEM=="usb", ATTRS{idVendor}=="0525", MODE="0660", GROUP="plugdev", TAG+="uaccess"
# Purism
SUBSYSTEM=="usb", ATTRS{idVendor}=="316d", MODE="0660", GROUP="plugdev", TAG+="uaccess"
# adb (Android) and the flash initramfs gadget
SUBSYSTEM=="usb", ATTRS{idVendor}=="18d1", MODE="0660", GROUP="plugdev", TAG+="uaccess"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1d6b", ATTRS{idProduct}=="0104", MODE="0660", GROUP="plugdev", TAG+="uaccess"
EOF
sudo udevadm control --reload-rules
sudo udevadm trigger

echo "== USB network for flashing (10.66.0.2/24 on the phone's cdc_ncm interface)"
if command -v nmcli >/dev/null; then
    nmcli -t -f NAME con show | grep -qx librem5-flash ||
        sudo nmcli con add type ethernet con-name librem5-flash match.driver cdc_ncm \
            ipv4.method manual ipv4.addresses 10.66.0.2/24 ipv6.method disabled connection.autoconnect yes >/dev/null
    echo "   NetworkManager profile 'librem5-flash' ok"
else
    echo "   no NetworkManager: when the phone is in flash mode, run"
    echo "   sudo ip addr add 10.66.0.2/24 dev <the new cdc_ncm interface> && sudo ip link set <it> up"
fi
echo "== host setup done"
