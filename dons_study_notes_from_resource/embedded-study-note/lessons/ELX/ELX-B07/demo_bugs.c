/* ENV: qemu */
// SPDX-License-Identifier: GPL-2.0
/*
 * ELX-B07 demo: kecho with one classic mistake switched on by bug=N.
 * Same /dev/kecho, same class name, so kecho_test and the shell can drive it.
 *   bug=1 spin   spinlock held across copy_to_user()  -> BUG: sleeping function ...
 *   bug=2 nolock no lock at all                       -> torn 4 KiB reads (maybe)
 *   bug=3 direct copy_from_user() straight into data  -> partial fault zeroes the tail
 *   bug=4 write0 return 0 (not -ENOSPC) at the end    -> write loops never finish
 *   bug=5 noppos read ignores *ppos                   -> cat never sees EOF
 *   bug=6 leak   exit forgets class_destroy()         -> next insmod: duplicate sysfs name
 * check.sh demo <name> builds this as demo_bugs.ko and runs one scenario.
 */
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define KECHO_SIZE	4096

struct kecho_dev {
	struct mutex lock;
	spinlock_t slock;		/* bug=1 only */
	size_t len;
	u8 data[KECHO_SIZE];
	u8 bounce[KECHO_SIZE];
	struct cdev cdev;
};

static struct kecho_dev kecho;
static dev_t kecho_devt;
static struct class *kecho_class;

static int bug;
module_param(bug, int, 0444);
MODULE_PARM_DESC(bug, "1 spin, 2 nolock, 3 direct, 4 write0, 5 noppos, 6 leak");

static void lock(struct kecho_dev *d)
{
	if (bug != 2)
		mutex_lock(&d->lock);
}

static void unlock(struct kecho_dev *d)
{
	if (bug != 2)
		mutex_unlock(&d->lock);
}

static int kecho_open(struct inode *inode, struct file *file)
{
	file->private_data = container_of(inode->i_cdev, struct kecho_dev, cdev);
	return 0;
}

static ssize_t kecho_read(struct file *file, char __user *buf, size_t count,
			  loff_t *ppos)
{
	struct kecho_dev *d = file->private_data;
	loff_t pos = bug == 5 ? 0 : *ppos;	/* bug 5: always from the start */
	size_t n, left;
	ssize_t ret;

	if (!count)
		return 0;
	lock(d);
	if (pos >= d->len) {
		ret = 0;
		goto out;
	}
	n = min_t(size_t, count, d->len - pos);
	if (bug == 1) {
		spin_lock(&d->slock);		/* bug 1: may sleep inside */
		left = copy_to_user(buf, d->data + pos, n);
		spin_unlock(&d->slock);
	} else {
		left = copy_to_user(buf, d->data + pos, n);
	}
	if (left == n) {
		ret = -EFAULT;
		goto out;
	}
	n -= left;
	if (bug != 5)
		*ppos = pos + n;
	ret = n;
out:
	unlock(d);
	return ret;
}

static ssize_t kecho_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct kecho_dev *d = file->private_data;
	loff_t pos = *ppos;
	size_t n, left;
	ssize_t ret;

	if (!count)
		return 0;
	lock(d);
	if (pos >= KECHO_SIZE) {
		ret = bug == 4 ? 0 : -ENOSPC;	/* bug 4: "nothing written" */
		goto out;
	}
	n = min_t(size_t, count, KECHO_SIZE - pos);
	if (bug == 3) {				/* bug 3: no bounce buffer */
		left = copy_from_user(d->data + pos, buf, n);
	} else {
		left = copy_from_user(d->bounce, buf, n);
		memcpy(d->data + pos, d->bounce, n - left);
	}
	if (left == n) {
		ret = -EFAULT;
		goto out;
	}
	n -= left;
	d->len = max_t(size_t, d->len, pos + n);
	*ppos = pos + n;
	ret = n;
out:
	unlock(d);
	return ret;
}

static loff_t kecho_llseek(struct file *file, loff_t off, int whence)
{
	struct kecho_dev *d = file->private_data;
	loff_t base, ret = -EINVAL;		/* same rules as solution.c */

	mutex_lock(&d->lock);
	base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? file->f_pos :
	       whence == SEEK_END ? (loff_t)d->len : -1;
	if (base >= 0 && off >= -base && off <= KECHO_SIZE - base)
		ret = file->f_pos = base + off;
	mutex_unlock(&d->lock);
	return ret;
}

static const struct file_operations kecho_fops = {
	.owner	= THIS_MODULE,
	.open	= kecho_open,
	.read	= kecho_read,
	.write	= kecho_write,
	.llseek	= kecho_llseek,
};

static int __init kecho_init(void)
{
	struct device *dev;
	int err;

	mutex_init(&kecho.lock);
	spin_lock_init(&kecho.slock);
	err = alloc_chrdev_region(&kecho_devt, 0, 1, "kecho");
	if (err)
		return err;
	cdev_init(&kecho.cdev, &kecho_fops);
	kecho.cdev.owner = THIS_MODULE;
	err = cdev_add(&kecho.cdev, kecho_devt, 1);
	if (err)
		goto err_region;
	kecho_class = class_create("kecho");
	if (IS_ERR(kecho_class)) {
		err = PTR_ERR(kecho_class);
		goto err_cdev;
	}
	dev = device_create(kecho_class, NULL, kecho_devt, NULL, "kecho");
	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		goto err_class;
	}
	pr_info("kecho(demo bug=%d): registered %d:%d\n", bug,
		MAJOR(kecho_devt), MINOR(kecho_devt));
	return 0;
err_class:
	class_destroy(kecho_class);
err_cdev:
	cdev_del(&kecho.cdev);
err_region:
	unregister_chrdev_region(kecho_devt, 1);
	return err;
}

static void __exit kecho_exit(void)
{
	device_destroy(kecho_class, kecho_devt);
	if (bug != 6)				/* bug 6: class left behind */
		class_destroy(kecho_class);
	cdev_del(&kecho.cdev);
	unregister_chrdev_region(kecho_devt, 1);
}

module_init(kecho_init);
module_exit(kecho_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ELX-B07 demo: kecho with classic char-driver bugs");
