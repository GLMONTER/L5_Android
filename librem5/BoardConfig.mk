# SPDX-License-Identifier: Apache-2.0
# Purism Librem 5 (NXP i.MX8MQ)

BC_PATH := $(patsubst $(CURDIR)/%,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

# Same switches as device.mk (board config is evaluated separately)
GD_NO_DEFAULT_CAMERA := true
GD_NO_DEFAULT_MODEM := true
GD_NO_DEFAULT_CODECS := true
GD_NO_DEFAULT_BLUETOOTH := true
GD_NO_DEFAULT_AUDIO := true

include glodroid/configuration/common/board-common.mk

# --- Don't use GloDroid's platform layer (its own u-boot/ATF/kernel builds) ---
# We boot with Purism's u-boot and a kernel built from Purism's tree.
BOARD_USES_GLODROID_MAKE_TARGETS := false
BOARD_BUILD_GLODROID_KERNEL := false

# --- Mesa: etnaviv for the GC7000L GPU, imx for the display (kmsro) ---
BOARD_MESA3D_GALLIUM_DRIVERS := etnaviv

# minigbm: GloDroid's patches rebased on minigbm 93e16b9 (see patches-minigbm/README.md)
BOARD_MINIGBM_PATCHES_DIRS := $(BC_PATH)patches-minigbm

# --- Kernel (prebuilt; refresh with device/purism/librem5-kernel/update.sh) ---
LIBREM5_KERNEL := device/purism/librem5-kernel
PRODUCT_COPY_FILES += $(LIBREM5_KERNEL)/Image:kernel
BOARD_PREBUILT_DTBIMAGE_DIR := $(LIBREM5_KERNEL)/dtb
BOARD_KERNEL_BASE := 0x40000000

BOARD_VENDOR_KERNEL_MODULES := $(wildcard $(LIBREM5_KERNEL)/modules/*.ko)
BOARD_VENDOR_KERNEL_MODULES_LOAD := $(shell cat $(LIBREM5_KERNEL)/modules.load)

BOARD_KERNEL_CMDLINE += \
    console=ttymxc0,115200 console=tty0 \
    firmware_class.path=/vendor/firmware \
    androidboot.hardware=librem5 \
    androidboot.boot_devices=soc@0/30800000.bus/30b40000.mmc

# --- SELinux ---
BOARD_VENDOR_SEPOLICY_DIRS += $(BC_PATH)/sepolicy/vendor
