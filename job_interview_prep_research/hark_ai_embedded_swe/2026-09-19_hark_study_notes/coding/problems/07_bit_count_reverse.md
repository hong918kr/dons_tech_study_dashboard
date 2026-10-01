# 07. popcount, 비트 반전, find first/last set, 2의 거듭제곱

> **주제**: 비트 세기·뒤집기·찾기 · 2의 거듭제곱 · **난이도**: 기초 · **목표 시간**: 25분
> **레벨**: L0 비트·정수 기초 (06 → 07 → 08 → 09)
> **해설은 보지 말 것**: 먼저 `starters/07_bit_count_reverse.c`를 채워 `make run N=07`으로 통과시킨다.

## 면접관의 문장

"Count the set bits in a 32-bit word. Now do it again without the loop over 32 positions. Now do it with a table, and tell me how big the table is and why you chose that size. Next, reverse the bit order of a word, and of a single byte. Then give me the index of the lowest set bit and of the highest set bit, and decide what you return when the word is zero. Finally, two one-liners that show up in every driver: is this a power of two, and round this up to the next power of two. No compiler builtins. The target is a Cortex-M0, so there is no `CLZ` instruction to lean on."

## 요구사항

1. `popcount_naive(x)`는 32번 돌면서 최하위 비트를 센다. 실행 시간이 입력과 무관하게 **일정**해야 한다.
2. `popcount_kernighan(x)`는 `x &= x - 1` 관용구로 **세워진 비트 수만큼만** 돈다.
3. `popcount_table(x)`는 니블(4비트) 테이블 16바이트로 8회 조회한다. 테이블은 `static const`다. 바이트 테이블(256바이트)을 쓰지 않은 이유를 말할 수 있어야 한다.
4. `popcount_swar(x)`는 루프도 테이블도 없이 접어 올리는 방식이다(2 → 4 → 8비트). 막히면 마지막에 해도 된다.
5. 네 구현은 **모든 입력에서 같은 값**을 준다. 경계값 0, 1, `0x80000000`, `0xFFFFFFFF`, 교대 패턴을 포함한다.
6. `bit_reverse8(x)` / `bit_reverse32(x)`는 비트 순서를 뒤집는다. 1 → 2 → 4 (→ 8 → 16) 단계로 접는다. 32번 도는 루프는 쓰지 않는다.
7. 비트 반전은 **두 번 적용하면 항등**이고 **popcount를 보존**한다. 테스트로 확인한다.
8. `find_first_set(x)`는 가장 낮은 1 비트의 인덱스(**0 기반**), `find_last_set(x)`는 가장 높은 1 비트의 인덱스다. `x == 0`이면 **둘 다 -1**이다. POSIX `ffs()`는 1 기반이고 0을 "없음"으로 쓴다 — 이 문제는 그 규약을 쓰지 않는다.
9. 두 함수는 절반씩 좁히는 방식으로 **항상 5번만 비교**한다.
10. `lowest_set_bit(x)`는 가장 낮은 1 비트만 남긴 값이다. 두 연산자로 끝난다. `x == 0`이면 0.
11. `is_power_of_two(x)`는 `x & (x - 1)` 관용구를 쓴다. **`x == 0`은 거짓**이다.
12. `next_power_of_two(x)`는 `x` 이상인 가장 작은 2의 거듭제곱이다. `0 -> 1`, `1 -> 1`, 이미 거듭제곱이면 그대로, `0x80000001` 이상은 32비트에 담기지 않으므로 **0을 돌려 "불가"를 알린다**.
13. 컴파일러 내장 함수(`__builtin_popcount`, `__builtin_clz`, `__builtin_ffs`) 금지.

## 인터페이스

```c
static unsigned popcount_naive(uint32_t x);
static unsigned popcount_kernighan(uint32_t x);
static unsigned popcount_table(uint32_t x);
static unsigned popcount_swar(uint32_t x);
static uint8_t  bit_reverse8(uint8_t x);
static uint32_t bit_reverse32(uint32_t x);
static int      find_first_set(uint32_t x);   /* 0 기반, 없으면 -1 */
static int      find_last_set(uint32_t x);    /* 0 기반, 없으면 -1 */
static uint32_t lowest_set_bit(uint32_t x);
static bool     is_power_of_two(uint32_t x);
static uint32_t next_power_of_two(uint32_t x);
```

## 제약

- 동적 할당 금지. 표준 라이브러리는 `assert.h` / `stdbool.h` / `stdint.h` / `stdio.h`만.
- `cc -std=c11 -Wall -Wextra -O2 -g`에서 경고 0개.
- 시프트 카운트는 항상 0..31. 왼쪽으로 미는 대상은 항상 unsigned.
- 테이블은 16바이트를 넘지 않는다(flash 예산 가정).
- 테스트와 난수 헬퍼(`lcg_next`)는 수정하지 않는다.

## 예시 동작

```text
Kernighan: x &= x - 1 은 "가장 낮은 1 비트 하나를 지운다"

  x     = 0101 1000
  x - 1 = 0101 0111      최하위 1이 0이 되고 그 아래가 전부 1이 된다
  AND   = 0101 0000      최하위 1 하나만 사라졌다

  0x5C (0101 1100) -> 0x58 -> 0x50 -> 0x40 -> 0   =  4회 = popcount 4
```

```text
bit_reverse32 다섯 단계 (8비트만 그려 본다)

  입력      a b c d e f g h
  1비트 쌍  b a d c f e h g
  2비트 쌍  d c b a h g f e
  4비트 쌍  h g f e d c b a      <- 8비트 완료
  (32비트는 여기서 8비트 쌍, 16비트 쌍을 한 번 더)
```

```text
next_power_of_two(100)

  x-1 = 99   = 0110 0011
  |= >>1     = 0111 0011
  |= >>2     = 0111 1111        <- 최상위 1 아래가 전부 1로 채워졌다(smear)
  |= >>4..16 = 0111 1111
  +1         = 1000 0000 = 128

  100 -> 128,  128 -> 128,  0 -> 1,  0x80000000 -> 0x80000000,  0x80000001 -> 0
```

## 스스로 점검할 질문

1. 왜 Kernighan 방식이 인터럽트 pending 마스크에는 좋고 암호 코드에는 나쁜가.
2. 니블 테이블 16바이트와 바이트 테이블 256바이트의 trade-off는 무엇인가. 조회 횟수와 flash 크기를 숫자로 말해 보라.
3. `x == 0`을 -1로 돌려주는 것과 32를 돌려주는 것, 각각 호출부에 어떤 버그를 유도하나.
4. `31 - find_last_set(x)`는 무엇인가. ARM의 어떤 명령과 같은가.
5. `x & -x`에서 `-x`는 `x`가 unsigned일 때 왜 UB가 아닌가. signed였다면?
6. `is_power_of_two`에서 `x != 0` 검사를 빼면 어떤 코드가 조용히 깨지나.
7. `next_power_of_two(0x80000001)`에서 0을 돌려주는 것 말고 어떤 API 설계가 가능한가. 각 장단점은?
8. 링버퍼 크기를 2의 거듭제곱으로 강제하면 무엇이 빨라지나. 그 대가는?

## follow-up (면접관이 이어서 물을 것)

1. "Your target has `CLZ`. Rewrite `find_last_set` in one instruction and show the fallback for the core that lacks it."
2. "Reverse the bits of a 4096-entry buffer as fast as you can. What changes?"
3. "How do you count set bits across a 256-bit interrupt pending array?"
4. "Prove your `next_power_of_two` never returns a value smaller than the input."
5. "Which of these would you make `static inline` in a header, and why not all of them?"

---

**해설**: [notes/L0_bit_basics.md](../drills/L0_bit_basics.html) §5, §6 · **답안**: `solutions/07_bit_count_reverse.c`
