/* OS-M02 추가 연습 — 한 번에 검증
 *
 *   연습 1  같은 코어 / 다른 코어 깨움으로 나눠 중앙값 구하기 (pthread_cpu_number_np 기록 분석)
 *   연습 2  조건 변수 핑퐁 (mutex + 조건 변수 2개 + turn) — 파이프 없이 커널 대기/깨움
 *   연습 3  원자 변수 스핀 핑퐁 — 커널 없음, 코어 간 캐시 라인 이동만 (release/acquire 로 데이터 전달)
 *   연습 4  log2 지연 히스토그램 (꼬리 보기)
 *   연습 5  블록하지 않는 기준선: O_NONBLOCK 파이프에서 EAGAIN, write+read 쌍, 파이프 용량
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) g_pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

/* ---------------- 연습 1 — 같은 코어 / 다른 코어로 나누기 ----------------
 * i 번째 왕복에서 퐁이 깨어난 코어 cpu_pong[i], 그 뒤 핑이 깨어난 코어 cpu_ping[i].
 * 둘이 같으면 "퐁이 막히면서 비운 코어에서 핑이 깨어났다" = 한 코어에서의 진짜 스위치.
 * 각 그룹의 (낮은) 중앙값과 같은 코어 개수를 구한다. 그룹이 비면 중앙값 0. */
typedef struct { size_t n_same, n_cross; uint64_t med_same, med_cross; } split_t;
static split_t split_by_core(const uint64_t *rt, const uint8_t *cpu_ping, const uint8_t *cpu_pong, size_t n) {
    split_t r = { 0, 0, 0, 0 };
    uint64_t *a = malloc((n ? n : 1) * sizeof *a), *b = malloc((n ? n : 1) * sizeof *b);
    for (size_t i = 0; i < n; i++) {
        if (cpu_ping[i] == cpu_pong[i]) a[r.n_same++] = rt[i];
        else b[r.n_cross++] = rt[i];
    }
    qsort(a, r.n_same, sizeof *a, cmp_u64);
    qsort(b, r.n_cross, sizeof *b, cmp_u64);
    if (r.n_same) r.med_same = a[(r.n_same - 1) / 2];
    if (r.n_cross) r.med_cross = b[(r.n_cross - 1) / 2];
    free(a); free(b);
    return r;
}

/* ---------------- 연습 2 — 조건 변수 핑퐁 ----------------
 * turn: 0 = 핑 차례, 1 = 퐁 차례. 각자 자기 차례에만 log 에 자기 표시를 남긴다 → 엄격한 교대. */
typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv_ping, cv_pong;     /* 기다리는 쪽마다 하나 (OS-B06 의 empty/fill 과 같은 이유) */
    int turn;
    unsigned rounds, pos;
    char *log;                           /* "PQPQ..." */
} cvpp_t;
static void *cv_pong(void *arg) {
    cvpp_t *c = arg;
    for (unsigned i = 0; i < c->rounds; i++) {
        pthread_mutex_lock(&c->mu);
        while (c->turn != 1) pthread_cond_wait(&c->cv_pong, &c->mu);   /* while: 가짜 깨움 대비 */
        c->log[c->pos++] = 'Q';
        c->turn = 0;
        pthread_cond_signal(&c->cv_ping);
        pthread_mutex_unlock(&c->mu);
    }
    return NULL;
}
static unsigned cv_pingpong(unsigned rounds, char *log) {
    cvpp_t c = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, rounds, 0, log };
    pthread_t t;
    pthread_create(&t, NULL, cv_pong, &c);
    unsigned done = 0;
    for (unsigned i = 0; i < rounds; i++) {
        pthread_mutex_lock(&c.mu);
        c.log[c.pos++] = 'P';
        c.turn = 1;
        pthread_cond_signal(&c.cv_pong);
        while (c.turn != 0) pthread_cond_wait(&c.cv_ping, &c.mu);
        pthread_mutex_unlock(&c.mu);
        done++;
    }
    pthread_join(t, NULL);
    pthread_mutex_destroy(&c.mu);
    pthread_cond_destroy(&c.cv_ping);
    pthread_cond_destroy(&c.cv_pong);
    return done;
}

/* ---------------- 연습 3 — 원자 스핀 핑퐁 ----------------
 * ball 이 2i+1 이면 퐁 차례, 2i+2 면 핑 차례. 공 위에 일반 변수 payload 를 실어 보낸다:
 * release 저장 전에 쓴 payload 는 그 값을 acquire 로 읽은 쪽에서 반드시 보인다 (C-A03). */
typedef struct { _Alignas(128) atomic_uint ball; uint32_t payload; unsigned rounds, bad; } spin_t;
static void *spin_pong(void *arg) {
    spin_t *s = arg;
    for (unsigned i = 0; i < s->rounds; i++) {
        while (atomic_load_explicit(&s->ball, memory_order_acquire) != 2u * i + 1u) { }
        if (s->payload != i * 2654435761u) s->bad++;
        s->payload = ~(i * 2654435761u);
        atomic_store_explicit(&s->ball, 2u * i + 2u, memory_order_release);
    }
    return NULL;
}
static unsigned spin_pingpong(unsigned rounds, unsigned *bad) {
    static spin_t s;
    atomic_store(&s.ball, 0u);
    s.rounds = rounds;
    s.bad = 0;
    pthread_t t;
    pthread_create(&t, NULL, spin_pong, &s);
    unsigned done = 0, mybad = 0;
    for (unsigned i = 0; i < rounds; i++) {
        s.payload = i * 2654435761u;
        atomic_store_explicit(&s.ball, 2u * i + 1u, memory_order_release);
        while (atomic_load_explicit(&s.ball, memory_order_acquire) != 2u * i + 2u) { }
        if (s.payload != ~(i * 2654435761u)) mybad++;
        done++;
    }
    pthread_join(t, NULL);
    *bad = mybad + s.bad;
    return done;
}

/* ---------------- 연습 4 — log2 히스토그램 ----------------
 * bin k (k >= 1) = [2^k, 2^(k+1)) ns, bin 0 = [0, 2). nbins-1 이상은 마지막 칸에 모은다. */
static void hist_log2(const uint64_t *s, size_t n, unsigned *bins, unsigned nbins) {
    memset(bins, 0, nbins * sizeof *bins);
    for (size_t i = 0; i < n; i++) {
        unsigned k = 0;
        uint64_t v = s[i];
        while (v >= 2u) { v >>= 1; k++; }          /* k = floor(log2(s)) (s>=2), 0/1 → 0 */
        bins[k < nbins ? k : nbins - 1u]++;
    }
}

/* ---------------- 연습 5 — 블록하지 않는 기준선 ----------------
 * O_NONBLOCK 파이프: 비어 있을 때 read 는 -1/EAGAIN (기다리지 않음). 이 상태에서 write+read 쌍을
 * n 번 하면 한 번도 블록하지 않는다 = 스위치 없는 기준선. 1바이트씩 가득 채우면 용량도 보인다. */
static int set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL);
    return fl < 0 ? -1 : fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}
typedef struct { bool empty_eagain; unsigned pairs_ok; size_t capacity; bool full_eagain; long pipe_buf; } nb_t;
static nb_t nonblock_probe(unsigned pairs) {
    nb_t r = { false, 0, 0, false, 0 };
    int p[2];
    if (pipe(p) || set_nonblock(p[0]) || set_nonblock(p[1])) return r;
    r.pipe_buf = fpathconf(p[1], _PC_PIPE_BUF);     /* 이 크기 이하의 write 는 원자적 (POSIX) */
    uint8_t b = 0;
    r.empty_eagain = read(p[0], &b, 1) == -1 && errno == EAGAIN;
    for (unsigned i = 0; i < pairs; i++) {
        uint8_t w = (uint8_t)i, x = 0;
        if (write(p[1], &w, 1) == 1 && read(p[0], &x, 1) == 1 && x == w) r.pairs_ok++;
    }
    for (;;) {                                         /* 가득 찰 때까지 1바이트씩 */
        ssize_t n = write(p[1], &b, 1);
        if (n == 1) { r.capacity++; continue; }
        r.full_eagain = n == -1 && errno == EAGAIN;
        break;
    }
    close(p[0]); close(p[1]);
    return r;
}

int main(void) {
    /* 연습 1 */
    {
        uint64_t rt[]  = { 2800, 2900, 4500, 2700, 6000, 2750, 4100, 3000 };
        uint8_t ping[] = {    7,    7,    0,    6,    9,    7,    3,    8 };
        uint8_t pong[] = {    7,    7,    6,    6,    8,    7,    9,    8 };
        split_t s = split_by_core(rt, ping, pong, 8);
        CHECK(s.n_same == 5u && s.n_cross == 3u);
        CHECK(s.med_same == 2800u && s.med_cross == 4500u);   /* same {2700,2750,2800,2900,3000}, cross {4100,4500,6000} */
        split_t e = split_by_core(rt, ping, ping, 8);         /* 전부 같은 코어 */
        CHECK(e.n_cross == 0u && e.med_cross == 0u && e.n_same == 8u && e.med_same == 2900u);
    }
    /* 연습 2 */
    {
        enum { R = 50000 };
        static char log[2 * R + 1];
        CHECK(cv_pingpong(R, log) == R);
        bool alt = true;
        for (unsigned i = 0; i < 2u * R; i++) alt &= log[i] == (i % 2u ? 'Q' : 'P');
        CHECK(alt);                                           /* 정확히 P Q P Q ... */
    }
    /* 연습 3 */
    {
        unsigned bad = 1;
        CHECK(spin_pingpong(200000, &bad) == 200000u);
        CHECK(bad == 0u);                                     /* 양방향 payload 가 매번 온전 */
    }
    /* 연습 4 */
    {
        uint64_t s[] = { 0, 1, 2, 3, 1500, 2047, 2048, 2900, 4095, 70000, 5000000000ull };
        unsigned bins[20];
        hist_log2(s, 11, bins, 20);
        CHECK(bins[0] == 2u && bins[1] == 2u);                /* {0,1}, {2,3} */
        CHECK(bins[10] == 2u && bins[11] == 3u);              /* [1024,2048): 1500,2047 / [2048,4096): 2048,2900,4095 */
        CHECK(bins[16] == 1u && bins[19] == 1u);              /* 70000 → 2^16, 5e9 → 2^32 → 마지막 칸 */
        unsigned sum = 0;
        for (unsigned k = 0; k < 20; k++) sum += bins[k];
        CHECK(sum == 11u);
    }
    /* 연습 5 */
    {
        nb_t r = nonblock_probe(100000);
        CHECK(r.empty_eagain);                                /* 빈 파이프에서 기다리지 않는다 */
        CHECK(r.pairs_ok == 100000u);
        CHECK(r.full_eagain && r.capacity >= 512u);           /* PIPE_BUF(>=512) 이상은 들어간다 */
        printf("pipe capacity with 1-byte nonblocking writes: %zu bytes (PIPE_BUF=%ld)\n", r.capacity, r.pipe_buf);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
