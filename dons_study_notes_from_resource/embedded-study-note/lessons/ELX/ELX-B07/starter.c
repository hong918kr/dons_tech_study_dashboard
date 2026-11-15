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
	/*
	 * TODO(A2): read from *ppos.
	 *  - count == 0 -> 0; take d->lock (interruptible)
	 *  - pos >= len -> 0 (EOF)
	 *  - copy min(count, len - pos) with copy_to_user()
	 *  - nothing copied -> -EFAULT; part copied -> return the part
	 *  - advance *ppos by what was copied; unlock on every path
	 */
	return -EINVAL;
}

/* ---- Part A/B: write at *ppos, short write, -ENOSPC ---------------------- */
static ssize_t kecho_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	/*
	 * TODO(A3): write at *ppos.
	 *  - count == 0 -> 0; pos >= KECHO_SIZE -> -ENOSPC (never 0)
	 *  - copy min(count, KECHO_SIZE - pos); short write if it does not fit
	 *  - copy_from_user() into d->bounce, then memcpy only the bytes that
	 *    arrived into d->data (Part B: why not straight into d->data?)
	 *  - len = max(len, pos + n); advance *ppos; -EFAULT if nothing came
	 */
	return -EINVAL;
}

/* ---- Part B: llseek ------------------------------------------------------ */
static loff_t kecho_llseek(struct file *file, loff_t off, int whence)
{
	/*
	 * TODO(B1): SEEK_SET (0) / SEEK_CUR (f_pos) / SEEK_END (len) + off.
	 * Result must stay in 0..KECHO_SIZE, else -EINVAL and f_pos unchanged.
	 * Any other whence -> -EINVAL. Return the new position.
	 */
	return -EINVAL;
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
	mutex_init(&kecho.lock);

	/*
	 * TODO(A1): register in this order, each step guarded by
	 * "err = kecho_inject(N) ?: <call>;" so fail_at=N can test the unwind:
	 *  1 alloc_chrdev_region(&kecho_devt, 0, 1, KECHO_NAME)
	 *  2 cdev_init(&kecho.cdev, &kecho_fops) + owner + cdev_add(.., 1)
	 *  3 kecho_class = class_create(KECHO_NAME)            (IS_ERR!)
	 *  4 kecho_device = device_create(kecho_class, NULL, kecho_devt,
	 *                                 NULL, KECHO_NAME)   (IS_ERR!)
	 * On failure undo the finished steps in reverse (goto ladder).
	 */
	(void)kecho_inject; (void)kecho_devt; (void)kecho_class; (void)kecho_device;
	return 0;
}

static void __exit kecho_exit(void)
{
	/* TODO(A1): undo all four steps of kecho_init, in reverse order */
}

module_init(kecho_init);
module_exit(kecho_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ELX-B07 kecho: 4 KiB echo char device");
