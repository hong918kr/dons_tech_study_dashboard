/* ENV: linux */
/* LPERF-M07 락 경합 프로파일링: futex 대기와 holder 찾기
 *
 *   Part A  측정 결과를 읽는 코드: 지연 히스토그램(lat_*), bpftrace 출력 파서(bt_parse_line),
 *           주소 → 락 이름(reg_lookup), 락 순위(rank_locks), 누가 쥐고 있었나(top_holder)
 *   Part B  개선 1 — 락 분할: stripe_of, smap_put, smap_get_locked, smap_scan, smap_put2
 *   Part C  개선 2 — seqlock 읽기 경로: seq_write_begin/end, smap_get_seq
 *
 * 체크는 고정 데이터와 "매 실행 항상 성립하는 것"(맵 내용, 개수, 찢어진 읽기 0)만 검사한다.
 * 타이밍 수치는 CHECK 하지 않는다. 실측은 demo_server.c + futex_wait.bt.
 *
 * build (Lima VM elinux):
 *   gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ======================= 공통: 값 · 슬롯 · 테이블 ======================= */
#define VAL_WORDS 4
typedef struct { uint64_t w[VAL_WORDS]; } val_t;

/* 락 없이 읽는 경로(Part C)가 있으므로 슬롯의 모든 워드는 relaxed atomic 이다 (data race 가 아니다). */
typedef struct {
    _Atomic uint64_t key;              /* 0 = 빈 슬롯. 삭제 없음 */
    _Atomic uint64_t w[VAL_WORDS];
} slot_t;

typedef struct { slot_t *slots; uint32_t mask; uint32_t used; } table_t;

static inline uint64_t rd(const _Atomic uint64_t *p) { return atomic_load_explicit(p, memory_order_relaxed); }
static inline void wr(_Atomic uint64_t *p, uint64_t v) { atomic_store_explicit(p, v, memory_order_relaxed); }
static inline uint64_t key_hash(uint64_t k) { return k * 0x9E3779B97F4A7C15ull; }

/* 슬롯 번호는 해시의 "아래쪽" 비트를 쓴다. (stripe_of 는 위쪽 비트를 써야 한다 → Part B) */
static inline uint32_t home_slot(const table_t *t, uint64_t key) { return (uint32_t)(key_hash(key) >> 8) & t->mask; }

static bool table_init(table_t *t, unsigned cap_log2) {
    t->mask = (1u << cap_log2) - 1;
    t->used = 0;
    t->slots = calloc((size_t)t->mask + 1, sizeof(slot_t));
    return t->slots != NULL;
}

/* 선형 탐사. 없으면: insert=true → 빈 슬롯을 차지해 반환(가득 차면 NULL), false → NULL */
static slot_t *table_find(table_t *t, uint64_t key, bool insert) {
    uint32_t i = home_slot(t, key);
    for (uint32_t n = 0; n <= t->mask; n++, i = (i + 1) & t->mask) {
        uint64_t k = rd(&t->slots[i].key);
        if (k == key) return &t->slots[i];
        if (k == 0) {
            if (!insert) return NULL;
            wr(&t->slots[i].key, key);
            t->used++;
            return &t->slots[i];
        }
    }
    return NULL;
}

static inline void slot_write(slot_t *e, const val_t *v) { for (int i = 0; i < VAL_WORDS; i++) wr(&e->w[i], v->w[i]); }
static inline void slot_read(const slot_t *e, val_t *v) { for (int i = 0; i < VAL_WORDS; i++) v->w[i] = rd(&e->w[i]); }

/* ======================= 출발점: 전역 mutex 맵 (주어짐) ======================= */
typedef struct { pthread_mutex_t lock; table_t t; } gmap_t;

static bool gmap_init(gmap_t *m, unsigned cap_log2) {
    pthread_mutex_init(&m->lock, NULL);
    return table_init(&m->t, cap_log2);
}
static void gmap_destroy(gmap_t *m) { pthread_mutex_destroy(&m->lock); free(m->t.slots); }
static bool gmap_put(gmap_t *m, uint64_t key, const val_t *v) {
    pthread_mutex_lock(&m->lock);
    slot_t *e = table_find(&m->t, key, true);
    if (e) slot_write(e, v);
    pthread_mutex_unlock(&m->lock);
    return e != NULL;
}
static bool gmap_get(gmap_t *m, uint64_t key, val_t *out) {
    pthread_mutex_lock(&m->lock);
    slot_t *e = table_find(&m->t, key, false);
    if (e) slot_read(e, out);
    pthread_mutex_unlock(&m->lock);
    return e != NULL;
}
/* 전체 순회: 락을 쥔 채 테이블 끝까지 간다 — 이 레슨의 "느린 holder" */
static uint64_t gmap_scan(gmap_t *m, uint64_t *count) {
    uint64_t sum = 0, c = 0;
    pthread_mutex_lock(&m->lock);
    for (uint32_t i = 0; i <= m->t.mask; i++)
        if (rd(&m->t.slots[i].key)) { sum += rd(&m->t.slots[i].w[0]); c++; }
    pthread_mutex_unlock(&m->lock);
    if (count) *count = c;
    return sum;
}

/* ======================= Part A: 측정 결과 읽기 ======================= */

/* 8 까지는 1 ns 칸, 그 위로는 옥타브(2 배)마다 8 칸. 상대 오차 ≤ 12.5 %. */
#define LAT_BUCKETS 496
typedef struct { uint64_t n[LAT_BUCKETS]; uint64_t total; } lat_hist_t;

/* TODO(A1): ns → 칸 번호.  ns < 8 이면 ns 그대로.
 * 아니면 msb = 최상위 비트 위치, shift = msb-3, sub = (ns >> shift) & 7, 칸 = 8*(msb-2) + sub */
static unsigned lat_bucket(uint64_t ns) {
    if (ns < 8) return (unsigned)ns;
    unsigned msb = 63u - (unsigned)__builtin_clzll(ns);
    unsigned shift = msb - 3;
    return 8 * (msb - 2) + (unsigned)((ns >> shift) & 7);
}

static uint64_t lat_upper(unsigned b) {
    if (b < 8) return b;
    unsigned oct = b / 8, sub = b % 8, shift = oct - 1;
    uint64_t lower = (uint64_t)(8 + sub) << shift;
    return lower + ((uint64_t)1 << shift) - 1;
}

static inline void lat_record(lat_hist_t *h, uint64_t ns) { h->n[lat_bucket(ns)]++; h->total++; }

/* TODO(A2): p (0 < p ≤ 100, 0.1 단위를 위해 permille 로 받는다: 990 = p99) 분위수의 칸 상한.
 * 순위 = ceil(total * permille / 1000). 누적 합이 순위에 처음 도달하는 칸의 lat_upper. 비었으면 0. */
static uint64_t lat_percentile(const lat_hist_t *h, unsigned permille) {
    if (h->total == 0) return 0;
    uint64_t rank = (h->total * permille + 999) / 1000, seen = 0;
    if (rank == 0) rank = 1;
    for (unsigned b = 0; b < LAT_BUCKETS; b++) {
        seen += h->n[b];
        if (seen >= rank) return lat_upper(b);
    }
    return lat_upper(LAT_BUCKETS - 1);
}

/* bpftrace 가 맵을 출력한 한 줄:   @holder_ns[281472890175488, 2231]: 913455
 * key2 가 없는 맵은                @wait_ns[281472890175488]: 913455          (key2 = -1) */
typedef struct { char map[32]; uint64_t uaddr; int key2; uint64_t value; } bt_row_t;

/* TODO(A3): 위 두 형식만 받아 out 을 채우고 true. 다른 줄(히스토그램 막대, 스택 등)은 false.
 * 맵 이름은 '@' 다음부터 '[' 앞까지 (31 자 넘으면 false). 줄 끝의 개행은 있어도 없어도 된다. */
static bool bt_parse_line(const char *line, bt_row_t *out) {
    char name[32], tail[2];
    uint64_t a, v;
    int k2;
    if (sscanf(line, "@%31[^[][%" SCNu64 ", %d]: %" SCNu64 " %1s", name, &a, &k2, &v, tail) == 4) {
        /* 두 키 형식 */
    } else if (sscanf(line, "@%31[^[][%" SCNu64 "]: %" SCNu64 " %1s", name, &a, &v, tail) == 3) {
        k2 = -1;
    } else {
        return false;
    }
    strcpy(out->map, name);
    out->uaddr = a;
    out->key2 = k2;
    out->value = v;
    return true;
}

/* 서버가 시작할 때 출력한 "락 등록부". 배열이면 elem_size·count 로 범위를 나타낸다. */
typedef struct { const char *name; uint64_t base; uint32_t elem_size; uint32_t count; } lock_reg_t;

/* TODO(A4): uaddr 가 어느 항목의 [base, base + elem_size*count) 안인가. 찾으면 항목 번호, 없으면 -1.
 * elem 이 NULL 이 아니면 배열 안에서의 칸 번호((uaddr-base)/elem_size)를 넣는다. */
static int reg_lookup(const lock_reg_t *regs, int nregs, uint64_t uaddr, uint32_t *elem) {
    for (int i = 0; i < nregs; i++) {
        uint64_t size = (uint64_t)regs[i].elem_size * regs[i].count;
        if (uaddr >= regs[i].base && uaddr - regs[i].base < size) {
            if (elem) *elem = (uint32_t)((uaddr - regs[i].base) / regs[i].elem_size);
            return i;
        }
    }
    return -1;
}

typedef struct { int reg; uint64_t ns; unsigned share_permille; } lock_rank_t;

/* TODO(A5): rows 중 map 이름이 "wait_ns" 인 줄만 락별로 합산해 ns 내림차순으로 out 에 쓴다.
 * 등록부에 없는 주소는 reg = -1 한 묶음. share = 그 묶음 ns * 1000 / 전체 ns (내림). 반환 = 묶음 수.
 * 동률이면 reg 번호가 작은 쪽이 먼저 (결과가 입력 순서에 의존하지 않게). */
static int rank_locks(const bt_row_t *rows, int nrows, const lock_reg_t *regs, int nregs,
                      lock_rank_t *out, int max_out) {
    uint64_t sum[16] = {0}, unknown = 0, total = 0;   /* sum[i] = regs[i], nregs ≤ 16 */
    for (int i = 0; i < nrows; i++) {
        if (strcmp(rows[i].map, "wait_ns") != 0) continue;
        int r = reg_lookup(regs, nregs, rows[i].uaddr, NULL);
        if (r < 0) unknown += rows[i].value; else sum[r] += rows[i].value;
        total += rows[i].value;
    }
    int n = 0;
    for (int i = 0; i < nregs && n < max_out; i++)
        if (sum[i]) out[n++] = (lock_rank_t){ i, sum[i], 0 };
    if (unknown && n < max_out) out[n++] = (lock_rank_t){ -1, unknown, 0 };
    for (int i = 1; i < n; i++) {                     /* 삽입 정렬 (안정) */
        lock_rank_t x = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].ns < x.ns) { out[j + 1] = out[j]; j--; }
        out[j + 1] = x;
    }
    for (int i = 0; i < n; i++) out[i].share_permille = (unsigned)(out[i].ns * 1000 / total);
    return n;
}

/* 서버가 출력한 "tid 역할" 표 */
typedef struct { int tid; const char *role; } tid_role_t;

/* TODO(A6): rows 중 "holder_ns" 줄(key2 = 대기 시작 순간의 owner tid)에서, uaddr 가 regs[reg] 범위인 것만 보고
 * 역할별로 ns 를 합산해 가장 큰 역할 이름을 돌려준다. 동률이면 tids 표에서 먼저 나온 역할.
 * key2 가 표에 없으면 "?" 역할로 친다. 데이터가 없으면 NULL. *share_permille 에 그 역할의 비중을 넣는다. */
static const char *top_holder(const bt_row_t *rows, int nrows, const lock_reg_t *regs, int reg,
                              const tid_role_t *tids, int ntids, unsigned *share_permille) {
    const char *names[32];
    uint64_t sum[32];
    int nn = 0;
    uint64_t total = 0;
    for (int i = 0; i < nrows; i++) {
        if (strcmp(rows[i].map, "holder_ns") != 0) continue;
        if (reg_lookup(&regs[reg], 1, rows[i].uaddr, NULL) != 0) continue;
        const char *role = "?";
        for (int t = 0; t < ntids; t++) if (tids[t].tid == rows[i].key2) { role = tids[t].role; break; }
        int k = 0;
        while (k < nn && strcmp(names[k], role) != 0) k++;
        if (k == nn) { if (nn == 32) continue; names[nn] = role; sum[nn] = 0; nn++; }
        sum[k] += rows[i].value;
        total += rows[i].value;
    }
    if (total == 0) return NULL;
    /* 동률 규칙: tids 표 순서 ("?" 는 맨 뒤) */
    int best = -1;
    for (int t = 0; t <= ntids; t++) {
        const char *role = t < ntids ? tids[t].role : "?";
        for (int k = 0; k < nn; k++)
            if (strcmp(names[k], role) == 0 && (best < 0 || sum[k] > sum[best])) best = k;
    }
    *share_permille = (unsigned)(sum[best] * 1000 / total);
    return names[best];
}

/* ======================= Part B: 락 분할 ======================= */

/* mutex 와 seq 가 옆 스트라이프의 것과 같은 캐시 라인에 있지 않게 정렬한다 (ARCH-M08: M6 는 128 B) */
typedef struct {
    _Alignas(128) pthread_mutex_t lock;
    _Atomic uint32_t seq;              /* Part C: 짝수 = 안정, 홀수 = 쓰는 중 */
    table_t t;
} stripe_t;

typedef struct { stripe_t *s; uint32_t n; } smap_t;   /* n = 2 의 거듭제곱 */

static void (*g_scan_hook)(const smap_t *, uint32_t);    /* 테스트용: 스트라이프 i 를 쥔 채 호출 */
static void (*g_read_hook)(int word);                    /* 테스트용: 슬롯 워드를 읽은 직후 호출 */

static bool smap_init(smap_t *m, uint32_t n, unsigned cap_log2_per_stripe) {
    m->n = n;
    m->s = aligned_alloc(128, n * sizeof(stripe_t));
    if (!m->s) return false;
    memset(m->s, 0, n * sizeof(stripe_t));
    for (uint32_t i = 0; i < n; i++) {
        pthread_mutex_init(&m->s[i].lock, NULL);
        if (!table_init(&m->s[i].t, cap_log2_per_stripe)) return false;
    }
    return true;
}

static void smap_destroy(smap_t *m) {
    for (uint32_t i = 0; i < m->n; i++) { pthread_mutex_destroy(&m->s[i].lock); free(m->s[i].t.slots); }
    free(m->s);
}

/* TODO(C1): 쓰기 구간의 시작/끝. 시작: seq 를 홀수로(relaxed) → release fence.  끝: seq 를 짝수로(release).
 * (반드시 stripe 락을 쥔 상태로 부른다 — seqlock 은 writer 하나를 가정한다. 이유는 LLAT-M02) */
__attribute__((unused)) static inline void seq_write_begin(stripe_t *s) {
    uint32_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, q + 1, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
}
__attribute__((unused)) static inline void seq_write_end(stripe_t *s) {
    uint32_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, q + 1, memory_order_release);
}

/* TODO(B1): key → 스트라이프 번호. key_hash 의 "위쪽" 비트를 쓴다: (h >> 56) & (n-1).
 * home_slot 이 쓰는 아래쪽 비트를 또 쓰면 한 스트라이프의 키들이 슬롯 일부에만 몰린다. */
static inline uint32_t stripe_of(uint64_t key, uint32_t n) {
    return (uint32_t)(key_hash(key) >> 56) & (n - 1);
}

/* TODO(B2): 해당 스트라이프 하나만 잠그고 쓴다. 쓰는 동안은 seq_write_begin/end 로 감싼다. 테이블이 차면 false. */
static bool smap_put(smap_t *m, uint64_t key, const val_t *v) {
    stripe_t *s = &m->s[stripe_of(key, m->n)];
    pthread_mutex_lock(&s->lock);
    seq_write_begin(s);
    slot_t *e = table_find(&s->t, key, true);
    if (e) slot_write(e, v);
    seq_write_end(s);
    pthread_mutex_unlock(&s->lock);
    return e != NULL;
}

/* TODO(B3): 해당 스트라이프 하나만 잠그고 읽는다. */
static bool smap_get_locked(smap_t *m, uint64_t key, val_t *out) {
    stripe_t *s = &m->s[stripe_of(key, m->n)];
    pthread_mutex_lock(&s->lock);
    slot_t *e = table_find(&s->t, key, false);
    if (e) slot_read(e, out);
    pthread_mutex_unlock(&s->lock);
    return e != NULL;
}

/* TODO(B4): 스트라이프를 "하나씩" 잠그며 합산한다 (두 개를 겹쳐 쥐지 않는다).
 * 스트라이프 i 를 쥔 동안 g_scan_hook 이 있으면 g_scan_hook(m, i) 를 부른다.
 * 대가: 합계가 한 순간의 스냅샷은 아니다. */
static uint64_t smap_scan(smap_t *m, uint64_t *count) {
    uint64_t sum = 0, c = 0;
    for (uint32_t i = 0; i < m->n; i++) {
        stripe_t *s = &m->s[i];
        pthread_mutex_lock(&s->lock);
        if (g_scan_hook) g_scan_hook(m, i);
        for (uint32_t j = 0; j <= s->t.mask; j++)
            if (rd(&s->t.slots[j].key)) { sum += rd(&s->t.slots[j].w[0]); c++; }
        pthread_mutex_unlock(&s->lock);
    }
    if (count) *count = c;
    return sum;
}

/* TODO(B5): 두 키를 원자적으로 쓴다. 스트라이프가 같으면 한 번만 잠근다(자기 교착 방지).
 * 다르면 번호가 작은 쪽부터 잠근다(OS-M10 의 순환 대기 제거). 두 스트라이프 모두 seq 를 올린다.
 * 둘 중 하나라도 테이블이 차면 false. */
static bool smap_put2(smap_t *m, uint64_t k1, const val_t *v1, uint64_t k2, const val_t *v2) {
    uint32_t a = stripe_of(k1, m->n), b = stripe_of(k2, m->n);
    uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
    pthread_mutex_lock(&m->s[lo].lock);
    if (hi != lo) pthread_mutex_lock(&m->s[hi].lock);
    seq_write_begin(&m->s[lo]);
    if (hi != lo) seq_write_begin(&m->s[hi]);
    slot_t *e1 = table_find(&m->s[a].t, k1, true);
    slot_t *e2 = table_find(&m->s[b].t, k2, true);
    if (e1) slot_write(e1, v1);
    if (e2) slot_write(e2, v2);
    if (hi != lo) seq_write_end(&m->s[hi]);
    seq_write_end(&m->s[lo]);
    if (hi != lo) pthread_mutex_unlock(&m->s[hi].lock);
    pthread_mutex_unlock(&m->s[lo].lock);
    return e1 && e2;
}

/* ======================= Part C: seqlock 읽기 경로 ======================= */
#define SEQ_MAX_RETRY 64

/* TODO(C2): 락 없이 읽는다.
 *   1) s1 = seq (acquire). 홀수면 이번 시도 실패.
 *   2) 키를 찾아 w[0..3] 을 relaxed 로 복사한다. 워드 k 를 읽은 직후 g_read_hook 이 있으면 g_read_hook(k).
 *   3) acquire fence → s2 = seq (relaxed). s1 == s2 면 성공 (없는 키면 false 를 돌려준다).
 *   4) 실패가 SEQ_MAX_RETRY 번 쌓이면 smap_get_locked 로 넘어간다 (reader 굶주림의 상한).
 * *retries 에 실패 횟수를 넣는다. 복사는 지역 변수에 하고, 성공했을 때만 out 에 쓴다. */
static bool smap_get_seq(smap_t *m, uint64_t key, val_t *out, unsigned *retries) {
    stripe_t *s = &m->s[stripe_of(key, m->n)];
    unsigned fails = 0;
    while (fails < SEQ_MAX_RETRY) {
        uint32_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 & 1) { fails++; continue; }
        val_t tmp;
        slot_t *e = table_find(&s->t, key, false);
        if (e) {
            for (int i = 0; i < VAL_WORDS; i++) {
                tmp.w[i] = rd(&e->w[i]);
                if (g_read_hook) g_read_hook(i);
            }
        }
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&s->seq, memory_order_relaxed) == s1) {
            if (retries) *retries = fails;
            if (e) *out = tmp;
            return e != NULL;
        }
        fails++;
    }
    if (retries) *retries = fails;
    return smap_get_locked(m, key, out);
}

/* ======================= 테스트 하네스 ======================= */
#ifndef LPERF_M07_NO_MAIN
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

static void on_alarm(int sig) {
    (void)sig;
    static const char msg[] = "FAIL  timeout: 락이 풀리지 않았다 (교착 또는 멈춤)\n";
    if (write(1, msg, sizeof msg - 1) < 0) _exit(2);
    _exit(1);
}

static val_t mkval(uint64_t b) { val_t v; for (int i = 0; i < VAL_WORDS; i++) v.w[i] = b + (uint64_t)i; return v; }
static bool val_ok(const val_t *v) { for (int i = 1; i < VAL_WORDS; i++) if (v->w[i] != v->w[0] + (uint64_t)i) return false; return true; }

/* ---- Part A ---- */
static void test_lat(void) {
    printf("== A1/A2 지연 히스토그램 ==\n");
    CHECK(lat_bucket(0) == 0);
    CHECK(lat_bucket(7) == 7);
    CHECK(lat_bucket(8) == 8);
    CHECK(lat_bucket(15) == 15);
    CHECK(lat_bucket(16) == 16);
    CHECK(lat_bucket(17) == 16);
    CHECK(lat_bucket(18) == 17);
    CHECK(lat_upper(lat_bucket(1000000)) >= 1000000);
    CHECK(lat_upper(lat_bucket(1000000)) <= 1125000);
    CHECK(lat_bucket(UINT64_MAX) == LAT_BUCKETS - 1);

    static lat_hist_t h;
    memset(&h, 0, sizeof h);
    CHECK(lat_percentile(&h, 500) == 0);
    for (int i = 0; i < 990; i++) lat_record(&h, 5);
    for (int i = 0; i < 10; i++) lat_record(&h, 1000);
    CHECK(lat_percentile(&h, 500) == 5);
    CHECK(lat_percentile(&h, 990) == 5);        /* 순위 990 은 5 칸의 마지막 */
    CHECK(lat_percentile(&h, 991) == lat_upper(lat_bucket(1000)));
    CHECK(lat_percentile(&h, 1000) == lat_upper(lat_bucket(1000)));
}

static void test_parse(void) {
    printf("== A3 bpftrace 줄 파싱 ==\n");
    bt_row_t r;
    CHECK(bt_parse_line("@wait_ns[187650912338240]: 913455\n", &r));
    CHECK(strcmp(r.map, "wait_ns") == 0 && r.uaddr == 187650912338240ull && r.key2 == -1 && r.value == 913455);
    CHECK(bt_parse_line("@holder_ns[187650912338240, 2231]: 77", &r));
    CHECK(strcmp(r.map, "holder_ns") == 0 && r.key2 == 2231 && r.value == 77);
    CHECK(!bt_parse_line("[2, 4)     12 |@@@@@@@@@   |", &r));
    CHECK(!bt_parse_line("        map_get+44", &r));
    CHECK(!bt_parse_line("@wait_hist_us[187650912338240]:", &r));
    CHECK(!bt_parse_line("@wait_ns[12]: 34 garbage", &r));
}

#define B0 0x10000ull
static const lock_reg_t REGS[] = {
    { "map_lock", B0, 40, 1 },
    { "stats_lock", B0 + 0x100, 40, 1 },
    { "stripe", B0 + 0x1000, 128, 64 },
};
static void test_rank(void) {
    printf("== A4/A5 주소 → 락, 순위 ==\n");
    uint32_t el = 99;
    CHECK(reg_lookup(REGS, 3, B0, &el) == 0 && el == 0);
    CHECK(reg_lookup(REGS, 3, B0 + 39, NULL) == 0);
    CHECK(reg_lookup(REGS, 3, B0 + 40, NULL) == -1);
    CHECK(reg_lookup(REGS, 3, B0 + 0x1000 + 128 * 5 + 8, &el) == 2 && el == 5);
    CHECK(reg_lookup(REGS, 3, B0 + 0x1000 + 128 * 64, NULL) == -1);

    bt_row_t rows[] = {
        { "wait_ns", B0 + 0x100, -1, 300 }, { "wait_ns", B0, -1, 600 },
        { "wait_ns", B0 + 0x1000, -1, 50 }, { "wait_ns", B0 + 0x1000 + 128, -1, 50 },
        { "wait_ns", 5, -1, 100 },          { "wait_cnt", B0, -1, 999999 },
    };
    lock_rank_t out[8];
    memset(out, 0, sizeof out);
    int n = rank_locks(rows, 6, REGS, 3, out, 8);
    CHECK(n == 4);
    CHECK(out[0].reg == 0 && out[0].ns == 600 && out[0].share_permille == 545);
    CHECK(out[1].reg == 1 && out[1].ns == 300);
    CHECK(out[2].reg == 2 && out[2].ns == 100);    /* 스트라이프 둘이 한 묶음 */
    CHECK(out[3].reg == -1 && out[3].ns == 100);   /* 동률: 등록부 항목이 먼저 */
    CHECK(rank_locks(rows, 0, REGS, 3, out, 8) == 0);
}

static void test_holder(void) {
    printf("== A6 holder 집계 ==\n");
    tid_role_t tids[] = { { 100, "worker" }, { 101, "worker" }, { 200, "flusher" } };
    bt_row_t rows[] = {
        { "holder_ns", B0, 100, 10 }, { "holder_ns", B0, 101, 20 }, { "holder_ns", B0, 200, 90 },
        { "holder_ns", B0 + 0x100, 200, 100000 }, { "holder_ns", B0, 777, 5 }, { "wait_ns", B0, -1, 123456 },
    };
    unsigned sh = 0;
    const char *who = top_holder(rows, 6, REGS, 0, tids, 3, &sh);
    CHECK(who && strcmp(who, "flusher") == 0);
    CHECK(sh == 720);   /* 90 / 125 */
    rows[2].value = 30; /* worker 30 vs flusher 30 → 표에서 먼저 나온 worker */
    who = top_holder(rows, 6, REGS, 0, tids, 3, &sh);
    CHECK(who && strcmp(who, "worker") == 0);
    CHECK(top_holder(rows, 1, REGS, 2, tids, 3, &sh) == NULL);
}

/* ---- Part B ---- */
static unsigned g_scan_violations;
static void scan_hook_check_prev_released(const smap_t *m, uint32_t i) {
    if (i == 0) return;
    if (pthread_mutex_trylock(&m->s[i - 1].lock) == 0) pthread_mutex_unlock(&m->s[i - 1].lock);
    else g_scan_violations++;
}

static void test_stripes(void) {
    printf("== B1 스트라이프 선택 ==\n");
    uint32_t cnt[64] = {0};
    bool bad = false;
    for (uint64_t k = 1; k <= 64000; k++) {
        uint32_t s = stripe_of(k, 64);
        if (s >= 64) { bad = true; break; }
        cnt[s]++;
    }
    CHECK(!bad);
    uint32_t mn = UINT32_MAX, mx = 0;
    for (int i = 0; i < 64; i++) { if (cnt[i] < mn) mn = cnt[i]; if (cnt[i] > mx) mx = cnt[i]; }
    CHECK(mn >= 800 && mx <= 1200);             /* 평균 1000 ± 20 % */

    /* 스트라이프 하나에 들어간 키들이 슬롯 전체에 퍼져야 한다 (같은 비트를 쓰면 일부 슬롯에만 몰린다) */
    table_t t = { NULL, 1023, 0 };
    static bool seen[1024];
    memset(seen, 0, sizeof seen);
    uint32_t distinct = 0, inside = 0;
    for (uint64_t k = 1; k <= 1000000 && inside < 4000; k++) {
        if (stripe_of(k, 64) != 0) continue;
        inside++;
        uint32_t h = home_slot(&t, k);
        if (!seen[h]) { seen[h] = true; distinct++; }
    }
    CHECK(inside == 4000);
    CHECK(distinct > 900);                      /* 1024 슬롯 중 대부분 */

    printf("== B2/B3/B4 기본 동작 ==\n");
    smap_t m;
    CHECK(smap_init(&m, 16, 8));
    val_t v = mkval(500), o;
    CHECK(smap_put(&m, 7, &v));
    memset(&o, 0, sizeof o);
    CHECK(smap_get_locked(&m, 7, &o) && o.w[0] == 500 && val_ok(&o));
    CHECK(!smap_get_locked(&m, 8, &o));
    uint32_t cnt_put = 0;
    for (uint64_t k = 1; k <= 1000; k++) { v = mkval(k * 10); cnt_put += smap_put(&m, k, &v); }
    CHECK(cnt_put == 1000);
    uint64_t c = 0, sum = smap_scan(&m, &c);
    CHECK(c == 1000);
    CHECK(sum == 10ull * 1000 * 1001 / 2);

    g_scan_violations = 0;
    g_scan_hook = scan_hook_check_prev_released;
    (void)smap_scan(&m, NULL);
    g_scan_hook = NULL;
    CHECK(g_scan_violations == 0);              /* 다음 스트라이프를 쥘 때 앞 스트라이프는 풀려 있다 */

    /* 스트라이프 하나를 가득 채우면 put 은 false */
    smap_t tiny;
    CHECK(smap_init(&tiny, 1, 2));              /* 스트라이프 1 개, 슬롯 4 개 */
    bool ok = true;
    for (uint64_t k = 1; k <= 4; k++) ok = ok && smap_put(&tiny, k, &v);
    CHECK(ok);
    CHECK(!smap_put(&tiny, 5, &v));
    smap_destroy(&tiny);
    smap_destroy(&m);
}

typedef struct { smap_t *m; int id; int rounds; } p2_arg_t;
static void *p2_worker(void *ap) {
    p2_arg_t *a = ap;
    /* 짝수 스레드는 (1,2), 홀수 스레드는 (2,1) 순서: 락 순서를 안 지키면 교착한다 */
    for (int i = 0; i < a->rounds; i++) {
        val_t v = mkval((uint64_t)i);
        if (a->id % 2 == 0) (void)smap_put2(a->m, 1, &v, 2, &v);
        else                (void)smap_put2(a->m, 2, &v, 1, &v);
    }
    return NULL;
}

static void test_put2(void) {
    printf("== B5 두 키 원자적 쓰기 ==\n");
    smap_t m;
    CHECK(smap_init(&m, 16, 8));
    CHECK(stripe_of(1, 16) != stripe_of(2, 16));   /* 이 테스트의 전제: 서로 다른 스트라이프 */

    /* 같은 스트라이프의 두 키: 자기 교착 없이 끝나야 한다 */
    uint64_t k2 = 2;
    while (stripe_of(k2, 16) != stripe_of(1, 16)) k2++;
    val_t v = mkval(42), o;
    memset(&o, 0, sizeof o);
    CHECK(smap_put2(&m, 1, &v, k2, &v));
    CHECK(smap_get_locked(&m, k2, &o) && o.w[0] == 42);

    pthread_t th[4];
    p2_arg_t arg[4];
    for (int i = 0; i < 4; i++) { arg[i] = (p2_arg_t){ &m, i, 20000 }; pthread_create(&th[i], NULL, p2_worker, &arg[i]); }
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    val_t a, b;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    CHECK(smap_get_locked(&m, 1, &a) && smap_get_locked(&m, 2, &b));
    CHECK(a.w[0] == 19999 && b.w[0] == 19999);     /* 마지막 쓰기가 살아 있다: 모두 같은 값으로 끝남 */
    CHECK(val_ok(&a) && val_ok(&b));
    smap_destroy(&m);
}

/* ---- Part C ---- */
static smap_t *g_hook_map;
static int g_hook_after_word = -1, g_hook_fired;
static uint64_t g_hook_key;
static void read_hook_write_midway(int word) {
    if (word != g_hook_after_word || g_hook_fired) return;
    g_hook_fired = 1;
    val_t v = mkval(777000);
    (void)smap_put(g_hook_map, g_hook_key, &v);   /* 읽는 도중 쓰기 — 강제 스케줄 */
}

typedef struct { smap_t *m; atomic_int *stop; uint64_t torn, reads; } rd_arg_t;
static void *rd_worker(void *ap) {
    rd_arg_t *a = ap;
    while (!atomic_load_explicit(a->stop, memory_order_relaxed)) {
        for (uint64_t k = 1; k <= 256; k++) {
            val_t o; unsigned r;
            memset(&o, 0, sizeof o);
            if (smap_get_seq(a->m, k, &o, &r)) { if (!val_ok(&o)) a->torn++; a->reads++; }
        }
    }
    return NULL;
}
typedef struct { smap_t *m; uint64_t base; int rounds; } wr_arg_t;
static void *wr_worker(void *ap) {
    wr_arg_t *a = ap;
    for (int i = 0; i < a->rounds; i++)
        for (uint64_t k = a->base; k < a->base + 64; k++) {
            val_t v = mkval((uint64_t)i * 1000 + k);
            (void)smap_put(a->m, k, &v);
        }
    return NULL;
}

static void test_seq(void) {
    printf("== C 강제 스케줄: 읽는 도중 쓰기 ==\n");
    smap_t m;
    CHECK(smap_init(&m, 16, 8));
    val_t v = mkval(1000), o;
    CHECK(smap_put(&m, 7, &v));

    unsigned r = 99;
    memset(&o, 0, sizeof o);
    CHECK(smap_get_seq(&m, 7, &o, &r) && o.w[0] == 1000 && r == 0);
    CHECK(!smap_get_seq(&m, 8, &o, &r));

    /* 2 번째 워드를 읽은 직후 writer 가 끼어든다: 옛 w0,w1 + 새 w2,w3 를 돌려주면 찢어진 값 */
    g_hook_map = &m; g_hook_key = 7; g_hook_after_word = 1; g_hook_fired = 0;
    g_read_hook = read_hook_write_midway;
    memset(&o, 0, sizeof o);
    bool got = smap_get_seq(&m, 7, &o, &r);
    g_read_hook = NULL;
    CHECK(g_hook_fired == 1);
    CHECK(got && val_ok(&o));
    CHECK(o.w[0] == 777000);                       /* 재시도 끝에 새 값 */
    CHECK(r == 1);

    /* writer 가 쓰는 중(seq 홀수)이면 값을 믿지 않고, 상한까지 재시도 후 락으로 넘어간다 */
    stripe_t *s = &m.s[stripe_of(7, m.n)];
    atomic_store(&s->seq, 1u);
    memset(&o, 0, sizeof o);
    r = 0;
    CHECK(smap_get_seq(&m, 7, &o, &r) && o.w[0] == 777000);
    CHECK(r == SEQ_MAX_RETRY);
    atomic_store(&s->seq, 2u);

    /* 빠른 경로는 스트라이프 락을 잡지 않는다: 락을 쥔 채 불러도 멈추지 않는다 (잡으면 watchdog) */
    pthread_mutex_lock(&s->lock);
    memset(&o, 0, sizeof o);
    r = 99;
    CHECK(smap_get_seq(&m, 7, &o, &r) && o.w[0] == 777000 && r == 0);
    pthread_mutex_unlock(&s->lock);

    printf("== C 스트레스: 찢어진 읽기 0, 마지막 값 정확 ==\n");
    smap_t m2;
    CHECK(smap_init(&m2, 16, 9));
    bool ok = true;
    for (uint64_t k = 1; k <= 256; k++) { val_t x = mkval(k); ok = ok && smap_put(&m2, k, &x); }
    CHECK(ok);
    atomic_int stop = 0;
    pthread_t rt[3], wt[2];
    rd_arg_t ra[3];
    wr_arg_t wa[2] = { { &m2, 1, 3000 }, { &m2, 65, 3000 } };
    for (int i = 0; i < 3; i++) { ra[i] = (rd_arg_t){ &m2, &stop, 0, 0 }; pthread_create(&rt[i], NULL, rd_worker, &ra[i]); }
    for (int i = 0; i < 2; i++) pthread_create(&wt[i], NULL, wr_worker, &wa[i]);
    for (int i = 0; i < 2; i++) pthread_join(wt[i], NULL);
    atomic_store(&stop, 1);
    uint64_t torn = 0, reads = 0;
    for (int i = 0; i < 3; i++) { pthread_join(rt[i], NULL); torn += ra[i].torn; reads += ra[i].reads; }
    CHECK(torn == 0);
    CHECK(reads > 0);
    memset(&o, 0, sizeof o);
    CHECK(smap_get_seq(&m2, 1, &o, &r) && o.w[0] == 2999ull * 1000 + 1 && val_ok(&o));
    memset(&o, 0, sizeof o);
    CHECK(smap_get_locked(&m2, 128, &o) && o.w[0] == 2999ull * 1000 + 128);
    smap_destroy(&m2);
    smap_destroy(&m);
}

int main(void) {
    signal(SIGALRM, on_alarm);
    alarm(60);
    gmap_t gm;
    CHECK(gmap_init(&gm, 8));
    val_t v = mkval(3), o;
    CHECK(gmap_put(&gm, 9, &v));
    CHECK(gmap_get(&gm, 9, &o) && o.w[0] == 3);
    uint64_t c = 0;
    CHECK(gmap_scan(&gm, &c) == 3 && c == 1);
    gmap_destroy(&gm);

    test_lat();
    test_parse();
    test_rank();
    test_holder();
    test_stripes();
    test_put2();
    test_seq();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* LPERF_M07_NO_MAIN */
