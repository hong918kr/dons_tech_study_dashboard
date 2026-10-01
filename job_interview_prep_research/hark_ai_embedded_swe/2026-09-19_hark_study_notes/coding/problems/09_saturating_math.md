# 09. 포화 연산과 Q15 고정소수점

> **주제**: 포화 산술 · 오버플로 검출 · 고정소수점 · **난이도**: 중급 · **목표 시간**: 30분
> **레벨**: L0 비트·정수 기초 (06 → 07 → 08 → 09)
> **해설은 보지 말 것**: 먼저 `starters/09_saturating_math.c`를 채워 `make run N=09`으로 통과시킨다.

## 면접관의 문장

"We are mixing two audio channels. If a sample overflows I do not want it to wrap around, because a wrap turns a loud sample into a full-scale sample of the opposite sign and the user hears a click. So: saturating add and subtract, for unsigned and for signed 32-bit, and for the 16-bit sample type. Detect the overflow first, then clamp. Careful — I will be reading your code for signed overflow, because that is undefined behaviour and at `-O2` the compiler deletes the check you wrote after it. Then a Q15 multiply with rounding, and tell me which single pair of inputs needs saturation. Last one: average two values without dividing and without overflowing."

## 요구사항

1. `u32_to_s32(u)`는 `uint32_t` 비트 패턴을 `int32_t`로 **표준 안에서** 되돌린다. 범위를 벗어난 unsigned → signed 변환은 C11에서 구현 정의이므로 그냥 캐스트하지 않는다.
2. `asr32(x, n)`은 부호를 확장하는 오른쪽 시프트다. `0 <= n <= 31`. C11이 음수의 `>>`를 구현 정의로 남겨 둔 것을 우회한다. `asr32(-7, 1) == -4`(내림)이어야 한다.
3. `sat_add_u32` / `sat_sub_u32`는 각각 `UINT32_MAX`와 `0`에서 멈춘다. unsigned 랩어라운드는 표준이 정의해 두었으므로 결과를 보고 판정한다.
4. `u32_add_overflow(a, b, out)`은 넘쳤는지 돌려주고, `out`이 `NULL`이 아니면 **랩된 합**을 넣는다.
5. `s32_add_overflow(a, b, out)` / `s32_sub_overflow(a, b, out)`은 signed 오버플로를 검출한다. **계산 자체를 `uint32_t`에서** 해야 한다. `out`은 넘치지 않았을 때만 쓴다.
6. `if (a + b > INT32_MAX)` 같은 검사는 금지다. `a + b`가 이미 UB이기 때문이다.
7. `s32_sub_overflow`는 `b`를 부정해 덧셈으로 돌리는 방식을 쓰지 않는다. `b == INT32_MIN`에서 무너진다.
8. `sat_add_s32` / `sat_sub_s32`는 넘쳤을 때 넘친 방향의 끝값(`INT32_MAX` 또는 `INT32_MIN`)으로 clamp한다.
9. `sat_add_s16`은 더 넓은 타입으로 올려 계산한 뒤 clamp한다. `INT16_MAX + 1 == INT16_MAX`, `INT16_MIN - 1 == INT16_MIN`.
10. `q15_mul(a, b)`는 Q15 곱셈이다. Q15는 `int16_t`를 `2^15`로 나눈 값으로 읽는다(범위 `[-1.0, +0.99997]`). 곱은 Q30이므로 15비트 내린다. **반올림**(버릴 비트의 절반을 미리 더한다)을 넣고, 결과를 `int16_t` 범위로 clamp한다.
11. `q15_mul`에서 포화가 필요한 입력은 **정확히 한 쌍**이다. 그게 무엇인지 찾아 테스트로 확인한다.
12. `q15_mul_trunc`는 반올림 없는 버전이다. 두 구현의 차이는 항상 0 또는 1 LSB다. 반올림 버전이 float 참값에 더 가까워야 한다.
13. `avg_u32(a, b)`는 나눗셈 없이 **내림 평균**을 구한다. `a`와 `b`가 둘 다 `UINT32_MAX`여도 넘치지 않는다.
14. `avg_s32(a, b)`도 같은 항등식을 쓰되 시프트가 **산술**이어야 한다. 논리 시프트로 짜면 `a = -1, b = 1`에서 `0`이 아니라 `INT32_MIN`이 나온다.
15. `avg_s32`는 64비트로 계산한 `(a + b) >> 1`과 모든 입력에서 일치한다.
16. 뼈대의 `mix2`와 테스트는 수정하지 않는다.

## 인터페이스

```c
static int32_t  u32_to_s32(uint32_t u);
static int32_t  asr32(int32_t x, unsigned n);

static uint32_t sat_add_u32(uint32_t a, uint32_t b);
static uint32_t sat_sub_u32(uint32_t a, uint32_t b);
static bool     u32_add_overflow(uint32_t a, uint32_t b, uint32_t *out);

static bool     s32_add_overflow(int32_t a, int32_t b, int32_t *out);
static bool     s32_sub_overflow(int32_t a, int32_t b, int32_t *out);
static int32_t  sat_add_s32(int32_t a, int32_t b);
static int32_t  sat_sub_s32(int32_t a, int32_t b);
static int16_t  sat_add_s16(int16_t a, int16_t b);

static int16_t  q15_mul(int16_t a, int16_t b);
static int16_t  q15_mul_trunc(int16_t a, int16_t b);

static uint32_t avg_u32(uint32_t a, uint32_t b);
static int32_t  avg_s32(int32_t a, int32_t b);
```

## 제약

- 동적 할당 금지. 표준 라이브러리는 `assert.h` / `stdbool.h` / `stdint.h` / `stdio.h`만.
- `cc -std=c11 -Wall -Wextra -O2 -g`에서 경고 0개.
- **어디에서도 signed 오버플로를 일으키지 않는다.** `-fwrapv`나 `-fno-strict-overflow`에 의존하지 않는다.
- `float` / `double`은 테스트의 참값 비교에만 쓴다. 구현부에는 쓰지 않는다.
- `__builtin_add_overflow` 계열 금지.
- 64비트 정수는 구현부에 쓰지 않는다(테스트의 참값 계산에만 쓴다). 32비트 코어에서 64비트 연산은 라이브러리 호출이 된다.

## 예시 동작

```text
signed 덧셈 오버플로 검출 — 부호 비트만 본다

  (a ^ s) & (b ^ s) & 0x80000000

  a 와 s 의 부호가 다르고 AND b 와 s 의 부호도 다를 때만 최상위 비트가 1.
  a 와 b 의 부호가 서로 다르면 절대 오버플로할 수 없고, 이 식은 자동으로 0이 된다.

  a = 0x7FFFFFFF (+max), b = 1        -> s = 0x80000000
    a^s = 0xFFFFFFFF,  b^s = 0x7FFFFFFF,  AND & sign = 0x80000000  -> 오버플로
  a = 0x7FFFFFFF,       b = 0x80000000 -> s = 0xFFFFFFFF
    a^s = 0x80000000,  b^s = 0x7FFFFFFF,  AND & sign = 0           -> 정상 (-1)
```

```text
Q15 비트 그림

  a (Q15)   s.fff ffff ffff ffff                    16비트
  b (Q15)   s.fff ffff ffff ffff                    16비트
  a * b     ss.ff ffff ffff ffff ffff ffff ffff ff  32비트 = Q30
            ^^
            부호 비트 2개. 여기서 15비트 내리면 다시 Q15.
            버려지는 하위 15비트의 절반(1 << 14)을 미리 더하면 반올림.

  0.5 x 0.5   : 16384 x 16384 = 0x1000_0000, +2^14, >>15 = 8192   = 0.25
  (-1) x (-1) : -32768 x -32768 = +2^30, >>15 = 32768  -> Q15 에 없다 -> 32767
```

```text
나눗셈 없는 평균

  a + b = (a & b) * 2 + (a ^ b)
    (a & b) = 두 수가 공통으로 가진 비트 = 자리올림이 확실한 부분
    (a ^ b) = 한쪽에만 있는 비트

  => (a + b) / 2 = (a & b) + (a ^ b) / 2 = (a & b) + ((a ^ b) >> 1)

  a = 0xFFFFFFFF, b = 0xFFFFFFFF  ->  0xFFFFFFFF + 0 = 0xFFFFFFFF   (안 넘친다)
  a = -1, b = 1  (signed)         ->  1 + asr(0xFFFFFFFE, 1) = 1 + (-1) = 0
                                      논리 시프트였다면 1 + 0x7FFFFFFF = INT32_MIN
```

## 스스로 점검할 질문

1. `if (a + b < a)`가 unsigned에서는 맞고 signed에서는 틀린 이유는 무엇인가.
2. `-O2`에서 컴파일러가 signed 오버플로 검사를 지워 버리는 과정을 한 문장으로 설명하라.
3. `-INT32_MIN`이 왜 표현 불가인가. 그래서 뺄셈을 따로 쓰는 이유는?
4. C11에서 범위를 벗어난 unsigned → signed 변환은 UB인가 구현 정의인가. C23에서는 무엇이 바뀌었나.
5. 음수의 `>>`는 무엇이 보장되고 무엇이 안 되나. 실무에서 gcc/clang은 무엇을 하나.
6. Q15에서 `+1.0`을 표현할 수 없는데도 이 포맷을 쓰는 이유는 무엇인가. Q14로 바꾸면 무엇이 좋고 나빠지나.
7. 반올림을 빼면 오디오에서 구체적으로 무엇이 들리나. 왜 "DC offset"인가.
8. 랩어라운드 믹싱이 포화 믹싱보다 나쁜 이유를 파형으로 설명하라.
9. `avg_u32`가 `(a + b) >> 1`보다 느릴 수도 있다. 그래도 쓰는 이유는?
10. ARM에 `QADD` / `SSAT` 명령이 있는데 왜 손으로 짜 보는가.

## follow-up (면접관이 이어서 물을 것)

1. "Show me the Cortex-M4 DSP instruction that does your `sat_add_s16` in one cycle."
2. "Your filter accumulates 64 Q15 samples. Where do you keep the accumulator, and in what format?"
3. "Now do a saturating multiply for `int32_t` x `int32_t`. What is the accumulator width?"
4. "How would you unit-test a fixed-point filter against a float reference, and what tolerance do you accept?"
5. "Where in a real audio path do you clip, and where do you let it ride into a wider type instead?"

---

**해설**: [notes/L0_bit_basics.md](../drills/L0_bit_basics.html) §9, §10 · **답안**: `solutions/09_saturating_math.c`
