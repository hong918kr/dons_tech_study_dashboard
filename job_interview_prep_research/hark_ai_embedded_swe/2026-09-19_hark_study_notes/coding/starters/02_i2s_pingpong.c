/* 02. I2S DMA ping-pong buffer handoff — 연습용 뼈대
 *
 * TODO 만 채운다. 아래 테스트(main 과 fake_dma_*)는 건드리지 않는다.
 * 빌드·실행: `make run N=02`  (지금 상태로도 컴파일은 되지만 테스트는 실패한다)
 *
 * 하드웨어 헤더 없이 순수 C11로만 쓴다. 실제 타깃의 I2S RX DMA(circular mode)는
 * 반 버퍼를 다 채울 때마다 HT(half-transfer) / TC(transfer-complete) 인터럽트를
 * 올린다. 여기서는 테스트 main 안의 fake_dma_*() 가 그 인터럽트를 대신 호출한다.
 */

#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. 공개 인터페이스 (S01 Q03 과 동일한 이름/의미)                     */
/* ------------------------------------------------------------------ */

#define FRAME_SAMPLES 160u /* 16 kHz x 10 ms = 160 sample (mono, 16-bit) */
#define HALF_COUNT    2u   /* ping + pong                                */

typedef struct {
    /* DMA 대상 영역. 실제 타깃에서는 DMA 가능한 SRAM 에 둔다. */
    int16_t buf[HALF_COUNT][FRAME_SAMPLES];

    /* bit i = 절반 i 가 "다 찼고 task 가 아직 안 가져감" 상태 */
    _Atomic uint32_t ready_mask;

    /* task 가 늦어서 DMA 가 아직 안 읽은 절반을 다시 덮어쓴 횟수 */
    uint32_t overruns;
} pingpong_t;

void            pingpong_init(pingpong_t *pp);
void            audio_dma_isr(pingpong_t *pp, uint32_t half);
const int16_t  *audio_get_frame(pingpong_t *pp, uint32_t *half_out);
void            audio_release_frame(pingpong_t *pp, uint32_t half);
uint32_t        audio_overrun_count(const pingpong_t *pp);

/* ------------------------------------------------------------------ */
/* 2. 구현 — 여기만 채운다                                              */
/* ------------------------------------------------------------------ */

/* DMA 를 켜기 전에 한 번 호출한다. 버퍼를 0 으로, 플래그와 카운터를 초기화. */
void pingpong_init(pingpong_t *pp)
{
    (void)pp;
    /* TODO: buf 를 0 으로 지우고, ready_mask 와 overruns 를 0 으로 만든다. */
}

/* ISR 컨텍스트. half = 0 이면 HT, 1 이면 TC.
 * 샘플을 복사하면 안 된다. "절반 half 가 준비됐다"는 표시만 남긴다.
 * 그 표시가 이미 남아 있었다면 task 가 늦은 것이므로 overrun 을 센다. */
void audio_dma_isr(pingpong_t *pp, uint32_t half)
{
    (void)pp;
    (void)half;
    /* TODO: ready_mask 의 bit(half) 를 원자적으로 세우고, 직전 값에 그 bit 가
     *       이미 있었으면 overruns 를 1 증가시킨다. */
}

/* Task 컨텍스트. 처리할 절반의 시작 포인터를 돌려준다. 없으면 NULL.
 * 두 절반이 동시에 ready 면 낮은 번호(0)부터 준다. NULL 을 돌려줄 때는
 * half_out 을 건드리지 않는다. */
const int16_t *audio_get_frame(pingpong_t *pp, uint32_t *half_out)
{
    (void)pp;
    (void)half_out;
    /* TODO: ready_mask 를 읽어 세워진 bit 중 가장 낮은 번호의 절반을 고르고,
     *       half_out 에 번호를 쓰고 pp->buf[half] 를 반환한다. */
    return NULL;
}

/* 처리가 끝난 절반을 DMA 에게 돌려준다. 준비되지 않은 절반을 반납하거나
 * 두 번 반납해도 안전해야 한다(idempotent). */
void audio_release_frame(pingpong_t *pp, uint32_t half)
{
    (void)pp;
    (void)half;
    /* TODO: ready_mask 에서 bit(half) 만 원자적으로 지운다.
     *       다른 절반의 bit 를 건드리면 안 된다. */
}

uint32_t audio_overrun_count(const pingpong_t *pp)
{
    (void)pp;
    /* TODO: 지금까지 센 overrun 횟수를 반환한다. */
    return 0u;
}

/* ------------------------------------------------------------------ */
/* 3. 가짜 DMA 하드웨어 (테스트 전용)                                   */
/* ------------------------------------------------------------------ */

/* 실제 I2S RX DMA 를 흉내낸다. circular mode 이므로 절반을 번갈아 채우고,
 * 절반이 찰 때마다 인터럽트(= audio_dma_isr 호출)를 올린다. */
typedef struct {
    pingpong_t *pp;
    uint32_t    next_half; /* 지금 채우고 있는 절반 */
    int         block_id;  /* 몇 번째 10 ms 블록인가 */
} fake_dma_t;

static void fake_dma_init(fake_dma_t *d, pingpong_t *pp)
{
    d->pp        = pp;
    d->next_half = 0u;
    d->block_id  = 0;
}

/* block_id 와 sample index 를 둘 다 알아볼 수 있는 값. block_id < 128 이면
 * int16_t 범위 안이고, 어느 블록의 몇 번째 샘플인지 값만 보고 알 수 있다. */
static int16_t sample_of(int block_id, size_t i)
{
    return (int16_t)((block_id << 8) | (int)(i & 0xFFu));
}

/* 반 버퍼 하나가 다 찬 순간 = HT 또는 TC 인터럽트 한 번 */
static int fake_dma_fill_half(fake_dma_t *d)
{
    uint32_t half = d->next_half;
    int      id   = d->block_id;

    for (size_t i = 0; i < FRAME_SAMPLES; i++) {
        d->pp->buf[half][i] = sample_of(id, i); /* DMA 가 SRAM 에 쓰는 중 */
    }
    audio_dma_isr(d->pp, half); /* 하드웨어 인터럽트 */

    d->next_half ^= 1u;
    d->block_id++;
    return id;
}

/* 한 프레임을 꺼내 "처리"하고 반납한다. 처리한 block_id 를 돌려준다.
 * 꺼낼 게 없으면 -1. */
static int consume_one(pingpong_t *pp)
{
    uint32_t       half = 0u;
    const int16_t *pcm  = audio_get_frame(pp, &half);
    if (pcm == NULL) {
        return -1;
    }
    int id = pcm[0] >> 8;
    /* 프레임 전체가 같은 블록인지(= 중간에 찢기지 않았는지) 확인 */
    for (size_t i = 0; i < FRAME_SAMPLES; i++) {
        assert(pcm[i] == sample_of(id, i));
    }
    audio_release_frame(pp, half);
    return id;
}

static uint32_t mask_of(const pingpong_t *pp)
{
    return atomic_load_explicit(&pp->ready_mask, memory_order_acquire);
}

/* ------------------------------------------------------------------ */
/* 4. 테스트                                                           */
/* ------------------------------------------------------------------ */

static void test_init_state(void)
{
    pingpong_t pp;
    uint32_t   half = 99u;

    pingpong_init(&pp);
    assert(mask_of(&pp) == 0u);
    assert(audio_overrun_count(&pp) == 0u);
    assert(audio_get_frame(&pp, &half) == NULL);
    assert(half == 99u); /* NULL 이면 half_out 을 건드리지 않는다 */

    printf("  [ok] init: ready_mask=0, overruns=0, get_frame -> NULL\n");
}

static void test_single_ht(void)
{
    pingpong_t pp;
    fake_dma_t dma;
    uint32_t   half = 99u;

    pingpong_init(&pp);
    fake_dma_init(&dma, &pp);

    (void)fake_dma_fill_half(&dma); /* HT: 절반 0 이 찼다 */
    assert(mask_of(&pp) == 0x1u);

    const int16_t *pcm = audio_get_frame(&pp, &half);
    assert(pcm == pp.buf[0]); /* zero-copy: 복사본이 아니라 DMA 버퍼 자신 */
    assert(half == 0u);
    assert(pcm[0] == sample_of(0, 0));
    assert(pcm[FRAME_SAMPLES - 1] == sample_of(0, FRAME_SAMPLES - 1));

    audio_release_frame(&pp, half);
    assert(mask_of(&pp) == 0u);
    assert(audio_get_frame(&pp, &half) == NULL);
    assert(audio_overrun_count(&pp) == 0u);

    printf("  [ok] HT one shot: half0 ready -> zero-copy pointer -> release\n");
}

static void test_alternates(void)
{
    pingpong_t pp;
    fake_dma_t dma;

    pingpong_init(&pp);
    fake_dma_init(&dma, &pp);

    /* 제때 소비하는 task: 인터럽트 한 번마다 한 프레임 처리 */
    for (int n = 0; n < 8; n++) {
        int produced = fake_dma_fill_half(&dma);
        int consumed = consume_one(&pp);
        assert(consumed == produced);       /* 블록이 순서대로 나온다 */
        assert(mask_of(&pp) == 0u);         /* 매번 비워진다 */
    }
    assert(audio_overrun_count(&pp) == 0u); /* 늦지 않았으니 0 */

    printf("  [ok] steady state: 8 blocks alternate half0/half1, 0 overruns\n");
}

static void test_ownership_isolated(void)
{
    pingpong_t pp;
    fake_dma_t dma;
    uint32_t   half = 99u;

    pingpong_init(&pp);
    fake_dma_init(&dma, &pp);

    int id0 = fake_dma_fill_half(&dma); /* 절반 0 = block 0 */
    const int16_t *pcm = audio_get_frame(&pp, &half);
    assert(half == 0u);

    /* task 가 절반 0 을 읽는 동안 DMA 는 절반 1 을 채운다 */
    int id1 = fake_dma_fill_half(&dma);
    assert(mask_of(&pp) == 0x3u);

    /* 내가 들고 있던 절반 0 은 한 바이트도 안 바뀌었다 */
    for (size_t i = 0; i < FRAME_SAMPLES; i++) {
        assert(pcm[i] == sample_of(id0, i));
    }
    assert(pp.buf[1][0] == sample_of(id1, 0));
    assert(audio_overrun_count(&pp) == 0u);

    printf("  [ok] ownership: DMA writes half1 while task reads half0, no tearing\n");
}

static void test_overrun_detect(void)
{
    pingpong_t pp;
    fake_dma_t dma;

    pingpong_init(&pp);
    fake_dma_init(&dma, &pp);

    (void)fake_dma_fill_half(&dma); /* block 0 -> half0, mask=0b01 */
    (void)fake_dma_fill_half(&dma); /* block 1 -> half1, mask=0b11 */
    assert(audio_overrun_count(&pp) == 0u);

    /* task 가 하나도 안 가져갔는데 DMA 가 한 바퀴 돌아 half0 을 다시 썼다 */
    (void)fake_dma_fill_half(&dma); /* block 2 -> half0, 이미 ready */
    assert(audio_overrun_count(&pp) == 1u);
    assert(mask_of(&pp) == 0x3u); /* bit 는 여전히 둘 다 서 있다 */

    (void)fake_dma_fill_half(&dma); /* block 3 -> half1, 또 overrun */
    assert(audio_overrun_count(&pp) == 2u);

    printf("  [ok] overrun: late consumer counted twice, mask stays 0b11\n");
}

static void test_overrun_is_stale_not_dropped(void)
{
    pingpong_t pp;
    fake_dma_t dma;
    uint32_t   half = 99u;

    pingpong_init(&pp);
    fake_dma_init(&dma, &pp);

    (void)fake_dma_fill_half(&dma); /* block 0 -> half0 */
    (void)fake_dma_fill_half(&dma); /* block 1 -> half1 */
    (void)fake_dma_fill_half(&dma); /* block 2 -> half0 (block 0 을 덮어씀) */
    assert(audio_overrun_count(&pp) == 1u);

    /* 잃은 것은 block 0 이다. 버퍼에는 최신 block 2 가 들어 있다.
     * 즉 overrun 은 "새 데이터를 버리는" 것이 아니라 "옛 데이터를 잃는" 것이다. */
    const int16_t *pcm = audio_get_frame(&pp, &half);
    assert(half == 0u);
    assert((pcm[0] >> 8) == 2);
    audio_release_frame(&pp, half);

    /* 그 다음에 half1 = block 1 이 나온다. 시간 순서가 뒤집혀 보인다(2 -> 1).
     * 실제 제품에서는 이 지점에서 resync 정책이 필요하다(노트 10절). */
    pcm = audio_get_frame(&pp, &half);
    assert(half == 1u);
    assert((pcm[0] >> 8) == 1);
    audio_release_frame(&pp, half);
    assert(mask_of(&pp) == 0u);

    printf("  [ok] overrun semantics: oldest block lost, buffer holds newest\n");
}

static void test_both_ready_order(void)
{
    pingpong_t pp;
    fake_dma_t dma;
    uint32_t   half = 99u;

    pingpong_init(&pp);
    fake_dma_init(&dma, &pp);

    (void)fake_dma_fill_half(&dma);
    (void)fake_dma_fill_half(&dma);
    assert(mask_of(&pp) == 0x3u);

    assert(audio_get_frame(&pp, &half) == pp.buf[0]); /* 낮은 번호부터 */
    assert(half == 0u);
    audio_release_frame(&pp, 0u);
    assert(mask_of(&pp) == 0x2u);

    assert(audio_get_frame(&pp, &half) == pp.buf[1]);
    assert(half == 1u);
    audio_release_frame(&pp, 1u);
    assert(mask_of(&pp) == 0u);

    printf("  [ok] backlog drain: both halves ready -> half0 then half1\n");
}

static void test_release_is_idempotent(void)
{
    pingpong_t pp;
    fake_dma_t dma;

    pingpong_init(&pp);
    fake_dma_init(&dma, &pp);

    (void)fake_dma_fill_half(&dma); /* half0 ready */
    audio_release_frame(&pp, 1u);   /* 준비 안 된 절반 반납: 무해 */
    assert(mask_of(&pp) == 0x1u);

    audio_release_frame(&pp, 0u);
    audio_release_frame(&pp, 0u); /* 두 번 반납해도 안전 */
    assert(mask_of(&pp) == 0u);
    assert(audio_overrun_count(&pp) == 0u);

    printf("  [ok] release: idempotent, only clears its own bit\n");
}

static void test_late_consumer_rate(void)
{
    pingpong_t pp;
    fake_dma_t dma;

    pingpong_init(&pp);
    fake_dma_init(&dma, &pp);

    /* 처리 시간이 프레임 주기의 2배인 task: 인터럽트 2번마다 1프레임만 처리.
     *  i=0 half0 set (mask 01)
     *  i=1 half1 set (mask 11) -> half0 소비 (mask 10)
     *  i=2 half0 set (mask 11)
     *  i=3 half1 set -> 이미 ready => overrun #1, half0 소비
     *  i=5, 7, 9 도 같은 이유로 overrun. 총 4회. */
    for (int i = 0; i < 10; i++) {
        (void)fake_dma_fill_half(&dma);
        if (i % 2 == 1) {
            (void)consume_one(&pp);
        }
    }
    assert(audio_overrun_count(&pp) == 4u);
    assert(mask_of(&pp) == 0x2u); /* 백로그가 계속 남아 있다 */

    printf("  [ok] half-rate consumer: 10 IRQs -> 4 overruns, backlog never drains\n");
}

static void test_recovery_after_overrun(void)
{
    pingpong_t pp;
    fake_dma_t dma;

    pingpong_init(&pp);
    fake_dma_init(&dma, &pp);

    /* 먼저 일부러 밀린 상태를 만든다 */
    for (int i = 0; i < 4; i++) {
        (void)fake_dma_fill_half(&dma);
    }
    uint32_t before = audio_overrun_count(&pp);
    assert(before == 2u);

    /* task 가 백로그를 전부 비운다(drain) */
    while (consume_one(&pp) >= 0) {
        /* 두 절반 다 반납 */
    }
    assert(mask_of(&pp) == 0u);

    /* 이제부터 제때 처리하면 카운터는 더 이상 늘지 않는다 */
    for (int i = 0; i < 20; i++) {
        int produced = fake_dma_fill_half(&dma);
        int consumed = consume_one(&pp);
        assert(consumed == produced);
    }
    assert(audio_overrun_count(&pp) == before);

    printf("  [ok] recovery: drain backlog, then 20 frames with no new overrun\n");
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0); /* assert 로 죽어도 앞 줄은 보이게 */
    printf("02_i2s_pingpong (starter)\n");

    test_init_state();
    test_single_ht();
    test_alternates();
    test_ownership_isolated();
    test_overrun_detect();
    test_overrun_is_stale_not_dropped();
    test_both_ready_order();
    test_release_is_idempotent();
    test_late_consumer_rate();
    test_recovery_after_overrun();

    printf("ALL TESTS PASSED\n");
    return 0;
}
