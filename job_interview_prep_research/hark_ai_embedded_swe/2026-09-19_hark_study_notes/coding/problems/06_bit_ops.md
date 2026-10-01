# 06. 비트 set / clear / toggle / test 와 비트 범위

> **주제**: 비트 연산 기초 · 마스크 생성 · 필드 추출·삽입 · **난이도**: 기초 · **목표 시간**: 20분
> **레벨**: L0 비트·정수 기초 (06 → 07 → 08 → 09)
> **해설은 보지 말 것**: 먼저 `starters/06_bit_ops.c`를 채워 `make run N=06`으로 통과시킨다.

03번과 겹치지 않는다. 03번은 "MMIO 레지스터 헤더에 넣을 매크로"였고, 이 문제는 그 아래 단계인 **값 하나를 비트 단위로 다루는 순수 함수**다. `volatile`도 레지스터도 나오지 않는다.

## 면접관의 문장

"Before we touch any peripheral, let me check the basics. Write me a small set of pure functions on a 32-bit word: make a mask for a single bit, make a mask for a contiguous range, set, clear or toggle several bits at once, test whether all or any of a set of bits are on, and pull a bit range out as a plain integer and put one back in. No hardware, no `volatile`, just values. I care about two things: that the shift counts are always in range, and that inserting a field never disturbs its neighbours. When you are done, tell me what `1 << 31` does, and what a 32-bit-wide mask looks like."

## 요구사항

1. `bit_mask(n)`은 bit `n` 하나만 1인 `uint32_t`를 돌려준다. `n`은 0..31이고, 범위를 벗어나면 `assert`로 막는다. `bit_mask(31) == 0x80000000`이어야 한다.
2. `mask_range(hi, lo)`는 bit `hi`..`lo`가 연속으로 1인 마스크다. `0 <= lo <= hi <= 31`. `mask_range(31, 0) == 0xFFFFFFFF`여야 하고, 이 값을 만드는 과정에서 **시프트 카운트가 32가 되는 식을 쓰지 않는다**.
3. `bits_apply(v, mask, op)`는 `mask`에 든 비트들에만 `op`(`BITS_SET` / `BITS_CLEAR` / `BITS_TOGGLE`)를 적용한 **새 값**을 돌려준다. `mask` 밖의 비트는 그대로다. `mask`는 여러 비트를 동시에 담을 수 있다.
4. `bits_apply`는 `BITS_SET`과 `BITS_CLEAR`에서 **멱등**이다. 이미 1인 비트를 다시 세워도, 이미 0인 비트를 다시 지워도 결과가 같다.
5. `bits_assign(v, mask, on)`은 `on`이 참이면 set, 거짓이면 clear다. `if` 없이 한 식으로 쓴다(분기 예측이 없는 코어에서 쓰는 관용구다).
6. `bits_test(v, mask, how)`는 `BITS_ALL`(mask의 모든 비트가 1), `BITS_ANY`(하나라도 1), `BITS_NONE`(하나도 없음)을 판정한다. `mask == 0`일 때의 답을 **먼저 정하고** 구현한다. 이 문제의 정답은 ALL = true, ANY = false, NONE = true다.
7. `bits_extract(v, hi, lo)`는 bit `hi`..`lo`를 0부터 시작하는 정수로 꺼낸다. `bits_extract(0xDEADBEEF, 15, 8) == 0xBE`.
8. `bits_insert(v, hi, lo, field)`는 bit `hi`..`lo`만 `field`로 교체한다. 폭을 넘는 상위 비트는 **잘라낸다**. 이웃 필드와 나머지 비트는 읽은 값 그대로 남아야 한다.
9. `bits_extract`와 `bits_insert`는 모든 유효 값에서 왕복이 성립한다.
10. `bits_fits(hi, lo, field)`는 `field`가 그 폭에 잘림 없이 들어가면 `true`다. 파라미터 검증용이다.
11. 전체에서 UB가 없어야 한다. 시프트 카운트는 항상 0..31, 왼쪽으로 미는 대상은 항상 unsigned다.

## 인터페이스

```c
typedef enum { BITS_SET, BITS_CLEAR, BITS_TOGGLE } bits_op_t;
typedef enum { BITS_ALL, BITS_ANY, BITS_NONE }    bits_match_t;

static uint32_t bit_mask(unsigned n);
static uint32_t mask_range(unsigned hi, unsigned lo);
static uint32_t bits_apply(uint32_t v, uint32_t mask, bits_op_t op);
static uint32_t bits_assign(uint32_t v, uint32_t mask, bool on);
static bool     bits_test(uint32_t v, uint32_t mask, bits_match_t how);
static uint32_t bits_extract(uint32_t v, unsigned hi, unsigned lo);
static uint32_t bits_insert(uint32_t v, unsigned hi, unsigned lo, uint32_t field);
static bool     bits_fits(unsigned hi, unsigned lo, uint32_t field);
```

## 제약

- 동적 할당 금지. 표준 라이브러리는 `assert.h` / `stdbool.h` / `stdint.h` / `stdio.h`만.
- 컴파일러 내장 함수(`__builtin_*`) 금지.
- `cc -std=c11 -Wall -Wextra -O2 -g`에서 경고 0개.
- 모든 함수는 **순수 함수**다. 인자를 고치지 않고 새 값을 돌려준다.
- 뼈대 파일의 타입 정의와 테스트는 수정하지 않는다.

## 예시 동작

```text
mask_range(15, 8)
  0xFFFFFFFF >> (31 - 15) = 0x0000FFFF     위쪽을 깎는다
  0xFFFFFFFF << 8         = 0xFFFFFF00     아래쪽을 밀어낸다
  AND                     = 0x0000FF00

  31                    16 15            8 7             0
 +------------------------+---------------+--------------+
 |0000 0000 0000 0000     |1111 1111      |0000 0000     |
 +------------------------+---------------+--------------+
```

```text
bits_insert(0xAAAA5555, 15, 8, 0x3C)

  v      = 1010 1010 1010 1010 0101 0101 0101 0101
  m      = 0000 0000 0000 0000 1111 1111 0000 0000
  v & ~m = 1010 1010 1010 1010 0000 0000 0101 0101   <- 필드 자리만 비운다
  f << 8 = 0000 0000 0000 0000 0011 1100 0000 0000
  OR     = 1010 1010 1010 1010 0011 1100 0101 0101 = 0xAAAA3C55

  bit 31..16 과 bit 7..0 은 한 비트도 달라지지 않았다.
```

```text
절단 (truncation)

  bits_insert(0, 11, 8, 0x1FF)     4비트 자리에 9비트 값을 넣으면
    (0x1FF << 8)      = 0x0001FF00
    & mask_range(11,8)= 0x00000F00      <- & m 이 넘친 비트를 버린다
  결과 0x00000F00.  & m 을 빼면 0x0001FF00 이 되어 bit 12 이상을 오염시킨다.
```

## 스스로 점검할 질문

1. `1 << 31`과 `1u << 31`의 차이는 무엇인가. 전자가 왜 UB인가.
2. `1u << 32`는 0이 아니다. 무엇인가. (답: "무엇이든 될 수 있다"가 정답이다. 왜?)
3. 폭 `w` 마스크를 `(1u << w) - 1`로 만들면 `w == 32`에서 무슨 일이 생기나. `mask_range(w - 1, 0)`은 왜 괜찮은가.
4. `~mask`에서 `mask`가 `uint32_t`가 아니라 `int`라면 어떤 값이 나오나.
5. `bits_insert`에서 `& m`을 빼면 정확히 어떤 입력에서 어떤 비트가 오염되나.
6. `bits_assign`을 `if` 없이 쓰는 식을 적어 보라. `-(uint32_t)on`이 왜 0 또는 0xFFFFFFFF인가.
7. `mask == 0`일 때 ALL을 true로 정한 이유는 무엇인가. 반대로 정하면 호출부에서 어떤 버그가 나기 쉬운가.
8. `x >> 1`에서 `x`가 `int32_t`이고 음수라면 결과가 무엇인가. 표준은 뭐라고 하나.

## follow-up (면접관이 이어서 물을 것)

1. "Make these compile-time constants. Which of them can be macros without double-evaluation bugs?"
2. "Now the word is 64-bit on one target and 32-bit on another. What changes?"
3. "How would you extract a field that straddles two bytes of a byte stream?"
4. "Show me the same set-and-clear done with a single store on hardware that has a bit-band or a BSRR register."
5. "Where would you put an `assert` in production firmware, and where would you not?"

---

**해설**: [notes/L0_bit_basics.md](../drills/L0_bit_basics.html) §2, §3, §4 · **답안**: `solutions/06_bit_ops.c`
