#!/usr/bin/env bash
# Build U-Boot + ARM Trusted Firmware for the Orange Pi Zero 2W.
#
# 64-bit sunxi U-Boot needs a TF-A bl31.bin; U-Boot alone will not boot.
# Versions match what Armbian ships for this board so we inherit a known-good
# combination rather than guessing.
#
#   sudo apt install gcc-aarch64-linux-gnu bc bison flex swig \
#                    python3-dev python3-setuptools libssl-dev device-tree-compiler
set -euo pipefail

TFA_TAG="${TFA_TAG:-lts-v2.12.9}"
UBOOT_TAG="${UBOOT_TAG:-v2026.07}"
DEFCONFIG="orangepi_zero2w_defconfig"
PLAT="sun50i_h616"

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${WORK:-${HERE}/work/uboot}"
OUT="${HERE}/out"
export CROSS_COMPILE=aarch64-linux-gnu-

mkdir -p "$WORK" "$OUT"; cd "$WORK"

[ -d trusted-firmware-a ] || git clone --depth 1 -b "$TFA_TAG" \
  https://github.com/ARM-software/arm-trusted-firmware.git trusted-firmware-a
[ -d u-boot ] || git clone --depth 1 -b "$UBOOT_TAG" \
  https://source.denx.de/u-boot/u-boot.git u-boot

echo ":: building TF-A (PLAT=${PLAT})"
make -C trusted-firmware-a PLAT="$PLAT" DEBUG=1 bl31 -j"$(nproc)"
BL31="${WORK}/trusted-firmware-a/build/${PLAT}/debug/bl31.bin"
[ -f "$BL31" ] || { echo "bl31.bin not produced"; exit 1; }

echo ":: building U-Boot (${DEFCONFIG})"
make -C u-boot "$DEFCONFIG"
make -C u-boot BL31="$BL31" -j"$(nproc)"

cp "${WORK}/u-boot/u-boot-sunxi-with-spl.bin" "${OUT}/"
echo
echo "Done: ${OUT}/u-boot-sunxi-with-spl.bin"
echo "Written to the card at 8 KiB offset by build-image.sh."
