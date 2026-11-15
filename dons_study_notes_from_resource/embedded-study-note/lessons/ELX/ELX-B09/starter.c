/* ENV: qemu */
// SPDX-License-Identifier: GPL-2.0
/*
 * ELX-B09: sysfs attributes on a platform device (thsense).
 *
 *   /sys/bus/platform/devices/thsense/
 *       threshold  rw  0..1000, decimal only, -EINVAL on anything else
 *       count      ro  number of rising crossings (level goes >= threshold)
 *       sample     wo  fake sensor input 0..1000 (stands in for the ADC/IRQ path)
 *
 * The module registers BOTH the platform_driver and its platform_device, so
 * the normal driver-core path runs: device_add -> probe -> dev_groups created
 * -> KOBJ_BIND. Attributes come from .driver.dev_groups (never from probe).
 * A rising crossing bumps count and calls sysfs_notify(.., "count") so a
 * poll(POLLPRI) waiter on the count file wakes up.
 *
 * Runs only in a QEMU guest (Image-dbg, 6.18.55-dbg). Never insmod this into
 * the Lima VM's running kernel.
 */
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>

#define THS_NAME	"thsense"
#define THS_MIN		0
#define THS_MAX		1000
#define THS_DEFAULT	500

struct ths_priv {
	struct mutex lock;	/* protects every field below */
	int threshold;		/* 0..1000 */
	int level;		/* last sample */
	bool above;		/* level >= threshold after the last update */
	u32 count;		/* rising crossings since probe */
};

static struct platform_device *ths_pdev;

/* ---------- Part A/B: parse one decimal value in [0, 1000] ---------- */

/* TODO(A1): kstrtoint base 10; any parse error or out-of-range -> -EINVAL */
static int ths_parse_value(const char *buf, int *out)
{
	(void)buf;
	(void)out;
	return -ENOSYS;		/* TODO */
}

/* ---------- Part C: crossing detection (call with p->lock held) ---------- */

/* TODO(C1): recompute above; on a rising edge (false -> true) count++ and return true */
static bool ths_update_locked(struct ths_priv *p)
{
	(void)p;
	return false;		/* TODO */
}

/* TODO(C2): wake poll(POLLPRI) waiters on "count" -- outside the mutex */
static void ths_notify(struct device *dev, bool rising)
{
	(void)dev;
	(void)rising;		/* TODO */
}

/* ---------- show / store ---------- */

/* TODO(A2): print threshold as decimal + '\n' with sysfs_emit */
static ssize_t threshold_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	(void)dev;
	(void)buf;
	return 0;		/* TODO */
}

static ssize_t threshold_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct ths_priv *p = dev_get_drvdata(dev);
	bool rising;
	int v, ret;

	ret = ths_parse_value(buf, &v);
	if (ret)
		return ret;
	mutex_lock(&p->lock);
	p->threshold = v;
	rising = ths_update_locked(p);
	mutex_unlock(&p->lock);
	ths_notify(dev, rising);
	return count;		/* consumed everything */
}
static DEVICE_ATTR_RW(threshold);

/* TODO(A2): print count as unsigned decimal + '\n' with sysfs_emit */
static ssize_t count_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	(void)dev;
	(void)buf;
	return 0;		/* TODO */
}
static DEVICE_ATTR_RO(count);

static ssize_t sample_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct ths_priv *p = dev_get_drvdata(dev);
	bool rising;
	int v, ret;

	ret = ths_parse_value(buf, &v);
	if (ret)
		return ret;
	mutex_lock(&p->lock);
	p->level = v;
	rising = ths_update_locked(p);
	mutex_unlock(&p->lock);
	ths_notify(dev, rising);
	return count;
}
static DEVICE_ATTR_WO(sample);

static struct attribute *ths_attrs[] = {
	&dev_attr_threshold.attr,
	&dev_attr_count.attr,
	&dev_attr_sample.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ths);		/* -> ths_group + ths_groups[] */

/* ---------- driver / device ---------- */

static int ths_probe(struct platform_device *pdev)
{
	struct ths_priv *p;

	p = devm_kzalloc(&pdev->dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	mutex_init(&p->lock);
	p->threshold = THS_DEFAULT;
	platform_set_drvdata(pdev, p);
	dev_info(&pdev->dev, "probed, threshold=%d\n", p->threshold);
	return 0;		/* driver core adds ths_groups after this */
}

static struct platform_driver ths_driver = {
	.probe = ths_probe,
	.driver = {
		.name = THS_NAME,
		.dev_groups = ths_groups,
	},
};

static int __init ths_init(void)
{
	int ret;

	ret = platform_driver_register(&ths_driver);
	if (ret)
		return ret;
	ths_pdev = platform_device_register_simple(THS_NAME, PLATFORM_DEVID_NONE,
						   NULL, 0);
	if (IS_ERR(ths_pdev)) {
		platform_driver_unregister(&ths_driver);
		return PTR_ERR(ths_pdev);
	}
	return 0;
}

static void __exit ths_exit(void)
{
	platform_device_unregister(ths_pdev);
	platform_driver_unregister(&ths_driver);
}

module_init(ths_init);
module_exit(ths_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ELX-B09 sysfs threshold/count attributes on a platform device");
