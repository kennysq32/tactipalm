#!/usr/bin/env bash
# Cross-compile the patched kernel on an x86_64 host (Debian/Kali/Ubuntu).
# This is the Phase 0B bring-up path: no packaging, just a kernel to test with.
#
#   apt install gcc-aarch64-linux-gnu bc bison flex libssl-dev libelf-dev
#
# Output lands in out/: Image, the Zero 2W dtb, and a modules tarball.
set -euo pipefail

KVER="${KVER:-6.18.49}"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${WORK:-${HERE}/work}"
OUT="${HERE}/out"
JOBS="${JOBS:-$(nproc)}"

export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-

mkdir -p "$WORK" "$OUT"
cd "$WORK"

if [ ! -d "linux-${KVER}" ]; then
  [ -f "linux-${KVER}.tar.xz" ] || \
    curl -fL# -o "linux-${KVER}.tar.xz" \
      "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${KVER}.tar.xz"
  echo ":: extracting"
  tar xf "linux-${KVER}.tar.xz"

  echo ":: applying $(ls "${HERE}"/patches/*.patch | wc -l) patches"
  cd "linux-${KVER}"
  for p in "${HERE}"/patches/*.patch; do
    patch -p1 --forward --no-backup-if-mismatch < "$p" >/dev/null \
      || { echo "FAILED: $(basename "$p")"; exit 1; }
  done
  cd ..
fi

cd "linux-${KVER}"
[ -f .config ] || {
  make defconfig
  ./scripts/kconfig/merge_config.sh -m -O . .config "${HERE}/config.fragment"
  make olddefconfig
}

for sym in CONFIG_DRM_SUN4I CONFIG_DRM_SUN8I_MIXER CONFIG_DRM_SUN8I_DW_HDMI \
           CONFIG_SUN50I_H6_PRCM_PPU CONFIG_SUNXI_SRAM CONFIG_DRM_PANFROST \
           CONFIG_DRM_SUN50I_PLANES; do
  grep -q "^${sym}=[ym]" .config || { echo "MISSING: ${sym}"; exit 1; }
done

make -j"${JOBS}" Image dtbs modules

cp arch/arm64/boot/Image "${OUT}/"
cp arch/arm64/boot/dts/allwinner/sun50i-h618-orangepi-zero2w.dtb "${OUT}/"
rm -rf "${WORK}/modroot" && mkdir -p "${WORK}/modroot"
make INSTALL_MOD_PATH="${WORK}/modroot" INSTALL_MOD_STRIP=1 modules_install
tar -C "${WORK}/modroot" -czf "${OUT}/modules-${KVER}.tar.gz" lib/modules

echo
echo "Done. Artifacts in ${OUT}:"
ls -lh "${OUT}"
echo
echo "Install onto the SD card (adjust mount points):"
echo "  cp out/Image                             /mnt/boot/"
echo "  cp out/sun50i-h618-orangepi-zero2w.dtb   /mnt/boot/dtbs/allwinner/"
echo "  tar -C /mnt/root -xzf out/modules-${KVER}.tar.gz"
