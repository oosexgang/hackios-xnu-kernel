# HakiOS: one Makefile at the top, the way an Android tree has one. Every target is a script
# below it; run the script itself for its options.
#
# This is the branch `mtk`: the same targets for a MediaTek phone, the Infinix Hot 20 (DEVICE=x6826b) by default
# (mtk/README.md). A device whose SoC is one of mtk/ gets mtk/'s loader, boot image and run; any other, the Qualcomm ones.
#   make x6826b_defconfig && make kernel     the kernel: the common one, the MT6768's layer, the phone's configuration
#   make loader          mtk/loader/build.sh: mtkloader, which runs in boot_a in front of Android's kernel
#   make payload         build-payload.bin: hakios-kernel and the RAM disk (RAMDISK=, default userland/root.apfs)
#   make boot-image      xnu-flashed-slot_a.img: the loader and the payload in your rooted boot image (ANDROID_BOOT=,
#                        default backup/boot_a_magisk.img) -- what make phone writes to boot_a, and to nothing else
#   make phone           one run on the phone (mtk/tools/xnu-cycle.sh): boot_a written, XNU started once, Android back,
#                        the log out of pstore into logs/ (HAKIOS_SERIAL= the phone's serial, as adb devices shows it)
#   make system-flash    the system image (SYSTEM_IMAGE=, default userland/system.apfs) into the partition xnusys
#
#   make                 help
#   make unpack IPSW=F   the OS files out of your own Darwin userland 16.5 IPSW into sysimage/ (sysimage/unpack.sh)
#   make images          userland/root.apfs and userland/system.apfs (userland/build.sh)
#   make kernel          the kernel from source (xnu/apply.sh, xnu-deps/setup.sh, xnu-build.sh)
#   make loader          the boot loader, with identity.h made from your IPSW (xnuloader/build.sh)
#   make firmware        the phone's firmware off the phone over adb (devices/$(DEVICE)/pull-firmware.sh)
#   make qemu            a 20-minute session in QEMU (needs the Darwin images)
#   make qemu-open       boot the open kernel in QEMU with NO Apple software
#                        (kernel + the hello RAM disk + a placeholder loader)
#   make phone           a session on the phone, then back to Android (tools/modem-cycle.sh)
#   make prebuilt        refresh prebuilt/ from what was built (tools/update-prebuilt.sh)
#   make clean           build output of the kernel, loader and images
#   make doctor          check this machine and a phone on adb for what HakiOS needs (tools/doctor.sh)
#   make esp             an EFI system partition tree in build/esp: the loader and a payload it reads itself,
#                        to copy to a FAT32 volume (UEFI-native boot, no fastboot; docs/uefi-boot.md)
#   make u-boot          HakiOS's U-Boot for $(DEVICE) (uefi/u-boot/apply.sh, uefi/u-boot/build.sh; docs/u-boot.md)
#   make u-boot-img      hakios-u-boot.img: U-Boot as a boot image for fastboot boot, starting the loader from RAM
#   make qemu-u-boot     make qemu-open's payload booted the phone's way: U-Boot, the hakios command, bootefi
#
# Configuration, the way Linux's make does it (configs/, tools/hakios-config.py; docs/configs.md):
#   make redwood_defconfig   .config from configs/defconfig + redwood_defconfig (devices/redwood/configs/), which is
#                            common_defconfig + sc7280.config (devices/redwood/soc/) + redwood.config
#                            (any NAME_defconfig; make common_defconfig is the common kernel alone; docs/configs.md)
#   make devices             the device and SoC repositories (tools/hakios-device.py list; docs/porting/repositories.md)
#   make virt.config         lay a fragment (configs/virt.config) over the .config there is
#   make config              show the .config: its symbols, the kernel's arguments, the firmware it wants
#   make savedefconfig       the .config as a minimal defconfig
#   make phone               with a .config, its arguments are the kernel's; without, the phone line below
#   FRAGMENTS="open trace"   fragments for a NAME_defconfig: make redwood_defconfig FRAGMENTS=open
#
#   DEVICE=redwood   the device directory under devices/ (default redwood)
#   ARGS="..."           kernel arguments for make phone (default: the .config's, else everything the phone has)
#   SECS=600             the session's length for make phone / make qemu
#   JOBS=8               parallel jobs for the kernel build

DEVICE ?= x6826b
# a MediaTek device: its SoC's layer is in mtk/ (its device.conf says `soc mt...`), and so are its loader and runner
DEVICE_SOC := $(shell awk '$$1 == "soc" { print $$2; exit }' devices/$(DEVICE)/device.conf 2>/dev/null)
MTK := $(if $(wildcard mtk/$(DEVICE_SOC)/soc.conf),1,)
RAMDISK ?= userland/root.apfs
SYSTEM_IMAGE ?= userland/system.apfs
JOBS ?= 8
SECS ?= 600
# qemu-open runs the open configuration: configs/defconfig (nothing of a phone's own, one core)
ARGS_OPEN ?= $(shell tools/hakios-config.py args --defconfig defconfig)
ifneq ($(wildcard .config),)
# a .config is the kernel's arguments; the runner then does not add its own four (cores, clock, storage): they are in it
ARGS ?= $(shell tools/hakios-config.py args)
RUNNER_BASE = BASE_ARGS=
else
ARGS ?= touchscreen=1 gpu=1 adsp=1 sound=4 wifi=1 haptic=5 modem=1 modem_again=1 modem_buffer=0xa8000000
RUNNER_BASE =
endif
IMAGE_OPTS ?= REST=1
ESP_DIR ?= build/esp
PAYLOAD ?= qemu/payload.bin
# make u-boot-img: the payload put in the image's ramdisk with the loader (none: U-Boot waits for them over fastboot)
UBOOT_PAYLOAD ?=

.PHONY: devices help unpack images kernel loader payload boot-image system-flash loader-open hello qemu qemu-open qemu-u-boot u-boot u-boot-img phone prebuilt clean all doctor defconfig config savedefconfig esp

help:
	@awk '{ if (!/^#/) exit; sub(/^# ?/, ""); print }' Makefile

doctor:
	tools/doctor.sh

all: kernel loader images

unpack:
	sysimage/unpack.sh $(IPSW)

images:
	$(IMAGE_OPTS) userland/build.sh

kernel:
	[ -d xnu-src/xnu-8796.121.2 ] || xnu/apply.sh
	[ -e xnu-deps/host/bin/ld64 ] || xnu-deps/setup.sh
	tools/make-kernel.sh -j$(JOBS)

loader:
	$(if $(MTK),mtk/loader/build.sh,xnuloader/build.sh)

# MediaTek (mtk/README.md): the kernel and the RAM disk as mtkloader's payload, and the boot image made of it
payload:
	[ -n "$(MTK)" ] || { echo "make payload: $(DEVICE) is not a MediaTek device (mtk/)"; exit 1; }
	[ -f hakios-kernel ] || { echo "make payload: no hakios-kernel (make $(DEVICE)_defconfig && make kernel)"; exit 1; }
	[ -f "$(RAMDISK)" ] || { echo "make payload: no RAM disk $(RAMDISK) (RAMDISK=...)"; exit 1; }
	python3 xnuloader/mkpayload.py -o build-payload.bin kernel=hakios-kernel ramdisk=$(RAMDISK)

boot-image: payload loader
	python3 mtk/loader/build_flashed.py

system-flash:
	[ -n "$(MTK)" ] || { echo "make system-flash: $(DEVICE) is not a MediaTek device (mtk/)"; exit 1; }
	mtk/tools/system-flash.sh $(SYSTEM_IMAGE)

firmware:
	devices/$(DEVICE)/pull-firmware.sh

qemu:
	mkdir -p logs
	qemu/run-darwin.sh $(SECS) -display gtk | tee logs/qemu.log

# The Apple-free path: no IPSW, no Darwin user space. The loader is built with a placeholder identity
# (xnuloader/mkidentity.sh --placeholder), and the kernel boots the raw-syscall `hello` RAM disk.
# This is the configuration an outside contributor can build and run with nothing of Apple's.
loader-open:
	PLACEHOLDER=1 xnuloader/mkidentity.sh
	xnuloader/build.sh

# `hello` on its own: a static, raw-syscall program (no Darwin SDK, no Apple libraries).
hello:
	. ./xnu-env.sh && xnu-clang -arch arm64 -Os -static -nostdlib -ffreestanding \
		-fno-stack-protector -fno-builtin -Wl,-e,_start -Wl,-pagezero_size,0x100000000 \
		-o userland/hello userland/src/hello.c

qemu-open: kernel loader-open hello
	mkdir -p logs
	xnuloader/mkpayload.py -o qemu/payload.bin \
		kernel=hakios-kernel \
		ramdisk=userland/hello --cmdline "$(ARGS_OPEN)"
	qemu/run.sh $(SECS) | tee logs/qemu-open.log

# HakiOS's U-Boot (docs/u-boot.md): the phone's ABL starts it from RAM (fastboot boot), and it starts the loader from
# memory; nothing is flashed. u-boot/ is made once by uefi/u-boot/apply.sh, with every device's U-Boot files.
u-boot:
	[ -d u-boot ] || uefi/u-boot/apply.sh
	uefi/u-boot/build.sh $(DEVICE)

u-boot-img:
	tools/mkubootimg.py -d $(DEVICE) $(if $(wildcard $(UBOOT_PAYLOAD)),-p $(UBOOT_PAYLOAD))

qemu-u-boot: kernel loader-open hello
	[ -f u-boot/build-qemu/u-boot.bin ] || { [ -d u-boot ] || uefi/u-boot/apply.sh; uefi/u-boot/build.sh qemu; }
	mkdir -p logs
	xnuloader/mkpayload.py -o qemu/payload.bin \
		kernel=hakios-kernel \
		ramdisk=userland/hello --cmdline "$(ARGS_OPEN)"
	qemu/run-u-boot.sh $(SECS) | tee logs/qemu-u-boot.log

phone:
	[ ! -f .config ] || tools/hakios-config.py check
ifneq ($(MTK),)
	$(MAKE) payload
	mkdir -p logs
	XNU_SECS=$(SECS) XNU_ARGS="$(ARGS)" XNU_LOG_OUT=$${XNU_LOG_OUT:-logs/xnu-$(DEVICE).log} mtk/tools/xnu-cycle.sh
else
	XL_SB_DELAY=8 $(RUNNER_BASE) tools/modem-cycle.sh "$(ARGS)" $(SECS)
endif

# The loader and a payload (make qemu-open makes qemu/payload.bin; xnuloader/mkpayload.py makes any other) as the files an
# EFI system partition holds, which the loader reads itself: UEFI-native boot. The default tree is made again each time.
esp:
	[ "$(ESP_DIR)" != build/esp ] || rm -rf build/esp
	tools/mkesp.py $(ESP_DIR) $(PAYLOAD)

# ---- configuration (configs/, tools/hakios-config.py) ---------------------------------------------------------
defconfig:
	tools/hakios-config.py defconfig defconfig $(FRAGMENTS)

devices:
	@tools/hakios-device.py list

%_defconfig:
	tools/hakios-config.py defconfig $* $(FRAGMENTS)

%.config:
	tools/hakios-config.py merge $*

config:
	tools/hakios-config.py show

savedefconfig:
	tools/hakios-config.py savedefconfig

prebuilt:
	tools/update-prebuilt.sh

clean:
	rm -rf xnu-src/xnu-8796.121.2/BUILD xnuloader/loader.obj xnuloader/loader.efi userland/root.apfs userland/system.apfs userland/obj
