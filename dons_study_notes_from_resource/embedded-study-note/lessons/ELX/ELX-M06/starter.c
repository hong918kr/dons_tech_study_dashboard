/* ENV: qemu */
// SPDX-License-Identifier: GPL-2.0
/*
 * ELX-M06 — the same work item done by five deferral mechanisms.
 * Linux 6.18.55-dbg, QEMU TCG cortex-a57 -smp 4 (gpio-sim line = IRQ source).
 *
 *   hard IRQ (bhc_hard): stamp t0, dispatch to the selected mechanism
 *   bottom half (bhc_body): t1 - t0, record context, do the checksum work
 *
 * sysfs: /sys/kernel/bh_cmp/{mode,done,stats,irq,sleep_us,reset}
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/interrupt.h>
#include <linux/kobject.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>

#define BHC_SAMPLES 4096

enum bhc_mech {
	MECH_TASKLET, MECH_WQ_SYS, MECH_WQ_HI, MECH_THREADED, MECH_WQ_BH, MECH_NR,
};

static const char *const mech_name[MECH_NR] = {
	"tasklet", "wq_sys", "wq_hi", "threaded", "wq_bh",
};

#define CTX_HARDIRQ	1u
#define CTX_SOFTIRQ	2u
#define CTX_TASK	4u
#define CTX_CAN_SLEEP	8u

struct bhc_work {
	struct work_struct work;
	enum bhc_mech mech;
};

struct bhc_stats {
	unsigned long done;		/* release-store after each sample */
	u32 ctx_and, ctx_or;
	char comm[TASK_COMM_LEN];	/* last `current` seen in the body */
	u32 lat_ns[BHC_SAMPLES];
};

static struct {
	struct gpio_device *gdev;
	struct gpio_desc *desc;
	int irq;
	atomic_t mode;
	u64 t0_ns;
	atomic_long_t irqs;
	u32 hard_ctx_or;
	u32 sleep_us;
	struct tasklet_struct tl;
	struct bhc_work wq_sys_w, wq_hi_w, wq_bh_w;
	struct workqueue_struct *wq_hi, *wq_bh;
	struct bhc_stats st[MECH_NR];
	struct kobject *kobj;
	u8 payload[256];
} g;

static bool bad_tasklet_sleep;
module_param(bad_tasklet_sleep, bool, 0444);
MODULE_PARM_DESC(bad_tasklet_sleep, "demo only: call msleep(1) from the tasklet body");

static char *chip_label = "bhc-chip";
module_param(chip_label, charp, 0444);

/* TODO 1 ------------------------------------------------------------------ */
/* Report where we are running, from the code itself. */
static u32 bhc_context_flags(void)
{
	/* TODO: set CTX_HARDIRQ / CTX_SOFTIRQ / CTX_TASK from in_hardirq(),
	 * in_serving_softirq() and in_task(), and CTX_CAN_SLEEP when the code
	 * runs in task context with preemption and IRQs enabled.
	 */
	return 0;
}

/* TODO 2 ------------------------------------------------------------------ */
/* Hand the event to the selected mechanism; called from the hard handler. */
static irqreturn_t bhc_dispatch(enum bhc_mech m)
{
	/* TODO: switch (m): tasklet_schedule, queue_work on system_percpu_wq,
	 * g.wq_hi or g.wq_bh, return IRQ_HANDLED; for MECH_THREADED return
	 * IRQ_WAKE_THREAD so the core wakes bhc_thread_fn.
	 */
	(void)m;
	return IRQ_HANDLED;
}

/* The one work item every mechanism runs. */
static void bhc_body(enum bhc_mech m)
{
	u64 t1 = ktime_get_ns();
	struct bhc_stats *st = &g.st[m];
	unsigned long n = st->done;
	u32 ctx = bhc_context_flags();
	u32 sum = 0;
	int i;

	if (n < BHC_SAMPLES)
		st->lat_ns[n] = (u32)(t1 - READ_ONCE(g.t0_ns));
	if (n == 0) {
		st->ctx_and = ctx;
		st->ctx_or = ctx;
	} else {
		st->ctx_and &= ctx;
		st->ctx_or |= ctx;
	}
	strscpy(st->comm, current->comm, sizeof(st->comm));

	for (i = 0; i < ARRAY_SIZE(g.payload); i++)
		sum = sum * 31 + g.payload[i];
	g.payload[0] = (u8)sum;

	if (bad_tasklet_sleep && m == MECH_TASKLET)
		msleep(1);	/* WRONG on purpose: softirq context */
	else if (g.sleep_us && (ctx & CTX_CAN_SLEEP))
		usleep_range(g.sleep_us, g.sleep_us + 50);

	smp_store_release(&st->done, n + 1);
}

static void __maybe_unused bhc_tasklet_fn(struct tasklet_struct *t)
{
	bhc_body(MECH_TASKLET);
}

static void __maybe_unused bhc_work_fn(struct work_struct *work)
{
	struct bhc_work *bw = container_of(work, struct bhc_work, work);

	bhc_body(bw->mech);
}

static irqreturn_t bhc_thread_fn(int irq, void *data)
{
	bhc_body(MECH_THREADED);
	return IRQ_HANDLED;
}

static irqreturn_t bhc_hard(int irq, void *data)
{
	WRITE_ONCE(g.t0_ns, ktime_get_ns());
	g.hard_ctx_or |= bhc_context_flags();
	atomic_long_inc(&g.irqs);
	return bhc_dispatch(atomic_read(&g.mode));
}

/* TODO 3 ------------------------------------------------------------------ */
static int bhc_setup_bottom_halves(void)
{
	/* TODO: tasklet_setup(); INIT_WORK() + .mech for the three bhc_work
	 * items; g.wq_hi = alloc_workqueue(WQ_HIGHPRI | WQ_PERCPU, 0);
	 * g.wq_bh = alloc_workqueue(WQ_BH | WQ_PERCPU, 0) (max_active must be 0).
	 */
	return 0;
}

/* free_irq() already ran: nothing can queue new work. */
static void bhc_teardown_bottom_halves(void)
{
	/* TODO: tasklet_kill(), cancel_work_sync() each work, destroy_workqueue(). */
}
/* end of TODOs ------------------------------------------------------------ */

static int cmp_u32(const void *a, const void *b)
{
	u32 x = *(const u32 *)a, y = *(const u32 *)b;

	return x < y ? -1 : x > y;
}

static const char *ctx_str(u32 f, char *buf, size_t len)
{
	snprintf(buf, len, "%s%s%s%s%s",
		 f & CTX_HARDIRQ ? "HARD" : "", f & CTX_SOFTIRQ ? "SOFT" : "",
		 f & CTX_TASK ? "TASK" : "", f & CTX_CAN_SLEEP ? "+SLEEP" : "",
		 f ? "" : "NONE");
	return buf;
}

static ssize_t stats_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	u32 *tmp = kmalloc_array(BHC_SAMPLES, sizeof(u32), GFP_KERNEL);
	int m, off = 0;
	char cbuf[32];

	if (!tmp)
		return -ENOMEM;
	off += sysfs_emit_at(buf, off, "hard ctx=%s irqs=%ld\n",
			     ctx_str(g.hard_ctx_or, cbuf, sizeof(cbuf)),
			     atomic_long_read(&g.irqs));
	for (m = 0; m < MECH_NR; m++) {
		struct bhc_stats *st = &g.st[m];
		unsigned long n = smp_load_acquire(&st->done);
		unsigned long cnt = min_t(unsigned long, n, BHC_SAMPLES);
		u32 min = 0, p50 = 0, p99 = 0, max = 0;

		if (cnt) {
			memcpy(tmp, st->lat_ns, cnt * sizeof(u32));
			sort(tmp, cnt, sizeof(u32), cmp_u32, NULL);
			min = tmp[0];
			p50 = tmp[cnt / 2];
			p99 = tmp[min_t(unsigned long, cnt * 99 / 100, cnt - 1)];
			max = tmp[cnt - 1];
		}
		off += sysfs_emit_at(buf, off,
				     "%s n=%lu ctx=%s stable=%d comm=%s min=%u p50=%u p99=%u max=%u\n",
				     mech_name[m], n,
				     ctx_str(st->ctx_or, cbuf, sizeof(cbuf)),
				     st->ctx_and == st->ctx_or, st->comm,
				     min, p50, p99, max);
	}
	kfree(tmp);
	return off;
}

static ssize_t done_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	int m = atomic_read(&g.mode);

	return sysfs_emit(buf, "%lu\n", smp_load_acquire(&g.st[m].done));
}

static ssize_t irq_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%d\n", g.irq);
}

static ssize_t mode_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%s\n", mech_name[atomic_read(&g.mode)]);
}

static ssize_t mode_store(struct kobject *k, struct kobj_attribute *a,
			  const char *buf, size_t n)
{
	int m = sysfs_match_string(mech_name, buf);

	if (m < 0)
		return -EINVAL;
	atomic_set(&g.mode, m);
	return n;
}

static ssize_t sleep_us_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%u\n", g.sleep_us);
}

static ssize_t sleep_us_store(struct kobject *k, struct kobj_attribute *a,
			      const char *buf, size_t n)
{
	u32 v;
	int ret = kstrtou32(buf, 0, &v);

	if (ret)
		return ret;
	g.sleep_us = v;
	return n;
}

static ssize_t reset_store(struct kobject *k, struct kobj_attribute *a,
			   const char *buf, size_t n)
{
	int m;

	for (m = 0; m < MECH_NR; m++)
		smp_store_release(&g.st[m].done, 0);
	atomic_long_set(&g.irqs, 0);
	g.hard_ctx_or = 0;
	return n;
}

static struct kobj_attribute a_mode = __ATTR_RW(mode);
static struct kobj_attribute a_sleep = __ATTR_RW(sleep_us);
static struct kobj_attribute a_done = __ATTR_RO(done);
static struct kobj_attribute a_stats = __ATTR_RO(stats);
static struct kobj_attribute a_irq = __ATTR_RO(irq);
static struct kobj_attribute a_reset = __ATTR_WO(reset);

static struct attribute *bhc_attrs[] = {
	&a_mode.attr, &a_sleep.attr, &a_done.attr, &a_stats.attr,
	&a_irq.attr, &a_reset.attr, NULL,
};
ATTRIBUTE_GROUPS(bhc);

static int __init bhc_init(void)
{
	int ret;

	g.gdev = gpio_device_find_by_label(chip_label);
	if (!g.gdev) {
		pr_err("bh_cmp: gpio chip '%s' not found\n", chip_label);
		return -ENODEV;
	}
	/* gpio-sim only fires the IRQ for a requested input line. */
	g.desc = gpiochip_request_own_desc(gpio_device_get_chip(g.gdev), 0, "bh_cmp",
					   GPIO_ACTIVE_HIGH, GPIOD_IN);
	if (IS_ERR(g.desc)) {
		ret = PTR_ERR(g.desc);
		goto put_dev;
	}
	g.irq = gpiod_to_irq(g.desc);
	if (g.irq < 0) {
		ret = g.irq;
		goto free_desc;
	}

	ret = bhc_setup_bottom_halves();
	if (ret)
		goto free_desc;

	ret = request_threaded_irq(g.irq, bhc_hard, bhc_thread_fn,
				   IRQF_TRIGGER_RISING, "bh_cmp", &g);
	if (ret) {
		pr_err("bh_cmp: request_threaded_irq: %d\n", ret);
		goto teardown;
	}

	g.kobj = kobject_create_and_add("bh_cmp", kernel_kobj);
	if (!g.kobj) {
		ret = -ENOMEM;
		goto freeirq;
	}
	ret = sysfs_create_groups(g.kobj, bhc_groups);
	if (ret)
		goto putk;
	pr_info("bh_cmp: ready irq=%d\n", g.irq);
	return 0;

putk:
	kobject_put(g.kobj);
freeirq:
	free_irq(g.irq, &g);
teardown:
	bhc_teardown_bottom_halves();
free_desc:
	gpiochip_free_own_desc(g.desc);
put_dev:
	gpio_device_put(g.gdev);
	return ret;
}

static void __exit bhc_exit(void)
{
	sysfs_remove_groups(g.kobj, bhc_groups);
	kobject_put(g.kobj);
	free_irq(g.irq, &g);	/* first: stop new events, waits for the thread */
	bhc_teardown_bottom_halves();
	gpiochip_free_own_desc(g.desc);
	gpio_device_put(g.gdev);
}

module_init(bhc_init);
module_exit(bhc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ELX-M06: tasklet vs workqueue vs threaded IRQ vs WQ_BH");
