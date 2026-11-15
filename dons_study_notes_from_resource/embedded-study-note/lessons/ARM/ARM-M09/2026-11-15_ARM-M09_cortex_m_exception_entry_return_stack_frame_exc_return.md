---
id: ARM-M09
time: 80분
updated: 2026-11-15
---
# ARM-M09 Cortex-M 예외 진입/복귀: 스택 프레임과 EXC_RETURN

## 학습 목표

- 예외 진입 때 하드웨어가 **자동으로 쌓는 8워드 프레임**(r0, r1, r2, r3, r12, LR, PC, xPSR)을 주소까지 그리고, 스택된 PC 가 "어디로 돌아갈지" 를 가리킨다는 것을 gdb 덤프로 보여 준다.
- `tst lr, #4` / `ite eq` / `mrseq r0, msp` / `mrsne r0, psp` 진입 스텁을 써서 프레임이 MSP 에 있는지 PSP 에 있는지 고르고, `b`(bl 아님)로 C 핸들러에 넘긴다.
- 스택된 PC 앞 2바이트(`0xDFnn`)에서 SVC 번호를 꺼내는 **SVC 디스패처**를 쓰고, 반환값을 스택된 r0 에 써서 시스템 콜을 만든다.
- EXC_RETURN 6가지 값(0xFFFFFFF1/F9/FD, 0xFFFFFFE1/E9/ED)을 디코딩하는 함수를 쓰고, 6개 모두 QEMU 에서 실제로 만들어 본다 (FPU 프레임은 mps2-an386 Cortex-M4F).
- 가장자리 동작을 실측으로 설명한다: xPSR bit9 정렬 패딩, 같은 우선순위는 중첩 안 됨, 핸들러/PRIMASK 안의 `svc` → HardFault 승격, lazy FP stacking(FPCCR.LSPACT, FPCAR).

## 1. 문제

"HardFault 났을 때 PC 는 어디서 찾아?", "RTOS 시스템 콜은 SVC 번호를 어떻게 알아?", "EXC_RETURN 0xFFFFFFFD 가 무슨 뜻이야?" — Cortex-M 펌웨어 면접(그리고 실제 크래시 분석)의 단골 질문이다. 답은 전부 **예외 진입 때 하드웨어가 스택에 남기는 프레임**과 **LR 에 들어가는 EXC_RETURN** 에 있다. STM32·nRF52 의 FreeRTOS 포트, Zephyr 의 시스템 콜, SSD 컨트롤러 보조 코어의 fault 로그가 모두 이 두 가지를 읽는다.

ARM-B14 에서는 SVC/PendSV 핸들러 안에서 EXC_RETURN 값을 **찍기만** 했다 (0xFFFFFFFD, 0xFFFFFFF9). 이번에는 그 값을 해석하고, 프레임을 직접 읽고 고쳐서 시스템 콜을 만들고, FPU 가 있는 코어에서 프레임이 26워드로 커지는 것까지 본다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">svc #1 직전 sp 와 하드웨어가 쌓은 프레임 (QEMU mps2-an385 실측, 주소는 위가 높음)</text>
  <text x="20" y="40" fill="currentColor">A1: sp = 0x2000ffa8 (8 정렬)</text>
  <text x="400" y="40" fill="currentColor">B2: sp = 0x2000ffa4 (4 mod 8)</text>
  <line x1="20" y1="54" x2="330" y2="54" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="200" y="68" fill="currentColor" font-size="12">← svc 직전 sp 0x2000ffa8</text>
  <rect x="20" y="74" width="170" height="24" class="f-acc" stroke="currentColor"/>
  <text x="28" y="91" fill="currentColor" font-family="monospace">xPSR 0x61000000</text>
  <rect x="20" y="98" width="170" height="24" class="f-acc" stroke="currentColor"/>
  <text x="28" y="115" fill="currentColor" font-family="monospace">PC   0x00001032</text>
  <rect x="20" y="122" width="170" height="24" class="f-mut" stroke="currentColor"/>
  <text x="28" y="139" fill="currentColor" font-family="monospace">LR   0x1ee71ee7</text>
  <rect x="20" y="146" width="170" height="24" class="f-mut" stroke="currentColor"/>
  <text x="28" y="163" fill="currentColor" font-family="monospace">r12  0xc12c12c1</text>
  <rect x="20" y="170" width="170" height="24" class="f-ok" stroke="currentColor"/>
  <text x="28" y="187" fill="currentColor" font-family="monospace">r3   0xa3a3a3a3</text>
  <rect x="20" y="194" width="170" height="24" class="f-ok" stroke="currentColor"/>
  <text x="28" y="211" fill="currentColor" font-family="monospace">r2   0xa2a2a2a2</text>
  <rect x="20" y="218" width="170" height="24" class="f-ok" stroke="currentColor"/>
  <text x="28" y="235" fill="currentColor" font-family="monospace">r1   0xa1a1a1a1</text>
  <rect x="20" y="242" width="170" height="24" class="f-ok" stroke="currentColor"/>
  <text x="28" y="259" fill="currentColor" font-family="monospace">r0   0xa0a0a0a0</text>
  <text x="196" y="91" fill="currentColor" font-family="monospace" font-size="12">0x2000ffa4</text>
  <text x="196" y="115" fill="currentColor" font-family="monospace" font-size="12">0x2000ffa0</text>
  <text x="196" y="139" fill="currentColor" font-family="monospace" font-size="12">0x2000ff9c</text>
  <text x="196" y="259" fill="currentColor" font-family="monospace" font-size="12">0x2000ff88 ← 핸들러 sp</text>
  <text x="20" y="286" fill="currentColor" font-size="12">PC = svc 다음 명령 (svc 는 0x1030, 0xDF01)</text>
  <text x="20" y="302" fill="currentColor" font-size="12">xPSR: T(bit24)=1, IPSR=0, bit9=0</text>
  <line x1="400" y1="54" x2="710" y2="54" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="520" y="68" fill="currentColor" font-size="12">← svc 직전 sp 0x2000ffa4</text>
  <rect x="400" y="74" width="170" height="24" class="f-warn" stroke="currentColor"/>
  <text x="408" y="91" fill="currentColor" font-family="monospace">(패딩 4 B)</text>
  <rect x="400" y="98" width="170" height="24" class="f-acc" stroke="currentColor"/>
  <text x="408" y="115" fill="currentColor" font-family="monospace">xPSR 0x01000200</text>
  <rect x="400" y="122" width="170" height="24" class="f-acc" stroke="currentColor"/>
  <text x="408" y="139" fill="currentColor" font-family="monospace">PC   0x00001032</text>
  <rect x="400" y="146" width="170" height="96" class="f-mut" stroke="currentColor"/>
  <text x="408" y="190" fill="currentColor" font-family="monospace">LR r12 r3 r2 r1</text>
  <rect x="400" y="242" width="170" height="24" class="f-ok" stroke="currentColor"/>
  <text x="408" y="259" fill="currentColor" font-family="monospace">r0   0xa0a0a0a0</text>
  <text x="576" y="91" fill="currentColor" font-family="monospace" font-size="12">0x2000ffa0</text>
  <text x="576" y="115" fill="currentColor" font-family="monospace" font-size="12">0x2000ff9c</text>
  <text x="576" y="259" fill="currentColor" font-family="monospace" font-size="12">0x2000ff80 (8 정렬)</text>
  <text x="400" y="286" fill="currentColor" font-size="12">xPSR bit9 = 1 → "4 B 패딩 넣었음"</text>
  <text x="400" y="302" fill="currentColor" font-size="12">진입 전 sp = 프레임 + 32 + 4</text>
  <text x="20" y="324" fill="currentColor" font-size="12">두 경우 모두 lr = EXC_RETURN 0xFFFFFFF9 (Thread 로, MSP, 기본 프레임)</text>
</svg>
```

### Part A — 진입 스텁, 프레임 읽기, SVC 디스패처

- `starter.S`: `SVC_Handler`, `SysTick_Handler` 진입 스텁 (각 6명령). EXC_RETURN bit2 로 MSP/PSP 를 골라 `r0 = 프레임 주소`, `r1 = EXC_RETURN` 을 담고 C 함수(`svc_handler_c`, `systick_c`)로 `b`.
- `starter.c`: `uint32_t svc_dispatch(uint32_t *frame)` — 스택된 PC 앞 하프워드의 하위 바이트가 SVC 번호. 0 ADD(r0+r1), 1 XOR4(r0^r1^r2^r3), 2 TICKS, 3 WAIT(핸들러 안에서 틱 대기), 그 밖 0xFFFFFFFF. 결과는 `frame[0]` 에.
- 하네스(주어짐): `svc_sentinel()` 이 r0–r3 = 0xA0A0A0A0…0xA3A3A3A3, r12 = 0xC12C12C1, lr = 0x1EE71EE7 을 넣고 `svc #1` → 8워드가 정확히 이 값인지, 스택된 PC = `svc_sentinel_ret` 라벨, `[PC-2] == 0xDF01`, 프레임 = svc 직전 sp − 32 인지 본다. SysTick 이 asm 루프 `spin_until_change` 를 끊으면 스택된 PC 가 그 루프 안인지도 본다.

### Part B — EXC_RETURN 디코더와 가장자리

- `int decode_exc_return(uint32_t v, int has_fpu, struct exc_info *out)` — 유효하면 1 과 {Handler 복귀?, PSP?, FP 프레임?, 8/26 워드}, 아니면 0. 테스트: 0xFFFFFF00–0xFFFFFFFF 256값 × FPU 유무 전수, 랜덤 10만 개, 0xFFFFFFF5(Handler+PSP) 같은 불가능한 값.
- `uint32_t pre_exception_sp(const uint32_t *frame, uint32_t exc_return)` — 프레임 크기 + xPSR bit9 패딩으로 **예외 직전 sp** 를 되살린다 (크래시 덤프에서 호출자 스택을 이어 읽을 때 필요).
- 실측 가장자리: sp 를 일부러 4 mod 8 로 만들고 svc (bit9 = 1), PSP 태스크에서 svc (0xFFFFFFFD), 같은 우선순위 SysTick 은 SVC 를 못 끊음, SysTick(0x40) > SVCall(0x80) 이면 중첩 (0xFFFFFFF1), SysTick 핸들러 안 `svc` 와 PRIMASK=1 상태 `svc` → **HardFault 로 승격** (HFSR.FORCED).

### Part C — 비특권 태스크의 시스템 콜 + FPU 프레임

- C1: PSP 위 비특권(nPRIV=1) 태스크가 `sys_xor4/sys_ticks/sys_bad` 를 부른다 → 프레임은 태스크 스택에, EXC_RETURN 0xFFFFFFFD, 결과는 PSP 프레임의 r0 로 돌아온다. FreeRTOS-MPU / Zephyr 사용자 모드 시스템 콜의 뼈대다.
- C2 (`-M mps2-an386`, Cortex-M4F): FP 를 안 쓴 Thread → 0xFFFFFFF9, FP 쓴 Thread → **0xFFFFFFE9 + 26워드**, PSP + FP → 0xFFFFFFED, FP 쓴 핸들러 안에서 중첩 → 0xFFFFFFE1. lazy stacking: S0–S15 자리는 잡히지만 핸들러가 FP 명령을 쓸 때까지 **안 채워진다** (실측).

| 파일 | 역할 |
|---|---|
| `harness.c` | 펌웨어 테스트 + C 핸들러(`svc_handler_c`, `systick_c`, `hardfault_c`). starter.c/solution.c 가 `#include` |
| `starter.c` / `solution.c` | C TODO: `decode_exc_return`, `pre_exception_sp`, `svc_dispatch` |
| `starter.S` / `solution.S` | asm TODO: `SVC_Handler`, `SysTick_Handler` 진입 스텁 + 주어진 헬퍼(svc 래퍼, 센티널, run_on_psp, FPU 프로브) |
| `shared_startup.c` / `arm_m09.ld` | 벡터 테이블·Reset·세미호스팅 (ARM-B14 축약판), 링커 스크립트 |
| `run_qemu.sh` | VM 안에서 빌드·실행: `starter|solution|practice|gdb|objdump|bugs` |
| `demo_frame.gdb` / `demo_frame_out.txt` | gdb 세션 스크립트와 실제 출력 |
| `demo_bugs.S` | 7. 실수 재현 (MSP 고정 스텁, `bl` 스텁) |
| `practice.c` / `practice.S` | 추가 연습 답 |

## 2. Problem (English)

> **Cortex-M exception entry/return.** Target: Cortex-M3 (ARMv7-M) on QEMU `mps2-an385`; FP part on `mps2-an386` (Cortex-M4F). Bare metal, semihosting output.
>
> 1. Write Thumb-2 entry stubs `SVC_Handler` and `SysTick_Handler` that pass `(uint32_t *frame, uint32_t exc_return)` to C. The frame may be on MSP or PSP. The C function's `return` must perform the exception return.
> 2. Write `uint32_t svc_dispatch(uint32_t *frame)`: extract the SVC immediate from the instruction before the stacked PC, implement services 0..3 (`ADD`, `XOR4`, `TICKS`, `WAIT`), return results through the stacked r0, unknown numbers → `0xFFFFFFFF`.
> 3. Write `int decode_exc_return(uint32_t v, int has_fpu, struct exc_info *out)` for ARMv7-M (no Security Extension): reject reserved encodings, report return mode, stack, and frame type (8 or 26 words).
> 4. Write `uint32_t pre_exception_sp(const uint32_t *frame, uint32_t exc_return)` that recovers the SP value before the exception, including the xPSR[9] alignment pad.
>
> Example (real run): `svc #1` with r0..r3 = 0xA0A0A0A0..0xA3A3A3A3 from Thread/MSP at sp = 0x2000ffa8 → handler sees `lr = 0xfffffff9`, frame at 0x2000ff88 = `a0a0a0a0 a1a1a1a1 a2a2a2a2 a3a3a3a3 c12c12c1 1ee71ee7 00001032 61000000`, `*(uint16_t *)(0x1032 - 2) = 0xdf01`.
>
> Follow-ups: Why are exactly these 8 registers stacked? What happens if you call `svc` with PRIMASK set, or from a SysTick handler of the same priority? What does xPSR bit 9 mean? What is lazy FP stacking and when are S0–S15 actually written?

## 3. 시작 코드

테스트 하네스 (starter.c / solution.c 가 `#include "harness.c"` 로 같이 쓴다):

<!-- code: harness.c -->

C TODO:

<!-- code: starter.c -->

asm TODO (진입 스텁 두 개) + 주어진 헬퍼:

<!-- code: starter.S -->

공용 스타트업 (ARM-B14 축약판, TODO 없음):

<!-- code: shared_startup.c -->

빌드·실행은 Lima VM `elinux` 안에서 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1). 맥에서 한 줄:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ARM/ARM-M09
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # solution / practice / gdb / objdump / bugs
```

`starter`/`solution` 은 같은 펌웨어를 **두 번** 돈다: `-mcpu=cortex-m3` 로 `-M mps2-an385`, 그리고 `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DHAS_FPU` 로 `-M mps2-an386` (Part C2 가 추가됨). 둘 중 하나라도 실패면 마지막 줄 `qemu exit=1`.

<!-- code: run_qemu.sh -->

처음 실행 결과 (실제 출력, 줄 일부 생략):

```text
ARM-M09 exception entry/return (QEMU mps2-an385, Cortex-M3)
  CCR = 0x00000200
A1 svc #1 (MSP, aligned): EXC_RETURN=0x00000000 frame@0x00000000 sp_before=0x00000000
    r0=0x00000000 r1=0x00000000 r2=0x00000000 r3=0x00000000
    r12=0x00000000 lr=0x00000000 pc=0x00000000 xpsr=0x00000000
FAIL line 207: g_svc.n == 1u && g_svc.ipsr == 11u
FAIL line 208: g_svc.exc_return == 0xFFFFFFF9u
...
B4 equal prio: wait result = 0x002dc6c0
FAIL line 297: r == WAIT_TIMEOUT && after >= 1u
...
FAIL line 334: 0 && "Part C1 skipped: SVC path not working (run_on_psp would not return)"
9/47 passed
[mps2-an385] qemu exit=1
ARM-M09 exception entry/return (QEMU mps2-an386, Cortex-M4F)
...
12/61 passed
[mps2-an386] qemu exit=1
qemu exit=1
```

스텁이 `bx lr` 뿐이라 svc 는 "아무 일 없이" 돌아온다 — r0 이 그대로라 `sys_wait_tick(3000000)` 이 0x002dc6c0(= 3000000)을 돌려준 게 그 증거다. starter 에서도 PASS 하는 줄은 TODO 와 무관한 것들이다: `svc_sentinel_ret − svc_sentinel_svc == 2`, 스텁 결과 "0 은 거부" 검사들, 그리고 **주어진** HardFault 경로를 쓰는 B7(PRIMASK + svc). C1 은 SVC 경로가 안 되면 `sys_priv()` 로 특권을 못 되찾아 `run_on_psp` 가 PSP 에서 `pop {r4, pc}` 를 하게 되므로 **건너뛰고 FAIL 한 줄**만 남긴다 (starter 가 크래시하지 않게).

VM 없이 맥에서 "어셈블은 되나?" 만 확인:

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
clang --target=thumbv7m-none-eabi -c solution.S -o /tmp/ARM-M09_solution.o && /usr/bin/objdump -d /tmp/ARM-M09_solution.o
clang --target=thumbv7em-none-eabihf -mfpu=fpv4-sp-d16 -DHAS_FPU -c solution.S -o /tmp/ARM-M09_fpu.o   # FPU 판도 OK
```

```text
00000000 <SVC_Handler>:
       0: f01e 0f04    	tst.w	lr, #0x4
       4: bf0c         	ite	eq
       6: f3ef 8008    	mrseq	r0, msp
       a: f3ef 8009    	mrsne	r0, psp
       e: 4671         	mov	r1, lr
      10: f7ff bffe    	b.w	0x10 <SVC_Handler+0x10> @ imm = #-0x4
```

(`b.w` 대상이 자기 자신인 것은 오브젝트 단계라 재배치가 아직 안 풀려서다. 링크 후에는 `b.w 41c <svc_handler_c>`.)

## 4. 힌트

<details>
<summary>힌트 1 — 핸들러 첫 명령에서 lr 은 무엇인가</summary>

예외 진입 때 하드웨어는 lr 에 복귀 주소가 아니라 **EXC_RETURN**(0xFFFFFFxx)을 넣는다. 그 bit2 가 "프레임을 어느 스택에 쌓았나" 다. `tst lr, #4` 는 `lr & 4` 로 플래그만 바꾸고(Z = 1 이면 bit2 = 0) 레지스터는 안 바꾼다.

</details>

<details>
<summary>힌트 2 — 왜 b 이고 bl 이 아닌가</summary>

C 함수는 끝에서 `bx lr` 이나 `pop {…, pc}` 로 lr 값으로 돌아간다. lr 이 EXC_RETURN 그대로면 그 "복귀" 가 곧 예외 복귀가 된다. `bl` 은 lr 을 덮어쓴다.

</details>

<details>
<summary>힌트 3 — SVC 번호는 어디에</summary>

스택된 PC(`frame[6]`)는 svc **다음** 명령 주소다. Thumb `svc #imm8` 은 16비트 `0xDF00 | imm8` 이고 리틀엔디언이라 `pc − 2` 바이트가 imm8 이다.

</details>

<details>
<summary>힌트 4 — EXC_RETURN 의 고정 비트</summary>

ARMv7-M 에서 유효한 값은 딱 6개다. bit4·3·2 만 의미가 있고 나머지는 [31:5] = 전부 1, [1:0] = 01 로 고정. 그러니 `v & ~0x1C` 를 한 상수와 비교하면 고정 비트 검사가 끝난다. 그다음 "Handler + PSP" 와 "FPU 없음 + FP 프레임" 을 거른다.

</details>

<details>
<summary>힌트 5 — 진입 전 sp</summary>

프레임 주소 + 32 (FP 프레임이면 104). 그런데 진입 때 sp 가 4 mod 8 이었으면 하드웨어가 4 바이트를 더 내리고 **스택된 xPSR 의 bit9** 에 표시해 둔다. xPSR 은 두 프레임 모두 `frame[7]`.

</details>

## 5. 풀이

### 0. 하드웨어가 예외 진입·복귀 때 하는 일

| 단계 | 내용 | 근거 / 실측 |
|---|---|---|
| 1. 프레임 쌓기 | 끊긴 쪽 스택(Thread 면 SPSEL 이 고른 MSP/PSP, Handler 면 MSP)에 xPSR, PC(복귀 주소), LR, r12, r3, r2, r1, r0 — 8 워드. 메모리 배치는 낮은 주소부터 r0…xPSR 로 고정 (쓰는 **순서**는 구현 자유) | ARMv7-M ARM B1.5.6 Exception entry behavior (확인 권장). 실측 gdb `x/8wx $sp` |
| 2. 정렬 | CCR.STKALIGN = 1 이면 프레임 시작을 8 바이트로 맞추고, 4 바이트를 내렸으면 스택된 xPSR bit9 = 1 | QEMU an385/an386 모두 `CCR = 0x00000200` (STKALIGN = 1) 실측. Cortex-M3 r2p0 이후 리셋값 1, r1p1 이전 0 (확인 권장) |
| 3. FP 확장 | CONTROL.FPCA = 1 이면 S0–S15, FPSCR, 예약 1워드까지 26 워드 공간. lazy 면 공간만 | ARMv7-M B1.5.7, FPCCR. 실측 C2 |
| 4. 레지스터 갱신 | lr ← EXC_RETURN, IPSR ← 예외 번호, Handler 모드(MSP, 특권), PC ← 벡터 | 실측: SVC ipsr=11, SysTick 15, HardFault 3 |
| 5. 복귀 | Handler 모드에서 EXC_RETURN 값을 PC 에 쓰면(`bx lr`, `pop {pc}`, `ldr pc`) 프레임을 꺼내고 bit3/bit2/bit4 대로 모드·스택·FP 복원 | B1.5.8 Exception return behavior |

왜 하필 r0–r3, r12, LR, PC, xPSR 인가: AAPCS 에서 **호출자 저장(caller-saved)** 레지스터가 r0–r3, r12, lr 이다. 이것들을 하드웨어가 저장해 주면 핸들러는 그냥 C 함수여도 된다 — C 함수가 r4–r11 을 쓰면 자기가 push/pop 하기 때문이다. 그래서 Cortex-M 은 "ISR 을 C 로 바로 쓸 수 있는" 구조다 (`__attribute__((interrupt))` 불필요).

### Part A — 진입 스텁과 프레임, SVC 디스패처

**스텁 한 줄씩** (C1 의 비특권 PSP 태스크에서 들어온 svc 기준, gdb 실측 값):

| 명령 | 효과 | lr / r0 / r1 |
|---|---|---|
| (진입) | 하드웨어가 PSP 0x200005a0 에 8 워드, lr ← EXC_RETURN | lr = 0xfffffffd |
| `tst lr, #4` | Z ← ((lr & 4) == 0) → bit2 = 1 이라 Z = 0 | 변화 없음 |
| `ite eq` | 다음 2 명령: 첫째는 EQ, 둘째는 NE 일 때만 | — |
| `mrseq r0, msp` | Z = 0 → **실행 안 됨** | — |
| `mrsne r0, psp` | 실행 → r0 = 0x200005a0 | r0 = 프레임 |
| `mov r1, lr` | 둘째 인자 | r1 = 0xfffffffd |
| `b svc_handler_c` | lr 은 그대로 0xfffffffd | C 의 `bx lr` = 예외 복귀 |

Handler 모드에서 `mrs r0, msp` 와 `mov r0, sp` 는 같다 (Handler 는 항상 MSP). 그래서 "MSP 쪽" 경로는 `mov r0, sp` 로 써도 된다 — CMSIS 예제나 FreeRTOS 의 fault 핸들러가 `tst lr, #4; ite eq; mrseq r0, msp; mrsne r0, psp` 를 쓰는 것은 대칭이 읽기 쉬워서다. 단, 스텁이 **먼저 push 를 하면** MSP 가 내려가 프레임 주소가 어긋나므로, 프레임 주소는 스텁 맨 앞에서 잡는다.

**gdb 로 본 프레임** (`run_qemu.sh gdb`, `demo_frame_out.txt` 에서 발췌):

```text
=== (1) A1: svc #1 직전 (Thread, MSP) ===
=> 0x1030 <svc_sentinel+38>:	svc	1
  r0=0xa0a0a0a0 r1=0xa1a1a1a1 r2=0xa2a2a2a2 r3=0xa3a3a3a3 r12=0xc12c12c1 lr=0x1ee71ee7 sp=0x2000ffa8

=== (2) stepi 한 번 → SVC_Handler 첫 명령. 하드웨어가 8 워드를 쌓았다 ===
=> 0xfc0 <SVC_Handler>:	tst.w	lr, #4
  sp=0x2000ff88 lr(EXC_RETURN)=0xfffffff9 ipsr=11 msp=0x2000ff88 psp=0x00000000
0x2000ff88:	0xa0a0a0a0	0xa1a1a1a1	0xa2a2a2a2	0xa3a3a3a3
0x2000ff98:	0xc12c12c1	0x1ee71ee7	0x00001032	0x61000000
  stacked pc=0x00001032 -> svc_sentinel + 40 in section .text
  [stacked pc - 2] = 0xdf01  (svc #imm8 = 0xDFnn)
```

`stepi` 한 번에 sp 가 0x2000ffa8 → 0x2000ff88 (32 B), lr 이 0x1ee71ee7 → 0xfffffff9 로 바뀌었다. 0x1ee71ee7 은 **스택된 LR** 자리에 고스란히 남아 있다 — 핸들러에서 "끊긴 함수의 lr" 을 알고 싶으면 `frame[5]` 를 본다 (HardFault 분석에서 호출자를 찾는 단서, ARM-M12). 스택된 xPSR 0x61000000 = Z·C 플래그(이전 비교 결과) + T 비트(bit24) — `mrs` 로는 T 가 0 으로 읽히지만(ARM-B14) 프레임에는 진짜 EPSR 이 들어간다.

**비동기 예외도 같은 모양**: A3 에서 SysTick 이 `spin_until_change` 루프를 끊은 순간 (gdb):

```text
  sp=0x2000ff98 lr(EXC_RETURN)=0xfffffff9 ipsr=15 msp=0x2000ff98 psp=0x00000000
0x2000ff98:	0x20000098	0x00000000	0x001f062e	0x00000000
0x2000ffa8:	0x2000ffb8	0x000005f1	0x0000105c	0x61000000
  stacked pc=0x0000105c -> spin_until_change + 16 in section .text
```

스택된 PC 0x105c 는 루프의 `subs r2, #1` — "다음에 실행할 명령" 이다. r2 = 0x001f062e 는 그 순간 남은 반복 횟수, r12 = 0x2000ffb8 은 루프 앞에서 `mov ip, sp` 로 담은 sp. 즉 프레임은 **끊긴 순간의 레지스터 스냅숏**이다. 추가 연습 3 의 PC 샘플링 프로파일러가 이 성질을 쓴다.

**SVC 디스패처**: 핵심은 한 줄 `num = ((const uint8_t *)frame[6])[-2]`. arm-none-eabi-gcc -O2 출력 (발췌):

```text
svc_dispatch:
	push	{r4, r5}
	ldr	r2, [r0, #24]        @ r2 = frame[6] = 스택된 PC
	mov	r3, r0
	ldrb	r0, [r2, #-2]        @ SVC 번호 = [pc-2] 바이트
	cmp	r0, #3
	bhi	.L267                 @ 3 초과 → ENOSYS
	tbb	[pc, r0]              @ switch → 바이트 점프 테이블
```

`frame[0] = …` 은 **스택 메모리**에 쓰는 것이고, 예외 복귀 때 하드웨어가 그 값을 r0 으로 꺼낸다 → 호출자에게는 `sys_add()` 의 반환값. r1–r3 도 같은 방식으로 여러 값을 돌려줄 수 있다 (추가 연습 2 의 64비트 r0:r1).

`run_qemu.sh objdump` 로 본 센티널 헬퍼의 실제 링크 결과 (발췌):

```text
    1018:	f04f 30a0 	mov.w	r0, #2694881440	@ 0xa0a0a0a0
    1028:	f8df c010 	ldr.w	ip, [pc, #16]	@ 103c <svc_sentinel_ret+0xa>
    102c:	f8df e010 	ldr.w	lr, [pc, #16]	@ 1040 <svc_sentinel_ret+0xe>
00001030 <svc_sentinel_svc>:
    1030:	df01      	svc	1
00001032 <svc_sentinel_ret>:
    1032:	b106      	cbz	r6, 1036 <svc_sentinel_ret+0x4>
```

`ldr r0, =0xA0A0A0A0` 이 `mov.w r0, #0xa0a0a0a0` 로 나온 것도 볼 만하다: 바이트 반복 패턴은 Thumb-2 수정 즉치(modified immediate)로 표현되므로 gas 가 리터럴 풀 대신 mov 로 바꿨다. 0xC12C12C1 처럼 안 되는 값만 `ldr.w ip, [pc, #16]` 리터럴 로드다.

### Part B — EXC_RETURN 디코딩과 가장자리 동작

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">EXC_RETURN (ARMv7-M) — 의미 있는 비트는 4·3·2 뿐</text>
  <rect x="10" y="34" width="380" height="30" class="f-mut" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor" font-family="monospace">[31:5] = 1111 … 111 (예약, 전부 1)</text>
  <rect x="390" y="34" width="90" height="30" class="f-acc" stroke="currentColor"/>
  <text x="398" y="54" fill="currentColor" font-family="monospace">4 FType</text>
  <rect x="480" y="34" width="80" height="30" class="f-warn" stroke="currentColor"/>
  <text x="488" y="54" fill="currentColor" font-family="monospace">3 Mode</text>
  <rect x="560" y="34" width="90" height="30" class="f-ok" stroke="currentColor"/>
  <text x="568" y="54" fill="currentColor" font-family="monospace">2 SPSEL</text>
  <rect x="650" y="34" width="80" height="30" class="f-mut" stroke="currentColor"/>
  <text x="660" y="54" fill="currentColor" font-family="monospace">[1:0]=01</text>
  <text x="392" y="82" fill="currentColor" font-size="12">0 = FP 26워드</text>
  <text x="482" y="82" fill="currentColor" font-size="12">1 = Thread</text>
  <text x="562" y="82" fill="currentColor" font-size="12">1 = PSP</text>
  <text x="10" y="112" fill="currentColor" font-weight="bold">유효한 6 값 (이 레슨에서 전부 실측)</text>
  <text x="10" y="134" fill="currentColor" font-family="monospace">값          복귀    스택  프레임   어디서 봤나</text>
  <line x1="10" y1="140" x2="730" y2="140" stroke="currentColor"/>
  <rect x="8" y="145" width="724" height="20" class="f-ok"/>
  <text x="10" y="160" fill="currentColor" font-family="monospace">0xFFFFFFF9  Thread  MSP   8워드    A1 svc, A3 SysTick, B7 HardFault</text>
  <text x="10" y="182" fill="currentColor" font-family="monospace">0xFFFFFFFD  Thread  PSP   8워드    B3 / C1 PSP 태스크의 svc</text>
  <rect x="8" y="189" width="724" height="20" class="f-ok"/>
  <text x="10" y="204" fill="currentColor" font-family="monospace">0xFFFFFFF1  Handler MSP   8워드    B5 중첩 SysTick, B6 승격 HardFault</text>
  <text x="10" y="226" fill="currentColor" font-family="monospace">0xFFFFFFE9  Thread  MSP   26워드   C2 FP 쓴 main 의 svc (an386)</text>
  <rect x="8" y="233" width="724" height="20" class="f-ok"/>
  <text x="10" y="248" fill="currentColor" font-family="monospace">0xFFFFFFED  Thread  PSP   26워드   C2 FP 쓴 PSP 태스크 (an386)</text>
  <text x="10" y="270" fill="currentColor" font-family="monospace">0xFFFFFFE1  Handler MSP   26워드   C2 FP 쓴 SVC 안 중첩 SysTick (an386)</text>
  <text x="10" y="292" fill="currentColor" font-size="12">0xFFFFFFF5 (Handler+PSP) 등 나머지는 예약 → 복귀에 쓰면 INVPC UsageFault (ARM-M12)</text>
</svg>
```

**디코더 논리**: `(v & 0xFFFFFFE3) == 0xFFFFFFE1` 이 고정 비트 검사다 (0xFFFFFFE3 = bit4·3·2 를 뺀 마스크). 통과하면 2³ = 8 조합 중 bit3 = 0 & bit2 = 1 (Handler + PSP) 2개를 빼면 6개, FPU 가 없으면 bit4 = 0 인 3개를 더 빼서 3개. 하네스는 비트 논리와 **독립인 표**(6개 값 목록)를 기준 모델로 써서 256 × 2 전수 + 랜덤 10만 개를 비교했다 — 같은 논리로 기준을 만들면 같은 버그를 두 번 쓴다.

**xPSR bit9 (STKALIGN 패딩)**: `svc_sentinel(1)` 은 `sub sp, #4` 로 sp = 0x2000ffa4 를 만들고 svc 했다. 실측 프레임 0x2000ff80, 스택된 xPSR = 0x01000200 (bit9 = 1). 0x2000ffa4 − 4 − 32 = 0x2000ff80 이므로 `pre_exception_sp` 는 `+32 +4` 로 정확히 0x2000ffa4 를 되살린다. 복귀 때 하드웨어도 같은 계산(`sp = frame + 32 + (xPSR[9] ? 4 : 0)`)을 한다. AAPCS 는 공개 인터페이스에서 sp 8 정렬을 요구하는데, 인터럽트는 함수 **중간**(push 한 개 뒤 등)에서도 들어오므로 하드웨어가 맞춰 주는 것이다.

**같은 우선순위는 중첩 안 됨 (B4)**: 기본 우선순위는 전부 0. SVC 핸들러 안에서 300만 번 돌며 틱을 기다려도 `WAIT_TIMEOUT` — SysTick 이 pending 이지만 "현재 실행 우선순위보다 **엄격히 높아야** 선점" 이라 못 들어온다. svc 가 끝나는 순간 pending 이던 SysTick 이 바로 들어와 `g_ticks ≥ 1` (꼬리 물기, 추가 연습 1).

**중첩 (B5)**: SHPR2[31:24] = 0x80 (SVCall), SHPR3[31:24] = 0x40 (SysTick). 이제 SysTick 이 SVC 를 끊는다. 실측: SysTick 의 lr = **0xfffffff1**, 프레임 0x2000ff60 은 MSP 위 SVC 핸들러 스택 아래, 스택된 xPSR = 0x6100000b (IPSR = 11 → 끊긴 쪽이 SVCall), 스택된 PC 0x3cc 는 `svc_dispatch` 안 (wait 루프가 인라인됨). 0x80/0x40 은 상위 비트만 쓰므로 우선순위 비트가 3개뿐인 실제 AN385 FPGA·STM32F1(4비트)에서도 같은 순서다 (QEMU 는 8비트 구현, BM-M02).

**승격 (B6, B7)**: SVC 는 **동기** 예외라 "나중에" 받을 수 없다. 실행하는 순간 SVCall 우선순위가 현재 실행 우선순위보다 높지 않으면 HardFault 로 바뀐다 (ARMv7-M B1.5.4 priority escalation, 확인 권장).

| 실험 | 실측 HardFault | 의미 |
|---|---|---|
| B6 SysTick(우선순위 0) 핸들러 안에서 `sys_add(1, 2)` | lr = 0xfffffff1, 스택된 xPSR = 0x0100000f (IPSR 15), HFSR = 0x40000000 (FORCED), 스택된 PC = `sys_add+2` | SVC 는 실행 안 됨 → `sys_add` 가 r0 = 1 그대로 반환 |
| B7 `cpsid i` 후 `svc #0` | lr = 0xfffffff9, IPSR 0, 스택된 PC = `svc_primask_ret` | PRIMASK 가 실행 우선순위를 0 으로 올린다 → 같은 결과 |

두 경우 모두 스택된 PC 가 **svc 다음 명령**이라 HardFault 핸들러가 그냥 돌아가면 svc 를 "건너뛴" 것이 된다 (하네스가 그렇게 복구). 실무에서는 "ISR 에서 블로킹 RTOS API 호출" 이나 "크리티컬 섹션 안에서 시스템 콜" 이 정확히 이 HardFault 로 나타난다. FreeRTOS 의 `configASSERT` 들과 `...FromISR` API 가 따로 있는 이유다.

### Part C — 비특권 시스템 콜과 FPU 프레임

**C1**: 태스크가 `CONTROL.nPRIV = 1` 로 내려간 뒤(실측 CONTROL = 3) `sys_xor4(0x12345678, 0x0F0F0000, 0xF0, 1)` = 0x1d3b5689, `sys_ticks()` = 77, `sys_bad()` = 0xFFFFFFFF. 프레임은 매번 태스크 스택(`g_task_stack`) 안, lr = 0xfffffffd. gdb 실측 (svc 를 부른 쪽이 CONTROL = 3 이었는데 핸들러 안 `$control` = 1: nPRIV 는 남고 SPSEL 은 Handler 에서 0 으로 읽힘):

```text
  sp(MSP)=0x2000ffb0 lr=0xfffffffd psp=0x200005a0 control=1
0x200005a0 <g_task_stack+976>:	0x12345678	0x0f0f0000	0x000000f0	0x00000001
0x200005b0 <g_task_stack+992>:	0x00000007	0x00000191	0x00000ff8	0x21000000
```

비특권 코드가 커널 데이터(`g_ticks`)를 "읽게" 해 주는 통로가 SVC 이고, 인자 검증은 핸들러가 한다 — 사용자 태스크가 넘긴 포인터를 그대로 역참조하면 MPU 를 우회하는 구멍이 된다 (Zephyr 의 `K_SYSCALL_MEMORY_*` 검증과 같은 이유).

**C2 — FPU 프레임과 lazy stacking** (mps2-an386, 실측):

```svg
<svg viewBox="0 0 740 350" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="14" fill="currentColor" font-weight="bold">EXC_RETURN 0xFFFFFFE9: 26워드 확장 프레임 (an386 실측, lazy stacking)</text>
  <rect x="20" y="34" width="200" height="24" class="f-mut" stroke="currentColor"/>
  <text x="28" y="51" fill="currentColor" font-family="monospace">reserved</text>
  <text x="228" y="51" fill="currentColor" font-family="monospace" font-size="12">0x2000ffa4</text>
  <text x="20" y="28" fill="currentColor" font-size="12">(위 = 진입 전 sp 0x2000ffa8)</text>
  <rect x="20" y="58" width="200" height="24" class="f-acc" stroke="currentColor"/>
  <text x="28" y="75" fill="currentColor" font-family="monospace">FPSCR</text>
  <text x="228" y="75" fill="currentColor" font-family="monospace" font-size="12">0x2000ffa0</text>
  <rect x="20" y="82" width="200" height="130" class="f-warn" stroke="currentColor"/>
  <text x="28" y="110" fill="currentColor" font-family="monospace">S15 … S0</text>
  <text x="28" y="134" fill="currentColor" font-family="monospace" font-size="12">진입 직후: 0x5a5a5a5a</text>
  <text x="28" y="152" fill="currentColor" font-family="monospace" font-size="12">(공간만, 아직 안 씀)</text>
  <text x="28" y="176" fill="currentColor" font-family="monospace" font-size="12">핸들러가 FP 명령 →</text>
  <text x="28" y="194" fill="currentColor" font-family="monospace" font-size="12">1.0f … 16.0f 채워짐</text>
  <text x="228" y="208" fill="currentColor" font-family="monospace" font-size="12">0x2000ff60 ← FPCAR</text>
  <rect x="20" y="212" width="200" height="60" class="f-ok" stroke="currentColor"/>
  <text x="28" y="236" fill="currentColor" font-family="monospace">xPSR PC LR r12</text>
  <text x="28" y="258" fill="currentColor" font-family="monospace">r3 r2 r1 r0</text>
  <text x="228" y="268" fill="currentColor" font-family="monospace" font-size="12">0x2000ff40 ← 핸들러 sp (frame)</text>
  <text x="480" y="50" fill="currentColor" font-weight="bold">FPCCR (0xE000EF34)</text>
  <text x="480" y="72" fill="currentColor" font-family="monospace" font-size="12">리셋      0xc0000000</text>
  <text x="480" y="90" fill="currentColor" font-family="monospace" font-size="12">  bit31 ASPEN  bit30 LSPEN</text>
  <text x="480" y="112" fill="currentColor" font-family="monospace" font-size="12">핸들러 안 0xc0000019</text>
  <text x="480" y="130" fill="currentColor" font-family="monospace" font-size="12">  bit0 LSPACT = 1 (저장 보류 중)</text>
  <text x="480" y="148" fill="currentColor" font-family="monospace" font-size="12">  bit3 THREAD  bit4 HFRDY</text>
  <rect x="476" y="166" width="256" height="98" class="f-mut" stroke="currentColor"/>
  <text x="484" y="186" fill="currentColor" font-size="12">LSPEN=1: FP 명령 때 저장 (lazy)</text>
  <text x="484" y="206" fill="currentColor" font-size="12">LSPEN=0: 진입 때 바로 저장</text>
  <text x="484" y="226" fill="currentColor" font-size="12">ASPEN=1: FP 쓰면 FPCA 자동 1</text>
  <text x="484" y="246" fill="currentColor" font-size="12">FPCA=0 이면 8워드 (0xFFFFFFF9)</text>
  <text x="20" y="298" fill="currentColor" font-size="12">0x2000ffa8 − 104 = 0x2000ff40 (26 워드).</text>
  <text x="20" y="316" fill="currentColor" font-size="12">핸들러가 s0 = 0xDEADBEEF 로 망가뜨려도 복귀 후 Thread 의 s0 = 1.0f (프레임에서 복원).</text>
  <text x="20" y="334" fill="currentColor" font-size="12">FPSCR 자리 = 0x02c00000 (Thread 가 넣은 DN·RMode=RZ 값 그대로).</text>
</svg>
```

| 실험 (an386) | EXC_RETURN | 프레임 | S0 자리 (진입 직후 → FP 명령 후) |
|---|---|---|---|
| FPCA 를 지우고 svc | 0xFFFFFFF9 | 32 B | — |
| s0–s15 = 1.0…16.0 후 svc, LSPEN = 1, 핸들러 FP 안 씀 | 0xFFFFFFE9 | 104 B, FPCAR = 프레임 + 32 | 0x5a5a5a5a (안 씀) |
| 같은 조건 + 핸들러가 `vmov s0, r0` | 0xFFFFFFE9 | 104 B | 0x5a5a5a5a → 0x3f800000 |
| LSPEN = 0 | 0xFFFFFFE9 | 104 B | 0x3f800000 (진입 때 저장), LSPACT = 0 |
| PSP 태스크가 FP 쓴 뒤 svc | 0xFFFFFFED | 태스크 스택 | — |
| FP 쓴 SVC 핸들러를 SysTick 이 중첩 | 0xFFFFFFE1 | MSP | — |

lazy stacking 의 목적은 **FP 를 안 쓰는 ISR 의 진입 지연을 기본 프레임 수준으로** 유지하는 것이다 (18워드 저장을 생략; 사이클 수는 칩·메모리에 따라 다르므로 수치 주장은 하지 않는다). 공간은 미리 잡아 두므로 나중에 저장해도 스택 레이아웃이 바뀌지 않고, FPCAR 이 그 자리를 기억한다. 비용: ISR 안 첫 FP 명령이 갑자기 느려지고, 그 명령이 MemManage/BusFault(스택 오버플로)를 낼 수 있다 — 그래서 FPCCR 에 MMRDY/BFRDY/HFRDY("그 fault 를 pending 할 수 있었나") 비트가 있다 (확인 권장).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

<!-- code: solution.S diff=starter.S -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"
```

실제 출력 (PASS 줄과 일부 덤프 줄 생략):

```text
ARM-M09 exception entry/return (QEMU mps2-an385, Cortex-M3)
  CCR = 0x00000200
A1 svc #1 (MSP, aligned): EXC_RETURN=0xfffffff9 frame@0x2000ff88 sp_before=0x2000ffa8
    r0=0xa0a0a0a0 r1=0xa1a1a1a1 r2=0xa2a2a2a2 r3=0xa3a3a3a3
    r12=0xc12c12c1 lr=0x1ee71ee7 pc=0x00001032 xpsr=0x61000000
A3 SysTick (thread, MSP): EXC_RETURN=0xfffffff9 frame@0x2000ff98 sp_before=0x2000ffb8
B2 svc #1 (sp = 4 mod 8): EXC_RETURN=0xfffffff9 frame@0x2000ff80 sp_before=0x2000ffa4
    r12=0xc12c12c1 lr=0x1ee71ee7 pc=0x00001032 xpsr=0x01000200
B3 PSP task: EXC_RETURN = 0xfffffffd
B4 equal prio: wait result = 0xfffffffe
B5 SysTick nested in SVC: EXC_RETURN=0xfffffff1 frame@0x2000ff60 sp_before=0x2000ff80
    r12=0x20000068 lr=0x00000433 pc=0x000003cc xpsr=0x6100000b
B6 svc inside SysTick -> HardFault: EXC_RETURN=0xfffffff1 frame@0x2000ff70 sp_before=0x2000ff90
    r12=0x2000ffb8 lr=0x0000026b pc=0x00000ff4 xpsr=0x0100000f
   HFSR = 0x40000000
C1 user task: control = 0x00000003
   xor4 = 0x1d3b5689
51/51 passed
[mps2-an385] qemu exit=0
ARM-M09 exception entry/return (QEMU mps2-an386, Cortex-M4F)
...
C2 FPCCR at reset = 0xc0000000
C2 lazy (LSPEN=1), no FP in handler: EXC_RETURN=0xffffffe9 frame@0x2000ff40 sp_before=0x2000ffa8
   FPCCR in handler = 0xc0000019
   FPCAR in handler = 0x2000ff60
   frame[8] (S0 slot) = 0x5a5a5a5a
C2 lazy, handler touched FP: frame[8] = 0x3f800000
   FPSCR slot = 0x02c00000
C2 LSPEN=0: frame[8] = 0x3f800000
C2 PSP task with FP: EXC_RETURN = 0xffffffed
C2 SysTick nested in FP-using SVC: EXC_RETURN = 0xffffffe1
65/65 passed
[mps2-an386] qemu exit=0
qemu exit=0
```

gdb 세션 스크립트 (`run_qemu.sh gdb` — `-s -S` 로 멈춘 QEMU 에 `gdb-multiarch -batch` 로 붙는다; 전체 출력은 `demo_frame_out.txt`):

<!-- code: demo_frame.gdb -->

B5·B6 지점의 실제 gdb 출력:

```text
=== (5) B5: SVC 핸들러 안에서 중첩된 SysTick (lr == 0xfffffff1) ===
  sp=0x2000ff60 lr(EXC_RETURN)=0xfffffff1 ipsr=15 msp=0x2000ff60 psp=0x200005d0
0x2000ff60:	0x00000003	0x00000000	0x002c4d2d	0x2000ff98
0x2000ff70:	0x20000068	0x00000433	0x000003ca	0x6100000b
  stacked pc=0x000003ca -> svc_dispatch + 30 in section .text

=== (6) B6: SysTick 핸들러 안 svc → HardFault ===
  sp=0x2000ff70 lr(EXC_RETURN)=0xfffffff1 ipsr=3 msp=0x2000ff70 psp=0x200005d0
0x2000ff70:	0x00000001	0x00000002	0x001ce416	0x00000000
0x2000ff80:	0x2000ffb8	0x0000026b	0x00000ff4	0x0100000f
  stacked pc=0x00000ff4 -> sys_add + 2 in section .text
  HFSR=0x40000000
```

(gdb 판과 일반 실행판의 루프 안 PC 가 조금 다른 것은 틱이 루프의 다른 명령에서 들어왔기 때문 — 정상.) psp = 0x200005d0 은 B3 태스크가 남긴 값이다: 예외는 PSP 를 쓰지 않는 한 PSP 를 건드리지 않는다.

## 7. 자주 하는 실수

**1. 스택 선택 없이 항상 MSP** — 베어메탈(PSP 안 씀)에서는 잘 돌다가 RTOS 를 올리는 순간 터진다. `run_qemu.sh bugs` 의 BUG=1 (`demo_bugs.S`) 실측:

```text
B3 PSP task: EXC_RETURN = 0xfffffffd
FAIL line 286: t_sum == 107u && t_er == 0xFFFFFFFDu
FAIL line 287: on_task_stack(t_frame) && g_svc.psp == t_frame
...
unexpected HardFault, HFSR=0x40000000 CFSR=0x00020000 stacked PC=0x00000003
qemu exit=3
```

MSP 쪽 "프레임" 은 `run_on_psp` 가 push 한 r4/lr 같은 엉뚱한 값이라 SVC 번호도 틀리고, `frame[0] = 결과` 가 **MSP 의 저장된 레지스터를 덮어쓴다**. 결국 C1 에서 PC = 0x3 으로 점프 → INVSTATE(CFSR bit17) → HardFault. 고친 코드 = `tst lr, #4; ite eq; mrseq r0, msp; mrsne r0, psp`.

**2. `b` 대신 `bl`** — BUG=2 실측:

```text
unexpected HardFault, HFSR=0x40000000 CFSR=0x00000001 stacked PC=0xa3a3a3a2
qemu exit=3
```

`bl` 이 lr 의 EXC_RETURN 을 지운다. C 가 돌아온 뒤의 `bx lr` 은 예외 복귀가 아니라 C 가 남긴 아무 lr 로의 점프 → Handler 모드에 갇힌 채 쓰레기를 실행하다 PC = 0xa3a3a3a2 (센티널 r3 값!)에서 IACCVIOL. 꼭 `bl` 을 써야 하면 `push {r4, lr}` … `pop {r4, pc}` 로 EXC_RETURN 을 지킨다.

**3. 프레임을 나중에 읽기** — 이 레슨을 쓰면서 실제로 낸 버그다. 처음 하네스는 `pre_exception_sp(g_svc.frame_addr, …)` 를 **핸들러가 끝난 뒤** main 에서 불렀다 → B2 FAIL. 그 주소의 xPSR 은 이미 다른 함수 호출이 덮어쓴 스택이었다 (aligned 경우는 bit9 = 0 이 우연히 맞아 PASS). 고친 코드: 핸들러 안 `record()` 에서 계산해 저장. 크래시 덤프도 같다 — 프레임은 **핸들러 안에서** 복사해 둬야 한다 (BM-A09 의 .noinit 기록).

**4. 핸들러/크리티컬 섹션 안에서 시스템 콜** — B6/B7 처럼 HardFault 로 승격된다. 고친 코드: ISR 에서는 SVC 를 거치지 않는 `...FromISR` 경로, PRIMASK 대신 BASEPRI 로 SVCall 은 열어 두기 (ARM-M10).

**5. M4F 에서 프레임이 항상 32 B 라고 가정** — FP 를 한 번이라도 쓴 컨텍스트면 104 B 다. `frame[6]`·`frame[7]` 위치는 같아서 PC 읽기는 우연히 맞지만, 진입 전 sp 나 호출자 스택 추적은 72 B 어긋난다. 고친 코드: EXC_RETURN bit4 로 크기 결정 (`pre_exception_sp`). 또 CPACR(CP10/CP11)을 안 켠 채 FP 명령 → NOCP UsageFault — `shared_startup.c` 가 Reset 에서 켠다.

**6. 스택된 PC 의 bit0 을 1 로 쓰기** — 추가 연습 4 처럼 복귀 주소를 바꿀 때 `frame[6] = fn` (Thumb 함수 포인터 = 홀수)이라고 쓰면 안 된다. 프레임의 PC 는 bit0 = 0 이고 Thumb 상태는 xPSR.T 가 맡는다 (ARMv7-M 은 홀수 PC 복귀를 UNPREDICTABLE 로 규정 — 확인 권장). `frame[6] = fn & ~1u`.

## 8. 면접 Q&A

<details>
<summary>Q1. Cortex-M 예외 진입 때 하드웨어가 스택에 쌓는 것은? 왜 그것만?</summary>

r0, r1, r2, r3, r12, LR, PC(복귀 주소), xPSR — 8 워드, 낮은 주소부터 이 순서. AAPCS 의 caller-saved 레지스터 전부라서 핸들러를 보통 C 함수로 쓸 수 있다 (r4–r11 은 C 함수가 필요하면 스스로 저장). FPU 문맥이 살아 있으면(CONTROL.FPCA = 1) S0–S15, FPSCR, 예약 1워드가 더해져 26 워드. 꼬리질문 "r4–r11 은 누가?" → 일반 ISR 은 컴파일러, RTOS 컨텍스트 스위치는 PendSV 에서 직접 `stmdb` (ARM-A01).

</details>

<details>
<summary>Q2. 핸들러는 프레임이 MSP 에 있는지 PSP 에 있는지 어떻게 아나? 그냥 sp 를 쓰면?</summary>

EXC_RETURN(진입 직후 lr)의 bit2. 0 이면 MSP, 1 이면 PSP. Handler 모드의 sp 는 항상 MSP 라서, PSP 태스크가 끊긴 경우 sp 는 프레임이 아니다 — 7. 실수 1 처럼 엉뚱한 값을 읽고, 더 나쁘게는 MSP 의 다른 데이터를 덮어쓴다. 스텁이 push 를 하기 **전에** 골라야 한다.

</details>

<details>
<summary>Q3. 0xFFFFFFF9 같은 값을 pc 에 넣으면 왜 "복귀" 가 되나? 6개 값의 뜻은?</summary>

Handler 모드에서 0xF 로 시작하는 EXC_RETURN 값이 PC 로 로드되면(`bx`, `pop {pc}`, `ldr pc`, `ldm`) 코어가 점프 대신 예외 복귀를 한다. 0xF0000000 이상은 실행 불가(XN) 영역이라 진짜 코드 주소와 겹치지 않는다. bit3 = Thread/Handler, bit2 = PSP/MSP, bit4 = 0 이면 FP 확장 프레임: F1 Handler·MSP, F9 Thread·MSP, FD Thread·PSP, E1/E9/ED 는 FP 판. 꼬리질문 "0xFFFFFFF5 는?" → 예약값, 복귀 시 INVPC UsageFault.

</details>

<details>
<summary>Q4. SVC 번호는 어떻게 꺼내나? Linux 처럼 레지스터로 넘기는 방식과 비교하면?</summary>

스택된 PC 는 svc 다음 명령이므로 `((uint8_t *)frame[6])[-2]` 가 Thumb `svc #imm8` 의 imm8 (인코딩 0xDFnn, 리틀엔디언). FreeRTOS-MPU 의 ARMv7-M 포트가 이 방식이다 (확인 권장). 대안: Linux ARM EABI 처럼 `svc #0` + 번호를 r7 에. r7 은 프레임에 없으므로 스텁이 직접 넘겨야 하지만, 코드를 데이터로 읽지 않아도 된다 — I/D 분리 캐시나 실행 전용(execute-only) 메모리에서는 그쪽이 안전하다. 레지스터 방식은 번호를 런타임에 정할 수도 있다.

</details>

<details>
<summary>Q5. ISR 안에서, 또는 __disable_irq() 상태에서 svc 를 부르면?</summary>

HardFault. SVC 는 동기 예외라 미룰 수 없는데, SVCall 우선순위가 현재 실행 우선순위보다 높지 않으면(같은 우선순위 ISR 안, PRIMASK = 1 이면 실행 우선순위 0) 선점할 수 없어 HardFault 로 승격된다. 이 레슨 실측: HFSR = 0x40000000 (FORCED), 스택된 PC = svc 다음 명령, 끊긴 쪽 IPSR = 15 또는 0. 그래서 RTOS 는 ISR 용 API 를 따로 두고, 크리티컬 섹션을 PRIMASK 대신 BASEPRI 로 만든다 (ARM-M10).

</details>

<details>
<summary>Q6. 스택된 xPSR 의 bit9 는? 언제 중요한가?</summary>

진입 때 sp 를 8 바이트로 맞추느라 4 바이트 패딩을 넣었다는 표시 (CCR.STKALIGN = 1 일 때; QEMU 리셋값 CCR = 0x200). 복귀 때 하드웨어가 이것을 보고 sp 를 되돌린다. 크래시 분석기가 "예외 직전 sp" 를 계산해 호출자 스택을 이어 읽을 때 이 4 바이트를 빼먹으면 모든 스택 슬롯이 한 칸 어긋난다. 실측: sp 0x2000ffa4 → 프레임 0x2000ff80, xPSR 0x01000200.

</details>

<details>
<summary>Q7. lazy FP stacking 이 뭔가? 장단점은?</summary>

FPCA = 1 인 컨텍스트에서 예외가 오면 26 워드 공간은 잡지만 S0–S15/FPSCR 은 바로 쓰지 않고 FPCCR.LSPACT = 1, FPCAR = 그 자리 주소로 표시만 한다. 핸들러가 FP 명령을 처음 실행할 때 하드웨어가 그 자리에 저장하고, 끝까지 안 쓰면 저장·복원을 통째로 건너뛴다. 실측: 진입 직후 S0 자리 0x5a5a5a5a, `vmov s0, r0` 뒤 0x3f800000. 장점: FP 를 안 쓰는 ISR 의 지연·스택 트래픽 절약. 단점: ISR 첫 FP 명령의 지연이 들쭉날쭉, 그때 스택 fault 가 날 수 있음, RTOS 컨텍스트 스위치가 EXC_RETURN bit4 를 보고 S16–S31 저장 여부를 정해야 함.

</details>

<details>
<summary>Q8. tail-chaining 과 late-arrival 은?</summary>

tail-chaining: 핸들러가 끝날 때 다른 예외가 pending 이면 Thread 로 돌아가 프레임을 꺼냈다 다시 쌓는 대신 바로 다음 핸들러로 간다. 추가 연습 1 실측: SVC 안에서 pend 한 PendSV 가 SVC 직후, Thread 보다 먼저 실행되고, 같은 프레임 주소(0x2000ffb0)를 쓰며 lr = 0xFFFFFFF9 (Thread 로 돌아갈 것이므로 F1 이 아님). late-arrival: 프레임을 쌓는 도중 더 급한 예외가 오면 쌓기는 그대로 두고 그 예외의 벡터를 먼저 실행. 둘 다 사이클 최적화라 QEMU 에서 타이밍은 못 보고, 관찰 가능한 결과(순서·프레임·EXC_RETURN)만 같다.

</details>

<details>
<summary>Q9. ARMv8-M(Cortex-M33)의 EXC_RETURN 은 같은가?</summary>

아니다. 상위 바이트가 0xFF 이고 bit6 S(보안 스택), bit5 DCRS(callee 레지스터 기본 규칙), bit0 ES(보안 상태) 같은 TrustZone 비트가 추가되고, bit4/3/2 의 뜻은 유지된다 (확인 권장). 그래서 이 레슨의 `decode_exc_return` 은 "ARMv7-M, 보안 확장 없음" 으로 범위를 명시했다. 이식 코드는 0xFFFFFFFD 같은 상수 비교 대신 비트 검사를 써야 한다.

</details>

## 9. 추가 연습

### 연습 1 — 꼬리 물기 vs 중첩 (PendSV)

SVC 핸들러(svc #6) 안에서 PendSV 를 pend 하라 (`ICSR.PENDSVSET`). 같은 우선순위일 때와 PendSV(0x40) > SVCall(0x80) 일 때 실행 순서, PendSV 의 EXC_RETURN, 프레임 주소를 비교하라.

<details>
<summary>답</summary>

같은 우선순위: 순서 SVC 진입 → SVC 끝 → PendSV → Thread, PendSV lr = 0xFFFFFFF9, 프레임 주소 = SVC 프레임 주소(실측 둘 다 0x2000ffb0), 스택된 PC 도 같다 → **프레임 재사용**. PendSV 가 더 급하면: SVC 진입 → PendSV → SVC 끝 → Thread, lr = 0xFFFFFFF1, 프레임 0x2000ff88 (SVC 핸들러 스택 아래). RTOS 가 컨텍스트 스위치를 **가장 낮은 우선순위** PendSV 에 두는 이유가 앞의 경우다: 다른 모든 핸들러가 끝난 뒤, Thread 로 돌아가기 직전에 실행된다 (ARM-A01).

</details>

### 연습 2 — 64비트를 돌려주는 시스템 콜

`uint64_t sys_mul64(uint32_t a, uint32_t b)` (svc #4) 를 만들어라.

<details>
<summary>답</summary>

AAPCS 에서 uint64_t 반환은 r0(하위):r1(상위). 핸들러가 `frame[0] = (uint32_t)p; frame[1] = (uint32_t)(p >> 32);` 하면 복귀 때 하드웨어가 둘 다 꺼낸다. 래퍼는 `svc #4; bx lr` 그대로. 0xFFFFFFFF² = 0xFFFFFFFE00000001 확인. 같은 이치로 r0–r3 네 개까지 돌려줄 수 있다 (구조체 반환은 메모리 경유).

</details>

### 연습 3 — PC 샘플링 프로파일러

SysTick(reload 3000)을 켜고 asm 루프 `busy_a`, `busy_b` 를 차례로 돌리며 SysTick 핸들러가 스택된 PC 를 주소 범위로 분류하라.

<details>
<summary>답</summary>

`systick_c` 가 `frame[6]` 을 `[busy_a & ~1, busy_a_end)` / `[busy_b & ~1, busy_b_end)` / 기타로 센다. 실측 한 번: busy_a 동안 a = 58, b = 0, 기타 0 / busy_b 동안 a = 0, b = 27, 기타 1 (개수는 QEMU 속도·호스트 부하에 따라 매번 다르다 — 테스트는 "≥ 3, 섞임 0, 기타 ≤ 3" 만 검사). 함수 포인터의 bit0(Thumb)을 지우고 비교해야 한다. 실제 MCU 에서는 SysTick 대신 DWT PC 샘플링이나 SWO 를 쓰지만, "프레임의 PC = 끊긴 지점" 이라는 원리는 같다.

</details>

### 연습 4 — 복귀 주소 바꿔치기 (함수 끼워 넣기)

`sys_inject(fn, arg)` (svc #5): 예외에서 돌아가면 Thread 가 먼저 `fn(arg)` 를 실행하고, fn 이 끝나면 svc 다음 명령으로 돌아오게 하라.

<details>
<summary>답</summary>

`frame[5] = frame[6] | 1` (fn 의 lr = 원래 복귀 지점, Thumb 비트 필수), `frame[6] = fn & ~1` (프레임 PC 는 bit0 = 0), `frame[0] = frame[1]` (인자). 래퍼는 자기 lr 이 덮어써지므로 `push {r4, lr}` 로 먼저 지킨다 (8 B → fn 진입 sp 8 정렬 유지). 실측: fn 이 본 lr = 0x5f7 = `sys_inject_ret | 1`, MSP 균형 유지. POSIX 시그널 전달, RTOS 의 첫 태스크 시작("가짜 프레임을 만들어 예외 복귀로 점프", ARM-A01) 이 이 기법이다.

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # practice.c + practice.S(#include "solution.S") + shared_startup.c, an385
```

```text
ARM-M09 practice
  tail-chain: PendSV EXC_RETURN = 0xfffffff9
  tail-chain: SVC frame = 0x2000ffb0
  tail-chain: PendSV frame = 0x2000ffb0
  nested: PendSV EXC_RETURN = 0xfffffff1
  nested: PendSV frame = 0x2000ff88
  profile busy_a: a=58 b=0 other=0
  profile busy_b: a=0 b=27 other=1
  inject: lr seen by injected() = 0x000005f7
13/13 passed
[mps2-an385] qemu exit=0
qemu exit=0
```

(PASS 줄 13개 생략.)

## 10. 핵심 정리

- 예외 진입 = 하드웨어가 끊긴 쪽 스택에 **r0 r1 r2 r3 r12 LR PC xPSR** (낮은 주소부터) 8 워드를 쌓고, lr ← EXC_RETURN, IPSR ← 예외 번호. FPCA = 1 이면 26 워드.
- 스택된 PC = 복귀할 명령 (SVC·IRQ 는 다음 명령). 스택된 LR = 끊긴 함수의 lr. 스택된 xPSR 에는 T 비트와 끊긴 쪽 IPSR, bit9 패딩 표시.
- 스텁: `tst lr,#4 / ite eq / mrseq r0,msp / mrsne r0,psp / mov r1,lr / b handler_c` — 프레임은 push 전에, 이동은 `b`.
- EXC_RETURN: F9 Thread·MSP, FD Thread·PSP, F1 Handler·MSP, E9/ED/E1 = FP 판. 고정 비트 `(v & 0xFFFFFFE3) == 0xFFFFFFE1`, Handler+PSP 없음.
- SVC 번호 = `((uint8_t *)frame[6])[-2]`, 반환값 = `frame[0]` 에 쓰기.
- 진입 전 sp = 프레임 + 32/104 + (xPSR[9] ? 4 : 0). 프레임은 핸들러 안에서 읽어라.
- 같은 우선순위는 선점 못 함, `svc` 를 ISR/PRIMASK 안에서 → HardFault(FORCED).
- lazy stacking: 자리만 잡고(FPCAR, LSPACT) 첫 FP 명령 때 저장. LSPEN = 0 이면 진입 때 저장.

## 참고자료

- ARMv7-M Architecture Reference Manual (DDI 0403) — B1.5.6 Exception entry behavior, B1.5.7 Stack alignment on exception entry, B1.5.8 Exception return behavior (EXC_RETURN 표), B1.5.4 priority escalation, FP 확장과 FPCCR/FPCAR (절 번호는 기억 기준, 확인 권장): <https://developer.arm.com/documentation/ddi0403/latest/>
- Cortex-M3 Devices Generic User Guide (DUI 0552) — Exception entry and return, EXC_RETURN 값, CCR.STKALIGN: <https://developer.arm.com/documentation/dui0552/latest/>
- Cortex-M4 Devices Generic User Guide (DUI 0553) — FPCCR, FPCAR, CPACR: <https://developer.arm.com/documentation/dui0553/latest/>
- Arm AN298 "Cortex-M4(F) Lazy Stacking and Context Switching": <https://developer.arm.com/documentation/dai0298/latest/>
- ARMv8-M Architecture Reference Manual (DDI 0553) — v8-M EXC_RETURN (S/DCRS/ES 비트) 비교용: <https://developer.arm.com/documentation/ddi0553/latest/>
- FreeRTOS ARM_CM3 / ARM_CM4F / ARM_CM3_MPU 포트 (`vPortSVCHandler`, `xPortPendSVHandler`, SVC 번호 추출): <https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/main/portable/GCC>
- QEMU Arm system emulator (mps2-an385/an386, 세미호스팅, gdbstub): <https://www.qemu.org/docs/master/system/target-arm.html>
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* — 8장 (예외 처리 세부: 스택 프레임, EXC_RETURN, tail-chaining, late-arrival), 13장 (FPU, lazy stacking)
- 관련 레슨: ARM-B13 (Thumb-2·IT), ARM-B14 (MSP/PSP/CONTROL, run_on_psp), BM-B08 (벡터 테이블), BM-M02 (우선순위), ARM-M08 (Thumb 비트), ARM-M10 (PRIMASK/BASEPRI), ARM-M12 (HardFault 분석), ARM-A01 (PendSV 컨텍스트 스위치), BM-A09 (크래시 덤프 .noinit)
