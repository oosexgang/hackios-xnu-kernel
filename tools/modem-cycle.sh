#!/bin/bash
# tools/modem-cycle.sh "KERNEL ARGS" [SECS] [LOG]: wait for the phone (up to 6 h), enter the loader, upload the
# system image, run the kernel with those args, summarize the modem's log, and leave the phone in the loader
# (or fastboot) for the next run (XL_END=android: back to Android).
# The phone is never flashed: the kernel is booted with "fastboot boot"-like upload through xl.
cd "$(dirname "$0")/.." || exit 1
PATH=$PWD/tools/macos-bin/platform-tools:$PWD/tools/macos-bin:$PATH
ARGS=$1; SECS=${2:-300}
# A port's SoC addresses (devices/<name>/soc.args, made by devices/<name>/pull-soc.sh): added to the
# kernel command line so hakios.mpss.c / hakios.ipa.c map that phone's blocks. SOC_ARGS names the file;
# default is DEVICE's (as in the Makefile; poco-x5-pro), not whichever devices/*/ sorts first. The POCO's values equal
# the kernel defaults, so this is a no-op there.
SOC_ARGS=${SOC_ARGS-devices/${DEVICE:-poco-x5-pro}/soc.args}
SOC=""; [ -n "$SOC_ARGS" ] && [ -f "$SOC_ARGS" ] && SOC=$(tr '\n' ' ' < "$SOC_ARGS")
for i in $(seq 1 2160); do s=$(./xl state); [ "$s" != absent ] && break; sleep 10; done
echo "$(date +%T) phone: $s"; [ "$s" = absent ] && { echo "NO PHONE"; exit 1; }
./xl enter 2>&1 | tail -1
./xl ramdisk userland/system.apfs 2>&1 | tail -1
XL_SB_DELAY=${XL_SB_DELAY:-600} XL_CMDLINE="debug=0x8 rd=md0 hv_wdt_pet=1 bootcpu=7 cpus=8 cpufreq=1 ufs=2 $ARGS $SOC" XL_SECS=$SECS XL_FOLLOW=1 XL_FOLLOW_KEEP=1 ./xl run 2>&1 | tail -2
echo "$(date +%T) state after run: $(./xl state)"
L=$(ls -t logs/*-run.log.live | head -1); F=${L%.live}; echo "LOG=$F"
cat $L $F 2>/dev/null | grep -a "mpss: \(7: \(THE\|the watch\)\|11\|rmtfs\|tftp\|servreg\|dms\|nas\|ipa: \(THE\|from node\|the modem\|a probe\)\|link: \(THE\|tftp\)\|LAST WORDS\|step 1[01]\)\|serving system\|panic(\|ipa: \|cell: \|net: en1\|wda: \|wds: \(THE\|the client\|about\)" | grep -v "about to ask\|about to tell" | sed 's/^\[ *[0-9.]*\]: //' | awk '!seen[$0]++' | cut -c1-230 | head -80
# Where the phone is left (XL_END): "loader" (default) -- the kernel hands control back to our loader
# at the end of the run (loader_return_after: the modem, the DSPs and the GPU stopped first), and the
# phone stays there, in the loader's fastboot-protocol channel, ready for the next run with no reboot
# at all; if the return failed and the watchdog reset the phone into Android, it is sent back to
# fastboot, so a phone left alone always sits where it can be recovered. "android": the old way,
# back to Android after every run.
case "${XL_END:-loader}" in
android)
	echo "== back to Android"
	case "$(./xl state)" in loader|fastboot) XL_FORCE=1 ./xl reboot >/dev/null 2>&1 ;; esac
	for i in $(seq 1 40); do s=$(./xl state); [ "$s" = android ] && break; sleep 5; done ;;
*)
	echo "== staying in the loader / fastboot"
	for i in $(seq 1 60); do s=$(./xl state); [ "$s" != absent ] && break; sleep 5; done
	if [ "$s" = android ]; then
		PATH=$PWD/tools/macos-bin/platform-tools:$PATH adb reboot bootloader >/dev/null 2>&1
		for i in $(seq 1 30); do s=$(./xl state); [ "$s" = fastboot ] && break; sleep 5; done
	fi ;;
esac
echo "$(date +%T) phone: $s"; echo DONE
