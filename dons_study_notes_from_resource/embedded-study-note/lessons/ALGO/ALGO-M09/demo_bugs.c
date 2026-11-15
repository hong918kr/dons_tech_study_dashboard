/* ALGO-M09 demo: 흔한 버그 변형 6개를 정답 함수(solution.c)와 대조
 * 입력: 격자 0..5 위 모든 n <= 3 순서쌍(3616개) + 무작위 n <= 12 (좌표 < 52) 4000개.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bugs.c -o demo_bugs && ./demo_bugs
 */
#define ALGO_M09_NO_MAIN
#include "solution.c"

static uint16_t W[MAX_IV], P[MAX_IV];
static ival_t O1[MAX_IV], O2[MAX_IV];
static uint32_t SB[MAX_IV], EB[MAX_IV];
static uint8_t CH[MAX_IV];

/* B1: 시작이 이른 순서로 고르기 (마지막 끝과만 비교) */
static size_t bug_select_by_start(const ival_t *iv, size_t n) {
    sort_idx(iv, n, W, BY_START);
    size_t k = 0; uint32_t last = 0;
    for (size_t t = 0; t < n; t++) if (iv[W[t]].s >= last) { k++; last = iv[W[t]].e; }
    return k;
}
/* B2: 맞닿음을 겹침으로 (s > last_end) */
static size_t bug_select_strict(const ival_t *iv, size_t n) {
    sort_idx(iv, n, W, BY_END);
    size_t k = 0; uint32_t last = 0; int have = 0;
    for (size_t t = 0; t < n; t++) if (!have || iv[W[t]].s > last) { k++; last = iv[W[t]].e; have = 1; }
    return k;
}
/* B3: 병합에서 max 없이 끝을 덮어쓰기 */
static size_t bug_merge_overwrite(const ival_t *iv, size_t n, ival_t *out) {
    if (n == 0) return 0;
    sort_idx(iv, n, W, BY_START);
    size_t m = 0; out[0] = iv[W[0]];
    for (size_t t = 1; t < n; t++) { ival_t c = iv[W[t]]; if (c.s <= out[m].e) out[m].e = c.e; else out[++m] = c; }
    return m + 1;
}
/* B4: 스위프에서 같은 시각이면 시작 먼저 (<=) */
static unsigned bug_depth_start_first(const ival_t *iv, size_t n) {
    for (size_t i = 0; i < n; i++) { SB[i] = iv[i].s; EB[i] = iv[i].e; }
    sort_u32(SB, n); sort_u32(EB, n);
    unsigned d = 0, best = 0; size_t i = 0, j = 0;
    while (i < n) { if (SB[i] <= EB[j]) { if (++d > best) best = d; i++; } else { d--; j++; } }
    return best;
}
/* B5: 힙 재사용 조건을 엄격하게 (end < s) */
static int bug_dma_strict(const ival_t *x, size_t n) {
    slot_t heap[MAX_CH]; unsigned hn = 0, used = 0;
    sort_idx(x, n, W, BY_START);
    for (size_t t = 0; t < n; t++) {
        uint16_t i = W[t];
        if (hn > 0 && heap[0].end < x[i].s) CH[i] = slot_pop(heap, &hn).ch;
        else if (used < MAX_CH) CH[i] = (uint8_t)used++;
        else return -1;
        slot_push(heap, &hn, (slot_t){ x[i].e, CH[i] });
    }
    return (int)used;
}
/* B6: "각 구간과 겹치는 구간 수 + 1" 의 최댓값을 깊이로 */
static unsigned bug_depth_pairwise(const ival_t *iv, size_t n) {
    unsigned best = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned c = 1;
        for (size_t j = 0; j < n; j++) if (j != i && iv[i].s < iv[j].e && iv[j].s < iv[i].e) c++;
        if (c > best) best = c;
    }
    return best;
}

enum { B1, B2, B3, B4, B5, B6, NB };
static const char *const bname[NB] = {
    "B1 select: sort by start", "B2 select: s > last_end (strict)", "B3 merge: e = c.e (no max)",
    "B4 sweep: start before end on tie", "B5 dma: reuse only if end < s", "B6 depth = max overlaps-per-interval + 1" };
static unsigned long wrong[NB], cases;
static ival_t first_bad[NB][16]; static size_t first_n[NB]; static int have_first[NB];

static void run(const ival_t *iv, size_t n) {
    uint32_t at;
    size_t sel = max_nonoverlap(iv, n, W, P);
    size_t m = merge_intervals(iv, n, W, O1);
    unsigned d = max_depth(iv, n, SB, EB, &at);
    int ok[NB];
    ok[B1] = bug_select_by_start(iv, n) == sel;
    ok[B2] = bug_select_strict(iv, n) == sel;
    { size_t mb = bug_merge_overwrite(iv, n, O2); ok[B3] = mb == m && memcmp(O1, O2, m * sizeof(ival_t)) == 0; }
    ok[B4] = bug_depth_start_first(iv, n) == d;
    ok[B5] = bug_dma_strict(iv, n) == dma_assign(iv, n, MAX_CH, W, CH);
    ok[B6] = bug_depth_pairwise(iv, n) == d;
    for (int b = 0; b < NB; b++)
        if (!ok[b]) {
            wrong[b]++;
            if (!have_first[b]) { memcpy(first_bad[b], iv, n * sizeof(ival_t)); first_n[b] = n; have_first[b] = 1; }
        }
    cases++;
}

int main(void) {
    ival_t cand[15], iv[16];
    int nc = 0;
    for (uint32_t s = 0; s < 5; s++) for (uint32_t e = s + 1; e <= 5; e++) cand[nc++] = (ival_t){ s, e };
    for (size_t n = 0; n <= 3; n++) {
        size_t total = 1;
        for (size_t r = 0; r < n; r++) total *= (size_t)nc;
        for (size_t code = 0; code < total; code++) {
            size_t c = code;
            for (size_t r = 0; r < n; r++) { iv[r] = cand[c % (size_t)nc]; c /= (size_t)nc; }
            run(iv, n);
        }
    }
    unsigned long grid_wrong[NB];
    memcpy(grid_wrong, wrong, sizeof wrong);
    unsigned long grid_cases = cases;
    uint32_t r = 0xC0FFEEu;
    for (int it = 0; it < 4000; it++) {
        size_t n = 0;
        r ^= r << 13; r ^= r >> 17; r ^= r << 5; n = r % 13;
        for (size_t i = 0; i < n; i++) {
            r ^= r << 13; r ^= r >> 17; r ^= r << 5; uint32_t s = r % 40;
            r ^= r << 13; r ^= r >> 17; r ^= r << 5; uint32_t len = 1 + r % 12;
            iv[i] = (ival_t){ s, s + len };
        }
        run(iv, n);
    }
    printf("%-42s | grid n<=3: wrong / %lu | random n<=12: wrong / %lu | first failing input\n", "variant", grid_cases, cases - grid_cases);
    for (int b = 0; b < NB; b++) {
        printf("%-42s | %8lu            | %8lu              | ", bname[b], grid_wrong[b], wrong[b] - grid_wrong[b]);
        for (size_t i = 0; i < first_n[b]; i++) printf("[%u,%u)", first_bad[b][i].s, first_bad[b][i].e);
        printf("\n");
    }
    return 0;
}
