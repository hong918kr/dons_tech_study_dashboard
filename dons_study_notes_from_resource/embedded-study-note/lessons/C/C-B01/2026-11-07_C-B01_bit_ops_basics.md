---
id: C-B01
time: 40분
updated: 2026-11-07
---
# C-B01 비트연산 기초: Set Clear Toggle Test

## 학습 목표

- 마스크 `1u << n` 하나로 비트 **set / clear / toggle / test** 4가지를 바로 쓸 수 있다.
- 여러 비트짜리 **필드(field)** 를 "지우고 → 넣는" read-modify-write 패턴으로 바꿀 수 있다.
- `1 << 31`, `1u << 32` 같은 **정의되지 않은 동작(UB)** 함정을 피한다.
- `ODR |= x` 같은 RMW가 인터럽트와 경쟁하는 이유와, **BSRR 같은 원자적 set/reset 레지스터**가 왜 있는지 설명한다.

## 1. 문제

펌웨어 코드의 절반은 "레지스터의 몇 번 비트를 켜고 끄는 일"이다. 면접에서도 첫 5분 워밍업으로 거의 항상 나온다 — 그래서 **틀리면 안 되는 문제**다.

32비트 레지스터는 그냥 `uint32_t` 숫자다. 각 비트가 스위치 하나다. 비트 n 하나만 건드리려면 **그 자리만 1인 마스크** `1u << n` 을 만들고, 연산자를 고른다.

```svg
<svg viewBox="0 0 720 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">비트 5를 다루는 4가지 연산 (8비트만 표시)</text>
  <g fill="currentColor">
    <text x="10" y="62">reg</text><text x="10" y="102">mask = 1u &lt;&lt; 5</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="150" y="44" width="240" height="28"/><rect x="150" y="84" width="240" height="28"/>
  </g>
  <rect x="210" y="84" width="30" height="28" class="f-acc"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="165" y="63">1</text><text x="195" y="63">0</text><text x="225" y="63">0</text><text x="255" y="63">1</text>
    <text x="285" y="63">0</text><text x="315" y="63">1</text><text x="345" y="63">1</text><text x="375" y="63">0</text>
    <text x="165" y="103">0</text><text x="195" y="103">0</text><text x="225" y="103">1</text><text x="255" y="103">0</text>
    <text x="285" y="103">0</text><text x="315" y="103">0</text><text x="345" y="103">0</text><text x="375" y="103">0</text>
    <text x="165" y="132" font-size="11" opacity=".7">7</text><text x="225" y="132" font-size="11" opacity=".7">5</text><text x="375" y="132" font-size="11" opacity=".7">0</text>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="420" y="62">set    : reg |  mask   → 1011 0110</text>
    <text x="420" y="88">clear  : reg &amp; ~mask  → 1001 0110</text>
    <text x="420" y="114">toggle : reg ^  mask   → 1011 0110</text>
    <text x="420" y="140">test   : (reg &gt;&gt; 5) &amp; 1 → 0</text>
  </g>
  <text x="10" y="185" fill="currentColor">OR 는 1을 "켜고", AND-NOT 은 0으로 "끄고", XOR 는 "뒤집고", 시프트+AND 는 "읽는다".</text>
  <text x="10" y="210" fill="currentColor">마스크의 0 자리는 항상 원래 값 그대로 — 다른 비트를 건드리지 않는 것이 핵심.</text>
</svg>
```

### Part A — 단일 비트 연산

`bit_set`, `bit_clear`, `bit_toggle`, `bit_test` 4개 함수를 구현한다. 값을 받아 새 값을 돌려주는 순수 함수다 (`n` 은 0~31). 비트 31도 정상 동작해야 한다.

### Part B — 여러 비트짜리 필드 읽기/쓰기

실제 레지스터는 1비트 플래그만 있지 않다. STM32 `GPIOx_MODER` 는 **핀당 2비트**, 클럭 분주기는 3~4비트 필드다.

- `field_mask(width)` — 하위 `width` 비트가 모두 1인 마스크 (width 1~32)
- `field_get(reg, shift, width)` — `[shift+width-1 : shift]` 필드 값을 꺼낸다
- `field_set(reg, shift, width, val)` — 그 필드만 `val` 로 바꾸고 나머지 비트는 보존. `val` 이 필드보다 크면 **잘라낸다**.

### Part C — 가짜 GPIO 주변장치 드라이버

`GPIO_TypeDef` 구조체(가짜 메모리 맵 레지스터)로 작은 드라이버를 만든다.

- `gpio_set_mode(g, pin, mode)` — MODER의 해당 2비트 필드에 mode를 쓴다.
- `gpio_write(g, pin, level)` — **ODR을 직접 RMW 하지 말고 BSRR에 써서** 핀을 켜고 끈다. (BSRR 하위 16비트 = set, 상위 16비트 = reset. 하드웨어가 알아서 ODR에 반영 → 테스트에선 `sim_apply_bsrr()` 가 그 역할.)
- `gpio_toggle(g, pin)` — 현재 ODR 값을 읽어 반대로 쓴다.

## 2. Problem (English)

> **Bit manipulation on memory-mapped registers.**
>
> Implement the following in C11 without using compiler builtins:
>
> 1. `uint32_t bit_set(uint32_t reg, unsigned n)`, `bit_clear`, `bit_toggle`, and `bool bit_test(uint32_t reg, unsigned n)` for `0 <= n <= 31`.
> 2. `uint32_t field_get(uint32_t reg, unsigned shift, unsigned width)` and `uint32_t field_set(uint32_t reg, unsigned shift, unsigned width, uint32_t val)` for `1 <= width <= 32`, `shift + width <= 32`. `field_set` must preserve all bits outside the field and truncate `val` to `width` bits.
> 3. Given a GPIO peripheral with `MODER` (2 bits per pin), `ODR`, and a write-only `BSRR` (bits [15:0] set, bits [31:16] reset), write `gpio_set_mode`, `gpio_write`, and `gpio_toggle`. Explain why writing `BSRR` is preferable to `ODR |= (1u << pin)` when an ISR may also drive pins on the same port.
>
> Example: `field_set(0x00000000, 4, 4, 0x1F) == 0x000000F0`; `field_get(0xABCD1234, 8, 8) == 0x12`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 줄이 쭉 나오고 "1/20 passed" — TODO를 채워 20/20 을 만든다
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

## 4. 힌트

<details>
<summary>힌트 1 — 마스크부터 만든다</summary>

모든 연산은 `uint32_t m = 1u << n;` 에서 시작한다. 그 다음 연산자만 다르다: `|` (켜기), `& ~` (끄기), `^` (뒤집기).

</details>

<details>
<summary>힌트 2 — 왜 1이 아니라 1u ?</summary>

`1` 은 `int` (부호 있음, 32비트). `1 << 31` 은 부호 비트로 넘쳐서 **UB** 다. `1u` 는 `unsigned int` 라 31번까지 안전하다.

</details>

<details>
<summary>힌트 3 — 필드 쓰기는 두 단계</summary>

① 필드 자리를 0으로 지운다: `reg & ~(mask << shift)` ② 새 값을 넣는다: `| ((val << shift) & (mask << shift))`. 두 번째 `& mask` 가 넘치는 값을 잘라준다.

</details>

<details>
<summary>힌트 4 — width == 32 는 특별 취급</summary>

`(1u << width) - 1u` 는 width=32 일 때 `1u << 32` → 시프트 양이 타입 폭 이상이라 **UB**. 32면 `0xFFFFFFFFu` 를 바로 돌려준다.

</details>

## 5. 풀이

### Part A — 마스크 하나, 연산자 넷

| 연산 | 식 | 마스크가 1인 자리 | 마스크가 0인 자리 |
|---|---|---|---|
| set | `reg \| m` | 무조건 1 | 그대로 |
| clear | `reg & ~m` | 무조건 0 | 그대로 |
| toggle | `reg ^ m` | 뒤집힘 | 그대로 |
| test | `(reg >> n) & 1u` | 그 비트 값(0/1) | — |

test는 `(reg & m) != 0` 으로 써도 같다. 흔한 버그는 `(reg & m) == 1` — 비트 0일 때만 맞고 나머지는 항상 거짓이다 (n=5면 `reg & m` 은 `0x20`).

### Part B — 필드는 "지우고 → 넣는다"

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">field_set(reg, shift=4, width=4, val=0xA)</text>
  <g font-family="monospace" fill="currentColor">
    <text x="10" y="60">reg            = 0x1234_5678</text>
    <text x="10" y="90">m = 0xF &lt;&lt; 4   = 0x0000_00F0</text>
    <text x="10" y="120">① reg &amp; ~m     = 0x1234_5608</text>
    <text x="10" y="150">② (val&lt;&lt;4) &amp; m = 0x0000_00A0</text>
    <text x="10" y="180">①  |  ②        = 0x1234_56A8</text>
  </g>
  <rect x="455" y="40" width="250" height="160" rx="10" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="470" y="64" font-weight="bold">왜 ①을 먼저?</text>
    <text x="470" y="90">OR 은 1을 켤 수만 있다.</text>
    <text x="470" y="112">기존 필드 0x7 위에 0xA 를 OR</text>
    <text x="470" y="134">하면 0xF 가 되어 틀린다.</text>
    <text x="470" y="160">그래서 반드시 clear → set.</text>
    <text x="470" y="186">② 의 &amp; m 은 넘친 값 잘라내기.</text>
  </g>
  <text x="10" y="232" fill="currentColor">field_get 은 반대로: 오른쪽으로 shift 만큼 밀고 width 마스크로 거른다.</text>
</svg>
```

`field_mask(width)` 는 `(1u << width) - 1u` — 예를 들어 width=4 면 `0b10000 - 1 = 0b01111`. width=32는 UB를 피하려고 분기한다.

### Part C — 왜 BSRR인가 (RMW 경쟁)

`GPIOA->ODR |= (1u << 5);` 는 한 줄이지만 CPU에서는 **읽기 → 수정 → 쓰기** 3단계다. 읽기와 쓰기 사이에 인터럽트가 끼어들어 같은 포트의 다른 핀을 바꾸면, 메인 코드가 *옛날 값*을 다시 써서 ISR의 변경을 지워버린다 (lost update).

```svg
<svg viewBox="0 0 720 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="cb01-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="24" fill="currentColor" font-weight="bold">ODR |= (1u&lt;&lt;5) 가 ISR과 경쟁하는 모습</text>
  <text x="705" y="24" fill="currentColor" text-anchor="end" opacity=".6" font-size="12">시간 →</text>
  <rect x="84" y="40" width="626" height="70" rx="8" class="f-mut solid"/>
  <rect x="84" y="122" width="626" height="70" rx="8" class="f-mut solid"/>
  <rect x="84" y="204" width="626" height="44" rx="8" class="f-mut solid"/>
  <g fill="currentColor" font-weight="bold">
    <text x="14" y="80">main</text><text x="14" y="162">ISR</text><text x="14" y="231">ODR 값</text>
  </g>
  <rect x="98" y="51" width="190" height="48" rx="6" class="f-acc solid s-acc" stroke-width="1.5"/>
  <rect x="300" y="133" width="200" height="48" rx="6" class="f-warn solid s-warn" stroke-width="1.5"/>
  <rect x="512" y="51" width="190" height="48" rx="6" class="f-acc solid s-acc" stroke-width="1.5"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="193" y="70" font-weight="bold">① 읽기 (Read)</text>
    <text x="193" y="90" font-family="monospace" font-size="12.5">r0 = ODR → 0x80</text>
    <text x="400" y="152" font-weight="bold">② 인터럽트 끼어듦</text>
    <text x="400" y="172" font-family="monospace" font-size="12.5">ODR |= 0x04 → 0x84</text>
    <text x="607" y="70" font-weight="bold">③ 수정 + 쓰기</text>
    <text x="607" y="90" font-family="monospace" font-size="12.5">ODR = r0|0x20 → 0xA0</text>
  </g>
  <path d="M288 92 L312 130" stroke="currentColor" fill="none" stroke-dasharray="4 3" marker-end="url(#cb01-arr)"/>
  <path d="M488 130 L518 101" stroke="currentColor" fill="none" stroke-dasharray="4 3" marker-end="url(#cb01-arr)"/>
  <g text-anchor="middle" font-family="monospace" font-size="12.5">
    <text x="193" y="231" fill="currentColor">0x80</text>
    <text x="296" y="231" fill="currentColor" opacity=".5">→</text>
    <text x="400" y="231" class="f-ok" font-weight="bold">0x84 (핀2 ON)</text>
    <text x="506" y="231" fill="currentColor" opacity=".5">→</text>
    <text x="607" y="231" class="f-warn" font-weight="bold">0xA0 (핀2 사라짐!)</text>
  </g>
  <text x="10" y="282" fill="currentColor">결과 0xA0 — ISR이 켠 핀 2(0x04)가 사라졌다. 메인은 ①에서 읽어둔 "낡은 사본" 0x80으로 덮어썼다.</text>
  <text x="10" y="308" fill="currentColor">BSRR = 1u&lt;&lt;5 는 쓰기 한 번 — 하드웨어가 그 비트만 바꾸므로 중간에 끼어들 틈이 없다.</text>
</svg>
```

- **BSRR**: 하위 16비트에 1을 쓰면 그 핀 set, 상위 16비트에 1을 쓰면 reset. 0을 쓴 비트는 아무 일도 안 한다 → 단일 store 하나로 끝나는 **원자적** 연산.
- 해결책 비교: ① BSRR 같은 set/clear 전용 레지스터 (가장 좋음) ② 인터럽트 잠깐 끄고 RMW (`__disable_irq()`) ③ Cortex-M3/M4 bit-banding ④ LDREX/STREX.
- `volatile`: 컴파일러가 "같은 메모리를 두 번 읽을 필요 없네"라며 레지스터 접근을 없애거나 합치지 못하게 한다. 원자성을 보장하는 건 **아니다**.
- `gpio_toggle` 은 ODR을 *읽어서* 결정하므로 읽기와 쓰기 사이 경쟁은 남는다. STM32는 토글 전용 레지스터가 없어서, 진짜 원자적 토글이 필요하면 인터럽트를 잠깐 막는다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 20/20 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `reg \|= 1 << 31;` | `1` 은 signed int → 부호 비트로 시프트 = UB | `reg \|= 1u << 31;` |
| `if ((reg & (1u<<n)) == 1)` | n≠0 이면 결과가 `1<<n` 이라 항상 거짓 | `if (reg & (1u<<n))` 또는 `((reg>>n)&1u)` |
| `reg \|= val << shift;` (필드 쓰기) | 기존 필드를 안 지워서 비트가 섞임 | 먼저 `reg &= ~(mask<<shift)` |
| `(1u << width) - 1` 에 width=32 | 시프트 양 ≥ 32 → UB (x86은 `1u<<32 == 1` 로 동작해 마스크 0) | width ≥ 32 분기 |
| `reg & ~mask` 에서 mask가 `uint8_t` | `~` 하기 전에 int로 승격 → 결과는 맞지만 비교·대입에서 경고·버그 (C-B02) | 마스크는 `uint32_t` 로 통일 |
| `&&` / `\|\|` 를 비트 연산에 사용 | 논리 연산은 0/1만 돌려준다 | `&` / `\|` |
| W1C 상태 레지스터에 `SR \|= FLAG` | 읽은 값의 다른 1들까지 써서 **모든 플래그가 지워짐** | `SR = FLAG;` (추가 연습 6) |

## 8. 면접 Q&A

<details>
<summary>Q1. 비트 n을 set/clear/toggle/test 하는 코드를 써보세요. 왜 1u 인가요?</summary>

`r |= 1u<<n; r &= ~(1u<<n); r ^= 1u<<n; (r>>n)&1u`. `1` 은 signed int라 `1<<31` 은 부호 있는 오버플로(UB)다. 꼬리질문으로 "64비트 레지스터면?" → `1ull << n` (또는 `UINT64_C(1) << n`).

</details>

<details>
<summary>Q2. `PORT |= mask;` 가 ISR과 함께 쓰일 때 무슨 문제가 있나요? 어떻게 고치나요?</summary>

읽기-수정-쓰기 사이에 ISR이 같은 레지스터를 바꾸면 그 변경이 사라진다(lost update). 해결: set/reset 전용 레지스터(BSRR), 임계 구역(`__disable_irq`/`__enable_irq` 또는 PRIMASK 저장·복원), bit-banding, LDREX/STREX. ISR과 메인이 공유하는 레지스터 목록을 먼저 파악하는 게 설계 포인트.

</details>

<details>
<summary>Q3. `volatile` 은 무엇을 보장하고 무엇을 보장하지 않나요?</summary>

보장: 모든 읽기/쓰기가 소스에 적힌 대로, 생략·병합 없이 메모리에 일어남 (MMIO, ISR이 바꾸는 플래그에 필요). 보장 안 함: 원자성, CPU 간 메모리 순서(배리어 필요), 캐시 일관성. `volatile uint32_t x; x++;` 도 여전히 RMW 3단계다.

</details>

<details>
<summary>Q4. 비트필드 구조체(`struct { unsigned en:1; unsigned mode:2; }`)로 레지스터를 표현하면 안 되나요?</summary>

비트 배치 순서·패딩·접근 폭이 **구현 정의(implementation-defined)** 라 컴파일러/ABI가 바뀌면 깨질 수 있고, 필드 하나 쓰는 것도 숨은 RMW다. 그래서 벤더 헤더(CMSIS)는 `#define XXX_Pos / XXX_Msk` 매크로 + 마스크 연산을 쓴다. 단, 한 컴파일러로 고정된 프로젝트에서 가독성을 위해 쓰기도 한다.

</details>

<details>
<summary>Q5. 1인 비트 수를 세는 가장 빠른 방법은? x & (x-1) 은 무엇을 하나요?</summary>

`x & (x-1)` 은 가장 낮은 1비트를 지운다 → 1의 개수만큼만 루프 (Kernighan). 하드웨어 명령이 있으면 `__builtin_popcount` (ARMv8 `CNT`, x86 `POPCNT`), 없으면 SWAR 방식(0x5555…, 0x3333… 마스크로 병렬 합산). 추가 연습 1·2 참고.

</details>

<details>
<summary>Q6. 시프트 연산에서 UB가 되는 경우를 모두 말해보세요.</summary>

① 시프트 양이 음수이거나 (승격된) 타입 폭 이상 ② signed 값을 왼쪽으로 밀어서 표현 범위를 넘을 때 (음수를 왼쪽 시프트하는 것도 C11에선 UB) ③ 음수 오른쪽 시프트는 UB는 아니지만 **구현 정의** (보통 산술 시프트). 그래서 레지스터 코드는 항상 unsigned로.

</details>

## 9. 추가 연습

### 연습 1 — popcount: 1인 비트 개수 세기

`unsigned popcount32(uint32_t x)` 를 내장 함수 없이 구현하라. 루프 횟수가 1의 개수와 같게.

<details>
<summary>답 보기</summary>

`while (x) { x &= x - 1u; c++; }` — `x - 1` 은 가장 낮은 1을 0으로, 그 아래 0들을 1로 바꾼다. AND 하면 가장 낮은 1만 사라진다.

</details>

### 연습 2 — 2의 거듭제곱 판별

`bool is_pow2(uint32_t x)`. 0은 거짓이어야 한다.

<details>
<summary>답 보기</summary>

`x && !(x & (x - 1u))` — 2의 거듭제곱은 1이 정확히 하나라서 `x & (x-1)` 이 0. `x &&` 로 0을 거른다. (DMA 정렬, 링버퍼 크기 검사에 자주 씀.)

</details>

### 연습 3 — 가장 낮은 1비트만 남기기

`uint32_t lowest_set(uint32_t x)`: `0x58 → 0x08`.

<details>
<summary>답 보기</summary>

`x & (~x + 1u)` (= `x & -x`). 2의 보수 `-x` 는 가장 낮은 1 위쪽 비트가 모두 반전되어 AND 하면 그 비트만 남는다. 우선순위 비트맵 스케줄러에서 "가장 높은 우선순위 준비 태스크" 찾기에 쓰는 기법의 친척.

</details>

### 연습 4 — 니블 교환과 비트 뒤집기

`swap_nibbles(0xA5) == 0x5A`, `reverse8(0xB4) == 0x2D`. (실제 Rivian HM 스크린 질문: "바이트의 니블을 뒤집어라")

<details>
<summary>답 보기</summary>

니블 교환: `(b << 4) | (b >> 4)` 를 `uint8_t` 로 캐스트 (int로 승격되므로 상위 비트 잘라내기). 비트 뒤집기: 4비트 묶음 → 2비트 묶음 → 1비트 순서로 교환 (`0xF0/0x0F`, `0xCC/0x33`, `0xAA/0x55`). 루프 없이 3단계.

</details>

### 연습 5 — 바이트 순서 뒤집기 (bswap32)

`bswap32(0x11223344) == 0x44332211`.

<details>
<summary>답 보기</summary>

각 바이트를 제자리로 밀고 마스크: `(x>>24) | ((x>>8)&0xFF00) | ((x<<8)&0xFF0000) | (x<<24)`. 네트워크 바이트 순서(빅 엔디언) ↔ ARM/x86(리틀 엔디언) 변환. ARM은 `REV` 명령 한 개.

</details>

### 연습 6 — W1C 상태 레지스터 함정

하드웨어 규칙 "SR에 1을 쓴 비트만 지워진다(write-1-to-clear)" 에서, `SR |= FLAG` 습관이 왜 다른 플래그까지 지우는지 시뮬레이션으로 보여라.

<details>
<summary>답 보기</summary>

`SR |= FLAG` 는 `SR = SR | FLAG` — 현재 1인 *모든* 비트를 다시 쓴다 → 모두 1을 쓴 셈이라 전부 지워진다. 처리 안 한 인터럽트 플래그를 잃어버리는 실제 버그. 올바른 코드는 `SR = FLAG;` (지울 비트만 1).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 15/15 passed
```

## 10. 핵심 정리

- 마스크 `m = 1u << n` → set `|= m`, clear `&= ~m`, toggle `^= m`, test `(r >> n) & 1u`.
- 레지스터 코드는 **항상 unsigned**: `1u`, `1ul`, `1ull`. `1 << 31` 은 UB.
- 필드 쓰기 = **clear 후 set**: `r = (r & ~(M << s)) | ((v << s) & (M << s))`, `M = (1u << w) - 1` (w=32 예외).
- `r |= m` 은 RMW 3단계 → ISR과 경쟁. 원자적 set/reset 레지스터(BSRR), 임계 구역, bit-band, LDREX/STREX 로 해결.
- `volatile` = 접근 생략 금지. 원자성·순서 보장은 아님.
- W1C 레지스터는 `=` 로 쓴다. `|=` 금지.
- 자주 나오는 트릭: `x & (x-1)` (가장 낮은 1 제거), `x & -x` (가장 낮은 1 추출), 니블/비트/바이트 뒤집기.

## 참고자료

- [STM32F4 Reference Manual RM0090 — GPIO 레지스터 (MODER, ODR, BSRR)](https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [ARM CMSIS-Core — 레지스터 `_Pos`/`_Msk` 정의 규칙](https://arm-software.github.io/CMSIS_6/latest/Core/index.html)
- [Bit Twiddling Hacks (Sean Eron Anderson)](https://graphics.stanford.edu/~seander/bithacks.html)
- C11 표준 6.5.7 Bitwise shift operators — 시프트 UB 조건
- Michael Barr, *Embedded C Coding Standard* — 비트 연산·volatile 규칙
