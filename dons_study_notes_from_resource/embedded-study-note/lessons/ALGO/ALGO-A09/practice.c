/* ALGO-A09 추가 연습 — 한 번에 검증
 *   연습 1  겹침 쌍의 "개수만" O(n log n) — k 가 n^2 급이어도 열거하지 않는다
 *   연습 2  고장 주소(MMFAR/BFAR) → 섹션 찾기: 겹침 없는 맵에서 이진 탐색
 *   연습 3  ISR 로그가 단일 코어에서 가능한가(적층·laminar 검사) + 최대 중첩 깊이 (스택 하나로)
 *   연습 4  겹쳐 그린 넓이(overdraw): 두 번 이상 덮인 넓이 — 슬랩 + 띠 안 1D 깊이 스위프
 *   연습 5  32비트 타임스탬프 랩어라운드 풀기 (DWT CYCCNT / µs 타이머) → uint64 타임라인
 * 기준: 모든 쌍 · 선형 탐색 · 정수 격자 전수 · 생성기가 아는 정답. xorshift32 고정 시드.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t g_rng = 0xA09F00Du;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

static void sort_u64(uint64_t *a, size_t n, uint64_t *tmp) {     /* 상향식 병합 정렬 (본문과 같음) */
    uint64_t *src = a, *dst = tmp;
    for (size_t w = 1; w < n; w *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * w) {
            size_t mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n, i = lo, j = mid, k = lo;
            while (i < mid && j < hi) dst[k++] = src[i] <= src[j] ? src[i++] : src[j++];
            while (i < mid) dst[k++] = src[i++];
            while (j < hi) dst[k++] = src[j++];
        }
        uint64_t *t = src; src = dst; dst = t;
    }
    if (src != a) memcpy(a, src, n * sizeof *a);
}

#define NMAX 4096
static uint64_t g_ev[2 * NMAX], g_tmp[2 * NMAX];
typedef struct { uint32_t base, size; } reg_t;
static inline uint64_t rend(const reg_t *r) { return (uint64_t)r->base + r->size; }

/* ---- 연습 1: 개수만. 키 = (좌표 << 1) | START. 같은 좌표면 END(0) 먼저 → 맞닿음은 안 센다. ---- */
static uint64_t count_pairs(const reg_t *r, size_t n) {
    size_t ne = 0;
    for (size_t i = 0; i < n; i++) {
        if (r[i].size == 0) continue;
        g_ev[ne++] = ((uint64_t)r[i].base << 1) | 1u;
        g_ev[ne++] = rend(&r[i]) << 1;
    }
    sort_u64(g_ev, ne, g_tmp);
    uint64_t active = 0, k = 0;
    for (size_t e = 0; e < ne; e++) {
        if (g_ev[e] & 1u) { k += active; active++; }   /* 새 영역은 지금 활성인 모두와 겹친다 */
        else active--;
    }
    return k;
}
static uint64_t count_brute(const reg_t *r, size_t n) {
    uint64_t k = 0;
    for (size_t a = 0; a < n; a++)
        for (size_t b = a + 1; b < n; b++)
            if (r[a].size && r[b].size && r[a].base < rend(&r[b]) && r[b].base < rend(&r[a])) k++;
    return k;
}

/* ---- 연습 2: base 오름차순 · 서로 안 겹치는 섹션 표에서 addr 를 담은 섹션 번호 (없으면 -1). O(log n) ---- */
static int find_region(const reg_t *s, size_t n, uint32_t addr) {
    size_t lo = 0, hi = n;                      /* 불변식: [0,lo) 의 base <= addr, [hi,n) 의 base > addr */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s[mid].base <= addr) lo = mid + 1; else hi = mid;
    }
    if (lo == 0) return -1;                     /* lo-1 = base <= addr 인 마지막 섹션 — 후보는 그것뿐 */
    return (uint64_t)addr < rend(&s[lo - 1]) ? (int)(lo - 1) : -1;
}
static int find_linear(const reg_t *s, size_t n, uint32_t addr) {
    for (size_t i = 0; i < n; i++) if (s[i].base <= addr && (uint64_t)addr < rend(&s[i])) return (int)i;
    return -1;
}

/* ---- 연습 3: 단일 코어 ISR 로그는 적층(laminar)이어야 한다: 두 구간은 서로소이거나 한쪽이 다른 쪽을 포함.
 * (t0 오름차순, 같으면 t1 내림차순 = 바깥 먼저)으로 정렬해 스택으로 검사. 위반이면 -1, 아니면 최대 깊이. ---- */
typedef struct { uint32_t t0, t1; } iv_t;
static int laminar_depth(const iv_t *v, size_t n) {
    static uint64_t key[NMAX], tmp[NMAX];
    static uint32_t stk[NMAX];
    size_t m = 0, sp = 0;
    int best = 0;
    for (size_t i = 0; i < n; i++)
        if (v[i].t1 > v[i].t0) key[m++] = ((uint64_t)v[i].t0 << 32) | (uint32_t)~v[i].t1;   /* t1 내림차순 */
    sort_u64(key, m, tmp);
    for (size_t i = 0; i < m; i++) {
        uint32_t t0 = (uint32_t)(key[i] >> 32), t1 = ~(uint32_t)key[i];
        while (sp > 0 && stk[sp - 1] <= t0) sp--;     /* 이미 끝난 바깥 구간 정리 (반개구간: 끝 == 시작이면 끝남) */
        if (sp > 0 && t1 > stk[sp - 1]) return -1;    /* 안에서 시작했는데 바깥보다 늦게 끝남 = 부분 겹침 */
        stk[sp++] = t1;
        if ((int)sp > best) best = (int)sp;
    }
    return best;
}
static int depth_brute(const iv_t *v, size_t n, uint32_t T) {
    int best = 0;
    for (uint32_t t = 0; t <= T; t++) {
        int d = 0;
        for (size_t i = 0; i < n; i++) d += v[i].t0 <= t && t < v[i].t1;
        if (d > best) best = d;
    }
    return best;
}
static bool laminar_brute(const iv_t *v, size_t n) {
    for (size_t a = 0; a < n; a++)
        for (size_t b = 0; b < n; b++) {
            if (a == b || v[a].t1 <= v[a].t0 || v[b].t1 <= v[b].t0) continue;
            bool overlap = v[a].t0 < v[b].t1 && v[b].t0 < v[a].t1;
            bool a_in_b = v[b].t0 <= v[a].t0 && v[a].t1 <= v[b].t1, b_in_a = v[a].t0 <= v[b].t0 && v[b].t1 <= v[a].t1;
            if (overlap && !a_in_b && !b_in_a) return false;
        }
    return true;
}
/* 적층 로그 생성기: [lo, hi) 안에 서로소 자식들을 놓고 재귀적으로 안을 채운다 (깊이 제한) */
static size_t gen_laminar(iv_t *v, size_t n, uint32_t lo, uint32_t hi, int depth) {
    uint32_t t = lo;
    while (n < 60 && t + 2 < hi && xs32() % 4 != 0) {
        uint32_t a = t + xs32() % 3, b = a + 1 + xs32() % (hi - a);
        if (b > hi) b = hi;
        if (a >= b) break;
        v[n++] = (iv_t){a, b};
        if (depth < 5) n = gen_laminar(v, n, a, b, depth + 1);
        t = b;
    }
    return n;
}

/* ---- 연습 4: 두 번 이상 덮인 넓이. 띠마다 y 끝점을 (+1/-1) 사건으로 정렬해 깊이 >= 2 인 길이를 잰다. O(n^2 log n) ---- */
typedef struct { int32_t x1, y1, x2, y2; } rect_t;
static uint64_t overdraw_area(const rect_t *r, size_t n) {
    static int32_t xs[2 * NMAX];
    static uint64_t ev[2 * NMAX], tmp[2 * NMAX];
    const int64_t B = (int64_t)1 << 31;
    size_t m = 0, nx = 0;
    for (size_t i = 0; i < n; i++) {
        if (r[i].x1 >= r[i].x2 || r[i].y1 >= r[i].y2) continue;
        ev[m++] = (uint64_t)((int64_t)r[i].x1 + B);
        ev[m++] = (uint64_t)((int64_t)r[i].x2 + B);
    }
    sort_u64(ev, m, tmp);
    for (size_t i = 0; i < m; i++) if (i == 0 || ev[i] != ev[i - 1]) xs[nx++] = (int32_t)((int64_t)ev[i] - B);
    uint64_t area = 0;
    for (size_t s = 0; s + 1 < nx; s++) {
        size_t q = 0;
        for (size_t i = 0; i < n; i++)
            if (r[i].x1 < r[i].x2 && r[i].y1 < r[i].y2 && r[i].x1 <= xs[s] && xs[s + 1] <= r[i].x2) {
                ev[q++] = (uint64_t)((int64_t)r[i].y1 + B) << 1 | 1u;    /* 같은 y 면 끝(0) 먼저 — 넓이엔 무관 */
                ev[q++] = (uint64_t)((int64_t)r[i].y2 + B) << 1;
            }
        sort_u64(ev, q, tmp);
        int d = 0;
        uint64_t len = 0;
        for (size_t e = 0; e < q; e++) {
            int64_t y = (int64_t)(ev[e] >> 1) - B;
            if (d >= 2 && e > 0) len += (uint64_t)(y - ((int64_t)(ev[e - 1] >> 1) - B));
            d += (ev[e] & 1u) ? 1 : -1;
        }
        area += len * (uint64_t)((int64_t)xs[s + 1] - xs[s]);
    }
    return area;
}
static uint64_t overdraw_grid(const rect_t *r, size_t n, int G) {
    uint64_t c = 0;
    for (int x = 0; x < G; x++)
        for (int y = 0; y < G; y++) {
            int d = 0;
            for (size_t i = 0; i < n; i++) d += r[i].x1 <= x && x < r[i].x2 && r[i].y1 <= y && y < r[i].y2;
            c += d >= 2;
        }
    return c;
}

/* ---- 연습 5: 32비트 시각열(진입 시각이 단조 증가, 이웃 간격 < 2^31)을 64비트로 풀기 ---- */
static void unwrap32(const uint32_t *t, size_t n, uint64_t *out) {
    uint64_t acc = 0;
    for (size_t i = 0; i < n; i++) {
        if (i == 0) acc = t[0];
        else acc += (uint32_t)(t[i] - t[i - 1]);   /* 부호 없는 뺄셈 = 모듈러 2^32 차이 = 실제 경과 (간격 < 2^32) */
        out[i] = acc;
    }
}

int main(void) {
    /* 연습 1 */
    {
        static reg_t r[NMAX];
        size_t bad = 0;
        for (int it = 0; it < 1500; it++) {
            size_t n = xs32() % 80;
            for (size_t i = 0; i < n; i++) r[i] = (reg_t){xs32() % 100, xs32() % 5 ? xs32() % 30 : 0};
            if (count_pairs(r, n) != count_brute(r, n)) bad++;
        }
        CHECK(bad == 0);
        for (size_t i = 0; i < 4000; i++) r[i] = (reg_t){0xFFFFFF00u, 0x100};      /* 2^32 에서 끝남, 전부 같음 */
        CHECK(count_pairs(r, 4000) == 4000ull * 3999 / 2);
        reg_t touch[] = {{0, 10}, {10, 10}, {20, 0}, {20, 1}};
        CHECK(count_pairs(touch, 4) == 0);
    }
    /* 연습 2 */
    {
        static const reg_t sec[] = {{0x08000000u, 0x10000}, {0x08010000u, 0x4000}, {0x08020000u, 0x60000},
                                    {0x20000000u, 0x800}, {0x20000800u, 0x3000}, {0xE0000000u, 0x1000}, {0xFFFFF000u, 0x1000}};
        size_t n = sizeof sec / sizeof sec[0];
        CHECK(find_region(sec, n, 0x08013FFFu) == 1 && find_region(sec, n, 0x08014000u) == -1     /* 끝 = 다음 칸(빈틈) */
              && find_region(sec, n, 0x07FFFFFFu) == -1 && find_region(sec, n, 0xFFFFFFFFu) == 6
              && find_region(sec, n, 0x20000800u) == 4 && find_region(sec, 0, 0x20000800u) == -1);
        size_t bad = 0;
        for (int it = 0; it < 200000; it++) {
            uint32_t a = xs32() % 4 ? sec[xs32() % n].base + (xs32() % 0x70000) - 0x100 : xs32();
            if (find_region(sec, n, a) != find_linear(sec, n, a)) bad++;
        }
        CHECK(bad == 0);
    }
    /* 연습 3 */
    {
        static const iv_t trace[] = {{100, 160}, {120, 140}, {130, 135}, {300, 420}, {310, 400}, {320, 390},
                                     {330, 333}, {500, 560}, {530, 532}, {600, 640}, {640, 660}, {1000, 1003}};
        CHECK(laminar_depth(trace, 12) == 4);
        iv_t broken[] = {{100, 160}, {150, 170}};                 /* 바깥보다 늦게 끝나는 안쪽 = 단일 코어에선 불가능 */
        CHECK(laminar_depth(broken, 2) == -1);
        iv_t same[] = {{0, 10}, {0, 10}, {0, 4}, {10, 12}};       /* 같은 구간 두 번 = 서로 포함 (로거 중복?) */
        CHECK(laminar_depth(same, 4) == 3);
        static iv_t v[64];
        size_t bad = 0, broken_seen = 0;
        for (int it = 0; it < 2000; it++) {
            size_t n = gen_laminar(v, 0, 0, 200, 0);
            if (it % 3 == 0 && n >= 2) { size_t i = xs32() % n; v[i].t1 += 1 + xs32() % 20; }   /* 가끔 깨뜨린다 */
            int got = laminar_depth(v, n);
            bool lam = laminar_brute(v, n);
            if (!lam) broken_seen++;
            if (lam ? got != depth_brute(v, n, 240) : got != -1) bad++;
        }
        printf("laminar: 2000 random logs, %zu non-laminar, mismatches=%zu\n", broken_seen, bad);
        CHECK(bad == 0 && broken_seen > 100);
    }
    /* 연습 4 */
    {
        rect_t two[] = {{0, 0, 4, 4}, {2, 2, 6, 6}, {10, 10, 12, 12}};
        CHECK(overdraw_area(two, 3) == 4);
        static rect_t r[64];
        size_t bad = 0;
        for (int it = 0; it < 800; it++) {
            size_t n = xs32() % 20;
            for (size_t i = 0; i < n; i++) {
                int32_t x = (int32_t)(xs32() % 32), y = (int32_t)(xs32() % 32);
                r[i] = (rect_t){x, y, x + (int32_t)(xs32() % 12) - 1, y + (int32_t)(xs32() % 12) - 1};
                if (r[i].x2 > 32) r[i].x2 = 32;
                if (r[i].y2 > 32) r[i].y2 = 32;
            }
            if (overdraw_area(r, n) != overdraw_grid(r, n, 32)) bad++;
        }
        CHECK(bad == 0);
        static const rect_t lcd[] = {{0, 0, 240, 20}, {180, 0, 240, 20}, {200, 2, 236, 18}, {10, 40, 230, 200},
                                     {100, 100, 140, 140}, {20, 180, 120, 230}, {60, 150, 180, 250}};
        uint64_t od = overdraw_area(lcd, 7);
        printf("LCD dirty rects: overdraw (covered >= 2) = %" PRIu64 " px\n", od);
        CHECK(od == overdraw_grid(lcd, 7, 320));
    }
    /* 연습 5 */
    {
        static uint32_t t32[2000];
        static uint64_t t64[2000], got[2000];
        size_t bad = 0;
        for (int it = 0; it < 200; it++) {
            uint64_t t = 0xFFFFF000ull * (uint64_t)(xs32() % 4) + xs32();     /* 2^32 근처에서 시작하기도 */
            size_t n = 1 + xs32() % 2000;
            for (size_t i = 0; i < n; i++) {
                t += xs32() % 4 == 0 ? (xs32() & 0x7FFFFFFFu) : xs32() % 5000;  /* 간격 < 2^31 */
                t64[i] = t; t32[i] = (uint32_t)t;
            }
            unwrap32(t32, n, got);
            for (size_t i = 0; i < n; i++) if (got[i] - got[0] != t64[i] - t64[0]) { bad++; break; }
        }
        CHECK(bad == 0);
        uint32_t w[] = {0xFFFFFFF0u, 0xFFFFFFFEu, 0x00000005u, 0x00000100u};
        uint64_t o[4];
        unwrap32(w, 4, o);
        CHECK(o[1] == 0xFFFFFFFEull && o[2] == 0x100000005ull && o[3] == 0x100000100ull);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
