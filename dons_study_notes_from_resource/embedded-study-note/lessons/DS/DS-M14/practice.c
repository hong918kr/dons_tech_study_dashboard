/* DS-M14 추가 연습 — 한 번에 검증
 * cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 * [1] 카운팅 블룸 필터(4비트 카운터)로 삭제  [2] 위치 생성 방식 비교
 * [3] 켜진 비트 수로 원소 수 추정  [4] 합집합(OR)/교집합(AND)  [5] 블록 블룸 필터
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t fnv1a64(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 0x00000100000001B3ull; }
    return h;
}
static uint64_t fmix64(uint64_t x) {
    x ^= x >> 33; x *= 0xFF51AFD7ED558CCDull;
    x ^= x >> 33; x *= 0xC4CEB9FE1A85EC53ull;
    x ^= x >> 33;
    return x;
}
static uint64_t hkey(uint32_t key) { return fmix64(fnv1a64(&key, sizeof key)); }
static void hash2(uint32_t key, uint32_t *h1, uint32_t *h2) {
    uint64_t h = hkey(key);
    *h1 = (uint32_t)h; *h2 = (uint32_t)(h >> 32) | 1u;
}

static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------- 일반 블룸 필터 (본문과 같은 더블 해싱) ---------- */
typedef struct { uint32_t *w, m, mask, k; } bf_t;
static void bf_init(bf_t *b, uint32_t *w, uint32_t m, uint32_t k) {
    b->w = w; b->m = m; b->mask = m - 1u; b->k = k; memset(w, 0, (m / 32u) * sizeof w[0]);
}
static void bf_add(bf_t *b, uint32_t key) {
    uint32_t h1, h2; hash2(key, &h1, &h2);
    for (uint32_t i = 0; i < b->k; i++, h1 += h2) b->w[(h1 & b->mask) >> 5] |= 1u << (h1 & 31u);
}
static bool bf_has(const bf_t *b, uint32_t key) {
    uint32_t h1, h2; hash2(key, &h1, &h2);
    for (uint32_t i = 0; i < b->k; i++, h1 += h2)
        if (!((b->w[(h1 & b->mask) >> 5] >> (h1 & 31u)) & 1u)) return false;
    return true;
}
static uint32_t bf_pop(const bf_t *b) {
    uint32_t c = 0;
    for (uint32_t i = 0; i < b->m / 32u; i++) c += (uint32_t)__builtin_popcount(b->w[i]);
    return c;
}

/* ---------- [1] 카운팅 블룸 필터: 4비트 카운터 2개 / 바이트 ---------- */
#define CBF_M 4096u
#define CBF_K 5u
typedef struct { uint8_t c[CBF_M / 2]; uint32_t saturated; } cbf_t;
static unsigned cnt_get(const cbf_t *f, uint32_t i) { return (f->c[i >> 1] >> ((i & 1u) * 4u)) & 0xFu; }
static void cnt_put(cbf_t *f, uint32_t i, unsigned v) {
    unsigned sh = (i & 1u) * 4u;
    f->c[i >> 1] = (uint8_t)((f->c[i >> 1] & ~(0xFu << sh)) | (v << sh));
}
static void cbf_add(cbf_t *f, uint32_t key) {
    uint32_t h1, h2; hash2(key, &h1, &h2);
    for (uint32_t i = 0; i < CBF_K; i++, h1 += h2) {
        uint32_t p = h1 & (CBF_M - 1u);
        unsigned v = cnt_get(f, p);
        if (v == 15u) { f->saturated++; continue; }   /* 포화: 15 에 고정 (거짓 음성 방지) */
        cnt_put(f, p, v + 1u);
    }
}
static bool cbf_has(const cbf_t *f, uint32_t key) {
    uint32_t h1, h2; hash2(key, &h1, &h2);
    for (uint32_t i = 0; i < CBF_K; i++, h1 += h2) if (cnt_get(f, h1 & (CBF_M - 1u)) == 0) return false;
    return true;
}
/* "아마 있음"일 때만 감소. 하지만 거짓 양성 키는 이 검사를 통과한다! */
static bool cbf_remove(cbf_t *f, uint32_t key) {
    if (!cbf_has(f, key)) return false;
    uint32_t h1, h2; hash2(key, &h1, &h2);
    for (uint32_t i = 0; i < CBF_K; i++, h1 += h2) {
        uint32_t p = h1 & (CBF_M - 1u);
        unsigned v = cnt_get(f, p);
        if (v != 15u) cnt_put(f, p, v - 1u);           /* 포화된 카운터는 줄이지 않는다 */
    }
    return true;
}
static uint32_t cbf_sum(const cbf_t *f) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < CBF_M; i++) s += cnt_get(f, i);
    return s;
}

/* ---------- [2] 위치 생성 방식 3가지 ---------- */
enum { POS_DOUBLE, POS_LINEAR, POS_INDEP };
static uint32_t pos_of(int mode, uint32_t key, uint64_t h, uint32_t i) {
    uint32_t h1 = (uint32_t)h, h2 = (uint32_t)(h >> 32) | 1u;
    switch (mode) {
    case POS_DOUBLE: return h1 + i * h2;                        /* Kirsch–Mitzenmacher */
    case POS_LINEAR: return h1 + i;                             /* 연속 k 비트 — 나쁜 예 */
    default:         return (uint32_t)fmix64(((uint64_t)i << 32 | key) * 0x9E3779B97F4A7C15ull + i);
    }
}
static double fp_mode(int mode, uint32_t *w, uint32_t n, uint32_t m, uint32_t k, uint32_t q) {
    memset(w, 0, (m / 32u) * sizeof w[0]);
    for (uint32_t x = 0; x < n; x++) {
        uint64_t h = hkey(x << 1);
        for (uint32_t i = 0; i < k; i++) { uint32_t g = pos_of(mode, x << 1, h, i) & (m - 1u); w[g >> 5] |= 1u << (g & 31u); }
    }
    uint32_t hit = 0;
    for (uint32_t j = 0; j < q; j++) {
        uint32_t key = (j << 1) | 1u;
        uint64_t h = hkey(key);
        bool all = true;
        for (uint32_t i = 0; i < k && all; i++) { uint32_t g = pos_of(mode, key, h, i) & (m - 1u); all = (w[g >> 5] >> (g & 31u)) & 1u; }
        hit += all;
    }
    return (double)hit / q;
}

/* ---------- [3] 원소 수 추정: X 켜진 비트 → n^ = -(m/k) ln(1 - X/m) ---------- */
static double bf_estimate_n(const bf_t *b) {
    double x = bf_pop(b);
    if (x >= b->m) return INFINITY;                              /* 포화: 추정 불가 */
    return -(double)b->m / b->k * log(1.0 - x / b->m);
}

/* ---------- [5] 블록 블룸 필터: 512비트(64B = 캐시 라인) 블록 하나에 k 비트 ---------- */
#define BLK_BITS 512u
/* 블록 안 위치 i: mode 0 = 블록 안 더블 해싱, mode 1 = 64비트 해시를 9비트씩 잘라 쓰기 */
typedef struct { uint64_t h, bits; uint32_t left, a, d; } blk_it;
static uint32_t blk_start(blk_it *it, uint32_t nblk, uint32_t key) {
    it->h = hkey(key);
    it->a = (uint32_t)(it->h >> 32); it->d = ((uint32_t)it->h >> 16) | 1u;
    it->bits = it->h >> 7; it->left = 57;                       /* 하위 7비트는 블록 번호 */
    return ((uint32_t)it->h & (nblk - 1u)) * BLK_BITS;
}
static uint32_t blk_next(blk_it *it, int mode) {
    if (mode == 0) { uint32_t g = it->a & (BLK_BITS - 1u); it->a += it->d; return g; }
    if (it->left < 9) { it->h = fmix64(it->h + 0x9E3779B97F4A7C15ull); it->bits = it->h; it->left = 64; }
    uint32_t g = (uint32_t)it->bits & (BLK_BITS - 1u);
    it->bits >>= 9; it->left -= 9;
    return g;
}
static void blk_add(uint32_t *w, uint32_t nblk, uint32_t k, int mode, uint32_t key) {
    blk_it it;
    uint32_t base = blk_start(&it, nblk, key);
    for (uint32_t i = 0; i < k; i++) { uint32_t g = base + blk_next(&it, mode); w[g >> 5] |= 1u << (g & 31u); }
}
static bool blk_has(const uint32_t *w, uint32_t nblk, uint32_t k, int mode, uint32_t key) {
    blk_it it;
    uint32_t base = blk_start(&it, nblk, key);
    for (uint32_t i = 0; i < k; i++) {
        uint32_t g = base + blk_next(&it, mode);
        if (!((w[g >> 5] >> (g & 31u)) & 1u)) return false;
    }
    return true;
}
/* 이론: 블록 부하 L ~ Poisson(n/nblk), FP = Σ P(L) (1 - e^{-kL/B})^k */
static double blk_fp_theory(uint32_t n, uint32_t nblk, uint32_t k) {
    double lam = (double)n / nblk, p = exp(-lam), s = 0;
    for (uint32_t L = 0; L < 400; L++) {
        s += p * pow(1.0 - exp(-(double)k * L / BLK_BITS), k);
        p *= lam / (L + 1);
    }
    return s;
}

static cbf_t g_cbf;
static uint32_t g_w[65536 / 32], g_w2[65536 / 32], g_w3[65536 / 32];

int main(void) {
    /* [1] 카운팅 블룸 필터: 랜덤 add/remove 20000 회, 참조 = 키별 개수 */
    {
        static uint8_t ref[600];
        memset(&g_cbf, 0, sizeof g_cbf);
        rng_state = 0xC0B1u;
        uint32_t total = 0;
        bool ok = true;
        for (int op = 0; op < 20000 && ok; op++) {
            uint32_t r = xorshift32(), id = r % 600u;
            if ((r >> 16) % 3u != 0 && ref[id] < 2u) { cbf_add(&g_cbf, 7000u + id); ref[id]++; total++; }
            else if (ref[id] > 0) { ok = cbf_remove(&g_cbf, 7000u + id); ref[id]--; total--; }
            /* 포화가 없다면: 카운터 합 == k * (넣은 개수, 중복 포함) — add 는 +1 을 k 번 */
            ok = ok && (g_cbf.saturated > 0 || cbf_sum(&g_cbf) == CBF_K * total);
            if ((op & 63) == 0)
                for (uint32_t j = 0; j < 600 && ok; j++) ok = !ref[j] || cbf_has(&g_cbf, 7000u + j);
        }
        for (uint32_t j = 0; j < 600 && ok; j++) while (ref[j]) { ok = ok && cbf_remove(&g_cbf, 7000u + j); ref[j]--; }
        CHECK(ok && g_cbf.saturated == 0 && cbf_sum(&g_cbf) == 0);
        printf("[1] counting BF random add/remove: %s, all counters back to 0\n", ok ? "ok" : "BROKEN");

        /* 함정: 넣은 적 없는 키(거짓 양성)를 지우면 진짜 키가 거짓 음성이 된다 */
        memset(&g_cbf, 0, sizeof g_cbf);
        for (uint32_t x = 0; x < 600; x++) cbf_add(&g_cbf, x);
        uint32_t ghost = 0, fn = 0;
        for (uint32_t q = 1000000; q < 2000000; q++) if (cbf_has(&g_cbf, q)) { ghost = q; break; }
        bool removed = ghost && cbf_remove(&g_cbf, ghost);
        for (uint32_t x = 0; x < 600; x++) fn += !cbf_has(&g_cbf, x);
        CHECK(removed && fn > 0);
        printf("[1] removed never-inserted key %u (a false positive): %u of 600 real keys now false negative\n",
               ghost, fn);
    }

    /* [2] 위치 생성 방식: 더블 해싱 vs h1+i vs k개 독립 해시 */
    {
        static const char *name[] = { "double h1+i*h2", "linear h1+i   ", "k independent " };
        static const uint32_t cfg[][3] = { { 8192, 65536, 6 }, { 4096, 65536, 11 } };
        const uint32_t q = 200000;
        bool ok = true;
        for (unsigned c = 0; c < 2; c++) {
            uint32_t n = cfg[c][0], m = cfg[c][1], k = cfg[c][2];
            double th = pow(1.0 - exp(-(double)k * n / m), k), fp[3];
            printf("[2] n=%u m=%u k=%u  theory %.5f\n", n, m, k, th);
            for (int mode = 0; mode < 3; mode++) {
                fp[mode] = fp_mode(mode, g_w, n, m, k, q);
                printf("[2]   %s FP %.5f  (x%.2f)\n", name[mode], fp[mode], fp[mode] / th);
            }
            double sd = sqrt(th * (1 - th) / q);
            ok = ok && fabs(fp[POS_DOUBLE] - fp[POS_INDEP]) < 6 * sd && fp[POS_LINEAR] > 1.5 * fp[POS_INDEP];
        }
        CHECK(ok);
    }

    /* [3] 원소 수 추정 */
    {
        static const uint32_t ns[] = { 100, 1000, 4000, 8000 };
        bf_t b;
        bool ok = true;
        for (unsigned c = 0; c < 4; c++) {
            bf_init(&b, g_w, 65536, 6);
            for (uint32_t x = 0; x < ns[c]; x++) bf_add(&b, 0xA0000000u + x);
            double est = bf_estimate_n(&b);
            printf("[3] n=%5u  set bits=%5u  estimate=%8.1f  (err %+.2f%%)\n", ns[c], bf_pop(&b), est,
                   100.0 * (est - ns[c]) / ns[c]);
            ok = ok && fabs(est - ns[c]) <= 0.03 * ns[c] + 3;
        }
        bf_init(&b, g_w, 64, 4);
        for (uint32_t x = 0; x < 5000; x++) bf_add(&b, x);
        CHECK(ok && isinf(bf_estimate_n(&b)));
    }

    /* [4] 합집합 = OR (비트 단위로 정확), 교집합 AND 는 근사 */
    {
        bf_t a, b, u;
        bf_init(&a, g_w, 16384, 7); bf_init(&b, g_w2, 16384, 7); bf_init(&u, g_w3, 16384, 7);
        for (uint32_t x = 0; x < 1000; x++) bf_add(&a, x);
        for (uint32_t x = 500; x < 1500; x++) bf_add(&b, x);
        for (uint32_t x = 0; x < 1500; x++) bf_add(&u, x);
        bool or_exact = true;
        for (uint32_t i = 0; i < 16384 / 32; i++) or_exact = or_exact && (a.w[i] | b.w[i]) == u.w[i];
        CHECK(or_exact);
        /* AND: g_w := A & B, g_w3 := filter(A∩B) 를 새로 만듦 */
        for (uint32_t i = 0; i < 16384 / 32; i++) a.w[i] &= b.w[i];
        bf_init(&u, g_w3, 16384, 7);
        for (uint32_t x = 500; x < 1000; x++) bf_add(&u, x);
        bool superset = true, has_all = true;
        for (uint32_t i = 0; i < 16384 / 32; i++) superset = superset && (a.w[i] & u.w[i]) == u.w[i];
        for (uint32_t x = 500; x < 1000; x++) has_all = has_all && bf_has(&a, x);
        uint32_t fa = 0, fu = 0;
        for (uint32_t x = 2000000; x < 3000000; x++) { fa += bf_has(&a, x); fu += bf_has(&u, x); }
        printf("[4] OR == filter(A u B): %s | AND bits %u vs filter(A n B) bits %u | FP AND %.5f vs %.5f\n",
               or_exact ? "bit-exact" : "NO", bf_pop(&a), bf_pop(&u), fa / 1e6, fu / 1e6);
        CHECK(superset && has_all && bf_pop(&a) > bf_pop(&u) && fa > fu);
    }

    /* [5] 블록 블룸 필터 vs 일반 — 같은 m, n, k */
    {
        const uint32_t m = 65536, nblk = m / BLK_BITS, k = 6, q = 200000;
        static const uint32_t ns[] = { 4096, 8192 };
        bool ok = true;
        for (unsigned c = 0; c < 2; c++) {
            uint32_t n = ns[c];
            bf_t b;
            bf_init(&b, g_w, m, k);
            memset(g_w2, 0, sizeof g_w2); memset(g_w3, 0, sizeof g_w3);
            for (uint32_t x = 0; x < n; x++) {
                bf_add(&b, x << 1); blk_add(g_w2, nblk, k, 0, x << 1); blk_add(g_w3, nblk, k, 1, x << 1);
            }
            uint32_t h_std = 0, h_dh = 0, h_fld = 0, fn = 0;
            for (uint32_t x = 0; x < n; x++) fn += !blk_has(g_w2, nblk, k, 0, x << 1) + !blk_has(g_w3, nblk, k, 1, x << 1);
            for (uint32_t j = 0; j < q; j++) {
                h_std += bf_has(&b, (j << 1) | 1u);
                h_dh  += blk_has(g_w2, nblk, k, 0, (j << 1) | 1u);
                h_fld += blk_has(g_w3, nblk, k, 1, (j << 1) | 1u);
            }
            double th = pow(1.0 - exp(-(double)k * n / m), k), tb = blk_fp_theory(n, nblk, k);
            double fs = (double)h_std / q, fd = (double)h_dh / q, ff = (double)h_fld / q;
            printf("[5] n=%u m/n=%.0f k=%u | standard %.5f (theory %.5f) | blocked: 9-bit fields %.5f, "
                   "in-block double hash %.5f (theory %.5f)\n", n, (double)m / n, k, fs, th, ff, fd, tb);
            ok = ok && fn == 0 && ff > fs && fd > ff && fabs(ff - tb) < 6 * sqrt(tb * (1 - tb) / q);
        }
        CHECK(ok);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
