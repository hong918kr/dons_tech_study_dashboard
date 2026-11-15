/* ARCH-A03 추가 연습 — solution.c 를 그대로 가져와(main 이름만 바꿔 포함) 연습 1~4 의 답을 검증한다.
 *   연습 1  4 KB 를 가정한 코드를 16 KB 페이지에서: stride 별로 실제로 몇 페이지가 폴트되나 (공식 vs mincore)
 *   연습 2  Fisher–Yates 로 섞으면 체이스가 왜 틀리나: 사이클 수 세기, 0 에서 출발한 체이스가 덮는 원소 수
 *   연습 3  이 맥에서 잰 체이스 표(demo_tlb)에서 무릎을 찾고, packed 대조군으로 TLB/캐시 무릎을 가른 뒤
 *           "TLB 커버리지 ≈ P × 16 KB" 로 바꾸기
 *   연습 4  큰 페이지 계산: 1 GiB 를 덮는 데 필요한 항목 수와 페이지 테이블 바이트 (4 KB / 16 KB 그래뉼)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr   (solution.c 와 같은 폴더에서)
 */
#define main solution_main
#include "solution.c"
#undef main

/* ---- 연습 1 ---- */
/* 주소 0, stride, 2·stride, ... (< len) 을 만질 때 서로 다른 페이지 수 */
static size_t pages_touched(size_t len, size_t stride, size_t pg) {
    if (len == 0) return 0;
    size_t n = len / stride + (len % stride != 0);           /* 터치 횟수 */
    if (stride >= pg) return n;                               /* 터치마다 다른 페이지 */
    return ((n - 1) * stride) / pg + 1;                       /* 페이지를 건너뛰지 않는다: 마지막 터치의 페이지까지 전부 */
}

/* ---- 연습 2 ---- */
static void fisher_yates(uint32_t *a, size_t P, uint64_t *seed) {      /* 섞기용 — 사이클 하나가 아니다 */
    for (size_t i = 0; i < P; i++) a[i] = (uint32_t)i;
    for (size_t i = P - 1; i > 0; i--) {
        size_t j = (size_t)(xs64(seed) % (i + 1));                     /* 0..i : 자기 자신도 고를 수 있다 */
        uint32_t t = a[i]; a[i] = a[j]; a[j] = t;
    }
}
static size_t count_cycles(const uint32_t *next, size_t P) {
    static unsigned char seen[1u << 16];
    memset(seen, 0, P);
    size_t c = 0;
    for (size_t s = 0; s < P; s++) {
        if (seen[s]) continue;
        c++;
        for (size_t x = s; !seen[x]; x = next[x]) seen[x] = 1;
    }
    return c;
}
static size_t cycle_len_from(const uint32_t *next, size_t start) {
    size_t k = 1;
    for (size_t x = next[start]; x != start; x = next[x]) k++;
    return k;
}

/* ---- 연습 3: demo_tlb 실측 (세트 1, load 37.85 → 34.61, ns/접근, 7 회 중앙값). md 의 표와 같은 값 ---- */
static const size_t MP[] = {4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048,
                            3072, 4096, 6144, 8192, 12288, 16384, 24576, 32768, 49152, 65536};
static const double MS_SPREAD[] = {0.94, 0.92, 0.77, 0.80, 0.73, 0.80, 0.78, 0.77, 0.98, 0.72, 0.72, 1.14, 2.41,
                                   2.72, 2.23, 2.90, 7.93, 8.53, 11.36, 17.19, 17.24, 18.44, 19.97, 18.89, 29.89,
                                   24.86, 28.98, 52.01, 108.66};
static const double MS_PACKED[] = {0.96, 0.96, 0.93, 0.80, 0.81, 0.76, 0.76, 0.74, 0.74, 0.78, 0.72, 1.06, 0.91,
                                   0.87, 1.03, 0.87, 2.99, 5.71, 6.62, 4.89, 5.66, 10.29, 5.00, 5.89, 8.07,
                                   10.71, 12.07, 14.57, 15.96};
/* spread 의 무릎 k 가 TLB 탓인가 캐시 탓인가: 같은 자리에서 packed(같은 줄 수, 페이지 최소)도
 * ratio 이상 올랐으면 캐시('C'), 아니면 페이지 수 탓('T') */
static char classify_knee(const double *packed, size_t k, double ratio) {
    return packed[k] >= ratio * packed[k - 1] ? 'C' : 'T';
}

/* ---- 연습 4 ---- */
/* bytes 를 unit 크기 항목으로 덮는 데 필요한 항목 수 */
static size_t entries_to_cover(size_t bytes, size_t unit) { return pages_for(bytes, unit); }
/* 마지막 레벨(L3) 테이블만 센 바이트: 항목 8 B, 테이블 한 장 = 그래뉼 크기 (항목 gran/8 개) */
static size_t l3_table_bytes(size_t bytes, size_t gran) {
    size_t ptes = pages_for(bytes, gran);
    size_t per_table = gran / 8;
    return pages_for(ptes, per_table) * gran;
}

int main(void) {
    size_t pg = sys_page_size();

    /* 연습 1 */
    CHECK(pages_touched(1u << 20, 4096, 16384) == 64 && pages_touched(1u << 20, 4096, 4096) == 256);
    CHECK(pages_touched(1u << 20, 16384, 16384) == 64 && pages_touched(1u << 20, 20000, 16384) == 53);
    CHECK(pages_touched(1u << 20, 100, 16384) == 64 && pages_touched(1, 4096, 16384) == 1 && pages_touched(0, 4096, 16384) == 0);
    const size_t strides[] = {100, 4096, 16384, 20000, 65536};
    bool all = true;
    for (size_t k = 0; k < 5; k++) {
        uint8_t *m = map_anon(1u << 20);
        if (!m) { all = false; break; }
        size_t n = (1u << 20) / strides[k] + ((1u << 20) % strides[k] != 0);
        touch_pages(m, n, strides[k], 0, T_WRITE);
        size_t res = resident_pages(m, (1u << 20) / pg, pg);
        printf("info  ex1 stride %5zu: %4zu touches -> resident %zu pages (formula %zu)\n", strides[k], n, res,
               pages_touched(1u << 20, strides[k], pg));
        all = all && res == pages_touched(1u << 20, strides[k], pg);
        munmap(m, 1u << 20);
    }
    CHECK(all);

    /* 연습 2 */
    static uint32_t a[4096];
    uint64_t s1 = 12345, s2 = 12345;
    sattolo(a, 4096, &s1);
    CHECK(count_cycles(a, 4096) == 1 && cycle_len_from(a, 0) == 4096);
    fisher_yates(a, 4096, &s2);
    size_t cyc = count_cycles(a, 4096), len0 = cycle_len_from(a, 0);
    printf("info  ex2 Fisher-Yates P=4096: %zu cycles, chase from 0 covers %zu of 4096 elements\n", cyc, len0);
    CHECK(cyc > 1 && len0 < 4096 && !is_single_cycle(a, 4096));
    { uint32_t b[] = {1, 0, 3, 2}; CHECK(count_cycles(b, 4) == 2 && cycle_len_from(b, 2) == 2); }

    /* 연습 3 */
    size_t n = sizeof MP / sizeof MP[0], k[16];
    CHECK(n == sizeof MS_SPREAD / sizeof MS_SPREAD[0] && n == sizeof MS_PACKED / sizeof MS_PACKED[0]);
    size_t nk = find_knees(MS_SPREAD, n, 2.0, k, 16);
    char cls[16] = {0};
    printf("info  ex3 spread knees (ratio 2):");
    for (size_t j = 0; j < nk; j++) {
        cls[j] = classify_knee(MS_PACKED, k[j], 2.0);
        printf(" P=%zu[%c] (%zu-%zu MiB)", MP[k[j]], cls[j], MP[k[j] - 1] * 16384 >> 20, MP[k[j]] * 16384 >> 20);
    }
    printf("\n");
    CHECK(nk == 4 && MP[k[0]] == 256 && MP[k[1]] == 1024 && MP[k[2]] == 3072 && MP[k[3]] == 49152);
    CHECK(nk == 4 && cls[0] == 'T' && cls[1] == 'C' && cls[2] == 'T' && cls[3] == 'T');
    CHECK(MP[k[0] - 1] * 16384 == (size_t)3 << 20 && MP[k[0]] * 16384 == (size_t)4 << 20);   /* 첫 무릎: 3~4 MiB */
    CHECK(MP[k[0]] * 4096 == (size_t)1 << 20);          /* 같은 항목 수가 4 KB 페이지였다면 1 MiB 만 덮는다 */

    /* 연습 4 */
    size_t G = (size_t)1 << 30;
    CHECK(entries_to_cover(G, 16384) == 65536 && entries_to_cover(G, 4096) == 262144);
    CHECK(entries_to_cover(G, 2u << 20) == 512 && entries_to_cover(G, 32u << 20) == 32);
    CHECK(l3_table_bytes(G, 16384) == 512u << 10 && l3_table_bytes(G, 4096) == 2u << 20);
    CHECK(l3_table_bytes(16384, 16384) == 16384);            /* 한 페이지라도 테이블 한 장 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
