/* ENV: qemu */
// SPDX-License-Identifier: GPL-2.0
/*
 * ELX-M05 solution: gpio-sim line -> gpiod_to_irq -> request_threaded_irq.
 *   hard handler  : timestamp + push one event into a kfifo, return IRQ_WAKE_THREAD (no sleeping here)
 *   thread handler: drains every queued event (may sleep), counts it, records hard->thread latency
 *   sysfs         : /sys/devices/platform/elx_m05/{irq,stats,latency,gate,reset,lat_inject,work_us}
 * Built only for Linux 6.18.55-dbg in a QEMU guest. See check.sh.
 */
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/hardirq.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/ktime.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/sysfs.h>

#define M05_NAME	"elx_m05"
#define M05_LINE	"m05-sig"	/* configfs: bank0/line0/name */
#define M05_FIFO_SIZE	256		/* kfifo needs a power of two */
#define M05_MAX_SAMPLES	4096
#define M05_HIST_BUCKETS 40
#define M05_GATE_TIMEOUT_MS 8000	/* a forgotten gate never hangs rmmod */

struct m05_event {
	u64 ts_ns;
	u32 seq;
};

struct m05 {
	struct platform_device *pdev;
	struct gpio_desc *gpio;
	int irq;
	bool irq_requested;

	/* SPSC: the hard handler is the only writer, the IRQ thread the only reader. */
	DECLARE_KFIFO(fifo, struct m05_event, M05_FIFO_SIZE);
	u32 seq;

	atomic_t hard_count;
	atomic_t thread_runs;
	atomic_t processed;
	atomic_t dropped;
	atomic_t hard_ctx_bad;
	atomic_t thread_ctx_bad;

	/* Forced schedule for tests: parks the thread so edges pile up. */
	bool gate_on;
	struct completion gate;
	unsigned int work_us;

	/* Written by the thread, read by sysfs: neither side is atomic, so a mutex is fine. */
	struct mutex lat_lock;
	u64 *lat_ns;
	u32 lat_n;
	u64 lat_min, lat_max, lat_sum;
	u32 hist[M05_HIST_BUCKETS];
};

static struct gpiod_lookup_table m05_lookup = {
	.dev_id = M05_NAME,
	.table = {
		/* chip_hwnum == U16_MAX: key is a line name, not a chip label */
		GPIO_LOOKUP(M05_LINE, U16_MAX, "sig", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static void m05_lat_add(struct m05 *d, u64 ns)
{
	mutex_lock(&d->lat_lock);
	if (d->lat_n < M05_MAX_SAMPLES)
		d->lat_ns[d->lat_n] = ns;
	d->lat_n++;
	d->lat_sum += ns;
	if (d->lat_n == 1 || ns < d->lat_min)
		d->lat_min = ns;
	if (ns > d->lat_max)
		d->lat_max = ns;
	d->hist[min_t(u32, ns ? ilog2(ns) : 0, M05_HIST_BUCKETS - 1)]++;
	mutex_unlock(&d->lat_lock);
}

static irqreturn_t m05_hard(int irq, void *dev_id)
{
	struct m05 *d = dev_id;
	struct m05_event ev;

	/* TODO(1): timestamp only; no sleeping. Count the edge, queue the event, wake the thread. */
	ev.ts_ns = ktime_get_ns();
	ev.seq = d->seq++;
	atomic_inc(&d->hard_count);
	if (!in_hardirq())
		atomic_inc(&d->hard_ctx_bad);
	if (!kfifo_put(&d->fifo, ev))
		atomic_inc(&d->dropped);
	return IRQ_WAKE_THREAD;
}

static irqreturn_t m05_thread(int irq, void *dev_id)
{
	struct m05 *d = dev_id;
	struct m05_event ev;

	atomic_inc(&d->thread_runs);
	if (!in_task())
		atomic_inc(&d->thread_ctx_bad);
	if (READ_ONCE(d->gate_on))
		wait_for_completion_timeout(&d->gate, msecs_to_jiffies(M05_GATE_TIMEOUT_MS));

	/* TODO(2): one wake-up can stand for many edges: drain until the fifo is empty. */
	while (kfifo_get(&d->fifo, &ev)) {
		unsigned int us = READ_ONCE(d->work_us);

		m05_lat_add(d, ktime_get_ns() - ev.ts_ns);	/* hard stamp -> thread pickup */
		if (us)
			usleep_range(us, us + 50);	/* the "slow I2C read": legal here */
		atomic_inc(&d->processed);
	}
	return IRQ_HANDLED;
}

static int m05_request_irq(struct m05 *d)
{
	int ret;

	/* TODO(3): hard + thread handler, rising edge, ONESHOT, name M05_NAME, dev_id d. */
	ret = request_threaded_irq(d->irq, m05_hard, m05_thread,
				   IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				   M05_NAME, d);
	if (!ret)
		d->irq_requested = true;
	return ret;
}

/* Nearest-rank percentile of an ascending array; n >= 1, 1 <= pct <= 100. */
static u64 m05_percentile(const u64 *sorted, u32 n, u32 pct)
{
	/* TODO(4): rank = ceil(pct * n / 100), 1-based. */
	return sorted[DIV_ROUND_UP(pct * n, 100) - 1];
}

static int m05_cmp_u64(const void *a, const void *b)
{
	u64 x = *(const u64 *)a, y = *(const u64 *)b;

	return x < y ? -1 : x > y;
}

static struct m05 *to_m05(struct device *dev)
{
	return dev_get_drvdata(dev);
}

static ssize_t irq_show(struct device *dev, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%d\n", to_m05(dev)->irq);
}

static ssize_t stats_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct m05 *d = to_m05(dev);

	return sysfs_emit(buf, "hard=%d thread_runs=%d processed=%d dropped=%d hard_ctx_bad=%d thread_ctx_bad=%d\n",
			  atomic_read(&d->hard_count), atomic_read(&d->thread_runs),
			  atomic_read(&d->processed), atomic_read(&d->dropped),
			  atomic_read(&d->hard_ctx_bad), atomic_read(&d->thread_ctx_bad));
}

static ssize_t latency_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct m05 *d = to_m05(dev);
	u64 *copy, sum, min, max;
	u32 n, stored, i, h[M05_HIST_BUCKETS];
	int len;

	mutex_lock(&d->lat_lock);
	n = d->lat_n;
	stored = min_t(u32, n, M05_MAX_SAMPLES);
	copy = stored ? kmemdup(d->lat_ns, stored * sizeof(u64), GFP_KERNEL) : NULL;
	sum = d->lat_sum;
	min = d->lat_min;
	max = d->lat_max;
	memcpy(h, d->hist, sizeof(h));
	mutex_unlock(&d->lat_lock);

	if (!n)
		return sysfs_emit(buf, "n=0\n");
	if (!copy)
		return -ENOMEM;
	sort(copy, stored, sizeof(u64), m05_cmp_u64, NULL);
	len = sysfs_emit(buf, "n=%u min_ns=%llu avg_ns=%llu p50_ns=%llu p99_ns=%llu max_ns=%llu\n",
			 n, min, div_u64(sum, n), m05_percentile(copy, stored, 50),
			 m05_percentile(copy, stored, 99), max);
	kfree(copy);
	for (i = 0; i < M05_HIST_BUCKETS; i++)
		if (h[i])
			len += sysfs_emit_at(buf, len, "hist 2^%u_ns: %u\n", i, h[i]);
	return len;
}

static ssize_t gate_store(struct device *dev, struct device_attribute *a,
			  const char *buf, size_t count)
{
	struct m05 *d = to_m05(dev);
	bool on;
	int ret = kstrtobool(buf, &on);

	if (ret)
		return ret;
	if (on) {
		reinit_completion(&d->gate);
		WRITE_ONCE(d->gate_on, true);
	} else {
		WRITE_ONCE(d->gate_on, false);
		complete_all(&d->gate);
	}
	return count;
}

static void m05_reset_counts(struct m05 *d)
{
	struct m05_event ev;

	/* disable_irq() waits for the hard handler and the thread: nobody is touching the fifo now. */
	if (d->irq_requested)
		disable_irq(d->irq);
	while (kfifo_get(&d->fifo, &ev))
		;
	atomic_set(&d->hard_count, 0);
	atomic_set(&d->thread_runs, 0);
	atomic_set(&d->processed, 0);
	atomic_set(&d->dropped, 0);
	atomic_set(&d->hard_ctx_bad, 0);
	atomic_set(&d->thread_ctx_bad, 0);
	mutex_lock(&d->lat_lock);
	d->lat_n = 0;
	d->lat_sum = 0;
	d->lat_min = 0;
	d->lat_max = 0;
	memset(d->hist, 0, sizeof(d->hist));
	mutex_unlock(&d->lat_lock);
	if (d->irq_requested)
		enable_irq(d->irq);
}

static ssize_t reset_store(struct device *dev, struct device_attribute *a,
			   const char *buf, size_t count)
{
	m05_reset_counts(to_m05(dev));
	return count;
}

static ssize_t lat_inject_store(struct device *dev, struct device_attribute *a,
				const char *buf, size_t count)
{
	u64 v;
	int ret = kstrtou64(buf, 0, &v);

	if (ret)
		return ret;
	m05_lat_add(to_m05(dev), v);
	return count;
}

static ssize_t work_us_show(struct device *dev, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%u\n", READ_ONCE(to_m05(dev)->work_us));
}

static ssize_t work_us_store(struct device *dev, struct device_attribute *a,
			     const char *buf, size_t count)
{
	unsigned int v;
	int ret = kstrtouint(buf, 0, &v);

	if (ret)
		return ret;
	WRITE_ONCE(to_m05(dev)->work_us, v);
	return count;
}

static DEVICE_ATTR_RO(irq);
static DEVICE_ATTR_RO(stats);
static DEVICE_ATTR_RO(latency);
static DEVICE_ATTR_WO(gate);
static DEVICE_ATTR_WO(reset);
static DEVICE_ATTR_WO(lat_inject);
static DEVICE_ATTR_RW(work_us);

static struct attribute *m05_attrs[] = {
	&dev_attr_irq.attr, &dev_attr_stats.attr, &dev_attr_latency.attr,
	&dev_attr_gate.attr, &dev_attr_reset.attr, &dev_attr_lat_inject.attr,
	&dev_attr_work_us.attr, NULL,
};
static const struct attribute_group m05_group = { .attrs = m05_attrs };

static struct m05 *g_m05;

static int __init m05_init(void)
{
	struct m05 *d;
	int ret;

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	d->lat_ns = kcalloc(M05_MAX_SAMPLES, sizeof(u64), GFP_KERNEL);
	if (!d->lat_ns) {
		ret = -ENOMEM;
		goto err_free;
	}
	INIT_KFIFO(d->fifo);
	mutex_init(&d->lat_lock);
	init_completion(&d->gate);
	d->work_us = 100;

	gpiod_add_lookup_table(&m05_lookup);
	d->pdev = platform_device_register_simple(M05_NAME, -1, NULL, 0);
	if (IS_ERR(d->pdev)) {
		ret = PTR_ERR(d->pdev);
		goto err_lookup;
	}
	dev_set_drvdata(&d->pdev->dev, d);

	d->gpio = gpiod_get(&d->pdev->dev, "sig", GPIOD_IN);
	if (IS_ERR(d->gpio)) {
		ret = PTR_ERR(d->gpio);
		goto err_pdev;
	}
	d->irq = gpiod_to_irq(d->gpio);
	if (d->irq < 0) {
		ret = d->irq;
		goto err_gpio;
	}

	ret = m05_request_irq(d);
	if (ret)
		goto err_gpio;

	ret = sysfs_create_group(&d->pdev->dev.kobj, &m05_group);
	if (ret)
		goto err_irq;
	g_m05 = d;
	pr_info(M05_NAME ": irq=%d gpio line %s\n", d->irq, M05_LINE);
	return 0;

err_irq:
	if (d->irq_requested)
		free_irq(d->irq, d);
err_gpio:
	gpiod_put(d->gpio);
err_pdev:
	platform_device_unregister(d->pdev);
err_lookup:
	gpiod_remove_lookup_table(&m05_lookup);
	kfree(d->lat_ns);
err_free:
	kfree(d);
	return ret;
}

static void __exit m05_exit(void)
{
	struct m05 *d = g_m05;

	sysfs_remove_group(&d->pdev->dev.kobj, &m05_group);
	if (d->irq_requested)
		free_irq(d->irq, d);	/* waits for the thread and joins it */
	gpiod_put(d->gpio);
	platform_device_unregister(d->pdev);
	gpiod_remove_lookup_table(&m05_lookup);
	kfree(d->lat_ns);
	kfree(d);
}

module_init(m05_init);
module_exit(m05_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ELX-M05 threaded IRQ on a gpio-sim line");
