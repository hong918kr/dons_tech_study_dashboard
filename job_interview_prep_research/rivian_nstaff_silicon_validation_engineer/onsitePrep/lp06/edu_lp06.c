// SPDX-License-Identifier: GPL-2.0
/*
 * edu_lp06.c - minimal PCI endpoint driver for QEMU's "edu" device (1234:11e8)
 *
 * LP06 study note lab (Rivian onsite prep, 2026-10-31).
 * Shows the canonical probe order:
 *   pcim_enable_device -> pcim_iomap_region(BAR0) -> pci_set_master
 *   -> dma_set_mask_and_coherent -> pci_alloc_irq_vectors -> devm_request_irq
 *   -> device self-test (ID, liveness, factorial poll + IRQ, raise IRQ, DMA).
 *
 * Register map: QEMU docs/specs/edu.rst + hw/misc/edu.c.
 * Built against the lab kernel 6.18.55: make -C /var/lab/out/linux-build M=$PWD modules
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/io.h>

#define DRV_NAME "edu_lp06"

#define EDU_REG_ID		0x00	/* RO: 0xRRrr00ed */
#define EDU_REG_LIVENESS	0x04	/* RW: reads back ~written */
#define EDU_REG_FACT		0x08	/* RW: write n, read n! */
#define EDU_REG_STATUS		0x20
#define  EDU_STATUS_COMPUTING	0x01	/* RO */
#define  EDU_STATUS_IRQFACT	0x80	/* RW: raise IRQ when factorial done */
#define EDU_REG_IRQ_STATUS	0x24	/* RO */
#define EDU_REG_IRQ_RAISE	0x60	/* WO: OR value into IRQ status */
#define EDU_REG_IRQ_ACK		0x64	/* WO: clear bits from IRQ status */
#define EDU_REG_DMA_SRC		0x80	/* 64-bit */
#define EDU_REG_DMA_DST		0x88	/* 64-bit */
#define EDU_REG_DMA_CNT		0x90	/* 64-bit */
#define EDU_REG_DMA_CMD		0x98	/* 64-bit */
#define  EDU_DMA_START		0x01
#define  EDU_DMA_TO_RAM		0x02	/* 0: RAM -> EDU, 1: EDU -> RAM */
#define  EDU_DMA_IRQ		0x04	/* raise IRQ 0x100 when done */
#define EDU_DMA_BUF_OFF		0x40000	/* device-internal 4 KiB buffer */
#define EDU_DMA_BUF_SIZE	4096

#define EDU_IRQ_FACT		0x00000001
#define EDU_IRQ_DMA		0x00000100

static unsigned int dma_bits = 28;	/* the real edu default is 28 bits */
module_param(dma_bits, uint, 0444);
MODULE_PARM_DESC(dma_bits, "DMA mask width (match QEMU -device edu,dma_mask=)");

static bool force_intx;
module_param(force_intx, bool, 0444);
MODULE_PARM_DESC(force_intx, "Use INTx instead of MSI");

struct edu {
	struct pci_dev *pdev;
	void __iomem *bar0;
	int irq;
	bool dma_ok;
	u32 last_irq_status;
	unsigned int irq_count;
	struct completion done;
};

static irqreturn_t edu_isr(int irq, void *data)
{
	struct edu *edu = data;
	u32 st = readl(edu->bar0 + EDU_REG_IRQ_STATUS);

	if (!st)
		return IRQ_NONE;	/* shared INTx line: not ours */

	/* edu.rst: ACK is required even with MSI, else status stays set */
	writel(st, edu->bar0 + EDU_REG_IRQ_ACK);
	edu->last_irq_status = st;
	edu->irq_count++;
	complete(&edu->done);
	return IRQ_HANDLED;
}

static int edu_wait_irq(struct edu *edu, u32 expect, const char *what)
{
	if (!wait_for_completion_timeout(&edu->done, msecs_to_jiffies(2000))) {
		dev_err(&edu->pdev->dev, "%s: no interrupt within 2 s\n", what);
		return -ETIMEDOUT;
	}
	dev_info(&edu->pdev->dev, "%s: IRQ received, irq_status=0x%08x (expect 0x%08x) count=%u\n",
		 what, edu->last_irq_status, expect, edu->irq_count);
	return (edu->last_irq_status & expect) ? 0 : -EIO;
}

static int edu_fact_poll(struct edu *edu, u32 n, u32 *out)
{
	u32 st;
	int ret;

	writel(n, edu->bar0 + EDU_REG_FACT);
	ret = readl_poll_timeout(edu->bar0 + EDU_REG_STATUS, st,
				 !(st & EDU_STATUS_COMPUTING), 10, 100000);
	if (ret)
		return ret;
	*out = readl(edu->bar0 + EDU_REG_FACT);
	return 0;
}

static int edu_dma_xfer(struct edu *edu, u64 src, u64 dst, u64 cnt, u64 dir)
{
	reinit_completion(&edu->done);
	/* edu only decodes 0x80/0x88/0x90/0x98 as whole 4- or 8-byte regs */
	writeq(src, edu->bar0 + EDU_REG_DMA_SRC);
	writeq(dst, edu->bar0 + EDU_REG_DMA_DST);
	writeq(cnt, edu->bar0 + EDU_REG_DMA_CNT);
	writeq(EDU_DMA_START | EDU_DMA_IRQ | dir, edu->bar0 + EDU_REG_DMA_CMD);
	return edu_wait_irq(edu, EDU_IRQ_DMA, dir ? "dma edu->ram" : "dma ram->edu");
}

static int edu_dma_test(struct edu *edu)
{
	struct device *dev = &edu->pdev->dev;
	const size_t len = 256;
	dma_addr_t handle;
	u8 *buf;
	size_t i;
	int ret;

	/* one coherent page: [0..len) = source pattern, [2048..) = destination */
	buf = dma_alloc_coherent(dev, PAGE_SIZE, &handle, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	for (i = 0; i < len; i++) {
		buf[i] = (u8)(i * 7 + 3);
		buf[2048 + i] = 0;
	}
	dev_info(dev, "dma: coherent buf cpu=%px dma=%pad\n", buf, &handle);

	ret = edu_dma_xfer(edu, handle, EDU_DMA_BUF_OFF, len, 0);
	if (!ret)
		ret = edu_dma_xfer(edu, EDU_DMA_BUF_OFF, handle + 2048, len, EDU_DMA_TO_RAM);
	if (!ret) {
		ret = memcmp(buf, buf + 2048, len) ? -EIO : 0;
		dev_info(dev, "dma: round trip %zu bytes %s (dst[0..3]=%02x %02x %02x %02x)\n",
			 len, ret ? "MISMATCH" : "OK",
			 buf[2048], buf[2049], buf[2050], buf[2051]);
	}
	dma_free_coherent(dev, PAGE_SIZE, buf, handle);
	return ret;
}

static void edu_selftest(struct edu *edu)
{
	struct device *dev = &edu->pdev->dev;
	u32 id, v, f;

	id = readl(edu->bar0 + EDU_REG_ID);
	dev_info(dev, "ID reg = 0x%08x (major %u minor %u)\n", id, id >> 24, (id >> 16) & 0xff);

	writel(0x12345678, edu->bar0 + EDU_REG_LIVENESS);
	v = readl(edu->bar0 + EDU_REG_LIVENESS);
	dev_info(dev, "liveness: wrote 0x12345678 read 0x%08x (%s)\n", v,
		 v == ~0x12345678u ? "OK" : "BAD");

	if (!edu_fact_poll(edu, 5, &f))
		dev_info(dev, "factorial(5) by polling = %u\n", f);

	/* factorial with completion interrupt */
	reinit_completion(&edu->done);
	writel(EDU_STATUS_IRQFACT, edu->bar0 + EDU_REG_STATUS);
	writel(10, edu->bar0 + EDU_REG_FACT);
	if (!edu_wait_irq(edu, EDU_IRQ_FACT, "factorial(10) irq"))
		dev_info(dev, "factorial(10) = %u\n", readl(edu->bar0 + EDU_REG_FACT));
	writel(0, edu->bar0 + EDU_REG_STATUS);

	/* software-raised interrupt */
	reinit_completion(&edu->done);
	writel(0x00ab0000, edu->bar0 + EDU_REG_IRQ_RAISE);
	edu_wait_irq(edu, 0x00ab0000, "raise irq");

	if (edu->dma_ok)
		dev_info(dev, "dma test: %s\n", edu_dma_test(edu) ? "FAIL" : "PASS");
	else
		dev_info(dev, "dma test skipped (no usable DMA mask)\n");
}

/* sysfs: echo N > fact ; cat fact  -> N! */
static ssize_t fact_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct edu *edu = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", readl(edu->bar0 + EDU_REG_FACT));
}

static ssize_t fact_store(struct device *dev, struct device_attribute *a,
			  const char *buf, size_t count)
{
	struct edu *edu = dev_get_drvdata(dev);
	u32 n, out;
	int ret;

	ret = kstrtou32(buf, 0, &n);
	if (ret)
		return ret;
	ret = edu_fact_poll(edu, n, &out);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(fact);

static struct attribute *edu_attrs[] = {
	&dev_attr_fact.attr,
	NULL,
};
ATTRIBUTE_GROUPS(edu);

static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	unsigned int flags;
	struct edu *edu;
	int ret, nvec;

	edu = devm_kzalloc(dev, sizeof(*edu), GFP_KERNEL);
	if (!edu)
		return -ENOMEM;
	edu->pdev = pdev;
	init_completion(&edu->done);

	/* 1. enable: sets Memory Space Enable in COMMAND, wakes to D0 */
	ret = pcim_enable_device(pdev);
	if (ret)
		return dev_err_probe(dev, ret, "pcim_enable_device\n");

	/* 2. claim + ioremap BAR0 (pcim_iomap_regions() is deprecated) */
	edu->bar0 = pcim_iomap_region(pdev, 0, DRV_NAME);
	if (IS_ERR(edu->bar0))
		return dev_err_probe(dev, PTR_ERR(edu->bar0), "BAR0\n");
	dev_info(dev, "BAR0 %pR mapped at %px\n", &pdev->resource[0], edu->bar0);

	/* 3. allow the device to master the bus (DMA + MSI are memory writes) */
	pci_set_master(pdev);

	/* 4. tell the DMA API what addresses the device can generate */
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(dma_bits));
	if (ret) {
		dev_warn(dev, "dma_set_mask_and_coherent(%u bits) failed: %d\n", dma_bits, ret);
	} else {
		edu->dma_ok = true;
		dev_info(dev, "DMA mask %u bits OK\n", dma_bits);
	}

	/* 5. interrupt vectors: MSI preferred, INTx fallback */
	flags = force_intx ? PCI_IRQ_INTX : (PCI_IRQ_MSI | PCI_IRQ_INTX);
	nvec = pci_alloc_irq_vectors(pdev, 1, 1, flags);
	if (nvec < 0)
		return dev_err_probe(dev, nvec, "pci_alloc_irq_vectors\n");
	edu->irq = pci_irq_vector(pdev, 0);
	ret = devm_request_irq(dev, edu->irq, edu_isr,
			       pdev->msi_enabled ? 0 : IRQF_SHARED, DRV_NAME, edu);
	if (ret)
		return dev_err_probe(dev, ret, "request_irq %d\n", edu->irq);
	dev_info(dev, "irq %d via %s\n", edu->irq, pdev->msi_enabled ? "MSI" : "INTx");

	pci_set_drvdata(pdev, edu);

	/* 6. device init / self-test */
	edu_selftest(edu);
	return 0;
}

static void edu_remove(struct pci_dev *pdev)
{
	struct edu *edu = pci_get_drvdata(pdev);

	/* quiesce the device; devres then frees irq, vectors, mapping, enable */
	writel(0, edu->bar0 + EDU_REG_STATUS);
	dev_info(&pdev->dev, "remove: %u interrupts handled\n", edu->irq_count);
}

static const struct pci_device_id edu_ids[] = {
	{ PCI_DEVICE(0x1234, 0x11e8) },
	{ }
};
MODULE_DEVICE_TABLE(pci, edu_ids);

static struct pci_driver edu_driver = {
	.name		= DRV_NAME,
	.id_table	= edu_ids,
	.probe		= edu_probe,
	.remove		= edu_remove,
	.dev_groups	= edu_groups,
};
module_pci_driver(edu_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LP06 lab: minimal driver for QEMU edu PCI device");
