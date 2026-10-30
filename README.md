# HakiOS for MediaTek: the Infinix Hot 20 (X6826B)

**This is the branch `mtk`**: HakiOS -- Apple's open-source Darwin kernel (XNU, xnu-8796.121.2) on Android phones -- for a
phone with a **MediaTek** SoC, the **Infinix Hot 20 (X6826B)**: Helio G85 (MT6768), Mali-G52 MC2, eMMC, 6 GB of RAM (the kernel
is given 4). The kernel, its MediaTek drivers, the loader that starts it from the phone's boot partition and the scripts that run
it are here; the phone's own configuration is its device repository, [hakios-mt6768](https://github.com/oosexgang/hakios-mt6768).
The Qualcomm phone (POCO X5 Pro 5G) and QEMU are the branch [`main`](https://github.com/oosexgang/hakios-xnu-kernel/tree/main).

**Documentation: [mtk/README.md](mtk/README.md)** (the MediaTek part), [docs/devices/x6826b.md](docs/devices/x6826b.md) (the
phone), [docs/](docs/index.md) (the common part, written for the Qualcomm phone).

## What works

As seen on the phone, run by run (the device's `device.conf`, and the commits of the port):

| | Infinix Hot 20 (`x6826b`) | How |
|---|---|---|
| Boot | yes | LK starts `boot_a`; `mtkloader` in front of Android's kernel starts XNU once when Android asks, Android comes back after it |
| Lock screen, home screen, widgets, the stock apps, iOS 16's own wallpaper | yes | with the earlier user space (below) |
| Display | yes | 720 × 1640, the iPhone X's 750 × 1624 at scale 2, one pixel to a pixel (`fbscale=2 fbfit=3`), flipped not copied (`fb_flip=1`) |
| Touch, multitouch | yes | OmniVision TD4160 on SPI2 (`TCM`, `touchscreen=2`); the loader sends its firmware |
| Buttons | yes | power and volume (`MTK_KEYS`, `buttons=2`) |
| CPU | yes | 4 cores (the two Cortex-A75 among them), the big ones at speed (`mtk_cpufreq=3`) |
| GPU | yes | Mali-G52 MC2 brought up by the kernel (`MALI`, `mali=1`, 800 MHz), Mesa's panfrost and a Metal driver: CoreAnimation draws through Metal |
| Storage | read only | the system image in the partition `xnusys`, read off the eMMC as it is needed (`MTK_EMMC`, `emmc_root=1`): ~1 GB of RAM free |
| Sensors | partly | the accelerometer through the SCP's sensor hub, the screen turns (`MTK_SCP`, `scp_step=7`) |
| Wi-Fi, Bluetooth | partly | CONNSYS powered, firmware in, BTIF, STP and WMT answer; the MCU's patch by PDA is being brought up; no network yet (`MTK_CONNSYS`, `connsys_step=N`) |
| Audio, battery, vibration, cameras, cellular | no | |
| Apple's services | no | not a goal |

## The drivers

The MT6768's layer, [mtk/mt6768/](mtk/mt6768/): every driver is a `CONFIG_HAKIOS_*` symbol, `default n`, and does nothing without
its boot-argument; `configs/mt6768.config` puts them in and leaves every Qualcomm and QEMU driver out.

| Driver | Symbol | Boot-argument | What |
|---|---|---|---|
| `drivers/storage/hakios.mtk_emmc.c` | `MTK_EMMC` | `emmc_root=1` | the eMMC through MSDC0, read only; `xnusys` as `/dev/emmc0` and the root |
| `drivers/input/hakios.tcm.c` | `TCM` | `touchscreen=2` | the TD4160 touch screen (TCM protocol) on SPI2 |
| `drivers/input/hakios.mtk_keys.c` | `MTK_KEYS` | `buttons=2`, `mtk_cpufreq=`, `mali_mhz=` | the keypad and the PMIC's power key; the big cores' and the Mali's clocks |
| `drivers/input/hakios.mtk_scp.c` | `MTK_SCP` | `scp_step=N` | the SCP sensor hub: the accelerometer, the way up |
| `drivers/gpu/hakios.mali.c`, `hakios.mali.cpp` | `MALI` | `mali=1` | the Mali-G52: power, clocks, MMU, a job queue; `HakiosMali`, panfrost's DRM interface for IOKit |
| `drivers/wifi/hakios.mtk_connsys.c` | `MTK_CONNSYS` | `connsys_step=N` | CONNSYS (Wi-Fi/Bluetooth) step by step: power, firmware, BTIF, STP, WMT, the patch by PDA |

## Quick start

What it needs: a Mac; an Infinix Hot 20 (X6826B) with its bootloader unlocked and Android rooted (Magisk), **its `boot_a` saved**
(`backup/boot_a_magisk.img`); `adb` and `fastboot`; your own Darwin userland 16.5 IPSW for `xnuloader/identity.h`.

```sh
git clone -b mtk https://github.com/oosexgang/hakios-xnu-kernel.git hakios && cd hakios
tools/hakios-device.py sync x6826b    # the phone's repository into devices/x6826b
devices/x6826b/pull-firmware.sh       # the touch controller's and CONNSYS's firmware off your phone into fw-*/
xnuloader/mkidentity.sh your.ipsw     # xnuloader/identity.h, from your IPSW (nothing of Apple's is in the tree)
make x6826b_defconfig                 # common_defconfig + mt6768.config + x6826b.config
make kernel                           # hakios-kernel: mali mtk_keys mtk_scp tcm mtk_emmc mtk_connsys built in
make boot-image RAMDISK=...           # xnu-flashed-slot_a.img: mtkloader + Android's kernel + the kernel and its RAM disk
export HAKIOS_SERIAL=...              # your phone, as `adb devices` shows it
make phone SECS=300 RAMDISK=...       # one run: boot_a written, XNU once, Android back, the log in logs/
```

`make phone` writes `boot_a` and nothing else; `make system-flash` writes the system image into the partition `xnusys`, which you
cut from the end of `userdata` once (`mtk/tools/gpt-carve.py`). Slot B and every other partition are never written. A run ends after
`SECS`: the kernel hands the phone back to `mtkloader`, which keeps the log in pstore and starts Android again; the log
(`/sys/fs/pstore/console-ramoops-0`, and what the kernel kept for the host in the pmsg zone) lands in `logs/`. A loader that never
comes back costs a power cycle; the way back from a broken `boot_a` is mtkclient in BROM mode with your saved image.

## Not here yet

- **The user space for this phone.** `make images` is the common tree's, for the Qualcomm phone: it has no Mali in it (Mesa's
  panfrost over `HakiosMali`, the Metal driver). The user space that draws the home screen above is the project's earlier one,
  which reads the former names (`kern.redwood.*`, `RedwoodMali`, `/dev/redwood-touch`). With this tree's kernel it starts and
  draws the home screen on the processor (runs 359 and 360, 30 October: "hakios-kernel: 6 drivers built in", "BSD root: emmc0",
  "mali: came up to step 6 of 6", the touch controller up, the home screen with its widgets and wallpaper in the snapshot); its
  touch and its Metal driver look for the devices under their former names, which this kernel does not have. Moving it to the
  `hakios` names is the next step; until then `RAMDISK=` and the system image in `xnusys` are that user space's.
- `mtkloader` (`mtk/loader/`) knows this phone's panel and touch controller; another MT6768 phone changes those.
- HFS Plus, the common tree's data volume, is not used on this phone: `/private/var` lives in RAM for a run.

## The three layers

1. **The open core**: the kernel port, the loader, the drivers, the tools. All source, in these repositories.
2. **What you put on top**: the kernel can start a user space that you supply. It was built so that it can start the Darwin userland 16.5
   user space, which you lay out yourself from files you download from Apple; nothing of it is shipped, and nothing is
   circumvented to get it. Whether you may do that, and whether you want to, is your decision.
3. **Apple's services** (activation, Apple ID, iCloud, the App Store): not supported and not a goal.

Unlike the Qualcomm phone, which boots from RAM, this phone's loader lives in `boot_a` (in front of Android's own kernel, which it
starts unless Android asked for XNU) and the system image in a partition of its own (`xnusys`). This is a research bench, not a ROM.

## Repositories

| Repository | What |
|---|---|
| [hakios-xnu-kernel](https://github.com/oosexgang/hakios-xnu-kernel/tree/mtk), branch `mtk` | this: the kernel port, the MediaTek layer (`mtk/`), the loader, the tools |
| [hakios-mt6768](https://github.com/oosexgang/hakios-mt6768) | the Infinix Hot 20: its `device.conf`, `configs/x6826b_defconfig`, firmware script, the bring-up notes |
| [hakios-xnu-kernel](https://github.com/oosexgang/hakios-xnu-kernel/tree/main), branch `main` | the same for the POCO X5 Pro 5G (Qualcomm) and QEMU |

## What is in the repository, and what never is

In it: only our own code and data: the patch to Apple's open-source XNU (`xnu/hakios.patch`, APSL 2.0) and our files, the boot
loaders, our programs and libraries, the tools, and the device and SoC directories.

Never in it: any file of Darwin (you build the system image on your machine from an IPSW you get yourself; nothing is downloaded
for you, decrypted or circumvented); the phone's firmware (pulled from your own phone into `fw-*/`, which git ignores; the
loader's `touchfw.h` is made from it at build time and never kept); keys, certificates, activation, or anything that would make the
phone pass as an iPhone to Apple.

## Independent

HakiOS is an independent research project. It is **not affiliated with or endorsed by Apple**, MediaTek or Infinix;
"Darwin", "iPhone", "XNU" and "Apple" are named only to describe what the code works with.

## Licenses and the legal side

My code is under the license of the files it changes: the XNU patch and the kernel's drivers under the Apple Public Source
License 2.0 (Apple publishes XNU under it; the terms are met: changes are published as source, notices are kept), the loader and
the tools as the file headers say. MediaTek's and Linux's GPL sources were read for register addresses and the order of hardware
steps and are named where used, never copied. Darwin, its images, Apple's trademarks and the phone's firmware belong to their
owners, are not in this repository and are not distributed by me; everyone takes them from their own phone and their own IPSW.
If you believe a file here infringes anyone's rights, open an issue and I will remove it.

The detail is in separate files: `LEGAL.md`, `COMPLIANCE.md`, `THIRD_PARTY_LICENSES.md` and `NOTICE`. None of this is legal advice.

hakios is an experimental project focused on bringing darwin to android hardware. our goal is to make hakios free and accessible to everyone.

owners:
@oodze - founder
@netusd - adversting / ceo-dev
@wannqn - dev 
@hxfuxyyXD - dev
@wryzzew - dev
Rhaenja Hun（杭润嘉） — Developer / Sponsor

tym sicilium for the sources!
