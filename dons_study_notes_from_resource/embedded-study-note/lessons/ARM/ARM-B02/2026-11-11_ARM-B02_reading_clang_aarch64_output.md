---
id: ARM-B02
time: 60분
updated: 2026-11-11
---
# ARM-B02 clang -S로 C→AArch64 어셈블리 읽기

## 학습 목표

- `clang -S` 출력에서 **잡음(지시어·주석·CFI)** 을 걸러내고, 함수 하나를 "인자 레지스터 → 계산 → `x0`/`w0` 반환" 흐름으로 읽는다.
- 같은 C 함수의 `-O0` 과 `-O2` 출력을 비교해서 **스택 spill, 중복 로드, 프레임, 꼬리 호출(tail call), 점프 테이블, 상수 나눗셈 magic number** 가 어떻게 바뀌는지 표로 말한다.
- 컴파일러가 만든 **실제 -O2 asm 6개를 C로 역해석** 해서, 경계 입력(INT32_MIN, 음수 나눗셈, 0x80 이상 바이트, 링 버퍼 wrap)까지 똑같이 동작하게 만든다.
- asm 이 "정의한" 동작(32비트 wrap)을 C 로 옮길 때 **UB 를 피하는 법**(unsigned 로 계산)과, Apple arm64 vs AArch64 Linux 의 **`char` 부호·인자 확장 차이**를 asm 에서 알아본다.

## 1. 문제

"이 asm 뭐 하는 코드야?" — 크래시 덤프의 PC 주변 디스어셈블리, 소스 없는 벤더 라이브러리, "컴파일러가 내 코드를 어떻게 바꿨나" 를 읽는 능력은 펌웨어 면접(Apple·NVIDIA·Rivian 의 silicon/firmware 라운드)에서 화이트보드에 asm 몇 줄을 주고 바로 묻는 단골이다. SSD 컨트롤러 펌웨어(Cortex-R/A 코어)에서 HardFault/abort 의 PC 를 objdump 로 찾아가 본 적이 있다면, 그때 본 것이 바로 이 레슨의 `-O2` 출력이다 — `-O0` 처럼 생기지 않았다.

이 레슨은 B01(첫 `.S` 함수, x0–x7 인자, w/x 레지스터)을 안다고 가정하고, **쓰기가 아니라 읽기** 에 집중한다. 플래그와 조건 코드의 산술은 ARCH-B02, 64비트 상수 만들기는 ARM-B03, 주소 지정 모드는 ARM-B04, 스택 프레임 상세는 ARM-B08, 벡터화된 출력은 ARM-A11 이 맡는다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">struct pkt 레이아웃과 pkt_score 의 로드 — 오프셋이 asm 의 #즉시값으로 보인다</text>
<g stroke="currentColor"><rect x="20" y="40" width="20" height="30" class="f-mut"/><rect x="40" y="40" width="20" height="30" fill="none" stroke-dasharray="3 2"/><rect x="60" y="40" width="40" height="30" class="f-acc"/><rect x="100" y="40" width="80" height="30" class="f-acc"/><rect x="180" y="40" width="160" height="30" class="f-mut"/><rect x="340" y="40" width="320" height="30" class="f-mut"/><rect x="400" y="40" width="20" height="30" class="f-acc"/></g>
<g fill="currentColor" font-size="12"><text x="24" y="60">ty</text><text x="66" y="60">len</text><text x="125" y="60">seq</text><text x="250" y="60">ts</text><text x="348" y="60">[0..2]</text><text x="402" y="60">[3]</text><text x="470" y="60">payload[4..15]</text>
<text x="20" y="86">0</text><text x="60" y="86">2</text><text x="100" y="86">4</text><text x="180" y="86">8</text><text x="340" y="86">16</text><text x="400" y="86">19</text><text x="645" y="86">32</text></g>
<text x="20" y="112" fill="currentColor" font-weight="bold">-O0 (12 명령): p 를 스택에 두고 매번 다시 읽는다</text>
<text x="400" y="112" fill="currentColor" font-weight="bold">-O2 (6 명령): p 는 x0 에 그대로</text>
<g fill="currentColor" font-family="monospace" font-size="12">
<text x="20" y="132">sub  sp, sp, #16</text><text x="20" y="146">str  x0, [sp, #8]   // p → 스택</text><text x="20" y="160">ldr  x8, [sp, #8]   // p 읽기</text><text x="20" y="174">ldr  w8, [x8, #4]   // seq</text>
<text x="20" y="188">ldr  x9, [sp, #8]   // p 다시</text><text x="20" y="202">ldrh w9, [x9, #2]   // len</text><text x="20" y="216">add  w8, w8, w9</text><text x="20" y="230">ldr  x9, [sp, #8]   // p 또 다시</text>
<text x="20" y="244">ldrb w9, [x9, #19]  // payload[3]</text><text x="20" y="258">add  w0, w8, w9</text><text x="20" y="272">add  sp, sp, #16</text><text x="20" y="286">ret</text>
<text x="400" y="132">ldr  w8, [x0, #4]    // seq</text><text x="400" y="146">ldrh w9, [x0, #2]    // len</text><text x="400" y="160">ldrb w10, [x0, #19]  // payload[3]</text>
<text x="400" y="174">add  w8, w8, w9</text><text x="400" y="188">add  w0, w8, w10</text><text x="400" y="202">ret</text></g>
<text x="400" y="236" fill="currentColor">사라진 것: 스택 프레임(sub/add sp),</text><text x="400" y="254" fill="currentColor">p 의 spill 1번 + reload 3번.</text>
<text x="400" y="276" fill="currentColor">ldrh/ldrb = 0 확장 로드 (uint16/uint8).</text>
</svg>
```

(주석은 `//` 로 단다. Apple 어셈블러에서 `;` 는 주석이지만 GNU as(AArch64)에서는 문장 구분자라, `.S` 파일에는 `//` 만 쓴다 — clang 출력의 `;` 주석을 지운 이유.)

### Part A — 6개 함수의 -O0 vs -O2 읽기 + csel 두 개

`demo_six.c` 의 6개 함수(루프 합, switch, 구조체 필드, 다른 함수 호출, 범위 검사 인덱스, 상수 곱셈/나눗셈)를 `-O0`/`-O2` 로 컴파일해서 줄마다 주석을 달고, `-O2` 에서 **무엇이 사라졌거나 바뀌었는지** 표로 정리한다(5. 풀이에 정답 주석). 그 다음 코딩: `shared_mystery.S` 의 `mystery1`(sub/subs/csel), `mystery2`(cmp/csel 두 번)를 C 로 다시 쓴다.

### Part B — 루프와 부호 함정

`mystery3` 은 회전된(rotated) 루프 — 비교가 **시프트 전의 옛 값** 에 대해 일어난다. `mystery4` 는 `ldrsb` + `b.gt` — 바이트를 **부호 있게** 비교한다. 둘 다 "대충 읽으면" 경계에서 틀린다. 그리고 `mystery1(INT32_MIN, INT32_MAX)` 처럼 C 로 그대로 쓰면 **UB** 인 입력도 asm 과 같은 값을 내야 한다.

### Part C — 펌웨어 코드 역해석

`mystery5` 는 SoA(struct-of-arrays) 센서 테이블 검색 + 스케일링(`*5/2`, 부호 보정), `mystery6` 은 24칸 링 버퍼의 채워진 개수(`umull` magic number). 마지막으로 링크 전 `.o` 의 `dispatch` 점프 테이블 **바이트** 를 objdump 로 읽어 opcode → 핸들러를 손으로 복원한다(크래시 덤프 읽기 연습).

## 2. Problem (English)

> **"What does this assembly do?"**
>
> You are given `shared_mystery.S`: Apple clang 21 `-O2` output (arm64, AAPCS64 with Apple's variations) of six C functions whose sources are hidden. For each, write a C11 function with the same prototype that returns **exactly** the same value for **every** input:
>
> ```c
> uint32_t mystery1(int32_t a, int32_t b);
> long     mystery2(long x, long lo, long hi);
> int      mystery3(uint32_t x);
> size_t   mystery4(const char *s);                 /* s is NUL-terminated */
> struct sensor_tab { uint32_t n; uint16_t id[16]; int16_t temp[16]; };   /* n <= 16 */
> int32_t  mystery5(const struct sensor_tab *t, uint16_t id);
> uint32_t mystery6(uint32_t head, uint32_t tail);
> ```
>
> Constraints: no inline asm, no compiler builtins, and **no undefined behaviour** in your C (UBSan-clean) even where the assembly relies on two's-complement wrap-around. Your functions are checked against the assembly on exhaustive small ranges, named edge cases (`INT32_MIN`, `INT64_MAX`, bytes `>= 0x80`, `lo > hi`) and 10^5–10^6 random inputs.
>
> Examples: `mystery1(3, 7) == 4`, `mystery1(INT32_MIN, INT32_MAX) == 0xFFFFFFFF`, `mystery2(5, 10, 0) == 10`, `mystery3(0xFFFFFFFF) == 16`, `mystery4("caf\xC3\xA9 au lait") == 3`, `mystery6(2, 5) == 21`.
>
> Follow-up: compile the provided six functions at `-O0` and `-O2` and explain every difference (stack spills, frame record, callee-saved registers, tail calls, jump table encoding, division by constants).

## 3. 시작 코드

<!-- code: starter.c -->

<!-- code: shared_mystery.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있음
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c shared_mystery.S -o /tmp/ARM-B02_v && /tmp/ARM-B02_v
# 처음: FAIL 줄들 + "6/35 passed" (asm 자체 확인 1개 + 우연히 맞는 0 값들), exit 1
```

Part A 읽기 과제용 파일: `demo_six.c`(6개 함수), `gen_listings.sh`(→ `demo_six_O0.s`, `demo_six_O2.s` 재생성 + 함수별 명령 수). **`demo_mystery_src.c` 는 정답(원본 C)이니 풀기 전에 열지 말 것.**

## 4. 힌트

<details>
<summary>힌트 1 — 잡음 걸러내기</summary>

`.globl` `.p2align` `.cfi_*` `.build_version` `.loh` `.subsections_via_symbols` 는 지시어(directive) — 실행되지 않는다. `LBB3_1:` 같은 `L` 접두 라벨은 기본 블록(basic block), `; %bb.2:` 는 라벨이 필요 없는 블록 표시 주석이다. 남는 줄(탭 + 소문자 니모닉)만 명령이다. 첫 줄에서 **어떤 인자 레지스터를 읽는지**(w0? x1?), 마지막 `ret` 직전에 **누가 w0/x0 에 쓰는지** 부터 본다.

</details>

<details>
<summary>힌트 2 — csel 은 "cond ? Rn : Rm"</summary>

`csel Rd, Rn, Rm, cond` = `Rd = cond ? Rn : Rm`. cond 는 **바로 앞의 플래그 설정 명령**(cmp/subs) 기준이다. `cmp x0, x2` + `lt` 는 "x0 < x2 (signed)". `gt/lt/ge/le` 는 signed, `hi/lo/hs/ls` 는 unsigned (ARCH-B02 참고).

</details>

<details>
<summary>힌트 3 — 회전된 루프: 비교가 어느 값에 대한 것인가</summary>

`mystery3` 에서 `cmp w8, #3` 은 `lsr` **앞** 에 있다. `lsr` 은 플래그를 안 바꾼다. 그러니 `b.hi` 는 "옛 w8 > 3" 이고, 그건 "새 w8 = 옛 w8 >> 2 가 0 이 아니다" 와 같다. `mystery4` 도 `add x8, x8, #1` 이 비교 앞이라 끝에서 `sub x0, x8, #1` 로 하나를 되돌린다.

</details>

<details>
<summary>힌트 4 — 큰 홀수 상수 + umull + lsr = 나눗셈</summary>

`0xaaaaaaab` ≈ 2^36 / 24, `0xcccccccd` ≈ 2^35 / 10. `umull` 후 `lsr #k` 는 `x * m / 2^k` = `x / d`. 그 뒤 `msub w0, q, d, x` 는 `x - q*d` = 나머지. 부호 있는 나눗셈은 음수일 때만 `2^k - 1` 을 더하는 보정(`add …, lsr #31` 또는 `csel … lt`) + `asr` 이다.

</details>

<details>
<summary>힌트 5 — UB 없이 wrap 을 쓰는 법</summary>

`a - b` (int32_t) 는 `INT32_MIN - 1` 에서 UB 다. asm 의 `sub w8, w1, w0` 는 그냥 mod 2^32 다. C 에서는 `(uint32_t)a - (uint32_t)b` 처럼 **unsigned 로 계산** 하면 정의된 wrap 이 된다.

</details>

## 5. 풀이

### 0. clang 출력 읽는 법 (Apple arm64)

| 줄 | 뜻 | 실행되나 |
|---|---|---|
| `.globl _f` / `.p2align 2` / `_f:` | 심볼 공개, 4바이트 정렬, 함수 시작 (C 이름 `f` → Mach-O 심볼 `_f`) | ✗ |
| `; %bb.1:` / `LBB0_2:` | 기본 블록. `L` 로 시작하는 라벨은 어셈블러 지역 심볼(Mach-O) | ✗ |
| `.cfi_startproc`, `.cfi_offset w30, -8` | 언와인드 정보(DWARF CFI). **Apple 에서는 `-fno-asynchronous-unwind-tables` 를 줘도 남는다** (실측: -O0 24줄, -O2 17줄) → `grep -v '\.cfi_'` | ✗ |
| `Lloh0:` + `.loh AdrpAdd Lloh0, Lloh1` | Mach-O 링커 최적화 힌트 (링커가 adrp+add 를 더 짧게 바꿀 수 있다는 표시, 확인 권장) | ✗ |
| `mov x8, #0 ; =0x0` | 명령 + clang 이 붙인 값 주석 | ✓ |

인자는 x0–x7(32비트 타입이면 w0–w7), 반환은 x0/w0, x0–x18 은 호출하면 깨질 수 있는(caller-saved) 레지스터, x19–x28 은 callee 가 보존 — 여기까지는 B01 의 내용이고 깊은 규칙은 ARM-M01.

### Part A — 6개 함수, -O0 vs -O2 (`bash gen_listings.sh`)

<!-- code: demo_six.c -->

```text
$ bash gen_listings.sh
Apple clang version 21.0.0 (clang-2100.0.123.102)
-O0: 24 .cfi lines even with -fno-asynchronous-unwind-tables (Apple keeps unwind info)
-O2: 17 .cfi lines even with -fno-asynchronous-unwind-tables (Apple keeps unwind info)
insns -O0: _sum_loop=25 _dispatch=65 _pkt_score=12 _call_twice=19 _get_checked=24 _const_math=16
insns -O2: _sum_loop=40 _dispatch=31 _pkt_score=6 _call_twice=12 _get_checked=6 _const_math=11
insns -O2 -fno-vectorize: _sum_loop=12 _dispatch=31 _pkt_score=6 _call_twice=12 _get_checked=6 _const_math=11
```

**① sum_loop — -O0: 모든 변수가 스택에 산다** (`demo_six_O0.s` 에서 발췌, 주석은 내가 단 것)

```text
_sum_loop:
	sub	sp, sp, #32          // 지역 슬롯 32B: [sp+24]=a [sp+16]=n [sp+8]=s [sp]=i
	str	x0, [sp, #24]        // a spill
	str	x1, [sp, #16]        // n spill
	str	xzr, [sp, #8]        // s = 0   (xzr = 0 레지스터)
	str	xzr, [sp]            // i = 0
	b	LBB0_1               // -O0 은 "바로 다음 블록으로 가는 b" 도 남긴다
LBB0_1:                              // 조건 검사
	ldr	x8, [sp]             // i
	ldr	x9, [sp, #16]        // n
	subs	x8, x8, x9           // i - n, 플래그
	b.ge	LBB0_4               // i >= n (signed, long) → 끝
	b	LBB0_2
LBB0_2:                              // 본문
	ldr	x8, [sp, #24]        // a
	ldr	x9, [sp]             // i
	ldr	x9, [x8, x9, lsl #3] // a[i]  (i*8)
	ldr	x8, [sp, #8]         // s
	add	x8, x8, x9
	str	x8, [sp, #8]         // s 저장
	b	LBB0_3
LBB0_3:                              // i++ : ldr x8,[sp] / add x8,x8,#1 / str x8,[sp] / b LBB0_1
LBB0_4:
	ldr	x0, [sp, #8]         // return s
	add	sp, sp, #32
	ret
```

-O0 은 **문장 하나마다 메모리에서 읽고 메모리에 쓴다** — 디버거가 아무 줄에서나 변수를 읽고 바꿀 수 있게 하려는 것. 리프(leaf) 함수라 `stp x29, x30` 프레임 레코드는 없다(x29 는 호출자의 레코드를 계속 가리킨다).

**-O2 (`-fno-vectorize`): 4명령 루프**

```text
_sum_loop:
	cmp	x1, #1
	b.lt	LBB0_4               // n < 1 → 0 반환
	mov	x8, #0               // s = 0 (레지스터)
LBB0_2:
	ldr	x9, [x0], #8         // x9 = *a; a += 8  (post-index, B04)
	add	x8, x9, x8           // s += x9
	subs	x1, x1, #1           // n--  → i 라는 변수 자체가 사라졌다
	b.ne	LBB0_2
	mov	x0, x8
	ret                          // (LBB0_4: mov x8,#0 / mov x0,x8 / ret — n<1 경로)
```

**기본 -O2** 는 이 루프를 **벡터화** 한다(40명령): `n >= 8` 이면 `ldp q4, q5, [x8, #-32]` / `ldp q6, q7, [x8], #64` 로 한 번에 long 8개를 읽고 `add.2d` 4개로 누산기 4개(v0–v3)에 더한 뒤, `addp.2d d0, v0` + `fmov x8, d0` 로 합치고 나머지는 위 스칼라 루프로 처리한다. `add.2d` 는 Apple 문법 표기(표준 표기 `add v0.2d, v4.2d, v0.2d`). 이 부분 읽기는 ARM-A11 몫이다.

**② dispatch — switch 10개 케이스: 둘 다 점프 테이블, 모양이 다르다**

```text
// -O0 (발췌)
	add	w8, w8, #0           // opc - 최소 case(0)  ← -O0 의 쓸데없는 명령
	mov	w8, w8               // 0 확장 (w 쓰기 → x 상위 32비트 0)
	str	x8, [sp, #8]         // 인덱스까지 spill
	subs	x8, x8, #12
	b.hi	LBB1_12              // unsigned > 12 → default (음수 opc 도 여기로)
	ldr	x11, [sp, #8]
	adrp	x10, lJTI1_0@PAGE    // 테이블 주소 = 4KB 페이지 + 하위 12비트
	add	x10, x10, lJTI1_0@PAGEOFF
Ltmp1:	adr	x8, Ltmp1            // 기준 주소
	ldrsw	x9, [x10, x11, lsl #2]  // 4바이트 signed 엔트리
	add	x8, x8, x9
	br	x8
LBB1_2:	ldur	w0, [x29, #-12]      // arg 를 스택에서 → bl _op_delete_sq → stur w0 → b LBB1_13 (공통 에필로그)
lJTI1_0:                             // 테이블이 __text 안, 함수 바로 뒤 (code 안의 data)
	.long	LBB1_2-Ltmp1         // ... opc 3 자리는 .long LBB1_12-Ltmp1 (구멍 → default)
// -O2
	cmp	w0, #12
	b.hi	LBB1_8               // default: mov w0, #-22 / ret
	mov	w8, w0
	adrp	x9, lJTI1_0@PAGE
	add	x9, x9, lJTI1_0@PAGEOFF
	adr	x10, LBB1_2          // 기준 = 첫 case 블록
	ldrb	w11, [x9, x8]        // 1바이트 엔트리!
	add	x10, x10, x11, lsl #2   // 엔트리 = (목표 - 기준) / 4  (명령 단위)
	br	x10
LBB1_2:	mov	x0, x1               // arg 를 x0 으로 옮기고
	b	_op_delete_sq        // bl 이 아니라 b → 꼬리 호출, 반환은 op_delete_sq 의 ret 이 직접
	.section	__TEXT,__const     // 테이블은 읽기 전용 상수 섹션으로
lJTI1_0:
	.byte	(LBB1_2-LBB1_2)>>2   // ... 13개
```

`demo_jt_threshold.sh` 로 케이스 수를 바꿔 보면 이 clang 은 **9개 이하에서는 비교 트리(cmp/b.le/b.eq…), 10개부터 점프 테이블** 을 냈다 (-O0, -O2 동일; 휴리스틱이라 버전·타깃마다 다를 수 있다):

```text
$ bash demo_jt_threshold.sh
-O0: 3=cmp 4=cmp 5=cmp 6=cmp 7=cmp 8=cmp 9=cmp 10=JT 11=JT 12=JT
-O2: 3=cmp 4=cmp 5=cmp 6=cmp 7=cmp 8=cmp 9=cmp 10=JT 11=JT 12=JT
```

(시험 삼아 만든 **7개짜리 산술 switch**(`a+b`, `a-b`, `a*b`, …)는 -O2 에서 분기 없이 모든 케이스를 계산해 놓고 `csel` 7개로 고르는 코드가 됐다 — switch 가 꼭 점프 테이블이나 비교 트리가 되는 것도 아니다. 10개짜리 산술 switch 는 다시 점프 테이블이었다: 연습 2.)

**③ pkt_score** — 위 그림. 오프셋 `#2`, `#4`, `#19` 가 그대로 보이고, 폭에 맞는 로드(`ldrh` = uint16 0 확장, `ldrb` = uint8, `ldr w` = 32비트)를 고른다. 크래시 덤프에서 `ldr w8, [x0, #4]` 를 보면 "x0 이 가리키는 구조체의 오프셋 4 필드" 라고 읽고, `offsetof` 로 어느 필드인지 찾는다.

**④ call_twice — 비(非)리프 함수: 프레임 + callee-saved**

-O0(19명령)은 `sub sp, sp, #48` / `stp x29, x30, [sp, #32]` 로 프레임을 만들고, x 를 `stur x0, [x29, #-8]` 슬롯에, a·b 를 `[sp, #16]`·`[sp, #8]` 슬롯에 넣어 **메모리로** 호출을 버틴 뒤 `mul` + `add` 한다. -O2(12명령):

```text
// -O2
	stp	x20, x19, [sp, #-32]!   // callee-saved 2개 저장 + sp -= 32 (pre-index)
	stp	x29, x30, [sp, #16]
	add	x29, sp, #16
	mov	x19, x0              // x 를 x19 에: helper 가 x19 는 보존해야 하므로 안전
	bl	_helper
	mov	x20, x0              // a 를 x20 에
	add	x0, x19, #1
	bl	_helper
	madd	x0, x0, x20, x19     // x0 = x19 + x0 * x20 = x + b*a  (mul+add 융합)
	ldp	x29, x30, [sp, #16]
	ldp	x20, x19, [sp], #32
	ret
```

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">call_twice -O2 의 프레임 (S = 진입 시 sp, 16바이트 정렬 유지)</text>
<g stroke="currentColor"><rect x="40" y="36" width="200" height="30" class="f-mut"/><rect x="40" y="66" width="200" height="30" class="f-acc"/><rect x="40" y="96" width="200" height="30" class="f-acc"/><rect x="40" y="126" width="200" height="30" class="f-ok"/><rect x="40" y="156" width="200" height="30" class="f-ok"/></g>
<g fill="currentColor" font-family="monospace" font-size="12">
<text x="60" y="56">호출자 프레임 …</text><text x="60" y="86">x30 (lr)    S-8</text><text x="60" y="116">x29 (옛 fp) S-16</text><text x="60" y="146">x19         S-24</text><text x="60" y="176">x20         S-32</text>
<text x="250" y="116">← x29 = sp+16</text><text x="250" y="176">← sp = S-32</text>
<text x="470" y="56">x0=x  → mov x19, x0</text><text x="470" y="78">bl helper (x0..x18 깨져도 됨)</text><text x="470" y="100">x0=a  → mov x20, x0</text>
<text x="470" y="122">add x0, x19, #1</text><text x="470" y="144">bl helper</text><text x="470" y="166">x0=b  → madd x0,x0,x20,x19</text><text x="470" y="188">       = b*a + x</text></g>
<text x="40" y="220" fill="currentColor">x19, x20 은 callee-saved: 쓰기 전에 저장, ret 전에 복원. 그 대가로 helper 호출 두 번을</text>
<text x="40" y="240" fill="currentColor">지나도 x, a 가 레지스터에 살아 있다. -O0 은 callee-saved 없이 스택 슬롯으로 버틴다 (19 vs 12 명령).</text>
</svg>
```

**⑤ get_checked — 분기 방향 반전 + 꼬리 호출로 프레임 소멸**

```text
// -O0: stp x29,x30 프레임, 3개 인자 spill, 그리고
	subs	x8, x8, x9           // i - n
	b.lo	LBB4_2               // i < n (unsigned, size_t) → 정상 경로로 점프
	...  bl _report_oob / stur / b LBB4_3 ...
// -O2 (6명령)
	cmp	x2, x1               // i - n
	b.hs	LBB4_2               // i >= n (unsigned) → 드문 경로로 점프, 정상 경로는 fall-through
	ldr	w0, [x0, x2, lsl #2] // return a[i]
	ret
LBB4_2:
	mov	x0, x2
	b	_report_oob          // 꼬리 호출 → bl 이 없으니 x30 저장도 프레임도 필요 없다
```

`size_t` 비교라 `lo/hs`(unsigned)다. `int` 인덱스였다면 `lt/ge` 가 나오고 음수 인덱스 검사가 따로 필요하다 — "unsigned 비교 하나로 `0 <= i < n` 을 검사" 하는 관용구.

**⑥ const_math — 곱셈·나눗셈이 사라진다**

```text
// -O0
	mov	w9, #9
	mul	w8, w8, w9           // x * 9
	mov	w10, #10
	udiv	w9, w9, w10          // x / 10   (진짜 나눗셈 명령)
	mov	w10, #4
	sdiv	w9, w9, w10          // y / 4
// -O2
	add	w8, w0, w0, lsl #3   // x*9 = x + (x<<3)
	mov	w9, #52429           // 0xcccd
	movk	w9, #52428, lsl #16  // w9 = 0xcccccccd ≈ 2^35/10
	umull	x9, w0, w9           // 64비트 곱
	lsr	x9, x9, #35          // = x / 10  (모든 uint32 에서 정확, 아래 증명)
	add	w8, w8, w9
	add	w9, w1, #3           // y + (4-1)
	cmp	w1, #0
	csel	w9, w9, w1, lt       // y<0 이면 y+3, 아니면 y   (0 쪽 절삭 보정)
	add	w0, w8, w9, asr #2   // + (보정된 y) >> 2 = y / 4
```

**-O2 에서 사라지거나 바뀐 것 — 정리표**

| 흔적 | -O0 | -O2 | 어디서 |
|---|---|---|---|
| 인자·지역변수 spill | 전부 `str` 로 스택에 | 레지스터에만 | 6개 전부 |
| 중복 로드 | `ldr x8, [sp, #8]` 같은 p 재로드 3번 | 0번 | pkt_score |
| 쓸데없는 명령 | `add w8, w8, #0`, `mov w8, w8`, 다음 블록으로 `b` | 없음 | dispatch, sum_loop |
| 루프 | i·s 가 메모리, 3블록·반복당 분기 2개 | 기본: 벡터화 8개/반복 + 스칼라 꼬리; `-fno-vectorize`: 4명령, i 소거, n 카운트다운 | sum_loop |
| 프레임 레코드 (`stp x29, x30`) | bl 있는 함수 모두 | bl 이 남은 call_twice 만 | dispatch, get_checked 는 소멸 |
| callee-saved x19/x20 | 안 씀 | 호출을 넘는 값 보관 | call_twice |
| `bl` + 반환값 저장 + `b` | 그대로 | `b _f` 꼬리 호출 | dispatch, get_checked |
| 점프 테이블 | 4B `.long`, `__text` 안, `ldrsw` | 1B `(목표-기준)>>2`, `__TEXT,__const`, `ldrb` | dispatch |
| 분기 배치 | 정상 경로로 점프 (`b.lo`) | 드문 경로로 점프 (`b.hs`), 정상은 fall-through | get_checked |
| 상수 곱셈/나눗셈 | `mul`, `udiv`, `sdiv` | shift-add, magic `umull`+`lsr`, 보정+`asr` | const_math |
| mul + add | `mul` 후 `add` | `madd` 하나 | call_twice |

### Part A — mystery1, mystery2 (코딩)

**mystery1** = `sub w8, w1, w0` / `subs w9, w0, w1` / `csel w0, w9, w8, gt` → `a > b ? a - b : b - a` 를 **32비트 mod 2^32** 로. GT 조건은 `Z==0 && N==V` 라서 뺄셈이 넘쳐도(V=1) 올바른 signed 비교다(ARCH-B02).

| 입력 (a, b) | w8 = b−a | w9 = a−b | NZCV | GT? | w0 |
|---|---|---|---|---|---|
| (3, 7) | 4 | 0xFFFFFFFC | N=1 Z=0 C=0 V=0 | ✗ (N≠V) | 4 |
| (INT32_MIN, INT32_MAX) | 0xFFFFFFFF | 0x00000001 | N=0 Z=0 C=1 V=1 | ✗ (N≠V) | 0xFFFFFFFF |

C 로 옮길 때 `(uint32_t)(a > b ? a - b : b - a)` 라고 쓰면 `a - b` 가 int32 오버플로 → **UB**. asm 의 `sub` 는 wrap 이 정의돼 있지만 C 추상 기계(abstract machine)에서 signed 오버플로는 정의되지 않으므로, 컴파일러는 "안 넘친다" 고 가정하고 최적화한다. 실제로 그렇게 쓴 버전을 돌려 보면:

```text
$ cc -O2 bug1.c shared_mystery.S && ./a.out      # (a > b ? a - b : b - a) 를 int 로 계산
FAIL ...  my_mystery1(INT32_MIN, INT32_MAX) == 0xFFFFFFFFu
  first mismatch: mystery1(1, -2147483648): asm=2147483649 mine=2147483647
33/35 passed
$ cc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined bug1.c shared_mystery.S && ./a.out
bug1.c:31:41: runtime error: signed integer overflow: 2147483647 - -2147483648 cannot be represented in type 'int32_t' (aka 'int')
```

-O2 에서는 "wrap 이겠지" 라는 기대와도 다른 값(2147483647)이 나왔다. 정답은 `(uint32_t)a - (uint32_t)b` — unsigned 산술은 C11 6.2.5p9 에 의해 mod 2^N 으로 정의된다.

**mystery2** = `x8 = (x < hi) ? x : hi` → `x0 = (x < lo) ? lo : x8`. 즉 `clamp`, 그런데 **lo 검사가 마지막** 이라 `lo > hi` 일 때 `x < lo` 이면 무조건 lo, 아니면 hi: `(5, 10, 0)` → x8=0, 5<10 → **10**, `(20, 10, 0)` → **0**. "hi 먼저 검사" 로 쓰면(`x > hi ? hi : x < lo ? lo : x`) 정상 범위에선 같지만 `lo > hi` 에서 다르다:

```text
FAIL ...  my_mystery2(5, 10, 0) == 10
  first mismatch: mystery2(-7, -6, -8): asm=-6 mine=-8
```

### Part B — mystery3: 회전된 루프

```text
	cbz	w0, LBB2_3        // x == 0 → w0(=0) 반환
	mov	x8, x0
	mov	w0, #0
LBB2_2:	and	w9, w8, #0x1      // 최하위 비트
	add	w0, w0, w9        // 개수 += 비트
	cmp	w8, #3            // 옛 x > 3 ?  (= 2칸 민 뒤에도 0 이 아닌가)
	lsr	w8, w8, #2        // x >>= 2  — 플래그 유지
	b.hi	LBB2_2
```

x = 0x2D (0b101101) 추적:

| 반복 | w8 (진입) | and → w9 | w0 | cmp w8,#3 → HI? | lsr 후 w8 |
|---|---|---|---|---|---|
| 1 | 0x2D | 1 | 1 | 45>3 ✓ | 0xB |
| 2 | 0xB | 1 | 2 | 11>3 ✓ | 0x2 |
| 3 | 0x2 | 0 | 2 | 2>3 ✗ → 탈출 | 0x0 |

→ 비트 0, 2, 4, …(짝수 위치)의 1 개수. `for (; x; x >>= 2) n += x & 1;`. 컴파일러가 `while (x)` 검사를 "시프트 전 값 > 3" 으로 바꿔 루프 끝 한 곳에 둔 것이 **루프 회전(loop rotation)** 이다.

### Part B — mystery4: `ldrsb` = signed char

`ldrsb w9, [x0, x8]` 은 바이트를 **부호 확장** 해서 읽고 `b.gt`(signed)로 32 와 비교한다 → 0x80–0xFF 바이트는 음수라 즉시 멈춘다. 즉 원본은 `while (*p > ' ') p++;` 이고 **Apple arm64 에서 `char` 는 signed** 다. AAPCS64(Linux)에서는 `char` 가 unsigned 라 같은 C 가 `ldrb` + `b.hi` 가 된다 (`demo_char_sign.c`, 실측):

<!-- code: demo_char_sign.c -->

```text
$ cc -O2 demo_char_sign.c && ./a.out                     # Mac 기본
CHAR_MIN=-128  tok_len("abc\x80def")=3  tok_len("caf\xC3\xA9 x")=3
$ cc -O2 -funsigned-char demo_char_sign.c && ./a.out     # Linux 식
CHAR_MIN=0  tok_len("abc\x80def")=7  tok_len("caf\xC3\xA9 x")=5
# clang -S -O2:                  ldrsb w9, [x0, x8] / cmp w9, #32 / b.gt LBB0_1
# clang -S -O2 -funsigned-char:  ldrb  w9, [x0, x8] / cmp w9, #32 / b.hi LBB0_1
# Lima VM (aarch64 Ubuntu, gcc 15.2 -O2, 기본 char): CHAR_MIN=0 ... =7 ... =5, 루프는
#   .L3: ldrb w2, [x1, 1]! / cmp w2, 32 / bhi .L3
```

(GCC 출력은 `#` 없는 즉시값, `bhi`(점 없음), `.L3` 라벨, pre-index 루프 — 같은 ISA, 다른 표기·다른 루프 모양.) 그래서 정답은 플랫폼과 무관하게 `(signed char)s[i] > ' '` 로 **asm 이 한 일을 명시** 한다. `(unsigned char)` 로 쓰면:

```text
FAIL ...  my_mystery4("caf\xC3\xA9 au lait") == 3
FAIL ...  my_mystery4("abc\x80" "def") == 3
  first mismatch: mystery4(len 32 string): asm=4 mine=8
```

끝의 `sub x0, x8, #1` 은 비교 **전에** `add x8, x8, #1` 을 했기 때문에 하나를 되돌리는 것 — 멈춘 바이트의 인덱스 = 길이.

### Part C — mystery5: SoA 센서 테이블

```text
	ldr	w8, [x0]              // n = t->n            (오프셋 0)
	cbz	w8, LBB4_4            // n == 0 → INT32_MIN
	add	x9, x0, #36           // x9 = &t->temp[0]    (오프셋 36)
LBB4_2:	ldurh	w10, [x9, #-32]       // t->id[i]  = temp[i] 주소 - 32 = 4 + 2i  (음수 오프셋 → unscaled ldur)
	cmp	w10, w1
	b.eq	LBB4_5
	add	x9, x9, #2            // i++ (두 배열을 포인터 하나로 같이 걷는다)
	subs	x8, x8, #1            // 남은 개수--
	b.ne	LBB4_2
LBB4_4:	mov	w0, #-2147483648      // 못 찾음: INT32_MIN
	ret
LBB4_5:	ldrsh	w8, [x9]              // temp[i] (int16 → 부호 확장)
	add	w8, w8, w8, lsl #2    // *5
	add	w8, w8, w8, lsr #31   // 음수면 +1  (2로 나눌 때의 0 쪽 절삭 보정)
	asr	w0, w8, #1            // /2
```

`{n=4, id={0x10,0x22,0x10,0x7F}, temp={100,-3,555,-32768}}` 에서 id=0x22 찾기:

| 단계 | x8 | x9 (t+…) | 읽은 값 | 결과 |
|---|---|---|---|---|
| 진입 | 4 | 36 | — | — |
| 반복 1 | 4→3 | 36→38 | id[0]=0x10 ≠ 0x22 | 계속 |
| 반복 2 | 3 | 38 | id[1]=0x22 = | LBB4_5 |
| LBB4_5 | | | temp[1] = −3 | −15 → −15+1 = −14 → asr → **−7** |

`-3*5/2` 는 C 에서 −7(0 쪽 절삭). `>> 1` 로 쓰면 −8(−∞ 쪽)이 되어 틀린다 (실측: `first mismatch: mystery5(n=8, id=0): asm=-1457 mine=-1458`). 두 가지 더: (1) 루프가 `id[i] == id` 를 **처음** 만나면 끝 — 중복 id 는 첫 번째. (2) `cmp w10, w1` 은 `w1` 의 상위 16비트가 0 이라고 믿는다 — Apple ABI 는 32비트 미만 인자를 **호출자가** 확장한다(B01, Apple 문서 — 확인 권장). 같은 C 를 `--target=aarch64-linux-gnu` 로 컴파일하면 callee 가 직접 `and w10, w1, #0xffff` 를 넣는다(실측). 즉 이 `.S` 는 Linux 에서도 어셈블은 되지만 **mystery5 는 Apple ABI 전제 코드** 다.

### Part C — mystery6: 링 버퍼와 magic number

```text
	sub	w8, w0, w1            // head - tail       (mod 2^32)
	add	w8, w8, #24           // + 24
	mov	w9, #43691            // 0xaaab
	movk	w9, #43690, lsl #16   // w9 = 0xaaaaaaab
	umull	x9, w8, w9
	lsr	x9, x9, #36           // q = w8 / 24
	mov	w10, #24
	msub	w0, w9, w10, w8       // w8 - q*24 = w8 % 24
```

**왜 0xAAAAAAAB >> 36 이 /24 인가** (Granlund–Montgomery): m = ⌈2^36/24⌉ = 2863311531 = 0xAAAAAAAB, 오차 e = 24m − 2^36 = **8**. x·m/2^36 = x/24 + x·e/(24·2^36) 이고, x < 2^32 이면 x·e < 2^35 이라 두 번째 항이 1/24 보다 작다 → 내림이 x/24 의 내림과 같다. /10 의 0xCCCCCCCD (>>35) 도 e = 2 로 같은 논리(파이썬으로 확인: `0xAAAAAAAB*24 - 2**36 == 8`, `0xCCCCCCCD*10 - 2**35 == 2`). 64비트 `x/10` 이면 `umulh`(곱의 상위 64비트)가 나온다: `mov x8, #-3689348814741910324` (=0xcccccccccccccccc, 논리 즉시값이라 한 명령, ARM-B03) / `movk x8, #52429` / `umulh x8, x0, x8` / `lsr x0, x8, #3` (실측).

| (head, tail) | w8 | umull 결과 | q | w0 |
|---|---|---|---|---|
| (50, 3) | 71 | 0x2f5555556d | 2 | 71 − 48 = 23 |
| (2, 5) | 21 | 0xe00000007 | 0 | 21 |
| (0, 100) | 0xFFFFFFB4 | — | — | 12 (2^32 ≡ 16 mod 24 이라 계약 밖 입력은 "이상한" 값) |

C: `(head - tail + 24u) % 24u` — 전부 uint32_t 라 wrap 이 정의돼 있고, 이 식을 `-O2` 로 컴파일하면 위 asm 이 그대로 나온다.

### Part C — 링크 전 .o 의 점프 테이블을 바이트로 읽기

크래시 덤프나 펌웨어 이미지에는 `.s` 가 없다. 있는 건 바이트와 디스어셈블리뿐:

```text
$ clang -c -O2 demo_six.c -o six.o && objdump -s -j __const six.o
 01a8 0006020c 0e10040c 080a140c 12        .............
$ objdump -d -r --no-show-raw-insn six.o      (dispatch 발췌)
      b4:      	adr	x10, 0xc4 <_dispatch+0x24>
      b8:      	ldrb	w11, [x9, x8]
      bc:      	add	x10, x10, x11, lsl #2
      c0:      	br	x10
      c4:      	mov	x0, x1
      c8:      	b	0xc8 <_dispatch+0x28>
		00000000000000c8:  ARM64_RELOC_BRANCH26	_op_delete_sq
      ...
      f4:      	mov	w0, #-0x16              ; =-22
      f8:      	ret
```

(`b 0xc8` 처럼 자기 자신을 가리키는 건 아직 링크 전이라서 — 진짜 목적지는 재배치(relocation) `ARM64_RELOC_BRANCH26 _op_delete_sq` 가 말해 준다.)

```svg
<svg viewBox="0 0 740 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">dispatch -O2 점프 테이블 (__TEXT,__const 의 실제 바이트): 목표 = 0xc4 + 엔트리 × 4</text>
<g fill="currentColor" font-size="12"><text x="10" y="52">opc</text><text x="10" y="80">byte</text><text x="10" y="108">목표</text></g>
<g stroke="currentColor"><rect x="60" y="62" width="50" height="26" class="f-ok"/><rect x="110" y="62" width="50" height="26" class="f-ok"/><rect x="160" y="62" width="50" height="26" class="f-ok"/><rect x="210" y="62" width="50" height="26" class="f-warn"/><rect x="260" y="62" width="50" height="26" class="f-ok"/><rect x="310" y="62" width="50" height="26" class="f-ok"/><rect x="360" y="62" width="50" height="26" class="f-ok"/><rect x="410" y="62" width="50" height="26" class="f-warn"/><rect x="460" y="62" width="50" height="26" class="f-ok"/><rect x="510" y="62" width="50" height="26" class="f-ok"/><rect x="560" y="62" width="50" height="26" class="f-ok"/><rect x="610" y="62" width="50" height="26" class="f-warn"/><rect x="660" y="62" width="50" height="26" class="f-ok"/></g>
<g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle"><text x="85" y="52">0</text><text x="135" y="52">1</text><text x="185" y="52">2</text><text x="235" y="52">3</text><text x="285" y="52">4</text><text x="335" y="52">5</text><text x="385" y="52">6</text><text x="435" y="52">7</text><text x="485" y="52">8</text><text x="535" y="52">9</text><text x="585" y="52">10</text><text x="635" y="52">11</text><text x="685" y="52">12</text>
<text x="85" y="80">00</text><text x="135" y="80">06</text><text x="185" y="80">02</text><text x="235" y="80">0c</text><text x="285" y="80">0e</text><text x="335" y="80">10</text><text x="385" y="80">04</text><text x="435" y="80">0c</text><text x="485" y="80">08</text><text x="535" y="80">0a</text><text x="585" y="80">14</text><text x="635" y="80">0c</text><text x="685" y="80">12</text>
<text x="85" y="108">c4</text><text x="135" y="108">dc</text><text x="185" y="108">cc</text><text x="235" y="108">f4</text><text x="285" y="108">fc</text><text x="335" y="108">104</text><text x="385" y="108">d4</text><text x="435" y="108">f4</text><text x="485" y="108">e4</text><text x="535" y="108">ec</text><text x="585" y="108">114</text><text x="635" y="108">f4</text><text x="685" y="108">10c</text></g>
<g fill="currentColor" font-family="monospace" font-size="12">
<text x="60" y="140">opc 1 → 06 → 0xc4 + 24 = 0xdc → b _op_create_sq</text>
<text x="60" y="160">opc 3, 7, 11 (구멍) → 0c → 0xf4 → mov w0, #-22 / ret</text>
<text x="60" y="180">opc 12 → 0x12 = 18 → 0xc4 + 72 = 0x10c → b _op_async_ev</text>
<text x="60" y="200">opc 13 이상, 음수 → cmp w0, #12 / b.hi → 테이블을 안 읽는다</text></g>
<text x="60" y="226" fill="currentColor">ldrb 엔트리 최대 255 × 4 = 기준에서 1020 바이트 앞까지만 가리킬 수 있다.</text>
</svg>
```

### ELF/Linux 판과 "진짜 clang 출력" 증명

`shared_mystery.S` 는 `SYM(n)` 매크로(Apple `_n`, ELF `n`)와 `TYPE/SIZE`(ELF 전용 `.type n, %function` / `.size n, . - n`)로 감싸서 **같은 파일이 Linux 에서도** 어셈블된다(`practice.S` 는 `@PAGE/@PAGEOFF` ↔ `:lo12:` 와 `__TEXT,__const` ↔ `.rodata` 도 매크로). Lima VM(aarch64 Ubuntu)에서 실제로 빌드했다:

```text
$ limactl shell elinux -- bash -lc 'cd /tmp/ARM-B02_work && ...'   (gcc 15.2 / clang 21.1.8 각각)
[gcc starter] 6/35 passed (exit=1)   [gcc solution] 35/35 passed   [gcc practice] 21/21 passed
[clang starter] 6/35 passed (exit=1) [clang solution] 35/35 passed [clang practice] 21/21 passed
$ gcc -c shared_mystery.S -o m.o && objdump -d m.o
0000000000000000 <mystery1>:
   0:	4b000028 	sub	w8, w1, w0
   4:	6b010009 	subs	w9, w0, w1
   8:	1a88c120 	csel	w0, w9, w8, gt
   c:	d65f03c0 	ret
```

ELF 에서는 `LBB2_3` 같은 라벨이 LOCAL 심볼로 심볼 테이블에 남는다(`readelf -s` 로 확인; Mach-O 의 `L` 접두는 어셈블러 임시 라벨, ELF 의 대응은 `.L`). 그리고 손으로 바꾼 게 주석·지시어뿐임을 `check_mystery_sync.sh` 가 증명한다:

```text
$ bash check_mystery_sync.sh
IDENTICAL: 122 instruction lines (clang -O2 vs shared_mystery.S + practice.S)
```

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c shared_mystery.S -o /tmp/ARM-B02_v && /tmp/ARM-B02_v
# 35/35 passed  (0.02 s)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c shared_mystery.S -o /tmp/ARM-B02_san && /tmp/ARM-B02_san
# 35/35 passed  — ASan/UBSan 깨끗 (단, 계측은 C 쪽만; .S 안의 메모리 접근은 ASan 이 못 본다)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 것 |
|---|---|---|
| `csel x0, x1, x8, lt` 를 "lt 면 x8" 로 읽음 | `csel Rd, Rn, Rm, c` = `c ? Rn : Rm` — 첫 소스가 참일 때 | 조건 참 → 첫 번째 |
| asm 의 wrap 을 `int32_t` 뺄셈으로 옮김 | C 에서 signed 오버플로는 UB. -O2 실측 결과가 asm 과 달랐다(2147483647 vs 2147483649) | `(uint32_t)a - (uint32_t)b` |
| `cmp w8, #3` 을 시프트 **후** 값 비교로 읽음 | `lsr` 은 플래그를 안 바꾼다; cmp 는 옛 값 기준 | "옛 값 > 3" ⟺ "새 값 ≠ 0" |
| `ldrsb`/`b.gt` 를 놓치고 unsigned 로 구현 | 0x80 이상 바이트(UTF-8)에서 결과가 다름 | `(signed char)` 명시 |
| `add w8, w8, w8, lsr #31` + `asr #1` 을 그냥 `>> 1` 로 | 보정은 음수일 때 0 쪽 절삭을 만드는 것 — `>>` 는 −∞ 쪽 | C 의 `/ 2` |
| clamp 에서 hi 를 먼저 검사 | `lo > hi` 일 때 누가 이기는지가 바뀜 | asm 순서대로 lo 가 마지막 |
| 크래시 백트레이스에 `get_checked` 가 없다고 당황 | `b _report_oob` 꼬리 호출은 lr 을 안 바꾼다 → 스택에 흔적 없음 | 디스어셈블리에서 `b _f` 를 찾아 호출 경로 복원 |

## 8. 면접 Q&A

<details>
<summary>Q1. -O0 출력은 왜 모든 변수를 스택에 넣었다가 다시 읽나요? 성능 측정에 -O0 을 쓰면 왜 안 되죠?</summary>

-O0 은 "각 변수가 고정된 메모리 주소를 가진다" 를 보장해서 디버거가 아무 줄에서나 변수를 읽고 바꿀 수 있게 한다. 그래서 문장마다 load/store 가 생긴다(pkt_score 12 vs 6 명령, dispatch 65 vs 31). -O0 의 병목은 이 메모리 왕복이라, -O0 측정은 실제 코드의 비용 구조와 다르다. 펌웨어에서 "Debug 빌드에선 타이밍이 맞는데 Release 에선 깨진다" 의 출발점이기도 하다.

</details>

<details>
<summary>Q2. call_twice -O2 는 x 를 왜 x9 가 아니라 x19 에 두나요? 그 비용은 누가 내죠?</summary>

x9 는 caller-saved(임시) 레지스터라 `bl _helper` 뒤에 값이 남는다는 보장이 없다. x19 는 callee-saved — helper 가 쓰려면 저장·복원해야 한다. 대신 call_twice 자신도 x19/x20 을 쓰기 전에 `stp x20, x19, [sp, #-32]!` 로 저장하고 `ldp` 로 복원하는 비용을 낸다. 호출을 넘는 값이 많을수록 callee-saved 를 쓰는 게 이득(저장 1번 vs 호출마다 spill/reload). 상세 규칙은 ARM-M01.

</details>

<details>
<summary>Q3. `b _report_oob` 같은 꼬리 호출은 무슨 의미이고, 디버깅에 어떤 영향이 있나요?</summary>

마지막 일이 "다른 함수를 호출하고 그 값을 그대로 반환" 이면 `bl` + `ret` 대신 `b` 로 점프한다. lr(x30)은 원래 호출자 주소 그대로라 피호출자의 `ret` 이 원래 호출자로 바로 돌아간다. 그래서 프레임도 x30 저장도 필요 없다(get_checked 6명령). 대가: 백트레이스에서 get_checked 가 사라진다 — 크래시 덤프에서 "report_oob 를 부른 게 main?" 처럼 보이면 꼬리 호출을 의심한다. `-fno-optimize-sibling-calls` 로 끌 수 있다.

</details>

<details>
<summary>Q4. switch 가 언제 점프 테이블이 되고, 테이블 범위 검사는 어떻게 하나요?</summary>

케이스 수와 밀도에 대한 휴리스틱이다. 이 Apple clang 21 에서는 9개 이하가 비교 트리, 10개부터 테이블이었다(실측, 버전 의존). 범위 검사는 `cmp w0, #12` + `b.hi` 하나 — **unsigned** 비교라 음수 opc 도 큰 수로 보여 default 로 간다. 구멍(opc 3, 7, 11)은 default 블록을 가리키는 엔트리로 채운다. -O2 는 엔트리를 `(목표−기준)>>2` 1바이트로 압축해 `__TEXT,__const` 에 둔다(가까우면 1B, 멀면 2B/4B — LLVM 이 고른다, 확인 권장). BTI 가 켜진 코드라면 `br` 목적지에 `bti j` 가 필요하다(ARM-A10).

</details>

<details>
<summary>Q5. x/10 이 umull + lsr #35 로 바뀌었습니다. 왜 정확하고, 왜 이렇게 하나요?</summary>

m = ⌈2^35/10⌉ = 0xCCCCCCCD, 오차 e = 10m − 2^35 = 2. x·m/2^35 = x/10 + x·e/(10·2^35) 에서 x < 2^32 이면 두 번째 항 < 1/10 이라 내림이 일치한다. 이유: 나눗셈 명령은 곱셈보다 지연이 훨씬 길다(정확한 사이클은 코어마다 다르니 확인 권장). Cortex-M0 처럼 하드웨어 나눗셈이 아예 없는 코어에서는 이 변환이 라이브러리 호출(`__aeabi_uidiv`)을 없애 준다. AArch64 는 `udiv`/`sdiv` 가 있고 0 으로 나눠도 트랩 없이 0 — 그건 ARM-B07.

</details>

<details>
<summary>Q6. y/4 가 `add w9, w1, #3` / `csel … lt` / `asr #2` 로 나왔습니다. 그냥 `asr #2` 면 안 되나요?</summary>

`asr` 은 −∞ 쪽으로 내림(−5 >> 2 = −2)이고 C 의 `/` 는 0 쪽 절삭(−5/4 = −1, C11 6.5.5p6)이다. 음수일 때만 `2^k − 1` 을 더하면 두 결과가 같아진다. mystery5 의 `add w8, w8, w8, lsr #31` 은 k=1 의 같은 보정을 분기 없이(부호 비트 = 1) 한 것. 그래서 asm 에서 "부호 비트를 더하고 asr" 패턴을 보면 → signed 나눗셈 by 2^k.

</details>

<details>
<summary>Q7. 같은 C 가 Mac 에선 ldrsb, Linux arm64 에선 ldrb 가 됐습니다. 왜죠? 펌웨어에 무슨 영향이 있나요?</summary>

plain `char` 의 부호는 구현 정의(implementation-defined)다. AAPCS64 는 unsigned, Apple arm64 는 signed 로 정했다(실측 CHAR_MIN −128 vs 0; Apple 문서 "Writing ARM64 code for Apple platforms" 참고). 0x80 이상 바이트(UTF-8, 바이너리 프로토콜 헤더)를 `char` 로 비교하는 파서는 플랫폼마다 다르게 동작한다. 해결: 바이트 데이터는 `uint8_t`, 비교 의도가 signed 면 `int8_t` 를 쓴다. asm 에서는 `ldrsb`/`gt` 냐 `ldrb`/`hi` 냐로 즉시 구분된다.

</details>

<details>
<summary>Q8. 0xcccccccccccccccd 같은 64비트 상수는 몇 명령으로 만들어지나요?</summary>

일반적으로 MOVZ + MOVK 최대 4개(16비트씩). 하지만 clang 은 `mov x8, #0xcccccccccccccccc` (반복 패턴이라 **논리 즉시값**, 실제로는 `orr x8, xzr, #imm`) + `movk x8, #0xcccd` 로 2개에 끝냈다(실측; objdump 인코딩 `b202e7e8` = 논리 즉시값 ORR 클래스). 주소면 `adrp` + `add` (페이지 + 하위 12비트), 멀거나 PIC 이면 GOT(ARM-A09). 이 계획을 직접 세우는 건 ARM-B03.

</details>

## 9. 추가 연습

`practice.S` 의 mystery7–10 도 진짜 clang -O2 출력이다(같은 정리 규칙, `check_mystery_sync.sh` 로 확인).

### 연습 1 — mystery7: 상수 + 루프

`umull … 0xcccccccd` / `lsr #35` / `msub … w10(=10)` / `cmp w8, #9` / `b.hi`. 무엇을 계산하나?

<details>
<summary>답</summary>

10진 자릿수 합. 루프마다 q = x/10, 나머지 = x − 10q 를 더하고, "옛 x > 9" ⟺ "q ≠ 0" 이면 반복(mystery3 과 같은 회전 루프). `mystery7(4294967295) == 57`.

</details>

### 연습 2 — mystery8: 1바이트 점프 테이블

`practice.S` 를 `clang -c` 하면 `__const` 바이트가 `00 0d 07 09 02 0f 14 0b 18 05`, 기준 `adr x10` = 0x5c 이다(실측). op=5 는 어느 블록이고 무엇을 계산하나? C 로 다시 쓸 때 UB 가 생기는 케이스는?

<details>
<summary>답</summary>

0x0f = 15 → 0x5c + 60 = 0x98 → `add w8, w1, #7` / `cmp w1, #0` / `csel lt` / `asr #3` = `x / 8` (0 쪽 절삭). `x + 7`, `x * 3`, `-x`, `x - 100`, `x << 4` 는 원본 C 에서 오버플로/음수 시프트 시 UB 지만 asm 은 wrap → `uint32_t` 로 계산 후 `int32_t` 로 변환(구현 정의, clang/gcc 는 mod 2^32). `cmp w0, #9` / `b.hi` 는 unsigned 라 음수 op 도 default(0).

</details>

### 연습 3 — mystery9: 한 줄

`bfi w0, w1, #4, #7` 은 무엇인가? C 로.

<details>
<summary>답</summary>

w0 의 비트 [10:4] 에 w1 의 하위 7비트를 넣고 나머지는 보존 = C-B01 의 `field_set(reg, 4, 7, v)`. 원본 `(reg & ~0x7F0u) | ((v & 0x7Fu) << 4)` 가 명령 하나가 됐다(BFI 상세는 ARM-B06).

</details>

### 연습 4 — mystery10: callee-saved 4개 + blr

프롤로그가 x19–x22 를 저장하고 루프에서 `blr x19` 를 부른다. 각 callee-saved 레지스터에는 무엇이 들어 있나? 프레임 레코드는 sp+몇에 있나?

<details>
<summary>답</summary>

x19 = 함수 포인터 f, x20 = 남은 개수 n, x21 = 배열 포인터(post-index 로 전진), x22 = 합. 모두 `blr` 을 넘어 살아야 하므로 callee-saved. 프레임: `[sp]`=x22, `[sp+8]`=x21, `[sp+16]`=x20, `[sp+24]`=x19, `[sp+32]`=x29, `[sp+40]`=x30, `x29 = sp+32` (call_twice 처럼 Apple clang 은 fp/lr 을 프레임 맨 위에 둔다). = `for (i…) s += f(a[i]);`. 테스트는 콜백 호출 순서·횟수와 n=0 에서 0번 호출도 확인한다.

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_mystery.S -o /tmp/ARM-B02_p && /tmp/ARM-B02_p
# 21/21 passed  (ASan/UBSan 빌드도 21/21)
```

## 10. 핵심 정리

- 읽는 순서: 지시어·CFI 버리기 → 어떤 인자 레지스터(w/x)를 읽나 → 플래그를 누가 세우고 누가 쓰나 → `ret` 직전 x0/w0.
- `csel Rd, Rn, Rm, c` = `c ? Rn : Rm`. `gt/lt/ge/le` = signed, `hi/lo/hs/ls` = unsigned. `ldrsb/ldrsh` = signed 타입, `ldrb/ldrh` = unsigned 타입.
- -O0: 변수마다 스택 슬롯, 문장마다 load/store. -O2: 레지스터, 루프 회전·카운트다운, callee-saved 로 호출 넘기기, 꼬리 호출 `b`, 프레임 소멸.
- 점프 테이블: `cmp #max` + `b.hi`(unsigned 범위 검사) → `ldrb/ldrsw` 엔트리 → `add` → `br`. -O2 는 1바이트 `(목표−기준)>>2`.
- 큰 홀수 상수 + `umull/umulh` + `lsr` = 상수 나눗셈, `msub` = 나머지. 부호 비트 더하기 + `asr` = signed ÷ 2^k (0 쪽 절삭).
- asm 의 wrap 을 C 로 옮길 땐 unsigned 로 계산 — signed 오버플로는 UB 라 -O2 가 다른 값을 낼 수 있다(실측).
- Apple arm64: `char` signed, 32비트 미만 인자는 호출자가 확장, `_` 접두, `.cfi` 는 항상 남음. AAPCS64 Linux: `char` unsigned, callee 가 확장.

## 참고자료

- Arm, *Arm Architecture Reference Manual for A-profile* (DDI 0487) — CSEL, UMULL/UMULH, MSUB/MADD, LDRSB/LDRSH, 조건 코드 표: https://developer.arm.com/documentation/ddi0487/latest
- Arm, *Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)*: https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst
- Apple, *Writing ARM64 code for Apple platforms* (char 부호, 인자 확장, x18, 프레임 포인터): https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms
- T. Granlund, P. Montgomery, *Division by Invariant Integers using Multiplication*, PLDI 1994 / H. Warren, *Hacker's Delight* 2판 10장
- Compiler Explorer (같은 C 를 clang/gcc, Apple/Linux 타깃으로 나란히 보기): https://godbolt.org
- 이 레슨 파일: `demo_six.c`, `demo_six_O0.s`, `demo_six_O2.s`, `gen_listings.sh`, `demo_jt_threshold.sh`, `demo_char_sign.c`, `check_mystery_sync.sh`, `demo_mystery_src.c`(정답 원본)
