/* OS-A11 추가 연습 — 데몬을 키울 때 바로 만나는 다섯 가지. 전부 결정적으로 검증한다.
 *   연습 1  self-pipe 시그널 (EVFILT_SIGNAL 없는 환경의 표준 해법) + kill(getpid()) vs raise() 차이
 *   연습 2  슬랩 이중 해제·범위 밖 포인터 검출 (객체 상태 표)
 *   연습 3  로그 컴팩션: 새 파일 → fsync → rename, 충돌 지점 주입
 *   연습 4  연결당 동시 요청 여러 개: 순서 버퍼(reorder buffer) 로 응답 순서 복원
 *   연습 5  부하 차단 (load shedding): 상한·하한 히스테리시스
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_total, g_pass;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
    else printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---- 연습 1: self-pipe ----
 * 핸들러 안에서는 async-signal-safe 함수만 부를 수 있다. write(2) 는 그 목록에 있다.
 * errno 를 저장·복원해야 하는 이유: 핸들러가 끼어든 코드가 방금 실패한 호출의 errno 를 읽고 있을 수 있다. */
static int g_pipe[2];
static void on_sig(int sig) {
    int saved = errno;
    unsigned char b = (unsigned char)sig;
    (void)!write(g_pipe[1], &b, 1);      /* O_NONBLOCK: 파이프가 가득 차면 EAGAIN — 버려도 된다(깨우기가 목적) */
    errno = saved;
}
static void test_self_pipe(void) {
    CHECK(pipe(g_pipe) == 0);
    (void)fcntl(g_pipe[0], F_SETFL, O_NONBLOCK);
    (void)fcntl(g_pipe[1], F_SETFL, O_NONBLOCK);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sig;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    CHECK(sigaction(SIGUSR1, &sa, NULL) == 0);

    errno = EDOM;
    raise(SIGUSR1); raise(SIGUSR1); raise(SIGUSR1);
    CHECK(errno == EDOM);                /* 핸들러가 errno 를 안 건드렸다 */
    unsigned char buf[16];
    ssize_t n = read(g_pipe[0], buf, sizeof buf);
    CHECK(n == 3 && buf[0] == SIGUSR1);

    /* 파이프가 가득 차도 핸들러는 막히지 않는다 → 프로세스가 멈추지 않는다 */
    for (int i = 0; i < 200000; i++) raise(SIGUSR1);
    size_t drained = 0;
    unsigned char big[4096];
    for (;;) { ssize_t r = read(g_pipe[0], big, sizeof big); if (r <= 0) break; drained += (size_t)r; }
    CHECK(drained > 0 && drained < 200000);   /* 쌓인 건 일부뿐: 시그널 "개수"를 믿으면 안 된다 */
    close(g_pipe[0]); close(g_pipe[1]);

    /* EVFILT_SIGNAL: process-directed 만 잡힌다. raise() 는 호출 스레드로 간다 */
    signal(SIGUSR2, SIG_IGN);
    int kq = kqueue();
    struct kevent ke;
    EV_SET(&ke, SIGUSR2, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
    CHECK(kevent(kq, &ke, 1, NULL, 0, NULL) == 0);
    struct timespec z = { 0, 0 }, wait10 = { 0, 10000000 };
    raise(SIGUSR2);
    int got = kevent(kq, NULL, 0, &ke, 1, &wait10);
    printf("      raise() 뒤 EVFILT_SIGNAL 이벤트 수: %d\n", got);
    CHECK(got == 0);
    kill(getpid(), SIGUSR2);
    kill(getpid(), SIGUSR2);
    kill(getpid(), SIGUSR2);
    got = kevent(kq, NULL, 0, &ke, 1, &wait10);
    printf("      kill(getpid()) x3 뒤: events=%d, data(횟수)=%lld\n", got, got > 0 ? (long long)ke.data : -1LL);
    CHECK(got == 1 && ke.data == 3);
    CHECK(kevent(kq, NULL, 0, &ke, 1, &z) == 0);   /* 읽으면 사라진다 */
    close(kq);
}

/* ---- 연습 2: 이중 해제 검출 ---- */
#define POOL_N 8
#define POOL_SZ 32
typedef struct {
    unsigned char mem[POOL_N][POOL_SZ];
    uint8_t state[POOL_N];               /* 1 = 나가 있음 */
} pool_t;
static void pool_init(pool_t *p) { memset(p, 0, sizeof *p); }
static void *pool_alloc(pool_t *p) {
    for (int i = 0; i < POOL_N; i++)
        if (!p->state[i]) { p->state[i] = 1; return p->mem[i]; }
    return NULL;
}
/* 0 정상, -1 풀 밖/정렬 어긋남, -2 이중 해제 */
static int pool_free(pool_t *p, void *q) {
    uintptr_t base = (uintptr_t)p->mem, a = (uintptr_t)q;
    if (a < base || a >= base + sizeof p->mem || (a - base) % POOL_SZ != 0) return -1;
    size_t i = (a - base) / POOL_SZ;
    if (!p->state[i]) return -2;
    p->state[i] = 0;
    return 0;
}
static void test_pool(void) {
    pool_t p;
    pool_init(&p);
    void *a = pool_alloc(&p);
    CHECK(pool_free(&p, a) == 0);
    CHECK(pool_free(&p, a) == -2);
    void *b = pool_alloc(&p);
    CHECK(pool_free(&p, (char *)b + 3) == -1);
    int x;
    CHECK(pool_free(&p, &x) == -1);
    int n = 0;
    while (pool_alloc(&p)) n++;
    CHECK(n == POOL_N - 1);              /* b 를 빼고 전부 */
}

/* ---- 연습 3: 컴팩션 ----
 * 로그 형식은 연습용 텍스트: "S key value\n" / "D key\n". 충돌 지점 k 에서 함수를 멈춘다. */
enum { CRASH_NONE, CRASH_AFTER_TMP_WRITE, CRASH_AFTER_RENAME };
#define MAP_MAX 32
typedef struct { char k[16], v[16]; bool on; } ent_t;
typedef struct { ent_t e[MAP_MAX]; } map_t;
static ent_t *map_find(map_t *m, const char *k) {
    for (int i = 0; i < MAP_MAX; i++) if (m->e[i].on && !strcmp(m->e[i].k, k)) return &m->e[i];
    return NULL;
}
static void map_set(map_t *m, const char *k, const char *v) {
    ent_t *e = map_find(m, k);
    if (!e) for (int i = 0; i < MAP_MAX && !e; i++) if (!m->e[i].on) e = &m->e[i];
    if (!e) return;
    e->on = true;
    snprintf(e->k, sizeof e->k, "%s", k);
    snprintf(e->v, sizeof e->v, "%s", v);
}
static void map_load(map_t *m, const char *path) {
    memset(m, 0, sizeof *m);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char op[4], k[16], v[16];
    char line[64];
    while (fgets(line, sizeof line, f)) {
        int n = sscanf(line, "%3s %15s %15s", op, k, v);
        if (n == 3 && !strcmp(op, "S")) map_set(m, k, v);
        else if (n == 2 && !strcmp(op, "D")) { ent_t *e = map_find(m, k); if (e) e->on = false; }
    }
    fclose(f);
}
static bool map_eq(const map_t *a, const map_t *b) {
    for (int i = 0; i < MAP_MAX; i++) {
        if (!a->e[i].on) continue;
        ent_t *e = map_find((map_t *)b, a->e[i].k);
        if (!e || strcmp(e->v, a->e[i].v)) return false;
    }
    for (int i = 0; i < MAP_MAX; i++)
        if (b->e[i].on && !map_find((map_t *)a, b->e[i].k)) return false;
    return true;
}
/* 임시 파일에 현재 상태만 쓰고 fsync → rename → 디렉터리 fsync */
static int compact(const char *path, const char *dir, int crash_at) {
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    map_t m;
    map_load(&m, path);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    for (int i = 0; i < MAP_MAX; i++)
        if (m.e[i].on) fprintf(f, "S %s %s\n", m.e[i].k, m.e[i].v);
    fflush(f);
    (void)fsync(fileno(f));
    fclose(f);
    if (crash_at == CRASH_AFTER_TMP_WRITE) return 0;     /* rename 전 충돌 */
    if (rename(tmp, path) != 0) return -1;
    if (crash_at == CRASH_AFTER_RENAME) return 0;        /* 디렉터리 fsync 전 충돌 */
    int dfd = open(dir, O_RDONLY);
    if (dfd >= 0) { (void)fsync(dfd); close(dfd); }
    return 0;
}
static void test_compaction(void) {
    char dir[] = "/tmp/osa11p.XXXXXX";
    if (!mkdtemp(dir)) { CHECK(0); return; }
    char log[128], tmp[140];
    snprintf(log, sizeof log, "%s/kv.log", dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", log);

    const char *body = "S a 1\nS b 2\nS a 3\nD b\nS c 7\nS a 9\n";
    FILE *f = fopen(log, "w");
    fputs(body, f);
    fclose(f);
    map_t before, after;
    map_load(&before, log);

    /* 충돌 지점 1: rename 전에 죽었다 → 옛 로그가 그대로, 임시 파일은 무시 */
    CHECK(compact(log, dir, CRASH_AFTER_TMP_WRITE) == 0);
    map_load(&after, log);
    CHECK(map_eq(&before, &after));
    struct stat st;
    CHECK(stat(log, &st) == 0 && (size_t)st.st_size == strlen(body));
    /* 충돌 지점 2: rename 뒤 → 새 로그, 상태 동일, 크기는 작아졌다 */
    CHECK(compact(log, dir, CRASH_AFTER_RENAME) == 0);
    map_load(&after, log);
    CHECK(map_eq(&before, &after));
    CHECK(stat(log, &st) == 0 && (size_t)st.st_size < strlen(body));
    /* 전체 경로: 한 번 더 컴팩션해도 상태는 같다 */
    CHECK(compact(log, dir, CRASH_NONE) == 0);
    map_load(&after, log);
    CHECK(map_eq(&before, &after));
    ent_t *a = map_find(&after, "a");
    CHECK(a && !strcmp(a->v, "9") && map_find(&after, "b") == NULL);
    unlink(log); unlink(tmp); rmdir(dir);
}

/* ---- 연습 4: 순서 버퍼 ----
 * 연결당 동시 요청을 여러 개 허용하면 워커가 순서 없이 끝낸다. 도착 순서로 번호를 매기고,
 * 맨 앞 번호부터 끝난 것만 내보낸다. 창(window) 이 가득 차면 더 받지 않는다(역압). */
#define ROB_N 8
typedef struct {
    uint64_t next_issue, next_deliver;
    bool done[ROB_N];
    int  value[ROB_N];
} rob_t;
static bool rob_issue(rob_t *r, uint64_t *seq) {
    if (r->next_issue - r->next_deliver == ROB_N) return false;
    *seq = r->next_issue++;
    r->done[*seq % ROB_N] = false;
    return true;
}
static void rob_complete(rob_t *r, uint64_t seq, int v) {
    r->done[seq % ROB_N] = true;
    r->value[seq % ROB_N] = v;
}
static bool rob_deliver(rob_t *r, int *v) {
    if (r->next_deliver == r->next_issue || !r->done[r->next_deliver % ROB_N]) return false;
    *v = r->value[r->next_deliver % ROB_N];
    r->next_deliver++;
    return true;
}
static void test_rob(void) {
    rob_t r;
    memset(&r, 0, sizeof r);
    uint64_t s[ROB_N];
    for (int i = 0; i < ROB_N; i++) CHECK(rob_issue(&r, &s[i]) && s[i] == (uint64_t)i);
    uint64_t extra;
    CHECK(!rob_issue(&r, &extra));       /* 창이 찼다 */
    rob_complete(&r, 2, 200);
    rob_complete(&r, 1, 100);
    int v = -1;
    CHECK(!rob_deliver(&r, &v));         /* 0 번이 아직 */
    rob_complete(&r, 0, 50);
    int got[3] = { 0 };
    CHECK(rob_deliver(&r, &got[0]) && rob_deliver(&r, &got[1]) && rob_deliver(&r, &got[2]));
    CHECK(got[0] == 50 && got[1] == 100 && got[2] == 200);
    CHECK(!rob_deliver(&r, &v));         /* 3 번이 아직 */
    CHECK(rob_issue(&r, &extra) && extra == 8);   /* 창이 비었으니 다시 받는다 (번호는 그대로 증가) */
}

/* ---- 연습 5: 히스테리시스 부하 차단 ----
 * 큐 깊이가 high 이상이면 거절 모드, low 이하로 내려와야 해제. 한 경계에서 깜박이지 않는다. */
typedef struct { size_t low, high; bool shedding; unsigned flips; } admit_t;
static bool admit_ok(admit_t *a, size_t depth) {
    bool was = a->shedding;
    if (!a->shedding && depth >= a->high) a->shedding = true;
    else if (a->shedding && depth <= a->low) a->shedding = false;
    if (was != a->shedding) a->flips++;
    return !a->shedding;
}
static void test_admit(void) {
    admit_t a = { 2, 6, false, 0 };
    size_t depths[] = { 1, 3, 5, 6, 5, 4, 3, 2, 1 };
    bool want[]     = { 1, 1, 1, 0, 0, 0, 0, 1, 1 };
    bool same = true;
    for (size_t i = 0; i < sizeof depths / sizeof depths[0]; i++)
        if (admit_ok(&a, depths[i]) != want[i]) same = false;
    CHECK(same);
    CHECK(a.flips == 2);
    /* 깜박임 없음: high 언저리에서 흔들려도 1 번만 전환 */
    admit_t b = { 2, 6, false, 0 };
    size_t wobble[] = { 6, 5, 6, 5, 6, 5, 6 };
    for (size_t i = 0; i < 7; i++) (void)admit_ok(&b, wobble[i]);
    CHECK(b.flips == 1);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("--- 연습 1: self-pipe ---\n");  test_self_pipe();
    printf("--- 연습 2: 풀 검증 ---\n");    test_pool();
    printf("--- 연습 3: 컴팩션 ---\n");     test_compaction();
    printf("--- 연습 4: 순서 버퍼 ---\n");  test_rob();
    printf("--- 연습 5: 히스테리시스 ---\n"); test_admit();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
