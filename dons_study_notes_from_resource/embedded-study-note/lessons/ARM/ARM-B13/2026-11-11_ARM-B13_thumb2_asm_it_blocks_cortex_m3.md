---
id: ARM-B13
time: 70분
updated: 2026-11-11
---
# ARM-B13 Thumb-2 어셈블리 작성과 IT 블록

## 학습 목표

- `.syntax unified` / `.thumb` / `.thumb_func` / `.type %function` 로 **C 에서 부를 수 있는 Thumb-2 함수**를 쓰고, 함수 주소의 bit0(Thumb 비트)이 왜 1 이어야 하는지 설명한다.
- Cortex-M 의 r0–r15 와 **AAPCS**(r0–r3 인자, r0 반환, r4–r11 callee-saved, sp 8 바이트 정렬)를 지키는 leaf / non-leaf 함수를 쓴다 (`push {…, lr}` / `pop {…, pc}`).
- **IT 블록**(`it`, `ite`, `itt`)으로 분기 없이 조건 실행을 하고, IT 의 규칙(최대 4개, T/E 조건 일치, 분기는 마지막, IT 안에서는 16비트 명령이 플래그를 안 씀)을 어셈블러 에러와 `objdump` 로 확인한다.
- QEMU `mps2-an385`(Cortex-M3) + 세미호스팅에서 테스트를 돌리고, gdb 로 **xPSR 안의 IT 상태 비트**가 명령마다 바뀌는 것을 직접 본다.

## 1. 문제

"이 Thumb 코드 뭐 하는 거야? `ite lt` 다음 두 줄은 언제 실행돼?" — Cortex-M 펌웨어 면접과 크래시 덤프 읽기(HardFault 의 stacked pc 주변 디스어셈블)에서 매번 나오는 장면이다. STM32·nRF·SSD 컨트롤러의 Cortex-M/R 코어에서 컴파일러가 내는 코드는 대부분 Thumb-2 이고, 짧은 `if` 는 거의 다 IT 블록으로 바뀐다. 이 레슨은 그 코드를 **직접 써서** 읽는 눈을 만든다.

ARM-B01~B11 은 AArch64(맥 M6)에서 했다. 여기서는 32비트 M-profile 로 내려간다. 차이는 세 가지만 기억하면 된다: 레지스터가 r0–r15(32비트, sp/lr/pc 가 번호 있는 레지스터), 명령 길이가 16/32비트 섞임(ARM-B12), 조건 실행을 **IT 블록**으로 한다(AArch64 는 `csel` 계열, ARM-B05).

```svg
<svg viewBox="0 0 740 420" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Cortex-M3 레지스터와 AAPCS 역할</text>
  <rect x="10" y="32" width="120" height="80" class="f-acc" stroke="currentColor"/>
  <text x="20" y="55" fill="currentColor" font-family="monospace">r0  r1</text>
  <text x="20" y="75" fill="currentColor" font-family="monospace">r2  r3</text>
  <text x="140" y="55" fill="currentColor">인자 1–4, r0 = 반환값 (64비트면 r0:r1)</text>
  <text x="140" y="75" fill="currentColor">caller-saved: 호출하면 망가진다고 가정</text>
  <rect x="10" y="118" width="120" height="80" class="f-ok" stroke="currentColor"/>
  <text x="20" y="140" fill="currentColor" font-family="monospace">r4 … r8</text>
  <text x="20" y="160" fill="currentColor" font-family="monospace">r9 (플랫폼)</text>
  <text x="20" y="180" fill="currentColor" font-family="monospace">r10 r11</text>
  <text x="140" y="145" fill="currentColor">callee-saved: 쓰려면 push 로 저장 → pop 으로 복원</text>
  <text x="140" y="165" fill="currentColor">objdump 별명: r10 = sl, r11 = fp</text>
  <rect x="10" y="204" width="120" height="24" class="f-mut" stroke="currentColor"/>
  <text x="20" y="221" fill="currentColor" font-family="monospace">r12 (ip)</text>
  <text x="140" y="221" fill="currentColor">scratch, caller-saved (rc_call4 가 fn 주소를 둔다)</text>
  <rect x="10" y="232" width="120" height="72" class="f-warn" stroke="currentColor"/>
  <text x="20" y="252" fill="currentColor" font-family="monospace">r13 sp</text>
  <text x="20" y="272" fill="currentColor" font-family="monospace">r14 lr</text>
  <text x="20" y="292" fill="currentColor" font-family="monospace">r15 pc</text>
  <text x="140" y="252" fill="currentColor">MSP 또는 PSP (ARM-B14). 공개 인터페이스에서 8 바이트 정렬</text>
  <text x="140" y="272" fill="currentColor">bl/blx 가 "복귀 주소 | 1" 을 넣는다 → bx lr 로 리턴</text>
  <text x="140" y="292" fill="currentColor">bit0 은 항상 0. 함수 "주소" 의 bit0 = Thumb 표시</text>
  <rect x="10" y="316" width="720" height="96" rx="6" stroke="currentColor" fill="none"/>
  <text x="20" y="336" fill="currentColor" font-weight="bold">xPSR (gdb: $xpsr)</text>
  <text x="20" y="358" fill="currentColor" font-family="monospace">[31:28] N Z C V   [26:25] IT[1:0]</text>
  <text x="20" y="378" fill="currentColor" font-family="monospace">[24] T (항상 1)   [15:10] IT[7:2]</text>
  <text x="20" y="398" fill="currentColor" font-family="monospace">[8:0] 예외 번호 (IPSR)</text>
  <text x="400" y="358" fill="currentColor" font-family="monospace">예: 0x2100b400 = C, T, IT=0xB4</text>
  <text x="400" y="378" fill="currentColor">Cortex-M 은 Thumb 만 실행한다. T=0 이면</text>
  <text x="400" y="398" fill="currentColor">UsageFault INVSTATE (7. 실수 4 에서 실측)</text>
</svg>
```

모든 함수는 손으로 쓴 Thumb-2(`solution.S`)이고, 테스트 하네스는 펌웨어 C(`harness.c`)다. 둘을 `arm-none-eabi-gcc` 로 묶어 QEMU 의 Cortex-M3 에서 돌리고, 결과를 세미호스팅(BKPT 0xAB)으로 찍는다. 부팅 코드는 BM-B08 의 레시피를 줄인 `shared_startup.c` + `arm_b13.ld` 이다.

### Part A — 첫 Thumb-2 함수 세 개

- `int32_t max_s32(int32_t a, int32_t b)` — `cmp` + `it lt` + `movlt`. 분기 없음.
- `int32_t abs_s32(int32_t x)` — `cmp` + `it lt` + `rsblt`. `abs_s32(INT32_MIN) == INT32_MIN` (ISA 의 32비트 래핑. C 의 `-INT32_MIN` 은 UB 라서 테스트는 손으로 쓴 기대값과 비교한다).
- `int32_t sum_array_s32(const int32_t *a, uint32_t n)` — post-index `ldr r3, [r0], #4` 루프. `n == 0` 이면 **포인터를 읽지 않는다**(테스트가 `0xFFFFFFF0` 같은 엉터리 포인터를 넘긴다). 합은 mod 2^32 로 래핑.

### Part B — 경계와 규칙: 부호 없는 조건, Thumb 비트, callee-saved

- `uint32_t max_u32(uint32_t a, uint32_t b)` — 같은 `cmp` 인데 조건만 `lo`(부호 없음). `max_u32(0xFFFFFFFF, 1)` 은 `0xFFFFFFFF`, 같은 비트를 `max_s32` 에 주면 1.
- 테스트가 **함수 주소의 bit0 == 1** 을 확인하고, `shared_regcheck.S` 의 `rc_call4` 가 r4–r11 에 표지값을 넣고 불러서 **callee-saved 와 sp 가 그대로인지** 검사한다.
- IT 규칙 위반을 일부러 어셈블해서 어셈블러 에러를 본다 (`demo_it_rules.S`).

### Part C — 펌웨어: ADC 샘플 클램프와 non-leaf 함수

- `int32_t clamp_s32(int32_t x, int32_t lo, int32_t hi)` (전제 `lo <= hi`) — **ITE 블록 하나 + IT 블록 하나**. `ite lt` 의 Else 칸에 조건부 `cmpge` 를 넣는다.
- `int32_t sum_clamped_s32(const int32_t *a, uint32_t n, int32_t lo, int32_t hi)` — 12비트 ADC 샘플 배열에서 글리치(-32768, 65535 같은 값)를 `[lo, hi]` 로 자르고 합한다(평균 필터의 앞단). 원소마다 `bl clamp_s32` 를 부르는 **non-leaf** 라서 lr 과 r4–r8 을 저장해야 한다.
- gdb 로 `clamp_s32` 를 한 명령씩 돌려 xPSR 의 IT 비트와 플래그 변화를 기록한다 (`demo_it.gdb`).

## 2. Problem (English)

> **Thumb-2 on a Cortex-M3 (ARMv7-M), AAPCS.** Write these functions in GNU-syntax Thumb-2 assembly (`.syntax unified`). They are called from C:
>
> ```c
> int32_t  max_s32(int32_t a, int32_t b);            // branchless, use an IT block
> int32_t  abs_s32(int32_t x);                       // abs(INT32_MIN) == INT32_MIN (wraps)
> int32_t  sum_array_s32(const int32_t *a, uint32_t n);   // n may be 0: do not touch *a
> uint32_t max_u32(uint32_t a, uint32_t b);          // unsigned compare
> int32_t  clamp_s32(int32_t x, int32_t lo, int32_t hi);  // lo <= hi; one ITE block
> int32_t  sum_clamped_s32(const int32_t *a, uint32_t n, int32_t lo, int32_t hi);
>                                                    // calls clamp_s32 for each element
> ```
>
> Constraints: leaf functions must not touch the stack; `sum_clamped_s32` must preserve r4–r11, return via `pop {…, pc}`, and keep `sp` 8-byte aligned at the call. Sums wrap modulo 2^32. Every exported symbol must be a Thumb function (`bit0 == 1` when its address is taken).
>
> Examples: `max_s32(-1, 1) == 1`, `max_u32(0xFFFFFFFF, 1) == 0xFFFFFFFF`, `abs_s32(INT32_MIN) == INT32_MIN`, `sum_array_s32({INT32_MAX, 1}, 2) == INT32_MIN`, `clamp_s32(5000, 0, 4095) == 4095`, `sum_clamped_s32({100, 4095, 65535, -32768, 2048}, 5, 0, 4095) == 10338`.
>
> Follow-ups: How many instructions can one IT cover and what must their conditions look like? Why does `add r0, r1, r2` need a 32-bit encoding outside an IT block but not inside? What happens if you call a function whose address has bit0 = 0? Why did AArch64 drop IT?

## 3. 시작 코드

실행은 Lima VM `elinux` 안에서 한다 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1). Mac 홈은 VM 에서 읽기 전용이라 `run_qemu.sh` 가 소스를 VM 의 `/tmp/ARM-B13_work` 로 복사해 빌드한다. Mac 에서 치는 한 줄:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ARM/ARM-B13
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # solution | practice | gdb | dis | cc | itrules | bugs
```

빌드 규칙 (변형 v = starter / solution / practice):

```bash
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib \
    -fno-tree-loop-distribute-patterns -T arm_b13.ld  harness.c(또는 v.c) v.S shared_*.c shared_*.S -o v.elf -lgcc
qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none \
    -semihosting-config enable=on,target=native -kernel v.elf      # 펌웨어가 SYS_EXIT 로 종료, 종료 코드 = main 반환값
```

VM 없이 "어셈블은 되나?" 만 맥에서 확인 (실제로 됨):

```bash
clang --target=thumbv7m-none-eabi -c solution.S -o /tmp/ARM-B13_solution.o && /usr/bin/objdump -d /tmp/ARM-B13_solution.o
```

(맥의 `/usr/local/bin/llvm-objdump` 는 LLVM 3.2 라 Thumb-2 를 못 읽는다 — Xcode 의 `/usr/bin/objdump`(LLVM 21)를 쓴다.)

<!-- code: harness.c -->

<!-- code: starter.S -->

starter 실행 결과 (실제 출력 일부):

```text
ARM-B13 Thumb-2 on Cortex-M3 (QEMU mps2-an385)
--- Part A: max_s32 / abs_s32 / sum_array_s32 ---
FAIL harness.c:61  max_s32(3, 7) == 7
PASS max_s32(-5, -9) == -5
...
PASS abs_s32(INT32_MIN) == INT32_MIN
...
FAIL harness.c:154  rc_call4((anyfn_t)clamp_s32, a3, &m) == 4095 && m == 0u
FAIL harness.c:156  rc_call4((anyfn_t)sum_clamped_s32, a4, &m) == 10338 && m == 0u
16/38 passed
qemu exit=1
```

스텁이 "첫 인자 그대로" 를 돌려주므로 우연히 맞는 줄(`max_s32(-5,-9)`, `abs_s32(INT32_MIN)`)도 있다. 그래서 고정 예제 몇 개가 아니라 **14개 경계값의 모든 조합 + 2000개 난수**를 함께 본다.

부팅과 공용 검사기 (모든 변형에 링크):

<!-- code: shared_startup.c -->

<!-- code: arm_b13.ld -->

<!-- code: run_qemu.sh -->

## 4. 힌트

<details><summary>힌트 1 — 파일 머리와 함수 머리</summary>

`.syntax unified` + `.thumb` + `.text` 를 맨 위에. 함수마다 `.global f` / `.type f, %function` / `.thumb_func` / `f:` / … / `.size f, . - f`. `.type %function` 이 있어야 링커가 함수 주소에 bit0 = 1 을 붙인다. 리턴은 `bx lr`.

</details>

<details><summary>힌트 2 — max / abs</summary>

`cmp r0, r1` 은 `r0 - r1` 로 플래그만 바꾼다. "a < b 면 r0 = b" 는 `it lt` + `movlt r0, r1`. abs 는 `cmp r0, #0` 뒤 `it lt` + `rsblt r0, r0, #0` (0 - x). 부호 있는 조건은 LT/GT/LE/GE, 부호 없는 조건은 LO(=CC)/HI/LS/HS(=CS).

</details>

<details><summary>힌트 3 — 배열 합</summary>

`cbz r1, done` 으로 n == 0 을 먼저 거른다 (cbz 는 앞으로만, r0–r7 만). 루프 몸통은 `ldr r3, [r0], #4` (읽고 나서 r0 += 4) → `adds` → `subs r1, r1, #1` → `bne`. `subs` 가 Z 를 세팅하므로 따로 `cmp` 가 필요 없다.

</details>

<details><summary>힌트 4 — clamp 를 ITE 하나로</summary>

`cmp r0, r1` (x vs lo) → `ite lt` → Then: `movlt r0, r1`, Else: `cmpge r0, r2` (x vs hi). 그다음 `it gt` + `movgt r0, r2`. Then 칸을 탄 경우 플래그는 첫 `cmp` 의 LT 그대로인데, LT 이면 GT 는 반드시 거짓이라 두 번째 IT 는 아무것도 안 한다.

</details>

<details><summary>힌트 5 — non-leaf 함수의 틀</summary>

`push {r4, r5, r6, r7, r8, lr}` (6 워드 = 24 B → 8 바이트 정렬 유지) → 호출을 건너 살아야 하는 값(p, end, lo, hi, sum)은 r4–r8 에 → 루프에서 `mov r1, r6` / `mov r2, r7` / `bl clamp_s32` → 끝에 `pop {r4, r5, r6, r7, r8, pc}`. lr 을 pc 로 pop 하는 것이 곧 리턴이다.

</details>

## 5. 풀이

### Part A — max, abs, 배열 합

**max_s32.** `cmp r0, r1` 은 `r0 - r1` 을 계산해 NZCV 만 남긴다 (플래그 계산은 ARCH-B02). `it lt` 는 "다음 1개 명령은 LT 일 때만 실행" 이라는 **접두 명령**이다. 조건이 거짓이면 `movlt` 는 NOP 처럼 지나간다 (pc 는 그 명령을 거쳐 간다 — gdb 에서 실제로 멈춘다). 그래서 분기 예측 실패가 없다.

| 조건 | 플래그 식 | 의미 (cmp a, b 뒤) |
|---|---|---|
| EQ / NE | Z=1 / Z=0 | a == b / a != b |
| LT / GE | N≠V / N=V | a < b / a ≥ b (부호 있음) |
| GT / LE | Z=0 ∧ N=V / Z=1 ∨ N≠V | a > b / a ≤ b (부호 있음) |
| LO(CC) / HS(CS) | C=0 / C=1 | a < b / a ≥ b (부호 없음) |
| HI / LS | C=1 ∧ Z=0 / C=0 ∨ Z=1 | a > b / a ≤ b (부호 없음) |
| MI / PL / VS / VC | N=1 / N=0 / V=1 / V=0 | 음수 / 양수·0 / 오버플로 / 아님 |

**abs_s32.** `rsblt r0, r0, #0` 은 r0 = 0 − r0. `x = INT32_MIN = 0x80000000` 이면 0 − 0x80000000 = 0x80000000 (mod 2^32) 이라 그대로 INT32_MIN 이 된다. ISA 는 이 값을 정확히 정의하지만 C 의 `-x` 는 이 경우 UB(C11 6.5p5)이므로, 하네스는 `abs_s32(INT32_MIN) == INT32_MIN` 을 **손으로 쓴 기대값**으로 비교하고, 참조 모델은 `0u - (uint32_t)x` (부호 없는 산술, UB 없음)로 계산해 `uint32_t` 끼리 비교한다.

**sum_array_s32.** 맨 앞 `cbz r1, 2f` 가 n == 0 을 거른다. 이게 없으면 do-while 이 되어 첫 원소를 무조건 읽고, `subs` 가 0 → 0xFFFFFFFF 로 래핑해 40억 번 돈다 — 7. 실수 5 에서 실제로 BusFault 를 냈다. post-index `ldr r3, [r0], #4` 는 "r0 주소에서 읽고, 그다음 r0 += 4" 다 (AArch64 의 `ldr x, [x0], #8` 과 같은 모양, ARM-B04). 이 형태는 **32비트 인코딩만** 있다 (`ldr.w`).

objdump 로 본 Part A (`run_qemu.sh dis`, 실제 출력):

```text
00000000 <max_s32>:
   0:	4288      	cmp	r0, r1
   2:	bfb8      	it	lt
   4:	4608      	movlt	r0, r1
   6:	4770      	bx	lr

00000008 <abs_s32>:
   8:	2800      	cmp	r0, #0
   a:	bfb8      	it	lt
   c:	4240      	neglt	r0, r0
   e:	4770      	bx	lr

00000010 <sum_array_s32>:
  10:	2200      	movs	r2, #0
  12:	b121      	cbz	r1, 1e <sum_array_s32+0xe>
  14:	f850 3b04 	ldr.w	r3, [r0], #4
  18:	18d2      	adds	r2, r2, r3
  1a:	3901      	subs	r1, #1
  1c:	d1fa      	bne.n	14 <sum_array_s32+0x4>
  1e:	4610      	mov	r0, r2
  20:	4770      	bx	lr
```

- 2바이트 칸(`4288`)이 16비트, 4바이트 칸(`f850 3b04`, 하프워드 두 개)이 32비트 명령이다. 16/32 판별 규칙과 크기 통계는 ARM-B12.
- GNU objdump 는 `rsb r0, r0, #0` 을 별명 `neg` 로 보여준다 (Xcode objdump 는 `rsblt` 그대로). `.n`/`.w` 접미사는 "같은 니모닉에 다른 길이 인코딩도 있다" 를 알려 줄 때 붙는다: `ldr.w`, `bne.n`.
- 우리가 쓴 `max_s32`/`abs_s32` 는 GCC `-O2` 가 C 버전에 대해 내는 코드와 **바이트까지 같다** (`run_qemu.sh cc`: `c_max_s32` = `4288 bfb8 4608 4770`, 단 GCC 는 `a > b ? a : b` 를 `it lt` 로 뒤집어 낸다). C 버전 합 루프는 GCC 가 pre-index `ldr.w r0, [r3, #4]!` + 끝 포인터 비교로 바꿨다.

### Part B — 부호 없는 조건, Thumb 비트, callee-saved 검사

**max_u32** 는 `cmp` 가 똑같고 조건만 `lo` 다. `cmp` 는 부호를 모른다 — 한 번의 뺄셈으로 N, Z, C, V 를 모두 만들고, **어느 플래그를 읽느냐**(LT 는 N≠V, LO 는 C=0)가 부호 있음/없음을 정한다. GNU objdump 는 `lo` 를 동의어 `cc` 로 찍는다 (`bf38 it cc`). 7. 실수 1 에서 `lt` 를 쓴 버전이 `max(0xFFFFFFFF, 1) = 1` 을 내는 것을 실제로 봤다.

**Thumb 비트.** Cortex-M 은 Thumb 상태만 있다. `bx`/`blx`/`pop {pc}`/`ldr pc` 로 점프할 때 대상 주소의 bit0 이 새 T 비트가 되므로, 함수 주소는 항상 **홀수**여야 한다. 어셈블러에서 `.type f, %function`(또는 `.thumb_func`)을 달면 ELF 심볼이 `FUNC` + Thumb 로 표시되고, 링커가 주소를 쓸 때 +1 을 한다. 실제로 `bug5.elf` 의 심볼 표:

```text
    53: 0000010d     8 FUNC    GLOBAL DEFAULT    1 max_s32
    62: 000000f8     0 NOTYPE  GLOBAL DEFAULT    1 max_nothumb
```

`max_s32` 는 0x10c 에 있지만 심볼 값은 0x10d(=|1). 표시 없는 `max_nothumb` 은 0xf8 그대로라, 함수 포인터로 부르면 폴트가 난다 (7. 실수 4). 하네스는 여섯 함수 모두 `((uintptr_t)f & 1) == 1` 을 확인한다. 벡터 테이블의 Thumb 비트는 ARM-M08.

**callee-saved 검사기** `rc_call4(fn, args[4], &bad)` 는 r4–r11 에 `0xC0DE0004`…`0xC0DE000B` 를 넣고 `blx r12` 로 fn 을 부른 뒤 하나씩 비교한다. 이 상수들은 Thumb-2 수정 즉시값(modified immediate: 8비트를 회전하거나 `0x00XY00XY`/`0xXY00XY00`/`0xXYXYXYXY` 반복)으로 못 만들기 때문에 `ldr r4, =0xC0DE0004` 가 **리터럴 풀** 로드가 된다 (objdump: `4c22 ldr r4, [pc, #136]` 와 함수 뒤의 `.word 0xc0de0004`). 32비트 상수를 싣는 방법은 둘: `movw`+`movt` (2 명령 8 B, 메모리 읽기 없음) 또는 `ldr =` 리터럴 풀 (명령 2–4 B + 상수 4 B, 로드 1회).

<!-- code: shared_regcheck.S -->

**IT 규칙 — 어셈블러가 거절하는 것들** (`run_qemu.sh itrules`, GNU as 2.45.50 실제 출력):

```text
### CASE 1   (IT 없이 movgt)
demo_it_rules.S:12: Error: thumb conditional instruction should be in IT block -- `movgt r0,r1'
### CASE 2   (ite gt 다음에 movgt, movgt)
demo_it_rules.S:17: Error: incorrect condition in IT block -- `movgt r0,r2'
### CASE 3   (it gt 하나에 조건 명령 두 개)
demo_it_rules.S:22: Error: thumb conditional instruction should be in IT block -- `addgt r0,r0,#1'
### CASE 4   (itt eq 의 첫 칸에 beq)
demo_it_rules.S:26: Error: branch must be last instruction in IT block -- `beq 1f'
### CASE 5   (it eq 안에 cbz)
demo_it_rules.S:32: Error: instruction not allowed in IT block -- `cbz r0,1f'
### CASE 6   (ittttt — 5개)
demo_it_rules.S:36: Error: bad instruction `ittttt eq'
```

clang(맥)도 같은 것을 다른 말로 거절한다: `error: incorrect condition in IT block; got 'gt', but expected 'le'`, `error: predicated instructions must be in IT block`. GNU as 에 `-Wa,-mimplicit-it=always` 를 주면 CASE 1 에 IT 를 **몰래 끼워 넣는다** (`bfc8 it gt` 가 생김) — 손으로 쓴 asm 에서는 실수를 덮으므로 기본값(Thumb 에서는 에러)을 권한다.

| IT 규칙 | 이유 / 확인 |
|---|---|
| IT 하나는 1–4개 명령 (`it`, `itt`, `ite`, `ittt`, … `itete` 등) | 마스크 4비트 (아래 그림). CASE 6 |
| 각 명령의 조건 = firstcond 또는 그 반대 (T/E 패턴) | UAL 은 조건을 적게 해서 사람이 검증. CASE 2 |
| 분기(`b<c>`, `bx`, `pop {pc}` 등)는 블록의 **마지막**만 | 블록 중간에서 pc 가 바뀌면 IT 상태가 꼬인다. CASE 4 |
| `cbz/cbnz`, `it` 자체, `bkpt` 등은 IT 안 금지 | Armv7-M ARM 의 IT 설명. CASE 5 |
| IT 안의 16비트 데이터처리 명령은 **플래그를 안 쓴다** | 다음 표 |
| `cmp`/`cmn`/`tst` 는 IT 안에서도 플래그를 쓴다 | 그게 일이니까. `clamp_s32` 의 `cmpge` |

**IT 안/밖의 인코딩** (`demo_encodings.S`, 실제 objdump). 16비트 데이터처리 인코딩은 대부분 "IT 밖이면 `…s`(플래그 O), IT 안이면 플래그 X" 로 정의돼 있다. 그래서 같은 니모닉의 길이가 위치에 따라 바뀐다:

| 명령 | IT 밖 | IT 안 (gt) |
|---|---|---|
| `adds r0, r0, r1` / `addgt r0, r1, r2` | `1840` 16비트 | `1888` 16비트 (플래그 X) |
| `add r0, r1, r2` / `addsgt r0, r1, r2` | `eb01 0002` 32비트 `add.w` | `eb11 0002` 32비트 `addsgt.w` |
| `rsbs r0, r0, #0` / `rsbgt r0, r0, #0` | `4240` 16비트 (`negs`) | `4240` 16비트 (`neggt`) |
| `rsb r0, r0, #0` | `f1c0 0000` 32비트 | — |
| `movs r0, #1` / `movgt r0, #1` | `2001` 16비트 | `2001` 16비트 |
| `mov r0, #1` / `movsgt r0, #1` | `f04f 0001` `mov.w` | `f05f 0001` `movsgt.w` |
| `movgt r0, #300` | — | `f44f 7096` `movgt.w` (즉시값 > 255) |

같은 비트 패턴 `4240` 이 IT 밖에서는 `negs`, IT 안에서는 `neggt` 다 — CPU 는 xPSR 의 IT 상태를 보고 플래그를 쓸지 정한다. 그래서 `abs_s32` 의 `rsblt` 가 공짜로 16비트가 됐다.

### Part C — clamp (ITE) 와 non-leaf 합

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">ite lt = 0xBFB4 → xPSR 의 ITSTATE (gdb 실측값)</text>
  <rect x="10" y="35" width="160" height="34" class="f-mut" stroke="currentColor"/>
  <text x="22" y="57" fill="currentColor" font-family="monospace">1011 1111</text>
  <rect x="170" y="35" width="110" height="34" class="f-acc" stroke="currentColor"/>
  <text x="182" y="57" fill="currentColor" font-family="monospace">1011</text>
  <rect x="280" y="35" width="110" height="34" class="f-ok" stroke="currentColor"/>
  <text x="292" y="57" fill="currentColor" font-family="monospace">0100</text>
  <text x="22" y="88" fill="currentColor">IT 명령 (0xBF__)</text>
  <text x="172" y="88" fill="currentColor">firstcond = LT</text>
  <text x="282" y="88" fill="currentColor">mask: E, 끝표시 1</text>
  <text x="410" y="50" fill="currentColor">mask bit3 = firstcond[0] 이면 T,</text>
  <text x="410" y="68" fill="currentColor">반대면 E. 맨 아래 1 = 블록 끝.</text>
  <text x="410" y="86" fill="currentColor">LT(1011)[0]=1, bit3=0 → E</text>
  <text x="10" y="125" fill="currentColor" font-weight="bold">명령마다 ITSTATE[4:0] 가 왼쪽으로 한 칸 (ITSTATE[7:5] 는 그대로)</text>
  <rect x="10" y="138" width="230" height="64" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="160" fill="currentColor" font-family="monospace">ITSTATE = 0xB4</text>
  <text x="20" y="178" fill="currentColor" font-family="monospace">xpsr = 0x2100b400</text>
  <text x="20" y="196" fill="currentColor">다음: movlt (조건 LT)</text>
  <rect x="260" y="138" width="230" height="64" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="270" y="160" fill="currentColor" font-family="monospace">ITSTATE = 0xA8</text>
  <text x="270" y="178" fill="currentColor" font-family="monospace">xpsr = 0x2100a800</text>
  <text x="270" y="196" fill="currentColor">다음: cmpge (조건 GE)</text>
  <rect x="510" y="138" width="220" height="64" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="520" y="160" fill="currentColor" font-family="monospace">ITSTATE = 0x00</text>
  <text x="520" y="178" fill="currentColor" font-family="monospace">xpsr = 0x21000000</text>
  <text x="520" y="196" fill="currentColor">IT 블록 밖</text>
  <path d="M240 170 L258 170" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M490 170 L508 170" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="10" y="232" fill="currentColor" font-family="monospace">ITSTATE[7:2] = xPSR[15:10],  ITSTATE[1:0] = xPSR[26:25]</text>
  <text x="10" y="252" fill="currentColor" font-family="monospace">0xB4 = 1011 0100 → [15:10] = 101101 → 0xb400</text>
  <text x="10" y="272" fill="currentColor">현재 조건 = ITSTATE[7:4] (1011 = LT → 1010 = GE). ITSTATE[3:0] = 0 이면 블록 끝.</text>
  <text x="10" y="292" fill="currentColor">예외가 IT 블록 중간에 들어와도 xPSR 째로 스택에 저장 → 복귀 후 이어서 실행된다.</text>
</svg>
```

**clamp_s32 실측 트레이스** — `run_qemu.sh gdb` (QEMU `-s -S` + `gdb-multiarch -batch -x demo_it.gdb`)의 실제 출력. 각 줄은 "그 명령을 실행하기 **전**" 상태다:

```text
=== call 1: clamp_s32(5000, 0, 4095) ===
pc=0xe0e r0=5000   r1=0  r2=4095  xpsr=0x41000000  next: => 0xe0e <clamp_s32>:	cmp	r0, r1
pc=0xe10 r0=5000   r1=0  r2=4095  xpsr=0x21000000  next: => 0xe10 <clamp_s32+2>:	ite	lt
pc=0xe12 r0=5000   r1=0  r2=4095  xpsr=0x2100b400  next: => 0xe12 <clamp_s32+4>:	movlt	r0, r1
pc=0xe14 r0=5000   r1=0  r2=4095  xpsr=0x2100a800  next: => 0xe14 <clamp_s32+6>:	cmpge	r0, r2
pc=0xe16 r0=5000   r1=0  r2=4095  xpsr=0x21000000  next: => 0xe16 <clamp_s32+8>:	it	gt
pc=0xe18 r0=5000   r1=0  r2=4095  xpsr=0x2100c800  next: => 0xe18 <clamp_s32+10>:	movgt	r0, r2
pc=0xe1a r0=4095   r1=0  r2=4095  xpsr=0x21000000  next: => 0xe1a <clamp_s32+12>:	bx	lr
=== call 2: clamp_s32(-7, 0, 4095) ===
pc=0xe0e r0=-7     r1=0  r2=4095  xpsr=0x41000000  next: => 0xe0e <clamp_s32>:	cmp	r0, r1
pc=0xe10 r0=-7     r1=0  r2=4095  xpsr=0xa1000000  next: => 0xe10 <clamp_s32+2>:	ite	lt
pc=0xe12 r0=-7     r1=0  r2=4095  xpsr=0xa100b400  next: => 0xe12 <clamp_s32+4>:	movlt	r0, r1
pc=0xe14 r0=0      r1=0  r2=4095  xpsr=0xa100a800  next: => 0xe14 <clamp_s32+6>:	cmpge	r0, r2
pc=0xe16 r0=0      r1=0  r2=4095  xpsr=0xa1000000  next: => 0xe16 <clamp_s32+8>:	it	gt
pc=0xe18 r0=0      r1=0  r2=4095  xpsr=0xa100c800  next: => 0xe18 <clamp_s32+10>:	movgt	r0, r2
pc=0xe1a r0=0      r1=0  r2=4095  xpsr=0xa1000000  next: => 0xe1a <clamp_s32+12>:	bx	lr
```

읽는 법 (xpsr 맨 앞 니블 = NZCV):

| 단계 | call 1 (x=5000) | call 2 (x=-7) |
|---|---|---|
| `cmp r0, r1` 후 | 0x2 = C → N=V=0 → **GE** | 0xa = N,C → N≠V → **LT** |
| `ite lt` 후 | ITSTATE 0xB4 (다음 LT) | 0xB4 |
| `movlt r0, r1` | 조건 거짓 → 지나감, r0=5000 | 실행 → r0 = 0 |
| `cmpge r0, r2` | 실행: 5000−4095 → 0x2 = **GT** | 조건 거짓 → 플래그 0xa 유지 |
| `it gt` / `movgt` | 실행 → r0 = 4095 | GT = Z=0 ∧ N=V → 거짓 → r0 = 0 유지 |

- 첫 줄의 `0x41000000` 은 호출 전 하네스가 남긴 Z 플래그다 — 입구의 플래그는 아무 의미가 없다(AAPCS 상 플래그는 보존 대상이 아님).
- bit 24 (`0x01000000`) 는 항상 1 = T 비트. QEMU 의 gdb stub 은 `$xpsr` 에 IT 비트를 그대로 보여준다 (QEMU 10.2.1 에서 확인).
- 조건이 거짓인 `movlt` 에서도 gdb 가 멈춘다 — 조건 실행 명령은 "실행되지만 효과가 없는" 명령이다. 사이클은 Cortex-M3 TRM 기준 보통 1 사이클을 그대로 쓰고, IT 자체는 앞 명령과 접혀(folding) 0 사이클이 될 수 있다고 한다 (TRM 확인 권장, 이 레슨은 사이클을 재지 않음).

**sum_clamped_s32 의 스택 프레임.** 호출 직전 sp = S 라고 하면:

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">push {r4, r5, r6, r7, r8, lr} (= stmdb sp!, 6 워드) 직후</text>
  <rect x="120" y="35" width="200" height="28" class="f-mut" stroke="currentColor"/>
  <text x="130" y="54" fill="currentColor" font-family="monospace">호출자의 스택</text>
  <text x="10" y="54" fill="currentColor" font-family="monospace">S   →</text>
  <rect x="120" y="63" width="200" height="28" class="f-warn" stroke="currentColor"/>
  <text x="130" y="82" fill="currentColor" font-family="monospace">lr (복귀 주소|1)</text>
  <text x="10" y="82" fill="currentColor" font-family="monospace">S-4</text>
  <rect x="120" y="91" width="200" height="28" class="f-ok" stroke="currentColor"/>
  <text x="130" y="110" fill="currentColor" font-family="monospace">r8 (호출자 값)</text>
  <text x="10" y="110" fill="currentColor" font-family="monospace">S-8</text>
  <rect x="120" y="119" width="200" height="28" class="f-ok" stroke="currentColor"/>
  <text x="130" y="138" fill="currentColor" font-family="monospace">r7</text>
  <text x="10" y="138" fill="currentColor" font-family="monospace">S-12</text>
  <rect x="120" y="147" width="200" height="28" class="f-ok" stroke="currentColor"/>
  <text x="130" y="166" fill="currentColor" font-family="monospace">r6</text>
  <text x="10" y="166" fill="currentColor" font-family="monospace">S-16</text>
  <rect x="120" y="175" width="200" height="28" class="f-ok" stroke="currentColor"/>
  <text x="130" y="194" fill="currentColor" font-family="monospace">r5</text>
  <text x="10" y="194" fill="currentColor" font-family="monospace">S-20</text>
  <rect x="120" y="203" width="200" height="28" class="f-acc" stroke="currentColor"/>
  <text x="130" y="222" fill="currentColor" font-family="monospace">r4</text>
  <text x="10" y="222" fill="currentColor" font-family="monospace">S-24 ← sp</text>
  <text x="350" y="82" fill="currentColor">번호가 작은 레지스터가 낮은 주소 (STMDB 규칙)</text>
  <text x="350" y="110" fill="currentColor">24 B = 8 의 배수 → bl clamp_s32 시점에도 sp 8B 정렬</text>
  <text x="350" y="138" fill="currentColor">push {r4-r7, lr} (5 워드 = 20 B) 였다면 4B 어긋남:</text>
  <text x="350" y="156" fill="currentColor">AAPCS 위반 (64비트 인자·printf 의 double 에서 문제)</text>
  <text x="350" y="194" fill="currentColor">pop {r4, …, r8, pc}: 같은 6 워드를 역순으로,</text>
  <text x="350" y="212" fill="currentColor">lr 자리 값이 pc 로 → 리턴 (bit0 → Thumb 유지)</text>
  <text x="10" y="252" fill="currentColor">p, end, lo, hi, sum 이 bl 을 건너 살아야 함 → r4–r8. r0–r3, r12 는 망가져도 된다.</text>
</svg>
```

objdump 로 보면 `push` 는 32비트 `stmdb sp!, {r4, r5, r6, r7, r8, lr}` (`e92d 41f0`) 다. r8 이 들어가서 16비트 `push`(r0–r7 + lr 만 가능)를 못 쓴다. 연습 4 의 `push {r4, lr}` 는 16비트 `b510` 이다. `add r8, r8, r0` 는 16비트 `4480 add r8, r0` — high 레지스터용 16비트 add 는 IT 밖에서도 플래그를 쓰지 않는다.

solution 전체는 100 바이트: 16비트 명령 36개(72 B) + 32비트 명령 7개(28 B) (`arm-none-eabi-size solution.o` → `text 100`).

## 6. 정답 코드

<!-- code: solution.S diff=starter.S -->

실행 결과 (`run_qemu.sh solution`, 실제 출력 일부):

```text
ARM-B13 Thumb-2 on Cortex-M3 (QEMU mps2-an385)
--- Part A: max_s32 / abs_s32 / sum_array_s32 ---
PASS max_s32(3, 7) == 7
...
PASS sum_array_s32((const int32_t *)0xFFFFFFF0u, 0) == 0
PASS bad_sum == 0
--- Part B: unsigned 조건, Thumb 비트, callee-saved ---
...
PASS ((uintptr_t)clamp_s32 & (uintptr_t)sum_clamped_s32 & 1u) == 1u
...
PASS rc_call4((anyfn_t)sum_clamped_s32, a4, &m) == 10338 && m == 0u
38/38 passed
qemu exit=0
```

## 7. 자주 하는 실수

모두 `run_qemu.sh bugs` 로 재현한 실제 결과다 (`demo_bugs.S` + `demo_bugs.c`, BUG 마다 따로 빌드).

**실수 1 — 부호 없는 값에 부호 있는 조건.** `it lt` 로 쓴 `max_u32_lt`:

```text
BUG1 max_u32_lt(0xFFFFFFFF, 1) = 0x00000001  (expected 0xffffffff)
```

`cmp` 결과는 같고 조건만 다르다. 레지스터 맵 값, 길이, 주소, 타임스탬프 같은 부호 없는 값은 `lo/hi/ls/hs`. 고친 코드: `it lo` / `movlo r0, r1`.

**실수 2 — 쓰는 callee-saved 를 push 목록에서 빠뜨림.** `push {r5, r6, r7, r8, lr}` 로 r4 를 빠뜨린 버전:

```text
BUG2 sum_clamped_nosave = 10338  clobbered mask = 0x00000010
```

반환값은 맞다! 단위 테스트가 값만 보면 통과하고, 호출자(예: 컴파일러가 r4 에 루프 변수를 둔 main)가 나중에 엉뚱하게 죽는다. `rc_call4` 같은 표지값 검사가 bit 4 를 잡아낸다. 고친 코드: `push {r4, r5, r6, r7, r8, lr}` / `pop {r4, r5, r6, r7, r8, pc}`.

**실수 3 — non-leaf 에서 lr 을 저장하지 않음.** `push {r4-r8}` … `pop {r4-r8}` + `bx lr`:

```text
BUG3 sum_clamped_nolr(adc, 0, ...) = 0   (n == 0: bl 이 없어 lr 이 살아 있다)
BUG3 sum_clamped_nolr(adc, 5, ...) 호출 → ...
HardFault! CFSR=0x00008200 HFSR=0x40000000 BFAR=0x24000000
  stacked pc=0x000000e2 lr=0x000000ef xpsr=0x21000200
```

`bl clamp_s32` 가 lr 을 "`add r8, r8, r0` 의 주소|1" (=0xef) 로 덮어써서 마지막 `bx lr` 이 **자기 루프 안으로** 돌아간다. 거기서 `pop` 이 반복되며 sp 가 스택 위로 올라가고, 쓰레기가 된 r4 로 `ldr.w r0, [r4], #4` (pc=0xe2) 를 하다가 매핑 안 된 0x24000000 에서 정밀(precise) BusFault → HardFault 로 승격(HFSR.FORCED). 정확한 경로는 스택 내용에 따라 달라진다 — 핵심은 "n == 0 테스트만 하면 안 보인다" 이다. CFSR 해독은 ARM-M12.

**실수 4 — `.type %function` / `.thumb_func` 를 빠뜨림 (Thumb 비트 없음).**

```text
BUG5 &max_nothumb = 0x000000f8  (bit0 = 0 → Thumb 비트 없음). blx 로 호출 → ...
HardFault! CFSR=0x00020000 HFSR=0x40000000 BFAR=0x00000000
  stacked pc=0x000000f8 lr=0x00000063 xpsr=0x20000000
BUG7 &max_nothumb2 = 0x00000102  (bit0 = 0, 그리고 4 의 배수도 아님). blx 로 호출 → ...
HardFault! CFSR=0x01000000 HFSR=0x40000000 BFAR=0x00000000
  stacked pc=0x00000102 lr=0x00000063 xpsr=0x20000000
```

`blx r3` 가 T 비트를 0 으로 만들고(stacked xpsr 의 bit24 = 0), 다음 명령 실행에서 UsageFault. 워드 정렬 주소에서는 CFSR = 0x00020000 = **INVSTATE** — 교과서 답이다. 주소가 4n+2 일 때 QEMU 10.2.1 은 0x01000000 = UNALIGNED 를 보고했다; 실리콘 Cortex-M3 는 Armv7-M ARM 기준 이 경우도 INVSTATE 일 것으로 보이며(T=0 상태로 명령을 실행하려 함), 이것은 QEMU 의 에뮬레이션 차이로 보인다 (실기 확인 권장). C 에서 직접 `bl` 로 부르면 아예 링크가 안 된다:

```text
### BUG 6
/tmp/ccfqpLVk.o(max_nothumb): Unknown destination type (ARM/Thumb) in /tmp/ccf7RfIl.o
/tmp/ccf7RfIl.o: in function `main':
/tmp/ARM-B13_work/demo_bugs.c:53:(.text.startup+0xc): dangerous relocation: unsupported relocation
collect2: error: ld returned 1 exit status
```

(`run_qemu.sh` 가 `ld:` 앞의 툴체인 경로를 잘라서 보여준다. 링커는 심볼 타입이 NOTYPE 이라 `bl` 을 Thumb→Thumb `bl` 로 둘지 ARM 으로 가는 `blx` 로 바꿀지 정하지 못한다.)

**실수 5 — n == 0 가드 없는 do-while.**

```text
BUG4 sum_noguard(0xFFFFFFF0, 0) 호출 → ...
HardFault! CFSR=0x00008200 HFSR=0x40000000 BFAR=0xfffffff0
  stacked pc=0x000000cc lr=0x00000053 xpsr=0x61000000
```

첫 `ldr` 이 n 과 상관없이 실행됐다(BFAR = 넘긴 포인터). 정상 포인터였다면 `subs` 가 0xFFFFFFFF 로 래핑해 RAM 끝까지 읽다가 같은 폴트를 낸다. 고친 코드: 루프 앞 `cbz r1, 2f`.

**실수 6 — Cortex-M0 에 같은 코드를.** ARMv6-M 에는 IT, CBZ, post-index `ldr` 가 없다 (`-mcpu=cortex-m0` 로 solution.S 를 어셈블한 실제 에러):

```text
solution.S:17: Error: selected processor does not support `it lt' in Thumb mode
solution.S:41: Error: selected processor does not support `cbz r1,2f' in Thumb mode
solution.S:42: Error: cannot honor width suffix -- `ldr r3,[r0],#4'
```

M0/M0+ (STM32G0, RP2040, nRF51) 용은 짧은 분기(`bge 1f`)로 쓴다.

## 8. 면접 Q&A

<details><summary>Q1. IT 명령은 정확히 무엇을 하나? 몇 개까지, 어떤 제약이 있나?</summary>

뒤따르는 1–4개 명령에 조건을 붙이는 접두 명령이다. 명령 안에 firstcond(4비트)와 mask(4비트)가 있고, 실행되면 이 8비트가 xPSR 의 ITSTATE(EPSR 의 [26:25], [15:10])로 들어간다. 다음 명령마다 ITSTATE 가 한 칸씩 밀리며 각 명령의 조건이 정해진다. 제약: 각 명령의 조건은 firstcond 또는 그 반대(T/E), 분기는 마지막 칸만, `cbz`·`it` 등은 금지. 블록 중간에 예외가 와도 ITSTATE 가 xPSR 째로 스택에 저장되므로 복귀 후 이어서 실행된다 (이 레슨에서 gdb 로 0xB4 → 0xA8 → 0 을 확인).

</details>

<details><summary>Q2. (꼬리) `add r0, r1, r2` 가 IT 밖에서는 32비트, IT 안에서는 16비트인 이유는?</summary>

16비트 Thumb 데이터처리 인코딩은 "IT 밖에서는 플래그를 쓴다" 로 정의돼 있다 (원래 Thumb-1 은 거의 모든 ALU 명령이 플래그를 썼다). 그래서 플래그를 안 쓰는 `add`(s 없음)를 IT 밖에서 쓰려면 S 비트를 고를 수 있는 32비트 `add.w` 가 필요하다. IT 안에서는 같은 16비트 인코딩이 플래그를 안 쓰는 뜻이 되므로 `addgt r0, r1, r2` 가 16비트가 된다. 반대로 IT 안에서 플래그를 쓰고 싶으면 (`addsgt`) 32비트다. 실제 objdump: 밖 `eb01 0002 add.w`, 안 `1888 addgt`, 안+s `eb11 0002 addsgt.w`. 코드 크기를 아끼려면 IT 밖에서는 플래그가 필요 없어도 `adds` 를 쓰는 게 관례다.

</details>

<details><summary>Q3. 함수 포인터의 bit0 은 뭐고, 0 이면 Cortex-M 에서 무슨 일이 나나?</summary>

ARM/Thumb 인터워킹 규칙: `bx`/`blx`/`pop {pc}`/`ldr pc` 의 대상 주소 bit0 이 새 실행 상태(1 = Thumb)다. Cortex-M 은 Thumb 만 있으므로 bit0 = 0 으로 점프하면 EPSR.T = 0 이 되고 다음 명령에서 UsageFault(INVSTATE) → 핸들러가 꺼져 있으면 HardFault(FORCED). 이 레슨에서 실측: CFSR = 0x00020000, stacked xpsr 의 bit24 = 0. 원인은 보통 asm 에서 `.type f, %function` 을 빠뜨린 것, 또는 벡터 테이블에 주소를 손으로 적은 것 (ARM-M08). 리셋 벡터의 bit0 이 0 이면 첫 명령에서 같은 INVSTATE → HardFault (BM-B08 실측 `CFSR=0x00020000`), HardFault 벡터까지 짝수면 Lockup.

</details>

<details><summary>Q4. caller-saved 와 callee-saved 는 누가 비용을 내나? 왜 둘로 나누나?</summary>

r0–r3, r12 (와 lr) 는 caller-saved: 호출 후에도 필요한 값이 있으면 **호출하는 쪽**이 저장한다. r4–r11 은 callee-saved: 쓰려는 **불리는 쪽**이 push/pop 한다. 짧은 leaf 함수(이 레슨의 max/abs/clamp)는 r0–r3, r12 만으로 끝나서 스택을 전혀 안 건드리고, 긴 루프를 돌며 다른 함수를 부르는 함수는 r4+ 에 값을 두면 호출마다 저장할 필요가 없다. 양쪽의 흔한 경우를 다 싸게 만드는 절충이다. r9 는 AAPCS 에서 플랫폼 레지스터(RTOS·PIC 에서 정적 베이스 등)로 쓰일 수 있어서 이식성 있는 asm 은 피하는 게 안전하다.

</details>

<details><summary>Q5. non-leaf 함수가 lr 을 꼭 저장해야 하는 이유? `pop {pc}` 와 `bx lr` 의 차이는?</summary>

`bl`/`blx` 가 lr 을 덮어쓰기 때문이다. 저장 안 하면 리턴 주소를 잃는다 — 실수 3 에서 `bx lr` 이 자기 루프로 돌아가 BusFault 로 끝났다. `push {…, lr}` 로 저장했다가 `pop {…, pc}` 로 복원하면 복원과 리턴이 한 명령이다. ARMv7-M 에서 `pop {pc}` 도 인터워킹 분기라 bit0 으로 상태를 정한다 (그래서 lr 의 |1 이 그대로 의미가 있다). 예외 핸들러 안에서는 lr 이 EXC_RETURN 값(0xFFFFFFF9 등)이라 이 pop/bx 가 예외 복귀를 일으킨다 — ARM-M09.

</details>

<details><summary>Q6. 왜 sp 를 8 바이트로 맞추나? `push {r4-r7, lr}` 는 틀린 건가?</summary>

AAPCS 는 sp 가 항상 4 바이트 정렬, **공개 인터페이스(다른 함수를 부르는 순간)** 에서 8 바이트 정렬이길 요구한다. 64비트 값(`int64_t`, `double`)을 스택에 `ldrd/strd` 로 다루는 코드와 가변 인자(`printf("%f")`)가 이것을 가정한다. 5 워드 push 는 4 B 어긋나므로 다른 함수를 부를 거라면 레지스터를 하나 더 넣어(`push {r3-r7, lr}` 처럼 쓰레기 레지스터를 끼우는 것이 컴파일러의 흔한 트릭) 짝수 워드로 맞춘다. 이 레슨의 `sum_clamped_s32` 는 6 워드, 연습 4 는 2 워드. Cortex-M3 는 예외 진입 때 CCR.STKALIGN 에 따라 하드웨어가 8 B 정렬을 맞춰 주기도 한다 (r2p0 부터 리셋 기본값 1 로 알려짐, TRM 확인 권장).

</details>

<details><summary>Q7. AArch64 는 왜 IT 를 없앴나? ARMv8-A 의 AArch32 에서 IT 는?</summary>

IT 는 다음 명령들의 해석을 바꾸는 **숨은 상태**(ITSTATE)를 만든다 — 디코더가 이전 명령을 기억해야 하고, 예외·디버거·명령 융합이 모두 이 상태를 신경 써야 한다. 넓은 out-of-order 코어에서는 비싸다. AArch64 는 조건 실행을 "한 명령 안에서 끝나는" `csel/csinc/csneg/ccmp` 로 바꿨다 (ARM-B05). ARMv8-A 의 AArch32 에서는 IT 블록 중 **명령 2개 이상 또는 32비트 명령을 포함하는 형식**이 성능상 폐기(deprecated)됐다 (Armv8-A ARM 기준, 확인 권장). 실제로 GNU as 에 `-march=armv8-a -Wa,-mwarn-restrict-it` 를 주면 `Warning: IT blocks containing more than one conditional instruction are performance deprecated in ARMv8-A and ARMv8-R` 가 나오고, GCC 는 `-march=armv8-a` 에서 기본 `-mrestrict-it` 라 `x > y ? x + 300 : y` 를 `ble.n` + `add.w` 로 냈다 (`-mno-restrict-it` 면 `it gt` + `addgt.w`, cortex-m3 와 같음). Cortex-M(ARMv7-M/ARMv8-M Mainline)에서는 IT 가 여전히 정상이다.

</details>

<details><summary>Q8. Thumb-2 에서 32비트 상수나 주소를 레지스터에 넣는 방법은?</summary>

(1) 수정 즉시값으로 표현되면 `mov.w` 한 개 (`0x80000000`, `0x44444444`, `0x00FF00FF` 등). (2) `movw r0, #:lower16:x` + `movt r0, #:upper16:x` — 2 명령 8 B, 메모리 읽기 없음, 플래시 대기 상태가 있는 MCU 에서 선호. (3) `ldr r0, =x` — 어셈블러가 근처(±4 KB)에 리터럴 풀을 만들고 pc 상대 로드. 명령이 16비트일 수 있어 짧지만 데이터 읽기가 1번 생긴다. `rc_call4` 의 표지값이 (3) 이다 (`.ltorg` 로 풀 위치 지정). AArch64 는 MOVZ/MOVK 와 ADRP+ADD (ARM-B03, ARM-A09).

</details>

## 9. 추가 연습

<details><summary>연습 1 — <code>sign_s32(x)</code>: −1/0/+1 을 ITE 하나로 (양쪽 칸을 다 쓰기)</summary>

`cmp r0, #0` → `ite gt` → `movgt r0, #1` / `asrle r0, r0, #31`. x ≤ 0 일 때 산술 시프트 31 은 0 이면 0, 음수면 −1(0xFFFFFFFF) 이다. 두 명령 모두 IT 안이라 16비트(`2001`, `17c0`) — IT 밖이었다면 `movs`/`asrs` 로 플래그를 썼을 인코딩이다.

</details>

<details><summary>연습 2 — <code>max_array_s32(a, n)</code>: n == 0 이면 INT32_MIN</summary>

`mov r2, #0x80000000` (수정 즉시값이라 `mov.w` 한 개) → `cbz r1` → 루프: `ldr r3, [r0], #4`, `cmp r3, r2`, `it gt`, `movgt r2, r3`, `subs`, `bne`. 초기값을 a[0] 로 하면 n == 0 에서 메모리를 읽는다 — 실수 5 와 같은 함정.

</details>

<details><summary>연습 3 — <code>sat_add_s32(a, b)</code>: 오버플로면 INT32_MAX/INT32_MIN 으로 포화 (ITT)</summary>

`adds r2, r0, r1` 이 V 를 만든다. `itt vs` → `mvnvs r2, #0x80000000` (= 0x7FFFFFFF) → `addvs r2, r2, r0, lsr #31` (a 가 음수면 +1 → 0x80000000). 오버플로는 a, b 의 부호가 같을 때만 나므로 a 의 부호가 곧 포화 방향이다. 0x7FFFFFFF 는 수정 즉시값이 아니지만 `mvn` 으로 0x80000000 의 보수를 쓰면 한 명령이다. Cortex-M4 이상은 `qadd` 한 개 (ARM-A07). C 참조는 `int64_t` 합 (UB 없음).

</details>

<details><summary>연습 4 — <code>count_in_range_s32(a, n, lo, hi)</code>: 조건부 비교 체인의 순서 함정</summary>

`cmp x, hi` → `it le` → `cmple lo, x` → `it le` → `addle r12, r12, #1`. x > hi 면 두 번째 cmp 가 건너뛰어지고 플래그가 GT 그대로라 마지막 LE 가 거짓. 순서를 바꿔 `cmp x, lo` → `it ge` → `cmpge x, hi` → `it le` 로 쓰면 x < lo 일 때 플래그가 LT 로 남는데 **LT 는 LE 를 만족**하므로 범위 밖을 센다 — 실제로 이렇게 바꿔 돌리면 18/22 passed (4개 FAIL, `count_in_range_s32(s, 8, 5000, 5000) == 0u` 포함). 규칙: "건너뛴 경로의 플래그가 최종 조건을 거짓으로 만들어야 한다." r4 를 쓰므로 `push {r4, lr}` (16비트 `b510`, 8 B 정렬).

</details>

<details><summary>연습 5 — xPSR 값에서 IT 상태 읽기 (C)</summary>

`ITSTATE = ((xpsr >> 8) & 0xFC) | ((xpsr >> 25) & 3)`. ITSTATE[3:0] == 0 이면 블록 밖, 아니면 다음 명령의 조건 = ITSTATE[7:4], 남은 명령 수 = 4 − (ITSTATE[3:0] 의 맨 아래 1 의 위치). gdb 실측값으로: 0x2100b400 → LT, 2개 / 0x2100a800 → GE, 1개 / 0x2100c800 → GT, 1개 / 0x21000000 → 밖. HardFault 덤프의 stacked xpsr 에서 "IT 블록 한가운데서 죽었나?" 를 볼 때 쓴다.

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"
```

실제 출력 끝부분:

```text
PASS it_next_cond(0x2100c800u) == 0xC && it_insns_left(0x2100c800u) == 1u
PASS it_next_cond(0x21000000u) == -1 && it_insns_left(0xa1000000u) == 0u
22/22 passed
qemu exit=0
```

## 10. 핵심 정리

1. 파일 머리 `.syntax unified` + `.thumb`, 함수마다 `.type f, %function` + `.thumb_func` → 함수 주소 bit0 = 1. 빠뜨리면 INVSTATE UsageFault 또는 링크 에러.
2. AAPCS(32): r0–r3 인자, r0 반환, r0–r3·r12 는 망가져도 됨, r4–r11 은 쓰면 저장. 다른 함수를 부를 땐 sp 8 B 정렬 (짝수 개 push).
3. leaf 는 `bx lr`, non-leaf 는 `push {…, lr}` / `pop {…, pc}`.
4. `cmp` 는 부호를 모른다 — 조건이 정한다: LT/GT/LE/GE(부호) vs LO/HI/LS/HS(부호 없음).
5. IT: 1–4개, T/E 조건 일치, 분기는 마지막, `cbz` 금지. 상태는 xPSR 의 ITSTATE([26:25], [15:10])에 있고 명령마다 한 칸씩 밀린다.
6. 16비트 데이터처리 명령은 IT 밖에선 플래그를 쓰고(…s), IT 안에선 안 쓴다 → `add`(밖)·`adds<c>`(안)는 32비트. `cmp` 는 어디서나 플래그를 쓴다.
7. 조건부 비교 체인은 "건너뛴 경로의 플래그가 최종 조건을 거짓으로" 되게 순서를 정한다.
8. AArch64 는 IT 대신 `csel/ccmp`, ARMv8-A AArch32 는 복잡한 IT 를 폐기, Cortex-M0 은 IT·CBZ 가 없다.

## 참고자료

- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403E) — IT 명령, 32비트 Thumb 인코딩, xPSR/EPSR(ITSTATE), 예외 진입 시 xPSR 저장, 인터워킹 분기: https://developer.arm.com/documentation/ddi0403/latest
- Arm, *Procedure Call Standard for the Arm Architecture (AAPCS32)*: https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst
- Arm, *Cortex-M3 Technical Reference Manual* (r2p1) — 명령 타이밍, IT folding, CCR.STKALIGN: https://developer.arm.com/documentation/ddi0337/latest
- Arm, *Cortex-M3 Devices Generic User Guide* — IT, CBZ, 조건 코드 표, CFSR(UsageFault INVSTATE): https://developer.arm.com/documentation/dui0552/latest
- GNU as manual, ARM-dependent options (`-mimplicit-it`, `-mwarn-restrict-it`), `.thumb_func`, `.syntax`: https://sourceware.org/binutils/docs/as/ARM_002dDependent.html
- GCC ARM options (`-mrestrict-it`): https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html
- QEMU `mps2-an385` board: https://www.qemu.org/docs/master/system/arm/mps2.html
- 관련 레슨: BM-B08 (C startup·벡터 테이블·세미호스팅), ARM-B04 (주소 지정 모드), ARM-B05 (AArch64 조건 실행·플래그), ARM-B12 (Thumb-2 인코딩 길이), ARM-B14 (MSP/PSP·CONTROL), ARM-M08 (벡터 테이블과 Thumb 비트), ARM-M09 (EXC_RETURN), ARM-M11 (Cortex-M LDREX/STREX), ARM-M12 (HardFault 분석), ARM-A07 (포화 명령)
