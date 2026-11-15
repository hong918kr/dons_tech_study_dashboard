/* ENV: qemu */
// SPDX-License-Identifier: GPL-2.0
/*
 * ELX-B08 kecho + ioctl: /dev/kecho (4096 B 버퍼, ELX-B07 의 read/write/llseek 규칙을
 * 짧게 다시 구현한 사본) 에 GET_STATS / SET_MODE / RESET ioctl 을 붙인다.
 *   - 번호 검사: _IOC_TYPE / _IOC_NR / _IOC_DIR / _IOC_SIZE 가 하나라도 다르면 -ENOTTY
 *   - 나쁜 유저 포인터: -EFAULT,  모르는 mode: -EINVAL
 *   - 32비트 앱: .compat_ioctl = compat_ptr_ioctl (구조체가 ABI 독립이라 충분)
 * TODO 4곳: kecho_check_cmd / kecho_get_stats / kecho_set_mode + kecho_reset / .compat_ioctl
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include <linux/build_bug.h>
#include "kecho_ioctl.h"

#define KECHO_BUF_SZ	4096

struct kecho_dev {
	struct mutex lock;		/* buf, data_len, mode, 통계 전부를 지킨다 */
	char buf[KECHO_BUF_SZ];
	size_t data_len;
	u32 mode;
	u64 bytes_written, bytes_read;
	u32 nwrites, nreads, bad_cmds;
	struct cdev cdev;
};

static struct kecho_dev kdev;
static dev_t kecho_devt;
static struct class *kecho_class;

/* 이 드라이버가 아는 명령 전체. 인덱스 = nr, 값 = 번호 전체(dir/size/type/nr). */
static const unsigned int kecho_cmds[] = {
	[KECHO_NR_GET_STATS]	= KECHO_IOC_GET_STATS,
	[KECHO_NR_SET_MODE]	= KECHO_IOC_SET_MODE,
	[KECHO_NR_RESET]	= KECHO_IOC_RESET,
};

/* ---------------- B07 사본: open / read / write / llseek ---------------- */
static int kecho_open(struct inode *inode, struct file *f)
{
	f->private_data = container_of(inode->i_cdev, struct kecho_dev, cdev);
	return 0;
}

static ssize_t kecho_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
	struct kecho_dev *d = f->private_data;
	ssize_t ret;

	mutex_lock(&d->lock);
	if (*ppos >= d->data_len) {
		ret = 0;				/* EOF */
		goto out;
	}
	count = min_t(size_t, count, d->data_len - *ppos);
	if (copy_to_user(ubuf, d->buf + *ppos, count)) {
		ret = -EFAULT;
		goto out;
	}
	*ppos += count;
	d->nreads++;
	d->bytes_read += count;
	ret = count;
out:
	mutex_unlock(&d->lock);
	return ret;
}

static ssize_t kecho_write(struct file *f, const char __user *ubuf, size_t count,
			   loff_t *ppos)
{
	struct kecho_dev *d = f->private_data;
	ssize_t ret;
	size_t i;

	mutex_lock(&d->lock);
	if (*ppos >= KECHO_BUF_SZ) {
		ret = -ENOSPC;				/* 남은 공간 0 */
		goto out;
	}
	count = min_t(size_t, count, KECHO_BUF_SZ - *ppos);	/* 일부만 들어가면 짧은 쓰기 */
	if (copy_from_user(d->buf + *ppos, ubuf, count)) {
		ret = -EFAULT;
		goto out;
	}
	if (d->mode == KECHO_MODE_UPPER)
		for (i = 0; i < count; i++)
			d->buf[*ppos + i] = toupper(d->buf[*ppos + i]);
	*ppos += count;
	d->data_len = max_t(size_t, d->data_len, *ppos);
	d->nwrites++;
	d->bytes_written += count;
	ret = count;
out:
	mutex_unlock(&d->lock);
	return ret;
}

static loff_t kecho_llseek(struct file *f, loff_t off, int whence)
{
	struct kecho_dev *d = f->private_data;
	loff_t pos;

	mutex_lock(&d->lock);
	pos = generic_file_llseek_size(f, off, whence, KECHO_BUF_SZ, d->data_len);
	mutex_unlock(&d->lock);
	return pos;
}

/* ---------------- ioctl ---------------- */

/* 번호의 네 필드를 이 드라이버가 정의한 번호와 하나씩 비교한다. 다르면 -ENOTTY. */
static int kecho_check_cmd(unsigned int cmd)
{
	unsigned int nr = _IOC_NR(cmd);

	if (_IOC_TYPE(cmd) != KECHO_IOC_MAGIC)
		return -ENOTTY;			/* 다른 드라이버(또는 tty) 번호 */
	if (nr >= ARRAY_SIZE(kecho_cmds) || !kecho_cmds[nr])
		return -ENOTTY;			/* 모르는 nr */
	if (_IOC_DIR(cmd) != _IOC_DIR(kecho_cmds[nr]))
		return -ENOTTY;			/* _IOR 자리에 _IOW 등 */
	if (_IOC_SIZE(cmd) != _IOC_SIZE(kecho_cmds[nr]))
		return -ENOTTY;			/* 구조체 크기가 다른 ABI 로 빌드된 앱 */
	return 0;
}

static long kecho_get_stats(struct kecho_dev *d, void __user *argp)
{
	struct kecho_stats st;

	memset(&st, 0, sizeof(st));		/* __pad 까지 0: 커널 스택 내용 누출 방지 */
	mutex_lock(&d->lock);
	st.bytes_written = d->bytes_written;
	st.bytes_read = d->bytes_read;
	st.nwrites = d->nwrites;
	st.nreads = d->nreads;
	st.data_len = d->data_len;
	st.mode = d->mode;
	st.bad_cmds = d->bad_cmds;
	mutex_unlock(&d->lock);			/* 스냅숏만 락 안에서, 유저 복사는 밖에서 */
	if (copy_to_user(argp, &st, sizeof(st)))
		return -EFAULT;
	return 0;
}

static long kecho_set_mode(struct kecho_dev *d, void __user *argp)
{
	u32 mode;

	if (get_user(mode, (u32 __user *)argp))
		return -EFAULT;			/* 먼저 복사, 그다음 검사 */
	if (mode > KECHO_MODE_MAX)
		return -EINVAL;			/* 명령은 알지만 값이 틀림 */
	mutex_lock(&d->lock);
	d->mode = mode;
	mutex_unlock(&d->lock);
	return 0;
}

static long kecho_reset(struct kecho_dev *d)
{
	mutex_lock(&d->lock);			/* 데이터와 통계를 지운다. mode 는 유지 */
	memset(d->buf, 0, sizeof(d->buf));
	d->data_len = 0;
	d->bytes_written = d->bytes_read = 0;
	d->nwrites = d->nreads = d->bad_cmds = 0;
	mutex_unlock(&d->lock);
	return 0;
}

static long kecho_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct kecho_dev *d = f->private_data;
	void __user *argp = (void __user *)arg;
	int ret;

	ret = kecho_check_cmd(cmd);
	if (ret) {
		mutex_lock(&d->lock);
		d->bad_cmds++;
		mutex_unlock(&d->lock);
		return ret;
	}
	switch (_IOC_NR(cmd)) {			/* 위에서 네 필드를 다 확인했으니 nr 로 갈라도 안전 */
	case KECHO_NR_GET_STATS:
		return kecho_get_stats(d, argp);
	case KECHO_NR_SET_MODE:
		return kecho_set_mode(d, argp);
	case KECHO_NR_RESET:
		return kecho_reset(d);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations kecho_fops = {
	.owner		= THIS_MODULE,
	.open		= kecho_open,
	.read		= kecho_read,
	.write		= kecho_write,
	.llseek		= kecho_llseek,
	.unlocked_ioctl	= kecho_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,	/* 32비트 앱: arg 를 compat_ptr() 로 넓혀 위로 */
};

static int __init kecho_init(void)
{
	struct device *dev;
	int ret;

	BUILD_BUG_ON(sizeof(struct kecho_stats) != 40);
	BUILD_BUG_ON(offsetof(struct kecho_stats, __pad) != 36);
	mutex_init(&kdev.lock);
	ret = alloc_chrdev_region(&kecho_devt, 0, 1, "kecho");
	if (ret)
		return ret;
	cdev_init(&kdev.cdev, &kecho_fops);
	ret = cdev_add(&kdev.cdev, kecho_devt, 1);
	if (ret)
		goto err_region;
	kecho_class = class_create("kecho");
	if (IS_ERR(kecho_class)) {
		ret = PTR_ERR(kecho_class);
		goto err_cdev;
	}
	dev = device_create(kecho_class, NULL, kecho_devt, NULL, "kecho");
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err_class;
	}
	pr_info("kecho: /dev/kecho %d:%d GET_STATS=0x%08x SET_MODE=0x%08x RESET=0x%08x\n",
		MAJOR(kecho_devt), MINOR(kecho_devt), (unsigned int)KECHO_IOC_GET_STATS,
		(unsigned int)KECHO_IOC_SET_MODE, (unsigned int)KECHO_IOC_RESET);
	return 0;

err_class:
	class_destroy(kecho_class);
err_cdev:
	cdev_del(&kdev.cdev);
err_region:
	unregister_chrdev_region(kecho_devt, 1);
	return ret;
}

static void __exit kecho_exit(void)
{
	device_destroy(kecho_class, kecho_devt);
	class_destroy(kecho_class);
	cdev_del(&kdev.cdev);
	unregister_chrdev_region(kecho_devt, 1);
}

module_init(kecho_init);
module_exit(kecho_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ELX-B08 kecho char device with a versioned ioctl ABI");
