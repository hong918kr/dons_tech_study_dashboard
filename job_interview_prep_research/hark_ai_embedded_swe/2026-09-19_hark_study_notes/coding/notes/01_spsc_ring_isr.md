# 01. ISR-safe SPSC ring buffer — 해설

> **문제**: [problems/01_spsc_ring_isr.md](../problems/01_spsc_ring_isr.html) · **답안**: `solutions/01_spsc_ring_isr.c`
> **이 노트를 다 읽으면**: (1) head/tail의 writer를 한 명으로 고정하는 것이 왜 lock을 대신하는지 그림으로 설명할 수 있고, (2) free-running index와 2의 거듭제곱 마스킹이 full/empty 구분과 wrap을 동시에 해결하는 과정을 손으로 그릴 수 있고, (3) `volatile`로는 부족하고 release/acquire가 필요한 이유를 깨지는 인터리빙까지 들어 설명할 수 있다.

---

## 0. 한 문장으로

`head`는 producer만 쓰고 `tail`은 consumer만 쓰게 만들면 두 index 모두 writer가 한 명뿐이라 경쟁이 사라지고, index를 자르지 않고 계속 증가시키면서 접근할 때만 `& (RB_SIZE-1)`로 마스킹하면 wrap과 full/empty 구분이 공짜로 따라온다.

---

## 1. 왜 이 자료구조/패턴이 필요한가 — 하드웨어에서 출발

UART로 초당 115200 bit가 들어오면 한 바이트가 약 87 us마다 도착한다. UART 주변장치의 RX FIFO는 보통 1~16 바이트라, 이 FIFO가 차기 전에 CPU가 꺼내지 않으면 overrun 에러가 나고 바이트가 사라진다. 그런데 main loop는 센서를 읽고 DSP를 돌리고 BLE 스택을 처리하느라 수 ms 동안 돌아오지 않을 수 있다. 87 us와 수 ms 사이의 간극을 메우는 물건이 필요하고, 그게 ring buffer다.

```
  Companion SoC   UART    MCU
  +-----------+  115200  +--------------+  IRQ   +------------------+
  | audio/AI  |--------->| RX FIFO (8B) |------->| UART RX ISR      | producer
  +-----------+          +--------------+        | rb_put()  87 us  |
                                                 +--------+---------+
                                                          v
                                                 +------------------+
                                                 | ringbuf_t (256B) | 완충 지대
                                                 +--------+---------+
                                                          v
                                                 +------------------+
                                                 | main loop 파서   | consumer
                                                 | rb_get()   수 ms |
                                                 +------------------+
```

같은 구조가 기기 안에서 반복된다. 오디오 기기는 I2S/PDM DMA가 채운 샘플 블록을 처리 task에 넘길 때, 로그는 아무 문맥에서나 불리는 `log_write()`와 낮은 우선순위 flush task 사이에서, 센서는 FIFO watermark 인터럽트가 긁어 온 샘플을 모아 둘 때 쓴다. SSD/NVMe의 submission/completion queue와 doorbell도 같은 원리다. 그래서 이 문제가 임베디드 코딩 면접 1순위다. 자료구조 자체는 쉽고, 면접관이 실제로 보는 것은 **동시성 추론**이다. 핵심 비대칭을 먼저 못 박자.

- producer(ISR)는 **기다릴 수 없다**. full이면 블로킹이 아니라 드롭이다. ISR이 스핀하면 다른 인터럽트가 막히고 시스템이 죽는다.
- producer는 consumer를 **언제든 선점한다**. consumer가 `rb_get` 한가운데 있어도 ISR이 끼어든다. 반대는 없다.

---

## 2. 먼저 그림으로 이해하기

`RB_SIZE == 8`로 줄여서 그린다. 위 줄은 배열 index, 그 아래는 내용, 그 아래에 `H`(head 물리 위치)와 `T`(tail 물리 위치)를 찍고, 오른쪽에 free-running index의 실제 값을 적는다.

**(1) 초기 상태와 첫 세 번의 put.** `head == tail == 0`이면 `count = 0 - 0 = 0`, 비어 있다. 여기서 `rb_put('A')`, `rb_put('B')`, `rb_put('C')`를 한다. 매번 `buf[head & 7]`에 **데이터를 먼저 쓰고** 그 다음에 head를 올린다. 이 순서가 이 문제의 전부다.

```
  idx : 0  1  2  3  4  5  6  7
  buf : A  B  C  .  .  .  .  .          head = 3
        ^        ^                      tail = 0
        T        H                      count = 3 - 0 = 3
```

**(2) `rb_get(&x)`.** `x`에 `buf[0 & 7]`인 `'A'`를 먼저 복사하고 그 다음에 tail을 1로 올린다. 여기서도 데이터가 먼저, index가 나중이다. 배열의 `'A'`는 지우지 않는다. 의미 없는 쓰기이고, tail이 지나간 칸은 그냥 "producer가 다시 써도 되는 칸"이 된다.

```
  idx : 0  1  2  3  4  5  6  7
  buf : A  B  C  .  .  .  .  .          head = 3
           ^     ^                      tail = 1
           T     H                      count = 3 - 1 = 2
             (A는 남아 있지만 죽은 값)
```

**(3) 물리적 wrap과 full.** 계속 넣어 head가 8이 되면 `8 & 7 == 0`이라 물리적으로 배열 처음으로 돌아온다. free-running index는 8인데 물리 위치는 0이다. 이 둘을 구분해서 보는 것이 이 구현을 이해하는 전부다. 한 번 더 넣어 `buf[9 & 7]`까지 채우면 `count == 9 - 1 == 8 == RB_SIZE`, 즉 full이다.

```
  idx : 0  1  2  3  4  5  6  7
  buf : I  B  C  D  E  F  G  H          head = 9   (물리 1)
           ^                            tail = 1   (물리 1)
           TH                           count = 9 - 1 = 8 = FULL

  비교:  empty 는 head = 1, tail = 1  ->  차이 0
         full  은 head = 9, tail = 1  ->  차이 8
         물리 위치는 둘 다 1 로 같고, 잘리지 않은 index 의 차이만 다르다.
```

여기가 결정적인 대목이다. **full일 때도 empty일 때도 `H`와 `T`의 물리 위치가 같다.** 그래서 index를 잘라 버리는 흔한 구현은 한 칸을 비워 두고 `RB_SIZE-1`개만 쓴다. free-running index가 있으면 그럴 필요가 없다.

**(4) index 자체의 wrap.** `head`가 `0xFFFFFFFF`를 넘어가면 0이 된다. 그래도 `head - tail`은 정확하다. unsigned 산술이 modulo 2^32로 정의돼 있기 때문이다.

```
  head = 0xFFFFFFFE, tail = 0xFFFFFFFA  ->  count = 4
  put 을 세 번 더 하면 head = 0x00000001  (0xFFFFFFFF 를 넘어감)
      count = 0x00000001 - 0xFFFFFFFA
            = 0x100000001 - 0xFFFFFFFA   (2^32 를 빌려온다)  =  7   (정확)
```

115200 baud로 하루 종일 받아도 32-bit index가 한 바퀴 도는 데 4.3일이 걸리고, 한 바퀴 돌아도 위처럼 문제가 없다.

**(5) `rb_write`가 쪼개지는 모습.** head=tail=6인 빈 버퍼에 5바이트를 쓰면 배열 끝을 넘는다. 연속으로 쓸 수 있는 길이는 `RB_SIZE - (head & 7) = 8 - 6 = 2`이므로 memcpy를 두 번 한다.

```
  src = [p q r s t]
  1차 memcpy: &buf[6] <- "p q"      (first = 2)
  2차 memcpy: &buf[0] <- "r s t"    (n - first = 3)

  idx : 0  1  2  3  4  5  6  7
  buf : r  s  t  .  .  .  p  q      head = 11  (마지막에 한 번만 6 -> 11)
        ^        ^        ^         tail = 6
        |        H        T         count = 11 - 6 = 5
        (물리적으로는 뒤에서 앞으로 이어진다)
```

head를 바이트마다 올리지 않고 **마지막에 한 번만** 올리는 것이 핵심이다. 중간에 올리면 consumer가 아직 다 쓰지도 않은 영역을 유효 데이터로 읽는다.

---

## 3. 잘못된 구현부터 보기

### 3.1 순진한 버전: 공유 `count`

교과서 ring buffer는 보통 이렇게 생겼다.

```c
/* 깨지는 코드 — 절대 이렇게 쓰지 말 것 */
typedef struct {
    uint8_t  buf[RB_SIZE];
    uint32_t head, tail;
    uint32_t count;      /* <-- producer 도 쓰고 consumer 도 쓴다 */
} bad_rb_t;

bool bad_put(bad_rb_t *rb, uint8_t b)      /* ISR에서 호출 */
{
    if (rb->count == RB_SIZE) return false;
    rb->buf[rb->head] = b;
    rb->head = (rb->head + 1u) % RB_SIZE;
    rb->count++;                            /* read-modify-write */
    return true;
}

/* bad_get 은 대칭이다. tail 을 올리고 count-- 를 한다 (역시 RMW). */
```

`count`를 두면 full과 empty를 구분하기 쉽다. 그런데 `count`는 **두 문맥이 모두 쓰는 변수**이고, `count--`는 하나의 연산처럼 보이지만 기계어로는 `LDR` / `SUBS` / `STR` 세 단계다. ISR은 그 사이 어디에서든 끼어든다. 시작 상태 `count == 5`에서의 인터리빙이다.

```
  시간  main loop (bad_get)              UART ISR (bad_put)           메모리의 count
  ----  ------------------------------   --------------------------   --------------
   t0   LDR r0, [count]   -> r0 = 5                                         5
   t1   SUBS r0, r0, #1   -> r0 = 4                                         5
   t2        --- 인터럽트 발생, main loop 선점당함 ---
   t3                                    LDR r1, [count] -> r1 = 5          5
   t4                                    ADDS r1, r1, #1 -> r1 = 6          5
   t5                                    STR r1, [count]                    6
   t6                                    (buf 에 1바이트 써 둠)             6
   t7        --- ISR 리턴, main loop 재개 ---
   t8   STR r0, [count]                                                     4   <-- !!
```

`t8`에서 main loop가 레지스터에 들고 있던 4를 그대로 덮어쓴다. ISR이 만든 6이 사라졌고, 실제로는 5개가 들어 있는데 `count`는 4라고 말한다. **한 바이트가 영원히 유실된다.** 게다가 오차가 누적된다. 1000번 중 한 번만 일어나도 장시간 돌리면 `count`는 계속 작아지다 0에 도달하고, `bad_get`이 영원히 empty를 돌려주면서 파서가 멈춘다. 필드에서는 "몇 시간 돌리면 UART가 먹통이 된다, 리셋하면 멀쩡하다"로 보고되고, 디버거를 붙여 멈추면 타이밍이 바뀌어 사라진다. 가장 비싼 종류의 버그다.

**교훈 1: 두 문맥이 모두 쓰는 변수를 만들지 마라.** `count`를 없애고 `head - tail`로 계산하면 이 경쟁 자체가 존재하지 않는다.

### 3.2 두 번째 실수: `% RB_SIZE`로 index를 잘라 버리기

`count`를 없애고 `head`, `tail`만 남겼다고 하자. 그런데 `rb->head = (rb->head + 1u) % RB_SIZE;`처럼 index를 매번 잘라 버리면 `head == tail`이 empty도 되고 full도 된다. 2장 그림 (3)이 그대로 이 문제다. 보통 한 칸을 비워 두는 것으로 우회한다.

```
  한 칸 비우는 방식:  full 조건 = ((head + 1) & (RB_SIZE-1)) == tail
  idx : 0  1  2  3  4  5  6  7
  buf : I  B  C  D  E  F  G  .      head = 7, tail = 0 (둘 다 물리 위치)
        T                       H   idx 7 은 영원히 안 쓴다 -> 용량 7/8
```

동작은 한다. 하지만 256바이트 버퍼가 255바이트가 되고, `% RB_SIZE`는 크기가 2의 거듭제곱이 아니면 나눗셈 명령을 부른다. Cortex-M0/M0+에는 하드웨어 나눗셈기가 없어 컴파일러가 `__aeabi_uidivmod` 호출을 넣고, ISR 안에서 수십 사이클이 날아간다. **교훈 2: index를 자르지 말고 free-running으로 두고, 접근할 때만 마스킹하라.** 그러면 용량 전부를 쓰고, 나눗셈도 사라지고, `head - tail`이 곧 개수가 된다.

### 3.3 세 번째 실수: 순서가 뒤집히는 것 — `volatile`로는 못 막는다

구조는 맞다고 하자(`count` 없음, free-running index, 마스킹). 그런데 이렇게 썼다.

```c
/* buf[] 는 volatile 이 아니고, head/tail 만 volatile 이다 */
bool weak_put(weak_rb_t *rb, uint8_t b)
{
    uint32_t h = rb->head, t = rb->tail;
    if ((uint32_t)(h - t) == RB_SIZE) return false;
    rb->buf[h & (RB_SIZE - 1u)] = b;    /* (A) 일반 변수 쓰기 */
    rb->head = h + 1u;                  /* (B) volatile 쓰기 */
    return true;
}
```

`volatile`은 "이 변수 접근을 지우지도, 합치지도, 레지스터로 대체하지도 마라"만 약속한다. **일반 변수와의 상대적 순서는 약속하지 않는다.** 표준은 volatile 접근끼리의 순서만 보장하고 `buf[]`는 volatile이 아니므로, 컴파일러는 (A)와 (B)를 바꿔도 된다. 버퍼가 비어 있고 head=tail=0인 상태에서 순서가 뒤집혔을 때의 인터리빙이다.

```
  시간  UART ISR (weak_put, 재배치됨)        main loop (weak_get)
  ----  ---------------------------------   ---------------------------------
   t0   rb->head = 1            (B 먼저!)
   t1        --- ISR 이 여기서 끝나거나, 더 높은 우선순위 IRQ 에 선점됨 ---
   t2                                       t=0, h=1  ->  h != t 이므로 비지 않음
   t3                                       *out = buf[0]  <-- 아직 안 쓴 칸!
   t4                                       rb->tail = 1
   t5   rb->buf[0] = b          (A 나중)                 <-- 너무 늦었다
```

`t3`에서 consumer가 읽은 것은 정확히 한 바퀴 전의 죽은 바이트다(2장 그림 (2)에서 읽은 칸을 지우지 않았으니까). 패킷 파서 입장에서는 멀쩡해 보이는 바이트라 CRC가 깨지기 전까지 알아채지도 못한다. 같은 일이 consumer 쪽에서도 일어난다. tail 갱신이 데이터 읽기보다 먼저 보이면 producer가 아직 읽지도 않은 칸을 덮어쓴다.

consumer가 `tail`을 먼저 올리고 `*out = buf[...]`를 나중에 하면, 그 사이에 끼어든 ISR이 공간이 생긴 줄 알고 그 칸을 새 바이트로 덮어쓴다. 원래 읽어야 했던 오래된 바이트가 사라지고 최신 바이트가 두 번 나온다.

**교훈 3: 필요한 것은 "접근을 지우지 마라"가 아니라 "두 접근의 순서를 유지하라"다.** 그게 release/acquire이고 C11에서는 `_Atomic` + `memory_order_release` / `memory_order_acquire`로 표현한다.

---

## 4. 한 줄씩 만들기

### 4.1 단계 1 — 자료구조에서 경쟁을 없앤다 (두 문맥이 함께 쓰는 변수 제거)

```c
#define RB_SIZE 256u
_Static_assert((RB_SIZE & (RB_SIZE - 1u)) == 0u, "RB_SIZE must be power of 2");

typedef struct {
    uint8_t buf[RB_SIZE];
    _Atomic uint32_t head;    /* producer(ISR)만 store 한다 */
    _Atomic uint32_t tail;    /* consumer(task)만 store 한다 */
    uint32_t         dropped; /* full 때문에 버린 바이트. producer만 만진다 */
} ringbuf_t;
```

- `_Static_assert`가 "2의 거듭제곱" 전제를 컴파일 타임에 강제한다. 없으면 마스킹이 조용히 틀린 index를 만들고 버퍼는 쓰레기를 뱉으면서 동작하는 척한다.
- `_Atomic uint32_t`는 tearing 방지와 `memory_order`를 붙일 자리를 준다. Cortex-M에서 정렬된 32-bit 접근은 원래 single-copy atomic이라 전자는 공짜고, 우리가 실제로 사는 것은 후자다. `dropped`는 producer만 읽고 쓰므로 `_Atomic`이 아니다.
- 주석의 "만 store 한다"가 이 구현의 계약 전체다. 이 줄을 어기면 3.1의 `count` 경쟁이 그대로 돌아온다.

### 4.2 단계 2 — 개수를 뺄셈으로 구한다 (full/empty를 저장하지 말고 계산)

```c
static inline uint32_t rb_count(const ringbuf_t *rb)
{
    uint32_t h = atomic_load_explicit(&rb->head, memory_order_acquire);
    uint32_t t = atomic_load_explicit(&rb->tail, memory_order_acquire);
    return h - t;
}

static inline uint32_t rb_space(const ringbuf_t *rb)   { return RB_SIZE - rb_count(rb); }
static inline bool     rb_is_empty(const ringbuf_t *rb){ return rb_count(rb) == 0u; }
static inline bool     rb_is_full(const ringbuf_t *rb) { return rb_count(rb) == RB_SIZE; }
```

- `h - t`에 캐스팅도 조건문도 없다. `uint32_t` 뺄셈이 modulo 2^32라서 2장 그림 (4)의 index wrap이 자동으로 처리된다. `if (h < t)` 같은 보정을 넣는 순간 오히려 틀린다.
- 저장된 `count` 필드가 없으니 3.1의 lost update가 구조적으로 불가능하다. 돌려주는 값은 **호출한 쪽 기준의 안전한 추정치**다. producer가 부르면 "공간이 적어도 이만큼"이 보장되고(consumer는 tail을 올리기만 하니 공간은 늘기만 한다), consumer가 부르면 "적어도 이만큼은 읽을 수 있다"가 보장된다. 양쪽 다 자기에게 불리한 쪽으로만 틀린다.

### 4.3 단계 3 — producer: 데이터 먼저, index 나중 (3.3의 재배치를 막는다)

```c
static inline bool rb_put(ringbuf_t *rb, uint8_t b)
{
    uint32_t h = atomic_load_explicit(&rb->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&rb->tail, memory_order_acquire);
    if ((uint32_t)(h - t) == RB_SIZE) {
        rb->dropped++;
        return false;
    }
    rb->buf[h & (RB_SIZE - 1u)] = b;
    atomic_store_explicit(&rb->head, h + 1u, memory_order_release);
    return true;
}
```

- `head`는 나만 쓰는 값이라 `relaxed`로 읽는다. 내가 마지막에 쓴 값을 내가 읽는 것이라 동기화가 끼어들 여지가 없고, acquire를 붙이면 효과 없이 배리어 비용만 낸다.
- `tail`은 상대가 쓴 값이라 `acquire`로 읽는다. acquire load는 "뒤의 접근이 이 load보다 앞으로 옮겨지지 않는다"를 보장하므로, consumer가 tail을 올리기 전에 끝낸 데이터 읽기가 내 눈에 보이고 그 칸을 덮어써도 안전해진다.
- full 검사는 `>=`가 아니라 `==`로 충분하다. `head`가 `tail`을 앞지르려면 full인데도 put이 성공해야 하는데 바로 이 검사가 그걸 막기 때문이다. `==`가 성립한다는 사실 자체가 불변식이다.
- `rb->buf[...] = b;`가 release store보다 **위에** 있다는 것이 이 함수의 전부다. `memory_order_release`는 "이 store 앞의 모든 접근이 이 store보다 먼저 보인다"이고, consumer의 acquire load와 짝을 이뤄 release-acquire 쌍이 된다. relaxed로 바꾸면 3.3의 첫 타임라인이 그대로 재현된다.
- 루프가 없다. 분기 하나, 마스킹(2의 거듭제곱이라 `AND` 한 번) 하나, store 두 개. ISR에 적합하다.

### 4.4 단계 4 — consumer: producer의 거울상

```c
static inline bool rb_get(ringbuf_t *rb, uint8_t *out)
{
    uint32_t t = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&rb->head, memory_order_acquire);
    if (h == t) {
        return false;
    }
    *out = rb->buf[t & (RB_SIZE - 1u)];
    atomic_store_explicit(&rb->tail, t + 1u, memory_order_release);
    return true;
}
```

- 역할이 정확히 뒤집혔다. 내 변수(`tail`)는 relaxed, 상대 변수(`head`)는 acquire, 내 index 갱신은 release.
- 비교는 반드시 **이미 읽어 둔 지역 변수**로 한다. 여기서 `rb_count()`를 다시 부르면 head/tail을 한 번 더 읽게 되고, 그 사이 ISR이 끼어들어 검사한 값과 아래에서 쓰는 값이 달라진다.
- empty일 때 `*out`을 건드리지 않는 것은 계약이다. 호출부가 실패 시 이전 값을 유지하는 패턴을 쓸 수 있어야 한다.
- `*out = buf[...]`가 release store보다 위에 있다. 없으면 producer가 아직 안 읽은 칸을 덮어쓰는 3.3의 두 번째 경합이 생긴다. 읽은 칸을 0으로 지우지 않는 것은 의미 없는 쓰기이기 때문이고, 민감한 데이터라면 지울 이유가 생기지만 그건 별개의 요구사항이다.

### 4.5 단계 5 — bulk: 바이트마다가 아니라 마지막에 한 번만 publish

```c
static inline uint32_t rb_write(ringbuf_t *rb, const uint8_t *src, uint32_t n)
{
    uint32_t h    = atomic_load_explicit(&rb->head, memory_order_relaxed);
    uint32_t t    = atomic_load_explicit(&rb->tail, memory_order_acquire);
    uint32_t free = RB_SIZE - (uint32_t)(h - t);
    if (n > free) { rb->dropped += (n - free); n = free; }
    if (n == 0u)  { return 0u; }

    uint32_t off   = h & (RB_SIZE - 1u);   /* 물리 위치 */
    uint32_t first = RB_SIZE - off;        /* 끝까지 연속 길이 */
    if (first > n) { first = n; }
    memcpy(&rb->buf[off], src, first);
    if (n > first) { memcpy(&rb->buf[0], src + first, n - first); }

    atomic_store_explicit(&rb->head, h + n, memory_order_release);
    return n;
}
```

- `free`로 `n`을 먼저 잘라 부분 성공을 허용한다. UART 드라이버에는 "8바이트 중 3바이트만 들어갔다"가 "실패"보다 유용하다.
- `off`는 물리 위치, `first`는 배열 끝까지의 연속 길이다(2장 그림 (5)). `if (first > n) first = n;`을 빼먹으면 두 번째 memcpy의 길이 `n - first`가 unsigned로 wrap 해 거대한 값이 되고 메모리를 밟는다.
- memcpy는 최대 두 번이다(`n <= RB_SIZE`라 배열을 한 바퀴 넘게 돌 수 없다). release store가 딱 하나뿐인 것이 bulk 버전의 존재 이유다. 256바이트를 `rb_put` 256번으로 넣으면 ARMv7-M에서 `DMB`가 256번 나갈 수도 있다. `rb_read`는 거울상이라 `tail`을 relaxed, `head`를 acquire로 읽고, memcpy 방향만 반대로 한 뒤 마지막에 `tail += n`을 release로 store 한다.

---

## 5. 전체 코드 읽기

`solutions/01_spsc_ring_isr.c`는 1절 자료구조, 2절 `rb_init`, 3절 관찰 함수(`rb_count` / `rb_space` / `rb_is_empty` / `rb_is_full`), 4절 producer(`rb_put` / `rb_write`), 5절 consumer(`rb_get` / `rb_read`), 6절 별칭(`rb_push` / `rb_pop`), 7절 테스트 순서다. 4장에서 만든 조각이 그대로 들어가 있다.

`rb_init`은 `atomic_init`으로 head/tail/dropped를 0으로 만들고 `buf[]`는 건드리지 않는다. `atomic_init`은 `atomic_store`와 달리 **다른 문맥이 아직 이 객체를 보지 못한 시점에만** 쓰는 비원자적 초기화다. 그래서 `rb_init`은 인터럽트를 켜기 전에, 즉 producer가 존재하기 전에 불려야 한다. 이미 ISR이 돌고 있는 버퍼를 리셋하는 것은 그 자체가 경쟁이다. `rb_push` / `rb_pop`은 S01 드릴의 `rb_put` / `rb_get`을 문제 지문의 이름으로도 부를 수 있게 한 얇은 별칭이라 동작이 완전히 같다.

테스트 9개가 각각 무엇을 잡는지.

| 테스트 | 잡아내는 버그 |
|--------|---------------|
| `test_init_empty` | 초기화 누락, empty일 때 `out`을 건드리는 구현 |
| `test_fifo_order` | LIFO로 잘못 만든 구현, 별칭 연결 오류 |
| `test_full_behaviour` | 한 칸 비워 두기(용량 255), full일 때 덮어쓰기, `dropped` 미증가 |
| `test_wrap_around_buffer` | 마스킹 실수, 버퍼 끝에서의 off-by-one |
| `test_index_overflow` | `h - t`에 부호 있는 비교나 `if (h < t)` 보정을 넣은 구현 |
| `test_bulk_write_read` | 공간보다 많이 요청했을 때의 오버런, `dropped` 누락 |
| `test_bulk_split_at_wrap` | `first = n` 클램프 누락, 두 번째 memcpy 길이 계산 오류 |
| `test_interleaved_producer_consumer` | 속도가 다른 두 문맥에서의 누락·중복(1000라운드) |
| (`test_index_overflow` 는 테스트 전용으로 head/tail 을 `0xFFFFFFF8` 로 직접 옮긴다) | 4.3일을 기다리지 않고 2^32 경계를 검증하는 유일한 방법 |

---

## 6. 동시성·메모리 관점

### 6.1 왜 lock이 필요 없는가 — 소유권 그림

```
                store(쓰기)          load(읽기)
  head          producer ONLY        producer(relaxed) / consumer(acquire)
  tail          consumer ONLY        consumer(relaxed) / producer(acquire)
  buf[head..]   producer ONLY        (아직 게시되지 않은 빈 칸)
  buf[tail..]   consumer ONLY        (게시된 데이터 칸)
```

lock이 막아 주는 것은 "두 문맥이 같은 위치에 동시에 쓰는 일"과 "RMW가 쪼개지는 일"인데, 이 구현에는 둘 다 없다.

- store 하는 문맥이 둘인 변수가 하나도 없다. 따라서 3.1의 lost update가 불가능하다. `head = h + 1`은 RMW처럼 보이지만 `h`는 내가 아까 읽은 내 값이고 그 사이 아무도 `head`를 바꾸지 않으므로 단순 store다. 반면 `count--`는 상대도 쓰는 값을 읽어 고치는 진짜 RMW였다.
- `buf[]`도 영역이 겹치지 않는다. producer는 head가 가리키는 빈 칸만, consumer는 tail이 가리키는 채워진 칸만 만지고, 그 경계는 각자 자기 것만 올리는 index가 정한다. 남는 문제는 **순서**뿐이고, 그건 release/acquire가 해결한다.

### 6.2 release-acquire 쌍이 하는 일

```
  producer (ISR)                          consumer (main loop)
  ----------------------                  --------------------------
  buf[h & MASK] = b;     ----+  이 쓰기가 아래보다 먼저 보인다
  store_release(head, h+1) --+----------> h = load_acquire(head)
                                          |  이 읽기가 아래보다 먼저 일어난다
                                          +--> *out = buf[t & MASK];
```

release store는 그 **앞의** 접근이 뒤로 새지 않게 하고, acquire load는 그 **뒤의** 접근이 앞으로 새지 않게 한다. 둘이 같은 변수(`head`)에 대해 짝을 이루면 consumer가 새 head를 본 순간 producer가 그 전에 한 모든 쓰기를 본다(happens-before). `tail`에 대해서도 대칭으로 같은 쌍이 성립하고, 그쪽은 "producer가 새 tail을 봤다면 consumer는 그 칸을 이미 다 읽은 뒤다"를 보장해 덮어쓰기를 안전하게 만든다.

### 6.3 `volatile`은 무엇을 하고 무엇을 안 하는가

```
  volatile 이 하는 것                volatile 이 안 하는 것
  -------------------------------    ---------------------------------------
  접근을 지우거나 합치지 않는다      일반 변수와의 순서를 지키지 않는다
  레지스터에 캐싱하지 않는다         store buffer 를 비우거나 cache 를 다루지 않는다
  volatile 접근끼리는 순서 유지      원자성을 주지 않는다 (v++ 은 여전히 RMW)
```

즉 `volatile`은 MMIO 레지스터 접근이나 `while (!isr_done_flag);` 같은 폴링에는 맞는 도구지만, "데이터와 index 사이의 순서"라는 우리 문제에는 맞지 않는다.

### 6.4 실제 하드웨어에서 저 배리어는 무엇이 되는가

- **단일 코어 Cortex-M4/M33의 ISR과 task**: 같은 코어에서 돌고, 코어는 자기 관점의 프로그램 순서를 뒤집지 않는다. 진짜로 필요한 것은 **컴파일러 배리어**뿐이다. 그래도 GCC/Clang은 ARMv7-M에서 release store 앞에 `DMB`를 넣는 경우가 많다. 몇 사이클짜리 보험이라 ISR 예산에 보통 문제가 안 되고, ARMv8-M Mainline이면 `STL`/`LDA` 단일 명령으로 더 싸게 처리된다.
- **DMA가 버퍼를 읽거나 쓰는 경우**: DMA는 프로그램 순서를 모르는 별도 bus master라 `DMB`가 진짜로 필요하다. Cortex-M7처럼 D-cache가 있으면 배리어로도 부족해 `SCB_CleanDCache_by_Addr` / `SCB_InvalidateDCache_by_Addr`가 따라오고, 버퍼를 cache line(보통 32바이트) 경계에 정렬하고 크기도 line 배수로 맞춰야 이웃 데이터가 같이 날아가지 않는다. 듀얼 코어 공유 SRAM도 같은 이유로 배리어와 캐시 관리가 둘 다 필요하고, 공유 영역은 보통 MPU로 non-cacheable하게 잡는다.
- **tearing**: 정렬된 32-bit 접근은 Cortex-M에서 single-copy atomic이라 index가 반쯤 읽히지 않는다. index를 `uint64_t`로 만들었다면 32-bit 접근 두 번으로 쪼개지고, 그 사이 인터럽트가 끼면 앞뒤 절반이 다른 시점의 값이 된다.

### 6.5 producer가 둘이 되면 무엇이 깨지는가

우선순위가 다른 두 ISR이 모두 `rb_put`을 부르면 `head = h + 1`이 더 이상 "내 값을 내가 쓰는 것"이 아니다. 진짜 RMW가 되고 3.1의 그림이 돌아온다.

```
  시간  ISR-A (낮은 우선순위)             ISR-B (높은 우선순위)      head
  ----  ------------------------------   -----------------------    ----
   t0   h = load(head) -> 10                                         10
   t1   buf[10 & MASK] = 'x'                                         10
   t2        --- ISR-B 가 ISR-A 를 선점 ---
   t3                                    h = load(head) -> 10        10
   t4                                    buf[10 & MASK] = 'y'  <--!  10
   t5                                    store(head, 11)             11
   t6        --- ISR-A 재개 ---
   t7   store(head, 11)                                              11
```

두 ISR이 같은 슬롯 10에 썼고(`'x'`가 `'y'`로 덮였다) head는 11로 한 번만 올라갔다. 바이트 하나가 조용히 사라진다. 고치는 방법은 셋이다.

- producer 구간을 짧은 critical section으로 감싼다. Cortex-M이면 PRIMASK로 전부 막기보다 BASEPRI로 해당 우선순위 이하만 막는 것이 낫다.
- `head`를 CAS(`LDREX`/`STREX`, C11의 `atomic_compare_exchange_weak`)로 예약한다. 다만 "예약됐지만 아직 안 쓴 슬롯"이 생겨 consumer가 그걸 기다리게 만드는 상태가 추가로 필요해 구현이 확 복잡해진다.
- 가장 실용적인 답: **producer마다 ring buffer를 하나씩 준다.** consumer가 여러 버퍼를 돌아가며 비우면 lock도 CAS도 없이 SPSC 성질이 유지된다.

consumer가 둘이 되는 경우도 대칭으로 깨진다. 두 task가 같은 바이트를 읽고 tail을 각자 올린다.

---

## 7. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|------|------|------|-----------|
| 공유 `count` 필드를 둔다 | 몇 시간 뒤 수신이 멈춘다. 리셋하면 정상 | `count++`/`count--`가 RMW라 lost update | `count`를 없애고 `head - tail`로 계산 |
| index를 `% RB_SIZE`로 자른다 | full과 empty를 구분 못 해 데이터가 통째로 사라지거나 무한 empty | 물리 위치가 같은 두 상태 | free-running index + 접근 시 마스킹 |
| `RB_SIZE`가 2의 거듭제곱이 아니다 | 특정 index에서만 엉뚱한 칸을 읽는다 | `h & (SIZE-1)`이 틀린 index를 만든다 | `_Static_assert`로 컴파일 타임에 막는다 |
| `head`만 `volatile`로 선언한다 | `-O0`에서는 되고 `-O2`에서 간헐적으로 깨진다 | `buf[]`와의 상대 순서가 보장되지 않는다 | `_Atomic` + release/acquire |
| 데이터 쓰기 전에 index를 올린다 | 수신 스트림에 한 바퀴 전 바이트가 섞인다. CRC 에러 | publish가 데이터보다 먼저 보인다 | 데이터 먼저, index 나중 (4.3) |
| bulk에서 `first = n` 클램프를 빼먹는다 | 즉시 메모리 손상, 하드폴트 | `n - first`가 unsigned로 wrap | `if (first > n) first = n;` |
| full일 때 ISR이 스핀하며 기다린다 | 시스템 전체가 멈춘다. watchdog 리셋 | ISR이 consumer를 기다리는데 consumer는 선점당해 있다 | 드롭하고 `dropped` 증가, 나중에 보고 |
| `rb_count()`를 검사와 접근에 두 번 부른다 | 드물게 empty 버퍼를 읽거나 full에 쓴다 | 두 호출 사이에 ISR이 끼어든다 | 한 번 읽은 지역 변수로 검사와 접근을 모두 처리 |
| DMA/듀얼코어인데 배리어만 넣는다 | 첫 몇 바이트가 오래된 값 | cache가 낀다 | clean/invalidate + cache line 정렬 |

---

## 8. 직접 확인하기

```
cd coding
make sol N=01     # 모범답안. ok 9줄 + ALL TESTS PASSED
make run N=01     # 내 구현. 채우기 전에는 첫 assert 에서 멈춘다
```

볼 것은 `ok` 줄 9개와 마지막 `ALL TESTS PASSED`이고, 경고는 한 줄도 나오면 안 된다. 그리고 일부러 깨뜨려 본다.

- **한 칸을 낭비해 보기**: `rb_put`의 full 조건을 `== RB_SIZE - 1u`로 바꾸면 `test_full_behaviour`가 256번째 put에서 실패한다. 256바이트에서는 0.4% 손실이지만 8슬롯짜리 오디오 프레임 큐에서는 12.5%다.
- **wrap 보정을 넣어 보기**: `rb_count`를 `return (h >= t) ? (h - t) : (t - h);`로 바꾸면 평범한 테스트는 다 통과하는데 `test_index_overflow`만 실패한다. "그럴듯해 보이는 보정"이 4.3일에 한 번 터지는 버그를 심는다.
- **순서를 뒤집어 보기**: `rb_put`에서 데이터 쓰기와 release store의 순서를 바꿔도 테스트는 **통과한다**. 단일 스레드라 재배치가 드러나지 않기 때문이다. 교훈은 "단위 테스트로는 메모리 순서 버그를 못 잡는다"이고, 진짜로 잡으려면 두 스레드 + ThreadSanitizer(`-fsanitize=thread`)가 필요하다.
- **마스크를 깨뜨려 보기**: `RB_SIZE`를 200으로 바꾸면 `_Static_assert`가 컴파일을 막는다. 그 assert를 지우고 빌드하면 `test_wrap_around_buffer`가 실패한다. 컴파일 타임 검사 한 줄이 런타임 미스터리 하나를 없앤다.

---

## 9. 면접에서 말하기

**한국어 설명 흐름** (2분 안에)

1. 구조: 고정 크기 바이트 배열 하나에 index 두 개. head는 ISR만, tail은 task만 쓴다.
2. lock이 없어도 되는 이유: 각 index의 writer가 한 명이라 RMW 경쟁이 없다. 공유 count를 두는 순간 깨진다고 덧붙인다.
3. index 설계: 자르지 않고 32-bit로 계속 증가시키고 접근할 때만 마스킹한다. 그래서 `head - tail`이 그대로 개수고, wrap을 넘어서도 정확하고, 한 칸 낭비 없이 전부 쓴다. 크기는 2의 거듭제곱이어야 하고 `_Static_assert`로 막는다.
4. 순서와 실무 디테일: 양쪽 다 데이터 먼저 index 나중, `volatile`로는 부족한 이유를 한 문장으로. full이면 ISR은 드롭하고 카운터를 올린다. 단일 코어에서는 사실상 컴파일러 배리어지만 DMA나 듀얼코어면 `DMB`와 캐시 관리가 진짜로 필요하다.

**그대로 쓸 영어 문장 5개**

1. "Head is written only by the ISR and tail only by the task, so each index has exactly one writer and I don't need a lock."
2. "I keep the indices free-running as 32-bit counters and mask on access, so head minus tail is the fill level even across the 2^32 wrap, and I get to use every slot instead of wasting one."
3. "The producer writes the data first and then publishes head with a release store; the consumer reads head with acquire before touching the payload — that pairing is what gives me happens-before."
4. "Volatile alone isn't enough here: it stops the compiler from eliding the access, but it says nothing about the ordering between the payload write and the index update."
5. "On a single-core Cortex-M this is effectively a compiler barrier, but it becomes a real DMB — plus cache clean and invalidate on an M7 — the moment DMA or a second core touches the buffer."

**화이트보드에 그릴 순서**

1. 칸 8개짜리 배열을 그리고 `H`, `T`를 같은 자리에 찍는다. "이게 empty다." 3개를 채워 `H`를 옮기고, 1개를 빼서 `T`를 옮긴다.
2. `H`를 배열 끝까지 밀어 앞으로 넘긴 뒤 `H`와 `T`가 같은 칸에 오는 두 경우를 나란히 그린다. "물리 위치만 보면 구분이 안 된다." 옆에 `head = 9`, `tail = 1`을 쓰고 `9 - 1 = 8 = SIZE`라고 적는다. "그래서 안 자른다."
3. 마지막에 producer와 consumer를 세로 두 줄로 그리고 `buf 쓰기 -> head publish`, `head 읽기 -> buf 읽기`를 화살표로 잇는다. 화살표 위에 `release`와 `acquire`라고 쓴다.

---

## 10. follow-up 답안

**1. "What changes if a second ISR also writes into this buffer?"** `head` 갱신이 진짜 read-modify-write가 되면서 SPSC 가정이 깨진다. 우선순위가 다른 두 ISR은 서로 선점하므로 6.5의 인터리빙이 그대로 발생한다. 두 ISR이 같은 슬롯에 쓰고 head는 한 번만 올라가 바이트가 유실된다. 선택지는 세 가지다. BASEPRI로 해당 우선순위 구간만 막는 짧은 critical section, `LDREX`/`STREX` CAS로 슬롯을 예약하는 방식(예약과 게시가 분리되면서 consumer 쪽 상태가 복잡해진다), 그리고 실무에서 가장 흔한 답인 producer마다 버퍼를 하나씩 주고 consumer가 돌아가며 비우는 방식이다. 마지막 것이 SPSC 성질을 유지하면서 코드도 제일 단순하다.

**2. "The DMA controller wants to write directly into the buffer. What API do you need to add?"** DMA는 연속된 물리 영역에만 쓸 수 있으므로 "지금 head부터 몇 바이트를 연속으로 쓸 수 있는가"를 돌려주는 API가 필요하다. `uint32_t rb_claim(rb, uint8_t **ptr)`처럼 포인터와 길이를 주고, DMA 완료 인터럽트에서 `rb_commit(rb, n)`이 실제로 전송된 만큼 head를 release store로 올린다. 길이는 `min(free, RB_SIZE - (head & MASK))`다. wrap 지점에서는 한 번의 DMA로 다 못 채우므로 두 번으로 나누거나, 링을 두 블록으로 보고 핑퐁으로 돌린다. 그리고 DMA는 별도 bus master라 여기서는 `DMB`가 진짜로 필요하고, D-cache가 있는 코어라면 `rb_commit` 전에 invalidate가 들어간다. 버퍼는 cache line 경계에 정렬한다.

**3. "I want to queue fixed-size records so a reader never sees half a record."** 두 가지 길이 있다. 하나는 바이트 링을 유지하되 슬롯 단위로 바꾸는 것이다. `uint8_t buf[]` 대신 `record_t buf[N_SLOTS]`를 두고 index를 레코드 단위로 센다. `N_SLOTS`가 2의 거듭제곱이면 마스킹이 그대로 통하고, 레코드 전체를 쓴 다음 head를 한 번 올리니 부분 노출이 원천적으로 없다. 다른 하나는 가변 길이를 지원해야 할 때인데, 길이 프리픽스를 붙여 바이트 링에 넣고 `rb_write`로 헤더와 페이로드를 한 번에 게시한다. 이때 레코드가 wrap을 가로지르면 길이 필드 자체가 쪼개질 수 있으므로, 남은 공간이 레코드보다 작으면 끝을 패딩으로 버리고 처음부터 쓰는 규칙을 둔다. 고정 크기로 갈 수 있으면 고정 크기가 항상 더 낫다.

**4. "How do you wake the consumer task, and how do you avoid waking it once per byte?"** FreeRTOS면 ISR에서 `vTaskNotifyGiveFromISR()`를 부르고 `portYIELD_FROM_ISR(xHigherPriorityTaskWoken)`으로 마무리한다. Zephyr면 `k_sem_give()`이고 ISR에서 호출해도 안전하다. 바이트마다 깨우면 context switch 비용이 데이터 처리 비용을 넘어서고 배터리 기기에서는 전력 문제가 된다. 세 가지 방법을 섞는다. UART의 idle line 인터럽트나 RX FIFO watermark를 써서 여러 바이트가 모였을 때만 인터럽트가 나게 하고, 링버퍼 점유율이 임계치를 넘을 때만 알림을 보내고, 짧은 타임아웃 타이머로 데이터가 적게 와도 결국은 처리되게 보장한다. 참고로 Zephyr에는 이미 `ring_buf` API가 있어서 실제 프로젝트라면 그걸 먼저 검토한다.

**5. "Two different cores sharing SRAM, one of them has a data cache. What breaks and what do you add?"** 세 가지가 동시에 문제가 된다. 첫째, 두 코어는 서로의 프로그램 순서를 보장받지 못하므로 컴파일러 배리어로는 부족하고 진짜 `DMB`가 필요하다. release/acquire로 짜 두면 컴파일러가 알아서 넣어 준다. 둘째, 캐시 일관성이 하드웨어로 유지되지 않는 조합이면 한쪽이 쓴 데이터가 자기 캐시에만 남는다. 공유 영역을 MPU로 non-cacheable하게 잡는 것이 가장 단순한 해법이고, 성능이 필요하면 clean/invalidate를 명시적으로 넣되 cache line 경계에 맞춰 버퍼를 정렬한다. 셋째, 두 코어의 워드 크기나 구조체 패딩이 다르면 `ringbuf_t`의 레이아웃 자체가 어긋나므로 공유 구조체는 고정 폭 타입만 쓰고 정렬을 명시한다. 그리고 한쪽 코어가 리셋되는 경우를 생각해서 매직 넘버와 버전 필드를 헤더에 두는 것이 보통이다.

---

## 11. 요약 & 체크리스트

핵심 세 줄로 줄이면 이렇다.

- **소유권**: head는 producer만, tail은 consumer만 store 한다. 공유 `count`를 두는 순간 lock이 필요해진다.
- **index**: 자르지 말고 32-bit free-running으로 두고 접근할 때만 `& (SIZE-1)`. 그래서 `head - tail`이 개수고, wrap도 full/empty도 공짜로 해결된다.
- **순서**: 양쪽 다 데이터 먼저, index 나중. `volatile`이 아니라 release/acquire다.

- [ ] 빈 화면에서 10분 안에 `ringbuf_t`, `rb_init`, `rb_put`, `rb_get`, `rb_count`를 쓸 수 있다.
- [ ] 왜 lock이 필요 없는지를 "각 index의 writer가 한 명"이라는 문장으로 설명할 수 있다.
- [ ] 공유 `count`가 깨지는 인터리빙을 t0부터 t8까지 표로 그릴 수 있다.
- [ ] full과 empty에서 물리 위치가 같아지는 그림을 그리고, free-running index가 그걸 어떻게 구분하는지 보일 수 있다. `RB_SIZE`가 2의 거듭제곱이어야 하는 이유와 `_Static_assert`도 함께 말할 수 있다.
- [ ] `volatile`이 보장하는 것과 보장하지 않는 것을 각각 한 문장으로 말할 수 있다.
- [ ] release store와 acquire load가 짝을 이뤄 happens-before를 만드는 과정을 화살표로 그릴 수 있고, 단일 코어와 DMA·듀얼코어에서 그것이 각각 무엇으로 컴파일되는지 안다.
- [ ] bulk write가 memcpy 두 번으로 쪼개지는 이유와 head를 한 번만 올려야 하는 이유를 안다.
- [ ] producer가 둘이 되면 무엇이 깨지는지, 그리고 세 가지 해법을 댈 수 있다.
- [ ] `make sol N=01`이 `ALL TESTS PASSED`를 찍는 것을 직접 확인했다.
