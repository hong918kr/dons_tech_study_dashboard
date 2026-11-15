/* ENV: qemu */
// SPDX-License-Identifier: GPL-2.0
/*
 * ELX-B09 practice: solution + P1 hysteresis, P2 edge (enum), P3 reset (wo),
 * P4 a raw kobject /sys/kernel/thsense_info/abi_version.
 *
 * Base lesson: sysfs attributes on a platform device (thsense).
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
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>

#define THS_NAME	"thsense"
#define THS_MIN		0
#define THS_MAX		1000
#define THS_DEFAULT	500
#define THS_HYST_MAX	100

enum ths_edge { EDGE_RISING, EDGE_FALLING, EDGE_BOTH };
static const char * const ths_edge_names[] = { "rising", "falling", "both" };

struct ths_priv {
	struct mutex lock;	/* protects every field below */
	int threshold;		/* 0..1000 */
	int level;		/* last sample */
	bool above;		/* level >= threshold after the last update */
	u32 count;		/* counted crossings since probe */
	int hyst;		/* P1: re-arm only below threshold - hyst */
	enum ths_edge edge;	/* P2: which crossings count */
};

static struct platform_device *ths_pdev;

/* ---------- Part A/B: parse one decimal value in [0, 1000] ---------- */

/* kstrtoint base 10; any parse error or out-of-range -> -EINVAL */
static int ths_parse_value(const char *buf, int *out)
{
	int v;

	if (kstrtoint(buf, 10, &v))	/* -EINVAL garbage, -ERANGE int overflow */
		return -EINVAL;		/* this ABI: every bad input is -EINVAL */
	if (v < THS_MIN || v > THS_MAX)
		return -EINVAL;
	*out = v;
	return 0;
}

/* ---------- Part C: crossing detection (call with p->lock held) ---------- */

/* P1+P2: Schmitt-trigger style state + edge filter; true if count changed */
static bool ths_update_locked(struct ths_priv *p)
{
	bool rising = false, falling = false, counted;

	if (!p->above && p->level >= p->threshold) {
		p->above = true;
		rising = true;
	} else if (p->above && p->level < p->threshold - p->hyst) {
		p->above = false;
		falling = true;
	}
	counted = (rising && p->edge != EDGE_FALLING) ||
		  (falling && p->edge != EDGE_RISING);
	if (counted)
		p->count++;
	return counted;
}

/* wake poll(POLLPRI) waiters on "count" -- outside the mutex */
static void ths_notify(struct device *dev, bool rising)
{
	if (rising)
		sysfs_notify(&dev->kobj, NULL, "count");
}

/* ---------- show / store ---------- */

static ssize_t threshold_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct ths_priv *p = dev_get_drvdata(dev);
	int v;

	mutex_lock(&p->lock);
	v = p->threshold;
	mutex_unlock(&p->lock);
	return sysfs_emit(buf, "%d\n", v);
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

static ssize_t count_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct ths_priv *p = dev_get_drvdata(dev);
	u32 v;

	mutex_lock(&p->lock);
	v = p->count;
	mutex_unlock(&p->lock);
	return sysfs_emit(buf, "%u\n", v);
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

/* ---------- P1: hysteresis (rw, 0..100) ---------- */
static ssize_t hysteresis_show(struct device *dev, struct device_attribute *attr,
			       char *buf)
{
	struct ths_priv *p = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", READ_ONCE(p->hyst));
}

static ssize_t hysteresis_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct ths_priv *p = dev_get_drvdata(dev);
	bool changed;
	int v;

	if (kstrtoint(buf, 10, &v) || v < 0 || v > THS_HYST_MAX)
		return -EINVAL;
	mutex_lock(&p->lock);
	p->hyst = v;
	changed = ths_update_locked(p);
	mutex_unlock(&p->lock);
	ths_notify(dev, changed);
	return count;
}
static DEVICE_ATTR_RW(hysteresis);

/* ---------- P2: edge (rw enum, "[selected]" style) ---------- */
static ssize_t edge_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct ths_priv *p = dev_get_drvdata(dev);
	enum ths_edge cur = READ_ONCE(p->edge);
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(ths_edge_names); i++)
		len += sysfs_emit_at(buf, len, i == cur ? "%s[%s]" : "%s%s",
				     i ? " " : "", ths_edge_names[i]);
	len += sysfs_emit_at(buf, len, "\n");
	return len;
}

static ssize_t edge_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct ths_priv *p = dev_get_drvdata(dev);
	int i = sysfs_match_string(ths_edge_names, buf);	/* eats one '\n' */

	if (i < 0)
		return i;
	mutex_lock(&p->lock);
	p->edge = i;
	mutex_unlock(&p->lock);
	return count;
}
static DEVICE_ATTR_RW(edge);

/* ---------- P3: reset (wo, only "1") ---------- */
static ssize_t reset_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct ths_priv *p = dev_get_drvdata(dev);

	if (!sysfs_streq(buf, "1"))	/* not kstrtobool: it accepts "yes please" */
		return -EINVAL;
	mutex_lock(&p->lock);
	p->count = 0;
	mutex_unlock(&p->lock);
	sysfs_notify(&dev->kobj, NULL, "count");
	return count;
}
static DEVICE_ATTR_WO(reset);

static struct attribute *ths_attrs[] = {
	&dev_attr_threshold.attr,
	&dev_attr_count.attr,
	&dev_attr_sample.attr,
	&dev_attr_hysteresis.attr,
	&dev_attr_edge.attr,
	&dev_attr_reset.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ths);		/* -> ths_group + ths_groups[] */

/* ---------- P4: a bare kobject, no struct device ---------- */
static ssize_t abi_version_show(struct kobject *kobj, struct kobj_attribute *attr,
				char *buf)
{
	return sysfs_emit(buf, "1\n");
}
static struct kobj_attribute abi_version_attr = __ATTR_RO(abi_version);

static struct attribute *info_attrs[] = {
	&abi_version_attr.attr,
	NULL,
};
static const struct attribute_group info_group = { .attrs = info_attrs };
static struct kobject *info_kobj;

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

	info_kobj = kobject_create_and_add("thsense_info", kernel_kobj);
	if (!info_kobj)
		return -ENOMEM;
	ret = sysfs_create_group(info_kobj, &info_group);
	if (ret)
		goto put_kobj;
	ret = platform_driver_register(&ths_driver);
	if (ret)
		goto put_kobj;
	ths_pdev = platform_device_register_simple(THS_NAME, PLATFORM_DEVID_NONE,
						   NULL, 0);
	if (IS_ERR(ths_pdev)) {
		ret = PTR_ERR(ths_pdev);
		platform_driver_unregister(&ths_driver);
		goto put_kobj;
	}
	return 0;

put_kobj:
	kobject_put(info_kobj);		/* also removes its sysfs files */
	return ret;
}

static void __exit ths_exit(void)
{
	platform_device_unregister(ths_pdev);
	platform_driver_unregister(&ths_driver);
	kobject_put(info_kobj);
}

module_init(ths_init);
module_exit(ths_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ELX-B09 practice: hysteresis, edge enum, reset, raw kobject");
