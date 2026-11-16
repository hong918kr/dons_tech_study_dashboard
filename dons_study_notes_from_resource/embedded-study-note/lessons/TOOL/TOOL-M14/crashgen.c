/* TOOL-M14 crash generator: a tiny "sensor packet + IRQ" pipeline that crashes on purpose
 * and prints a crash log like a field device would (raw PCs + where the image was loaded).
 *
 *   ./crashgen null    NULL payload  -> parse_record -> inlined rd_le16 faults
 *   ./crashgen panic   bad magic     -> check_header -> fw_panic (noreturn) faults
 *   ./crashgen isr     IRQ 3         -> dispatch_irq -> uart_rx_isr -> inlined rb_put faults
 *
 * Mac (Mach-O):  cc -g -O1 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer \
 *                   -fno-optimize-sibling-calls crashgen.c -o crashgen
 * Linux (ELF):   gcc -g -O1 ... -Wl,--build-id crashgen.c -o crashgen   (demo_vm.sh)
 * Lines tagged "@T <name>" are the ground truth that check.sh compares against.
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <sys/ucontext.h>
#else
#include <elf.h>
#include <link.h>
#include <ucontext.h>
#endif

struct packet { uint16_t magic; uint16_t len; const uint8_t *payload; };
struct ringbuf { uint8_t buf[16]; unsigned head; };
struct uart { struct ringbuf *rb; unsigned overruns; };

static volatile unsigned g_records, g_bytes;     /* side effects so calls are not removed */
static struct uart g_uart1;                       /* rb never set up: the bug for "isr" */

static inline __attribute__((always_inline)) uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));                         // @T rd16
}

static inline __attribute__((always_inline)) void rb_put(struct ringbuf *rb, uint8_t b)
{
    rb->buf[rb->head & 15u] = b;                                   // @T rbput
    rb->head++;
}

__attribute__((noreturn, noinline)) static void fw_panic(unsigned code)
{
    g_records = code;
    *(volatile uint32_t *)(uintptr_t)(0xDEAD0000u | code) = code;  // @T panicw
    __builtin_unreachable();
}

__attribute__((noinline)) static void check_header(const struct packet *pk)
{
    if (pk->magic != 0xA55A)
        fw_panic(0x0BAD);                                          // @T chkcall
    g_bytes += pk->len;
}

__attribute__((noinline)) static unsigned parse_record(const struct packet *pk)
{
    unsigned id = rd_le16(pk->payload);                            // @T parsecall
    return id + rd_le16(pk->payload + 2);
}

__attribute__((noinline)) static unsigned process_packet(const struct packet *pk)
{
    check_header(pk);                                              // @T ppchk
    unsigned id = parse_record(pk);                                // @T ppparse
    g_records++;
    return id;
}

__attribute__((noinline)) static void uart_rx_isr(void)
{
    uint8_t byte = (uint8_t)g_bytes;
    rb_put(g_uart1.rb, byte);                                      // @T isrput
    g_uart1.overruns += byte == 0;
}

typedef void (*irq_handler_t)(void);
static irq_handler_t const g_irq_table[4] = { 0, 0, 0, uart_rx_isr };

__attribute__((noinline)) static void dispatch_irq(unsigned irq)
{
    g_irq_table[irq & 3u]();                                       // @T dispcall
    g_records += irq;
}

/* ---------------- crash log writer (async-signal-safe: write(2) + hand-made hex) ---------- */
static char g_hdr[1024];          /* image facts, prepared in main() before anything can crash */
static size_t g_hdr_len;
static uintptr_t g_text_lo, g_text_hi;

static void put(const char *s) { (void)!write(1, s, strlen(s)); }
static void put_hex(uint64_t v)
{
    char b[19] = "0x";
    for (int i = 0; i < 16; i++) b[2 + i] = "0123456789abcdef"[(v >> (60 - 4 * i)) & 15];
    b[18] = 0;
    put(b);
}
static void put_dec(unsigned v)
{
    char b[12]; int i = 11; b[i] = 0;
    do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v && i > 0);
    put(b + i);
}

static uint64_t strip_pac(uint64_t a) { return a & 0x0000ffffffffffffULL; }  /* 48-bit VA: drop PAC bits */

static void on_crash(int sig, siginfo_t *si, void *ctx)
{
    uint64_t pc, lr, fp, sp;
#ifdef __APPLE__
    ucontext_t *uc = ctx;
    pc = uc->uc_mcontext->__ss.__pc; lr = uc->uc_mcontext->__ss.__lr;
    fp = uc->uc_mcontext->__ss.__fp; sp = uc->uc_mcontext->__ss.__sp;
#else
    ucontext_t *uc = ctx;
    pc = uc->uc_mcontext.pc; lr = uc->uc_mcontext.regs[30];
    fp = uc->uc_mcontext.regs[29]; sp = uc->uc_mcontext.sp;
#endif
    put("signal:       ");
    put(sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGILL ? "SIGILL" : "SIGTRAP");
    put(" ("); put_dec((unsigned)sig); put(") fault_addr "); put_hex((uint64_t)(uintptr_t)si->si_addr); put("\n");
    (void)!write(1, g_hdr, g_hdr_len);
    put("registers:    pc "); put_hex(pc); put("  lr "); put_hex(strip_pac(lr));
    put("  fp "); put_hex(fp); put("  sp "); put_hex(sp); put("\n");
    put("backtrace:\n");
    /* frame 0 = the faulting pc; frames 1.. = return addresses from the frame-record chain
       [fp] = caller's fp, [fp+8] = saved lr.  Every function here builds a frame record. */
    uint64_t frames[32]; unsigned n = 0;
    frames[n++] = pc;
    for (uint64_t f = fp; f && n < 32 && (f & 7) == 0 && f >= sp; ) {
        const uint64_t *rec = (const uint64_t *)(uintptr_t)f;
        uint64_t ret = strip_pac(rec[1]);
        if (!ret) break;
        frames[n++] = ret;
        if (rec[0] <= f) break;           /* the stack grows down: caller frames are higher */
        f = rec[0];
    }
    for (unsigned i = 0; i < n; i++) {
        put("  "); put_dec(i); put(i < 10 ? "   " : "  "); put_hex(frames[i]);
        put(frames[i] >= g_text_lo && frames[i] < g_text_hi ? "  crashgen\n" : "  ?\n");
    }
    put("end\n");
    _exit(3);                              /* handled: no system crash report is written */
}

static void hdr_add(const char *s)
{
    size_t n = strlen(s);
    if (g_hdr_len + n < sizeof g_hdr) { memcpy(g_hdr + g_hdr_len, s, n); g_hdr_len += n; }
}

#ifdef __APPLE__
static void prepare_image_info(void)
{
    const struct mach_header_64 *mh = (const struct mach_header_64 *)_dyld_get_image_header(0);
    intptr_t slide = _dyld_get_image_vmaddr_slide(0);
    const struct load_command *lc = (const struct load_command *)(mh + 1);
    uint64_t vmaddr = 0, vmsize = 0; const uint8_t *u = NULL;
    for (uint32_t i = 0; i < mh->ncmds; i++, lc = (const void *)((const char *)lc + lc->cmdsize)) {
        if (lc->cmd == LC_SEGMENT_64 && !strcmp(((const struct segment_command_64 *)lc)->segname, "__TEXT")) {
            vmaddr = ((const struct segment_command_64 *)lc)->vmaddr;
            vmsize = ((const struct segment_command_64 *)lc)->vmsize;
        }
        if (lc->cmd == LC_UUID) u = ((const struct uuid_command *)lc)->uuid;
    }
    char line[160], uuid[40] = "?";
    if (u) snprintf(uuid, sizeof uuid, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                    u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
    g_text_lo = (uintptr_t)mh; g_text_hi = g_text_lo + vmsize;
    snprintf(line, sizeof line, "image:        crashgen arm64 UUID %s\n", uuid); hdr_add(line);
    snprintf(line, sizeof line, "text_vmaddr:  0x%016llx\n", (unsigned long long)vmaddr); hdr_add(line);
    snprintf(line, sizeof line, "load_address: 0x%016llx\n", (unsigned long long)(uintptr_t)mh); hdr_add(line);
    snprintf(line, sizeof line, "slide:        0x%016llx\n", (unsigned long long)slide); hdr_add(line);
    snprintf(line, sizeof line, "text_end:     0x%016llx\n", (unsigned long long)g_text_hi); hdr_add(line);
}
#else
static int first_object(struct dl_phdr_info *info, size_t size, void *arg)
{
    (void)size; (void)arg;
    char line[200], id[80] = "?";
    uint64_t hi = 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type == PT_LOAD && (ph->p_flags & PF_X) && ph->p_vaddr + ph->p_memsz > hi) hi = ph->p_vaddr + ph->p_memsz;
        if (ph->p_type != PT_NOTE) continue;
        const char *p = (const char *)(info->dlpi_addr + ph->p_vaddr), *end = p + ph->p_memsz;
        while (p + sizeof(ElfW(Nhdr)) <= end) {
            const ElfW(Nhdr) *nh = (const ElfW(Nhdr) *)p;
            const unsigned char *desc = (const unsigned char *)(p + sizeof *nh + ((nh->n_namesz + 3) & ~3u));
            if (nh->n_type == NT_GNU_BUILD_ID && nh->n_descsz <= 32) {
                for (unsigned k = 0; k < nh->n_descsz; k++) snprintf(id + 2 * k, 3, "%02x", desc[k]);
            }
            p = (const char *)desc + ((nh->n_descsz + 3) & ~3u);
        }
    }
    g_text_lo = info->dlpi_addr; g_text_hi = info->dlpi_addr + hi;
    snprintf(line, sizeof line, "image:        crashgen aarch64 BUILD-ID %s\n", id); hdr_add(line);
    snprintf(line, sizeof line, "text_vmaddr:  0x%016llx\n", 0ULL); hdr_add(line);
    snprintf(line, sizeof line, "load_address: 0x%016llx\n", (unsigned long long)info->dlpi_addr); hdr_add(line);
    snprintf(line, sizeof line, "slide:        0x%016llx\n", (unsigned long long)info->dlpi_addr); hdr_add(line);
    snprintf(line, sizeof line, "text_end:     0x%016llx\n", (unsigned long long)g_text_hi); hdr_add(line);
    return 1;                                  /* the first object is the main program */
}
static void prepare_image_info(void) { dl_iterate_phdr(first_object, NULL); }
#endif

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "null";
#ifdef FW_V2
    setvbuf(stdout, NULL, _IOLBF, 0);       /* "v2": one extra call moves every function after main */
#endif
    prepare_image_info();
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);  sigaction(SIGTRAP, &sa, NULL);
    printf("TOOL-M14 crash report\nprocess:      crashgen [pid %d] mode %s\n", (int)getpid(), mode);
    fflush(stdout);

    static const uint8_t good[4] = { 0x34, 0x12, 0x02, 0x00 };
    struct packet ok = { 0xA55A, 4, good }, nullp = { 0xA55A, 4, NULL }, badm = { 0x1234, 4, good };
    unsigned r = process_packet(&ok);                              // warm-up: no crash
    if (!strcmp(mode, "null"))  r += process_packet(&nullp);       // @T mainnull
    if (!strcmp(mode, "panic")) r += process_packet(&badm);        // @T mainpanic
    if (!strcmp(mode, "isr"))   dispatch_irq(3);                   // @T mainisr
    printf("no crash (r=%u)\n", r);
    return 0;
}
