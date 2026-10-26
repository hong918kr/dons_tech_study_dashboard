# B4. RTOS가 쓰는 세 예외 (SysTick · PendSV · SVC) — SysTick이 시간을 알리고, PendSV가 미뤄 둔 전환을 실행하고, SVC가 커널의 문을 연다

> **이 노트를 다 읽으면**: SysTick의 네 레지스터(CSR · RVR · CVR · CALIB)로 1 kHz tick과 µs 단위 정확한 지연을 직접 만들 수 있다 · PendSV가 왜 "가장 낮은 우선순위로 미뤄 둔 소프트웨어 인터럽트"여야 하는지 타임스탬프로 증명할 수 있다 · `svc #imm`의 번호를 쌓인 PC에서 꺼내고 R0~R3로 인자를 받고 쌓인 R0로 값을 돌려주는 미니 시스템 콜을 짤 수 있다 · FreeRTOS `port.c`와 Zephyr `cortex_m` 코드에서 세 예외가 어디서 설정되고 어디서 불리는지 짚을 수 있다
> **왜 필요한가**: RTOS 포팅 · bring-up 면접의 단골 질문 "Why is the context switch done in PendSV, and why is PendSV the lowest priority?"와 "How does SVC get its number?"에 레지스터 이름과 측정 숫자로 답해야 하고, 실무에서는 "tick이 이상하다 · 첫 태스크가 안 뜬다 · HardFault가 svc에서 난다"를 이 세 예외에서 찾는다
> **Don 기준 난이도**: 타이머 · 인터럽트 우선순위 · 레지스터 비트는 이미 잘 안다 / 새로 배울 부분은 "결정과 실행을 다른 예외로 나누는" RTOS의 설계 이유, PendSV 요청이 깃발 하나로 합쳐지는 성질, SVC의 동기 예외 성질(지금 못 들어가면 HardFault), FreeRTOS와 Zephyr가 우선순위를 서로 다르게 배치하는 이유
> **선행 노트**: B1(프로그래머 모델 · MSP/PSP · CONTROL), B2(예외 진입 · 복귀 · EXC_RETURN · tail-chaining), B3(NVIC · 우선순위 · BASEPRI), A7(같은 보드에서 잰 RTOS 비용). 이어지는 노트: C2(PendSV 핸들러 어셈블리 한 줄씩), C3(SVC로 첫 태스크 시작), C5(tick과 시간 관리), C6(tickless), C8(커널 진입 경로 · 시스템 콜), E3(지연 · 지터 분해), M6(타이밍 측정)
> 🔁 **Bare-Metal 겹침**: BM C7(SysTick · DWT) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 "RTOS 없이 SysTick 하나로 슈퍼 루프의 시간 기반 · 타임아웃 · 지연을 만드는 법"만 더한다

---

## 0. 큰 그림 — 결정하는 예외와 실행하는 예외를 나눈다

Don이 SSD 컨트롤러 펌웨어에서 쓰던 실행부(executive)를 떠올려 보자. NAND 완료 인터럽트가 오면 ISR은 일을 다 하지 않는다. "이 채널의 완료 큐에 일이 생겼다"는 **깃발(flag)** 만 세우고 빠져나온다. 그리고 main 루프가 한 바퀴 돌 때 깃발을 보고 실제 일을 처리한다. 깃발은 여러 번 세워도 한 번 처리하면 된다. ISR이 여러 개 겹쳐도, 실제 처리는 "모든 ISR이 끝나고 main으로 돌아온 다음"에 한 번 일어난다.

Cortex-M의 RTOS는 이 패턴을 **하드웨어 예외 하나로** 만든다. 그게 PendSV다.

- **SysTick** — 코어 안에 들어 있는 24비트 타이머. 주기적으로 "시간이 흘렀다"고 알린다. RTOS는 여기서 "깨어날 태스크가 있나, time slice가 끝났나"를 **결정**한다.
- **PendSV** — "pendable service call". 소프트웨어가 비트 하나(`ICSR.PENDSVSET`)를 써서 요청하는 예외. RTOS는 이걸 **가장 낮은 우선순위**로 두고, 여기서 컨텍스트 스위치를 **실행**한다.
- **SVC** — `svc #imm` 명령이 일으키는 **동기(synchronous)** 예외. 명령을 실행하는 바로 그 순간 들어간다. RTOS는 이걸로 첫 태스크를 시작하거나(FreeRTOS Cortex-M3 포트), 비특권 코드가 커널을 부르는 시스템 콜 문으로 쓴다(MPU 포트, Zephyr userspace).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="110" y="16" font-size="13" text-anchor="middle" font-weight="bold">결정한다</text>
<text x="565" y="16" font-size="13" text-anchor="middle" font-weight="bold">실행한다</text>
<rect x="10" y="25" width="200" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="110" y="45" font-size="13" text-anchor="middle">SysTick (tick)</text>
<text x="110" y="64" font-size="12" text-anchor="middle">시간이 흘렀다 → 깨울 태스크?</text>
<rect x="10" y="90" width="200" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="110" y="110" font-size="13" text-anchor="middle">주변장치 ISR</text>
<text x="110" y="129" font-size="12" text-anchor="middle">사건 발생 → 태스크를 깨움</text>
<rect x="10" y="155" width="200" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="110" y="175" font-size="13" text-anchor="middle">태스크의 API 호출</text>
<text x="110" y="194" font-size="12" text-anchor="middle">블록 · taskYIELD()</text>
<rect x="260" y="90" width="160" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="110" font-size="13" text-anchor="middle">ICSR.PENDSVSET</text>
<text x="340" y="129" font-size="12" text-anchor="middle">깃발 하나 (요청이 합쳐짐)</text>
<line x1="210" y1="50" x2="258" y2="105" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="210" y1="115" x2="258" y2="115" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="210" y1="180" x2="258" y2="125" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="460" y="90" width="210" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="565" y="110" font-size="13" text-anchor="middle">PendSV (최저 우선순위)</text>
<text x="565" y="129" font-size="12" text-anchor="middle">모든 ISR이 끝난 뒤 전환 실행</text>
<line x1="420" y1="115" x2="458" y2="115" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="460" y="170" width="210" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="565" y="195" font-size="12" text-anchor="middle">새 태스크가 Thread 모드에서 실행</text>
<line x1="565" y1="140" x2="565" y2="168" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="10" y="240" width="660" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.08" stroke="#4a7bd0" stroke-dasharray="5 3"/>
<text x="340" y="262" font-size="13" text-anchor="middle">SVC #imm — 동기 예외 (명령을 실행하는 그 순간 들어간다)</text>
<text x="340" y="283" font-size="12" text-anchor="middle">FreeRTOS CM3: 첫 태스크 시작 (svc 0) · MPU 포트 · Zephyr userspace: 시스템 콜 문</text>
<text x="340" y="318" font-size="12" text-anchor="middle">세 예외의 번호: SVCall = 11 · PendSV = 14 · SysTick = 15 (IPSR 값)</text>
</svg>
```

그림 1 — RTOS의 역할 분담. 왼쪽 세 곳은 "다음에 누가 돌아야 하나"를 **결정**만 하고 깃발을 세운다. 실제 전환은 PendSV 한 곳에서, 모든 ISR이 끝난 뒤 한 번 **실행**된다. SVC는 이 흐름과 따로, 코드가 "지금 당장" 커널에 들어가고 싶을 때 쓰는 문이다.

이 노트는 세 예외를 **하나씩 bare-metal로 직접 만들어 보고**, 마지막에 셋을 조립해 "스케줄러 뼈대"를 돌린다. 진짜 스택 전환(R4~R11 저장 · PSP 교체)은 C2가 한 줄씩 다루므로 여기서는 하지 않는다. 대신 "누가 결정하고 누가 언제 실행하나"를 타임스탬프로 보여 준다.

| 예외 | 번호 | 일으키는 것 | 동기/비동기 | RTOS에서 하는 일 | 이 노트 절 |
|---|---|---|---|---|---|
| SVCall | 11 | `svc #imm` 명령 | 동기 (지금 당장) | 첫 태스크 시작, 시스템 콜 | §3 |
| PendSV | 14 | `ICSR.PENDSVSET = 1` | 비동기 (우선순위가 허락할 때) | 컨텍스트 스위치 실행 | §2 |
| SysTick | 15 | 24비트 카운터가 0이 됨 (또는 `ICSR.PENDSTSET`) | 비동기 | tick: 시간 진행 · 깨움 · time slice 결정 | §1 |

### 실습 환경 한 줄

- QEMU `mps2-an385` = Cortex-M3 @ 25 MHz 가상 보드. 예제 대부분은 **RTOS 없는 bare-metal**(lab 템플릿 `baremetal-mps2`)로 SysTick · PendSV · SVC를 직접 다룬다. 커널 쪽 확인은 **FreeRTOS V11.3.2**(ARM_CM3 포트)와 **Zephyr v4.4.2**(`mps2/an385`)로, 다른 CPU 비교는 QEMU `virt` RISC-V rv32로 한다.
- 타이밍은 모두 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간)로 돌렸다. 실행할 때마다 같은 숫자가 나온다. **QEMU icount 기준이라 실제 칩과 절대값은 다르다**(캐시 · 플래시 wait state · 버스 · 예외 스태킹 사이클을 모델링하지 않는다). 이 노트가 숫자로 보여 주려는 것은 **순서와 모양**(PendSV가 언제 도나, 요청이 몇 번으로 합쳐지나, 드리프트가 쌓이나 안 쌓이나)이다.
- 시계는 MPS2 FPGAIO COUNTER(`0x40028018`, 리셋부터 25 MHz로 증가)를 쓴다. QEMU에는 DWT CYCCNT가 없다(§1.7에서 직접 확인).

### 예제 지도

| 예제 | 무엇을 확인하나 | 환경 |
|---|---|---|
| 1 | SysTick 네 레지스터: 리셋값, 24비트, 클럭 소스별 속도, COUNTFLAG | bare-metal |
| 2 | 1 kHz tick: 올바른 reload · off-by-one · ISR 재시작의 지터와 드리프트 | bare-metal |
| 3 | tick + CVR로 sub-tick 타임스탬프: 순진한 읽기가 거꾸로 가는 경우 | bare-metal |
| 4 | 돌고 있는 SysTick으로 µs 정확한 지연 (그리고 흔한 wrap 버그) | bare-metal |
| 5 | DWT CYCCNT 켜는 표준 순서 (실칩용, QEMU에서는 0) | bare-metal |
| 6 | 높은 ISR과 SysTick 안에서 요청한 PendSV가 실제로 도는 시각 | bare-metal |
| 7 | PendSV 요청 합쳐짐 · 취소 | bare-metal |
| 8 | PendSV 우선순위를 높게 두면 ISR 한가운데서 돈다 (EXC_RETURN 증거) | bare-metal |
| 9 | 미니 시스템 콜: `svc #imm` 번호 · R0~R3 인자 · 쌓인 R0 반환 · 비특권 | bare-metal |
| 10 | SVC 인코딩 디스어셈블과 HardFault 승격 | bare-metal + objdump |
| 11 | FreeRTOS가 설정한 세 예외 우선순위, tick 1000번에 PendSV 몇 번 | FreeRTOS |
| 12 | 스케줄러 뼈대: SysTick·ISR이 결정, PendSV가 실행 (간트 차트) | bare-metal |
| 13 | Zephyr가 설정한 세 예외 우선순위와 tickless SysTick | Zephyr |
| 14 | RISC-V에는 셋이 없다: mtimecmp · MSIP · ecall로 같은 역할 | QEMU virt rv32 |
| 15 | SysTick reload 계산기 (Python) | VM Python |

예제 공용 헤더 `b4.h`(Mac `~/.rtos_scratch/B4/b4.h`)는 다음과 같다. 레지스터 주소는 Armv7-M 아키텍처가 정한 System Control Space(SCS, `0xE000E000`~) 안에 있어서 **모든 Cortex-M3/M4/M7 칩에서 같다**.

```c
/* B4 공용 정의: SysTick · SCB · FPGAIO counter */
#ifndef B4_H
#define B4_H
#include <stdint.h>
#include "board.h"
#define SYST_CSR   REG32(0xE000E010u)   /* Control and Status */
#define SYST_RVR   REG32(0xE000E014u)   /* Reload Value (24-bit) */
#define SYST_CVR   REG32(0xE000E018u)   /* Current Value */
#define SYST_CALIB REG32(0xE000E01Cu)   /* Calibration (read-only) */
#define CSR_ENABLE    (1u << 0)
#define CSR_TICKINT   (1u << 1)
#define CSR_CLKSOURCE (1u << 2)         /* 1 = processor clock */
#define CSR_COUNTFLAG (1u << 16)
#define SCB_ICSR   REG32(0xE000ED04u)
#define ICSR_PENDSVSET (1u << 28)
#define ICSR_PENDSVCLR (1u << 27)
#define ICSR_PENDSTSET (1u << 26)
#define SCB_SHPR2  REG32(0xE000ED1Cu)   /* [31:24] SVCall */
#define SCB_SHPR3  REG32(0xE000ED20u)   /* [31:24] SysTick, [23:16] PendSV */
#define SCB_HFSR   REG32(0xE000ED2Cu)
#define FPGA_CNT   REG32(0x40028018u)   /* MPS2 FPGAIO COUNTER, 25 MHz */
static inline uint32_t cyc(void) { return FPGA_CNT; }
static inline uint32_t get_ipsr(void) { uint32_t r; __asm volatile("mrs %0, ipsr" : "=r"(r)); return r; }
#endif
```

빌드와 실행은 늘 같은 한 줄이다(예제 번호만 바꾼다).

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile \
  SRCS=/Users/donh/.rtos_scratch/B4/ex1/main.c OUT=/var/tmp/rtos/B4/ex1 && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B4/ex1/app.elf'
```

---

## 1. SysTick — 코어 안에 들어 있는 24비트 down counter

> 🔁 이 절은 Bare-Metal BM C7과 겹친다 (주인: 이 노트).

### 1.1 직관 — "모든 Cortex-M에 똑같이 달린 알람 시계"

STM32의 TIM2, nRF의 RTC, NXP의 PIT는 칩 회사가 만든 타이머다. 레지스터 주소도 동작도 회사마다 다르다. 반면 SysTick은 **Arm이 코어 안에 넣은 타이머**다. 주소(`0xE000E010`)와 비트가 아키텍처로 정해져 있다. 그래서 RTOS는 칩마다 타이머 드라이버를 새로 쓰지 않고 SysTick 하나로 tick을 만든다. FreeRTOS의 `vPortSetupTimerInterrupt()`가 Cortex-M3 포트 하나로 수백 종류의 칩에서 도는 이유다.

Don의 SSD 펌웨어 감각으로 말하면, SysTick은 "코어마다 하나씩 붙은 free-running down counter + 0 도달 인터럽트"다. 특징은 세 가지다.

- **아래로 센다**(down counter). RVR에서 시작해 0까지 1씩 줄고, 0 다음 클럭에 다시 RVR을 싣는다.
- **24비트**다. 최대 주기는 2^24 클럭. 25 MHz에서 671 ms, 480 MHz에서 35 ms다.
- **자동 reload**다. 소프트웨어가 다시 쓰지 않아도 주기가 이어진다. 그래서 ISR 진입이 늦어져도 **주기가 밀리지 않는다**(§1.5에서 측정).

### 1.2 네 레지스터

| 레지스터 | 주소 | 중요한 비트 | 뜻 |
|---|---|---|---|
| `SYST_CSR` (CTRL) | `0xE000E010` | bit0 ENABLE, bit1 TICKINT, bit2 CLKSOURCE, bit16 COUNTFLAG | 켜기 · 인터럽트 허용 · 클럭 고르기 · "0을 지났나" 깃발 |
| `SYST_RVR` (LOAD) | `0xE000E014` | [23:0] RELOAD | 0 다음에 CVR에 실을 값. 주기 = RVR + 1 클럭 |
| `SYST_CVR` (VAL) | `0xE000E018` | [23:0] CURRENT | 지금 카운터 값. **아무 값이나 쓰면 0이 되고 COUNTFLAG도 지워진다** |
| `SYST_CALIB` | `0xE000E01C` | bit31 NOREF, bit30 SKEW, [23:0] TENMS | 칩 회사가 넣은 "10 ms 분량 count 수" 힌트 (읽기 전용) |

괄호 안 이름(CTRL · LOAD · VAL · CALIB)은 CMSIS의 `SysTick->CTRL` 같은 구조체 필드 이름이다. Zephyr 드라이버는 이 이름을 쓰고, FreeRTOS는 `portNVIC_SYSTICK_CTRL_REG`처럼 주소를 직접 쓴다.

비트 하나씩 풀면 이렇다.

- **ENABLE** — 1이면 센다. 0이면 멈춘다(CVR 값은 남는다).
- **TICKINT** — 1이면 1 → 0이 될 때 SysTick 예외(번호 15)를 pending 시킨다. 0이면 COUNTFLAG만 선다(폴링용).
- **CLKSOURCE** — 1이면 프로세서 클럭, 0이면 칩이 정한 "외부 참조 클럭"(STCLK). 참조 클럭이 없는 칩도 있다(CALIB.NOREF = 1). 칩마다 참조 클럭이 무엇인지 다르니 데이터시트를 본다.
- **COUNTFLAG** — 마지막으로 CSR을 읽은 뒤 카운터가 1 → 0을 지난 적이 있으면 1. **CSR을 읽으면 지워진다.** CVR에 쓸 때도 지워진다.
- **RVR = 0** 이면 카운터가 0에 머물고 인터럽트도 COUNTFLAG도 생기지 않는다(예외는 1 → 0 전이에서만 생기기 때문).

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="30" width="170" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="95" y="52" font-size="13" text-anchor="middle">프로세서 클럭</text>
<rect x="10" y="80" width="170" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="95" y="102" font-size="13" text-anchor="middle">참조 클럭 (STCLK)</text>
<rect x="210" y="40" width="96" height="64" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="258" y="68" font-size="12" text-anchor="middle">CLKSOURCE</text>
<text x="258" y="86" font-size="12" text-anchor="middle">1 / 0</text>
<line x1="180" y1="47" x2="208" y2="60" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="180" y1="97" x2="208" y2="85" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="336" y="40" width="170" height="64" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="421" y="64" font-size="13" text-anchor="middle">CVR (24비트)</text>
<text x="421" y="84" font-size="12" text-anchor="middle">ENABLE=1이면 1씩 감소</text>
<line x1="306" y1="72" x2="334" y2="72" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="336" y="150" width="170" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="421" y="175" font-size="13" text-anchor="middle">RVR (reload 값)</text>
<line x1="421" y1="150" x2="421" y2="106" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="412" y="132" font-size="12" text-anchor="end">0 다음 클럭에 다시 싣기</text>
<rect x="540" y="30" width="130" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="605" y="52" font-size="12" text-anchor="middle">COUNTFLAG = 1</text>
<rect x="540" y="80" width="130" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="605" y="100" font-size="12" text-anchor="middle">TICKINT=1이면</text>
<text x="605" y="118" font-size="12" text-anchor="middle">예외 15 pending</text>
<line x1="506" y1="60" x2="538" y2="48" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="506" y1="85" x2="538" y2="100" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="518" y="140" font-size="12" text-anchor="middle">1 → 0</text>
<rect x="10" y="150" width="316" height="40" rx="5" fill="#888" fill-opacity="0.08" stroke="#888" stroke-dasharray="4 3"/>
<text x="168" y="175" font-size="12" text-anchor="middle">CALIB.TENMS = 10 ms 분량 count (칩마다 다름)</text>
<text x="10" y="235" font-size="12">QEMU mps2-an385: 프로세서 클럭 25 MHz, 참조 클럭 1 MHz, TENMS = 9999 (예제 1 실측)</text>
</svg>
```

그림 2 — SysTick 블록. 클럭 하나를 골라 24비트 CVR을 1씩 줄이고, 1 → 0이 되는 순간 COUNTFLAG를 세우고(TICKINT면 예외 15도 pending), 다음 클럭에 RVR을 다시 싣는다.

### 예제 1 — 네 레지스터를 직접 만져 보기

리셋 직후 값, RVR이 정말 24비트인지, CLKSOURCE 0/1일 때 1 ms에 몇 count 줄어드는지, COUNTFLAG가 읽으면 지워지는지를 확인한다.

```c
/* ex1: SysTick 네 레지스터 탐색 — 리셋값, 24비트 폭, 클럭 소스별 속도 */
#include "../b4.h"

static uint32_t rate_per_ms(uint32_t clksrc)
{
    SYST_CSR = 0;
    SYST_RVR = 0x00FFFFFFu;
    SYST_CVR = 0;                         /* 아무 값이나 쓰면 0 + COUNTFLAG 클리어 */
    SYST_CSR = CSR_ENABLE | clksrc;
    while (SYST_CVR == 0) { }             /* 첫 클럭에 RVR이 CVR로 로드될 때까지 */
    uint32_t c0 = cyc(), v0 = SYST_CVR;
    while (cyc() - c0 < 25000u) { }       /* FPGAIO 25000 cycle = 1 ms */
    uint32_t v1 = SYST_CVR;
    SYST_CSR = 0;
    return v0 - v1;                       /* 1 ms 동안 줄어든 카운트 */
}

int main(void)
{
    board_printf_raw("reset: CSR=0x%x RVR=0x%x CVR=0x%x CALIB=0x%x\n",
                     SYST_CSR, SYST_RVR, SYST_CVR, SYST_CALIB);
    uint32_t calib = SYST_CALIB;
    board_printf_raw("CALIB: NOREF=%u SKEW=%u TENMS=%u\n",
                     calib >> 31, (calib >> 30) & 1u, calib & 0xFFFFFFu);

    SYST_RVR = 0xFFFFFFFFu;               /* 32비트를 써도 */
    board_printf_raw("RVR after writing 0xFFFFFFFF -> 0x%x (24-bit)\n", SYST_RVR);

    board_printf_raw("CLKSOURCE=1 (CPU clock): %u counts per ms\n", rate_per_ms(CSR_CLKSOURCE));
    board_printf_raw("CLKSOURCE=0 (ref clock): %u counts per ms\n", rate_per_ms(0));
    SYST_RVR = 2500u - 1u; SYST_CVR = 0;  /* 100 us 주기, 인터럽트 없이 폴링 */
    SYST_CSR = CSR_ENABLE | CSR_CLKSOURCE;
    while (!(SYST_CSR & CSR_COUNTFLAG)) { }  /* 이 읽기가 COUNTFLAG를 지운다 */
    uint32_t c0 = cyc();
    while (!(SYST_CSR & CSR_COUNTFLAG)) { }
    uint32_t c1 = cyc(), again = SYST_CSR;
    board_printf_raw("COUNTFLAG period=%u cyc, flag on 2nd read=%u\n", c1 - c0, (again >> 16) & 1u);
    return 0;
}
```

```text
   text	   data	    bss	    dec	    hex	filename
   4580	     96	    328	   5004	   138c	/var/tmp/rtos/B4/ex1/app.elf
reset: CSR=0x0 RVR=0x0 CVR=0x0 CALIB=0x270f
CALIB: NOREF=0 SKEW=0 TENMS=9999
RVR after writing 0xFFFFFFFF -> 0xffffff (24-bit)
CLKSOURCE=1 (CPU clock): 25004 counts per ms
CLKSOURCE=0 (ref clock): 1000 counts per ms
COUNTFLAG period=2500 cyc, flag on 2nd read=0
[board_exit] code=0
```

출력에서 볼 것: RVR에 `0xFFFFFFFF`를 써도 `0xffffff`만 남는다(24비트). CLKSOURCE=1은 25 MHz(1 ms에 약 25000, 4 count 차이는 측정 루프의 오차), CLKSOURCE=0은 이 보드의 참조 클럭 1 MHz다. CALIB.TENMS = 9999는 "참조 클럭으로 10 ms = 10000 count"라는 뜻이다(RVR에 바로 넣을 수 있게 −1 된 값). COUNTFLAG는 2500 cycle(= RVR + 1)마다 서고, 한 번 읽으면 지워진다. 이후 예제부터는 빌드할 때 나오는 `arm-none-eabi-size` 줄을 생략한다.

> 실칩 주의: Armv7-M에서 RVR · CVR의 리셋 값은 정해져 있지 않다(UNKNOWN). QEMU가 0을 보여 준 것은 QEMU 사정이다. 그래서 FreeRTOS도 Zephyr도 **CVR에 0을 먼저 쓰고** 켠다. CALIB 값도 칩 회사가 정한 것이라, 클럭 트리를 바꾸면(PLL 설정) 맞지 않는다. 실무에서는 CALIB를 믿지 말고 `SystemCoreClock` 같은 실제 클럭 값으로 계산한다.

### 1.3 reload 계산 — "주기 = RVR + 1"

1 kHz tick을 만들려면 1 ms 동안의 클럭 수를 RVR에 넣는다. 단, **하나 빼서** 넣는다. 카운터는 RVR, RVR−1, …, 1, 0을 지나는데 그게 RVR + 1개의 클럭이기 때문이다.

- 25 MHz, 1 kHz: 25,000,000 / 1000 = 25000 count → **RVR = 24999**
- 168 MHz, 1 kHz: 168000 → RVR = 167999
- 24비트 한계: RVR ≤ 16,777,215. 480 MHz에서 100 Hz(4,800,000 count)는 들어가지만, 480 MHz에서 10 Hz(48,000,000)는 안 들어간다.

FreeRTOS 코드가 정확히 이렇게 쓴다(`portable/GCC/ARM_CM3/port.c`, `vPortSetupTimerInterrupt()`).

```c
portNVIC_SYSTICK_LOAD_REG = ( configSYSTICK_CLOCK_HZ / configTICK_RATE_HZ ) - 1UL;
```

### 예제 15 — reload 계산기 (Python, VM에서 실행)

여러 클럭과 tick rate에서 RVR, 24비트에 들어가는지, 나누어떨어지지 않을 때 tick이 얼마나 틀리는지 계산한다.

```python
# ex15: SysTick reload 계산기 — RVR, 24비트 한계, 나누어떨어지지 않을 때의 오차
clocks = [("AN385 25 MHz", 25_000_000), ("STM32F4 168 MHz", 168_000_000),
          ("STM32H7 480 MHz", 480_000_000), ("32.768 kHz ref", 32_768)]
print(f"{'clock':18} {'tick':>6} {'RVR':>10} {'fits24':>6} {'real tick Hz':>14} {'error ppm':>10}")
for name, f in clocks:
    for hz in (100, 1000, 10000):
        n = round(f / hz)                 # 한 주기 count 수 (정수여야 함)
        rvr = n - 1
        fits = rvr <= 0xFFFFFF
        real = f / n
        ppm = (real - hz) / hz * 1e6
        print(f"{name:18} {hz:>6} {rvr:>10} {str(fits):>6} {real:>14.4f} {ppm:>10.1f}")
for name, f in clocks[:3]:
    print(f"{name}: longest possible SysTick period = {0x1000000 / f * 1e3:.3f} ms")
# off-by-one(RVR = f/hz) 드리프트
f, hz = 25_000_000, 1000
ppm = (f / (f // hz + 1) - hz) / hz * 1e6
print(f"RVR=25000 instead of 24999: {ppm:.1f} ppm -> {abs(ppm) * 3600 / 1e6 * 1e3:.0f} ms lost per hour")
```

```text
clock                tick        RVR fits24   real tick Hz  error ppm
AN385 25 MHz          100     249999   True       100.0000        0.0
AN385 25 MHz         1000      24999   True      1000.0000        0.0
AN385 25 MHz        10000       2499   True     10000.0000        0.0
STM32F4 168 MHz       100    1679999   True       100.0000        0.0
STM32F4 168 MHz      1000     167999   True      1000.0000        0.0
STM32F4 168 MHz     10000      16799   True     10000.0000        0.0
STM32H7 480 MHz       100    4799999   True       100.0000        0.0
STM32H7 480 MHz      1000     479999   True      1000.0000        0.0
STM32H7 480 MHz     10000      47999   True     10000.0000        0.0
32.768 kHz ref        100        327   True        99.9024     -975.6
32.768 kHz ref       1000         32   True       992.9697    -7030.3
32.768 kHz ref     10000          2   True     10922.6667    92266.7
AN385 25 MHz: longest possible SysTick period = 671.089 ms
STM32F4 168 MHz: longest possible SysTick period = 99.864 ms
STM32H7 480 MHz: longest possible SysTick period = 34.953 ms
RVR=25000 instead of 24999: -40.0 ppm -> 144 ms lost per hour
```

출력에서 볼 것: MHz급 코어 클럭은 흔한 tick rate로 딱 나누어떨어진다. 32.768 kHz 같은 저전력 클럭은 1 kHz로 나누어떨어지지 않아 7000 ppm(1시간에 25초)이나 틀린다. 그래서 저전력 타이머를 tick 원천으로 쓰는 RTOS는 tick rate를 1024 Hz나 32768 Hz처럼 2의 거듭제곱으로 잡는 경우가 많다(C6). 그리고 **RVR 하나 차이(off-by-one)가 40 ppm**, 1시간에 144 ms다. 수정 발진기 오차(보통 수십 ppm)와 같은 크기라서 현장에서는 "크리스털이 나쁘다"로 오진되기 쉽다. 가장 긴 SysTick 주기는 480 MHz에서 35 ms뿐이라서, tickless 커널은 오래 자려면 SysTick을 여러 번 이어 붙이거나 다른 타이머를 쓴다(C6).

### 1.4 아래로 세기 · COUNTFLAG · sub-tick 타임스탬프

tick 카운터는 ms 해상도다. 그런데 RTOS 안에서도 "지금이 tick 안의 어디쯤인가"를 알고 싶을 때가 많다. 센서 데이터에 µs 타임스탬프를 붙이거나, ISR 처리 시간을 재거나, tickless에서 몇 count를 잤는지 보정할 때다. 답은 CVR이다. 카운터는 RVR에서 0으로 내려가므로 **"이번 tick이 시작된 뒤 흐른 count = RVR − CVR"** 이다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<line x1="70" y1="30" x2="70" y2="190" stroke="currentColor"/>
<line x1="70" y1="190" x2="660" y2="190" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="62" y="45" font-size="12" text-anchor="end">RVR</text>
<text x="62" y="185" font-size="12" text-anchor="end">0</text>
<text x="640" y="210" font-size="12" text-anchor="end">시간</text>
<polyline points="80,40 250,180 250,40 420,180 420,40 590,180 590,40 630,73" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<line x1="250" y1="30" x2="250" y2="190" stroke="#d0564a" stroke-dasharray="4 3"/>
<line x1="420" y1="30" x2="420" y2="190" stroke="#d0564a" stroke-dasharray="4 3"/>
<line x1="590" y1="30" x2="590" y2="190" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="250" y="24" font-size="12" text-anchor="middle">tick N</text>
<text x="420" y="24" font-size="12" text-anchor="middle">tick N+1</text>
<text x="590" y="24" font-size="12" text-anchor="middle">tick N+2</text>
<circle cx="330" cy="106" r="4" fill="#4a7bd0"/>
<line x1="330" y1="106" x2="330" y2="40" stroke="#4a7bd0" stroke-dasharray="2 2"/>
<line x1="252" y1="120" x2="328" y2="120" stroke="#4a7bd0" marker-end="url(#ah3)"/>
<text x="340" y="98" font-size="12">지금 CVR</text>
<text x="290" y="138" font-size="12" text-anchor="middle">RVR − CVR</text>
<text x="70" y="232" font-size="13">now = ticks × (RVR + 1) + (RVR − CVR)</text>
<text x="70" y="254" font-size="12">빨간 점선 = 1 → 0 (COUNTFLAG · SysTick pending) · 다음 클럭에 RVR 재적재</text>
</svg>
```

그림 3 — SysTick 카운터는 톱니 모양으로 내려간다. tick 카운터(ISR이 올림)와 CVR을 합치면 클럭 단위 타임스탬프가 된다. 문제는 두 값을 **동시에** 읽을 수 없다는 것이다.

함정이 하나 있다. `ticks`를 읽고 나서 CVR을 읽는 사이에 카운터가 0을 지나 ISR이 `ticks`를 올리면, 옛 `ticks` + 새 CVR(RVR 근처, 즉 "이번 tick 시작")이 합쳐져 **시간이 거의 1 tick 거꾸로 간다**. Don이 SSD에서 64비트 타임스탬프를 32비트 두 번 읽기로 만들 때 겪는 "찢어진 읽기(torn read)"와 같은 문제다.

### 예제 3 — 순진한 읽기는 1 ms 거꾸로 간다

tick ISR이 도는 동안 200 ms 내내 타임스탬프를 읽고, 이전 값보다 작아진 횟수를 센다. 두 함수의 차이는 "ticks를 두 번 읽어 같을 때만 쓰는가"뿐이다.

```c
/* ex3: tick 카운터 + CVR로 만든 sub-tick 타임스탬프 — 순진한 읽기의 역행 vs 안전한 읽기 */
#include "../b4.h"
#define RELOAD 25000u                     /* 1 ms = 25000 count, RVR = RELOAD-1 */
static volatile uint32_t g_ticks;
void SysTick_Handler(void) { g_ticks++; }

static uint64_t now_naive(void)           /* 틀린 버전: ticks와 CVR 사이에 wrap이 끼면 역행 */
{
    uint32_t t = g_ticks;
    __asm volatile("nop; nop; nop; nop"); /* 실제 코드의 다른 연산 자리(창을 넓혀 재현을 쉽게) */
    uint32_t c = SYST_CVR;
    return (uint64_t)t * RELOAD + (RELOAD - 1u - c);
}

static uint64_t now_safe(void)            /* ticks를 두 번 읽어 같을 때만 쓴다 */
{
    uint32_t t, c;
    do {
        t = g_ticks;
        __asm volatile("nop; nop; nop; nop");
        c = SYST_CVR;
    } while (t != g_ticks);
    return (uint64_t)t * RELOAD + (RELOAD - 1u - c);
}

static void test(const char *name, uint64_t (*fn)(void))
{
    uint32_t back = 0, worst = 0;
    uint64_t prev = fn();
    uint32_t start = g_ticks;
    while (g_ticks - start < 200u) {      /* 200 ms 동안 계속 읽는다 */
        uint64_t t = fn();
        if (t < prev) { back++; if ((uint32_t)(prev - t) > worst) worst = (uint32_t)(prev - t); }
        prev = t;
    }
    board_printf_raw("%s: went backwards %u times, worst %u counts (%u us)\n",
                     name, back, worst, worst / 25u);
}

int main(void)
{
    SYST_RVR = RELOAD - 1u; SYST_CVR = 0;
    SYST_CSR = CSR_ENABLE | CSR_TICKINT | CSR_CLKSOURCE;
    test("naive", now_naive);
    test("safe ", now_safe);
    uint64_t a = now_safe(); uint32_t c0 = cyc();
    while (cyc() - c0 < 1234u) { }
    uint64_t b = now_safe();
    board_printf_raw("1234-cycle busy wait measured by SysTick timestamp: %u counts\n", (uint32_t)(b - a));
    return 0;
}
```

```text
naive: went backwards 47 times, worst 24976 counts (999 us)
safe : went backwards 0 times, worst 0 counts (0 us)
1234-cycle busy wait measured by SysTick timestamp: 1256 counts
[board_exit] code=0
```

출력에서 볼 것: 200번의 tick 중 47번, 순진한 읽기가 거의 1 tick(999 µs) 거꾸로 갔다. 두 번 읽기로 고치면 0번이다. 그리고 이 타임스탬프로 1234 cycle 바쁜 대기를 재면 1256 count — 함수 호출 오버헤드 22 count를 빼면 클럭 단위로 정확하다.

**인터럽트가 막혀 있을 때는 이것도 부족하다.** 크리티컬 섹션 안이라 ISR이 아직 `ticks`를 못 올렸는데 카운터는 이미 0을 지났을 수 있다. 이때는 COUNTFLAG를 같이 봐야 한다("CVR을 읽은 뒤 COUNTFLAG가 1이면 tick 하나를 더하고 CVR을 다시 읽는다"). Zephyr의 SysTick 드라이버(`drivers/timer/cortex_m_systick.c`)가 `elapsed()` 함수에서 정확히 이 일을 한다. 단, COUNTFLAG는 **읽으면 지워지는** 깃발이라 여러 곳에서 읽으면 서로 깃발을 훔친다. 그래서 Zephyr는 `elapsed()` 한 함수만 CSR을 읽고, 본 결과를 `overflow_cyc`에 쌓아 둔다. 이 설계는 C5 · C6에서 다시 본다.

### 1.5 reload의 지터와 드리프트 — 자동 reload가 왜 중요한가

tick ISR은 늘 정시에 들어가지 못한다. 더 높은 ISR이 돌고 있거나, main이 인터럽트를 막은 크리티컬 섹션 안이면 기다린다. 이 늦음이 **지터(jitter)** 다. 지터가 있으면 각 tick의 "ISR 진입 시각"은 흔들린다. 그런데 **tick의 평균 주기까지 흔들리면(드리프트, drift) 시계가 틀어진다.** SysTick은 하드웨어가 0 다음 클럭에 RVR을 다시 싣기 때문에, ISR이 늦게 들어가도 다음 주기의 시작점은 밀리지 않는다.

옛날 타이머 코드에서 흔한 실수는 ISR 안에서 카운터를 소프트웨어로 다시 시작하는 것이다("ISR이 들어오면 타이머를 다시 로드"). 그러면 **ISR 진입 지연만큼 매 주기가 길어지고 그게 쌓인다.**

### 예제 2 — 세 가지 tick의 지터와 누적 드리프트

main이 0~8 µs 길이의 크리티컬 섹션을 무작위로 반복하는(= tick ISR을 무작위로 늦추는) 상황에서 세 가지 tick을 500번씩 돌리고, ISR 진입 시각의 주기 최소/최대와 500 tick 뒤 누적 드리프트를 잰다. ISR 안에서 `RVR − CVR`을 읽으면 "0을 지난 뒤 몇 count 만에 들어왔나" = 진입 지연이 바로 나온다(SysTick만의 장점).

```c
/* ex2: 1 kHz tick 세 가지 — 올바른 reload, off-by-one, ISR에서 재시작. 지터와 누적 드리프트 */
#include "../b4.h"
#define N 500
static volatile uint32_t ts[N + 1], lat_max, n;
static volatile int mode;                 /* 0: RVR=24999, 1: RVR=25000, 2: ISR에서 CVR 재시작 */

void SysTick_Handler(void)
{
    uint32_t now = cyc();
    uint32_t lat = SYST_RVR - SYST_CVR;   /* wrap 이후 흐른 SysTick 카운트 = 진입 지연 */
    if (mode == 2) SYST_CVR = 0;          /* 나쁜 습관: 소프트웨어로 다시 시작 */
    if (lat > lat_max) lat_max = lat;
    if (n <= N) ts[n++] = now;
}

static void run(int m, uint32_t rvr)
{
    mode = m; n = 0; lat_max = 0;
    SYST_CSR = 0; SYST_RVR = rvr; SYST_CVR = 0;
    SYST_CSR = CSR_ENABLE | CSR_TICKINT | CSR_CLKSOURCE;
    uint32_t seed = 12345;
    while (n <= N) {                      /* 바쁜 main: 0~8 us 길이의 크리티컬 섹션 */
        seed = seed * 1103515245u + 12345u;
        uint32_t len = (seed >> 16) % 200u;           /* 0~199 cycle(약 8 us) */
        __asm volatile("cpsid i");
        uint32_t t0 = cyc(); while (cyc() - t0 < len) { }
        __asm volatile("cpsie i");
    }
    SYST_CSR = 0;
    uint32_t pmin = ~0u, pmax = 0;
    for (int i = 1; i <= N; i++) {
        uint32_t p = ts[i] - ts[i - 1];
        if (p < pmin) pmin = p;
        if (p > pmax) pmax = p;
    }
    int32_t drift = (int32_t)(ts[N] - ts[0]) - (int32_t)(N * 25000u);
    board_printf_raw("mode %d RVR=%u: period min=%u max=%u cyc, entry lat max=%u, drift after %u ticks = %d cyc (%d us)\n",
                     m, rvr, pmin, pmax, lat_max, N, drift, drift / 25);
}

int main(void)
{
    run(0, 25000u - 1u);
    run(1, 25000u);
    run(2, 25000u - 1u);
    return 0;
}
```

```text
mode 0 RVR=24999: period min=24832 max=25171 cyc, entry lat max=205, drift after 500 ticks = -84 cyc (-3 us)
mode 1 RVR=25000: period min=24816 max=25196 cyc, entry lat max=205, drift after 500 ticks = 478 cyc (19 us)
mode 2 RVR=24999: period min=25010 max=25212 cyc, entry lat max=205, drift after 500 ticks = 35412 cyc (1416 us)
[board_exit] code=0
```

출력에서 볼 것: 세 경우 모두 진입 지연은 최대 205 count(8 µs, 크리티컬 섹션 길이)로 같다. 그래서 각 주기는 ±7 µs쯤 흔들린다(지터). 하지만 누적 드리프트는 전혀 다르다.

- **mode 0 (RVR = 24999)**: 500 tick 뒤 −3 µs. 처음 tick과 마지막 tick의 진입 지연 차이일 뿐, **쌓이지 않는다**.
- **mode 1 (RVR = 25000)**: +19 µs. tick마다 정확히 1 cycle(40 ns)씩 늘어난다. 예제 15의 40 ppm이다.
- **mode 2 (ISR에서 CVR = 0)**: +1416 µs. 매 주기가 "진입 지연 + 처리"만큼 길어지고 그게 쌓인다. 0.5초에 1.4 ms, 1시간이면 10초 넘게 틀린다. 그리고 주기 최소값이 25010 — 절대 25000보다 짧아지지 않는다(한쪽으로만 밀린다).

```svg
<svg viewBox="0 0 680 200" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="43" font-size="13">RVR = 24999 (올바름)</text>
<rect x="230" y="28" width="2.0" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="238.0" y="44" font-size="12">-3 µs</text>
<text x="10" y="88" font-size="13">RVR = 25000 (off-by-one)</text>
<rect x="230" y="73" width="4.7" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="240.7" y="89" font-size="12">19 µs</text>
<text x="10" y="133" font-size="13">ISR에서 CVR=0 재시작</text>
<rect x="230" y="118" width="349.3" height="22" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<text x="585.3" y="134" font-size="12">1416 µs</text>
<line x1="230" y1="20" x2="230" y2="160" stroke="currentColor"/>
<text x="10" y="185" font-size="12">500 tick(0.5 s) 뒤 누적 드리프트, 같은 바쁜 main(0~8 µs 크리티컬 섹션) — 예제 2 실측</text>
</svg>
```

그림 4 — 예제 2의 측정값. 지터(각 tick의 흔들림)는 셋이 같지만, 드리프트(쌓이는 오차)는 reload 방식이 정한다. 하드웨어 자동 reload + 정확한 RVR이면 드리프트가 0으로 수렴한다.

### 1.6 SysTick으로 µs 정확한 지연 만들기

bring-up 코드에는 "센서 리셋 후 10 µs 기다림", "EEPROM 쓰기 후 5 ms" 같은 짧은 지연이 많다. 흔한 방법 두 가지는 둘 다 문제가 있다.

- **NOP 루프** — 컴파일러 최적화(-O0과 -O2), 플래시 wait state, 캐시에 따라 길이가 몇 배 달라진다.
- **SysTick을 지연용으로 다시 설정** — RTOS(또는 HAL)가 tick으로 쓰고 있는 SysTick을 건드리면 tick이 망가진다.

정답은 **돌고 있는 SysTick을 읽기만 하는 것**이다. CVR이 몇 count 줄었는지 누적하되, 0을 지나 RVR로 돌아간 경우(wrap)를 **주기(RVR + 1) 기준으로** 처리한다. 이 방식은 tick ISR에 전혀 영향을 주지 않고, 지연 도중에 인터럽트가 들어와도(지연이 길어질 뿐) 짧아지지는 않는다.

### 예제 4 — 1 kHz tick을 건드리지 않는 `delay_us()`

1 µs부터 2.5 ms까지 지연을 요청하고 FPGAIO 카운터로 실제 길이를 잰다. 비교용으로 흔한 버그 버전(24비트 마스크로 wrap 처리)도 시작 위상을 바꿔 가며 돌린다.

```c
/* ex4: 돌고 있는 1 kHz SysTick을 건드리지 않고 us 단위 정확한 지연 만들기 */
#include "../b4.h"
#define RELOAD 25000u                     /* RVR+1 = 한 주기 count 수 */
#define CNT_PER_US 25u                    /* CLKSOURCE=1, 25 MHz */
static volatile uint32_t g_ticks;
void SysTick_Handler(void) { g_ticks++; }

static void delay_us(uint32_t us)          /* 올바른 버전: 주기(RELOAD) 기준으로 wrap 처리 */
{
    uint32_t need = us * CNT_PER_US, done = 0, prev = SYST_CVR;
    while (done < need) {
        uint32_t cur = SYST_CVR;
        done += (prev >= cur) ? (prev - cur) : (prev + RELOAD - cur);  /* 아래로 센다 */
        prev = cur;
    }
}

static void delay_us_wrong(uint32_t us)    /* 흔한 실수: 24비트 마스크로 wrap 처리 */
{
    uint32_t need = us * CNT_PER_US, start = SYST_CVR;
    while (((start - SYST_CVR) & 0xFFFFFFu) < need) { }
}

static void measure(const char *name, void (*fn)(uint32_t), uint32_t us)
{
    uint32_t t0 = cyc();
    fn(us);
    uint32_t dt = cyc() - t0;
    int32_t err = (int32_t)dt - (int32_t)(us * 25u);
    board_printf_raw("%s %u us: measured %u cyc = %u.%u us (err %d cyc)\n", name, us, dt,
                     dt / 25u, (dt % 25u) * 10u / 25u, err);
}

int main(void)
{
    SYST_RVR = RELOAD - 1u; SYST_CVR = 0;
    SYST_CSR = CSR_ENABLE | CSR_TICKINT | CSR_CLKSOURCE;
    static const uint32_t req[] = {1, 2, 5, 10, 100, 999, 1000, 2500};
    for (unsigned i = 0; i < sizeof req / sizeof req[0]; i++) measure("good ", delay_us, req[i]);
    for (int k = 0; k < 3; k++) {         /* 시작 위상만 바꿔 가며 잘못된 버전 */
        uint32_t t = g_ticks; while (g_ticks == t) { }      /* tick 직후로 맞춤 */
        uint32_t c0 = cyc(); while (cyc() - c0 < 8000u * (uint32_t)k) { }
        measure("wrong", delay_us_wrong, 500);
    }
    board_printf_raw("ticks kept running: %u\n", g_ticks);
    return 0;
}
```

```text
good  1 us: measured 38 cyc = 1.5 us (err 13 cyc)
good  2 us: measured 61 cyc = 2.4 us (err 11 cyc)
good  5 us: measured 142 cyc = 5.6 us (err 17 cyc)
good  10 us: measured 261 cyc = 10.4 us (err 11 cyc)
good  100 us: measured 2509 cyc = 100.3 us (err 9 cyc)
good  999 us: measured 24986 cyc = 999.4 us (err 11 cyc)
good  1000 us: measured 25010 cyc = 1000.4 us (err 10 cyc)
good  2500 us: measured 62510 cyc = 2500.4 us (err 10 cyc)
wrong 500 us: measured 12506 cyc = 500.2 us (err 6 cyc)
wrong 500 us: measured 12506 cyc = 500.2 us (err 6 cyc)
wrong 500 us: measured 8997 cyc = 359.8 us (err -3503 cyc)
ticks kept running: 8
[board_exit] code=0
```

출력에서 볼 것: 올바른 버전은 1 µs에서 2.5 ms까지 **오차가 늘 +9~17 cycle**(함수 호출과 측정 자체의 비용, 0.4~0.7 µs)로 일정하고, tick 경계를 여러 번 넘는 2.5 ms도 정확하다. 지연 동안 tick ISR은 계속 돌았다(`ticks kept running: 8`). 잘못된 버전은 지연 구간이 tick 경계를 **넘지 않을 때는** 맞다가(처음 두 번), 넘는 순간(세 번째, 시작 위상을 0.64 ms 늦춤) **140 µs 일찍 끝난다**. RVR이 2^24 − 1이 아닌데 24비트 마스크로 wrap을 처리했기 때문이다: wrap 직후 `start − CVR`이 음수가 되어 마스크하면 거대한 값이 되고, 루프가 바로 빠져나온다. 이 버그는 지연이 짧고 tick이 느리면 거의 안 보이다가 가끔만 터진다 — 센서 리셋 타이밍이 가끔 틀리는 종류의 버그다.

몇 가지 덧붙인다.

- **지연은 "최소" 보장이다.** 지연 도중 ISR이 돌면 그만큼 길어진다. "정확히 10 µs"가 필요하면(예: 비트뱅잉 프로토콜) 인터럽트를 막거나 HW 타이머의 출력 비교를 쓴다(E5).
- **RTOS 안에서 ms 단위 지연은 `vTaskDelay()` / `k_sleep()`이다.** 바쁜 대기는 CPU를 다른 태스크에 주지 않는다. µs 바쁜 대기는 "tick 하나보다 훨씬 짧을 때만" 쓴다. Zephyr의 `k_busy_wait()`이 바로 이 용도이고, Cortex-M SysTick 드라이버에서는 시스템 시계(`k_cycle_get_32()`)를 읽어 기다린다. Linux의 `udelay()` vs `msleep()` 구분과 같은 생각이다(EL F8).
- **`us × 25`의 오버플로**: 32비트로 약 171초까지만 안전하다. 긴 지연은 tick 기반 API로 간다.

### 1.7 DWT CYCCNT — 진짜 사이클 카운터 (QEMU에는 없다)

SysTick은 24비트이고 tick용으로 이미 쓰고 있다. 측정 전용으로는 **DWT(Data Watchpoint and Trace) 유닛의 CYCCNT**가 표준이다. CYCCNT는 코어 클럭마다 1씩 **올라가는** 32비트 카운터다. 25 MHz에서 171.8초, 480 MHz에서 8.9초마다 wrap한다. 함수 하나, ISR 하나의 사이클 수를 잴 때(M6 · E3) 쓴다.

켜는 순서가 정해져 있다.

1. `DEMCR`(Debug Exception and Monitor Control Register, `0xE000EDFC`)의 **bit 24 TRCENA**를 1로 — DWT · ITM 블록 자체에 전원/클럭을 준다. 이걸 안 하면 DWT 레지스터에 써도 아무 일도 안 일어난다.
2. (Cortex-M7 등 CoreSight 잠금이 있는 코어만) `DWT_LAR`(`0xE0001FB0`)에 `0xC5ACCE55`를 써서 잠금 해제. 잠금이 없는 코어에서는 무시된다.
3. `DWT_CTRL`(`0xE0001000`)의 **bit 25 NOCYCCNT**가 0인지 확인 — 1이면 CYCCNT가 구현되지 않은 칩이다.
4. `DWT_CYCCNT`(`0xE0001004`)에 0을 쓰고, `DWT_CTRL`의 **bit 0 CYCCNTENA**를 1로.

ARMv6-M(Cortex-M0/M0+)의 DWT에는 CYCCNT가 아예 없다. 그 칩에서는 SysTick이나 HW 타이머로 잰다.

### 예제 5 — DWT CYCCNT 켜기 (실칩용 코드 · **빌드만 함 (QEMU 미지원)**)

아래 코드는 실칩(Cortex-M3/M4/M7/M33)에서 쓰는 표준 순서다. QEMU는 DWT를 모델링하지 않으므로 측정에 쓸 수 없다. 대신 "QEMU에서 돌리면 무엇이 보이는지"를 확인해서, **레지스터가 0으로 읽히면 NOCYCCNT 검사도 통과해 버린다**는 함정을 보여 준다.

```c
/* ex5: DWT CYCCNT 켜는 표준 순서 — 실칩용. QEMU에서는 무엇이 보이는지 확인 */
#include "../b4.h"
#define DEMCR      REG32(0xE000EDFCu)     /* Debug Exception and Monitor Control */
#define DEMCR_TRCENA (1u << 24)           /* DWT · ITM 전원/클럭 켜기 */
#define DWT_CTRL   REG32(0xE0001000u)
#define DWT_CYCCNT REG32(0xE0001004u)
#define DWT_LAR    REG32(0xE0001FB0u)     /* Cortex-M7 등: 소프트웨어 잠금 해제 */
#define DWT_CTRL_NOCYCCNT (1u << 25)      /* 1이면 CYCCNT 미구현 */
#define DWT_CTRL_CYCCNTENA (1u << 0)

static int dwt_init(void)
{
    DEMCR |= DEMCR_TRCENA;                /* 1) 먼저 trace 블록을 켠다 */
    DWT_LAR = 0xC5ACCE55u;                /* 2) 잠금이 있는 코어(M7)만 의미, 나머지는 무시됨 */
    if (DWT_CTRL & DWT_CTRL_NOCYCCNT) return -1;
    DWT_CYCCNT = 0;                       /* 3) 0부터 */
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;       /* 4) 카운트 시작 */
    return 0;
}

int main(void)
{
    int r = dwt_init();
    board_printf_raw("dwt_init=%d DEMCR=0x%x DWT_CTRL=0x%x NUMCOMP=%u\n",
                     r, DEMCR, DWT_CTRL, DWT_CTRL >> 28);
    uint32_t a = DWT_CYCCNT, fa = cyc();
    for (volatile int i = 0; i < 1000; i++) { }
    uint32_t b = DWT_CYCCNT, fb = cyc();
    board_printf_raw("1000-iteration loop: DWT_CYCCNT delta=%u, FPGAIO COUNTER delta=%u\n", b - a, fb - fa);
    return 0;
}
```

```text
dwt_init=0 DEMCR=0x0 DWT_CTRL=0x0 NUMCOMP=0
1000-iteration loop: DWT_CYCCNT delta=0, FPGAIO COUNTER delta=4806
[board_exit] code=0
```

출력에서 볼 것: QEMU에서는 DEMCR에 TRCENA를 써도 0으로 읽히고, DWT_CTRL도 0이다. NOCYCCNT 비트가 0이니 `dwt_init()`은 "성공"을 돌려주지만 CYCCNT는 영원히 0이다. 같은 루프를 FPGAIO COUNTER는 4806 cycle로 쟀다. **교훈: CYCCNT를 켠 뒤에는 "실제로 증가하는지" 한 번 읽어 확인하라.** 실칩에서도 디버그 도메인 전원이 꺼져 있거나 보안 설정(TrustZone에서 비보안 측 접근 제한)으로 같은 증상이 나올 수 있다. 실칩에서 기대하는 출력은 `DWT_CTRL`의 NUMCOMP(상위 4비트)가 비교기 개수(예: 4)이고, CYCCNT delta가 FPGAIO처럼 수천 cycle인 것이다 — 이 기대값은 **QEMU에서 확인할 수 없어서 실행 결과로 보여 주지 못한다.**

이 노트의 나머지 측정은 실행 가능한 대체물 **FPGAIO COUNTER(`0x40028018`)** 를 쓴다. MPS2 보드의 FPGA 레지스터 블록에 있는, 리셋부터 25 MHz로 증가하는 32비트 카운터다. 용도는 CYCCNT와 같다(빼기 한 번으로 cycle 수). 실칩으로 옮길 때는 `cyc()` 한 함수만 `DWT_CYCCNT`로 바꾸면 된다.

| 시계 | 폭 | 방향 | 해상도 | 쓰임 | 주의 |
|---|---|---|---|---|---|
| SysTick CVR | 24비트 | 아래로 | 코어 클럭(또는 ref) | tick + sub-tick 타임스탬프 | RTOS가 이미 쓰고 있다. 읽기만 |
| DWT CYCCNT | 32비트 | 위로 | 코어 클럭 | 사이클 측정 | M0/M0+에는 없음, TRCENA 필요, QEMU 미지원 |
| HW 타이머 (TIMx) | 16/32비트 | 위로(보통) | 프리스케일러 | 입력 캡처 · 장기 타임스탬프 | 칩마다 다름 (E5) |
| FPGAIO COUNTER | 32비트 | 위로 | 25 MHz | 이 노트의 QEMU 측정 | MPS2 보드 전용 |

### 1.8 SysTick 함정 모음

- **CLKSOURCE를 잘못 고름** — 참조 클럭(예: 코어 클럭 ÷ 8)을 쓰면서 RVR을 코어 클럭으로 계산하면 tick이 8배 느리다. FreeRTOS는 `configSYSTICK_CLOCK_HZ`를 정의하면 CLKSOURCE = 0(참조 클럭)을 쓰고, 정의하지 않으면 `configCPU_CLOCK_HZ`와 CLKSOURCE = 1을 쓴다(`port.c`의 `portNVIC_SYSTICK_CLK_BIT_CONFIG`).
- **HAL과 RTOS가 SysTick을 나눠 씀** — STM32 HAL은 `HAL_Delay()`의 시간 기반으로 SysTick을 쓴다(`HAL_IncTick()`). FreeRTOS도 SysTick을 쓰면 둘이 핸들러를 다툰다. STM32CubeMX는 FreeRTOS를 켜면 HAL 시간 기반을 TIM6 같은 다른 타이머로 바꾸라고 권한다.
- **저전력 모드에서 멈춤** — 깊은 sleep(예: STM32 Stop 모드)에서는 코어 클럭이 꺼져 SysTick도 선다. 그 동안 tick이 없으니 시간이 멈춘다. tickless + 저전력 타이머(LPTIM · RTC)로 보정한다(C6).
- **디버거로 멈춘 동안** — 코어가 디버그 halt 상태면 SysTick도 세지 않는다(Armv7-M 규칙). 브레이크포인트에서 오래 멈춰도 tick은 그만큼 안 흐른다. 반면 칩 회사 타이머는 디버그 halt에서 계속 도는 것도 있어서(설정 비트가 따로 있다) 두 시계가 어긋나 보일 수 있다.
- **RVR을 실행 중에 바꿈** — 새 RVR은 **다음 wrap 때** 적용된다. 지금 주기를 바로 바꾸려면 CVR에 써서 0으로 만든다(그 순간 주기 하나가 짧아진다). tickless 코드가 이 처리를 조심스럽게 한다(`port.c`의 `vPortSuppressTicksAndSleep()`).
- **24비트 한계** — 고클럭에서 100 Hz 이하 tick은 안 들어갈 수 있다(480 MHz에서 최대 35 ms).

---

## 2. PendSV — 가장 낮은 우선순위로 미뤄 둔 소프트웨어 인터럽트

### 2.1 직관 — "깃발만 세우고, 일은 다들 끝난 뒤에"

RTOS의 컨텍스트 스위치는 "지금 돌던 태스크의 레지스터를 그 태스크 스택에 저장하고, 다음 태스크의 스택에서 레지스터를 꺼낸 뒤, 그 태스크로 돌아가는" 일이다(C2). 이 일을 **언제** 해야 할까?

전환이 필요해지는 순간은 여러 곳에서 생긴다. UART 수신 ISR이 큐에 바이트를 넣어 높은 우선순위 태스크를 깨웠을 때, SysTick이 "10 ms 잔 태스크의 시간이 됐다"고 판단했을 때, 태스크가 스스로 세마포어를 기다리며 블록할 때. 그런데 ISR 안에서 바로 전환하면 문제가 생긴다.

- **ISR이 중첩돼 있을 수 있다.** 낮은 우선순위 ISR이 돌다가 높은 ISR에 선점당했다고 하자. 높은 ISR에서 태스크를 바꾸면, 아래에 깔린 낮은 ISR의 남은 절반은 **다른 태스크 위에서** 돌아오게 된다(ISR의 프레임이 어느 스택에 있느냐에 따라 아예 망가진다). "ISR 안에서는 전환하지 않는다, Thread 모드로 돌아갈 때만 전환한다"가 규칙이어야 한다.
- **요청이 여러 번 온다.** 한 번의 인터럽트 폭주 동안 ISR 세 개가 각자 "전환해!"라고 할 수 있다. 전환을 세 번 할 필요는 없다. 마지막에 "지금 가장 높은 ready 태스크"로 **한 번만** 바꾸면 된다.
- **언제 끝날지 모른다.** 높은 ISR은 자기 일이 끝나는 시점에, 아래에 무엇이 깔려 있는지 모른다. "모든 ISR이 끝나는 순간"을 소프트웨어로 알아내기는 어렵다.

Cortex-M은 이 세 문제를 **예외 하나로** 풀었다. PendSV를 **가장 낮은 우선순위**로 두고, 전환이 필요하면 `ICSR.PENDSVSET` 비트만 쓴다. 그러면 하드웨어가 알아서 "더 높은 일(모든 ISR)이 다 끝나고, Thread 모드로 돌아가기 직전"에 PendSV를 한 번 실행한다. NVIC의 우선순위 규칙이 곧 "모든 ISR이 끝났나?"의 답이다.

Don의 SSD 실행부에 대응시키면 이렇다: ISR이 세우는 "deferred work" 비트 = `PENDSVSET`, main 루프가 비트를 보고 일을 하는 지점 = PendSV 핸들러, main 루프는 ISR이 다 끝나야 돈다 = PendSV는 최저 우선순위. 차이는 main 루프의 "다음 바퀴"를 기다리지 않고 **마지막 ISR에서 바로 이어서(tail-chaining)** 돈다는 것이다.

### 2.2 정의

- **PendSV**(pendable service call)는 예외 번호 **14**인 시스템 예외다. 하드웨어 사건이 아니라 **소프트웨어가 pending 비트를 써서** 일으킨다.
- 요청: `SCB->ICSR`(`0xE000ED04`)의 **bit 28 PENDSVSET**에 1을 쓴다. 취소: **bit 27 PENDSVCLR**에 1. 읽으면 bit 28이 "지금 pending인가"를 보여 준다. (같은 레지스터의 bit 26 PENDSTSET / bit 25 PENDSTCLR은 SysTick을 소프트웨어로 pending/취소한다.)
- 우선순위: `SHPR3`(`0xE000ED20`)의 **[23:16]** 바이트. **리셋 값은 0 = 설정 가능한 우선순위 중 가장 높다.** RTOS가 시작할 때 반드시 최저(0xFF)로 바꿔야 하는 이유다(§2.5).
- 성질: **비동기**다. pending만 되고, 현재 실행 우선순위보다 높아질 때(= 더 높은 핸들러가 다 끝나고 PRIMASK/BASEPRI가 허락할 때) 들어간다. pending 비트는 **카운터가 아니라 1비트**라서 여러 번 써도 한 번 실행된다.
- SVC와의 차이: SVC는 동기(지금 당장 들어가지 못하면 HardFault, §3.3), PendSV는 비동기(기다린다). "pendable"이라는 이름이 이 차이다.

### 예제 6 — 높은 ISR과 SysTick 안에서 요청한 PendSV는 언제 도나

Thread에서 "센서 IRQ"(TIMER0, IRQ 8, 우선순위 0x40)를 소프트웨어로 일으킨다. 그 ISR이 PENDSVSET을 쓰고, 마침 tick도 도래한 것처럼 SysTick(0x80)도 pending 시킨 뒤 10 µs 더 일한다. SysTick 핸들러도 PENDSVSET을 한 번 더 쓴다. 각 지점에서 FPGAIO 시각과 IPSR(지금 무슨 예외 안인가)을 기록한다.

```c
/* ex6: 높은 우선순위 ISR과 SysTick 안에서 PendSV를 요청하면 실제로 언제 도는가 */
#include "../b4.h"
#define NVIC_ISPR0 REG32(0xE000E200u)     /* 쓰기로 IRQ를 pending 시킨다 */
static volatile uint32_t t0, nlog, pendsv_runs, pendsv_lr;
static struct { const char *what; uint32_t t; uint32_t ipsr; } lg[16];
static void mark(const char *w) { lg[nlog].what = w; lg[nlog].t = cyc() - t0; lg[nlog].ipsr = get_ipsr(); nlog++; }
static void spin_us(uint32_t us) { uint32_t s = cyc(); while (cyc() - s < us * 25u) { } }

void TIMER0_IRQHandler(void)              /* IRQ 8, 우선순위 0x40: "센서 데이터 도착" ISR */
{
    mark("TIMER0 ISR enter");
    SCB_ICSR = ICSR_PENDSVSET;            /* 태스크를 깨웠다고 치고 전환 요청 */
    mark("TIMER0 set PENDSVSET");
    SCB_ICSR = ICSR_PENDSTSET;            /* 마침 tick도 도래했다고 치자 (SysTick pending) */
    spin_us(10);                          /* ISR 일의 나머지 */
    mark("TIMER0 ISR exit");
}

void SysTick_Handler(void)                /* 우선순위 0x80 */
{
    mark("SysTick enter");
    SCB_ICSR = ICSR_PENDSVSET;            /* tick 처리 결과 또 전환 필요 */
    spin_us(5);
    mark("SysTick exit");
}

void PendSV_Handler(void)                 /* 우선순위 0xFF(최저) */
{
    pendsv_lr = (uint32_t)__builtin_return_address(0);  /* = EXC_RETURN */
    mark("PendSV runs");
    pendsv_runs++;
}

int main(void)
{
    SCB_SHPR3 = (0x80u << 24) | (0xFFu << 16);           /* SysTick 0x80, PendSV 0xFF */
    NVIC_IPR(8) = 0x40;
    NVIC_ISER0 = 1u << 8;
    t0 = cyc();
    mark("thread: pend IRQ8");
    NVIC_ISPR0 = 1u << 8;                 /* 센서 IRQ 발생 */
    mark("thread resumes");
    for (uint32_t i = 0; i < nlog; i++)
        board_printf_raw("%u.%u us  IPSR=%u  %s\n", lg[i].t / 25u, (lg[i].t % 25u) * 10u / 25u,
                         lg[i].ipsr, lg[i].what);
    board_printf_raw("PENDSVSET written 2 times -> PendSV ran %u time(s), EXC_RETURN=0x%x\n",
                     pendsv_runs, pendsv_lr);
    return 0;
}
```

```text
0.3 us  IPSR=0  thread: pend IRQ8
1.2 us  IPSR=24  TIMER0 ISR enter
1.9 us  IPSR=24  TIMER0 set PENDSVSET
12.7 us  IPSR=24  TIMER0 ISR exit
13.5 us  IPSR=15  SysTick enter
19.4 us  IPSR=15  SysTick exit
20.4 us  IPSR=14  PendSV runs
21.2 us  IPSR=0  thread resumes
PENDSVSET written 2 times -> PendSV ran 1 time(s), EXC_RETURN=0xfffffff9
[board_exit] code=0
```

출력에서 볼 것: IPSR 24 = 예외 16 + IRQ 8(TIMER0), 15 = SysTick, 14 = PendSV, 0 = Thread. PendSV는 1.9 µs에 요청됐지만 **TIMER0 ISR이 끝나고(12.7 µs), 그 뒤 pending이던 SysTick까지 끝난(19.4 µs) 다음, 20.4 µs에** 돌았다. 두 번 요청했지만 **한 번만** 돌았다. PendSV 안에서 본 EXC_RETURN `0xFFFFFFF9`는 "Thread 모드로, MSP를 써서 돌아간다"는 뜻이다 — PendSV는 **ISR 위가 아니라 Thread 바로 위에서** 돈다(EXC_RETURN 값의 의미는 B2). 진짜 RTOS라면 바로 이 자리에서 태스크 스택을 바꾸므로, 21.2 µs의 "thread resumes"는 **새 태스크**가 된다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="59" font-size="13">TIMER0 ISR (0x40)</text>
<line x1="170" y1="68" x2="650" y2="68" stroke="currentColor" stroke-opacity="0.25"/>
<text x="10" y="109" font-size="13">SysTick (0x80)</text>
<line x1="170" y1="118" x2="650" y2="118" stroke="currentColor" stroke-opacity="0.25"/>
<text x="10" y="159" font-size="13">PendSV (0xFF)</text>
<line x1="170" y1="168" x2="650" y2="168" stroke="currentColor" stroke-opacity="0.25"/>
<text x="10" y="209" font-size="13">Thread</text>
<line x1="170" y1="218" x2="650" y2="218" stroke="currentColor" stroke-opacity="0.25"/>
<rect x="196.2" y="44" width="250.9" height="24" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<rect x="464.5" y="94" width="128.7" height="24" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="615.1" y="144" width="17.5" height="24" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="170.0" y="194" width="26.2" height="24" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="632.5" y="194" width="17.5" height="24" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<line x1="211.5" y1="40" x2="211.5" y2="72" stroke="#3f9a6b" stroke-width="2"/>
<line x1="466.7" y1="90" x2="466.7" y2="122" stroke="#3f9a6b" stroke-width="2"/>
<text x="215.5" y="36" font-size="12">PENDSVSET 1.9 µs</text>
<text x="470.7" y="86" font-size="12">PENDSVSET 13.5 µs</text>
<line x1="211.5" y1="72" x2="613.1" y2="150" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
<text x="611.1" y="184" font-size="12" text-anchor="end">PendSV는 20.4 µs에 한 번만</text>
<line x1="170" y1="245" x2="650" y2="245" stroke="currentColor"/>
<line x1="170.0" y1="245" x2="170.0" y2="250" stroke="currentColor"/>
<text x="170.0" y="264" font-size="12" text-anchor="middle">0</text>
<line x1="213.6" y1="245" x2="213.6" y2="250" stroke="currentColor"/>
<text x="213.6" y="264" font-size="12" text-anchor="middle">2</text>
<line x1="257.3" y1="245" x2="257.3" y2="250" stroke="currentColor"/>
<text x="257.3" y="264" font-size="12" text-anchor="middle">4</text>
<line x1="300.9" y1="245" x2="300.9" y2="250" stroke="currentColor"/>
<text x="300.9" y="264" font-size="12" text-anchor="middle">6</text>
<line x1="344.5" y1="245" x2="344.5" y2="250" stroke="currentColor"/>
<text x="344.5" y="264" font-size="12" text-anchor="middle">8</text>
<line x1="388.2" y1="245" x2="388.2" y2="250" stroke="currentColor"/>
<text x="388.2" y="264" font-size="12" text-anchor="middle">10</text>
<line x1="431.8" y1="245" x2="431.8" y2="250" stroke="currentColor"/>
<text x="431.8" y="264" font-size="12" text-anchor="middle">12</text>
<line x1="475.5" y1="245" x2="475.5" y2="250" stroke="currentColor"/>
<text x="475.5" y="264" font-size="12" text-anchor="middle">14</text>
<line x1="519.1" y1="245" x2="519.1" y2="250" stroke="currentColor"/>
<text x="519.1" y="264" font-size="12" text-anchor="middle">16</text>
<line x1="562.7" y1="245" x2="562.7" y2="250" stroke="currentColor"/>
<text x="562.7" y="264" font-size="12" text-anchor="middle">18</text>
<line x1="606.4" y1="245" x2="606.4" y2="250" stroke="currentColor"/>
<text x="606.4" y="264" font-size="12" text-anchor="middle">20</text>
<line x1="650.0" y1="245" x2="650.0" y2="250" stroke="currentColor"/>
<text x="650.0" y="264" font-size="12" text-anchor="middle">22</text>
<text x="410" y="283" font-size="12" text-anchor="middle">시간 (µs, QEMU icount, 예제 6 실측)</text>
<text x="10" y="307" font-size="12">요청은 1.9 µs에 했지만, 더 높은 두 핸들러가 끝나 Thread로 돌아가기 직전에 실행된다</text>
</svg>
```

그림 5 — 예제 6의 측정 타임라인. 초록 세로선 두 개가 PENDSVSET을 쓴 순간이다. PendSV(초록 막대)는 TIMER0과 SysTick이 모두 끝난 뒤 한 번만 돌고, 곧바로 Thread로 돌아간다.

참고로 실칩에서는 TIMER0 → SysTick → PendSV가 **tail-chaining**으로 이어진다. 앞 핸들러가 끝날 때 8워드 프레임을 꺼냈다가 다시 쌓는 일 없이 바로 다음 핸들러로 들어간다(Cortex-M3에서 6 cycle, B2). 그래서 PendSV가 "Thread의 프레임이 이미 스택에 쌓여 있는 상태"에서 시작한다는 점이 중요하다 — 나머지 레지스터(R4~R11)만 저장하면 전환이 끝난다(C2). QEMU는 스태킹 사이클을 모델링하지 않으므로 이 절약은 여기서 잴 수 없다.

### 예제 7 — 요청은 "개수"가 아니라 "깃발 하나"다

세 가지를 확인한다. (1) 인터럽트를 막은 채 Thread에서 PENDSVSET을 5번 쓰기, (2) 우선순위가 다른 ISR 세 개가 동시에 들어와 각자 요청하기, (3) 요청했다가 PENDSVCLR로 취소하기.

```c
/* ex7: PendSV 요청은 "개수"가 아니라 "깃발 하나"다 — 합쳐짐(coalescing)과 취소 */
#include "../b4.h"
#define NVIC_ISPR0 REG32(0xE000E200u)
static volatile uint32_t runs;
static char order[16]; static volatile uint32_t no;
void PendSV_Handler(void) { runs++; order[no++] = 'P'; }
void TIMER0_IRQHandler(void)    { order[no++] = '8';  SCB_ICSR = ICSR_PENDSVSET; }
void TIMER1_IRQHandler(void)    { order[no++] = '9';  SCB_ICSR = ICSR_PENDSVSET; }
void DUALTIMER_IRQHandler(void) { order[no++] = 'A';  SCB_ICSR = ICSR_PENDSVSET; }

int main(void)
{
    SCB_SHPR3 = 0xFFu << 16;              /* PendSV 최저 */
    NVIC_IPR(8) = 0x40; NVIC_IPR(9) = 0x60; NVIC_IPR(10) = 0x80;
    NVIC_ISER0 = (1u << 8) | (1u << 9) | (1u << 10);

    /* 1) 스레드에서 인터럽트를 막은 채 5번 요청 */
    __asm volatile("cpsid i");
    for (int i = 0; i < 5; i++) SCB_ICSR = ICSR_PENDSVSET;
    board_printf_raw("[1] ICSR.PENDSVSET reads %u while masked\n", (SCB_ICSR >> 28) & 1u);
    __asm volatile("cpsie i");
    __asm volatile("isb");
    board_printf_raw("[1] 5 requests -> PendSV ran %u time(s)\n", runs);

    /* 2) 우선순위가 다른 ISR 세 개가 동시에 들어와 각자 요청 */
    runs = 0; no = 0;
    __asm volatile("cpsid i");
    NVIC_ISPR0 = (1u << 8) | (1u << 9) | (1u << 10);
    __asm volatile("cpsie i");
    __asm volatile("isb");
    order[no] = 0;
    board_printf_raw("[2] 3 ISRs each requested -> PendSV ran %u time(s), order=%s\n", runs, order);

    /* 3) 요청했다가 PENDSVCLR로 취소 */
    runs = 0;
    __asm volatile("cpsid i");
    SCB_ICSR = ICSR_PENDSVSET;
    SCB_ICSR = ICSR_PENDSVCLR;
    __asm volatile("cpsie i");
    __asm volatile("isb");
    board_printf_raw("[3] set then PENDSVCLR -> PendSV ran %u time(s)\n", runs);
    return 0;
}
```

```text
[1] ICSR.PENDSVSET reads 1 while masked
[1] 5 requests -> PendSV ran 1 time(s)
[2] 3 ISRs each requested -> PendSV ran 1 time(s), order=89AP
[3] set then PENDSVCLR -> PendSV ran 0 time(s)
[board_exit] code=0
```

출력에서 볼 것: 5번 요청해도 1번, ISR 세 개가 각자 요청해도 1번이다. 실행 순서 `89AP`는 우선순위 순(0x40 → 0x60 → 0x80)으로 세 ISR이 이어서 돌고, **맨 마지막에** PendSV가 한 번 돌았다는 뜻이다. PENDSVCLR로 취소하면 0번이다.

이 성질 때문에 RTOS의 "전환 요청"은 몇 번을 불러도 안전하다. FreeRTOS의 `portYIELD_FROM_ISR(xHigherPriorityTaskWoken)`을 ISR 안에서 여러 번 불러도, Zephyr의 여러 ISR 래퍼가 각자 `PENDSVSET`을 써도, 실제 전환은 한 번이다. 그리고 PendSV 핸들러는 "요청한 순간의 결정"이 아니라 **"실행하는 순간의 가장 높은 ready 태스크"** 로 바꾼다(FreeRTOS `vTaskSwitchContext()`가 PendSV 안에서 그때 다시 고른다). 결정을 여러 번 해도 마지막 상태로 한 번 실행되는 구조다.

PENDSVCLR은 언제 쓰나? Zephyr의 `z_arm_pendsv`(`swap_helper.S`)는 ready queue 캐시에서 다음 스레드를 꺼내 `_kernel.current`에 넣은 **직후** PendSV pending을 지운다. 소스 주석의 설명은 "그 전에 세워진 요청은 옛 커널 상태로 내린 결정이고 지금 처리했으니 버리고, 새 스레드로 바뀐 상태를 보고 다른 인터럽트가 새로 내리는 요청만 받겠다"는 것이다. 합쳐짐을 의도적으로 이용하는 코드다.

### 2.3 왜 PendSV는 반드시 최저 우선순위여야 하나 — 예제 8

말로는 "ISR 안에서 전환하면 안 된다"인데, 우선순위를 잘못 두면 실제로 무슨 일이 생기는지 보자. 낮은 우선순위 ISR(DUALTIMER, IRQ 10, 0xC0)이 전반부를 마치고 PENDSVSET을 쓴 뒤 후반부를 계속한다. PendSV 우선순위를 0xFF(정상)와 0x00(잘못: **리셋 값**)으로 바꿔 가며 실행 순서와 PendSV가 본 EXC_RETURN을 기록한다.

```c
/* ex8: PendSV 우선순위를 높게 두면? — ISR 한가운데서 "전환"이 일어나는 것을 EXC_RETURN으로 본다 */
#include "../b4.h"
static volatile uint32_t lr_seen, step;
static volatile uint32_t seq[8]; static volatile uint32_t ns;

void PendSV_Handler(void)
{
    lr_seen = (uint32_t)__builtin_return_address(0);
    seq[ns++] = 100 + step;               /* 100+: PendSV가 돈 시점의 step */
}

void DUALTIMER_IRQHandler(void)            /* IRQ 10, 우선순위 0xC0 — 낮은 우선순위 ISR */
{
    step = 1; seq[ns++] = 1;               /* ISR 전반부 */
    SCB_ICSR = ICSR_PENDSVSET;
    __asm volatile("isb");
    step = 2; seq[ns++] = 2;               /* ISR 후반부: 아직 이 ISR 안이다 */
}

static void trial(uint32_t pendsv_prio)
{
    SCB_SHPR3 = pendsv_prio << 16;
    ns = 0; step = 0; lr_seen = 0;
    REG32(0xE000E200u) = 1u << 10;          /* IRQ10 pending */
    __asm volatile("isb");
    board_printf_raw("PendSV prio 0x%x: sequence =", pendsv_prio);
    for (uint32_t i = 0; i < ns; i++)
        board_printf_raw(" %s", seq[i] == 1 ? "ISR-first-half" : seq[i] == 2 ? "ISR-second-half"
                                : seq[i] == 101 ? "PendSV(inside ISR!)" : "PendSV(after ISR)");
    board_printf_raw("\n    EXC_RETURN in PendSV = 0x%x (%s)\n", lr_seen,
                     lr_seen == 0xFFFFFFF1u ? "return to Handler mode = nested in ISR"
                                            : "return to Thread mode");
}

int main(void)
{
    NVIC_IPR(10) = 0xC0;
    NVIC_ISER0 = 1u << 10;
    trial(0xFF);                          /* RTOS 방식: 최저 */
    trial(0x00);                          /* 잘못된 설정: 최고 */
    return 0;
}
```

```text
PendSV prio 0xff: sequence = ISR-first-half ISR-second-half PendSV(after ISR)
    EXC_RETURN in PendSV = 0xfffffff9 (return to Thread mode)
PendSV prio 0x0: sequence = ISR-first-half PendSV(inside ISR!) ISR-second-half
    EXC_RETURN in PendSV = 0xfffffff1 (return to Handler mode = nested in ISR)
[board_exit] code=0
```

출력에서 볼 것: 0xFF면 ISR이 끝까지 돈 다음 PendSV가 돌고, PendSV의 EXC_RETURN은 `0xFFFFFFF9`(Thread로 복귀)다. 0x00이면 PendSV가 **ISR 전반부와 후반부 사이에 끼어들고**, EXC_RETURN은 `0xFFFFFFF1`(Handler 모드로 복귀 = ISR 위에 중첩)이다. 진짜 RTOS였다면 이 순간 PSP를 다른 태스크 것으로 바꾼다. 그러면 ISR 후반부는 **엉뚱한 태스크 문맥** 위에서 끝나고, 그 ISR이 돌아갈 곳(원래 태스크)의 상태가 어긋난다. 더 나쁘게는 C2의 PendSV 핸들러가 "Thread에서 들어왔다"고 가정하고 PSP에 레지스터를 저장하는데, 실제로 선점당한 쪽은 MSP 위의 ISR이라 저장 대상이 틀린다.

```svg
<svg viewBox="0 0 680 240" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="30" font-size="13" font-weight="bold">PendSV = 0xFF (RTOS 방식)</text>
<rect x="10" y="42" width="140" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="80" y="64" font-size="12" text-anchor="middle">ISR 전반부</text>
<rect x="160" y="42" width="140" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="230" y="64" font-size="12" text-anchor="middle">ISR 후반부</text>
<rect x="310" y="42" width="170" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="395" y="64" font-size="12" text-anchor="middle">PendSV: 전환 실행</text>
<rect x="490" y="42" width="180" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="580" y="64" font-size="12" text-anchor="middle">새 태스크 (Thread)</text>
<text x="395" y="96" font-size="12" text-anchor="middle">EXC_RETURN 0xFFFFFFF9</text>
<text x="10" y="140" font-size="13" font-weight="bold">PendSV = 0x00 (리셋 값 그대로 — 잘못)</text>
<rect x="10" y="152" width="140" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="80" y="174" font-size="12" text-anchor="middle">ISR 전반부</text>
<rect x="160" y="152" width="170" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="245" y="174" font-size="12" text-anchor="middle">PendSV (ISR 안!)</text>
<rect x="340" y="152" width="170" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="425" y="174" font-size="12" text-anchor="middle">ISR 후반부 (어느 문맥?)</text>
<text x="245" y="206" font-size="12" text-anchor="middle">EXC_RETURN 0xFFFFFFF1</text>
<text x="10" y="230" font-size="12">아래 줄에서 PendSV가 PSP를 바꾸면, ISR 후반부는 다른 태스크 위에서 끝나고 복귀 경로가 어긋난다</text>
</svg>
```

그림 6 — 예제 8 결과를 그림으로. 위: 최저 우선순위면 PendSV는 ISR이 다 끝난 뒤 Thread 바로 위에서 돈다. 아래: 리셋 값(0x00)이면 PendSV가 ISR을 선점해 ISR 한가운데서 "전환"이 일어난다.

정리하면 PendSV가 최저여야 하는 이유는 세 가지다.

1. **ISR 중첩 안에서 전환하지 않기 위해** — 최저면 "모든 ISR이 끝난 뒤"에만 들어갈 수 있다. EXC_RETURN이 늘 Thread 복귀(`0xFFFFFFFD`, PSP)라서 PendSV 핸들러는 "저장할 곳 = 지금 태스크의 PSP"라고 가정할 수 있다.
2. **ISR 지연에 영향을 주지 않기 위해** — 전환(수십~수백 cycle)이 어떤 ISR도 늦추지 않는다. 모터 제어 ISR 같은 급한 일은 전환 도중에도 선점해 들어온다.
3. **요청을 합치기 위해** — 최저 우선순위라 pending 상태로 오래 머물 수 있고, 그동안 오는 요청이 하나로 합쳐진다(예제 7).

### 2.4 Thread 모드에서의 요청 — `portYIELD()`에 배리어가 있는 이유

태스크(Thread 모드)가 `taskYIELD()`를 부르면 FreeRTOS는 PENDSVSET을 쓴다. Thread 모드는 어떤 예외보다 낮으므로 PendSV가 **바로** 들어가야 한다. 그런데 "바로"가 정확히 몇 명령 뒤인지는 아키텍처가 보장하지 않는다. 그래서 FreeRTOS `portmacro.h`(ARM_CM3)는 이렇게 쓴다.

```c
#define portYIELD()                                     \
    {                                                   \
        /* Set a PendSV to request a context switch. */ \
        portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT; \
                                                        \
        /* Barriers are normally not required but do ensure the code is completely \
         * within the specified behaviour for the architecture. */ \
        __asm volatile ( "dsb" ::: "memory" );                     \
        __asm volatile ( "isb" );                                  \
    }
```

`dsb`는 "ICSR 쓰기가 끝났다"를 보장하고, `isb`는 그 뒤 명령을 다시 가져오게 해서 pending된 PendSV가 `isb` 다음 명령 전에 들어가게 한다. 예제 7과 8에서도 같은 이유로 `isb`를 넣었다. 실칩에서 배리어를 빼도 대부분 동작하지만, "yield 다음 줄의 코드가 전환 전에 한두 줄 실행되는" 미묘한 버그의 원천이 될 수 있다.

### 2.5 PendSV 함정

- **우선순위 설정을 잊음** — SHPR3 리셋 값은 0이다. bare-metal에서 PendSV를 직접 쓰거나 자기 RTOS를 포팅할 때 이걸 빼면 예제 8의 아래 줄이 된다. FreeRTOS는 `xPortStartScheduler()`에서 `portNVIC_SHPR3_REG |= portNVIC_PENDSV_PRI`(0xFF)로, Zephyr는 `z_arm_exc_setup()`에서 `NVIC_SetPriority(PendSV_IRQn, _EXC_PENDSV_PRIO)`로 설정한다.
- **우선순위 비트 수** — 실칩은 보통 3~4비트만 구현한다. 0xFF를 쓰면 구현된 상위 비트만 남아 "최저"가 된다(4비트 칩은 0xF0). QEMU NVIC는 8비트를 다 구현한다. 반대로 "0xFE를 최저라고 착각"하면 4비트 칩에서 0xF0 = 최저와 같아진다(B3).
- **PendSV를 앱이 다른 용도로 씀** — 이미 RTOS가 쓰는 PendSV를 앱이 "소프트웨어 인터럽트"로 또 쓰면 핸들러가 충돌한다. 앱용 소프트웨어 인터럽트가 필요하면 안 쓰는 IRQ 번호를 `NVIC_SetPendingIRQ()`로 일으킨다(예제 6에서 TIMER0을 이렇게 썼다).
- **PendSV 안에서 오래 일함** — PendSV는 최저라 다른 ISR에 선점당하지만, 그동안 **모든 태스크가 못 돈다**. PendSV는 전환만 하고, 일은 태스크에서 한다.
- **BASEPRI/PRIMASK를 오래 잡음** — 마스킹 중에는 PendSV도 못 들어간다. "깨웠는데 태스크가 늦게 돈다"의 흔한 원인이 긴 크리티컬 섹션이다(C7, E3).

---

## 3. SVC — 동기 예외로 여는 커널의 문

### 3.1 직관 — "지금 당장, 그리고 특권으로"

`svc #imm`(supervisor call)은 **명령 한 줄로 예외를 일으킨다**. 일반 함수 호출과 다른 점은 두 가지다.

- **Handler 모드로 들어간다** = 특권(privileged)으로 실행된다. 비특권(unprivileged) Thread 코드는 SCB · NVIC · MPU 레지스터를 만질 수 없지만, `svc`를 통해 들어간 핸들러는 만질 수 있다. 이것이 **시스템 콜의 본질**이다: 사용자 코드가 커널 기능을 쓸 수 있는 유일한, 통제된 문.
- **지금 당장 들어간다**(동기). PendSV처럼 "나중에 형편 될 때"가 아니다. 들어갈 수 없는 상황이면(우선순위가 막혀 있으면) HardFault로 승격된다(§3.3).

Linux에서 `write()`가 `svc #0`(AArch64)으로 커널에 들어가는 경로는 EL E6에서 이미 봤다. MCU의 SVC도 생각은 같다. 다른 점은 MMU가 없고, 주소 공간이 하나이며, 보호를 MPU가 담당한다는 것이다(F 모듈). 그리고 많은 RTOS 구성(보호 없는 FreeRTOS, `CONFIG_USERSPACE` 없는 Zephyr)에서는 태스크가 이미 특권이라서 **시스템 콜이 필요 없다** — API가 그냥 함수 호출이다. 그런 구성에서 SVC는 거의 "첫 태스크를 시작하는 트릭"으로만 쓰인다.

### 3.2 정의 — 번호, 인자, 반환값

- **예외 번호 11**(SVCall). 우선순위는 `SHPR2`(`0xE000ED1C`)의 **[31:24]**.
- **인코딩**: Thumb 16비트 명령 `0xDFxx`. 하위 8비트 `xx`가 `#imm`(0~255). 하드웨어는 이 번호를 **어디에도 넣어 주지 않는다**. 핸들러가 직접 명령어를 읽어서 꺼내야 한다.
- **번호 꺼내기**: 예외 진입 때 하드웨어가 쌓은 8워드 프레임(R0, R1, R2, R3, R12, LR, **PC**, xPSR)의 PC는 "돌아갈 주소" = **svc 다음 명령**이다. 그러니 `PC − 2` 위치의 halfword가 svc 명령이고, 그 하위 바이트가 번호다. 리틀 엔디언이라 바이트 주소 `PC − 2`의 한 바이트를 읽어도 같다(Zephyr · FreeRTOS-MPU가 `ldrb r1, [r1, #-2]`로 이렇게 한다).
- **어느 스택?**: 프레임은 svc를 실행한 쪽의 스택에 쌓인다. Thread가 PSP를 쓰고 있었으면 PSP, 아니면 MSP. 핸들러에 들어올 때 LR에 담긴 **EXC_RETURN의 bit 2**가 그 답이다(0 = MSP, 1 = PSP).
- **인자**: AAPCS에 따라 호출 쪽이 R0~R3에 넣은 값이 프레임의 `frame[0]`~`frame[3]`에 그대로 쌓여 있다. 핸들러는 레지스터가 아니라 **프레임에서 읽는다**(핸들러 안의 C 코드가 실행되는 동안 실제 R0~R3는 이미 다른 값일 수 있다).
- **반환값**: 핸들러가 `frame[0]`에 쓴다. 예외 복귀 때 하드웨어가 프레임에서 R0를 꺼내므로, 호출 쪽은 `svc` 다음 줄에서 R0로 결과를 받는다. **핸들러 안에서 레지스터 R0에 쓰는 것은 소용없다** — 복귀하면서 프레임 값으로 덮인다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="130" y="22" font-size="13" text-anchor="middle" font-weight="bold">쌓인 프레임 (PSP 또는 MSP)</text>
<text x="44" y="51" font-size="12" text-anchor="end">+0</text>
<rect x="50" y="34" width="190" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="145" y="52" font-size="12" text-anchor="middle">R0 = 인자 1 · 반환값</text>
<text x="44" y="77" font-size="12" text-anchor="end">+4</text>
<rect x="50" y="60" width="190" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="145" y="78" font-size="12" text-anchor="middle">R1 = 인자 2</text>
<text x="44" y="103" font-size="12" text-anchor="end">+8</text>
<rect x="50" y="86" width="190" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="145" y="104" font-size="12" text-anchor="middle">R2 = 인자 3</text>
<text x="44" y="129" font-size="12" text-anchor="end">+12</text>
<rect x="50" y="112" width="190" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="145" y="130" font-size="12" text-anchor="middle">R3 = 인자 4</text>
<text x="44" y="155" font-size="12" text-anchor="end">+16</text>
<rect x="50" y="138" width="190" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="145" y="156" font-size="12" text-anchor="middle">R12</text>
<text x="44" y="181" font-size="12" text-anchor="end">+20</text>
<rect x="50" y="164" width="190" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="145" y="182" font-size="12" text-anchor="middle">LR</text>
<text x="44" y="207" font-size="12" text-anchor="end">+24</text>
<rect x="50" y="190" width="190" height="26" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/>
<text x="145" y="208" font-size="12" text-anchor="middle">PC = svc 다음 주소</text>
<text x="44" y="233" font-size="12" text-anchor="end">+28</text>
<rect x="50" y="216" width="190" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="145" y="234" font-size="12" text-anchor="middle">xPSR</text>
<text x="500" y="22" font-size="13" text-anchor="middle" font-weight="bold">코드 메모리 (예제 9의 objdump)</text>
<rect x="360" y="34" width="280" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="372" y="52" font-size="12">0x14e:  2304   movs r3, #4</text>
<rect x="360" y="60" width="280" height="26" fill="#d0564a" fill-opacity="0.15" stroke="#d0564a"/>
<text x="372" y="78" font-size="12">0x150:  df01   svc 1   ← PC − 2</text>
<rect x="360" y="86" width="280" height="26" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/>
<text x="372" y="104" font-size="12">0x152:  다음 명령   ← 쌓인 PC</text>
<line x1="240" y1="203" x2="358" y2="102" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="360" y="140" width="300" height="70" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="372" y="162" font-size="12">insn = ((uint16_t)PC)[−1] = 0xDF01</text>
<text x="372" y="182" font-size="12">imm = insn &amp; 0xFF = 1 → 1번 서비스</text>
<text x="372" y="200" font-size="12">asm 한 줄: ldrb r1, [r1, #-2]</text>
<text x="10" y="272" font-size="12">핸들러는 frame[0..3]에서 인자를 읽고, 결과를 frame[0]에 쓴다</text>
<text x="10" y="292" font-size="12">→ 예외 복귀 때 하드웨어가 frame[0]을 R0로 꺼내 주므로 호출 쪽은 R0로 결과를 받는다</text>
<text x="10" y="312" font-size="12">어느 스택인가: EXC_RETURN bit 2 (0 = MSP, 1 = PSP) — tst lr, #4</text>
</svg>
```

그림 7 — SVC 핸들러가 보는 세상. 쌓인 PC는 svc **다음** 명령을 가리키므로, 번호는 PC − 2의 halfword(0xDFxx)에서 꺼낸다. 인자와 반환값은 레지스터가 아니라 프레임 슬롯이다.

### 예제 9 — bare-metal 미니 시스템 콜 테이블

시스템 콜 세 개(`SYS_WRITE` 문자열 출력, `SYS_ADD4` 인자 4개 더하기, `SYS_SHPR3` 특권 레지스터 읽기)를 테이블로 만든다. naked 어셈블리 핸들러가 프레임 포인터를 고르고, C 디스패처가 번호를 꺼내 테이블을 부른다. 마지막에는 Thread를 **PSP + 비특권**으로 바꾼 뒤, SVC로는 특권 레지스터를 읽을 수 있지만 직접 읽으면 fault가 나는 것을 확인한다.

```c
/* ex9: bare-metal 미니 시스템 콜 — svc #imm 번호 꺼내기, R0~R3 인자, 쌓인 R0로 반환 */
#include "../b4.h"
enum { SYS_WRITE = 0, SYS_ADD4 = 1, SYS_SHPR3 = 2, SYS_MAX };
#define SVC_CALL(n, a, b, c, d) ({                                                   \
    register uint32_t r0 __asm__("r0") = (uint32_t)(a), r1 __asm__("r1") = (uint32_t)(b); \
    register uint32_t r2 __asm__("r2") = (uint32_t)(c), r3 __asm__("r3") = (uint32_t)(d); \
    __asm volatile("svc %[imm]" : "+r"(r0) : [imm] "i"(n), "r"(r1), "r"(r2), "r"(r3) : "memory"); \
    r0; })

static uint32_t sys_write(uint32_t *f) { board_puts((const char *)f[0]); return 0; }
static uint32_t sys_add4(uint32_t *f)  { return f[0] + f[1] + f[2] + f[3]; }
static uint32_t sys_shpr3(uint32_t *f) { (void)f; return SCB_SHPR3; }  /* 특권 레지스터 */
static uint32_t (*const sys_table[SYS_MAX])(uint32_t *) = { sys_write, sys_add4, sys_shpr3 };
static volatile uint32_t last_lr;

void svc_dispatch(uint32_t *frame, uint32_t exc_return)   /* frame = 하드웨어가 쌓은 8워드 */
{
    uint16_t insn = ((uint16_t *)frame[6])[-1];   /* stacked PC 바로 앞 halfword = SVC 명령 */
    uint8_t  imm  = insn & 0xFFu;                 /* 하위 8비트 = #imm (0xDFxx) */
    last_lr = exc_return;
    frame[0] = imm < SYS_MAX ? sys_table[imm](frame) : 0xFFFFFFFFu;  /* 쌓인 R0 = 반환값 */
}

__attribute__((naked)) void SVC_Handler(void)
{
    __asm volatile(
        " tst   lr, #4          \n"   /* EXC_RETURN bit2: 0=MSP, 1=PSP 에 프레임이 있다 */
        " ite   eq              \n"
        " mrseq r0, msp         \n"
        " mrsne r0, psp         \n"
        " mov   r1, lr          \n"
        " push  {r4, lr}        \n"   /* 8바이트 정렬 유지 */
        " bl    svc_dispatch    \n"
        " pop   {r4, pc}        \n"); /* pc <- EXC_RETURN: 예외 복귀 */
}

static uint32_t psp_stack[256];
int main(void)
{
    SCB_SHPR3 = (0xFFu << 16) | (0xFFu << 24);
    SVC_CALL(SYS_WRITE, "hello from svc #0 (MSP)\n", 0, 0, 0);
    board_printf_raw("svc #1 add4(1,2,3,4) = %u, EXC_RETURN=0x%x\n", SVC_CALL(SYS_ADD4, 1, 2, 3, 4), last_lr);
    board_printf_raw("svc #7 (no such call) = 0x%x\n", SVC_CALL(7, 0, 0, 0, 0));

    __asm volatile("msr psp, %0" :: "r"(&psp_stack[256]));
    __asm volatile("msr control, %0; isb" :: "r"(3u));     /* SPSEL=1(PSP), nPRIV=1(비특권) */
    board_printf_raw("now unprivileged on PSP: svc #2 SHPR3 = 0x%x, EXC_RETURN=0x%x\n",
                     SVC_CALL(SYS_SHPR3, 0, 0, 0, 0), last_lr);
    board_printf_raw("direct read of SHPR3 from unprivileged code ->\n");
    volatile uint32_t x = SCB_SHPR3; (void)x;                /* BusFault → HardFault 예상 */
    return 0;
}
```

```text
hello from svc #0 (MSP)
svc #1 add4(1,2,3,4) = 10, EXC_RETURN=0xfffffff9
svc #7 (no such call) = 0xffffffff
now unprivileged on PSP: svc #2 SHPR3 = 0xffff0000, EXC_RETURN=0xfffffffd
direct read of SHPR3 from unprivileged code ->

[HardFault] PC=0x19c LR=0x19b xPSR=0x61000000 CFSR=0x8200 HFSR=0x40000000
[board_exit] code=255
```

출력에서 볼 것:

- `svc #0`은 프레임의 R0(문자열 포인터)를 받아 출력했고, `svc #1`은 R0~R3 네 인자를 더해 **frame[0]으로 돌려준 10**이 호출 쪽 R0로 왔다. 없는 번호 `svc #7`은 테이블 밖이라 `0xFFFFFFFF`(오류 코드)를 받았다. 실제 커널이라면 여기서 `-ENOSYS`를 돌려주거나 태스크를 죽인다 — **번호 검사는 필수**다(검사 없이 테이블을 인덱싱하면 비특권 코드가 임의 주소로 점프하게 만드는 보안 구멍이 된다).
- EXC_RETURN이 MSP일 때 `0xFFFFFFF9`, PSP로 바꾼 뒤 `0xFFFFFFFD` — 핸들러가 bit 2로 스택을 고른 것이 맞았다.
- 비특권 + PSP 상태에서 SVC로는 특권 레지스터 SHPR3(`0xffff0000` = 우리가 쓴 SysTick · PendSV 0xFF)를 읽었지만, **같은 레지스터를 직접 읽자 HardFault**가 났다. CFSR `0x8200` = BusFault의 PRECISERR(bit 9) + BFARVALID(bit 15), HFSR `0x40000000` = FORCED(BusFault 핸들러가 꺼져 있어 HardFault로 승격). 비특권 코드는 System Control Space에 접근할 수 없다. **SVC가 그 벽을 넘는 유일한 정식 문이다.** (이 보호를 RAM · 주변장치까지 넓히는 것이 MPU다 — F 모듈.)

### 예제 10 — svc 인코딩 확인과 HardFault 승격

먼저 예제 9의 ELF를 디스어셈블해서 `svc` 명령의 인코딩과 컴파일러가 만든 디스패처를 본다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/B4/ex9; arm-none-eabi-objdump -d app.elf | grep -B1 "svc\s"; \
  arm-none-eabi-objdump -d app.elf | sed -n "/<SVC_Handler>:/,/^$/p"; \
  arm-none-eabi-objdump -d app.elf | sed -n "/<svc_dispatch>:/,/^$/p" | head -12'
```

```text
     144:	460b      	mov	r3, r1
     146:	df00      	svc	0
--
     14e:	2304      	movs	r3, #4
     150:	df01      	svc	1
--
     164:	4603      	mov	r3, r0
     166:	df07      	svc	7
--
     186:	4603      	mov	r3, r0
     188:	df02      	svc	2
00000118 <SVC_Handler>:
     118:	f01e 0f04 	tst.w	lr, #4
     11c:	bf0c      	ite	eq
     11e:	f3ef 8008 	mrseq	r0, MSP
     122:	f3ef 8009 	mrsne	r0, PSP
     126:	4671      	mov	r1, lr
     128:	b510      	push	{r4, lr}
     12a:	f7ff ffdd 	bl	e8 <svc_dispatch>
     12e:	bd10      	pop	{r4, pc}

000000e8 <svc_dispatch>:
      e8:	6983      	ldr	r3, [r0, #24]
      ea:	4a09      	ldr	r2, [pc, #36]	@ (110 <svc_dispatch+0x28>)
      ec:	f813 3c02 	ldrb.w	r3, [r3, #-2]
      f0:	b510      	push	{r4, lr}
      f2:	2b02      	cmp	r3, #2
      f4:	4604      	mov	r4, r0
      f6:	6011      	str	r1, [r2, #0]
      f8:	d805      	bhi.n	106 <svc_dispatch+0x1e>
      fa:	4a06      	ldr	r2, [pc, #24]	@ (114 <svc_dispatch+0x2c>)
      fc:	f852 3023 	ldr.w	r3, [r2, r3, lsl #2]
     100:	4798      	blx	r3
     102:	6020      	str	r0, [r4, #0]
```

출력에서 볼 것: `svc 0/1/7/2`가 `df00/df01/df07/df02` — 하위 바이트가 번호다. 컴파일러는 C의 `((uint16_t *)frame[6])[-1] & 0xFF`를 `ldr r3, [r0, #24]`(frame[6] = 쌓인 PC) + `ldrb.w r3, [r3, #-2]`로 만들었다. Zephyr의 `z_arm_svc`(`arch/arm/core/cortex_m/svc.S`)와 FreeRTOS-MPU 포트의 `vPortSVCHandler`가 손으로 쓴 어셈블리와 **똑같은 두 줄**이다. 그리고 `str r0, [r4, #0]`이 "반환값을 frame[0]에 쓰기"다.

다음은 SVC의 **동기** 성질이다. SVC는 실행하는 그 순간 들어가야 한다. 그런데 지금의 실행 우선순위가 SVCall 우선순위보다 높거나 같으면(= SVC가 들어갈 수 없으면) 기다리지 않고 **HardFault로 승격(escalation)** 된다. 두 경우를 일부러 만들어 본다: (B) PRIMASK = 1(인터럽트 금지) 상태에서 svc, (C) SVCall(0x80)보다 높은 ISR(0x20) 안에서 svc.

```c
/* ex10: SVC는 "동기" 예외 — 지금 당장 못 들어가면 HardFault로 승격된다 */
#include "../b4.h"
static volatile uint32_t svc_runs;
void SVC_Handler(void) { svc_runs++; }
void TIMER0_IRQHandler(void)              /* IRQ8: 우선순위를 SVC보다 높게(숫자 작게) 둔다 */
{
    board_printf_raw("  in IRQ8 (prio 0x20), calling svc with SVCall prio 0x80...\n");
    __asm volatile("svc 0");
    board_printf_raw("  returned (should not happen)\n");
}

int main(void)
{
    SCB_SHPR2 = 0x80u << 24;              /* SVCall 우선순위 0x80 */
    __asm volatile("svc 0");
    board_printf_raw("case A thread, unmasked: svc ran %u time(s)\n", svc_runs);
#if CASE == 1
    board_printf_raw("case B: PRIMASK=1 then svc ->\n");
    __asm volatile("cpsid i; svc 0");     /* 인터럽트 마스킹 중 SVC */
#elif CASE == 2
    board_printf_raw("case C: svc inside higher-priority ISR ->\n");
    NVIC_IPR(8) = 0x20; NVIC_ISER0 = 1u << 8;
    REG32(0xE000E200u) = 1u << 8;
    __asm volatile("isb");
#endif
    board_printf_raw("end\n");
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'for c in 1 2; do make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile \
  SRCS=/Users/donh/.rtos_scratch/B4/ex10/main.c OUT=/var/tmp/rtos/B4/ex10_$c EXTRA_CFLAGS=-DCASE=$c >/dev/null && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B4/ex10_$c/app.elf; echo "qemu exit=$?"; done'
```

```text
case A thread, unmasked: svc ran 1 time(s)
case B: PRIMASK=1 then svc ->

[HardFault] PC=0x110 LR=0x10d xPSR=0x61000000 CFSR=0x0 HFSR=0x40000000
[board_exit] code=255
qemu exit=1
case A thread, unmasked: svc ran 1 time(s)
case C: svc inside higher-priority ISR ->
  in IRQ8 (prio 0x20), calling svc with SVCall prio 0x80...

[HardFault] PC=0xda LR=0xd9 xPSR=0x61000018 CFSR=0x0 HFSR=0x40000000
[board_exit] code=255
qemu exit=1
```

출력에서 볼 것: Thread에서 마스킹 없이 부르면 정상(case A). PRIMASK = 1이면(case B) 그리고 더 높은 ISR 안에서 부르면(case C, 쌓인 xPSR의 예외 번호 `0x18` = 24 = IRQ 8) **둘 다 HardFault**, HFSR = FORCED, CFSR = 0(다른 fault 원인 없음)이다. "HFSR.FORCED인데 CFSR이 0"이면 SVC 승격(또는 다른 동기 예외의 승격)을 의심하는 단서가 된다. 이 QEMU에서는 쌓인 PC가 svc **다음** 주소(case B: svc는 `0x10e`, PC `0x110`)였다. 실칩에서 승격된 SVC의 쌓인 PC가 정확히 어디인지는 이 노트에서 실칩으로 확인하지 못했다. fault 분석은 M1에서 다시 본다.

실무에서 이 함정을 밟는 장면: ISR 안에서 "커널 API니까"라며 SVC를 쓰는 시스템 콜 래퍼를 부를 때(보호 모드 RTOS에서 ISR은 `...FromISR` 직접 호출을 써야 한다), 또는 크리티컬 섹션 안에서 SVC 기반 API를 부를 때다. **FreeRTOS가 `xPortStartScheduler()`에서 SVCall을 최고 우선순위(SHPR2 = 0)로 두는 이유**가 이것이다 — 첫 태스크를 시작하는 `svc 0`이 어떤 상황에서도 바로 들어가도록.

### 3.3 RTOS는 SVC를 어떻게 쓰나 (이름만 — 자세히는 C3 · C8)

| RTOS · 포트 | SVC 용도 | 번호 체계 | 소스 위치 |
|---|---|---|---|
| FreeRTOS ARM_CM3 (보호 없음) | **첫 태스크 시작만** | `svc 0` 하나, 핸들러는 번호를 **보지 않는다** | `portable/GCC/ARM_CM3/port.c`: `prvPortStartFirstTask()` → `vPortSVCHandler()` |
| FreeRTOS ARM_CM3_MPU | 스케줄러 시작 · yield · 특권 올리기 · 시스템 콜 복귀 | `portSVC_START_SCHEDULER` 100, `portSVC_YIELD` 101, `portSVC_RAISE_PRIVILEGE` 102, `portSVC_SYSTEM_CALL_EXIT` 103 | `ARM_CM3_MPU/portmacro.h`, `port.c`의 `vSVCHandler_C()`가 `((uint8_t *)ulPC)[-2]`로 번호를 꺼냄 |
| FreeRTOS ARMv8-M (CM33 등) | 위 + TrustZone 보안 컨텍스트 할당/해제 | `portSVC_ALLOCATE_SECURE_CONTEXT`, `portSVC_FREE_SECURE_CONTEXT` 등 | `portable/ARMv8M/non_secure/port.c` |
| Zephyr Cortex-M | irq_offload, `k_oops`/`k_panic`, 시스템 콜 | `_SVC_CALL_IRQ_OFFLOAD` 1, `_SVC_CALL_RUNTIME_EXCEPT` 2, `_SVC_CALL_SYSTEM_CALL` 3 (0은 미사용) | `include/zephyr/arch/arm/syscall.h`, `arch/arm/core/cortex_m/svc.S`의 `z_arm_svc` |

몇 가지가 흥미롭다.

- **FreeRTOS ARM_CM3의 `vPortSVCHandler()`는 번호를 읽지 않는다.** 핸들러 전체가 "`pxCurrentTCB`의 스택에서 R4~R11을 꺼내고, PSP를 설정하고, EXC_RETURN을 Thread/PSP로 만들어 복귀"다. 그래서 이 포트를 쓰는 앱이 자기 용도로 `svc`를 실행하면 **현재 태스크의 옛 문맥을 다시 복원**해 버린다. 보호 없는 FreeRTOS 위에서는 앱이 `svc`를 쓰면 안 된다.
- **Zephyr의 시스템 콜은 번호를 immediate에 넣지 않는다.** 모든 시스템 콜이 `svc #3` 하나이고, 시스템 콜 ID는 **R6 레지스터**에 넣는다(`arch_syscall_invoke6()`가 `register uint32_t r6 __asm__("r6") = call_id`). 8비트 immediate는 256개뿐이고, 명령어 메모리를 읽어야 하기 때문이다. 인자는 R0~R5에 최대 6개.
- **Zephyr는 첫 스레드를 SVC로 시작하지 않는다.** `arch_switch_to_main_thread()`(`arch/arm/core/cortex_m/thread.c`)가 PSP를 설정하고 `bx`로 main 스레드 진입 함수로 바로 뛴다. "첫 태스크 = SVC"는 FreeRTOS Cortex-M3 포트의 선택이지 Cortex-M RTOS의 공통 규칙이 아니다.
- **ARMv6-M(M0/M0+)** 에서도 SVC · PendSV · SysTick은 모두 있다(SysTick은 구현 선택). 다만 BASEPRI가 없어서 Zephyr는 M0 계열에서 SVCall 우선순위를 기본값 0으로 둔다(`z_arm_exc_setup()` 주석).

FreeRTOS CM3 포트의 첫 태스크 시작 경로(자세한 해부는 C3)는 이렇다.

```c
static void prvPortStartFirstTask( void )
{
    __asm volatile (
        " ldr r0, =0xE000ED08   \n" /* Use the NVIC offset register to locate the stack. */
        " ldr r0, [r0]          \n"
        " ldr r0, [r0]          \n"
        " msr msp, r0           \n" /* Set the msp back to the start of the stack. */
        " cpsie i               \n" /* Globally enable interrupts. */
        " cpsie f               \n"
        " dsb                   \n"
        " isb                   \n"
        " svc 0                 \n" /* System call to start first task. */
        " nop                   \n"
        " .ltorg                \n"
        );
}
```

VTOR(`0xE000ED08`)에서 벡터 테이블 주소를 읽고, 그 첫 워드(초기 MSP)로 MSP를 **리셋 상태로 되돌린다**(main이 쓰던 스택을 버린다 — "main 스택의 운명", C3). 인터럽트를 켜고 `svc 0`. 왜 그냥 함수로 뛰지 않고 SVC를 쓸까? 첫 태스크로 들어가는 길을 **예외 복귀(EXC_RETURN)** 로 만들면, 하드웨어가 태스크 스택의 가짜 프레임(fake frame)에서 PC · xPSR · R0를 꺼내 주고, 동시에 Thread 모드 + PSP로 전환해 준다. 이후 모든 전환(PendSV)과 **똑같은 복귀 경로**를 첫 태스크도 타게 하는 것이다.

### 3.4 SVC 함정

- **번호를 레지스터에서 읽으려 함** — 하드웨어는 번호를 어디에도 저장하지 않는다. 반드시 쌓인 PC − 2를 읽는다.
- **스택을 MSP로 가정** — 태스크는 PSP를 쓴다. EXC_RETURN bit 2를 보지 않으면 엉뚱한 프레임을 읽는다(예제 9가 두 경우를 다 확인).
- **반환값을 R0 레지스터에 씀** — 복귀하면서 프레임의 R0로 덮인다. frame[0]에 쓴다.
- **번호 검사 없음** — 비특권 코드가 아무 번호나 부를 수 있다. 범위 검사 + 알 수 없는 번호는 오류.
- **ISR · 크리티컬 섹션 안에서 svc** — HardFault 승격(예제 10).
- **Thumb-2 32비트 명령 뒤** — svc 자체는 늘 16비트(`0xDFxx`)라서 PC − 2가 맞다. Armv8-M의 BTI 같은 확장이 있어도 svc 인코딩은 같다(Zephyr는 BTI를 켜면 svc 다음에 `bti`를 둔다).

---

## 4. 셋을 조립하기 — 스케줄러 뼈대 (SysTick이 결정하고, PendSV가 실행한다)

이제 세 조각을 붙인다. 진짜 RTOS와 같은 **역할 분담**을 하되, 진짜 스택 전환은 하지 않는 작은 실험이다.

- "태스크" 넷: `idle`(0), `telem`(1, 10 ms마다 3 ms 일), `ctrl`(2, 4 ms마다 1 ms 일), `imu`(3, 이벤트로 깨어 0.3 ms 일). 숫자가 클수록 높은 우선순위. ready 집합은 비트맵이고 `__builtin_clz`로 가장 높은 비트를 찾는다(C1의 "CLZ로 O(1) 최고 우선순위 찾기" 축소판).
- **결정하는 곳 세 군데**: SysTick(1 kHz, 0x80)이 주기 태스크를 깨우고, IMU ISR(TIMER0, 7 ms마다, 0x40)이 `imu`를 깨우고, 태스크가 일을 다 하면 스스로 블록한다. 셋 다 `decide()`만 부른다: "가장 높은 ready가 지금 것과 다르면 `nxt`를 적고 PENDSVSET".
- **실행하는 곳 한 군데**: PendSV(0xFF)가 `cur = nxt`. 진짜 RTOS는 여기서 R4~R11을 저장하고 PSP를 바꾼다(C2). 여기서는 `cur` 변수만 바꾸고, Thread의 루프가 `cur`를 보고 그 태스크의 일을 10 µs씩 소비한다. 선점당한 태스크가 "중간부터 이어 하기"는 남은 일(`remaining[]`)로 흉내 낸다 — 레지스터를 저장하지 않으니 이렇게 쪼갤 수밖에 없다. 바로 이 불편을 없애는 것이 C2의 진짜 컨텍스트 스위치다.

### 예제 12 — 스케줄러 뼈대와 이벤트 로그

```c
/* ex12: 스케줄러 뼈대 — SysTick·ISR·태스크가 "결정"하고 PendSV가 "실행"한다 (진짜 스택 전환은 C2) */
#include "../b4.h"
#define NT 4                               /* 0 idle, 1 telem(10 ms), 2 ctrl(4 ms), 3 imu(이벤트) */
static const uint32_t work_cyc[NT] = {0, 3000 * 25, 1000 * 25, 300 * 25};
static volatile uint32_t tick, ready = 1, remaining[NT], cur, nxt, t0;
static struct { uint32_t t; char k; uint8_t a; } ev[160]; static volatile uint32_t nev;
static void log_ev(char k, uint32_t a) { if (nev < 160) { ev[nev].t = cyc() - t0; ev[nev].k = k; ev[nev].a = (uint8_t)a; nev++; } }
static uint32_t highest(void) { return 31u - (uint32_t)__builtin_clz(ready); }  /* 높은 비트 = 높은 우선순위 */
static void make_ready(uint32_t t) { ready |= 1u << t; remaining[t] = work_cyc[t]; }
static void decide(char who)               /* 결정만 한다: 바뀌어야 하면 PENDSVSET */
{
    uint32_t h = highest();
    if (h != cur) { nxt = h; SCB_ICSR = ICSR_PENDSVSET; log_ev(who, h); }
}
void SysTick_Handler(void)                 /* 0x80 */
{
    tick++; log_ev('T', tick);
    if (tick % 10 == 0) make_ready(1);
    if (tick % 4 == 0)  make_ready(2);
    decide('S');
}
void TIMER0_IRQHandler(void)               /* 0x40: IMU data-ready 흉내, 7 ms마다 */
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    make_ready(3); decide('I');
}
void PendSV_Handler(void)                  /* 0xFF: 실행만 한다 (여기서 진짜 RTOS는 스택을 바꾼다) */
{
    cur = nxt; log_ev('P', cur);
}

int main(void)
{
    SCB_SHPR3 = (0x80u << 24) | (0xFFu << 16);
    NVIC_IPR(8) = 0x40; NVIC_ISER0 = 1u << 8;
    TIMER_RELOAD(TIMER0_BASE) = 7000u * 25u - 1u; TIMER_VALUE(TIMER0_BASE) = 7000u * 25u - 1u;
    t0 = cyc();
    TIMER_CTRL(TIMER0_BASE) = 1u | 8u;     /* EN + IRQ EN */
    SYST_RVR = 25000u - 1u; SYST_CVR = 0; SYST_CSR = 7u;
    while (tick < 21) {                    /* "태스크 실행" = 현재 태스크의 일을 10 us씩 소비 */
        uint32_t me = cur;
        if (me == 0) continue;             /* idle */
        uint32_t s = cyc();
        while (cyc() - s < 250u && cur == me) { }
        uint32_t used = cyc() - s;
        __asm volatile("cpsid i");
        if (used >= remaining[me]) { remaining[me] = 0; ready &= ~(1u << me); decide('Y'); }  /* 일 끝 → 블록 */
        else remaining[me] -= used;
        __asm volatile("cpsie i");
    }
    SYST_CSR = 0; TIMER_CTRL(TIMER0_BASE) = 0;
    for (uint32_t i = 0; i < nev; i++) board_printf_raw("%u %c %u\n", ev[i].t / 25u, ev[i].k, ev[i].a);
    return 0;
}
```

출력은 한 줄에 "시각(µs) 종류 대상"이다. 종류: `T` = SysTick(대상은 tick 번호), `S` / `I` / `Y` = SysTick · IMU ISR · 태스크가 "대상 태스크로 바꿔라"라고 결정, `P` = PendSV가 대상 태스크로 실제 전환. 58줄 중 앞부분과 흥미로운 구간만 남겼다(`...`는 생략).

```text
1000 T 1
2000 T 2
3000 T 3
4000 T 4
4002 S 2
4003 P 2
5000 T 5
5064 Y 0
5065 P 0
6000 T 6
7001 I 3
7002 T 7
7003 S 3
7004 P 3
7329 Y 0
7330 P 0
...
10000 T 10
10002 S 1
10003 P 1
11000 T 11
12000 T 12
12002 S 2
12003 P 2
13000 T 13
13064 Y 1
13065 P 1
14001 I 3
14002 T 14
14003 S 3
14004 P 3
14330 Y 1
14331 P 1
14559 Y 0
14560 P 0
...
21001 I 3
21002 T 21
21003 S 3
21004 P 3
[board_exit] code=0
```

출력에서 볼 것:

- **결정과 실행이 1 µs 간격으로 짝을 이룬다.** `4002 S 2` → `4003 P 2`: SysTick이 ctrl을 깨워 결정하고, SysTick이 끝나자마자 PendSV가 tail-chain으로 전환했다. 태스크가 블록할 때도 `5064 Y 0` → `5065 P 0`.
- **두 결정이 한 번의 전환으로 합쳐졌다.** `7001 I 3`(IMU ISR이 imu로 바꾸라고 결정) → `7002 T 7`(그 사이 tick 도래, SysTick이 tail-chain으로 먼저 돈다 — 0x80이 PendSV보다 높으니까) → `7003 S 3`(SysTick도 같은 결정) → `7004 P 3`(PendSV는 **한 번**). 예제 7의 합쳐짐이 실제 흐름에서 일어난 장면이다.
- **선점과 재개**: telem이 10003 µs에 돌기 시작해 12003 µs에 ctrl에게, 14004 µs에 imu에게 선점당하고, 그때마다 높은 태스크가 블록하면 다시 telem으로 돌아온다(`13065 P 1`, `14331 P 1`). 모든 전환은 `P` 줄에서만 일어났다.

이 로그를 Python으로 간트 차트로 그리면 이렇다(좌표는 로그에서 계산).

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="47" font-size="13">imu</text>
<line x1="90" y1="56" x2="660" y2="56" stroke="currentColor" stroke-opacity="0.25"/>
<text x="10" y="87" font-size="13">ctrl</text>
<line x1="90" y1="96" x2="660" y2="96" stroke="currentColor" stroke-opacity="0.25"/>
<text x="10" y="127" font-size="13">telem</text>
<line x1="90" y1="136" x2="660" y2="136" stroke="currentColor" stroke-opacity="0.25"/>
<text x="10" y="167" font-size="13">idle</text>
<line x1="90" y1="176" x2="660" y2="176" stroke="currentColor" stroke-opacity="0.25"/>
<rect x="90.0" y="154" width="107.1" height="22" fill="#888" fill-opacity="0.35" stroke="#888"/>
<rect x="197.1" y="74" width="28.4" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="225.5" y="154" width="51.9" height="22" fill="#888" fill-opacity="0.35" stroke="#888"/>
<rect x="277.4" y="34" width="8.7" height="22" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<rect x="286.1" y="154" width="18.0" height="22" fill="#888" fill-opacity="0.35" stroke="#888"/>
<rect x="304.1" y="74" width="28.4" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="332.5" y="154" width="25.1" height="22" fill="#888" fill-opacity="0.35" stroke="#888"/>
<rect x="357.6" y="114" width="53.5" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="411.1" y="74" width="28.4" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="439.6" y="114" width="25.1" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="464.7" y="34" width="8.7" height="22" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<rect x="473.4" y="114" width="6.1" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="479.6" y="154" width="38.6" height="22" fill="#888" fill-opacity="0.35" stroke="#888"/>
<rect x="518.2" y="74" width="28.4" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="546.6" y="154" width="78.6" height="22" fill="#888" fill-opacity="0.35" stroke="#888"/>
<rect x="625.2" y="74" width="26.8" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="652.0" y="34" width="8.0" height="22" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<line x1="116.8" y1="187" x2="116.8" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="143.5" y1="187" x2="143.5" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="170.3" y1="187" x2="170.3" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="197.0" y1="187" x2="197.0" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="223.8" y1="187" x2="223.8" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="250.5" y1="187" x2="250.5" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<path d="M277.3,32 l-5,-9 l10,0 z" fill="#d0564a"/>
<line x1="277.3" y1="187" x2="277.3" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="304.0" y1="187" x2="304.0" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="330.8" y1="187" x2="330.8" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="357.6" y1="187" x2="357.6" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="384.3" y1="187" x2="384.3" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="411.1" y1="187" x2="411.1" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="437.8" y1="187" x2="437.8" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<path d="M464.6,32 l-5,-9 l10,0 z" fill="#d0564a"/>
<line x1="464.6" y1="187" x2="464.6" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="491.3" y1="187" x2="491.3" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="518.1" y1="187" x2="518.1" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="544.8" y1="187" x2="544.8" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="571.6" y1="187" x2="571.6" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="598.4" y1="187" x2="598.4" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="625.1" y1="187" x2="625.1" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<path d="M651.9,32 l-5,-9 l10,0 z" fill="#d0564a"/>
<line x1="651.9" y1="187" x2="651.9" y2="195" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="90" y1="195" x2="660" y2="195" stroke="currentColor"/>
<text x="90.0" y="213" font-size="12" text-anchor="middle">0</text>
<text x="143.5" y="213" font-size="12" text-anchor="middle">2</text>
<text x="197.0" y="213" font-size="12" text-anchor="middle">4</text>
<text x="250.5" y="213" font-size="12" text-anchor="middle">6</text>
<text x="304.0" y="213" font-size="12" text-anchor="middle">8</text>
<text x="357.6" y="213" font-size="12" text-anchor="middle">10</text>
<text x="411.1" y="213" font-size="12" text-anchor="middle">12</text>
<text x="464.6" y="213" font-size="12" text-anchor="middle">14</text>
<text x="518.1" y="213" font-size="12" text-anchor="middle">16</text>
<text x="571.6" y="213" font-size="12" text-anchor="middle">18</text>
<text x="625.1" y="213" font-size="12" text-anchor="middle">20</text>
<text x="375" y="233" font-size="12" text-anchor="middle">시간 (ms, 예제 12 실측) — 주황 눈금 = SysTick, 빨간 삼각형 = IMU ISR</text>
<text x="10" y="257" font-size="12">막대가 바뀌는 순간마다 PendSV가 한 번 돌았다 (결정은 SysTick·ISR·태스크, 실행은 PendSV)</text>
</svg>
```

그림 8 — 예제 12의 측정 간트 차트. 위 네 줄은 그 순간 `cur`가 가리킨 태스크(= PendSV가 마지막으로 고른 태스크)다. 아래 축의 주황 눈금이 SysTick, 빨간 삼각형이 IMU ISR이다. 1 tick마다 SysTick이 돌지만, 전환(막대가 바뀌는 곳)은 결정이 "지금과 다르다"일 때만 일어난다.

뼈대에서 진짜 RTOS로 가려면 무엇이 더 필요한가? (1) PendSV 안에서 R4~R11 저장 · PSP 교체 · 복원(C2), (2) 태스크마다 스택과 가짜 초기 프레임, 그리고 SVC로 첫 태스크 진입(C3), (3) tick에서 "시간이 된 태스크"를 효율적으로 찾는 delayed list(C1 · C5), (4) `decide()`와 ready 비트맵을 ISR과 태스크가 동시에 건드리지 않게 하는 크리티컬 섹션(C7 — 여기서는 Thread 쪽만 `cpsid`로 막았다).

---

## 5. RTOS 커널 안에서는 무슨 일이 일어나나

### 5.1 FreeRTOS V11.3.2 — `portable/GCC/ARM_CM3/port.c`

FreeRTOS Cortex-M3 포트는 세 예외를 이렇게 쓴다.

- **세 핸들러 이름**: 포트는 `vPortSVCHandler`, `xPortPendSVHandler`, `xPortSysTickHandler`를 정의한다. 앱(또는 이 lab 템플릿의 `FreeRTOSConfig.h`)이 `#define vPortSVCHandler SVC_Handler`처럼 CMSIS 벡터 이름에 연결한다. V11 포트는 `configCHECK_HANDLER_INSTALLATION`이 켜져 있으면 `xPortStartScheduler()`에서 벡터 테이블의 SVC · PendSV 칸이 정말 이 함수인지 `configASSERT`로 확인한다(연결을 잊는 흔한 실수를 시작 시점에 잡는다).
- **우선순위 설정** — `xPortStartScheduler()`:

```c
    /* Make PendSV and SysTick the lowest priority interrupts, and make SVCall
     * the highest priority. */
    portNVIC_SHPR3_REG |= portNVIC_PENDSV_PRI;
    portNVIC_SHPR3_REG |= portNVIC_SYSTICK_PRI;
    portNVIC_SHPR2_REG = 0;

    /* Start the timer that generates the tick ISR.  Interrupts are disabled
     * here already. */
    vPortSetupTimerInterrupt();

    /* Initialise the critical nesting count ready for the first task. */
    uxCriticalNesting = 0;

    /* Start the first task. */
    prvPortStartFirstTask();
```

- **tick 타이머** — `vPortSetupTimerInterrupt()`(weak, 앱이 다른 타이머로 바꿀 수 있다):

```c
    /* Stop and clear the SysTick. */
    portNVIC_SYSTICK_CTRL_REG = 0UL;
    portNVIC_SYSTICK_CURRENT_VALUE_REG = 0UL;

    /* Configure SysTick to interrupt at the requested rate. */
    portNVIC_SYSTICK_LOAD_REG = ( configSYSTICK_CLOCK_HZ / configTICK_RATE_HZ ) - 1UL;
    portNVIC_SYSTICK_CTRL_REG = ( portNVIC_SYSTICK_CLK_BIT_CONFIG | portNVIC_SYSTICK_INT_BIT | portNVIC_SYSTICK_ENABLE_BIT );
```

§1에서 직접 한 순서 그대로다: 멈춤 → CVR = 0 → RVR = 주기 − 1 → CLKSOURCE · TICKINT · ENABLE.

- **tick 핸들러** — `xPortSysTickHandler()`. **결정만 하고 PENDSVSET**:

```c
void xPortSysTickHandler( void )
{
    /* The SysTick runs at the lowest interrupt priority, so when this interrupt
     * executes all interrupts must be unmasked.  There is therefore no need to
     * save and then restore the interrupt mask value as its value is already
     * known. */
    portDISABLE_INTERRUPTS();
    traceISR_ENTER();
    {
        /* Increment the RTOS tick. */
        if( xTaskIncrementTick() != pdFALSE )
        {
            traceISR_EXIT_TO_SCHEDULER();

            /* A context switch is required.  Context switching is performed in
             * the PendSV interrupt.  Pend the PendSV interrupt. */
            portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;
        }
        else
        {
            traceISR_EXIT();
        }
    }
    portENABLE_INTERRUPTS();
}
```

`xTaskIncrementTick()`(`tasks.c`)는 tick 카운터를 올리고, delayed list에서 시간이 된 태스크를 ready로 옮기고, "지금보다 높은 태스크가 ready가 됐거나 같은 우선순위 time slice가 끝났으면" `pdTRUE`를 돌려준다(C5). `portDISABLE_INTERRUPTS()`는 PRIMASK가 아니라 BASEPRI를 `configMAX_SYSCALL_INTERRUPT_PRIORITY`로 올리는 것이다 — 그보다 높은 "zero-latency" ISR은 tick 처리 중에도 들어온다(B3 · E2).

- **ISR과 태스크의 전환 요청** — `portYIELD()` = PENDSVSET + `dsb` + `isb`(§2.4). `portYIELD_FROM_ISR(x)` = `portEND_SWITCHING_ISR(x)` = x가 참이면 `portYIELD()`. **ISR이든 태스크든 tick이든, 전환은 결국 PENDSVSET 한 줄**이다.
- **실행** — `xPortPendSVHandler()`(naked 어셈블리): PSP를 읽어 R4~R11 저장 → BASEPRI 올림 → `vTaskSwitchContext()`로 `pxCurrentTCB` 교체 → 새 스택에서 R4~R11 복원 → `bx r14`. 한 줄씩 해부는 **C2**.

### 예제 11 — FreeRTOS가 실제로 설정한 값과 "tick 1000번에 PendSV 몇 번"

FreeRTOS를 올리고, 가장 높은 `report` 태스크에서 SHPR2 · SHPR3 · SysTick 레지스터를 읽는다. 그리고 trace 매크로 세 개로 (1) tick ISR 횟수(`traceTASK_INCREMENT_TICK`), (2) SysTick이 PENDSVSET을 쓴 횟수(`traceISR_EXIT_TO_SCHEDULER`), (3) PendSV 안의 `vTaskSwitchContext()` 횟수와 실제로 태스크가 바뀐 횟수(`traceTASK_SWITCHED_IN`)를 1000 tick 동안 센다. 시나리오 1은 같은 우선순위 바쁜 태스크 둘(time slicing), 시나리오 2는 200 Hz 주기 태스크 + 바쁜 배경 태스크다. 앱 폴더의 `FreeRTOSConfig.h`가 trace 매크로를 정의하고 템플릿 설정을 그대로 include한다.

```c
/* ex11 전용: 템플릿 설정 + 세 예외 경로를 세는 trace 매크로 */
#ifndef B4_EX11_CONFIG_H
#define B4_EX11_CONFIG_H
#include <stdint.h>
extern volatile uint32_t g_tick_isr, g_tick_pend, g_switch_calls, g_switch_real;
extern void * volatile g_last_tcb;
#define traceTASK_INCREMENT_TICK( x )   ( g_tick_isr++ )         /* SysTick 안 xTaskIncrementTick */
#define traceISR_EXIT_TO_SCHEDULER()    ( g_tick_pend++ )        /* SysTick이 PENDSVSET을 쓸 때 */
#define traceTASK_SWITCHED_IN()                                   /* PendSV 안 vTaskSwitchContext */ \
    do { g_switch_calls++; if( ( void * ) pxCurrentTCB != g_last_tcb ) { g_switch_real++; g_last_tcb = ( void * ) pxCurrentTCB; } } while( 0 )
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#endif
```

```c
/* ex11: FreeRTOS가 세 예외를 어떻게 설정하고, tick 1000번에 PendSV가 몇 번 도는가 */
#include "FreeRTOS.h"
#include "task.h"
#include "../b4.h"
volatile uint32_t g_tick_isr, g_tick_pend, g_switch_calls, g_switch_real;
void * volatile g_last_tcb;

static void busy(void *p) { (void)p; for (;;) { __asm volatile("nop"); } }
#if SCEN == 2
static void ctrl(void *p)                 /* 200 Hz 주기 태스크 */
{
    (void)p; TickType_t t = xTaskGetTickCount();
    for (;;) { vTaskDelayUntil(&t, pdMS_TO_TICKS(5)); }
}
#endif
static void report(void *p)
{
    (void)p;
    board_printf_raw("SHPR2=0x%x (SVCall prio %u) SHPR3=0x%x (SysTick %u, PendSV %u)\n",
                     SCB_SHPR2, SCB_SHPR2 >> 24, SCB_SHPR3, SCB_SHPR3 >> 24, (SCB_SHPR3 >> 16) & 0xFFu);
    board_printf_raw("SysTick CSR=0x%x RVR=%u\n", SYST_CSR, SYST_RVR);
    uint32_t a = g_tick_isr, b = g_tick_pend, c = g_switch_calls, d = g_switch_real;
    vTaskDelay(1000);
    board_printf_raw("SCEN %d over 1000 ticks: tick ISR=%u, tick->PENDSVSET=%u, "
                     "vTaskSwitchContext=%u, real task changes=%u\n", SCEN,
                     g_tick_isr - a, g_tick_pend - b, g_switch_calls - c, g_switch_real - d);
    board_exit(0);
}

int main(void)
{
    xTaskCreate(report, "report", 512, NULL, 4, NULL);
#if SCEN == 1
    xTaskCreate(busy, "busyA", 256, NULL, 1, NULL);   /* 같은 우선순위 둘: time slicing */
    xTaskCreate(busy, "busyB", 256, NULL, 1, NULL);
#else
    xTaskCreate(ctrl, "ctrl", 256, NULL, 3, NULL);    /* 주기 태스크 + 바쁜 배경 */
    xTaskCreate(busy, "bg", 256, NULL, 1, NULL);
#endif
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'for s in 1 2; do make -B -s -f /var/rtoslab/templates/freertos-mps2/Makefile \
  SRCS=/Users/donh/.rtos_scratch/B4/ex11/main.c OUT=/var/tmp/rtos/B4/ex11_$s EXTRA_CFLAGS=-DSCEN=$s 2>&1 | grep -c warning; \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B4/ex11_$s/app.elf; done'
```

```text
0
SHPR2=0x0 (SVCall prio 0) SHPR3=0xffff0000 (SysTick 255, PendSV 255)
SysTick CSR=0x7 RVR=24999
SCEN 1 over 1000 ticks: tick ISR=1000, tick->PENDSVSET=1000, vTaskSwitchContext=1001, real task changes=1001
[board_exit] code=0
0
SHPR2=0x0 (SVCall prio 0) SHPR3=0xffff0000 (SysTick 255, PendSV 255)
SysTick CSR=0x7 RVR=24999
SCEN 2 over 1000 ticks: tick ISR=1000, tick->PENDSVSET=200, vTaskSwitchContext=401, real task changes=401
[board_exit] code=0
```

출력에서 볼 것(첫 줄 `0`은 경고 개수):

- FreeRTOS가 설정한 값: **SVCall = 0(최고), SysTick = PendSV = 0xFF(최저)**, SysTick CSR = 7(ENABLE · TICKINT · CLKSOURCE = 코어 클럭), RVR = 24999. §1과 §2에서 손으로 한 설정과 같다.
- 시나리오 1: tick 1000번에 PENDSVSET 1000번 — 같은 우선순위 태스크가 둘이라 **매 tick이 time slice 끝**이다. 전환 1001번(+1은 report가 깨어난 것).
- 시나리오 2: tick 1000번 중 PENDSVSET은 **200번뿐** — ctrl이 5 ms마다 깨어날 때만 tick이 전환을 결정했다. 나머지 800 tick은 카운터만 올리고 PendSV 없이 끝났다. 그런데 전환은 401번이다: tick이 결정한 200번(bg → ctrl) + ctrl이 `vTaskDelayUntil()`로 **스스로 블록하며** `portYIELD()`로 요청한 200번(ctrl → bg) + report 1번. 전환을 결정하는 곳이 tick만이 아니라는 것, 그리고 모든 전환이 같은 PendSV를 지난다는 것이 숫자로 보인다.
- 이 설정에서 tick과 PendSV의 비용(tick당 약 1.5 µs, 순수 전환 약 1.9 µs)은 A7에서 같은 보드로 쟀다.

FreeRTOS가 **SysTick도 최저**에 두는 이유는 무엇일까? tick ISR은 커널 API를 부르므로 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높을 수 없다. 그 범위 안에서 가장 낮게 두면 tick 처리가 **어떤 앱 ISR도 늦추지 않는다**. 대가는 tick ISR의 지터(앱 ISR이 길면 tick이 늦게 처리된다)인데, §1.5에서 봤듯 자동 reload 덕분에 **지터는 쌓이지 않는다.** 참고로 SysTick과 PendSV가 같은 우선순위이고 둘 다 pending이면, Cortex-M은 예외 번호가 작은 쪽(PendSV 14)을 먼저 받는다. 이때 tick이 결정한 전환은 SysTick 안에서 PENDSVSET을 다시 써서 바로 이어지는 PendSV에서 처리된다.

### 5.2 Zephyr v4.4.2 — `arch/arm/core/cortex_m/`

Zephyr는 같은 세 예외를 쓰지만, 이름과 배치가 다르다.

- **벡터 테이블** — `arch/arm/core/cortex_m/vector_table.S`: SVC 칸에 `z_arm_svc`, PendSV 칸에 `z_arm_pendsv`(단 `CONFIG_USE_SWITCH`가 꺼져 있을 때), SysTick 칸에 `sys_clock_isr`(`CONFIG_CORTEX_M_SYSTICK_INSTALL_ISR`일 때).
- **우선순위** — `arch/arm/include/cortex_m/exception.h`의 `z_arm_exc_setup()`이 `NVIC_SetPriority(PendSV_IRQn, _EXC_PENDSV_PRIO)`(0xFF → 구현 비트로 잘려 최저)와 `NVIC_SetPriority(SVCall_IRQn, _EXC_SVC_PRIO)`(0, zero-latency IRQ를 켜면 그 레벨)를 한다. SysTick은 드라이버(`drivers/timer/cortex_m_systick.c`)가 `NVIC_SetPriority(SysTick_IRQn, _IRQ_PRIO_OFFSET)`으로 둔다. Mainline(M3 이상)에서 `_IRQ_PRIO_OFFSET` = 1(레벨 0은 fault용으로 예약) — **일반 IRQ 중 가장 높은 레벨**이다.
- **tick 경로** — `sys_clock_isr()` → `elapsed()`로 COUNTFLAG와 지난 cycle 정리 → `sys_clock_announce(dticks)`(커널 `kernel/timeout.c`가 만료된 타임아웃 처리, 스레드 깨움) → `z_arm_int_exit()`(= `z_arm_exc_exit()`, `exc_exit.c`): `_kernel.ready_q.cache != _kernel.cpus->current`이면 `SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk`. 일반 IRQ도 `_isr_wrapper()`가 끝에서 같은 `z_arm_exc_exit()`를 부른다. **결정은 ready queue 캐시, 실행은 PendSV** — FreeRTOS와 같은 구조다.
- **스레드의 자발적 전환** — `arch_swap()`(`arch/arm/include/cortex_m/kernel_arch_func.h`): 반환값 자리에 `-EAGAIN`을 적고, PENDSVSET을 쓰고, `irq_unlock(0)`으로 마스크를 풀어 PendSV가 바로 들어가게 한다. PendSV에서 돌아오면 그게 "다시 이 스레드가 선택된 시점"이다.
- **실행** — `z_arm_pendsv`(`swap_helper.S`): 옛 스레드 문맥 저장 → `_kernel.ready_q.cache`를 `_kernel.current`로 → PendSV pending 지움(§2.2) → 새 스레드 문맥 복원. 해부는 C2.
- **새 경로** — Zephyr에는 `CONFIG_USE_SWITCH`를 켜면 쓰는 `arch/arm/core/cortex_m/arm-m-switch.c`(파일 머리 저작권 표기 2025, `arch_switch()` 방식)도 있다. Cortex-M에서 기본은 꺼짐(이 노트의 빌드 `.config`: `# CONFIG_USE_SWITCH is not set`)이라 위의 PendSV 경로가 쓰인다.
- **SVC** — `z_arm_svc`(`svc.S`)가 쌓인 PC − 2에서 번호를 꺼내 1(irq_offload) · 2(oops/panic) · 3(시스템 콜)으로 나눈다. 시스템 콜 경로는 C8, 첫 스레드 시작(SVC를 쓰지 않음)은 C3.

### 예제 13 — Zephyr가 실제로 설정한 값

Zephyr 앱에서 SHPR2 · SHPR3와 SysTick을 직접 읽는다. 부팅 직후, 50 ms 잔 뒤, 300 µs 바쁜 대기 뒤의 LOAD를 본다.

```c
/* ex13: Zephyr(mps2/an385)는 세 예외를 어떻게 설정했나 — 레지스터를 직접 읽는다 */
#include <zephyr/kernel.h>
#define R32(a) (*(volatile uint32_t *)(a))

static void dump(const char *when)
{
    printk("%-18s SysTick CTRL=0x%x LOAD=%u (=%u us)\n", when, R32(0xE000E010),
           R32(0xE000E014), (R32(0xE000E014) + 1) / 25);
}

int main(void)
{
    uint32_t shpr2 = R32(0xE000ED1C), shpr3 = R32(0xE000ED20);
    printk("TICKS_PER_SEC=%d, hw cycles/s=%d, tickless=%d\n", CONFIG_SYS_CLOCK_TICKS_PER_SEC,
           sys_clock_hw_cycles_per_sec(), IS_ENABLED(CONFIG_TICKLESS_KERNEL));
    printk("prio: SVCall=0x%02x PendSV=0x%02x SysTick=0x%02x (NUM_IRQ_PRIO_BITS=%d)\n",
           shpr2 >> 24, (shpr3 >> 16) & 0xff, shpr3 >> 24, NUM_IRQ_PRIO_BITS);
    dump("boot:");
    k_sleep(K_MSEC(50));
    dump("after 50ms sleep:");
    k_busy_wait(300);
    dump("after busy 300us:");
    return 0;
}
```

`CMakeLists.txt`는 Zephyr 표준 3줄(`find_package(Zephyr)`, `project()`, `target_sources(app PRIVATE src/main.c)`), `prj.conf`는 빈 파일이다.

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/B4/ex13 /var/tmp/rtos/B4/ex13 >/var/tmp/rtos/B4/ex13.log 2>&1; \
  echo build=$?; tail -2 /var/tmp/rtos/B4/ex13.log; /var/rtoslab/bin/rtos-zrun /var/tmp/rtos/B4/ex13 5'
```

```text
build=0
           FLASH:       13360 B         4 MB      0.32%
             RAM:        6112 B         4 MB      0.15%
qemu-system-arm: warning: nic lan9118.0 has no peer
*** Booting Zephyr OS build v4.4.2 ***
TICKS_PER_SEC=100, hw cycles/s=25000000, tickless=1
prio: SVCall=0x00 PendSV=0xe0 SysTick=0x20 (NUM_IRQ_PRIO_BITS=3)
boot:              SysTick CTRL=0x7 LOAD=498963 (=19958 us)
after 50ms sleep:  SysTick CTRL=0x7 LOAD=498777 (=19951 us)
after busy 300us:  SysTick CTRL=0x7 LOAD=498777 (=19951 us)
```

출력에서 볼 것:

- **우선순위 배치가 FreeRTOS와 다르다.** SVCall 0x00(최고) · PendSV 0xE0(3비트 칩의 최저) · **SysTick 0x20(레벨 1 = 일반 IRQ 중 최고)**. Zephyr는 이 보드를 우선순위 3비트로 정의하므로(`NUM_IRQ_PRIO_BITS=3`) 값이 상위 3비트에만 들어간다. FreeRTOS는 SysTick을 최저에 두고, Zephyr는 높은 쪽에 둔다. Zephyr 쪽은 tick(시간 계산)이 다른 IRQ에 밀려 늦어지는 것을 줄이는 선택이고, 대신 tick 처리 동안 그보다 낮은 IRQ가 기다린다. 어느 쪽이든 **PendSV만은 둘 다 최저**다.
- **tickless**: `TICKS_PER_SEC=100`(Kconfig `SYS_CLOCK_TICKS_PER_SEC`는 `QEMU_TARGET`이면 기본 100)인데 LOAD는 한 tick(250000 count)이 아니라 약 498,800 count(약 20 ms)다. tickless 커널은 LOAD를 "다음 타임아웃까지"로 매번 다시 프로그램하기 때문에 LOAD가 고정값이 아니다. 이 값이 정확히 어떻게 정해지는지는 C6에서 `sys_clock_set_timeout()`을 따라가며 본다.
- FreeRTOS 예제와 같은 SysTick CSR = 7.

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="170" y="18" font-size="13" text-anchor="middle" font-weight="bold">FreeRTOS V11.3.2 (ARM_CM3)</text>
<text x="510" y="18" font-size="13" text-anchor="middle" font-weight="bold">Zephyr v4.4.2 (cortex_m)</text>
<rect x="10" y="30" width="320" height="52" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="170" y="50" font-size="12" text-anchor="middle">SysTick_Handler = xPortSysTickHandler</text>
<text x="170" y="70" font-size="12" text-anchor="middle">port.c: BASEPRI로 커널 IRQ 마스크</text>
<rect x="10" y="102" width="320" height="52" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="170" y="122" font-size="12" text-anchor="middle">xTaskIncrementTick()  (tasks.c)</text>
<text x="170" y="142" font-size="12" text-anchor="middle">깨울 태스크 · time slice 판단 → pdTRUE?</text>
<rect x="10" y="174" width="320" height="52" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="170" y="194" font-size="12" text-anchor="middle">portNVIC_INT_CTRL_REG = PENDSVSET</text>
<text x="170" y="214" font-size="12" text-anchor="middle">결정 끝: 깃발만 세우고 ISR 종료</text>
<rect x="10" y="246" width="320" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="170" y="266" font-size="12" text-anchor="middle">xPortPendSVHandler  (port.c, naked asm)</text>
<text x="170" y="286" font-size="12" text-anchor="middle">vTaskSwitchContext() → pxCurrentTCB 교체 (C2)</text>
<line x1="170" y1="82" x2="170" y2="100" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="170" y1="154" x2="170" y2="172" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="170" y1="226" x2="170" y2="244" stroke="currentColor" marker-end="url(#ah7)"/>
<rect x="350" y="30" width="320" height="52" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="510" y="50" font-size="12" text-anchor="middle">sys_clock_isr  (cortex_m_systick.c)</text>
<text x="510" y="70" font-size="12" text-anchor="middle">elapsed(): COUNTFLAG · 지난 cycle 정리</text>
<rect x="350" y="102" width="320" height="52" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="510" y="122" font-size="12" text-anchor="middle">sys_clock_announce(dticks)</text>
<text x="510" y="142" font-size="12" text-anchor="middle">타임아웃 만료 처리 · 스레드를 ready로</text>
<rect x="350" y="174" width="320" height="52" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="510" y="194" font-size="12" text-anchor="middle">z_arm_int_exit()  (exc_exit.c)</text>
<text x="510" y="214" font-size="12" text-anchor="middle">ready_q.cache ≠ current 이면 PENDSVSET</text>
<rect x="350" y="246" width="320" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="510" y="266" font-size="12" text-anchor="middle">z_arm_pendsv  (swap_helper.S)</text>
<text x="510" y="286" font-size="12" text-anchor="middle">_kernel.current 교체 · PendSV 지움 (C2)</text>
<line x1="510" y1="82" x2="510" y2="100" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="510" y1="154" x2="510" y2="172" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="510" y1="226" x2="510" y2="244" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="340" y="322" font-size="12" text-anchor="middle">주황 = SysTick 예외 안 (결정) · 회색 = 깃발 · 초록 = PendSV 예외 안 (실행)</text>
<text x="340" y="342" font-size="12" text-anchor="middle">태스크의 yield도 같은 깃발: portYIELD() / arch_swap() → PENDSVSET</text>
</svg>
```

그림 9 — 두 커널의 tick → 전환 경로. 함수 이름은 다르지만 모양은 같다: SysTick 안에서 결정하고 깃발을 세우면, PendSV가 실행한다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">세 예외의 우선순위 (위 = 높음, 숫자 작을수록 높다) — 예제 11 · 13 실측</text>
<line x1="120" y1="40" x2="120" y2="270" stroke="currentColor"/>
<text x="112" y="45" font-size="12" text-anchor="end">0x00</text>
<text x="112" y="76" font-size="12" text-anchor="end">0x20</text>
<text x="112" y="245" font-size="12" text-anchor="end">0xE0</text>
<text x="112" y="270" font-size="12" text-anchor="end">0xFF</text>
<line x1="120" y1="72" x2="360" y2="72" stroke="#888" stroke-dasharray="4 3"/>
<text x="10" y="100" font-size="12">FreeRTOS:</text>
<text x="10" y="116" font-size="12">0x20 위는</text>
<text x="10" y="132" font-size="12">커널 API 금지</text>
<rect x="150" y="32" width="200" height="22" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="250" y="48" font-size="12" text-anchor="middle">SVCall 0x00</text>
<rect x="150" y="252" width="200" height="22" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="250" y="268" font-size="12" text-anchor="middle">SysTick · PendSV 0xFF</text>
<text x="250" y="160" font-size="13" text-anchor="middle" font-weight="bold">FreeRTOS CM3</text>
<text x="250" y="180" font-size="12" text-anchor="middle">앱 ISR은 0x20~0xF0 사이</text>
<rect x="430" y="32" width="200" height="22" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="530" y="48" font-size="12" text-anchor="middle">SVCall · fault 0x00</text>
<rect x="430" y="62" width="200" height="22" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="530" y="78" font-size="12" text-anchor="middle">SysTick 0x20 (레벨 1)</text>
<rect x="430" y="228" width="200" height="22" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="530" y="244" font-size="12" text-anchor="middle">PendSV 0xE0 (3비트 최저)</text>
<text x="530" y="160" font-size="13" text-anchor="middle" font-weight="bold">Zephyr mps2/an385</text>
<text x="530" y="180" font-size="12" text-anchor="middle">IRQ_CONNECT 우선순위 + 1</text>
<text x="390" y="290" font-size="12" text-anchor="middle">공통점: SVCall은 최고(바로 들어가야 하니까), PendSV는 최저(다 끝난 뒤 전환)</text>
</svg>
```

그림 10 — FreeRTOS와 Zephyr가 실제로 설정한 우선순위(예제 11 · 13에서 레지스터로 읽은 값). SysTick의 위치만 다르고, SVCall 최고 · PendSV 최저는 같다.

### 5.3 다른 CPU 타깃에서는 — 세 예외가 없는 곳

SysTick · PendSV · SVC는 **Cortex-M 전용**이다. 다른 코어로 포팅하면 같은 세 역할(tick · 미룬 전환 · 동기 커널 진입)을 다른 수단으로 만든다. FreeRTOS V11.3.2 포트에서 확인한 것만 적는다.

| 코어 | tick | 전환 요청 (yield) | 전환 실행 시점 | 근거 |
|---|---|---|---|---|
| Cortex-M3/M4/M7 | SysTick | PENDSVSET | PendSV (최저) | `ARM_CM3/port.c` |
| Cortex-M0/M0+ (ARMv6-M) | SysTick (칩에 따라 없음) | PendSV, MPU 켜면 `svc portSVC_YIELD` | PendSV | `ARM_CM0/portmacro.h` |
| Cortex-M33 (ARMv8-M) | SysTick (보안 상태별로 있을 수 있음) | PENDSVSET | PendSV, SVC로 보안 컨텍스트 관리 | `ARMv8M/non_secure/port.c` |
| Cortex-R5 | 칩 회사 타이머 + 인터럽트 컨트롤러 | `SWI 0` (= svc) | IRQ 복귀 경로 · SWI 핸들러 | `ARM_CR5/portmacro.h` |
| Cortex-A53 (AArch64) | Generic Timer | `SVC 0` (EL1, 아니면 `SMC 0`) | IRQ 복귀 경로 · SVC 핸들러 | `ARM_AARCH64/portmacro.h` |
| RISC-V | `mtime`/`mtimecmp` (CLINT) | `ecall` | trap 핸들러 끝 (`vTaskSwitchContext()` 직접) | `RISC-V/portmacro.h`, `portASM.S` |

PendSV가 없는 코어에서는 "ISR 중첩 안에서 전환하지 않기"를 어떻게 지킬까? RISC-V는 trap에 들어가면 하드웨어가 `mstatus.MIE`를 꺼서 **기본적으로 중첩이 없다**. 그래서 trap 핸들러의 끝이 곧 "모든 처리가 끝나고 태스크로 돌아가는 지점"이다. FreeRTOS RISC-V 포트의 `portEND_SWITCHING_ISR()`은 PendSV를 pending하는 대신 그 자리에서 `vTaskSwitchContext()`를 부르고, trap 출구가 새 태스크 문맥을 복원한다. 대신 소프트웨어 인터럽트(CLINT의 MSIP)를 쓰면 PendSV와 비슷한 "미뤄 둔 전환"도 만들 수 있다. 직접 확인해 보자.

### 예제 14 — RISC-V(QEMU virt rv32): mtimecmp = tick, MSIP = 미룬 전환, ecall = 시스템 콜

timer trap 안에서 소프트웨어 인터럽트(MSIP)를 두 번 요청하고, 그게 언제 · 몇 번 도는지 기록한다. `ecall`로 시스템 콜도 하나 한다. 빌드는 Zephyr SDK의 `riscv64-zephyr-elf-gcc`(`-march=rv32imac_zicsr -mabi=ilp32`)와 4줄짜리 링커 스크립트(`.text`를 `0x80000000`에)로 했다.

```c
/* ex14: RISC-V(QEMU virt rv32)에는 SysTick·PendSV·SVC가 없다 — mtimecmp · MSIP · ecall로 같은 역할 */
#include <stdint.h>
#define R32(a) (*(volatile uint32_t *)(a))
#define UART 0x10000000u
#define MSIP 0x02000000u                     /* CLINT 소프트웨어 인터럽트 (PendSV 자리) */
#define MTIMECMP_LO 0x02004000u
#define MTIME_LO 0x0200BFF8u                 /* QEMU virt: 10 MHz */
static void putc_(char c) { *(volatile uint8_t *)UART = (uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void putu(uint32_t v) { char b[11]; int i = 0; do b[i++] = '0' + v % 10; while (v /= 10); while (i) putc_(b[--i]); }
static volatile uint32_t ticks, soft_runs, log_n; static char lg[32];

__attribute__((interrupt("machine"), aligned(4))) void trap(void)
{
    uint32_t cause; __asm volatile("csrr %0, mcause" : "=r"(cause));
    if (cause == 0x80000007u) {              /* machine timer = tick */
        R32(MTIMECMP_LO) = R32(MTIME_LO) + 10000u;   /* 다음 1 ms (상위 32비트는 0 가정) */
        lg[log_n++] = 'T';
        if (++ticks == 3) { R32(MSIP) = 1; R32(MSIP) = 1; lg[log_n++] = 's'; }  /* "전환 요청" 2번 */
        lg[log_n++] = 't';
    } else if (cause == 0x80000003u) {       /* machine software = 미뤄 둔 전환 */
        R32(MSIP) = 0; soft_runs++; lg[log_n++] = 'P';
    } else if (cause == 11u) {               /* ecall from M-mode = 시스템 콜 */
        uint32_t epc; __asm volatile("csrr %0, mepc" : "=r"(epc));
        __asm volatile("csrw mepc, %0" :: "r"(epc + 4));   /* ecall 다음으로 (SVC와 달리 수동) */
    }
}

static uint32_t sys_add(uint32_t a, uint32_t b)
{
    register uint32_t a0 __asm__("a0") = a, a1 __asm__("a1") = b;
    __asm volatile("ecall" : "+r"(a0) : "r"(a1) : "memory");
    return a0;                               /* 핸들러가 a0를 안 바꿨으니 a 그대로 돌아온다 */
}

void main_c(void)
{
    __asm volatile("csrw mtvec, %0" :: "r"(trap));
    puts_("ecall returned a0="); putu(sys_add(40, 2)); puts_(" (mepc+4 makes it resume)\n");
    R32(MTIMECMP_LO + 4) = 0; R32(MTIMECMP_LO) = R32(MTIME_LO) + 10000u;
    __asm volatile("csrs mie, %0" :: "r"((1u << 7) | (1u << 3)));   /* MTIE, MSIE */
    __asm volatile("csrs mstatus, %0" :: "r"(1u << 3));             /* MIE */
    while (ticks < 5) { __asm volatile("wfi"); }
    lg[log_n] = 0;
    puts_("trap order: "); puts_(lg); puts_("  soft-irq runs="); putu(soft_runs); puts_("\n");
    R32(0x100000u) = 0x5555u;                /* sifive_test: QEMU 종료 */
    for (;;) { }
}
__asm__(".section .text.start\n.globl _start\n_start:\n la sp, 0x80100000\n j main_c\n");
```

```sh
limactl shell elinux -- bash -lc 'T=/var/rtoslab/zephyr-sdk-1.0.1/gnu/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-; \
  ${T}gcc -march=rv32imac_zicsr -mabi=ilp32 -O2 -Wall -Wextra -ffreestanding -nostdlib \
  -T /Users/donh/.rtos_scratch/B4/ex14/virt.ld -o /var/tmp/rtos/B4/ex14/app.elf /Users/donh/.rtos_scratch/B4/ex14/main.c && \
  timeout 5 qemu-system-riscv32 -machine virt -bios none -nographic -monitor none -serial stdio \
  -kernel /var/tmp/rtos/B4/ex14/app.elf; echo exit=$?'
```

```text
ecall returned a0=40 (mepc+4 makes it resume)
trap order: TtTtTstPTtTt  soft-irq runs=1
exit=0
```

출력에서 볼 것: `T`/`t`는 timer trap의 시작/끝, `s`는 그 안에서 MSIP를 두 번 쓴 지점, `P`는 소프트웨어 인터럽트 처리다. 세 번째 timer trap 안에서 요청한 소프트웨어 인터럽트는 **그 trap이 끝난 뒤(`t` 다음)에 한 번만** 돌았다 — PendSV와 같은 "미루기 + 합치기"를 trap 비중첩 성질이 만들어 준다. `ecall`은 SVC와 두 가지가 다르다. (1) 복귀 주소를 하드웨어가 "다음 명령"으로 해 주지 않아 `mepc + 4`를 직접 써야 한다(ecall은 늘 32비트 명령, 압축형이 없다). (2) 레지스터를 메모리에 자동으로 쌓지 않는다. `interrupt("machine")` 속성의 C 핸들러는 컴파일러가 레지스터를 저장 · 복원하므로 a0에 결과를 써도 복귀 때 덮인다 — 그래서 결과가 42가 아니라 40이다. 반환값을 돌려주려면 FreeRTOS RISC-V 포트처럼 trap 입구에서 레지스터를 자기 프레임에 저장하고 그 프레임의 a0 자리를 고쳐야 한다(B10에서 자세히). Cortex-M의 "하드웨어 자동 스태킹 덕분에 frame[0]만 고치면 된다"가 얼마나 편한지 대조된다.

---

## 6. 실무에서 다시 보기

### 6.1 드론 비행 제어기 — tick은 느리게, 제어 루프는 tick 밖에서

예를 들어 STM32H7급 비행 제어기라면 IMU가 4~8 kHz로 data-ready 인터럽트를 낸다. 이 rate loop를 **tick으로 돌리면 안 된다.** 1 kHz tick으로는 8 kHz를 못 만들고, tick을 8 kHz로 올리면 시스템 전체가 tick 세금을 낸다(A7에서 25 MHz 보드의 10 kHz tick은 FreeRTOS 1.47%, Zephyr 4.45%의 CPU). 무엇보다 tick의 해상도와 지터가 그대로 제어 루프의 지터가 된다. 그래서 구조는 이렇게 된다.

- IMU EXTI(또는 SPI DMA 완료) ISR이 **높은 우선순위**에서 돈다. 커널 API를 부르면 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 아래(숫자 큰 쪽)에 두고 `vTaskNotifyGiveFromISR()` + `portYIELD_FROM_ISR()`로 rate 태스크를 깨운다. 그러면 전환은 PendSV에서, 모든 ISR이 끝난 뒤 한 번 일어난다(예제 6의 모양).
- SysTick(1 kHz)과 PendSV는 최저라서 IMU ISR을 **절대 늦추지 않는다**. 반대로 rate loop가 ISR 안에서 직접 돌 만큼 빡빡하면(Betaflight식) 그 ISR을 커널 위(zero-latency)에 두고 커널 API를 쓰지 않는다(E2).
- IMU 샘플 타임스탬프는 §1.4의 "tick + CVR 안전 읽기"나 DWT CYCCNT로 µs 단위로 찍는다. tick(ms)만 쓰면 8 kHz 샘플 8개가 같은 타임스탬프를 갖는다.

### 6.2 로봇 관절 모터 컨트롤러 — 20 kHz FOC와 PendSV

FOC 전류 루프는 PWM 주기(20 kHz = 50 µs)마다 ADC 완료 ISR에서 돈다. 이 ISR이 RTOS 때문에 흔들리면 토크 리플이 생긴다. 확인할 것은 두 가지다.

- FOC ISR 우선순위가 BASEPRI 임계값 **위**인가. 그래야 커널 크리티컬 섹션(tick 처리 · PendSV 전환 중 BASEPRI 올림)이 FOC를 막지 못한다. PendSV 자체는 최저라 선점당하지만, PendSV 안의 짧은 BASEPRI 구간은 임계값 아래 ISR만 막는다.
- FOC ISR이 FreeRTOS API를 부르지 않는가. 부르면 `vPortValidateInterruptPriority()`의 `configASSERT`에 걸린다(우선순위 검사 — B3, E1).

### 6.3 센서 허브 MCU — Linux SoC에 넘길 타임스탬프

Linux SoC 옆의 센서 허브는 샘플마다 "MCU 시계 기준 µs 타임스탬프"를 붙여 보내고, 호스트가 그걸 자기 시계로 매핑한다(Q7). MCU 쪽 시계를 만드는 방법이 §1.4다. Zephyr라면 `k_cycle_get_32()`가 Cortex-M SysTick 드라이버에서 `cycle_count + elapsed()`로 이 일을 한다 — 우리가 예제 3에서 손으로 만든 것과 같은 계산을 COUNTFLAG까지 고려해서 한다. 32비트 cycle은 25 MHz에서 171초마다 wrap하므로 장기 타임스탬프는 64비트로 확장한다(`k_cycle_get_64()`, E5).

### 6.4 Don의 SSD 컨트롤러 — PendSV 없는 세상의 같은 패턴

Don이 쓰던 Cortex-R 기반 SSD 컨트롤러에는 SysTick도 PendSV도 없다(B9). 그래도 같은 패턴이 있었다.

- **tick** — 칩 회사 타이머 인터럽트가 주기적으로 시간 기반을 올리고 타임아웃(커맨드 타임아웃, 백그라운드 GC 주기)을 검사했다.
- **PendSV 자리** — ISR이 세운 "deferred work" 비트를 실행부 루프가 처리했다. 여러 ISR이 같은 비트를 세워도 루프는 한 번 처리했다(예제 7의 합쳐짐). Cortex-M의 PendSV는 이 루프의 "다음 바퀴"를 하드웨어가 tail-chain으로 바로 이어 주는 버전이다.
- **SVC 자리** — 특권 분리가 있는 펌웨어라면 SWI(Arm 상태의 svc)로 커널 서비스를 부른다. FreeRTOS Cortex-R5 포트가 `portYIELD()`를 `SWI 0`으로 만드는 것과 같다.

면접에서 "PendSV를 써 본 적 있나?"라는 질문에 Don은 "Cortex-R 실행부에서 같은 일을 deferred-work 비트와 루프로 했고, Cortex-M은 그걸 최저 우선순위 예외 하나로 하드웨어가 해 준다"라고 연결해 답할 수 있다.

---

## 7. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| RVR에 `f / Hz`를 그대로 넣음 | 시계가 하루에 수 초씩 늦다 | 주기 = RVR + 1, 40 ppm(25 MHz · 1 kHz) | `f / Hz − 1` (예제 2 · 15) |
| ISR에서 타이머를 다시 로드 | 주기 태스크가 점점 늦어진다 | 진입 지연이 매 주기 쌓임 | 자동 reload를 믿고 손대지 않는다 (예제 2 mode 2) |
| CLKSOURCE와 RVR 계산 클럭 불일치 | tick이 정확히 8배(또는 n배) 느리다 | 참조 클럭으로 세면서 코어 클럭으로 계산 | `configSYSTICK_CLOCK_HZ` 확인, 예제 1처럼 실제 속도 측정 |
| ticks와 CVR을 따로 읽음 | 타임스탬프가 가끔 1 ms 거꾸로 간다 | 두 읽기 사이 wrap | 두 번 읽기 + COUNTFLAG (예제 3) |
| SysTick 지연 함수가 24비트 마스크로 wrap 처리 | 가끔 지연이 짧다 (센서 초기화 간헐 실패) | RVR ≠ 2^24 − 1 | 주기(RVR + 1) 기준 누적 (예제 4) |
| PendSV 우선순위 미설정 (리셋 값 0) | 랜덤 HardFault, 태스크 문맥 깨짐 | PendSV가 ISR을 선점해 중첩 안에서 전환 | SHPR3[23:16] = 0xFF (예제 8) |
| SVC를 ISR · 크리티컬 섹션 안에서 실행 | HardFault, HFSR.FORCED, CFSR = 0 | SVC는 동기, 못 들어가면 승격 | ISR에서는 `...FromISR` 직접 호출 (예제 10) |
| SVC 번호를 R0 등 레지스터에서 찾음 | 엉뚱한 시스템 콜 실행 | 하드웨어는 번호를 저장하지 않음 | 쌓인 PC − 2의 하위 바이트 (예제 9 · 10) |
| SVC 반환값을 레지스터 R0에 씀 | 호출 쪽이 받은 값이 인자 그대로 | 복귀 때 프레임에서 R0 복원 | `frame[0] = 결과` (예제 9) |
| 보호 없는 FreeRTOS CM3에서 앱이 `svc` 사용 | 태스크가 옛 상태로 돌아가거나 HardFault | `vPortSVCHandler()`는 번호를 안 보고 첫 태스크 복원을 실행 | 앱용 SVC가 필요하면 MPU 포트나 다른 IRQ 사용 |
| DWT CYCCNT를 켜고 확인 안 함 | 측정값이 늘 0 | TRCENA 미설정, M0 계열, 에뮬레이터 | 켠 뒤 한 번 증가 확인 (예제 5) |
| HAL과 RTOS가 SysTick 공유 | `HAL_Delay()`가 멈추거나 tick이 두 배 | 두 핸들러가 한 벡터를 다툼 | HAL 시간 기반을 다른 TIM으로 |

---

## 8. 면접에서 이렇게 말한다

**Q.** "Why is the context switch done in PendSV instead of directly in the SysTick or peripheral ISR?"

**A.** ISR 안에서 바로 전환하면 중첩된 ISR 위에서 태스크 스택을 바꾸게 될 수 있고, 같은 순간에 여러 ISR이 요청하면 여러 번 바꾸게 된다. PendSV는 소프트웨어로 pending 시키는 최저 우선순위 예외라서, 모든 ISR이 끝나고 Thread로 돌아가기 직전에, 몇 번 요청했든 한 번 실행된다. 그래서 ISR과 tick은 "결정 + PENDSVSET"만 하고, 전환은 한 곳에서 한다. 25 MHz Cortex-M3에서 높은 ISR 안에서 1.9 µs에 요청한 PendSV가, 그 ISR과 뒤이은 SysTick이 끝난 20.4 µs에 딱 한 번 도는 것을 확인했다.

> Switching inside an ISR is unsafe because that ISR may be nested on top of another one, and several ISRs may ask for a switch at the same time. PendSV is a software-pended exception at the lowest priority, so it runs exactly once, after every other handler has finished, right before returning to thread mode. So SysTick and peripheral ISRs only decide and set PENDSVSET; the actual switch happens in one place. I measured it on a Cortex-M3: a PendSV requested at 1.9 microseconds inside a high-priority ISR ran once at 20.4 microseconds, after that ISR and a pending SysTick both completed.

**꼬리 질문**: "PendSV가 늦게 도는 게 지연을 늘리지 않나?" → 늘리는 건 "높은 ISR이 남은 일을 마치는 시간"뿐이고, 그건 어차피 기다려야 하는 시간이다. 실칩에서는 tail-chaining으로 프레임을 다시 쌓지 않아 오히려 싸다.

**Q.** "What happens if PendSV is not the lowest priority?"

**A.** PendSV가 다른 ISR보다 높으면 그 ISR 한가운데서 PendSV가 끼어들어 전환한다. 실험에서 우선순위를 리셋 값 0으로 두자 PendSV가 ISR 전반부와 후반부 사이에서 돌았고, EXC_RETURN이 `0xFFFFFFF1`(Handler 모드로 복귀)이었다. RTOS의 PendSV 핸들러는 "Thread에서 PSP로 들어왔다"고 가정하고 PSP에 레지스터를 저장하므로, 이 상태에서 전환하면 ISR 후반부가 다른 태스크 위에서 끝나고 문맥이 깨진다. 리셋 값이 0이라서 포팅할 때 SHPR3를 설정하지 않으면 바로 이 버그다.

> If PendSV can preempt another ISR, the switch happens in the middle of that ISR. I reproduced it by leaving PendSV at its reset priority of zero: PendSV ran between the two halves of a low-priority ISR, and its EXC_RETURN was 0xFFFFFFF1, return to handler mode. The PendSV handler assumes it came from a thread on PSP, so switching there corrupts the interrupted context. Since the reset value is zero, forgetting to program SHPR3 during a port gives you exactly this bug.

**Q.** "How does an SVC handler know the SVC number and the arguments? How does it return a value?"

**A.** 하드웨어는 번호를 어디에도 넣어 주지 않는다. 핸들러가 EXC_RETURN bit 2로 프레임이 MSP인지 PSP인지 고르고, 프레임의 쌓인 PC(offset 24)에서 2를 뺀 주소의 halfword(`0xDFxx`)를 읽어 하위 바이트를 번호로 쓴다. 인자는 프레임의 R0~R3 슬롯에서 읽고, 반환값은 프레임의 R0 슬롯에 쓴다. 예외 복귀 때 하드웨어가 그 슬롯을 R0로 꺼내 주기 때문이다. 컴파일러도 `ldr r3, [r0, #24]` 다음 `ldrb r3, [r3, #-2]`를 만들었고, Zephyr와 FreeRTOS-MPU가 손으로 쓴 것과 같은 코드다.

> The hardware doesn't store the number. The handler checks bit 2 of EXC_RETURN to pick MSP or PSP, takes the stacked PC at offset 24, and reads the halfword at PC minus 2, which is the 0xDFxx SVC opcode; the low byte is the number. Arguments come from the stacked R0 to R3 slots, and the return value goes into the stacked R0 slot, because exception return reloads R0 from the frame. Zephyr's z_arm_svc and the FreeRTOS MPU port do exactly that with an ldrb at minus two.

**꼬리 질문**: "인자가 4개보다 많으면?" → Zephyr는 R4·R5에 인자 5·6을, R6에 시스템 콜 ID를 넣고 모든 호출을 `svc #3` 하나로 한다. 이 레지스터들은 하드웨어 프레임에 없으므로 핸들러가 C 코드로 덮어쓰기 전에 어셈블리에서 꺼내야 한다.

**Q.** "Walk me through configuring SysTick for a 1 kHz tick on a 168 MHz part."

**A.** 멈추고(CSR = 0), CVR에 0을 써서 카운터와 COUNTFLAG를 지우고, RVR = 168,000,000 / 1000 − 1 = 167,999를 넣고, CSR에 CLKSOURCE(코어 클럭) · TICKINT · ENABLE을 켠다. −1은 카운터가 RVR부터 0까지 RVR + 1 클럭을 세기 때문이다. 빼먹으면 25 MHz 기준 40 ppm, 1시간에 144 ms 늦는다. 그리고 RVR은 24비트라서 고클럭에서 낮은 tick rate는 안 들어갈 수 있다(480 MHz에서 최대 35 ms). 우선순위는 RTOS가 정한다: FreeRTOS는 최저, Zephyr는 일반 IRQ 중 최고 레벨이다.

> Stop it, write zero to the current value register to clear the counter and COUNTFLAG, load 168 million over 1000 minus one, which is 167,999, then enable it with the processor clock source and TICKINT. The minus one is because it counts from the reload value down to zero, which is reload-plus-one clocks; forgetting it costs 40 ppm at 25 MHz, about 144 milliseconds an hour. The counter is 24 bits, so at 480 MHz the longest period is about 35 milliseconds. Priority is the RTOS's choice — FreeRTOS puts SysTick at the lowest level, Zephyr at the highest regular IRQ level.

**Q.** "The RTOS already owns SysTick. How do you implement an accurate microsecond delay?"

**A.** SysTick을 다시 설정하지 않고 읽기만 한다. CVR이 줄어든 양을 누적하되 wrap은 주기(RVR + 1)를 더해 처리한다. 25 MHz에서 1 µs부터 2.5 ms까지 오차가 함수 비용인 10~17 cycle로 일정했고, tick은 그동안 계속 돌았다. 24비트 마스크로 wrap을 처리하는 흔한 버전은 tick 경계를 넘을 때만 140 µs 일찍 끝나는 간헐 버그를 냈다. DWT CYCCNT가 있으면 그게 더 간단하다. 단 이건 바쁜 대기라서 tick보다 훨씬 짧은 지연에만 쓰고, ms 단위는 `vTaskDelay`로 CPU를 양보한다.

> I don't touch the configuration; I only read the current value and accumulate how far it has counted down, adding the period, reload plus one, whenever it wraps. On a 25 MHz Cortex-M3 that was accurate from 1 microsecond to 2.5 milliseconds with a constant 10 to 17 cycle overhead, while the tick kept running. The common bug masks the difference with 24 bits, which only works if the reload is the full 24-bit range — mine ended 140 microseconds early whenever the delay crossed a tick. If DWT CYCCNT exists I'd use that. And it's a busy-wait, so only for delays much shorter than a tick; anything in milliseconds should block.

**Q.** "What's the difference between SVC and PendSV?"

**A.** SVC는 명령이 일으키는 **동기** 예외라서 그 명령에서 바로 들어가야 하고, 들어갈 수 없으면(PRIMASK가 켜져 있거나 더 높은 ISR 안) HardFault로 승격된다. 실험에서 두 경우 모두 HFSR.FORCED, CFSR = 0이었다. PendSV는 비트를 써서 pending하는 **비동기** 예외라서 우선순위가 허락될 때까지 기다리고 요청이 합쳐진다. 그래서 RTOS는 "지금 당장 커널로"(첫 태스크 시작, 시스템 콜)에 SVC를 최고 우선순위로, "나중에 한 번"(전환)에 PendSV를 최저 우선순위로 쓴다.

> SVC is synchronous: it's taken at the instruction, and if it can't be taken — PRIMASK set, or called from a higher-priority handler — it escalates to HardFault. I saw HFSR FORCED with CFSR zero in both cases. PendSV is asynchronous: you set a pending bit, it waits until priority allows, and multiple requests merge into one. That's why RTOSes use SVC at the highest priority for "enter the kernel now" — starting the first task, system calls — and PendSV at the lowest for "switch once, later."

**Q.** "How does FreeRTOS start the first task on a Cortex-M3, and why use SVC for it?"

**A.** `xPortStartScheduler()`가 PendSV · SysTick을 최저, SVCall을 최고로 두고 SysTick을 켠 다음, `prvPortStartFirstTask()`가 VTOR로 벡터 테이블의 초기 MSP 값을 읽어 MSP를 리셋 상태로 되돌리고 인터럽트를 켠 뒤 `svc 0`을 한다. `vPortSVCHandler()`는 첫 태스크 TCB의 스택에서 R4~R11을 꺼내 PSP를 설정하고 EXC_RETURN을 Thread · PSP로 만들어 복귀한다. 예외 복귀를 쓰면 하드웨어가 가짜 프레임에서 PC · xPSR · R0를 꺼내 주고 모드와 스택까지 바꿔 주므로, 첫 태스크도 이후 모든 전환과 같은 복귀 경로를 탄다. 참고로 Zephyr는 SVC 없이 PSP를 설정하고 `bx`로 바로 들어간다.

> xPortStartScheduler sets PendSV and SysTick to the lowest priority and SVCall to the highest, starts SysTick, then prvPortStartFirstTask resets MSP from the vector table through VTOR, enables interrupts and executes svc 0. The SVC handler pops R4 to R11 from the first task's stack, sets PSP, and returns with an EXC_RETURN for thread mode on PSP. Using an exception return means the hardware pops PC, xPSR and R0 from the fake initial frame and switches mode and stack for you, so the first task enters the same way every later switch does. Zephyr, by contrast, just sets PSP and branches.

**꼬리 질문**: "그 SVC 핸들러는 번호를 검사하나?" → CM3 비MPU 포트는 검사하지 않는다. 그래서 그 포트 위에서 앱이 `svc`를 쓰면 안 된다. MPU 포트는 100~103 번호로 나눈다.

**Q.** "How do you measure cycle counts on a Cortex-M?"

**A.** DWT CYCCNT다. DEMCR의 TRCENA(bit 24)로 DWT를 켜고, M7처럼 잠금이 있으면 LAR에 `0xC5ACCE55`를 쓰고, DWT_CTRL의 NOCYCCNT를 확인한 뒤, CYCCNT를 0으로 하고 CYCCNTENA를 켠다. 32비트라 480 MHz에서 9초마다 wrap하니 빼기는 unsigned로 한다. M0/M0+에는 없어서 SysTick이나 타이머로 잰다. 그리고 켠 뒤 실제로 올라가는지 확인한다 — QEMU처럼 0으로 읽히는 환경에서는 NOCYCCNT 검사도 통과해 버린다.

> DWT CYCCNT. Set TRCENA, bit 24 of DEMCR, unlock with 0xC5ACCE55 on cores like the M7 that have a lock, check NOCYCCNT in DWT_CTRL, zero CYCCNT and set CYCCNTENA. It's 32 bits, so it wraps every nine seconds at 480 MHz — always subtract as unsigned. Cortex-M0 and M0+ don't have it, so there I use SysTick or a timer. And I always check it's actually counting; on an emulator that reads it as zero, the NOCYCCNT check passes anyway.

---

## 9. 직접 해보기

1. **개념 — reload 계산**: 72 MHz 코어, SysTick 참조 클럭 = 코어 ÷ 8. CLKSOURCE = 0으로 1 kHz tick을 만들 RVR은? CLKSOURCE = 1이면? 각 경우 가장 긴 가능한 주기는?
   정답/힌트: 참조 클럭 9 MHz → RVR = 8999, 최대 2^24 / 9 MHz ≈ 1.86 s. 코어 클럭 → RVR = 71999, 최대 약 233 ms.
2. **코드 — 마스킹 중에도 안전한 타임스탬프**: 예제 3의 `now_safe()`를 인터럽트가 꺼진 상태에서도 맞게 고쳐라. CVR을 읽은 뒤 CSR의 COUNTFLAG가 1이면 어떻게 해야 하나? 그리고 이 함수가 COUNTFLAG를 "훔치는" 문제를 어떻게 피하나?
   정답/힌트: COUNTFLAG가 1이면 tick 하나를 더하고 CVR을 다시 읽는다. 깃발을 본 결과를 전역 누적값(Zephyr의 `overflow_cyc`)에 반영하고, CSR을 읽는 곳을 한 함수로 모은다. `cpsid i` 안에서 1 ms 이상 돌려 검증한다.
3. **측정 — 우선순위를 바꾸면 순서는?**: 예제 6에서 SysTick 우선순위를 0x20(TIMER0의 0x40보다 높게)으로 바꾸면 로그 순서가 어떻게 바뀔지 먼저 예측하고 실행해 확인하라. PendSV는 여전히 한 번만 도는가?
   정답/힌트: TIMER0이 PENDSTSET을 쓰는 순간 SysTick이 TIMER0을 선점해 그 안에서 돈다(IPSR 15가 TIMER0 enter와 exit 사이에 나온다). PendSV는 여전히 둘이 다 끝난 뒤 한 번.
4. **코드 — 5번째 인자**: 예제 9에 `SYS_SUM5(a, b, c, d, e)`를 추가하라. 다섯 번째 인자를 R4로 넘기면 핸들러에서 어떻게 꺼내야 하나? 왜 프레임에서는 찾을 수 없나?
   정답/힌트: R4는 하드웨어 프레임(R0~R3, R12, LR, PC, xPSR)에 없다. naked 핸들러에서 C를 부르기 전에 `mov r2, r4`처럼 세 번째 인자로 넘긴다. Zephyr의 `z_arm_svc`가 시스템 콜 경로에서 R4~R6을 다루는 방식을 보라.
5. **측정 — 뼈대에서 선점 패턴 바꾸기**: 예제 12에서 `imu`의 일을 300 µs에서 2 ms로 늘리고 IMU 주기를 7 ms에서 3 ms로 줄여라. 간트 차트에서 `telem`(10 ms마다 3 ms)은 마감(다음 release 전 완료)을 지키는가? PendSV 횟수는 어떻게 변하나?
   정답/힌트: imu가 CPU의 약 2/3를 가져가 ctrl · telem이 밀린다. 로그의 `P` 줄 수를 세고, telem의 `Y` 시각이 다음 release(10 ms 배수)를 넘는지 본다. 이 질문의 수학적 답이 N 모듈(RTA)이다.
6. **코드 — RISC-V에서 값 돌려주기**: 예제 14의 `ecall`이 42를 돌려주게 하라. `interrupt("machine")` 속성을 버리고 trap 입구를 어셈블리로 써서 레지스터를 스택 프레임에 저장해야 한다.
   정답/힌트: 입구에서 `addi sp, sp, -64` 후 a0~a7 · ra · t0~t6 저장, C 핸들러에 프레임 포인터를 넘겨 `frame[a0] = a0 + a1`, 복원 후 `mret`. FreeRTOS `portable/GCC/RISC-V/portContext.h`의 `portcontextSAVE_CONTEXT_INTERNAL`이 참고가 된다.
7. **실칩 — DWT로 다시 재기**: 손에 있는 Cortex-M4/M7 보드에서 예제 5를 실행해 NUMCOMP와 CYCCNT가 실제로 증가하는지 확인하고, 예제 4의 `delay_us()` 오차를 CYCCNT로 다시 재라. QEMU 수치와 무엇이 다른가?
   정답/힌트: 실칩은 플래시 wait state와 캐시 때문에 함수 오버헤드가 다르게 나오지만, "오차가 지연 길이와 무관하게 일정"이라는 모양은 같아야 한다.

---

## 10. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| SysTick | System Tick Timer | Arm이 코어에 넣은 24비트 down counter, 예외 15 (Cortex-M3/M4/M7은 늘 있고, M0/M0+ 등은 칩에 따라 없을 수 있다) |
| CSR / RVR / CVR / CALIB | SysTick 네 레지스터 | 제어·상태 / reload 값 / 현재 값 / 10 ms 보정 힌트 |
| COUNTFLAG | CSR bit 16 | 마지막 CSR 읽기 이후 1 → 0을 지났으면 1, 읽으면 지워짐 |
| CLKSOURCE | CSR bit 2 | 1 = 프로세서 클럭, 0 = 칩이 정한 참조 클럭 |
| tick | RTOS의 기본 시간 단위 | SysTick 한 주기, 시간 진행 · 깨움 · time slice 결정 시점 |
| drift | 누적 시간 오차 | 주기 평균이 틀려 계속 쌓이는 오차 (지터와 다름) |
| jitter | 각 사건 시각의 흔들림 | 진입 지연 차이로 생기며 자동 reload면 쌓이지 않는다 |
| PendSV | pendable service call | 소프트웨어가 pending 시키는 예외 14, RTOS의 전환 실행 장소 |
| ICSR | Interrupt Control and State Register | PENDSVSET(28) · PENDSVCLR(27) · PENDSTSET(26) · PENDSTCLR(25) |
| SHPR2 / SHPR3 | System Handler Priority Register | SVCall 우선순위 / SysTick · PendSV 우선순위 |
| coalescing | 요청 합쳐짐 | pending은 1비트라 여러 요청이 한 번 실행으로 합쳐진다 |
| tail-chaining | 꼬리 물기 | 앞 핸들러 끝에서 프레임을 다시 쌓지 않고 다음 pending 핸들러로 진입 (B2) |
| SVC / SVCall | supervisor call | `svc #imm`이 일으키는 동기 예외 11, 시스템 콜 문 |
| escalation | 승격 | 지금 받을 수 없는 동기 예외(SVC 등)가 HardFault로 바뀌는 것, HFSR.FORCED |
| EXC_RETURN | 예외 복귀 코드 | 핸들러의 LR 값, bit 2 = 어느 스택에 프레임이 있나 (B2) |
| stacked frame | 쌓인 프레임 | 예외 진입 때 하드웨어가 쌓는 R0~R3 · R12 · LR · PC · xPSR 8워드 |
| DWT CYCCNT | Data Watchpoint and Trace 사이클 카운터 | 코어 클럭마다 1 증가하는 32비트 카운터, TRCENA 필요 |
| DEMCR.TRCENA | DEMCR bit 24 | DWT · ITM 블록을 켜는 비트 |
| FPGAIO COUNTER | MPS2 보드 카운터 | `0x40028018`, 25 MHz, 이 노트에서 CYCCNT 대신 사용 |
| mtimecmp / MSIP / ecall | RISC-V 대응물 | tick 비교 레지스터 / 소프트웨어 인터럽트 / 환경 호출 명령 |

---

## 11. 요약 & 체크리스트

Cortex-M RTOS는 세 예외로 시간과 전환과 커널 진입을 나눈다. **SysTick**은 코어 안의 24비트 down counter로, RVR = 클럭 / tick − 1로 설정하면 하드웨어 자동 reload 덕분에 ISR이 늦어도 드리프트가 쌓이지 않는다(예제 2: −3 µs vs 재시작 방식 1416 µs). CVR은 sub-tick 시계와 µs 지연의 재료지만, tick과 함께 읽을 때는 찢어진 읽기를 막아야 한다(예제 3 · 4). 측정 전용 시계는 DWT CYCCNT이고 TRCENA부터 켠다(QEMU에는 없어 FPGAIO로 대신). **PendSV**는 최저 우선순위의 소프트웨어 예외라서, 어디서 몇 번 요청하든 모든 ISR이 끝나고 Thread로 돌아가기 직전 한 번 실행된다(예제 6 · 7). 그래서 SysTick · ISR · 태스크는 "결정 + PENDSVSET"만 하고 전환은 PendSV 한 곳에서 한다. 우선순위를 리셋 값 0으로 두면 ISR 한가운데서 전환이 일어난다(예제 8). **SVC**는 동기 예외로, 번호는 쌓인 PC − 2에서, 인자는 프레임의 R0~R3에서, 반환값은 frame[0]으로 주고받는다(예제 9 · 10). 지금 들어갈 수 없으면 HardFault로 승격된다. FreeRTOS는 SVCall 최고 · SysTick과 PendSV 최저, Zephyr는 SVCall 최고 · SysTick 레벨 1 · PendSV 최저로 둔다(예제 11 · 13). 다른 코어에는 이 셋이 없어서 RISC-V는 mtimecmp · ecall · trap 출구 전환으로 같은 역할을 한다(예제 14).

- [ ] SysTick 네 레지스터와 CSR의 네 비트(ENABLE · TICKINT · CLKSOURCE · COUNTFLAG)를 말할 수 있다
- [ ] 임의의 클럭과 tick rate에서 RVR을 계산하고, −1을 빼먹었을 때의 ppm과 24비트 한계를 말할 수 있다
- [ ] 지터와 드리프트의 차이, 자동 reload가 드리프트를 막는 이유를 예제 2 숫자로 설명할 수 있다
- [ ] tick + CVR 타임스탬프의 찢어진 읽기를 설명하고, 두 번 읽기와 COUNTFLAG로 고칠 수 있다
- [ ] 돌고 있는 SysTick을 건드리지 않는 `delay_us()`를 wrap까지 맞게 쓸 수 있다
- [ ] DWT CYCCNT를 켜는 4단계(TRCENA · LAR · NOCYCCNT 확인 · CYCCNTENA)를 말할 수 있다
- [ ] PendSV를 요청 · 취소하는 ICSR 비트와 우선순위 레지스터 위치를 안다
- [ ] "왜 전환을 PendSV에서 하나"를 중첩 · 합쳐짐 · Thread 복귀 직전 세 가지로 말할 수 있다
- [ ] PendSV 우선순위가 높으면 무슨 일이 생기는지 EXC_RETURN 값으로 설명할 수 있다
- [ ] SVC 핸들러에서 스택 선택 · 번호 추출 · 인자 · 반환값 처리를 어셈블리 몇 줄로 쓸 수 있다
- [ ] SVC가 HardFault로 승격되는 두 조건과 그 fault 레지스터 모양(HFSR.FORCED, CFSR = 0)을 안다
- [ ] FreeRTOS와 Zephyr에서 세 예외가 설정 · 호출되는 함수 이름과 파일을 짚을 수 있다

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM C7 | SysTick · DWT | §1 전체 (1.2 레지스터, 1.3 reload, 1.4 sub-tick, 1.5 드리프트, 1.6 µs 지연, 1.7 DWT CYCCNT) | 링크 + RTOS 없는 슈퍼 루프에서 SysTick 하나로 `millis()` · 타임아웃 · 협력형 타이머 목록을 만드는 법 |
| BM M7 | 타이밍 측정 기법 | §1.7 DWT CYCCNT 켜기와 함정, §1.4 타이머 스냅샷 | GPIO 토글 + scope, 측정이 측정을 바꾸는 문제 (주인은 RTOS E3) |
| BM C8 | Fault 예외 | §3.3 예제 10 SVC의 HardFault 승격 (HFSR.FORCED) | fault 레지스터 전체 해석 (주인은 RTOS M1) |
| BM C4 | 예외 진입 · 복귀 | §2 · §3에서 EXC_RETURN · 쌓인 프레임을 쓰는 부분만 | 주인은 RTOS B2 — 이 노트는 참조만 |

## 참고 자료

- Arm, "Armv7-M Architecture Reference Manual" (DDI 0403) — SysTick, SCB(ICSR · SHPR), 예외 모델, SVC 인코딩, DWT. https://developer.arm.com/documentation/ddi0403/latest/
- Arm, "Cortex-M3 Devices Generic User Guide" (DUI 0552) — SysTick 레지스터, System Control Block 레지스터 요약. https://developer.arm.com/documentation/dui0552/latest/
- Joseph Yiu, "The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors", 3rd ed., Newnes, 2014 — SysTick · PendSV · SVC와 OS 지원 기능 설명.
- FreeRTOS Kernel V11.3.2 소스(lab `/var/rtoslab/src/FreeRTOS-Kernel`): `portable/GCC/ARM_CM3/port.c`(`xPortStartScheduler`, `vPortSetupTimerInterrupt`, `xPortSysTickHandler`, `vPortSVCHandler`, `prvPortStartFirstTask`, `xPortPendSVHandler`), `portable/GCC/ARM_CM3/portmacro.h`(`portYIELD`), `portable/GCC/ARM_CM3_MPU/port.c` · `portmacro.h`(`vSVCHandler_C`, `portSVC_*`), `portable/GCC/ARM_CM0/portmacro.h`, `portable/GCC/ARM_CR5/portmacro.h`, `portable/GCC/ARM_AARCH64/portmacro.h`, `portable/GCC/RISC-V/portmacro.h` · `portASM.S`, `include/FreeRTOS.h`(trace 매크로). https://github.com/FreeRTOS/FreeRTOS-Kernel
- FreeRTOS 문서, "Running the RTOS on an ARM Cortex-M core"(인터럽트 우선순위와 `configMAX_SYSCALL_INTERRUPT_PRIORITY`). https://www.freertos.org/RTOS-Cortex-M3-M4.html
- Zephyr v4.4.2 소스(lab `/var/rtoslab/zephyrproject/zephyr`): `arch/arm/core/cortex_m/vector_table.S`, `swap_helper.S`(`z_arm_pendsv`), `svc.S`(`z_arm_svc`), `exc_exit.c`(`z_arm_exc_exit`), `isr_wrapper.c`, `thread.c`(`arch_switch_to_main_thread`), `arm-m-switch.c`, `arch/arm/include/cortex_m/kernel_arch_func.h`(`arch_swap`), `arch/arm/include/cortex_m/exception.h`(`z_arm_exc_setup`), `include/zephyr/arch/arm/cortex_m/exception.h`(우선순위 매크로), `include/zephyr/arch/arm/syscall.h`(`_SVC_CALL_*`, `arch_syscall_invoke6`), `drivers/timer/cortex_m_systick.c`(`sys_clock_isr`, `elapsed`), `kernel/busy_wait.c`. https://github.com/zephyrproject-rtos/zephyr
- Zephyr 문서, Arm Cortex-M Developer Guide. https://docs.zephyrproject.org/latest/hardware/arch/arm_cortex_m.html
- Zephyr 문서, System Calls. https://docs.zephyrproject.org/latest/kernel/usermode/syscalls.html
- Memfault Interrupt 블로그, "A Practical guide to ARM Cortex-M Exception Handling". https://interrupt.memfault.com/blog/arm-cortex-m-exceptions-and-nvic
- Memfault Interrupt 블로그, "Cortex-M RTOS Context Switching". https://interrupt.memfault.com/blog/cortex-m-rtos-context-switching
- QEMU 문서, Arm MPS2 and MPS3 boards. https://www.qemu.org/docs/master/system/arm/mps2.html
- RISC-V International, "The RISC-V Instruction Set Manual, Volume II: Privileged Architecture" — `mtvec` · `mcause` · `mepc` · `mstatus.MIE` · `ecall`. https://github.com/riscv/riscv-isa-manual
- 같은 사이트의 관련 노트: A7(같은 보드에서 잰 tick · 전환 비용), B1 · B2 · B3(프로그래머 모델 · 예외 진입 · NVIC), C2 · C3 · C5 · C6 · C8(전환 · 첫 태스크 · tick · tickless · 시스템 콜), EL E6(Linux 시스템 콜 경로), EL F8(Linux 시간 · `udelay`)
