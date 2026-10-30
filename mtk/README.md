# MediaTek

The MediaTek part of the common tree. Every other phone HakiOS runs on is a Qualcomm one; a MediaTek phone differs in almost
everything below the kernel's own code: no UEFI (LK, MediaTek's boot loader, starts Android's kernel out of `boot_a`), eMMC
instead of UFS, a Mali GPU instead of an Adreno, CONNSYS instead of the WCN chips, an SCP for the sensors, no SMMU in the way.
Nothing here is shared with the Qualcomm layers, and nothing of a Qualcomm phone is built into a MediaTek kernel.

```
mtk/
├── mt6768/                      the MT6768 (Helio G85) SoC layer: soc.conf, configs/mt6768.config, drivers/
├── loader/                      mtkloader: runs in boot_a in front of Android's kernel and starts XNU once (make loader)
└── tools/                       chainboot.py, xnu-cycle.sh (make phone), boot-xnu.sh, system-flash.sh, probes, log readers
```

`mtk/<soc>/` is a SoC layer of the same shape as `socs/<soc>/` and a device's `soc/` ([How the repositories
fit](../docs/porting/repositories.md)), kept in this tree so that every MediaTek device shares one copy; the tools find it the
same way (`tools/hakios-config.py`, `tools/hakios-device.py`, `tools/boot-args.py`). The devices are repositories of their own,
named in `devices/manifest.conf`.

## The MT6768

| Driver | `CONFIG_HAKIOS_` | Boot-argument | What, as seen on the Infinix Hot 20 (x6826b) |
|---|---|---|---|
| `drivers/storage/hakios.mtk_emmc.c` | `MTK_EMMC` | `emmc_root=1` | the eMMC through MSDC0, read only: the partition `xnusys` as `/dev/emmc0` and the root, the system image read as it is needed (~1 GB of RAM free instead of 10-26 MB) |
| `drivers/input/hakios.mtk_keys.c` | `MTK_KEYS` | `buttons=2`, `mtk_cpufreq=N`, `mali_mhz=N` | power and volume keys (keypad at 0x10010000, the PMIC's power key); the big cores' clock; the Mali's clock (375 to 950 MHz: GPUPLL, and VSRAM_GPU and VGPU as MediaTek's table has them for it) |
| `drivers/input/hakios.tcm.c` | `TCM` | `touchscreen=2` | the x6826b's touch screen: an OmniVision TD4160 on SPI2, TCM protocol; its firmware is sent by the loader |
| `drivers/input/hakios.mtk_scp.c` | `MTK_SCP` | `scp_step=N` | the SCP's sensor hub: the accelerometer (`kern.hakios.accel`) and the way up (`kern.hakios.orientation_state`) at `scp_step=7` |
| `drivers/gpu/hakios.mali.c`, `hakios.mali.cpp` | `MALI` | `mali=1` | the Mali-G52 MC2: power domains, clocks, its MMU, a job queue; `HakiosMali`, panfrost's DRM interface as an IOKit service; `kern.hakios.say` and `kern.hakios.host_keep` |
| `drivers/wifi/hakios.mtk_connsys.c` | `MTK_CONNSYS` | `connsys_step=N` | CONNSYS step by step: power, firmware into its memory, BTIF, STP and WMT; the MCU patch by PDA (steps 9 and 10) is being brought up; no network yet |

All of them are `default n`: a configuration has them when it includes `mt6768.config`, which also leaves every Qualcomm and
QEMU driver out. The head of each file says where its addresses come from (Android's device tree on the phone, MediaTek's
GPL sources, read and named, not copied) and what the runs showed.

The common files have the hooks these drivers need, each harmless where no MediaTek driver is built: the drivers are started
from `hakios_input_init()` with stand-ins in `drivers/core/hakios.stubs.c`; `IOFindBSDRoot` takes `emmc_root=1`; the loader's
second RAM disk is `md1` even when there is no `md0`; the display has `fbfit=` (the panel's proportions), `fb_flip=1` (the
panel's scan-out pointed at a finished picture: MT6768 only, it checks OVL0 first), `fbshot=` (small pictures of the screen in
the log) and `fb_inuse=`; IOSurface has `surf_align=1` (rows of 64 bytes, as the Mali takes them) and `surf_lockwait=1` (a lock
waits for the GPU). Their defaults are what the POCO had; the x6826b's configuration turns them on.

## Building

```sh
tools/hakios-device.py sync x6826b      # the device repository (hakios-mt6768) into devices/x6826b
make x6826b_defconfig                   # common_defconfig + mt6768.config + x6826b.config
make kernel                             # hakios-kernel, with "mali mtk_keys mtk_scp tcm mtk_emmc mtk_connsys" built in
```

The kernel boots through `mtk/loader/` (mtkloader), which sits in `boot_a` in front of Android's kernel and starts XNU once when
Android asks for it (`mtk/tools/boot-xnu.sh`); `make boot-image` puts the two together with the payload (`make payload`: the
kernel and its RAM disk) in your rooted boot image, and `make phone` writes it to `boot_a`, runs the phone once and brings the log
back (the branch `mtk`'s Makefile; the top README has the steps).

## Not here yet

- The user space for the Mali (Mesa's panfrost over `HakiosMali`, the Metal driver) is not in this tree's `userland/`; the one
  that runs on the x6826b still reads the project's former names (`kern.redwood.*`, `RedwoodMali`, `/dev/redwood-touch`). On this
  tree's kernel it draws the home screen on the processor (runs 359, 360).
- The loader knows the x6826b's panel (720 x 1640, LK's framebuffer) and touch controller; another MT6768 phone changes those.
