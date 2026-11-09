/* ENV: qemu */
// SPDX-License-Identifier: GPL-2.0
/*
 * ELX-M05 demo_bugs: real failures, one per mode (insmod demo_bugs.ko mode=...)
 *   lost       single-slot hand-off between hard handler and thread: edges vanish
 *   nooneshot  request_threaded_irq(irq, NULL, fn, RISING, ...) -> -EINVAL
 *   fixoneshot same call + IRQF_ONESHOT -> works
 *   badtype    IRQF_TRIGGER_HIGH on irq_sim (edge-only chip) -> -EINVAL
 *   sleep_hard msleep() in the hard handler -> BUG: sleeping function ...
 *   getvalue   gpiod_get_value() on a can_sleep chip from the hard handler -> WARNING
 */
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>

static char *mode = "lost";
module_param(mode, charp, 0444);
static int hard, runs, handled, gate, req_ret = 12345;
module_param(hard, int, 0444);
module_param(runs, int, 0444);
module_param(handled, int, 0444);
module_param(gate, int, 0644);
module_param(req_ret, int, 0444);

static struct platform_device *g_pdev;
static struct gpio_desc *g_gpio;
static int g_irq, g_ok;
static u64 g_slot;		/* BUG: one slot for possibly many edges */
static bool g_slot_valid;

static struct gpiod_lookup_table demo_lookup = {
	.dev_id = "elx_m05_demo",
	.table = { GPIO_LOOKUP("m05-sig", U16_MAX, "sig", GPIO_ACTIVE_HIGH), { } },
};

static irqreturn_t demo_hard(int irq, void *d)
{
	WRITE_ONCE(hard, hard + 1);
	WRITE_ONCE(g_slot, ktime_get_ns());
	WRITE_ONCE(g_slot_valid, true);
	if (!strcmp(mode, "sleep_hard"))
		msleep(1);			/* wrong: atomic context */
	if (!strcmp(mode, "getvalue"))
		pr_info("demo: level=%d\n", gpiod_get_value(g_gpio));	/* wrong: can_sleep chip */
	return IRQ_WAKE_THREAD;
}

static irqreturn_t demo_thread(int irq, void *d)
{
	int waited = 0;

	WRITE_ONCE(runs, runs + 1);
	while (READ_ONCE(gate) && waited++ < 8000)
		msleep(1);
	if (READ_ONCE(g_slot_valid)) {
		WRITE_ONCE(g_slot_valid, false);	/* BUG: one wake-up, one slot */
		WRITE_ONCE(handled, handled + 1);
	}
	return IRQ_HANDLED;
}

static int __init demo_init(void)
{
	unsigned long flags = IRQF_TRIGGER_RISING | IRQF_ONESHOT;
	irq_handler_t h = demo_hard;
	int ret;

	gpiod_add_lookup_table(&demo_lookup);
	g_pdev = platform_device_register_simple("elx_m05_demo", -1, NULL, 0);
	if (IS_ERR(g_pdev))
		return PTR_ERR(g_pdev);
	g_gpio = gpiod_get(&g_pdev->dev, "sig", GPIOD_IN);
	if (IS_ERR(g_gpio))
		return PTR_ERR(g_gpio);
	g_irq = gpiod_to_irq(g_gpio);

	if (!strcmp(mode, "nooneshot")) {
		h = NULL;
		flags = IRQF_TRIGGER_RISING;
	} else if (!strcmp(mode, "fixoneshot")) {
		h = NULL;
	} else if (!strcmp(mode, "badtype")) {
		flags = IRQF_TRIGGER_HIGH | IRQF_ONESHOT;
	}
	ret = request_threaded_irq(g_irq, h, demo_thread, flags, "elx_m05_demo", NULL);
	req_ret = ret;
	pr_info("demo mode=%s irq=%d request_threaded_irq() returned %d\n", mode, g_irq, ret);
	if (!ret)
		g_ok = 1;
	return 0;	/* keep the module loaded so the outcome can be inspected */
}

static void __exit demo_exit(void)
{
	WRITE_ONCE(gate, 0);
	if (g_ok)
		free_irq(g_irq, NULL);
	gpiod_put(g_gpio);
	platform_device_unregister(g_pdev);
	gpiod_remove_lookup_table(&demo_lookup);
}

module_init(demo_init);
module_exit(demo_exit);
MODULE_LICENSE("GPL");
