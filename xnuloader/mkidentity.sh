#!/bin/bash
# Make xnuloader/identity.h: the identity of the iPhone whose files iOS 16.5 was built for, taken
# from the device tree in the IPSW (Firmware/all_flash/DeviceTree.d22ap.im4p) at build time, on
# the builder's machine, from their own copy of the firmware. The header is not in the repository.
#
#   sysimage/fetch-from-ipsw.py      the one file out of the IPSW on Apple's CDN (34 KB, HTTP ranges)
#   xnuloader/im4p.py              its payload: an LZFSE stream
#   lzfse(1) / compression_tool(1) / python3 -m lzfse / xnu-deps/host/bin/lzfse (built from the
#                                  BSD-licensed github.com/lzfse/lzfse when nothing else is there)
#   xnuloader/mkidentity.py        the header from the decoded tree
#
# usage: xnuloader/mkidentity.sh [DeviceTree.im4p]   (an im4p already at hand skips the download)
#        xnuloader/mkidentity.sh --placeholder       (no IPSW: an identity.h with no Apple data,
#                                                     enough to build and boot the open kernel under
#                                                     QEMU with an Apple-free user space, not iOS)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$HERE/.." && pwd)
if [ "$1" = --placeholder ] || [ "${PLACEHOLDER:-}" = 1 ]; then
	python3 "$HERE/mkidentity.py" --placeholder > "$HERE/identity.h.new"
	mv "$HERE/identity.h.new" "$HERE/identity.h"
	echo "mkidentity.sh: xnuloader/identity.h made WITHOUT an IPSW (placeholder; no Apple data, not for iOS)"
	exit 0
fi
WORK=$(mktemp -d "${TMPDIR:-/tmp}/mkidentity.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
IM4P=${1:-$WORK/dt.im4p}
if [ ! -f "$IM4P" ]; then
	python3 "$TOP/sysimage/fetch-from-ipsw.py" Firmware/all_flash/DeviceTree.d22ap.im4p "$IM4P"
fi
python3 "$HERE/im4p.py" "$IM4P" "$WORK/dt.lzfse"
decode() {
	if command -v lzfse >/dev/null; then lzfse -decode -i "$1" -o "$2"
	elif [ -x /usr/bin/compression_tool ]; then /usr/bin/compression_tool -decode -a lzfse -i "$1" -o "$2"
	elif python3 -c 'import lzfse' 2>/dev/null; then python3 -c 'import lzfse,sys; open(sys.argv[2],"wb").write(lzfse.decompress(open(sys.argv[1],"rb").read()))' "$1" "$2"
	elif [ -x "$TOP/xnu-deps/host/bin/lzfse" ]; then "$TOP/xnu-deps/host/bin/lzfse" -decode -i "$1" -o "$2"
	else
		echo "mkidentity.sh: no LZFSE decoder: building lzfse (BSD, github.com/lzfse/lzfse) into xnu-deps/host/bin" >&2
		git clone -q --depth 1 https://github.com/lzfse/lzfse "$WORK/lzfse" && make -s -C "$WORK/lzfse" all >/dev/null &&
			mkdir -p "$TOP/xnu-deps/host/bin" && cp "$WORK/lzfse/build/bin/lzfse" "$TOP/xnu-deps/host/bin/lzfse" &&
			"$TOP/xnu-deps/host/bin/lzfse" -decode -i "$1" -o "$2"
	fi
}
decode "$WORK/dt.lzfse" "$WORK/dt.bin"
python3 "$HERE/mkidentity.py" "$WORK/dt.bin" > "$HERE/identity.h.new"
mv "$HERE/identity.h.new" "$HERE/identity.h"
echo "mkidentity.sh: xnuloader/identity.h made from the device tree ($(wc -c < "$WORK/dt.bin") bytes)"
