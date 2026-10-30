#!/bin/bash
# Boot the XNU loader + kernel payload under QEMU (virt, GICv3, UEFI at EL1).
# usage: run.sh [seconds] [extra qemu args...]
# env: QEMU_TB_MB           megabytes for QEMU's cache of translated code (default 384; QEMU's own
#                           default of 1 GB mostly costs host memory)
#      QEMU_SMP             processor cores of the machine (default 1); the kernel uses as many of them as
#                           its boot-arg cpus=N says (CMDLINE="cpus=4" for run-darwin.sh), one without it
#      QEMU_ESP=1           no loader device: the payload is the file \EFI\hakios\payload.bin on the ESP, which the loader
#                           reads itself (UEFI-native boot; what a boot menu or a USB stick does)
#      QEMU_EFI, QEMU_VARS  the UEFI firmware for aarch64 guests (found by itself where Arch's edk2-aarch64
#                           puts it, or Debian/Ubuntu's /usr/share/AAVMF/AAVMF_CODE.fd and AAVMF_VARS.fd)
cd "$(dirname "$0")"
T=${1:-20}; shift
# the UEFI firmware: where Arch's edk2-aarch64 puts it, or where Debian and Ubuntu's AAVMF does
EFI=${QEMU_EFI-}; VARS_SRC=${QEMU_VARS-}
if [ -z "$EFI" ]; then
	for f in /usr/share/edk2/aarch64/QEMU_EFI.fd /usr/share/AAVMF/AAVMF_CODE.fd; do [ -f "$f" ] && { EFI=$f; break; }; done
fi
if [ -z "$VARS_SRC" ]; then
	for f in /usr/share/edk2/aarch64/QEMU_VARS.fd /usr/share/AAVMF/AAVMF_VARS.fd; do [ -f "$f" ] && { VARS_SRC=$f; break; }; done
fi
[ -f "$EFI" ] || { echo "run.sh: no UEFI firmware found (looked for edk2-aarch64's and AAVMF's); set QEMU_EFI and QEMU_VARS"; exit 1; }
cp "$VARS_SRC" vars.fd 2>/dev/null
# the loader is the boot application of the little FAT volume QEMU makes of esp/:
# the one built here, or else the one that comes with the repository
L=../xnuloader/loader.efi; [ -f $L ] || L=../prebuilt/loader.efi
mkdir -p esp/EFI/BOOT && cp $L esp/EFI/BOOT/BOOTAA64.EFI
# The payload (kernel, root image, command line) reaches the loader in one of two ways. By default QEMU's loader
# device puts it in memory at 0xF0000000, as the boot shim does behind Project Silicium's image on the phone.
# With QEMU_ESP=1 it is not put anywhere: it is the file \EFI\hakios\payload.bin of the ESP, and the loader reads it
# itself from the volume it was started from -- UEFI-native boot, what a boot menu or a USB stick does (docs/uefi-boot.md).
PAYLOAD_DEVICE=(-device loader,file=payload.bin,addr=0xF0000000,force-raw=on)
if [ -n "${QEMU_ESP:-}" ]; then
	mkdir -p esp/EFI/hakios && cp payload.bin esp/EFI/hakios/payload.bin
	PAYLOAD_DEVICE=()
fi
timeout "$T" qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a76 -smp ${QEMU_SMP:-1} -m ${QEMU_MEM:-6G} \
  -accel tcg,tb-size=${QEMU_TB_MB:-384} \
  -drive if=pflash,format=raw,readonly=on,file="$EFI" \
  -drive if=pflash,format=raw,file=vars.fd \
  -drive file=fat:rw:esp,format=raw,if=virtio \
  "${PAYLOAD_DEVICE[@]}" \
  -display none -serial stdio -monitor none -no-reboot "$@" 2>&1
