# FEL mode on the Orange Pi Zero 2W

FEL is the Allwinner BROM's USB recovery mode. It lets you push SPL, U-Boot and
a kernel straight into DRAM over the USB-C cable — no SD card, no flash cycle.
For display bring-up that turns the loop from *build → flash → boot* into
*push → boot*.

## There is no FEL button, and none is needed

The H618 boot ROM walks a fixed chain and takes the first thing that responds:

```
microSD (mmc0)  →  eMMC (mmc2)  →  SPI NOR  →  FEL
```

The Zero 2W has **no eMMC**, so the SD card is normally the only thing in the
chain. Empty the slot and the BROM runs out of options and drops into FEL by
itself. The button other boards carry exists to *force* FEL when there is
bootable media present; remove the media and you get the same result.

**So: power off, pull the microSD, plug in, done.**

### Caveat: the SPI NOR link in the chain

The Zero 2W spec lists **16 MB of onboard SPI NOR flash**. If yours is populated
*and* carries a bootloader, pulling the SD card does not reach FEL — the BROM
finds the SPI flash first and boots that instead. Boards generally ship with it
blank, in which case the BROM skips it, but it is worth knowing about when
"remove the card" does not produce `1f3a:efe8`.

To force past a programmed SPI NOR, hold the flash's CLK or CS pin to ground
while powering on so the BROM's read fails. Only reach for this after the
simpler causes below are ruled out.

### Two USB-C ports — and only one of them does data

The board has **two Type-C sockets**: one power-in, one USB 2.0 OTG. They are
physically identical, so plugging the data cable into the power socket produces
exactly the same silence as a charge-only cable.

Powering through the dedicated power socket while using the OTG socket for data
is the right configuration: it lifts the 500 mA host-port ceiling without
putting a hub in the data path.

If FEL stops appearing after re-cabling, the first thing to check is that the
data cable went back into the socket that produced `1f3a:efe8` before.

## Procedure

1. Unplug power.
2. **Remove the microSD card.** This is the whole trick.
3. Connect the USB-C port to the host with a **data-capable** cable.
   Charge-only cables are the single most common failure here.
4. Confirm on the host:

```bash
lsusb | grep 1f3a
```

Expect `ID 1f3a:efe8 Allwinner Technology sunxi SoC OTG connector in FEL/flashing mode`.

## Host setup

```bash
sudo apt install sunxi-tools
```

The packaged version on this machine is `1.4.2+git20260530.d7bbd1-1` — a recent
git snapshot, new enough for H616/H618. (The usual advice to build sunxi-tools
from source is aimed at older distro packages; it does not apply here.)

Non-root access, so you are not running flashing tools under sudo:

```bash
sudo tee /etc/udev/rules.d/99-sunxi-fel.rules >/dev/null <<'RULE'
SUBSYSTEM=="usb", ATTR{idVendor}=="1f3a", ATTR{idProduct}=="efe8", MODE="0660", TAG+="uaccess"
RULE
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### ModemManager will break FEL

ModemManager probes unknown USB devices. On this hardware it produces a hard
**3.10 s** FEL window followed by a disconnect, and if it writes to the
interface while `sunxi-fel` is reading, the AW protocol desyncs:

```
sunxi-fel: fel_lib.c:158: aw_read_usb_response:
           Assertion `strcmp(buf, "AWUS") == 0' failed.
```

Tell it to leave the device alone — same rule file, extra line:

```bash
sudo tee /etc/udev/rules.d/99-sunxi-fel.rules >/dev/null <<'RULE'
SUBSYSTEM=="usb", ATTR{idVendor}=="1f3a", ATTR{idProduct}=="efe8", \
  MODE="0660", TAG+="uaccess", ENV{ID_MM_DEVICE_IGNORE}="1"
RULE
sudo udevadm control --reload-rules && sudo udevadm trigger
```

For a quick one-off test instead: `sudo systemctl stop ModemManager`.

Verify:

```bash
sunxi-fel version
```

Expect `soc=00001823(H616)`. The H618 reports as H616 — same die family, that is
correct, not a mismatch.

## Booting something

```bash
sunxi-fel -v -p uboot image/out/u-boot-sunxi-with-spl.bin \
  write 0x45000000 image/kernel/out/Image \
  write 0x4FA00000 image/kernel/out/sun50i-h618-orangepi-zero2w.dtb
```

`uboot` is the important verb: it runs the SPL first to initialise DRAM, then
hands off to U-Boot. Loading a kernel without DRAM init will not work.

## Power: the FEL window may be a brownout, not a timeout

The Zero 2W is specified at **5 V / 2 A**. Its single Type-C port carries both
power and data, and it has no PD controller — both CC pins are tied to GND, so
it cannot negotiate anything. It draws what it can get.

A laptop USB 2.0 port offers **500 mA** by default; USB 3.x offers 900 mA. In
FEL the SoC runs from internal SRAM with DRAM uninitialised and no display, so
it may squeak by. **Booting Linux with HDMI up will not** — SoC, DRAM and the
display engine together comfortably exceed 500 mA.

Symptoms of under-powering, all of which look like unrelated bugs:

- a short, repeatable window before the board resets
- inconsistent HDMI output across reboots — sometimes a picture, sometimes not
- boots that get further on some attempts than others

### Powering it properly

Options, best first:

1. **Use both Type-C sockets** — a 2 A+ supply into the power socket, the host
   into the OTG socket. Cleanest: full current, no hub in the data path.
2. **Feed 5 V into the GPIO header** (pins 2/4 = 5 V, pin 6 = GND) from a
   regulated 2 A+ supply. Mainline's own DTS comment acknowledges GPIO powering
   as a supported configuration. No protection on this path — use a clean supply.
3. **A self-powered USB hub.** Different from the bus-powered hub that caused
   protocol trouble earlier, but it does put a hub back in the data path.

## Diagnosis on this board: FEL enumerates but does not serve commands

Established by experiment, so nobody repeats this:

| Hypothesis | Verdict |
|---|---|
| Charge-only cable | **Was** the original cause of total silence. Fixed. |
| Wrong Type-C socket | Real — the board has two, one is power-only. |
| ModemManager interference | Ruled out. Stopped it; behaviour identical. |
| USB hub in the path | Ruled out. Direct root port; behaviour identical. |
| Insufficient bus power | Ruled out for this symptom. Dedicated 5 V supply; window unchanged. |
| udev permissions | Fixed via `GROUP="plugdev"` (the `uaccess` ACL lands too late for a ~3 s window). |
| sunxi-tools too old | Ruled out. Debian's package is upstream HEAD `d7bbd17` verbatim. |

What actually happens, from the core dump of the failing run:

- Our request is well-formed — magic `AWUC`, tag 0, length 16, request `0x0012`
  (`AW_USB_WRITE`).
- `usb_bulk_recv` returns **success** with 13 bytes, so this is not a transfer
  error — those exit loudly via `usb_error()`, not via the assertion.
- Those 13 bytes are **all zero**. `strcmp(buf, "AWUS")` therefore compares an
  empty string against the magic and fails.
- The FEL window is a hard **2.94 s**, repeatable to 10 ms across every
  configuration tried.

So the BROM enumerates as `1f3a:efe8`, accepts the request, answers with nulls,
and drops off. It is not servicing the classic FEL command protocol. Newer
Allwinner parts in the H616 family have a secure/TOC0 FEL variant that does not
speak this protocol; `sunxi-tools` tracks that as an open issue.

## Recommendation: do not block on FEL

FEL was only ever a convenience for shortening the bring-up loop. It is **not**
on the critical path — the SD card route needs no FEL, has no 3-second timer,
and is what the product ships on regardless.

The useful outcome of this exercise is what it proved: the board is alive, its
BROM runs, the USB path works, and the host toolchain is correctly set up.
That is enough. Build U-Boot and the kernel, write an SD card, and bring the
board up on serial console.

## The catch

**FEL gives you no output.** You can push code into the board and start it, but
without a display driver (which is exactly what we are trying to prove) and
without a serial console, you cannot see whether anything happened, and you
cannot type at the U-Boot prompt to boot the kernel you just pushed.

FEL is a fast *delivery* mechanism. It is not a substitute for the UART adapter.
Serial console on the GPIO header — 115200 8N1, 3.3 V — remains the first
hardware dependency of this project.
