/* ENV: qemu */
// SPDX-License-Identifier: GPL-2.0
/*
 * ELX-B11 test fixture: a tiny I2C adapter WITH a real .xfer (I2C level).
 *
 * Why it exists: i2c-stub only implements .smbus_xfer, so I2C_RDWR on it
 * fails with -EOPNOTSUPP (drivers/i2c/i2c-dev.c checks I2C_FUNC_I2C first).
 * To test a write-then-read (repeated START) for real we need an adapter
 * whose .xfer() receives struct i2c_msg[] -- like every real SoC controller.
 *
 * The "chip" at 0x48 is a byte-addressed register file with an internal
 * pointer, the usual sensor/EEPROM model:
 *   write msg  [reg, d0, d1, ...] -> pointer = reg, store d0.. with auto-inc
 *   read  msg  [r0, r1, ...]      -> bytes from pointer with auto-inc
 *   0x00/0x01  temperature, TMP102 format (12-bit left-justified, BE)
 *   0x0F       ID register 0xB1 (read-only, writes ignored)
 *   0x10..0xFF pattern (i * 7 + 3)
 * Any other address = no ACK = -ENXIO (Documentation/i2c/fault-codes.rst).
 *
 * Read-only module params let the user test SEE the transaction shape:
 *   xfer_calls  number of .xfer() calls (1 call = START ... STOP)
 *   last_nmsgs  messages in the last call (2 = write + Sr + read)
 *   stop_resets_ptr (rw) emulate a chip that forgets its pointer at STOP
 *
 * Load only in the QEMU guest (6.18.55-dom). Never insmod into the VM.
 */
#include <linux/i2c.h>
#include <linux/module.h>

#define FAKE_ADDR	0x48
#define FAKE_ID_REG	0x0f
#define FAKE_ID		0xb1

static u8 regs[256];
static u8 ptr;

static unsigned int xfer_calls;
module_param(xfer_calls, uint, 0444);
MODULE_PARM_DESC(xfer_calls, "number of .xfer() calls (transactions)");

static unsigned int last_nmsgs;
module_param(last_nmsgs, uint, 0444);
MODULE_PARM_DESC(last_nmsgs, "i2c_msg count of the last transaction");

static bool stop_resets_ptr;
module_param(stop_resets_ptr, bool, 0644);
MODULE_PARM_DESC(stop_resets_ptr, "register pointer returns to 0 at STOP");

/* Called with the adapter bus lock held: no extra locking needed. */
static int fake_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	int i, j, ret = num;

	xfer_calls++;
	last_nmsgs = num;
	for (i = 0; i < num; i++) {
		struct i2c_msg *m = &msgs[i];

		/* (repeated) START + address: nobody else answers -> NACK */
		if (m->addr != FAKE_ADDR || (m->flags & I2C_M_TEN)) {
			ret = -ENXIO;
			break;
		}
		if (m->flags & I2C_M_RECV_LEN) {	/* not advertised */
			ret = -EOPNOTSUPP;
			break;
		}
		if (m->flags & I2C_M_RD) {
			for (j = 0; j < m->len; j++)
				m->buf[j] = regs[ptr++];	/* u8: wraps */
		} else if (m->len) {
			ptr = m->buf[0];
			for (j = 1; j < m->len; j++, ptr++)
				if (ptr != FAKE_ID_REG)
					regs[ptr] = m->buf[j];
		}
		/* len == 0 write = SMBus quick: address phase only */
	}
	/* STOP */
	if (stop_resets_ptr)
		ptr = 0;
	return ret;
}

static u32 fake_func(struct i2c_adapter *adap)
{
	/* I2C level + every SMBus op the core can build from i2c_msg */
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm fake_algo = {
	.xfer		= fake_xfer,
	.functionality	= fake_func,
};

static struct i2c_adapter fake_adapter = {
	.owner	= THIS_MODULE,
	.algo	= &fake_algo,
	.name	= "b11-fakei2c",
};

static int __init fake_init(void)
{
	int i;

	for (i = 0x10; i < 256; i++)
		regs[i] = (u8)(i * 7 + 3);
	regs[0x00] = 0x19;		/* 25.0 C = 0x190 << 4 */
	regs[0x01] = 0x00;
	regs[FAKE_ID_REG] = FAKE_ID;
	return i2c_add_adapter(&fake_adapter);
}

static void __exit fake_exit(void)
{
	i2c_del_adapter(&fake_adapter);
}

module_init(fake_init);
module_exit(fake_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ELX-B11 fake I2C adapter with a register-file chip at 0x48");
