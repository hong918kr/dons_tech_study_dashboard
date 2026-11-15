/* ENV: qemu */
// SPDX-License-Identifier: GPL-2.0
/*
 * ELX-B06: two kernel modules from one source file.
 *
 *   -DHELLO_PART_CORE -> hello.ko       count/name module parameters,
 *                                       exports hello_greet() (GPL-only)
 *   -DHELLO_PART_USER -> hello_user.ko  calls hello_greet() from its init,
 *                                       so modpost records depends=hello
 *
 * The Makefile copies this file to hello.c and hello_user.c (one object per
 * module) and sets the define with CFLAGS_<obj>.o. Built against
 * /var/lab/out/linux-build (Linux 6.18.55). Load it only inside a QEMU guest,
 * never into the Lima VM's own kernel.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/init.h>
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

/* TODO A1: count -> int, rw for root (0644); name -> charp, read-only (0444) */

/* Called by other modules. Logs the *current* count (sysfs writes show up). */
int hello_greet(const char *who)
{
	/* TODO A2: bump greets atomically, log, return the new total */
	(void)who;
	(void)greets;
	return 0;
}
/* TODO A3: export hello_greet to GPL-compatible modules only */

static int __init hello_init(void)
{
	/* TODO A4: reject count outside 1..HELLO_MAX_COUNT, else print the lines */
	(void)count;
	(void)name;
	return 0;
}

static void __exit hello_exit(void)
{
	/* TODO A5: say goodbye with the number of greets served */
}

module_init(hello_init);
module_exit(hello_exit);
MODULE_DESCRIPTION("ELX-B06 hello: module_param + EXPORT_SYMBOL_GPL provider");

#elif defined(HELLO_PART_USER)

static int __init hello_user_init(void)
{
	/* TODO A6: call hello_greet(KBUILD_MODNAME) and log what it returned */
	return 0;
}

static void __exit hello_user_exit(void)
{
	pr_info("unloaded\n");
}

module_init(hello_user_init);
module_exit(hello_user_exit);
MODULE_DESCRIPTION("ELX-B06 hello_user: consumer of hello_greet()");

#else
#error "build with -DHELLO_PART_CORE or -DHELLO_PART_USER (see Makefile)"
#endif

MODULE_LICENSE("GPL");
