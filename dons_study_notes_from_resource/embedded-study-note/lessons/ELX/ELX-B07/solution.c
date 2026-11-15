/* ENV: qemu */
// SPDX-License-Identifier: GPL-2.0
/*
 * ELX-B07: /dev/kecho - a 4 KiB echo buffer behind a char device.
 *
 * write() stores bytes at *ppos, read() returns them from *ppos, like a tiny
 * file that can never grow past KECHO_SIZE. One device, one buffer, shared by
 * every opener; each open has its own file position (struct file->f_pos).
 *
 * Rules this driver promises (kecho_test.c checks every one in the guest):
 *   read   at pos >= len           -> 0 (EOF)
 *   read   count > len - pos       -> short read (what is there)
 *   write  pos + count > 4096      -> short write (what fits)
 *   write  pos >= 4096             -> -ENOSPC (nothing fits; never return 0)
 *   copy_*_user copies nothing     -> -EFAULT, *ppos and the buffer untouched
 *   copy_*_user copies part        -> return the part, *ppos moves by the part
 *   llseek SET/CUR/END(len)        -> 0..4096, else -EINVAL
 *   concurrent openers             -> one per-device mutex, no torn data
 *
 * fail_at=N (1..4) makes init step N fail, to test the unwind path.
 * Runs only in a QEMU guest (Image-dbg, 6.18.55-dbg). Never insmod this into
 * the Lima VM's running kernel.
 */
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define KECHO_NAME	"kecho"
#define KECHO_SIZE	4096

struct kecho_dev {
	struct mutex lock;		/* protects data[], bounce[], len */
	size_t len;			/* valid bytes = highest offset ever written */
	u8 data[KECHO_SIZE];
	u8 bounce[KECHO_SIZE];		/* copy_from_user lands here first */
	struct cdev cdev;
};

static struct kecho_dev kecho;
static dev_t kecho_devt;
static struct class *kecho_class;
static struct device *kecho_device;

static int fail_at;
module_param(fail_at, int, 0444);
MODULE_PARM_DESC(fail_at, "test only: make init step N (1..4) fail");

static int kecho_inject(int step)
{
	if (fail_at != step)
		return 0;
	pr_info("kecho: injected failure at step %d\n", step);
	return -ENOMEM;
}

static int kecho_open(struct inode *inode, struct file *file)
{
	/* the inode knows its cdev; the cdev is inside our device struct */
	file->private_data = container_of(inode->i_cdev, struct kecho_dev, cdev);
	return 0;
}

static int kecho_release(struct inode *inode, struct file *file)
{
	return 0;
}

/* ---- Part A: read from *ppos ------------------------------------------- */
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
		ret = 0;			/* EOF */
		goto out;
	}
	n = min_t(size_t, count, d->len - pos);
	left = copy_to_user(buf, d->data + pos, n);
	if (left == n) {
		ret = -EFAULT;			/* nothing reached the user */
		goto out;
	}
	n -= left;				/* partial copy: report only that */
	*ppos = pos + n;
	ret = n;
out:
	mutex_unlock(&d->lock);
	return ret;
}

/* ---- Part A/B: write at *ppos, short write, -ENOSPC ---------------------- */
static ssize_t kecho_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct kecho_dev *d = file->private_data;
	loff_t pos = *ppos;
	size_t n, left;
	ssize_t ret;

	if (!count)
		return 0;
	if (mutex_lock_interruptible(&d->lock))
		return -ERESTARTSYS;
	if (pos >= KECHO_SIZE) {
		ret = -ENOSPC;			/* not 0: 0 makes write loops spin */
		goto out;
	}
	n = min_t(size_t, count, KECHO_SIZE - pos);
	left = copy_from_user(d->bounce, buf, n);	/* zero-fills its own tail */
	if (left == n) {
		ret = -EFAULT;
		goto out;
	}
	n -= left;
	memcpy(d->data + pos, d->bounce, n);	/* only the bytes that arrived */
	d->len = max_t(size_t, d->len, pos + n);
	*ppos = pos + n;
	ret = n;
out:
	mutex_unlock(&d->lock);
	return ret;
}

/* ---- Part B: llseek ------------------------------------------------------ */
static loff_t kecho_llseek(struct file *file, loff_t off, int whence)
{
	struct kecho_dev *d = file->private_data;
	loff_t base, ret;

	mutex_lock(&d->lock);
	switch (whence) {
	case SEEK_SET:
		base = 0;
		break;
	case SEEK_CUR:
		base = file->f_pos;
		break;
	case SEEK_END:
		base = d->len;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}
	if (off < -base || off > KECHO_SIZE - base) {	/* no overflow: base in 0..4096 */
		ret = -EINVAL;
		goto out;
	}
	file->f_pos = base + off;
	ret = file->f_pos;
out:
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

/* ---- Part C: register, unwind in reverse --------------------------------- */
static int __init kecho_init(void)
{
	int err;

	mutex_init(&kecho.lock);

	err = kecho_inject(1) ?: alloc_chrdev_region(&kecho_devt, 0, 1, KECHO_NAME);
	if (err)
		return err;

	cdev_init(&kecho.cdev, &kecho_fops);
	kecho.cdev.owner = THIS_MODULE;
	err = kecho_inject(2) ?: cdev_add(&kecho.cdev, kecho_devt, 1);
	if (err)
		goto err_region;

	kecho_class = kecho_inject(3) ? ERR_PTR(-ENOMEM) : class_create(KECHO_NAME);
	if (IS_ERR(kecho_class)) {
		err = PTR_ERR(kecho_class);
		goto err_cdev;
	}

	kecho_device = kecho_inject(4) ? ERR_PTR(-ENOMEM) :
		device_create(kecho_class, NULL, kecho_devt, NULL, KECHO_NAME);
	if (IS_ERR(kecho_device)) {
		err = PTR_ERR(kecho_device);
		goto err_class;
	}

	pr_info("kecho: registered %d:%d, %d-byte buffer\n",
		MAJOR(kecho_devt), MINOR(kecho_devt), KECHO_SIZE);
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
	class_destroy(kecho_class);
	cdev_del(&kecho.cdev);
	unregister_chrdev_region(kecho_devt, 1);
	pr_info("kecho: unregistered\n");
}

module_init(kecho_init);
module_exit(kecho_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ELX-B07 kecho: 4 KiB echo char device");
