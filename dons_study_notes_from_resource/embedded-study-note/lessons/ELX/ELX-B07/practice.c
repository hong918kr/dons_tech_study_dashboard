/* ENV: qemu */
// SPDX-License-Identifier: GPL-2.0
/*
 * ELX-B07 practice: kecho with all four extra exercises in one module.
 *
 *   P1  two minors: /dev/kecho (minor 0) and /dev/kecho1 (minor 1), each with
 *       its own buffer; open() finds the right one with container_of().
 *   P2  O_TRUNC on a write-open empties the buffer; O_APPEND writes at len.
 *   P3  llseek via generic_file_llseek_size(): SEEK_DATA / SEEK_HOLE for free.
 *   P4  the same fops also behind a misc device /dev/kecho_misc (major 10),
 *       where misc_open() has already put the miscdevice in private_data.
 *
 * read/write rules are the solution's (short read/write, -ENOSPC, -EFAULT,
 * bounce buffer, per-device mutex). fail_at=N (1..5) tests every unwind step,
 * including the half-done loops (step 2 and 4 fail on the 2nd minor).
 * Runs only in a QEMU guest (Image-dbg, 6.18.55-dbg).
 */
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/minmax.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define KECHO_NAME	"kecho"
#define KECHO_SIZE	4096
#define KECHO_MINORS	2		/* cdev minors; the misc device is extra */

struct kecho_dev {
	struct mutex lock;		/* protects data[], bounce[], len */
	size_t len;
	u8 data[KECHO_SIZE];
	u8 bounce[KECHO_SIZE];
	struct cdev cdev;		/* minors 0, 1 */
	struct miscdevice misc;		/* kdevs[KECHO_MINORS] only */
};

static struct kecho_dev kdevs[KECHO_MINORS + 1];
static dev_t kecho_devt;
static struct class *kecho_class;

static int fail_at;
module_param(fail_at, int, 0444);
MODULE_PARM_DESC(fail_at, "test only: make init step N (1..5) fail");

static int kecho_inject(int step)
{
	if (fail_at != step)
		return 0;
	pr_info("kecho: injected failure at step %d\n", step);
	return -ENOMEM;
}

static int kecho_open(struct inode *inode, struct file *file)
{
	struct kecho_dev *d;

	if (imajor(inode) == MISC_MAJOR)	/* P4: misc_open() set private_data */
		d = container_of(file->private_data, struct kecho_dev, misc);
	else					/* P1: the minor's own cdev */
		d = container_of(inode->i_cdev, struct kecho_dev, cdev);
	file->private_data = d;

	if ((file->f_mode & FMODE_WRITE) && (file->f_flags & O_TRUNC)) {	/* P2 */
		if (mutex_lock_interruptible(&d->lock))
			return -ERESTARTSYS;
		memset(d->data, 0, d->len);	/* holes written later must read 0 */
		d->len = 0;
		mutex_unlock(&d->lock);
	}
	return 0;
}

static int kecho_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t kecho_read(struct file *file, char __user *buf, size_t count,
			  loff_t *ppos)
{
	struct kecho_dev *d = file->private_data;
	loff_t pos = *ppos;
	size_t n, left;
	ssize_t ret;

	if (!count)
		return 0;
	if (mutex_lock_interruptible(&d->lock))
		return -ERESTARTSYS;
	if (pos >= d->len) {
		ret = 0;
		goto out;
	}
	n = min_t(size_t, count, d->len - pos);
	left = copy_to_user(buf, d->data + pos, n);
	if (left == n) {
		ret = -EFAULT;
		goto out;
	}
	n -= left;
	*ppos = pos + n;
	ret = n;
out:
	mutex_unlock(&d->lock);
	return ret;
}

static ssize_t kecho_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct kecho_dev *d = file->private_data;
	loff_t pos;
	size_t n, left;
	ssize_t ret;

	if (!count)
		return 0;
	if (mutex_lock_interruptible(&d->lock))
		return -ERESTARTSYS;
	pos = (file->f_flags & O_APPEND) ? d->len : *ppos;	/* P2: under the lock */
	if (pos >= KECHO_SIZE) {
		ret = -ENOSPC;
		goto out;
	}
	n = min_t(size_t, count, KECHO_SIZE - pos);
	left = copy_from_user(d->bounce, buf, n);
	if (left == n) {
		ret = -EFAULT;
		goto out;
	}
	n -= left;
	memcpy(d->data + pos, d->bounce, n);
	d->len = max_t(size_t, d->len, pos + n);
	*ppos = pos + n;
	ret = n;
out:
	mutex_unlock(&d->lock);
	return ret;
}

static loff_t kecho_llseek(struct file *file, loff_t off, int whence)
{
	struct kecho_dev *d = file->private_data;
	loff_t ret;

	mutex_lock(&d->lock);		/* P3: len must not move under us */
	ret = generic_file_llseek_size(file, off, whence, KECHO_SIZE, d->len);
	mutex_unlock(&d->lock);
	return ret;
}

static const struct file_operations kecho_fops = {
	.owner		= THIS_MODULE,
	.open		= kecho_open,
	.release	= kecho_release,
	.read		= kecho_read,
	.write		= kecho_write,
	.llseek		= kecho_llseek,
};

static int __init kecho_init(void)
{
	struct kecho_dev *m = &kdevs[KECHO_MINORS];
	struct device *dev;
	int err, i, j;

	for (i = 0; i <= KECHO_MINORS; i++)
		mutex_init(&kdevs[i].lock);

	err = kecho_inject(1) ?: alloc_chrdev_region(&kecho_devt, 0, KECHO_MINORS,
						     KECHO_NAME);
	if (err)
		return err;

	for (i = 0; i < KECHO_MINORS; i++) {
		cdev_init(&kdevs[i].cdev, &kecho_fops);
		kdevs[i].cdev.owner = THIS_MODULE;
		err = (i == 1 ? kecho_inject(2) : 0) ?:
			cdev_add(&kdevs[i].cdev, MKDEV(MAJOR(kecho_devt), i), 1);
		if (err)
			goto err_cdev;			/* cdevs 0..i-1 are live */
	}

	kecho_class = kecho_inject(3) ? ERR_PTR(-ENOMEM) : class_create(KECHO_NAME);
	if (IS_ERR(kecho_class)) {
		err = PTR_ERR(kecho_class);
		goto err_cdev;
	}

	for (j = 0; j < KECHO_MINORS; j++) {
		dev = (j == 1 && kecho_inject(4)) ? ERR_PTR(-ENOMEM) :
			device_create(kecho_class, NULL, MKDEV(MAJOR(kecho_devt), j),
				      NULL, j ? "kecho%d" : "kecho", j);
		if (IS_ERR(dev)) {
			err = PTR_ERR(dev);
			goto err_device;		/* devices 0..j-1 exist */
		}
	}

	m->misc.minor = MISC_DYNAMIC_MINOR;
	m->misc.name = "kecho_misc";
	m->misc.fops = &kecho_fops;
	err = kecho_inject(5) ?: misc_register(&m->misc);
	if (err)
		goto err_device;

	pr_info("kecho: registered %d:0..%d + misc 10:%d\n",
		MAJOR(kecho_devt), KECHO_MINORS - 1, m->misc.minor);
	return 0;

err_device:
	while (j--)
		device_destroy(kecho_class, MKDEV(MAJOR(kecho_devt), j));
	class_destroy(kecho_class);
err_cdev:
	while (i--)
		cdev_del(&kdevs[i].cdev);
	unregister_chrdev_region(kecho_devt, KECHO_MINORS);
	return err;
}

static void __exit kecho_exit(void)
{
	int i;

	misc_deregister(&kdevs[KECHO_MINORS].misc);
	for (i = KECHO_MINORS - 1; i >= 0; i--)
		device_destroy(kecho_class, MKDEV(MAJOR(kecho_devt), i));
	class_destroy(kecho_class);
	for (i = KECHO_MINORS - 1; i >= 0; i--)
		cdev_del(&kdevs[i].cdev);
	unregister_chrdev_region(kecho_devt, KECHO_MINORS);
	pr_info("kecho: unregistered\n");
}

module_init(kecho_init);
module_exit(kecho_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ELX-B07 practice: kecho with 2 minors, O_TRUNC/O_APPEND, SEEK_DATA/HOLE, misc");
