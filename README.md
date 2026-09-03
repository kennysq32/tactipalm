# Tactipalm

A 3D character renderer for the Orange Pi Zero 2W. The device runs Arch Linux
ARM with no graphical desktop. You start the renderer from the command line.
It draws directly to the HDMI output through DRM/KMS.

## Two tracks

This project is split across two work streams. They share only one document.

| Track | Owns | Delivers |
|---|---|---|
| System | `image/`, `docs/` | A bootable Arch image with a working `/dev/dri/card0` |
| Renderer | `src/`, `tools/` | The `tactipalm` binary and the asset pipeline |

The contract between them is [`docs/CONTRACT.md`](docs/CONTRACT.md). Read it
before you write renderer code.

## Documents

| File | Content |
|---|---|
| [PLAN.md](PLAN.md) | The full project plan and the phase order |
| [docs/CONTRACT.md](docs/CONTRACT.md) | What the system track guarantees to the renderer track |
| [docs/OS-COMPATIBILITY.md](docs/OS-COMPATIBILITY.md) | Peripheral support, and why there is no WiFi |
| [docs/FEL.md](docs/FEL.md) | USB recovery mode, and why it does not work on this board |
| [image/README.md](image/README.md) | How to build the image |
| [image/kernel/README.md](image/kernel/README.md) | The out-of-tree display patches |

## Status

| Item | State |
|---|---|
| Kernel patch series | 44 patches, apply with no fuzz |
| Kernel build | **Builds.** `Image`, modules and DTB produced |
| Device tree | **Verified.** Display engine, HDMI and connector all enabled |
| U-Boot | Not built yet |
| SD image | Not built yet |
| Boot on hardware | Not tested. No serial adapter yet |

## Hardware notes

- The board has two USB-C sockets. Only one carries data.
- USB FEL mode enumerates but does not answer commands. Use an SD card.
- There is no usable WiFi. Networking is USB gadget ethernet.
- You need a 3.3 V UART adapter. Without it you cannot debug a failed boot.
