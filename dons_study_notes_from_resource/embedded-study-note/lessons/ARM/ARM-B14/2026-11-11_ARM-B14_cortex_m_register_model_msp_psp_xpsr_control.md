---
id: ARM-B14
time: 65분
updated: 2026-11-11
---
# ARM-B14 Cortex-M 레지스터 모델: MSP/PSP, xPSR, CONTROL

## 학습 목표

- Cortex-M(ARMv7-M)의 **두 스택 포인터**(MSP, PSP)와 **특수 레지스터**(xPSR = APSR/IPSR/EPSR, CONTROL, PRIMASK)를 그림 하나로 그리고, 각각 리셋 직후 값이 무엇인지 말한다.
- `MRS`/`MSR` 로 이 레지스터들을 읽고 쓰는 Thumb-2 헬퍼(`get_msp/get_psp/set_psp/get_control/set_control/get_xpsr/get_ipsr`)를 직접 쓰고, `objdump` 로 32비트 인코딩(SYSm 필드)을 읽는다.
- Thread 모드를 PSP 로 바꾸는 순서(PSP 설정 → `CONTROL.SPSEL=1` → `ISB`)를 쓰고, QEMU + gdb 로 **sp 가 PSP 값으로 바뀌는 그 한 명령**을 본다.
- SVC/PendSV 핸들러 안에서 IPSR = 예외 번호, Handler 모드는 항상 MSP, `CONTROL.SPSEL` 이 0 으로 읽힘, LR = EXC_RETURN 값(0xFFFFFFFD / 0xFFFFFFF9)을 실제로 확인한다.
- 비특권(nPRIV=1) Thread 에서 `MSR CONTROL` 이 **무시되는 것**을 관찰하고, SVC 로만 특권을 되찾는 "시스템 콜" 경로를 만든다.

## 1. 문제

HardFault 덤프를 읽거나 RTOS 포팅을 할 때 면접관이 꼭 묻는다: "지금 sp 가 MSP 야 PSP 야? 그걸 어떻게 알아? 태스크는 왜 PSP 를 써?" — Cortex-M 펌웨어(STM32, nRF52, SSD 컨트롤러의 보조 Cortex-M 코어)의 시동 코드·ISR·컨텍스트 스위치는 전부 이 레지스터 모델 위에 서 있다. AArch64 와 달리 Cortex-M 은 **스택 포인터가 두 개**이고, 어느 쪽을 쓰는지는 모드(Thread/Handler)와 `CONTROL.SPSEL` 비트가 정한다.

ARM-B13 에서 Thumb-2 함수를 썼다면, 이번에는 일반 레지스터가 아닌 **특수 레지스터**를 만진다. 이것들은 `ldr/str` 로 접근할 수 없고 오직 `MRS`(특수 → 범용)와 `MSR`(범용 → 특수)로만 읽고 쓴다.

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Cortex-M3 레지스터 모델 (ARMv7-M) — 값은 이 레슨 QEMU 실측</text>
  <rect x="10" y="35" width="250" height="44" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor" font-family="monospace">r0–r3  인자·반환 (caller-saved)</text>
  <text x="20" y="71" fill="currentColor" font-family="monospace">r4–r11 callee-saved, r12 (ip)</text>
  <rect x="10" y="88" width="250" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="20" y="108" fill="currentColor" font-family="monospace">r13 = sp (보이는 건 하나)</text>
  <rect x="10" y="128" width="120" height="44" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="18" y="147" fill="currentColor" font-family="monospace">MSP (main)</text>
  <text x="18" y="164" fill="currentColor" font-family="monospace">0x20010000</text>
  <rect x="140" y="128" width="120" height="44" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="148" y="147" fill="currentColor" font-family="monospace">PSP (process)</text>
  <text x="148" y="164" fill="currentColor" font-family="monospace">0x00000000</text>
  <path d="M70 128 L110 118 M200 128 L160 118" stroke="currentColor" fill="none"/>
  <text x="10" y="190" fill="currentColor" font-size="12">리셋: MSP=vector[0], PSP=UNKNOWN</text>
  <text x="10" y="206" fill="currentColor" font-size="12">Handler 모드 → 항상 MSP</text>
  <text x="10" y="222" fill="currentColor" font-size="12">Thread 모드 → CONTROL.SPSEL 이 고른다</text>
  <rect x="10" y="235" width="250" height="26" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="20" y="253" fill="currentColor" font-family="monospace">r14 = lr   (예외 안: EXC_RETURN)</text>
  <rect x="10" y="268" width="250" height="26" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="20" y="286" fill="currentColor" font-family="monospace">r15 = pc   (bit0 없음, T 는 EPSR)</text>
  <text x="290" y="54" fill="currentColor" font-weight="bold">xPSR (MRS 이름: apsr / ipsr / epsr / xpsr)</text>
  <rect x="290" y="64" width="40" height="28" class="f-acc" stroke="currentColor"/>
  <text x="296" y="83" fill="currentColor" font-family="monospace">NZCV</text>
  <rect x="330" y="64" width="28" height="28" class="f-acc" stroke="currentColor"/>
  <text x="338" y="83" fill="currentColor" font-family="monospace">Q</text>
  <rect x="358" y="64" width="60" height="28" class="f-warn" stroke="currentColor"/>
  <text x="363" y="83" fill="currentColor" font-family="monospace">ICI/IT</text>
  <rect x="418" y="64" width="28" height="28" class="f-warn" stroke="currentColor"/>
  <text x="426" y="83" fill="currentColor" font-family="monospace">T</text>
  <rect x="446" y="64" width="110" height="28" class="f-mut" stroke="currentColor"/>
  <text x="456" y="83" fill="currentColor" font-family="monospace">ICI/IT, 예약</text>
  <rect x="556" y="64" width="170" height="28" class="f-ok" stroke="currentColor"/>
  <text x="566" y="83" fill="currentColor" font-family="monospace">예외 번호 [8:0]</text>
  <text x="290" y="108" fill="currentColor" font-size="12" font-family="monospace">31..28  27  26:25  24  15:10        8:0</text>
  <text x="290" y="126" fill="currentColor" font-size="12">APSR(플래그)   EPSR(실행 상태, MRS 로 읽으면 0)   IPSR</text>
  <text x="290" y="160" fill="currentColor" font-weight="bold">CONTROL</text>
  <rect x="290" y="170" width="200" height="28" class="f-mut" stroke="currentColor"/>
  <text x="300" y="189" fill="currentColor" font-family="monospace">0 … (bit2 FPCA: M4F/M7)</text>
  <rect x="490" y="170" width="110" height="28" class="f-warn" stroke="currentColor"/>
  <text x="498" y="189" fill="currentColor" font-family="monospace">bit1 SPSEL</text>
  <rect x="600" y="170" width="126" height="28" class="f-acc" stroke="currentColor"/>
  <text x="608" y="189" fill="currentColor" font-family="monospace">bit0 nPRIV</text>
  <text x="290" y="214" fill="currentColor" font-size="12">SPSEL: Thread 의 sp = 0 MSP / 1 PSP</text>
  <text x="290" y="230" fill="currentColor" font-size="12">nPRIV: Thread 가 0 특권 / 1 비특권</text>
  <text x="290" y="250" fill="currentColor" font-weight="bold">마스크 레지스터 (읽기만 — 사용은 ARM-M10)</text>
  <rect x="290" y="260" width="140" height="28" class="f-mut" stroke="currentColor"/>
  <text x="300" y="279" fill="currentColor" font-family="monospace">PRIMASK bit0</text>
  <rect x="440" y="260" width="130" height="28" class="f-mut" stroke="currentColor"/>
  <text x="450" y="279" fill="currentColor" font-family="monospace">BASEPRI</text>
  <rect x="580" y="260" width="146" height="28" class="f-mut" stroke="currentColor"/>
  <text x="590" y="279" fill="currentColor" font-family="monospace">FAULTMASK</text>
  <text x="10" y="326" fill="currentColor" font-size="12">gdb-multiarch 17.1 + QEMU 10.2 이름: $msp $psp $control $primask $basepri $faultmask $xpsr</text>
</svg>
```

### Part A — 특수 레지스터 읽기/쓰기 헬퍼

`starter.S` 의 TODO 를 채운다 (전부 leaf, 스택 안 씀, 2~3 명령):
`get_msp`, `get_psp`, `set_psp`, `get_control`, `set_control`(MSR 뒤 `ISB`), `get_xpsr`, `get_ipsr`, `get_primask`.
`get_sp`(지금 쓰는 sp = `mov r0, sp`)와 `svc_call`(SVC 발생)은 이미 주어져 있다 — 테스트의 기준값이다.
하네스는 리셋 직후 Thread 모드 상태를 확인한다: IPSR = 0, CONTROL = 0, MSP == sp, MSP 가 `_estack`(= vector[0]) 근처, EPSR.T 가 MRS 로 0, PRIMASK = 0, `set_psp` → `get_psp` 왕복.

### Part B — Thread 모드를 PSP 로: `run_on_psp`

`uint32_t run_on_psp(uint32_t *psp_top, uint32_t (*fn)(uint32_t), uint32_t arg)` — RTOS 가 첫 태스크를 띄울 때 하는 일의 축소판.
`PSP = psp_top`(8바이트 정렬된 정적 배열 꼭대기) → `CONTROL.SPSEL = 1` → `ISB` → `fn(arg)` 를 **PSP 위에서** 실행 → `SPSEL = 0` → `ISB` → MSP 로 돌아와 `fn` 의 반환값을 돌려준다.
테스트: 태스크 안에서 `sp == psp`, CONTROL = 2, sp 가 태스크 스택 배열 안, MSP 에는 `push {r4, lr}` 8 바이트만, IPSR 은 여전히 0(Thread), 돌아온 뒤 CONTROL = 0.

### Part C — 예외 안에서 본 레지스터, 그리고 비특권 태스크

`SVC_Handler`, `PendSV_Handler` 를 4 명령짜리 **트램펄린**으로 쓴다: `r0 = lr`(EXC_RETURN), `r1 = sp`, `r2 = MSP`, `r3 = PSP` 를 담고 C 함수 `exc_observe()` 로 `b`(bl 아님) 한다.

- C1: PSP 를 쓰던 태스크에서 `svc #0` → IPSR = 11, 핸들러 sp == MSP, CONTROL.SPSEL 은 0 으로 읽힘, EXC_RETURN = 0xFFFFFFFD, PSP 가 32 바이트(8 워드) 내려감.
- C2: MSP 를 쓰던 main 에서 PendSV pend(`ICSR.PENDSVSET`) → IPSR = 14, EXC_RETURN = 0xFFFFFFF9, 이번엔 MSP 가 32 바이트 내려감.
- C3: 태스크가 `nPRIV=1` 로 특권을 버린 뒤 `MSR CONTROL, 0` → **무시**(여전히 3). `svc_call(SVC_CMD_PRIV)` → 핸들러가 nPRIV 를 지워서 2 로 복귀.

| 파일 | 역할 |
|---|---|
| `harness.c` | 펌웨어 테스트 (starter/solution 공용), `exc_observe` |
| `starter.S` / `solution.S` | Thumb-2 헬퍼 (TODO / 정답) |
| `shared_startup.c` | 벡터 테이블, Reset_Handler, 세미호스팅, HardFault 보고 (BM-B08 축약) |
| `arm_b14.ld` | 링커 스크립트 (FLASH 0x0, RAM 0x20000000 64 KB) |
| `run_qemu.sh` | VM 안에서 빌드·실행: `starter|solution|practice|gdb|noisb|objdump|spselbug` |
| `demo_regs.gdb` / `demo_regs_out.txt` | gdb 세션 스크립트와 실제 출력 |
| `demo_spsel_bug.c` | 7. 실수 1 재현 (PSP 없이 SPSEL=1 → HardFault) |
| `practice.c` / `practice.S` | 추가 연습 답 |

## 2. Problem (English)

> **Cortex-M register model.** Target: Cortex-M3 (ARMv7-M), QEMU `mps2-an385`, bare metal, semihosting output.
>
> 1. Write Thumb-2 leaf functions (no stack use) satisfying:
>    `uint32_t get_msp(void); uint32_t get_psp(void); void set_psp(uint32_t); uint32_t get_control(void); void set_control(uint32_t); uint32_t get_xpsr(void); uint32_t get_ipsr(void); uint32_t get_primask(void);`
>    After reset in Thread mode: `get_ipsr() == 0`, `get_control() == 0`, `get_msp() == current sp`, and the T bit is **not** visible through `MRS xPSR`.
> 2. Write `uint32_t run_on_psp(uint32_t *psp_top, uint32_t (*fn)(uint32_t), uint32_t arg)` that runs `fn(arg)` in Thread mode on the process stack and returns its result on the main stack. `psp_top` is 8-byte aligned. Inside `fn`, `sp == PSP` and `CONTROL == 2` must hold. Explain why an `ISB` follows `MSR CONTROL`.
> 3. Write `SVC_Handler` / `PendSV_Handler` that pass (EXC_RETURN, sp, MSP, PSP) to a C function without disturbing LR. Show: IPSR equals the exception number; Handler mode runs on MSP; the stacked frame lands on whichever stack the interrupted Thread was using; EXC_RETURN is 0xFFFFFFFD vs 0xFFFFFFF9.
> 4. Drop privilege in a task (`CONTROL.nPRIV = 1`), show that `MSR CONTROL` is ignored while unprivileged, and regain privilege through an SVC.
>
> Example (real run): in the task `sp = psp = 0x20000440`, `control = 2`; in the SVC handler `ipsr = 11`, `sp = msp = 0x2000ff80`, `psp = 0x20000420`, `lr = 0xfffffffd`.
>
> Follow-ups: Why does an RTOS run tasks on PSP? What happens if you set SPSEL from the middle of a C function? How does a HardFault handler know which stack holds the frame? Why can't unprivileged code just write CONTROL back?

## 3. 시작 코드

테스트 하네스 (starter 와 solution 이 같이 쓴다):

<!-- code: harness.c -->

TODO 가 있는 어셈블리:

<!-- code: starter.S -->

빌드·실행은 Lima VM `elinux` 안에서 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1). Mac 홈은 VM 에서 읽기 전용이라 스크립트가 `/tmp/ARM-B14_work` 로 복사해서 빌드한다. 맥에서 한 줄:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ARM/ARM-B14
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # solution / practice / gdb / noisb / objdump / spselbug
```

`run_qemu.sh` 가 실제로 하는 일 (starter 예):

```bash
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib \
    -fno-tree-loop-distribute-patterns -T arm_b14.ld harness.c starter.S shared_startup.c -o starter.elf -lgcc
timeout 20 qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none \
    -semihosting-config enable=on,target=native -kernel starter.elf      # 펌웨어가 SYS_EXIT 로 끝낸다
```

<!-- code: run_qemu.sh -->

처음 실행 결과 (실제 출력, PASS 줄과 중간 덤프 일부 생략):

```text
ARM-B14 Cortex-M register model (QEMU mps2-an385)
  reset: sp      = 0x2000ff88
  reset: msp     = 0x00000000
FAIL line 109: msp0 == sp0
FAIL line 110: msp0 > (uint32_t)(uintptr_t)_ebss && msp0 <= (uint32_t)(uintptr_t)_estack
FAIL line 116: get_psp() == (uint32_t)(uintptr_t)TASK_TOP
FAIL line 118: get_psp() == 0x20004000u
  task : sp      = 0x2000ff70
  task : psp     = 0x00000000
FAIL line 129: t.sp == t.psp
FAIL line 130: t.control == CTRL_SPSEL
...
FAIL line 172: u.ctrl_back == CTRL_SPSEL
FAIL line 173: r == 42u && get_control() == 0u && get_sp() == get_msp()
9/32 passed
qemu exit=1
```

starter 에서도 PASS 하는 9개는 TODO 와 무관하거나(vector[0] == `_estack`, `fn` 의 반환값 42) **정답이 0** 인 검사다 (Thread 의 IPSR = 0, 리셋 CONTROL = 0, PRIMASK = 0, EPSR.T 가 0 으로 읽힘, 스텁끼리 0 == 0). 그래서 진짜 검증은 0 이 아닌 값이 나와야 하는 Part B/C(IPSR = 11/14, CONTROL = 2/3, EXC_RETURN)가 맡는다.

VM 없이 맥에서 "어셈블은 되나?" 만 확인 (Apple clang 은 ARM ELF 오브젝트도 만든다):

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
clang --target=thumbv7m-none-eabi -c solution.S -o /tmp/ARM-B14_solution.o && /usr/bin/objdump -d /tmp/ARM-B14_solution.o
```

```text
0000001c <get_msp>:
      1c: f3ef 8008    	mrs	r0, msp
      20: 4770         	bx	lr
...
00000034 <set_control>:
      34: f380 8814    	msr	control, r0
      38: f3bf 8f6f    	isb	sy
      3c: 4770         	bx	lr
```

이 파일들은 처음부터 ELF(arm-none-eabi) 용이다: `.global name` / `.type name, %function` / `.thumb_func` / `.size`. Apple Mach-O 의 `_name` 접두사 규칙(ARM-B01)은 여기 해당 없다. `.thumb_func` 는 심볼에 Thumb 표시를 붙여서 링커가 함수 주소의 bit0 을 1 로 만든다 — 벡터 테이블의 `SVC_Handler` 항목과 `blx r1` 이 그 bit0 에 의존한다 (자세히는 ARM-M08).

## 4. 힌트

<details>
<summary>힌트 1 — MRS/MSR 의 레지스터 이름</summary>

GNU as 는 `msp`, `psp`, `control`, `primask`, `ipsr`, `apsr`, `xpsr`(= `psr`) 를 이름으로 받는다. 읽기는 `mrs r0, <이름>`, 쓰기는 `msr <이름>, r0`. 함수는 `bx lr` 로 끝낸다. 스택은 필요 없다.

</details>

<details>
<summary>힌트 2 — CONTROL 은 읽고-고치고-쓰기</summary>

SPSEL 은 bit1 이다. 다른 비트(nPRIV)를 지우지 않으려면 `mrs r4, control` → `orr r4, r4, #2` → `msr control, r4`. 끝나면 `bic r4, r4, #2` 로 되돌린다. r4 는 callee-saved 라 함수 입구에서 `push {r4, lr}` 했는지 확인 (8 바이트라 정렬도 유지된다).

</details>

<details>
<summary>힌트 3 — MSR CONTROL 직후에 무엇이 필요한가</summary>

MSR 이 끝나기 전에 이미 가져온(fetch) 다음 명령이 옛 SP 로 실행되지 않게 하려면 파이프라인을 비워야 한다. Arm 은 CONTROL 을 쓴 뒤 `ISB` 를 요구한다 (ARM-M05 의 ISB 와 같은 명령).

</details>

<details>
<summary>힌트 4 — 트램펄린은 왜 b 인가</summary>

예외 핸들러에 들어온 순간 `lr` 에는 복귀 주소가 아니라 **EXC_RETURN** 이라는 마법 값이 들어 있다. `bl exc_observe` 를 쓰면 `lr` 이 덮여서 C 함수의 return 이 트램펄린으로 돌아오고, 그다음 할 일이 없다. `b exc_observe` 면 C 함수가 `lr`(EXC_RETURN)로 돌아가고, 그게 곧 예외 복귀다. `lr` 은 덮이기 전에 `mov r0, lr` 로 먼저 복사한다.

</details>

## 5. 풀이

### 0. 규칙 정리 — 어디에 쓰여 있나

| 규칙 | 근거 (확인 권장 표시는 기억에 의존한 부분) |
|---|---|
| 리셋: MSP ← vector[0], PC ← vector[1](bit0 = 1 → EPSR.T), CONTROL = 0 (특권 + MSP), PRIMASK = 0 | ARMv7-M ARM B1.5.5 Reset behavior. 실측: gdb 가 리셋 직후 `msp = 0x20010000 = vector[0]` |
| PSP 는 리셋 후 UNKNOWN | ARMv7-M ARM 리셋 의사코드 (확인 권장). QEMU 는 0 을 보여줬다 → 의존 금지 |
| Handler 모드는 항상 MSP, 항상 특권 | B1.4.1 / B1.3.1 |
| CONTROL.SPSEL: Handler 모드에서 0 으로 읽히고 쓰기 무시, 예외 복귀 때 EXC_RETURN 에 따라 갱신 | Cortex-M3 Devices Generic User Guide, CONTROL register. 실측: 핸들러 안 CONTROL 의 bit1 = 0, 쓰기 무시 (연습 2) |
| 비특권 Thread 의 `MSR CONTROL/MSP/PSP/PRIMASK` 는 무시 | ARMv7-M ARM B5.2.3 MSR 의사코드 (`if CurrentModeIsPrivileged()`) |
| 비특권 `MRS MSP/PSP` 는 0 | B5.2.2 MRS 의사코드로 기억 — **확인 권장**. 실측: QEMU 는 0 (연습 2) |
| `MRS` 로 읽은 EPSR 비트(T, ICI/IT)는 0 | Generic User Guide "Execution Program Status Register". 실측: MRS 0x01000000 비트 없음, gdb `$xpsr` 에는 있음 |
| MSR CONTROL 뒤 ISB | ARMv7-M ARM B5.2.3 / Arm AN321 (M3/M4 구현상 없어도 동작하지만 이식성 위해 넣으라는 취지 — 확인 권장) |
| 예외 번호: SVCall 11, PendSV 14, SysTick 15, IRQn = 16 + n | B1.5.2 |

### Part A — MRS/MSR 헬퍼와 인코딩

헬퍼는 전부 2~3 명령이다. 핵심은 **어떤 이름이 어떤 SYSm 번호로 인코딩되나**다. `run_qemu.sh objdump` 실제 출력:

```text
000006f4 <get_msp>:
     6f4:	f3ef 8008 	mrs	r0, MSP
000006fa <get_psp>:
     6fa:	f3ef 8009 	mrs	r0, PSP
00000700 <set_psp>:
     700:	f380 8809 	msr	PSP, r0
00000706 <get_control>:
     706:	f3ef 8014 	mrs	r0, CONTROL
0000070c <set_control>:
     70c:	f380 8814 	msr	CONTROL, r0
     710:	f3bf 8f6f 	isb	sy
00000716 <get_xpsr>:
     716:	f3ef 8003 	mrs	r0, PSR
0000071c <get_ipsr>:
     71c:	f3ef 8005 	mrs	r0, IPSR
00000722 <get_primask>:
     722:	f3ef 8010 	mrs	r0, PRIMASK
```

(`bx lr` = `4770` 줄은 생략.) MRS/MSR 은 Thumb-2 **32비트** 명령(하프워드 두 개)이고, 두 번째 하프워드의 하위 8비트가 SYSm 이다:

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">MRS / MSR (T1) — 실측 인코딩으로 읽기</text>
  <text x="10" y="50" fill="currentColor" font-family="monospace">MRS r0, MSP  = f3ef 8008</text>
  <rect x="210" y="34" width="250" height="26" class="f-mut" stroke="currentColor"/>
  <text x="220" y="52" fill="currentColor" font-family="monospace">hw1 = 11110 0 111110 1111</text>
  <rect x="460" y="34" width="70" height="26" class="f-mut" stroke="currentColor"/>
  <text x="470" y="52" fill="currentColor" font-family="monospace">1000</text>
  <rect x="530" y="34" width="70" height="26" class="f-acc" stroke="currentColor"/>
  <text x="540" y="52" fill="currentColor" font-family="monospace">Rd=0</text>
  <rect x="600" y="34" width="130" height="26" class="f-ok" stroke="currentColor"/>
  <text x="610" y="52" fill="currentColor" font-family="monospace">SYSm=0x08</text>
  <text x="10" y="94" fill="currentColor" font-family="monospace">MSR CONTROL,r4 = f384 8814</text>
  <rect x="210" y="78" width="250" height="26" class="f-mut" stroke="currentColor"/>
  <text x="220" y="96" fill="currentColor" font-family="monospace">hw1 = 11110 0 11100 0 Rn=4</text>
  <rect x="460" y="78" width="140" height="26" class="f-warn" stroke="currentColor"/>
  <text x="468" y="96" fill="currentColor" font-family="monospace">1000 m=10 00</text>
  <rect x="600" y="78" width="130" height="26" class="f-ok" stroke="currentColor"/>
  <text x="610" y="96" fill="currentColor" font-family="monospace">SYSm=0x14</text>
  <text x="10" y="134" fill="currentColor" font-weight="bold">SYSm 번호 (arm-none-eabi-as 로 어셈블 → objdump 로 전부 확인)</text>
  <text x="10" y="156" fill="currentColor" font-family="monospace">0x00 APSR 0x01 IAPSR 0x02 EAPSR 0x03 xPSR(PSR) 0x05 IPSR 0x06 EPSR 0x07 IEPSR</text>
  <text x="10" y="176" fill="currentColor" font-family="monospace">0x08 MSP 0x09 PSP 0x10 PRIMASK 0x11 BASEPRI 0x12 BASEPRI_MAX 0x13 FAULTMASK 0x14 CONTROL</text>
  <text x="10" y="204" fill="currentColor" font-size="12">bit0 = IPSR 포함, bit1 = EPSR 포함, bit2 = 0 이면 APSR 포함 → 0x03 = 셋 다, 0x05 = IPSR 만</text>
  <text x="10" y="222" fill="currentColor" font-size="12">(objdump 는 SYSm 0x00 을 "CPSR" 로 표시한다 — A-profile 이름을 빌려 쓴 것, 뜻은 APSR)</text>
</svg>
```

리셋 직후 Thread 모드의 실측 (`run_qemu.sh solution` 앞부분):

```text
  reset: sp      = 0x2000ff88
  reset: msp     = 0x2000ff88
  reset: psp     = 0x00000000
  reset: control = 0x00000000
  reset: xpsr    = 0x20000000
```

- `msp == sp`: Thread 모드 + SPSEL = 0 → 지금 sp 가 곧 MSP. `0x2000ff88` 은 `_estack = 0x20010000` 에서 Reset_Handler·main 프레임만큼 내려온 값.
- `xpsr = 0x20000000`: bit29 = C 플래그(직전 비교 결과) 하나만. **T 비트(bit24)는 없다**. 그런데 gdb 는 `run_on_psp` 진입 때 `xpsr=0x01000000`, 리셋 때 `0x41000000` 처럼 늘 T 를 보여준다 — gdb 는 디버그 인터페이스로 전체 xPSR 을 읽고, 소프트웨어의 `MRS` 는 EPSR 을 0 으로 읽는다. "T 비트가 0 이니 ARM 상태인가?" 는 함정이다. Cortex-M 은 Thumb 상태밖에 없고, T = 0 으로 실행하려 하면 INVSTATE UsageFault 다 (ARM-M08/M12).
- `psp = 0`: QEMU 값일 뿐 아키텍처는 UNKNOWN. 쓰기 전에 읽지 말 것.

### Part B — `run_on_psp` 한 줄씩

```text
run_on_psp:
    push    {r4, lr}         ① MSP 에 8 바이트 (돌아올 주소와 r4)
    msr     psp, r0          ② PSP = 태스크 스택 꼭대기 (아직 아무도 PSP 를 안 쓴다)
    mrs     r4, control      ③ 읽고
    orr     r4, r4, #2       ④ SPSEL = 1 로 고치고
    msr     control, r4      ⑤ 쓰는 순간 sp 가 PSP 로 바뀐다
    isb                      ⑥ 이후 명령이 새 SP 를 보게
psp_switched:
    mov     r0, r2 / blx r1  ⑦ fn(arg) — PSP 위에서. blx 는 r1 의 bit0(Thumb) 을 본다
    mrs/bic/msr/isb          ⑧ SPSEL = 0 → 다시 MSP
    pop     {r4, pc}         ⑨ ①에서 MSP 에 넣은 것을 MSP 에서 꺼낸다
```

①을 ⑤보다 **먼저** 하는 게 핵심이다. ⑨는 MSP 에서 꺼내야 하므로 넣을 때도 MSP 에 넣어야 한다. 태스크 쪽 스택에는 우리 함수의 흔적이 전혀 없다.

gdb 로 한 명령씩 실행한 실제 기록 (`run_qemu.sh gdb`, `demo_regs.gdb` 의 (2)):

| 실행한 명령 (pc) | sp | msp | psp | control |
|---|---|---|---|---|
| (진입, 0x728) | 0x2000ff88 | 0x2000ff88 | 0x20004000 | 0 |
| `push {r4, lr}` | 0x2000ff80 | 0x2000ff80 | 0x20004000 | 0 |
| `msr PSP, r0` | 0x2000ff80 | 0x2000ff80 | **0x20000450** | 0 |
| `mrs r4, CONTROL` | 0x2000ff80 | 0x2000ff80 | 0x20000450 | 0 |
| `orr.w r4, r4, #2` | 0x2000ff80 | 0x2000ff80 | 0x20000450 | 0 |
| `msr CONTROL, r4` | **0x20000450** | 0x2000ff80 | 0x20000450 | **2** |
| `isb sy` (→ psp_switched, 0x73e) | 0x20000450 | 0x2000ff80 | 0x20000450 | 2 |

(`psp = 0x20004000` 은 Part A 마지막에 시험 삼아 넣어 둔 값이다.) `msr CONTROL` 한 명령으로 `sp` 열이 MSP 값에서 PSP 값으로 넘어간다. 레지스터 r13 은 하나로 보이지만 실제로는 **MSP/PSP 두 개 중 하나를 가리키는 이름**이라는 뜻이다.

```svg
<svg viewBox="0 0 740 316" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">두 스택 (solution 실측 주소) — 각자 아래로 자란다</text>
  <rect x="40" y="40" width="300" height="230" class="f-mut" stroke="currentColor"/>
  <text x="50" y="60" fill="currentColor" font-weight="bold">MSP 영역 (RAM 끝)</text>
  <text x="200" y="80" fill="currentColor" font-family="monospace">0x20010000 _estack</text>
  <rect x="60" y="92" width="260" height="40" class="f-ok" stroke="currentColor"/>
  <text x="70" y="116" fill="currentColor">Reset_Handler · main 프레임</text>
  <text x="200" y="148" fill="currentColor" font-family="monospace">0x2000ff88</text>
  <rect x="60" y="154" width="260" height="30" class="f-acc" stroke="currentColor"/>
  <text x="70" y="174" fill="currentColor" font-family="monospace">push {r4, lr} (run_on_psp)</text>
  <text x="70" y="204" fill="currentColor" font-family="monospace">0x2000ff80 = 태스크 실행 중 MSP</text>
  <text x="70" y="224" fill="currentColor">= SVC 핸들러의 sp (그대로 시작)</text>
  <text x="70" y="252" fill="currentColor">PendSV(main): 프레임 32 B 가 여기로</text>
  <rect x="400" y="40" width="320" height="230" class="f-mut" stroke="currentColor"/>
  <text x="410" y="60" fill="currentColor" font-weight="bold">PSP 영역 = g_task_stack[256] (1 KB)</text>
  <text x="560" y="80" fill="currentColor" font-family="monospace">0x20000450 top</text>
  <rect x="420" y="92" width="280" height="30" class="f-ok" stroke="currentColor"/>
  <text x="430" y="112" fill="currentColor" font-family="monospace">task_probe push {r3,r4,r5,lr}</text>
  <text x="560" y="138" fill="currentColor" font-family="monospace">0x20000440 sp</text>
  <rect x="420" y="146" width="280" height="40" class="f-warn" stroke="currentColor"/>
  <text x="430" y="164" fill="currentColor">svc #0 → 하드웨어가 8 워드 저장</text>
  <text x="430" y="180" fill="currentColor" font-size="12">(내용 해독은 ARM-M09)</text>
  <text x="560" y="204" fill="currentColor" font-family="monospace">0x20000420 psp</text>
  <text x="430" y="236" fill="currentColor">…</text>
  <text x="560" y="258" fill="currentColor" font-family="monospace">0x20000050 바닥</text>
  <path d="M340 180 L400 180" stroke="currentColor" stroke-dasharray="4 3" fill="none"/>
  <text x="10" y="290" fill="currentColor" font-size="12">태스크 스택에는 run_on_psp 의 흔적이 없다.</text>
  <text x="10" y="306" fill="currentColor" font-size="12">예외 프레임은 "끊긴 Thread 가 쓰던 스택" 에 쌓이고, 핸들러는 MSP 에서 실행된다.</text>
</svg>
```

**왜 ISB 인가.** `MSR CONTROL` 은 실행 상태를 바꾸는 명령이다. 파이프라인에는 이미 다음 명령이 가져와져(디코드되어) 있을 수 있고, Arm ARM 은 "CONTROL 쓰기가 다음 명령부터 보이게 하려면 ISB" 라고 규정한다. Cortex-M3/M4 는 구현상 ISB 없이도 동작한다는 Arm 앱 노트(AN321) 설명이 있지만, M7(긴 파이프라인)·이식성을 위해 넣는다 (확인 권장). `run_qemu.sh noisb` 로 ISB 를 빼고 돌려 봤다:

```text
00000728 <run_on_psp>:
     728:	b510      	push	{r4, lr}
     72a:	f380 8809 	msr	PSP, r0
     72e:	f3ef 8414 	mrs	r4, CONTROL
     732:	f044 0402 	orr.w	r4, r4, #2
     736:	f384 8814 	msr	CONTROL, r4
0000073a <psp_switched>:
     73a:	4610      	mov	r0, r2
...
32/32 passed
qemu exit=0
```

QEMU 에서는 **아무 차이가 없다**. QEMU 는 명령을 하나씩 순서대로 흉내 내므로 "가져왔지만 아직 옛 상태로 실행" 이 생기지 않는다. 그러니 "QEMU 에서 통과 = 실리콘에서 안전" 이 아니다 — 배리어 같은 것은 문서 규칙으로 넣는다.

### Part C — 예외 안에서 본 것

트램펄린은 이렇다:

```text
SVC_Handler:
    mov  r0, lr     // EXC_RETURN — 다음 명령들이 lr 을 바꾸기 전에 복사
    mov  r1, sp     // Handler 모드의 sp
    mrs  r2, msp
    mrs  r3, psp
    b    exc_observe   // lr 은 그대로 EXC_RETURN → C 함수의 return 이 곧 예외 복귀
```

컴파일러가 만든 `exc_observe` 끝부분을 보면 정말로 EXC_RETURN 을 pc 에 넣어서 돌아간다: `ldmia.w sp!, {r4, r5, r6, r7, r8, pc}` (실측 objdump, 0x116). C 함수가 예외 핸들러로 쓰일 수 있는 이유가 이것이다 — Cortex-M 은 "특정 값(0xFFFFFFxx)이 pc 에 들어가면 예외 복귀" 로 정했기 때문에 핸들러가 평범한 AAPCS 함수여도 된다.

실측 (`run_qemu.sh solution`):

```text
  task : sp      = 0x20000440
  task : psp     = 0x20000440
  task : msp     = 0x2000ff80
  task : control = 0x00000002
  SVC  : ipsr       = 0x0000000b
  SVC  : control    = 0x00000000
  SVC  : EXC_RETURN = 0xfffffffd
  SVC  : sp         = 0x2000ff80
  SVC  : msp        = 0x2000ff80
  SVC  : psp        = 0x20000420
  SVC  : thread sp before svc = 0x20000440
  PendSV: ipsr       = 0x0000000e
  PendSV: EXC_RETURN = 0xfffffff9
  PendSV: sp         = 0x2000ff68
  unpriv: control after drop = 0x00000003
  unpriv: control after msr  = 0x00000003
  unpriv: control after svc  = 0x00000002
32/32 passed
qemu exit=0
```

| 관찰 | SVC (PSP 태스크에서) | PendSV (MSP 의 main 에서) |
|---|---|---|
| IPSR | 11 = SVCall | 14 = PendSV |
| 핸들러 sp | == MSP = 0x2000ff80 (태스크 때 MSP 그대로) | == MSP = 0x2000ff68 (main sp − 32) |
| 프레임이 쌓인 곳 | PSP: 0x20000440 → 0x20000420 (32 B) | MSP: main sp → −32 B |
| 핸들러 안 CONTROL | 0 (SPSEL 0 으로 읽힘) | 0 |
| LR = EXC_RETURN | 0xFFFFFFFD | 0xFFFFFFF9 |
| 복귀 후 | CONTROL = 2, sp = 0x20000440 그대로 | main 계속 |

EXC_RETURN 두 값의 차이가 "돌아갈 때 어느 스택에서 프레임을 꺼낼지" 를 담고 있다는 정도만 기억하고, 비트 해석과 프레임 내용은 ARM-M09 에서 한다. 예외 진입 때 하드웨어가 CONTROL.SPSEL 을 0 으로 만들고, 복귀 때 EXC_RETURN 을 보고 다시 1 로 돌려 놓는다. 그래서 핸들러 안에서 SPSEL 이 0 으로 읽히고 태스크로 돌아오면 2 가 다시 보인다.

**비특권 태스크 (C3)** — gdb (5) 실측:

```text
Breakpoint 4, svc_call () at solution.S:27
  pc=0x000006dc sp=0x20000440 msp=0x2000ff80 psp=0x20000440 control=3 xpsr=0x01000000 ipsr=0
Breakpoint 5, SVC_Handler () at solution.S:147
  pc=0x00000754 sp=0x2000ff80 msp=0x2000ff80 psp=0x20000420 control=1 xpsr=0x0100000b ipsr=11
  lr (EXC_RETURN) = 0xfffffffd
```

핸들러 안에서 `control=1` 이다. nPRIV = 1 이 그대로 남아 있지만 **Handler 모드는 nPRIV 와 무관하게 항상 특권**이다. nPRIV 는 "Thread 모드일 때의 특권" 만 정한다. 그래서 핸들러가 `MSR CONTROL` 로 nPRIV 를 지울 수 있고, 복귀하면 Thread 가 특권으로 돌아와 있다 (`control after svc = 2`). 이게 RTOS 의 "시스템 콜" 구조다: 비특권 태스크 → `svc` → 특권 커널 코드 → 복귀.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Thread/Handler × 특권 × 스택 — 누가 어디로 갈 수 있나 (괄호 = CONTROL 값)</text>
  <rect x="20" y="50" width="200" height="56" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="32" y="72" fill="currentColor" font-weight="bold">Thread · 특권 · MSP (0)</text>
  <text x="32" y="92" fill="currentColor" font-size="12">리셋 직후, main</text>
  <rect x="270" y="50" width="200" height="56" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="282" y="72" fill="currentColor" font-weight="bold">Thread · 특권 · PSP (2)</text>
  <text x="282" y="92" fill="currentColor" font-size="12">run_on_psp 안 task_probe</text>
  <rect x="20" y="190" width="200" height="56" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="32" y="212" fill="currentColor" font-weight="bold">Thread · 비특권 · MSP (1)</text>
  <text x="32" y="232" fill="currentColor" font-size="12">연습 2</text>
  <rect x="270" y="190" width="200" height="56" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="282" y="212" fill="currentColor" font-weight="bold">Thread · 비특권 · PSP (3)</text>
  <text x="282" y="232" fill="currentColor" font-size="12">RTOS 태스크 (task_unpriv)</text>
  <rect x="540" y="110" width="190" height="80" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="552" y="134" fill="currentColor" font-weight="bold">Handler 모드</text>
  <text x="552" y="154" fill="currentColor" font-size="12">항상 특권 · 항상 MSP</text>
  <text x="552" y="172" fill="currentColor" font-size="12">IPSR = 예외 번호</text>
  <path d="M220 70 L270 70" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M262 65 L270 70 L262 75" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M270 92 L220 92" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M228 87 L220 92 L228 97" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="200" y="45" fill="currentColor" font-size="12" font-family="monospace">MSR CONTROL (SPSEL)</text>
  <path d="M120 106 L120 190" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M115 182 L120 190 L125 182" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M370 106 L370 190" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M365 182 L370 190 L375 182" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="128" y="150" fill="currentColor" font-size="12">MSR nPRIV=1</text>
  <text x="378" y="150" fill="currentColor" font-size="12">MSR nPRIV=1</text>
  <text x="20" y="272" fill="currentColor" font-size="12">비특권 → 특권: MSR 은 무시됨. 유일한 길 = 예외(SVC) → 핸들러가 nPRIV 를 지우고 복귀.</text>
  <path d="M470 220 C 520 220, 560 210, 600 190" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M590 192 L600 190 L596 200" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="480" y="240" fill="currentColor" font-size="12" font-family="monospace">svc / IRQ</text>
  <path d="M600 110 C 560 80, 520 75, 470 75" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M478 70 L470 75 L478 80" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="560" y="70" fill="currentColor" font-size="12" font-family="monospace">복귀 0xFFFFFFFD</text>
  <path d="M540 150 C 400 150, 300 140, 220 100" stroke="currentColor" fill="none" stroke-dasharray="5 3"/>
  <text x="300" y="162" fill="currentColor" font-size="12" font-family="monospace">복귀 0xFFFFFFF9</text>
</svg>
```

## 6. 정답 코드

<!-- code: solution.S diff=starter.S -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"    # 마지막 줄: 32/32 passed, qemu exit=0
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh gdb"         # demo_regs.gdb → demo_regs_out.txt 와 같은 출력
```

gdb 세션 스크립트 (전체 실제 출력은 `demo_regs_out.txt`):

<!-- code: demo_regs.gdb -->

리셋 직후 `info registers` 실측 (gdb-multiarch 17.1 이 QEMU 10.2 에서 받은 M-profile 레지스터 목록):

```text
sp             0x20010000          0x20010000
lr             0xffffffff          -1
pc             0x88c               0x88c <Reset_Handler>
xpsr           0x41000000          1090519040
msp            0x20010000          536936448
psp            0x0                 0
primask        0x0                 0
control        0x0                 0
basepri        0x0                 0
faultmask      0x0                 0
  vector[0]=0x20010000 vector[1]=0x0000088d
```

`pc = 0x88c` 인데 vector[1] 은 `0x88d` 이다: bit0 = 1 은 "Thumb 상태" 표시로 EPSR.T 로 들어가고 pc 에는 0x88c 가 들어간다. `$xpsr = 0x41000000` 의 `0x01000000` 이 바로 그 T 비트다 (ARM-M08). 이름은 `$msp $psp $control $primask $basepri $faultmask` 로 쓰면 된다 (`p/x $msp` → `$1 = 0x2000ff80` 처럼 실측됨).

## 7. 자주 하는 실수

**1. PSP 를 정하지 않고 C 에서 `set_control(SPSEL)`.** `set_control(2)` 가 돌아오는 순간 C 함수의 sp 가 PSP(리셋 값, QEMU 에서 0)로 바뀐다. 그다음 push 가 0 − 8 = 0xFFFFFFF8 에 쓰려 한다. `run_qemu.sh spselbug` (`demo_spsel_bug.c`) 실측:

```text
before: sp=0x2000fff0 psp=0x00000000 control=0x00000000
HardFault! CFSR=0x00009200 HFSR=0x40000000 BFAR=0xfffffff8
qemu exit=3
```

BFAR = 0xfffffff8 이 정확히 "0 에서 push" 주소다 (CFSR/HFSR 해석은 ARM-M12). 고친 것: PSP 를 먼저 정하고, 스위치는 **스택을 안 쓰는 asm 안에서** 한 다음 새 스택에서 시작하는 함수를 부른다 (`run_on_psp`). 같은 이유로 C 함수 중간에서 MSP 를 바꾸는 `__set_MSP()` 도 그 함수가 돌아갈 수 없게 만든다.

**2. 트램펄린에서 `bl exc_observe`.** `bl` 은 lr 을 트램펄린 다음 주소로 덮는다. C 함수가 그리로 돌아오면 그 뒤에 아무 명령도 없다(다음 함수로 흘러간다). 그리고 EXC_RETURN 은 이미 사라져서 예외 복귀를 할 수 없다. 필요하면 `push {r0, lr}` 로 EXC_RETURN 을 저장하고 `bl` 한 뒤 `pop {r0, pc}` 로 복귀한다 (두 레지스터 = 8 바이트 정렬 유지).

**3. 비특권으로 내려간 뒤 MSR 로 돌아오려 함.** `task_unpriv` 의 `set_control(0)` 은 무시됐다(실측 `control after msr = 3`). 더 무서운 경우는 `run_on_psp` 의 ⑧이다: 태스크가 비특권인 채로 돌아오면 `msr control` (SPSEL = 0) 이 무시되어 **PSP 위에서** `pop {r4, pc}` 를 하게 되고, MSP 에 넣었던 복귀 주소 대신 쓰레기로 점프한다. 그래서 하네스의 태스크는 돌아가기 전에 반드시 SVC 로 특권을 되찾는다. 실제 RTOS 는 태스크가 리턴하는 경우를 막으려고 lr 에 "태스크 종료 함수" 를 넣어 둔다.

**4. PSP 꼭대기를 8 바이트 정렬하지 않음.** AAPCS 는 공개 인터페이스에서 sp 8 바이트 정렬을 요구하고(64비트 인자, `ldrd`), 예외 진입도 `CCR.STKALIGN = 1` 이면 프레임을 8 바이트 경계로 맞추려고 4 바이트 패딩을 넣는다 (이 경우 EXC_RETURN 이 아니라 저장된 xPSR bit9 에 표시 — ARM-M09). 정적 배열에 `__attribute__((aligned(8)))` 을 붙이고 `top = &stack[N]` 으로 잡는다. 이 레슨에서 PSP 차이가 정확히 32 B 였던 것은 svc 직전 sp(0x20000440)가 8 정렬이어서다.

**5. ISB 생략 — QEMU 가 숨긴다.** 위 `noisb` 실측처럼 QEMU 에서는 그대로 통과한다. 리뷰에서 잡아야 하는 종류의 버그다. CMSIS 의 `__set_CONTROL()` 을 쓸 때도 바로 뒤에 `__ISB()` 를 두는 것이 Arm 권장 패턴이다 (확인 권장: CMSIS 버전에 따라 `__set_CONTROL` 내부에 ISB 가 있는지 다르다).

**6. "지금 ISR 안인가?" 를 CONTROL 로 판단.** 핸들러 안에서 CONTROL.SPSEL 은 늘 0 이고 nPRIV 는 Thread 쪽 값(실측 `control=1`)이 남아 있다. ISR 여부는 **IPSR != 0** 으로 본다 (FreeRTOS 의 `xPortIsInsideInterrupt()` 가 IPSR 을 읽는 것과 같은 방식).

## 8. 면접 Q&A

<details>
<summary>Q1. RTOS 는 왜 태스크를 PSP 로 돌리나? MSP 하나로는 안 되나?</summary>

(1) **ISR 스택 공간을 태스크마다 잡지 않아도 된다**: 예외가 나면 프레임(8 워드)만 태스크 스택에 쌓이고, 핸들러 자체와 중첩 ISR 은 전부 MSP 하나를 쓴다. MSP 하나뿐이면 모든 태스크 스택이 "최악의 ISR 중첩 깊이" 만큼 여유를 가져야 한다. (2) **컨텍스트 스위치가 단순해진다**: PendSV 핸들러는 MSP 에서 돌면서 `mrs r0, psp` 로 현재 태스크 스택을 얻고, 남은 레지스터를 저장한 뒤 다음 태스크의 PSP 를 `msr psp` 로 넣고 돌아가면 끝이다 (ARM-A01). (3) 태스크 스택이 넘쳐도 커널(MSP) 스택은 덜 오염된다 — 단 M3/M4 에는 하드웨어 한계 레지스터가 없어서 MPU 가드나 칠하기(연습 3)로 잡고, ARMv8-M(M33)부터 PSPLIM/MSPLIM 이 생겼다.

</details>

<details>
<summary>Q2. 예외가 들어오면 스택과 CONTROL 에 무슨 일이 일어나나? (EXC_RETURN 은 값만)</summary>

끊긴 코드가 쓰던 스택(Thread + SPSEL=1 이면 PSP, 아니면 MSP)에 하드웨어가 8 워드 프레임을 쌓는다 (실측: PSP 0x20000440 → 0x20000420). 그다음 Handler 모드로 바뀌며 sp 는 MSP, CONTROL.SPSEL 은 0, IPSR 은 예외 번호(SVC 11), lr 에는 EXC_RETURN(PSP Thread 로 돌아가면 0xFFFFFFFD, MSP Thread 면 0xFFFFFFF9, 중첩 Handler 면 0xFFFFFFF1)이 들어간다. 이 값을 pc 에 넣으면 복귀가 시작되고, 하드웨어가 그 값을 보고 어느 스택에서 프레임을 꺼낼지, SPSEL 을 몇으로 할지 정한다. 비트별 의미와 프레임 해독은 ARM-M09.

</details>

<details>
<summary>Q3. HardFault 핸들러는 프레임이 어느 스택에 있는지 어떻게 아나?</summary>

EXC_RETURN 의 bit2 를 본다: `tst lr, #4` → 0 이면 `mrs r0, msp`, 1 이면 `mrs r0, psp`. 그 r0 가 프레임 주소이고 그 안의 저장된 pc 가 사고 지점이다 (BM-B08 `handlers.c` 에 이미 이 4 줄이 있다, 자세히는 ARM-M12). 그래서 HardFault 핸들러는 **naked asm 으로 lr 을 건드리기 전에** 읽어야 한다 — 이 레슨의 트램펄린과 같은 이유다.

</details>

<details>
<summary>Q4. 비특권 코드가 CONTROL 을 다시 0 으로 쓰면 왜 안 되나? 그럼 어떻게 특권으로 돌아가나?</summary>

특권 분리의 의미가 없어지기 때문이다. 비특권 Thread 의 `MSR CONTROL/MSP/PSP/PRIMASK/BASEPRI` 는 **조용히 무시**된다 (폴트도 안 난다 — 실측 `control after msr = 3`). 돌아가는 유일한 길은 예외다: `svc` → Handler(항상 특권)에서 커널이 요청을 검사하고 `CONTROL.nPRIV` 를 지운 뒤 복귀. 비특권 코드가 SCS(0xE000E000~, 예: ICSR, SysTick)에 접근하면 BusFault 가 난다 (Arm ARM 기준, 이 레슨에서는 실행하지 않음). MPU 와 묶으면 비특권 태스크가 다른 태스크·커널 메모리도 못 건드린다.

</details>

<details>
<summary>Q5. MSR CONTROL 뒤에 ISB 는 왜? 없으면 실제로 깨지나?</summary>

CONTROL 은 "다음 명령부터 어느 SP 를 쓰는가 / 특권인가" 를 바꾼다. MSR 이 끝날 때 파이프라인에 이미 들어온 명령이 옛 설정으로 실행될 수 있으므로 Arm ARM 은 ISB 를 요구한다. Cortex-M3/M4 는 파이프라인이 짧아(3 단) 실제로는 문제가 안 된다는 Arm 앱 노트 설명이 있지만, 규칙은 "넣어라" 이고 M7 등에서는 필요하다 (확인 권장). QEMU 는 명령을 순차로 흉내 내서 차이를 못 보여준다 (`noisb` 실측 32/32).

</details>

<details>
<summary>Q6. xPSR 을 MRS 로 읽으면 T 비트가 0 이다. Cortex-M 이 ARM 상태가 된 건가?</summary>

아니다. EPSR(T, ICI/IT 비트)은 소프트웨어 `MRS` 로는 항상 0 으로 읽히게 정해져 있다. 디버거는 다른 경로로 읽기 때문에 gdb `$xpsr` 에는 0x01000000 이 보인다 (실측: MRS 로는 0x20000000, gdb 로는 0x01000000 / 0x4100000b 처럼 bit24 가 켜져 있음). Cortex-M 은 Thumb 상태만 있고, T = 0 으로 명령을 실행하려 하면 UsageFault(INVSTATE)다. 흔한 원인은 bit0 이 0 인 함수 포인터나 벡터 — `.thumb_func` 를 빠뜨린 asm 심볼 (ARM-M08).

</details>

<details>
<summary>Q7. IPSR 은 어디에 쓰나?</summary>

"지금 ISR 안인가?" 판정. RTOS API 는 ISR 에서 부르면 안 되는 함수(블로킹)와 ISR 판(`...FromISR`)이 나뉘는데, 이걸 assert 하거나 자동으로 고르는 데 IPSR != 0 을 쓴다. 값 자체도 유용하다: Default_Handler 에서 IPSR 을 찍으면 "어느 IRQ 가 핸들러 없이 들어왔나" 가 바로 나온다 (`shared_startup.c` 의 Default_Handler 가 그렇게 한다; IRQn = IPSR − 16).

</details>

<details>
<summary>Q8. AArch64 에도 MSP/PSP 같은 게 있나?</summary>

비슷한 구조가 있다: 예외 레벨마다 SP 가 있고(SP_EL0, SP_EL1, …), EL1 이상에서는 `SPSel` 비트로 "SP_EL0 을 쓸지 자기 SP_ELx 를 쓸지" 고른다. 리눅스 커널은 EL1 에서 SP_EL1 을 쓰고 유저 태스크는 SP_EL0 을 쓴다 — Cortex-M 의 MSP(커널/ISR)·PSP(태스크)와 같은 역할 분담이다. 다른 점은 Cortex-M 이 예외 진입 저장·EXC_RETURN 같은 일을 하드웨어로 하는 반면 AArch64 는 벡터 코드가 직접 저장한다는 것 (ARM-M13, ARM-A02).

</details>

## 9. 추가 연습

### 연습 1 — APSR 보기: CMP 뒤 NZCV, USAT 뒤 Q

`uint32_t nzcv_after_cmp(uint32_t a, uint32_t b)` — `cmp` 직후 `mrs apsr` 로 N Z C V 니블을 돌려준다. `uint32_t q_after_usat8(int32_t x)` — Q 를 지우고 `usat #8` 후 Q(bit27)를 돌려준다. 플래그 계산은 ARCH-B02 참고.

<details>
<summary>답</summary>

`cmp r0, r1` / `mrs r0, apsr` / `lsrs r0, r0, #28`. 기댓값(손으로 계산): (1,2) → N = 0x8, (2,2) → Z C = 0x6, (3,2) → C = 0x2, (0x80000000,1) → C V = 0x3, (0x7FFFFFFF,0xFFFFFFFF) → N V = 0x9, (0xFFFFFFFF,1) → N C = 0xA. Q 는 **스스로 지워지지 않는 sticky 비트**라 `msr apsr_nzcvq, r1`(r1 = 0)로 먼저 지운다 — 이 MSR 은 비특권에서도 허용된다. `usat` 이 300, −5 를 자르면 Q = 1, 100, 255 는 0.

</details>

### 연습 2 — 무시되는 쓰기, 0 으로 읽히는 읽기

main(MSP)을 비특권으로 내린 뒤 `get_msp/get_psp` 를 읽고 `set_psp` 를 시도하고, SVC 로 돌아와서 PSP 가 바뀌었는지 확인하라. 또 SVC 핸들러 안에서 `CONTROL | SPSEL` 을 써 보라.

<details>
<summary>답</summary>

실측: 비특권 `mrs msp/psp` → 둘 다 0 (특권일 때 MSP 는 0x2000ffd8), 비특권 `msr psp` → 무시 (돌아와서 0x20002000 그대로), 핸들러 안 `msr control` 의 SPSEL 쓰기 → 무시 (0 그대로). 앞의 둘은 MRS/MSR 의사코드의 `if CurrentModeIsPrivileged()` 와 맞고, 마지막은 "Handler 모드에서 SPSEL 은 0 으로 읽고 쓰기 무시" 와 맞는다. 비특권 MRS 가 0 인 것은 QEMU 실측이며, 실리콘·Arm ARM 으로 한 번 더 확인 권장. 주의: 비특권 상태에서는 SCS 레지스터 접근과 세미호스팅 출력도 피했다 (출력은 SVC 뒤에).

</details>

### 연습 3 — 스택 칠하기와 최고 수위(high-water mark)

태스크 스택을 0xDEADBEEF 로 칠하고, `burn(depth)`(지역 배열 8 워드 + 재귀)를 PSP 위에서 돌린 뒤 바닥부터 칠이 벗겨진 첫 워드를 찾아 "쓰인 바이트 수" 를 계산하라. 가장 깊은 곳에서 읽은 `get_sp()` 와 비교하라.

<details>
<summary>답</summary>

`stack_high_water()` = (256 − 바닥부터 칠이 그대로인 워드 수) × 4. 실측: depth 2 → 120 B, depth 8 → 360 B, 둘 다 `top − deepest_sp` 와 정확히 같다 → 재귀 한 단계 = (360 − 120) / 6 = 40 B (이 빌드 기준). FreeRTOS `uxTaskGetStackHighWaterMark()`, Zephyr 의 stack sentinel 과 같은 기법이다. 한계: 칠과 같은 값을 쓰면 못 본다, 지역 배열을 건너뛰고 쓰면 과소 측정. M3 에는 PSPLIM 이 없으므로 바닥 워드(canary) 검사나 MPU 가드 영역으로 오버플로를 잡는다.

</details>

### 연습 4 — SysTick 이 PSP 태스크를 끊는다

PSP 태스크 안에서 SysTick 을 켜고 틱이 올 때까지 기다려라. `SysTick_Handler` 트램펄린을 `practice.S` 에 추가해서 IPSR, EXC_RETURN, 핸들러 sp 를 확인하라.

<details>
<summary>답</summary>

`shared_startup.c` 의 `SysTick_Handler` 는 weak alias 라 `practice.S` 의 강한 정의가 이긴다. 실측: `ipsr=15 EXC_RETURN=0xfffffffd sp=0x2000ffd0(=msp) psp=0x200003d0`(태스크 스택 안). SVC 와 똑같이 "PSP 태스크를 끊은 예외" 의 모양이다 — 동기(svc)든 비동기(SysTick)든 하드웨어 동작은 같다. 이 구조에서 PendSV 핸들러가 PSP 를 다른 태스크 것으로 바꾸고 돌아가면 그게 컨텍스트 스위치다 (ARM-A01).

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # practice.c + practice.S(#include "solution.S") + shared_startup.c
```

```text
ARM-B14 practice
  unpriv reads: msp=0x00000000 psp=0x00000000 control=0x00000001 (priv msp=0x2000ffd8)
  handler: control before=0x00000000 after msr(|SPSEL)=0x00000000
  depth 2: high-water=120 B, top-deepest_sp=120 B
  depth 8: high-water=360 B, top-deepest_sp=360 B
  SysTick: ipsr=15 EXC_RETURN=0xfffffffd sp=0x2000ffd0 psp=0x200003d0
20/20 passed
qemu exit=0
```

(PASS 줄 20개 생략.)

## 10. 핵심 정리

- Cortex-M 의 sp(r13)는 **MSP / PSP 중 하나를 가리키는 이름**. Handler 모드 = 항상 MSP. Thread 모드 = `CONTROL.SPSEL`(bit1)이 고른다. 리셋: MSP = vector[0], CONTROL = 0, PSP = UNKNOWN.
- 특수 레지스터는 `MRS`/`MSR` 로만: SYSm 0x08 MSP, 0x09 PSP, 0x14 CONTROL, 0x10 PRIMASK, 0x05 IPSR, 0x03 xPSR. 32비트 Thumb-2 명령.
- PSP 전환 = `msr psp` → `orr SPSEL` → `msr control` → **`isb`**, 스택을 안 쓰는 asm 안에서. C 중간에서 SPSEL 을 바꾸면 그 함수의 스택이 사라진다 (실측 HardFault, BFAR 0xfffffff8).
- 예외: 프레임은 끊긴 쪽 스택에, 핸들러는 MSP 에서. IPSR = 예외 번호(SVC 11, PendSV 14, SysTick 15, IRQn + 16). LR = EXC_RETURN (PSP Thread 0xFFFFFFFD, MSP Thread 0xFFFFFFF9).
- 트램펄린은 `b` 로 — lr(EXC_RETURN)을 지켜야 C 함수의 return 이 예외 복귀가 된다.
- nPRIV(bit0) = Thread 의 특권. 비특권에서 MSR CONTROL/MSP/PSP 는 **무시**, 돌아오는 길은 SVC 뿐. Handler 는 nPRIV 와 무관하게 특권.
- MRS 로 읽은 EPSR(T 비트)은 0, gdb `$xpsr` 에는 보인다. ISR 판정은 IPSR != 0.
- QEMU 는 ISB 누락 같은 파이프라인 버그를 못 보여준다 → 규칙은 문서로 지킨다.

## 참고자료

- ARMv7-M Architecture Reference Manual (DDI 0403) — B1.3 모드·특권, B1.4 레지스터 (SP 뱅킹, xPSR, CONTROL), B1.5 예외 모델 (리셋, 예외 번호, 진입/복귀), B5.2.2 MRS / B5.2.3 MSR 의사코드: <https://developer.arm.com/documentation/ddi0403/latest/>
- Cortex-M3 Devices Generic User Guide (DUI 0552) — Core registers 절 (CONTROL: "In Handler mode this bit reads as zero and ignores writes"), EPSR 읽기 규칙, MRS/MSR 명령: <https://developer.arm.com/documentation/dui0552/latest/>
- Arm AN321 "ARM Cortex-M Programming Guide to Memory Barrier Instructions" — CONTROL 변경 뒤 ISB: <https://developer.arm.com/documentation/dai0321/latest/>
- CMSIS-Core(M) — `__get_MSP/__set_PSP/__get_CONTROL/__set_CONTROL/__get_xPSR/__get_IPSR/__get_PRIMASK`: <https://arm-software.github.io/CMSIS_6/latest/Core/index.html>
- FreeRTOS ARM_CM3 port (`port.c`: `prvPortStartFirstTask`, `vPortSVCHandler`, `xPortPendSVHandler`) — PSP 태스크와 SVC/PendSV 사용 예: <https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/main/portable/GCC/ARM_CM3>
- QEMU Arm system emulator (mps2-an385, 세미호스팅, gdbstub `-s -S`): <https://www.qemu.org/docs/master/system/target-arm.html>
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* — 4장 (레지스터, 특수 레지스터, 스택 두 개)
- 관련 레슨: ARM-B13 (Thumb-2·IT), BM-B08 (벡터 테이블·스타트업·세미호스팅), ARCH-B02 (NZCV), ARM-M05 (ISB), ARM-M08 (Thumb 비트), ARM-M09 (예외 프레임·EXC_RETURN), ARM-M10 (PRIMASK/BASEPRI), ARM-M12 (HardFault), ARM-M13 (AArch64 EL/SPSel), ARM-A01 (PendSV 컨텍스트 스위치), TOOL-A03 (gdb remote)
