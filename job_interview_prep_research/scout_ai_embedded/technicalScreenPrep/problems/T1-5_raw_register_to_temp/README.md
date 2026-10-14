# T1-5 · Raw Register to Temperature

> **ID**: T1-5 · **Priority**: P0 · **Pattern**: bit assembly + sign extension + fixed-point rounding · **Time**: 20 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T1-5` / `./run.sh T1-5 sol`

## 1. Problem

A TMP102-style I2C temperature sensor returns its temperature register as **two bytes, MSB first**. The temperature is a **12-bit two's complement** number stored **left-justified** in the 16-bit register (bits 15..4). Bits 3..0 are not part of the reading and may contain garbage. One LSB of the 12-bit value is **0.0625 °C**.

1. Convert the two bytes to temperature in **0.01 °C** units (`2500` means 25.00 °C). Round to the nearest integer; exact `.5` ties round **away from zero** (−6.25 → −6, 12.5 → 13, −12.5 → −13).
2. Decide whether the reading is an overheat: `true` when the converted value is `>= thresh_c100`.

- Integer math only (assume no FPU).
- Do not depend on implementation-defined behavior such as right-shifting a negative number.

```c
int16_t raw_to_c100(uint8_t msb, uint8_t lsb);
bool    is_overheat(uint8_t msb, uint8_t lsb, int16_t thresh_c100);
```

### Example

```
msb  lsb    register   12-bit   degC        raw_to_c100
0x19 0x00   0x1900       400     25.0           2500
0x7F 0xF0   0x7FF0      2047    127.9375       12794
0xFF 0xF0   0xFFF0        -1     -0.0625          -6
0xE7 0x00   0xE700      -400    -25.0          -2500
```

### Constraints

- Any `uint8_t` pair is valid input. Output range is −12800 … 12794, so `int16_t` is enough.
- Ignore bits 3..0 of `lsb` (mask them off).

### Clarifying questions to ask (first 3 minutes)

1. Is the data MSB first (big-endian) on the bus? Is the sensor in 12-bit mode, or 13-bit extended mode (EM bit)?
2. What output unit and rounding rule do you want — truncate, round-half-up, or round-half-away-from-zero?
3. Are the low 4 bits guaranteed to be zero, or should I mask them?
4. Is "overheat" `>=` or `>` the threshold, and is the threshold given in the same 0.01 °C unit?
5. Can I assume two's complement and a compiler that converts out-of-range unsigned to signed by wrapping? (Every real MCU compiler does; I'll note the portable form anyway.)

## 2. Your workspace

`./run.sh T1-5`로 바로 테스트하세요. 테스트 6묶음(13개 CHECK)이 들어 있습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 12비트 값을 꺼내는 가장 안전한 방법</summary>

16비트로 합친 다음 **`int16_t`로 바꾸고 16으로 나눕니다**. 12비트 값이 이미 왼쪽 정렬되어 있으므로 16비트 부호 비트(bit 15)가 곧 12비트 부호 비트(T11)입니다. 따로 부호 확장 코드를 쓸 필요가 없습니다.

</details>

<details>
<summary>Hint 2 — 왜 `>> 4` 가 아니라 `/ 16` 인가?</summary>

C에서 **음수의 오른쪽 시프트는 구현 정의(implementation-defined)**입니다(C11 6.5.7). 반면 `/`는 표준이 "0 방향 절삭"으로 정해 둡니다. 하위 4비트를 0으로 마스킹해 두면 값이 항상 16의 배수라 `/ 16`이 **나머지 없이 정확**합니다. 마스킹을 안 하면 `0xFFFF`(−1)/16 = 0 처럼 틀립니다.

</details>

<details>
<summary>Hint 3 — 0.0625 °C 를 0.01 °C 로</summary>

0.0625 °C = 6.25 c100 = **625 / 100**. 먼저 곱하고(`counts * 625`, 최대 ±1,280,000 → `int32_t`), 마지막에 한 번 나눕니다. "0에서 먼 쪽 반올림"은 양수면 `+50`, 음수면 `−50`을 더한 뒤 `/100`(0 방향 절삭)입니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 레지스터 비트 배치

<div class="chart" data-src="chart1.svg" data-caption="12비트 값이 bit 15..4에 왼쪽 정렬. bit 15(T11)의 가중치는 −128 °C. 0xE7,0x00 → −128+64+32+4+2+1 = −25 °C"></div>

12비트 2의 보수에서 최상위 비트 T11의 가중치는 **−2¹¹ × 0.0625 = −128 °C**입니다. 레지스터가 왼쪽 정렬이라 T11이 정확히 16비트의 부호 비트 자리에 있습니다. 그래서:

- `(int16_t)reg` → 값은 "12비트 값 × 16" (부호까지 맞음)
- `/ 16` → 12비트 값 (counts). 하위 4비트를 0으로 만들었으니 나눗셈이 정확

이 두 단계가 **부호 확장을 공짜로** 해 줍니다. 오른쪽 정렬된 12비트 값을 받았다면 `if (v & 0x800) v -= 0x1000;`로 직접 확장해야 합니다.

### 4.2 정수 변환 파이프라인 (음수 예)

<div class="chart" data-src="chart2.svg" data-caption="0xFF,0xF0 → −6. 오른쪽은 실제로 자주 나오는 오답 4가지"></div>

### 4.3 손으로 따라가기 (trace)

| 입력 | reg | (int16_t)reg | counts = /16 | num = ×625 | ±50 후 /100 | 결과 |
|---|---|---|---|---|---|---|
| 0x19,0x00 | 0x1900 | 6400 | 400 | 250000 | 250050/100 | **2500** |
| 0x7F,0xF0 | 0x7FF0 | 32752 | 2047 | 1279375 | 1279425/100 | **12794** |
| 0xFF,0xF0 | 0xFFF0 | −16 | −1 | −625 | −675/100 | **−6** |
| 0xE7,0x00 | 0xE700 | −6400 | −400 | −250000 | −250050/100 | **−2500** |
| 0x00,0x20 | 0x0020 | 32 | 2 | 1250 | 1300/100 | **13** (12.5 → 13) |
| 0xFF,0xE0 | 0xFFE0 | −32 | −2 | −1250 | −1300/100 | **−13** |
| 0xFF,0xFF | 0xFFF0 (마스킹) | −16 | −1 | −625 | −675/100 | **−6** |
| 0x18,0xF0 | 0x18F0 | 6384 | 399 | 249375 | 249425/100 | **2494** → `is_overheat(…, 2500)` = false |

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `(uint16_t)msb << 8`: `uint8_t`는 `int`로 승격된 뒤 시프트됩니다. 16비트 `int`인 MCU(MSP430, AVR)에서 `msb << 8`은 부호 비트에 닿을 수 있으니 **먼저 `uint16_t`로 캐스팅**하는 습관이 안전합니다.
- `(int16_t)reg`: 범위 밖 unsigned → signed 변환은 엄밀히 **구현 정의**(C11 6.3.1.3)지만 gcc/clang/armcc 모두 "비트 그대로"로 정의합니다. 완전 이식성이 필요하면 주석의 `if (v & 0x8000) v -= 0x10000;` 형태를 씁니다. 면접에서 이 차이를 한 문장으로 말하면 가산점입니다.
- `/ 16` vs `>> 4`: 마스킹 후에는 나눗셈이 정확하므로 결과가 같지만, `/`는 표준이 보장하고 `>>`(음수)는 보장하지 않습니다. 컴파일러는 `/16`을 어차피 시프트+보정 몇 명령으로 바꿉니다.
- 반올림: C의 `/`는 0 방향 절삭이라, `(num + 50) / 100`만 쓰면 음수에서 틀립니다(−625 → −575/100 = −5). 부호에 따라 ±50을 고릅니다.

### 4.5 복잡도

- 시간 **O(1)**, 메모리 **O(1)**. 곱셈 1번, 나눗셈 2번(상수 나눗셈이라 컴파일러가 곱셈으로 바꿈).

---

## 5. 임베디드 관점으로 말할 것

- **판정은 raw 단위로 할 수도 있다**: 임계값을 미리 counts로 바꿔 두면(`thresh_c100 → counts`, 부팅 시 1회) 매 샘플마다 곱셈·나눗셈 없이 정수 비교 한 번으로 과열 판정이 끝납니다. ISR에서 판정해야 할 때 유용합니다.
- **I2C 읽기 순서**: 포인터 레지스터(0x00)를 write → repeated START → 2바이트 read. MSB가 먼저 옵니다. 두 바이트를 따로 읽으면 그 사이 변환 결과가 바뀌어 **찢어진 값(torn read)**이 나올 수 있으니 한 트랜잭션으로 읽습니다.
- **에러 처리**: NACK·버스 타임아웃이면 이 함수를 부르지 말고 에러를 올립니다. `raw_to_c100`에는 "에러 값"이 없습니다(모든 바이트 쌍이 유효한 온도). 그래서 반환형을 `int` 상태 코드 + `int16_t *out`으로 분리하는 API가 더 안전합니다.
- **13비트 확장 모드(EM=1)**: 값이 bit 15..3에 있고 lsb bit 0이 EM 플래그입니다. `lsb & 0x01`로 모드를 보고, `& 0xF8` 마스킹 후 `/ 8`로 바꾸면 됩니다.

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Why not just `((msb << 8) \| lsb) >> 4`? | unsigned로 시프트하면 부호가 사라져 0xFFF0 → 4095 → 25594(255.94 °C)가 됩니다. signed로 시프트하면 음수 `>>`는 구현 정의입니다. `(int16_t)` 후 `/16`이 정확하고 표준적입니다. |
| How would you support 13-bit extended mode? | `lsb & 0x01`(EM 비트)로 분기, 값은 `(int16_t)(reg & 0xFFF8) / 8`. LSB 크기는 같은 0.0625 °C라 이후 계산은 동일, 범위만 ±256 °C로 넓어집니다. |
| How do you read these two bytes over I2C? | START, addr+W, 포인터 0x00, repeated START, addr+R, MSB(ACK), LSB(NACK), STOP. 한 트랜잭션으로 읽어야 바이트 사이에 값이 갱신돼도 안전합니다. |
| What if the sensor NACKs? | 재시도 횟수 제한 → 계속 실패하면 센서 fault로 보고하고 과열 판단은 fail-safe 정책(예: 팬 최대)으로. 마지막 값을 영원히 쓰면 안 됩니다 → [T1-6](../T1-6_sensor_fault_detect/index.html). |
| Can you avoid the division entirely? | 출력 단위를 1/16 °C(counts) 그대로 쓰거나, 임계값을 counts로 미리 변환해 비교만 합니다. 표시용 변환만 느린 경로에서 합니다. |

## 7. 자주 하는 실수

- `uint16_t`로 합친 뒤 `>> 4` → 음수가 큰 양수로 나옴 (테스트 2번 실패)
- 하위 4비트 마스킹을 안 함 → `0xFF,0xFF`가 0이 됨 (테스트 5번 실패)
- `(num + 50) / 100` 하나로 반올림 → 음수 반올림이 틀림 (−12.5 → −12, 테스트 4번 실패)
- `float`로 `* 0.0625` 계산 후 캐스팅 → FPU 없는 MCU에서 느리고, 캐스팅은 절삭이라 반올림도 틀림
- `int16_t`로 `counts * 625`를 계산 → 2047×625 = 1,279,375가 넘침. 곱셈은 `int32_t`로
