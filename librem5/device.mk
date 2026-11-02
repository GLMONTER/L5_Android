# SPDX-License-Identifier: Apache-2.0
# Purism Librem 5

# --- GloDroid feature switches (first boot: keep it minimal) ---
GD_NO_DEFAULT_APPS := true
GD_NO_DEFAULT_CAMERA := true
GD_NO_DEFAULT_MODEM := true
GD_NO_DEFAULT_CODECS := true
GD_NO_DEFAULT_BLUETOOTH := true
GD_NO_DEFAULT_AUDIO := true
# HIDL gatekeeper@1.0 / thermal@2.0 are deprecated at FCM 8; use the AIDL vendor APEXes instead
GD_NO_DEFAULT_GATEKEEPER := true
GD_NO_DEFAULT_THERMAL := true
# GloDroid's power HAL is AIDL v2 (Android 17 needs >= v4); ours is power/ (touch/launch boosts)
GD_NO_DEFAULT_POWER := true

$(call inherit-product, glodroid/configuration/common/device-common.mk)

PRODUCT_PACKAGES += \
    com.android.hardware.gatekeeper.nonsecure \
    com.android.hardware.thermal

PRODUCT_SOONG_NAMESPACES += device/purism/librem5

# fstab (vendor + first-stage). First-stage init reads it from the vendor ramdisk; GloDroid's
# platform layer (disabled here) normally installs that copy.
PRODUCT_PACKAGES += fstab.librem5
PRODUCT_COPY_FILES += device/purism/librem5/fstab.librem5:$(TARGET_COPY_OUT_VENDOR_RAMDISK)/first_stage_ramdisk/fstab.librem5

# Device init
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/init.librem5.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.librem5.rc \
    $(LOCAL_PATH)/uevent.device.rc:$(TARGET_COPY_OUT_VENDOR)/etc/uevent.device.rc

# Firmware from the Librem 5 SPI flash (firmware-librem5-nonfree)
LIBREM5_FW := device/purism/librem5-firmware
PRODUCT_COPY_FILES += \
    $(LIBREM5_FW)/brcm/brcmfmac43752-sdio.bin:$(TARGET_COPY_OUT_VENDOR)/firmware/brcm/brcmfmac43752-sdio.bin \
    $(LIBREM5_FW)/brcm/brcmfmac43752-sdio.txt:$(TARGET_COPY_OUT_VENDOR)/firmware/brcm/brcmfmac43752-sdio.txt \
    $(LIBREM5_FW)/brcm/brcmfmac43752-sdio.clm_blob:$(TARGET_COPY_OUT_VENDOR)/firmware/brcm/brcmfmac43752-sdio.clm_blob \
    $(LIBREM5_FW)/brcm/BCM4362A2.hcd:$(TARGET_COPY_OUT_VENDOR)/firmware/brcm/BCM4362A2.hcd

# GLES 2.0 (etnaviv on GC7000L)
PRODUCT_VENDOR_PROPERTIES += \
    ro.opengles.version=131072

# Slow GPU: lighter SystemUI
PRODUCT_PACKAGES += \
    SystemUISlowGpu

# VINTF: base manifest with target-level (product vars from inherited makefiles are appended after
# this file, so filter-out here has no effect; composer3 V5 is fixed in glodroid/configuration instead)
DEVICE_MANIFEST_FILE += device/purism/librem5/manifest.xml

# Android 17 libcutils only uses memfd automatically on vendor API 202604+ with the
# memfd_class SELinux capability; otherwise it falls back to /dev/ashmem, which upstream
# kernels don't have (system_server dies with "Failed to create ashmem").
PRODUCT_PRODUCT_PROPERTIES += sys.use_memfd=true

# Audio: system_server blocks forever in AudioService if audioserver has no HAL
# (AudioFlinger::onFirstRef null deref). Use AOSP's default AIDL audio HAL (stub primary
# module, r_submix, USB) as Cuttlefish does, until the WM8962 codec gets a real HAL.
PRODUCT_PACKAGES += com.android.hardware.audio
$(call inherit-product, hardware/interfaces/audio/aidl/default/audio_effects.mk)

# Display: the i.MX8MQ LCDIF (mxsfb) primary plane only takes XRGB8888/RGB565, linear.
# SurfaceFlinger renders its client target as BGRA_8888 (= DRM ARGB8888) and drm_hwcomposer
# scans it out as XRGB8888 (local patch in glodroid/vendor/drm_hwcomposer, branch librem5).
# Overlay planes off: the ARGB->XRGB trick would drop real alpha there.
PRODUCT_VENDOR_PROPERTIES += \
    ro.surface_flinger.default_composition_pixel_format=5 \
    ro.vendor.hwc.use_overlay_planes=0 \
    ro.vendor.hwc.drm.argb_as_xrgb=1

# Audio policy: our top level + Cuttlefish's primary module; r_submix and volume tables from frameworks/av
PRODUCT_COPY_FILES += \
    device/purism/librem5/audio/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    device/purism/librem5/audio/primary_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/primary_audio_policy_configuration.xml \
    device/purism/librem5/audio/mixer_paths.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths.xml \
    frameworks/av/services/audiopolicy/config/bluetooth_with_le_audio_policy_configuration_7_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_with_le_audio_policy_configuration_7_0.xml
# The audio APEX's VINTF declares IModule/{default,r_submix,bluetooth}; audioserver waits for
# every declared instance, so the policy must define all three modules.
$(call inherit-product, frameworks/av/services/audiopolicy/audio_policy_config_vendor_1.mk)

# HWUI partial updates / buffer age produce garbage around animating views on Mesa etnaviv
# (stale contents outside the damage region). Always redraw full frames.
PRODUCT_PRODUCT_PROPERTIES += \
    debug.hwui.use_buffer_age=false \
    debug.hwui.use_partial_updates=false

# WM8962 routing is applied by the AIDL primary module from mixer_paths.xml (local
# hardware/interfaces commit, PrimaryRoute). tinyalsa tools for debugging.
PRODUCT_PACKAGES += tinymix tinyplay tinycap tinypcminfo

# Sensors: IIO-backed AIDL HAL (LSM9DS1 accel/gyro, VCNL4000 light/proximity)
PRODUCT_PACKAGES += android.hardware.sensors-service.librem5

# LSM9DS1 axes, measured on an r4: accel reads -9.8 on the up axis for every orientation and the
# gyro reads negative for positive (counter-clockwise) rotation about x and z, i.e. both report all
# three axes negated. The device tree's matrix (1,0,0; 0,1,0; 0,0,-1) only fixes z.
# Magnetometer (verified): a flat CCW turn rotates the field clockwise in the device frame and
# the vertical field reads ~-36 uT screen-up (northern hemisphere). DT has 1,0,0;0,-1,0;0,0,-1.
PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.sensors.lsm9ds1-imu_accel.mount_matrix=-1,0,0;0,-1,0;0,0,-1 \
    ro.vendor.sensors.lsm9ds1-imu_gyro.mount_matrix=-1,0,0;0,-1,0;0,0,-1 \
    ro.vendor.sensors.lsm9ds1_magn.mount_matrix=-1,0,0;0,1,0;0,0,-1

# Bluetooth: BCM4362 on UART4 (kernel hci_uart/btbcm via DT serdev node, kernel patch 0003),
# AOSP default AIDL HAL over the HCI user channel. GloDroid's bluetooth module stays off: its
# audio parts would duplicate IBluetoothAudioProviderFactory from the audio APEX.
PRODUCT_PACKAGES += android.hardware.bluetooth-service.default
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml

# Power HAL with CPU/DRAM boosts on touch and app launch
PRODUCT_PACKAGES += android.hardware.power-service.librem5

# Lights HAL: RGB status LED (charging / battery low / notifications)
PRODUCT_PACKAGES += android.hardware.lights-service.librem5

# Framework overlay: adaptive brightness option + lux -> backlight curve
PRODUCT_PACKAGES += Librem5FrameworkResOverlay

# Bluetooth profiles (the stack starts a profile service only if its property is set; from
# Cuttlefish's phone config, minus LE Audio/ASHA which need extra HAL support)
PRODUCT_PRODUCT_PROPERTIES += \
    bluetooth.device.class_of_device=90,2,12 \
    bluetooth.profile.a2dp.source.enabled=true \
    bluetooth.profile.avrcp.target.enabled=true \
    bluetooth.profile.gatt.enabled=true \
    bluetooth.profile.hfp.ag.enabled=true \
    bluetooth.profile.hid.host.enabled=true \
    bluetooth.profile.hid.device.enabled=true \
    bluetooth.profile.map.server.enabled=true \
    bluetooth.profile.mcp.server.enabled=true \
    bluetooth.profile.opp.enabled=true \
    bluetooth.profile.pan.nap.enabled=true \
    bluetooth.profile.pan.panu.enabled=true \
    bluetooth.profile.pbap.server.enabled=true
