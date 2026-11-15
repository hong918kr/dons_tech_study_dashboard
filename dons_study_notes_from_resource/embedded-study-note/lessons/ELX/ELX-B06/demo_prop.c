/* ENV: qemu */
// SPDX-License-Identifier: GPL-2.0
/*
 * ELX-B06 demo: a "vendor blob" that is NOT GPL-compatible and calls the
 * GPL-only hello_greet(). Same code as hello_user, different license string.
 *
 * modpost refuses it when it can see hello's exports (same build dir or
 * KBUILD_EXTRA_SYMBOLS): "GPL-incompatible module ... uses GPL-only symbol".
 * check.sh builds it ALONE with KBUILD_MODPOST_WARN=1, so modpost only warns
 * "hello_greet undefined!" and the kernel's own check at insmod time is what
 * we observe in the guest. (The source file is GPL; the module claims
 * "Proprietary" on purpose to trigger the check.)
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

int hello_greet(const char *who);

static int __init demo_prop_init(void)
{
	pr_info("hello_greet returned %d\n", hello_greet(KBUILD_MODNAME));
	return 0;
}

static void __exit demo_prop_exit(void)
{
}

module_init(demo_prop_init);
module_exit(demo_prop_exit);
MODULE_DESCRIPTION("ELX-B06 demo: proprietary-licensed caller of a GPL-only symbol");
MODULE_LICENSE("Proprietary");
