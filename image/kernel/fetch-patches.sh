#!/usr/bin/env bash
# Re-fetch the vendored Armbian patch series from a pinned commit.
# Patches are committed to git; this script exists to prove provenance and to
# make bumping the pin an explicit, reviewable act.
set -euo pipefail

ARMBIAN_PIN="6d07521aecf698a6fc52b23245c8d47906c687ad"
ARMBIAN_SERIES="sunxi-6.18"
BASE="https://raw.githubusercontent.com/armbian/build/${ARMBIAN_PIN}/patch/kernel/archive/${ARMBIAN_SERIES}"
OUT="$(dirname "$0")/patches"

echo "Armbian pin:    ${ARMBIAN_PIN}"
echo "Patch series:   ${ARMBIAN_SERIES}"
mkdir -p "$OUT"

# The DRM series, in series.conf order. T95-broken-CD is a TV-box SD-card
# quirk with nothing to do with display — deliberately excluded.
curl -fsSL "${BASE}/series.conf" \
  | grep 'patches\.drm/' | sed 's/^[[:space:]]*//' | grep -v 'T95-broken-CD' \
  | while read -r rel; do
      echo "  ${rel}"
      curl -fsSL "${BASE}/${rel}" -o "${OUT}/$(basename "$rel")"
    done

# H616 SRAM C1 claim. Patch 0041 adds `allwinner,sram = <&de3_sram 1>` to the
# display engine node, so the display pipeline does not probe without this.
echo "  patches.armbian/drv-soc-sunxi-sram-add-h616-sram-c1.patch"
curl -fsSL "${BASE}/patches.armbian/drv-soc-sunxi-sram-add-h616-sram-c1.patch" \
  -o "${OUT}/0000-armbian-soc-sunxi-sram-add-h616-sram-c1.patch"

echo
echo "Fetched $(ls "$OUT"/*.patch | wc -l) patches."
echo "NOTE: 0100-tactipalm-* is ours, not Armbian's. It is not re-fetched."
