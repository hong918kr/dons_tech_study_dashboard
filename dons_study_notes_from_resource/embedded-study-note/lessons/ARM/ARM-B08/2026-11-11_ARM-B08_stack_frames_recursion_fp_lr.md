---
id: ARM-B08
time: 60분
updated: 2026-11-11
---
# ARM-B08 스택 프레임과 재귀: FP/LR 저장

## 학습 목표

- 프롤로그 `stp x29, x30, [sp, #-N]!` / `mov x29, sp` 와 에필로그 `ldp x29, x30, [sp], #N` / `ret` 를 손으로 쓰고, 각 줄이 sp · x29 · x30 · 메모리를 어떻게 바꾸는지 표로 따라간다.
- **프레임 레코드(frame record)** {이전 FP, 돌아갈 LR} 가 연결 리스트라는 것을 이해하고, C 와 asm 양쪽에서 그 체인을 걸어 백트레이스를 만든다.
- callee-saved `x19–x28` 을 "쓰는 것만" 저장/복원하고, 지역 배열 크기를 16 의 배수로 올려 `sub sp, sp, #N` 한다.
- lldb 로 `bt` · `register read fp sp lr` · `memory read $fp` 를 해서 스택 그림과 실제 주소가 일치하는지 확인한다.
- LR 저장을 빠뜨리거나 sp 를 8 만 내린 코드가 **실제로 어떻게 죽는지** (SIGSEGV / PC 정렬 SIGBUS / SP 정렬 SIGBUS) 크래시 레지스터로 읽는다.

## 1. 문제

크래시 덤프에 남는 것은 결국 `pc`, `lr`, `sp`, `fp` 네 개와 스택 메모리다. "이 프롤로그는 뭘 저장하나요? 이 덤프에서 호출자를 찾아 보세요." 는 펌웨어 면접의 단골이고, SSD 컨트롤러(Cortex-R/A) 의 panic 로거, Apple/NVIDIA SoC 의 커널 백트레이스가 모두 이 레슨의 FP 체인 걷기로 만들어진다.

ARM-B01 에서는 프레임이 필요 없는 **leaf 함수** 만 썼다. 다른 함수를 부르는 순간 `bl` 이 x30(LR) 을 덮어쓰므로, 내 반환 주소를 어딘가에 저장해야 한다. 그 "어딘가" 가 스택 프레임이다. AAPCS64 (Arm 64비트 호출 규약) 와 Apple 규칙은 x29(FP) 가 항상 **프레임 레코드** — 16 바이트짜리 {호출자의 x29, 내 반환 주소} 쌍 — 를 가리키라고 한다. 그래서 디버거는 심볼 없이도 `[fp]` → `[[fp]]` → … 로 호출 체인을 복원한다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">AArch64 정수 레지스터 역할 (AAPCS64 + Apple) 과 FP 체인</text>
  <g stroke="currentColor">
    <rect x="10" y="38" width="330" height="26" class="f-mut"/>
    <rect x="10" y="64" width="330" height="26" class="f-mut"/>
    <rect x="10" y="90" width="330" height="26" class="f-mut"/>
    <rect x="10" y="116" width="330" height="26" class="f-warn"/>
    <rect x="10" y="142" width="330" height="26" class="f-ok"/>
    <rect x="10" y="168" width="330" height="26" class="f-acc"/>
    <rect x="10" y="194" width="330" height="26" class="f-acc"/>
    <rect x="10" y="220" width="330" height="26" class="f-acc"/>
  </g>
  <g fill="currentColor">
    <text x="18" y="56"><tspan font-family="monospace">x0–x7  </tspan> 인자 · x0 반환 (호출로 깨짐)</text>
    <text x="18" y="82"><tspan font-family="monospace">x8–x15 </tspan> 임시 (호출로 깨짐)</text>
    <text x="18" y="108"><tspan font-family="monospace">x16–x17</tspan> IP0/IP1: veneer 가 깰 수 있음</text>
    <text x="18" y="134"><tspan font-family="monospace">x18    </tspan> 플랫폼 예약 — Apple: 금지</text>
    <text x="18" y="160"><tspan font-family="monospace">x19–x28</tspan> callee-saved: 쓰면 내가 저장</text>
    <text x="18" y="186"><tspan font-family="monospace">x29 FP </tspan> 프레임 레코드를 가리킴</text>
    <text x="18" y="212"><tspan font-family="monospace">x30 LR </tspan> bl 이 "돌아갈 주소" 를 넣음</text>
    <text x="18" y="238"><tspan font-family="monospace">sp     </tspan> 항상 16 의 배수</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="430" y="50" width="200" height="34" class="f-acc"/>
    <rect x="430" y="140" width="200" height="34" class="f-acc"/>
    <rect x="430" y="230" width="200" height="34" class="f-acc"/>
    <line x1="530" y1="50" x2="530" y2="84"/><line x1="530" y1="140" x2="530" y2="174"/><line x1="530" y1="230" x2="530" y2="264"/>
    <path d="M440 230 L440 184"/><path d="M434 192 L440 184 L446 192"/>
    <path d="M440 140 L440 94"/><path d="M434 102 L440 94 L446 102"/>
    <path d="M370 247 L426 247"/><path d="M418 241 L426 247 L418 253"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="445" y="72">이전 fp</text><text x="540" y="72">LR (호출자로)</text>
    <text x="445" y="162">이전 fp</text><text x="540" y="162">LR</text>
    <text x="445" y="252">이전 fp</text><text x="540" y="252">LR</text>
    <text x="356" y="240">x29</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="640" y="72">바깥 (높은 주소)</text>
    <text x="640" y="252">지금 함수</text>
    <text x="360" y="295">[fp] = 호출자의 레코드 주소, [fp+8] = 내 반환 주소.</text>
    <text x="360" y="312">이전 fp == 0 이면 체인 끝 (AAPCS64).</text>
  </g>
</svg>
```

### Part A — 재귀 함수 세 개를 asm 으로

- `uint64_t fact_asm(uint64_t n)` — 재귀 n! (mod 2^64). 프레임 32 B, `x19` 에 n 보관.
- `uint64_t fib_asm(uint64_t n)` — 순진한 재귀 fibonacci. `x19 = n`, `x20 = fib(n-1)` — 두 번째 `bl` 을 건너서 살아남아야 하는 값이 둘이다.
- `int64_t local_array_sum(int32_t seed, void (*cb)(int32_t *a, size_t n))` — 스택에 `int32_t a[10]` (40 B → **48 B 로 올림**), `a[i] = seed + i` (32비트 wrap), `cb` 가 있으면 `cb(a, 10)` 을 부른 뒤 a 를 **다시 읽어** int64 로 합한다.
- 두 재귀 함수의 base case 는 C 훅 `base_hook()` 을 `bl` 로 부른다. 하네스가 그 안에서 FP 체인을 걷는다.

### Part B — 반환값 너머를 검증 (훅 + 보존 검사)

- base case 에서 본 체인 깊이: `fact(n)` 이면 `max(n,1)`, fib 는 훅 호출 수 `fib(n+1)`, 최대 깊이 `max(n,1)`. 깊이 10000 (스택 320 KB) 도.
- 저장된 LR 은 전부 `[fact_asm, fact_asm_end)` 안에 있어야 하고, 재귀 복귀 주소는 **하나**, base 복귀 주소는 따로 하나.
- 프레임 레코드는 16 정렬, 간격 정확히 32 B. `[fp+16]` 에 저장된 x19 는 **호출자의** n.
- 주어진 도우미 `call_preserve_check` (shared_regcheck.S) 로 x19–x28 과 sp 보존 확인. 경계값: `INT32_MIN`, `INT32_MAX` seed (손으로 쓴 기대값).

### Part C — 크래시 로거와 스택 버퍼 콜백

- `int fp_backtrace(uintptr_t *out, int max)` — leaf asm. `out[0] = x30`, 이후 `ldp` 로 {이전 fp, LR} 을 읽으며 올라간다. 멈춤 조건 4개: max 도달 / fp == 0 / fp 16 정렬 아님 / 다음 fp 가 위로 안 감. panic 핸들러가 쓰는 바로 그 코드다.
- 하네스는 이 결과를 C 로 걸은 체인과 한 칸씩 비교하고, `dladdr` 로 `fact_asm+0x2c` 같은 크래시 로그 줄을 찍는다.
- `local_array_sum` 의 콜백은 받은 포인터가 16 정렬인지, 호출자 x29 에서 정확히 48 B 아래인지 확인한다 — NVMe 커맨드나 DMA 디스크립터를 스택에 만들어 드라이버 함수에 넘기는 패턴이다.

## 2. Problem (English)

> **Stack frames by hand (AArch64, Apple arm64 / AAPCS64).**
>
> Write in assembly (`.S`, called from C):
>
> 1. `uint64_t fact_asm(uint64_t n)` (recursive, result mod 2^64) and `uint64_t fib_asm(uint64_t n)` (naive double recursion). Each must build a proper frame record (`stp x29, x30` / `mov x29, sp`), keep `sp` 16-byte aligned, and save/restore only the callee-saved registers it uses. In the base case call the C function `void base_hook(void)`.
> 2. `int64_t local_array_sum(int32_t seed, void (*cb)(int32_t *a, size_t n))`: allocate `int32_t a[10]` on the stack, fill `a[i] = seed + i` (32-bit wrap), call `cb(a, 10)` if non-NULL, then return the 64-bit sum of the (possibly modified) array.
> 3. `int fp_backtrace(uintptr_t *out, int max)`: a leaf function that writes its own return address to `out[0]` and then walks the frame-pointer chain, storing each saved LR. Stop on `max`, a null or misaligned FP, or a chain that does not move to higher addresses. Return the count.
>
> Constraints: do not touch `x18`; `sp % 16 == 0` at every instruction that accesses memory through `sp`; tests check depth, saved-LR ranges, frame spacing, callee-saved preservation, and compare your backtrace with one walked in C.
>
> Example: `fact_asm(5) == 120`; inside `base_hook` during `fact_asm(4)`, the saved LRs are `fact_asm+0x1c` once and `fact_asm+0x2c` three times.
>
> Follow-ups: Why must a non-leaf function save LR? Why 16-byte alignment? Who saves x19? Where is the frame record inside a clang-generated frame? What happens to the backtrace with a tail call?

## 3. 시작 코드

하네스 (starter / solution 공용, 테스트 30개):

<!-- code: harness.c -->

주어진 도우미 — x19–x28 에 무늬를 넣고 함수를 부른 뒤 무늬가 그대로인지 본다 (TODO 아님):

<!-- code: shared_regcheck.S -->

여기를 채운다:

<!-- code: starter.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c starter.S shared_regcheck.S -o starter && ./starter
```

실제 출력 (스텁은 `mov x0, #0` / `ret` 이라 멈추거나 죽지 않는다):

```text
FAIL harness.c:99  fact_asm(0) == 1u
FAIL harness.c:100  fact_asm(5) == 120u
… (Part A 7줄 더)
FAIL harness.c:121  ok
FAIL harness.c:126  ok && g_lr[0] != g_lr[1]
… (Part B·C 17줄 더)
FAIL harness.c:179  fp_backtrace(buf, 3) == 3 && buf[3] == 0xAA && buf[0] != 0xAA
1/30 passed
```

(통과하는 1개는 `fp_backtrace(buf, 0) == 0` — 스텁이 우연히 맞는 경계 사례.)

`.S` 는 C 전처리기를 거친다 → 주석은 `//` 와 `/* */` 만. Apple Mach-O 이므로 C 의 `fact_asm` 은 asm 에서 `_fact_asm` 이다. Linux/ELF 판은 5장 끝 참고.

## 4. 힌트

<details>
<summary>힌트 1 — 프롤로그 세 줄, 에필로그 세 줄</summary>

```text
stp  x29, x30, [sp, #-32]!   // sp -= 32 먼저 (pre-index), 그 자리에 x29, x30
mov  x29, sp                 // 새 레코드 주소
str  x19, [sp, #16]          // 쓸 callee-saved 저장
...
ldr  x19, [sp, #16]
ldp  x29, x30, [sp], #32     // 읽고 나서 sp += 32 (post-index)
ret
```

`!` 와 `], #` 의 차이는 ARM-B04 에서 본 pre/post-index 그대로다.

</details>

<details>
<summary>힌트 2 — 무엇을 x19 에 두나</summary>

`bl` 뒤에도 필요한 값만. fact 는 n 하나 (`mul x0, x0, x19`), fib 는 n 과 fib(n-1) 둘. x0–x17 에 두면 피호출자가 마음대로 깬다. 프레임은 레코드 16 B + x19 8 B = 24 → **32** 로 올린다.

</details>

<details>
<summary>힌트 3 — 지역 배열은 sp 에, 해제는 mov sp, x29</summary>

`sub sp, sp, #48` 후 `a = sp`. 원소 주소는 `[sp, x9, lsl #2]`. 콜백 인자는 `mov x0, sp`. 끝에서 `mov sp, x29` 한 줄이면 배열 크기를 몰라도 레코드 위치로 돌아온다. `blr` 로 부를 함수 포인터는 x1 을 덮어쓰기 전에 다른 레지스터(x9)로 옮긴다.

</details>

<details>
<summary>힌트 4 — fp_backtrace 는 프레임이 없다</summary>

leaf 라 x29 는 아직 **호출자** 의 레코드다. `ldp x11, x12, [x10]` 한 번이면 {이전 fp, LR} 둘 다 온다. 개수를 out 에 쓸 때는 `str x12, [x0, w9, uxtw #3]`. "이전 fp ≤ 지금 fp" 를 `b.ls` 하나로 검사하면 0 도 같이 걸린다.

</details>

## 5. 풀이

### Part A — 프롤로그/에필로그 한 줄씩 (fact_asm(2) 추적)

S = 호출 순간의 sp (16 정렬). R = C 호출자로 돌아갈 주소. `+0x1c`, `+0x2c` 는 실제 링크 결과의 오프셋이다 (lldb `disassemble` 와 일치).

| # | 명령 | x0 | x19 | x29 | x30 | sp | 메모리 변화 |
|---|---|---|---|---|---|---|---|
| 0 | (fact(2) 진입) | 2 | x19_C | fp_C | R | S | |
| 1 | `stp x29, x30, [sp, #-32]!` | 2 | x19_C | fp_C | R | S-32 | [S-32]=fp_C, [S-24]=R |
| 2 | `mov x29, sp` | | | S-32 | | | |
| 3 | `str x19, [sp, #16]` | | | | | | [S-16]=x19_C |
| 4 | `mov x19, x0` | 2 | **2** | | | | |
| 5 | `cmp x19, #1` / `b.hi 1f` | | | | | | 2 > 1 → 분기 |
| 6 | `sub x0, x19, #1` | 1 | | | | | |
| 7 | `bl _fact_asm` | 1 | 2 | S-32 | **fact+0x2c** | S-32 | |
| 8 | ↳ fact(1): `stp` / `mov x29, sp` / `str x19` | | | S-64 | | S-64 | [S-64]=S-32, [S-56]=fact+0x2c, [S-48]=**2** |
| 9 | ↳ `mov x19, x0`, `cmp`, `b.hi` 안 감 | 1 | 1 | | | | |
| 10 | ↳ `bl _base_hook` (훅이 체인을 걷는다) | | | | fact+0x1c | | |
| 11 | ↳ `mov x0, #1`, `ldr x19`, `ldp x29, x30, [sp], #32`, `ret` | 1 | 2 | S-32 | fact+0x2c | S-32 | |
| 12 | `mul x0, x0, x19` | **2** | 2 | | | | |
| 13 | `ldr x19, [sp, #16]` | | x19_C | | | | |
| 14 | `ldp x29, x30, [sp], #32` | | | fp_C | R | S | |
| 15 | `ret` | 2 | x19_C | fp_C | R | S | pc = R |

관찰 세 가지:

1. 7번 `bl` 이 x30 을 `fact+0x2c` 로 덮는다. 1번에서 R 을 저장하지 않았다면 15번 `ret` 은 `fact+0x2c` 로 간다 (아래 버그 1 실측).
2. 8번의 `[S-48] = 2` — fact(1) 프레임에 저장된 x19 는 **fact(2) 의 n** 이다. callee-saved 슬롯에는 "내 값" 이 아니라 "호출자의 값" 이 들어간다. 하네스가 `g_saved19[k] == k + 2` 로 이걸 검사한다.
3. 모든 재귀 호출은 같은 `bl` 에서 나가므로 저장된 LR 은 모두 `fact+0x2c` 로 같다. 체인을 보면 "같은 함수가 재귀 중" 임이 바로 보인다.

fib 는 같은 틀에 x20 하나가 더해진다. `stp x19, x20, [sp, #16]` 로 둘을 한 번에 저장하고, 첫 `bl` 결과를 x20 에 두었다가 두 번째 결과와 더한다. 재귀 지점이 두 곳이라 체인에 나오는 LR 값은 최대 세 종류다 (두 재귀 지점 + base 의 훅 호출).

### Part B — lldb 로 체인 따라가기 (실측)

`demo_frames.c` 는 `fact_asm(4)` 를 한 번 부르고 `base_hook` 에서 멈추는 최소 프로그램이다.

```bash
cc -std=c11 -g -O0 demo_frames.c solution.S -o /tmp/ARM-B08_frames
lldb -b -s demo_frames.lldb /tmp/ARM-B08_frames      # 전체 기록: demo_frames_lldb.txt
```

```text
(lldb) bt
* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1
  * frame #0: 0x000000010000046c ARM-B08_frames`base_hook at demo_frames.c:12:34
    frame #1: 0x00000001000004f0 ARM-B08_frames`fact_asm + 28
    frame #2: 0x0000000100000500 ARM-B08_frames`fact_asm + 44
    frame #3: 0x0000000100000500 ARM-B08_frames`fact_asm + 44
    frame #4: 0x0000000100000500 ARM-B08_frames`fact_asm + 44
    frame #5: 0x00000001000004b0 ARM-B08_frames`main at demo_frames.c:16:56
    frame #6: 0x000000018753e054 dyld`start + 6652
(lldb) register read fp sp lr pc
      fp = 0x000000016fdfea00
      sp = 0x000000016fdfe9f0
      lr = 0x00000001000004f0  ARM-B08_frames`fact_asm + 28
      pc = 0x000000010000046c  ARM-B08_frames`base_hook + 12 at demo_frames.c:12:34
(lldb) memory read -fx -s8 -c4 $fp
0x16fdfea00: 0x000000016fdfea10 0x00000001000004f0
0x16fdfea10: 0x000000016fdfea30 0x0000000100000500
(lldb) up
frame #1: 0x00000001000004f0 ARM-B08_frames`fact_asm + 28
(lldb) register read fp sp lr pc
      fp = 0x000000016fdfea10
      sp = 0x000000016fdfea10
      lr = 0x00000001000004f0  ARM-B08_frames`fact_asm + 28
      pc = 0x00000001000004f0  ARM-B08_frames`fact_asm + 28
(lldb) memory read -fx -s8 -c4 $fp
0x16fdfea10: 0x000000016fdfea30 0x0000000100000500
0x16fdfea20: 0x0000000000000002 0x000000018757bb34
(lldb) up
(lldb) memory read -fx -s8 -c4 $fp
0x16fdfea30: 0x000000016fdfea50 0x0000000100000500
0x16fdfea40: 0x0000000000000003 0x0000000000000001
(lldb) up
(lldb) memory read -fx -s8 -c4 $fp
0x16fdfea50: 0x000000016fdfea70 0x0000000100000500
0x16fdfea60: 0x0000000000000004 0x00000001f240cd20
(lldb) up
(lldb) memory read -fx -s8 -c2 $fp
0x16fdfea70: 0x000000016fdfeaa0 0x00000001000004b0
```

(`up` 뒤에 lldb 가 찍는 프레임 줄·디스어셈블 4줄은 frame #1 만 남기고 줄였다. 원문은 demo_frames_lldb.txt.)

읽는 법:

- 각 `memory read` 의 첫 줄 = 레코드 {이전 fp, LR}. 이전 fp 값을 다음 `memory read` 주소로 쓰면 그게 "링크를 따라가기" 다: `ea00 → ea10 → ea30 → ea50 → ea70 → eaa0(main)`.
- 간격이 정확히 `0x20` = 32 B. base_hook → fact(1) 만 0x10 인데, base_hook (clang `-O0`) 은 32 B 프레임의 **위쪽** (`add x29, sp, #0x10`) 에 레코드를 두고 아래 16 B 에 printf 의 가변 인자를 넣기 때문이다 (`otool -tv` 로 확인, Apple 은 가변 인자를 스택으로 — ARM-B01).
- 둘째 줄의 첫 칸 = 저장된 x19: `2, 3, 4` — 위 추적표 관찰 2 그대로 (호출자의 n). 둘째 칸은 패딩이라 쓰레기값 (`0x18757bb34` 등) — 아무도 안 쓴 칸이다.
- frame #1 에서 `lr = fact_asm+28` 인 것은 맞는 값이다: fact(1) 은 `bl _base_hook` 를 실행한 상태로 멈춰 있으므로 그 순간의 x30 은 `+28`. fact(1) **자신의** 반환 주소는 메모리 `[fp+8] = +44` 에 있다. "레지스터 LR" 과 "저장된 LR" 을 구분하자.
- 위 그림의 화살표가 바로 이 숫자들이다:

```svg
<svg viewBox="0 0 760 470" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">fact_asm(4) → base_hook 에서 멈춘 스택 (lldb 실측 주소, 위가 높은 주소)</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="10" y="54">0x16fdfeaa0</text><text x="10" y="84">0x16fdfea90</text><text x="10" y="114">0x16fdfea80</text>
    <text x="10" y="144">0x16fdfea70</text><text x="10" y="174">0x16fdfea60</text><text x="10" y="204">0x16fdfea50</text>
    <text x="10" y="234">0x16fdfea40</text><text x="10" y="264">0x16fdfea30</text><text x="10" y="294">0x16fdfea20</text>
    <text x="10" y="324">0x16fdfea10</text><text x="10" y="354">0x16fdfea00</text><text x="10" y="384">0x16fdfe9f0</text>
  </g>
  <g stroke="currentColor">
    <rect x="150" y="35" width="300" height="30" class="f-mut"/>
    <rect x="150" y="65" width="300" height="30" class="f-mut"/>
    <rect x="150" y="95" width="150" height="30" class="f-ok"/><rect x="300" y="95" width="150" height="30" class="f-mut"/>
    <rect x="150" y="125" width="150" height="30" class="f-acc"/><rect x="300" y="125" width="150" height="30" class="f-acc"/>
    <rect x="150" y="155" width="150" height="30" class="f-ok"/><rect x="300" y="155" width="150" height="30" class="f-mut"/>
    <rect x="150" y="185" width="150" height="30" class="f-acc"/><rect x="300" y="185" width="150" height="30" class="f-acc"/>
    <rect x="150" y="215" width="150" height="30" class="f-ok"/><rect x="300" y="215" width="150" height="30" class="f-mut"/>
    <rect x="150" y="245" width="150" height="30" class="f-acc"/><rect x="300" y="245" width="150" height="30" class="f-acc"/>
    <rect x="150" y="275" width="150" height="30" class="f-ok"/><rect x="300" y="275" width="150" height="30" class="f-mut"/>
    <rect x="150" y="305" width="150" height="30" class="f-acc"/><rect x="300" y="305" width="150" height="30" class="f-acc"/>
    <rect x="150" y="335" width="150" height="30" class="f-acc"/><rect x="300" y="335" width="150" height="30" class="f-acc"/>
    <rect x="150" y="365" width="300" height="30" class="f-mut"/>
  </g>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="158" y="54">main 의 레코드 (→ dyld start)</text>
    <text x="158" y="84">main 지역</text>
    <text x="158" y="114">x19 (main 값)</text><text x="308" y="114">패딩</text>
    <text x="158" y="144">fp=…eaa0</text><text x="308" y="144">lr=main</text>
    <text x="158" y="174">x19 = 4</text><text x="308" y="174">패딩</text>
    <text x="158" y="204">fp=…ea70</text><text x="308" y="204">lr=fact+0x2c</text>
    <text x="158" y="234">x19 = 3</text><text x="308" y="234">패딩</text>
    <text x="158" y="264">fp=…ea50</text><text x="308" y="264">lr=fact+0x2c</text>
    <text x="158" y="294">x19 = 2</text><text x="308" y="294">패딩</text>
    <text x="158" y="324">fp=…ea30</text><text x="308" y="324">lr=fact+0x2c</text>
    <text x="158" y="354">fp=…ea10</text><text x="308" y="354">lr=fact+0x1c</text>
    <text x="158" y="384">base_hook 지역 (printf 인자)</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M150 350 C 128 345, 128 330, 150 322"/><path d="M142 318 L150 322 L143 328"/>
    <path d="M150 320 C 118 305, 118 270, 150 262"/><path d="M142 258 L150 262 L143 268"/>
    <path d="M150 260 C 118 245, 118 210, 150 202"/><path d="M142 198 L150 202 L143 208"/>
    <path d="M150 200 C 118 185, 118 150, 150 142"/><path d="M142 138 L150 142 L143 148"/>
    <path d="M150 140 C 112 120, 112 62, 150 52"/><path d="M142 48 L150 52 L143 58"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="465" y="130">fact(4) 프레임 32 B (ea70–ea8f)</text>
    <text x="465" y="190">fact(3) 프레임 32 B</text>
    <text x="465" y="250">fact(2) 프레임 32 B</text>
    <text x="465" y="310">fact(1) 프레임 32 B ← frame #1 fp</text>
    <text x="465" y="354">← x29 = 0x16fdfea00 (base_hook 레코드)</text>
    <text x="465" y="384">← sp = 0x16fdfe9f0</text>
    <text x="465" y="114">[fp+16] = 호출자의 x19</text>
  </g>
  <text x="10" y="420" fill="currentColor">화살표 = [fp] 의 "이전 fp". 간격 0x20 = 프레임 32 B. 저장된 x19 = 2, 3, 4 = 호출자의 n.</text>
  <text x="10" y="444" fill="currentColor">저장된 LR: base 지점 fact+0x1c 한 번, 재귀 지점 fact+0x2c 세 번 — 하네스의 Part B 검사가 이걸 본다.</text>
</svg>
```

### Part B — 하네스는 무엇을 보나

| 검사 | 무엇을 잡나 |
|---|---|
| 깊이 == max(n,1), 범위 안 LR 개수 == 깊이 | LR 을 저장 안 함, 체인이 끊김 (x29 를 안 세움), 꼬리 호출로 프레임이 사라짐 |
| 재귀 LR 은 모두 같고 base LR 은 다름 | 레코드 칸을 바꿔 저장 (`stp x30, x29`) |
| 레코드 16 정렬 + 간격 32 | 프레임 크기 계산 실수, `stp ..., [sp, #-16]!` 후 `str x19, [sp, #16]` (호출자 레코드를 덮어씀) |
| `[fp+16] == k+2` | callee-saved 를 다른 칸에 저장 |
| `call_preserve_check == 0` | x19–x28 을 쓰고 저장 안 함, sp 를 안 되돌림 |
| `INT32_MIN` / `INT32_MAX` seed | 합을 w 레지스터로 함 (`ldr w` + `add w`), `ldrsw` 대신 `ldr w` (부호 확장 빠짐) |
| 깊이 10000 | 스택 사용이 n 에 비례하는지 (320 KB, 기본 8 MB 메인 스레드 스택 안) |

`INT32_MAX` seed 의 기대값 `-17179869149` 는 Python 으로 32비트 wrap 을 계산해 손으로 적었다. C 로 `seed + i` 를 `int32_t` 에서 하면 부호 있는 오버플로 = **UB** 라 테스트에서 계산하지 않는다. ISA 의 `add w10, w0, w9` 는 그냥 mod 2^32 로 정의되어 있다.

`call_preserve_check` 가 실제로 잡는지는 demo_bugs 로 확인했다 (아래). 반환값이 맞아도 x20 을 몰래 덮은 함수는 마스크 `0x2` 가 나온다.

### Part B — 틀린 프레임은 실제로 이렇게 죽는다 (`demo_bugs.S`)

```bash
cc -std=c11 -O2 demo_bugs.c demo_bugs.S shared_regcheck.S -o /tmp/ARM-B08_bugs && /tmp/ARM-B08_bugs
```

각 버그를 fork 한 자식에서 돌리고, SIGSEGV/SIGBUS 핸들러(대체 스택 `sigaltstack` — sp 가 망가졌으니)가 `ucontext` 의 레지스터를 찍는다. 실제 출력 (`demo_bugs_out.txt`, 주소는 ASLR 로 매번 바뀜):

```text
  nolr(1) = 1 (base case: bl 없음 → 정상)
  [crash] SIGSEGV addr=0x16d03c000 pc=0x102dc4854 sp=0x16d03c000 lr=0x102dc4850 fp=0x16d03ac40 esr=0x92000007
bug1 x30 저장 안 함            → exit 139 (핸들러가 잡은 크래시)
  noxsave(5) = 1 (기대 120), 보존 마스크 = 0x1
bug2 x19 저장 안 함            → exit 0
  [crash] SIGBUS addr=0x300000002 pc=0x300000002 sp=0x16d03aba0 lr=0x300000002 fp=0x100000000 esr=0x8a000000
bug3 지역 영역 해제 안 함  → exit 138 (핸들러가 잡은 크래시)
  [crash] SIGBUS addr=0x0 pc=0x102dc48c4 sp=0x16d03abc8 lr=0x102dc46e0 fp=0x16d03ac40 esr=0x9a000000
bug4 sp 8 바이트 정렬         → exit 138 (핸들러가 잡은 크래시)
  sum(10) = 45 (기대 45), 보존 마스크 = 0x2
bug5 x20 몰래 덮어씀          → exit 0
```

ESR 의 위 6비트 EC(Exception Class)를 풀면 (Arm ARM 의 ESR_ELx 표 기준, 값은 아래 계산과 일치):

| 버그 | 실측 | EC | 해석 |
|---|---|---|---|
| 1. x30 저장 안 함 | pc = `ldr x19, [sp]`, lr = `mul` 줄, sp = 페이지 경계 = fault 주소 | 0x24 Data Abort (하위 EL), DFSC 0x07 = 레벨 3 변환 오류 | `ret` 이 계속 `mul` 줄로 "돌아가며" 매번 `add sp, sp, #16` → sp 가 스택 꼭대기 너머 미매핑 페이지까지 올라감. **n ≤ 1 은 정상** (base 에는 bl 이 없다) — 작은 입력 테스트만으론 못 잡는다 |
| 2. x19 저장 안 함 | 결과 1, 마스크 0x1 | — | 자식이 x19 를 1 로 바꿔 놓아 `mul` 이 매번 ×1 |
| 3. 지역 영역 해제 안 함 | pc = lr = `0x300000002`, fp = `0x100000000` | 0x22 PC alignment fault | `ldp x29, x30` 이 배열을 읽었다: x29 = a[1]:a[0] = 1:0, x30 = a[3]:a[2] = 3:2. 덤프에서 "pc 가 작은 정수 쌍" 이면 스택 불균형을 의심 |
| 4. sp 8 정렬 | pc = `str x0, [sp]` (`sub sp` 는 통과) | 0x26 SP alignment fault | SCTLR_EL1.SA0 이 켜져 있어 EL0 에서 sp 기준 접근 시 검사 — macOS 에서 실제로 켜져 있음을 확인 |
| 5. x20 몰래 덮어씀 | 반환값 정답, 마스크 0x2 | — | 호출자가 x20 에 뭘 두었느냐에 따라 **나중에, 다른 곳에서** 터진다 — 가장 찾기 어려운 종류 |

```text
0x92000007 EC= 0x24 IL= 1 ISS= 0x7      (python3 로 계산: e >> 26, (e >> 25) & 1, e & 0x1ffffff)
0x8a000000 EC= 0x22 IL= 1 ISS= 0x0
0x9a000000 EC= 0x26 IL= 1 ISS= 0x0
```

### Part C — 지역 배열 프레임

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">local_array_sum 의 프레임: int32_t a[10] = 40 B → 48 B</text>
  <g stroke="currentColor">
    <rect x="150" y="40" width="160" height="30" class="f-acc"/><rect x="310" y="40" width="160" height="30" class="f-acc"/>
    <rect x="150" y="70" width="80" height="30" class="f-ok"/><rect x="230" y="70" width="80" height="30" class="f-ok"/><rect x="310" y="70" width="160" height="30" class="f-warn"/>
    <rect x="150" y="100" width="80" height="30" class="f-ok"/><rect x="230" y="100" width="80" height="30" class="f-ok"/><rect x="310" y="100" width="80" height="30" class="f-ok"/><rect x="390" y="100" width="80" height="30" class="f-ok"/>
    <rect x="150" y="130" width="80" height="30" class="f-ok"/><rect x="230" y="130" width="80" height="30" class="f-ok"/><rect x="310" y="130" width="80" height="30" class="f-ok"/><rect x="390" y="130" width="80" height="30" class="f-ok"/>
  </g>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="20" y="60">x29 + 0</text><text x="20" y="90">x29 - 16</text><text x="20" y="120">x29 - 32</text><text x="20" y="150">x29 - 48 = sp</text>
    <text x="158" y="60">이전 fp</text><text x="318" y="60">LR</text>
    <text x="160" y="90">a[8]</text><text x="240" y="90">a[9]</text><text x="318" y="90">패딩 8 B</text>
    <text x="160" y="120">a[4]</text><text x="240" y="120">a[5]</text><text x="320" y="120">a[6]</text><text x="400" y="120">a[7]</text>
    <text x="160" y="150">a[0]</text><text x="240" y="150">a[1]</text><text x="320" y="150">a[2]</text><text x="400" y="150">a[3]</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="490" y="60">stp x29, x30, [sp, #-16]!</text>
    <text x="490" y="90">40 B 를 16 의 배수로 → 48 B</text>
    <text x="490" y="150">sub sp, sp, #48 → a = sp</text>
    <text x="490" y="170">cb(a, 10): 콜백이 a 를 바꿔도 된다</text>
    <text x="10" y="200">콜백 안에서 [자기 fp] = local_array_sum 의 x29 → 하네스가 "x29 − a == 48" 과 "a % 16 == 0" 을 검사.</text>
    <text x="10" y="222">끝: mov sp, x29 (배열 해제) → ldp x29, x30, [sp], #16 → ret. 해제를 빼먹으면 버그 3 (pc = 0x300000002).</text>
    <text x="10" y="244">합은 ldrsw (부호 확장 64비트 load) + add x — 이 레이아웃은 직접 정한 것, clang 은 다르게 배치할 수 있다.</text>
  </g>
</svg>
```

콜백 호출 뒤 a[] 를 **메모리에서 다시 읽는** 것이 요점이다. `cb_patch` 가 `a[3] += 1000` 을 하면 합이 1045 → 2045 가 되어야 한다. 콜백 전 레지스터에 합을 미리 계산해 두는 "최적화" 는 틀린 답이다 — 포인터를 넘긴 순간 그 메모리는 바뀔 수 있다 (C 컴파일러가 `escape` 된 지역 변수를 다시 load 하는 이유와 같다).

### Part C — fp_backtrace 와 크래시 로그

하네스는 `fact_asm(4)` 의 base 훅 안에서 `fp_backtrace` 를 부르고, 같은 훅이 C 로 걸은 체인과 `out[1..]` 를 `memcmp` 한다. `out[0]` 은 asm 자신의 반환 주소 (훅 안) 이다. 실제 출력 (solution):

```text
  #0 base_hook+0x38
  #1 fact_asm+0x1c
  #2 fact_asm+0x2c
  #3 fact_asm+0x2c
  #4 fact_asm+0x2c
  #5 main+0x1514
  #6 start+0x19fc
  ... (8 frames)
30/30 passed
```

멈춤 조건이 왜 4개인가: 크래시 핸들러는 **깨진 스택** 위에서 돈다. fp 가 0 이면 정상 종료, 정렬이 안 맞거나 아래로 가면 스택이 덮인 것 — 그대로 따라가면 핸들러가 두 번째 fault 를 낸다. 실제 펌웨어(커널의 `unwind_frame`, RTOS 의 fault 로거)는 여기에 "fp 가 현재 스택 범위 안인가" 검사를 더한다 (연습 4 의 한계 검사와 같은 생각).

`dladdr` 는 실행 파일의 심볼표로 이름을 찾는다. 맥에서는 그대로 됐고, Lima VM(Linux)에서는 `-rdynamic` 이 필요했고, asm 함수에 `.size` 가 없으면 `#1 ?` 로 나왔다 (실측, 아래).

### 컴파일러는 같은 재귀를 어떻게 내나 (`clang -S`, 실측)

```c
uint64_t fact_c(uint64_t n) { if (n <= 1) return 1; return n * fact_c(n - 1); }
uint64_t fib_c(uint64_t n)  { if (n < 2) return n; return fib_c(n - 1) + fib_c(n - 2); }
```

- `-O0` fact_c: `sub sp, sp, #48` / `stp x29, x30, [sp, #32]` / `add x29, sp, #32` — 레코드가 프레임 **위쪽** 에 있고, n 은 callee-saved 대신 `[sp, #16]` 에 spill 한다.
- `-O2` fact_c: **프레임이 없다.** 재귀를 루프로 바꾸고 4갈래로 펼친 `mul` 루프 + 나머지 루프 (C 의 곱셈이 결합법칙을 만족하는 unsigned 라 가능). base_hook 을 넣은 판도 루프 뒤에서 훅을 **한 번** 부르고 끝 — C 로 짰다면 깊이 검사가 1 이 나왔을 것이다. 이 레슨을 asm 으로 하는 이유다.
- `-O2` fib_c: 재귀 하나만 남기고 다른 하나는 루프로:

```text
_fib_c:
	stp	x20, x19, [sp, #-32]!           ; 16-byte Folded Spill
	stp	x29, x30, [sp, #16]             ; 16-byte Folded Spill
	add	x29, sp, #16
	mov	x19, x0
	...                                     ; n < 2 경로 생략
LBB2_3:                                 ; =>This Inner Loop Header: Depth=1
	sub	x0, x19, #1
	bl	_fib_c                          ; fib(n-1) 만 재귀
	sub	x19, x19, #2                    ; fib(n-2) 는 루프로: n -= 2
	add	x20, x0, x20
	cmp	x19, #1
	b.hi	LBB2_3
LBB2_4:
	add	x0, x19, x20
	ldp	x29, x30, [sp, #16]             ; 16-byte Folded Reload
	ldp	x20, x19, [sp], #32             ; 16-byte Folded Reload
	ret
```

clang 은 callee-saved 를 프레임 바닥에, 레코드를 그 위(`x29 = sp + 16`)에 둔다. 내 fact_asm 은 반대 (레코드가 바닥, x19 가 `[fp+16]`). **둘 다 맞다** — ABI 는 "x29 가 유효한 레코드를 가리킨다" 만 요구하고 프레임 안 배치는 자유다. 그래서 하네스의 `[fp+16] == 호출자의 n` 검사는 *내 레이아웃* 에 대한 검사이지 ABI 규칙이 아니다.

### Linux/ELF 판 (Lima VM 에서 빌드·실행 확인)

바뀌는 것은 심볼 이름과 메타데이터뿐이다:

```text
    .globl  fact_asm                 // '_' 없음
    .type   fact_asm, %function      // 심볼 종류 (gdb, dladdr, perf 가 씀)
fact_asm:
    ...                              // 본문 동일 (bl fact_asm, bl base_hook)
    ret
    .size   fact_asm, . - fact_asm   // 함수 크기
```

VM (aarch64 Ubuntu, gcc 15.2.0 / clang) 에서 `_` 를 지우고 `.type` 를 넣은 사본으로 `gcc -O2 -rdynamic ... -ldl` → **30/30 passed** (gcc, clang 둘 다). `.size` 가 없을 때 백트레이스는 `#1 ?` 였고, `fact_asm` 에 `.size` 를 넣자 `#1 fact_asm+0x1c` 로 바뀌었다 (`readelf -s` 의 Size 칸: 0 → 60). glibc 의 `dladdr` 는 크기 0 인 심볼 안의 주소를 그 심볼로 보지 않는다.

## 6. 정답 코드

<!-- code: solution.S diff=starter.S -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_regcheck.S -o solution && ./solution    # 30/30, 0.12 s
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   harness.c solution.S shared_regcheck.S -o sol_asan && ./sol_asan                                    # 30/30, 보고 없음
```

ASan 빌드의 백트레이스는 `#0 base_hook+0x4c`, `#5 main+0x10f0` 로 C 쪽 오프셋만 바뀌고 asm 쪽 `fact_asm+0x1c / +0x2c` 는 같다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| non-leaf 인데 x30 저장 안 함 | `bl` 이 LR 을 덮음 → `ret` 이 자기 몸으로 돌아가 sp 가 계속 올라감 (버그 1: SIGSEGV). base case 만 테스트하면 통과 | `stp x29, x30, [sp, #-32]!` … `ldp x29, x30, [sp], #32` |
| `stp x29, x30, [sp, #-16]!` 후 `str x19, [sp, #16]` | 내 프레임은 16 B 인데 `[sp+16]` = **호출자의 레코드** 를 덮음 → 호출자의 ret 에서 터짐 | 프레임을 32 B 로: `[sp, #-32]!` |
| x19 를 쓰고 저장 안 함 | 재귀 자식이 같은 x19 를 덮음 → 결과가 1 (버그 2). 재귀가 없어도 호출자가 깨진다 (버그 5) | `str x19, [sp, #16]` / `ldr x19, [sp, #16]` |
| `bl` 너머로 필요한 값을 x9–x15 에 둠 | caller-saved: 피호출자(여기선 C 훅)가 마음대로 쓴다. 테스트에서 우연히 통과할 수 있음 | x19–x28 에 두고 저장 |
| `sub sp, sp, #40` (배열 크기 그대로) | sp % 16 == 8 → sp 기준 접근에서 SP alignment fault (버그 4: SIGBUS, EC 0x26) | 16 의 배수로 올림: `#48` |
| 에필로그에서 `add sp, sp, #48` 누락 | `ldp x29, x30` 이 배열 내용을 읽음 → pc = 배열 값 (버그 3) | `mov sp, x29` 를 `ldp` 앞에 |
| `blr x1` 을 하려는데 그 전에 `mov x1, #10` | 함수 포인터를 인자로 덮어씀 | `mov x9, x1` 먼저, 그 다음 인자 세팅, `blr x9` |
| 콜백 뒤 a[] 를 다시 안 읽음 | 포인터를 넘긴 메모리는 바뀔 수 있다 (cb_patch 테스트 실패) | 콜백 **뒤에** 합 계산 |
| 합을 `ldr w` + `add w` 로 | 32비트로 잘리거나 부호 확장 누락 → `INT32_MIN` 테스트 실패 | `ldrsw x10, [...]` + `add x0, x0, x10` |
| fp_backtrace 에서 x29 에 바로 `ldp x29, x30, [x29]` | x29/x30 을 덮으면 leaf 가 돌아갈 곳을 잃음 | 임시 레지스터(x10–x12) 로 걷기 |
| 백트레이스에 멈춤 조건이 fp == 0 뿐 | 깨진 스택에서 쓰레기 주소를 따라가 핸들러가 두 번째 fault | 정렬 + 단조 증가 + (펌웨어라면) 스택 범위 검사 |
| x18 을 임시로 사용 | Apple 은 플랫폼 예약 (OS 가 쓰거나 0 으로 만들 수 있음) — 문서 기준 | x9–x15 사용 |

## 8. 면접 Q&A

<details>
<summary>Q1. 왜 non-leaf 함수는 LR 을 저장해야 하나요? leaf 는요?</summary>

`bl` 은 "다음 명령 주소를 x30 에 넣고 점프" 이다. 내가 다른 함수를 `bl` 하면 내 반환 주소가 지워진다. 그래서 첫 `bl` 전에 x30 을 스택에 저장하고 `ret` 전에 되돌린다. leaf 는 x30 을 건드리지 않으니 저장할 필요가 없다 (Apple 문서도 leaf 는 레코드를 생략할 수 있다고 함 — 이 경우 백트레이스에서 그 함수는 lr 레지스터로만 보인다). 실측: LR 을 저장 안 한 fact 는 `n ≤ 1` 에선 맞고, `n = 5` 에서 sp 가 스택 꼭대기를 넘어 SIGSEGV 로 죽었다.

</details>

<details>
<summary>Q2. 왜 sp 는 16 바이트 정렬이어야 하나요? 8 이면 안 되나요?</summary>

AAPCS64 가 `sp mod 16 == 0` 을 요구하고(규약), 하드웨어는 SCTLR_ELx.SA/SA0 비트가 켜져 있으면 sp 를 base 로 쓰는 load/store 에서 정렬을 검사해 SP alignment fault (EC 0x26) 를 낸다. 이 맥에서 `sub sp, sp, #8; str x0, [sp]` 가 실제로 SIGBUS 였다 (`sub` 자체는 통과, 접근에서 fault). 16 인 이유: `stp` 두 개 = 16 B 레코드, 128비트 q 레지스터 저장이 자연 정렬, 규칙이 단순해 모든 함수가 정렬을 믿을 수 있다. (AAPCS32 는 4 B 상시 + 공용 인터페이스에서 8 B 였다. 정확한 조항 문구는 원문 확인 권장.)

</details>

<details>
<summary>Q3. caller-saved 와 callee-saved 는 누가 비용을 내나요? 왜 둘 다 있나요?</summary>

x0–x17 은 호출자가 비용을 낸다: 호출 뒤에도 필요하면 호출자가 저장하거나 callee-saved 로 옮긴다. x19–x28 은 피호출자가 낸다: 쓸 때만 프롤로그에서 저장한다. 둘로 나누면 짧은 leaf 함수는 x0–x17 만으로 저장 없이 끝나고, 호출을 넘어 살아야 하는 값(fact 의 n, fib 의 n 과 fib(n-1))은 callee-saved 에 두어 **함수당 한 번만** 저장한다. 루프 안에서 호출할 때 차이가 크다 — 매 호출마다 spill/reload 하는 대신 프롤로그에서 한 번. 실측: clang `-O0` 은 n 을 매번 `[sp, #16]` 에 spill, `-O2` 는 x19/x20 에 두었다. (FP 쪽은 d8–d15 의 하위 64비트만 callee-saved — ARM-B11.)

</details>

<details>
<summary>Q4. 크래시 덤프에 pc, lr, sp, fp 만 있습니다. 호출 체인을 어떻게 복원하나요?</summary>

현재 함수 = pc. 호출자 = lr (현재 함수가 leaf 이거나 아직 LR 을 저장하기 전이면 이게 유일한 단서). 그 다음은 fp 체인: `[fp+8]` 이 한 단계 위 반환 주소, `[fp]` 가 다음 레코드. fp 가 0 이거나 정렬이 깨지거나 아래로 가면 멈춘다. 주소 → 심볼은 맵 파일/ELF 심볼표로 (`addr2line`, `atos`). 함정: 크래시가 프롤로그 도중이면 레코드가 아직 없다 (lr 이 진짜 호출자, `[fp]` 는 호출자의 레코드). `-fomit-frame-pointer` 로 빌드된 코드는 체인이 없다 → DWARF CFI(.eh_frame) 로 풀어야 한다. 이 레슨의 버그 3 덤프처럼 pc 가 `0x300000002` 같은 "작은 정수 쌍" 이면 스택 불균형이나 버퍼 오버플로로 저장된 LR 이 덮였다는 신호다.

</details>

<details>
<summary>Q5. `ret` 은 실제로 무엇을 하나요? `br x30` 과 같은가요?</summary>

`ret` 은 `ret x30` 의 줄임이고 동작은 "pc = x30" 으로 `br x30` 과 같다. 차이는 **힌트**: `ret` 은 "함수 반환" 이라고 알려 분기 예측기의 return address stack 을 쓰게 하고, `bl`/`blr` 은 그 스택에 push 한다. `br x30` 으로 돌아가면 결과는 같지만 예측이 틀릴 수 있다 (마이크로아키텍처마다 다름 — 일반론). `ret x9` 처럼 다른 레지스터로도 반환할 수 있다. 꼬리 호출은 `bl` 대신 `b` 라서 return stack 에 넣지 않는다.

</details>

<details>
<summary>Q6. 꼬리 호출(tail call) 최적화가 백트레이스에 어떤 영향을 주나요?</summary>

`return f(x);` 를 `b f` 로 바꾸면 내 프레임을 먼저 걷어내고 점프하므로 f 의 LR 은 **내 호출자** 를 가리킨다 → 백트레이스에서 나는 사라진다. 연습 1 의 `fact_tail` 은 n = 10000 에서도 base 훅이 본 asm 프레임이 1 개였다. clang `-O2` 는 재귀 `fact_c` 를 아예 루프로 바꿔 프레임 0 개 (실측). 디버깅용으로 `-fno-optimize-sibling-calls` 나 `-O0` 을 쓰는 이유. 펌웨어에서 "이 함수가 왜 백트레이스에 없지?" 는 대개 꼬리 호출이나 인라이닝이다.

</details>

<details>
<summary>Q7. Apple 은 왜 x18 을 예약하고, 프레임 포인터를 항상 쓰나요?</summary>

Apple 문서: x18 은 플랫폼 예약이라 앱 코드가 쓰면 안 된다 (OS 가 문맥 전환 때 값을 보존하지 않을 수 있음). Windows ARM64 에서는 TEB 포인터, 다른 OS 에서는 shadow call stack 등에 쓰인다. 프레임 포인터: Apple 은 "x29 는 항상 유효한 프레임 레코드를 가리켜야 한다" 고 규정 — 디버그 정보 없이도 샘플링 프로파일러(Instruments)와 크래시 리포터가 빠르고 정확하게 스택을 걷게 하려는 것. 대가는 레지스터 하나와 함수당 명령 몇 개. Linux 배포판도 프로파일링 때문에 `-fno-omit-frame-pointer` 로 돌아가는 추세다 (확인 권장). arm64e 에서는 저장되는 LR 에 포인터 인증 서명(`pacibsp`)이 붙는다 — ARM-A10.

</details>

<details>
<summary>Q8. 지역 배열 크기가 런타임 값이면(VLA, alloca) 에필로그는 어떻게 되나요?</summary>

`sub sp, sp, xN` 은 (extended-register 형태라) sp 를 쓸 수 있고, N 은 `(8n + 15) & ~15` 로 올린다. 해제할 때 크기를 다시 계산할 필요 없이 `mov sp, x29` — 레코드 주소는 고정이니까. 이게 프레임 포인터가 꼭 필요한 경우다: sp 가 움직이는 함수에서는 sp 기준 오프셋이 고정되지 않으므로 고정 지역 변수와 저장 레지스터를 x29 기준으로 접근한다. 연습 2 가 이 패턴이다. 펌웨어에서는 VLA 를 보통 금지한다 (MISRA C:2012 Rule 18.8) — 스택 최대 사용량을 정적으로 계산할 수 없게 되므로.

</details>

## 9. 추가 연습

### 연습 1 — 꼬리 호출 factorial: `fact_tail(n, acc)`

`acc *= n; n -= 1; b _fact_tail` 로 재귀를 점프로 바꾸고, base 에서만 프레임을 만들어 `base_hook` 을 부른다. 훅이 본 asm 프레임 수는?

<details>
<summary>답 보기</summary>

재귀 경로에는 `stp` 도 `bl` 도 없으므로 LR 은 처음 C 가 넣은 값 그대로, sp 도 그대로다. base 경로만 non-leaf (훅 호출) 라 프레임을 만들고 `acc` 를 x19 에 둔다. 훅이 센 깊이는 n = 0, 1, 5, 20, 10000 모두 **1**. 스택 사용량이 n 에 무관하다 = 루프와 같다. 대가: 백트레이스에 중간 단계가 없다. 테스트: 결과 == C 루프, 깊이 == 1, `call_preserve_check` 마스크 0.

</details>

### 연습 2 — 런타임 크기 지역 배열: `vla_sum(n, cb)`

`uint64_t a[n]` 을 스택에 (`a[i] = i*i`), cb 에 넘기고 합한다. 크기 = round_up(8n, 16).

<details>
<summary>답 보기</summary>

```text
lsl  x9, x0, #3          // 8n
add  x9, x9, #15
and  x9, x9, #0xfffffffffffffff0
sub  sp, sp, x9          // 동적 할당
mov  x20, sp             // a — blr 너머로 필요하니 callee-saved
...
mov  sp, x29             // 해제: 크기 몰라도 됨
```

`and` 의 마스크 `0xFFFF_FFFF_FFFF_FFF0` 는 논리 즉시값으로 인코딩된다 (연속된 1 의 회전 — ARM-B03). 테스트: n = 0..40 에서 합 == (n−1)n(2n−1)/6 (+ 콜백이 더한 1000000), 콜백이 받은 a 가 16 정렬이고 `x29 − a == round16(8n)` (n = 0 이면 0), 보존 마스크 0.

</details>

### 연습 3 — 이진 트리 합: `tree_sum(const struct node *)`

`struct node { int64_t val; const struct node *left, *right; }` — 오프셋 0, 8, 16. NULL 은 프레임 없이 0 을 돌려준다.

<details>
<summary>답 보기</summary>

`cbz x0, 9f` 로 NULL 을 프레임 만들기 **전에** 처리한다 (shrink-wrapping: 필요한 경로에서만 프롤로그). 그 다음 `x19 = t`, `ldr x0, [x19, #8]` → `bl` → `x20 = 왼쪽 합`, `ldr x0, [x19, #16]` → `bl` → `add`, `ldr x9, [x19]` 더하기. x19, x20 둘 다 두 번째 호출을 넘어 살아야 한다. 펌웨어 예: 디바이스 트리·설정 트리를 재귀로 걷는 부트 코드. 테스트: 고정 시드 xorshift 로 만든 64 노드 무작위 트리의 모든 부분 트리 합 == C 재귀, NULL == 0, 보존 마스크 0.

</details>

### 연습 4 — 스택 한계 가드: `fact_guarded(n, limit)`

프레임을 만들기 전에 `sp − 32 < limit` 이면 `UINT64_MAX` 를 돌려주고, 하위 호출의 `UINT64_MAX` 는 그대로 전파한다. Cortex-M33 의 `MSPLIM/PSPLIM` (ARMv8-M) 이나 RTOS 의 스택 워터마크를 소프트웨어로 흉내낸 것이다.

<details>
<summary>답 보기</summary>

```text
mov  x9, sp
sub  x9, x9, #32
cmp  x9, x1
b.lo 8f                  // 부호 없는 비교 (주소)
...
bl   _fact_guarded
cmn  x0, #1              // x0 == UINT64_MAX ?
b.eq 2f                  // 전파
```

경계 테스트: C 에서 `S = get_sp()` (leaf asm 이 호출 순간 sp 를 돌려줌 — 같은 C 프레임에서 부르므로 fact_guarded 진입 sp 와 같다), `limit = S − 32·5` 면 프레임 5 개까지 허용 → `fact_guarded(5) == 120`, `fact_guarded(6) == UINT64_MAX`, `limit = S − 32·6` 이면 `720`. n! mod 2^64 는 n ≥ 2 에서 짝수라 `UINT64_MAX` 와 겹치지 않는다. 주소 비교라 `b.lo` (부호 없음) — `b.lt` 면 주소 상위 비트에 따라 틀린다 (ARM-B05).

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_regcheck.S -o practice && ./practice   # 14/14 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   practice.c practice.S shared_regcheck.S -o pr_asan && ./pr_asan                                     # 14/14, 보고 없음
```

## 10. 핵심 정리

- 프롤로그 `stp x29, x30, [sp, #-N]!` → `mov x29, sp` → (callee-saved 저장, `sub sp` 지역). 에필로그는 정확히 역순, `ret` = pc ← x30.
- 프레임 레코드 = {이전 fp, LR} 16 B, x29 가 가리킨다. `[fp]` 를 따라가면 백트레이스, 이전 fp == 0 이 끝. 레코드가 프레임 안 어디에 있는지는 자유 (clang 은 위쪽, 이 레슨은 바닥).
- sp 는 언제나 16 의 배수 — 프레임/지역 크기를 올린다. 어기면 macOS 에서 실제 SIGBUS (SP alignment fault, EC 0x26).
- `bl` 을 하나라도 하면 x30 저장. 호출 너머로 필요한 값은 x19–x28 에 두고 **쓴 것만** 저장. callee-saved 슬롯에는 호출자의 값이 들어간다.
- 반환값만으로는 부족: 깊이, 저장된 LR 범위, 프레임 간격, callee-saved 보존을 따로 검사해야 프레임 버그가 잡힌다 (x20 을 몰래 덮은 함수도 정답을 냈다).
- 덤프 읽기: pc 가 `ldr ... [sp]` 이고 sp 가 페이지 경계 → 스택 폭주. pc 가 작은 정수 쌍 → 저장된 LR 이 덮임. EC 0x22 = PC 정렬, 0x24 = 데이터 abort, 0x26 = SP 정렬.
- 꼬리 호출(`b`)과 컴파일러의 재귀→루프 변환은 프레임을 없앤다 → 백트레이스에서 함수가 빠진다.

## 참고자료

- Arm, *Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)* — "General-purpose Registers", "The Stack" (universal stack constraints, SP mod 16), "The Frame Pointer" (frame record, chain end = 0): <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
- Apple, *Writing ARM64 code for Apple platforms* — x18 예약, 프레임 포인터 항상 유지, red zone, 호출 규약 차이: <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
- Arm Architecture Reference Manual for A-profile (DDI 0487) — STP/LDP pre/post-index, BL/BLR/RET (return hint), SCTLR_EL1.SA/SA0, ESR_ELx EC 표 (0x22 PC alignment, 0x24 Data Abort lower EL, 0x26 SP alignment): <https://developer.arm.com/documentation/ddi0487/latest/>
- LLVM `lldb` 명령: `bt`, `up`, `register read`, `memory read`, `disassemble` — <https://lldb.llvm.org/use/map.html>
- Linux kernel `arch/arm64/kernel/stacktrace.c` (FP 체인 unwinder 의 검사 조건): <https://elixir.bootlin.com/linux/latest/source/arch/arm64/kernel/stacktrace.c>
- 레슨 파일: `demo_frames.c` + `demo_frames.lldb` (+ 기록 `demo_frames_lldb.txt`), `demo_bugs.S` + `demo_bugs.c` (+ 출력 `demo_bugs_out.txt`)
- 관련 레슨: ARM-B01 (첫 .S, leaf), ARM-B03 (논리 즉시값), ARM-B04 (pre/post-index, LDP/STP), ARM-B05 (부호/무부호 조건), ARM-B09 (lldb 단일 스텝 심화), ARM-B11 (d8–d15), ARM-M01 (AAPCS64 심화: 많은 인자·구조체 반환), ARM-M09 (Cortex-M 예외 스택 프레임), ARM-M12 / ARM-M14 (크래시 덤프 분석), ARM-A10 (PAC: 서명된 LR), TOOL-A03 (gdb remote)
