/* MEM-B10 포인터 체이싱으로 메모리 지연 측정
 *
 * Part A: Sattolo 한-사이클 순열 + 사이클 세기, 체인 배치(줄 단위 128/64 B · 페이지 stride)와
 *         체인 만들기/검사, working set 크기 목록 (2 의 거듭제곱 + √2 단계)
 * Part B: 단위 변환 (mach tick → ns, 바이트 → "5.6 KiB"), 중앙값, 지연 곡선의 평탄 구간(plateau) 찾기
 * Part C: 평탄 구간에 캐시 계층 이름 붙이기 + bring-up 점검 (기대한 캐시 경계가 곡선에 보이나, 캐시 꺼짐 의심)
 *
 * 실제 측정 (4 KB … 1 GB 스윕, 순차 체인, 페이지 stride, MLP) 은 demo_chase.c — 이 파일을 #include 해서
 * 여기서 검사한 함수 그대로 잰다. solution.c 자체는 결정적인 부품만 검사한다 (< 2 s, 타이밍 없음).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- 공통: 난수 (고정 시드 → 결정적) ---------------- */
static uint64_t xs64(uint64_t *s) {
    uint64_t x = *s;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return *s = x;
}
/* [0, bound) 균등. 거부(rejection) 방식으로 모듈로 편향을 없앤다. bound >= 1 */
static uint64_t rand_below(uint64_t *s, uint64_t bound) {
    uint64_t lim = UINT64_MAX - UINT64_MAX % bound;   /* bound 의 배수 중 가장 큰 것 */
    uint64_t r;
    do { r = xs64(s); } while (r >= lim);
    return r % bound;
}

/* ---------------- Part A: 순열 · 체인 ---------------- */

/* 대조군 (주어짐): Fisher–Yates — 균등한 "아무" 순열. 사이클이 여러 개 생긴다. */
static void fisher_yates(uint32_t *next, size_t n, uint64_t *seed) {
    for (size_t i = 0; i < n; i++) next[i] = (uint32_t)i;
    for (size_t i = n; i-- > 1;) {
        size_t j = (size_t)rand_below(seed, i + 1);       /* j ∈ [0, i] */
        uint32_t t = next[i]; next[i] = next[j]; next[j] = t;
    }
}

/* TODO A1: Sattolo — i → next[i] 가 길이 n 인 사이클 "하나" 가 되는 균등 무작위 순열.
 * Fisher–Yates 와 딱 한 글자 차이: j ∈ [0, i) (자기 자신과는 안 바꾼다). n == 0 이면 아무것도 안 함. */
static void sattolo(uint32_t *next, size_t n, uint64_t *seed) {
    for (size_t i = 0; i < n; i++) next[i] = (uint32_t)i;
    (void)seed; /* TODO: i = n-1 … 1 로 내려가며 j = rand_below(seed, i) 와 맞바꾸기 (지금은 항등 = 사이클 n 개) */
}

/* TODO A2: i → next[i] 의 사이클 수. 순열이 아니면 (값 ≥ n, 또는 두 원소가 같은 곳을 가리킴) 0.
 * seen 은 n 바이트 작업 공간. longest 에 가장 긴 사이클 길이 (n == 0 이거나 순열이 아니면 0). */
static size_t count_cycles(const uint32_t *next, size_t n, uint8_t *seen, size_t *longest) {
    (void)next; (void)n; (void)seen;
    *longest = 0;
    return 0; /* TODO: 순열 검사 → seen 초기화 → 안 본 시작점마다 사이클 하나 따라가며 길이 세기 */
}

/* 순차 체인 (주어짐): 0 → 1 → … → n-1 → 0. 프리페처가 맞히기 가장 쉬운 순서. */
static void seq_cycle(uint32_t *next, size_t n) {
    for (size_t i = 0; i < n; i++) next[i] = (uint32_t)((i + 1) % n);
}

/* 배치: LAY_LINE = 원소 i 가 줄(node 바이트) i 에 빽빽이.
 *       LAY_PAGE = 원소 i 가 페이지 i 안의 (i mod (page/node)) 번째 칸 — 페이지마다 원소 하나, 칸을 돌려
 *                  L1 세트가 한 곳에 몰리지 않게 한다 (ARCH-A03 의 spread 와 같은 배치). */
typedef enum { LAY_LINE, LAY_PAGE } layout_t;

/* 주소 범위 (주어짐): LINE = n × node, PAGE = n × page. */
static size_t layout_span(layout_t lay, size_t n, size_t node, size_t page) {
    return lay == LAY_LINE ? n * node : n * page;
}

/* TODO A3: 원소 i 의 바이트 오프셋 (위 배치 정의). */
static size_t node_offset(layout_t lay, size_t i, size_t node, size_t page) {
    (void)lay; (void)i; (void)node; (void)page;
    return 0; /* TODO: LINE = i*node, PAGE = i*page + (i mod (page/node))*node */
}

/* TODO A4: buf 에 체인을 깐다 — 원소 i 자리에 "원소 next[i] 의 주소" 를 쓴다. 반환: 원소 0 의 주소.
 * n == 0, node < sizeof(void *) 이거나 node 가 포인터 크기의 배수가 아님, page % node != 0 (PAGE),
 * layout_span > cap 이면 NULL (buf 는 건드리지 않음). buf 는 malloc/mmap 메모리 (포인터 정렬). */
static void *chain_build(uint8_t *buf, size_t cap, const uint32_t *next, size_t n,
                         layout_t lay, size_t node, size_t page) {
    (void)buf; (void)cap; (void)next; (void)n; (void)lay; (void)node; (void)page;
    return NULL; /* TODO: 인자 검사 → 원소 i 자리에 원소 next[i] 의 주소 쓰기 → 원소 0 의 주소 */
}

/* 체인 걷기 (주어짐) — 측정하는 바로 그 루프. 다음 주소가 이번 load 의 결과 → load 들이 겹치지 못한다. */
static void *chain_walk(void *p, size_t steps) {
    while (steps--) p = *(void **)p;
    return p;
}

/* TODO A5: 체인 검사 — start 에서 n 걸음: 매 걸음의 주소를 원소 번호로 되돌려 (node_offset 의 역)
 * 범위 안 · 정확한 칸 · 처음 보는 원소인지 확인하고, n 걸음 뒤 start 로 돌아오면 true.
 * seen 은 n 바이트 작업 공간. */
static bool chain_verify(void *start, uint8_t *buf, size_t n, layout_t lay, size_t node, size_t page,
                         uint8_t *seen) {
    (void)start; (void)buf; (void)n; (void)lay; (void)node; (void)page; (void)seen;
    return false; /* TODO: n 걸음 — 주소 → 원소 번호, 범위/칸/중복 검사, 끝에 start 로 돌아왔나 */
}

/* isqrt (주어짐): floor(√v) */
static uint64_t isqrt64(uint64_t v) {
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* TODO A6: working set 크기 목록 lo..hi (둘 다 포함 가능): 2^k 와 floor(√2 · 2^k) = isqrt(2^(2k+1)) 를
 * node 배수로 내린 값을 번갈아. lo 는 2 의 거듭제곱 (≥ node), hi ≤ 2^31, 아니면 0. cap 개까지만 쓴다.
 * 반환: 쓴 개수. 예 (node 128): 4096, 5760, 8192, 11520, 16384, … */
static size_t ws_sizes(size_t lo, size_t hi, size_t node, size_t *out, size_t cap) {
    (void)lo; (void)hi; (void)node; (void)out; (void)cap; (void)isqrt64;
    return 0; /* TODO: 인자 검사 → p = lo, 2lo, … 마다 p 와 isqrt(2^(2k+1)) (node 배수로 내림) */
}

/* ---------------- Part B: 단위 · 통계 · 평탄 구간 ---------------- */

/* TODO B1: mach_absolute_time tick → ns. ns = ticks × numer / denom 을 넘침 없이:
 * q = ticks / denom, r = ticks % denom → q × numer + r × numer / denom (결과가 64 비트에 들어가는 범위). */
static uint64_t ticks_to_ns(uint64_t ticks, uint32_t numer, uint32_t denom) {
    (void)ticks; (void)numer; (void)denom;
    return 0; /* TODO: 몫/나머지로 나눠 곱하기 (ticks * numer 를 먼저 하면 넘친다) */
}

/* 주어짐: ns/load, 의존 체인 하나의 "대역폭" (바이트/ns = GB/s, 1 GB = 1e9 B) */
static double ns_per_load(uint64_t ticks, uint32_t numer, uint32_t denom, uint64_t loads) {
    return loads ? (double)ticks_to_ns(ticks, numer, denom) / (double)loads : 0.0;
}
static double chain_gbps(size_t bytes_per_load, double ns) { return ns > 0 ? (double)bytes_per_load / ns : 0.0; }

/* TODO B2: 바이트 → 사람 읽는 문자열. 1024 미만 "640 B", 아니면 가장 큰 단위 KiB/MiB/GiB:
 * 나누어떨어지면 정수 "4 KiB", 아니면 소수 한 자리 "5.6 KiB". 반환: 길이, 안 들어가면 -1 (cap > 0 이면 ""). */
static int fmt_bytes(size_t b, char *buf, size_t cap) {
    (void)b;
    if (cap) buf[0] = '\0';
    return -1; /* TODO: 단위 고르기 → 정수/소수 한 자리 snprintf → 잘렸으면 -1 */
}

/* TODO B3: 중앙값. v 는 정렬해도 된다 (삽입 정렬로 충분). n 짝수면 가운데 둘의 평균, n == 0 → 0. */
static double median(double *v, size_t n) {
    (void)v; (void)n;
    return 0.0; /* TODO: 삽입 정렬 → 가운데 (짝수면 두 개 평균) */
}

#define MAXPTS 128
typedef struct { size_t first, last; double level; } plateau_t;

/* 주어짐: y[a..b] 의 중앙값 (복사해서) */
static double range_median(const double *y, size_t a, size_t b) {
    double tmp[MAXPTS];
    size_t m = b - a + 1;
    memcpy(tmp, y + a, m * sizeof tmp[0]);
    return median(tmp, m);
}

/* TODO B4: 지연 곡선 y[0..n) (working set 이 커지는 순) 의 평탄 구간 찾기.
 *  1) 나누기: 구간 [s, i-1] 의 중앙값 ref 에 대해 y[i] 가 [ref/ratio, ref*ratio] 밖이면 구간을 닫고 s = i.
 *  2) 길이 < min_len 인 구간은 "전이(transition)" 로 버린다 (경사 · 튀는 점).
 *  3) 남은 이웃 평탄 구간의 레벨 비 (큰 것/작은 것) 가 ratio 이하면 합친다 (튀는 점 하나로 갈라진 것 복구).
 *     합친 구간의 레벨 = y[first..last] 전체의 중앙값.
 * n > MAXPTS, ratio <= 1, min_len == 0 이면 0. 반환: out 에 쓴 개수 (≤ maxout). */
static size_t find_plateaus(const double *y, size_t n, double ratio, size_t min_len,
                            plateau_t *out, size_t maxout) {
    (void)y; (void)n; (void)ratio; (void)min_len; (void)out; (void)maxout; (void)range_median;
    return 0; /* TODO: 1) 중앙값 대비 ratio 로 나누기 2) 짧은 구간 버리기 3) 같은 레벨 이웃 합치기 */
}

/* ---------------- Part C: 계층 이름 · bring-up 점검 ---------------- */
typedef struct { const char *name; size_t bytes; } level_cfg;   /* 기대 캐시 크기 (sysctl/데이터시트) */

/* TODO C1: working set ws 가 들어가는 가장 작은 계층 이름: ws ≤ cfg[j].bytes 인 첫 j (cfg 는 크기순),
 * 없으면 "DRAM". */
static const char *name_level(size_t ws, const level_cfg *cfg, size_t ncfg) {
    (void)ws; (void)cfg; (void)ncfg;
    return "DRAM"; /* TODO: ws <= cfg[j].bytes 인 첫 j 의 이름 */
}

typedef struct { unsigned seen_mask; bool cache_off_suspect; size_t nlevels; } hier_report;

/* TODO C2: bring-up 점검. find_plateaus(y, n, 1.4, 2) 로 평탄 구간을 찾고,
 *  - nlevels = 평탄 구간 수
 *  - 이웃 구간 k, k+1 의 경계 구간 [x[p_k.last], x[p_{k+1}.first]] 이 [cfg[j].bytes/2, cfg[j].bytes*2] 와
 *    겹치면 seen_mask 의 비트 j 를 켠다 ("이 캐시의 경계가 곡선에 보였다")
 *  - 첫 구간 레벨 > l1_max_ns 이면 cache_off_suspect (가장 작은 working set 도 느림 = 캐시 꺼짐/미설정 의심) */
static hier_report hierarchy_check(const size_t *x, const double *y, size_t n,
                                   const level_cfg *cfg, size_t ncfg, double l1_max_ns) {
    (void)x; (void)y; (void)n; (void)cfg; (void)ncfg; (void)l1_max_ns;
    hier_report r = { 0, false, 0 };
    return r; /* TODO: find_plateaus → 레벨 수, 첫 레벨 > l1_max_ns, 이웃 경계 구간이 [C/2, 2C] 와 겹치나 */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 합성 곡선: 이상적인 3 단 계층 (C1, C2 바이트, 레벨 t0 < t1 < t2 ns).
 * mix = false: 계단. mix = true: 무작위 교체 근사 — working set W 중 C1/W 는 L1, (C2-C1)/W 는 L2, 나머지 DRAM. */
static void synth(const size_t *x, double *y, size_t n, size_t C1, size_t C2,
                  double t0, double t1, double t2, bool mix) {
    for (size_t i = 0; i < n; i++) {
        double W = (double)x[i];
        if (!mix) { y[i] = x[i] <= C1 ? t0 : x[i] <= C2 ? t1 : t2; continue; }
        double a = (W < C1 ? W : (double)C1) / W;
        double b = ((W < C2 ? W : (double)C2) - (W < C1 ? W : (double)C1)) / W;
        y[i] = a * t0 + b * t1 + (1.0 - a - b) * t2;
    }
}

int main(void) {
    static uint32_t nx[1u << 20];
    static uint8_t seen[1u << 20];
    size_t longest = 0;

    /* ---- Part A1/A2: Sattolo = 한 사이클 ---- */
    {
        uint32_t id[5] = { 0, 1, 2, 3, 4 }, sw[4] = { 1, 0, 3, 2 }, bad1[2] = { 0, 0 }, bad2[1] = { 5 };
        CHECK(count_cycles(id, 5, seen, &longest) == 5 && longest == 1);
        CHECK(count_cycles(sw, 4, seen, &longest) == 2 && longest == 2);
        CHECK(count_cycles(bad1, 2, seen, &longest) == 0 && longest == 0);    /* 중복 */
        CHECK(count_cycles(bad2, 1, seen, &longest) == 0);                    /* 범위 밖 */
        CHECK(count_cycles(id, 0, seen, &longest) == 0 && longest == 0);
    }
    {
        int bad = 0;
        for (size_t n = 1; n <= 300; n++)
            for (uint64_t sd = 1; sd <= 6; sd++) {
                uint64_t seed = sd * 0x9E3779B97F4A7C15ull;
                sattolo(nx, n, &seed);
                if (count_cycles(nx, n, seen, &longest) != 1 || longest != n) bad++;
            }
        printf("sattolo n=1..300 x 6 seeds: %d not single-cycle\n", bad);
        CHECK(bad == 0);
        uint64_t seed = 42;
        sattolo(nx, 1u << 20, &seed);                                 /* 큰 n 한 번 (1 M) */
        CHECK(count_cycles(nx, 1u << 20, seen, &longest) == 1 && longest == (1u << 20));
        uint32_t a[100], b[100];
        uint64_t s1 = 7, s2 = 7, s3 = 8;
        sattolo(a, 100, &s1); sattolo(b, 100, &s2);
        CHECK(memcmp(a, b, sizeof a) == 0);                           /* 같은 시드 → 같은 체인 (재현성) */
        sattolo(b, 100, &s3);
        CHECK(memcmp(a, b, sizeof a) != 0);
        int fixed = 0;
        for (size_t i = 0; i < 100; i++) fixed += a[i] == i;
        CHECK(fixed == 0);                                            /* 한 사이클 (n ≥ 2) → 고정점 없음 */
    }
    {   /* 대조군: Fisher–Yates 는 거의 항상 사이클 여러 개 (한 사이클일 확률 1/n) */
        int single = 0; double cyc = 0;
        for (uint64_t sd = 1; sd <= 200; sd++) {
            uint64_t seed = sd * 0x2545F4914F6CDD1Dull;
            fisher_yates(nx, 1000, &seed);
            size_t c = count_cycles(nx, 1000, seen, &longest);
            single += c == 1; cyc += (double)c;
        }
        printf("fisher-yates n=1000 x 200: single-cycle %d, mean cycles %.2f (H_1000 = 7.49)\n", single, cyc / 200);
        CHECK(single <= 3);
        CHECK(cyc / 200 > 6.5 && cyc / 200 < 8.5);
    }

    /* ---- Part A3/A4/A5: 배치 · 체인 ---- */
    CHECK(node_offset(LAY_LINE, 3, 128, 16384) == 384);
    CHECK(node_offset(LAY_LINE, 3, 64, 16384) == 192);
    CHECK(node_offset(LAY_PAGE, 3, 128, 16384) == 3 * 16384 + 3 * 128);
    CHECK(node_offset(LAY_PAGE, 130, 128, 16384) == 130 * 16384 + 2 * 128);   /* 칸이 128 마다 돌아온다 */
    CHECK(node_offset(LAY_PAGE, 300, 64, 16384) == 300 * 16384 + 44 * 64);     /* 16384/64 = 256 칸 */
    CHECK(layout_span(LAY_PAGE, 10, 128, 16384) == 163840);
    {
        size_t cap = layout_span(LAY_PAGE, 300, 128, 16384);
        uint8_t *buf = malloc(cap);
        CHECK(buf != NULL);
        if (buf) {
            static const layout_t lays[2] = { LAY_LINE, LAY_PAGE };
            static const size_t nodes[2] = { 64, 128 };
            static const size_t ns[5] = { 1, 2, 3, 100, 300 };
            int okc = 0, tot = 0;
            for (int L = 0; L < 2; L++)
                for (int d = 0; d < 2; d++)
                    for (int k = 0; k < 5; k++)
                        for (int mode = 0; mode < 2; mode++) {          /* 0 = Sattolo, 1 = 순차 */
                            size_t n = ns[k];
                            uint64_t seed = 99 + (uint64_t)k;
                            if (mode == 0) sattolo(nx, n, &seed); else seq_cycle(nx, n);
                            void *s = chain_build(buf, cap, nx, n, lays[L], nodes[d], 16384);
                            tot++;
                            if (s && chain_verify(s, buf, n, lays[L], nodes[d], 16384, seen)
                                  && chain_walk(s, n) == s) okc++;
                        }
            printf("chain build+verify: %d/%d layouts x nodes x n x order OK\n", okc, tot);
            CHECK(okc == tot);

            seq_cycle(nx, 4);
            void *s = chain_build(buf, cap, nx, 4, LAY_LINE, 128, 16384);
            CHECK(s == buf && chain_walk(s, 1) == buf + 128 && chain_walk(s, 3) == buf + 384);
            s = chain_build(buf, cap, nx, 4, LAY_PAGE, 128, 16384);
            CHECK(s == buf && chain_walk(s, 1) == buf + 16384 + 128);
            CHECK(chain_build(buf, 511, nx, 4, LAY_LINE, 128, 16384) == NULL);      /* 용량 512 필요 */
            CHECK(chain_build(buf, cap, nx, 4, LAY_LINE, 4, 16384) == NULL);        /* 포인터보다 작은 원소 */
            CHECK(chain_build(buf, cap, nx, 4, LAY_LINE, 12, 16384) == NULL);       /* 정렬 안 맞음 */
            CHECK(chain_build(buf, cap, nx, 0, LAY_LINE, 128, 16384) == NULL);
            CHECK(chain_build(buf, cap, nx, 4, LAY_PAGE, 96, 16384) == NULL);       /* 16384 % 96 != 0 */

            for (size_t i = 0; i < 4; i++) nx[i] = (uint32_t)i;                     /* 항등 = 사이클 4 개 */
            s = chain_build(buf, cap, nx, 4, LAY_LINE, 128, 16384);
            CHECK(s != NULL && !chain_verify(s, buf, 4, LAY_LINE, 128, 16384, seen));
            uint64_t seed = 5;
            sattolo(nx, 50, &seed);
            s = chain_build(buf, cap, nx, 50, LAY_LINE, 128, 16384);
            CHECK(s != NULL && chain_verify(s, buf, 50, LAY_LINE, 128, 16384, seen));
            if (s) {
                *(void **)s = s;                                                    /* 고장: 자기 자신 */
                CHECK(!chain_verify(s, buf, 50, LAY_LINE, 128, 16384, seen));
                *(void **)s = buf + 64;                                             /* 고장: 칸 경계 아님 */
                CHECK(!chain_verify(s, buf, 50, LAY_LINE, 128, 16384, seen));
            }
            free(buf);
        }
    }

    /* ---- Part A6: 크기 목록 ---- */
    size_t xs[64];
    {
        size_t n = ws_sizes(4096, (size_t)1 << 30, 128, xs, 64);
        CHECK(n == 37);
        CHECK(xs[0] == 4096 && xs[1] == 5760 && xs[2] == 8192 && xs[3] == 11520 && xs[36] == ((size_t)1 << 30));
        int ok = 1;
        for (size_t i = 1; i < n; i++) {
            double r = (double)xs[i] / (double)xs[i - 1];
            if (xs[i] % 128 || r < 1.38 || r > 1.45) ok = 0;
        }
        CHECK(ok);
        size_t y64[8];
        CHECK(ws_sizes(4096, 16384, 64, y64, 8) == 5 && y64[1] == 5760 && y64[3] == 11584);
        CHECK(ws_sizes(4096, (size_t)1 << 30, 128, y64, 5) == 5 && y64[4] == 16384);   /* cap */
        CHECK(ws_sizes(5000, 1 << 20, 128, y64, 8) == 0);                             /* lo 가 2^k 아님 */
        CHECK(ws_sizes(8192, 4096, 128, y64, 8) == 0);
        CHECK(ws_sizes(4096, 4096, 128, y64, 8) == 1 && y64[0] == 4096);
    }

    /* ---- Part B1/B2/B3: 단위 · 중앙값 ---- */
    CHECK(ticks_to_ns(3, 125, 3) == 125);                    /* Apple Silicon timebase 125/3 → 41.67 ns/tick */
    CHECK(ticks_to_ns(1, 125, 3) == 41);
    CHECK(ticks_to_ns(24000000, 125, 3) == 1000000000ull);    /* 24 MHz × 1 s */
    CHECK(ticks_to_ns(12345, 1, 1) == 12345);
    {
        uint64_t t = (uint64_t)1 << 50;
        unsigned __int128 exact = (unsigned __int128)t * 125u / 3u;
        CHECK(ticks_to_ns(t, 125, 3) == (uint64_t)exact);    /* t × 125 는 64 비트를 넘지만 결과는 정확 */
    }
    CHECK(ns_per_load(48000, 125, 3, 2000000) == 1.0);        /* 48000 tick = 2 ms, 2 M 걸음 → 1 ns */
    CHECK(chain_gbps(128, 100.0) == 1.28);
    {
        char b[16];
        CHECK(fmt_bytes(640, b, sizeof b) == 5 && strcmp(b, "640 B") == 0);
        CHECK(fmt_bytes(4096, b, sizeof b) > 0 && strcmp(b, "4 KiB") == 0);
        CHECK(fmt_bytes(5760, b, sizeof b) > 0 && strcmp(b, "5.6 KiB") == 0);
        CHECK(fmt_bytes(131072, b, sizeof b) > 0 && strcmp(b, "128 KiB") == 0);
        CHECK(fmt_bytes(20971520, b, sizeof b) > 0 && strcmp(b, "20 MiB") == 0);
        CHECK(fmt_bytes((size_t)1 << 30, b, sizeof b) > 0 && strcmp(b, "1 GiB") == 0);
        CHECK(fmt_bytes((size_t)3 << 29, b, sizeof b) > 0 && strcmp(b, "1.5 GiB") == 0);
        CHECK(fmt_bytes(5760, b, 4) == -1 && b[0] == '\0');
    }
    {
        double a[3] = { 3, 1, 2 }, b[4] = { 4, 1, 3, 2 }, c[1] = { 5 }, d[6] = { 9, 9, 1, 9, 2, 9 };
        CHECK(median(a, 3) == 2.0);
        CHECK(median(b, 4) == 2.5);
        CHECK(median(c, 1) == 5.0);
        CHECK(median(d, 6) == 9.0);
        CHECK(median(a, 0) == 0.0);
    }

    /* ---- Part B4: 평탄 구간 (합성 곡선) ---- */
    size_t nx37 = ws_sizes(4096, (size_t)1 << 30, 128, xs, 64);
    double y[64];
    plateau_t p[16];
    const size_t C1 = 131072, C2 = (size_t)16 << 20;          /* 인덱스 10 = 128 KiB, 24 = 16 MiB */
    {
        synth(xs, y, nx37, C1, C2, 0.9, 5.0, 110.0, false);    /* 깨끗한 계단 */
        size_t np = find_plateaus(y, nx37, 1.4, 2, p, 16);
        CHECK(np == 3);
        CHECK(p[0].first == 0 && p[0].last == 10 && p[0].level == 0.9);
        CHECK(p[1].first == 11 && p[1].last == 24 && p[1].level == 5.0);
        CHECK(p[2].first == 25 && p[2].last == 36 && p[2].level == 110.0);
        CHECK(find_plateaus(y, nx37, 1.4, 2, p, 1) == 1 && p[0].last == 10);   /* maxout */

        uint64_t seed = 2026;                                   /* ±8 % 잡음 */
        double yn[64];
        for (size_t i = 0; i < nx37; i++) yn[i] = y[i] * (0.92 + 0.16 * (double)(xs64(&seed) % 1001) / 1000.0);
        np = find_plateaus(yn, nx37, 1.4, 2, p, 16);
        CHECK(np == 3 && p[0].last == 10 && p[1].first == 11 && p[1].last == 24 && p[2].first == 25);

        y[5] = 4.0;                                             /* 튀는 점 하나 (다른 프로세스가 끼어든 run) */
        np = find_plateaus(y, nx37, 1.4, 2, p, 16);
        CHECK(np == 3 && p[0].first == 0 && p[0].last == 10 && p[0].level == 0.9);
    }
    {
        synth(xs, y, nx37, C1, C2, 0.9, 5.0, 110.0, true);     /* 점진적 전이 (혼합 모델) */
        size_t np = find_plateaus(y, nx37, 1.4, 2, p, 16);
        printf("mix-model curve: %zu plateaus:", np);
        for (size_t k = 0; k < np; k++) printf(" [%zu..%zu]=%.2f", p[k].first, p[k].last, p[k].level);
        printf("\n");
        CHECK(np == 3);
        CHECK(p[0].first == 0 && p[0].last == 10 && p[0].level == 0.9);
        CHECK(p[1].level > 4.0 && p[1].level < 5.0 && p[1].last <= 24);
        CHECK(p[2].last == 36 && p[2].level > 90.0 && p[2].level <= 110.0);
    }
    {
        double g[10], f[8] = { 2, 2, 2, 2, 2, 2, 2, 2 };
        for (int i = 0; i < 10; i++) g[i] = 1.0;
        for (int i = 1; i < 10; i++) g[i] = g[i - 1] * 1.5;     /* 평탄 없이 계속 오름 */
        CHECK(find_plateaus(g, 10, 1.4, 2, p, 16) == 0);
        CHECK(find_plateaus(f, 8, 1.4, 2, p, 16) == 1 && p[0].first == 0 && p[0].last == 7);
        CHECK(find_plateaus(f, 0, 1.4, 2, p, 16) == 0);
        CHECK(find_plateaus(f, 8, 1.0, 2, p, 16) == 0);
        CHECK(find_plateaus(f, MAXPTS + 1, 1.4, 2, p, 16) == 0);
    }

    /* ---- Part C: 이름 · bring-up 점검 (M6 Super 코어 sysctl 값) ---- */
    const level_cfg m6[2] = { { "L1D", 131072 }, { "L2", 20971520 } };
    CHECK(strcmp(name_level(4096, m6, 2), "L1D") == 0);
    CHECK(strcmp(name_level(131072, m6, 2), "L1D") == 0);
    CHECK(strcmp(name_level(131073, m6, 2), "L2") == 0);
    CHECK(strcmp(name_level(20971520, m6, 2), "L2") == 0);
    CHECK(strcmp(name_level((size_t)1 << 30, m6, 2), "DRAM") == 0);
    CHECK(strcmp(name_level(4096, m6, 0), "DRAM") == 0);
    {
        synth(xs, y, nx37, C1, C2, 0.9, 5.0, 110.0, true);     /* 정상 보드 */
        hier_report r = hierarchy_check(xs, y, nx37, m6, 2, 3.0);
        CHECK(r.seen_mask == 3u && !r.cache_off_suspect && r.nlevels == 3);
        for (size_t i = 0; i < nx37; i++) y[i] = 110.0;         /* 캐시 꺼짐 (MMU/캐시 미설정) */
        r = hierarchy_check(xs, y, nx37, m6, 2, 3.0);
        CHECK(r.seen_mask == 0u && r.cache_off_suspect && r.nlevels == 1);
        synth(xs, y, nx37, C1, C1, 0.9, 0.9, 110.0, true);     /* L2 꺼짐: L1 다음 바로 DRAM */
        r = hierarchy_check(xs, y, nx37, m6, 2, 3.0);
        CHECK(r.seen_mask == 1u && !r.cache_off_suspect && r.nlevels == 2);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
