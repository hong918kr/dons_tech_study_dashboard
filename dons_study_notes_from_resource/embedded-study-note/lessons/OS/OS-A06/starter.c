/* OS-A06 사용자 공간 페이지 폴트 처리: mprotect + SIGSEGV
 *
 *   Part A  지연 할당(demand paging 흉내): PROT_NONE 으로 예약한 영역을 처음 만지면 핸들러가
 *           페이지를 "가져와"(pagein) PROT_READ 로 열고, 처음 쓰면 PROT_READ|PROT_WRITE 로 올린다.
 *           TODO A1 vmr_init, A2 vmr_handle_fault, A3 pass_to_default (내 영역이 아니면 기본 동작으로).
 *   Part B  쓰기 추적 + 증분 체크포인트: RW 페이지 = dirty. checkpoint 는 dirty 페이지만 저장하고
 *           다시 READ 로 보호, restore(e) 는 epoch e 의 내용을 재구성한다.
 *           TODO B1 vmr_checkpoint, B2 vmr_restore.
 *   Part C  arm64 ESR 로 읽기/쓰기 구분: TODO C1 esr_decode. ESR 모드면 "쓰기 첫 접근" 을 폴트 1 번에 RW 로.
 *
 * 하네스 규칙
 *   - 영역을 만지는 테스트 코드는 전부 guarded() 안에서 돈다. 핸들러가 "내 것 아님"(false)을 돌려주면
 *     siglongjmp 로 빠져나와 FAIL 로 센다 → 시작 코드가 멈추거나 죽지 않는다.
 *   - 같은 주소에서 폴트가 4 번 넘게 이어지면(핸들러가 고치지 못함) 역시 빠져나온다.
 *   - "남의 폴트는 원래 신호로 죽는다" 는 fork 한 자식에서만 시험한다 (wait_child 가 1.5 초 뒤 SIGKILL).
 *   - 페이지 크기는 sysconf(_SC_PAGESIZE) — 이 맥(Apple M6)은 16384, 리눅스 VM 은 4096.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _DARWIN_C_SOURCE        /* macOS: MAP_ANON, ucontext_t 의 __es (ESR/FAR) */
#define _DEFAULT_SOURCE         /* Linux glibc: MAP_ANONYMOUS 등 */
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

static size_t PG;                               /* main 에서 sysconf 로 채운다 */

enum { PS_NONE = 0, PS_READ = 1, PS_RW = 2 };   /* 페이지 상태 = 지금 걸어 둔 보호 */
typedef void (*pagein_fn)(size_t idx, unsigned char *dst, size_t pg);

typedef struct {
    unsigned char *base;                /* mmap 결과 (페이지 정렬) */
    size_t pg, npages, len;
    volatile unsigned char *state;      /* 페이지마다 PS_* */
    pagein_fn pagein;                   /* NULL 이면 demand-zero (커널이 준 0 페이지 그대로) */
    int use_esr;                        /* 1: ESR WnR 이 쓰기라고 하면 NONE → RW 를 폴트 1 번에 */
    volatile unsigned long n_read;      /* NONE → READ  (첫 접근) */
    volatile unsigned long n_upgrade;   /* READ → RW    (첫 쓰기) */
    volatile unsigned long n_direct;    /* NONE → RW    (ESR 모드, 첫 접근이 쓰기) */
    volatile unsigned long n_pagein;    /* pagein 호출 수 */
} vmr_t;

typedef struct {
    void *addr; int sig, code;          /* siginfo 에서 */
    int esr_valid; uint32_t esr;        /* arm64 macOS: uc_mcontext->__es.__esr */
} fault_t;

/* 스냅숏 저장소: (epoch, page, 복사본) 을 epoch 순서로 덧붙인다 */
typedef struct { unsigned epoch; size_t page; unsigned char *data; } snap_ent_t;
typedef struct { snap_ent_t *e; size_t n, cap; unsigned epoch; size_t pg; } snapstore_t;

enum { ESR_OTHER = 0, ESR_TRANSLATION = 1, ESR_PERMISSION = 2, ESR_ACCESS_FLAG = 3, ESR_DABT_OTHER = 4 };

static void pass_to_default(const fault_t *f);

/* ---------------- 하네스 제공 도우미 ---------------- */

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
static void snap_free(snapstore_t *s) {
    for (size_t i = 0; i < s->n; i++) free(s->e[i].data);
    free(s->e);
    memset(s, 0, sizeof *s);
}
static void vmr_destroy(vmr_t *r) {
    if (r->base) munmap(r->base, r->len);
    free((void *)r->state);
    memset(r, 0, sizeof *r);
}

/* ---------------- TODO 구역 ---------------- */

/* A1: npages 페이지를 PROT_NONE 으로 예약하고 상태 배열을 PS_NONE 으로. 성공 0, 실패 -1. */
static int vmr_init(vmr_t *r, size_t npages, pagein_fn pagein, int use_esr) {
    /* TODO A1: mmap(PROT_NONE, MAP_PRIVATE|MAP_ANON) 로 예약, state 를 calloc, 필드 채우기 */
    (void)npages; (void)pagein; (void)use_esr;
    memset(r, 0, sizeof *r);
    return -1;
}

/* C1: arm64 ESR_ELx 를 해석한다. EC(31:26) == 0x24 (하위 EL 데이터 어보트) 가 아니면 ESR_OTHER.
 *     DFSC(5:0): 0x04-0x07 변환, 0x08-0x0B 접근 플래그, 0x0C-0x0F 권한, 그 밖 ESR_DABT_OTHER.
 *     *is_write = WnR(bit 6) && !CM(bit 8)  — 캐시 유지보수 명령은 WnR=1 이어도 쓰기가 아니다. */
static int esr_decode(uint32_t esr, int *is_write) {
    /* TODO C1: EC = esr >> 26, WnR = bit 6, CM = bit 8, DFSC = esr & 0x3f */
    (void)esr;
    *is_write = 0;
    return ESR_OTHER;
}

/* A2: 시그널 핸들러 안에서 불린다. 내 영역의 폴트면 페이지를 열고 true, 아니면 false.
 *     NONE → (pagein) → READ,  READ → RW,  ESR 모드에서 첫 접근이 쓰기면 NONE → (pagein) → RW. */
static bool vmr_handle_fault(vmr_t *r, const fault_t *f) {
    /* TODO A2: 범위 검사 → 페이지 번호 → 상태에 따라 mprotect (+ pagein), 카운터 증가 */
    (void)r; (void)f;
    return false;
}

/* A3: 내 폴트가 아닐 때. 처리를 기본 동작(SIG_DFL)으로 되돌리고 같은 신호를 다시 보낸다.
 *     (지금 이 신호는 핸들러 동안 막혀 있으므로, 핸들러가 return 하는 순간 기본 동작이 실행된다.)
 *     하드웨어 폴트는 return 만 해도 같은 명령이 다시 폴트하지만, kill 로 온 신호는 다시 안 온다 —
 *     그리고 이 맥에서는 kill 로 온 SIGSEGV 도 si_code=2 라 출처로 구분할 수 없다 → 항상 다시 보낸다. */
static void pass_to_default(const fault_t *f) {
    /* TODO A3: sigaction(f->sig, SIG_DFL) 후 raise(f->sig) */
    (void)f;
}

/* B1: 새 epoch 을 연다 (s->epoch + 1). RW(=dirty) 페이지를 전부 저장소에 복사하고 READ 로 되돌린다.
 *     반환: 저장한 페이지 수, 실패 -1. */
static long vmr_checkpoint(vmr_t *r, snapstore_t *s) {
    /* TODO B1: e = s->epoch + 1; RW 페이지마다 snap_put(s, e, i, 페이지) → mprotect(READ) → state = READ */
    (void)r; (void)s; (void)snap_put;
    return -1;
}

/* B2: epoch e 시점으로 되돌린다. e 보다 뒤의 기록은 버린다 (역사 잘라내기).
 *     만진 적 있는 페이지마다 "epoch ≤ e 인 마지막 복사본" (없으면 초기 내용 = pagein 또는 0) 을 쓰고 READ 로.
 *     반환 0, 실패 -1. */
static int vmr_restore(vmr_t *r, snapstore_t *s, unsigned e) {
    /* TODO B2: epoch > e 기록 버리기 → 만진 페이지마다 "≤ e 마지막 복사본 / 초기 내용" 쓰기 → READ */
    (void)r; (void)s; (void)e;
    return -1;
}

/* ---------------- 하네스: 시그널 진입점과 탈출 ---------------- */

static vmr_t *volatile g_vmr;                    /* 핸들러가 찾는 현재 영역 (한 번에 하나) */
static sigjmp_buf g_jb;
static volatile sig_atomic_t g_armed, g_same, g_escapes;
static void *volatile g_last_addr;
static volatile int g_report_fd = -1;            /* 자식 시험용: 남의 폴트 신호 번호를 부모에게 */

static void on_fault(int sig, siginfo_t *si, void *ucv) {
    int saved_errno = errno;
    fault_t f = { si->si_addr, sig, si->si_code, 0, 0 };
#if defined(__APPLE__) && defined(__aarch64__)
    ucontext_t *uc = ucv;
    f.esr_valid = 1;
    f.esr = uc->uc_mcontext->__es.__esr;
#else
    (void)ucv;
#endif
    if (f.addr == g_last_addr) g_same++; else { g_same = 0; g_last_addr = f.addr; }
    if (g_same < 4 && vmr_handle_fault(g_vmr, &f)) { errno = saved_errno; return; }
    g_same = 0; g_last_addr = NULL;
    if (g_armed) { g_armed = 0; g_escapes++; siglongjmp(g_jb, 1); }
    if (g_report_fd < 0) {                       /* 시험 프로세스 본체에서 guarded() 밖 폴트: 멈추지 말고 끝낸다 */
        static const char m[] = "FAIL unexpected fault outside guarded()\n";
        (void)!write(1, m, sizeof m - 1);
        _exit(2);
    }
    if (g_report_fd >= 0) { unsigned char b = (unsigned char)sig; (void)!write(g_report_fd, &b, 1); }
    pass_to_default(&f);
    errno = saved_errno;
}
static void install_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_fault;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);               /* macOS: 보호 위반은 SIGBUS 로 온다 */
}
/* fn(arg) 를 실행한다. 핸들러가 고치지 못한 폴트가 나면 0, 끝까지 돌면 1. */
static int guarded(void (*fn)(void *), void *arg) {
    g_last_addr = NULL; g_same = 0;
    if (sigsetjmp(g_jb, 1)) return 0;
    g_armed = 1;
    fn(arg);
    g_armed = 0;
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
    kill(pid, SIGKILL);
    waitpid(pid, &st, 0);
    return -1;
}

/* ---------------- 시험 작업 ---------------- */

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static unsigned char init_byte(size_t page, size_t off) { return (unsigned char)(page * 31u + off * 7u + 1u); }
static void pagein_pattern(size_t idx, unsigned char *dst, size_t pg) {    /* "백킹 스토어에서 읽기" */
    for (size_t o = 0; o < pg; o++) dst[o] = init_byte(idx, o);
}

enum { NP = 64 };
typedef struct {
    vmr_t *r; int nr, nw; const size_t *rd, *wr;   /* 읽을 페이지·쓸 페이지 목록 (페이지 안 위치는 고정) */
    int bad_reads;
} touch_job_t;
static void touch_job(void *a) {
    touch_job_t *j = a;
    for (int k = 0; k < j->nr; k++) {
        size_t off = j->rd[k] * PG + 123;
        if (((volatile unsigned char *)j->r->base)[off] != init_byte(j->rd[k], 123)) j->bad_reads++;
    }
    for (int k = 0; k < j->nw; k++) ((volatile unsigned char *)j->r->base)[j->wr[k] * PG + 77] = 0xEE;
}
static void touch_again(void *a) {               /* 같은 위치를 다시: 폴트가 더 나면 안 된다 */
    touch_job_t *j = a;
    for (int k = 0; k < j->nr; k++) (void)((volatile unsigned char *)j->r->base)[j->rd[k] * PG + 999];
    for (int k = 0; k < j->nw; k++) ((volatile unsigned char *)j->r->base)[j->wr[k] * PG + 5] = 0xDD;
}

typedef struct { vmr_t *r; uint32_t seed; int nops; unsigned char *written; unsigned char *firstw; } rand_job_t;
static void rand_job(void *a) {                 /* 무작위 읽기·쓰기. written[p] = 이번 epoch 에 쓴 페이지 */
    rand_job_t *j = a;
    for (int k = 0; k < j->nops; k++) {
        uint32_t x = xs32(&j->seed);
        size_t p = x % NP, off = (x >> 8) % PG;
        if (k % 3 == 0) {                                  /* 3 번에 1 번은 읽기 */
            (void)((volatile unsigned char *)j->r->base)[p * PG + off];
            if (j->firstw && !j->firstw[p]) j->firstw[p] = 1;          /* 1 = 읽기가 먼저 */
        } else {
            ((volatile unsigned char *)j->r->base)[p * PG + off] = (unsigned char)(x >> 24);
            j->written[p] = 1;
            if (j->firstw && !j->firstw[p]) j->firstw[p] = 2;          /* 2 = 첫 접근이 쓰기 */
        }
    }
}
typedef struct { vmr_t *r; int which; } atomic_job_t;
static void atomic_job(void *a) {
    atomic_job_t *j = a;
    __atomic_fetch_add((int *)(void *)(j->r->base + (size_t)j->which * PG + 64), 1, __ATOMIC_SEQ_CST);
}

static size_t count_state(const vmr_t *r, int st) {
    size_t n = 0;
    for (size_t i = 0; i < r->npages; i++) n += r->state[i] == st;
    return n;
}
typedef struct { vmr_t *r; unsigned char *img; int copy_out, equal; } img_job_t;
static void img_job(void *a) {                  /* 영역 ↔ 기준 이미지 (READ 이상 페이지만 영역에서 읽는다) */
    img_job_t *j = a;
    if (j->copy_out) {
        for (size_t p = 0; p < NP; p++)
            if (j->r->state[p] != PS_NONE) memcpy(j->img + p * PG, j->r->base + p * PG, PG);
    } else {
        j->equal = memcmp(j->r->base, j->img, NP * PG) == 0;      /* NONE 페이지는 여기서 폴트 → pagein */
    }
}
static int region_equals(vmr_t *r, unsigned char *img) {
    img_job_t j = { r, img, 0, 0 };
    return r->base && r->state && guarded(img_job, &j) && j.equal;
}
static int saved_set_equals(const snapstore_t *s, unsigned e, const unsigned char *written) {
    unsigned char got[NP] = { 0 };
    for (size_t k = 0; k < s->n; k++)
        if (s->e[k].epoch == e) { if (got[s->e[k].page]) return 0; got[s->e[k].page] = 1; }   /* 중복 저장도 오류 */
    return memcmp(got, written, NP) == 0;
}

/* 자식: 관리 영역을 둔 채 남의 주소를 만진다. mode 0 = 남의 PROT_NONE 페이지, 1 = kill(SIGSEGV) */
static void child_foreign(int mode, int report_fd) {
    alarm(5);
    vmr_t r;
    if (vmr_init(&r, 4, NULL, 0)) _exit(3);
    g_vmr = &r;
    g_report_fd = report_fd;
    install_handlers();
    ((volatile unsigned char *)r.base)[10] = 1;                 /* 내 폴트는 처리된다 */
    if (mode == 0) {
        unsigned char *other = mmap(NULL, PG, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        (void)((volatile unsigned char *)other)[0];
    } else {
        kill(getpid(), SIGSEGV);
    }
    _exit(0);                                                   /* 여기 오면 실패 */
}
static void foreign_test(int mode, const char *name) {
    int p[2];
    if (pipe(p)) { CHECK(!"pipe"); return; }
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) { close(p[0]); child_foreign(mode, p[1]); }
    close(p[1]);
    int st = wait_child(pid, 1500);
    unsigned char rep = 0;
    struct pollfd pf = { p[0], POLLIN, 0 };
    if (poll(&pf, 1, 0) == 1 && read(p[0], &rep, 1) != 1) rep = 0;
    close(p[0]);
    int died = st != -1 && WIFSIGNALED(st);
    printf("  (%s: child %s, reported sig %d, term sig %d)\n", name,
           st == -1 ? "hung -> killed" : died ? "died by signal" : "exited", rep, died ? WTERMSIG(st) : 0);
    CHECK(died && rep != 0 && WTERMSIG(st) == rep);
    CHECK(died && (WTERMSIG(st) == SIGSEGV || WTERMSIG(st) == SIGBUS));
}

int main(void) {
    alarm(60);                                         /* 최후의 안전장치 */
    PG = (size_t)sysconf(_SC_PAGESIZE);
    printf("page size %zu\n", PG);
    install_handlers();

    /* ===== Part A: 지연 할당 ===== */
    vmr_t r;
    CHECK(vmr_init(&r, NP, pagein_pattern, 0) == 0);
    CHECK(r.base != NULL && ((uintptr_t)r.base % PG) == 0 && r.len == NP * PG);
    CHECK(r.state != NULL && count_state(&r, PS_NONE) == NP);
    g_vmr = &r;
    static const size_t rd[] = { 0, 3, 7, 8, 20, 21, 63 };
    static const size_t wr[] = { 3, 9, 20, 40 };             /* 3·20 은 읽고 쓰기, 9·40 은 쓰기만 */
    touch_job_t tj = { &r, 7, 4, rd, wr, 0 };
    int ok = r.base ? guarded(touch_job, &tj) : 0;
    printf("  (A: read faults %lu, upgrades %lu, pageins %lu)\n", r.n_read, r.n_upgrade, r.n_pagein);
    CHECK(ok);
    CHECK(r.n_read == 9 && r.n_upgrade == 4 && r.n_direct == 0);   /* 만진 페이지 9, 쓴 페이지 4 */
    CHECK(r.n_pagein == 9 && tj.bad_reads == 0);
    CHECK(count_state(&r, PS_NONE) == NP - 9 && count_state(&r, PS_RW) == 4);
    ok = r.base ? guarded(touch_again, &tj) : 0;
    CHECK(ok && r.n_read == 9 && r.n_upgrade == 4);                /* 두 번째 접근은 폴트 없음 */
    CHECK(ok && r.base[9 * PG + 77] == 0xEE && r.base[9 * PG + 78] == init_byte(9, 78));
    {   /* 페이지 경계를 걸친 8 바이트 쓰기: 두 페이지가 각자 폴트 */
        unsigned long before = r.n_read + r.n_upgrade;
        if (r.base) {
            uint64_t v = 0x1122334455667788ull;
            g_last_addr = NULL; g_same = 0;
            if (sigsetjmp(g_jb, 1) == 0) { g_armed = 1; memcpy((void *)(r.base + 31 * PG - 4), &v, 8); g_armed = 0; ok = 1; }
            else ok = 0;
        } else ok = 0;
        CHECK(ok && r.n_read + r.n_upgrade == before + 4 && r.state && r.state[30] == PS_RW && r.state[31] == PS_RW);
    }
    g_vmr = NULL;
    vmr_destroy(&r);

    foreign_test(0, "foreign PROT_NONE page");
    foreign_test(1, "kill(getpid(), SIGSEGV)");

    /* ===== Part B: dirty 추적 + 증분 체크포인트 ===== */
    snapstore_t s = { .pg = PG };
    CHECK(vmr_init(&r, NP, pagein_pattern, 0) == 0);
    g_vmr = &r;
    enum { EPOCHS = 8 };
    unsigned char *shadow[EPOCHS + 1];                          /* 기준: epoch 마다 전체 memcpy 사본 */
    shadow[0] = malloc(NP * PG);
    for (size_t p = 0; p < NP; p++) pagein_pattern(p, shadow[0] + p * PG, PG);
    int sets_ok = 1, guard_ok = 1, cnt_ok = 1;
    long total_saved = 0;
    uint32_t seed = 0xC0FFEEu;
    for (unsigned e = 1; e <= EPOCHS; e++) {
        unsigned char written[NP] = { 0 };
        rand_job_t rj = { &r, seed + e * 7919u, 16 + (int)e * 3, written, NULL };
        guard_ok &= r.base ? guarded(rand_job, &rj) : 0;
        long n = vmr_checkpoint(&r, &s);
        long nw = 0;
        for (int p = 0; p < NP; p++) nw += written[p];
        sets_ok &= n == nw && saved_set_equals(&s, e, written) && s.epoch == e;
        cnt_ok &= count_state(&r, PS_RW) == 0;
        total_saved += n > 0 ? n : 0;
        shadow[e] = malloc(NP * PG);
        memcpy(shadow[e], shadow[e - 1], NP * PG);
        img_job_t ij = { &r, shadow[e], 1, 0 };                /* 지금 영역 = epoch e 의 기준 사본 */
        guard_ok &= r.base && r.state ? guarded(img_job, &ij) : 0;
        if (e == 1) printf("  (B: epoch 1 wrote %ld pages, checkpoint saved %ld)\n", nw, n);
    }
    printf("  (B: %d epochs, %ld pages saved = %ld KiB vs full copies %d x %zu KiB)\n",
           EPOCHS, total_saved, total_saved * (long)PG / 1024, EPOCHS, NP * PG / 1024);
    CHECK(guard_ok);
    CHECK(sets_ok);                                            /* 매 epoch: 저장 집합 == 쓴 집합 */
    CHECK(cnt_ok);                                             /* 체크포인트 뒤 RW(=dirty) 0 */
    CHECK(vmr_restore(&r, &s, 5) == 0 && region_equals(&r, shadow[5]));
    CHECK(s.epoch == 5 && count_state(&r, PS_RW) == 0);
    {   /* 되돌린 뒤 계속: 새 epoch 6' 의 저장 집합도 정확해야 한다 */
        unsigned char written[NP] = { 0 };
        rand_job_t rj = { &r, 4242u, 30, written, NULL };
        int g = r.base ? guarded(rand_job, &rj) : 0;
        long n = vmr_checkpoint(&r, &s);
        long nw = 0;
        for (int p = 0; p < NP; p++) nw += written[p];
        CHECK(g && n == nw && s.epoch == 6 && saved_set_equals(&s, 6, written));
    }
    CHECK(vmr_restore(&r, &s, 3) == 0 && region_equals(&r, shadow[3]));
    CHECK(vmr_restore(&r, &s, 0) == 0 && region_equals(&r, shadow[0]) && s.n == 0);
    CHECK(vmr_restore(&r, &s, 1) == -1);                       /* 버린 미래로는 못 간다 */
    for (unsigned e = 0; e <= EPOCHS; e++) free(shadow[e]);
    snap_free(&s);
    g_vmr = NULL;
    vmr_destroy(&r);

    /* ===== Part C: ESR 로 읽기/쓰기 구분 ===== */
    int w = -1;
    CHECK(esr_decode(0x92000007u, &w) == ESR_TRANSLATION && w == 0);   /* 맥 실측: PROT_NONE 읽기 */
    CHECK(esr_decode(0x92000047u, &w) == ESR_TRANSLATION && w == 1);   /* PROT_NONE 쓰기 */
    CHECK(esr_decode(0x9200004fu, &w) == ESR_PERMISSION && w == 1);    /* PTE 있는 READ 페이지에 쓰기 */
    CHECK(esr_decode(0x9200014fu, &w) == ESR_PERMISSION && w == 0);    /* CM=1: 캐시 유지보수 */
    CHECK(esr_decode(0x9200000bu, &w) == ESR_ACCESS_FLAG && w == 0);
    CHECK(esr_decode(0x92000010u, &w) == ESR_DABT_OTHER);              /* 동기 외부 어보트 */
    CHECK(esr_decode(0x82000007u, &w) == ESR_OTHER && w == 0);         /* EC 0x20 명령 어보트 */
    CHECK(esr_decode(0x96000047u, &w) == ESR_OTHER && w == 0);         /* EC 0x25 같은 EL */
#if defined(__APPLE__) && defined(__aarch64__)
    {   /* 같은 무작위 작업을 두 모드로: 만진 집합·dirty 집합이 같아야, ESR 모드는 폴트가 적어야 */
        unsigned long faults[2] = { 0, 0 };
        int same = 1, okc = 1;
        unsigned char ws[2][NP], st[2][NP];
        for (int mode = 0; mode < 2; mode++) {
            vmr_t q;
            unsigned char written[NP] = { 0 }, firstw[NP] = { 0 };
            okc &= vmr_init(&q, NP, NULL, mode) == 0;
            g_vmr = &q;
            rand_job_t rj = { &q, 777u, 70, written, firstw };
            okc &= q.base ? guarded(rand_job, &rj) : 0;
            size_t touched = 0, wcount = 0, fw = 0;
            for (int p = 0; p < NP; p++) { touched += firstw[p] != 0; wcount += written[p]; fw += firstw[p] == 2; }
            if (mode == 0) okc &= q.n_read == touched && q.n_upgrade == wcount && q.n_direct == 0;
            else okc &= q.n_read + q.n_direct == touched && q.n_upgrade + q.n_direct == wcount && q.n_direct == fw;
            faults[mode] = q.n_read + q.n_upgrade + q.n_direct;
            memcpy(ws[mode], written, NP);
            for (int p = 0; p < NP; p++) st[mode][p] = q.state ? q.state[p] : 0xFF;
            printf("  (C: mode %s: touched %zu, written %zu, first-touch-is-write %zu -> faults %lu)\n",
                   mode ? "ESR " : "READ", touched, wcount, fw, faults[mode]);
            g_vmr = NULL;
            vmr_destroy(&q);
        }
        same = memcmp(ws[0], ws[1], NP) == 0 && memcmp(st[0], st[1], NP) == 0;
        CHECK(okc);
        CHECK(same && faults[1] < faults[0]);
        /* 원자적 RMW (LDADDAL) 첫 접근: 맥 실측 WnR=0 → READ 후 업그레이드. 어느 쪽이든 결과는 dirty */
        vmr_t q;
        okc = vmr_init(&q, 4, NULL, 1) == 0;
        g_vmr = &q;
        atomic_job_t aj = { &q, 2 };
        okc &= q.base ? guarded(atomic_job, &aj) : 0;
        printf("  (C: atomic RMW on fresh page: n_read %lu, n_upgrade %lu, n_direct %lu)\n", q.n_read, q.n_upgrade, q.n_direct);
        CHECK(okc && q.state && q.state[2] == PS_RW && q.n_read + q.n_direct == 1 && *(volatile int *)(void *)(q.base + 2 * PG + 64) == 1);
        g_vmr = NULL;
        vmr_destroy(&q);
    }
#else
    printf("SKIP ESR mode cross-check (not arm64 macOS)\n");
#endif

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
