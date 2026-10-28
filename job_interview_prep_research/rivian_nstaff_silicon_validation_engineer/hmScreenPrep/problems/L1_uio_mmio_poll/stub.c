/*
 * L1  Userspace Register Access on Zynq PetaLinux (/dev/mem + UIO)
 * Practice stub: implement the six functions below, then ./run.sh L1
 *
 * A validation tool on Zynq UltraScale+ (PetaLinux) drives a PL IP block from userspace.
 * The IP sits behind M_AXI_HPM0_FPD at physical 0xA001_0000. Registers are reached by
 * mmap()ing /dev/mem; the IP's interrupt is exposed by the generic-uio driver as /dev/uioN.
 *
 * Implement:
 *   regmap_open   validate args (m/path NULL, len 0, phys or len not 4-byte aligned,
 *                 phys + len overflows -> -EINVAL). open(path, O_RDWR | O_SYNC), mmap the
 *                 page-aligned window that covers [phys, phys + len) (page = sysconf(_SC_PAGESIZE)),
 *                 set base = map + (phys - page_base). Return 0 or -errno.
 *                 On ANY failure the regmap is left closed (fd = -1, map = NULL, base = NULL).
 *   regmap_close  munmap + close; safe on a closed regmap and when called twice.
 *   reg_rd32 / reg_wr32
 *                 32-bit volatile access at base + off. Contract: off must be 4-byte aligned
 *                 and off + 4 <= len. A bad offset does NO bus access: read returns REG_BAD
 *                 (0xFFFFFFFF, like an aborted PCIe read), write is dropped.
 *   reg_poll      wait until (reg & mask) == want. CLOCK_MONOTONIC deadline; the register is
 *                 always read once more after the deadline. 0 or -ETIMEDOUT.
 *                 Bad offset, or want has bits outside mask -> -EINVAL.
 *   uio_wait_irq  UIO handshake: write a 4-byte 1 (re-enable the IRQ), poll() for POLLIN,
 *                 read the 4-byte interrupt count.
 *                 *count is IN/OUT: in = last count the caller saw, out = new count.
 *                 Returns the number of MISSED interrupts (0 = exactly one new IRQ),
 *                 -ETIMEDOUT, -EIO (short read/write, EOF, or count did not advance),
 *                 -EINVAL (count NULL), or -errno from write/poll/read.
 *
 * Harness (no hardware, works on macOS): "/dev/mem" is a sparse temp file, so physical
 * address P is file offset P. A helper thread maps the same file pages and plays the PL IP.
 * UIO is a socketpair: the device end reads the 1 we write and writes 4-byte counts.
 *
 * Build & run:  ./run.sh L1
 */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* Zynq-7000 is 32-bit ARM: without -D_FILE_OFFSET_BITS=64, 0xA000_0000 does not fit in off_t. */
_Static_assert(sizeof(off_t) == 8, "build with -D_FILE_OFFSET_BITS=64");

#define REG_BAD 0xFFFFFFFFu

struct regmap {
    int               fd;        /* -1 when closed */
    void             *map;       /* page-aligned mmap() result, NULL when closed */
    size_t            map_len;   /* bytes mapped, multiple of the page size */
    volatile uint8_t *base;      /* register offset 0 (= map + in-page offset) */
    size_t            len;       /* register window in bytes */
    uint64_t          phys;      /* physical address of offset 0 */
};

int      regmap_open(struct regmap *m, const char *path, uint64_t phys, size_t len);
void     regmap_close(struct regmap *m);
uint32_t reg_rd32(const struct regmap *m, uint32_t off);
void     reg_wr32(const struct regmap *m, uint32_t off, uint32_t val);
int      reg_poll(const struct regmap *m, uint32_t off, uint32_t mask, uint32_t want,
                  unsigned timeout_ms);
int      uio_wait_irq(int fd, unsigned timeout_ms, uint32_t *count);

/* ---------------- your code ---------------- */
int regmap_open(struct regmap *m, const char *path, uint64_t phys, size_t len)
{
    /* TODO: validate args, open(O_RDWR | O_SYNC), mmap the page-aligned window,
             base = map + (phys - page_base); leave m closed on any failure */
    (void)m; (void)path; (void)phys; (void)len;
    return -ENOSYS;
}

void regmap_close(struct regmap *m)
{
    /* TODO: munmap + close, then mark closed (safe to call twice) */
    (void)m;
}

uint32_t reg_rd32(const struct regmap *m, uint32_t off)
{
    /* TODO: check alignment and bounds, then one volatile 32-bit load */
    (void)m; (void)off;
    return 0;
}

void reg_wr32(const struct regmap *m, uint32_t off, uint32_t val)
{
    /* TODO: check alignment and bounds, then one volatile 32-bit store */
    (void)m; (void)off; (void)val;
}

int reg_poll(const struct regmap *m, uint32_t off, uint32_t mask, uint32_t want,
             unsigned timeout_ms)
{
    /* TODO: CLOCK_MONOTONIC deadline; read until (reg & mask) == want; one read after the deadline */
    (void)m; (void)off; (void)mask; (void)want; (void)timeout_ms;
    return 0;
}

int uio_wait_irq(int fd, unsigned timeout_ms, uint32_t *count)
{
    /* TODO: write 4-byte 1 (unmask), poll() POLLIN with timeout, read 4-byte count,
             return the number of missed interrupts (count delta - 1) */
    (void)fd; (void)timeout_ms; (void)count;
    return -ENOSYS;
}

/* ---------------- test harness (do not modify) ---------------- */
/* PL IP "pgen" register map (offsets from 0xA001_0000) */
#define PL_BASE      0xA0010000ull
#define REG_CTRL     0x00u     /* b0 START (self-clearing)          */
#define REG_STATUS   0x04u     /* b0 BUSY | b1 DONE | b15:8 RESULT  */
#define REG_ID       0x08u     /* reads PGEN_ID                     */
#define REG_SCRATCH  0x0Cu
#define CTRL_START   (1u << 0)
#define ST_BUSY      (1u << 0)
#define ST_DONE      (1u << 1)
#define PGEN_ID      0xC0DE0101u
#define DEV_SPAN     0x10000u  /* harness maps 64 KiB of "PL" (multiple of 4K and 16K pages) */

static volatile uint32_t *dev;  /* the device's own view of the same file pages */
static inline uint32_t dget(uint32_t off) { return dev[off / 4]; }
static inline void dset(uint32_t off, uint32_t v) { dev[off / 4] = v; }

static int64_t test_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void sleep_ms(int ms)
{
    struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

/* PL IP model: wait for START, stay BUSY 20 ms, then DONE with RESULT = 0x5A. */
static atomic_int ip_quit, ip_starts;
static void *pl_ip_thread(void *arg)
{
    (void)arg;
    for (int t = 0; t < 2000 && !atomic_load(&ip_quit); t++) {
        if (dget(REG_CTRL) & CTRL_START) {
            atomic_fetch_add(&ip_starts, 1);
            dset(REG_CTRL, 0);                     /* START self-clears */
            dset(REG_STATUS, ST_BUSY);
            sleep_ms(20);
            dset(REG_STATUS, ST_DONE | (0x5Au << 8));
            return NULL;
        }
        sleep_ms(1);
    }
    return NULL;
}

/* UIO model: fires only after the IRQ is unmasked (like a masked level IRQ). */
typedef struct { int fd; uint32_t fire; int unmask_seen; } UioDev;
static void *uio_dev_thread(void *arg)
{
    UioDev *u = arg;
    struct pollfd p = { .fd = u->fd, .events = POLLIN, .revents = 0 };
    uint32_t v = 0;
    if (poll(&p, 1, 3000) == 1 && read(u->fd, &v, 4) == 4 && v == 1) {
        u->unmask_seen = 1;
        sleep_ms(20);
        (void)!write(u->fd, &u->fire, 4);          /* "IRQ": kernel bumps the count */
    }
    return NULL;
}

/* device end: read back what the tool wrote; returns words read (expects exactly one 1) */
static int drain_unmask(int dfd, uint32_t *last)
{
    int k = 0;
    uint32_t v;
    struct pollfd p = { .fd = dfd, .events = POLLIN, .revents = 0 };
    while (poll(&p, 1, 0) == 1 && (p.revents & POLLIN) && read(dfd, &v, 4) == 4) { *last = v; k++; }
    return k;
}
static void fire(int dfd, uint32_t c) { (void)!write(dfd, &c, 4); }

int main(void)
{
    signal(SIGPIPE, SIG_IGN);   /* writing to a closed socket returns EPIPE instead */

    char path[512];
    const char *tmp = getenv("TMPDIR");
    snprintf(path, sizeof path, "%s/l1_devmem_XXXXXX", tmp && *tmp ? tmp : "/tmp");
    int hfd = mkstemp(path);
    if (hfd < 0 || ftruncate(hfd, (off_t)(PL_BASE + DEV_SPAN)) != 0) {
        printf("harness: cannot create sparse /dev/mem file\n");
        return 2;
    }
    void *dv = mmap(NULL, DEV_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, hfd, (off_t)PL_BASE);
    if (dv == MAP_FAILED) { printf("harness: mmap failed\n"); unlink(path); return 2; }
    dev = dv;
    const size_t pg = (size_t)sysconf(_SC_PAGESIZE);
    dset(REG_ID, PGEN_ID);

    /* 1. aligned window: map fields, ID read, write reaches the device, device write is seen */
    struct regmap m = {0};
    CHECK(regmap_open(&m, path, PL_BASE, 0x1000) == 0);
    CHECK(m.fd >= 0 && m.map != NULL && m.len == 0x1000 && m.phys == PL_BASE);
    CHECK((uintptr_t)m.map % pg == 0 && m.map_len == (0x1000 + pg - 1) / pg * pg);
    CHECK((uintptr_t)m.base == (uintptr_t)m.map);
    CHECK(reg_rd32(&m, REG_ID) == PGEN_ID);
    reg_wr32(&m, REG_SCRATCH, 0xDEADBEEFu);
    CHECK(dget(REG_SCRATCH) == 0xDEADBEEFu);
    dset(REG_SCRATCH, 0x12345678u);                      /* HW changes it behind our back */
    CHECK(reg_rd32(&m, REG_SCRATCH) == 0x12345678u);
    int oldfd = m.fd;
    regmap_close(&m);
    CHECK(m.fd == -1 && m.map == NULL && m.base == NULL);
    CHECK(oldfd > 2 && fcntl(oldfd, F_GETFD) == -1 && errno == EBADF);   /* really closed */
    regmap_close(&m);                                    /* double close is safe */

    /* 2. unaligned phys 0xA0010F00, len 0x200: in-page offset 0xF00, window spans a 4K page boundary */
    dset(0x1000, 0x600DCAFEu);                           /* phys 0xA0011000 = window off 0x100 */
    dset(0x1100, 0x5A5A5A5Au);                           /* phys 0xA0011100 = first byte past the window */
    CHECK(regmap_open(&m, path, PL_BASE + 0xF00, 0x200) == 0);
    CHECK((uintptr_t)m.base - (uintptr_t)m.map == 0xF00);
    CHECK(m.map_len == (0xF00 + 0x200 + pg - 1) / pg * pg);   /* 0x2000 on 4K pages */
    CHECK(reg_rd32(&m, 0x100) == 0x600DCAFEu);
    reg_wr32(&m, 0x1FC, 0xA1B2C3D4u);                    /* last valid register */
    CHECK(dget(0x10FC) == 0xA1B2C3D4u);

    /* 3. bounds contract: off + 4 > len or misaligned -> no access */
    CHECK(reg_rd32(&m, 0x200) == REG_BAD);               /* mapped slack, but outside len */
    reg_wr32(&m, 0x200, 0);
    CHECK(dget(0x1100) == 0x5A5A5A5Au);                  /* write was dropped */
    CHECK(reg_rd32(&m, 0x102) == REG_BAD);
    reg_wr32(&m, 0xFFFFFFFCu, 0);                        /* off + 4 wraps in 32 bits */
    CHECK(reg_rd32(NULL, 0) == REG_BAD);
    regmap_close(&m);
    CHECK(reg_rd32(&m, 0x100) == REG_BAD);               /* closed map */

    /* 4. argument validation; on failure the regmap is left closed */
    CHECK(regmap_open(&m, path, PL_BASE, 0) == -EINVAL);
    CHECK(m.fd == -1 && m.map == NULL);
    CHECK(regmap_open(&m, path, PL_BASE + 2, 0x100) == -EINVAL);
    CHECK(regmap_open(&m, path, PL_BASE, 6) == -EINVAL);
    CHECK(regmap_open(&m, path, UINT64_MAX - 3, 8) == -EINVAL);
    CHECK(regmap_open(&m, NULL, PL_BASE, 0x100) == -EINVAL);
    CHECK(regmap_open(NULL, path, PL_BASE, 0x100) == -EINVAL);
    CHECK(regmap_open(&m, "/nonexistent/l1/devmem", PL_BASE, 0x100) == -ENOENT);
    CHECK(m.fd == -1 && m.map == NULL && m.base == NULL);
    regmap_close(&m);

    /* 5. reg_poll */
    CHECK(regmap_open(&m, path, PL_BASE, 0x1000) == 0);
    dset(REG_STATUS, 0); dset(REG_CTRL, 0);
    CHECK(reg_poll(&m, REG_STATUS, ST_BUSY, 0, 0) == 0);              /* already true, timeout 0 */
    int64_t t0 = test_ms();
    CHECK(reg_poll(&m, REG_STATUS, ST_DONE, ST_DONE, 50) == -ETIMEDOUT);
    int64_t dt = test_ms() - t0;
    CHECK(dt >= 45 && dt < 1500);
    CHECK(reg_poll(&m, REG_STATUS, ST_DONE, ST_DONE | ST_BUSY, 10) == -EINVAL);  /* want outside mask */
    CHECK(reg_poll(&m, 0x1000, 1, 1, 10) == -EINVAL);                   /* off out of range */
    pthread_t th;
    atomic_store(&ip_quit, 0); atomic_store(&ip_starts, 0);
    pthread_create(&th, NULL, pl_ip_thread, NULL);
    reg_wr32(&m, REG_CTRL, CTRL_START);
    t0 = test_ms();
    CHECK(reg_poll(&m, REG_STATUS, ST_DONE, ST_DONE, 2000) == 0);
    dt = test_ms() - t0;
    atomic_store(&ip_quit, 1);
    pthread_join(th, NULL);
    CHECK(atomic_load(&ip_starts) == 1 && dt >= 15 && dt < 1500);
    CHECK(((reg_rd32(&m, REG_STATUS) >> 8) & 0xFFu) == 0x5Au);
    regmap_close(&m);

    /* 6. uio_wait_irq: unmask word, count, missed interrupts, wrap, stale count */
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    int ufd = sv[0], dfd = sv[1];
    uint32_t last = 0, got = 0;
    fire(dfd, 1);
    CHECK(uio_wait_irq(ufd, 1000, &last) == 0 && last == 1);
    CHECK(drain_unmask(dfd, &got) == 1 && got == 1);     /* exactly one 4-byte 1 */
    fire(dfd, 4);
    CHECK(uio_wait_irq(ufd, 1000, &last) == 2 && last == 4);   /* 1 -> 4: two missed */
    drain_unmask(dfd, &got);
    last = 0xFFFFFFFFu; fire(dfd, 1);
    CHECK(uio_wait_irq(ufd, 1000, &last) == 1 && last == 1);   /* wraps: 0xFFFFFFFF -> 0 -> 1 */
    drain_unmask(dfd, &got);
    last = 4; fire(dfd, 4);
    CHECK(uio_wait_irq(ufd, 1000, &last) == -EIO);       /* count did not advance */
    drain_unmask(dfd, &got);
    CHECK(uio_wait_irq(ufd, 100, NULL) == -EINVAL);

    /* 7. timeout, then a device that fires only after unmask (order matters) */
    last = 4; got = 0;
    t0 = test_ms();
    CHECK(uio_wait_irq(ufd, 50, &last) == -ETIMEDOUT && last == 4);
    dt = test_ms() - t0;
    CHECK(dt >= 45 && dt < 1500);
    CHECK(drain_unmask(dfd, &got) == 1 && got == 1);     /* still unmasked before waiting */
    UioDev ud = { dfd, 5, 0 };
    pthread_create(&th, NULL, uio_dev_thread, &ud);
    t0 = test_ms();
    int r = uio_wait_irq(ufd, 2000, &last);
    dt = test_ms() - t0;
    close(ufd);                                          /* lets the device thread exit if stuck */
    pthread_join(th, NULL);
    CHECK(r == 0 && last == 5 && ud.unmask_seen == 1 && dt >= 15);

    /* 8. errors from the fd itself */
    CHECK(uio_wait_irq(ufd, 10, &last) == -EBADF);       /* closed fd */
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    close(sv[1]);                                        /* device side gone */
    CHECK(uio_wait_irq(sv[0], 10, &last) == -EPIPE);
    close(sv[0]);
    close(dfd);

    munmap(dv, DEV_SPAN);
    close(hfd);
    unlink(path);
    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
