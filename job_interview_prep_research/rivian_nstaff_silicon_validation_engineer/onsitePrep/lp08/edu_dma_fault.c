// edu_dma_fault.c - make the QEMU "edu" PCI device (1234:11e8) DMA to an IOVA
// that is NOT mapped in the SMMU, so the guest kernel logs a real SMMUv3 fault.
// Linux-only (sysfs + mmap). Build static in the Lima VM:
//   gcc -static -O2 -Wall -Wextra -o edu_dma_fault edu_dma_fault.c
// Run inside the QEMU guest (root):  ./edu_dma_fault 0000:02:00.0 0x1000
// edu DMA regs (QEMU docs/specs/edu.rst): 0x80 src, 0x88 dst, 0x90 count, 0x98 cmd
//   cmd bit0 = start (HW clears when done), bit1 = direction (0: RAM->edu, 1: edu->RAM)
//   edu-internal buffer lives at device address 0x40000 (4 KiB).
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define EDU_ID      0x00
#define EDU_DMA_SRC 0x80
#define EDU_DMA_DST 0x88
#define EDU_DMA_CNT 0x90
#define EDU_DMA_CMD 0x98
#define EDU_BUF     0x40000ULL

static volatile uint8_t *bar;
static uint64_t rd64(unsigned o) { return *(volatile uint64_t *)(bar + o); }
static void wr64(unsigned o, uint64_t v) { *(volatile uint64_t *)(bar + o) = v; }

static int run_dma(uint64_t src, uint64_t dst, uint64_t cnt, uint64_t dir)
{
    wr64(EDU_DMA_SRC, src);
    wr64(EDU_DMA_DST, dst);
    wr64(EDU_DMA_CNT, cnt);
    wr64(EDU_DMA_CMD, 1 | (dir << 1));          /* start, no IRQ */
    for (int i = 0; i < 200; i++) {             /* edu runs DMA from a 100 ms timer */
        if (!(rd64(EDU_DMA_CMD) & 1)) return 0;
        usleep(10000);
    }
    return -1;
}

int main(int argc, char **argv)
{
    const char *bdf = argc > 1 ? argv[1] : "0000:02:00.0";
    uint64_t iova = argc > 2 ? strtoull(argv[2], NULL, 0) : 0x1000;
    char p[256];

    /* 1. enable Memory Space + Bus Master in the Command register (config offset 0x04) */
    snprintf(p, sizeof p, "/sys/bus/pci/devices/%s/config", bdf);
    int cfd = open(p, O_RDWR);
    if (cfd < 0) { perror(p); return 1; }
    uint16_t cmd;
    if (pread(cfd, &cmd, 2, 4) != 2) { perror("pread"); return 1; }
    printf("Command before = 0x%04x\n", cmd);
    cmd |= 0x0002 | 0x0004;                     /* PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER */
    if (pwrite(cfd, &cmd, 2, 4) != 2) { perror("pwrite"); return 1; }
    if (pread(cfd, &cmd, 2, 4) != 2) { perror("pread"); return 1; }
    printf("Command after  = 0x%04x (Mem+ BusMaster+)\n", cmd);

    /* 2. map BAR0 */
    snprintf(p, sizeof p, "/sys/bus/pci/devices/%s/resource0", bdf);
    int fd = open(p, O_RDWR | O_SYNC);
    if (fd < 0) { perror(p); return 1; }
    bar = mmap(NULL, 1 << 20, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bar == MAP_FAILED) { perror("mmap"); return 1; }
    printf("edu id = 0x%08x\n", *(volatile uint32_t *)(bar + EDU_ID));

    /* 3. DMA read from an IOVA nobody mapped (device has no driver -> empty DMA domain) */
    printf("DMA RAM(iova 0x%llx) -> edu: %s\n", (unsigned long long)iova,
           run_dma(iova, EDU_BUF, 64, 0) ? "timeout" : "cmd done");
    /* 4. DMA write to the same unmapped IOVA */
    printf("DMA edu -> RAM(iova 0x%llx): %s\n", (unsigned long long)iova,
           run_dma(EDU_BUF, iova, 64, 1) ? "timeout" : "cmd done");
    return 0;
}
