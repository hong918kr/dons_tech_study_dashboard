// edu_mmap.c - user-space register access to QEMU edu BAR0 via sysfs resource0
// LP06 lab. Usage: edu_mmap 0000:00:02.0
// Build (VM): aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -Werror edu_mmap.c -o edu_mmap
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define EDU_REG_ID       0x00
#define EDU_REG_LIVENESS 0x04
#define EDU_REG_FACT     0x08
#define EDU_REG_STATUS   0x20
#define EDU_STATUS_COMPUTING 0x01u

static volatile uint32_t *regs;

static uint32_t rd32(uint32_t off) { return regs[off / 4]; }
static void wr32(uint32_t off, uint32_t v) { regs[off / 4] = v; }

int main(int argc, char **argv)
{
    char path[256];
    const size_t maplen = 4096;   /* only need the first page of the 1 MiB BAR */
    struct timespec t0, t1;
    uint32_t id, live, fact, st;
    long polls = 0;
    void *p;
    int fd;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <domain:bus:dev.fn>\n", argv[0]);
        return 2;
    }
    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/resource0", argv[1]);
    fd = open(path, O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return 1;
    }
    p = mmap(NULL, maplen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        fprintf(stderr, "mmap: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    regs = p;

    id = rd32(EDU_REG_ID);
    printf("resource0 mmap OK: ID=0x%08x (%s)\n", id,
           (id & 0xffff) == 0x00ed ? "edu" : "not edu?");

    wr32(EDU_REG_LIVENESS, 0xcafef00du);
    live = rd32(EDU_REG_LIVENESS);
    printf("liveness: wrote 0xcafef00d read 0x%08x (%s)\n", live,
           live == ~0xcafef00du ? "OK" : "BAD");

    clock_gettime(CLOCK_MONOTONIC, &t0);
    wr32(EDU_REG_FACT, 12);
    do {
        st = rd32(EDU_REG_STATUS);
        polls++;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        if (t1.tv_sec - t0.tv_sec > 1) {
            fprintf(stderr, "timeout: status=0x%08x\n", st);
            break;
        }
    } while (st & EDU_STATUS_COMPUTING);
    fact = rd32(EDU_REG_FACT);
    printf("factorial(12) = %u after %ld status polls (%s)\n", fact, polls,
           fact == 479001600u ? "OK" : "BAD");

    munmap(p, maplen);
    close(fd);
    return (live == ~0xcafef00du && fact == 479001600u) ? 0 : 1;
}
