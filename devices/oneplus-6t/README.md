# Device: OnePlus 6T (fajita, A6013)

The second Qualcomm phone of HakiOS, after the POCO X5 Pro (`devices/poco-x5-pro/`). Everything that belongs to this
phone is here; the kernel, loader and user space are the common ones. `device.conf` holds the state of each part.

| What | Where |
|---|---|
| SoC | Qualcomm Snapdragon 845 (SDM845, "napali"; `qcom,msm-id <0x141 0x20001>`, `qcom,board-id <8 0>`) |
| UEFI for the phone | `uefi/`: `fajitaPkg`, Mu-Silicium's `enchiladaPkg` (OnePlus 6, the same board) with the 6T's name, model and 1080x2340 panel. Its vendor drivers are Mu-Silicium's own `Binaries/enchilada`: nothing to extract from the phone |
| The phone's device tree | `uefi/files/Resources/DTBs/fajita.dtb`: the phone's own, with `/chosen` emptied (the raw `fdt` pulled over adb carries the serial number and is git-ignored) |
| SoC boot-args | `soc.args`, from `pull-soc.sh` / `tools/soc-args.py` (13 of the 16; see below) |
| The phone's firmware | **not in the repository**: `pull-firmware.sh` pulls it over adb into `fw-*/` at the top |

## How it differs from redwood

| | OnePlus 6T | POCO X5 Pro | What it means for the port |
|---|---|---|---|
| GIC, core numbering | GICv3 at `0x17A00000`/`0x17A60000`, `cpu@0..cpu@700` | the same | the loader's `platform_fajita` is redwood's with another name |
| UEFI framebuffer | `0x9D400000` (cont_splash_region) | `0xE1000000` | how the loader tells the two apart |
| UEFI load address | `0x9FC00000`, 3 MB | `0x9FD00000`, 2 MB | `mkxnuimg.py`'s table |
| Storage | UFS 2.1 at `0x1d84000` | UFS 3.1 | the same Qualcomm host controller; check `hakios.ufshc.c`'s addresses |
| Touch | Synaptics S3320-family, I2C (QUP `i2c@a90000`, address `0x20`, IRQ GPIO 125, reset GPIO 99, 1.8 V GPIO 88) | Goodix on SPI | a new driver (Linux: `rmi4` / OnePlus's `synaptics_driver_s3320.c`) |
| GPU | Adreno 630 (chipid `0x6030001`) | Adreno 642L | `hakios.adreno.c`'s steps hold; registers, GMU firmware and RPMh levels are a630's (Linux `a6xx_gmu.c`, the a630 paths) |
| Wi-Fi | WCN3990, `icnss` | WCN6750 | Linux's ath10k_snoc, not ath11k: a new back end |
| Audio | ADSP, Q6 APR, WCD934x "tavil" | ADSP | rework of `hakios.adsp.c` / `hakios.sound.c` |
| Battery, charging | PMI8998 (fuel gauge, SMB2) | through the ADSP | a PMIC driver instead |
| Haptics | PMI8998 haptics, SPMI `0xc000` | Awinic AW86927 | a small new driver |
| Cellular | IPA 3.5.1; no IPCC; the modem loaded through the MBA (`qcom,pil-q6v55-mss`) | IPA 4.11; IPCC; TrustZone PAS | the largest piece, last |

## soc.args

`tools/soc-args.py` reads SDM845's downstream names as well as SC7280's (`qcom,qmp-mbox`, `qcom,tcs-drv` with `qcom,drv-id
2`, `*_region` reserved memory, `qcom,smem`'s own `reg`). Three values cannot come from this tree and are left to the kernel's
defaults, which are redwood's and wrong here, so **do not pass `modem=1` on this phone yet**:

- `soc_ipcc`: the SDM845 has no IPCC; GLINK to the modem goes through the APCS mailbox (`0x17990000`). The kernel's modem path
  needs that first.
- `soc_cmd_db`: the tree only points at where AOP leaves its address at run time (`qcom,cmd-db` reg `0xc3f000c`).
  Mainline Linux puts it at `0x85fe0000`; read the pointer on the phone to confirm before using it.
- `soc_pas_mpss`: the modem is not a TrustZone PAS image here; it is started through the MBA.

## Bring-up, in order

Each step is one run; `logs/` keeps what it said. Nothing is flashed: `fastboot boot` puts everything in RAM, and a reboot
gives the phone back to Android. Before the first one, back up `persist`, `modemst1`, `modemst2` and `fsg` from the phone
(root, `dd`) — the drivers write to hardware, and those partitions are the ones a phone cannot get back.

**1. A loader that builds.** The loader needs a newer LLVM than Ubuntu 22.04's clang 14 (its inline assembly fails with
"fixup value out of range" there, with or without this port): `sudo apt install clang-15 lld-15` and put
`/usr/lib/llvm-15/bin` first on `PATH`. The kernel (`make kernel`) and the iOS images (`make fetch images`) as in the main
README; check them in QEMU (`make qemu`) before the phone.

**2. The UEFI alone.**

```sh
devices/oneplus-6t/uefi/apply.sh        # Mu-Silicium/ with HakiOS's hooks, redwood's and fajita's platforms
sudo apt install python3.11 python3.11-venv     # build_uefi.py needs 3.11 (tomllib); Ubuntu 22.04's python3 is 3.10
python3.11 -m venv venv && venv/bin/pip install -r Mu-Silicium/pip-requirements.txt
cd Mu-Silicium && source ../venv/bin/activate && python3 build_uefi.py -d fajita -r RELEASE && cd ..
```

If `mu_nasm` fails to download (it comes through NuGet and wants Mono), point it at the system's nasm as
`devices/poco-x5-pro/uefi/README.md` shows.

Success: the HakiOS logo on the 6T's screen, then the loader's text. If the screen stays black, try Mu-Silicium's plain
`enchilada` build on the phone first: if that does not start either, the problem is the UEFI, not HakiOS.

**3. The loader on the phone.**

```sh
export DEVICE=oneplus-6t
./xl enter                  # Android -> fastboot -> UEFI -> the loader, which answers on USB as "xnuloader"
./xl memmap                 # the phone's memory map as UEFI reports it: 0xF0000000 must be free memory
```

The loader's log must say `platform fajita`. If it says `qemu-virt`, UEFI's framebuffer is not at `0x9D400000`: the log's
`display ... at ...` line gives the address to put in `xnuloader/loader.c`.

Done on 30 October 2026: `platform fajita`, display 1080 x 2340, and the payload's place (`0xF0000000`, 64 MB) inside free
memory (`a1769000 - 27c8a0000`, `logs/fajita-memmap.txt`).

The OnePlus 6's USB driver differs from redwood's in three ways, which the loader handles where `usb_old_driver` is set
(`platform_fajita`): it repeats "connected" each time the host opens the device (taken as a new cable, this dropped every
other command); it can leave a send unreported; and it never starts a reply queued a few milliseconds after the command
(`oem log`, which composes 2 MB first, now replies before composing). **Any other command that replies late will hang the
same way** — `getvar result` (which waits for the kernel) and the end of a download are the ones to watch in step 4.

**4. XNU, one core, no drivers.**

```sh
./xl ramdisk userland/system.apfs
XL_SECS=300 XL_CMDLINE="-v cpus=1" ./xl run
```

Not `tools/modem-cycle.sh` / `make phone` yet: they add `hv_wdt_pet=1` (the hypervisor's watchdog, called with redwood's
SMC) and redwood's driver arguments. Success is the lock screen drawn in software. Then `cpus=8 bootcpu=7`.

Done on 30 October 2026, as `XL_CMDLINE="-v cpus=8 bootcpu=7 soc_vtimer=19"`: SpringBoard's home screen, seven cores
online, the run back in the loader after its 300 s. `soc_vtimer=19` is not optional here: the SDM845's virtual timer is
intid 19, not the architecture's 27, and without it the kernel gets one tick and no more (on core 0 it stops after
`lo0`, on any other core sooner). Two loader and image fixes came with it: uploads are received 16 KB at a time
(this driver sends no reply after a receive of 29696 bytes or more), and `userland/build.sh` sets `umask 022`
(launchd refuses SpringBoard's plist when its group may write it).

**5. Storage** (`ufs=2`), so the system image is read from the phone instead of RAM.

**6. Touch and buttons** — the first new driver, and the one that makes it usable. A `hakios.synaptics.c` beside
`hakios.goodix.c`, on the QUP I2C engine at `0xa90000`.

**7. GPU** (`gpu=1`): run `adreno=N` step by step with a630 values, as `hakios.adreno.c` describes.

**8. Then** haptics, battery, Wi-Fi, audio; cellular last.

## Firmware

```sh
devices/oneplus-6t/pull-firmware.sh     # the phone in Android, on adb
```

Pulls the GPU (`a630_*`), Wi-Fi (`wlanmdsp.mbn`, board files), the audio DSP and the modem (with `mba.mbn` and its TFTP
`image/`). The vibration motor needs none; the cameras are not ported.
