# 02. I2S DMA 핑퐁 버퍼 핸드오프 — 해설

> **문제**: [problems/02_i2s_pingpong.md](../problems/02_i2s_pingpong.html) · **답안**: `solutions/02_i2s_pingpong.c`
> **이 노트를 다 읽으면**: (1) DMA가 쓰는 절반과 CPU가 읽는 절반이 왜 절대 겹치면 안 되는지를 타임라인으로 그릴 수 있다. (2) overrun을 "늦게 감지"하는 것과 "실제로 데이터가 깨지는 순간"이 다르다는 것을 설명할 수 있다. (3) 블록 크기를 latency, 인터럽트 빈도, 전력으로 정당화할 수 있다.

## 0. 한 문장으로

DMA가 절반 A를 채우는 동안 task는 절반 B를 처리하고, 인터럽트는 "B 준비됨"이라는 bit 하나만 원자적으로 세우며, 그 bit가 이미 서 있었다면 task가 마감을 놓친 것이므로 overrun으로 센다.

---

## 1. 왜 이 패턴이 필요한가 — 하드웨어에서 출발

마이크는 멈추지 않는다. I2S는 워드 클럭(LRCLK)에 맞춰 16 kHz면 62.5 us마다 한 샘플씩 꼬박꼬박 내보낸다. CPU가 무슨 일을 하든, 저전력 모드에 들어가 있든 상관없다. 그래서 오디오 경로는 "CPU가 여유 있을 때 읽는다"가 성립하지 않는다. 다음 세 가지가 동시에 참이어야 한다.

```
 [MEMS mic] --SD--> [I2S 주변장치] --FIFO--> [DMA controller] --AHB--> [SRAM]
      ^                   |                        |                     |
   BCLK/LRCLK        FIFO 깊이 4~8 word       burst 전송             ping-pong 버퍼
   (마스터가 공급)    (여기가 넘치면 즉시 손실)   CPU 개입 0회          [ half0 | half1 ]
                                                                         |
                                                              절반이 찰 때마다 IRQ
                                                                         v
                                              [ISR: bit 하나 세우고 task 깨움] --> [처리 task]
                                                                                   FFT / MFCC
                                                                                   wake-word 추론
```

- **연속성**: 샘플 사이에 빈틈이 있으면 안 된다. DMA가 절반을 다 채우고 다음 절반으로 넘어가는 데 CPU가 개입하면 그 사이 샘플이 FIFO에 쌓이다 넘친다. 그래서 circular mode DMA를 쓰고 하드웨어가 알아서 wrap하게 둔다.
- **배타성**: CPU가 읽는 영역에 DMA가 쓰면 프레임이 찢어진다(tearing). 앞부분은 1번 블록, 뒷부분은 3번 블록인 오디오가 만들어진다.
- **저비용 ISR**: ISR에서 160 샘플을 복사하면 그만큼 다른 인터럽트가 밀린다. 복사는 0회여야 한다(zero-copy). ISR은 플래그만 남긴다.

ping-pong 버퍼는 이 셋을 동시에 만족시키는 최소 구조다. 메모리를 둘로 나눠 하나는 하드웨어 소유, 하나는 소프트웨어 소유로 두고, 인터럽트 시점마다 소유권을 맞바꾼다.

---

## 2. 먼저 그림으로 이해하기

버퍼 하나를 둘로 나눈 모습이다. `H`는 하드웨어(DMA) 소유, `S`는 소프트웨어(task) 소유를 뜻한다.

### 2.1 초기 상태 (DMA 시작 직후)

```
 pingpong_t
 +----------------------------------+----------------------------------+
 | buf[0]  (160 sample = 10 ms)     | buf[1]  (160 sample = 10 ms)     |
 |  H  DMA 가 여기를 채우는 중        |     아직 아무도 안 씀              |
 +----------------------------------+----------------------------------+
   ^
   DMA write pointer

 ready_mask = 0b00     overruns = 0
 audio_get_frame() -> NULL   (task 는 잘 수 있다)
```

### 2.2 HT 인터럽트 — 절반 0이 찼다

```
 +----------------------------------+----------------------------------+
 | buf[0]  block #0  가득           | buf[1]                           |
 |  S  task 소유                    |  H  DMA 가 지금 여기를 채운다      |
 +----------------------------------+----------------------------------+
                                      ^
                                      DMA write pointer

 ready_mask = 0b01     overruns = 0
 audio_get_frame(&half) -> &buf[0][0],  half = 0
```

소유권이 바뀐 것은 메모리를 옮겨서가 아니다. `ready_mask`의 bit 0을 세운 것뿐이다. **소유권은 비트 하나로 표현되는 약속**이고, 실제 데이터는 제자리에 있다.

### 2.3 task가 처리하는 동안 (정상)

```
 시각 t = 10 ms ~ 20 ms

 +----------------------------------+----------------------------------+
 | buf[0]  block #0                 | buf[1]  block #1 채워지는 중       |
 |  S  [FFT][MFCC][NN] 진행 중       |  H  DMA write ->->->             |
 +----------------------------------+----------------------------------+

 두 화살표가 절대 같은 칸을 가리키지 않는다. 이것이 이 구조의 전부다.
```

### 2.4 release — 소유권 반납

```
 audio_release_frame(pp, 0)

 ready_mask = 0b01  --(atomic_fetch_and ~0b01)-->  0b00

 +----------------------------------+----------------------------------+
 | buf[0]  block #0 (폐기 가능)      | buf[1]  block #1                 |
 |  H  다음 wrap 때 덮어써도 OK       |  H  DMA write ->->               |
 +----------------------------------+----------------------------------+
```

bit를 지우는 순간 그 절반은 다시 하드웨어 소유로 돌아간다. 이 시점 이후에 `pcm` 포인터를 계속 들고 읽으면 use-after-release다. C의 `free()` 후 접근과 똑같은 버그이고 증상만 다르다.

### 2.5 한 주기 전체 타임라인

```
 시간 →   0        10ms       20ms       30ms       40ms       50ms
          |---------|----------|----------|----------|----------|
 DMA 쓰기 [== h0 ==][== h1 ==][== h0 ==][== h1 ==][== h0 ==]
 IRQ                ^HT        ^TC        ^HT        ^TC        ^HT
 mask               01->00     10->00     01->00     10->00     01->00
 task               [proc h0]  [proc h1]  [proc h0]  [proc h1]
                    |<-- 이 처리는 다음 IRQ 전에 끝나야 한다 -->|
 overruns   0        0          0          0          0          0
```

deadline은 "한 절반이 채워지는 시간"이다. 여기서는 10 ms. 이것은 소프트웨어가 정한 값이 아니라 **샘플레이트와 블록 크기가 곱해 만든 물리적 상수**다. 이 예산 안에 전처리와 추론이 다 들어가야 한다.

---

## 3. 잘못된 구현부터 보기

### 3.1 순진한 버전 1 — 플래그 하나

```c
volatile int frame_ready;      /* 나쁜 예 */
volatile int which_half;

void isr(int half) { which_half = half; frame_ready = 1; }
```

```
 시간 →
 IRQ           HT(h0)                TC(h1)
 which_half    0                     1
 frame_ready   1                     1
 task                  r = which_half (=0)
                       ...스케줄러가 선점...
                                          task 재개: buf[which_half] = buf[1] 를 읽는다!
                                          그런데 그 절반은 지금 DMA 가... 아니 h0 를 쓴다
```

두 변수를 따로 쓰면 둘 사이가 찢어진다. task가 `which_half`를 읽은 뒤 인터럽트가 그 값을 바꾸면 task는 자기가 예약하지 않은 절반을 읽는다. **교훈: 상태는 한 word에 모으고 원자적으로 바꾼다.**

### 3.2 순진한 버전 2 — 원자적이지 않은 read-modify-write

```c
uint32_t ready_mask;                 /* 나쁜 예 */
void isr(uint32_t half) { ready_mask |=  (1u << half); }
void task_release(uint32_t half) { ready_mask &= ~(1u << half); }
```

Cortex-M에서 `|=`는 보통 LDR / ORR / STR 세 명령으로 쪼개진다. task가 그 가운데서 선점당하면 이렇게 된다.

```
 mask 초기값 = 0b11 (둘 다 ready, task 가 밀려 있었다)

 task:  LDR  r0, [mask]      ; r0 = 0b11
        BIC  r0, r0, #1      ; r0 = 0b10   (절반 0 반납하려는 중)
        --- 여기서 TC 인터럽트 ---
 ISR:           LDR r1,[mask] ; r1 = 0b11
                ORR r1,r1,#2  ; r1 = 0b11  (이미 서 있었다 -> overrun 정상 감지)
                STR r1,[mask] ; mask = 0b11
        --- 복귀 ---
 task:  STR  r0, [mask]      ; mask = 0b10   <-- ISR 이 쓴 값이 통째로 덮였다
```

이 경우엔 우연히 같은 값이지만, 반대 순서(ISR이 bit를 세운 직후 task가 stale 값을 쓰는 경우)에는 **ready bit가 통째로 사라진다**. 그러면 task는 그 프레임을 영원히 못 받고, 다음 인터럽트는 bit가 비어 있으니 overrun으로도 잡히지 않는다. 오디오는 조용히 10 ms씩 사라진다. 가장 나쁜 종류의 버그다. **교훈: 두 컨텍스트가 같은 word를 고치면 `atomic_fetch_or` / `atomic_fetch_and`를 쓴다.**

### 3.3 순진한 버전 3 — ISR에서 복사

```c
void isr(uint32_t half) { memcpy(work_buf, pp->buf[half], sizeof work_buf); ready = 1; }
```

```
 ISR 실행 시간
 좋은 구현:  |#|                     (수십 사이클, 원자연산 하나)
 복사 구현:  |##################|    (160 sample x 2 B = 320 B 복사)

 그동안 막혀 있는 것: 같은/낮은 우선순위 인터럽트 전부
 -> UART RX overrun, 타이머 tick 지연, BLE 연결 이벤트 미스
```

복사는 latency를 줄이지도 않는다. 데이터는 이미 SRAM에 있고 포인터만 넘기면 된다. **교훈: ISR에서 하는 일은 "표시 + 깨우기" 두 가지뿐이다.**

---

## 4. 한 줄씩 만들기

### 4.1 단계 1 — 상태를 한 구조체에 모은다

```c
typedef struct {
    int16_t          buf[HALF_COUNT][FRAME_SAMPLES];
    _Atomic uint32_t ready_mask;
    uint32_t         overruns;
} pingpong_t;
```

- `buf[2][160]`: 2차원으로 잡으면 `pp->buf[half]`가 곧 절반의 시작 주소다. 오프셋 계산 실수가 사라진다. 실제 DMA에는 이 전체(`2 x 160 x 2 B = 640 B`)를 하나의 선형 영역으로 넘긴다. C 표준이 배열을 연속으로 보장하므로 안전하다.
- `_Atomic uint32_t ready_mask`: `volatile`이 아니라 `_Atomic`이다. `volatile`은 "최적화하지 마라"일 뿐 원자성을 주지 않는다. 이 줄이 없으면 3.2절의 잃어버린 bit 버그가 난다.
- `overruns`가 plain `uint32_t`인 이유: 쓰는 쪽이 ISR 하나뿐이고 읽는 쪽은 task뿐이다(single-writer). 32-bit MCU에서 정렬된 word 하나의 load/store는 쪼개지지 않는다. 다만 컴파일러가 task 루프 안의 반복 읽기를 캐싱할 수 있으므로, 실제 제품에서 계속 폴링한다면 `_Atomic`이나 최소한 `volatile`로 두는 편이 안전하다.

### 4.2 단계 2 — ISR: bit를 세우면서 직전 값을 본다

```c
void audio_dma_isr(pingpong_t *pp, uint32_t half)
{
    uint32_t bit  = 1u << half;
    uint32_t prev = atomic_fetch_or_explicit(&pp->ready_mask, bit,
                                             memory_order_release);
    if (prev & bit) {
        pp->overruns++;
    }
}
```

- `atomic_fetch_or_explicit`: "세우기"와 "직전 값 읽기"를 한 번에 한다. 두 연산으로 쪼개면(`load` 후 `store`) 그 사이에 task가 bit를 지울 수 있고, 그러면 overrun을 놓친다. 이 함수 하나로 검출과 표시가 동시에 끝나는 것이 이 문제의 핵심 트릭이다.
- `memory_order_release`: 앞선 DMA 데이터 쓰기가 bit 세우기보다 먼저 보이도록 한다. 순서가 뒤집히면 task가 bit를 보고 들어왔는데 데이터는 아직 안 온 상태를 볼 수 있다. Cortex-M 단일 코어에서는 DMB 하나거나 아예 사라지지만, 의도를 코드에 남겨야 멀티코어로 옮겨도 산다.
- `if (prev & bit)`: bit가 이미 서 있었다 = task가 지난번 프레임을 아직 안 가져갔다 = 그 데이터는 방금 덮였다. 이 조건이 **유일한 overrun 판정**이다.
- `pp->overruns++`가 없으면: 오디오가 끊기는데 원인을 알 수 없다. 필드에서 "가끔 wake word를 놓친다"는 버그 리포트만 남는다.

### 4.3 단계 3 — task: 읽을 절반을 고른다

```c
const int16_t *audio_get_frame(pingpong_t *pp, uint32_t *half_out)
{
    uint32_t m = atomic_load_explicit(&pp->ready_mask, memory_order_acquire);
    if (m == 0u) {
        return NULL;
    }
    uint32_t half = (m & 1u) ? 0u : 1u;
    *half_out = half;
    return pp->buf[half];
}
```

- `memory_order_acquire`: ISR의 release와 짝이다. bit를 본 뒤에 읽는 샘플이 실제로 새 데이터임을 보장한다.
- `m == 0`이면 `half_out`을 건드리지 않고 나간다. 호출자가 `NULL` 체크를 잊었을 때 쓰레기 half 번호로 엉뚱한 절반을 반납하는 2차 사고를 막는다.
- `(m & 1u) ? 0u : 1u`: 가장 낮은 세워진 bit를 고른다. `m == 0b11`(둘 다 밀림)이면 0번부터. 이 선택의 의미는 6.3절에서 다시 본다.
- 반환값이 `const int16_t *`인 이유: task는 입력 오디오를 고쳐 쓸 이유가 없다. 제자리 필터링이 필요하면 별도 작업 버퍼로 복사하는 편이 안전하다.

### 4.4 단계 4 — task: 반납

```c
void audio_release_frame(pingpong_t *pp, uint32_t half)
{
    atomic_fetch_and_explicit(&pp->ready_mask, ~(1u << half),
                              memory_order_release);
}
```

- `fetch_and`로 **자기 bit만** 지운다. `atomic_store(0)`으로 통째로 지우면 그 사이 ISR이 세운 다른 절반의 bit까지 날아간다. 그 절반은 영원히 처리되지 않고 overrun으로도 안 잡힌다(3.2절과 같은 증상).
- 이미 0이어도 결과는 0이다. 그래서 두 번 불러도 안전하다.
- 이 줄이 없으면: 다음 인터럽트가 전부 overrun으로 잡히고 카운터만 올라간다. 처음 이 코드를 쓰면 실제로 가장 자주 하는 실수다.

### 4.5 단계 5 — init

```c
void pingpong_init(pingpong_t *pp)
{
    memset(pp->buf, 0, sizeof pp->buf);
    atomic_store_explicit(&pp->ready_mask, 0u, memory_order_relaxed);
    pp->overruns = 0u;
}
```

DMA를 켜기 **전에** 부른다. 순서가 뒤집히면 첫 인터럽트가 올린 bit를 init이 지워 첫 프레임을 잃는다. 버퍼를 0으로 지우는 것은 첫 프레임이 실수로 처리되더라도 쓰레기 대신 무음이 나가게 하기 위함이다.

---

## 5. 전체 코드 읽기

`solutions/02_i2s_pingpong.c`는 세 덩어리다.

- 1절 공개 인터페이스(4.1의 구조체와 프로토타입 다섯 개)와 2절 구현(4.2 ~ 4.5 그대로).
- 3절 `fake_dma_*`: 하드웨어 대역. `fake_dma_fill_half()`가 절반 하나를 채우고 `audio_dma_isr()`을 직접 부른다. 이것이 HT 또는 TC 인터럽트 한 번이다. 채워 넣는 값은 `(block_id << 8) | (i & 0xFF)`라서, 샘플 값만 보면 **몇 번째 10 ms 블록의 몇 번째 샘플인지** 바로 알 수 있다. 프레임이 찢어졌는지 판정하는 데 이 성질을 쓴다.

```c
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
```

- 4절 테스트 10개: init 상태 / 단발 HT / 정상 교대 8블록 / 소유권 분리 / overrun 검출 / overrun 의미(stale) / 백로그 배수 순서 / release idempotent / 절반 속도 소비자 / 복구.

`test_late_consumer_rate`는 인터럽트 2번마다 1프레임만 처리하는 task를 흉내내고 정확히 4번의 overrun을 기대한다. 그 4라는 숫자가 어디서 나오는지는 소스의 주석에 단계별로 적어 두었다. 외우지 말고 직접 표를 그려 따라가면 된다.

---

## 6. 동시성·메모리 관점

### 6.1 세 명의 행위자

```
        누가                무엇을 만지나              언제
 ---------------------------------------------------------------
  DMA 엔진        buf[hw_half] 에 쓴다        항상. CPU 와 무관
  ISR             ready_mask 의 bit 세움,      절반이 찰 때마다
                  overruns 증가                (선점 불가 구간)
  task            buf[sw_half] 읽고            깨어났을 때
                  ready_mask 의 bit 지움
```

DMA 엔진은 C 코드가 아니다. 컴파일러도 CPU도 그 존재를 모른다. 그래서 `volatile`이나 `_Atomic`만으로는 DMA와의 관계가 해결되지 않고, **영역 분리(누가 어느 절반을 만지는가)** 로 해결해야 한다. 원자성이 필요한 것은 ISR과 task 사이의 `ready_mask` 하나뿐이다.

### 6.2 왜 volatile로는 부족한가

```
 volatile uint32_t mask;  mask |= bit;

   LDR  r0, [mask]    <-- volatile 은 이 세 줄이 "생략되지 않음"만 보장
   ORR  r0, r0, bit       원자적으로 묶이지는 않는다
   STR  r0, [mask]

 _Atomic + fetch_or

   LDREX r0, [mask]   <-- 배타적 로드
   ORR   r1, r0, bit
   STREX r2, r1,[mask]    끼어든 쓰기가 있었으면 실패
   CBNZ  r2, retry        실패하면 다시
```

ARMv7-M(Cortex-M3/M4/M7)은 LDREX/STREX가 있어 이 루프가 인라인으로 나온다. ARMv6-M(Cortex-M0/M0+)은 없기 때문에 `atomic_fetch_or`가 라이브러리 호출이 되거나 짧게 인터럽트를 막는 코드로 바뀐다. 면접에서 타깃 코어를 물어보면 이 차이를 말하면 된다.

### 6.3 overrun 검출은 "사후"다

이 구현이 overrun을 알아차리는 시점과 데이터가 실제로 깨지는 시점은 다르다.

```
 시간 →      0        10ms          20ms                 30ms
             [== h0 ==][== h1 ==][====== h0 ======]
 IRQ                   ^HT         ^TC              ^HT
 task                  [-- proc h0 (아직 처리 중) -----------]
                                    |                  |
                                    |                  +-- 여기서 비로소 overruns++
                                    +-- 실제 오염 시작: DMA 가 h0 를 다시 쓰기 시작한 순간
                                        task 는 지금 그 위를 읽고 있다 (찢어진 프레임)
```

TC 시점(20 ms)부터 DMA는 절반 0을 다시 쓰기 시작한다. task는 그때부터 이미 찢어진 데이터를 읽는다. 그런데 코드가 그것을 아는 것은 다음 HT(30 ms)다. 10 ms 늦다.

엄밀하게 하려면 처리 직후 DMA의 남은 전송 카운트 레지스터(STM32의 `CNDTR`, `NDTR`)를 읽어 write pointer가 이미 내 절반으로 넘어왔는지 확인하고, 넘어왔으면 그 프레임을 버린다. 면접에서 이 한 문장을 덧붙이면 "DMA를 실제로 써 봤다"는 신호가 된다.

### 6.4 D-cache가 있는 코어

Cortex-M7, Cortex-A는 DMA가 쓴 SRAM을 CPU가 읽기 전에 그 영역을 invalidate해야 한다. 안 하면 CPU는 캐시에 남은 옛 데이터를 읽는다.

```
 DMA --write--> SRAM   [ 새 오디오 ]
                         ^
                         | CPU 는 여기를 안 본다
 CPU  <--read-- D-cache [ 10 ms 전 오디오 ]   <-- invalidate 안 하면 이걸 읽는다
```

- 읽기 전에 `SCB_InvalidateDCache_by_Addr(pp->buf[half], sizeof pp->buf[half])`를 부른다. 버퍼는 cache line(보통 32 B) 정렬이고 크기도 배수여야 한다. 아니면 같은 line에 걸친 옆 변수까지 무효화되어 엉뚱한 데이터가 날아간다. `160 x 2 B = 320 B`는 32의 배수지만 정렬은 `_Alignas(32)`로 못박아야 한다.
- 이 문제의 테스트는 호스트 PC에서 돌기 때문에 캐시 코드가 없다. 면접에서는 "타깃에 D-cache가 있으면 여기에 invalidate가 들어간다"고 말로 채우면 된다.

---

## 7. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `release`를 빼먹음 | overrun 카운터가 인터럽트마다 1씩 증가, 오디오는 멀쩡 | bit가 영원히 서 있어 모든 ISR이 재진입으로 보임 | 처리 끝난 절반마다 `audio_release_frame` 호출 |
| `atomic_store(&mask, 0)`으로 반납 | 가끔 프레임 하나가 통째로 사라지는데 overrun은 0 | 다른 절반의 bit까지 지움 | `atomic_fetch_and(~bit)`로 자기 bit만 |
| 비원자적 or/and 대입 연산자 사용 | 며칠에 한 번 오디오 glitch, 재현 불가 | 비원자적 RMW가 서로 덮어씀 | `_Atomic` + `fetch_or` / `fetch_and` |
| ISR에서 `memcpy` | 다른 주변장치가 인터럽트를 놓침(UART overrun 등) | ISR 실행 시간이 수백 사이클로 늘어남 | 포인터만 넘기는 zero-copy 유지 |
| DMA 크기를 byte 단위로 넘김 | 절반만 채워지거나 버퍼 밖을 침범 | HAL의 Size 인자가 데이터 폭 단위(16-bit면 half-word 개수) | RM과 HAL 주석 확인 후 단위 맞추기 |
| release 후에도 포인터 사용 | 주기적으로 프레임 뒤쪽만 이상한 값 | 반납한 절반을 DMA가 덮어쓰는 중 | release는 처리가 완전히 끝난 뒤 마지막에 |
| D-cache invalidate 누락 | M7에서만 옛 오디오가 반복 재생 | CPU가 캐시의 stale line을 읽음 | 읽기 전 invalidate, 버퍼 32 B 정렬 |
| 버퍼를 DMA 불가 영역에 둠 | DMA 에러 인터럽트 또는 전송 0 | CCM/TCM 등 그 DMA가 접근 못 하는 SRAM | 링커 스크립트로 DMA 가능 영역에 배치 |
| `half_out`을 NULL일 때도 씀 | 엉뚱한 절반이 반납되어 무작위 손실 | 호출자가 NULL 체크를 잊음 | NULL 경로에서 출력 인자를 건드리지 않음 |

---

## 8. 직접 확인하기

```
make sol N=02     # 모범답안: [ok] 10줄 + ALL TESTS PASSED
make run N=02     # 내 구현(starters/)
```

일부러 깨뜨려 볼 것 세 가지다.
1. `audio_release_frame`의 본문을 주석 처리한다. `test_alternates`가 두 번째 루프에서 죽고, overrun 카운터가 인터럽트 수만큼 올라간다. 위 표의 첫 행을 눈으로 확인하는 실험이다.
2. `audio_release_frame`을 `atomic_store_explicit(&pp->ready_mask, 0u, memory_order_release)`로 바꾼다. 단일 스레드 테스트라 대부분 통과하지만 `test_late_consumer_rate`의 overrun 수가 4에서 달라진다. bit 하나를 지우는 것과 전부를 지우는 것이 다르다는 증거다.
3. `audio_get_frame`에서 `(m & 1u) ? 0u : 1u`를 `(m & 2u) ? 1u : 0u`로 바꿔 높은 번호부터 주게 한다. `test_both_ready_order`가 깨진다. 이때 오디오 시간 순서가 어떻게 뒤집히는지 6.3절 그림과 같이 생각해 본다.

---

## 9. 면접에서 말하기

설명 순서는 이렇게 잡는다. 먼저 하드웨어 제약(마이크는 안 멈춘다), 다음 구조(버퍼 둘, 소유권은 bit), 다음 ISR이 하는 일(복사 금지, 표시만), 다음 검출(bit가 이미 서 있으면 늦은 것), 마지막에 한계(사후 검출, 캐시)를 스스로 말한다. 한계를 먼저 말하는 것이 신뢰를 준다. 그대로 쓸 영어 문장 다섯 개다.

- "The DMA runs circular over two halves, so the hardware never waits for the CPU. Ownership is just one bit per half."
- "The ISR does not copy audio. It sets the ready bit with an atomic fetch-or and returns; the task gets a pointer into the DMA buffer itself."
- "If the fetch-or tells me the bit was already set, the task missed its deadline and that block is gone, so I count an overrun instead of processing a torn frame."
- "Note that this detects the overrun one half-period late. The corruption actually starts when the DMA wraps, so if I need to be exact I read the remaining-transfer count register and check whether the write pointer already crossed into my half."
- "The deadline is not a tuning knob. With 16 kHz and a 160-sample block it is exactly 10 milliseconds, and the whole front end plus inference has to fit inside it."

화이트보드에는 이 순서로 그린다.

1. 가로로 긴 상자 하나를 그리고 가운데를 잘라 `half0`, `half1`이라고 쓴다.
2. 위쪽에 DMA write pointer 화살표, 아래쪽에 task read 화살표를 그린다. 둘이 다른 칸을 가리키는 것을 강조한다.
3. 오른쪽에 시간 축을 그리고 HT, TC를 찍는다.
4. 각 시점의 `ready_mask` 값을 `01`, `10`, `11`로 적는다.
5. 마지막으로 task 처리 막대가 다음 IRQ를 넘어가는 그림을 그리고 `11`에 동그라미를 친 뒤 `overrun++`라고 쓴다.

---

## 10. follow-up 답안

### 10.1 "Overrun happens. Do you drop the new audio or keep the stale frame?"

이 구현은 선택의 여지가 없다. **DMA는 멈추지 않으므로 옛 데이터가 이미 덮였다.** 버퍼에는 최신 블록이 들어 있고 잃은 것은 오래된 블록이다. `test_overrun_is_stale_not_dropped`가 이것을 직접 확인한다. 즉 정책은 "가장 오래된 것을 버린다"가 자동으로 적용된다.

선택할 수 있는 것은 그 다음 정책이다.

- **최신으로 리싱크**: 밀린 bit를 전부 지우고 다음 프레임부터 새로 시작한다. wake word처럼 최신성이 중요한 경로에 맞다. 시간 순서가 뒤집힌 프레임을 모델에 넣지 않는 것이 핵심이다.
- **순서대로 다 처리**: 녹음이나 스트리밍 업로드처럼 연속성이 중요하면 밀린 것도 순서대로 처리한다. 대신 지연이 누적되므로 버퍼가 더 필요하다.
- **무음 삽입**: 잃은 구간을 0이나 직전 프레임의 페이드아웃으로 채워 다운스트림의 블록 인덱스를 맞춘다.
- 어느 쪽이든 카운터는 telemetry로 올린다. 사용자에게 보이지 않는 실패를 숫자로 남기는 것이 핵심이다.

### 10.2 "Two buffers are not enough because inference time is bursty."

버퍼 2개는 지터 여유가 0이다. 한 프레임이라도 늦으면 바로 손실이다. N개 블록 큐로 바꾼다.

```
 double buffer (N=2)         block queue (N=4)
 [h0][h1]                    [b0][b1][b2][b3]  <- DMA 가 순환하며 채움
  ^   ^                        ^        ^
  DMA task                    DMA      task (최대 2블록 뒤처져도 안전)

 여유 지터 = 0                여유 지터 = (N-2) x 블록시간 = 20 ms
 SRAM = 640 B                 SRAM = 1280 B
 추가 latency = 0             추가 latency = 최악의 경우 (N-2) x 10 ms
```

구현은 `ready_mask`를 N bit로 넓히고 쓰기/읽기 인덱스를 두면 된다(문제 01의 SPSC 링버퍼와 같은 구조가 된다). Zephyr의 memory slab이나 `k_msgq`로 블록 포인터를 넘기는 방식이 흔하다. 대가는 SRAM과 latency이고, 얻는 것은 추론 시간의 분산을 흡수하는 능력이다. 평균 처리 시간이 블록 시간을 넘으면 어떤 N으로도 해결되지 않는다는 점도 같이 말한다.

### 10.3 "Your DMA controller has no half-transfer interrupt."

두 가지 길이 있다.

- **디스크립터/리스트 모드**: 버퍼 2개를 각각 별도 전송으로 등록하고 전송마다 완료 인터럽트를 받는다. nRF52의 I2S는 `RXPTRUPD` 이벤트마다 다음 버퍼 포인터를 미리 써 넣고, ESP32의 I2S는 DMA 디스크립터 링크드 리스트를 쓴다. 이름만 다르고 개념은 같다.
- **소프트웨어 핑퐁**: TC 인터럽트에서 다음 버퍼 주소를 다시 프로그래밍한다. ISR 지연이 곧 샘플 손실이므로 우선순위를 높게 잡는다. 주변장치 FIFO 깊이가 유예 시간을 정한다.
- 어느 쪽이든 상위 API 세 개는 그대로 두고 드라이버 계층만 바꾼다. 이 분리가 이 설계의 이점이다.

### 10.4 "How do you wake the task, and what does it cost?"

- FreeRTOS: ISR에서 `xTaskNotifyFromISR(handle, 1u << half, eSetBits, &woken)`, 복귀 직전 `portYIELD_FROM_ISR(woken)`. task는 `xTaskNotifyWait()`로 블록한다. notification은 큐보다 빠르고 task마다 word 하나만 쓴다. Zephyr면 `k_sem_give()`나 `k_msgq_put()`이고 둘 다 ISR-safe다.
- 비용: notify와 컨텍스트 스위치는 Cortex-M4 기준 수 마이크로초로, 10 ms 예산의 0.1% 이하다. 진짜 비용은 **우선순위 역전**이다. 오디오 task보다 높은 우선순위의 긴 작업(플래시 쓰기, 크립토)이 deadline을 먹는다.
- 이 문제의 테스트에는 RTOS가 없어 `main`이 스케줄러 역할을 하고 `ready_mask`가 notification을 대신한다. 실제로 notification을 쓰면 mask 없이 half 번호만 값으로 넘겨도 된다.

### 10.5 "How do you pick the block size for a battery-powered always-on mic?"

16 kHz mono 16-bit 기준이다. SRAM은 절반 둘을 합한 값이다.

| 블록(샘플) | 블록 시간 | IRQ/초 | 버퍼 SRAM | 최소 latency | 전력·CPU 관점 |
|---|---|---|---|---|---|
| 32 | 2 ms | 1000 | 128 B | 2 ms | 웨이크업이 너무 잦다. 진입/복귀 오버헤드가 실제 일보다 커진다 |
| 80 | 5 ms | 400 | 320 B | 5 ms | 저지연 경로(에코 제거, 실시간 모니터링)에 적합 |
| 160 | 10 ms | 200 | 640 B | 10 ms | 음성 프레임의 표준 크기. wake word 전처리와 딱 맞는다 |
| 320 | 20 ms | 100 | 1280 B | 20 ms | 웨이크업 절반으로 감소. 지연 20 ms는 대부분 허용 |
| 800 | 50 ms | 40 | 3200 B | 50 ms | 배터리에 유리하지만 응답이 굼뜨게 느껴지기 시작 |
| 1600 | 100 ms | 20 | 6400 B | 100 ms | 로깅·업로드용. 대화형 응답에는 부적합 |

읽는 법은 이렇다.

- **latency는 블록 시간 이상이 될 수 없다.** 첫 샘플은 블록이 다 찰 때까지 기다린다. 여기에 처리 시간과 큐 대기가 더해진다.
- **전력은 웨이크업 횟수에 붙는다.** MCU가 sleep에서 깨어나 클럭이 안정되고 캐시가 다시 더워지는 고정 비용이 매번 든다. 블록을 2배로 키우면 그 고정 비용이 절반이 된다. 반면 DMA가 SRAM에 쓰는 전력은 블록 크기와 무관하게 샘플레이트에만 비례한다.
- **알고리즘이 크기를 정해 줄 때가 많다.** MFCC가 25 ms 창에 10 ms hop을 쓴다면 블록을 10 ms로 맞추는 것이 자연스럽다. 블록과 hop이 어긋나면 중간 버퍼와 복사가 추가된다.
- **실무 결론**: 알고리즘 hop에 맞추고, 전력이 모자라면 블록을 키우는 대신 N개 큐로 지터를 흡수한 뒤 CPU를 더 깊은 sleep에 보낸다. 결정 근거는 IRQ/초, 예산 대비 처리 시간, overrun 카운터 셋이고 측정 없이 숫자를 고르지 않는다.

---

## 11. 요약 & 체크리스트

- ping-pong의 본질은 메모리 이동이 아니라 **소유권 표시**다. bit 하나가 "이 절반은 소프트웨어 것"을 뜻한다.
- ISR은 복사하지 않는다. `atomic_fetch_or` 한 번으로 표시와 overrun 검출을 동시에 한다. 반납은 `atomic_fetch_and`로 자기 bit만 지운다. 통째로 지우면 다른 절반의 프레임이 조용히 사라진다.
- overrun은 "새 데이터를 버리는" 것이 아니라 "옛 데이터를 잃는" 것이다. 버퍼에는 항상 최신이 들어 있다.
- 검출은 반 주기 늦다. 정확히 하려면 DMA의 남은 전송 카운트를 읽는다.
- deadline은 샘플레이트와 블록 크기가 정한 물리 상수다. 소프트웨어가 협상할 수 없다.

- [ ] `make sol N=02`가 `ALL TESTS PASSED`를 찍는가
- [ ] 내 `audio_dma_isr`에 루프도 복사도 없는가
- [ ] 반납이 `fetch_and`인가, 통째 store가 아닌가
- [ ] `NULL`을 반환할 때 `half_out`을 건드리지 않는가
- [ ] 두 절반이 동시에 ready일 때의 정책을 말로 설명할 수 있는가
- [ ] 블록 크기와 latency, IRQ 빈도, SRAM의 관계를 표 없이 그릴 수 있는가
- [ ] D-cache 코어에서 무엇을 추가해야 하는지 말할 수 있는가
