/* ENV: qemu */
/* ELX-M04: ioctl interface shared by the module and the stress.c helper. */
#ifndef SYNCDRV_UAPI_H
#define SYNCDRV_UAPI_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ioctl.h>
#else
#include <linux/types.h>
#include <sys/ioctl.h>
#endif

enum sd_mode {
	SD_MODE_NONE,		/* no protection: the bug */
	SD_MODE_MUTEX,		/* mutex in both paths */
	SD_MODE_SPIN,		/* plain spin_lock in both paths */
	SD_MODE_IRQSAVE,	/* spin_lock_irqsave in both paths */
	SD_MODE_ATOMIC,		/* counter = atomic_long_t, list unprotected */
	SD_MODE_BASE_COUNT,	/* practice.c adds the modes below */
	SD_MODE_RAW_IRQSAVE = SD_MODE_BASE_COUNT,
	SD_MODE_WQ_MUTEX,	/* IRQ path defers to a work item */
	SD_MODE_SPIN_IRQ,	/* spin_lock_irq + spin_lock split */
	SD_MODE_PERCPU,		/* this_cpu counter */
	SD_MODE_COUNT
};

struct sd_config {
	__u32 mode;
	__u32 timer_us;		/* 0 = no IRQ path */
	__u32 timer_hard;	/* 1 = HRTIMER_MODE_REL_HARD, 0 = HRTIMER_MODE_REL */
	__u32 with_list;	/* 0 = counter only */
};

struct sd_stats {
	__u64 counter;
	__u64 list_len;
	__u64 irq_fires;
	__u64 pool_exhausted;
	__u32 not_hardirq;	/* timer callbacks NOT in hard IRQ context */
	__u32 walk_overflow;	/* bounded list walk hit its limit */
};

struct sd_selftest {
	__u32 held;		/* lock really held inside the irqsave helper */
	__u32 irqs_off;		/* IRQs really off inside the irqsave helper */
	__u32 irqs_restored;	/* IRQs on again after the unlock helper */
	__u32 atomic_ok;	/* 1000 atomic helper calls == 1000 */
};

#define SD_MAGIC	's'
#define SD_IOC_CONFIG	_IOW(SD_MAGIC, 1, struct sd_config)	/* reset, start timer */
#define SD_IOC_UPDATE	_IOW(SD_MAGIC, 2, __u32)		/* n updates, process path */
#define SD_IOC_STOP	_IO(SD_MAGIC, 3)			/* stop the timer */
#define SD_IOC_GET_STATS _IOR(SD_MAGIC, 4, struct sd_stats)	/* -EBUSY while timer runs */
#define SD_IOC_ABBA	_IOW(SD_MAGIC, 5, __u32)		/* 0:A->B 1:B->A 2:same class 3:nested */
#define SD_IOC_SELFTEST	_IOR(SD_MAGIC, 6, struct sd_selftest)

#endif /* SYNCDRV_UAPI_H */
