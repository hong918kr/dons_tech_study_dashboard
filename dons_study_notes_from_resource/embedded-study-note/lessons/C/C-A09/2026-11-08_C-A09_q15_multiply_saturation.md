---
id: C-A09
time: 90분
updated: 2026-11-08
---
# C-A09 고정소수점 맛보기: Q15 곱셈과 포화 연산

## 학습 목표

- Q15(Q1.15) 표현의 범위·해상도·비대칭(−1 은 있고 +1 은 없다)을 설명하고, `double ↔ Q15` 변환을 **반올림·포화·NaN** 까지 정확하게 구현할 수 있다.
- `q15_mul` 을 "32비트 곱(Q30) → `+0x4000` → floor `>> 15` → 포화" 로 쓰고, **−1 × −1**, 음수 동점, 절단(truncate) 바이어스 −0.5 LSB 를 테스트로 보일 수 있다.
- Q15 코드에 숨은 C 함정 — 음수 `>>`(구현 정의), 범위 밖 정수→`int16_t`(구현 정의), 음수 `<<`·signed overflow·범위 밖 실수→정수(UB) — 을 구분하고 피하는 코드를 쓴다 (C11 6.3.1.3, 6.3.1.4, 6.5.7).
- 펌웨어 파이프라인(12비트 ADC DMA → 게인·믹스 → 12비트 DAC)에서 **누산기 폭**과 **반올림 횟수**를 설계하고, double 오라클로 비트 단위(bit-exact) 검증한다.
- Cortex-M4 의 DSP 확장 명령(`SMLABB`, `QADD16`, `SMLALBB`, `SSAT`)이 C 코드에서 실제로 나오는 모습을 보고, M0+ 와 비교해 설명한다.

## 1. 문제

FPU 가 없는 MCU(Cortex-M0/M0+/M3)나 FPU 가 있어도 결과가 비트 단위로 재현돼야 하는 오디오·모터 제어·센서 필터에서는 실수를 **정수로 흉내 낸다** — 그 기본 단위가 Q15 이고, 펌웨어 면접에서 "Q15 곱셈 짜 보세요" 다음 꼬리질문은 거의 항상 "**−1 × −1 은요?**" 다.

Q15 는 `int16_t q` 가 실수 `q / 2^15` 를 뜻하는 약속이다. 비트로 보면 부호 비트 1개 + 소수 비트 15개, 정수 비트는 없다 (그래서 Q0.15 라고도 부른다).

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Q15: int16_t q ↔ 실수 q / 32768 — 범위는 [-1, 1 - 2^-15], 1 LSB = 2^-15 ≈ 3.05e-5</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="40" height="30" class="f-warn"/>
    <rect x="60" y="40" width="40" height="30" class="f-acc"/>
    <rect x="100" y="40" width="40" height="30" class="f-acc"/>
    <rect x="140" y="40" width="40" height="30" class="f-acc"/>
    <rect x="180" y="40" width="200" height="30" class="f-mut"/>
    <rect x="380" y="40" width="40" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="28" y="60">-2^0</text><text x="66" y="60">2^-1</text><text x="106" y="60">2^-2</text>
    <text x="146" y="60">2^-3</text><text x="250" y="60">. . .</text><text x="384" y="60">2^-15</text>
  </g>
  <text x="440" y="54" fill="currentColor">bit15 의 무게는 -1 (2의 보수)</text>
  <text x="440" y="72" fill="currentColor">나머지 15비트가 소수부</text>
  <line x1="40" y1="150" x2="700" y2="150" stroke="currentColor"/>
  <g stroke="currentColor">
    <line x1="60" y1="140" x2="60" y2="160"/><line x1="370" y1="140" x2="370" y2="160"/>
    <line x1="676" y1="140" x2="676" y2="160"/><line x1="680" y1="140" x2="680" y2="160"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="30" y="128">-1.0 = 0x8000</text><text x="340" y="128">0 = 0x0000</text>
    <text x="520" y="128">1 - 2^-15 = 0x7FFF</text>
    <text x="30" y="182">-32768</text><text x="362" y="182">0</text><text x="640" y="182">32767</text>
  </g>
  <rect x="690" y="140" width="40" height="20" class="f-warn" stroke="currentColor"/>
  <text x="694" y="155" fill="currentColor" font-size="12">+1 ✗</text>
  <rect x="10" y="200" width="720" height="78" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="222" fill="currentColor">음수 쪽이 하나 더 많다: -1 은 표현되지만 +1 은 안 된다.</text>
  <text x="22" y="244" fill="currentColor">그래서 (-1) × (-1) = +1, -(-1) = +1, |(-1)| = +1 은 모두 범위 밖 → 포화(saturation)로 0x7FFF.</text>
  <text x="22" y="266" fill="currentColor">포화 = 넘치면 감기지(wrap) 말고 끝값에 붙인다. 감기면 오디오는 "딱", 모터는 토크 부호가 뒤집힌다.</text>
</svg>
```

이 레슨은 **맛보기**다. 서로 다른 Q 형식 혼합, guard bit 계산, 전수(2^32) 검사, FIR/biquad 는 DSP 트랙(DSP-B01~B04, DSP-B08, DSP-M05)이 맡는다. 여기서는 C 언어 관점의 정확성 — 반올림·포화·UB/구현 정의 — 에 집중한다. 정수 승격(C-B02), 시프트 함정(C-B03), UB 카탈로그(C-M12)는 안다고 가정한다.

### Part A — 변환 · 곱셈 · 포화 덧셈

| 함수 | 규칙 |
|---|---|
| `int32_t floor_shr(int32_t x, unsigned k)` | `floor(x / 2^k)`, 1 ≤ k ≤ 30. 음수 `>>` 를 쓰지 않는다 |
| `q15_t sat_q15(int32_t x)` | 범위 밖이면 `INT16_MAX` / `INT16_MIN` |
| `q15_t q15_from_double(double x)` | 가장 가까운 값, 동점은 0 에서 먼 쪽(`lround` 와 같음), 포화, NaN → 0 |
| `q15_t q15_mul(q15_t a, q15_t b)` | 32비트 곱 → `+0x4000` → floor `>> 15` → 포화 (동점은 +∞ 쪽, "round half up") |
| `q15_t q15_add_sat(q15_t a, q15_t b)` | 포화 덧셈 |

### Part B — 경계: −1 의 짝이 없다

- `q15_sub_sat`, `q15_neg_sat`, `q15_abs_sat` — `0 − (−1)`, `−(−1)`, `|−1|` 이 모두 0x7FFF.
- 테스트가 찌르는 곳: `q15_mul(−32768, −32768)`, 음수 동점과 "동점 바로 옆"(`−113 × 145`), `q15_from_double` 의 1.0 / ±∞ / NaN / 정확히 0.5 LSB / **0.5 LSB 바로 아래**(고전적 `+0.5` 버그).

### Part C — ADC DMA → 게인·믹스 → DAC

STM32 류 MCU 에서 흔한 구성: 12비트 ADC(마이크)가 DMA 원형 버퍼를 채우고, 절반 완료(HT)·전체 완료(TC) 인터럽트마다 그 절반을 처리해 12비트 DAC 로 내보낸다. 처리 내용은 `y = mic × g_mic + tone × g_tone` (톤은 Q15 사인 LUT).

- `q15_t adc12_to_q15(uint16_t raw)` — 오른쪽 정렬 12비트 offset-binary(0…4095, 중앙 2048) → Q15. 위 4비트는 무시.
- `uint16_t q15_to_dac12(q15_t q)` — Q15 → 0…4095, 가장 가까운 값(동점은 위로), 포화.
- `q15_t mix_sample(mic, tone, cfg, bool *clipped)` — 곱 두 개를 **넓게 더하고 반올림은 한 번**, 그다음 포화. 포화했으면 `*clipped = true`.
- `size_t mix_block(adc, tone, dac, n, cfg)` — n 샘플 처리, 포화된 샘플 수를 돌려준다 (현장에선 이 카운터를 텔레메트리로 올린다).

## 2. Problem (English)

> **Q15 fixed-point basics: conversion, multiply with rounding, saturating add — and a small ADC→mix→DAC pipeline.**
>
> Q15 represents the real value `q / 32768` with `int16_t q`; range `[-1, 1 - 2^-15]`.
>
> 1. `int32_t floor_shr(int32_t x, unsigned k)` — `floor(x / 2^k)` for `1 <= k <= 30` without relying on implementation-defined right shift of negative values. `q15_t sat_q15(int32_t)`. `q15_t q15_from_double(double x)` — round to nearest, ties away from zero, saturate, NaN → 0. `q15_t q15_mul(q15_t a, q15_t b)` — round half up (`+0x4000`, floor shift by 15), saturate. `q15_t q15_add_sat(q15_t, q15_t)`.
> 2. `q15_sub_sat`, `q15_neg_sat`, `q15_abs_sat`. Handle `-1 * -1`, `-(-1)`, `|-1|`.
> 3. `q15_t adc12_to_q15(uint16_t raw)` (12-bit offset binary, ignore bits 15:12), `uint16_t q15_to_dac12(q15_t)` (round, clamp to 4095), `q15_t mix_sample(q15_t mic, q15_t tone, const mix_cfg_t *cfg, bool *clipped)` computing `mic*g_mic + tone*g_tone` with a single rounding step, and `size_t mix_block(...)` returning the number of clipped samples. It is called from DMA half/full-transfer callbacks.
>
> Example: `q15_mul(16384, 16384) == 8192`, `q15_mul(-32768, -32768) == 32767`, `q15_mul(3, 16384) == 2` (1.5 LSB tie → up), `q15_mul(-113, 145) == -1`, `q15_from_double(0x1.fffffffffffffp-17) == 0`, `q15_to_dac12(32767) == 4095`.
>
> Constraints: no UB, no implementation-defined behaviour in your code; `-Wall -Wextra -Werror` clean. Tests compare against a `double` oracle (bit-exact) and against error bounds in LSB.
>
> Follow-ups: why does truncation bias the result? Which ARM instructions implement this? How wide must the accumulator be? How would you test all inputs?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "6/52 passed" — 주어진 naive 코드의 버그 재현 테스트 등은 통과, TODO 를 채워 52/52 를 만든다
```

시작 코드에 **주어진** 함수 세 개는 비교·재현용이다: `q15_mul_naive` (흔히 보는 한 줄, 포화 없음), `q15_mul_trunc` (절단 곱), `mix_sample_2round` (곱마다 반올림하는 믹스). naive 버전은 구현 정의 동작 두 가지에 기대므로 파일 위에 `_Static_assert` 두 줄로 **가정을 컴파일 타임에 못 박아** 두었다 (C-A08 의 기법). 다른 컴파일러에서 가정이 틀리면 빌드가 실패한다.

관찰용 파일 두 개 (하네스와 별개):

```bash
# 함정 6개 + 오차 통계 (일부러 UB 포함 → -Werror 없이)
cc -std=c11 -O0 demo_q15_pitfalls.c -o dp0 && ./dp0
cc -std=c11 -O2 demo_q15_pitfalls.c -o dp2 && ./dp2
cc -std=c11 -g -O1 -fsanitize=undefined demo_q15_pitfalls.c -o dpu && ./dpu
# Q15 연산이 어떤 명령이 되나 (freestanding)
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_q15_m4.c -o -
clang --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -mthumb -O2 -S demo_q15_m4.c -o -
cc -O2 -S demo_q15_m4.c -o -
clang --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -O2 -S demo_q15_m4.c -o -
```

## 4. 힌트

<details>
<summary>힌트 1 — floor_shr: / 와 % 로 floor 만들기</summary>

C11 6.5.5p6: 정수 나눗셈은 **0 쪽으로** 버린다. `-7 / 2 == -3` 인데 floor 는 `-4`. 몫이 0 쪽으로 버려졌다는 신호는 나머지가 음수라는 것(`-7 % 2 == -1`)이다. 그럴 때만 몫에서 1 을 빼면 floor. 컴파일러는 이걸 `asr` 한 개로 바꾼다 (5. 풀이에서 확인). C-B03 의 `~(~x >> k)` 방식도 맞다.

</details>

<details>
<summary>힌트 2 — q15_mul: 곱의 형식과 크기</summary>

Q15 × Q15 = Q30. `int16_t` 끼리 곱하면 `int` 로 승격되니 32비트 `int` 에선 넘치지 않지만, **16비트 `int` 플랫폼(MSP430, AVR)에선 곱이 `int` 범위를 넘어 UB** 다 → `(int32_t)a * b`. Q30 → Q15 는 2^15 로 나누기. 반올림은 그 전에 0.5 LSB(= 2^14 = `0x4000`)를 더하고 floor. 마지막으로 `int32_t` 결과가 Q15 범위인지 — 어떤 입력이 범위를 넘을까?

</details>

<details>
<summary>힌트 3 — q15_from_double: 변환 "전에" 검사, 그리고 +0.5 의 함정</summary>

실수 → 정수 변환에서 정수부가 대상 타입에 안 들어가면 **UB** (6.3.1.4p1) — 구현 정의가 아니다. 그러니 `s = x * 32768.0` 을 먼저 범위 검사(`s >= 32767.5` / `s <= -32768.5`)하고 나서 변환한다. NaN 은 모든 비교가 거짓이라 검사를 그냥 통과하므로 맨 앞에서 `x != x` 로 거른다. 반올림을 `(int)(s + 0.5)` 로 하면 `s = 0.5 - 2^-54` 에서 `s + 0.5` 가 double 반올림으로 정확히 1.0 이 된다. 대신 `t = (int32_t)s` 후 `s - t` (정확한 소수부)를 0.5 와 비교한다.

</details>

<details>
<summary>힌트 4 — mix_sample: 누산기 폭</summary>

곱 하나는 |p| ≤ 2^30 이라 `int32_t` 에 들어간다. 그런데 두 곱의 합은 최대 2^30 + 2^30 = **2^31** (`mic = tone = g_mic = g_tone = −32768`) → `int32_t` 덧셈은 signed overflow(UB). 합만 `int64_t` 로. 곱을 `int64_t` 로 하면 정답이긴 하지만 M0+ 에서 비싸다 (풀이의 asm 참고). 반올림은 합한 **다음** 한 번만.

</details>

## 5. 풀이

### 0. Q 형식 한 장 요약

| 형식 | 저장 타입 | 값 | 범위 | 1 LSB | 곱의 형식 |
|---|---|---|---|---|---|
| Q15 (Q1.15) | `int16_t` | q / 2^15 | [−1, 1 − 2^−15] | 3.05e−5 (−90.3 dB) | Q30 in `int32_t` |
| Q31 (Q1.31) | `int32_t` | q / 2^31 | [−1, 1 − 2^−31] | 4.66e−10 | Q62 in `int64_t` |
| Q7.8 | `int16_t` | q / 2^8 | [−128, 128 − 2^−8] | 0.0039 | Q14.16 in `int32_t` |

규칙은 하나다: Qm.n × Qp.r = Q(m+p).(n+r). 형식 혼합과 guard bit 는 DSP-B04. 이 레슨은 Q15 하나만.

### Part A — 변환: 범위 검사가 변환보다 먼저

```c
static q15_t q15_from_double(double x) {
    if (x != x) return 0;                    /* NaN: 비교가 전부 거짓이라 아래 검사를 통과해 버린다 */
    double s = x * 32768.0;                  /* 2의 거듭제곱 배: 지수만 바뀌어 정확 */
    if (s >= 32767.5) return Q15_MAX;        /* +∞, 1.0, 1e9 ... */
    if (s <= -32768.5) return Q15_MIN;
    int32_t t = (int32_t)s;                  /* 이제 범위 안 → 정의된 변환 (0 쪽으로 버림) */
    double frac = s - t;                     /* 소수부: 정확히 계산된다 */
    if (frac >= 0.5) t++; else if (frac <= -0.5) t--;
    return sat_q15(t);                       /* 32767.5 직전 값이 32768 로 올라갈 일은 없지만 방어 */
}
```

경계 테스트 표 (모두 checked):

| 입력 x | s = x·2^15 | 결과 | 포인트 |
|---|---|---|---|
| 0.1 | 3276.8 | 3277 | 일반 반올림 |
| `0x1p-16` / `-0x1p-16` | ±0.5 | 1 / −1 | 동점은 0 에서 먼 쪽 (`lround` 와 같음, C11 7.12.9.7) |
| `0x1.fffffffffffffp-17` | 0.5 − 2^−54 | **0** | `(int)(s + 0.5)` 는 1 (아래) |
| 32767.25/32768 | 32767.25 | 32767 | 범위 안 |
| 32767.5/32768, 1.0, 1e9, +∞ | ≥ 32767.5 | 32767 | 포화 |
| −32767.75/32768 | −32767.75 | −32768 | 반올림 결과가 정확히 −1 |
| −1.0, −∞, −32768.5/32768 | ≤ −32768.5 | −32768 | 포화 |
| NaN, −0.0 | — | 0 | NaN 정책은 명세에 적는다 |

**고전적 `+0.5` 버그:** `s = 0.49999999999999994` (= 0.5 − 2^−54) 에 0.5 를 더하면 참값 `1 − 2^−54` 는 1 바로 아래 double 간격(2^−53)의 정중앙이라 짝수 쪽인 **1.0** 으로 반올림된다 → `(int)1.0 = 1`. 정답은 0. 소수부를 따로 구해 비교하면 덧셈 반올림이 끼어들 틈이 없다. 실측 (`demo_q15_pitfalls.c` ④):

```text
4) +0.5   q15(0.5 LSB - 2^-69) = 1   (정답 0)
```

테스트는 추가로 [−1.1, 1.1) 의 무작위 double 10만 개를 `lround` 기반 오라클과 비교한다 (불일치 0). 실무 MCU 에선 이 변환이 **부팅 때 계수 테이블을 만들 때** 나 오프라인 툴에서 쓰이고, 실시간 경로에는 거의 나오지 않는다 — 그래서 속도보다 정확성이 중요하다.

### Part A — floor 시프트: 구현 정의를 피하되 반올림은 지킨다

| 쓰는 법 | `-7 → k=1` | 표준상 | 비고 |
|---|---|---|---|
| `x >> k` | −4 (clang/gcc) | **구현 정의** (6.5.7p5) | 거의 모든 컴파일러가 산술 시프트지만 보장 아님 |
| `x / (1 << k)` | **−3** | 정의됨 | 0 쪽 버림 → floor 가 아니다 |
| `x < 0 ? ~(~x >> k) : x >> k` | −4 | 정의됨 | C-B03 `sar32`. `~x ≥ 0` 이라 `>>` 가 정의됨 |
| `q = x / d; if (x % d < 0) q--;` | −4 | 정의됨 | 이 레슨의 `floor_shr` |

정의된 두 방식은 공짜인가? `demo_q15_m4.c` 에서 ①(/ % 방식), ②(그냥 `>>`), ⑥(`~` 방식)을 비교한 실제 Cortex-M4 출력 — **세 함수가 명령 하나까지 똑같다**:

```text
q15_mul:                    q15_mul_rawshift:           q15_mul_tilde:
    mov.w   r2, #16384          mov.w   r2, #16384          mov.w   r2, #16384
    smlabb  r1, r1, r0, r2      smlabb  r1, r1, r0, r2      smlabb  r1, r1, r0, r2
    asrs    r2, r1, #15         asrs    r2, r1, #15         asrs    r2, r1, #15
    movw    r0, #32767          movw    r0, #32767          movw    r0, #32767
    cmp     r2, r0              cmp     r2, r0              cmp     r2, r0
    it      lt                  it      lt                  it      lt
    asrlt   r0, r1, #15         asrlt   r0, r1, #15         asrlt   r0, r1, #15
    bx      lr                  bx      lr                  bx      lr
```

이식 가능한 코드를 써도 비용이 0 이다. 그러니 "어차피 산술 시프트니까" 에 기댈 이유가 없다.

### Part A — `q15_mul` 의 데이터 경로

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">q15_mul(a, b): 폭이 어떻게 변하나 (괄호 안: -1 × -1 일 때)</text>
  <g stroke="currentColor">
    <rect x="20" y="44" width="110" height="34" rx="4" class="f-acc"/>
    <rect x="20" y="92" width="110" height="34" rx="4" class="f-acc"/>
    <rect x="170" y="66" width="140" height="40" rx="4" class="f-mut"/>
    <rect x="345" y="66" width="110" height="40" rx="4" class="f-mut"/>
    <rect x="490" y="66" width="110" height="40" rx="4" class="f-mut"/>
    <rect x="635" y="66" width="95" height="40" rx="4" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="30" y="66">a: Q15 (16b)</text><text x="30" y="114">b: Q15 (16b)</text>
    <text x="178" y="84">(int32_t)a * b</text><text x="178" y="100" font-size="12">Q30 (-0x8000 × -0x8000)</text>
    <text x="353" y="84">+ 0x4000</text><text x="353" y="100" font-size="12">+0.5 LSB</text>
    <text x="498" y="84">floor &gt;&gt; 15</text><text x="498" y="100" font-size="12">→ int32</text>
    <text x="643" y="84">sat → Q15</text><text x="643" y="100" font-size="12">0x7FFF</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M130 61 L170 80"/><path d="M130 109 L170 92"/>
    <path d="M310 86 L345 86"/><path d="M455 86 L490 86"/><path d="M600 86 L635 86"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="178" y="136">0x40000000 (= 2^30)</text>
    <text x="353" y="136">0x40004000</text>
    <text x="498" y="136">32768 ✗</text>
    <text x="643" y="136">32767</text>
  </g>
  <rect x="10" y="156" width="720" height="132" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="22" y="178">곱의 범위: [-32768·32767, 32768²] = [-1073709056, 2^30]. 2^30 하나만 빼면 bit31 = bit30.</text>
    <text x="22" y="200">floor 후 범위: [-32768, 32768]. 아래쪽은 절대 안 넘치고, 위쪽은 오직 a = b = -32768 일 때 32768.</text>
    <text x="22" y="222">포화 없이 (int16_t)32768 로 자르면 구현 정의(6.3.1.3p3) → clang/gcc: "-1 × -1 = -1".</text>
    <text x="22" y="244">그래서 컴파일러는 위쪽 비교만 남긴다 (asm 의 cmp 하나). 아래쪽 검사는 죽은 코드라 지워졌다.</text>
    <text x="22" y="266">Q31 이면 같은 이야기가 Q62 / int64_t 에서 반복된다 (추가 연습 1).</text>
  </g>
</svg>
```

**왜 0x4000 을 더하나 — 절단 바이어스.** `>> 15` 만 하면(= floor) 결과는 항상 참값 이하, 오차는 (−1, 0] LSB, 평균 약 −0.5 LSB 다. 한 번은 작아도 IIR 필터·적분기처럼 결과를 다시 입력으로 쓰는 루프에서는 매 샘플 같은 방향으로 쌓여 **DC 오프셋**이 생긴다. `+0x4000` 후 floor 면 오차는 (−0.5, +0.5] LSB, 평균은 거의 0 (동점 1/32768 확률에서만 +쪽). 실측 100만 쌍 (`demo_q15_pitfalls.c` ⑦):

```text
7) mean err: trunc -0.4996 LSB, round -0.0002 LSB  (N=1000000)
```

(round 의 평균이 정확히 0 이 아닌 건 표본 잡음과 −1×−1 포화 때문이다.) checked 테스트는 같은 측정을 20만 쌍으로 해서 `trunc < −0.45`, `|round| < 0.01` 을 확인하고, 모든 쌍이 double 오라클과 **비트 단위로 일치**하며 |오차| ≤ 0.5 LSB 임을 본다 (−1×−1 만 1 LSB). 오라클이 정확한 이유: `a/32768`, `b/32768` 은 정확하고 그 곱은 가수 30비트라 double(53비트)에 정확히 들어간다. 동점을 짝수 쪽으로 보내는 RNE(round half to even)는 추가 연습 2.

**포화 덧셈** 은 C-B02 Part C 의 `sat_add_i16` 그대로다: `int16_t` 둘의 합은 `int` 로 승격돼 ±65535 를 넘지 않으니 넓은 쪽에서 더하고 자른다. 흥미로운 건 Cortex-M4 출력:

```text
q15_add_sat:
    qadd16  r0, r0, r1      ; 16비트 2개를 병렬로 포화 덧셈 (SIMD) — 아래 절반만 쓴다
    sxth    r0, r0
    bx      lr
```

clang 이 "넓혀 더하고 clamp" 패턴을 알아보고 DSP 확장 명령 `QADD16` 하나로 바꿨다. M0+ 는 DSP 확장이 없어 `adds` + 비교 2번 + 분기 2번이다 (아래 asm 절).

### Part B — 경계: 무엇이 언제 틀리나

| 입력 | 정답 | 흔한 오답 | 오답의 원인 |
|---|---|---|---|
| `q15_mul(-32768, -32768)` | 32767 | −32768 | 포화 없음 + 구현 정의 축소 변환 |
| `q15_mul(-113, 145)` (p = −16385 = −0.50003 LSB) | −1 | **0** | `>>` 대신 `/` → 0 쪽 버림 |
| `q15_mul(-128, 128)` (p = −16384 = 정확히 −0.5 LSB) | 0 | — | round half up: 동점은 위로 |
| `q15_mul(-3, 16384)` (−1.5 LSB) | −1 | −2 | 동점 규칙을 "0 에서 멀리" 로 착각 |
| `q15_sub_sat(0, -32768)` | 32767 | −32768 | 32768 이 감김 |
| `q15_neg_sat(-32768)` / `q15_abs_sat(-32768)` | 32767 | −32768 | 같은 이유. `abs()` 도 `INT_MIN` 에서 UB (7.22.6.1) |
| `q15_from_double(1.0)` | 32767 | −32768 또는 32768 | 범위 밖 실수→정수 = UB |

`q15_neg_sat(a) = sat_q15(-(int32_t)a)` 처럼 **넓힌 다음** 부정하는 게 핵심이다. `-a` 를 `int16_t` 그대로 쓰면 승격 덕에 32768 이 되긴 하지만, 그걸 다시 `int16_t` 에 넣는 순간 구현 정의 변환으로 감긴다.

**UB 냐 구현 정의냐 — 실측.** `demo_q15_pitfalls.c` 를 −O0 / −O2 / UBSan 으로 돌린 실제 출력:

```text
=== -O0 ===
1) naive  (-1)*(-1)            = -32768
2) div    (-113)*145 (-0.50003 LSB) = 0   (floor 버전은 -1)
3) cast   q15(1.0)             = -32768
4) +0.5   q15(0.5 LSB - 2^-69) = 1   (정답 0)
5) adc    (100-2048)<<4        = -31168
6) acc32  (-1)(-1)+(-1)(-1)    = -2147483648   (참값 2^31 = 2147483648)
=== -O2 (다른 줄은 같음) ===
3) cast   q15(1.0)             = 32768
=== -fsanitize=undefined ===
demo_q15_pitfalls.c:17:51: runtime error: 32768 is outside the range of representable values of type 'short'
demo_q15_pitfalls.c:22:81: runtime error: left shift of negative value -1948
demo_q15_pitfalls.c:24:28: runtime error: signed integer overflow: 1073741824 + 1073741824 cannot be represented in type 'int'
(각 보고 뒤의 SUMMARY 줄과, 보고 뒤에 이어지는 1)~8) 출력은 생략 — 값은 -O2 와 같았다)
```

| # | 코드 | 표준상 분류 | UBSan | 결과 |
|---|---|---|---|---|
| 1 | `(q15_t)((a*b + 0x4000) >> 15)` | 구현 정의 ×2 (6.5.7p5, 6.3.1.3p3) | 침묵 | −1×−1 = −1. **테스트만 잡는다** |
| 2 | `/ 32768` 로 floor 대신 | 정의됨 — 그냥 틀린 반올림 | 침묵 | 0 |
| 3 | `(q15_t)(x * 32768.0)` | **UB** (6.3.1.4p1) | 보고 | −O2 에서 `int16_t` 가 **32768** 을 들고 있다 |
| 4 | `(int)(s + 0.5)` | 정의됨 — double 반올림 | 침묵 | 1 |
| 5 | `(raw - 2048) << 4` | **UB** (6.5.7p4: 음수 `<<`) | 보고 | 값은 −31168 로 "맞아 보임" |
| 6 | `int32` 로 두 곱 합 | **UB** (6.5p5: signed overflow) | 보고 | 양수 2^31 이 음수 최솟값으로 |

3번이 이 레슨에서 가장 무서운 관찰이다. −O2 asm 을 보면:

```text
    ldr     d0, [x8, _v_one@PAGEOFF]
    fcvtzs  w8, d0, #15        ; ×2^15 와 변환을 고정소수점 변환 명령 하나로 (32비트 결과)
    sxtw    x8, w8             ; printf 로 넘길 64비트 확장 — 16비트 자르기(sxth)가 없다
```

변환 결과가 `int16_t` 에 들어간다고 **가정해도 되므로**(안 들어가면 UB니까) 컴파일러는 16비트로 자르는 `sxth` 를 생략했다. −O0 에선 `fcvtzs w8, d0` 뒤에 `sxth w0, w8` 이 있어 −32768 이 나왔다. 같은 소스, 최적화 수준만 바꿨는데 "`int16_t` 변수에 32768" 이라는 불가능한 값이 나온다. 1·2·4번은 UB 가 아니라서 sanitizer 가 **아무 말도 하지 않는다** — Q 형식 코드는 double 오라클 테스트가 유일한 그물이다.

참고: C23 에서 2의 보수 표현이 필수가 됐지만, 음수 `>>` 와 범위 밖 정수→signed 변환은 여전히 구현 정의로 남았다. C++20 은 둘 다 정의했다(산술 시프트, 모듈로 2^N). 펌웨어 C 코드에서는 이 레슨처럼 쓰거나, 쓸 거라면 `_Static_assert((-1 >> 1) == -1, ...)` 로 가정을 문서화한다.

### Part C — 파이프라인 설계: 폭과 반올림 횟수

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">12비트 ADC → Q15 → 게인·믹스 (64비트 누산, 반올림 1번) → 12비트 DAC</text>
  <g stroke="currentColor">
    <rect x="10" y="44" width="150" height="60" rx="4" class="f-mut"/>
    <rect x="12" y="74" width="72" height="26" class="f-acc"/>
    <rect x="86" y="74" width="72" height="26" class="f-ok"/>
    <rect x="200" y="44" width="120" height="36" rx="4" class="f-mut"/>
    <rect x="200" y="96" width="120" height="36" rx="4" class="f-mut"/>
    <rect x="360" y="64" width="150" height="50" rx="4" class="f-acc"/>
    <rect x="545" y="64" width="85" height="50" rx="4" class="f-warn"/>
    <rect x="655" y="64" width="75" height="50" rx="4" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="62">ADC DMA 원형 버퍼</text>
    <text x="18" y="92" font-size="12">HT: 앞 절반</text><text x="92" y="92" font-size="12">TC: 뒤 절반</text>
    <text x="208" y="60" font-size="12">(raw&amp;0xFFF)-2048</text><text x="208" y="75" font-size="12">× 16 → mic Q15</text>
    <text x="208" y="112" font-size="12">tone LUT (Q15)</text><text x="208" y="127" font-size="12">사인파</text>
    <text x="368" y="84" font-size="12">p1 = mic·g_mic (i32)</text><text x="368" y="100" font-size="12">p2 = tone·g_tone (i32)</text>
    <text x="553" y="84" font-size="12">+0x4000</text><text x="553" y="100" font-size="12">floor&gt;&gt;15, sat</text>
    <text x="663" y="84" font-size="12">+32768</text><text x="663" y="100" font-size="12">/16 → DAC</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M160 70 L200 62"/><path d="M320 62 L360 80"/><path d="M320 114 L360 98"/>
    <path d="M510 89 L545 89"/><path d="M630 89 L655 89"/>
  </g>
  <text x="368" y="132" fill="currentColor" font-size="12">acc = (i64)p1 + p2 : 최대 2^31</text>
  <text x="545" y="132" fill="currentColor" font-size="12">clip++</text>
  <rect x="10" y="150" width="720" height="140" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="22" y="172">① 곱 하나는 |p| ≤ 2^30 → int32 OK. 합은 2^31 까지 → int32 면 overflow(UB). 합만 int64.</text>
    <text x="22" y="194">② 반올림은 합한 뒤 한 번. 곱마다 반올림하면 오차가 0.5 + 0.5 → 최악 약 1 LSB (실측 0.9999).</text>
    <text x="22" y="216">③ 포화도 마지막에 한 번. 중간 합이 범위를 넘어도 최종이 범위 안이면 정답 (예: +0.9 + 0.6 - 0.7).</text>
    <text x="22" y="238">④ DAC: (q + 32768) 은 0..65535, +8 후 &gt;&gt; 4. 65528 이상은 4096 이 되므로 4095 로 포화.</text>
    <text x="22" y="260">⑤ HT 콜백이 앞 절반을 처리하는 동안 DMA 는 뒤 절반을 채운다 → 처리 시간 &lt; 절반 버퍼 시간.</text>
    <text x="22" y="282">clip 수는 버리지 말고 센다: 게인 설정 오류의 가장 싼 진단 신호.</text>
  </g>
</svg>
```

**ADC → Q15.** 12비트 offset-binary 를 부호 있는 값으로 바꾸고(−2048…2047) 2^4 배 하면 Q15 의 위 12비트를 채운다. `<< 4` 를 쓰면 안 되는 이유는 위 표 5번: 음수의 왼쪽 시프트는 UB (6.5.7p4)이고, UBSan 이 `left shift of negative value -1948` 로 잡았다. `* 16` 은 정의된 곱이고 컴파일러는 같은 `lsl` 을 낸다. `& 0x0FFF` 는 데이터 레지스터 위 비트에 다른 정보가 실리는 ADC(채널 번호, 정렬 옵션)를 대비한 것이다. 참고로 STM32 ADC 의 **왼쪽 정렬(ALIGN=1)** 모드면 12비트가 이미 bit15:4 에 있으므로 `raw − 32768` 만 하면 Q15 다 — 하드웨어 설정이 코드의 형식을 바꾼다. 다른 해상도(8~16비트)는 추가 연습 4.

**누산기 폭.** 테스트의 극단값: `mic = tone = −32768`(raw 0), `g_mic = g_tone = −32768` → 두 곱의 합 = 2^31 → 결과 32767, `clipped = true`. `demo_q15_pitfalls.c` 6번이 `int32` 누산의 실제 결과 `-2147483648` 와 UBSan 보고를 보여 준다. 포화가 안 일어나는 게 아니라 **부호가 뒤집혀** 반대쪽 끝으로 간다 — 오디오에선 최대 볼륨 클릭, 모터 제어에선 최대 역토크다.

곱을 어느 폭에서 할지는 asm 에서 차이가 크다. `demo_q15_m4.c` ④(`(int64_t)m * gm + ...`)와 ⑦(곱은 `int32_t`, 합만 `int64_t`) 실제 출력:

```text
@ Cortex-M4, ⑦ mix2_p32                    @ Cortex-M0+, ④ mix2_div (곱을 int64 로)
    muls    r2, r3, r2                          push    {r4, r5, r6, r7, lr}
    asrs    r3, r2, #31                         ...
    smlalbb r2, r3, r1, r0   ; 64비트 += 16×16   bl      __aeabi_lmul      ; 64×64 곱 라이브러리 호출
    adds.w  r0, r2, #16384                      ...
    adc     r1, r3, #0                          bl      __aeabi_lmul      ; 또 한 번
    lsrs    r0, r0, #15                         ...
    orr.w   r0, r0, r1, lsl #17
    ssat    r0, #16, r0      ; 16비트 포화 1개
    bx      lr
```

- M4 ⑦: `SMLALBB`(16×16 곱을 64비트 누산기에 더하기)와 `SSAT`(부호 있는 16비트 포화)까지 나왔다. 컴파일러가 ">> 15 의 결과는 [−2^16, 2^16] 이라 하위 32비트만 보면 된다" 를 증명했기 때문이다.
- M4 ④(곱을 `int64_t` 로)도 `smull` + `smlal` 로 라이브러리 호출은 없지만, 포화가 64비트 비교 여러 개(`cmn`/`sbcs`/`it`)로 남아 ⑦ 보다 길다.
- M0+ ④: 16비트 값인데도 64비트 곱 `__aeabi_lmul` 을 **두 번** 호출한다. 같은 M0+ 에서 ⑦ 은 `muls` 두 개 + 64비트 덧셈(`adds`/`adcs`)으로 끝난다.

교훈: "넓게" 는 필요한 곳(합)에만. 정답 코드의 `mix_sample` 이 ⑦ 형태다.

**반올림 한 번 vs 두 번.** 주어진 `mix_sample_2round` 는 `q15_add_sat(q15_mul(m, gm), q15_mul(t, gt))` — 각 곱이 이미 ±0.5 LSB 오차를 갖고 들어온다. 실측 (`demo_q15_pitfalls.c` ⑧, 포화 근처 제외 100만 샘플):

```text
8) mix worst err: 1 rounding 0.5000 LSB, 2 roundings 0.9999 LSB
```

checked 테스트는 10만 샘플에서 `mix_sample` 이 오라클과 **비트 일치**, 클립 판정 일치, 최대 오차 ≤ 0.5 LSB, 그리고 2회 반올림 버전은 0.5 LSB 를 넘는 샘플이 있음을 확인한다. N 탭 FIR 이면 차이가 N 배로 벌어진다 — 그래서 DSP 라이브러리는 누산을 넓은 레지스터(Q30/Q34 in 64비트)에서 하고 마지막에 한 번 줄인다 (DSP-B08).

**DAC.** `q15_to_dac12` 는 offset-binary 로 되돌린다: `u = q + 32768` (0…65535, `uint32_t`), `(u + 8) >> 4`. 여기서 `>>` 는 **unsigned** 라 아무 문제 없다 — 부호 있는 값의 반올림 시프트 문제를 "먼저 음수가 아니게 옮기고 시프트" 로 피하는 흔한 기법이다. `q = 32760…32767` 은 4096 이 되므로 4095 로 포화 (테스트 `q15_to_dac12(32760) == 4095`).

**DMA 콜백.** 테스트는 64샘플 원형 버퍼를 HT 콜백(앞 32개)과 TC 콜백(뒤 32개)으로 처리하고, 결과 DAC 코드 64개가 샘플별 double 오라클과 모두 같은지, 클립 수(앞 8샘플은 최대 입력)가 맞는지 본다. 실제 칩에서는 이 콜백이 ISR 문맥이므로 블록 처리 시간이 "절반 버퍼가 채워지는 시간" 보다 짧아야 하고, `g_cfg` 를 main 이 바꾼다면 C-M14 의 공유 데이터 규칙(게인 두 개를 한 번에 바꾸려면 임계 구역이나 더블 버퍼)이 적용된다.

### 호스트 arm64 와 M0+ 의 `q15_mul` / `q15_add_sat` (실제 출력)

```text
@ Cortex-M0+ (DSP 확장 없음)                 ; 호스트 arm64 (Apple clang 21)
q15_mul:                                     _q15_mul:
    muls    r0, r1, r0                           mov   w8, #16384
    movs    r1, #1                               madd  w8, w1, w0, w8     ; a*b + 0x4000
    lsls    r1, r1, #14       ; 0x4000 만들기    asr   w8, w8, #15
    adds    r0, r0, r1                           mov   w9, #32767
    asrs    r0, r0, #15                          cmp   w8, w9
    ldr     r1, .LCPI0_0      ; 32767            csel  w0, w8, w9, lt     ; 위쪽만 clamp
    cmp     r0, r1                               ret
    blt     .LBB0_2
    mov     r0, r1
.LBB0_2: bx  lr
q15_add_sat:                                 _q15_add_sat:
    adds    r0, r0, r1                           mov   w8, #32767
    ldr r1, =32767 / cmp / blt / mov             add   w9, w0, w1
    ldr r1, =-32768 / cmp / bgt / mov            cmp   w9, w8 / csel ...
    bx      lr                                   cmn   w8, #8, lsl #12 / csel ...
```

(M0+ 의 `q15_add_sat` 상수 로드·비교 줄은 지면상 한 줄씩 묶었다.) 세 타깃 모두 `q15_mul` 의 아래쪽 clamp 가 없다 — 범위 분석 결과다. M0+ 에는 `SMLABB`/`QADD16`/`SSAT` 이 없어(ARMv6-M 은 DSP 확장 없음) 같은 C 코드가 몇 배 긴 명령열이 된다. M3 도 DSP 확장이 없지만 `SSAT` 명령은 있다(ARMv7-M 기본 명령).

### 덤: FPU 가 있는 M4F 라면 — float 은 하드웨어, double 은 소프트웨어

`demo_q15_m4.c` ⑧⑨ 를 `-mfloat-abi=hard -mfpu=fpv4-sp-d16` 로 컴파일한 실제 출력 요약: `q15_from_float` 은 `vmul.f32`, `vcvt.s32.f32`, `vcmp.f32` 같은 FPU 명령으로 끝나지만, 같은 로직의 `q15_from_double_m4` 는 `__aeabi_dcmpun`, `__aeabi_dmul`, `__aeabi_dcmpge`, `__aeabi_dcmple`, `__aeabi_d2iz`, `__aeabi_i2d`, `__aeabi_dsub` … 를 줄줄이 호출한다. FPv4-SP 는 **단정도만** 하드웨어다. 펌웨어에서 `0.5` 같은 double 리터럴 하나가 float 식을 double 로 끌어올려 소프트웨어 경로로 보내는 일이 흔하다 (`-Wdouble-promotion` 으로 잡는다).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 52/52 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 52/52, 보고 없음
cc -std=c11 -Wall -Wextra -Wconversion -Wsign-conversion -O2 -c solution.c   # 경고 0
```

`-Wconversion` 까지 깨끗한 건 모든 축소 변환이 **범위를 확인한 뒤의 명시적 캐스트**이기 때문이다 (C-B02 의 `-Wconversion` 습관). 단, 명시적 캐스트는 경고를 끌 뿐 값을 지켜 주지 않는다 — naive 곱의 `(q15_t)` 도 경고가 없다. 범위는 코드(`sat_q15`)와 테스트가 지킨다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `return (q15_t)((a * b) >> 15);` | 포화 없음: −1×−1 → −32768. 반올림 없음: −0.5 LSB 바이어스. 16비트 `int` 면 곱 자체가 UB | `sat_q15(floor_shr((int32_t)a * b + 0x4000, 15))` |
| floor 대신 `/ 32768` | 0 쪽 버림이라 음수 쪽 반올림이 틀림 (`-113×145 → 0`) | `/` 후 나머지가 음수면 `q--` |
| `(q15_t)(x * 32768.0)` | 범위 밖이면 UB — −O2 에서 `int16_t` 가 32768 을 들고 다님 | 범위 검사 → 변환 → 반올림 |
| `(int)(s + 0.5)` 로 반올림 | `0.5 − 2^−54` 에서 덧셈이 1.0 으로 반올림 | `t = (int)s; frac = s - t; frac >= 0.5 ? t+1 : t` |
| NaN 검사 없음 | NaN 은 모든 비교가 거짓 → 범위 검사 통과 → UB 변환 | 맨 앞 `if (x != x)`. `-ffast-math` 는 이 검사를 지울 수 있다 |
| `(raw - 2048) << 4` | 음수 `<<` 는 UB (6.5.7p4) | `* 16` (같은 명령이 나온다) |
| 두 곱을 `int32_t` 로 누산 | 2^31 → signed overflow, 부호 뒤집힘 | 곱은 `int32_t`, 합은 `int64_t` |
| 곱마다 반올림·포화 후 더하기 | 오차 최악 ~1 LSB, 중간 포화가 최종 결과를 바꿈 | 넓게 누산 → 마지막에 반올림 1번 → 포화 1번 |
| `q15_neg = -a` / `abs(a)` | −(−32768) 은 int16 에 없음. `abs(INT_MIN)` 은 UB | `sat_q15(-(int32_t)a)` |
| `q15_to_dac12` 에서 포화 잊음 | 32760 이상은 4096 → 12비트 레지스터에 쓰면 0 (위 비트 잘림) | `d > 4095 ? 4095 : d` |
| 클립을 조용히 삼킴 | 게인 설정 오류가 "소리가 좀 거칠다" 로만 나타남 | 클립 수를 세어 진단 경로로 |

## 8. 면접 Q&A

<details>
<summary>Q1. Q15 곱셈을 구현해 보세요. −1 × −1 은 어떻게 되나요?</summary>

`int32_t p = (int32_t)a * b;` (Q30) → `p + 0x4000` (0.5 LSB) → floor `>> 15` → `INT16_MAX` 로 포화. −1×−1 = 0x8000×0x8000 = 2^30 → 반올림 후 32768 = +1.0 인데 Q15 최댓값은 1 − 2^−15 라 표현 불가 → 32767 로 포화. 포화가 없으면 `(int16_t)32768` 은 구현 정의(6.3.1.3p3) 변환이고 gcc/clang 에선 −32768 → "−1 × −1 = −1" 이 된다. 범위 분석상 넘치는 입력은 이 한 쌍뿐이라 아래쪽 clamp 는 필요 없다 (컴파일러도 지운다). 꼬리: "`(int32_t)` 캐스트는 왜?" → `int` 가 16비트인 MCU(MSP430, AVR)에선 승격해도 16비트라 곱이 UB.

</details>

<details>
<summary>Q2. 반올림 없이 >> 15 만 하면 뭐가 문제죠? 어떤 반올림을 쓰나요?</summary>

`>>` (floor)는 항상 아래로 버려 평균 −0.5 LSB 바이어스가 생긴다 (실측 −0.4996 LSB). 피드백 루프(IIR, 적분기, PID)에서는 매 샘플 같은 방향으로 쌓여 DC 오프셋·정상상태 오차가 된다. `+0x4000` (round half up) 은 평균을 거의 0 으로 만들고 ARM NEON 의 `SQRDMULH`(saturating rounding doubling multiply high)가 Q15 레인에서 정확히 이 연산 — `(2ab + 2^15) >> 16`, −1×−1 포화 — 을 한다. 동점까지 치우침을 없애려면 RNE(round half to even, convergent)를 쓴다 (추가 연습 2). 절단이 맞는 경우도 있다: CMSIS-DSP 의 여러 Q15 함수처럼 "속도 우선, 바이어스는 감수" 하는 라이브러리는 함수별 문서(Scaling and Overflow Behavior)에 규칙을 적어 둔다 — **규칙을 명세에 적고 테스트로 고정하는 것** 이 핵심이다.

</details>

<details>
<summary>Q3. 음수의 >> 는 구현 정의라던데, 그럼 나눗셈으로 바꾸면 되나요?</summary>

아니다. `/` 는 0 쪽으로 버린다(6.5.5p6) — floor 가 아니다. `q15_mul(-113, 145)` 에서 p + 0x4000 = −1 → `/ 32768` 은 0, floor 는 −1. 정답은 −1 (참값 −0.50003 LSB). 이식 가능한 floor 는 `q = x / d; if (x % d < 0) q--;` 또는 C-B03 의 `x < 0 ? ~(~x >> k) : x >> k`. Cortex-M4·arm64 실측에서 둘 다 `asrs` / `asr` 한 개로 컴파일돼 raw `>>` 와 명령이 똑같았다 — 이식성의 비용이 0 이다. 그래도 raw `>>` 를 쓰겠다면 `_Static_assert((-1 >> 1) == -1, ...)` 로 가정을 박는다.

</details>

<details>
<summary>Q4. (int16_t)(x * 32768.0) 한 줄 변환의 문제는?</summary>

세 가지. ① x ≥ 1.0 이면 정수부가 `int16_t` 범위 밖 → **UB** (6.3.1.4p1). 정수→정수 축소(구현 정의)와 달리 실수→정수 범위 밖은 UB 다. 실측: −O0 에선 −32768, −O2 에선 `fcvtzs w8, d0, #15` 뒤에 16비트 자르기가 생략돼 `int16_t` 가 32768 을 들고 다녔다. UBSan 은 `32768 is outside the range of representable values of type 'short'` 로 잡는다. ② 반올림이 아니라 0 쪽 절단. ③ NaN 이면 역시 UB. 고친 버전: NaN 검사 → `s` 범위 검사 → 변환 → 소수부로 반올림. `(int)(s + 0.5)` 는 `s = 0.5 − 2^−54` 에서 1 을 주는 고전적 버그가 있다.

</details>

<details>
<summary>Q5. Q15 샘플 N 개를 곱해 더하는 FIR 에서 누산기는 몇 비트여야 하나요?</summary>

곱 하나는 Q30, |p| ≤ 2^30 (정확히는 −1×−1 만 2^30). 두 개만 더해도 2^31 이라 `int32_t` 를 넘는다 — 이 레슨의 `mix_sample` 이 그 경우이고 `int32` 로 더하면 signed overflow(UB), 실측으로 부호가 뒤집혔다. N 개면 대략 log2(N) 비트의 guard bit 가 필요하다 → 64비트 누산기(Q30 + 33 guard bit)면 실무상 충분하다. Cortex-M4 는 `SMLALBB`/`SMLALD` 로 16×16 곱을 64비트에 누산하는 명령이 있고, 실측에서 clang 이 `smlalbb` 를 냈다. 핵심 규칙: 곱은 정확하게, 누산은 넓게, 반올림·포화는 마지막에 한 번. guard bit 계산기는 DSP-B04, FIR 은 DSP-B08.

</details>

<details>
<summary>Q6. 곱마다 반올림하고 더하는 것과 다 더한 뒤 한 번 반올림하는 것, 차이가 있나요?</summary>

있다. 곱마다 반올림하면 각 항이 ±0.5 LSB 오차를 가져와 두 항이면 최악 ~1 LSB (실측 0.9999 LSB), N 항이면 N/2 LSB 까지 커진다. 한 번 반올림은 항 수와 상관없이 ≤ 0.5 LSB (실측 0.5000). 또 중간 포화가 문제다: `+0.9 + 0.6 − 0.7` 을 왼쪽부터 포화 덧셈하면 0.9+0.6 이 1−2^−15 에 붙은 뒤 −0.7 → 0.3 이 되지만 참값은 0.8. 넓은 누산기는 중간값이 범위를 넘어도 괜찮다 (포화는 비선형이라 결합법칙이 깨진다).

</details>

<details>
<summary>Q7. Cortex-M4 에는 이런 연산을 위한 명령이 있나요? M0 는요?</summary>

ARMv7E-M (M4/M7/M33 의 DSP 확장)에 있다: `SMULBB/SMLABB` (16×16 곱(누산)), `SMLALBB/SMLALD` (64비트 누산), `QADD/QSUB` (32비트 포화), `QADD16/QSUB16` (16비트 2개 병렬 포화), `SSAT/USAT` (임의 비트 포화, 시프트 옵션 포함). 실측으로 clang 은 평범한 C 에서 `q15_mul` → `smlabb`, `q15_add_sat` → `qadd16`, 믹스 → `smlalbb` + `ssat` 을 골랐다. CMSIS 는 `__SSAT`, `__QADD16`, `__SMLALD` 같은 intrinsic 을 제공한다. M0/M0+ (ARMv6-M) 에는 DSP 확장이 없어 `muls` + 비교·분기로 풀리고, 64비트 곱을 쓰면 `__aeabi_lmul` 라이브러리 호출까지 생겼다. M3 (ARMv7-M) 는 `SSAT` 와 `SMULL/SMLAL` 은 있지만 16비트 SIMD·Q 명령은 없다.

</details>

<details>
<summary>Q8. 이런 Q15 함수를 어떻게 테스트하겠어요?</summary>

① **정확한 오라클**: Q15 값과 그 곱은 double 에 정확히 들어가므로 double 로 계산한 참값을 같은 반올림 규칙으로 정수화해 **비트 단위 비교**한다 (이 레슨: 무작위 20만 쌍, 불일치 0). ② **오차 한계 검사**: |오차| ≤ 0.5 LSB (포화 제외), 평균 바이어스. ③ **손으로 고른 경계**: −1×−1, ±동점, 동점 바로 옆(−113×145), 0, ±최대, NaN/∞, `0.5 − 2^−54`. ④ **결정적 PRNG**(고정 시드 xorshift32)로 재현 가능하게. ⑤ 입력이 2^32 쌍뿐이라 **전수 검사**도 가능하다 (DSP-B02). ⑥ 타깃 코드와 호스트 골든 모델의 bit-exact 비교 자동화는 DSP-A08. sanitizer 는 UB 만 잡고 "틀린 반올림" 은 못 잡는다는 걸 기억 — 이 레슨의 함정 6개 중 3개는 UBSan 이 침묵했다.

</details>

<details>
<summary>Q9. Cortex-M4F 처럼 FPU 가 있으면 고정소수점은 필요 없지 않나요?</summary>

여전히 쓰는 이유: ① M4F 의 FPv4-SP 는 **단정도만** — double 은 소프트웨어 (`demo_q15_m4.c` ⑨ 에서 `__aeabi_dmul` 등 실측). ② 데이터가 이미 정수: ADC/I2S/DAC 는 12~24비트 정수를 DMA 로 주고받는다. ③ SIMD: `SMLAD`/`QADD16` 은 16비트 두 개를 한 명령에 처리 → Q15 FIR 이 float 보다 빠를 수 있다. ④ 메모리: Q15 는 2바이트, float 은 4바이트. ⑤ bit-exact 재현성: 같은 입력이면 모든 타깃·컴파일러에서 같은 비트 (float 은 FMA 융합 여부 등으로 달라질 수 있다). ⑥ ISR 에서 FPU 를 쓰면 FP 레지스터 스택킹(lazy stacking)으로 인터럽트 지연·스택이 늘어난다. 반대로 동적 범위가 큰 계산(칼만 필터, 큰 행렬)은 float 이 안전하다 — 선택 기준은 NUM-A09.

</details>

## 9. 추가 연습

### 연습 1 — Q31 곱셈

`q31_t q31_mul_sat(q31_t a, q31_t b)` 를 같은 규칙(64비트 곱 Q62, `+2^30`, floor `>> 31`, 포화)으로 작성하라. 오라클은 무엇으로 하나? (double 은 Q62 를 정확히 담지 못한다.)

<details>
<summary>답 보기</summary>

`int64_t p = (int64_t)a * b;` — |p| ≤ 2^62 라 `int64_t` 에 들어간다. `floor_div64(p + 2^30, 2^31)` 후 `INT32_MAX` 초과면 포화 (역시 −1×−1 뿐). 오라클은 floor 의 **정의 자체**를 정수로 검사한다: `r·2^31 ≤ p + 2^30 < (r+1)·2^31` — 모든 항이 `int64_t` 범위 안이라 `__int128` 없이 된다. Cortex-M4 의 `SMMULR` 는 32×32 곱의 상위 32비트를 반올림해 주지만 결과가 Q30 이라(>>32) Q31 로 쓰려면 한 비트 보정이 필요하다 — 라이브러리마다 이 처리가 다르니 문서를 확인한다.

</details>

### 연습 2 — 동점은 짝수 쪽으로 (RNE)

`q15_mul_rne(a, b)` 를 작성하라: 정확히 0.5 LSB 동점이면 결과가 짝수가 되는 쪽으로. 동점만 모아(`a` 홀수 × 16384) half-up 과 RNE 의 바이어스를 비교하라.

<details>
<summary>답 보기</summary>

`r = floor(p / 2^15)`, `rem = p − r·2^15` (0…32767). `rem > 16384` 이면 올리고, `rem == 16384` 이면 `r` 이 홀수일 때만 올린다 (`r & 1` 은 `int32_t` 가 2의 보수로 보장(7.20.1.1)되므로 음수에서도 맞다). 동점 32768 개에서 half-up 은 모든 동점이 +0.5 LSB (합 = n × 0.5 LSB), RNE 는 +0.5 와 −0.5 가 정확히 반반이라 합 0. 동점이 아닌 입력에서는 두 방식이 같다 (10만 쌍 확인). 오디오처럼 동점이 드문 데이터에선 차이가 작지만, 정수 배율(예: ×0.5)처럼 동점이 자주 생기는 계산에선 RNE 가 바이어스를 없앤다.

</details>

### 연습 3 — Q15 나눗셈

`q15_t q15_div_sat(q15_t num, q15_t den)` — 결과 `num/den` 을 Q15 로, 가장 가까운 값(동점은 0 에서 먼 쪽), |num| ≥ |den| 이면 포화, `den == 0` 이면 `num` 의 부호에 따라 포화(0/0 → 0).

<details>
<summary>답 보기</summary>

`n = num · 32768` (|n| ≤ 2^30, `int32_t` OK), `q = n / den`, `r = n % den` (0 쪽 버림), `2|r| ≥ |den|` 이면 몫의 부호 쪽으로 1 더한다, 마지막에 포화. 경계: `-16384 / 16384 = −1.0` 은 **표현 가능**(−32768)이지만 `16384 / 16384 = +1.0` 은 포화 — Q15 의 비대칭이 나눗셈에도 그대로 나온다. `INT16_MIN / -1` 은 n = −2^30 을 −1 로 나눠 2^30 → 포화 (정수 `INT_MIN / -1` 처럼 UB 가 되지 않는 건 넓혀서 계산했기 때문, C-M12 ⑨). Cortex-M3/M4 는 `SDIV` 가 있지만 2~12 사이클이고 M0/M0+ 는 나눗셈 명령이 없어 라이브러리 호출이다 → 실시간 경로에선 역수 곱(DSP-B12 Newton-Raphson)을 쓴다.

</details>

### 연습 4 — 해상도가 다른 ADC

`q15_t adc_to_q15(uint16_t raw, unsigned bits)` — 8~16비트 offset-binary ADC 값을 Q15 로. `bits` 비트 밖은 무시.

<details>
<summary>답 보기</summary>

`v = (raw & mask) − 2^(bits−1)`, 결과 `v · 2^(16−bits)` (음수 `<<` 대신 곱). 8비트 255 → 127·256 = 32512, 10비트 1023 → 511·64 = 32704, 16비트 65535 → 32767. 어떤 해상도든 결과를 32768 로 나누면 `(raw − mid) / mid` 와 **정확히** 같다 (9가지 해상도 × 2000 개 확인). 위쪽은 1 LSB(ADC 기준) 모자란 비대칭이 해상도마다 다르다는 점(12비트면 32752, 최대 1 − 2^−11)에 주의 — 게인 보정·클립 임계값을 정할 때 이 값을 쓴다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 23/23 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 23/23, 보고 없음
```

## 10. 핵심 정리

- Q15 = `int16_t q` ↔ `q / 2^15`, 범위 [−1, 1 − 2^−15]. **+1 이 없다** → −1×−1, −(−1), |−1|, 0−(−1) 은 모두 포화 대상.
- 곱: `(int32_t)a * b` (Q30) → `+0x4000` → **floor** `>> 15` → 포화. 넘치는 입력은 −1×−1 하나뿐. 절단은 평균 −0.5 LSB 바이어스.
- floor 는 `/`+`%` 보정 또는 `~(~x >> k)` 로 — 음수 `>>` 는 구현 정의, `/` 는 0 쪽 버림이라 틀린다. 실측상 비용 0 (`asr` 한 개).
- 실수→정수 범위 밖은 **UB**, 정수→`int16_t` 범위 밖은 **구현 정의**, 음수 `<<`·signed overflow 는 **UB**. 범위 검사가 항상 변환보다 먼저.
- `(int)(s + 0.5)` 는 `0.5 − 2^−54` 에서 틀린다. 소수부를 따로 구해 비교. NaN 은 맨 앞에서.
- 파이프라인: 곱은 정확히(32비트), 누산은 넓게(64비트), 반올림·포화는 마지막에 한 번. 두 곱 합만으로도 2^31 → int32 overflow.
- M4 는 `SMLABB`·`QADD16`·`SMLALBB`·`SSAT` 이 C 코드에서 바로 나온다. M0+ 는 DSP 확장이 없고, 64비트 곱은 `__aeabi_lmul`. M4F 의 double 은 소프트웨어.
- 테스트는 double 오라클 bit-exact + 경계값 + 고정 시드 PRNG. sanitizer 는 틀린 반올림을 못 잡는다.

## 참고자료

- C11 (N1570): 6.3.1.1 정수 승격, 6.3.1.3p3 범위 밖 signed 변환(구현 정의), 6.3.1.4p1 실수→정수 변환(범위 밖 UB), 6.5p5 예외 조건(signed overflow), 6.5.5p6 나눗셈 0 쪽 버림, 6.5.7p4–5 시프트(음수 `<<` UB, 음수 `>>` 구현 정의), 7.12.9.7 `lround`, 7.20.1.1 정확 폭 정수(2의 보수), 7.22.6.1 `abs` — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- ARMv7-M Architecture Reference Manual (ARM DDI0403) — DSP 확장 명령 `SMLABB`, `SMLALBB`, `QADD16`, `SSAT`, `SMMULR` — <https://developer.arm.com/documentation/ddi0403/latest/>
- Arm Cortex-M4 Technical Reference Manual / Cortex-M4 Devices Generic User Guide — 명령 사이클, FPv4-SP — <https://developer.arm.com/documentation/dui0553/latest/>
- CMSIS-Core SIMD intrinsics (`__SSAT`, `__QADD16`, `__SMLALD`) — <https://arm-software.github.io/CMSIS_5/Core/html/group__intrinsic__SIMD__gr.html>
- CMSIS-DSP 문서 (함수별 Scaling and Overflow Behavior) — <https://arm-software.github.io/CMSIS-DSP/latest/>
- Arm A64 `SQRDMULH` (NEON Q15/Q31 rounding doubling multiply high) — <https://developer.arm.com/documentation/ddi0602/latest/>
- 관련 레슨: C-B02 (승격, `sat_add_i16`, `-Wconversion`), C-B03 (`sar32`), C-M12 (UB 카탈로그, `INT_MIN / -1`), C-M14 (DMA 콜백 = ISR 공유 데이터), C-A08 (`_Static_assert` 로 가정 고정), DSP-B01 (Q 변환 전반), DSP-B02 (Q15 곱 전수 검사·SMULBB/QDMULH), DSP-B03 (분기 없는 포화), DSP-B04 (Q 형식 혼합·guard bit), DSP-B08 (Q15 FIR), DSP-B12 (Newton-Raphson 역수), DSP-M05 (고정소수점 biquad limit cycle), DSP-A08 (bit-exact 골든 모델), NUM-A09 (고정 vs 부동 선택)
