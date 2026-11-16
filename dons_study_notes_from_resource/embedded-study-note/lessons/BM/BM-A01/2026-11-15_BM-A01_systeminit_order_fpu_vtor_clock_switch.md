---
id: BM-A01
time: 110분
updated: 2026-11-15
---
# BM-A01 SystemInit 순서: FPU 활성화, VTOR 재배치, 클럭 전환

## 학습 목표

- `main` 이전에 무엇이 어떤 순서로 일어나야 하는지 — **FPU(CPACR) → .data/.bss → 생성자 → 벡터 테이블(VTOR) → 클럭** — 와 각 순서를 바꾸면 무엇이 깨지는지를 실측으로 말할 수 있다.
- CPACR 을 켜지 않고 float 를 쓰면 생기는 **NOCP** 를 Cortex-M4F (QEMU `mps2-an386`) 에서 재현하고, UsageFault 를 켰을 때(IPSR 6)와 안 켰을 때(HardFault FORCED, IPSR 3)의 레지스터 값을 구분한다.
- 벡터 테이블을 RAM 으로 복사하고 VTOR 정렬 규칙(2 의 거듭제곱 ≥ 테이블 크기, 최소 128 B)과 그 이유를 설명하며, 런타임 핸들러 교체가 실제로 동작함을 확인한다.
- STM32F4 의 HSI → HSE+PLL 168 MHz 전환을 모델로 구현하고, **플래시 대기 상태는 빨라지기 전에 올리고 느려진 뒤에 내린다** 는 규칙을 결함 주입으로 증명한다.
- 진단 결과를 "부팅 로그" 한 덩어리로 보고하는 bring-up 습관을 들인다.

## 1. 문제

시니어 펌웨어 면접의 단골 질문 "**main 전에 뭘 하나요? FPU 안 켜고 float 쓰면?**" 은 외운 목록이 아니라 **의존성** 을 묻는다. Don 이 SSD 컨트롤러 bring-up 에서 "DDR 트레이닝 전에 PLL 을 잠그고, NAND 클럭을 올리기 전에 타이밍 레지스터를 먼저" 했던 것과 똑같은 종류의 문제다 — 순서가 틀리면 대부분은 *가끔* 죽거나, 최종 상태만 보면 멀쩡해서 더 위험하다. 이 레슨은 진짜 Cortex-M4F (QEMU 10.2.1 `mps2-an386`) 위에서 Reset_Handler 의 순서를 직접 짜고, 순서를 일부러 틀려서 fault 를 관찰한 뒤, mps2 에 없는 클럭 트리(RCC/PLL)는 STM32F4 레지스터 모델로 같은 원리를 검증한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Reset_Handler 의 순서와 "어기면 무엇이 깨지나" (이 레슨의 실측)</text>
  <rect x="10" y="34" width="110" height="50" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="18" y="54" fill="currentColor" font-weight="bold">(1) FPU</text>
  <text x="18" y="72" fill="currentColor">CPACR=0xF00000</text>
  <rect x="135" y="34" width="120" height="50" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="143" y="54" fill="currentColor" font-weight="bold">(2) .data/.bss</text>
  <text x="143" y="72" fill="currentColor">복사 · 0 채우기</text>
  <rect x="270" y="34" width="110" height="50" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="278" y="54" fill="currentColor" font-weight="bold">(2b) 생성자</text>
  <text x="278" y="72" fill="currentColor">init_array (float)</text>
  <rect x="395" y="34" width="120" height="50" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="403" y="54" fill="currentColor" font-weight="bold">(3) VTOR</text>
  <text x="403" y="72" fill="currentColor">RAM 테이블 0x20000600</text>
  <rect x="530" y="34" width="110" height="50" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="538" y="54" fill="currentColor" font-weight="bold">(4) 클럭</text>
  <text x="538" y="72" fill="currentColor">LATENCY → SW</text>
  <rect x="655" y="34" width="75" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="667" y="64" fill="currentColor" font-weight="bold">main</text>
  <path d="M120 59 L135 59 M255 59 L270 59 M380 59 L395 59 M515 59 L530 59 M640 59 L655 59" stroke="currentColor"/>
  <rect x="10" y="110" width="350" height="62" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="18" y="128" fill="currentColor" font-weight="bold">O1: (1) 을 (2b) 뒤로</text>
  <text x="18" y="146" fill="currentColor">생성자의 첫 VFP 명령 vmov.f32 s13,#0.25 에서 NOCP</text>
  <text x="18" y="163" fill="currentColor">→ HardFault FORCED (HFSR 0x40000000, CFSR 0x00080000)</text>
  <rect x="380" y="110" width="350" height="62" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="388" y="128" fill="currentColor" font-weight="bold">O2: (3) 을 (2) 앞으로</text>
  <text x="388" y="146" fill="currentColor">RAM 테이블이 .bss 0 채우기에 지워짐 → 첫 예외의</text>
  <text x="388" y="163" fill="currentColor">벡터 = 0 → QEMU Lockup, exit 134</text>
  <rect x="10" y="190" width="350" height="62" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="18" y="208" fill="currentColor" font-weight="bold">B4: VTOR 에 0x40 어긋난 주소</text>
  <text x="18" y="226" fill="currentColor">하위 7 비트 버려짐 0x20000340 → 0x20000300</text>
  <text x="18" y="243" fill="currentColor">→ SysTick 이 엉뚱한 칸(Wrong_Handler) 실행</text>
  <rect x="380" y="190" width="350" height="62" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="388" y="208" fill="currentColor" font-weight="bold">C3/C5: 클럭 전환 순서 뒤집기 (모델)</text>
  <text x="388" y="226" fill="currentColor">168 MHz 인데 LATENCY 0 인 구간 → 쓰레기 읽기</text>
  <text x="388" y="243" fill="currentColor">199 / 204 회, SWS 폴링이 끝나지 않음</text>
  <path d="M65 84 L65 110 M330 84 L330 110 M455 84 L455 110 M585 84 L585 190" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="10" y="270" width="720" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="18" y="290" fill="currentColor">의존성: 생성자 ← FP·.data·.bss / RAM 벡터 테이블 ⊂ .bss / 클럭 코드 ← .data (SystemCoreClock)</text>
  <text x="18" y="308" fill="currentColor">→ 7 단계의 5040 순열 중 11 규칙을 모두 만족하는 순서는 66 개 (practice.c 연습 3 이 전수 계산)</text>
</svg>
```

### Part A — Reset_Handler 의 순서: CPACR, 초기화, RAM 벡터 테이블

- 주어진 것: `a01.ld` (FLASH 256 KB @0x0, RAM 64 KB @0x20000000, `.init_array` 수집), startup (BM-B08 startup 축약판: 플래시 벡터 테이블 48 칸, `.data` 복사·`.bss` 0·`init_array` 루프), "생성자" `ctor_gain_table()` (`.data` 의 `volatile float g_ctor_scale = 0.5f` 를 읽어 `g_gain[8]` 을 만든다), fault 기록 핸들러 (NOCP 면 FPU 를 켜고 같은 명령을 재실행하는 **진단 정책**).
- **TODO 1** `fpu_enable()`: CPACR (0xE000ED88) CP10/CP11 = full access + DSB/ISB. 전역 변수 금지 (아직 .data/.bss 전).
- **TODO 2** `vtor_relocate()`: 48 칸을 256 B 정렬 RAM 배열로 복사 → DSB → VTOR → DSB/ISB.
- **TODO 3** `vtor_set_handler(exc, fn)`: RAM 테이블의 한 칸 교체, 이전 값 반환, 위험한 요청은 거부.
- 확인: 부팅 중 fault 0 개, `1.5*2.25+0.5 == 3.875f`, `1/3` 의 비트 0x3EAAAAAB, SysTick·IRQ 6 핸들러를 런타임에 바꿔 pend → 새 핸들러가 돈다.

### Part B — 순서 결함과 fault: NOCP 두 갈래, 생성자, VTOR 정렬

- B1/B2: CPACR=0 에서 VFP 명령 → UsageFault 를 켰으면 **UsageFault NOCP**, 안 켰으면 (리셋 기본) **HardFault FORCED** + CFSR 에 NOCP. 쌓인 PC = 그 명령의 라벨, EXC_RETURN 으로 확장 프레임 여부.
- B3: "FPU 가 꺼진 채 생성자" 를 런타임에 재현 → fault 1 번, PC 는 `ctor_gain_table+6`, 명령은 VFP 인코딩.
- B4/B5: VTOR 에 정렬 안 된 주소 → 하위 7 비트가 버려져 엉뚱한 칸 / 128 B 정렬이지만 필요 정렬(256 B)은 아님 → QEMU 는 덧셈이라 "동작해 보임" (실칩 보장 없음).
- 별도 빌드 `run_qemu.sh order`: Reset_Handler 순서를 실제로 바꾼 O1 (FPU 늦게), O2 (VTOR 를 .bss 0 앞에) — 둘 다 잡혀야 통과.
- `run_qemu.sh probe`: 위 모든 QEMU 동작의 원자료 (P1–P10).

### Part C — 클럭 전환: STM32F4 RCC/FLASH 모델

- mps2 에는 RCC/PLL 이 없다. 그래서 **레지스터 모델** (BM-B02 mock 축약판 — `REG_READ/REG_WRITE` → `mock_read32/mock_write32`) 을 펌웨어 안에서 돌린다: HSERDY/PLLRDY/SWS 지연, LATENCY 는 쓴 다음 접근부터 유효, **HCLK > (LATENCY+1)·30 MHz 인 접근마다 `garbage++` 이고 읽기 값이 오염**, APB1 > 42 MHz / APB2 > 84 MHz 면 `apb_viol++`.
- **TODO 4** `clock_up()`: PLL 값 찾기 → HSE (상한 있는 대기) → PLL 설정·잠금 → 버스 분주 → LATENCY 올리기 → SW=PLL.
- **TODO 5** `clock_down()`: SW=HSI → 분주 → LATENCY 내리기 → PLL/HSE 끄기.
- 결함 주입 (순서만 바꾼 시퀀스): LATENCY 를 스위치 뒤에, APB 분주를 스위치 뒤에, 내려올 때 LATENCY 먼저 → 모델이 잡는다. HSE 고장·PLL 미잠금 → 상한 있는 실패, HSI 에 머무름. HSE 4..26 MHz × 목표 6 개 전수.

## 2. Problem (English)

> **Bring-up order for a Cortex-M4F image (QEMU `mps2-an386`).**
>
> Write the pieces of `Reset_Handler` and early init:
>
> 1. `void fpu_enable(void)` — grant full access to CP10 and CP11 in `SCB->CPACR` (`0xE000ED88`, bits [23:20]) without touching other bits, then `DSB; ISB`. It runs before `.data`/`.bss` exist.
> 2. `void vtor_relocate(void)` — copy the 48-entry flash vector table (16 system + 32 IRQs) to a RAM array aligned to the smallest power of two ≥ 48·4 bytes (and ≥ 128), barrier, write `SCB->VTOR`, barrier. Must run *after* `.bss` is zeroed (the array lives in `.bss`).
> 3. `vector_t vtor_set_handler(uint32_t exc, vector_t fn)` — swap one entry at run time and return the old one; refuse (return `NULL`) if VTOR does not point to the RAM table, `exc < 2`, `exc >= 48`, or `fn == NULL`.
> 4. `clk_rc_t clock_up(uint32_t hse_hz, uint32_t target_hz, pll_cfg_t *out)` / `clk_rc_t clock_down(void)` for an STM32F4-style RCC: given helpers `st_hse_on`, `st_pll_config`, `st_pll_on`, `st_bus_prescalers`, `st_flash_latency`, `st_switch`, sequence them so that HCLK never exceeds `(LATENCY+1)·30 MHz` and APB1/APB2 never exceed 42/84 MHz at any register access. Every wait is bounded; on HSE/PLL failure stay on HSI and return the error.
>
> Expected: no fault during boot; `CPACR == 0x00F00000`; with CPACR cleared, a VFP instruction raises UsageFault (`CFSR = 0x00080000` NOCP) if `SHCSR.USGFAULTENA` is set, otherwise HardFault with `HFSR = 0x40000000`; `pll_search(8 MHz, 168 MHz)` finds 5 exact solutions and picks `M=4, N=168, P=2, Q=7`.
>
> Follow-ups: Why `ISB` after CPACR? What does the core do if the first FP instruction comes from a C++ static constructor? Why must the vector table be aligned to a power of two? When slowing the clock down, which comes first? What would you change for a bootloader that jumps to an app (BM-A03)?

## 3. 시작 코드

<!-- code: starter.c -->

같이 쓰는 파일: `a01.ld` (링커 스크립트), `run_qemu.sh` (VM 빌드·실행), `demo_probe.c` (QEMU 사실 확인 펌웨어), `practice.c` (호스트 C). 빌드 플래그: `-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns`.

<!-- code: a01.ld -->

Lima VM `elinux` 안에서 실행한다 (Mac 홈은 VM 에서 읽기 전용 → 스크립트가 `/tmp/BM-A01_work` 로 복사해 빌드). Mac 에서:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-A01
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # 31/50, qemu exit=1
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"   # 50/50, qemu exit=0 (0.3 s)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh probe"      # QEMU 사실 확인 P1~P10
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh order"      # Reset_Handler 순서 결함 O1, O2 (둘 다 잡혀야 exit 0)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh disasm"     # 어느 함수가 VFP 명령을 쓰나
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # practice.c (호스트 C)
```

시작 코드 실행 결과 (실제 출력, Part C 의 FAIL 일부 생략 표시):

```text
   text	   data	    bss	    dec	    hex	filename
  13468	     12	   1224	  14704	   3970	starter.elf
== Part A: Reset_Handler order ==
[boot] (1) FPU    CPACR=0x00f00000 FPCCR=0xc0000000 faults during boot=1
[boot] (2) .data 8 B, .bss 1224 B, ctors 1, g_gain[7]*4=15
[boot] (3) VTOR   0x00000000 -> 0x00000000 (&g_ram_vectors=0x20000500, align 256, 48 vectors)
  boot fault: IPSR=3 CFSR=0x00080000 HFSR=0x40000000 pc=0x00000a3e insn=0xeef56a00 EXC_RETURN=0xfffffff9
FAIL starter.c:442  boot_faults == 0u
  1.5*2.25+0.5 = 3875/1000, 1/3 bits = 0x3eaaaaab
FAIL starter.c:451  SCB_VTOR == (uint32_t)(uintptr_t)g_ram_vectors
FAIL starter.c:455  same
FAIL starter.c:459  old == SysTick_Handler
  swap SysTick: old hits=1 new hits=0
  … (FAIL 5 줄 생략) …
== Part B: wrong order / missing FPU / misaligned VTOR ==
  B1 USGFAULTENA=1: IPSR=6 CFSR=0x00080000 HFSR=0x00000000 pc=0x000005b2 EXC_RETURN=0xffffffe9 result=0x40400000
  … (B2–B6 는 TODO 와 무관 — 정답과 같은 줄) …
== Part C: STM32F4 clock switch (RCC/FLASH model) ==
  C1 clock_up correct order       rc=TODO SWS=0 HCLK=16MHz LAT=0 garbage=0 apb_viol=0 t=0
FAIL starter.c:581  rc == CLK_OK && ((g_m.cfgr >> 2) & 3u) == SW_PLL && m_hclk() == 168000000u && g_core_hz == 168000000u
  … (FAIL 9 줄 생략) …
31/50 passed
qemu exit=1
```

읽는 법: 첫 줄의 `CPACR=0x00f00000` 은 **TODO 를 채워서가 아니라** 진단 핸들러가 NOCP 를 보고 FPU 를 켠 결과다 — 그래서 `faults during boot=1` 과 `IPSR=3 … HFSR=0x40000000` (HardFault FORCED) 이 같이 찍혔다. 리셋 직후 SHCSR=0 이라 UsageFault 가 꺼져 있었기 때문이다. VTOR 은 0 그대로라 RAM 테이블에 쓴 SysTick 교체가 무시되고 옛 핸들러가 돌았다 (`old hits=1`).

## 4. 힌트

<details>
<summary>힌트 1 — CPACR 은 두 칸, 그리고 ISB</summary>

CP10 = bits [21:20], CP11 = bits [23:22]. 각 칸 `00` 거부, `01` 특권 모드만, `11` 전부, `10` 예약. FPU 는 두 코프로세서 번호를 같이 쓰므로 두 칸을 **같은 값**으로 (ARMv7-M ARM: 다르면 UNPREDICTABLE — 확인 권장). `|=` 로 다른 칸 보존. 시스템 레지스터 쓰기가 다음 명령에 반영된다는 보장은 `DSB; ISB` 다 — 이미 파이프라인에 들어온 FP 명령이 옛 권한으로 디코드되지 않게.

</details>

<details>
<summary>힌트 2 — RAM 테이블은 언제 채우나</summary>

`g_ram_vectors` 는 `.bss` 에 있다. `.bss` 를 0 으로 채우는 루프보다 **먼저** 복사하면 곧바로 지워진다 (`run_qemu.sh order` O2). 복사 → DSB (테이블 쓰기가 메모리에 닿게) → VTOR 쓰기 → DSB → ISB.

</details>

<details>
<summary>힌트 3 — 정렬 크기 계산</summary>

칸 수 = 16 + 외부 IRQ 수. QEMU mps2-an386 은 IRQ 32 개 (probe P8: `ICTR=0`, `ISER1` 에 1 을 다 써도 0) → 48 칸 × 4 = 192 B → 2 의 거듭제곱으로 올림 = **256 B**. VTOR 의 하위 7 비트는 아예 저장되지 않는다 (P7: `0xFFFFFFFF` 쓰면 `0xFFFFFF80`).

</details>

<details>
<summary>힌트 4 — 클럭: "빨라지기 전에 준비, 느려진 뒤에 정리"</summary>

플래시 대기 상태(LATENCY)와 APB 분주는 **새 주파수에서 안전한 값**으로 먼저 바꾼다. 올라갈 때: 분주·LATENCY → SW. 내려올 때: SW → 분주·LATENCY. LATENCY 를 쓴 뒤엔 다시 읽어 적용 확인 (`st_flash_latency` 가 해 준다). 부품이 실패하면 그 rc 를 바로 반환.

</details>

## 5. 풀이

### Part A — CPACR, 초기화, RAM 벡터 테이블

**1) QEMU 가 실제로 무엇을 하나 — 먼저 탐침.** `run_qemu.sh probe` (demo_probe.c) 의 실제 출력:

```text
P1 CPUID=0x410fc240 MVFR0=0x10110021 MVFR1=0x11000011
P2 reset: CPACR=0x00000000 FPCCR=0xc0000000 FPCAR=0x00000000 FPDSCR=0x00000000 CONTROL=0x00000000 VTOR=0x00000000 SHCSR=0x00000000
P6a before any FP: SysTick EXC_RETURN=0xfffffff9 CONTROL=0x00000000
P3 CPACR after write 0xFFFFFFFF -> 0x00f00000
P4 CPACR=0 USGFAULTENA=0: faults=0x00000001 IPSR=0x00000003 CFSR=0x00080000 HFSR=0x40000000 pc=0x0000020c (=label) EXC_RETURN=0xfffffff9
   result after fix-up retry=0x40400000 CPACR now=0x00f00000 CONTROL=0x00000004
P6b after FP use: SysTick EXC_RETURN=0xffffffe9 FPCCR in handler=0xc0000019 FPCAR=0x2000ff88
P5 CPACR=0 USGFAULTENA=1: faults=0x00000001 IPSR=0x00000006 CFSR=0x00080000 HFSR=0x00000000 pc=0x000001f8 (=label) EXC_RETURN=0xffffffe9
   result after fix-up retry=0x40400000
P9 CPACR write 0x00300000 -> 0x00300000 result=0x40400000
    faults=0x00000000 IPSR=0x00000006 CFSR=0x00080000 HFSR=0x00000000 pc=0x000001f8 (!=label) EXC_RETURN=0xffffffe9
P10 LSPEN=1, CPACR=0, FPCA=1, SysTick: EXC_RETURN=0xffffffe9 faults=0x00000000 CPACR now=0x00000000
P10b LSPEN=0, CPACR=0, FPCA=1, SysTick: faults=0x00000001 IPSR=0x00000006 CFSR=0x00080000 FPCCR=0x80000018 CPACR now=0x00f00000
P7 VTOR write 0xFFFFFFFF -> 0xffffff80, 0x20000040 -> 0x20000000, 0x20000080 -> 0x20000080
P8 ICTR=0x00000000 ISER0=0xffffffff ISER1=0x00000000
qemu exit=0
```

(P9 의 둘째 줄은 "fault 0 번" 이고 IPSR/CFSR 칸은 P5 의 기록이 그대로 남은 것이다.)

| 사실 | 값 | 근거 |
|---|---|---|
| 코어 | Cortex-M4 r0p0 (CPUID 0x410FC240), FPv4-SP: MVFR0 의 단정도 2, 배정도 0 | P1 |
| CPACR 리셋값 | 0 (FPU 접근 거부) | P2 |
| CPACR 저장 비트 | [23:20] 만 | P3 / `armv7m_nvic.c:1820` `value & (0xf << 20)` |
| FPCCR 리셋값 | 0xC0000000 = ASPEN(31) \| LSPEN(30) | P2 / `target/arm/cpu.c:433` |
| NOCP, UsageFault 꺼짐 | HardFault, HFSR.FORCED, CFSR bit19 | P4 |
| NOCP, UsageFault 켜짐 | UsageFault (IPSR 6), HFSR 0 | P5 |
| 첫 FP 명령 뒤 | CONTROL.FPCA=1 (ASPEN 덕), 예외 EXC_RETURN 0xFFFFFFE9 | P4, P6b |
| QEMU 가 보는 칸 | **CP10 [21:20] 만** — CP11=0 이어도 통과 | P9 / `internals.h:1170` `extract32(cpacr, 20, 2)` |
| VTOR 쓰기 | `value & 0xFFFFFF80` | P7 / `armv7m_nvic.c:1605` |
| 벡터 주소 | `vecbase + exc * 4` (덧셈) | `m_helper.c:665` |
| 외부 IRQ | 32 개 | P8 / `mps2.c:227–228` `num-irq 32` |

**2) CPACR 레지스터.** 아래 그림의 위쪽. 정답은 한 줄 `SCB_CPACR |= 0xF << 20` 과 배리어 둘이다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">CPACR (0xE000ED88) 와 VTOR (0xE000ED08) — QEMU 실측 비트</text>
  <rect x="10" y="34" width="240" height="40" class="f-mut" stroke="currentColor"/>
  <text x="70" y="58" fill="currentColor">[31:24] 예약 (RAZ)</text>
  <rect x="250" y="34" width="100" height="40" class="f-acc" stroke="currentColor"/>
  <text x="262" y="52" fill="currentColor">CP11 [23:22]</text>
  <text x="280" y="68" fill="currentColor">11</text>
  <rect x="350" y="34" width="100" height="40" class="f-acc" stroke="currentColor"/>
  <text x="362" y="52" fill="currentColor">CP10 [21:20]</text>
  <text x="380" y="68" fill="currentColor">11</text>
  <rect x="450" y="34" width="280" height="40" class="f-mut" stroke="currentColor"/>
  <text x="470" y="58" fill="currentColor">[19:0] 다른 코프로세서 (M4: 없음 → 0)</text>
  <text x="10" y="94" fill="currentColor">00 거부 · 01 특권만 · 10 예약 · 11 전부.  0xFFFFFFFF 를 써도 0x00F00000 만 남는다 (P3).</text>
  <text x="10" y="112" fill="currentColor">QEMU 는 CP10 칸만 검사 (P9: 0x00300000 으로도 동작) — 실칩에선 두 칸이 다르면 UNPREDICTABLE.</text>
  <rect x="10" y="134" width="480" height="40" class="f-ok" stroke="currentColor"/>
  <text x="120" y="158" fill="currentColor">TBLOFF [31:7] — 테이블 시작 주소의 상위 비트</text>
  <rect x="490" y="134" width="240" height="40" class="f-warn" stroke="currentColor"/>
  <text x="520" y="158" fill="currentColor">[6:0] 저장 안 됨 (RAZ/WI)</text>
  <text x="10" y="196" fill="currentColor">쓰기 0x20000340 → 읽기 0x20000300 (B4).  최소 정렬 128 B 는 여기서 온다.</text>
  <text x="10" y="214" fill="currentColor">추가 규칙: 테이블 크기 (16+IRQ)·4 이상의 2 의 거듭제곱으로 정렬 — 48 칸 = 192 B → 256 B.</text>
  <text x="10" y="232" fill="currentColor">정답 배치: g_ram_vectors @0x20000600 (aligned(256), .bss), _Static_assert 로 크기·2 의 거듭제곱 검사.</text>
</svg>
```

**왜 DSB + ISB 인가.** `STR` 로 CPACR 을 쓴 직후에도 그 쓰기가 아직 버스 위에 있을 수 있고(→ DSB 가 완료를 기다림), 그 뒤 이미 가져와 디코드된 명령은 옛 권한을 볼 수 있다(→ ISB 가 파이프라인을 비움). ARM 의 배리어 응용 노트(AN321)는 CPACR 변경 뒤 ISB 를 요구한다 (확인 권장). QEMU 는 명령 하나하나를 순서대로 처리해 배리어를 빼도 결과가 같다 — **배리어 누락은 QEMU 로 검출할 수 없다** (정직하게: 이 레슨의 시험도 못 잡는다).

**3) 왜 맨 앞인가 — 컴파일러는 FP 를 "어디서나" 쓸 수 있다.** `run_qemu.sh disasm` 의 실제 출력:

```text
<ctor_gain_table>:           7 VFP insns
<gain_ok>:                   9 VFP insns
<main>:                      21 VFP insns
Reset_Handler: 0 VFP instructions
fault_c: 0 VFP instructions
Fault_Entry: 0 VFP instructions
--- ctor_gain_table ---
00000d2c <ctor_gain_table>:
     d2c:	4a0a      	ldr	r2, [pc, #40]	@ (d58 <ctor_gain_table+0x2c>)
     d2e:	490b      	ldr	r1, [pc, #44]	@ (d5c <ctor_gain_table+0x30>)
     d30:	2300      	movs	r3, #0
     d32:	eef5 6a00 	vmov.f32	s13, #80	@ 0x3e800000  0.250
     d36:	ee07 3a90 	vmov	s15, r3
     d3a:	ed91 7a00 	vldr	s14, [r1]
     d3e:	eef8 7ae7 	vcvt.f32.s32	s15, s15
```

생성자의 **첫 VFP 명령은 `.data` 읽기도 아니고 상수 `0.25f` 를 레지스터에 넣는 `vmov.f32`** 다 (`ctor_gain_table+6`, 인코딩 0xEEF56A00 — 시작 코드의 boot fault 줄과 B3 줄에 같은 값이 찍혀 있다). 즉 "float 변수를 안 읽었으니 괜찮다" 는 틀린다. `-mfloat-abi=hard` 에선 float 인자·반환값도 s0 으로 오가고, GCC 가 정수 코드의 레지스터 압박 때 S 레지스터를 임시 저장소로 쓰는 경우도 보고된다 (확인 권장). 그래서 CPACR 은 **C 코드가 하나라도 돌기 전** 이 원칙이고, Reset_Handler·fault 경로에 VFP 명령이 0 개인지 disasm 으로 확인하는 것이 안전장치다.

**4) .data → .bss → 생성자 → VTOR.** 생성자는 `.data` (g_ctor_scale) 와 `.bss` (g_gain) 를 쓰므로 둘 다 뒤에. RAM 벡터 테이블은 `.bss` 안이므로 0 채우기 뒤에. 실제 부팅 로그:

```text
[boot] (1) FPU    CPACR=0x00f00000 FPCCR=0xc0000000 faults during boot=0
[boot] (2) .data 8 B, .bss 1480 B, ctors 1, g_gain[7]*4=15
[boot] (3) VTOR   0x00000000 -> 0x20000600 (&g_ram_vectors=0x20000600, align 256, 48 vectors)
  1.5*2.25+0.5 = 3875/1000, 1/3 bits = 0x3eaaaaab
  swap SysTick: old hits=0 new hits=1
  swap IRQ6: Irq6_New hits=1
```

`g_gain[7] = 7·0.5 + 0.25 = 3.75` → ×4 = 15. `1/3` 의 단정도 비트 0x3EAAAAAB 는 VFP `vdiv.f32` 가 진짜로 계산한 값이다 (가수 23 비트 뒤의 나머지가 1010… > 절반이라 …AA 가 …AB 로 올림).

**5) 런타임 핸들러 교체.** VTOR 이 RAM 을 가리키면 테이블 한 칸 = 함수 포인터 한 워드. 정렬된 32 비트 쓰기는 원자적이라 "반쯤 바뀐 주소" 는 없다. DSB 로 쓰기 완료를 보장한 뒤 pend 하면 다음 예외 진입이 새 값을 읽는다. 교체 순서 추적:

| 단계 | RAM[15] | VTOR | pend SysTick → 실행된 것 |
|---|---|---|---|
| 부팅 직후 | SysTick_Handler | 0x20000600 | (안 함) |
| `vtor_set_handler(15, SysTick_New)` → 반환 SysTick_Handler | SysTick_New | 0x20000600 | SysTick_New (new hits 1) |
| `vtor_set_handler(15, SysTick_Handler)` → 반환 SysTick_New | SysTick_Handler | 0x20000600 | SysTick_Handler (old hits 1) |
| 시작 코드: VTOR 미설정 | (RAM 은 0) | 0x00000000 | 플래시 SysTick_Handler (교체 무시) |

`vtor_set_handler` 가 "VTOR ≠ RAM 테이블이면 거부" 하는 이유가 마지막 줄이다: 써 봐야 효과가 없는데 성공을 돌려주면 호출자는 새 핸들러가 붙었다고 믿는다 (변이 M5 가 이걸 잡는다). 칸 0 (초기 SP), 1 (Reset) 도 거부 — 런타임에 바꿀 이유가 없고, 바꾸면 다음 리셋이 아니라 VTOR 을 쓰는 부트로더 점프(BM-A03)에서 사고가 난다.

### Part B — 순서 결함과 fault

**1) NOCP 의 두 갈래.** 정답 실행의 Part B (실제 출력):

```text
== Part B: wrong order / missing FPU / misaligned VTOR ==
  B1 USGFAULTENA=1: IPSR=6 CFSR=0x00080000 HFSR=0x00000000 pc=0x00000722 EXC_RETURN=0xffffffe9 result=0x40400000
  B2 USGFAULTENA=0: IPSR=3 CFSR=0x00080000 HFSR=0x40000000 pc=0x00000714
  B3 ctor with FPU off: faults=1 pc=ctor_gain_table+6 insn=0xeef56a00 table ok after fix-up=1
  B4 VTOR write 0x20000340 -> reads 0x20000300, SysTick -> Wrong_Handler (slot 15 of the masked base)
  B5 VTOR 0x20000380 (128-aligned, needs 256): IRQ31 -> Irq31_Handler (QEMU adds); OR-concatenating HW would fetch slot 47 = table entry 15 (SysTick)
  B6 SysTick after FP use: EXC_RETURN=0xffffffe9, VTOR restored to 0x20000600
```

- B1: SHCSR.USGFAULTENA(bit18)=1 → UsageFault 가 직접 받는다. CFSR=0x00080000 = UFSR 의 NOCP (CFSR bit 19 = UFSR bit 3). HFSR 0.
- B2: 같은 명령, USGFAULTENA=0 → 받을 핸들러가 꺼져 있어 **HardFault 로 승격** (HFSR.FORCED bit30). CFSR 에는 여전히 NOCP — 원인은 CFSR, "누가 받았나" 는 IPSR (BM-M13 그림과 같은 규칙).
- 쌓인 PC = `pc_b1`/`pc_b2` 라벨 = **fault 를 낸 그 명령** (NOCP 는 정밀: 명령 실행 전에 걸린다). 그래서 진단 핸들러가 CPACR 을 켜고 PC 를 그대로 둔 채 복귀하면 같은 명령이 성공한다 (`result=0x40400000` = 3.0f).
- EXC_RETURN 0xFFFFFFE9: bit4=0 → **확장 프레임** (FP 를 쓴 스레드). 부팅 중 생성자 fault 는 0xFFFFFFF9 (아직 FP 를 한 번도 못 써서 FPCA=0). 예외 프레임 크기 32 B vs 104 B 는 연습 4.

**2) 지연 스태킹(lazy stacking) 함정 — CPACR 을 끈 채 예외가 나면?** FPCCR 리셋값 ASPEN=LSPEN=1 이라 FPCA=1 인 스레드에서 예외가 나면 FP 레지스터 자리만 잡고(LSPACT=1, P6b 의 FPCCR=0xC0000019 의 bit0) 실제 저장은 핸들러가 FP 를 쓸 때 한다. P10: LSPEN=1 이면 CPACR=0 인 채 예외가 나도 fault 없음. P10b: LSPEN=0 (진입 때 즉시 저장) 이면 **스태킹 자체가 NOCP UsageFault** — QEMU `m_helper.c` 의 "CPACR.CP10 prevents stacking FP regs" 경로. 복귀 때도 같다: LSPACT=0 인 확장 프레임을 CPACR=0 으로 복원하려 하면 "CPACR.CP10 prevents unstacking FP regs" (`m_helper.c:1784` 부근). 교훈: **FPU 를 런타임에 끄는 코드(저전력 등)는 FPCA/LSPACT 상태까지 정리해야** 한다.

**3) Reset_Handler 순서를 진짜로 바꾼 빌드** — `run_qemu.sh order` 실제 출력:

```text
##### O1: fpu_enable() 을 생성자 뒤로 (-DINJECT_FPU_LATE) #####
[boot] (1) FPU    CPACR=0x00f00000 FPCCR=0xc0000000 faults during boot=1
[boot] (2) .data 8 B, .bss 1480 B, ctors 1, g_gain[7]*4=15
[boot] (3) VTOR   0x00000000 -> 0x20000600 (&g_ram_vectors=0x20000600, align 256, 48 vectors)
  boot fault: IPSR=3 CFSR=0x00080000 HFSR=0x40000000 pc=0x00000d32 insn=0xeef56a00 EXC_RETURN=0xfffffff9
FAIL solution.c:470  boot_faults == 0u
[boot] (4) clock  C1 rc=OK: HSI 16 MHz -> PLL 168 MHz (M=4 N=168 P=2 Q=7), LATENCY ->5 at access #39, SW=PLL at #42, 45 model accesses
49/50 passed
qemu exit=1
O1 caught: NOCP at boot (HardFault FORCED)
##### O2: vtor_relocate() 를 .bss 0 채우기 앞으로 (-DINJECT_VTOR_EARLY) #####
[boot] (1) FPU    CPACR=0x00f00000 FPCCR=0xc0000000 faults during boot=0
[boot] (2) .data 8 B, .bss 1480 B, ctors 1, g_gain[7]*4=15
[boot] (3) VTOR   0x00000000 -> 0x20000600 (&g_ram_vectors=0x20000600, align 256, 48 vectors)
FAIL solution.c:483  same
FAIL solution.c:487  old == SysTick_Handler
FAIL solution.c:496  old6 == Default_Handler
FAIL solution.c:499  g_hit_irq6_new == 1u
qemu: fatal: Lockup: can't escalate 3 to HardFault (current priority -1)
qemu exit=134
O2 caught: table wiped by .bss zeroing
order injections caught: 2/2
```

(O1 의 pc=0xd32 = `ctor_gain_table` 0xd2c + 6 — 위 disasm 과 일치. O1 은 50 개 중 부팅-fault 검사 하나만 FAIL: 나머지는 진단 핸들러가 FPU 를 켜 준 덕에 통과한다.)

O1 은 제품이라면 **가끔만 보이는** 버그다: 진단 핸들러가 없으면 HardFault → (BM-M13 정책이면) 리셋 → 같은 자리에서 또 → 리셋 루프. O2 는 VTOR 은 맞는데 테이블 내용이 0 이다 → SysTick pend → 벡터 0 → 주소 0 으로 Thumb 비트 없이 분기 → fault → HardFault 벡터도 0 → QEMU 가 **Lockup** 으로 끝낸다 (BM-B08 F3 과 같은 메시지). 진단 로그의 `[boot] (3)` 줄은 두 경우 모두 "정상" 으로 보인다는 것도 기억할 것 — VTOR 레지스터만 찍는 부팅 로그로는 O2 를 못 잡는다. 정답의 `same` 검사(RAM 테이블 == 플래시 테이블)가 필요한 이유다.

**4) VTOR 정렬 — B4 와 B5.**

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">벡터 주소 = VTOR + 4·exc (덧셈, QEMU) vs VTOR | 4·exc (비트 결합 모델)</text>
  <text x="10" y="42" fill="currentColor">g_vt_scratch @0x20000300 (256 B 정렬, 512 B). 표 48 칸을 +0x80 (=칸 32) 에 복사, 나머지는 Wrong_Handler</text>
  <rect x="10" y="56" width="160" height="34" class="f-warn" stroke="currentColor"/>
  <text x="18" y="77" fill="currentColor">0x300–0x37C Wrong</text>
  <rect x="170" y="56" width="120" height="34" class="f-ok" stroke="currentColor"/>
  <text x="178" y="77" fill="currentColor">0x380 표[0..15]</text>
  <rect x="290" y="56" width="200" height="34" class="f-ok" stroke="currentColor"/>
  <text x="298" y="77" fill="currentColor">0x3C0–0x43C 표[16..47]</text>
  <rect x="490" y="56" width="240" height="34" class="f-warn" stroke="currentColor"/>
  <text x="498" y="77" fill="currentColor">0x440– Wrong</text>
  <text x="10" y="118" fill="currentColor" font-weight="bold">B5: VTOR = 0x20000380 (128 정렬 OK, 256 정렬 아님), IRQ 31 = 예외 47, 4·47 = 0xBC</text>
  <rect x="10" y="130" width="350" height="56" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="18" y="150" fill="currentColor">덧셈: 0x380 + 0xBC = 0x43C = 표[47]</text>
  <text x="18" y="170" fill="currentColor">→ Irq31_Handler (QEMU 실측, m_helper.c:665)</text>
  <rect x="380" y="130" width="350" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="388" y="150" fill="currentColor">결합: 0x380 | 0x0BC = 0x3BC = 표[15]</text>
  <text x="388" y="170" fill="currentColor">→ SysTick 핸들러가 IRQ 31 대신 실행</text>
  <text x="10" y="212" fill="currentColor">0x80 비트가 VTOR 과 4·exc 양쪽에 있어 겹친다 → exc ≥ 32 인 16 칸이 전부 틀림 (practice 연습 2: 16/48).</text>
  <text x="10" y="232" fill="currentColor">256 B 정렬이면 VTOR 의 하위 8 비트가 0 → 덧셈 == 결합. 그래서 "2 의 거듭제곱 ≥ 테이블 크기" 규칙.</text>
  <rect x="10" y="246" width="720" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="18" y="268" fill="currentColor">B4: VTOR=0x20000340 → 하위 7 비트 버려짐 → 0x20000300 → 칸 15 = Wrong_Handler</text>
</svg>
```

ARMv7-M ARM 의 규칙: 테이블은 "지원 예외 수 × 4 이상인 2 의 거듭제곱" 으로 자연 정렬, 최소 128 B (B3.2.5 VTOR — 절 번호 확인 권장). 이유는 구현이 벡터 주소를 **덧셈 대신 비트 결합** 으로 만들 수 있게 허락하기 위해서다 (덧셈기 없이 배선만으로 — 이 해석은 일반적인 설명이고 ARM 이 내부 구현을 공개하진 않는다, 확인 권장). 규칙을 어기면 결과는 "정의 안 됨" 이지 "반드시 이렇게 틀린다" 가 아니다 — 그래서 B5 의 CHECK 는 "QEMU 는 덧셈이라 동작한다" 를 **QEMU 관찰** 로만 검사하고, 결합 모델의 결과는 계산으로 보여 준다. 실무 결론: QEMU 에서 동작했다고 정렬을 줄이지 말 것 — 정렬은 `_Static_assert` 와 링커 `ASSERT` 로 강제한다.

STM32F407 은 외부 IRQ 82 개 (RM0090 — 확인 권장) → 98 칸 × 4 = 392 B → **512 B 정렬**. Cortex-M4 최대 240 IRQ → 1 KB. 연습 2 가 표로 계산한다.

### Part C — 클럭 전환 (STM32F4 레지스터 모델)

**1) 모델의 정의** (테스트의 진실 = 이 모델; 주소·비트는 RM0090 기준 대표값 — RM 확인 권장):

| 항목 | 모델 |
|---|---|
| 레지스터 | RCC_CR 0x40023800 (HSION 0, HSIRDY 1, HSEON 16, HSERDY 17, PLLON 24, PLLRDY 25, 리셋 0x83), RCC_PLLCFGR 0x40023804 (M [5:0], N [14:6], P [17:16] = P/2−1, PLLSRC 22, Q [27:24], 리셋 0x24003010), RCC_CFGR 0x40023808 (SW [1:0], SWS [3:2], HPRE [7:4], PPRE1 [12:10], PPRE2 [15:13]), FLASH_ACR 0x40023C00 (LATENCY [2:0], PRFTEN 8, ICEN 9, DCEN 10) |
| 시간 | 레지스터 접근 1 번 = 1 틱. HSERDY = HSEON 후 20 틱, PLLRDY = PLLON 후 10 틱 (설정이 유효하고 HSE 가 준비됐을 때만), SWS = SW 를 쓴 뒤 3 틱 (대상이 준비됐을 때만) |
| LATENCY | 쓴 **다음** 접근부터 유효 (RM 의 "다시 읽어 확인" 규칙을 모델로) |
| 위반 | 매 틱 HCLK > (LATENCY+1)·30 MHz 면 `garbage++` 이고 그 틱의 읽기 값은 `^ 0x5A5A5A5A`. PCLK1 > 42 MHz, PCLK2 > 84 MHz 면 `apb_viol++` |
| 무시 | PLL 켜진 채 PLLCFGR 쓰기 → 무시 + `cfg_while_on++`. SWS 는 읽기 전용 |

**2) PLL 값 (BM-B10 계산기 축약판).** HSE 8 MHz → 168 MHz: SYSCLK = 8·N/(M·P). VCO 출력 ≤ 432 MHz 이므로 P=2, VCO=336 MHz 만 가능 (P=4 면 672). VCO 입력 8/M ∈ [1, 2] MHz → M ∈ {4,…,8}, N = 336·M/8 = 42·M → (4,168), (5,210), (6,252), (7,294), (8,336) — 코드가 센 **5 개** 와 일치. 순위: USB 48 MHz 를 정확히 내는 Q (336/48 = 7, 다섯 모두 가능) > VCO 입력이 큼 (2 MHz, 지터·PLL 잡음에 유리 — RM 권고, 확인 권장) → **M=4, N=168, P=2, Q=7**. 교과서 예제의 M=8, N=336 (입력 1 MHz) 도 합법이다. HSE 25 MHz 면 해가 1 개 (M=25, N=336).

**3) 순서와 그 이유.**

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">HCLK 와 "플래시가 감당하는 최대 HCLK" = (LATENCY+1)·30 MHz — 모델 접근 번호 기준</text>
  <line x1="60" y1="250" x2="730" y2="250" stroke="currentColor"/>
  <line x1="60" y1="40" x2="60" y2="250" stroke="currentColor"/>
  <text x="14" y="64" fill="currentColor">180</text><text x="14" y="76" fill="currentColor">168</text>
  <text x="22" y="236" fill="currentColor">16</text><text x="22" y="214" fill="currentColor">30</text>
  <text x="70" y="270" fill="currentColor">정답 C1: … #39 LATENCY=5 쓰기 → #42 SW=PLL 쓰기 → SWS 가 PLL 로 (#45 무렵)</text>
  <path d="M60 232 L330 232 L330 70 L730 70" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M60 214 L300 214 L300 60 L730 60" stroke="currentColor" fill="none" stroke-dasharray="6 3"/>
  <text x="340" y="96" fill="currentColor">실선 HCLK: 16 → 168 MHz</text>
  <text x="340" y="52" fill="currentColor">점선 한도: 30 → 180 MHz (LATENCY 0 → 5)</text>
  <rect x="380" y="130" width="340" height="80" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="390" y="150" fill="currentColor" font-weight="bold">C3 결함: SW 를 먼저, LATENCY 를 나중에</text>
  <text x="390" y="168" fill="currentColor">실선이 점선(30 MHz) 위로 → 쓰레기 읽기 199 회,</text>
  <text x="390" y="186" fill="currentColor">SWS 폴링이 오염된 값을 읽어 200 회 상한까지 돎.</text>
  <text x="390" y="204" fill="currentColor">끝 상태만 보면 정상 (SWS=PLL, LAT=5)!</text>
  <text x="70" y="288" fill="currentColor">내려올 때는 거울상: SW=HSI 먼저 → 분주 → LATENCY 0. 거꾸로면 C5 (쓰레기 204 회)</text>
</svg>
```

정답 실행의 Part C (실제 출력):

```text
== Part C: STM32F4 clock switch (RCC/FLASH model) ==
  pll_search(8 MHz -> 168 MHz): 5 solutions, best M=4 N=168 P=2 Q=7
  pll_search(25 MHz -> 168 MHz): 1 solutions, best M=25 N=336 P=2 Q=7
  C1 clock_up correct order       rc=OK SWS=2 HCLK=168MHz LAT=5 garbage=0 apb_viol=0 t=45
  C2 clock_down correct order     rc=OK SWS=0 HCLK=16MHz LAT=0 garbage=0 apb_viol=0 t=58
  C3 inject: LATENCY after SW     rc=OK SWS=2 HCLK=168MHz LAT=5 garbage=199 apb_viol=0 t=242
  C4 inject: APB prescaler after  rc=OK SWS=2 HCLK=168MHz LAT=5 garbage=0 apb_viol=6 t=45
  C5 inject: LATENCY down first   rc=OK SWS=0 HCLK=16MHz LAT=0 garbage=204 apb_viol=2 t=251
  C6 HSE broken                   rc=HSE SWS=0 HCLK=16MHz LAT=0 garbage=0 apb_viol=0 t=204
  C7 PLL never locks              rc=PLL SWS=0 HCLK=16MHz LAT=0 garbage=0 apb_viol=0 t=228
  C9 sweep HSE 4..26 MHz x 6 targets: runs=138 ok=138 no_pll=0 bad=0
[boot] (4) clock  C1 rc=OK: HSI 16 MHz -> PLL 168 MHz (M=4 N=168 P=2 Q=7), LATENCY ->5 at access #39, SW=PLL at #42, 45 model accesses
50/50 passed
qemu exit=0
```

(C3–C5 의 `rc=OK` 는 결함 주입판이 반환값을 버리기 때문에 표시용으로 넣은 것이다. 실제로 C3 의 `st_switch` 는 SWS 를 끝내 못 읽어 `CLK_ERR_SWITCH` 였다 — 쓰레기 읽기가 "드라이버 오동작" 으로 이어지는 모습.)

C9 의 `no_pll=0`: 정수 MHz 의 HSE 는 M = HSE / 1 MHz 로 언제나 VCO 입력 1 MHz 를 만들 수 있고, 여섯 목표 모두 P ∈ {2,4,6,8} 중 하나로 VCO 100–432 MHz 에 들어가서 해가 없는 조합이 없었다 (해가 없는 경우는 C8 이 따로 검사).

| 시험 | 무엇을 주입 | 모델이 본 것 | 판정 |
|---|---|---|---|
| C1 | 없음 (정답 순서) | LATENCY 쓰기 #39 < SW 쓰기 #42, 위반 0 | PASS |
| C2 | 없음 (내려오기) | SW→분주→LAT 0, PLL/HSE 꺼짐, 위반 0 | PASS |
| C3 | LATENCY 를 SW 뒤에 | garbage 199, t_sw < t_lat | 잡힘 |
| C4 | APB 분주를 SW 뒤에 | apb_viol 6 (APB1 이 잠깐 168 MHz) | 잡힘 |
| C5 | 내려올 때 LATENCY 먼저 | garbage 204, 오염된 RMW 로 분주 비트까지 망가져 apb_viol 2 | 잡힘 |
| C6 | HSE 안 뜸 | 200 폴링 후 포기, HSEON 원복, HSI·LAT 0 유지 (t=204) | PASS |
| C7 | PLL 안 잠김 | rc=PLL, SW 안 건드림 | PASS |
| C8 | 167.000001 MHz | 해 없음 → 레지스터 접근 0 | PASS |
| C9 | HSE 4..26 × {48,84,100,120,144,168} MHz | 138 번 올라갔다 내려와도 위반 0 | PASS |

C5 의 `apb_viol=2` 가 재미있다: LATENCY 를 먼저 내리면 그 순간부터 읽기가 오염되고, `st_switch` 의 **RMW (읽고-고치고-쓰기)** 가 오염된 CFGR 값을 다시 써 넣어 분주 비트까지 망가뜨렸다. 실칩에서 대기 상태가 부족할 때의 증상은 "명령/데이터를 잘못 읽음" 이고 그 결과는 이처럼 연쇄적이다 — 그래서 증상이 매번 다르다.

**4) 진짜 STM32F4 에서 더 있는 것 (이 모델 밖, 확인 권장).** (a) 전압 스케일: F405/407 은 PWR_CR.VOS 가 Scale 1 이어야 168 MHz, F42x/43x 는 180 MHz 에 Over-drive 모드 진입 절차가 있다. (b) 대기 상태 표는 VDD 범위마다 다르다 (1.8–2.1 V 면 훨씬 낮은 주파수에서 WS 가 늘어남). (c) ART 가속기 (ICEN/DCEN/PRFTEN) — 켜기 전에 캐시 리셋 비트로 비우는 절차. (d) HSE 실패 대비 CSS (Clock Security System) → NMI. (e) CMSIS `SystemCoreClock` 는 `.data` 변수라 **.data 복사 전에 SystemInit 이 쓰면 덮인다** — ST 의 CubeF4 `startup_stm32f4xx.s` 는 버전에 따라 `SystemInit` 을 .data 복사 앞에서 부르고, `SystemInit` 은 CPACR 켜기와 (옵션) VTOR 설정만 하며, 클럭 전환은 `main` 의 `SystemClock_Config()` 가 한다 (버전·파일 확인 권장). 그 구조가 이 레슨의 순서와 같은 의존성을 지킨다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

## 7. 자주 하는 실수

**실수 1 — CP10 만 켜기 (`CPACR |= 3 << 20`).** QEMU 에선 동작한다 (P9, QEMU 는 CP10 칸만 본다). 실칩에선 두 칸이 다르면 UNPREDICTABLE. 이 레슨의 변이 M1 은 **동작 시험으로는 안 잡히고** `CPACR == 0x00F00000` 레지스터 검사로만 잡혔다 — 시뮬레이터가 관대할 때는 레지스터 값을 직접 단언해야 한다는 예. 고침: `SCB_CPACR |= 0xF << 20;`

**실수 2 — 배리어 없이 바로 float.** `CPACR |= …; x = y * 2.0f;` — 실칩에서 바로 다음 FP 명령이 옛 권한으로 디코드될 수 있다. QEMU 로는 재현 불가. 고침: `DSB; ISB` (CMSIS `SystemInit` 도 그렇게 한다 — 확인 권장).

**실수 3 — "float 는 main 에서만 쓰니까" FPU 를 main 에서 켜기.** C++ static 생성자, `__attribute__((constructor))`, newlib 의 초기화, 하드 float ABI 의 인자 전달, 컴파일러의 S 레지스터 사용 — main 이전에 FP 명령이 나올 길은 많다 (O1: 첫 VFP 는 상수 0.25 로드). 고침: Reset_Handler 첫 줄 (또는 `.data` 복사 전에 불리는 SystemInit).

**실수 4 — RAM 벡터 테이블을 .bss 0 채우기 전에 채우기.** O2 → Lockup. 고침: 0 채우기 뒤에 복사하거나, 테이블을 `.bss` 밖의 전용 `NOLOAD` 섹션에 두고 순서를 문서화.

**실수 5 — 정렬을 128 B 로 고정.** `aligned(128)` 은 IRQ 16 개까지만 맞다. 48 칸이면 256, F407 이면 512. 고침: `_Static_assert(VTOR_ALIGN >= N_VECTORS*4)` + 2 의 거듭제곱 검사, 링커 스크립트면 `ALIGN(512)` 와 `ASSERT`.

**실수 6 — 클럭을 올리고 나서 LATENCY.** C3: 끝 상태가 정상이라 리뷰에서도 놓친다. 증상은 온도·전압·코드 위치(캐시 적중)에 따라 "가끔" 하드폴트. 고침: 올라갈 땐 LATENCY·분주 먼저, 다시 읽어 확인, 그 다음 SW. 내려올 땐 SW 먼저.

**실수 7 — 무한 대기 `while (!(RCC->CR & HSERDY));`.** 크리스털이 없거나 납땜 불량인 보드는 여기서 영원히 멈추고 워치독도 아직 안 켜져 있다. 고침: 상한 있는 대기 + 실패 시 HSI 로 계속 부팅 + 부팅 로그에 "HSE FAIL" (C6).

## 8. 면접 Q&A

<details>
<summary>Q1. main 전에 무슨 일이 일어나나요? 순서까지 말해 주세요.</summary>

코어: 벡터 0 에서 MSP, 벡터 1 에서 Reset_Handler 주소를 읽는다 (BM-B08). 펌웨어: (1) FPU 켜기 (CPACR + DSB/ISB) — 어떤 C 코드보다 먼저, (2) `.data` 를 플래시 LMA 에서 RAM 으로 복사, `.bss` 0, (3) 생성자 (`__libc_init_array`), (4) 필요하면 벡터 테이블을 RAM 으로 + VTOR, (5) 클럭: 대기 상태·분주 → PLL 로 스위치, `SystemCoreClock` 갱신, (6) main. 각 화살표 이유를 하나씩 대면 된다: 생성자는 FP·.data·.bss 를 쓴다, RAM 테이블은 .bss 안이다, 클럭 코드는 .data 변수를 쓴다. 이 레슨 기준 11 개 규칙을 만족하는 순서는 5040 중 66 개 — "유일한 정답" 이 아니라 "의존성 그래프의 위상 정렬" 이라고 말하면 시니어답다. 꼬리질문 "클럭을 맨 앞에 두는 회사도 있다?" → 부팅 시간(복사 루프를 168 MHz 로) 때문에 그렇게 한다. 그러면 클럭 코드는 .data/.bss 를 쓰면 안 되고 결과를 레지스터에서 다시 계산해야 한다 (`SystemCoreClockUpdate()`).

</details>

<details>
<summary>Q2. FPU 를 안 켜고 float 를 쓰면 정확히 무슨 일이 생기나요?</summary>

첫 FP 명령에서 NOCP (No Coprocessor) UsageFault — CFSR 0x00080000. 리셋 직후엔 SHCSR.USGFAULTENA=0 이라 **HardFault 로 승격** (HFSR 0x40000000 FORCED) — 이 레슨 P4/B2 실측. 쌓인 PC 는 그 FP 명령 (정밀, 실행 전). 그래서 HardFault 핸들러에서 CFSR 을 안 보고 "HardFault 났다" 만 기록하면 원인을 놓친다. 꼬리질문 "핸들러에서 CPACR 을 켜고 복귀하면?" → 같은 명령이 성공한다 (B1 `result=0x40400000`). 실제로 일부 OS 는 이걸 이용해 "FPU 첫 사용 시점" 을 잡는다. 하지만 Cortex-M 에선 하드웨어 지연 스태킹(ASPEN/LSPEN)이 그 역할을 하므로 보통 필요 없다.

</details>

<details>
<summary>Q3. ASPEN 과 LSPEN 은 뭐고, 예외 프레임 크기에 어떤 영향이 있나요?</summary>

FPCCR (0xE000EF34) 리셋값 0xC0000000 (P2). ASPEN=1: FP 명령을 실행하면 하드웨어가 CONTROL.FPCA=1 (P4 이후 CONTROL=0x4). FPCA=1 인 상태에서 예외가 나면 확장 프레임 104 B (8 워드 + S0–S15 + FPSCR + 예약) 를 잡고 EXC_RETURN bit4=0 (0xFFFFFFE9, B1/B6). LSPEN=1: 자리만 잡고 LSPACT=1, FPCAR = 그 자리 (P6b: 0x2000FF88) — 핸들러가 FP 를 쓸 때 실제 저장. 효과: FP 를 안 쓰는 ISR 의 지연이 거의 늘지 않는다. 꼬리질문 "스택 예산은?" → 최악은 104 B (+정렬 4) × 중첩 깊이로 잡아야 한다. 지연 스태킹은 시간만 아끼고 공간은 아끼지 않는다 (BM-A07 스택 HWM 과 연결).

</details>

<details>
<summary>Q4. VTOR 정렬 규칙은 무엇이고 왜 있나요? 어기면?</summary>

테이블 크기 이상의 2 의 거듭제곱으로, 최소 128 B. VTOR 하위 7 비트는 아예 저장되지 않아 (P7) 128 B 미만 어긋남은 즉시 엉뚱한 칸 (B4). 128 B 이상 어긋남은 아키텍처상 결과 미정 — QEMU 는 덧셈이라 동작하고 (B5), 비트 결합 구현이라면 exc ≥ 32 의 16 칸이 다른 칸과 겹친다 (연습 2). 규칙은 구현에 결합을 허락하기 위한 것으로 이해하면 된다 (확인 권장). 꼬리질문 "STM32F407 이면?" → 98 칸, 512 B 정렬. "링커 스크립트로는?" → `.ram_vectors (NOLOAD) : ALIGN(512)` + `ASSERT(. % 512 == 0)`.

</details>

<details>
<summary>Q5. 런타임에 인터럽트 핸들러를 바꾸는 건 안전한가요? 주의할 점은?</summary>

RAM 테이블에서 한 칸 = 정렬된 32 비트 쓰기 = 원자적이라, 예외 진입이 "반쯤 쓴 주소" 를 읽는 일은 없다. 주의: (1) 쓰기 후 DSB — 그래야 이어지는 pend/예외가 새 값을 본다. (2) 옛 핸들러가 지금 **실행 중** 일 수 있다 (선점·중첩) — 옛 핸들러가 쓰는 자원을 바로 해제하면 안 된다. 보통 그 IRQ 를 disable → 교체 → enable. (3) 새 핸들러가 기대하는 상태(페리페럴 설정, 컨텍스트 포인터)가 먼저 준비돼야 한다. (4) VTOR 이 RAM 이 아니면 교체가 조용히 무시된다 — 그래서 `vtor_set_handler` 가 거부하고 0 을 반환 (시작 코드 실측: `old hits=1`). 꼬리질문 "왜 함수 포인터 디스패치 대신 테이블 교체?" → 간접 호출 한 번이 없어 지연이 짧고, 부트로더/앱이 각자 테이블을 갖는 구조(BM-A03)의 기반.

</details>

<details>
<summary>Q6. 클럭을 168 MHz 로 올릴 때 플래시 대기 상태는 언제 바꾸나요? 내릴 때는?</summary>

올릴 때: **스위치 전에** 올리고 FLASH_ACR 을 다시 읽어 적용 확인. 내릴 때: **스위치 후에** 내린다. 규칙은 "어느 순간에도 HCLK ≤ (LATENCY+1)·30 MHz (2.7–3.6 V, RM0090 표 — 확인 권장)". 모델 C3: 거꾸로 하면 쓰레기 읽기 199 회 — 그런데 끝 상태는 정상. 그래서 이 버그는 레지스터 덤프로 못 찾고, 순서를 코드 리뷰하거나 이런 모델로 잡는다. APB 분주도 같은 원리 (C4: APB1 이 잠깐 168 MHz). 꼬리질문 "LATENCY 를 미리 최대로 두면?" → 안전하지만 저속 동작에서 성능·전력 손해. 부팅 중 잠깐이면 실무에서 흔한 단순화다.

</details>

<details>
<summary>Q7. HSE 크리스털이 안 뜨면 펌웨어는 어떻게 해야 하나요? (staff 꼬리: 이 설계가 틀리는 경우는?)</summary>

상한 있는 대기 → 실패면 HSEON 원복, HSI 로 계속 부팅, 부팅 로그·리셋 원인에 "HSE FAIL" 기록, 기능을 낮춰 동작 (USB 처럼 정확한 클럭이 필요한 기능은 끔). 무한 대기는 금지 (C6, POLL_MAX=200). **틀리는 경우**: (1) 폴링 횟수 상한은 "현재 CPU 클럭" 에 따라 실제 시간이 달라진다 — HSI 16 MHz 에서 200 회와 168 MHz 에서 200 회는 10 배 차이. 크리스털 시동은 ms 단위라 실제 코드는 SysTick/타이머 기반 시간 상한이 맞다 (모델은 접근 횟수로 단순화). (2) HSE 가 떴다가 나중에 죽는 경우는 부팅 코드가 못 잡는다 → CSS (Clock Security System) 가 NMI 를 내고 HSI 로 자동 전환, NMI 핸들러가 PLL 을 다시 구성. (3) HSE 가 "틀린 주파수" 로 뜨면(잘못된 크리스털 장착) HSERDY 는 정상 — 타이머로 HSE 를 LSI/HSI 와 비교 측정해야 잡힌다.

</details>

<details>
<summary>Q8. 부트로더가 있는 제품이라면 이 순서를 어떻게 바꾸겠나요? (staff)</summary>

부트로더와 앱이 각자 Reset_Handler 를 가진다. 바꿀 점: (1) **클럭은 누가?** 부트로더가 PLL 을 올린 채 점프하면 앱의 클럭 코드는 "HSI 리셋 상태" 를 가정하면 안 된다 — 앱은 PLL 이 이미 켜져 있으면 `st_pll_config` 가 거부된다 (PLL 켜진 채 설정 무시, 모델의 `cfg_while_on`). 계약을 정한다: 부트로더가 클럭을 리셋 상태로 되돌리고 점프하거나(가장 단순), 앱이 현재 상태를 읽어 판단. (2) **VTOR**: 앱은 자기 테이블 주소로 VTOR 을 다시 쓴다 (부트로더가 해 줘도 앱이 확인). (3) **FPU**: 부트로더가 FP 를 썼다면 CONTROL.FPCA 가 1 일 수 있다 — 점프 전에 정리 (BM-A03 의 점프 체크리스트). (4) CPACR 은 앱도 다시 켠다 (멱등). 자세한 점프 절차는 BM-A03.

</details>

<details>
<summary>Q9. QEMU 로 검증한 이 부팅 코드를 실보드에 올릴 때 무엇을 다시 봐야 하나요? (staff)</summary>

QEMU 가 관대하거나 다르게 모델링하는 곳: (1) 배리어 — QEMU 는 DSB/ISB 누락을 드러내지 않는다. (2) CPACR CP11 — QEMU 는 CP10 만 본다. (3) VTOR 정렬 위반 — QEMU 는 덧셈. (4) 타이밍 — 플래시 대기 상태·PLL 잠금 시간·크리스털 시동은 QEMU 에 없다 (그래서 Part C 는 모델). (5) 메모리 — mps2 의 "플래시" 는 쓰기 가능한 SSRAM 이라 플래시에 쓰는 버그가 숨는다. 대응: 실보드 첫 부팅에서 부팅 로그 4 줄 (CPACR, .data/.bss 크기, VTOR, 클럭 결과 + 측정한 실제 SYSCLK — MCO 핀을 스코프로) 을 찍고, HSE 를 뽑은 보드로 C6 경로를 실제로 한 번 태워 본다. 그리고 이 레슨의 변이(M1 CP10 만)가 보여 주듯 "동작했다" 보다 "레지스터 값이 기대와 같다" 를 단언한다.

</details>

## 9. 추가 연습

`practice.c` 는 **호스트 C** 다 (Mac `cc` 로, 또는 VM 에서 `run_qemu.sh practice`). 펌웨어 없이 이 레슨의 "규칙" 부분만 전수 검사한다.

### 연습 1 — `cpacr_set_fp(old, mode)`

CP10/CP11 두 칸을 같은 `mode` (0 거부, 1 특권, 3 전부) 로, 다른 비트 보존, 예약값 2 는 거부(원래 값 반환).

<details>
<summary>답</summary>

`(old & ~(0xF << 20)) | (mode << 20) | (mode << 22)`. 검사: FULL = 0x00F00000, PRIV = 0x00500000, 다른 칸 보존, xorshift32 4000 값 × 3 모드 = 12000 경우에서 다른 비트 변화 0, 두 칸 불일치 0.

</details>

### 연습 2 — `vtor_align_bytes(n_irq)` 와 덧셈 vs 결합

정렬 = max(128, (16+n)·4 이상의 2 의 거듭제곱). 그리고 1 KB 창 안의 128 B 단위 시작점마다 `VTOR + 4e` 와 `VTOR | 4e` 가 다른 칸 수를 세라.

<details>
<summary>답</summary>

0 → 128, 16 → 128, 17 → 256, 32 → 256, 82 → 512, 240 → 1024. 실제 출력: 256 B 정렬 시작점은 불일치 0, 0x80 어긋난 시작점은 **16/48** (예외 32–47 의 오프셋 0x80–0xBC 가 VTOR 의 bit7 과 겹침). B5 의 `0x380 | 0xBC = 0x3BC = 표[15]` 도 검사.

</details>

### 연습 3 — `boot_order_ok()`: 위상 정렬 검사기

7 단계 {FPU, DATA, BSS, CTORS, VTOR, CLOCK, MAIN} 와 11 개 "앞서야 함" 규칙. 위반이면 어느 규칙인지 반환. 5040 순열 전수.

<details>
<summary>답</summary>

위치 배열을 만들고 규칙마다 `pos[a] < pos[b]` 확인. 이 레슨 순서 OK, O1 (FPU 늦게) → 규칙 FPU<CTORS, O2 → BSS<VTOR, 클럭 맨 앞 → DATA<CLOCK (SystemCoreClock 덮임). 중복·길이 오류 거부. 전수: **66 / 5040**.

</details>

### 연습 4 — `exc_frame_bytes(fpca, sp, &new_sp)`

기본 32 B, 확장 104 B, 진입 SP 가 8 의 배수가 아니면 4 B 패딩 (STKALIGN). 결과 SP 는 항상 8 정렬.

<details>
<summary>답</summary>

`sz = fpca ? 104 : 32; pad = (sp & 4) ? 4 : 0; new_sp = sp − sz − pad`. 0x20010000 → 0x2000FFE0 / 0x2000FF98, 0x2000FFFC → 36 B / 108 B. P6b 의 FPCAR 0x2000FF88 = 프레임 시작 0x2000FF68 + 0x20 (FP 영역은 기본 8 워드 뒤). 128 개 SP 모두 8 정렬.

</details>

### 연습 5 — `clk_transition(from, to)` 전수

{16, 24, 30, 31, 48, 60, 61, 84, 90, 100, 120, 144, 150, 168} MHz 의 모든 쌍에서 "올라가면 LATENCY 먼저, 내려가면 SW 먼저" 가 안전한지, 그리고 WS 가 바뀌는 쌍에서 순서를 뒤집으면 **반드시** 위반인지.

<details>
<summary>답</summary>

중간 상태 두 개를 시뮬레이션: 182 쌍 모두 규칙대로면 안전, WS 가 바뀌는 160 쌍은 뒤집으면 160 개 전부 위반 (WS 가 같은 22 쌍은 순서 무관). 경계: 30 MHz → WS 0, 31 MHz → WS 1.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-A01_pr && /tmp/BM-A01_pr    # 39/39 passed
# VM 에서: limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"
```

실행 결과 (실제 출력):

```text
ex1 CPACR: FULL=0x00F00000 PRIV=0x00500000, 12000 random RMW cases bad=0
ex2 base   mismatching vectors (of 48) if HW concatenates instead of adds
    0x20000400  0 (aligned)
    0x20000480 16 
    0x20000500  0 (aligned)
    0x20000580 16 
    0x20000600  0 (aligned)
    0x20000680 16 
    0x20000700  0 (aligned)
    0x20000780 16 
ex3 boot orders: 66 of 5040 permutations satisfy the 11 rules
ex4 frame: basic 32 B, extended 104 B (+4 pad when SP%8==4), 128 SPs all 8-aligned after push
ex5 transitions: 182 pairs, rule-safe 182; WS changes in 160, reversed order unsafe in 160
39/39 passed
```

실행 스크립트와 QEMU 확인 펌웨어 전체:

<!-- code: run_qemu.sh -->

<!-- code: demo_probe.c -->

## 10. 핵심 정리

- 순서 = 의존성: **FPU(CPACR) → .data/.bss → 생성자 → RAM 벡터 테이블 + VTOR → 클럭 → main**. 생성자는 FP·.data·.bss 를, RAM 테이블은 .bss 를, 클럭 코드는 .data 를 쓴다.
- CPACR (0xE000ED88) `|= 0xF << 20` (CP10 [21:20] + CP11 [23:22] = 11) + DSB + ISB. 리셋값 0. QEMU 는 CP10 만 보지만 실칩 규칙대로 둘 다.
- FPU 꺼진 채 FP 명령 → NOCP (CFSR 0x00080000). UsageFault 꺼져 있으면(리셋 기본) HardFault FORCED (HFSR 0x40000000). 쌓인 PC = 그 명령. 첫 VFP 는 상수 로드일 수도 있다.
- FPCCR 리셋 ASPEN|LSPEN = 0xC0000000 → FP 사용 후 예외는 EXC_RETURN 0xFFFFFFE9, 확장 프레임 104 B (지연 저장: 시간은 아끼고 공간은 아니다).
- VTOR: 하위 7 비트 저장 안 됨, 정렬 = max(128, 2 의 거듭제곱 ≥ (16+IRQ)·4) — mps2 48 칸 → 256 B, F407 → 512 B. QEMU 는 덧셈이라 정렬 위반이 숨는다.
- 핸들러 교체: RAM 테이블 한 워드 + DSB. VTOR 이 RAM 이 아니면 거부.
- 클럭: 올라갈 땐 분주·LATENCY(다시 읽어 확인) → SW, 내려올 땐 SW → 분주·LATENCY. 모든 대기는 상한, HSE 실패면 HSI 로 계속.
- 끝 상태가 정상이어도 "전환 창" 이 틀릴 수 있다 — 레지스터 값 단언 + 순서 모델로 검증.

## 참고자료

- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403E) — B3.2.5 VTOR (정렬 규칙), B3.2.20 CPACR, B3.2.15 CFSR/UFSR (NOCP), B1.5.7 예외 진입·확장 프레임, B3.2 의 FPCCR/FPCAR (절 번호 모두 확인 권장).
- Arm, *Cortex-M4 Devices Generic User Guide* (DUI 0553) — 4.6 Floating Point Unit (CPACR, FPCCR, "Enabling the FPU" 예제 코드), 2.3.7 Exception entry and return.
- Arm Application Note 298, *Cortex-M4(F) Lazy Stacking and Context Switching* (확인 권장); Application Note 321, *ARM Cortex-M Programming Guide to Memory Barrier Instructions* (CPACR·VTOR 변경 후 DSB/ISB).
- STMicroelectronics RM0090 (STM32F405/407/415/417/427/437/429/439) — 6 RCC (CR, PLLCFGR, CFGR, 클럭 스위치 절차), 3 Embedded Flash (FLASH_ACR, 표 "Number of wait states according to CPU clock frequency"), 5 PWR (VOS) — 모두 확인 권장. AN3988 Clock configuration tool (참고).
- Arm CMSIS 5 `Device/ARM/ARMCM4/Source/system_ARMCM4.c` (`SystemInit`: `__FPU_USED` 이면 CPACR, VTOR 설정), ST CubeF4 `startup_stm32f407xx.s` / `system_stm32f4xx.c` (`SystemInit` 호출 위치는 버전마다 다름 — 확인 권장).
- QEMU v10.2.1 소스 (실제로 읽음): `hw/arm/mps2.c` (AN386 = Cortex-M4, `num-irq` 32, SSRAM1 @0x0 4 MB), `hw/intc/armv7m_nvic.c` (VTOR `value & 0xffffff80`, CPACR `value & (0xf << 20)`), `target/arm/tcg/m_helper.c` (`arm_v7m_load_vector`: `vecbase + exc * 4`; FP 스태킹/언스태킹 NOCP), `target/arm/internals.h` (`v7m_cpacr_pass`: CP10 칸만), `target/arm/cpu.c` (FPCCR 리셋 ASPEN|LSPEN) — https://gitlab.com/qemu-project/qemu/-/tree/v10.2.1
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* (3rd ed.) — 13 장 Floating point (lazy stacking), 7 장 벡터 테이블 재배치.
- 관련 레슨: BM-B08 (C startup, 첫 QEMU 부팅, Lockup), BM-B07 (crt0 호스트 시뮬레이션), BM-B10 (PLL 계산기 전체판), BM-B02 (mock 프레임워크), BM-M13 (fault 정책 + 리셋 기록), BM-M02 (우선순위, mps2 IRQ 배선), BM-A03 (부트로더 → 앱 점프: VTOR/MSP/주변장치 정리), BM-A07 (스택 HWM — 확장 프레임 104 B), ARM-M04/M05. 다음 단계: ARM-M08 (asm 벡터 테이블), ARM-M12 (CFSR/HFSR 디코더 + 언와인딩), TOOL-A07 (고급 링커 스크립트: 정렬된 RAM 벡터 섹션).
