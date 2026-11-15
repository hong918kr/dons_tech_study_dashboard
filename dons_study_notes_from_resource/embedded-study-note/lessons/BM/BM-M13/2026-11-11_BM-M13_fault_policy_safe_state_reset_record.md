---
id: BM-M13
time: 90분
updated: 2026-11-11
---
# BM-M13 Fault 처리 정책: 세분 fault 활성화와 안전 상태 진입

## 학습 목표

- SHCSR 의 MEMFAULTENA/BUSFAULTENA/USGFAULTENA 와 CCR 의 DIV_0_TRP/UNALIGN_TRP 가 각각 무엇을 바꾸는지, 끄면 fault 가 **어디로 가는지(HardFault FORCED)** 또는 **아예 안 나는지(몫 0, 비정렬 허용)** 를 실측 레지스터 값으로 말할 수 있다.
- fault 핸들러의 순서 — **출력 안전 상태 → 기록(내용 → 체크값 → magic) → (로그) → SYSRESETREQ** — 를 지키고, 각 순서가 바뀌면 무엇을 잃는지 설명할 수 있다.
- 리셋을 건너 살아남는 `.noinit` (NOLOAD) 기록을 링커 스크립트로 만들고, QEMU 가 SYSRESETREQ 때 **실제로** 무엇을 지우고 무엇을 남기는지(ELF 로드 세그먼트는 다시 쓰고, NOLOAD 는 그대로) 직접 확인한다.
- 승격(escalation) 두 가지 — 핸들러가 꺼져 있을 때, 그리고 **켜져 있어도 우선순위가 모자랄 때(같은 우선순위 IRQ 안의 fault)** — 를 재현하고 고친다.
- 다음 부팅에서 리셋 사유(reset cause)를 보고하고, 리셋 루프를 막는 정책(연속 fault N 번 → SAFE 모드)을 설계한다.

## 1. 문제

모터·히터·RF PA 처럼 **켜진 채로 멈추면 위험한 출력**을 가진 보드에서 면접관이 묻는 건 "HardFault 를 디코드할 줄 아나요?" 다음 단계다: **"fault 가 나면 시스템은 어떤 상태가 되어야 하고, 리부팅 뒤엔 무엇을 알 수 있어야 하나요?"** Don 이 SSD 컨트롤러에서 assert/panic 덤프를 NAND 에 남기고 재부팅하던 것과 같은 문제를 Cortex-M 의 fault 하드웨어로 푸는 것이다. 이 레슨은 진짜 Cortex-M3 (QEMU `mps2-an385`) 에서 fault 7 가지를 **한 부팅에 하나씩** 일으키고, 핸들러가 출력을 끄고 기록을 남긴 뒤 리셋하고, 다음 부팅이 그 기록을 보고하는 것을 8 번의 부팅으로 끝까지 돌린다. CFSR 비트 전체 해독과 PC 역추적은 ARM-M12 (다음 단계) 의 몫이고, 여기서는 **정책(policy)** 이 주인공이다.

fault 가 어느 핸들러로 가는지는 세 가지 질문으로 정해진다 (ARMv7-M ARM B1.5 의 승격 규칙 — 절 번호 확인 권장). 그림의 숫자는 이 레슨의 실측값이다.

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">fault 는 어디로 가나 (QEMU mps2-an385 실측 CFSR/HFSR)</text>
  <rect x="10" y="34" width="190" height="52" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor">명령이 문제를 일으킴</text>
  <text x="20" y="72" fill="currentColor">UDIV/0 · 비정렬 LDR · UDF · …</text>
  <path d="M200 60 L240 60" stroke="currentColor"/><path d="M240 60 l-8 -4 l0 8 z" fill="currentColor"/>
  <rect x="240" y="34" width="210" height="52" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="250" y="54" fill="currentColor">① 트랩이 켜져 있나? (CCR)</text>
  <text x="250" y="72" fill="currentColor">DIV_0_TRP bit4, UNALIGN_TRP bit3</text>
  <path d="M450 60 L490 60" stroke="currentColor"/><path d="M490 60 l-8 -4 l0 8 z" fill="currentColor"/>
  <text x="456" y="52" fill="currentColor">아니오</text>
  <rect x="490" y="34" width="240" height="52" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="500" y="54" fill="currentColor">fault 없음: 7/0 → 0,</text>
  <text x="500" y="72" fill="currentColor">LDR 홀수 → 0x44332211</text>
  <path d="M345 86 L345 116" stroke="currentColor"/><path d="M345 116 l-4 -8 l8 0 z" fill="currentColor"/>
  <text x="352" y="106" fill="currentColor">예 (또는 BusFault·MPU·UDF·LDRD 홀수 = 항상)</text>
  <rect x="240" y="116" width="210" height="52" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="250" y="136" fill="currentColor">② 그 핸들러가 켜져 있나?</text>
  <text x="250" y="154" fill="currentColor">SHCSR bit16 Mem / 17 Bus / 18 Usage</text>
  <path d="M345 168 L345 198" stroke="currentColor"/><path d="M345 198 l-4 -8 l8 0 z" fill="currentColor"/>
  <text x="352" y="188" fill="currentColor">예</text>
  <rect x="240" y="198" width="210" height="52" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="250" y="218" fill="currentColor">③ 지금 실행 중인 것보다</text>
  <text x="250" y="236" fill="currentColor">우선순위가 높나? (SHPR1, 리셋 0)</text>
  <path d="M345 250 L345 280" stroke="currentColor"/><path d="M345 280 l-4 -8 l8 0 z" fill="currentColor"/>
  <text x="352" y="270" fill="currentColor">예</text>
  <rect x="200" y="280" width="300" height="58" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="210" y="300" fill="currentColor">전용 핸들러: Usage 6 / Bus 5 / MemManage 4</text>
  <text x="210" y="318" fill="currentColor">CFSR 0x02000000 DIVBYZERO, 0x00008200</text>
  <text x="210" y="333" fill="currentColor">PRECISERR|BFARVALID, 0x82 DACC|MMARVALID</text>
  <path d="M240 142 L120 142 L120 280" stroke="currentColor" fill="none"/>
  <path d="M240 224 L120 224" stroke="currentColor" fill="none"/>
  <text x="130" y="136" fill="currentColor">아니오</text><text x="130" y="218" fill="currentColor">아니오</text>
  <path d="M120 280 l-4 -8 l8 0 z" fill="currentColor"/>
  <rect x="10" y="280" width="180" height="58" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="300" fill="currentColor" font-weight="bold">HardFault (3), 승격</text>
  <text x="20" y="318" fill="currentColor">HFSR = 0x40000000 FORCED</text>
  <text x="20" y="333" fill="currentColor">CFSR 는 원래 원인을 그대로</text>
  <path d="M100 338 L100 366" stroke="currentColor"/><path d="M100 366 l-4 -8 l8 0 z" fill="currentColor"/>
  <rect x="10" y="366" width="720" height="28" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="385" fill="currentColor">HardFault 안에서 또 fault → Lockup (QEMU: "Lockup: can't escalate", exit 134 — BM-B08 F3)</text>
  <text x="515" y="300" fill="currentColor">원인 비트는 CFSR,</text>
  <text x="515" y="318" fill="currentColor">"누가 받았나" 는 IPSR,</text>
  <text x="515" y="336" fill="currentColor">"승격됐나" 는 HFSR.FORCED</text>
</svg>
```

### Part A — fault 세분 활성화와 핸들러 하나의 정책

- 주어진 것: `m13.ld` (FLASH 256 KB / RAM 63 KB / **NOINIT 1 KB @0x2000FC00**), startup (BM-B08 축약판), 네 개의 fault 벡터가 모두 `Fault_Entry` (naked: MSP/PSP 고르기 + IPSR) → `fault_policy(frame, ipsr)` 로 가는 구조, 기록 구조체 `crash_rec_t`, 주입 함수 7 개 (각 fault 명령에 전역 라벨 `pc_div0` … `pc_isr`).
- **TODO 1** `fault_policy_enable()`: SHCSR 세 비트 + CCR 두 비트, DSB/ISB.
- **TODO 2** `safe_outputs()`: 출력 레지스터(mps2 FPGAIO LED0 = 모의 MOTOR_EN | HEATER_EN)에 안전값 0 을 쓰고, DSB 후 **다시 읽어** 돌려준다.
- **TODO 3** `fault_address()`: BFAR/MMFAR 는 CFSR 의 BFARVALID/MMARVALID 가 1 일 때만 기록 (아니면 0).

### Part B — 승격과 함정: 무엇이 HardFault 로 가고, 무엇이 아예 안 나나

- 시작 코드(TODO 그대로)로 8 부팅을 돌리면: DIV0·UNALIGNED 는 **fault 가 안 나고**, 나머지 다섯은 전부 **HardFault (FORCED)** 로 들어오며, 출력은 켜진 채(0x3) 기록된다 → 23 개 FAIL, `qemu exit=1`.
- 정답에서도 일부러 HardFault 로 가는 두 경우: `ESC_OFF` (USGFAULTENA 를 끄고 UDF), `ESC_PRIO` (**우선순위 0 IRQ 안에서 UDF** — UsageFault 도 우선순위 0 이라 선점 못 함). 그리고 그 고침(IRQ 우선순위 0x80 → UsageFault)을 `demo_probe.c` 에서 실측.
- QEMU 사실 확인 (`run_qemu.sh probe`): 어떤 주소가 진짜 BusFault 를 내나, BFAR 의 옛 값, 트랩 on/off 의 결과, LDRD 는 항상 fault.

### Part C — 리셋을 건너는 기록, 다음 부팅의 보고, 리셋 루프

- **먼저 QEMU 가 SYSRESETREQ 에서 하는 일을 실측**: 머신 리셋인가? RAM 은 남나? `-kernel` 이 ELF 를 다시 써서 기록을 덮나? `-no-reboot` 면? 그 사실 위에 실행 설계를 올린다.
- 다음 부팅의 `main` 이 기록을 검증(magic + FNV-1a)하고 보고한 뒤 무효화, 다음 fault 로 넘어간다. 마지막 부팅이 요약 표 + 62 개 검사 → `SYS_EXIT 0`. 최대 부팅 수 가드 + `timeout 20`.
- 결함 주입: 기록을 `.noinit` 대신 `.bss` 에 두면 (`run_qemu.sh bssbug`) — 3 초 동안 **수천 번** 콜드 부팅하는 리셋 루프.

## 2. Problem (English)

> **Fault policy for a Cortex-M3 controlling a motor and a heater.**
>
> Target: QEMU `mps2-an385`. Outputs are modelled by FPGAIO `LED0` at `0x40028000` (bit0 MOTOR_EN, bit1 HEATER_EN; safe = 0).
>
> 1. `void fault_policy_enable(void)`: enable MemManage, BusFault and UsageFault in `SCB->SHCSR` (bits 16/17/18) without disturbing other bits, and enable `DIV_0_TRP` (bit 4) and `UNALIGN_TRP` (bit 3) in `SCB->CCR`. Barrier afterwards.
> 2. All four fault vectors go to one naked entry that passes the stacked frame (MSP or PSP by `EXC_RETURN[2]`) and `IPSR` to `fault_policy(frame, ipsr)`, which must, in this order: (a) drive outputs to the safe state and read them back (`uint32_t safe_outputs(void)`), (b) write a crash record `{reason=IPSR, CFSR, HFSR, BFAR (only if BFARVALID), MMFAR (only if MMARVALID), stacked LR/PC/xPSR, outputs read-back}` into a `.noinit` RAM struct, check value first and `magic` last, (c) request a system reset with `AIRCR = 0x05FA<<16 | PRIGROUP | SYSRESETREQ`.
> 3. On the next boot, validate the record (magic + FNV-1a), report the reset cause, invalidate the record, and inject the next fault. Faults, one per boot: UDIV by zero, word LDR from an odd address, LDR from an unmapped address, UDF, STR into a read-only MPU region, UDF with UsageFault disabled, UDF inside a priority-0 IRQ handler.
>
> Expected (QEMU): `DIV0 → UsageFault, CFSR=0x02000000`; `BUS → BusFault, CFSR=0x00008200, BFAR=0x30000000`; `MPU → MemManage, CFSR=0x00000082, MMFAR=&guard`; both escalation cases `→ HardFault, HFSR=0x40000000, CFSR=0x00010000`; stacked PC equals the address of the faulting instruction in every case; outputs read back 0.
>
> Follow-ups: What happens to the same seven faults if you skip step 1? Why must the outputs be made safe *before* anything else? Why is `magic` written last? Why can't the record live in `.bss`? What does QEMU's `-no-reboot` do to your test? How do you avoid an infinite fault-reset loop in the field?

## 3. 시작 코드

<!-- code: starter.c -->

같이 쓰는 파일: `m13.ld` (링커 스크립트), `run_qemu.sh` (VM 빌드·실행), `demo_probe.c` (QEMU 사실 확인 펌웨어), `practice.c` (호스트 C).

<!-- code: m13.ld -->

실행은 Lima VM `elinux` 안에서 (Mac 홈은 VM 에서 읽기 전용 → 스크립트가 `/tmp/BM-M13_work` 로 복사해 빌드). 여러 번 리셋해도 **QEMU 프로세스는 하나**이고, 마지막 부팅의 `SYS_EXIT` 로 끝난다 (`timeout 20` 안전망). Mac 에서:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-M13
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # 39/62, qemu exit=1
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"   # 62/62, qemu exit=0 (8 부팅, 0.04 s)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh probe"      # QEMU 사실 확인 (P1~P7)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh syms"       # 라벨 주소, .noinit 배치, ELF 세그먼트
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh noreboot"   # -no-reboot 이면?
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh bssbug"     # 기록을 .bss 에 두면? (리셋 루프)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # practice.c (호스트 C)
```

시작 코드 실행 결과 (실제 출력, 부팅 5~8 의 줄은 생략 표시):

```text
   text	   data	    bss	    dec	    hex	filename
   5600	      8	    440	   6048	   17a0	starter.elf
cold boot: no valid plan in .noinit @0x2000fc00
== boot #1 ==  CFSR=0x00000000 HFSR=0x00000000 LED0=0x00000000 SHCSR=0x00000000 -> 0x00000000
  inject 0: DIV0      (outputs=0x00000003)
  no fault happened! (trap not enabled?) -> record reason=none, reset
== boot #2 ==  CFSR=0x00000000 HFSR=0x00000000 LED0=0x00000000 SHCSR=0x00000000 -> 0x00000000
  reset cause: SOFTWARE (injected fault did not trap)  CFSR=0x00000000 [ ]
    pc=0xffffffff lr=0xffffffff outputs=0x00000003
  inject 1: UNALIGNED (outputs=0x00000003)
  no fault happened! (trap not enabled?) -> record reason=none, reset
== boot #3 ==  CFSR=0x00000000 HFSR=0x00000000 LED0=0x00000000 SHCSR=0x00000000 -> 0x00000000
  reset cause: SOFTWARE (injected fault did not trap)  CFSR=0x00000000 [ ]
    pc=0xffffffff lr=0xffffffff outputs=0x00000003
  inject 2: BUSFAULT  (outputs=0x00000003)
  [fault] HardFault  CFSR=0x00008200 -> outputs=0x00000003, record sealed, SYSRESETREQ
== boot #4 ==  CFSR=0x00000000 HFSR=0x00000000 LED0=0x00000000 SHCSR=0x00000000 -> 0x00000000
  reset cause: FAULT HardFault (FORCED) CFSR=0x00008200 [ PRECISERR BFARVALID ]
    pc=0x00000296 lr=0x00000e5d BFAR=0x00000000 outputs=0x00000003
  … (부팅 5~8 생략: UNDEF / MPU_GUARD / ESC_OFF / ESC_PRIO 도 모두 HardFault (FORCED)) …
== reset-cause summary (QEMU mps2-an385) ==
step fault     handler    HFSR       CFSR       fault-addr stacked-pc expect-pc  out
0    DIV0      none       0x00000000 0x00000000 0x00000000 0xffffffff 0x0000028a 0x00000003
1    UNALIGNED none       0x00000000 0x00000000 0x00000000 0xffffffff 0x00000292 0x00000003
2    BUSFAULT  HardFault  0x40000000 0x00008200 0x00000000 0x00000296 0x00000296 0x00000003
3    UNDEF     HardFault  0x40000000 0x00010000 0x00000000 0x0000029c 0x0000029c 0x00000003
4    MPU_GUARD HardFault  0x40000000 0x00000082 0x00000000 0x000002c8 0x000002c8 0x00000003
5    ESC_OFF   HardFault  0x40000000 0x00010000 0x00000000 0x000002ec 0x000002ec 0x00000003
6    ESC_PRIO  HardFault  0x40000000 0x00010000 0x00000000 0x000002f0 0x000002f0 0x00000003
FAIL starter.c:364  r->reason == e.exc  [step 0]
FAIL starter.c:365  r->cfsr == e.cfsr  [step 0]
… (FAIL 23 줄 중 19 줄 생략) …
FAIL starter.c:376  (SCB_SHCSR & (7u << 16)) == (7u << 16)  [step 7]
FAIL starter.c:377  (SCB_CCR & (CCR_DIV_0_TRP | CCR_UNALIGN_TRP)) == (CCR_DIV_0_TRP | CCR_UNALIGN_TRP)  [step 7]
39/62 passed
qemu exit=1
```

읽는 법: 실행 틀(리셋 → 기록 → 다음 부팅)은 이미 돈다. 그런데 **출력이 0x3 (모터·히터 켜짐) 인 채로** 리셋되고, 세분 핸들러는 한 번도 불리지 않았다. 스텁 BFAR 이 0 이라 "어느 주소에서 BusFault 났나" 도 잃었다 — CFSR 에는 BFARVALID 가 서 있는데도.

## 4. 힌트

<details>
<summary>힌트 1 — SHCSR 은 RMW, 그리고 배리어</summary>

SHCSR 에는 enable 비트 말고도 "지금 활성/펜딩" 상태 비트(MEMFAULTACT 등)가 같이 있다. 통째로 쓰지 말고 `|=` 로 세 비트만 켠다. CCR 도 리셋값 0x200 (STKALIGN, QEMU 실측) 을 보존해야 하므로 `|=`. 시스템 제어 레지스터를 바꾼 뒤엔 `DSB; ISB` 로 "다음 명령부터 확실히 적용" 을 보장한다.

</details>

<details>
<summary>힌트 2 — 안전 상태는 "썼다" 가 아니라 "확인했다"</summary>

`LED0 = 0;` 한 줄은 버스에 쓰기를 **보냈다**는 뜻일 뿐이다. 쓰기 버퍼가 있는 코어에서 바로 리셋을 걸면 이론상 그 쓰기가 끝나기 전일 수 있다. `DSB` 로 끝을 기다리고, 다시 읽어서 기록에 남긴다 (기록의 `outputs` 칸 = "정말 꺼졌다" 는 증거).

</details>

<details>
<summary>힌트 3 — BFAR/MMFAR 는 VALID 비트가 있을 때만</summary>

`run_qemu.sh probe` 의 P4: CFSR 를 지운 뒤에도 BFAR 에는 **예전 주소 0x30000000 이 그대로** 남아 있다. VALID 비트 없이 BFAR 을 기록하면 이전 fault 의 주소를 이번 fault 의 원인으로 오해한다. `CFSR_BFARVALID` (bit15), `CFSR_MMARVALID` (bit7).

</details>

<details>
<summary>힌트 4 — 왜 리셋까지 하나</summary>

fault 핸들러에서 "복귀" 하면 같은 명령을 다시 실행해 같은 fault 가 무한 반복되거나(정밀 fault), 망가진 상태로 계속 돈다. 원인 명령을 건너뛰는 복귀는 `demo_probe.c` 같은 **진단용 탐침**에서만 쓴다. 제품 정책 = 안전 상태 + 기록 + 리셋, 그리고 리셋 루프 가드 (연습 4).

</details>

## 5. 풀이

### Part A — 레지스터와 정책 하나

**쓰는 레지스터** (ARMv7-M ARM / Cortex-M3 Generic User Guide 기준 — 확인 권장. 시험의 진실은 QEMU 실측값):

| 레지스터 | 주소 | 이 레슨에서 쓰는 비트 | 접근 |
|---|---|---|---|
| SHCSR | 0xE000ED24 | MEMFAULTENA 16, BUSFAULTENA 17, USGFAULTENA 18 | rw (RMW 로) |
| CCR | 0xE000ED14 | UNALIGN_TRP 3, DIV_0_TRP 4 (STKALIGN 9 = 리셋 1) | rw |
| CFSR | 0xE000ED28 | MMFSR [7:0]: DACCVIOL 1, MMARVALID 7 · BFSR [15:8]: PRECISERR 9, BFARVALID 15 · UFSR [31:16]: UNDEFINSTR 16, UNALIGNED 24, DIVBYZERO 25 | rc_w1 (W1C, sticky) |
| HFSR | 0xE000ED2C | FORCED 30 (VECTTBL 1, DEBUGEVT 31) | rc_w1 |
| MMFAR / BFAR | 0xE000ED34 / 38 | 주소 (VALID 비트가 있을 때만 의미) | rw |
| AIRCR | 0xE000ED0C | VECTKEY 0x05FA [31:16] 쓰기, PRIGROUP [10:8] 보존, SYSRESETREQ 2 | 읽으면 0xFA05xxxx |
| MPU RNR/RBAR/RASR/CTRL | 0xE000ED98/9C/A0/94 | 영역 0, AP=110 (RO), SIZE=4 (32 B), XN, PRIVDEFENA | rw |
| FPGAIO LED0 | 0x40028000 | bit0, bit1 (QEMU: 2 비트만 저장) | rw |

`FPGAIO LED0` 주소는 기억이 아니라 QEMU 에서 확인했다: `info mtree -f` 에 `0000000040028000-0000000040028fff (prio 0, i/o): mps2-fpgaio` 가 있고, `demo_probe.c` 에서 0xFF 를 쓰면 `0x00000003` 이 읽힌다 (2 비트 레지스터).

**정답 실행 결과** (실제 출력 전체):

```text
   text	   data	    bss	    dec	    hex	filename
   5680	      8	    440	   6128	   17f0	solution.elf
cold boot: no valid plan in .noinit @0x2000fc00
== boot #1 ==  CFSR=0x00000000 HFSR=0x00000000 LED0=0x00000000 SHCSR=0x00000000 -> 0x00070000
  inject 0: DIV0      (outputs=0x00000003)
  [fault] UsageFault CFSR=0x02000000 -> outputs=0x00000000, record sealed, SYSRESETREQ
== boot #2 ==  CFSR=0x00000000 HFSR=0x00000000 LED0=0x00000000 SHCSR=0x00000000 -> 0x00070000
  reset cause: FAULT UsageFault CFSR=0x02000000 [ DIVBYZERO ]
    pc=0x000002ae lr=0x00000eb9 outputs=0x00000000
  inject 1: UNALIGNED (outputs=0x00000003)
  [fault] UsageFault CFSR=0x01000000 -> outputs=0x00000000, record sealed, SYSRESETREQ
== boot #3 ==  CFSR=0x00000000 HFSR=0x00000000 LED0=0x00000000 SHCSR=0x00000000 -> 0x00070000
  reset cause: FAULT UsageFault CFSR=0x01000000 [ UNALIGNED ]
    pc=0x000002b6 lr=0x00000e59 outputs=0x00000000
  inject 2: BUSFAULT  (outputs=0x00000003)
  [fault] BusFault   CFSR=0x00008200 -> outputs=0x00000000, record sealed, SYSRESETREQ
== boot #4 ==  CFSR=0x00000000 HFSR=0x00000000 LED0=0x00000000 SHCSR=0x00000000 -> 0x00070000
  reset cause: FAULT BusFault   CFSR=0x00008200 [ PRECISERR BFARVALID ]
    pc=0x000002ba lr=0x00000ea9 BFAR=0x30000000 outputs=0x00000000
  inject 3: UNDEF     (outputs=0x00000003)
  [fault] UsageFault CFSR=0x00010000 -> outputs=0x00000000, record sealed, SYSRESETREQ
== boot #5 ==  CFSR=0x00000000 HFSR=0x00000000 LED0=0x00000000 SHCSR=0x00000000 -> 0x00070000
  reset cause: FAULT UsageFault CFSR=0x00010000 [ UNDEFINSTR ]
    pc=0x000002c0 lr=0x00000e9d outputs=0x00000000
  inject 4: MPU_GUARD (outputs=0x00000003)
  [fault] MemManage  CFSR=0x00000082 -> outputs=0x00000000, record sealed, SYSRESETREQ
== boot #6 ==  CFSR=0x00000000 HFSR=0x00000000 LED0=0x00000000 SHCSR=0x00000000 -> 0x00070000
  reset cause: FAULT MemManage  CFSR=0x00000082 [ DACCVIOL MMARVALID ]
    pc=0x000002ec lr=0x00000e97 MMFAR=0x20000040 outputs=0x00000000
  inject 5: ESC_OFF   (outputs=0x00000003)
  [fault] HardFault  CFSR=0x00010000 -> outputs=0x00000000, record sealed, SYSRESETREQ
== boot #7 ==  CFSR=0x00000000 HFSR=0x00000000 LED0=0x00000000 SHCSR=0x00000000 -> 0x00070000
  reset cause: FAULT HardFault (FORCED) CFSR=0x00010000 [ UNDEFINSTR ]
    pc=0x00000310 lr=0x00000e8f outputs=0x00000000
  inject 6: ESC_PRIO  (outputs=0x00000003)
  [fault] HardFault  CFSR=0x00010000 -> outputs=0x00000000, record sealed, SYSRESETREQ
== boot #8 ==  CFSR=0x00000000 HFSR=0x00000000 LED0=0x00000000 SHCSR=0x00000000 -> 0x00070000
  reset cause: FAULT HardFault (FORCED) CFSR=0x00010000 [ UNDEFINSTR ]
    pc=0x00000314 lr=0xfffffff9 outputs=0x00000000
== reset-cause summary (QEMU mps2-an385) ==
step fault     handler    HFSR       CFSR       fault-addr stacked-pc expect-pc  out
0    DIV0      UsageFault 0x00000000 0x02000000 0x00000000 0x000002ae 0x000002ae 0x00000000
1    UNALIGNED UsageFault 0x00000000 0x01000000 0x00000000 0x000002b6 0x000002b6 0x00000000
2    BUSFAULT  BusFault   0x00000000 0x00008200 0x30000000 0x000002ba 0x000002ba 0x00000000
3    UNDEF     UsageFault 0x00000000 0x00010000 0x00000000 0x000002c0 0x000002c0 0x00000000
4    MPU_GUARD MemManage  0x00000000 0x00000082 0x20000040 0x000002ec 0x000002ec 0x00000000
5    ESC_OFF   HardFault  0x40000000 0x00010000 0x00000000 0x00000310 0x00000310 0x00000000
6    ESC_PRIO  HardFault  0x40000000 0x00010000 0x00000000 0x00000314 0x00000314 0x00000000
62/62 passed
qemu exit=0
```

**쌓인 PC 가 정말 그 명령인가** — `run_qemu.sh syms` (실제 출력 일부):

```text
pc_div0       2ae:	fbb0 f0f1 	udiv	r0, r0, r1
pc_unal       2b6:	6800      	ldr	r0, [r0, #0]
pc_bus        2ba:	6800      	ldr	r0, [r0, #0]
pc_udf        2c0:	de01      	udf	#1
pc_mpu        2ec:	6003      	str	r3, [r0, #0]
pc_esc        310:	de02      	udf	#2
pc_isr        314:	de03      	udf	#3
20000040 b g_guard
```

일곱 경우 모두 **정밀(precise)** fault 라 하드웨어가 쌓은 PC = fault 를 낸 명령의 주소 그대로다. 8 번째 부팅의 `lr=0xfffffff9` 는 IRQ7 핸들러 안에서 쌓인 LR — 그 핸들러는 leaf 함수라 LR 이 아직 EXC_RETURN 값(Thread, MSP 로 복귀)이다.

**정책 한 곳**: 네 벡터가 모두 `Fault_Entry` 의 alias 다 (`nm`: `0000025c T HardFault_Handler`, `0000025c T UsageFault_Handler`). "어느 fault 인가" 는 벡터가 아니라 IPSR 로 구분한다. 정책이 한 함수에 있으면 "출력부터 끈다" 를 빼먹는 핸들러가 생길 수 없다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">fault_policy() 의 순서 — 각 단계에서 멈추면 무엇을 잃나</text>
  <rect x="10" y="36" width="160" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="56" fill="currentColor" font-weight="bold">① safe_outputs()</text>
  <text x="20" y="74" fill="currentColor">LED0 = 0, DSB,</text>
  <text x="20" y="92" fill="currentColor">다시 읽기 → 0x0</text>
  <path d="M170 71 L190 71" stroke="currentColor"/><path d="M190 71 l-8 -4 l0 8 z" fill="currentColor"/>
  <rect x="190" y="36" width="180" height="70" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="200" y="56" fill="currentColor" font-weight="bold">② crash_commit()</text>
  <text x="200" y="74" fill="currentColor">magic=0 → 내용 9 워드 →</text>
  <text x="200" y="92" fill="currentColor">check → DSB → magic</text>
  <path d="M370 71 L390 71" stroke="currentColor"/><path d="M390 71 l-8 -4 l0 8 z" fill="currentColor"/>
  <rect x="390" y="36" width="150" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="400" y="56" fill="currentColor" font-weight="bold">③ 로그 (선택)</text>
  <text x="400" y="74" fill="currentColor">세미호스팅 한 줄</text>
  <text x="400" y="92" fill="currentColor">(제품에선 빼거나 맨 뒤)</text>
  <path d="M540 71 L560 71" stroke="currentColor"/><path d="M560 71 l-8 -4 l0 8 z" fill="currentColor"/>
  <rect x="560" y="36" width="170" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="570" y="56" fill="currentColor" font-weight="bold">④ system_reset()</text>
  <text x="570" y="74" fill="currentColor">DSB, AIRCR=0x05FA0004</text>
  <text x="570" y="92" fill="currentColor">| PRIGROUP, DSB, loop</text>
  <text x="20" y="134" fill="currentColor">① 이전에 멈추면:</text>
  <text x="20" y="150" fill="currentColor">모터·히터가 켜진 채</text>
  <text x="20" y="166" fill="currentColor">(가장 비싼 실패)</text>
  <text x="200" y="134" fill="currentColor">② 중간에 멈추면 (중첩 fault,</text>
  <text x="200" y="150" fill="currentColor">전원 끊김): magic 이 0 → 다음</text>
  <text x="200" y="166" fill="currentColor">부팅이 "기록 없음" 으로 정직하게</text>
  <text x="400" y="134" fill="currentColor">③ 에서 멈추면: 기록은</text>
  <text x="400" y="150" fill="currentColor">이미 완성. 디버거 없는</text>
  <text x="400" y="166" fill="currentColor">BKPT → Lockup (BM-B09)</text>
  <text x="570" y="134" fill="currentColor">④ 가 안 걸리면: 워치독</text>
  <text x="570" y="150" fill="currentColor">이 마지막 그물 (BM-M10)</text>
  <rect x="10" y="190" width="720" height="96" rx="6" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="20" y="210" fill="currentColor">핸들러에서 하지 말 것: malloc·printf(newlib)·긴 루프·RTOS API·전역 상태를 믿는 계산.</text>
  <text x="20" y="228" fill="currentColor">스택 프레임 포인터도 RAM 범위·4-정렬을 확인한 뒤에만 읽는다 (frame_in_ram) — 스택이 망가져서</text>
  <text x="20" y="246" fill="currentColor">온 fault 라면 frame[6] 을 읽는 순간 또 fault → HardFault 안의 fault = Lockup.</text>
  <text x="20" y="264" fill="currentColor">safe_outputs() 가 건드리는 주변장치는 클럭이 켜져 있고 단순해야 한다 (GPIO BSRR 한 번 수준).</text>
  <text x="20" y="280" fill="currentColor">더 단단히: 하드웨어 경로 — 타이머 BKIN(브레이크 입력)·외부 인에이블 핀이 CPU 없이도 출력을 끈다.</text>
</svg>
```

### Part B — 승격: 끄면, 그리고 켜도 우선순위가 모자라면

**시작 코드에서 일어난 일** (위 실측 요약 표, 시작 코드 열):

| step | 트랩/핸들러 상태 | 결과 | 왜 |
|---|---|---|---|
| DIV0 | CCR.DIV_0_TRP=0 | fault 없음, 몫 0 | v7-M 정의: 트랩이 꺼져 있으면 UDIV/SDIV 의 0 나눗셈 결과 = 0 |
| UNALIGNED | CCR.UNALIGN_TRP=0 | fault 없음, 값 0x44332211 | M3/M4 는 비정렬 LDR/STR/LDRH 를 하드웨어가 나눠 처리 |
| BUSFAULT | BUSFAULTENA=0 | HardFault, HFSR 0x40000000 | 꺼진 configurable fault → FORCED 승격, CFSR 에는 원래 BFSR 비트 |
| UNDEF / MPU / ESC_* | 각 ENA=0 | HardFault FORCED | 같은 이유 |

시작 코드의 BUSFAULT 기록을 보자: `CFSR=0x00008200 [ PRECISERR BFARVALID ]` 인데 `BFAR=0x00000000`. HardFault 로 왔어도 **원인 정보(CFSR, BFAR)는 그대로 있다** — 승격은 "누가 받나" 만 바꾼다. 그러니 HardFault 하나로 다 받는 펌웨어도 CFSR 를 읽으면 원인을 알 수 있다. 그런데도 세분 활성화를 하는 이유는 (1) 우선순위를 따로 줄 수 있고(HardFault 는 −1 고정), (2) HardFault 안의 fault 는 Lockup 이지만 UsageFault 핸들러 안의 fault 는 HardFault 로 승격되어 **한 번 더 기회**가 있고, (3) DIV0/UNALIGN 은 켜지 않으면 **아예 감지가 안 된다** (조용히 0 을 쓰고 계속 감).

**ESC_PRIO — 켜져 있는데도 HardFault**. UsageFault 의 우선순위는 SHPR1 리셋값 0, IRQ7 도 NVIC 리셋값 0. 실행 우선순위가 0 인 IRQ 핸들러 안에서 UDF 가 나면 UsageFault(0) 는 현재(0)보다 **높지 않으므로** 선점할 수 없고 → HardFault FORCED. 고침을 `demo_probe.c` P7 에서 실측했다:

```text
P7 UDF inside IRQ7, USGFAULTENA=1, UsageFault priority 0 (SHPR1 reset):
  IRQ7 prio 0x00 (IPR read back 0x00000000) exc=0x00000003 CFSR=0x00010000 HFSR=0x40000000 BFAR=0x30000000 val=0x00000000
  IRQ7 prio 0x80 (IPR read back 0x00000080) exc=0x00000006 CFSR=0x00010000 HFSR=0x00000000 BFAR=0x30000000 val=0x00000000
```

설계 규칙: **fault 핸들러 3 개에 가장 높은 configurable 우선순위(0)를 주고, 모든 IRQ 는 그보다 낮게(≥1)** 둔다. 우선순위 표 설계 자체는 BM-M02, PRIMASK/BASEPRI 와의 관계는 ARM-M10 (다음 단계).

**QEMU 사실 확인** — `run_qemu.sh probe` 의 P1~P5 (실제 출력). 핸들러가 fault 명령을 건너뛰고 복귀하는 **탐침(probe)** 방식이라 한 부팅에 여러 개를 본다:

```text
P1 load32 with BUSFAULTENA=1 (handler skips the faulting LDR):
0x30000000 (mtree: 아무것도 없음)   exc=0x00000005 CFSR=0x00008200 HFSR=0x00000000 BFAR=0x30000000 val=0xeeeeeeee
0x60000000 (mtree: 아무것도 없음)   exc=0x00000005 CFSR=0x00008200 HFSR=0x00000000 BFAR=0x60000000 val=0xeeeeeeee
0x41200000 (VGA 뒤, 없음)           exc=0x00000005 CFSR=0x00008200 HFSR=0x00000000 BFAR=0x41200000 val=0xeeeeeeee
0xE0100000 (PPB 뒤, 없음)           exc=0x00000005 CFSR=0x00008200 HFSR=0x00000000 BFAR=0xe0100000 val=0xeeeeeeee
0x00800000 (RESERVED 1, unimp dev)  exc=0x00000000 CFSR=0x00000000 HFSR=0x00000000 BFAR=0xe0100000 val=0x00000000
0x40030000 (RESERVED 4, unimp dev)  exc=0x00000000 CFSR=0x00000000 HFSR=0x00000000 BFAR=0xe0100000 val=0x00000000
0x40003000 (APB 빈 슬롯, unimp dev) exc=0x00000000 CFSR=0x00000000 HFSR=0x00000000 BFAR=0xe0100000 val=0x00000000
0x20400000 (SSRAM2&3 미러)          exc=0x00000000 CFSR=0x00000000 HFSR=0x00000000 BFAR=0xe0100000 val=0x11111111
P2 store32 to 0x30000000:
  str                                exc=0x00000005 CFSR=0x00008200 HFSR=0x00000000 BFAR=0x30000000 val=0x00000000
P3 traps OFF (CCR=0x00000200):
  udiv 7/0                           exc=0x00000000 CFSR=0x00000000 HFSR=0x00000000 BFAR=0x30000000 val=0x00000000
  ldr  [odd]                         exc=0x00000000 CFSR=0x00000000 HFSR=0x00000000 BFAR=0x30000000 val=0x44332211
  ldrh [odd]                         exc=0x00000000 CFSR=0x00000000 HFSR=0x00000000 BFAR=0x30000000 val=0x00002211
  ldrd [odd]                         exc=0x00000006 CFSR=0x01000000 HFSR=0x00000000 BFAR=0x30000000 val=0xeeeeeeee
   traps ON (CCR=0x00000218):
  udiv 7/0                           exc=0x00000006 CFSR=0x02000000 HFSR=0x00000000 BFAR=0x30000000 val=0xdeadbeef
     stacked r0 for that udiv = 0xdeadbeef (the mov before it loaded 0xeeeeeeee)
  ldr  [odd]                         exc=0x00000006 CFSR=0x01000000 HFSR=0x00000000 BFAR=0x30000000 val=0xeeeeeeee
  ldr  [+2]                          exc=0x00000006 CFSR=0x01000000 HFSR=0x00000000 BFAR=0x30000000 val=0xeeeeeeee
  ldrh [odd]                         exc=0x00000006 CFSR=0x01000000 HFSR=0x00000000 BFAR=0x30000000 val=0xeeeeeeee
P4 after W1C clear: CFSR=0x00000000 BFAR=0x30000000 MMFAR=0x00000000
P5 MPU_TYPE=0x00000800 (DREGION=bits[15:8])
```

| 관찰 | 의미 |
|---|---|
| `info mtree` 에 아무것도 없는 주소(0x30000000 등) → 정밀 BusFault, BFAR 정확 | 그래서 BUS_HOLE = 0x30000000. BM-B08 F3 의 "_estack=0x30000000 → Lockup" 과 같은 구멍 |
| "RESERVED"/빈 슬롯(0x00800000, 0x40030000, 0x40003000) → **fault 없이 0** | QEMU 의 unimplemented-device 영역 (RAZ/WI). 진짜 칩이면 BusFault 였을 주소가 여기선 조용하다 |
| 0x20400000 → 0x11111111 (= 0x20000000 의 `g_data`) | SSRAM2&3 미러 (BM-B08 F4 와 같은 현상) |
| 저장(STR)도 **PRECISERR** | 실제 Cortex-M3 는 버퍼 가능한 영역의 쓰기를 쓰기 버퍼에 넣고 진행하므로 보통 **IMPRECISERR** (BFAR 무효, 쌓인 PC 는 몇 명령 뒤)가 된다 — Generic User Guide / TRM 확인 권장. 디버깅 땐 ACTLR.DISDEFWBUF 로 정밀하게 만들 수 있다 (M3/M4, 확인 권장). QEMU 는 이 차이를 모델링하지 않는다 |
| 트랩 off: 7/0 → 0, LDR 홀수 → 0x44332211, LDRH 홀수 → 0x2211 | 결과가 "그럴듯한 값" 이라 버그가 숨는다 |
| LDRD 홀수 주소 → 트랩과 무관하게 UNALIGNED | LDRD/STRD/LDM/STM 은 항상 정렬 필요 (`-O2` 가 두 워드 읽기를 LDRD 로 합치면 packed 구조체에서 터진다) |
| 트랩 on: `ldr [+2]` (하프워드 정렬)도 fault | UNALIGN_TRP 는 "크기에 맞게 정렬" 을 요구 |
| P4: CFSR 를 지워도 BFAR=0x30000000 유지 | VALID 비트 없이 BFAR 을 믿으면 안 되는 이유 (TODO 3) |
| MPU_TYPE=0x800 | DREGION = 8 개 영역 |
| DIV0 트랩 때 쌓인 r0 = 0xdeadbeef | 직전 `mov.w r0, #0xeeeeeeee` 가 실제 칩이었다면 그대로 쌓였을 값. QEMU 의 번역(TCG) 최적화로 추정 (소스 미확인). **교훈: 에뮬레이터에서는 쌓인 PC·xPSR 은 믿되 r0–r3 값으로 결론 내지 말 것** |

### Part C — 리셋을 건너는 기록

**먼저 QEMU 가 SYSRESETREQ 때 실제로 하는 일** — `demo_probe.c` P6 (부팅 1 에서 표식을 쓰고 SYSRESETREQ, 부팅 2 의 `Reset_Handler` 가 `.data` 복사·`.bss` 0 **전에** 읽음; 실제 출력):

```text
P6 boot 1: write markers, LED0=3, then SYSRESETREQ
   .rodata(FLASH) after write = 0x55555555
   LED0 after writing 0xff = 0x00000003
P6 boot 2 (before .data/.bss init):
   .data=0x22222222 .bss=0x33333333 .noinit=0xb007cafe raw RAM=0x44444444
   .rodata(FLASH)=0x0da7a000 loop iterations after SYSRESETREQ=0x00000000
   CFSR=0x00000000 SHCSR=0x00000000 CCR=0x00000200 LED0=0x00000000 AIRCR=0xfa050000
qemu exit=0
```

| 질문 | QEMU 10.2.1 실측 답 | 실제 칩 (확인 권장) |
|---|---|---|
| SYSRESETREQ = 머신 리셋? | 예. 같은 QEMU 프로세스에서 벡터 [0]/[1] 부터 다시 시작 | 예. STM32 면 RCC_CSR.SFTRSTF 가 선다 |
| RAM 은 남나? | **전부 남는다**: `.data`(VMA) 0x22222222, `.bss` 0x33333333, `.noinit`, 섹션 밖 RAM | 시스템 리셋에선 SRAM 유지가 보통 (전원 차단·BOR 은 아님) |
| `-kernel` 이 다시 쓰나? | **예 — ELF 로드 세그먼트(FileSiz > 0)는 매 리셋마다 다시 쓴다**: 0x55555555 로 바꾼 `.rodata` 가 0x0da7a000 으로 복구. FileSiz 0 인 세그먼트(`.bss`, `.noinit`)는 그대로 | 해당 없음 (플래시는 프로그램된 그대로) |
| SYSRESETREQ 쓰기 뒤 몇 명령 더 도나? | 0 번 (루프 카운터 0) | 몇 사이클 걸릴 수 있다 → CMSIS 는 DSB 후 무한 루프 |
| 레지스터는? | CFSR/HFSR/SHCSR = 0, CCR = 0x200, LED0 = 0 | 같은 방향: fault enable 은 **매 부팅 다시 켜야** 한다 |
| `-no-reboot` 이면? | 첫 SYSRESETREQ 에서 QEMU 가 **종료 코드 0** 으로 끝남 (아래) | — |

`run_qemu.sh syms` 의 세그먼트 표가 왜 `.noinit` 이 안전한지 보여준다 (실제 출력):

```text
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  LOAD           0x010000 0x00000000 0x00000000 0x01630 0x01630 R E 0x10000
  LOAD           0x020000 0x20000000 0x00001630 0x00008 0x00008 RW  0x10000
  LOAD           0x000020 0x20000020 0x00001638 0x00000 0x00040 RW  0x10000
  LOAD           0x00fc00 0x2000fc00 0x2000fc00 0x00000 0x00178 RW  0x10000
   03     .noinit
2000fc00 B g_plan
2000fd4c B g_crash
2000fd78 B _enoinit
```

`.noinit` 세그먼트는 FileSiz 0 — 파일에 내용이 없으니 QEMU 로더가 다시 쓸 것이 없고, 우리 `Reset_Handler` 는 `[_sbss, _ebss)` 만 0 으로 만든다. `.data` 세그먼트의 PhysAddr 가 0x1630 (FLASH 쪽 LMA) 인 것도 보인다 — QEMU 는 LMA 에 올린다 (BM-B08).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">한 QEMU 프로세스, 8 번의 부팅 — 무엇이 리셋을 건너나 (실측)</text>
  <rect x="10" y="34" width="250" height="40" class="f-mut" stroke="currentColor"/>
  <text x="18" y="52" fill="currentColor">FLASH 0x0 (QEMU SSRAM1): 코드·.rodata·.data 초기값</text>
  <text x="18" y="68" fill="currentColor">→ 매 리셋 ELF 에서 다시 씀 (FileSiz &gt; 0)</text>
  <rect x="10" y="84" width="250" height="34" class="f-warn" stroke="currentColor"/>
  <text x="18" y="105" fill="currentColor">.data/.bss 0x20000000.. → reset_init 이 덮음</text>
  <rect x="10" y="118" width="250" height="40" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="18" y="142" fill="currentColor">스택 ↓ (꼭대기 _estack = 0x2000FC00)</text>
  <rect x="10" y="158" width="250" height="52" class="f-ok" stroke="currentColor"/>
  <text x="18" y="176" fill="currentColor">.noinit 0x2000FC00 (NOLOAD, FileSiz 0)</text>
  <text x="18" y="192" fill="currentColor">g_plan 332 B: boots, next, res[7]</text>
  <text x="18" y="206" fill="currentColor">g_crash 44 B @0x2000FD4C</text>
  <text x="300" y="50" fill="currentColor" font-weight="bold">부팅 k</text>
  <text x="300" y="70" fill="currentColor">1. fault_policy_enable() (리셋이 지웠으니 매번)</text>
  <text x="300" y="88" fill="currentColor">2. g_plan 검증 → 없으면 콜드 부팅 초기화</text>
  <text x="300" y="106" fill="currentColor">3. g_crash 유효? → 보고, res[] 에 복사, magic=0</text>
  <text x="300" y="124" fill="currentColor">4. next++ 봉인 (주입 "전에"), LED0 = 0x3</text>
  <text x="300" y="142" fill="currentColor">5. inject(k) → fault</text>
  <rect x="290" y="152" width="440" height="58" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="300" y="172" fill="currentColor">fault_policy: LED0=0 → g_crash 봉인 → SYSRESETREQ</text>
  <text x="300" y="190" fill="currentColor">QEMU: 머신 리셋, ELF 세그먼트 재기록, RAM 은 그대로</text>
  <text x="300" y="204" fill="currentColor">→ 부팅 k+1 의 3 단계가 g_crash 를 읽는다</text>
  <path d="M260 184 L290 184" stroke="currentColor"/><path d="M260 184 l8 -4 l0 8 z" fill="currentColor"/>
  <text x="10" y="240" fill="currentColor" font-weight="bold">bssbug (기록을 .bss 에): 매번 콜드 부팅 → DIV0 무한 반복 → 3 초에 3031~4775 번, timeout 124</text>
  <text x="10" y="260" fill="currentColor">-no-reboot: 5 단계의 첫 SYSRESETREQ 에서 QEMU 가 exit 0 으로 끝 → "통과" 처럼 보이는 함정</text>
  <text x="10" y="280" fill="currentColor">부팅 8: next == 7 → 요약 표 + 62 검사 → SYS_EXIT 0. 가드: boots &gt; 12 이면 exit 2 (리셋 루프 감지)</text>
  <text x="10" y="300" fill="currentColor">진짜 보드의 대안 저장소: RTC 백업 레지스터(Standby 에서도 유지), 전용 SRAM 섹션, 다음 부팅에 플래시 로그로 옮기기</text>
  <text x="10" y="320" fill="currentColor">(fault 핸들러 안에서 플래시 erase/program 은 하지 않는다 — 수 ms~수백 ms, 실패 경로가 많다)</text>
</svg>
```

**-no-reboot 의 함정** (`run_qemu.sh noreboot`, 실제 출력):

```text
cold boot: no valid plan in .noinit @0x2000fc00
== boot #1 ==  CFSR=0x00000000 HFSR=0x00000000 LED0=0x00000000 SHCSR=0x00000000 -> 0x00070000
  inject 0: DIV0      (outputs=0x00000003)
  [fault] UsageFault CFSR=0x02000000 -> outputs=0x00000000, record sealed, SYSRESETREQ
qemu exit=0
```

리셋 요청 = QEMU 종료, 그리고 **종료 코드 0**. CI 가 exit code 만 본다면 7 개 중 1 개만 돌고 "PASS" 로 찍힌다. 그래서 이 레슨은 `-no-reboot` 를 쓰지 않고, 마지막 부팅의 `SYS_EXIT_EXTENDED` 만이 성공 코드를 낸다 (+ `timeout 20`).

**bssbug — 기록이 리셋을 못 건너면** (`run_qemu.sh bssbug`, 실제 출력):

```text
cold boot: no valid plan in .noinit @0x20000020
== boot #1 ==  CFSR=0x00000000 HFSR=0x00000000 LED0=0x00000000 SHCSR=0x00000000 -> 0x00070000
  inject 0: DIV0      (outputs=0x00000003)
  [fault] UsageFault CFSR=0x02000000 -> outputs=0x00000000, record sealed, SYSRESETREQ
cold boot: no valid plan in .noinit @0x20000020
== boot #1 ==  CFSR=0x00000000 HFSR=0x00000000 LED0=0x00000000 SHCSR=0x00000000 -> 0x00070000
... (3 초 동안) '== boot #' 줄 수: 3031, 'cold boot' 줄 수: 3031
    '== boot #2' 가 나온 횟수: 0
qemu exit=124 (124 = timeout 이 죽임)
```

(같은 명령의 다른 실행에서는 4775 번 — 횟수는 호스트 속도에 따라 다르다.) 주소가 0x20000020 (`.bss`) 로 옮겨졌고, `reset_init` 이 매번 0 으로 지우니 **부팅 카운터 가드도 같이 지워져** 영원히 돈다. 현장의 장비라면: 모터가 켜졌다(LED0=3) 꺼졌다를 1 ms 단위로 반복하는 최악의 상태. 가드 카운터는 반드시 리셋을 건너는 저장소에 있어야 한다.

**기록 봉인 순서** (`crash_commit`): `magic = 0` → 내용 9 워드 → `check = FNV-1a(내용)` → `DSB` → `magic = 0xFA17C0DE` → `DSB`. 다음 부팅의 `crash_valid` 는 magic **과** 체크값을 둘 다 본다. 중간에 끊긴 기록(중첩 fault, 전원 차단)은 magic 이 0 이라 "기록 없음" 이 되고, 콜드 부팅 쓰레기 RAM 은 magic 이 2^-32 확률로만 맞고 그마저 체크값이 거른다 (연습 3 에서 10 만 건 실측). 소비한 기록은 `magic = 0` 으로 무효화해서 **다음의 원인 모를 리셋**(워치독, 브라운아웃)을 이전 fault 로 잘못 보고하지 않게 한다. 진짜 칩이면 여기에 하드웨어 리셋 원인(STM32 RCC_CSR, BM-M10)을 같이 읽어 "fault 기록 + SFTRSTF" 일치를 확인한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

## 7. 자주 하는 실수

**실수 1 — SHCSR 를 통째로 쓰기.** `SCB_SHCSR = 7u << 16;` 는 같은 레지스터의 활성/펜딩 상태 비트(MEMFAULTACT 0, BUSFAULTACT 1, USGFAULTACT 3, SVCALLACT 7, … — 확인 권장)를 0 으로 덮는다. 부팅 직후엔 우연히 무해하지만, 예외 안에서 부르면 상태가 깨진다. 고침: `SCB_SHCSR |= SHCSR_MEMFAULTENA | SHCSR_BUSFAULTENA | SHCSR_USGFAULTENA;`

**실수 2 — 기록부터 하고 출력은 나중에.** 기록 중에 또 fault 가 나면(망가진 스택, 잘못된 포인터) 그 다음 줄은 영영 실행되지 않는다 — 출력은 켜진 채로 Lockup. 고침: `safe_outputs()` 가 핸들러의 **첫 줄**. 시작 코드 실측의 `outputs=0x00000003` 이 이 실수의 결과다.

**실수 3 — VALID 비트 없이 BFAR/MMFAR 기록.** P4 실측: CFSR 를 지워도 BFAR=0x30000000 이 남는다. 다음 UsageFault 의 기록에 "BFAR=0x30000000" 을 넣으면 엉뚱한 주소를 쫓게 된다. 고침: `*bfar = (cfsr & CFSR_BFARVALID) ? SCB_BFAR : 0u;`

**실수 4 — 기록을 `.bss` (또는 그냥 전역) 에.** `bssbug` 실측: 3 초에 수천 번 콜드 부팅. `.noinit` 은 링커 스크립트에서 `(NOLOAD)` 이고 `reset_init` 의 0 범위 밖이어야 한다. 섹션 이름만 `.noinit` 으로 붙이고 링커 스크립트에 출력 섹션이 없으면 GNU ld 가 고아 섹션(orphan)으로 아무 데나 붙인다 — `m13.ld` 처럼 `ASSERT(_snoinit == ORIGIN(NOINIT))` 로 고정 주소를 확인.

**실수 5 — DIV0/UNALIGN 트랩을 안 켜고 "fault 없으니 안전" 이라 믿기.** 시작 코드의 DIV0 은 fault 없이 0 을, UNALIGNED 는 0x44332211 을 돌려줬다. 제어 루프에서 0 나눗셈의 결과 0 은 "출력 0" 같은 그럴듯한 값이라 더 위험하다. 반대로 UNALIGN_TRP 를 켤 땐 기존 코드의 의도된 비정렬 접근(packed 구조체 파싱 등)이 fault 로 바뀌므로 `-mno-unaligned-access` 로 빌드하고 memcpy 로 바꿔야 한다 (켜기 전에 전수 시험).

**실수 6 — IRQ 와 fault 핸들러를 같은 우선순위 0 에.** ESC_PRIO 실측: USGFAULTENA 를 켰는데도 HardFault FORCED. 고침: IRQ 는 1 이상, fault 핸들러는 0 (P7: IRQ7 0x80 → UsageFault).

**실수 7 — AIRCR 에 VECTKEY 없이, 또는 PRIGROUP 을 지우며 쓰기.** `SCB_AIRCR = 1u << 2;` 는 키가 없어 무시된다(리셋 안 됨 → 핸들러가 다음 줄로 흘러감). `SCB_AIRCR = 0x05FA0004;` 는 리셋은 되지만 PRIGROUP 을 0 으로 바꾼다 — 리셋 직전이라 무해해 보여도, 같은 함수를 "리셋 안 하는" 경로에서 재사용하면 우선순위 그룹이 바뀐다. 고침: `VECTKEY | (AIRCR & PRIGROUP_Msk) | SYSRESETREQ`, 앞뒤 DSB, 그 뒤 무한 루프.

## 8. 면접 Q&A

<details>
<summary>Q1. MemManage/BusFault/UsageFault 를 따로 켜는 이유가 뭔가요? 어차피 HardFault 에서 CFSR 를 보면 되잖아요.</summary>

맞다, 원인은 HardFault 에서도 CFSR 로 보인다 (시작 코드의 BUSFAULT 기록: HardFault 인데 CFSR=0x8200). 그래도 켜는 이유: (1) HardFault 는 우선순위 −1 고정이라 다른 모든 것을 막는다 — 세분 fault 는 SHPR1 로 우선순위를 정할 수 있어 NMI 나 더 급한 처리를 살려 둘 수 있다. (2) 세분 핸들러 안에서 또 fault 가 나면 HardFault 로 한 번 더 승격될 기회가 있다; HardFault 안의 fault 는 곧장 Lockup. (3) 핸들러별 정책을 다르게 할 수 있다 (예: MPU 스택 가드 위반 = 해당 태스크만 죽이고 계속 — RTOS 에서). 꼬리질문 "DIV0 은?" → 트랩(CCR) 을 켜지 않으면 HardFault 로도 안 온다. 그냥 0.

</details>

<details>
<summary>Q2. fault 가 났을 때 모터 출력은 어떻게 되어야 하고, 소프트웨어만으로 충분한가요?</summary>

알려진 안전 상태(보통 꺼짐/브레이크)로, **핸들러의 첫 동작**으로. 쓰고 DSB 후 다시 읽어 확인. 하지만 소프트웨어 경로는 CPU 가 핸들러를 실행할 수 있을 때만 동작한다 — Lockup, 클럭 고장, 스택 붕괴면 못 한다. 그래서 진짜 설계는 층을 쌓는다: (1) fault 핸들러, (2) 워치독 리셋 (리셋되면 GPIO 는 입력/Hi-Z 로 → 외부 풀다운으로 게이트 드라이버 OFF), (3) 하드웨어 경로: 고급 타이머의 브레이크 입력(BKIN)·MOE 자동 해제, 외부 인에이블 핀, 세이프티 MCU. 면접에선 "리셋 후 핀의 기본 상태가 안전하도록 보드가 설계되어야 한다" 까지 말하면 좋다.

</details>

<details>
<summary>Q3. 기록을 왜 magic 을 마지막에 쓰나요? 체크값만으로는 안 되나요?</summary>

쓰기 도중에 끊기는 경우(핸들러 안의 중첩 fault, 전원 차단)가 있다. magic 을 먼저 쓰면 "magic 은 맞는데 내용은 반쯤" 인 기록이 생기고, 체크값이 그걸 걸러 주긴 하지만 그건 우연에 기대는 것이다. magic 을 마지막(그리고 그 전에 DSB)에 쓰면 magic 의 존재 자체가 "앞의 쓰기가 모두 끝났다" 는 커밋 표시가 된다 — 저널링 파일시스템의 커밋 레코드, Don 이 아는 FTL 의 메타데이터 커밋과 같은 원리. 체크값은 그 위에 쓰레기 RAM / 비트 플립을 거른다 (연습 3: 1비트 352 가지 전부 검출).

</details>

<details>
<summary>Q4. 리셋 원인을 하드웨어가 알려주는데(RCC_CSR) 왜 RAM 기록이 필요한가요?</summary>

RCC_CSR 은 "소프트웨어 리셋이었다(SFTRSTF)" 까지만 안다. 어떤 fault 였는지, 어느 PC 인지, 출력이 꺼졌는지는 모른다. 반대로 RAM 기록만으로는 워치독·브라운아웃·핀 리셋을 구분 못 한다. 둘을 같이 쓴다: 부팅 때 RCC_CSR 를 읽고(RMVF 로 지움, BM-M10), RAM 기록이 유효하고 SFTRSTF 면 "fault → 정책 리셋", IWDGRSTF 인데 기록이 없으면 "행(hang)", PORRSTF 면 기록은 쓰레기로 간주. 기록은 다음 부팅에 플래시 로그나 텔레메트리로 옮긴다.

</details>

<details>
<summary>Q5. 같은 fault 로 계속 리셋되면 어떻게 하나요?</summary>

리셋 루프 가드. `.noinit` (또는 RTC 백업 레지스터)에 "짧은 간격의 연속 fault 리셋" 카운터를 두고, N 번(예: 3)을 넘으면 SAFE 모드로 부팅한다: 액추에이터는 켜지 않고, 통신·진단만 살려 원격으로 로그를 꺼내거나 업데이트를 받게 한다. 일정 시간(예: 60 s) 문제없이 돌면 카운터를 0 으로. 소프트웨어 업데이트 같은 의도된 리셋은 카운터를 리셋하되 SAFE 는 풀지 않는다 (연습 4). 이 레슨의 bssbug 는 가드 카운터가 리셋을 못 건너서 가드가 무력해진 예다.

</details>

<details>
<summary>Q6. 같은 우선순위의 IRQ 안에서 난 fault 는 왜 HardFault 로 가나요? 고치는 법은?</summary>

동기 fault 도 "예외" 라 선점 규칙을 따른다. UsageFault 의 우선순위가 현재 실행 우선순위보다 높지 않으면(같거나 낮으면) 들어갈 수 없으므로 HardFault 로 승격된다 (ESC_PRIO 실측: HFSR 0x40000000). 리셋값은 SHPR1 의 세 fault 모두 0, IRQ 도 0 이므로 **아무 설정도 안 한 시스템에서는 모든 ISR 안의 fault 가 HardFault** 다. 고침: IRQ 우선순위를 1 이상으로 내린다 (P7: 0x80 → UsageFault). 꼬리질문 "BASEPRI 로 막고 있을 때는?" → BASEPRI 는 실행 우선순위를 올리므로 fault 우선순위가 그보다 높아야 한다 (ARM-M10).

</details>

<details>
<summary>Q7. BusFault 의 쌓인 PC 가 엉뚱한 곳을 가리킵니다. 왜죠?</summary>

부정확(imprecise) BusFault 일 가능성. Cortex-M3/M4 는 버퍼 가능한 영역에 대한 쓰기를 쓰기 버퍼에 넣고 다음 명령을 실행하므로, 버스 오류가 돌아왔을 땐 PC 가 이미 몇 명령 뒤다. CFSR 에 IMPRECISERR (bit10), BFARVALID 는 0. 디버깅 때 ACTLR.DISDEFWBUF 를 켜서 정밀하게 만들고 재현한다 (성능 저하 — 확인 권장). 주의: QEMU 는 저장도 PRECISERR 로 보고했다 (P2 실측) — 에뮬레이터에서 정밀하게 잡혔다고 실제 칩에서도 그렇다고 가정하면 안 된다.

</details>

<details>
<summary>Q8. fault 핸들러에서 printf 로 덤프를 찍으면 안 되나요?</summary>

개발 보드에 디버거가 붙어 있을 땐 편하다. 하지만 (1) newlib printf 는 힙·락·재진입을 쓰고, 망가진 상태에서 그것들을 믿을 수 없다. (2) 세미호스팅은 BKPT 0xAB — 디버거가 없으면 HardFault, 그리고 그게 HardFault 안이면 Lockup (BM-B09). (3) UART 폴링 출력은 115200 bps 에서 100 바이트에 ~9 ms — 그 동안 시스템은 멈춰 있다. 그래서 이 레슨은 기록을 **먼저 봉인한 뒤에야** 한 줄 로그를 찍고, 진짜 보고는 다음 부팅의 main 에서 한다. 제품 빌드에선 핸들러 로그를 컴파일 아웃.

</details>

<details>
<summary>Q9. QEMU 에서 이 시험을 돌릴 때 실제 칩과 다른 점은 뭐였나요?</summary>

실측한 차이: (1) 매핑 안 된 주소 중 "RESERVED" 영역은 fault 없이 0 을 읽는다 — BusFault 시험용 주소를 `info mtree` 로 골라야 했다(0x30000000). (2) 저장도 정밀 BusFault. (3) 리셋 때 `-kernel` ELF 세그먼트를 다시 쓴다 — FileSiz 가 있는 섹션에 기록을 두면 덮인다. (4) SYSRESETREQ 뒤 0 명령 만에 리셋. (5) `-no-reboot` 는 리셋을 exit 0 으로 바꾼다. (6) 쌓인 r0 이 실제 칩과 다르게 보인 경우(0xdeadbeef). 결론: 정책 로직과 레지스터 비트는 QEMU 로 검증하되, 정밀/부정확·타이밍·r0–r3 은 실보드에서 다시 본다.

</details>

## 9. 추가 연습

`practice.c` 는 **호스트 C** 다 (Mac `cc` 로, 또는 VM 에서 `run_qemu.sh practice`). 펌웨어 없이 정책의 "판단" 부분을 검증하고, 마지막 연습은 이 레슨의 **실측 요약 표**(시작 코드 7 줄 + 정답 7 줄)를 그대로 넣어 예측과 대조한다.

### 연습 1 — `fault_route()`: 어느 핸들러가 받나

`fault_route(kind, shcsr, ccr, cur_prio, fault_prio)` → `{exc, forced}`. kind ∈ {DIV0, 비정렬 LDR, 비정렬 LDRD, 정밀 BusFault, UDF, MPU}. 트랩(CCR) → 핸들러 enable(SHCSR) → 우선순위 → HardFault 안이면 LOCKUP.

<details>
<summary>답</summary>

DIV0/비정렬 LDR 은 CCR 비트가 없으면 `EXC_NONE`. LDRD 비정렬·BusFault·UDF·MPU 는 항상 fault. 대상 핸들러의 enable 비트가 0 이거나 `fault_prio >= cur_prio` (숫자가 작을수록 높음; 스레드 모드 = 256) 면 `HARD, forced=1`. `cur_prio == -1` (HardFault 안) 이면 `LOCKUP`. 성질 검사 4608 조합: HardFault ⇔ forced, 트랩 여부는 SHCSR 와 무관, SHCSR=0 이면 Hard/LOCKUP/NONE 뿐, 세분 핸들러가 받았다면 그 우선순위가 현재보다 높다 — 위반 0.

</details>

### 연습 2 — `cfsr_describe()`: 이름으로

이 레슨에서 다룬 10 비트(IACCVIOL, DACCVIOL, MMARVALID, PRECISERR, IMPRECISERR, BFARVALID, UNDEFINSTR, INVSTATE, UNALIGNED, DIVBYZERO)를 `|` 로 잇고, 목록 밖 비트는 `+0x…`, 0 이면 `-`. 용량이 모자라면 −1 과 빈 문자열.

<details>
<summary>답</summary>

`0x00008200 → "PRECISERR|BFARVALID"` (19 자 → cap 19 는 −1, cap 20 은 19). `0x00090000 → "UNDEFINSTR|+0x00080000"` (bit19 = NOCP 는 이 목록 밖 — 전체 해독은 ARM-M12). 먼저 임시 버퍼에 만들고 길이 확인 후 복사하면 잘린 문자열을 내보내는 실수가 없다.

</details>

### 연습 3 — 기록 봉인 `rec_seal()/rec_valid()`

펌웨어와 같은 FNV-1a. 검사: 1비트 뒤집힘 전수(11 워드 × 32 = 352), 무작위 다중 손상 20000 건(고정 시드 xorshift32), magic 전에 끊긴 기록, 소비된 기록, 콜드 부팅 쓰레기 10 만 건.

<details>
<summary>답</summary>

실측: 352 가지 전부 검출, 20000 건 미검출 0, 쓰레기 10 만 건 통과 0. 1바이트 변화는 FNV-1a 에서 **항상** 검출된다 — 각 단계 `h = (h ^ b) · p` 가 (p 가 홀수라) 2^32 위의 전단사이므로 한 바이트가 바뀌면 그 뒤의 h 가 전부 달라진다. 여러 바이트 변화는 확률적(충돌 ≈ 2^-32). CRC-32 를 쓰면 버스트 오류에 대한 보장이 생긴다 (COMM-B08).

</details>

### 연습 4 — 리셋 루프 가드 `boot_policy()`

상태 `{consecutive, total_faults, mode}` 는 `.noinit` 에 산다고 가정. FAULT/WATCHDOG 리셋 전 가동 시간이 60 s 미만이면 연속 카운터 +1, 이상이면 1 로. 연속 3 이면 SAFE. SOFTWARE 리셋은 카운터만 0 (SAFE 유지), POWER_ON 은 전부 초기화.

<details>
<summary>답</summary>

경계: 59 s 는 "짧음", 60 s 는 "안정" (59, 59, 60 → 카운터 1). 간헐 fault (매번 1 시간 돌고 죽음) 10 번 → NORMAL, total 10. 이 레슨의 8 부팅(각 수 ms)에 가드를 넣었다면 **3 번째** fault 리셋에서 SAFE — 시험 하네스가 `MAX_BOOTS=12` 로 따로 넉넉히 잡은 이유다. 콜드 부팅 쓰레기(0xA5…)는 POWER_ON 원인으로 정리된다 — 원인 판별은 하드웨어 리셋 플래그로 (BM-M10).

</details>

### 연습 5 — 실측 요약 표 대조

`run_qemu.sh starter/solution` 의 요약 14 줄을 `sscanf` 로 파싱하고, 각 줄의 핸들러·HFSR.FORCED 가 연습 1 의 예측(그 단계의 실제 SHCSR/CCR/우선순위 조건)과 같은지, 정답 줄은 CFSR 이름·PC 일치·출력 0 까지 확인.

<details>
<summary>답</summary>

조건표: 정답 = SHCSR 세 비트 + 두 트랩, 단 step 5 는 USGFAULTENA 를 끔, step 6 은 현재 우선순위 0. 시작 코드 = SHCSR 0, CCR 0x200. 14 줄 모두 일치 → 연습 1 의 규칙이 QEMU 의 실제 동작과 같다는 증거. 펌웨어를 바꾸면 주소가 바뀌므로 `run_qemu.sh` 출력에서 이 표를 다시 붙여 넣는다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-M13_pr && /tmp/BM-M13_pr    # 31/31 passed
# VM 에서: limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"
```

실행 결과 (실제 출력):

```text
  ex1: 4608 조합 성질 검사, 위반 0
  ex3: 1비트 뒤집힘 352 가지 중 못 잡은 것 0
  ex3: 무작위 다중 손상 20000 건 중 통과(미검출) 0
  ex5: 실측 14 줄 중 예측과 일치 14
31/31 passed
```

실행 스크립트와 QEMU 확인 펌웨어 전체:

<!-- code: run_qemu.sh -->

<!-- code: demo_probe.c -->

## 10. 핵심 정리

- 켜기: `SHCSR |= 7<<16` (Mem 16 / Bus 17 / Usage 18), `CCR |= DIV_0_TRP(4) | UNALIGN_TRP(3)`, DSB+ISB. **리셋이 다 지우므로 매 부팅** (실측 SHCSR=0, CCR=0x200).
- 끄면: Bus/Mem/UDF 는 HardFault FORCED (HFSR 0x40000000, CFSR 에 원인 유지), DIV0/비정렬은 **아예 fault 없음** (0, 0x44332211).
- 켜도: 같은 우선순위 IRQ 안의 fault → HardFault. IRQ ≥ 1, fault 핸들러 0. HardFault 안의 fault → Lockup.
- 핸들러 순서: **출력 안전(쓰기+DSB+읽기) → 기록(내용 → 체크 → DSB → magic) → 로그 → SYSRESETREQ(VECTKEY|PRIGROUP|bit2) + 루프**.
- BFAR/MMFAR 는 VALID 비트가 있을 때만. 쌓인 PC = 정밀 fault 의 명령 주소 (실측 7/7 일치).
- 기록은 `.noinit (NOLOAD)` 고정 주소, 스택 위쪽. QEMU: RAM 은 리셋을 건너고, FileSiz > 0 인 ELF 세그먼트는 매 리셋 다시 씌워진다. `.bss` 에 두면 리셋 루프.
- 다음 부팅: 기록 검증 → 보고 → 무효화, 하드웨어 리셋 원인과 대조, 연속 fault N 번이면 SAFE 모드.
- QEMU 로 정책·비트를 검증하되 정밀/부정확 BusFault, 리셋 타이밍, r0–r3 값은 실보드에서 다시 본다.

## 참고자료

- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403E) — B1.5 예외 모델 (우선순위·승격·Lockup), B3.2 SCB: SHCSR, CCR, CFSR(MMFSR/BFSR/UFSR), HFSR, MMFAR, BFAR, AIRCR; B3.5 PMSAv7 MPU (절 번호 확인 권장).
- Arm, *Cortex-M3 Devices Generic User Guide* (DUI 0552A) — 4.3 System control block, 4.5 MPU; *Cortex-M3 TRM* — 쓰기 버퍼, ACTLR.DISDEFWBUF.
- Arm CMSIS 5 `CMSIS/Core/Include/core_cm3.h` — `NVIC_SystemReset()` (DSB, VECTKEY|PRIGROUP|SYSRESETREQ, DSB, 루프), `SCB_SHCSR_*_Msk`, `SCB_CFSR_*`.
- Arm Application Note 209, *Using Cortex-M3/M4/M7 Fault Exceptions* — 핸들러 구조, 정밀/부정확 BusFault (확인 권장).
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* (3rd ed.) — 12 장 Fault exceptions.
- QEMU 10.2.1: `info mtree -f` (mps2-an385 메모리 맵 실측), 소스 `hw/arm/mps2.c`, `hw/misc/mps2-fpgaio.c`, `hw/intc/armv7m_nvic.c` (SYSRESETREQ, 승격), `hw/core/loader.c` (리셋 때 ROM blob 재기록), `-no-reboot` 옵션 문서.
- STM32F4 RM0090 — RCC_CSR 리셋 플래그(SFTRSTF, IWDGRSTF, RMVF), RTC 백업 레지스터 (확인 권장).
- 관련 레슨: BM-B08 (C startup, QEMU 부팅, Lockup 관찰 F3), BM-B09 (세미호스팅이 디버거 없이 멈추는 이유), BM-B06 (고정 주소 `.noinit` 레이아웃), BM-M10 (워치독·리셋 원인 레지스터), BM-M02 (우선순위 표 설계), ARM-M11 (같은 QEMU 랩), COMM-B08 (CRC vs 체크섬). 다음 단계: ARM-M12 (CFSR/HFSR 전체 디코드 + PC 복구), ARM-M10 (PRIMASK/BASEPRI), TEST-B09 (태스크 체크인 워치독).
