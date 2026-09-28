/* 02_i2s_pingpong.c — I2S DMA ping-pong buffer handoff
 *
 * 빌드: cc -std=c11 -Wall -Wextra -O2 02_i2s_pingpong.c -o run_02
 * 또는: make prob N=02 && make run N=02
 *
 * 요구사항: 문제 파일(02_i2s_pingpong.md)을 읽고, 아래 // TODO 섹션들을 채워라.
 * 모든 테스트가 PASS 되면 완성이다.
 *
 * 개요:
 * - I2S RX DMA(circular mode)가 반 버퍼를 다 채우면 HT/TC 인터럽트 발생.
 * - ISR은 "절반이 준비됐다"는 비트만 세운다 (복사 없음).
 * - Task는 ready 비트를 확인하고 절반의 포인터를 받아서 처리한다.
 * - Task가 늦으면 overrun: ISR이 task가 아직 안 가져간 절반을 덮어쓴다.
 */

#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. 자료구조                                                         */
/* ------------------------------------------------------------------ */

#define FRAME_SAMPLES 160u /* 16 kHz x 10 ms = 160 sample (mono, 16-bit) */
#define HALF_COUNT    2u   /* ping + pong                                */

typedef struct {
    // TODO: 다음 필드들을 추가하세요:
    // - buf: int16_t buf[HALF_COUNT][FRAME_SAMPLES]
    //        DMA가 쓸 메모리. 실제 타깃에서는 D-cache line(32 B) 정렬.
    // - ready_mask: _Atomic uint32_t
    //        bit i = "절반 i가 다 찬 상태 (ISR이 세웠으나 task가 아직 안 가져감)".
    //        ISR과 task가 다른 bit를 건드리므로 atomic fetch_or 필요.
    // - overruns: uint32_t (ISR만 쓰므로 plain int)
    //        task가 늦어서 DMA가 절반을 덮어쓴 횟수.
} pingpong_t;

/* ------------------------------------------------------------------ */
/* 2. 초기화                                                           */
/* ------------------------------------------------------------------ */

void pingpong_init(pingpong_t *pp)
{
    // TODO: buf를 0으로 채우고, ready_mask와 overruns를 초기화하세요.
    // memset(pp->buf, 0, sizeof pp->buf);
    // atomic_store_explicit(..., memory_order_relaxed);
    // pp->overruns = 0u;
}

/* ------------------------------------------------------------------ */
/* 3. ISR (DMA HT/TC 인터럽트)                                         */
/* ------------------------------------------------------------------ */

/* ISR 컨텍스트. half = 0이면 HT(첫 절반), 1이면 TC(두 번째 절반).
 * 복사하지 않는다. "절반 half가 준비됐다"는 bit만 세운다.
 */
void audio_dma_isr(pingpong_t *pp, uint32_t half)
{
    // TODO: 구현하세요.
    // 1. bit = 1u << half로 비트를 만든다.
    // 2. atomic_fetch_or_explicit으로 ready_mask에 bit를 세운다 (memory_order_release).
    //    (atomic_fetch_or는 이전 값을 반환한다)
    // 3. 반환된 값이 이미 bit를 포함했다면 overrun: pp->overruns++
    //    (task가 아직 이 절반을 안 가져갔는데 DMA가 다시 덮어쓴 것)
    //
    // 힌트: atomic_fetch_or는 memory_order_release로 세워서 task가 보게 한다.
}

/* ------------------------------------------------------------------ */
/* 4. Task (메인 루프)                                                 */
/* ------------------------------------------------------------------ */

/* Task 컨텍스트. 처리할 절반의 시작 포인터를 돌려준다. 없으면 NULL.
 * half_out에는 돌려주는 절반의 인덱스를 저장한다.
 * 두 절반이 동시에 ready면 낮은 번호부터 준다.
 */
const int16_t *audio_get_frame(pingpong_t *pp, uint32_t *half_out)
{
    // TODO: 구현하세요.
    // 1. ready_mask를 memory_order_acquire로 읽는다.
    // 2. m이 0이면 NULL 반환.
    // 3. __builtin_ctz(m) 또는 __builtin_ffs(m)로 첫 세워진 비트를 찾는다.
    //    (또는 manual loop)
    // 4. half를 그 인덱스로 설정하고, pp->buf[half]의 포인터를 반환.
    // 5. half_out에 half를 저장.
    //
    // 예: half = __builtin_ctz(m); /* 첫 세워진 비트의 위치 */
    //     *half_out = half;
    //     return pp->buf[half];
}

/* Task가 절반 처리를 다 했다고 알린다. ready_mask에서 bit를 내린다. */
void audio_release_frame(pingpong_t *pp, uint32_t half)
{
    // TODO: 구현하세요.
    // atomic_fetch_and_explicit으로 ready_mask에서 bit를 내린다.
    // bit = 1u << half;
    // atomic_fetch_and_explicit(&pp->ready_mask, ~bit, memory_order_release);
}

/* ------------------------------------------------------------------ */
/* 5. 관찰 함수                                                        */
/* ------------------------------------------------------------------ */

uint32_t audio_overrun_count(const pingpong_t *pp)
{
    return pp->overruns;
}

/* ------------------------------------------------------------------ */
/* 6. 테스트                                                           */
/* ------------------------------------------------------------------ */

#define OK(msg) printf("  ok  %s\n", (msg))

static void test_init(void)
{
    pingpong_t pp;
    pingpong_init(&pp);

    uint32_t half = 999;
    const int16_t *frame = audio_get_frame(&pp, &half);
    assert(frame == NULL);
    assert(audio_overrun_count(&pp) == 0u);

    OK("init 후 no ready frame, no overruns");
}

static void test_single_half(void)
{
    pingpong_t pp;
    pingpong_init(&pp);

    audio_dma_isr(&pp, 0);  /* HT: 절반 0이 준비됨 */

    uint32_t half = 999;
    const int16_t *frame = audio_get_frame(&pp, &half);
    assert(frame != NULL);
    assert(half == 0);
    assert(frame == pp.buf[0]);

    audio_release_frame(&pp, half);

    frame = audio_get_frame(&pp, &half);
    assert(frame == NULL);  /* 다시 비었다 */

    OK("HT ISR -> get frame 0 -> release -> empty");
}

static void test_pingpong_alternation(void)
{
    pingpong_t pp;
    pingpong_init(&pp);

    for (uint32_t i = 0; i < 10u; i++) {
        uint32_t half_to_irq = i % 2;
        audio_dma_isr(&pp, half_to_irq);

        uint32_t half = 999;
        const int16_t *frame = audio_get_frame(&pp, &half);
        assert(frame != NULL);
        assert(half == half_to_irq);

        audio_release_frame(&pp, half);
    }

    assert(audio_overrun_count(&pp) == 0u);
    OK("ping-pong alternation (0/1/0/1/...) 10라운드, no overrun");
}

static void test_double_irq_same_half(void)
{
    pingpong_t pp;
    pingpong_init(&pp);

    audio_dma_isr(&pp, 0);  /* 절반 0 ready */
    assert(audio_overrun_count(&pp) == 0u);

    audio_dma_isr(&pp, 0);  /* 절반 0 다시 ready (task가 안 가져감) = overrun */
    assert(audio_overrun_count(&pp) == 1u);

    uint32_t half = 999;
    const int16_t *frame = audio_get_frame(&pp, &half);
    assert(frame != NULL);
    assert(half == 0);

    audio_release_frame(&pp, half);

    OK("절반 0 두 번 ISR -> overrun 카운트 증가");
}

static void test_both_halves_ready(void)
{
    pingpong_t pp;
    pingpong_init(&pp);

    audio_dma_isr(&pp, 1);  /* 절반 1 ready */
    audio_dma_isr(&pp, 0);  /* 절반 0 ready (1은 아직 안 가져감) */

    /* 낮은 번호부터 준다 */
    uint32_t half = 999;
    const int16_t *frame = audio_get_frame(&pp, &half);
    assert(frame != NULL);
    assert(half == 0);

    audio_release_frame(&pp, half);

    frame = audio_get_frame(&pp, &half);
    assert(frame != NULL);
    assert(half == 1);

    audio_release_frame(&pp, half);

    frame = audio_get_frame(&pp, &half);
    assert(frame == NULL);

    OK("두 절반 동시 ready -> 낮은 번호부터 반환");
}

int main(void)
{
    printf("=== 02 I2S DMA ping-pong buffer ===\n");
    test_init();
    test_single_half();
    test_pingpong_alternation();
    test_double_irq_same_half();
    test_both_halves_ready();
    printf("ALL TESTS PASSED\n");
    return 0;
}
