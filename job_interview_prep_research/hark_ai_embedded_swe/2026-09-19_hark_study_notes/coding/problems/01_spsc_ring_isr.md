# 01. ISR-safe SPSC ring buffer

> **주제**: 동시성·메모리 · **난이도**: 중급 · **목표 시간**: 25분
> **해설은 보지 말 것**: 먼저 `starters/01_spsc_ring_isr.c`를 채워 `make run N=01`으로 통과시킨다.

## 면접관의 문장

"Let's say we have a UART receiving bytes from a companion SoC, and the RX interrupt fires once per byte. The main loop parses those bytes into packets, but it can't keep up in real time — it runs whenever it gets scheduled. Write me a fixed-size byte ring buffer that the ISR writes into and the main loop reads from. Don't disable interrupts, and don't use a mutex. When it's full the ISR should drop the byte and keep going rather than block. Walk me through why this is safe without a lock, and then tell me what you'd change if a second ISR also had to write into it."

## 요구사항

1. 버퍼는 고정 크기 바이트 배열이다. 크기는 컴파일 타임 상수 `RB_SIZE`이고 2의 거듭제곱이어야 한다. 이 조건은 `_Static_assert`로 컴파일 타임에 강제한다.
2. 동적 할당 금지. 버퍼는 `ringbuf_t` 안에 인라인으로 들어 있다.
3. producer는 정확히 하나(ISR), consumer는 정확히 하나(main loop)다. `head`는 producer만 store 하고 `tail`은 consumer만 store 한다.
4. index는 `RB_SIZE`로 나누거나 자르지 않는다. 32-bit free-running counter로 계속 증가시키고, 배열에 접근할 때만 `& (RB_SIZE - 1u)`로 자른다.
5. 따라서 `head - tail`이 곧 들어 있는 개수다. 이 식은 index가 `0xFFFFFFFF`를 넘어 0으로 wrap 해도 정확해야 한다.
6. 한 칸을 비워 두는 방식은 쓰지 않는다. `RB_SIZE`개 전부를 채울 수 있어야 한다.
7. `rb_put`은 full이면 아무것도 쓰지 않고 `false`를 돌려주며 `dropped` 카운터를 1 올린다. 버퍼 내용은 그대로 유지된다.
8. `rb_get`은 empty면 `false`를 돌려주고 `out`이 가리키는 값을 **건드리지 않는다**.
9. bulk 버전 `rb_write` / `rb_read`는 실제로 처리한 바이트 수를 돌려준다(부분 성공 허용). 공간보다 많이 요청하면 들어가는 만큼만 넣고 나머지는 `dropped`에 더한다. index는 루프 안에서 한 바이트씩 올리지 말고 **마지막에 딱 한 번** 올린다.
10. producer의 쓰기 순서는 "데이터 먼저, `head` 나중", consumer의 순서는 "데이터 먼저, `tail` 나중"이어야 한다. 컴파일러와 CPU가 이 순서를 바꾸지 못하게 막는 코드가 들어가야 한다.
11. 모든 연산은 O(1)이다. 나눗셈이나 모듈로 연산을 쓰지 않는다.

## 인터페이스

```c
#define RB_SIZE 256u
_Static_assert((RB_SIZE & (RB_SIZE - 1u)) == 0u, "RB_SIZE must be power of 2");

typedef struct {
    uint8_t          buf[RB_SIZE];
    _Atomic uint32_t head;     /* producer(ISR)만 store */
    _Atomic uint32_t tail;     /* consumer(task)만 store */
    uint32_t         dropped;  /* full로 버린 바이트 수 */
} ringbuf_t;

void     rb_init(ringbuf_t *rb);

/* 관찰 */
uint32_t rb_count(const ringbuf_t *rb);
uint32_t rb_space(const ringbuf_t *rb);
bool     rb_is_empty(const ringbuf_t *rb);
bool     rb_is_full(const ringbuf_t *rb);

/* producer 쪽 (ISR) */
bool     rb_put(ringbuf_t *rb, uint8_t b);
uint32_t rb_write(ringbuf_t *rb, const uint8_t *src, uint32_t n);

/* consumer 쪽 (main loop) */
bool     rb_get(ringbuf_t *rb, uint8_t *out);
uint32_t rb_read(ringbuf_t *rb, uint8_t *dst, uint32_t n);

/* 같은 함수의 다른 이름. 면접관이 push/pop이라고 부르면 이쪽을 쓴다. */
bool     rb_push(ringbuf_t *rb, uint8_t b);
bool     rb_pop(ringbuf_t *rb, uint8_t *out);
```

## 제약

- `malloc`, `calloc`, `realloc` 금지. 전역/정적 버퍼만 쓴다.
- 인터럽트 비활성화(`__disable_irq`, PRIMASK/BASEPRI 조작) 금지. mutex, spinlock, semaphore 금지.
- 나눗셈·모듈로(`/`, `%`) 금지. MCU에 하드웨어 나눗셈기가 없을 수 있다.
- C11 표준 라이브러리만 쓴다. 하드웨어 헤더나 RTOS 헤더는 쓰지 않는다. ISR은 설명상의 문맥일 뿐, 테스트는 호스트에서 단일 스레드로 돈다.
- `rb_put` 하나의 실행 경로는 짧아야 한다. ISR 안에서 도는 코드이므로 루프나 블로킹이 있으면 안 된다.
- producer 문맥은 consumer 문맥을 **언제든 선점할 수 있다**고 가정한다. consumer가 `rb_get` 중간 어디에서 멈춰도 producer가 끼어들어 정상 동작해야 한다.

## 예시 동작

`RB_SIZE == 8`이라고 가정한 타임라인이다. `H`는 head, `T`는 tail의 물리 위치다.

```
초기 (head=0, tail=0, count=0)
  idx : 0  1  2  3  4  5  6  7
  buf : .  .  .  .  .  .  .  .
        HT

rb_put('A'), rb_put('B'), rb_put('C')   -> head=3, count=3
  buf : A  B  C  .  .  .  .  .
        T        H

rb_get(&x) -> true, x=='A'              -> tail=1, count=2
  buf : A  B  C  .  .  .  .  .
           T     H

8개를 채운 상태 (head=11, tail=3, count=8) -> full
  buf : I  J  K  D  E  F  G  H
                 TH

rb_put('L') -> false, dropped=1, 내용 변화 없음
```

index 자체의 wrap:

```
head = 0xFFFFFFFE, tail = 0xFFFFFFFA
  count = 0xFFFFFFFE - 0xFFFFFFFA = 4

rb_put 을 세 번 더 하면 head = 0x00000001
  count = 0x00000001 - 0xFFFFFFFA = 7   (unsigned 산술이라 정확하다)
```

bulk write가 쪼개지는 경우(`RB_SIZE == 8`, head=6, 5바이트 쓰기):

```
  idx : 0  1  2  3  4  5  6  7
  buf : .  .  .  .  .  .  .  .
                          H

  첫 번째 memcpy: idx 6,7   (2바이트)
  두 번째 memcpy: idx 0,1,2 (3바이트)
  head는 마지막에 한 번만 6 -> 11
```

## 스스로 점검할 질문

1. `head`와 `tail`을 각각 한 명만 쓴다는 사실이 왜 lock을 없애 주는가? 두 명이 쓰는 변수가 하나라도 생기면 무엇이 깨지는가?
2. index를 `RB_SIZE`로 자르지 않고 free-running으로 두면 무엇이 쉬워지는가? "한 칸 비워 두기" 기법과 비교해 어떤 점이 나은가?
3. `RB_SIZE`가 2의 거듭제곱이 아니면 정확히 어느 줄이 틀리는가?
4. `volatile`만 붙이면 왜 충분하지 않은가? `volatile`이 보장하는 것과 보장하지 않는 것을 각각 한 문장으로 말해 보라.
5. `rb_put`에서 `head`는 relaxed로 읽고 `tail`은 acquire로 읽는다. 왜 둘의 강도가 다른가?
6. 데이터 쓰기와 `head` 갱신의 순서가 뒤집히면 consumer가 구체적으로 무엇을 보게 되는가? 타임라인으로 그려 보라.
7. 단일 코어 Cortex-M에서 실제로 필요한 것이 컴파일러 배리어인지 CPU 배리어(`DMB`)인지, 그리고 그 답이 바뀌는 조건은 무엇인가?
8. `rb_count`가 producer 쪽에서 호출될 때와 consumer 쪽에서 호출될 때, 돌려주는 값은 각각 어떤 의미에서 "안전한 추정치"인가?

## follow-up (면접관이 이어서 물을 것)

1. "What changes if a second ISR also writes into this buffer?"
2. "The DMA controller wants to write directly into the buffer instead of the CPU. What API do you need to add?"
3. "This is a byte buffer, but I want to queue fixed-size records so a reader never sees half a record. How do you change it?"
4. "How do you wake the consumer task, and how do you avoid waking it once per byte?"
5. "Suppose the producer and consumer are on two different cores sharing SRAM, and one of them has a data cache. What breaks and what do you add?"
