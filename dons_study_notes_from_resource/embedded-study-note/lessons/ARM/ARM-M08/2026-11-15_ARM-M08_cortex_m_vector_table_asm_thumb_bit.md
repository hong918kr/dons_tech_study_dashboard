---
id: ARM-M08
time: 75분
updated: 2026-11-15
---
# ARM-M08 Cortex-M 벡터 테이블과 Thumb 비트

## 학습 목표

- Cortex-M3 벡터 테이블(초기 SP, Reset, NMI, HardFault, … SysTick, IRQ0–31)을 **어셈블리 `.word`** 로 직접 쓰고, `.isr_vector` 섹션·링커 스크립트로 주소 0 에 놓아 QEMU 에서 부팅한다.
- 왜 코드 엔트리는 전부 **홀수**(bit0 = Thumb)인지, `.type %function` / `.thumb_func` / `.thumb_set` 중 무엇이 링커에게 bit0 을 붙이게 하는지 `readelf`·`objdump -s` 실측으로 설명한다.
- `Reset_Handler` 를 어셈블리로 쓴다: `.data` 복사(ITTT 블록 + post-index), `.bss` 0, `bl main`.
- 테이블을 RAM 으로 옮기고(`VTOR` 정렬 규칙, `DSB`/`ISB`) 런타임에 SysTick 엔트리를 바꿔 끼워, 새 핸들러만 불리는 것을 증명한다.
- 엔트리의 Thumb 비트를 일부러 지워 **UsageFault(INVSTATE)** 를 만들고, UsageFault 켬/끔·우선순위에 따라 **HardFault(FORCED)** 로 승격되는 조건을 실제 레지스터 값으로 확인한 뒤, 쌓인 xPSR.T 를 고쳐 복구한다.

## 1. 문제

"벡터 테이블 엔트리가 짝수면 어떻게 돼?", "부트로더에서 앱으로 넘어갈 때 VTOR 은 왜 바꾸고, 정렬은 얼마여야 해?" — Cortex-M 펌웨어 면접(STM32·nRF52 부트로더, SSD 컨트롤러의 보조 Cortex-M 코어 bring-up)에서 시동 코드와 크래시 덤프를 읽을 수 있는지 보는 단골 질문이다. 테이블을 C 배열로 만들 때(BM-B08)는 컴파일러가 bit0 을 알아서 붙여 주지만, 벤더 `startup_*.s` 처럼 **어셈블리로 쓰면 지시어 하나 빠뜨린 것**이 그대로 짝수 엔트리 → 부팅 직후 HardFault 가 된다.

ARM-B13 에서 `blx` 로 짝수 주소를 불러 INVSTATE 를 한 번 봤고, BM-B08 은 리셋 벡터가 짝수일 때를 봤다. 이번에는 (1) 테이블 전체를 asm 으로 쓰고, (2) RAM 으로 재배치해 **런타임에 엔트리를 패치**하고, (3) 그 패치로 일부러 Thumb 비트를 지워 fault 가 어떤 경로로 어디에 기록되는지, 어떻게 되살리는지를 끝까지 따라간다.

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">벡터 테이블 두 벌 — 값은 이 레슨 solution.elf 실측 (objdump -s, nm)</text>
  <text x="10" y="40" fill="currentColor" font-weight="bold">FLASH 0x00000000 (.isr_vector, 192 B)</text>
  <rect x="10" y="48" width="330" height="22" class="f-mut" stroke="currentColor"/>
  <text x="16" y="63" fill="currentColor" font-family="monospace">[0]  0x00  0x20010000  _estack (데이터)</text>
  <rect x="10" y="70" width="330" height="22" class="f-ok" stroke="currentColor"/>
  <text x="16" y="85" fill="currentColor" font-family="monospace">[1]  0x04  0x00000abd  Reset_Handler</text>
  <rect x="10" y="92" width="330" height="22" class="f-ok" stroke="currentColor"/>
  <text x="16" y="107" fill="currentColor" font-family="monospace">[2]  0x08  0x00000b19  NMI → Default</text>
  <rect x="10" y="114" width="330" height="22" class="f-ok" stroke="currentColor"/>
  <text x="16" y="129" fill="currentColor" font-family="monospace">[3]  0x0c  0x00000b21  HardFault</text>
  <rect x="10" y="136" width="330" height="22" class="f-ok" stroke="currentColor"/>
  <text x="16" y="151" fill="currentColor" font-family="monospace">[6]  0x18  0x00000b21  UsageFault (같은 코드)</text>
  <rect x="10" y="158" width="330" height="22" class="f-mut" stroke="currentColor"/>
  <text x="16" y="173" fill="currentColor" font-family="monospace">[7..10], [13]  0       reserved</text>
  <rect x="10" y="180" width="330" height="22" class="f-ok" stroke="currentColor"/>
  <text x="16" y="195" fill="currentColor" font-family="monospace">[11] 0x2c  0x000000c1  SVC_Handler</text>
  <rect x="10" y="202" width="330" height="22" class="f-ok" stroke="currentColor"/>
  <text x="16" y="217" fill="currentColor" font-family="monospace">[15] 0x3c  0x000000d1  SysTick_Handler</text>
  <rect x="10" y="224" width="330" height="22" class="f-ok" stroke="currentColor"/>
  <text x="16" y="239" fill="currentColor" font-family="monospace">[22] 0x58  0x000000f1  Irq6_Handler (IRQ6)</text>
  <rect x="10" y="246" width="330" height="22" class="f-mut" stroke="currentColor"/>
  <text x="16" y="261" fill="currentColor" font-family="monospace">[16+n] 나머지 IRQ  0x00000b19</text>
  <text x="10" y="290" fill="currentColor">코드 엔트리 = 심볼 주소 | 1 (nm: SVC_Handler 0xc0 → 워드 0xc1)</text>
  <text x="10" y="308" fill="currentColor">리셋: MSP ← [0], PC ← [1] &amp; ~1, EPSR.T ← [1] &amp; 1</text>
  <text x="400" y="40" fill="currentColor" font-weight="bold">RAM 0x20000200 g_ram_vectors (256 B 정렬)</text>
  <rect x="400" y="48" width="330" height="22" class="f-mut" stroke="currentColor"/>
  <text x="406" y="63" fill="currentColor" font-family="monospace">[0..47] vtor_relocate 로 복사</text>
  <rect x="400" y="180" width="330" height="22" class="f-warn" stroke="currentColor"/>
  <text x="406" y="195" fill="currentColor" font-family="monospace">[11] 0x000000c0  ← Part B: bit0 지움!</text>
  <rect x="400" y="202" width="330" height="22" class="f-acc" stroke="currentColor"/>
  <text x="406" y="217" fill="currentColor" font-family="monospace">[15] 0x000000e1  SysTick_Handler2</text>
  <path d="M340 59 L400 59" stroke="currentColor" fill="none"/>
  <path d="M392 55 L400 59 L392 63" stroke="currentColor" fill="none"/>
  <text x="346" y="52" fill="currentColor" font-size="12">복사</text>
  <rect x="460" y="270" width="210" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="472" y="290" fill="currentColor" font-family="monospace">SCB-&gt;VTOR (0xE000ED08)</text>
  <text x="472" y="310" fill="currentColor" font-family="monospace">0x0 → 0x20000200</text>
  <path d="M560 270 L560 230" stroke="currentColor" fill="none"/>
  <path d="M556 238 L560 230 L564 238" stroke="currentColor" fill="none"/>
  <text x="10" y="350" fill="currentColor">예외 번호 n 의 핸들러 주소 = *(VTOR + 4n). VTOR 만 바꾸면 테이블 전체가 바뀐다.</text>
  <text x="10" y="370" fill="currentColor">FLASH 쪽은 실제 MCU 에서 런타임에 못 고친다 → 고치려면 RAM 으로 옮긴다 (부트로더, ISR 동적 설치).</text>
  <text x="10" y="390" fill="currentColor">QEMU mps2-an385 의 0x0 은 SSRAM1(RAM)이라 실기와 다를 수 있다 — 시뮬레이터 사정에 기대지 않는다 (BM-B08).</text>
</svg>
```

### Part A — 어셈블리 벡터 테이블과 Reset_Handler

`starter.S` 의 TODO 를 채운다.

- 테이블 48 워드(16 시스템 예외 + QEMU mps2-an385 의 IRQ 32 개)를 `.section .isr_vector,"a"` 에 `.word` 로. `[0] _estack`, `[1] Reset_Handler`, `[3] HardFault_Handler` 는 주어졌다. NMI·MemManage·BusFault·UsageFault·SVCall·DebugMon·PendSV·SysTick·IRQ0–31 을 채운다 (`Irq6_Handler` 는 IRQ6 자리, 나머지 IRQ 는 `Default_Handler`).
- 약한 기본 핸들러는 `.weak` + `.thumb_set` 로 이미 선언돼 있다. 강한 정의(harness.c 의 `SVC_Handler` 등)가 이긴다.
- `Reset_Handler`: `.data` 를 LMA(`_sidata`)에서 VMA(`_sdata`..`_edata`)로 복사, `.bss`(`_sbss`..`_ebss`) 0, `bl main`, `b sh_exit`.

테스트: VTOR = 0, 주소 0/4 의 워드 = `_estack` / `Reset_Handler|1`, 모든 핸들러 엔트리 ≠ 0 이고 홀수, `[11]`·`[15]`·`[22]` = C 에서 본 함수 포인터 값, `.data` 값, 그리고 실제로 `svc #0`·SysTick·IRQ6(소프트웨어 pend)가 테이블을 통해 각 핸들러를 부른다.

### Part B — Thumb 비트를 지우면: INVSTATE 와 승격

RAM 테이블의 SVCall 엔트리를 `SVC_Handler & ~1` 로 바꾸고 `svc #0`:

- B1: `SHCSR.USGFAULTENA = 1`, SVCall 우선순위 0x80 → **UsageFault**, `CFSR = 0x00020000`(INVSTATE).
- B2: `USGFAULTENA = 0` → **HardFault**, `HFSR = 0x40000000`(FORCED) + CFSR.INVSTATE.
- B3: `USGFAULTENA = 1` 인데 SVCall 우선순위 0 (UsageFault 와 같음) → 그래도 **HardFault**.
- B4: Thread 에서 `blx` 로 짝수 함수 주소 → UsageFault (ARM-B13 실수 4 의 재현 + 이번엔 복구까지).

fault 핸들러(주어진 asm 트램펄린 + C `fault_record`)가 IPSR·CFSR·HFSR·쌓인 pc/lr/xPSR·EXC_RETURN 을 기록하고, **쌓인 xPSR 의 T 비트를 1 로 고쳐** 복귀한다 → 원래 핸들러/함수가 정상 실행된다. 테스트는 그 "실행됐다" 까지 확인한다.

### Part C — RAM 으로 재배치하고 런타임 패치

`void vtor_relocate(uint32_t *dst, const uint32_t *src, uint32_t nwords)` — 복사 → `DSB` → `VTOR = dst` → `DSB` → `ISB`.
`void vector_patch(uint32_t *table, uint32_t exc_num, uint32_t handler)` — `table[n] = handler` + `DSB`.
테스트: VTOR 읽기 = RAM 주소, 두 테이블 동일, SysTick 엔트리를 `SysTick_Handler2` 로 바꾼 뒤 SysTick 3 번 → `tick2 = 3`, 원래 핸들러의 `tick1` 은 그대로. 또 `VTOR = base | 0x7F` 를 쓰면 아래 7 비트가 무시되는지.

| 파일 | 역할 |
|---|---|
| `harness.c` | 펌웨어 테스트 (starter/solution 공용), C 핸들러, `fault_record` |
| `starter.S` / `solution.S` | 벡터 테이블 + Reset_Handler + `vtor_relocate` + `vector_patch` (TODO / 정답) + 주어진 asm (Default_Handler, fault 트램펄린, peek32, call_blx, bx_target) |
| `shared_startup.c` | 세미호스팅 출력·종료, CHECK 카운터 (ARM-B14 축약판) |
| `arm_m08.ld` | 링커 스크립트: `.isr_vector` 를 0x0 에, 크기 48 워드 `ASSERT` |
| `run_qemu.sh` | VM 안에서 빌드·실행: `starter|solution|practice|gdb|objdump|thumbfunc` |
| `demo_thumbfunc.S` | 지시어별 심볼 bit0 실험 |
| `demo_vectors.gdb` / `demo_vectors_out.txt` | 리셋 순간과 INVSTATE 진입 gdb 세션, 실제 출력 |
| `practice.c` / `practice.S` | 추가 연습 답 |

## 2. Problem (English)

> **Vector table in assembly + the Thumb bit.** Target: Cortex-M3 (ARMv7-M), QEMU `mps2-an385` (32 external IRQs), bare metal, semihosting output.
>
> 1. In a `.S` file, emit a 48-word vector table in section `.isr_vector` (linked at address 0): initial MSP, Reset, NMI, HardFault, MemManage, BusFault, UsageFault, SVCall, DebugMonitor, PendSV, SysTick, IRQ0–31. Unused handlers must be weak aliases of `Default_Handler`. Every code entry must have bit 0 set — explain which assembler directive guarantees that.
> 2. Write `Reset_Handler` in assembly: copy `.data` from its load address, zero `.bss`, call `main`, pass its return value to `sh_exit`.
> 3. Implement `void vtor_relocate(uint32_t *dst, const uint32_t *src, uint32_t nwords)` and `void vector_patch(uint32_t *table, uint32_t exc_num, uint32_t handler)`. What alignment must `dst` have for 48 entries, and which barriers are needed after writing `VTOR`?
> 4. Clear bit 0 of the SVCall entry in the RAM table and execute `svc #0`. Predict and verify IPSR, CFSR, HFSR, the stacked PC and xPSR, and EXC_RETURN for: UsageFault enabled with SVCall priority 0x80; UsageFault disabled; UsageFault enabled with SVCall priority 0. Recover so that `SVC_Handler` still runs.
>
> Example (real run): `vec[11] = 0x000000c1` (`nm`: `SVC_Handler` at `0xc0`); with the bit cleared → `ipsr = 6, cfsr = 0x00020000, stacked pc = 0x000000c0, stacked xpsr = 0x2000000b (T = 0, IPSR field = 11), exc_return = 0xfffffff1`.
>
> Follow-ups: Why is entry 0 not odd? Why can a Cortex-M handler be a plain C function? What does a bootloader do with VTOR and MSP before jumping to the application? Does `mov pc, r0` with an even address fault?

## 3. 시작 코드

테스트 하네스 (starter 와 solution 이 같이 쓴다):

<!-- code: harness.c -->

TODO 가 있는 어셈블리 (테이블 엔트리는 `.word 0`, 루프 자리는 `nop` — 부팅은 되고, 망가진 엔트리로 예외를 일으키지 않도록 하네스가 먼저 엔트리를 검사한다):

<!-- code: starter.S -->

공용 런타임과 링커 스크립트:

<!-- code: shared_startup.c -->

<!-- code: arm_m08.ld -->

빌드·실행은 Lima VM `elinux` 안에서 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1). Mac 홈은 VM 에서 읽기 전용이라 스크립트가 `/tmp/ARM-M08_work` 로 복사해 빌드한다. 맥에서 한 줄:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ARM/ARM-M08
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # solution / practice / gdb / objdump / thumbfunc
```

<!-- code: run_qemu.sh -->

실제로 도는 명령 (starter 예):

```bash
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib \
    -fno-tree-loop-distribute-patterns -T arm_m08.ld harness.c starter.S shared_startup.c -o starter.elf -lgcc
timeout 20 qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none \
    -semihosting-config enable=on,target=native -kernel starter.elf
```

starter 실행 결과 (실제 출력, 뒷부분 FAIL 줄 일부 생략):

```text
ARM-M08 vector table in asm + Thumb bit (QEMU mps2-an385)
[A] boot table
PASS line 123: SCB_VTOR == 0u
PASS line 124: peek32(0u) == FN(_estack)
PASS line 125: peek32(4u) == FN(Reset_Handler) && (peek32(4u) & 1u)
  bad entries: 40, first = [2]
FAIL line 132: bad == 0u
FAIL line 133: g_vectors[EXC_SVC] == FN(SVC_Handler)
...
FAIL line 139: g_data_magic == 0xC0FFEE08u
PASS line 142: nz == 0u
FAIL line 145: g_svc_n == 1u
...
FAIL line 160: SCB_VTOR == base
...
PASS line 179: vtor_rb == base
[B] clear the Thumb bit
FAIL line 197: g_f.ipsr == 6u
...
5/35 passed
qemu exit=1
```

starter 에서 PASS 하는 5 개는 주어진 부분(VTOR 리셋값, `[0]`, `[1]`)과 하드웨어 자체 검사(VTOR 아래 7 비트 무시), 그리고 **QEMU RAM 이 원래 0 이라 통과해 버리는 `.bss` 검사**다. 마지막 것은 약한 검사라는 걸 기억하자 — 진짜 SRAM 은 전원 투입 시 쓰레기 값이다 (BM-B08 의 nopoison 실험).

VM 없이 맥에서 "어셈블은 되나?" 만 확인 (Apple clang 은 ARM ELF 오브젝트도 만든다):

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
clang --target=thumbv7m-none-eabi -c solution.S -o /tmp/ARM-M08_solution.o && /usr/bin/objdump -d /tmp/ARM-M08_solution.o
```

```text
00000000 <Reset_Handler>:
       0: 4923         	ldr	r1, [pc, #0x8c]         @ 0x90 <bx_target+0xc>
       ...
       6: 429a         	cmp	r2, r3
       8: bf3e         	ittt	lo
       a: f851 0b04    	ldrlo	r0, [r1], #4
       e: f842 0b04    	strlo	r0, [r2], #4
      12: e7f8         	blo	0x6 <Reset_Handler+0x6> @ imm = #-0x10
```

(오브젝트 단계라 `.isr_vector` 의 워드는 아직 0 이고 재배치(relocation) 항목만 있다 — bit0 은 **링커**가 넣는다.)

## 4. 힌트

<details>
<summary>힌트 1 — 엔트리 하나는 그냥 .word 심볼</summary>

`.word SVC_Handler` 라고만 쓰면 된다. 심볼이 Thumb 함수(`STT_FUNC` + Thumb 표시)이면 링커가 `R_ARM_ABS32` 재배치를 풀 때 `주소 | 1` 을 넣는다. C 함수는 컴파일러가 이미 `.type %function` + `.thumb_func` 를 붙여 두었다. 같은 핸들러를 여러 번 쓰려면 `.rept 25` / `.word Default_Handler` / `.endr`.

</details>

<details>
<summary>힌트 2 — Reset_Handler 의 복사 루프</summary>

`r1 = _sidata, r2 = _sdata, r3 = _edata` 를 `ldr rX, =sym` 으로 담고, `cmp r2, r3` → `ittt lo` → `ldrlo r0, [r1], #4` / `strlo r0, [r2], #4` / `blo 1b`. 크기가 0 이어도 첫 비교에서 빠져나간다. `.bss` 는 r0 = 0 으로 `itt lo` / `strlo` / `blo`. 부호 없는 비교(`lo`)인 이유: 주소다 (ARM-B05 의 HI vs GT).

</details>

<details>
<summary>힌트 3 — VTOR 을 바꾸는 순서</summary>

새 테이블을 다 쓴 다음에 VTOR 을 바꿔야 한다. 그 사이에 예외가 오면 반쯤 빈 테이블을 읽는다. 쓰기 완료를 보장하는 것은 `DSB`, 뒤 명령이 새 상태를 보게 하는 것은 `ISB` (ARM-M05). VTOR 주소 0xE000ED08 은 `ldr r3, =0xE000ED08` 로.

</details>

<details>
<summary>힌트 4 — 짝수 엔트리로 들어간 핸들러를 어떻게 살리나</summary>

예외 진입은 이미 끝났고, 실패한 것은 "T = 0 상태로 첫 명령을 실행" 이다. fault 의 쌓인 프레임에는 그 핸들러 주소(pc)와 T = 0 인 xPSR 이 있다. 엔트리를 고쳐도 지금 프레임은 그대로다 — 쌓인 xPSR(frame[7])의 bit24 를 1 로 만들고 복귀하면, 같은 pc 를 이번엔 Thumb 로 실행한다.

</details>

## 5. 풀이

### 0. 규칙 정리 — 어디에 쓰여 있나

| 규칙 | 근거 (확인 권장 표시는 기억에 의존한 부분) |
|---|---|
| 예외 번호 n 의 엔트리 = `VTOR + 4n`. 0 = 초기 SP, 1 = Reset, 2 NMI, 3 HardFault, 4 MemManage, 5 BusFault, 6 UsageFault, 11 SVCall, 12 DebugMon, 14 PendSV, 15 SysTick, 16+n = IRQn | ARMv7-M ARM B1.5.2–B1.5.3, Cortex-M3 Generic User Guide 2.3.4 |
| 진입 시 `PC = entry & ~1`, `EPSR.T = entry & 1` | ARMv7-M ARM ExceptionTaken 의사코드 (B1.5.6) — 확인 권장 |
| T = 0 으로 명령을 실행하려 하면 UsageFault, `UFSR.INVSTATE`(CFSR bit17) | ARMv7-M ARM B3.2.15, Generic UG 4.3.10. 실측 CFSR 0x00020000 |
| 리셋: `SP_main = [0] & ~3` (아래 2 비트 0) | ARMv7-M ARM TakeReset 의사코드 — 확인 권장 |
| UsageFault 가 꺼져 있거나 **현재 실행 우선순위보다 높지 않으면** HardFault 로 승격, `HFSR.FORCED`(bit30) | ARMv7-M ARM B1.5.4 (priority escalation). 실측 B2·B3 |
| VTOR 정렬 = (엔트리 수 × 4) 를 2 의 거듭제곱으로 올림, 최소 128 B | ARMv7-M ARM B3.2.5 VTOR — 확인 권장. Cortex-M3 r2p0 Generic UG: TBLOFF = [29:7] |
| VTOR 변경 후 DSB(+ISB) | Arm AN321 (memory barrier 가이드) 의 VTOR 항목 — 확인 권장 |

### Part A — 테이블을 asm 으로, 그리고 bit0 은 누가 붙이나

테이블은 그냥 **데이터**다. 그래서 섹션 플래그가 `"a"`(alloc) 뿐이고 `x`(exec)가 없다. 링커 스크립트가 `KEEP(*(.isr_vector))` 로 주소 0 에 고정한다 (KEEP 이 없으면 `--gc-sections` 에서 아무도 참조하지 않는 테이블이 지워질 수 있다). `ASSERT(SIZEOF(.isr_vector) == 48 * 4)` 는 엔트리 하나 빠뜨려서 뒤 IRQ 들이 한 칸씩 밀리는 실수를 링크 단계에서 잡는다.

`run_qemu.sh objdump` 실측 — 테이블의 실제 바이트(리틀 엔디언)와 심볼:

```text
  0 .isr_vector   000000c0  00000000  00000000  00010000  2**2
  1 .text         0000139c  000000c0  000000c0  000100c0  2**2
  2 .data         00000004  20000000  0000145c  00020000  2**2
  3 .bss          000001cc  20000100  00001460  00020100  2**8
Contents of section .isr_vector:
 0000 00000120 bd0a0000 190b0000 210b0000  ... ........!...
 0010 190b0000 190b0000 210b0000 00000000  ........!.......
 0020 00000000 00000000 00000000 c1000000  ................
 0030 190b0000 00000000 190b0000 d1000000  ................
 0040 190b0000 190b0000 190b0000 190b0000  ................
 0050 190b0000 190b0000 f1000000 190b0000  ................
 ...
00000000 R g_vectors
000000c0 T SVC_Handler
000000d0 T SysTick_Handler
000000e0 T SysTick_Handler2
000000f0 T Irq6_Handler
00000abc T Reset_Handler
00000b18 T Default_Handler
00000b18 W NMI_Handler
00000b20 T HardFault_Handler
00000b20 T UsageFault_Handler
00000b3c T call_blx_ret
00000b40 T bx_target
20000200 B g_ram_vectors
    71: 00000b19     8 FUNC    WEAK   DEFAULT    2 NMI_Handler
    82: 00000abd    44 FUNC    GLOBAL DEFAULT    2 Reset_Handler
    83: 00000000   192 OBJECT  GLOBAL DEFAULT    1 g_vectors
    97: 000000c1    16 FUNC    GLOBAL DEFAULT    2 SVC_Handler
   105: 00000b3c     0 NOTYPE  GLOBAL DEFAULT    2 call_blx_ret
```

읽는 법: `bd0a0000` = 0x00000abd = `Reset_Handler`(nm 0xabc) | 1. `nm` 은 bit0 을 뗀 **명령 주소**(짝수)를 보여주고, `readelf -s` 는 ELF `st_value` 에 bit0 이 붙은 값(0xabd)을 보여준다. 둘 다 맞다 — 같은 정보를 다르게 표현할 뿐. `[0]` 0x20010000 은 SP 값이라 홀수가 아니다. `.bss` 가 2**8 정렬인 것은 `g_ram_vectors` 의 `aligned(256)` 때문이다.

어떤 지시어가 bit0 을 만드나 — `run_qemu.sh thumbfunc` (demo_thumbfunc.S 를 0x100 에 링크) 실측:

| 라벨 정의 | ELF 심볼 | `.word` 결과 | 벡터로 쓰면 |
|---|---|---|---|
| 지시어 없음 (`lbl_plain:`) | `NOTYPE 0x100` | 0x00000100 | INVSTATE |
| `.type x, %function` 만 | `FUNC 0x105` | 0x00000105 | OK |
| `.thumb_func` 만 | `FUNC 0x109` | 0x00000109 | OK |
| 둘 다 (이 레슨 규칙) | `FUNC 0x10d` | 0x0000010d | OK |
| `.thumb_set y, lbl_plain` | `FUNC 0x101` | 0x00000101 | OK (별칭에 Thumb 표시) |
| `.set y, lbl_plain` | `NOTYPE 0x100` | 0x00000100 | INVSTATE |

<!-- code: demo_thumbfunc.S -->

GNU as 2.4x(arm-none-eabi 14.2.1 동봉)에서는 `.thumb` 모드의 `.type %function` 만으로도 Thumb 함수가 된다. 하지만 오래된 도구·다른 어셈블러와의 호환을 위해 CMSIS `startup_*.s` 처럼 **둘 다** 쓰는 것이 안전하다. 그리고 `.set` 별칭은 원본이 Thumb 함수여도 표시가 따라오지 않는다 → 약한 기본 핸들러는 반드시 `.thumb_set`.

**Reset_Handler 한 줄씩** (이 빌드: `_sidata = 0x145c`, `.data` = 0x20000000–0x20000004 (1 워드), `.bss` = 0x20000100–0x200002cc (115 워드)):

| pc | 명령 | r1 | r2 | r3 | 메모 |
|---|---|---|---|---|---|
| 0xabc | `ldr r1, =_sidata` | 0x145c | – | – | 리터럴 풀에서 |
| 0xabe–0xac0 | `ldr r2/r3` | 0x145c | 0x20000000 | 0x20000004 | |
| 0xac2 | `cmp r2, r3` | | | | C = 0 (빌림) → `lo` 참 |
| 0xac4 | `ittt cc` | | | | objdump 는 `lo` 를 `cc` 로 표기 (같은 조건) |
| 0xac6/0xaca | `ldrcc.w` / `strcc.w` | 0x1460 | 0x20000004 | | `g_data_magic = 0xC0FFEE08` 복사 |
| 0xace | `bcc.n ac2` | | | | 다시 비교 → C = 1, IT 3 명령 모두 건너뜀 |
| 0xad0–0xade | `.bss` 루프 | | 0x20000100→0x200002cc | 0x200002cc | 115 회 |
| 0xae0 | `bl main` | | | | r0 = 0 (전부 PASS) |
| 0xae4 | `b.w sh_exit` | | | | SYS_EXIT(0) |

ITTT 블록 안의 `ldr`/`str` 은 post-index 형식이라 32 비트(`.w`)가 됐다 — post-index 는 32 비트 인코딩만 있다 (ARM-B13). 블록 마지막의 `blo` 는 16 비트 `bcc.n`. `bl main` 다음 `b sh_exit` 는 `bl` 이 아니라 `b` — 돌아올 필요가 없고, r0(main 의 반환값)이 그대로 `sh_exit(int)` 의 인자가 된다.

**핸들러가 그냥 C 함수인 이유.** `SVC_Handler` 의 디스어셈블(0xc0)은 `ldr/ldr/adds/str/bx lr` 다섯 명령으로 평범한 함수다. Cortex-M 은 예외 진입 때 하드웨어가 caller-saved 레지스터(r0–r3, r12, lr, pc, xPSR)를 쌓고 lr 에 EXC_RETURN 을 넣으므로, AAPCS 를 지키는 함수가 `bx lr` 하면 그게 예외 복귀다. ARM7TDMI·Cortex-A 처럼 `__attribute__((interrupt))` 나 asm 래퍼가 필요 없다 — Cortex-M 설계의 핵심 장점이다 (프레임 해부는 ARM-M09).

### Part C — VTOR 재배치

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">VTOR (0xE000ED08) — Cortex-M3 r2p0 Generic UG 기준 비트 배치</text>
  <rect x="10" y="34" width="70" height="30" class="f-mut" stroke="currentColor"/>
  <text x="18" y="54" fill="currentColor" font-family="monospace">31:30</text>
  <rect x="80" y="34" width="60" height="30" class="f-warn" stroke="currentColor"/>
  <text x="96" y="54" fill="currentColor" font-family="monospace">29</text>
  <rect x="140" y="34" width="420" height="30" class="f-acc" stroke="currentColor"/>
  <text x="270" y="54" fill="currentColor" font-family="monospace">TBLOFF [29:7]</text>
  <rect x="560" y="34" width="170" height="30" class="f-mut" stroke="currentColor"/>
  <text x="580" y="54" fill="currentColor" font-family="monospace">[6:0] 0 (무시)</text>
  <text x="10" y="84" fill="currentColor">예약</text>
  <text x="76" y="84" fill="currentColor">0 코드 / 1 SRAM 영역 ("TBLBASE")</text>
  <text x="560" y="84" fill="currentColor">→ 최소 128 B 정렬</text>
  <text x="10" y="116" fill="currentColor" font-family="monospace">실측: VTOR = 0x20000200 | 0x7F  →  읽으면 0x20000200</text>
  <text x="10" y="140" fill="currentColor">정렬 = (16 + IRQ 수) × 4 를 2 의 거듭제곱으로 올림: 이 레슨 48 워드 = 192 B → 256 B</text>
  <text x="10" y="160" fill="currentColor">STM32F407(82 IRQ) 98 워드 = 392 B → 512 B, IRQ 240 개(M3 최대) → 1024 B</text>
  <text x="10" y="184" fill="currentColor">ARMv7-M ARM / Cortex-M4 문서는 TBLOFF = [31:7] 로 적는다 — 칩 문서 확인</text>
</svg>
```

`vtor_relocate` 는 Part A 와 같은 post-index 복사 루프 + VTOR 쓰기다. 순서가 핵심이다:

1. **복사를 끝까지** 한다. 도중에 VTOR 을 바꾸면, 그 사이 들어온 예외가 반쯤 빈 테이블(0 엔트리 → PC = 0, T = 0 → INVSTATE)을 읽는다.
2. `DSB` — 테이블 쓰기가 메모리 시스템에 도달한 뒤에 VTOR 쓰기가 일어나게.
3. `str` VTOR.
4. `DSB` + `ISB` — VTOR 쓰기 완료, 뒤 명령(예: 바로 `cpsie i` 나 SysTick 시작)이 새 테이블 기준으로 예외를 받게. Cortex-M3 는 캐시도 쓰기 버퍼 재정렬도 거의 없어서 QEMU·실기 모두 배리어 없이도 대개 동작하지만, Cortex-M7(캐시·AXI)에서는 실제로 필요하다 — CMSIS 도 같은 이유로 넣는다 (확인 권장).

정렬이 왜 필요한가: 하드웨어가 "VTOR + 4n" 을 덧셈기 대신 **비트 연결(concatenation)** 로 만들 수 있게 하려는 것이다. 정렬이 모자라면 큰 번호 IRQ 의 엔트리 주소가 엉뚱하게 계산될 수 있다(UNPREDICTABLE). QEMU 는 그냥 더하므로 이 버그를 보여주지 않는다 — 그래서 practice 의 `vtor_ok()` 같은 정적 검사로 잡는다.

실측 (solution):

```text
[C] relocate to RAM
  g_ram_vectors = 0x20000200
PASS line 160: SCB_VTOR == base
PASS line 163: diff == 0u
PASS line 165: g_ram_vectors[EXC_SYSTICK] == FN(SysTick_Handler2)
PASS line 166: g_vectors[EXC_SYSTICK] == FN(SysTick_Handler)
PASS line 170: g_tick2 >= 3u && g_tick1 == t1_before
  tick1 (flash handler) = 0x00000003
  tick2 (patched RAM handler) = 0x00000003
  VTOR after writing base|0x7F = 0x20000200
PASS line 179: vtor_rb == base
```

`tick1 = 3` 은 Part A 에서 FLASH 테이블로 돌린 3 번이고, 재배치 뒤에는 늘지 않았다. 실무 연결: 부트로더 → 앱 점프(BM-A03: 앱 테이블 주소로 VTOR, `MSP = app[0]`, `app[1]` 로 점프), `SystemInit` 의 `USER_VECT_TAB_ADDRESS`(BM-A01), 플래시 지우는 동안 RAM 에서 ISR 돌리기, RTOS 의 `NVIC_SetVector`.

### Part B — 짝수 엔트리가 만드는 fault, 그리고 승격

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">svc #0 → RAM vector[11] = 0x000000c0 (bit0 = 0) — 실측 경로</text>
  <rect x="10" y="32" width="160" height="40" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="20" y="50" fill="currentColor">Thread, MSP</text>
  <text x="20" y="66" fill="currentColor" font-family="monospace">svc #0</text>
  <path d="M170 52 L210 52" stroke="currentColor" fill="none"/>
  <path d="M202 48 L210 52 L202 56" stroke="currentColor" fill="none"/>
  <rect x="210" y="32" width="220" height="40" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="220" y="50" fill="currentColor">SVCall 진입 성공 (프레임 쌓음)</text>
  <text x="220" y="66" fill="currentColor" font-family="monospace">pc=0xc0  T=0  IPSR=11</text>
  <path d="M430 52 L470 52" stroke="currentColor" fill="none"/>
  <path d="M462 48 L470 52 L462 56" stroke="currentColor" fill="none"/>
  <rect x="470" y="32" width="260" height="40" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="480" y="50" fill="currentColor">첫 명령 실행 시도 → UsageFault</text>
  <text x="480" y="66" fill="currentColor" font-family="monospace">CFSR = 0x00020000 (INVSTATE)</text>
  <path d="M600 72 L600 100" stroke="currentColor" fill="none"/>
  <path d="M596 92 L600 100 L604 92" stroke="currentColor" fill="none"/>
  <rect x="380" y="100" width="350" height="56" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="390" y="120" fill="currentColor">USGFAULTENA = 1 이고</text>
  <text x="390" y="138" fill="currentColor">UsageFault 우선순위(0) &lt; 실행 우선순위?</text>
  <text x="390" y="152" fill="currentColor" font-size="12">(숫자 작을수록 급함: SVC 0x80 → 예, 0 → 아니오)</text>
  <path d="M470 156 L300 196" stroke="currentColor" fill="none"/>
  <path d="M310 188 L300 196 L311 197" stroke="currentColor" fill="none"/>
  <text x="330" y="182" fill="currentColor">예 (B1, B4)</text>
  <path d="M640 156 L640 196" stroke="currentColor" fill="none"/>
  <path d="M636 188 L640 196 L644 188" stroke="currentColor" fill="none"/>
  <text x="540" y="182" fill="currentColor">아니오 (B2, B3)</text>
  <rect x="120" y="196" width="260" height="56" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="130" y="216" fill="currentColor">UsageFault_Handler, IPSR = 6</text>
  <text x="130" y="234" fill="currentColor" font-family="monospace">HFSR = 0</text>
  <rect x="460" y="196" width="270" height="56" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="470" y="216" fill="currentColor">HardFault_Handler, IPSR = 3</text>
  <text x="470" y="234" fill="currentColor" font-family="monospace">HFSR = 0x40000000 (FORCED)</text>
  <path d="M250 252 L330 290" stroke="currentColor" fill="none"/>
  <path d="M590 252 L480 290" stroke="currentColor" fill="none"/>
  <rect x="160" y="290" width="440" height="58" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="170" y="310" fill="currentColor">fault_record: 쌓인 프레임 pc = 0xc0, xPSR = 0x2000000b (T = 0, IPSR 11)</text>
  <text x="170" y="328" fill="currentColor">복구: frame[7] |= 1 &lt;&lt; 24, CFSR/HFSR W1C → bx lr (EXC_RETURN 0xFFFFFFF1)</text>
  <text x="170" y="344" fill="currentColor">→ 0xc0 을 Thumb 로 다시 실행 = SVC_Handler 정상 수행 (g_svc_n + 1)</text>
</svg>
```

실측 4 가지 (solution 출력에서):

| 케이스 | USGFAULTENA | 끊긴 곳 / 우선순위 | 받은 핸들러 (IPSR) | CFSR | HFSR | 쌓인 pc | 쌓인 xPSR | EXC_RETURN |
|---|---|---|---|---|---|---|---|---|
| B1 | 1 | SVC 핸들러, SVC 0x80 | UsageFault (6) | 0x00020000 | 0 | 0x000000c0 | 0x2000000b | 0xfffffff1 |
| B2 | 0 | SVC 핸들러, SVC 0x80 | HardFault (3) | 0x00020000 | 0x40000000 | 0x000000c0 | 0x2000000b | 0xfffffff1 |
| B3 | 1 | SVC 핸들러, SVC **0** | HardFault (3) | 0x00020000 | 0x40000000 | 0x000000c0 | 0x2000000b | 0xfffffff1 |
| B4 | 1 | Thread, `blx 0xb40` | UsageFault (6) | 0x00020000 | 0 | 0x00000b40 | 0x20000000 | 0xfffffff9 |

해석:

- **쌓인 pc = 짝수 핸들러 주소 그 자체** (0xc0 = `SVC_Handler`). 예외 진입은 성공했고, 실패한 것은 그 주소의 첫 명령이다. 크래시 덤프에서 "pc 가 어떤 핸들러의 첫 주소이고 xPSR bit24 = 0" 이면 → 벡터 엔트리의 bit0 을 의심.
- 쌓인 xPSR 의 IPSR 필드 = 11: fault 가 SVC 핸들러 **안에서** 났다는 뜻. 그래서 EXC_RETURN 이 0xFFFFFFF1(Handler 로 복귀)이다. B4 는 Thread 에서 났으므로 0xFFFFFFF9.
- **B3 가 핵심 함정**: UsageFault 를 켰어도, SVCall 과 UsageFault 의 우선순위가 둘 다 리셋값 0 이면 UsageFault 가 SVC 핸들러를 **선점할 수 없다**. 동기 fault 는 기다릴 수 없으므로 HardFault(-1)로 승격된다. "UsageFault 켰는데 왜 HardFault 로 와요?" 의 흔한 원인. 이 레슨 B1 은 `SHPR2` 로 SVCall 을 0x80 으로 내려서 해결했다. (QEMU 10.2 의 mps2-an385 는 우선순위 8 비트를 구현; 실제 AN385 M3 는 3 비트 — 0x80 은 둘 다에서 유효한 값.)
- B4 의 쌓인 lr = 0xb3d = `call_blx_ret`(0xb3c) | 1: `blx` 가 남긴 복귀 주소. 이것으로 "누가 짝수 포인터로 불렀나" 를 찾는다. 함수 포인터 테이블·콜백 등록에서 자주 나온다.

**복구 방법 고르기.** 시도할 수 있는 것은 세 가지다:

| 방법 | 결과 |
|---|---|
| 엔트리만 고치고 복귀 | 현재 프레임의 T = 0 은 그대로 → 복귀하자마자 다시 INVSTATE (무한 반복) |
| 쌓인 pc 를 다른 곳(예: lr)으로 | SVC 핸들러를 건너뜀 — 결과가 사라진다 |
| **쌓인 xPSR.T = 1** (이 레슨) | 같은 주소를 Thumb 로 실행 → 원래 하려던 일이 완료됨 |

복구는 **테스트 하네스용** 기술이다. 제품에서는 INVSTATE = 메모리 손상·잘못된 포인터의 증상이므로 기록하고 안전 상태로 리셋하는 것이 맞다 (BM-M13 의 fault 정책, ARM-M12 의 분석기).

gdb 로 본 같은 장면 (`run_qemu.sh gdb`, demo_vectors_out.txt 에서 발췌):

<!-- code: demo_vectors.gdb -->

```text
=== (1) reset: 첫 명령 직전 ===
  vector[0]=0x20010000  vector[1]=0x00000abd
  pc=0x00000abc  msp=0x20010000  sp=0x20010000  xpsr=0x41000000  (T = bit24 = 1)
Reset_Handler in section .text

=== (2) B1: SVC 엔트리 bit0 = 0 → UsageFault_Handler(= HardFault_Handler) 첫 명령 ===
Breakpoint 1, HardFault_Handler () at solution.S:132
  ipsr=6  lr(EXC_RETURN)=0xfffffff1  CFSR=0x00020000  VTOR=0x20000200
  RAM vector[11]=0x000000c0
stacked frame (r0 r1 r2 r3 r12 lr pc xpsr):
0x2000ffa0:	0x20000200	0x0000000b	0x000000c0	0x00000001
0x2000ffb0:	0x00000007	0xfffffff9	0x000000c0	0x2000000b
SVC_Handler in section .text
...
  B4 ipsr=6  lr(EXC_RETURN)=0xfffffff9
0x2000ffb8:	0x00000b40	0x0000000b	0x000000c1	0x00000001
0x2000ffc8:	0x00000007	0x00000b3d	0x00000b40	0x20000000
bx_target in section .text
call_blx + 5 in section .text
```

리셋 순간: 하드웨어가 이미 `msp = vector[0]`, `pc = vector[1] & ~1 = 0xabc`, xPSR bit24(T) = 1 로 만들어 놓았다 — 첫 명령 전에 스택이 있으므로 Reset_Handler 를 C 로도 쓸 수 있는 것이다 (BM-B08). B1 프레임의 쌓인 r0 = 0x20000200, r1 = 11, r2 = 0xc0 은 `vector_patch(table, 11, 0xc0)` 의 인자가 그대로 남은 것이다 (svc 직전 마지막 호출). 쌓인 lr 0xfffffff9 는 SVC 핸들러 진입 때의 EXC_RETURN — 즉 "SVC 핸들러의 lr" 이 그대로 쌓였다.

## 6. 정답 코드

<!-- code: solution.S diff=starter.S -->

solution 실행 결과 (실제 출력, 중간 PASS 줄 일부 생략):

```text
ARM-M08 vector table in asm + Thumb bit (QEMU mps2-an385)
[A] boot table
PASS line 123: SCB_VTOR == 0u
...
  vec[11] SVC_Handler = 0x000000c1
  vec[15] SysTick_Handler = 0x000000d1
PASS line 139: g_data_magic == 0xC0FFEE08u
PASS line 142: nz == 0u
PASS line 145: g_svc_n == 1u
PASS line 147: g_tick1 >= 3u
PASS line 153: g_irq6_n == 1u
[C] relocate to RAM
...
[B] clear the Thumb bit
  B1 (USGFAULTENA=1, SVC prio 0x80):
  ipsr = 0x00000006
  cfsr = 0x00020000
  hfsr = 0x00000000
  stacked pc = 0x000000c0
  stacked lr = 0xfffffff9
  stacked xpsr = 0x2000000b
  exc_return = 0xfffffff1
PASS line 197: g_f.ipsr == 6u
...
  B4 (blx even, thread):
  ipsr = 0x00000006
  cfsr = 0x00020000
  hfsr = 0x00000000
  stacked pc = 0x00000b40
  stacked lr = 0x00000b3d
  stacked xpsr = 0x20000000
  exc_return = 0xfffffff9
...
PASS line 240: g_bx_ran == 1u
PASS line 242: active_table()[EXC_SVC] == FN(SVC_Handler)
35/35 passed
qemu exit=0
```

## 7. 자주 하는 실수

**실수 1 — asm 핸들러에 `.type`/`.thumb_func` 를 안 붙였다.** 라벨이 `NOTYPE` 이 되어 `.word` 에 짝수가 들어간다 (위 표의 `lbl_plain`: 0x100). 리셋 벡터면 첫 명령에서 FORCED HardFault (BM-B08 실측), 다른 엔트리면 그 예외가 처음 올 때 터진다 — 즉 **몇 시간 뒤 첫 인터럽트에서** 터질 수 있다. 고친 코드: 모든 핸들러에

```asm
    .type SysTick_Handler, %function
    .thumb_func
SysTick_Handler:
```

그리고 링크 후 `arm-none-eabi-objdump -s -j .isr_vector` 로 [0] 을 뺀 모든 0 아닌 워드의 마지막 니블이 홀수(1,3,5,…,f)인지 본다 (practice 의 `vec_audit` 가 같은 검사).

**실수 2 — 약한 별칭을 `.set` 으로.** `.weak NMI_Handler` + `.set NMI_Handler, Default_Handler` 는 Default_Handler 가 Thumb 함수여도 NOTYPE 짝수가 된다 (실측 `lbl_set` 0x100). 고친 코드: `.thumb_set NMI_Handler, Default_Handler`. C 라면 `__attribute__((weak, alias("Default_Handler")))` 가 알아서 처리한다.

**실수 3 — 엔트리 하나를 빠뜨려 IRQ 가 한 칸씩 밀림.** reserved 칸 `[7..10]`, `[13]` 의 `.word 0` 을 하나 빼먹으면 SVCall 자리에 DebugMon 이 들어가고 IRQ0 이 15 번으로 간다. 증상은 "엉뚱한 핸들러가 불린다" 라서 찾기 어렵다. 고친 코드: 링커 스크립트에 `ASSERT(SIZEOF(.isr_vector) == 48 * 4, ...)` (이 레슨), 또는 C 쪽에서 `_Static_assert`.

**실수 4 — 테이블 복사 전에 VTOR 을 바꾸거나, 정렬 안 된 배열을 쓴다.** `uint32_t ram_vec[48];` (4 바이트 정렬)에 VTOR 을 쓰면 아래 7 비트가 잘려(실측: `base|0x7F` → `base`) 엉뚱한 주소를 가리키거나, 정렬이 128 은 맞는데 256 이 아니면 큰 번호 IRQ 가 잘못 계산될 수 있다. 고친 코드: `__attribute__((aligned(256)))` (또는 링커 스크립트에 전용 섹션 + `ALIGN(256)`), 복사 → `DSB` → VTOR → `DSB; ISB`.

**실수 5 — "UsageFault 를 켰으니 UsageFault 로 오겠지".** B3 처럼 fault 가 우선순위 0 인 핸들러(SVC, 다른 IRQ) 안에서 나면 UsageFault 가 선점할 수 없어 HardFault 로 간다. HardFault 핸들러에서 `HFSR.FORCED = 1` 이면 **CFSR 을 같이 읽어야** 진짜 원인(여기선 INVSTATE)이 보인다. 고친 코드: 설정 가능한 fault 우선순위를 다른 예외보다 높게(숫자 작게) 두거나, 일반 IRQ 를 0 보다 큰 우선순위로 설계한다 (BM-M02).

**실수 6 — 함수 포인터를 정수로 다루다 bit0 을 잃는다.** `(fn_addr & ~3)` 로 "정렬" 하거나, 주소를 링커 맵에서 복사해 상수로 박거나(`(void(*)(void))0x08001234` — 맵의 주소는 짝수), 부트로더가 `app[1]` 을 `& ~1` 해서 `blx` 하면 INVSTATE. B4 실측: 쌓인 lr(0xb3d)이 범인 위치를 알려준다. 고친 코드: 함수 포인터는 그대로 전달, 상수로 쓸 땐 `| 1`, 부트로더는 `app[1]` 을 수정 없이 `bx`.

## 8. 면접 Q&A

<details>
<summary>Q1. 벡터 테이블 엔트리의 bit0 이 0 이면 정확히 무슨 일이 일어나나요?</summary>

예외 진입 자체는 성공한다: 프레임을 쌓고, `PC = entry & ~1`, `EPSR.T = entry & 1 = 0`. Cortex-M 은 Thumb 만 실행하므로 그 주소의 첫 명령을 실행하려는 순간 UsageFault, `CFSR.INVSTATE`(bit17, 값 0x00020000). UsageFault 가 꺼져 있거나 현재 우선순위보다 높지 않으면 HardFault 로 승격(`HFSR.FORCED`). 쌓인 pc = 그 핸들러 주소, 쌓인 xPSR bit24 = 0. 리셋 벡터면 리셋 직후 UsageFault 가 꺼져 있으니 곧장 FORCED HardFault, HardFault 벡터까지 짝수면 Lockup. 이 레슨 실측: B1 `ipsr 6, cfsr 0x00020000, pc 0xc0, xpsr 0x2000000b`.

</details>

<details>
<summary>Q2. 왜 [0] 은 홀수가 아니고, 그 값에는 어떤 제약이 있나요?</summary>

[0] 은 코드 주소가 아니라 **초기 MSP 값**(데이터)이다. 하드웨어가 리셋 때 그대로 SP_main 에 넣는데, ARMv7-M 은 SP 아래 2 비트를 0 으로 강제한다(4 바이트 정렬). AAPCS 는 함수 경계에서 8 바이트 정렬을 요구하므로 링커 스크립트에서 RAM 끝(예: 0x20010000)처럼 8 의 배수로 둔다. 꼬리질문 "SP 를 왜 테이블에 두나?" — 첫 명령 전에 스택이 있어야 Reset_Handler 를 C 로 쓰고, 그 전에 NMI/HardFault 가 와도 프레임을 쌓을 곳이 있기 때문.

</details>

<details>
<summary>Q3. C 로 쓴 핸들러는 왜 bit0 을 신경 안 써도 되죠? asm 에서는 무엇을 붙여야 하나요?</summary>

컴파일러가 Thumb 함수마다 `.type f, %function` 과 `.thumb_func` 를 내보내고, 링커가 `R_ARM_ABS32` 재배치(`.word f`)를 풀 때 Thumb 함수 심볼이면 `| 1` 을 한다. asm 에서는 직접 붙여야 한다: 실측상 GNU as 14.2 에서는 `.type %function` 만으로도, `.thumb_func` 만으로도 bit0 이 붙고, 아무것도 없으면(`NOTYPE`) 짝수. 별칭은 `.thumb_set` 이어야 하고 `.set` 은 짝수. `nm` 은 짝수 주소, `readelf -s` 는 st_value 에 bit0 을 포함해 보여준다.

</details>

<details>
<summary>Q4. VTOR 은 어떤 정렬이어야 하고 왜죠? Cortex-M0 는요?</summary>

ARMv7-M: 테이블 크기(엔트리 수 × 4)를 2 의 거듭제곱으로 올린 값, 최소 128 B. 이 레슨 48 엔트리 = 192 B → 256 B, M3 최대 240 IRQ 면 1024 B. 하드웨어가 엔트리 주소를 VTOR 과 예외 번호의 **비트 연결**로 만들 수 있게 하려는 규칙이다. VTOR 아래 7 비트는 구현되지 않는다(실측 `base|0x7F` → `base`). Cortex-M3 r2p0 문서는 TBLOFF = [29:7] 이고 bit29 가 코드/SRAM 영역 선택이다 (M4/ARMv7-M ARM 은 [31:7]로 기술 — 칩 문서 확인). Cortex-M0 에는 VTOR 이 없고(M0+ 는 선택 사항), STM32F0 는 SYSCFG 의 메모리 리맵으로 SRAM 을 주소 0 에 매핑해서 같은 효과를 낸다.

</details>

<details>
<summary>Q5. 부트로더에서 앱으로 점프하는 순서를 말해 보세요. VTOR 은 어디서 바꾸나요?</summary>

(1) 인터럽트 끄기(`cpsid i`), SysTick 정지, NVIC 의 enable/pending 지우기, 부트로더가 켠 주변장치·클럭 원복. (2) `SCB->VTOR = APP_BASE`(정렬 확인) + `DSB; ISB`. (3) `MSP = *(uint32_t*)APP_BASE`. (4) `pc = *(uint32_t*)(APP_BASE + 4)` 로 `bx` — **bit0 그대로**(홀수). (5) 앱 쪽에서 `cpsie i`. 앱의 SystemInit 이 VTOR 을 다시 쓰는 경우도 많다(`USER_VECT_TAB_ADDRESS`). 자세한 정리는 BM-A03. 흔한 버그: VTOR 을 안 바꿔서 앱의 첫 SysTick 이 부트로더 핸들러로 감, `app[1] & ~1` 로 INVSTATE.

</details>

<details>
<summary>Q6. UsageFault 를 켰는데도 HardFault 로 오는 경우는?</summary>

(a) fault 가 난 곳의 실행 우선순위가 UsageFault 우선순위보다 높거나 같을 때 — 실측 B3: SVC(0) 안에서 INVSTATE → UsageFault(0)가 선점 못 함 → FORCED HardFault. (b) fault 처리 중 또 fault(예: UsageFault 핸들러 엔트리 자체가 짝수). (c) 벡터 fetch 자체가 실패할 때(`HFSR.VECTTBL`, 이때는 CFSR 이 아니라 HFSR bit1). 그래서 HardFault 핸들러는 HFSR.FORCED 를 보면 CFSR 을 같이 읽는다 (ARM-M12).

</details>

<details>
<summary>Q7. 왜 Cortex-M 의 핸들러는 그냥 C 함수여도 되죠? Cortex-A/ARM7 과 뭐가 다르죠?</summary>

Cortex-M 은 예외 진입 때 하드웨어가 AAPCS 의 caller-saved 레지스터 8 개(r0–r3, r12, lr, pc, xPSR)를 스택에 쌓고, lr 에 EXC_RETURN(0xFFFFFFF9 같은 마법 값)을 넣는다. 그래서 컴파일러가 만든 평범한 함수의 `bx lr`/`pop {pc}` 가 곧 예외 복귀다 — 이 레슨의 `SVC_Handler` 는 5 명령짜리 일반 함수(0xc0). ARM7TDMI·Cortex-A 는 모드별 뱅크 레지스터와 `subs pc, lr, #4` 같은 특수 복귀가 필요해서 `__attribute__((interrupt))` 나 asm 래퍼를 쓴다 (AArch64 벡터는 ARM-A02).

</details>

<details>
<summary>Q8. `bx r0`, `pop {pc}`, `mov pc, r0` 에 짝수 주소를 주면 각각 어떻게 되나요?</summary>

ARMv7-M 의사코드 기준: `BX/BLX`(BXWritePC)와 `POP {pc}`/`LDR pc`(LoadWritePC → BXWritePC)는 **bit0 을 새 T 로** 쓴다 → 짝수면 T = 0 → INVSTATE. `MOV pc, Rm`/`ADD pc, …`(ALUWritePC → BranchWritePC)는 bit0 을 버리고 상태를 바꾸지 않는다 → 짝수든 홀수든 그냥 점프. practice 연습 4 실측: `pop {pc}` 짝수 → CFSR 0x00020000, `mov pc` 짝수·홀수 → fault 없이 정상 실행. 크래시 덤프에서 "pop {pc} 직후 INVSTATE" = 스택에 저장된 lr 이 망가졌다는 강한 신호다.

</details>

<details>
<summary>Q9. 런타임에 핸들러를 바꾸고 싶다. FLASH 테이블을 직접 고치면 안 되나요?</summary>

실제 MCU 의 내부 플래시는 일반 `str` 로 써지지 않는다(무시되거나 BusFault, 플래시 컨트롤러 절차 필요, 지우기 단위도 크다). 그래서 테이블을 RAM 으로 복사하고 VTOR 을 옮긴 뒤 RAM 쪽을 고친다 — CMSIS `NVIC_SetVector()` 가 정확히 `*(VTOR + 4*(16+IRQn)) = handler` (practice 연습 3 의 `irq_install`). QEMU mps2-an385 의 0x0 은 플래시가 아니라 SSRAM 이라 실기와 동작이 다를 수 있으니(이 레슨에서는 쓰기를 시험하지 않았다) 의존하면 안 된다. 쓰고 나서 `DSB` 를 두어 다음 예외의 벡터 fetch 전에 쓰기가 끝나게 한다.

</details>

## 9. 추가 연습

### 연습 1 — VTOR 정렬 계산기

`uint32_t vtor_align_bytes(uint32_t n_irq)` — 필요한 정렬(바이트), `int vtor_ok(uint32_t addr, uint32_t n_irq)` — 정렬과 TBLOFF 범위([31:30] = 0, Cortex-M3 r2p0 기준)를 만족하는지.

<details>
<summary>답</summary>

`bytes = (16 + n_irq) * 4`, `a = 128`, `while (a < bytes) a <<= 1`. 0 → 128, 16 → 128, 17 → 256, 32 → 256 (이 레슨), 82 → 512, 240 → 1024. `vtor_ok` = `addr % a == 0 && (addr >> 30) == 0`. 0x20000280 은 IRQ 16 개면 OK, 32 개면 정렬 위반. 이런 검사는 런타임보다 **빌드 타임**(링커 `ASSERT(ram_vec % 256 == 0)` 나 `_Static_assert`)에 두는 게 좋다.

</details>

### 연습 2 — 벡터 테이블 린트

`uint32_t vec_audit(const uint32_t *t, uint32_t n, uint32_t lo, uint32_t hi, uint32_t *first)` — 핸들러 칸(reserved 제외)이 0 이거나 짝수이거나 코드 범위 `[lo, hi)` 밖이면 bad. 개수와 첫 번호를 돌려준다. 실제 `g_vectors` 와 일부러 망가뜨린 복사본에 돌려라.

<details>
<summary>답</summary>

실제 테이블(`lo = 0xc0`(테이블 끝), `hi = _sidata`) → 0 개. `[15] &= ~1`(짝수), `[30] = 0x20000001`(RAM, 코드 범위 밖), `[8] = 0x12345678`(reserved, 무시) → 2 개, 첫 번호 15. `[2] = 0` 추가 → 3 개, 첫 번호 2. 실무에선 이 검사를 호스트 쪽 빌드 후처리(파이썬으로 ELF 의 `.isr_vector` 읽기)로 CI 에 넣는다.

</details>

### 연습 3 — 런타임 설치: IRQ7 과 NMI

`irq_install(irqn, handler)` 를 asm 으로 써라 (VTOR 을 읽어 `table[16 + irqn]` 에 쓰기 + `DSB`). RAM 테이블로 옮긴 뒤 IRQ7 핸들러를 설치하고 NVIC ISPR 로 pend 해서 불리는지 보라. 또 NMI 엔트리를 바꿔 끼우고, `cpsid i` 상태에서 `ICSR.NMIPENDSET`(bit31)으로 NMI 를 걸어 보라.

<details>
<summary>답</summary>

`ldr r2, =0xE000ED08` / `ldr r2, [r2]` / `adds r0, #16` / `str r1, [r2, r0, lsl #2]` / `dsb` / `bx lr`. 실측: `g_irq7_n == 1`, FLASH 테이블의 [23] 은 그대로. NMI 는 PRIMASK = 1 인 동안에도 바로 들어왔다(`nmi_in_primask == 1`) — PRIMASK 는 설정 가능한 우선순위(0 이상)만 막고 NMI(-2)·HardFault(-1)는 못 막는다 (ARM-M10).

</details>

### 연습 4 — 짝수 주소로 가는 세 가지 길

`pop_pc_to(target)` (`push {r0}; pop {pc}`)와 `mov_pc_to(target)` (`mov pc, r0`)를 asm 으로 써서 짝수 `bx_target` 주소로 보내 보라. 어느 쪽이 fault 를 내나?

<details>
<summary>답</summary>

실측: `pop {pc}` 짝수 → UsageFault, CFSR 0x00020000, 쌓인 pc = 0x5ac(`bx_target`), 쌓인 lr = 0x5cf(`pop_pc_ret` | 1) — 복구 뒤 `bx_target` 실행. `mov pc, r0` 은 짝수(0x5ac)·홀수(0x5ad) 모두 fault 없이 실행되어 카운터가 3 이 됐다. Q8 의 BXWritePC vs BranchWritePC 차이 그대로다. (주의: 짝수 주소가 4n+2 일 때 QEMU 10.2 는 INVSTATE 대신 UNALIGNED 를 보고했다 — ARM-B13 실측. 이 레슨은 그 차이를 피하려고 시험 대상 함수를 `.p2align 2` / `aligned(4)` 로 4 바이트 정렬했다.)

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # practice.c + practice.S + shared_startup.c
```

```text
ARM-M08 practice
PASS line 103: vtor_align_bytes(0) == 128u
...
PASS line 132: g_irq7_n == 1u
PASS line 139: nmi_in_primask == 1u
  4a pop{pc} even: cfsr = 0x00020000
  4a stacked pc = 0x000005ac
  4a stacked lr = 0x000005cf
PASS line 147: g_f.ipsr == 6u && g_f.cfsr == (1u << 17) && g_f.pc == even
PASS line 148: g_f.lr == (FN(pop_pc_ret) | 1u) && g_bx_ran == 1u
PASS line 153: g_f.n == nfault && g_bx_ran == 3u
18/18 passed
qemu exit=0
```

## 10. 핵심 정리

- 벡터 테이블 = 주소 `VTOR + 4n` 의 워드 배열. [0] 초기 MSP(데이터), [1] Reset, 2 NMI, 3 HardFault, 4–6 MemManage/BusFault/UsageFault, 11 SVCall, 14 PendSV, 15 SysTick, 16+n IRQn. 리셋 VTOR = 0.
- 코드 엔트리는 전부 **홀수**: 진입 때 `EPSR.T = entry & 1`. asm 에서는 `.type %function`/`.thumb_func`, 별칭은 `.thumb_set` (`.set` 은 짝수). `nm` 은 짝수, `readelf` st_value 는 홀수.
- 짝수 엔트리 → 진입은 성공, 첫 명령에서 INVSTATE(CFSR 0x00020000). 쌓인 pc = 핸들러 주소, 쌓인 xPSR.T = 0.
- UsageFault 가 꺼져 있거나 **우선순위로 선점 못 하면** HardFault + HFSR.FORCED(0x40000000). FORCED 를 보면 CFSR 을 읽어라.
- 재배치 = RAM 에 복사 → DSB → VTOR → DSB/ISB. 정렬 = 테이블 크기를 2 의 거듭제곱으로 올림(최소 128 B): 48 엔트리 → 256 B. VTOR 아래 7 비트는 무시된다.
- Reset_Handler asm: `.data` 복사(ITTT + post-index), `.bss` 0, `bl main`. Cortex-M 핸들러는 하드웨어 스태킹 덕에 평범한 C 함수.
- `bx`/`blx`/`pop {pc}`/`ldr pc` 는 bit0 → T, `mov pc` 는 bit0 무시. "pop {pc} 직후 INVSTATE" = 저장된 lr 손상.

## 참고자료

- ARMv7-M Architecture Reference Manual (DDI 0403) — B1.5 예외 모델 (벡터 테이블, 리셋, ExceptionTaken, 우선순위 승격), B3.2 SCB (VTOR, SHCSR, CFSR/UFSR, HFSR), A2.3.1 BXWritePC / BranchWritePC / LoadWritePC: <https://developer.arm.com/documentation/ddi0403/latest/>
- Cortex-M3 Devices Generic User Guide (DUI 0552) — 2.3.4 Vector table, 4.3.4 VTOR (TBLOFF [29:7]), 4.3.10 CFSR, 4.3.11 HFSR: <https://developer.arm.com/documentation/dui0552/latest/>
- Arm AN321 "ARM Cortex-M Programming Guide to Memory Barrier Instructions" — VTOR 변경 뒤 배리어: <https://developer.arm.com/documentation/dai0321/latest/>
- CMSIS-Core(M) — `startup_<device>.S` 템플릿(`.thumb_set` 약한 핸들러), `NVIC_SetVector`, `SCB->VTOR`: <https://arm-software.github.io/CMSIS_6/latest/Core/index.html>
- GNU as ARM 지시어 (`.thumb_func`, `.thumb_set`, `.syntax unified`): <https://sourceware.org/binutils/docs/as/ARM-Directives.html>
- ELF for the Arm Architecture (AAELF32) — `R_ARM_ABS32` 의 `(S + A) | T`: <https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst>
- QEMU Arm system emulator (mps2-an385, 세미호스팅, gdbstub): <https://www.qemu.org/docs/master/system/target-arm.html>
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* — 7장 예외와 인터럽트(벡터 테이블 재배치), 12장 fault 처리
- 관련 레슨: BM-B08 (C 벡터 테이블·스타트업), ARM-B13 (Thumb-2, blx 짝수 INVSTATE), ARM-B14 (MSP/PSP, EXC_RETURN 맛보기), BM-M02 (우선순위), BM-M13 (fault 정책), BM-A01 (SystemInit·VTOR), BM-A03 (부트로더 점프), ARM-M05 (DSB/ISB), ARM-M09 (예외 프레임), ARM-M10 (PRIMASK/BASEPRI), ARM-M12 (HardFault 분석기)
