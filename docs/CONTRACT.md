# Platform contract — system half → renderer half

Tactipalm is built in two parallel tracks. This document is the only coupling
between them.

- **System track** owns `image/`, `docs/`, the kernel, the OS, and everything
  up to and including a working `/dev/dri/card0`.
- **Renderer track** owns `src/`, `tools/`, and the `tactipalm` binary.

The renderer track should **not** wait on hardware. Everything below is standard
Mesa, so the identical binary runs from a TTY on any desktop Linux box with
Intel, AMD or nouveau graphics. Develop there; integrate later.

---

## 1. What the system half guarantees

| | |
|---|---|
| Distro | Arch Linux ARM, generic **aarch64** |
| CPU baseline | **ARMv8.0-A**, Cortex-A53. Compile with `-mcpu=cortex-a53`. Do not assume ARMv8.2 (no LSE atomics, no fp16, no dotprod) |
| Kernel | 6.18.49 + vendored H618 display stack (`image/kernel/`) |
| Init | systemd. No X, no Wayland, no display manager, no compositor |
| Launch context | a real VT. The renderer will be DRM master |

### Device nodes

| Path | Provided by | Notes |
|---|---|---|
| `/dev/dri/card0` | `sun4i-drm` (out-of-tree DE33 stack) | KMS. Modeset + page flip |
| `/dev/dri/renderD128` | `panfrost` | Mali-G31 MP2. Render node, usable headless |
| `/dev/fb0` | fbdev emulation | Escape hatch only — see §5 |

### Userspace libraries present in the image

`mesa` · `libdrm` · `libgbm` · `libglvnd`. Nothing else graphics-related.
If you need a library, ask the system track to add it to the package set —
do not assume anything not on this list.

---

## 2. The API surface you may target

- **EGL** via `EGL_PLATFORM_GBM_KHR`, `eglGetPlatformDisplay`.
- **OpenGL ES 3.1** is what Panfrost advertises on Mali-G31.
  **Target GLES 3.0.** Compute shaders and SSBOs are where Panfrost bugs
  concentrate and skinned character rendering needs neither. UBOs and
  vertex-shader skinning only.
- **Scanout format:** `XRGB8888` (`GBM_FORMAT_XRGB8888`), `GBM_BO_USE_SCANOUT |
  GBM_BO_USE_RENDERING`.
- **Modeset:** legacy `drmModeSetCrtc` / `drmModePageFlip` first. Atomic KMS is
  a later optimisation, not a requirement.
- **Target mode:** 1280×720. 1080p is a stretch goal, not a baseline.

### Where the Pi differs from your desktop

These are the things that will work on your dev box and break on the board.
Design for them from the start:

1. **Texture compression.** Mali-G31 gives you **ETC2 and ASTC**. It does *not*
   give you S3TC/DXT/BCn, which is what desktop GPUs have. Anything shipping
   BCn textures will fail on-device. The `.tpm` asset format must carry ETC2 or
   ASTC (KTX2 container).
2. **Tile-based deferred rendering.** Bandwidth is the cost, not triangles.
   Full-screen passes and overdraw are far more expensive than on desktop;
   4× MSAA is far *cheaper* (resolves in tile memory). Your desktop timings
   will not predict board timings — do not tune against them.
3. **Unified memory.** GPU allocations come out of the same LPDDR4 as
   everything else. Budget RSS under ~150 MB total.
4. **GL extension set is smaller.** Query, never assume. In particular check
   `GL_EXT_disjoint_timer_query` exists before relying on it for frame timing.

---

## 3. What the renderer half owns

The system track will not implement, review, or block on any of these:

- The `tactipalm` binary and its DRM/GBM/EGL platform layer
- `tpmconv` and the `.tpm` asset format
- Skinning, animation, blend trees, shading
- `tp_input` / `tp_state` / `tp_ctl` and the control socket protocol
- CLI flags and config file format

The one thing the system track needs from you eventually: **a package list**.
Tell us what the binary links against and we will put it in the image.

---

## 4. Handover artifacts

The system track will deliver into `docs/` as they are measured:

- `glmark2-es2` scores, on- and off-screen
- `modetest -c` output — the real connector and mode list
- `eglinfo` / `glxinfo -B` equivalent: GL vendor, renderer, version, and the
  **full extension string** from the actual board
- Thermal soak logs

Until those land, treat the numbers in `PLAN.md` as estimates.

---

## 5. Current status

| Component | State |
|---|---|
| Mali GPU / Panfrost | **Mainline.** DT node upstream and enabled for this board |
| DE33 / HDMI display | **Out-of-tree.** Vendored in `image/kernel/`, 44 patches, verified to apply. Not yet booted |
| Arch image build | In progress |
| Networking | USB gadget ethernet over USB-C — see `docs/OS-COMPATIBILITY.md` |

**Until HDMI is proven on real hardware, `/dev/dri/card0` is a promise, not a
fact.** `/dev/dri/renderD128` is much closer to certain, since it needs nothing
out-of-tree. If you want an on-device milestone before display works, an
offscreen render to a `renderD128` render node + `glReadPixels` to a PNG will
run headless today.
