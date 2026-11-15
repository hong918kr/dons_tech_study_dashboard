/* ENV: qemu */
/*
 * ELX-B11: userspace I2C/SPI with i2c-dev and spidev (raw ioctls, no libi2c).
 *
 * i2creg = a register read utility + its self-test. It runs in the QEMU guest
 * (Linux 6.18.55-dom) against two adapters that both have a chip at 0x48:
 *   STUB  i2c-stub chip_addr=0x48   SMBus-only adapter (no I2C_FUNC_I2C)
 *   FAKE  b11_fakei2c.ko            real .xfer: I2C level + SMBus emulation
 * BusyBox i2cget/i2cset/i2cdump on the same adapters are the reference.
 *
 *   i2creg test STUBBUS FAKEBUS       run every check, print "N/M passed"
 *   i2creg read BUS ADDR REG [LEN]    print LEN bytes (default 1)
 *
 * TODO A1 i2c_rdwr_read   A2 i2c_rdwr_write   A3 smbus_read_i2c_block
 *      B1 i2creg_read     C1 tmp_raw_to_mC    S1 spi_build_wr_rd
 * Build (VM): gcc -static -std=c11 -Wall -Wextra -Werror -O2 X.c -o i2creg
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/spi/spidev.h>

#define CHIP		0x48
#define I2CREG_MAX_WR	32
#define FAKE_PARAM	"/sys/module/b11_fakei2c/parameters/"

/* ---------------- given helpers ---------------- */
static int open_bus(int bus)
{
	char p[32];

	snprintf(p, sizeof(p), "/dev/i2c-%d", bus);
	return open(p, O_RDWR);
}

static unsigned long i2c_funcs(int fd)
{
	unsigned long f = 0;

	if (ioctl(fd, I2C_FUNCS, &f) < 0)
		return 0;
	return f;
}

/* bind this fd to addr for read()/write()/I2C_SMBUS. 0 or -errno */
static int i2c_set_addr(int fd, uint16_t addr, int force)
{
	if (ioctl(fd, force ? I2C_SLAVE_FORCE : I2C_SLAVE, (unsigned long)addr) < 0)
		return -errno;
	return 0;
}

static int smbus_xfer(int fd, uint8_t rw, uint8_t cmd, uint32_t size,
		      union i2c_smbus_data *data)
{
	struct i2c_smbus_ioctl_data a = {
		.read_write = rw, .command = cmd, .size = size, .data = data,
	};

	if (ioctl(fd, I2C_SMBUS, &a) < 0)
		return -errno;
	return 0;
}

static int smbus_read_byte_data(int fd, uint8_t reg)
{
	union i2c_smbus_data d;
	int r = smbus_xfer(fd, I2C_SMBUS_READ, reg, I2C_SMBUS_BYTE_DATA, &d);

	return r < 0 ? r : d.byte;
}

/* SMBus word = low byte first on the wire */
static int smbus_read_word_data(int fd, uint8_t reg)
{
	union i2c_smbus_data d;
	int r = smbus_xfer(fd, I2C_SMBUS_READ, reg, I2C_SMBUS_WORD_DATA, &d);

	return r < 0 ? r : d.word;
}

/* ---------------- Part A ---------------- */
/* A1: S addr+W reg Sr addr+R buf[0..len-1] P -- ONE I2C_RDWR, 2 msgs.
 * return 0 or -errno. */
static int i2c_rdwr_read(int fd, uint16_t addr, uint8_t reg, uint8_t *buf,
			 uint16_t len)
{
	struct i2c_msg m[2] = {
		{ .addr = addr, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = addr, .flags = I2C_M_RD, .len = len, .buf = buf },
	};
	struct i2c_rdwr_ioctl_data x = { .msgs = m, .nmsgs = 2 };
	int r = ioctl(fd, I2C_RDWR, &x);

	if (r < 0)
		return -errno;
	return r == 2 ? 0 : -EIO;
}

/* A2: S addr+W reg d0..d(len-1) P -- ONE message. len <= I2CREG_MAX_WR.
 * return 0 or -errno. */
static int i2c_rdwr_write(int fd, uint16_t addr, uint8_t reg,
			  const uint8_t *data, uint16_t len)
{
	uint8_t b[1 + I2CREG_MAX_WR];
	struct i2c_msg m = { .addr = addr, .flags = 0, .buf = b };
	struct i2c_rdwr_ioctl_data x = { .msgs = &m, .nmsgs = 1 };

	if (len > I2CREG_MAX_WR)
		return -EINVAL;
	b[0] = reg;
	memcpy(b + 1, data, len);
	m.len = (uint16_t)(len + 1);
	if (ioctl(fd, I2C_RDWR, &x) < 0)
		return -errno;
	return 0;
}

/* A3: SMBus "I2C block read" (fd already bound with I2C_SLAVE).
 * len 1..32. return bytes read or -errno. */
static int smbus_read_i2c_block(int fd, uint8_t reg, uint8_t *buf, uint8_t len)
{
	union i2c_smbus_data d;
	int r;

	if (len == 0 || len > I2C_SMBUS_BLOCK_MAX)
		return -EINVAL;
	d.block[0] = len;
	r = smbus_xfer(fd, I2C_SMBUS_READ, reg, I2C_SMBUS_I2C_BLOCK_DATA, &d);
	if (r < 0)
		return r;
	memcpy(buf, &d.block[1], d.block[0]);
	return d.block[0];
}

/* ---------------- Part B ---------------- */
/* B1: read len bytes from reg.. of addr. Adapter has I2C_FUNC_I2C -> one
 * I2C_RDWR. Otherwise bind addr (no force) and use SMBus I2C block reads
 * (<= 32 each) or, failing that, byte reads. reg + len must fit in 256.
 * return len or -errno (-EINVAL bad args, -EOPNOTSUPP no usable method). */
static int i2creg_read(int fd, uint16_t addr, uint8_t reg, uint8_t *buf,
		       unsigned int len)
{
	unsigned long f = i2c_funcs(fd);
	unsigned int done = 0;
	int r;

	if (len == 0 || reg + len > 256)
		return -EINVAL;
	if (f & I2C_FUNC_I2C) {
		r = i2c_rdwr_read(fd, addr, reg, buf, (uint16_t)len);
		return r < 0 ? r : (int)len;
	}
	r = i2c_set_addr(fd, addr, 0);
	if (r < 0)
		return r;
	while (done < len) {
		unsigned int n = len - done;

		if (f & I2C_FUNC_SMBUS_READ_I2C_BLOCK) {
			if (n > I2C_SMBUS_BLOCK_MAX)
				n = I2C_SMBUS_BLOCK_MAX;
			r = smbus_read_i2c_block(fd, (uint8_t)(reg + done),
						 buf + done, (uint8_t)n);
		} else if (f & I2C_FUNC_SMBUS_READ_BYTE_DATA) {
			r = smbus_read_byte_data(fd, (uint8_t)(reg + done));
			if (r >= 0) {
				buf[done] = (uint8_t)r;
				r = 1;
			}
		} else {
			return -EOPNOTSUPP;
		}
		if (r < 0)
			return r;
		if (r == 0)
			return -EIO;
		done += (unsigned int)r;
	}
	return (int)done;
}

/* ---------------- Part C ---------------- */
/* C1: TMP102 temperature register (MSB first): 12-bit two's complement,
 * left-justified, 0.0625 C/LSB. return milli-degC (C division: toward 0). */
static int tmp_raw_to_mC(uint8_t msb, uint8_t lsb)
{
	int raw = (msb << 4) | (lsb >> 4);

	if (raw & 0x800)
		raw -= 0x1000;
	return raw * 125 / 2;
}

/* ---------------- SPI ---------------- */
/* S1: one SPI message = "send tx[txlen], then read rxlen bytes", CS held
 * low across both transfers. Zero everything else. return the ioctl
 * request to pass to ioctl(fd, req, x). */
static unsigned long spi_build_wr_rd(struct spi_ioc_transfer x[2],
				     const uint8_t *tx, uint32_t txlen,
				     uint8_t *rx, uint32_t rxlen,
				     uint32_t speed_hz, uint8_t bits)
{
	memset(x, 0, 2 * sizeof(x[0]));
	x[0].tx_buf = (uint64_t)(uintptr_t)tx;
	x[0].len = txlen;
	x[0].speed_hz = speed_hz;
	x[0].bits_per_word = bits;
	x[1].rx_buf = (uint64_t)(uintptr_t)rx;
	x[1].len = rxlen;
	x[1].speed_hz = speed_hz;
	x[1].bits_per_word = bits;
	return SPI_IOC_MESSAGE(2);	/* cs_change = 0: CS stays asserted */
}

/* ---------------- test harness ---------------- */
static int g_pass, g_total;
#define CHECK(expr, ...) do { int ok_ = !!(expr); g_total++; g_pass += ok_; \
	printf("%s: ", ok_ ? "PASS" : "FAIL"); printf(__VA_ARGS__); \
	putchar('\n'); } while (0)

/* run a shell command, return its first hex number ("0xab") or -1 */
static long sh_hex(const char *fmt, ...)
{
	char cmd[256];
	unsigned long v;
	va_list ap;
	FILE *p;
	int n;

	va_start(ap, fmt);
	vsnprintf(cmd, sizeof(cmd), fmt, ap);
	va_end(ap);
	p = popen(cmd, "r");
	if (!p)
		return -1;
	n = fscanf(p, "%lx", &v);
	pclose(p);
	return n == 1 ? (long)v : -1;
}

static int sh_run(const char *fmt, ...)
{
	char cmd[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(cmd, sizeof(cmd), fmt, ap);
	va_end(ap);
	return system(cmd);
}

/* parse "i2cdump -y BUS 0x48 b" into out[256]; return bytes parsed */
static int i2cdump_parse(int bus, uint8_t out[256])
{
	char cmd[64], line[160];
	int got = 0;
	FILE *p;

	snprintf(cmd, sizeof(cmd), "i2cdump -y %d 0x%02x b 2>&1", bus, CHIP);
	p = popen(cmd, "r");
	if (!p)
		return 0;
	while (fgets(line, sizeof(line), p)) {
		unsigned int row, v, k;

		if (strlen(line) < 52 || line[2] != ':' ||
		    sscanf(line, "%2x:", &row) != 1)
			continue;
		for (k = 0; k < 16; k++) {
			if (sscanf(line + 4 + 3 * k, "%2x", &v) != 1)
				break;
			out[row + k] = (uint8_t)v;
			got++;
		}
	}
	pclose(p);
	return got;
}

static long read_param(const char *name)
{
	char path[128];
	long v = -1;
	FILE *f;

	snprintf(path, sizeof(path), FAKE_PARAM "%s", name);
	f = fopen(path, "r");
	if (!f)
		return -1;
	if (fscanf(f, "%ld", &v) != 1)
		v = -1;
	fclose(f);
	return v;
}

static int write_file(const char *path, const char *s)
{
	FILE *f = fopen(path, "w");
	int r;

	if (!f)
		return -errno;
	r = fputs(s, f) < 0 ? -EIO : 0;
	if (fclose(f) != 0 && r == 0)
		r = -errno;
	return r;
}

static int raw_rdwr(int fd, struct i2c_msg *m, unsigned int n)
{
	struct i2c_rdwr_ioctl_data x = { .msgs = m, .nmsgs = n };

	return ioctl(fd, I2C_RDWR, &x) < 0 ? -errno : 0;
}

static void test_part_a(int fs, int ff, int sbus, int fbus)
{
	uint8_t b[8] = { 0 };
	const uint8_t w[4] = { 0xde, 0xad, 0xbe, 0xef };
	long c0;
	int r, i, ok;

	printf("--- Part A: I2C_RDWR on FAKE (i2c-%d), SMBus block on both\n", fbus);
	CHECK(i2c_funcs(ff) & I2C_FUNC_I2C, "FAKE advertises I2C_FUNC_I2C");
	c0 = read_param("xfer_calls");
	r = i2c_rdwr_read(ff, CHIP, 0x0f, b, 1);
	CHECK(r == 0 && b[0] == 0xb1, "A1 rdwr_read ID reg 0x0f -> r=%d 0x%02x (want 0xb1)", r, b[0]);
	CHECK(read_param("xfer_calls") - c0 == 1 && read_param("last_nmsgs") == 2,
	      "A1 one transaction, 2 msgs (write + Sr + read): calls+%ld nmsgs=%ld",
	      read_param("xfer_calls") - c0, read_param("last_nmsgs"));
	r = i2c_rdwr_read(ff, CHIP, 0x10, b, 4);
	ok = r == 0;
	for (i = 0; i < 4; i++)
		ok = ok && b[i] == sh_hex("i2cget -y %d 0x48 0x%02x", fbus, 0x10 + i);
	CHECK(ok, "A1 rdwr_read 4 bytes @0x10 == i2cget x4: %02x %02x %02x %02x",
	      b[0], b[1], b[2], b[3]);
	r = i2c_rdwr_write(ff, CHIP, 0x40, w, 4);
	CHECK(r == 0 && read_param("last_nmsgs") == 1,
	      "A2 rdwr_write 4 bytes @0x40 -> r=%d, 1 msg", r);
	ok = 1;
	for (i = 0; i < 4; i++)
		ok = ok && sh_hex("i2cget -y %d 0x48 0x%02x", fbus, 0x40 + i) == w[i];
	CHECK(ok, "A2 i2cget 0x40..0x43 == de ad be ef");
	CHECK(i2c_rdwr_write(ff, CHIP, 0, w, I2CREG_MAX_WR + 1) == -EINVAL,
	      "A2 len > %d -> -EINVAL", I2CREG_MAX_WR);
	sh_run("i2cset -y %d 0x48 0x50 0x77", fbus);
	memset(b, 0, sizeof(b));
	r = i2c_rdwr_read(ff, CHIP, 0x50, b, 1);
	CHECK(r == 0 && b[0] == 0x77, "A1 sees i2cset 0x50=0x77 -> 0x%02x", b[0]);

	memset(b, 0, sizeof(b));
	r = i2c_set_addr(ff, CHIP, 0);
	if (r == 0)
		r = smbus_read_i2c_block(ff, 0x40, b, 4);
	CHECK(r == 4 && !memcmp(b, w, 4), "A3 SMBus i2c-block on FAKE (core emulates) -> %d", r);
	for (i = 0; i < 4; i++)
		sh_run("i2cset -y %d 0x48 0x%02x 0x%02x", sbus, 0x10 + i, 0xa0 + i);
	memset(b, 0, sizeof(b));
	r = i2c_set_addr(fs, CHIP, 0);
	if (r == 0)
		r = smbus_read_i2c_block(fs, 0x10, b, 4);
	CHECK(r == 4 && b[0] == 0xa0 && b[3] == 0xa3,
	      "A3 SMBus i2c-block on STUB -> %d: %02x..%02x", r, b[0], b[3]);
	r = smbus_read_byte_data(fs, 0x13);
	CHECK(r == 0xa3, "given smbus_read_byte_data STUB 0x13 -> 0x%02x", r);
	CHECK(smbus_read_i2c_block(fs, 0, b, 33) == -EINVAL, "A3 len 33 -> -EINVAL");
}

static void test_part_b(int fs, int ff, int sbus, int fbus)
{
	uint8_t b[256], ref[256];
	unsigned long f = i2c_funcs(fs);
	struct i2c_msg m[43];
	uint8_t one = 0;
	long c0;
	int r, i, ok;

	printf("--- Part B: STUB (i2c-%d) is SMBus-only; fallbacks and errors\n", sbus);
	CHECK(!(f & I2C_FUNC_I2C) && (f & I2C_FUNC_SMBUS_READ_I2C_BLOCK),
	      "STUB funcs=0x%08lx: no I2C_FUNC_I2C, has I2C block read", f);
	r = i2c_rdwr_read(fs, CHIP, 0x10, b, 1);
	CHECK(r == -EOPNOTSUPP, "B  rdwr_read on STUB -> %d (%s)", r, strerror(-r));
	i2c_set_addr(fs, CHIP, 0);
	errno = 0;
	r = (int)read(fs, b, 1);
	CHECK(r < 0 && errno == EOPNOTSUPP, "B  plain read() on STUB -> errno %d (%s)",
	      errno, strerror(errno));
	for (i = 0; i < 16; i++)
		sh_run("i2cset -y %d 0x48 0x%02x 0x%02x", sbus, 0x10 + i, 0xa0 + i);
	memset(b, 0, sizeof(b));
	r = i2creg_read(fs, CHIP, 0x10, b, 16);
	ok = r == 16;
	for (i = 0; i < 16; i++)
		ok = ok && b[i] == 0xa0 + i;
	CHECK(ok, "B1 i2creg_read STUB 16 bytes (SMBus fallback) -> %d", r);
	memset(b, 0, sizeof(b));
	r = i2creg_read(fs, CHIP, 0, b, 256);
	CHECK(r == 256 && i2cdump_parse(sbus, ref) == 256 && !memcmp(b, ref, 256),
	      "B1 i2creg_read STUB 256 bytes == i2cdump (r=%d)", r);
	memset(b, 0, sizeof(b));
	c0 = read_param("xfer_calls");
	r = i2creg_read(ff, CHIP, 0, b, 256);
	CHECK(r == 256 && read_param("xfer_calls") - c0 == 1,
	      "B1 i2creg_read FAKE 256 bytes in ONE transaction (r=%d)", r);
	c0 = read_param("xfer_calls");
	CHECK(i2cdump_parse(fbus, ref) == 256 && !memcmp(b, ref, 256),
	      "B1 ... == i2cdump FAKE (i2cdump needed %ld transactions)",
	      read_param("xfer_calls") - c0);
	r = i2creg_read(ff, 0x49, 0, b, 1);
	CHECK(r == -ENXIO, "B  FAKE addr 0x49 (no ACK) -> %d (%s)", r, strerror(-r));
	r = i2creg_read(fs, 0x49, 0, b, 1);
	CHECK(r == -ENODEV, "B  STUB addr 0x49 -> %d (%s)", r, strerror(-r));
	CHECK(i2creg_read(ff, CHIP, 0, b, 0) == -EINVAL, "B1 len 0 -> -EINVAL");
	CHECK(i2creg_read(fs, CHIP, 0xf0, b, 32) == -EINVAL, "B1 0xf0 + 32 > 256 -> -EINVAL");

	for (i = 0; i < 43; i++)
		m[i] = (struct i2c_msg){ .addr = CHIP, .flags = I2C_M_RD, .len = 1, .buf = &one };
	r = raw_rdwr(ff, m, 43);
	CHECK(r == -EINVAL, "kernel: I2C_RDWR nmsgs 43 > I2C_RDWR_IOCTL_MAX_MSGS(42) -> %d", r);
	m[0].len = 8193;
	r = raw_rdwr(ff, m, 1);
	CHECK(r == -EINVAL, "kernel: msg len 8193 > 8192 -> %d", r);
}

static void test_part_c(int ff, int fbus)
{
	static const struct { uint8_t m, l; int mC; } t[] = {
		{ 0x19, 0x00, 25000 }, { 0x00, 0x00, 0 }, { 0x7f, 0xf0, 127937 },
		{ 0xe7, 0x00, -25000 }, { 0xff, 0xf0, -62 }, { 0xc9, 0x00, -55000 },
		{ 0x00, 0x10, 62 },
	};
	uint8_t b[2] = { 0 }, reg = 0x10, v = 0;
	unsigned int i;
	long w;
	int r, ok = 1;

	printf("--- Part C: sensor at 0x48 on FAKE (i2c-%d)\n", fbus);
	for (i = 0; i < sizeof(t) / sizeof(t[0]); i++)
		ok = ok && tmp_raw_to_mC(t[i].m, t[i].l) == t[i].mC;
	CHECK(ok, "C1 tmp_raw_to_mC table (25.0, 0, 127.9375, -25.0, -0.0625, -55.0, 0.0625 C)");
	r = i2creg_read(ff, CHIP, 0x00, b, 2);
	CHECK(r == 2 && tmp_raw_to_mC(b[0], b[1]) == 25000,
	      "C1 power-on temp %02x %02x -> %d mC", b[0], b[1], tmp_raw_to_mC(b[0], b[1]));
	sh_run("i2cset -y %d 0x48 0x00 0xe7 && i2cset -y %d 0x48 0x01 0x00", fbus, fbus);
	r = i2creg_read(ff, CHIP, 0x00, b, 2);
	CHECK(r == 2 && tmp_raw_to_mC(b[0], b[1]) == -25000,
	      "C1 after i2cset: %02x %02x -> %d mC", b[0], b[1], tmp_raw_to_mC(b[0], b[1]));
	i2c_set_addr(ff, CHIP, 0);
	w = smbus_read_word_data(ff, 0x00);
	CHECK(w == 0x00e7 && w == sh_hex("i2cget -y %d 0x48 0x00 w", fbus) &&
	      (((w >> 8) | (w << 8)) & 0xffff) == ((b[0] << 8) | b[1]),
	      "C  SMBus word = LSB first: 0x%04lx, byte-swap -> 0x%02x%02x", w, b[0], b[1]);

	/* a chip that forgets its pointer at STOP needs the repeated START */
	write_file(FAKE_PARAM "stop_resets_ptr", "1");
	r = (int)write(ff, &reg, 1);			/* S W 0x10 P   */
	if (r == 1)
		r = (int)read(ff, &v, 1);		/* S R ..   P   */
	CHECK(r == 1 && v == 0xe7, "C  write()+read() = 2 transactions -> got reg0 0x%02x, not reg 0x10", v);
	r = i2c_rdwr_read(ff, CHIP, 0x10, &v, 1);
	CHECK(r == 0 && v == 0x73, "C  rdwr_read (Sr, no STOP) -> 0x%02x (want 0x73)", v);
	write_file(FAKE_PARAM "stop_resets_ptr", "0");
}

static void test_spi(void)
{
	struct spi_ioc_transfer x[2], e[2];
	uint8_t tx[3] = { 0x0b, 0x00, 0x10 }, rx[4];
	unsigned long req;

	printf("--- SPI: spi_ioc_transfer encoding (no SPI controller in QEMU virt)\n");
	CHECK(sizeof(struct spi_ioc_transfer) == 32 &&
	      offsetof(struct spi_ioc_transfer, len) == 16 &&
	      offsetof(struct spi_ioc_transfer, speed_hz) == 20 &&
	      offsetof(struct spi_ioc_transfer, delay_usecs) == 24 &&
	      offsetof(struct spi_ioc_transfer, bits_per_word) == 26 &&
	      offsetof(struct spi_ioc_transfer, cs_change) == 27 &&
	      offsetof(struct spi_ioc_transfer, pad) == 31,
	      "layout: 32 bytes, len@16 speed@20 delay@24 bits@26 cs_change@27 pad@31");
	CHECK(_IOC_SIZE(SPI_IOC_MESSAGE(2)) == 64 && _IOC_TYPE(SPI_IOC_MESSAGE(2)) == 'k' &&
	      _IOC_NR(SPI_IOC_MESSAGE(2)) == 0 && _IOC_DIR(SPI_IOC_MESSAGE(2)) == _IOC_WRITE,
	      "SPI_IOC_MESSAGE(2) = 0x%08lx: dir W, type 'k', nr 0, size 64",
	      (unsigned long)SPI_IOC_MESSAGE(2));
	CHECK(_IOC_SIZE(SPI_IOC_MESSAGE(511)) == 16352 && _IOC_SIZE(SPI_IOC_MESSAGE(512)) == 0,
	      "SPI_IOC_MESSAGE(511) size 16352, (512) size 0 (14-bit size field)");
	memset(x, 0xa5, sizeof(x));
	req = spi_build_wr_rd(x, tx, 3, rx, 4, 1000000, 8);
	memset(e, 0, sizeof(e));
	e[0].tx_buf = (uint64_t)(uintptr_t)tx;
	e[0].len = 3;
	e[1].rx_buf = (uint64_t)(uintptr_t)rx;
	e[1].len = 4;
	e[0].speed_hz = e[1].speed_hz = 1000000;
	e[0].bits_per_word = e[1].bits_per_word = 8;
	CHECK(req == SPI_IOC_MESSAGE(2), "S1 returns SPI_IOC_MESSAGE(2) (0x%08lx)", req);
	CHECK(x[0].tx_buf == e[0].tx_buf && x[0].rx_buf == 0 && x[0].len == 3 &&
	      x[1].rx_buf == e[1].rx_buf && x[1].tx_buf == 0 && x[1].len == 4,
	      "S1 x[0] = tx 3 bytes, x[1] = rx 4 bytes");
	CHECK(x[0].cs_change == 0 && x[1].cs_change == 0 && !memcmp(x, e, sizeof(x)),
	      "S1 cs_change 0 (CS held), speed/bits set, every other byte 0");
	CHECK(access("/dev/spidev0.0", F_OK) != 0 && errno == ENOENT,
	      "guest: spidev loaded but no /dev/spidev* (no controller)");
}

static int cli_read(int argc, char **argv)
{
	int bus = atoi(argv[2]), fd, r, i;
	unsigned int len = argc > 5 ? (unsigned int)strtoul(argv[5], NULL, 0) : 1;
	uint16_t addr = (uint16_t)strtoul(argv[3], NULL, 0);
	uint8_t reg = (uint8_t)strtoul(argv[4], NULL, 0), b[256];

	fd = open_bus(bus);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	r = i2creg_read(fd, addr, reg, b, len);
	if (r < 0) {
		fprintf(stderr, "i2creg: i2c-%d 0x%02x reg 0x%02x: %s\n", bus, addr, reg, strerror(-r));
		return 1;
	}
	printf("i2c-%d 0x%02x reg 0x%02x:", bus, addr, reg);
	for (i = 0; i < r; i++)
		printf(" %02x", b[i]);
	putchar('\n');
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	int sbus, fbus, fs, ff;

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc >= 5 && !strcmp(argv[1], "read"))
		return cli_read(argc, argv);
	if (argc != 4 || strcmp(argv[1], "test")) {
		fprintf(stderr, "usage: i2creg test STUBBUS FAKEBUS | read BUS ADDR REG [LEN]\n");
		return 2;
	}
	sbus = atoi(argv[2]);
	fbus = atoi(argv[3]);
	fs = open_bus(sbus);
	ff = open_bus(fbus);
	if (fs < 0 || ff < 0) {
		perror("open /dev/i2c-N");
		return 1;
	}
	test_part_a(fs, ff, sbus, fbus);
	test_part_b(fs, ff, sbus, fbus);
	test_part_c(ff, fbus);
	test_spi();
	close(fs);
	close(ff);
	printf("%d/%d passed\n", g_pass, g_total);
	return g_pass == g_total ? 0 : 1;
}
