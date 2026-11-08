/* C-B06 배열 vs 포인터: Decay, sizeof, 다차원 배열
 * 기준: 64비트 호스트(macOS arm64, x86-64 Linux) — sizeof(포인터) == 8. Cortex-M 이면 4.
 * IS_ARRAY / ARRAY_SIZE 는 GNU 확장(__builtin_types_compatible_p, __typeof__)을 쓴다 → gcc/clang 전용.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- Part A: decay 와 sizeof 예측 ---------------- */
static int32_t     arr[5];
static int16_t     grid[3][4];
static char        msg[]  = "ok";
static const char *pmsg   = "ok";
static char        tag[4] __attribute__((nonstring)) = "ABCD";  /* C 에선 합법: NUL 없이 4바이트 */

/* 각 식의 값을 "실행 전에" 손으로 예측해 채운다 (0 = 아직 안 채움 = TODO). */
enum {
    P_SIZEOF_ARR      = 20,   /* sizeof arr                          */
    P_SIZEOF_ADDR_ARR = 8,    /* sizeof &arr                         */
    P_SIZEOF_ELEM0    = 8,    /* sizeof &arr[0]                      */
    P_SIZEOF_DEREF    = 20,   /* sizeof *&arr                        */
    P_STEP_ARR        = 4,    /* (char *)(arr + 1)  - (char *)arr    */
    P_STEP_ADDR_ARR   = 20,   /* (char *)(&arr + 1) - (char *)&arr   */
    P_SIZEOF_GRID     = 24,   /* sizeof grid                         */
    P_SIZEOF_ROW      = 8,    /* sizeof grid[0]                      */
    P_STEP_GRID       = 8,    /* (char *)(grid + 1) - (char *)grid   */
    P_SIZEOF_MSG      = 3,    /* sizeof msg                          */
    P_SIZEOF_PMSG     = 8,    /* sizeof pmsg                         */
    P_SIZEOF_TAG      = 4,    /* sizeof tag                          */
};

/* ---------------- Part B: 포인터를 거부하는 ARRAY_SIZE ---------------- */
/* x 가 진짜 배열이면 1, 포인터면 0. 힌트: 배열 a 와 &a[0] 은 타입이 다르다. */
#define IS_ARRAY(x) (!__builtin_types_compatible_p(__typeof__(x), __typeof__(&(x)[0])))
/* x 가 배열이 아니면 "컴파일 에러", 배열이면 값이 0 인 정수 상수식 */
#define MUST_BE_ARRAY(x) (0 * sizeof(struct { \
    _Static_assert(IS_ARRAY(x), "ARRAY_SIZE() needs an array, not a pointer"); char dummy_; }))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]) + MUST_BE_ARRAY(x))

static int param_is_array(int32_t a[5]) { return IS_ARRAY(a); }   /* 파라미터 a 의 진짜 타입은? */

/* 배열 "크기까지" 넘기기: 배열 포인터 파라미터. 함수 안에서도 sizeof *key == 16 이 유지된다. */
static size_t key_fill(uint8_t (*key)[16], uint8_t v) {
    memset(*key, v, sizeof *key);
    return sizeof *key;
}

/* ---------------- Part C: ADC DMA 버퍼(2D) + DMA 디스크립터 링 ---------------- */
#define N_CH     4    /* ADC 스캔 채널 수 */
#define N_FRAMES 8    /* DMA 가 한 번에 채우는 프레임 수 */
static uint16_t adc_dma_buf[N_FRAMES][N_CH];   /* DMA 가 [frame][ch] 순서로 인터리브해서 채운다 */

/* 채널 ch 의 평균(내림). frames == 0 이거나 ch 가 범위 밖이면 0. */
static uint16_t adc_channel_avg(size_t frames, const uint16_t (*buf)[N_CH], size_t ch) {
    if (frames == 0 || ch >= ARRAY_SIZE(*buf)) return 0;
    uint32_t sum = 0;
    for (size_t f = 0; f < frames; f++) sum += buf[f][ch];
    return (uint16_t)(sum / frames);
}

#define RX_BLK   32   /* 디스크립터 하나가 맡는 바이트 */
#define RX_NDESC 4
#define DESC_OWN 1u   /* ctrl bit0: 1 = DMA 소유 */

/* 실제 칩(STM32 ETH DMA, NXP eDMA TCD 등)은 32비트 주소 필드. 호스트 테스트라 uintptr_t. */
struct dma_desc {
    uintptr_t buf;    /* 버퍼 시작 주소 */
    uint32_t  len;    /* 바이트 수 */
    uint32_t  ctrl;   /* DESC_OWN 등 */
    uintptr_t next;   /* 다음 디스크립터 주소 */
};
static struct dma_desc rx_ring[RX_NDESC];
static uint8_t         rx_bufs[RX_NDESC][RX_BLK];
_Static_assert(ARRAY_SIZE(rx_ring) == ARRAY_SIZE(rx_bufs), "descriptor/buffer count mismatch");

/* n 개 디스크립터를 링으로 묶는다: buf = bufs[i], len = 블록 크기(sizeof 로), ctrl = OWN,
 * next = 다음 디스크립터(마지막은 ring[0]). 반환: 링 전체 바이트 수(캐시 clean 길이). */
static size_t dma_ring_init(struct dma_desc *ring, size_t n, uint8_t (*bufs)[RX_BLK]) {
    for (size_t i = 0; i < n; i++) {
        ring[i].buf  = (uintptr_t)bufs[i];
        ring[i].len  = (uint32_t)sizeof bufs[i];
        ring[i].ctrl = DESC_OWN;
        ring[i].next = (uintptr_t)&ring[(i + 1) % n];
    }
    return n * sizeof ring[0];
}

/* DMA 엔진이 next 를 따라가는 동작 흉내 (테스트용) */
static const struct dma_desc *dma_sim_walk(const struct dma_desc *d, size_t steps) {
    while (steps-- && d) d = (const struct dma_desc *)d->next;   /* next == 0 이면 멈춤 */
    return d;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* Part A — 예측이 실제와 같은가 */
    CHECK(P_SIZEOF_ARR      == sizeof arr);
    CHECK(P_SIZEOF_ADDR_ARR == sizeof &arr);
    CHECK(P_SIZEOF_ELEM0    == sizeof &arr[0]);
    CHECK(P_SIZEOF_DEREF    == sizeof *&arr);
    CHECK(P_STEP_ARR        == (char *)(arr + 1) - (char *)arr);
    CHECK(P_STEP_ADDR_ARR   == (char *)(&arr + 1) - (char *)&arr);
    CHECK(P_SIZEOF_GRID     == sizeof grid);
    CHECK(P_SIZEOF_ROW      == sizeof grid[0]);
    CHECK(P_STEP_GRID       == (char *)(grid + 1) - (char *)grid);
    CHECK(P_SIZEOF_MSG      == sizeof msg && msg[2] == '\0');
    CHECK(P_SIZEOF_PMSG     == sizeof pmsg && pmsg[1] == 'k');
    CHECK(P_SIZEOF_TAG      == sizeof tag && tag[3] == 'D');

    /* Part B — IS_ARRAY / ARRAY_SIZE / 배열 포인터 파라미터 */
    CHECK(IS_ARRAY(arr) == 1);
    CHECK(IS_ARRAY(grid[1]) == 1);
    CHECK(IS_ARRAY("hello") == 1);
    CHECK(IS_ARRAY(pmsg) == 0);
    CHECK(IS_ARRAY(arr + 0) == 0);
    CHECK(IS_ARRAY(&arr) == 0);
    CHECK(param_is_array(arr) == 0);
    CHECK(ARRAY_SIZE(arr) == 5 && ARRAY_SIZE(grid) == 3 && ARRAY_SIZE(grid[0]) == 4);
    CHECK(ARRAY_SIZE("hello") == 6 && ARRAY_SIZE(msg) == 3);
    uint8_t keys[3][16] = {{0}};
    CHECK(key_fill(&keys[1], 0xAA) == 16);
    CHECK(keys[1][0] == 0xAA && keys[1][15] == 0xAA && keys[0][15] == 0 && keys[2][0] == 0);
    CHECK(key_fill(keys + 2, 0x55) == 16 && keys[2][15] == 0x55);

    /* Part C — ADC 2D 버퍼 */
    for (size_t f = 0; f < N_FRAMES; f++)
        for (size_t c = 0; c < N_CH; c++) adc_dma_buf[f][c] = (uint16_t)(1000 * c + f);
    CHECK((char *)&adc_dma_buf[1][0] - (char *)&adc_dma_buf[0][0] == N_CH * sizeof(uint16_t));
    CHECK(adc_channel_avg(N_FRAMES, adc_dma_buf, 0) == 3);      /* (0+..+7)/8 = 3.5 → 3 */
    CHECK(adc_channel_avg(N_FRAMES, adc_dma_buf, 3) == 3003);
    CHECK(adc_channel_avg(4, adc_dma_buf + 4, 1) == 1005);      /* 뒤쪽 절반(4..7)만 */
    CHECK(adc_channel_avg(N_FRAMES, adc_dma_buf, N_CH) == 0 && adc_channel_avg(0, adc_dma_buf, 0) == 0);

    /* Part C — DMA 디스크립터 링 */
    size_t bytes = dma_ring_init(rx_ring, ARRAY_SIZE(rx_ring), rx_bufs);
    CHECK(bytes == sizeof rx_ring);
    CHECK(rx_ring[2].buf == (uintptr_t)rx_bufs[2] && rx_ring[2].len == RX_BLK);
    CHECK(rx_ring[0].ctrl == DESC_OWN && rx_ring[RX_NDESC - 1].ctrl == DESC_OWN);
    CHECK(rx_ring[RX_NDESC - 1].next == (uintptr_t)&rx_ring[0]);
    CHECK(dma_sim_walk(rx_ring, 1) == &rx_ring[1] && dma_sim_walk(rx_ring, RX_NDESC) == rx_ring);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
