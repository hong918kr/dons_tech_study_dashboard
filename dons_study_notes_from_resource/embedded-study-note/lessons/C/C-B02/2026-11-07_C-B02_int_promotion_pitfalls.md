---
id: C-B02
time: 50분
updated: 2026-11-07
---
# C-B02 고정 폭 정수와 정수 승격의 함정

## 학습 목표

- `uint8_t`/`uint16_t` 가 계산 전에 **int 로 승격(integer promotion)** 된다는 규칙을 코드로 확인한다.
- signed와 unsigned를 섞을 때의 **일반 산술 변환(usual arithmetic conversions)** 으로 `-1 < 1u` 가 거짓이 되는 이유를 설명한다.
- 오버플로 UB를 피하는 **캐스트 위치**를 고를 수 있다 (`(uint32_t)a * b`, `(uint32_t)p[3] << 24`).
- 랩어라운드에 안전한 **타이머/시퀀스 비교**와 **포화 연산**을 쓸 수 있다.

## 1. 문제

임베디드 C에서 가장 많은 "조용한 버그"는 정수 타입에서 나온다. 컴파일도 되고 테스트도 대부분 통과하는데, 49.7일 뒤(32비트 ms 카운터가 넘칠 때)나 특정 패킷 값에서만 터진다. 면접관은 이걸로 "C를 정말 아는지"를 본다.

규칙은 두 개뿐이다.

1. **정수 승격**: `int` 보다 작은 타입(`char`, `short`, `uint8_t`, `uint16_t`, 비트필드)은 산술 연산 전에 `int` 로 바뀐다 (값이 int에 다 들어가면). `~`, `<<`, `-` 같은 단항 연산도 마찬가지.
2. **일반 산술 변환**: 두 피연산자의 타입이 다르면 "더 큰" 쪽으로 맞춘다. 같은 크기에서 signed vs unsigned면 **unsigned가 이긴다** → 음수가 거대한 양수가 된다.

```svg
<svg viewBox="0 0 720 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">uint8_t a = 0xA5;  ~a == 0x5A ?</text>
  <g stroke="currentColor" fill="none"><rect x="20" y="45" width="120" height="34"/><rect x="230" y="45" width="300" height="34"/><rect x="230" y="120" width="300" height="34"/></g>
  <rect x="440" y="45" width="90" height="34" class="f-acc"/>
  <rect x="230" y="120" width="210" height="34" class="f-warn"/>
  <g fill="currentColor" font-family="monospace">
    <text x="45" y="67">0xA5</text>
    <text x="245" y="67">0x00 00 00  A5   (int 승격)</text>
    <text x="245" y="142">0xFF FF FF  5A   (~ 결과)</text>
    <text x="560" y="142">≠ 0x0000005A</text>
  </g>
  <path d="M140 62 L225 62" stroke="currentColor" marker-end="url(#ar)"/>
  <path d="M380 80 L380 115" stroke="currentColor" marker-end="url(#ar)"/>
  <defs><marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="150" y="56" fill="currentColor" font-size="11">승격</text>
  <text x="390" y="102" fill="currentColor" font-size="11">~ (32비트 전체 반전)</text>
  <text x="10" y="195" fill="currentColor">상위 24비트까지 뒤집혀서 b(=0x5A, 역시 int로 승격)와 절대 같을 수 없다 → 항상 false.</text>
  <text x="10" y="220" fill="currentColor">고침: (uint8_t)~a == b  — 비교 전에 다시 8비트로 잘라준다.</text>
  <text x="10" y="245" fill="currentColor">같은 이유로 uint8_t 두 개의 덧셈 200 + 100 은 44가 아니라 300 (int)이다.</text>
</svg>
```

### Part A — 승격 때문에 틀리는 계산 4개

- `checksum8(buf, n)` — 바이트 합의 하위 8비트 (패킷 체크섬).
- `is_complement(a, b)` — 체크 바이트 `b` 가 `a` 의 1의 보수인가? (시작 코드의 `~a == b` 는 왜 항상 false?)
- `mul_u16(a, b)` — 16×16 → 32비트 곱. `a * b` 를 그대로 쓰면 65535×65535 에서 **int 오버플로 = UB**.
- `load_le32(p)` — 리틀 엔디언 4바이트 → `uint32_t`. `p[3] << 24` 의 함정을 피하라.

### Part B — signed / unsigned 비교

- `in_range(x, limit)` — `int32_t x` 가 `[0, limit)` 에 있는가? `x < limit` 만 쓰면 `x = -1` 이 통과(?)하지 못하고… 정확히 무슨 일이 일어나는지 설명하라.
- `has_room(used, cap, len)` — `len` 바이트를 더 쓸 수 있나? `used + len <= cap` 은 `len` 이 거대하면 넘쳐서 참이 된다.

### Part C — 랩어라운드 타이머와 포화 연산

- `timer_elapsed(now, start, timeout)` — 32비트 틱 카운터가 `0xFFFFFFFF → 0` 으로 넘어가도 맞아야 한다.
- `sat_add_i16(a, b)` — 오디오/센서 샘플 덧셈. 넘치면 `INT16_MAX`/`INT16_MIN` 에 고정.

## 2. Problem (English)

> **Integer types in firmware.** Fix or implement each function so it is correct for *all* inputs and free of undefined behavior on a platform with 32-bit `int`:
>
> 1. `uint8_t checksum8(const uint8_t *buf, size_t n)` — sum of bytes modulo 256.
> 2. `bool is_complement(uint8_t a, uint8_t b)` — the current code `return ~a == b;` always returns false. Explain why and fix it.
> 3. `uint32_t mul_u16(uint16_t a, uint16_t b)` — exact 32-bit product.
> 4. `uint32_t load_le32(const uint8_t *p)` — little-endian load without UB.
> 5. `bool in_range(int32_t x, uint32_t limit)` — true iff `0 <= x < limit`.
> 6. `bool has_room(size_t used, size_t cap, size_t len)` — true iff `used + len <= cap` mathematically (assume `used <= cap`), without overflow.
> 7. `bool timer_elapsed(uint32_t now, uint32_t start, uint32_t timeout)` — correct across tick-counter wraparound (elapsed time < 2^32).
> 8. `int16_t sat_add_i16(int16_t a, int16_t b)` — saturating add.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter   # 9/23 passed 에서 시작
```

## 4. 힌트

<details>
<summary>힌트 1 — "이 식의 타입은 무엇인가?"를 매번 묻는다</summary>

`uint8_t + uint8_t` → int, `uint16_t * uint16_t` → int, `~uint8_t` → int, `uint8_t << 24` → int. `uint32_t` 와 `int` 가 섞이면 → `unsigned int`. 각 줄에 타입을 적어보면 버그가 보인다.

</details>

<details>
<summary>힌트 2 — 캐스트는 "연산 전에", 결과가 아니라 피연산자에</summary>

`(uint32_t)(a * b)` 는 이미 UB가 일어난 뒤에 캐스트하는 것. `(uint32_t)a * b` 처럼 피연산자 하나를 먼저 넓히면, 다른 쪽도 따라서 `uint32_t` 로 변환된다.

</details>

<details>
<summary>힌트 3 — 오버플로를 피하는 비교는 "빼기"로 바꾼다</summary>

`used + len <= cap` ⇔ `len <= cap - used` (used ≤ cap 이면 뺄셈은 넘치지 않는다). 타이머도 `now >= start + timeout` 대신 **경과 시간** `now - start` 를 계산해 비교한다.

</details>

<details>
<summary>힌트 4 — 부호 없는 뺄셈은 넘쳐도 정의되어 있다</summary>

unsigned 연산은 모듈로 2^N 으로 정의된다(UB 아님). `5 - 0xFFFFFFFE` 는 `7` — 카운터가 넘어간 뒤에도 경과 시간이 정확하다.

</details>

## 5. 풀이

### Part A — 계산 직전 타입을 맞춘다

**checksum8**: `sum + buf[i]` 는 int로 계산되어 255를 넘는다. `sum = (uint8_t)(sum + buf[i])` 처럼 저장할 때 잘리므로 결과는 맞지만, `-Wconversion` 경고를 없애고 의도를 보이려고 명시적으로 캐스트한다. `0xF0 + 0x20 + 0x01 = 0x111` → 하위 8비트 `0x11`.

**is_complement**: 그림처럼 `~a` 는 32비트 int 전체를 뒤집는다. 다시 `(uint8_t)` 로 잘라서 비교.

**mul_u16**: 함정 중의 함정이다. `uint16_t` 는 *unsigned* 인데도 **signed int 로 승격**된다. 65535 × 65535 = 4,294,836,225 > INT_MAX → signed 오버플로 UB. 컴파일러는 "UB는 안 일어난다"고 가정하고 최적화하므로, 결과가 우연히 맞아 보여도 믿을 수 없다. `(uint32_t)a * b` 로 고친다.

**load_le32**: `p[3]` 이 `0x80` 이상이면 `p[3] << 24` 가 int의 부호 비트로 넘어간다 → UB. 각 바이트를 `(uint32_t)` 로 먼저 캐스트.

```svg
<svg viewBox="0 0 720 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">메모리 [EF BE AD DE] → 0xDEADBEEF (리틀 엔디언: 낮은 주소 = 낮은 바이트)</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="45" width="60" height="30"/><rect x="80" y="45" width="60" height="30"/><rect x="140" y="45" width="60" height="30"/><rect x="200" y="45" width="60" height="30"/>
    <rect x="360" y="120" width="80" height="30"/><rect x="440" y="120" width="80" height="30"/><rect x="520" y="120" width="80" height="30"/><rect x="600" y="120" width="80" height="30"/>
  </g>
  <rect x="200" y="45" width="60" height="30" class="f-warn"/><rect x="360" y="120" width="80" height="30" class="f-warn"/>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="50" y="65">EF</text><text x="110" y="65">BE</text><text x="170" y="65">AD</text><text x="230" y="65">DE</text>
    <text x="50" y="92" font-size="11">p[0]</text><text x="110" y="92" font-size="11">p[1]</text><text x="170" y="92" font-size="11">p[2]</text><text x="230" y="92" font-size="11">p[3]</text>
    <text x="400" y="140">DE</text><text x="480" y="140">AD</text><text x="560" y="140">BE</text><text x="640" y="140">EF</text>
    <text x="400" y="168" font-size="11">&lt;&lt;24</text><text x="480" y="168" font-size="11">&lt;&lt;16</text><text x="560" y="168" font-size="11">&lt;&lt;8</text><text x="640" y="168" font-size="11">&lt;&lt;0</text>
  </g>
  <path d="M230 78 C 260 110, 330 120, 358 132" stroke="currentColor" fill="none"/>
  <text x="10" y="192" fill="currentColor">0xDE(≥0x80)를 int 로 24비트 밀면 부호 비트를 침범 → UB. (uint32_t)p[3] &lt;&lt; 24 로 고친다.</text>
</svg>
```

### Part B — 섞이면 unsigned 가 이긴다

`x < limit` 에서 `x` 는 `int32_t`, `limit` 은 `uint32_t` → x가 `uint32_t` 로 변환된다. `x = -1` 은 `0xFFFFFFFF` (4,294,967,295) 가 되어 `limit` 보다 커진다. 이번 예에선 우연히 false가 나와서 맞아 보이지만, `limit = UINT32_MAX` 이면 `INT32_MIN` (→ `0x80000000`) 이 범위 안으로 판정된다. 해결: **음수를 먼저 걸러내고**, 그 다음 안전하게 캐스트해서 비교. `-Wsign-compare` 를 켜두면 컴파일러가 대부분 잡아준다.

`has_room`: `size_t` 는 unsigned라 `used + len` 이 `SIZE_MAX` 를 넘으면 0 근처로 돌아가서 "공간 있음"이 된다 → 버퍼 오버플로 보안 취약점의 전형. `len <= cap - used` 로 바꾸면 뺄셈은 `used <= cap` 이므로 안전.

### Part C — 경과 시간으로 비교하고, 넓혀서 포화

```svg
<svg viewBox="0 0 720 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">32비트 틱 카운터 랩어라운드 (start = 0xFFFFFF00, timeout = 300)</text>
  <line x1="30" y1="80" x2="690" y2="80" stroke="currentColor"/>
  <rect x="60" y="65" width="300" height="30" class="f-mut"/>
  <rect x="360" y="65" width="200" height="30" class="f-acc"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="40" y="115">0xFFFFFF00</text><text x="320" y="115">0xFFFFFFFF→0</text><text x="540" y="115">now=100</text>
  </g>
  <text x="10" y="150" fill="currentColor">틀림: now &gt;= start + timeout → 100 &gt;= 0xFFFFFF00+300 (=44) → 참? 시작 직후 now=0xFFFFFF10 에서도 0xFFFFFF10 &gt;= 44 → 참! (즉시 타임아웃)</text>
  <text x="10" y="175" fill="currentColor">맞음: (uint32_t)(now - start) &gt;= timeout → 100 - 0xFFFFFF00 = 356 &gt;= 300 → 참.</text>
  <text x="10" y="200" fill="currentColor">경과 시간이 2^32 틱 미만이면 언제나 정확 (1 kHz 틱이면 약 49.7일).</text>
</svg>
```

`(uint32_t)(now - start)` 의 캐스트는 32비트 int 플랫폼에선 없어도 되지만, 타입이 `uint16_t` 인 카운터라면 **필수**다 (빼기 결과가 int로 승격되어 음수가 되므로). 그래서 습관적으로 붙인다.

`sat_add_i16`: `int32_t` 로 넓혀서 더하면 절대 넘치지 않는다(±65534). 그 다음 범위를 검사해서 고정. ARM에는 `QADD16` 같은 포화 명령이 있고 CMSIS-DSP의 `__QADD16` 이 이를 쓴다 (DSP 트랙에서 다룸).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 23/23 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `uint32_t r = a * b;` (a,b: `uint16_t`) | int로 승격 → signed 오버플로 UB | `(uint32_t)a * b` |
| `(uint32_t)(a * b)` | 이미 UB가 일어난 뒤 캐스트 | 피연산자를 캐스트 |
| `p[3] << 24` | `uint8_t` → int, 부호 비트 침범 UB | `(uint32_t)p[3] << 24` |
| `if (~a == b)` | `~` 가 32비트 전체 반전 | `(uint8_t)~a == b` |
| `if (x < sizeof(buf))` (x: int) | x가 음수면 거대한 `size_t` 로 변환 | `x >= 0 && (size_t)x < sizeof buf` |
| `if (used + len <= cap)` | unsigned 덧셈 넘침 → 거짓 통과 | `len <= cap - used` |
| `if (now >= start + timeout)` | 카운터 랩어라운드에서 오판 | `(uint32_t)(now - start) >= timeout` |
| `for (uint8_t i = 0; i < 256; i++)` | i는 256이 될 수 없음 → 무한 루프 | 루프 변수는 `unsigned`/`size_t` |
| `char c = 0xFF; if (c == 0xFF)` | `char` 부호가 구현 정의 (ARM은 unsigned, x86은 signed) | 바이트 데이터는 `uint8_t` |

## 8. 면접 Q&A

<details>
<summary>Q1. integer promotion 이 무엇이고, 왜 uint16_t 끼리 곱하면 UB가 될 수 있나요?</summary>

int보다 작은 정수 타입은 연산 전에 int로 승격된다. uint16_t의 모든 값은 32비트 int에 들어가므로 *signed* int가 된다. 65535×65535는 INT_MAX를 넘어 signed overflow → UB. (16비트 int 플랫폼이었다면 unsigned int로 승격되어 모듈로 연산 — 그래서 "플랫폼에 따라 다르다"가 핵심 포인트.)

</details>

<details>
<summary>Q2. `-1 < 1u` 의 값은? `-1 < 1ul` 은? `-1L < 1u` 는?</summary>

`-1 < 1u` → 0 (int가 unsigned int로 변환되어 0xFFFFFFFF). `-1 < 1ul` → 0 (unsigned long 승리). `-1L < 1u` → LP64(맥/리눅스 64비트)에선 long(64비트)이 unsigned int(32비트)의 모든 값을 담을 수 있으므로 1u가 long으로 변환 → 1(참). 32비트 ARM(ILP32)에선 long도 32비트라 → 0. "같은 코드가 플랫폼마다 다르다"는 게 정답의 요지.

</details>

<details>
<summary>Q3. 49.7일 버그를 아나요? 타이머 비교는 어떻게 써야 하나요?</summary>

32비트 ms 카운터는 2^32 ms ≈ 49.7일 후 0으로 돌아간다 (Windows 95, Boeing 787 GCU 248일 버그 등이 같은 계열). `now >= deadline` 식 절대 비교 대신 `(uint32_t)(now - start) >= timeout` 처럼 **경과 시간**을 unsigned 뺄셈으로 구한다. 데드라인을 저장해야 하면 `(int32_t)(now - deadline) >= 0` (경과 차이 < 2^31 가정).

</details>

<details>
<summary>Q4. unsigned overflow 와 signed overflow 의 차이는?</summary>

unsigned는 모듈로 2^N 으로 **정의된 동작**. signed는 **UB** — 컴파일러가 `x + 1 > x` 를 항상 참으로 최적화하는 등 예상 못한 코드가 나온다. 감지: `-fsanitize=undefined` (UBSan), `__builtin_add_overflow`. 의도적 래핑이 필요하면 unsigned로 계산 후 변환.

</details>

<details>
<summary>Q5. int8_t 에 200을 넣으면? (int8_t)0x80 은?</summary>

C11에선 signed 타입으로의 범위 밖 변환은 **구현 정의**(또는 시그널) — 실제 모든 주요 컴파일러는 2의 보수로 잘라서 -56, -128. C23부터는 2의 보수 표현이 표준으로 고정됐지만, 범위 밖 *산술* 오버플로는 여전히 UB.

</details>

<details>
<summary>Q6. 왜 MISRA C는 "essential type model"을 강조하나요?</summary>

위 함정들(암묵적 승격, signed/unsigned 혼합, 좁은 타입으로의 암묵적 대입)을 막기 위해서다. MISRA C:2012 Rule 10.x 는 서로 다른 essential type 간 암묵적 변환을 금지하고, 복합 식의 결과를 더 넓은 타입에 대입하는 것을 금지한다 (`uint32_t r = u16a * u16b;` 위반). TEST 트랙의 MISRA 아이템과 연결.

</details>

## 9. 추가 연습

### 연습 1 — 결과 예측 퀴즈

코드를 돌리기 전에 각 식의 값을 종이에 적어라: `a + b` (`uint8_t` 200, 100), `(uint8_t)(a + b)`, `sizeof(a + b)`, `-1 < 1u`, `-1 < (int64_t)1u`, `(int8_t)0x80`, `uint16_t 0xFFFF + 1`, `uint32_t 0xFFFFFFFF + 1`.

<details>
<summary>답 보기</summary>

300 / 44 / `sizeof(int)`=4 / 0(거짓) / 1(참) / -128 / 65536 (int로 승격, 넘침 없음) / 0 (uint32_t는 승격되지 않고 모듈로). 핵심 대비: **16비트는 승격돼서 안 넘치고, 32비트는 승격 안 돼서 넘친다.**

</details>

### 연습 2 — 16비트 시퀀스 번호 비교

패킷 번호가 `uint16_t` 로 0~65535를 돈다. `seq16_before(a, b)` — a가 b보다 "앞"인가 (`65530` 은 `5` 보다 앞).

<details>
<summary>답 보기</summary>

`(int16_t)(uint16_t)(a - b) < 0`. `a - b` 는 int로 승격되므로 먼저 `uint16_t` 로 잘라 모듈로 2^16 차이를 만들고, `int16_t` 로 해석하면 차이가 ±32767 이내일 때 부호가 순서를 알려준다 (RFC 1982 serial number arithmetic).

</details>

### 연습 3 — 12비트 ADC 값 부호 확장

12비트 2의 보수 ADC 출력(0x800 = -2048, 0xFFF = -1)을 `int16_t` 로.

<details>
<summary>답 보기</summary>

이식성 있는 트릭: `(raw ^ 0x800) - 0x800`. 부호 비트를 뒤집으면 0~4095 오프셋 값이 되고, 2048을 빼면 -2048~2047. `(int16_t)(raw << 4) >> 4` 는 signed 오른쪽 시프트가 구현 정의라 피한다.

</details>

### 연습 4 — 오버플로 없는 평균

`avg_u32(0xFFFFFFFF, 0xFFFFFFFD) == 0xFFFFFFFE` — `(a + b) / 2` 는 넘친다.

<details>
<summary>답 보기</summary>

`(a & b) + ((a ^ b) >> 1)` — 둘 다 1인 비트(그대로)와 한쪽만 1인 비트(절반)로 나눠 더한다. 또는 `a + (b - a) / 2` (a ≤ b 일 때). 이진 탐색의 `mid = lo + (hi - lo) / 2` 와 같은 이유 (Java `binarySearch` 버그, 2006).

</details>

### 연습 5 — INT32_MIN 절댓값

`abs(INT32_MIN)` 은 UB(결과가 int32에 안 들어감). 결과를 `uint32_t` 로 돌려주는 안전한 버전을 써라.

<details>
<summary>답 보기</summary>

`x < 0 ? 0u - (uint32_t)x : (uint32_t)x` — 먼저 unsigned로 바꾼 뒤 unsigned 뺄셈(모듈로)으로 부정. `-x` 를 signed로 계산하지 않는 게 포인트.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 18/18 passed
```

## 10. 핵심 정리

- int보다 작은 타입은 연산 전에 **int(signed)** 로 승격된다 — `uint8_t`, `uint16_t` 도.
- signed + unsigned (같은 폭) → **unsigned**. 음수는 거대한 양수가 된다. `-Wsign-compare` 를 켜라.
- 캐스트는 **연산 전에 피연산자에**: `(uint32_t)a * b`, `(uint32_t)p[3] << 24`.
- `~`, `<<` 결과는 int → 좁은 타입과 비교하려면 다시 캐스트.
- 오버플로 가능한 비교는 **빼기**로: `len <= cap - used`, `(uint32_t)(now - start) >= timeout`.
- unsigned 넘침 = 정의됨(모듈로), signed 넘침 = **UB** (UBSan으로 잡기).
- 포화 연산은 넓은 타입에서 계산 후 범위 고정.

## 참고자료

- C11 표준 (N1570) 6.3.1.1 Boolean, characters, and integers (promotion), 6.3.1.8 Usual arithmetic conversions
- [SEI CERT C — INT02-C. Understand integer conversion rules](https://wiki.sei.cmu.edu/confluence/display/c/INT02-C.+Understand+integer+conversion+rules)
- [SEI CERT C — INT30-C. Ensure that unsigned integer operations do not wrap](https://wiki.sei.cmu.edu/confluence/display/c/INT30-C.+Ensure+that+unsigned+integer+operations+do+not+wrap)
- [RFC 1982 — Serial Number Arithmetic](https://www.rfc-editor.org/rfc/rfc1982)
- MISRA C:2012 Rule 10.1–10.8 (essential type model)
- [Clang UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
