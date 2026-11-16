---
id: BM-A03
time: 120분
updated: 2026-11-15
---
# BM-A03 부트로더 → 앱 점프: VTOR, MSP, 주변장치 정리

## 학습 목표

- 부트로더와 앱을 **서로 다른 링커 스크립트**(0x00000000 / 0x00004000)로 빌드하고, `objcopy` + 0xFF 패딩으로 **하나의 플래시 이미지**로 합친 뒤, QEMU 가 그것을 실제로 어떻게 로드하는지(raw 바이너리 → 주소 0) 확인한다.
- 점프 절차 — `cpsid i` → SysTick 정지(CTRL·CVR·**PENDSTCLR**) → 장치 끄기 → NVIC **ICER + ICPR** → VTOR → CONTROL → MSP → PRIMASK → ISB → `bx` — 의 각 줄이 **무엇을 막는지**를 QEMU 실측으로 말할 수 있다.
- 앱 쪽에서 "부트로더를 믿지 않는" 진입 검사(VTOR, MSP, PRIMASK, CONTROL, SysTick, NVIC pending, 장치 상태, 준비 전 인터럽트)를 만들고, **실수 6 가지를 주입해 각각이 어떤 증상으로 나타나는지** 재현한다.
- 앱 벡터 sanity check(지워진 슬롯, SP 범위·정렬, Thumb 비트, 리셋 벡터 범위)로 "앱이 없거나 깨졌을 때 부트로더에 머무는" 경로를 만든다.
- 이 핸드오프 설계가 틀리는 경우(클럭·DMA·캐시·FPU·MPU·PSP)와 대안("리셋을 거쳐 점프")을 면접에서 설명할 수 있다.

## 1. 문제

펌웨어 업데이트가 있는 모든 제품에는 부트로더가 있고, 면접관은 "부트로더에서 앱으로 점프할 때 뭘 정리하나요?" 를 거의 반드시 묻는다. 답이 "VTOR 랑 MSP 설정하고 점프요" 에서 끝나면 시니어 답이 아니다 — **부트로더가 켜 놓은 모든 인터럽트 원천이, 앱이 준비되기 전에 앱의 핸들러로 들어올 수 있기 때문**이다. Don 이 SSD 컨트롤러에서 ROM 코드 → 로더 → 메인 FW 로 넘어가던 단계와 같은 문제를 Cortex-M3 (QEMU `mps2-an385`) 위에서 처음부터 끝까지 만든다. 이 레슨의 앱 검증은 벡터 sanity check 까지만이고, 헤더·CRC·SHA 는 BM-A04, 서명은 SEC 트랙(다음 단계)의 몫이다.

이 레슨의 플래시/RAM 배치 (숫자는 `run_qemu.sh layout` 실측):

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">합친 이미지 merged.bin (18184 B) 와 RAM 배치 — QEMU mps2-an385</text>
  <text x="10" y="42" fill="currentColor">"플래시" = ZBT SSRAM1 0x00000000 (4 MB)</text>
  <rect x="10" y="52" width="330" height="40" class="f-acc" stroke="currentColor"/>
  <text x="20" y="70" fill="currentColor">0x0000 부트로더 벡터 (SP=0x2000E000, PC=0x22D)</text>
  <text x="20" y="85" fill="currentColor">.text 6968 B (0x0000–0x1B37)</text>
  <rect x="10" y="92" width="330" height="40" class="f-mut" stroke="currentColor"/>
  <text x="20" y="110" fill="currentColor">0x1B38–0x3FFF 패딩 0xFF (objcopy --gap-fill)</text>
  <text x="20" y="125" fill="currentColor">= 섹터 0 (16 KB) 의 나머지</text>
  <rect x="10" y="132" width="330" height="40" class="f-ok" stroke="currentColor"/>
  <text x="20" y="150" fill="currentColor">0x4000 앱 벡터 (SP=0x2000F800, PC=0x41BD)</text>
  <text x="20" y="165" fill="currentColor">.text 1800 B (0x4000–0x4707)</text>
  <rect x="10" y="172" width="330" height="40" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="20" y="190" fill="currentColor">0x4708–0x3FFFF 앱 슬롯 여유</text>
  <text x="20" y="205" fill="currentColor">APP_LIMIT = 0x40000 (240 KB 슬롯)</text>
  <text x="400" y="42" fill="currentColor">RAM = SSRAM2&amp;3 0x20000000 (이 레슨은 64 KB 만)</text>
  <rect x="400" y="52" width="330" height="70" class="f-ok" stroke="currentColor"/>
  <text x="410" y="72" fill="currentColor">앱 RAM 0x20000000–0x2000F7FF (62 KB)</text>
  <text x="410" y="88" fill="currentColor">.data/.bss 아래에서 위로, 스택은</text>
  <text x="410" y="104" fill="currentColor">0x2000F800 에서 아래로</text>
  <rect x="400" y="122" width="330" height="40" class="f-acc" stroke="currentColor"/>
  <text x="410" y="140" fill="currentColor">부트로더 RAM 0x2000D000–0x2000DFFF (4 KB)</text>
  <text x="410" y="155" fill="currentColor">점프 뒤에는 "앱 RAM 의 일부" = 죽은 메모리</text>
  <rect x="400" y="172" width="330" height="40" class="f-warn" stroke="currentColor"/>
  <text x="410" y="190" fill="currentColor">0x2000F800 메일박스 256 B (부트로더↔앱)</text>
  <text x="410" y="205" fill="currentColor">0x2000F900 시험 계획 1.75 KB (부트로더)</text>
  <text x="410" y="228" fill="currentColor">어느 링커 스크립트도 할당 안 함 → 리셋을 건너 남는다</text>
  <path d="M340 152 L400 92" stroke="currentColor" fill="none" stroke-dasharray="3 3"/>
  <text x="300" y="250" fill="currentColor">VTOR 가 가리키는 표가 "지금 누구의 핸들러가 불리나" 를 정한다:</text>
  <rect x="10" y="262" width="350" height="50" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="282" fill="currentColor">리셋 직후 VTOR = 0x00000000</text>
  <text x="20" y="300" fill="currentColor">→ 부트로더의 SysTick/IRQ7/Default 핸들러</text>
  <rect x="380" y="262" width="350" height="50" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="390" y="282" fill="currentColor">점프 직전 VTOR = 0x00004000 (256 B 정렬)</text>
  <text x="390" y="300" fill="currentColor">→ 앱의 SysTick/IRQ/Fault 핸들러</text>
  <path d="M360 287 L380 287" stroke="currentColor"/><path d="M380 287 l-8 -4 l0 8 z" fill="currentColor"/>
  <rect x="10" y="324" width="720" height="46" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="343" fill="currentColor">벡터 48 × 4 B = 192 B → VTOR 정렬 = 2^k ≥ 192 → 256 B (최소 128 B, ARMv7-M — 확인 권장)</text>
  <text x="20" y="361" fill="currentColor">부트로더 16 KB = STM32F4 섹터 0 → 앱 업데이트가 부트로더를 안 지운다 (RM0090 확인 권장)</text>
</svg>
```

### Part A — 두 이미지, 하나의 플래시, 그리고 점프

- 주어진 것: `boot.ld` (FLASH 16 KB @0x0, RAM 4 KB @0x2000D000), `app.ld` (FLASH 240 KB @0x4000, RAM 62 KB @0x20000000), 공유 헤더 `boot_app.h` (배치 상수, 메일박스 구조체, 레지스터 주소, 세미호스팅), 앱 `app.c` (완성본), `run_qemu.sh` (빌드 → `objcopy -O binary --pad-to=0x4000 --gap-fill=0xFF` → `cat` → `-kernel merged.bin`).
- 부트로더(`starter.c` → `solution.c`) 의 TODO 네 개:
  - **TODO 1** `app_vector_ok(vt)` — 지워진 슬롯 / SP 범위 / SP 8 정렬 / Thumb 비트 / 리셋 벡터 범위 검사, 첫 실패 코드 반환.
  - **TODO 2** `systick_stop()` — CTRL=0, CVR=0, ICSR.PENDSTCLR.
  - **TODO 3** `nvic_quiesce()` — UART0 끄기(CTRL=0, INTSTATUS W1C) → ICTR 로 줄 수 계산 → 모든 워드 ICER/ICPR → DSB/ISB.
  - **TODO 4** `boot_jump(vtor, sp, pc, primask)` — VTOR, DSB/ISB, CONTROL=0, MSP, PRIMASK, ISB, BX 를 **한 덩어리 인라인 asm** 으로.
- 앱은 naked `Reset_Handler` 첫 세 명령으로 MSP/PRIMASK/CONTROL 을 잡고, 진입 스냅샷을 메일박스에 적은 뒤 자기 SysTick 을 켜서 **자기 핸들러가 3 번 불리는지**(부트로더 핸들러 카운터는 0 그대로인지) 확인한다.

### Part B — 실수를 주입하고 증상을 본다 (한 QEMU 실행, 부팅 10 번)

매 부팅의 부트로더는 일부러 "더러운 상태" 를 만든다: SysTick 100 µs 동작 + **pending**, IRQ7 enable + **pending**, UART0 TX + TX 인터럽트 켜짐(IRQ1 pending), PRIMASK=1. 그리고 단계마다 한 가지를 빠뜨린다.

| 단계 | 주입 | 앱(또는 부트로더)이 잡아내는 증상 |
|---|---|---|
| 0, 8 | 없음 | 계약 OK, 앱 SysTick 3 회, 부트로더 핸들러 0 회 |
| 1 | VTOR 안 바꿈 | 진입 VTOR=0, 앱 SysTick 이 **부트로더 핸들러**로 (앱 0 회) |
| 2 | SysTick 안 끔 | 진입 SYST_CSR ENABLE=1, 앱 준비 전 tick |
| 3 | CTRL=0 만 (PENDSTCLR 잊음) | SysTick 은 꺼졌는데 tick 1 회가 **앱 첫 명령보다 먼저** |
| 4 | NVIC·UART 정리 안 함 | IRQ7 이 앱 진입 전에, UART0 켜진 채, IRQ1 pending 남음 |
| 5 | PRIMASK=1 로 점프 | 리셋 상태를 믿는 앱은 인터럽트를 영영 못 받음 |
| 6 | 리셋 벡터 bit0 지우고 BX | 첫 명령에서 INVSTATE → HardFault FORCED (QEMU 실측) |
| 7 | 앱 벡터 0xFFFFFFFF (지워진 슬롯) | 부트로더가 거부하고 머묾 |

### Part C — 부트로그와 QEMU 사실 확인

- 부팅 로그(bring-up 엔지니어가 읽는 형태)와 마지막 부팅의 요약 표 + 36 개 CHECK → `SYS_EXIT 0`.
- `run_qemu.sh probe`: SysTick CTRL=0 vs pending, ICER vs pending, VTOR 쓰기 가능 비트, CMSDK UART TX 인터럽트가 level 인지, 짝수 주소 BX. `loader` (합치지 않고 `-device loader`), `nopad` (패딩 없이 합침), `appalone` (앱 ELF 만 `-kernel`), `layout` (크기·세그먼트·hexdump·`boot_jump` 디스어셈블).

## 2. Problem (English)

> **Bootloader-to-application handoff on a Cortex-M3.**
>
> Flash layout: bootloader at `0x00000000` (16 KB), application slot at `0x00004000` (240 KB, vector table at the slot start). RAM for the app: `0x20000000`–`0x2000F800` (initial MSP = `0x2000F800`). Target: QEMU `mps2-an385`, 32 external IRQs.
>
> 1. `uint32_t app_vector_ok(const uint32_t *vt)` — return the first failing check: `ERASED` (both words `0xFFFFFFFF`), `SP_RANGE` (SP not in `(0x20000000, 0x2000F800]`), `SP_ALIGN` (SP not 8-byte aligned), `PC_THUMB` (bit0 of the reset vector clear), `PC_RANGE` (entry not in `[0x4000 + 4·48, 0x40000)`), else `OK`.
> 2. `void systick_stop(void)`, `void nvic_quiesce(void)` — leave no interrupt source enabled or pending: SysTick off and its pending bit cleared; UART0 disabled and its interrupt status cleared *before* clearing NVIC pending bits; all NVIC lines disabled and un-pended (number of words from `ICTR`).
> 3. `void boot_jump(uint32_t vtor, uint32_t sp, uint32_t pc, uint32_t primask)` — set `VTOR`, barrier, `CONTROL = 0`, `MSP = sp`, `PRIMASK = primask`, `ISB`, `BX pc`. No C code may touch the stack after `MSP` changes.
>
> Contract: the application starts as if from reset — `VTOR` = its table, `MSP` = its `vt[0]`, `PRIMASK = 0`, `CONTROL = 0`, SysTick off with nothing pending, every NVIC line disabled and not pending, UART0 off.
>
> Example (QEMU): app entry snapshot `VTOR=0x00004000 MSP=0x2000f800 PRIMASK=0 CONTROL=0 SYST_CSR=0 ISER0=0 ISPR0=0 UART0.CTRL=0`; app SysTick → app handler 3×, bootloader handler 0×. Erased slot → stays in the bootloader.
>
> Follow-ups: Why is `SYST_CSR = 0` not enough? Why clear the peripheral before `ICPR`? What do you see if you forget `VTOR`? Why must the jump be in assembly? What happens on `BX` to an even address? Who re-enables interrupts? When is this whole approach wrong, and what would you do instead?

## 3. 시작 코드

<!-- code: starter.c -->

같이 쓰는 파일 — 공유 헤더, 앱(완성본), 링커 스크립트 두 개, QEMU 사실 확인 펌웨어, 실행 스크립트:

<!-- code: boot_app.h -->

<!-- code: app.c -->

<!-- code: boot.ld -->

<!-- code: app.ld -->

`demo_probe.c` (QEMU probe), `run_qemu.sh`, `practice.c` (호스트 C), 실측 캡처 `qemu_solution.txt` / `qemu_probe.txt` 도 같은 디렉터리에 있다.

실행은 Lima VM `elinux` 안에서 (Mac 홈은 VM 에서 읽기 전용 → 스크립트가 `/tmp/BM-A03_work` 로 복사해 빌드). 부팅을 10 번 해도 **QEMU 프로세스는 하나**이고 마지막 부팅의 `SYS_EXIT` 로 끝난다 (`timeout 20` 안전망). Mac 에서:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-A03
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # 3/36, qemu exit=1
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"   # 36/36, qemu exit=0
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh probe"      # QEMU 사실 확인 P1~P6
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh layout"     # 크기, 세그먼트, hexdump, boot_jump 디스어셈블
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh loader"     # -kernel boot.elf -device loader,file=app.bin,addr=0x4000
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh nopad"      # 패딩 없이 합치면?
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh appalone"   # 앱 ELF 만 -kernel 하면?
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # practice.c (호스트 C)
```

시작 코드 실행 결과 (실제 출력 앞부분): `app_vector_ok` 스텁이 "전부 거부" 를 돌려주므로 앱은 한 번도 실행되지 않고, 부트로더는 매 단계 "머문다".

```text
   text	   data	    bss	    dec	    hex	filename
   6660	      0	      0	   6660	   1a04	starter.elf
   1800	      0	      0	   1800	    708	app.elf
merged.bin = 18184 bytes (boot 0x0000-0x3fff, app from 0x4000)
cold boot: no valid plan @0x2000f900
== boot #1 (bootloader @0x0, VTOR=0x00000000) ==
  [boot] step 0: NORMAL
  [boot] state before handoff: PRIMASK=1 SYST_CSR=0x00000007 PENDST=1 ISER0=0x00000080 ISPR0=0x00000082 UART0.CTRL=0x00000005 ticks=2 irq7=1
  [boot] app vector @0x00004000: SP=0x2000f800 PC=0x000041bd -> TODO
  [boot] no valid app -> staying in bootloader (recovery mode would wait for an image)
...
FAIL starter.c:201  R[k].outcome == OUT_RAN && R[k].refuse == VEC_OK  [step 0]
...
3/36 passed
qemu exit=1
```

TODO 1 만 채우면 점프 스텁이 그냥 돌아오고 하네스가 `RETURNED` 로 기록한 뒤 리셋한다 — 어떤 조합으로 채워도 시작 코드는 멈추지 않는다 (최대 부팅 14 회 가드 + `timeout 20`).

## 4. 힌트

<details><summary>힌트 1 — SP 범위의 경계는 어디가 포함인가</summary>

Cortex-M 스택은 full-descending: push 는 "먼저 SP 를 4 줄이고, 그 주소에 쓴다". 그래서 초기 SP 가 RAM 끝 주소(0x2000F800) 와 **같아도** 된다 — 첫 push 는 0x2000F7FC 에 들어간다. 반대로 SP == RAM 시작(0x20000000) 이면 첫 push 가 RAM 밖. 그래서 `(RAM_LO, APP_RAM_HI]`.
</details>

<details><summary>힌트 2 — SysTick 을 끄는 데 왜 세 줄이 필요한가</summary>

`demo_probe` P2 를 보라: 인터럽트를 막은 채 SysTick 이 한 번 wrap 하면 `ICSR.PENDSTSET=1`. 거기서 `SYST_CSR=0` 을 써도 **PENDSTSET 은 1 그대로**이고, `cpsie i` 하는 순간 핸들러가 한 번 돈다. pending 은 카운터가 아니라 NVIC/SCB 쪽 상태다. 순서도 중요: 먼저 끄고(CTRL=0) 그 다음에 지운다 — 반대로 하면 그 사이에 다시 pending 될 수 있다. CVR 쓰기는 COUNTFLAG 를 지워 앱이 깨끗한 CSR 을 보게 한다.
</details>

<details><summary>힌트 3 — ICPR 을 쳤는데 pending 이 다시 서는 이유</summary>

`demo_probe` P5: UART0 의 TX 인터럽트 상태가 1 인 동안(신호선 high) ICPR 로 IRQ1 pending 을 지워도 ISPR0 은 `0x00000002` 그대로다. NVIC 는 level 신호가 살아 있으면 다시 pending 을 세운다. **원천(장치)을 먼저 끄고, NVIC 는 그 다음.**
</details>

<details><summary>힌트 4 — 왜 C 로 `__set_MSP(sp); ((void(*)(void))pc)();` 를 쓰면 위험한가</summary>

컴파일러는 MSP 가 바뀐 것을 모른다. `__set_MSP` 뒤에 지역 변수(`pc`)를 스택에서 다시 읽거나, 호출 전에 레지스터를 push 하면 **새 스택(앱 스택)** 을 건드리거나 엉뚱한 값을 읽는다. -O2 에서 우연히 레지스터에만 있어서 "동작" 하다가 -O0 디버그 빌드에서 깨지는 전형적인 버그. MSP 쓰기부터 BX 까지는 입력이 전부 레지스터인 asm 한 덩어리로.
</details>

<details><summary>힌트 5 — 점프 asm 의 모양</summary>

```c
SCB_VTOR = vtor; dsb(); isb();
__asm__ volatile("msr control, %3\n isb\n msr msp, %0\n msr primask, %2\n isb\n bx %1\n"
                 :: "r"(sp), "r"(pc), "r"(primask), "r"(0u) : "memory");
```

`primask` 는 호출자가 계약값(0)을 넘긴다. 정리가 다 끝났으니 PRIMASK 를 0 으로 해도 들어올 인터럽트가 없다 — **정리가 하나라도 빠졌다면 바로 이 `msr primask` 에서 터진다**(단계 3, 4).
</details>

## 5. 풀이

### 5.1 Part A — 이미지 두 개를 하나로: 무엇이 어디에 놓이나

두 이미지는 링커 스크립트의 `FLASH ORIGIN` 하나만 다르지만, 그 하나가 **벡터 테이블 주소, 모든 함수 주소(리셋 벡터 0x41BD), .data 의 LMA** 를 다 바꾼다. 그래서 앱은 "0x4000 에 놓일 때만" 맞는 바이너리다. 합치기는 run_qemu.sh 의 세 줄:

```bash
arm-none-eabi-objcopy -O binary app.elf app.bin                                   # LMA 가장 낮은 곳(0x4000) = 파일 오프셋 0
arm-none-eabi-objcopy -O binary --pad-to=0x4000 --gap-fill=0xFF boot.elf boot_pad.bin   # 정확히 16384 B 인지 검사
cat boot_pad.bin app.bin > merged.bin
```

QEMU 가 이걸 어떻게 로드하나? `hw/arm/armv7m.c` 의 `armv7m_load_kernel()` 은 먼저 `load_elf_as()` 를 시도하고 실패하면 `load_image_targphys_as(kernel_filename, mem_base, ...)` 로 raw 이미지를 로드한다. mps2 는 `armv7m_load_kernel(..., 0, 0x400000)` 로 부르므로 **raw 바이너리는 주소 0 에, 최대 4 MB** (QEMU v10.2.0 소스, 참고자료 링크). 실측으로 확인 (`run_qemu.sh layout`):

```text
--- arm-none-eabi-size ---
   text	   data	    bss	    dec	    hex	filename
   6968	      0	      0	   6968	   1b38	solution.elf
   1800	      0	      0	   1800	    708	app.elf
--- readelf -l (boot / app) ---
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  LOAD           0x000074 0x00000000 0x00000000 0x01b38 0x01b38 R E 0x4
  LOAD           0x001bac 0x2000d000 0x00001b38 0x00000 0x00000 RW  0x1
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  LOAD           0x000074 0x00004000 0x00004000 0x00708 0x00708 R E 0x4
  LOAD           0x00077c 0x20000000 0x00004708 0x00000 0x00000 RW  0x1
--- merged.bin: offset 0x0000 (boot vectors) / 0x3ff0 (padding) / 0x4000 (app vectors) ---
00000000: 00e0 0020 2d02 0000 6501 0000 6501 0000  ... -...e...e...
00003ff0: ffff ffff ffff ffff ffff ffff ffff ffff  ................
00004000: 00f8 0020 bd41 0000 a541 0000 a541 0000  ... .A...A...A..
--- symbols ---
00000000 R g_vectors
00000144 T SysTick_Handler
000001b0 t boot_jump
0000022c T Reset_Handler
2000e000 R _estack
00004000 R g_vectors
000041bc T Reset_Handler
000041cc T SysTick_Handler
00004274 T app_entry
2000f800 R _estack
```

읽는 법: 0x0 의 첫 두 워드 `0x2000E000`, `0x0000022D` = 부트로더 `_estack` 과 `Reset_Handler|1`. 0x4000 의 첫 두 워드 `0x2000F800`, `0x000041BD` = 앱 `_estack` 과 `Reset_Handler(0x41BC)|1`. 그 사이 0xFF 는 실제 NOR 플래시의 지워진 상태와 같다 (QEMU 의 SSRAM 자체는 0 으로 시작한다 — `nopad` 에서 그 차이가 보인다). `.data` 가 0 B 인 이유: 두 이미지 모두 초기값 있는 전역을 상수로만 쓰도록 -O2 가 접었다 (앱의 `g_app_data` 는 읽기만 해서 상수가 됐다 — 그래도 `.data` 복사 루프는 계약대로 둔다).

**같은 시험을 합치지 않고** 돌리는 방법도 확인했다: `-kernel solution.elf -device loader,file=app.bin,addr=0x4000` (`run_qemu.sh loader`) → `36/36 passed`, `qemu exit=0`. 제품 쪽에선 합친 하나의 `.bin`/`.hex` 가 공장 프로그래밍 단위이므로 이 레슨은 merged.bin 을 기본으로 했다.

### 5.2 Part A — 점프 절차: 한 줄씩 무엇을 막나

```svg
<svg viewBox="0 0 740 470" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">handoff(): 정리 순서와, 빠뜨렸을 때 QEMU 에서 실제로 본 증상</text>
  <rect x="10" y="30" width="330" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="20" y="52" fill="currentColor">0  cpsid i — 이 아래로는 아무것도 못 끼어든다</text>
  <rect x="10" y="74" width="330" height="48" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="20" y="94" fill="currentColor">1  SysTick: CSR=0 → CVR=0 → ICSR=PENDSTCLR</text>
  <text x="20" y="112" fill="currentColor">   (끄고 나서 지운다)</text>
  <rect x="10" y="132" width="330" height="48" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="20" y="152" fill="currentColor">2  장치 먼저: UART0 CTRL=0, INTSTATUS=0xF</text>
  <text x="20" y="170" fill="currentColor">   (신호선을 내린다)</text>
  <rect x="10" y="190" width="330" height="48" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="20" y="210" fill="currentColor">3  NVIC: ICTR → 워드 수, 모든 ICER / ICPR</text>
  <text x="20" y="228" fill="currentColor">   DSB, ISB</text>
  <rect x="10" y="248" width="330" height="34" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="20" y="270" fill="currentColor">4  VTOR = 0x4000, DSB, ISB</text>
  <rect x="10" y="292" width="330" height="34" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="20" y="314" fill="currentColor">5  CONTROL = 0, ISB (MSP · 특권)</text>
  <rect x="10" y="336" width="330" height="34" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="20" y="358" fill="currentColor">6  MSP = app[0] = 0x2000F800</text>
  <rect x="10" y="380" width="330" height="34" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="20" y="402" fill="currentColor">7  PRIMASK = 0 (계약), ISB ← pending 터지는 곳</text>
  <rect x="10" y="424" width="330" height="34" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="20" y="446" fill="currentColor">8  BX app[1] = 0x41BD (bit0 = Thumb)</text>
  <path d="M175 64 L175 74 M175 122 L175 132 M175 180 L175 190 M175 238 L175 248 M175 282 L175 292 M175 326 L175 336 M175 370 L175 380 M175 414 L175 424" stroke="currentColor"/>
  <text x="370" y="52" fill="currentColor">asm 한 덩어리 = 5~8 (MSP 바뀐 뒤 스택 사용 금지)</text>
  <path d="M345 300 L360 300 L360 450 L345 450" stroke="currentColor" fill="none"/>
  <text x="370" y="92" fill="currentColor">빠뜨리면 (단계 2) ENABLE=1, 준비 전 tick 2 회</text>
  <text x="370" y="110" fill="currentColor">PENDSTCLR 만 빠뜨리면 (3) 앱 첫 명령 전 tick 1 회</text>
  <text x="370" y="152" fill="currentColor">빠뜨리면 (4) UART0.CTRL=0x5 그대로,</text>
  <text x="370" y="170" fill="currentColor">IRQ1 pending 남음 (ISPR0=0x2)</text>
  <text x="370" y="210" fill="currentColor">빠뜨리면 (4) IRQ7 이 앱 진입 전에 1 회</text>
  <text x="370" y="228" fill="currentColor">ICPR 을 장치보다 먼저 (mutant) → ISPR0=0x2</text>
  <text x="370" y="270" fill="currentColor">빠뜨리면 (1) 앱 SysTick → 부트로더 핸들러 (91회 등)</text>
  <text x="370" y="314" fill="currentColor">PSP 로 돌던 부트로더라면 앱 스택이 엉뚱한 곳</text>
  <text x="370" y="358" fill="currentColor">빠뜨리면 부트로더 스택 위에서 앱이 돈다</text>
  <text x="370" y="402" fill="currentColor">1 로 넘기면 (5) 앱 SysTick 0 회 (영영 안 옴)</text>
  <text x="370" y="446" fill="currentColor">bit0=0 이면 (6) INVSTATE → HardFault FORCED</text>
</svg>
```

정답의 `boot_jump` 가 실제로 어떤 명령이 됐는지 (`run_qemu.sh layout`):

```text
000001b0 <boot_jump>:
     1b0:	f04f 2ce0 	mov.w	ip, #3758153728	@ 0xe000e000
     1b4:	f8cc 0d08 	str.w	r0, [ip, #3336]	@ 0xd08
     1b8:	f3bf 8f4f 	dsb	sy
     1bc:	f3bf 8f6f 	isb	sy
     1c0:	2000      	movs	r0, #0
     1c2:	f380 8814 	msr	CONTROL, r0
     1c6:	f3bf 8f6f 	isb	sy
     1ca:	f381 8808 	msr	MSP, r1
     1ce:	f383 8810 	msr	PRIMASK, r3
     1d2:	f3bf 8f6f 	isb	sy
     1d6:	4710      	bx	r2
```

스택 접근이 한 번도 없다 — 인자 4 개가 r0–r3 로 들어와 그대로 쓰인다. 그래서 MSP 를 바꾼 뒤에도 안전하다. (`noinline` 을 붙인 이유: 인라인되면 호출자의 스택 정리 코드와 섞일 수 있고, 디스어셈블로 확인하기도 어렵다.)

**왜 이 순서인가 — 규칙 두 개.** `practice.c` 연습 3 이 5 개 정리 동작(SYST_OFF, PENDSTCLR, UART_OFF, ICER, ICPR) 의 120 가지 순서를 QEMU 에서 관찰한 규칙(켜진 원천은 언제든 다시 pending, level 신호가 살아 있으면 ICPR 무효, ICER 는 pending 을 안 지움)으로 돌린 결과: **안전한 순서 30/120** 이고, 안전 ⇔ "SYST_OFF 가 PENDSTCLR 보다 먼저" 그리고 "UART_OFF 가 ICPR 보다 먼저" — 두 조건이 각각 1/2 씩 → 120/4 = 30. 그리고 하나라도 빼면 전부 UNSAFE. 일반화하면: **원천을 끄고 → 그 원천의 pending 을 지운다.**

**PRIMASK 계약 — 누가 `cpsie i` 를 하나.** 이 레슨의 계약은 "앱은 리셋 직후와 같은 상태로 시작한다 = PRIMASK 0". 이유: 대부분의 앱(벤더 HAL 템플릿 포함)은 리셋값 PRIMASK=0 을 가정하고 `__enable_irq()` 를 부르지 않는다(확인 권장 — 프로젝트마다 다르다). 그러면 부트로더가 PRIMASK=1 로 넘긴 순간 그 앱은 SysTick 하나 못 받는다 — 단계 5 가 정확히 그것(앱 SysTick 0 회). 반대 계약("PRIMASK=1 로 넘기고 앱이 직접 켠다")도 정당한 선택이지만, 그러면 **앱의 Reset_Handler 가 cpsie 를 해야 한다는 것을 문서로 고정**해야 한다. 어느 쪽이든 PRIMASK=0 으로 넘기려면 그 전에 모든 원천이 꺼지고 pending 이 0 이어야 한다 — 그래서 `msr primask` 는 정리의 "검증 지점" 역할을 한다.

### 5.3 Part B — 주입 결과 (실측 요약 표)

`run_qemu.sh solution` 마지막 부팅의 출력 (전체는 `qemu_solution.txt`):

```text
== handoff summary (QEMU mps2-an385, bootloader 0x0 -> app 0x4000) ==
step inject          outcome  vec      e_VTOR     e_MSP      PM SYST_CSR   ISPR0      early t/i apptk boottk fault
0    NORMAL          ran      OK       0x00004000 0x2000f800 0  0x00000000 0x00000000 0/0       3     0
1    NO_VTOR         ran      OK       0x00000000 0x2000f800 0  0x00000000 0x00000000 0/0       0     91
2    SYSTICK_RUNNING ran      OK       0x00004000 0x2000f800 0  0x00010007 0x00000000 2/0       3     0
3    PENDST_LEFT     ran      OK       0x00004000 0x2000f800 0  0x00000000 0x00000000 1/0       3     0
4    NVIC_LEFT       ran      OK       0x00004000 0x2000f800 0  0x00000000 0x00000002 0/1       3     0
5    PRIMASK_SET     ran      OK       0x00004000 0x2000f800 1  0x00000000 0x00000000 0/0       0     0
6    THUMB_CLEAR     FAULT    OK       0x00000000 0x00000000 0  0x00000000 0x00000000 0/0       0     0    exc3 CFSR=0x00020000 pc=0x000041bc
7    ERASED_APP      REFUSED  ERASED 0x00000000 0x00000000 0  0x00000000 0x00000000 0/0       0     0
8    NORMAL_AGAIN    ran      OK       0x00004000 0x2000f800 0  0x00000000 0x00000000 0/0       3     0
36/36 passed
qemu exit=0
```

단계별로 읽기:

- **1 NO_VTOR** — `boottk` (앱이 SysTick 을 켠 동안 부트로더 핸들러가 불린 횟수)는 실시간에 따라 실행마다 다르다 (82, 90, 91, 102, 105 를 봤다). 그래서 CHECK 은 `> 0` 과 `app_ticks == 0` 만 본다. 부트로더의 SysTick 핸들러가 메일박스 카운터 하나만 올리게 만든 것이 핵심이다 — 진짜 부트로더 핸들러라면 자기 `.bss` 변수를 쓸 텐데, 그 주소는 이제 **앱의 RAM** 이다 (0x2000D000 대는 앱 RAM 범위 안). 즉 VTOR 를 잊으면 부트로더 코드가 앱 메모리를 조용히 고쳐 쓴다.
- **2 SYSTICK_RUNNING** — 진입 시 `SYST_CSR=0x00010007` (COUNTFLAG|CLKSOURCE|TICKINT|ENABLE). early tick 2 회: pending 1 회 + 앱 초기화 중 한 번 더 (실시간 의존, CHECK 은 `≥ 1`).
- **3 PENDST_LEFT** — 결정적으로 **정확히 1 회**, 그것도 `early_stage = 0` (앱 Reset_Handler 의 첫 명령보다 먼저). `msr primask, r3` 직후 pending 이 받아들여지고, 복귀 주소는 `bx r2` — 앱의 SysTick 핸들러가 앱 스택 위에서, 앱 .data/.bss 초기화 전에 돈다. 앱 핸들러가 `.bss` 변수를 올렸다면 그 값은 곧 Reset_Handler 의 0 채우기로 지워진다 = "첫 tick 이 사라지는" 버그.
- **4 NVIC_LEFT** — IRQ7 이 앱 진입 전 1 회 (`early_irq_num=7`, stage 0). 진입 스냅샷에 `ISPR0=0x00000002` (UART0 TX = IRQ1, NVIC 에서 enable 된 적도 없는데 pending — disable 된 줄도 pending 은 쌓인다), `ICSR=0x00400800` (ISRPENDING bit22), `UART0.CTRL=0x00000005`. 이 앱이 나중에 UART 인터럽트를 켜는 순간 **옛 pending 이 즉시 터진다** (probe P3: "later ISER (stale pending): ran 1x").
- **5 PRIMASK_SET** — 진입 PRIMASK=1, 앱 SysTick 0 회 (300 만 회 반복 상한까지 기다림).
- **6 THUMB_CLEAR** — 아래 5.4.
- **7 ERASED_APP** — 부트로더가 슬롯 첫 두 워드를 0xFFFFFFFF 로 바꾼 뒤 검사 → `ERASED` → 머묾. **8 NORMAL_AGAIN** 이 다시 성공하고 벡터가 단계 0 과 같다는 CHECK 이 "QEMU 는 리셋마다 raw 이미지(ROM blob)를 다시 써 넣는다" 를 실측으로 보여 준다 (BM-M13 이 ELF 로 확인한 것과 같은 성질, 여기선 raw 바이너리로).

### 5.4 Part B — Thumb 비트: 짝수 주소로 BX 하면

Cortex-M 은 Thumb 상태만 있다. `BX`/`BLX` 의 주소 bit0 은 "다음 상태" 를 정하는데, 0 이면 EPSR.T=0 이 되고 **그 다음 명령을 실행하려는 순간** INVSTATE UsageFault 가 난다 (ARMv7-M ARM 규칙 — 확인 권장). UsageFault 가 꺼져 있으면(리셋 기본값, SHCSR=0) HardFault 로 승격된다. QEMU 실측 두 번:

```text
P6 BX to 0x000001a8 (target_fn with bit0 cleared), SHCSR=0x00000000
P6 fault taken: IPSR=3 CFSR=0x00020000 HFSR=0x40000000 stacked PC=0x000001a8 stacked xPSR=0x20000000
```

```text
  [app] FAULT exc=3 CFSR=0x00020000 HFSR=0x40000000 stacked PC=0x000041bc -> reset
```

CFSR `0x00020000` = INVSTATE (bit 17), HFSR `0x40000000` = FORCED, 쌓인 PC = 목적지(짝수), 쌓인 xPSR 의 T 비트(bit 24) = 0. **누가 이 fault 를 받았나?** VTOR 는 이미 0x4000 이었으므로 **앱의** HardFault 핸들러다 — 앱의 첫 명령도 돌기 전에 앱의 fault 핸들러가 불렸다(`f_stage = 0`). 그래서 앱의 fault 핸들러도 `.data/.bss` 에 기대면 안 된다. 실제 버그 모양: `uint32_t entry = (uint32_t)&app_reset_handler_symbol;` 처럼 **함수 포인터가 아닌 심볼 주소**나 `vt[1] & ~1` 같은 "정렬" 코드로 bit0 을 잃는 경우.

### 5.5 Part B — 앱 벡터 sanity check 의 경계

부트로더 마지막 부팅의 단위 시험 14 개(`tv[]`) 가 경계를 고정한다:

| SP | PC | 결과 | 이유 |
|---|---|---|---|
| 0xFFFFFFFF | 0xFFFFFFFF | ERASED | 지워진 슬롯 (가장 흔한 "앱 없음") |
| 0x2000F800 | 0x00004101 | OK | SP == RAM 끝 허용 (full-descending) |
| 0x2000F808 | 0x00004101 | SP_RANGE | 메일박스 위 |
| 0x20000000 | 0x00004101 | SP_RANGE | 0 바이트 스택 |
| 0x2000F7FC | 0x00004101 | SP_ALIGN | AAPCS 8 B 정렬 위반 |
| 0x2000F800 | 0x00004100 | PC_THUMB | bit0 = 0 |
| 0x2000F800 | 0x00004001 | PC_RANGE | 벡터 테이블 안을 가리킴 |
| 0x2000F800 | 0x000040BF / 0x000040C1 | PC_RANGE / OK | 테이블 끝 = 0x4000 + 48·4 = 0x40C0 |
| 0x2000F800 | 0x00040001 / 0x0003FFFF | PC_RANGE / OK | 슬롯 끝 |
| 0x2000F800 | 0x00000101 | PC_RANGE | 부트로더 안 → 무한 재부팅 고리 |

이건 **모양** 검사일 뿐이다. 반쯤 쓰인 이미지도 벡터 두 워드는 멀쩡할 수 있다 — 전체 무결성은 BM-A04 (헤더 + CRC/SHA-256), 진위는 SEC 트랙의 서명 검증 몫이다. 그래도 이 검사는 "지워진 슬롯으로 점프 → 0xFFFFFFFF 를 SP 로 로드 → 즉시 fault → 리셋 → 반복" 하는 **벽돌 고리**를 공짜로 막는다.

### 5.6 Part C — QEMU 사실 확인 (`run_qemu.sh probe`)

```text
== BM-A03 QEMU probe (mps2-an385) ==
P1 ICTR=0x00000000 -> lines=32
P2 masked, COUNTFLAG seen: ICSR.PENDSTSET=1 | after CTRL=0: PENDSTSET=1 | cpsie -> handler ran 1x
P2 CTRL=0 + PENDSTCLR: PENDSTSET=0 | cpsie -> handler ran 0x
P3 IRQ7 en+pend (masked): ISPR0=0x00000080 | after ICER: ISER0=0x00000000 ISPR0=0x00000080 | cpsie: ran 0x
P3 later ISER (stale pending): ran 1x | ISPR then ICPR then ISER: ran 0x
P4 VTOR write->read: 0x00004000->0x00004000 0x00004004->0x00004000 0x00004080->0x00004080 0x00004100->0x00004100 0xffffffff->0xffffff80
P5 UART0 TX: CTRL=0x00000005 INTSTATUS=0x00000001 | ISPR0=0x00000002 | after ICPR (line still high): ISPR0=0x00000002
P5 UART INTCLEAR + CTRL=0, then ICPR: INTSTATUS=0x00000000 ISPR0=0x00000000 | cpsie: IRQ1 ran 0x
P6 BX to 0x000001a8 (target_fn with bit0 cleared), SHCSR=0x00000000
P6 fault taken: IPSR=3 CFSR=0x00020000 HFSR=0x40000000 stacked PC=0x000001a8 stacked xPSR=0x20000000
qemu exit=0
```

- **P1** INTLINESNUM=0 → 32 줄 → ICER/ICPR 1 워드. QEMU 소스(mps2.c)의 `num-irq = 32` 와 일치. 코드는 상수 대신 ICTR 을 읽어 칩이 바뀌어도 맞게 했다 (STM32F4 라면 82 줄 → 3 워드, 확인 권장).
- **P4** QEMU 는 VTOR 의 하위 7 비트만 0 으로 만든다 — `0x4080` 도 받아들인다. 그러나 ARMv7-M 규칙은 "벡터 수×4 이상인 2 의 거듭제곱 정렬" 이라 48 벡터면 256 B 정렬이 필요하다(`practice.c` 연습 1). 즉 **QEMU 에서 돌았다는 것이 정렬이 맞다는 증거가 아니다**. 그래서 `app.ld` 에 `ASSERT((ADDR(.isr_vector) & 0xFF) == 0)` 을 넣었다.
- **P5** CMSDK UART 의 TX 인터럽트는 INTSTATUS 가 지워질 때까지 신호를 들고 있다(level). QEMU 소스상 UART0 RX=IRQ0, TX=IRQ1 (`uartirq[] = {0, 2, 4, 18, 20}`, "TX irq is always one greater").
- IRQ7 은 mps2-an385 에서 장치가 연결되지 않은 줄이라(QEMU mps2.c: UART 0/1/2/3/4, 타이머 8/9, 듀얼타이머 10, UART 오버플로 12) 부트로더의 "플래시 컨트롤러 IRQ" 역할로 썼다.

### 5.7 Part C — 부트로그 한 부팅 읽기

```text
== boot #1 (bootloader @0x0, VTOR=0x00000000) ==
  [boot] step 0: NORMAL
  [boot] state before handoff: PRIMASK=1 SYST_CSR=0x00000007 PENDST=1 ISER0=0x00000080 ISPR0=0x00000082 UART0.CTRL=0x00000005 ticks=3 irq7=1
  [boot] app vector @0x00004000: SP=0x2000f800 PC=0x000041bd -> OK
  [app] entry: VTOR=0x00004000 MSP=0x2000f800 PRIMASK=0 CONTROL=0 SYST_CSR=0x00000000 ICSR=0x00000800
          ISER0=0x00000000 ISPR0=0x00000000 UART0.CTRL=0x00000000 early ticks/irqs=0/0 .data=0xa5a5a5a5 .bss=0x00000000
  [app] contract: OK
  [app] SysTick test: app handler 3x, bootloader handler 0x
```

bring-up 엔지니어가 보는 순서: (1) 부트로더가 어떤 상태에서 넘겼나 — 더러운 것 5 개(SysTick 동작+pending, IRQ7 en+pend, IRQ1 pend, UART 켜짐, PRIMASK=1), (2) 앱 벡터가 기대대로인가, (3) 앱이 본 상태가 리셋과 같은가 — `ICSR=0x00000800` 은 RETTOBASE(bit 11) 하나, 나머지 0. 리셋 직후 부팅과 구별할 수 없는 상태 = 계약 성공. `SYST_CSR=0` 인 것은 `CVR=0` 덕분이다: CVR 쓰기를 넣기 전 실행에서는 앱이 `SYST_CSR=0x00010000` (COUNTFLAG 만 1) 을 봤다 — CTRL=0 쓰기가 COUNTFLAG 를 지우지 않았다는 실측.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">단계 3 (PENDSTCLR 잊음): pending tick 이 앱보다 먼저 도는 순간</text>
  <line x1="20" y1="70" x2="720" y2="70" stroke="currentColor"/>
  <rect x="20" y="40" width="170" height="30" class="f-acc" stroke="currentColor"/>
  <text x="28" y="60" fill="currentColor">부트로더 cpsid, 정리</text>
  <rect x="190" y="40" width="130" height="30" class="f-ok" stroke="currentColor"/>
  <text x="198" y="60" fill="currentColor">VTOR, MSP 설정</text>
  <rect x="320" y="40" width="110" height="30" class="f-warn" stroke="currentColor"/>
  <text x="326" y="60" fill="currentColor">msr primask,0</text>
  <rect x="430" y="40" width="70" height="30" class="f-mut" stroke="currentColor"/>
  <text x="446" y="60" fill="currentColor">bx</text>
  <rect x="500" y="40" width="220" height="30" class="f-ok" stroke="currentColor"/>
  <text x="508" y="60" fill="currentColor">앱 Reset_Handler: 스냅샷 → .bss</text>
  <path d="M375 70 L375 110" stroke="currentColor"/><path d="M375 110 l-4 -8 l8 0 z" fill="currentColor"/>
  <rect x="250" y="110" width="270" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="260" y="130" fill="currentColor">앱 SysTick_Handler (VTOR=0x4000)</text>
  <text x="260" y="146" fill="currentColor">stage = 0, 앱 스택 0x2000F800 에서</text>
  <text x="260" y="160" fill="currentColor">early_ticks = 1  → 복귀 주소 = bx</text>
  <path d="M520 138 L465 138 L465 70" stroke="currentColor" fill="none" stroke-dasharray="3 3"/>
  <text x="20" y="196" fill="currentColor">앱 .bss 카운터를 올렸다면 → 곧 Reset_Handler 의 0 채우기가 지운다 (tick 증발).</text>
  <text x="20" y="216" fill="currentColor">드라이버 상태를 만졌다면 → 초기화 안 된 RAM 을 따라간다 (HW 랜덤, QEMU 0).</text>
  <text x="20" y="236" fill="currentColor">VTOR 까지 잊었다면 → 같은 tick 이 부트로더 핸들러로 → 증상이 숨는다 (실수 3).</text>
</svg>
```

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

검증 (Lima VM, QEMU 10.2.1): `run_qemu.sh solution` → `36/36 passed`, `qemu exit=0` (부팅 10 회, 한 QEMU 프로세스). `run_qemu.sh loader` 도 `36/36`. `run_qemu.sh starter` → `3/36`, `qemu exit=1`.

결함 주입 확인 (정답 사본에 일부러 버그를 넣어 하네스가 잡는지, 레슨 디렉터리 밖에서):

| 변형 | 결과 |
|---|---|
| `systick_stop` 에서 PENDSTCLR 삭제 | 34/36, 단계 0·8 `check_contract_ok` FAIL (early tick) |
| `nvic_quiesce` 에서 ICPR 을 UART 끄기보다 먼저 | 34/36, 단계 0·8 FAIL (진입 ISPR0=0x2) |
| SP 검사 `sp > HI` → `sp >= HI` (off-by-one) | 10/36: 정상 앱(SP=0x2000F800)을 거부 → 점프하는 모든 단계 + 단위 시험 8 개 FAIL |
| `boot_jump` 에서 VTOR 쓰기 삭제 | 모든 단계 계약 `VTOR` 위반, 단계 6 의 HardFault 가 **부트로더** Default_Handler 로 → `qemu exit=3` |
| PC 범위 하한 `APP_BASE + 4·48` → `APP_BASE` | 34/36, 단위 시험 2 개 FAIL (벡터 테이블 안을 가리키는 PC 통과) |

## 7. 자주 하는 실수

**실수 1 — NVIC 를 먼저 지우고 장치는 나중에.** `ICPR = 0xFFFFFFFF; UART0_CTRL = 0;` — 장치가 아직 신호를 들고 있는 동안 지운 pending 은 즉시 다시 선다 (probe P5, mutant 2). 앱은 UART 인터럽트를 켜는 순간 "아무것도 안 받았는데 RX 인터럽트" 를 본다. 고친 순서: 장치 끄기 + 장치 쪽 상태 W1C → 그 다음 ICER/ICPR.

**실수 2 — `SysTick->CTRL = 0;` 한 줄.** 이미 걸린 PENDSTSET 은 남는다 (probe P2: "after CTRL=0: PENDSTSET=1"). 고친 코드: `SYST_CSR = 0; SYST_CVR = 0; SCB_ICSR = ICSR_PENDSTCLR;` — 끄고 나서 지운다.

**실수 3 — VTOR 를 잊었는데 "잘 돈다".** 앱이 인터럽트를 안 쓰는 동안은 아무 증상이 없다. 증상이 나와도 엉뚱하다: mutant 4 에서 단계 3 의 early tick 은 **부트로더** 핸들러로 가서 앱의 early 카운터에 안 잡혔고, 단계 6 의 HardFault 는 부트로더의 Default_Handler 가 받아 "부트로더가 죽었다" 처럼 보였다. 고친 코드: 점프 직전 `SCB_VTOR = APP_BASE; dsb(); isb();` — 그리고 앱의 SystemInit 에서도 VTOR 를 자기 테이블로 **한 번 더** 쓰는 방어(벤더 템플릿의 `VECT_TAB_OFFSET` 류, 확인 권장).

**실수 4 — 패딩 없이 합치기.** `cat boot.bin app.bin > merged.bin` 이면 앱이 부트로더 바로 뒤(0x1B38)에 놓인다 (`run_qemu.sh nopad`):

```text
boot.bin (no pad) = 6968 bytes -> app lands at 0x1b38
  [boot] step 0: NORMAL
  [boot] app vector @0x00004000: SP=0x00000000 PC=0x00000000 -> SP_RANGE
```

0x4000 에는 아무것도 없다 — QEMU SSRAM 은 0 으로 시작해서 `0x00000000` 이 보이지만, 실제 플래시라면 `0xFFFFFFFF` (ERASED) 였을 것. sanity check 덕분에 벽돌 대신 "머묾" 이 됐다. 고친 코드: `objcopy --pad-to=0x4000 --gap-fill=0xFF` + 크기 검사 (run_qemu.sh `merge()`).

**실수 5 — 앱 ELF 만 `-kernel` 로.** 디버깅하려고 앱만 올리면 (`run_qemu.sh appalone`):

```text
qemu: fatal: Lockup: can't escalate 3 to HardFault (current priority -1)
R12=00000000 R13=ffffffe0 R14=fffffff9 R15=00000000
XPSR=40000003 -Z-- A handler
qemu exit=134
```

리셋은 VTOR=0 → 주소 0 의 표를 읽는다. 거기엔 아무것도 없으니 SP=0, PC=0 (bit0=0) → INVSTATE → HardFault 벡터도 0 → Lockup. 앱 단독 디버깅은 부트로더와 같이 올리거나, gdb 에서 `set $sp = ...; set $pc = ...; set *0xE000ED08 = 0x4000` 처럼 부트로더 역할을 손으로.

**실수 6 — C 에서 MSP 를 바꾸고 함수 포인터로 호출.**

```c
/* 위험: -O0 에서는 app_reset 이 스택 변수 → __set_MSP 뒤에 "새 스택" 에서 읽는다 */
void (*app_reset)(void) = (void (*)(void))vt[1];
__set_MSP(vt[0]);
app_reset();          /* BLX: LR 을 남기고, 호출 전 push 가 끼어들 수 있다 */
```

고친 코드: 5.2 의 asm 한 덩어리 (입력 전부 레지스터, `bx`, `__builtin_unreachable()`).

**실수 7 — 링커가 ELF 헤더를 주소 0 세그먼트에 넣는다.** `-Wl,-n` 없이 앱을 링크했을 때 실측: `LOAD 0x000000 0x00000000 0x00000000 0x04708 ...` — 0x4000 아래 빈 공간에 ELF/프로그램 헤더가 들어가면서 **세그먼트가 주소 0 부터** 시작했다. `-kernel app.elf` 나 ELF 를 쓰는 플래셔가 이걸 보면 부트로더 영역(0x0000–0x3FFF)을 헤더 바이트로 덮어쓴다. `objcopy -O binary` 는 섹션 기준이라 영향이 없었지만(app.bin = 1800 B), 고친 빌드는 `-Wl,-n` (nmagic, 페이지 정렬 끔) → `LOAD 0x000074 0x00004000 ...`.

## 8. 면접 Q&A

<details><summary>Q1. 부트로더에서 앱으로 점프할 때 무엇을, 어떤 순서로 정리하나요?</summary>

`cpsid i` → 부트로더가 쓴 모든 인터럽트 원천 끄기(SysTick: CTRL=0·CVR=0·PENDSTCLR, 장치: 인터럽트 enable 끄고 상태 W1C) → NVIC 모든 줄 ICER + ICPR → VTOR = 앱 테이블, DSB/ISB → CONTROL=0 → MSP = app[0] → PRIMASK 계약값 → ISB → `BX app[1]`. 규칙은 "원천 먼저, pending 은 그 다음" (연습 3: 120 순서 중 30 만 안전). 클럭·DMA·캐시를 바꿨다면 그것도 되돌린다 (Q8).

꼬리: "왜 cpsid 가 맨 앞인가?" — 정리하는 도중에 부트로더 핸들러가 돌면 방금 끈 것을 다시 켤 수 있다 (예: UART RX 핸들러가 다음 수신을 위해 인터럽트를 재활성).
</details>

<details><summary>Q2. SysTick 은 CTRL=0 만 쓰면 되지 않나요?</summary>

아니다. pending 은 SysTick 블록이 아니라 SCB 의 ICSR.PENDSTSET 에 있다. QEMU 실측(probe P2): 마스크된 상태에서 wrap → PENDSTSET=1 → CTRL=0 후에도 1 → `cpsie` 하자마자 핸들러 1 회. 단계 3 에선 그 1 회가 앱의 첫 명령보다 먼저 돌았다. PENDSTCLR 로 지우되 **끈 다음에** (반대 순서면 사이에 다시 pending). 추가로 CVR 쓰기는 COUNTFLAG 를 지운다(ARMv7-M: CSR 읽기 또는 CVR 쓰기로 클리어 — 확인 권장). CTRL=0 은 COUNTFLAG 를 지우지 않았다는 것도 실측(앱이 SYST_CSR=0x00010000 을 봄).
</details>

<details><summary>Q3. ICER 로 다 disable 했는데 왜 ICPR 도 필요하고, 왜 장치를 먼저 꺼야 하나요?</summary>

ICER 는 enable 만 지우고 pending 은 남긴다 (probe P3: ICER 후 ISPR0=0x80). 그 pending 은 **앱이 그 IRQ 를 enable 하는 순간** 터진다("later ISER (stale pending): ran 1x"). 그리고 level 신호를 내는 장치가 신호를 들고 있으면 ICPR 이 먹히지 않는다 (P5: ICPR 후에도 ISPR0=0x2). 그래서 장치 쪽 원인을 먼저 끄고 지운 뒤 ICPR. pulse 성 인터럽트(소프트웨어 pend, 일부 edge 장치)는 ICPR 한 번이면 되지만, 어느 것이 level 인지 외우기보다 순서를 고정하는 편이 안전하다.
</details>

<details><summary>Q4. VTOR 를 안 바꾸면 어떻게 되고, 왜 그 버그는 오래 숨어 있나요?</summary>

앱의 모든 예외가 부트로더 테이블로 간다. 실측(단계 1): 앱이 켠 SysTick 이 부트로더 핸들러로 91 회, 앱 핸들러 0 회. 숨는 이유: (1) 앱이 인터럽트를 쓰기 전까지 증상이 없다, (2) 부트로더 핸들러가 "그럴듯하게" 동작한다 — 그런데 그 핸들러가 쓰는 부트로더 .bss 주소는 이제 앱 RAM 이라 앱 변수를 조용히 덮는다, (3) fault 도 부트로더로 가서 "부트로더가 죽었다" 로 보인다(mutant 4: 앱의 INVSTATE 를 부트로더 Default_Handler 가 받음). 방어: 앱 SystemInit 에서 VTOR 를 자기 테이블로 다시 쓰고, 앱 진입 검사에서 `SCB->VTOR == &g_vectors` 를 assert.

꼬리: "VTOR 정렬은?" — 벡터 수×4 이상인 2 의 거듭제곱, 최소 128 B. 48 벡터 → 256 B, STM32F407(98 벡터) → 512 B (연습 1, 확인 권장). QEMU 는 하위 7 비트만 지우므로(P4) 정렬 위반을 잡아 주지 않는다.
</details>

<details><summary>Q5. 왜 점프는 어셈블리여야 하나요? __set_MSP 후 함수 포인터 호출은?</summary>

MSP 를 바꾼 순간부터 컴파일러가 만든 스택 접근(spill 된 지역 변수 다시 읽기, 호출 전 push)은 앱 스택을 건드리거나 쓰레기를 읽는다. -O2 에선 운 좋게 레지스터에만 있어서 되고 -O0 에서 깨지는, 최적화 레벨 의존 버그가 된다. 그리고 `BLX` 로 부르면 LR 에 부트로더 주소가 남는다 — 앱이 실수로 main 에서 return 하면 부트로더 한가운데로 돌아간다. 정답의 `boot_jump` 디스어셈블은 스택 접근이 0 개(인자 r0–r3 → `msr`/`bx`).
</details>

<details><summary>Q6. 리셋 벡터의 bit0 이 0 이면? 실제 칩과 QEMU 가 같나요?</summary>

Cortex-M 은 Thumb 만 실행한다. bit0=0 으로 BX 하면 EPSR.T=0 이 되고 다음 명령 실행 시 INVSTATE UsageFault, UsageFault 가 꺼져 있으면 HardFault FORCED. QEMU 실측: `IPSR=3 CFSR=0x00020000 HFSR=0x40000000 stacked PC=0x000041bc`, 쌓인 xPSR T=0. 실제 HW 도 같은 아키텍처 규칙이다(ARMv7-M ARM — 확인 권장). 주의: 그 fault 는 이미 VTOR 가 바뀐 뒤라 **앱의** HardFault 핸들러가 받는다 — 앱이 한 명령도 안 돌았는데 앱 fault 로그가 남는 혼란. 반대로 리셋 시 벡터[1] 의 bit0 이 0 이면 리셋 직후 바로 같은 fault(그리고 VTOR=0 표가 비어 있으면 Lockup — 실수 5).
</details>

<details><summary>Q7. 점프 후 PRIMASK 는 누가 켜나요? 계약을 어떻게 정하나요?</summary>

이 레슨: "앱은 리셋과 같은 상태로 시작 = PRIMASK 0" — 그래서 부트로더가 모든 원천을 정리한 뒤 `msr primask, 0` 하고 점프. 리셋값을 믿는 앱(대부분)이 그대로 동작한다. 반대 계약(PRIMASK=1 로 넘기고 앱이 직접 `cpsie`)은 "정리가 덜 됐어도 앱이 준비될 때까지 안 터진다" 는 장점이 있지만, 앱이 그걸 모르면 단계 5 처럼 인터럽트를 영영 못 받는다 (앱 SysTick 0 회). 어느 쪽이든 **문서화 + 앱 진입 assert** (`e_primask == 계약값`). FAULTMASK, BASEPRI 도 같은 대상이다 (ARM-M10, 다음 단계).
</details>

<details><summary>Q8. (staff) 이 "정리 후 점프" 설계가 틀리는 경우는? 어떻게 바꾸겠나?</summary>

부트로더가 바꾼 상태가 늘어날수록 되돌리기 목록이 길어지고 하나라도 빠지면 앱은 "리셋과 다른" 칩을 받는다: PLL/클럭 트리와 플래시 wait state(앱의 SystemInit 이 "HSI 에서 시작" 을 가정하면 PLL 재설정이 실패할 수 있다 — STM32 류, 확인 권장), 진행 중인 **DMA**(점프 후에도 RAM 에 쓴다 — 앱 .bss 를 덮음), 캐시(Cortex-M7: D-cache 에 남은 dirty 라인, I-cache 에 부트로더 코드), MPU 영역, FPU(CPACR, FPCCR lazy stacking — BM-A01), PSP/CONTROL.SPSEL, SHCSR/CCR 트랩, 예외 우선순위(IPR/SHPR), 워치독(한 번 켜면 못 끈다 — 앱이 곧바로 킥해야 함).

대안 — **리셋을 거쳐 점프**: 부트로더는 "앱으로 가라" 플래그(magic)를 .noinit RAM 에 쓰고 SYSRESETREQ. 리셋 후 부트로더의 Reset_Handler **맨 앞**(클럭·장치 아무것도 건드리기 전)에서 플래그를 보고, 지우고, VTOR/MSP 만 설정해 바로 점프. 하드웨어 리셋이 위 목록을 전부 되돌려 주므로 정리 코드가 필요 없다. 단점: 리셋 원인 레지스터(RCC_CSR)가 "소프트웨어 리셋" 으로 바뀌고, 리셋에 안 지워지는 것(백업 도메인, 이미 켠 IWDG)은 여전히 남는다. 이 레슨의 시험 하네스가 정확히 이 플래그(+SYSRESETREQ) 메커니즘으로 단계를 넘긴다 — BM-M13 의 .noinit 기록과 같은 원리.
</details>

<details><summary>Q9. (staff) 앱을 A/B 두 슬롯 중 하나에서 실행해야 한다면 무엇이 바뀌나?</summary>

앱은 링크 주소에 묶여 있다(리셋 벡터 0x41BD, 모든 절대 주소). 선택지: (1) 슬롯마다 따로 링크한 두 이미지를 배포(업데이트 서버가 "비활성 슬롯용" 을 고름) — 단순하고 흔하다, (2) 항상 한 슬롯(primary)에서 실행하고 업데이트는 secondary 에 받아 **swap/copy** (MCUboot 방식 — BM-A06, PLAT-M07 다음 단계), (3) 일부 MCU 의 플래시 뱅크 리맵(STM32 dual-bank BFB2 류 — 확인 권장) 으로 같은 주소에 보이게, (4) 위치 독립 코드(-fpic + GOT, `-msingle-pic-base`)는 Cortex-M 에서 가능은 하지만 벡터 테이블·.data 초기값 재배치가 필요해 드물다. 부트로더 쪽은 `APP_BASE` 가 상수에서 "검증된 슬롯 주소" 로 바뀌고, sanity check 범위·VTOR 값도 슬롯별이 된다.
</details>

<details><summary>Q10. 벡터 sanity check 만으로 "앱이 유효하다" 고 할 수 있나요?</summary>

아니다. 이건 "명백히 비었거나 망가진" 슬롯을 거르는 첫 관문이다: 지워진 슬롯(0xFFFFFFFF), SP 범위/정렬, Thumb 비트, 리셋 벡터가 슬롯 안·테이블 밖. 업데이트 도중 전원이 나가 앞부분만 쓰인 이미지는 통과할 수 있다. 무결성은 헤더 + CRC-32/SHA-256 (BM-A04), 진위(누가 만들었나)는 서명 검증(SEC 트랙), 롤백 방지는 버전 카운터. 그리고 검사 실패 시 행동이 중요하다: 이 레슨처럼 **부트로더에 머물러 복구 경로**(YMODEM — BM-A05) 를 열어야지, 리셋 반복은 벽돌이다.
</details>

## 9. 추가 연습

### 연습 1 — VTOR 정렬 규칙

벡터 수 n 에 대해 필요한 정렬 `vtor_align(n)` (2 의 거듭제곱 ≥ 4n, 최소 128) 과 `vtor_base_ok(base, n)` 을 만들고, n = 1..512 와 base = 0..0xFFC0 (64 간격) 전수로 "가장 작은 2 의 거듭제곱" 성질을 검사하라. 이 레슨 보드와 대표 칩들의 표를 출력하라.

<details><summary>답</summary>

```text
연습 1  VTOR alignment
  chip                            nvec  align  0x4000 0x4080 0x4100
  mps2-an385 (this lesson)          48    256  ok     BAD    ok
  nRF52840 (48 IRQ, hedge)          64    256  ok     BAD    ok
  STM32F407 (82 IRQ, hedge)         98    512  ok     BAD    BAD
  STM32H743 (150 IRQ, hedge)       166   1024  ok     BAD    BAD
```

앱 슬롯을 섹터 경계(16 KB)에 두면 어떤 칩이든 정렬은 자동으로 맞는다. 위험한 건 "헤더 뒤에 벡터 테이블" 레이아웃(BM-A04 의 64 B 헤더처럼) — 그때 테이블 주소는 slot + hdr_size 가 되고, 0x4040 은 48 벡터에도 불합격. 그래서 MCUboot 류는 헤더 크기를 벡터 정렬에 맞춰 키운다(헤더 패딩 — 확인 권장). IRQ 수는 데이터시트 확인 권장.
</details>

### 연습 2 — 합친 이미지 만들기를 함수로

`merge_image(boot, blen, app, alen, slot, out, cap)` — 부트로더 + 0xFF 패딩 + 앱. 부트로더가 슬롯을 침범하면 `BOOT_TOO_BIG`, 출력 용량이 모자라면 `CAP`. 작은 크기 전수(slot 0..64 / 16 간격, blen·alen 0..80)로 내용, 패딩 바이트, 넘쳐 쓰지 않음을 검사하라.

<details><summary>답</summary>

`slot + alen` 은 64 비트로 더해 넘침을 막는다. 실측: `연습 2  merge_image: 32805 cases, 20480 rejected (boot too big / capacity)`. 패딩이 0x00 이 아니라 0xFF 인 이유: 실제 NOR 플래시의 지워진 값이고, 프로그래머가 그 영역을 "쓰지 않아도 되는 곳" 으로 건너뛸 수 있으며(쓰기 시간 절약), 0xFF 벡터는 sanity check 에서 `ERASED` 로 분류된다.
</details>

### 연습 3 — 정리 순서 모델: 120 가지 중 몇 개가 안전한가

QEMU probe 에서 관찰한 규칙 세 개(켜진 SysTick 은 언제든 다시 pending, level 신호가 살아 있으면 ICPR 무효, ICER 는 pending 을 안 지움)로 "적대적 하드웨어" 모델을 만들고, 5 개 정리 동작의 모든 순서를 시뮬레이션해 안전한 순서를 세라. 안전 ⇔ 어떤 조건인지 증명하듯 검사하라.

<details><summary>답</summary>

```text
연습 3  handoff order model: 30/120 orders safe; dropping one op -> -SYST_OFF:UNSAFE -PENDSTCLR:UNSAFE -UART_OFF:UNSAFE -ICER:UNSAFE -ICPR:UNSAFE
```

안전 ⇔ (SYST_OFF 가 PENDSTCLR 앞) ∧ (UART_OFF 가 ICPR 앞). 두 조건은 서로 다른 동작 쌍에 대한 것이라 독립 → 각각 순열의 절반 → 120 × ½ × ½ = 30. 모든 120 개에서 모델 결과 == 조건식 (CHECK). 하나라도 빠지면 UNSAFE.
</details>

### 연습 4 — 앱 쪽 계약 검사를 실측 스냅샷으로

`app.c` 의 `contract_check()` 를 호스트로 옮겨, `qemu_solution.txt` 에서 앱이 찍은 진입 스냅샷 6 개(단계 0~5)로 위반 집합을 계산하고, 앱이 QEMU 에서 출력한 `contract:` 줄과 같은지 검사하라. 부트로더가 PSP(CONTROL.SPSEL=1)로 돌다 그대로 점프한 경우도 하나 추가하라.

<details><summary>답</summary>

```text
연습 4  contract_check on QEMU snapshots: NORMAL=0x000 NO_VTOR=0x001 SYSTICK_RUNNING=0x210 PENDST_LEFT=0x200 NVIC_LEFT=0x380 PRIMASK_SET=0x004
```

0x210 = SYSTICK_ON|EARLY, 0x380 = NVIC_PEND|UART_ON|EARLY — QEMU 에서 앱이 찍은 `contract: SYSTICK_ON EARLY_IRQ`, `contract: NVIC_PEND UART_ON EARLY_IRQ` 와 같다. 단계 4 의 ISER0 이 0 인 이유: 앱의 IRQ 핸들러가 들어온 줄(IRQ7)을 폭주 방지로 끄기 때문 — 그래서 "enable 됐었나" 는 early_irqs 로 본다. CONTROL=2 사례는 V_CONTROL 하나만.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cd /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-A03
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-A03_pr && /tmp/BM-A03_pr     # 22154/22154 passed
# 또는 VM 에서: limactl shell elinux -- bash -lc "bash $PWD/run_qemu.sh practice"
```

## 10. 핵심 정리

- 앱은 링크 주소에 묶인다: 부트로더 0x0 (16 KB) / 앱 0x4000, `objcopy --pad-to --gap-fill=0xFF` 로 합치고, QEMU 는 raw 이미지를 주소 0 에 로드한다 (리셋마다 다시).
- 점프 = `cpsid` → **원천 끄기 → 그 pending 지우기** (SysTick CTRL/CVR/PENDSTCLR, 장치 끄기+W1C, NVIC ICER+ICPR) → VTOR+DSB/ISB → CONTROL=0 → MSP → PRIMASK(계약) → ISB → BX. 120 순서 중 30 만 안전.
- `CTRL=0` 은 pending 을, `ICER` 는 pending 을, `ICPR` 은 level 신호를 이기지 못한다 (QEMU probe P2/P3/P5 실측).
- MSP 를 바꾼 뒤엔 스택 금지 → 입력이 전부 레지스터인 asm 한 덩어리. bit0=0 이면 INVSTATE → HardFault FORCED (CFSR 0x00020000).
- 앱은 부트로더를 믿지 않는다: naked 진입에서 MSP/PRIMASK/CONTROL, VTOR, SysTick, NVIC, 장치 상태를 찍고 assert. 핸들러는 준비 전에 불릴 수 있으니 .bss 에 기대지 않는다.
- 벡터 sanity check(ERASED/SP/Thumb/PC 범위)는 벽돌 고리를 막는 첫 관문일 뿐 — 무결성은 CRC/SHA(BM-A04), 진위는 서명(SEC).
- 되돌릴 게 많으면 "플래그 + 리셋 + Reset_Handler 맨 앞에서 점프" 가 더 깨끗하다.

## 참고자료

- QEMU v10.2.0 `hw/arm/mps2.c` — SSRAM1 0x0 4 MB, SSRAM2&3 0x20000000, `num-irq = 32`, UART IRQ `{0, 2, 4, 18, 20}` (TX = RX+1), 타이머 8/9, 듀얼타이머 10, 워치독 → NMI: https://raw.githubusercontent.com/qemu/qemu/v10.2.0/hw/arm/mps2.c
- QEMU v10.2.0 `hw/arm/armv7m.c` `armv7m_load_kernel()` — ELF 먼저, 실패하면 raw 이미지를 `mem_base` 에: https://raw.githubusercontent.com/qemu/qemu/v10.2.0/hw/arm/armv7m.c
- ARMv7-M Architecture Reference Manual (ARM DDI 0403) — B1.5 예외 모델, B3.2 SCB (VTOR 정렬, ICSR PENDSTSET/PENDSTCLR), B3.3 SysTick (COUNTFLAG 클리어 조건), B3.4 NVIC (ICER/ICPR, level 입력) — 절 번호 확인 권장.
- Cortex-M3 Technical Reference Manual (ARM DDI 0337) — VTOR 구현 비트, ICTR.
- ARM Cortex-M System Design Kit (CMSDK) TRM — APB UART CTRL/INTSTATUS.
- STM32F4 RM0090 — 플래시 섹터 0~3 = 16 KB (확인 권장).
- MCUboot 문서 (부트 흐름, swap) — https://docs.mcuboot.com/ (다음 단계 BM-A06 / PLAT-M07).
- 관련 레슨: BM-B08 (startup, 벡터 테이블, 첫 부팅), BM-M13 (.noinit 기록 + SYSRESETREQ, QEMU 리셋 사실), BM-M02 (NVIC 우선순위 on QEMU), BM-A01 (CPACR/VTOR 순서), BM-A04 (이미지 헤더 + CRC/SHA), BM-A05 (YMODEM 복구), BM-A06 (전원 차단 안전 업데이트). 다음 단계: ARM-M08 (asm 벡터 테이블), ARM-M10 (PRIMASK/BASEPRI), ARM-M12 (CFSR 디코더), SEC (서명 검증), PLAT-M07 (A/B 슬롯 정책).
