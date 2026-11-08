---
id: C-M03
time: 80분
updated: 2026-11-07
---
# C-M03 비트필드 vs 마스크/시프트: 이식성 분석

## 학습 목표

- 같은 32비트 레지스터를 **C 비트필드 구조체**와 **CMSIS 스타일 `_Pos`/`_Msk` 마스크**로 각각 정의하고, 두 정의가 이 호스트에서 같은 비트를 만드는지 표 테스트와 퍼즈 테스트로 증명한다.
- C11 6.7.2.1 이 **구현 정의(implementation-defined)** 로 남긴 것 — 할당 순서, 저장 단위 경계 넘김(straddling), plain `int` 비트필드의 부호, 허용 타입 — 과 **unspecified/UB/제약 위반** 을 정확히 구분해 말한다.
- 같은 소스를 호스트(arm64), Cortex-M4 little-endian, Cortex-M4 **big-endian**, MSVC ABI 로 크로스 컴파일하고 오브젝트 바이트를 직접 비교한다. 비트 순서가 뒤집히고 구조체 크기가 바뀌는 걸 본다.
- volatile 비트필드 쓰기가 만드는 실제 어셈블리(`ldr/bfi/str` RMW, 필드마다 반복, 타깃마다 다른 접근 폭)를 읽고, "레지스터는 마스크로, 비트필드는 어디까지" 라는 **사용 기준** 을 세운다.

## 1. 문제

"레지스터를 비트필드 구조체로 정의하면 안 되나요?" 는 C-B04 다음에 꼭 나오는 꼬리질문이다. 정답은 "이 컴파일러·이 ABI 에서는 동작하지만, **표준이 보장하지 않는 것** 에 기대고 있고, 접근 횟수·폭을 소스가 통제하지 못한다" 이다. 이 레슨은 그 말을 바이트와 어셈블리로 증명한다.

C 비트필드(bit-field)는 읽기 좋다: `ADC1->CFG_b.ch = 5;`. 하지만 그 `ch` 가 레지스터의 몇 번 비트에 놓이는지는 **컴파일러(ABI)가 정한다** (C11 6.7.2.1p11). 반면 `(v & ~ADC_CFG_CH_Msk) | (5u << ADC_CFG_CH_Pos)` 는 위치를 **소스가 정한다** — 정수 연산이라 어떤 컴파일러에서도 같은 값이다. 이번 문제는 아래 가상의 ADC 설정 레지스터 하나를 두 방식으로 정의하고 비교한다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">ADC1-&gt;CFG (offset 0x04) — 같은 레지스터, 두 가지 정의</text>
  <g fill="currentColor" font-size="12" text-anchor="middle" opacity=".75">
    <text x="50" y="44">31</text><text x="197" y="44">24</text><text x="365" y="44">16</text><text x="533" y="44">8</text><text x="701" y="44">0</text>
  </g>
  <g stroke="currentColor">
    <rect x="40" y="52" width="42" height="34" class="f-mut"/>
    <rect x="82" y="52" width="21" height="34" class="f-warn"/>
    <rect x="103" y="52" width="105" height="34" class="f-acc"/>
    <rect x="208" y="52" width="84" height="34" class="f-mut"/>
    <rect x="292" y="52" width="147" height="34" class="f-ok"/>
    <rect x="439" y="52" width="168" height="34" class="f-acc"/>
    <rect x="607" y="52" width="42" height="34" class="f-ok"/>
    <rect x="649" y="52" width="42" height="34" class="f-acc"/>
    <rect x="691" y="52" width="21" height="34" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="61" y="74">rsvd</text><text x="155" y="74">CH 28:24</text><text x="250" y="74">rsvd</text>
    <text x="365" y="74">OFFSET 19:13 (signed)</text><text x="523" y="74">SMPT 12:5</text>
    <text x="628" y="74">RES</text><text x="670" y="74">MODE</text>
    <text x="92" y="104">START</text><text x="701" y="104">EN</text>
  </g>
  <g stroke="currentColor" stroke-dasharray="4 3">
    <line x1="208" y1="48" x2="208" y2="118"/><line x1="376" y1="48" x2="376" y2="118"/><line x1="544" y1="48" x2="544" y2="118"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle" opacity=".75">
    <text x="124" y="118">byte 3</text><text x="292" y="118">byte 2</text><text x="460" y="118">byte 1</text><text x="628" y="118">byte 0</text>
  </g>
  <rect x="10" y="132" width="355" height="128" rx="8" class="f-warn" stroke="currentColor"/>
  <rect x="375" y="132" width="355" height="128" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="22" y="152" fill="currentColor" font-weight="bold">(1) 비트필드 — 위치는 ABI 가 정한다</text>
  <text x="387" y="152" fill="currentColor" font-weight="bold">(2) 마스크 — 위치는 소스가 정한다</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="22" y="176">uint32_t   en     : 1;</text>
    <text x="22" y="194">uint32_t   smpt   : 8;  /* 바이트 경계! */</text>
    <text x="22" y="212">signed int offset : 7;</text>
    <text x="22" y="230">uint32_t   ch     : 5;</text>
    <text x="22" y="250">ADC1_BF-&gt;CFG.b.ch = 5;</text>
    <text x="387" y="176">#define ADC_CFG_CH_Pos 24u</text>
    <text x="387" y="194">#define ADC_CFG_CH_Msk (0x1Fu &lt;&lt; 24)</text>
    <text x="387" y="212">v = ADC1-&gt;CFG;</text>
    <text x="387" y="230">v = (v &amp; ~CH_Msk) | (5u &lt;&lt; CH_Pos);</text>
    <text x="387" y="250">ADC1-&gt;CFG = v;</text>
  </g>
  <text x="10" y="284" fill="currentColor">SMPT 는 byte 0/1, OFFSET 은 byte 1/2 를 걸친다 (바이트 경계 straddle).</text>
  <text x="10" y="306" fill="currentColor">예약 비트는 읽은 값 그대로 다시 쓴다. START 는 1을 쓰면 변환 시작, 읽으면 0.</text>
</svg>
```

모든 테스트는 호스트에서 돌고, 크로스 타깃 관찰은 컴파일만 하는 데모 파일로 한다 (이 맥엔 arm-none-eabi-gcc 가 없지만 `clang --target=...` 은 오브젝트까지 만든다). 마스크 매크로 설계 자체(GENMASK, FIELD_PREP)는 C-B04, volatile 레지스터 구조체 오버레이는 C-M01, W1C 비트가 섞인 RMW 위험은 C-M02, 구조체 패딩은 C-B12 에서 다룬다. 여기서는 **비트필드의 레이아웃·코드 생성·이식성** 에 집중한다.

### Part A — 같은 레지스터, 두 정의로 pack / unpack

- `cfg_pack_mask(c)` — `adc_cfg_t`(필드 값 묶음)를 `_Pos`/`_Msk` 로 32비트 값으로. 필드보다 큰 값은 잘라서 이웃 필드를 침범하지 않는다. `offset` 은 음수일 수 있다.
- `cfg_pack_bf(c)` — 같은 일을 비트필드 구조체 `adc_cfg_bits_t` 에 대입한 뒤 4바이트를 `uint32_t` 로 복사해서.
- `cfg_unpack_bf(raw, c)` — 비트필드로 풀기 (`signed int offset:7` 이 부호 확장을 해 준다).
- 테스트: 고정 벡터 + 고정 시드 xorshift32 2000개 무작위 설정에서 두 pack 결과가 같고 왕복(round-trip)이 되는지.

### Part B — 구현 정의 지점을 코드로 다루기

- `sext(v, width)` — 하위 `width`(1..32)비트를 2의 보수로 부호 확장. **구현 정의 동작 없이** (음수의 `>>`, 범위 밖 unsigned→signed 변환 금지). `cfg_unpack_mask` 는 이걸로 OFFSET 을 푼다.
- `bf_probe_mask(field)` — 0으로 채운 비트필드 구조체에서 필드 하나만 전부 1로 만들고 워드로 읽어, **이 ABI 에서 그 필드가 차지하는 마스크** 를 알아낸다. 데이터시트 `_Msk` 와 같은지 확인 (= 부팅 시 self-test 로도 쓸 수 있는 함수).
- `abi_place(w, n, unit_bits, off)` — 같은 선언 타입의 비트필드 n개가 놓이는 비트 오프셋과 구조체 크기를 **SysV/AAPCS 규칙으로 예측** 한다. 테스트는 호스트 컴파일러가 실제로 배치한 결과(탐침)와 대조한다: `unsigned a:30, b:4`, `uint8_t a:6, b:4`, `unsigned a:6, b:4`, `:0`, `uint16_t` 10비트 ×3.

### Part C — 드라이버: 레지스터 접근은 마스크로

모든 레지스터 접근은 `reg_read()`/`reg_write()` 를 거쳐 trace 문자열을 남긴다 (C-B09 와 같은 방식). 하드웨어 모델: CFG 에 START=1 이 쓰이는 **그 순간의 CH** 로 변환이 시작되고, START 는 다시 0으로 읽힌다. 예약 비트는 1로 읽힐 수 있다 (리셋 값 `0x40500000`).

- `adc_configure(a, c)` — 범위 검사(MODE 3 은 예약) 후 **읽기 1번 + 쓰기 1번**. 예약 비트 보존, START 는 항상 0. 하나라도 틀리면 접근 0회로 `false`.
- `adc_start(a, ch)` — CH 와 START 를 **같은 한 번의 쓰기** 에 담는다.
- `adc_read_offset(a)` — 읽기 1번, 부호 있는 보정값.
- 주어진 `adc_start_fieldwise()` 는 `CFG.b.ch = ch; CFG.b.start = 1;` 이 컴파일되는 모양(필드마다 RMW)을 흉내 낸다. 테스트가 버스 접근 4번을 보여 준다.

## 2. Problem (English)

> **Bit-fields vs. mask/shift for a memory-mapped register.**
>
> `ADC_CFG` is a 32-bit register: `EN[0]`, `MODE[2:1]` (3 is reserved), `RES[4:3]`, `SMPT[12:5]`, `OFFSET[19:13]` (signed two's complement, −64..63), reserved `[23:20]`, `CH[28:24]` (0..18), `START[29]` (write-1-to-start, reads 0), reserved `[31:30]`.
>
> 1. Given both a bit-field struct and CMSIS-style `_Pos`/`_Msk` macros for it, implement `uint32_t cfg_pack_mask(const adc_cfg_t *)`, `uint32_t cfg_pack_bf(const adc_cfg_t *)`, `void cfg_unpack_bf(uint32_t, adc_cfg_t *)`, and show they agree on this host.
> 2. Implement `int32_t sext(uint32_t v, unsigned width)` for `1 <= width <= 32` without relying on implementation-defined behavior, and `cfg_unpack_mask`. Write `bf_probe_mask(field)` that discovers where the compiler put each bit-field, and `abi_place()` that predicts bit offsets and `sizeof` for a run of same-typed bit-fields under the SysV/AAPCS rules.
> 3. Write a driver: `bool adc_configure(ADC_TypeDef *, const adc_cfg_t *)` (validate, then exactly one read and one write, preserving reserved bits) and `bool adc_start(ADC_TypeDef *, unsigned ch)` (CH and START in a single write).
>
> Then explain: which aspects of bit-field layout does C11 leave implementation-defined? What does `ADC1->CFG.b.ch = ch; ADC1->CFG.b.start = 1;` compile to on a Cortex-M4, and why do CMSIS device headers use masks instead?
>
> Example: `{en=1, mode=2, res=1, smpt=0x5A, offset=-3, ch=7}` packs to `0x070FAB4D`; `sext(0x40, 7) == -64`; `sext(0x80000000, 32) == INT32_MIN`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 줄이 나오고 "9/48 passed" — TODO 10곳을 채워 48/48 을 만든다
```

관찰용 파일 세 개가 같은 폴더에 있다 (하네스와 별개):

```bash
# ① 타깃별 레이아웃 — 컴파일만 (main 없음, <stdint.h> 만 include)
clang -std=c11 -O2 -c --target=thumbv7em-none-eabi   -mcpu=cortex-m4 demo_layout.c -o le.o
clang -std=c11 -O2 -c --target=thumbebv7em-none-eabi -mcpu=cortex-m4 demo_layout.c -o be.o
xcrun llvm-objdump -s -j .rodata le.o be.o
# ② 비트필드 쓰기의 asm (RMW, 접근 폭)
clang -std=c11 -O2 -S --target=thumbv7em-none-eabi -mcpu=cortex-m4 demo_rmw.c -o -
# ③ plain int 비트필드의 부호 — 호스트에서 실행
cc -std=c11 -Wall -Wextra -Werror -O2 demo_plain_int.c -o demo_plain_int && ./demo_plain_int
```

## 4. 힌트

<details>
<summary>힌트 1 — Part A: 비트필드 ↔ uint32_t 는 memcpy 로</summary>

`*(uint32_t *)&b` 같은 포인터 캐스트는 엄격한 별칭 규칙(strict aliasing) 위반이다 (C-M13). `memcpy(&raw, &b, 4)` 는 정의된 동작이고 `-O2` 에서 그냥 레지스터 이동이 된다. 대입 전에 `memset(&b, 0, sizeof b)` — `= {0}` 은 **이름 없는** 필드(예약)를 초기화하지 않는다 (C11 6.7.9p9).

</details>

<details>
<summary>힌트 2 — Part A: 음수 offset 을 마스크로 넣기</summary>

`(uint32_t)c->offset` 은 음수를 2^32 로 나눈 나머지로 바꾼다 — 구현 정의가 아니라 **정의된** 변환이다 (6.3.1.3p2). 예: −3 → `0xFFFFFFFD`. 시프트 후 `& ADC_CFG_OFFSET_Msk` 로 7비트만 남기면 끝.

</details>

<details>
<summary>힌트 3 — Part B: 시프트 없이 부호 확장</summary>

`s = 1u << (width-1)` (부호 비트), `v &= (1u << width) - 1`. 그러면 `(int32_t)(v ^ s) - (int32_t)s`. `v ^ s` 와 `s` 는 둘 다 2^31 미만이라 `int32_t` 변환이 안전하다. 예: width 7, v = 0x40 → (0x00) − 64 = −64. width 32 는 `1u << 32` 가 UB 이므로 따로: 최상위 비트가 서 있으면 `-(int32_t)(~v) - 1`.

</details>

<details>
<summary>힌트 4 — Part B: abi_place 의 규칙</summary>

비트 커서 `pos` 를 0에서 시작한다. 각 필드에서: 폭이 0이거나 `pos % unit + w > unit`(이 단위 안에 안 들어감)이면 `pos` 를 다음 단위 시작으로 올린다. 그 `pos` 가 필드 오프셋, 그다음 `pos += w`. 크기 = 사용한 비트를 단위 배수로 올린 바이트 수.

</details>

<details>
<summary>힌트 5 — Part C: 검사 → 읽기 1번 → 계산 → 쓰기 1번</summary>

범위 검사는 레지스터를 **읽기 전에** 끝낸다 (거부 시 trace 가 비어 있어야 한다). `v = (v & ADC_CFG_RSVD_Msk) | cfg_pack_mask(&w)` — 예약 비트만 남기고 나머지는 새 값으로. `adc_start` 는 `~CH_Msk` 로 지우고 CH 와 `START_Msk` 를 같이 OR.

</details>

## 5. 풀이

### Part A — 두 정의가 같은 비트를 만든다 (이 호스트에서는)

| 필드 | 마스크 식 | 비트필드 | `mix` 값 → 비트 |
|---|---|---|---|
| EN | `(en << 0) & 0x1` | `uint32_t en:1` | 1 → `0x00000001` |
| MODE | `(mode << 1) & 0x6` | `uint32_t mode:2` | 2 → `0x00000004` |
| RES | `(res << 3) & 0x18` | `uint32_t res:2` | 1 → `0x00000008` |
| SMPT | `(smpt << 5) & 0x1FE0` | `uint32_t smpt:8` | 0x5A → `0x00000B40` |
| OFFSET | `((uint32_t)off << 13) & 0xFE000` | `signed int offset:7` | −3 = `0x7D` → `0x000FA000` |
| CH | `(ch << 24) & 0x1F000000` | `uint32_t ch:5` | 7 → `0x07000000` |
| 합 | | | **`0x070FAB4D`** |

퍼즈 2000개에서 `cfg_pack_mask == cfg_pack_bf` 와 왕복이 전부 성립한다. 하지만 이건 "**이 ABI(Apple arm64)에서** 비트필드가 LSB 부터, 선언 순서대로, 32비트 단위 안에 채워진다" 는 사실을 테스트한 것이지 C 가 보장하는 사실이 아니다. 테스트가 통과한다고 이식성이 생기지는 않는다 — 그걸 Part B 와 크로스 컴파일로 본다.

두 가지 세부:

- **memcpy**: 비트필드 구조체와 `uint32_t` 사이 변환은 `memcpy` (또는 union — C11 6.5.2.3 각주 95 가 union 을 통한 재해석을 허용). `-O2` 에서 memcpy 는 사라지고 레지스터 연산만 남는다.
- **`= {0}` 의 함정**: C11 6.7.9p9 "이름 없는 멤버는 초기화에 참여하지 않고, 초기화 후에도 값이 **indeterminate**". 비트필드 레지스터 구조체의 예약 비트(`uint32_t :4`)가 바로 이름 없는 멤버다. clang 은 실제로 0을 채우지만(아래 `.rodata` 의 `00` 바이트) 표준이 약속한 건 아니다. 그래서 `memset` 으로 시작한다.

### Part B-1 — 부호 있는 필드: 비트필드는 공짜, 마스크는 손으로

`signed int offset:7` 을 읽으면 컴파일러가 부호 확장까지 해 준다. 실제 asm (`demo_rmw.c` ⑥):

```text
Cortex-M4:  ldr r0, [r0, #4] ; sbfx r0, r0, #13, #7          ← signed bit-field extract 한 명령
arm64    :  ldr w8, [x0, #4] ; sbfx w0, w8, #13, #7
Cortex-M0+: ldr r0, [r0, #4] ; lsls r0, r0, #12 ; asrs r0, r0, #25   ← SBFX 없음 (ARMv6-M)
```

마스크 쪽은 `sext()` 를 직접 쓴다. 흔한 구현 `((int32_t)(v << (32 - w))) >> (32 - w)` 는 두 군데에서 표준 밖으로 나간다:

| 단계 | 문제 | 분류 |
|---|---|---|
| `(int32_t)(v << 25)` — 값이 2^31 이상 | 범위 밖 unsigned → signed 변환 | 구현 정의 (6.3.1.3p3) |
| 음수 `>> 25` | 음수의 오른쪽 시프트 결과 | 구현 정의 (6.5.7p5) |

clang/GCC 에선 둘 다 기대대로 동작하고 위의 `lsls/asrs` 가 바로 그것이지만, 정답 코드는 XOR-빼기 트릭으로 둘 다 피한다:

```text
width = 7, s = 0x40 (부호 비트)
v = 0x3F (63) : (0x3F ^ 0x40) − 0x40 = 0x7F − 64 = 63
v = 0x40 (−64): (0x40 ^ 0x40) − 0x40 = 0    − 64 = −64
v = 0x7D (−3) : (0x7D ^ 0x40) − 0x40 = 0x3D − 64 = −3
```

부호 비트가 0이면 XOR 이 부호 비트를 켜서 64를 더했다가 다시 빼고(그대로), 1이면 끄고 64를 빼서 음수가 된다. 두 피연산자 모두 2^w 미만이라 `int32_t` 범위 안 — width = 32 만 `1u << 32` 문제로 따로 처리한다. 테스트는 7비트 128개 전부를 비트필드 읽기 결과와 대조한다.

> **plain int 의 부호**: `int x:3` 이 signed 인지 unsigned 인지는 **구현 정의** 다 (C11 6.7.2p5, 6.7.2.1 각주 125). `demo_plain_int.c` 실제 출력 (Apple clang 21):
>
> ```text
> plain=-1  signed=-1  unsigned=7
> plain is SIGNED on this compiler
> ready=-1  (ready == 1) -> false   (ready != 0) -> true
> ```
>
> GCC 에는 이걸 바꾸는 `-funsigned-bitfields` 가 있지만, clang 은 받지 않는다 (실제 출력: `clang: warning: the clang compiler does not support '-funsigned-bitfields'`, 결과 동일). 상수를 대입하면 clang 이 잡아 준다: `implicit truncation from 'int' to bit-field changes value from 7 to -1 [-Wbitfield-constant-conversion]`, `... one-bit wide bit-field changes value from 1 to -1 [-Wsingle-bit-bitfield-constant-conversion]` (`-DSHOW_WARNING` 로 확인). 런타임 값은 경고가 없다. **규칙: 비트필드에는 항상 `signed int` 또는 `unsigned int`(/`_Bool`)를 명시한다.**

### Part B-2 — 컴파일러가 필드를 어디에 뒀는지 탐침하기

`bf_probe_mask()` 는 "필드 하나만 전부 1" 인 구조체를 워드로 읽는다. 호스트에서 7개 필드가 전부 데이터시트 `_Msk` 와 같다. 이걸 왜 `_Static_assert` 로 하지 않을까? 해 봤다:

```text
$ clang -std=c11 -fsyntax-only sa.c      # _Static_assert(((u_t){ .b = { .en = 1 } }).raw == 1u, ...)
error: static assertion expression is not an integral constant expression
note: read of member 'raw' of union with active member 'b' is not allowed in a constant expression

$ clang -std=c11 -fsyntax-only bc.c      # _Static_assert(__builtin_bit_cast(uint32_t, (b_t){ .en = 1 }) == 1u, ...)
note: constexpr bit_cast involving bit-field is not yet supported
```

`-std=c23` 의 `constexpr` 객체로 해도 같은 "active member" 에러였다. **비트필드의 비트 위치는 정수 상수식으로 꺼낼 방법이 없다** (`offsetof` 를 비트필드에 쓰면 UB — 7.19p3, `&`·`sizeof` 는 제약 위반 — 6.5.3.2p1, 6.5.3.4p1). 반면 마스크는 처음부터 상수식이라 `_Static_assert((ALL_FIELDS | RSVD) == 0xFFFFFFFFu, ...)` 로 맵 전체를 컴파일타임에 고정한다 (solution.c 상단, C-B04 Part B). 비트필드 레이아웃은 **런타임 self-test**(이 탐침)나 **오브젝트 바이트 검사**(다음 절)로만 확인할 수 있다.

### Part B-3 — 저장 단위와 straddling: 규칙 예측 vs 실제

`abi_place()` 가 예측하고 하네스가 호스트 배치를 탐침해 대조한 결과:

| 구조체 | 예측 오프셋 | 실제 (탐침) | sizeof | 관찰 |
|---|---|---|---|---|
| `unsigned a:30, b:4` | b = 32 | 32 | 8 | b 는 32비트 경계를 **넘지 않고** 다음 단위로 |
| `uint8_t a:6, b:4` | b = 8 | 8 | 2 | 선언 타입이 저장 단위(8비트)를 정한다 |
| `unsigned a:6, b:4` | b = 6 | 6 | 4 | 폭이 같아도 타입이 다르면 위치가 다르다 |
| `unsigned a:5, :0, c:3` | c = 32 | 32 | 8 | `:0` = "다음 단위로" (6.7.2.1p12) |
| `uint16_t a:10, b:10, c:10` | 0, 16, 32 | 16, 32 | 6 | 16비트 단위마다 하나씩 |
| ADC 레지스터 (폭 1,2,2,8,7,4,5,1,2) | 0,1,3,5,13,20,24,29,30 | (Part A 와 일치) | 4 | 32비트 안에 딱 맞음 |

C11 이 정해 준 건 "자리가 남으면 **같은 단위의 인접 비트에** 채운다" 뿐이다. 자리가 모자랄 때 다음 단위로 갈지 걸칠지(straddle), 단위 안에서 위→아래로 채울지 아래→위로 채울지는 구현 정의, 단위의 정렬은 unspecified 다. SysV/AAPCS 는 "선언 타입 크기의 정렬된 구간을 넘지 않는다" 를 택했다. 한 단위 안에서 바이트 경계를 넘는 건 허용되므로 SMPT(byte 0/1)·OFFSET(byte 1/2) 은 걸친다 — 바이트 단위로 접근하는 코드가 있다면 그것도 깨진다.

### 크로스 타깃: 같은 소스, 다른 바이트

`demo_layout.c` 의 `k_*` 상수 객체(필드 하나만 1)를 네 타깃으로 컴파일해 `.rodata` 를 덤프한 **실제 출력**:

```text
$ xcrun llvm-objdump -s -j .rodata le.o          # --target=thumbv7em-none-eabi (Cortex-M4 LE)
le.o:	file format elf32-littlearm
 0000 01000000 06000000 e01f0000 00e00f00  ................
 0010 0000001f 4dab0f07 04000000 02000000  ....M...........
$ xcrun llvm-objdump -s -j .rodata be.o          # --target=thumbebv7em-none-eabi (Cortex-M4 BE)
be.o:	file format elf32-bigarm
 0000 80000000 60000000 07f80000 0007f000  ....`...........
 0010 000000f8 cad7d038 00000004 00000002  .......8........
```

호스트 arm64 Mach-O(`__TEXT,__const`)는 LE 와 바이트 단위로 **완전히 같았고**, MSVC(`--target=x86_64-pc-windows-msvc`, `.rdata`)도 레지스터 구조체 부분은 같았지만 마지막 두 크기가 `08000000 06000000` 이었다. 바이트를 그 타깃의 32비트 워드로 읽어 정리하면:

| 상수 (필드만 1) | 데이터시트 `_Msk` | Cortex-M4 LE | Cortex-M4 BE | 호스트 / MSVC |
|---|---|---|---|---|
| `k_en` | `0x00000001` | `0x00000001` | **`0x80000000`** | = LE |
| `k_mode` | `0x00000006` | `0x00000006` | **`0x60000000`** | = LE |
| `k_smpt` | `0x00001FE0` | `0x00001FE0` | **`0x07F80000`** | = LE |
| `k_off` | `0x000FE000` | `0x000FE000` | **`0x0007F000`** | = LE |
| `k_ch` | `0x1F000000` | `0x1F000000` | **`0x000000F8`** | = LE |
| `k_mix` | `0x070FAB4D` | `0x070FAB4D` | **`0xCAD7D038`** | = LE |
| `sizeof {u8 a:4; u32 b:4;}` | — | 4 | 4 | 4 / **8** |
| `sizeof {u8 a:4; u16 b:4; u8 c:4;}` | — | 2 | 2 | 2 / **6** |

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 struct 선언 → 레지스터 워드에서의 위치 (bit 31 이 왼쪽)</text>
  <text x="10" y="64" fill="currentColor">LE</text>
  <g stroke="currentColor">
    <rect x="40" y="44" width="42" height="30" class="f-mut"/><rect x="82" y="44" width="21" height="30" class="f-warn"/>
    <rect x="103" y="44" width="105" height="30" class="f-acc"/><rect x="208" y="44" width="84" height="30" class="f-mut"/>
    <rect x="292" y="44" width="147" height="30" class="f-ok"/><rect x="439" y="44" width="168" height="30" class="f-acc"/>
    <rect x="607" y="44" width="42" height="30" class="f-ok"/><rect x="649" y="44" width="42" height="30" class="f-acc"/>
    <rect x="691" y="44" width="21" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="155" y="64">CH</text><text x="365" y="64">OFFSET</text><text x="523" y="64">SMPT</text>
    <text x="628" y="64">RES</text><text x="670" y="64">MODE</text><text x="701" y="64">EN</text>
  </g>
  <text x="10" y="144" fill="currentColor">BE</text>
  <g stroke="currentColor">
    <rect x="40" y="124" width="21" height="30" class="f-ok"/><rect x="61" y="124" width="42" height="30" class="f-acc"/>
    <rect x="103" y="124" width="42" height="30" class="f-ok"/><rect x="145" y="124" width="168" height="30" class="f-acc"/>
    <rect x="313" y="124" width="147" height="30" class="f-ok"/><rect x="460" y="124" width="84" height="30" class="f-mut"/>
    <rect x="544" y="124" width="105" height="30" class="f-acc"/><rect x="649" y="124" width="21" height="30" class="f-warn"/>
    <rect x="670" y="124" width="42" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="50" y="144">EN</text><text x="82" y="144">MODE</text><text x="124" y="144">RES</text><text x="229" y="144">SMPT</text>
    <text x="386" y="144">OFFSET</text><text x="596" y="144">CH</text>
  </g>
  <g stroke="currentColor" stroke-dasharray="3 3" fill="none">
    <path d="M701 76 L50 122"/><path d="M155 76 L596 122"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="184">LE(AAPCS): 첫 필드가 bit 0 부터 (LSB-first)  →  en = 1 은 0x00000001</text>
    <text x="10" y="206">BE(AAPCS): 첫 필드가 bit 31 부터 (MSB-first) →  en = 1 은 0x80000000 = 예약 비트에 쓰기!</text>
    <text x="10" y="228">필드 "안"의 비트 순서(값)는 그대로, 필드 "위치"만 거울상: LE 비트 p..p+w-1 → BE 비트 31-p-w+1..31-p</text>
    <text x="10" y="256">LE 용 비트필드 헤더를 BE 칩에 쓰면 컴파일 에러도 경고도 없이 모든 필드가 엉뚱한 비트로 간다.</text>
  </g>
</svg>
```

`thumbebv7em` 의 `.o` 가 `elf32-bigarm` 이고 바이트 `80 00 00 00` 이 BE CPU 의 32비트 로드로 `0x80000000` 이 된다. AAPCS 는 big-endian 에서 비트필드를 컨테이너의 **최상위 비트부터** 할당한다고 정한다 — 그래서 "바이트 순서" 만이 아니라 "비트필드 할당 순서" 까지 뒤집힌다. Linux 의 `struct iphdr` 가 `#if defined(__LITTLE_ENDIAN_BITFIELD)` 로 `ihl:4, version:4` 의 선언 순서를 엔디안별로 두 벌 적어 두는 이유가 이것이다. 크기 차이(MSVC 8 vs 4)는 MSVC 가 **선언 타입 크기가 바뀌면 새 저장 단위를 연다** 는 다른 규칙을 쓰기 때문이다 (추가 연습 4). Windows 툴로 만든 비트필드 구조체를 펌웨어와 주고받으면 여기서 깨진다.

`demo_layout.c` 는 `sizeof` 처럼 상수식인 것은 타깃별로 `_Static_assert` 해 둔다. AAPCS 빌드에 MSVC 기대값(8)을 일부러 넣어 보면:

```text
demo_layout.c:50:16: error: static assertion failed due to requirement 'sizeof(mix_8_32) == 8': WRONG ON PURPOSE
note: expression evaluates to '4 == 8'
```

### C11 6.7.2.1 — 무엇이 어떻게 남겨져 있나

| 항목 | 조항 | 분류 | Apple clang arm64 / AAPCS 에서 |
|---|---|---|---|
| plain `int` 비트필드가 signed 인가 | 6.7.2p5, 각주 125 | 구현 정의 | signed |
| `_Bool`/`signed int`/`unsigned int` 외 타입(`uint8_t`, `uint16_t`, enum…) 허용 | 6.7.2.1p5 | 구현 정의 | 허용 (선언 타입이 저장 단위·정렬에 영향) |
| atomic 비트필드 허용 | 6.7.2.1p5 | 구현 정의 | — |
| 단위 안 할당 순서 (상위→하위 / 하위→상위) | 6.7.2.1p11 | 구현 정의 | LE: LSB-first, BE: MSB-first |
| 자리가 모자랄 때 다음 단위 vs 걸치기(straddle) | 6.7.2.1p11 | 구현 정의 | 다음 단위 (선언 타입 크기 정렬 구간 기준) |
| 저장 단위의 정렬 | 6.7.2.1p11 | **unspecified** | 선언 타입 정렬 |
| 자리가 남으면 같은 단위 인접 비트에 채움 | 6.7.2.1p11 | **보장** | — |
| `:0` 은 다음 단위로, 이름 없는 필드는 패딩 | 6.7.2.1p12 | **보장** | — |
| 폭 ≤ 타입 폭, 상수식 | 6.7.2.1p4 | 제약 (위반 시 진단) | `uint8_t x:9` → 에러 |
| `&`, `sizeof` 를 비트필드에 | 6.5.3.2p1, 6.5.3.4p1 | 제약 위반 | 에러 |
| `offsetof` 를 비트필드에 | 7.19p3 | UB | — |
| 이름 없는 멤버 값 (초기화 후에도) | 6.7.9p9 | indeterminate | clang 은 0 을 채움 |
| 범위 밖 값을 signed 비트필드에 대입 | 6.3.1.3p3 | 구현 정의 (또는 시그널) | 잘라서 2의 보수 (7 → −1) |
| volatile 객체 "접근" 이 무엇인가 (접근 폭) | 6.7.3p7 | 구현 정의 | AAPCS: 선언 타입 폭, 호스트: 좁힘 (Part C) |

이 목록은 부록 J.3.9 "Structures, unions, enumerations, and bit-fields" 에 모여 있다. 한 가지 더: GCC arm-none-eabi 에서는 `uint32_t` 가 `unsigned int` 가 아니라 `long unsigned int` 로 정의된다(이 맥의 clang 은 `__UINT32_TYPE__ = unsigned int` 로 확인, GCC 는 여기 없어서 미확인 — 널리 알려진 사실). 그러면 `uint32_t x:3` 조차 엄밀히는 "구현 정의 타입" 비트필드가 된다.

### Part C — 비트필드 쓰기는 필드마다 RMW

`demo_rmw.c` 를 `--target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S` 로 컴파일한 **실제 출력** (지시어 줄 생략):

```text
bf_set_ch:                    ; a->CFG.b.ch = ch;                    (volatile)
	ldr	r2, [r0, #4]
	bfi	r2, r1, #24, #5       ; bit-field insert: r1 의 하위 5비트를 bit 24 자리에
	str	r2, [r0, #4]
bf_start_ch:                  ; a->CFG.b.ch = ch; a->CFG.b.start = 1;   (volatile)
	ldr	r2, [r0, #4]
	bfi	r2, r1, #24, #5
	str	r2, [r0, #4]          ; ← 쓰기 1: CH 만 바뀐 중간 상태가 버스에 나간다
	ldr	r1, [r0, #4]          ; ← 다시 읽기
	orr	r1, r1, #536870912    ; 0x20000000 = START
	str	r1, [r0, #4]          ; ← 쓰기 2
ram_start_ch:                 ; 같은 두 줄, volatile 아닌 RAM 구조체
	ldr	r2, [r0]
	bic	r2, r2, #1056964608   ; 0x3F000000: CH 와 START 를 한 번에 지움
	bfi	r2, r1, #24, #5
	add.w	r1, r2, #536870912    ; START 는 방금 지웠으니 add == orr
	str	r1, [r0]              ; 쓰기 1번으로 합쳐짐
mask_start_ch:                ; v = CFG; v = (v & ~CH_Msk) | (ch << 24) | START; CFG = v;
	ldr	r2, [r0, #4]
	bic	r2, r2, #1056964608
	bfi	r2, r1, #24, #5
	add.w	r1, r2, #536870912
	str	r1, [r0, #4]
shadow_start_ch:              ; 지역 union 사본에 비트필드 대입 후 워드로 1번 쓰기 — mask_start_ch 와 명령까지 동일
```

(`;` 뒤 주석은 설명용으로 붙인 것이고, 명령 줄은 출력 그대로다. 호스트 arm64 도 `ldr/bfi/str` 두 쌍 vs `and/bfi/orr` 후 `str` 한 번으로 모양이 같았다.) 관찰:

1. **필드 하나 = RMW 하나**. `volatile` 은 접근을 합치지 못하게 하므로 (C-B09) 필드 두 개면 `ldr/str` 이 두 쌍이다. 하네스의 `adc_start_fieldwise` trace `"rCFG wCFG=455F6613 rCFG wCFG=655F6613"` 가 정확히 이 모양이다. 7개 필드를 하나씩 설정하면 읽기 7 + 쓰기 7.
2. **중간 상태가 하드웨어에 보인다**. 첫 쓰기 직후 CH 만 바뀐 상태가 장치에 도달한다. 이번 모델에선 무해하지만, "EN=1 인 동안 MODE 변경 금지" 같은 규칙이 있는 주변장치라면 중간 쓰기 자체가 버그다. 순서를 반대로(`start=1; ch=…`) 쓰면 변환이 **옛 채널** 에서 시작된다.
3. **마스크 버전 = 소스가 접근을 정한다**. `mask_start_ch` 는 읽기 1 + 쓰기 1 이고, 계산은 컴파일러가 `bic/bfi/add` 로 최적화한다. 비트필드를 **지역 사본** 에만 쓰는 `shadow_start_ch` 도 명령까지 같다 — "비트필드 문법 자체가 느리다" 가 아니라 "volatile 객체에 필드별로 쓰는 것" 이 문제다.
4. **ARMv6-M(Cortex-M0/M0+)** 엔 `BFI` 가 없다: `bf_set_ch` 가 `movs/lsls/ldr/bics/lsls/lsrs/adds/str` 8개 명령이 된다 (`--target=thumbv6m-none-eabi -mcpu=cortex-m0plus` 실측).

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">adc_start(ch=5) — CFG 리셋 값 0x435F6613 에서 (하네스 trace 그대로)</text>
  <text x="10" y="58" fill="currentColor">필드별</text>
  <g stroke="currentColor">
    <rect x="80" y="40" width="70" height="28" class="f-acc"/><rect x="160" y="40" width="160" height="28" class="f-warn"/>
    <rect x="330" y="40" width="70" height="28" class="f-acc"/><rect x="410" y="40" width="160" height="28" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="90" y="59">rCFG</text><text x="168" y="59">wCFG=455F6613</text>
    <text x="340" y="59">rCFG</text><text x="418" y="59">wCFG=655F6613</text>
  </g>
  <text x="580" y="59" fill="currentColor">접근 4번</text>
  <text x="160" y="88" fill="currentColor" font-size="12">CH=5, START=0 인 중간 상태가</text>
  <text x="160" y="104" fill="currentColor" font-size="12">장치에 먼저 도착</text>
  <text x="410" y="88" fill="currentColor" font-size="12">여기서 변환 시작 (CH=5)</text>
  <text x="10" y="148" fill="currentColor">마스크</text>
  <g stroke="currentColor">
    <rect x="80" y="130" width="70" height="28" class="f-acc"/><rect x="160" y="130" width="160" height="28" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="90" y="149">rCFG</text><text x="168" y="149">wCFG=655F6613</text>
  </g>
  <text x="330" y="149" fill="currentColor">접근 2번 — CH 와 START 가 같은 쓰기에</text>
  <text x="10" y="194" fill="currentColor">volatile 이 "소스의 접근을 하나도 합치지 마라" 를 지키기 때문에, 필드 대입 2줄 = 버스 RMW 2번.</text>
  <text x="10" y="216" fill="currentColor">순서를 start=1; ch=5; 로 바꾸면 첫 쓰기에서 옛 채널(3)로 변환이 시작된다.</text>
</svg>
```

**접근 폭** 은 더 미묘하다. `demo_rmw.c` ⑦ `struct { volatile uint32_t lo:4; volatile uint32_t hi:4; }` (sizeof 4) 에 `r->hi = 3` 을 쓴 실제 결과:

| 타깃 / 옵션 | 명령 | 접근 폭 |
|---|---|---|
| Cortex-M4 (기본, AAPCS) | `ldr r1,[r0]` … `bfi r1,r2,#4,#4` … `str r1,[r0]` | 32비트 |
| Cortex-M4 `-fno-aapcs-bitfield-width` | `ldrb r1,[r0]` … `strb r1,[r0]` | **8비트** |
| 호스트 arm64 | `ldrb w8,[x0]` … `bfxil` … `strb w9,[x0]` | **8비트** |
| ⑧ `volatile uint8_t en:1` (Cortex-M4, 호스트 모두) | `ldrb` / `orr #1` / `strb` | 8비트 |

AAPCS 는 "volatile 비트필드는 **선언된 컨테이너 타입의 폭**으로 접근한다" 고 정하고 clang 은 ARM 에서 이를 기본으로 따른다 (`-faapcs-bitfield-width`; GCC 는 `-fstrict-volatile-bitfields` 가 ARM 에서 기본). 그러나 같은 소스가 호스트에선 바이트 접근이 된다. 주변장치 레지스터가 32비트 접근만 허용하면(많은 APB 주변장치가 그렇다) 바이트 쓰기는 무시되거나, 다른 바이트 레인에 복제되거나, 버스 폴트가 난다. **표준은 이걸 정하지 않는다**: "volatile 객체에 대한 접근이 무엇인가는 구현 정의" (6.7.3p7).

### 그래서 CMSIS/벤더 헤더는 마스크를 쓴다

| 이유 | 비트필드 | `_Pos` / `_Msk` |
|---|---|---|
| 비트 위치 | ABI 가 정함 (LE/BE·컴파일러별) | 소스가 정함, 어디서나 같음 |
| 컴파일타임 검증 | 불가 (위 에러) | `_Static_assert` 로 맵 전체 |
| 접근 횟수 | 필드마다 RMW | 원하는 만큼 — 보통 1R + 1W |
| 접근 폭 | ABI·옵션에 따라 8/16/32 | 항상 레지스터 타입(`uint32_t`) 폭 |
| 여러 필드 원자적 갱신 | 불가 (필드별) | 한 번의 쓰기 |
| 쓰기 전용·W1C·트리거 비트 | 다른 필드 쓰기에 휩쓸림 (C-M02) | 쓸 비트를 명시 |
| 부호 있는 필드 | 공짜 (`sbfx`) | `sext()` 한 줄 |
| 가독성 | 좋음 | `FIELD_PREP` 류 매크로로 보완 (C-B04) |

CMSIS 코어 헤더에도 비트필드가 있긴 하다 — `APSR_Type`, `xPSR_Type`, `CONTROL_Type` 같은 `union { struct { ... } b; uint32_t w; }`. 하지만 이것들은 `__get_xPSR()` 같은 인트린직이 돌려준 **값(지역 사본)** 을 해석하는 용도이고, `SCB`·`NVIC` 같은 메모리 맵 레지스터 접근은 `_Pos`/`_Msk` 로 한다. TI C2000 의 `.bit`/`.all` 헤더나 Microchip 의 `PORTAbits.RA0` 처럼 비트필드 레지스터를 쓰는 생태계도 있는데, 이건 **특정 컴파일러 하나** 에 묶인 헤더라서 가능한 선택이다.

### 사용 기준 정리

| 상황 | 비트필드 | 이유 |
|---|---|---|
| MMIO 레지스터에 직접 오버레이 | **쓰지 않는다** | 위치·폭·접근 횟수 모두 소스 밖 |
| 레지스터 값의 **지역 사본** 해석 (shadow, 디버그 덤프) | 조건부 OK | 1R+1W 는 소스가 통제. 단 레이아웃 self-test(`bf_probe_mask`) 또는 타깃 objdump 확인 |
| 통신 프로토콜 헤더·파일 포맷·다른 컴파일러와 공유 | **쓰지 않는다** | 엔디안·ABI 마다 다름 → 바이트 직렬화 (C-M05) |
| ISR 과 메인이 같은 구조체의 다른 필드를 갱신 | **쓰지 않는다** | 인접 비트필드는 하나의 메모리 위치 (3.14) → RMW 경쟁 (C-M14) |
| 모듈 내부 RAM 플래그, 메모리 절약 (외부 레이아웃 무관) | OK | 레이아웃을 아무도 관찰하지 않음. `unsigned`/`_Bool` 명시 |
| 벤더가 비트필드 헤더만 제공 (단일 컴파일러) | 따르되 격리 | 드라이버 경계 안에 가두고 테스트를 타깃에서 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 48/48 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 48/48, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `struct { int ready:1; }` 후 `if (s.ready == 1)` | plain int 는 (clang/GCC 에서) signed → 1비트 signed 의 값은 0 / −1 뿐. 영원히 거짓 | `unsigned ready:1` 또는 `!= 0` |
| `int offset:7` 로 부호 있는 필드 선언 | plain int 의 부호는 구현 정의 (6.7.2p5) | `signed int offset:7` 명시 |
| `ADC1->CFG_b.ch = c; ADC1->CFG_b.start = 1;` | 필드마다 RMW → 접근 4번, 중간 상태 노출, 순서 바꾸면 옛 채널로 변환 | 사본에 계산 후 한 번 쓰기 (`adc_start`) |
| LE 용 비트필드 헤더를 BE 타깃에 재사용 | 필드가 거울상 위치로 이동 (`en=1` → `0x80000000`), 경고 없음 | 마스크, 또는 `#if __BYTE_ORDER__` 로 두 벌 + 바이트 검사 |
| `adc_cfg_bits_t b = {0}; b.ch = 3; reg = *(uint32_t*)&b;` | 예약(이름 없는) 비트 indeterminate (6.7.9p9) + 별칭 규칙 위반 (C-M13) | `memset` + `memcpy` |
| 비트필드 구조체로 패킷을 `memcpy` 해 송신 | 엔디안·컴파일러마다 비트 위치·크기 다름 (MSVC 8 vs AAPCS 4) | `put_le16/put_be32` 직렬화 (C-M05) |
| `volatile uint8_t` 비트필드로 32비트 전용 레지스터 정의 | `ldrb/strb` 바이트 접근 → 무시·복제·버스 폴트 | `volatile uint32_t` 레지스터 + 마스크 |
| "`_Static_assert` 로 비트필드 위치를 고정했다" | 비트 위치는 상수식으로 못 꺼냄 — 그런 assert 는 컴파일 에러 | 마스크 맵을 assert, 비트필드는 런타임 self-test |
| ISR 은 `flags.rx = 1`, 메인은 `flags.tx = 0` | 인접 비트필드 = 같은 메모리 위치 → 같은 워드 RMW 경쟁 → lost update | 별도 변수/워드, 또는 임계 구역 (C-M14) |

## 8. 면접 Q&A

<details>
<summary>Q1. 레지스터를 비트필드 구조체로 정의하면 안 되는 이유를 말해 보세요.</summary>

네 가지. ① **위치**: 단위 안 할당 순서와 straddle 여부가 구현 정의(6.7.2.1p11) — 실측으로 같은 선언이 Cortex-M4 LE 에선 `en` = bit 0, BE 에선 bit 31. ② **검증 불가**: 비트 위치는 상수식이 아니라 `_Static_assert` 로 못 고정한다. ③ **접근 횟수**: volatile 비트필드는 필드마다 `ldr/bfi/str` 한 번씩, 여러 필드를 원자적으로 못 바꾼다. ④ **접근 폭**: volatile 접근이 무엇인지는 구현 정의(6.7.3p7) — AAPCS 는 컨테이너 폭이지만 호스트 arm64 는 같은 소스를 `ldrb/strb` 로 만들었다. 그래서 CMSIS 는 `_Pos`/`_Msk`. 꼬리질문 "그래도 쓰는 곳은?" → 단일 컴파일러에 묶인 벤더 헤더(TI C2000 `.bit`), CMSIS `APSR_Type` 같은 **값 해석용** union.

</details>

<details>
<summary>Q2. 그럼 표준이 비트필드에 대해 보장하는 건 뭔가요?</summary>

허용 타입(`_Bool`, `signed int`, `unsigned int` + 구현 정의 타입), 폭은 타입 폭 이하의 상수식(6.7.2.1p4), 자리가 남으면 **같은 단위의 인접 비트에** 채움(p11), 이름 없는 필드는 패딩, `:0` 은 다음 단위로(p12), 인접 비트필드들은 하나의 메모리 위치(3.14). 순서·straddle·plain int 부호는 구현 정의, 단위 정렬은 unspecified, `offsetof` 는 UB, `&`/`sizeof` 는 제약 위반. "구현 정의" 는 컴파일러가 **문서화해야** 하는 것이고(3.4.1), ARM 에선 AAPCS 가 그 문서다.

</details>

<details>
<summary>Q3. `ADC->CFG_b.ch = 5;` 는 Cortex-M4 에서 어떤 명령이 되나요? 필드 두 개면?</summary>

`ldr r2,[r0,#4]; bfi r2,r1,#24,#5; str r2,[r0,#4]` — read-modify-write 한 번 (`BFI` 는 ARMv7-M 의 bit-field insert). 두 필드면 volatile 때문에 합쳐지지 않아 ldr/str 두 쌍, 중간 상태가 버스에 나간다. 같은 코드가 volatile 이 아닌 RAM 구조체면 `bic/bfi/add` 후 str 한 번으로 합쳐진다 (실측). Cortex-M0+ 는 BFI 가 없어 시프트·마스크 명령 8개. 꼬리질문 "W1C 플래그가 같은 레지스터에 있으면?" → RMW 가 읽은 1을 다시 써서 다른 플래그를 지운다 (C-M02).

</details>

<details>
<summary>Q4. volatile 비트필드 쓰기는 몇 비트 폭으로 접근하나요?</summary>

표준은 정하지 않는다 (6.7.3p7 구현 정의). AAPCS 는 "선언된 컨테이너 타입 폭으로, 쓰기도 최소 한 번 읽고 나서" 를 요구하고, clang 은 ARM 에서 `-faapcs-bitfield-width` 가 기본, GCC 는 `-fstrict-volatile-bitfields` 가 ARM 기본이다. 실측: `volatile uint32_t lo:4, hi:4` 에 `hi = 3` → Cortex-M4 기본 32비트 `ldr/str`, `-fno-aapcs-bitfield-width` 면 `ldrb/strb`, 호스트 arm64 는 처음부터 `ldrb/strb`. 선언 타입을 `uint8_t` 로 하면 ARM 에서도 바이트 접근. 32비트 접근만 받는 주변장치에선 이 차이가 곧 버그다.

</details>

<details>
<summary>Q5. ISR 과 메인 루프가 같은 비트필드 구조체의 서로 다른 필드만 건드려요. 안전한가요?</summary>

아니다. C11 3.14: 폭이 0이 아닌 인접 비트필드들의 열은 **하나의 메모리 위치** 다. `flags.tx = 0` 은 워드(또는 바이트) 전체의 RMW 이므로, 메인이 읽은 뒤 ISR 이 `flags.rx = 1` 을 쓰고 돌아오면 메인의 쓰기가 rx 를 옛 값으로 되돌린다 (lost update). 별도 변수(또는 별도 워드)로 나누거나 임계 구역으로 감싼다 (C-B09 Part C, C-M14). 멀티 스레드라면 data race = UB (5.1.2.4p25).

</details>

<details>
<summary>Q6. 네트워크 패킷 헤더를 비트필드로 정의하면 어떤가요? Linux 는 어떻게 하죠?</summary>

비트 순서가 엔디안·ABI 마다 다르므로 그대로는 이식 불가. Linux `struct iphdr` 는 `#if defined(__LITTLE_ENDIAN_BITFIELD) __u8 ihl:4, version:4; #elif defined(__BIG_ENDIAN_BITFIELD) __u8 version:4, ihl:4;` 로 **두 벌** 적어 둔다 (GCC 의 할당 규칙을 전제한 것). 새 코드라면 바이트 버퍼에서 `get_be16()` + 마스크로 파싱하는 편이 낫다 (C-M05). 드라이버 레지스터 쪽 커널 관용구는 `FIELD_GET`/`FIELD_PREP` (C-B04).

</details>

<details>
<summary>Q7. 비트필드 레이아웃이 맞는지 어떻게 확인하나요? _Static_assert 로 되나요?</summary>

안 된다. union 의 다른 멤버 읽기도, `__builtin_bit_cast` 도 상수식이 아니다 (clang 실측: "read of member 'raw' of union with active member 'b' is not allowed in a constant expression", "constexpr bit_cast involving bit-field is not yet supported"). `sizeof`/`_Alignof` 만 assert 가능하다. 위치는 ① 타깃에서 돌리는 self-test (필드를 전부 1로 → 워드로 읽어 `_Msk` 와 비교, 이 레슨의 `bf_probe_mask`) ② 상수 객체를 크로스 컴파일해 `llvm-objdump -s` 로 바이트 확인 — 둘 중 하나. 그래서 처음부터 마스크로 정의하면 이 일이 사라진다.

</details>

<details>
<summary>Q8. `struct { uint8_t a:4; uint32_t b:4; }` 의 sizeof 는?</summary>

ABI 마다 다르다. SysV/AAPCS/Apple arm64: b 가 32비트 컨테이너(bit 0..31) 안에 들어가므로 bit 4 에 붙고, 구조체 정렬은 `uint32_t` 의 4 → **4**. MSVC: 선언 타입 크기가 바뀌면 새 단위 → a 는 byte 0, b 는 offset 4 의 새 `uint32_t` 단위 → **8** (`--target=x86_64-pc-windows-msvc` 실측). 게다가 `uint8_t`/`uint32_t` 비트필드 자체가 표준 타입이 아닐 수도 있다 (6.7.2.1p5 구현 정의 타입).

</details>

## 9. 추가 연습

### 연습 1 — 1비트 int 비트필드

옛 헤더에 `struct { int ready:1; int error:1; }` 가 있다. 1을 저장한 뒤 `ready == 1` 은 참인가? 올바른 판정 함수와 고친 선언을 써라.

<details>
<summary>답 보기</summary>

거짓. clang/GCC 에서 plain int 비트필드는 signed 이고 1비트 signed 의 값 범위는 {−1, 0} 이라, 1 을 저장하면 (범위 밖 → 구현 정의 변환) −1 이 읽힌다 (`demo_plain_int.c` 실측 `ready=-1`). 판정은 `!= 0`, 근본 수정은 `unsigned ready:1` (또는 `_Bool ready:1`). 상수 1 을 대입하면 clang 은 `-Wsingle-bit-bitfield-constant-conversion` 경고를 낸다.

</details>

### 연습 2 — 로컬 사본(shadow) 패턴

비트필드 union 을 **지역 변수** 에만 써서 `raw` 에 CH 와 START 를 넣은 새 값을 돌려주는 `shadow_set_ch_start(raw, ch)` 를 작성하고, 마스크 버전과 1000개 무작위 입력에서 같은지 확인하라.

<details>
<summary>답 보기</summary>

`t.w = raw; t.b.ch = ch; t.b.start = 1; return t.w;` — 레지스터 관점에선 읽기 1 + 쓰기 1 이고, Cortex-M4 asm 은 `mask_start_ch` 와 명령까지 같다 (`demo_rmw.c` ⑤). 예약 비트도 이 ABI 에선 보존된다(테스트). 단 결과의 비트 위치는 여전히 ABI 에 기댄다 — 이 패턴을 쓰려면 Part B 의 self-test 를 함께 둔다.

</details>

### 연습 3 — big-endian 배치 예측

LE 에서 비트 `p..p+w-1` 에 있는 필드가 BE ARM 에선 어디로 가는지 `be_field_mask(p, w)` 로, 설정 전체를 `pack_msb_first(c)` 로 예측하고, `demo_layout.c` 를 `--target=thumbebv7em-none-eabi` 로 컴파일한 objdump 값(`80000000 60000000 07f80000 0007f000 000000f8 cad7d038`)과 맞춰 보라.

<details>
<summary>답 보기</summary>

`mask(w) << (32 - p - w)` — 위치만 거울상, 필드 안 비트 순서는 그대로. `mix` 는 EN `0x80000000` + MODE 2 `0x40000000` + RES 1 `0x08000000` + SMPT 0x5A `0x02D00000` + OFFSET 0x7D `0x0007D000` + CH 7 `0x00000038` = **`0xCAD7D038`** — 실측과 일치. 같은 레지스터 값이 LE 에선 `0x070FAB4D` 이므로, LE 용 헤더를 BE 칩에 쓰면 완전히 다른 설정이 하드웨어에 들어간다.

</details>

### 연습 4 — 선언 타입이 섞인 비트필드: SysV/AAPCS vs MSVC

`{type_bytes, width}` 목록을 받아 구조체 크기를 예측하는 `size_sysv()` 와 `size_ms()` 를 작성하라. SysV 예측은 호스트 `sizeof` 와, MSVC 예측은 `--target=x86_64-pc-windows-msvc` 실측값(8, 8, 6, 8, 6)과 맞춘다.

<details>
<summary>답 보기</summary>

SysV/AAPCS: 비트 커서 하나, 필드가 "자기 선언 타입 크기로 정렬된 구간" 을 넘으면 그 경계로 민다. 구조체 정렬 = 이름 있는 필드 타입 중 최대. MSVC: 선언 타입 크기가 바뀌거나 남은 비트가 모자라면 그 크기로 정렬된 **새 단위를 통째로** 연다. 실측 쌍 (AAPCS/MSVC): `{u8:4,u32:4}` 4/8, `{u32:4,u8:4}` 4/8, `{u8:4,u16:4,u8:4}` 2/6, `{u8:3,u16:7,u32:20}` 4/8, `{u16:10 ×3}` 6/6, ADC 레지스터 4/4 (`uint32_t` 와 `signed int` 는 크기가 같아 MSVC 도 한 단위).

</details>

### 연습 5 — X-macro 로 레지스터 맵 한 곳 정의

`X(name, pos, width, signed)` 목록 하나로 `_Msk` 상수, 겹침 검사 `_Static_assert`, 필드 설명 테이블을 모두 생성하고, UART 로그용 `cfg_format(buf, cap, raw)` → `"EN=1 MODE=2 RES=1 SMPT=90 OFFSET=-3 CH=7 START=0"` 을 작성하라.

<details>
<summary>답 보기</summary>

`#define ADC_CFG_FIELDS(X) X(EN,0,1,0) X(MODE,1,2,0) ...` 를 세 번 펼친다: enum 마스크, `0u + Msk + Msk ...` (64비트 합) == `0u | Msk | ...` (OR) 로 겹침 검사 (C-B04 Q3 의 트릭), `{ #name, pos, w, sg }` 테이블. 덤프는 테이블을 돌며 `(raw >> pos) & mask(w)`, signed 면 2^w 를 빼서 부호 확장. 비트필드의 "읽기 좋음" 을 레이아웃 위험 없이 얻는 방법이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 23/23 passed
```

## 10. 핵심 정리

- 비트필드의 비트 위치는 **ABI 가 정한다**: 할당 순서·straddle·plain int 부호·비표준 타입 허용은 구현 정의(6.7.2.1p11, 6.7.2p5), 단위 정렬은 unspecified.
- 실측: 같은 선언이 Cortex-M4 LE 에선 `en` = `0x00000001`, BE 에선 `0x80000000`. MSVC 는 선언 타입이 섞이면 크기까지 다르다 (8 vs 4).
- 비트필드 위치는 상수식이 아니라 `_Static_assert` 불가 → 마스크 맵은 assert, 비트필드는 런타임 self-test / objdump 로만 확인.
- volatile 비트필드 쓰기 = 필드마다 `ldr/bfi/str`. 두 필드 = 접근 4번 + 중간 상태 노출. 접근 폭도 타깃·옵션마다 다름 (AAPCS 32비트, 호스트 `ldrb/strb`).
- 마스크/시프트는 위치·횟수·폭을 **소스가** 정한다 → CMSIS `_Pos`/`_Msk`, Linux `FIELD_PREP`. 지역 사본에 계산 후 한 번 쓰기.
- 부호 있는 필드는 `sext()` 로: `(v ^ s) - s`, 음수 `>>` 와 범위 밖 변환 없이.
- 비트필드를 쓴다면: `unsigned`/`signed int` 명시, 지역 사본에만, 외부 레이아웃(패킷·파일·ISR 공유)엔 금지.

## 참고자료

- C11 (N1570) 6.7.2.1 Structure and union specifiers (p4 폭, p5 허용 타입, p11 할당·straddle·정렬, p12 이름 없는 필드·`:0`), 6.7.2p5 plain int, 6.7.9p9 이름 없는 멤버 초기화, 6.7.3p7 volatile 접근, 3.14 memory location, 6.3.1.3 정수 변환, 6.5.7 시프트, 7.19p3 offsetof, 부록 J.3.9 — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- [Arm — Procedure Call Standard for the Arm Architecture (AAPCS32): Bit-fields, Volatile bit-fields](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst)
- [Clang command line reference — `-faapcs-bitfield-width`, `-faapcs-bitfield-load`](https://clang.llvm.org/docs/ClangCommandLineReference.html)
- [GCC Code Gen Options — `-fstrict-volatile-bitfields`](https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html)
- [Microsoft — C Bit Fields (MSVC 배치 규칙)](https://learn.microsoft.com/en-us/cpp/c-language/c-bit-fields)
- [ARM CMSIS-Core — core_cm4.h 의 `APSR_Type` / `_Pos` `_Msk` 규칙](https://arm-software.github.io/CMSIS_6/latest/Core/index.html)
- [Linux `include/uapi/linux/ip.h` — `struct iphdr` 의 `__LITTLE_ENDIAN_BITFIELD`](https://github.com/torvalds/linux/blob/master/include/uapi/linux/ip.h)
- 관련 레슨: C-B04 (FIELD_GET/PREP 마스크 매크로), C-B09 (volatile 접근 횟수), C-B12 (패딩), C-B13 (엔디안), C-M01 (레지스터 구조체 오버레이), C-M02 (RMW 와 W1C), C-M05 (엔디안 독립 직렬화), C-M13 (strict aliasing), C-M14 (ISR 공유 데이터)
