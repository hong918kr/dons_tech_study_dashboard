# 02. I2S DMA 핑퐁 버퍼 핸드오프

> **주제**: DMA double buffering · ISR-task 소유권 · overrun 검출 · **난이도**: 중급 · **목표 시간**: 35분
> **해설은 보지 말 것**: 먼저 `starters/02_i2s_pingpong.c`를 채워 `make run N=02`로 통과시킨다.

## 면접관의 문장

"Our device has a MEMS microphone on I2S. The RX DMA runs in circular mode over one buffer that is split into two halves. When the first half is full we get a half-transfer interrupt, and when the second half is full we get a transfer-complete interrupt, and then the DMA wraps around and starts overwriting the first half again. Write the handoff between that ISR and the processing task. The ISR must not copy audio, the task must only ever touch the half the DMA is not writing, and if the task is too slow I want to know about it — count it, don't hide it. Assume no RTOS calls; just show me the data structure and the three functions."

## 요구사항

1. 버퍼는 `int16_t buf[2][FRAME_SAMPLES]`이고 `FRAME_SAMPLES`는 160이다. 16 kHz mono 기준 한 절반이 정확히 10 ms다.
2. `audio_dma_isr(pp, half)`는 ISR 컨텍스트에서 불린다. `half == 0`이면 half-transfer(HT), `half == 1`이면 transfer-complete(TC)다. 샘플을 **복사하지 않는다**. "절반 `half`가 준비됨"만 기록한다.
3. 준비 상태는 `ready_mask`의 bit로 표현한다. bit 0이 절반 0, bit 1이 절반 1이다.
4. ISR이 bit를 세우려는데 **이미 세워져 있었다면** overrun이다. `overruns`를 1 증가시킨다. bit는 그대로 둔다(DMA는 멈추지 않았고 데이터는 이미 덮여 있다).
5. `audio_get_frame(pp, &half)`는 task 컨텍스트에서 불린다. 준비된 절반이 없으면 `NULL`을 반환하고 `half_out`을 **건드리지 않는다**. 있으면 그 절반의 첫 샘플 주소(zero-copy)를 반환하고 `half_out`에 절반 번호를 쓴다.
6. 두 절반이 동시에 준비된 경우(`ready_mask == 0b11`, task가 한 프레임 이상 밀린 상태) 낮은 번호인 절반 0부터 반환한다.
7. `audio_release_frame(pp, half)`는 그 절반의 bit **하나만** 지운다. 준비되지 않은 절반을 반납하거나 같은 절반을 두 번 반납해도 안전해야 한다(idempotent).
8. `pingpong_init(pp)`는 DMA를 켜기 전에 불린다. 버퍼를 0으로 지우고 `ready_mask`와 `overruns`를 0으로 만든다.
9. `audio_overrun_count(pp)`는 지금까지 센 overrun 횟수를 반환한다.
10. ISR과 task가 같은 word의 서로 다른 bit를 동시에 바꾼다. 읽고-고치고-쓰기가 **원자적**이어야 한다.

## 인터페이스

```c
#define FRAME_SAMPLES 160u   /* 16 kHz x 10 ms */
#define HALF_COUNT    2u

typedef struct {
    int16_t          buf[HALF_COUNT][FRAME_SAMPLES];
    _Atomic uint32_t ready_mask;   /* bit i = 절반 i 가 처리 대기 중 */
    uint32_t         overruns;     /* ISR 만 쓴다 */
} pingpong_t;

void            pingpong_init(pingpong_t *pp);
void            audio_dma_isr(pingpong_t *pp, uint32_t half);
const int16_t  *audio_get_frame(pingpong_t *pp, uint32_t *half_out);
void            audio_release_frame(pingpong_t *pp, uint32_t half);
uint32_t        audio_overrun_count(const pingpong_t *pp);
```

## 제약

- 동적 할당 금지. 버퍼는 구조체 안에 통째로 들어 있다.
- 하드웨어·RTOS 헤더 금지. DMA 인터럽트는 테스트 `main`이 부르는 평범한 함수로 흉내낸다.
- ISR 본문은 O(1)이고 루프가 없어야 한다. 샘플 복사·`memcpy`·부동소수점 금지.
- 락 금지. ISR에서 mutex를 잡을 수 없다. C11 `<stdatomic.h>`만 쓴다.
- `cc -std=c11 -Wall -Wextra -O2`에서 경고 0개.
- `audio_get_frame`이 돌려준 포인터는 같은 절반을 `audio_release_frame`으로 반납할 때까지 task 소유다.

## 예시 동작

정상(task가 제때 처리):

```
시간 →     0ms        10ms       20ms       30ms
DMA 쓰기  [= half0 =][= half1 =][= half0 =][= half1 =]
IRQ                 HT         TC         HT         TC
mask                01 -> 00   10 -> 00   01 -> 00   10 -> 00
task                [proc h0]  [proc h1]  [proc h0]
overruns  0          0          0          0          0
```

overrun(task가 한 프레임 이상 밀림):

```
시간 →     0ms        10ms       20ms       30ms
DMA 쓰기  [= half0 =][= half1 =][= half0 =][= half1 =]
IRQ                 HT         TC         HT(!)      TC(!)
mask                01         11         11         11
task                [------ proc h0 아직 안 끝남 ------]
overruns  0          0          0          1          2
```

세 번째 IRQ에서 bit 0이 이미 서 있다. 그 순간 half0에 있던 첫 번째 블록은 이미 사라졌고 버퍼에는 세 번째 블록이 들어 있다.

## 스스로 점검할 질문

1. ISR이 `ready_mask |= bit` 를 그냥 썼다면 무엇이 깨지나? task가 동시에 `&= ~other_bit`를 하고 있다면?
2. `overruns`는 왜 `_Atomic`이 아니어도 되나? 어떤 조건이 깨지면 필요해지나?
3. overrun이 났을 때 bit를 그대로 두는 것과 지우는 것은 각각 어떤 의미인가?
4. `audio_get_frame`이 `NULL`일 때 `half_out`을 건드리지 않아야 하는 이유는?
5. 두 절반이 동시에 ready일 때 절반 0부터 주면 오디오 시간 순서에 무슨 일이 생기나?
6. `FRAME_SAMPLES`를 160에서 320으로 바꾸면 latency, 인터럽트 빈도, SRAM은 각각 어떻게 변하나?
7. D-cache가 있는 코어(Cortex-M7)에서 이 코드에 무엇을 더 넣어야 하나?
8. overrun 카운터가 0이 아니면 제일 먼저 확인할 숫자는 무엇인가?

## follow-up (면접관이 이어서 물을 것)

1. "Overrun happens. Do you drop the new audio or keep the stale frame? Why?"
2. "Two buffers are not enough because our inference time is bursty. How would you change this?"
3. "Your DMA controller has no half-transfer interrupt. Now what?"
4. "How do you wake the task from the ISR in FreeRTOS or Zephyr, and what does that cost?"
5. "How would you pick the block size for a battery-powered always-on microphone?"
