# OS compatibility — Orange Pi Zero 2W on Arch Linux ARM

Status of every board peripheral under Arch Linux ARM with kernel 6.18.49 plus
the vendored display stack. Derived from the mainline device tree
(`sun50i-h618-orangepi-zero2w.dts`, `sun50i-h616.dtsi`) and the Armbian patch
series. **Nothing here has been booted yet** — this is a paper audit, and the
"verified" column says so honestly.

## Distro choice — settled

Use **Arch Linux ARM** (`archlinuxarm.org`) generic aarch64.

Do *not* use `ports.archlinux.page`. That project builds for ARMv8.2+ only; the
H618's Cortex-A53 is ARMv8.0-A and those packages will not run. This is the
single most likely way to waste a day on this project.

## Peripheral matrix

| Peripheral | DT node | Status | Verified |
|---|---|---|---|
| CPU + cpufreq | `&cpu0` + `sun50i-h616-cpu-opp` | Mainline | paper |
| Mali-G31 / Panfrost | `&gpu` | **Mainline, enabled for this board.** Needs `CONFIG_SUN50I_H6_PRCM_PPU` | paper |
| HDMI / DE33 | *(patched in)* | **Out-of-tree.** 44 vendored patches, incl. ours for this board | patches apply cleanly |
| microSD | `&mmc0` | Mainline | paper |
| Serial console | `&uart0`, `uart0_ph_pins` | Mainline. GPIO header, 3.3 V, 115200 | paper |
| USB-C (OTG) | `&usbotg`, `dr_mode = "peripheral"` | Mainline. **Gadget only** — see below | paper |
| USB host | `&ehci1` / `&ohci1` | Mainline. On the FPC/expansion connector | paper |
| SPI | `&spi0` | Mainline. Relevant only if an LCD is ever added | paper |
| Audio codec | `&codec` | Mainline | paper |
| Thermal | `sun8i-thermal`, `gpu-thermal` zone | Mainline | paper |
| **WiFi / Bluetooth** | *(absent)* | **Broken. No mainline support.** | — |

## WiFi: don't fight it

The Zero 2W carries an **AW859A** WiFi/BT combo — a Spreadtrum **UWE5622**.
There is no mainline driver. The board DTS does not even describe it: there is
no `&mmc1` or WiFi node in `sun50i-h618-orangepi-zero2w.dts` at all.

Armbian makes it work with an out-of-tree stack — an address-management driver
patch, a `sprdbt_tty` module, `aw859a-wifi`/`aw859a-bluetooth` systemd units,
and a prebuilt `hciattach_opi` binary blob for Bluetooth attach. Porting that
onto Arch with a mainline 6.18 kernel is a project in itself, and it buys this
appliance nothing.

**Decision: no WiFi.** Tactipalm does not need it.

## Networking: USB gadget ethernet

The mainline DTS pins the USB-C port to peripheral mode, and explains why:

> both CC pins are pulled to GND … the VBUS pins power the device, so a fixed
> peripheral mode is the best choice

That is not a limitation here, it is the answer. The single cable that powers
the board also carries the network. Bring up `usb_f_ecm` (or `f_rndis` for
Windows hosts) via configfs, and the board appears as a USB ethernet device on
your development machine — SSH, `scp`, `rsync` of new builds, all over the
existing power cable. Zero extra hardware.

Fallbacks if you ever need real networking:

- A USB ethernet dongle on the `&ehci1` host port via the FPC/expansion board.
- Powering over GPIO instead of USB-C, which frees port 0 for host mode — but
  this requires a DT change (`dr_mode = "host"`, enable `ohci0`/`ehci0`) and
  costs you the gadget link.

## Consequences for the image

1. **The image must be self-sufficient at first boot.** With no WiFi and no
   Ethernet, there is no "just `pacman -S` it on the device" step. Every package
   the renderer needs has to be in the image before it ships to the card.
2. **Serial console is mandatory**, not optional. It is the only channel that
   works before USB gadget networking comes up.
3. **The gadget link is the dev loop.** Getting `usb_f_ecm` up early pays for
   itself immediately.

## Hardware validation, 2026-09-05

The board was booted with the stock Orange Pi OS card. This eliminated every
hardware variable before our own image was tried.

| Check | Result |
|---|---|
| Board boots from microSD | Pass |
| DRM device created | `card0` present |
| HDMI connector exposed | `card0-HDMI-A-1` present |
| Connector status | **connected** |
| EDID and mode list | present |
| GPU render node | **absent** — no `renderD128` |

Two conclusions.

**The display hardware works.** Board, HDMI cable, monitor and EDID are all
good. Any black screen from our image is therefore a software fault in our
kernel or device tree, not a wiring or panel problem.

**The vendor kernel has no working GPU.** `renderD128` does not exist, which
matches the known state of the official Orange Pi images: the Mali driver does
not load. Our image should be *better* than the vendor one here — mainline
already carries the Mali node and enables it for this board, so we get Panfrost
that Orange Pi OS does not have. We are adding display support to a kernel that
already has the GPU; the vendor did the reverse.
