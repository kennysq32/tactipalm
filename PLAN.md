# Tactipalm — Project Plan

> **This project is split across two sessions.** The *system* track owns
> `image/` and `docs/` — OS, kernel, display stack. The *renderer* track owns
> `src/` and `tools/`. They are coupled only through
> [`docs/CONTRACT.md`](docs/CONTRACT.md).

A CLI-launched 3D character renderer running on bare Arch Linux ARM (no X, no Wayland)
on an Orange Pi Zero 2W.

**Locked decisions:** mini-HDMI output · glTF/GLB with skeletal animation · custom minimal
C renderer on OpenGL ES · baseline behaviour is an animation loop, extensible later.

---

## 1. Hardware reality check

| | |
|---|---|
| SoC | Allwinner H618 |
| CPU | 4× Cortex-A53 @ 1.5 GHz — **ARMv8.0-A** |
| GPU | Mali-G31 MP2 (Bifrost), open-source **Panfrost** driver, GLES 3.1 |
| RAM | 1 / 1.5 / 2 / 4 GB LPDDR4, **shared with the GPU** |
| Storage | microSD only — slow random I/O, and it is your asset-load bottleneck |
| Cooling | passive by default — sustained GPU load *will* thermally throttle |
| Display | mini-HDMI 2.0 |

Three consequences that shape everything downstream:

1. **ARMv8.0 rules out `ports.archlinux.page`.** The newer Arch Linux aarch64 ports project
   builds for ARMv8.2+ only and will not run on a Cortex-A53. You must use classic
   **Arch Linux ARM** (`archlinuxarm.org`) generic aarch64. Verify this before you build an
   image — it is the single easiest way to lose a weekend.
2. **Mali-G31 is a tile-based deferred renderer.** Memory bandwidth is the enemy, not
   triangle count. Full-screen passes, overdraw, and large render targets cost far more than
   geometry. Design the look around that (see §7).
3. **Target 1280×720.** 1080p is reachable for a single character but leaves no headroom.
   Decide 720p now and treat 1080p as a stretch goal.

**Buy before you start:** a USB-UART serial adapter (3.3 V) and a second microSD card.
Headless kernel bring-up without serial console is guesswork. Also get a heatsink.
If you have a choice of board, take the 2 GB or 4 GB variant.

---

## 2. The stack

```
tactipalm  (single ELF, launched from a TTY)
├── platform   libdrm (KMS) → GBM → EGL          ← no X11, no Wayland, no compositor
├── graphics   OpenGL ES 3.1 via Mesa / Panfrost
├── assets     cgltf (offline) → custom .tpm binary (runtime)
├── math       HandmadeMath (single header) or your own
└── control    unix socket + state machine        ← where future features plug in
```

The `DRM → GBM → EGL` path is the whole reason this works without a GUI. You open
`/dev/dri/card0`, become DRM master, set a video mode yourself, allocate scanout buffers
through GBM, wrap them in an EGL surface, and page-flip. This is exactly what `kmscube`
does, and `kmscube` is your reference implementation — read it early.

**Note on DRM master:** only one process can hold it at a time. Run from a real VT
(not an SSH session) or nothing will appear. Plan to handle VT switching and to hide the
kernel console text cursor over your output.

---

## 3. Phase 0 — Bring-up spike (do this before writing any engine code)

**This phase splits into two independent gates with wildly different risk profiles.**
Verified against mainline Linux `v7.3-rc1` and the Armbian build tree.

### Gate 0A — GPU (low risk, works on stock Arch today)

The Mali GPU is **fully upstream**. `arch/arm64/boot/dts/allwinner/sun50i-h616.dtsi`
carries the node, and the Zero 2W board file already wires the regulator and enables it:

```
gpu: gpu@1800000 {                              /* sun50i-h616.dtsi */
    compatible = "allwinner,sun50i-h616-mali",
                 "arm,mali-bifrost";
    power-domains = <&prcm_ppu 2>;
    status = "disabled";
};

&gpu {                                          /* sun50i-h618-orangepi-zero2w.dts */
    mali-supply = <&reg_dcdc1>;
    status = "okay";
};
```

Nothing to patch. Enable `CONFIG_DRM_PANFROST` and the sunxi PRCM power-domain driver
(the GPU sits behind `prcm_ppu` domain 2 — without it the GPU never powers on).

Check: `/dev/dri/renderD128` exists · `dmesg | grep panfrost` shows the probe ·
`glmark2-es2 --off-screen` produces a score. **All of this works headless, with no display
whatsoever.** You can benchmark the GPU before HDMI ever lights up.

### Gate 0B — Display (the real risk)

**H616/H618 display output is not in mainline.** As of `v7.3-rc1`, `sun50i-h616.dtsi`
contains no display-engine, mixer, TCON or HDMI nodes at all, and
`drivers/gpu/drm/sun4i/` contains no DE33 code — the Kconfig still tops out at
Display Engine 2.0. This is not "might be flaky." It is absent.

Armbian carries it out-of-tree as a curated patch series:

| Armbian branch | Kernel | DRM patch set |
|---|---|---|
| `current` *(their CI-tested target)* | 6.18 | 43 patches, `patch/kernel/archive/sunxi-6.18/patches.drm` |
| `edge` | 7.1 | 24 patches, `patch/kernel/archive/sunxi-7.1/patches.drm` |

The load-bearing ones: `drm-sun4i-Add-support-for-H616-HDMI-PHY`,
`drm-sun4i-Add-H616-TCON-TV-support`, `drm-sun4i-Add-planes-driver`,
`arm64-dts-allwinner-h616-Add-display-pipeline`, and
`arm64-dts-allwinner-h616-Enable-HDMI-on-several-boards`.

Check: `modetest -c` shows a connected HDMI connector with modes ·
`modetest -s <conn>:<mode>` puts a test pattern up · `kmscube` renders.

**Exit criterion:** a photo of kmscube on your panel, plus a glmark2 score in `docs/`.

### Mitigation: vendor the patch stack, don't wait for upstream

> **Implemented.** See [`image/kernel/`](image/kernel/) — 44 patches pinned to
> `armbian/build@6d07521`, kernel 6.18.49, verified to apply with zero fuzz,
> plus `0100-tactipalm-*` which adds the Zero 2W HDMI enablement Armbian omits.

Copy Armbian's `patches.drm` series into `image/kernel/patches/`, pin the kernel version,
and have your Arch `PKGBUILD` apply them. This makes the out-of-tree delta an explicit,
versioned, reviewable input to your build. As patches land upstream you delete files from
the directory — never a rewrite, just a shrinking stack. Start from **`current` / 6.18**,
because the board config sets `KERNEL_TEST_TARGET="current"`, so that is the combination
Armbian actually tests.

Note the board is registered as `orangepizero2w.csc` — *Community Supported
Configuration*, Armbian's lowest support tier. It works, but nobody owes you a fix.

### There is no simplefb escape hatch — correcting an earlier claim

An earlier draft of this plan said U-Boot could initialise HDMI and hand the
kernel a `simple-framebuffer`. **That is wrong on the H616.** U-Boot's
`drivers/video/sunxi/` implements DE2 only, contains no H616 references, and our
build enables no video options. U-Boot cannot light the panel.

The consequence: nothing appears on HDMI until our own kernel driver binds, and
there is no fallback framebuffer to fall back to.

### Reading a failed boot without a UART adapter

The image now sets `Storage=persistent` in journald, so the log survives a
reboot. If the board boots but shows nothing, power it off, move the card to a
PC, and read `var/log/journal/` on the root partition.

This captures everything from the point the root filesystem mounts read-write.
It does **not** capture a U-Boot failure or a kernel panic before mount — for
those, only a serial console works.

## 4. Phase 1 — The Arch image

Goal: a reproducible, scripted build that outputs a flashable `.img`. Script it from day one
— you will reflash many times.

- **U-Boot** — mainline U-Boot has `orangepi_zero2w_defconfig`. Build the SPL + U-Boot
  and `dd` it to the correct offset on the SD card.
- **Rootfs** — `ArchLinuxARM-aarch64-latest.tar.gz` from archlinuxarm.org.
- **Kernel** — a `PKGBUILD` wrapping whichever tree Phase 0 proved. Owning this as a
  package (not a hand-copied `Image` file) is what keeps the system upgradeable.
- **Partitioning** — FAT32 boot + ext4 root. Consider mounting root read-only later for
  power-loss resilience; an appliance that corrupts its SD card on unplug is a bad appliance.
- **Strip it** — no X, no Wayland, no display manager, no desktop packages. You need
  `mesa` (for Panfrost), `libdrm`, `libgbm`, and that is nearly it.
- **Deliverable:** `build-image.sh` in the repo, and a written note of the exact package
  set. Put this in git before you write a line of renderer code.

---

## 5. Phase 2 — Renderer skeleton ("triangle on HDMI")

The smallest thing that proves the platform layer. Roughly:

- Open `/dev/dri/card0`, enumerate resources, pick the connected connector, its preferred
  mode, and a compatible CRTC + encoder.
- Create a `gbm_device` and a `gbm_surface` matching the mode, format `XRGB8888`.
- `eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, ...)`, create a GLES 3 context, make it current.
- Render → `eglSwapBuffers` → lock the GBM front buffer → wrap it in a DRM framebuffer →
  `drmModePageFlip` → `poll()` the DRM fd for the flip event → release the previous buffer.
- Handle `SIGINT`/`SIGTERM`: restore the original CRTC mode and the VT. A renderer that
  leaves the console black when it crashes will waste hours of your life.

**Develop this on your desktop, not the Pi.** The DRM → GBM → EGL path is standard Mesa,
not board-specific: the identical code runs from a TTY (Ctrl+Alt+F3) on any Intel, AMD or
nouveau machine. Doing so removes the H618 display risk from the critical path of every
phase except final integration. This is the highest-leverage decision in the plan and it
costs nothing.

Use **legacy modesetting** first. Atomic KMS is better but is a distraction until you have
pixels. Double-buffer first, then measure whether triple-buffering helps.

**Exit criterion:** a spinning triangle, locked to vsync, stable frame times, and you know
its RSS.

---

## 6. Phase 3 — Asset pipeline

Do **not** parse glTF on the Pi. Base64 decoding, PNG decompression, and buffer
de-interleaving on a Cortex-A53 reading from microSD is exactly the wrong workload.

Build `tpmconv`, an **offline converter that runs on your PC**, not on the board:

```
model.glb  →  tpmconv  →  model.tpm
```

`tpmconv` uses `cgltf` to read the file and emits a binary blob that the Pi can essentially
`mmap` and use directly:

- interleaved, pre-transformed vertex buffers in final GPU layout
- quantised bone indices/weights (`u8`/`unorm8`)
- animation tracks pre-sorted by time, converted to a fixed sample layout
- textures as **KTX2 with ASTC or ETC2** — Mali-G31 handles both; shipping PNG means
  decompressing on-device *and* burning 4× the GPU memory bandwidth forever after
- a header with everything needed for one-pass loading, no pointer patching

**Content budget** (start here, measure, then adjust):
20k triangles · ≤64 bones · one 1024² albedo · 2–3 materials · no per-frame allocations.

Write down the Blender export conventions as a checklist in the repo — axis convention,
scale in metres, single skin, baked animation, no non-uniform bone scale. Future-you will
break one of these and spend an evening on it.

---

## 7. Phase 4 — Skinning, animation, and the look

**Target GLES 3.0, not 3.1.** Compute shaders and SSBOs are where Panfrost driver bugs
concentrate, and skinned character rendering needs none of it. UBOs and vertex-shader
skinning keep you on the well-trodden path.

**Skin on the GPU.** The A53 cores are weak and you want them free. Put the bone palette in
a UBO (64 × `mat4` = 4 KB; use `mat4x3` if you want it tighter) and skin in the vertex shader.

**Animation system**, in the order you need it:
1. A sampler that evaluates one clip at time *t* — linear for translation/scale, slerp for
   rotation.
2. Cross-fade between two clips. This alone makes it look alive.
3. *Design for, but do not yet build:* additive layers for blink and look-at. Getting the
   architecture right now costs nothing; retrofitting it later costs a rewrite.

**Lighting — resist PBR + IBL.** On a G31 the naive PBR path is a poor trade. A stylised
look is both cheaper and more likely to look deliberate:

- one directional light, half-lambert wrap
- a matcap or a baked ramp texture for the shading response
- a rim/fresnel term — cheap, and does most of the visual work
- **no real-time shadow maps at first.** A blob shadow projected on the ground is fine.
- **no post-processing.** Every full-screen pass is pure bandwidth cost.
- **do** measure 4× MSAA — on a tile-based GPU it resolves in tile memory and is often
  much cheaper than you would expect. It may be your best quality-per-watt buy.

---

## 8. Phase 5 — App shell and the extension seam

Baseline behaviour is "load character, play idle loop." But you said features come later, so
put the seam in now — it is cheap now and expensive in six months.

```
tactipalm [--model char.tpm] [--scene scene.toml] [--res 1280x720] [--fps-cap 60]
```

Three small subsystems, each behind a narrow interface:

- **`tp_input`** — an abstract event source. Implementations: stdin, evdev, GPIO, unix socket.
  The renderer never knows which one is live.
- **`tp_state`** — a behaviour state machine that owns "what should the character be doing"
  and emits animation blend weights. The renderer consumes weights and nothing else.
- **`tp_ctl`** — a unix socket accepting simple text commands (`play wave`,
  `set expr happy`, `look 0.3 -0.1`).

That socket is the whole point. Voice input, an LLM, sensors, a web UI — every future
feature becomes a separate process that writes lines to a socket, and the renderer stays a
renderer. This is the single most valuable architectural decision in the plan.

Optionally add a systemd unit for boot-to-character mode, but keep CLI launch as the
primary path since that is what you asked for.

---

## 9. Phase 6 — Performance and polish

- **Instrument frame time** with `GL_EXT_disjoint_timer_query`. Log a rolling p99, not an
  average — stutter is what people notice.
- **Watch thermals** via `/sys/class/thermal/thermal_zone*/temp` and log when the governor
  throttles. Run a 30-minute soak test; the frame rate at minute 30 is the real frame rate.
- **Boot time** — target under 10 seconds from power to first frame if you ever go
  boot-to-app. `systemd-analyze blame` will show you the offenders.
- **Memory** — hold RSS under ~150 MB so the 1 GB board variant is comfortable.

---

## 10. Repository layout

```
tactipalm/
├── PLAN.md
├── image/          build-image.sh, PKGBUILDs, U-Boot config, package list
├── src/
│   ├── platform/   drm_kms.c, gbm_egl.c, vt.c, signal handling
│   ├── gfx/        shader.c, mesh.c, texture.c, render pass
│   ├── anim/       sampler, blend tree, bone palette
│   ├── app/        tp_input, tp_state, tp_ctl, main.c
│   └── shaders/
├── tools/tpmconv/  offline glTF → .tpm converter (host build)
├── assets/         .blend sources + export checklist
└── docs/           bring-up notes, glmark2 numbers, thermal logs
```

Two build targets from the start: **host** (for `tpmconv`) and **target** (aarch64).
Cross-compile rather than building on the Pi — a full build on a Zero 2W is slow enough to
break your flow.

---

## 11. Risks, honestly ranked

| Risk | Severity | Mitigation |
|---|---|---|
| H618 display stack absent from mainline (**confirmed**, v7.3-rc1) | **Blocking** | Vendor Armbian's `sunxi-6.18/patches.drm` into your PKGBUILD; simplefb escape hatch |
| Panfrost / GPU support | ~~High~~ **Low** | Resolved — GPU node is upstream and enabled in the Zero 2W DTS. Verify headless in Gate 0A |
| Armbian patch series breaks on kernel rebase | Medium | Pin the kernel version; treat the patch stack as versioned build input |
| Thermal throttling under sustained load | Medium | Heatsink; 30-min soak test in Phase 6; cap frame rate |
| microSD I/O stalls on asset load | Medium | Offline `.tpm` format; load once at startup, never stream |
| Scope creep from "features later" | Medium | The `tp_ctl` socket boundary — new features are new processes |
| 1 GB RAM variant too tight | Low | Budget RSS < 150 MB; prefer a 2 GB board |

---

## 12. What to do first

1. Order the USB-UART adapter and a heatsink. Everything waits on serial console.
2. Flash **Armbian** (not Arch) and confirm the board works, HDMI lights up, and you can
   get a `kmscube` + `glmark2-es2-drm` result. Armbian is the fast path to a known-good
   baseline — you are using it as a measuring instrument, not as the final OS.
3. Record the glmark2 score and the working kernel version in `docs/`.
4. *Then* start Phase 1, porting that known-good kernel configuration onto Arch Linux ARM.

Proving the hardware on someone else's working image first, then porting to Arch, is
dramatically faster than debugging a custom Arch image and an unproven display stack at the
same time.
