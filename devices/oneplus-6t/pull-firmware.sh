#!/bin/bash
# Pull the OnePlus 6T's own firmware out of its Android into the fw-*/ directories at the top of the
# repository (never committed: it is the vendor's). The phone is on adb, in Android (OxygenOS or a
# LineageOS-based ROM; the modem partition is mounted at /vendor/firmware_mnt).
#
# The SDM845's names differ from the POCO X5 Pro's: the GPU is an Adreno 630 (a630_*), Wi-Fi is the
# WCN3990, whose firmware (wlanmdsp.mbn) the modem loads, so there is no wpss; the vibration motor is
# the PMI8998's own haptics block and needs no firmware; the cameras are not ported yet. Each set is
# listed on the phone (adb shell ls) rather than counted here, because the number of .bNN segments
# differs between firmware versions.
set -e
cd "$(dirname "$0")/../.."
pull() { # DIR PATTERN...  (patterns are expanded on the phone; nothing found is told, not fatal)
	local dir=$1; shift
	mkdir -p "$dir"
	local files
	files=$(adb shell "ls $* 2>/dev/null" | tr -d '\r')
	[ -n "$files" ] || echo "pull-firmware.sh: $dir: none of $* on the phone"
	for f in $files; do adb pull "$f" "$dir/" >/dev/null 2>&1 || echo "pull-firmware.sh: $f: could not be read"; done
	echo "pull-firmware.sh: $dir: $(ls -1 "$dir" | wc -l | tr -d ' ') files"
}
adb get-state >/dev/null
img=/vendor/firmware_mnt/image
pull fw-gpu /vendor/firmware/a630_sqe.fw /vendor/firmware/a630_gmu.bin '/vendor/firmware/a630_zap.*'
pull fw-wifi $img/wlanmdsp.mbn '/vendor/firmware/wlan/qca_cld/*' '/vendor/etc/wifi/bdwlan*' "$img/bdwlan*"
pull fw-audio "$img/adsp.mdt" "$img/adsp.b*"
pull fw-modem "$img/mba.mbn" "$img/modem.mdt" "$img/modem.b*"
mkdir -p fw-modem/image && adb pull $img fw-modem/ >/dev/null 2>&1 && echo "pull-firmware.sh: fw-modem/image: the modem's own files (TFTP)"
echo "pull-firmware.sh: done; Wi-Fi also wants fw-wifi/mac (the radio's address) and fw-wifi/network (the network's name and passphrase)"
