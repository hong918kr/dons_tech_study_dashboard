---
id: ALGO-M15
time: 80분
updated: 2026-11-11
---
# ALGO-M15 정수 산술 알고리즘: isqrt·거듭제곱·GCD·상수 나눗셈

## 학습 목표

- 비트 단위 정수 제곱근(digit-by-digit isqrt, uint32 → uint16)을 짜고, 불변식 "res = P·2^(j+1), rem = x − P²" 로 왜 맞는지 증명한다. 2^32 개 전부를 정의 r² ≤ x < (r+1)² 와 대조한다.
- 모듈러 빠른 거듭제곱(square-and-multiply)을 uint64 중간값으로 넘침 없이 짜고, 128비트 정수 없이 64비트 법의 `mulmod` 도 만든다 (페르마 소정리로 검증).
- 유클리드 GCD 와 이진 GCD(Stein)를 비교한다: 나눗셈기 없는 MCU 에서 왜 Stein 인지, 단계 수(피보나치 최악 45단계)와 이 맥의 실측.
- `x / 10 = (x · 0xCCCCCCCD) >> 35` 를 **유도·증명**하고, 임의의 상수 d 에 대한 매직 넘버(곱수·시프트·add 보정)를 계산하는 함수를 짠다. 컴파일러가 Cortex-M4(`umull`)·Cortex-M0(`__aeabi_uidiv`)·arm64 에서 실제로 내는 코드와 맞춰 본다.
- 펌웨어 쓰임: 가속도계 크기 |a|, printf 없는 10진 출력, 주파수 호핑 PRNG 의 슬롯 점프, 클럭 비율 약분·LCM.

## 1. 문제

FPU 도 나눗셈기도 없는 Cortex-M0 에서 "제곱근·나눗셈·나머지를 정수로만" 은 펌웨어 코딩 면접의 단골이다 — 코드는 짧지만 **넘침, 경계값, 증명**에서 실력이 갈린다.

MCU 마다 쓸 수 있는 연산이 다르다. Cortex-M0/M0+ 에는 하드웨어 나눗셈이 없어서 `x / 10` 이 런타임 라이브러리 함수 호출(`__aeabi_uidiv`)이 되고, 32×32→64 곱(`UMULL`)도 없다. Cortex-M3/M4 에는 `UDIV` 와 `UMULL` 이 있지만, M4F 의 FPU 는 단정밀도(float)라서 `sqrtf` 로는 32비트 정수의 제곱근이 정확하지 않다(이 레슨에서 2^32 개 중 6,986,365 개 틀림을 실측). 그래서 정수 알고리즘 네 가지를 손에 쥐고 있어야 한다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" font-size="14">MCU 가 가진 연산 → 없으면 이 레슨의 정수 알고리즘으로 대신</text>
  <text x="150" y="48">ADD/SUB/시프트</text><text x="268" y="48">MUL 32→32</text><text x="372" y="48">UMULL 32×32→64</text>
  <text x="502" y="48">UDIV</text><text x="590" y="48">FPU sqrt</text>
  <text x="10" y="78">Cortex-M0/M0+</text>
  <rect x="150" y="62" width="100" height="24" class="f-ok"/><rect x="268" y="62" width="90" height="24" class="f-ok"/>
  <rect x="372" y="62" width="110" height="24" class="f-warn"/><rect x="500" y="62" width="70" height="24" class="f-warn"/>
  <rect x="588" y="62" width="130" height="24" class="f-warn"/>
  <text x="380" y="79">없음 → lmul</text><text x="506" y="79">없음</text><text x="596" y="79">없음</text>
  <text x="10" y="112">Cortex-M3</text>
  <rect x="150" y="96" width="100" height="24" class="f-ok"/><rect x="268" y="96" width="90" height="24" class="f-ok"/>
  <rect x="372" y="96" width="110" height="24" class="f-ok"/><rect x="500" y="96" width="70" height="24" class="f-ok"/>
  <rect x="588" y="96" width="130" height="24" class="f-warn"/><text x="596" y="113">없음</text>
  <text x="10" y="146">Cortex-M4F</text>
  <rect x="150" y="130" width="100" height="24" class="f-ok"/><rect x="268" y="130" width="90" height="24" class="f-ok"/>
  <rect x="372" y="130" width="110" height="24" class="f-ok"/><rect x="500" y="130" width="70" height="24" class="f-ok"/>
  <rect x="588" y="130" width="130" height="24" class="f-mut"/><text x="596" y="147">float 만 (24비트)</text>
  <line x1="10" y1="168" x2="730" y2="168" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="10" y="182" width="170" height="70" class="f-acc"/>
  <text x="18" y="202">isqrt32</text><text x="18" y="220">시프트·뺄셈·비교만</text><text x="18" y="238">16단계, 분기 고정</text>
  <rect x="195" y="182" width="170" height="70" class="f-acc"/>
  <text x="203" y="202">powmod</text><text x="203" y="220">제곱-곱하기 O(log e)</text><text x="203" y="238">uint64 중간값</text>
  <rect x="380" y="182" width="170" height="70" class="f-acc"/>
  <text x="388" y="202">GCD: Euclid / Stein</text><text x="388" y="220">Stein = % 없이</text><text x="388" y="238">시프트·뺄셈</text>
  <rect x="565" y="182" width="165" height="70" class="f-acc"/>
  <text x="573" y="202">x / 상수 d</text><text x="573" y="220">곱수 m, 시프트 p</text><text x="573" y="238">÷10: 0xCCCCCCCD, 35</text>
</svg>
```

### Part A — 네 가지 기본 연산

- `isqrt32(x)` — floor(√x), uint32 → uint16. 비트 단위(2비트씩) 방법, `%`·`/`·곱셈 없이.
- `powmod32(b, e, m)` — b^e mod m, m ≥ 1, 곱은 `uint64_t` 에서. 0^0 = 1, mod 1 = 0.
- `gcd_euclid(a, b)` 와 `gcd_binary(a, b)` (Stein, `%` 금지). gcd(0, 0) = 0.
- `div10(x)` = `(x · 0xCCCCCCCD) >> 35`, `mod10(x)` = x − 10·div10(x) (곱셈 대신 시프트 두 개).
- 테스트: isqrt 는 x < 2^20 전수 + 모든 k² 과 k² ± 1 + 랜덤 10^6, powmod 는 작은 지수 직접 곱 + 큰 지수 왼→오 기준 + 페르마, GCD 는 0..200 전수(최대 공약수 직접 탐색) + 랜덤 30만(**베주 항등식**으로 검증), ÷10 은 x < 2^24 전수 + 최상위 10^6 + 랜덤. 2^32 전체는 `demo_exhaustive.c`.

### Part B — 일반 상수 나눗셈과 64비트 mulmod

- `magic_u32(d)` — 아무 d ≥ 1 에 대해 (곱수, 시프트, 모드)를 계산한다. 2의 거듭제곱 → 시프트만(mode 0), 곱수가 32비트에 들어가면 `(x·m) >> p`(mode 1), 33비트면 add 보정(mode 2). `magic_apply(g, x)` 로 적용.
- `mulmod64(a, b, m)` — 128비트 정수 없이 a·b mod m (m 이 2^63 이상이어도), `powmod64`.
- 테스트: d = 10/3/7/1000/641 의 알려진 상수, d = 1..2000 전부 + 2^k ± 1 + 랜덤 d 4000개 × 경계 x(0, d−1, d, 몫 경계 q·d − 1, UINT32_MAX …) + 랜덤 x. mulmod64 는 `__uint128_t`(테스트 쪽만)와 20만 번, powmod64 는 소수 2^64 − 59 와 2^61 − 1 에서 페르마.

### Part C — 펌웨어에서

- `accel_mag(x, y, z)` — 3축 가속도계 raw(int16)의 크기 round(√(x²+y²+z²)). 최악 3·2^30 이 uint32 에 들어가는지 확인, 반올림 보정의 증명.
- `u32_to_dec(x, buf)` — printf 없는 로그 출력. `/`·`%` 없이 div10/mod10 만.
- `lcg_jump(x0, k)` — Park–Miller PRNG(x' = 16807·x mod 2^31−1)로 호핑 채널을 고르는 무선 노드가 잠에서 깨어 **슬롯 k 의 상태를 O(log k)** 로 계산.
- `ratio_reduce(&n, &d)`, `lcm32(a, b)` — 48000/44100 → 160/147 (PLL·샘플레이트 변환 비율), 공통 클럭 7.056 MHz, 넘치면 0.

## 2. Problem (English)

You are writing a math helper library for a Cortex-M0 sensor node (no FPU, no hardware divider, no 32×32→64 multiply). Implement:

```c
uint16_t isqrt32(uint32_t x);                               /* floor(sqrt(x)), no division, no multiply */
uint32_t powmod32(uint32_t b, uint32_t e, uint32_t m);      /* b^e mod m, m >= 1, 0^0 = 1 */
uint32_t gcd_euclid(uint32_t a, uint32_t b);                /* gcd(0,0) = 0 */
uint32_t gcd_binary(uint32_t a, uint32_t b);                /* Stein: no '%' or '/' */
uint32_t div10(uint32_t x);  uint32_t mod10(uint32_t x);    /* exact for ALL uint32, no '/' or '%' */
udiv_magic magic_u32(uint32_t d);                           /* multiplier + shift for x / d, d >= 1 */
uint32_t   magic_apply(udiv_magic g, uint32_t x);           /* == x / d for every uint32 x */
uint64_t mulmod64(uint64_t a, uint64_t b, uint64_t m);      /* no 128-bit integers */
```

Constraints: no dynamic allocation, no floating point, no compiler builtins; every function must be correct on the full input range (you will be asked to prove it). O(1) memory, O(log) time where applicable.

Examples:

```text
isqrt32(8) = 2           isqrt32(4294967295) = 65535
powmod32(2, 10, 1000) = 24        powmod32(3, 4294967290, 4294967291) = 1   (Fermat, p = 2^32 - 5)
gcd(48000, 44100) = 300            gcd(0, 0) = 0
div10(4294967295) = 429496729      mod10(4294967295) = 5
magic_u32(10) = {mul 0xCCCCCCCD, shift 35}     magic_u32(7) = {mul 0x24924925, shift 2, add-fixup}
accel_mag(-32768, -32768, -32768) = 56756      lcg_jump(1, 10000) = 1043618065
```

Follow-ups: Why is `>> 35` the smallest shift that works for /10? What does the compiler emit for `x / 10` on a Cortex-M0? When is Euclid's algorithm slowest? How would you compute `a * b % m` for a 64-bit `m`?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열다섯 군데(Part A 6, Part B 4, Part C 5). 기준 구현(`is_floor_sqrt`, `ref_powmod`, 베주 계수로 확인하는 `is_gcd`)과 하네스는 주어진다. 스텁은 0 이나 입력 그대로를 돌려줘서 멈추거나 죽지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:168  isqrt32(1) == 1
…
isqrt32: x < 2^20 all + k²-1/k²/k²+1 (k<=65535) + 1e6 random, bad=2245179
…
magic_u32: 6000 divisors (mode0 6000, mode1 0, mode2 0), ~190 x each, bad=1148172
…
ratio_reduce/lcm32: 100000 random (coprime + cross-multiply, lcm overflow -> 0), bad=193713
FAIL starter.c:405  bad == 0
13/67 passed
```

(exit 1. 통과한 13개는 스텁 값이 우연히 맞는 경우 — isqrt32(0) = 0, gcd(12, 0) = 12, mod 1 = 0 등.)

## 4. 힌트

<details><summary>힌트 1 — isqrt 는 "손 개평법"의 2진판</summary>

10진 개평법은 두 자리씩 묶어 근의 한 자리를 정한다. 2진법이면 두 비트씩 묶고, 근의 비트는 0 아니면 1 이니 "넣어 볼까?"만 판단하면 된다. 지금까지의 근 P 에 2^j 를 더해도 (P + 2^j)² ≤ x 인가? (P + 2^j)² − P² = 2P·2^j + 4^j 이므로, 변수 하나에 2P·2^j 를 들고 다니면 비교가 덧셈 하나다.

</details>

<details><summary>힌트 2 — 거듭제곱은 지수의 2진 비트</summary>

b^13 = b^8 · b^4 · b^1 (13 = 1101₂). b, b², b⁴, b⁸ 을 제곱으로 만들면서 비트가 1 인 것만 곱한다. 매 곱 뒤에 `% m`. m < 2^32 이면 두 수 < m 의 곱 < 2^64 이라 `uint64_t` 로 충분하다. `r = 1 % m` 으로 시작해야 m = 1 이 맞는다.

</details>

<details><summary>힌트 3 — Stein 의 세 규칙</summary>

(1) 둘 다 짝수: gcd(a, b) = 2·gcd(a/2, b/2). (2) 하나만 짝수: 그 2 는 공약수가 아니니 버린다. (3) 둘 다 홀수: gcd(a, b) = gcd(a, b − a), 그리고 b − a 는 짝수 → (2) 로. 공통 2^k 는 맨 처음에 세어 두고 맨 끝에 `<< k`.

</details>

<details><summary>힌트 4 — ÷10 의 곱수와 오차</summary>

m = ⌈2^35 / 10⌉ = 3435973837 = 0xCCCCCCCD. 그러면 x·m / 2^35 = x/10 + (작은 오차). 오차가 "x/10 의 소수 부분이 가장 클 때(0.9)도 1 을 못 넘는" 크기인지 확인하라. 0xCCCCCCCC(내림)로 하면 x = 10 에서 바로 틀린다 — 왜?

</details>

<details><summary>힌트 5 — 일반 d: 가장 작은 p 를 찾는 루프</summary>

p = 32, 33, … 에 대해 m = ⌈2^p/d⌉, e = m·d − 2^p (0 ≤ e < d). 조건 e ≤ 2^(p−32) 이 처음 맞는 p 를 쓴다. p = 32 + ⌈log₂ d⌉ 에서는 e < d ≤ 2^(p−32) 라 반드시 맞는다. m 이 2^32 이상이면(d = 7) 곱수 하위 32비트만 저장하고 `x` 를 한 번 더 더하는 보정을 넣되, x + t 가 넘치지 않게 `((x − t) >> 1) + t`.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 질문으로 범위 확인 | 입력 범위(uint32 전부? 음수?), floor 인가 반올림인가, 타깃(M0 = 나눗셈·UMULL 없음, M4F = float FPU), m = 0·1, gcd(0, 0), 상수 d 가 컴파일 시점에 정해지나 |
| 2. 단순 해법 | isqrt: r 을 0 부터 올리며 r² ≤ x (O(√x) = 65536번), 이분 탐색 O(32)(ALGO-B02). 거듭제곱: e 번 곱하기 O(e). GCD: 공약수 직접 탐색. ÷10: 반복 뺄셈 |
| 3. 개선 + 불변식 | isqrt: 비트마다 결정, 불변식 res = P·2^(j+1), rem = x − P². powmod: r·b^e 불변. GCD: gcd 가 보존되는 변환. ÷10: m = ⌈2^p/d⌉ 의 오차 한계 |
| 4. 코드 | 아래 Part A/B/C |
| 5. 소리 내어 말할 테스트 | 0, 1, k² − 1 / k² / k² + 1, UINT32_MAX, 65535², m = 1, e = 0, gcd(0,0), 연속 피보나치, x ≡ 9 (mod 10) 인 최대 x(4294967289), d = 2^k·2^k ± 1 |
| 6. 복잡도 | isqrt 16단계 고정, powmod O(log e) 곱 2개씩, Euclid O(log min) (피보나치 최악), Stein ≤ 64 뺄셈, ÷상수 O(1). 모두 추가 메모리 O(1), 재귀 없음 |

### Part A — isqrt32: 근의 비트를 위에서부터 하나씩

S = ⌊√x⌋ 의 비트를 j = 15 → 0 순서로 정한다. P = "S 에서 j+1 번 비트 이상만 남긴 값" 이라 하면, 비트 j 가 1 인지는 **(P + 2^j)² ≤ x 인가**와 같다 — S − P < 2^(j+1) 이므로 "P + 2^j ≤ S" ⇔ "S 의 j 번 비트 = 1", 그리고 S 는 r² ≤ x 인 최대 r 이므로 "P + 2^j ≤ S" ⇔ "(P + 2^j)² ≤ x".

비교를 덧셈으로 만드는 것이 핵심이다. (P + 2^j)² − P² = 2P·2^j + 4^j. 그래서 코드는 두 변수를 들고 다닌다.

**불변식** (one = 4^j 인 반복 직전): `res = P·2^(j+1)`, `rem = x − P²`.

- 시험: `rem >= res + one` ⇔ x − P² ≥ P·2^(j+1) + 4^j ⇔ (P + 2^j)² ≤ x. ✔
- 넣을 때: P' = P + 2^j → 다음(j−1) 의 res = P'·2^j = P·2^j + 4^j = `(res >> 1) + one`, rem 은 그 차이만큼 줄어든다. 안 넣을 때: res = P·2^j = `res >> 1`.
- 끝(one = 0, 즉 j = −1): res = P·2^0 = S. ✔

넘침: res ≤ 2S < 2^17, one ≤ 2^30 이라 `res + one` < 2^31. **곱셈·나눗셈이 하나도 없다**. `while (one > rem) one >>= 2;` 는 앞쪽 0 비트를 건너뛰는 최적화일 뿐 정확성과 무관하다(넣으면 안 되는 비트를 미리 버리는 것).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" font-size="14">x = 200 = 11 00 10 00₂ — 두 비트(4^j)마다 근의 비트 하나</text>
  <text x="10" y="55">x 의 비트 쌍</text>
  <rect x="130" y="36" width="70" height="28" class="f-mut"/><text x="156" y="55">11</text>
  <rect x="220" y="36" width="70" height="28" class="f-mut"/><text x="246" y="55">00</text>
  <rect x="310" y="36" width="70" height="28" class="f-mut"/><text x="336" y="55">10</text>
  <rect x="400" y="36" width="70" height="28" class="f-mut"/><text x="426" y="55">00</text>
  <text x="10" y="95">근 S = 14</text>
  <rect x="130" y="76" width="70" height="28" class="f-ok"/><text x="160" y="95">1</text>
  <rect x="220" y="76" width="70" height="28" class="f-ok"/><text x="250" y="95">1</text>
  <rect x="310" y="76" width="70" height="28" class="f-ok"/><text x="340" y="95">1</text>
  <rect x="400" y="76" width="70" height="28" class="f-warn"/><text x="430" y="95">0</text>
  <text x="150" y="124">j=3</text><text x="240" y="124">j=2</text><text x="330" y="124">j=1</text><text x="420" y="124">j=0</text>
  <text x="500" y="55">시험값 = res + one</text>
  <text x="500" y="75">= 2P·2^j + 4^j</text>
  <text x="500" y="95">= (P + 2^j)² − P²</text>
  <text x="500" y="124">rem ≥ 시험값 → 비트 1</text>
  <line x1="10" y1="140" x2="730" y2="140" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="10" y="162">P (지금까지 근)</text>
  <text x="132" y="162">0 → 8</text><text x="222" y="162">8 → 12</text><text x="312" y="162">12 → 14</text><text x="402" y="162">14</text>
  <text x="10" y="186">rem = x − P²</text>
  <text x="132" y="186">200 → 136</text><text x="222" y="186">136 → 56</text><text x="312" y="186">56 → 4</text><text x="402" y="186">4</text>
  <text x="10" y="210">res = P·2^(j+1)</text>
  <text x="132" y="210">0 → 64</text><text x="222" y="210">64 → 48</text><text x="312" y="210">48 → 28</text><text x="402" y="210">28 → 14</text>
  <text x="10" y="238">끝: res = 14 = ⌊√200⌋ (14² = 196 ≤ 200 &lt; 225 = 15²)</text>
</svg>
```

트레이스 (x = 200, 실제로 코드를 따라간 값):

| one = 4^j | res (전) | rem (전) | 시험 res + one | 비트 | res (후) | rem (후) |
|---|---|---|---|---|---|---|
| 64 (j=3) | 0 | 200 | 64 | 1 | 64 | 136 |
| 16 (j=2) | 64 | 136 | 80 | 1 | 48 | 56 |
| 4 (j=1) | 48 | 56 | 52 | 1 | 28 | 4 |
| 1 (j=0) | 28 | 4 | 29 | 0 | 14 | 4 |

끝에 `rem` = x − S² 가 남는다는 것도 쓸모 있다 — Part C 의 반올림이 바로 이 값을 쓴다.

### Part A — powmod32: 제곱-곱하기

```c
uint64_t r = 1u % m, b = base % m;
while (e) { if (e & 1) r = r * b % m;  b = b * b % m;  e >>= 1; }
```

**불변식**: r · b^e ≡ base^e₀ (mod m). 처음 r = 1, b = base, e = e₀ 라 성립. e 가 홀수면 r·b^e = (r·b)·b^(e−1), 그다음 b^(e−1) = (b²)^((e−1)/2) — `e >>= 1` 이 (e−1)/2 와 같다. 짝수면 바로 (b²)^(e/2). 끝에 e = 0 → r ≡ base^e₀. 곱 횟수 ≤ 2·⌈log₂(e+1)⌉, 32비트 지수면 최대 64번의 64비트 `%`.

넘침: r, b < m ≤ 2^32 − 1 이라 곱 ≤ (2^32 − 2)² < 2^64. `1u % m` 은 m = 1 을 위해서다(아무 수 mod 1 = 0). 트레이스 3^13 mod 100 (13 = 1101₂):

| e (2진) | 비트 | r | b (다음) |
|---|---|---|---|
| 1101 | 1 | 1·3 = 3 | 9 |
| 110 | 0 | 3 | 81 |
| 11 | 1 | 3·81 = 243 → 43 | 6561 → 61 |
| 1 | 1 | 43·61 = 2623 → 23 | — |

3^13 = 1594323 → 23 ✔. 테스트는 왼쪽→오른쪽(MSB 부터 제곱 후 곱) 기준 구현과 20만 번, 직접 곱하기와 2만 번, 두 소수(2^31 − 1, 2^32 − 5)에서 a^(p−1) ≡ 1 을 2만 번씩 맞춘다.

### Part A — GCD: Euclid 와 Stein

**Euclid**: gcd(a, b) = gcd(b, a mod b). a 와 b 의 공약수 집합 = b 와 a − qb 의 공약수 집합이므로(어느 쪽 공약수든 다른 쪽 선형 결합을 나눈다) 최대값도 같다. 단계 수 최악은 **연속 피보나치 수**(Lamé): (F₄₇, F₄₆) = (2971215073, 1836311903) — uint32 에 들어가는 가장 큰 쌍 — 은 **45단계**(이 레슨 `demo_bench` 실측). 랜덤 uint32 쌍 2^20 개에선 평균 18.76, 최대 36.

**Stein**: 나눗셈 없이 시프트·뺄셈·비교만. 규칙(힌트 3)이 각각 gcd 를 보존하므로 맞다. 끝나는 이유: 루프 한 바퀴마다 a, b 가 홀수이고 b ≥ a 일 때 b' = (b − a) 를 적어도 한 번 반으로 나누니 **곱 a·b 가 매번 절반 이하**가 된다 → 최대 log₂(a·b) < 64 바퀴. 실측: 랜덤 평균 22.07, 최대 31 뺄셈, (2^32 − 1, 1) 은 32 뺄셈, 피보나치 쌍은 26.

트레이스 gcd(48000, 44100):

| Euclid | a | b | a mod b |
|---|---|---|---|
| 1 | 48000 | 44100 | 3900 |
| 2 | 44100 | 3900 | 1200 |
| 3 | 3900 | 1200 | 300 |
| 4 | 1200 | 300 | 0 → **300** |

Stein: 공통 2² (k = 2) → (12000, 11025) → a 를 홀수로 375. 이후 (a, b) = (375, 11025) → b − a = 10650 → 반으로 5325 → (375, 5325) → 4950 → 2475 → (375, 2475) → 2100 → 525 → (375, 525) → 150 → 75, 교환 → (75, 375) → 300 → 75 → (75, 75) → 0. 답 75 << 2 = **300**. 뺄셈 6번, 대신 시프트가 많다.

검증이 재미있다: GCD 의 기준 구현으로 GCD 를 쓰면 같은 버그를 공유할 수 있다. 그래서 테스트는 확장 유클리드로 **베주 계수** s, t 를 구해 s·a + t·b = g 이고 g | a, g | b 인지 본다. 공약수 c 는 반드시 s·a + t·b = g 를 나누니 g 는 **최대** 공약수다 — 수학적으로 완결된 검사.

### Part A — ÷10 = 곱하기 0xCCCCCCCD, >> 35

m = ⌈2^35/10⌉ = 3435973837 = 0xCCCCCCCD, e = 10m − 2^35 = 2. x = 10q + r (0 ≤ r ≤ 9) 이면

  x·m / 2^35 = x/10 + x·e/(10·2^35) = q + r/10 + ε,  0 ≤ ε = x/(5·2^35) < 2^32/(5·2^35) = 1/40.

r/10 + ε ≤ 0.9 + 0.025 < 1 이므로 floor 는 **정확히 q**. 모든 uint32 x 에서 성립하고, `demo_exhaustive` 로 2^32 개 전부 확인했다(0개 틀림). `mod10` 은 x − 10q 를 `(q << 3) + (q << 1)` 로.

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" font-size="14">x·m/2^35 은 x/10 보다 ε 만큼 크다 — [q, q+1) 를 벗어나지 않으면 floor 가 정확</text>
  <line x1="60" y1="90" x2="700" y2="90" stroke="currentColor"/>
  <line x1="60" y1="78" x2="60" y2="102" stroke="currentColor"/><text x="54" y="122">q</text>
  <line x1="700" y1="78" x2="700" y2="102" stroke="currentColor"/><text x="684" y="122">q + 1</text>
  <circle cx="60" cy="90" r="4" class="f-ok"/><circle cx="124" cy="90" r="4" class="f-ok"/>
  <circle cx="188" cy="90" r="4" class="f-ok"/><circle cx="252" cy="90" r="4" class="f-ok"/>
  <circle cx="316" cy="90" r="4" class="f-ok"/><circle cx="380" cy="90" r="4" class="f-ok"/>
  <circle cx="444" cy="90" r="4" class="f-ok"/><circle cx="508" cy="90" r="4" class="f-ok"/>
  <circle cx="572" cy="90" r="4" class="f-ok"/><circle cx="636" cy="90" r="4" class="f-ok"/>
  <text x="70" y="70">x/10 = q + r/10, r = 0..9 (점)</text>
  <rect x="636" y="84" width="16" height="12" class="f-acc"/>
  <text x="500" y="150">r = 9 에 ε &lt; 1/40 을 더해도</text><text x="500" y="168">q + 0.925 &lt; q + 1 ✔</text>
  <rect x="636" y="40" width="64" height="12" class="f-warn"/>
  <text x="330" y="50">p = 34 (e = 6): ε 가 0.1 을 넘을 수 있음</text>
  <text x="10" y="194">p = 34 이면 ε = 6x/(10·2^34) — x ≥ 2863311531 에서 0.1 이상 → 첫 반례 x = 2863311539 (실측)</text>
</svg>
```

**왜 35 가 최소인가.** p = 34 의 올림 곱수는 0x66666667, e = 6. 위 식에서 ε = 6x/(10·2^34) 가 r = 9 인 x 에서 1/10 이상이 되면 틀린다: x ≥ 2^34/6 ≈ 2863311530.7 이고 x ≡ 9 (mod 10) 인 첫 수 = **2863311539**. `demo_exhaustive` 의 실측 첫 반례가 정확히 이 값이고, 틀린 개수는 143,165,576. p = 34 에서 곱수를 더 키우면 오차만 커지고, 더 줄이면(⌈⌉ 미만) x = 10 이 0 이 되니 p = 34 로는 불가능하다. 내림 곱수 0xCCCCCCCC 는 ε 가 음수라 r = 0 에서 바로 틀린다(첫 반례 x = 10, 429,496,729 개 틀림 — 10 의 배수 전부).

### Part B — 임의의 d: 매직 넘버 계산

**정리.** N = 32, d ≥ 1, p ≥ 32, m = ⌈2^p/d⌉, e = m·d − 2^p (0 ≤ e < d). **e ≤ 2^(p−32)** 이면 모든 0 ≤ x < 2^32 에서 ⌊x·m/2^p⌋ = ⌊x/d⌋.

증명: x = qd + r. x·m/2^p = q + r/d + x·e/(d·2^p). 마지막 항은 0 이상이고 x·e/(d·2^p) < 2^32·2^(p−32)/(d·2^p) = 1/d. r ≤ d − 1 이므로 r/d + (그 항) < (d − 1)/d + 1/d = 1. ∎

존재: p = 32 + ⌈log₂ d⌉ 이면 e < d ≤ 2^⌈log₂ d⌉ = 2^(p−32). 그래서 루프는 p ≤ 64 에서 반드시 멈춘다. `magic_u32` 는 p = 32 부터 올려 **가장 작은 p** 를 고른다. ⌈2^p/d⌉ 를 128비트 없이 구하려고 A = 2^p − 1 (p = 64 면 UINT64_MAX) 을 써서 m = A/d + 1, e = d − 1 − A mod d (d 가 2^p 를 못 나눌 때 — 2의 거듭제곱은 앞에서 시프트로 처리).

| d | p=32 (m, e, 한계) | p=33 | p=34 | p=35 | 결과 |
|---|---|---|---|---|---|
| 10 | 0x1999999A, 4 > 1 | 0x33333334, 8 > 2 | 0x66666667, 6 > 4 | 0xCCCCCCCD, 2 ≤ 8 ✔ | mode 1, >> 35 |
| 7 | 0x24924925, 3 > 1 | 0x4924924A, 6 > 2 | 0x92492493, 5 > 4 | 0x124924925, 3 ≤ 8 ✔ | m ≥ 2^32 → mode 2 |
| 641 | 6700417, 1 ≤ 1 ✔ | | | | mode 1, >> 32 |

641 은 재밌는 경우다: 641 · 6700417 = 2^32 + 1 (오일러가 F₅ 를 인수분해한 식)이라 p = 32 에서 e = 1. 그래서 x/641 = (x·6700417) >> 32, 상위 워드만 취하면 끝.

**mode 2 (33비트 곱수, d = 7).** m = 2^32 + m', m' = 0x24924925. ⌊x·m/2^p⌋ = ⌊(x + x·m'/2^32)/2^(p−32)⌋. t = ⌊x·m'/2^32⌋ 로 바꿔도 같다 (정수 a 와 0 ≤ f < 1 에 대해 ⌊(a + f)/2^k⌋ = ⌊a/2^k⌋). x + t 는 2^33 까지 갈 수 있어 32비트에서 넘친다. 그래서 t ≤ x 를 이용해 ⌊(x + t)/2⌋ = ⌊(x − t)/2⌋ + t 로 계산하고 남은 시프트 p − 33 을 한다:

```c
uint32_t t = (uint32_t)(((uint64_t)x * g.mul) >> 32);
return (((x - t) >> 1) + t) >> g.shift;      /* shift = p - 33, d = 7 이면 2 */
```

m < 2^33 은 p ≤ 32 + ⌈log₂ d⌉ 와 2^⌈log₂ d⌉ < 2d 에서 나온다. 테스트: 6000개 d (1..2000 전부, 2^k ± 1, 크기가 고르게 퍼진 랜덤) × 경계 x 11개 + 몫 경계 40개 + 랜덤 150개 → 0개 틀림. 그중 mode 2 가 1837개 — 생각보다 흔하다. d = 3, 7, 1000, 641 은 2^32 전수도 0개(`demo_exhaustive`).

### Part B — 128비트 없는 mulmod64

m 이 2^32 를 넘으면 a·b 가 uint64 를 넘는다. 두 배-더하기(곱셈을 덧셈으로 — powmod 의 덧셈판): b 의 비트마다 a 를 두 배로, 비트가 1 이면 r 에 더한다. 덧셈 r + a 도 m ≥ 2^63 이면 넘칠 수 있으니 **빼기로 비교**한다: r, a < m 일 때 `r >= m − a ? r − (m − a) : r + a` 는 넘침 없이 (r + a) mod m. 64번 반복, 128비트 곱 하나보다 수십 배 느리지만 어떤 64비트 CPU·MCU 에서도 돈다. 테스트는 `unsigned __int128` (테스트 쪽만)과 20만 번, m 을 UINT64_MAX 근처·작은 값으로도. powmod64 로 a^(p−1) ≡ 1 (p = 2^64 − 59, 2^61 − 1 — 둘 다 소수임을 이 레슨 작업 중 Miller–Rabin 으로 확인) 300번.

### Part C — 가속도계 크기: 반올림 isqrt

int16 세 축 제곱합 최대 = 3·32768² = 3·2^30 = 3,221,225,472 < 2^32 ✔ (네 축이었으면 2^32 = 넘침). 각 제곱은 int32 에 들어간다(2^30 < 2^31). 반올림: r = ⌊√s⌋ 일 때 √s ≥ r + ½ ⇔ s ≥ r² + r + ¼ ⇔ (정수라서) **s − r² > r**. 그리고 (r + ½)² 는 정수가 아니니 동률(tie)은 없다. s − r² 은 isqrt 의 `rem` 그대로다. 최악 입력 √(3·2^30) = 56755.84 → 56756. 자유 낙하 검출(|a| < 0.3 g 가 일정 시간)이나 충격 검출(|a| > 임계)에 쓰는 값인데, 사실 **비교만 할 거면 제곱한 임계와 s 를 비교**하는 게 더 싸다(Q&A 6). 테스트 기준: round(√s) = r ⇔ (2r − 1)² ≤ 4s < (2r + 1)², uint64 로.

### Part C — printf 없는 10진 출력

`mod10` 으로 끝자리부터 뽑고 `div10` 으로 줄인 뒤 뒤집어 복사한다. `do { } while (x)` 라서 0 도 "0" 이 된다. uint32 최대 10자리 + NUL = 11바이트. M0 에서 `x % 10`, `x / 10` 을 그대로 쓰면 자리마다 `__aeabi_uidivmod` 호출이 생긴다(아래 asm). 다만 M0 에는 UMULL 도 없어서 div10 의 64비트 곱 자체가 `__aeabi_lmul` 이 된다 — M0 에선 연습 3 의 **곱셈 없는 divu10** 이 진짜 답이다. 테스트: 30만 개(모든 자릿수)를 `snprintf("%u")` 와 비교.

### Part C — 주파수 호핑 PRNG 점프

Park–Miller "minimal standard" 는 x_{n+1} = 16807·x_n mod (2^31 − 1). 곱셈만 있는 선형 합동(multiplicative LCG)이라 x_k = 16807^k · x₀ mod M — **powmod 한 번**이면 슬롯 k 의 상태가 나온다. 슬립에서 깨어난 노드가 k 개의 슬롯을 하나씩 돌리지 않고 O(log k) 곱으로 맞춘다. 테스트: Park & Miller(1988)의 확인 값 "시드 1 에서 10000번째 = 1043618065" (이 레슨에서 직접 반복으로도 재현), 20만 단계 직접 반복과 대조, 점프 합성 jump(jump(x, a), b) = jump(x, a + b), 그리고 k = M − 1 이면 제자리(페르마). 덧셈 상수가 있는 LCG(x' = ax + c)는 2×2 행렬 거듭제곱 또는 등비급수 합이 필요하다 — 연습으로 남긴다.

### Part C — 클럭 비율과 LCM

48 kHz 와 44.1 kHz 를 한 PLL 에서 만들려면 비율 48000/44100 을 기약분수로: gcd = 300 → **160/147**. 두 클럭이 정수 분주로 나오는 공통 클럭은 lcm = 48000/300 · 44100 = **7,056,000 Hz** (÷147 = 48 kHz, ÷160 = 44.1 kHz). LCM 은 `a / gcd · b` 순서로 계산해 중간값을 줄이고 uint64 로 넘침을 검사한다(넘치면 0 = 오류). 태스크 주기 6 ms 와 10 ms 의 하이퍼주기(hyper-period, 스케줄 표 길이) 30 ms 도 같은 계산이다.

### 컴파일러는 실제로 뭘 내나 — Cortex-M4 / M0 / arm64

`demo_target.c` 를 이 맥의 clang 으로 (지시어 줄은 걸러냄, 실제 출력):

```text
@ clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -
div10_c:                      @ x / 10
    movw   r1, #52429         @ 0xCCCD
    movt   r1, #52428         @ 0xCCCC → r1 = 0xCCCCCCCD
    umull  r0, r1, r0, r1     @ 64비트 곱, 상위 워드 r1
    lsrs   r0, r1, #3         @ >> 32 + 3 = >> 35
    bx     lr
div7_c:                       @ x / 7 : mode 2 그대로
    movw   r1, #18725
    movt   r1, #9362          @ 0x24924925
    umull  r1, r2, r0, r1     @ t = 상위 워드
    subs   r0, r0, r2         @ x − t
    add.w  r0, r2, r0, lsr #1 @ t + (x − t) >> 1
    lsrs   r0, r0, #2         @ >> (35 − 33)
    bx     lr
div_var:                      @ a / b (변수)
    udiv   r0, r0, r1
```

```text
@ clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2 -S demo_target.c -o -
div10_c:
    push   {r7, lr}
    movs   r1, #10
    bl     __aeabi_uidiv      @ 상수 ÷10 도 라이브러리 호출
    pop    {r7, pc}
div10_magic:                  @ 손으로 쓴 (uint64_t)x * 0xCCCCCCCD >> 35
    push   {r7, lr}
    ldr    r2, .LCPI5_0       @ .long 3435973837 (0xcccccccd)
    movs   r1, #0
    mov    r3, r1
    bl     __aeabi_lmul       @ M0 에는 UMULL 이 없다 → 64×64 곱 함수
    lsrs   r0, r1, #3
    pop    {r7, pc}
```

M0 의 `divu10`(연습 3)는 함수 호출 없이 `lsrs/adds` 10개 + `muls` 하나(r = n − 10q 를 −10 곱으로) + 비교·분기다. arm64(이 맥)는 M4 와 같은 상수로 `umull x8, w0, w8` + `lsr x0, x8, #35`, ÷7 도 같은 mode 2 순서, 부호 있는 ÷10 은 `smull` + `asr #34` + 부호 비트 더하기(연습 4 와 같은 상수 0x66666667). **컴파일러가 이 레슨의 `magic_u32` 와 같은 (곱수, 시프트)를 골랐다.** Cortex-M3/M4 의 `UDIV` 는 피연산자에 따라 2~12 사이클로 알려져 있다(ARM TRM — 확인 권장), 곱+시프트는 몇 사이클로 고정이다.

### 전수 검사 — `demo_exhaustive.c` (2^32 개 전부)

8 스레드, -O2, 이 맥(Apple M6, Apple clang 21). 실제 출력:

```text
all 2^32 inputs, 8 threads, -O2
test                                       mismatches  first bad x      sec
div10 0xCCCCCCCD>>35 + mod10                        0            -     0.96
div10 0x66666667>>34 (one shift less)       143165576   2863311539     0.74
div10 0xCCCCCCCC>>35 (floor mult)           429496729           10     0.74
divu10 shift-add (no mul)                           0            -     0.91
magic_u32 d=3,7,1000,641                            0            -     2.91
isqrt32 bitwise vs r^2<=x<(r+1)^2                   0            -    15.64
(uint32_t)sqrt((double)x)                           0            -    14.57
(uint32_t)sqrtf((float)x)                     6986365     16785407    12.11
newton isqrt (== isqrt32, max divisions)            0            -    16.12   max divisions = 6
total 64.7 s, all exact methods exact
```

- double `sqrt` 는 32비트 입력에서 항상 맞다: √(k² − 1) ≈ k − 1/(2k) 이고 상대 간격 1/(2k²) ≥ 1.2·10^−10 ≫ 2^−53 이라 k 로 반올림될 수 없다.
- **float `sqrtf` 는 2^24 위에서 틀린다.** 첫 반례 16785407 = 4097² − 2: float 로 바꾸면 16785408 (2^24 위 float 간격 = 2), 그 제곱근 4096.99988 이 float 간격(2^−11 ≈ 0.00049) 안에서 4097.0 으로 반올림 → floor 가 4097 (정답 4096). Cortex-M4F 의 FPU 는 단정밀도뿐이라 이것이 현실 문제다.
- 시간 열의 isqrt·sqrt 줄에는 기준(isqrt32 또는 정의 검사) 계산 시간이 같이 들어 있다.

### 이 맥의 처리량 — `demo_bench.c`

입력 2^20 개 배열을 훑는 시간, 7번 중 중앙값(워밍업 1번). **측정 당시 다른 작업으로 load average ≈ 48 이라 흔들렸다** — 네 번 돌린 범위를 같이 적는다. 절대값보다 순서를 볼 것.

| 연산 | 마지막 실행 ns/op | 4회 범위 |
|---|---|---|
| `x / 10` (컴파일러: umull + lsr) | 0.08 | 0.08–0.13 |
| `x / d` (변수: udiv) | 0.48 | 0.46–0.56 |
| `div10` (매직) | 0.08 | 0.08–0.12 |
| `divu10` (시프트·덧셈) | 0.23 | 0.19–0.23 |
| isqrt32 비트 단위 | 13.22 | 10.5–32.3 |
| isqrt 뉴턴 (x / r) | 8.20 | 7.8–25.9 |
| isqrt 이분 탐색 (ALGO-B02) | 19.61 | 18.4–64.8 |
| `(uint32_t)sqrt((double)x)` | 0.25 | 0.23–0.57 |
| gcd Euclid | 61.20 | 58–167 |
| gcd Stein (비트 루프) | 133.56 | 112–318 |
| gcd Stein (`__builtin_ctz`) | 38.41 | 35–87 |
| powmod32 (m 변수) | 150.41 | 138–256 |

읽는 법: (1) 상수 나눗셈은 컴파일러가 이미 매직 곱으로 바꾼다 — 손으로 쓴 div10 과 같은 코드. 변수 나눗셈 udiv 는 여기서 4~6배. (2) **이 맥에서는** double sqrt 가 정수 isqrt 보다 수십 배 빠르다 — 하드웨어 FPU 가 있으면 FPU 를 써라. 정수 isqrt 는 FPU 가 없거나(M0/M3) float 로 정확도가 모자랄 때(M4F) 쓰는 것이다. 랜덤 입력에선 비트 단위 isqrt 의 분기 예측 실패가 크다. (3) 나눗셈이 빠른 CPU 에서는 Euclid 가 비트를 하나씩 미는 Stein 보다 빠르다. ctz 로 0 비트를 한 번에 밀면 Stein 이 이긴다. **M0 에선 반대로** Euclid 의 `%` 가 단계마다 `__aeabi_uidivmod` 호출이고, CTZ 명령도 없어서 clang 이 `__builtin_ctz` 를 de Bruijn 곱 + 32바이트 표 조회로 바꾼다(이 레슨에서 asm 확인; ALGO-B12 참고). M0 실측은 없으니 사이클 수는 말하지 않는다.

### 복잡도

| 함수 | 시간 | 추가 메모리 | 메모 |
|---|---|---|---|
| isqrt32 | 16단계 고정 (앞 0 건너뛰면 더 적음) | O(1) | 곱셈·나눗셈 0, 최악 = 평균 → ISR 에서도 결정적 |
| powmod32 | ≤ 2·32 곱 + 64비트 `%` | O(1) | M0 에선 64비트 `%` 가 `__aeabi_uldivmod` |
| gcd_euclid | O(log min(a,b)), uint32 최악 45단계 | O(1) | 피보나치 최악 |
| gcd_binary | ≤ 64 뺄셈 + ≤ 64 시프트 | O(1) | 나눗셈 0 |
| div10 / mod10 / magic_apply | O(1) | O(1) | 64비트 곱 1 (M0 에선 lmul) |
| magic_u32 | ≤ 33 반복, 64비트 나눗셈 | O(1) | 컴파일 시점·초기화 때 한 번 |
| mulmod64 / powmod64 | 64 / 64·2·64 덧셈-비교 | O(1) | 128비트 곱이 있으면 그게 훨씬 빠름 |

재귀 없음, 스택은 지역 변수 몇 개(u32_to_dec 만 10바이트 임시 버퍼).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 67/67 passed, 약 0.3 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_exhaustive.c -o demo_exhaustive -lm && ./demo_exhaustive   # 약 65 초 (8 스레드)
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench -lm && ./demo_bench                 # 약 10 초
clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2 -S demo_target.c -o -                  # asm 보기
```

## 7. 자주 하는 실수

**1. 내림 곱수.** `m = 2^35 / 10` (= 0xCCCCCCCC, 내림)로 하면 오차가 음수라 10 의 배수에서 몫이 하나 작다. x = 10 → 0. 2^32 전수에서 429,496,729개(10 의 배수 전부) 틀림. 고친 코드: **올림** m = ⌈2^p/d⌉.

**2. 시프트를 하나 아낀다.** 0x66666667 >> 34 는 그럴듯하지만 x ≥ 2863311539 의 일부에서 틀린다 (143,165,576개 = 전체의 3.3%). 큰 x 를 고르게 뽑는 랜덤 테스트면 잡히지만, "x < 2^24 전수 + 작은 값 몇 개" 같은 테스트는 첫 반례가 2.86·10^9 이라 하나도 안 걸린다 — 이런 코드는 **증명 또는 전수**로 믿어야 한다.

**3. powmod 에서 32비트 곱.** `uint32_t r; r = r * b % m;` 은 곱이 넘친 뒤 나머지를 구한다. m < 2^16 일 때만 맞는다. 고친 코드: `uint64_t` 로 올려서 곱하기. 또 `r = 1` 로 시작하면 m = 1, e = 0 에서 1 이 나온다(정답 0) → `r = 1 % m`.

**4. isqrt 에서 `(r+1)*(r+1) <= x` 를 uint32 로.** r = 65535 이면 (r+1)² = 2^32 → 0 으로 넘쳐서 무한 루프나 오답. 이분 탐색 isqrt(ALGO-B02 스타일)도 `mid * mid` 를 uint64 로 하거나 `mid <= x / mid` 로 비교해야 한다. 비트 단위 방법은 곱이 없어 이 문제가 없다.

**5. 가속도 제곱합을 int32 로.** (−32768)² · 3 = 3,221,225,472 > INT32_MAX → **부호 있는 넘침은 UB**. 각 제곱은 int32 에 들어가지만 합은 uint32 로 더해야 한다. 4축(예: 쿼터니언 노름)이면 uint32 도 넘친다 → uint64.

**6. Stein 에서 0 처리 누락.** gcd(0, b) 를 따로 안 돌려주면 `while ((a & 1) == 0) a >>= 1;` 이 a = 0 에서 끝나지 않는다(무한 루프). 공통 2^k 루프도 둘 다 0 이면 영원히 돈다. 첫 두 줄 `if (a == 0) return b; if (b == 0) return a;` 가 필수.

**7. mulmod64 의 덧셈 넘침.** `r = (r + a) % m` 은 m > 2^63 이면 r + a 가 2^64 를 넘는다. m = UINT64_MAX 근처에서만 터져서 작은 테스트로는 안 보인다. 고친 코드: `r >= m - a ? r - (m - a) : r + a`.

**8. 부호 있는 ÷10 에 부호 없는 매직.** `(int32_t)(((uint64_t)(uint32_t)x * 0xCCCCCCCD) >> 35)` 는 음수에서 엉터리. C 의 `/` 는 0 쪽으로 버림이라 floor 결과에 음수일 때 +1 보정이 필요하다 (연습 4).

## 8. 면접 Q&A

<details><summary>Q1. x / 10 을 곱셈과 시프트로 바꾸는 상수를 유도해 보세요. 왜 하필 35 인가요?</summary>

m = ⌈2^p/10⌉ 로 두면 x·m/2^p = x/10 + x·e/(10·2^p), e = 10m − 2^p. x = 10q + r 에서 r ≤ 9 이니 오차 항이 1/10 미만이면 floor 가 q. 2^32 범위 전부에서 보장하는 충분조건이 e ≤ 2^(p−32). p = 32, 33, 34 는 e = 4, 8, 6 으로 실패, p = 35 에서 m = 0xCCCCCCCD, e = 2 ≤ 8 로 성립. p = 34 는 충분조건만 실패한 게 아니라 실제로 x = 2863311539 에서 틀린다(전수 검사). 컴파일러(arm64·Cortex-M4)도 정확히 이 상수와 `>> 35` 를 낸다.

</details>

<details><summary>Q2. 상수가 32비트에 안 들어가는 d = 7 은 어떻게 하나요? 꼬리: 짝수 d 라면?</summary>

d = 7 은 p = 35 에서 m = 0x124924925 (33비트). m = 2^32 + m' 로 쪼개 t = mulhi(x, m') 을 구하고 (x + t) >> 3 을 해야 하는데 x + t 가 넘칠 수 있어서 `((x − t) >> 1) + t` 후 `>> 2`. t ≤ x 이므로 x − t 는 음수가 안 된다. Cortex-M4 asm 이 정확히 `umull / subs / add lsr #1 / lsrs #2` 다. 짝수 d = 2^s·d' 는 x 를 먼저 >> s 하면 입력이 32 − s 비트가 되어 조건 e ≤ 2^(p−(32−s)) 가 쉬워진다. 실제로 이 맥의 clang 은 `x / 14` 를 `lsr #1` → 0x92492493(= ⌈2^34/7⌉, e = 5 ≤ 2^(34−31)) 곱 → `>> 34` 로 냈다 (arm64·Cortex-M4 asm 확인) — add 보정이 사라진다.

</details>

<details><summary>Q3. Cortex-M0 에서 x / 10 과 x % 10 은 어떻게 되나요? 더 빠르게 하려면?</summary>

M0 에는 나눗셈 명령이 없어 clang 이 `movs r1, #10; bl __aeabi_uidiv` (나머지는 `__aeabi_uidivmod`)를 낸다 — 상수여도 그렇다. 매직 곱으로 바꾸고 싶어도 M0 에는 32×32→64 곱(UMULL)이 없어서 `(uint64_t)x * 0xCCCCCCCD` 가 `bl __aeabi_lmul` 이 된다(asm 확인). 답은 곱셈 없는 divu10: q ≈ 0.8x 를 시프트·덧셈 4단으로 만들고 `>> 3`, r = x − 10q 로 한 번 보정 (연습 3, 2^32 전수 0개 틀림). 로그 출력처럼 자릿수 전부가 필요하면 자리마다 이걸 반복하거나, 10^k 를 빼는 방식도 있다. 그리고 애초에 로그를 16진으로 찍으면 나눗셈이 필요 없다.

</details>

<details><summary>Q4. 비트 단위 isqrt 가 맞다는 걸 증명해 보세요. 뉴턴법과 비교하면?</summary>

불변식: one = 4^j 일 때 res = P·2^(j+1), rem = x − P², P 는 ⌊√x⌋ 의 j+1 번 이상 비트. 시험 `rem ≥ res + one` 은 (P + 2^j)² ≤ x 와 같고, 이는 ⌊√x⌋ 의 j 번 비트가 1 인 것과 같다. 갱신식이 불변식을 유지하고 j = −1 에서 res = P = ⌊√x⌋. 곱·나눗셈이 없고 반복 횟수가 고정이라 **최악 시간이 결정적** → ISR·하드 실시간에 좋다. 뉴턴(r ← (r + x/r)/2)은 위에서 출발하면 단조 감소하고 ⌊√x⌋ 에서 멈추며(AM–GM), 2^32 전수에서 최대 6번 나눗셈 — UDIV 가 있는 M3/M4 에선 빠르지만 M0 에선 나눗셈 6번이 라이브러리 호출 6번이다. 역수 제곱근 뉴턴은 DSP-B12.

</details>

<details><summary>Q5. Euclid 는 언제 가장 느린가요? Stein 은 왜 존재하나요?</summary>

Lamé 정리: 단계 수 최악은 연속 피보나치 수. uint32 안에서는 (F₄₇, F₄₆) 가 45단계(실측). 단계 수 ≤ log_φ(min) + O(1) 이라 O(log n). Stein 은 나눗셈 대신 시프트·뺄셈만 써서 **나눗셈이 비싼 하드웨어**(M0, 옛 CPU, 큰 정수 bignum)에서 이긴다. 각 바퀴마다 a·b 가 절반 이하라 ≤ 64 바퀴. 이 맥처럼 나눗셈이 빠른 곳에선 Euclid 가 비트 루프 Stein 보다 빠르고, ctz 로 0 비트를 한 번에 밀면 Stein 이 다시 이긴다(실측 61 / 134 / 38 ns). 즉 "어떤 GCD 가 빠른가"는 타깃의 나눗셈·CTZ 비용으로 답이 바뀐다.

</details>

<details><summary>Q6. 가속도 크기가 임계값을 넘는지만 보면 될 때도 sqrt 를 해야 하나요?</summary>

아니다. |a| > T ⇔ x² + y² + z² > T² (양수끼리 제곱은 단조). T² 를 컴파일 시점 상수로 두면 sqrt 없이 곱 3개 + 비교 1개다. sqrt 는 값 자체를 로그·필터·표시해야 할 때만. 넘침만 조심: int16 축 세 개면 uint32 로 충분, T² 도 uint32. 히스테리시스(올라갈 때 T_hi, 내려갈 때 T_lo)도 같은 방식. 근사 크기(alpha-max-beta-min, CORDIC 벡터링 모드)는 DSP-M09.

</details>

<details><summary>Q7. 64비트 법에서 a·b mod m 은? 꼬리: 더 빠르게?</summary>

128비트 곱(`unsigned __int128`, arm64 의 `umulh`)이 있으면 곱 한 번 + 128÷64 나머지. 없으면 두 배-더하기: b 의 비트마다 a = 2a mod m, 비트가 1 이면 r = r + a mod m, 덧셈은 `r >= m − a` 비교로 넘침 없이. 64번 반복. 더 빠른 방법은 Montgomery 곱셈(나머지를 2^64 로의 시프트로 바꿈, m 홀수) — 반복 거듭제곱(소수 판정, RSA)에서 쓴다. 여기 연습 6 의 "2^32 위의 역원"이 Montgomery 의 재료다.

</details>

<details><summary>Q8. 가벼운 PRNG 를 무선 노드 간 주파수 호핑에 쓸 때 거듭제곱이 왜 필요한가요?</summary>

두 노드가 같은 시드와 슬롯 번호만 알면 같은 채널을 고른다. 노드가 오래 자다 깨면 지난 슬롯 수 k 만큼 PRNG 를 돌려야 하는데, 곱셈 LCG 는 x_k = a^k·x₀ mod M 이라 powmod 한 번(O(log k), 31비트 지수면 곱 ~62번)으로 끝난다. 슬롯마다 깨어 돌리는 것보다 전력이 훨씬 적다. 단, Park–Miller 는 통계 품질이 약하고 보안이 없다 — 실제 규격(예: BLE 의 채널 선택 알고리즘)은 자체 정의를 쓰니 그 규격을 따라야 한다. 덧셈 상수가 있는 LCG 는 (a, c) 쌍의 합성을 제곱-곱하기로 하면 같은 O(log k).

</details>

## 9. 추가 연습

### 연습 1 — isqrt64

`uint32_t isqrt64(uint64_t x)`. UINT64_MAX → 4294967295. 무엇이 바뀌나?

<details><summary>답 보기</summary>

`one = 1ull << 62`, rem·res 를 uint64 로. 불변식·증명은 같다. res ≤ 2^33, one ≤ 2^62 라 넘침 없음. 검증 기준은 r² ≤ x < (r+1)² 인데 r = 2^32 − 1 이면 (r+1)² = 2^64 → **`unsigned __int128`** 로 비교(테스트 쪽만). (2^32 − 1)², 그 −1, 완전제곱 근처를 포함한 랜덤 10^6 개 확인. LeetCode 69 Sqrt(x) 의 64비트판이다.

</details>

### 연습 2 — 뉴턴 정수 제곱근

r₀ = 2^⌈bits/2⌉ 에서 시작해 y = (r + x/r) >> 1 을 y ≥ r 일 때까지. 왜 멈춘 r 이 ⌊√x⌋ 인가? 나눗셈은 최대 몇 번?

<details><summary>답 보기</summary>

```c
uint32_t r = 1u << ((bits + 1) / 2);
for (;;) { uint32_t y = (r + x / r) >> 1; if (y >= r) return r; r = y; }
```

S = ⌊√x⌋. (1) r ≥ S 이면 y ≥ S: ⌊(r + ⌊x/r⌋)/2⌋ = ⌊(r + x/r)/2⌋ (정수 r 을 더한 뒤의 floor 중첩) ≥ ⌊√x⌋ (AM–GM). (2) r > S 이면 r² > x 라 x/r < r → y < r (진짜 감소). (3) r = S 이면 x/r ≥ S 라 y ≥ S = r → 멈춤. 출발값 2^⌈bits/2⌉ > √x 이고 감소하며 S 아래로 안 내려가니 S 에서 멈춘다. 측정: x < 2^20 전수 + 랜덤에서 최대 6번, 랜덤 32비트 평균 3.98번. 2^32 전수에서도 최대 6(`demo_exhaustive`).

</details>

### 연습 3 — 곱셈 없는 divu10 (Cortex-M0)

UMULL 도 UDIV 도 없는 M0 용 ÷10. 시프트와 덧셈만으로.

<details><summary>답 보기</summary>

```c
uint32_t q = (n >> 1) + (n >> 2);          /* 0.75 n */
q += q >> 4;  q += q >> 8;  q += q >> 16;  /* × (1 + 2^-4)(1 + 2^-8)(1 + 2^-16) ≈ 0.8 n */
q >>= 3;                                   /* ≈ n / 10, 조금 작을 수 있음 */
uint32_t r = n - (((q << 2) + q) << 1);    /* n − 10q */
return q + (r > 9);
```

0.8 = 0.75 · 16/15 이고 16/15 = 1/(1 − 2^−4) = (1 + 2^−4)(1 + 2^−8)(1 + 2^−16)(1 + 2^−32)… 의 앞 세 항. 잘린 항과 시프트의 버림 때문에 q 는 정답보다 작을 수 있고, 그 차이가 1 이하라 r > 9 한 번 보정으로 맞는다는 것을 **2^32 전수로 확인**했다(0개 틀림). Hacker's Delight 10장에 같은 계열이 있다. M0 asm: `lsrs/adds` 반복 + `muls` 1개(10q 를 −10 곱으로) + 비교 — 함수 호출 없음. 이 맥에선 0.19–0.23 ns/op (매직 0.08).

</details>

### 연습 4 — 부호 있는 int32 ÷ 10

C 의 `/` 처럼 0 쪽으로 버림. INT32_MIN 포함.

<details><summary>답 보기</summary>

```c
int32_t q = (int32_t)(((int64_t)x * 0x66666667) >> 34);   /* floor(x · m / 2^34) */
return q + (int32_t)((uint32_t)x >> 31);                  /* 음수면 +1 */
```

|x| < 2^31 이라 입력이 31비트: m = ⌈2^34/10⌉ = 0x66666667, e = 6 ≤ 2^(34−31) = 8. 양수는 Part B 정리. 음수 x 는 x·m/2^34 = x/10 − δ, 0 < δ ≤ 6·2^31/(10·2^34) = 0.075 < 0.1. x/10 이 정수 k 면 floor = k − 1, +1 → k. 아니면 소수 부분 ≥ 0.1 이라 floor = ⌊x/10⌋, +1 = ⌈x/10⌉ = 0 쪽 버림. 음수의 `>>` 는 C 에서 **구현 정의**(clang·gcc 는 산술 시프트). 컴파일러는 x 대신 q 의 부호 비트를 더한다(`add w0, w8, w8, lsr #31`) — x < 0 ⇔ q < 0 이라 같다. 확인: |x| < 2^21 전수 + 랜덤 2·10^6 + 양 끝 1000개씩.

</details>

### 연습 5 — 모듈러 역원

`modinv32(a, m)`: a·inv ≡ 1 (mod m), 없으면 0. 소수 m 에서는 페르마 a^(m−2) 와 같아야 한다.

<details><summary>답 보기</summary>

확장 유클리드로 r 만 아니라 계수 t 도 따라간다. 불변식 t_i · a ≡ r_i (mod m) (r₀ = m, t₀ = 0; r₁ = a mod m, t₁ = 1 로 시작하면 성립하고 같은 선형 결합으로 갱신되니 유지). 끝에 r = gcd. gcd ≠ 1 이면 역원 없음, 아니면 t₀ (음수면 + m). |t| ≤ m 이라 int64 로 충분. 확인: 랜덤 20만 개(79,099개는 역원 없음 → 0 반환 확인) + 소수 2^32 − 5 에서 2만 번 powmod(a, p−2, p) 와 일치. 페르마 방식은 곱 ~64번, 확장 유클리드는 나눗셈 ~20번 — 타깃에 따라 고른다.

</details>

### 연습 6 — 나머지 없이 "d 로 나누어떨어지나" (홀수 d)

`x % d == 0` 을 곱셈 하나와 비교 하나로.

<details><summary>답 보기</summary>

d 가 홀수면 2^32 위에서 역원 d⁻¹ 이 있다. 뉴턴: inv = d 로 시작(d·d ≡ 1 mod 8, 3비트 정확), `inv *= 2 − d·inv` 를 4번 → 6, 12, 24, 48 비트. 그러면 **x 는 d 의 배수 ⇔ x·d⁻¹ mod 2^32 ≤ ⌊(2^32 − 1)/d⌋**. 증명: x ↦ x·d⁻¹ 은 Z/2^32 위의 전단사이고, 배수 k·d (k = 0..⌊MAX/d⌋) 는 정확히 k 로 간다. 그 상(image)이 [0, ⌊MAX/d⌋] 를 다 채우므로, 배수가 아닌 x 는 전단사라서 그 구간 밖으로 간다. inv(3) = 0xAAAAAAAB (3 · 0xAAAAAAAB = 2^33 + 1). 짝수 d = 2^s·d' 는 회전(rotate right s)을 섞는다 — Granlund–Montgomery / Lemire 의 방법. 확인: 홀수 d < 64 × x < 2^20 전수 + 랜덤 d 2만 개 × (배수, 배수 ± 1, 랜덤, 끝).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 19/19 passed, 약 0.2 초
```

## 10. 핵심 정리

- isqrt: 근의 비트를 위에서부터, 시험값 = (P+2^j)² − P² = res + one. 불변식 res = P·2^(j+1), rem = x − P². 곱·나눗셈 0, 16단계 고정. float `sqrtf` 는 2^24 위에서 틀린다(실측 6,986,365개).
- powmod: r·b^e 불변, 곱은 uint64, `r = 1 % m`. 64비트 법은 두 배-더하기 + `r >= m − a` 덧셈.
- GCD: Euclid 최악 = 연속 피보나치(uint32 에서 45단계). Stein 은 시프트·뺄셈만, 매 바퀴 a·b 절반 → ≤ 64. 빠른 쪽은 타깃의 나눗셈·CTZ 비용이 정한다.
- 상수 나눗셈: m = ⌈2^p/d⌉, e = md − 2^p, **e ≤ 2^(p−32) 이면 정확**. ÷10 = 0xCCCCCCCD >> 35 (34 는 x = 2863311539 에서 틀림). m 이 33비트면 `((x − t) >> 1) + t`.
- 컴파일러: M4/arm64 는 상수 ÷ 를 umull+시프트로 바꾼다(이 레슨의 상수와 같음). M0 는 `__aeabi_uidiv`, 손 매직도 `__aeabi_lmul` → 시프트·덧셈 divu10.
- 반올림 isqrt: s − r² > r 이면 +1. 임계값 비교만이면 sqrt 대신 제곱끼리 비교.
- 증명이 있는 코드도 **2^32 전수**로 확인할 수 있으면 확인한다 — 1분이면 끝난다.

## 참고자료

- H. S. Warren Jr., *Hacker's Delight*, 2nd ed., Addison-Wesley 2012 — 10장 상수 나눗셈(부호 없는 매직, add 보정, divu10 계열, 나누어떨어짐 검사), 11장 정수 제곱근.
- T. Granlund, P. L. Montgomery, "Division by Invariant Integers using Multiplication", PLDI 1994 — 이 레슨의 정리(e ≤ 2^(p−N))와 같은 계열의 원 논문.
- D. Lemire, O. Kaser, N. Kurz, "Faster Remainder by Direct Computation", *Software: Practice and Experience* 2019 — 곱셈으로 바로 나머지·나누어떨어짐 (연습 6 의 확장).
- D. E. Knuth, *TAOCP* Vol. 2, §4.5.2 (GCD: Euclid, Lamé 정리, 이진 GCD), §4.6.3 (거듭제곱).
- J. Stein, "Computational problems associated with Racah algebra", *J. Comput. Phys.* 1(3), 1967 — 이진 GCD. 서지 세부는 기억에 의존 — 확인 권장.
- S. K. Park, K. W. Miller, "Random Number Generators: Good Ones Are Hard to Find", *CACM* 31(10), 1988 — minimal standard (16807, 2^31 − 1), 시드 1 의 10000번째 값 1043618065 (이 레슨에서 반복으로 재현).
- ARM, *ARMv6-M Architecture Reference Manual* (나눗셈·UMULL·CLZ 없음), *Cortex-M4 Technical Reference Manual* (UDIV 2~12 사이클 — 확인 권장), *Run-time ABI for the Arm Architecture* (`__aeabi_uidiv`, `__aeabi_lmul`). 이 레슨의 asm 은 Apple clang 21 의 `--target=thumbv6m/thumbv7em` 실제 출력.
- LeetCode 69 Sqrt(x), LeetCode 50 Pow(x, n), LeetCode 29 Divide Two Integers — 같은 아이디어의 면접판.
- AlgoMonster 노트 M02 §6 Square Root Estimation, M01 §6 Math Basics (Don 정리본) — isqrt 를 이분 탐색으로 푸는 판, 수학 기초.
- 관련 레슨: ALGO-B02 (이분 탐색 불변식 — 이분 탐색 isqrt), ALGO-B12 (popcount·ctz·de Bruijn), ALGO-B14 (M0 의 `% 255` 와 접기), DSP-B12 (정수 sqrt·뉴턴 역수), DSP-M09 (CORDIC 크기), ARM-B07 (MADD·SMULL·UMULH·SDIV), C-B02 (정수 승격·넘침), C-A09 (Q15 고정소수점), NUM-B04 (뉴턴법 수렴).
