// uio_edu.c - user-space driver for QEMU edu via uio_pci_generic (INTx only)
// LP06 lab. Usage: uio_edu <BDF> /dev/uioN
// Flow per interrupt (uio_pci_generic has no irqcontrol hook):
//   kernel IRQ handler masks INTx (sets COMMAND.INTx_Disable) and wakes read()
//   user: read(uio) -> count, read/ack device status, then clear INTx_Disable via config write
// Build (VM): gcc -static -O2 -Wall -Wextra -Werror uio_edu.c -o uio_edu
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define EDU_REG_ID         0x00
#define EDU_REG_FACT       0x08
#define EDU_REG_STATUS     0x20
#define EDU_REG_IRQ_STATUS 0x24
#define EDU_REG_IRQ_RAISE  0x60
#define EDU_REG_IRQ_ACK    0x64
#define PCI_COMMAND        0x04
#define PCI_COMMAND_INTX_DISABLE 0x400

static volatile uint32_t *regs;
static int cfg_fd;

static uint32_t rd32(uint32_t off) { return regs[off / 4]; }
static void wr32(uint32_t off, uint32_t v) { regs[off / 4] = v; }

static int intx_unmask(void)
{
    uint16_t cmd;

    if (pread(cfg_fd, &cmd, 2, PCI_COMMAND) != 2)
        return -errno;
    cmd &= (uint16_t)~PCI_COMMAND_INTX_DISABLE;
    if (pwrite(cfg_fd, &cmd, 2, PCI_COMMAND) != 2)
        return -errno;
    return 0;
}

/* wait one UIO interrupt; returns new total count or -errno */
static long wait_irq(int uio_fd, int timeout_ms)
{
    struct pollfd p = { .fd = uio_fd, .events = POLLIN };
    uint32_t count;
    int r = poll(&p, 1, timeout_ms);

    if (r == 0)
        return -ETIMEDOUT;
    if (r < 0)
        return -errno;
    if (read(uio_fd, &count, 4) != 4)
        return -EIO;
    return (long)count;
}

int main(int argc, char **argv)
{
    char path[256];
    int uio_fd, fails = 0;
    long cnt;
    void *p;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <BDF> /dev/uioN\n", argv[0]);
        return 2;
    }
    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/config", argv[1]);
    cfg_fd = open(path, O_RDWR);
    uio_fd = open(argv[2], O_RDWR);
    if (cfg_fd < 0 || uio_fd < 0) {
        fprintf(stderr, "open: %s\n", strerror(errno));
        return 1;
    }
    /* UIO map N is selected by mmap offset N * page size; map0 = BAR0 */
    p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, uio_fd, 0);
    if (p == MAP_FAILED) {
        fprintf(stderr, "mmap uio map0: %s\n", strerror(errno));
        return 1;
    }
    regs = p;
    printf("uio map0 (BAR0) ID=0x%08x\n", rd32(EDU_REG_ID));

    for (int i = 0; i < 3; i++) {
        uint32_t val = 0x10u << i, st;

        if (intx_unmask()) {
            fprintf(stderr, "intx_unmask failed\n");
            return 1;
        }
        wr32(EDU_REG_IRQ_RAISE, val);
        cnt = wait_irq(uio_fd, 2000);
        st = rd32(EDU_REG_IRQ_STATUS);
        wr32(EDU_REG_IRQ_ACK, st);          /* lower the level-triggered INTx at the source */
        printf("raise 0x%02x -> uio count=%ld irq_status=0x%08x %s\n", val, cnt, st,
               (cnt > 0 && st == val) ? "OK" : "BAD");
        fails += !(cnt > 0 && st == val);
    }

    /* factorial with completion interrupt */
    intx_unmask();
    wr32(EDU_REG_STATUS, 0x80);
    wr32(EDU_REG_FACT, 7);
    cnt = wait_irq(uio_fd, 2000);
    {
        uint32_t st = rd32(EDU_REG_IRQ_STATUS);
        wr32(EDU_REG_IRQ_ACK, st);
        printf("factorial(7) irq: count=%ld irq_status=0x%08x fact=%u %s\n", cnt, st,
               rd32(EDU_REG_FACT), rd32(EDU_REG_FACT) == 5040u ? "OK" : "BAD");
        fails += rd32(EDU_REG_FACT) != 5040u;
    }
    wr32(EDU_REG_STATUS, 0);
    intx_unmask();

    munmap(p, 4096);
    close(uio_fd);
    close(cfg_fd);
    return fails ? 1 : 0;
}
