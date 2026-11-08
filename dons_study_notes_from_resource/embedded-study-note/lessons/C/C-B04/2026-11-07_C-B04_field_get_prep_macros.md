---
id: C-B04
time: 50분
updated: 2026-11-07
---
# C-B04 비트 필드 추출·삽입: Mask와 Shift로 GET/SET_FIELD

## 학습 목표

- `GENMASK(h, l)` 로 비트 h..l 마스크를 **UB 없이** 만든다 (h = 31 포함).
- shift 인자 없이 **마스크 하나만** 받는 `FIELD_GET` / `FIELD_PREP` / `FIELD_REPLACE` 를 Linux `<linux/bitfield.h>` 스타일로 구현한다. shift 는 마스크의 가장 낮은 1비트(`mask & -mask`)에서 유도한다.
- CMSIS `_Pos` / `_Msk` 이름 규칙으로 레지스터 맵을 정의하고, `_Static_assert` 로 "마스크가 연속인가 / 값이 들어가나 / 필드끼리 겹치지 않나" 를 **컴파일타임에** 잡는다.
- 가짜 타이머 `CTRL` 레지스터 드라이버를 쓰고, 표 테스트로 **다른 필드가 그대로임** 을 증명한다.

## 1. 문제

데이터시트의 레지스터 표는 거의 다 "bits 7:4 PRESC, bits 3:1 MODE" 같은 **다중 비트 필드** 다. 면접에서 "이 필드만 바꾸는 매크로를 써보라" 는 C-B01 의 다음 단골 질문이고, 실제 드라이버 리뷰에서는 "shift 와 mask 를 따로 들고 다니다 짝이 어긋난 버그" 가 흔하다.

C-B01 에서는 `field_set(reg, shift, width, val)` 처럼 **shift 와 width 를 따로** 받았다. 이번엔 한 단계 위로 간다: 필드는 **마스크 하나** 로만 표현하고 (`0x000000F0`), shift(4)·폭(4)·최댓값(15)은 전부 마스크에서 계산한다. 그러면 `PRESC_Pos` 와 `MODE_Msk` 를 섞어 쓰는 실수가 원천 차단된다. Linux 커널(`FIELD_GET`/`FIELD_PREP`), Zephyr(`sys/util.h`), CMSIS(`_FLD2VAL`/`_VAL2FLD`) 모두 이 방향이다. clear-then-set 원리 자체는 C-B01 Part B 를 참고하고, 여기서는 **매크로 설계와 검사** 에 집중한다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">가짜 TIM2-&gt;CTRL (32비트) 레지스터 맵</text>
  <g stroke="currentColor">
    <rect x="50" y="50" width="160" height="34" class="f-mut"/>
    <rect x="210" y="50" width="160" height="34" class="f-acc"/>
    <rect x="370" y="50" width="140" height="34" class="f-mut"/>
    <rect x="510" y="50" width="20" height="34" class="f-ok"/>
    <rect x="530" y="50" width="80" height="34" class="f-acc"/>
    <rect x="610" y="50" width="60" height="34" class="f-warn"/>
    <rect x="670" y="50" width="20" height="34" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="130" y="72">reserved</text><text x="290" y="72">DTIME</text><text x="440" y="72">reserved</text>
    <text x="570" y="72">PRESC</text><text x="640" y="72">MODE</text>
    <text x="520" y="108" font-size="12">IRQEN</text><text x="690" y="108" font-size="12">EN</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle" opacity=".75">
    <text x="60" y="44">31</text><text x="200" y="44">24</text><text x="220" y="44">23</text><text x="360" y="44">16</text>
    <text x="500" y="44">9</text><text x="520" y="44">8</text><text x="540" y="44">7</text><text x="600" y="44">4</text>
    <text x="620" y="44">3</text><text x="660" y="44">1</text><text x="680" y="44">0</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" xml:space="preserve">
    <text x="50" y="140">TIM_CTRL_MODE_Pos  = 1    TIM_CTRL_MODE_Msk  = 0x7  &lt;&lt; 1  = 0x0000000E = GENMASK(3,1)</text>
    <text x="50" y="162">TIM_CTRL_PRESC_Pos = 4    TIM_CTRL_PRESC_Msk = 0xF  &lt;&lt; 4  = 0x000000F0 = GENMASK(7,4)</text>
    <text x="50" y="184">TIM_CTRL_DTIME_Pos = 16   TIM_CTRL_DTIME_Msk = 0xFF &lt;&lt; 16 = 0x00FF0000 = GENMASK(23,16)</text>
  </g>
  <text x="50" y="216" fill="currentColor">마스크 하나만 알면 Pos(가장 낮은 1의 위치), 폭(1의 개수), 최댓값(Msk &gt;&gt; Pos)이 전부 나온다.</text>
  <text x="50" y="238" fill="currentColor">reserved 비트는 "읽은 값 그대로 다시 쓴다" — FIELD_REPLACE 가 자동으로 보존한다.</text>
</svg>
```

### Part A — GENMASK 와 마스크 전용 FIELD 매크로

모두 **매크로** 로, 내장 함수(builtin) 없이 쓴다. `uint32_t` 기준.

- `GENMASK(h, l)` — 비트 h..l 이 1. `GENMASK(31, 0) == 0xFFFFFFFF`, `GENMASK(31, 28) == 0xF0000000` 이 UB 없이 나와야 한다.
- `FIELD_LSB(mask)` — 마스크의 가장 낮은 1비트만 남긴 값 (`0x70 → 0x10`, 즉 `1u << shift`).
- `FIELD_GET(mask, reg)` — 필드 값 꺼내기. `FIELD_PREP(mask, val)` — 필드 자리에 놓을 값 만들기 (넘친 비트는 잘라냄).
- `FIELD_REPLACE(mask, reg, val)` — `reg` 에서 그 필드만 바꾼 새 값.
- 테스트에는 528개 연속 마스크 전부를 `__builtin_ctz` 기반 참조 구현과 대조하는 전수 검사가 들어 있다.

### Part B — 검사 매크로와 컴파일타임 체크

- `FIELD_MAX(mask)` (필드 최댓값), `FIELD_MASK_OK(mask)` (0이 아니고 1이 **연속**), `FIELD_FIT(mask, val)` (값이 들어가나).
- `FIELD_PREP_CONST(mask, val)` — 상수 인자일 때 마스크가 이상하거나 값이 넘치면 **컴파일 에러** 가 나게 한다 (`_Static_assert` 를 식 안에 넣는 트릭).
- 다 되면 `STATIC_CHECKS` 를 1로 바꿔, Part C 레지스터 맵 자체를 `_Static_assert` 로 검증한다 (GENMASK 와 일치, 필드 겹침 없음, 빈틈 없음).

### Part C — 가짜 타이머 CTRL 드라이버

- `tim_set_mode(t, mode)` — 모드 4..7(예약)은 **거부** (`false`, 레지스터 손대지 않음).
- `tim_set_presc(t, presc)` / `tim_get_presc(t)` — 4비트에 안 맞는 값은 잘라 쓰지 말고 거부.
- `tim_configure(t, mode, presc, dtime, irq)` — 4개 필드를 **읽기 1번 + 쓰기 1번** 으로 바꾼다. 하나라도 틀리면 아무것도 안 쓴다.
- 표 테스트: (필드, 값) 10줄 × 시작값 5개 — 바꾼 필드만 바뀌고 나머지 비트(예약 포함)는 그대로인지 확인.

## 2. Problem (English)

> **Mask-only register field accessors.**
>
> Implement in C11 (no compiler builtins), for 32-bit registers:
>
> 1. `GENMASK(h, l)` producing a mask with bits `h..l` set, valid for `0 <= l <= h <= 31` without undefined behavior.
> 2. `FIELD_GET(mask, reg)`, `FIELD_PREP(mask, val)`, `FIELD_REPLACE(mask, reg, val)` that take **only the mask** — derive the shift from the mask's lowest set bit. They must be usable in integer constant expressions when the arguments are constants.
> 3. `FIELD_MASK_OK(mask)` (non-zero and contiguous), `FIELD_FIT(mask, val)`, and `FIELD_PREP_CONST(mask, val)` that fails to **compile** when the mask is invalid or the value does not fit.
> 4. For a timer `CTRL` register (EN[0], MODE[3:1], PRESC[7:4], IRQEN[8], DTIME[23:16], other bits reserved), write `tim_set_mode`, `tim_set_presc`, `tim_get_presc`, `tim_configure`. Reject out-of-range values without touching the register; preserve every bit outside the targeted fields.
>
> Example: `FIELD_GET(0x0000FF00, 0xABCD1234) == 0x12`, `FIELD_PREP(0xF0, 0x1F) == 0xF0`, `FIELD_REPLACE(0xF0, 0x12345678, 0xA) == 0x123456A8`. Follow-up: what does the compiler emit for `FIELD_GET` with a constant mask on Cortex-M4?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "10/47 passed" — TODO 를 채우고 STATIC_CHECKS 를 1로 바꿔 47/47 을 만든다
```

## 4. 힌트

<details>
<summary>힌트 1 — GENMASK: "아래를 깎고, 위를 깎는다"</summary>

`0xFFFFFFFFu << l` 은 아래 l 비트를 0으로, `0xFFFFFFFFu >> (31 - h)` 는 h 위쪽을 0으로 만든다. 둘을 AND. 두 시프트 양 모두 0~31 이라 UB 가 없다. `(1u << (h + 1)) - 1` 방식은 h = 31 에서 `1u << 32` → UB.

</details>

<details>
<summary>힌트 2 — 가장 낮은 1비트 찾기</summary>

unsigned 에서 `-x == ~x + 1` (2의 보수). `x & -x` 는 가장 낮은 1만 남긴다 (C-B01 추가 연습 3). 마스크 `0x70` 이면 `0x10` = `1u << 4`.

</details>

<details>
<summary>힌트 3 — shift 를 몰라도 시프트할 수 있다</summary>

`x >> 4 == x / 16`, `x << 4 == x * 16` (unsigned, 모듈로 2^32). 16 은 바로 힌트 2의 `FIELD_LSB`. 곱셈·나눗셈은 상수식이라 `_Static_assert` 에도 쓸 수 있고, 컴파일러는 2의 거듭제곱이면 시프트로 바꾼다.

</details>

<details>
<summary>힌트 4 — 연속 마스크 판별</summary>

연속인 1 덩어리에 그 최하위 비트를 더하면 받아올림이 덩어리 위로 쭉 올라가 덩어리가 통째로 0이 된다. 그래서 `(mask + LSB) & mask == 0`. 구멍이 있으면 위쪽 1이 살아남는다.

</details>

<details>
<summary>힌트 5 — 식 안에 _Static_assert 넣기</summary>

`_Static_assert` 는 선언이라 식 자리에 못 쓴다. 하지만 C11 은 struct 멤버 목록 안에 static_assert 선언을 허용한다 → `sizeof(struct { _Static_assert(...); char c; })` 는 **식** 이다. 여기에 `0u *` 를 곱해 더하면 값은 안 바뀐다.

</details>

## 5. 풀이

### Part A — 마스크에서 shift 를 유도한다

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">FIELD_LSB(0x70) — 가장 낮은 1비트 뽑기 (하위 8비트만 표시)</text>
  <g font-family="monospace" fill="currentColor" xml:space="preserve">
    <text x="20" y="60">mask         0111 0000  = 0x70</text>
    <text x="20" y="88">~mask        1000 1111</text>
    <text x="20" y="116">~mask + 1    1001 0000  (= -mask)</text>
    <text x="20" y="144">mask &amp; -mask 0001 0000  = 0x10 = 1u &lt;&lt; 4</text>
  </g>
  <rect x="440" y="40" width="290" height="120" rx="10" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="455" y="64" font-weight="bold">LSB = 0x10 을 쓰는 법</text>
    <text x="455" y="90" font-family="monospace">GET : (reg &amp; mask) / LSB</text>
    <text x="455" y="112" font-family="monospace">PREP: (val * LSB) &amp; mask</text>
    <text x="455" y="138">/16 == &gt;&gt;4, *16 == &lt;&lt;4 (unsigned)</text>
  </g>
  <text x="20" y="190" fill="currentColor">~mask 는 1 덩어리를 0으로, 그 아래 0들을 1로 만든다. +1 하면 아래 1들이 받아올림으로 0이 되며</text>
  <text x="20" y="212" fill="currentColor">덩어리의 최하위 자리에서 멈춘다. 그 자리만 원래 mask 와 겹치므로 AND 결과는 그 비트 하나.</text>
  <text x="20" y="236" fill="currentColor">shift 숫자(4)는 끝내 필요 없다 — "2^4 로 나누기/곱하기" 로 같은 일을 한다.</text>
</svg>
```

| 매크로 | 식 | 같은 뜻의 shift 버전 |
|---|---|---|
| `GENMASK(h,l)` | `(0xFFFFFFFFu << l) & (0xFFFFFFFFu >> (31u - h))` | — |
| `FIELD_LSB(m)` | `m & (~m + 1u)` | `1u << ctz(m)` |
| `FIELD_GET(m, r)` | `(r & m) / FIELD_LSB(m)` | `(r & m) >> ctz(m)` |
| `FIELD_PREP(m, v)` | `(v * FIELD_LSB(m)) & m` | `(v << ctz(m)) & m` |
| `FIELD_REPLACE(m, r, v)` | `(r & ~m) \| FIELD_PREP(m, v)` | C-B01 `field_set` |

**왜 곱셈·나눗셈인가?**

1. **정확성**: unsigned 정수의 `E1 >> E2` 는 표준상 `E1 / 2^E2` 의 정수 부분이다 (C11 6.5.7p5). `E1 << E2` 는 `E1 × 2^E2` 를 2^32 로 나눈 나머지 (6.5.7p4). 즉 곱셈·나눗셈 버전과 **수학적으로 같다**. unsigned 곱셈의 넘침은 UB 가 아니라 모듈로 랩어라운드다 (6.2.5p9) — 넘친 상위 비트는 어차피 `& mask` 로 버린다.
2. **상수식**: `*`, `/`, `&`, `~`, 캐스트만 쓰므로 인자가 상수면 결과도 정수 상수식(integer constant expression) → `_Static_assert`, `case` 라벨, 전역 초기화에 그대로 쓸 수 있다. `__builtin_ctz` 도 clang/GCC 에서 상수 인자면 접히지만, 표준 C 만으로 되는 쪽이 이식성이 좋다. Zephyr 의 `FIELD_GET`/`FIELD_PREP` 가 정확히 이 곱셈·나눗셈 방식이고, Linux 는 `__bf_shf(x) = __builtin_ffsll(x) - 1` 로 shift 를 구한다.
3. **비용 0**: 실제로 컴파일해 봤다 (`demo_codegen.c`).

<!-- code: demo_codegen.c -->

```text
$ cc -std=c11 -O2 -S -o - demo_codegen.c                 # Apple arm64 (지시어 줄 생략)
_get_presc:
	ubfx	w0, w0, #4, #4          ; unsigned bit-field extract: bit 4 부터 4비트
	ret
_set_presc:
	bfi	w0, w1, #4, #4          ; bit-field insert: w1 의 하위 4비트를 bit 4 자리에
	ret
_get_any:                               ; 마스크가 런타임 값
	and	w8, w1, w0
	rbit	w9, w1
	clz	w9, w9                  ; rbit+clz = ctz  →  나눗셈이 아니라 시프트로 바뀜
	lsr	w0, w8, w9
	ret

$ clang -std=c11 -O2 -S -o - --target=thumbv7em-none-eabi -mcpu=cortex-m4 -ffreestanding demo_codegen.c
get_presc:  ubfx r0, r0, #4, #4 / bx lr
set_presc:  bfi  r0, r1, #4, #4 / bx lr
get_any:    ands r0, r1 / rbit r1, r1 / clz r1, r1 / lsrs r0, r1 / bx lr
```

상수 마스크는 **명령 1개** (ARMv7-M 의 `UBFX`/`BFI`). 놀랍게도 clang 은 런타임 마스크의 `/ (m & -m)` 조차 "2의 거듭제곱으로 나누기" 임을 알아보고 `udiv` 대신 `rbit+clz+lsr` 을 냈다. 다만 `CLZ` 가 없는 Cortex-M0(ARMv6-M)로 같은 파일을 컴파일하면 `get_any` 가 de Bruijn 곱셈 + 테이블 조회로 커진다 (직접 확인: `--target=thumbv6m-none-eabi -mcpu=cortex-m0`). 결론: **마스크는 상수로** 쓰는 게 원칙이고, Linux 는 아예 `__builtin_constant_p(mask)` 가 아니면 빌드를 깨뜨린다. 핀 번호처럼 런타임 인덱스가 필요하면 `_Pos` 로 직접 시프트한다 (추가 연습 5). 이 결과는 clang 21 에서 본 것이고, GCC 도 상수 마스크는 같은 명령으로 접을 것으로 기대하지만 여기선 확인하지 않았다.

### Part B — 연속 마스크 판별과 컴파일타임 검사

```svg
<svg viewBox="0 0 740 245" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">FIELD_MASK_OK: (mask + LSB) &amp; mask == 0 ?</text>
  <rect x="10" y="38" width="350" height="128" rx="10" class="f-ok" stroke="currentColor"/>
  <rect x="380" y="38" width="350" height="128" rx="10" class="f-warn" stroke="currentColor"/>
  <g font-family="monospace" fill="currentColor" xml:space="preserve">
    <text x="25" y="64">mask      0111 0000 (0x70)</text>
    <text x="25" y="86">+ LSB     0001 0000</text>
    <text x="25" y="108">= sum     1000 0000  ← 받아올림</text>
    <text x="25" y="130">sum &amp; mask 0000 0000 ✓ 연속</text>
    <text x="395" y="64">mask      0101 0000 (0x50)</text>
    <text x="395" y="86">+ LSB     0001 0000</text>
    <text x="395" y="108">= sum     0110 0000</text>
    <text x="395" y="130">sum &amp; mask 0100 0000 ✗ 구멍</text>
  </g>
  <text x="25" y="155" fill="currentColor" font-size="12">덩어리가 통째로 위로 밀려 사라진다</text>
  <text x="395" y="155" font-size="12" fill="currentColor">받아올림이 구멍(bit 5)에서 멈춰 bit 6 이 남는다</text>
  <text x="10" y="192" fill="currentColor">0xF0000000 처럼 bit 31 에 닿는 마스크는 합이 2^32 로 넘쳐 0 이 된다.</text>
  <text x="10" y="213" fill="currentColor">unsigned 랩어라운드(정의된 동작)라 판정도 맞다.</text>
  <text x="10" y="236" fill="currentColor">Linux 는 같은 사실을 "mask + LSB 가 2의 거듭제곱인가" 로 검사한다 (__BUILD_BUG_ON_NOT_POWER_OF_2).</text>
</svg>
```

- `FIELD_MAX(m) = m / LSB` — `0x70 / 0x10 = 7`. `FIELD_FIT(m, v) = v <= FIELD_MAX(m)`. 음수를 넣으면 `(uint32_t)` 캐스트로 거대한 수가 되어 자연히 거부된다.
- `FIELD_MASK_OK(0)` 은 `m != 0 &&` 에서 바로 거짓. 마스크 0 은 `FIELD_LSB` 도 0 이라 `FIELD_GET` 이 **0으로 나누기 = UB** 가 되므로 반드시 막아야 한다 (상수면 clang 이 `-Wdivision-by-zero` 로 경고).

**`FIELD_PREP_CONST` — 식 안의 `_Static_assert`**

```c
#define FIELD_PREP_CONST(mask, val) ((uint32_t)(FIELD_PREP(mask, val) + 0u * sizeof(struct { \
    _Static_assert(FIELD_MASK_OK(mask), "...mask is zero or not contiguous");                \
    _Static_assert(!FIELD_MASK_OK(mask) || FIELD_FIT(mask, val), "...value too large");      \
    char dummy; })))
```

| 조각 | 이유 |
|---|---|
| `struct { _Static_assert(...); ... }` | C11 6.7.2.1p1 문법: struct-declaration 자리에 static_assert-declaration 허용 |
| `char dummy;` | 이름 있는 멤버가 없는 struct 는 UB (C11 6.7.2.1p8). 빈 struct 는 GNU 확장일 뿐 |
| `sizeof(...)` | 타입을 식으로 바꾸는 통로. C 는 sizeof 안에서 타입 정의를 허용 (**C++ 는 금지** — 헤더를 C++ 와 공유하면 못 씀) |
| `0u *` + 바깥 `(uint32_t)` | 값은 안 바뀌고, `size_t` 로 커진 타입을 다시 32비트로 |
| `!OK \|\| FIT` | 마스크가 0이면 `FIELD_MAX` 가 0으로 나누기 → 상수식이 아니게 됨. 첫 assert 만 깔끔히 뜨게 단락 평가 |

Linux 의 `FIELD_PREP_CONST` 도 이름과 목적이 같고, 고전적인 `BUILD_BUG_ON_ZERO(e)` 는 음수 폭 비트필드 `struct { int:(-!!(e)); }` 로 같은 일을 한다. 일부러 틀려서 에러를 확인했다 (`demo_static_assert.c`, 출력은 앞부분만):

<!-- code: demo_static_assert.c -->

```text
$ cc -std=c11 -c -DBAD_MASK demo_static_assert.c
demo_static_assert.c:27:21: error: static assertion failed due to requirement '(((unsigned int)(208U) + ...
      ...: FIELD_PREP_CONST: mask is zero or not contiguous
demo_static_assert.c:27:21: note: expression evaluates to '192 == 0'
$ cc -std=c11 -c -DBAD_VAL demo_static_assert.c
demo_static_assert.c:30:20: error: static assertion failed due to requirement '!((unsigned int)(14U) != 0U && ...
      ...: FIELD_PREP_CONST: value too large for field
$ cc -std=c11 -c -DBAD_OVERLAP demo_static_assert.c
demo_static_assert.c:34:16: error: static assertion failed due to requirement '(unsigned long long)14U + 240U + 16U == (14U | 240U | 16U)': CTRL fields overlap
demo_static_assert.c:34:74: note: expression evaluates to '270 == 254'
```

`0xD0 = 1101_0000` 은 `0xD0 + 0x10 = 0xE0`, `0xE0 & 0xD0 = 0xC0 = 192` 라서 실패. 런타임 테스트가 아니라 **빌드가 깨진다** 는 게 핵심이다 — 레지스터 헤더를 손으로 옮겨 적다 낸 오타가 보드에 올라가기 전에 잡힌다.

**레지스터 맵 전체 검사 (`STATIC_CHECKS`)**

- `TIM_CTRL_MODE_Msk == GENMASK(3, 1)` — CMSIS 식 `0x7u << 1` 과 데이터시트 식 "bits 3:1" 이 서로 맞는지.
- **겹침 없음**: 필드 마스크들의 *합* 과 *OR* 이 같다. 두 마스크가 비트 하나라도 공유하면 합에서 그 비트가 두 번 더해져 OR 보다 커진다. 6개를 더하면 32비트를 넘을 수 있으므로 `(uint64_t)` 로 더한다.
- **빈틈 없음**: OR == `0xFFFFFFFF`. 두 검사를 **둘 다** 해야 한다 — 합만 `0xFFFFFFFF` 과 비교하면 `{0x1, 0x1, 0xFFFFFFFD}` 처럼 bit 0 이 겹치고 bit 1 이 비었는데 합이 우연히 `0xFFFFFFFF` 가 되는 경우를 놓친다.

### Part C — 드라이버: 검사 → 읽기 1번 → 쓰기 1번

| 단계 | `tim_configure(t, PWM, 9, 0x40, true)`, 시작 `CTRL = 0xFF00FE00` |
|---|---|
| ① 인자 검사 | mode 2 ≤ 3, `FIELD_FIT(PRESC, 9)`, `FIELD_FIT(DTIME, 0x40)` 통과. 하나라도 실패면 `return false` — **레지스터를 읽지도 쓰지도 않는다** |
| ② 읽기 + clear | `v = CTRL & ~(MODE\|PRESC\|DTIME\|IRQEN)` = `0xFF00FE00` (예약 비트 그대로) |
| ③ 4개 PREP OR | `0x4 \| 0x90 \| 0x00400000 \| 0x100` |
| ④ 쓰기 1번 | `CTRL = 0xFF40FF94` |

- **왜 쓰기 1번?** `tim_set_mode` 후 `tim_set_presc` 를 따로 부르면 그 사이에 "새 MODE + 옛 PRESC" 조합이 하드웨어에 실제로 걸린다. 타이머가 돌고 있으면 PWM 한 주기가 잘못된 분주로 나간다. 또 `volatile` 접근 2번 = 버스 트랜잭션 2번이다 (추가 연습 4).
- **잘라내기 vs 거부**: `FIELD_PREP(PRESC, 16)` 은 조용히 0 이 된다 (최고 분주 → 분주 없음, 16배 빠른 타이머!). 매크로는 잘라내는 게 맞지만, **API 경계(드라이버 함수)에서는 거부** 해야 버그가 드러난다.
- **예약 비트**: 데이터시트의 "Reserved, must be kept at reset value" 는 RMW 로 읽은 값을 그대로 쓰면 지켜진다. `CTRL = FIELD_PREP(...)` 처럼 `=` 로 쓰면 다른 필드와 예약 비트까지 0 으로 날아간다.
- **표 테스트의 판정식**: `FIELD_GET(m, n) == val` (그 필드는 원하는 값) **그리고** `((n ^ start) & ~m) == 0` (XOR 로 바뀐 비트를 구해, 마스크 밖에 하나도 없음). 시작값을 `0`, `0xFFFFFFFF`, `0xA5A5A5A5`, `0x5A5A5A5A`, `0x12345678` 로 돌려 "원래 0이던 비트 / 1이던 비트" 를 모두 덮는다.
- **원자성은 별개**: `FIELD_REPLACE` 를 `volatile` 레지스터에 쓰는 것도 RMW 3단계다. ISR 이 같은 레지스터를 만지면 C-B01 Part C 의 lost update 가 그대로 생긴다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 47/47 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o san && ./san   # 47/47, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `GENMASK(h,l)` 를 `((1u << (h+1)) - 1) & ~((1u << l) - 1)` | h = 31 이면 `1u << 32` → UB (C11 6.5.7p3) | 양쪽에서 깎기: `(~0u << l) & (~0u >> (31-h))` |
| `GENMASK(0, 3)` (h, l 뒤바뀜) | 조용히 0 이 된다 → 이후 `FIELD_GET` 이 0으로 나누기 | `_Static_assert(GENMASK(...) == ..)` 로 맵 고정. Linux 는 `GENMASK_INPUT_CHECK` |
| `FIELD_GET(reg, MASK)` (인자 순서) | 두 인자 모두 `uint32_t` 라 컴파일 통과, 값은 엉망 | Linux 순서 그대로 **mask 먼저**. 표 테스트로 잡는다 |
| `reg = FIELD_PREP(M, v);` | `=` 라서 다른 필드·예약 비트를 0 으로 덮음 | `reg = FIELD_REPLACE(M, reg, v);` |
| `(v << s)` 후 `& mask` 생략 | 넘친 값이 이웃 필드를 오염 (`PRESC=16` → bit 8 IRQEN 켜짐) | `FIELD_PREP` 은 항상 `& mask` |
| `#define X_Msk (7 << 29)` | `7` 은 int → `7<<29` 가 int 범위를 넘음 = UB | `(0x7u << 29)` — CMSIS 가 `UL`/`U` 접미사를 붙이는 이유 |
| 런타임 마스크로 `FIELD_GET(m, r)` 에 m = 0 | `/ 0` → UB | 상수 마스크만 쓰거나 `FIELD_MASK_OK` 로 먼저 검사 |
| `FIELD_REPLACE(masks[i++], r, v)` | 매크로가 mask 를 여러 번 평가 → `i` 가 여러 번 증가 (C-B15) | 부수효과 없는 인자만, 또는 inline 함수 |
| 두 필드를 따로 `tim_set_*` | 중간 상태가 하드웨어에 노출 + 쓰기 2번 | 한 번에 clear/set 해서 쓰기 1번 (`tim_configure`) |

## 8. 면접 Q&A

<details>
<summary>Q1. GENMASK(h, l) 를 써보세요. h = 31 에서도 안전한가요?</summary>

`((~0u) << l) & ((~0u) >> (31 - h))`. 두 시프트 양이 모두 0..31 이라 UB 가 없다. 흔한 오답 `(1u << (h+1)) - 1` 은 h = 31 에서 시프트 양 32 → UB (x86 은 시프트 양을 mod 32 로 잘라 `1u<<32` 가 1 처럼 동작해 마스크가 0 이 되지만, 상수 접기·최적화 수준에 따라 결과가 또 달라질 수 있다 — UB 라서 어떤 값도 믿을 수 없다). 꼬리질문 "64비트는?" → `~0ull` 과 `63 - h` (추가 연습 3).

</details>

<details>
<summary>Q2. FIELD_GET 에 shift 인자가 없는데 shift 는 어디서 오나요? 런타임 비용은?</summary>

`mask & -mask` 가 가장 낮은 1비트 = `1 << shift`. 그 값으로 나누면 오른쪽 시프트, 곱하면 왼쪽 시프트와 같다 (unsigned, C11 6.5.7). 또는 `__builtin_ctz(mask)` (Linux 는 `__builtin_ffsll(x) - 1`). 마스크가 상수면 컴파일러가 접어서 Cortex-M3/M4 에선 `UBFX`/`BFI` 한 명령, M0 에선 시프트 2개 정도가 된다. 마스크가 런타임 값이면 ctz 계산이 남는다 (M3+ 는 `RBIT+CLZ`, M0 는 테이블) — 그래서 커널은 마스크가 상수가 아니면 빌드 에러를 낸다.

</details>

<details>
<summary>Q3. 마스크가 연속인지 컴파일타임에 어떻게 검사하나요? 필드 겹침은?</summary>

연속이면 `mask + lsb(mask)` 가 덩어리를 받아올림으로 날려 `& mask == 0` (= 합이 2의 거듭제곱 또는 0). 검사를 `_Static_assert` 에 넣는다. 식 안에서 써야 하면 `sizeof(struct { _Static_assert(...); char c; })` 트릭. 겹침은 "마스크들의 합(64비트) == OR", 빈틈은 "OR == 전체" 두 개를 같이 본다. 커널 쪽 이름: `BUILD_BUG_ON`, `BUILD_BUG_ON_ZERO`, `__BF_FIELD_CHECK`.

</details>

<details>
<summary>Q4. C 비트필드 구조체(`struct { uint32_t en:1, mode:3, presc:4; }`)로 레지스터를 표현하면 안 되나요?</summary>

비트 배치 순서·저장 단위·`uint32_t` 비트필드 허용 여부가 구현 정의(implementation-defined, C11 6.7.2.1p11)라 표준 C 만으로는 보장이 없다 (ARM 은 AAPCS 가 레이아웃을 정해 주긴 한다). `volatile` 비트필드 접근을 몇 바이트 폭으로 하는지도 컴파일러 옵션에 따라 다를 수 있는데 (GCC `-fstrict-volatile-bitfields`), 주변장치 레지스터는 **32비트 접근만** 허용하는 경우가 많다. 그리고 필드 2개를 쓰면 RMW 2번이 된다. 그래서 CMSIS/벤더 헤더는 `_Pos`/`_Msk` + 마스크 연산을 쓴다 (C-B01 Q4 도 참고).

</details>

<details>
<summary>Q5. 값이 필드보다 크면 잘라야 하나요, 거부해야 하나요?</summary>

계층마다 다르다. 저수준 매크로(`FIELD_PREP`)는 `& mask` 로 잘라서 이웃 필드 오염만 막는다. Linux 는 값이 상수면 넘침을 **빌드 에러** 로 만든다. 런타임 값을 받는 드라이버 API 는 `FIELD_FIT` 으로 검사해 에러를 돌려줘야 한다 — 분주 16 이 조용히 0 이 되면 타이머가 16배 빨라지는 버그를 디버깅하게 된다.

</details>

<details>
<summary>Q6. 같은 레지스터의 필드 두 개를 바꿔야 합니다. 쓰기를 몇 번 하나요? 꼬리: 예약 비트는요?</summary>

한 번. 읽고 → 두 필드 clear → 두 PREP OR → 한 번 쓰기. 따로 쓰면 중간 조합이 하드웨어에 걸리고, 어떤 주변장치는 동작 중(EN=1) 설정 필드 변경을 아예 금지한다 (그럴 땐 EN 끄기 → 설정 → EN 켜기 순서). 예약 비트는 데이터시트가 "reset value 유지" 라고 하면 RMW 로 읽은 값을 그대로 돌려 쓰는 게 안전하다. STM32 헤더의 `MODIFY_REG(REG, CLEARMASK, SETMASK)` 가 정확히 이 패턴이다.

</details>

## 9. 추가 연습

### 연습 1 — 내장 함수 없이 상수식 FIELD_SHIFT

`FIELD_SHIFT(mask)` 를 `__builtin_ctz` 없이, `_Static_assert` 에 쓸 수 있는 상수식으로 만들어라.

<details>
<summary>답 보기</summary>

`LSB - 1` 은 shift 개수만큼의 1 (`0x10 - 1 = 0xF`) → 1의 개수 = shift. 1 개수는 SWAR popcount 3단계 매크로 (`0x5555…`, `0x3333…`, `0x0F0F…` 후 `* 0x01010101 >> 24`). 매크로 인자가 여러 번 복사되어 전개가 길어지지만 상수라 컴파일러가 다 접는다.

</details>

### 연습 2 — 부호 있는 필드 꺼내기

bits 27:16 의 12비트 2의 보수 값을 `int32_t` 로: `0x7FF → 2047`, `0x800 → -2048`, `0xFFF → -1`.

<details>
<summary>답 보기</summary>

`sign = (FIELD_MAX(m) >> 1) + 1` (필드 최상위 비트 값). `(int32_t)(u ^ sign) - (int32_t)sign` — 부호 비트를 뒤집어 0..2^w−1 로 옮긴 뒤 빼서 −2^(w−1)..2^(w−1)−1 로 되돌린다. 폭 ≤ 31 이면 모든 중간값이 int32 범위라 구현 정의 변환이 없다. 흔한 대안 `(int32_t)(u << (32-w)) >> (32-w)` 는 범위 밖 unsigned→signed 변환(6.3.1.3p3)과 음수 오른쪽 시프트(6.5.7p5)가 둘 다 **구현 정의** 다 (gcc/clang 은 기대대로 동작하지만 표준 보장은 아님).

</details>

### 연습 3 — 64비트 레지스터 (AArch64 MPIDR_EL1)

`GENMASK_ULL`, `FIELD_GET64`, `FIELD_PREP64` 를 만들고 Aff0[7:0], Aff1[15:8], Aff3[39:32] 를 꺼내라.

<details>
<summary>답 보기</summary>

`~0ull` 과 `63 - h` 로 바꾸고 캐스트를 `uint64_t` 로. 32비트 버전을 그대로 쓰면 `0xFFFFFFFFu << 32` 가 UB 이고 Aff3 는 영영 못 꺼낸다. Linux 도 `GENMASK_ULL` 이 따로 있다 (최근 커널엔 `GENMASK_U8/U16/U32/U64` 도 생겼다).

</details>

### 연습 4 — 쓰기 1번 vs 2번 관찰

쓰기마다 기록을 남기는 가짜 레지스터로, 필드 2개를 따로 RMW 하면 첫 쓰기에서 "새 MODE + 옛 PRESC" 가 하드웨어에 보임을 증명하고, 한 번에 쓰는 버전은 쓰기 1번에 같은 최종값임을 보여라.

<details>
<summary>답 보기</summary>

`reg_write()` 가 `wlog[]` 에 값을 남긴다. 두 번 쓰기 버전의 `wlog[0]` 은 MODE=2, PRESC=0 (중간 상태). 한 번 쓰기 버전: `v = read() & ~(M1|M2); write(v | PREP(M1,a) | PREP(M2,b));` → `wcount == 1`.

</details>

### 연습 5 — 런타임 인덱스 필드 (GPIOx_MODER, 핀당 2비트)

`moder_set(moder, pin, mode)` 를 CMSIS `GPIO_MODER_MODE0_Pos/_Msk` 로 쓰고, 런타임 마스크 `FIELD_REPLACE(MODE0_Msk << 2*pin, …)` 버전과 16핀 × 4모드 전부 같은지 확인하라.

<details>
<summary>답 보기</summary>

`pos = 2*pin; (moder & ~(0x3u << pos)) | ((mode & 3u) << pos)`. shift 를 이미 아는 경우엔 마스크에서 다시 유도할 이유가 없다 — 마스크 전용 매크로는 **상수 필드** 용, 배열형 필드는 `_Pos` 기반이 더 단순하고 빠르다. 결과는 C-B01 `gpio_set_mode` 와 같다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 15/15 passed
```

## 10. 핵심 정리

- `GENMASK(h,l) = (~0u << l) & (~0u >> (31-h))` — 양쪽에서 깎으면 h = 31 도 UB 없음.
- 마스크 하나로 끝: `lsb = m & -m` → `GET = (r & m) / lsb`, `PREP = (v * lsb) & m`, `REPLACE = (r & ~m) | PREP`.
- 상수 마스크면 비용 0 (Cortex-M4 `UBFX`/`BFI` 한 명령). 마스크는 상수로, 런타임 인덱스는 `_Pos` 시프트로.
- 연속 검사 `((m + lsb) & m) == 0`, 겹침 검사 `Σmask(64비트) == OR`, 빈틈 검사 `OR == 전체`.
- `_Static_assert` 를 식에 넣으려면 `sizeof(struct { _Static_assert(...); char c; })` (C 전용).
- 매크로는 잘라내고, 드라이버 API 는 `FIELD_FIT` 으로 거부한다.
- 여러 필드는 읽기 1번·쓰기 1번. 예약 비트는 RMW 로 보존. RMW 는 여전히 원자적이지 않다 (C-B01).

## 참고자료

- [Linux `include/linux/bitfield.h` — FIELD_GET / FIELD_PREP / FIELD_PREP_CONST / FIELD_FIT](https://github.com/torvalds/linux/blob/master/include/linux/bitfield.h)
- [Linux `include/linux/bits.h` — GENMASK / GENMASK_ULL](https://github.com/torvalds/linux/blob/master/include/linux/bits.h)
- [Zephyr `include/zephyr/sys/util.h` — LSB_GET / FIELD_GET / FIELD_PREP (곱셈·나눗셈 방식)](https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/sys/util.h)
- [ARM CMSIS-Core — `_Pos`/`_Msk` 규칙, `_VAL2FLD` / `_FLD2VAL`](https://arm-software.github.io/CMSIS_6/latest/Core/index.html)
- [ARMv7-M Architecture Reference Manual — UBFX, BFI, RBIT, CLZ](https://developer.arm.com/documentation/ddi0403/latest/)
- C11 표준 6.5.7 (시프트), 6.2.5p9 (unsigned 랩어라운드), 6.7.2.1 (struct 안 static_assert, 비트필드 구현 정의), 6.7.10 (static assertions)
