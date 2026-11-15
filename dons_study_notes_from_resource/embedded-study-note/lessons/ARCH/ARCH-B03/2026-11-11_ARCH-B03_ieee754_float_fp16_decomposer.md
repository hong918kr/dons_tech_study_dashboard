---
id: ARCH-B03
time: 70분
updated: 2026-11-11
---
# ARCH-B03 IEEE 754 부동소수점 분해기

## 학습 목표

- float32(1·8·23)와 FP16(binary16, 1·5·10) 비트 패턴을 **부호·지수·가수** 로 분해/조립하고, 0·비정규수(subnormal)·정규수·Inf·qNaN·sNaN 을 비트만 보고 분류할 수 있다.
- 유한한 float 를 **정확한 값 m·2^e** 로 바꿔 "0.1f 는 0.1 보다 1.49e-9 크다" 를 정수 산수로 증명할 수 있다.
- float → FP16 변환을 **round-to-nearest-even(RNE)** 으로 직접 구현하고 — 동점, 비정규 결과, 65520 → Inf 경계, NaN quiet 화까지 — 이 맥(Apple M6)의 하드웨어 `FCVT` 와 **2^32 개 전수 비교로 0 개 불일치** 를 확인한다.
- 센서/ML 텐서를 FP16 DMA 버퍼로 내릴 때 생기는 손실(반올림·언더플로·오버플로·NaN)을 세고, 포화(saturate) 정책을 고를 수 있다.
- Cortex-M4F `VCVTB`, Cortex-M0 `__aeabi_f2h`, arm64 `FCVT`/`BFCVT` 가 각각 무엇을 대신하는지 설명한다.

## 1. 문제

"0.1 + 0.2 는 왜 0.3 이 아닌가", "FP16 최댓값은?", "float 를 half 로 바꾸는 코드를 짜 보라" — 펌웨어(센서 데이터 압축, DSP), 실리콘 검증(FPU/NPU 변환 유닛의 골든 모델), AI 가속기 면접에서 반복해서 나오는 주제다. 하드웨어 변환기를 검증하려면 **비트 단위로 같은 답을 내는 C 참조 모델** 이 있어야 하고, 이 레슨이 바로 그 모델을 만든다.

IEEE 754 수는 정수 세 개를 한 워드에 붙여 놓은 것이다. 값은 (정규수일 때) `(-1)^s × 1.f × 2^(e − bias)` 이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">float32 와 FP16 의 비트 배치</text>
  <text x="10" y="58" fill="currentColor">float32</text>
  <rect x="90" y="40" width="20" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="110" y="40" width="160" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="270" y="40" width="460" height="28" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="100" y="59">s</text><text x="190" y="59">exp 8비트 (bias 127)</text><text x="500" y="59">frac 23비트 (숨은 1 은 저장 안 함)</text>
  </g>
  <g fill="currentColor" font-size="12" opacity=".75">
    <text x="92" y="84">31</text><text x="112" y="84">30</text><text x="252" y="84">23</text><text x="272" y="84">22</text><text x="718" y="84">0</text>
  </g>
  <text x="10" y="128" fill="currentColor">FP16</text>
  <rect x="90" y="110" width="20" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="110" y="110" width="100" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="210" y="110" width="200" height="28" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="100" y="129">s</text><text x="160" y="129">exp 5 (bias 15)</text><text x="310" y="129">frac 10비트</text>
  </g>
  <g fill="currentColor" font-size="12" opacity=".75">
    <text x="92" y="154">15</text><text x="112" y="154">14</text><text x="194" y="154">10</text><text x="212" y="154">9</text><text x="400" y="154">0</text>
  </g>
  <text x="430" y="129" fill="currentColor" font-size="12">bfloat16 = float32 의 상위 16비트 (1·8·7)</text>
  <rect x="10" y="172" width="720" height="118" rx="6" fill="none" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12.5">
    <text x="22" y="194">exp 필드         의미                         float32 값               FP16 값</text>
    <text x="22" y="216">0, frac=0        ±0                           ±0                       ±0</text>
    <text x="22" y="236">0, frac≠0        비정규: 0.f × 2^(1−bias)     f × 2^-149               f × 2^-24</text>
    <text x="22" y="256">1 .. max−1       정규:  1.f × 2^(e−bias)      2^-126 .. 3.4e38         2^-14 .. 65504</text>
    <text x="22" y="276">max(255 / 31)    frac=0 → ±Inf, ≠0 → NaN     최상위 frac 비트 1 = quiet NaN</text>
  </g>
</svg>
```

### Part A — 분해 · 조립 · 분류 · 정확한 값

- `f32_split / f32_join`, `f16_split / f16_join` — 원시 필드 `{sign, exp(바이어스 그대로), frac}` 로 나누고 다시 합친다.
- `f32_classify / f16_classify` — `FC_ZERO, FC_SUBNORMAL, FC_NORMAL, FC_INF, FC_QNAN, FC_SNAN`.
- `f32_exact(b, &m, &e)` — 유한한 값을 **정수 m × 2^e** 로 (m 은 홀수로 정규화, 0 은 m=0,e=0). 0.1f → `13421773 × 2^-27`.
- `f16_to_f32(h)` — FP16 → float32 확장(항상 정확). 65536 개 **전수** 를 하드웨어와 비교한다.

### Part B — float32 → FP16, round-to-nearest-even

`f32_to_f16(b)` 를 정수 연산만으로 구현한다 (`_Float16` 캐스트는 테스트의 기준값으로만). 꼭 맞춰야 하는 경계:

- 동점(tie)은 짝수로: `1 + 2^-11 → 1.0`, `1 + 3·2^-11 → 1 + 2^-9`
- 비정규 결과: `2^-24 → 0x0001`, `2^-25 → 0`(동점→짝수), `1.5·2^-24 → 0x0002`, `2^-14` 바로 아래 → 최소 정규수 `0x0400` 로 올림
- 오버플로: `65504 → 0x7BFF`, `65519.996 → 0x7BFF`, **`65520 → Inf`**
- NaN 정책: **부호 유지, frac 상위 10비트(payload) 유지, quiet 비트(bit 9) 켬** — sNaN 도 qNaN 으로. 이것이 Arm `FCVT`(FPCR.DN=0)의 동작과 같다는 것을 테스트가 확인한다.

체크 테스트는 경계값 + 고정 시드 무작위 40만 개 + 모든 FP16 값과 그 사이 동점을 하드웨어와 비교한다. **2^32 전부** 는 `demo_exhaustive.c` 에서 한다.

### Part C — 센서 텐서를 FP16 DMA 버퍼로 포장

NPU/DSP 가 FP16 입력만 받는다고 하자. 펌웨어는 IMU 값(m/s²) 배열을 FP16 버퍼로 바꿔 DMA 에 넘긴다. `f16_pack(in, out, n, saturate, &report)` 는 변환하면서 `exact / inexact / underflow(→±0) / overflow / nan` 과 `subnormal`(결과가 비정규) 개수를 센다. `saturate` 가 켜지면 유한 오버플로를 Inf 대신 `±65504` 로 포화한다 (Inf 입력은 Inf 그대로). 고장 센서의 NaN, 충격 스파이크 70000, 노이즈 1e-6 이 섞인 입력으로 리포트를 검증한다.

## 2. Problem (English)

> **IEEE 754 binary32 / binary16 bit-level model.**
>
> Implement in C11, using integer operations only (no float casts in the implementation):
>
> 1. `fparts f32_split(uint32_t)`, `uint32_t f32_join(fparts)`, `fparts f16_split(uint16_t)`, `uint16_t f16_join(fparts)`, and `classify` for both formats (zero, subnormal, normal, inf, quiet NaN, signaling NaN).
> 2. `void f32_exact(uint32_t b, int64_t *m, int *e)` such that the value equals `m * 2^e` exactly with `m` odd (or `m == 0`).
> 3. `uint32_t f16_to_f32(uint16_t)` (exact widening) and `uint16_t f32_to_f16(uint32_t)` with round-to-nearest-ties-to-even, correct subnormal results, overflow to ±Inf, and NaNs converted to quiet NaNs preserving sign and the top payload bits.
> 4. A packing routine that converts a float array to binary16 and reports how many elements were exact, rounded, flushed to zero, overflowed, or NaN, with an optional saturate-to-±65504 mode.
>
> Your conversion must match the hardware (`(_Float16)x`, i.e. Arm `FCVT Hd, Sn`) bit-for-bit for all 2^32 inputs.
>
> Example: `f32_to_f16(0x3DCCCCCD /*0.1f*/) == 0x2E66`, `f32_to_f16(0x477FF000 /*65520*/) == 0x7C00`, `f32_to_f16(0x7FA00000) == 0x7F00`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # SDK 27.2 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 이 쭉 나오고 "4/58 passed", exit 1 — TODO 를 채워 58/58 을 만든다
```

`_Float16` 은 Apple clang arm64 에서 `-std=c11` 로도 쓸 수 있다 (테스트의 `hw_f16 / hw_f32` 만 사용). 다른 컴파일러/타깃에서는 `__fp16` 이거나 없을 수 있다.

## 4. 힌트

<details>
<summary>힌트 1 — 분해는 C-B04 의 GET_FIELD 그대로</summary>

float32: `s = b >> 31`, `e = (b >> 23) & 0xFF`, `f = b & 0x7FFFFF`. FP16: `>> 15`, `>> 10 & 0x1F`, `& 0x3FF`. 조립은 각 필드를 마스크한 뒤 제자리로 밀어 OR. NaN 의 quiet/signaling 은 **frac 최상위 비트** (float bit 22, FP16 bit 9) 가 1 이면 quiet.

</details>

<details>
<summary>힌트 2 — 정확한 값 m·2^e</summary>

정규수: `m = frac | 0x800000`(숨은 1), `e = exp − 127 − 23 = exp − 150`. 비정규수: 숨은 1 이 없고 지수는 **exp=1 과 같은** `−126 − 23 = −149`. 그다음 m 이 짝수인 동안 `m >>= 1, e++`.

</details>

<details>
<summary>힌트 3 — FP16 → float 는 바이어스만 바꾼다 (비정규만 예외)</summary>

정규: `exp32 = exp16 − 15 + 127 = exp16 + 112`, `frac32 = frac16 << 13`. FP16 비정규 `f × 2^-24` 는 float 에서는 **정규수** 다 → frac 을 bit 10 이 1 이 될 때까지 왼쪽으로 밀고 그만큼 지수를 줄인다. NaN 은 quiet 비트(0x00400000)를 켠다.

</details>

<details>
<summary>힌트 4 — float → FP16: "몇 비트를 버리나" 하나로 정규·비정규를 통일</summary>

24비트 유효숫자 `m = frac | 0x800000` 에서, FP16 정규 결과는 11비트를 남기므로 **13비트** 를 버린다. 결과가 비정규(`eh = exp − 112 ≤ 0`)면 2^-24 단위로 맞춰야 하므로 `14 − eh` 비트를 버린다. 버린 부분 `rem` 과 그 절반 `half` 를 비교: `rem > half` 이면 올림, `rem == half` 이면 남긴 값 `q` 가 홀수일 때만 올림.

</details>

<details>
<summary>힌트 5 — 올림 캐리는 지수로 흘러가게 두자</summary>

정규 결과를 `((eh − 1) << 10) + q` 로 만들면 (q 는 숨은 1 포함 11비트) 숨은 1 이 지수에 +1 을 더하고, 반올림으로 `q == 0x800` 이 되면 그 캐리도 자동으로 지수 +1 이 된다. eh=30 에서 캐리가 나면 `31 << 10 = 0x7C00` = Inf — 65520 경계가 공짜로 맞는다. 비정규에서 `q == 0x400` 이 되면 그대로 최소 정규수 0x0400 이다.

</details>

## 5. 풀이

### Part A — 필드는 정수, 의미는 표 하나

| 필드 조합 | float32 예 (비트) | 분류 | 값 |
|---|---|---|---|
| exp=0, frac=0 | `0x00000000`, `0x80000000` | ZERO | +0, −0 (비교는 같지만 `1/x` 부호가 다름) |
| exp=0, frac≠0 | `0x00000001` | SUBNORMAL | 2^-149 = FLT_TRUE_MIN |
| exp=1, frac=0 | `0x00800000` | NORMAL | 2^-126 = FLT_MIN |
| exp=255, frac=0 | `0x7F800000` | INF | +∞ |
| exp=255, frac bit22=1 | `0x7FC00000` | QNAN | 기본 qNaN |
| exp=255, bit22=0, frac≠0 | `0x7F800001` | SNAN | 쓰면 Invalid 예외 |

비정규수의 지수가 `0 − 127 = −127` 이 아니라 **−126** 인 이유: exp=1 의 최소 정규수 `1.0 × 2^-126` 과 빈틈 없이 이어지려고 (`0.111…1 × 2^-126` 바로 다음이 `1.000…0 × 2^-126`). 그래서 비정규 구간의 간격은 최소 정규 구간의 간격과 같은 2^-149 다 — "점진적 언더플로(gradual underflow)".

**0.1f 의 정체** (`f32_exact`): `0x3DCCCCCD` → exp=123, frac=0x4CCCCD → m = 0xCCCCCD = 13421773, e = 123 − 150 = −27. 0.1 과 비교하려면 `m/2^27` 대 `1/10` → `m·10` 대 `2^27` 을 정수로 비교한다: `134217730 > 134217728`. 즉 **0.1f 는 0.1 보다 2/(10·2^27) ≈ 1.49e-9 크다**. 10진 이진 소수는 끝나지 않으므로(0.0001100110011…₂) 어디선가 반올림해야 하고, 잘린 꼬리 `0.1100…` 이 반을 넘어서 올림이 됐다. FP16 의 0.1 은 반대로 `0x2E66 = 1638·2^-14 = 0.0999755859375` — 내림이다.

`demo_table.c` 실행 결과 (macOS `printf` 는 float 의 정확한 10진값을 끝까지 찍어 준다, FP16 열은 하드웨어 변환):

```text
$ cc -std=c11 -Wall -Wextra -O2 demo_table.c -o /tmp/esn_ARCH-B03/tb && /tmp/esn_ARCH-B03/tb
0.1f                   3DCCCCCD  s=0 e=123 f=4CCCCD  0.100000001490116119384765625      FP16 2E66 = 0.0999755859375
1/3                    3EAAAAAB  s=0 e=125 f=2AAAAB  0.3333333432674407958984375        FP16 3555 = 0.333251953125
FLT_TRUE_MIN (2^-149)  00000001  s=0 e=  0 f=000001  1.40129846432481707092372958329e-45 FP16 0000 = 0
FLT_MIN (2^-126)       00800000  s=0 e=  1 f=000000  1.17549435082228750796873653722e-38 FP16 0000 = 0
FP16 min sub 2^-24     33800000  s=0 e=103 f=000000  5.9604644775390625e-08             FP16 0001 = 5.9604644775390625e-08
2^-25 (tie)            33000000  s=0 e=102 f=000000  2.98023223876953125e-08            FP16 0000 = 0
1.5*2^-24 (tie)        33C00000  s=0 e=103 f=400000  8.94069671630859375e-08            FP16 0002 = 1.1920928955078125e-07
FP16 min normal 2^-14  38800000  s=0 e=113 f=000000  6.103515625e-05                    FP16 0400 = 6.103515625e-05
65504 (FP16 max)       477FE000  s=0 e=142 f=7FE000  65504                              FP16 7BFF = 65504
65519.996              477FEFFF  s=0 e=142 f=7FEFFF  65519.99609375                     FP16 7BFF = 65504
65520 (tie)            477FF000  s=0 e=142 f=7FF000  65520                              FP16 7C00 = inf
1+2^-11 (tie)          3F801000  s=0 e=127 f=001000  1.00048828125                      FP16 3C00 = 1
1+3*2^-11 (tie)        3F803000  s=0 e=127 f=003000  1.00146484375                      FP16 3C02 = 1.001953125
2049                   45001000  s=0 e=138 f=001000  2049                               FP16 6800 = 2048
qNaN 7FC00000          7FC00000  s=0 e=255 f=400000  nan                                FP16 7E00 = nan
sNaN 7FA00000          7FA00000  s=0 e=255 f=200000  nan                                FP16 7F00 = nan
```

**FP16 → float32** 는 지수 범위(−14..15 → −126..127)와 정밀도(11 → 24비트)가 모두 포함되므로 반올림이 없다. 주의할 곳은 하나, FP16 비정규수 `f·2^-24` 가 float 에선 정규수라는 점이다: 예) `0x0001` → frac=1 을 10번 밀어 bit 10 에 1 → 지수 `1 − 10 = −9` → biased `−9 + 112 = 103` → `0x33800000` (= 2^-24). 65536 개 전수가 하드웨어 `FCVT Sd, Hn` 과 같은지 체크 테스트가 확인한다 (sNaN 입력 → quiet 비트가 켜진 qNaN 까지 포함).

### Part B — 반올림은 "버린 꼬리 vs 반" 비교

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">f32_to_f16: 24비트 m 에서 shift 비트를 버리고 RNE</text>
  <g fill="currentColor" font-family="monospace" font-size="12.5">
    <text x="10" y="52">정규 결과 (eh = exp−112 ≥ 1): shift = 13</text>
  </g>
  <rect x="10" y="62" width="242" height="28" class="f-ok" stroke="currentColor"/>
  <rect x="252" y="62" width="22" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="274" y="62" width="264" height="28" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-size="12.5">
    <text x="131" y="81">q: 남길 11비트 (1.xxxxxxxxxx)</text>
    <text x="263" y="81">R</text>
    <text x="406" y="81">sticky: 나머지 12비트</text>
  </g>
  <text x="560" y="74" fill="currentColor" font-size="12">rem = R+sticky (13비트)</text>
  <text x="560" y="90" fill="currentColor" font-size="12">half = 1 &lt;&lt; 12</text>
  <g fill="currentColor" font-family="monospace" font-size="12.5">
    <text x="10" y="122">비정규 결과 (eh ≤ 0): shift = 14 − eh  (eh=0 → 14, eh=−10 → 24, 그 아래는 0)</text>
  </g>
  <rect x="10" y="134" width="720" height="104" rx="6" fill="none" stroke="currentColor"/>
  <g fill="currentColor" font-size="13">
    <text x="24" y="156" font-weight="bold">결정 표</text>
    <text x="24" y="178">rem &lt; half              → 버림 (truncate)</text>
    <text x="24" y="198">rem &gt; half              → q + 1</text>
    <text x="24" y="218">rem == half (정확한 동점) → q 가 홀수면 q + 1, 짝수면 그대로 → 결과 lsb 는 항상 0</text>
    <text x="440" y="178" font-size="12">R = round 비트, sticky = 그 아래 OR</text>
    <text x="440" y="198" font-size="12">(하드웨어 용어: guard/round/sticky)</text>
  </g>
  <line x1="40" y1="282" x2="700" y2="282" stroke="currentColor"/>
  <g stroke="currentColor"><line x1="80" y1="274" x2="80" y2="290"/><line x1="380" y1="274" x2="380" y2="290"/><line x1="680" y1="274" x2="680" y2="290"/></g>
  <line x1="230" y1="276" x2="230" y2="288" stroke="currentColor" stroke-dasharray="2 2"/>
  <line x1="530" y1="276" x2="530" y2="288" stroke="currentColor" stroke-dasharray="2 2"/>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="80" y="306">0x3C00 = 1.0 (짝)</text><text x="380" y="306">0x3C01 (홀)</text><text x="680" y="306">0x3C02 (짝)</text>
    <text x="230" y="270" class="f-ok">1+2^-11 → 1.0</text><text x="530" y="270" class="f-ok">1+3·2^-11 → 0x3C02</text>
  </g>
  <text x="10" y="324" fill="currentColor" font-size="12">동점은 언제나 짝수 이웃으로 — 평균적으로 위·아래로 반씩 가서 누적 편향(bias)이 0 이 된다.</text>
</svg>
```

단계별:

1. **특수값 먼저**: exp=255 → frac=0 이면 ±Inf(`sign|0x7C00`), 아니면 `sign | 0x7E00 | (frac >> 13)`. `0x7E00` 에 quiet 비트가 들어 있으므로 payload 상위 비트가 모두 0 이어도(예: `0x7F800001`) 결과가 Inf 로 바뀌지 않는다 — **NaN 을 Inf 로 만드는 것이 가장 흔한 변환기 버그** 다.
2. **float 비정규·0**: |x| < 2^-126 은 FP16 최소 비정규 2^-24 의 반(2^-25)보다 훨씬 작다 → ±0 (부호 유지).
3. **범위**: `eh = exp − 127 + 15 = exp − 112`. `eh ≥ 31` 이면 |x| ≥ 2^16 > 65520 → Inf.
4. **shift 결정 → RNE**: 위 그림. 비정규 결과에서 shift 가 24 를 넘으면 (|x| < 2^-25) 0. shift = 24 (|x| ∈ [2^-25, 2^-24)) 일 때 `q = 0` 이고 `rem` 이 정확히 `half` 면 짝수(0)로, 넘으면 1 로.
5. **합치기**: 정규는 `sign | (((eh − 1) << 10) + q)`, 비정규는 `sign | q`. 캐리가 지수로 흘러가는 것이 이 표현의 장점이다 (힌트 5).

경계 값 검산:

| 입력 | m 과 버린 꼬리 | 결과 |
|---|---|---|
| 65504 = `0x477FE000` | eh=30, q=0x7FF, rem=0 | `0x7BFF` |
| 65519.996 = `0x477FEFFF` | q=0x7FF, rem=0xFFF < 0x1000 | `0x7BFF` (내림) |
| 65520 = `0x477FF000` | q=0x7FF(홀), rem=0x1000 = half | q=0x800 → `(29<<10)+0x800 = 0x7C00` **Inf** |
| 2^-25 = `0x33000000` | eh=−10, shift=24, q=0, rem=half | 짝수 → `0x0000` |
| `0x387FFFFF` (2^-14 바로 아래) | eh=0, shift=14, q=0x3FF(홀), rem > half | q=0x400 → `0x0400` 최소 정규 |
| 0.1f | eh=11, q=0x666, rem=0x0CCD < 0x1000 | `0x2E66` |

FP16 오버플로 경계가 65504 가 아니라 **65520** 인 이유: 65504 다음 칸(가상의 65536)과의 정확히 가운데가 65520 이고, RNE 는 "무한대 지수에 있는 가상의 짝수 값 65536" 쪽으로 동점을 보낸다 → Inf. 그래서 [65504, 65520) 은 65504 로, 65520 이상은 Inf 다.

**전수 비교 (`demo_exhaustive.c`, Apple M6, Apple clang 21.0.0, `-O2`)**: 체크 테스트는 무작위 표본만 보지만, 32비트 입력은 전부 돌려도 몇 초다.

```text
$ cc -std=c11 -Wall -Wextra -O2 demo_exhaustive.c -o /tmp/esn_ARCH-B03/ex && time /tmp/esn_ARCH-B03/ex
all 2^32 float bit patterns: sw vs hw (FCVT) mismatches = 0
  NaN inputs 16777214, ->Inf 1879056386, ->±0 1711276034, ->FP16 subnormal 184532990
  compare loop 7.68 s | sw only 3.33 s (0.77 ns/conv) | hw only 1.36 s (0.32 ns/conv) | acc=0000
/tmp/esn_ARCH-B03/ex  11.95s user 0.08s system 95% cpu 12.550 total
```

- **불일치 0 개** — 4,294,967,296 개 입력 모두 비트 단위로 `FCVT Hd, Sn` 과 같다. NaN payload 정책(부호·상위 payload 유지 + quiet)이 이 맥의 하드웨어 동작과 같다는 뜻이다 (이 프로세스의 `FPCR` 을 `mrs` 로 읽으면 0 — DN=0(기본 NaN 모드 꺼짐), FZ=0, 반올림 모드 RN).
- NaN 입력 2^24 − 2 개(지수 255, frac≠0, 부호 2개), Inf 로 간 입력 약 18.8억 개(|x| ≥ 65520 과 ±Inf), ±0 으로 간 것 약 17.1억 개 — 2^32 비트 패턴의 대부분은 FP16 범위 밖이다. 그래서 체크 테스트는 무작위 비트 20만 개와 별도로 **지수 100..145 구간에 집중한 20만 개** 를 돌린다.
- 시간은 참고용: 소프트웨어 0.77 ns/변환, `FCVT` 0.32 ns (둘 다 스칼라 루프, `-S` 로 확인한 하드웨어 경로는 `fcvt h0, s0` 한 개). `acc=0000` 은 두 루프가 같은 값들을 XOR 해서 상쇄된 것 — 이것도 "결과가 같다" 는 증거다.

### Part C — 손실을 세는 것이 펌웨어의 일

`f16_pack` 의 분류 규칙 (위에서부터 먼저 맞는 것):

| 분류 | 조건 | 펌웨어 의미 |
|---|---|---|
| nan | 입력이 NaN | 고장 센서/0으로 나눔 — 상위로 보고 |
| overflow | 입력은 유한, 결과 ±Inf | 스케일 잘못/충격 — 포화 또는 스케일 조정 |
| exact | `f16_to_f32(h) == 입력 비트` | 손실 없음 (±0, ±Inf 입력 포함) |
| underflow | 0 아닌 입력이 ±0 으로 | 작은 신호 소실 |
| inexact | 그 밖의 반올림 | 정상적인 양자화 오차 |
| subnormal (겹침) | 결과가 FP16 비정규 | 정밀도가 11비트보다 적음 |

테스트 입력 12개에서 나온 값: `9.80665 → 0x48E7 = 9.8046875` (오차 −0.00196, 상대 2e-4), `2049 → 2048` (FP16 은 2048 넘어서 정수 간격이 2), `1e-6 → 0x0011 = 17·2^-24 ≈ 1.0133e-6` (비정규라 상대오차 1.3%), `1e-9 → 0` (underflow), `70000 → Inf`, `-1e6 → -Inf`. 리포트 = exact 4 (0, −0, 0.5, 2048) / inexact 4 / underflow 1 / overflow 2 / nan 1 / subnormal 1.

포화 모드에서 `70000 → 0x7BFF`, `-1e6 → 0xFBFF`. 실제 가속기 설계에서 "변환 오버플로는 Inf 냐 포화냐" 는 스펙 항목이다 (IEEE 기본은 Inf, 일부 ML 가속기와 정수 양자화는 포화를 택한다). 이 모델은 `saturate` 플래그로 둘 다 표현하고, 검증 엔지니어는 RTL 의 설정 레지스터 값에 맞춰 골든 모델 모드를 바꾼다.

**같은 변환이 각 칩에서 무엇이 되나** (`demo_asm.c`, 실제 출력에서 지시어 제거):

```text
$ cc -O2 -S -o - demo_asm.c                        # arm64 (M6) 기본 타깃
_to_half:   fcvt h0, s0 ; ret
_to_float:  fcvt s0, h0 ; ret
_to_bf16:   fcmp s0, s0 ; fmov w8, s0 ; ubfx w9, w8, #16, #1 ; mov w10, #32767
            add w9, w9, w8 ; add w9, w9, w10 ; orr w8, w8, #0x400000 ; csel w8, w8, w9, vs
            lsr w8, w8, #16 ; fmov s0, w8 ; ret
$ cc -O2 -mcpu=apple-m4 -S -o - demo_asm.c         # BF16 확장을 켠 타깃
_to_bf16:   bfcvt h0, s0 ; ret
$ clang --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfloat-abi=hard -mthumb -O2 -DNO_BF16 -S demo_asm.c -o -
to_half:    vcvtb.f16.f32 s0, s0 ; bx lr
to_float:   vcvtb.f32.f16 s0, s0 ; bx lr
$ clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2 -DNO_BF16 -S demo_asm.c -o -
to_half:    push {r7, lr} ; bl __aeabi_f2h ; pop {r7, pc}
to_float:   push {r7, lr} ; uxth r0, r0 ; bl __aeabi_h2f ; pop {r7, pc}
```

- arm64: 단일 명령 `FCVT`. 기본 타깃의 bf16 은 컴파일러가 **정수 RNE 트릭**(`+0x7FFF + lsb`, NaN 이면 `|0x400000`)을 펼친 것 — 추가 연습 1 의 답과 같은 코드다. `-mcpu=apple-m4` 처럼 BF16 확장을 켜면 `BFCVT` 한 개.
- Cortex-M4F: FPv4-SP 에 반정밀 변환 `VCVTB/VCVTT`(하위/상위 16비트 반쪽) 이 있다. 저장 형식으로만 FP16 을 쓰고 연산은 float 로.
- Cortex-M0: FPU 가 없어 런타임 라이브러리 `__aeabi_f2h` 호출 — 이 레슨의 `f32_to_f16` 이 바로 그 함수의 내용이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 58/58 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 58/58, 경고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `h = (b >> 16)...` 처럼 frac 을 그냥 잘라냄 (truncate) | 반올림 모드가 RZ(0 방향)가 됨. 0.1f·1/3·9.80665 는 우연히 맞지만 0.7f 는 `0x3999` (정답 `0x399A`) 로 1 ulp 틀림 — 꼬리가 반을 넘는 값마다 틀린다 | `rem` vs `half` 비교 후 `q++` |
| 동점에서 무조건 올림 (round-half-up) | `1+2^-11 → 0x3C01`, `2^-25 → 0x0001` — 하드웨어와 다르고 통계적으로 위로 편향 | `rem == half && (q & 1)` 일 때만 올림 |
| NaN 을 `sign \| 0x7C00 \| (f >> 13)` 로 변환 | payload 상위 10비트가 0 인 NaN(`0x7F800001`)이 **Inf** 가 됨 | quiet 비트 포함 `0x7E00` 을 OR |
| 비정규 FP16 결과를 0 으로 flush | 2^-24..2^-14 구간 값이 사라짐 (FTZ 는 별도 모드이지 기본이 아님) | shift = 14 − eh 로 계속 진행 |
| 오버플로 판정을 `x > 65504` 로 | 65504 < x < 65520 은 65504 로 내려가야 함 | 반올림 **후** 지수 캐리로 판정 (`(eh−1)<<10 + q`) |
| 반올림 캐리 `q == 0x800` 처리 누락 | `frac = q & 0x3FF` 만 쓰면 0x7FF+1 이 0 이 되어 값이 반토막 | 덧셈으로 합쳐 캐리가 지수로 |
| `1u << shift` 에서 shift ≥ 32 | 시프트 양 ≥ 폭은 UB (C11 6.5.7) | shift > 24 이면 먼저 0 반환 |
| `*(uint32_t *)&f` 로 비트 꺼내기 | strict aliasing 위반 (C11 6.5p7) | `memcpy(&u, &f, 4)` — `-O2` 에서 `fmov` 한 개로 최적화됨 |
| float 비정규 입력에 숨은 1 을 붙임 | exp=0 이면 숨은 비트 0, 지수는 −126 | `exp ? (frac \| 0x800000) : frac`, e=−149 |

## 8. 면접 Q&A

<details>
<summary>Q1. 0.1 + 0.2 != 0.3 인 이유를 비트 수준에서 설명해 보세요.</summary>

0.1, 0.2, 0.3 모두 이진 소수로 끝나지 않아 가장 가까운 double 로 반올림된다. double 에서 0.1 과 0.2 는 각각 올림된 값이고, 그 합을 다시 반올림한 결과가 0.3 의 가장 가까운 double 보다 1 ulp 크다 (`0.30000000000000004`). float 32 에서는 우연히 `0.1f + 0.2f == 0.3f` 가 참이다 — "항상 다르다" 가 아니라 "반올림이 세 번 일어나므로 같다는 보장이 없다" 가 정답이다.
꼬리질문: "그럼 비교는?" → `fabs(a−b) <= max(abs_tol, rel_tol·max(|a|,|b|))` 또는 ulp 거리(추가 연습 2). 금액·카운터는 정수(고정소수점)로.

</details>

<details>
<summary>Q2. FP16 의 최댓값, 최소 정규수, 최소 비정규수, 정밀도는?</summary>

최댓값 `(2 − 2^-10)·2^15 = 65504` (`0x7BFF`), 최소 정규 `2^-14 ≈ 6.10e-5`, 최소 비정규 `2^-24 ≈ 5.96e-8`, 유효숫자 11비트(10진 약 3.3자리), 1.0 에서의 ulp `2^-10 ≈ 9.77e-4`. 정수는 2048 까지 정확(2049 부터 빈칸).
꼬리질문: "bfloat16 은?" → 지수 8비트(float 와 같은 범위 ~3.4e38), 가수 7비트(8비트 정밀도). 그래서 ML 학습에서 FP16 은 그래디언트 언더플로 때문에 loss scaling 이 필요하지만 bf16 은 대개 필요 없다.

</details>

<details>
<summary>Q3. round-to-nearest-even 은 왜 기본 모드인가요? 하드웨어는 어떻게 구현하나요?</summary>

동점을 항상 올리면 평균 오차가 +½ ulp·(동점 비율) 만큼 한쪽으로 쌓인다. 짝수로 보내면 위·아래가 반반이라 편향이 없고, 결과 lsb 가 0 이 되어 다음 연산에서 다시 동점이 생길 확률도 줄어든다. 하드웨어는 남길 비트 아래에 guard·round 비트와 그 아래 전부를 OR 한 sticky 비트를 유지하고, `round_up = R & (sticky | lsb)` 한 줄로 결정한다 (이 레슨의 `rem` vs `half` 비교와 같은 식).
꼬리질문: "다른 모드?" → RZ(0 방향, 잘라내기), RP(+∞), RM(−∞) — 구간 산술, 그리고 float→int 캐스트는 C 에서 RZ.

</details>

<details>
<summary>Q4. 비정규수는 왜 있고, FTZ/DAZ 는 무엇인가요? 펌웨어에서 언제 신경 쓰나요?</summary>

비정규수가 없으면 `x != y` 인데 `x − y == 0` 이 될 수 있다 (점진적 언더플로가 이를 막음). 비용: 일부 FPU 는 비정규 입력/출력을 마이크로코드나 트랩으로 처리해 수십~수백 배 느리다 → DSP 루프(IIR 필터 꼬리가 0 으로 감쇠할 때)에서 갑자기 CPU 부하가 튄다. 그래서 FTZ(flush-to-zero, 결과) / DAZ(입력) 모드가 있다: Arm 은 FPCR(A64) / FPSCR(A32, Cortex-M) 의 FZ 비트, 반정밀은 별도 FZ16. 이 맥 프로세스의 FPCR 은 0(FZ 꺼짐)이었다.
꼬리질문: "FTZ 를 켜면 이 레슨의 변환 결과가 바뀌나?" → float 입력의 비정규는 어차피 FP16 에서 0 이라 입력 FZ 는 영향이 없다. 결과 쪽 FP16 비정규 처리는 구현/모드에 따라 다를 수 있으니 RTL 과 골든 모델의 모드를 맞춰야 한다.

</details>

<details>
<summary>Q5. qNaN 과 sNaN 의 차이, 그리고 NaN 을 FP16 으로 바꿀 때 payload 는?</summary>

frac 최상위 비트가 1 이면 quiet, 0 이면 signaling (IEEE 754-2008 권장, 대부분의 현대 ISA. 옛 MIPS 등은 반대였다). sNaN 을 연산/변환에 넣으면 Invalid 예외 플래그(Arm FPSR.IOC)가 서고 결과는 quiet NaN. Arm FCVT 는 기본 NaN 모드(FPCR.DN)가 꺼져 있으면 부호와 payload 상위 비트를 유지하고 quiet 비트를 켠다 — 이 레슨의 정책이고 2^32 전수 비교로 같음을 확인했다. DN=1 이면 항상 기본 NaN(FP16 `0x7E00`)이다.
꼬리질문: "NaN 끼리 비교하면?" → `x != x` 가 참인 유일한 값. `==` 로 NaN 검사 불가 → `isnan` 또는 `(bits & 0x7FFFFFFF) > 0x7F800000`.

</details>

<details>
<summary>Q6. float 를 정수 비트로 볼 때 왜 memcpy 를 쓰나요? union 은?</summary>

`*(uint32_t*)&f` 는 strict aliasing 위반(UB)이라 `-O2` 에서 순서가 바뀌거나 지워질 수 있다. `memcpy` 는 정의된 동작이고 컴파일러가 `fmov w0, s0` 한 개로 바꾼다. C 에서는 union type punning 도 허용된다(C11 6.5.2.3 각주 95), C++ 에서는 UB 이고 C++20 `std::bit_cast` 를 쓴다.
꼬리질문: "Cortex-M 에서 이 변환을 ISR 안에서 해도 되나?" → FPU 레지스터를 쓰면 lazy stacking(FPCCR.LSPEN)로 예외 진입 시 FP 컨텍스트 저장 비용이 생긴다. 정수 경로(이 레슨 코드)는 FPU 를 건드리지 않는다.

</details>

<details>
<summary>Q7. double → float → half 로 두 번 내리면 double → half 한 번과 같나요?</summary>

아니다 — **이중 반올림(double rounding)**. 예: `d = 1 + 2^-11 + 2^-40` 은 FP16 동점보다 살짝 위라 직접 변환은 `0x3C01` 이지만, float 로 먼저 내리면 `2^-40` 이 사라져 정확히 동점 `1 + 2^-11` → 짝수 `0x3C00`. 추가 연습 3 에서 무작위 동점 근처 10만 개 중 22174 개가 틀렸다. 해결: 중간 단계를 **round-to-odd** 로 하면(중간 정밀도 ≥ 최종 + 2비트) 최종 결과가 정확하다. 반면 FP16 끼리의 +−×÷√ 을 float 로 계산해 FP16 으로 내리는 것은 안전하다 (24 ≥ 2·11 + 2 조건).
꼬리질문: "하드웨어 검증에서 이게 왜 중요?" → 골든 모델을 double 로 짜고 마지막에 캐스트하면 RTL 과 드물게 1 ulp 어긋나서 "가끔 실패하는 테스트" 가 된다.

</details>

## 9. 추가 연습

### 연습 1 — float32 → bfloat16 (RNE, NaN quiet)

bf16 은 float 의 상위 16비트다. 잘라내기 대신 RNE 로, NaN 은 Inf 가 되지 않게 구현하라.

<details>
<summary>답 보기</summary>

`(b + 0x7FFF + ((b >> 16) & 1)) >> 16` — 하위 16비트가 0x8000 을 넘으면 0x7FFF 를 더할 때 캐리가 생기고, 정확히 0x8000 이면 lsb 가 1 일 때만 캐리. 캐리가 지수로 흘러 FLT_MAX → Inf 도 자동. NaN 은 먼저 `(b | 0x400000) >> 16` — 단순히 자르면 `0x7F800001 → 0x7F80 = Inf`. Apple clang 기본 arm64 타깃이 `(__bf16)f` 에 대해 정확히 이 정수 시퀀스를 생성한다 (§5 Part C asm). practice.c 는 30만 개를 그 컴파일러 결과와 비교한다.

</details>

### 연습 2 — next_up 과 ulp 거리

`f32_next_up(b)` (nextafterf(x, +∞) 와 같게) 와 `f32_ulp_distance(a, b)` 를 비트 연산으로. `0.1f` 를 10번 더한 결과는 1.0 에서 몇 ulp 떨어져 있나?

<details>
<summary>답 보기</summary>

양수는 비트 패턴이 값 순서와 같으므로 `b + 1`, 음수는 크기가 줄어야 커지므로 `b − 1`, `−0 → 0x00000001`, +Inf·NaN 은 그대로. 거리는 부호-크기를 정수 키로 바꿔(음수는 `−(b & 0x7FFFFFFF)`) 뺀다. `0.1f` 10번 합 = `0x3F800001` = 1.0 에서 **1 ulp** 위 (1.00000012). 테스트에서 하드웨어 합산 결과로 확인.

</details>

### 연습 3 — 이중 반올림과 round-to-odd

`d = 1 + 2^-11 + 2^-40` 에서 `(_Float16)d` 와 `(_Float16)(float)d` 가 다름을 보이고, double → float 를 round-to-odd 로 하는 `f64_to_f32_round_odd` 를 만들어 `f32_to_f16` 과 합치면 하드웨어 `FCVT Hd, Dn` 과 같아짐을 확인하라.

<details>
<summary>답 보기</summary>

round-to-odd: 버릴 29비트를 잘라내고, 버린 비트 중 1 이 하나라도 있으면 결과 lsb 를 1 로 (sticky 를 lsb 에 보존). 결과가 홀수면 "정확한 값이 아니었다" 는 정보가 남아서, 다음 RNE 단계가 그것을 가짜 동점으로 오해하지 않는다. 실행 결과: `[연습3] 100000 개 중 double→float→half 오답 22174 개, round-to-odd 경로 오답 0 개`.

</details>

### 연습 4 — FP16 이 정확히 담는 정수의 한계와 누산 정체

본문 변환기만으로 "FP16 으로 정확히 표현되지 않는 가장 작은 양의 정수" 를 찾아라. 그리고 `_Float16 acc = 2048; acc += 1` 을 100 번 하면?

<details>
<summary>답 보기</summary>

2049 (11비트 유효숫자 → 2^11 = 2048 까지 연속, 그 위는 간격 2). 2048 + 1 = 2049 는 2048 과 2050 의 정확한 동점이라 짝수 쪽인 2048 로 → 100 번 더해도 2048. **FP16 누산기의 정체(stagnation)** 로, 그래서 텐서 코어/NPU 는 FP16 곱셈 + FP32 누산을 쓴다. float 는 `2^24 + 1 = 16777217` 에서 같은 일이 생긴다 (1 kHz 샘플 카운터를 float 로 두면 2^24 / 1000 ≈ 16777 초, 약 4.7 시간 만에 멈춘다).

</details>

### 연습 5 — FP16 전순서(total order) 정수 키

정렬·최댓값 검색을 정수 비교로 하려고 한다. `f16_order_key(h)` 를 만들어 NaN 을 뺀 모든 FP16 에서 `key(a) < key(b)` ⇔ `a < b` (단 −0 < +0) 가 되게 하라.

<details>
<summary>답 보기</summary>

양수는 부호 비트를 켜서(`h | 0x8000`) 위쪽 절반으로, 음수는 전체 비트를 뒤집어(`~h`) 크기가 클수록 작은 키로. 그러면 `0xFC00(−Inf) → 0x03FF` 가 가장 작고 `0x7C00(+Inf) → 0xFC00` 이 가장 크다. 기수 정렬(radix sort)과 하드웨어 max-pool 비교기에서 쓰는 트릭이고, IEEE 754-2008 의 totalOrder 와 (NaN 을 뺀 범위에서) 같다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
#   [연습3] 100000 개 중 double→float→half 오답 22174 개, round-to-odd 경로 오답 0 개
# 26/26 passed
```

## 10. 핵심 정리

- 값 = `(−1)^s × 1.f × 2^(e−bias)`; float32 1·8·23 bias 127, FP16 1·5·10 bias 15, bf16 1·8·7. exp=0 → 0/비정규(지수는 1−bias), exp=max → Inf/NaN.
- 0.1f = `0x3DCCCCCD` = 13421773·2^-27 (0.1 보다 1.49e-9 큼), FP16 0.1 = `0x2E66` = 0.0999755859375 (작음).
- FP16: max 65504 `0x7BFF`, **65520 이상 → Inf**, 최소 정규 2^-14, 최소 비정규 2^-24, 정수 2048 까지 정확.
- RNE = `rem > half` 이면 올림, `rem == half` 면 lsb 가 1 일 때만 올림. 하드웨어는 guard/round/sticky.
- 변환 구현: 특수값 → 범위 → shift(정규 13, 비정규 14−eh) → RNE → `((eh−1)<<10)+q` 로 캐리를 지수에 흘림.
- NaN 은 quiet 비트를 켜서 변환 (안 켜면 NaN → Inf 버그). Arm FCVT(DN=0)와 2^32 전수 일치 확인.
- 이중 반올림 주의: double→float→half ≠ double→half. 중간을 round-to-odd 로.
- arm64 `FCVT`/`BFCVT`, Cortex-M4F `VCVTB`, Cortex-M0 `__aeabi_f2h`(소프트웨어 = 이 레슨 코드).

## 참고자료

- IEEE Std 754-2019, *IEEE Standard for Floating-Point Arithmetic* (binary16/32 형식, 반올림 규칙, NaN, totalOrder)
- [Arm Architecture Reference Manual for A-profile — FCVT, BFCVT, FPCR (DN, FZ, FZ16, AHP)](https://developer.arm.com/documentation/ddi0487/latest)
- [Arm Cortex-M4 Technical Reference Manual — FPU (VCVTB/VCVTT, FPSCR)](https://developer.arm.com/documentation/100166/0001)
- [Clang Language Extensions — `_Float16`, `__bf16`](https://clang.llvm.org/docs/LanguageExtensions.html)
- David Goldberg, "What Every Computer Scientist Should Know About Floating-Point Arithmetic", ACM Computing Surveys 1991
- S. Boldo, G. Melquiond, "Emulation of FMA and correctly rounded sums: proved algorithms using rounding to odd", IEEE Trans. Computers 2008
- [Random ASCII (Bruce Dawson) — Comparing Floating Point Numbers](https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/)
- 관련 레슨: C-B04 (필드 GET/SET), C-B01 (비트 연산), ARCH-B01 (2의 보수 · 부호 확장), ARCH-B02 (NZCV 플래그)
