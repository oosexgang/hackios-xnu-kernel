/*
 * The touch screen of the OnePlus 6T (fajita): a Synaptics S3320-family controller speaking RMI4 over
 * I2C (Android's "synaptics-rmi-ts@20", compatible "HWK,synaptics,s3320"). Every address and number
 * below belongs to this phone, from its own device tree (devices/oneplus-6t/README.md):
 *
 *   I2C        the serial engine 12 (qupv3_se12_i2c at 0xa90000, function qup12 on GPIO 49 data and
 *              50 clock, no pulls of its own), address 0x20; done by hand, as hakios.haptic.c does
 *   reset      GPIO 99, pulled up, high = running
 *   interrupt  GPIO 125, pulled up, low when the chip has something to say
 *   power      1.8 V switched by GPIO 88 (synaptics,1v8-gpio), which also feeds the bus's pull-ups; 3.008 V
 *              from the PM8998's LDO28 (vdd_2v8-supply, synaptics,avdd-voltage; nothing else is on it),
 *              switched through RPMh. Out of the boot loader both are off.
 *
 * The SDM845's TLMM is in three tiles, and each pin's registers are in its own (Linux
 * drivers/pinctrl/qcom/pinctrl-sdm845.c): 49, 50 and 99 in the north one, 88 and 125 in the east one.
 *
 * Nothing here is done unless a boot-arg asks for it:
 *
 *   synaptics=1   a look: the pins, and whether anything answers at 0x20 (one read of the page
 *                 register). Nothing is powered or reset.
 *   synaptics=2   ... and, if nothing answered, the chip powered as Android's driver does it: the reset
 *                 held low, the 3.0 V on, the 1.8 V on, and the reset let go 10 ms later, the chip
 *                 given 200 ms to start; then the
 *                 RMI4 page description tables read, and F01's identity and status. The chip's
 *                 registers are only read: the one write is the page select (0xff), which RMI4 needs
 *                 to read past page 0.
 *
 * At the run's end (and on a panic's way out) the two pins this changes are put back as they were and
 * the 3.0 V is switched off again.
 */
#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/hakios.input.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern kern_return_t thread_terminate(thread_t thread);
extern void loader_log_putc(char c);

static vm_offset_t tlmm;
static uint32_t level;
static bool powered, leaving;

/* every line to the loader's log as well (and to the console when that may be written) */
static int say(const char *format, ...) __printflike(1, 2);

static int
say(const char *format, ...)
{
	char line[300];
	va_list list;
	int length;

	va_start(list, format);
	length = vsnprintf(line, sizeof(line), format, list);
	va_end(list);
	for (const char *c = line; *c != 0; c++) {
		loader_log_putc(*c);
	}
	if (!leaving && ml_get_interrupts_enabled()) {
		printf("%s", line);
	}
	return length;
}

static inline uint32_t
rd(vm_offset_t address)
{
	return *(volatile uint32_t *)address;
}

static inline void
wr(vm_offset_t address, uint32_t value)
{
	*(volatile uint32_t *)address = value;
}

static void
spin_us(uint32_t us)
{
	uint64_t until;

	nanoseconds_to_absolutetime((uint64_t)us * NSEC_PER_USEC, &until);
	until += mach_absolute_time();
	while (mach_absolute_time() < until) {
	}
}

static void
wait_ms(uint32_t ms)
{
	if (leaving) {
		spin_us(ms * 1000);
	} else {
		delay_for_interval(ms, NSEC_PER_MSEC);
	}
}

/* ---- pins ------------------------------------------------------------------------------------------ */

#define TLMM_PHYS               0x03400000UL
#define TILE_NORTH              0x500000UL
#define TILE_EAST               0x100000UL
#define CFG_PULL_UP             3
#define CFG_OUTPUT              (1u << 9)

#define PIN_SDA                 49      /* the serial engine's first pin is the data line, the second the clock */
#define PIN_SCL                 50
#define PIN_RESET               99
#define PIN_1V8                 88
#define PIN_INT                 125

static vm_offset_t
tile_of(unsigned pin)
{
	return pin == PIN_1V8 || pin == PIN_INT ? TILE_EAST : TILE_NORTH;
}

/*
 * Each pin's own page, mapped alone: the whole TLMM is 12 MB, and a mapping that size fails at boot
 * ("kmem_alloc(..., 12582912, ...): failed with 3", a panic) where redwood's 1 MB one does not.
 */
static const unsigned used_pins[] = { PIN_SDA, PIN_SCL, PIN_RESET, PIN_1V8, PIN_INT };
static vm_offset_t pin_page[sizeof(used_pins) / sizeof(used_pins[0])];

static void
map_pins(void)
{
	for (unsigned i = 0; i < sizeof(used_pins) / sizeof(used_pins[0]); i++) {
		pin_page[i] = ml_io_map(TLMM_PHYS + tile_of(used_pins[i]) + 0x1000UL * used_pins[i], 0x1000);
	}
	tlmm = pin_page[0];
}

static vm_offset_t
page_of(unsigned pin)
{
	for (unsigned i = 0; i < sizeof(used_pins) / sizeof(used_pins[0]); i++) {
		if (used_pins[i] == pin) {
			return pin_page[i];
		}
	}
	panic("synaptics: gpio%u is not one of this driver's", pin);
}

#define GPIO_CFG(n)             (page_of(n))            /* [1:0] pull, [5:2] function, [8:6] drive, [9] output */
#define GPIO_IO(n)              (page_of(n) + 4)        /* bit 0 in, bit 1 out */

static void
show_pins(const char *when)
{
	static const struct {
		unsigned pin;
		const char *what;
	} pins[] = { { PIN_SDA, "I2C data" }, { PIN_SCL, "I2C clock" }, { PIN_RESET, "reset" }, { PIN_1V8, "1.8 V" },
		     { PIN_INT, "interrupt" } };

	say("synaptics: the pins %s:\n", when);
	for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
		uint32_t cfg = rd(GPIO_CFG(pins[i].pin)), io = rd(GPIO_IO(pins[i].pin));

		say("synaptics:   gpio%u (%s): function %u, pull %u, %u mA, %s, reads %u\n", pins[i].pin, pins[i].what,
		    (cfg >> 2) & 15, cfg & 3, (((cfg >> 6) & 7) + 1) * 2,
		    (cfg & CFG_OUTPUT) ? (io & 2 ? "drives high" : "drives low") : "input", io & 1);
	}
}

/* what the power and reset pins were when this found them, for putting back */
static uint32_t found_cfg[2], found_io[2];

static void
drive(unsigned pin, bool high)
{
	wr(GPIO_IO(pin), high ? 2 : 0);
	wr(GPIO_CFG(pin), CFG_OUTPUT);
}

/* ---- I2C by hand (as in hakios.haptic.c) ---------------------------------------------------------- */

static void
half_bit(void)
{
	spin_us(5);                     /* 100 kHz at most */
}

static void
line(unsigned pin, bool high)
{
	wr(GPIO_IO(pin), 0);
	wr(GPIO_CFG(pin), CFG_PULL_UP | (high ? 0 : CFG_OUTPUT));
}

static bool
level_of(unsigned pin)
{
	return rd(GPIO_IO(pin)) & 1;
}

static void
i2c_start(void)
{
	line(PIN_SDA, true);
	line(PIN_SCL, true);
	half_bit();
	line(PIN_SDA, false);
	half_bit();
	line(PIN_SCL, false);
	half_bit();
}

static void
i2c_stop(void)
{
	line(PIN_SDA, false);
	half_bit();
	line(PIN_SCL, true);
	half_bit();
	line(PIN_SDA, true);
	half_bit();
}

static bool
i2c_bit(bool out)
{
	bool in;

	line(PIN_SDA, out);
	half_bit();
	line(PIN_SCL, true);
	for (unsigned stretched = 0; !level_of(PIN_SCL) && stretched < 100; stretched++) {
		half_bit();
	}
	half_bit();
	in = level_of(PIN_SDA);
	line(PIN_SCL, false);
	return in;
}

static bool
i2c_write(uint8_t byte)
{
	for (int bit = 7; bit >= 0; bit--) {
		i2c_bit((byte >> bit) & 1);
	}
	return !i2c_bit(true);
}

static uint8_t
i2c_read(bool more)
{
	uint8_t byte = 0;

	for (int bit = 7; bit >= 0; bit--) {
		byte = (uint8_t)(byte << 1 | i2c_bit(true));
	}
	i2c_bit(!more);
	return byte;
}

static uint32_t taken_cfg[2], taken_io[2];

static bool
bus_take(void)
{
	taken_cfg[0] = rd(GPIO_CFG(PIN_SDA));
	taken_cfg[1] = rd(GPIO_CFG(PIN_SCL));
	taken_io[0] = rd(GPIO_IO(PIN_SDA));
	taken_io[1] = rd(GPIO_IO(PIN_SCL));
	line(PIN_SDA, true);
	line(PIN_SCL, true);
	half_bit();
	if (!level_of(PIN_SDA) || !level_of(PIN_SCL)) {
		wr(GPIO_IO(PIN_SDA), taken_io[0] & 2);
		wr(GPIO_IO(PIN_SCL), taken_io[1] & 2);
		wr(GPIO_CFG(PIN_SDA), taken_cfg[0]);
		wr(GPIO_CFG(PIN_SCL), taken_cfg[1]);
		say("synaptics: the I2C bus is not at rest (gpio%u reads %u, gpio%u reads %u): not touched\n", PIN_SDA,
		    level_of(PIN_SDA), PIN_SCL, level_of(PIN_SCL));
		return false;
	}
	return true;
}

static void
bus_give_back(void)
{
	wr(GPIO_IO(PIN_SDA), taken_io[0] & 2);
	wr(GPIO_IO(PIN_SCL), taken_io[1] & 2);
	wr(GPIO_CFG(PIN_SDA), taken_cfg[0]);
	wr(GPIO_CFG(PIN_SCL), taken_cfg[1]);
}

#define S3320_ADDRESS           0x20

/* `count' registers from `reg' on, in the page selected last */
static bool
regs_read(uint8_t reg, uint8_t *values, unsigned count)
{
	bool answered;

	if (!bus_take()) {
		return false;
	}
	i2c_start();
	answered = i2c_write(S3320_ADDRESS << 1) && i2c_write(reg);
	if (answered) {
		i2c_start();
		answered = i2c_write(S3320_ADDRESS << 1 | 1);
	}
	for (unsigned i = 0; answered && i < count; i++) {
		values[i] = i2c_read(i + 1 < count);
	}
	i2c_stop();
	bus_give_back();
	return answered;
}

static bool
reg_write(uint8_t reg, uint8_t value)
{
	bool answered;

	if (!bus_take()) {
		return false;
	}
	i2c_start();
	answered = i2c_write(S3320_ADDRESS << 1) && i2c_write(reg) && i2c_write(value);
	i2c_stop();
	bus_give_back();
	return answered;
}

/* ---- RMI4 (Linux drivers/input/rmi4/rmi_driver.c) -------------------------------------------------- */

#define RMI_PAGE_SELECT         0xff
#define RMI_PDT_START           0xe9    /* the first entry; the table goes down from there, 6 bytes each */
#define RMI_PDT_END             0x0a
#define RMI_PAGES               5       /* Linux looks at no more on any chip it knows */

struct pdt_entry {
	uint8_t page, query, command, control, data, irqs, version, function;
};

static struct pdt_entry f01;

static bool
chip_answers(void)
{
	uint8_t page;

	if (!regs_read(RMI_PAGE_SELECT, &page, 1)) {
		say("synaptics: nothing answers at 0x%02x\n", S3320_ADDRESS);
		return false;
	}
	say("synaptics: 0x%02x answers, page register %u\n", S3320_ADDRESS, page);
	return true;
}

static void
read_pdt(void)
{
	unsigned functions = 0;

	for (uint8_t page = 0; page < RMI_PAGES; page++) {
		bool found = false;

		if (!reg_write(RMI_PAGE_SELECT, page)) {
			say("synaptics: page %u: the page select is not acknowledged\n", page);
			break;
		}
		for (unsigned at = RMI_PDT_START; at >= RMI_PDT_END; at -= 6) {
			uint8_t entry[6];
			struct pdt_entry e;

			if (!regs_read((uint8_t)at, entry, sizeof(entry))) {
				say("synaptics: page %u: the entry at 0x%02x does not read\n", page, at);
				break;
			}
			if (entry[5] == 0 || entry[5] == 0xff) {
				break;          /* the end of this page's table */
			}
			e = (struct pdt_entry){ page, entry[0], entry[1], entry[2], entry[3], (uint8_t)(entry[4] & 7),
				                (uint8_t)((entry[4] >> 5) & 3), entry[5] };
			say("synaptics: page %u: F%02x version %u, query 0x%02x, command 0x%02x, control 0x%02x, data 0x%02x, "
			    "%u interrupt sources\n", page, e.function, e.version, e.query, e.command, e.control, e.data, e.irqs);
			if (e.function == 0x01) {
				f01 = e;
			}
			found = true;
			functions++;
		}
		if (!found && page > 0) {
			break;
		}
	}
	(void)reg_write(RMI_PAGE_SELECT, 0);
	say("synaptics: %u functions in all\n", functions);
}

static void
read_identity(void)
{
	uint8_t query[21], status;
	char product[11];

	if (f01.function != 0x01) {
		say("synaptics: no F01 in the tables: no identity\n");
		return;
	}
	if (!reg_write(RMI_PAGE_SELECT, f01.page) || !regs_read(f01.query, query, sizeof(query)) ||
	    !regs_read(f01.data, &status, 1)) {
		say("synaptics: F01 does not read\n");
		(void)reg_write(RMI_PAGE_SELECT, 0);
		return;
	}
	(void)reg_write(RMI_PAGE_SELECT, 0);
	for (unsigned i = 0; i < 10; i++) {
		char c = (char)query[11 + i];

		product[i] = c >= 0x20 && c < 0x7f ? c : 0;
	}
	product[10] = 0;
	say("synaptics: F01: manufacturer %u%s, properties 0x%02x, product \"%s\", firmware build %u\n", query[0],
	    query[0] == 1 ? " (Synaptics)" : "", query[1], product, query[18] | query[19] << 8 | query[20] << 16);
	/* (Linux rmi_f01.c: bit 7 unconfigured -- so it is after every reset, until a driver configures it -- bit 6 the boot loader) */
	say("synaptics: F01: status 0x%02x: code %u%s%s\n", status, status & 15, status & 0x80 ? ", unconfigured" : "",
	    status & 0x40 ? ", in the boot loader (flash programming)" : "");
}

/* ---- the 3.0 V (RPMh, as in hakios.goodix.c) ------------------------------------------------------ */

/*
 * The command database is where the boot loader leaves it, and on the SDM845 that is said by AOP's
 * message RAM ("qcom,cmd-db" reg <0xc3f000c 8>: its address, then its size); the RPMh controller is
 * the application processor's DRV 2 ("apps_rsc", 0x179e0000, its command slots 0xd00 on, the first
 * set an active one: qcom,tcs-config lists those first). The register layout is RSC 2.x's, the same
 * as the SC7280's.
 */
#define CMD_DB_POINTER_PHYS     0x0c3f0000UL
#define CMD_DB_POINTER_OFFSET   0xc
#define CMD_DB_MOST             0x40000u
#define RSC_PHYS                0x179e0000UL
#define RSC_SIZE                0x10000UL
#define TCS(reg)                (rsc + 0xd00 + (reg))
#define TCS_IRQ_ENABLE          0x00
#define TCS_IRQ_STATUS          0x04
#define TCS_IRQ_CLEAR           0x08
#define TCS_WAIT_FOR_COMPLETION 0x10
#define TCS_CONTROL             0x14
#define TCS_CONTROL_ENABLE      (1u << 16)
#define TCS_CONTROL_TRIGGER     (1u << 24)
#define TCS_STATUS              0x18            /* 0 while busy */
#define TCS_CMD_ENABLE          0x1c
#define TCS_CMD_MSGID(n)        (0x30 + 20 * (n))
#define TCS_CMD_ADDR(n)         (0x34 + 20 * (n))
#define TCS_CMD_DATA(n)         (0x38 + 20 * (n))
#define MSGID_WRITE_4_BYTES     0x10008
#define MSGID_RESPONSE          0x100

#define VRM_VOLTAGE             0               /* a regulator's registers, from its address: millivolts */
#define VRM_ENABLE              4
#define VRM_MODE                8
#define VRM_MODE_LDO_HPM        7               /* PMIC4's LDOs (the PM8998's) */

#define AVDD_NAME               "ldoa28"        /* vdd_2v8-supply: the PM8998's LDO28, nothing else on it */
#define AVDD_MV                 3008            /* synaptics,avdd-voltage, as Android sets it */

struct cmd_db_slave {
	uint16_t id, header_offset, data_offset, count, version, reserved[3];
};
struct cmd_db_header {
	uint32_t version;
	uint8_t magic[4];
	struct cmd_db_slave slave[8];
	uint32_t checksum, reserved;
	uint8_t data[];
};
struct cmd_db_entry {
	char id[8];
	uint32_t priority[2];
	uint32_t address;
	uint16_t length, offset;
};

static vm_offset_t rsc, cmd_db;
static uint32_t cmd_db_size, avdd;              /* avdd: LDO28's RPMh address once it is switched on */

static bool
find_cmd_db(void)
{
	vm_offset_t pointer = ml_io_map(CMD_DB_POINTER_PHYS, 0x1000);
	uint32_t at = rd(pointer + CMD_DB_POINTER_OFFSET), size = rd(pointer + CMD_DB_POINTER_OFFSET + 4);
	const struct cmd_db_header *db;

	say("synaptics: RPMh: AOP says the command database is at 0x%x, %u bytes\n", at, size);
	/* (the 6T's is 3520 bytes: smaller than a page, but it starts on one) */
	if (at == 0 || (at & 0xfff) != 0 || size < sizeof(struct cmd_db_header) || size > CMD_DB_MOST) {
		say("synaptics: RPMh: that is not a command database's place: the 3.0 V is left as it is\n");
		return false;
	}
	cmd_db = ml_io_map(at, (size + 0xfff) & ~0xfffu);
	cmd_db_size = size;
	db = (const struct cmd_db_header *)cmd_db;
	if (db->magic[0] != 0xdb || db->magic[1] != 0x30 || db->magic[2] != 0x03 || db->magic[3] != 0x0c) {
		say("synaptics: RPMh: no command database there (magic %02x %02x %02x %02x)\n", db->magic[0], db->magic[1],
		    db->magic[2], db->magic[3]);
		return false;
	}
	return true;
}

/* The RPMh address of a resource by its name in the command database; 0 = none. */
static uint32_t
rpmh_address(const char *name)
{
	const struct cmd_db_header *db = (const struct cmd_db_header *)cmd_db;
	char id[8] = { 0 };

	strlcpy(id, name, sizeof(id));
	for (unsigned s = 0; s < 8; s++) {
		const struct cmd_db_slave *slave = &db->slave[s];
		const struct cmd_db_entry *entry = (const struct cmd_db_entry *)(db->data + slave->header_offset);

		if (slave->id == 0 || sizeof(*db) + slave->header_offset + (uint32_t)slave->count * sizeof(*entry) > cmd_db_size) {
			continue;               /* (a table that would run past the end is not read) */
		}
		for (unsigned i = 0; i < slave->count; i++) {
			if (memcmp(entry[i].id, id, sizeof(id)) == 0) {
				return entry[i].address;
			}
		}
	}
	return 0;
}

static bool
rpmh_wait(uint32_t reg, uint32_t mask, uint32_t value, const char *what)
{
	for (int i = 0; i < 2000; i++) {
		if ((rd(TCS(reg)) & mask) == value) {
			return true;
		}
		spin_us(100);
	}
	say("synaptics: RPMh: timeout waiting for %s (register %#x = %#x)\n", what, reg, rd(TCS(reg)));
	return false;
}

/* Send up to three register writes to RPMh and wait until they are done. */
static bool
rpmh_write(const uint32_t *address, const uint32_t *data, unsigned count)
{
	uint32_t irq_enable = rd(TCS(TCS_IRQ_ENABLE));
	bool done;

	/* the slot set must be idle, and switched off the way the hardware wants it: trigger, then enable */
	for (int i = 0; rd(TCS(TCS_STATUS)) == 0; i++) {
		if (i > 2000) {
			say("synaptics: RPMh: the command slots stay busy\n");
			return false;
		}
		spin_us(100);
	}
	wr(TCS(TCS_CONTROL), rd(TCS(TCS_CONTROL)) & ~TCS_CONTROL_TRIGGER);
	wr(TCS(TCS_CONTROL), rd(TCS(TCS_CONTROL)) & ~TCS_CONTROL_ENABLE);
	wr(TCS(TCS_IRQ_CLEAR), 1);
	for (unsigned i = 0; i < count; i++) {
		wr(TCS(TCS_CMD_MSGID(i)), MSGID_WRITE_4_BYTES | MSGID_RESPONSE);
		wr(TCS(TCS_CMD_ADDR(i)), address[i]);
		wr(TCS(TCS_CMD_DATA(i)), data[i]);
	}
	wr(TCS(TCS_WAIT_FOR_COMPLETION), (1u << count) - 1);
	wr(TCS(TCS_CMD_ENABLE), (1u << count) - 1);
	wr(TCS(TCS_IRQ_ENABLE), irq_enable | 1);        /* the status bit follows the enable; the interrupt itself stays masked */
	wr(TCS(TCS_CONTROL), TCS_CONTROL_ENABLE);
	wr(TCS(TCS_CONTROL), TCS_CONTROL_ENABLE | TCS_CONTROL_TRIGGER);

	done = rpmh_wait(TCS_IRQ_STATUS, 1, 1, "the request");
	wr(TCS(TCS_CONTROL), rd(TCS(TCS_CONTROL)) & ~TCS_CONTROL_TRIGGER);
	wr(TCS(TCS_CONTROL), rd(TCS(TCS_CONTROL)) & ~TCS_CONTROL_ENABLE);
	wr(TCS(TCS_CMD_ENABLE), 0);
	wr(TCS(TCS_WAIT_FOR_COMPLETION), 0);
	wr(TCS(TCS_IRQ_CLEAR), 1);
	wr(TCS(TCS_IRQ_ENABLE), irq_enable);
	return done;
}

/* The chip's analog supply: LDO28 at Android's 3.008 V, in high-power mode. */
static bool
avdd_on(void)
{
	uint32_t base, address[3], data[3] = { AVDD_MV, 1, VRM_MODE_LDO_HPM };

	if (!find_cmd_db()) {
		return false;
	}
	base = rpmh_address(AVDD_NAME);
	/* addresses of regulators ("VRM") have 4 in bits 19:16; anything else is not what we are looking for */
	if (base == 0 || (base >> 16 & 0xf) != 4) {
		say("synaptics: RPMh: no regulator \"%s\" (address %#x)\n", AVDD_NAME, base);
		return false;
	}
	rsc = ml_io_map(RSC_PHYS, RSC_SIZE);
	say("synaptics: RPMh: %s is at %#x; slot status %#x, control %#x\n", AVDD_NAME, base, rd(TCS(TCS_STATUS)),
	    rd(TCS(TCS_CONTROL)));
	address[0] = base + VRM_VOLTAGE;
	address[1] = base + VRM_ENABLE;
	address[2] = base + VRM_MODE;
	if (!rpmh_write(address, data, 3)) {
		return false;
	}
	avdd = base;
	say("synaptics: the 3.0 V on (%s, %u mV)\n", AVDD_NAME, AVDD_MV);
	return true;
}

/* ---- the probe ------------------------------------------------------------------------------------- */

static void
power_up(void)
{
	found_cfg[0] = rd(GPIO_CFG(PIN_1V8));
	found_io[0] = rd(GPIO_IO(PIN_1V8));
	found_cfg[1] = rd(GPIO_CFG(PIN_RESET));
	found_io[1] = rd(GPIO_IO(PIN_RESET));
	powered = true;
	/* (Android's driver: the reset held, the 3.0 V on, then the 1.8 V, then the reset let go and the chip left to start) */
	drive(PIN_RESET, false);
	(void)avdd_on();
	wait_ms(5);
	say("synaptics: the 1.8 V on (gpio%u high), the reset pulsed (gpio%u low 10 ms, then high), 200 ms to start\n",
	    PIN_1V8, PIN_RESET);
	drive(PIN_1V8, true);
	wait_ms(10);
	drive(PIN_RESET, true);
	wait_ms(200);
}

static void
synaptics_thread(__unused void *arg, __unused wait_result_t wr)
{
	map_pins();
	say("synaptics: level %u: %s\n", level, level == 1 ? "a look" : "powered and probed, the chip only read");
	show_pins("as found");
	if (!chip_answers() && level >= 2) {
		power_up();
		show_pins("powered");
		if (!chip_answers()) {
			say("synaptics: (the interrupt pin reads %u; the 3.0 V %s)\n", level_of(PIN_INT),
			    avdd != 0 ? "was switched on" : "was not switched on");
		}
	}
	if (level >= 2 && regs_read(RMI_PAGE_SELECT, (uint8_t[1]){ 0 }, 1)) {
		read_pdt();
		read_identity();
		say("synaptics: the interrupt pin reads %u\n", level_of(PIN_INT));
	}
	say("synaptics: done\n");
	thread_terminate(current_thread());
}

void
hakios_synaptics_init(void)
{
	thread_t thread;

	if (!PE_parse_boot_argn("synaptics", &level, sizeof(level)) || level == 0) {
		return;
	}
	if (level > 2) {
		level = 2;
	}
	if (kernel_thread_start(synaptics_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}

/* At the run's end and on a panic's way out: the power and reset pins as they were found, the 3.0 V off. */
void
hakios_synaptics_quiesce(void)
{
	if (powered && tlmm != 0) {
		leaving = true;
		wr(GPIO_IO(PIN_RESET), found_io[1] & 2);
		wr(GPIO_CFG(PIN_RESET), found_cfg[1]);
		wr(GPIO_IO(PIN_1V8), found_io[0] & 2);
		wr(GPIO_CFG(PIN_1V8), found_cfg[0]);
		say("synaptics: at the run's end: gpio%u and gpio%u as they were found\n", PIN_RESET, PIN_1V8);
		if (avdd != 0) {
			uint32_t address = avdd + VRM_ENABLE, off = 0;

			say("synaptics: ... and the 3.0 V %s\n", rpmh_write(&address, &off, 1) ? "off" : "NOT switched off");
		}
	}
}
