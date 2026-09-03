# Kernel — Orange Pi Zero 2W (H618) with DE33 / HDMI

Mainline Linux has **no H616/H618 display support**. Verified against `v7.3-rc1`:
`sun50i-h616.dtsi` contains no display-engine, mixer, TCON or HDMI nodes, and
`drivers/gpu/drm/sun4i/` contains no DE33 code. The GPU, by contrast, *is*
mainline and needs nothing from this directory.

This package vendors the out-of-tree display stack so it becomes an explicit,
versioned, reviewable input to the build rather than a mystery blob.

## Pins

| | |
|---|---|
| Kernel | **6.18.49** — longterm; matches Armbian's `current` series |
| Armbian | `armbian/build` @ `6d07521aecf698a6fc52b23245c8d47906c687ad` |
| Series | `patch/kernel/archive/sunxi-6.18/patches.drm` |

## Contents of `patches/`

| File(s) | Origin | Purpose |
|---|---|---|
| `0000-armbian-soc-sunxi-sram-…` | `patches.armbian` | H616 SRAM C1 claim. Patch 0041 adds `allwinner,sram = <&de3_sram 1>` to the DE node, so the pipeline will not probe without it. |
| `0001`–`0042` | `patches.drm` | DE33 mixer/layer rework, planes driver, H616 TCON-TV, **H616 HDMI PHY**, display-engine compatible, DT pipeline, board enablement. |
| `0100-tactipalm-enable-hdmi-on-orangepi-zero2w` | **ours** | See below. |

`T95-broken-CD.patch` from the upstream series is deliberately excluded — it is a
TV-box SD-card quirk unrelated to display.

### Why 0100 exists

Armbian's board-enablement patch (0042) turns HDMI on for tanix-tx1,
orangepi-zero, orangepi-zero2, x96-mate, orangepi-zero3, transpeed-8k618-t and
yuzukihd-chameleon. **The Zero 2W is not in that list.** Applying the Armbian
series alone gets you a working DE33 driver and a dark screen. 0100 supplies all four
missing pieces.

The Zero 2 and Zero 3 board files only add `&hdmi`, because they include
`sun50i-h616-orangepi-zero.dtsi`, which supplies the rest. **The Zero 2W
includes `sun50i-h616.dtsi` directly and inherits none of it.** It therefore
needs the connector node, `&de`, `&hdmi` and `&hdmi_out`.

Enabling `&hdmi` alone is not enough. The `display-engine` node defaults to
`status = "disabled"` in the SoC dtsi, so `sun4i-drm` never binds and no DRM
device appears. The first version of this patch made exactly that mistake; the
built DTB showed `display-engine status=disabled`.

## Verification status

- **Patch application: verified.** All 43 apply to pristine 6.18.49 with zero
  fuzz and zero rejects. Result: 63 display-node lines in `sun50i-h616.dtsi`
  (was 0), new `drivers/gpu/drm/sun4i/sun50i_planes.{c,h}`, and `&hdmi` enabled
  on the Zero 2W.
- **Kernel build: verified.** `Image` (42 MB), modules and the DTB all produced
  with the full series applied.
- **Device tree: verified** by decompiling the built DTB —
  `display-engine okay`, `hdmi okay`, `hdmi-phy` enabled, `hdmi-connector`
  present as type `c` (mini-HDMI), endpoints resolved.
- **Boot: not tested.** No hardware access yet.

## Building

### Fast path (Phase 0B bring-up, x86_64 host)

```
sudo apt install gcc-aarch64-linux-gnu bc bison flex libssl-dev libelf-dev
./build-cross.sh
```

Produces `out/Image`, `out/sun50i-h618-orangepi-zero2w.dtb` and a modules
tarball. No packaging — this is the "does HDMI light up" loop.

### Proper path (Arch package, aarch64)

Run `makepkg` on the board, in an ALARM chroot, or under qemu-user. Slow on a
Zero 2W (1 GB RAM will struggle to link); prefer a chroot on a bigger machine.

## Bumping the pin

1. Edit `ARMBIAN_PIN` / `ARMBIAN_SERIES` in `fetch-patches.sh`, run it.
2. `git diff patches/` — review what changed.
3. Re-run `build-cross.sh` from a clean `work/`.
4. Delete any patch that has landed upstream. **The stack should only shrink.**

When `patches/` is empty, mainline has caught up and this directory can go.
