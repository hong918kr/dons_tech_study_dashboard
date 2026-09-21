# J10. Experience with ARM Cortex-M or Cortex-A processors and associated toolchains

> **분류**: Requirement 3/7 · **관련 개념 노트**: C01, C02, C10, S02
> **Don 현재 상태**: ✅ 강함 (Cortex-M 쪽) / 🟡 부분 (Cortex-A 쪽) — 레쥬메에 "ARM Cortex R8/R82/M0+ … FW bring-up", Trace32, bare-metal C/C++가 있으나 Cortex-A/Linux BSP 문장은 없다.
> **이 노트를 다 읽으면**: ① 면접관이 "IDE만 써 본 사람"과 "코어를 아는 사람"을 가르는 지점이 어디인지 안다 ② arm-none-eabi 툴체인에서 링커 스크립트·map 파일·objdump·addr2line을 근거로 말할 수 있다 ③ Cortex-A 갭을 과장 없이 답하는 문장을 갖는다.

---

## 0. 문장 뜯어보기

| 구(句) | 표면적 의미 | 채용담당자가 이 단어를 고른 이유 |
|---|---|---|
| Experience with | "써 봤다" — 수식어가 없다(Strong, Expert 아님) | 필수 통과선은 낮게 잡았다. 그러나 면접에서는 깊이로 레벨을 정한다. 이 문장은 **필터가 아니라 레벨 판정용**이다 |
| ARM Cortex-M | MCU 계열. ARMv6-M/v7-M/v8-M | Hark 기기의 always-on MCU(Ambiq Apollo 계열 추정)를 가리킨다 |
| **or** Cortex-A | 둘 중 하나면 된다 | Qualcomm SoC(Android) 쪽 BSP를 만질 사람도 뽑고 싶다. **or**이므로 M만 깊어도 요건은 충족된다 |
| Cortex-A | Application 프로세서. MMU, 다중 exception level, OS 구동 | System Test 공고의 "Qualcomm chipsets, RTOS/Linux/Android"와 같은 축 [추정] |
| associated toolchains | 컴파일러/링커/디버거/빌드 시스템 | 여기가 진짜 필터다. "Keil에서 Build 버튼을 눌렀다"와 "링커 스크립트를 고쳐서 .data를 TCM에 올렸다"를 가르려는 단어 |
| (없는 단어) RTOS, BSP | 이 문장에는 없다 | RTOS는 J11, BSP는 J02가 따로 맡는다. 이 문장은 **코어 + 빌드 체인**만이다 |

핵심: 이 문장은 "Cortex 칩이 들어간 보드를 만져 봤나"가 아니라 **"코어가 무엇을 하고, 그걸 빌드하는 체인이 무엇을 만들어 내는지 설명할 수 있나"**를 묻는다.

---

## 1. Hark에서 실제로 하게 될 일 (추정)

context 파일 2.2/2.7절의 구조 추정(Qualcomm SoC + always-on 저전력 MCU)을 근거로 한다. 전부 [추정]이다.

- **[추정] MCU 쪽 (Cortex-M)**: 새 EVT 보드에 처음 이미지를 올린다. 벤더 SDK의 startup 파일과 링커 스크립트를 이 보드의 메모리 맵에 맞게 고친다. 센서·마이크 드라이버를 NVIC 우선순위 체계 안에 배치한다. RAM이 부족해지면 map 파일을 놓고 어떤 버퍼를 어디로 옮길지 결정한다.
- **[추정] 하루 업무 예**: 아침에 HW팀이 "리비전 B에서 SPI2가 안 뜬다"고 한다 → 링커 스크립트의 `.bss` 끝이 새 SRAM 뱅크 경계를 넘었는지 map 파일로 확인 → J-Link + GDB로 HardFault 잡아 CFSR 읽음 → `addr2line`으로 stacked PC를 함수로 환산 → 원인이 DMA 버퍼가 non-cacheable 영역 밖에 있던 것으로 판명.
- **[추정] 한 주 업무 예**: 펌웨어 크기가 flash 예산을 넘어 CI가 깨진다 → `-Og`/`-Os` 비교, `--gc-sections` 누락 확인, newlib-nano 적용, float printf 제거로 20KB 회수 → CI에 `--print-memory-usage` 게이트 추가.
- **[추정] SoC 쪽 (Cortex-A)**: Qualcomm BSP는 보통 벤더가 준 코드베이스를 쓴다. 이 역할이 매일 커널을 짜지는 않아도, **MCU ↔ SoC IPC**, 부팅 순서, 전원 시퀀스에서 양쪽을 다 봐야 한다. 그래서 "Cortex-A를 설계할 줄 안다"보다 **"A 쪽 부팅·devicetree·로그를 읽고 경계에서 디버깅할 줄 안다"**가 요구 수준일 가능성이 높다.
- **[추정] 채용 관점**: 1세대 기기 팀은 인원이 적다. 링커 스크립트를 고칠 줄 아는 사람이 팀에 1~2명뿐인 경우가 흔하고, 그 사람이 bring-up 병목을 푼다. Don의 pre-silicon/FPGA bring-up 경험이 여기에 직결된다.

---

## 2. 핵심 개념

### 2.1 면접관이 실제로 확인하는 8가지

| # | 확인 항목 | "IDE만 쓴 사람"의 답 | "코어를 아는 사람"의 답 |
|---|---|---|---|
| 1 | 예외 진입 | "인터럽트가 걸리면 핸들러로 간다" | "하드웨어가 8워드를 자동 push하고 LR에 EXC_RETURN을 넣는다" |
| 2 | 우선순위 | "숫자를 설정한다" | "숫자가 작을수록 높다. 상위 N비트만 구현되어 칩마다 레벨 수가 다르다" |
| 3 | 스택 | "스택 크기를 늘렸다" | "MSP/PSP가 나뉘고, RTOS task는 PSP를 쓴다" |
| 4 | 폴트 | "HardFault가 떴다" | "CFSR을 읽고 stacked PC로 원인 명령을 찾는다" |
| 5 | MPU | "안 써 봤다" | "task 스택 끝에 no-access 영역을 두면 overflow가 MemManage로 잡힌다" |
| 6 | 캐시/DMA | "가끔 데이터가 이상하다" | "M7은 D-cache가 있어 clean/invalidate와 32B 정렬이 필요하다" |
| 7 | 링커 | "IDE가 알아서 한다" | "LMA/VMA가 다르고 startup이 .data를 복사한다" |
| 8 | map 파일 | "본 적 없다" | "어떤 아카이브 멤버가 왜 끌려왔는지 map에서 찾는다" |

### 2.2 Cortex-M 예외 모델 — 하드웨어가 공짜로 해 주는 일

인터럽트가 뜨면 소프트웨어가 레지스터를 저장하기 전에 코어가 먼저 움직인다.

```
 [Thread 모드, PSP 사용 중]
        │  IRQn assert
        ▼
 ① 현재 스택(SP)에 8워드 자동 push  ── "exception stack frame"
        높은 주소 ┌──────────┐
                 │  xPSR    │
                 │  PC      │ ← 중단된 명령 주소 (bit0=0)
                 │  LR      │
                 │  R12     │
                 │  R3      │
                 │  R2      │
                 │  R1      │
        SP →     │  R0      │
        낮은 주소 └──────────┘
   (FPU 사용 중이면 S0~S15 + FPSCR + 예약 1워드 = 18워드 추가,
    단 FPCCR.LSPEN=1이면 자리만 잡고 실제 저장은 미룸 = lazy stacking)

 ② LR ← EXC_RETURN (0xFFFFFFxx 패턴). 반환 방식을 인코딩한 값
 ③ SP ← MSP 로 전환, 모드 ← Handler
 ④ PC ← 벡터 테이블[IRQn] 에서 읽은 주소
```

EXC_RETURN 주요 값(ARMv7-M 기준):

| 값 | 반환할 모드 | 사용할 스택 | FP 프레임 |
|---|---|---|---|
| `0xFFFFFFF1` | Handler | MSP | 없음 |
| `0xFFFFFFF9` | Thread | MSP | 없음 |
| `0xFFFFFFFD` | Thread | PSP | 없음 |
| `0xFFFFFFE1` | Handler | MSP | 있음 |
| `0xFFFFFFE9` | Thread | MSP | 있음 |
| `0xFFFFFFED` | Thread | PSP | 있음 |

ARMv8-M은 여기에 Secure/Non-secure 상태 비트가 더 붙는다. 이 표는 ARMv7-M 값이다.

이게 왜 중요한가: RTOS 컨텍스트 스위치(PendSV)는 **하드웨어가 이미 절반을 저장해 줬다는 전제** 위에서 나머지 R4~R11만 저장한다. 이 사실을 모르면 "왜 PendSV 핸들러가 저렇게 짧지?"를 설명하지 못한다.

### 2.3 우선순위 — 가장 자주 틀리는 지점

- 숫자가 **작을수록 높은 우선순위**다. Reset=-3, NMI=-2, HardFault=-1로 고정.
- NVIC의 우선순위 레지스터는 필드가 8비트지만 **구현된 상위 비트 수는 칩마다 다르다**. 3비트면 8단계, 4비트면 16단계. 하위 비트는 읽으면 0이다.
- 그래서 "우선순위 5"는 칩을 밝히지 않으면 의미가 없다. Cortex-M0/M0+(ARMv6-M)는 2비트 = 4단계다.
- `AIRCR.PRIGROUP`이 preempt priority와 sub priority의 경계를 정한다. sub priority는 선점에 관여하지 않고 **동시에 pending된 예외 중 무엇을 먼저 처리할지**만 정한다.
- 마스킹: `PRIMASK`는 configurable priority 전부를 막는다(HardFault/NMI 제외). `BASEPRI`는 "이 값보다 낮은 우선순위만" 막는다 — **ARMv7-M 이상에만 있다**. Cortex-M0+에는 BASEPRI가 없어 critical section이 `PRIMASK` 전면 차단이 된다.

### 2.4 스택 두 개 — MSP와 PSP

| 레지스터 | 누가 쓰나 | 왜 나눴나 |
|---|---|---|
| MSP (Main Stack Pointer) | reset 직후, 모든 예외 핸들러 | 벡터 테이블의 첫 워드로 초기화된다 |
| PSP (Process Stack Pointer) | RTOS의 각 task | task 스택이 터져도 커널/ISR 스택은 살아 있게 |

`CONTROL` 레지스터 bit1(SPSEL)이 Thread 모드에서 어느 쪽을 쓸지 정하고, bit0(nPRIV)이 권한, bit2(FPCA)가 FP 컨텍스트 사용 여부를 표시한다.

### 2.5 폴트 — CFSR을 읽는 습관

| 레지스터 | 주소 | 무엇 |
|---|---|---|
| CFSR | `0xE000ED28` | MMFSR(byte0) + BFSR(byte1) + UFSR(upper halfword) |
| HFSR | `0xE000ED2C` | bit30 FORCED(= 하위 폴트가 escalate됨), bit1 VECTTBL |
| MMFAR | `0xE000ED34` | MemManage 주소 (MMFSR.MMARVALID일 때) |
| BFAR | `0xE000ED38` | BusFault 주소 (BFSR.BFARVALID일 때) |

Cortex-M0/M0+에는 이 레지스터들이 없다. 폴트는 전부 HardFault로 오고, **stacked PC를 직접 꺼내 보는 것 말고는 단서가 없다**. 이 차이를 말할 수 있으면 v6-M/v7-M을 구분해서 아는 사람이다.

```c
/* HardFault에서 stacked frame 포인터를 C 핸들러로 넘기는 표준 관용구.
   EXC_RETURN bit2 가 0이면 MSP, 1이면 PSP 를 쓰고 있었다. */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile (
        "tst lr, #4            \n"
        "ite eq                \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "b hard_fault_report   \n"
    );
}

struct exc_frame { uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr; };

void hard_fault_report(struct exc_frame *f)
{
    volatile uint32_t cfsr = *(volatile uint32_t *)0xE000ED28u;
    volatile uint32_t hfsr = *(volatile uint32_t *)0xE000ED2Cu;
    /* f->pc 를 기록해 두면 addr2line 으로 함수/라인을 복원할 수 있다. */
    (void)cfsr; (void)hfsr; (void)f;
    for (;;) { }
}
```

### 2.6 MPU — "안 써 봤다"로 끝내면 아까운 질문

MPU는 MMU가 아니다. 주소를 **번역하지 않고** 영역별 권한/속성만 검사한다.

| | ARMv7-M MPU (M3/M4/M7) | ARMv8-M MPU (M23/M33/M55/M85) |
|---|---|---|
| 영역 지정 | base + 2의 거듭제곱 size | base + limit (임의 크기, 32B 단위) |
| 정렬 | size에 맞춰 정렬 필요 | 32바이트 정렬 |
| 부분 비활성화 | 8개 subregion disable 비트 | 없음 (필요 없음) |
| 메모리 속성 | RASR 안에 TEX/S/C/B 직접 인코딩 | MAIR 간접 인덱스 |
| 영역 개수 | 구현 의존(흔히 8 또는 16) | 구현 의존(흔히 8 또는 16) |

임베디드에서 실제로 쓰는 용도 세 가지:
1. **스택 overflow 잡기** — task 스택 하한에 no-access 영역을 두면 넘치는 순간 MemManage 폴트. 값이 조용히 망가지는 대신 즉시 터진다.
2. **NULL 포인터 역참조 잡기** — 주소 0 근처를 no-access로. 단 벡터 테이블이 거기 있으면 VTOR로 먼저 옮겨야 한다.
3. **DMA 버퍼를 non-cacheable로** — 캐시 있는 코어에서 일관성 문제를 원천 차단.

### 2.7 캐시와 DMA 일관성 — Cortex-M7 이상에서만 생기는 문제

M0~M4는 캐시가 없어 이 문제가 없다. M7/M55/M85는 L1 I/D 캐시가 있다.

```
 [DMA가 메모리에 쓰는 경우: 주변장치 → 메모리 (RX)]
   CPU 캐시에 그 주소의 옛날 줄이 남아 있으면,
   DMA가 DRAM/SRAM을 갱신해도 CPU는 캐시의 낡은 값을 읽는다.
   → DMA 완료 후, 읽기 전에 Invalidate

 [DMA가 메모리를 읽는 경우: 메모리 → 주변장치 (TX)]
   CPU가 쓴 값이 아직 캐시에만 있고 메모리에 안 내려갔으면,
   DMA는 옛날 값을 보낸다.
   → DMA 시작 전에 Clean (write-back)
```

CMSIS 함수 이름은 `SCB_CleanDCache_by_Addr()`, `SCB_InvalidateDCache_by_Addr()`, `SCB_CleanInvalidateDCache_by_Addr()`이다. 함정: **캐시 라인 단위로 동작**한다(Cortex-M7은 32바이트). 버퍼가 32바이트 정렬이 아니면 같은 라인에 있는 옆 변수까지 invalidate되어 그 변수가 날아간다. 그래서 DMA 버퍼는 `__attribute__((aligned(32)))` + 크기도 32의 배수로 잡는다.

### 2.8 Cortex-A는 무엇이 근본적으로 다른가

Don이 약한 쪽이다. 깊게 아는 척하지 말고 **"차이를 안다"** 수준을 정확히 보여주는 게 목표다.

| 축 | Cortex-M | Cortex-A |
|---|---|---|
| 메모리 | 물리 주소 직접. MPU는 권한만 | **MMU**가 가상→물리 번역. 페이지 테이블, TLB |
| 특권 구조 | Thread/Handler + privileged/unprivileged | **EL0~EL3** (앱 / OS / 하이퍼바이저 / 시큐어 펌웨어) |
| 인터럽트 컨트롤러 | NVIC (코어에 내장, 벡터 테이블 = 주소 배열) | **GIC** (별도 IP). 하나의 IRQ 진입점에서 INTID를 읽어 분기 |
| 명령어 집합 | Thumb 전용 | AArch64(A64) / AArch32 |
| 부팅 | reset 벡터 → startup → main | **다단 부트로더** 체인 |
| 소프트웨어 | bare-metal 또는 RTOS | Linux/Android + devicetree + 드라이버 모델 |
| 타이머 | SysTick | Generic Timer (CNTFRQ_EL0 등) |

GIC 인터럽트 ID 범위(GICv2/v3 공통 부분):

| INTID | 종류 | 뜻 |
|---|---|---|
| 0~15 | SGI | Software Generated Interrupt (코어 간 IPI) |
| 16~31 | PPI | Private Peripheral Interrupt (코어 전용, 예: generic timer) |
| 32~1019 | SPI | Shared Peripheral Interrupt (일반 주변장치) |
| 8192~ | LPI | GICv3의 메시지 기반 인터럽트 (ITS) |

Arm 레퍼런스 부트 체인(TF-A 기준):

```
 BL1  (BootROM, 마스크 ROM. 변경 불가)
   └─> BL2  (플랫폼 초기화, DRAM 초기화, 다음 단계 로드/검증)
         ├─> BL31 (EL3 런타임 펌웨어. PSCI 로 코어 on/off, 전원 관리)
         └─> BL33 (U-Boot 또는 UEFI)  ──> Linux kernel (EL1)
```

Qualcomm 같은 벤더는 이름이 다르다(PBL → SBL/XBL → ABL → 커널). **벤더마다 다름**을 반드시 붙여서 말한다.

---

## 3. 실무 패턴과 함정

### 3.1 툴체인 — arm-none-eabi-gcc 플래그를 근거 있게

코어별 최소 세트(하드 FP를 쓰는 경우):

| 코어 | 플래그 |
|---|---|
| Cortex-M0+ | `-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft` |
| Cortex-M3 | `-mcpu=cortex-m3 -mthumb -mfloat-abi=soft` |
| Cortex-M4F | `-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard` |
| Cortex-M7 (DP FPU) | `-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard` |
| Cortex-M33 | `-mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard` |

`-mfloat-abi` 세 값의 차이는 면접에서 자주 파고든다.

| 값 | 코드 생성 | 함수 인자 전달 |
|---|---|---|
| `soft` | FP 명령 안 씀, 라이브러리 호출 | 정수 레지스터 |
| `softfp` | FP 명령 씀 | **정수 레지스터** (soft와 ABI 호환) |
| `hard` | FP 명령 씀 | **FP 레지스터** |

함정: 벤더가 준 `.a`가 softfp로 빌드됐는데 앱을 hard로 빌드하면 링크 에러가 난다 — `uses VFP register arguments, output does not`. 이때 답은 "전부 같은 float-abi로 다시 빌드한다"이다.

크기/디버그 관련:

| 플래그 | 효과 | 주의 |
|---|---|---|
| `-Og -g3` | 디버깅 가능한 수준의 최적화 + 매크로까지 디버그 정보 | 개발 기본값으로 좋다 |
| `-Os` | 크기 최적화 | 인라인이 줄어 스택 프로파일이 바뀔 수 있다 |
| `-ffunction-sections -fdata-sections` | 심볼마다 섹션 분리 | 링커 옵션과 **짝**으로 써야 의미 있음 |
| `-Wl,--gc-sections` | 참조 없는 섹션 제거 | 벡터 테이블처럼 참조가 없는 것은 `KEEP()`으로 보호 |
| `-Wl,-Map=build/fw.map` | map 파일 생성 | CI에 남겨 두면 크기 회귀 추적이 쉽다 |
| `-Wl,--print-memory-usage` | 링크 직후 MEMORY 영역별 사용률 출력 | 링커 스크립트에 MEMORY 블록이 있어야 함 |
| `-flto` | 링크 타임 최적화 | 섹션 배치 attribute와 충돌하는 경우가 있다 |
| `-fno-exceptions -fno-rtti` | C++ 런타임 비용 제거 | `-fno-use-cxa-atexit`도 같이 보는 경우가 많다 |
| `-specs=nano.specs` | newlib-nano 사용 | printf의 float 미지원이 기본 |
| `-specs=nosys.specs` | `_write`, `_sbrk` 등 syscall을 stub으로 | 링크는 되지만 printf가 아무 데도 안 나간다 |
| `-u _printf_float` | newlib-nano에서 float printf 강제 포함 | 수 KB 늘어난다. 정말 필요한지 먼저 본다 |

### 3.2 링커 스크립트 — 최소 골격과 읽는 순서

```ld
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 1024K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH   /* gc-sections 로부터 보호 */
  .text       : { *(.text*) *(.rodata*) } > FLASH

  _sidata = LOADADDR(.data);                        /* LMA: flash 안의 사본 */
  .data : {
    _sdata = .;
    *(.data*)
    _edata = .;
  } > RAM AT > FLASH                                /* VMA=RAM, LMA=FLASH */

  .bss : {
    _sbss = .;
    *(.bss*) *(COMMON)
    _ebss = .;
  } > RAM
}
```

읽을 때의 핵심 한 문장: **`AT >`가 있으면 그 섹션은 flash에 저장되어 있고(LMA), 실행 시 RAM 주소(VMA)에서 동작한다. 그 사이를 이어 주는 게 startup 코드의 복사 루프다.**

`.bss`는 flash를 차지하지 않는다 — startup이 0으로 채우기만 하면 되기 때문이다. `size` 출력에서 `data`는 flash와 RAM을 **둘 다** 먹고, `bss`는 RAM만 먹는다. 이 구분을 못 하면 "왜 flash 사용량이 data+text인가"를 설명하지 못한다.

### 3.3 map 파일 읽는 법 — 면접에서 바로 먹히는 4가지

| 보는 것 | map 안의 단서 | 무엇을 알 수 있나 |
|---|---|---|
| 왜 이 라이브러리가 들어왔나 | `Archive member included to satisfy reference by file (symbol)` | 누가 `sprintf`를 불러서 newlib이 끌려왔는지 |
| 무엇이 버려졌나 | `Discarded input sections` | `--gc-sections`가 제대로 동작했는지 |
| 실제 배치 | `Memory Configuration` + 섹션별 주소/크기 목록 | `.bss`가 RAM 끝을 넘었는지 |
| 무엇이 큰가 | 심볼별 주소·크기 줄 | 어떤 배열/테이블이 RAM을 먹는지 |

map보다 빠른 방법도 같이 말하면 좋다:

```sh
# 섹션별 요약 (text/data/bss)
arm-none-eabi-size -Ax build/fw.elf
arm-none-eabi-size -Bd build/fw.elf

# 큰 심볼 순으로
arm-none-eabi-nm --print-size --size-sort --radix=d build/fw.elf | tail -30

# C++ 심볼 디맹글링 + 소스 섞은 디스어셈블
arm-none-eabi-objdump -d -S -C build/fw.elf | less

# 섹션 헤더(주소/크기/LMA)
arm-none-eabi-objdump -h build/fw.elf

# 크래시 로그의 PC 값을 파일:라인으로
arm-none-eabi-addr2line -e build/fw.elf -f -C -p 0x0001a3c4
```

`addr2line`은 필드 크래시 대응의 핵심 도구다. **릴리스마다 `.elf`를 아카이브해 두지 않으면 쓸 수 없다** — 이 운영상의 조건까지 말하면 실제로 해 본 사람으로 들린다.

### 3.4 빌드 시스템 세 가지

| 체계 | 언제 만나나 | 핵심 명령 |
|---|---|---|
| Make | 벤더 SDK 예제, 오래된 프로젝트 | `make -j8`, `make V=1`로 실제 명령 확인 |
| CMake | 요즘의 기본. `CMAKE_TOOLCHAIN_FILE`로 크로스 컴파일 지정 | `cmake -B build -DCMAKE_TOOLCHAIN_FILE=arm.cmake && cmake --build build` |
| west (Zephyr) | Zephyr/nRF Connect SDK. 내부는 CMake + Kconfig + devicetree | `west build -b <board> app`, `west flash`, `west build -t menuconfig` |

컴파일 문제를 디버깅할 때 쓰는 공통 무기:

```sh
# 전처리 결과만 보기 (매크로가 무엇으로 펼쳐졌나)
arm-none-eabi-gcc -E -dM -mcpu=cortex-m4 -mthumb foo.c | sort

# 어셈블리 출력 (컴파일러가 정말 그 명령을 냈나)
arm-none-eabi-gcc -S -Og -mcpu=cortex-m4 -mthumb foo.c -o -

# CMake 빌드에서 실제 컴파일 커맨드 보기
cmake --build build -- VERBOSE=1
```

### 3.5 디버그 심볼 워크플로

```
 [빌드]  fw.elf  (심볼 + 디버그 정보 포함, 플래시에 쓰지는 않음)
            │
            ├─ objcopy -O binary  → fw.bin   (실제로 플래시에 쓰는 것)
            └─ objcopy -O ihex    → fw.hex

 [디버그] OpenOCD 또는 J-Link GDB Server 를 띄우고
          arm-none-eabi-gdb build/fw.elf
            (gdb) target extended-remote localhost:3333   # OpenOCD 기본 포트
            (gdb) monitor reset halt
            (gdb) load
            (gdb) break main
            (gdb) continue
```

J-Link GDB Server의 기본 포트는 2331이다. 포트 번호는 툴마다 다르므로 **"OpenOCD는 3333, SEGGER는 2331"** 정도로 말하면 충분하다.

`.elf`와 실제 플래시 내용이 어긋나면(예: 다시 빌드했는데 플래시는 옛날 이미지) 브레이크포인트가 엉뚱한 곳에 걸린다. 그래서 `load`를 습관처럼 넣거나, 빌드 해시를 이미지에 넣고 런타임에 출력한다.

### 3.6 함정 모음

| 함정 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| float-abi 불일치 | 링크 에러 `uses VFP register arguments` | 라이브러리와 앱의 ABI가 다름 | 전 구성요소를 같은 `-mfloat-abi`로 재빌드 |
| `--gc-sections`인데 벡터 테이블 소실 | 부팅 즉시 HardFault | 아무도 참조하지 않아 제거됨 | 링커 스크립트에서 `KEEP()` |
| `-specs=nosys.specs`만 넣음 | printf가 조용히 사라짐 | `_write`가 stub | UART로 보내는 `_write` 직접 구현 |
| newlib-nano + `%f` | `%f`가 그대로 출력됨 | float printf 미포함 | `-u _printf_float` 또는 정수로 포맷 |
| 스택/힙 크기 미지정 | 희귀한 데이터 손상 | 링커 스크립트가 예약 영역을 안 잡음 | `_estack` 및 힙/스택 가드 영역 명시 |
| VTOR 미설정 부트로더 | 앱의 ISR이 안 불림 | 벡터 테이블이 여전히 0 기준 | 앱 startup에서 `SCB->VTOR = 앱 베이스` |
| DMA 버퍼가 캐시 영역 | 간헐적 데이터 깨짐 (M7) | clean/invalidate 누락 | 32B 정렬 + 명시적 캐시 유지, 또는 MPU로 non-cacheable |
| 32B 정렬 안 한 invalidate | 옆 전역변수가 0으로 | 캐시 라인 단위 동작 | 버퍼 주소·크기 모두 32의 배수로 |
| 릴리스 `.elf` 미보관 | 필드 크래시 PC 해석 불가 | 아티팩트 관리 부재 | CI에서 elf + map을 버전 태그와 함께 저장 |

---

## 4. 리서치 — 근거 자료

| 자료 | 무엇을 담고 있나 | 어디를 읽어야 하나 | URL |
|---|---|---|---|
| ARMv7-M Architecture Reference Manual | Cortex-M3/M4/M7의 정본. 예외 모델, 시스템 제어 블록, MPU | B1 "System Level Architecture" (예외/우선순위), B3 (SCB, CFSR, MPU 레지스터) | https://developer.arm.com/documentation/ddi0403/latest/ |
| ARMv8-M Architecture Reference Manual | Cortex-M23/M33/M55/M85. TrustZone-M, 새 MPU | MPU 프로그래머 모델, Security state 전환 | https://developer.arm.com/documentation/ddi0553/latest/ |
| Cortex-M4 Devices Generic User Guide | 레지스터 비트 단위 설명이 가장 읽기 쉬운 문서 | 4장 Core Peripherals (NVIC, SCB, SysTick, MPU) | https://developer.arm.com/documentation/dui0553/latest/ |
| Arm Architecture Reference Manual for A-profile | Cortex-A 정본. EL, MMU, 시스템 레지스터 | D "AArch64 System Level Architecture" — EL 모델, 번역 테이블 | https://developer.arm.com/documentation/ddi0487/latest/ |
| GICv3/GICv4 Architecture Specification | GIC 동작, INTID 분류, ITS | 1장 개요, 2장 Distributor/Redistributor/CPU interface | https://developer.arm.com/documentation/ihi0069/latest/ |
| Arm ABI (AAPCS) | 레지스터 호출 규약, 스택 정렬 | `aapcs32.rst` / `aapcs64.rst`의 "Core registers", "The Stack" | https://github.com/ARM-software/abi-aa |
| CMSIS 문서 | `SCB_*`, `NVIC_*`, `__disable_irq` 등 표준 API 이름의 근거 | CMSIS-Core "Cache Functions", "NVIC Functions" | https://arm-software.github.io/CMSIS_6/latest/General/index.html |
| Arm GNU Toolchain 다운로드 | arm-none-eabi 공식 배포처 | 릴리스 노트에서 newlib 버전 확인 | https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads |
| GCC ARM Options | `-mcpu`, `-mfpu`, `-mfloat-abi` 조합의 정본 | "ARM Options" 전체 | https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html |
| GNU ld 매뉴얼 | 링커 스크립트 문법(MEMORY, SECTIONS, AT, KEEP, LOADADDR) | 3장 "Linker Scripts" | https://sourceware.org/binutils/docs/ld/ |
| GNU binutils 매뉴얼 | objdump/nm/size/addr2line/objcopy 옵션 | 각 도구의 옵션 절 | https://sourceware.org/binutils/docs/binutils/ |
| newlib 문서 | newlib / newlib-nano 차이, syscall stub 목록 | "Syscalls" 절 | https://sourceware.org/newlib/ |
| OpenOCD 매뉴얼 | GDB 연결, 플래시 드라이버, `monitor` 명령 | "GDB and OpenOCD" 장 | https://openocd.org/doc/html/index.html |
| Trusted Firmware-A 문서 | BL1/BL2/BL31/BL33 부트 흐름, PSCI | "Firmware Design" | https://trustedfirmware-a.readthedocs.io/ |
| Linux arm64 booting 문서 | 커널이 부트로더에 요구하는 상태(EL, MMU off, devicetree 포인터) | 전체가 짧다 | https://docs.kernel.org/arch/arm64/booting.html |
| U-Boot 문서 | BL33 단계에서 실제로 하는 일 | "Booting" 절 | https://docs.u-boot.org/ |
| Zephyr 문서 | west 빌드, 보드 이름 체계 | "Building, Flashing and Debugging" | https://docs.zephyrproject.org/latest/develop/west/build-flash-debug.html |

**버전 의존 주의**
- NVIC 우선순위 비트 수, MPU 영역 개수, 캐시 유무는 **코어가 아니라 실리콘 구현**이 정한다. 반드시 칩 데이터시트/TRM으로 확인한다.
- Zephyr 보드 이름은 3.7에서 도입된 hardware model v2로 바뀌었다. 예전 `nrf52840dk_nrf52840` → 현재 `nrf52840dk/nrf52840` 형태. **사용 중인 Zephyr 버전에 따라 다름**.
- `-mfpu` 이름(fpv4-sp-d16, fpv5-d16 등)은 GCC 버전에 따라 `-mcpu=cortex-m7+nofp` 같은 확장 문법으로 대체 가능하다. GCC 버전마다 지원 범위가 다름.
- newlib-nano가 기본인지 여부는 **배포판마다 다름**. Arm 공식 툴체인은 `-specs=nano.specs`를 명시해야 한다.

---

## 5. 예상 면접 질문

난이도 표기: **[기초]** = 못 하면 탈락, **[중급]** = 실무 경험 확인, **[심화]** = 레벨을 올리는 질문.

### Q01. [기초] What happens in hardware when an interrupt fires on Cortex-M?

**왜 묻나**: 이 하나로 "IDE 사용자"와 "코어를 아는 사람"이 갈린다. 후속 질문(RTOS 컨텍스트 스위치, 폴트 분석) 전부의 전제다.

**30초 답변**: 코어가 현재 스택에 8워드(R0-R3, R12, LR, PC, xPSR)를 자동으로 push하고, LR에 EXC_RETURN을 넣고, MSP로 전환한 뒤 Handler 모드로 들어가 벡터 테이블에서 핸들러 주소를 읽어 점프한다. FPU를 쓰고 있었으면 FP 레지스터도 대상이지만 lazy stacking으로 실제 저장은 미뤄질 수 있다.

**English answer**: The core stacks eight words automatically — R0 through R3, R12, LR, the return PC, and xPSR — onto whichever stack was active. It loads LR with an EXC_RETURN value that encodes which mode and which stack to return to, switches to MSP in Handler mode, and fetches the handler address from the vector table. If the FPU context was active, another eighteen words are reserved for S0-S15 and FPSCR, though lazy stacking defers the actual save until the handler touches the FPU. That is exactly why an RTOS context switch in PendSV only has to save R4 through R11 — the hardware already did half the work.

**꼬리질문**
- "EXC_RETURN 0xFFFFFFFD는 무슨 뜻인가?" → Thread 모드로, PSP를 써서, FP 프레임 없이 복귀.
- "tail-chaining이 뭔가?" → 인터럽트가 연달아 pending이면 unstack 후 다시 stack하지 않고 바로 다음 핸들러로 넘어간다. 지연이 줄어든다.
- "lazy stacking을 끄는 이유는?" → 최악 지연을 결정적으로 만들고 싶을 때. 대신 FP를 쓰는 ISR의 진입 시간이 항상 길어진다.

### Q02. [기초] On Cortex-M, is priority 2 higher or lower than priority 5? And how many priority levels does your chip have?

**왜 묻나**: 방향을 반대로 아는 사람이 정말 많다. 두 번째 질문은 "칩 문서를 읽는 사람인가"를 본다.

**30초 답변**: 숫자가 작을수록 높다. 레벨 수는 코어가 아니라 실리콘이 정한다. NVIC 우선순위 필드는 8비트지만 구현된 상위 비트만 유효해서 3비트면 8단계, 4비트면 16단계다. Cortex-M0+는 2비트 = 4단계다.

**English answer**: Lower numbers mean higher priority, so 2 preempts 5. The number of usable levels is implementation-defined: the register field is eight bits wide, but silicon vendors only implement the top few. Three bits gives eight levels, four bits gives sixteen, and the unimplemented low bits read back as zero. On ARMv6-M parts like Cortex-M0+ you only get two bits, so four levels. I always check the device header for the priority-bits define before assigning anything.

**꼬리질문**
- "priority grouping은?" → AIRCR.PRIGROUP이 preempt/sub 경계를 정한다. sub priority는 선점에 관여하지 않고 동시 pending 시 순서만 정한다.
- "왜 하위 비트가 0으로 읽히나?" → 미구현 비트라서. 그래서 값을 쓸 때 상위로 시프트해서 넣어야 한다.

### Q03. [기초] PRIMASK versus BASEPRI — how do you implement a critical section?

**왜 묻나**: RTOS·드라이버 설계의 기본기. v6-M/v7-M 구분을 아는지도 같이 본다.

**30초 답변**: PRIMASK는 configurable 우선순위 전부를 막는다. BASEPRI는 지정한 값보다 낮은 우선순위만 막아서, 고우선순위 실시간 인터럽트는 계속 살려 둘 수 있다. BASEPRI는 ARMv7-M 이상에만 있고 Cortex-M0/M0+에는 없다.

**English answer**: PRIMASK is a single bit that masks every configurable-priority interrupt, so it is the blunt instrument. BASEPRI masks only interrupts at or below a given priority level, which lets you protect a kernel data structure while a hard real-time interrupt — say an audio DMA completion — still gets through. FreeRTOS uses exactly that: everything at or below configMAX_SYSCALL_INTERRUPT_PRIORITY is masked, and anything above it must never call a FreeRTOS API. On ARMv6-M there is no BASEPRI, so the critical section is all-or-nothing with PRIMASK.

**꼬리질문**
- "critical section 안에서 하면 안 되는 일은?" → 블로킹 호출, 긴 루프, printf. 최악 지연이 곧 시스템의 인터럽트 응답 시간이 된다.
- "중첩되면?" → PRIMASK 값을 저장/복원하는 형태로 감싸야 한다. 무조건 enable하면 바깥 critical section이 깨진다.

### Q04. [중급] You get a HardFault in the field. Walk me through finding the cause.

**왜 묻나**: 실제로 디버깅해 본 사람인지가 바로 드러난다.

**30초 답변**: CFSR을 읽어 어떤 폴트인지 보고, HFSR.FORCED면 escalate된 하위 폴트다. MMFAR/BFAR가 valid면 문제 주소가 나온다. stacked frame에서 PC를 꺼내 addr2line으로 함수와 라인을 찾는다. 재현이 안 되면 이 정보를 fault 핸들러에서 비휘발성 영역에 기록하고 다음 부팅에 보낸다.

**English answer**: First I read CFSR at 0xE000ED28 to see whether it was a MemManage, BusFault, or UsageFault, and I check HFSR bit 30 — if FORCED is set, a configurable fault escalated because it was disabled or masked. If MMARVALID or BFARVALID is set, MMFAR or BFAR gives me the faulting address. Then I take the stacked PC from the exception frame, which tells me the instruction, and run addr2line against the exact ELF for that build. For field units that only fail rarely, I make the fault handler write a small record — CFSR, stacked PC and LR, task name — into a reserved RAM or flash region that survives reset, and ship it up with telemetry on the next boot.

**꼬리질문**
- "Cortex-M0+에서는?" → CFSR 자체가 없다. HardFault만 있고 stacked PC가 유일한 단서다.
- "왜 릴리스 elf가 필요한가?" → addr2line은 디버그 정보를 elf에서 읽는다. bin만으로는 주소를 함수로 되돌릴 수 없다.
- "가장 흔한 원인은?" → 스택 overflow, NULL/dangling 포인터, 정렬되지 않은 접근, 초기화 안 된 주변장치 클럭.

### Q05. [중급] How does the linker script and startup code get from reset to main()?

**왜 묻나**: BSP를 처음부터 올려 본 사람인지 확인. 이 역할의 핵심 업무다.

**30초 답변**: 벡터 테이블 첫 워드가 초기 MSP, 둘째 워드가 Reset_Handler 주소다. Reset_Handler는 `.data`를 flash의 LMA에서 RAM의 VMA로 복사하고 `.bss`를 0으로 채운 뒤, C++이면 전역 생성자를 돌리고 main으로 점프한다. 링커 스크립트가 그 복사에 필요한 심볼(`_sidata`, `_sdata`, `_edata`, `_sbss`, `_ebss`)을 정의한다.

**English answer**: On reset the core loads the first word of the vector table into MSP and the second word into PC, so it lands in Reset_Handler. That routine copies the .data section from its load address in flash to its run address in RAM — the linker script sets that up with `> RAM AT > FLASH` and exposes the boundary symbols — then zeroes .bss, optionally calls SystemInit to set up clocks, runs the C++ static constructors through __libc_init_array, and branches to main. If a bootloader is jumping to an application, the application startup also has to set SCB->VTOR to its own vector table base, otherwise interrupts still dispatch through the bootloader's table.

**꼬리질문**
- "왜 .bss는 flash를 안 먹나?" → 값이 전부 0이라 저장할 게 없다. startup이 채운다.
- "`size` 출력의 data가 왜 flash와 RAM 둘 다 차지하나?" → 초기값 사본이 flash에 있고 실행 사본이 RAM에 있어서.
- "링커 스크립트에서 KEEP이 필요한 이유는?" → `--gc-sections`가 참조 없는 벡터 테이블을 지워 버린다.

### Q06. [중급] Your firmware suddenly doesn't fit in flash. What do you do, in order?

**왜 묻나**: 툴체인을 도구로 쓰는지, 감으로 찍는지 본다.

**30초 답변**: 먼저 `size`로 증가분을 확인하고, map 파일에서 어떤 아카이브가 왜 끌려왔는지 찾는다. 대개 printf 계열이 범인이다. `-ffunction-sections -fdata-sections` + `--gc-sections`가 켜져 있는지 확인하고, newlib-nano로 바꾸고, float printf를 뺀다. 그다음 `-Os`, LTO를 검토한다. 마지막으로 큰 상수 테이블을 찾아 압축하거나 외부 flash로 뺀다.

**English answer**: I never guess. I diff `arm-none-eabi-size` against the last good build, then open the map file and look at the "archive member included to satisfy reference" lines — nine times out of ten something pulled in full printf or a floating-point formatting path. Then I verify the build actually has -ffunction-sections, -fdata-sections and -Wl,--gc-sections, because without the linker half the compiler flags do nothing. After that: newlib-nano via -specs=nano.specs, drop -u _printf_float, try -Os on non-critical translation units, and consider LTO. If it is still tight I sort symbols by size with nm and look at the big const tables — those often belong in external flash or can be generated at runtime.

**꼬리질문**
- "`--print-memory-usage`는?" → 링크 직후 MEMORY 영역별 퍼센트를 출력한다. CI 게이트로 쓰기 좋다.
- "LTO의 위험은?" → 섹션 배치 attribute나 weak 심볼 오버라이드가 기대대로 동작하지 않을 수 있다.

### Q07. [중급] What is the difference between soft, softfp and hard float ABI, and when does it bite you?

**왜 묻나**: 벤더 바이너리를 통합할 때 실제로 겪는 문제. Hark처럼 여러 벤더 SDK를 붙이는 팀에 직결된다.

**30초 답변**: soft는 FP 명령을 아예 안 쓰고 라이브러리로 처리한다. softfp는 FP 명령을 쓰지만 인자는 정수 레지스터로 넘겨 soft와 호환된다. hard는 인자도 FP 레지스터로 넘긴다. 섞이면 링크 단계에서 "uses VFP register arguments" 에러가 난다.

**English answer**: They differ in two independent things: whether the compiler emits VFP instructions, and how floating-point arguments cross a function boundary. Soft does neither — everything goes through libgcc calls. Softfp emits VFP instructions but still passes floats in core registers, so it links against soft-float objects. Hard passes them in VFP registers, which is faster but ABI-incompatible with the other two. The failure mode is a link error saying the object uses VFP register arguments while the output does not, and it usually shows up when a vendor gives you a prebuilt .a. There is no clever fix — everything in the image has to be built with the same setting.

**꼬리질문**
- "Cortex-M4F에서 `-mfpu`는?" → `fpv4-sp-d16`, 단정밀도만.
- "double 연산을 쓰면?" → M4F의 FPU는 single만 하드웨어 지원이라 double은 소프트웨어 에뮬레이션이 된다. 코드가 커지고 느려진다.

### Q08. [심화] How do you guarantee DMA and CPU see the same data on a Cortex-M7?

**왜 묻나**: 캐시가 있는 M 코어를 다뤄 봤는지. 오디오/센서 스트리밍에서 바로 터지는 문제다.

**30초 답변**: TX는 DMA 시작 전에 clean(write-back), RX는 DMA 완료 후 읽기 전에 invalidate. 캐시 라인 단위(M7은 32바이트)로 동작하므로 버퍼는 32바이트 정렬에 크기도 32의 배수여야 한다. 더 안전한 방법은 MPU로 DMA 영역을 non-cacheable로 잡는 것이다.

**English answer**: On M7 the D-cache sits between the core and SRAM, and the DMA engine does not. For a transmit buffer I clean the cache by address before starting the transfer so the data is actually in memory; for a receive buffer I invalidate before reading so I do not get a stale line. The subtlety is granularity: these operations work on 32-byte lines, so if my buffer is not 32-byte aligned and a multiple of 32 bytes, invalidating it will also throw away a neighbouring variable that happens to share a line — and that bug looks like random corruption of an unrelated global. When I can, I prefer to configure an MPU region covering the DMA buffers as non-cacheable device or normal-non-cacheable memory, so the whole class of bug disappears at the cost of some bandwidth.

**꼬리질문**
- "M4에서는 왜 이 문제가 없나?" → 캐시가 없다. TCM/SRAM을 직접 접근한다.
- "메모리 배리어는 언제?" → 캐시 유지 명령 후 DSB, 명령어 스트림에 영향 주는 변경 후 ISB.

### Q09. [심화] How would you use the MPU in a product?

**왜 묻나**: "설정만 할 줄 아는 사람"이 아니라 "쓸 데를 아는 사람"인지 본다. 대부분 답을 못 한다.

**30초 답변**: 세 가지다. task 스택 하한에 no-access 영역을 두어 overflow를 즉시 MemManage로 잡는다. 주소 0 근처를 막아 NULL 역참조를 잡는다. DMA 버퍼 영역을 non-cacheable로 만든다. 성능 비용은 거의 없고, 조용한 데이터 손상을 결정적인 폴트로 바꾸는 게 핵심 가치다.

**English answer**: The value of the MPU in a small system is not security so much as turning silent corruption into a deterministic fault. The first use is a guard region at the low end of each task stack with no access — an overflow faults immediately instead of quietly overwriting the neighbouring task's control block, and the fault handler can name the task. The second is blocking the area around address zero so a null dereference traps, though you have to relocate the vector table with VTOR first. The third is marking DMA buffers non-cacheable on a cached core. If the product later needs isolation between a trusted and an untrusted component, that is when I would look at ARMv8-M TrustZone rather than the MPU alone.

**꼬리질문**
- "ARMv7-M MPU의 제약은?" → 영역이 2의 거듭제곱 크기에 그 크기로 정렬되어야 한다. subregion 비트로 8등분 중 일부만 끌 수 있다.
- "RTOS와 같이 쓰면?" → 컨텍스트 스위치마다 MPU 영역을 갱신해야 해서 스위치 비용이 늘어난다.

### Q10. [심화] Compare Cortex-M and Cortex-A. Which have you worked with?

**왜 묻나**: JD에 "or"가 있으므로 이건 필터가 아니라 **범위 파악**이다. 솔직도도 같이 본다.

**30초 답변**: M은 MPU로 권한만 검사하고 물리 주소를 직접 쓰며 NVIC가 코어에 내장되어 벡터 테이블이 주소 배열이다. A는 MMU로 가상 주소를 번역하고 EL0~EL3의 특권 계층이 있고 GIC라는 별도 인터럽트 컨트롤러가 있으며, 다단 부트로더를 거쳐 Linux를 올린다.

**English answer**: Structurally the differences are memory, privilege and interrupts. Cortex-M runs on physical addresses with an optional MPU that only checks permissions; Cortex-A has a real MMU with translation tables and a TLB, so every access is translated. M has two privilege levels in thread mode; A has exception levels zero through three, splitting application, OS, hypervisor and secure firmware. M has the NVIC built into the core with a vector table that is literally an array of handler addresses; A has an external GIC, and you take one IRQ entry and read the interrupt ID to dispatch. And boot: M goes reset vector to startup to main, while A goes through a multi-stage chain — boot ROM, a platform loader, EL3 runtime firmware, then U-Boot or UEFI, then the kernel. My hands-on depth is on the M and R side; on the A side I have worked at the boundary rather than inside the kernel, and I would say that plainly.

**꼬리질문**
- "GIC의 SPI/PPI/SGI 구분은?" → SGI 0~15 코어 간, PPI 16~31 코어 전용, SPI 32~1019 공용 주변장치.
- "Cortex-R은 어디에 있나?" → 실시간용. MPU 기반이고 결정적 지연이 목표라 M과 A 사이. 스토리지 컨트롤러, 모뎀, 자동차에 쓰인다.

### Q11. [심화] Describe the boot chain of an ARM application processor running Linux.

**왜 묻나**: Cortex-A 쪽 깊이 측정. Don에게는 갭 질문이다 — 모른다고 하지 말고 **아는 뼈대를 정확히** 말한다.

**30초 답변**: 마스크 ROM이 첫 단계를 올리고, 플랫폼 로더가 DRAM을 초기화하고, EL3 런타임 펌웨어가 PSCI 같은 전원 서비스를 제공하고, U-Boot/UEFI가 커널과 devicetree를 메모리에 올려 EL1으로 진입시킨다. 각 단계가 다음 단계를 서명 검증하면 secure boot 체인이 된다. 벤더마다 단계 이름이 다르다.

**English answer**: The generic Arm reference flow is boot ROM, then a platform initialisation stage that brings up DRAM, then EL3 runtime firmware that stays resident to provide PSCI power services, then a normal-world bootloader like U-Boot or UEFI, and finally the kernel entered at EL1 with the MMU off and a pointer to the device tree in a register. Each stage verifying the signature of the next is what makes it a chain of trust. Vendor naming differs — Qualcomm parts, for instance, use their own stage names — so on a real program I would read the vendor's boot architecture document rather than assume. I want to be straight with you: I have debugged at this boundary, but I have not owned a Linux BSP end to end.

**꼬리질문**
- "devicetree가 뭔가?" → 하드웨어 구성을 서술하는 자료구조. 커널이 컴파일 타임 하드코딩 대신 런타임에 읽는다. Zephyr도 같은 개념을 쓴다.
- "커널 진입 조건은?" → MMU off, 캐시 정리, 특정 레지스터에 DTB 주소. 문서가 짧으니 읽어 두면 좋다.

### Q12. [중급] Walk me through your build and debug workflow, command by command.

**왜 묻나**: "associated toolchains"를 가장 직접적으로 묻는 질문. IDE 이름만 대면 여기서 걸린다.

**30초 답변**: CMake로 크로스 툴체인 파일을 지정해 빌드하고, elf에서 bin/hex를 뽑고, size와 map으로 크기를 확인한다. 디버그는 OpenOCD나 J-Link GDB Server를 띄우고 arm-none-eabi-gdb로 붙어 load 후 브레이크포인트를 건다. 크래시 주소는 addr2line으로 환산한다.

**English answer**: I build with CMake and a toolchain file that pins arm-none-eabi-gcc and the cpu flags, and I keep -Wl,-Map and --print-memory-usage on so every build tells me its footprint. objcopy produces the bin or hex that actually gets flashed, but I always keep the ELF because that is where the symbols live. For debugging I start OpenOCD or the SEGGER GDB server, attach with arm-none-eabi-gdb against the ELF, do monitor reset halt, load, and then work with breakpoints and watchpoints. When something is wrong at the instruction level I go to objdump -d -S. When I have a crash address from the field I use addr2line against the archived ELF for that exact release.

**꼬리질문**
- "CI에서 뭘 게이트로 두나?" → 워닝 0, flash/RAM 사용률 상한, 정적 분석, 단위 테스트(호스트 빌드).
- "static analysis는?" → `-Wall -Wextra -Werror`부터. 그 위에 cppcheck나 상용 도구.

### Q13. [중급] How do you profile where cycles go on a Cortex-M?

**왜 묻나**: 저전력·레이턴시 예산(JD의 다른 문장들)과 연결되는 실무 능력.

**30초 답변**: DWT의 CYCCNT로 구간 사이클을 직접 센다. GPIO 토글 + 로직 애널라이저로 타이밍을 외부에서 본다. ITM/SWO로 이벤트를 흘린다. 코어가 지원하면 ETM 트레이스로 명령 흐름 전체를 잡는다.

**English answer**: The cheapest and most precise tool is the DWT cycle counter — enable the trace block, zero CYCCNT, run the region, read it back, and you have exact cycles with almost no overhead. For anything involving interrupt latency or interaction with hardware I toggle a GPIO at entry and exit and look at it on a logic analyser, because that also shows me the gap between the hardware event and the handler. ITM with SWO is good for low-overhead event streams, and SEGGER RTT works when I only have SWD pins. If the part has ETM and I have a trace probe, instruction trace tells me the whole story, which is what I have used on higher-end cores.

**꼬리질문**
- "CYCCNT의 함정은?" → 코어 클럭이 바뀌면(DVFS, sleep) 사이클과 시간의 관계가 깨진다.
- "인터럽트 지연을 어떻게 재나?" → 이벤트 발생 핀과 ISR 진입 핀을 같은 LA로 캡처해 차이를 본다.

### Q14. [심화] A bootloader jumps to an application but the application's interrupts never fire. Why?

**왜 묻나**: OTA/부트로더(J04)와 이 문장의 교차점. 실제로 자주 나는 버그다.

**30초 답변**: 앱이 VTOR를 자기 벡터 테이블 베이스로 안 옮겼을 가능성이 가장 크다. 그 외에 부트로더가 켜 놓은 인터럽트를 안 끄고 점프했거나, 앱 진입 전 MSP를 앱 테이블 첫 워드로 설정하지 않았거나, 클럭·주변장치 상태가 부트로더가 남긴 그대로인 경우다.

**English answer**: The first thing I check is SCB->VTOR. If the application never points it at its own vector table, every interrupt still dispatches through the bootloader's table, which either does nothing or traps. The second is hygiene at the jump: the bootloader should disable interrupts, deinit any peripheral it enabled, set MSP from the first word of the application's vector table, and only then branch to the reset handler address from the second word — with bit zero handled correctly for Thumb. A third, subtler one is that VTOR has an alignment requirement, so if the application is linked at an address that is not sufficiently aligned, the write silently does not take effect.

**꼬리질문**
- "Cortex-M0에는 VTOR가 없는데?" → M0는 VTOR가 없어 벡터 테이블 재배치가 안 된다(M0+는 옵션). RAM에 테이블 복사 후 메모리 리맵 같은 벤더별 수단을 쓴다.
- "점프 전 인터럽트 정리를 안 하면?" → 앱 초기화 도중에 옛 ISR이 떠서 초기화되지 않은 상태를 건드린다.

### Q15. [중급] Which Cortex core would you pick for an always-on audio wake-word path, and why?

**왜 묻나**: 코어 지식을 제품 결정으로 연결할 수 있는지. Hark의 실제 문제와 가장 가깝다.

**30초 답변**: 마이크를 항상 듣는 쪽은 μA급 슬립과 DSP 명령이 필요하므로 저전력 M4F/M33 계열이 맞다. 추론이 무거우면 캐시/DSP가 있는 M7/M55 또는 별도 DSP·NPU로 넘긴다. 단 캐시가 붙는 순간 DMA 일관성 관리가 필요해진다는 비용이 생긴다.

**English answer**: For the always-listening stage what matters is energy per inference and deep-sleep leakage, not peak MIPS, so I would start with a low-power M4F or M33 class core with DSP instructions and a good retention sleep mode, feeding it from an I2S or PDM DMA in ping-pong buffers so the core wakes only per block. If the model does not fit that budget I would look at M55 with Helium or a dedicated DSP or NPU, but I would be explicit about the cost: once you add a cache you inherit the DMA coherency work, and once you add a second compute engine you inherit a memory-sharing and wake-handshake problem. In a two-chip design the honest framing is that the MCU's job is to be cheap enough to never sleep, and to decide when waking the big SoC is worth the joules.

**꼬리질문**
- "M55의 Helium은?" → ARMv8.1-M의 MVE 벡터 확장. INT8 추론 처리량을 올린다.
- "SoC를 언제 깨우나?" → false accept 비용과 전력을 저울질해 2단계 검출(MCU 1차, SoC 2차 확인)을 쓴다.

---

## 6. Don 매핑

근거는 context 3.1절의 레쥬메 문장만 사용한다.

### 6.1 그대로 쓸 수 있는 증거

| 레쥬메 근거 | 이 문장에 어떻게 대응하나 |
|---|---|
| "ARM Cortex R8/R82/M0+ … FW bring-up" | **세 개 코어를 다 올려 봤다**는 말이 된다. 특히 M0+는 ARMv6-M이라 BASEPRI/CFSR가 없는 제약을 직접 겪었을 가능성이 높다. 그 제약을 말하면 깊이가 증명된다 |
| "Developed using embedded C and C++ programming on bare-metal (pre/post-silicon)" | 툴체인·링커 스크립트·startup을 직접 다뤘다는 근거. bare-metal은 RTOS가 없다는 뜻이라 **부트 경로 전부를 본인이 소유**했다는 의미다 |
| FPGA pre-silicon bring-up | 첫 부팅이 안 될 때의 절차를 말할 수 있는 드문 경험. "reset 벡터·클럭·메모리 맵 셋 중 무엇이 틀렸는지 좁혀 간다"는 답이 나온다 |
| "SoC verification … I2C, SPI, DMA, PCIe, SRAM/DRAM bring-up" | 메모리 맵과 DMA를 실제로 다뤘다는 근거. 캐시/DMA 일관성 질문으로 자연스럽게 연결된다 |
| Trace32 | Cortex-R/A 쪽에서 많이 쓰는 도구. **"Trace32를 써 봤다"는 Cortex-A 대화에서 신뢰도를 올린다** |
| "JTAG, Oscilloscope, Logic Analyzer, Power Analyzer" | 디버그 워크플로 질문(Q12, Q13)의 직접 근거 |

### 6.2 강점을 이야기로 만드는 법

Q01/Q05 같은 기초 질문에서는 개념만 말하고 끝내지 말고 **한 문장짜리 경험 태그**를 붙인다.

> "...and that is not theory for me — on the SSD program I brought firmware up on Cortex-R8 and M0+ on an FPGA before silicon, which means I owned the reset vector, the startup copy loop and the linker script rather than inheriting a working project."

Q04(HardFault)는 Don의 강점 영역이다. 레쥬메의 error reporting/handling 설계 경험과 연결한다.

> "In production SSD firmware I owned the error reporting and handling path, so I think about faults as something you design for, not something you debug once. The same instinct applies here: reserve a region that survives reset, record CFSR and the stacked PC, and make sure the release ELF is archived so addr2line still works a year later."

Q08(캐시/DMA)은 SSD의 데이터 경로 경험으로 받는다. **단, SSD 컨트롤러의 캐시 구조와 Cortex-M7의 D-cache는 다르므로** "같은 종류의 문제"라고만 말하고 세부를 지어내지 않는다.

### 6.3 갭 — Cortex-A를 솔직하게 답하는 스크립트

JD가 "Cortex-M **or** Cortex-A"라고 썼으므로 **A를 모른다고 해서 요건 미달이 아니다**. 방어하지 말고 경계를 그은 뒤 인접 경험으로 이어 붙인다.

권장 3단 구조 — ① 내 깊이가 어디까지인지 명시 → ② 그래도 아는 뼈대를 보여 줌 → ③ 인접 경험으로 연결.

> "Let me be precise about where my depth is. My hands-on core work is Cortex-M and Cortex-R — bare-metal bring-up, exception model, linker scripts, fault analysis. On the Cortex-A side I have worked at the boundary rather than inside it: I know the exception level model, that the GIC replaces the NVIC and you dispatch from an interrupt ID, and that boot is a multi-stage signed chain ending in the kernel at EL1. What I have actually done is debug across that boundary — at Apple I root-cause interface failures between a new radio chip and a full application-processor system over PCIe, I2C, SPMI and RFFE, which means reading logs and bus captures on both sides. If this role needs someone to own a Linux BSP end to end, I would be learning that; if it needs someone to make the MCU and the SoC agree with each other, that is what I already do."

이 답의 강점: **거짓이 없고, 요건의 "or"를 이용하며, Apple 경험이 오히려 희소 자원으로 들린다.**

### 6.4 확인 필요

- `<확인 필요: SSD 펌웨어 프로젝트에서 링커 스크립트를 직접 작성/수정했는가, 아니면 팀의 기존 스크립트를 사용했는가>` — Q05/Q06 답의 강도가 달라진다.
- `<확인 필요: Cortex-M0+ 작업에서 폴트 디버깅을 해 봤는가>` — 해 봤다면 "v6-M은 CFSR이 없어 stacked PC만 봤다"는 문장이 아주 강한 증거가 된다.
- `<확인 필요: 사용한 툴체인이 arm-none-eabi-gcc인가, ARM Compiler(armclang)인가, 벤더 전용인가>` — 이 노트의 플래그를 그대로 말하려면 GCC 기반이어야 한다. armclang이면 옵션 이름이 다르다(`-mcpu`는 같지만 링커는 armlink + scatter file).
- `<확인 필요: Trace32를 Cortex-R에만 썼는가, Cortex-A 타깃에도 썼는가>` — A에도 썼다면 6.3 스크립트에 한 문장 추가 가능.
- `<확인 필요: Apple 업무에서 Android/Linux 로그(logcat, dmesg)를 직접 보는가>` — 본다면 Cortex-A 갭이 한 단계 좁혀진다.

### 6.5 절대 하지 말 것

- "Cortex-A도 좀 했습니다"처럼 모호하게 넘기기. 꼬리질문 두 개면 드러난다.
- MMU 페이지 테이블 포맷, GIC 레지스터 이름 등 외운 것을 먼저 꺼내기. 실제로 해 본 사람 앞에서는 역효과다.
- 링커 스크립트를 안 고쳐 봤는데 고쳐 봤다고 하기. "그때 `AT >`를 어떻게 썼나"로 바로 검증된다.

---

## 7. 준비 체크리스트

- [ ] Cortex-M 예외 진입 8워드 스택 프레임을 **종이에 그려서** 설명해 본다 (Q01을 소리 내어 3회)
- [ ] 갖고 있는 보드/SDK의 헤더에서 `__NVIC_PRIO_BITS` 값을 찾아 "이 칩은 몇 단계인가"를 확인한다
- [ ] 아무 프로젝트나 빌드해 `arm-none-eabi-size -Ax`와 map 파일을 열고, 가장 큰 심볼 5개를 `nm --size-sort`로 찾아 본다
- [ ] `-specs=nano.specs`를 넣고 빼면서 크기 차이를 측정해 숫자로 기억한다 (면접에서 "몇 KB 줄었다"가 강하다)
- [ ] 최소 링커 스크립트(2.x의 골격)를 백지에서 다시 써 본다. `AT >`와 `_sidata`의 역할을 말로 설명
- [ ] HardFault 핸들러(2.5의 naked 함수)를 실제로 넣고, 일부러 NULL 역참조를 만들어 CFSR과 stacked PC를 찍어 본다
- [ ] 그 PC 값을 `arm-none-eabi-addr2line`으로 함수:라인으로 환산해 본다 — 이 전체 사이클을 한 번은 손으로 해 둔다
- [ ] Cortex-M vs Cortex-A 비교표(2.8)를 **외우지 말고 이유로** 설명할 수 있게 한다: "M은 왜 MMU가 없나" → 결정적 지연과 비용
- [ ] 6.3의 Cortex-A 솔직 답변 스크립트를 영어로 소리 내어 연습한다 (60초 이내)
- [ ] 6.4의 `<확인 필요>` 5개를 본인 기억으로 채운다 — 특히 링커 스크립트와 툴체인 종류
- [ ] 캐시/DMA 일관성(2.7)을 SSD 데이터 경로 경험과 잇는 2문장을 만든다. **다르다는 점을 명시**하는 문장으로

---

## 8. 더 읽기

| 가고 싶은 곳 | 노트와 절 |
|---|---|
| Cortex-M 프로그래머 모델 전체 | C01 §2 (레지스터, 모드, Thumb) |
| 예외/NVIC를 처음부터 | C01 §3 (예외 번호, EXC_RETURN, tail-chaining, 마스킹) |
| 부트와 링커 스크립트 심화 | C01 §5, §6 (startup 코드, LMA/VMA, 섹션 배치) |
| MPU·캐시 | C01 §7 |
| Cortex-A 차이 상세 | C01 §8 (EL, GIC, 부트 체인) |
| 툴체인 옵션·map 파일·CMSIS | C01 §9, §10 |
| 원자성, volatile, 메모리 배리어 | C02 (Cortex-M 원자성 절) |
| 폴트 해석·디버그 프로브 실전 | C10 (HardFault 해석, J-Link/OpenOCD/Trace32) |
| 같은 주제의 면접 문답 | S02 Q01~Q06 (부트/벡터/NVIC/critical section/HardFault/PendSV), Q25~Q28 (volatile, 배리어, 원자성) |
| 컨텍스트 스위치가 이 예외 모델 위에서 어떻게 동작하나 | C04 §3 |
| JD의 이웃 문장 | J01 (C/C++ on ARM SoC — 업무 관점), J09 (자원 제약 C/C++), J11 (RTOS), J14 (디버깅 툴) |
