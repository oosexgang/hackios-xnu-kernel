#!/usr/bin/env python3
"""Print the modem/IPA SoC addresses of a Qualcomm phone as HakiOS boot-args, from its device tree.

usage: tools/soc-args.py DEVICE-TREE.dtb

The kernel's modem and IPA drivers (bsd/dev/hakios.mpss.c, hakios.ipa.c) carry the first phone's
addresses as defaults and take any that differ from boot-args soc_* (see soc_override there). This
reads a phone's own flattened device tree -- /sys/firmware/fdt on a rooted phone, or the dtb a port
already has -- and prints the soc_* it needs, which devices/<name>/soc.args holds and the run
scripts pass along. Only numbers are read out; no part of the tree is copied.
"""
import struct
import sys


def nodes_of(blob):
    if blob[:4] != b'\xd0\x0d\xfe\xed':
        sys.exit('soc-args.py: not a flattened device tree')
    off_struct, off_strings = struct.unpack('>II', blob[8:16])
    size_strings, = struct.unpack('>I', blob[32:36])
    strings = blob[off_strings:off_strings + size_strings]
    out, path, at = {}, [], off_struct
    while True:
        token, = struct.unpack('>I', blob[at:at + 4])
        at += 4
        if token == 1:                                  # FDT_BEGIN_NODE
            end = blob.index(b'\0', at)
            path.append(blob[at:end].decode())
            at = (end + 4) & ~3
            out['/'.join(path)] = {}
        elif token == 2:                                # FDT_END_NODE
            path.pop()
        elif token == 3:                                # FDT_PROP
            length, name_off = struct.unpack('>II', blob[at:at + 8])
            at += 8
            name = strings[name_off:strings.index(b'\0', name_off)].decode()
            out['/'.join(path)][name] = blob[at:at + length]
            at = (at + length + 3) & ~3
        elif token == 9:                                # FDT_END
            return out
        elif token != 4:                                # 4 = FDT_NOP
            sys.exit(f'soc-args.py: unexpected token {token}')


def find(nodes, compatible):
    for path, props in nodes.items():
        if compatible.encode() in props.get('compatible', b'').split(b'\0'):
            return path, props
    return None, None


def find_named(nodes, leaf):
    """The node whose last path component begins with `leaf` (e.g. apps-smmu@..., several SMMUs share a compatible)."""
    for path, props in nodes.items():
        if path.rsplit('/', 1)[-1].split('@', 1)[0] == leaf:
            return path, props
    return None, None


def u32(value, index=0):
    return struct.unpack_from('>I', value, index * 4)[0]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    nodes = nodes_of(open(sys.argv[1], 'rb').read())
    args = []

    path, ipa = find(nodes, 'qcom,ipa')
    if ipa and 'reg' in ipa and len(ipa['reg']) >= 16:  # reg = ipa-base, size, gsi-base, size (1 address + 1 size cell each)
        ipa_region = u32(ipa['reg'], 0)                 # the IPA region's start; the registers the driver maps are 0x40000 into it
        gsi_base = u32(ipa['reg'], 2)                   # the GSI's base is given directly
        args.append(('soc_ipa_base', ipa_region + 0x40000))
        args.append(('soc_gsi_base', gsi_base))

    def block(compatible, arg, cell=0):                 # these nodes have 1 address + 1 size cell: address at an even index
        _, props = find(nodes, compatible)
        if props and 'reg' in props and len(props['reg']) >= (cell + 1) * 4:
            args.append((arg, u32(props['reg'], cell)))

    block('qcom,ipcc', 'soc_ipcc')                      # cell 0: the address (SDM845 and older have none: the APCS mailbox instead)
    block('qcom,sm8350-aoss-qmp', 'soc_aoss')           # the AOSS mailbox (its compatible varies by SoC)
    if not any(name == 'soc_aoss' for name, _ in args):
        block('qcom,qmp-mbox', 'soc_aoss')              # SDM845's downstream tree: the AOP mailbox, cell 0 its message RAM
    block('qcom,rpmh-rsc', 'soc_rsc', cell=4)           # drv-2 (the application processors') is the third addr/size pair (reg-names drv-0/1/2)
    if not any(name == 'soc_rsc' for name, _ in args):
        for path, props in nodes.items():               # SDM845's downstream tree: one qcom,tcs-drv node per DRV, the apps one is drv-id 2
            if b'qcom,tcs-drv' in props.get('compatible', b'').split(b'\0') and u32(props.get('qcom,drv-id', b'\0' * 4)) == 2:
                args.append(('soc_rsc', u32(props['reg'], 0)))
                break
    _, smmu = find_named(nodes, 'apps-smmu')            # not by compatible: the GPU's SMMU shares qcom,qsmmu-v500
    if smmu and 'reg' in smmu:
        args.append(('soc_apps_smmu', u32(smmu['reg'], 0)))

    # the memory addresses: the command DB and the modem's region are reserved-memory nodes named by
    # their address; shared memory is the reserved-memory the /soc/qcom,smem node points at; the SoC's
    # internal memory is the msm-imem node rounded down to its 2 MB block (what the IPA's table maps).
    by_phandle = {}
    for path, props in nodes.items():
        if 'phandle' in props:
            by_phandle[u32(props['phandle'])] = (path, props)

    def reserved(leaf, arg):
        _, props = find_named(nodes, leaf)
        if not props or 'reg' not in props:
            _, props = find_named(nodes, leaf + '_region')  # SDM845's downstream tree: modem_region@..., ipa_gsi_region@...
        if props and 'reg' in props:
            args.append((arg, u32(props['reg'], 1)))    # reserved-memory: 2 address cells, so the low word is cell 1

    reserved('cmd_db', 'soc_cmd_db')
    reserved('mpss', 'soc_mpss_mem')
    if not any(name == 'soc_mpss_mem' for name, _ in args):
        reserved('modem', 'soc_mpss_mem')
    reserved('ipa_gsi', 'modem_ipa_fw_at')              # where the IPA's firmware is loaded (the ipa_gsi reservation)
    _, smem = find(nodes, 'qcom,smem')
    if smem and 'memory-region' in smem:
        target = by_phandle.get(u32(smem['memory-region']))
        if target and 'reg' in target[1]:
            args.append(('soc_smem', u32(target[1]['reg'], 1)))
    elif smem and 'reg' in smem:                        # SDM845's downstream tree: the node's own first reg is shared memory
        args.append(('soc_smem', u32(smem['reg'], 0)))
    _, imem = find(nodes, 'qcom,msm-imem')
    if imem and 'reg' in imem:
        args.append(('soc_imem', u32(imem['reg'], 0) & ~0x1fffff))   # the 2 MB block that holds it

    def stream(compatible, arg):
        _, props = find(nodes, compatible)
        if props and 'iommus' in props and len(props['iommus']) >= 8:
            args.append((arg, u32(props['iommus'], 1)))   # iommus = <phandle stream mask>; the stream is the second cell

    stream('qcom,ipa-smmu-ap-cb', 'soc_ipa_stream')
    stream('qcom,ipa-smmu-uc-cb', 'soc_ipa_stream_uc')

    def pas_id(leaf, arg):
        _, props = find_named(nodes, leaf)
        if props and 'qcom,pas-id' in props:
            args.append((arg, u32(props['qcom,pas-id'])))

    pas_id('qcom,mss', 'soc_pas_mpss')
    pas_id('qcom,ipa_fws', 'soc_pas_ipa')

    _, ipa_fws = find_named(nodes, 'qcom,ipa_fws')
    ipa_fw_name = None
    if ipa_fws and 'qcom,firmware-name' in ipa_fws:
        ipa_fw_name = ipa_fws['qcom,firmware-name'].split(b'\0')[0].decode()

    for name, value in args:
        print(f'{name}={value:#x}')
    if ipa_fw_name:
        print(f'soc_ipa_fw={ipa_fw_name}')
    if not args:
        sys.exit('soc-args.py: found none of the expected nodes; is this the phone\'s own device tree?')


if __name__ == '__main__':
    main()
