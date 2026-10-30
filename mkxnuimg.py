#!/usr/bin/env python3
"""
Pack a phone's UEFI (with the hook that starts the XNU loader) and an optional payload
into an Android boot image for `fastboot boot`. The phone is DEVICE (a directory under devices/,
as in the Makefile; default poco-x5-pro).

  kernel = gzip(BootShim + UEFI FD + ["XNUP" container]) + DTB

The boot shim copies the FD to its base and the payload to PAYLOAD_BASE.

The UEFI pieces are taken from a Mu-Silicium tree built here (Mu-Silicium/, see uefi/README.md)
or, when there is none, from devices/$DEVICE/prebuilt/.

usage: mkxnuimg.py [-p payload.bin] [-o out.img]
"""
import argparse, gzip, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
MU = os.path.join(HERE, 'Mu-Silicium')
# devices/<dir>: its Mu-Silicium platform (<name>Pkg, Resources/DTBs/<name>.dtb) and where its UEFI is loaded
# (Resources/Configs/<name>.toml, [uefi_fd])
DEVICES = {
    'poco-x5-pro': ('redwood', 0x9FD00000, 0x200000),
    'oneplus-6t':  ('fajita',  0x9FC00000, 0x300000),
}
DEVICE = os.environ.get('DEVICE', 'poco-x5-pro')
if DEVICE not in DEVICES:
    sys.exit(f'mkxnuimg.py: DEVICE={DEVICE}: not one of {", ".join(DEVICES)}')
NAME, FD_BASE, FD_SIZE = DEVICES[DEVICE]
PREBUILT = os.path.join(HERE, 'devices', DEVICE, 'prebuilt')
PAYLOAD_BASE, PAYLOAD_MAX = 0xF0000000, 0x04000000

ap = argparse.ArgumentParser()
ap.add_argument('-p', '--payload')
ap.add_argument('-o', '--output', default=os.path.join(HERE, f'xnu-{NAME}.img'))
ap.add_argument('--mode', default='RELEASE')
args = ap.parse_args()

fd_path = os.path.join(MU, f'Build/{NAME}Pkg/{args.mode}_CLANGPDB/FV/SILICIUM_UEFI.fd')
if os.path.isfile(fd_path):
    shim_dir = os.path.join(MU, 'BootShim')
    for f in ('BootShim.elf', 'BootShim.bin'):
        try: os.unlink(os.path.join(shim_dir, f))
        except FileNotFoundError: pass
    subprocess.check_call(['make', 'REQUIRES_KERNEL_HEADER=1', f'FD_BASE={FD_BASE:#x}', f'FD_SIZE={FD_SIZE:#x}',
                           f'CFLAGS=--target=aarch64-linux-gnu -DPAYLOAD_BASE={PAYLOAD_BASE:#x}'],
                          cwd=shim_dir, stdout=subprocess.DEVNULL)
    parts = {'shim': os.path.join(shim_dir, 'BootShim.bin'), 'fd': fd_path,
             'dtb': os.path.join(MU, f'Resources/DTBs/{NAME}.dtb'), 'ramdisk': os.path.join(MU, 'Resources/ramdisk'),
             'mkbootimg': os.path.join(MU, 'Resources/Scripts/mkbootimg.py')}
else:
    parts = {'shim': os.path.join(PREBUILT, 'BootShim.bin'), 'fd': os.path.join(PREBUILT, 'SILICIUM_UEFI.fd'),
             'dtb': os.path.join(PREBUILT, 'device-tree.dtb'), 'ramdisk': os.path.join(PREBUILT, 'ramdisk'),
             'mkbootimg': os.path.join(PREBUILT, 'mkbootimg.py')}
missing = [p for p in parts.values() if not os.path.isfile(p)]
if missing:
    sys.exit(f'mkxnuimg.py: no UEFI for {DEVICE}: {missing[0]} is missing; build one (uefi/README.md, devices/{DEVICE}/README.md)')
shim = open(parts['shim'], 'rb').read()
fd = open(parts['fd'], 'rb').read()
assert len(fd) == FD_SIZE, hex(len(fd))

blob = shim + fd
if args.payload:
    data = open(args.payload, 'rb').read()
    # the payload file is a complete "XNUP" container (see xnuloader/mkpayload.py)
    assert data[:4] == b'XNUP' and len(data) <= PAYLOAD_MAX - 0x1000, 'bad or oversized payload'
    blob += data
    blob += b'\0' * (-len(blob) % 16)
    print(f'payload: {len(data)} bytes, bytesum={sum(data) & 0xffffffff:08x}')

dtb = open(parts['dtb'], 'rb').read()
kernel = args.output + '.kernel~'
open(kernel, 'wb').write(gzip.compress(blob, 6) + dtb)
try:
    subprocess.check_call([sys.executable, parts['mkbootimg'], '--kernel', kernel, '--ramdisk', parts['ramdisk'],
                           '--header_version', '1', '-o', args.output])
finally:
    os.unlink(kernel)
print(f'{args.output}: {os.path.getsize(args.output)} bytes (shim {len(shim)}, uncompressed {len(blob)})')
