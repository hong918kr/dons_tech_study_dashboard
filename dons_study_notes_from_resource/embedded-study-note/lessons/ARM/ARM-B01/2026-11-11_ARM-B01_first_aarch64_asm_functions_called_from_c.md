---
id: ARM-B01
time: 60분
updated: 2026-11-11
---
# ARM-B01 첫 AArch64 함수: C에서 호출하는 .S 파일

## 학습 목표

- `.S` 파일에 AArch64 함수를 직접 쓰고 C에서 부른다: 인자는 `x0`–`x7`, 반환은 `x0`, 복귀는 `ret` (= `x30` 으로 점프).
- `int` 인자는 `w` 레지스터로 다룬다. **`x` 의 윗 32비트는 ABI가 보장하지 않는다(unspecified)** 는 것을 실제 버그와 "적대적 호출자" 테스트로 확인한다.
- `w` 레지스터에 쓰면 윗 32비트가 0이 된다는 규칙, `add x0, x0, w1, sxtw` 같은 확장 레지스터 피연산자를 쓸 수 있다.
- Apple arm64 ABI가 표준 AAPCS64와 다른 점(x18 예약, `_` 접두사, frame pointer, red zone, 가변 인자, 작은 정수 확장, `char` 부호, `long double`)을 표로 말하고, 그중 4개를 실제 asm 출력으로 보여 줄 수 있다.
- 크래시 덤프에서 "x0–x7 = 인자, x29 = 프레임, x30 = 복귀 주소" 를 읽는 감각을 패닉 핸들러식 레지스터 스냅숏으로 익힌다.

## 1. 문제

펌웨어 면접의 "이 asm 뭐 하는 코드야?", 크래시 덤프 읽기, 스타트업 코드·ISR 진입부는 전부 **호출 규약(calling convention)** 위에 서 있다. "x0 이 첫 인자, x30 이 복귀 주소" 를 모르면 덤프의 레지스터 한 줄도 해석할 수 없다. SSD 컨트롤러 펌웨어(Cortex-R/A 코어)의 panic 로그, Apple·NVIDIA AArch64 SoC 의 부트 ROM·하이퍼바이저 코드가 다 그렇다.

이 레슨은 ARM 트랙의 **첫 손 코딩**이다. 이 맥(Apple M6, arm64)에서 바로 돌린다. C 하네스가 `extern` 프로토타입으로 asm 함수를 부르고, CHECK 매크로로 결과를 검사한다. 함수는 전부 **leaf** (다른 함수를 부르지 않음)라서 스택 프레임 없이 레지스터만으로 끝난다. 프레임·재귀는 ARM-B08, 주소 지정 모드는 ARM-B04, 조건 플래그는 ARM-B05 가 깊게 다룬다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">AArch64 범용 레지스터 31개의 역할 (AAPCS64 + Apple)</text>
  <g stroke="currentColor">
    <rect x="10" y="38" width="180" height="50" rx="6" class="f-acc"/>
    <rect x="200" y="38" width="70" height="50" rx="6" class="f-mut"/>
    <rect x="280" y="38" width="150" height="50" rx="6" class="f-mut"/>
    <rect x="440" y="38" width="120" height="50" rx="6" class="f-mut"/>
    <rect x="570" y="38" width="160" height="50" rx="6" class="f-warn"/>
    <rect x="10" y="100" width="230" height="50" rx="6" class="f-ok"/>
    <rect x="250" y="100" width="110" height="50" rx="6" class="f-ok"/>
    <rect x="370" y="100" width="110" height="50" rx="6" class="f-acc"/>
    <rect x="490" y="100" width="110" height="50" rx="6" class="f-ok"/>
    <rect x="610" y="100" width="120" height="50" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="100" y="58" font-family="monospace" font-weight="bold">x0 – x7</text>
    <text x="100" y="78" font-size="12">인자 / 반환값 (caller-saved)</text>
    <text x="235" y="58" font-family="monospace" font-weight="bold">x8</text>
    <text x="235" y="78" font-size="12">간접 결과</text>
    <text x="355" y="58" font-family="monospace" font-weight="bold">x9 – x15</text>
    <text x="355" y="78" font-size="12">임시 (caller-saved)</text>
    <text x="500" y="58" font-family="monospace" font-weight="bold">x16, x17</text>
    <text x="500" y="78" font-size="12">IP0/IP1 (veneer)</text>
    <text x="650" y="58" font-family="monospace" font-weight="bold">x18</text>
    <text x="650" y="78" font-size="12">플랫폼 — Apple: 금지</text>
    <text x="125" y="120" font-family="monospace" font-weight="bold">x19 – x28</text>
    <text x="125" y="140" font-size="12">callee-saved (쓰면 복원)</text>
    <text x="305" y="120" font-family="monospace" font-weight="bold">x29 = FP</text>
    <text x="305" y="140" font-size="12">frame pointer</text>
    <text x="425" y="120" font-family="monospace" font-weight="bold">x30 = LR</text>
    <text x="425" y="140" font-size="12">복귀 주소</text>
    <text x="545" y="120" font-family="monospace" font-weight="bold">SP</text>
    <text x="545" y="140" font-size="12">16B 정렬</text>
    <text x="670" y="120" font-family="monospace" font-weight="bold">PC</text>
    <text x="670" y="140" font-size="12">직접 못 씀</text>
  </g>
  <text x="10" y="180" fill="currentColor" font-weight="bold">한 레지스터의 두 이름: x0 (64비트) 와 w0 (아래 32비트)</text>
  <g stroke="currentColor">
    <rect x="10" y="192" width="360" height="34" class="f-mut"/>
    <rect x="370" y="192" width="360" height="34" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="190" y="214">x0[63:32]</text>
    <text x="550" y="214">x0[31:0] = w0</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="252">· w0 에 쓰면 x0[63:32] 는 항상 0 이 된다 (Arm ARM: W 레지스터 쓰기는 상위 32비트를 0 으로).</text>
    <text x="10" y="272">· int 인자가 w0 로 들어올 때 x0[63:32] 는 unspecified → 읽으면 안 된다 (Part B).</text>
    <text x="10" y="292">· 레지스터 번호 31 은 명령에 따라 SP 또는 XZR(항상 0) — 그래서 str sp, [x7] 은 안 된다 (Part C).</text>
  </g>
</svg>
```

### Part A — 첫 세 함수

- `long add3(long a, long b, long c)` — 세 인자 합 (64비트 wrap).
- `long max_long(long a, long b)` — **부호 있는** 최댓값.
- `long sum_array(const long *p, size_t n)` — 배열 합. `n == 0` 이면 `p` 를 한 번도 읽지 않는다 (`NULL` 이어도 안전).

### Part B — `int` 인자와 윗 32비트

- `int max_int(int a, int b)` — `int` 는 `w0`/`w1` 로 들어온다.
- `long add_wide(long a, int b)` — `a + (long)b`. `b` 를 64비트로 **부호 확장** 해야 한다.
- 하네스는 `shared_hostile.S` 의 **적대적 호출자(hostile caller)** 로 `x0`/`x1` 의 윗 32비트에 `0xDEADBEEF` 같은 쓰레기를 넣어 부른다. ABI상 합법인 호출이다. 여기서 틀리면 asm 이 규약을 어긴 것이다.

### Part C — 패닉 핸들러식 레지스터 스냅숏

- `void regdump_capture(long a0, …, long a6, struct regdump *d)` — 8번째 인자 `d` (= `x7`) 가 가리키는 구조체에 `x0`–`x6`, `x29`(fp), `x30`(lr), `sp` 를 저장한다.
- 펌웨어의 panic/HardFault 핸들러가 맨 처음 하는 일이 이것이다: "레지스터를 메모리에 떠 놓고 나중에 읽는다." 테스트는 `fp` 가 호출자의 `__builtin_frame_address(0)` 과 같은지, `lr - 4` 의 명령이 **정말 `regdump_capture` 로 가는 `BL`** 인지를 디코드해서 확인한다. 덤프 분석 자체는 ARM-M14 에서 한다.

## 2. Problem (English)

> **Write your first AArch64 functions in a `.S` file and call them from C (macOS arm64).**
>
> Implement in assembly (leaf functions, no stack use, do not touch x18 or x19–x28):
>
> 1. `long add3(long a, long b, long c);` `long max_long(long a, long b);` (signed) `long sum_array(const long *p, size_t n);` — must not dereference `p` when `n == 0`.
> 2. `int max_int(int a, int b);` and `long add_wide(long a, int b);` (returns `a + (long)b`). Your code must be correct for **any** caller that conforms to AAPCS64. In particular, bits [63:32] of an `int` argument register are unspecified.
> 3. `void regdump_capture(long a0, long a1, long a2, long a3, long a4, long a5, long a6, struct regdump *d);` with `struct regdump { uint64_t x[7]; uint64_t fp, lr, sp; };` — store the incoming x0–x6, x29, x30 and sp.
>
> Examples: `add3(LONG_MAX, 1, 0) == LONG_MIN` (wraps, as the ISA does), `max_long(-1, 1) == 1`, `max_int` called with `x0 = 0xDEADBEEF_FFFFFFFB` (`a = -5`) and `x1 = 3` returns `3`.
>
> Follow-ups: list the differences between Apple's arm64 ABI and the generic AAPCS64. Why is x18 off-limits? What does `ret` actually do? Why can't you `str sp, [x7]`?

## 3. 시작 코드

파일 셋 + 공용 트램펄린 하나다. 빌드 규칙: `harness.c` + `<variant>.S` + `shared_hostile.S`.

<!-- code: harness.c -->

<!-- code: shared_hostile.S -->

<!-- code: starter.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c starter.S shared_hostile.S -o /tmp/ARM-B01_starter && /tmp/ARM-B01_starter
```

처음 실행 결과 (실제 출력, 앞뒤만):

```text
FAIL harness.c:77  add3(1, 2, 3) == 6
FAIL harness.c:78  add3(-5, 2, 1) == -2
…
PASS sum_array(NULL, 0) == 0
…
FAIL harness.c:142  d.sp != 0 && d.sp % 16 == 0 && d.sp <= local && local - d.sp < 4096
info: take_dump=0x102a214ec  lr=0 (+18446744069370407700)  fp=0  sp=0  &local=0x16d3de8d8
1/30 passed
```

- `.S` (대문자)는 C 전처리기를 먼저 거친다 → `#ifdef`·`#define` 을 쓸 수 있다. 주석은 `//` 나 `/* */` 만 쓴다. **`;` 주석은 쓰지 않는다**: Apple arm64 어셈블러에선 `;` 가 주석이지만 GNU as(ELF)에선 문장 구분자라서, 같은 파일이 Linux 에서 다르게 해석된다.
- `.globl _add3` 는 링커에 이름을 공개하는 지시어다. `.p2align 2` 는 4바이트 정렬이다 (A64 명령은 전부 4바이트). `_add3:` 는 라벨. C 의 `add3` 는 Mach-O 에서 `_add3` 가 된다.
- `cc` 의 `-O2` 는 `harness.c` 에만 영향이 있다. `.S` 는 내가 쓴 명령 그대로 조립된다.

## 4. 힌트

<details>
<summary>힌트 1 — add3 는 명령 두 개 + ret</summary>

`add Xd, Xn, Xm` 은 3-피연산자 형식이다. `a` 가 이미 `x0` 에 있고 반환도 `x0` 이니 `x0` 에 누적하면 된다. `add` 는 플래그를 안 바꾼다 (바꾸는 건 `adds`).

</details>

<details>
<summary>힌트 2 — max 는 "비교 후 선택"</summary>

`cmp x0, x1` 이 `x0 - x1` 로 NZCV 를 설정한다. `csel Xd, Xn, Xm, cond` 은 cond 가 참이면 Xn, 아니면 Xm 을 고른다. 부호 있는 `>` 는 `gt`, 부호 없는 `>` 는 `hi` 다. 조건 코드표는 ARM-B05.

</details>

<details>
<summary>힌트 3 — sum_array 루프 뼈대</summary>

```text
acc = 0
if n == 0 goto done          // cbz x1, 2f
1: x3 = *p; p += 8           // ldr x3, [x0], #8
   acc += x3
   n -= 1, 플래그 설정        // subs x1, x1, #1
   if n != 0 goto 1          // b.ne 1b
2: x0 = acc; ret
```

`1:` / `1b` 는 숫자 지역 라벨이다 (`b` = backward, `f` = forward).

</details>

<details>
<summary>힌트 4 — int 인자는 w 로만</summary>

`cmp w0, w1` 은 아래 32비트만 비교하므로 윗 비트의 쓰레기를 아예 보지 않는다. 결과를 `w0` 에 쓰면 윗 32비트는 0 이 된다. `add_wide` 는 `add x0, x0, w1, sxtw` 처럼 **확장 레지스터(extended register)** 형식을 쓰면 확장과 덧셈이 명령 하나로 끝난다.

</details>

<details>
<summary>힌트 5 — sp 는 str 의 데이터 레지스터가 될 수 없다</summary>

`str sp, [x7]` 은 조립 오류다. 레지스터 31번이 그 자리에선 `xzr` 이기 때문이다. 먼저 `mov x9, sp` 로 복사한다 (`mov` 의 이 형식은 실제로 `add x9, sp, #0` 이라서 31번 = SP 로 해석된다).

</details>

## 5. 풀이

### Part A — 인자가 어디 있는지만 알면 끝

C 가 `add3(1, 2, 3)` 을 부르면 호출자는 `x0=1, x1=2, x2=3` 을 채우고 `bl _add3` 를 실행한다. `bl` 은 다음 명령 주소를 `x30` 에 넣고 점프한다. asm 은 결과를 `x0` 에 두고 `ret` 한다. `ret` 은 `x30` 으로 점프한다.

| 단계 | 명령 | x0 | x1 | x2 | 비고 |
|---|---|---|---|---|---|
| 진입 | — | 1 | 2 | 3 | x30 = 호출자의 `bl` 다음 주소 |
| 1 | `add x0, x0, x1` | 3 | 2 | 3 | |
| 2 | `add x0, x0, x2` | 6 | 2 | 3 | |
| 3 | `ret` | 6 | | | PC ← x30 |

`add3(LONG_MAX, 1, 0)` 의 기대값 `LONG_MIN` 은 테스트에 **손으로** 적었다. C 로 `LONG_MAX + 1` 을 계산하면 부호 있는 오버플로라서 **UB(정의되지 않은 동작)** 이다. 컴파일러는 "그런 일은 없다" 고 가정해도 된다. 반면 ISA 의 `add` 는 그냥 2^64 모듈러 덧셈이다. **C 추상 기계(abstract machine)의 규칙과 ISA 의 정의된 동작은 다른 층**이다. 그래서 무작위 테스트의 기준 모델도 `uint64_t` 덧셈 (모듈러, UB 없음)으로 썼다.

`max_long` 의 `gt` vs `hi`: `max_long(-1, 1)` 에서 `-1` 은 비트로 `0xFFFF…FF` 다. 부호 없는 조건 `hi` 로 고르면 `-1` 이 "더 크다". 테스트 `max_long(-1, 1) == 1` 이 이 실수를 잡는다. 플래그 계산 자체는 ARCH-B02, 조건 코드 전체는 ARM-B05 에서 다룬다.

`sum_array({10, -3, 7}, 3)` 추적 (A = 배열 주소):

| 명령 | x0 (p) | x1 (n) | x2 (acc) | x3 | Z |
|---|---|---|---|---|---|
| `mov x2, #0` | A | 3 | 0 | — | — |
| `cbz x1, 2f` (안 뜀) | A | 3 | 0 | — | — |
| `ldr x3, [x0], #8` | A+8 | 3 | 0 | 10 | — |
| `add` / `subs` / `b.ne` 뜀 | A+8 | 2 | 10 | 10 | 0 |
| 2회차 | A+16 | 1 | 7 | -3 | 0 |
| 3회차, `b.ne` 안 뜀 | A+24 | 0 | 14 | 7 | 1 |
| `mov x0, x2` / `ret` | **14** | | | | |

`cbz` 를 루프 **앞**에 두는 이유: 빼면 `n == 0` 일 때 `ldr` 이 한 번 실행되고 `subs` 가 0 − 1 = `0xFFFF…FF` 를 만든다. 그러면 사실상 무한 루프로 메모리를 훑다가 크래시한다. 하네스의 `sum_array(NULL, 0) == 0` 이 이 경우를 지킨다.

실제로 조립된 바이트 (`objdump -d` 실출력, 일부):

```text
0000000000000000 <ltmp0>:
       0: 8b010000      add     x0, x0, x1
       4: 8b020000      add     x0, x0, x2
       8: d65f03c0      ret
000000000000000c <_max_long>:
       c: eb01001f      cmp     x0, x1
      10: 9a81c000      csel    x0, x0, x1, gt
0000000000000038 <_max_int>:
      38: 6b01001f      cmp     w0, w1
      3c: 1a81c000      csel    w0, w0, w1, gt
0000000000000044 <_add_wide>:
      44: 8b21c000      add     x0, x0, w1, sxtw
```

- `cmp x0, x1` = `0xeb01001f`, `cmp w0, w1` = `0x6b01001f`. 둘은 **비트 31(sf, sixty-four)** 하나만 다르다. `csel` 도 `0x9a…` vs `0x1a…` 로 같다. x/w 는 "같은 명령의 폭 비트" 다.
- `cmp` 는 별도 명령이 아니라 `subs xzr, x0, x1` 의 별칭이다. 마지막 5비트 `11111` = Rd 31 = xzr (결과 버림).
- `ret` = `0xd65f03c0`: Rn 필드(비트 9–5) = `11110` = 30. 기본 레지스터가 x30 인 `RET Xn` 이다.
- `ltmp0` 는 섹션 시작에 붙은 어셈블러 임시 라벨이다. `_add3` 와 주소가 같아서 objdump 가 그 이름을 골랐다. `nm` 은 `0000000000000000 T _add3` 로 정확히 보여 준다.

### Part B — "unspecified" 는 진짜로 쓰레기가 들어온다는 뜻

규약 원문부터 보자 (AAPCS64 2026Q3, Stage C): *"When an argument is assigned to a register any unused bits in the register have unspecified value."* `int` 는 4바이트라서 `x0[63:32]` 가 바로 이 "unused bits" 다. 요약 노트에는 *"named integral values must be narrowed by the callee rather than the caller"* 라고 적혀 있다. 즉 **피호출자가 필요한 폭만 보라**는 것이다. Apple 문서도 32비트 미만 인자의 확장만 호출자 책임으로 바꿨다. **32비트 `int` 의 윗 32비트에 대해서는 아무것도 보장하지 않는다** (아래 ABI 표 참고).

이론만이 아니다. `demo_intbug.c` 가 버그 판 asm (`cmp x0, x1` / `add x0, x0, x1`)을 **평범한 clang -O2 C 코드**에서 부른 결과다 (실제 출력):

```text
case 1  int 변수 -5, 3 을 그냥 넘김     : x0=0x00000000fffffffb x1=0x0000000000000003
case 2  (int)0x1FFFFFFFB, 3 을 넘김     : x0=0x00000001fffffffb x1=0x0000000000000003
max_int_bad(-5, 3)        = -5   (정답 3)
add_wide_bad(100, -1)     = 4294967395 (정답 99)
hostile: max_int_bad(x0=0xDEADBEEF_00000007, x1=3) = 3 (정답 7)
```

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">max_int(-5, 3): 호출자가 실제로 넘긴 x0 (case 1, 실측)</text>
  <g stroke="currentColor">
    <rect x="80" y="38" width="300" height="32" class="f-mut"/>
    <rect x="380" y="38" width="300" height="32" class="f-acc"/>
    <rect x="80" y="80" width="300" height="32" class="f-mut"/>
    <rect x="380" y="80" width="300" height="32" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="20" y="60">x0</text><text x="20" y="102">x1</text>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="230" y="60">0x00000000</text><text x="530" y="60">0xFFFFFFFB (w0 = -5)</text>
    <text x="230" y="102">0x00000000</text><text x="530" y="102">0x00000003 (w1 = 3)</text>
  </g>
  <rect x="10" y="128" width="350" height="72" rx="8" class="f-warn" stroke="currentColor"/>
  <rect x="380" y="128" width="350" height="72" rx="8" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="22" y="150" font-family="monospace">cmp x0, x1   (64비트, 버그)</text>
    <text x="22" y="170">4294967291 &gt; 3 → gt 참</text>
    <text x="22" y="190">→ -5 를 고름 ✗</text>
    <text x="392" y="150" font-family="monospace">cmp w0, w1   (32비트, 정답)</text>
    <text x="392" y="170">-5 &lt; 3 → gt 거짓</text>
    <text x="392" y="190">→ 3 을 고름 ✓</text>
  </g>
  <text x="10" y="226" fill="currentColor">호출자는 ldr w0 로 int 를 읽었으므로 x0[63:32] = 0 (w 쓰기 규칙). 음수 int 가 "부호 확장" 되어 있지 않다.</text>
  <text x="10" y="250" fill="currentColor">case 2 처럼 long → int 변환은 레지스터에선 아무 일도 안 해서, 옛 윗 비트(0x1)가 그대로 남는다.</text>
</svg>
```

- **case 1**: clang 은 `volatile int` 를 `ldr w0, [sp, #36]` 으로 읽는다. w 쓰기라서 x0[63:32] = 0 이다. 음수 int 가 64비트로는 큰 양수 `0x00000000FFFFFFFB` 가 된다. 버그 판은 아무 장난 없는 평범한 호출에서 이미 틀린다.
- **case 2**: `(int)big` 변환을 clang 은 명령 없이 처리한다. `-O2 -S` 출력에 `; kill: def $w0 killed $w0 killed $x0` 주석만 있고 `mov x2, x1` / `b _raw_regs` 로 넘어간다. 그래서 x0 윗 비트에 `0x1` 이 남는다.
- **hostile**: `shared_hostile.S` 가 64비트 전체를 마음대로 채운다. `0xDEADBEEF00000007` 은 64비트로 음수라서 버그 판이 3 을 고른다.

해결은 명령 선택 하나다:

| 함수 | 버그 | 정답 | 이유 |
|---|---|---|---|
| `max_int` | `cmp x0, x1` | `cmp w0, w1` | 32비트 비교는 윗 비트를 보지 않는다 |
| `add_wide` | `add x0, x0, x1` | `add x0, x0, w1, sxtw` | `w1` 을 비트 31 기준으로 부호 확장한 뒤 더한다 |
| `(long)int` 반환 | `ret` (x0 그대로) | `sxtw x0, w0` | 연습 5 |
| `(uint64_t)u32` 반환 | `ret` | `mov w0, w0` | w 쓰기 = 공짜 0 확장. 연습 5 |

하네스 Part B 는 무작위 1000쌍 × (윗 32비트 무작위) 로 두 함수를 검증한다. 기대값 `a > b ? a : b`, `w + b` 는 C 로 계산해도 UB 가 아니다 (`|w| < 2^61`). 넘침이 생기는 `add_wide(LONG_MIN, -1) == LONG_MAX` 만 손으로 적었다.

#### Apple arm64 ABI vs 표준 AAPCS64

출처: Apple 「Writing ARM64 code for Apple platforms」(이하 [Apple]), Arm 「AAPCS64」 2026Q3 (이하 [AAPCS64]). 인용은 문서 원문, "실측" 은 이 맥에서 `bash demo_abi_diff.sh` 로 얻은 결과다 (전체: `demo_abi_diff.txt`).

| 항목 | 표준 AAPCS64 | Apple arm64 | 확인 |
|---|---|---|---|
| x18 | "Platform Register, if needed; otherwise a Caller-saved register" [AAPCS64] | "The platforms reserve register x18. Don't use this register." [Apple] | **실측**: 레지스터 압박 함수에서 Linux 타깃은 x18 을 5줄 사용, Apple 은 0줄, Linux + `-ffixed-x18` 도 0줄 |
| 심볼 이름 | ELF: C 이름 그대로 (`add3`) | Mach-O: `_` 접두사 (`_add3`) — Mach-O/C 관례 (위 Apple 문서엔 언급 없음) | **실측**: Mac `nm` → `T _add3`, VM `nm` → `T add3` |
| frame pointer | 플랫폼이 수준을 정함 [AAPCS64 "The Frame Pointer"] | "x29 must always address a valid frame record" (leaf·꼬리 호출은 생략 가능) [Apple] | `regdump_capture` 가 leaf 라 프레임 없이 x29 = 호출자 frame record (Part C 실측) |
| red zone | 없음: "No thread is permitted to access … the inactive region" [AAPCS64] | sp 아래 128 B, "can potentially make use of the space" [Apple] | 문서만 확인. clang 이 실제로 쓰는지는 이 레슨에서 확인하지 않음 |
| 가변 인자 | 고정 인자처럼 x0–x7 / v0–v7, `va_list` = 구조체 | 가변 인자는 **전부 8B 스택 슬롯**, `va_list` = `char *` [Apple] | **실측**: 아래 printf 데모 + clang -S |
| 32비트 미만 인자 확장 | 피호출자가 확장 ("narrowed by the callee") | "The caller … is responsible for signing or zero-extending any argument with fewer than 32 bits" [Apple] | **실측**: `call_char` Apple `sxtb w0, w0` (호출자), Linux 없음 / `take_schar` Linux 만 `sxtb` (피호출자) |
| `char` 부호 | unsigned byte [AAPCS64 C 매핑] | "The char type is a signed type." [Apple] | **실측**: `take_char` Linux `and w0, w0, #0xff`, Apple `ret` |
| 스택 인자 배치 | 8 B 미만도 8 B 슬롯 (C.16) | 자연 크기·정렬로 채움 (char 두 개 → sp, sp+1) [Apple] | 문서만 확인. 8개 초과 인자는 ARM-M01 |
| 16 B 정렬 인자 | 짝수 x 레지스터부터 | 홀수 레지스터에서 시작 가능 [Apple] | 문서만 확인 (ARM-M01) |
| `long double` | IEEE quad (16 B) | `double` 과 동일 (8 B) [Apple] | **실측**: `ld_size` Apple `mov w0, #8`, Linux `mov w0, #16` |
| `wchar_t` | unsigned (word) | 32비트 signed [Apple] | 문서만 확인 |
| SP 정렬 | 16 B (public interface, SP 로 메모리 접근할 때) | 같음 [Apple] | Part C 테스트가 `sp % 16 == 0` 확인 |

`demo_abi_diff.txt` 에서 발췌 (실출력, 지시어·주석 줄 제거):

```text
================ arm64-apple-macos14 ================      ================ aarch64-linux-gnu ================
_call_printf:                                              call_printf:
	sub	sp, sp, #32                                         	mov	x2, x1
	stp	x29, x30, [sp, #16]                                 	mov	x1, x0
	add	x29, sp, #16                                        	adrp	x0, .L.str
	stp	x0, x1, [sp]          ← a, b 를 스택에                	add	x0, x0, :lo12:.L.str
	adrp	x0, l_.str@PAGE                                     	b	printf             ← 레지스터로, 꼬리 호출
	add	x0, x0, l_.str@PAGEOFF
	bl	_printf
	ldp	x29, x30, [sp, #16]
	add	sp, sp, #32
	ret
_take_char:  ret                                           take_char:  and w0, w0, #0xff ; ret
_take_schar: add w0, w0, #1 ; ret                          take_schar: sxtb w8, w0 ; add w0, w8, #1 ; ret
_call_char:  sxtb w0, w0 ; b _use_char                     call_char:  b use_char
_ld_size:    mov w0, #8 ; ret                              ld_size:    mov w0, #16 ; ret
pressure(): x18/w18 를 언급한 줄 수 = 0                     pressure(): x18/w18 를 언급한 줄 수 = 5
                                                           aarch64-linux-gnu -ffixed-x18: x18/w18 줄 수 = 0
```

(두 열 배치와 `←` 설명은 읽기 쉽게 내가 붙였다. 명령 자체는 원본 그대로다.)

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">printf(fmt, a, b) — 가변 인자가 가는 곳</text>
  <text x="20" y="50" fill="currentColor" font-weight="bold">Apple arm64</text>
  <text x="390" y="50" fill="currentColor" font-weight="bold">표준 AAPCS64 (Linux)</text>
  <g stroke="currentColor">
    <rect x="20" y="62" width="150" height="30" class="f-acc"/>
    <rect x="20" y="102" width="150" height="30" class="f-mut"/>
    <rect x="20" y="142" width="150" height="30" class="f-mut"/>
    <rect x="200" y="102" width="160" height="30" class="f-ok"/>
    <rect x="200" y="142" width="160" height="30" class="f-ok"/>
    <rect x="390" y="62" width="150" height="30" class="f-acc"/>
    <rect x="390" y="102" width="150" height="30" class="f-ok"/>
    <rect x="390" y="142" width="150" height="30" class="f-ok"/>
    <rect x="570" y="102" width="160" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="30" y="82">x0 = fmt</text>
    <text x="30" y="122">x1 = (안 씀)</text>
    <text x="30" y="162">x2 = (안 씀)</text>
    <text x="210" y="122">[sp]   = a</text>
    <text x="210" y="162">[sp+8] = b</text>
    <text x="400" y="82">x0 = fmt</text>
    <text x="400" y="122">x1 = a</text>
    <text x="400" y="162">x2 = b</text>
    <text x="580" y="122">스택: 안 씀</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="200">고정 인자(fmt)는 두 ABI 모두 레지스터. 차이는 "..." 뒤의 인자뿐.</text>
    <text x="10" y="222">Apple 의 va_list 는 char * 하나 → va_arg 는 포인터를 8씩 올리기만 하면 된다.</text>
    <text x="10" y="244">그래서 프로토타입 없이(또는 비가변 프로토타입으로 잘못) printf 를 부르는 asm/C 는 Apple 에서 깨진다.</text>
  </g>
</svg>
```

asm 에서 printf 를 직접 불러 확인했다 (`demo_printf.S`, 실제 출력). `print2_linux_style` 은 a, b 를 x1, x2 에 넣고 스택 슬롯에는 표지값 `0x5A5A` (= 23130)를 둔다:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_printf.c demo_printf.S -o /tmp/ARM-B01_printf && /tmp/ARM-B01_printf
```

```text
print2_apple(11, 22):
  a=11 b=22
print2_linux_style(11, 22):
  a=23130 b=23130
```

Apple 의 printf 는 x1/x2 를 아예 보지 않고 스택만 읽는다. printf 를 부르는 함수는 leaf 가 아니므로 `stp x29, x30` 프레임이 필요하다 (ARM-B08). 포맷 문자열 주소는 `adrp` + `@PAGEOFF` 로 읽는다 (ARM-A09).

#### 같은 asm 을 Linux(ELF) 에서

Mach-O 와 ELF 는 **심볼 이름과 지시어만** 다르다. 명령은 그대로다. `demo_portable.S` 는 `SYM()` 매크로와 `.macro FUNC/ENDF` 로 한 파일을 두 형식으로 조립한다:

```text
// Apple (solution.S)              // Linux ELF (demo_portable.S 가 펼친 모습)
    .globl  _add3                      .globl  add3
    .p2align 2                         .type   add3, %function
_add3:                                 .p2align 2
    add x0, x0, x1                 add3:
    ...                                add x0, x0, x1 ...
                                       .size   add3, . - add3
                                       .section .note.GNU-stack,"",%progbits
```

Lima VM(`elinux`, aarch64 Ubuntu)에서 gcc 15.2 와 clang 21.1 로 `harness.c demo_portable.S shared_hostile.S` 를 빌드·실행했다 (실제 출력):

```text
info: take_dump=0xb4ec7d621a00  lr=0xb4ec7d621a58 (+88)  fp=0xffffdb36c800  sp=0xffffdb36c7f0  &local=0xffffdb36c7f0
30/30 passed
gcc exit=0
info: take_dump=0xb36a28f41700  lr=0xb36a28f41744 (+68)  fp=0xfffff6737700  sp=0xfffff67376f0  &local=0xfffff67376f8
30/30 passed
clang exit=0
0000000000001a90 T add3
0000000000001b0c T call2_dirty_i
    92: 0000000000001a90    12 FUNC    GLOBAL DEFAULT   12 add3
   104: 0000000000001adc    48 FUNC    GLOBAL DEFAULT   12 regdump_capture
```

`.size` 덕분에 `readelf` 가 `add3` 의 크기를 12 B (명령 3개)로 안다. 디버거·프로파일러·`perf` 가 이 정보를 쓴다. `.note.GNU-stack` 이 없으면 GNU ld 가 "실행 가능한 스택" 경고를 낸다 (binutils 2.39+ 기준, 확인 권장).

### Part C — 레지스터를 메모리에 떠 놓기

`regdump_capture` 는 인자 8개를 받는다. 정수 인자 레지스터 x0–x7 이 꽉 찬다. 그래서 `d` 는 `x7` 에 있고, `str xN, [x7, #off]` 로 저장한다 (오프셋 = 필드 위치, `_Static_assert(offsetof…)` 로 C 쪽과 맞췄다).

| 저장 대상 | 왜 이 값인가 |
|---|---|
| x0–x6 | 호출 순간의 인자 그대로. 크래시 덤프에서 "이 함수가 어떤 인자로 불렸나" 를 보는 곳 |
| x29 | 우리는 프레임을 만들지 않았다 → x29 는 **호출자(take_dump)의** frame record 를 가리킨다 |
| x30 | `bl _regdump_capture` 가 넣은 복귀 주소 = take_dump 안의 `bl` 다음 명령 |
| sp | leaf 라 sp 를 안 움직였다 → 호출자의 sp 와 같다. `str sp, …` 는 조립 오류라서 `mov x9, sp` 를 거친다 |

`str sp, [x7]` 을 시도하면 (실제 조립 오류):

```text
bad.S:4:7: error: invalid operand for instruction
  str sp, [x7]
      ^
```

호출자 `take_dump` 의 실제 clang -O2 출력과 캡처 값 (실측, 세 번 실행해도 오프셋 동일. `...` 생략과 `←` 설명은 내가 붙였다):

```text
_take_dump:
	sub	sp, sp, #32
	stp	x29, x30, [sp, #16]
	add	x29, sp, #16
	...
	mov	w0, #10            ← long 인자 10 을 w0 로! (w 쓰기 = 윗 32비트 0, 그래서 64비트로도 10)
	mov	x1, #-20           ← 음수는 x 로 (윗 비트까지 1 이어야 하니까)
	...
	bl	_regdump_capture   ← take_dump+64
	ldr	x8, [sp, #8]       ← take_dump+68 = lr
info: take_dump=0x100d854ec  lr=0x100d85530 (+68)  fp=0x16f07a8e0  sp=0x16f07a8d0  &local=0x16f07a8d8
```

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">regdump_capture 진입 순간의 스택 (실측 sp = 0x16f07a8d0)</text>
  <g stroke="currentColor">
    <rect x="200" y="40" width="240" height="36" class="f-mut"/>
    <rect x="200" y="76" width="240" height="36" class="f-acc"/>
    <rect x="200" y="112" width="240" height="36" class="f-acc"/>
    <rect x="200" y="148" width="240" height="36" class="f-ok"/>
    <rect x="200" y="184" width="240" height="36" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="end">
    <text x="190" y="63">…8f0</text>
    <text x="190" y="99">sp+24 …8e8</text>
    <text x="190" y="135">sp+16 …8e0</text>
    <text x="190" y="171">sp+8  …8d8</text>
    <text x="190" y="207">sp+0  …8d0</text>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="320" y="63">main 의 프레임</text>
    <text x="320" y="99" font-family="monospace">saved lr (main 으로)</text>
    <text x="320" y="135" font-family="monospace">saved fp (main 의 x29)</text>
    <text x="320" y="171" font-family="monospace">local = 42</text>
    <text x="320" y="207">(빈 칸: 16B 정렬)</text>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="460" y="135">← x29 = d-&gt;fp</text>
    <text x="460" y="171">← &amp;local</text>
    <text x="460" y="207">← sp = d-&gt;sp</text>
  </g>
  <text x="10" y="248" fill="currentColor">x30 = d-&gt;lr = take_dump+68 → lr-4 의 워드를 디코드하면 BL(100101|imm26), 목적지 = _regdump_capture.</text>
  <text x="10" y="270" fill="currentColor">frame record = [fp] 이전 fp, [fp+8] 복귀 주소 — 이 사슬을 따라가는 것은 ARM-B08.</text>
</svg>
```

테스트 `bl_target(d.lr - 4) == regdump_capture` 는 코드도 메모리라는 사실을 쓴다. `__TEXT` 는 읽을 수 있으므로 복귀 주소 바로 앞 4바이트를 읽는다. 상위 6비트가 `100101` 이면 `BL` 이다. imm26 을 부호 확장해 ×4 하면 PC 상대 목적지가 나온다. 펌웨어에서 스택을 훑어 "이 값이 복귀 주소처럼 보이나?" 를 판단하는 휴리스틱(스택 스캐닝 언와인더)이 정확히 이 검사를 한다.

ASan 빌드에서는 take_dump 가 커져서 `lr` 이 `+288` 이 됐다 (실측). 그래서 범위 검사를 1024 B 로 넉넉히 두고, 진짜 검증은 BL 디코드에 맡겼다.

## 6. 정답 코드

<!-- code: solution.S diff=starter.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_hostile.S -o /tmp/ARM-B01_solution && /tmp/ARM-B01_solution
# → 30/30 passed, exit 0 (실행 0.01 s 미만)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   harness.c solution.S shared_hostile.S -o /tmp/ARM-B01_san && /tmp/ARM-B01_san
# → 30/30 passed, 경고 없음
```

관찰용 파일 (전부 이 디렉터리):

```bash
cc -std=c11 -O2 demo_intbug.c demo_intbug.S shared_hostile.S -o /tmp/ARM-B01_intbug && /tmp/ARM-B01_intbug   # int 를 x 로 읽는 버그
cc -std=c11 -O2 demo_printf.c demo_printf.S -o /tmp/ARM-B01_printf && /tmp/ARM-B01_printf                    # 가변 인자 위치
bash demo_abi_diff.sh                                                                                        # Apple vs AAPCS64 asm 비교
cc -std=c11 -O2 harness.c demo_portable.S shared_hostile.S -o /tmp/ARM-B01_port && /tmp/ARM-B01_port          # Mach-O/ELF 겸용 판
# Linux (Lima VM, 맥 홈은 읽기 전용이라 복사 후 빌드):
limactl shell elinux -- bash -lc 'mkdir -p /tmp/ARM-B01_work && cd /tmp/ARM-B01_work && cp -f /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ARM/ARM-B01/{harness.c,demo_portable.S,shared_hostile.S} . && gcc -std=c11 -Wall -Wextra -Werror -O2 harness.c demo_portable.S shared_hostile.S -o port && ./port | tail -1'
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `int` 인자를 `cmp x0, x1` 로 비교 | x0[63:32] 는 unspecified. 평범한 clang 호출에서도 음수 int 는 `0x00000000FFFFFFFB` 로 온다 | `cmp w0, w1` |
| `int` 를 `long` 에 그냥 `add x0, x0, x1` | 부호 확장이 안 됨 → `100 + (-1)` = 4294967395 (실측) | `add x0, x0, w1, sxtw` |
| `.globl add3` (밑줄 없음) on macOS | C 의 `add3` 는 Mach-O 에서 `_add3`. 링크 오류 `Undefined symbols: _add3` | `.globl _add3` / `_add3:` 또는 `SYM()` 매크로 |
| `;` 로 주석 | Apple 어셈블러에선 주석, GNU as(ELF)에선 문장 구분자 → 이식 시 엉뚱한 명령 | `//` 또는 `/* */` |
| `sum_array` 에서 `cbz` 생략 | n=0 이면 한 번 읽고 n 이 `0xFFFF…FF` 로 wrap → 메모리 훑다 크래시 | 루프 앞 `cbz x1, 2f` |
| 부호 있는 max 에 `hi` | `hi` 는 unsigned 조건 → `max(-1, 1) = -1` | `gt` (ARM-B05) |
| x18 을 임시로 사용 | Apple: 예약 레지스터. OS 가 보존을 약속하지 않음 | x9–x15 (또는 x16/x17) |
| `str sp, [x7]` | Rt=31 은 이 자리에서 xzr → 조립 오류 `invalid operand` | `mov x9, sp` / `str x9, [x7]` |
| 가변 인자 함수(printf)에 x1, x2 로 인자 | Apple 은 가변 인자를 스택에서 읽는다 → 실측 `a=23130 b=23130` | `stp x0, x1, [sp]` (스택 슬롯) |
| x19–x28 을 쓰고 복원 안 함 | callee-saved 계약 위반. 호출자의 변수가 조용히 바뀜 | 안 쓰는 게 최선. 쓰려면 저장/복원 (ARM-B08, ARM-M01) |

## 8. 면접 Q&A

<details>
<summary>Q1. AArch64 에서 함수 인자와 반환값은 어디에 있나요? 8개를 넘으면?</summary>

정수·포인터는 x0–x7, 부동소수점은 v0–v7 (ARM-B11)이다. 반환은 x0 (128비트면 x0:x1), 큰 구조체 반환은 호출자가 x8 에 주소를 준다. 9번째부터는 스택으로 간다. 표준은 8 B 슬롯, Apple 은 자연 크기로 채운다. 깊은 규칙은 ARM-M01. 꼬리질문 "`int` 가 w0 로 오면 x0 윗 32비트는?" → AAPCS64 는 unspecified, 피호출자가 필요한 폭만 써야 한다. Apple 은 32비트 미만 타입만 호출자가 32비트까지 확장한다.

</details>

<details>
<summary>Q2. 왜 Apple 은 x18 을 쓰지 말라고 하나요?</summary>

AAPCS64 가 x18 을 "플랫폼 레지스터" 로 남겨 두었고, 각 OS 가 용도를 정한다. Windows on ARM 은 x18 에 TEB(스레드 블록) 포인터를 두고, Android 는 ShadowCallStack 포인터로 쓴다. Apple 문서는 "reserve. Don't use" 라고만 한다. XNU 커널이 문맥 전환 때 x18 을 보존하지 않는다(0 으로 지운다)는 관찰이 널리 알려져 있지만 공식 설명은 아니다 (확인 권장). 실무 결론: 이식 가능한 asm 은 x18 을 절대 쓰지 않고, 컴파일러에는 `-ffixed-x18` 로 막는다. 실측으로 Apple 타깃은 레지스터 압박에서도 x18 을 0회 썼고, Linux 타깃은 5회 썼다.

</details>

<details>
<summary>Q3. caller-saved 와 callee-saved 는 누가 비용을 내나요?</summary>

caller-saved(x0–x17, 그리고 `bl` 이 덮어쓰는 x30)는 함수 호출 후 망가져 있을 수 있다. 호출 뒤에도 값이 필요하면 **호출자**가 저장한다. callee-saved(x19–x28, x29, sp, d8–d15 하위 64비트)는 **피호출자**가 쓸 때만 저장·복원한다. leaf 함수는 임시 레지스터만으로 끝내면 저장 비용이 0 이다. 이 레슨의 함수들이 그렇다. 반대로 호출을 가로질러 값을 많이 들고 있어야 하는 함수는 x19+ 를 쓰고 프롤로그에서 `stp` 한다. 레지스터 반반 분할은 "누가 저장하나" 를 상황에 따라 싸게 고르게 하려는 절충이다.

</details>

<details>
<summary>Q4. `ret` 은 정확히 무엇을 하나요? `br x30` 과 같나요?</summary>

동작은 같다: PC ← x30. 차이는 **힌트**다. `RET` 는 "함수 복귀" 라고 분기 예측기에 알려서 반환 주소 스택(return stack)에서 목적지를 예측하게 한다. `BR x30` 은 일반 간접 분기로 취급될 수 있다 (Arm ARM 의 RET 설명 기준, 마이크로아키텍처마다 다름, 확인 권장). 인코딩 `0xd65f03c0` 의 Rn 필드 = 30 이다. `ret x5` 처럼 다른 레지스터로도 쓸 수 있다. `bl` 은 반대로 x30 ← PC+4 를 하면서 return stack 에 push 하는 힌트를 준다. 그래서 non-leaf 함수가 x30 을 저장하지 않으면 내부 `bl` 이 x30 을 덮어 복귀할 곳을 잃는다 (ARM-B08 에서 실제 크래시를 본다).

</details>

<details>
<summary>Q5. 레지스터 번호 31 은 무엇인가요? `str sp, [x7]` 이 왜 안 되나요?</summary>

A64 인코딩에서 5비트 레지스터 필드 31 은 명령마다 SP 또는 XZR/WZR(읽으면 0, 쓰면 버림)로 해석된다. 로드/스토어의 데이터 레지스터(Rt)에서는 XZR 이다. 그래서 `str xzr, [x0]` (0 저장)은 되지만 `str sp, …` 는 인코딩이 없다 → 실측 `invalid operand for instruction`. 주소 베이스(Rn)에서는 SP 이므로 `str x0, [sp]` 는 된다. `mov x9, sp` 는 `add x9, sp, #0` 의 별칭이다 (ADD 즉시값 형식에선 Rn=31 이 SP). `cmp x0, x1` = `subs xzr, x0, x1` 처럼 결과를 버리는 명령도 같은 원리다.

</details>

<details>
<summary>Q6. Apple ARM64 ABI 가 표준 AAPCS64 와 다른 점을 말해 보세요.</summary>

x18 예약, x29 항상 유효한 frame record (leaf 생략 가능), sp 아래 128 B red zone 허용 (표준은 금지), 가변 인자 전부 스택 + `va_list = char*`, 32비트 미만 인자는 호출자가 32비트로 확장 (표준은 피호출자), 스택 인자를 자연 크기로 촘촘히, 16 B 정렬 인자가 홀수 레지스터에서 시작 가능, `char` signed, `wchar_t` signed 32, `long double` = `double`. 심볼 `_` 접두사는 ABI 문서가 아니라 Mach-O/C 이름 관례다. 이 레슨에서 실측한 것: 가변 인자, 작은 정수 확장, `char` 부호, `long double`, x18, `_` 접두사.

</details>

<details>
<summary>Q7. 64비트 상수나 전역 변수 주소는 asm 에서 어떻게 x0 에 넣나요?</summary>

상수: `mov` 한 번(16비트 조각 하나, 또는 논리 즉시값으로 표현 가능할 때). 아니면 MOVZ + MOVK 최대 4개. 자세한 계획 세우기는 ARM-B03 이다. 주소: PC 상대 `adrp x0, sym@PAGE` + `add x0, x0, sym@PAGEOFF` (Apple). ELF 는 `adrp x0, sym` + `add x0, x0, :lo12:sym` 이다. ±4 GB 범위이고 위치 독립적이다 (ARM-A09). 그 밖에 리터럴 풀 `ldr x0, =value` 도 있다. 펌웨어 스타트업 코드는 MMU 꺼진 상태에서 주소를 다뤄야 해서 이 차이를 자주 묻는다.

</details>

<details>
<summary>Q8. 크래시 덤프에서 x0–x7, x29, x30 을 보면 무엇을 알 수 있나요?</summary>

크래시 지점이 함수 진입 직후라면 x0–x7 은 인자다. 함수 중간이면 이미 임시로 덮였을 수 있으므로 디스어셈블리로 확인해야 한다. x30 은 마지막 `bl` 의 복귀 주소다. leaf 에서 죽었으면 바로 호출자다. x29 는 frame record 사슬의 시작이다: `[fp]` = 이전 fp, `[fp+8]` = 그 프레임의 복귀 주소. 이것을 따라가면 백트레이스가 된다. Apple 이 frame pointer 를 강제하는 이유가 "디버그 정보 없이도 스택 트레이스가 의미 있게" 이다. 의심스러운 복귀 주소는 Part C 처럼 "`lr - 4` 가 BL 인가" 로 검증한다. 분석 연습은 ARM-M14, Cortex-M 버전은 ARM-M12.

</details>

## 9. 추가 연습

### 연습 1 — 인자 8개 다 쓰기: `add8`

`long add8(long a0, …, long a7)`. x0–x7 이 모두 인자다.

<details>
<summary>답 보기</summary>

`add` 7번이면 된다. 테스트는 `1, 10, 100, …, 10000000` 처럼 자리마다 다른 값을 넣어 레지스터 하나를 빠뜨리면 바로 보이게 했다. 답은 `(x0+x1)+(x2+x3)` 처럼 두 줄기로 더한다. 덧셈 사슬의 의존 길이가 7 → 3 단계로 줄어서 out-of-order 코어에서 유리하다 (이 크기에선 측정 불가 수준, 개념만).

</details>

### 연습 2 — 부호 없는 최솟값: `min_ulong`

`unsigned long min_ulong(unsigned long a, unsigned long b)`. `min_ulong(1, ULONG_MAX) == 1`.

<details>
<summary>답 보기</summary>

`cmp x0, x1` 후 `csel x0, x0, x1, lo`. `lo` (= `cc`, C==0) 는 부호 없는 `<` 다. `lt` 를 쓰면 `ULONG_MAX` 를 −1 로 보고 그쪽을 고른다. 같은 비트, 다른 조건 → ARM-B05.

</details>

### 연습 3 — 두 ABI 에서 다 맞는 `add_u8`

`uint32_t add_u8(uint8_t a, uint8_t b)`. Apple 만 생각하면 `add w0, w0, w1` 로 충분하다 (호출자가 32비트로 확장해 줌). 표준 AAPCS64 에서도 맞게 써라. 테스트는 비트 8–63 에 쓰레기를 넣어 부른다.

<details>
<summary>답 보기</summary>

`and w0, w0, #0xff` 로 a 를 0 확장하고, `add w0, w0, w1, uxtb` 로 b 를 확장하면서 더한다. Apple 기준으로 보면 이 테스트는 "계약 밖" 호출이다 (Apple 호출자는 확장해 준다). 하지만 같은 `.S` 를 Linux 에서도 쓰려면 이렇게 써야 한다. clang 도 같은 판단을 한다: Linux 타깃의 `take_schar` 는 `sxtb` 를 넣고 Apple 타깃은 안 넣었다 (실측).

</details>

### 연습 4 — 값 세기: `count_eq`

`size_t count_eq(const long *p, size_t n, long key)`.

<details>
<summary>답 보기</summary>

`sum_array` 뼈대에서 `add` 대신 `cmp x4, x2` + `cinc x3, x3, eq` 를 쓴다. `cinc` 는 조건이 참이면 +1 하는 명령으로, 분기가 없다 (조건 명령은 ARM-B05). 무작위 500회 × 길이 0–100, 값 0–3 으로 C 기준 모델과 비교한다.

</details>

### 연습 5 — 공짜 0 확장과 부호 확장

`long sext_s32(int v)` = `(long)v`, `uint64_t zext_u32(uint32_t v)` = `(uint64_t)v`. 둘 다 윗 32비트 쓰레기와 무관해야 한다.

<details>
<summary>답 보기</summary>

`sxtw x0, w0` (= `sbfm x0, x0, #0, #31`)가 부호 확장이다. 0 확장은 `mov w0, w0` 하나다. w 레지스터 쓰기가 [63:32] 를 0 으로 만든다. 그래서 컴파일러는 `uint32_t → uint64_t` 변환에 별도 명령이 필요 없는 경우가 많다 (이미 w 로 계산된 값이면 공짜). Part C 의 take_dump 가 long 인자 10 을 `mov w0, #10` 으로 넣은 것도 같은 원리다.

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_hostile.S -o /tmp/ARM-B01_practice && /tmp/ARM-B01_practice
# → 15/15 passed (ASan/UBSan 빌드도 15/15)
```

## 10. 핵심 정리

- 인자 x0–x7 (int 는 w0–w7), 반환 x0, `bl` 이 x30 에 복귀 주소, `ret` = x30 으로 점프 (+ return 예측 힌트).
- **w 에 쓰면 윗 32비트 = 0.** int 인자의 x[63:32] 는 unspecified → `cmp w`, `add …, w1, sxtw`, `sxtw`, `mov w0, w0`.
- leaf 함수는 프레임 없이 x0–x17 (x18 제외)만 쓰면 저장할 것이 없다. x19–x28 은 쓰면 복원 의무.
- Apple: x18 금지, `_` 접두사, x29 항상 유효, red zone 128 B, 가변 인자는 스택, 32비트 미만 인자는 호출자가 확장, `char` signed, `long double` = `double`.
- C 의 UB(부호 있는 오버플로)와 ISA 의 정의된 동작(wrap)은 다른 층이다 → 테스트 기대값은 손으로 적거나 unsigned 로 계산한다.
- 레지스터 31 = SP 또는 XZR (명령마다). `str sp` 는 없다.
- 덤프 읽기: x0–x7 인자(진입 직후만 신뢰), x30 복귀 주소(`lr-4` 가 BL 인지 확인), x29 frame record 사슬.

## 참고자료

- [Apple — Writing ARM64 code for Apple platforms](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms) — x18, x29, red zone, 가변 인자, 작은 정수 확장, char/long double (이 레슨의 인용 출처)
- [Arm — Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64), 2026Q3](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst) — 레지스터 역할표, Stage C "unused bits … unspecified", "narrowed by the callee", 스택 제약
- [Arm Architecture Reference Manual for A-profile (DDI 0487)](https://developer.arm.com/documentation/ddi0487/latest) — W 레지스터 쓰기의 상위 비트 0, ADD (extended register), RET, BL 인코딩
- [Arm A64 Instruction Set — ADD (extended register)](https://developer.arm.com/documentation/ddi0602/latest/Base-Instructions/ADD--extended-register---Add--extended-register--) · [RET](https://developer.arm.com/documentation/ddi0602/latest/Base-Instructions/RET--Return-from-subroutine-)
- [Microsoft — ARM64 ABI conventions (x18 = TEB)](https://learn.microsoft.com/en-us/cpp/build/arm64-windows-abi-conventions) · [Clang ShadowCallStack (x18 on AArch64)](https://clang.llvm.org/docs/ShadowCallStack.html)
- 관련 레슨: ARCH-B02 (NZCV), ARM-B03 (상수 로드), ARM-B04 (주소 지정), ARM-B05 (조건·CSEL), ARM-B08 (프레임), ARM-M01 (AAPCS64 심화), ARM-A09 (ADRP/GOT), ARM-M14 (덤프 역추적)
