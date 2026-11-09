/* DS-A11 demo_bench — 레이아웃이 처리량에 주는 영향 실측. (malloc 은 측정 코드라서 여기서만 쓴다)
 *
 * build: cc -std=c11 -O2 -Wall -Wextra demo_bench.c -o bench                     (기본: AoSoA W=8)
 *        cc -std=c11 -O2 -DAW=32 demo_bench.c -o bench32                         (AoSoA W=32)
 *        cc -std=c11 -O2 -fno-vectorize -fno-slp-vectorize demo_bench.c -o nv    (SIMD 끄기 → 원인 분리)
 * run:   ./bench layouts | ./bench list | ./bench align | ./bench all
 *
 * 레코드 64B. 작업 집합 S 를 키우면서 n = S/64 개 레코드에 커널을 돌려 ns/record 를 잰다.
 * 측정 1칸 = 5회 반복의 중앙값 (min/max 같이 출력). 반복 1회 = 레코드 1600만 개 이상 처리.
 * 시간은 CLOCK_UPTIME_RAW. macOS 는 코어 고정이 안 되므로 QOS_CLASS_USER_INTERACTIVE 로 P 코어를 '권유' 할 뿐.
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <pthread/qos.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef AW
#define AW 8
#endif

#define NOINLINE __attribute__((noinline))

typedef struct {
    uint64_t t_us;
    uint32_t x, y, z, vx, vy, vz, seq;
    uint16_t temp;
    uint8_t flags, health;
    uint32_t cal[6];
} Rec;
_Static_assert(sizeof(Rec) == 64, "64");

typedef struct {
    uint64_t *t_us;
    uint32_t *x, *y, *z, *vx, *vy, *vz, *seq;
    uint16_t *temp;
    uint8_t *flags, *health;
    uint32_t (*cal)[6];
} Soa;

typedef struct {
    uint32_t x[AW], y[AW], z[AW], vx[AW], vy[AW], vz[AW];
    uint64_t t_us[AW];
    uint32_t seq[AW];
    uint16_t temp[AW];
    uint8_t flags[AW], health[AW];
    uint32_t cal[AW][6];
} Block;
_Static_assert(sizeof(Block) == 64 * AW, "block size");

static uint32_t rng_s = 0x9E3779B9u;
static uint32_t rnd(void) { uint32_t x = rng_s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng_s = x; }

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

static void *xalign(size_t bytes) {
    void *p = NULL;
    if (posix_memalign(&p, 16384, bytes) != 0) { fprintf(stderr, "oom\n"); exit(2); }
    memset(p, 0, bytes);   /* 미리 건드려 페이지 폴트를 측정에서 뺀다 */
    return p;
}

/* ---------- 커널: 레이아웃마다 하나씩 (noinline — Rpass 위치가 함수별로 나오게) ---------- */
NOINLINE static uint32_t aos_sum_x(const Rec *restrict a, uint32_t n) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += a[i].x;
    return s;
}
NOINLINE static uint32_t soa_sum_x(const uint32_t *restrict x, uint32_t n) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += x[i];
    return s;
}
NOINLINE static uint32_t aosoa_sum_x(const Block *restrict b, uint32_t nb) {
    uint32_t s = 0;
    for (uint32_t k = 0; k < nb; k++)
        for (uint32_t l = 0; l < AW; l++) s += b[k].x[l];
    return s;
}

NOINLINE static uint32_t aos_sum_masked(const Rec *restrict a, uint32_t n) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += (a[i].flags & 1u) ? a[i].x : 0u;
    return s;
}
NOINLINE static uint32_t soa_sum_masked(const uint32_t *restrict x, const uint8_t *restrict f, uint32_t n) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += (f[i] & 1u) ? x[i] : 0u;
    return s;
}
NOINLINE static uint32_t aosoa_sum_masked(const Block *restrict b, uint32_t nb) {
    uint32_t s = 0;
    for (uint32_t k = 0; k < nb; k++)
        for (uint32_t l = 0; l < AW; l++) s += (b[k].flags[l] & 1u) ? b[k].x[l] : 0u;
    return s;
}

/* 같은 마스크 합산을 분기 없이: flags bit0 을 전부-1/전부-0 마스크로 바꿔 AND. 분기 예측 비용을 뺀 대조군. */
NOINLINE static uint32_t aos_sum_mask_bl(const Rec *restrict a, uint32_t n) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += a[i].x & (0u - (uint32_t)(a[i].flags & 1u));
    return s;
}
NOINLINE static uint32_t soa_sum_mask_bl(const uint32_t *restrict x, const uint8_t *restrict f, uint32_t n) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += x[i] & (0u - (uint32_t)(f[i] & 1u));
    return s;
}
NOINLINE static uint32_t aosoa_sum_mask_bl(const Block *restrict b, uint32_t nb) {
    uint32_t s = 0;
    for (uint32_t k = 0; k < nb; k++)
        for (uint32_t l = 0; l < AW; l++) s += b[k].x[l] & (0u - (uint32_t)(b[k].flags[l] & 1u));
    return s;
}

NOINLINE static void aos_update_pos(Rec *restrict a, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        a[i].x += a[i].vx; a[i].y += a[i].vy; a[i].z += a[i].vz;
    }
}
NOINLINE static void soa_update_pos(Soa s, uint32_t n) {
    uint32_t *restrict x = s.x, *restrict y = s.y, *restrict z = s.z;
    const uint32_t *restrict vx = s.vx, *restrict vy = s.vy, *restrict vz = s.vz;
    for (uint32_t i = 0; i < n; i++) x[i] += vx[i];
    for (uint32_t i = 0; i < n; i++) y[i] += vy[i];
    for (uint32_t i = 0; i < n; i++) z[i] += vz[i];
}
NOINLINE static void aosoa_update_pos(Block *restrict b, uint32_t nb) {
    for (uint32_t k = 0; k < nb; k++) {
        for (uint32_t l = 0; l < AW; l++) b[k].x[l] += b[k].vx[l];
        for (uint32_t l = 0; l < AW; l++) b[k].y[l] += b[k].vy[l];
        for (uint32_t l = 0; l < AW; l++) b[k].z[l] += b[k].vz[l];
    }
}

NOINLINE static void aos_update_all(Rec *restrict a, uint32_t n, uint32_t dt) {
    for (uint32_t i = 0; i < n; i++) {
        Rec *r = &a[i];
        r->t_us += dt;
        r->x += r->vx; r->y += r->vy; r->z += r->vz;
        r->vx -= r->vx >> 4; r->vy -= r->vy >> 4; r->vz -= r->vz >> 4;
        r->seq += 1;
        r->temp = (uint16_t)(r->temp + 1);
        r->flags = (uint8_t)(r->flags ^ 1u);
        r->health = (uint8_t)(r->health - (r->health > 0));
        for (uint32_t j = 0; j < 6; j++) r->cal[j] += j + 1;
    }
}
NOINLINE static void soa_update_all(Soa s, uint32_t n, uint32_t dt) {
    uint64_t *restrict t = s.t_us;
    uint32_t *restrict x = s.x, *restrict y = s.y, *restrict z = s.z;
    uint32_t *restrict vx = s.vx, *restrict vy = s.vy, *restrict vz = s.vz, *restrict seq = s.seq;
    uint16_t *restrict temp = s.temp;
    uint8_t *restrict flags = s.flags, *restrict health = s.health;
    uint32_t (*restrict cal)[6] = s.cal;
    for (uint32_t i = 0; i < n; i++) t[i] += dt;
    for (uint32_t i = 0; i < n; i++) x[i] += vx[i];
    for (uint32_t i = 0; i < n; i++) y[i] += vy[i];
    for (uint32_t i = 0; i < n; i++) z[i] += vz[i];
    for (uint32_t i = 0; i < n; i++) vx[i] -= vx[i] >> 4;
    for (uint32_t i = 0; i < n; i++) vy[i] -= vy[i] >> 4;
    for (uint32_t i = 0; i < n; i++) vz[i] -= vz[i] >> 4;
    for (uint32_t i = 0; i < n; i++) seq[i] += 1;
    for (uint32_t i = 0; i < n; i++) temp[i] = (uint16_t)(temp[i] + 1);
    for (uint32_t i = 0; i < n; i++) flags[i] = (uint8_t)(flags[i] ^ 1u);
    for (uint32_t i = 0; i < n; i++) health[i] = (uint8_t)(health[i] - (health[i] > 0));
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < 6; j++) cal[i][j] += j + 1;
}
NOINLINE static void aosoa_update_all(Block *restrict bk, uint32_t nb, uint32_t dt) {
    for (uint32_t k = 0; k < nb; k++) {
        Block *b = &bk[k];
        for (uint32_t l = 0; l < AW; l++) b->t_us[l] += dt;
        for (uint32_t l = 0; l < AW; l++) b->x[l] += b->vx[l];
        for (uint32_t l = 0; l < AW; l++) b->y[l] += b->vy[l];
        for (uint32_t l = 0; l < AW; l++) b->z[l] += b->vz[l];
        for (uint32_t l = 0; l < AW; l++) b->vx[l] -= b->vx[l] >> 4;
        for (uint32_t l = 0; l < AW; l++) b->vy[l] -= b->vy[l] >> 4;
        for (uint32_t l = 0; l < AW; l++) b->vz[l] -= b->vz[l] >> 4;
        for (uint32_t l = 0; l < AW; l++) b->seq[l] += 1;
        for (uint32_t l = 0; l < AW; l++) b->temp[l] = (uint16_t)(b->temp[l] + 1);
        for (uint32_t l = 0; l < AW; l++) b->flags[l] = (uint8_t)(b->flags[l] ^ 1u);
        for (uint32_t l = 0; l < AW; l++) b->health[l] = (uint8_t)(b->health[l] - (b->health[l] > 0));
        for (uint32_t l = 0; l < AW; l++)
            for (uint32_t j = 0; j < 6; j++) b->cal[l][j] += j + 1;
    }
}

/* ---------- 레이아웃 측정 ---------- */
static volatile uint32_t g_sink;
#define REPS 5
#define TARGET_RECS 16000000ull

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

typedef void (*RunFn)(void *ctx);

/* 한 칸 측정: passes 번 돌린 시간을 REPS 번 → 중앙값을 ns/record 로 */
static void cell(const char *label, const char *kernel, size_t bytes, RunFn run, void *ctx, uint32_t n) {
    uint64_t t[REPS];
    uint32_t passes = (uint32_t)((TARGET_RECS + n - 1) / n);
    run(ctx);   /* 데우기 (콜드 첫 패스 제외) */
    for (int r = 0; r < REPS; r++) {
        uint64_t t0 = now_ns();
        for (uint32_t p = 0; p < passes; p++) run(ctx);
        t[r] = now_ns() - t0;
    }
    qsort(t, REPS, sizeof t[0], cmp_u64);
    double per = (double)passes * n;
    printf("CELL %s %s %zu %.3f %.3f %.3f\n", label, kernel, bytes / 1024,
           (double)t[REPS / 2] / per, (double)t[0] / per, (double)t[REPS - 1] / per);
    fflush(stdout);
}

typedef struct { Rec *aos; Soa soa; Block *ab; uint32_t n, nb; } Ctx;

#define DEFRUN(NAME, EXPR) static void NAME(void *p) { Ctx *c = p; (void)c; EXPR; }
DEFRUN(run_aos_sum, g_sink += aos_sum_x(c->aos, c->n))
DEFRUN(run_soa_sum, g_sink += soa_sum_x(c->soa.x, c->n))
DEFRUN(run_ab_sum, g_sink += aosoa_sum_x(c->ab, c->nb))
DEFRUN(run_aos_mask, g_sink += aos_sum_masked(c->aos, c->n))
DEFRUN(run_soa_mask, g_sink += soa_sum_masked(c->soa.x, c->soa.flags, c->n))
DEFRUN(run_ab_mask, g_sink += aosoa_sum_masked(c->ab, c->nb))
DEFRUN(run_aos_bl, g_sink += aos_sum_mask_bl(c->aos, c->n))
DEFRUN(run_soa_bl, g_sink += soa_sum_mask_bl(c->soa.x, c->soa.flags, c->n))
DEFRUN(run_ab_bl, g_sink += aosoa_sum_mask_bl(c->ab, c->nb))
DEFRUN(run_aos_pos, aos_update_pos(c->aos, c->n))
DEFRUN(run_soa_pos, soa_update_pos(c->soa, c->n))
DEFRUN(run_ab_pos, aosoa_update_pos(c->ab, c->nb))
DEFRUN(run_aos_all, aos_update_all(c->aos, c->n, 3))
DEFRUN(run_soa_all, soa_update_all(c->soa, c->n, 3))
DEFRUN(run_ab_all, aosoa_update_all(c->ab, c->nb, 3))

static void soa_alloc(Soa *s, uint32_t n, size_t bytes) {
    char *p = xalign(bytes + 12 * 16384);
#define BUMP(field, sz) do { s->field = (void *)p; p += (((sz) + 127u) & ~(size_t)127u); } while (0)
    BUMP(t_us, n * 8ull); BUMP(x, n * 4ull); BUMP(y, n * 4ull); BUMP(z, n * 4ull);
    BUMP(vx, n * 4ull); BUMP(vy, n * 4ull); BUMP(vz, n * 4ull); BUMP(seq, n * 4ull);
    BUMP(temp, n * 2ull); BUMP(flags, (size_t)n); BUMP(health, (size_t)n); BUMP(cal, n * 24ull);
}

static void seed_rec(Rec *r) {
    r->t_us = rnd(); r->x = rnd(); r->y = rnd(); r->z = rnd();
    r->vx = rnd(); r->vy = rnd(); r->vz = rnd(); r->seq = rnd();
    r->temp = (uint16_t)rnd(); r->flags = (uint8_t)rnd(); r->health = (uint8_t)rnd();
    for (int j = 0; j < 6; j++) r->cal[j] = rnd();
}

static void bench_layouts(void) {
    static const size_t KB[] = {16, 64, 256, 1024, 4096, 16384, 65536, 262144};
    for (size_t k = 0; k < sizeof KB / sizeof KB[0]; k++) {
        size_t bytes = KB[k] * 1024;
        uint32_t n = (uint32_t)(bytes / 64);
        n -= n % 32;                         /* 블록 정렬 — W=8/32 모두 통째로 */
        bytes = (size_t)n * 64;
        Ctx c;
        c.n = n;
        c.nb = n / AW;
        c.aos = xalign(bytes);
        soa_alloc(&c.soa, n, bytes);
        c.ab = xalign(bytes);
        rng_s = 0x9E3779B9u;
        for (uint32_t i = 0; i < n; i++) {
            Rec r;
            seed_rec(&r);
            c.aos[i] = r;
            c.soa.t_us[i] = r.t_us; c.soa.x[i] = r.x; c.soa.y[i] = r.y; c.soa.z[i] = r.z;
            c.soa.vx[i] = r.vx; c.soa.vy[i] = r.vy; c.soa.vz[i] = r.vz; c.soa.seq[i] = r.seq;
            c.soa.temp[i] = r.temp; c.soa.flags[i] = r.flags; c.soa.health[i] = r.health;
            memcpy(c.soa.cal[i], r.cal, 24);
            Block *b = &c.ab[i / AW];
            uint32_t l = i % AW;
            b->t_us[l] = r.t_us; b->x[l] = r.x; b->y[l] = r.y; b->z[l] = r.z;
            b->vx[l] = r.vx; b->vy[l] = r.vy; b->vz[l] = r.vz; b->seq[l] = r.seq;
            b->temp[l] = r.temp; b->flags[l] = r.flags; b->health[l] = r.health;
            memcpy(b->cal[l], r.cal, 24);
        }
        if (aos_sum_x(c.aos, n) != soa_sum_x(c.soa.x, n) || soa_sum_x(c.soa.x, n) != aosoa_sum_x(c.ab, c.nb)) {
            fprintf(stderr, "layouts disagree\n");
            exit(3);
        }
        cell("aos", "sum_x", bytes, run_aos_sum, &c, n);
        cell("soa", "sum_x", bytes, run_soa_sum, &c, n);
        cell("aosoa", "sum_x", bytes, run_ab_sum, &c, n);
        cell("aos", "sum_masked", bytes, run_aos_mask, &c, n);
        cell("soa", "sum_masked", bytes, run_soa_mask, &c, n);
        cell("aosoa", "sum_masked", bytes, run_ab_mask, &c, n);
        cell("aos", "sum_mask_bl", bytes, run_aos_bl, &c, n);
        cell("soa", "sum_mask_bl", bytes, run_soa_bl, &c, n);
        cell("aosoa", "sum_mask_bl", bytes, run_ab_bl, &c, n);
        cell("aos", "update_pos", bytes, run_aos_pos, &c, n);
        cell("soa", "update_pos", bytes, run_soa_pos, &c, n);
        cell("aosoa", "update_pos", bytes, run_ab_pos, &c, n);
        cell("aos", "update_all", bytes, run_aos_all, &c, n);
        cell("soa", "update_all", bytes, run_soa_all, &c, n);
        cell("aosoa", "update_all", bytes, run_ab_all, &c, n);
        free(c.aos); free(c.soa.t_us); free(c.ab);
    }
}

/* ---------- 연결 리스트 vs 배열: 같은 데이터, 노드 64B / 16B, 할당 순서 vs 뒤섞음 ---------- */
typedef struct Node64 { struct Node64 *next; uint32_t val; uint8_t pad[52]; } Node64;
typedef struct Node16 { struct Node16 *next; uint32_t val; uint32_t pad; } Node16;
_Static_assert(sizeof(Node64) == 64 && sizeof(Node16) == 16, "node sizes");

NOINLINE static uint32_t walk64(const Node64 *h) { uint32_t s = 0; for (; h; h = h->next) s += h->val; return s; }
NOINLINE static uint32_t walk16(const Node16 *h) { uint32_t s = 0; for (; h; h = h->next) s += h->val; return s; }
NOINLINE static uint32_t arr64(const Node64 *a, uint32_t n) { uint32_t s = 0; for (uint32_t i = 0; i < n; i++) s += a[i].val; return s; }
NOINLINE static uint32_t arr16(const Node16 *a, uint32_t n) { uint32_t s = 0; for (uint32_t i = 0; i < n; i++) s += a[i].val; return s; }
NOINLINE static uint32_t arrv(const uint32_t *a, uint32_t n) { uint32_t s = 0; for (uint32_t i = 0; i < n; i++) s += a[i]; return s; }

typedef struct { const Node64 *h64; const Node16 *h16; const Node64 *a64; const Node16 *a16; const uint32_t *v; uint32_t n; } LCtx;
static void run_w64(void *p) { LCtx *c = p; g_sink += walk64(c->h64); }
static void run_w16(void *p) { LCtx *c = p; g_sink += walk16(c->h16); }
static void run_a64(void *p) { LCtx *c = p; g_sink += arr64(c->a64, c->n); }
static void run_a16(void *p) { LCtx *c = p; g_sink += arr16(c->a16, c->n); }
static void run_av(void *p) { LCtx *c = p; g_sink += arrv(c->v, c->n); }

static void bench_list(void) {
    static const size_t KB[] = {16, 64, 256, 1024, 4096, 16384, 65536, 262144};
    for (size_t k = 0; k < sizeof KB / sizeof KB[0]; k++) {
        size_t bytes = KB[k] * 1024;
        uint32_t n = (uint32_t)(bytes / 64);
        Node64 *a64 = xalign((size_t)n * 64 + 16384);
        Node16 *a16 = xalign((size_t)n * 16 + 16384);
        uint32_t *v = xalign((size_t)n * 4 + 16384);
        uint32_t *order = xalign((size_t)n * 4 + 16384);
        for (uint32_t i = 0; i < n; i++) { v[i] = i * 7u; a64[i].val = v[i]; a16[i].val = v[i]; order[i] = i; }
        for (uint32_t i = 0; i < n; i++) {
            a64[i].next = i + 1 < n ? &a64[i + 1] : NULL;
            a16[i].next = i + 1 < n ? &a16[i + 1] : NULL;
        }
        LCtx c = {a64, a16, a64, a16, v, n};
        cell("arr_u32", "scan", bytes, run_av, &c, n);
        cell("arr_node64", "scan", bytes, run_a64, &c, n);
        cell("list64_inorder", "walk", bytes, run_w64, &c, n);
        cell("arr_node16", "scan", bytes, run_a16, &c, n);
        cell("list16_inorder", "walk", bytes, run_w16, &c, n);
        rng_s = 0xA5A5A5A5u;
        for (uint32_t i = n - 1; i > 0; i--) {
            uint32_t j = (uint32_t)(((uint64_t)rnd() * (i + 1ull)) >> 32);
            uint32_t t = order[i]; order[i] = order[j]; order[j] = t;
        }
        for (uint32_t i = 0; i < n; i++) {
            a64[order[i]].next = i + 1 < n ? &a64[order[i + 1]] : NULL;
            a16[order[i]].next = i + 1 < n ? &a16[order[i + 1]] : NULL;
        }
        c.h64 = &a64[order[0]];
        c.h16 = &a16[order[0]];
        cell("list64_shuffled", "walk", bytes, run_w64, &c, n);
        cell("list16_shuffled", "walk", bytes, run_w16, &c, n);
        free(a64); free(a16); free(v); free(order);
    }
}

/* ---------- 정렬: 같은 코드, 시작 주소만 다르게 ---------- */
typedef uint32_t __attribute__((aligned(1))) u32_ua;
NOINLINE static uint32_t sum_ua(const u32_ua *x, uint32_t n) { uint32_t s = 0; for (uint32_t i = 0; i < n; i++) s += x[i]; return s; }
typedef struct __attribute__((packed)) { uint64_t a; uint32_t x; uint32_t rest[13]; } RecUa;
_Static_assert(sizeof(RecUa) == 64, "RecUa");
NOINLINE static void pos_ua(RecUa *a, uint32_t n) { for (uint32_t i = 0; i < n; i++) { a[i].x += a[i].rest[2]; a[i].rest[0] += a[i].rest[3]; a[i].rest[1] += a[i].rest[4]; } }

typedef struct { uint8_t *base; uint32_t n; } ACtx;
static void run_sum_ua(void *p) { ACtx *c = p; g_sink += sum_ua((const u32_ua *)c->base, c->n); }
static void run_pos_ua(void *p) { ACtx *c = p; pos_ua((RecUa *)c->base, c->n); }

static void bench_align(void) {
    static const size_t KB[] = {64, 4096, 65536};
    for (size_t k = 0; k < sizeof KB / sizeof KB[0]; k++) {
        size_t bytes = KB[k] * 1024;
        uint8_t *raw = xalign(bytes + 16384);
        for (size_t i = 0; i < bytes + 16384; i++) raw[i] = (uint8_t)rnd();
        static const size_t off[] = {0, 4, 1};
        static const char *name[] = {"+0B(line)", "+4B(natural)", "+1B(unaligned)"};
        for (int o = 0; o < 3; o++) {
            ACtx c = {raw + off[o], (uint32_t)(bytes / 4)};
            cell(name[o], "soa_sum_x", bytes, run_sum_ua, &c, c.n);
        }
        static const size_t off2[] = {0, 32, 1};
        static const char *name2[] = {"+0B(record on line)", "+32B(half straddle)", "+1B(unaligned)"};
        for (int o = 0; o < 3; o++) {
            ACtx c = {raw + off2[o], (uint32_t)(bytes / 64)};
            cell(name2[o], "aos_update_pos", bytes, run_pos_ua, &c, c.n);
        }
        free(raw);
    }
}

int main(int argc, char **argv) {
    const char *what = argc > 1 ? argv[1] : "all";
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    printf("# AW=%d sizeof Block=%zu\n", AW, sizeof(Block));
    if (!strcmp(what, "all") || !strcmp(what, "layouts")) bench_layouts();
    if (!strcmp(what, "all") || !strcmp(what, "list")) bench_list();
    if (!strcmp(what, "all") || !strcmp(what, "align")) bench_align();
    return (int)(g_sink & 0u);
}
