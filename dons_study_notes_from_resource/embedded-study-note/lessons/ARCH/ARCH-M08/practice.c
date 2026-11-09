/* ARCH-M08 추가 연습 — 한 번에 검증
 *
 *   연습 1  배열 원소가 라인 경계를 넘는 수 (24 B 원소 vs 32 B 원소)
 *   연습 2  리눅스식 ____cacheline_aligned_in_smp 와 "크기만 패딩" union 의 함정
 *   연습 3  핫/콜드 분리: 훑기 루프가 건드리는 라인 수 (뚱뚱한 구조체 vs 핫 배열)
 *   연습 4  Cortex-M7 (32 B D-cache 라인): DMA 수신 버퍼와 ISR 변수가 한 라인에 — 그리고 고친 배치
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _DARWIN_C_SOURCE
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

/* 본문 Part A/C 와 같은 도구 */
static size_t granules_touched(uintptr_t addr, size_t size, size_t g) {
    if (size == 0 || g == 0) return 0;
    return (size_t)((addr + size - 1) / g - addr / g + 1);
}
static bool share_granule(uintptr_t a, size_t sa, uintptr_t b, size_t sb, size_t g) {
    if (sa == 0 || sb == 0 || g == 0) return false;
    return a / g <= (b + sb - 1) / g && b / g <= (a + sa - 1) / g;
}
enum { G_COLD = -1, G_RO = 0 };
typedef struct { const char *name; size_t off, size; int group; } field_t;
static int layout_violations(const field_t *f, int n, size_t g) {
    int bad = 0;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            int a = f[i].group, b = f[j].group;
            if (a == G_COLD || b == G_COLD || (a == G_RO && b == G_RO) || a == b) continue;
            if (share_granule(f[i].off, f[i].size, f[j].off, f[j].size, g)) bad++;
        }
    return bad;
}
#define FLD(T, m, grp) { #m, offsetof(T, m), sizeof(((T *)0)->m), grp }

/* ---------- 연습 1: 경계를 넘는 원소 ---------- */
/* g 정렬된 주소에서 시작하는 배열(원소 elem B, n 개) 중 두 블록 이상에 걸치는 원소 수 */
static int count_straddlers(size_t elem, int n, size_t g) {
    int c = 0;
    for (int i = 0; i < n; i++) if (granules_touched((uintptr_t)i * elem, elem, g) >= 2) c++;
    return c;
}

/* ---------- 연습 2: 리눅스식 매크로와 union 패딩 ---------- */
#define SMP_CACHE_BYTES 128            /* 리눅스 arm64 는 64 (L1_CACHE_SHIFT 6 — 버전 확인 필요) */
#define ____cacheline_aligned_in_smp __attribute__((__aligned__(SMP_CACHE_BYTES)))
struct percpu_stats {
    uint64_t pkts, bytes, drops;
} ____cacheline_aligned_in_smp;        /* 타입에 붙이면 크기도 128 로 올라간다 */

#define ROUND_UP(n, g) (((n) + (g) - 1) / (g) * (g))
union padded_stats {                   /* 크기만 맞춘 "패딩 union" — 정렬은 8 그대로 */
    struct { uint64_t pkts, bytes, drops; } v;
    char pad[ROUND_UP(3 * sizeof(uint64_t), 128)];
};

/* ---------- 연습 3: 핫/콜드 분리 ---------- */
typedef struct {                       /* 뚱뚱한 연결 구조체: 핫 16 B + 콜드 80 B */
    uint32_t seq, ack, wnd, flags;     /* 핫: 매 패킷 */
    char name[32];                     /* 콜드 */
    uint64_t created_ns;
    uint64_t stats[5];
} conn_fat_t;
typedef struct { uint32_t seq, ack, wnd, flags; } conn_hot_t;
/* base 가 g 정렬인 배열 n 개를 훑으며 각 원소의 [off, off+fsz) 만 건드릴 때 서로 다른 블록 수 */
static size_t lines_for_scan(int n, size_t elem, size_t off, size_t fsz, size_t g) {
    size_t cnt = 0, last = SIZE_MAX;
    for (int i = 0; i < n; i++) {
        uintptr_t a = (uintptr_t)i * elem + off;
        for (uintptr_t blk = a / g; blk <= (a + fsz - 1) / g; blk++)
            if (blk != last) { cnt++; last = blk; }      /* 오름차순으로 훑으니 직전 블록만 비교 */
    }
    return cnt;
}

/* ---------- 연습 4: Cortex-M7 DMA 버퍼 ---------- */
#define M7_LINE 32
enum { W_CPU = 1, W_DMA = 3 };
typedef struct {                       /* 나쁜 배치: DMA 가 쓰는 40 B 뒤에 ISR 카운터 */
    uint8_t rx_dma[40];
    volatile uint32_t isr_count;
    uint32_t baud;                     /* RO */
} uart_bad_t;
typedef struct {                       /* 고친 배치: 버퍼는 라인 정렬 + 라인 배수(64 B), 다른 것은 다음 라인부터 */
    _Alignas(M7_LINE) uint8_t rx_dma[ROUND_UP(40, M7_LINE)];
    _Alignas(M7_LINE) volatile uint32_t isr_count;
    _Alignas(M7_LINE) uint32_t baud_ro;  /* 읽기 위주 설정도 ISR 이 쓰는 라인과 떼어 둔다 */
} uart_good_t;

int main(void) {
    printf("-- 연습 1 --\n");
    CHECK(count_straddlers(24, 16, 64) == 4);     /* 48, 120, 240, 312 */
    CHECK(count_straddlers(24, 16, 128) == 2);    /* 120, 240 */
    CHECK(count_straddlers(32, 64, 128) == 0);    /* 라인을 나누는 크기면 절대 안 넘는다 */
    CHECK(count_straddlers(48, 8, 128) == 2);     /* 96, 240 */

    printf("-- 연습 2 --\n");
    CHECK(sizeof(struct percpu_stats) == 128 && alignof(struct percpu_stats) == 128);
    static struct percpu_stats per_cpu[12];
    bool ok = true;
    for (int i = 0; i + 1 < 12; i++)
        if (share_granule((uintptr_t)&per_cpu[i], sizeof per_cpu[i], (uintptr_t)&per_cpu[i + 1], sizeof per_cpu[i + 1], 128))
            ok = false;
    CHECK(ok);
    CHECK(sizeof(union padded_stats) == 128 && alignof(union padded_stats) == 8);
    /* 정렬이 8 이면 배열 시작이 0x1008 일 수 있다 → 원소 0 과 1 이 블록 하나를 같이 쓴다 */
    CHECK(share_granule(0x1008, 128, 0x1008 + 128, 128, 128) == true);
    CHECK(granules_touched(0x1008, 128, 128) == 2);

    printf("-- 연습 3 --\n");
    CHECK(sizeof(conn_fat_t) == 96 && sizeof(conn_hot_t) == 16);
    size_t fat = lines_for_scan(256, sizeof(conn_fat_t), 0, 16, 128);
    size_t hot = lines_for_scan(256, sizeof(conn_hot_t), 0, 16, 128);
    printf("      256 connections, scan seq/ack/wnd/flags: fat %zu lines, hot %zu lines\n", fat, hot);
    CHECK(fat == 192 && hot == 32);

    printf("-- 연습 4 --\n");
    static const field_t bad[] = { FLD(uart_bad_t, rx_dma, W_DMA), FLD(uart_bad_t, isr_count, W_CPU),
                                   FLD(uart_bad_t, baud, G_RO) };
    static const field_t good[] = { FLD(uart_good_t, rx_dma, W_DMA), FLD(uart_good_t, isr_count, W_CPU),
                                    FLD(uart_good_t, baud_ro, G_RO) };
    CHECK(layout_violations(bad, 3, M7_LINE) == 3);   /* dma-isr, dma-baud, isr-baud */
    CHECK(layout_violations(good, 3, M7_LINE) == 0);
    CHECK(offsetof(uart_good_t, rx_dma) % M7_LINE == 0 && sizeof(((uart_good_t *)0)->rx_dma) % M7_LINE == 0);
    CHECK(offsetof(uart_good_t, isr_count) == 64 && alignof(uart_good_t) == M7_LINE);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
