/* OS-M05 추가 연습 — 한 번에 검증
 *   연습 1  비어 있지 않은 클래스 비트맵: 다음 클래스를 O(1) 로 (TLSF 의 1단계, DS-A06)
 *   연습 2  요청 크기 r 기준 외부 단편화 E(r) = (r 보다 작은 free 블록 바이트) / 전체 free
 *   연습 3  2의 거듭제곱 반올림(segregated storage / 버디) vs 우리 분할: 요청당 내부 단편화
 *   연습 4  푸터 생략(prev_alloc 비트): 할당 블록 헤더만 8 B 일 때 줄어드는 내부 단편화
 * 엔진은 solution.c 를 그대로 가져온다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#define MM_NO_MAIN
#include "solution.c"

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* ---- 연습 1 ---- */
static int next_class(uint32_t mask, int c) {       /* c 이상에서 비어 있지 않은 가장 작은 클래스, 없으면 -1 */
    if (c >= NCLASS) return -1;
    uint32_t m = mask & (~0u << c);
    return m ? __builtin_ctz(m) : -1;
}
static int next_class_linear(uint32_t mask, int c) {
    for (; c < NCLASS; c++) if (mask & (1u << c)) return c;
    return -1;
}
static uint32_t class_mask(void) {
    uint32_t m = 0;
    for (int c = 0; c < NCLASS; c++) if (g_head[c]) m |= 1u << c;
    return m;
}
static void *seg_find_bitmap(size_t asize, uint32_t mask) {   /* 자기 클래스는 훑고, 위는 비트맵으로 한 번에 */
    int c = size_class(asize);
    for (void *bp = g_head[c]; bp; bp = nextf(bp)) if (bsize(bp) >= asize) return bp;
    int u = next_class(mask, c + 1);
    return u < 0 ? NULL : g_head[u];
}

/* ---- 연습 2 ---- */
static double ext_frag_for(size_t r) {
    size_t small = 0, total = 0;
    for (char *bp = next_blk(heap_listp); bsize(bp) > 0; bp = next_blk(bp))
        if (!balloc(bp)) { total += bsize(bp); if (bsize(bp) < r) small += bsize(bp); }
    return total ? (double)small / (double)total : 0.0;
}

/* ---- 연습 3, 4 ---- */
static size_t pow2_block(size_t req) {               /* 헤더/푸터 16 B 는 같게, 블록을 2의 거듭제곱으로 */
    size_t b = 32;
    while (b < req + DSIZE) b <<= 1;
    return b;
}
static size_t elided_block(size_t req) {             /* 할당 블록은 헤더 8 B 만 (푸터는 free 일 때만) */
    size_t a = (req + WSIZE + 15) & ~(size_t)15;
    return a < MINBLK ? MINBLK : a;
}
typedef struct { size_t n, ours, pow2, elided; } waste_t;
static waste_t waste_of(const trace_t *t) {          /* 트레이스의 모든 alloc/realloc 요청마다 (블록 − 요청) 합 */
    waste_t w = { 0, 0, 0, 0 };
    for (int k = 0; k < t->n; k++) {
        if (t->op[k].op == 'f') continue;
        size_t r = t->op[k].size;
        w.n++;
        w.ours += adjust(r) - r;
        w.pow2 += pow2_block(r) - r;
        w.elided += elided_block(r) - r;
    }
    return w;
}

int main(void) {
    gen_traces();

    printf("-- 연습 1: non-empty class bitmap\n");
    long bad = 0;
    for (uint32_t m = 0; m < (1u << NCLASS); m++)
        for (int c = 0; c <= NCLASS; c++)
            if (next_class(m, c) != next_class_linear(m, c)) bad++;
    CHECK(bad == 0);                                   /* 2^17 마스크 × 18 시작점 전부 */
    ropt_t mid = { 0, true, false, 1200 };
    replay(&g_tr[1], POL_SEG, mid);                    /* T2 를 1200 연산에서 멈춘 힙 */
    uint32_t mask = class_mask();
    printf("  T2@1200 non-empty classes mask = 0x%05x\n", (unsigned)mask);
    long diff = 0;
    for (size_t a = 32; a <= 70000; a += 16) {
        unsigned long v = 0;
        if (seg_find_bitmap(a, mask) != seg_find(a, &v)) diff++;
    }
    CHECK(diff == 0 && mm_check(0) == 0);              /* 같은 블록을 고른다 */

    printf("-- 연습 2: request-aware external fragmentation E(r)\n");
    mm_set_policy(POL_SEG); mm_init();
    {
        void *p[6]; uint32_t q[6] = { 16, 48, 16, 112, 16, 1 };
        for (int i = 0; i < 6; i++) p[i] = mm_malloc(q[i]);
        mm_free(p[1]); mm_free(p[3]);                  /* free 64, 128 */
        CHECK(ext_frag_for(32) == 0.0 && ext_frag_for(65) > 0.33333 && ext_frag_for(65) < 0.33334);
        CHECK(ext_frag_for(129) == 1.0);
    }
    ropt_t t4mid = { 0, true, false, 1500 };
    replay(&g_tr[3], POL_SEG, t4mid);                  /* T4: 448 구멍 500개 */
    ext_frag_t e = frag_external();
    printf("  T4@1500: E=%.4f  E(464)=%.3f  E(528)=%.3f\n", e.ext, ext_frag_for(464), ext_frag_for(528));
    CHECK(e.ext > 0.997 && ext_frag_for(464) == 0.0 && ext_frag_for(528) == 1.0);

    printf("-- 연습 3/4: internal fragmentation per request (all alloc/realloc requests)\n");
    for (int t = 0; t < NTRACE; t++) {
        waste_t w = waste_of(&g_tr[t]);
        printf("  %-18s n=%5zu  ours %6.1f B/req  pow2 %7.1f B/req  footer-elided %6.1f B/req\n", g_tr[t].name, w.n,
               (double)w.ours / (double)w.n, (double)w.pow2 / (double)w.n, (double)w.elided / (double)w.n);
        CHECK(w.pow2 >= w.ours && w.elided <= w.ours);
    }
    waste_t w1 = waste_of(&g_tr[0]);
    CHECK(w1.ours - w1.elided > 0 && (w1.ours - w1.elided) % 16 == 0);   /* 줄어드는 단위는 늘 16 B */

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
