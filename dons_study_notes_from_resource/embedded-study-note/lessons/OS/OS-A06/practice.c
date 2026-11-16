/* OS-A06 추가 연습 — 한 번에 검증
 *
 *   연습 1  지연 복원 (post-copy / CRIU lazy-pages 흉내): restore 는 페이지를 PROT_NONE 으로만 돌려 두고
 *           실제 내용은 처음 만질 때 pagein 이 스냅숏 저장소에서 가져온다.
 *   연습 2  참조 비트 흉내 (OSTEP 23장 Babaoglu & Joy): 상주 페이지를 잠깐 PROT_NONE 으로 "덫" 을 놓고
 *           구간 동안 만져진 페이지 집합 = 워킹셋을 정확히 잰다. 원래 보호(READ/RW)는 그대로 복귀.
 *   연습 3  쓰기 전 사본 스냅숏 (pre-image, COW 식): snap_begin 순간의 내용을 멈추지 않고 보존한다.
 *           첫 쓰기 폴트에서만 원래 페이지를 미리 잡아 둔 풀로 복사 → 사본 수 == 쓴 페이지 수.
 *   연습 4  sigaltstack 으로 스택 오버플로 잡기 (자식 프로세스): 대체 스택이 없으면 핸들러를 못 돌리고 죽는다.
 *   연습 5  이전 핸들러로 넘기기 (libsigsegv 식 chaining): 내 영역이 아니면 먼저 깔려 있던 핸들러를 부른다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include <errno.h>
#include <poll.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static size_t PG;
enum { NP = 32 };
enum { PS_NONE = 0, PS_READ = 1, PS_RW = 2, PS_TRAP = 0x80 };   /* TRAP: 내용은 유효, 참조 감지용 PROT_NONE */

typedef void (*pagein_fn)(size_t idx, unsigned char *dst, size_t pg);
typedef struct {
    unsigned char *base; size_t pg, npages, len;
    volatile unsigned char *state;
    pagein_fn pagein;
    volatile unsigned long n_read, n_upgrade, n_pagein, n_ref, n_cow;
    unsigned char *ref;                         /* 연습 2: 이번 구간에 참조됨 */
    unsigned char *cow_pool, *cow_has;          /* 연습 3: 미리 잡은 사본 풀 (핸들러에서 malloc 금지) */
    volatile int cow_active;
} vmr_t;
typedef struct { unsigned epoch; size_t page; unsigned char *data; } snap_ent_t;
typedef struct { snap_ent_t *e; size_t n, cap; unsigned epoch; size_t pg; } snapstore_t;

static int prot_of(int st) { return st == PS_RW ? PROT_READ | PROT_WRITE : st == PS_READ ? PROT_READ : PROT_NONE; }
static unsigned char init_byte(size_t page, size_t off) { return (unsigned char)(page * 31u + off * 7u + 1u); }
static void pagein_pattern(size_t idx, unsigned char *dst, size_t pg) { for (size_t o = 0; o < pg; o++) dst[o] = init_byte(idx, o); }
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static int vmr_init(vmr_t *r, size_t npages, pagein_fn pagein) {
    memset(r, 0, sizeof *r);
    r->pg = PG; r->npages = npages; r->len = npages * PG; r->pagein = pagein;
    void *p = mmap(NULL, r->len, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) return -1;
    r->base = p;
    r->state = calloc(npages, 1);
    r->ref = calloc(npages, 1);
    return r->state && r->ref ? 0 : -1;
}
static void vmr_destroy(vmr_t *r) {
    if (r->base) munmap(r->base, r->len);
    free((void *)r->state); free(r->ref); free(r->cow_pool); free(r->cow_has);
    memset(r, 0, sizeof *r);
}
static int snap_put(snapstore_t *s, unsigned epoch, size_t page, const void *src) {
    if (s->n == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 64;
        snap_ent_t *ne = realloc(s->e, nc * sizeof *ne);
        if (!ne) return -1;
        s->e = ne; s->cap = nc;
    }
    unsigned char *d = malloc(s->pg);
    if (!d) return -1;
    memcpy(d, src, s->pg);
    s->e[s->n++] = (snap_ent_t){ epoch, page, d };
    return 0;
}
static void snap_free(snapstore_t *s) { for (size_t i = 0; i < s->n; i++) free(s->e[i].data); free(s->e); memset(s, 0, sizeof *s); }

/* 핸들러 본체: 본문 A2 + 덫(연습 2) + 쓰기 전 사본(연습 3) */
static bool vmr_handle_fault(vmr_t *r, void *addr) {
    if (!r || !r->base) return false;
    uintptr_t a = (uintptr_t)addr, b = (uintptr_t)r->base;
    if (a < b || a >= b + r->len) return false;
    size_t i = (a - b) / r->pg;
    unsigned char *pa = r->base + i * r->pg;
    unsigned char st = r->state[i];
    if (st & PS_TRAP) {                                     /* 연습 2: 참조됨 — 원래 보호로 */
        st &= (unsigned char)~PS_TRAP;
        if (mprotect(pa, r->pg, prot_of(st))) return false;
        r->state[i] = st; r->ref[i] = 1; r->n_ref++;
        return true;
    }
    if (st == PS_NONE) {
        if (r->pagein) {
            if (mprotect(pa, r->pg, PROT_READ | PROT_WRITE)) return false;
            r->pagein(i, pa, r->pg);
            r->n_pagein++;
        }
        if (mprotect(pa, r->pg, PROT_READ)) return false;
        r->state[i] = PS_READ; r->n_read++;
        return true;
    }
    if (st == PS_READ) {
        if (r->cow_active && !r->cow_has[i]) {              /* 연습 3: 쓰기 직전 원래 내용을 보존 */
            memcpy(r->cow_pool + i * r->pg, pa, r->pg);
            r->cow_has[i] = 1; r->n_cow++;
        }
        if (mprotect(pa, r->pg, PROT_READ | PROT_WRITE)) return false;
        r->state[i] = PS_RW; r->n_upgrade++;
        return true;
    }
    return false;
}

static long vmr_checkpoint(vmr_t *r, snapstore_t *s) {
    unsigned e = s->epoch + 1;
    long saved = 0;
    for (size_t i = 0; i < r->npages; i++) {
        if (r->state[i] != PS_RW) continue;
        unsigned char *pa = r->base + i * r->pg;
        if (snap_put(s, e, i, pa) || mprotect(pa, r->pg, PROT_READ)) return -1;
        r->state[i] = PS_READ; saved++;
    }
    s->epoch = e;
    return saved;
}

/* ---------- 연습 1: 지연 복원 ---------- */
static const snapstore_t *g_src;                /* pagein 이 읽을 저장소와 epoch */
static unsigned g_src_epoch;
static void pagein_from_store(size_t idx, unsigned char *dst, size_t pg) {
    for (size_t k = g_src->n; k-- > 0;)          /* 핸들러 안: 검색과 memcpy 만, 할당 없음 */
        if (g_src->e[k].page == idx && g_src->e[k].epoch <= g_src_epoch) { memcpy(dst, g_src->e[k].data, pg); return; }
    pagein_pattern(idx, dst, pg);
}
static int vmr_restore_lazy(vmr_t *r, snapstore_t *s, unsigned e) {
    if (e > s->epoch) return -1;
    size_t keep = 0;
    while (keep < s->n && s->e[keep].epoch <= e) keep++;
    for (size_t k = keep; k < s->n; k++) free(s->e[k].data);
    s->n = keep; s->epoch = e;
    for (size_t i = 0; i < r->npages; i++) {
        if (r->state[i] == PS_NONE) continue;
        if (mprotect(r->base + i * r->pg, r->pg, PROT_NONE)) return -1;   /* 내용은 아직 옛것 — 아무도 못 본다 */
        r->state[i] = PS_NONE;
    }
    g_src = s; g_src_epoch = e;
    r->pagein = pagein_from_store;
    return 0;
}

/* ---------- 연습 2: 참조 비트 흉내 ---------- */
static void ws_begin(vmr_t *r) {
    memset(r->ref, 0, r->npages);
    for (size_t i = 0; i < r->npages; i++) {
        if (r->state[i] == PS_NONE || (r->state[i] & PS_TRAP)) continue;
        mprotect(r->base + i * r->pg, r->pg, PROT_NONE);
        r->state[i] |= PS_TRAP;
    }
}
static size_t ws_end(vmr_t *r, unsigned char *out) {
    size_t n = 0;
    for (size_t i = 0; i < r->npages; i++) { out[i] = r->ref[i]; n += r->ref[i]; }
    return n;
}

/* ---------- 연습 3: 쓰기 전 사본 스냅숏 ---------- */
static int snap_begin(vmr_t *r) {
    if (!r->cow_pool) { r->cow_pool = malloc(r->len); r->cow_has = calloc(r->npages, 1); }
    if (!r->cow_pool || !r->cow_has) return -1;
    memset(r->cow_has, 0, r->npages);
    r->n_cow = 0;
    for (size_t i = 0; i < r->npages; i++)
        if (r->state[i] == PS_RW) { if (mprotect(r->base + i * r->pg, r->pg, PROT_READ)) return -1; r->state[i] = PS_READ; }
    r->cow_active = 1;
    return 0;
}
static const unsigned char *snap_page(const vmr_t *r, size_t i) {      /* 스냅숏 시점의 i 번 페이지 */
    return r->cow_has[i] ? r->cow_pool + i * r->pg : r->base + i * r->pg;
}

/* ---------- 하네스: 시그널 진입점 + 연습 5 체이닝 ---------- */
static vmr_t *volatile g_vmr;
static sigjmp_buf g_jb;
static volatile sig_atomic_t g_armed, g_same;
static void *volatile g_last;
static struct sigaction g_old_segv, g_old_bus;

static void chain(int sig, siginfo_t *si, void *uc) {                  /* 연습 5 */
    struct sigaction *o = sig == SIGBUS ? &g_old_bus : &g_old_segv;
    if (o->sa_flags & SA_SIGINFO) { if (o->sa_sigaction) { o->sa_sigaction(sig, si, uc); return; } }
    else if (o->sa_handler != SIG_DFL && o->sa_handler != SIG_IGN) { o->sa_handler(sig); return; }
    struct sigaction d;
    memset(&d, 0, sizeof d);
    d.sa_handler = SIG_DFL;
    sigemptyset(&d.sa_mask);
    sigaction(sig, &d, NULL);
    raise(sig);
}
static void on_fault(int sig, siginfo_t *si, void *uc) {
    int e = errno;
    if (si->si_addr == g_last) g_same++; else { g_same = 0; g_last = si->si_addr; }
    if (g_same < 4 && vmr_handle_fault(g_vmr, si->si_addr)) { errno = e; return; }
    g_same = 0; g_last = NULL;
    if (g_armed) { g_armed = 0; siglongjmp(g_jb, 1); }
    chain(sig, si, uc);
    errno = e;
}
static void install(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_fault; sa.sa_flags = SA_SIGINFO; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g_old_segv);
    sigaction(SIGBUS, &sa, &g_old_bus);
}
static void uninstall(void) { sigaction(SIGSEGV, &g_old_segv, NULL); sigaction(SIGBUS, &g_old_bus, NULL); }
static int guarded(void (*fn)(void *), void *arg) {
    g_last = NULL; g_same = 0;
    if (sigsetjmp(g_jb, 1)) return 0;
    g_armed = 1; fn(arg); g_armed = 0;
    return 1;
}
static int wait_child(pid_t pid, int ms) {
    int st;
    for (int t = 0; t < ms; t++) {
        pid_t w = waitpid(pid, &st, WNOHANG);
        if (w == pid) return st;
        if (w < 0 && errno != EINTR) return -1;
        struct timespec one = { 0, 1000000 };
        nanosleep(&one, NULL);
    }
    kill(pid, SIGKILL); waitpid(pid, &st, 0);
    return -1;
}

/* ---------- 작업들 ---------- */
typedef struct { vmr_t *r; uint32_t seed; int nops; unsigned char *written; } rand_job_t;
static void rand_job(void *a) {
    rand_job_t *j = a;
    for (int k = 0; k < j->nops; k++) {
        uint32_t x = xs32(&j->seed);
        size_t p = x % NP, off = (x >> 8) % PG;
        if (k % 3 == 0) (void)((volatile unsigned char *)j->r->base)[p * PG + off];
        else { ((volatile unsigned char *)j->r->base)[p * PG + off] = (unsigned char)(x >> 24); j->written[p] = 1; }
    }
}
typedef struct { vmr_t *r; unsigned char *img; int equal; } cmp_job_t;
static void cmp_job(void *a) { cmp_job_t *j = a; j->equal = memcmp(j->r->base, j->img, NP * PG) == 0; }
static void copy_job(void *a) { cmp_job_t *j = a; memcpy(j->img, j->r->base, NP * PG); }
typedef struct { vmr_t *r; const size_t *pages; int n; int write; } list_job_t;
static void list_job(void *a) {
    list_job_t *j = a;
    for (int k = 0; k < j->n; k++) {
        volatile unsigned char *p = (volatile unsigned char *)j->r->base + j->pages[k] * PG + 300;
        if (j->write) *p = (unsigned char)(*p + 1); else (void)*p;
    }
}

/* 연습 4: 깊은 재귀 (꼬리 호출·누적기 변환을 막으려고 버퍼 주소를 넘긴다) */
static volatile int g_depth, g_depth_limit = 1 << 30;   /* 한계는 사실상 무한 — 경고(-Winfinite-recursion) 회피용 */
__attribute__((noinline)) static int deep(int n, volatile char *prev) {
    volatile char buf[512];
    if (n > g_depth_limit) return 0;
    buf[0] = (char)n; buf[511] = prev ? prev[0] : 0;
    g_depth = n;
    return deep(n + 1, buf) + buf[511];
}
static int g_ovf_fd = -1;
static void on_overflow(int sig, siginfo_t *si, void *uc) {
    (void)si; (void)uc;
    int d[2] = { sig, g_depth };
    (void)!write(g_ovf_fd, d, sizeof d);
    _exit(42);
}
static void overflow_child(int use_alt, int fd) {
    alarm(5);
    g_ovf_fd = fd;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_overflow; sa.sa_flags = SA_SIGINFO | (use_alt ? SA_ONSTACK : 0);
    sigemptyset(&sa.sa_mask);
    if (use_alt) {
        stack_t ss = { .ss_sp = malloc(65536), .ss_size = 65536, .ss_flags = 0 };
        if (!ss.ss_sp || sigaltstack(&ss, NULL)) _exit(3);
    }
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    _exit(deep(0, NULL) & 1);
}

/* 연습 5: 먼저 깔린 "바깥" 핸들러 */
static sigjmp_buf g_outer_jb;
static volatile sig_atomic_t g_outer_hits;
static void *volatile g_outer_addr;
static void outer_handler(int sig, siginfo_t *si, void *uc) {
    (void)sig; (void)uc;
    g_outer_hits++; g_outer_addr = si->si_addr;
    siglongjmp(g_outer_jb, 1);
}

int main(void) {
    alarm(60);
    PG = (size_t)sysconf(_SC_PAGESIZE);
    install();

    /* ===== 연습 1: 지연 복원 ===== */
    {
        vmr_t r; snapstore_t s = { .pg = PG };
        unsigned char *shadow[5];
        CHECK(vmr_init(&r, NP, pagein_pattern) == 0);
        g_vmr = &r;
        int ok = 1;
        shadow[0] = malloc(NP * PG);
        for (size_t p = 0; p < NP; p++) pagein_pattern(p, shadow[0] + p * PG, PG);
        for (unsigned e = 1; e <= 4; e++) {
            unsigned char w[NP] = { 0 };
            rand_job_t rj = { &r, 99u + e, 24, w };
            ok &= guarded(rand_job, &rj) && vmr_checkpoint(&r, &s) >= 0;
            shadow[e] = malloc(NP * PG);
            cmp_job_t cj = { &r, shadow[e], 0 };
            ok &= guarded(copy_job, &cj);
        }
        CHECK(ok && s.epoch == 4);
        CHECK(vmr_restore_lazy(&r, &s, 2) == 0 && r.state[0] == PS_NONE);
        unsigned long pi0 = r.n_pagein;
        static const size_t three[] = { 1, 7, 30 };
        list_job_t lj = { &r, three, 3, 0 };
        ok = guarded(list_job, &lj);
        CHECK(ok && r.n_pagein - pi0 == 3);                             /* 만진 3 페이지만 가져왔다 */
        CHECK(memcmp(r.base + 7 * PG, shadow[2] + 7 * PG, PG) == 0);
        cmp_job_t cj = { &r, shadow[2], 0 };
        CHECK(guarded(cmp_job, &cj) && cj.equal && r.n_pagein - pi0 == NP);   /* 전체 바이트 일치 */
        unsigned char w[NP] = { 0 };
        rand_job_t rj = { &r, 555u, 12, w };
        long n = guarded(rand_job, &rj) ? vmr_checkpoint(&r, &s) : -1;
        long nw = 0;
        for (int p = 0; p < NP; p++) nw += w[p];
        CHECK(n == nw && s.epoch == 3);                                  /* 복원 뒤에도 dirty 추적이 맞다 */
        printf("  (ex1: lazy restore to epoch 2, first 3 touches -> 3 pageins, full compare -> %lu pageins)\n", r.n_pagein - pi0);
        for (int e = 0; e <= 4; e++) free(shadow[e]);
        snap_free(&s);
        g_vmr = NULL; vmr_destroy(&r);
    }

    /* ===== 연습 2: 워킹셋 ===== */
    {
        vmr_t r;
        CHECK(vmr_init(&r, NP, NULL) == 0);
        g_vmr = &r;
        unsigned char w[NP] = { 0 };
        rand_job_t rj = { &r, 2024u, 120, w };
        int ok = guarded(rand_job, &rj);                                 /* 많은 페이지를 상주시킨다 */
        unsigned char before[NP];
        for (int p = 0; p < NP; p++) before[p] = r.state[p];
        ws_begin(&r);
        static const size_t S[] = { 2, 5, 6, 11, 17 };
        int all_resident = 1;
        for (int k = 0; k < 5; k++) all_resident &= before[S[k]] != PS_NONE;
        list_job_t rd = { &r, S, 3, 0 }, wr = { &r, S + 3, 2, 1 };    /* 3 개 읽고 2 개 쓰기 */
        ok &= guarded(list_job, &rd) && guarded(list_job, &wr);
        unsigned char got[NP], want[NP] = { 0 };
        for (int k = 0; k < 5; k++) want[S[k]] = 1;
        size_t n = ws_end(&r, got);
        CHECK(ok && all_resident && n == 5 && memcmp(got, want, NP) == 0);
        int prot_ok = 1;                                                 /* 안 만진 상주 페이지는 아직 덫, 만진 건 원래 상태 */
        for (int p = 0; p < NP; p++) {
            if (before[p] == PS_NONE) prot_ok &= r.state[p] == PS_NONE;
            else if (want[p]) prot_ok &= r.state[p] == (p == 11 || p == 17 ? PS_RW : before[p]);
            else prot_ok &= r.state[p] == (before[p] | PS_TRAP);
        }
        CHECK(prot_ok);
        int resident = 0;
        for (int p = 0; p < NP; p++) resident += before[p] != PS_NONE;
        printf("  (ex2: resident %d pages, working set in interval = %zu pages)\n", resident, n);
        g_vmr = NULL; vmr_destroy(&r);
    }

    /* ===== 연습 3: 쓰기 전 사본 스냅숏 ===== */
    {
        vmr_t r;
        CHECK(vmr_init(&r, NP, pagein_pattern) == 0);
        g_vmr = &r;
        unsigned char w0[NP] = { 0 };
        rand_job_t rj0 = { &r, 31337u, 60, w0 };
        int ok = guarded(rand_job, &rj0);
        unsigned char *img = malloc(NP * PG);
        cmp_job_t cj = { &r, img, 0 };
        ok &= guarded(copy_job, &cj);                                    /* 기준: 스냅숏 순간의 전체 사본 */
        ok &= snap_begin(&r) == 0;
        unsigned char w[NP] = { 0 };
        rand_job_t rj = { &r, 4711u, 45, w };
        ok &= guarded(rand_job, &rj);                                    /* 스냅숏 뒤에도 계속 쓴다 */
        long nw = 0;
        for (int p = 0; p < NP; p++) nw += w[p];
        int same = 1;
        for (size_t p = 0; p < NP; p++) same &= memcmp(snap_page(&r, p), img + p * PG, PG) == 0;
        CHECK(ok && same);                                               /* 스냅숏 = 그 순간의 내용 */
        CHECK((long)r.n_cow == nw);                                      /* 복사는 쓴 페이지만 */
        printf("  (ex3: snapshot kept by copying %lu of %d pages on first write)\n", r.n_cow, NP);
        free(img);
        g_vmr = NULL; vmr_destroy(&r);
    }

    /* ===== 연습 4: sigaltstack 스택 오버플로 ===== */
    for (int alt = 0; alt < 2; alt++) {
        int p[2];
        if (pipe(p)) { CHECK(!"pipe"); continue; }
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) { close(p[0]); uninstall(); overflow_child(alt, p[1]); }
        close(p[1]);
        int st = wait_child(pid, 3000);
        int msg[2] = { 0, 0 };
        struct pollfd pf = { p[0], POLLIN, 0 };
        if (poll(&pf, 1, 0) == 1 && read(p[0], msg, sizeof msg) != (ssize_t)sizeof msg) msg[0] = 0;
        close(p[0]);
        printf("  (ex4: altstack=%d -> %s %d, handler saw sig %d at depth %d)\n", alt,
               st != -1 && WIFEXITED(st) ? "exit" : "signal", st == -1 ? -1 : WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st),
               msg[0], msg[1]);
        if (alt) CHECK(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 42 && msg[1] > 1000);
        else CHECK(st != -1 && WIFSIGNALED(st) && (WTERMSIG(st) == SIGSEGV || WTERMSIG(st) == SIGBUS) && msg[0] == 0);
    }

    /* ===== 연습 5: 이전 핸들러로 넘기기 ===== */
    {
        uninstall();
        struct sigaction o;
        memset(&o, 0, sizeof o);
        o.sa_sigaction = outer_handler; o.sa_flags = SA_SIGINFO; sigemptyset(&o.sa_mask);
        sigaction(SIGSEGV, &o, NULL); sigaction(SIGBUS, &o, NULL);
        install();                                                       /* 바깥 핸들러를 g_old_* 에 저장 */
        vmr_t r;
        CHECK(vmr_init(&r, 4, NULL) == 0);
        g_vmr = &r;
        unsigned char *other = mmap(NULL, PG, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        ((volatile unsigned char *)r.base)[3] = 9;                       /* 내 영역: 내가 처리 */
        if (sigsetjmp(g_outer_jb, 1) == 0) (void)((volatile unsigned char *)other)[17];
        CHECK(g_outer_hits == 1 && g_outer_addr == other + 17 && r.n_read == 1 && r.n_upgrade == 1);
        munmap(other, PG);
        g_vmr = NULL; vmr_destroy(&r);
        uninstall();
    }

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
