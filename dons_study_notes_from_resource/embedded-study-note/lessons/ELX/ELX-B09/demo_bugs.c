/* ENV: qemu */
// SPDX-License-Identifier: GPL-2.0
/*
 * ELX-B09 demo: three classic sysfs driver bugs, on purpose.
 *   bug=1  threshold_store returns 0 instead of count -> userspace write loops
 *   bug=2  sysfs_notify() called with a spinlock held  -> sleeping in atomic
 *   -DB09_DEMO_PERM  a 0666 attribute                   -> build error
 * check.sh demo <ret0|atomic|perm>. Guest only (Image-dbg).
 */
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>

static int bug;
module_param(bug, int, 0444);

static DEFINE_SPINLOCK(demo_lock);
static int threshold = 500, level;
static bool above;
static unsigned int count;
static atomic_t stores = ATOMIC_INIT(0);
static struct platform_device *demo_pdev;

static ssize_t threshold_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", READ_ONCE(threshold));
}

static ssize_t threshold_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t n)
{
	int v;

	atomic_inc(&stores);
	if (kstrtoint(buf, 10, &v) || v < 0 || v > 1000)
		return -EINVAL;
	WRITE_ONCE(threshold, v);
	return bug == 1 ? 0 : n;	/* BUG 1: "0 bytes consumed" */
}
static DEVICE_ATTR_RW(threshold);

static ssize_t count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", READ_ONCE(count));
}
static DEVICE_ATTR_RO(count);

static ssize_t stores_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", atomic_read(&stores));
}
static DEVICE_ATTR_RO(stores);

static ssize_t sample_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t n)
{
	bool rising;
	int v;

	if (kstrtoint(buf, 10, &v) || v < 0 || v > 1000)
		return -EINVAL;
	spin_lock(&demo_lock);
	level = v;
	rising = level >= threshold && !above;
	above = level >= threshold;
	if (rising)
		count++;
	if (rising && bug == 2)
		sysfs_notify(&dev->kobj, NULL, "count");	/* BUG 2: under spinlock */
	spin_unlock(&demo_lock);
	if (rising && bug != 2)
		sysfs_notify(&dev->kobj, NULL, "count");
	return n;
}
static DEVICE_ATTR_WO(sample);

#ifdef B09_DEMO_PERM
static DEVICE_ATTR(world, 0666, threshold_show, threshold_store);	/* BUG 3 */
#endif

static struct attribute *demo_attrs[] = {
	&dev_attr_threshold.attr,
	&dev_attr_count.attr,
	&dev_attr_stores.attr,
	&dev_attr_sample.attr,
#ifdef B09_DEMO_PERM
	&dev_attr_world.attr,
#endif
	NULL,
};
ATTRIBUTE_GROUPS(demo);

static int demo_probe(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver demo_driver = {
	.probe = demo_probe,
	.driver = { .name = "thsense", .dev_groups = demo_groups },
};

static int __init demo_init(void)
{
	int ret = platform_driver_register(&demo_driver);

	if (ret)
		return ret;
	demo_pdev = platform_device_register_simple("thsense", PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(demo_pdev)) {
		platform_driver_unregister(&demo_driver);
		return PTR_ERR(demo_pdev);
	}
	return 0;
}

static void __exit demo_exit(void)
{
	platform_device_unregister(demo_pdev);
	platform_driver_unregister(&demo_driver);
}

module_init(demo_init);
module_exit(demo_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ELX-B09 demo: broken sysfs attributes");
