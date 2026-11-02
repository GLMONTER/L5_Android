# u-boot for RAM boot

`u-boot-librem5.imx` is Purism's stock Librem 5 u-boot (`2022.10-g956aa590c9`, the version
flashed on the phones), used only to boot over USB in download mode (`scripts/flash-first.sh`).
The phone's own u-boot in eMMC boot0 is never modified.

It is GPL-2.0 software; source: https://source.puri.sm/Librem5/u-boot-builder (builds
https://source.puri.sm/Librem5/uboot-imx). SHA-256 in `SHA256SUMS`.
