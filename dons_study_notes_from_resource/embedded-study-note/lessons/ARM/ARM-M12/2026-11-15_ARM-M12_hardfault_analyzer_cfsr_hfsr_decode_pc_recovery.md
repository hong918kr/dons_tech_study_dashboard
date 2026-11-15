---
id: ARM-M12
time: 85분
updated: 2026-11-15
---
# ARM-M12 HardFault 분석기: CFSR/HFSR 디코딩과 PC 복원

## 학습 목표

- fault 가 나는 순간 하드웨어가 하는 일(8 워드 프레임을 **그때 쓰던 스택**에 쌓고 LR 에 EXC_RETURN)을 그리고, 그 프레임을 찾아 C 로 넘기는 **어셈블리 진입부**(`tst lr, #4` → `mrs msp/psp`)를 직접 쓴다.
- CFSR(MMFSR/BFSR/UFSR)·HFSR 의 모든 이름 있는 비트를 디코딩하고, MMFAR/BFAR 는 **VALID 비트가 있을 때만** 믿는 디코더를 만들어 단일 비트 64 가지 전수 + 무작위 5000 건으로 시험한다.
- 0 나눗셈·비정렬·잘못된 주소·undefined 명령(16/32 비트)·XN 실행·Thumb 비트 누락 fault 를 QEMU 에서 실제로 일으키고, **원인 비트 + 쌓인 PC == 그 명령의 라벨**을 테스트로 확인한다. 세분 핸들러를 끈 경우(HardFault FORCED)와 켠 경우 둘 다.
- 한 부팅에서 여러 fault 를 보려고 "그 명령을 건너뛰고 복귀" 하는 법(Thumb-2 길이 규칙, `xPSR.T`, IT 상태)과, 그게 **시험 하네스에서만** 괜찮은 이유를 말한다.
- gdb 의 `bt` 가 핸들러 안에서 무엇을 보여 주고 무엇을 못 보여 주는지 실측하고, 쌓인 PC/LR 로 크래시 지점을 직접 복원한다 (`info symbol`, `addr2line` 의 Thumb 비트 함정 포함).

## 1. 문제

Cortex-M 펌웨어 면접의 단골 질문은 "HardFault 났을 때 어디서 죽었는지 어떻게 찾아요?" 이다 — 답은 "CFSR 보고, 스택 프레임에서 PC 꺼내요" 한 줄인데, 꼬리질문(MSP 인지 PSP 인지 어떻게 알아? FORCED 면? BFAR 이 언제 믿을 만해? PC 가 0xE0000000 이면?)에서 실력이 갈린다. Don 이 SSD 컨트롤러에서 assert 덤프의 PC/LR 로 원인 함수를 찾던 일을 Cortex-M 의 fault 하드웨어로 하는 것이다.

이 레슨은 **분석기**(analyzer)를 만든다. fault 가 나면 어떤 상태로 가고 무엇을 기록하고 언제 리셋할지 같은 **정책**(policy)은 BM-M13 의 몫이라 여기서는 다루지 않는다. 대신 BM-M13 이 실측한 QEMU 사실(0x30000000 = 정밀 BusFault, 트랩을 끄면 0 나눗셈은 0, LDRD 비정렬은 항상 fault)을 재사용하고, 그 위에 비트 전체 해독 + PC 복원 + 한 부팅 안의 반복 시험을 올린다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">fault 한 번의 흐름 (값은 이 레슨 QEMU 실측: B0 의 LDRD fault)</text>
  <rect x="10" y="32" width="200" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="52" fill="currentColor">Thread 코드 (MSP 또는 PSP)</text>
  <text x="20" y="70" fill="currentColor" font-family="monospace">0x10a6: ldrd r0, r1, [r0]</text>
  <text x="20" y="84" fill="currentColor">r0 = 0x20000006 (4 정렬 아님)</text>
  <path d="M210 60 L250 60" stroke="currentColor"/><path d="M250 60 l-8 -4 l0 8 z" fill="currentColor"/>
  <rect x="250" y="32" width="230" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="260" y="52" fill="currentColor">하드웨어 (명령 0 개)</text>
  <text x="260" y="68" fill="currentColor">① 8 워드를 그 스택에 push</text>
  <text x="260" y="84" fill="currentColor">② LR = EXC_RETURN ③ 벡터 [3..6]</text>
  <path d="M480 60 L520 60" stroke="currentColor"/><path d="M520 60 l-8 -4 l0 8 z" fill="currentColor"/>
  <rect x="520" y="32" width="210" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="530" y="52" fill="currentColor" font-weight="bold">Fault_Entry (asm, TODO 1)</text>
  <text x="530" y="68" fill="currentColor" font-family="monospace">tst lr,#4 → msp/psp</text>
  <text x="530" y="84" fill="currentColor" font-family="monospace">b hardfault_c(frame, lr)</text>
  <path d="M625 88 L625 118" stroke="currentColor"/><path d="M625 118 l-4 -8 l8 0 z" fill="currentColor"/>
  <rect x="440" y="118" width="290" height="116" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="450" y="138" fill="currentColor" font-weight="bold">hardfault_c (C)</text>
  <text x="450" y="156" fill="currentColor">decode_fault  — TODO 2 (이름, VALID)</text>
  <text x="450" y="174" fill="currentColor">fault_recover — TODO 3·4 (PC 를 어디로)</text>
  <text x="450" y="192" fill="currentColor">fault_clear   — TODO 5 (W1C)</text>
  <text x="450" y="210" fill="currentColor">return → bx EXC_RETURN → 하드웨어가</text>
  <text x="450" y="226" fill="currentColor">프레임을 pop, 새 PC 에서 계속</text>
  <text x="10" y="116" fill="currentColor" font-weight="bold">MSP 위 프레임 (gdb x/8wx 실측)</text>
  <rect x="10" y="124" width="380" height="196" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="20" y="144" fill="currentColor" font-family="monospace">0x2000fe30 +0   r0   = 0x20000006</text>
  <text x="20" y="166" fill="currentColor" font-family="monospace">0x2000fe34 +4   r1   = 0x00000000</text>
  <text x="20" y="188" fill="currentColor" font-family="monospace">0x2000fe38 +8   r2   = 0x00000000</text>
  <text x="20" y="210" fill="currentColor" font-family="monospace">0x2000fe3c +12  r3   = 0x00000000</text>
  <text x="20" y="232" fill="currentColor" font-family="monospace">0x2000fe40 +16  r12  = 0x00000007</text>
  <text x="20" y="254" fill="currentColor" font-family="monospace">0x2000fe44 +20  lr   = 0x00001097 (trigger_ret|1)</text>
  <text x="20" y="276" fill="currentColor" font-family="monospace">0x2000fe48 +24  pc   = 0x000010a6 ← fault 명령</text>
  <text x="20" y="298" fill="currentColor" font-family="monospace">0x2000fe4c +28  xpsr = 0x01000000 (T=1)</text>
  <text x="20" y="314" fill="currentColor">frame[5] = LR, frame[6] = PC, frame[7] = xPSR</text>
  <path d="M440 176 L390 270" stroke="currentColor" fill="none"/>
  <text x="400" y="300" fill="currentColor">frame = r0 =</text>
  <text x="400" y="316" fill="currentColor" font-family="monospace">0x2000fe30</text>
</svg>
```

### Part A — 디코더: 비트에 이름 붙이기 (순수 C)

- `unsigned decode_fault(cfsr, hfsr, mmfar, bfar, fault_info_t *out)`: 켜진 비트마다 이름을 `out->names[]` 에 **비트 순서대로**(CFSR bit0→31, 그다음 HFSR VECTTBL·FORCED·DEBUGEVT) 넣고 개수를 돌려준다. 이름 없는(reserved) 비트는 `cfsr_unknown/hfsr_unknown` 으로.
- MMFAR 는 MMARVALID(bit7), BFAR 는 BFARVALID(bit15) 일 때만 값을 넣고 아니면 0. 스태킹 오류(MSTKERR/STKERR/MUNSTKERR/UNSTKERR)면 `frame_valid = 0`.
- `unsigned thumb_insn_len(uint16_t hw1)`: 첫 하프워드만 보고 2 또는 4 (ARM-B12 의 규칙).
- 시험: 단일 비트 64 가지 전수(CFSR 32 + HFSR 32), VALID 규칙 4 건, 무작위 다중 비트 5000 건을 독립 기준 표와 대조.

### Part B — 진짜 fault 8 종 × 두 가지 경로

- `Fault_Entry` (asm, TODO 1): EXC_RETURN bit2 로 MSP/PSP 를 골라 `hardfault_c(frame, exc_return)` 로 `b`.
- `fault_recover(frame, cfsr)` (TODO 4): PC 가 정상 명령이면 길이만큼 건너뛰고(2/4), PC 자체가 나쁜 곳(IACCVIOL·IBUSERR·INVSTATE·INVPC)이면 쌓인 LR 로 돌아간다 (+ `xPSR.T = 1`). `fault_clear` (TODO 5): CFSR/HFSR 는 W1C.
- 라운드 0: 트랩을 끈 리셋 상태 — SDIV 7/0 = 0, 비정렬 LDR 허용, **LDRD 비정렬만 fault**. 라운드 1: SHCSR enable 끔 → 8 종 모두 HardFault + HFSR.FORCED. 라운드 2: 켬 → MemManage(4)/BusFault(5)/UsageFault(6). 각 fault 마다 6 가지 검사: 누가 받았나, CFSR 정확히 그 비트만, **쌓인 PC == 라벨**, 쌓인 LR == `trigger_ret|1` + EXC_RETURN, 해독 문자열 + BFAR, 복구 방법 + 반환값 + fault 1 번.

### Part C — 태스크(PSP)에서 난 fault 와 크래시 지점 복원

- `run_on_psp` 로 PSP 위 "태스크" 에서 0 나눗셈 → EXC_RETURN 0xFFFFFFFD, 프레임이 태스크 스택 안, PC 정확.
- `run_qemu.sh gdb`: 핸들러 안에서 `bt`, `x/8wx $r0`, `info symbol`, 프레임 값으로 컨텍스트를 바꿔 보기. `addr2line` 으로 호스트에서 복원.
- 결함 주입 데모 `run_qemu.sh skip2bug`: "Thumb 명령은 2 바이트" 라고 믿고 항상 +2 하면 실제로 무슨 일이 나는가.

## 2. Problem (English)

> **Write a Cortex-M3 fault analyzer.** Target: QEMU `mps2-an385`. Vectors 3–6 (HardFault, MemManage, BusFault, UsageFault) all point to `Fault_Entry`.
>
> 1. `Fault_Entry` (Thumb-2 assembly, no stack use): select the stacked exception frame from MSP or PSP using bit 2 of `EXC_RETURN` (the value in LR on entry), then tail-branch to `void hardfault_c(uint32_t *frame, uint32_t exc_return)` so that returning from the C function performs the exception return.
> 2. `unsigned decode_fault(uint32_t cfsr, uint32_t hfsr, uint32_t mmfar, uint32_t bfar, fault_info_t *out)`: name every set bit of CFSR (MMFSR: IACCVIOL DACCVIOL MUNSTKERR MSTKERR MLSPERR MMARVALID; BFSR: IBUSERR PRECISERR IMPRECISERR UNSTKERR STKERR LSPERR BFARVALID; UFSR: UNDEFINSTR INVSTATE INVPC NOCP UNALIGNED DIVBYZERO) and HFSR (VECTTBL FORCED DEBUGEVT) in ascending bit order; report reserved bits separately; expose MMFAR/BFAR only when MMARVALID/BFARVALID is set; mark the frame invalid on stacking/unstacking errors.
> 3. To run many faults in one boot, `fault_recover(frame, cfsr)` must resume after the faulting instruction (decode its Thumb length from the first halfword) — or, if the PC itself is bad (instruction-fetch fault, invalid state), return to the stacked LR with `xPSR.T` set. Clear CFSR/HFSR (write-one-to-clear).
>
> Expected on QEMU: `SDIV 7/0` with `DIV_0_TRP` → UsageFault `CFSR=0x02000000`, stacked PC = the SDIV; `LDR [0x30000000]` → BusFault `0x00008200`, `BFAR=0x30000000`; `UDF.W #1` → `0x00010000`, skip 4; branch to `0xE0000001` → MemManage `IACCVIOL` (`0x00000001`, MMFAR not valid), PC = `0xE0000000`; with SHCSR enables cleared every case arrives as HardFault with `HFSR=0x40000000` and the same CFSR bits. Fault in a PSP thread → `EXC_RETURN=0xFFFFFFFD`.
>
> Follow-ups: Why can't `Fault_Entry` be an ordinary C function? When is BFAR trustworthy? Why is "PC += 2" wrong? What changes for an imprecise BusFault? Why is skipping a faulting instruction fine in a test harness but not in production?

## 3. 시작 코드

TODO 2~5 가 있는 C (테스트 하네스 포함 — starter 와 solution 은 TODO 몸통만 다르다):

<!-- code: starter.c -->

TODO 1 이 있는 어셈블리 진입부 (지금은 **항상 MSP**):

<!-- code: starter.S -->

주어진 것 (TODO 없음): fault 트리거 + 호출 틀 + `run_on_psp`, 그리고 벡터 테이블·세미호스팅 스타트업, 링커 스크립트.

<!-- code: shared_triggers.S -->

<!-- code: shared_startup.c -->

<!-- code: arm_m12.ld -->

트리거 규칙: 각 fault 명령 앞에 전역 라벨 `<이름>_insn` (Thumb 비트 없는 NOTYPE 심볼 → 짝수 주소, 쌓인 PC 와 바로 비교 가능), 명령 바로 뒤는 항상 `adds r0, r0, #1`. 그래서 반환값으로 **복구가 어디로 갔는지** 안다: 정확히 건너뛰면 `입력+1` (UDF 는 11 / 21), LR 로 돌아가면 `입력` 그대로.

빌드·실행은 Lima VM `elinux` 안에서 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1). Mac 홈은 VM 에서 읽기 전용이라 스크립트가 `/tmp/ARM-M12_work` 로 복사해서 빌드한다. 맥에서:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ARM/ARM-M12
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # 43/107, ABORT, qemu exit=1
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"   # 110/110, qemu exit=0
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # 19/19
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh syms"       # 라벨 주소 + 인코딩
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh gdb"        # 핸들러 안 bt / 프레임 복원
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh skip2bug"   # 항상 +2 하면?
```

<!-- code: run_qemu.sh -->

VM 없이 "어셈블은 되나?" 만 맥에서 (실제 출력):

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
clang --target=thumbv7m-none-eabi -c solution.S -o /tmp/ARM-M12_sol.o && /usr/bin/objdump -d /tmp/ARM-M12_sol.o
```

```text
00000000 <Fault_Entry>:
       0: f01e 0f04    	tst.w	lr, #0x4
       4: bf0c         	ite	eq
       6: f3ef 8008    	mrseq	r0, msp
       a: f3ef 8009    	mrsne	r0, psp
       e: 4671         	mov	r1, lr
      10: f7ff bffe    	b.w	0x10 <Fault_Entry+0x10> @ imm = #-0x4
```

시작 코드 실행 결과 (실제 출력, PASS 줄과 반복 줄 일부 생략):

```text
ARM-M12 HardFault analyzer (QEMU mps2-an385, Cortex-M3)
FAIL A line 225: bad == 0u
FAIL A line 226: decode_fault(0, 0, 0x1234u, 0x5678u, &f) == 0u && f.mmfar == 0u && f.bfar == 0u && f.frame_valid
… (Part A 의 FAIL 6 줄 더) …
PASS B0 line 292: run_trigger(fault_div, 7u, 0u) == 1u && g_rec.n == n0
PASS B0 line 293: run_trigger(fault_ldr, ADDR(g_buf) + 1u, 0u) == 0x55443323u && g_rec.n == n0
  #1 HardFault  [] pc=0x00000c82 <fault_ldrd_insn> lr=0x00000c73 MSP exc_ret=0xfffffff9 -> (safety net) return to LR
PASS B0 line 296: g_rec.n == n0 + 1u && g_rec.cfsr == CFSR_UNALIGNED && g_rec.pc == ADDR(fault_ldrd_insn)
== round 1: SHCSR enables OFF -> every fault escalates to HardFault (FORCED) ==
  #2 HardFault  [] pc=0x00000c74 <fault_div_insn> lr=0x00000c73 MSP exc_ret=0xfffffff9 -> (safety net) return to LR
PASS DIV0      line 275: r->n == n0 + 1u && r->ipsr == (forced ? 3u : c->exc) && r->hfsr == (forced ? HFSR_FORCED : 0u)
FAIL DIV0      line 276: r->cfsr == c->cfsr
PASS DIV0      line 277: r->pc == c->pc
PASS DIV0      line 278: r->lr == (ADDR(trigger_ret) | 1u) && r->exc_return == 0xFFFFFFF9u
FAIL DIV0      line 279: streq(nb, want) && r->info.bfar == c->bfar && r->info.mmfar == 0u
FAIL DIV0      line 280: !r->bailed && r->method == c->method && ret == c->ret && g_case_faults == 1u
… (UNALIGN, LDRD, BUS, UDF16, UDF32, XN-EXEC, INVSTATE 같은 모양) …
== round 2: SHCSR enables ON -> MemManage / BusFault / UsageFault ==
  #10 UsageFault [] pc=0x00000c74 <fault_div_insn> lr=0x00000c73 MSP exc_ret=0xfffffff9 -> (safety net) return to LR
FAIL DIV0      line 275: r->n == n0 + 1u && r->ipsr == (forced ? 3u : c->exc) && r->hfsr == (forced ? HFSR_FORCED : 0u)
… (라운드 2 는 HFSR.FORCED 가 안 지워져 route 검사까지 FAIL) …
== round 3: fault in a thread running on PSP ==
  #18 UsageFault [] pc=0x00000007 <?> lr=0x00000c75 PSP exc_ret=0xfffffffd -> (safety net) return to LR
  #19 UsageFault [] pc=0x00000c74 <fault_div_insn> lr=0x00000c75 PSP exc_ret=0xfffffffd -> (safety net) return to LR
  #20 UsageFault [] pc=0x00000c74 <fault_div_insn> lr=0x00000c75 PSP exc_ret=0xfffffffd -> (safety net) return to LR
ABORT: frame=0x2000fe60 faults in this case=4 (wrong stack? PC not advanced?)
43/107 passed
qemu exit=1
```

읽는 법: 진입부·스택 고르기와 무관한 것들(누가 받았나, 쌓인 PC, 쌓인 LR)은 시작 코드에서도 맞다 — 하드웨어가 한 일이니까. 틀린 건 전부 **소프트웨어가 할 일**이다. 해독은 빈 칸 `[]`, 복구는 안전망이 대신 LR 로 돌려보냈고, CFSR 은 안 지워서 점점 쌓였다. PSP 라운드에서는 MSP 를 프레임으로 읽어 `pc=0x00000007` 같은 쓰레기가 나오고, 진짜 프레임(PSP)은 그대로라 같은 명령이 계속 fault → 안전망 1 이 4 번째에서 끊는다 (그래서 총 검사 수가 107 로 정답의 110 보다 적다). 안전망 2 개(케이스당 fault 3 번 초과 / PC 그대로면 LR 로)는 시작 코드가 **멈추지 않게** 하려고 주어진 코드에 넣은 것이다.

## 4. 힌트

<details>
<summary>힌트 1 — 진입 순간의 LR 은 주소가 아니다</summary>

예외 진입 때 LR 에는 복귀 주소 대신 EXC_RETURN (0xFFFFFFF1/F9/FD …) 이 들어 있다. bit2 = 복귀할 때 쓸 스택(0 = MSP, 1 = PSP), bit3 = 복귀할 모드(0 = Handler, 1 = Thread). `tst lr, #4` 로 Z 플래그를 만들고 `ite eq` + `mrseq/mrsne` (ARM-B13 의 IT 블록). LR 을 그대로 두고 `b` 로 C 에 넘기면 C 의 `return` 이 곧 예외 복귀가 된다.

</details>

<details>
<summary>힌트 2 — 이름 표는 배열 하나</summary>

`static const char *const NAMES[32] = { [0] = "IACCVIOL", ... }` 처럼 지정 초기화(designated initializer)로 비트 번호 → 이름. reserved 칸은 NULL 로 남는다 → 루프에서 "켜졌는데 NULL" 이면 unknown. HFSR 은 3 비트뿐이니 `if` 3 개.

</details>

<details>
<summary>힌트 3 — Thumb-2 명령 길이는 첫 하프워드의 상위 5 비트</summary>

`hw1[15:11]` 이 0b11101, 0b11110, 0b11111 이면 32 비트, 나머지는 16 비트 (ARM-B12 에서 objdump 로 65536 값 전수 검증한 규칙). 쌓인 PC 의 하프워드를 읽으면 된다 — PC 가 정상 코드 영역일 때만!

</details>

<details>
<summary>힌트 4 — "건너뛸 명령" 이 없는 fault</summary>

PC 가 0xE0000000 (실행 금지 영역) 이거나 Thumb 비트 없는 주소라면, 그 주소의 하프워드는 명령이 아니다. 이때 범인은 **그 주소로 점프한 쪽** = 쌓인 LR. 트리거들은 leaf 라 LR 이 아직 호출자 복귀 주소다. INVSTATE 는 쌓인 xPSR 의 T 비트가 0 이라는 뜻이므로 PC 만 고치면 복귀하자마자 또 INVSTATE.

</details>

<details>
<summary>힌트 5 — CFSR 은 저절로 안 지워진다</summary>

CFSR/HFSR 비트는 sticky, "1 을 써야 0 이 되는"(rc_w1, W1C) 비트다. 읽은 값을 그대로 다시 쓰면 그 비트만 지워진다. 안 지우면 다음 fault 의 CFSR 에 이전 원인이 섞인다 — 시작 코드의 `r->cfsr == c->cfsr` FAIL 이 그것.

</details>

## 5. 풀이

### Part A — 비트 지도와 디코더

CFSR 은 하나의 32 비트 레지스터(0xE000ED28)지만 세 개의 바이트/하프워드 레지스터를 합친 것이다: MMFSR (0xE000ED28, 8 비트), BFSR (0xE000ED29, 8 비트), UFSR (0xE000ED2A, 16 비트). 비트 위치는 ARMv7-M ARM B3.2.15 (CFSR) 와 B3.2.16 (HFSR) 기준 (절 번호는 판에 따라 다르니 확인 권장). 아래 그림의 진한 칸은 이 레슨에서 QEMU 로 **실제로 켜 본** 비트다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">CFSR (0xE000ED28, W1C) + HFSR (0xE000ED2C) — 진한 칸 = 이 레슨에서 실측</text>
  <text x="10" y="44" fill="currentColor" font-weight="bold">UFSR [31:16] → UsageFault (6)</text>
  <rect x="10" y="52" width="110" height="40" rx="3" class="f-acc" stroke="currentColor"/><text x="16" y="68" fill="currentColor">25 DIVBYZERO</text><text x="16" y="84" fill="currentColor">CCR.DIV_0_TRP</text>
  <rect x="125" y="52" width="110" height="40" rx="3" class="f-acc" stroke="currentColor"/><text x="131" y="68" fill="currentColor">24 UNALIGNED</text><text x="131" y="84" fill="currentColor">LDRD 는 항상</text>
  <rect x="240" y="52" width="110" height="40" rx="3" class="f-acc" stroke="currentColor"/><text x="246" y="68" fill="currentColor">19 NOCP</text><text x="246" y="84" fill="currentColor">M3 에서 VFP</text>
  <rect x="355" y="52" width="110" height="40" rx="3" class="f-mut" stroke="currentColor"/><text x="361" y="68" fill="currentColor">18 INVPC</text><text x="361" y="84" fill="currentColor">잘못된 EXC_RET</text>
  <rect x="470" y="52" width="110" height="40" rx="3" class="f-acc" stroke="currentColor"/><text x="476" y="68" fill="currentColor">17 INVSTATE</text><text x="476" y="84" fill="currentColor">T=0 로 실행</text>
  <rect x="585" y="52" width="145" height="40" rx="3" class="f-acc" stroke="currentColor"/><text x="591" y="68" fill="currentColor">16 UNDEFINSTR</text><text x="591" y="84" fill="currentColor">UDF / 미정의</text>
  <text x="10" y="116" fill="currentColor" font-weight="bold">BFSR [15:8] → BusFault (5)</text>
  <rect x="10" y="124" width="95" height="40" rx="3" class="f-acc" stroke="currentColor"/><text x="16" y="140" fill="currentColor">15 BFARVALID</text><text x="16" y="156" fill="currentColor">BFAR 믿어도 됨</text>
  <rect x="110" y="124" width="80" height="40" rx="3" class="f-mut" stroke="currentColor"/><text x="116" y="140" fill="currentColor">13 LSPERR</text><text x="116" y="156" fill="currentColor">FP 전용</text>
  <rect x="195" y="124" width="80" height="40" rx="3" class="f-mut" stroke="currentColor"/><text x="201" y="140" fill="currentColor">12 STKERR</text><text x="201" y="156" fill="currentColor">push 실패</text>
  <rect x="280" y="124" width="95" height="40" rx="3" class="f-mut" stroke="currentColor"/><text x="286" y="140" fill="currentColor">11 UNSTKERR</text><text x="286" y="156" fill="currentColor">pop 실패</text>
  <rect x="380" y="124" width="115" height="40" rx="3" class="f-mut" stroke="currentColor"/><text x="386" y="140" fill="currentColor">10 IMPRECISERR</text><text x="386" y="156" fill="currentColor">PC 는 몇 명령 뒤</text>
  <rect x="500" y="124" width="110" height="40" rx="3" class="f-acc" stroke="currentColor"/><text x="506" y="140" fill="currentColor">9 PRECISERR</text><text x="506" y="156" fill="currentColor">PC 정확</text>
  <rect x="615" y="124" width="115" height="40" rx="3" class="f-mut" stroke="currentColor"/><text x="621" y="140" fill="currentColor">8 IBUSERR</text><text x="621" y="156" fill="currentColor">명령 fetch 오류</text>
  <text x="10" y="188" fill="currentColor" font-weight="bold">MMFSR [7:0] → MemManage (4)</text>
  <rect x="10" y="196" width="110" height="40" rx="3" class="f-mut" stroke="currentColor"/><text x="16" y="212" fill="currentColor">7 MMARVALID</text><text x="16" y="228" fill="currentColor">MMFAR 믿어도 됨</text>
  <rect x="125" y="196" width="95" height="40" rx="3" class="f-mut" stroke="currentColor"/><text x="131" y="212" fill="currentColor">5 MLSPERR</text><text x="131" y="228" fill="currentColor">FP 전용</text>
  <rect x="225" y="196" width="95" height="40" rx="3" class="f-mut" stroke="currentColor"/><text x="231" y="212" fill="currentColor">4 MSTKERR</text><text x="231" y="228" fill="currentColor">push 위반</text>
  <rect x="325" y="196" width="105" height="40" rx="3" class="f-mut" stroke="currentColor"/><text x="331" y="212" fill="currentColor">3 MUNSTKERR</text><text x="331" y="228" fill="currentColor">pop 위반</text>
  <rect x="435" y="196" width="100" height="40" rx="3" class="f-mut" stroke="currentColor"/><text x="441" y="212" fill="currentColor">1 DACCVIOL</text><text x="441" y="228" fill="currentColor">데이터 접근</text>
  <rect x="540" y="196" width="190" height="40" rx="3" class="f-acc" stroke="currentColor"/><text x="546" y="212" fill="currentColor">0 IACCVIOL</text><text x="546" y="228" fill="currentColor">XN 실행 (MMFAR 무효)</text>
  <text x="10" y="260" fill="currentColor" font-weight="bold">HFSR</text>
  <rect x="60" y="246" width="150" height="40" rx="3" class="f-mut" stroke="currentColor"/><text x="66" y="262" fill="currentColor">31 DEBUGEVT</text><text x="66" y="278" fill="currentColor">디버그 이벤트</text>
  <rect x="215" y="246" width="230" height="40" rx="3" class="f-acc" stroke="currentColor"/><text x="221" y="262" fill="currentColor">30 FORCED (실측 0x40000000)</text><text x="221" y="278" fill="currentColor">승격됨 → 원인은 CFSR</text>
  <rect x="450" y="246" width="280" height="40" rx="3" class="f-mut" stroke="currentColor"/><text x="456" y="262" fill="currentColor">1 VECTTBL</text><text x="456" y="278" fill="currentColor">벡터 읽기 BusFault (합성 시험만)</text>
</svg>
```

**디코더의 설계 결정** (정답 코드 TODO 2):

| 결정 | 이유 |
|---|---|
| 이름 표 = `const char *NAMES[32]` 지정 초기화 | 비트 번호가 곧 인덱스. 빈 칸(NULL) = reserved → `cfsr_unknown` 으로 따로 보고 |
| 출력 순서 = 비트 오름차순, HFSR 은 뒤에 | 시험이 결정적(deterministic). 보고 줄 `[PRECISERR BFARVALID FORCED]` 도 늘 같은 모양 |
| `CFSR_DEFINED = 0x030FBFBB`, `HFSR_DEFINED = 0xC0000002` | MMFSR 0xBB (0,1,3,4,5,7) + BFSR 0xBF00 (8–13,15) + UFSR 0x030F0000 (16–19,24,25) |
| MLSPERR(5)/LSPERR(13) 도 이름을 붙임 | v7-M FP 확장(M4F/M7)의 지연 FP 스태킹 오류. M3 에서는 0 으로 읽히지만 디코더는 v7-M 공용으로 |
| `mmfar/bfar` 는 VALID 일 때만 값, 아니면 0 | BM-M13 실측: CFSR 를 지워도 BFAR=0x30000000 이 남는다. VALID 없이 쓰면 **지난번** 주소를 이번 원인으로 오해 |
| `frame_valid = !(MSTKERR|STKERR|MUNSTKERR|UNSTKERR)` | 진입 때 push 가 실패했으면 그 자리에 "프레임" 은 없다 → 쌓인 PC 를 믿지 말 것 (pop 쪽 오류도 보수적으로 같게 취급 — 확인 권장) |

ARMv8-M 은 UFSR bit20 STKOF (스택 한계 위반, MSPLIM/PSPLIM) 를 더한다 — v7-M 디코더에서는 unknown 으로 나오는 게 맞다 (이 레슨 대상 아님, 확인 권장).

**Part A 시험** (정답 실행의 처음 8 줄, 실제 출력): 단일 비트 64 가지 전수는 "독립 기준 표 `REF[]` (비트, 이름) 와 하나씩 비교", 무작위 5000 건은 xorshift32 고정 시드로 CFSR/HFSR/MMFAR/BFAR 를 만들고(절반은 AND 로 비트를 줄여서) 이름 순서·개수·unknown·VALID 규칙을 모델과 비교. 불일치 0.

```text
PASS A line 238: bad == 0u
PASS A line 239: decode_fault(0, 0, 0x1234u, 0x5678u, &f) == 0u && f.mmfar == 0u && f.bfar == 0u && f.frame_valid
PASS A line 241: !f.mmfar_valid && f.mmfar == 0u && !f.bfar_valid && f.bfar == 0u
PASS A line 243: f.mmfar_valid && f.mmfar == 0x20000040u && !f.bfar_valid && f.bfar == 0u
PASS A line 245: f.n == 3u && streq(f.names[0], "PRECISERR") && streq(f.names[1], "BFARVALID") && streq(f.names[2], "FORCED") && f.bfar == 0x30000000u && f.mmfar == 0u
PASS A line 248: !f.frame_valid
PASS A line 264: bad == 0u
PASS A line 265: thumb_insn_len(0xDE00u) == 2u && thumb_insn_len(0xF7F0u) == 4u && thumb_insn_len(0xFB90u) == 4u && thumb_insn_len(0xE7FEu) == 2u && thumb_insn_len(0xE800u) == 4u && thumb_insn_len(0x6800u) == 2u
```

`0xE7FE` (`b .`, 16 비트) 와 `0xE800` (32 비트 LDM/STM 계열의 첫 하프워드) 는 경계값이다: 상위 5 비트 0b11100 vs 0b11101.

### Part B — 진입부, 그리고 8 종 fault 의 실측

**Fault_Entry 한 줄씩** (TODO 1):

| 명령 | 하는 일 | 왜 |
|---|---|---|
| `tst lr, #4` | Z = (EXC_RETURN bit2 == 0) | bit2 = "복귀 때 쓸 스택" = fault 당시 쓰던 스택 |
| `ite eq` | 다음 두 명령을 EQ / NE 조건으로 | ARM-B13 의 IT 블록 |
| `mrseq r0, msp` / `mrsne r0, psp` | r0 = 프레임 주소 | 특수 레지스터는 MRS 로만 (ARM-B14) |
| `mov r1, lr` | r1 = EXC_RETURN | C 쪽에서 보고·검사용 |
| `b hardfault_c` | 꼬리 분기 | LR 이 EXC_RETURN 그대로 → C 의 `pop {…, pc}` 가 예외 복귀 |

**왜 C 로는 안 되나**: 정답 ELF 의 `hardfault_c` 프롤로그 (실제 objdump) — `stmdb sp!, {r4, r5, r6, r7, r8, lr}` 후 `sub sp, #168`. 함수 본문이 시작될 땐 sp 가 진입 순간보다 **192 바이트** 아래다. C 안에서 `mrs %0, msp` 를 해도 프레임 주소가 아니고, 최적화 수준·함수 내용이 바뀌면 그 차이도 바뀐다. 그래서 "스택이 손대기 전에" 실행되는 asm 진입부(또는 GCC `__attribute__((naked))` C — BM-M13 방식)가 필요하다.

EXC_RETURN 값 표 (ARMv7-M, FP 없는 M3 에서는 위 세 개만 나온다):

| 값 | 복귀 모드 | 스택 | 이 레슨에서 본 곳 |
|---|---|---|---|
| 0xFFFFFFF1 | Handler | MSP | 연습 1: SVC 핸들러 안의 fault |
| 0xFFFFFFF9 | Thread | MSP | 라운드 0~2 전부 |
| 0xFFFFFFFD | Thread | PSP | 라운드 3 (PSP 태스크) |
| 0xFFFFFFE1/E9/ED | (위와 같음) | + FP 확장 프레임 (bit4 = 0) | M4F 이상 — 해독 함수는 ARM-M09 |

**정답 실행 결과** (실제 출력, PASS 110 줄은 생략하고 핸들러 보고 줄만):

```text
ARM-M12 HardFault analyzer (QEMU mps2-an385, Cortex-M3)
  #1 HardFault  [UNALIGNED FORCED] pc=0x000010a6 <fault_ldrd_insn> lr=0x00001097 MSP exc_ret=0xfffffff9 -> skip 4
== round 1: SHCSR enables OFF -> every fault escalates to HardFault (FORCED) ==
  #2 HardFault  [DIVBYZERO FORCED] pc=0x00001098 <fault_div_insn> lr=0x00001097 MSP exc_ret=0xfffffff9 -> skip 4
  #3 HardFault  [UNALIGNED FORCED] pc=0x000010a0 <fault_ldr_insn> lr=0x00001097 MSP exc_ret=0xfffffff9 -> skip 2
  #4 HardFault  [UNALIGNED FORCED] pc=0x000010a6 <fault_ldrd_insn> lr=0x00001097 MSP exc_ret=0xfffffff9 -> skip 4
  #5 HardFault  [PRECISERR BFARVALID FORCED] pc=0x000010a0 <fault_ldr_insn> lr=0x00001097 MSP exc_ret=0xfffffff9 BFAR=0x30000000 -> skip 2
  #6 HardFault  [UNDEFINSTR FORCED] pc=0x000010b0 <fault_udf16_insn> lr=0x00001097 MSP exc_ret=0xfffffff9 -> skip 2
  #7 HardFault  [UNDEFINSTR FORCED] pc=0x000010b8 <fault_udf32_insn> lr=0x00001097 MSP exc_ret=0xfffffff9 -> skip 4
  #8 HardFault  [IACCVIOL FORCED] pc=0xe0000000 <?> lr=0x00001097 MSP exc_ret=0xfffffff9 -> return to LR
  #9 HardFault  [INVSTATE FORCED] pc=0x000010c0 <fault_tbit (even!)> lr=0x00001097 MSP exc_ret=0xfffffff9 -> return to LR
== round 2: SHCSR enables ON -> MemManage / BusFault / UsageFault ==
  #10 UsageFault [DIVBYZERO] pc=0x00001098 <fault_div_insn> lr=0x00001097 MSP exc_ret=0xfffffff9 -> skip 4
  #11 UsageFault [UNALIGNED] pc=0x000010a0 <fault_ldr_insn> lr=0x00001097 MSP exc_ret=0xfffffff9 -> skip 2
  #12 UsageFault [UNALIGNED] pc=0x000010a6 <fault_ldrd_insn> lr=0x00001097 MSP exc_ret=0xfffffff9 -> skip 4
  #13 BusFault   [PRECISERR BFARVALID] pc=0x000010a0 <fault_ldr_insn> lr=0x00001097 MSP exc_ret=0xfffffff9 BFAR=0x30000000 -> skip 2
  #14 UsageFault [UNDEFINSTR] pc=0x000010b0 <fault_udf16_insn> lr=0x00001097 MSP exc_ret=0xfffffff9 -> skip 2
  #15 UsageFault [UNDEFINSTR] pc=0x000010b8 <fault_udf32_insn> lr=0x00001097 MSP exc_ret=0xfffffff9 -> skip 4
  #16 MemManage  [IACCVIOL] pc=0xe0000000 <?> lr=0x00001097 MSP exc_ret=0xfffffff9 -> return to LR
  #17 UsageFault [INVSTATE] pc=0x000010c0 <fault_tbit (even!)> lr=0x00001097 MSP exc_ret=0xfffffff9 -> return to LR
== round 3: fault in a thread running on PSP ==
  #18 UsageFault [DIVBYZERO] pc=0x00001098 <fault_div_insn> lr=0x00001097 PSP exc_ret=0xfffffffd -> skip 4
110/110 passed
qemu exit=0
```

**쌓인 PC 가 정말 그 명령인가** — `run_qemu.sh syms` (실제 출력 일부: `nm -n` 다음 `objdump -d`):

```text
00001096 T trigger_ret
00001098 T fault_div_insn
000010a0 T fault_ldr_insn
000010a6 T fault_ldrd_insn
000010b0 T fault_udf16_insn
000010b8 T fault_udf32_insn
000010c0 T fault_tbit
00001098 <fault_div>:
    1098:	fb90 f0f1 	sdiv	r0, r0, r1
    109c:	3001      	adds	r0, #1
000010a6 <fault_ldrd>:
    10a6:	e9d0 0100 	ldrd	r0, r1, [r0]
    10aa:	3001      	adds	r0, #1
000010b0 <fault_udf16_insn>:
    10b0:	de00      	udf	#0
000010b8 <fault_udf32_insn>:
    10b8:	f7f0 a001 	udf.w	#1
    10bc:	3001      	adds	r0, #1
```

8 종 모두 **정밀(precise)** fault 라 쌓인 PC = fault 를 낸 명령 자체. skip 길이(4/2/4/2/2/4)가 인코딩 길이와 정확히 맞는다 — `sdiv`, `ldrd`, `udf.w` 의 첫 하프워드 0xFB90/0xE9D0/0xF7F0 은 상위 5 비트가 0b11111/0b11101/0b11110.

**8 종 정리표** (라운드 2 기준, 라운드 1 은 전부 IPSR 3 + HFSR 0x40000000 + 같은 CFSR):

| 케이스 | 트리거 | 받은 핸들러 | CFSR | 쌓인 PC | 복구 |
|---|---|---|---|---|---|
| DIV0 | `sdiv` 7/0, DIV_0_TRP=1 | UsageFault 6 | 0x02000000 | 0x1098 sdiv | +4 → 반환 8 |
| UNALIGN | `ldr` 홀수, UNALIGN_TRP=1 | UsageFault 6 | 0x01000000 | 0x10a0 | +2 |
| LDRD | `ldrd` 4 정렬 아님 | UsageFault 6 | 0x01000000 | 0x10a6 | +4 (트랩 꺼도 fault: 라운드 0 #1) |
| BUS | `ldr [0x30000000]` | BusFault 5 | 0x00008200 + BFAR | 0x10a0 | +2 |
| UDF16 | `udf #0` (0xDE00) | UsageFault 6 | 0x00010000 | 0x10b0 | +2 → 11 |
| UDF32 | `udf.w #1` | UsageFault 6 | 0x00010000 | 0x10b8 | +4 → 21 |
| XN-EXEC | `blx` 0xE0000001 | MemManage 4 | 0x00000001 (MMARVALID 0) | **0xE0000000** | LR 로 |
| INVSTATE | `blx` 짝수 주소 | UsageFault 6 | 0x00020000 | **0x10c0** (목표 함수) | LR 로 + T=1 |

마지막 두 줄이 "PC 복원" 의 핵심이다. 쌓인 PC 가 가리키는 곳은 **범인이 아니라 피해 현장**이다: 0xE0000000 은 시스템 영역(기본 메모리 맵에서 XN — 실행 금지)이고, 0x10c0 은 멀쩡한 함수인데 Thumb 비트 없이 불려서 T=0 상태로 실행하려다 fault. 범인은 그 주소로 뛴 `blx r4` = 쌓인 LR 0x1097 → `trigger_ret` 바로 앞. 실전에서는 "깨진 함수 포인터 / 덮어쓴 콜백 테이블" 패턴이다. MMFAR 은 데이터 접근 주소 레지스터라 명령 fetch 위반(IACCVIOL)에서는 MMARVALID=0 — 실측 CFSR 0x00000001 그대로.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">fault_recover: 쌓인 PC 를 어디로 보내나 (+ 틀렸을 때 실측)</text>
  <rect x="10" y="32" width="300" height="52" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="52" fill="currentColor">IACCVIOL|IBUSERR|INVSTATE|INVPC ?</text>
  <text x="20" y="70" fill="currentColor">(연습 3: 또는 쌓인 xPSR.T == 0)</text>
  <path d="M310 58 L360 58" stroke="currentColor"/><path d="M360 58 l-8 -4 l0 8 z" fill="currentColor"/>
  <text x="318" y="50" fill="currentColor">예</text>
  <rect x="360" y="32" width="370" height="52" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="370" y="52" fill="currentColor" font-family="monospace">frame[6] = frame[5] &amp; ~1; frame[7] |= T</text>
  <text x="370" y="70" fill="currentColor">PC 는 명령이 아님 → 범인 = 호출자 (LR). 반환 0</text>
  <path d="M160 84 L160 112" stroke="currentColor"/><path d="M160 112 l-4 -8 l8 0 z" fill="currentColor"/>
  <text x="168" y="104" fill="currentColor">아니오</text>
  <rect x="10" y="112" width="300" height="52" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="132" fill="currentColor" font-family="monospace">hw1 = *(uint16_t *)frame[6]</text>
  <text x="20" y="150" fill="currentColor" font-family="monospace">len = hw1[15:11] &gt;= 0b11101 ? 4 : 2</text>
  <path d="M310 138 L360 138" stroke="currentColor"/><path d="M360 138 l-8 -4 l0 8 z" fill="currentColor"/>
  <rect x="360" y="112" width="370" height="52" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="370" y="132" fill="currentColor" font-family="monospace">frame[6] += len</text>
  <text x="370" y="150" fill="currentColor">IT 블록 안이면 ITSTATE 도 한 칸 (연습 4)</text>
  <path d="M545 164 L545 192" stroke="currentColor"/><path d="M545 192 l-4 -8 l8 0 z" fill="currentColor"/>
  <rect x="360" y="192" width="370" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="370" y="216" fill="currentColor">CFSR/HFSR W1C → return = 예외 복귀</text>
  <rect x="10" y="192" width="330" height="100" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="212" fill="currentColor" font-weight="bold">항상 +2 로 하면 (skip2bug 실측)</text>
  <text x="20" y="230" fill="currentColor" font-family="monospace">sdiv  +2 → f0f1 3001 = UNDEFINED</text>
  <text x="20" y="248" fill="currentColor" font-family="monospace">ldrd  +2 → 0100 = lsls r0,r0,#4 (조용히)</text>
  <text x="20" y="266" fill="currentColor" font-family="monospace">udf.w +2 → a001 = add r0,pc,#4 (조용히)</text>
  <text x="20" y="284" fill="currentColor">IACCVIOL +2 → 0xE0000002, …04 계속 fault</text>
</svg>
```

**W1C 를 안 하면** — 시작 코드를 gdb 로 멈춰 실제 CFSR 을 읽었다 (실측): 라운드 1 의 DIV0 (fault #2) 진입 때 `CFSR = 0x03000000` (= 라운드 0 의 UNALIGNED + 이번 DIVBYZERO), 라운드 2 의 DIV0 (fault #10) 에서는 `0x03038201` (IACCVIOL·PRECISERR·BFARVALID·UNDEFINSTR·INVSTATE·UNALIGNED·DIVBYZERO 가 다 섞임), `HFSR = 0x40000000` (라운드 2 는 승격이 아닌데도 FORCED 가 남음). 이 상태로 보고서를 쓰면 "DIVBYZERO 와 BusFault 와 XN 실행이 동시에" 라는 엉터리가 된다.

**fault_clear 에 "읽은 값" 을 쓰는 이유**: `SCB_CFSR = 0xFFFFFFFF` 도 지워지긴 한다. 하지만 읽은 뒤 쓰기 전에 다른 fault 비트가 새로 켜졌다면(예: 핸들러 안의 BusFault 가 HardFault 로 승격된 경우) 그것까지 지워 버린다. 읽은 비트만 지우면 새 비트는 다음 분석에 남는다.

### Part C — 태스크에서 난 fault, gdb, 그리고 크래시 지점 복원

라운드 3: `run_on_psp(&task_stack[256], psp_task, 9)` → 태스크가 `fault_div(9, 0)`. 보고 줄 `#18 … PSP exc_ret=0xfffffffd -> skip 4`, 반환 10, 프레임은 태스크 스택 안. 시작 코드의 MSP 고정 진입부로는 이 경우만 무너진다 (3. 시작 코드 출력의 `pc=0x00000007`). RTOS 에서는 태스크 fault 가 대부분이므로 이 한 줄(`mrsne r0, psp`)이 실전에서 제일 중요하다.

**gdb 세션** (`run_qemu.sh gdb`, 스크립트는 6. 정답 코드 아래 `demo_fault.gdb`, 실제 출력 발췌):

```text
Breakpoint 1, hardfault_c (frame=0x2000fe30, exc_return=4294967289) at solution.c:173
=== stop 1: 첫 fault (B0 의 LDRD, MSP) — gdb 의 bt ===
#0  hardfault_c (frame=0x2000fe30, exc_return=4294967289) at solution.c:173
#1  <signal handler called>
#2  fault_ldrd () at shared_triggers.S:52
=== 쌓인 8 워드 {r0 r1 r2 r3 r12 lr pc xpsr} ===
0x2000fe30:	0x20000006	0x00000000	0x00000000	0x00000000
0x2000fe40:	0x00000007	0x00001097	0x000010a6	0x01000000
=== 쌓인 PC / LR 이 가리키는 곳 ===
fault_ldrd in section .text
Line 52 of "shared_triggers.S" starts at address 0x10a6 <fault_ldrd> and ends at 0x10aa <fault_ldrd+4>.
   0x10a6 <fault_ldrd>:	ldrd	r0, r1, [r0]
   0x10aa <fault_ldrd+4>:	adds	r0, #1
run_trigger + 11 in section .text
=== stop 2: PSP 태스크 안의 fault (#18) ===
Breakpoint 1, hardfault_c (frame=0x200004b0 <task_stack+984>, exc_return=4294967293) at solution.c:173
$3 = 0xfffffffd
$4 = 0x200004b0
$5 = 0x200004b0
$6 = 0x2000fe50
#0  hardfault_c (frame=0x200004b0 <task_stack+984>, exc_return=4294967293) at solution.c:173
#1  <signal handler called>
#2  fault_div () at shared_triggers.S:36
0x200004b0 <task_stack+984>:	0x00000009	0x00000000	0x00000000	0x0000189c
0x200004c0 <task_stack+1000>:	0x00000007	0x00001097	0x00001098	0x61000000
fault_div in section .text
--- addr2line on the stacked PC/LR (host side) ---
run_trigger
/tmp/ARM-M12_work/shared_triggers.S:36
run_trigger
/tmp/ARM-M12_work/shared_triggers.S:30
```

`$3..$6` = `$r1` (EXC_RETURN), `$r0` (프레임), `$psp`, `$msp` 순서 — 프레임 주소가 PSP 와 같고, 핸들러 자신은 MSP 위에서 돈다. 태스크 프레임의 xPSR 0x61000000 은 Z·C 플래그 + T.

예상과 달랐던 것 세 가지 (실측이 진실):

1. **gdb 의 `bt` 는 fault 함수를 보여 줬다.** gdb-multiarch 는 M-profile 의 EXC_RETURN 을 알아보고 쌓인 프레임을 풀어 `<signal handler called>` 다음에 `fault_ldrd` 를 보여 준다. 다만 **거기서 멈춘다** — `run_trigger`, `main` 은 안 나온다 (asm 트리거에 CFI 가 없어 더 못 푼다). 디버거 없이 덤프(BM-M13 의 `.noinit` 기록)만 있는 실전에서는 이 풀이를 직접 해야 하므로, 쌓인 PC/LR 을 읽는 법이 여전히 기본기다.
2. **`addr2line -f` 의 함수 이름이 틀렸다.** 0x1098 (`fault_div_insn`) 을 `run_trigger` 라고 한다. 줄 번호 `:36` (= `sdiv`) 은 맞다. 원인 추정: `.S` 파일에는 DWARF 함수 정보가 없어 심볼 표로 이름을 찾는데, Thumb FUNC 심볼 값은 bit0 = 1 (`readelf -s`: `fault_div` = 0x00001099) 이라 0x1098 은 그 **앞** 함수 `run_trigger` (0x108d, 크기 12) 에 속한 것으로 보인다 (binutils 내부 동작은 확인 권장). 교훈: asm 코드의 크래시는 `nm`/gdb `info symbol` 로 이름을, `addr2line` 으로는 줄 번호만.
3. 쌓인 LR 0x1097 = `run_trigger + 11` (gdb) = `trigger_ret | 1` → "fault 함수를 부른 곳". leaf 가 아닌 함수에서는 LR 이 이미 덮였을 수 있으니, 그때는 스택을 거슬러 저장된 LR 을 찾아야 한다 (ARM-M14 의 AArch64 판과 같은 절차).

**면접에서 말하는 크래시 읽기 순서** (Cortex-M):

| 순서 | 보는 것 | 이 레슨의 예 |
|---|---|---|
| 1 | IPSR (누가 받았나) + HFSR.FORCED (승격?) | 3 + FORCED → 원인은 CFSR 에 |
| 2 | CFSR 비트 이름 | DIVBYZERO / PRECISERR BFARVALID / IACCVIOL |
| 3 | VALID 면 BFAR/MMFAR | BFAR=0x30000000 → 무엇이 그 주소를 만들었나 |
| 4 | EXC_RETURN bit2 → 프레임 위치, bit3 → Thread/Handler | 0xFFFFFFFD → PSP 태스크 |
| 5 | 쌓인 PC → 명령 (objdump/gdb) | `ldrd r0, r1, [r0]`, r0 = 0x20000006 |
| 6 | PC 가 명령이 아니면(XN/INVSTATE) → 쌓인 LR 이 범인 | 0xE0000000 ← `blx r4` |
| 7 | 쌓인 xPSR: T 비트, [8:0] = 끊긴 문맥 (0 = Thread, 11 = SVCall …) | 연습 1: 0x2100000b |
| 8 | IMPRECISERR 면 PC 를 믿지 말고 DISDEFWBUF 로 재현 | QEMU 는 정밀만 (연습 5) |

**부정확(imprecise) BusFault** — QEMU 는 저장 fault 도 PRECISERR 로 보고한다 (연습 5 실측, BM-M13 P2 와 같음). 실제 Cortex-M3/M4 는 버퍼 가능한(bufferable) 영역의 저장을 쓰기 버퍼에 넣고 다음 명령으로 진행하므로, 버스 오류가 돌아올 때 PC 는 이미 몇 명령 뒤이고 CFSR 에는 IMPRECISERR (bit10), BFARVALID=0 이 된다 (Cortex-M3 TRM / Arm AN209, 확인 권장). 디버그 때는 ACTLR (0xE000E008) 의 DISDEFWBUF (bit1) 를 켜서 쓰기 버퍼를 끄면 정밀해진다 (성능 저하). QEMU 10.2 에서는 ACTLR 이 0 으로 읽히고 쓰기가 무시된다 (연습 5 실측: before 0x00000000, after 0x00000000).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

<!-- code: solution.S diff=starter.S -->

gdb 세션 스크립트 (`run_qemu.sh gdb` — 다른 실습과 겹치지 않게 gdb 포트 3412):

<!-- code: demo_fault.gdb -->

## 7. 자주 하는 실수

**실수 1 — 진입부를 평범한 C 로.** `void HardFault_Handler(void) { uint32_t *f = (uint32_t *)__get_MSP(); … }` 는 프롤로그가 sp 를 옮긴 **뒤**의 MSP 를 읽는다. 이 레슨의 `hardfault_c` 는 192 바이트를 옮겼다 (5. 풀이 Part B). 게다가 MSP 고정이라 PSP 태스크 fault 에서는 완전히 엉뚱한 곳(시작 코드의 `pc=0x00000007`). 고침: asm 진입부(또는 `naked`)에서 `tst lr, #4` 로 고르고 `b` 로 넘긴다.

**실수 2 — "Thumb 명령은 2 바이트" 라고 PC += 2.** `run_qemu.sh skip2bug` (실제 출력 발췌, 이 빌드는 주소가 4 바이트 밀려 있다):

```text
  #2 HardFault  [DIVBYZERO FORCED] pc=0x0000109c <fault_div_insn> lr=0x0000109b MSP exc_ret=0xfffffff9 -> skip 2
  #3 HardFault  [UNDEFINSTR FORCED] pc=0x0000109e <?> lr=0x0000109b MSP exc_ret=0xfffffff9 -> skip 2
  #5 HardFault  [UNALIGNED FORCED] pc=0x000010aa <fault_ldrd_insn> lr=0x0000109b MSP exc_ret=0xfffffff9 -> skip 2
FAIL LDRD      line 293: !r->bailed && r->method == c->method && ret == c->ret && g_case_faults == 1u
  #9 HardFault  [IACCVIOL FORCED] pc=0xe0000000 <?> lr=0x0000109b MSP exc_ret=0xfffffff9 -> skip 2
  #10 HardFault  [IACCVIOL FORCED] pc=0xe0000002 <?> lr=0x0000109b MSP exc_ret=0xfffffff9 -> skip 2
  #11 HardFault  [IACCVIOL FORCED] pc=0xe0000004 <?> lr=0x0000109b MSP exc_ret=0xfffffff9 -> skip 2
ABORT: frame=0x2000fd38 faults in this case=4 (wrong stack? PC not advanced?)
40/47 passed
qemu exit=1
```

SDIV 의 둘째 하프워드 `f0f1` 이 다음 `3001` 과 붙어 32 비트 명령 `0xf0f13001` 로 읽혀 **두 번째 fault** (UNDEFINSTR). 더 나쁜 건 LDRD·UDF.W 다: 둘째 하프워드 `0x0100` = `lsls r0, r0, #4`, `0xa001` = `add r0, pc, #4` 로 **fault 없이** 실행돼 반환값만 조용히 틀린다 (objdump 로 확인). XN 영역에서는 0xE0000000, …02, …04 로 계속 fault — 안전망이 4 번째에 끊었다. 고침: `thumb_insn_len(hw1)` (ARM-B12), PC 가 명령이 아닌 fault 는 LR 로.

**실수 3 — CFSR/HFSR 를 안 지우기.** sticky 라 다음 fault 에 섞인다 (실측 0x03038201). 지울 땐 읽은 값을 W1C 로. 반대로 **정책상 리셋할 거라면** 지우지 말고 기록부터 (BM-M13).

**실수 4 — VALID 확인 없이 BFAR/MMFAR 출력.** BFAR 는 이전 주소를 들고 있을 수 있다 (BM-M13 P4). IACCVIOL 의 MMFAR 도 무의미 (MMARVALID=0 실측). Arm AN209 는 "BFAR 를 읽은 뒤 BFARVALID 가 여전히 1 인지 다시 확인" 을 권한다 — 더 높은 우선순위 fault 가 그 사이 덮을 수 있어서 (확인 권장).

**실수 5 — INVSTATE 에서 PC 만 고치기.** 쌓인 xPSR 의 T 비트가 0 이라(연습 3 실측 xpsr=0x00000000), PC 를 LR 로 바꿔도 복귀 직후 또 INVSTATE. `frame[7] |= 1 << 24` 를 같이.

**실수 6 — "범인 = 쌓인 PC" 로 단정.** IACCVIOL(0xE0000000)·INVSTATE(멀쩡한 함수 주소)·IMPRECISERR(몇 명령 뒤)·STKERR(프레임 자체가 없음) 에서는 PC 가 범인이 아니다. 각각 LR / LR / DISDEFWBUF 재현 / SP 와 스택 한계를 본다.

**실수 7 — IT 블록 안에서 건너뛰고 ITSTATE 는 그대로.** 연습 4 실측: 반환 6 이어야 할 것이 7 — 건너뛴 다음 명령이 엉뚱한 조건으로 실행된다.

## 8. 면접 Q&A

<details>
<summary>Q1. HardFault 가 났을 때 죽은 지점의 PC 를 어떻게 찾나요?</summary>

진입 때 LR 의 EXC_RETURN bit2 로 프레임이 MSP 인지 PSP 인지 고르고, 그 주소 + 24 (`frame[6]`) 가 쌓인 PC. 정밀 fault 면 그게 fault 를 낸 명령 자체다 (이 레슨의 명령 fault 14 건 실측 일치; XN·INVSTATE 4 건은 Q5). 꼬리질문 "+24 인 이유?" → 프레임 순서 r0, r1, r2, r3, r12, LR, PC, xPSR 가 4 바이트씩. "FP 있는 M4F 면?" → 확장 프레임이라도 앞 8 워드 배치는 같다 (S0–S15, FPSCR 는 그 뒤 — ARM-M09).

</details>

<details>
<summary>Q2. 왜 HardFault 진입부를 어셈블리(또는 naked)로 쓰나요?</summary>

C 함수는 본문이 실행되기 전에 레지스터를 push 하고 지역 변수 공간을 잡아 sp 를 바꾼다. 이 레슨의 `hardfault_c` 는 `stmdb sp!, {r4-r8, lr}` + `sub sp, #168` 로 192 바이트. 그러면 "진입 순간의 sp" 를 C 에서 알 방법이 없다. asm 은 스택을 건드리기 전에 MSP/PSP 를 r0 에 담고, LR(EXC_RETURN) 을 보존한 채 `b` 로 넘겨 C 의 return 이 예외 복귀가 되게 한다.

</details>

<details>
<summary>Q3. HFSR = 0x40000000 (FORCED) 이면 원인은 어디서 보나요?</summary>

FORCED 는 "세분 fault(MemManage/BusFault/UsageFault)가 꺼져 있었거나 우선순위 때문에 못 받아서 HardFault 로 승격됨" 이라는 뜻일 뿐, 원인은 CFSR 에 그대로 있다 (라운드 1 실측: 8 종 모두 라운드 2 와 같은 CFSR). 그래서 HardFault 핸들러 하나로도 원인은 알 수 있다. 꼬리질문 "그래도 세분 핸들러를 켜는 이유?" → 우선순위를 따로 줄 수 있고, 세분 핸들러 안의 fault 는 HardFault 로 한 번 더 기회가 있으며, 0 나눗셈/비정렬은 트랩을 켜야만 감지된다 (BM-M13).

</details>

<details>
<summary>Q4. BFAR 는 언제 믿을 수 있나요? 부정확 BusFault 는 어떻게 디버깅하나요?</summary>

BFSR.BFARVALID (CFSR bit15) 가 1 일 때만. IMPRECISERR 에서는 0 이고 쌓인 PC 도 fault 낸 저장 명령보다 뒤다 — 쓰기 버퍼 때문. 재현할 때 ACTLR.DISDEFWBUF (bit1) 로 쓰기 버퍼를 끄면 PRECISERR 로 바뀌어 PC 와 BFAR 이 정확해진다 (M3/M4, 확인 권장). QEMU 는 이 구분을 모델링하지 않는다: 저장 fault 도 PRECISERR, ACTLR 은 RAZ/WI (연습 5 실측).

</details>

<details>
<summary>Q5. 쌓인 PC 가 0xE0000000 이거나 짝수 함수 주소면요?</summary>

PC 는 "어디서 실행하려다 실패했나" 이지 범인이 아니다. 0xE0000000 대 이상은 기본 메모리 맵에서 실행 금지(XN) → IACCVIOL (MMARVALID=0). 짝수 주소로의 `bx/blx` 는 EPSR.T=0 → INVSTATE, 쌓인 xPSR 의 T=0. 둘 다 그 주소로 점프한 **호출자**를 쌓인 LR 에서 찾는다 (실측 LR = `trigger_ret|1`). 전형적 원인: 손상된 함수 포인터, 덮어쓴 vtable/콜백 표, 벡터 테이블 엔트리의 Thumb 비트 누락 (ARM-M08).

</details>

<details>
<summary>Q6. fault 난 명령을 건너뛰고 복귀하는 건 제품에서도 쓰나요?</summary>

시험 하네스(한 부팅에 fault 여러 개 확인)나 "이 주소가 존재하나?" 탐침(BM-M13 의 probe) 에서만. 제품에서 건너뛰면 그 명령의 결과(레지스터·메모리)가 없는 채로 계속 실행되어 상태가 망가진다 — 더 나쁜 건 잘못 건너뛰면 조용히 틀린 값이 나온다 (skip2bug 의 `lsls`). 제품 정책은 안전 상태 → 기록 → 리셋 (BM-M13). 건너뛸 땐 Thumb 길이, `xPSR.T`, IT 상태까지 맞춰야 한다.

</details>

<details>
<summary>Q7. ISR 안에서 fault 가 나면 덤프가 어떻게 달라지나요?</summary>

EXC_RETURN 이 0xFFFFFFF1 (Handler 로 복귀, MSP), 쌓인 xPSR[8:0] = 끊긴 예외의 번호 (연습 1 실측: 11 = SVCall, xpsr=0x2100000b), 쌓인 LR 은 그 ISR 이 leaf 면 아직 그 ISR 의 EXC_RETURN (실측 0xFFFFFFF9). 그리고 fault 핸들러의 우선순위가 그 ISR 보다 높지 않으면 HardFault FORCED 로 승격 (연습 1: SVCall 우선순위 0 일 때 IPSR 3).

</details>

<details>
<summary>Q8. 스택 오버플로로 HardFault 가 나면 이 분석기는 믿을 만한가요?</summary>

아니다. 진입 때 push 자체가 실패하면 STKERR (BusFault) 또는 MSTKERR (MPU 가드) 가 켜지고, 그 자리에 프레임이 없다 → `frame_valid = 0` 으로 표시하고 PC 를 보고하지 않는다. 핸들러가 같은 망가진 스택을 쓰면 Lockup 까지 간다. 대책: MSP 와 태스크 스택 사이에 MPU 가드 영역, 스택 칠하기(ARM-B14 연습 3)로 최고 수위 감시, ARMv8-M 이면 MSPLIM/PSPLIM (STKOF).

</details>

## 9. 추가 연습

`practice.c` + `practice.S` 는 QEMU 펌웨어다 (`run_qemu.sh practice`). 디코더는 solution 과 같은 복사본이고, `fault_recover` 를 연습 3·4 의 규칙으로 강화했다.

### 연습 1 — 핸들러 안의 fault

SVC 핸들러가 `udf #2` 를 실행한다. SVCall 우선순위를 0x80 (SHPR2 [31:24]) 으로 낮추면 UsageFault 가 선점하고, 0 이면 HardFault 로 승격된다. 쌓인 xPSR 에서 "무엇이 끊겼나" 를 이름으로 (`ctx_name`).

<details>
<summary>답</summary>

실측 (우선순위 0x80): `IPSR=6 CFSR=0x00010000 pc=svc_udf_insn lr=0xfffffff9 xpsr=0x2100000b exc_ret=0xfffffff1 -> skip 2`. xPSR[8:0] = 11 → "SVCall", 쌓인 LR = SVC 핸들러 자신의 EXC_RETURN (leaf). 우선순위 0: `IPSR=3 HFSR=0x40000000 xpsr=0x0100000b exc_ret=0xfffffff1` — 같은 우선순위는 선점 못 한다.

</details>

### 연습 2 — FPU 없는 코어에서 FPU 명령

Cortex-M3 에서 `VMOV S0, R0` (인코딩 0xEE00 0x0A10 을 `.inst.w` 로 — 어셈블러가 M3 에서는 VFP 를 거부) 를 실행해 원인과 건너뛰기 길이를 확인.

<details>
<summary>답</summary>

실측 `CFSR=0x00080000` = NOCP (bit19), UsageFault, skip 4, 반환 6. M4F 에서 CPACR 로 CP10/CP11 을 안 켜고 FPU 명령을 써도 같은 NOCP — "printf("%f") 넣었더니 HardFault" 의 단골 원인 (확인 권장).

</details>

### 연습 3 — QEMU 의 INVSTATE 특이 동작과 더 튼튼한 판단

Thumb 비트 없는 목표 주소가 4 의 배수가 아니면(≡ 2 mod 4) QEMU 10.2 는 무엇을 보고하나? 원인 비트 대신 쌓인 `xPSR.T` 로 "PC 가 명령이 아님" 을 판단하도록 `fault_recover` 를 고쳐 fault 1 번에 복구.

<details>
<summary>답</summary>

본문을 처음 짤 때 실제로 겪었다: 목표가 0x10a6 일 때 QEMU 는 먼저 **UNALIGNED** (pc=목표, xPSR=0x00000000) 를 내고, +2 로 건너뛴 다음 4 정렬 주소에서 INVSTATE 를 냈다. QEMU 의 번역기가 T=0 이면 ARM(A32) 상태로 보고 "PC 정렬" 검사를 먼저 하는 것으로 추정 (소스 미확인). 실제 M3 기대값은 INVSTATE (확인 권장). 그래서 본문 트리거 `fault_tbit` 은 `.p2align 2`. 고친 규칙: `!(frame[7] & XPSR_T)` 이면 원인 비트와 무관하게 LR 로 → 실측 fault 1 번, CFSR 0x01000000, 반환 0x77.

</details>

### 연습 4 — IT 블록 안에서 건너뛰기

`cmp r2,#0; ite eq; sdiveq r0,r0,r1; movne r0,#7; adds r0,r0,#1`. flag=0, b=0 이면 SDIVEQ 가 fault. 건너뛸 때 ITSTATE 를 진행시키는 `it_advance(xpsr)` 를 써라 (EPSR 의 IT[1:0] = xPSR[26:25], IT[7:2] = xPSR[15:10]).

<details>
<summary>답</summary>

Arm ARM 의 ITAdvance: IT[2:0] == 000 이면 IT = 0 (블록 끝), 아니면 IT[4:0] 을 왼쪽으로 1. 실측: fault 때 쌓인 xPSR = 0x61000c00 → IT = 0x0C (ITE EQ = 0000_1100), 진행 후 0x18 (현재 조건 0001 = NE, IT[2:0]=000 → 마지막 슬롯). 진행시키면 MOVNE 가 실행 안 돼 반환 6 (= 5+1). 진행 안 시키면 반환 **7**: MOVNE 가 아직 남은 "EQ" 조건으로 실행되고, 그다음 ADDS 가 "NE" 슬롯으로 처리돼 무시된다. flag≠0 이면 fault 없이 8.

</details>

### 연습 5 — 저장 fault 와 ACTLR

`str r1, [0x30000004]` 의 원인·BFAR·PC 를 확인하고, ACTLR.DISDEFWBUF 를 켜 보라.

<details>
<summary>답</summary>

실측: BusFault, `CFSR=0x00008200` (PRECISERR|BFARVALID), BFAR=0x30000004, PC = `fault_str_insn` 그대로, skip 2. ACTLR 은 쓰기 전후 모두 0x00000000 (QEMU 는 RAZ/WI). 실제 M3 이었다면 이 저장은 IMPRECISERR 로 PC 가 뒤로 밀렸을 가능성이 크다 — 그래서 시험은 "QEMU 실측" 이라고 주석을 달았다.

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"    # 19/19 passed, qemu exit=0
```

실행 결과 (실제 출력, PASS 줄 생략):

```text
ARM-M12 practice (QEMU mps2-an385)
  #1 IPSR=6 CFSR=0x00010000 HFSR=0x00000000 pc=0x000007e2 lr=0xfffffff9 xpsr=0x2100000b exc_ret=0xfffffff1 -> skip 2
  #2 IPSR=3 CFSR=0x00010000 HFSR=0x40000000 pc=0x000007e2 lr=0xfffffff9 xpsr=0x0100000b exc_ret=0xfffffff1 -> skip 2
  #3 IPSR=6 CFSR=0x00080000 HFSR=0x00000000 pc=0x000007ec lr=0x00000a23 xpsr=0x01000000 exc_ret=0xfffffff9 -> skip 4
  #4 IPSR=6 CFSR=0x01000000 HFSR=0x00000000 pc=0x000007f6 lr=0x00000a23 xpsr=0x00000000 exc_ret=0xfffffff9 -> LR
  #5 IPSR=6 CFSR=0x02000000 HFSR=0x00000000 pc=0x000007fe lr=0x0000052b xpsr=0x61000c00 exc_ret=0xfffffff9 -> skip 4
  #6 IPSR=6 CFSR=0x02000000 HFSR=0x00000000 pc=0x000007fe lr=0x0000055f xpsr=0x61000c00 exc_ret=0xfffffff9 -> skip 4
  #7 IPSR=5 CFSR=0x00008200 HFSR=0x00000000 pc=0x00000808 lr=0x00000a23 xpsr=0x01000000 exc_ret=0xfffffff9 -> skip 2
  ACTLR before=0x00000000 after |= DISDEFWBUF: 0x00000000
19/19 passed
qemu exit=0
```

## 10. 핵심 정리

- fault 진입: 하드웨어가 {r0,r1,r2,r3,r12,LR,PC,xPSR} 를 **그때 쓰던 스택**에 쌓고 LR = EXC_RETURN. bit2 = PSP, bit3 = Thread (F1 / F9 / FD).
- 진입부는 asm: `tst lr,#4; ite eq; mrseq r0,msp; mrsne r0,psp; mov r1,lr; b hardfault_c`. C 프롤로그는 sp 를 옮긴다 (실측 192 B).
- CFSR = MMFSR[7:0] | BFSR[15:8] | UFSR[31:16], HFSR = VECTTBL 1 / FORCED 30 / DEBUGEVT 31. FORCED 면 원인은 CFSR 에 그대로.
- MMFAR/BFAR 는 MMARVALID(7)/BFARVALID(15) 일 때만. IACCVIOL·IMPRECISERR 에서는 무효.
- 쌓인 PC = 정밀 fault 의 명령 (명령 fault 14/14 실측). XN 실행·INVSTATE 는 PC 가 피해 현장 → 쌓인 LR 이 범인.
- 건너뛰기(시험용만): `hw1[15:11] >= 0b11101 ? 4 : 2`, T=0 이면 LR 로 + T=1, IT 블록이면 ITSTATE 진행, CFSR/HFSR 는 읽은 값으로 W1C.
- 실측 QEMU 차이: 저장도 PRECISERR, ACTLR RAZ/WI, (2 mod 4) 짝수 목표는 UNALIGNED 먼저. gdb `bt` 는 `<signal handler called>` 까지 풀지만 asm 함수에서 멈춘다; `addr2line -f` 는 asm 에서 이름이 틀릴 수 있다.

## 참고자료

- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403E) — B1.5.6 예외 진입 스태킹과 EXC_RETURN, B1.5.14 fault 종류, B3.2.15 CFSR (MMFSR/BFSR/UFSR), B3.2.16 HFSR, B3.2.17–18 MMFAR/BFAR, A7.3 ITSTATE·ITAdvance, A6.1 Thumb 명령 길이 (절 번호 확인 권장).
- Arm, *Cortex-M3 Devices Generic User Guide* (DUI 0552A) — 4.3.10–4.3.13 CFSR/HFSR/MMFAR/BFAR, 2.3.7 exception entry and return; *Cortex-M3 TRM* — ACTLR.DISDEFWBUF, 쓰기 버퍼.
- Arm Application Note 209, *Using Cortex-M3/M4/M7 Fault Exceptions* — 핸들러 진입부 예제(`TST LR,#4`), BFAR 재확인, 부정확 BusFault 디버깅.
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* (3rd ed.) — 12 장 Fault exceptions, 12.8 fault 분석.
- CMSIS 5 `core_cm3.h` — `SCB_CFSR_*_Msk`, `SCB_HFSR_*`, `__get_MSP/__get_PSP`.
- GDB 문서 — ARM M-profile 예외 프레임 unwinding (`<signal handler called>`); binutils `addr2line`, `readelf -s` (Thumb 심볼 bit0).
- QEMU 10.2.1 소스 `target/arm/tcg/translate.c` (T=0 일 때 처리), `hw/intc/armv7m_nvic.c` (CFSR/HFSR/ACTLR) — 이 레슨의 QEMU 특이점 해석은 추정, 소스로 확인 권장.
- 관련 레슨: ARM-B12 (Thumb 명령 길이 규칙), ARM-B13 (IT 블록), ARM-B14 (MSP/PSP, `run_on_psp`), BM-M13 (fault 정책·QEMU 실측 P1–P7), BM-B08 (C 벡터 테이블, Lockup). 같은 시기 작성: ARM-M08 (벡터 테이블·Thumb 비트), ARM-M09 (예외 프레임·EXC_RETURN 해독), ARM-M14 (AArch64 크래시 덤프). 다음 단계: BM-A09 (크래시 덤프를 .noinit 에), TOOL-A05 (덤프 형식), TOOL-M14 (addr2line/DWARF).
