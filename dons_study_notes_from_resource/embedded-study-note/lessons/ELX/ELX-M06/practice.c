/* ENV: qemu */
// SPDX-License-Identifier: GPL-2.0
/*
 * ELX-M06 practice — deterministic semantics of the deferral APIs, run from
 * module_init (process context). No IRQ line needed. Every wait is bounded.
 * Linux 6.18.55-dbg. Results: dmesg lines "CHECK <name> PASS|FAIL" and
 * "PRACTICE RESULT pass=N fail=M". The module always loads; guest.sh decides.
 */
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/workqueue.h>

static int g_pass, g_fail;

#define CHECK(name, cond) do {						\
	bool ok_ = (cond);						\
	pr_info("CHECK %s %s\n", (name), ok_ ? "PASS" : "FAIL");	\
	if (ok_)							\
		g_pass++;						\
	else								\
		g_fail++;						\
} while (0)

/* bounded poll for a counter reaching `want`: 2 s max */
static bool wait_count(atomic_t *c, int want)
{
	int i;

	for (i = 0; i < 400; i++) {
		if (atomic_read(c) >= want)
			return true;
		msleep(5);
	}
	return atomic_read(c) >= want;
}

/* --- P1: tasklet coalescing while disabled, and self-reschedule ---------- */
static atomic_t p1_runs;

static void p1_fn(struct tasklet_struct *t)
{
	atomic_inc(&p1_runs);
}

static atomic_t p1b_runs;

static void p1b_fn(struct tasklet_struct *t)
{
	if (atomic_inc_return(&p1b_runs) < 3)
		tasklet_schedule(t);	/* re-schedule from inside: runs again */
}

static DECLARE_TASKLET(p1_tl, p1_fn);
static DECLARE_TASKLET(p1b_tl, p1b_fn);

static void test_tasklet(void)
{
	int i;

	tasklet_disable(&p1_tl);
	for (i = 0; i < 5; i++)
		tasklet_schedule(&p1_tl);
	CHECK("p1_disabled_no_run", atomic_read(&p1_runs) == 0);
	tasklet_enable(&p1_tl);
	/* it stayed queued while disabled; the 5 schedules collapsed into 1 run */
	CHECK("p1_ran", wait_count(&p1_runs, 1));
	msleep(20);
	CHECK("p1_coalesced_once", atomic_read(&p1_runs) == 1);

	tasklet_schedule(&p1b_tl);
	CHECK("p1b_three_runs", wait_count(&p1b_runs, 3));
	msleep(20);
	CHECK("p1b_exactly_three", atomic_read(&p1b_runs) == 3);
	tasklet_kill(&p1_tl);
	tasklet_kill(&p1b_tl);
}

/* --- P2: queue_work() return value: pending vs running -------------------- */
static DECLARE_COMPLETION(p2_started);
static DECLARE_COMPLETION(p2_release);
static atomic_t p2_runs;

static void p2_fn(struct work_struct *w)
{
	atomic_inc(&p2_runs);
	complete(&p2_started);
	/* bounded: a broken test must not wedge the worker */
	wait_for_completion_timeout(&p2_release, 3 * HZ);
}

static DECLARE_WORK(p2_work, p2_fn);

static void test_queue_work_semantics(void)
{
	bool r1, r2;
	struct workqueue_struct *wq = alloc_ordered_workqueue("p2_ord", 0);

	if (!wq) {
		CHECK("p2_alloc", false);
		return;
	}
	r1 = queue_work(wq, &p2_work);	/* pending -> true */
	CHECK("p2_first_queue_true", r1);
	CHECK("p2_started", wait_for_completion_timeout(&p2_started, 2 * HZ) > 0);
	/* now RUNNING, no longer pending: queueing again is accepted */
	r2 = queue_work(wq, &p2_work);
	CHECK("p2_queue_while_running_true", r2);
	/* it is now pending again: a further queue is coalesced */
	CHECK("p2_queue_again_false", !queue_work(wq, &p2_work));
	complete(&p2_release);	/* first run returns */
	complete(&p2_release);	/* second run (the re-queue) returns */
	flush_workqueue(wq);
	CHECK("p2_exactly_two_runs", atomic_read(&p2_runs) == 2);
	destroy_workqueue(wq);
}

/* --- P3: BH workqueue is softirq context, disable_work_sync() replaces
 *         tasklet_disable() --------------------------------------------- */
static atomic_t p3_runs;
static atomic_t p3_softirq;

static void p3_fn(struct work_struct *w)
{
	if (in_serving_softirq())
		atomic_set(&p3_softirq, 1);
	atomic_inc(&p3_runs);
}

static DECLARE_WORK(p3_work, p3_fn);

static void test_bh_workqueue(void)
{
	/* disable first: queue_work() refuses a disabled item */
	CHECK("p3_disable_was_idle", !disable_work_sync(&p3_work));
	CHECK("p3_disabled_queue_false", !queue_work(system_bh_wq, &p3_work));
	msleep(20);
	CHECK("p3_no_run_while_disabled", atomic_read(&p3_runs) == 0);
	CHECK("p3_enable_and_queue", enable_and_queue_work(system_bh_wq, &p3_work));
	CHECK("p3_ran_once", wait_count(&p3_runs, 1));
	msleep(20);
	CHECK("p3_exactly_one_run", atomic_read(&p3_runs) == 1);
	CHECK("p3_softirq_context", atomic_read(&p3_softirq) == 1);
	cancel_work_sync(&p3_work);
}

/* --- P4: cancel_work_sync() waits for a running item ----------------------- */
static DECLARE_COMPLETION(p4_started);
static atomic_t p4_finished;

static void p4_fn(struct work_struct *w)
{
	complete(&p4_started);
	msleep(100);
	atomic_set(&p4_finished, 1);
}

static DECLARE_WORK(p4_work, p4_fn);

static void test_cancel_sync(void)
{
	bool was_pending;

	queue_work(system_percpu_wq, &p4_work);
	CHECK("p4_started", wait_for_completion_timeout(&p4_started, 2 * HZ) > 0);
	/* the item is running and sleeping: cancel must wait for it */
	was_pending = cancel_work_sync(&p4_work);
	CHECK("p4_was_not_pending", !was_pending);
	CHECK("p4_finished_before_cancel_returned", atomic_read(&p4_finished) == 1);
}

static int __init practice_init(void)
{
	test_tasklet();
	test_queue_work_semantics();
	test_bh_workqueue();
	test_cancel_sync();
	pr_info("PRACTICE RESULT pass=%d fail=%d\n", g_pass, g_fail);
	return 0;
}

static void __exit practice_exit(void)
{
}

module_init(practice_init);
module_exit(practice_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ELX-M06 practice: deferral API semantics");
