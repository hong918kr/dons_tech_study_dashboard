---
id: ARM-A02
time: 120분
updated: 2026-11-15
---
# ARM-A02 AArch64 예외 벡터와 IRQ 컨텍스트 저장

## 학습 목표

- EL1 벡터 테이블의 각 엔트리 안에 `SAVE_ALL` 을 넣고 x0–x30 · SP_EL0 · ELR_EL1 · SPSR_EL1 · ESR_EL1 · FAR_EL1 을 288 바이트 프레임에 저장한다. 같은 배치의 `struct trap_frame` 을 C 에 두고 `_Static_assert(offsetof…)` 로 묶는다. 프레임 크기가 왜 16 의 배수여야 하는지도 설명할 수 있다.
- QEMU virt 의 GICv2(분배기 0x08000000, CPU 인터페이스 0x08010000 — DTB 에서 확인)를 직접 초기화한다. 제네릭 타이머(EL1 가상 타이머, PPI 11 = INTID 27)를 CNTFRQ_EL0 로 1 kHz 에 맞추고, `wfi` 로 틱을 세며, IAR → 처리 → EOIR 과 spurious 1023 을 처리한다.
- x0–x30 과 NZCV 에 센티널(sentinel, 감시값)을 넣은 채 IRQ 를 맞고 검사해서 "레지스터가 하나도 안 바뀌었다" 를 보인다. svc 왕복으로는 "프레임의 모든 칸이 제자리다" 를 보인다.
- 면접 단골 버그 다섯 가지를 실제로 재현하고 증상을 숫자로 설명한다: EOI 빠뜨리기(딱 1 번 받고 침묵), x30 저장 빠뜨리기, ELR/SPSR 복원 빠뜨리기 + 중첩, 레벨 트리거 타이머 재무장 빠뜨리기(폭주), 정렬 안 된 프레임.
- 우선순위가 높은 SGI 가 타이머 핸들러 안으로 들어오는 중첩 IRQ 를 올바른 순서(ELR/SPSR 를 프레임에 저장 → `daifclr`)로 만든다. 이때 ELR_EL1 이 실제로 덮인다는 것을 측정한다.

## 1. 문제

"AArch64 IRQ 진입 스텁에서 무엇을 저장해야 해?", "GIC 에 EOI 안 하면 어떻게 돼?", "핸들러 안에서 인터럽트를 다시 켜려면 뭘 먼저 해야 해?" — 리눅스·RTOS 포팅, 하이퍼바이저, Cortex-A/R 기반 SoC 펌웨어(차량용 SoC, Jetson, i.MX8, 스토리지 컨트롤러의 A 코어) 면접에서 반드시 나오는 질문이다. Cortex-M(ARM-M09)은 하드웨어가 r0–r3, r12, LR, PC, xPSR 을 스택에 쌓아 준다. AArch64 는 **아무것도 쌓아 주지 않는다.** 하드웨어는 ELR/SPSR/ESR 를 쓰고 PC 를 벡터로 바꿀 뿐이다. 나머지는 전부 벡터 코드가 직접 해야 한다.

ARM-M13 에서는 동기 예외만 다뤘다. 그래서 C 를 부르는 데 필요한 레지스터만 저장했고(x0–x18, x29, x30), 인터럽트는 켜지 않았다. 이번에는 **비동기 IRQ** 다. IRQ 는 아무 명령 사이에나 떨어지므로 끼어든 코드의 상태를 하나도 빠짐없이 지켜야 한다. 이번 프레임은 그대로 컨텍스트 스위치에도 쓸 수 있는 "전체 프레임" 이다.

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">타이머 IRQ 한 번의 경로 (QEMU virt, GICv2 — 숫자는 실측)</text>
  <rect x="20" y="40" width="150" height="66" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="30" y="60" fill="currentColor" font-weight="bold">제네릭 타이머</text>
  <text x="30" y="78" fill="currentColor" font-size="12" font-family="monospace">CNTV_CVAL 지남</text>
  <text x="30" y="96" fill="currentColor" font-size="12" font-family="monospace">ISTATUS=1 (레벨)</text>
  <rect x="220" y="40" width="200" height="66" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="230" y="60" fill="currentColor" font-weight="bold">GICD 0x08000000</text>
  <text x="230" y="78" fill="currentColor" font-size="12" font-family="monospace">INTID 27 pending</text>
  <text x="230" y="96" fill="currentColor" font-size="12" font-family="monospace">enable · prio 0xa0</text>
  <rect x="470" y="40" width="250" height="66" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="480" y="60" fill="currentColor" font-weight="bold">GICC 0x08010000</text>
  <text x="480" y="78" fill="currentColor" font-size="12" font-family="monospace">0xa0 &lt; PMR 0xf0, &lt; RPR 0xff</text>
  <text x="480" y="96" fill="currentColor" font-size="12" font-family="monospace">→ nIRQ 를 코어로</text>
  <path d="M170 73 L218 73" stroke="currentColor" stroke-width="2"/>
  <path d="M210 68 L218 73 L210 78" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M420 73 L468 73" stroke="currentColor" stroke-width="2"/>
  <path d="M460 68 L468 73 L460 78" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="20" y="140" width="700" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="30" y="162" fill="currentColor" font-weight="bold">코어 (하드웨어가 하는 일 전부)</text>
  <text x="30" y="184" fill="currentColor" font-size="12" font-family="monospace">ELR_EL1 ← 다음 PC · SPSR_EL1 ← PSTATE · DAIF ← 1111 · PC ← VBAR_EL1 + 0x280 (ESR 는 안 바뀜)</text>
  <path d="M595 106 L595 138" stroke="currentColor" stroke-width="2"/>
  <path d="M590 130 L595 138 L600 130" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="20" y="226" width="220" height="80" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="30" y="246" fill="currentColor" font-weight="bold">벡터 0x280 (asm)</text>
  <text x="30" y="266" fill="currentColor" font-size="12" font-family="monospace">SAVE_ALL: 288 B 프레임</text>
  <text x="30" y="284" fill="currentColor" font-size="12" font-family="monospace">x0=frame x1=5</text>
  <text x="30" y="300" fill="currentColor" font-size="12" font-family="monospace">bl exc_c</text>
  <rect x="270" y="226" width="220" height="80" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="280" y="246" fill="currentColor" font-weight="bold">irq_c (C)</text>
  <text x="280" y="266" fill="currentColor" font-size="12" font-family="monospace">iar = GICC_IAR (= 27)</text>
  <text x="280" y="284" fill="currentColor" font-size="12" font-family="monospace">틱++ · TVAL 재무장</text>
  <text x="280" y="300" fill="currentColor" font-size="12" font-family="monospace">GICC_EOIR = iar</text>
  <rect x="520" y="226" width="200" height="80" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="530" y="246" fill="currentColor" font-weight="bold">ret_from_exc (asm)</text>
  <text x="530" y="266" fill="currentColor" font-size="12" font-family="monospace">RESTORE_ALL</text>
  <text x="530" y="284" fill="currentColor" font-size="12" font-family="monospace">msr elr/spsr ← 프레임</text>
  <text x="530" y="300" fill="currentColor" font-size="12" font-family="monospace">eret</text>
  <path d="M130 196 L130 224" stroke="currentColor" stroke-width="2"/>
  <path d="M125 216 L130 224 L135 216" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M240 266 L268 266" stroke="currentColor" stroke-width="2"/>
  <path d="M260 261 L268 266 L260 271" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M490 266 L518 266" stroke="currentColor" stroke-width="2"/>
  <path d="M510 261 L518 266 L510 271" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="20" y="336" fill="currentColor" font-size="12">IAR 을 읽는 순간 27 은 pending → active, 실행 우선순위(RPR) 0xff → 0xa0. EOIR 을 써야 다시 0xff.</text>
  <text x="20" y="356" fill="currentColor" font-size="12">TVAL 을 다시 쓰지 않으면 ISTATUS 가 1 로 남아 선이 계속 높다 → EOI 직후 같은 IRQ 가 또 온다 (Part B 폭주).</text>
  <text x="20" y="374" fill="currentColor" font-size="12">ESR_EL1 은 IRQ 때 갱신되지 않는다: gdb 실측에서 직전 svc 의 0x56000077 이 그대로 남아 있었다.</text>
</svg>
```

### Part A — 전체 프레임 저장 + GIC + 타이머 틱

- `SAVE_ALL` / `RESTORE_ALL` (asm 매크로): 288 B 프레임 = x0..x30 (0..240), SP_EL0 (248), ELR (256), SPSR (264), ESR (272), FAR (280). 엔트리 하나 안에 다 들어가야 한다(29 명령 ≤ 32).
- `gicd_reg_addr(off, intid, bits)` / `gicd_reg_shift(intid, bits)`: ISENABLER(1 비트), ICFGR(2), IPRIORITYR·ITARGETSR(8) 의 워드 주소와 비트 위치. INTID 0..1019 × 3 종류를 전수 검사한다.
- `decode_iar(iar, &cpuid)`: [9:0] INTID, [12:10] CPUID. `ticks_per_period(cntfrq, hz)`: 반올림, TVAL 범위(부호 있는 32 비트) 검사.
- `gic_init()`, `gic_enable_irq(intid, prio)`, `vtimer_rearm()`, `irq_c(frame)`.
- 테스트: svc 왕복(31 개 레지스터와 NZCV 를 C 가 프레임에서 확인하고 바꿔 쓴다) → 1 kHz 틱 100 개(`wfi`) → 센티널을 든 채 IRQ 맞기 200 번 → 불일치 0.

### Part B — 버그를 실제로 넣어 보기 (런타임 모드 + `run_qemu.sh bug_*`)

- **EOI 안 함**: 30 주기 동안 IRQ 가 딱 1 번만 오는지 확인한다. RPR = 0xa0, ISACTIVER0 bit27 = 1 인 것도 본다. 늦게라도 EOI 하면 다시 흐른다.
- **x30 저장 안 함**: 검사기 마스크가 정확히 bit 30 만 잡는지 본다(시뮬레이션). 진짜 빌드(`bug_nolr`)에서는 명령 어보트가 난다.
- **재무장 안 함(레벨 트리거)**: EOI 직후 같은 IRQ 가 또 들어와 2000 번까지 세어진다. ISTATUS 는 매번 1 이고, IMASK 로만 멈출 수 있다.
- **spurious**: 장치가 IAR 을 읽기 전에 선을 내리면 IAR = 1023 이 나온다. 틱으로 세지 않고 EOI 도 하지 않아야 한다.
- **ELR/SPSR 복원 안 함**(`bug_noelr`), **280 B 프레임**(`bug_align`) — 빌드 스위치로 재현한다.

### Part C — 중첩 IRQ 를 올바르게

- 타이머 핸들러(우선순위 0xa0) 안에서 자기 자신에게 SGI 1(0x80)을 보내고 `daifclr #2`. 그러면 SGI 가 핸들러 안으로 들어와 프레임이 두 겹이 된다(depth 2).
- 중첩이 끝난 뒤 live ELR_EL1 이 프레임의 ELR 과 **다르다**는 것을 100 번 모두 측정한다. 이것이 "IRQ 를 다시 켜기 전에 ELR/SPSR 를 저장하라" 는 규칙의 실측 근거다.
- 그 상태에서도 센티널 검사기 불일치는 0 이어야 한다.

| 파일 | 역할 |
|---|---|
| `starter.c` / `solution.c` | GIC 드라이버·디코더·`irq_c`·`vtimer_rearm` (TODO / 정답) + 주어진 디스패처(`exc_c`, `on_irq`, `sync_c`) + 펌웨어 테스트(main) |
| `starter.S` / `solution.S` | `SAVE_ALL`/`RESTORE_ALL` (TODO / 정답) + 16 엔트리 벡터 테이블 + 레지스터 검사기 `regs_hold`/`regs_svc` + 시스템 레지스터 도우미 |
| `shared_start.S`, `shared_rt.c`, `arm_a02.ld` | 부트·세미호스팅·CHECK (ARM-M13 축약판) |
| `run_qemu.sh` | VM 안 빌드·실행: `starter|solution|practice|gdb|objdump|bug_nolr|bug_noelr|bug_align` |
| `dtb_excerpt.txt` | `-M virt,gic-version=2,dumpdtb=` + dtc 로 뽑은 GIC·타이머 노드 (실제 출력) |
| `demo_irq.gdb` / `demo_irq_out.txt` | 첫 IRQ 에서 본 프레임·시스템 레지스터 (실제 출력) |
| `practice.c` / `practice.S` | 추가 연습 답 (본문을 `#include` 해서 이어 붙인다) |

## 2. Problem (English)

> **AArch64 IRQ entry with a full register frame.** Target: QEMU `-M virt,gic-version=2 -cpu cortex-a53`, EL1, MMU off, semihosting output.
>
> 1. Write `SAVE_ALL`/`RESTORE_ALL` assembler macros. Every one of the 16 EL1 vector entries (0x80 bytes each, table 2 KB-aligned) must save x0–x30, SP_EL0, ELR_EL1, SPSR_EL1, ESR_EL1 and FAR_EL1 into a frame on SP_EL1, call `void exc_c(struct trap_frame *f, uint64_t entry)`, and return with `ERET` after restoring **from the frame** (C may modify any field). Keep SP 16-byte aligned. Mirror the layout in C and pin it with `_Static_assert`.
> 2. GICv2 helpers: `uint64_t gicd_reg_addr(uint32_t off, uint32_t intid, uint32_t bits)`, `uint32_t gicd_reg_shift(uint32_t intid, uint32_t bits)`, `uint32_t decode_iar(uint32_t iar, uint32_t *cpuid)`; `gic_init()`, `gic_enable_irq(intid, prio)`.
> 3. Program the EL1 virtual timer for 1 kHz from `CNTFRQ_EL0`. In `irq_c`: acknowledge via `GICC_IAR`, ignore spurious IDs (≥ 1020) without EOI, re-arm the level-triggered timer, write the same IAR value to `GICC_EOIR`. Count 100 ticks sleeping in `wfi`.
> 4. Prove context integrity: hold sentinels in x0–x30 and NZCV across IRQs; zero mismatches.
> 5. Show what breaks: missing EOI, missing x30 save, missing re-arm, spurious IRQ, missing ELR/SPSR restore with nesting. Then allow a higher-priority SGI to nest inside the timer handler correctly.
>
> Example (real run): `CNTFRQ_EL0 = 0x3b9aca0` (62.5 MHz) → period 62500; `VBAR_EL1 = 0x40081800`; idle `GICC_IAR = 0x3ff`; without EOI: 1 IRQ in 30 periods, `GICC_RPR = 0xa0`; nested: `RPR` 0xa0 in the timer handler, 0x80 in the SGI handler.
>
> Follow-ups: Why must the frame be a multiple of 16? Why save x19–x28 if the C handler preserves them anyway? What does reading IAR change in the GIC? What happens if you `msr daifclr, #2` before saving ELR/SPSR? Why is a level-triggered timer an IRQ storm waiting to happen? Why don't kernels save v0–v31 on every IRQ?

## 3. 시작 코드

C 쪽 (TODO: `gicd_reg_addr/shift`, `decode_iar`, `ticks_per_period`, `gic_init`, `gic_enable_irq`, `vtimer_rearm`, `irq_c`):

<!-- code: starter.c -->

어셈블리 쪽 (TODO: `SAVE_ALL` 의 x0..x30 저장, `RESTORE_ALL` 의 x0..x30 복원):

<!-- code: starter.S -->

주어진 부트·런타임·링커 스크립트 (ARM-M13 축약판, TODO 없음):

<!-- code: shared_start.S -->

<!-- code: shared_rt.c -->

<!-- code: arm_a02.ld -->

빌드와 실행은 Lima VM `elinux` (aarch64 Ubuntu, gcc 15.2.0, QEMU 10.2.1, GNU gdb 17.1) 안에서 한다. VM 의 네이티브 gcc 가 곧 AArch64 컴파일러다. 플래그는 ARM-M13 과 같다. 특히 `-mgeneral-regs-only` 덕분에 C 핸들러가 FP/SIMD 레지스터를 쓰지 않는다 — 그래서 프레임에 v0–v31 이 없어도 된다(Q8). 맥에서 한 줄:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ARM/ARM-A02
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"   # solution / practice / gdb / objdump / bug_nolr / bug_noelr / bug_align
```

<!-- code: run_qemu.sh -->

처음 실행 결과 (실제 출력, 앞부분 생략 없음):

```text
=== qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a53 -kernel starter.elf  ===
ARM-A02 AArch64 vectors + IRQ context save (QEMU virt, GICv2, cortex-a53)
  CurrentEL     = 0x1
  CNTFRQ_EL0    = 0x3b9aca0
  SCTLR_EL1     = 0xc50838
  GICD_TYPER    = 0x8
  GICD_IIDR     = 0x43b
  GICC_IIDR     = 0x2043b
PASS g_boot_el == 1u && read_current_el() == 1u
FAIL line 307: bad == 0u
  …(gicd_reg_addr/shift, decode_iar, ticks_per_period 의 FAIL 8 줄)…
  VBAR_EL1      = 0x40081800
PASS read_vbar_el1() == ADDR(vectors_el1)
  SVC frame mismatch (SAVE)    = 0x7fffffff
  SVC regs mismatch (RESTORE)  = 0x7fffffff
FAIL line 341: g_svc_seen_mask == 0u
FAIL line 342: diff_mask(out, want) == 0u
  IPRIORITYR[27]= 0x0
  ISENABLER0    = 0xffff
  GICC_BPR      = 0x0
FAIL line 354: pri == PRIO_TIMER
  …
  IAR (idle)    = 0x3ff
FAIL line 360: decode_iar(idle_iar, &cpu) == INTID_SPURIOUS
  period (ticks)= 0x0
FAIL line 367: ready
  (no timer IRQ: GIC/timer/vector TODO not done -> IRQ tests skipped)
6/22 passed
qemu exit=1
```

**안전장치.** starter 의 `SAVE_ALL` 은 x0..x30 을 저장하지 않지만, svc 왕복은 그래도 끝까지 안전하게 돈다. 시스템 레지스터 부분(주어짐)이 ELR/SPSR/ESR 을 저장하고, `regs_svc` 는 자기 x19–x30 을 자기 스택에서 되살리기 때문이다. 그래서 불일치 마스크 0x7fffffff(31 개 전부)만 남는다. 첫 틱이 200 ms(카운터 기준) 안에 오지 않으면 IRQ 테스트를 통째로 건너뛰므로, 틱을 기다리는 `wfi` 에서 영원히 멈추지 않는다. 그 밖에 예외가 50000 번을 넘으면 exit 3, 예상 밖 벡터나 sync 예외면 exit 4 로 끝난다.

VM 없이 맥에서 "어셈블은 되나?" 만 확인 (실행):

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
clang --target=aarch64-none-elf -c solution.S -o /tmp/ARM-A02_solution.o && /usr/bin/objdump -d /tmp/ARM-A02_solution.o | sed -n '/<vectors_el1>:/,+3p'
```

```text
0000000000000000 <vectors_el1>:
       0: d10483ff     	sub	sp, sp, #0x120
       4: a90007e0     	stp	x0, x1, [sp]
       8: a9010fe2     	stp	x2, x3, [sp, #0x10]
```

## 4. 힌트

<details>
<summary>힌트 1 — 프레임 오프셋은 C 가 정한다</summary>

`struct trap_frame { uint64_t x[31]; uint64_t sp_el0, elr, spsr, esr, far; }` → x[k] 는 8k. 짝(x0,x1), (x2,x3) … (x28,x29) 를 `stp` 하면 오프셋은 16·0 … 16·14 = 224, x30 은 240. `_Static_assert` 가 C 쪽을 고정하고 `.S` 의 `#define TF_*` 가 같은 숫자를 쓴다.

</details>

<details>
<summary>힌트 2 — 스크래치 레지스터는 저장한 뒤에만</summary>

ELR/SPSR 을 `mrs` 하려면 범용 레지스터가 하나 필요하다. x0..x4 를 먼저 저장해 두면 그 다음부터는 마음대로 써도 된다. 복원은 거꾸로 한다. 프레임에서 ELR/SPSR 을 꺼내 `msr` 하는 데 x0..x2 를 쓰고, 그 셋은 **맨 마지막에** 프레임에서 다시 읽는다.

</details>

<details>
<summary>힌트 3 — GIC 레지스터 하나에 INTID 몇 개?</summary>

32 비트 워드에 `32 / bits` 개. 워드 주소 = base + off + 4·(intid / (32/bits)), 비트 위치 = bits·(intid % (32/bits)). ISENABLER 는 **쓰기 1 = 켜기(W1S)** 라서 읽고-고치고-쓰기(RMW)가 필요 없다(오히려 위험하다). IPRIORITYR 는 바이트 필드라 RMW 를 한다. BM-M01 의 NVIC ISER/IPR 계산과 똑같은 모양이다.

</details>

<details>
<summary>힌트 4 — IAR 과 EOIR 은 짝</summary>

`iar = GICC_IAR` 을 읽는 것 자체가 acknowledge 다(pending → active, 실행 우선순위 상승). INTID ≥ 1020 이면 처리할 것도, EOI 할 것도 없다. 처리가 끝나면 **읽은 값 그대로**(SGI 면 CPUID 비트 포함) `GICC_EOIR` 에 쓴다.

</details>

<details>
<summary>힌트 5 — 타이머는 레벨 트리거</summary>

`CNTV_CTL_EL0.ISTATUS` 는 "CVAL ≤ 지금" 인 동안 계속 1 이다. 그 선이 GIC 에 들어간다. `TVAL ← period` 를 쓰면 CVAL = 지금 + period 가 되어 조건이 거짓이 되고 선이 내려간다. 재무장은 EOI **전에** 해야 한다.

</details>

## 5. 풀이

### 0. 규칙 정리 — 어디에 쓰여 있나

| 규칙 | 근거 |
|---|---|
| EL1 로 가는 예외 진입: ELR_EL1 ← 복귀 주소, SPSR_EL1 ← PSTATE, PSTATE.DAIF ← 1111, SPSel ← 1, PC ← VBAR_EL1 + 오프셋. 범용 레지스터는 저장 안 함 | Arm ARM (DDI 0487) D1 "Exception entry" (문서 확인 권장). 실측: gdb 에서 cpsr = 0x3c5 |
| IRQ 의 복귀 주소 = 아직 실행 안 한 다음 명령. ESR_EL1 은 IRQ 때 갱신되지 않음 | Arm ARM (확인 권장). 실측: ELR = `read_cntvct+8`(`ret`), ESR 는 직전 svc 값 0x56000077 그대로 |
| 벡터 0x280 = 현재 EL, SP_ELx, IRQ | Arm ARM D1 "Exception vectors" / ARM-M13 실측 표 |
| SP 를 베이스로 쓰는 load/store 는 SCTLR_EL1.SA = 1 이면 SP 가 16 정렬이어야 함 (아니면 EC 0x26) | Arm ARM (확인 권장). SCTLR_EL1 = 0xc50838 → bit3 SA = 1. **QEMU 는 이 검사를 안 한다** (7. 실수 4) |
| GICv2 virt 주소: GICD 0x08000000, GICC 0x08010000 | `dtb_excerpt.txt` (QEMU dumpdtb + dtc 실측) |
| 타이머 PPI: `<1 13 0x104>, <1 14 0x104>, <1 11 0x104>, <1 10 0x104>` = 보안 물리 / 비보안 물리 / 가상 / 하이퍼바이저, INTID = 16 + PPI 번호 → 29, 30, **27**, 26. 0x104 = CPU 마스크 1, 레벨-high | DT 바인딩 `arm,armv8-timer` + `arm,gic` 3-cell 형식 (확인 권장). 실측: 가상 타이머는 27, 물리 타이머는 30 (연습 3) |
| GICC_IAR [9:0] INTID, [12:10] CPUID. 1023 = spurious. EOIR 에는 IAR 값 그대로 | GICv2 Architecture Spec (IHI 0048B) 4.4.4/4.4.5 (확인 권장) |
| GICC_BPR = n → 그룹(선점) 우선순위 = bit[7:n+1] | GICv2 spec 3.3 (확인 권장). 실측: 2 를 쓰면 2 가 읽힌다 |

### Part A — 프레임, GIC, 타이머

**1) 프레임 배치와 크기.**

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">struct trap_frame — 288 B = 36 워드 = 16 × 18 (gdb 실측: frame = sp = 0x40093340)</text>
  <rect x="20" y="40" width="420" height="190" class="f-acc" stroke="currentColor"/>
  <text x="30" y="62" fill="currentColor" font-family="monospace">+0    x0  x1      stp x0, x1, [sp, #0]</text>
  <text x="30" y="82" fill="currentColor" font-family="monospace">+16   x2  x3</text>
  <text x="30" y="102" fill="currentColor" font-family="monospace">…     (짝마다 16 바이트)</text>
  <text x="30" y="122" fill="currentColor" font-family="monospace">+144  x18 x19   ← 여기부터 callee-saved</text>
  <text x="30" y="142" fill="currentColor" font-family="monospace">…</text>
  <text x="30" y="162" fill="currentColor" font-family="monospace">+224  x28 x29(fp)</text>
  <text x="30" y="182" fill="currentColor" font-family="monospace">+240  x30(lr)   str x30, [sp, #240]</text>
  <text x="30" y="214" fill="currentColor" font-size="12">31 × 8 = 248 B: 끼어든 코드의 범용 레지스터 전부</text>
  <rect x="20" y="230" width="420" height="80" class="f-warn" stroke="currentColor"/>
  <text x="30" y="252" fill="currentColor" font-family="monospace">+248  SP_EL0   +256 ELR_EL1</text>
  <text x="30" y="272" fill="currentColor" font-family="monospace">+264  SPSR_EL1 +272 ESR_EL1</text>
  <text x="30" y="292" fill="currentColor" font-family="monospace">+280  FAR_EL1  (end = +288)</text>
  <text x="470" y="62" fill="currentColor" font-weight="bold">크기 선택지</text>
  <text x="470" y="88" fill="currentColor" font-family="monospace" font-size="12">33 워드 (x0–x30,ELR,SPSR)</text>
  <text x="470" y="106" fill="currentColor" font-family="monospace" font-size="12">= 264 → 16 의 배수 아님 ✗</text>
  <text x="470" y="132" fill="currentColor" font-family="monospace" font-size="12">34 워드 (+SP_EL0) = 272 ✓</text>
  <text x="470" y="158" fill="currentColor" font-family="monospace" font-size="12">36 워드 (+ESR,FAR) = 288 ✓</text>
  <text x="470" y="194" fill="currentColor" font-size="12">sub sp, sp, #N 의 N 이 16 의</text>
  <text x="470" y="212" fill="currentColor" font-size="12">배수가 아니면 SP 정렬이 깨진다</text>
  <text x="470" y="230" fill="currentColor" font-size="12">→ 다음 [sp] 접근이 SP 정렬 폴트</text>
  <text x="470" y="248" fill="currentColor" font-size="12">(실리콘, SA=1). AAPCS64 도 C 를</text>
  <text x="470" y="266" fill="currentColor" font-size="12">부를 때 sp 16 정렬을 요구한다.</text>
  <text x="470" y="300" fill="currentColor" font-size="12">QEMU 는 검사 생략 (7. 실수 4)</text>
</svg>
```

- ESR/FAR 까지 넣은 이유: C 디스패처가 중첩 예외 걱정 없이 원인을 읽을 수 있고, 크래시 덤프(ARM-M14)에 그대로 찍을 수 있다. 덕분에 크기도 자연히 288 = 16·18 이 된다.
- **x19–x28 은 왜 저장하나?** C 핸들러만 부른다면 AAPCS64 덕분에 C 가 x19–x29 를 보존한다. 그러니 저장하지 않아도 IRQ 는 돌아간다(ARM-M13 의 동기 프레임이 그랬다). 그래도 저장하는 이유는 세 가지다. ① **컨텍스트 스위치**: 핸들러가 "다른 태스크의 프레임" 으로 돌아가려면 그 태스크의 x19–x28 이 어딘가에 있어야 한다. ② 디버거·크래시 덤프·시그널 전달(리눅스 `pt_regs`)이 전체 레지스터를 본다. ③ C 가 프레임의 레지스터를 **고쳐서** 돌려보낼 수 있다. 이 레슨의 svc 테스트가 바로 ③ 이다. C 가 31 개 전부를 새 값으로 바꾸고, `RESTORE_ALL` 이 그 값을 레지스터에 실제로 싣는지 확인한다.

**2) 엔트리 하나에 다 넣기.** `run_qemu.sh objdump` 로 본 0x280 엔트리의 끝부분 (실제 출력):

```text
    40081ac0:	f9007bfe 	str	x30, [sp, #240]
    40081ac4:	d5384100 	mrs	x0, sp_el0
    40081ac8:	d5384021 	mrs	x1, elr_el1
    40081acc:	d5384002 	mrs	x2, spsr_el1
    40081ad0:	d5385203 	mrs	x3, esr_el1
    40081ad4:	d5386004 	mrs	x4, far_el1
    40081ad8:	a90f87e0 	stp	x0, x1, [sp, #248]
    40081adc:	a9108fe2 	stp	x2, x3, [sp, #264]
    40081ae0:	f9008fe4 	str	x4, [sp, #280]
    40081ae4:	910003e0 	mov	x0, sp
    40081ae8:	d28000a1 	mov	x1, #0x5                   	// #5
    40081aec:	97fffac5 	bl	40080600 <exc_c>
    40081af0:	14000144 	b	40082000 <ret_from_exc>
	...
```

0x40081a80 에서 시작해 0x40081af0 이 29 번째 명령이다. 남은 3 칸은 `.org` 가 0 으로 채운다. 매크로 끝의 `.org .Lvent\kind + 0x80` 은 엔트리가 0x80 을 넘으면 어셈블러가 "뒤로 갈 수 없다" 에러를 내게 하는 장치다(리눅스 `kernel_ventry` 와 같은 방식 — 확인 권장). 처음에는 `.if (end - start) != 0x800` 으로 검사하려 했다. 그런데 gas 가 `non-constant expression in ".if"` 로 거부했다(정렬 frag 때문에 어셈블 시점에는 거리가 확정되지 않는다).

리눅스는 엔트리에 `sub sp` + 분기만 두고 저장은 공통 코드에서 한다(확인 권장). 그 방식은 엔트리가 짧아 디버깅이 쉽고, 분기 한 번이 더 든다. 이 레슨은 "엔트리 번호를 x1 로 넘기기" 를 간단히 하려고 저장을 엔트리 안에 두었다.

**3) GIC 초기화 순서와 실측 값.**

| 단계 | 레지스터 | 쓴 값 | 실측 읽기 |
|---|---|---|---|
| 정보 | GICD_TYPER | — | 0x8 → ITLinesNumber 8 → 32·9 = 288 개 INTID, CPU 1 개, 보안 확장 없음 (연습 4) |
| 정보 | GICD_IIDR / GICC_IIDR | — | 0x43b / 0x2043b (0x43B = Arm JEP106, GICC 쪽 [19:16] = 2 → GICv2 — 해석은 확인 권장) |
| 1 | GICD_CTLR | 0 (설정 중 끔) | |
| 2 | GICC_PMR | 0xF0 | 0xf0 |
| 3 | GICC_BPR | 2 | 2 (리셋 값은 0 — starter 출력) |
| 4 | GICC_CTLR | 1 | 1 (그룹 0, FIQEn = 0 → IRQ 로) |
| 5 | GICD_CTLR | 1 | 1 |
| 6 | IPRIORITYR[27] (0x0800041B 바이트 = 0x08000418 워드의 bit 31:24) | 0xA0 | 0xa0 (8 비트 다 구현) |
| 7 | ISENABLER0 (0x08000100) | 1 << 27 | 0x800ffff — bit 0..15 (SGI) 는 처음부터 1 |
| — | ICFGR1 (PPI 16–31) | (안 씀) | 0x0 → PPI 는 레벨 |
| — | GICC_IAR (아무것도 pending 아님) | — | 0x3ff = 1023 |

SGI enable 비트가 리셋부터 1 인 것(0xffff)과 PPI 설정이 읽기 전용 레벨인 것은 QEMU 가 보여 준 값이다. GICv2 스펙은 둘 다 구현 정의로 허용한다고 기억한다(확인 권장).

**4) 타이머.** CNTFRQ_EL0 = 0x3b9aca0 = **62,500,000** (62.5 MHz). 1 kHz → `ticks_per_period` = 62500 (0xf424). `TVAL ← 62500; CTL ← ENABLE` 다음에 첫 틱을 기다렸다(바쁜 대기, 200 ms 상한). 이어서 `wfi` 루프로 101 틱을 셌다. 실측 `ticks = 101`, `g_last_intid == 27`, 끝난 뒤 `GICC_RPR = 0xff`(활성 없음).

**5) 첫 IRQ 의 순간 — gdb 실측** (`run_qemu.sh gdb`, 엔트리 0x280 의 `bl exc_c` 에서 멈춤, 전체는 `demo_irq_out.txt`):

```text
pc=0x40081aec cpsr=0x3c5 EL=1 sp=0x40093340 x0=0x40093340 x1=5
ELR_EL1=0x400822b0 SPSR_EL1=0x80000345 ESR_EL1=0x56000077
elr=0x400822b0 spsr=0x80000345 esr=0x56000077 sp_el0=0
x[30]=0x40080150  x[29]=0x40093460  sizeof=288
read_cntvct + 8 in section .text
wait_until + 48 in section .text
GICC_HPPIR=0x1b GICD_ISPENDR0=0x8000000 GICC_RPR=0xff
```

- `cpsr = 0x3c5`: 진입하자 DAIF 가 1111 이 됐다. 그래서 핸들러 안에서는 다른 IRQ 가 들어오지 않는다.
- `SPSR_EL1 = 0x80000345`: 끼어든 코드의 PSTATE 다. M = 0101 (EL1h), bit 7 (I) = 0 → `wait_until` 이 `daifclr #2` 로 I 만 열어 둔 상태. D·A·F 는 부팅 때 그대로 1. N = 1 은 루프의 `cmp` 결과다. 복귀할 때 이 값이 PSTATE 로 돌아가므로 **NZCV 도 SPSR 를 통해 보존**된다. 그래서 센티널 검사기가 NZCV 까지 확인한다.
- `ELR = read_cntvct + 8` (= `ret`): IRQ 는 `mrs x0, cntvct_el0` 다음에 떨어졌다. x30 = `wait_until + 48` 은 그 함수가 돌아갈 곳이다. 이 x30 을 프레임에 저장하지 않으면 무슨 일이 생기는지가 7. 실수 1 이다.
- `ESR_EL1 = 0x56000077`: 앞서 svc 테스트(`svc #0x77`)가 남긴 값이 **그대로** 있다. IRQ 의 원인은 ESR 이 아니라 GIC 의 IAR 에서 읽어야 한다.
- 아직 IAR 을 읽기 전이라 HPPIR = 0x1b (27), RPR = 0xff.

**6) 레지스터 보존 검사기 `regs_hold`.** x30 을 베이스로 x0..x29 를 채우고 x30 을 마지막에 채운다. 그 다음 `daifclr #2; wfi; nop×16; daifset #2` 창을 연다. 창이 끝나면 sp 를 베이스로 31 개 + NZCV 를 스냅샷한다. 범용 레지스터를 하나도 쓰지 않는 구간이므로, 값이 하나라도 바뀌었다면 그건 IRQ 경로 탓이다. 각 IRQ 의 ELR 이 창 [hold_win_start, hold_win_end] 안에 있었는지도 세었다(`hits`). 테스트가 "IRQ 가 한 번도 안 와서 통과" 하는 공허한 테스트가 아님을 보이기 위해서다. 실측: 200 번 호출 → hits 201, 불일치 마스크 0x0. 다른 실행에서는 200 이었다. 201 은 아마 어떤 호출에서 `daifclr` 직후 이미 pending 이던 IRQ 하나와 `wfi` 가 기다린 IRQ 하나를 함께 맞은 경우다(그래서 CHECK 는 `>= 200`).

### Part B — 실측: 무엇이 어떻게 망가지나

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">GICv2 INTID 27 의 상태 — 정상 경로와 두 가지 버그 (괄호 = 실측)</text>
  <rect x="20" y="50" width="120" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="44" y="77" fill="currentColor">inactive</text>
  <rect x="200" y="50" width="120" height="44" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="228" y="77" fill="currentColor">pending</text>
  <rect x="380" y="50" width="120" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="414" y="77" fill="currentColor">active</text>
  <rect x="560" y="50" width="160" height="44" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="572" y="77" fill="currentColor">active + pending</text>
  <path d="M140 72 L198 72" stroke="currentColor" stroke-width="2"/>
  <path d="M190 67 L198 72 L190 77" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="140" y="44" fill="currentColor" font-size="12">선 high</text>
  <path d="M320 72 L378 72" stroke="currentColor" stroke-width="2"/>
  <path d="M370 67 L378 72 L370 77" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="318" y="44" fill="currentColor" font-size="12">IAR 읽기</text>
  <path d="M500 72 L558 72" stroke="currentColor" stroke-width="2"/>
  <path d="M550 67 L558 72 L550 77" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="498" y="44" fill="currentColor" font-size="12">선 다시 high</text>
  <path d="M440 94 C 440 140, 80 140, 80 96" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M75 104 L80 96 L85 104" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="200" y="136" fill="currentColor" font-size="12">EOIR (재무장으로 선이 내려가 있으면) → inactive</text>
  <rect x="20" y="166" width="340" height="120" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="30" y="188" fill="currentColor" font-weight="bold">버그 1: EOI 없음</text>
  <text x="30" y="208" fill="currentColor" font-size="12">active 에 갇힘 → RPR = 0xa0 (실측)</text>
  <text x="30" y="226" fill="currentColor" font-size="12">다음 만료: active+pending (ISPENDR0 0x8000000)</text>
  <text x="30" y="244" fill="currentColor" font-size="12">0xa0 은 RPR 0xa0 보다 높지 않다 → 신호 없음</text>
  <text x="30" y="262" fill="currentColor" font-size="12">30 주기 동안 IRQ 1 번 (실측)</text>
  <text x="30" y="280" fill="currentColor" font-size="12">HPPIR = 0x3ff (QEMU: active 는 후보 제외)</text>
  <rect x="380" y="166" width="340" height="120" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="390" y="188" fill="currentColor" font-weight="bold">버그 3: 재무장 없음 (레벨)</text>
  <text x="390" y="208" fill="currentColor" font-size="12">EOIR → inactive, 그런데 선은 여전히 high</text>
  <text x="390" y="226" fill="currentColor" font-size="12">→ 곧바로 pending → 또 IRQ → …</text>
  <text x="390" y="244" fill="currentColor" font-size="12">2000 번 모두 ISTATUS = 1 로 진입 (실측)</text>
  <text x="390" y="262" fill="currentColor" font-size="12">IMASK 로만 멈춤: CNTV_CTL = 0x7</text>
  <text x="390" y="280" fill="currentColor" font-size="12">main 루프 진행: 0 ~ 17040 회 (실행마다 다름)</text>
</svg>
```

`run_qemu.sh solution` 의 Part B 출력 (실제):

```text
  no-EOI: IRQs in 30 periods = 1
  no-EOI: GICC_RPR  = 0xa0
  no-EOI: GICC_HPPIR= 0x3ff
  no-EOI: ISACTIVER0= 0x8000000
  no-EOI: ISPENDR0  = 0x8000000
  lr-lost: mismatch mask = 0x40000000
  lr-lost: x30 after IRQ = 0x40081af0
  lr-lost: = vectors_el1 + 0x2f0
  storm: IRQs taken      = 2000
  storm: main loop spins = 11377
  storm: CNTV_CTL after  = 0x7
  spurious: count = 1
```

| 버그 | 넣은 방법 | 실측 증상 | 테스트가 확인한 것 |
|---|---|---|---|
| EOI 없음 | `MODE_NO_EOI`: 재무장은 하고 EOIR 만 생략 | 30 주기에 IRQ 1 번. RPR 0xa0, 27 은 active 이면서 pending | `g_ticks - t0 == 1`, `rpr == 0xA0`, 늦은 EOIR 뒤 다시 3 틱, RPR 0xff |
| x30 저장 없음 | `MODE_LR_LOST`: `exc_c` 가 frame.x[30] 을 자기 복귀 주소로 덮는다 = 저장 안 했을 때 남는 값 | 끼어든 코드의 x30 = 0x40081af0 = `vectors_el1 + 0x2f0` (0x280 엔트리의 `bl` 다음) | 마스크 == 1<<30 정확히, 값이 0x280–0x300 범위 |
| 재무장 없음 | `MODE_STORM`: EOI 는 함 | EOI 직후 같은 IRQ 반복, 2000 번 | 2000 번 모두 ISTATUS = 1, IMASK 뒤 조용 |
| spurious | `MODE_SPURIOUS`: `exc_c` 가 IAR 읽기 전에 CNTV_CTL = 0 (장치가 선을 내린 상황) | IAR = 1023 | spurious 1 번, 틱 안 셈, RPR 0xff (EOI 할 게 없었다) |

- **x30 을 "시뮬레이션" 한 이유.** 진짜로 저장을 빼면, IRQ 가 C 코드 한가운데 떨어지는 순간 프로그램이 죽는다(7. 실수 1). 그러면 검사기가 무엇을 잡는지 숫자로 확인할 수 없다. 그래서 `exc_c` 가 `__builtin_return_address(0)` 를 frame.x[30] 에 쓰게 했다. `bl exc_c` 가 x30 에 남기는 바로 그 값이다. 진짜 버그 빌드에서 svc 테스트가 같은 마스크(0x40000000)를 보인 것으로 둘이 일치함을 확인했다.
- **HPPIR 이 1023 인 이유.** 27 은 pending 이지만 동시에 active 다. QEMU 는 최고 우선순위 pending 후보를 고를 때 active 인 것을 뺀다 — `hw/intc/arm_gic.c:94-97` (v10.2.0, `!GIC_DIST_TEST_ACTIVE(irq, cm)`). 연습 2 처럼 PMR 에 막혀도 1023 이다(같은 파일 200-201 줄: `best_prio < priority_mask` 일 때만 current_pending 을 기록). 실리콘 GIC-400 이 같은 값을 주는지는 스펙 확인 권장이다. 그러니 디버깅할 때는 HPPIR 하나만 보지 말고 ISPENDR/ISACTIVER 를 함께 보자.
- **폭주 중 main 이 얼마나 굶나.** 실행마다 0, 4824, 4854, 5805, 7437, 8280, 10487, 11377, 17040 번이 나왔다. QEMU 가 IRQ 를 "다음 명령 경계" 가 아니라 번역 블록(TB) 경계에서 확인하기 때문으로 보인다(QEMU 동작에 대한 추정 — 확인 권장). 실리콘이라면 main 은 사실상 멈춘다. 숫자는 출력만 하고 CHECK 하지 않는다.

### Part C — 중첩 IRQ

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">타이머(0xa0) 핸들러 안에 SGI 1(0x80) — 두 겹 프레임과 ELR_EL1 의 운명</text>
  <rect x="20" y="40" width="300" height="270" class="f-mut" stroke="currentColor"/>
  <text x="30" y="60" fill="currentColor" font-weight="bold">SP_EL1 (높은 주소 → 아래로)</text>
  <rect x="40" y="72" width="260" height="40" class="f-ok" stroke="currentColor"/>
  <text x="50" y="96" fill="currentColor" font-size="12">regs_hold 의 스택 (센티널 보유)</text>
  <rect x="40" y="118" width="260" height="56" class="f-acc" stroke="currentColor"/>
  <text x="50" y="138" fill="currentColor" font-size="12">프레임 1 (288 B): 타이머 IRQ</text>
  <text x="50" y="158" fill="currentColor" font-size="12" font-family="monospace">elr = hold 창 안</text>
  <rect x="40" y="180" width="260" height="34" class="f-mut" stroke="currentColor"/>
  <text x="50" y="202" fill="currentColor" font-size="12">exc_c → irq_c → on_irq 의 C 스택</text>
  <rect x="40" y="220" width="260" height="56" class="f-warn" stroke="currentColor"/>
  <text x="50" y="240" fill="currentColor" font-size="12">프레임 2 (288 B): SGI IRQ</text>
  <text x="50" y="260" fill="currentColor" font-size="12" font-family="monospace">elr = on_irq 의 대기 루프</text>
  <text x="30" y="300" fill="currentColor" font-size="12">depth 2 (g_max_depth == 2 실측)</text>
  <text x="350" y="60" fill="currentColor" font-weight="bold">on_irq(타이머) 의 순서</text>
  <text x="350" y="84" fill="currentColor" font-size="12" font-family="monospace">1 SAVE_ALL 이 ELR/SPSR → 프레임 1</text>
  <text x="350" y="106" fill="currentColor" font-size="12" font-family="monospace">2 RPR 읽기 = 0xa0</text>
  <text x="350" y="128" fill="currentColor" font-size="12" font-family="monospace">3 GICD_SGIR = (2&lt;&lt;24)|1</text>
  <text x="350" y="150" fill="currentColor" font-size="12" font-family="monospace">4 msr daifclr, #2</text>
  <text x="350" y="172" fill="currentColor" font-size="12" font-family="monospace">  → SGI 진입 (RPR 0x80), EOI, eret</text>
  <text x="350" y="194" fill="currentColor" font-size="12" font-family="monospace">5 msr daifset, #2</text>
  <text x="350" y="216" fill="currentColor" font-size="12" font-family="monospace">6 live ELR_EL1 ≠ 프레임 1 elr (100/100)</text>
  <text x="350" y="238" fill="currentColor" font-size="12" font-family="monospace">7 재무장, EOIR(27), RESTORE_ALL</text>
  <text x="350" y="270" fill="currentColor" font-size="12">6 에서 eret 했다면 on_irq 안으로 돌아간다</text>
  <text x="350" y="290" fill="currentColor" font-size="12">→ bug_noelr 실측: PC = 센티널 값, EC 0x22</text>
</svg>
```

실측 (`run_qemu.sh solution`):

```text
  nest: timer IRQs        = 100
  nest: SGIs (depth 2)    = 100
  nest: ELR_EL1 clobbered = 100
  nest: RPR in timer/SGI  = 0xa080
  nest: mismatch mask     = 0x0
```

- 0x80 이 0xa0 을 선점할 수 있었던 것은 BPR = 2 → 그룹 우선순위 bit[7:3] 이 서로 달랐기(0b10000 vs 0b10100) 때문이다. BPR 을 7 로 두면 그룹 우선순위 비트가 없어 선점이 일어나지 않는다고 기억한다(GICv2, 확인 권장). 이 레슨에서 실측한 것은 아니다.
- SGI 가 `daifclr` 직후 들어온다는 보장은 없다(실리콘에선 GIC → 코어 전달에 시간이 걸린다). 그래서 `g_sgis` 가 바뀔 때까지 최대 100000 번 기다린 뒤 `daifset` 한다. QEMU 에서는 100 번 모두 중첩으로 들어왔다(`g_nest_seen == 100`).
- 순서가 핵심이다. **ELR/SPSR 저장 → IRQ 열기 → … → IRQ 닫기 → ELR/SPSR 복원 → ERET.** `RESTORE_ALL` 의 `msr elr_el1` 부터 `eret` 까지는 IRQ 가 닫혀 있어야 한다. 그 사이에 IRQ 가 들어오면 방금 쓴 ELR 이 또 덮인다.
- RTOS 라면 이 자리에서 "선점형 커널 진입" 을 고민한다. 리눅스 arm64 는 하드 IRQ 핸들러 안에서 IRQ 를 다시 켜지 않는다(중첩 IRQ 없음, 확인 권장). 대신 일을 softirq/스레드로 미룬다(RTOS-M02 의 "지연 처리" 와 같은 생각). FIQ/의사 NMI(GICv3 우선순위 마스킹)는 별개 이야기다.
- 지연 측정: 첫 101 틱의 "마감 → C 진입" 은 3073 ~ 722803 카운터 틱(49 µs ~ 11.6 ms)이었다. 다른 실행에서는 최소 690 틱도 나왔다. 측정 때 맥 부하는 load average 22 였다. QEMU TCG 는 타이머를 호스트 시계로 흉내 내고 명령은 번역해서 실행하므로, 이 숫자는 실리콘의 IRQ 지연과 **무관**하다. 출력만 하고 CHECK 하지 않는다. 실리콘 지연 분석은 BM-A10(Cortex-M)·ARM-A08 쪽 일이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

<!-- code: solution.S diff=starter.S -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"   # 45/45 passed, qemu exit=0 (약 1.5 s)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh gdb"        # demo_irq.gdb → demo_irq_out.txt
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh objdump"    # 0x280 엔트리 29 명령
```

`#ifdef BUG_*` 줄은 7. 의 재현 스위치다(기본은 꺼짐). gdb 스크립트 (QEMU 10.2 gdbstub 은 `$ELR_EL1`, `$SPSR_EL1`, `$ESR_EL1` 같은 이름을 준다 — ARM-M13 에서 확인):

<!-- code: demo_irq.gdb -->

## 7. 자주 하는 실수

**1. x30 을 프레임에 안 넣음 (`run_qemu.sh bug_nolr`, 진짜 빌드).** "x30 은 lr 이니까 C 가 알아서 보존하겠지" 라고 생각하기 쉽다. 하지만 `bl exc_c` 가 **호출하는 순간** x30 을 덮는다. 실측:

```text
  SVC frame mismatch (SAVE)    = 0x40000000
  SVC regs mismatch (RESTORE)  = 0x40000000
FAIL line 360: g_svc_seen_mask == 0u
FAIL line 361: diff_mask(out, want) == 0u
  …
  period (ticks)= 0xf424
unexpected sync: ESR=0x86000000 ELR=0xfd8b77a70db5ef40 FAR=0xfd8b77a70db5ef40
qemu exit=4
```

svc 왕복이 bit 30 만 정확히 잡는다. 그 다음 첫 타이머 IRQ 에서 프로그램이 죽는다. 가장 그럴듯한 경로는 이렇다. IRQ 가 `read_cntvct` 의 `ret` 에 떨어진다(gdb 실측 위치) → 돌아온 x30 = 0x280 엔트리의 `bl` 다음 → `ret` 이 벡터 안의 `b ret_from_exc` 로 간다 → `RESTORE_ALL` 이 프레임이 아닌 일반 스택을 프레임으로 읽는다 → 거기 적혀 있던 쓰레기로 ERET 한다. ESR 0x86000000 = EC 0x21 (같은 EL 명령 어보트), IFSC 0 (level 0 address size fault — MMU off 인데 PA 범위를 넘는 주소). PC 가 된 0xfd8b77a70db5ef40 은 앞선 svc 테스트의 센티널 in[10] 과 **정확히 같은 값**이다(xorshift 순번 8202 로 대조). 스택에 남아 있던 옛 값이 PC 가 된 것이다.

고침: `str x30, [sp, #TF_X30]` 를 `bl` 전에, `ldr x30` 를 `eret` 전에 둔다.

**2. ELR/SPSR 를 프레임에서 복원하지 않음 (`bug_noelr`).** 중첩이 없으면 live ELR/SPSR 가 그대로 남아 있어 아무 문제가 없어 보인다. 실측으로 틱 100 개, 센티널 200 번, no-EOI, 폭주, spurious 가 **전부 통과**했다. 걸린 곳은 두 군데다:

```text
  SVC regs mismatch (RESTORE)  = 0x80000000
FAIL line 361: diff_mask(out, want) == 0u
  …
  spurious: count = 1
unexpected sync: ESR=0x8a000000 ELR=0x78fdc9b0cd55277d FAR=0x78fdc9b0cd55277d
qemu exit=4
```

- svc 왕복에서 C 가 프레임의 SPSR 에 쓴 NZCV 가 반영되지 않았다(마스크 bit 31 = NZCV). "C 가 고친 복귀 상태" 가 사라진 것이다. 컨텍스트 스위치였다면 다른 태스크로 가지 못한다.
- 중첩 첫 번째에서 죽었다. ESR 0x8a000000 = EC 0x22 (PC 정렬 폴트). 바깥 핸들러가 SGI 의 ELR(on_irq 대기 루프 안)로 ERET 해서, 이미 프레임이 빠진 sp 로 on_irq 의 뒷부분을 다시 돌았다. 결국 엉뚱한 슬롯에서 읽은 값으로 `ret` 했다. 그 PC 0x78fdc9b0cd55277d 는 첫 NEST `regs_hold` 호출이 x30 에 넣은 센티널과 정확히 같다(xorshift 순번 14484 = 그 호출의 index 30 — 세부 경로는 추정).

고침: `SAVE_ALL` 에서 ELR/SPSR 를 프레임에 넣고, `RESTORE_ALL` 에서 프레임 → `msr` 한 뒤 `eret`. 중첩을 허용하려면 반드시 저장한 **뒤에** `daifclr`.

**3. EOI 를 깜빡하거나 재무장을 깜빡함.** Part B 표 그대로다. 둘 다 처음 한 번은 멀쩡히 동작하므로 "틱이 한 번만 오네", "부팅 직후 멈추네" 로 나타난다. 판단 기준: RPR 이 0xff 로 돌아오지 않으면 EOI 문제, ISTATUS 가 1 로 남아 같은 INTID 가 계속 나오면 재무장 문제. 변이 테스트(mutant)에서 재무장 한 줄을 지웠을 때는 50000 번 감시가 `exception storm` 을 찍고 exit 3 으로 끝났다.

**4. 프레임 크기를 16 의 배수가 아니게 (`bug_align`, 280 B).** 실리콘(SCTLR_EL1.SA = 1, 이 QEMU 의 리셋 값도 0xc50838 로 SA = 1)이라면 첫 `stp x0, x1, [sp]` 가 SP 정렬 폴트(EC 0x26)를 낸다. 그리고 sync 벡터도 같은 `sub sp` 를 하므로 같은 폴트를 반복한다(확인 권장). 그런데 실측은:

```text
  SVC frame mismatch (SAVE)    = 0x0
  SVC regs mismatch (RESTORE)  = 0x0
45/45 passed
qemu exit=0
```

**QEMU 는 SP 정렬 검사를 하지 않는다.** `target/arm/tcg/translate-a64.c:1672-1683` (v10.2.0) 의 `gen_check_sp_alignment()` 는 빈 함수다. 주석에 이렇게 적혀 있다: *"In line with general QEMU practice regarding misaligned accesses, we omit these checks for the sake of guest program performance."* 에뮬레이터에서 통과한 코드가 보드에서 첫 IRQ 에 죽는 전형적인 함정이다. 그러니 `_Static_assert(sizeof(struct trap_frame) % 16 == 0)` 같은 정적 검사가 유일한 안전망이다.

**5. EOIR 에 INTID 만 씀 / spurious 에도 EOI.** 변이 테스트 결과, 둘 다 이 하네스가 **잡지 못했다**(45/45). 이유도 확인했다.
- `EOIR = id`(CPUID 비트 제거): 코어가 하나뿐이라 SGI 의 CPUID 가 항상 0 이다. 그래서 iar 과 id 가 같은 값이 된다. 멀티코어에서 다른 코어가 보낸 SGI 라면 [12:10] 이 0 이 아니다. 이때 CPUID 가 틀린 EOI 는 그 인터럽트를 끝내지 못한다(GICv2 spec — 확인 권장). 습관적으로 **iar 그대로** 쓰자.
- 1023 에 EOI: GICv2 는 spurious ID 를 쓴 EOIR 을 무시한다. QEMU `hw/intc/arm_gic.c:899-900`: *"If software writes the ID of a spurious interrupt [ie 1023] to the GICC_EOIR, the GIC ignores that write."* 무해하지만 "spurious 면 EOI 하지 않는다" 가 스펙의 의도다.

## 8. 면접 Q&A

<details>
<summary>Q1. AArch64 IRQ 진입 때 하드웨어는 뭘 저장하고, 소프트웨어는 뭘 저장해야 해?</summary>

하드웨어는 ELR_EL1(복귀 주소), SPSR_EL1(PSTATE: NZCV·DAIF·EL·SP 선택)만 쓰고 DAIF 를 전부 마스크한 뒤 VBAR_EL1+0x280(현재 EL, SP_ELx)으로 간다. 범용 레지스터는 하나도 저장하지 않는다. 소프트웨어는 C 를 부르기 전에 최소한 caller-saved(x0–x18)와 x29/x30 을 저장해야 한다. ELR/SPSR 는 핸들러가 IRQ 를 다시 열거나 sync 예외를 낼 수 있다면 저장해야 한다. 컨텍스트 스위치·디버깅을 위해서는 x0–x30 전부와 SP_EL0 까지 저장한다. 이 레슨은 288 B 를 저장한다. ESR 은 IRQ 때 갱신되지 않는다(실측: 이전 svc 값이 남아 있었다).

</details>

<details>
<summary>Q2. 프레임 크기 264 B 로 하면 왜 안 돼? QEMU 에선 돌던데?</summary>

`sub sp, sp, #264` 후 sp 가 8 mod 16 이 된다. SCTLR_EL1.SA = 1 이면 sp 를 베이스로 한 첫 load/store 가 SP 정렬 폴트(EC 0x26)다. 게다가 AAPCS64 는 C 함수 호출 시점에 sp 16 정렬을 요구한다. QEMU 는 성능 때문에 이 검사를 생략한다(`translate-a64.c` 의 빈 `gen_check_sp_alignment`). 그래서 280 B 프레임도 45/45 를 통과했다. 실측으로 확인했기 때문에, 이건 보드에서만 터지는 버그라고 말할 수 있다.

</details>

<details>
<summary>Q3. C 핸들러가 x19–x28 을 보존하는데 왜 굳이 저장해? (꼬리: 언제 안 저장해도 돼?)</summary>

C 만 부르고 같은 곳으로 돌아간다면 저장하지 않아도 동작한다(ARM-M13 의 동기 프레임). 그래도 저장하는 이유는 ① 컨텍스트 스위치 — 핸들러에서 다른 태스크로 ERET 하려면 지금 태스크의 x19–x28 을 프레임에 남겨야 한다, ② ptrace·크래시 덤프·시그널(리눅스 pt_regs) — 전체 상태를 읽고 쓴다, ③ 이 레슨의 svc 테스트처럼 핸들러가 레지스터 값을 바꿔서 돌려보내는 경우다. 안 저장해도 되는 경우: 아주 빠른 경로(예: 카운터 하나만 올리는 FIQ)를 asm 으로만 짜서 쓰는 레지스터만 저장할 때다.

</details>

<details>
<summary>Q4. GIC 에서 IAR 을 읽으면 무슨 일이 일어나? EOI 를 안 하면?</summary>

IAR 읽기 = acknowledge 다. 가장 높은 pending 인터럽트가 active 가 되고, CPU 인터페이스의 실행 우선순위(RPR)가 그 우선순위로 올라간다. 이후 그보다 높지 않은 인터럽트는 신호되지 않는다. EOIR 을 쓰면 우선순위가 내려가고(EOImode 0 이면 비활성화도 함께) RPR 이 돌아간다. EOI 를 빠뜨리면 실측으로 30 주기 동안 IRQ 가 1 번만 왔고, RPR = 0xa0 에 묶였으며, 27 은 active+pending 이었다. 같은 우선순위 이하의 **모든** 인터럽트가 함께 막힌다는 점이 무섭다.

</details>

<details>
<summary>Q5. 핸들러 안에서 인터럽트를 다시 켜려면 뭘 먼저 해야 해? (꼬리: GIC 쪽 조건은?)</summary>

CPU 쪽: ELR_EL1/SPSR_EL1 를 프레임(메모리)에 저장한 **뒤에** `msr daifclr, #2`. 다음 IRQ 가 그 둘을 덮기 때문이다. 실측으로 중첩 100 번 모두 live ELR 이 바뀌었다. 저장하지 않은 빌드는 첫 중첩에서 EC 0x22 로 죽었다. 돌아갈 때는 `daifset` → 프레임에서 `msr elr/spsr` → `eret`. GIC 쪽: 새 인터럽트의 **그룹 우선순위**(BPR 로 정해지는 상위 비트)가 현재 실행 우선순위보다 높아야 선점한다. 실측에서 0x80 이 0xa0 핸들러 안으로 들어왔고(RPR 0xa0 → 0x80), 같은 0xa0 은 들어오지 못한다(no-EOI 실험).

</details>

<details>
<summary>Q6. 타이머 IRQ 가 끝없이 들어와. 원인 후보는? (꼬리: 레벨 vs 에지)</summary>

제네릭 타이머 출력은 레벨이다. CVAL ≤ CNT 인 동안 ISTATUS = 1 이고 선이 high 다. 핸들러가 EOI 만 하고 TVAL/CVAL 을 다시 쓰지 않거나 IMASK 를 켜지 않으면, EOI 직후 다시 pending → IRQ 가 된다. 실측으로 2000 번 모두 ISTATUS = 1 로 진입했다. 에지 트리거라면 한 번의 상승 에지에 한 번만 pending 이 되므로 이런 폭주는 없다. 대신 처리 중 들어온 에지를 놓치기 쉽다. 그 밖의 후보: ICFGR 을 잘못 설정한 SPI, 장치 쪽 상태 레지스터를 클리어하지 않은 경우(UART RX FIFO 를 안 비움).

</details>

<details>
<summary>Q7. spurious 인터럽트 1023 은 언제 생기고 어떻게 처리해?</summary>

CPU 가 IRQ 신호를 받은 뒤 IAR 을 읽기 전에 그 인터럽트가 사라지면 생긴다. 장치가 선을 내렸거나, 다른 코어가 먼저 acknowledge 했거나, 우선순위 마스크가 바뀐 경우다. IAR = 1023 이면 아무것도 하지 않고 돌아간다. EOI 도 하지 않는다(GICv2 는 1023 EOI 를 무시한다 — QEMU 소스 주석으로 확인). 이 레슨에서는 IAR 읽기 직전에 타이머를 꺼서 재현했다. spurious 1 번, 틱 0 번, RPR 0xff. 1020–1022 도 특수 ID 라 `>= 1020` 으로 거른다.

</details>

<details>
<summary>Q8. 리눅스는 왜 IRQ 마다 v0–v31 을 저장하지 않아? (꼬리: 이 설계가 틀리는 경우는?)</summary>

커널 C 코드를 `-mgeneral-regs-only` 로 빌드하므로 핸들러가 FP/SIMD 레지스터를 건드리지 않는다. 그러니 IRQ 진입·복귀 때 512 B + FPSR/FPCR 을 저장할 필요가 없다. 사용자 FP 상태는 컨텍스트 스위치 때(또는 필요할 때 lazy 하게) 저장한다. 커널이 NEON 을 쓰려면 `kernel_neon_begin/end` 로 명시적으로 저장한다(확인 권장). 이 레슨도 같은 플래그를 쓴다(ARM-M13 실수 3: 플래그를 빼면 컴파일러가 NEON 을 써서 CPACR 트랩이 났다). 틀리는 경우: 핸들러가 부르는 라이브러리(memcpy 등)가 NEON 으로 빌드돼 있으면 사용자 v 레지스터를 조용히 망가뜨린다. 그래서 커널·RTOS 는 FP 사용 영역을 빌드 플래그로 강제한다.

</details>

<details>
<summary>Q9. 틱을 TVAL 로 재무장하는 것과 CVAL += 주기로 하는 것의 차이는? (꼬리: RTOS 라면?)</summary>

TVAL 은 "지금 + 주기" 라서 매 틱마다 IRQ 지연만큼 늦어지고, 그 오차가 쌓인다. CVAL += 주기는 "지난 마감 + 주기" 라서 장기 오차가 0 이다. 연습 3 실측: 물리 타이머 30 틱에서 CVAL 방식의 오차는 정확히 0, TVAL 방식은 245881 ~ 4841392 카운터 틱(실행마다 다름, 맥 load average 22 전후). RTOS 틱이나 tickless idle(RTOS-A02)은 CVAL 방식을 쓴다. 다만 핸들러가 한 주기 넘게 늦어지면 CVAL 이 이미 과거라서 IRQ 가 연달아 온다. 이때 몇 틱을 놓쳤는지 계산해 따라잡는 로직이 필요하다.

</details>

## 9. 추가 연습

<details>
<summary>연습 1 — SGI 0..15 를 자기 자신에게 보내고 IAR 을 확인하라</summary>

`GICD_SGIR = (0b10 << 24) | n` (TargetListFilter 0b10 = 요청한 코어 자신). 각 SGI 가 정확히 한 번 오는지, `decode_iar` 가 INTID n · CPUID 0 을 내는지 확인한다. 실측 `IAR of SGI 15 = 0xf` (CPUID 0, 보낸 코어가 0 번이라). 16/16 → 끝나고 RPR 0xff. 멀티코어라면 CPUID 가 보낸 코어 번호가 되고, 그 값을 그대로 EOIR 에 써야 한다(7. 실수 5).

</details>

<details>
<summary>연습 2 — PMR 경계: 0xa0 은 PMR 0xa0 에서 막히나?</summary>

PMR 은 "우선순위 값이 PMR **보다 작아야**(더 높아야) 통과" 다. PMR = 0xa0 으로 10 주기 기다리면 틱 0 번, ISPENDR0 = 0x8000000 (pending 이긴 하다), HPPIR = 0x3ff (QEMU 는 PMR 에 막힌 것을 HPPIR 에 보이지 않는다 — arm_gic.c:200-201). PMR = 0xb0 으로 올리면 바로 들어온다. 크리티컬 섹션에서 "낮은 우선순위만 막기" 를 이 PMR 로 한다. Cortex-M BASEPRI(ARM-M10)에 해당하고, GICv3 의 ICC_PMR_EL1 로 리눅스가 의사 NMI 를 만드는 원리이기도 하다(확인 권장).

</details>

<details>
<summary>연습 3 — 두 번째 소스: 물리 타이머(INTID 30)와 드리프트</summary>

`cntp_*_el0` 도우미를 practice.S 에 더한다. EL2 가 없으므로 EL1 에서 바로 쓸 수 있다. INTID 30 을 0x90 으로 켜고, 300 Hz(208333 틱)로 30 틱을 돌린다. 그동안 가상 타이머(27)도 계속 틱을 센다. 실측 `CNTV ticks meanwhile = 45 ~ 76`. 재무장을 `CVAL += P` 로 하면 (마지막 − 처음) − 29·P = **0** (정확), `TVAL = P` 로 하면 > 0 (실측 245881 ~ 4841392). 함정 하나를 실제로 밟았다. 목표 횟수에 도달해 대기 루프가 끝난 뒤 `irq_disable()` 하기 전에 31 번째 IRQ 가 들어와서 오차가 정확히 1 주기(208333)로 찍혔다. 그래서 핸들러가 목표에 도달하면 **핸들러 안에서** 타이머를 끄도록 고쳤다.

</details>

<details>
<summary>연습 4 — GICD_TYPER 해석기</summary>

ITLinesNumber [4:0] → INTID 수 = 32·(N+1), 최대 1020. CPUNumber [7:5] + 1. SecurityExtn [10]. LSPI [15:11]. 실측 0x8 → 288 개, 1 CPU, 보안 확장 없음(`-M virt` 기본은 secure=off). 드라이버가 SPI 를 초기화할 때 이 수만큼만 레지스터를 돌아야 한다. 그보다 많이 돌면 없는 레지스터에 쓰게 된다. 손으로 만든 0xFCFF → 1020(상한), 8 CPU, 보안 확장, LSPI 31.

</details>

### 한 번에 검증

<!-- code: practice.c -->

<!-- code: practice.S -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # 56/56 passed (본문 45 + 연습 11), qemu exit=0
```

```text
  -- (solution tests above, practice below) --
  ex1: IAR of SGI 15 = 0xf
PASS ok_sgi == 16u
PASS rd32(GICC(GICC_RPR)) == 0xFFu
  ex2: PMR=0xa0 HPPIR = 0x3ff
  ex2: PMR=0xa0 ISPENDR0 = 0x8000000
PASS g_ticks == t0 && ((pend >> INTID_VTIMER) & 1u)
PASS wait_until(&g_ticks, t0 + 1u, frq / 5u)
  ex3: CNTP period (ticks)   = 208333
  ex3: CNTV ticks meanwhile  = 76
  ex3: drift CVAL+=P (ticks) = 0
  ex3: drift TVAL=P  (ticks) = 889472
PASS p_pt_period == 208333u
PASS v1 - v0 > 0u && g_other >= 60u
PASS drift_cval == 0u
PASS drift_tval != ~0ull && drift_tval > 0u
  ex4: GIC interrupts = 288
  ex4: GIC CPUs       = 1
PASS ti.num_irqs == 288u && ti.cpus == 1u && ti.security_ext == 0u
PASS ti.num_irqs == 1020u && ti.cpus == 8u && ti.security_ext == 1u && ti.lspi == 31u
PASS ti.num_irqs == 512u && ti.cpus == 1u && ti.security_ext == 0u
56/56 passed
qemu exit=0
```

여러 번 돌려 본 범위: CNTV 45–76, TVAL 드리프트 245881–4841392 카운터 틱 (맥 load average 22 전후). CVAL 드리프트는 매번 0 이었다.

## 10. 핵심 정리

- AArch64 하드웨어는 ELR/SPSR(+ sync 면 ESR/FAR)만 쓰고 DAIF 를 마스크한다. 범용 레지스터는 **0 개** 저장한다. IRQ 는 VBAR+0x280(현재 EL, SPx), ESR 는 갱신하지 않는다.
- 전체 프레임 = x0–x30 + SP_EL0 + ELR + SPSR (+ ESR, FAR) = 288 B. **16 의 배수**가 아니면 실리콘에선 SP 정렬 폴트가 나지만 QEMU 는 검사를 생략한다 → `_Static_assert` 로 막는다.
- 저장 순서: `sub sp` → GPR 저장 → (스크래치로) 시스템 레지스터 저장. 복원은 역순, x0/x1 은 맨 마지막, `msr elr/spsr` ~ `eret` 사이 IRQ 닫힘.
- GICv2: IAR 읽기 = acknowledge(RPR 상승) → 처리 → 같은 값으로 EOIR. 1023 은 처리·EOI 모두 안 함. EOI 를 빼먹으면 같은·낮은 우선순위가 전부 막힌다.
- 제네릭 타이머는 레벨: EOI 전에 재무장(TVAL/CVAL)하거나 IMASK. 오차가 쌓이지 않으려면 CVAL += 주기.
- 중첩: ELR/SPSR 를 프레임에 저장한 뒤 `daifclr #2`. GIC 는 그룹 우선순위가 더 높은 것만 선점시킨다(BPR).
- QEMU virt: GICD 0x08000000, GICC 0x08010000, CNTFRQ 62.5 MHz, 가상 타이머 INTID 27, 물리 30. QEMU 의 지연 숫자는 의미 없다.

## 참고자료

- Arm Architecture Reference Manual for A-profile (DDI 0487): D1 "The AArch64 Exception Model", ELR/SPSR/ESR/VBAR_EL1, SCTLR_EL1.SA, D11 "The Generic Timer" — <https://developer.arm.com/documentation/ddi0487/latest>
- Arm Generic Interrupt Controller Architecture Specification v2.0 (IHI 0048B): GICD_*/GICC_* 레지스터, IAR/EOIR, BPR, spurious — <https://developer.arm.com/documentation/ihi0048/b>
- CoreLink GIC-400 TRM (실리콘 GICv2 구현) — <https://developer.arm.com/documentation/ddi0471/latest>
- AAPCS64 (sp 16 정렬, callee-saved x19–x29) — <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
- QEMU 소스 v10.2.0 (VM 은 10.2.1): `target/arm/tcg/translate-a64.c:1672-1683` (SP 정렬 검사 생략), `hw/intc/arm_gic.c:94-97` (active 는 HPPIR 후보 제외), `:200-201` (PMR), `:899-900` (EOIR 1023 무시) — <https://gitlab.com/qemu-project/qemu/-/blob/v10.2.0/hw/intc/arm_gic.c>
- QEMU virt 보드 문서 — <https://www.qemu.org/docs/master/system/arm/virt.html>
- Linux arm64 진입 코드 `arch/arm64/kernel/entry.S` (kernel_ventry, kernel_entry/pt_regs) — <https://github.com/torvalds/linux/blob/master/arch/arm64/kernel/entry.S>
- DT 바인딩: `arm,armv8-timer`, `arm,gic` 3-cell interrupts — <https://www.kernel.org/doc/Documentation/devicetree/bindings/>
- 관련 레슨: ARM-M13 (EL·VBAR·ESR, 동기 예외), ARM-M09 (Cortex-M 하드웨어 스태킹과 비교), ARM-M10 (PRIMASK/BASEPRI ↔ DAIF/PMR), ARM-M05 (ISB), ARM-M14 (크래시 덤프), BM-M01 (NVIC 레지스터 계산 ↔ GICD), BM-M02 (우선순위 설계), BM-M11 (WFI), RTOS-M02 (ISR 지연 처리), RTOS-M08 (중첩 프레임 포함 스택 예산), RTOS-A01 (Cortex-M 컨텍스트 스위치), RTOS-A02 (tickless, CVAL), ARM-A01 (PendSV 스위처), ARM-A03 (MMU)
