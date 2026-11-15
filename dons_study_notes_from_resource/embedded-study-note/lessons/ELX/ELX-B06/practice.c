/* ENV: qemu */
// SPDX-License-Identifier: GPL-2.0
/*
 * ELX-B06 practice: solution + P1 (range-checked param op), P2 (array param),
 * P3 (hello_opt.ko: optional dependency through symbol_get/symbol_put).
 *
 *   -DHELLO_PART_CORE -> hello.ko       count/name module parameters,
 *                                       exports hello_greet() (GPL-only)
 *   -DHELLO_PART_USER -> hello_user.ko  calls hello_greet() from its init,
 *                                       so modpost records depends=hello
 *   -DHELLO_PART_OPT  -> hello_opt.ko   symbol_get(hello_greet): depends= empty
 *
 * The Makefile copies this file to hello.c, hello_user.c, hello_opt.c (one object per
 * module) and sets the define with CFLAGS_<obj>.o. Built against
 * /var/lab/out/linux-build (Linux 6.18.55). Load it only inside a QEMU guest,
 * never into the Lima VM's own kernel.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>

/* --- the "header" both modules share (would be hello.h in a real tree) --- */
#define HELLO_MAX_COUNT 10
int hello_greet(const char *who);

#if defined(HELLO_PART_CORE)

static int count = 1;
static char *name = "world";
static atomic_t greets = ATOMIC_INIT(0);

/* P1: a .set op validates EVERY write: insmod args and sysfs writes alike */
static int count_set(const char *val, const struct kernel_param *kp)
{
	int v;
	int ret = kstrtoint(val, 0, &v);	/* accepts one trailing '\n' */

	if (ret)
		return ret;
	if (v < 1 || v > HELLO_MAX_COUNT)
		return -EINVAL;
	*(int *)kp->arg = v;
	return 0;
}

static const struct kernel_param_ops count_ops = {
	.set = count_set,
	.get = param_get_int,
};

/* A1 */
module_param_cb(count, &count_ops, &count, 0644);
__MODULE_PARM_TYPE(count, "int");
MODULE_PARM_DESC(count, "greetings printed at load, 1..10 (default 1)");
module_param(name, charp, 0444);
MODULE_PARM_DESC(name, "who to greet (default \"world\")");

/* P2: up to 4 ints, "ids=5,6,7"; nr_ids = how many were given */
#define HELLO_MAX_IDS 4
static int ids[HELLO_MAX_IDS];
static unsigned int nr_ids;
module_param_array(ids, int, &nr_ids, 0444);
MODULE_PARM_DESC(ids, "up to 4 board ids, comma separated");

/* Called by other modules. Logs the *current* count (sysfs writes show up). */
int hello_greet(const char *who)
{
	/* A2 */
	int n = atomic_inc_return(&greets);

	pr_info("greet #%d from %s (count=%d)\n", n, who, READ_ONCE(count));
	return n;
}
/* A3 */
EXPORT_SYMBOL_GPL(hello_greet);

static int __init hello_init(void)
{
	int i;

	/* A4 (count_set already refused bad insmod values; keep the guard) */
	if (count < 1 || count > HELLO_MAX_COUNT) {
		pr_err("count=%d out of range 1..%d\n", count, HELLO_MAX_COUNT);
		return -EINVAL;
	}
	for (i = 1; i <= count; i++)
		pr_info("hello %s %d/%d\n", name, i, count);
	if (nr_ids) {
		char buf[64];
		int len = 0;
		unsigned int k;

		for (k = 0; k < nr_ids; k++)
			len += scnprintf(buf + len, sizeof(buf) - len, " %d", ids[k]);
		pr_info("ids n=%u:%s\n", nr_ids, buf);
	}
	return 0;
}

static void __exit hello_exit(void)
{
	/* A5 */
	pr_info("bye (greets=%d)\n", atomic_read(&greets));
}

module_init(hello_init);
module_exit(hello_exit);
MODULE_DESCRIPTION("ELX-B06 hello: module_param + EXPORT_SYMBOL_GPL provider");

#elif defined(HELLO_PART_USER)

static int __init hello_user_init(void)
{
	/* A6 */
	int n = hello_greet(KBUILD_MODNAME);

	pr_info("hello_greet returned %d\n", n);
	return 0;
}

static void __exit hello_user_exit(void)
{
	pr_info("unloaded\n");
}

module_init(hello_user_init);
module_exit(hello_user_exit);
MODULE_DESCRIPTION("ELX-B06 hello_user: consumer of hello_greet()");

#elif defined(HELLO_PART_OPT)

/* P3: no hard link to hello_greet -> modpost writes "depends=" (empty) */
static int (*greet)(const char *who);

static int __init hello_opt_init(void)
{
	greet = symbol_get(hello_greet);	/* takes a ref on hello, or NULL */
	if (!greet) {
		pr_info("hello not loaded, running without it\n");
		return 0;
	}
	pr_info("optional greet returned %d\n", greet(KBUILD_MODNAME));
	return 0;
}

static void __exit hello_opt_exit(void)
{
	if (greet)
		symbol_put(hello_greet);	/* drops the ref on hello */
	pr_info("unloaded\n");
}

module_init(hello_opt_init);
module_exit(hello_opt_exit);
MODULE_DESCRIPTION("ELX-B06 hello_opt: optional user of hello_greet()");

#else
#error "build with -DHELLO_PART_CORE, _USER or _OPT (see Makefile)"
#endif

MODULE_LICENSE("GPL");
