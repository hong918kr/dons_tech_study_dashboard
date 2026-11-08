/* DS-M03 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t rng = 1u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------- 연습 1: 크기 클래스 — 루프 / CLZ / 표 ---------- */
static int cls_loop(size_t n) {                  /* 본문 풀이와 같은 기준 구현 */
    if (n == 0 || n > 128) return -1;
    int c = 0;
    while (((size_t)1 << (4 + c)) < n) c++;
    return c;
}
static int cls_clz(size_t n) {
    if (n == 0 || n > 128) return -1;
    if (n <= 16) return 0;                       /* n-1 == 0 이면 __builtin_clz(0) 은 UB → 먼저 거른다 */
    return (32 - __builtin_clz((uint32_t)(n - 1))) - 4;   /* ceil(log2 n) - 4 */
}
static int cls_lut(size_t n) {
    static const int8_t lut[8] = { 0, 1, 2, 2, 3, 3, 3, 3 };   /* (n-1) >> 4 → 클래스 */
    if (n == 0 || n > 128) return -1;
    return lut[(n - 1) >> 4];
}

/* ---------- 연습 2: 슬랩 밖 메타데이터(side table) 미니 할당기 ---------- */
/* 헤더를 슬랩 안에 두지 않고 meta[(p - base) >> 12] 로 찾는다. 슬랩은 객체로만 채워진다.
 * 아레나가 4KB 정렬일 필요도 없다 — 마스크 대신 "base 를 빼고 시프트" 하므로. */
#define PS_SHIFT 12u
#define PS_SIZE  (1u << PS_SHIFT)
#define PN       16u
typedef struct { uint8_t *free; uint16_t size, nobj, inuse; bool used; } pmeta_t;
static _Alignas(16) uint8_t p_arena[PN][PS_SIZE];
static pmeta_t p_meta[PN];
static uint32_t g_locks;                         /* 슬랩 계층 진입 횟수 (= 락/임계 구역 진입 횟수) */

static uint8_t *nx(const uint8_t *o) { uint8_t *n; memcpy(&n, o, sizeof n); return n; }
static void set_nx(uint8_t *o, uint8_t *n) { memcpy(o, &n, sizeof n); }

static void p_init(void) { memset(p_meta, 0, sizeof p_meta); g_locks = 0; }

static pmeta_t *side_of(const void *p) {
    uintptr_t a = (uintptr_t)p, b = (uintptr_t)p_arena;
    if (a < b || a >= b + sizeof p_arena) return NULL;
    pmeta_t *m = &p_meta[(a - b) >> PS_SHIFT];
    uintptr_t off = (a - b) & (PS_SIZE - 1u);
    if (!m->used || off % m->size != 0 || off / m->size >= m->nobj) return NULL;
    return m;
}

static void *p_alloc(uint16_t size) {
    pmeta_t *m = NULL;
    for (uint32_t i = 0; i < PN && !m; i++)
        if (p_meta[i].used && p_meta[i].size == size && p_meta[i].free) m = &p_meta[i];
    for (uint32_t i = 0; i < PN && !m; i++)
        if (!p_meta[i].used) {                   /* 새 슬랩 포맷: 오프셋 0 부터 객체 */
            m = &p_meta[i];
            *m = (pmeta_t){ p_arena[i], size, (uint16_t)(PS_SIZE / size), 0, true };
            for (uint32_t k = 0; k < m->nobj; k++)
                set_nx(p_arena[i] + k * size, k + 1 < m->nobj ? p_arena[i] + (k + 1) * size : NULL);
        }
    if (!m) return NULL;
    uint8_t *o = m->free;
    m->free = nx(o);
    m->inuse++;
    return o;
}
static bool p_free(void *p) {
    pmeta_t *m = side_of(p);
    if (!m) return false;
    set_nx(p, m->free);
    m->free = p;
    if (--m->inuse == 0) m->used = false;        /* 빈 슬랩은 바로 다른 크기가 쓸 수 있게 */
    return true;
}

/* ---------- 연습 3: 임의 크기 캐시 — 나눗셈 대신 역수 곱으로 객체 인덱스 ---------- */
/* idx = floor(off / size) = (off * R) >> 32, R = ceil(2^32 / size). off < 2^32/size 이면 정확 */
static uint64_t recip(uint32_t size) { return (((uint64_t)1 << 32) + size - 1) / size; }   /* size=1 이면 2^32 */
static uint32_t obj_index(uint32_t off, uint64_t r) { return (uint32_t)(((uint64_t)off * r) >> 32); }

/* ---------- 연습 5: 매거진(per-CPU 캐시 흉내) — 슬랩 계층 진입 줄이기 ---------- */
#define MAG_CAP   16u
#define MAG_BATCH 8u
typedef struct { void *obj[MAG_CAP]; uint32_t n; uint16_t size; } mag_t;
static void *mag_alloc(mag_t *g) {
    if (g->n == 0 && ++g_locks)                  /* 비었으면 락 한 번에 한 묶음 채움 */
        for (uint32_t k = 0; k < MAG_BATCH; k++) {
            void *o = p_alloc(g->size);
            if (!o) break;
            g->obj[g->n++] = o;
        }
    return g->n ? g->obj[--g->n] : NULL;
}
static void mag_free(mag_t *g, void *p) {
    if (g->n == MAG_CAP && ++g_locks)            /* 가득 차면 락 한 번에 절반 반납 */
        for (uint32_t k = 0; k < MAG_BATCH; k++) p_free(g->obj[--g->n]);
    g->obj[g->n++] = p;
}
static void mag_drain(mag_t *g) { g_locks++; while (g->n) p_free(g->obj[--g->n]); }

/* 연습 5 트레이스: live 집합 최대 cap 개, 랜덤 alloc/free. use_mag 이면 매거진 경유 */
static bool run_trace(bool use_mag, uint32_t *calls) {
    static struct { uint8_t *p; uint8_t pat; } live[300];
    uint32_t n = 0;
    bool ok = true;
    mag_t g = { .n = 0, .size = 32 };
    p_init();
    rng = 777u;
    for (int op = 0; op < 100000 && ok; op++) {
        uint32_t r = xorshift32();
        if (n < 300 && (r & 1u)) {
            uint8_t *p = use_mag ? mag_alloc(&g) : (g_locks++, p_alloc(32));
            ok = p != NULL && side_of(p) != NULL;
            if (ok) { live[n].p = p; live[n].pat = (uint8_t)(r >> 8); memset(p, live[n].pat, 32); n++; }
        } else if (n > 0) {
            uint32_t i = (r >> 1) % n;
            uint8_t *p = live[i].p;
            for (int k = 0; k < 32; k++) ok = ok && p[k] == live[i].pat;
            live[i] = live[--n];
            if (use_mag) mag_free(&g, p); else { g_locks++; ok = ok && p_free(p); }
        }
    }
    while (n) { n--; if (use_mag) mag_free(&g, live[n].p); else { g_locks++; p_free(live[n].p); } }
    mag_drain(&g);
    for (uint32_t i = 0; i < PN; i++) ok = ok && !p_meta[i].used;   /* 전부 반납됐나 */
    *calls = g_locks;
    return ok;
}

int main(void) {
    /* 연습 1 */
    {
        bool ok = true;
        for (size_t n = 0; n <= 1024; n++) ok = ok && cls_clz(n) == cls_loop(n) && cls_lut(n) == cls_loop(n);
        CHECK(ok);
        CHECK(cls_clz((size_t)-1) == -1 && cls_lut(17) == 1 && cls_clz(65) == 3);
    }

    /* 연습 2: side table — 용량 비교 + 모든 바이트 주소에 대한 조회 */
    {
        printf("[2] class | header-in-slab nobj | side-table nobj\n");
        bool cap_ok = true;
        for (uint32_t s = 16; s <= 128; s <<= 1) {
            uint32_t a = (4096u - 64u) / s, b = 4096u / s;
            printf("[2] %5u | %19u | %15u\n", s, a, b);
            cap_ok = cap_ok && b == a + (64u + s - 1u) / s;
        }
        CHECK(cap_ok);
        p_init();
        uint8_t *x = p_alloc(32), *y = p_alloc(128);
        CHECK(x == p_arena[0] && y == p_arena[1] && p_meta[0].nobj == 128 && p_meta[1].nobj == 32);
        uint32_t hits = 0;
        bool ok = true;
        for (uint32_t i = 0; i < 3; i++)                       /* 슬랩 0(32B), 1(128B), 2(미사용) */
            for (uint32_t off = 0; off < PS_SIZE; off++) {
                pmeta_t *m = side_of(&p_arena[i][off]);
                bool want = i < 2 && off % (i == 0 ? 32u : 128u) == 0;
                ok = ok && (m != NULL) == want;
                hits += m != NULL;
            }
        CHECK(ok && hits == 128 + 32);
        CHECK(p_free(x) && p_free(y) && !p_meta[0].used && !p_free(x + 1));
    }

    /* 연습 3: 역수 곱 인덱스 — 크기 1..4096, 오프셋 0..4095 전수 검사 + 메시지 전용 캐시 */
    {
        bool ok = true;
        for (uint32_t s = 1; s <= 4096 && ok; s++) {
            uint64_t r = recip(s);
            for (uint32_t off = 0; off < 4096; off++)
                if (obj_index(off, r) != off / s) { ok = false; break; }
        }
        CHECK(ok);
        printf("[3] size | nobj | tail B | waste/obj in pow2 class\n");
        static const uint32_t sz[] = { 20, 24, 48, 72, 82 };
        bool t_ok = true;
        for (unsigned i = 0; i < 5; i++) {
            uint32_t nobj = (4096u - 64u) / sz[i], tail = 4096u - 64u - nobj * sz[i];
            uint32_t pow2 = 16; while (pow2 < sz[i]) pow2 <<= 1;
            printf("[3] %4u | %4u | %6u | %u\n", sz[i], nobj, tail, pow2 - sz[i]);
            t_ok = t_ok && tail < sz[i];
        }
        CHECK(t_ok && (4096u - 64u) % 72u == 0);
    }

    /* 연습 4: 슬랩 고정(pinning) — 슬랩마다 객체 하나만 살아 있어도 슬랩 전체가 묶인다 */
    {
        static uint8_t *p[PN * 256 + 1];
        p_init();
        uint32_t n = 0;
        while ((p[n] = p_alloc(16)) != NULL) n++;
        bool ok = n == PN * 256;
        for (uint32_t i = 0; i < n; i++) if (i % 256 != 0) ok = ok && p_free(p[i]);   /* 슬랩당 1개만 남김 */
        uint32_t used = 0;
        for (uint32_t i = 0; i < PN; i++) used += p_meta[i].used;
        printf("[4] live %u objs x 16 B = %u B pin %u slabs = %u B -> utilization %.2f%%; alloc(128) = %s\n",
               PN, PN * 16u, used, used * PS_SIZE, 100.0 * PN * 16.0 / (used * (double)PS_SIZE),
               p_alloc(128) ? "ok" : "NULL");
        CHECK(ok && used == PN && p_alloc(128) == NULL);
        for (uint32_t i = 0; i < n; i += 256) ok = ok && p_free(p[i]);
        CHECK(ok && p_alloc(128) != NULL);
    }

    /* 연습 5: 매거진 유무에 따른 슬랩 계층 진입 횟수 */
    {
        uint32_t c_plain = 0, c_mag = 0;
        bool a = run_trace(false, &c_plain), b = run_trace(true, &c_mag);
        printf("[5] slab-layer lock entries for 100000 ops: plain %u, magazine %u (%.1fx fewer)\n",
               c_plain, c_mag, (double)c_plain / c_mag);
        CHECK(a && b && c_mag * 4u < c_plain);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
