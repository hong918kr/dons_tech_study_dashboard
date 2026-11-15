---
id: ARM-M13
time: 80분
updated: 2026-11-15
---
# ARM-M13 AArch64 예외 레벨과 시스템 레지스터

## 학습 목표

- EL0–EL3 의 역할(앱 / OS 커널 / 하이퍼바이저 / 보안 모니터)과 각 EL 의 스택 포인터(SP_EL0, SP_EL1, …)를 그림 하나로 설명하고, `CurrentEL` 을 읽어 지금 몇 EL 인지 출력한다.
- `VBAR_EL1` 에 2 KB 정렬된 16 엔트리 × 0x80 벡터 테이블을 직접 써서 설치하고, 예외가 **어느 엔트리로** 들어오는지(0x000 / 0x200 / 0x400 …) 실측으로 맞힌다.
- 예외 진입 때 하드웨어가 하는 일(PC → `ELR_EL1`, PSTATE → `SPSR_EL1`, 원인 → `ESR_EL1`, 주소 → `FAR_EL1`, DAIF 마스크, SPSel = 1)과 `ERET` 이 되돌리는 일을 말하고, `decode_esr()` 로 EC/IL/ISS(데이터 어보트의 DFSC·WnR·ISV)를 푼다.
- SVC 는 ELR = 다음 명령, BRK·UDF·어보트는 ELR = 그 명령 자신이라는 차이를 테스트로 확인하고, 틀렸을 때 생기는 무한 예외·명령 건너뛰기를 실제로 재현한다.
- EL2 로 부팅해서 `HCR_EL2.RW` → `SPSR_EL2` → `ELR_EL2` → `ERET` 으로 EL1 에 내려오고, 다시 EL0 으로 내려가 `svc` 로 돌아오는 "시스템 콜" 왕복을 만든다.

## 1. 문제

"부트로더가 EL2 에서 커널을 부르면 커널은 뭘 먼저 해?", "EL1 에서 SVC 받으면 ESR 에 뭐가 찍혀?", "데이터 어보트 났는데 ELR 이랑 FAR 중 뭘 봐?" — Cortex-A 기반 SoC(i.MX8, Jetson, Zynq MPSoC, 차량용 SoC)의 부트로더·커널·하이퍼바이저·보안 펌웨어 면접과 크래시 덤프 분석은 전부 이 레지스터 몇 개 위에서 이루어진다. Cortex-M(ARM-B14, ARM-M09)은 하드웨어가 레지스터를 스택에 쌓아 주고 EXC_RETURN 이라는 특수 값으로 복귀했다. AArch64 는 그 일을 **시스템 레지스터 + 소프트웨어 벡터 코드**로 나눠 맡긴다.

이번 레슨은 qemu-system-aarch64 `-M virt -cpu cortex-a53` 에서 MMU 를 켜지 않은 **베어메탈** 이미지를 직접 만들어 돌린다. 출력은 Arm 세미호스팅(`hlt #0xf000`)으로 하고, 테스트 결과로 QEMU 종료 코드를 정한다 (Cortex-M 레슨들과 같은 구조).

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">AArch64 예외 레벨 — 숫자가 클수록 특권이 크다 (괄호 = 이 레슨에서 실측한 부팅 EL)</text>
  <rect x="20" y="40" width="380" height="54" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="32" y="62" fill="currentColor" font-weight="bold">EL3 보안 모니터 (TF-A BL31)</text>
  <text x="32" y="82" fill="currentColor" font-size="12" font-family="monospace">SP_EL3 · VBAR_EL3 · SCR_EL3   (-M virt,secure=on → 3)</text>
  <rect x="20" y="108" width="380" height="54" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="130" fill="currentColor" font-weight="bold">EL2 하이퍼바이저 (KVM, Xen)</text>
  <text x="32" y="150" fill="currentColor" font-size="12" font-family="monospace">SP_EL2 · VBAR_EL2 · HCR_EL2  (virtualization=on → 2)</text>
  <rect x="20" y="176" width="380" height="54" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="32" y="198" fill="currentColor" font-weight="bold">EL1 OS 커널 / RTOS / 이 레슨의 main</text>
  <text x="32" y="218" fill="currentColor" font-size="12" font-family="monospace">SP_EL1 · VBAR_EL1 · ESR/ELR/SPSR/FAR_EL1 (-M virt → 1)</text>
  <rect x="20" y="244" width="380" height="54" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="32" y="266" fill="currentColor" font-weight="bold">EL0 앱 / 유저 태스크 (el0_main)</text>
  <text x="32" y="286" fill="currentColor" font-size="12" font-family="monospace">SP_EL0 만. 시스템 레지스터 접근 = 예외</text>
  <path d="M440 270 L440 198" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M435 206 L440 198 L445 206" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="450" y="240" fill="currentColor" font-family="monospace">svc #imm</text>
  <path d="M530 198 L530 130" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M525 138 L530 130 L535 138" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="540" y="170" fill="currentColor" font-family="monospace">hvc #imm</text>
  <path d="M620 198 L620 62" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M615 70 L620 62 L625 70" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="630" y="110" fill="currentColor" font-family="monospace">smc #imm</text>
  <path d="M700 62 L700 270" stroke="currentColor" stroke-width="2" stroke-dasharray="6 4" fill="none"/>
  <path d="M695 262 L700 270 L705 262" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="640" y="300" fill="currentColor" font-family="monospace">eret (아래로)</text>
  <text x="20" y="322" fill="currentColor" font-size="12">위로 가는 길 = 예외 (동기: svc/hvc/smc/brk/어보트, 비동기: IRQ/FIQ/SError). 아래로 가는 길 = ERET 하나뿐.</text>
  <text x="20" y="342" fill="currentColor" font-size="12">각 EL 은 자기 SP_ELx 를 가진다. EL1 이상은 SPSel 로 "SP_EL0 을 빌려 쓸지(t)" "자기 것(h)" 을 고른다.</text>
</svg>
```

### Part A — CurrentEL, VBAR_EL1, SVC → ESR 디코딩 → ERET

- `get_current_el()` (asm): `CurrentEL` 의 bit[3:2] 를 0..3 으로 돌려준다.
- `set_vbar_el1(table)` (asm): 2 KB 정렬된 `vectors_el1` 을 설치한다.
- 벡터 공통 경로 `exc_common` (asm): x0–x18, x29, x30 을 저장하고 `ESR_EL1/ELR_EL1/SPSR_EL1/FAR_EL1` 을 프레임에 담아 C 함수 `exc_handler(frame)` 를 부른 뒤, C 가 고친 ELR/SPSR 을 되돌려 쓰고 `ERET`.
- `decode_esr(esr, &info)` (C): EC [31:26], IL [25], ISS [24:0], SVC/HVC/BRK 의 imm16, 어보트의 DFSC/WnR/ISV/SRT/SAS.
- 테스트: `svc #0x42` → ESR = 0x56000042, 엔트리 0x200, ELR = svc 다음 명령, SPSR.M = EL1h. 핸들러가 x0 에 1 을 더하고, 돌아가서 svc 다음의 `add x0, x0, #100` 이 실행돼야 106 이 나온다.

### Part B — ELR 규칙이 다른 예외들 (`exc_handler` 의 복귀 정책)

- `brk #7` → EC 0x3C, **ELR = brk 자신** → 핸들러가 ELR += 4.
- `udf #0x1234` → EC 0x00 (Unknown reason), ELR = udf 자신.
- `hvc #0` (EL2 없음) → 예외가 오는가? (QEMU 실측이 예상과 다르다 — 5. 풀이)
- 아무것도 없는 주소 `0x50000000` 읽기/쓰기 → EC 0x25 데이터 어보트, FAR = 그 주소, WnR 로 읽기/쓰기 구분, DFSC = ?
- MMU off 상태에서 **비정렬 `ldr`** → 정렬 폴트 (DFSC 0x21). 왜 MMU 를 안 켰는데 정렬 폴트가 나나?
- 버그 재현: BRK 뒤 ELR 을 안 옮기기 → 같은 명령으로 무한 반복(폭주 감시가 exit 3), SVC 에도 ELR += 4 → 명령 하나 건너뛰기.

### Part C — EL2 → EL1 → EL0 → EL1

- `el2_to_el1()` (asm, `_start` 가 EL2 부팅일 때 부른다): `HCR_EL2.RW = 1`, `SP_EL1 = sp`, `SPSR_EL2 = 0x3C5`(EL1h, DAIF 마스크), `ELR_EL2 = x30`, `ERET` — "EL2 에서 불려서 EL1 에서 돌아오는 함수".
- `run_el0(fn, arg, sp_top)` (asm): EL1 쪽 callee-saved 를 SP_EL1 에 저장 → `SP_EL0 = sp_top`, `ELR_EL1 = fn`, `SPSR_EL1 = 0`(EL0t) → `ERET`. EL0 의 `svc #1` 은 x0 *= 10 시스템 콜, `svc #0` 은 exit — 하위 EL 벡터(0x400)에서 받아 `run_el0` 의 호출자에게 돌아간다.
- EL0 에서 `mrs x0, CurrentEL` → 금지 → EL1 이 0xDEAD 로 대신 돌려준다.

| 파일 | 역할 |
|---|---|
| `starter.c` / `solution.c` | 펌웨어 테스트(main) + `decode_esr` + `exc_handler` (TODO / 정답) |
| `starter.S` / `solution.S` | `get_current_el`, `set_vbar_el1`, 벡터 테이블·`exc_common`, `el2_to_el1`, `run_el0` (TODO / 정답) + 예외를 일으키는 헬퍼 |
| `shared_start.S` | `_start` (SP, .bss, 부팅 EL 기록, EL3/EL2 면 내려가기), 세미호스팅 `sh_call`/`sh_exit` |
| `shared_rt.c` | `sh_puts/sh_hex/sh_dec`, `check()`, `finish()` ("N/M passed") |
| `arm_m13.ld` | 링커 스크립트 (0x40080000, 64 KB 부트 스택) |
| `run_qemu.sh` | VM 안에서 빌드·실행: `starter|solution|practice|el2|gdb|objdump|bug_*` |
| `demo_sysregs.gdb` / `demo_sysregs_out.txt` | gdb 로 본 시스템 레지스터 (실제 출력) |
| `practice.c` / `practice.S` | 추가 연습 답 (본문을 `#include` 해서 그대로 쓰고 덧붙인다) |

## 2. Problem (English)

> **AArch64 exception levels, bare metal.** Target: QEMU `-M virt -cpu cortex-a53`, MMU off, no interrupts, semihosting output. Build with a freestanding AArch64 toolchain and your own linker script.
>
> 1. `uint64_t get_current_el(void)` — return the current EL (0..3) from `CurrentEL`.
> 2. Write a 2 KB-aligned EL1 vector table (16 entries × 0x80 bytes: {current EL with SP_EL0, current EL with SP_ELx, lower EL AArch64, lower EL AArch32} × {Sync, IRQ, FIQ, SError}) and `void set_vbar_el1(const void *)`. Each entry must save enough state to call a C handler `int exc_handler(struct exc_frame *)` that sees `ESR_EL1`, `ELR_EL1`, `SPSR_EL1`, `FAR_EL1` and the entry index, may modify the saved ELR/SPSR/x0, and returns via `ERET`.
> 3. `uint32_t decode_esr(uint64_t esr, struct esr_info *out)` — split EC/IL/ISS; for SVC/HVC/SMC/BRK extract imm16; for data aborts extract DFSC, WnR, ISV (and SRT/SAS when ISV = 1).
> 4. Handle: `svc #0x42` (return to the next instruction, add 1 to x0), `brk #7` and `udf` (skip the instruction), a load/store to an unmapped address and a misaligned load (skip it, return 0xBAD). Explain why the misaligned load faults although the MMU is off.
> 5. If the image is entered at EL2, drop to EL1h with all of DAIF masked before `main`. Then run a C function at EL0 on its own stack; it makes `svc` system calls and finally `svc #0` to exit, and `run_el0()` returns its exit code at EL1.
>
> Example (real run): `CurrentEL = 1`, `VBAR_EL1 = 0x40081800`; `svc #0x42` → `ESR_EL1 = 0x56000042`, `ELR_EL1 = 0x40081004`, `SPSR_EL1 = 0x600003c5`, entry `0x200`; load from `0x50000000` → `ESR_EL1 = 0x96000010`, `FAR_EL1 = 0x50000000`; EL0 `svc #1` → entry `0x400`, `SPSR_EL1 = 0`.
>
> Follow-ups: What does each EL run in a real phone/car SoC? Why is ELR different for SVC and BRK? Where does the handler's stack come from when EL0 traps? Why must `HCR_EL2.RW` be set before dropping to EL1? What happens to an `hvc` when there is no EL2?

## 3. 시작 코드

C 쪽 (테스트 하네스 + TODO 두 개: `decode_esr`, `exc_handler` 의 복귀 정책):

<!-- code: starter.c -->

어셈블리 쪽 (TODO: `get_current_el`, `set_vbar_el1`, `exc_common` 의 시스템 레지스터 읽기/쓰기, `el2_to_el1`, `run_el0`):

<!-- code: starter.S -->

주어진 부트 코드와 런타임 (TODO 없음):

<!-- code: shared_start.S -->

<!-- code: shared_rt.c -->

<!-- code: arm_m13.ld -->

빌드·실행은 Lima VM `elinux` (aarch64 Ubuntu, gcc 15.2, clang 21.1, qemu-system-aarch64 10.2.1, gdb-multiarch) 안에서 한다. VM 이 aarch64 리눅스라 **네이티브 gcc 가 곧 AArch64 컴파일러**다 — `-ffreestanding -nostdlib -nostartfiles -static -T arm_m13.ld` 로 libc·crt 없이 베어메탈 ELF 를 만든다. Mac 홈은 VM 에서 읽기 전용이라 스크립트가 `/tmp/ARM-M13_work` 로 복사해서 빌드한다. 맥에서 한 줄:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ARM/ARM-M13
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"   # solution / practice / el2 / gdb / objdump / bug_noadvance / bug_svcadvance / bug_simd / bug_norw
```

`run_qemu.sh` 는 `gcc <플래그> -T arm_m13.ld starter.c starter.S shared_start.S shared_rt.c -lgcc` 로 빌드하고 `timeout 20 qemu-system-aarch64 -M virt -cpu cortex-a53 -nographic -monitor none -serial none -semihosting-config enable=on,target=native -kernel starter.elf` 를 두 번(그냥 `virt`, `virt,virtualization=on`) 돌린다.

플래그가 많은 이유: 이 VM 의 Ubuntu gcc 는 기본으로 PIE(`--enable-default-pie`)와 `-fstack-protector-strong`(`__stack_chk_guard` 참조)을 켜므로 끈다 (`gcc -Q --help=common` 실측). `-mbranch-protection=none` 은 배포판에 따라 PAC/BTI 힌트가 기본으로 켜지는 경우를 대비한 것 — 이 gcc 15 는 실측으로 안 켠다. `-mgeneral-regs-only` 는 컴파일러가 NEON/FP 레지스터를 못 쓰게 하고(안 그러면 실제로 트랩 — 7. 실수 3), `-mstrict-align` 은 비정렬 접근을 만들지 않게 한다(MMU off = Device 메모리 — 5. 풀이). clang + ld.lld 로도 같은 소스가 돌아간다 (`-no-pie` 를 빼고 `-fuse-ld=lld` — 실측 36/36).

<!-- code: run_qemu.sh -->

처음 실행 결과 (실제 출력, 두 번째 부팅도 같은 모양):

```text
=== qemu-system-aarch64 -M virt -cpu cortex-a53 -kernel starter.elf ===
ARM-M13 AArch64 exception levels (QEMU virt, cortex-a53)
  boot EL     = 0x1
  CurrentEL   = 0x0
  DAIF        = 0x3c0
FAIL line 159: el == 1u
PASS g_boot_el >= 1u
PASS (ADDR(vectors_el1) & 0x7FFu) == 0u
FAIL line 182: ok == sizeof tv / sizeof tv[0]
FAIL line 185: d7.sas == 3u && d7.is_abort == 1u && d7.iss == 0x1C10045u
  VBAR_EL1    = 0x0
FAIL line 189: read_vbar_el1() == ADDR(vectors_el1)
  (EL1 + VBAR_EL1 not ready: exception tests skipped)
FAIL line 193: ready
2/7 passed
qemu exit=1
```

**안전장치**: 벡터가 설치되지 않았거나 EL1 이 아니면 예외 테스트를 아예 건너뛴다. VBAR_EL1 리셋 값(QEMU 에서 0)으로 예외가 가면 0x200 — virt 의 플래시(0 으로 채워짐, 0x00000000 = `udf #0`)로 뛰어 같은 예외를 영원히 반복하기 때문이다 (7. 실수 3 의 실측 로그). 또 `exc_handler` 는 예외가 64 번을 넘으면 "exception storm" 을 찍고 exit 3 으로 끝낸다.

VM 없이 맥에서 "어셈블은 되나?" 만 확인:

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
clang --target=aarch64-none-elf -c solution.S -o /tmp/ARM-M13_solution.o && /usr/bin/objdump -d /tmp/ARM-M13_solution.o
```

```text
    1040: d53c1100     	mrs	x0, HCR_EL2
    104c: b2610000     	orr	x0, x0, #0x80000000
    1068: d51c4000     	msr	SPSR_EL2, x0
    1070: d69f03e0     	eret
```

(`<el2_to_el1>` 의 일부만.) 이 .S 들은 처음부터 ELF/GNU as 문법이다 (`.global name`, `.type name, %function`, `.size`) — Apple Mach-O 의 `_name` 규칙(ARM-B01)은 해당 없다.

## 4. 힌트

<details>
<summary>힌트 1 — 시스템 레지스터는 MRS/MSR 로만</summary>

`mrs x0, CurrentEL` / `mrs x1, esr_el1` / `msr vbar_el1, x0` / `msr elr_el2, x30`. 이름 뒤의 `_ELn` 은 "이 레지스터를 쓰려면 최소 ELn 이어야 한다" 는 뜻에 가깝다 (EL1 에서 `_EL2` 를 만지면 예외). `CurrentEL` 은 EL 을 bit[3:2] 에 담으므로 `ubfx x0, x0, #2, #2`.

</details>

<details>
<summary>힌트 2 — VBAR 은 2 KB 정렬, 엔트리는 0x80</summary>

`.p2align 11` 로 테이블 시작을 2048 바이트 경계에, 각 엔트리 앞에 `.p2align 7`. 엔트리 32 명령 안에 다 못 넣으면 `b common` 으로 넘긴다. 시스템 레지스터를 쓴 뒤 "다음 예외부터 새 값" 이 보장되려면 `isb` (ARM-M05).

</details>

<details>
<summary>힌트 3 — 핸들러의 sp 는 어디서 오나</summary>

예외를 받으면 PSTATE.SPSel 이 1 이 되어 sp = **SP_EL1** 이다. EL0 에서 왔든 EL1t 에서 왔든 마찬가지. 그러니 SP_EL1 은 늘 쓸 수 있는 스택이어야 한다. `run_el0` 은 EL0 으로 내려가기 전에 자기 레지스터를 SP_EL1 에 저장해 두면, EL0 의 `svc #0` 이 벡터로 들어온 순간의 sp 가 바로 그 저장 영역 바로 아래다.

</details>

<details>
<summary>힌트 4 — "EL2 에서 불려 EL1 에서 돌아오는 함수"</summary>

`ret` 은 x30 으로 점프만 한다. `eret` 은 PC ← ELR_EL2, PSTATE ← SPSR_EL2 를 **동시에** 한다. 그러니 `ELR_EL2 = x30`, `SPSR_EL2 = EL1h(0b0101) + DAIF(0xF << 6)`, `SP_EL1 = 지금 sp` 를 준비하고 `eret` 하면 호출자에게, EL1 로 돌아간다. EL1 이 AArch64 로 돌려면 `HCR_EL2` 의 bit 31(RW).

</details>

<details>
<summary>힌트 5 — ELR 은 언제 +4 해야 하나</summary>

"이 명령을 **다시** 실행하면 되는가?" 로 판단한다. SVC/HVC/SMC 는 명령이 끝나서 생긴 예외라 ELR 이 이미 다음 명령. BRK·미정의·어보트는 ELR 이 그 명령 — 건너뛰려면 +4 (A64 명령은 항상 4 바이트).

</details>

## 5. 풀이

### 0. 규칙 정리 — 어디에 쓰여 있나

| 규칙 | 근거 (확인 권장 표시는 기억에 의존한 부분) |
|---|---|
| `CurrentEL[3:2]` = EL, 나머지 RES0. EL0 에서 접근 불가 | Arm ARM (DDI 0487) D17/C5 "CurrentEL". 실측: EL0 의 `mrs CurrentEL` → ESR 0x02000000 |
| 예외 진입: ELR_ELx ← 복귀 주소, SPSR_ELx ← PSTATE, PSTATE.DAIF ← 1111, SPSel ← 1, PC ← VBAR_ELx + 오프셋 | Arm ARM D1 "Exception entry". 실측: gdb (3) |
| 벡터 오프셋: 0x000 현재 EL+SP_EL0, 0x200 현재 EL+SP_ELx, 0x400 하위 EL AArch64, 0x600 하위 EL AArch32, 각 묶음 안 Sync/IRQ/FIQ/SError = +0/+0x80/+0x100/+0x180 | Arm ARM D1 "Exception vectors". 실측: svc → 0x200, EL0 svc → 0x400, EL1t svc → 0x000 |
| VBAR_ELx[10:0] RES0 → 2 KB 정렬 | Arm ARM VBAR_EL1 설명 |
| ESR: EC [31:26], IL [25] (1 = 32비트 명령), ISS [24:0] | Arm ARM ESR_EL1. EC 값 표는 아래 |
| SVC/HVC/SMC 의 ELR = 다음 명령, 동기 어보트·미정의·BRK 의 ELR = 그 명령 | Arm ARM "preferred return address". 실측 표 아래 |
| ERET: PC ← ELR, PSTATE ← SPSR (EL, SPSel, DAIF, NZCV 포함), 문맥 동기화(context synchronizing) | Arm ARM ERET 의사코드 (동기화 여부는 확인 권장) |
| MMU off(SCTLR_EL1.M = 0): 데이터 접근 = Device-nGnRnE → 비정렬 접근은 정렬 폴트 | Arm ARM D8 "behavior when stage 1 translation is disabled" (확인 권장). 실측: DFSC 0x21 |
| HCR_EL2.RW (bit 31) = 1 → EL1 이 AArch64 | Arm ARM HCR_EL2. 실측: 안 켜면 ERET 이 illegal return (7. 실수 4) |

### Part A — 부팅해서 EL1 의 첫 예외를 받기까지

**1) 부팅 EL.** QEMU `-M virt -kernel x.elf` 는 ELF 의 물리 주소(0x40080000)에 이미지를 올리고 그 머신에서 가장 높은 EL 에서 `e_entry` 로 시작한다. 실측: 그냥 virt → EL1, `virtualization=on` → EL2, `secure=on` → EL3. gdb 로 본 리셋 직후:

```text
(1) reset    pc=0x40080000 cpsr=0x400003c5 EL=1 VBAR_EL1=0 SCTLR_EL1=0xc50838
```

`cpsr` (gdb 가 PSTATE 를 이렇게 부른다) = 0x3c5 = DAIF 전부 마스크 + M = 0101 (EL1h). `SCTLR_EL1 = 0xc50838` 의 bit0(M, MMU) = 0, bit2(C, 데이터 캐시) = 0, bit1(A, 정렬 검사) = 0, bit3(SA, sp 정렬 검사) = 1. 즉 MMU·캐시가 꺼진 채로 시작한다. QEMU 는 RAM 에 아무것도 넘겨주지 않았다 (실측: ELF 부팅에서 x0 = 0, 0x40000000 = 0 — 0x40000000 에 링크해도 실측으로 동작했다).

**2) CurrentEL.** `mrs x0, CurrentEL` → 0x4 (EL1) → `ubfx x0, x0, #2, #2` → 1. objdump 로는 `d5384240 mrs x0, currentel`.

**3) 벡터 테이블.** 링커가 `vectors_el1` 을 0x40081800 에 뒀다 (`.text` 정렬이 2**11 이 됨: `objdump -h` 의 `.text ... 2**11`). 엔트리 하나는 4 명령 + nop 채움이다:

```text
0000000040081800 <vectors_el1>:
    40081800:	d10383ff 	sub	sp, sp, #0xe0
    40081804:	a90007e0 	stp	x0, x1, [sp]
    40081808:	d2800000 	mov	x0, #0x0                   	// #0
    4008180c:	140001e1 	b	40081f90 <exc_common>
    40081810:	d503201f 	nop
```

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">VBAR_EL1 = 0x40081800 — 16 엔트리 × 0x80 (실측으로 맞은 엔트리 강조)</text>
  <text x="170" y="46" fill="currentColor" font-family="monospace">+0x000 Sync</text>
  <text x="310" y="46" fill="currentColor" font-family="monospace">+0x080 IRQ</text>
  <text x="450" y="46" fill="currentColor" font-family="monospace">+0x100 FIQ</text>
  <text x="590" y="46" fill="currentColor" font-family="monospace">+0x180 SError</text>
  <text x="10" y="80" fill="currentColor" font-size="12">0x000 현재 EL, SP_EL0</text>
  <rect x="165" y="60" width="130" height="34" class="f-acc" stroke="currentColor"/>
  <text x="172" y="82" fill="currentColor" font-size="12">kind 0: 연습 1 EL1t</text>
  <rect x="305" y="60" width="130" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="445" y="60" width="130" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="585" y="60" width="140" height="34" class="f-mut" stroke="currentColor"/>
  <text x="10" y="130" fill="currentColor" font-size="12">0x200 현재 EL, SP_ELx</text>
  <rect x="165" y="110" width="130" height="34" class="f-acc" stroke="currentColor"/>
  <text x="172" y="125" fill="currentColor" font-size="12">kind 4: svc brk udf</text>
  <text x="172" y="139" fill="currentColor" font-size="12">어보트 (EL1h)</text>
  <rect x="305" y="110" width="130" height="34" class="f-mut" stroke="currentColor"/>
  <text x="312" y="132" fill="currentColor" font-size="12">ARM-A02</text>
  <rect x="445" y="110" width="130" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="585" y="110" width="140" height="34" class="f-mut" stroke="currentColor"/>
  <text x="10" y="180" fill="currentColor" font-size="12">0x400 하위 EL, AArch64</text>
  <rect x="165" y="160" width="130" height="34" class="f-ok" stroke="currentColor"/>
  <text x="172" y="182" fill="currentColor" font-size="12">kind 8: EL0 svc·mrs</text>
  <rect x="305" y="160" width="130" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="445" y="160" width="130" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="585" y="160" width="140" height="34" class="f-mut" stroke="currentColor"/>
  <text x="10" y="230" fill="currentColor" font-size="12">0x600 하위 EL, AArch32</text>
  <rect x="165" y="210" width="130" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="305" y="210" width="130" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="445" y="210" width="130" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="585" y="210" width="140" height="34" class="f-mut" stroke="currentColor"/>
  <text x="10" y="272" fill="currentColor" font-size="12" font-family="monospace">엔트리 = sub sp,sp,#0xe0 · stp x0,x1,[sp] · mov x0,#kind · b exc_common  (나머지 28 칸 nop)</text>
  <text x="10" y="294" fill="currentColor" font-size="12">kind = (오프셋 / 0x80). 같은 EL 의 예외라도 SPSel(t/h) 에 따라 0x000 / 0x200 으로 갈린다.</text>
  <text x="10" y="314" fill="currentColor" font-size="12">이 레슨은 IRQ/FIQ/SError 를 켜지 않는다 → 회색 칸은 설치만 되고 실행되지 않는다.</text>
</svg>
```

**4) `svc #0x42` 한 명령 동안 일어나는 일** — gdb `stepi` 한 번의 실측 (`run_qemu.sh gdb`, `demo_sysregs.gdb`):

```text
(2) at svc   pc=0x40081000 cpsr=0x600003c5 sp=0x400944b0 VBAR_EL1=0x40081800
(3) stepi -> pc=0x40081a00 (VBAR+0x200) cpsr=0x3c5 sp=0x400944b0
             ESR_EL1=0x56000042 ELR_EL1=0x40081004 SPSR_EL1=0x600003c5
(4) after eret pc=0x40081004 cpsr=0x600003c5 x0=6
```

| 단계 | PC | PSTATE (cpsr) | ESR_EL1 | ELR_EL1 | SPSR_EL1 |
|---|---|---|---|---|---|
| svc 직전 | 0x40081000 | 0x600003c5 (Z C, DAIF, EL1h) | — | — | — |
| svc 실행 = 예외 진입 | **0x40081a00** = VBAR + 0x200 | 0x3c5 (NZCV 는 새 값, DAIF 1111, EL1h) | **0x56000042** | **0x40081004** (다음 명령) | **0x600003c5** (직전 PSTATE 복사) |
| 벡터 → exc_common → C → ERET | 0x40081004 | 0x600003c5 (SPSR 에서 복원) | | | |

하드웨어가 한 일은 딱 레지스터 네 개를 쓰고 PC 를 바꾼 것뿐이다. 범용 레지스터는 하나도 저장하지 않았다 — 그래서 엔트리 첫 명령이 `sub sp` + `stp x0, x1` 이다. sp 가 바뀌지 않은 것(0x400944b0)도 볼 수 있다: 예외 전에도 EL1h 였으니 같은 SP_EL1 을 계속 쓴다.

**5) ESR 0x56000042 풀기.**

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">ESR_EL1 (하위 32 비트) — 실측 두 값으로 읽기</text>
  <rect x="20" y="36" width="150" height="30" class="f-acc" stroke="currentColor"/>
  <text x="30" y="56" fill="currentColor" font-family="monospace">EC [31:26]</text>
  <rect x="170" y="36" width="70" height="30" class="f-warn" stroke="currentColor"/>
  <text x="178" y="56" fill="currentColor" font-family="monospace">IL [25]</text>
  <rect x="240" y="36" width="480" height="30" class="f-mut" stroke="currentColor"/>
  <text x="250" y="56" fill="currentColor" font-family="monospace">ISS [24:0] — EC 마다 뜻이 다르다</text>
  <text x="20" y="96" fill="currentColor" font-family="monospace">0x56000042 = 010101 1 0000000 0000000001000010</text>
  <text x="20" y="116" fill="currentColor" font-size="12">EC = 0x15 (SVC, AArch64)   IL = 1 (32비트 명령)   ISS[15:0] = imm16 = 0x42</text>
  <text x="20" y="150" fill="currentColor" font-weight="bold">데이터 어보트 ISS (EC 0x24 하위 EL / 0x25 같은 EL)</text>
  <rect x="20" y="162" width="56" height="30" class="f-ok" stroke="currentColor"/>
  <text x="26" y="182" fill="currentColor" font-family="monospace">ISV</text>
  <rect x="76" y="162" width="64" height="30" class="f-mut" stroke="currentColor"/>
  <text x="82" y="182" fill="currentColor" font-family="monospace">SAS</text>
  <rect x="140" y="162" width="70" height="30" class="f-mut" stroke="currentColor"/>
  <text x="146" y="182" fill="currentColor" font-family="monospace">SSE</text>
  <rect x="210" y="162" width="80" height="30" class="f-mut" stroke="currentColor"/>
  <text x="216" y="182" fill="currentColor" font-family="monospace">SRT</text>
  <rect x="290" y="162" width="200" height="30" class="f-mut" stroke="currentColor"/>
  <text x="296" y="182" fill="currentColor" font-family="monospace">SF AR … FnV EA CM S1PTW</text>
  <rect x="490" y="162" width="70" height="30" class="f-warn" stroke="currentColor"/>
  <text x="498" y="182" fill="currentColor" font-family="monospace">WnR</text>
  <rect x="560" y="162" width="160" height="30" class="f-acc" stroke="currentColor"/>
  <text x="570" y="182" fill="currentColor" font-family="monospace">DFSC [5:0]</text>
  <text x="20" y="210" fill="currentColor" font-size="12" font-family="monospace">24    23:22  21    20:16       15 … 7                6        5:0</text>
  <text x="20" y="240" fill="currentColor" font-family="monospace">0x96000050 = EC 0x25, IL 1, ISV 0, WnR 1, DFSC 0x10</text>
  <text x="20" y="260" fill="currentColor" font-size="12">= 같은 EL 에서 쓰기 중 동기 외부 어보트 (store64 → 0x50000008). FAR_EL1 = 그 주소.</text>
  <text x="20" y="284" fill="currentColor" font-size="12">ISV = 0 이면 SAS/SRT 는 무의미 → 어느 레지스터였는지는 ELR 의 명령을 디스어셈블해서 본다.</text>
</svg>
```

자주 보는 EC (decode_esr 의 이름표와 같다):

| EC | 이름 | ELR 이 가리키는 곳 | 이 레슨 |
|---|---|---|---|
| 0x00 | Unknown reason (미정의 명령 등) | 그 명령 | udf, EL0 의 `mrs CurrentEL`, EL2 없는데 smc |
| 0x07 | FP/SIMD 접근 트랩 (CPACR_EL1.FPEN) | 그 명령 | 7. 실수 3 |
| 0x0E | Illegal Execution state | 그 명령 | 7. 실수 4 (EL2 에서) |
| 0x15 / 0x16 / 0x17 | SVC / HVC / SMC (AArch64) | **다음 명령** | svc #0x42, EL0 svc #0/#1 |
| 0x18 | MSR/MRS/시스템 명령 트랩 | 그 명령 | (설정된 트랩이 있을 때) |
| 0x20 / 0x21 | 명령 어보트 하위 / 같은 EL | 그 명령 | — |
| 0x24 / 0x25 | 데이터 어보트 하위 / 같은 EL | 그 명령 | ldr/str 0x50000000, 비정렬 ldr |
| 0x22 / 0x26 | PC / SP 정렬 폴트 | 그 명령 | — |
| 0x2F | SError | (비동기) | — |
| 0x3C | BRK (AArch64) | **그 명령** | brk #7 |

### Part B — 실측 표: 어떤 예외가 무엇을 남기나

`run_qemu.sh solution` 첫 부팅의 실제 출력(`SVC/BRK/UDF/DABT/ALGN ... = ` 줄)을 표로 옮기면:

| 명령 | ESR_EL1 | EC | ELR_EL1 | 핸들러 정책 |
|---|---|---|---|---|
| `svc #0x42` | 0x56000042 | 0x15 | svc + 4 | x0 += 1, 그대로 ERET |
| `brk #7` | 0xF2000007 | 0x3C | brk 자신 | ELR += 4 |
| `udf #0x1234` | 0x02000000 | 0x00 | udf 자신 (ISS = 0: imm 은 안 남는다) | ELR += 4 |
| `ldr x0, [0x50000000]` | 0x96000010 | 0x25 | ldr 자신, FAR = 0x50000000 | ELR += 4, x0 = 0xBAD |
| `str x1, [0x50000008]` | 0x96000050 | 0x25 | str 자신, WnR = 1 | ELR += 4 |
| `ldr x0, [buf+1]` | 0x96000021 | 0x25 | DFSC 0x21 정렬 폴트, FAR = buf+1 | ELR += 4 |

**HVC 의 놀라운 결과.** EL2 가 없으면 HVC 는 미정의 명령이라 EC 0x00 예외가 와야 한다(Arm ARM 기준). 그런데 실측은 이랬다:

```text
  HVC PSCI_VERSION -> x0 = 0x10001
  HVC exceptions taken    = 0x0
  HVC fid 3        -> x0 = 0xffffffffffffffff
```

`hvc #0` 이 **예외 없이** x0 = 0x10001 을 돌려줬다. x0 = 0x84000000 은 PSCI_VERSION 함수 ID 이고 0x10001 = PSCI 1.1. 없는 ID(3)에는 −1 (NOT_SUPPORTED). 즉 QEMU 가 "EL2/EL3 펌웨어" 역할을 대신 해서 HVC 를 PSCI 호출로 처리한다 (`-kernel` 직접 부팅용 편의 기능: QEMU 는 EL2·EL3 를 게스트가 안 쓸 때 PSCI 를 흉내 낸다 — 확인 권장). 실리콘에서는 이 자리에 TF-A(EL3) 가 있고 리눅스는 DT 의 `psci { method = "smc" or "hvc"; }` 로 conduit 을 고른다. 연습 4 에서 부팅 방식별로 표를 만든다: `virtualization=on` 에선 PSCI 가 **smc** 로 옮겨 가고, `secure=on` 에선 PSCI 가 없어 hvc 가 진짜로 EC 0x00 예외가 된다.

**왜 MMU 를 안 켰는데 정렬 폴트?** `SCTLR_EL1.A = 0`(정렬 검사 끔)인데도 `ldr x0, [buf+1]` 이 DFSC 0x21 로 죽었다. stage 1 변환이 꺼져 있으면 **데이터 접근은 Device-nGnRnE 메모리로 취급**되고, Device 메모리에 대한 비정렬 접근은 SCTLR.A 와 무관하게 정렬 폴트다 (Arm ARM 기준, 확인 권장). 명령 fetch 는 Normal(캐시 정책은 SCTLR.I 에 따름)로 취급된다. 실무 결과:

- 부트로더·`_start` 처럼 MMU 를 켜기 전에 도는 C 코드는 `-mstrict-align` 으로 빌드해야 한다 (U-Boot·TF-A 의 arm64 빌드도 이 옵션을 쓴다 — 확인 권장). 이 레슨의 C 코드는 실측으로 `-mstrict-align` 없이도 비정렬 접근을 안 만들었지만, `memcpy`·packed 구조체가 들어오면 바로 터진다.
- 캐시도 꺼져 있어 느리고, exclusive(LDXR/STXR, ARM-M04)는 Device 메모리에서 동작이 보장되지 않는다 — 스핀락은 MMU+캐시를 켠 뒤에.
- FAR_EL1 = 정확히 buf+1 (0x...1). FAR 은 "접근한 가상 주소" 이고, MMU off 라 물리 주소와 같다.

**ISV = 0.** 세 데이터 어보트 모두 ISV(bit24) = 0 이라 SRT(어느 레지스터) 가 안 나왔다. Arm ARM 은 ISV = 1 을 주로 EL2 로 보고되는 stage 2 어보트(하이퍼바이저가 MMIO 를 흉내 낼 때 레지스터 번호가 필요)에서 요구하고, EL1 보고에서는 0 이어도 된다고 기억한다 — 확인 권장. 그래서 `decode_esr` 는 ISV 가 1 일 때만 SRT/SAS 를 채운다 (테스트 표의 0x93C10045 는 손으로 만든 ISV = 1 예).

### Part C — EL 사이를 오가기

**EL2 → EL1** (`run_qemu.sh solution` 두 번째 부팅, `-M virt,virtualization=on`):

```text
  boot EL     = 0x2
  CurrentEL   = 0x1
  HCR_EL2 before = 0x0
  HCR_EL2 after  = 0x80000000
```

`_start` 가 EL2 에서 `bl el2_to_el1` 하면 x30 = 0x40080070 (`bl main`). 함수는 HCR_EL2 를 0x0 → 0x80000000 (RW) 으로, `SP_EL1 = sp`, `SPSR_EL2 = 0x3c5` (DAIF 1111 | M 0101), `ELR_EL2 = x30` 을 쓰고 `eret` → 0x40080070 에서 EL1h 로 계속한다 (objdump 는 3. 의 Mac 출력).

**EL1 → EL0 → EL1** — gdb 실측 (5), (6):

```text
(5) EL0      pc=0x4008104c cpsr=0 EL=0 sp=0x40084510 SP_EL0=0x40084530 SP_EL1=0x40094450
(6) svc #1 -> pc=0x40081c00 (VBAR+0x400) EL=1 ESR_EL1=0x56000001 SPSR_EL1=0 sp=0x40094450
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">run_el0: 스택 두 개 (gdb 실측 주소)</text>
  <rect x="20" y="40" width="320" height="200" class="f-mut" stroke="currentColor"/>
  <text x="30" y="60" fill="currentColor" font-weight="bold">SP_EL1 영역 (부트 스택)</text>
  <rect x="40" y="74" width="280" height="44" class="f-acc" stroke="currentColor"/>
  <text x="50" y="92" fill="currentColor" font-size="12">run_el0 이 저장: x29 x30 x19–x28 (96 B)</text>
  <text x="50" y="110" fill="currentColor" font-size="12" font-family="monospace">SP_EL1 = 0x40094450 (EL0 동안 그대로)</text>
  <rect x="40" y="130" width="280" height="44" class="f-warn" stroke="currentColor"/>
  <text x="50" y="148" fill="currentColor" font-size="12">EL0 의 svc → 벡터: sub sp, #0xe0</text>
  <text x="50" y="166" fill="currentColor" font-size="12">exc_frame (x0–x18, fp, lr, ESR …)</text>
  <text x="30" y="200" fill="currentColor" font-size="12">svc #0 (exit) → 프레임 버리고 run_el0_ret 로:</text>
  <text x="30" y="218" fill="currentColor" font-size="12">저장해 둔 x19–x30 복원 → ret = run_el0 의 반환</text>
  <rect x="400" y="40" width="320" height="200" class="f-mut" stroke="currentColor"/>
  <text x="410" y="60" fill="currentColor" font-weight="bold">SP_EL0 영역 = g_el0_stack (4 KB)</text>
  <text x="560" y="84" fill="currentColor" font-family="monospace">0x40084530 top</text>
  <rect x="420" y="92" width="280" height="40" class="f-ok" stroke="currentColor"/>
  <text x="430" y="116" fill="currentColor" font-size="12">el0_main 프레임 (32 B)</text>
  <text x="560" y="152" fill="currentColor" font-family="monospace">0x40084510 sp</text>
  <text x="430" y="186" fill="currentColor" font-size="12">EL0 은 SP_EL1 을 볼 수도 바꿀 수도 없다</text>
  <path d="M400 112 C 370 112, 360 150, 340 150" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M348 145 L340 150 L348 156" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="352" y="104" fill="currentColor" font-size="12" font-family="monospace">svc</text>
  <text x="20" y="266" fill="currentColor" font-size="12">벡터로 들어오는 순간 SPSel = 1 → sp 는 자동으로 SP_EL1. EL0 이 sp 를 망가뜨려도 커널 스택은 안전하다.</text>
  <text x="20" y="286" fill="currentColor" font-size="12">gdb $SP_EL0 = 0x40084530 은 ERET 때 값 (QEMU 는 EL 이 바뀔 때만 뱅크에 복사, 지금 값은 $sp).</text>
</svg>
```

- `SPSR_EL1 = 0` → ERET 후 PSTATE.M = 0000 (EL0t), DAIF = 0. 인터럽트 마스크가 풀렸지만 GIC 를 설정하지 않아 IRQ 는 오지 않는다 (IRQ 처리는 ARM-A02).
- EL0 의 `svc #1` 은 **0x400** (하위 EL, AArch64) 으로 들어왔고 그때 sp = 0x40094450 = run_el0 이 남겨 둔 SP_EL1. 이것이 "커널 스택" 의 원리다: 리눅스는 태스크마다 커널 스택을 두고 컨텍스트 스위치 때 SP_EL1 을 바꾼다.
- EL0 의 `mrs x0, CurrentEL` → ESR 0x02000000 (Unknown), ELR = 그 명령 → 핸들러가 +4, x0 = 0xDEAD. 결과: `g_el0_el == 0xDEAD`, exit code 1007 (= 7 + 1000), `g_el0_mul == 70`.
- **gdb 함정**: (5) 에서 `$sp = 0x40084510` 인데 `$SP_EL0 = 0x40084530`. QEMU 는 지금 쓰는 SP 를 따로 들고 있다가 EL 이 바뀔 때만 뱅크 레지스터에 복사하는 것으로 보인다 — "지금 sp" 는 `$sp` 를 믿고, `$SP_ELx` 는 "그 EL 을 떠날 때의 값" 으로 읽자 (QEMU 구현 관찰, 확인 권장).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

<!-- code: solution.S diff=starter.S -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"   # 36/36 (EL1 부팅), 34/34 (EL2 부팅), 마지막 qemu exit=0
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh gdb"        # demo_sysregs.gdb → demo_sysregs_out.txt 와 같은 출력
```

`#ifdef BUG_*` 줄은 7. 의 버그 재현 스위치(기본 꺼짐). EL2 부팅이 2 개 적은 이유: HVC 검사 3 개는 EL2 가 있으면 건너뛰고(hvc 가 VBAR_EL2 로 가므로) HCR_EL2 검사 1 개가 붙는다.

gdb 스크립트 (전체 실제 출력은 `demo_sysregs_out.txt`). QEMU 10.2 gdbstub 은 시스템 레지스터를 `$ESR_EL1`, `$ELR_EL1`, `$SPSR_EL1`, `$VBAR_EL1`, `$FAR_EL1`, `$SP_EL0`, `$SP_EL1`, `$SCTLR_EL1` … 이름으로 준다 (`maint print raw-registers` 로 확인). `CurrentEL` 은 목록에 없어서 `($cpsr >> 2) & 3` 으로 본다.

<!-- code: demo_sysregs.gdb -->

## 7. 자주 하는 실수

**1. BRK/UDF/어보트 뒤 ELR 을 안 옮김 → 같은 명령으로 영원히.** `run_qemu.sh bug_noadvance` 실측:

```text
  SVC  vector   = 0x200
exception storm: ELR=0x40081010 ESR=0xf2000007
qemu exit=3
```

ELR = 0x40081010 = `brk_insn` 그 자체. 핸들러가 돌아갈 때마다 같은 brk 가 다시 실행된다. 고친 것: "원인을 고쳤으면 재실행, 건너뛸 거면 +4" 를 EC 마다 정한다 (solution 의 `switch`). 디버거가 BRK 를 쓸 때는 원래 명령을 되돌려 놓고 재실행하는 쪽이다.

**2. SVC 에도 ELR += 4 → 명령 하나가 사라짐.** `run_qemu.sh bug_svcadvance` 실측:

```text
FAIL line 230: r == 5u + 1u + 100u
35/36 passed
qemu exit=1
```

SVC 의 ELR 은 이미 다음 명령(`add x0, x0, #100`)이다. 거기에 +4 를 하면 add 를 건너뛰고 `ret` 으로 간다. 크래시가 아니라 **조용히 틀린 값**이라 더 위험하다. 테스트가 svc 뒤에 일부러 의미 있는 명령을 둔 이유다.

**3. `-mgeneral-regs-only` 없이 빌드 → NEON 이 트랩 → 끝나지 않음.** `run_qemu.sh bug_simd` 실측:

```text
SIMD/FP instructions in image: 6
...
qemu exit=124 (124 = timeout: 끝나지 않았다)
--- qemu -d int (semihosting 제외, 처음 12 줄) ---
Taking exception 1 [Undefined Instruction] on CPU 0
...from EL1 to EL1
...with ESR 0x7/0x1fe00000
...with SPSR 0x600003c5
...with ELR 0x400801c8
...to EL1 PC 0x200 PSTATE 0x3c5
Taking exception 1 [Undefined Instruction] on CPU 0
...from EL1 to EL1
...with ESR 0x0/0x2000000
...with SPSR 0x3c5
...with ELR 0x200
...to EL1 PC 0x200 PSTATE 0x3c5
```

gcc -O2 가 `decode_esr` 의 비트 추출을 `dup v0.2s, w4` / `ushl` 같은 NEON 으로 벡터화했다. `CPACR_EL1.FPEN` 이 리셋 상태(트랩)라 EC **0x07** 예외. 아직 VBAR_EL1 = 0 이라 PC = 0x200 으로 갔고, 거기 플래시는 0 → `udf` → EC 0x00 → 또 0x200 … 무한 루프. 두 가지 교훈: (a) 베어메탈 C 는 FP 를 켜기 전엔 `-mgeneral-regs-only` (리눅스 커널도 같은 옵션으로 빌드한다), FP 를 쓰려면 `CPACR_EL1.FPEN = 0b11` + `isb` 를 먼저. (b) **VBAR 은 C 코드보다 먼저** 설치하라 — 이 레슨은 테스트 편의상 main 에서 설치하지만, 실제 부트 코드는 `_start` 에서 바로 VBAR 을 잡는다.

**4. `HCR_EL2.RW` 를 안 켜고 EL2 → EL1 ERET.** `run_qemu.sh bug_norw` 실측:

```text
Taking exception 1 [Undefined Instruction] on CPU 0
...from EL2 to EL2
...with ESR 0xe/0x3a000000
...with SPSR 0x1003c9
...with ELR 0x40080070
...to EL2 PC 0x200 PSTATE 0x3c9
```

RW = 0 이면 EL1 은 AArch32 여야 하는데 SPSR_EL2.M 은 AArch64 EL1h(M[4] = 0)를 요구 → **illegal exception return**: EL 이 바뀌지 않고 PSTATE.IL(bit 20)만 켜진 채 ELR(0x40080070 = `bl main`)로 간다. 그 명령은 실행되지 못하고 EC 0x0E(Illegal Execution state) 예외가 EL2 에서 난다 (SPSR 0x1003c9 의 0x100000 이 IL 비트). VBAR_EL2 도 0 이라 다시 0x200 루프. 리눅스 `init_kernel_el`/`finalise_el2` 류 코드가 HCR_EL2 를 맨 먼저 쓰는 이유다 (함수 이름은 커널 버전마다 다르다 — 확인 권장).

**5. ERET 대신 `ret`, 또는 SPSR 을 안 써 둠.** `ret` 은 PC 만 바꾸고 EL·DAIF·SPSel 은 그대로다 (starter 의 `el2_to_el1` 이 바로 그 상태: EL2 에 남는다 — 위 starter 출력 두 번째 부팅의 `CurrentEL` 은 스텁이라 0 이지만, 실제로 EL2 다). 반대로 `eret` 전에 SPSR 을 안 쓰면 **예전 예외가 남긴 값**으로 복귀한다 — run_el0 이 `msr spsr_el1, xzr` 를 빼먹으면 EL1h 로 "내려가서" EL0 이 아니게 된다.

**6. VBAR 쓰기 뒤 ISB 생략, 엔트리 0x80 초과.** `msr vbar_el1` 다음 `isb` 가 없으면 바로 뒤 예외가 옛 VBAR 을 쓸 수 있다 (Arm ARM 의 "시스템 레지스터 쓰기는 문맥 동기화 이후에 보장" 규칙; QEMU 에서는 차이가 안 보인다 — ARM-B14 의 ISB 실험과 같은 이유, 이번엔 실행해 보지 않았다). 엔트리 코드가 32 명령을 넘으면 `.p2align 7` 이 다음 칸까지 밀어서 **이후 엔트리 오프셋이 전부 어긋난다** — 어셈블러는 경고하지 않는다. 엔트리는 짧게 두고 `b common` 으로 넘긴다.

**7. FAR 을 항상 믿음.** FAR_EL1 은 어보트·정렬 폴트·워치포인트 때만 유효하고 svc/brk 에선 UNKNOWN. EC 를 먼저 보고, 외부 어보트면 ISS.FnV(bit 10) = 1 일 때 FAR 무효 (이 레슨 실측 FnV = 0).

## 8. 면접 Q&A

<details>
<summary>Q1. EL0–EL3 에는 실제로 무엇이 도나? 차량/폰 SoC 기준으로.</summary>

EL0 = 앱·유저 프로세스, EL1 = 리눅스/RTOS 커널, EL2 = 하이퍼바이저 (KVM, Xen, 차량 SoC 의 파티셔닝 하이퍼바이저; VHE 가 있으면 리눅스 커널 자체가 EL2 에서 돌기도 함), EL3 = 보안 모니터 (TF-A BL31: PSCI 전원 관리, 보안/비보안 세계 전환). 보안 세계에는 S-EL1 에 OP-TEE 같은 TEE OS, S-EL0 에 trusted app. 부팅은 위에서 아래로: ROM → (EL3) BL1/BL2/BL31 → (EL2 또는 EL1) U-Boot/UEFI → 리눅스. 이 레슨의 QEMU 실측도 같은 순서로 내려간다: secure=on → 3 → (2) → 1.

</details>

<details>
<summary>Q2. SVC 와 BRK 와 데이터 어보트의 ELR 은 각각 어디를 가리키나? 왜 다른가?</summary>

SVC/HVC/SMC 는 명령이 "실행을 마친" 뒤 생기는 예외라 ELR = **다음 명령** (실측 svc + 4). BRK, 미정의 명령, 동기 어보트는 명령이 실행되지 못한 것이라 ELR = **그 명령** (실측 brk_insn, udf_insn, load64_insn). 그래야 페이지 폴트 핸들러가 페이지를 채운 뒤 같은 명령을 재실행할 수 있다. IRQ 는 "다음에 실행할 명령" (ARM-A02). Cortex-M 은 스택 프레임의 PC 가 같은 역할을 한다 (ARM-M09).

</details>

<details>
<summary>Q3. 데이터 어보트 덤프(ESR, FAR, ELR)를 받으면 어떤 순서로 읽나?</summary>

① ESR.EC: 0x24 (하위 EL = 유저) / 0x25 (커널 자신). ② ISS.DFSC: 0x04–0x07 translation fault (레벨 0–3), 0x09–0x0B access flag, 0x0D–0x0F permission, 0x10 동기 외부 어보트, 0x21 정렬 폴트. ③ WnR 로 읽기/쓰기. ④ FAR = 접근한 주소 (FnV = 0 일 때) — 작은 값이면 NULL 구조체 멤버. ⑤ ELR = 그 명령 → 디스어셈블해서 베이스 레지스터 확인 (ISV = 1 이면 SRT 가 레지스터 번호를 알려 준다). 이 레슨 실측: `0x96000010` + FAR 0x50000000 = 커널이 아무것도 없는 물리 주소를 읽음, `0x96000021` = Device 메모리에 비정렬 접근. 유저 공간 크래시 분석 절차는 ARM-M14.

</details>

<details>
<summary>Q4. MMU 를 켜기 전의 C 코드에는 어떤 제약이 있나?</summary>

데이터 접근이 Device-nGnRnE 라 (1) 비정렬 접근 → 정렬 폴트 (실측 DFSC 0x21) → `-mstrict-align`, (2) 캐시 없음 → 느림, (3) exclusive/원자 명령이 보장되지 않음 → 스핀락 금지, (4) 투기적 접근 없음. 그 밖의 부트 초기 제약: FP/SIMD 는 `CPACR_EL1.FPEN` 을 켜기 전엔 트랩(실측 EC 0x07) → `-mgeneral-regs-only`, 스택 보호·PIE 같은 툴체인 기본값 끄기, .bss 0 채우기, VBAR 을 가장 먼저.

</details>

<details>
<summary>Q5. 벡터 테이블은 왜 4 묶음이고, SP_EL0 / SP_ELx 묶음은 무슨 의미인가?</summary>

{같은 EL + SP_EL0, 같은 EL + SP_ELx, 하위 EL AArch64, 하위 EL AArch32} × {Sync, IRQ, FIQ, SError}. 하드웨어가 "어디서 왔나" 를 오프셋으로 미리 나눠 주므로 핸들러가 SPSR 을 해석하지 않아도 된다. EL1t(SPSel = 0)는 커널이 SP_EL0 을 빌려 쓰는 모드인데, 리눅스는 이 묶음을 "잘못된 모드" 로 처리한다 (커널은 항상 EL1h 로 돈다 — 확인 권장). 실측: 같은 svc 가 EL1h 에서 0x200, EL1t 에서 0x000, EL0 에서 0x400. 예외에 들어가면 SPSel 은 항상 1 이 되므로 핸들러는 늘 자기 EL 의 SP_ELx 위에서 시작한다.

</details>

<details>
<summary>Q6. 부트로더가 EL2 에서 커널을 부르면 커널은 EL1 로 어떻게 내려가나?</summary>

HCR_EL2.RW = 1 (EL1 을 AArch64 로), SP_EL1 준비, SPSR_EL2 = EL1h + DAIF 마스크 (0x3C5), ELR_EL2 = EL1 진입점, `eret`. 실측 HCR_EL2 0x0 → 0x80000000. 실제 커널은 여기에 더해 CNTHCTL_EL2(EL1 이 타이머 접근 가능하게), VPIDR/VMPIDR_EL2(EL1 이 읽는 MIDR/MPIDR 값), SCTLR_EL1 초기값, 그리고 KVM 을 위해 VBAR_EL2 에 스텁 벡터를 남긴다. RW 를 빼먹으면 illegal exception return (실측 EC 0x0E, PSTATE.IL).

</details>

<details>
<summary>Q7. SVC, HVC, SMC 의 차이와 PSCI 는? QEMU 에서 HVC 가 예외 없이 돌아온 이유는?</summary>

SVC → EL1 (시스템 콜), HVC → EL2 (하이퍼콜), SMC → EL3 (보안 모니터 콜). PSCI(CPU on/off, 시스템 리셋)는 SMCCC 규약으로 x0 = 함수 ID 를 넣고 hvc 또는 smc 를 실행한다. QEMU `-kernel` 직접 부팅은 펌웨어가 없으므로 QEMU 자신이 PSCI 를 처리한다: 실측으로 plain virt 는 **hvc** (0x84000000 → 0x10001 = PSCI 1.1), virtualization=on 이면 **smc**, secure=on 이면 없음 (hvc 가 진짜 EC 0x00 예외). 그래서 "EL2 없으면 HVC 는 UNDEFINED" 라는 아키텍처 규칙을 QEMU virt 기본 설정에서는 관찰할 수 없었다.

</details>

<details>
<summary>Q8. Cortex-M 의 예외 처리와 무엇이 다른가?</summary>

Cortex-M: 하드웨어가 r0–r3, r12, lr, pc, xPSR 8 워드를 스택에 쌓고, lr 에 EXC_RETURN 을 넣고, 핸들러는 평범한 C 함수, `bx lr` 이 복귀 (ARM-B14, ARM-M09). 벡터 테이블은 함수 주소 배열. AArch64: 하드웨어는 ELR/SPSR/ESR/FAR 만 쓰고 범용 레지스터는 하나도 안 쌓는다, 벡터는 **코드** (엔트리당 32 명령), 저장·복원은 소프트웨어, 복귀는 전용 `eret`. 대신 AArch64 는 예외 원인을 ESR 한 레지스터에 정리해 준다 (Cortex-M 은 CFSR/HFSR/MMFAR/BFAR 로 나뉨 — ARM-M12).

</details>

<details>
<summary>Q9. ERET 은 `br x30` 과 무엇이 다른가?</summary>

ERET 은 PC ← ELR_ELx 와 PSTATE ← SPSR_ELx (EL, SPSel, DAIF, NZCV, 실행 상태) 를 **한 명령으로 원자적으로** 바꾼다. 그래서 "EL 을 낮추면서 동시에 점프" 가 가능하다 — br 로는 EL 을 바꿀 방법이 없고, 먼저 EL 을 바꾸고 점프하면 그 사이에 낮은 EL 이 높은 EL 코드를 실행하는 구간이 생긴다. ERET 은 또 문맥 동기화 이벤트라 그 전에 바꾼 시스템 레지스터가 새 EL 에서 보인다 (Arm ARM 기준, 확인 권장). SPSR 이 불가능한 상태를 요구하면 illegal return 이 된다 (7. 실수 4).

</details>

## 9. 추가 연습

### 연습 1 — EL1t: 같은 EL, 다른 SP

`svc_on_sp0(sp0_top)` 를 asm 으로 써라: `SP_EL0 = sp0_top` → `msr spsel, #0` → `svc #0x50` → `msr spsel, #1`. 핸들러가 기록한 엔트리, SPSR.M, SP_EL0 를 확인하라.

<details>
<summary>답</summary>

실측: 엔트리 0x000 (kind 0), SPSR = 0x600003c4 (M = 0100 = EL1t), ESR 0x56000050, ELR = svc + 4, 기록된 SP_EL0 = 준 값 그대로 (예외 진입 때 SPSel 이 1 이 되어 프레임은 SP_EL1 에 쌓였으므로). ERET 은 SPSR.M = EL1t 로 돌려놓으므로 함수가 `msr spsel, #1` 로 원래대로 돌아가야 한다. `msr spsel` 과 svc 사이에서 스택을 쓰면 안 된다 (sp 가 바뀐 상태).

</details>

### 연습 2 — ID 레지스터로 "내가 어떤 칩의 어떤 EL 구성 위에 있나"

`MIDR_EL1`, `MPIDR_EL1`, `ID_AA64PFR0_EL1` 을 읽어 implementer/part, 코어 번호, EL0–EL3 지원 필드를 출력하라.

<details>
<summary>답</summary>

실측: MIDR = 0x410fd034 → implementer 0x41 (Arm), variant 0, architecture 0xF, part 0xD03 (Cortex-A53), revision 4 → "r0p4". MPIDR = 0x80000000 → bit31 RES1, U(bit30) = 0 (멀티프로세서 시스템), Aff0 = 0. ID_AA64PFR0_EL1 하위 16 비트 (EL3 EL2 EL1 EL0 니블): virt `0x0022`, virtualization=on `0x0222`, secure=on `0x2022`, 둘 다 `0x2222` — 2 = AArch64 + AArch32 지원, 0 = 그 EL 없음. QEMU 는 꺼진 EL 의 필드를 0 으로 보여 준다. 주의: EL2 가 있으면 EL1 의 MIDR/MPIDR 읽기는 VPIDR_EL2/VMPIDR_EL2 값을 받는다 (QEMU 는 둘 다 실제 값으로 초기화한 것으로 보인다 — 4 부팅 모두 같은 MIDR, 확인 권장).

</details>

### 연습 3 — SPSR 해석기

`decode_spsr(spsr, &info)` (C): M[4] (AArch32 출신?), M[3:2] = EL, M[0] = h/t, DAIF [9:6], NZCV [31:28]. 본문 실측값 0x600003c5, 0x0, 연습 1 의 값으로 테스트하라.

<details>
<summary>답</summary>

0x600003c5 → EL1h, DAIF = 1111, NZCV = 0110 (Z, C — svc 직전 비교 결과), AArch64. 0x0 → EL0t, DAIF 0. 0x600003c4 → EL1t. 주의: SPSR.M 인코딩은 0b0000 EL0t, 0b0100 EL1t, 0b0101 EL1h, 0b1000 EL2t, 0b1001 EL2h, 0b1100/1101 EL3t/h — "EL × 4 + h" 다. AArch32 에서 온 예외면 M[4] = 1 이고 M[3:0] 은 USR/SVC/IRQ 같은 AArch32 모드 번호다.

</details>

### 연습 4 — EL3 에서 부팅해서 내려오기 + HVC/SMC 결과 표

`-M virt,secure=on` 은 EL3 에서 시작한다. `shared_start.S` 의 weak `el3_to_lower` 를 덮어써서 SCR_EL3 = RW | NS (0x401) → EL2 가 있으면 EL2h, 없으면 EL1h 로 ERET 하라 (EL2 면 `_start` 가 이어서 `el2_to_el1` 을 부른다). 네 가지 부팅에서 EL1 의 `hvc`/`smc` 결과를 표로 만들어라 (VBAR_EL2/EL3 가 없으므로 그쪽으로 가는 조합은 실행하지 않는다).

<details>
<summary>답</summary>

| 부팅 | ID_AA64PFR0 [15:0] | hvc #0 (x0 = PSCI_VERSION) | smc #0 |
|---|---|---|---|
| `virt` | 0x0022 | QEMU PSCI → 0x10001, 예외 없음 | EC 0x00 예외 (x0 → 0xDEAD) |
| `virt,virtualization=on` | 0x0222 | EL2 로 감 (실행 안 함) | QEMU PSCI → 0x10001, 예외 없음 |
| `virt,secure=on` | 0x2022 | EC 0x00 예외 (EL2 없음, PSCI 없음) | EL3 로 감 (실행 안 함) |
| `virt,secure=on,virtualization=on` | 0x2222 | EC 0x00 예외 (SCR_EL3.HCE = 0 → HVC 미정의) | EL3 로 감 (실행 안 함) |

전부 실측. EL3 체인에서 SCR_EL3.NS = 1 이므로 EL1 은 비보안 세계다 — RAM 0x40000000 은 비보안에서 접근 가능해서 그대로 돈다. HCE 를 켜면 마지막 줄의 hvc 는 EL2 로 갈 것이다 (실행 안 함).

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # 부팅 4 가지, 각각 마지막 줄 qemu exit=0
```

```text
  -- (solution tests above, practice below) --
  [1] EL1t svc vector = 0x0
  [1] EL1t svc SPSR   = 0x600003c4
  [2] MIDR_EL1         = 0x410fd034
  [2] MPIDR_EL1        = 0x80000000
  [2] ID_AA64PFR0_EL1  = 0x22
  [4] boot EL = 0x1
  [4] EL2 field = 0x0
  [4] EL3 field = 0x0
  [4] smc PSCI_VERSION -> x0 = 0xdead
  [4] smc exceptions to EL1  = 0x1
48/48 passed
qemu exit=0
...
  [4] SCR_EL3 written = 0x401
  [4] hvc -> x0 = 0xdead
  [4] hvc ESR   = 0x2000000
46/46 passed
```

(첫 번째 부팅과 세 번째(secure=on) 부팅 끝부분 발췌. 네 부팅 결과: 48/48, 46/46, 46/46, 46/46, 모두 qemu exit=0.)

## 10. 핵심 정리

- EL0 앱 / EL1 커널 / EL2 하이퍼바이저 / EL3 보안 모니터. 올라가는 길 = 예외(svc/hvc/smc/abort/IRQ), 내려가는 길 = `eret` 하나. `CurrentEL[3:2]` = 지금 EL.
- 예외 진입 때 하드웨어는 ELR(복귀 주소)·SPSR(직전 PSTATE)·ESR(원인)·FAR(주소)만 쓰고 DAIF 를 마스크, SPSel = 1, PC = VBAR + 오프셋. 범용 레지스터 저장은 벡터 코드 몫.
- VBAR_ELx: 2 KB 정렬, 16 엔트리 × 0x80. 0x000 EL·SP0 / 0x200 EL·SPx / 0x400 하위 AArch64 / 0x600 하위 AArch32, 그 안에 Sync/IRQ/FIQ/SError. 실측: EL1 svc 0x200, EL0 svc 0x400.
- ESR = EC[31:26] | IL[25] | ISS[24:0]. SVC 0x15 (imm16 in ISS), BRK 0x3C, Unknown 0x00, DABT 0x24/0x25 (DFSC, WnR, ISV), FP 트랩 0x07, illegal state 0x0E.
- ELR: SVC/HVC/SMC → 다음 명령, BRK·UDF·어보트 → 그 명령 (건너뛰려면 +4). 틀리면 무한 예외 또는 조용한 명령 누락.
- EL2 → EL1: HCR_EL2.RW = 1, SP_EL1, SPSR_EL2 = 0x3C5, ELR_EL2, eret. RW 를 빼면 illegal return (EC 0x0E, PSTATE.IL).
- MMU off = 데이터는 Device 메모리 → 비정렬 접근은 정렬 폴트(DFSC 0x21). 부트 C 는 `-mstrict-align -mgeneral-regs-only`, VBAR 은 가장 먼저.
- QEMU `-kernel` 은 펌웨어 대신 PSCI 를 흉내 낸다: virt = hvc, virtualization=on = smc, secure=on = 없음.

## 참고자료

- Arm Architecture Reference Manual for A-profile (DDI 0487) — D1 "The AArch64 Exception Model" (예외 진입·복귀, 벡터 오프셋, preferred return address), ESR_EL1/ELR_EL1/SPSR_EL1/FAR_EL1/VBAR_EL1/CurrentEL/HCR_EL2/SCR_EL3/SCTLR_EL1 레지스터 설명, ERET 의사코드: <https://developer.arm.com/documentation/ddi0487/latest/>
- Arm "Learn the architecture — AArch64 Exception Model" (EL 역할, 벡터 테이블, 예외 처리 흐름 입문): <https://developer.arm.com/documentation/102412/latest/>
- Arm ESR_EL1 EC 값 표 (온라인 레지스터 문서): <https://developer.arm.com/documentation/ddi0601/latest/AArch64-Registers/ESR-EL1--Exception-Syndrome-Register--EL1->
- Arm Cortex-A53 Technical Reference Manual (MIDR r0p4, 리셋 동작): <https://developer.arm.com/documentation/ddi0500/latest/>
- Arm Semihosting specification (AArch64 `HLT #0xF000`, SYS_WRITE0 0x04, SYS_EXIT 0x18 + ADP_Stopped_ApplicationExit 0x20026): <https://github.com/ARM-software/abi-aa/blob/main/semihosting/semihosting.rst>
- Arm PSCI (DEN 0022) 와 SMCCC (DEN 0028) — PSCI_VERSION 0x84000000, conduit: <https://developer.arm.com/documentation/den0022/latest/>
- QEMU 'virt' 보드 문서 (virtualization=on / secure=on, 메모리 맵, -kernel 부팅): <https://www.qemu.org/docs/master/system/arm/virt.html>
- Linux `arch/arm64/kernel/head.S`, `entry.S` (EL2 → EL1 진입, 벡터 테이블 `vectors`, `kernel_ventry` 매크로): <https://github.com/torvalds/linux/tree/master/arch/arm64/kernel>
- Trusted Firmware-A (BL31, EL3 런타임, PSCI 구현): <https://trustedfirmware-a.readthedocs.io/>
- 관련 레슨: ARM-B14 (Cortex-M 의 MSP/PSP·EXC_RETURN), ARM-M05 (ISB), ARM-M04 (LDXR/STXR — Device 메모리 주의), ARM-M09 (Cortex-M 예외 진입/복귀), ARM-M12 (Cortex-M 폴트 분석), ARM-M14 (크래시 덤프 읽기), ARM-A02 (AArch64 IRQ·GIC·전체 컨텍스트 저장), TOOL-A03 (gdb remote)
