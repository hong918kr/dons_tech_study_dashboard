# B2. 예외 진입 · 복귀 — 하드웨어가 8워드를 대신 저장해 주기 때문에 ISR이 그냥 C 함수가 된다

> **이 노트를 다 읽으면**: 예외 번호 · 벡터 테이블 · VTOR로 "IRQ가 오면 CPU가 어디로 뛰는지"를 주소 단위로 말할 수 있다 · ISR 안에서 하드웨어가 쌓은 8워드 프레임을 직접 덤프하고 EXC_RETURN(0xFFFFFFF1/F9/FD, FP면 E1/E9/ED)을 해독할 수 있다 · 선점(중첩) · tail-chaining · late-arrival이 언제 일어나고 무엇을 아끼는지 타임스탬프로 설명할 수 있다 · 가짜 예외 프레임으로 원하는 PC에 "복귀"하는, RTOS 포트가 첫 태스크를 띄우는 트릭을 손으로 만들 수 있다
> **왜 필요한가**: 면접 단골 "Why can a Cortex-M ISR be a normal C function?", "What is EXC_RETURN?", "Walk me through what happens when an interrupt fires" — 그리고 실무에서 HardFault의 스택 프레임으로 원인 줄을 찾는 일, 부트로더에서 앱으로 VTOR를 넘기는 일, PendSV 컨텍스트 스위치(C2)를 읽는 일이 모두 이 노트 위에 있다
> **Don 기준 난이도**: 인터럽트 · 벡터 · 스택은 SSD 펌웨어(Cortex-R)에서 이미 강하다 / 새로 배울 부분은 Cortex-M이 **레지스터 저장을 하드웨어로 옮겨 놓은 방식**(8워드 자동 스태킹, EXC_RETURN이라는 "가짜 주소"로 복귀), 그리고 RTOS가 이걸 어떻게 재활용하는지
> **선행 노트**: B1(R0–R15 · MSP/PSP · CONTROL · Thread/Handler 모드), A3(RTOS 구성 요소). Linux 쪽 인터럽트 흐름은 EL F6(GIC → irqchip → handler)에서 이미 다뤘다 — 여기서는 다시 풀지 않는다. 이어지는 노트: B3(NVIC 우선순위 · BASEPRI), B4(SysTick · PendSV · SVC의 역할), B6(FPU 확장 프레임 · lazy stacking), C2(PendSV 컨텍스트 스위치), C3(첫 태스크 시작), M1(HardFault 해독)
> 🔁 **Bare-Metal 겹침**: BM C4(예외 진입 · 복귀) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 "RTOS 없이 벡터 테이블 · 핸들러를 직접 배치하고 부트로더에서 앱으로 점프하는 bare-metal 관점"만 더한다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 컨트롤러에서 쓰던 Cortex-R(Armv7-R)을 떠올려 보자. IRQ가 오면 CPU는 IRQ 모드로 바뀌고, `LR_irq`와 `SPSR_irq`라는 **banked 레지스터**에 복귀 정보만 넣어 준다. R0–R12를 저장하는 것은 **소프트웨어의 몫**이었다. 그래서 벡터에서 바로 C 함수로 갈 수 없고, 어셈블리 진입 코드(`SRSDB`/`STMFD sp!, {r0-r3, r12, lr}` 같은 것)가 레지스터를 밀어 넣은 다음에야 C 핸들러를 불렀다. 복귀할 때도 `SUBS pc, lr, #4`나 `RFE` 같은 특별한 명령이 필요했다.

Cortex-M(Armv6-M/v7-M/v8-M)은 이 일을 **하드웨어로 옮겼다**. IRQ가 오면 CPU가 스스로

1. R0, R1, R2, R3, R12, LR, PC, xPSR — **8워드**를 지금 쓰던 스택(MSP 또는 PSP)에 밀어 넣고,
2. 벡터 테이블에서 핸들러 주소를 읽어 PC에 넣고,
3. LR에 **EXC_RETURN**이라는 "가짜 주소"(0xFFFFFFF9 같은 값)를 넣고,
4. Handler 모드로 바꾼다.

핸들러가 끝나면서 `bx lr`로 그 가짜 주소에 "점프"하면, CPU는 "아, 이건 진짜 주소가 아니라 예외 복귀 신호구나" 하고 8워드를 꺼내 원래 자리로 돌아간다. 그래서 ISR은 **진입 코드도, 특별한 복귀 명령도 없는 평범한 C 함수**다.

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="40" width="150" height="150" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="85" y="65" font-size="13" text-anchor="middle" font-weight="bold">Thread 코드</text>
<text x="85" y="95" font-size="12" text-anchor="middle">main() 또는 태스크</text>
<text x="85" y="117" font-size="12" text-anchor="middle">R0–R3 · R12 · LR 사용</text>
<text x="85" y="139" font-size="12" text-anchor="middle">스택 = MSP 또는 PSP</text>
<text x="85" y="170" font-size="12" text-anchor="middle">← IRQ 도착</text>
<rect x="185" y="40" width="150" height="150" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="260" y="65" font-size="13" text-anchor="middle" font-weight="bold">하드웨어 진입</text>
<text x="260" y="95" font-size="12" text-anchor="middle">① 8워드 push</text>
<text x="260" y="117" font-size="12" text-anchor="middle">② 벡터 fetch → PC</text>
<text x="260" y="139" font-size="12" text-anchor="middle">③ LR ← EXC_RETURN</text>
<text x="260" y="161" font-size="12" text-anchor="middle">④ Handler 모드, MSP</text>
<rect x="360" y="40" width="140" height="150" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="430" y="65" font-size="13" text-anchor="middle" font-weight="bold">ISR = C 함수</text>
<text x="430" y="95" font-size="12" text-anchor="middle">AAPCS 규칙대로</text>
<text x="430" y="117" font-size="12" text-anchor="middle">R4–R11은 필요하면</text>
<text x="430" y="139" font-size="12" text-anchor="middle">컴파일러가 push</text>
<text x="430" y="161" font-size="12" text-anchor="middle">끝: bx lr</text>
<rect x="525" y="40" width="145" height="150" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="597" y="65" font-size="13" text-anchor="middle" font-weight="bold">하드웨어 복귀</text>
<text x="597" y="95" font-size="12" text-anchor="middle">EXC_RETURN 감지</text>
<text x="597" y="117" font-size="12" text-anchor="middle">8워드 pop</text>
<text x="597" y="139" font-size="12" text-anchor="middle">스택 · 모드 복원</text>
<text x="597" y="161" font-size="12" text-anchor="middle">→ 원래 PC로</text>
<line x1="161" y1="115" x2="183" y2="115" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<line x1="336" y1="115" x2="358" y2="115" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<line x1="501" y1="115" x2="523" y2="115" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<path d="M597,192 L597,222 L85,222 L85,193" fill="none" stroke="currentColor" stroke-width="1.3" stroke-dasharray="5 3" marker-end="url(#ah1)"/>
<text x="340" y="244" font-size="13" text-anchor="middle">하드웨어가 caller-saved 8개를 저장하므로 ISR은 C 함수 그대로 쓸 수 있다</text>
<text x="340" y="25" font-size="13" text-anchor="middle">Cortex-M 예외 한 번 = 하드웨어가 해 주는 "함수 호출 + 복귀"</text>
</svg>
```

그림 1 — Cortex-M 예외 한 번의 흐름. 주황 상자는 소프트웨어가 한 줄도 쓰지 않는 하드웨어 동작이다

이 메커니즘이 RTOS의 바닥이다. RTOS는 **"하드웨어가 이미 절반(8워드)을 저장해 줬다"**는 사실 위에서 나머지 절반(R4–R11)만 저장하고 스택 포인터를 바꿔치기해서 다른 태스크로 "복귀"한다. 이게 컨텍스트 스위치(C2)다. 첫 태스크를 띄울 때는 아예 **가짜 프레임**을 만들어 놓고 거기로 "복귀"한다(C3). 그러니 이 노트를 손으로 확인해 두면 C 모듈은 외우기가 아니라 읽기가 된다.

| Don이 아는 것 (Cortex-R · SSD FW) | Cortex-M에서는 |
|---|---|
| IRQ 모드 진입, banked `LR_irq` · `SPSR_irq` | 모드는 Handler 하나, banked 레지스터 없음. 대신 **스택에 8워드 자동 저장** |
| 진입 어셈블리가 R0–R3, R12, LR을 push | 하드웨어가 push. 벡터가 곧 C 함수 주소 |
| `SUBS pc, lr, #4` · `RFE`로 복귀 | 평범한 `bx lr` 또는 `pop {…, pc}` — LR에 들어 있는 **EXC_RETURN** 값이 복귀를 알린다 |
| 벡터 = 고정 주소의 분기 명령(`LDR pc, [pc, #…]`) | 벡터 = **주소 표**(워드 배열). 첫 워드는 초기 MSP 값 |
| GIC/VIC에서 IRQ 번호 읽고 EOI 쓰기 | NVIC가 번호마다 별도 벡터. EOI 없음(복귀가 곧 EOI) |
| 중첩하려면 소프트웨어가 모드 · 스택을 다시 정리 | 우선순위만 맞으면 하드웨어가 자동 중첩(선점) |

### 실습 환경 한 줄

- QEMU `mps2-an385` = Cortex-M3 @ 25 MHz 가상 보드. 대부분 **bare-metal**(lab `templates/baremetal-mps2`)로 돌리고, RTOS 쪽은 **FreeRTOS V11.3.2**(ARM_CM3 포트, 예제 14)와 **Zephyr v4.4.2**(`mps2/an385`, 예제 15)로 확인한다. FPU 프레임은 **다른 CPU 타깃** `mps2-an386`(Cortex-M4F)에서 돌린다(예제 12).
- 타이밍은 모두 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간, 실행마다 같은 숫자). 시계는 MPS2 FPGAIO COUNTER(0x40028018, 25 MHz).
- **정직하게 밝혀 둘 것**: QEMU는 **예외 스태킹 사이클을 모델링하지 않는다**. 진입 12사이클 · tail-chain 6사이클 같은 숫자는 실제 칩(Arm 문서)의 값이고, QEMU에서 잰 숫자는 "명령 몇 개가 지났나"일 뿐이다. 이 노트는 QEMU로 **순서 · 프레임 내용 · 주소**를 보여 주고, 사이클 값은 Arm 문서를 인용한다. QEMU icount 기준 숫자는 실제 칩과 절대값이 다르다(캐시 · 플래시 wait state · 버스 미모델링).
- 소스는 Mac `~/.rtos_scratch/B2/`, 빌드 산출물은 VM `/var/tmp/rtos/B2/`. 모든 빌드는 `-Wall -Wextra` 경고 0개.

예제들이 같이 쓰는 작은 헤더 하나를 먼저 둔다. lab `board.h`(UART printf, `REG32`, `NVIC_IPR`)에 SCB 레지스터와 특수 레지스터 읽기만 더했다.

```c
/* b2.h — B2 노트 공용 도우미: NVIC · SCB 레지스터, 특수 레지스터 읽기, 25 MHz 카운터 */
#ifndef B2_H
#define B2_H
#include "board.h"
#define ICSR      REG32(0xE000ED04u)   /* bit28 PENDSVSET, [8:0] VECTACTIVE */
#define VTOR      REG32(0xE000ED08u)
#define CCR       REG32(0xE000ED14u)   /* bit9 STKALIGN */
#define CPUID     REG32(0xE000ED00u)
#define NVIC_ISPR0 REG32(0xE000E200u)  /* set-pending */
#define NVIC_IABR0 REG32(0xE000E300u)  /* active bits */
#define NVIC_STIR REG32(0xE000EF00u)   /* software trigger: write IRQ number */
#define CYC()     REG32(0x40028018u)   /* FPGAIO counter, 25 MHz from reset */
static inline uint32_t get_msp(void)  { uint32_t v; __asm volatile("mrs %0, msp" : "=r"(v)); return v; }
static inline uint32_t get_psp(void)  { uint32_t v; __asm volatile("mrs %0, psp" : "=r"(v)); return v; }
static inline uint32_t get_ipsr(void) { uint32_t v; __asm volatile("mrs %0, ipsr" : "=r"(v)); return v; }
static inline uint32_t get_ctrl(void) { uint32_t v; __asm volatile("mrs %0, control" : "=r"(v)); return v; }
static inline uint32_t get_lr(void)   { uint32_t v; __asm volatile("mov %0, lr" : "=r"(v)); return v; }
static inline void irq_enable(unsigned n, uint8_t prio) { NVIC_IPR(n) = prio; NVIC_ISER0 = 1u << n; }
static inline void irq_pend(unsigned n) { NVIC_ISPR0 = 1u << n; __asm volatile("dsb\n isb" ::: "memory"); }
#define P board_printf_raw
#endif
```

빌드 · 실행은 늘 이 모양이다(이름만 바뀐다).

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile \
  SRCS=/Users/donh/.rtos_scratch/B2/ex1.c OUT=/var/tmp/rtos/B2/ex1 && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B2/ex1/app.elf'
```

---

## 1. 예외 모델 — 번호 하나가 모든 것을 정한다

> 🔁 이 절은 Bare-Metal BM C4와 겹친다 (주인: 이 노트).

### 1.1 직관 — "예외"는 인터럽트를 포함하는 큰 말이다

Cortex-M에서 **예외(exception)**는 "지금 하던 일을 멈추고 다른 코드로 가야 하는 모든 사건"이다. 외부 장치가 거는 **인터럽트(IRQ)**도, 잘못된 메모리 접근으로 생기는 **fault**도, `svc` 명령이 거는 **시스템 호출**도, 리셋도 전부 예외다. 하드웨어 입장에서는 다 같은 절차(스태킹 → 벡터 → 핸들러 → 복귀)를 밟고, **번호**만 다르다.

### 1.2 정의 — 예외 번호표

| 번호 | 이름 | 우선순위 | 누가 거나 | RTOS에서 |
|---|---|---|---|---|
| 1 | Reset | −3 (고정, 최고) | 전원 · 리셋 핀 · `SYSRESETREQ` | 시작점(B5) |
| 2 | NMI | −2 (고정) | 마스킹 불가 입력 | 워치독 · 전원 경보 |
| 3 | HardFault | −1 (고정) | 처리 못 한 fault가 승격(escalation) | 크래시 덤프(M1) |
| 4 | MemManage | 설정 가능 | MPU 위반 | 태스크 격리(F4) |
| 5 | BusFault | 설정 가능 | 버스 오류 | 잘못된 포인터 |
| 6 | UsageFault | 설정 가능 | 정의 안 된 명령, 0으로 나누기, Thumb 상태 오류 | 함수 포인터 버그 |
| 7–10 | 예약 | — | (Armv8-M에서 7 = SecureFault) | — |
| 11 | SVCall | 설정 가능 | `svc` 명령 | 첫 태스크 시작 · 시스템 콜(B4, C3, C8) |
| 12 | DebugMonitor | 설정 가능 | 디버그 이벤트 | — |
| 13 | 예약 | — | — | — |
| 14 | PendSV | 설정 가능 | 소프트웨어가 `ICSR.PENDSVSET` | **컨텍스트 스위치**(B4, C2) |
| 15 | SysTick | 설정 가능 | 코어 내장 타이머 | **tick**(B4, C5) |
| 16 + n | IRQ n | 설정 가능 | 주변장치(NVIC 입력 n) | 드라이버 ISR |

외워 둘 것 세 가지.

- **IRQ n의 예외 번호는 16 + n**이다. NVIC 레지스터(ISER, ISPR, IPR)는 IRQ 번호 n으로 쓰고, 벡터 테이블과 IPSR은 예외 번호 16 + n으로 쓴다. 이 두 번호를 섞는 것이 흔한 실수다.
- 현재 실행 중인 예외 번호는 **IPSR**(xPSR의 아래 9비트)에 있다. Thread 모드면 0이다. `ICSR.VECTACTIVE`(0xE000ED04의 [8:0])도 같은 값을 보여 준다.
- 우선순위는 **숫자가 작을수록 높다**. Reset · NMI · HardFault만 음수로 고정이고 나머지는 설정한다(자세한 비트 구조와 함정은 B3).

### 1.3 벡터 테이블 — 분기 명령이 아니라 주소 표

Cortex-M의 벡터 테이블은 **워드 배열**이다. `테이블 주소 + 4 × 예외 번호`에 핸들러 주소가 들어 있다. 0번 칸은 예외가 아니라 **리셋 때 MSP에 넣을 초기 스택 값**이다. 리셋 직후 CPU는 0번 워드를 MSP에, 1번 워드를 PC에 넣고 시작한다 — 그래서 Cortex-M은 첫 명령 전에 이미 스택이 있고, `Reset_Handler`부터 C로 쓸 수 있다(B5).

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="22" font-size="13" font-weight="bold">오프셋</text>
<text x="215" y="22" font-size="13" font-weight="bold" text-anchor="middle">벡터 테이블 (VTOR가 가리키는 곳)</text>
<text x="10" y="45" font-size="12">0x00</text><rect x="70" y="30" width="290" height="22" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="80" y="45" font-size="12">초기 MSP 값 (_estack)</text>
<text x="10" y="67" font-size="12">0x04</text><rect x="70" y="52" width="290" height="22" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="80" y="67" font-size="12">1 Reset_Handler</text>
<text x="10" y="89" font-size="12">0x08</text><rect x="70" y="74" width="290" height="22" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="80" y="89" font-size="12">2 NMI</text>
<text x="10" y="111" font-size="12">0x0C</text><rect x="70" y="96" width="290" height="22" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="80" y="111" font-size="12">3 HardFault</text>
<text x="10" y="133" font-size="12">0x10</text><rect x="70" y="118" width="290" height="22" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="80" y="133" font-size="12">4–6 MemManage · BusFault · UsageFault</text>
<text x="10" y="155" font-size="12">0x1C</text><rect x="70" y="140" width="290" height="22" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="4 3"/><text x="80" y="155" font-size="12">7–10 예약 (0)</text>
<text x="10" y="177" font-size="12">0x2C</text><rect x="70" y="162" width="290" height="22" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="80" y="177" font-size="12">11 SVCall</text>
<text x="10" y="199" font-size="12">0x30</text><rect x="70" y="184" width="290" height="22" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="80" y="199" font-size="12">12 DebugMon · 13 예약</text>
<text x="10" y="221" font-size="12">0x38</text><rect x="70" y="206" width="290" height="22" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/><text x="80" y="221" font-size="12">14 PendSV — RTOS 컨텍스트 스위치</text>
<text x="10" y="243" font-size="12">0x3C</text><rect x="70" y="228" width="290" height="22" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/><text x="80" y="243" font-size="12">15 SysTick — RTOS tick</text>
<text x="10" y="265" font-size="12">0x40</text><rect x="70" y="250" width="290" height="22" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="80" y="265" font-size="12">16 IRQ0 (UART0 RX)</text>
<text x="10" y="287" font-size="12">0x44</text><rect x="70" y="272" width="290" height="22" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="80" y="287" font-size="12">17 IRQ1 (UART0 TX) …</text>
<text x="10" y="309" font-size="12">0x60</text><rect x="70" y="294" width="290" height="22" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="80" y="309" font-size="12">24 IRQ8 (TIMER0)</text>
<text x="10" y="331" font-size="12">0x68</text><rect x="70" y="316" width="290" height="22" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="80" y="331" font-size="12">26 IRQ10 (DUALTIMER) …</text>
<text x="10" y="353" font-size="12">0xBC</text><rect x="70" y="338" width="290" height="22" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="80" y="353" font-size="12">47 IRQ31 — lab 테이블의 끝</text>
<rect x="395" y="60" width="275" height="150" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0"/>
<text x="407" y="84" font-size="13" font-weight="bold">읽는 법</text>
<text x="407" y="108" font-size="12">주소 = VTOR + 4 × 예외 번호</text>
<text x="407" y="130" font-size="12">값의 bit0 = 1 (Thumb 표시)</text>
<text x="407" y="152" font-size="12">IPSR = 지금 실행 중인 예외 번호</text>
<text x="407" y="174" font-size="12">IRQ n → 예외 번호 16 + n</text>
<text x="407" y="196" font-size="12">0번 칸만 주소가 아닌 스택 값</text>
<rect x="395" y="240" width="275" height="96" rx="6" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c"/>
<text x="407" y="264" font-size="13" font-weight="bold">예제 1에서 읽은 값</text>
<text x="407" y="288" font-size="12">vec[1] Reset = 0x2a1 (함수 0x2a0)</text>
<text x="407" y="310" font-size="12">vec[24] IRQ8 = 0x121 (함수 0x120)</text>
<text x="407" y="330" font-size="12">VTOR = 0x0 (플래시 시작)</text>
<line x1="393" y1="300" x2="362" y2="305" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/>
<text x="215" y="385" font-size="12" text-anchor="middle">한 칸 = 4바이트 워드 · 높은 IRQ일수록 아래</text>
</svg>
```

그림 2 — lab 벡터 테이블(`startup.c`의 `g_vectors[16 + 32]`). 파랑 = 시스템 예외, 주황 = RTOS가 쓰는 두 예외, 초록 = 주변장치 IRQ

### 예제 1 — 벡터 테이블을 읽고, 각 핸들러 안에서 예외 번호 확인

무엇을 확인하나: 벡터 테이블의 실제 값(초기 MSP, 핸들러 주소의 bit0), 그리고 SVC · PendSV · IRQ8 · IRQ10 핸들러 안에서 IPSR이 정말 11 · 14 · 24 · 26인지.

```c
/* ex1 — 벡터 테이블과 예외 번호: 각 핸들러 안에서 IPSR · ICSR.VECTACTIVE 읽기 */
#include "b2.h"
extern const void *g_vectors[];
static void who(const char *name)
{
    P("  %s: IPSR=%u VECTACTIVE=%u MSP=0x%x\n", name, get_ipsr(), ICSR & 0x1FFu, get_msp());
}
void SVC_Handler(void)       { who("SVC_Handler     "); }
void PendSV_Handler(void)    { who("PendSV_Handler  "); }
void TIMER0_IRQHandler(void) { who("TIMER0 (IRQ 8)  "); }
void DUALTIMER_IRQHandler(void) { who("DUALTIMER(IRQ10)"); }

int main(void)
{
    static const char *nm[16] = { "initial MSP", "Reset", "NMI", "HardFault", "MemManage",
        "BusFault", "UsageFault", "-", "-", "-", "-", "SVCall", "DebugMon", "-", "PendSV", "SysTick" };
    P("CPUID=0x%x VTOR=0x%x CCR=0x%x (STKALIGN=%u)\n", CPUID, VTOR, CCR, (CCR >> 9) & 1u);
    for (int i = 0; i < 16; i++)
        P("  vec[%d]%s @0x%x = 0x%x  %s\n", i, i < 10 ? " " : "", 4 * i, (unsigned)g_vectors[i], nm[i]);
    P("  vec[24] @0x60 = 0x%x  IRQ 8 (TIMER0)\n", (unsigned)g_vectors[24]);
    P("thread: IPSR=%u MSP=0x%x\n", get_ipsr(), get_msp());
    __asm volatile("svc 0");
    ICSR = 1u << 28;                       /* PENDSVSET */
    __asm volatile("dsb\n isb");
    irq_enable(8, 0x80);  irq_pend(8);
    irq_enable(10, 0x80); NVIC_STIR = 10;  __asm volatile("dsb\n isb");
    P("back in thread: IPSR=%u\n", get_ipsr());
    return 0;
}
```

```text
CPUID=0x410fc231 VTOR=0x0 CCR=0x200 (STKALIGN=1)
  vec[0]  @0x0 = 0x20400000  initial MSP
  vec[1]  @0x4 = 0x2a1  Reset
  vec[2]  @0x8 = 0x2f9  NMI
  vec[3]  @0xc = 0x28d  HardFault
  vec[4]  @0x10 = 0x2f9  MemManage
  vec[5]  @0x14 = 0x2f9  BusFault
  vec[6]  @0x18 = 0x2f9  UsageFault
  vec[7]  @0x1c = 0x0  -
  vec[8]  @0x20 = 0x0  -
  vec[9]  @0x24 = 0x0  -
  vec[10] @0x28 = 0x0  -
  vec[11] @0x2c = 0xc1  SVCall
  vec[12] @0x30 = 0x2f9  DebugMon
  vec[13] @0x34 = 0x0  -
  vec[14] @0x38 = 0xf1  PendSV
  vec[15] @0x3c = 0x2f9  SysTick
  vec[24] @0x60 = 0x121  IRQ 8 (TIMER0)
thread: IPSR=0 MSP=0x203fffd8
  SVC_Handler     : IPSR=11 VECTACTIVE=11 MSP=0x203fffa8
  PendSV_Handler  : IPSR=14 VECTACTIVE=14 MSP=0x203fffa8
  TIMER0 (IRQ 8)  : IPSR=24 VECTACTIVE=24 MSP=0x203fffa8
  DUALTIMER(IRQ10): IPSR=26 VECTACTIVE=26 MSP=0x203fffa8
back in thread: IPSR=0
[board_exit] code=0
```

출력에서 볼 것:

- `vec[0] = 0x20400000` — 주소가 아니라 RAM 끝(`_estack`, 0x20000000 + 4 MB)이다. 리셋 때 이 값이 MSP가 된다.
- `vec[1] = 0x2a1`, `vec[24] = 0x121` — 홀수다. `nm`으로 보면 `Reset_Handler`는 0x2a0, `TIMER0_IRQHandler`는 0x120이다. **bit0 = 1은 "Thumb 코드"라는 표시**다. Cortex-M은 Thumb만 실행하므로 이 비트가 0이면 fault가 난다(예제 13의 CASE 3).
- `vec[2]`, `vec[4]`… 모두 0x2f9 = `Default_Handler`(weak alias). 쓰지 않는 예외는 기본 핸들러로 모아 둔 것이다.
- IPSR = 11 · 14 · 24 · 26. **IRQ 8은 예외 번호 24, IRQ 10은 26**이다. Thread로 돌아오면 다시 0.
- `CPUID = 0x410fc231` = Arm(0x41), Cortex-M3(0xC23), r0p1. `CCR = 0x200` = STKALIGN 비트가 켜져 있다(§4).
- 핸들러 안 MSP(0x203fffa8)가 thread의 MSP(0x203fffd8)보다 48바이트 낮다 — 하드웨어 프레임 32바이트 + `who()`를 부르는 핸들러 자신의 push 16바이트.

```sh
limactl shell elinux -- arm-none-eabi-nm -n /var/tmp/rtos/B2/ex1/app.elf   # 핸들러 주소만 골라 봄
```

```text
00000000 R g_vectors
000000c0 T SVC_Handler
000000f0 T PendSV_Handler
00000120 T TIMER0_IRQHandler
0000028c T HardFault_Handler
000002a0 T Reset_Handler
000002f8 T Default_Handler
```

### 1.4 함정

- **벡터에 0이 들어 있는 IRQ를 켜면** 그 IRQ가 오는 순간 CPU는 주소 0(bit0 = 0)으로 가려다 UsageFault(INVSTATE) → HardFault가 된다. 예제 13의 CASE 3이 정확히 이 모양이다. 테이블을 `[16 + 32]`처럼 짧게 만들고 IRQ 40을 켜면 테이블 **밖**의 아무 워드나 핸들러 주소로 쓰인다 — 더 나쁘다.
- 어셈블리로 핸들러 주소를 직접 쓸 때 `.thumb_func`을 빼먹으면 bit0이 0인 주소가 들어간다. C 함수 포인터는 컴파일러가 알아서 bit0을 1로 만든다.
- IRQ 번호(NVIC)와 예외 번호(벡터 · IPSR)를 섞지 않는다. Zephyr의 `_isr_wrapper`가 `IPSR - 16`을 하는 이유다(§12).

---

## 2. VTOR — 벡터 테이블 옮기기

### 2.1 직관

리셋 때 벡터 테이블은 주소 0(보통 플래시 시작)에 있다. 그런데 테이블을 다른 곳에 두고 싶은 경우가 많다.

- **부트로더 → 앱**: 부트로더가 0번지에 있고 앱은 0x08010000 같은 곳에 있다. 앱으로 점프하기 전에 VTOR를 앱 테이블로 바꿔야 앱의 IRQ가 앱 핸들러로 간다(MCUboot도, 벤더 부트로더도 한다 — L2).
- **RAM 테이블**: 실행 중에 핸들러를 바꿔 끼우거나(드라이버를 런타임에 등록), 플래시 wait state 없이 벡터를 읽고 싶을 때.
- **듀얼 이미지 · 테스트**: 같은 칩에서 다른 이미지로 갈아탈 때.

**VTOR**(Vector Table Offset Register, 0xE000ED08)는 "벡터 테이블이 어디 있나"를 CPU에 알려 주는 레지스터다. Armv7-M(M3/M4/M7)과 Armv8-M Mainline(M33…)에는 항상 있고, Armv6-M Cortex-M0에는 없고 M0+에서는 선택 사항이다(B7).

### 2.2 정렬 규칙 (Arm 문서)

Cortex-M3 TRM(r2p0)은 VTOR 오프셋이 **테이블 크기를 2의 거듭제곱으로 올린 값에 정렬**돼야 한다고 적는다. 최소 32워드(128바이트, IRQ 16개까지). 예를 들어 IRQ 21개면 테이블 37워드 → 64워드(256바이트) 정렬. VTOR의 [6:0] 비트는 구현돼 있지 않아서 항상 0으로 읽힌다. lab 테이블은 48워드(IRQ 32개)이므로 **64워드 = 256바이트 정렬**이 필요하다.

### 예제 2 — 테이블을 RAM으로 복사하고, 실행 중에 IRQ8 핸들러를 바꾸고, 없던 IRQ20을 설치

무엇을 확인하나: VTOR를 바꾸는 순간부터 CPU가 RAM 테이블을 쓰는지, RAM 테이블의 칸 하나를 바꾸면 다음 IRQ부터 새 핸들러가 불리는지, 일부러 어긋난 주소를 쓰면 VTOR가 어떻게 읽히는지.

```c
/* ex6 — VTOR 재배치: 벡터 테이블을 RAM으로 복사하고 실행 중에 IRQ 핸들러를 바꿔 끼운다 */
#include <string.h>
#include "b2.h"
#define NVEC (16 + 32)
extern const uint32_t g_vectors[NVEC];
static uint32_t ram_vectors[NVEC] __attribute__((aligned(256)));  /* 48 words -> next pow2 = 256 B */

void TIMER0_IRQHandler(void) { P("  flash-table handler for IRQ8 (TIMER0_IRQHandler)\n"); }
static void irq8_v2(void)    { P("  RAM-patched handler for IRQ8 (irq8_v2), IPSR=%u\n", get_ipsr()); }
static void irq20(void)      { P("  IRQ20 handler installed at runtime, IPSR=%u\n", get_ipsr()); }

int main(void)
{
    irq_enable(8, 0x80);
    P("1) VTOR=0x%x  vec[24]=0x%x\n", VTOR, g_vectors[24]);
    irq_pend(8);

    memcpy(ram_vectors, g_vectors, sizeof ram_vectors);
    ram_vectors[16 + 8]  = (uint32_t)irq8_v2;          /* function pointers already carry bit0=1 */
    ram_vectors[16 + 20] = (uint32_t)irq20;
    __asm volatile("cpsid i");
    VTOR = (uint32_t)ram_vectors;
    __asm volatile("dsb\n isb\n cpsie i" ::: "memory");
    P("2) VTOR=0x%x (ram_vectors=0x%x)  vec[24]=0x%x vec[36]=0x%x\n", VTOR, (uint32_t)ram_vectors,
      ram_vectors[24], ram_vectors[36]);
    irq_pend(8);
    irq_enable(20, 0x80); irq_pend(20);

    VTOR = (uint32_t)ram_vectors + 0x44;               /* misaligned on purpose */
    P("3) wrote VTOR=0x%x, read back 0x%x  (low 7 bits are not implemented)\n",
      (uint32_t)ram_vectors + 0x44, VTOR);
    VTOR = 0; __asm volatile("dsb\n isb");
    P("4) VTOR back to flash: ");
    irq_pend(8);
    return 0;
}
```

```text
1) VTOR=0x0  vec[24]=0xe1
  flash-table handler for IRQ8 (TIMER0_IRQHandler)
2) VTOR=0x20000100 (ram_vectors=0x20000100)  vec[24]=0xd1 vec[36]=0xc1
  RAM-patched handler for IRQ8 (irq8_v2), IPSR=24
  IRQ20 handler installed at runtime, IPSR=36
3) wrote VTOR=0x20000144, read back 0x20000100  (low 7 bits are not implemented)
4) VTOR back to flash:   flash-table handler for IRQ8 (TIMER0_IRQHandler)
[board_exit] code=0
```

출력에서 볼 것:

- 1)과 2) 사이에 코드의 `TIMER0_IRQHandler`는 그대로인데, VTOR만 바꾸자 같은 IRQ8이 `irq8_v2`로 간다. **CPU는 핸들러 이름을 모른다. VTOR + 4 × 24 자리의 워드만 읽는다.**
- 원래 테이블에서 IRQ20 칸(vec[36])은 0이었다. RAM 테이블에 주소를 넣고 나서야 IRQ20을 켤 수 있었다.
- 3) 0x20000144를 썼더니 0x20000100으로 읽힌다. 아래 7비트가 버려졌다. **QEMU는 이 마스킹은 재현하지만, "128바이트 정렬은 됐지만 256바이트 정렬은 안 된 테이블"에서 높은 IRQ 번호가 엉뚱한 칸을 읽는 실칩 동작은 확인하지 못했다** — 그래서 실무에서는 `aligned(테이블 크기를 2의 거듭제곱으로 올린 값)`을 지킨다.
- VTOR를 바꿀 때 `cpsid i` → 쓰기 → `dsb; isb` → `cpsie i` 순서를 썼다. 바꾸는 도중에 IRQ가 와서 반쯤 바뀐 상태를 보지 않게 하고, 다음 예외부터 새 값이 쓰이게 하려는 것이다.

### 2.3 RTOS 커널 안의 VTOR

- **FreeRTOS V11.3.2** `portable/GCC/ARM_CM3/port.c`의 `xPortStartScheduler()`는 `configCHECK_HANDLER_INSTALLATION`(기본 1)일 때 `portSCB_VTOR_REG`로 **현재 벡터 테이블을 읽어서** SVC 칸이 `vPortSVCHandler`, PendSV 칸이 `xPortPendSVHandler`인지 `configASSERT`한다. 주석에 "VTOR가 앱 테이블을 가리키지 않으면 여기서 assert나 fault가 날 수 있다"고 적혀 있다. 부트로더 뒤에 앱을 올렸는데 스케줄러 시작에서 멈춘다면 VTOR부터 의심하라는 뜻이다.
- 같은 파일의 `prvPortStartFirstTask()`는 `ldr r0, =0xE000ED08; ldr r0, [r0]; ldr r0, [r0]; msr msp, r0` — **VTOR가 가리키는 테이블의 0번 칸(초기 MSP)을 읽어 MSP를 처음 값으로 되돌린다.** main이 쓰던 MSP 스택을 버리고 그 공간을 ISR 전용 스택으로 재활용하는 것이다(C3).
- **Zephyr v4.4.2** `arch/arm/core/cortex_m/prep_c.c`의 `relocate_vector_table()`이 `SCB->VTOR = VECTOR_ADDRESS & VTOR_MASK`로 VTOR를 설정한다. `CONFIG_SRAM_VECTOR_TABLE`(arch/Kconfig)을 켜면 테이블을 SRAM으로 복사해 쓴다.

---

## 3. 진입 시퀀스 — 하드웨어가 쌓는 8워드

### 3.1 직관 — "하드웨어가 대신 해 주는 함수 호출 앞부분"

C 함수를 부를 때 호출하는 쪽(caller)은 "R0–R3, R12, LR은 부른 함수가 망가뜨려도 된다(caller-saved)"는 AAPCS 규칙을 따른다. Cortex-M 예외 진입은 바로 그 레지스터들 — 그리고 돌아갈 PC와 플래그(xPSR) — 을 **하드웨어가** 스택에 저장한다. 끼어든 쪽(ISR)이 그것들을 마음대로 써도 원래 코드는 복귀 후 아무 일 없었던 것처럼 계속한다.

### 3.2 정의 — 진입 단계 (Cortex-M3 TRM Table 5-4 요약)

| 단계 | 하는 일 | 비고 |
|---|---|---|
| 8워드 push | xPSR, PC, LR, R12, R3, R2, R1, R0을 **지금 쓰던 스택**(Thread였으면 MSP 또는 PSP, Handler였으면 MSP)에 | tail-chain이면 생략 |
| 벡터 읽기 | `테이블 + 4 × 예외 번호` | push와 동시에(다른 버스). late-arrival이면 다시 읽음 |
| PC 갱신 | 읽은 핸들러 주소 | |
| 파이프라인 채우기 | 핸들러 첫 명령 fetch | push와 병렬 |
| LR 갱신 | **EXC_RETURN** 값 | §5 |
| 모드 전환 | Handler 모드, 특권, 스택 = MSP, IPSR = 예외 번호 | |

스택 위의 순서는 낮은 주소부터 R0, R1, R2, R3, R12, LR, PC, xPSR이다. 새 SP가 R0을 가리킨다.

```
 higher address
  +--------------------+  <- SP before the exception (old SP)
  | (pad 4 B?)         |     only if STKALIGN=1 and old SP % 8 == 4
  +--------------------+
  | xPSR               |  [SP+28]   bit9 = padded?, bit24 = T
  | PC (return addr)   |  [SP+24]   next instruction to execute
  | LR                 |  [SP+20]   LR of the interrupted code
  | R12                |  [SP+16]
  | R3                 |  [SP+12]
  | R2                 |  [SP+8]
  | R1                 |  [SP+4]
  | R0                 |  [SP+0]  <- SP at handler entry
  +--------------------+
 lower address
```

### 예제 3 — 레지스터마다 표식을 넣고 IRQ를 pend, ISR 안에서 프레임 덤프

무엇을 확인하나: 하드웨어가 정말 R0–R3 · R12 · LR · PC · xPSR을 이 순서로 쌓는지, 쌓인 PC가 어디를 가리키는지. 레지스터에 0xA0A0A0A0 같은 표식을 넣은 직후 같은 어셈블리 블록 안에서 NVIC_ISPR0에 써서 IRQ8을 pend한다. 핸들러는 `naked`로 만들어 컴파일러가 아무것도 push하지 않은 상태의 SP(= 프레임 주소)와 LR(= EXC_RETURN)을 그대로 C 함수에 넘긴다.

```c
/* ex2 — ISR 안에서 하드웨어가 쌓은 8워드 프레임 덤프 (레지스터에 표식을 넣고 IRQ를 pend) */
#include "b2.h"
volatile uint32_t g_sp_before;
extern char b2_after_pend[];               /* asm label: the instruction after the pend */

void dump(uint32_t *f, uint32_t exc_return)
{
    static const char *n[8] = { "R0  ", "R1  ", "R2  ", "R3  ", "R12 ", "LR  ", "PC  ", "xPSR" };
    P("thread SP before IRQ = 0x%x, frame base (MSP in ISR) = 0x%x, diff = %u bytes\n",
      g_sp_before, (uint32_t)f, g_sp_before - (uint32_t)f);
    for (int i = 0; i < 8; i++) P("  [sp+%d%s] %s = 0x%x\n", 4 * i, i < 3 ? " " : "", n[i], f[i]);
    P("EXC_RETURN (LR in ISR) = 0x%x, label b2_after_pend = 0x%x\n", exc_return, (uint32_t)b2_after_pend);
    TIMER_INTCLR(TIMER0_BASE) = 1;
}
void TIMER0_IRQHandler(void) __attribute__((naked));
void TIMER0_IRQHandler(void)
{
    __asm volatile("mov r0, sp\n mov r1, lr\n push {r1, lr}\n bl dump\n pop {r1, pc}\n");
}

int main(void)
{
    irq_enable(8, 0x80);
    __asm volatile(
        " ldr r0, =0xA0A0A0A0 \n ldr r1, =0xA1A1A1A1 \n ldr r2, =0xA2A2A2A2 \n"
        " ldr r3, =0xA3A3A3A3 \n ldr r12, =0xCCCCCCCC \n ldr lr, =0xEEEEEEEF \n"
        " mov r6, sp \n ldr r7, =g_sp_before \n str r6, [r7] \n"
        " ldr r4, =0xE000E200 \n mov r5, #0x100 \n"      /* NVIC_ISPR0 = 1 << 8 */
        " str r5, [r4] \n dsb \n isb \n"
        ".global b2_after_pend \n b2_after_pend: \n nop \n"
        ::: "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r12", "lr", "memory");
    P("returned to thread, SP = 0x%x\n", get_msp());
    return 0;
}
```

```text
thread SP before IRQ = 0x203fffe0, frame base (MSP in ISR) = 0x203fffc0, diff = 32 bytes
  [sp+0 ] R0   = 0xa0a0a0a0
  [sp+4 ] R1   = 0xa1a1a1a1
  [sp+8 ] R2   = 0xa2a2a2a2
  [sp+12] R3   = 0xa3a3a3a3
  [sp+16] R12  = 0xcccccccc
  [sp+20] LR   = 0xeeeeeeef
  [sp+24] PC   = 0x192
  [sp+28] xPSR = 0x21000000
EXC_RETURN (LR in ISR) = 0xfffffff9, label b2_after_pend = 0x19a
returned to thread, SP = 0x203fffe0
[board_exit] code=0
```

```text
     192:	f3bf 8f4f 	dsb	sy
     196:	f3bf 8f6f 	isb	sy

0000019a <b2_after_pend>:
     19a:	bf00      	nop
```

출력에서 볼 것:

- 진입 직전 SP(0x203fffe0)와 프레임 주소(0x203fffc0)의 차이가 정확히 **32바이트 = 8워드**.
- R0–R3, R12, LR에 넣은 표식이 그대로 쌓였다. **LR = 0xeeeeeeef도 그대로** — 예외 직전의 LR이 프레임에 저장되고, ISR 안의 LR은 EXC_RETURN(0xfffffff9)으로 바뀐다. 이 둘을 헷갈리지 말 것.
- 쌓인 **PC = 0x192 = `dsb` 명령**이다. `str r5, [r4]`(0x190, ISPR 쓰기)가 끝난 직후 IRQ가 받아들여졌고, 아직 실행하지 않은 다음 명령 `dsb`가 복귀 주소가 됐다. **쌓인 PC는 "돌아가서 다음에 실행할 명령"**이다(fault일 때는 "fault를 낸 그 명령" — 예제 13). 실칩에서는 pend가 반영되기까지 몇 사이클이 걸릴 수 있어서 PC가 한두 명령 뒤일 수 있다. 그래서 "이 지점 전에는 반드시 받아들여졌다"를 보장하려고 `dsb; isb`를 쓴다.
- 쌓인 xPSR = 0x21000000 — bit24(T, Thumb) = 1, bit29(C 플래그) = 1. 플래그도 저장되므로 ISR이 `cmp`를 해도 원래 코드의 조건 분기가 망가지지 않는다. IPSR 부분(아래 9비트)은 0 = 예외 직전에 Thread였다.
- 복귀 후 SP가 다시 0x203fffe0. 하드웨어가 8워드를 꺼내며 SP도 되돌렸다.

### 3.3 RTOS 커널 안에서 이 프레임의 이름

- Zephyr: `include/zephyr/arch/arm/cortex_m/exception.h`의 `struct arch_esf` 안 `struct __basic_sf { a1(r0), a2(r1), a3(r2), a4(r3), ip(r12), lr(r14), pc(r15), xpsr }` — 바로 이 8워드다. fault 핸들러는 이 구조체 포인터를 받는다.
- FreeRTOS: 프레임을 구조체로 정의하지 않고 `pxPortInitialiseStack()`이 같은 순서로 워드를 하나씩 채운다(§12).

### 3.4 함정

- **naked가 아닌 C 핸들러에서 `mrs r0, msp`로 프레임을 찾으면 틀린다.** 컴파일러가 이미 push를 했기 때문에 SP가 프레임보다 낮다(예제 1에서 48바이트 차이). HardFault 핸들러가 늘 `naked` + 어셈블리 몇 줄(`tst lr, #4; ite eq; mrseq r0, msp; mrsne r0, psp`)로 시작하는 이유다.
- Thread가 PSP를 쓰고 있었다면 프레임은 **PSP 쪽**에 있다(예제 5). ISR 안의 SP(MSP)를 보면 안 된다.

---

## 4. 8바이트 정렬 — STKALIGN과 xPSR bit 9

### 4.1 직관

AAPCS는 "함수 경계에서 SP는 8바이트 정렬"을 요구한다(`double`, `uint64_t`, `LDRD`/`STRD`를 위해). 그런데 인터럽트는 **아무 명령 사이**에서나 온다. 그 순간 SP가 4바이트만 정렬돼 있을 수 있다(예: 함수가 `push {r4}` 하나만 한 상태). 그대로 32바이트를 쌓으면 ISR은 8바이트 정렬이 깨진 SP로 C 코드를 돌리게 된다. 그래서 하드웨어가 **필요하면 4바이트를 더 내려서** 정렬을 맞추고, "내가 4바이트 내렸다"는 사실을 **쌓은 xPSR의 bit 9**에 적어 둔다. 복귀할 때 그 비트를 보고 4바이트를 다시 올린다.

### 4.2 정의

- `CCR.STKALIGN`(0xE000ED14 bit 9) = 1이면 진입 시 old SP를 8바이트로 내리고, 내렸으면 쌓인 xPSR bit 9 = 1.
- Cortex-M3 TRM r2p0: STKALIGN 리셋 값이 **1로 바뀌었다**(그 전 리비전은 0). 이후 코어에서도 기본은 8바이트 정렬이고, Armv8-M에서는 이 비트가 RES1(항상 1)이다. 오래된 M3 r1p1 칩을 만나면 리셋 값을 꼭 확인한다.

### 예제 4 — SP를 일부러 4 mod 8로 만들고 IRQ를 받아 보기

무엇을 확인하나: SP가 8의 배수일 때와 4 mod 8일 때 프레임이 몇 바이트를 쓰는지, xPSR bit 9가 언제 1이 되는지, STKALIGN을 끄면 어떻게 되는지.

```c
/* ex7 — 8바이트 정렬 패딩: SP가 4 mod 8일 때 하드웨어가 4바이트를 더 내리고 xPSR bit9로 표시한다 */
#include "b2.h"
static volatile uint32_t s_sp_before, s_sp_after, s_frame, s_xpsr;
void rec(uint32_t *f) { s_frame = (uint32_t)f; s_xpsr = f[7]; }
void TIMER0_IRQHandler(void) __attribute__((naked));
void TIMER0_IRQHandler(void) { __asm volatile("mov r0, sp\n b rec\n"); }

static void trial(int misalign)
{
    __asm volatile(
        " mov r6, sp \n cbz %[m], 1f \n sub sp, #4 \n"     /* make SP = 4 mod 8 */
        "1: mov r5, sp \n str r5, [%[b]] \n"
        " ldr r4, =0xE000E200 \n mov r5, #0x100 \n str r5, [r4] \n dsb \n isb \n"
        " mov r5, sp \n str r5, [%[a]] \n mov sp, r6 \n"
        :: [m] "r"(misalign), [b] "r"(&s_sp_before), [a] "r"(&s_sp_after)
        : "r4", "r5", "r6", "memory");
    P("  SP before=0x%x (mod 8 = %u)  frame=0x%x  used=%u bytes  stacked xPSR=0x%x bit9=%u  SP after=0x%x\n",
      s_sp_before, s_sp_before & 7u, s_frame, s_sp_before - s_frame, s_xpsr, (s_xpsr >> 9) & 1u, s_sp_after);
}

int main(void)
{
    irq_enable(8, 0x80);
    P("CCR=0x%x STKALIGN=%u\n", CCR, (CCR >> 9) & 1u);
    trial(0); trial(1);
    CCR &= ~(1u << 9); __asm volatile("dsb\n isb");
    P("CCR=0x%x STKALIGN=%u (cleared)\n", CCR, (CCR >> 9) & 1u);
    trial(0); trial(1);
    return 0;
}
```

```text
CCR=0x200 STKALIGN=1
  SP before=0x203fffc8 (mod 8 = 0)  frame=0x203fffa8  used=32 bytes  stacked xPSR=0x61000000 bit9=0  SP after=0x203fffc8
  SP before=0x203fffc4 (mod 8 = 4)  frame=0x203fffa0  used=36 bytes  stacked xPSR=0x21000200 bit9=1  SP after=0x203fffc4
CCR=0x0 STKALIGN=0 (cleared)
  SP before=0x203fffc8 (mod 8 = 0)  frame=0x203fffa8  used=32 bytes  stacked xPSR=0x61000000 bit9=0  SP after=0x203fffc8
  SP before=0x203fffc4 (mod 8 = 4)  frame=0x203fffa4  used=32 bytes  stacked xPSR=0x21000000 bit9=0  SP after=0x203fffc4
[board_exit] code=0
```

출력에서 볼 것:

- STKALIGN = 1, SP 0x…c8(정렬): 32바이트, bit9 = 0.
- STKALIGN = 1, SP 0x…c4(4 mod 8): 프레임이 0x…a0 — **36바이트**, 쌓인 xPSR = 0x21000**2**00으로 bit9 = 1. 하드웨어가 4바이트 패딩을 끼웠다.
- 복귀 후 SP가 정확히 0x…c4로 돌아왔다. bit9 덕분에 패딩을 되돌렸다.
- STKALIGN = 0으로 끄면 0x…a4에 32바이트만 쌓는다 — **ISR이 4 mod 8 SP로 실행된다**. 이 상태에서 ISR이 `double`을 스택에 두거나 `LDRD`를 쓰는 라이브러리를 부르면 AAPCS 위반이다. QEMU는 이 차이로 fault를 내지 않지만, 정렬을 가정한 코드는 조용히 틀린 값을 낼 수 있다. 실무에서 STKALIGN을 끌 이유는 없다.

### 4.3 RTOS와 연결

FreeRTOS의 `pxPortInitialiseStack()` 앞부분에서 `pxTopOfStack--;  /* Offset added to account for the way the MCU uses the stack on entry/exit of interrupts */` 한 줄과 `tasks.c`가 스택 top을 `portBYTE_ALIGNMENT`(Cortex-M은 8)로 맞추는 코드는 이 규칙 때문이다. 가짜 프레임도 8바이트 정렬된 주소에 만들어야 한다(예제 11에서 `aligned(8)`).

---

## 5. EXC_RETURN — 복귀는 "특수한 주소로 점프"

### 5.1 직관

예외 진입 후 LR에는 진짜 주소 대신 0xFFFFFFxx 꼴의 값이 들어 있다. 이 영역(0xF0000000 이상)은 실행할 수 없는 주소라서, CPU는 Handler 모드에서 **이 값이 PC에 로드되는 순간**을 "예외 복귀 요청"으로 해석한다. 아래 4비트가 "어디로, 어떤 스택으로 돌아가나"를 알려 준다. 쉽게 말해 **EXC_RETURN = 하드웨어가 자기에게 남긴 메모**다: "돌아갈 때 Thread로 가고, PSP에서 8워드를 꺼내라".

어떤 명령이든 PC에 그 값이 들어가면 복귀가 시작된다: `bx lr`, `pop {r4, pc}`, `ldr pc, [sp], #4`, `ldm sp!, {…, pc}`. 그래서 컴파일러가 만든 평범한 함수 에필로그가 그대로 복귀 명령이 된다(예제 7).

### 5.2 정의 — 값과 비트

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">EXC_RETURN (Armv7-M) — 진입 시 LR에 들어가는 값</text>
<rect x="10" y="32" width="280" height="56" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="150" y="54" font-size="13" text-anchor="middle">bit 31–5: 전부 1</text>
<text x="150" y="76" font-size="12" text-anchor="middle">0xFFFFFFE0 이상 = 복귀 신호</text>
<rect x="290" y="32" width="100" height="56" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="340" y="54" font-size="13" text-anchor="middle">4 FType</text>
<text x="340" y="76" font-size="12" text-anchor="middle">0 = FP 프레임</text>
<rect x="390" y="32" width="100" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="440" y="54" font-size="13" text-anchor="middle">3 Mode</text>
<text x="440" y="76" font-size="12" text-anchor="middle">1 = Thread</text>
<rect x="490" y="32" width="100" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="540" y="54" font-size="13" text-anchor="middle">2 SPSEL</text>
<text x="540" y="76" font-size="12" text-anchor="middle">1 = PSP</text>
<rect x="590" y="32" width="40" height="56" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="610" y="54" font-size="13" text-anchor="middle">1</text>
<text x="610" y="76" font-size="12" text-anchor="middle">0</text>
<rect x="630" y="32" width="40" height="56" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="650" y="54" font-size="13" text-anchor="middle">0</text>
<text x="650" y="76" font-size="12" text-anchor="middle">1</text>
<text x="20" y="122" font-size="13" font-weight="bold">값</text>
<text x="140" y="122" font-size="13" font-weight="bold">복귀할 곳 · 스택 · 프레임</text>
<text x="400" y="122" font-size="13" font-weight="bold">언제 보나</text>
<line x1="10" y1="130" x2="670" y2="130" stroke="currentColor" stroke-width="0.8"/>
<text x="20" y="152" font-size="13">0xFFFFFFF1</text><text x="140" y="152" font-size="12">Handler · MSP · 8워드</text><text x="400" y="152" font-size="12">중첩: 다른 ISR을 선점했을 때</text>
<text x="20" y="176" font-size="13">0xFFFFFFF9</text><text x="140" y="176" font-size="12">Thread · MSP · 8워드</text><text x="400" y="176" font-size="12">bare-metal main 중 IRQ</text>
<text x="20" y="200" font-size="13">0xFFFFFFFD</text><text x="140" y="200" font-size="12">Thread · PSP · 8워드</text><text x="400" y="200" font-size="12">RTOS 태스크 중 IRQ</text>
<line x1="10" y1="210" x2="670" y2="210" stroke="currentColor" stroke-width="0.5" stroke-dasharray="3 3"/>
<text x="20" y="232" font-size="13">0xFFFFFFE1</text><text x="140" y="232" font-size="12">Handler · MSP · FP 26워드</text><text x="400" y="232" font-size="12">FPU 쓰던 ISR을 선점 (B6)</text>
<text x="20" y="256" font-size="13">0xFFFFFFE9</text><text x="140" y="256" font-size="12">Thread · MSP · FP 26워드</text><text x="400" y="256" font-size="12">FPU 쓴 main 중 IRQ (예제 12)</text>
<text x="20" y="280" font-size="13">0xFFFFFFED</text><text x="140" y="280" font-size="12">Thread · PSP · FP 26워드</text><text x="400" y="280" font-size="12">FPU 쓰는 태스크 중 IRQ (B6)</text>
<text x="340" y="308" font-size="12" text-anchor="middle">그 밖의 아래 4비트(예: 0x5, 0x3)는 예약 → 복귀하는 순간 UsageFault(INVPC)</text>
</svg>
```

그림 3 — EXC_RETURN의 비트와 여섯 가지 유효 값. 아래 세 줄(FP 프레임)은 FPU가 있는 코어(M4F/M7/M33…)에서만 나온다 — 자세히는 B6

정리하면 아래 4비트가 0x1 · 0x9 · 0xD 세 가지뿐이다.

- **0x1 (…F1)**: Handler로 돌아간다. Handler는 항상 MSP를 쓰므로 MSP에서 꺼낸다. "이 ISR은 다른 ISR을 선점했다"는 뜻.
- **0x9 (…F9)**: Thread로, MSP에서. RTOS 없는 bare-metal main의 기본값.
- **0xD (…FD)**: Thread로, PSP에서. RTOS 태스크가 돌다가 IRQ를 맞으면 이 값이다. 복귀 후 CONTROL.SPSEL = 1이 되어 Thread가 다시 PSP를 쓴다.
- bit 4(FType) = 0이면 FP 확장 프레임(…E1/E9/ED).

> Armv8-M(M23/M33/M55/M85)에서는 bit 0(ES, 어느 보안 상태로 간 예외인가), bit 5(DCRS), bit 6(S, 프레임이 Secure 스택에 있나)가 의미를 갖는다. 그래서 TrustZone Non-secure 쪽 RTOS는 0xFFFFFFBC 같은 값을 쓴다. Zephyr `arch/arm/core/cortex_m/thread.c`의 `DEFAULT_EXC_RETURN`이 `CONFIG_ARM_NONSECURE_FIRMWARE`면 0xBC, 아니면 0xFD로 나뉘는 이유다. 비트 정의는 `arch/arm/include/cortex_m/exception.h`(`EXC_RETURN_SPSEL_Msk`, `EXC_RETURN_MODE_Msk`, `EXC_RETURN_STACK_FRAME_TYPE_Msk`, `EXC_RETURN_CALLEE_STACK_Msk`)에 있다. 보안 쪽 이야기는 F6.

### 예제 5 — 세 가지 EXC_RETURN을 직접 만들어 보기

무엇을 확인하나: (A) Thread + MSP에서 IRQ → 0xFFFFFFF9, (B) Thread를 PSP로 바꾼 뒤 IRQ와 SVC → 0xFFFFFFFD, (C) IRQ8 안에서 더 높은 IRQ10을 pend → IRQ10의 LR은 0xFFFFFFF1. 그리고 각각 프레임이 어느 스택에 쌓였는지 MSP · PSP 값으로 확인.

```c
/* ex3 — EXC_RETURN 값 세 가지: Thread+MSP(F9), Thread+PSP(FD), Handler 중첩(F1) */
#include "b2.h"
static uint32_t psp_stack[256] __attribute__((aligned(8)));
static volatile int s_nest;

static void report(const char *who, uint32_t lr)
{
    P("  %s LR=0x%x  bit4 FType=%u bit3 Mode=%s bit2 SPSEL=%s  MSP=0x%x PSP=0x%x\n", who, lr,
      (lr >> 4) & 1u, (lr & 8u) ? "Thread " : "Handler", (lr & 4u) ? "PSP" : "MSP", get_msp(), get_psp());
}
void DUALTIMER_IRQHandler(void) { report("IRQ10(hi) ", get_lr()); }
void TIMER0_IRQHandler(void)
{
    uint32_t lr = get_lr();                 /* read before any call overwrites LR */
    report("IRQ8 (lo) ", lr);
    if (s_nest) { s_nest = 0; irq_pend(10); P("  IRQ8 resumes after IRQ10\n"); }
}
void SVC_Handler(void) { report("SVC       ", get_lr()); }

int main(void)
{
    irq_enable(8, 0x80);
    irq_enable(10, 0x40);                  /* smaller number = higher priority */
    P("A) Thread mode on MSP, CONTROL=0x%x\n", get_ctrl());
    irq_pend(8);
    P("B) switch Thread to PSP (psp_stack top = 0x%x)\n", (uint32_t)&psp_stack[256]);
    __asm volatile("msr psp, %0\n msr control, %1\n isb" :: "r"(&psp_stack[256]), "r"(2u) : "memory");
    P("   CONTROL=0x%x, SP now = PSP\n", get_ctrl());
    irq_pend(8);
    __asm volatile("svc 1");
    P("C) nested: IRQ8 pends higher-priority IRQ10\n");
    s_nest = 1; irq_pend(8);
    __asm volatile("msr control, %0\n isb" :: "r"(0u) : "memory");  /* back to MSP */
    return 0;
}
```

```text
A) Thread mode on MSP, CONTROL=0x0
  IRQ8 (lo)  LR=0xfffffff9  bit4 FType=1 bit3 Mode=Thread  bit2 SPSEL=MSP  MSP=0x203fffa8 PSP=0x0
B) switch Thread to PSP (psp_stack top = 0x20000468)
   CONTROL=0x2, SP now = PSP
  IRQ8 (lo)  LR=0xfffffffd  bit4 FType=1 bit3 Mode=Thread  bit2 SPSEL=PSP  MSP=0x203fffc8 PSP=0x20000448
  SVC        LR=0xfffffffd  bit4 FType=1 bit3 Mode=Thread  bit2 SPSEL=PSP  MSP=0x203fffc8 PSP=0x20000448
C) nested: IRQ8 pends higher-priority IRQ10
  IRQ8 (lo)  LR=0xfffffffd  bit4 FType=1 bit3 Mode=Thread  bit2 SPSEL=PSP  MSP=0x203fffc8 PSP=0x20000448
  IRQ10(hi)  LR=0xfffffff1  bit4 FType=1 bit3 Mode=Handler bit2 SPSEL=MSP  MSP=0x203fff88 PSP=0x20000448
  IRQ8 resumes after IRQ10
[board_exit] code=0
```

출력에서 볼 것:

- **A**: LR = 0xfffffff9, PSP = 0(아직 안 씀). 프레임은 MSP에.
- **B**: CONTROL = 0x2(SPSEL = 1)로 바꾼 뒤에는 LR = 0xfffffffd. PSP = 0x20000448 = `psp_stack` top(0x20000468) − 32 — **프레임이 PSP 쪽에 쌓였다**. ISR 안의 MSP(0x203fffc8)는 A 때(0x203fffa8)보다 32바이트 높다 — 이번엔 MSP에 프레임이 안 쌓였기 때문이다. SVC도 똑같이 FD. 이것이 RTOS의 모양이다: **태스크 스택(PSP)에는 그 태스크의 프레임, MSP는 ISR들만 공유**.
- **C**: IRQ10의 LR = 0xfffffff1(Handler · MSP). IRQ10의 MSP(0x203fff88)는 IRQ8 때보다 64바이트 낮다 — IRQ8의 프레임은 PSP에 있었지만, IRQ8 **핸들러 자신**이 MSP 위에서 돌던 중이라 IRQ10의 프레임(32 B)과 IRQ8 C 코드의 push가 MSP에 쌓였다.
- 실전 코드는 이렇게 함수 중간에 SP를 바꾸지 않는다. RTOS는 첫 태스크를 시작할 때 **한 번**, 예외 복귀로 PSP로 넘어간다(예제 11, C3).

### 예제 6 — EXC_RETURN과 fault 상태 비트를 사람 말로 (Python)

무엇을 확인하나: 비트 규칙을 코드로 써 두면 디버깅 때 바로 쓸 수 있다. 예제 5 · 12 · 13에서 본 값을 넣었다.

```python
# decode.py — EXC_RETURN과 CFSR/HFSR 값을 사람이 읽는 말로 (ex3 · ex9 · ex11 출력값 입력)
EXC = [0xFFFFFFF1, 0xFFFFFFF9, 0xFFFFFFFD, 0xFFFFFFE1, 0xFFFFFFE9, 0xFFFFFFED, 0xFFFFFFF5]
def exc_return(v):
    if v >> 28 != 0xF: return "not an EXC_RETURN"
    low = v & 0xF
    if low not in (0x1, 0x9, 0xD): return "RESERVED -> INVPC UsageFault"
    mode = "Thread" if v & 8 else "Handler"
    sp = "PSP" if v & 4 else "MSP"
    frame = "8 words" if v & 0x10 else "26 words (+S0-S15, FPSCR, pad)"
    return f"return to {mode:7s} on {sp}, frame {frame}"
CFSR_BITS = {0: "IACCVIOL", 1: "DACCVIOL", 3: "MUNSTKERR", 4: "MSTKERR", 7: "MMARVALID",
             8: "IBUSERR", 9: "PRECISERR", 10: "IMPRECISERR", 11: "UNSTKERR", 12: "STKERR", 15: "BFARVALID",
             16: "UNDEFINSTR", 17: "INVSTATE", 18: "INVPC", 19: "NOCP", 24: "UNALIGNED", 25: "DIVBYZERO"}
def cfsr(v):
    return " ".join(n for b, n in CFSR_BITS.items() if v >> b & 1) or "-"
def hfsr(v):
    return " ".join(n for b, n in {1: "VECTTBL", 30: "FORCED", 31: "DEBUGEVT"}.items() if v >> b & 1)
for v in EXC:
    print(f"EXC_RETURN 0x{v:08X}: {exc_return(v)}")
print()
for case, c, h in [(1, 0x8200, 0x40000000), (2, 0x20000, 0x40000000), (3, 0x20000, 0x40000000),
                   (4, 0x40000, 0x40000000), (5, 0x2000000, 0x40000000)]:
    print(f"CASE {case}: CFSR=0x{c:08X} -> {cfsr(c):22s} HFSR=0x{h:08X} -> {hfsr(h)}")
```

```text
EXC_RETURN 0xFFFFFFF1: return to Handler on MSP, frame 8 words
EXC_RETURN 0xFFFFFFF9: return to Thread  on MSP, frame 8 words
EXC_RETURN 0xFFFFFFFD: return to Thread  on PSP, frame 8 words
EXC_RETURN 0xFFFFFFE1: return to Handler on MSP, frame 26 words (+S0-S15, FPSCR, pad)
EXC_RETURN 0xFFFFFFE9: return to Thread  on MSP, frame 26 words (+S0-S15, FPSCR, pad)
EXC_RETURN 0xFFFFFFED: return to Thread  on PSP, frame 26 words (+S0-S15, FPSCR, pad)
EXC_RETURN 0xFFFFFFF5: RESERVED -> INVPC UsageFault

CASE 1: CFSR=0x00008200 -> PRECISERR BFARVALID    HFSR=0x40000000 -> FORCED
CASE 2: CFSR=0x00020000 -> INVSTATE               HFSR=0x40000000 -> FORCED
CASE 3: CFSR=0x00020000 -> INVSTATE               HFSR=0x40000000 -> FORCED
CASE 4: CFSR=0x00040000 -> INVPC                  HFSR=0x40000000 -> FORCED
CASE 5: CFSR=0x02000000 -> DIVBYZERO              HFSR=0x40000000 -> FORCED
```

출력에서 볼 것: 0xFFFFFFF5는 아래 4비트 0x5(Handler인데 PSP) — 그런 조합은 없으므로 예약 값이고, 예제 13 CASE 4에서 실제로 INVPC fault를 낸다. CFSR 해독은 예제 13 결과를 미리 보여 준 것이다.

### 5.3 함정

- **ISR 안에서 LR을 덮어쓴 뒤 `bx lr`**: 함수를 부르면 LR이 바뀐다. 컴파일러는 그래서 `push {…, lr}` … `pop {…, pc}`를 쓴다. naked 핸들러에서 `bl`을 쓸 때는 직접 LR을 보존해야 한다(예제 3의 `push {r1, lr}`).
- **EXC_RETURN을 일반 함수 포인터처럼 저장했다가 Thread에서 호출**: Thread 모드에서 0xFFFFFFF9로 분기하면 복귀가 아니라 실행 불가 주소로의 점프 → fault.
- **"ISR이 끝나면 인터럽트 플래그가 자동으로 지워지나?"** NVIC의 active 비트는 복귀로 지워지지만, **주변장치의 인터럽트 원인**(타이머의 INTCLR, UART 상태)은 ISR이 지워야 한다. 안 지우면 복귀 직후 같은 IRQ가 다시 pend된다 — tail-chain으로 무한 반복.

---

## 6. ISR이 그냥 C 함수인 이유 — AAPCS와 정확히 맞물린다

### 6.1 직관

AAPCS(Arm 호출 규약, 자세히는 BM C3)는 레지스터를 두 무리로 나눈다.

- **caller-saved(scratch)**: R0–R3, R12, LR(그리고 플래그). 함수를 부르면 망가질 수 있다고 가정한다.
- **callee-saved**: R4–R11, SP. 함수가 쓰려면 스스로 저장했다가 돌려놓는다.

Cortex-M 하드웨어 프레임은 **정확히 caller-saved 무리 + PC + xPSR**이다. 그러니 ISR 입장에서는 "누군가 나를 평범하게 불렀다"와 구별이 안 된다. ISR이 R4–R11을 쓰고 싶으면 컴파일러가 평소처럼 push/pop한다. 끝은 평소처럼 `bx lr` 또는 `pop {…, pc}` — 그 LR이 EXC_RETURN이라서 복귀가 된다. Cortex-M3 TRM도 "Interrupt return is passed as a data field in the LR, so ISR functions can be normal C/C++ functions, and do not require a veneer"라고 적는다.

### 예제 7 — 평범한 C ISR을 컴파일러가 어떻게 만드나 (`objdump`)

무엇을 확인하나: 특별한 attribute 없이 쓴 ISR 두 개의 기계어. 하나는 scratch 레지스터만 쓰는 leaf, 하나는 함수를 부르며 값을 유지하는 ISR.

```c
/* isr_c.c — 평범한 C 함수로 쓴 ISR 두 개: 컴파일러가 무엇을 저장하나 */
#include <stdint.h>
#define REG32(a) (*(volatile uint32_t *)(a))
volatile uint32_t g_count, g_sum;
extern void process(uint32_t v);

void TIMER0_IRQHandler(void)            /* leaf: uses only scratch registers */
{
    REG32(0x4000000Cu) = 1;               /* TIMER0 INTCLR */
    g_count++;
}
void UART0RX_IRQHandler(void)            /* calls a function and keeps values across it */
{
    uint32_t a = REG32(0x40004000u), b = REG32(0x40004004u);
    process(a);
    g_sum += a + b;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/B2 && arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 -O2 -Wall -Wextra \
  -c /Users/donh/.rtos_scratch/B2/isr_c.c -o isr_c.o && arm-none-eabi-objdump -d --no-show-raw-insn isr_c.o'
```

```text
00000000 <TIMER0_IRQHandler>:
   0:	mov.w	r3, #1073741824	@ 0x40000000
   4:	movs	r1, #1
   6:	ldr	r2, [pc, #12]	@ (14 <TIMER0_IRQHandler+0x14>)
   8:	str	r1, [r3, #12]
   a:	ldr	r3, [r2, #0]
   c:	add	r3, r1
   e:	str	r3, [r2, #0]
  10:	bx	lr
  12:	nop
  14:	.word	0x00000000

00000018 <UART0RX_IRQHandler>:
  18:	push	{r3, r4, r5, lr}
  1a:	mov.w	r3, #1073758208	@ 0x40004000
  1e:	ldr	r4, [r3, #0]
  20:	ldr	r5, [r3, #4]
  22:	mov	r0, r4
  24:	bl	0 <process>
  28:	ldr	r3, [pc, #8]	@ (34 <UART0RX_IRQHandler+0x1c>)
  2a:	add	r4, r5
  2c:	ldr	r2, [r3, #4]
  2e:	add	r4, r2
  30:	str	r4, [r3, #4]
  32:	pop	{r3, r4, r5, pc}
  34:	.word	0x00000000
```

출력에서 볼 것:

- `TIMER0_IRQHandler`: r1 · r2 · r3만 쓴다 → **push가 하나도 없다**. 진입 프롤로그 0개, 끝은 `bx lr`. 그 r1 · r2 · r3는 하드웨어가 이미 저장했다.
- `UART0RX_IRQHandler`: `process()` 호출 너머로 값을 유지해야 해서 callee-saved r4 · r5를 쓴다 → 컴파일러가 `push {r3, r4, r5, lr}`. **r3는 쓰려고 넣은 게 아니라 8바이트 정렬(4개 = 16바이트)을 맞추려는 패딩**이다(§4와 같은 규칙). 끝은 `pop {r3, r4, r5, pc}` — 스택에서 꺼낸 LR 값(= EXC_RETURN)이 PC로 들어가며 예외 복귀가 일어난다.
- GCC의 `__attribute__((interrupt))`는 Cortex-M에서 필요 없다. 벡터 테이블에 C 함수 주소를 그냥 넣는다.

### 6.2 Don 경험과 연결

SSD 펌웨어의 Cortex-R ISR 진입 매크로(레지스터 push → 모드 전환 → C 호출 → 복귀 정리)를 떠올리면, Cortex-M은 그 매크로를 **실리콘으로 구웠다**고 보면 된다. 대가도 있다. 진입 때마다 무조건 8워드(FP면 최대 26워드)를 쓰므로 **ISR 스택(MSP)과 태스크 스택 크기를 잡을 때 프레임 크기를 넣어야 한다**(F3). 이득은 진입 지연이 결정적이라는 것 — 소프트웨어 매크로 길이가 아니라 하드웨어 사이클로 정해진다.

---

## 7. 선점과 중첩 — 높은 우선순위가 ISR 한가운데로 들어온다

### 7.1 직관

ISR이 도는 중에 **더 높은**(숫자가 더 작은) 우선순위의 IRQ가 오면, 하드웨어는 지금 ISR을 Thread 코드처럼 취급해서 또 8워드를 MSP에 쌓고 새 ISR로 간다. 이게 **선점(preemption)**이고, ISR 안에 ISR이 들어간 상태를 **중첩(nesting)**이라고 한다. 같거나 낮은 우선순위는 기다렸다가 지금 ISR이 끝날 때 들어온다(§8 tail-chaining).

### 예제 8 — 세 우선순위 레벨, 25 MHz 카운터 타임스탬프 로그

무엇을 확인하나: IRQ8(낮음 0xC0)이 도는 중 IRQ10(높음 0x40)을 pend하면 즉시 선점하는지, IRQ10 안에서 IRQ9(중간 0x80)를 pend하면 IRQ10이 끝날 때까지 기다리는지, 그리고 IRQ9가 IRQ8보다 먼저 도는지(IRQ8보다 높으니까). IRQ8 안에서 같은 우선순위 IRQ1을 pend하면 IRQ8이 끝난 뒤에 도는지.

```c
/* ex4 — 선점(중첩)과 tail-chaining 순서를 25 MHz 카운터 타임스탬프로 기록 */
#include "b2.h"
#define UART0TX_IRQ 1
typedef struct { const char *ev; uint32_t t, msp, lr; } rec_t;
static rec_t s_log[24]; static volatile int s_n;
static inline void LOG(const char *ev, uint32_t lr) { s_log[s_n++] = (rec_t){ ev, CYC(), get_msp(), lr }; }
static void work(unsigned n) { for (volatile unsigned i = 0; i < n; i++) { } }

void UART0TX_IRQHandler(void)   { LOG("IRQ1  enter", get_lr()); work(100); LOG("IRQ1  exit ", 0); }
void TIMER1_IRQHandler(void)    { LOG("IRQ9  enter", get_lr()); work(150); LOG("IRQ9  exit ", 0); }
void DUALTIMER_IRQHandler(void)
{
    LOG("IRQ10 enter", get_lr()); work(100);
    irq_pend(9);                           /* mid priority: must wait for IRQ10 to finish */
    LOG("IRQ10 pend9", 0); work(100); LOG("IRQ10 exit ", 0);
}
void TIMER0_IRQHandler(void)
{
    LOG("IRQ8  enter", get_lr()); work(100);
    irq_pend(10);                          /* high priority: preempts right here */
    LOG("IRQ8  back ", 0);
    irq_pend(UART0TX_IRQ);                 /* same priority as IRQ8: waits, then tail-chains */
    LOG("IRQ8  pend1", 0); work(100); LOG("IRQ8  exit ", 0);
}

int main(void)
{
    irq_enable(8, 0xC0); irq_enable(UART0TX_IRQ, 0xC0);   /* low */
    irq_enable(9, 0x80);                                  /* mid */
    irq_enable(10, 0x40);                                 /* high */
    LOG("thread pend8", 0);
    irq_pend(8);
    LOG("thread back ", 0);
    uint32_t t0 = s_log[0].t;
    for (int i = 0; i < s_n; i++) {
        P("%s t=%u cyc (%u us) MSP=0x%x", s_log[i].ev, s_log[i].t - t0, (s_log[i].t - t0) / 25, s_log[i].msp);
        if (s_log[i].lr) P(" LR=0x%x", s_log[i].lr);
        P("\n");
    }
    return 0;
}
```

```text
thread pend8 t=0 cyc (0 us) MSP=0x203fffd0
IRQ8  enter t=16 cyc (0 us) MSP=0x203fff98 LR=0xfffffff9
IRQ10 enter t=516 cyc (20 us) MSP=0x203fff60 LR=0xfffffff1
IRQ10 pend9 t=1014 cyc (40 us) MSP=0x203fff60
IRQ10 exit  t=1508 cyc (60 us) MSP=0x203fff60
IRQ9  enter t=1524 cyc (60 us) MSP=0x203fff60 LR=0xfffffff1
IRQ9  exit  t=2259 cyc (90 us) MSP=0x203fff60
IRQ8  back  t=2274 cyc (90 us) MSP=0x203fff98
IRQ8  pend1 t=2287 cyc (91 us) MSP=0x203fff98
IRQ8  exit  t=2780 cyc (111 us) MSP=0x203fff98
IRQ1  enter t=2796 cyc (111 us) MSP=0x203fffa0 LR=0xfffffff9
IRQ1  exit  t=3290 cyc (131 us) MSP=0x203fffa0
thread back  t=3309 cyc (132 us) MSP=0x203fffd0
[board_exit] code=0
```

출력에서 볼 것:

- IRQ8 enter(t = 16)의 LR = …F9(Thread에서 옴), IRQ10 enter(t = 516)의 LR = …F1(**Handler에서 옴 = 선점**). IRQ10의 MSP(0x…60)는 IRQ8(0x…98)보다 56바이트 낮다 — 하드웨어 프레임 32 + IRQ8 C 코드의 push 24.
- IRQ10이 `pend9`를 해도 IRQ9는 바로 오지 않는다. IRQ10 exit(t = 1508) **다음** IRQ9 enter(t = 1524). 그리고 IRQ9는 IRQ8로 돌아가기 **전에** 돈다 — IRQ9(0x80)가 스택에 있는 IRQ8(0xC0)보다 높기 때문이다. IRQ9의 LR도 …F1, MSP가 IRQ10과 같다(0x…60) — 프레임을 다시 쌓지 않았다는 신호다(tail-chain, §8).
- IRQ8은 t = 516부터 2274까지 **1758 카운트(70 µs) 동안 선점당해 멈춰 있었다**. 낮은 우선순위 ISR의 응답 시간은 위의 모든 ISR 시간만큼 늘어난다 — 응답 시간 분석(N3)의 간섭 항이 바로 이것이다.
- IRQ1(IRQ8과 같은 0xC0)은 IRQ8 exit 뒤에야 enter. LR은 …F9 — 스택에 남은 것이 Thread뿐이므로 Thread로 돌아갈 차례에 tail-chain된 것이다.

### 예제 9 — 측정한 타임스탬프로 타임라인 그리기 (Python)

무엇을 확인하나: 예제 8의 로그를 파싱해서 간트 차트 SVG(그림 4)와, 이 노트에서 잰 프레임 크기 막대(그림 8)를 만든다.

```python
# chart.py — ex4 로그(25 MHz 카운터 타임스탬프)로 중첩 타임라인 SVG, 측정한 프레임 크기로 막대 SVG를 만든다
import re, sys
log = open(sys.argv[1]).read()
ev = [(m[1].strip(), int(m[2])) for m in re.finditer(r"^(\S+ +\S+) +t=(\d+) cyc", log, re.M)]
T = dict(ev)
lanes = [("thread", "#888", [(0, T["IRQ8  enter"]), (T["thread back"], 3400)], []),
         ("IRQ8 낮음 0xC0", "#4a7bd0", [(T["IRQ8  enter"], T["IRQ10 enter"]), (T["IRQ8  back"], T["IRQ8  exit"])],
          [(T["IRQ10 enter"], T["IRQ8  back"])]),
         ("IRQ1 낮음 0xC0", "#4a7bd0", [(T["IRQ1  enter"], T["IRQ1  exit"])], [(T["IRQ8  pend1"], T["IRQ1  enter"])]),
         ("IRQ9 중간 0x80", "#3f9a6b", [(T["IRQ9  enter"], T["IRQ9  exit"])], [(T["IRQ10 pend9"], T["IRQ9  enter"])]),
         ("IRQ10 높음 0x40", "#d0564a", [(T["IRQ10 enter"], T["IRQ10 exit"])], [])]
X0, X1, CMAX = 125, 665, 3400
x = lambda c: X0 + (X1 - X0) * c / CMAX
o = ['<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">']
for i, (name, col, run, wait) in enumerate(lanes):
    y = 30 + i * 40
    o.append(f'<text x="10" y="{y + 18}" font-size="12">{name}</text>')
    for a, b in wait:
        o.append(f'<rect x="{x(a):.1f}" y="{y + 4}" width="{x(b) - x(a):.1f}" height="20" fill="{col}" '
                 f'fill-opacity="0.08" stroke="{col}" stroke-dasharray="4 3"/>')
    for a, b in run:
        o.append(f'<rect x="{x(a):.1f}" y="{y}" width="{x(b) - x(a):.1f}" height="28" fill="{col}" '
                 f'fill-opacity="0.45" stroke="{col}"/>')
for lbl, key, lane in [("pend 9", "IRQ10 pend9", 3), ("pend 1", "IRQ8  pend1", 2)]:
    cx = x(T[key]); y = 30 + lane * 40
    o.append(f'<line x1="{cx:.1f}" y1="{y - 6}" x2="{cx:.1f}" y2="{y + 28}" stroke="currentColor" stroke-width="1.5"/>')
    o.append(f'<text x="{cx + 4:.1f}" y="{y - 1}" font-size="12">{lbl}</text>')
yb = 236
o.append(f'<line x1="{X0}" y1="{yb}" x2="{X1}" y2="{yb}" stroke="currentColor"/>')
for us in range(0, 140, 20):
    cx = x(us * 25)
    o.append(f'<line x1="{cx:.1f}" y1="{yb}" x2="{cx:.1f}" y2="{yb + 5}" stroke="currentColor"/>')
    o.append(f'<text x="{cx:.1f}" y="{yb + 19}" font-size="12" text-anchor="middle">{us}</text>')
o.append(f'<text x="{(X0 + X1) / 2:.0f}" y="{yb + 40}" font-size="12" text-anchor="middle">'
         'µs (QEMU icount, 25 MHz 카운터) — 진한 막대 = 실행, 점선 = 대기 또는 선점당함</text>')
o.append('</svg>')
open(sys.argv[2], "w").write("\n".join(o) + "\n")
# measured frame sizes (bytes) from ex2/ex7/ex11/ex10 runs
bars = [("기본 프레임 (ex2)", 32), ("정렬 패딩 (ex7)", 36), ("FreeRTOS 전환 (ex10)", 64), ("FP 프레임 M4F (ex11)", 104)]
o = ['<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">']
cols = ["#4a7bd0", "#e08a3c", "#3f9a6b", "#d0564a"]
for i, ((name, v), c) in enumerate(zip(bars, cols)):
    y = 20 + i * 46; w = v * 3
    o.append(f'<text x="10" y="{y + 20}" font-size="13">{name}</text>')
    o.append(f'<rect x="190" y="{y}" width="{w}" height="30" fill="{c}" fill-opacity="0.45" stroke="{c}"/>')
    o.append(f'<text x="{196 + w}" y="{y + 20}" font-size="13">{v} B = {v // 4} words</text>')
o.append('<line x1="190" y1="12" x2="190" y2="200" stroke="currentColor"/>')
o.append('<text x="190" y="220" font-size="12">0</text><text x="310" y="220" font-size="12" text-anchor="middle">40</text>'
         '<text x="430" y="220" font-size="12" text-anchor="middle">80 bytes</text>')
o.append('</svg>')
open(sys.argv[3], "w").write("\n".join(o) + "\n")
print("events:", len(ev), " IRQ8 preempted for", T["IRQ8  back"] - T["IRQ10 enter"], "cycles,",
      "tail-chain gaps:", T["IRQ9  enter"] - T["IRQ10 exit"], T["IRQ1  enter"] - T["IRQ8  exit"], "cycles")
```

```sh
limactl shell elinux -- python3 /Users/donh/.rtos_scratch/B2/chart.py /var/tmp/rtos/B2/logs/ex4.log \
  /var/tmp/rtos/B2/timeline.svg /var/tmp/rtos/B2/frames.svg
```

```text
events: 13  IRQ8 preempted for 1758 cycles, tail-chain gaps: 16 16 cycles
```

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="48" font-size="12">thread</text>
<rect x="125.0" y="30" width="2.5" height="28" fill="#888" fill-opacity="0.45" stroke="#888"/>
<rect x="650.5" y="30" width="14.5" height="28" fill="#888" fill-opacity="0.45" stroke="#888"/>
<text x="10" y="88" font-size="12">IRQ8 낮음 0xC0</text>
<rect x="207.0" y="74" width="279.2" height="20" fill="#4a7bd0" fill-opacity="0.08" stroke="#4a7bd0" stroke-dasharray="4 3"/>
<rect x="127.5" y="70" width="79.4" height="28" fill="#4a7bd0" fill-opacity="0.45" stroke="#4a7bd0"/>
<rect x="486.2" y="70" width="80.4" height="28" fill="#4a7bd0" fill-opacity="0.45" stroke="#4a7bd0"/>
<text x="10" y="128" font-size="12">IRQ1 낮음 0xC0</text>
<rect x="488.2" y="114" width="80.8" height="20" fill="#4a7bd0" fill-opacity="0.08" stroke="#4a7bd0" stroke-dasharray="4 3"/>
<rect x="569.1" y="110" width="78.5" height="28" fill="#4a7bd0" fill-opacity="0.45" stroke="#4a7bd0"/>
<text x="10" y="168" font-size="12">IRQ9 중간 0x80</text>
<rect x="286.0" y="154" width="81.0" height="20" fill="#3f9a6b" fill-opacity="0.08" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<rect x="367.0" y="150" width="116.7" height="28" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="10" y="208" font-size="12">IRQ10 높음 0x40</text>
<rect x="207.0" y="190" width="157.6" height="28" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<line x1="286.0" y1="144" x2="286.0" y2="178" stroke="currentColor" stroke-width="1.5"/>
<text x="290.0" y="149" font-size="12">pend 9</text>
<line x1="488.2" y1="104" x2="488.2" y2="138" stroke="currentColor" stroke-width="1.5"/>
<text x="492.2" y="109" font-size="12">pend 1</text>
<line x1="125" y1="236" x2="665" y2="236" stroke="currentColor"/>
<line x1="125.0" y1="236" x2="125.0" y2="241" stroke="currentColor"/>
<text x="125.0" y="255" font-size="12" text-anchor="middle">0</text>
<line x1="204.4" y1="236" x2="204.4" y2="241" stroke="currentColor"/>
<text x="204.4" y="255" font-size="12" text-anchor="middle">20</text>
<line x1="283.8" y1="236" x2="283.8" y2="241" stroke="currentColor"/>
<text x="283.8" y="255" font-size="12" text-anchor="middle">40</text>
<line x1="363.2" y1="236" x2="363.2" y2="241" stroke="currentColor"/>
<text x="363.2" y="255" font-size="12" text-anchor="middle">60</text>
<line x1="442.6" y1="236" x2="442.6" y2="241" stroke="currentColor"/>
<text x="442.6" y="255" font-size="12" text-anchor="middle">80</text>
<line x1="522.1" y1="236" x2="522.1" y2="241" stroke="currentColor"/>
<text x="522.1" y="255" font-size="12" text-anchor="middle">100</text>
<line x1="601.5" y1="236" x2="601.5" y2="241" stroke="currentColor"/>
<text x="601.5" y="255" font-size="12" text-anchor="middle">120</text>
<text x="395" y="276" font-size="12" text-anchor="middle">µs (QEMU icount, 25 MHz 카운터) — 진한 막대 = 실행, 점선 = 대기 또는 선점당함</text>
</svg>
```

그림 4 — **측정 차트**: 예제 8의 실제 타임스탬프(QEMU icount, 25 MHz 카운터). IRQ8은 IRQ10에 선점당해 70 µs를 기다리고(점선), IRQ9는 IRQ10이 끝나자마자 tail-chain으로 들어오며, IRQ1은 IRQ8이 끝난 뒤에야 돈다. 세로선은 소프트웨어 pend 시점

출력에서 볼 것: tail-chain 간격이 둘 다 16카운트(= 약 20명령)다. Thread → IRQ8 진입 간격(16카운트)과 **똑같다**. QEMU에서는 진입이든 tail-chain이든 스태킹 비용이 0이고 C 코드의 로그 · 에필로그 · 프롤로그 명령만 보이기 때문이다. **이 숫자로 tail-chain이 빠르다고 결론 내리면 안 된다.** 실칩 사이클은 §8.2의 Arm 문서 값을 쓴다.

### 7.2 중첩 깊이와 MSP 크기

선점은 **서로 다른 선점 우선순위 레벨 수만큼만** 깊어질 수 있다(같은 레벨끼리는 선점하지 않는다). 그러니 MSP 최악 사용량 = Σ(레벨별로 가장 깊게 쓰는 ISR의 스택 + 프레임 32 B 또는 FP 104 B). 우선순위 비트를 몇 개 쓰는지(B3)와 PRIGROUP으로 선점 레벨 수를 줄이는 것이 MSP를 아끼는 방법이다. RTOS에서는 태스크 스택에 프레임 하나만 들어가고(첫 진입만 PSP), 중첩은 전부 MSP에 쌓인다 — FreeRTOS가 "ISR 스택 = MSP" 하나를 공유하는 설계다(F3).

---

## 8. tail-chaining · late-arrival · pop preemption — 하드웨어의 세 가지 지름길

### 8.1 정의 (Cortex-M3 TRM r2p0 §5.5–5.8)

- **tail-chaining**: ISR이 끝날 때 스택에 있는 것(돌아갈 Thread나 ISR)보다 높은 우선순위의 pending 예외가 있으면, **8워드 pop과 다시 push를 건너뛰고** 바로 그 예외의 벡터를 읽는다. 스택 내용은 그대로이므로 할 필요가 없는 일이다.
- **late-arrival**: 낮은 우선순위 예외의 **스태킹 도중**에 더 높은 우선순위 예외가 오면, 스태킹은 그대로 계속하고(쌓는 내용은 같으니까) **벡터만 높은 쪽으로 바꿔** 읽는다. 높은 쪽이 먼저 실행되고, 끝나면 낮은 쪽이 tail-chain으로 들어온다. 핸들러 첫 명령이 실행 단계에 들어가기 전까지 가능하다.
- **pop preemption**(TRM 문구는 "If a new interrupt arrives during the state restoration…"): 복귀하며 **pop하는 도중**에 새 인터럽트가 오면 pop을 중단하고 tail-chain으로 그 인터럽트를 처리한다.

### 8.2 사이클 — Arm 문서 값 (측정 아님)

| 항목 | Cortex-M3 / M4 | 출처 |
|---|---|---|
| IRQ → ISR 첫 명령 | 12 사이클 | Cortex-M3 TRM r2p0 §5.5 그림 5-2 "12-cycle latency"; Arm 블로그(Yiu, 2016) 표 M3 12 · M4 12 |
| tail-chain (ISR 끝 → 다음 ISR 첫 명령) | 6 사이클 | Cortex-M3 TRM §5.6 "starts execution six cycles after exiting the previous ISR"; 같은 블로그 "just six cycles in the Cortex-M3 and Cortex-M4" |
| 참고: Cortex-M0 / M0+ 진입 | 16 / 15 사이클 | 같은 블로그 표 |

조건: **zero wait state 메모리**, 다른 버스 마스터 경합 없음, 더 높은 예외에 막히지 않음. 실제 칩에서는 플래시 wait state, 벡터 · 스택이 있는 메모리의 속도, 끝나야 하는 멀티사이클 명령(LDM/STM은 중단 후 재개)이 더한다. 480 MHz STM32H7에서 12사이클은 25 ns지만, 측정하면 그보다 길게 나오는 이유다 — 실칩에서는 DWT CYCCNT나 GPIO + scope로 잰다(M6). **QEMU는 이 사이클을 모델링하지 않는다.**

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="40" font-size="13" font-weight="bold">일반 진입</text>
<rect x="130" y="20" width="90" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="175" y="40" font-size="12" text-anchor="middle">Thread</text>
<rect x="220" y="20" width="180" height="30" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="310" y="40" font-size="12" text-anchor="middle">push 8 + 벡터 = 12 cyc</text>
<rect x="400" y="20" width="160" height="30" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="480" y="40" font-size="12" text-anchor="middle">ISR 첫 명령 …</text>
<text x="10" y="110" font-size="13" font-weight="bold">tail-chaining</text>
<rect x="130" y="90" width="170" height="30" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="215" y="110" font-size="12" text-anchor="middle">ISR A … bx lr</text>
<rect x="300" y="90" width="60" height="30" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="330" y="110" font-size="12" text-anchor="middle">6 cyc</text>
<rect x="360" y="90" width="200" height="30" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="460" y="110" font-size="12" text-anchor="middle">ISR B (같은 프레임 위)</text>
<text x="300" y="138" font-size="12">pop 8 + push 8 생략 — 스택은 그대로</text>
<text x="10" y="180" font-size="13" font-weight="bold">late-arrival</text>
<rect x="130" y="160" width="70" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="165" y="180" font-size="12" text-anchor="middle">Thread</text>
<rect x="200" y="160" width="200" height="30" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="300" y="180" font-size="12" text-anchor="middle">push (low 때문에 시작)</text>
<rect x="400" y="160" width="100" height="30" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="450" y="180" font-size="12" text-anchor="middle">high ISR</text>
<rect x="500" y="160" width="40" height="30" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/>
<rect x="540" y="160" width="120" height="30" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="600" y="180" font-size="12" text-anchor="middle">low ISR</text>
<line x1="290" y1="152" x2="290" y2="196" stroke="#d0564a" stroke-width="2"/>
<text x="200" y="210" font-size="12">high 도착 → 벡터만 바꿈 (push는 계속)</text>
<text x="10" y="250" font-size="13" font-weight="bold">pop preemption</text>
<rect x="130" y="230" width="120" height="30" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="190" y="250" font-size="12" text-anchor="middle">ISR A … bx lr</text>
<rect x="250" y="230" width="60" height="30" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c" stroke-dasharray="4 3"/><text x="280" y="250" font-size="12" text-anchor="middle">pop</text>
<rect x="310" y="230" width="70" height="30" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="345" y="250" font-size="12" text-anchor="middle">tail</text>
<rect x="380" y="230" width="140" height="30" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="450" y="250" font-size="12" text-anchor="middle">ISR C</text>
<line x1="300" y1="222" x2="300" y2="266" stroke="#d0564a" stroke-width="2"/>
<text x="250" y="280" font-size="12">pop 도중 새 IRQ → pop 중단, tail-chain</text>
</svg>
```

그림 5 — 하드웨어 지름길 세 가지. 사이클 숫자는 Cortex-M3 TRM(r2p0)의 zero-wait-state 값이고 **이 노트에서 잰 값이 아니다**(QEMU는 스태킹 시간을 모델링하지 않는다). 상자 길이는 비율이 아닌 모식

### 예제 10 — tail-chaining 증거: 스레드가 명령 하나도 실행하지 않았다

무엇을 확인하나: QEMU에서 사이클은 못 재지만, **"두 ISR 사이에 Thread로 돌아가지 않았다"**는 것은 증명할 수 있다. Thread가 r0으로 카운트를 세게 하고, 두 핸들러가 첫 명령에서 프레임의 R0 · PC를 기록한다. 둘이 같으면 그 사이에 Thread 명령은 0개다.

```c
/* ex5 — tail-chaining 증거: 프레임 주소 · 스택에 쌓인 R0(스레드 카운터) · 시각을 핸들러 첫 명령에서 기록 */
#include "b2.h"
typedef struct { int irq; uint32_t frame, lr, r0, pc, t; } rec_t;
static rec_t s_rec[8]; static volatile int s_n;
void rec(uint32_t *f, uint32_t lr, int irq)
{
    s_rec[s_n++] = (rec_t){ irq, (uint32_t)f, lr, f[0], f[6], CYC() };
}
void TIMER0_IRQHandler(void) __attribute__((naked));
void TIMER0_IRQHandler(void)    { __asm volatile("mov r0, sp\n mov r1, lr\n movs r2, #8\n b rec\n"); }
void DUALTIMER_IRQHandler(void) __attribute__((naked));
void DUALTIMER_IRQHandler(void) { __asm volatile("mov r0, sp\n mov r1, lr\n movs r2, #10\n b rec\n"); }

/* pend IRQ8 and IRQ10 with PRIMASK set, then count in r0. `gap` = loop count between the two pends. */
static void scenario(const char *title, int together)
{
    s_n = 0;
    if (together)
        __asm volatile("cpsid i\n ldr r4, =0xE000E200\n ldr r5, =0x500\n str r5, [r4]\n dsb\n isb\n"
                       " movs r0, #0\n cpsie i\n"
                       "1: adds r0, #1\n cmp r0, #40\n bne 1b\n" ::: "r0", "r4", "r5", "cc", "memory");
    else
        __asm volatile(" ldr r4, =0xE000E200\n movs r0, #0\n mov r5, #0x100\n str r5, [r4]\n dsb\n isb\n"
                       "1: adds r0, #1\n cmp r0, #20\n bne 1b\n"
                       " mov r5, #0x400\n str r5, [r4]\n dsb\n isb\n"
                       "2: adds r0, #1\n cmp r0, #40\n bne 2b\n" ::: "r0", "r4", "r5", "cc", "memory");
    P("%s\n", title);
    for (int i = 0; i < s_n; i++)
        P("  IRQ%d%s frame=0x%x LR=0x%x stacked R0=%u stacked PC=0x%x t=%u\n", s_rec[i].irq,
          s_rec[i].irq < 10 ? " " : "", s_rec[i].frame, s_rec[i].lr, s_rec[i].r0, s_rec[i].pc,
          s_rec[i].t - s_rec[0].t);
}

int main(void)
{
    irq_enable(8, 0x80); irq_enable(10, 0x80);
    scenario("1) same priority, pended together (PRIMASK) -> tail-chain", 1);
    scenario("2) same priority, pended 20 loop iterations apart -> two separate entries", 0);
    NVIC_IPR(10) = 0x40;
    scenario("3) IRQ10 higher priority, pended together -> IRQ10 first, IRQ8 tail-chains", 1);
    return 0;
}
```

```text
1) same priority, pended together (PRIMASK) -> tail-chain
  IRQ8  frame=0x203fff98 LR=0xfffffff9 stacked R0=0 stacked PC=0xea t=0
  IRQ10 frame=0x203fff98 LR=0xfffffff9 stacked R0=0 stacked PC=0xea t=17
2) same priority, pended 20 loop iterations apart -> two separate entries
  IRQ8  frame=0x203fff98 LR=0xfffffff9 stacked R0=0 stacked PC=0x15a t=0
  IRQ10 frame=0x203fff98 LR=0xfffffff9 stacked R0=20 stacked PC=0x16e t=69
3) IRQ10 higher priority, pended together -> IRQ10 first, IRQ8 tail-chains
  IRQ10 frame=0x203fff98 LR=0xfffffff9 stacked R0=0 stacked PC=0xea t=0
  IRQ8  frame=0x203fff98 LR=0xfffffff9 stacked R0=0 stacked PC=0xea t=18
[board_exit] code=0
```

출력에서 볼 것:

- **1)** PRIMASK로 막아 두고 IRQ8 · IRQ10을 한 번에 pend → `cpsie i` 직후 둘 다 처리된다. 두 핸들러가 본 프레임은 **같은 주소, 같은 R0 = 0, 같은 PC = 0xea**(`cpsie i` 다음 `adds r0, #1`). IRQ8이 끝나고 IRQ10으로 갈 때 Thread는 한 명령도 실행하지 않았다 = **tail-chain**. 같은 우선순위면 **예외 번호가 작은 쪽(IRQ8)이 먼저**다.
- **2)** 20바퀴 간격으로 pend → IRQ10의 프레임에는 R0 = 20, PC = 0x16e. Thread가 돌아와서 20번 더 센 뒤에 다시 들어갔다. 일반 진입 두 번.
- **3)** IRQ10을 0x40(높음)으로 바꾸고 동시에 pend → IRQ10이 먼저, IRQ8이 tail-chain. 동시에 pending된 것들 사이의 **우선순위 중재**다. late-arrival은 "스태킹 **도중**에 높은 것이 도착"하는 경우의 같은 결과인데, QEMU에는 스태킹 시간이 없어서 "도중"을 만들 수 없다 — **late-arrival 자체는 이 랩에서 재현할 수 없다.**

### 8.3 RTOS와 연결 — PendSV가 tail-chain의 최대 수혜자

RTOS는 PendSV를 **가장 낮은 우선순위**로 둔다(B4). ISR이 "더 높은 태스크가 깨어났다"며 PendSV를 pend하면, PendSV는 그 ISR(그리고 중첩된 모든 ISR)이 끝날 때 **tail-chain으로** 들어온다. Thread로 한 번 돌아갔다가 다시 들어오는 비용 없이, 스택에 프레임이 이미 쌓인 상태에서 바로 컨텍스트 스위치를 한다. FreeRTOS `portYIELD_FROM_ISR()`는 결국 `portmacro.h`의 `portYIELD()` — `portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT` — 로 PendSV를 pend해서 이 동작을 쓴다. 자세히는 B4 · C2.

---

## 9. 가짜 예외 복귀 — 원하는 PC로 "돌아가기"

### 9.1 직관

예외 복귀는 "스택에 있는 8워드를 믿고 꺼내는 것"이다. CPU는 그 8워드를 **누가 썼는지 확인하지 않는다**. 그러니 소프트웨어가 8워드를 직접 만들어 놓고 SP를 그곳으로 돌린 뒤 EXC_RETURN으로 복귀하면, CPU는 **한 번도 실행된 적 없는 함수로 "돌아간다"**. RTOS가 첫 태스크를 시작하는 방법이 정확히 이것이다(C3). 새 태스크의 스택에 "이 태스크가 막 선점당한 것처럼 보이는" 프레임을 만들어 둔다.

### 예제 11 — SVC 핸들러가 PSP 위에 프레임을 만들고 0xFFFFFFFD로 복귀

무엇을 확인하나: 가짜 프레임의 PC로 `task_fn`이 Thread 모드 · PSP에서 시작되는지, R0이 첫 인자로 전달되는지, 프레임의 LR에 심어 둔 함수로 `task_fn`이 return하는지.

```c
/* ex8 — 가짜 예외 프레임으로 "돌아가기": SVC 핸들러가 PSP 위에 프레임을 만들고 EXC_RETURN 0xFFFFFFFD로 복귀 */
#include "b2.h"
static uint32_t task_stack[128] __attribute__((aligned(8)));

static void task_exit(void)
{
    P("task_exit: the task function returned into the LR we planted (IPSR=%u)\n", get_ipsr());
    board_exit(0);
}
static void task_fn(void *arg)
{
    P("task_fn(arg=0x%x): IPSR=%u CONTROL=0x%x SP(PSP)=0x%x MSP=0x%x\n",
      (uint32_t)arg, get_ipsr(), get_ctrl(), get_psp(), get_msp());
}
uint32_t *build_frame(void)               /* returns the new PSP */
{
    uint32_t *sp = &task_stack[128] - 8;    /* 8-word hardware frame, 8-byte aligned */
    sp[0] = 0xCAFE0001u;                    /* R0 = first argument of task_fn */
    sp[1] = sp[2] = sp[3] = 0; sp[4] = 0;   /* R1-R3, R12 */
    sp[5] = (uint32_t)task_exit;            /* LR: where task_fn "returns" to */
    sp[6] = (uint32_t)task_fn & ~1u;        /* PC: bit0 must be 0 in the frame */
    sp[7] = 0x01000000u;                    /* xPSR: only the T bit */
    P("SVC: fake frame at 0x%x  PC=0x%x LR=0x%x xPSR=0x%x\n", (uint32_t)sp, sp[6], sp[5], sp[7]);
    return sp;
}
void SVC_Handler(void) __attribute__((naked));
void SVC_Handler(void)
{
    __asm volatile(
        " bl build_frame      \n"           /* r0 = new PSP */
        " msr psp, r0         \n"
        " ldr lr, =0xFFFFFFFD \n"           /* return to Thread mode, use PSP */
        " bx lr               \n");
}
int main(void)
{
    P("main: IPSR=%u CONTROL=0x%x MSP=0x%x  -> svc 0\n", get_ipsr(), get_ctrl(), get_msp());
    __asm volatile("svc 0");
    P("never printed\n");
    return 1;
}
```

```text
main: IPSR=0 CONTROL=0x0 MSP=0x203ffff0  -> svc 0
SVC: fake frame at 0x20000240  PC=0xc0 LR=0xe9 xPSR=0x1000000
task_fn(arg=0xcafe0001): IPSR=0 CONTROL=0x2 SP(PSP)=0x20000250 MSP=0x203fffd0
task_exit: the task function returned into the LR we planted (IPSR=0)
[board_exit] code=0
```

출력에서 볼 것:

- `main`에서 `svc 0` → SVC 핸들러가 `task_stack` 끝에 8워드를 쓰고 `msr psp` → `bx lr`(0xFFFFFFFD). 하드웨어는 PSP에서 8워드를 꺼내 R0 = 0xcafe0001, PC = 0xc0(`task_fn`), xPSR = 0x01000000을 복원한다.
- `task_fn` 안: IPSR = 0(Thread), CONTROL = 0x2(PSP 사용 — EXC_RETURN의 SPSEL 비트가 CONTROL.SPSEL을 1로 바꿨다), SP = PSP. **main은 다시 실행되지 않는다**("never printed"가 안 나온다).
- `task_fn`이 return하자 프레임의 LR(0xe9 = `task_exit`)로 갔다. FreeRTOS는 이 자리에 `prvTaskExitError`를 넣어 "태스크 함수는 return하면 안 된다"를 잡는다(예제 14).
- 프레임 규칙 세 가지: **PC의 bit0은 0**(T 비트는 xPSR에 따로 있다; FreeRTOS `portSTART_ADDRESS_MASK` = 0xfffffffe), **xPSR = 0x01000000**(T = 1, 나머지 0 — T가 0이면 복귀 직후 INVSTATE fault), **프레임 주소는 8바이트 정렬**.
- MSP = 0x203fffd0 — `svc`로 들어갈 때 MSP에 쌓인 main의 프레임은 꺼내지지 않은 채 남았다. FreeRTOS는 이 공간을 `prvPortStartFirstTask()`에서 MSP를 초기값으로 리셋해 회수한다(§2.3, C3).

### 9.2 다른 용도

- **fault 복구**: fault 핸들러가 프레임의 PC를 "안전한 함수" 주소로 바꿔 복귀하면 그 함수에서 재개한다(테스트 하네스에서 "fault가 나야 통과" 검사에 쓴다).
- **시스템 콜 반환값**: SVC 핸들러가 프레임의 R0을 바꾸면 Thread 쪽에서는 그것이 반환값으로 보인다(C8의 syscall 구현).
- **위험**: 프레임을 망가뜨리는 스택 오버플로는 엉뚱한 PC로 "복귀"하게 만든다. 스택 오버플로가 랜덤한 HardFault로 보이는 이유다(F3, M1).

---

## 10. 다른 CPU 타깃 — FPU가 있으면 프레임이 26워드

### 예제 12 — Cortex-M4F(`mps2-an386`): 부동소수점 명령 하나가 프레임 크기를 바꾼다

무엇을 확인하나: FPU를 켠 M4F에서 FP 명령을 실행하기 전 · 후로 IRQ를 받아 EXC_RETURN과 프레임이 차지한 공간을 비교. lab 템플릿은 M3 전용이라 `templates/common`의 `startup.c` · `board.c` · `mps2_m3.ld`를 **수정 없이** `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`로 다시 컴파일했다(AN386은 AN385와 메모리 맵 · 주변장치가 같아서 그대로 돈다).

```c
/* ex11 — Cortex-M4F (mps2-an386): FPU를 쓴 뒤의 IRQ는 EXC_RETURN 0xFFFFFFE9, 프레임 공간 26워드 */
#include "b2.h"
#define CPACR REG32(0xE000ED88u)
#define FPCCR REG32(0xE000EF34u)           /* bit31 ASPEN, bit30 LSPEN, bit0 LSPACT */
static volatile uint32_t s_lr, s_frame, s_sp_before, s_fpccr_in_isr;
void rec(uint32_t *f, uint32_t lr) { s_frame = (uint32_t)f; s_lr = lr; s_fpccr_in_isr = FPCCR; }
void TIMER0_IRQHandler(void) __attribute__((naked));
void TIMER0_IRQHandler(void) { __asm volatile("mov r0, sp\n mov r1, lr\n b rec\n"); }
static void trial(const char *t)
{
    __asm volatile(" mov r5, sp\n str r5, [%0]\n ldr r4, =0xE000E200\n mov r5, #0x100\n str r5, [r4]\n dsb\n isb\n"
                   :: "r"(&s_sp_before) : "r4", "r5", "memory");
    P("%s CONTROL=0x%x  EXC_RETURN=0x%x  frame used %u bytes  FPCCR in ISR=0x%x (LSPACT=%u)\n", t,
      get_ctrl(), s_lr, s_sp_before - s_frame, s_fpccr_in_isr, s_fpccr_in_isr & 1u);
}
int main(void)
{
    P("CPUID=0x%x FPCCR=0x%x\n", CPUID, FPCCR);
    CPACR |= 0xFu << 20; __asm volatile("dsb\n isb");   /* CP10, CP11 full access */
    irq_enable(8, 0x80);
    trial("1) no FP instruction yet:  ");
    volatile float x = 1.5f; x = x * 3.0f;              /* first FP instruction sets CONTROL.FPCA */
    trial("2) after a float multiply: ");
    __asm volatile("msr control, %0\n isb" :: "r"(0u));  /* clear FPCA by hand */
    trial("3) FPCA cleared again:     ");
    return (int)x == 4 ? 0 : 1;
}
```

```sh
limactl shell elinux -- bash -lc 'O=/var/tmp/rtos/B2/ex11; C=/var/rtoslab/templates/common; mkdir -p $O
  arm-none-eabi-gcc -mthumb -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -O2 -g -std=gnu11 -Wall -Wextra \
    -I/Users/donh/.rtos_scratch/B2 -I$C /Users/donh/.rtos_scratch/B2/ex11.c $C/startup.c $C/board.c \
    -T $C/mps2_m3.ld -nostartfiles --specs=nano.specs -Wl,--gc-sections -o $O/app.elf &&
  timeout 10 qemu-system-arm -machine mps2-an386 -cpu cortex-m4 -nographic -monitor none -serial stdio \
    -semihosting-config enable=on,target=native -icount shift=5,align=off,sleep=off -kernel $O/app.elf'
```

```text
CPUID=0x410fc240 FPCCR=0xc0000000
1) no FP instruction yet:   CONTROL=0x0  EXC_RETURN=0xfffffff9  frame used 32 bytes  FPCCR in ISR=0xc0000000 (LSPACT=0)
2) after a float multiply:  CONTROL=0x4  EXC_RETURN=0xffffffe9  frame used 104 bytes  FPCCR in ISR=0xc0000019 (LSPACT=1)
3) FPCA cleared again:      CONTROL=0x0  EXC_RETURN=0xfffffff9  frame used 32 bytes  FPCCR in ISR=0xc0000018 (LSPACT=0)
[board_exit] code=0
```

출력에서 볼 것:

- CPUID = 0x410fc240 = Cortex-M4 r0p0.
- FP 명령 전: EXC_RETURN = …F9, 32바이트. **float 곱셈 한 번 뒤**: CONTROL = 0x4(FPCA = 1, "이 컨텍스트는 FP를 쓴다"), EXC_RETURN = **…E9**(bit4 = 0), 프레임 공간 **104바이트 = 26워드**(8 + S0–S15 16개 + FPSCR + 정렬용 1워드).
- ISR 안 FPCCR의 LSPACT = 1 — **lazy stacking**: 104바이트 공간만 잡아 두고 S0–S15는 아직 쓰지 않았다. ISR이 FP 명령을 실행할 때에야 실제로 저장된다. 이 동작과 RTOS 비용은 B6에서 자세히 다룬다.
- FPCA를 지우면 다시 32바이트. RTOS는 태스크마다 FPCA 상태가 다르므로 PendSV가 EXC_RETURN bit4를 보고 S16–S31을 저장할지 정한다(Zephyr `swap_helper.S`의 `tst lr, #_EXC_RETURN_FTYPE_Msk` → `vstmia r0, {s16-s31}`).

### 10.1 다른 코어는 어떻게 다른가 (요약)

| 코어 · 아키텍처 | 자동 스태킹 | VTOR | EXC_RETURN에서 다른 점 | 노트 |
|---|---|---|---|---|
| Cortex-M0 (v6-M) | 8워드 | 없음 | FP 없음 → F1/F9/FD만 | B7 |
| Cortex-M0+ (v6-M) | 8워드 | 선택 사항 | 같음 | B7 |
| Cortex-M3 (v7-M) | 8워드 | 있음 | F1/F9/FD | 이 노트 |
| Cortex-M4F/M7 (v7E-M) | 8 또는 26워드 | 있음 | + E1/E9/ED | B6 |
| Cortex-M33/M55/M85 (v8-M) | 8/26 (+ Secure면 callee 레지스터 추가 가능) | 있음(보안 상태별) | S · DCRS · ES 비트, 0xFFFFFFBC 등 | F6 |
| Cortex-R5/R52 (v7-R/v8-R) | **없음** — banked LR/SPSR, 소프트웨어가 저장 | VBAR(R52) / 고정 | EXC_RETURN 개념 없음 | B9 |
| RISC-V (M 모드) | **없음** — mepc · mcause만, 소프트웨어가 저장 | mtvec | `mret`로 복귀 | B10 |

Cortex-M만 이 "하드웨어 스태킹 + EXC_RETURN" 모델이다. Don이 Cortex-R이나 RISC-V로 RTOS를 포팅한다면 진입 어셈블리(레지스터 저장)를 다시 써야 한다는 뜻이다.

---

## 11. HardFault 프레임 해독 — 스택이 범인을 기억한다

### 11.1 직관

fault도 예외이므로 **똑같이 8워드를 쌓는다**. 쌓인 PC는 fault를 낸 명령(정밀 fault일 때), 쌓인 LR은 그 함수를 부른 곳 근처다. 그러니 HardFault 핸들러가 프레임 포인터만 제대로 찾으면 `addr2line`으로 원인 줄을 바로 얻는다. lab의 HardFault 핸들러가 정확히 그렇게 만들어져 있다(`startup.c`: `tst lr, #4` → MSP 또는 PSP → `board_hardfault(frame)`가 PC · LR · xPSR · CFSR · HFSR 출력). fault 종류 · escalation · CFSR 세부는 M1(그리고 BM C8)에서 다루고, 여기서는 **프레임을 읽는 법**만 확인한다.

### 예제 13 — 다섯 가지 fault를 일부러 내고 프레임으로 해독

무엇을 확인하나: 잘못된 주소 읽기, bit0 = 0 함수 포인터, 벡터가 0인 IRQ, 예약된 EXC_RETURN, 0으로 나누기 — 각각에서 쌓인 PC · xPSR이 무엇을 말해 주는지.

```c
/* ex9 — 다섯 가지 fault를 일부러 내고 lab HardFault 핸들러(스택 프레임의 PC/LR 출력)로 해독한다 */
#include "b2.h"
#ifndef CASE
#define CASE 1
#endif
#if CASE == 1
__attribute__((noinline)) static uint32_t bad_read(void) { return REG32(0x50000000u); }
#elif CASE == 2
static void target(void) { P("target ran?\n"); }
#elif CASE == 4
void TIMER0_IRQHandler(void) __attribute__((naked));
void TIMER0_IRQHandler(void) { __asm volatile("ldr lr, =0xFFFFFFF5\n bx lr\n"); }   /* bogus EXC_RETURN */
#elif CASE == 5
__attribute__((noinline)) static int divide(volatile int a, volatile int b) { return a / b; }
#endif

int main(void)
{
    P("CASE %d: ", CASE);
#if CASE == 1
    P("load from unmapped address 0x50000000\n");
    P("value=%u\n", bad_read());
#elif CASE == 2
    P("call a function pointer with bit0 = 0 (ARM state)\n");
    void (*fp)(void) = (void (*)(void))((uint32_t)target & ~1u);
    fp();
#elif CASE == 3
    P("pend IRQ20 whose vector entry is 0\n");
    irq_enable(20, 0x80); irq_pend(20);
#elif CASE == 4
    P("ISR returns with EXC_RETURN=0xFFFFFFF5 (reserved on Armv7-M)\n");
    irq_enable(8, 0x80); irq_pend(8);
#elif CASE == 5
    P("integer divide by zero with CCR.DIV_0_TRP=1\n");
    CCR |= 1u << 4; __asm volatile("dsb\n isb");
    P("q=%d\n", divide(7, 0));
#endif
    return 0;
}
```

```sh
# ex9.sh — CASE마다 빌드 · 실행하고 PC를 addr2line으로
for c in 1 2 3 4 5; do
  O=/var/tmp/rtos/B2/ex9_$c
  make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/B2/ex9.c OUT=$O EXTRA_CFLAGS=-DCASE=$c >/dev/null
  out=$(ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/app.elf 5)
  echo "$out" | grep -v board_exit
  pc=$(echo "$out" | sed -n 's/.*PC=\(0x[0-9a-f]*\).*/\1/p')
  [ -n "$pc" ] && echo "  addr2line $pc -> $(arm-none-eabi-addr2line -f -s -e $O/app.elf $pc | tr '\n' ' ')"
done
```

```text
CASE 1: load from unmapped address 0x50000000

[HardFault] PC=0xc4 LR=0xdd xPSR=0x61000000 CFSR=0x8200 HFSR=0x40000000
  addr2line 0xc4 -> bad_read ex9.c:7 
CASE 2: call a function pointer with bit0 = 0 (ARM state)

[HardFault] PC=0xc0 LR=0xe5 xPSR=0x60000000 CFSR=0x20000 HFSR=0x40000000
  addr2line 0xc0 -> target ex9.c:9 
CASE 3: pend IRQ20 whose vector entry is 0

[HardFault] PC=0x0 LR=0xfffffff9 xPSR=0x20000024 CFSR=0x20000 HFSR=0x40000000
  addr2line 0x0 -> ?? startup.c:30 
CASE 4: ISR returns with EXC_RETURN=0xFFFFFFF5 (reserved on Armv7-M)

[HardFault] PC=0x175 LR=0x175 xPSR=0x0 CFSR=0x40000 HFSR=0x40000000
  addr2line 0x175 -> Default_Handler startup.c:49 
CASE 5: integer divide by zero with CCR.DIV_0_TRP=1

[HardFault] PC=0xc4 LR=0xf9 xPSR=0x21000000 CFSR=0x2000000 HFSR=0x40000000
  addr2line 0xc4 -> divide ex9.c:14 
```

출력에서 볼 것(CFSR 해독은 예제 6):

- **CASE 1** (PRECISERR + BFARVALID): PC = 0xc4 → `bad_read` ex9.c:7, **fault를 낸 바로 그 load**. LR = 0xdd는 `main`에서 `bad_read`를 부른 다음 주소. HFSR FORCED = BusFault가 켜져 있지 않아 HardFault로 승격됐다.
- **CASE 2** (INVSTATE): PC = 0xc0 = `target`의 첫 명령, xPSR = 0x60000000 — **T 비트(bit24)가 0**. bit0 = 0 주소로 `blx`해서 Thumb가 아닌 상태가 됐다.
- **CASE 3** (INVSTATE): PC = 0x0, xPSR = 0x20000024 → IPSR = 0x24 = **36 = IRQ20**, T = 0. 벡터 칸이 0이었다(§1.4). 쌓인 LR = 0xfffffff9 — IRQ20이 받은 EXC_RETURN이 그대로 보인다(Thread에서 IRQ20이 걸렸고, 그 핸들러의 첫 명령에서 fault).
- **CASE 4** (INVPC): CFSR은 정확하지만 **PC = LR = 0x175, xPSR = 0 — 프레임 해독이 틀렸다.** 이유: 예약 EXC_RETURN 0xFFFFFFF5로 복귀를 시도하자 QEMU는 그 값을 그대로 LR에 둔 채 UsageFault로 들어갔다. 0x5의 bit2 = 1이라 lab 핸들러의 `tst lr, #4`가 **PSP**를 골랐고, 아직 PSP = 0이므로 주소 0(벡터 테이블!)을 프레임으로 읽었다 — frame[5] · frame[6] = vec[5] · vec[6] = `Default_Handler`(0x175), frame[7] = vec[7] = 0. **"LR bit2로 스택 고르기"는 EXC_RETURN이 정상일 때만 맞다.** INVPC면 CFSR을 먼저 보고, 프레임은 MSP 쪽에서 찾는다. (실칩의 정확한 동작은 Armv7-M ARM의 ExceptionReturn 의사 코드를 따른다 — 이 노트에서는 QEMU 동작만 확인했다.)
- **CASE 5** (DIVBYZERO): `CCR.DIV_0_TRP`를 켜야 `sdiv`가 0으로 나눌 때 fault가 난다(기본은 결과 0). PC = 0xc4 → `divide` ex9.c:14.

---

## 12. RTOS 커널 안에서는 무슨 일이 일어나나

이 노트의 메커니즘을 FreeRTOS V11.3.2와 Zephyr v4.4.2 소스가 어떻게 쓰는지, 짧게 따라간다. 깊은 설명은 C2(컨텍스트 스위치) · C3(첫 태스크) · E1(ISR 규칙)이 한다.

### 12.1 FreeRTOS — `portable/GCC/ARM_CM3/port.c`

| 함수 | 이 노트의 어느 메커니즘 | 핵심 줄 |
|---|---|---|
| `pxPortInitialiseStack()` | 가짜 프레임(§9) | xPSR = `portINITIAL_XPSR`(0x01000000), PC = `pxCode & portSTART_ADDRESS_MASK`, LR = `prvTaskExitError`, R0 = `pvParameters`, 그 아래 R4–R11 8칸 |
| `prvPortStartFirstTask()` | VTOR[0]으로 MSP 리셋(§2.3), `svc 0` | `ldr r0, =0xE000ED08` … `msr msp, r0` … `svc 0` |
| `vPortSVCHandler()` | 가짜 프레임으로 복귀(§9) | `ldmia r0!, {r4-r11}` → `msr psp, r0` → `orr r14, #0xd` → `bx r14` (0xFFFFFFFD) |
| `xPortPendSVHandler()` | 하드웨어가 8워드를 저장했다는 전제(§3, §6) | `mrs r0, psp` → `stmdb r0!, {r4-r11}` → `vTaskSwitchContext` → `ldmia r0!, {r4-r11}` → `msr psp, r0` → `bx r14` |
| `xPortStartScheduler()` | VTOR로 핸들러 설치 확인(§2.3) | `configASSERT( pxVectorTable[ portVECTOR_INDEX_PENDSV ] == xPortPendSVHandler )` |

`vPortSVCHandler`의 `orr r14, #0xd`가 재미있다. SVC 진입 때 LR은 0xFFFFFFF9(main이 MSP에서 `svc`했으니까)인데, 아래 4비트를 0xD로 만들어 **"Thread · PSP로 돌아가라"**로 바꾼다. 예제 11의 `ldr lr, =0xFFFFFFFD`와 같은 일이다.

PendSV가 R4–R11 **여덟 개만** 저장하는 이유가 이 노트 전체의 결론이다: 나머지 여덟(R0–R3, R12, LR, PC, xPSR)은 PendSV에 들어올 때 **하드웨어가 이미 태스크의 PSP 스택에 쌓았다**. PendSV는 그 바로 아래에 이어 붙이기만 하면 된다.

### 예제 14 — FreeRTOS: 태스크 스택에 쌓인 16워드 (하드웨어 8 + PendSV 8)

무엇을 확인하나: (1) 태스크 A가 아직 한 번도 안 돈 상태에서 A의 TCB가 가리키는 스택 = `pxPortInitialiseStack()`이 만든 가짜 프레임, (2) A 안에서 IRQ가 오면 EXC_RETURN = 0xFFFFFFFD이고 프레임이 A의 스택 배열 안에 있다, (3) A가 모든 레지스터에 표식을 넣고 PendSV를 요청해 B로 전환되면, A의 스택 top에 R4–R11(PendSV가 저장) + R0…xPSR(하드웨어가 저장)이 순서대로 있다. TCB의 첫 멤버가 `pxTopOfStack`이라는 FreeRTOS 규칙(`tasks.c`의 `tskTCB` 주석 "MUST BE THE FIRST MEMBER")을 이용해 핸들에서 바로 읽었다.

```c
/* ex10 — FreeRTOS: 태스크 중 IRQ는 PSP(태스크 스택)에 8워드, PendSV는 그 아래에 R4-R11 8워드를 더 쌓는다 */
#include "FreeRTOS.h"
#include "task.h"
#include "b2.h"
static StackType_t stkA[256], stkB[256];
static StaticTask_t tcbA, tcbB;
static TaskHandle_t hA;
static volatile uint32_t s_frame, s_lr;
void rec(uint32_t *f, uint32_t lr) { s_frame = (uint32_t)f; s_lr = lr; }
void TIMER0_IRQHandler(void) __attribute__((naked));
void TIMER0_IRQHandler(void)
{   /* pick the stack the frame went to, like the lab HardFault handler */
    __asm volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n mov r1, lr\n b rec\n");
}
static void taskA(void *p)
{
    (void)p;
    irq_pend(8);
    P("A: IRQ8 frame=0x%x (stkA 0x%x..0x%x) EXC_RETURN=0x%x\n", s_frame,
      (uint32_t)stkA, (uint32_t)&stkA[256], s_lr);
    __asm volatile(                        /* markers in every register, then request PendSV */
        " ldr r0, =0xA0000000\n ldr r1, =0xA1111111\n ldr r2, =0xA2222222\n ldr r3, =0xA3333333\n"
        " ldr r4, =0x44444444\n ldr r5, =0x55555555\n ldr r6, =0x66666666\n ldr r7, =0x77777777\n"
        " ldr r8, =0x88888888\n ldr r9, =0x99999999\n ldr r10, =0xAAAAAAAA\n ldr r11, =0xBBBBBBBB\n"
        " ldr r12, =0xCCCCCCCC\n ldr lr, =0xEEEEEEEF\n"
        " ldr r0, =0xE000ED04\n mov r1, #0x10000000\n str r1, [r0]\n dsb\n isb\n"   /* ICSR.PENDSVSET */
        ".global b2_A_resume\n b2_A_resume: nop\n"
        ::: "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "lr", "memory");
    for (;;) { }
}
static void taskB(void *p)
{
    (void)p;
    static const char *n[16] = { "R4  ", "R5  ", "R6  ", "R7  ", "R8  ", "R9  ", "R10 ", "R11 ",
                                 "R0  ", "R1  ", "R2  ", "R3  ", "R12 ", "LR  ", "PC  ", "xPSR" };
    static uint32_t first[16];
    extern char b2_A_resume[];
    uint32_t *top = *(uint32_t **)hA;      /* first TCB member = pxTopOfStack */
    for (int i = 0; i < 16; i++) first[i] = top[i];
    P("B: A not started yet, A.pxTopOfStack=0x%x -> taskYIELD()\n", (uint32_t)top);
    taskYIELD();                           /* A runs, then switches back to us via PendSV */
    top = *(uint32_t **)hA;
    P("B: A switched out, A.pxTopOfStack=0x%x (IRQ frame was 0x%x)\n", (uint32_t)top, s_frame);
    P("        before A ran   after PendSV\n");
    for (int i = 0; i < 16; i++) {
        P("  %s  0x%x ", n[i], first[i]);
        for (uint32_t v = first[i] >> 4, d = 1; d < 8; d++, v >>= 4) if (!v) P(" ");
        P("0x%x%s\n", top[i], i == 0 ? "   <- PendSV: stmdb r0!, {r4-r11}" : i == 8 ? "   <- hardware frame" : "");
    }
    P("  taskA=0x%x  b2_A_resume=0x%x\n", (uint32_t)taskA, (uint32_t)b2_A_resume);
    board_exit(0);
}
int main(void)
{
    irq_enable(8, 0x80);
    xTaskCreateStatic(taskB, "B", 256, NULL, 1, stkB, &tcbB);
    hA = xTaskCreateStatic(taskA, "A", 256, NULL, 1, stkA, &tcbA);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile \
  SRCS=/Users/donh/.rtos_scratch/B2/ex10.c OUT=/var/tmp/rtos/B2/ex10 && \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/B2/ex10/app.elf'
```

```text
B: A not started yet, A.pxTopOfStack=0x20000930 -> taskYIELD()
A: IRQ8 frame=0x20000938 (stkA 0x20000574..0x20000974) EXC_RETURN=0xfffffffd
B: A switched out, A.pxTopOfStack=0x20000918 (IRQ frame was 0x20000938)
        before A ran   after PendSV
  R4    0xa5a5a5a5 0x44444444   <- PendSV: stmdb r0!, {r4-r11}
  R5    0xa5a5a5a5 0x55555555
  R6    0xa5a5a5a5 0x66666666
  R7    0xa5a5a5a5 0x77777777
  R8    0xa5a5a5a5 0x88888888
  R9    0xa5a5a5a5 0x99999999
  R10   0xa5a5a5a5 0xaaaaaaaa
  R11   0xa5a5a5a5 0xbbbbbbbb
  R0    0x0        0xe000ed04   <- hardware frame
  R1    0xa5a5a5a5 0x10000000
  R2    0xa5a5a5a5 0xa2222222
  R3    0xa5a5a5a5 0xa3333333
  R12   0xa5a5a5a5 0xcccccccc
  LR    0x1d29     0xeeeeeeef
  PC    0xc0       0x12a
  xPSR  0x1000000  0x61000000
  taskA=0xc1  b2_A_resume=0x132
[board_exit] code=0
```

```text
00000132 T b2_A_resume
00001d28 t prvTaskExitError
000000c0 t taskA
     128:	6001      	str	r1, [r0, #0]
     12a:	f3bf 8f4f 	dsb	sy
     12e:	f3bf 8f6f 	isb	sy

00000132 <b2_A_resume>:
```

출력에서 볼 것:

- **왼쪽 열(A가 돌기 전)**: R4–R11, R1–R3, R12가 0xa5a5a5a5(FreeRTOS가 스택을 채우는 패턴 `tskSTACK_FILL_BYTE`), R0 = 0(`pvParameters` = NULL), **LR = 0x1d29 = `prvTaskExitError`**(0x1d28) + Thumb 비트, **PC = 0xc0 = `taskA`**(bit0 = 0으로 마스크됨), **xPSR = 0x01000000**. 예제 11에서 손으로 만든 프레임과 같은 모양이다.
- A 안의 IRQ8: EXC_RETURN = 0xfffffffd, 프레임 0x20000938이 `stkA`(0x20000574..0x20000974) 안에 있다. **태스크 중 IRQ의 프레임은 그 태스크의 스택에 쌓인다** — 그래서 태스크 스택 크기에 "IRQ 프레임 32 B(FP면 104 B)"를 꼭 넣어야 한다.
- **오른쪽 열(PendSV 뒤)**: 위 8칸에 표식 0x44444444…0xbbbbbbbb = R4–R11이 그대로 — PendSV의 `stmdb r0!, {r4-r11}`이 저장한 것. 아래 8칸 = 하드웨어 프레임: R2 · R3 · R12 · LR 표식이 그대로이고, R0 · R1은 ICSR 주소 · 값으로 쓰여서 0xe000ed04 · 0x10000000. PC = 0x12a = PENDSVSET 쓰기 직후의 `dsb` — A는 다시 스케줄되면 바로 여기서 이어 간다.
- pxTopOfStack 0x20000918과 프레임 0x20000938의 차이 = **32바이트 = R4–R11**. 태스크 하나가 전환되어 나갈 때 스택에 남기는 컨텍스트 = 64바이트(FP를 안 쓸 때). 그림 8의 초록 막대다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="120" y="20" font-size="13" text-anchor="middle" font-weight="bold">태스크 A의 스택 (PSP)</text>
<rect x="10" y="32" width="220" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="120" y="57" font-size="12" text-anchor="middle">A가 쓰던 스택 (높은 주소)</text>
<rect x="10" y="72" width="220" height="44" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="120" y="91" font-size="12" text-anchor="middle">xPSR · PC · LR · R12</text>
<text x="120" y="108" font-size="12" text-anchor="middle">하드웨어가 쌓음</text>
<rect x="10" y="116" width="220" height="44" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="120" y="135" font-size="12" text-anchor="middle">R3 · R2 · R1 · R0</text>
<text x="120" y="152" font-size="12" text-anchor="middle">0x20000938 (예제 14)</text>
<rect x="10" y="160" width="220" height="44" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<text x="120" y="179" font-size="12" text-anchor="middle">R11 · R10 · R9 · R8</text>
<text x="120" y="196" font-size="12" text-anchor="middle">PendSV가 쌓음</text>
<rect x="10" y="204" width="220" height="44" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<text x="120" y="223" font-size="12" text-anchor="middle">R7 · R6 · R5 · R4</text>
<text x="120" y="240" font-size="12" text-anchor="middle">0x20000918</text>
<line x1="120" y1="290" x2="120" y2="252" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/>
<text x="120" y="308" font-size="12" text-anchor="middle">TCB_A.pxTopOfStack</text>
<rect x="260" y="32" width="410" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="272" y="57" font-size="12">1 PendSV 진입: 하드웨어가 PSP에 8워드 (EXC_RETURN FD)</text>
<rect x="260" y="82" width="410" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="272" y="107" font-size="12">2 mrs r0, psp → stmdb r0!, {r4-r11} → TCB에 r0 저장</text>
<rect x="260" y="132" width="410" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="272" y="157" font-size="12">3 vTaskSwitchContext(): pxCurrentTCB = B</text>
<rect x="260" y="182" width="410" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="272" y="207" font-size="12">4 TCB_B에서 SP → ldmia r0!, {r4-r11} → msr psp, r0</text>
<rect x="260" y="232" width="410" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="272" y="257" font-size="12">5 bx r14 (FD): 하드웨어가 B의 8워드 복원 → B 실행</text>
<text x="465" y="300" font-size="12" text-anchor="middle">소프트웨어는 8워드만, 나머지 8워드는 하드웨어</text>
</svg>
```

그림 6 — FreeRTOS ARM_CM3 포트의 컨텍스트 스위치와 태스크 스택 모양. 왼쪽 주소는 예제 14의 실측값. 한 줄씩 읽기는 C2

### 12.2 Zephyr — 벡터는 전부 `_isr_wrapper` 하나

Zephyr는 IRQ마다 벡터를 따로 두지 않는다(기본 `CONFIG_GEN_SW_ISR_TABLE`). 모든 IRQ 칸에 `_isr_wrapper`(`arch/arm/core/cortex_m/isr_wrapper.c`)가 들어 있고, 래퍼가 **IPSR을 읽어 16을 빼고** `_sw_isr_table[n]`에서 (인자, ISR) 쌍을 꺼내 부른 다음, `z_arm_int_exit()`(`exc_exit.c`, 필요하면 PendSV를 pend)로 꼬리 호출한다. `IRQ_CONNECT(irq, prio, isr, arg, flags)`가 빌드 시점에 이 표를 채운다.

```svg
<svg viewBox="0 0 680 170" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="40" width="110" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="65" y="62" font-size="12" text-anchor="middle">벡터 칸 24</text>
<text x="65" y="80" font-size="12" text-anchor="middle">= _isr_wrapper</text>
<rect x="145" y="40" width="120" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="205" y="62" font-size="12" text-anchor="middle">mrs r3, IPSR</text>
<text x="205" y="80" font-size="12" text-anchor="middle">subs r3, #16</text>
<rect x="290" y="40" width="130" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="355" y="62" font-size="12" text-anchor="middle">_sw_isr_table[8]</text>
<text x="355" y="80" font-size="12" text-anchor="middle">{arg, isr}</text>
<rect x="445" y="40" width="100" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="495" y="62" font-size="12" text-anchor="middle">blx r1</text>
<text x="495" y="80" font-size="12" text-anchor="middle">my_isr(arg)</text>
<rect x="560" y="40" width="112" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="616" y="62" font-size="12" text-anchor="middle">b.w</text>
<text x="616" y="80" font-size="12" text-anchor="middle">z_arm_int_exit</text>
<line x1="121" y1="65" x2="143" y2="65" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/>
<line x1="266" y1="65" x2="288" y2="65" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/>
<line x1="421" y1="65" x2="443" y2="65" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/>
<line x1="546" y1="65" x2="558" y2="65" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/>
<text x="340" y="125" font-size="12" text-anchor="middle">하드웨어는 이미 8워드를 쌓았으므로 래퍼도, my_isr도 평범한 C 함수다</text>
<text x="340" y="148" font-size="12" text-anchor="middle">z_arm_int_exit: 더 높은 스레드가 준비됐으면 PendSV pend → tail-chain으로 전환</text>
<text x="340" y="22" font-size="13" text-anchor="middle" font-weight="bold">Zephyr IRQ 8 한 번의 경로 (예제 15의 objdump 그대로)</text>
</svg>
```

그림 7 — Zephyr v4.4.2 Cortex-M의 IRQ 경로. 벡터 테이블은 고정이고, 어떤 ISR을 부를지는 소프트웨어 표가 정한다

### 예제 15 — Zephyr: 벡터 테이블 칸이 전부 같은 주소인지 확인

무엇을 확인하나: `IRQ_CONNECT`로 IRQ8에 ISR을 붙여도 벡터 칸 24에는 `_isr_wrapper`가 있는지, 래퍼가 정말 IPSR로 표를 찾는지.

```text
# zvec/CMakeLists.txt
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(b2_zvec)
target_sources(app PRIVATE src/main.c)
# zvec/prj.conf
CONFIG_PRINTK=y
```

```c
/* zvec — Zephyr: IRQ 벡터는 모두 _isr_wrapper, 실제 ISR은 _sw_isr_table에서 IPSR로 찾는다 */
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/printk.h>
#define REG(a) (*(volatile uint32_t *)(a))
static void my_isr(const void *arg)
{
    uint32_t lr, ipsr;
    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    __asm volatile("mov %0, lr" : "=r"(lr));
    printk("my_isr(arg=%p): IPSR=%u  (called from _isr_wrapper, LR=0x%x)\n", arg, ipsr, lr);
}
int main(void)
{
    extern uint32_t _vector_table[];
    IRQ_CONNECT(8, 2, my_isr, (void *)0x1234, 0);
    irq_enable(8);
    printk("VTOR=0x%x _vector_table=%p\n", REG(0xE000ED08), _vector_table);
    printk("vec[11] SVC=0x%x vec[14] PendSV=0x%x vec[15] SysTick=0x%x\n",
           _vector_table[11], _vector_table[14], _vector_table[15]);
    printk("vec[24] IRQ8=0x%x vec[25] IRQ9=0x%x  _isr_wrapper=%p\n",
           _vector_table[24], _vector_table[25], (void *)_isr_wrapper);
    NVIC_SetPendingIRQ(8);
    __DSB(); __ISB();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/B2/zvec /var/tmp/rtos/B2/zvec && \
  /var/rtoslab/bin/rtos-zrun /var/tmp/rtos/B2/zvec 5'
limactl shell elinux -- bash -c 'cd /var/tmp/rtos/B2/zvec; arm-none-eabi-nm zephyr/zephyr.elf | grep " z_arm_svc\| z_arm_pendsv\| sys_clock_isr\|_isr_wrapper\| _sw_isr_table\|z_arm_int_exit"; \
  arm-none-eabi-objdump -d zephyr/zephyr.elf | sed -n "/<_isr_wrapper>:/,/^$/p"'
```

```text
qemu-system-arm: warning: nic lan9118.0 has no peer
*** Booting Zephyr OS build v4.4.2 ***
VTOR=0x0 _vector_table=0
vec[11] SVC=0x641 vec[14] PendSV=0x78d vec[15] SysTick=0xbd5
vec[24] IRQ8=0x7ed vec[25] IRQ9=0x7ed  _isr_wrapper=0x7ed
my_isr(arg=0x1234): IPSR=24  (called from _isr_wrapper, LR=0x803)
```

```text
000007ec T _isr_wrapper
000028f8 R _sw_isr_table
00000bd4 T sys_clock_isr
00000740 T z_arm_int_exit
0000078c T z_arm_pendsv
00000640 T z_arm_svc
000007ec <_isr_wrapper>:
     7ec:	b508      	push	{r3, lr}
     7ee:	f3ef 8305 	mrs	r3, IPSR
     7f2:	4a06      	ldr	r2, [pc, #24]	@ (80c <_isr_wrapper+0x20>)
     7f4:	3b10      	subs	r3, #16
     7f6:	eb02 01c3 	add.w	r1, r2, r3, lsl #3
     7fa:	f852 0033 	ldr.w	r0, [r2, r3, lsl #3]
     7fe:	6849      	ldr	r1, [r1, #4]
     800:	4788      	blx	r1
     802:	e8bd 4008 	ldmia.w	sp!, {r3, lr}
     806:	f7ff bf9b 	b.w	740 <z_arm_exc_exit>
     80a:	bf00      	nop
     80c:	000028f8 	.word	0x000028f8
```

출력에서 볼 것:

- vec[24] = vec[25] = 0x7ed = `_isr_wrapper`(0x7ec) + Thumb 비트. 연결 안 한 IRQ9도 같은 래퍼다(표의 그 칸에는 "spurious" 핸들러가 들어 있다).
- vec[11] = 0x641 = `z_arm_svc`, vec[14] = 0x78d = `z_arm_pendsv`, vec[15] = 0xbd5 = `sys_clock_isr`(SysTick 드라이버). RTOS가 쓰는 세 예외가 이렇게 묶인다(B4).
- 래퍼 기계어: `mrs r3, IPSR` → `subs r3, #16` → `_sw_isr_table`(0x28f8) + n × 8에서 arg(r0) · isr(r1) 로드 → `blx r1` → `b.w z_arm_exc_exit`(= `z_arm_int_exit`, 같은 주소 0x740). `my_isr`가 받은 LR = 0x803 = 래퍼 안 `blx` 다음 명령 — `my_isr`는 **예외 핸들러가 아니라 래퍼가 부른 평범한 함수**다.
- 경고 0개로 빌드됐다(FLASH 12824 B, RAM 6112 B).
- 비용: 래퍼 몇 명령 + 표 접근 한 번. 이게 싫으면 Zephyr는 `IRQ_DIRECT_CONNECT`/`ISR_DIRECT_DECLARE`(벡터에 직접)와 zero-latency IRQ(E2)를 제공한다.

### 12.3 측정 정리 — 이 노트에서 잰 프레임 크기

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="40" font-size="13">기본 프레임 (ex2)</text>
<rect x="190" y="20" width="96" height="30" fill="#4a7bd0" fill-opacity="0.45" stroke="#4a7bd0"/>
<text x="292" y="40" font-size="13">32 B = 8 words</text>
<text x="10" y="86" font-size="13">정렬 패딩 (ex7)</text>
<rect x="190" y="66" width="108" height="30" fill="#e08a3c" fill-opacity="0.45" stroke="#e08a3c"/>
<text x="304" y="86" font-size="13">36 B = 9 words</text>
<text x="10" y="132" font-size="13">FreeRTOS 전환 (ex10)</text>
<rect x="190" y="112" width="192" height="30" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="388" y="132" font-size="13">64 B = 16 words</text>
<text x="10" y="178" font-size="13">FP 프레임 M4F (ex11)</text>
<rect x="190" y="158" width="312" height="30" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="508" y="178" font-size="13">104 B = 26 words</text>
<line x1="190" y1="12" x2="190" y2="200" stroke="currentColor"/>
<text x="190" y="220" font-size="12">0</text><text x="310" y="220" font-size="12" text-anchor="middle">40</text><text x="430" y="220" font-size="12" text-anchor="middle">80 bytes</text>
</svg>
```

그림 8 — **측정 차트**: 이 노트의 예제에서 직접 잰 "예외 한 번이 스택에 남기는 바이트". 기본 32 B(예제 3), 정렬 패딩 36 B(예제 4), FreeRTOS 태스크가 전환되어 나갈 때 64 B(예제 14), Cortex-M4F FP 프레임 104 B(예제 12). 태스크 스택 · MSP 크기 산정(F3)의 기본 숫자다

---

## 13. 실무에서 다시 보기

### 13.1 드론 비행 제어기 — 8 kHz IMU ISR의 지연 예산

STM32H7급(480 MHz) FC에서 IMU data-ready EXTI가 8 kHz(125 µs 주기)로 온다고 하자. Arm 문서상 진입은 12사이클 = **25 ns**지만, 그건 zero-wait-state 기준이다. 실제로는 (1) 벡터 테이블과 ISR 코드가 플래시에 있으면 wait state가 붙고, (2) 더 높은 우선순위 ISR(예: DShot 타이머 DMA)이 돌고 있으면 그 끝까지 기다리며, (3) 크리티컬 섹션(PRIMASK/BASEPRI)이 길면 그만큼 밀린다. 그래서 FC 펌웨어는 자주 **벡터 테이블과 핫 ISR을 ITCM/RAM으로 옮기고 VTOR를 바꾼다**(예제 2 패턴). 그리고 지연을 DWT CYCCNT로 잰다(M6). 이 노트의 사이클 숫자는 "하한"이지 예산이 아니다.

FPU를 쓰는 태스크(자세 제어)가 있으면 IRQ 프레임이 26워드가 될 수 있다(예제 12). lazy stacking 덕분에 ISR이 FP를 안 쓰면 실제 저장은 안 하지만, **공간은 잡는다** — 태스크 스택 크기에 104 B를 넣는다.

### 13.2 부트로더 → 앱 점프 (bare-metal · MCUboot 공통 패턴)

부트로더가 앱으로 넘어가는 코드는 이 노트의 개념만으로 읽힌다. 아래는 패턴 설명용 코드다(이 노트에서 따로 실행하지 않았다 — 같은 동작을 예제 2(VTOR)와 예제 11(SP · PC 바꿔 넘어가기)에서 실행해 확인했다).

```c
/* 개념 코드 — 앱 이미지가 APP_BASE에 있고, 그 첫 두 워드가 앱의 초기 MSP와 Reset_Handler */
#define APP_BASE 0x08010000u
static void jump_to_app(void)
{
    const uint32_t *app = (const uint32_t *)APP_BASE;
    __asm volatile("cpsid i");
    /* 부트로더가 켠 주변장치 · SysTick · NVIC pending을 끈다 (안 끄면 앱의 벡터로 바로 IRQ가 온다) */
    REG32(0xE000E010u) = 0;                         /* SysTick CTRL */
    for (int i = 0; i < 8; i++) { REG32(0xE000E180u + 4 * i) = ~0u; REG32(0xE000E280u + 4 * i) = ~0u; } /* ICER, ICPR */
    REG32(0xE000ED08u) = APP_BASE;                  /* VTOR = 앱 테이블 */
    __asm volatile("msr msp, %0\n dsb\n isb\n cpsie i\n bx %1" :: "r"(app[0]), "r"(app[1]));
}
```

체크 포인트: VTOR 정렬(§2.2), 앱의 vec[1] bit0 = 1(§1.3), 부트로더가 남긴 pending IRQ(§5.3의 "원인을 안 지우면"), MSP를 앱 값으로(§9.1의 "main 스택의 운명"). Don이 SSD 펌웨어에서 ROM 코드 → 메인 FW 핸드오프를 디버깅해 봤다면 순서가 똑같다는 걸 알 것이다.

### 13.3 모터 컨트롤러 — 같은 우선순위 ISR 둘은 tail-chain으로 이어진다

FOC 컨트롤러에서 PWM 업데이트 ISR과 ADC 변환 완료 ISR을 **같은 우선순위**로 두면 서로 선점하지 않고, 겹치면 tail-chain으로 이어 실행된다(예제 10의 1)). 두 ISR이 공유 변수를 쓸 때 락이 필요 없어진다(같은 레벨은 서로 끼어들지 않으므로). 대신 한쪽이 길어지면 다른 쪽 지연이 그만큼 늘어난다 — 우선순위를 나눌지 묶을지는 지터 예산으로 정한다(E2).

### 13.4 현장 크래시 덤프 — 프레임을 noinit RAM에

양산 장비에서 HardFault가 나면 디버거가 없다. 그래서 HardFault 핸들러가 (예제 13처럼) 프레임 8워드 + CFSR · HFSR · MMFAR · BFAR + EXC_RETURN을 `.noinit` 영역(lab 링커 스크립트에도 있다)에 저장하고 리셋한 뒤, 부팅 후 그걸 로그로 올린다. Memfault 같은 서비스의 코어 덤프도 원리는 같다. 단, CASE 4처럼 EXC_RETURN 자체가 이상하면 프레임 위치 판단을 믿지 말고 **MSP · PSP 둘 다** 저장해 두는 것이 안전하다(M1, M5).

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 켠 IRQ의 벡터 칸이 0이거나 테이블 밖 | IRQ가 오는 순간 HardFault, PC = 0, IPSR = 그 IRQ의 예외 번호 | 벡터 = 0 → bit0 = 0 → INVSTATE (예제 13 CASE 3) | 테이블 크기를 쓰는 IRQ 수에 맞추고, 모든 칸을 기본 핸들러로 채운다 |
| IRQ 번호와 예외 번호 혼동 | 엉뚱한 핸들러가 불리거나 NVIC 설정이 다른 IRQ에 적용 | NVIC는 n, 벡터 · IPSR은 16 + n | `IRQn_Type`(CMSIS)처럼 한쪽 번호만 쓰고, 래퍼는 `IPSR - 16` |
| VTOR 정렬 부족 | 낮은 IRQ는 되는데 높은 IRQ 번호만 엉뚱한 핸들러 | 하위 비트가 무시되어 테이블 시작이 어긋남(§2.2) | 테이블 크기를 2의 거듭제곱으로 올린 값에 `aligned` |
| C 핸들러 안에서 `mrs r0, msp`로 프레임을 찾음 | HardFault 로그의 PC가 엉뚱함 | 컴파일러 push 때문에 SP ≠ 프레임 (예제 1의 48 B 차이) | naked 진입 + `tst lr, #4`로 MSP/PSP 선택 |
| 태스크가 PSP를 쓰는데 MSP만 덤프 | 크래시 PC가 ISR 스택의 쓰레기 값 | 프레임은 PSP에 있다 (예제 5 B) | EXC_RETURN bit2를 보고 고른다 |
| 가짜 프레임의 xPSR에 T 비트를 안 넣음 | 첫 태스크 시작 직후 HardFault(INVSTATE) | 복귀 시 T = 0 | xPSR = 0x01000000 (`portINITIAL_XPSR`) |
| 가짜 프레임 PC에 bit0 = 1 | 실칩에서 동작 불명확(구현에 따라 다름 — 확인 못 함) | 프레임 PC는 bit0 = 0이 규칙 | `& ~1u` (`portSTART_ADDRESS_MASK`) |
| 주변장치 인터럽트 원인을 안 지움 | ISR이 끝없이 반복, Thread가 안 돈다 | 복귀 직후 다시 pending → tail-chain | ISR 안에서 INTCLR · 상태 레지스터를 지운다(가능하면 앞부분에서) |
| 태스크 스택 크기에 IRQ 프레임을 안 넣음 | 가끔(IRQ가 깊은 함수 호출 중에 올 때만) 스택 오버플로 | 프레임 32 B / FP 104 B가 태스크 스택에 쌓임 (예제 14) | 최대 사용량 + 프레임 + 여유로 산정(F3) |
| STKALIGN을 끄거나 정렬 안 된 가짜 프레임 | `double` · `uint64_t` 값이 이상하거나 라이브러리가 오동작 | AAPCS 8바이트 정렬 위반 (예제 4) | STKALIGN = 1 유지, 프레임 주소 8바이트 정렬 |

---

## 15. 면접에서 이렇게 말한다

**Q.** "Walk me through what happens when an interrupt fires on a Cortex-M."

**A.** NVIC가 우선순위를 비교해서 지금보다 높으면 받아들인다. 하드웨어가 R0–R3, R12, LR, PC, xPSR 8워드를 지금 쓰던 스택(MSP나 PSP)에 쌓고, 필요하면 8바이트 정렬 패딩을 넣는다. 동시에 VTOR + 4 × 예외 번호에서 핸들러 주소를 읽는다. LR에는 EXC_RETURN을 넣고 Handler 모드로 바꾼다. M3/M4에서 여기까지 zero-wait-state 기준 12사이클이다. 핸들러가 EXC_RETURN을 PC에 로드하면 8워드를 꺼내 돌아간다.

> The NVIC compares priorities; if the new interrupt is higher than what's running, the core accepts it. Hardware pushes eight words — R0 to R3, R12, LR, PC and xPSR — onto whichever stack was active, MSP or PSP, adding a padding word if needed for 8-byte alignment. In parallel it fetches the handler address from VTOR plus four times the exception number. LR gets an EXC_RETURN value and the core switches to Handler mode. On an M3 or M4 that's 12 cycles with zero-wait-state memory. When the handler loads EXC_RETURN into the PC, hardware pops the frame and resumes.

**꼬리 질문**: "Which stack does the frame go on?" → Thread가 쓰던 스택. RTOS 태스크 중이면 PSP(태스크 스택), ISR 중이면 MSP. 중첩은 전부 MSP.

**Q.** "Why can an ISR on Cortex-M be a plain C function?"

**A.** 하드웨어가 쌓는 8워드가 AAPCS의 caller-saved 레지스터 전부 + PC + xPSR이기 때문이다. 그러니 ISR은 "평범하게 불린 함수"와 구별이 안 되고, R4–R11을 쓰면 컴파일러가 평소처럼 저장한다. 복귀는 LR에 들어 있는 EXC_RETURN으로 하니까 `bx lr`이나 `pop {…, pc}` 같은 일반 에필로그가 그대로 복귀 명령이 된다. objdump로 보면 leaf ISR은 push 0개에 `bx lr`로 끝난다.

> Because the hardware frame is exactly the AAPCS caller-saved set plus PC and xPSR. From the handler's point of view it was just called like any function; if it needs R4 to R11 the compiler saves them as usual. The return goes through EXC_RETURN in LR, so a normal epilogue — bx lr or pop with PC — performs the exception return. I've looked at the disassembly: a leaf ISR has no pushes at all and ends in bx lr.

**꼬리 질문**: "Then why does PendSV save R4–R11 in assembly?" → 컨텍스트 스위치는 **태스크의** callee-saved 값을 TCB 쪽 스택에 보관해야 하는데, C 함수는 그걸 자기 스택에 잠깐 저장할 뿐 태스크를 바꿔 주지 않는다. 그래서 그 8개만 손으로 저장한다(C2).

**Q.** "What is EXC_RETURN and what values can it take?"

**A.** 진입 때 LR에 들어가는 0xFFFFFFxx 꼴의 값으로, PC에 로드되면 예외 복귀로 해석된다. 아래 비트가 Thread냐 Handler냐, MSP냐 PSP냐, FP 프레임이냐를 정한다. FPU 없는 코어는 F1(Handler · MSP), F9(Thread · MSP), FD(Thread · PSP) 세 개. FP 프레임이면 E1 · E9 · ED. 실제로 찍어 보면 bare-metal main 중 IRQ는 F9, FreeRTOS 태스크 중이면 FD, 중첩되면 F1이다. 예약 값으로 복귀하면 INVPC UsageFault다.

> It's a magic value the core puts in LR on entry; loading it into the PC triggers the exception return. Its low bits encode where to return — Thread or Handler mode — which stack to pop from, and whether the frame includes FP state. Without an FPU you see F1 for returning to a handler, F9 for thread on MSP and FD for thread on PSP; with FP context it's E1, E9, ED. I printed them on QEMU: F9 from bare-metal main, FD inside a FreeRTOS task, F1 when nested. A reserved value gives an INVPC usage fault.

**Q.** "Explain tail-chaining and late arrival. What do they save?"

**A.** tail-chaining은 ISR이 끝날 때 더 높은 pending 예외가 있으면 pop과 push를 건너뛰고 바로 다음 핸들러로 가는 것 — M3/M4에서 6사이클. late-arrival은 낮은 예외를 위해 스태킹하는 도중에 높은 예외가 오면 스태킹은 계속하고 벡터만 높은 쪽으로 바꾸는 것. 둘 다 "쌓을 내용이 같으니 다시 할 필요 없다"는 관찰에서 나온다. QEMU에서 사이클은 못 재지만, 두 ISR이 본 스택 프레임의 PC와 R0이 같았다 — 그 사이에 Thread가 명령 하나도 안 돌았다는 증거다.

> Tail-chaining: when a handler finishes and a higher-priority exception than whatever is stacked is pending, the core skips the unstack and re-stack and goes straight to the next vector — six cycles on M3/M4 instead of a full exit plus a 12-cycle entry. Late arrival: if a higher-priority interrupt shows up while the core is still stacking for a lower one, stacking continues because the content is the same, and only the vector fetch switches. On QEMU I can't measure cycles, but I showed both handlers saw the identical stacked PC and R0, so the thread executed zero instructions in between.

**꼬리 질문**: "How does an RTOS benefit?" → PendSV를 최저 우선순위로 두면 ISR이 끝날 때 tail-chain으로 들어와 프레임을 다시 쌓지 않고 컨텍스트 스위치한다.

**Q.** "How does an RTOS start its first task on Cortex-M?"

**A.** 태스크 스택에 "이 태스크가 방금 선점당한 것처럼 보이는" 가짜 프레임을 만든다: PC = 태스크 함수(bit0 = 0), xPSR = 0x01000000, LR = 태스크가 return하면 잡을 함수, R0 = 인자. 그리고 SVC 핸들러에서 PSP를 그 프레임으로 맞추고 EXC_RETURN 0xFFFFFFFD로 복귀한다. 하드웨어는 프레임이 진짜인지 확인하지 않으니 태스크 함수가 Thread 모드 · PSP로 시작된다. FreeRTOS는 `pxPortInitialiseStack`와 `vPortSVCHandler`가 이 일을 하고, LR 자리에 `prvTaskExitError`를 넣는다.

> It forges an exception frame on the new task's stack as if the task had just been preempted: PC is the task entry with bit 0 cleared, xPSR is 0x01000000 for the Thumb bit, LR is a trap function in case the task returns, and R0 is the argument. Then an SVC handler points PSP at that frame and returns with EXC_RETURN 0xFFFFFFFD. The hardware doesn't care who wrote the frame, so the task starts in thread mode on PSP. In FreeRTOS that's pxPortInitialiseStack plus vPortSVCHandler, with prvTaskExitError in the LR slot. I built the same thing by hand on QEMU.

**Q.** "You get a HardFault in the field. How do you find the faulting line?"

**A.** naked 핸들러에서 LR bit2로 MSP/PSP를 골라 프레임 포인터를 얻고, 쌓인 PC · LR · xPSR과 CFSR · HFSR · BFAR · MMFAR를 저장한다. 정밀 fault면 쌓인 PC가 그 명령이니 `addr2line`으로 줄을 얻고, xPSR의 IPSR로 어느 예외 문맥이었는지, T 비트로 Thumb 상태 문제인지 본다. 단 EXC_RETURN 자체가 깨진 INVPC 같은 경우에는 LR bit2를 믿을 수 없으니 CFSR부터 보고 양쪽 스택을 다 저장한다 — 실제로 QEMU에서 lab 핸들러가 그 경우 벡터 테이블을 프레임으로 읽는 걸 봤다.

> A naked handler picks MSP or PSP from bit 2 of LR, then saves the stacked PC, LR and xPSR plus CFSR, HFSR and the fault address registers to no-init RAM. For a precise fault the stacked PC is the faulting instruction, so addr2line gives the line; IPSR in the stacked xPSR tells me which exception context it was, and a cleared T bit means a bad function pointer. One caveat: with INVPC the EXC_RETURN itself is bogus, so bit 2 can send you to the wrong stack — I saw a handler decode the vector table as a frame on QEMU — so I check CFSR first and dump both stacks.

**Q.** "Why would you relocate the vector table, and what can go wrong?"

**A.** 부트로더 뒤에 앱이 있을 때, 런타임에 핸들러를 바꿀 때, 플래시 wait state 없이 벡터를 읽고 싶을 때 RAM이나 TCM으로 옮긴다. 틀리기 쉬운 것은 정렬(테이블 크기를 2의 거듭제곱으로 올린 값), bit0 = 1, 바꾸는 순간의 IRQ(마스크 → 쓰기 → DSB/ISB), 부트로더가 남긴 pending IRQ. FreeRTOS V11은 스케줄러 시작 때 VTOR 테이블에서 SVC · PendSV 칸을 assert로 확인하니, VTOR를 안 바꾸면 거기서 멈춘다.

> To hand off from a bootloader, to patch handlers at runtime, or to fetch vectors from zero-wait RAM or TCM. The traps are alignment — the table size rounded up to a power of two, minimum 128 bytes — the Thumb bit in each entry, changing it with interrupts masked followed by DSB and ISB, and stale pending interrupts left by the bootloader. FreeRTOS V11 even asserts at scheduler start that the SVC and PendSV entries in the VTOR table are its handlers, so a wrong VTOR stops you right there.

---

## 16. 직접 해보기

1. **개념 — 번호 맞히기**: IRQ 37을 쓰는 칩에서 (a) 벡터 칸의 오프셋, (b) 그 ISR 안의 IPSR 값, (c) VTOR 정렬 최소값을 구하라. 테이블이 IRQ 0–37까지만 있다고 하자.
   **정답/힌트**: (a) (16 + 37) × 4 = 0xD4, (b) 53, (c) 테이블 54워드 → 64워드 = 256바이트 정렬.
2. **코드 — 프레임 덤프 확장**: 예제 3의 `dump()`를 고쳐서 쌓인 xPSR을 N · Z · C · V · T · bit9 · IPSR로 나눠 출력하고, 어셈블리 블록 앞에 `cmp r0, r0`(Z = 1)를 넣어 xPSR이 어떻게 바뀌는지 보라.
   **정답/힌트**: Z 플래그(bit30)가 1이 된다. "ISR이 플래그를 망가뜨려도 원래 코드의 조건 분기가 안전한 이유"를 출력으로 확인하는 과제다.
3. **코드 — 가짜 프레임으로 두 태스크 번갈아 돌리기**: 예제 11을 확장해 스택 두 개, 가짜 프레임 두 개를 만들고, SVC 핸들러가 호출될 때마다 R4–R11을 저장 · 복원하며 PSP를 바꿔치기하라(미니 컨텍스트 스위치). 태스크는 `svc 0`으로 양보한다.
   **정답/힌트**: SVC 핸들러는 naked로: `mrs r0, psp; stmdb r0!, {r4-r11}; (C로 다음 SP 고르기); ldmia r0!, {r4-r11}; msr psp, r0; ldr lr, =0xFFFFFFFD; bx lr`. 가짜 프레임은 16워드(R4–R11 자리 포함)로 만든다. 이게 S2(미니 RTOS)의 핵심이고 C2와 똑같은 구조다.
4. **측정 — 중첩 깊이와 MSP**: 예제 8에 IRQ 하나를 더(우선순위 0x20) 추가해 4단 중첩을 만들고, 단계마다 MSP를 기록해 "레벨당 몇 바이트"인지 표로 만들라. 핸들러 하나를 `-O0`으로 바꾸면 어떻게 변하나?
   **정답/힌트**: 레벨당 = 프레임 32 B + 그 ISR C 코드의 push(최적화에 따라 다름). 예제 8에서는 IRQ8 → IRQ10이 56 B였다. `-O0`은 프레임 포인터와 지역 변수 때문에 크게 늘어난다 — MSP 산정은 최적화 옵션별로 다시 해야 한다.
5. **측정 — FP 태스크의 비용**: 예제 14를 `mps2-an386`(M4F)과 FreeRTOS `ARM_CM4F` 포트로 옮겨, FP를 쓰는 태스크가 전환되어 나갈 때 스택에 몇 바이트를 남기는지 재라.
   **정답/힌트**: 템플릿이 M3 전용이라 Makefile을 자기 폴더에 복사해 `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`, `portable/GCC/ARM_CM4F/port.c`로 바꾼다. 하드웨어 26워드 + PendSV가 S16–S31(16워드)과 R4–R11 · EXC_RETURN을 더한다. 숫자는 B6에서 확인.
6. **디버깅 — CASE 4 고치기**: lab HardFault 핸들러처럼 `tst lr, #4`로 스택을 고르되, LR의 아래 4비트가 0x1 · 0x9 · 0xD가 아니면 MSP를 쓰고 "bad EXC_RETURN"을 출력하는 naked 핸들러를 자기 startup에 써서 예제 13 CASE 4를 다시 돌려라.
   **정답/힌트**: lab `startup.c`의 `HardFault_Handler`는 weak가 아니라서 바꿀 수 없다 — `templates/common`을 자기 폴더로 복사해서 고친다. MSP 쪽 프레임에는 IRQ8이 걸린 main의 PC가 보여야 한다.
7. **조사 — late-arrival 실측 계획**: QEMU로는 못 하는 late-arrival을 실제 STM32 보드에서 보이려면 무엇을 어떻게 잴지 계획을 써라(GPIO, 두 인터럽트 원천의 시간차를 어떻게 1~10사이클 단위로 조절할지, DWT CYCCNT를 어디서 읽을지).
   **정답/힌트**: 타이머 두 개를 같은 클럭에서 비교값만 1사이클씩 다르게 → 높은 쪽 ISR 첫 명령에서 CYCCNT, 낮은 쪽도. 시간차가 스태킹 창(약 12사이클) 안이면 높은 쪽 진입 지연이 "12 − 시간차"가 아니라 거의 일정하게 나온다. M6에서 측정 도구.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| exception (예외) | 정상 흐름을 멈추고 핸들러로 가는 모든 사건 | Reset · NMI · fault · SVC · PendSV · SysTick · IRQ 모두 |
| exception number (예외 번호) | 예외마다 붙은 번호 | 1 Reset … 15 SysTick, IRQ n = 16 + n. IPSR에 들어감 |
| IPSR | Interrupt Program Status Register | 지금 실행 중인 예외 번호. Thread면 0 |
| vector table (벡터 테이블) | 핸들러 주소의 워드 배열 | 0번 칸 = 초기 MSP, 주소 = VTOR + 4 × 번호 |
| VTOR | Vector Table Offset Register (0xE000ED08) | 벡터 테이블 위치. 크기의 2의 거듭제곱 정렬 |
| stacking (스태킹) | 진입 시 하드웨어가 레지스터를 스택에 저장 | 8워드(R0–R3, R12, LR, PC, xPSR), FP면 26워드 |
| exception frame (예외 프레임) | 스태킹으로 생긴 8(26)워드 묶음 | Zephyr `struct __basic_sf` |
| EXC_RETURN | 진입 시 LR에 들어가는 0xFFFFFFxx 값 | PC에 로드되면 예외 복귀. F1/F9/FD, FP면 E1/E9/ED |
| STKALIGN | CCR bit 9 | 진입 시 SP를 8바이트로 맞춤. 패딩했으면 쌓인 xPSR bit 9 = 1 |
| preemption (선점) | 높은 우선순위가 실행 중인 ISR에 끼어듦 | 중첩. 새 ISR의 LR = …F1 |
| nesting (중첩) | ISR 안에 ISR이 들어간 상태 | 깊이 ≤ 선점 우선순위 레벨 수. 전부 MSP에 쌓임 |
| tail-chaining | ISR 끝 → pop · push 없이 다음 ISR | M3/M4 6사이클(Arm 문서). PendSV가 이걸로 들어온다 |
| late-arrival | 스태킹 도중 도착한 높은 예외가 먼저 | 스태킹은 계속, 벡터만 교체 |
| pop preemption | 복귀 pop 도중 새 예외 → pop 중단 | 바로 tail-chain |
| AAPCS | Arm 프로시저 호출 표준 | caller-saved R0–R3 · R12 · LR = 하드웨어 프레임과 일치 |
| caller-saved / callee-saved | 호출자가 / 피호출자가 보존 책임 | 하드웨어는 caller-saved를, PendSV는 callee-saved를 저장 |
| fake frame (가짜 프레임) | 소프트웨어가 만든 예외 프레임 | 첫 태스크 시작 · 스레드 생성 (`pxPortInitialiseStack`, `arch_new_thread`) |
| FPCA · lazy stacking | CONTROL bit 2 · FPCCR.LSPEN | FP 쓴 컨텍스트 표시 · FP 레지스터 저장을 실제 사용 때까지 미룸(B6) |
| INVSTATE · INVPC | UsageFault 원인 비트 | T 비트 0으로 실행 · 잘못된 EXC_RETURN |
| `_isr_wrapper` | Zephyr 공통 IRQ 진입 함수 | IPSR − 16으로 `_sw_isr_table`을 찾아 ISR 호출 |

---

## 18. 요약 & 체크리스트

Cortex-M 예외는 "하드웨어가 대신 해 주는 함수 호출"이다. 번호(1 Reset … 15 SysTick, IRQ n = 16 + n)로 벡터 테이블(VTOR + 4 × 번호, 0번 칸은 초기 MSP, 주소 bit0 = 1)에서 핸들러를 찾고, 진입할 때 R0–R3 · R12 · LR · PC · xPSR 8워드를 쓰던 스택에 쌓는다(SP가 4 mod 8이면 4바이트 패딩, xPSR bit 9로 표시). LR에는 EXC_RETURN(F1 Handler · MSP, F9 Thread · MSP, FD Thread · PSP, FP면 E1/E9/ED)이 들어가고, 그 값이 PC에 로드되면 복귀다. 8워드가 AAPCS caller-saved와 같으니 ISR은 평범한 C 함수다(objdump: push 0개, `bx lr`). 더 높은 우선순위는 ISR을 선점하고(LR = F1), 같거나 낮은 것은 기다렸다가 tail-chain으로 이어진다 — 실칩에서 진입 12 · tail-chain 6사이클(M3/M4, Arm 문서; QEMU는 재현 안 함). 프레임은 누가 썼는지 확인되지 않으므로 가짜 프레임으로 원하는 PC에 "복귀"할 수 있고, RTOS는 이걸로 첫 태스크를 띄우며(C3), PendSV는 하드웨어가 저장하지 않은 R4–R11만 저장해서 태스크를 바꾼다(C2). fault도 같은 프레임을 남기므로 쌓인 PC로 원인 줄을 찾는다(M1).

- [ ] 예외 번호표(1–15, 16 + n)와 IPSR · ICSR.VECTACTIVE의 관계를 말할 수 있다
- [ ] 벡터 테이블의 0번 칸과 bit0 = 1의 의미를 설명하고 `nm`으로 확인할 수 있다
- [ ] VTOR를 RAM 테이블로 옮기는 순서와 정렬 규칙(크기의 2의 거듭제곱, 최소 128 B)을 안다
- [ ] 하드웨어 프레임 8워드의 순서와 쌓인 PC의 의미(다음 명령 / fault 명령)를 안다
- [ ] STKALIGN 패딩과 쌓인 xPSR bit 9를 예제로 보였다
- [ ] EXC_RETURN 여섯 값을 비트로 해독하고 각각 언제 나오는지 말할 수 있다
- [ ] "ISR이 C 함수인 이유"를 AAPCS와 objdump로 설명할 수 있다
- [ ] 선점 · 중첩 · tail-chaining · late-arrival · pop preemption을 구분하고 Arm 문서 사이클 값을 출처와 함께 말할 수 있다
- [ ] QEMU가 스태킹 사이클을 모델링하지 않는다는 한계를 알고, 실칩에서 잴 방법을 안다
- [ ] 가짜 프레임으로 Thread · PSP에서 함수를 시작시킬 수 있다 (PC bit0 = 0, xPSR = 0x01000000, 8바이트 정렬)
- [ ] FreeRTOS `pxPortInitialiseStack` · `vPortSVCHandler` · `xPortPendSVHandler`와 Zephyr `_isr_wrapper`가 이 메커니즘을 어디서 쓰는지 안다
- [ ] HardFault 프레임에서 PC · xPSR(IPSR, T 비트)로 원인을 읽고, INVPC에서 LR bit2를 믿으면 안 되는 이유를 안다

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM C4 | 예외 진입 · 복귀 (벡터 테이블, 자동 스태킹, EXC_RETURN, tail-chaining, late-arrival, VTOR) | §1–§9 전체 (주인) | 이 노트 링크 + 벡터 테이블을 어셈블리/링커 스크립트로 직접 배치하는 법, 부트로더 → 앱 점프를 실제 보드에서 해 보기(§13.2 개념 코드의 실행판) |
| BM C3 | AAPCS 호출 규약 | §6 (caller-saved = 하드웨어 프레임 부분만) | 인자 · 반환값 · 구조체 전달 · 가변 인자 등 AAPCS 전체 |
| BM C8 | Fault 예외 | §11 (프레임 읽는 법만) | fault 종류 · escalation · lockup · CFSR 비트 전체 (RTOS 쪽 주인은 M1) |
| BM C10 | FPU · DSP · Helium | §10 예제 12 (EXC_RETURN E9 · 26워드만) | FPU 켜는 순서 · DSP 명령 (lazy stacking 깊이는 RTOS B6) |

---

## 참고 자료

- Arm, **Cortex-M3 Technical Reference Manual r2p0 (ARM DDI 0337G)** — §5.5 Pre-emption(Table 5-4 진입 단계, 그림 5-1 스택 내용, "ISR functions can be normal C/C++ functions"), §5.5 그림 5-2 "12-cycle latency", §5.6 Tail-chaining("six cycles"), §5.7 Late-arriving, §8 Vector Table Offset Register(정렬 규칙), Configuration Control Register(STKALIGN, 리셋 값 1). 사본: https://www.keil.com/dd/docs/datashts/arm/cortex_m3/r2p0/ddi0337g_cortex_m3_r2p0_trm.pdf (2026-10-25 확인)
- Joseph Yiu, "A Beginner's Guide on Interrupt Latency — and Interrupt Latency of the Arm Cortex-M processors", Arm Community blog, 2016-04-01 (M0 16 · M0+ 15 · M3 12 · M4 12사이클, tail-chain 6사이클, zero wait state 조건; 2026-10-25 확인): https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/beginner-guide-on-interrupt-latency-and-interrupt-latency-of-the-arm-cortex-m-processors
- Arm, **Armv7-M Architecture Reference Manual** (ARM DDI 0403) — 예외 모델, EXC_RETURN, ExceptionReturn 의사 코드
- Arm, **Armv8-M Architecture Reference Manual** (ARM DDI 0553) — Armv8-M EXC_RETURN(S · DCRS · ES 비트)
- Joseph Yiu, "The Definitive Guide to Arm Cortex-M3 and Cortex-M4 Processors" — 예외 처리 · 스택 프레임 · 인터럽트 지연 장
- FreeRTOS Kernel V11.3.2 소스(`/var/rtoslab/src/FreeRTOS-Kernel`): `portable/GCC/ARM_CM3/port.c`(`pxPortInitialiseStack`, `prvTaskExitError`, `vPortSVCHandler`, `prvPortStartFirstTask`, `xPortStartScheduler`의 `configCHECK_HANDLER_INSTALLATION`, `xPortPendSVHandler`), `include/FreeRTOS.h`(`configCHECK_HANDLER_INSTALLATION` 기본값 1) — https://github.com/FreeRTOS/FreeRTOS-Kernel
- Zephyr v4.4.2 소스(`/var/rtoslab/zephyrproject/zephyr`): `arch/arm/core/cortex_m/isr_wrapper.c`(`_isr_wrapper`), `exc_exit.c`(`z_arm_int_exit`), `swap_helper.S`(`z_arm_pendsv`), `thread.c`(`arch_new_thread`의 `iframe`, `DEFAULT_EXC_RETURN`), `prep_c.c`(`relocate_vector_table`), `arch/arm/include/cortex_m/exception.h`(EXC_RETURN 비트), `include/zephyr/arch/arm/cortex_m/exception.h`(`struct arch_esf`), `arch/Kconfig`(`SRAM_VECTOR_TABLE`)
- QEMU 문서 — Arm MPS2 and MPS3 boards: https://www.qemu.org/docs/master/system/arm/mps2.html
- Don의 Embedded Linux 노트: EL F6(Linux 인터럽트 처리 — GIC · irqchip · threaded IRQ; Linux 쪽 흐름은 거기서)
- 같은 RTOS 노트 세트: B1(프로그래머 모델), B3(NVIC 우선순위), B4(SysTick · PendSV · SVC), B6(FPU 프레임 · lazy stacking), B7(Cortex-M 세대), B9(Cortex-R), B10(RISC-V), C2(컨텍스트 스위치), C3(첫 태스크 시작), E1 · E2(ISR 규칙 · 우선순위 설계), F3(스택 크기), M1(HardFault 해독), M6(측정 도구), S2(미니 RTOS)
