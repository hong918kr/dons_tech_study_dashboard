# B5. 리셋부터 main까지 (MCU) — 벡터 두 워드, 링커 스크립트, Reset_Handler, 그리고 부팅 시간

> **이 노트를 다 읽으면**: 리셋 순간 Cortex-M 코어가 하는 일(벡터 두 워드 읽기)을 gdb로 증명할 수 있다 · 링커 스크립트를 한 줄씩 읽고 VMA/LMA · `AT>` · KEEP · NOLOAD · PROVIDE · ASSERT를 직접 써서 고칠 수 있다 · `Reset_Handler`(.data 복사, .bss 0, SystemInit, FPU, 생성자, main 복귀)를 C와 어셈블리로 처음부터 쓸 수 있다 · 부팅 시간이 어디서 나가는지 재고 줄일 수 있다
> **왜 필요한가**: "Walk me through what happens from reset to main"은 펌웨어 면접 단골이고, 새 보드 bring-up · RTOS 포팅 · "디버거로는 되는데 전원 껐다 켜면 안 돼요" 버그는 모두 이 구간에서 생긴다.
> **Don 기준 난이도**: 링커 스크립트 · map 파일 · startup 코드는 SSD 펌웨어에서 이미 만졌다 / 생성자(`__libc_init_array`)와 `-nostartfiles`의 함정, 컴파일러가 startup 루프를 `memset`으로 바꾸는 일, QEMU가 숨겨 주는 버그, Zephyr가 main 전에 하는 일은 새롭다
> **선행 노트**: B1(프로그래머 모델: MSP/PSP · CONTROL), B2(벡터 테이블 · VTOR), EL B3(ELF 섹션 · 세그먼트 · 재배치 — 다시 설명하지 않는다). 이어지는 노트: B6(FPU), B11(libc · syscall stub · 코드 크기), C3(첫 태스크 시작), F8(메모리 배치 실무)
> 🔁 **Bare-Metal 겹침**: BM F3(링커 스크립트 심화) · BM F4(startup 코드 직접 쓰기) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 "벤더 startup 파일(ST · NXP · Nordic)을 비교하며 읽는 법"만 더한다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 컨트롤러 펌웨어를 처음 받았을 때를 떠올려 보자. `main()`은 깔끔한 C 코드였지만, 그 앞에는 누군가 써 둔 어셈블리 `reset.S`와 링커 스크립트 `fw.ld`가 있었다. 평소에는 아무도 안 본다. 그런데 새 실리콘이 오거나, SRAM 뱅크가 바뀌거나, 부트 ROM이 펌웨어를 올리는 주소가 바뀌면 **그 두 파일을 아는 사람만** 일을 진행할 수 있었다.

MCU + RTOS 세계도 똑같다. `main()` 첫 줄이 실행되려면 다음이 끝나 있어야 한다.

- 스택 포인터가 RAM 어딘가를 가리킨다 (C 함수 호출이 가능해진다)
- 초기값 있는 전역 변수(`int g_gain = 42;`)가 RAM에 그 값을 가지고 있다 (`.data`)
- 초기값 없는 전역 변수(`int g_count;`)가 0이다 (`.bss` — C 표준이 0을 약속한다)
- 클럭이 원하는 속도로 돌고, flash wait state가 그 속도에 맞다 (SystemInit)
- FPU를 쓸 거면 켜져 있다 (CPACR)
- C++ 전역 객체의 생성자가 돌았다 (`__libc_init_array`)

이 중 **하드웨어가 해 주는 것은 스택 포인터 하나뿐**이다. 나머지는 전부 `Reset_Handler`라는 평범한 함수가 하고, 그 함수가 어디에 무엇을 복사할지는 **링커 스크립트가 정한 심볼**로 안다. 이 노트는 그 전 과정을 랩 보드(QEMU mps2-an385)에서 한 단계씩 보여 주고, 일부러 망가뜨려 증상을 보고, 시간을 잰다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="20" width="150" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="85" y="44" font-size="12" text-anchor="middle" font-weight="bold">① 전원 · 리셋</text>
<text x="85" y="60" font-size="12" text-anchor="middle">POR / NRST 핀</text>
<line x1="160" y1="48" x2="178" y2="48" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<rect x="180" y="20" width="150" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="255" y="44" font-size="12" text-anchor="middle" font-weight="bold">② 벡터 2개 읽기</text>
<text x="255" y="60" font-size="12" text-anchor="middle">MSP=[0x0] PC=[0x4]</text>
<line x1="330" y1="48" x2="348" y2="48" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<rect x="350" y="20" width="150" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="425" y="44" font-size="12" text-anchor="middle" font-weight="bold">③ Reset_Handler</text>
<text x="425" y="60" font-size="12" text-anchor="middle">C 런타임 준비 시작</text>
<line x1="500" y1="48" x2="518" y2="48" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<rect x="520" y="20" width="150" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="595" y="44" font-size="12" text-anchor="middle" font-weight="bold">④ SystemInit</text>
<text x="595" y="60" font-size="12" text-anchor="middle">클럭 · wait state</text>
<rect x="10" y="110" width="150" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="85" y="134" font-size="12" text-anchor="middle" font-weight="bold">⑤ .data 복사</text>
<text x="85" y="150" font-size="12" text-anchor="middle">FLASH → RAM</text>
<line x1="160" y1="138" x2="178" y2="138" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<rect x="180" y="110" width="150" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="255" y="134" font-size="12" text-anchor="middle" font-weight="bold">⑥ .bss 0 채우기</text>
<text x="255" y="150" font-size="12" text-anchor="middle">0으로 memset</text>
<line x1="330" y1="138" x2="348" y2="138" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<rect x="350" y="110" width="150" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="425" y="134" font-size="12" text-anchor="middle" font-weight="bold">⑦ 생성자</text>
<text x="425" y="150" font-size="12" text-anchor="middle">__libc_init_array</text>
<line x1="500" y1="138" x2="518" y2="138" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<rect x="520" y="110" width="150" height="56" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="595" y="134" font-size="12" text-anchor="middle" font-weight="bold">⑧ main()</text>
<text x="595" y="150" font-size="12" text-anchor="middle">여기부터 앱 코드</text>
<path d="M 645,76 L 645,93 L 85,93 L 85,108" stroke="currentColor" stroke-width="1.2" fill="none" marker-end="url(#ah1)"/>
<text x="10" y="182" font-size="12" text-anchor="start">①②는 하드웨어가 한다 (명령 0개). ③~⑦은 startup 코드가 한다 — 이 노트의 주제.</text>
<path d="M 595,166 L 595,196 L 110,196 L 110,214" stroke="currentColor" stroke-width="1.2" fill="none" marker-end="url(#ah1)"/>
<line x1="340" y1="196" x2="340" y2="214" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<line x1="595" y1="196" x2="570" y2="214" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah1)"/>
<rect x="10" y="214" width="200" height="150" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="110" y="269" font-size="12" text-anchor="middle" font-weight="bold">bare-metal</text>
<text x="110" y="285" font-size="12" text-anchor="middle">while (1) { 슈퍼 루프 }</text>
<text x="110" y="301" font-size="12" text-anchor="middle">main이 return하면?</text>
<text x="110" y="317" font-size="12" text-anchor="middle">런타임마다 다르다 (§7)</text>
<rect x="230" y="214" width="220" height="150" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="340" y="261" font-size="12" text-anchor="middle" font-weight="bold">FreeRTOS</text>
<text x="340" y="277" font-size="12" text-anchor="middle">main 안에서 xTaskCreate</text>
<text x="340" y="293" font-size="12" text-anchor="middle">vTaskStartScheduler()</text>
<text x="340" y="309" font-size="12" text-anchor="middle">여기서부터 커널 (C3)</text>
<text x="340" y="325" font-size="12" text-anchor="middle">돌아오지 않는다</text>
<rect x="470" y="214" width="200" height="150" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="570" y="261" font-size="12" text-anchor="middle" font-weight="bold">Zephyr</text>
<text x="570" y="277" font-size="12" text-anchor="middle">main 전에 커널이 이미 뜸</text>
<text x="570" y="293" font-size="12" text-anchor="middle">z_prep_c → z_cstart</text>
<text x="570" y="309" font-size="12" text-anchor="middle">init level 5단계</text>
<text x="570" y="325" font-size="12" text-anchor="middle">main도 스레드 (§10)</text>
</svg>
```

그림 1 — 리셋부터 main까지 여덟 단계. ①②는 하드웨어, ③~⑦은 startup 코드(이 노트), ⑧ 이후는 런타임마다 다르다. FreeRTOS는 main이 커널을 시작하고, Zephyr는 커널이 main을 부른다.

SSD 펌웨어 비유로 정리하면:

| 이 노트의 말 | SSD 펌웨어에서 Don이 본 것 |
|---|---|
| 벡터 테이블 0번 워드(초기 MSP) | 부트 ROM이 FW 이미지 헤더에서 읽던 "스택 top" 필드 |
| `Reset_Handler` | `reset.S`의 `_start` |
| `.data` 복사 (FLASH → RAM) | NAND/NOR에서 SRAM으로 FW 이미지 로딩 (부트 ROM 또는 2단 로더가 했다) |
| `.bss` 0 채우기 | 부팅 초기에 SRAM 영역을 memset 0 하던 루프 (ECC SRAM이면 필수) |
| `.noinit` | 리셋을 넘어 살아남는 크래시 로그 · 부트 카운터 영역 |
| `.ramfunc` | flash 프로그래밍 중에 돌아야 하는 코드를 SRAM/ITCM에 올리던 것 |
| 링커 스크립트 `MEMORY` | 칩의 메모리 맵 표 (TCM · SRAM 뱅크 · 공유 메모리) |

### 실습 환경 한 줄

QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) + 랩 템플릿을 **내 폴더로 복사해서** 고친 startup · 링커 스크립트 / FreeRTOS V11.3.2 / Zephyr v4.4.2 / 다른 코어로 `mps2-an386`(M4F)과 `qemu-system-riscv32 virt`. 시간 숫자는 모두 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간)로 잰 값이다. **QEMU icount 기준이라 실제 칩과 절대값은 다르다** — 캐시 · flash wait state · 버스 대기를 모델링하지 않고, 사이클이 아니라 **명령 개수**를 센다(`stmia` 4워드도 명령 1개). 상대 비교와 "크기에 비례한다"는 경향만 믿는다.

예제는 Mac의 `~/.rtos_scratch/B5/`에 소스를 두고 VM에서 빌드한다. 템플릿 `startup.c · board.c · mps2_m3.ld`는 `~/.rtos_scratch/B5/common/`에 복사해서 고쳤다(`/var/rtoslab`은 건드리지 않는다). 빌드 스크립트는 랩 Makefile과 같은 옵션을 쓴다.

```sh
# ~/.rtos_scratch/B5/bld.sh — bld.sh OUTNAME LDSCRIPT STARTUP "SRCS" [extra flags...]
set -e
N=$1; LD=$2; ST=$3; SRCS=$4; shift 4
O=/var/tmp/rtos/B5/$N; mkdir -p $O
C=/Users/donh/.rtos_scratch/B5/common
arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 -O2 -g3 -std=gnu11 -Wall -Wextra -ffunction-sections -fdata-sections \
  -I$C $ST $C/board.c $SRCS -T $LD -nostartfiles --specs=nano.specs -Wl,--gc-sections -Wl,-Map=$O/app.map "$@" -o $O/app.elf
arm-none-eabi-size $O/app.elf
```

이 노트의 실행 예제 한눈에:

| # | 무엇을 보나 | 절 |
|---|---|---|
| 1 | gdb로 리셋 직후 레지스터 (MSP · PC · xPSR · CONTROL · LR) | §1 |
| 2 | 리셋 벡터의 Thumb 비트를 지우면 | §1 |
| 3 | RISC-V(rv32)는 리셋 때 아무것도 안 해 준다 | §1 |
| 4 | C에서 링커 심볼 읽기 — 모든 것이 어디에 놓였나 | §2 |
| 5 | `objdump -h` · `readelf -l` · map 파일 읽기 | §2 |
| 6 | RAM 두 영역 + copy table, 그리고 와일드카드 함정 | §2 |
| 7~11 | 버그 재현: KEEP 누락 · `AT>` 누락 · .bss 안 0 · 스택-힙 충돌 · ASSERT/PROVIDE | §3 |
| 12 | 컴파일러가 startup 루프를 `memcpy`/`memset` 호출로 바꾼다 | §4 |
| 13 | M4F: CPACR를 안 켜면 첫 float 명령에서 NOCP | §5 |
| 14 | 생성자: 템플릿은 조용히 버린다 → `_init` · `__dso_handle` 링크 에러 → 고친 버전 | §6 |
| 15~16 | main이 return하면: newlib `exit()` 경로 / 돌아갈 곳이 없을 때 | §7 |
| 17~18 | 부팅 시간: .bss · .data 크기 × 복사 방식, FreeRTOS heap을 `.noinit`으로 | §8 |
| 19 | `.ramfunc` — RAM에서 도는 함수와 long-branch veneer | §9 |
| 20~22 | Zephyr: init level 타임라인, gdb로 부팅 경로, 128 KB .bss의 대가 | §10 |

---

## 1. 리셋 순간 — 하드웨어는 두 워드만 읽는다

> 🔁 이 절은 Bare-Metal BM F4와 겹친다 (주인: 이 노트).

### 1.1 직관

Cortex-M의 리셋은 놀라울 만큼 단순하다. 코어는 메모리 맨 앞(정확히는 VTOR가 가리키는 곳, 리셋 때 보통 0x0)에서 **워드 두 개**를 읽는다.

- 0번 워드 → MSP(Main Stack Pointer)에 넣는다
- 1번 워드 → PC에 넣는다 (그리고 bit 0을 Thumb 상태 비트로 쓴다)

그리고 그 주소부터 명령을 실행한다. 끝이다. 클럭 설정도, RAM 초기화도, 캐시도 없다. 이것이 Cortex-M을 "C로 리셋 핸들러를 쓸 수 있는 코어"로 만든다 — 첫 명령이 실행될 때 **이미 스택이 있으니** 컴파일러가 만든 `push {r3, lr}`가 바로 동작한다. Cortex-A/R이나 RISC-V는 그렇지 않다(§1.5).

벡터 테이블 전체의 구조(예외 번호표, VTOR로 옮기기)는 B2에서 다뤘다. 여기서는 **리셋 한 순간**만 본다.

### 1.2 정의 — 리셋 직후 코어 상태 (Armv7-M)

Armv7-M 아키텍처 매뉴얼의 리셋 동작을 표로 줄이면:

| 항목 | 리셋 직후 값 | 의미 |
|---|---|---|
| MSP | `[VTOR + 0]` (하위 2비트는 0으로) | 메인 스택 꼭대기. 스택은 아래로 자란다 |
| PC | `[VTOR + 4]`에서 bit 0을 뗀 값 | `Reset_Handler` 주소 |
| EPSR.T | `[VTOR + 4]`의 bit 0 | 1이어야 한다. 0이면 첫 명령에서 INVSTATE fault |
| LR | `0xFFFFFFFF` | "돌아갈 곳 없음". main을 이걸로 return하면 §7의 사고 |
| 모드 | Thread 모드, privileged | 특권 레지스터(CONTROL · NVIC · SCB) 바로 접근 가능 |
| 스택 선택 | MSP (CONTROL.SPSEL = 0) | PSP는 RTOS가 태스크용으로 나중에 쓴다 (B1, C3) |
| IPSR | 0 | 예외 처리 중이 아니다 |
| PRIMASK · FAULTMASK · BASEPRI | 0 | 인터럽트 마스크 없음 (그래도 NVIC 활성화된 IRQ가 없으니 조용하다) |
| VTOR | M3/M4/M7: 0 | v8-M(M33 · M55)은 SoC가 정한 초기값(`INITSVTOR` 등)을 쓴다 |
| R0–R12 | 아키텍처상 UNKNOWN | QEMU는 0이지만 믿으면 안 된다 |

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="110" y="22" font-size="12" text-anchor="middle">FLASH 0x00000000 (벡터 테이블)</text>
<rect x="10" y="34" width="60" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="40" y="53" font-size="12" text-anchor="middle">0x00</text>
<rect x="74" y="34" width="100" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="124" y="53" font-size="12" text-anchor="middle">0x20400000</text>
<text x="184" y="54" font-size="12" text-anchor="start">초기 MSP 값</text>
<rect x="10" y="68" width="60" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="40" y="87" font-size="12" text-anchor="middle">0x04</text>
<rect x="74" y="68" width="100" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="124" y="87" font-size="12" text-anchor="middle">0x000000d5</text>
<text x="184" y="88" font-size="12" text-anchor="start">Reset_Handler | 1</text>
<rect x="10" y="102" width="60" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="40" y="121" font-size="12" text-anchor="middle">0x08</text>
<rect x="74" y="102" width="100" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="124" y="121" font-size="12" text-anchor="middle">0x...</text>
<text x="184" y="122" font-size="12" text-anchor="start">NMI_Handler</text>
<rect x="10" y="136" width="60" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="40" y="155" font-size="12" text-anchor="middle">0x0C</text>
<rect x="74" y="136" width="100" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="124" y="155" font-size="12" text-anchor="middle">0x...</text>
<text x="184" y="156" font-size="12" text-anchor="start">HardFault_Handler</text>
<rect x="10" y="170" width="60" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="40" y="189" font-size="12" text-anchor="middle">...</text>
<rect x="74" y="170" width="100" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="124" y="189" font-size="12" text-anchor="middle">...</text>
<text x="184" y="190" font-size="12" text-anchor="start">...</text>
<rect x="420" y="30" width="230" height="64" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="535" y="58" font-size="12" text-anchor="middle">MSP = 0x20400000</text>
<text x="535" y="74" font-size="12" text-anchor="middle">(main 스택의 꼭대기)</text>
<rect x="420" y="112" width="230" height="82" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="535" y="141" font-size="12" text-anchor="middle">PC = 0x000000d4</text>
<text x="535" y="157" font-size="12" text-anchor="middle">EPSR.T = 1 (bit0 = Thumb)</text>
<text x="535" y="173" font-size="12" text-anchor="middle">0이면 INVSTATE → HardFault</text>
<line x1="320" y1="51" x2="418" y2="62" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<line x1="320" y1="85" x2="418" y2="150" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="370" y="44" font-size="12" text-anchor="middle">① 읽기</text>
<text x="355" y="128" font-size="12" text-anchor="middle">② 읽기</text>
<rect x="10" y="214" width="640" height="74" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="20" y="239" font-size="12" text-anchor="start">리셋 직후 코어 상태 (gdb로 확인, §1.3): Thread 모드 · privileged · MSP 사용 (CONTROL = 0)</text>
<text x="20" y="255" font-size="12" text-anchor="start">IPSR = 0 · PRIMASK = 0 · LR = 0xFFFFFFFF · 나머지 레지스터는 미정 (QEMU는 0)</text>
<text x="20" y="271" font-size="12" text-anchor="start">VTOR = 0 (M3/M4/M7) → 벡터 테이블은 0x0에서 읽는다. 부트로더가 있으면 나중에 VTOR을 바꾼다</text>
</svg>
```

그림 2 — 리셋 때 하드웨어가 하는 일 전부. 벡터 테이블 0번 워드가 MSP, 1번 워드가 PC와 Thumb 비트가 된다. 주소는 예제 1의 실제 빌드 값이다.

### 1.3 예제 1 — gdb로 리셋 직후 레지스터 보기

무엇을 확인하나: QEMU를 `-S`(첫 명령 전에 멈춤)로 띄우고 gdb를 붙여 **명령이 하나도 실행되지 않은 상태**의 MSP · PC · xPSR · CONTROL · LR을 읽는다. 대상은 §2.4의 `ex1` 앱(랩 startup.c + mps2_m3.ld 그대로)이다. 여러 작성자가 같은 VM을 쓰므로 gdb 포트는 1234 대신 고유 번호를 쓴다.

```sh
# ~/.rtos_scratch/B5/gdb_reset.sh ELF PORT GDBCMDFILE — QEMU를 리셋 상태로 멈춰 두고 gdb 스크립트 실행
qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial null \
  -semihosting-config enable=on,target=native -kernel $ELF -gdb tcp::$P -S $QEXTRA &
QP=$!; sleep 0.5
gdb -q -batch -ex "target remote :$P" -x $CMDS $ELF 2>&1 | grep -v "^warning: \|^Reading\|^\[Inferior"
kill $QP
```

```text
# reset.gdb
x/2wx 0
p/x $msp
p/x $sp
p/x $pc
p/x $xpsr
p/x $control
p/x $primask
info symbol $pc
stepi            (4번 반복하며 x/i $pc)
p/x $sp
tbreak main
continue
p/x $sp
p g_gain
```

```sh
limactl shell elinux -- bash -lc '/Users/donh/.rtos_scratch/B5/gdb_reset.sh /var/tmp/rtos/B5/ex1/app.elf 3355 /Users/donh/.rtos_scratch/B5/ex1/reset.gdb'
```

```text
Reset_Handler () at common/startup.c:42
42	    while (dst < &_edata) *dst++ = *src++;          /* .data: FLASH → RAM */
--- state right after reset (before any instruction) ---
0x0 <g_vectors>:	0x20400000	0x000000d5
$1 = 0x20400000
$2 = 0x20400000
$3 = 0xd4
$4 = 0x41000000
$5 = 0x0
$6 = 0x0
Reset_Handler in section .text
--- stepi x4 ---
0x000000d6	42	    while (dst < &_edata) *dst++ = *src++;          /* .data: FLASH → RAM */
=> 0xd6 <Reset_Handler+2>:	ldr	r2, [pc, #64]	@ (0x118 <Reset_Handler+68>)
0x000000d8	42	    while (dst < &_edata) *dst++ = *src++;          /* .data: FLASH → RAM */
=> 0xd8 <Reset_Handler+4>:	ldr	r3, [pc, #64]	@ (0x11c <Reset_Handler+72>)
0x000000da	42	    while (dst < &_edata) *dst++ = *src++;          /* .data: FLASH → RAM */
=> 0xda <Reset_Handler+6>:	cmp	r3, r2
0x000000dc	42	    while (dst < &_edata) *dst++ = *src++;          /* .data: FLASH → RAM */
=> 0xdc <Reset_Handler+8>:	bcs.n	0xf0 <Reset_Handler+28>
$7 = 0x203ffff8
--- break main ---
Temporary breakpoint 1 at 0x36c: file ex1/main.c, line 17.

Temporary breakpoint 1, main () at ex1/main.c:17
17	    int local = 0;
$8 = 0x203ffff8
$9 = 42
```

같은 상태에서 LR도 읽었다(`p/x $lr` 한 줄짜리 스크립트).

```text
Reset_Handler () at common/startup.c:42
42	    while (dst < &_edata) *dst++ = *src++;          /* .data: FLASH → RAM */
$1 = 0xffffffff
$2 = 0x0
```

출력에서 볼 것:

- 메모리 0x0의 두 워드 `0x20400000 0x000000d5`가 그대로 **MSP = 0x20400000, PC = 0xd4**가 됐다. 0xd5의 bit 0(=1)은 PC로 가지 않고 Thumb 비트가 됐다 — xPSR `0x41000000`의 bit 24(T)가 1이다(bit 30은 Z 플래그, QEMU 초기값).
- CONTROL = 0(privileged, MSP 사용), PRIMASK = 0, LR = 0xFFFFFFFF. 표 1.2와 정확히 같다.
- 첫 `stepi` 한 번으로 SP가 `0x20400000 → 0x203ffff8`이 됐다. `Reset_Handler`의 첫 명령 `push {r3, lr}`가 **리셋 직후에 이미 동작하는 스택**에 8바이트를 쌓은 것이다. 이게 "Cortex-M은 startup을 C로 쓸 수 있다"의 증거다.
- main에 도착했을 때 `g_gain = 42`. 그 사이에 `Reset_Handler`가 .data를 복사했다(§2, §4).

**Don 경험과 연결**: SSD 컨트롤러의 Cortex-R은 리셋 때 SP를 읽어 주지 않는다. 그래서 `reset.S`의 첫 줄이 모드별 스택 설정(`msr cpsr_c, ...; ldr sp, =...`)이었다. Cortex-M에서는 그 줄이 벡터 테이블 0번 워드 하나로 대체된다.

### 1.4 예제 2 — 리셋 벡터의 Thumb 비트를 지우면

무엇을 확인하나: Cortex-M은 Thumb 명령만 실행한다. 벡터 엔트리의 bit 0이 0이면 "ARM 상태로 가라"는 뜻이 되는데, M 프로파일에는 ARM 상태가 없다. 실제 플래시 이미지(.bin)를 만들어 4번 바이트의 bit 0을 지우고, ELF 없이 **원시 이미지만** QEMU의 0x0에 올려서 부팅했다(실제 플래시 프로그래머가 하는 일과 같다). startup은 §4.4의 어셈블리 버전이다.

```sh
# ~/.rtos_scratch/B5/runbin.sh flash.bin : ELF 없이 원시 플래시 이미지를 0x0에 올려 부팅
timeout --foreground ${2:-5} qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none \
  -serial stdio -semihosting-config enable=on,target=native -device loader,file=$1,addr=0x0
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && ./bld.sh even common/mps2_m3.ld common/startup_asm.S hello/ret.c >/dev/null; O=/var/tmp/rtos/B5/even; arm-none-eabi-objcopy -O binary $O/app.elf $O/ok.bin; python3 -c "
import sys; b=bytearray(open(\"$O/ok.bin\",\"rb\").read()); print(\"vector[1] = 0x%08x\" % int.from_bytes(b[4:8],\"little\")); b[4]&=0xFE; open(\"$O/even.bin\",\"wb\").write(b); print(\"patched   = 0x%08x\" % int.from_bytes(b[4:8],\"little\"))"; ./runbin.sh $O/even.bin 3 2>&1 | head -6'
```

```text
vector[1] = 0x00000041
patched   = 0x00000040
qemu-system-arm: terminating on signal 15 from pid 1907570 (timeout)
```

아무 출력 없이 3초 뒤 timeout으로 죽었다. gdb로 들어가 봤다(같은 이미지를 `-device loader`로 올리고 `-gdb tcp::3358 -S`).

```text
Reset_Handler () at common/startup_asm.S:23
23	    ldr   r0, =_sidata          /* .data: copy FLASH -> RAM */
$1 = 0x40
$2 = 0x40000000
HardFault_Handler () at common/startup_asm.S:49
49	    tst   lr, #4
$3 = 0x70
$4 = 0x41000003
$5 = 0x20000
$6 = 0x40000000
HardFault_Handler in section .text
board_printf_raw (f=0x0) at /Users/donh/.rtos_scratch/B5/common/board.c:46
46	    va_list ap; va_start(ap, f);
$7 = 0x114
board_printf_raw in section .text
```

출력에서 볼 것:

- 리셋 직후 xPSR = `0x40000000` — **T 비트(bit 24)가 0**이다. PC는 0x40으로 맞는데도 상태가 틀렸다.
- `stepi` 한 번에 바로 `HardFault_Handler`. CFSR = `0x20000` = UFSR의 **INVSTATE**(bit 17), HFSR = `0x40000000` = **FORCED**(UsageFault가 꺼져 있어서 HardFault로 승격). 첫 명령조차 실행되지 못했다.
- 그런데 왜 아무것도 안 찍혔나? HardFault 핸들러가 `board_hardfault → board_printf_raw`로 UART에 쓰려 했지만, **UART를 켜는 `board_init()`은 아직 실행된 적이 없다.** TX가 꺼진 UART의 "버퍼 가득" 비트를 영원히 기다린다. 부팅 초기 fault는 "출력이 없다"로 나타나는 경우가 많다 — 그래서 bring-up 때는 디버거(또는 LED 하나)가 먼저다.

**함정**: 어셈블리로 벡터 테이블을 쓸 때 `.thumb_func` 없이 라벨만 쓰면 bit 0이 빠진다. GNU as는 `.thumb_func`가 붙은 심볼을 `.word`로 쓸 때만 링커가 bit 0을 채운다(실험: `.word Reset_Handler - 1`도 링커가 `| 1`을 해서 0x3f가 됐다 — bit 0을 빼려면 이미지를 직접 고쳐야 했다). C 배열로 쓰면 컴파일러가 함수 포인터에 bit 0을 넣어 주므로 이 실수는 거의 어셈블리 startup에서만 생긴다.

### 1.5 다른 코어는 리셋 때 무엇을 해 주나 — 그리고 예제 3 (RISC-V)

| 코어 | 리셋 때 하드웨어가 하는 일 | startup이 반드시 먼저 할 일 |
|---|---|---|
| Cortex-M (v6-M · v7-M · v8-M) | `[VTOR]` → MSP, `[VTOR+4]` → PC | 없음 — 바로 C 가능 |
| Cortex-M33/M55 (v8-M) | 위와 같음 + Secure 상태로 시작(TrustZone 있으면) | MSPLIM/PSPLIM(스택 한계 레지스터) 설정 권장 |
| Cortex-R5/R52 | PC = 0x0 또는 0xFFFF0000(HIVECS), **SP 설정 없음** | 모드별 SP 설정(어셈블리), MPU · TCM · 캐시 · ECC |
| Cortex-A53 (AArch64) | RVBAR 주소에서 시작, EL3 | `sp` 설정, 예외 레벨 내리기, MMU 끄고 시작 |
| RISC-V (M 모드) | 구현이 정한 리셋 주소로 점프, **sp도 mtvec도 없음** | `la sp` · `csrw mtvec` · `.bss` 0 — 전부 어셈블리 |

무엇을 확인하나: RISC-V rv32를 QEMU `virt` 머신(`-bios none`)에서 맨손으로 부팅한다. Zephyr SDK의 `riscv64-zephyr-elf-gcc`로 빌드했다. startup이 직접 sp와 mtvec를 설정해야 한다는 점, 그리고 그 전에 무엇이 실행되는지를 본다.

```asm
/* start.S — RISC-V (rv32) reset: no hardware SP load, no vector table; software does it all */
    .section .text.start, "ax"
    .global _start
_start:
    la    sp, _estack          /* 1. stack pointer: software must set it */
    la    t0, trap_entry
    csrw  mtvec, t0            /* 2. trap vector base: software must set it */
    la    t0, _sbss            /* 3. zero .bss */
    la    t1, _ebss
1:  bgeu  t0, t1, 2f
    sw    zero, 0(t0)
    addi  t0, t0, 4
    j     1b
2:  call  main                 /* (.data is already in RAM: this image runs from RAM) */
    li    t0, 0x100000         /* 4. sifive_test finisher: 0x5555 = pass, exit QEMU */
    li    t1, 0x5555
    sw    t1, 0(t0)
3:  j     3b
    .align 2
trap_entry:
    j     trap_entry
```

```c
/* main.c — rv32 on QEMU virt: print where we are, via the ns16550 UART at 0x10000000 */
#include <stdint.h>
#define UART ((volatile uint8_t *)0x10000000u)
static void puts_(const char *s) { while (*s) *UART = (uint8_t)*s++; }
static void hex(uint32_t v) { puts_("0x"); for (int i = 28; i >= 0; i -= 4) *UART = "0123456789abcdef"[(v >> i) & 15]; }
uint32_t g_counter;                         /* .bss */
int main(void)
{
    uint32_t sp, mtvec, mstatus, pc;
    __asm volatile("mv %0, sp" : "=r"(sp));
    __asm volatile("csrr %0, mtvec" : "=r"(mtvec));
    __asm volatile("csrr %0, mstatus" : "=r"(mstatus));
    __asm volatile("auipc %0, 0" : "=r"(pc));
    puts_("rv32 main: pc="); hex(pc); puts_(" sp="); hex(sp);
    puts_(" mtvec="); hex(mtvec); puts_(" mstatus="); hex(mstatus);
    puts_(" g_counter="); hex(g_counter); puts_("\n");
    return 0;
}
```

```text
/* rv.ld */
MEMORY { RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 1M }
ENTRY(_start)
SECTIONS {
    .text : { KEEP(*(.text.start)) *(.text*) *(.rodata*) } > RAM
    .data : { *(.data*) *(.sdata*) } > RAM
    .bss (NOLOAD) : ALIGN(4) { _sbss = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
```

```sh
limactl shell elinux -- bash -lc 'T=/var/rtoslab/zephyr-sdk-1.0.1/gnu/riscv64-zephyr-elf/bin/riscv64-zephyr-elf; O=/var/tmp/rtos/B5/rv; mkdir -p $O; cd /Users/donh/.rtos_scratch/B5/rv && $T-gcc -march=rv32imac_zicsr -mabi=ilp32 -O2 -Wall -Wextra -ffreestanding -nostdlib -T rv.ld start.S main.c -o $O/app.elf && $T-readelf -h $O/app.elf | grep -i "entry\|machine"; timeout 5 qemu-system-riscv32 -machine virt -bios none -nographic -monitor none -serial stdio -kernel $O/app.elf; echo "exit=$?"'
```

```text
  Machine:                           RISC-V
  Entry point address:               0x80000000
rv32 main: pc=0x8000009e sp=0x800ffff0 mtvec=0x80000044 mstatus=0x00000000 g_counter=0x00000000
exit=0
```

리셋 순간 무엇이 실행되는지는 QEMU의 명령 로그(`-d in_asm`)로 봤다(Ubuntu gdb에는 RISC-V 지원이 없어서).

```text
----------------
IN: 
0x00001000:  00000297          auipc                   t0,0                    # 0x1000
0x00001004:  02828613          addi                    a2,t0,40
0x00001008:  f1402573          csrrs                   a0,mhartid,zero

----------------
IN: 
0x0000100c:  0202a583          lw                      a1,32(t0)
0x00001010:  0182a283          lw                      t0,24(t0)
0x00001014:  00028067          jr                      t0

----------------
IN: 
0x80000000:  00100117          auipc                   sp,256                  # 0x80100000
0x80000004:  00010113          mv                      sp,sp
0x80000008:  00000297          auipc                   t0,0                    # 0x80000008
0x8000000c:  03c28293          addi                    t0,t0,60
0x80000010:  30529073          csrrw                   zero,mtvec,t0
...
```

출력에서 볼 것:

- 리셋 PC는 0x1000(QEMU virt의 작은 리셋 ROM)이다. 여섯 명령으로 hart ID를 a0에 넣고 0x80000000으로 점프한다. **sp는 아무도 설정하지 않았다** — 0x80000000의 첫 명령이 `auipc sp` · `mv sp`(= `la sp, _estack`)다.
- main에서 본 sp = `0x800ffff0`는 `_estack`(0x80100000)에서 main의 프레임만큼 내려간 값이다. mtvec = `0x80000044` = `trap_entry`. 둘 다 startup이 직접 넣었다.
- RISC-V에는 "벡터 테이블 0번 워드 = 스택"이라는 약속이 없다. 그래서 RISC-V startup은 **반드시 어셈블리로 시작**한다(C 함수는 첫 줄부터 sp를 쓴다). 이 예제는 RAM에서 바로 실행되는 이미지라 `.data` 복사가 없고, `-mrelax`가 쓰는 `gp`(global pointer)도 설정하지 않았다(`__global_pointer$`를 정의하지 않았으므로 링커가 gp 상대 주소로 바꾸지 않는다). 실제 RISC-V MCU startup은 `la gp, __global_pointer$`를 sp보다 먼저 넣는다. RISC-V 전반은 B10.

---

## 2. 링커 스크립트 한 줄씩

> 🔁 이 절은 Bare-Metal BM F3와 겹친다 (주인: 이 노트).

### 2.1 직관 — "메모리 지도 + 짐 싸는 규칙"

ELF의 섹션 · 세그먼트 · 재배치는 EL B3에서 봤다. Linux에서는 링커 기본 스크립트와 커널의 ELF 로더가 알아서 주소를 정하지만, MCU에는 로더가 없다. **링커 스크립트가 로더의 일까지 미리 정해 둔다.** 두 부분으로 나뉜다.

- `MEMORY` — 칩에 어떤 메모리가 어디에 얼마나 있는지 (지도)
- `SECTIONS` — 오브젝트 파일의 입력 섹션(`.text.main`, `.data.g_gain` …)을 어떤 출력 섹션으로 모아서 어느 메모리에 둘지 (짐 싸는 규칙)

그리고 `Reset_Handler`가 쓸 **주소 심볼**(`_sdata`, `_ebss`, `_estack` …)을 만들어 준다. startup 코드와 링커 스크립트는 이 심볼 이름으로 맺어진 한 쌍이다 — 하나만 바꾸면 다른 쪽이 깨진다.

```svg
<svg viewBox="0 0 680 352" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="90" y="20" font-size="12" text-anchor="middle" font-weight="bold">입력 섹션 (.o / .a)</text>
<text x="340" y="20" font-size="12" text-anchor="middle" font-weight="bold">출력 섹션 (SECTIONS)</text>
<text x="590" y="20" font-size="12" text-anchor="middle" font-weight="bold">영역 (MEMORY)</text>
<rect x="20" y="32" width="140" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="90" y="49" font-size="11" text-anchor="middle">.isr_vector</text>
<rect x="20" y="64" width="140" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="90" y="81" font-size="11" text-anchor="middle">.text.main</text>
<rect x="20" y="96" width="140" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="90" y="113" font-size="11" text-anchor="middle">.rodata.str1.1</text>
<rect x="20" y="128" width="140" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="90" y="145" font-size="11" text-anchor="middle">.init_array</text>
<rect x="20" y="160" width="140" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="90" y="177" font-size="11" text-anchor="middle">.data.g_gain</text>
<rect x="20" y="192" width="140" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="90" y="209" font-size="11" text-anchor="middle">.ramfunc</text>
<rect x="20" y="224" width="140" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="90" y="241" font-size="11" text-anchor="middle">.bss.g_count</text>
<rect x="20" y="256" width="140" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="90" y="273" font-size="11" text-anchor="middle">COMMON</text>
<rect x="20" y="288" width="140" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="90" y="305" font-size="11" text-anchor="middle">.noinit</text>
<rect x="270" y="32" width="140" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="340" y="53" font-size="12" text-anchor="middle">.isr_vector</text>
<line x1="160" y1="45" x2="268" y2="49" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="270" y="80" width="140" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="340" y="101" font-size="12" text-anchor="middle">.text</text>
<line x1="160" y1="77" x2="268" y2="97" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<line x1="160" y1="109" x2="268" y2="97" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="270" y="128" width="140" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="340" y="149" font-size="12" text-anchor="middle">.init_array</text>
<line x1="160" y1="141" x2="268" y2="145" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="270" y="176" width="140" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="340" y="197" font-size="12" text-anchor="middle">.data</text>
<line x1="160" y1="173" x2="268" y2="193" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<line x1="160" y1="205" x2="268" y2="193" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="270" y="224" width="140" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="340" y="245" font-size="12" text-anchor="middle">.bss</text>
<line x1="160" y1="237" x2="268" y2="241" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<line x1="160" y1="269" x2="268" y2="241" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="270" y="272" width="140" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="340" y="293" font-size="12" text-anchor="middle">.noinit</text>
<line x1="160" y1="301" x2="268" y2="289" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="540" y="40" width="120" height="90" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="600" y="73" font-size="12" text-anchor="middle" font-weight="bold">FLASH</text>
<text x="600" y="89" font-size="12" text-anchor="middle">&gt; FLASH</text>
<text x="600" y="105" font-size="12" text-anchor="middle">VMA = LMA</text>
<rect x="540" y="170" width="120" height="100" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="600" y="208" font-size="12" text-anchor="middle" font-weight="bold">RAM</text>
<text x="600" y="224" font-size="12" text-anchor="middle">&gt; RAM</text>
<text x="600" y="240" font-size="12" text-anchor="middle">NOLOAD</text>
<line x1="410" y1="49" x2="538" y2="85" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<line x1="410" y1="97" x2="538" y2="85" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<line x1="410" y1="145" x2="538" y2="85" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<line x1="410" y1="193" x2="538" y2="200" stroke="#3f9a6b" stroke-width="1.2" marker-end="url(#ah4)"/>
<path d="M 410,193 C 470,193 470,120 538,120" stroke="#3f9a6b" stroke-width="1.2" fill="none" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
<text x="476" y="169" font-size="11" text-anchor="middle">AT &gt; FLASH</text>
<line x1="410" y1="241" x2="538" y2="240" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<line x1="410" y1="289" x2="538" y2="240" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<text x="340" y="340" font-size="11" text-anchor="middle">KEEP()이 없으면 --gc-sections가 아무도 참조하지 않는 .isr_vector · .init_array를 버린다 (§3.1, §6)</text>
</svg>
```

그림 3 — 입력 섹션(왼쪽)이 출력 섹션(가운데)으로 모이고, 출력 섹션이 메모리 영역(오른쪽)에 놓인다. `.data`만 주소가 두 개다: 실행 위치는 RAM, 적재 위치는 FLASH(`AT > FLASH`, 점선).

### 2.2 랩 템플릿 `mps2_m3.ld` 해부

랩 공용 링커 스크립트 전체다(주석 포함 그대로).

```text
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}
ENTRY(Reset_Handler)

_Main_Stack_Size = 0x1000;     /* MSP: main() 그리고 RTOS 시작 뒤에는 인터럽트 스택 */

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
    .text : {
        *(.text*) *(.rodata*)
        KEEP(*(.init)) KEEP(*(.fini))
        . = ALIGN(4);
    } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH
    _sidata = LOADADDR(.data);
    .data : {
        . = ALIGN(4); _sdata = .;
        *(.data*)
        . = ALIGN(4); _edata = .;
    } > RAM AT > FLASH
    .bss (NOLOAD) : {
        . = ALIGN(4); _sbss = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4); _ebss = .;
    } > RAM
    .noinit (NOLOAD) : { . = ALIGN(4); *(.noinit*) . = ALIGN(4); } > RAM
    .heap (NOLOAD) : {
        . = ALIGN(8); end = .; _end = .; __heap_start = .;
    } > RAM
    _estack = ORIGIN(RAM) + LENGTH(RAM);
    __heap_end = _estack - _Main_Stack_Size;
    ASSERT(__heap_end > __heap_start, "RAM overflow")
}
```

한 줄씩:

| 줄 | 뜻 | 왜 이렇게 |
|---|---|---|
| `FLASH (rx) : ORIGIN = 0x0, LENGTH = 4M` | 0x0부터 4 MB, 읽기 · 실행 | 리셋 벡터가 0x0에 있어야 한다. 괄호 속성은 "어느 입력 섹션이 기본으로 여기 갈 수 있나"용이고 보호 기능은 아니다 |
| `RAM (rwx) : ORIGIN = 0x20000000` | Cortex-M의 SRAM 영역 시작 | 0x20000000은 Armv7-M 기본 메모리 맵의 SRAM 구역 |
| `ENTRY(Reset_Handler)` | ELF 헤더의 entry point | **코어는 이걸 안 본다**(벡터 1번 워드를 본다). 디버거 · `--gc-sections`의 뿌리로 쓰인다 |
| `_Main_Stack_Size = 0x1000;` | 심볼 대입 | 링커 스크립트 안의 "상수". 주소처럼 쓰이는 숫자다 |
| `KEEP(*(.isr_vector))` | 아무도 참조 안 해도 버리지 마라 | 벡터 테이블은 코드가 부르지 않는다. KEEP 없으면 `--gc-sections`가 지운다(§3.1) |
| `*(.text*) *(.rodata*)` | 모든 파일의 `.text`로 시작하는 섹션 | `-ffunction-sections`가 함수마다 `.text.<이름>`을 만든다 |
| `KEEP(*(.init)) KEEP(*(.fini))` | crti/crtn 조각 | `-nostartfiles`라 실제로는 비어 있다(§6) |
| `.ARM.exidx` | 스택 되감기(unwind) 표 | C++ 예외 · `backtrace`용. C만 써도 libgcc 때문에 8바이트 생긴다 |
| `_sidata = LOADADDR(.data);` | .data의 **적재 주소**(FLASH 안) | `Reset_Handler`의 복사 원본 |
| `> RAM AT > FLASH` | 실행 주소는 RAM, 적재 주소는 FLASH | §2.3 |
| `. = ALIGN(4); _sdata = .;` | 위치 카운터를 4의 배수로 맞추고 심볼 | 워드 단위 복사 루프가 경계를 넘지 않게 |
| `.bss (NOLOAD)` | 파일에 내용 없음, 주소만 할당 | 원래 `.bss`는 NOBITS라 NOLOAD 없이도 같다. 명시하면 읽는 사람이 편하다 |
| `*(COMMON)` | `-fcommon`으로 만든 공용 심볼 | GCC 10부터 기본이 `-fno-common`이라 보통 비어 있다 |
| `.noinit (NOLOAD)` | 0으로 채우지도 복사하지도 않는 RAM | 리셋을 넘어 살아남는 변수(§3.3) |
| `.heap (NOLOAD)` · `end` · `__heap_start` | heap 시작 표시 | newlib `_sbrk`가 `__heap_start`부터 늘린다(B11) |
| `_estack = ORIGIN(RAM) + LENGTH(RAM)` | RAM 끝 = 초기 MSP | 벡터 0번 워드가 된다 |
| `ASSERT(cond, "msg")` | 링크 시점 검사 | 조건이 거짓이면 링크 실패(§3.4) |

### 2.3 VMA와 LMA — `.data`만 주소가 두 개인 이유

- **VMA**(Virtual Memory Address): 프로그램이 **실행될 때** 그 섹션이 있는 주소. 코드가 `g_gain`을 읽을 때 쓰는 주소다.
- **LMA**(Load Memory Address): 이미지가 **저장 · 적재되는** 주소. 플래시에 구워지는 위치다.

MCU에는 VM이 없으니 "Virtual"은 역사적 이름일 뿐이다. 대부분의 섹션은 VMA = LMA다. `.text`는 플래시에서 그대로 실행되고(XIP, §9), `.bss`는 적재할 내용이 없다. **`.data`만 다르다**: 초기값 42는 전원이 꺼져도 남아 있어야 하니 플래시(LMA)에 살고, 프로그램은 그 변수를 고쳐 써야 하니 RAM(VMA)에서 쓴다. 둘을 잇는 것이 `Reset_Handler`의 복사 루프다.

링커 스크립트 문법으로는:

- `> RAM` — VMA를 RAM 영역에 배정
- `AT > FLASH` — LMA를 FLASH 영역의 다음 빈 곳에 배정 (`AT(주소)`로 숫자를 직접 줄 수도 있다)
- `LOADADDR(.data)` — 그 섹션의 LMA를 돌려준다. `ADDR(.data)`는 VMA, `SIZEOF(.data)`는 크기

```svg
<svg viewBox="0 0 680 492" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="130" y="20" font-size="12" text-anchor="middle" font-weight="bold">FLASH (rx) 0x00000000, 4 MB</text>
<text x="500" y="20" font-size="12" text-anchor="middle" font-weight="bold">RAM (rwx) 0x20000000, 4 MB</text>
<rect x="110" y="32" width="180" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="200" y="56" font-size="12" text-anchor="middle">.isr_vector</text>
<text x="102" y="45" font-size="11" text-anchor="end">0x00000000</text>
<rect x="110" y="72" width="180" height="90" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="200" y="121" font-size="12" text-anchor="middle">.text + .rodata</text>
<text x="102" y="85" font-size="11" text-anchor="end">0x000000c0</text>
<rect x="110" y="162" width="180" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="200" y="179" font-size="12" text-anchor="middle">.ARM.exidx</text>
<text x="102" y="175" font-size="11" text-anchor="end">0x00001254</text>
<rect x="110" y="188" width="180" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="200" y="212" font-size="12" text-anchor="middle">.data 초기값 (LMA)</text>
<text x="102" y="201" font-size="11" text-anchor="end">0x0000125c</text>
<rect x="110" y="228" width="180" height="110" rx="4" fill="#888" fill-opacity="0.05" stroke="#888" stroke-width="1.2"/>
<text x="200" y="287" font-size="12" text-anchor="middle">(빈 FLASH)</text>
<rect x="450" y="32" width="180" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="540" y="56" font-size="12" text-anchor="middle">.data (VMA)</text>
<text x="442" y="45" font-size="11" text-anchor="end">0x20000000</text>
<rect x="450" y="72" width="180" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="540" y="96" font-size="12" text-anchor="middle">.bss  (0으로)</text>
<text x="442" y="85" font-size="11" text-anchor="end">0x20000064</text>
<rect x="450" y="112" width="180" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="540" y="129" font-size="12" text-anchor="middle">.noinit (그대로)</text>
<text x="442" y="125" font-size="11" text-anchor="end">0x200001b0</text>
<rect x="450" y="138" width="180" height="70" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="540" y="177" font-size="12" text-anchor="middle">heap ↑ (_sbrk)</text>
<text x="442" y="151" font-size="11" text-anchor="end">0x200001b0</text>
<rect x="450" y="208" width="180" height="120" rx="4" fill="#888" fill-opacity="0.05" stroke="#888" stroke-width="1.2"/>
<text x="540" y="272" font-size="12" text-anchor="middle">(남는 RAM)</text>
<rect x="450" y="328" width="180" height="50" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.2"/>
<text x="540" y="357" font-size="12" text-anchor="middle">MSP 스택 ↓ 4 KB</text>
<text x="442" y="341" font-size="11" text-anchor="end">0x203ff000</text>
<text x="442" y="382" font-size="11" text-anchor="end">0x20400000 = _estack</text>
<path d="M 290,228 C 360,228 360,52 448,52" stroke="#3f9a6b" stroke-width="1.2" fill="none" marker-end="url(#ah3)"/>
<text x="440" y="250" font-size="12" text-anchor="end">Reset_Handler가</text>
<text x="440" y="266" font-size="12" text-anchor="end">LMA → VMA로 복사</text>
<rect x="10" y="396" width="660" height="86" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="20" y="419" font-size="12" text-anchor="start">숫자는 예제 1의 실제 빌드 (app.map, objdump -h).</text>
<text x="20" y="435" font-size="12" text-anchor="start">.data는 주소가 두 개다: 실행 주소 VMA(RAM)와 적재 주소 LMA(FLASH).</text>
<text x="20" y="451" font-size="12" text-anchor="start">.bss는 FLASH에 한 바이트도 없다 (크기만 기록).</text>
<text x="20" y="467" font-size="12" text-anchor="start">heap과 스택은 서로를 향해 자란다 — 둘 사이의 벽은 _sbrk 검사뿐 (§3.4).</text>
</svg>
```

그림 4 — 예제 4(`ex1`)의 실제 메모리 지도. .data의 초기값은 FLASH 0x125c에 있고, `Reset_Handler`가 RAM 0x20000000으로 복사한다. heap은 위로, 스택은 아래로 자란다.

### 2.4 예제 4 — C에서 링커 심볼 읽기

무엇을 확인하나: 링커 스크립트가 만든 심볼을 C에서 `extern`으로 선언하고 **주소**를 찍는다. 링커 심볼은 "값이 없는 변수"다 — 심볼의 주소 자체가 정보다. 그래서 `extern uint32_t _sdata;`로 선언하고 `&_sdata`로 쓴다(`_sdata`의 값을 읽으면 그 주소에 있는 엉뚱한 데이터를 읽는다).

```c
/* ex1 — where did everything land? print linker symbols from C */
#include <stdint.h>
#include "board.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;  /* from mps2_m3.ld */
extern char __heap_start, __heap_end;
extern const void *g_vectors[];

int g_gain = 42;            /* .data : value lives in FLASH, variable lives in RAM */
int g_count;                /* .bss  : no bytes in FLASH, zeroed by Reset_Handler   */
const int k_table[4] = {1, 2, 3, 4};   /* .rodata : stays in FLASH */

#define SHOW(name, v) board_printf_raw("  %s = 0x%x\n", name, (unsigned)(uintptr_t)(v))

int main(void)
{
    int local = 0;
    uint32_t msp, control;
    __asm volatile("mrs %0, msp" : "=r"(msp));
    __asm volatile("mrs %0, control" : "=r"(control));
    SHOW("g_vectors[0] (initial MSP)", g_vectors[0]);
    SHOW("g_vectors[1] (Reset_Handler)", g_vectors[1]);
    SHOW("&k_table   (.rodata)", k_table);
    SHOW("_sidata    (LMA of .data)", &_sidata);
    SHOW("_sdata     (VMA of .data)", &_sdata);
    SHOW("_edata", &_edata);
    SHOW("_sbss", &_sbss);
    SHOW("_ebss", &_ebss);
    SHOW("__heap_start", &__heap_start);
    SHOW("__heap_end", &__heap_end);
    SHOW("_estack", &_estack);
    SHOW("&local     (stack)", &local);
    SHOW("MSP now", msp);
    SHOW("CONTROL", control);
    board_printf_raw("  g_gain=%d g_count=%d data bytes=%u bss bytes=%u\n", g_gain, g_count,
        (unsigned)((char *)&_edata - (char *)&_sdata), (unsigned)((char *)&_ebss - (char *)&_sbss));
    return local;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && ./bld.sh ex1 common/mps2_m3.ld common/startup.c ex1/main.c && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B5/ex1/app.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
   4700	    100	    332	   5132	   140c	/var/tmp/rtos/B5/ex1/app.elf
  g_vectors[0] (initial MSP) = 0x20400000
  g_vectors[1] (Reset_Handler) = 0xd5
  &k_table   (.rodata) = 0x1244
  _sidata    (LMA of .data) = 0x125c
  _sdata     (VMA of .data) = 0x20000000
  _edata = 0x20000064
  _sbss = 0x20000064
  _ebss = 0x200001b0
  __heap_start = 0x200001b0
  __heap_end = 0x203ff000
  _estack = 0x20400000
  &local     (stack) = 0x203fffdc
  MSP now = 0x203fffd0
  CONTROL = 0x0
  g_gain=42 g_count=0 data bytes=100 bss bytes=332
[board_exit] code=0
```

출력에서 볼 것:

- `.data`는 100바이트, 그런데 우리가 만든 `.data` 변수는 `g_gain` 4바이트뿐이다. 나머지 96바이트는 newlib-nano의 `_impure_data`(stdio 상태) 등이다(§2.5 map 파일). `.bss` 332바이트 중 우리 것은 4바이트, 나머지는 newlib의 `__sf`(FILE 구조체 3개) 등이다. **라이브러리도 .data/.bss를 쓴다** — 그래서 startup이 .data를 복사하지 않으면 printf가 죽는다(§3.2).
- `__heap_end = _estack - 0x1000` — 스택 4 KB를 남기고 그 아래 전부가 heap이 될 수 있다.
- `CONTROL = 0`: main에 와서도 여전히 privileged · MSP. 이 상태가 FreeRTOS `vTaskStartScheduler()` 전까지 유지된다(C3).

### 2.5 예제 5 — `objdump -h` · `readelf -l` · map 파일 읽기

무엇을 확인하나: 같은 ELF를 세 가지 도구로 보고, VMA/LMA가 각각 어디에 나타나는지 확인한다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/B5/ex1 && arm-none-eabi-objdump -h app.elf | head -18; arm-none-eabi-readelf -lW app.elf'
```

```text
app.elf:     file format elf32-littlearm

Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .isr_vector   000000c0  00000000  00000000  00010000  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  1 .text         00001194  000000c0  000000c0  000100c0  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  2 .ARM.exidx    00000008  00001254  00001254  00011254  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  3 .data         00000064  20000000  0000125c  00020000  2**2
                  CONTENTS, ALLOC, LOAD, DATA
  4 .bss          0000014c  20000064  000012c0  00020064  2**2
                  ALLOC
  5 .noinit       00000000  200001b0  0000140c  00000000  2**0
                  ALLOC
  6 .heap         00000000  200001b0  200001b0  00020064  2**0
                  CONTENTS
...
Elf file type is EXEC (Executable file)
Entry point 0xd5
There are 3 program headers, starting at offset 52

Program Headers:
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  ARM_EXIDX      0x011254 0x00001254 0x00001254 0x00008 0x00008 R   0x4
  LOAD           0x010000 0x00000000 0x00000000 0x0125c 0x0125c R E 0x10000
  LOAD           0x020000 0x20000000 0x0000125c 0x00064 0x001b0 RW  0x10000

 Section to Segment mapping:
  Segment Sections...
   00     .ARM.exidx 
   01     .isr_vector .text .ARM.exidx 
   02     .data .bss 
```

(`objdump -h`의 `.debug_*` 줄은 생략했다.)

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/B5/ex1 && grep -n -A30 "^\.data " app.map | head -32'
```

```text
342:.data           0x20000000       0x64 load address 0x0000125c
343-                0x20000000                        . = ALIGN (0x4)
344-                0x20000000                        _sdata = .
345- *(.data*)
346- .data.brk.0    0x20000000        0x4 /tmp/cc6ivH5r.o
347- .data.g_gain   0x20000004        0x4 /tmp/ccr172DE.o
348-                0x20000004                g_gain
349- .data          0x20000008       0x50 /usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/lib/thumb/v7-m/nofp/libg_nano.a(libc_a-impure.o)
350-                0x20000008                _impure_ptr
351-                0x2000000c                _impure_data
352- .data          0x20000058        0xc /usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/lib/thumb/v7-m/nofp/libg_nano.a(libc_a-findfp.o)
353-                0x20000058                __sglue
354-                0x20000064                        . = ALIGN (0x4)
355-                0x20000064                        _edata = .
356-
357-.igot.plt       0x20000064        0x0 load address 0x000012c0
358- .igot.plt      0x20000064        0x0 /tmp/cc7lyOmB.o
359-
360-.bss            0x20000064      0x14c load address 0x000012c0
361-                0x20000064                        . = ALIGN (0x4)
362-                0x20000064                        _sbss = .
363- *(.bss*)
364- .bss.g_count   0x20000064        0x4 /tmp/ccr172DE.o
365-                0x20000064                g_count
366- .bss           0x20000068      0x13c /usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/lib/thumb/v7-m/nofp/libg_nano.a(libc_a-findfp.o)
367-                0x20000068                __sf
368-                0x200001a0                __stdio_exit_handler
369- .bss           0x200001a4        0x8 /usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/lib/thumb/v7-m/nofp/libg_nano.a(libc_a-mallocr.o)
370-                0x200001a4                __malloc_sbrk_start
371-                0x200001a8                __malloc_free_list
372- .bss           0x200001ac        0x4 /usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/lib/thumb/v7-m/nofp/libg_nano.a(libc_a-reent.o)
```

출력에서 볼 것:

- `objdump -h`에서 `.data`만 **VMA 0x20000000 ≠ LMA 0x0000125c**. LMA 0x125c는 `.ARM.exidx`(0x1254, 8바이트)가 끝난 바로 다음이다 — `AT > FLASH`는 "FLASH의 다음 빈 곳"이다.
- `readelf -l`에서 같은 사실이 세그먼트로 보인다: 두 번째 LOAD의 **VirtAddr = 0x20000000, PhysAddr = 0x125c**. ELF에서 LMA는 PhysAddr(`p_paddr`)다. 그리고 **FileSiz 0x64 < MemSiz 0x1b0** — 파일에는 .data 100바이트만 있고, 나머지 332바이트(.bss)는 "메모리에서 0으로 채울 것"이라는 크기 정보뿐이다.
- `.bss` 줄의 플래그가 `ALLOC`뿐이고 `LOAD`가 없다. 플래시에 한 바이트도 안 들어간다.
- map 파일은 **변수 하나하나의 주소와 출처(.o 또는 .a 안의 멤버)**를 보여 준다. "왜 .data가 100바이트지?"의 답이 `libc_a-impure.o`라고 바로 나온다. 코드 크기 분석은 B11에서 이 파일로 한다.
- `.heap`이 `CONTENTS`로 표시된 것은 빈 출력 섹션에 NOLOAD를 붙인 링커의 표시 방식일 뿐 크기는 0이다.

**QEMU는 ELF를 어느 주소에 올리나?** gdb로 리셋 순간(`Reset_Handler`가 아직 한 줄도 안 돈 때) RAM과 FLASH를 읽어 봤다.

```text
before Reset_Handler runs:
0x20000004 <g_gain>:	0x00000000
0x1260:	0x0000002a
at main:
0x20000004 <g_gain>:	0x0000002a
```

QEMU는 **PhysAddr(LMA)에** 올린다. 리셋 순간 RAM의 `g_gain`은 0이고, 42(0x2a)는 FLASH 0x1260(= LMA 0x125c + 4)에 있다. main에 오면 RAM에도 42가 있다 — `Reset_Handler`가 복사했다. 실제 칩에 플래시를 굽는 것과 같은 상황이라 이 실험이 의미가 있다.

### 2.6 완전판 링커 스크립트 `b5.ld` — init_array · ramfunc · PROVIDE · ASSERT

랩 템플릿은 C 전용이고 생성자 표(`.init_array`)가 없다(§6에서 그 결과를 본다). 이 노트의 나머지 예제는 아래 완전판을 쓴다. 랩 스크립트에서 바뀐 곳만 설명한다.

```text
/* b5.ld — MPS2-AN385 full linker script (B5 note). Based on lab mps2_m3.ld + init_array, ramfunc, noinit, stack section */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}
ENTRY(Reset_Handler)

PROVIDE(_Main_Stack_Size = 0x1000);   /* can be overridden: -Wl,--defsym=_Main_Stack_Size=0x2000 */
PROVIDE(_Min_Heap_Size   = 0x2000);

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH

    .text : {
        *(.text*)
        *(.rodata*)
        KEEP(*(.init)) KEEP(*(.fini))
        . = ALIGN(4);
    } > FLASH

    .preinit_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH
    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH
    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH

    .data : ALIGN(4) {
        _sdata = .;
        *(.data*)
        *(.ramfunc*)                 /* code that must run from RAM: copied with .data */
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH
    _sidata = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(4) {
        _sbss = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    .noinit (NOLOAD) : ALIGN(4) { *(.noinit*) . = ALIGN(4); } > RAM

    .heap (NOLOAD) : ALIGN(8) {
        __heap_start = .; end = .; _end = .;
        . = . + _Min_Heap_Size;      /* guaranteed minimum; heap may grow up to __heap_end */
    } > RAM

    _estack    = ORIGIN(RAM) + LENGTH(RAM);
    _sstack    = _estack - _Main_Stack_Size;
    __heap_end = _sstack;
    ASSERT(__heap_end >= __heap_start + _Min_Heap_Size, "RAM overflow: heap+stack do not fit")
    ASSERT(_estack % 8 == 0, "AAPCS: initial SP must be 8-byte aligned")
}
```

| 새로 쓴 것 | 뜻 |
|---|---|
| `PROVIDE(sym = v)` | "아무도 정의하지 않았으면 이 값". 명령줄 `-Wl,--defsym=_Main_Stack_Size=0x2000`이나 다른 오브젝트가 같은 심볼을 정의하면 그쪽이 이긴다. 보드마다 다른 스택 크기를 스크립트 수정 없이 바꾼다(예제 11) |
| `PROVIDE_HIDDEN(...)` | PROVIDE + 공유 라이브러리용 가시성 숨김. 정적 펌웨어에서는 PROVIDE와 같다. GNU 기본 스크립트 관례 |
| `.preinit_array` · `.init_array` · `.fini_array` | 함수 포인터 표. 생성자는 `.init_array`에 포인터로 들어간다(§6). KEEP 필수 |
| `SORT(.init_array.*)` | 우선순위 붙은 생성자(`constructor(101)`)는 `.init_array.00101` 같은 이름이 된다. 이름순 정렬 = 우선순위순 |
| `.data : ALIGN(4) { ... }` | 출력 섹션 자체의 시작 정렬. 안에서 `. = ALIGN(4)`를 쓰는 것과 같은 효과 |
| `*(.ramfunc*)` in `.data` | RAM에서 돌 코드를 .data와 같이 복사(§9). 이러면 `.data` 섹션에 CODE 플래그가 붙는다 |
| `. = . + _Min_Heap_Size` | heap 최소 크기를 실제로 예약 — 다 안 들어가면 `region RAM overflowed`로 걸린다 |
| `_sstack` | 스택 바닥(최저 주소). 스택 오버플로 검사 · stack painting에 쓴다 |
| `ASSERT(_estack % 8 == 0, ...)` | AAPCS는 공개 인터페이스에서 SP 8바이트 정렬을 요구한다. 초기 MSP가 4 mod 8이면 double · `printf("%f")`가 이상해진다(B2 §4의 STKALIGN과 같은 이야기) |

### 2.7 예제 6 — RAM 두 영역 + copy table, 그리고 와일드카드 함정

실제 MCU는 RAM이 여러 덩어리다. STM32H7을 예로 들면 DTCM · AXI SRAM · SRAM1~4 · Backup SRAM이 주소도 성격도 다르다(DMA가 닿는 곳, 캐시되는 곳, 저전력에서 유지되는 곳 — B8, F8). 영역이 늘어나면 `.data` 복사도 여러 번 해야 한다. 흔한 해법은 **링커가 만든 표(copy table)를 startup이 순회**하는 것이다(NXP MCUXpresso의 `__data_section_table`이 이 방식이다).

무엇을 확인하나: RAM을 RAM(2 MB) + RAM2(2 MB, "DMA SRAM"이라고 가정)로 나누고, 초기값 있는 변수와 DMA 버퍼를 RAM2에 둔다. 링커 스크립트가 `LONG()`으로 표를 FLASH에 박고, startup이 표를 돈다.

```text
/* two.ld — two RAM regions + copy/zero tables (B5 note) */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 2M     /* general SRAM: stack, heap, .data, .bss */
    RAM2  (rw)  : ORIGIN = 0x20200000, LENGTH = 2M     /* "DMA SRAM" bank (pretend: no cache, DMA-reachable) */
}
ENTRY(Reset_Handler)
SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
    .copy_table : ALIGN(4) {                 /* {LMA, VMA, bytes} records, read by startup */
        __copy_table_start = .;
        LONG(LOADADDR(.data))  LONG(ADDR(.data))  LONG(SIZEOF(.data))
        LONG(LOADADDR(.data2)) LONG(ADDR(.data2)) LONG(SIZEOF(.data2))
        __copy_table_end = .;
        __zero_table_start = .;              /* {VMA, bytes} records */
        LONG(ADDR(.bss))  LONG(SIZEOF(.bss))
        LONG(ADDR(.bss2)) LONG(SIZEOF(.bss2))
        __zero_table_end = .;
    } > FLASH
    .data  : ALIGN(4) { *(.data*) . = ALIGN(4); } > RAM AT > FLASH
    .bss   (NOLOAD) : ALIGN(4) { *(.bss*) *(COMMON) . = ALIGN(4); } > RAM
    .heap  (NOLOAD) : ALIGN(8) { __heap_start = .; end = .; } > RAM
    .data2 : ALIGN(4) { *(.data2*) . = ALIGN(4); } > RAM2 AT > FLASH
    .bss2  (NOLOAD) : ALIGN(32) { *(.dma_bss*) . = ALIGN(4); } > RAM2
    _estack    = ORIGIN(RAM) + LENGTH(RAM);
    __heap_end = _estack - 0x1000;
}
```

```c
/* startup_tbl.c — Reset_Handler that walks linker-generated copy/zero tables */
#include <stdint.h>
#include <stdlib.h>
#include "board.h"
typedef struct { const uint32_t *lma; uint32_t *vma; uint32_t bytes; } copy_rec_t;
typedef struct { uint32_t *vma; uint32_t bytes; } zero_rec_t;
extern const copy_rec_t __copy_table_start[], __copy_table_end[];
extern const zero_rec_t __zero_table_start[], __zero_table_end[];
extern uint32_t _estack;
int main(void);
void Reset_Handler(void);
static void Fault(void) { board_printf_raw("[fault]\n"); board_exit(255); }
__attribute__((section(".isr_vector"), used))
const void *const g_vectors[16] = { &_estack, Reset_Handler, Fault, Fault, Fault, Fault, Fault };

__attribute__((optimize("no-tree-loop-distribute-patterns")))
void Reset_Handler(void)
{
    for (const copy_rec_t *r = __copy_table_start; r < __copy_table_end; r++)
        for (uint32_t i = 0; i < r->bytes / 4; i++) r->vma[i] = r->lma[i];
    for (const zero_rec_t *z = __zero_table_start; z < __zero_table_end; z++)
        for (uint32_t i = 0; i < z->bytes / 4; i++) z->vma[i] = 0;
    board_init();
    exit(main());
}
```

```c
/* two — objects placed into a second RAM bank with section attributes */
#include <stdint.h>
#include "board.h"
#define IN_RAM2      __attribute__((section(".data2")))
#define IN_DMA_RAM   __attribute__((section(".dma_bss"), aligned(32)))
int g_mode = 3;                               /* .data  -> RAM  */
IN_RAM2 int g_motor_kp = 250;                 /* .data2 -> RAM2, init value copied from FLASH */
IN_DMA_RAM uint8_t g_spi_rx[512];             /* .bss2  -> RAM2, zeroed */
extern const uint32_t __copy_table_start[], __zero_table_start[];

int main(void)
{
    board_printf_raw("g_mode     @ %x = %d\n", (unsigned)&g_mode, g_mode);
    board_printf_raw("g_motor_kp @ %x = %d\n", (unsigned)&g_motor_kp, g_motor_kp);
    board_printf_raw("g_spi_rx   @ %x  [0]=%u [511]=%u\n", (unsigned)g_spi_rx, g_spi_rx[0], g_spi_rx[511]);
    const uint32_t *c = __copy_table_start;
    for (int i = 0; i < 2; i++, c += 3)
        board_printf_raw("copy rec %d: lma=%x vma=%x bytes=%u\n", i, c[0], c[1], c[2]);
    const uint32_t *z = __zero_table_start;
    for (int i = 0; i < 2; i++, z += 2)
        board_printf_raw("zero rec %d: vma=%x bytes=%u\n", i, z[0], z[1]);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && ./bld.sh two common/two.ld common/startup_tbl.c two/main.c && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B5/two/app.elf 5'
```

```text
   text	   data	    bss	    dec	    hex	filename
   3944	    144	    840	   4928	   1340	/var/tmp/rtos/B5/two/app.elf
g_mode     @ 20000008 = 3
g_motor_kp @ 20000004 = 250
g_spi_rx   @ 20200000  [0]=0 [511]=0
copy rec 0: lma=f90 vma=20000000 bytes=104
copy rec 1: lma=ff8 vma=20200000 bytes=0
zero rec 0: vma=20000068 bytes=328
zero rec 1: vma=20200000 bytes=512
[board_exit] code=0
```

**버그가 하나 있다.** `g_motor_kp`가 RAM2(0x2020xxxx)가 아니라 RAM(0x20000004)에 있고, `.data2`는 0바이트다. 이유: `.data` 출력 섹션의 `*(.data*)` 와일드카드가 **`.data2`라는 입력 섹션 이름도 매치**한다. 링커는 스크립트에 먼저 나온 규칙에 입력 섹션을 배정하므로 `.data2` 규칙에 닿기 전에 `.data`가 먹어 버렸다. 에러도 경고도 없다. 섹션 이름을 `.data`로 시작하지 않게 바꿔서 고쳤다(`.ram2_data`).

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && ./bld.sh two2 common/two_fixed.ld common/startup_tbl.c two/main_fixed.c && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B5/two2/app.elf 5; arm-none-eabi-objdump -h /var/tmp/rtos/B5/two2/app.elf | sed -n 6,22p | paste - - | awk "{print \$2, \$3, \$4, \$5}"'
```

```text
   text	   data	    bss	    dec	    hex	filename
   3944	    144	    840	   4928	   1340	/var/tmp/rtos/B5/two2/app.elf
g_mode     @ 20000004 = 3
g_motor_kp @ 20200000 = 250
g_spi_rx   @ 20200020  [0]=0 [511]=0
copy rec 0: lma=f90 vma=20000000 bytes=100
copy rec 1: lma=ff4 vma=20200000 bytes=4
zero rec 0: vma=20000064 bytes=328
zero rec 1: vma=20200020 bytes=512
[board_exit] code=0
.isr_vector 00000040 00000000 00000000
.text 00000f28 00000040 00000040
.copy_table 00000028 00000f68 00000f68
.data 00000064 20000000 00000f90
.data2 00000004 20200000 00000ff4
.bss 00000148 20000064 00000ff4
.bss2 00000200 20200020 00001000
.debug_info 0000bdc7 00000000 00000000
.debug_abbrev 00002efc 00000000 00000000
```

출력에서 볼 것:

- 고친 뒤 `g_motor_kp`는 RAM2 0x20200000에 있고 값 250은 FLASH 0xff4(= `.data2`의 LMA)에서 복사됐다. `.data`의 LMA(0xf90, 100바이트) 바로 뒤다 — `AT > FLASH`는 여러 섹션이 줄줄이 이어 붙는다.
- DMA 버퍼 `g_spi_rx`는 `ALIGN(32)`로 32바이트 경계(0x20200020)에 놓였다. 캐시 라인 정렬이 필요한 DMA 버퍼(B8)에 쓰는 방식이다.
- copy table은 FLASH의 40바이트(0x28)짜리 데이터일 뿐이다. 영역이 다섯 개로 늘어도 startup 코드는 그대로다.
- 사용한 수정: 링커 스크립트 `*(.data2*)` → `*(.ram2_data*)`, 소스 `section(".data2")` → `section(".ram2_data")`. 실무 규칙: **커스텀 섹션 이름은 표준 이름(`.data`, `.bss`, `.text`)으로 시작하지 않게** 짓는다. 피할 수 없으면 `.data` 규칙에서 `EXCLUDE_FILE`이나 더 구체적인 패턴으로 순서를 조정한다.

**Don 경험과 연결**: SSD 컨트롤러에서 "이 테이블은 TCM에, 이 버퍼는 DMA가 닿는 SRAM에"를 섹션 attribute + 링커 스크립트로 나눴던 것과 같다. 섹션 이름 와일드카드가 의도와 다르게 매치되는 사고는 map 파일을 열어 보기 전에는 아무도 모른다 — 그래서 bring-up 체크리스트에 "map 파일에서 핵심 객체 주소 확인"을 넣는다.

---

## 3. 링커 스크립트를 망가뜨려 보기 — 네 가지 고전 버그

> 🔁 이 절은 Bare-Metal BM F3와 겹친다 (주인: 이 노트).

링커 스크립트 버그는 **컴파일 에러가 안 난다.** 링크가 되고, 이미지가 나오고, 대부분은 부팅 직후 이상하게 죽거나, 더 나쁘게는 "가끔" 틀린 값을 낸다. 네 가지를 일부러 만들어 증상을 기록한다.

### 3.1 예제 7 — KEEP을 빠뜨리면: 벡터 테이블이 사라진다

무엇을 확인하나: 랩 스크립트에서 `KEEP(*(.isr_vector))`를 `*(.isr_vector)`로 바꾸고, 벡터 테이블을 아무도 참조하지 않는 가장 작은 앱을 빌드한다.

```c
/* hello — smallest app: nothing references the vector table */
#include "board.h"
int main(void)
{
    board_printf_raw("hello from main\n");
    return 0;
}
```

```sh
cd ~/.rtos_scratch/B5/common && sed 's/KEEP(\*(.isr_vector))/*(.isr_vector)/' mps2_m3.ld > nokeep.ld
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && ./bld.sh nokeep common/nokeep.ld common/startup.c hello/main.c; cd /var/tmp/rtos/B5/nokeep; arm-none-eabi-objdump -h app.elf | sed -n 5,9p; grep -n -B1 -A1 "isr_vector" app.map | head; arm-none-eabi-objdump -s -j .text app.elf | sed -n 4,5p; arm-none-eabi-nm app.elf | grep -i "g_vectors\| main$\| Reset_Handler"; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 app.elf 3; echo "exit=$?"'
```

```text
   text	   data	    bss	    dec	    hex	filename
   3716	     96	    328	   4140	   102c	/var/tmp/rtos/B5/nokeep/app.elf
Idx Name          Size      VMA       LMA       File off  Algn
  0 .isr_vector   00000000  00000000  00000000  00020060  2**0
                  CONTENTS
  1 .text         00000e7c  00000000  00000000  00010000  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
59-                0x00000000       0x18 /tmp/cc4k9RkX.o
60: .isr_vector    0x00000000       0xc0 /tmp/cc4k9RkX.o
61- .group         0x00000000        0xc /tmp/ccYekbcA.o
--
193-
194:.isr_vector     0x00000000        0x0
195: *(.isr_vector)
196-                0x00000000                        . = ALIGN (0x4)
Contents of section .text:
 0000 08b5104a 104b9342 08d2013a d21a22f0  ...J.K.B...:..".
00000000 T Reset_Handler
0000024c T main
qemu: fatal: Lockup: can't escalate 3 to HardFault (current priority -1)

R00=00000000 R01=00000000 R02=00000000 R03=00000000
R04=00000000 R05=00000000 R06=00000000 R07=00000000
R08=00000000 R09=00000000 R10=00000000 R11=00000000
R12=00000000 R13=4a10b4e8 R14=fffffff9 R15=f0221ad2
XPSR=40000003 -Z-- A handler
FPSCR: 00000000
exit=134
```

출력에서 볼 것:

- `.isr_vector` 출력 섹션 크기 **0**. map 파일 60번째 줄 — "Discarded input sections" 목록에 0xc0바이트짜리 `.isr_vector`가 있다. `--gc-sections`가 "아무도 안 부르는 데이터"라고 버렸다. `g_vectors`에 붙인 `__attribute__((used))`는 **컴파일러**가 지우지 못하게 할 뿐, **링커**의 gc는 막지 못한다.
- 그 결과 0x0에는 `Reset_Handler`의 기계어가 왔다(`ENTRY(Reset_Handler)`가 gc의 뿌리라 함수는 살아남았다). 코어는 기계어를 벡터로 읽는다. 0x0의 바이트 `08 b5 10 4a`는 little-endian 워드 0x4a10b508 → **MSP**, 0x4의 `10 4b 93 42`는 0x42934b10 → **PC**(bit 0 = 0이라 Thumb 비트도 0).
- 첫 명령부터 fault(INVSTATE) → HardFault 진입: 쓰레기 MSP에 프레임 32바이트를 쌓아 **R13 = 0x4a10b4e8**(= 0x4a10b508 − 0x20)이 됐고, HardFault 벡터(0xC의 `d2 1a 22 f0` = 0xf0221ad2, 역시 짝수)를 PC로 읽었다 — 덤프의 **R15 = 0xf0221ad2**. HardFault 안에서 또 fault = **Lockup**. 실칩이면 코어가 멈추고(LOCKUP 신호), 디버거 없이는 "보드가 죽었다"로만 보인다.
- 예제 4(`ex1`)로 같은 실험을 하면 멀쩡히 돈다 — main이 `g_vectors`를 읽어서 참조가 생기기 때문이다. **"우리 보드에서는 되는데"의 전형**이다.

### 3.2 예제 8 — `AT > FLASH`를 빠뜨리면: QEMU는 되고 실칩은 안 된다

무엇을 확인하나: `.data`를 `> RAM`에만 두면 VMA = LMA = RAM이 된다. 디버거나 QEMU는 ELF를 그 주소에 그대로 올려 주므로 **문제가 숨는다.** 실제 플래시 프로그래머는 FLASH 영역만 굽는다. 두 방식으로 부팅해 비교한다.

```c
/* datainit — is .data initialised? (run from ELF vs. from a flash-only .bin) */
#include <stdint.h>
#include "board.h"
int g_gain = 42;                       /* .data */
char g_name[8] = "imu0";               /* .data */
int g_count;                           /* .bss  */
int main(void)
{
    board_printf_raw("g_gain=%d g_name='%s' g_count=%d\n", g_gain, g_name, g_count);
    return g_gain == 42 ? 0 : 1;
}
```

```sh
cd ~/.rtos_scratch/B5/common && sed 's/} > RAM AT > FLASH/} > RAM/' mps2_m3.ld > noat.ld
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && for v in mps2_m3 noat; do echo "=== $v.ld"; ./bld.sh di_$v common/$v.ld common/startup.c datainit/main.c >/dev/null; O=/var/tmp/rtos/B5/di_$v; arm-none-eabi-objdump -h $O/app.elf | grep -A0 " .data"; arm-none-eabi-objcopy -O binary $O/app.elf $O/full.bin; ls -l $O/full.bin | awk "{print \"full.bin bytes:\", \$5}"; arm-none-eabi-objcopy -O binary -j .isr_vector -j .text -j .ARM.exidx $O/app.elf $O/flash_only.bin; echo "-- run ELF:"; /var/rtoslab/bin/rtos-run-mps2 $O/app.elf 3; echo "-- run flash .bin:"; B=$O/full.bin; [ $v = noat ] && B=$O/flash_only.bin; ./runbin.sh $B 3; echo "exit=$?"; done'
```

```text
=== mps2_m3.ld
  3 .data         0000006c  20000000  00001038  00020000  2**2
full.bin bytes: 4260
-- run ELF:
g_gain=42 g_name='imu0' g_count=0
[board_exit] code=0
-- run flash .bin:
g_gain=42 g_name='imu0' g_count=0
[board_exit] code=0
exit=0
=== noat.ld
  3 .data         0000006c  20000000  20000000  00020000  2**2
full.bin bytes: 536871020
-- run ELF:
g_gain=42 g_name='imu0' g_count=0
[board_exit] code=0
-- run flash .bin:

[HardFault] PC=0x32461004 LR=0x8df xPSR=0x0 CFSR=0x100 HFSR=0x40000000
[board_exit] code=255
exit=1
```

`LR = 0x8df`가 어디인지 찾고, 같은 이미지를 `setvbuf` 없이(내 복사본 board.c에 `#ifndef B5_NO_SETVBUF`를 넣었다) 다시 돌렸다.

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && ./bld.sh di_noat2 common/noat.ld common/startup.c datainit/main.c -DB5_NO_SETVBUF >/dev/null; O=/var/tmp/rtos/B5/di_noat2; arm-none-eabi-objcopy -O binary -j .isr_vector -j .text -j .ARM.exidx $O/app.elf $O/flash_only.bin; ls -l $O/flash_only.bin | awk "{print \$5}"; ./runbin.sh $O/flash_only.bin 3; echo "exit=$?"; arm-none-eabi-addr2line -f -e /var/tmp/rtos/B5/di_noat/app.elf 0x8df'
```

```text
1380
g_gain=0 g_name='' g_count=0
[board_exit] code=1
exit=1
__sflush_r
/build/newlib-Ipb6mw/newlib-4.6.0.20260123/build_nano/arm-none-eabi/thumb/v7-m/nofp/newlib/../../../../../../newlib/libc/stdio/fflush.c:154
```

출력에서 볼 것:

- `AT>`가 없으면 `.data`의 LMA가 **0x20000000**(RAM)이 된다. ELF로 부팅하면 QEMU가 RAM에 직접 올려 주므로 `g_gain=42` — **버그가 안 보인다.** 디버거로 `load`해서 테스트하는 개발 환경도 똑같다.
- 플래시 영역만 담은 이미지로 부팅하면 두 가지 증상이 나온다. (1) `board_init()`의 `setvbuf(stdout, ...)`가 newlib의 `_impure_ptr`(역시 `.data`, 지금은 0)를 따라가다 `__sflush_r`에서 HardFault(CFSR 0x100 = IBUSERR, PC가 쓰레기). (2) `setvbuf`를 빼면 죽지는 않지만 **`g_gain=0`, `g_name=''`** — 조용히 틀린 값. 드론 FC라면 PID 게인이 0인 채로 이륙한다.
- 덤으로 `objcopy -O binary`로 전체 .bin을 만들면 **536,871,020바이트(512 MB)**가 나온다. .bin은 가장 낮은 LMA(0x0)부터 가장 높은 LMA(0x20000000+)까지 빈틈을 0으로 채우기 때문이다. **".bin이 512 MB가 됐다"는 `AT>` 누락의 대표 증상**이다.
- 올바른 스크립트(`mps2_m3.ld`)의 .bin은 4260바이트이고, ELF로 띄우든 .bin으로 띄우든 같다.

**실제 사례 — 공식 데모에도 있다**: 랩에 들어 있는 FreeRTOS 공식 데모 `CORTEX_MPS2_QEMU_IAR_GCC/build/gcc/`의 `mps2_m3.ld`는 `.data`를 `> RAM`에만 두고(AT 없음), `startup_gcc.c`의 `Reset_Handler`는 `(void) main();` 한 줄뿐이다(.data 복사도 .bss 0도 없다). QEMU가 ELF를 RAM에 올리고 `.bss` 부분을 0으로 채워 주니 동작하는 것이다. 그 데모를 실제 MPS2 FPGA 보드나 다른 칩의 출발점으로 복사하면 바로 이 절의 버그를 물려받는다.

### 3.3 예제 9 — .bss를 0으로 채우지 않으면: 콜드 부팅은 되고 웜 리셋에서 틀린다

무엇을 확인하나: QEMU의 RAM은 전원 켤 때 0이다. 그래서 .bss 0 채우기를 빼도 첫 부팅은 멀쩡하다. 실칩의 SRAM은 전원 직후 임의 값이고, **소프트웨어 리셋(워치독, `NVIC_SystemReset`) 뒤에는 이전 값이 그대로 남는다.** 그 웜 리셋을 QEMU에서 재현한다. `AIRCR.SYSRESETREQ`로 리셋하면 QEMU는 플래시 이미지는 다시 쓰지만 RAM은 그대로 둔다. 동시에 `.noinit` 변수(일부러 0으로 안 채우는 영역)가 리셋을 넘어 살아남는지도 본다. startup은 §4.3의 `startup_b5.c`, 링커는 `b5.ld`, 이미지는 플래시 .bin으로 부팅했다.

```c
/* warm — what survives a software reset? .bss (if not zeroed) and .noinit */
#include <stdint.h>
#include "board.h"
#define AIRCR (*(volatile uint32_t *)0xE000ED0Cu)
#define BOOT_MAGIC 0xB007B007u

int g_events;                                         /* .bss: must be 0 at every boot */
__attribute__((section(".noinit"))) uint32_t g_magic; /* .noinit: untouched by startup */
__attribute__((section(".noinit"))) uint32_t g_boots;

int main(void)
{
    if (g_magic != BOOT_MAGIC) { g_magic = BOOT_MAGIC; g_boots = 0; }  /* cold boot */
    g_boots++;
    board_printf_raw("boot #%u: g_events=%d (expect 0)\n", (unsigned)g_boots, g_events);
    g_events += 100;                                  /* pretend we counted something */
    if (g_boots < 3) {
        AIRCR = (0x05FAu << 16) | (1u << 2);          /* VECTKEY + SYSRESETREQ */
        for (;;) { }
    }
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && for f in "" "-DSKIP_BSS_ZERO"; do echo "=== startup_b5.c $f"; ./bld.sh warm common/b5.ld common/startup_b5.c warm/main.c -DCOPY_MODE=1 $f >/dev/null; O=/var/tmp/rtos/B5/warm; arm-none-eabi-objcopy -O binary $O/app.elf $O/flash.bin; ./runbin.sh $O/flash.bin 3; echo "-- same, booted from ELF:"; /var/rtoslab/bin/rtos-run-mps2 $O/app.elf 3; done'
```

```text
=== startup_b5.c 
boot #1: g_events=0 (expect 0)
boot #2: g_events=0 (expect 0)
boot #3: g_events=0 (expect 0)
[board_exit] code=0
-- same, booted from ELF:
boot #1: g_events=0 (expect 0)
boot #2: g_events=0 (expect 0)
boot #3: g_events=0 (expect 0)
[board_exit] code=0
=== startup_b5.c -DSKIP_BSS_ZERO
boot #1: g_events=0 (expect 0)
boot #2: g_events=100 (expect 0)
boot #3: g_events=200 (expect 0)
[board_exit] code=0
-- same, booted from ELF:
boot #1: g_events=0 (expect 0)
boot #2: g_events=100 (expect 0)
boot #3: g_events=200 (expect 0)
[board_exit] code=0
```

출력에서 볼 것:

- 정상 startup: 리셋을 두 번 거쳐도 `g_events`는 매번 0. 0 채우기가 리셋마다 다시 돈다.
- `.bss` 0을 빼면: **boot #1은 0**(QEMU RAM 초기값이 우연히 0이라서), 리셋 뒤에는 100, 200 — 이전 부팅의 값이 그대로 남았다. 실칩에서는 #1부터 쓰레기일 수 있다. "전원 켜고는 되는데 워치독 리셋 뒤에 이상하다"의 정체다.
- `g_boots`는 1 → 2 → 3으로 셌다. **`.noinit`은 리셋을 넘어 살아남는다.** 매직 넘버로 "콜드 부팅인가"를 판정하는 패턴이다(전원 직후 SRAM이 우연히 매직 값일 확률은 2의 32승분의 1). 리셋 원인 · 크래시 로그 · 부트 카운터를 여기에 둔다(F8에서 실무 설계).
- `.noinit`은 거꾸로 **큰 버퍼를 0으로 채우는 시간을 아끼는** 데도 쓴다(§8의 FreeRTOS heap).

### 3.4 예제 10 — 스택과 heap이 만나면: fault 없이 데이터가 썩는다

무엇을 확인하나: 그림 4에서 heap은 위로, MSP 스택은 아래로 자란다. 둘 사이에 하드웨어 벽은 없다(MPU나 v8-M의 MSPLIM을 쓰지 않는 한). RAM을 16 KB로 줄인 `small.ld`(`b5.ld`에서 RAM 16K, 스택 0x400, 최소 heap 0x400)로 heap을 끝까지 채우고, 마지막 heap 블록(보정 테이블이라고 하자)을 0xA5로 채운 뒤 재귀를 점점 깊게 한다.

```c
/* collide — 16 KB RAM, 1 KB MSP stack: fill the heap, then recurse too deep */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
extern char __heap_start, __heap_end, _sstack, _estack;

static unsigned depth_max;
static uint32_t min_sp;
__attribute__((noinline)) static unsigned recurse(unsigned n)
{
    volatile uint8_t buf[64];                 /* 64 B + frame per call */
    buf[0] = (uint8_t)n;
    if (n == depth_max) { __asm volatile("mov %0, sp" : "=r"(min_sp)); return buf[0]; }
    return recurse(n + 1) + buf[0];
}

int main(void)
{
    board_printf_raw("heap %x..%x  stack %x..%x\n", (unsigned)&__heap_start, (unsigned)&__heap_end,
                     (unsigned)&_sstack, (unsigned)&_estack);
    uint8_t *last = NULL, *p; unsigned total = 0;
    while ((p = malloc(256)) != NULL) { last = p; total += 256; }
    board_printf_raw("malloc'd %u bytes, last block at %x..%x\n", total, (unsigned)last, (unsigned)last + 256);
    memset(last, 0xA5, 256);                  /* "sensor calibration table" */
    for (depth_max = 4; depth_max <= 40; depth_max += 4) {
        recurse(0);
        unsigned bad = 0;
        for (int i = 0; i < 256; i++) bad += last[i] != 0xA5;
        board_printf_raw("depth %u (lowest SP %x): corrupted bytes in last heap block = %u\n",
                         depth_max, (unsigned)min_sp, bad);
        if (bad) break;
    }
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && ./bld.sh collide common/small.ld common/startup_b5.c collide/main.c -DCOPY_MODE=1 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B5/collide/app.elf 5'
```

```text
   text	   data	    bss	    dec	    hex	filename
   4392	     96	   1376	   5864	   16e8	/var/tmp/rtos/B5/collide/app.elf
heap 200001c0..20003c00  stack 20003c00..20004000
malloc'd 14336 bytes, last block at 20003a80..20003b80
depth 4 (lowest SP 20003e68): corrupted bytes in last heap block = 0
depth 8 (lowest SP 20003d48): corrupted bytes in last heap block = 0
depth 12 (lowest SP 20003c28): corrupted bytes in last heap block = 0
depth 16 (lowest SP 20003b08): corrupted bytes in last heap block = 6
[board_exit] code=0
```

```svg
<svg viewBox="0 0 660 280" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="40" width="17" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<rect x="37" y="40" width="559" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="317" y="69" font-size="12" text-anchor="middle">heap: malloc(256) × 56 = 14336 B</text>
<rect x="587" y="40" width="10" height="50" rx="4" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a" stroke-width="1.2"/>
<rect x="601" y="40" width="39" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="640" y="30" font-size="11" text-anchor="end">스택 1 KB</text>
<text x="20" y="112" font-size="11" text-anchor="start">0x20000000</text>
<text x="601" y="128" font-size="11" text-anchor="end">_sstack = 0x20003c00</text>
<text x="640" y="112" font-size="11" text-anchor="end">0x20004000</text>
<text x="596" y="146" font-size="11" text-anchor="end">빨간 칸 = 보정 테이블 (마지막 heap 블록, 0xA5로 채움)</text>
<line x1="640" y1="174" x2="625" y2="174" stroke="#4a7bd0" stroke-width="2" marker-end="url(#ah5)"/>
<text x="617" y="178" font-size="12" text-anchor="end">재귀 깊이 4: SP 최저 0x20003e68</text>
<line x1="640" y1="200" x2="614" y2="200" stroke="#4a7bd0" stroke-width="2" marker-end="url(#ah5)"/>
<text x="606" y="204" font-size="12" text-anchor="end">재귀 깊이 8: SP 최저 0x20003d48</text>
<line x1="640" y1="226" x2="603" y2="226" stroke="#4a7bd0" stroke-width="2" marker-end="url(#ah5)"/>
<text x="595" y="230" font-size="12" text-anchor="end">재귀 깊이 12: SP 최저 0x20003c28</text>
<line x1="640" y1="252" x2="592" y2="252" stroke="#d0564a" stroke-width="2" marker-end="url(#ah5)"/>
<text x="584" y="256" font-size="12" text-anchor="end">재귀 깊이 16: SP 최저 0x20003b08  → heap 6바이트 손상, fault 없음</text>
</svg>
```

그림 5 — 예제 10의 실제 주소. 재귀 깊이 16에서 SP가 `_sstack`(0x20003c00) 아래로 내려가 마지막 heap 블록을 덮었다. fault는 없었다.

출력에서 볼 것:

- `malloc`은 `__heap_end`(= `_sstack`)에서 멈췄다. 랩 `_sbrk`가 `brk + inc > &__heap_end`를 검사하기 때문이다. **heap → 스택 방향은 막혀 있다.**
- 스택 → heap 방향은 아무도 안 막는다. 깊이 12까지는 SP가 스택 영역 안(0x20003c28)이었고, 깊이 16에서 0x20003b08 — heap 블록(0x20003a80..0x20003b80) 한가운데 — 까지 내려가 6바이트를 덮었다(`volatile buf[0]`과 저장된 레지스터 몇 개). **HardFault도 경고도 없다.** 보정 테이블이 몰래 바뀐 채로 계속 돈다.
- RTOS에서는 태스크 스택마다 같은 문제가 있다. FreeRTOS의 `configCHECK_FOR_STACK_OVERFLOW`, Zephyr의 MPU 스택 가드 · `CONFIG_HW_STACK_PROTECTION`이 이것을 잡으려는 장치다(D · K 모듈). v8-M(M33)은 MSPLIM/PSPLIM 레지스터로 하드웨어가 잡아 준다.

### 3.5 예제 11 — ASSERT와 region overflow, PROVIDE로 고치기

무엇을 확인하나: 16 KB RAM에 DMA 버퍼 크기를 바꿔 가며 링크한다. 링커가 어떤 메시지로 거절하는지, `PROVIDE`로 만든 스택 크기를 명령줄에서 바꿔 해결할 수 있는지 본다.

```c
/* overflow — a big DMA buffer that may not fit into 16 KB RAM */
#include <stdint.h>
#include "board.h"
uint8_t g_dma_buf[BUF_KB * 1024];
int main(void) { g_dma_buf[0] = 1; board_printf_raw("buf %u KB fits\n", BUF_KB); return 0; }
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && for k in 12 14 17; do echo "=== BUF_KB=$k"; ./bld.sh ovf common/small.ld common/startup_b5.c overflow/main.c -DCOPY_MODE=1 -DBUF_KB=$k 2>&1 | grep -v "^collect2"; done'
```

```text
=== BUF_KB=12
   text	   data	    bss	    dec	    hex	filename
   4056	     96	  13656	  17808	   4590	/var/tmp/rtos/B5/ovf/app.elf
=== BUF_KB=14
/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/bin/ld: RAM overflow: heap+stack do not fit
=== BUF_KB=17
/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/bin/ld: /var/tmp/rtos/B5/ovf/app.elf section `.bss' will not fit in region `RAM'
/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/bin/ld: RAM overflow: heap+stack do not fit
/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/bin/ld: region `RAM' overflowed by 2488 bytes
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && ./bld.sh ovf common/small.ld common/startup_b5.c overflow/main.c -DCOPY_MODE=1 -DBUF_KB=14 -Wl,--defsym=_Main_Stack_Size=0x200 && arm-none-eabi-nm /var/tmp/rtos/B5/ovf/app.elf | grep -i "_Main_Stack_Size\|_sstack\|__heap_end\|__heap_start\|_Min_Heap"; /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B5/ovf/app.elf 3'
```

```text
   text	   data	    bss	    dec	    hex	filename
   4056	     96	  15704	  19856	   4d90	/var/tmp/rtos/B5/ovf/app.elf
00000200 A _Main_Stack_Size
00000400 A _Min_Heap_Size
20003e00 A __heap_end
200039b8 B __heap_start
20003e00 A _sstack
buf 14 KB fits
[board_exit] code=0
```

출력에서 볼 것:

- 14 KB: 섹션들(.data + .bss + 최소 heap 1 KB)은 16 KB 영역 안에 들어가서 **영역 overflow는 안 났다.** 하지만 스택 1 KB까지는 안 들어간다 — 이건 `MEMORY`가 모르는 정보라 **우리가 쓴 `ASSERT`만 잡는다.** ASSERT가 없었다면 링크가 성공하고 §3.4의 충돌이 런타임에 났을 것이다.
- 17 KB: `.bss`가 영역 자체를 넘었다. 링커의 내장 검사(`region RAM overflowed by 2488 bytes`)와 ASSERT가 함께 나온다.
- `-Wl,--defsym=_Main_Stack_Size=0x200`을 주면 `PROVIDE`의 기본값(0x400) 대신 그 값이 쓰여서(`nm`의 `00000200 A`) 링크가 된다. 보드 변형마다 링커 스크립트를 복사하지 않고 빌드 옵션으로 조절하는 방법이다. (물론 스택을 줄여도 되는지는 §3.4의 high-water mark로 따로 확인해야 한다.)
- `size`의 `bss` 열(15704)에는 `.noinit` · `.heap`(NOLOAD 섹션)도 들어간다. "bss가 크다"고 보이면 `size -A`로 섹션별로 나눠 봐야 한다.

---

## 4. Reset_Handler 해부 — C로 쓰기, 어셈블리로 쓰기

> 🔁 이 절은 Bare-Metal BM F4와 겹친다 (주인: 이 노트).

### 4.1 랩 `startup.c` 한 줄씩

랩 템플릿의 startup(벡터 테이블과 `Reset_Handler`, 핸들러 부분은 B2 참고)이다.

```c
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);

void Reset_Handler(void);
void Default_Handler(void);
#define WEAK_DEFAULT __attribute__((weak, alias("Default_Handler")))
void SVC_Handler(void) WEAK_DEFAULT;
void PendSV_Handler(void) WEAK_DEFAULT;
void SysTick_Handler(void) WEAK_DEFAULT;
/* ... */
typedef void (*vector_t)(void);
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + 32] = {
    (vector_t)&_estack, Reset_Handler, NMI_Handler, HardFault_Handler,
    MemManage_Handler, BusFault_Handler, UsageFault_Handler, 0, 0, 0, 0,
    SVC_Handler, DebugMon_Handler, 0, PendSV_Handler, SysTick_Handler,
    [16 + 0] = UART0RX_IRQHandler, [16 + 1] = UART0TX_IRQHandler,
    [16 + 8] = TIMER0_IRQHandler, [16 + 9] = TIMER1_IRQHandler,
    [16 + 10] = DUALTIMER_IRQHandler,
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;          /* .data: FLASH → RAM */
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;  /* .bss: 0으로 */
    board_init();
    board_exit(main());
}
```

| 부분 | 설명 |
|---|---|
| `extern uint32_t _sidata ...` | 링커 심볼. 주소만 의미 있다(§2.4). `uint32_t`로 선언해서 워드 포인터로 바로 쓴다 |
| `section(".isr_vector"), used` | 이 배열을 링커 스크립트가 0x0에 놓는 섹션으로. `used`는 컴파일러 삭제 방지(링커 gc는 KEEP이 막는다) |
| `(vector_t)&_estack` | 0번 워드 = 초기 MSP. 함수 포인터 배열에 주소를 캐스트해서 넣는 관용구 |
| `weak, alias("Default_Handler")` | 앱이 같은 이름의 함수를 정의하면 그게 이긴다. FreeRTOS 포트는 `FreeRTOSConfig.h`의 `#define vPortSVCHandler SVC_Handler` 등으로 이 이름에 연결된다 |
| `[16 + 8] = TIMER0_IRQHandler` | C99 지정 초기화. 안 쓴 IRQ 칸은 0(= 그 IRQ가 오면 주소 0으로 점프 → fault). 실무에서는 전부 `Default_Handler`로 채우는 편이 디버깅에 낫다 |
| `while (dst < &_edata)` | .data 복사. 워드 단위라 링커 스크립트의 `ALIGN(4)`가 전제 |
| `board_init(); board_exit(main());` | 랩 고유. 실제 제품의 자리에는 `SystemInit()`이 앞에, `__libc_init_array()`가 main 앞에 온다(§5, §6) |

이 startup에는 **SystemInit도, FPU 활성화도, 생성자 호출도 없다.** M3 + C 전용 랩에는 충분하지만, §5~§7에서 각각이 빠지면 무슨 일이 생기는지 본다.

### 4.2 예제 12 — 컴파일러가 startup 루프를 `memcpy`/`memset` 호출로 바꾼다

무엇을 확인하나: 위 C 루프가 실제로 어떤 기계어가 됐는지 본다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/B5/ex1; arm-none-eabi-objdump -d --no-show-raw-insn app.elf | sed -n "/<Reset_Handler>:/,/^$/p"'
```

```text
000000d4 <Reset_Handler>:
      d4:	push	{r3, lr}
      d6:	ldr	r2, [pc, #64]	@ (118 <Reset_Handler+0x44>)
      d8:	ldr	r3, [pc, #64]	@ (11c <Reset_Handler+0x48>)
      da:	cmp	r3, r2
      dc:	bcs.n	f0 <Reset_Handler+0x1c>
      de:	subs	r2, #1
      e0:	subs	r2, r2, r3
      e2:	bic.w	r2, r2, #3
      e6:	mov	r0, r3
      e8:	ldr	r1, [pc, #52]	@ (120 <Reset_Handler+0x4c>)
      ea:	adds	r2, #4
      ec:	bl	49c <memcpy>
      f0:	ldr	r2, [pc, #48]	@ (124 <Reset_Handler+0x50>)
      f2:	ldr	r3, [pc, #52]	@ (128 <Reset_Handler+0x54>)
      f4:	cmp	r3, r2
      f6:	bcs.n	10a <Reset_Handler+0x36>
      f8:	subs	r2, #1
      fa:	subs	r2, r2, r3
      fc:	bic.w	r2, r2, #3
     100:	movs	r1, #0
     102:	mov	r0, r3
     104:	adds	r2, #4
     106:	bl	588 <memset>
     10a:	bl	188 <board_init>
     10e:	bl	36c <main>
     112:	bl	310 <board_exit>
     116:	nop
     118:	.word	0x20000064
     11c:	.word	0x20000000
     120:	.word	0x0000125c
     124:	.word	0x200001b0
     128:	.word	0x20000064
```

출력에서 볼 것:

- 우리가 쓴 두 루프가 사라지고 **`bl memcpy`, `bl memset`**이 됐다. GCC의 `-ftree-loop-distribute-patterns` 최적화(이 GCC 14의 `-O2`에서 켜져 있다)가 "이 루프는 memcpy와 같다"고 알아본 것이다. 바이트 수는 `(end - 1 - start) & ~3 + 4`로 계산했다.
- 이게 문제가 되는 경우: (1) `memcpy`/`memset`이 **.data나 .bss에 의존**하면(예: 어떤 libc는 CPU 기능 표를 전역 변수로 둔다) 아직 초기화 안 된 상태에서 불린다. (2) `memset` 자체를 `.ramfunc`로 RAM에 두었다면 아직 복사 전이다. (3) 링크할 libc가 없는 `-nostdlib` 빌드에서는 `undefined reference to memset`. newlib-nano의 `memcpy`/`memset`은 전역 상태를 안 쓰므로 여기서는 안전하다.
- 그리고 **속도**가 바뀐다. newlib-nano의 `memset`은 `strd` 두 개로 16바이트씩 쓰는 루프라 우리 C 루프보다 빠르다(§8에서 측정).
- 막는 법: 함수에 `__attribute__((optimize("no-tree-loop-distribute-patterns")))`를 붙이거나(§4.3의 `COPY_MODE 1`), 파일을 `-fno-tree-loop-distribute-patterns`로 컴파일하거나, 어셈블리로 쓴다. Zephyr는 `arch_early_memset()` · `arch_early_memcpy()`라는 별도 이름을 두고 아키텍처가 덮어쓸 수 있게 했다(§10).

### 4.3 완전판 `startup_b5.c` — SystemInit · FPU · .data/.bss · 생성자 · exit

이 노트의 나머지 예제가 쓰는 startup이다. 실제 측정에 쓴 파일 그대로이며, 길어서 두 덩어리로 나눴다. `COPY_MODE`로 복사 방식을 고르고(§8), `g_boot_t[]`에 25 MHz 카운터 값을 남긴다. `g_boot_t`는 `.noinit`에 있어서 .bss 0 채우기에 지워지지 않는다.

```c
/* startup_b5.c (1/2) — full Cortex-M startup in C (B5 note): SystemInit, FPU, .data/.bss, ctors, main, exit */
#include <stdint.h>
#include <stdlib.h>
#include "board.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);
void __libc_init_array(void);
#define FPGA_CNT (*(volatile uint32_t *)0x40028018u)   /* 25 MHz, counts from reset */
__attribute__((section(".noinit"))) volatile uint32_t g_boot_t[4]; /* entry,.data,.bss,main — survives zeroing */

void Reset_Handler(void);
void Default_Handler(void) { board_printf_raw("[Default_Handler]\n"); board_exit(254); }
void HardFault_Handler(void) __attribute__((naked));
void HardFault_Handler(void) { __asm volatile("tst lr,#4\n ite eq\n mrseq r0,msp\n mrsne r0,psp\n b board_hardfault\n"); }
#define WEAK_DEFAULT __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK_DEFAULT;   void SVC_Handler(void) WEAK_DEFAULT;
void PendSV_Handler(void) WEAK_DEFAULT; void SysTick_Handler(void) WEAK_DEFAULT;

typedef void (*vector_t)(void);
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + 32] = {
    (vector_t)&_estack, Reset_Handler, NMI_Handler, HardFault_Handler,
    Default_Handler, Default_Handler, Default_Handler, 0, 0, 0, 0,
    SVC_Handler, Default_Handler, 0, PendSV_Handler, SysTick_Handler,
};

__attribute__((weak)) void SystemInit(void) { }   /* real chip: PLL, flash wait states, clocks */
#ifndef NO_INIT_STUB
void _init(void) { }                              /* normally from crti.o; -nostartfiles drops it */
void _fini(void) { }
void *__dso_handle = 0;                          /* normally from crtbegin.o; C++ statics need it */
#endif
```

```c
/* startup_b5.c (2/2) */
#if COPY_MODE >= 1   /* plain word loops, forbid gcc turning them into memcpy/memset calls */
__attribute__((optimize("no-tree-loop-distribute-patterns")))
#endif
static void init_ram(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;
#if COPY_MODE == 2   /* 16 bytes per iteration with ldm/stm, then a word tail */
    uintptr_t lim = (uintptr_t)&_edata - 16;
    __asm volatile("1: cmp %0, %2\n bhi 2f\n ldmia %1!, {r3-r6}\n stmia %0!, {r3-r6}\n b 1b\n2:\n"
                   : "+r"(dst), "+r"(src) : "r"(lim) : "r3", "r4", "r5", "r6", "memory");
    while (dst < &_edata) *dst++ = *src++;
    g_boot_t[1] = FPGA_CNT;
    dst = &_sbss; lim = (uintptr_t)&_ebss - 16;
    __asm volatile("movs r3,#0\n movs r4,#0\n movs r5,#0\n movs r6,#0\n"
                   "1: cmp %0, %1\n bhi 2f\n stmia %0!, {r3-r6}\n b 1b\n2:\n"
                   : "+r"(dst) : "r"(lim) : "r3", "r4", "r5", "r6", "memory");
    while (dst < &_ebss) *dst++ = 0;
#else
    while (dst < &_edata) *dst++ = *src++;               /* .data: FLASH(LMA) -> RAM(VMA) */
    g_boot_t[1] = FPGA_CNT;
#ifndef SKIP_BSS_ZERO
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;       /* .bss: zero */
#endif
#endif
    g_boot_t[2] = FPGA_CNT;
}

void Reset_Handler(void)
{
    uint32_t t0 = FPGA_CNT;
    SystemInit();
#if defined(__ARM_FP) && !defined(NO_FPU_ENABLE)
    *(volatile uint32_t *)0xE000ED88u |= (0xFu << 20);   /* CPACR: CP10/CP11 full access (see B6) */
    __asm volatile("dsb\n isb");
#endif
    init_ram();
    g_boot_t[0] = t0;
    __libc_init_array();          /* .preinit_array, _init, .init_array (constructors) */
    board_init();
    g_boot_t[3] = FPGA_CNT;
    exit(main());                 /* newlib: atexit handlers, then _exit() -> board_exit() */
}
```

순서가 왜 이런가:

1. **SystemInit이 맨 앞** — 클럭을 먼저 올리면 뒤의 복사 · 0 채우기가 빨라진다. 외부 SDRAM에 `.data`를 둔 보드라면 SDRAM 컨트롤러를 켜야 복사할 수 있다. 대신 SystemInit 안에서는 **.data/.bss 전역 변수를 쓰면 안 된다**(§5.1의 함정).
2. **FPU 활성화가 복사 전** — 컴파일러는 `-mfloat-abi=hard`일 때 어디서든(심지어 memcpy 최적화나 레지스터 저장에) VFP 명령을 쓸 수 있다. CPACR이 꺼진 상태에서 그런 명령이 나오면 NOCP fault다(§5.2).
3. **.data → .bss 순서**는 무관하지만 관례다.
4. **`__libc_init_array`는 .data/.bss 뒤** — 생성자는 전역 변수를 쓴다.
5. **`board_init`(UART)는 생성자 뒤** — 랩 고유 순서. 그래서 생성자 안에서는 printf를 못 쓴다(§6 예제에서 순서를 배열에 기록한 이유).
6. **`exit(main())`** — main의 반환값으로 newlib `exit` 경로를 탄다(§7).

### 4.4 같은 startup을 어셈블리로

무엇을 확인하나: C startup이 가능하다고 어셈블리가 쓸모없는 것은 아니다. 벤더 startup(`startup_stm32xxx.s`)은 대부분 어셈블리이고, §7의 "main이 돌아갈 곳" 실험에도 쓴다. 벡터 테이블까지 전부 어셈블리로 쓴 버전이다.

```asm
/* startup_asm.S — the same startup in assembly (B5 note). -DTAILCALL: "b main" with no exit path */
    .syntax unified
    .cpu cortex-m3
    .thumb

    .section .isr_vector, "a", %progbits
    .global g_vectors
g_vectors:
    .word _estack               /* 0x00: initial MSP */
#ifdef EVEN_RESET
    .word Reset_Handler - 1     /* bug demo: Thumb bit cleared */
#else
    .word Reset_Handler         /* 0x04: .thumb_func makes this odd (Thumb bit) */
#endif
    .word Default_Handler       /* 0x08: NMI */
    .word HardFault_Handler     /* 0x0C: HardFault */
    .fill 12, 4, 0              /* 0x10..0x3C: not used in this demo */

    .text
    .thumb_func
    .global Reset_Handler
Reset_Handler:
    ldr   r0, =_sidata          /* .data: copy FLASH -> RAM */
    ldr   r1, =_sdata
    ldr   r2, =_edata
1:  cmp   r1, r2
    bhs   2f
    ldr   r3, [r0], #4
    str   r3, [r1], #4
    b     1b
2:  ldr   r1, =_sbss            /* .bss: zero */
    ldr   r2, =_ebss
    movs  r3, #0
3:  cmp   r1, r2
    bhs   4f
    str   r3, [r1], #4
    b     3b
4:  bl    board_init
#ifdef TAILCALL
    mvn   lr, #0                /* LR = 0xFFFFFFFF, its value at reset (as if Reset vector = main) */
    b     main                  /* main's return pops PC = 0xFFFFFFFF */
#else
    bl    main
    bl    board_exit            /* r0 = main's return value */
#endif

    .thumb_func
HardFault_Handler:
    tst   lr, #4
    ite   eq
    mrseq r0, msp
    mrsne r0, psp
    b     board_hardfault

    .thumb_func
Default_Handler:
    b     .
```

```c
/* ret — main returns */
#include "board.h"
int main(void)
{
    board_printf_raw("main: returning 7\n");
    return 7;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && ./bld.sh asm common/mps2_m3.ld common/startup_asm.S hello/ret.c | tail -1; /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B5/asm/app.elf 3; echo "exit=$?"'
```

```text
   3648	     96	    328	   4072	    fe8	/var/tmp/rtos/B5/asm/app.elf
main: returning 7
[board_exit] code=7
exit=1
```

출력에서 볼 것: `.S`(대문자)라 C 전처리기가 돌아서 `#ifdef`를 쓸 수 있다. `.thumb_func`가 붙은 라벨은 `.word`에 쓰일 때 bit 0이 1로 채워진다(§1.4). main의 반환값 7이 r0로 `board_exit`에 그대로 넘어갔다(AAPCS: 반환값 = r0). QEMU 종료코드 1은 board_exit의 "0이 아니면 1" 규칙이다.

### 4.5 C냐 어셈블리냐

| 기준 | C startup | 어셈블리 startup |
|---|---|---|
| Cortex-M에서 가능? | 가능 — 하드웨어가 MSP를 준다 | 가능 |
| Cortex-A/R, RISC-V | sp가 없어서 첫 몇 줄은 반드시 어셈블리 | 필수 |
| 읽기 · 고치기 | 쉽다. 링커 심볼을 C 타입으로 다룬다 | 어렵다. 실수(`.thumb_func` 누락)가 조용하다 |
| 컴파일러 간섭 | 루프 → `memcpy` 치환, FPU 명령 끼어들기, 스택 사용 | 쓴 그대로 |
| 성능 | 컴파일러 · libc 품질에 따름 | `ldm/stm`으로 직접 최적화 가능(§8) |
| SP 재설정 (`ldr sp, =_estack`) | 함수 안에서 SP를 바꾸면 위험 | 자연스럽다 |
| 대표 | CMSIS 최근 GCC 템플릿(`startup_<device>.c`), 랩 템플릿 | ST `startup_stm32xxx.s`, Zephyr `reset.S` |

**왜 벤더 startup은 `ldr sp, =_estack`로 SP를 다시 넣나?** 하드웨어가 이미 넣었는데도. 부트로더가 앱으로 점프하는 경우(VTOR만 바꾸고 `bx`로 들어오면 MSP는 부트로더 것), 디버거가 리셋 없이 PC만 바꿔 실행하는 경우를 대비한 것이다. Zephyr의 `z_arm_reset`도 같은 이유로 MSP를 `z_main_stack`으로 다시 설정한다(주석에 "chain loading or a debugger"라고 적혀 있다, §10).

---

## 5. SystemInit과 FPU 켜기

> 🔁 이 절은 Bare-Metal BM F4와 겹친다 (주인: 이 노트).

### 5.1 SystemInit — 클럭 트리를 세우는 함수 (QEMU에는 없다)

CMSIS 관례에서 `SystemInit()`은 **main 전에 칩을 "제 속도로" 만드는 함수**다. 실제 칩에서 하는 일은 대략 이 순서다.

1. 리셋 직후 코어는 내부 RC 발진기(STM32의 HSI 16 MHz 같은 것)로 느리게 돈다.
2. **flash wait state를 먼저 올린다.** 플래시는 코어보다 느리다. 168 MHz로 올린 뒤에 wait state를 올리면 그 사이 명령 fetch가 틀린 값을 읽는다. 예를 들어 STM32F407은 3.3 V · 168 MHz에서 5 wait state가 필요하다(RM0090의 표).
3. 외부 크리스털(HSE)을 켜고 안정될 때까지 기다린다.
4. PLL 배수 · 분주를 설정하고 lock을 기다린다.
5. 버스 분주(AHB · APB1 · APB2)를 맞추고, 시스템 클럭을 PLL로 전환한다.
6. (선택) VTOR 설정, 외부 메모리 컨트롤러(FMC/SDRAM) 초기화, 캐시 · ART 가속기 켜기.

QEMU mps2-an385는 클럭 트리를 모델링하지 않는다(25 MHz 고정, wait state 없음). 그래서 이 노트의 `SystemInit`은 빈 weak 함수다 — **실행해서 보여 줄 수 없는 부분**이다.

**함정 — SystemInit 안의 전역 변수**: CMSIS의 `SystemCoreClock`은 초기값 있는 전역 변수(.data)다. SystemInit이 클럭을 바꾸고 `SystemCoreClock = 168000000;`을 쓰면, 바로 다음의 .data 복사가 그 값을 **플래시의 초기값으로 덮어쓴다.** 그래서 CMSIS는 main에서 `SystemCoreClockUpdate()`를 다시 부르게 하거나, SystemInit에서는 레지스터만 만진다. SystemInit 안의 `.bss` 변수도 마찬가지로 곧 0이 된다. 벤더마다 SystemInit을 .data 복사 전에 부르기도 하고 후에 부르기도 하니, **자기 프로젝트의 startup 파일에서 순서를 직접 확인**한다.

**함정 — 워치독**: 일부 칩은 옵션 바이트로 하드웨어 워치독이 리셋부터 켜져 있다. §8처럼 큰 .bss를 0으로 채우는 데 수십 ms가 걸리면 main 전에 워치독 리셋이 난다. 증상은 "부팅 루프, main 첫 줄 브레이크포인트에 안 걸림".

### 5.2 예제 13 — Cortex-M4F: CPACR를 안 켜면 첫 float 명령에서 NOCP

무엇을 확인하나: M4F/M7/M33의 FPU는 리셋 때 **꺼져 있다**(CPACR의 CP10 · CP11 필드 = 0). startup이 켜 주지 않으면 첫 VFP 명령이 UsageFault(NOCP)를 낸다. 다른 코어 실험이라 QEMU `mps2-an386`(Cortex-M4F, AN385와 같은 주변장치 맵)을 쓰고, 같은 `startup_b5.c`를 `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`로 빌드했다(뒤에 오는 `-mcpu`가 앞의 것을 덮는다). FPU 컨텍스트 저장(lazy stacking)은 B6의 주제다.

```c
/* fpu — Cortex-M4F: the first float instruction needs CPACR enabled by startup */
#include <stdint.h>
#include "board.h"
volatile float g_gyro_dps = 12.5f;
int main(void)
{
    uint32_t cpacr = *(volatile uint32_t *)0xE000ED88u;
    board_printf_raw("CPACR=0x%x\n", (unsigned)cpacr);
    float rate = g_gyro_dps * 0.5f + 1.0f;          /* vldr / vmul / vadd */
    board_printf_raw("rate x100 = %d\n", (int)(rate * 100.0f));
    return 0;
}
```

```sh
# ~/.rtos_scratch/B5/runm4.sh
timeout --foreground 3 qemu-system-arm -machine mps2-an386 -cpu cortex-m4 -nographic -monitor none \
  -serial stdio -semihosting-config enable=on,target=native -kernel $1
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && for f in "" -DNO_FPU_ENABLE; do echo "=== M4F startup_b5.c $f"; ./bld.sh fpu common/b5.ld common/startup_b5.c fpu/main.c -DCOPY_MODE=1 $f -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard | tail -1; ./runm4.sh /var/tmp/rtos/B5/fpu/app.elf; done'
```

```text
=== M4F startup_b5.c 
   4148	    100	   8536	  12784	   31f0	/var/tmp/rtos/B5/fpu/app.elf
CPACR=0xf00000
rate x100 = 725
[board_exit] code=0
=== M4F startup_b5.c -DNO_FPU_ENABLE
   4124	    100	   8536	  12760	   31d8	/var/tmp/rtos/B5/fpu/app.elf
CPACR=0x0

[HardFault] PC=0x3d0 LR=0x3cd xPSR=0x61000000 CFSR=0x80000 HFSR=0x40000000
[board_exit] code=255
```

출력에서 볼 것:

- 정상: CPACR = `0xf00000` — bit 20~23(CP10 · CP11 = 0b11, full access). 12.5 × 0.5 + 1 = 7.25가 맞게 나왔다.
- CPACR를 안 켜면 정수 printf까지는 돌다가 **첫 `vldr`에서 HardFault**. CFSR = `0x80000` = UFSR의 **NOCP**(bit 19, "코프로세서 없음"), HFSR FORCED. PC 0x3d0이 main 안의 그 float 명령이다.
- CPACR 쓰기 뒤의 `dsb; isb`는 "설정이 다음 명령부터 확실히 반영되게" 하는 Arm 권장 순서다.
- 이 코드는 `#if defined(__ARM_FP)`로 감쌌다. M3 빌드에서는 컴파일러가 `__ARM_FP`를 정의하지 않아 아예 빠진다(위의 M3 예제들 크기에 영향 없음). Zephyr는 같은 일을 `z_prep_c()` 안의 `z_arm_floating_point_init()`에서 한다(§10).

---

## 6. 생성자 — `__libc_init_array`, `.preinit_array`, `.init_array`, 그리고 C++

> 🔁 이 절은 Bare-Metal BM F4와 겹친다 (주인: 이 노트).

### 6.1 직관 — 생성자는 "함수 포인터 표"다

`__attribute__((constructor))` 함수나 C++ 전역 객체(`static ImuDriver g_imu(1000);`)의 생성자는 main 전에 돌아야 한다. 컴파일러는 이것을 **함수 포인터 하나를 `.init_array` 섹션에 넣는** 것으로 구현한다. 누군가 그 표를 처음부터 끝까지 돌며 호출해야 하는데, Linux에서는 C 런타임(`crt1.o` · `__libc_start_main`)이 한다(EL B3). MCU에서는 **startup이 `__libc_init_array()`를 불러야** 한다. newlib의 `__libc_init_array`는 이 순서로 부른다.

1. `__preinit_array_start` ~ `__preinit_array_end`의 함수들
2. `_init()` (원래 `crti.o` · `crtn.o`가 만드는 함수)
3. `__init_array_start` ~ `__init_array_end`의 함수들 (우선순위 붙은 것이 먼저 — 링커의 `SORT`)

이 경계 심볼은 **링커 스크립트가 만들어야 한다**(§2.6의 `PROVIDE_HIDDEN`). 랩 템플릿에는 그게 없다.

### 6.2 예제 14 — 템플릿은 생성자를 조용히 버린다 → 링크 에러 → 고친 버전

무엇을 확인하나: C 생성자 세 종류(`.preinit_array` 포인터, `constructor(101)`, `constructor`)와 C++ 전역 객체 하나를 만들고, 세 가지 조합으로 빌드한다. 생성자는 UART가 켜지기 전에 돌므로 printf 대신 **실행 순서를 배열에 기록**한다.

```c
/* ctor — who runs constructors before main? */
#include <stdint.h>
#include "board.h"
static char order[8]; static int n;
static void mark(char c) { order[n++] = c; }      /* runs before board_init: no UART yet */

static void pre(void) { mark('P'); }
__attribute__((section(".preinit_array"), used)) static void (*const p_pre)(void) = pre;
__attribute__((constructor(101))) static void ctor_early(void) { mark('1'); }
__attribute__((constructor))      static void ctor_plain(void) { mark('2'); }
extern int g_cpp_ready;                          /* set by a C++ static object (ctor.cpp) */
void cpp_mark(char c) { mark(c); }

int main(void)
{
    order[n] = 0;
    board_printf_raw("constructors ran in order: '%s'  g_cpp_ready=%d\n", order, g_cpp_ready);
    return n == 4 ? 0 : 1;
}
```

```c
// ctor.cpp — a C++ global object whose constructor must run before main
extern "C" void cpp_mark(char c);
extern "C" { int g_cpp_ready; }
struct ImuDriver {
    int odr_hz;
    ImuDriver(int hz) : odr_hz(hz) { g_cpp_ready = hz; cpp_mark('C'); }
    ~ImuDriver() { g_cpp_ready = 0; }
};
static ImuDriver g_imu(1000);
```

C++ 파일은 `arm-none-eabi-g++`로 따로 컴파일해서 오브젝트로 넘겼다.

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && O=/var/tmp/rtos/B5/ctor; mkdir -p $O; arm-none-eabi-g++ -mthumb -mcpu=cortex-m3 -O2 -g -Wall -Wextra -fno-exceptions -fno-rtti -c ctor/ctor.cpp -o $O/ctor.o && arm-none-eabi-nm $O/ctor.o; arm-none-eabi-objdump -h $O/ctor.o | grep -A1 init_array'
```

```text
00000000 t _GLOBAL__sub_I_g_cpp_ready
00000004 b _ZL5g_imu
00000000 W _ZN9ImuDriverD1Ev
00000000 W _ZN9ImuDriverD2Ev
00000000 n _ZN9ImuDriverD5Ev
         U __aeabi_atexit
         U __dso_handle
         U cpp_mark
00000000 B g_cpp_ready
  6 .init_array   00000004  00000000  00000000  0000007c  2**2
                  CONTENTS, ALLOC, LOAD, RELOC, DATA
```

g++는 `_GLOBAL__sub_I_g_cpp_ready`라는 함수를 만들고 그 포인터를 `.init_array`(4바이트)에 넣었다. 그 함수는 생성자를 부르고, 소멸자를 `__aeabi_atexit(obj, dtor, &__dso_handle)`로 등록한다 — 그래서 `__dso_handle`이 필요하다(U = 정의 필요).

**A. 랩 startup.c + 랩 mps2_m3.ld**:

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && O=/var/tmp/rtos/B5/ctor; ./bld.sh ctorA common/mps2_m3.ld common/startup.c "ctor/main.c $O/ctor.o" && /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B5/ctorA/app.elf 3; sed -n 125,130p /var/tmp/rtos/B5/ctorA/app.map'
```

```text
   4176	     96	    352	   4624	   1210	/var/tmp/rtos/B5/ctorA/app.elf
constructors ran in order: ''  g_cpp_ready=0
[board_exit] code=1
 .init_array.00101
                0x00000000        0x4 /tmp/cckYH5uW.o
 .text.startup.ctor_plain
                0x00000000       0x1c /tmp/cckYH5uW.o
 .init_array    0x00000000        0x4 /tmp/cckYH5uW.o
 .text.cpp_mark
```

링크 성공, 경고 없음, **생성자는 하나도 안 돌았다**(`''`, `g_cpp_ready=0`). map 파일의 이 부분(52번째 줄부터 시작하는 "Discarded input sections")을 보면 `.init_array.00101`과 `.init_array`, 그리고 생성자 함수 자체(`.text.startup.ctor_plain`)가 **버려졌다.** 링커 스크립트에 KEEP도 없고 startup이 표를 읽지도 않으니 `--gc-sections`에게는 쓰레기다. C++ 드라이버 객체가 "생성 안 된 채로" main이 시작된다 — vtable 포인터가 0인 객체의 가상 함수를 부르면 주소 0으로 점프한다.

**B. `__libc_init_array`를 부르는 startup + `b5.ld`, 단 `_init` · `__dso_handle` 스텁 없이(`-DNO_INIT_STUB`)**:

```text
/Users/donh/.rtos_scratch/B5/ctor/ctor.cpp:9:(.text.startup+0x28): undefined reference to `__dso_handle'
/build/newlib-Ipb6mw/newlib-4.6.0.20260123/build_nano/arm-none-eabi/thumb/v7-m/nofp/newlib/../../../../../../newlib/libc/misc/init.c:40:(.text+0x1e): undefined reference to `_init'
(_init): Unknown destination type (ARM/Thumb) in /usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/lib/thumb/v7-m/nofp/libg_nano.a(libc_a-init.o)
/build/newlib-Ipb6mw/newlib-4.6.0.20260123/build_nano/arm-none-eabi/thumb/v7-m/nofp/newlib/../../../../../../newlib/libc/misc/init.c:40:(.text+0x1e): undefined reference to `_init'
/var/tmp/rtos/B5/ctorB/app.elf: hidden symbol `__dso_handle' isn't defined
final link failed: bad value
```

(빌드 명령은 아래 C와 같고 `-DNO_INIT_STUB`만 붙였다. 각 줄 앞의 `ld:` 경로와 `in function` · `collect2` 줄은 걸러 냈다.) `-nostartfiles`는 `crti.o` · `crtn.o` · `crtbegin.o` · `crtend.o`를 링크에서 뺀다. 그런데 newlib의 `__libc_init_array`는 `_init()`(crti/crtn 제공)을 부르고, g++ 코드는 `__dso_handle`(crtbegin 제공)을 참조한다. **"생성자를 켜려고 `__libc_init_array`를 넣었더니 링크가 안 된다"**의 정체다. 해결은 둘 중 하나: `-nostartfiles`를 빼고 툴체인의 crt 파일을 쓰거나, 빈 `_init`/`_fini`와 `__dso_handle`을 직접 정의한다(`startup_b5.c`가 후자).

**C. `startup_b5.c` + `b5.ld`**:

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && O=/var/tmp/rtos/B5/ctor; ./bld.sh ctorC common/b5.ld common/startup_b5.c "ctor/main.c $O/ctor.o" -DCOPY_MODE=1 && /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B5/ctorC/app.elf 3; arm-none-eabi-objdump -h /var/tmp/rtos/B5/ctorC/app.elf | grep -A0 "_array"; arm-none-eabi-objdump -s -j .init_array -j .preinit_array /var/tmp/rtos/B5/ctorC/app.elf | tail -4; arm-none-eabi-nm /var/tmp/rtos/B5/ctorC/app.elf | grep -i "ctor_\|_GLOBAL__sub\| pre$"'
```

```text
   text	   data	    bss	    dec	    hex	filename
   4328	    112	   8560	  13000	   32c8	/var/tmp/rtos/B5/ctorC/app.elf
constructors ran in order: 'P12C'  g_cpp_ready=1000
[board_exit] code=0
  2 .preinit_array 00000004  000010e8  000010e8  000110e8  2**2
--
  3 .init_array   0000000c  000010ec  000010ec  000110ec  2**2
Contents of section .preinit_array:
 10e8 c9030000                             ....            
Contents of section .init_array:
 10ec e5030000 01040000 79040000           ........y...    
00000478 t _GLOBAL__sub_I_g_cpp_ready
000003e4 t ctor_early
00000400 t ctor_plain
000003c8 t pre
```

출력에서 볼 것:

- 순서 **P → 1 → 2 → C**: `.preinit_array`가 먼저, 그다음 `.init_array` 중 우선순위 101(`SORT(.init_array.*)`로 앞에 정렬), 그다음 우선순위 없는 것들이 링크 순서대로(main.c가 ctor.o보다 앞). C++ 객체도 생성됐다(`g_cpp_ready=1000`).
- `.init_array`의 내용은 함수 주소 세 개 `0x3e5, 0x401, 0x479`이고, 각각 `ctor_early`(0x3e4) · `ctor_plain`(0x400) · `_GLOBAL__sub_I_g_cpp_ready`(0x478)에 Thumb 비트를 더한 값이다. **생성자 = 플래시에 있는 함수 포인터 표**라는 것이 바이트로 보인다.
- C++ 전역 객체 사이의 생성 순서는 **번역 단위(파일) 사이에서 정해지지 않는다**(static initialization order fiasco). 드라이버 객체 A의 생성자가 다른 파일의 객체 B를 쓰면 B가 아직 안 만들어졌을 수 있다. 펌웨어에서는 전역 객체 생성자를 "필드 초기화만" 하게 두고, 하드웨어 초기화는 main에서 명시적 `init()`으로 부르는 것이 정석이다.

**Don 경험과 연결**: SSD 펌웨어가 C였다면 이 절은 처음일 수 있다. 차량 · 로봇 쪽 펌웨어는 C++가 흔하다(Rivian · 로봇 회사). 새 프로젝트에서 "전역 객체가 초기화 안 된 것 같다"는 증상이 나오면 map 파일에서 `.init_array` 크기부터 본다.

---

## 7. main이 return하면

> 🔁 이 절은 Bare-Metal BM F4와 겹친다 (주인: 이 노트).

데스크톱 C에서 main의 return은 "프로세스 종료"다. MCU에는 돌아갈 OS가 없다. 그래서 **startup이 main 다음에 무엇을 해 두었느냐**가 전부다.

### 7.1 예제 15 — newlib `exit()` 경로: atexit와 C++ 소멸자

무엇을 확인하나: `startup_b5.c`는 `exit(main())`이다. newlib의 `exit()`는 `atexit`로 등록된 함수와 C++ 소멸자(`__aeabi_atexit`로 등록됨)를 역순으로 부르고, 마지막에 `_exit()`를 부른다. 랩 board.c의 `_exit`는 `board_exit`(QEMU 종료)다.

```c
/* exit — what happens after main returns, with newlib exit() in startup_b5.c */
#include <stdlib.h>
#include "board.h"
static void flush_log(void) { board_printf_raw("atexit: flush_log()\n"); }
void cpp_bye(int hz) { board_printf_raw("C++ ~ImuDriver(%d) via __aeabi_atexit\n", hz); }
void cpp_mark(char c) { (void)c; }
int main(void)
{
    atexit(flush_log);
    board_printf_raw("main: return 3\n");
    return 3;                      /* Reset_Handler: exit(main()) */
}
```

C++ 쪽은 예제 14의 `ctor.cpp`에서 소멸자만 `~ImuDriver() { cpp_bye(odr_hz); }`로 바꾼 `ctor_exit.cpp`다.

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && O=/var/tmp/rtos/B5/ctor; arm-none-eabi-g++ -mthumb -mcpu=cortex-m3 -O2 -g -Wall -Wextra -fno-exceptions -fno-rtti -c ctor/ctor_exit.cpp -o $O/ctor_exit.o && ./bld.sh exit common/b5.ld common/startup_b5.c "ctor/exit.c $O/ctor_exit.o" -DCOPY_MODE=1 | tail -1 && /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B5/exit/app.elf 3; echo "exit=$?"'
```

```text
   4640	    100	   8956	  13696	   3580	/var/tmp/rtos/B5/exit/app.elf
main: return 3
atexit: flush_log()
C++ ~ImuDriver(1000) via __aeabi_atexit
[board_exit] code=3
exit=1
```

출력에서 볼 것: **등록의 역순**으로 불렸다. C++ 소멸자는 main 전(생성자 안)에 먼저 등록됐고 `flush_log`는 main 안에서 나중에 등록됐으므로, `flush_log`가 먼저, 소멸자가 나중이다. 그다음 `_exit(3)` → `board_exit(3)`. 펌웨어에서 이 경로는 보통 **쓸모없거나 위험하다**: 드라이버 객체의 소멸자가 하드웨어를 끄고, 그 뒤 `_exit`가 무한 루프에 빠진다. 많은 팀이 `-fno-use-cxa-atexit`나 소멸자 없는 전역 객체로 이 코드를 아예 빼서 플래시를 아낀다(B11).

### 7.2 예제 16 — 돌아갈 곳이 없으면: PC = 0xFFFFFFFF

무엇을 확인하나: 리셋 벡터가 곧바로 main을 가리키거나, startup이 `bl main` 대신 `b main`(꼬리 점프)으로 들어가면 main의 return 주소는 **리셋 때의 LR = 0xFFFFFFFF**다. `startup_asm.S`를 `-DTAILCALL`로 빌드해서 재현했다.

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && ./bld.sh asm common/mps2_m3.ld common/startup_asm.S hello/ret.c -DTAILCALL | tail -1; /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B5/asm/app.elf 3; echo "exit=$?"'
```

```text
   3648	     96	    328	   4072	    fe8	/var/tmp/rtos/B5/asm/app.elf
main: returning 7

[HardFault] PC=0xfffffffe LR=0x2c9 xPSR=0x21000000 CFSR=0x1 HFSR=0x40000000
[board_exit] code=255
```

출력에서 볼 것:

- main의 `pop {r3, pc}`가 PC에 0xFFFFFFFF를 넣었다. bit 0은 Thumb 비트로 가고 PC = **0xFFFFFFFE**. Thread 모드에서는 이 값이 EXC_RETURN이 아니다(EXC_RETURN은 Handler 모드에서만 특별하다, B2 §5). 그냥 그 주소에서 명령을 가져오려다 실패한다.
- CFSR = `0x1` = MMFSR의 **IACCVIOL**(명령 접근 위반 — 0xE0000000 위는 실행 금지 System 영역), HFSR FORCED. 처음 이 줄을 빼먹고 `bl board_init` 다음에 바로 `b main`을 썼을 때는 LR이 `board_init`이 남긴 쓰레기 값이라 main이 엉뚱한 곳(0xcc, `board_putc` 한가운데)으로 "돌아가서" 결국 **Lockup**으로 끝났다(`R15=000000cc`, `can't escalate 3 to HardFault`). 어느 쪽이든 결과는 "알 수 없는 곳에서 죽는다"다.

### 7.3 런타임별로 main 뒤에 무엇이 있나

| 런타임 · startup | main이 return하면 |
|---|---|
| 이 랩 템플릿 | `board_exit(rc)` → semihosting으로 QEMU 종료 |
| `startup_b5.c` (newlib) | `exit(rc)` → atexit · C++ 소멸자 → `_exit(rc)` |
| ST CubeIDE `startup_stm32xxx.s` | `bl main` 다음 줄이 `bx lr`(또는 무한 루프 `LoopForever`) — 버전마다 다르다, 직접 확인 |
| 흔한 bare-metal 관례 | `while (1) { }` 또는 `__BKPT(0)`로 디버거에 알리고 멈춤, 또는 `NVIC_SystemReset()` |
| FreeRTOS | `vTaskStartScheduler()`는 **돌아오지 않는다.** 돌아왔다면 idle/timer 태스크를 만들 heap이 부족했던 것. main은 그 다음 줄에 `for (;;)`나 에러 처리를 둔다 |
| Zephyr | main은 "main 스레드"의 함수다. return하면 그 스레드가 끝날 뿐 커널과 다른 스레드는 계속 돈다(§10) |

**면접 포인트**: "main이 return하면?"의 정답은 "startup 코드가 정한다. 그래서 펌웨어는 return하지 않게 쓰고, startup은 return했을 때 안전하게 멈추거나 리셋하도록 쓴다. RTOS에서는 main이 스케줄러를 시작하고 돌아오지 않거나(FreeRTOS), 그 자체가 스레드다(Zephyr)."

---

## 8. 부팅 시간 — 복사와 0 채우기는 크기에 비례한다

### 8.1 직관

`Reset_Handler`의 일은 대부분 메모리 쓰기다. 그러니 시간은 **(.data 크기 + .bss 크기) ÷ 쓰기 속도**다. A7에서 FreeRTOS의 reset → main이 bare-metal보다 88배 길었던 이유(1.37 ms)가 커널이 아니라 `heap_4`의 `ucHeap[128 KB]`가 `.bss`에 있어서였다. A7은 heap을 16 KB로 줄이면 0.22 ms가 되는 것도 쟀다 — 112 KB 차이가 약 1.15 ms, 즉 **KB당 약 10 µs**다. 이 절에서는 그 숫자를 크기와 복사 방식별로 직접 재서 확인한다.

드론 비행 제어기라면 이 시간이 왜 중요한가: 비행 중 브라운아웃이나 워치독 리셋이 나면 **리셋에서 모터 제어 루프 재개까지가 곧 추락 위험 구간**이다. ESC는 신호가 끊기면 모터를 멈추고, IMU 필터도 다시 수렴해야 한다. "부팅 수 ms"는 이런 제품에서 요구사항이 된다.

### 8.2 예제 17 — .bss · .data 크기 × 복사 방식 측정

무엇을 확인하나: `startup_b5.c`(§4.3)가 남긴 25 MHz 카운터 값으로 진입 · 복사 · 0 채우기 · 생성자+board_init 구간을 잰다. 크기는 `-DBSS_KB` · `-DDATA_KB`로, 방식은 `-DCOPY_MODE`로 바꾼다.

- `COPY_MODE=0`: C 루프 그대로 → GCC가 `memcpy`/`memset` 호출로 바꾼다(§4.2). newlib-nano 구현을 탄다.
- `COPY_MODE=1`: 같은 C 루프, 치환 금지 → 워드당 `str` 1개 + 비교 + 분기
- `COPY_MODE=2`: 인라인 어셈블리 `ldmia/stmia` 4워드(16바이트)씩

```c
/* boot — how long does Reset_Handler take for a given .data / .bss size? */
#include <stdint.h>
#include "board.h"
#ifndef BSS_KB
#define BSS_KB 0
#endif
#ifndef DATA_KB
#define DATA_KB 0
#endif
extern volatile uint32_t g_boot_t[4];
#if BSS_KB
uint8_t big_bss[BSS_KB * 1024];
#endif
#if DATA_KB
uint8_t big_data[DATA_KB * 1024] = {1, 2, 3};
#endif
#define NS(c) ((unsigned)((c) * 40u))   /* 25 MHz -> 40 ns per count */

int main(void)
{
#if BSS_KB
    big_bss[BSS_KB * 1024 - 1] = 1;
#endif
#if DATA_KB
    big_data[DATA_KB * 1024 - 1]++;
#endif
    uint32_t t[4] = {g_boot_t[0], g_boot_t[1], g_boot_t[2], g_boot_t[3]};
    board_printf_raw("mode=%d data=%uKB bss=%uKB  entry=%u ns  copy=%u ns  zero=%u ns  "
                     "ctors+board=%u ns  reset->main=%u ns\n", COPY_MODE, DATA_KB, BSS_KB,
                     NS(t[0]), NS(t[1] - t[0]), NS(t[2] - t[1]), NS(t[3] - t[2]), NS(t[3]));
    return 0;
}
```

처음에는 배열을 `static`으로 선언했다가 `.bss`가 328바이트로 나왔다 — 쓰기만 하고 읽지 않는 static 배열을 컴파일러가 통째로 지웠다. 측정용 버퍼는 전역(외부 링크)으로 둬야 한다.

```sh
# boot/sweep.sh — 3 modes × (.bss 0..1024 KB, .data 4..256 KB), ICOUNT=1
cd /Users/donh/.rtos_scratch/B5
for m in 0 1 2; do
  for b in 0 16 64 128 256 512 1024; do
    ./bld.sh sw >/dev/null common/b5.ld common/startup_b5.c boot/main.c -DCOPY_MODE=$m -DBSS_KB=$b -DDATA_KB=0
    ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B5/sw/app.elf | grep mode=
  done
  for d in 4 16 64 256; do
    ./bld.sh sw >/dev/null common/b5.ld common/startup_b5.c boot/main.c -DCOPY_MODE=$m -DBSS_KB=0 -DDATA_KB=$d
    ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B5/sw/app.elf | grep mode=
  done
done
```

```text
mode=0 data=0KB bss=0KB  entry=80 ns  copy=2880 ns  zero=4840 ns  ctors+board=7800 ns  reset->main=15600 ns
mode=0 data=0KB bss=16KB  entry=80 ns  copy=2880 ns  zero=168680 ns  ctors+board=7800 ns  reset->main=179440 ns
mode=0 data=0KB bss=64KB  entry=80 ns  copy=2880 ns  zero=660200 ns  ctors+board=7800 ns  reset->main=670960 ns
mode=0 data=0KB bss=128KB  entry=80 ns  copy=2880 ns  zero=1315560 ns  ctors+board=7800 ns  reset->main=1326320 ns
mode=0 data=0KB bss=256KB  entry=80 ns  copy=2880 ns  zero=2626280 ns  ctors+board=7800 ns  reset->main=2637040 ns
mode=0 data=0KB bss=512KB  entry=80 ns  copy=2880 ns  zero=5247720 ns  ctors+board=7800 ns  reset->main=5258480 ns
mode=0 data=0KB bss=1024KB  entry=80 ns  copy=2880 ns  zero=10490600 ns  ctors+board=7800 ns  reset->main=10501360 ns
mode=0 data=4KB bss=0KB  entry=80 ns  copy=76600 ns  zero=4840 ns  ctors+board=7800 ns  reset->main=89320 ns
mode=0 data=16KB bss=0KB  entry=80 ns  copy=297800 ns  zero=4840 ns  ctors+board=7800 ns  reset->main=310520 ns
mode=0 data=64KB bss=0KB  entry=80 ns  copy=1182520 ns  zero=4840 ns  ctors+board=7800 ns  reset->main=1195240 ns
mode=0 data=256KB bss=0KB  entry=80 ns  copy=4721480 ns  zero=4840 ns  ctors+board=7800 ns  reset->main=4734200 ns
mode=1 data=0KB bss=0KB  entry=120 ns  copy=3720 ns  zero=8240 ns  ctors+board=7880 ns  reset->main=19960 ns
mode=1 data=0KB bss=16KB  entry=120 ns  copy=3720 ns  zero=401440 ns  ctors+board=7880 ns  reset->main=413160 ns
mode=1 data=0KB bss=64KB  entry=120 ns  copy=3720 ns  zero=1581120 ns  ctors+board=7840 ns  reset->main=1592800 ns
mode=1 data=0KB bss=128KB  entry=120 ns  copy=3720 ns  zero=3153960 ns  ctors+board=7880 ns  reset->main=3165680 ns
mode=1 data=0KB bss=256KB  entry=120 ns  copy=3720 ns  zero=6299680 ns  ctors+board=7880 ns  reset->main=6311400 ns
mode=1 data=0KB bss=512KB  entry=120 ns  copy=3720 ns  zero=12591160 ns  ctors+board=7880 ns  reset->main=12602880 ns
mode=1 data=0KB bss=1024KB  entry=120 ns  copy=3720 ns  zero=25174080 ns  ctors+board=7840 ns  reset->main=25185760 ns
mode=1 data=4KB bss=0KB  entry=120 ns  copy=134760 ns  zero=8280 ns  ctors+board=7880 ns  reset->main=151040 ns
mode=1 data=16KB bss=0KB  entry=120 ns  copy=528000 ns  zero=8240 ns  ctors+board=7880 ns  reset->main=544240 ns
mode=1 data=64KB bss=0KB  entry=120 ns  copy=2100840 ns  zero=8280 ns  ctors+board=7880 ns  reset->main=2117120 ns
mode=1 data=256KB bss=0KB  entry=120 ns  copy=8392320 ns  zero=8240 ns  ctors+board=7880 ns  reset->main=8408560 ns
mode=2 data=0KB bss=0KB  entry=120 ns  copy=1440 ns  zero=3480 ns  ctors+board=7840 ns  reset->main=12880 ns
mode=2 data=0KB bss=16KB  entry=120 ns  copy=1440 ns  zero=134560 ns  ctors+board=7840 ns  reset->main=143960 ns
mode=2 data=0KB bss=64KB  entry=120 ns  copy=1440 ns  zero=527760 ns  ctors+board=7840 ns  reset->main=537160 ns
mode=2 data=0KB bss=128KB  entry=120 ns  copy=1440 ns  zero=1052040 ns  ctors+board=7840 ns  reset->main=1061440 ns
mode=2 data=0KB bss=256KB  entry=120 ns  copy=1440 ns  zero=2100640 ns  ctors+board=7840 ns  reset->main=2110040 ns
mode=2 data=0KB bss=512KB  entry=120 ns  copy=1440 ns  zero=4197800 ns  ctors+board=7840 ns  reset->main=4207200 ns
mode=2 data=0KB bss=1024KB  entry=120 ns  copy=1440 ns  zero=8392080 ns  ctors+board=7840 ns  reset->main=8401480 ns
mode=2 data=4KB bss=0KB  entry=120 ns  copy=42400 ns  zero=3480 ns  ctors+board=7840 ns  reset->main=53840 ns
mode=2 data=16KB bss=0KB  entry=120 ns  copy=165280 ns  zero=3480 ns  ctors+board=7840 ns  reset->main=176720 ns
mode=2 data=64KB bss=0KB  entry=120 ns  copy=656800 ns  zero=3480 ns  ctors+board=7840 ns  reset->main=668240 ns
mode=2 data=256KB bss=0KB  entry=120 ns  copy=2622880 ns  zero=3480 ns  ctors+board=7840 ns  reset->main=2634320 ns
```

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<line x1="60" y1="320" x2="420" y2="320" stroke="currentColor" stroke-width="1.2"/>
<line x1="60" y1="320" x2="60" y2="30" stroke="currentColor" stroke-width="1.2"/>
<line x1="60" y1="320" x2="60" y2="324" stroke="currentColor" stroke-width="1.2"/>
<text x="60" y="338" font-size="11" text-anchor="middle">0</text>
<line x1="150" y1="320" x2="150" y2="324" stroke="currentColor" stroke-width="1.2"/>
<text x="150" y="338" font-size="11" text-anchor="middle">256</text>
<line x1="240" y1="320" x2="240" y2="324" stroke="currentColor" stroke-width="1.2"/>
<text x="240" y="338" font-size="11" text-anchor="middle">512</text>
<line x1="330" y1="320" x2="330" y2="324" stroke="currentColor" stroke-width="1.2"/>
<text x="330" y="338" font-size="11" text-anchor="middle">768</text>
<line x1="420" y1="320" x2="420" y2="324" stroke="currentColor" stroke-width="1.2"/>
<text x="420" y="338" font-size="11" text-anchor="middle">1024</text>
<line x1="56" y1="320" x2="60" y2="320" stroke="currentColor" stroke-width="1.2"/>
<text x="52" y="324" font-size="11" text-anchor="end">0</text>
<line x1="56" y1="264" x2="60" y2="264" stroke="currentColor" stroke-width="1.2"/>
<text x="52" y="268" font-size="11" text-anchor="end">5</text>
<line x1="60" y1="264" x2="420" y2="264" stroke="#888" stroke-width="0.4"/>
<line x1="56" y1="208" x2="60" y2="208" stroke="currentColor" stroke-width="1.2"/>
<text x="52" y="212" font-size="11" text-anchor="end">10</text>
<line x1="60" y1="208" x2="420" y2="208" stroke="#888" stroke-width="0.4"/>
<line x1="56" y1="153" x2="60" y2="153" stroke="currentColor" stroke-width="1.2"/>
<text x="52" y="157" font-size="11" text-anchor="end">15</text>
<line x1="60" y1="153" x2="420" y2="153" stroke="#888" stroke-width="0.4"/>
<line x1="56" y1="97" x2="60" y2="97" stroke="currentColor" stroke-width="1.2"/>
<text x="52" y="101" font-size="11" text-anchor="end">20</text>
<line x1="60" y1="97" x2="420" y2="97" stroke="#888" stroke-width="0.4"/>
<line x1="56" y1="41" x2="60" y2="41" stroke="currentColor" stroke-width="1.2"/>
<text x="52" y="45" font-size="11" text-anchor="end">25</text>
<line x1="60" y1="41" x2="420" y2="41" stroke="#888" stroke-width="0.4"/>
<text x="240" y="358" font-size="12" text-anchor="middle">.bss 크기 (KB)</text>
<text x="16" y="26" font-size="12" text-anchor="start">ms</text>
<path d="M 60.0,319.9 L 65.6,315.5 L 82.5,302.4 L 105.0,284.8 L 150.0,249.7 L 240.0,179.6 L 420.0,39.2" stroke="#d0564a" stroke-width="2" fill="none"/>
<circle cx="60.0" cy="319.9" r="3" fill="#d0564a"/>
<circle cx="65.6" cy="315.5" r="3" fill="#d0564a"/>
<circle cx="82.5" cy="302.4" r="3" fill="#d0564a"/>
<circle cx="105.0" cy="284.8" r="3" fill="#d0564a"/>
<circle cx="150.0" cy="249.7" r="3" fill="#d0564a"/>
<circle cx="240.0" cy="179.6" r="3" fill="#d0564a"/>
<circle cx="420.0" cy="39.2" r="3" fill="#d0564a"/>
<path d="M 60.0,319.9 L 65.6,318.1 L 82.5,312.6 L 105.0,305.3 L 150.0,290.7 L 240.0,261.5 L 420.0,203.0" stroke="#4a7bd0" stroke-width="2" fill="none"/>
<circle cx="60.0" cy="319.9" r="3" fill="#4a7bd0"/>
<circle cx="65.6" cy="318.1" r="3" fill="#4a7bd0"/>
<circle cx="82.5" cy="312.6" r="3" fill="#4a7bd0"/>
<circle cx="105.0" cy="305.3" r="3" fill="#4a7bd0"/>
<circle cx="150.0" cy="290.7" r="3" fill="#4a7bd0"/>
<circle cx="240.0" cy="261.5" r="3" fill="#4a7bd0"/>
<circle cx="420.0" cy="203.0" r="3" fill="#4a7bd0"/>
<path d="M 60.0,320.0 L 65.6,318.5 L 82.5,314.1 L 105.0,308.3 L 150.0,296.6 L 240.0,273.2 L 420.0,226.4" stroke="#3f9a6b" stroke-width="2" fill="none"/>
<circle cx="60.0" cy="320.0" r="3" fill="#3f9a6b"/>
<circle cx="65.6" cy="318.5" r="3" fill="#3f9a6b"/>
<circle cx="82.5" cy="314.1" r="3" fill="#3f9a6b"/>
<circle cx="105.0" cy="308.3" r="3" fill="#3f9a6b"/>
<circle cx="150.0" cy="296.6" r="3" fill="#3f9a6b"/>
<circle cx="240.0" cy="273.2" r="3" fill="#3f9a6b"/>
<circle cx="420.0" cy="226.4" r="3" fill="#3f9a6b"/>
<line x1="440" y1="56" x2="460" y2="56" stroke="#d0564a" stroke-width="2"/>
<text x="466" y="60" font-size="11" text-anchor="start">C 워드 루프 (str 1개/워드)</text>
<text x="466" y="76" font-size="11" text-anchor="start">128 KB: 3.15 ms · 1 MB: 25.2 ms</text>
<line x1="440" y1="102" x2="460" y2="102" stroke="#4a7bd0" stroke-width="2"/>
<text x="466" y="106" font-size="11" text-anchor="start">newlib-nano memset (strd ×2)</text>
<text x="466" y="122" font-size="11" text-anchor="start">128 KB: 1.32 ms · 1 MB: 10.5 ms</text>
<line x1="440" y1="148" x2="460" y2="148" stroke="#3f9a6b" stroke-width="2"/>
<text x="466" y="152" font-size="11" text-anchor="start">ldm/stm 4워드 (stmia)</text>
<text x="466" y="168" font-size="11" text-anchor="start">128 KB: 1.05 ms · 1 MB: 8.4 ms</text>
<text x="440" y="220" font-size="11" text-anchor="start">QEMU icount: 명령 1개 = 32 ns</text>
<text x="440" y="236" font-size="11" text-anchor="start">(사이클이 아니라 명령 수를 센다:</text>
<text x="440" y="252" font-size="11" text-anchor="start"> stmia 4워드도 명령 1개)</text>
</svg>
```

그림 6 — `.bss` 0 채우기 시간 vs 크기(예제 17 측정, QEMU icount). 세 방식 모두 완벽한 직선이다. 기울기 = 방식의 "워드당 명령 수".

기울기를 KB당 시간과 워드당 명령 수로 바꾸면(명령 1개 = 32 ns, 1 KB = 256워드):

| 방식 | .bss 0 (µs/KB) | 워드당 명령 | .data 복사 (µs/KB) | 워드당 명령 |
|---|---|---|---|---|
| C 워드 루프 (`str`, `cmp`, `bcc`) | 24.6 | 3.0 | 32.8 | 4.0 |
| newlib-nano `memset`/`memcpy` | 10.2 | 1.25 | 18.4 | 2.25 |
| `ldmia`/`stmia` 4워드 | 8.2 | 1.0 | 10.2 | 1.25 |

(1 MB 0 채우기: 25174080 ns ÷ 1024 = 24.6 µs/KB 같은 식으로 계산. 256 KB 복사: 8392320 ÷ 256 = 32.8.)

출력에서 볼 것:

- **완전히 선형**이다. .bss 128 KB, newlib `memset` 방식 = 1.316 ms. A7의 "1.37 ms"(그때 .bss 135 KB, 같은 랩 startup)와 KB당 약 10 µs로 정확히 맞는다. 부팅 시간 문제는 대부분 "누가 .bss에 큰 배열을 넣었나"로 귀결된다.
- 아무것도 없을 때(0 KB) reset → main은 13~20 µs. 그중 `ctors+board` 7.8 µs는 `board_init`의 `setvbuf` 등이다. **startup 자체는 거의 공짜**이고, 비용은 전부 크기에서 나온다.
- 방식 차이는 크다: 같은 1 MB가 25.2 ms(C 루프) vs 8.4 ms(`stmia`). 그런데 **QEMU는 명령 개수를 센다** — `stmia {r3-r6}`도 명령 1개로 친다. 실제 Cortex-M3에서 `STM`은 1 + N사이클(N = 레지스터 수), `STR`은 보통 2사이클(연속 load/store는 파이프라인되어 1), 분기는 1 + P사이클(파이프라인 재충전 1~3)이다(Cortex-M3 TRM 명령 타이밍 표). 그 표로 **추정**하면 C 루프는 워드당 5사이클 안팎, `stmia` 루프는 워드당 2.5사이클 안팎이다. 25 MHz에서 128 KB면 약 6.6 ms vs 3.3 ms, 168 MHz면 약 1.0 ms vs 0.5 ms. **이 실칩 숫자는 측정이 아니라 계산**이고, 여기에 flash wait state(루프가 플래시에서 돈다)와 버스 대기가 더해진다.
- 실칩에서 진짜 상한은 **메모리 대역폭**이다. 32비트 SRAM 포트에 사이클당 1워드가 최선이다. 그래서 대형 칩(H7 · R52)은 DMA나 하드웨어 메모리 초기화(ECC SRAM의 자동 0 채우기 기능)를 쓰기도 한다.

### 8.3 예제 18 — FreeRTOS: ucHeap을 `.noinit`으로 옮기면

무엇을 확인하나: 측정 결과의 결론을 RTOS에 적용한다. FreeRTOS `heap_4`의 `ucHeap[configTOTAL_HEAP_SIZE]`는 기본적으로 `heap_4.c` 안의 static 배열(.bss)이다. `configAPPLICATION_ALLOCATED_HEAP = 1`이면 `heap_4.c`가 `extern uint8_t ucHeap[]`로 선언만 하고(V11.3.2 `portable/MemMang/heap_4.c` 89~96줄), 정의는 앱이 한다 — 그 정의를 `.noinit`에 둔다. heap_4는 `prvHeapInit()`에서 블록 헤더를 직접 써 넣으므로 heap 메모리가 0일 필요가 없다. 랩 FreeRTOS 템플릿(Makefile, 랩 startup.c, 랩 mps2_m3.ld — 거기에도 `.noinit`이 있다) 그대로 빌드했다.

```c
/* frt — FreeRTOS: reset->main cost is .bss zeroing of ucHeap; move the heap to .noinit */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#define CYC (*(volatile uint32_t *)0x40028018u)
#if configAPPLICATION_ALLOCATED_HEAP == 1
__attribute__((section(".noinit"), aligned(8))) uint8_t ucHeap[configTOTAL_HEAP_SIZE];
#endif
extern uint32_t _sbss, _ebss;
static uint32_t t_main;

static void first_task(void *arg)
{
    (void)arg;
    uint32_t t_task = CYC;
    board_printf_raw("bss=%u B  heap@%x  reset->main=%u ns  reset->first task=%u ns\n",
                     (unsigned)((char *)&_ebss - (char *)&_sbss), (unsigned)pvPortMalloc(16),
                     t_main * 40u, t_task * 40u);
    board_exit(0);
}

int main(void)
{
    t_main = CYC;                      /* no kernel code has run yet */
    xTaskCreate(first_task, "first", 256, NULL, 2, NULL);
    vTaskStartScheduler();             /* only now: idle task, timer task, SysTick, SVC */
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'for v in "" "-DconfigAPPLICATION_ALLOCATED_HEAP=1"; do make -s -B -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/B5/frt/main.c OUT=/var/tmp/rtos/B5/frt EXTRA_CFLAGS="$v" | tail -1 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B5/frt/app.elf; done'
```

```text
  11708	    100	 135408	 147216	  23f10	/var/tmp/rtos/B5/frt/app.elf
bss=135404 B  heap@20001480  reset->main=1365800 ns  reset->first task=1476000 ns
[board_exit] code=0
  11708	    100	 135404	 147212	  23f0c	/var/tmp/rtos/B5/frt/app.elf
bss=4332 B  heap@200015d0  reset->main=55080 ns  reset->first task=165280 ns
[board_exit] code=0
```

출력에서 볼 것:

- reset → main **1.366 ms → 0.055 ms (25배)**, reset → 첫 태스크 1.476 ms → 0.165 ms. 실제 .bss가 135404 → 4332바이트로 줄었다.
- `size`의 `bss` 열은 두 경우 모두 약 135 KB다 — `.noinit`도 NOLOAD라서 `size`가 bss로 센다. 진짜 .bss는 `_ebss - _sbss`로 봐야 한다(§3.5의 교훈).
- main → 첫 태스크 구간(약 110 µs)은 그대로다. 그게 진짜 커널 비용(xTaskCreate · idle/timer 태스크 생성 · 첫 컨텍스트 스위치, C3)이다.
- 주의: `.noinit` heap은 리셋 뒤 **이전 내용이 남아 있다.** heap_4는 상관없지만, `pvPortMalloc`으로 받은 메모리를 0이라고 가정하는 코드(흔한 버그)는 콜드 부팅 때만 우연히 동작하다가 웜 리셋에서 깨진다. 그런 코드는 `pvPortCalloc`(V11 heap_4에 있다)이나 명시적 memset을 쓴다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="242" y="48" font-size="12" text-anchor="end">bare-metal · 랩 startup (memset)</text>
<rect x="250" y="30" width="191.0" height="28" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<text x="447" y="48" font-size="11" text-anchor="start">1.33 ms</text>
<text x="242" y="88" font-size="12" text-anchor="end">bare-metal · C 워드 루프</text>
<rect x="250" y="70" width="225.0" height="28" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/>
<text x="481" y="88" font-size="11" text-anchor="start">3.17 ms</text>
<text x="242" y="128" font-size="12" text-anchor="end">bare-metal · ldm/stm</text>
<rect x="250" y="110" width="182.3" height="28" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/>
<text x="438" y="128" font-size="11" text-anchor="start">1.06 ms</text>
<text x="242" y="168" font-size="12" text-anchor="end">FreeRTOS · ucHeap 128 KB in .bss</text>
<rect x="250" y="150" width="192.2" height="28" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
<text x="448" y="168" font-size="11" text-anchor="start">1.37 ms</text>
<text x="242" y="208" font-size="12" text-anchor="end">FreeRTOS · ucHeap in .noinit</text>
<rect x="250" y="190" width="66.7" height="28" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/>
<text x="323" y="208" font-size="11" text-anchor="start">0.055 ms</text>
<text x="242" y="248" font-size="12" text-anchor="end">Zephyr · 128 KB .bss, SIZE_OPT</text>
<rect x="250" y="230" width="290.6" height="28" fill="#888" fill-opacity="0.55" stroke="#888"/>
<text x="547" y="248" font-size="11" text-anchor="start">16.95 ms</text>
<text x="242" y="288" font-size="12" text-anchor="end">Zephyr · 128 KB .bss, SPEED_OPT</text>
<rect x="250" y="270" width="192.7" height="28" fill="#888" fill-opacity="0.55" stroke="#888"/>
<text x="449" y="288" font-size="11" text-anchor="start">1.38 ms</text>
<text x="242" y="328" font-size="12" text-anchor="end">Zephyr · .bss 588 B (기본)</text>
<rect x="250" y="310" width="112.0" height="28" fill="#888" fill-opacity="0.55" stroke="#888"/>
<text x="368" y="328" font-size="11" text-anchor="start">0.176 ms</text>
<line x1="250" y1="350" x2="610" y2="350" stroke="currentColor" stroke-width="1.2"/>
<line x1="250" y1="24" x2="250" y2="350" stroke="currentColor" stroke-width="1.2"/>
<line x1="250" y1="350" x2="250" y2="354" stroke="currentColor" stroke-width="1.2"/>
<text x="250" y="368" font-size="11" text-anchor="middle">0.01</text>
<line x1="340" y1="350" x2="340" y2="354" stroke="currentColor" stroke-width="1.2"/>
<text x="340" y="368" font-size="11" text-anchor="middle">0.1</text>
<line x1="340" y1="24" x2="340" y2="350" stroke="#888" stroke-width="0.4" stroke-dasharray="4 3"/>
<line x1="430" y1="350" x2="430" y2="354" stroke="currentColor" stroke-width="1.2"/>
<text x="430" y="368" font-size="11" text-anchor="middle">1</text>
<line x1="430" y1="24" x2="430" y2="350" stroke="#888" stroke-width="0.4" stroke-dasharray="4 3"/>
<line x1="520" y1="350" x2="520" y2="354" stroke="currentColor" stroke-width="1.2"/>
<text x="520" y="368" font-size="11" text-anchor="middle">10</text>
<line x1="520" y1="24" x2="520" y2="350" stroke="#888" stroke-width="0.4" stroke-dasharray="4 3"/>
<line x1="610" y1="350" x2="610" y2="354" stroke="currentColor" stroke-width="1.2"/>
<text x="610" y="368" font-size="11" text-anchor="middle">100</text>
<line x1="610" y1="24" x2="610" y2="350" stroke="#888" stroke-width="0.4" stroke-dasharray="4 3"/>
<text x="430" y="386" font-size="12" text-anchor="middle">reset → main 첫 줄 (ms, 로그 눈금) — QEMU mps2-an385, ICOUNT=1</text>
</svg>
```

그림 7 — .bss 128 KB 근처에서 reset → main 첫 줄까지(로그 눈금). bare-metal 세 방식(예제 17), FreeRTOS heap 위치(예제 18), Zephyr 최적화 설정(예제 22). 가장 큰 차이는 "0으로 채울 것을 줄인" FreeRTOS `.noinit`과 Zephyr의 libc 선택에서 나왔다.

---

## 9. XIP flash 실행 vs RAM 실행 — `.ramfunc`

### 9.1 직관과 정의

Cortex-M MCU의 코드는 보통 **내부 플래시에서 그대로 실행된다**. 이것을 XIP(eXecute In Place)라 한다. RAM으로 복사할 필요가 없어서 RAM을 아끼고 부팅이 빠르다. 대신:

- 플래시는 코어보다 느려서 **wait state**가 붙는다(§5.1). 칩은 prefetch 버퍼 · 플래시 캐시(ST의 ART 가속기 등)로 감추지만, 분기가 많은 코드에서는 지연이 드러나고 **실행 시간이 흔들린다**(지터).
- 플래시를 **지우거나 쓰는 동안**에는 같은 뱅크에서 명령을 가져올 수 없다(칩마다 다름). 펌웨어 업데이트 코드 · EEPROM 에뮬레이션은 RAM에서 돌아야 한다.

그래서 일부 함수만 RAM(또는 ITCM)에 둔다. 방법은 `.data`와 똑같다: **VMA는 RAM, LMA는 FLASH, startup이 복사**. 차이는 내용이 코드라는 것뿐이다.

### 9.2 예제 19 — RAM에서 도는 함수와 long-branch veneer

무엇을 확인하나: `section(".ramfunc")` 함수를 만들고(`b5.ld`가 `.data` 안에 `*(.ramfunc*)`를 넣는다) 주소, 플래시 안의 원본 위치, 실행 시간을 플래시 버전과 비교한다. `noipa`는 GCC가 함수 간 분석으로 결과를 미리 계산하거나 호출을 타이머 읽기 앞뒤로 옮기지 못하게 한다(처음 `noinline`만 붙였을 때 두 함수 모두 "1 cycle"이 나왔다 — 호출이 측정 구간 밖으로 옮겨졌다).

```c
/* ramfunc — run a function from RAM: section(".ramfunc") is copied with .data */
#include <stdint.h>
#include "board.h"
#define RAMFUNC __attribute__((section(".ramfunc"), noipa))
#define CYC (*(volatile uint32_t *)0x40028018u)

static uint32_t crc_step(uint32_t crc, uint32_t n)        /* same body twice */
{ for (uint32_t i = 0; i < n; i++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1)); return crc; }

__attribute__((noipa)) uint32_t crc_flash(uint32_t c, uint32_t n) { return crc_step(c, n); }
RAMFUNC uint32_t crc_ram(uint32_t c, uint32_t n)        { return crc_step(c, n); }
extern uint32_t _sidata, _sdata;

int main(void)
{
    uint32_t pc;
    board_printf_raw("crc_flash @ 0x%x   crc_ram @ 0x%x\n", (unsigned)crc_flash, (unsigned)crc_ram);
    board_printf_raw("crc_ram load copy in FLASH @ 0x%x\n",
        (unsigned)((uintptr_t)crc_ram - (uintptr_t)&_sdata + (uintptr_t)&_sidata));
    uint32_t t0 = CYC; uint32_t a = crc_flash(0xFFFFFFFFu, 20000); uint32_t t1 = CYC;
    uint32_t b = crc_ram(0xFFFFFFFFu, 20000);                  uint32_t t2 = CYC;
    board_printf_raw("flash: crc=%x %u cycles   ram: crc=%x %u cycles\n", a, t1 - t0, b, t2 - t1);
    __asm volatile("mov %0, pc" : "=r"(pc));
    board_printf_raw("main itself runs at pc=0x%x (XIP from flash)\n", pc);
    return a == b ? 0 : 1;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B5 && ./bld.sh ramf common/b5.ld common/startup_b5.c ramfunc/main.c -DCOPY_MODE=1 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B5/ramf/app.elf 5; arm-none-eabi-objdump -h /var/tmp/rtos/B5/ramf/app.elf | grep -A1 " .data"'
```

```text
   text	   data	    bss	    dec	    hex	filename
   4512	      0	   8536	  13048	   32f8	/var/tmp/rtos/B5/ramf/app.elf
crc_flash @ 0x3c9   crc_ram @ 0x20000061
crc_ram load copy in FLASH @ 0x117d
flash: crc=16aa7406 96009 cycles   ram: crc=16aa7406 96008 cycles
main itself runs at pc=0x448 (XIP from flash)
[board_exit] code=0
  2 .data         00000084  20000000  0000111c  00020000  2**2
                  CONTENTS, ALLOC, LOAD, CODE
```

main(플래시, 0x0 근처)에서 RAM(0x20000000 근처)까지는 512 MB 떨어져 있다. Thumb-2 `BL`의 도달 거리는 ±16 MB다. 그래서 링커가 무엇을 했는지 봤다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/B5/ramf; arm-none-eabi-nm app.elf | grep -i "crc_\|veneer"; grep -n -A3 "^ \.ramfunc" app.map; arm-none-eabi-objdump -d --no-show-raw-insn app.elf | grep -A4 "veneer>:"'
```

```text
00000fe8 t __crc_ram_veneer
000003c8 T crc_flash
20000060 T crc_ram
414: .ramfunc       0x20000060       0x24 /tmp/cc5BmlXL.o
415-                0x20000060                crc_ram
416-                0x20000084                        . = ALIGN (0x4)
417-                0x20000084                        _edata = .
00000fe8 <__crc_ram_veneer>:
     fe8:	ldr.w	pc, [pc]	@ fec <__crc_ram_veneer+0x4>
     fec:	.word	0x20000061
     ff0:	.word	0x6665445b
     ff4:	.word	0x746c7561
```

출력에서 볼 것:

- `crc_ram`의 주소는 **0x20000061**(RAM, Thumb 비트 포함), 플래시 원본은 0x117d. 두 함수가 같은 CRC를 냈다 — RAM에서 실제로 실행됐다.
- 링커가 플래시에 **`__crc_ram_veneer`**를 자동으로 만들었다: `ldr.w pc, [pc]` + 목적지 주소 한 워드(그 뒤 두 워드는 다음 데이터인 문자열이다). main의 `BL`은 이 veneer로 가고, veneer가 RAM으로 뛴다. 호출마다 명령 하나가 더 붙는다. GCC의 `__attribute__((long_call))`을 쓰면 컴파일러가 처음부터 `ldr r3, =func; blx r3`로 부른다(Zephyr의 GCC `__ramfunc` 매크로가 Arm에서 이 방식을 쓴다).
- **실행 시간은 96009 vs 96008 — 같다.** QEMU는 flash wait state를 모델링하지 않으므로 이 차이는 **보여 줄 수 없다.** 실칩(예: wait state 5인 STM32F4에서 ART 캐시가 미스 나는 루프)이라면 RAM 쪽이 빠르고 무엇보다 **일정**하다. 이 부분은 측정하지 못했다고 정직하게 적는다.
- `.data`의 플래그에 **CODE**가 붙었고, `size`가 `data` 열에 0을 찍고 그 132바이트를 `text`에 넣었다. `.data`와 `.ramfunc`를 한 출력 섹션에 섞은 부작용이다. 깔끔하게 하려면 `.ramfunc`를 별도 출력 섹션(`> RAM AT > FLASH`)으로 나누고 copy table(§2.7)에 한 줄을 추가한다(연습문제 2).
- 랩 board.c가 `.ramfunc`를 RAM에서 돌리는 데 추가 설정이 필요 없었던 것은 Cortex-M3의 기본 메모리 맵에서 SRAM 영역(0x20000000~)이 실행 가능이기 때문이다. MPU로 RAM을 XN(실행 금지)으로 막은 시스템에서는 그 영역만 예외로 열어야 한다(K 모듈).

**실무에서**: 모터 컨트롤러의 20 kHz FOC 전류 루프 ISR(50 µs마다)은 플래시 wait state로 인한 지터를 피하려고 ITCM이나 SRAM에 둔다. STM32G4 같은 칩은 CCM SRAM을, H7은 ITCM을 쓴다. 벡터 테이블까지 RAM으로 옮기는 것(VTOR)은 B2 §2.

---

## 10. RTOS 커널 안에서는 무슨 일이 일어나나

### 10.1 FreeRTOS — main까지는 아무것도 안 한다

FreeRTOS 커널은 **startup 코드를 갖고 있지 않다.** 벡터 테이블 · `Reset_Handler` · 링커 스크립트는 전부 칩 벤더나 앱이 준다(이 랩에서는 `templates/common`). 커널 코드는 main 안에서 처음 실행된다.

- `xTaskCreate()` → `prvCreateTask()` → `pvPortMalloc()`(heap_4) → `prvInitialiseNewTask()` → `pxPortInitialiseStack()`이 가짜 예외 프레임을 쌓는다(`tasks.c`, `portable/GCC/ARM_CM3/port.c`) — C3.
- `vTaskStartScheduler()`(`tasks.c`) → idle 태스크 · timer 태스크 생성 → `xPortStartScheduler()`(`port.c`): PendSV · SysTick 우선순위 설정, SysTick 시작, `prvPortStartFirstTask()`가 **MSP를 벡터 테이블 0번 워드 값으로 되돌리고**(main의 스택은 이제 버려지고 인터럽트 스택이 된다) `svc 0`으로 첫 태스크에 들어간다 — C3.

그래서 FreeRTOS에서 "main 전" 비용은 순수하게 startup의 몫이고(§8: 그중 대부분은 `ucHeap`), 링커 스크립트의 `_Main_Stack_Size`는 "main이 쓰는 스택 + 나중에 모든 ISR이 함께 쓰는 스택"이다(랩 ld의 주석이 그렇게 적혀 있다). §3.2에서 본 것처럼 공식 QEMU 데모의 startup은 `main()` 한 줄이다 — FreeRTOS는 그 부분에 관심이 없다는 뜻이기도 하다.

### 10.2 Zephyr — 커널이 main을 부른다 (v4.4.2 소스에서 확인한 이름)

Zephyr는 startup부터 main 호출까지 전부 커널이 갖고 있다. Cortex-M 경로를 v4.4.2 소스에서 따라가면:

| 단계 | 파일 · 함수 | 하는 일 |
|---|---|---|
| 1 | `arch/arm/core/cortex_m/vector_table.S` · `reset.S`의 `z_arm_reset` (`__start`와 같은 주소) | (옵션) CONTROL · MSPLIM 초기화, **MSP를 `z_main_stack` 꼭대기로 다시 설정**, `BASEPRI`로 인터럽트 잠금, **PSP를 `z_interrupt_stacks` 꼭대기로 설정하고 CONTROL.SPSEL = 1** (초기화 코드는 PSP로 돈다), `bl z_prep_c` |
| 2 | `arch/arm/core/cortex_m/prep_c.c`의 `z_prep_c()` | `soc_prep_hook()`, `relocate_vector_table()`(VTOR), `z_arm_floating_point_init()`(CPACR), **`arch_bss_zero()`**, **`arch_data_copy()`**, `z_arm_interrupt_init()`, `z_cstart()` |
| 3 | `arch/common/init.c`의 `arch_bss_zero()` | `arch_early_memset(__bss_start, 0, ...)` — `CONFIG_SKIP_BSS_CLEAR`면 건너뜀 |
| 4 | `arch/common/xip.c`의 `arch_data_copy()` | `arch_early_memcpy`로 `.data`, (있으면) ramfunc · nocache · ITCM/DTCM 영역 복사 — `CONFIG_XIP`일 때만 의미 |
| 5 | `kernel/init.c`의 `z_cstart()` | init level **EARLY** → `arch_kernel_init()` → `z_dummy_thread_init` → **PRE_KERNEL_1**(클럭 · UART 같은 기본 드라이버) → **PRE_KERNEL_2** → `switch_to_main_thread(prepare_multithreading())` |
| 6 | `arch/arm/core/cortex_m/thread.c`의 `arch_switch_to_main_thread()` | **PendSV도 SVC도 아니다**: `msr PSP, stack_ptr`, 인터럽트 잠금 해제, `bx z_thread_entry` |
| 7 | `kernel/init.c`의 `bg_thread_main()` (main 스레드 안) | **POST_KERNEL** → 부트 배너 → (`CONFIG_STATIC_INIT_GNU`면 C++ 생성자 `z_static_init_gnu()`) → **APPLICATION** → 정적 스레드 시작 → (SMP) → `main()` |

**확인한 사실 하나**: 오래된 Zephyr 글과 강의에는 `z_bss_zero()` · `z_data_copy()`라는 이름이 나온다. **v4.4.2 소스에는 그 이름의 함수가 없다** — `arch_bss_zero()`(`arch/common/init.c`) · `arch_data_copy()`(`arch/common/xip.c`)로 바뀌었다(전체 트리를 grep해서 `z_data_copy`는 한 SoC 파일의 주석에만 남아 있었다). 면접에서 Zephyr 부팅을 말할 때 `z_prep_c → z_cstart → bg_thread_main → main`으로 말하면 버전에 상관없이 맞다.

**C++ 생성자도 다르다**: newlib의 `__libc_init_array`가 아니라 커널의 `z_static_init_gnu()`가 `__zephyr_init_array_start`부터 돈다. 그리고 그 시점은 **main 스레드 안, POST_KERNEL 뒤**다 — 커널 객체가 이미 동작하는 상태에서 생성자가 돈다. bare-metal의 "생성자는 클럭 · UART보다 먼저"와 반대다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="150" y="22" font-size="13" text-anchor="middle" font-weight="bold">FreeRTOS (랩 템플릿)</text>
<text x="510" y="22" font-size="13" text-anchor="middle" font-weight="bold">Zephyr v4.4.2 (mps2/an385)</text>
<rect x="10" y="36" width="290" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="155" y="57" font-size="12" text-anchor="middle">Reset_Handler (startup.c)</text>
<rect x="10" y="80" width="290" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="155" y="101" font-size="12" text-anchor="middle">.data 복사 · .bss 0 (ucHeap 128 KB 포함)</text>
<line x1="155" y1="70" x2="155" y2="80" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<rect x="10" y="124" width="290" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="155" y="145" font-size="12" text-anchor="middle">board_init → main()</text>
<line x1="155" y1="114" x2="155" y2="124" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<rect x="10" y="168" width="290" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="155" y="189" font-size="12" text-anchor="middle">xTaskCreate (힙에서 TCB · 스택)</text>
<line x1="155" y1="158" x2="155" y2="168" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<rect x="10" y="212" width="290" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="155" y="233" font-size="12" text-anchor="middle">vTaskStartScheduler</text>
<line x1="155" y1="202" x2="155" y2="212" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<rect x="10" y="256" width="290" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="155" y="277" font-size="12" text-anchor="middle">idle · timer 태스크, SysTick</text>
<line x1="155" y1="246" x2="155" y2="256" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<rect x="10" y="300" width="290" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="155" y="321" font-size="12" text-anchor="middle">SVC → 첫 태스크 (C3)</text>
<line x1="155" y1="290" x2="155" y2="300" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<rect x="370" y="36" width="300" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="520" y="57" font-size="12" text-anchor="middle">z_arm_reset (reset.S): MSP, PSP 설정</text>
<rect x="370" y="80" width="300" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="520" y="101" font-size="12" text-anchor="middle">z_prep_c: VTOR · FPU</text>
<line x1="520" y1="70" x2="520" y2="80" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<rect x="370" y="124" width="300" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="520" y="145" font-size="12" text-anchor="middle">arch_bss_zero · arch_data_copy</text>
<line x1="520" y1="114" x2="520" y2="124" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<rect x="370" y="168" width="300" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="520" y="189" font-size="12" text-anchor="middle">z_cstart: EARLY, PRE_KERNEL_1/2</text>
<line x1="520" y1="158" x2="520" y2="168" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<rect x="370" y="212" width="300" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="520" y="233" font-size="12" text-anchor="middle">main 스레드로 전환 (PendSV 아님)</text>
<line x1="520" y1="202" x2="520" y2="212" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<rect x="370" y="256" width="300" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="520" y="277" font-size="12" text-anchor="middle">bg_thread_main: POST_KERNEL, APP</text>
<line x1="520" y1="246" x2="520" y2="256" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<rect x="370" y="300" width="300" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="520" y="321" font-size="12" text-anchor="middle">main() — 스레드 안</text>
<line x1="520" y1="290" x2="520" y2="300" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah6)"/>
<text x="340" y="352" font-size="12" text-anchor="middle">왼쪽: main이 커널을 시작한다.  오른쪽: 커널이 main을 부른다.</text>
</svg>
```

그림 8 — FreeRTOS(랩 템플릿)와 Zephyr v4.4.2의 리셋 → 첫 사용자 코드 경로. 파란 상자가 이 노트의 .data/.bss 단계다.

### 10.3 예제 20 — Zephyr init level 타임라인

무엇을 확인하나: 각 init level에 `SYS_INIT` 훅을 하나씩 걸고 25 MHz FPGAIO 카운터를 찍는다. FreeRTOS와 **같은 가상 보드**(`mps2/an385`)다. `CONFIG_BOOT_BANNER=n`.

```c
/* zboot — timestamp every Zephyr init level with the FPGAIO 25 MHz counter */
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/printk.h>
#define CYC (*(volatile uint32_t *)0x40028018u)
#define NS(c) ((unsigned)(c) * 40u)

static uint32_t t_lvl[5];
static uint32_t g_data_check = 0x1234;      /* .data: copied by arch_data_copy() */
static uint32_t g_bss_check;                /* .bss : zeroed by arch_bss_zero()  */

#define STAMP(idx, lvl) \
    static int stamp_##idx(void) { t_lvl[idx] = CYC; return 0; } \
    SYS_INIT(stamp_##idx, lvl, 0)
STAMP(0, EARLY);
STAMP(1, PRE_KERNEL_1);
STAMP(2, PRE_KERNEL_2);
STAMP(3, POST_KERNEL);
STAMP(4, APPLICATION);

int main(void)
{
    uint32_t t_main = CYC;
    static const char *const name[5] = {"EARLY", "PRE_KERNEL_1", "PRE_KERNEL_2",
                                        "POST_KERNEL", "APPLICATION"};
    for (int i = 0; i < 5; i++) {
        printk("%-13s first hook at %7u ns\n", name[i], NS(t_lvl[i]));
    }
    printk("main()        entered  at %7u ns\n", NS(t_main));
    printk("g_data_check=0x%x g_bss_check=%u\n", g_data_check, g_bss_check);
    return 0;
}
```

```text
# prj.conf
CONFIG_PRINTK=y
CONFIG_BOOT_BANNER=n
```

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/B5/zboot /var/tmp/rtos/B5/zboot && timeout 5 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio -icount shift=5,align=off,sleep=off -kernel /var/tmp/rtos/B5/zboot/zephyr/zephyr.elf 2>&1 | grep -v terminating'
```

```text
           FLASH:       12852 B         4 MB      0.31%
             RAM:        6136 B         4 MB      0.15%
EARLY         first hook at  104160 ns
PRE_KERNEL_1  first hook at  111760 ns
PRE_KERNEL_2  first hook at  135320 ns
POST_KERNEL   first hook at  153320 ns
APPLICATION   first hook at  173000 ns
main()        entered  at  175520 ns
g_data_check=0x1234 g_bss_check=0
```

출력에서 볼 것:

- **EARLY까지 104 µs** — 커널 초기화가 시작되기도 전이다. .bss는 588바이트뿐인데 왜? 예제 21에서 본다.
- EARLY → main 71 µs: PRE_KERNEL_1(UART 드라이버 등) 24 µs, PRE_KERNEL_2 → POST_KERNEL 18 µs(이 사이에 main 스레드로 전환), POST_KERNEL → APPLICATION 20 µs. 이것이 "Zephyr 커널이 main 전에 하는 일"의 비용이다. A7이 잰 Zephyr reset → main 205 µs와 같은 규모다(그쪽은 앱 구성이 달랐다).

### 10.4 예제 21 — gdb로 Zephyr 부팅 경로와 스택 전환 보기

무엇을 확인하나: 표 10.2의 함수에 중단점을 걸고 각 지점의 카운터 · SP · MSP · PSP · CONTROL을 찍는다.

```text
# z.gdb
define stamp
  printf "%-22s cnt=%6u ns  sp=0x%08x msp=0x%08x psp=0x%08x control=%u\n", $arg0, (*(unsigned*)0x40028018)*40, $sp, $msp, $psp, $control
end
break z_prep_c
break arch_bss_zero
break arch_data_copy
break z_cstart
break bg_thread_main
break main
stamp "reset (z_arm_reset)"
continue
stamp "z_prep_c"
...  (각 중단점마다 continue + stamp, 마지막에 bt)
```

```sh
limactl shell elinux -- bash -lc 'QEXTRA="-icount shift=5,align=off,sleep=off" /Users/donh/.rtos_scratch/B5/gdb_reset.sh /var/tmp/rtos/B5/zboot/zephyr/zephyr.elf 3356 /Users/donh/.rtos_scratch/B5/zboot/z.gdb 2>&1 | grep -v "^Breakpoint [0-9] at\|^$"; arm-none-eabi-nm -S /var/tmp/rtos/B5/zboot/zephyr/zephyr.elf | grep -i " z_main_stack\| z_interrupt_stacks\|__bss_start\|__bss_end"'
```

```text
qemu-system-arm: warning: icount sleep disabled and no active timers
z_arm_reset () at /var/rtoslab/zephyrproject/zephyr/arch/arm/core/cortex_m/reset.S:147
147	    ldr r0, =z_main_stack + CONFIG_MAIN_STACK_SIZE
reset (z_arm_reset)    cnt=     0 ns  sp=0x200017f8 msp=0x200017f8 psp=0x00000000 control=0
Breakpoint 1, z_prep_c () at /var/rtoslab/zephyrproject/zephyr/arch/arm/core/cortex_m/prep_c.c:202
202		relocate_vector_table();
z_prep_c               cnt=   440 ns  sp=0x200012f8 msp=0x200017f8 psp=0x200012f8 control=2
Breakpoint 2, arch_bss_zero () at /var/rtoslab/zephyrproject/zephyr/arch/common/init.c:57
57		arch_early_memset(__bss_start, 0, __bss_end - __bss_start);
arch_bss_zero          cnt=   800 ns  sp=0x200012f0 msp=0x200017f8 psp=0x200012f0 control=2
Breakpoint 3, arch_data_copy () at /var/rtoslab/zephyrproject/zephyr/arch/common/xip.c:29
29		arch_early_memcpy(&__data_region_start, &__data_region_load_start,
arch_data_copy         cnt= 76480 ns  sp=0x200012f0 msp=0x200017f8 psp=0x200012f0 control=2
Breakpoint 4, z_cstart () at /var/rtoslab/zephyrproject/zephyr/kernel/init.c:544
544		z_sys_init_run_level(INIT_LEVEL_EARLY);
z_cstart               cnt=103600 ns  sp=0x200012f0 msp=0x200017f8 psp=0x200012f0 control=2
Breakpoint 5, bg_thread_main (unused1=0x0 <sys_heap_aligned_realloc>, unused2=warning: (Internal error: pc 0x0 in read in CU, but not in symtab.)
0x0 <sys_heap_aligned_realloc>, unused3=warning: (Internal error: pc 0x0 in read in CU, but not in symtab.)
0x0 <sys_heap_aligned_realloc>) at /var/rtoslab/zephyrproject/zephyr/kernel/init.c:297
297		z_sys_post_kernel = true;
bg_thread_main         cnt=20137840 ns  sp=0x200017f0 msp=0x200012f8 psp=0x200017f0 control=2
Breakpoint 6, main () at /Users/donh/.rtos_scratch/B5/zboot/src/main.c:23
23	    uint32_t t_main = CYC;
main                   cnt=40139440 ns  sp=0x200017c8 msp=0x200012f8 psp=0x200017c8 control=2
#0  main () at /Users/donh/.rtos_scratch/B5/zboot/src/main.c:23
qemu-system-arm: terminating on signal 15 from pid 1534460 (/bin/bash)
200002f4 B __bss_end
200000a8 B __bss_start
200002f8 00001000 B z_interrupt_stacks
200013f8 00000400 B z_main_stack
```

출력에서 볼 것:

- **리셋 직후 MSP = 0x200017f8** — 벡터 테이블 0번 워드는 `z_main_stack`(0x200013f8, 1 KB)의 꼭대기다. 그런데 `z_prep_c`에 오면 **CONTROL = 2(SPSEL = 1), 코드가 PSP = 0x200012f8로 돈다.** 0x200012f8 = `z_interrupt_stacks`(0x200002f8) + 0x1000, 즉 **초기화 C 코드는 인터럽트 스택을 빌려 쓴다.** reset.S의 주석 그대로다.
- main 스레드 안(`bg_thread_main`, `main`)에서는 역할이 뒤바뀐다: **MSP = 0x200012f8(인터럽트 스택 꼭대기 — 이제 ISR 전용), PSP = 0x200017xx(`z_main_stack` 안 — main 스레드의 스택).** FreeRTOS의 "main 스택이 인터럽트 스택이 된다"(C3)와 같은 결론을 다른 방법으로 낸다.
- 카운터: `arch_bss_zero` 진입 0.8 µs → `arch_data_copy` 76.5 µs. **588바이트를 0으로 채우는 데 75.7 µs**가 걸렸다(바이트당 128 ns = 명령 4개). 예제 22에서 이유를 본다. `arch_data_copy` → `z_cstart` 27 µs.
- `bg_thread_main`과 `main`의 cnt(20 ms, 40 ms)는 **믿으면 안 된다.** icount에서 gdb가 멈춰 있는 동안 타이머가 걸린 뒤로는 가상 시간이 흘러간다(맨 위 QEMU 경고). 그 구간의 진짜 시간은 예제 20(디버거 없음)의 값이다.
- `bg_thread_main`의 인자 표시가 깨진 것은 gdb의 DWARF 해석 경고이고 실행과 무관하다.
- `nm`: `z_interrupt_stacks`와 `z_main_stack`은 `__bss_start..__bss_end`(0x200000a8..0x200002f4) **밖**이다. Zephyr는 스택을 `noinit` 섹션에 둔다(map 파일의 `noinit 0x200002f8 0x1500`). 스택을 0으로 채우는 시간을 아끼는 것이다 — §8의 `.noinit` 원칙을 커널이 기본으로 쓴다.

### 10.5 예제 22 — Zephyr에 128 KB .bss를 넣으면: 크기 최적화 libc의 대가

무엇을 확인하나: 예제 20 앱에 `uint8_t g_log_ring[128 * 1024];`(전역, 마지막 바이트를 써서 살려 둠)만 추가해 다시 잰다. 그리고 원인을 찾아 설정 하나로 고친다.

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/B5/zboot_big /var/tmp/rtos/B5/zboot_big && timeout 5 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio -icount shift=5,align=off,sleep=off -kernel /var/tmp/rtos/B5/zboot_big/zephyr/zephyr.elf 2>&1 | grep -v terminating'
```

```text
           FLASH:       12864 B         4 MB      0.31%
             RAM:      137208 B         4 MB      3.27%
EARLY         first hook at 16881360 ns
PRE_KERNEL_1  first hook at 16888960 ns
PRE_KERNEL_2  first hook at 16912520 ns
POST_KERNEL   first hook at 16930560 ns
APPLICATION   first hook at 16950240 ns
main()        entered  at 16952760 ns
g_data_check=0x1234 g_bss_check=0
```

**16.9 ms** — 같은 128 KB가 bare-metal newlib `memset`(1.3 ms)의 13배다. `memset`을 디스어셈블했다.

```sh
limactl shell elinux -- bash -lc 'arm-none-eabi-objdump -d --no-show-raw-insn /var/tmp/rtos/B5/zboot/zephyr/zephyr.elf | sed -n "/<arch_early_memset>:/,/^$/p;/<memset>:/,/^$/p" | head -12; grep -n "^CONFIG_PICOLIBC=y\|^CONFIG_PICOLIBC_SIZE_OPTIMIZATIONS\|^CONFIG_SIZE_OPTIMIZATIONS\|^CONFIG_XIP" /var/tmp/rtos/B5/zboot/zephyr/.config'
```

```text
00001eec <arch_early_memset>:
    1eec:	b.w	26c6 <memset>

000026c6 <memset>:
    26c6:	mov	r3, r0
    26c8:	add	r2, r0
    26ca:	cmp	r3, r2
    26cc:	bne.n	26d0 <memset+0xa>
    26ce:	bx	lr
    26d0:	strb.w	r1, [r3], #1
    26d4:	b.n	26ca <memset+0x4>

54:CONFIG_XIP=y
730:CONFIG_PICOLIBC=y
745:CONFIG_PICOLIBC_SIZE_OPTIMIZATIONS=y
1140:CONFIG_SIZE_OPTIMIZATIONS=y
```

(`sed`의 두 범위를 한 번에 찍은 출력이다. `.config`의 줄 번호는 파일 그대로.)

Zephyr 기본 libc인 picolibc가 크기 최적화로 빌드되어 `memset`이 **바이트 루프**(`strb`, `cmp`, `bne`, `b` — 바이트당 명령 4개)다. 128 KB × 4 × 32 ns ≈ 16.8 ms, 측정과 맞는다. 예제 21의 "588바이트에 75 µs"도 같은 이유다. 설정을 바꿔 봤다.

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/B5/zboot_big /var/tmp/rtos/B5/zboot_big2 -- -DCONFIG_PICOLIBC_SPEED_OPTIMIZATIONS=y && grep "PICOLIBC_S" /var/tmp/rtos/B5/zboot_big2/zephyr/.config; timeout 5 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio -icount shift=5,align=off,sleep=off -kernel /var/tmp/rtos/B5/zboot_big2/zephyr/zephyr.elf 2>&1 | grep -v terminating'
```

```text
           FLASH:       12864 B         4 MB      0.31%
             RAM:      137208 B         4 MB      3.27%
CONFIG_PICOLIBC_SUPPORTED=y
# CONFIG_PICOLIBC_SIZE_OPTIMIZATIONS is not set
CONFIG_PICOLIBC_SPEED_OPTIMIZATIONS=y
EARLY         first hook at 16881360 ns
PRE_KERNEL_1  first hook at 16888960 ns
PRE_KERNEL_2  first hook at 16912520 ns
POST_KERNEL   first hook at 16930560 ns
APPLICATION   first hook at 16950240 ns
main()        entered  at 16952760 ns
```

picolibc 옵션만 바꿔서는 **아무것도 안 바뀌었다**(FLASH 크기까지 같다). 전체 최적화 수준도 바꿨다.

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/B5/zboot_big /var/tmp/rtos/B5/zboot_big3 -- -DCONFIG_PICOLIBC_SPEED_OPTIMIZATIONS=y -DCONFIG_SPEED_OPTIMIZATIONS=y && timeout 5 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio -icount shift=5,align=off,sleep=off -kernel /var/tmp/rtos/B5/zboot_big3/zephyr/zephyr.elf 2>&1 | grep -v terminating; arm-none-eabi-objdump -d --no-show-raw-insn /var/tmp/rtos/B5/zboot_big3/zephyr/zephyr.elf | sed -n "/<memset>:/,/^$/p" | head -8'
```

```text
           FLASH:       16192 B         4 MB      0.39%
             RAM:      137208 B         4 MB      3.27%
EARLY         first hook at 1328960 ns
PRE_KERNEL_1  first hook at 1335560 ns
PRE_KERNEL_2  first hook at 1357480 ns
POST_KERNEL   first hook at 1374080 ns
APPLICATION   first hook at 1381600 ns
main()        entered  at 1383600 ns
g_data_check=0x1234 g_bss_check=0
000025d4 <memset>:
    25d4:	lsls	r3, r0, #30
    25d6:	push	{r4, r5, lr}
    25d8:	beq.n	2668 <memset+0x94>
    25da:	mov	ip, r0
    25dc:	adds	r4, r0, r2
    25de:	b.n	25ec <memset+0x18>
    25e0:	mov	ip, r3
```

출력에서 볼 것:

- `CONFIG_SPEED_OPTIMIZATIONS=y`(전체 `-O2`)에서 `memset`이 워드 정렬 + 언롤 버전으로 바뀌었고 **16.95 ms → 1.38 ms(12배)**. 대가는 FLASH +3.3 KB(12864 → 16192)다.
- `CONFIG_PICOLIBC_SPEED_OPTIMIZATIONS`만으로는 변화가 없었다. 이 빌드(gnuarmemb 툴체인 + picolibc 모듈)에서 picolibc 소스의 `memset` 선택은 전체 최적화 수준(`-Os` 여부)을 따르는 것으로 보인다 — 원인을 소스 수준까지는 확인하지 못했다.
- 교훈: **같은 "128 KB .bss"가 libc · 최적화 설정에 따라 1.3 ms도 되고 17 ms도 된다.** 부팅 시간이 이상하면 `arch_early_memset`/`memset`을 디스어셈블해 본다. 큰 버퍼는 Zephyr에서도 `__noinit`(`include/zephyr/linker/section_tags.h`) 매크로로 0 채우기 대상에서 빼는 것이 정석이다. 아키텍처가 `arch_early_memset`을 덮어써서 빠른 구현을 줄 수도 있다(weak 함수다).

---

## 11. 실무에서 다시 보기

### 11.1 드론 비행 제어기 — "리셋에서 모터 재개까지"

STM32H7급 FC를 예로 들자. 비행 중 브라운아웃 리셋이 나면 다음이 순서대로 돌아야 다시 자세 제어가 된다: 부트로더(서명 검사) → 앱 `Reset_Handler` → SystemInit(PLL, 수백 µs~ms) → .data/.bss → RTOS 시작 → IMU 초기화 → 필터 수렴. 이 노트에서 손댈 수 있는 것은:

- **큰 버퍼를 `.noinit`으로**: 블랙박스 로그 링버퍼(수십~수백 KB), DMA 버퍼, RTOS heap. 0 채우기 대상에서 빼면 ms 단위가 줄어든다(예제 18: 25배).
- **로그 링버퍼를 `.noinit`에 두면 덤으로** 리셋 직전 로그가 살아남는다 — 리셋 원인 분석에 쓴다(예제 9의 매직 넘버 패턴, F8).
- **DTCM에 스택 · 제어 루프 데이터, ITCM에 rate loop ISR**: 링커 스크립트에 영역을 추가하고(예제 6), ITCM 코드는 `.ramfunc`처럼 `AT > FLASH`로 복사한다(예제 19). H7에서 DTCM은 DMA가 못 닿는 것에 주의(B8).
- 워치독이 하드웨어로 켜져 있으면 부팅 중 큰 memset이 워치독을 넘지 않는지 계산한다(§5.1).

### 11.2 로봇 관절 모터 컨트롤러 — FOC ISR을 RAM에서

20 kHz FOC 전류 루프는 50 µs 안에 ADC 읽기 → Clarke/Park 변환 → PI → SVPWM을 끝내야 한다. 플래시 wait state가 캐시 미스와 겹치면 ISR 실행 시간이 흔들린다. 그래서:

```c
/* FOC ISR placed in RAM: copied by Reset_Handler together with .data (b5.ld: *(.ramfunc*)) */
#define RAMFUNC __attribute__((section(".ramfunc"), long_call, noinline))
RAMFUNC void ADC1_IRQHandler(void)
{
    /* read phase currents, Clarke/Park, PI, inverse Park, SVPWM duty update */
}
```

(빌드만 고려한 스케치 — 이 노트에서 실행하지 않았다. 핸들러를 RAM에 둘 때 벡터 테이블에는 RAM 주소가 들어가고, 그 주소는 링크 시점에 정해지므로 FLASH 벡터 테이블에 그대로 쓸 수 있다.) 플래시를 지우는 동안 이 ISR이 돌아야 한다면(현장 펌웨어 업데이트 중에도 모터를 잡고 있어야 하는 경우) RAM 배치는 선택이 아니라 필수다. 그 ISR이 부르는 함수(예: `arm_sin_cos_f32`)까지 전부 RAM에 있어야 한다는 것이 함정이다 — map 파일로 확인한다.

### 11.3 센서 허브 MCU (Zephyr) — 부트로더 뒤에서 시작하기

Linux SoC 옆의 센서 허브는 보통 MCUboot 같은 부트로더 뒤에서 돈다. 그러면:

- 앱의 링커 스크립트 FLASH ORIGIN이 0x0이 아니라 슬롯 주소(예: 0x10000)가 된다. Zephyr에서는 DT의 `zephyr,code-partition`이 이걸 정한다(H 모듈).
- 부트로더가 앱으로 넘어갈 때 VTOR를 앱 벡터 테이블로 바꾸고 MSP를 앱의 0번 워드로 넣은 뒤 1번 워드로 점프한다. 앱의 `z_arm_reset`이 MSP를 다시 설정하는 이유가 이것이다(§4.5).
- 부트로더가 켜 둔 주변장치 · 인터럽트가 남아 있을 수 있다. Zephyr의 `CONFIG_INIT_ARCH_HW_AT_BOOT`가 코어 레지스터와 NVIC를 리셋 상태로 되돌리는 옵션이다(reset.S에서 본 `z_arm_init_arch_hw_at_boot`).

### 11.4 Don의 SSD 컨트롤러 — 같은 문제, 큰 스케일

SSD 컨트롤러는 부트 ROM이 NAND나 SPI NOR에서 펌웨어를 SRAM/TCM으로 **통째로 복사한 뒤** 점프하는 구조가 흔하다. 그러면 "XIP 플래시 + .data만 복사"가 아니라 **전체가 RAM 실행**이고, LMA는 NAND 안의 이미지 오프셋, VMA는 SRAM이다. 링커 스크립트의 `AT>` 개념이 그대로 적용되고, 복사 주체가 `Reset_Handler`가 아니라 부트 ROM일 뿐이다. 또 ECC SRAM은 한 번도 쓰지 않은 주소를 읽으면 ECC 에러가 나므로, 부팅 초기에 **사용 전 전체 SRAM을 한 번 써 주는** 루프가 있었을 것이다 — 이 노트의 .bss 0 채우기를 "모든 RAM"으로 넓힌 것이고, §8의 측정이 말하듯 크기에 비례하는 부팅 비용이다(F8에서 ECC RAM 초기화를 따로 다룬다).

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `KEEP(*(.isr_vector))` 누락 | 리셋 직후 Lockup, 출력 없음. "다른 앱에서는 됐는데" | `--gc-sections`가 참조 없는 벡터 테이블을 버림. `used` 속성은 링커 gc를 못 막음 | 벡터 · `.init_array` · `.preinit_array`에 KEEP. map 파일 Discarded 목록 확인 (예제 7) |
| `.data`에 `AT > FLASH` 누락 | 디버거 · QEMU에서는 되고 플래시 단독 부팅에서 전역 초기값이 0, libc가 HardFault. .bin이 512 MB | LMA가 RAM이라 플래시 이미지에 초기값이 없음 | `> RAM AT > FLASH` + `_sidata = LOADADDR(.data)`. .bin 크기를 CI에서 검사 (예제 8) |
| .bss 0 채우기 누락 · 범위 오류 | 콜드 부팅은 되고 워치독 · 소프트 리셋 뒤에 카운터 · 플래그가 이상 | QEMU · 일부 칩 RAM은 전원 직후 우연히 0 | startup이 `_sbss.._ebss` 전체를 0으로. 경계 심볼을 ALIGN(4)로 (예제 9) |
| 스택 · heap 크기 검사 없음 | 깊은 호출에서 heap 데이터가 조용히 바뀜, fault 없음 | 둘 사이에 하드웨어 벽 없음 | 링커 ASSERT, stack painting으로 high-water 측정, MPU 가드 · v8-M MSPLIM (예제 10, 11) |
| 커스텀 섹션 이름을 `.data2`처럼 표준 이름으로 시작 | 객체가 엉뚱한 영역(RAM1)에 놓임, 경고 없음 | `*(.data*)` 와일드카드가 먼저 매치 | 이름을 `.ram2_data`처럼 짓기, map 파일로 주소 확인 (예제 6) |
| `-nostartfiles`에서 `__libc_init_array` 호출 | `undefined reference to _init`, `__dso_handle` 링크 에러 | crti/crtn/crtbegin을 뺐음 | 빈 `_init`/`_fini`, `__dso_handle` 정의 또는 crt 파일 사용 (예제 14) |
| 생성자를 부르지 않음 · `.init_array` 없는 링커 스크립트 | C++ 전역 객체가 0인 채로 시작, vtable 호출에서 주소 0 점프 | 표가 gc로 버려지거나 아무도 안 돎 | 링커에 `.init_array` + KEEP, startup에서 `__libc_init_array()` (예제 14) |
| FPU 칩에서 CPACR 설정 누락 | 첫 float 연산에서 HardFault, CFSR NOCP | FPU는 리셋 때 꺼져 있음 | `Reset_Handler` 첫머리에서 CPACR CP10/CP11 = 0b11 + DSB/ISB (예제 13) |
| SystemInit에서 전역 변수 설정 | `SystemCoreClock`이 기본값으로 돌아가 UART 보율 · 타이머가 틀림 | 뒤이은 .data 복사가 덮어씀 | SystemInit은 레지스터만, 변수는 main에서 `SystemCoreClockUpdate()` (§5.1) |
| startup 루프가 `memset`으로 치환된 줄 모름 | `-nostdlib`에서 링크 에러, 또는 RAM에 둔 memset을 복사 전에 호출 | `-ftree-loop-distribute-patterns` | 함수 attribute로 끄거나 어셈블리 (예제 12) |
| main이 return할 곳이 없음 | PC = 0xFFFFFFFE IACCVIOL, 또는 쓰레기 주소 Lockup | `b main`, 리셋 벡터 = main | `bl main` 뒤에 무한 루프 · 리셋 · BKPT (예제 16) |

---

## 13. 면접에서 이렇게 말한다

**Q.** "Walk me through what happens on a Cortex-M from power-on reset to the first line of main."

**A.** 하드웨어는 벡터 테이블 두 워드만 읽는다: 0번은 MSP, 1번은 PC(bit 0은 Thumb). Thread · privileged · MSP 상태로 `Reset_Handler`가 시작된다. 그다음은 전부 소프트웨어다: 클럭 설정(SystemInit), FPU 켜기, .data를 플래시에서 RAM으로 복사, .bss 0, 생성자 실행, main 호출. 주소는 링커 스크립트 심볼로 안다. 시간은 대부분 복사와 0 채우기이고 크기에 비례한다.

> On reset the core reads just two words from the vector table: word 0 goes into MSP and word 1 into the PC, with bit 0 setting the Thumb state. It starts Reset_Handler in privileged Thread mode on the main stack. Everything after that is software: SystemInit sets up clocks and flash wait states, the FPU is enabled through CPACR if present, .data is copied from its load address in flash to RAM, .bss is zeroed, static constructors run through __libc_init_array, and then main is called. The startup code finds those regions through linker-script symbols. In practice the boot time is dominated by the copy and zero loops, which scale linearly with size — I measured about 10 µs per KB of .bss on a 25 MHz M3 model.

**꼬리 질문**: "Why can Cortex-M startup be written in C but Cortex-R or RISC-V cannot?" → 하드웨어가 SP를 넣어 주는 건 M 프로파일뿐이다.

**Q.** "What is the difference between VMA and LMA, and why does .data need both?"

**A.** VMA는 실행할 때의 주소, LMA는 이미지에 저장된 주소다. .data의 초기값은 전원이 꺼져도 남아야 하니 플래시(LMA)에 있고, 변수는 써야 하니 RAM(VMA)에 있다. 링커 스크립트의 `> RAM AT > FLASH`가 둘을 정하고 startup이 복사한다.

> VMA is where a section lives when the program runs; LMA is where it is stored in the image. Initialized data has to survive power-off, so its initial values sit in flash, but the program writes those variables, so they must live in RAM. In the linker script I write `> RAM AT > FLASH` and export LOADADDR(.data), and Reset_Handler copies from the LMA to the VMA. If you forget the AT part, it still works under a debugger because the debugger loads RAM directly, but it fails when booting from flash — and objcopy produces a 512-megabyte .bin, which is a nice tell.

**꼬리 질문**: "What does `readelf -l` show for that?" → 두 번째 LOAD 세그먼트의 PhysAddr(LMA)가 플래시, VirtAddr가 RAM, FileSiz < MemSiz(.bss).

**Q.** "Our firmware works when flashed with the debugger attached but fails after a power cycle. Where do you look?"

**A.** 디버거가 대신 해 주던 일이 없어서다. 첫째, 디버거 load가 RAM을 직접 채워 숨긴 .data LMA 문제. 둘째, .bss를 0으로 안 채워서 RAM 잔여값에 의존. 셋째, 디버거가 클럭 · 워치독 · 저전력 디버그 설정을 바꿔 둔 것. 넷째, 부팅 시간이 길어 하드웨어 워치독에 걸림. 플래시만 담은 .bin으로 QEMU나 보드에서 부팅해 재현한다.

> I look for things the debugger was silently doing for us. The classic one is .data placed without a flash load address: the debugger writes RAM directly, so initial values look fine, but a flash-only boot sees zeros. Second, .bss not being zeroed, so the code depends on whatever is in RAM. Third, the debugger keeping clocks running in low-power modes or halting the watchdog. And fourth, a slow startup — a big zeroed buffer can take milliseconds and trip an option-byte watchdog before main. I reproduce by booting the raw flash image without the debugger and checking the map file and the .bin size.

**Q.** "How do C++ global constructors get called on bare metal?"

**A.** 컴파일러가 생성자 함수 포인터를 `.init_array`에 넣는다. 링커 스크립트가 그 섹션을 KEEP하고 시작 · 끝 심볼을 만들어야 하고, startup이 `__libc_init_array()`로 순회해야 한다. `-nostartfiles`면 `_init`과 `__dso_handle`이 없어 링크 에러가 나므로 스텁을 준다. Zephyr는 커널이 POST_KERNEL 뒤에 돌린다.

> The compiler emits a pointer to each constructor function into the .init_array section. Two things must be in place: the linker script must KEEP that section and define __init_array_start and end, and the startup code must walk it, usually by calling newlib's __libc_init_array after .data and .bss are ready. With -nostartfiles you also lose _init from crti and __dso_handle from crtbegin, so you either provide stubs or link the crt files. Without the KEEP, gc-sections drops the table and the constructors silently never run. I also avoid relying on cross-file construction order and keep hardware init out of constructors.

**Q.** "What happens if main returns on a microcontroller?"

**A.** startup이 main 뒤에 둔 코드가 정한다. newlib `exit`면 atexit · 소멸자 뒤 `_exit`, 벤더 startup은 무한 루프가 흔하다. 리셋 벡터가 main을 직접 가리키면 LR = 0xFFFFFFFF로 return해서 HardFault(IACCVIOL)가 난다. FreeRTOS는 스케줄러를 시작한 main이 돌아오지 않고, Zephyr는 main이 스레드라 그 스레드만 끝난다.

> It depends entirely on what the startup code put after the call. With newlib's exit you run atexit handlers and static destructors and end in _exit, which on a board is usually a loop or a reset. If main was entered without a proper call — say the reset vector points straight at it — it returns to the reset value of LR, 0xFFFFFFFF, and you get a HardFault on the instruction fetch; I reproduced that on QEMU with IACCVIOL. In FreeRTOS main starts the scheduler and never returns, and in Zephyr main is just the main thread, so returning ends that thread while the rest of the system keeps running.

**Q.** "Boot time is too long. How would you reduce it?"

**A.** 먼저 잰다(리셋부터 카운터, 단계별 타임스탬프). 대부분 .bss · .data 크기이므로 큰 버퍼(heap, 로그, DMA)를 `.noinit`으로 빼고, 0 채우기 루프를 `stm`이나 좋은 memset으로, 클럭을 먼저 올린다. RTOS 설정(Zephyr의 크기 최적화 libc 같은 것)도 확인한다. 나머지는 드라이버 초기화를 지연시키는 것.

> First I measure, with a free-running counter from reset and timestamps per stage. Most of the pre-main time is proportional to .bss and .data size, so the big wins are moving large buffers that don't need to be zero — RTOS heap, log rings, DMA buffers — into a NOLOAD noinit section, making sure the zero loop uses multi-word stores, and raising the clock before the loops. Moving FreeRTOS's heap to noinit cut reset-to-main from 1.37 ms to 55 µs in my measurement. I also check the library: a size-optimized Zephyr build used a byte-wise memset that made 128 KB of .bss cost about 17 ms instead of 1.4 ms. After that it's deferring driver init until it's actually needed.

**Q.** "Why and how would you run code from RAM on a flash-based MCU?"

**A.** 이유는 셋: 플래시를 지우거나 쓰는 동안 실행해야 하는 코드, wait state로 인한 지터를 없애야 하는 빠른 ISR, 더 빠른 실행. 방법은 `.data`와 같다: `.ramfunc` 섹션을 `> RAM AT > FLASH`로 두고 startup이 복사. 512 MB 떨어진 호출은 링커가 veneer를 만들거나 `long_call`을 쓴다.

> Three reasons: code that must keep running while flash is being erased or programmed, tight ISRs where flash wait states add jitter, and raw speed on parts without a good flash cache. Mechanically it's the same trick as .data — put the function in its own section, give it a RAM VMA and a flash LMA, and let the startup copy it. Because RAM is hundreds of megabytes away from flash, a plain BL can't reach it, so either the linker inserts a long-branch veneer, which I saw it do automatically, or you mark the function long_call. And you have to make sure everything that function calls is also in RAM.

**Q.** "What does Zephyr do before main that FreeRTOS doesn't?"

**A.** FreeRTOS는 startup을 안 가진다 — main이 커널을 시작한다. Zephyr는 `z_arm_reset`에서 스택을 잡고, `z_prep_c`에서 VTOR · FPU · `arch_bss_zero` · `arch_data_copy`, `z_cstart`에서 init level EARLY · PRE_KERNEL_1/2 드라이버 초기화, 그다음 main 스레드로 전환해 POST_KERNEL · APPLICATION을 돌리고 main을 부른다.

> FreeRTOS owns nothing before main; the vendor startup copies data and zeroes bss, and the kernel only runs once main calls xTaskCreate and vTaskStartScheduler. Zephyr owns the whole path: z_arm_reset sets up the stacks, z_prep_c relocates the vector table, enables the FPU, zeroes .bss and copies .data, then z_cstart runs the EARLY and PRE_KERNEL init levels, switches into the main thread directly without going through PendSV, and bg_thread_main runs POST_KERNEL and APPLICATION before finally calling main. So in Zephyr, main runs as a thread with drivers already initialized.

---

## 14. 직접 해보기

1. **개념** — `ENTRY(Reset_Handler)`를 `ENTRY(main)`으로 바꾸면 Cortex-M의 부팅이 달라지나? 무엇이 달라지나? 정답/힌트: 코어는 바뀌지 않는다(벡터 1번 워드를 본다). ELF의 e_entry가 바뀌어 디버거 `load` 뒤 PC가 main이 되고, `--gc-sections`의 뿌리가 바뀐다 — 다른 무엇도 Reset_Handler를 참조하지 않으면 벡터 테이블 KEEP 덕분에 살아남는지 확인해 볼 것.
2. **코드** — `b5.ld`에서 `.ramfunc`를 `.data`에서 떼어 별도 출력 섹션 `.ramfunc : { ... } > RAM AT > FLASH`로 만들고, `startup_b5.c`에 복사 루프를 하나 더 넣거나 §2.7의 copy table로 바꿔라. `size`가 `.data`를 다시 data 열에 찍는지 확인. 정답/힌트: `_sramfunc`, `_eramfunc`, `_siramfunc = LOADADDR(.ramfunc)` 세 심볼. objdump -h에서 `.data`의 CODE 플래그가 사라진다.
3. **측정** — 예제 17의 결과로 `.data` 복사의 KB당 시간을 세 방식별로 계산하고, 왜 복사가 0 채우기보다 느린지 명령 단위로 설명하라. 정답/힌트: 18.4 / 32.8 / 10.2 µs/KB. 복사는 워드마다 load가 하나 더 있다(C 루프: ldr+str+cmp+bcc = 4 명령).
4. **코드 · 측정** — stack painting: `Reset_Handler`에서 `_sstack.._estack - 64`를 0xDEADBEEF로 칠하고, main 끝에서 아직 칠해진 워드를 세어 MSP 최대 사용량을 출력하라. 예제 10의 `recurse()` 깊이별로 돌려 보라. 정답/힌트: 칠하기는 자기 자신이 쓰는 스택(현재 SP 위쪽)을 피해야 한다 — SP보다 아래만 칠한다. FreeRTOS의 `uxTaskGetStackHighWaterMark()`가 같은 원리다(태스크 생성 때 0xA5로 칠한다).
5. **버그 찾기** — 동료가 `SystemInit()`에서 `SystemCoreClock = 168000000;`을 쓰고, startup은 SystemInit → .data 복사 → main 순서다. main에서 `SystemCoreClock`을 읽으면? `.bss`라면? 정답/힌트: `.data`면 플래시의 초기값(보통 16000000)으로 덮이고, `.bss`(초기값 없음)면 0이 된다. 둘 다 UART 보율 계산이 틀린다.
6. **Zephyr** — `CONFIG_SKIP_BSS_CLEAR`는 언제 켜나? `kernel/Kconfig`의 도움말을 읽고, 켰을 때 누가 .bss를 0으로 만들어야 하는지 답하라. 정답/힌트: 부트 ROM · 이전 단계 로더나 하드웨어(ECC 메모리 초기화)가 .bss를 이미 0으로 만들어 주는 플랫폼. 그 책임은 그 외부 주체에게 넘어간다(도움말 원문 "the responsibility for .bss zeroing in all possible scenarios ...").
7. **포팅** — `mps2-an505`(Cortex-M33)으로 `startup_b5.c`를 옮겨라. 메모리 맵(QEMU an505의 코드 · SRAM 주소)을 확인해 `MEMORY`를 고치고, `-mcpu=cortex-m33`로 빌드한 뒤 `Reset_Handler` 첫머리에서 `msr msplim, <_sstack>`를 설정해 예제 10의 스택 충돌이 이제 fault(STKOF)로 잡히는지 보라. 정답/힌트: v8-M Mainline은 MSPLIM 아래로 SP가 내려가면 UsageFault STKOF. Secure 상태에서 시작한다는 점(TrustZone)도 확인할 것 — B7, K 모듈.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| 벡터 테이블 | 예외 핸들러 주소 배열 | 0번 워드만 예외가 아니라 초기 MSP (B2) |
| MSP / PSP | Main / Process Stack Pointer | 리셋 땐 MSP. RTOS는 태스크에 PSP를 쓴다 (B1, C3) |
| Thumb 비트 | 분기 주소의 bit 0 | Cortex-M에서는 항상 1이어야 한다. 0이면 INVSTATE |
| Reset_Handler | 리셋 벡터가 가리키는 함수 | C 런타임을 세우고 main을 부른다 |
| VMA | Virtual Memory Address | 실행할 때 섹션이 있는 주소 |
| LMA | Load Memory Address | 이미지에 저장 · 적재되는 주소. ELF의 PhysAddr |
| `AT > FLASH` | LMA 지정 | VMA와 다른 적재 위치를 준다 |
| `LOADADDR()` · `ADDR()` · `SIZEOF()` | 링커 내장 함수 | 섹션의 LMA · VMA · 크기 |
| KEEP | gc 금지 | 참조 없는 섹션을 `--gc-sections`에서 지킨다 |
| NOLOAD | 내용 없는 섹션 | 주소만 받고 파일 · 플래시에 바이트가 없다 |
| PROVIDE | 기본값 심볼 | 다른 곳에서 정의하면 그쪽이 이긴다 (`--defsym`) |
| ASSERT | 링크 시점 검사 | 거짓이면 메시지와 함께 링크 실패 |
| `.data` | 초기값 있는 전역 | 플래시에서 RAM으로 복사 |
| `.bss` | 초기값 없는 전역 | startup이 0으로 채운다 |
| `.noinit` | 초기화 안 하는 RAM | 리셋을 넘어 살아남음, 0 채우기 시간 절약 |
| `.init_array` | 생성자 포인터 표 | `__libc_init_array`가 순회 |
| `__libc_init_array` | newlib 생성자 실행기 | preinit_array → `_init` → init_array |
| `__dso_handle` | C++ 소멸자 등록용 핸들 | 원래 crtbegin.o가 정의 |
| `-nostartfiles` | `crt*.o` 빼기 | startup을 직접 쓸 때 쓰는 링커 옵션 |
| SystemInit | CMSIS 초기 클럭 설정 함수 | main 전에 PLL · wait state를 맞춘다 |
| wait state | 플래시 접근 대기 사이클 | 코어 클럭을 올리기 전에 먼저 올린다 |
| CPACR | Coprocessor Access Control Register | 0xE000ED88. CP10/CP11로 FPU를 켠다 |
| XIP | eXecute In Place | 플래시에서 바로 실행 |
| `.ramfunc` | RAM에서 실행할 코드 섹션 | LMA는 플래시, startup이 복사 |
| veneer | 링커가 끼우는 점프 조각 | 도달 거리 밖 호출을 대신 이어 준다 |
| copy table | 복사 · 0 채우기 영역 표 | 영역이 여러 개일 때 startup이 순회 |
| init level | Zephyr 초기화 단계 | EARLY · PRE_KERNEL_1/2 · POST_KERNEL · APPLICATION (· SMP) |
| `z_prep_c` / `z_cstart` | Zephyr 부팅 함수 | C 환경 준비 / 커널 초기화 |
| icount | QEMU 명령 수 기반 가상 시계 | `ICOUNT=1`이면 명령 1개 = 32 ns |

---

## 16. 요약 & 체크리스트

Cortex-M의 리셋에서 하드웨어가 하는 일은 벡터 두 워드(MSP, PC+Thumb)를 읽는 것뿐이고, 나머지는 `Reset_Handler`가 링커 스크립트 심볼을 보고 한다: SystemInit → FPU → .data를 LMA(플래시)에서 VMA(RAM)로 복사 → .bss 0 → 생성자(`.init_array`) → main → (return하면) startup이 정한 곳. 링커 스크립트 버그(KEEP · `AT>` · 와일드카드 · 스택/heap 경계)는 에러 없이 링크되고, QEMU나 디버거가 가려 주며, 실칩 · 웜 리셋 · 깊은 호출에서야 드러난다 — map 파일 · `objdump -h` · 플래시 단독 부팅으로 잡는다. main 전 시간은 .data/.bss 크기에 정비례(이 모델에서 0 채우기 약 8~25 µs/KB)하고, 가장 큰 개선은 0일 필요 없는 큰 버퍼를 `.noinit`으로 빼는 것(FreeRTOS 25배)과 libc · 최적화 설정(Zephyr 12배)에서 나왔다. FreeRTOS는 main이 커널을 시작하고, Zephyr는 `z_prep_c → z_cstart → bg_thread_main`을 거쳐 main을 스레드로 부른다.

- [ ] 리셋 직후 MSP · PC · xPSR.T · LR · CONTROL 값을 말하고 gdb로 확인할 수 있다
- [ ] Cortex-M과 Cortex-R/RISC-V의 리셋 차이(SP를 누가 넣나)를 설명할 수 있다
- [ ] 링커 스크립트의 MEMORY · SECTIONS · `> RAM AT > FLASH` · LOADADDR · KEEP · NOLOAD · PROVIDE · ASSERT를 직접 쓸 수 있다
- [ ] `objdump -h`의 VMA/LMA, `readelf -l`의 PhysAddr · FileSiz/MemSiz, map 파일의 Discarded 목록을 읽을 수 있다
- [ ] KEEP · `AT>` · .bss 0 · 스택/heap 버그의 증상을 각각 말하고 재현할 수 있다
- [ ] RAM 영역이 여러 개일 때 copy table로 startup을 일반화할 수 있다
- [ ] `Reset_Handler`를 C와 어셈블리로 쓰고, 컴파일러의 memset 치환을 알아본다
- [ ] SystemInit의 순서 함정(전역 변수, wait state, 워치독)을 설명할 수 있다
- [ ] FPU 칩에서 CPACR를 언제 · 왜 켜는지, 안 켜면 무슨 fault인지 안다
- [ ] 생성자가 `.init_array` → `__libc_init_array`로 도는 경로와 `-nostartfiles` 함정을 안다
- [ ] main이 return했을 때 런타임별로 무슨 일이 생기는지 말할 수 있다
- [ ] 부팅 시간을 단계별로 재고 `.noinit` · 복사 방식 · 설정으로 줄일 수 있다

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM F3 | 링커 스크립트 심화 (MEMORY/SECTIONS, VMA vs LMA `AT>`, KEEP, ALIGN, PROVIDE, 여러 RAM 영역, C에서 링커 심볼) | §2 전체, §3 (버그 4종), §9 (`.ramfunc`) | 벤더 링커 스크립트(STM32CubeIDE · MCUXpresso · nRF Connect) 나란히 읽기, 이 노트 링크 |
| BM F4 | startup 코드 직접 쓰기 (C 배열 벡터 테이블, Reset_Handler, .data/.bss, SystemInit, `__libc_init_array` · C++ 생성자, main 복귀) | §1, §4, §5, §6, §7 | 벤더 startup(`startup_stm32xxx.s`, CMSIS `startup_<device>.c`) 한 줄씩 비교, 이 노트 링크 |
| BM F5 (일부) | C 런타임 · libc · syscall stub | §2.4 (libc의 .data/.bss), §6 (`_init` · `__dso_handle`) | 자세한 내용은 RTOS B11 |
| BM B5 (일부) | 링크 · 배치 기본 | §2.1 | 레지스터 주소 상수 쪽 이야기만 |

---

## 참고 자료

- Arm, Armv7-M Architecture Reference Manual (ARM DDI 0403) — 리셋 동작(벡터 테이블에서 SP_main · PC 적재, LR 초기값), 예외 모델, CPACR.
- Arm, Cortex-M3 Technical Reference Manual — 명령 타이밍 표(LDM/STM 1+N, 분기 1+P), 메모리 맵.
- Arm, Cortex-M4 Devices Generic User Guide — CPACR(0xE000ED88)과 FPU 활성화 순서, CFSR 비트(NOCP · INVSTATE · IACCVIOL).
- GNU ld 매뉴얼, "Linker Scripts" 장 — `MEMORY`, `SECTIONS`, Output Section LMA(`AT>`), `KEEP`, `PROVIDE`, `ASSERT`, `LOADADDR`: https://sourceware.org/binutils/docs/ld/Scripts.html
- newlib 소스 `newlib/libc/misc/init.c` — `__libc_init_array`의 호출 순서(preinit → `_init` → init_array).
- FreeRTOS Kernel V11.3.2 소스: `portable/MemMang/heap_4.c`(`configAPPLICATION_ALLOCATED_HEAP`, `pvPortCalloc`), `tasks.c`(`vTaskStartScheduler`), `portable/GCC/ARM_CM3/port.c`(`xPortStartScheduler`, `prvPortStartFirstTask`).
- FreeRTOS 공식 데모 `FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc/` (`startup_gcc.c`, `mps2_m3.ld`) — 랩 `/var/rtoslab/src/FreeRTOS-demo`에 있는 사본으로 확인.
- Zephyr v4.4.2 소스: `arch/arm/core/cortex_m/reset.S`(`z_arm_reset`), `arch/arm/core/cortex_m/prep_c.c`(`z_prep_c`), `arch/common/init.c`(`arch_bss_zero`, `arch_early_memset`), `arch/common/xip.c`(`arch_data_copy`), `kernel/init.c`(`z_cstart`, `bg_thread_main`, `z_static_init_gnu`, init level 열거형), `arch/arm/core/cortex_m/thread.c`(`arch_switch_to_main_thread`), `kernel/Kconfig`(`INIT_STACKS`, `SKIP_BSS_CLEAR`, `STATIC_INIT_GNU`), `include/zephyr/toolchain/gcc.h`(`__ramfunc`).
- Zephyr 문서, "Initialization Levels"(SYS_INIT, 디바이스 초기화 단계): https://docs.zephyrproject.org/latest/kernel/drivers/index.html
- ST, RM0090 (STM32F405/407 Reference Manual) — flash 접근 지연(wait state) 대 HCLK 표.
- 이 노트와 이어지는 노트: B1, B2(벡터 테이블 · VTOR · EXC_RETURN), B6(FPU 컨텍스트), B8(캐시 · TCM), B10(RISC-V), B11(libc · map 파일), C3(첫 태스크 시작), F8(메모리 배치 실무), A7(부팅 시간 측정 원본), EL B3(ELF 섹션 · 세그먼트).
