/* OS-M03 COW fork와 페이지 폴트 관찰
 *
 *   Part A  부모가 큰 익명 영역을 다 쓴 뒤 fork → 자식이 k 페이지만 쓴다.
 *           자식 안에서 getrusage(RUSAGE_SELF).ru_minflt 를 앞뒤로 재서 증가량 ≈ k 인지,
 *           k 를 바꿔 가며 직선 맞춤(최소제곱) 기울기 ≈ 1 인지 본다. 같은 페이지를 다시 쓰면 ≈ 0.
 *           TODO A1 minflt_self + touch_pages, A2 cow_probe(fork·pipe·wait), A3 fit_line.
 *   Part B  폴트는 "페이지 단위": 쓰기 목록이 건드리는 서로 다른 페이지 수를 세는 TODO B1 pages_touched,
 *           그리고 격리(COW: 부모 데이터 그대로) vs MAP_SHARED(부모에게 보임) 대비 — TODO B2 child_write_changes.
 *           읽기만 하는 자식의 폴트 수는 OS 마다 다르다 → 상한만 체크하고 실제 값은 찍는다.
 *   Part C  큰 프로세스에서 다른 프로그램 띄우기: TODO C1 fork+exec (exec 실패를 CLOEXEC 파이프로 errno 전달),
 *           TODO C2 posix_spawn (출력 dup2 + 시그널 마스크 초기화). 지연 비교는 demo_bench.c.
 *
 * 페이지 크기는 하드코딩하지 않는다: sysconf(_SC_PAGESIZE) — 이 맥(Apple M6)은 16384, Linux VM 은 4096.
 * 체크는 "폴트 수 ≥ k 이고 ≤ k + slack" 처럼 넉넉한 경계만 쓴다. 시간은 재지 않는다.
 * 자식이 멈춰도 wait_child() 가 3초 뒤 SIGKILL 로 정리하고, main 의 alarm(30) 이 최후의 안전장치다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _DARWIN_C_SOURCE        /* macOS: MAP_ANON 등 */
#define _DEFAULT_SOURCE         /* Linux glibc: 같은 것들 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static size_t PG;               /* sysconf(_SC_PAGESIZE) — main 에서 채운다 */
#define NPAGES 2048             /* 실험 영역 = 2048 페이지 (맥 32 MiB, 4 KB 페이지면 8 MiB) */
#define SLACK(k) (16 + (long)(k) / 4)    /* 폴트 수 상한 여유: 스택·libc 잡음 + ASan 그림자 메모리(8 페이지당 1) */

/* ---------------- 하네스 제공 도우미 ---------------- */

/* fd 에서 정확히 n 바이트를 읽는다. 최대 ms 밀리초. 반환: 읽은 바이트 수 (EOF/시간 초과면 n 보다 작음). */
static size_t read_full_timeout(int fd, void *buf, size_t n, int ms) {
    size_t got = 0;
    while (got < n) {
        struct pollfd p = { fd, POLLIN, 0 };
        int r = poll(&p, 1, ms);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;                                   /* 시간 초과 */
        ssize_t k = read(fd, (char *)buf + got, n - got);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;                                   /* EOF 또는 오류 */
        got += (size_t)k;
    }
    return got;
}

/* 자식을 최대 ms 밀리초 기다린다. 끝났으면 waitpid status, 시간 초과면 SIGKILL + reap 후 -1. */
static int wait_child(pid_t pid, int ms) {
    int st;
    for (int t = 0; t < ms; t++) {
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) return st;
        if (r < 0 && errno != EINTR) return -1;
        struct timespec one = { 0, 1000000 };
        nanosleep(&one, NULL);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &st, 0);
    return -1;
}

/* 거둘 자식이 하나도 없어질 때까지 최대 ms 기다린다. 도중에 waitpid 가 pid 를 돌려주면 = 누가 좀비로 남아
   있었다는 뜻 → false. macOS 의 posix_spawn 은 실행 파일이 없어 실패해도 잠깐 동안 waitpid(-1, WNOHANG) 이 0
   ("아직 안 끝난 자식이 있음") 을 돌려준다(실측) — 그래서 즉시 한 번이 아니라 기다리며 본다. *waited_ms 에 걸린 시간. */
static bool no_children_within(int ms, int *waited_ms) {
    for (int t = 0; t <= ms; t++) {
        errno = 0;
        pid_t r = waitpid(-1, NULL, WNOHANG);
        if (r == -1 && errno == ECHILD) { *waited_ms = t; return true; }
        if (r > 0) { *waited_ms = t; return false; }
        struct timespec one = { 0, 1000000 };
        nanosleep(&one, NULL);
    }
    *waited_ms = ms;
    return false;
}

static int count_open_fds(void) {
    int n = 0;
    for (int fd = 0; fd < 256; fd++)
        if (fcntl(fd, F_GETFD) != -1) n++;
    return n;
}

/* 영역의 각 페이지 첫 바이트가 expect 와 다른 페이지 수 */
static size_t pages_not_equal(const char *base, size_t npages, uint8_t expect) {
    size_t n = 0;
    for (size_t i = 0; i < npages; i++)
        if ((uint8_t)base[i * PG] != expect) n++;
    return n;
}

static char *map_region(bool shared, size_t npages, uint8_t fill) {
    char *p = mmap(NULL, npages * PG, PROT_READ | PROT_WRITE,
                   (shared ? MAP_SHARED : MAP_PRIVATE) | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) return NULL;
    memset(p, fill, npages * PG);                           /* 전부 상주시킨다 (fork 전에 "다 쓴 부모") */
    return p;
}

typedef enum { T_READ, T_WRITE, T_RW } touch_op;

/* 자식 안에서 돌릴 일: 반환값은 부모에게 그대로 전달된다 */
typedef uint64_t (*work_fn)(void *ctx);

typedef struct {
    long     d1;                /* 첫 번째 work 동안의 ru_minflt 증가량 */
    long     d2;                /* 바로 이어서 같은 work 를 한 번 더 했을 때의 증가량 */
    uint64_t ret;               /* 두 번째 work 의 반환값 */
} probe_res;

/* ================= Part A ================= */

/* TODO A1-1: 이 프로세스가 지금까지 낸 minor fault 수 (getrusage RUSAGE_SELF 의 ru_minflt). */
static long minflt_self(void) {
    return 0;                                                /* TODO A1-1 */
}

/* TODO A1-2: base 의 페이지 first .. first+k-1 의 "첫 바이트" 를 건드린다 (페이지 간격 = PG).
 *   T_READ : 읽기만.  T_WRITE : val 을 쓴다.  T_RW : 읽고 나서 val 을 쓴다.
 *   반환: 읽은 바이트들의 합 (T_WRITE 는 0). 읽기가 최적화로 사라지지 않게 volatile 로 접근한다. */
static uint64_t touch_pages(char *base, size_t first, size_t k, touch_op op, uint8_t val) {
    (void)base; (void)first; (void)k; (void)op; (void)val;
    return 0;                                                /* TODO A1-2 */
}

/* TODO A2: fork 해서 자식 안에서 work 를 두 번 돌리고 폴트 증가량을 잰다.
 *   자식: f0 = minflt → work(ctx) → f1 → ret = work(ctx) → f2 → {f1-f0, f2-f1, ret} 를 파이프로 → _exit(0)
 *         (exit 가 아니라 _exit: 부모에게서 복제된 stdio 버퍼를 비우지 않게)
 *   부모: 결과를 read_full_timeout(…, 3000) 으로 받고, wait_child(pid, 3000) 로 거둔다.
 *   반환 0 = 성공 (결과 다 받음 + 자식 정상 종료 코드 0), -1 = 실패. 파이프 fd 는 전부 닫는다. */
static int cow_probe(work_fn work, void *ctx, probe_res *out) {
    (void)work; (void)ctx; (void)wait_child;
    memset(out, 0, sizeof *out);
    return -1;                                               /* TODO A2 */
}

/* TODO A3: 최소제곱 직선 y = slope*x + icept 와 결정계수 r2 (= 1 - SS_res/SS_tot).
 *   n < 2 또는 x 가 모두 같으면 slope = icept = 0, r2 = 0. y 가 모두 같으면(SS_tot = 0) 완벽 맞춤 → r2 = 1. */
static void fit_line(const double *x, const double *y, int n, double *slope, double *icept, double *r2) {
    (void)x; (void)y; (void)n;
    *slope = *icept = *r2 = 0;                               /* TODO A3 */
}

/* ================= Part B ================= */

typedef struct { size_t off, len; } wr_t;     /* 영역 시작에서 off 바이트 위치에 len 바이트 쓰기 */

static int cmp_range(const void *a, const void *b) {
    const size_t *x = a, *y = b;
    return (x[0] > y[0]) - (x[0] < y[0]);
}

/* TODO B1: 쓰기 목록 w[0..n) 이 건드리는 "서로 다른" 페이지 수 (페이지 크기 pg).
 *   쓰기 하나는 페이지 off/pg .. (off+len-1)/pg 를 덮는다. len == 0 은 아무것도 안 덮는다.
 *   겹치거나 같은 페이지는 한 번만 센다 (COW 는 페이지당 한 번). n ≤ 64.
 *   방법: [첫 페이지, 끝 페이지] 구간들로 바꿔 시작 순으로 정렬 → 겹치거나 맞닿는 구간을 합치며 길이 합. */
static size_t pages_touched(const wr_t *w, size_t n, size_t pg) {
    (void)w; (void)n; (void)pg; (void)cmp_range;
    return 0;                                                /* TODO B1 */
}

typedef struct { char *base; size_t first, k; touch_op op; uint8_t val; } touch_ctx;
static uint64_t touch_work(void *c) {
    touch_ctx *t = c;
    return touch_pages(t->base, t->first, t->k, t->op, t->val);
}

/* TODO B2: shared 면 MAP_SHARED, 아니면 MAP_PRIVATE 익명 영역 NPAGES 페이지를 0xA5 로 채워 만들고(map_region),
 *   자식이 첫 k 페이지에 T_RW 로 0x5A 를 쓰게 한다(cow_probe + touch_work).
 *   *child_saw = 자식의 두 번째 패스가 읽은 합 (자식은 자기가 쓴 0x5A 를 봐야 한다 → k * 0x5A),
 *   *faults    = 자식 첫 패스의 폴트 증가량 d1.
 *   반환: 자식이 끝난 뒤 부모 쪽에서 0xA5 가 아닌 페이지 수. 실패하면 (size_t)-1. 영역은 munmap 한다. */
static size_t child_write_changes(bool shared, size_t k, uint64_t *child_saw, long *faults) {
    (void)shared; (void)k;
    *child_saw = 0;
    *faults = -1;
    return (size_t)-1;                                       /* TODO B2 */
}

/* ================= Part C ================= */

/* TODO C1: fork + execv 로 path 를 실행한다. out_fd ≥ 0 이면 자식의 stdout(1) 을 out_fd 로 dup2.
 *   exec 가 실패하면 그 errno 를 부모가 받아 "반환값" 으로 돌려준다 (posix_spawn 과 같은 모양):
 *     FD_CLOEXEC 를 건 오류 파이프 → exec 성공 시 커널이 닫아서 부모는 EOF(0 바이트),
 *     실패 시 자식이 errno 4바이트를 쓰고 _exit(127).
 *   성공: 0 반환, *status = waitpid status. 실패: errno 값 (ENOENT 등), 자식은 이미 거둔 상태.
 *   자식이 3초 안에 안 끝나면 ETIMEDOUT. 부모 쪽 fd 는 새지 않게 닫는다. */
static int spawn_fork_exec(const char *path, char *const argv[], int out_fd, int *status) {
    (void)path; (void)argv; (void)out_fd; (void)status;
    return ENOSYS;                                           /* TODO C1 */
}

/* TODO C2: posix_spawn 으로 같은 일을 한다.
 *   file actions: out_fd ≥ 0 이면 adddup2(out_fd, 1).
 *   attr: POSIX_SPAWN_SETSIGMASK 로 자식의 시그널 마스크를 비운다 (부모가 막아 둔 시그널을 물려주지 않음).
 *   반환: posix_spawn 의 오류 코드 그대로 (ENOENT 등) 또는 0. 성공이면 *status = waitpid status,
 *   3초 안에 안 끝나면 ETIMEDOUT. attr/file actions 는 destroy 한다. */
static int spawn_posix(const char *path, char *const argv[], int out_fd, int *status) {
    (void)path; (void)argv; (void)out_fd; (void)status;
    return ENOSYS;                                           /* TODO C2 */
}

/* ================= 테스트 ================= */

/* 자식 안에서 임의의 쓰기 목록을 수행하는 work (Part B 실측용) */
typedef struct { char *base; const wr_t *w; size_t n; } wlist_ctx;
static uint64_t wlist_work(void *c) {
    wlist_ctx *t = c;
    for (size_t i = 0; i < t->n; i++) memset(t->base + t->w[i].off, 0x33, t->w[i].len);
    return 0;
}

static void test_part_a(void) {
    printf("== Part A: minflt / touch_pages ==\n");
    /* 막 만든 익명 영역에 처음 쓰면 demand-zero 폴트가 페이지마다 난다 (fork 와 무관) */
    char *z = mmap(NULL, 64 * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    long f0 = minflt_self();
    uint64_t s0 = touch_pages(z, 0, 32, T_WRITE, 7);
    long f1 = minflt_self();
    uint64_t s1 = touch_pages(z, 0, 32, T_READ, 0);
    uint64_t s2 = touch_pages(z, 30, 4, T_RW, 9);            /* 30,31 은 7, 32,33 은 아직 0 */
    printf("  zero-fill: 32 fresh pages written -> minflt +%ld\n", f1 - f0);
    CHECK(f0 >= 0 && f1 - f0 >= 32 && f1 - f0 <= 32 + SLACK(32));
    CHECK(s0 == 0 && s1 == 32 * 7 && s2 == 14);
    CHECK((uint8_t)z[30 * PG] == 9 && (uint8_t)z[33 * PG] == 9 && (uint8_t)z[34 * PG] == 0 && (uint8_t)z[30 * PG + 1] == 0);
    munmap(z, 64 * PG);

    printf("== Part A: fork, child writes k of %d pages ==\n", NPAGES);
    char *r = map_region(false, NPAGES, 0xA5);
    CHECK(r != NULL);
    if (!r) return;
    static const size_t ks[] = { 0, 1, 4, 16, 64, 256, 1024, 2048 };
    enum { NK = sizeof ks / sizeof ks[0] };
    double xs[NK], ys[NK];
    int nok = 0, in_bounds = 0, rewrite_free = 0;
    printf("  %6s %8s %8s\n", "k", "d1", "d2");
    for (int i = 0; i < NK; i++) {
        touch_ctx c = { r, 0, ks[i], T_WRITE, 0x5A };
        probe_res pr;
        if (cow_probe(touch_work, &c, &pr) != 0) continue;
        nok++;
        printf("  %6zu %8ld %8ld\n", ks[i], pr.d1, pr.d2);
        xs[i] = (double)ks[i];
        ys[i] = (double)pr.d1;
        if (pr.d1 >= (long)ks[i] && pr.d1 <= (long)ks[i] + SLACK(ks[i])) in_bounds++;
        if (pr.d2 >= 0 && pr.d2 <= SLACK(0)) rewrite_free++;
    }
    CHECK(nok == NK);
    CHECK(in_bounds == NK);                                  /* k ≤ d1 ≤ k + slack */
    CHECK(rewrite_free == NK);                               /* 같은 페이지 다시 쓰기 = 이미 내 사본 → 폴트 ≈ 0 */
    double sl = 0, ic = 0, r2 = 0;
    if (nok == NK) fit_line(xs, ys, NK, &sl, &ic, &r2);
    printf("  fit: d1 = %.4f * k + %.2f   (r2 = %.6f)\n", sl, ic, r2);
    CHECK(sl > 0.95 && sl < 1.30 && r2 > 0.999);             /* ASan 빌드는 ≈ 1.125 (그림자 메모리) */
    CHECK(pages_not_equal(r, NPAGES, 0xA5) == 0);            /* 자식이 몇 번을 써도 부모 데이터는 그대로 (COW 격리) */
    munmap(r, NPAGES * PG);

    /* fit_line 단위 테스트 (고정 입력) */
    double x1[] = { 0, 1, 2, 3 }, y1[] = { 1, 3, 5, 7 };
    fit_line(x1, y1, 4, &sl, &ic, &r2);
    CHECK(sl > 1.9999 && sl < 2.0001 && ic > 0.9999 && ic < 1.0001 && r2 > 0.9999);
    double y2[] = { 1, 2, 2, 3 };
    fit_line(x1, y2, 4, &sl, &ic, &r2);                      /* 기울기 0.6, 절편 1.1, r2 = 0.9 */
    CHECK(sl > 0.5999 && sl < 0.6001 && ic > 1.0999 && ic < 1.1001 && r2 > 0.8999 && r2 < 0.9001);
    double x3[] = { 5, 5, 5 }, y3[] = { 1, 2, 3 };
    fit_line(x3, y3, 3, &sl, &ic, &r2);
    CHECK(sl == 0 && ic == 0 && r2 == 0);
}

static void test_part_b(void) {
    printf("== Part B: pages_touched ==\n");
    size_t P = 16384;
    wr_t a1[] = { { 0, 1 } };
    wr_t a2[] = { { P - 1, 2 } };                            /* 2바이트가 경계를 넘는다 → 2 페이지 */
    wr_t a3[] = { { 0, 0 }, { 5 * P, 0 } };
    wr_t a4[] = { { 0, P } };
    wr_t a5[] = { { 0, P + 1 } };
    wr_t a6[] = { { 5, 1 }, { 6, 1 }, { 100, 8 } };          /* 같은 페이지 세 번 = 1 */
    wr_t a7[] = { { 2 * P, 3 * P }, { 0, 3 * P } };          /* 0..2, 2..4 → 0..4 = 5 */
    wr_t a8[] = { { 9 * P, 1 }, { 0, 1 }, { 4 * P + 7, 1 }, { 3 * P, P + 8 } };   /* 9, 0, 4, 3..4 → {0,3,4,9} */
    wr_t a9[] = { { 3 * P, P }, { 4 * P, P } };              /* 맞닿음: 3, 4 */
    CHECK(pages_touched(a1, 1, P) == 1);
    CHECK(pages_touched(a2, 1, P) == 2);
    CHECK(pages_touched(a3, 2, P) == 0 && pages_touched(a1, 0, P) == 0);
    CHECK(pages_touched(a4, 1, P) == 1 && pages_touched(a5, 1, P) == 2);
    CHECK(pages_touched(a6, 3, P) == 1);
    CHECK(pages_touched(a7, 2, P) == 5);
    CHECK(pages_touched(a8, 4, P) == 4);
    CHECK(pages_touched(a9, 2, P) == 2);
    CHECK(pages_touched(a2, 1, 4096) == 2 && pages_touched(a4, 1, 4096) == 4);   /* 같은 쓰기, 4 KB 페이지 */

    printf("== Part B: predicted vs measured (child writes a list) ==\n");
    char *r = map_region(false, NPAGES, 0xA5);
    if (!r) { CHECK(r != NULL); return; }
    /* 200 바이트 레코드 배열을 흉내: 경계를 넘는 레코드, 같은 페이지 여러 번, 멀리 떨어진 페이지 */
    wr_t w[] = { { PG - 100, 200 }, { 10 * PG + 3, 200 }, { 10 * PG + 400, 200 }, { 20 * PG, 3 * PG },
                 { 21 * PG + 50, 10 }, { 100 * PG - 1, 2 } };
    size_t n = sizeof w / sizeof w[0];
    size_t pred = pages_touched(w, n, PG);
    wlist_ctx wc = { r, w, n };
    probe_res pr;
    int ok = cow_probe(wlist_work, &wc, &pr);
    printf("  predicted %zu pages, child minflt +%ld (rewrite +%ld)\n", pred, ok == 0 ? pr.d1 : -1, ok == 0 ? pr.d2 : -1);
    CHECK(pred == 2 + 1 + 3 + 2);                            /* {0,1} {10} {20,21,22} {99,100} */
    CHECK(ok == 0 && pr.d1 >= (long)pred && pr.d1 <= (long)pred + SLACK(pred));

    /* 읽기만 하는 자식: 몇 번 폴트가 나는지는 OS 마다 다르다 → 상한만 체크, 실제 값은 출력 */
    touch_ctx rc = { r, 0, 256, T_READ, 0 };
    ok = cow_probe(touch_work, &rc, &pr);
    printf("  child READS 256 pages: minflt +%ld (second pass +%ld)\n", ok == 0 ? pr.d1 : -1, ok == 0 ? pr.d2 : -1);
    CHECK(ok == 0 && pr.d1 <= 256 + SLACK(256) && pr.d2 <= SLACK(0) && pr.ret == 256u * 0xA5);
    munmap(r, NPAGES * PG);

    printf("== Part B: COW isolation vs MAP_SHARED ==\n");
    uint64_t saw = 0;
    long fl = -1;
    size_t ch = child_write_changes(false, 300, &saw, &fl);
    printf("  PRIVATE: parent pages changed %ld, child faults %ld\n", (long)ch, fl);
    CHECK(ch == 0 && saw == 300u * 0x5A);                    /* 자식은 자기 쓰기를 보고, 부모는 못 본다 */
    CHECK(fl >= 300 && fl <= 2 * 300 + SLACK(300));          /* 읽고 쓰기: 페이지당 1~2 폴트 (OS 마다) */
    saw = 0;
    fl = -1;
    ch = child_write_changes(true, 300, &saw, &fl);
    printf("  SHARED : parent pages changed %ld, child faults %ld\n", (long)ch, fl);
    CHECK(ch == 300 && saw == 300u * 0x5A);                  /* 같은 물리 페이지 → 부모에게 보인다 */
    CHECK(fl >= 0 && fl <= 300 + SLACK(300));                /* 복사 없음: 많아야 페이지당 1 (매핑만) */
}

static void test_part_c(void) {
    printf("== Part C: fork+exec vs posix_spawn ==\n");
    int fds0 = count_open_fds();
    for (int m = 0; m < 2; m++) {
        const char *name = m ? "posix_spawn" : "fork+exec";
        int (*sp)(const char *, char *const[], int, int *) = m ? spawn_posix : spawn_fork_exec;
        int pp[2];
        if (pipe(pp) != 0) { CHECK(0); return; }
        char *echo_argv[] = { "echo", "hello", "cow", NULL };
        int st = -1;
        int rc = sp("/bin/echo", echo_argv, pp[1], &st);
        close(pp[1]);
        char buf[64] = { 0 };
        size_t got = read_full_timeout(pp[0], buf, sizeof buf - 1, 3000);
        close(pp[0]);
        printf("  %-11s echo: rc=%d status=%s output=\"%.*s\"\n", name, rc,
               (rc == 0 && WIFEXITED(st)) ? "exit" : "?", (int)(got ? got - 1 : 0), buf);
        CHECK(rc == 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0 && got == 10 && memcmp(buf, "hello cow\n", 10) == 0);

        char *false_argv[] = { "false", NULL };
        st = -1;
        rc = sp("/usr/bin/false", false_argv, -1, &st);
        CHECK(rc == 0 && WIFEXITED(st) && WEXITSTATUS(st) == 1);

        char *bad_argv[] = { "nope", NULL };
        st = 12345;
        rc = sp("/nonexistent/OS-M03_no_such_prog", bad_argv, -1, &st);
        printf("  %-11s nonexistent: rc=%d (%s)\n", name, rc, strerror(rc));
        CHECK(rc == ENOENT && st == 12345);                  /* 실행 실패가 "반환값" 으로, status 는 안 건드림 */
        int waited = -1;
        bool clean = no_children_within(1000, &waited);
        printf("  %-11s after failure: no child left after %d ms\n", name, waited);
        CHECK(clean);                                        /* 좀비 없음 */
    }
    CHECK(count_open_fds() == fds0);                         /* fd 누수 없음 */

    /* 부모가 SIGUSR1 을 막아 둔 채로 띄우면? 시그널 마스크는 fork 와 exec 를 거쳐 그대로 물려진다 (POSIX). */
    sigset_t blk, old;
    sigemptyset(&blk);
    sigaddset(&blk, SIGUSR1);
    sigprocmask(SIG_BLOCK, &blk, &old);
    char *sh_argv[] = { "sh", "-c", "kill -USR1 $$; exit 3", NULL };
    int st1 = -1, st2 = -1;
    int rc1 = spawn_fork_exec("/bin/sh", sh_argv, -1, &st1);
    int rc2 = spawn_posix("/bin/sh", sh_argv, -1, &st2);
    sigprocmask(SIG_SETMASK, &old, NULL);
    printf("  blocked SIGUSR1 in parent: fork+exec child %s %d, posix_spawn(SETSIGMASK) child %s %d\n",
           (rc1 == 0 && WIFEXITED(st1)) ? "exit" : "signal", rc1 == 0 ? (WIFEXITED(st1) ? WEXITSTATUS(st1) : WTERMSIG(st1)) : -1,
           (rc2 == 0 && WIFEXITED(st2)) ? "exit" : "signal", rc2 == 0 ? (WIFEXITED(st2) ? WEXITSTATUS(st2) : WTERMSIG(st2)) : -1);
    CHECK(rc2 == 0 && WIFSIGNALED(st2) && WTERMSIG(st2) == SIGUSR1);   /* 마스크를 비웠으니 자기 시그널에 죽는다 */
}

int main(void) {
    alarm(30);                                               /* 무엇이 멈춰도 30초 뒤 SIGALRM 으로 끝 */
    long pg = sysconf(_SC_PAGESIZE);
    PG = pg > 0 ? (size_t)pg : 4096;
    printf("page size %zu\n", PG);
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
