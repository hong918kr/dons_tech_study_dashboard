---
id: C-M13
time: 80분
updated: 2026-11-07
---
# C-M13 Strict Aliasing과 Type Punning

## 학습 목표

- C11 6.5p6(유효 타입, effective type)과 6.5p7(접근 허용 목록)을 정확히 말하고, "문자 타입은 무엇이든 aliasing 할 수 있지만 **그 반대는 안 된다**" 를 예로 설명할 수 있다.
- `*(uint32_t *)&f` 와 `*(uint32_t *)&rx[1]` 이 왜 UB 인지, 그리고 `-O2` 에서 실제로 다른 결과가 나오는 예제를 재현할 수 있다 (`f(int*, float*)` 가 상수 1을 반환).
- float 비트 추출과 버퍼 → `uint32_t` 를 포인터 캐스트·union·`memcpy` 세 방식으로 쓰고, C 와 C++ 에서 각각 합법인지 구분한다 (union: C OK / C++ UB).
- `memcpy` 버전이 `-O2` 에서 `fmov`/`vmov`/`ldr` **한 개**로 컴파일됨을 arm64 와 Cortex-M4 asm 으로 확인한다.
- IEEE-754 binary32 필드(sign/exp/frac)를 안전하게 분해·조립·분류하고, Quake 빠른 역제곱근을 aliasing-안전하게 다시 쓴다.

## 1. 문제

프로토콜 버퍼를 `*(uint32_t *)buf` 로 읽고, float 를 `*(uint32_t *)&f` 로 들여다보는 코드는 펌웨어에 흔하다. 그리고 **`-O0` 에선 되고 `-O2` 에선 틀리는** 대표적인 코드이기도 해서, "type punning 을 안전하게 하는 법" 은 펌웨어 면접의 단골 꼬리질문이다.

핵심 규칙은 C11 6.5p7 하나다: **객체는 정해진 타입의 lvalue 로만 접근할 수 있다.** 컴파일러는 이 규칙을 믿고 "`int *` 로 쓴 값은 `float *` 쓰기로 바뀔 수 없다" 고 가정해 읽기를 생략하거나 순서를 바꾼다 — 이것이 TBAA(type-based alias analysis)다. 아래 표는 "어떤 객체를 어떤 타입으로 읽어도 되는가" 다. 왼쪽 열(문자 타입)은 전부 ✔ 인데, 아래 줄(`uint8_t` 배열을 `uint32_t` 로)은 ✘ 다 — 이 **비대칭**이 이번 레슨의 중심이다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">C11 6.5p7 — 객체(유효 타입) × 접근하는 lvalue 타입</text>
  <g fill="currentColor">
    <text x="20" y="62">객체 ↓  /  lvalue →</text>
    <text x="230" y="62" font-family="monospace">unsigned char</text>
    <text x="370" y="62" font-family="monospace">uint32_t</text>
    <text x="480" y="62" font-family="monospace">int32_t</text>
    <text x="600" y="62" font-family="monospace">float</text>
    <text x="20" y="98" font-family="monospace">float f;</text>
    <text x="20" y="138" font-family="monospace">uint32_t w;</text>
    <text x="20" y="178" font-family="monospace">uint8_t rx[64];</text>
  </g>
  <rect x="215" y="78" width="120" height="30" class="f-ok" stroke="currentColor"/>
  <rect x="345" y="78" width="110" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="465" y="78" width="110" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="585" y="78" width="110" height="30" class="f-ok" stroke="currentColor"/>
  <rect x="215" y="118" width="120" height="30" class="f-ok" stroke="currentColor"/>
  <rect x="345" y="118" width="110" height="30" class="f-ok" stroke="currentColor"/>
  <rect x="465" y="118" width="110" height="30" class="f-ok" stroke="currentColor"/>
  <rect x="585" y="118" width="110" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="215" y="158" width="120" height="30" class="f-ok" stroke="currentColor"/>
  <rect x="345" y="158" width="110" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="465" y="158" width="110" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="585" y="158" width="110" height="30" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="225" y="98">✔ 문자 타입</text><text x="355" y="98">✘ UB</text><text x="475" y="98">✘ UB</text><text x="595" y="98">✔ 같은 타입</text>
    <text x="225" y="138">✔ 문자 타입</text><text x="355" y="138">✔ 같은 타입</text><text x="475" y="138">✔ 부호 대응</text><text x="595" y="138">✘ UB</text>
    <text x="225" y="178">✔ 같은 타입</text><text x="355" y="178">✘ UB (+정렬)</text><text x="475" y="178">✘ UB</text><text x="595" y="178">✘ UB</text>
    <text x="20" y="222">• 문자 타입(char, signed/unsigned char) lvalue 는 어떤 객체든 접근 가능 → 첫 열 전부 ✔</text>
    <text x="20" y="244">• 반대는 없다: 바이트 배열 객체를 uint32_t lvalue 로 읽으면 위반 (DMA·패킷 버퍼)</text>
    <text x="20" y="266">• 해결: 비트를 다른 객체로 복사 — memcpy (C/C++), union (C 만). 복사는 컴파일러가 지운다</text>
  </g>
</svg>
```

이번 문제는 이 규칙 안에서 **같은 일을 안전하게** 하는 함수들을 만든다. starter/solution 은 UB 가 없는 프로그램이고, UB 가 실제로 무엇을 하는지는 데모 파일 세 개로 관찰한다.

### Part A — float ⇄ 비트, 버퍼 ⇄ uint32_t

- `uint32_t float_bits(float f)` / `float bits_to_float(uint32_t u)` — 객체 표현(object representation)을 그대로 옮긴다. `-0.0f` → `0x80000000`, `FLT_TRUE_MIN` → `0x00000001`.
- `uint32_t load_u32(const void *p)` / `void store_u32(void *p, uint32_t v)` — `p` 는 **정렬 보장 없음**, 호스트 바이트 순서. 버퍼의 선언된 타입은 `uint8_t[]` 라고 가정한다 (엔디언 독립 버전은 C-M05, packed 구조체·정렬 폴트는 C-M04).

### Part B — IEEE-754 binary32 필드 (면접관이 파고드는 가장자리)

- `fp32_decode(f)` → `{sign, exp, frac}`, `fp32_encode(sign, exp, frac)` (범위 밖 입력은 마스크), `fp32_classify(f)` → ZERO / SUBNORMAL / NORMAL / INF / NAN — `<math.h>` 없이 비트만으로.
- `-0.0`, 서브노멀(subnormal), `FLT_MIN`, `FLT_MAX`, ±inf, quiet/signaling NaN 이 전부 맞아야 하고, 무작위 비트 패턴 10000개에서 libm `fpclassify` 와 일치해야 한다.

### Part C — DMA 버퍼의 IMU 프레임 + 빠른 역제곱근

- `imu_parse(frame, len, &out)` — 18바이트 프레임 `[0xA5][u32 ts][f32 ax][f32 ay][f32 az][xor]`. 프레임은 `static uint8_t rx_dma[32]` 의 **홀수 주소**에서 시작하므로 필드가 전부 비정렬이다. sync·길이·체크섬·NaN/Inf 를 검사하고, 실패하면 `out` 을 건드리지 않는다.
- `fast_rsqrt(x)` — Quake III 의 `0x5F3759DF` 트릭을 aliasing-안전하게. 상대 오차 < 0.2%. 주어진 `vec3_normalize` 가 이것을 쓴다.

관찰 과제: `demo_alias_break.c` 로 `-O0` vs `-O2` 결과 차이, `demo_punning_asm.c` 로 세 방식의 asm 비교(arm64·Cortex-M4F·Cortex-M0), `demo_quake_rsqrt.c` 로 원본 Quake 코드가 64비트 호스트에서 무엇이 되는지 본다.

## 2. Problem (English)

> **Strict aliasing and safe type punning.**
>
> C11 6.5p7 says an object may only be accessed through an lvalue of a compatible type (modulo signedness/qualifiers), an aggregate/union containing such a type, or a character type. Implement the following without violating it (no `*(T *)&x` reinterpreting casts):
>
> 1. `uint32_t float_bits(float f)`, `float bits_to_float(uint32_t u)`, `uint32_t load_u32(const void *p)`, `void store_u32(void *p, uint32_t v)` — `p` may be misaligned; host byte order.
> 2. `fp32_fields fp32_decode(float)`, `float fp32_encode(unsigned sign, unsigned exp, uint32_t frac)` (inputs are masked to 1/8/23 bits), `fp_class fp32_classify(float)` returning ZERO, SUBNORMAL, NORMAL, INF or NAN using only the bit pattern.
> 3. `bool imu_parse(const uint8_t *frame, size_t len, imu_sample *out)` for the 18-byte frame `A5 | ts:u32 | ax:f32 | ay:f32 | az:f32 | xor8`, little-endian, fields at offsets 1/5/9/13. Reject short frames, bad sync, bad checksum and non-finite floats; leave `*out` untouched on failure. `float fast_rsqrt(float x)` — the `0x5F3759DF` approximation plus one Newton step, relative error < 0.2% for positive normal `x`.
>
> Then: show a function that returns a different value at `-O2` than at `-O0` because of strict aliasing; explain why a union is fine in C but not in C++; prove with assembly that the `memcpy` version costs nothing.
>
> Examples: `float_bits(-0.0f) == 0x80000000`, `fp32_decode(0.1f) == {0, 123, 0x4CCCCD}`, `fp32_classify(FLT_MIN / 2) == SUBNORMAL`, `fast_rsqrt(4.0f) ≈ 0.4992`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk    # 27.2 SDK 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 줄이 41개, "9/50 passed" — TODO 를 채워 50/50 을 만든다
```

관찰용 데모 (하네스와 별개, **일부러 UB 포함** → `-Werror` 없이):

```bash
for o in -O0 -O2 "-O2 -fno-strict-aliasing"; do cc -std=c11 $o demo_alias_break.c -o ab && echo "== $o" && ./ab; done
clang -std=c11 -O2 -S demo_punning_asm.c -o - | less                                             # arm64
clang --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mthumb -O2 -S demo_punning_asm.c -o -
clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2 -S demo_punning_asm.c -o -          # 비정렬 불가 코어
for o in -O0 -O2; do cc -std=c11 $o demo_quake_rsqrt.c -o q && echo "== $o" && ./q; done
```

## 4. 힌트

<details>
<summary>힌트 1 — 캐스트 말고 "복사"</summary>

비트를 다른 타입으로 보고 싶으면 **그 타입의 새 객체에 바이트를 복사**한다: `uint32_t u; memcpy(&u, &f, sizeof u);`. `memcpy` 는 바이트(문자 타입) 복사로 정의되므로 6.5p7 에 걸리지 않고, 정렬도 요구하지 않는다. "함수 호출이라 느리다" 는 걱정은 풀이의 asm 을 보면 사라진다.

</details>

<details>
<summary>힌트 2 — 필드 분해는 C-B04 의 GET_FIELD</summary>

비트를 `uint32_t` 로 얻은 뒤에는 순수 정수 연산이다: sign = `u >> 31`, exp = `(u >> 23) & 0xFF`, frac = `u & 0x7FFFFF`. 조립은 마스크 → 시프트 → OR. 시프트 전에 `(uint32_t)` 로 넓혀야 `1 << 31` 류 UB 가 없다 (C-B03).

</details>

<details>
<summary>힌트 3 — 분류는 exp 두 값과 frac == 0 여부</summary>

exp == 0 이면 0 또는 서브노멀, exp == 255 이면 inf 또는 NaN, 나머지는 정규수. 어느 쪽인지는 frac 이 0 인지로 갈린다. 부호는 분류와 무관하다 (`-0.0` 도 ZERO).

</details>

<details>
<summary>힌트 4 — Part C: 오프셋 + load_u32 + bits_to_float</summary>

`s.ax = bits_to_float(load_u32(frame + 5));` — `frame + 5` 는 홀수 주소지만 `load_u32` 가 정렬을 요구하지 않는다. 출력은 지역 변수에 다 채우고 검사를 통과한 뒤에 `*out = s;` 로 한 번에 쓴다. `fast_rsqrt` 는 `i = 0x5F3759DFu - (i >> 1)` 를 **부호 없는** 정수로 계산한다.

</details>

## 5. 풀이

### 규칙을 정확히 — 6.5p6 과 6.5p7

**6.5p6 유효 타입(effective type)**: 접근할 때 객체의 "진짜 타입" 이다.

| 객체 | 유효 타입 |
|---|---|
| 선언된 객체 (`float f;`, `uint8_t rx[64];`, `static` DMA 버퍼) | **선언된 타입** — 영원히 바뀌지 않는다 |
| 선언된 타입이 없는 객체 (`malloc` 메모리) | 비문자 타입 lvalue 로 **저장**하면 그 타입이 된다. `memcpy`/`memmove`/문자 배열로 복사하면 원본의 유효 타입을 물려받는다. 그 외 접근은 접근하는 lvalue 타입 |

**6.5p7 허용 목록**: 유효 타입이 T 인 객체의 값은 다음 타입의 lvalue 로만 접근할 수 있다 (각주 88: "이 목록의 목적은 객체가 aliasing 될 수 있는 경우를 정하는 것").

1. T 와 호환되는(compatible) 타입 — `const`/`volatile` 한정 버전 포함
2. T 에 대응하는 signed/unsigned 타입 (`int` ↔ `unsigned int`, 한정 버전 포함)
3. 위 타입을 멤버로 (재귀적으로) 포함하는 aggregate 또는 union 타입
4. **문자 타입** (`char`, `signed char`, `unsigned char`)

목록에 "T 와 크기가 같은 아무 정수" 는 없다. 그래서:

| 코드 | 판정 | 이유 |
|---|---|---|
| `*(uint32_t *)&f` (`float f`) | ✘ UB | float 객체를 uint32_t lvalue 로 |
| `((unsigned char *)&f)[i]` | ✔ | 4번: 문자 타입은 어떤 객체든 |
| `*(uint32_t *)&rx[1]` (`uint8_t rx[64]`) | ✘ UB | 유효 타입 `uint8_t` 객체를 uint32_t 로 — **반대 방향은 없다**. 게다가 비정렬(6.3.2.3p7, C-M04) |
| `*(unsigned *)&i` (`int i`) | ✔ | 2번: 부호 대응 |
| `*(uint32_t *)&ul` (`unsigned long ul`, 둘 다 32비트) | ✘ UB | 크기가 같아도 다른 타입. GCC arm-none-eabi 에서 `uint32_t` 는 `long unsigned int` 로 알려져 있어 `unsigned int *` 와 섞으면 위반이 된다 (이 맥에는 gcc 가 없어 실측 못 함 — clang 은 thumbv7em 에서도 `unsigned int` 로 정의, `-dM -E` 로 확인) |
| `memcpy(&u, &f, 4)` | ✔ | 바이트 복사. 새 객체 `u` 를 `uint32_t` 로 읽는다 |

> "문자 타입" 은 `char`/`signed char`/`unsigned char` 셋뿐이다. `uint8_t` 는 gcc/clang 전부 `unsigned char` 의 typedef 라 실무에선 같지만 표준 보장은 아니다 (C-B05·C-B13 참고). C++ 은 `char`, `unsigned char`, `std::byte` 만 허용하고 **`signed char` 는 빠진다**.

### 실측 1 — `-O2` 가 결과를 바꾼다 (demo_alias_break.c)

```c
__attribute__((noinline)) int f(int *i, float *fp) { *i = 1; *fp = 0.0f; return *i; }
// main: int obj = 0;  f(&obj, (float *)&obj);
```

같은 주소를 두 타입으로 넘겼다. 실제 출력 (Apple clang 21, arm64):

```text
== -O0
1) f(&obj, (float*)&obj) = 0   (memory now 0x00000000)
2) patch_len -> 0x11113333  (memory now 0x11113333)
3) patch_len_may_alias -> 0x11113333
4) patch_len_memcpy    -> 0x11113333
5) flip_sign(3.5) = 0
== -O2
1) f(&obj, (float*)&obj) = 1   (memory now 0x00000000)
2) patch_len -> 0x11112222  (memory now 0x11113333)
3) patch_len_may_alias -> 0x11113333
4) patch_len_memcpy    -> 0x11113333
5) flip_sign(3.5) = 0
== -O2 -fno-strict-aliasing
1) f(&obj, (float*)&obj) = 0   (memory now 0x00000000)
2) patch_len -> 0x11113333  (memory now 0x11113333)
...(3~5 동일)
```

`-O2` 에서 **메모리는 0인데 함수는 1을 반환**한다. `-O1` 도 `-O2` 와 같았고, `-O0 -fstrict-aliasing` 은 `-O0` 과 같았다 (clang 은 최적화를 켤 때만 TBAA 를 쓴다. GCC 는 `-O2`/`-O3`/`-Os` 에서 `-fstrict-aliasing` 이 켜진다). asm 이 이유를 보여준다:

```text
_f:  (arm64 -O2)                     f:  (thumbv7em -O2)              f:  (thumbv7em -O2 -fno-strict-aliasing)
  mov  w8, #1                          movs r2, #1                      movs r2, #1
  str  w8, [x0]      ; *i = 1          str  r2, [r0]                    str  r2, [r0]
  str  wzr, [x1]     ; *fp = 0.0f      movs r2, #0                      movs r2, #0
  mov  w0, #1        ; return 1 (상수!) movs r0, #1    ; return 1        str  r2, [r1]
  ret                                  str  r2, [r1]                    ldr  r0, [r0]   ; 다시 읽는다
                                       bx   lr                          bx   lr
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">f(&amp;obj, (float *)&amp;obj) — 같은 4바이트, 두 개의 타입</text>
  <rect x="10" y="40" width="350" height="160" rx="8" class="f-ok" stroke="currentColor"/>
  <rect x="380" y="40" width="350" height="160" rx="8" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="25" y="64" font-weight="bold">-O0 / -fno-strict-aliasing</text>
    <text x="25" y="90" font-family="monospace">*i  = 1     → mem = 0x00000001</text>
    <text x="25" y="112" font-family="monospace">*fp = 0.0f  → mem = 0x00000000</text>
    <text x="25" y="134" font-family="monospace">return *i   → ldr  → 0</text>
    <text x="25" y="166">"쓴 뒤에는 메모리에서 다시 읽는다"</text>
    <text x="25" y="186">→ 소스를 문자 그대로 실행한 결과</text>
    <text x="395" y="64" font-weight="bold">-O1 이상 (TBAA)</text>
    <text x="395" y="90" font-family="monospace">*i  = 1     → mem = 0x00000001</text>
    <text x="395" y="112" font-family="monospace">*fp = 0.0f  → mem = 0x00000000</text>
    <text x="395" y="134" font-family="monospace">return *i   → mov w0, #1</text>
    <text x="395" y="166">"float 쓰기는 int 객체를 못 바꾼다(6.5p7)</text>
    <text x="395" y="186">→ *i 는 아직 1" — 읽기 생략, 상수 반환</text>
  </g>
  <text x="10" y="226" fill="currentColor">컴파일러는 틀리지 않았다: 두 포인터가 같은 객체를 가리킨 순간 프로그램이 이미 UB 다.</text>
  <text x="10" y="244" fill="currentColor">현실의 버그 모양: "패킷 헤더를 uint16_t* 로 고친 뒤 uint32_t* 로 읽었더니 옛 값" (②).</text>
</svg>
```

②(`patch_len`) 가 펌웨어 버전이다 — `uint32_t *` 로 헤더 워드를 쓰고 `uint16_t *` 로 길이 필드를 패치한 뒤 다시 워드로 읽으면 `-O2` 에서 **패치 전 값** `0x11112222` 를 돌려받는다 (메모리는 `0x11113333`). ③ `may_alias` 타입과 ④ `memcpy` 는 `-O2` 에서도 `strh` 다음에 `ldr w0, [x0]` 로 다시 읽어 정답을 낸다.

⑤ 는 반대 교훈이다: `*(uint32_t *)x ^= 0x80000000u` 는 위반인데도 결과가 맞았다. asm 을 보면 clang 이 이 패턴을 `fneg` 로 **알아보고** `before + (-before)` 를 `fsub s0, s0, s0` 로 바꿨다. "내 컴파일러에선 잘 된다" 는 UB 가 아니라는 증거가 아니다 — 다음 버전, 다른 인라이닝, 다른 타깃에서 ①② 처럼 바뀔 수 있다.

**새니타이저는 이것을 못 잡는다.** `-g -O1 -fsanitize=address,undefined` 빌드는 아무 보고 없이 `-O0` 과 같은 결과(0, 0x11113333)를 찍었다 — 계측 때문에 최적화가 달라져 버그가 숨기까지 했다. ASan/UBSan 은 aliasing 을 검사하지 않는다. clang 20 부터 `-fsanitize=type` (TypeSanitizer) 가 생겼지만, 이 맥의 Command Line Tools 에서는 컴파일은 되고 링크에서 `libclang_rt.tysan_osx_dynamic.dylib` 가 없어 실패했다. clang 은 `-Wstrict-aliasing` 도 받기만 하고 경고를 내지 않았다 (GCC 는 `-Wall` 에서 단순한 `*(int *)&f` 류에 "dereferencing type-punned pointer will break strict-aliasing rules" 를 낸다 — 여기선 gcc 가 없어 실측 못 함). **코드 리뷰와 규칙 숙지가 유일한 방어선**이라는 뜻이다.

### Part A 풀이 — 세 방식 비교와 asm

```c
uint32_t bits_cast(float f)   { return *(uint32_t *)&f; }                                   // UB
uint32_t bits_union(float f)  { union { float f; uint32_t u; } x = { .f = f }; return x.u; } // C OK, C++ UB
uint32_t bits_memcpy(float f) { uint32_t u; memcpy(&u, &f, sizeof u); return u; }           // C/C++ OK
```

`demo_punning_asm.c` 의 실제 출력 (`.cfi`/디렉티브 생략):

| 함수 | arm64 `-O2` | Cortex-M4F (eabihf) `-O2` | Cortex-M4 (soft-float ABI) | Cortex-M0 |
|---|---|---|---|---|
| `bits_cast` | `fmov w0, s0` | `vmov r0, s0` | `bx lr` (float 가 이미 r0) | — |
| `bits_union` | `fmov w0, s0` | `vmov r0, s0` | `bx lr` | — |
| `bits_memcpy` | `fmov w0, s0` | `vmov r0, s0` | `bx lr` | — |
| `float_from_memcpy` | `fmov s0, w0` | `vmov s0, r0` | `bx lr` | — |
| `load_cast(p)` | `ldr w0, [x0]` | `ldr r0, [r0]` | `ldr r0, [r0]` | `ldr r0, [r0]` (비정렬이면 HardFault) |
| `load_memcpy(p)` | `ldr w0, [x0]` | `ldr r0, [r0]` | `ldr r0, [r0]` | `ldrb` ×4 + `lsls`/`adds` |

세 방식은 **명령어 단위로 똑같다** — 그러니 벤치마크할 필요도 없다. `memcpy(&u, &f, 4)` 는 컴파일러에게 "4바이트를 옮겨라" 일 뿐이고, 크기가 상수인 `memcpy` 는 내장 함수로 처리되어 레지스터 이동 하나가 된다. 차이는 Cortex-M0 줄에서 드러난다: M0 는 비정렬 `LDR` 이 HardFault 이므로 컴파일러가 `memcpy` 를 **바이트 로드 4개**로 만들어 안전하게 처리하고, 캐스트 버전은 그대로 `ldr` 을 내서 홀수 주소면 죽는다 (정렬 자체는 C-M04 에서 자세히). M3/M4 는 일반 `LDR` 의 비정렬 접근을 하드웨어가 허용하므로 `ldr` 하나다.

`-O0` 에서는? `bits_memcpy` 는 `-O0` 에서도 `memcpy` 호출이 아니라 스택 경유 `str s0` → `ldr w0` 4개 명령이었다 (실측). 디버그 빌드에서도 함수 호출 비용은 없다.

| 방식 | C11 | C++ | 정렬 | 비고 |
|---|---|---|---|---|
| 포인터 캐스트 `*(T *)&x` | **UB** (6.5p7) | **UB** ([basic.lval]) | 요구함 | `-fno-strict-aliasing` 이면 GCC/clang 이 "기대대로" 처리 (확장) |
| union 멤버 바꿔 읽기 | **정의됨** — 6.5.2.3p3 각주 95: 마지막에 쓴 멤버와 다른 멤버로 읽으면 객체 표현을 새 타입으로 재해석 ("type punning"), 트랩 표현일 수는 있음 | **UB** — 활성 멤버(active member)가 아닌 멤버 읽기는 수명 밖 객체 접근 | 해당 없음 | GCC 는 C++ 에서도 확장으로 허용한다고 문서화. 단 **union 멤버 접근식으로 직접** 읽을 때만 (`int *p = &u.i; *p` 처럼 포인터를 거치면 보장 없음) |
| `memcpy` | 정의됨 (6.5p6, 6.2.6.1p4) | 정의됨 (trivially copyable) | 요구 안 함 | 상수 크기면 명령 1개. C++20 은 `std::bit_cast` |

> C 에서 union punning 이 "허용" 인 근거가 각주라 애매하다는 논쟁이 있다. C99 원문에는 없었고 TC3 에서 각주가 추가됐고(C99 각주 82 → C11 각주 95), Annex J.1 에는 아직 "마지막으로 저장한 멤버가 아닌 멤버의 바이트 값은 unspecified" 라는 옛 문구가 남아 있다. 결론: C 에서 union 은 쓸 수 있고 gcc/clang 이 보장하지만, **C/C++ 공용 헤더라면 `memcpy`** 가 유일하게 양쪽에서 합법이다.

`load_u32` 도 같다: `memcpy(&v, p, 4)` 하나로 aliasing 과 정렬 둘 다 해결된다. 하네스의 `ref_load` 는 반대 방향 — `uint32_t e` 객체에 `unsigned char *` 로 바이트를 써 넣는 것 — 이 합법이라는 것을 이용해 기대값을 독립적으로 만든다.

### Part B 풀이 — IEEE-754 binary32

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">binary32: 0.1f = 0x3DCCCCCD</text>
  <rect x="20" y="40" width="40" height="34" class="f-warn" stroke="currentColor"/>
  <rect x="60" y="40" width="200" height="34" class="f-acc" stroke="currentColor"/>
  <rect x="260" y="40" width="460" height="34" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="34" y="62" font-family="monospace">0</text>
    <text x="110" y="62" font-family="monospace">0111 1011</text>
    <text x="330" y="62" font-family="monospace">100 1100 1100 1100 1100 1101</text>
    <text x="22" y="92">bit31</text>
    <text x="70" y="92">bit30..23  exp = 123</text>
    <text x="270" y="92">bit22..0  frac = 0x4CCCCD</text>
    <text x="20" y="124">exp = 1..254  정규수     (-1)^s × 1.frac × 2^(exp-127)    0.1f = 1.6000000238 × 2^-4</text>
    <text x="20" y="146">exp = 0       frac = 0 → ±0,   frac ≠ 0 → 서브노멀 (-1)^s × 0.frac × 2^-126</text>
    <text x="20" y="168">exp = 255     frac = 0 → ±inf, frac ≠ 0 → NaN (bit22 = 1 quiet, 0 signaling)</text>
    <text x="20" y="196">FLT_TRUE_MIN = 0x00000001   FLT_MIN = 0x00800000   1.0f = 0x3F800000   FLT_MAX = 0x7F7FFFFF</text>
    <text x="20" y="218">-0.0f = 0x80000000 (0.0f 와 비트가 다름)   +inf = 0x7F800000   qNaN = 0x7FC00000</text>
  </g>
</svg>
```

비트를 `uint32_t` 로 꺼낸 다음은 C-B04 의 필드 추출 그대로다. 분류 결정표:

| exp | frac == 0 | frac ≠ 0 |
|---|---|---|
| 0 | ZERO (±0) | SUBNORMAL |
| 1..254 | NORMAL | NORMAL |
| 255 | INF (±inf) | NAN |

면접관이 파고드는 가장자리와 테스트가 확인하는 것:

| 입력 | 함정 | 테스트 |
|---|---|---|
| `-0.0f` | `== 0.0f` 는 참이라 값 비교로는 못 구분 | `float_bits(-0.0f) == 0x80000000`, decode → sign 1 |
| `FLT_MIN / 2` | 정규수 최솟값 아래 → 서브노멀 (Cortex-M4F 는 FPSCR.FZ 를 켜면 0 으로 flush) | `== FPC_SUBNORMAL` |
| `0x7F800001` | signaling NaN — frac 최상위 비트가 0 이어도 NaN | `== FPC_NAN` |
| 무작위 비트 10000개 | 1/4 은 exp 를 0 으로, 1/7 은 255 로 강제해 서브노멀·inf·NaN 을 섞음 | libm `fpclassify` 와 10000/10000 일치, decode→encode 비트 왕복 10000/10000 |
| `fp32_encode(2, 0x17F, 0xFF800000)` | 넘치는 입력이 이웃 필드를 오염 | 마스크 → `1.0f` |

> 비교는 **비트로** 한다. `NaN != NaN` 이라 `f == g` 로는 NaN 왕복을 검사할 수 없고, `-0.0f == 0.0f` 라 부호도 못 본다. Part A 의 왕복 테스트가 NaN 비트 패턴을 건너뛰는 이유도 같다 — 일부 ABI(x87)는 sNaN 을 레지스터로 옮기며 quiet 로 바꾼다.

### Part C 풀이 — 펌웨어 시나리오

`rx_dma` 는 `static uint8_t rx_dma[32]` — **선언된 타입이 `uint8_t` 배열**이다. 그래서 `*(uint32_t *)(rx_dma + 4)` 는 정렬과 무관하게 aliasing 위반이다 (정렬까지 맞으면 `-O0` 에서는 "잘 돈다" 가 함정). 프레임이 홀수 주소에서 시작하므로 정렬도 틀린다. `load_u32` + `bits_to_float` 조합은 둘 다 해결한다:

```c
s.ts = load_u32(frame + 1);
s.ax = bits_to_float(load_u32(frame + 5));   // arm64/M4: ldr + fmov(vmov) 2개
```

설계 포인트: ① 길이 검사를 `frame[0]` 읽기보다 먼저 ② 체크섬을 **필드 해석 전에** ③ 결과는 지역 `s` 에 채우고 마지막에 `*out = s` — 실패 경로에서 호출자의 이전 샘플이 반쯤 덮이는 일이 없다 (테스트가 `keep.ts == 7` 로 확인) ④ NaN/Inf 는 Part B 의 `fp32_classify` 로 거른다. 와이어 형식이 리틀 엔디언이고 호스트(arm64)와 Cortex-M 이 둘 다 리틀이라 `load_u32` 로 충분하다. 빅 엔디언 호스트까지 가려면 C-M05 의 `get_le32` 를 쓴다.

**빠른 역제곱근.** `demo_quake_rsqrt.c` 는 1999년 원본(`long i = *(long *)&y;`)을 64비트 맥에서 돌린다:

```text
== -O0
sizeof(long) = 8
q_long(4) = 0.499154   (첫 호출)
q_safe(4) = 0.499154
q_i32(4)  = 0.499154
q_long(4) = 0.499154   (q_i32 다음 호출)
== -O2
sizeof(long) = 8
q_long(4) = 3.95253e-323   (첫 호출)
q_safe(4) = 0.499154
q_i32(4)  = 0.499154
q_long(4) = 0.499154   (q_i32 다음 호출)
```

`-O2` 에서 `q_long` 의 본문은 `ret` 한 줄이 됐고, `main` 은 **`q_long` 을 아예 호출하지 않았다** (asm 의 `main` 에 `bl _q_long` 이 없다). 4바이트 float 객체를 8바이트로 읽는 것은 범위 밖 접근이라 결과가 "정의 안 됨" 으로 접혔고, printf 에는 스택에 남아 있던 값이 넘어갔다: `3.95253e-323` 은 직전 printf 에 넘긴 정수 `8`(sizeof) 의 비트를 double 로 읽은 값이고, 두 번째 `0.499154` 는 직전 `q_i32` 결과가 **우연히 남아 있던 것**이다 — 맞는 답처럼 보이는 쓰레기. 32비트 Cortex-M4 에서는 `long` 이 4바이트라 같은 원본이 `sub.w r0, r1, r0, asr #1` 로 "정상" 컴파일됐다 (`demo_punning_asm.c` 의 `rsqrt_quake`) — 타깃을 바꾸는 순간 터지는 코드다.

새니타이저는 이번엔 잡는다 — 단, **크기** 문제만:

```text
demo_quake_rsqrt.c:16:9: runtime error: load of address 0x00016d18ab00 with insufficient space for an object of type 'long'
==54715==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x00016d18ab00 ...
READ of size 8 at 0x00016d18ab00 thread T0
    #0 0x000102c749bc in q_long demo_quake_rsqrt.c:16
```

크기를 맞춘 `q_i32` (`int32_t`) 는 여전히 6.5p7 위반이지만 보고가 없다. 안전한 버전(`q_safe`/`fast_rsqrt`)은 arm64 에서 `q_i32` 와, Cortex-M4F 에서 원본 `rsqrt_quake` 와 같은 명령들로 컴파일된다 (`fmov w8, s0 ; sub w8, w9, w8, lsr #1 ; fmov s0, w8 ...`). 차이는 `asr`(부호 있는 시프트) → `lsr` 하나뿐이고, 양수 입력에선 결과가 같다. 즉 안전하게 고쳐도 비용은 0 이다.

정확도는 [1, 4) 의 모든 float 를 전수 측정했다 (지수가 2 올라가면 패턴이 반복되므로 이 구간이 전체를 대표):

| 매직 상수 | 뉴턴 0회 | 뉴턴 1회 | 뉴턴 2회 |
|---|---|---|---|
| `0x5F3759DF` (Quake) | 3.4376% | **0.1752%** | 0.000472% |
| `0x5F375A86` (Lomont) | 3.4365% | 0.1751% | 0.000472% |

테스트의 `< 0.2%` 는 이 실측 최댓값 0.1752% 에 여유를 둔 값이다. 실무 판단: Cortex-M4F 는 `VSQRT.F32`/`VDIV.F32` 가 각 14 사이클이라 `1.0f / sqrtf(x)` 가 충분히 빠르고 정확하다. 단 asm 을 보면 `rsqrt_fpu` 에 `vcmp`/`bvs` → `bl sqrtf` 경로가 붙는데, 음수 입력에서 `errno` 를 세우기 위한 것이다 — `-fno-math-errno` 로 없앤다. FPU 없는 M0/M0+ 에서는 트릭이 여전히 의미 있다.

### 탈출구 — `-fno-strict-aliasing` 과 `may_alias`

| 방법 | 효과 | 쓰는 곳 |
|---|---|---|
| `-fno-strict-aliasing` | TBAA 를 끈다 → ①② 가 `-O2` 에서도 0 / `0x11113333` (실측) | **Linux 커널은 최상위 Makefile 에서 항상 이 옵션으로 빌드한다**. 네트워크 헤더 캐스트가 너무 많고, Linus 는 이 규칙 자체를 여러 번 공개적으로 비판했다. 오래된 펌웨어 코드베이스에도 흔하다 |
| `__attribute__((may_alias))` 타입 | 그 타입의 lvalue 는 문자 타입처럼 취급 → ③ 이 정답 (실측) | 커널·라이브러리 내부 헬퍼, GCC 문서의 예 |
| `memcpy` | 표준만으로 안전, 비용 0 | **새 코드의 기본값** |
| union (C) | 표준상 정의됨 | 레지스터 뷰, 프로토콜 프레임 (`union { uint8_t raw[16]; struct hdr h; }`) |

`-fno-strict-aliasing` 의 대가는 성능이다. 반대로 생각하면 **문자 타입 쓰기도 같은 대가를 낸다**: `uint8_t *out` 에 쓰는 루프에서 컴파일러는 그 쓰기가 무엇이든 바꿀 수 있다고 가정한다. `demo_punning_asm.c` 의 `fill8`/`fill16` (Cortex-M4, `-O2 -fno-unroll-loops`) 실측:

```text
fill8 루프 (uint8_t *out):          fill16 루프 (uint16_t *out):
  ldr  r3, [r0, #4]   ; c->val 재로드     subs  r2, #1
  strb r3, [r1, r2]                       strh  r0, [r1, #2]!
  ldr  r3, [r0]       ; c->n 재로드       bne   .LBB11_2
  adds r2, #1                           (c->val, c->n 은 루프 밖에서 한 번만)
  cmp  r2, r3
  blo  .LBB10_2
```

`uint16_t` 쓰기는 `uint32_t` 필드를 못 바꾼다는 TBAA 덕에 `fill16` 은 3명령 루프인데, `fill8` 은 매 바이트마다 구조체 필드 두 개를 다시 읽는다. 고치려면 `uint32_t n = c->n, v = c->val;` 처럼 지역 변수로 복사하거나 `restrict` 를 쓴다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 50/50 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 50/50, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `uint32_t u = *(uint32_t *)&f;` | float 객체를 uint32_t lvalue 로 — 6.5p7 위반. 지금은 `fmov` 로 잘 나와도 인라이닝 후 ①처럼 읽기가 사라질 수 있다 | `memcpy(&u, &f, sizeof u);` |
| `uint32_t v = *(uint32_t *)(rx + 5);` | `uint8_t` 배열 객체를 uint32_t 로 (반대 방향은 없다) + 비정렬 (M0 HardFault) | `load_u32(rx + 5)` (memcpy) |
| "uint8_t* 로 읽는 건 되니까 uint32_t* 로도 되겠지" | 문자 타입 허용은 **한 방향**뿐 | 넓은 타입 객체로 선언하고 바이트로 보기, 또는 memcpy |
| `long i = *(long *)&y;` (Quake 원본) | LP64 에서 long 은 8바이트 → 범위 밖 읽기. `-O2` 에서 함수가 `ret` 으로 접힘 (실측) | 고정 폭 `uint32_t` + memcpy |
| C/C++ 공용 헤더에서 union punning | C++ 에서는 UB, 복합 리터럴 매크로 `F2U` 는 clang++ 에서 컴파일조차 안 됨 (연습 5) | `memcpy`, C++20 `std::bit_cast` |
| `int *p = &u.i; ... *p` 로 union 을 우회 | GCC 가 보장하는 punning 은 **union 멤버 접근식**으로 읽을 때뿐 | `u.i` 로 직접 읽기 |
| `uint32_t` 와 `unsigned long` 이 같은 32비트라 섞어 씀 | 크기 같아도 다른 타입 (arm-none-eabi GCC 의 `uint32_t` = `long unsigned int`) | 한 가지 타입으로 통일, 필요하면 memcpy |
| ASan/UBSan 이 조용하니 aliasing 도 OK | 둘 다 aliasing 을 검사하지 않는다 (demo_alias_break 실측: 보고 0건) | 코드 리뷰 + 규칙. GCC `-Wstrict-aliasing`, 가능하면 `-fsanitize=type` |
| `f == g` 로 float 왕복 검사 | NaN 은 자기 자신과도 다르고, `-0.0 == 0.0` | 비트(`float_bits`)로 비교 |

## 8. 면접 Q&A

<details>
<summary>Q1. strict aliasing rule 을 한 문장으로 설명하고, 왜 존재하나요?</summary>

"객체는 자기 유효 타입과 호환되는 타입(부호·한정자 차이는 허용), 그것을 포함한 aggregate/union, 또는 문자 타입의 lvalue 로만 접근할 수 있다" (C11 6.5p7). 존재 이유는 최적화다: 컴파일러가 `int *` 쓰기와 `float *` 읽기가 겹치지 않는다고 가정할 수 있어야 값을 레지스터에 유지하고, 루프에서 로드를 끌어올리고, 벡터화할 수 있다. 꼬리질문 "그 가정이 깨지면?" → `f(int*, float*)` 가 메모리는 0인데 1을 반환한 실측 (①).

</details>

<details>
<summary>Q2. char 포인터로는 아무거나 읽어도 되는데, 왜 uint8_t 버퍼를 uint32_t* 로 읽으면 안 되나요?</summary>

허용 목록이 "**lvalue** 가 문자 타입일 때" 만 있기 때문이다. `((unsigned char *)&x)[i]` 는 lvalue 가 문자 타입 → OK. `*(uint32_t *)buf` 는 lvalue 가 `uint32_t`, 객체의 유효 타입은 `uint8_t` → 목록에 없음 → UB. 거기에 정렬(6.3.2.3p7) 문제가 더해진다. 해결: `memcpy`, 또는 버퍼를 넓은 타입(`uint32_t buf[N]`)으로 선언하고 바이트가 필요할 때 `unsigned char *` 로 보기 — 이 방향은 합법이다. 꼬리질문 "`malloc` 버퍼는?" → 선언된 타입이 없어 유효 타입이 저장/접근 lvalue 로 정해진다(6.5p6). DMA 가 채운 `malloc` 버퍼를 `uint32_t` 로만 읽으면 문자 그대로는 위반이 아니지만, 정렬은 여전히 문제이고 같은 버퍼를 여러 타입으로 섞어 쓰면 다시 위반이다. 습관적으로 memcpy 가 안전하다.

</details>

<details>
<summary>Q3. union 으로 float 비트를 보는 건 합법인가요? C++ 에서는?</summary>

C: 합법. C11 6.5.2.3p3 각주 95 — 마지막에 쓴 멤버와 다른 멤버로 읽으면 객체 표현이 새 타입으로 재해석된다(트랩 표현일 가능성만 있음). C++: UB — union 은 한 번에 한 멤버만 활성(active)이고, 비활성 멤버를 읽는 것은 수명이 시작되지 않은 객체 접근이다. GCC 는 C++ 에서도 확장으로 허용한다고 문서화하지만 표준이 아니다. C++ 정답은 `memcpy` 또는 C++20 `std::bit_cast<uint32_t>(f)` (constexpr 도 됨). 꼬리질문 "C++ 에서 버퍼를 구조체로 보려면?" → `memcpy` 로 복사, C++23 `std::start_lifetime_as<T>(buf)`.

</details>

<details>
<summary>Q4. memcpy 는 함수 호출인데 펌웨어 hot path 에서 써도 되나요?</summary>

된다. 크기가 컴파일 타임 상수인 `memcpy` 는 컴파일러 내장(builtin)으로 처리되어 호출이 사라진다. 실측: `bits_memcpy` → arm64 `fmov w0, s0`, Cortex-M4F `vmov r0, s0`, soft-float ABI 에서는 `bx lr` 만 남음. `load_memcpy` → arm64/M4 `ldr` 하나, M0 는 비정렬 LDR 이 폴트이므로 `ldrb` 4개로 바꿔 줌. 캐스트 버전과 명령어가 같다. 주의: `-ffreestanding`/`-fno-builtin` 으로 빌드하면 내장 처리가 꺼져 진짜 호출이 될 수 있다 — 그때는 `__builtin_memcpy` 를 명시한다 (C-B07 참고).

</details>

<details>
<summary>Q5. Linux 커널은 왜 -fno-strict-aliasing 으로 빌드하나요? 우리 펌웨어도 그래야 하나요?</summary>

커널은 네트워크 헤더·페이지 구조체 등을 타입을 바꿔 보는 코드가 매우 많고, Linus 가 이 규칙이 실제 시스템 코드와 맞지 않는다고 보기 때문에 최상위 Makefile 에서 항상 끈다. 대가는 일부 최적화 손실이다. 펌웨어 팀의 선택지: ① 레거시 코드가 많으면 `-fno-strict-aliasing` 을 켜고 그 사실을 문서화 ② 새 코드는 `memcpy`/union 으로 규칙을 지켜 어느 쪽 옵션에서도 맞게. "옵션으로 덮었으니 코드는 아무렇게나" 는 안 된다 — 같은 코드가 다른 프로젝트·다른 컴파일러로 옮겨 가는 순간 깨진다. MISRA C:2012 도 서로 다른 객체 타입 포인터 간 변환을 금지(Rule 11.3)한다.

</details>

<details>
<summary>Q6. Quake 의 빠른 역제곱근 코드를 리뷰해 보세요.</summary>

① `*(long *)&y` 는 aliasing 위반 ② LP64 에서 `long` 은 8바이트라 범위 밖 읽기 — 실측으로 `-O2` 에서 함수가 `ret` 으로 접혔고 호출 자체가 사라져 쓰레기 값이 출력됨 ③ `i >> 1` 이 부호 있는 시프트 ④ 입력이 0/음수/서브노멀/inf 면 의미 없는 값. 고침: `uint32_t` + `memcpy` 로 비트 이동, 전제(양의 정규수) 명시. 정확도는 뉴턴 1회 후 최대 0.1752% (전수 측정). 꼬리질문 "요즘도 쓰나?" → FPU 있는 M4F/M7 은 `VSQRT`+`VDIV` (각 14 사이클, M4) 가 정확하고 빠르다. arm64/x86 은 전용 근사 명령(`frsqrte`, `rsqrtss`)이 있다. FPU 없는 M0 에선 아직 쓸모 있다.

</details>

<details>
<summary>Q7. 레지스터 구조체 오버레이 `(GPIO_TypeDef *)0x40020000` 은 aliasing 위반인가요?</summary>

아니다. 그 주소에는 C 객체가 선언되어 있지 않다 — 정수 → 포인터 변환은 구현 정의(6.3.2.3p5)이고, 선언된 타입이 없으므로 접근하는 lvalue(`volatile uint32_t` 멤버)가 곧 유효 타입이다. 같은 레지스터를 `*(volatile uint32_t *)0x40020014` 로 접근해도 타입이 같아 문제없다. 위험한 것은 **같은 RAM 객체를 두 타입으로** 보는 경우다: `static uint8_t dma_buf[64]` 를 `struct pkt *` 로 캐스트, 또는 `uint32_t` 레지스터 섀도를 `float *` 로 읽기. 레지스터 쪽은 aliasing 보다 volatile(C-B09)과 접근 폭(C-M03)이 문제다.

</details>

<details>
<summary>Q8. TBAA 때문에 오히려 코드가 느려지는 경우도 있나요?</summary>

있다 — 문자 타입 쓰기. `uint8_t *out` 은 "무엇이든 aliasing 할 수 있다" 이므로 루프 안의 `out[i] = c->val` 이 `c->n`, `c->val` 을 바꿀 수 있다고 가정해 매 반복 다시 읽는다. 실측 (Cortex-M4): `fill8` 루프는 `ldr`/`strb`/`ldr`/`adds`/`cmp`/`blo` 6명령, `fill16` 은 `subs`/`strh`/`bne` 3명령. 해결: 필드를 지역 변수로 복사하거나 `restrict`. UART/DMA 바이트 버퍼를 채우는 드라이버 루프에서 흔히 보이는 비용이다.

</details>

## 9. 추가 연습

### 연습 1 — 부호 비트 연산

`fp_abs`, `fp_neg`, `fp_copysign` 을 비트 연산만으로 만들어라. libm `fabsf`/`copysignf` 와 무작위 비트 10000쌍에서 **비트 단위로** 같아야 한다 (NaN 포함).

<details>
<summary>답 보기</summary>

`abs`: `& 0x7FFFFFFF`, `neg`: `^ 0x80000000`, `copysign`: `(mag & 0x7FFFFFFF) | (sgn & 0x80000000)`. 산술(`x < 0 ? -x : x`)은 `-0.0` 에서 틀리고(`-0.0 < 0` 은 거짓 → `-0.0` 반환) NaN 의 부호를 못 지운다. IEEE-754 도 abs/negate/copySign 을 "비트 연산, 예외 없음" 으로 정의하므로 libm 과 비트가 일치한다 (NaN 페이로드 `0xFFC12345` → `0x7FC12345`).

</details>

### 연습 2 — ULP 거리

`ulp_distance(a, b)` — 두 float 사이에 표현 가능한 float 가 몇 칸인가. `0.1f` 를 10번 더한 값과 `1.0f` 는 몇 ULP 차이인가?

<details>
<summary>답 보기</summary>

float 비트는 부호-크기(sign-magnitude) 표현이라, 양수끼리는 비트를 정수로 보면 순서가 그대로지만 음수는 뒤집혀 있다. 키 = 양수면 `u`, 음수면 `-(u & 0x7FFFFFFF)` 로 바꾸면 `-0` 과 `+0` 이 모두 0이 되고 전체가 단조 증가한다. 차이의 절댓값이 ULP 거리. 결과: `(-FLT_TRUE_MIN, FLT_TRUE_MIN)` = 2, `(FLT_MAX, inf)` = 1, `0.1f×10` 은 `1.0f` 와 같지 않고(`!=`) **1 ULP** 차이 — 그래서 테스트 비교는 `==` 가 아니라 "n ULP 이내" 로 한다.

</details>

### 연습 3 — nextafterf 를 비트로

`fp_next_up(x)` = `nextafterf(x, +INFINITY)`. 양수면 `u + 1`, 음수면 `u - 1` 인데 무엇이 예외인가?

<details>
<summary>답 보기</summary>

예외: NaN → 그대로, `+inf` → 그대로, **`-0.0` → `FLT_TRUE_MIN`** (`0x80000000 - 1` 은 `0x7FFFFFFF` = NaN 이 된다). `FLT_MAX + 1비트` 는 `0x7F800000` = inf 로 자연스럽게 넘어가고, `-FLT_TRUE_MIN` 은 `-0.0` 이 된다. 지수 경계(`0x3FFFFFFF → 0x40000000`)는 비트 +1 이 자동으로 처리한다 — IEEE-754 가 일부러 이렇게 설계됐다. libm 과 비-NaN 10000개 비트 일치.

</details>

### 연습 4 — float32 → bfloat16 (AI 가속기)

bfloat16 은 float32 의 상위 16비트(sign + 8비트 exp + 7비트 frac)다. round-to-nearest-even 으로 변환하라. 단순히 `u >> 16` 하면 무엇이 틀리나?

<details>
<summary>답 보기</summary>

`u += 0x7FFF + ((u >> 16) & 1); return u >> 16;` — 하위 16비트가 절반(0x8000) 미만이면 버림, 초과면 올림, **정확히 절반이면 결과 LSB 가 짝수가 되게**. 올림 캐리가 지수로 넘어가는 것도 자연스럽게 처리되고, `FLT_MAX` 는 `0x7F80` = inf 가 된다 (RNE 의 정상 동작). NaN 은 따로: `0x7F800001` 을 그냥 자르면 `0x7F80` = **inf** 가 되므로 quiet 비트 `0x0040` 을 세운다. 단순 절단은 항상 0 쪽으로 치우쳐 학습/추론 누적 오차가 생긴다. NPU 펌웨어·양자화 툴에서 실제로 쓰는 코드 모양이다.

</details>

### 연습 5 — union 복합 리터럴 매크로

`F2U(x)` / `U2F(x)` 를 union 복합 리터럴(compound literal) 한 줄로 만들어라. C++ 에서는?

<details>
<summary>답 보기</summary>

`#define F2U(x) (((union { float f; uint32_t u; }){ .f = (x) }).u)` — 식 안에서 임시 union 객체를 만들고 다른 멤버로 읽는다. C 에서는 6.5.2.3 각주 95 로 정의된 동작이고 memcpy 버전과 1000개 비트 일치. clang++ (`-std=c++20`) 에 넣으면 실측 `error: '(unnamed union ...)' cannot be defined in a type specifier` 로 **컴파일조차 안 된다** (C++ 에는 복합 리터럴이 표준에 없고, 확장으로 받아도 punning 은 UB). C/C++ 공용 헤더라면 memcpy 인라인 함수가 답이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 26/26 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o pr_san && ./pr_san   # 26/26, 보고 없음
```

## 10. 핵심 정리

- 6.5p7: 객체는 **호환 타입(부호·한정자 차이 OK), 그걸 담은 aggregate/union, 문자 타입** 으로만 접근. 크기가 같은 다른 타입은 목록에 없다.
- 문자 타입 → 무엇이든 OK, **무엇이든 → 바이트 배열 객체는 ✘** (`*(uint32_t *)rx` 는 aliasing + 정렬 위반).
- 실측: `f(int*, float*)` 가 `-O1`/`-O2` 에서 메모리 0 인데 **1 반환**, `-O0`·`-fno-strict-aliasing` 은 0. ASan/UBSan 은 못 잡는다.
- 안전한 punning: `memcpy` (C/C++ 모두), union (C 만 — 각주 95, C++ 은 UB → `std::bit_cast`).
- `memcpy` 는 `-O2` 에서 `fmov`/`vmov`/`ldr` **한 개** — 캐스트와 명령어가 같다. M0 에선 비정렬을 바이트 로드로 바꿔 HardFault 까지 막아 준다.
- IEEE-754: exp 0 → 0/서브노멀, 255 → inf/NaN, `-0.0` 과 NaN 은 **비트로** 비교.
- Quake 원본 `*(long *)&y` 는 LP64 에서 `-O2` 시 함수가 `ret` 으로 사라졌다. `uint32_t` + memcpy, 오차 ≤ 0.1752%.
- 탈출구: `-fno-strict-aliasing` (Linux 커널), `__attribute__((may_alias))`. 새 코드는 memcpy 가 기본.

## 참고자료

- C11 (N1570) 6.5p6–7 유효 타입·aliasing (각주 87·88), 6.5.2.3p3 각주 95 union type punning, 6.2.6.1 타입 표현, 6.3.2.3p5·p7 포인터 변환·정렬, Annex J.1 — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- [GCC — `-fstrict-aliasing` 설명 (union punning 확장 포함)](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html#index-fstrict-aliasing)
- [GCC — `may_alias` 타입 속성](https://gcc.gnu.org/onlinedocs/gcc/Common-Type-Attributes.html)
- [Clang — TypeSanitizer (`-fsanitize=type`)](https://clang.llvm.org/docs/TypeSanitizer.html)
- [cppreference — `std::bit_cast`](https://en.cppreference.com/w/cpp/numeric/bit_cast), [Type-based aliasing (C++)](https://en.cppreference.com/w/cpp/language/reinterpret_cast#Type_aliasing)
- Shafik Yaghmour, ["What is the Strict Aliasing Rule and Why do we care?"](https://gist.github.com/shafik/848ae25ee209f698763cffee272a58f8)
- Chris Lomont, "Fast Inverse Square Root" (2003) — 매직 상수 분석
- ARM Cortex-M4 Technical Reference Manual — FPU 명령 사이클 (VSQRT/VDIV 14 사이클)
- IEEE 754-2019 — binary32 형식, abs/negate/copySign
- 관련 레슨: C-B03 (시프트 UB), C-B04 (필드 추출), C-B05 (문자 포인터 aliasing 예외), C-B07 (freestanding memcpy), C-B09 (volatile), C-B13 (엔디언·union 판별), C-M03 (비트필드 접근 폭), C-M04 (packed·정렬), C-M05 (엔디언 독립 직렬화), C-M12 (UB 카탈로그)
