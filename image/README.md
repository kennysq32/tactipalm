# Image build — Arch Linux ARM for Orange Pi Zero 2W

Three stages, in order. Each is independently runnable and cached.

```bash
./build-uboot.sh          # TF-A bl31 + U-Boot        -> out/u-boot-sunxi-with-spl.bin
./kernel/build-cross.sh   # patched 6.18.49           -> kernel/out/{Image,dtb,modules}
sudo ./build-image.sh     # assemble + partition      -> out/tactipalm-YYYYMMDD.img
```

Host deps (Debian/Kali/Ubuntu):

```bash
sudo apt install gcc-aarch64-linux-gnu bc bison flex swig libssl-dev libelf-dev \
                 python3-dev python3-setuptools device-tree-compiler \
                 parted e2fsprogs libarchive-tools curl git
```

## Layout decisions

| Decision | Why |
|---|---|
| Single ext4 partition at 1 MiB, `/boot` inside | sunxi SPL lives at 8 KiB; 1 MiB clears it. U-Boot reads ext4, so a separate FAT partition buys nothing |
| `extlinux.conf`, not `boot.scr` | U-Boot distro boot is declarative and easy to edit on the card |
| `LABEL=TACTIPALM` for root | survives the card appearing as a different `/dev/sdX` |
| `bsdtar` for the rootfs | ALARM tarballs carry ownership and xattrs that GNU tar mangles |
| TF-A `lts-v2.12.9` + U-Boot `v2026.07` | matches Armbian's pins for this board — a known-good pair |
| `cma=64M` on the kernel cmdline | the display engine needs contiguous scanout buffers |

## Console and network

Serial console on the GPIO header, **115200 8N1**, 3.3 V. This is the only
channel that works before anything else, and the board has no usable WiFi
(see `../docs/OS-COMPATIBILITY.md`).

Networking is USB gadget ethernet over the same USB-C cable that powers the
board. Board takes `10.42.0.2/24` on `usb0`; give your host `10.42.0.1/24` on
its matching CDC-ECM interface.

## Verification status

- Patch series: **verified** — 44 patches apply to pristine 6.18.49, zero fuzz.
- Scripts: syntax-checked only. **Nothing has been built or booted yet** —
  no board in hand at time of writing.
- The first real run will surface problems. Expect to iterate on
  `config.fragment` and the U-Boot pin.
