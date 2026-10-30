/*
 * CONNSYS, the Wi-Fi/Bluetooth/GPS subsystem of the Infinix Hot 20's MT6768 (with the MT6631 radio):
 * docs/wifi-x6826b.md. Steps 1 and 2 read only; 3 and 4 write (the PMIC's supplies, then the power domain).
 *
 * The addresses are the ones Android's device tree gives MediaTek's driver for this SoC
 * (consys@18002000, "mediatek,mt6768-consys", printed by mtkloader/mtkconnsys.h), and what is read
 * is what that driver reads (common_main/platform/mt6768.c and include/mt6768.h in
 * MiCode/MTK_kernel_modules, merlin-r-oss):
 *
 *   SPM (0x10006000)     +0x32C  TOP1_PWR_CTRL  bit 0 reset released, 1 isolation, 2 power on, 3 power on (second),
 *                                               4 clock
 *                        +0x180  PWR_STATUS     bit 1 CONNSYS on, bit 30 its second (TOP2) domain on
 *                        +0x184  PWR_STATUS_2ND the same, from the other half of the power switch
 *   MCU_TOP_MISC_OFF (0x180B1000) +0x10  CONSYS_IP_VER: 0x10020501 once CONNSYS runs; +0x1C its configuration
 *   MCU (0x18002000)     +0x00  hardware version, +0x04 firmware (ROM) version
 *
 * The CONNSYS registers (the last two blocks) are read only when SPM says both of its domains are on:
 * a read of a powered-off block of this kind can hang the bus. Android powers CONNSYS on at its own
 * start and LK does not, so after a boot of XNU straight from LK it is expected to be off.
 *
 * Boot-arg connsys_step=1; 2 also reads its supplies, 3 turns them on, 4 powers CONNSYS up, 5 puts its firmware in and lets its CPU go, 6 also
 * listens on BTIF, 7 asks WMT for the chip ID through it, 8 puts STP into its full mode (below); 9 reads the MCU's
 * patch and says what would be sent, 10 sends it the PDA way and resets WMT before the full mode, as Android does
 * on this chip (connsys_pda_patch). What it finds is said once and kept for the end of the log.
 */

#include <kern/clock.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.input.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/vnode_internal.h>
#include <kern/thread.h>
#include <kern/kalloc.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void hakios_host_keep_add(const char *format, ...) __printflike(1, 2);
extern void delay(int usec);

#define SPM_PHYS                0x10006000u
#define SPM_TOP1_PWR_CTRL       0x32C
#define SPM_PWR_STATUS          0x180
#define SPM_PWR_STATUS_2ND      0x184
#define PWR_STATUS_CONN         (1u << 1)
#define PWR_STATUS_CONN_TOP2    (1u << 30)

#define MCU_TOP_MISC_OFF_PHYS   0x180B1000u
#define CONSYS_IP_VER           0x10
#define CONSYS_CONF_ID          0x1C
#define CONSYS_IP_VER_ID        0x10020501u

#define MCU_PHYS                0x18002000u
#define CONSYS_HW_ID            0x00
#define CONSYS_FW_ID            0x04

static uint32_t
rd(vm_offset_t base, uint32_t offset)
{
	return *(volatile uint32_t *)(base + offset);
}

/*
 * Step 2a (connsys_step=2), reading only: CONNSYS's supplies as LK leaves them -- the PMIC MT6358's
 * LDOs VCN18, VCN28 and VCN33 (two enables: Bluetooth's and Wi-Fi's), each its enable register and its
 * analog register with the voltage selection (include/linux/mfd/mt6358/registers.h of the MT6768's
 * vendor kernel), read through the PMIC wrapper's WACS2 channel as hakios.mtk_keys.c reads the PMIC.
 * Android turns VCN18 and VCN33 on (consys_hw_vcn18_ctrl in mt6768.c) before it powers CONNSYS up.
 */
#define PWRAP_PHYS              0x1000d000u
#define PWRAP_WACS2_CMD         0xc20
#define PWRAP_WACS2_RDATA       0xc24
#define PWRAP_WACS2_VLDCLR      0xc28
#define PWRAP_FSM(r)            (((r) >> 16) & 7u)

static vm_offset_t connsys_pwrap;

static bool
connsys_pwrap_wait(uint32_t state)
{
	for (uint32_t spin = 0; spin < 100000; spin++) {
		if (PWRAP_FSM(rd(connsys_pwrap, PWRAP_WACS2_RDATA)) == state) {
			return true;
		}
	}
	return false;
}

static bool
connsys_pmic_read(uint32_t reg, uint32_t *value)
{
	if (!connsys_pwrap_wait(0)) {
		return false;
	}
	*(volatile uint32_t *)(connsys_pwrap + PWRAP_WACS2_CMD) = (reg >> 1) << 16;
	if (!connsys_pwrap_wait(6)) {
		return false;
	}
	*value = rd(connsys_pwrap, PWRAP_WACS2_RDATA) & 0xffff;
	*(volatile uint32_t *)(connsys_pwrap + PWRAP_WACS2_VLDCLR) = 1;
	return true;
}

static void
connsys_power_report(void)
{
	static const struct { const char *name; uint32_t enable, analog; } ldo[] = {
		{ "VCN18", 0x1c58, 0x1e8c },
		{ "VCN28", 0x1d88, 0x1e14 },
		{ "VCN33 (BT)", 0x1d1c, 0x1e28 },
		{ "VCN33 (Wi-Fi)", 0x1d2a, 0x1e28 },
	};

	connsys_pwrap = ml_io_map(PWRAP_PHYS, 0x1000);
	for (unsigned i = 0; i < sizeof(ldo) / sizeof(ldo[0]); i++) {
		uint32_t enable = 0xdead, analog = 0xdead;
		bool read = connsys_pmic_read(ldo[i].enable, &enable) && connsys_pmic_read(ldo[i].analog, &analog);

		hakios_host_keep_add("connsys: step 2: %s: %s, enable register %#x = %#x, analog %#x = %#x", ldo[i].name,
		    read ? ((enable & 1) ? "ON" : "off") : "NOT READ", ldo[i].enable, enable, ldo[i].analog, analog);
	}
}

static bool
connsys_pmic_write(uint32_t reg, uint32_t value)
{
	if (!connsys_pwrap_wait(0)) {
		return false;
	}
	*(volatile uint32_t *)(connsys_pwrap + PWRAP_WACS2_CMD) = (1u << 31) | (reg >> 1) << 16 | (value & 0xffff);
	return connsys_pwrap_wait(0);
}

/* a PMIC register's bits under `mask` set to `value`, read back; false if the wrapper did not answer */
static bool
connsys_pmic_update(uint32_t reg, uint32_t mask, uint32_t value)
{
	uint32_t now;

	return connsys_pmic_read(reg, &now) && connsys_pmic_write(reg, (now & ~mask) | (value & mask));
}

/*
 * Step 3 (connsys_step=3): the supplies on, as consys_hw_vcn18_ctrl, consys_hw_wifi/bt_vcn33_ctrl and
 * mtk_wcn_consys_hw_efuse_paldo_ctrl do it (mt6768.c, mtk_wcn_consys_hw.c) with values from the MT6358
 * regulator driver: VCN18 (fixed 1.8 V) and VCN33 to software control (their OP_EN registers = 1),
 * VCN33's voltage selection to 3.3 V (VCN33_ANA_CON0 bits 8-9 = 1; LK leaves 3, 3.5 V), its Bluetooth
 * and Wi-Fi enables, and VCN28 (fixed 2.8 V, for the efuse). Then all four read again.
 */
static void
connsys_supplies_on(void)
{
	bool ok = connsys_pmic_write(0x1c5a, 1) &&              /* LDO_VCN18_OP_EN: SW */
	    connsys_pmic_update(0x1c58, 1, 1) &&                /* LDO_VCN18_CON0: EN */
	    connsys_pmic_write(0x1d1e, 1) &&                    /* LDO_VCN33_OP_EN: SW */
	    connsys_pmic_update(0x1e28, 0x300, 0x100) &&        /* VCN33_ANA_CON0: VOSEL 3.3 V */
	    connsys_pmic_update(0x1d1c, 1, 1) &&                /* LDO_VCN33_CON0_0: EN (Bluetooth's) */
	    connsys_pmic_update(0x1d2a, 1, 1) &&                /* LDO_VCN33_CON0_1: EN (Wi-Fi's) */
	    connsys_pmic_update(0x1d88, 1, 1);                  /* LDO_VCN28_CON0: EN */

	delay(1000);
	hakios_host_keep_add("connsys: step 3: VCN18, VCN33 (3.3 V) and VCN28 turned on: %s", ok ? "written" : "THE WRAPPER DID NOT ANSWER");
	connsys_power_report();
}

/*
 * Step 4 (connsys_step=4): CONNSYS powered up, as consys_set_if_pinmux, consys_hw_reset_bit_set and the
 * hand-made branch of consys_hw_power_ctrl do it (mt6768.c; Android goes through the clock framework's
 * clk_scp_conn_main, which does the same), every wait bounded and said instead of endless: the pins to
 * the radio, the CPU held in reset, SPM's key, the on-domain's two parts powered (each waiting for its
 * ack), the bus clock on, isolation off, the off-domain's acks, the software reset let go, the bus
 * protections (AP to CONNSYS, CONNSYS to AP) taken off, SPM's apsrc/ddr settings; then CONNSYS's IP
 * version polled -- 0x10020501 is the MT6768's and says it lives. The clock buffer of co-clock mode
 * (WMT_SOC.cfg: co_clock_flag=1, Android's clk_buf driver) is not done yet.
 */
static bool
connsys_wait(vm_offset_t base, uint32_t offset, uint32_t bit, const char *what)
{
	for (unsigned n = 0; n < 10000; n++) {
		if (rd(base, offset) & bit) {
			return true;
		}
		delay(10);
	}
	hakios_host_keep_add("connsys: step 4: %s: no ack after 100 ms (%#x = %#x)", what, offset, rd(base, offset));
	return false;
}

static void
connsys_power_on(vm_offset_t spm)
{
	vm_offset_t pinmux = ml_io_map(0x10005000u, 0x1000), rgu = ml_io_map(0x10007000u, 0x1000);
	vm_offset_t infra = ml_io_map(0x10001000u, 0x1000), misc = ml_io_map(MCU_TOP_MISC_OFF_PHYS, 0x1000);
	uint32_t version = 0;

#define W(base, offset, value) (*(volatile uint32_t *)((base) + (offset)) = (value))
	W(pinmux, 0x3f0, (rd(pinmux, 0x3f0) & 0xffffff88u) | 0x11u);
	W(pinmux, 0x3e0, (rd(pinmux, 0x3e0) & 0x8888888fu) | 0x11111110u);
	delay(150);
	W(rgu, 0x18, rd(rgu, 0x18) | (1u << 12) | (0x88u << 24));            /* the CPU's software reset on */
	W(spm, 0x000, (rd(spm, 0x000) & 0xffffu) | 0x0b160001u);              /* SPM's write key */
	W(spm, SPM_TOP1_PWR_CTRL, rd(spm, SPM_TOP1_PWR_CTRL) | (1u << 2));    /* on-domain power */
	if (!connsys_wait(spm, SPM_PWR_STATUS, 1u << 1, "on-domain power")) {
		return;
	}
	W(spm, SPM_TOP1_PWR_CTRL, rd(spm, SPM_TOP1_PWR_CTRL) | (1u << 3));    /* ... its second part */
	if (!connsys_wait(spm, SPM_PWR_STATUS_2ND, 1u << 1, "on-domain power, second part")) {
		return;
	}
	W(spm, SPM_TOP1_PWR_CTRL, rd(spm, SPM_TOP1_PWR_CTRL) & ~(1u << 4));   /* the bus clock */
	W(spm, SPM_TOP1_PWR_CTRL, rd(spm, SPM_TOP1_PWR_CTRL) & ~(1u << 1));   /* isolation off */
	/*
	 * The domain's own reset let go (PWR_RST_B, bit 0), as Linux's scpsys does it
	 * (spm_mtcmos_ctrl_conn_pwr, drivers/clk/mediatek/clk-mt6768-pg.c) -- WMT's hand-made branch
	 * leaves it out, and without it the off-domain's ack (bit 30) never came (run 305).
	 */
	W(spm, SPM_TOP1_PWR_CTRL, rd(spm, SPM_TOP1_PWR_CTRL) | (1u << 0));
	(void)connsys_wait(spm, SPM_PWR_STATUS, 1u << 30, "off-domain power");
	(void)connsys_wait(spm, SPM_PWR_STATUS_2ND, 1u << 30, "off-domain power, second part");
	W(rgu, 0x18, (rd(rgu, 0x18) & ~(1u << 9)) | (0x88u << 24));          /* ap_sw_rst_b: let go */
	/* the bus protections, through their CLR registers as spm_mtcmos_ctrl_conn_bus_prot does */
	W(infra, 0x2a4, (1u << 14) | (1u << 16));
	W(infra, 0x2a4, 1u << 13);
	W(infra, 0x2ac, 1u << 18);
	delay(10);
	W(spm, 0x6f8, 0x5);
	W(spm, 0x6fc, 0x50505);
#undef W
	delay(10);
	hakios_host_keep_add("connsys: step 4: powered: TOP1_PWR_CTRL %#x, PWR_STATUS %#x/%#x, bus protection %#x (%#x) and %#x (%#x)",
	    rd(spm, SPM_TOP1_PWR_CTRL), rd(spm, SPM_PWR_STATUS), rd(spm, SPM_PWR_STATUS_2ND), rd(infra, 0x220), rd(infra, 0x228),
	    rd(infra, 0x250), rd(infra, 0x258));
	/*
	 * Linux takes the domain as on with bit 1 alone (CONN_PWR_STA_MASK, clk-mt6768-pg.c) and Android reads
	 * the IP version right after; WMT's header has the off-domain's ack at bit 30, but on this phone it is
	 * bit 31 that comes once PWR_RST_B is let go (0x06c57c2e -> 0x86c57c2e, run 306). Either will do.
	 */
	{
		uint32_t both = rd(spm, SPM_PWR_STATUS) & rd(spm, SPM_PWR_STATUS_2ND);

		if (!(both & (1u << 1)) || !(both & ((1u << 30) | (1u << 31)))) {
			/* (a read of a block that is not powered can hang the bus, and the phone with it) */
			hakios_host_keep_add("connsys: step 4: the off-domain is not powered (%#x): its registers are not read", both);
			return;
		}
		hakios_host_keep_add("connsys: step 4: CONN_HIF_ON CPUPCR %#x", rd(ml_io_map(0x18007000u, 0x1000), 0x104));
	}
	for (unsigned n = 0; n < 10 && version != CONSYS_IP_VER_ID; n++) {
		version = rd(misc, CONSYS_IP_VER);
		delay(20000);
	}
	hakios_host_keep_add("connsys: step 4: IP version %#x: %s", version,
	    version == CONSYS_IP_VER_ID ? "CONNSYS LIVES (the MT6768's)" : "not the expected 0x10020501");
}

/*
 * Step 5 (connsys_step=5): the firmware into CONNSYS's memory, as mtk_wcn_soc_rom_patch_dwn
 * (core/wmt_ic_soc.c) does it, and its CPU let go. Each of the phone's soc1_0_*_hdr.bin files
 * (/vendor/firmware, in the system image as /fw/connsys: userland/build.sh) is a 48-byte struct
 * wmt_rom_patch (core/include/wmt_core.h) -- whose u4PatchAddr is where CONNSYS sees it, 0xf0000000 being
 * the start of its memory (CONSYS_EMI_FW_PHY_BASE against CONSYS_EMI_AP_PHY_BASE, include/mt6768.h) --
 * and then its body, copied there: the MCU's RAM code at 0, its ROM patch at 0x1c000, Bluetooth's at
 * 0x80000, Wi-Fi's at 0x140000. The memory is Android's 4 MB consys-reserve-memory; here at 0x51000000
 * (16 MB aligned, as INFRA's remapping register takes it: bits 24 and up), clear of the loader's and the
 * sensor hub's. Then INFRA+0x380 told where it is and +0x388 the peripheral window
 * (consys_emi_set_remapping_reg), the CPU's software reset let go (consys_hw_reset_bit_set), and
 * MCU+0x600 polled for 0x1D1E -- what Android waits for. In a thread: the files are there only once
 * the root is mounted.
 */
/* kern.hakios.connsys: how step 5 ended, for a job to read (the kept lines run out on a long run) */
static char connsys_said[1000] = "not run";
SYSCTL_DECL(_kern_hakios);
SYSCTL_STRING(_kern_hakios, OID_AUTO, connsys, CTLFLAG_RD | CTLFLAG_LOCKED, connsys_said, sizeof(connsys_said), "");

#define CONNSYS_EMI_PHYS        0x51000000u
#define CONNSYS_EMI_SIZE        0x400000u

static uint8_t *
connsys_file(const char *name, uint32_t *size)
{
	char path[96];
	vfs_context_t context = vfs_context_kernel();
	vnode_t vnode = NULLVP;
	struct vnode_attr attributes;
	uint8_t *data;
	int left = 0;

	snprintf(path, sizeof(path), "/fw/connsys/%s", name);
	if (vnode_lookup(path, 0, &vnode, context) != 0) {
		return NULL;
	}
	VATTR_INIT(&attributes);
	VATTR_WANTED(&attributes, va_data_size);
	if (vnode_getattr(vnode, &attributes, context) != 0 || attributes.va_data_size < 48 ||
	    attributes.va_data_size > CONNSYS_EMI_SIZE) {
		vnode_put(vnode);
		return NULL;
	}
	data = kalloc_data(attributes.va_data_size, Z_WAITOK);
	if (data != NULL && (vn_rdwr(UIO_READ, vnode, (caddr_t)data, (int)attributes.va_data_size, 0, UIO_SYSSPACE, 0,
	    vfs_context_ucred(context), &left, vfs_context_proc(context)) != 0 || left != 0)) {
		kfree_data(data, attributes.va_data_size);
		data = NULL;
	}
	vnode_put(vnode);
	*size = (uint32_t)attributes.va_data_size;
	return data;
}

extern void delay_for_interval(uint32_t interval, uint32_t scale_factor);
extern kern_return_t thread_terminate(thread_t thread);

/*
 * Step 6 (connsys_step=6): BTIF, the bridge WMT talks to CONNSYS through (btif@1100c000 in the device
 * tree, run 313): its clock on (infracfg's IFR2 gate, bit 31: CLR at +0x84 -- clk-mt6768.c, ifr_btif)
 * and the hardware set up as hal_btif_hw_init does it (drivers/misc/mediatek/btif/common/btif_plat.c):
 * normal mode, the new handshake, both FIFOs cleared, no DMA, auto-reset -- before CONNSYS is powered,
 * as Android does it; once CONNSYS runs, whatever it sends is read for a second (PIO: LSR bit 0, RBR).
 */
#define BTIF_PHYS               0x1100c000u

static vm_offset_t connsys_btif;

static void
connsys_btif_init(void)
{
	vm_offset_t infra = ml_io_map(0x10001000u, 0x1000);

	*(volatile uint32_t *)(infra + 0x84) = 1u << 31;                /* ifr_btif ungated */
	delay(10);
	connsys_btif = ml_io_map(BTIF_PHYS, 0x1000);
	*(volatile uint32_t *)(connsys_btif + 0x0c) = 0;                /* FAKELCR: normal mode */
	*(volatile uint32_t *)(connsys_btif + 0x6c) |= 1;               /* HANDSHAKE: the new one */
	*(volatile uint32_t *)(connsys_btif + 0x08) = 1u << 1;          /* FIFOCTRL: clear Rx */
	*(volatile uint32_t *)(connsys_btif + 0x08) = 0;
	*(volatile uint32_t *)(connsys_btif + 0x08) = 1u << 2;          /* ... and Tx */
	*(volatile uint32_t *)(connsys_btif + 0x08) = 0;
	*(volatile uint32_t *)(connsys_btif + 0x4c) = 1u << 2;          /* DMA_EN: no DMA, auto-reset */
	*(volatile uint32_t *)(connsys_btif + 0x04) = 0;                /* IER: none (polled) */
	hakios_host_keep_add("connsys: step 6: BTIF on: clock status %#x, LSR %#x", rd(infra, 0x90), rd(connsys_btif, 0x14));
}

static uint32_t connsys_step;

static void
connsys_btif_listen(const char *what)
{
	char line[200];
	size_t used = 0;
	unsigned got = 0;

	if (connsys_btif == 0) {
		return;
	}
	used = (size_t)snprintf(line, sizeof(line), "%s:", what);
	for (unsigned t = 0; t < 1000 && got < 48; t++) {
		while ((rd(connsys_btif, 0x14) & 1) && got < 48) {
			used += (size_t)snprintf(line + used, sizeof(line) - used, " %02x", rd(connsys_btif, 0x0) & 0xff);
			got++;
		}
		delay(1000);
	}
	if (got == 0) {
		snprintf(line + used, sizeof(line) - used, " nothing (LSR %#x)", rd(connsys_btif, 0x14));
	}
	hakios_host_keep_add("connsys: %s", line);
	strlcat(connsys_said, " | ", sizeof(connsys_said));
	strlcat(connsys_said, line, sizeof(connsys_said));
}

/*
 * Step 7 (connsys_step=7): the first thing WMT asks CONNSYS (wmt_core_hw_check): its chip ID, a register
 * read of 0x80000008 (GEN_HCR) -- WMT_SET_REG_CMD with op 2. Before full mode STP is in its "mandatory"
 * mode on BTIF (wmt_core_stp_init: MTKSTP_BTIF_MAND_MODE): a 4-byte header -- 0x80 + seq << 3, WMT's task
 * (4) << 4 + the length's high bits, its low byte, 0 -- the payload and a CRC of 00 00 (stp_core.c,
 * mtk_wcn_stp_send_data, the mand branch). CONNSYS is woken first as hal_btif_raise_wak_sig does it
 * (BTIF_WAK +0x64 low, ~150 us, high); bytes go to THR 16 at a time, each time the FIFO is empty (LSR bit 6).
 */
static void
connsys_btif_write(const uint8_t *bytes, unsigned length)
{
	for (unsigned at = 0; at < length;) {
		for (unsigned t = 0; t < 1000 && !(rd(connsys_btif, 0x14) & (1u << 6)); t++) {
			delay(10);
		}
		for (unsigned n = 0; n < 16 && at < length; n++, at++) {
			*(volatile uint8_t *)(connsys_btif + 0x0) = bytes[at];
		}
	}
}

static void
connsys_btif_wake(void)
{
	*(volatile uint32_t *)(connsys_btif + 0x64) &= ~1u;
	delay(150);
	*(volatile uint32_t *)(connsys_btif + 0x64) |= 1u;
	delay(1000);
}

static void
connsys_btif_send(const uint8_t *bytes, unsigned length)
{
	connsys_btif_wake();
	connsys_btif_write(bytes, length);
}

/*
 * Step 8 (connsys_step=8): STP into its full mode, as _wmt_soc_mode_ctrl (wmt_ic_soc.c) does it: the chip's STP
 * defaults asked for (init_table_1_2) and set (init_table_4) in the mandatory mode, the host switched, and
 * the defaults asked for again (init_table_5) in full mode -- where a frame's header is 0x80 + seq << 3 + ack,
 * the task << 4 + the length's high bits, its low byte and the sum of those three; then the payload and its
 * CRC-16 (the kernel's crc16, 0xA001 reflected, from 0), low byte first. A frame with a payload is answered by
 * a header-only one carrying the next ack (stp_send_ack). Each answer is said.
 */
static bool connsys_stp_full;
static uint8_t connsys_stp_seq, connsys_stp_ack;
static unsigned connsys_doing = 7;     /* the step a WMT exchange is said under */

static uint16_t
connsys_crc16(const uint8_t *bytes, unsigned length)
{
	uint16_t crc = 0;

	for (unsigned i = 0; i < length; i++) {
		crc ^= bytes[i];
		for (int bit = 0; bit < 8; bit++) {
			crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
		}
	}
	return crc;
}

static int
connsys_btif_byte(unsigned wait_ms)
{
	for (unsigned t = 0; t <= wait_ms * 10; t++) {
		if (rd(connsys_btif, 0x14) & 1) {
			return (int)(rd(connsys_btif, 0x0) & 0xff);
		}
		delay(100);
	}
	return -1;
}

static void
connsys_stp_send(const uint8_t *payload, unsigned length)
{
	uint8_t frame[4 + 64 + 2];
	uint16_t crc = connsys_stp_full ? connsys_crc16(payload, length) : 0;

	frame[0] = (uint8_t)(0x80 | (connsys_stp_seq << 3) | (connsys_stp_full ? connsys_stp_ack : 0));
	frame[1] = (uint8_t)((4 << 4) | ((length >> 8) & 0xf));
	frame[2] = (uint8_t)length;
	frame[3] = connsys_stp_full ? (uint8_t)(frame[0] + frame[1] + frame[2]) : 0;
	memcpy(frame + 4, payload, length);
	frame[4 + length] = (uint8_t)crc;
	frame[5 + length] = (uint8_t)(crc >> 8);
	connsys_btif_send(frame, 6 + length);
	connsys_stp_seq = (connsys_stp_seq + 1) & 7;
}

/* the next frame with a payload (header-only ones, CONNSYS's acks, skipped); its payload's length, or -1 */
static int
connsys_stp_receive(uint8_t *payload, unsigned room, char *heard, size_t heard_room)
{
	size_t used = strlen(heard);

	for (;;) {
		int b0 = connsys_btif_byte(1000), b1, b2, b3;
		unsigned length;

		if (b0 < 0) {
			return -1;
		}
		if (b0 == 0x55 || !(b0 & 0x80) || (b0 & 0x40)) {
			used += (size_t)snprintf(heard + used, heard_room - used, " ?%02x", b0);
			continue;
		}
		if ((b1 = connsys_btif_byte(50)) < 0 || (b2 = connsys_btif_byte(50)) < 0 || (b3 = connsys_btif_byte(50)) < 0) {
			return -1;
		}
		length = ((unsigned)(b1 & 0xf) << 8) | (unsigned)b2;
		if (length == 0) {
			used += (size_t)snprintf(heard + used, heard_room - used, " [ack %u]", b0 & 7);
			continue;
		}
		for (unsigned i = 0; i < length + 2; i++) {
			int b = connsys_btif_byte(50);

			if (b < 0) {
				return -1;
			}
			if (i < length && i < room) {
				payload[i] = (uint8_t)b;
			}
		}
		if (connsys_stp_full) {
			uint8_t ack[4];

			connsys_stp_ack = (uint8_t)((((b0 >> 3) & 7) + 1) & 7);
			ack[0] = (uint8_t)(0x80 | connsys_stp_ack);
			ack[1] = 0;
			ack[2] = 0;
			ack[3] = ack[0];
			connsys_btif_send(ack, 4);
		}
		return (int)(length < room ? length : room);
	}
}

static bool
connsys_wmt(const char *what, const uint8_t *command, unsigned length, const uint8_t *expected, unsigned expected_length)
{
	char line[220];
	uint8_t answer[32];
	int got;
	size_t used;

	used = (size_t)snprintf(line, sizeof(line), "%s:", what);
	/* (the first command is answered at once, after a second wake-up or not within 3 s -- runs 316-318: sent again) */
	for (unsigned try = 0; try < (connsys_stp_full ? 1 : 5); try++) {
		connsys_stp_send(command, length);
		if ((got = connsys_stp_receive(answer, sizeof(answer), line, sizeof(line))) >= 0) {
			break;
		}
		strlcat(line, " .", sizeof(line));
	}
	used = strlen(line);
	if (got < 0) {
		snprintf(line + used, sizeof(line) - used, " no answer (LSR %#x)", rd(connsys_btif, 0x14));
	}
	for (int i = 0; i < got; i++) {
		used += (size_t)snprintf(line + used, sizeof(line) - used, " %02x", answer[i]);
	}
	if (got >= 0) {
		snprintf(line + used, sizeof(line) - used, (unsigned)got >= expected_length && memcmp(answer, expected, expected_length) == 0 ?
		    " (as expected)" : " (NOT as expected)");
	}
	hakios_host_keep_add("connsys: step %u: %s", connsys_doing, line);
	strlcat(connsys_said, " | ", sizeof(connsys_said));
	strlcat(connsys_said, line, sizeof(connsys_said));
	return got >= 0 && (unsigned)got >= expected_length && memcmp(answer, expected, expected_length) == 0;
}

/*
 * Steps 9 and 10 (connsys_step=9, 10): the MCU's patch, which the MT6768 takes over BTIF and not in its memory
 * (pda_dl_patch_flag = 1 in platform/mt6768.c): in STP's mandatory mode, before the full one, as
 * mtk_wcn_soc_sw_init does it for a chip with that flag -- patch, WMT reset, and only then _wmt_soc_mode_ctrl.
 * The options of this chip (wmt_core_hw_check, "options = 3180da8" in Android's log) have no DLM table and no
 * MCU clock tables around it, and OPT_PATCH_CHECKSUM: the patch is the file less its 28-byte header (WMT_PATCH)
 * and 2 bytes of checksum, 25680 bytes of soc1_0_patch_mcu_1a_1_hdr.bin -- the "frag(26, 680)" Android logs.
 *
 * Where it goes is what Android's wmt_launcher gives the driver (WMT_IOCTL_SET_PATCH_INFO, addRess[4]): the
 * phone's /vendor/bin/wmt_launcher (sha256 39772aec...) reads 16 bytes, seeks to 22, reads 2 (the sequence,
 * its second byte compared with the patch's number) and then the 4 at offset 24 -- 11 c0 01 00 in this file,
 * whose upper half is the "PhVer=0x0001" the driver prints of the same field. mtk_wcn_soc_pda_patch_dwn
 * then sends, all in the mandatory mode:
 *   WMT_PATCH_PDA_CFG_CMD  01 01 11 00 00, the address (4), the size (4, little-endian), a scramble key of 0,
 *                          2f 02 02 00 -- answered 02 01 01 00 00;
 *   a 12-byte header       the size + 12, little-endian, then zeros -- raw, no STP frame around it;
 *   the patch              raw, in pieces of 1000 bytes;
 * and then init_table_3: WMT reset, 01 07 01 00 04, answered 02 07 01 00 00.
 * Step 9 reads the file and says what would be sent; step 10 sends it. True when that went as it should.
 */
static bool
connsys_pda_patch(void)
{
	static const uint8_t pda_done[] = { 0x02, 0x01, 0x01, 0x00, 0x00 };
	static const uint8_t reset[] = { 0x01, 0x07, 0x01, 0x00, 0x04 };
	static const uint8_t reset_done[] = { 0x02, 0x07, 0x01, 0x00, 0x00 };
	uint8_t pda[21] = {
		0x01, 0x01, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2f, 0x02, 0x02, 0x00,
	};
	uint8_t head[12] = { 0 };
	uint32_t size = 0, body, pieces = 0;
	uint8_t *file = connsys_file("soc1_0_patch_mcu_1a_1_hdr.bin", &size);
	bool ok;

	connsys_doing = 9;
	if (file == NULL || size <= 28 + 2) {
		hakios_host_keep_add("connsys: step 9: /fw/connsys/soc1_0_patch_mcu_1a_1_hdr.bin: not read");
		if (file != NULL) {
			kfree_data(file, size);
		}
		return false;
	}
	body = size - 28 - 2;
	memcpy(&pda[5], file + 24, 4);
	for (unsigned i = 0; i < 4; i++) {
		pda[9 + i] = (uint8_t)(body >> (8 * i));
		head[i] = (uint8_t)((body + sizeof(head)) >> (8 * i));
	}
	hakios_host_keep_add("connsys: step 9: patch %.16s (%.4s, versions %02x%02x/%02x%02x): %u bytes, %u pieces of 1000, the last %u; "
	    "to %02x %02x %02x %02x", (const char *)file, (const char *)file + 16, file[20], file[21], file[22], file[23], body,
	    (body + 999) / 1000, body - (body - 1) / 1000 * 1000, pda[5], pda[6], pda[7], pda[8]);
	if (connsys_step < 10) {
		kfree_data(file, size);
		return false;
	}
	connsys_doing = 10;
	ok = connsys_wmt("PDA configuration", pda, sizeof(pda), pda_done, sizeof(pda_done));
	if (ok) {
		/* one stream, woken once: run 356 woke CONNSYS before each piece (BTIF_WAK low and high 27 times inside the
		 * download) and the WMT reset after it went unanswered; Android raises that signal only out of sleep, and there
		 * is none during the download */
		connsys_btif_wake();
		connsys_btif_write(head, sizeof(head));
		for (uint32_t at = 0; at < body; at += 1000, pieces++) {
			connsys_btif_write(file + 28 + 2 + at, body - at < 1000 ? body - at : 1000);
		}
		for (unsigned t = 0; t < 1000 && !(rd(connsys_btif, 0x14) & (1u << 6)); t++) {
			delay(10);
		}
		hakios_host_keep_add("connsys: step 10: %u pieces of the patch sent, LSR %#x", pieces, rd(connsys_btif, 0x14));
		delay_for_interval(20, NSEC_PER_MSEC);
		ok = connsys_wmt("WMT reset", reset, sizeof(reset), reset_done, sizeof(reset_done));
	}
	kfree_data(file, size);
	return ok;
}

static void
connsys_stp_full_mode(void)
{
	static const uint8_t query[] = { 0x01, 0x04, 0x01, 0x00, 0x04 };
	static const uint8_t query_default[] = { 0x02, 0x04, 0x06, 0x00, 0x00, 0x04, 0x11, 0x00, 0x00, 0x00 };
	static const uint8_t set[] = { 0x01, 0x04, 0x05, 0x00, 0x03, 0xDB, 0x0E, 0x68, 0x01 };
	static const uint8_t set_done[] = { 0x02, 0x04, 0x02, 0x00, 0x00, 0x03 };
	static const uint8_t query_full[] = { 0x02, 0x04, 0x06, 0x00, 0x00, 0x04, 0xDB, 0x0E, 0x68, 0x01 };

	static const uint8_t chip_id[] = {
		0x01, 0x08, 0x10, 0x00, 0x02, 0x01, 0x00, 0x01, 0x08, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
	};
	static const uint8_t chip_id_read[] = { 0x02, 0x08, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x80 };

	if (!connsys_wmt("WMT read 0x80000008", chip_id, sizeof(chip_id), chip_id_read, sizeof(chip_id_read)) ||
	    connsys_step < 8 || (connsys_step >= 9 && !connsys_pda_patch())) {
		return;
	}
	connsys_doing = 8;
	if (!connsys_wmt("STP defaults", query, sizeof(query), query_default, sizeof(query_default)) ||
	    !connsys_wmt("STP set", set, sizeof(set), set_done, sizeof(set_done))) {
		return;
	}
	connsys_stp_full = true;
	connsys_stp_seq = 0;
	connsys_stp_ack = 0;
	delay(10000);
	connsys_wmt("full mode, STP asked again", query, sizeof(query), query_full, sizeof(query_full));
}

static void
connsys_firmware_thread(__unused void *parameter, __unused wait_result_t result)
{
	static const char *const files[] = {
		"soc1_0_ram_mcu_1a_1_hdr.bin", "soc1_0_patch_mcu_1a_1_hdr.bin", "soc1_0_ram_bt_1a_1_hdr.bin",
		"soc1_0_ram_wifi_1a_1_hdr.bin",
	};
	vm_offset_t emi, infra = ml_io_map(0x10001000u, 0x1000), rgu = ml_io_map(0x10007000u, 0x1000);
	vm_offset_t mcu = ml_io_map(MCU_PHYS, 0x1000), hif = ml_io_map(0x18007000u, 0x1000);
	uint32_t version = 0, copied = 0;

	for (unsigned s = 0; s < 240 && rootvnode == NULLVP; s++) {
		delay_for_interval(1, NSEC_PER_SEC);
	}
	delay_for_interval(5, NSEC_PER_SEC);
	/* (each file's own window: 4 MB at once was more than the kernel's I/O map had, run 308) */
	for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		uint32_t size = 0, address, offset;
		uint8_t *file;

		/* from step 9 the MCU's patch goes over BTIF (connsys_pda_patch), not into EMI: Android puts only the RAM
		 * images there ("[Rom Patch]" ram_bt, ram_wifi, ram_mcu in its log) */
		if (connsys_step >= 9 && strcmp(files[i], "soc1_0_patch_mcu_1a_1_hdr.bin") == 0) {
			continue;
		}
		file = connsys_file(files[i], &size);

		if (file == NULL) {
			hakios_host_keep_add("connsys: step 5: /fw/connsys/%s: not read", files[i]);
			thread_terminate(current_thread());
			return;
		}
		memcpy(&address, file + 0x18, 4);
		offset = address & 0x00ffff00u;
		if (offset + size - 48 > CONNSYS_EMI_SIZE) {
			hakios_host_keep_add("connsys: step 5: %s: %u bytes at %#x do not fit", files[i], size - 48, offset);
			kfree_data(file, size);
			thread_terminate(current_thread());
			return;
		}
		emi = ml_io_map(CONNSYS_EMI_PHYS + offset, round_page(size - 48));
		for (uint32_t at = 48; at < size; at += 4) {
			uint32_t word = 0;

			memcpy(&word, file + at, size - at >= 4 ? 4 : size - at);
			*(volatile uint32_t *)(emi + (at - 48)) = word;
		}
		/* (not unmapped: ml_io_map's entries are permanent, ml_io_unmap panics on them -- run 310; the windows,
		 * ~900 KB in all, and the sensor hub's 256 KB FIFO fit the I/O map together) */
		hakios_host_keep_add("connsys: step 5: %s (%.14s): %u bytes at EMI+%#x (its address %#x)", files[i], (const char *)file,
		    size - 48, offset, address);
		copied++;
		kfree_data(file, size);
	}
	__asm__ volatile ("dsb sy" ::: "memory");
	*(volatile uint32_t *)(infra + 0x380) = (rd(infra, 0x380) & 0xfffff000u) | ((CONNSYS_EMI_PHYS >> 24) & 0xfffu);
	*(volatile uint32_t *)(infra + 0x388) = (rd(infra, 0x388) & 0xf000ffffu) | (((0x10003000u >> 24) & 0xfffu) << 16);
	*(volatile uint32_t *)(rgu + 0x18) = (rd(rgu, 0x18) & ~(1u << 12)) | (0x88u << 24);     /* the CPU let go */
	for (unsigned n = 0; n < 50 && version != 0x1D1E; n++) {
		delay_for_interval(20, NSEC_PER_MSEC);
		version = rd(mcu, 0x600);
	}
	snprintf(connsys_said, sizeof(connsys_said), "step 5: %u files in; remap %#x/%#x; MCU+0x600 %#x (%s), CPUPCR %#x then %#x",
	    copied, rd(infra, 0x380), rd(infra, 0x388), version, version == 0x1D1E ? "CONNSYS'S ROM TOOK IT" : "not 0x1D1E",
	    rd(hif, 0x104), (delay_for_interval(10, NSEC_PER_MSEC), rd(hif, 0x104)));
	hakios_host_keep_add("connsys: %s", connsys_said);
	connsys_btif_listen("step 6: BTIF in a second");
	if (connsys_step >= 7) {
		connsys_stp_full_mode();
	}
	thread_terminate(current_thread());
}

void
hakios_mtk_connsys_init(void)
{
	uint32_t step = 0, ctrl, status, status2;
	vm_offset_t spm, misc, mcu;
	bool on;

	if (!PE_parse_boot_argn("connsys_step", &step, sizeof(step)) || step < 1) {
		return;
	}
	connsys_step = step;
	spm = ml_io_map(SPM_PHYS, 0x1000);
	ctrl = rd(spm, SPM_TOP1_PWR_CTRL);
	status = rd(spm, SPM_PWR_STATUS);
	status2 = rd(spm, SPM_PWR_STATUS_2ND);
	on = (status & status2 & PWR_STATUS_CONN) && (status & status2 & PWR_STATUS_CONN_TOP2);
	hakios_host_keep_add("connsys: step 1: SPM TOP1_PWR_CTRL %#x (reset %s, iso %s, on %u/%u, clock %u), PWR_STATUS %#x/%#x: "
	    "CONNSYS %s", ctrl, (ctrl & 1) ? "released" : "held", (ctrl & 2) ? "on" : "off", (ctrl >> 2) & 1, (ctrl >> 3) & 1,
	    (ctrl >> 4) & 1, status, status2, on ? "ON" : "off");
	if (step >= 2) {
		connsys_power_report();
	}
	if (step >= 3 && !on) {
		connsys_supplies_on();
	}
	if (step >= 4 && !on) {
		if (step >= 6) {
			connsys_btif_init();
		}
		connsys_power_on(spm);
		if (step >= 5 && (rd(spm, SPM_PWR_STATUS) & 2)) {
			thread_t thread;

			if (kernel_thread_start(connsys_firmware_thread, NULL, &thread) == KERN_SUCCESS) {
				thread_deallocate(thread);
			}
		}
		return;
	}
	if (!on) {
		hakios_host_keep_add("connsys: step 1: powered off, so its own registers are not read (as expected straight from LK)");
		return;
	}
	misc = ml_io_map(MCU_TOP_MISC_OFF_PHYS, 0x1000);
	mcu = ml_io_map(MCU_PHYS, 0x1000);
	hakios_host_keep_add("connsys: step 1: IP version %#x (%s), configuration %#x, hardware %#x, firmware %#x",
	    rd(misc, CONSYS_IP_VER), rd(misc, CONSYS_IP_VER) == CONSYS_IP_VER_ID ? "the MT6768's" : "NOT the expected 0x10020501",
	    rd(misc, CONSYS_CONF_ID) & 0xF, rd(mcu, CONSYS_HW_ID) & 0xFFFF, rd(mcu, CONSYS_FW_ID) & 0xFFFF);
}
