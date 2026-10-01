# 12. volatile과 const volatile — 무엇을 보장하고 무엇을 안 하는가

> **주제**: `volatile` 의미론 · 레지스터 접근 래퍼 · RMW의 비원자성 · write-1-to-clear · **난이도**: 중급 · **목표 시간**: 25분
> **레벨**: L1 메모리·포인터 · **해설은 보지 말 것**: 먼저 `starters/12_volatile_const.c`를 채워 `make run N=12`으로 통과시킨다.

## 면접관의 문장

"A junior engineer on my team removed a `volatile` because a static analyzer flagged it. The build got 200 bytes smaller and the UART driver started hanging on boot, about one time in twenty. Walk me through what the compiler did.

Then write me the register access layer. Read, write, read-modify-write, and a polling helper with a timeout. I want every hardware access to go through those wrappers, and I want to be able to count them — if I ask 'how many bus transactions did that function do', the code should be able to answer.

And be precise about the boundaries. `volatile` gets described as 'don't optimize this' which is too vague to be useful. Tell me exactly what it promises. Then tell me three things people assume it gives them that it doesn't."

## 요구사항

1. 가짜 주변장치 `fake_uart_t`는 `status`(하드웨어가 바꾼다), `data`, `ctrl`(CPU가 쓴다), `icr`(write-1-to-clear)의 네 `volatile uint32_t` 레지스터를 갖는다. 모델 내부 상태(`ticks`, `ready_at`, `next_byte`)는 CPU 쪽 코드가 절대 만지지 않는다.
2. `hw_tick()`은 "하드웨어 시간이 1틱 흘렀다"다. `ticks`가 `ready_at`에 도달하면 `status`에 `ST_READY`를 세운다. 이 함수가 CPU와 무관한 외부 변경의 역할을 한다.
3. **모든** 레지스터 접근은 `reg_read` / `reg_write`만 통과하고, 두 함수는 각각 전역 카운터 `g_reads` / `g_writes`를 1 올린다. 이 카운터가 이 문제의 계측기다.
4. `reg_read`의 인자 타입은 `const volatile uint32_t *`다. `const`와 `volatile`이 각각 무엇을 말하는지 설명할 수 있어야 한다.
5. `reg_modify(reg, clear_mask, set_mask)`는 정확히 `reg_read` 1회 + `reg_write` 1회를 수행한다. `clear_mask`를 먼저 내리고 `set_mask`를 올린다. 중간 계산은 지역 변수에서만 한다.
6. `reg_set_bits` / `reg_clear_bits`는 `reg_modify`로 구현한다.
7. `reg_wait_bits(reg, mask, max_polls, tick)`는 `mask`의 비트가 **모두** 설 때까지 폴링한다. 루프 한 바퀴마다 `reg_read`를 정확히 한 번 부른다. 성공은 `0`, 타임아웃은 `-1`이다.
8. `reg_wait_bits`는 `max_polls == 0`이면 읽지도 않고 `-1`을 돌려준다. 조건이 처음부터 만족되어 있으면 읽기 1회로 `0`을 돌려주고 `tick`을 부르지 않는다. `tick`이 NULL이면 부르지 않는다.
9. `reg_wait_bits`에서 `mask == 0`은 "조건 없음"이라 항상 즉시 성공한다.
10. `reg_wait_bits_snapshot`은 `reg_wait_bits`와 인자·의도가 같지만, `reg_read`를 루프 **밖**에서 딱 한 번만 부르고 그 값을 계속 다시 본다. 컴파일러가 `volatile`이 없는 코드에 정당하게 수행할 수 있는 변형(loop-invariant code motion)을 사람이 미리 적용한 형태다.
11. `reg_read_twice(reg, out1, out2, tick)`는 읽고 → `tick` → 다시 읽는다. 수행한 읽기 횟수를 돌려준다(2여야 한다).
12. `reg_clear_error_w1c(icr, mask)`는 `reg_write` 한 번으로 끝난다. 읽기를 하지 않는다. 같은 값을 두 번 쓰면 쓰기 카운터가 2 올라야 한다.

## 인터페이스

```c
#include <stdint.h>

#define ST_READY   (1u << 0)
#define ST_BUSY    (1u << 1)
#define ST_OVERRUN (1u << 2)

typedef struct {
    volatile uint32_t status;   /* RO: 하드웨어가 바꾼다 */
    volatile uint32_t data;     /* RO                    */
    volatile uint32_t ctrl;     /* RW: CPU가 설정한다    */
    volatile uint32_t icr;      /* WO: write-1-to-clear  */
    uint32_t ticks, ready_at, next_byte;  /* 모델 내부 — CPU는 안 봄 */
} fake_uart_t;

void hw_tick(void);                     /* 하드웨어 시간 1틱 */
void hw_reset(uint32_t ready_at);       /* 장치 + 카운터 초기화 */
void hw_apply_icr(void);                /* ICR 명령에 대한 하드웨어 반응 */

uint32_t reg_read(const volatile uint32_t *reg);
void     reg_write(volatile uint32_t *reg, uint32_t value);
void     reg_modify(volatile uint32_t *reg, uint32_t clear_mask,
                    uint32_t set_mask);
void     reg_set_bits(volatile uint32_t *reg, uint32_t mask);
void     reg_clear_bits(volatile uint32_t *reg, uint32_t mask);

int reg_wait_bits(const volatile uint32_t *reg, uint32_t mask,
                  uint32_t max_polls, void (*tick)(void));
int reg_wait_bits_snapshot(const volatile uint32_t *reg, uint32_t mask,
                           uint32_t max_polls, void (*tick)(void));

uint32_t reg_read_twice(const volatile uint32_t *reg,
                        uint32_t *out_first, uint32_t *out_second,
                        void (*tick)(void));

void reg_clear_error_w1c(volatile uint32_t *icr, uint32_t mask);
```

## 제약

- **옵티마이저의 동작을 테스트하지 않는다.** "volatile을 빼면 컴파일러가 읽기를 루프 밖으로 끌어낸다"를 assert로 확인하려 들지 말 것. 최적화는 보장이 아니고, 그걸 관측하려는 코드는 대개 undefined behaviour에 의존한다. 대신 두 동작을 **손으로 각각 구현해** 접근 횟수와 결과를 비교한다.
- undefined behaviour를 만들지 않는다. 데이터 경쟁, 초기화 안 된 읽기, 타입 재해석 전부 금지.
- 특정 컴파일러·최적화 수준에서만 통과하는 테스트를 쓰지 않는다. `-O0`과 `-O2`에서 결과가 같아야 한다.
- 레지스터를 래퍼 없이 직접 읽거나 쓰지 않는다(하드웨어 모델 함수 `hw_tick`/`hw_reset`/`hw_apply_icr`만 예외다 — 그건 CPU가 아니라 실리콘 역할이다).
- 실제 벤더 헤더나 실제 칩의 레지스터 이름을 쓰지 않는다. 이 파일의 장치는 가상이고 주석에 그렇게 밝힌다.
- 하드웨어 헤더 금지. C11 표준 라이브러리만.
- `cc -std=c11 -Wall -Wextra -O2 -g`에서 경고 0개.

## 예시 동작

`ready_at = 5`인 장치(5틱 뒤 `ST_READY`가 선다)에서 두 폴링 함수를 비교한다.

```
reg_wait_bits(&status, ST_READY, 100, hw_tick)

  poll  읽은 값       ticks   판정
   1    0x00000000     0      아니다 -> tick()
   ...                        (poll 5의 tick에서 ticks=5, READY 세워짐)
   6    0x00000001     5      맞다   -> return 0
  g_reads = 6, 결과 0 (성공)
```

```
reg_wait_bits_snapshot(&status, ST_READY, 100, hw_tick)

  cached = reg_read(&status)  ->  0x00000000     (읽기 1회, 여기서 끝)

  poll 1..100:  cached=0  ->  아니다  ->  tick()
  g_reads = 1, ticks = 100, status = 0x00000001, 결과 -1 (타임아웃)
                            ^^^^^^^^^^^^^^^^^^^ 비트는 실제로 서 있다!
```

두 함수는 같은 인자를 받아 다른 결과를 낸다. 차이는 "읽기를 몇 번 했는가" 하나뿐이다. 이게 `volatile`이 금지하는 변형이고, 증상은 언제나 "타임아웃" 또는 "행(hang)"이다.

RMW는 원자적이 아니다. 읽기와 쓰기 사이에 다른 주체가 끼어들면 그 변경이 사라진다.

```
ctrl = ENABLE
  [1] CPU: v = reg_read(&ctrl)          v = ENABLE
  [2] ISR: reg_set_bits(&ctrl, IRQ_EN)  ctrl = ENABLE | IRQ_EN
  [3] CPU: v |= LOOPBK; reg_write(&ctrl, v)    ctrl = ENABLE | LOOPBK

  결과: ISR이 세운 IRQ_EN이 통째로 사라졌다 (lost update). volatile은 이걸
  막지 않는다 — critical section이나 하드웨어의 set/clear 레지스터가 막는다.
```

write-1-to-clear는 "대입"이 아니라 "명령"이다.

```
status = OVERRUN | BUSY
  reg_clear_error_w1c(&icr, OVERRUN)   icr = OVERRUN  (쓰기 1회)
  hw_apply_icr()                       status &= ~OVERRUN; icr = 0
  결과: status = BUSY  ← BUSY는 그대로. RMW가 없으니 lost update도 없다
```

## 스스로 점검할 질문

1. `volatile`이 표준에서 보장하는 것을 한 문장으로 말하라. "최적화를 막는다"보다 정확하게.
2. `volatile`이 보장하지 **않는** 것 세 가지를 대라. 각각 무엇으로 해결하는가?
3. `const volatile`은 모순처럼 보인다. 어떤 레지스터가 정확히 그것인가? `const`만 붙이면 어떤 최적화가 허용되는가?
4. `volatile` 변수에 대한 두 번의 읽기를 컴파일러가 하나로 합칠 수 없는 이유는 무엇인가? 표준의 어떤 개념이 그걸 금지하는가?
5. `reg_modify`가 "읽기 1회 + 쓰기 1회"임을 어떻게 보장하는가? 소스를 어떻게 잘못 쓰면 3회가 되는가?
6. ISR과 task가 같은 `uint32_t` 플래그를 공유한다. `volatile`로 충분한 경우와 부족한 경우를 각각 하나씩 대라.
7. 멀티코어에서 `volatile`은 무엇을 해 주는가? 캐시 일관성과 메모리 순서는 누가 책임지는가?
8. write-1-to-clear 레지스터에 `reg_modify`(RMW)를 쓰면 무슨 일이 생기는가? 왜 그게 특히 위험한가?
9. `_Atomic`과 `volatile`의 차이를 한 문장으로. 펌웨어에서 언제 어느 쪽을 쓰는가?
10. 폴링 루프에 타임아웃이 없으면 어떤 장애 모드가 되는가? 타임아웃을 "폴링 횟수"로 세는 것과 "시간"으로 세는 것 중 어느 쪽이 맞는가?

## follow-up (면접관이 이어서 물을 것)

1. "Now the status register is on a bus that takes 6 cycles per access. Does your polling loop change?"
2. "Replace the polling with an interrupt. What does `volatile` do for you now, and what do you need on top?"
3. "Your `reg_modify` is called from both a task and an ISR. Fix it. How much interrupt latency did you just add?"
4. "The compiler reordered two register writes and the peripheral latched the wrong value. `volatile` didn't help. Why not?"
5. "Show me a register you'd declare `volatile` but NOT `const`, even though the CPU only ever reads it."

---

**해설**: [notes/L1_memory_pointers.md](../drills/L1_memory_pointers.html) §8, §9 · **답안**: `solutions/12_volatile_const.c`
