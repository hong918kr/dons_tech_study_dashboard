/* ENV: qemu */
// SPDX-License-Identifier: GPL-2.0
/*
 * ELX-M04 practice: the base driver plus five more correct patterns.
 *
 * P1 raw_irqsave  raw_spinlock_t in a HRTIMER_MODE_REL_HARD callback
 * P2 wq_mutex     mutex kept out of hard IRQ: the IRQ path defers to a work item
 * P3 spin_irq     process path spin_lock_irq(), hard-IRQ path plain spin_lock()
 * P4 percpu       per-CPU counter, summed on read (counter only)
 * P5 nested       two instances of one lock class, address order + spin_lock_nested
 *
 * Runs only in a QEMU guest (Image-dbg, 6.18.55-dbg). Never insmod this into
 * the Lima VM's running kernel.
 */
#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/hardirq.h>
#include <linux/hrtimer.h>
#include <linux/irqflags.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include "syncdrv_uapi.h"

#define SD_POOL_SIZE 200000	/* nodes are never freed during a run */

struct sd_node {
	struct list_head link;
	u64 seq;
};

static struct sd_dev {
	u32 mode;
	bool with_list;
	bool timer_on;
	enum hrtimer_mode tmode;
	ktime_t period;
	struct hrtimer timer;

	struct mutex mtx;
	spinlock_t lock;
	spinlock_t lock_a, lock_b;	/* the ABBA pair: two lock classes */
	spinlock_t inst[2];		/* one init site: one lock class */
	raw_spinlock_t rlock;		/* P1 */
	struct work_struct work;	/* P2 */
	atomic_long_t pending;		/* P2: IRQ-path updates not applied yet */

	u64 counter;			/* plain: protected (or not) by mode */
	atomic_long_t acounter;		/* SD_MODE_ATOMIC only */
	struct list_head head;

	struct sd_node *pool;
	atomic_t pool_next;
	atomic_long_t pool_exhausted;
	atomic_long_t irq_fires;
	atomic_t not_hardirq;
} sd;

static DEFINE_PER_CPU(unsigned long, sd_pcount);	/* P4 */

/* ---------- the shared state the two paths fight over ---------- */

static void sd_touch_list(void)
{
	int i;

	if (!sd.with_list)
		return;
	i = atomic_fetch_inc(&sd.pool_next);
	if (i >= SD_POOL_SIZE) {
		atomic_long_inc(&sd.pool_exhausted);
		return;
	}
	sd.pool[i].seq = i;
	list_add(&sd.pool[i].link, &sd.head);
}

/* READ_ONCE/WRITE_ONCE stop the compiler folding the loop; still not atomic. */
static void sd_touch(void)
{
	WRITE_ONCE(sd.counter, READ_ONCE(sd.counter) + 1);
	sd_touch_list();
}

/* ---------- TODO: the two correct protections ---------- */

/* Must keep working when an IRQ on this CPU also wants sd.lock. */
static void sd_irqsave_lock(unsigned long *flags)
{
	spin_lock_irqsave(&sd.lock, *flags);
}

static void sd_irqsave_unlock(unsigned long flags)
{
	spin_unlock_irqrestore(&sd.lock, flags);
}

/* Counter via sd.acounter. The list stays as it is (see Part C). */
static void sd_atomic_touch(void)
{
	atomic_long_inc(&sd.acounter);
	sd_touch_list();
}

/* ---------- practice extras ---------- */

/* P2: runs in process context, so it may take the mutex. */
static void sd_work_fn(struct work_struct *w)
{
	long n;

	mutex_lock(&sd.mtx);
	n = atomic_long_xchg(&sd.pending, 0);
	while (n-- > 0)
		sd_touch();
	mutex_unlock(&sd.mtx);
}

/* P5: two instances of one class. Address order + a nesting annotation. */
static void sd_pair_lock(spinlock_t *a, spinlock_t *b)
{
	if (a > b)
		swap(a, b);
	spin_lock(a);
	spin_lock_nested(b, SINGLE_DEPTH_NESTING);
}

static void sd_pair_unlock(spinlock_t *a, spinlock_t *b)
{
	if (a > b)
		swap(a, b);
	spin_unlock(b);
	spin_unlock(a);
}

/* ---------- the two writer paths ---------- */

static void sd_update(bool from_irq)
{
	unsigned long flags;

	switch (sd.mode) {
	case SD_MODE_NONE:
		sd_touch();
		break;
	case SD_MODE_MUTEX:
		mutex_lock(&sd.mtx);
		sd_touch();
		mutex_unlock(&sd.mtx);
		break;
	case SD_MODE_SPIN:
		spin_lock(&sd.lock);
		sd_touch();
		spin_unlock(&sd.lock);
		break;
	case SD_MODE_IRQSAVE:
		sd_irqsave_lock(&flags);
		sd_touch();
		sd_irqsave_unlock(flags);
		break;
	case SD_MODE_ATOMIC:
		sd_atomic_touch();
		break;
	case SD_MODE_RAW_IRQSAVE:	/* P1: raw_ never sleeps, even on RT */
		raw_spin_lock_irqsave(&sd.rlock, flags);
		sd_touch();
		raw_spin_unlock_irqrestore(&sd.rlock, flags);
		break;
	case SD_MODE_WQ_MUTEX:		/* P2 */
		if (from_irq) {
			atomic_long_inc(&sd.pending);
			schedule_work(&sd.work);
		} else {
			mutex_lock(&sd.mtx);
			sd_touch();
			mutex_unlock(&sd.mtx);
		}
		break;
	case SD_MODE_SPIN_IRQ:		/* P3 */
		if (from_irq) {
			spin_lock(&sd.lock);	/* IRQs already off here */
			sd_touch();
			spin_unlock(&sd.lock);
		} else {
			spin_lock_irq(&sd.lock);
			sd_touch();
			spin_unlock_irq(&sd.lock);
		}
		break;
	case SD_MODE_PERCPU:		/* P4: safe vs IRQs on this CPU */
		this_cpu_inc(sd_pcount);
		break;
	}
}

static enum hrtimer_restart sd_timer_fn(struct hrtimer *t)
{
	if (!in_hardirq())
		atomic_inc(&sd.not_hardirq);
	sd_update(true);
	atomic_long_inc(&sd.irq_fires);
	hrtimer_forward_now(t, sd.period);
	return HRTIMER_RESTART;
}

/* ---------- ioctl plumbing (no TODOs below) ---------- */

static void sd_stop_timer(void)
{
	if (sd.timer_on) {
		hrtimer_cancel(&sd.timer);
		sd.timer_on = false;
		flush_work(&sd.work);	/* P2: apply what the IRQ path deferred */
	}
}

static int sd_configure(const struct sd_config *cfg)
{
	int cpu;

	if (cfg->mode >= SD_MODE_COUNT)
		return -EINVAL;
	sd_stop_timer();
	sd.mode = cfg->mode;
	sd.with_list = cfg->with_list;
	sd.counter = 0;
	INIT_LIST_HEAD(&sd.head);
	atomic_long_set(&sd.acounter, 0);
	atomic_set(&sd.pool_next, 0);
	atomic_long_set(&sd.pool_exhausted, 0);
	atomic_long_set(&sd.irq_fires, 0);
	atomic_set(&sd.not_hardirq, 0);
	atomic_long_set(&sd.pending, 0);
	for_each_possible_cpu(cpu)
		per_cpu(sd_pcount, cpu) = 0;
	if (cfg->timer_us) {
		sd.period = us_to_ktime(cfg->timer_us);
		sd.tmode = cfg->timer_hard ? HRTIMER_MODE_REL_HARD : HRTIMER_MODE_REL;
		hrtimer_setup(&sd.timer, sd_timer_fn, CLOCK_MONOTONIC, sd.tmode);
		sd.timer_on = true;
		hrtimer_start(&sd.timer, sd.period, sd.tmode);
	}
	return 0;
}

static void sd_get_stats(struct sd_stats *st)
{
	struct list_head *p;
	u64 n = 0;

	st->walk_overflow = 0;
	list_for_each(p, &sd.head) {
		if (++n > SD_POOL_SIZE + 1) {	/* bounded: never hang on a bad list */
			st->walk_overflow = 1;
			break;
		}
	}
	st->list_len = n;
	st->counter = sd.mode == SD_MODE_ATOMIC ?
		      atomic_long_read(&sd.acounter) : sd.counter;
	if (sd.mode == SD_MODE_PERCPU) {
		int cpu;

		st->counter = 0;
		for_each_possible_cpu(cpu)
			st->counter += per_cpu(sd_pcount, cpu);
	}
	st->irq_fires = atomic_long_read(&sd.irq_fires);
	st->pool_exhausted = atomic_long_read(&sd.pool_exhausted);
	st->not_hardirq = atomic_read(&sd.not_hardirq);
}

static void sd_abba(u32 order)
{
	switch (order) {
	case 0:
		spin_lock(&sd.lock_a);
		spin_lock(&sd.lock_b);
		spin_unlock(&sd.lock_b);
		spin_unlock(&sd.lock_a);
		break;
	case 1:
		spin_lock(&sd.lock_b);
		spin_lock(&sd.lock_a);
		spin_unlock(&sd.lock_a);
		spin_unlock(&sd.lock_b);
		break;
	case 2:		/* two instances, one class */
		spin_lock(&sd.inst[0]);
		spin_lock(&sd.inst[1]);
		spin_unlock(&sd.inst[1]);
		spin_unlock(&sd.inst[0]);
		break;
	case 3:		/* P5 */
		sd_pair_lock(&sd.inst[0], &sd.inst[1]);
		sd_pair_unlock(&sd.inst[0], &sd.inst[1]);
		sd_pair_lock(&sd.inst[1], &sd.inst[0]);
		sd_pair_unlock(&sd.inst[1], &sd.inst[0]);
		break;
	}
}

static void sd_selftest(struct sd_selftest *r)
{
	unsigned long flags;
	int i;

	sd.with_list = false;
	atomic_long_set(&sd.acounter, 0);

	sd_irqsave_lock(&flags);
	r->held = spin_is_locked(&sd.lock);
	r->irqs_off = irqs_disabled();
	sd_irqsave_unlock(flags);
	r->irqs_restored = !irqs_disabled();

	for (i = 0; i < 1000; i++)
		sd_atomic_touch();
	r->atomic_ok = atomic_long_read(&sd.acounter) == 1000;
}

static long sd_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	void __user *up = (void __user *)arg;
	struct sd_selftest r;
	struct sd_config cfg;
	struct sd_stats st;
	u32 n, i;

	switch (cmd) {
	case SD_IOC_CONFIG:
		if (copy_from_user(&cfg, up, sizeof(cfg)))
			return -EFAULT;
		return sd_configure(&cfg);
	case SD_IOC_UPDATE:
		if (copy_from_user(&n, up, sizeof(n)))
			return -EFAULT;
		for (i = 0; i < n; i++)
			sd_update(false);
		return 0;
	case SD_IOC_STOP:
		sd_stop_timer();
		return 0;
	case SD_IOC_GET_STATS:
		if (sd.timer_on)
			return -EBUSY;
		memset(&st, 0, sizeof(st));
		sd_get_stats(&st);
		return copy_to_user(up, &st, sizeof(st)) ? -EFAULT : 0;
	case SD_IOC_ABBA:
		if (copy_from_user(&n, up, sizeof(n)))
			return -EFAULT;
		sd_abba(n);
		return 0;
	case SD_IOC_SELFTEST:
		if (sd.timer_on)
			return -EBUSY;
		memset(&r, 0, sizeof(r));
		sd_selftest(&r);
		return copy_to_user(up, &r, sizeof(r)) ? -EFAULT : 0;
	}
	return -ENOTTY;
}

static const struct file_operations sd_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = sd_ioctl,
};

static struct miscdevice sd_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "syncdrv",
	.fops = &sd_fops,
};

static int __init sd_init(void)
{
	int i, ret;

	mutex_init(&sd.mtx);
	spin_lock_init(&sd.lock);
	raw_spin_lock_init(&sd.rlock);
	INIT_WORK(&sd.work, sd_work_fn);
	spin_lock_init(&sd.lock_a);
	spin_lock_init(&sd.lock_b);
	for (i = 0; i < 2; i++)
		spin_lock_init(&sd.inst[i]);	/* one call site = one class */
	INIT_LIST_HEAD(&sd.head);
	hrtimer_setup(&sd.timer, sd_timer_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	sd.pool = vmalloc_array(SD_POOL_SIZE, sizeof(*sd.pool));
	if (!sd.pool)
		return -ENOMEM;
	ret = misc_register(&sd_misc);
	if (ret)
		vfree(sd.pool);
	return ret;
}

static void __exit sd_exit(void)
{
	misc_deregister(&sd_misc);
	sd_stop_timer();
	cancel_work_sync(&sd.work);
	vfree(sd.pool);
}

module_init(sd_init);
module_exit(sd_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ELX-M04 practice: more correct locking patterns");
