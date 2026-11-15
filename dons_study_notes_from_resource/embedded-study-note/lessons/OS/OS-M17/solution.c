/* ENV: linux */
/* BUILD: -pthread */
/* OS-M17 프로세스 간 공유 동기화: PROCESS_SHARED 뮤텍스와 robust mutex
 *
 * 공유 메모리(/dev/shm) 한 장 = { pthread_mutex_t + pthread_cond_t 2개 + 링 버퍼 + 메타데이터 + undo 저널 }.
 * 서로 다른 프로세스가 shm_open 으로 같은 이름을 열고 mmap 해서 같은 락·조건변수를 쓴다.
 *
 *   Part A  shring_create / shring_open / shring_destroy
 *           (shm_open + ftruncate + mmap, PTHREAD_PROCESS_SHARED 뮤텍스·조건변수, CLOCK_MONOTONIC 조건변수)
 *           → 생산자 프로세스 1 + 소비자 프로세스 1, 20000 개를 체크섬·순서로 검증
 *   Part B  락을 쥔 프로세스를 SIGKILL: 일반 뮤텍스는 영원히 잠김(timedlock → ETIMEDOUT 로 "멈추지 않고" 확인)
 *           robust 뮤텍스는 EOWNERDEAD → ring_repair(undo 저널로 되돌림) → pthread_mutex_consistent → 계속
 *           consistent 를 안 부르고 unlock → 이후 모든 lock 이 ENOTRECOVERABLE
 *   Part C  센서 데몬 재시작: 조건변수 대기 중에 주인이 죽으면 pthread_cond_timedwait 가 EOWNERDEAD 를 돌려준다.
 *           죽은 생산자가 못 보낸 signal 은 "짧은 조각 대기"로 스스로 회복, 재시작한 생산자는 shm 의 tail 부터 이어 쓴다.
 *
 * 리눅스(glibc) 전용: macOS 에는 pthread_mutexattr_setrobust 자체가 없다 (MacOSX26.4.sdk pthread.h 에 선언 없음).
 * CHECK 는 매 실행 항상 성립하는 것만 본다 (반환값·불변식·체크섬). 시간은 watchdog 으로만 쓴다.
 * build (Lima VM elinux): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
 */
#define _POSIX_C_SOURCE 200809L /* shm_open, ftruncate, pthread_mutexattr_setrobust, pthread_mutex_consistent */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define GIVEN __attribute__((unused)) /* 시작 코드가 아직 안 부르는 도우미의 -Wunused 경고 끄기 */

/* ======================= 공유 영역 레이아웃 (주어짐) ======================= */
enum { RING_CAP = 16 };
#define RING_MAGIC 0x4F534D17u /* "OSM" 17 */

typedef struct {               /* 한 번에 같이 바뀌어야 하는 값들 = 불변식의 단위 */
    uint32_t head, tail;       /* 단조 증가 카운터. 슬롯 = 카운터 % RING_CAP */
    uint32_t count;            /* 불변식: count == tail - head, count <= RING_CAP */
    uint64_t sum_put, sum_get; /* 불변식: sum_put - sum_get == 링 안 값들의 합 */
} meta_t;

typedef struct {
    uint32_t magic, robust;
    pthread_mutex_t mu;                 /* PTHREAD_PROCESS_SHARED (+ PTHREAD_MUTEX_ROBUST) */
    pthread_cond_t not_empty, not_full; /* PTHREAD_PROCESS_SHARED + CLOCK_MONOTONIC */
    meta_t m;                           /* 현재 상태 */
    meta_t j;                           /* undo 저널: 수정 시작 직전의 m */
    uint32_t j_active;                  /* 1 = m 을 고치는 중 (이때 죽으면 j 로 되돌린다) */
    uint32_t buf[RING_CAP];
    uint32_t recoveries, rollbacks;     /* EOWNERDEAD 처리 횟수, 그중 되돌린 횟수 */
    uint32_t rec_in_wait;               /* 그중 pthread_cond_timedwait 가 EOWNERDEAD 를 돌려준 횟수 */
    uint32_t nwait_get, nwait_put;      /* 조건변수에서 자는 수 (테스트 동기화용) */
    uint32_t hello;                     /* Part A: 자식이 쓴 pid */
} shring_t;

/* ======================= 시간 · 프로세스 도우미 (주어짐) ======================= */
static struct timespec deadline_on(clockid_t clk, int ms) {
    struct timespec t;
    clock_gettime(clk, &t);
    t.tv_sec += ms / 1000;
    t.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (t.tv_nsec >= 1000000000L) { t.tv_sec++; t.tv_nsec -= 1000000000L; }
    return t;
}
GIVEN static int ts_passed(clockid_t clk, const struct timespec *dl) {
    struct timespec now;
    clock_gettime(clk, &now);
    return now.tv_sec > dl->tv_sec || (now.tv_sec == dl->tv_sec && now.tv_nsec >= dl->tv_nsec);
}
static void sleep_ms(int ms) {
    struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };
    while (nanosleep(&t, &t) != 0 && errno == EINTR) {}
}
/* 자식을 기다린다. timeout_ms 안에 안 끝나면 SIGKILL 하고 -1. 끝나면 waitpid 의 status. */
static int wait_child(pid_t pid, int timeout_ms) {
    int st = 0;
    for (int t = 0; t < timeout_ms; t++) {
        pid_t w = waitpid(pid, &st, WNOHANG);
        if (w == pid) return st;
        if (w < 0) return -1;
        sleep_ms(1);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &st, 0);
    return -1;
}
static int exited_ok(int st) { return st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0; }
static int killed_by(int st, int sig) { return st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == sig; }
static pid_t fork_child(void) {      /* 자식은 5 초 alarm watchdog 을 달고 시작한다 */
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) alarm(5);
    return p;
}

/* ======================= 크래시 주입 (주어짐) ======================= */
enum { CR_NONE = 0, CR_LOCKED, CR_MID, CR_COMMITTED }; /* 락 직후 / m 수정 도중 / 커밋 후·signal 전 */
static int g_crash_pipe = -1; /* >= 0 이면 죽는 대신 부모에게 1 바이트 알리고 멈춤 → 부모가 SIGKILL */
static void crash_here(void) {
    if (g_crash_pipe >= 0) {
        char c = 'k';
        if (write(g_crash_pipe, &c, 1) != 1) _exit(99);
        for (;;) pause();
    }
    raise(SIGKILL); /* 락을 쥔 채 즉사: unlock 도, atexit 도, 아무것도 안 돈다 */
}
#define CRASH(mode, point) do { if ((mode) == (point)) crash_here(); } while (0)

/* undo 저널: 컴파일러가 j 복사와 j_active=1 의 순서를 바꾸지 못하게 signal fence (같은 CPU 의 "죽음" 에는 충분) */
static void j_begin(shring_t *r) {
    r->j = r->m;
    atomic_signal_fence(memory_order_seq_cst);
    r->j_active = 1;
    atomic_signal_fence(memory_order_seq_cst);
}
static void j_commit(shring_t *r) {
    atomic_signal_fence(memory_order_seq_cst);
    r->j_active = 0;
    atomic_signal_fence(memory_order_seq_cst);
}

/* 불변식 검사: 위반 개수 (0 = 일관) */
static int ring_check(const shring_t *r) {
    int bad = 0;
    if (r->magic != RING_MAGIC) bad++;
    if (r->m.count != r->m.tail - r->m.head) bad++;
    if (r->m.count > RING_CAP) bad++;
    if (r->j_active) bad++;
    uint64_t live = 0;
    for (uint32_t i = r->m.head; i != r->m.tail && i - r->m.head <= RING_CAP; i++) live += r->buf[i % RING_CAP];
    if (r->m.sum_put - r->m.sum_get != live) bad++;
    return bad;
}

/* ======================= Part A — 공유 링 만들기 / 열기 / 지우기 ======================= */
/* TODO(A1) shm_open(O_CREAT|O_EXCL|O_RDWR, 0600) → ftruncate(sizeof) → mmap(MAP_SHARED) → fd 닫기.
 *          뮤텍스: pshared = PTHREAD_PROCESS_SHARED, robust 면 PTHREAD_MUTEX_ROBUST.
 *          조건변수 2 개: pshared + CLOCK_MONOTONIC. magic/robust 기록. 실패 시 만든 것 정리하고 NULL. */
static shring_t *shring_create(const char *name, int robust) {
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) return NULL;                       /* 이미 있으면 EEXIST — 남의 것을 덮어 초기화하지 않는다 */
    if (ftruncate(fd, (off_t)sizeof(shring_t)) != 0) { close(fd); shm_unlink(name); return NULL; }
    void *p = mmap(NULL, sizeof(shring_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);                                     /* 매핑은 fd 를 닫아도 유지된다 */
    if (p == MAP_FAILED) { shm_unlink(name); return NULL; }
    shring_t *r = p;                               /* ftruncate 로 늘린 부분은 0 으로 채워져 있다 */

    pthread_mutexattr_t ma;
    pthread_condattr_t ca;
    int e = pthread_mutexattr_init(&ma);
    if (!e) e = pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
    if (!e && robust) e = pthread_mutexattr_setrobust(&ma, PTHREAD_MUTEX_ROBUST);
    if (!e) e = pthread_mutex_init(&r->mu, &ma);
    pthread_mutexattr_destroy(&ma);
    if (!e) e = pthread_condattr_init(&ca);
    if (!e) e = pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED);
    if (!e) e = pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    if (!e) e = pthread_cond_init(&r->not_empty, &ca);
    if (!e) e = pthread_cond_init(&r->not_full, &ca);
    pthread_condattr_destroy(&ca);
    if (e) { munmap(p, sizeof(shring_t)); shm_unlink(name); errno = e; return NULL; }
    r->robust = (uint32_t)robust;
    r->magic = RING_MAGIC;                         /* 마지막에: 여는 쪽은 magic 으로 "초기화 끝" 을 확인 */
    return r;
}

/* TODO(A2) 이미 있는 이름을 연다: shm_open(O_RDWR) → fstat 으로 크기 확인 → mmap → fd 닫기 → magic 확인. 실패 NULL. */
static shring_t *shring_open(const char *name) {
    int fd = shm_open(name, O_RDWR, 0);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size != (off_t)sizeof(shring_t)) { close(fd); errno = EINVAL; return NULL; }
    void *p = mmap(NULL, sizeof(shring_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return NULL;
    shring_t *r = p;
    if (r->magic != RING_MAGIC) { munmap(p, sizeof(shring_t)); errno = EINVAL; return NULL; }
    return r;
}

/* TODO(A3) 만든 쪽의 정리: 조건변수·뮤텍스 destroy, munmap, shm_unlink. 뮤텍스 destroy 결과(0/EBUSY…)를 돌려준다. */
static int shring_destroy(shring_t *r, const char *name) {
    pthread_cond_destroy(&r->not_empty);  /* 모든 대기자가 살아서 나간 뒤에만 부른다 (5. 풀이 Part C 주의) */
    pthread_cond_destroy(&r->not_full);
    int e = pthread_mutex_destroy(&r->mu);
    munmap(r, sizeof(shring_t));
    shm_unlink(name);
    return e;
}

/* ======================= Part B — 주인 사망 복구 ======================= */
/* TODO(B1) undo 저널로 m 을 되돌린다. j_active 면 m = j, j_active = 0, rollbacks++ → 1.
 *          저널이 없는데도 불변식이 깨져 있으면(있을 수 없어야 하지만) 링을 비워 초기화 → 2. 멀쩡하면 0. */
static int ring_repair(shring_t *r) {
    if (r->j_active) {
        r->m = r->j;
        r->j_active = 0;
        r->rollbacks++;
        return 1;
    }
    if (ring_check(r) != 0) {              /* 최후 수단: 데이터를 버리고 빈 링으로 */
        memset(&r->m, 0, sizeof r->m);
        return 2;
    }
    return 0;
}

/* 회복 절차 (주어짐, ring_lock 과 ring_wait 가 공유): 뮤텍스를 EOWNERDEAD 로 받은 직후, 즉 "내가 주인" 일 때만 부른다. */
static void recover_locked(shring_t *r, int in_wait);

/* TODO(B2) CLOCK_REALTIME 마감으로 pthread_mutex_timedlock. 0 → 0. EOWNERDEAD → recover_locked(r, 0) → 0.
 *          ETIMEDOUT / ENOTRECOVERABLE / 그 밖 → 그대로 돌려준다 (락 안 쥠). */
static int ring_lock(shring_t *r, int timeout_ms) {
    struct timespec dl = deadline_on(CLOCK_REALTIME, timeout_ms); /* timedlock 은 POSIX 상 CLOCK_REALTIME */
    int e = pthread_mutex_timedlock(&r->mu, &dl);
    if (e == EOWNERDEAD) { recover_locked(r, 0); return 0; }
    return e;
}

/* ======================= Part C — 조건변수 대기와 재시작 ======================= */
enum { WAIT_SLICE_MS = 50 };
/* TODO(C1) 뮤텍스를 쥔 채 호출. min(지금+50 ms, dl) 까지 pthread_cond_timedwait (조건변수는 CLOCK_MONOTONIC).
 *          EOWNERDEAD → recover_locked(r, 1) → 0 (호출자가 조건을 다시 본다). ETIMEDOUT → dl 이 지났으면 ETIMEDOUT, 아니면 0.
 *          0 → 0. ENOTRECOVERABLE 등 → 그대로 (이때는 락을 안 쥐고 있다). */
static int ring_wait(shring_t *r, pthread_cond_t *cv, const struct timespec *dl) {
    struct timespec slice = deadline_on(CLOCK_MONOTONIC, WAIT_SLICE_MS);
    if (slice.tv_sec > dl->tv_sec || (slice.tv_sec == dl->tv_sec && slice.tv_nsec > dl->tv_nsec)) slice = *dl;
    int e = pthread_cond_timedwait(cv, &r->mu, &slice);
    if (e == EOWNERDEAD) { recover_locked(r, 1); return 0; }
    if (e == ETIMEDOUT) return ts_passed(CLOCK_MONOTONIC, dl) ? ETIMEDOUT : 0;
    return e;
}

GIVEN static void recover_locked(shring_t *r, int in_wait) {
    ring_repair(r);                          /* ① 데이터 불변식부터 고친다 */
    pthread_cond_broadcast(&r->not_empty);   /* ② 죽은 주인이 못 보낸 signal 을 대신: 모두 깨워 조건을 다시 보게 */
    pthread_cond_broadcast(&r->not_full);
    pthread_mutex_consistent(&r->mu);        /* ③ 이제 "일관됨" 선언. 안 하면 unlock 순간 ENOTRECOVERABLE */
    r->recoveries++;
    r->rec_in_wait += (uint32_t)in_wait;
}

/* 링 연산 (주어짐). crash = CR_* 로 그 지점에서 죽는다. 0 / ETIMEDOUT / ENOTRECOVERABLE */
static int ring_put(shring_t *r, uint32_t v, int timeout_ms, int crash) {
    struct timespec dl = deadline_on(CLOCK_MONOTONIC, timeout_ms);
    int e = ring_lock(r, timeout_ms);
    if (e) return e;
    CRASH(crash, CR_LOCKED);
    while (r->m.count == RING_CAP) {
        r->nwait_put++;
        e = ring_wait(r, &r->not_full, &dl);
        if (e == ENOTRECOVERABLE) return e;            /* 락을 안 쥐고 있다 */
        r->nwait_put--;
        if (e) { pthread_mutex_unlock(&r->mu); return e; }
    }
    j_begin(r);
    r->buf[r->m.tail % RING_CAP] = v;
    r->m.tail++;
    CRASH(crash, CR_MID);                              /* tail 은 올렸는데 count·sum 은 아직 */
    r->m.count++;
    r->m.sum_put += v;
    j_commit(r);
    CRASH(crash, CR_COMMITTED);                        /* 커밋했지만 signal·unlock 을 못 함 */
    pthread_cond_signal(&r->not_empty);
    pthread_mutex_unlock(&r->mu);
    return 0;
}
static int ring_get(shring_t *r, uint32_t *out, int timeout_ms) {
    struct timespec dl = deadline_on(CLOCK_MONOTONIC, timeout_ms);
    int e = ring_lock(r, timeout_ms);
    if (e) return e;
    while (r->m.count == 0) {
        r->nwait_get++;
        e = ring_wait(r, &r->not_empty, &dl);
        if (e == ENOTRECOVERABLE) return e;
        r->nwait_get--;
        if (e) { pthread_mutex_unlock(&r->mu); return e; }
    }
    j_begin(r);
    uint32_t v = r->buf[r->m.head % RING_CAP];
    r->m.head++;
    r->m.count--;
    r->m.sum_get += v;
    j_commit(r);
    pthread_cond_signal(&r->not_full);
    pthread_mutex_unlock(&r->mu);
    *out = v;
    return 0;
}

static uint32_t VAL(uint32_t seq) { return seq * 2654435761u ^ 0x5bd1e995u; } /* seq → 값 (소비자가 재계산) */

/* TODO(C2) 생산자 한 생애: 공유 메모리의 커밋된 tail 부터 n-1 까지 VAL(seq) 를 넣는다.
 *          seq == crash_seq 인 put 만 crash 모드로. (tail 은 ring_lock 으로 읽는다 — 그 lock 이 회복 지점일 수 있다)
 *          0 = 끝까지 넣음, 그 밖 = ring_put/ring_lock 오류. */
static int producer_run(shring_t *r, uint32_t n, uint32_t crash_seq, int crash) {
    int e = ring_lock(r, 2000);
    if (e) return e;
    uint32_t seq = r->m.tail;            /* 진실은 프로세스 메모리가 아니라 shm 에 있다 */
    pthread_mutex_unlock(&r->mu);
    for (; seq < n; seq++) {
        e = ring_put(r, VAL(seq), 2000, seq == crash_seq ? crash : CR_NONE);
        if (e) return e;
    }
    return 0;
}

/* 감시자 (주어짐): 생산자를 띄우고, SIGKILL 로 죽으면 다음 크래시 계획으로 다시 띄운다. 재시작 횟수 또는 -1. */
typedef struct { uint32_t seq; int mode; } plan_t;
static int supervise(const char *name, uint32_t n, const plan_t *plans, int nplans) {
    for (int life = 0; life <= nplans; life++) {
        pid_t p = fork_child();
        if (p == 0) {
            shring_t *r = shring_open(name);
            if (!r) _exit(3);
            uint32_t cs = life < nplans ? plans[life].seq : UINT32_MAX;
            int cm = life < nplans ? plans[life].mode : CR_NONE;
            _exit(producer_run(r, n, cs, cm) == 0 ? 0 : 4);
        }
        int st = wait_child(p, 4000);
        if (exited_ok(st)) return life;              /* life = 지금까지 재시작한 횟수 */
        if (!killed_by(st, SIGKILL)) return -1;
    }
    return -1;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s  (line %d)\n", #expr, __LINE__); } } while (0)

static char g_names[8][64];
static const char *mkname(int i, const char *tag) {
    snprintf(g_names[i], sizeof g_names[i], "/OS-M17_%d_%s", (int)getpid(), tag);
    return g_names[i];
}
static int count_leftover_shm(void) {    /* /dev/shm 에 우리 pid 이름이 남았나 */
    char pre[32];
    snprintf(pre, sizeof pre, "OS-M17_%d_", (int)getpid());
    DIR *d = opendir("/dev/shm");
    if (!d) return -1;
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) if (strncmp(de->d_name, pre, strlen(pre)) == 0) n++;
    closedir(d);
    return n;
}
static int raw_timedlock(pthread_mutex_t *m, int ms) {
    struct timespec dl = deadline_on(CLOCK_REALTIME, ms);
    return pthread_mutex_timedlock(m, &dl);
}
/* 자식: 락만 잡고 즉사 */
static pid_t spawn_lock_and_die(const char *name) {
    pid_t p = fork_child();
    if (p == 0) {
        shring_t *r = shring_open(name);
        if (!r || pthread_mutex_lock(&r->mu) != 0) _exit(3);
        crash_here();
    }
    return p;
}
/* 자식: raw timedlock 결과 errno 를 exit code 로 (0 이면 unlock 후 0) */
static pid_t spawn_try(const char *name, int ms) {
    pid_t p = fork_child();
    if (p == 0) {
        shring_t *r = shring_open(name);
        if (!r) _exit(250);
        int e = raw_timedlock(&r->mu, ms);
        if (e == 0) pthread_mutex_unlock(&r->mu);
        _exit(e);
    }
    return p;
}
static int exit_code(int st) { return (st != -1 && WIFEXITED(st)) ? WEXITSTATUS(st) : -1; }

static void part_a(void) {
    printf("== Part A: shm_open + mmap 위의 PROCESS_SHARED 뮤텍스·조건변수 ==\n");
    const char *nm = mkname(0, "a");
    shring_t *r = shring_create(nm, 0);
    CHECK(r != NULL);
    if (!r) return;
    struct stat st;
    char path[96];
    snprintf(path, sizeof path, "/dev/shm%s", nm);
    CHECK(stat(path, &st) == 0 && st.st_size == (off_t)sizeof(shring_t) && (st.st_mode & 0777) == 0600);
    errno = 0;
    CHECK(shring_create(nm, 0) == NULL && errno == EEXIST);       /* O_EXCL: 남의 락을 다시 init 하지 않는다 */

    pid_t p = fork_child();                                        /* 자식이 이름으로 따로 연다 */
    if (p == 0) {
        shring_t *c = shring_open(nm);
        if (!c || pthread_mutex_lock(&c->mu) != 0) _exit(3);
        c->hello = (uint32_t)getpid();
        pthread_mutex_unlock(&c->mu);
        _exit(0);
    }
    int s = wait_child(p, 3000);
    CHECK(exited_ok(s) && r->hello == (uint32_t)p);

    enum { N = 20000 };
    pid_t prod = fork_child();
    if (prod == 0) {
        shring_t *c = shring_open(nm);
        if (!c) _exit(3);
        uint32_t x = 2463534242u;
        for (int i = 0; i < N; i++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            if (ring_put(c, x, 2000, CR_NONE) != 0) _exit(4);
        }
        _exit(0);
    }
    pid_t cons = fork_child();
    if (cons == 0) {
        shring_t *c = shring_open(nm);
        if (!c) _exit(3);
        uint32_t x = 2463534242u, v = 0;
        int bad = 0;
        for (int i = 0; i < N; i++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            if (ring_get(c, &v, 2000) != 0) _exit(4);
            if (v != x) bad++;
        }
        _exit(bad ? 5 : 0);
    }
    int sp = wait_child(prod, 4000), sc = wait_child(cons, 4000);
    printf("  producer exit=%d consumer exit=%d  sum_put=%llu sum_get=%llu head=%u tail=%u\n",
           exit_code(sp), exit_code(sc), (unsigned long long)r->m.sum_put, (unsigned long long)r->m.sum_get,
           r->m.head, r->m.tail);
    CHECK(exited_ok(sp) && exited_ok(sc));                         /* 소비자: 순서·값 20000 개 모두 일치 */
    CHECK(r->m.head == N && r->m.tail == N && r->m.count == 0 && r->m.sum_put == r->m.sum_get);
    CHECK(ring_check(r) == 0 && r->recoveries == 0);
    CHECK(shring_destroy(r, nm) == 0);
    errno = 0;
    CHECK(shm_open(nm, O_RDWR, 0) < 0 && errno == ENOENT);
}

static void part_b(void) {
    printf("== Part B: 락을 쥔 프로세스를 SIGKILL ==\n");
    /* B-1 일반(비 robust) 뮤텍스: 영원히 잠긴다 */
    const char *nn = mkname(1, "normal");
    shring_t *r = shring_create(nn, 0);
    CHECK(r != NULL);
    if (r) {
        int s = wait_child(spawn_lock_and_die(nn), 3000);
        CHECK(killed_by(s, SIGKILL));
        int e1 = raw_timedlock(&r->mu, 150);
        int e2 = exit_code(wait_child(spawn_try(nn, 150), 3000));   /* 제3의 프로세스도 */
        printf("  normal: parent timedlock=%s  other process=%s\n", strerror(e1), e2 >= 0 ? strerror(e2) : "?");
        CHECK(e1 == ETIMEDOUT && e2 == ETIMEDOUT);
        int ed = shring_destroy(r, nn);                            /* 잠긴 채 destroy: glibc 는 EBUSY */
        printf("  normal: pthread_mutex_destroy on dead-locked mutex = %d (%s)\n", ed, strerror(ed));
    }

    /* B-2 robust: 자식이 락만 잡고 자살 → EOWNERDEAD → 복구 */
    const char *nr = mkname(2, "robust");
    r = shring_create(nr, 1);
    CHECK(r != NULL);
    if (!r) return;
    CHECK(ring_put(r, 7, 500, CR_NONE) == 0);
    int s = wait_child(spawn_lock_and_die(nr), 3000);
    CHECK(killed_by(s, SIGKILL));
    CHECK(ring_lock(r, 500) == 0 && r->recoveries == 1 && r->rollbacks == 0 && ring_check(r) == 0);
    pthread_mutex_unlock(&r->mu);
    CHECK(ring_lock(r, 500) == 0 && r->recoveries == 1);           /* 두 번째부터는 평범한 lock */
    pthread_mutex_unlock(&r->mu);

    /* B-3 고치는 도중에 부모가 SIGKILL: 저널로 되돌린다 */
    meta_t before = r->m;
    int pfd[2];
    CHECK(pipe(pfd) == 0);
    pid_t p = fork_child();
    if (p == 0) {
        shring_t *c = shring_open(nr);
        if (!c) _exit(3);
        close(pfd[0]);
        g_crash_pipe = pfd[1];
        ring_put(c, 99, 500, CR_MID);                              /* CR_MID 에서 부모에게 알리고 멈춘다 */
        _exit(0);
    }
    close(pfd[1]);
    char ch = 0;
    CHECK(read(pfd[0], &ch, 1) == 1 && ch == 'k');                 /* 자식이 tail++ 만 하고 멈춘 상태 */
    close(pfd[0]);
    kill(p, SIGKILL);
    CHECK(killed_by(wait_child(p, 3000), SIGKILL));
    CHECK(r->j_active == 1 && r->m.tail == before.tail + 1 && ring_check(r) > 0); /* 찢어진 상태가 보인다 */
    CHECK(ring_lock(r, 500) == 0 && r->recoveries == 2 && r->rollbacks == 1);
    CHECK(memcmp(&r->m, &before, sizeof before) == 0 && ring_check(r) == 0);
    pthread_mutex_unlock(&r->mu);
    uint32_t v = 0;
    CHECK(ring_get(r, &v, 500) == 0 && v == 7 && ring_check(r) == 0); /* 99 는 없던 일, 7 은 그대로 */

    /* B-4 consistent 를 안 부르면: ENOTRECOVERABLE (모든 프로세스, 영원히) */
    s = wait_child(spawn_lock_and_die(nr), 3000);
    CHECK(killed_by(s, SIGKILL));
    CHECK(raw_timedlock(&r->mu, 500) == EOWNERDEAD);
    CHECK(pthread_mutex_unlock(&r->mu) == 0);                      /* consistent 없이 unlock */
    int e1 = raw_timedlock(&r->mu, 100);
    int e2 = exit_code(wait_child(spawn_try(nr, 100), 3000));
    printf("  robust, no consistent: parent=%s  other process=%s\n", strerror(e1), e2 >= 0 ? strerror(e2) : "?");
    CHECK(e1 == ENOTRECOVERABLE && e2 == ENOTRECOVERABLE);
    CHECK(ring_lock(r, 100) == ENOTRECOVERABLE);
    CHECK(shring_destroy(r, nr) == 0);                             /* destroy 는 허용된다 */
}

static void part_c(void) {
    printf("== Part C: 조건변수 대기 중 주인 사망, 생산자 재시작 ==\n");
    const char *nm = mkname(3, "cond");
    shring_t *r = shring_create(nm, 1);
    CHECK(r != NULL);
    if (!r) return;
    /* C-1 소비자가 not_empty 에서 자는 중, 생산자가 커밋하고 signal 전에 죽음 → 소비자가 스스로 회복해 값을 받는다 */
    for (int round = 0; round < 2; round++) {
        int mode = round == 0 ? CR_COMMITTED : CR_MID;
        uint32_t rec0 = r->recoveries, w0 = r->rec_in_wait, tail0 = r->m.tail;
        pid_t cons = fork_child();
        if (cons == 0) {
            shring_t *c = shring_open(nm);
            if (!c) _exit(3);
            uint32_t v = 0;
            int e = ring_get(c, &v, mode == CR_COMMITTED ? 2000 : 400);
            if (mode == CR_COMMITTED) _exit(e == 0 && v == 4242 ? 0 : 10);
            _exit(e == ETIMEDOUT ? 0 : 11);                         /* 되돌려졌으니 받을 값이 없다 */
        }
        int ok = 0;                                                 /* 소비자가 조건변수에서 잠들 때까지 */
        for (int t = 0; t < 2000 && !ok; t++) {
            if (ring_lock(r, 500) == 0) { ok = r->nwait_get == 1; pthread_mutex_unlock(&r->mu); }
            if (!ok) sleep_ms(1);
        }
        CHECK(ok);
        pid_t prod = fork_child();
        if (prod == 0) {
            shring_t *c = shring_open(nm);
            if (!c) _exit(3);
            ring_put(c, 4242, 500, mode);
            _exit(0);
        }
        CHECK(killed_by(wait_child(prod, 3000), SIGKILL));
        int sc = wait_child(cons, 3000);
        printf("  %s: consumer exit=%d recoveries %u→%u tail %u→%u\n", mode == CR_COMMITTED ? "CR_COMMITTED" : "CR_MID",
               exit_code(sc), rec0, r->recoveries, tail0, r->m.tail);
        CHECK(exited_ok(sc) && r->recoveries == rec0 + 1 && ring_check(r) == 0);
        CHECK(r->rec_in_wait == w0 + 1);                            /* 회복은 소비자의 cond_timedwait 안에서 */
        CHECK(r->m.tail == tail0 + (mode == CR_COMMITTED ? 1u : 0u));
    }

    /* C-3 감시자: 생산자를 세 번 다른 지점에서 죽이고 재시작. 소비자는 0..N-1 을 정확히 한 번씩, 순서대로 */
    enum { N = 3000 };
    const char *n2 = mkname(4, "sup");
    shring_t *q = shring_create(n2, 1);
    CHECK(q != NULL);
    if (!q) { shring_destroy(r, nm); return; }
    pid_t cons = fork_child();
    if (cons == 0) {
        shring_t *c = shring_open(n2);
        if (!c) _exit(3);
        for (uint32_t s = 0; s < N; s++) {
            uint32_t v = 0;
            if (ring_get(c, &v, 3000) != 0) _exit(4);
            if (v != VAL(s)) _exit(5);
        }
        _exit(0);
    }
    const plan_t plans[] = { { 100, CR_LOCKED }, { 1200, CR_MID }, { 2500, CR_COMMITTED } };
    int restarts = supervise(n2, N, plans, 3);
    int sc = wait_child(cons, 4000);
    printf("  supervisor: restarts=%d consumer exit=%d recoveries=%u rollbacks=%u tail=%u sum ok=%d\n", restarts,
           exit_code(sc), q->recoveries, q->rollbacks, q->m.tail, q->m.sum_put == q->m.sum_get);
    CHECK(restarts == 3 && exited_ok(sc));
    CHECK(q->m.tail == N && q->m.head == N && q->m.sum_put == q->m.sum_get && ring_check(q) == 0);
    CHECK(q->recoveries == 3 && q->rollbacks == 1);
    CHECK(shring_destroy(q, n2) == 0 && shring_destroy(r, nm) == 0);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    part_a();
    part_b();
    part_c();
    for (int i = 0; i < 8; i++) if (g_names[i][0]) shm_unlink(g_names[i]); /* 실패 경로의 잔여물 정리 */
    CHECK(count_leftover_shm() == 0);
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
