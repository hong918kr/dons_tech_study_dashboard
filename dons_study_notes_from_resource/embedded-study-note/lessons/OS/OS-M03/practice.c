/* BUILD: -pthread */
/* OS-M03 추가 연습 — 한 번에 검증
 *
 *   연습 1  누가 폴트를 내나: 부모가 안 쓴 페이지에 자식이 쓰면(demand-zero), fork 뒤 부모가 쓰면(COW 는 양쪽),
 *           부모가 다시 쓰면, 부모가 읽기만 하면
 *   연습 2  스레드가 있는 프로세스의 fork: 다른 스레드가 쥔 뮤텍스는 자식에서 영원히 잠김 → pthread_atfork
 *   연습 3  상속 제어: macOS minherit(VM_INHERIT_NONE / VM_INHERIT_SHARE), Linux MADV_DONTFORK / MADV_WIPEONFORK
 *   연습 4  COW 친화 배치: 레코드마다 카운터 하나를 바꾸는 자식 — AoS vs SoA 폴트 수 (예측 = 건드린 페이지 수)
 *
 * 폴트 체크는 solution.c 처럼 넉넉한 경계만 쓴다. 자식은 전부 wait_child 로 거두고, alarm(30) 이 안전장치.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
 */
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
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
#if defined(__APPLE__)
#include <mach/vm_inherit.h>
#endif

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static size_t PG;
#define SLACK(k) (16 + (long)(k) / 4)

static long minflt_self(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_minflt;
}
static void sleep_ms(int ms) {
    struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}
static int wait_child(pid_t pid, int ms) {
    int st;
    for (int t = 0; t < ms; t++) {
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) return st;
        if (r < 0 && errno != EINTR) return -1;
        sleep_ms(1);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &st, 0);
    return -1;
}
/* 자식에서 fn(ctx) 를 돌려 ru_minflt 증가량을 받는다. 실패 -1. */
static long child_faults(void (*fn)(void *), void *ctx) {
    int p[2];
    if (pipe(p) != 0) return -1;
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        close(p[0]);
        long f0 = minflt_self();
        fn(ctx);
        long d = minflt_self() - f0;
        _exit(write(p[1], &d, sizeof d) == (ssize_t)sizeof d ? 0 : 1);
    }
    close(p[1]);
    long d = -1;
    struct pollfd pf = { p[0], POLLIN, 0 };
    if (pid < 0 || poll(&pf, 1, 3000) != 1 || read(p[0], &d, sizeof d) != (ssize_t)sizeof d) d = -1;
    close(p[0]);
    if (pid > 0) {
        int st = wait_child(pid, 3000);
        if (st < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0) d = -1;
    }
    return d;
}
static void write_pages(char *b, size_t first, size_t k, uint8_t v) {
    volatile uint8_t *p = (volatile uint8_t *)b;
    for (size_t i = first; i < first + k; i++) p[i * PG] = v;
}
static uint64_t read_pages(const char *b, size_t first, size_t k) {
    const volatile uint8_t *p = (const volatile uint8_t *)b;
    uint64_t s = 0;
    for (size_t i = first; i < first + k; i++) s += p[i * PG];
    return s;
}

/* ---------- 연습 1: 누가 폴트를 내나 ---------- */
typedef struct { char *b; size_t first, k; } wctx;
static void w_work(void *c) { wctx *w = c; write_pages(w->b, w->first, w->k, 0x77); }

static void ex1(void) {
    printf("== ex1: who faults ==\n");
    const size_t N = 1024, k = 200;
    char *b = mmap(NULL, N * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    memset(b, 1, (N / 2) * PG);                              /* 앞 절반만 부모가 썼다, 뒤 절반은 아직 한 번도 안 건드림 */
    wctx zc = { b, N / 2, k };                               /* 자식이 "안 건드린" 페이지에 쓴다 = demand-zero */
    long dz = child_faults(w_work, &zc);
    printf("  child writes %zu never-touched pages: +%ld\n", k, dz);
    CHECK(dz >= (long)k && dz <= (long)k + SLACK(k));
    CHECK(read_pages(b, N / 2, k) == 0);                     /* 부모에게는 여전히 0 (그리고 이 읽기가 부모의 첫 접근) */

    wctx cc = { b, 0, 1 };
    CHECK(child_faults(w_work, &cc) >= 1);                   /* fork 한 번 — 이제 앞 절반은 양쪽 다 쓰기 보호 */
    long f0 = minflt_self();
    write_pages(b, 0, k, 2);                                 /* 부모가 fork 뒤 처음 쓰기: 자식은 이미 죽었어도 폴트 */
    long dp = minflt_self() - f0;
    f0 = minflt_self();
    write_pages(b, 0, k, 3);                                 /* 같은 페이지 다시 */
    long dp2 = minflt_self() - f0;
    f0 = minflt_self();
    uint64_t s = read_pages(b, k, k);                        /* fork 뒤 부모가 "읽기만" */
    long dr = minflt_self() - f0;
    printf("  parent after fork: first write +%ld, rewrite +%ld, read-only +%ld\n", dp, dp2, dr);
    CHECK(dp >= (long)k && dp <= (long)k + SLACK(k));
    CHECK(dp2 <= SLACK(0) && dr <= SLACK(0) && s == k);
    munmap(b, N * PG);
}

/* ---------- 연습 2: 스레드 + fork ---------- */
static pthread_mutex_t M1 = PTHREAD_MUTEX_INITIALIZER;     /* 보호 없음 */
static pthread_mutex_t M2 = PTHREAD_MUTEX_INITIALIZER;     /* pthread_atfork 로 보호 */
static int ready_pipe[2], release_pipe[2];

static void *holder1(void *arg) {
    (void)arg;
    pthread_mutex_lock(&M1);
    char c = 1;
    (void)!write(ready_pipe[1], &c, 1);
    struct pollfd pf = { release_pipe[0], POLLIN, 0 };
    poll(&pf, 1, 5000);                                      /* main 이 풀어 줄 때까지 (최대 5초) 쥐고 있는다 */
    (void)!read(release_pipe[0], &c, 1);
    pthread_mutex_unlock(&M1);
    return NULL;
}
static void *holder2(void *arg) {
    (void)arg;
    pthread_mutex_lock(&M2);
    char c = 2;
    (void)!write(ready_pipe[1], &c, 1);
    sleep_ms(50);                                            /* 잠깐 쥐었다 놓는다 — prepare 핸들러가 이걸 기다린다 */
    pthread_mutex_unlock(&M2);
    return NULL;
}
static void at_prepare(void) { pthread_mutex_lock(&M2); }   /* fork 직전: M2 를 "fork 하는 스레드" 가 쥔다 */
static void at_parent(void) { pthread_mutex_unlock(&M2); }
static void at_child(void) { pthread_mutex_unlock(&M2); }   /* 자식: 유일한 스레드가 주인이니 풀 수 있다 */

/* 자식에서 m 을 trylock 해 본다. 반환: 0 = 잡힘, EBUSY = 영원히 잠김, -1 = 실패 */
static int child_trylock(pthread_mutex_t *m) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        int r = pthread_mutex_trylock(m);                    /* lock() 이면 자식이 영원히 멈춘다 */
        _exit(r == 0 ? 0 : r == EBUSY ? 16 : 99);
    }
    if (pid < 0) return -1;
    int st = wait_child(pid, 3000);
    if (st < 0 || !WIFEXITED(st)) return -1;
    return WEXITSTATUS(st) == 0 ? 0 : WEXITSTATUS(st) == 16 ? EBUSY : -1;
}

static void ex2(void) {
    printf("== ex2: fork in a multithreaded process ==\n");
    if (pipe(ready_pipe) != 0 || pipe(release_pipe) != 0) { CHECK(0); return; }
    pthread_t t;
    char c;
    pthread_create(&t, NULL, holder1, NULL);
    struct pollfd pf = { ready_pipe[0], POLLIN, 0 };
    CHECK(poll(&pf, 1, 3000) == 1 && read(ready_pipe[0], &c, 1) == 1 && c == 1);
    int r1 = child_trylock(&M1);                             /* holder1 은 자식에 없다 — 아무도 M1 을 풀 수 없다 */
    c = 0;
    (void)!write(release_pipe[1], &c, 1);
    pthread_join(t, NULL);
    printf("  no atfork : child trylock -> %s\n", r1 == EBUSY ? "EBUSY (stuck forever)" : r1 == 0 ? "ok" : "error");
    CHECK(r1 == EBUSY);

    pthread_atfork(at_prepare, at_parent, at_child);
    pthread_create(&t, NULL, holder2, NULL);
    CHECK(poll(&pf, 1, 3000) == 1 && read(ready_pipe[0], &c, 1) == 1 && c == 2);
    int r2 = child_trylock(&M2);                             /* prepare 가 holder2 가 놓기를 기다렸다가 fork */
    pthread_join(t, NULL);
    printf("  with atfork: child trylock -> %s\n", r2 == 0 ? "ok" : r2 == EBUSY ? "EBUSY" : "error");
    CHECK(r2 == 0);
    CHECK(pthread_mutex_trylock(&M2) == 0 && pthread_mutex_unlock(&M2) == 0);   /* 부모 쪽도 풀려 있다 */
    close(ready_pipe[0]); close(ready_pipe[1]); close(release_pipe[0]); close(release_pipe[1]);
}

/* ---------- 연습 3: 상속 제어 ---------- */
/* 영역을 "자식에게 안 물려줌" 으로 표시. 성공 0. */
static int inherit_none(void *p, size_t len) {
#if defined(__APPLE__)
    return minherit(p, len, VM_INHERIT_NONE);
#elif defined(MADV_DONTFORK)
    return madvise(p, len, MADV_DONTFORK);
#else
    (void)p; (void)len; return -1;
#endif
}

static void ex3(void) {
    printf("== ex3: minherit / MADV_DONTFORK ==\n");
    char *b = mmap(NULL, 8 * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    memset(b, 0x11, 8 * PG);
    CHECK(inherit_none(b, 8 * PG) == 0);
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGSEGV, SIG_DFL);                            /* ASan 빌드의 SEGV 핸들러를 걷어 내고 */
        signal(SIGBUS, SIG_DFL);                             /* 기본 동작(종료)으로 — 결과를 신호로 받으려고 */
        volatile char x = b[0];                              /* 이 영역은 자식에 없다 → SIGSEGV/SIGBUS */
        (void)x;
        _exit(0);
    }
    int st = pid > 0 ? wait_child(pid, 3000) : -1;
    printf("  NONE : child %s %d\n", (st >= 0 && WIFSIGNALED(st)) ? "killed by signal" : "exited",
           st < 0 ? -1 : WIFSIGNALED(st) ? WTERMSIG(st) : WEXITSTATUS(st));
    CHECK(st >= 0 && WIFSIGNALED(st) && (WTERMSIG(st) == SIGSEGV || WTERMSIG(st) == SIGBUS));
    CHECK((uint8_t)b[0] == 0x11);                            /* 부모 쪽은 멀쩡 */
    munmap(b, 8 * PG);

#if defined(__APPLE__)
    /* VM_INHERIT_SHARE: MAP_PRIVATE 로 만든 영역이 fork 뒤에는 부모·자식 공유가 된다 */
    char *s = mmap(NULL, 4 * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    memset(s, 0x22, 4 * PG);
    CHECK(minherit(s, 4 * PG, VM_INHERIT_SHARE) == 0);
    wctx sc = { s, 0, 4 };
    CHECK(child_faults(w_work, &sc) >= 0);
    printf("  SHARE: parent sees child's write: %s\n", (uint8_t)s[3 * PG] == 0x77 ? "yes" : "no");
    CHECK((uint8_t)s[0] == 0x77 && (uint8_t)s[3 * PG] == 0x77);
    munmap(s, 4 * PG);
#elif defined(MADV_WIPEONFORK)
    /* Linux: MADV_WIPEONFORK — 자식에는 0 으로 채워진 새 영역 */
    char *s = mmap(NULL, 4 * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    memset(s, 0x22, 4 * PG);
    CHECK(madvise(s, 4 * PG, MADV_WIPEONFORK) == 0);
    fflush(stdout);
    pid_t p2 = fork();
    if (p2 == 0) _exit(s[0] == 0 ? 0 : 1);
    int st2 = wait_child(p2, 3000);
    CHECK(st2 >= 0 && WIFEXITED(st2) && WEXITSTATUS(st2) == 0 && s[0] == 0x22);
    munmap(s, 4 * PG);
#endif
}

/* ---------- 연습 4: AoS vs SoA ---------- */
#define NREC 65536
typedef struct { uint32_t refcnt; uint32_t id; char payload[56]; } rec_t;   /* 64 B */
typedef struct { rec_t *aos; uint32_t *soa_ref; } layout_ctx;
static void bump_aos(void *c) { layout_ctx *l = c; for (size_t i = 0; i < NREC; i++) l->aos[i].refcnt++; }
static void bump_soa(void *c) { layout_ctx *l = c; for (size_t i = 0; i < NREC; i++) l->soa_ref[i]++; }

/* 바이트 [0, bytes) 를 덮는 페이지 수 (영역은 페이지 정렬) */
static size_t pages_for(size_t bytes) { return (bytes + PG - 1) / PG; }

static void ex4(void) {
    printf("== ex4: AoS vs SoA under COW ==\n");
    _Static_assert(sizeof(rec_t) == 64, "rec_t is 64 bytes");
    layout_ctx l;
    l.aos = mmap(NULL, NREC * sizeof(rec_t), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    l.soa_ref = mmap(NULL, NREC * sizeof(uint32_t), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    for (size_t i = 0; i < NREC; i++) { l.aos[i].refcnt = 1; l.aos[i].id = (uint32_t)i; l.soa_ref[i] = 1; }
    size_t pa = pages_for(NREC * sizeof(rec_t)), ps = pages_for(NREC * sizeof(uint32_t));
    long fa = child_faults(bump_aos, &l);
    long fs = child_faults(bump_soa, &l);
    printf("  AoS: predicted %zu pages, child +%ld | SoA: predicted %zu pages, child +%ld\n", pa, fa, ps, fs);
    CHECK(pa == 16 * ps);                                    /* 64 B 레코드 vs 4 B 카운터 = 16 배 */
    /* refcnt++ 는 읽고-쓰기: 페이지당 1 (x86 Linux 예상) ~ 2 (이 맥 · arm64 VM 실측) 폴트 */
    CHECK(fa >= (long)pa && fa <= 2 * (long)pa + SLACK(pa));
    CHECK(fs >= (long)ps && fs <= 2 * (long)ps + SLACK(ps));
    CHECK(fa > 4 * fs);
    CHECK(l.aos[7].refcnt == 1 && l.soa_ref[7] == 1);        /* 부모 데이터는 그대로 */
    munmap(l.aos, NREC * sizeof(rec_t));
    munmap(l.soa_ref, NREC * sizeof(uint32_t));
}

int main(void) {
    alarm(30);
    long pg = sysconf(_SC_PAGESIZE);
    PG = pg > 0 ? (size_t)pg : 4096;
    printf("page size %zu\n", PG);
    ex1();
    ex2();
    ex3();
    ex4();
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
