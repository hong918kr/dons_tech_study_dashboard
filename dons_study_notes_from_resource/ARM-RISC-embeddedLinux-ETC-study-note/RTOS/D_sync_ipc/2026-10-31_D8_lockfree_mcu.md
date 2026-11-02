# D8. 락 없는 통신 (MCU) — LDREX/STREX, C11 atomics, M0의 한계, ISR ↔ 태스크 SPSC 링버퍼, DMB/DSB/ISB가 정말 필요한 곳

> **이 노트를 다 읽으면**: 단일 코어 Cortex-M에서 ISR ↔ 태스크 레이스(RMW · 찢어진 읽기 · 컴파일러 재배치)를 "어떤 도구로 막나"로 분류할 수 있다 · LDREX/STREX 재시도 루프와 exclusive monitor(로컬 · 글로벌), 예외 진입/복귀가 모니터를 지운다는 규칙을 QEMU 실측(`strex_fail` 8888 = 8888)으로 설명할 수 있다 · C11 `<stdatomic.h>`가 M3/M33/M0에서 각각 무엇으로 컴파일되는지(`ldrex`/`ldaex`/`__atomic_*` 라이브러리 호출) 보이고 M0용 대체 구현을 직접 써서 `microbit`(M0) QEMU에서 돌릴 수 있다 · 2의 거듭제곱 · free-running 인덱스 SPSC 링버퍼를 화이트보드에 쓰고, 어디에 컴파일러 배리어가, 어디에 DMB가 필요한지 arm64 실측(틀린 값 13만~18만 개 vs 0개)으로 말할 수 있다 · DMB/DSB/ISB가 Cortex-M에서 실제로 필요한 7가지 장면을 소스(FreeRTOS · Zephyr · CMSIS)로 짚을 수 있다
> **왜 필요한가**: 드론 · 모터 컨트롤러의 고속 ISR 경로(IMU 8 kHz, FOC 20 kHz, UART 1 Mbaud)는 커널 객체를 부를 시간이 없다. 면접에서는 "ISR과 태스크 사이에 락 없는 링버퍼를 짜 보라", "`volatile`이면 충분한가?", "LDREX/STREX는 어떻게 동작하나, M0에서는?", "DMB와 DSB 차이, 언제 필요한가?"가 화이트보드 단골이다
> **Don 기준 난이도**: 링버퍼 · DMA 디스크립터 · doorbell · 멀티코어 메시지 링(SSD 펌웨어)은 이미 안다. 일반 메모리 모델 · acquire/release · MP litmus는 EL F5에서 공부했다 / 새로 배울 부분은 **Cortex-M에서 달라지는 점** — 단일 코어에서는 무엇이 필요 없고 무엇이 여전히 필요한지, exclusive monitor가 예외와 어떻게 얽히는지, Armv6-M(M0)에서 C11 atomics가 링크조차 안 되는 이유, 그리고 FreeRTOS · Zephyr 프리미티브 중 무엇이 정말 락 없이 도는지
> **선행 노트**: C7(크리티컬 섹션 — 레이스 재현과 PRIMASK · BASEPRI 구현), B3(NVIC · BASEPRI), B7(Cortex-M 세대 — Armv6-M에 LDREX가 없다), C10(SMP 스핀락), EL F4(락 · seqlock), EL F5(메모리 배리어 · litmus). 이어지는 노트: D5(stream/message buffer — SPSC 규칙), E1 · E2(FromISR · 우선순위 배치), E6(DMA 완료 통지), P4(비대칭 멀티코어 메일박스)
> 🔁 **Bare-Metal 겹침**: BM J2(레이스 일부 — 찢어진 읽기 · RMW의 락 없는 해법), J3(락 없는 링버퍼), J4(원자 연산), J5(배리어가 필요한 순간), B6(순서 일부 — 쓰기 버퍼 · write posting) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 "RTOS 없는 슈퍼 루프에서 같은 링버퍼 · 배리어를 쓰는 최소 코드와 Normal/Device 메모리 타입 설명"만 더한다

---

## 0. 큰 그림 — 커널 객체를 부를 시간이 없을 때

예를 들어 드론 비행 제어기라면 UART RX ISR이 GNSS 바이트를 1 Mbaud로 받고, IMU DMA 완료 ISR이 8 kHz로 자이로 샘플을 넘기고, 모터 ISR이 20 kHz로 setpoint를 읽는다. 이 경로에서 FreeRTOS 큐(이 노트 §6.3에서 바이트 하나 넣고 빼기에 7 µs)나 뮤텍스(12.8 µs)를 부르면 ISR 예산이 그대로 사라진다. 그렇다고 매번 인터럽트를 끄면(C7) 다른 ISR의 지연에 그만큼이 더해진다. 그래서 "끼어들어도 깨지지 않게 **설계**한 자료 구조"를 쓴다. 이것이 **락 없는 통신(lock-free communication)**이다 — 누구도 누구를 기다리게 만들지 않고, 한쪽이 언제 끼어들어도 상대가 일관된 값을 보는 통신.

Don에게 익숙한 말로 바꾸면: SSD 컨트롤러에서 코어 A가 명령 디스크립터를 공유 SRAM 링에 쓰고 tail 레지스터(doorbell)를 올리면 코어 B나 하드웨어 엔진이 가져간다. 그 링에는 락이 없다. **"생산자는 tail만, 소비자는 head만 쓴다"는 소유권 규칙**과 **"데이터를 먼저 쓰고 인덱스를 나중에 쓴다"는 순서 규칙** 두 개로 안전하다. 이 노트는 그 두 규칙을 단일 코어 Cortex-M의 ISR ↔ 태스크에 옮기고, 순서 규칙을 지키는 데 **실제로 무엇이 필요한지**(컴파일러 배리어? DMB? 아무것도?)를 측정으로 가른다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">ISR ↔ 태스크 사이에 무엇을 넘기나 → 어떤 도구를 쓰나</text>
<text x="115" y="44" font-size="12" text-anchor="middle">넘기는 것</text>
<text x="370" y="44" font-size="12" text-anchor="middle">도구 (이 노트의 절)</text>
<text x="595" y="44" font-size="12" text-anchor="middle">주의</text>
<rect x="10" y="56" width="210" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="115" y="80.2" font-size="12" text-anchor="middle">32비트 값 · 플래그 하나</text>
<rect x="260" y="56" width="220" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="370" y="80.2" font-size="12" text-anchor="middle">volatile + 정렬된 LDR/STR</text>
<rect x="520" y="56" width="150" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="595" y="80.2" font-size="12" text-anchor="middle">원자성 = 워드 단위</text>
<line x1="222" y1="76" x2="257" y2="76" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="482" y1="76" x2="517" y2="76" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="10" y="108" width="210" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="132.2" font-size="12" text-anchor="middle">카운터 · 비트 (RMW)</text>
<rect x="260" y="108" width="220" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="370" y="132.2" font-size="12" text-anchor="middle">LDREX/STREX · C11 (§2, §3)</text>
<rect x="520" y="108" width="150" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="595" y="132.2" font-size="12" text-anchor="middle">M0: IRQ 마스크로</text>
<line x1="222" y1="128" x2="257" y2="128" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="482" y1="128" x2="517" y2="128" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="10" y="160" width="210" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="115" y="184.2" font-size="12" text-anchor="middle">여러 워드 최신값 (setpoint)</text>
<rect x="260" y="160" width="220" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="370" y="184.2" font-size="12" text-anchor="middle">seqlock · 더블 버퍼 (§4)</text>
<rect x="520" y="160" width="150" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="595" y="184.2" font-size="12" text-anchor="middle">읽는 쪽이 재시도</text>
<line x1="222" y1="180" x2="257" y2="180" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="482" y1="180" x2="517" y2="180" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="10" y="212" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="115" y="236.2" font-size="12" text-anchor="middle">바이트 · 샘플 스트림 (FIFO)</text>
<rect x="260" y="212" width="220" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="370" y="236.2" font-size="12" text-anchor="middle">SPSC 링버퍼 (§5, §6)</text>
<rect x="520" y="212" width="150" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="595" y="236.2" font-size="12" text-anchor="middle">생산자 1 · 소비자 1</text>
<line x1="222" y1="232" x2="257" y2="232" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="482" y1="232" x2="517" y2="232" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="10" y="264" width="210" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="115" y="288.2" font-size="12" text-anchor="middle">DMA · 주변장치 · 다른 코어</text>
<rect x="260" y="264" width="220" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="370" y="288.2" font-size="12" text-anchor="middle">DMB / DSB / ISB (§7)</text>
<rect x="520" y="264" width="150" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="595" y="288.2" font-size="12" text-anchor="middle">순서를 밖에 보이게</text>
<line x1="222" y1="284" x2="257" y2="284" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="482" y1="284" x2="517" y2="284" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="10" y="322" font-size="12">위에서 아래로 갈수록 "한 명령으로 끝나지 않는 것"이 커진다. 크리티컬 섹션(C7)은 언제나 되는 대안이다.</text>
</svg>
```

그림 1 — 넘기는 데이터의 모양에 따라 도구가 달라진다. 초록은 하드웨어가 공짜로 주는 것, 파랑은 하드웨어 명령 하나 또는 재시도, 주황은 자료 구조 설계, 빨강은 순서를 코어 밖에 보이게 하는 명령이다.

### Bare-Metal · Linux · 다른 RTOS 노트와의 경계

- **레이스 자체의 재현과 크리티컬 섹션 구현**(PRIMASK 저장/복원, BASEPRI, 중첩 카운터, `vTaskSuspendAll`)은 **C7**이 주인이다. C7 §1이 이미 QEMU에서 잃어버린 갱신(lost 5491/9153)과 찢어진 64비트 타임스탬프(인터럽트를 꺼도 20번 거꾸로 감)를 셌다. 이 노트는 그 레이스를 **락 없이** 고치는 방법(LDREX/STREX, seqlock, SPSC)만 쓴다.
- **일반 메모리 모델**(compiler vs CPU 재배치, acquire/release의 정의, MP · SB · LB litmus, LKMM, `smp_mb`/`READ_ONCE`)은 **EL F5**, 스핀락 · seqlock의 Linux 구현은 **EL F4**(§12 seqlock)가 주인이다. 여기서는 정의를 반복하지 않고 **Cortex-M에서 달라지는 점**만 쓴다.
- **SMP RTOS의 스핀락**(FreeRTOS SMP `portGET_TASK_LOCK`, Zephyr `k_spin_lock`이 aarch64에서 `msr daifset` + CAS가 되는 것)은 **C10**이 주인이다.
- **메모리 타입**(Normal / Device / Strongly-ordered 속성 자체)은 Bare-Metal BM B6에 남긴다. 이 노트는 그중 "쓰기 버퍼 · write posting 때문에 레지스터 쓰기가 늦게 반영되는 현상"과 그래서 배리어가 필요한 곳만 쓴다(§7).
- **FreeRTOS stream/message buffer API**는 D5가 주인이다. 여기서는 "쓰는 쪽 하나 · 읽는 쪽 하나" 규칙이 왜 락 없는 설계에서 나오는지만 본다(§8).

### 실습 환경 한 줄

QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) 위에서 bare-metal과 FreeRTOS V11.3.2(ARM_CM3 포트), Zephyr v4.4.2를 돌리고, Armv6-M은 QEMU `microbit`(nRF51, Cortex-M0)에서 bare-metal로 돌린다. 진짜 메모리 재배치가 필요한 실험은 **VM 호스트 arm64**(Lima VM `elinux`, Apple M2 위 vCPU 6개, gcc 15, 실제 arm64 코어)에서 pthread 두 개로 한다. 타이밍은 모두 `ICOUNT=1`(명령 1개 = 32 ns, 결정적)이고 **QEMU icount 기준이라 실제 칩과 절대값은 다르다**(캐시 · 플래시 wait state · 버스 · DMB 비용을 모델링하지 않음). 시계는 FPGAIO COUNTER(0x40028018, 25 MHz, 1 카운트 = 40 ns). 이 세션에서는 VM 루트 파일 시스템이 읽기 전용으로 바뀌어 있어서(`emergency_ro`) 빌드 산출물을 `/var/tmp/rtos/D8` 대신 VM의 tmpfs `/tmp/rtosD8`에 두었다 — 명령의 `O=/tmp/rtosD8`이 그 때문이다. Zephyr 빌드도 같은 이유로 캐시를 `-DUSER_CACHE_DIR=/tmp/rtosD8/zcache`로 옮겼다. 모든 빌드는 `-Wall -Wextra` 경고 0개다.

모든 bare-metal · FreeRTOS 예제가 쓰는 작은 헤더 `d8.h`다(C7의 `c7.h`에서 필요한 것만 남겼다).

```c
/* d8.h — D8 helpers for QEMU mps2-an385 (Cortex-M3, bare-metal + FreeRTOS) */
#ifndef D8_H
#define D8_H
#include "board.h"
#define CYC()  REG32(0x40028018u)              /* FPGAIO counter, 25 MHz: 1 count = 40 ns */
static inline void timer0_start(uint32_t period, uint8_t prio)
{
    TIMER_CTRL(TIMER0_BASE) = 0;
    TIMER_RELOAD(TIMER0_BASE) = period - 1;
    TIMER_VALUE(TIMER0_BASE) = period - 1;
    TIMER_INTCLR(TIMER0_BASE) = 1;
    NVIC_IPR(8) = prio;
    NVIC_ISER0 = 1u << 8;
    TIMER_CTRL(TIMER0_BASE) = 1u | 8u;          /* enable + IRQ enable */
}
static inline void timer0_stop(void) { TIMER_CTRL(TIMER0_BASE) = 0; }
static inline uint32_t irq_save(void)
{
    uint32_t r;
    __asm volatile("mrs %0, primask\n cpsid i" : "=r"(r) : : "memory");
    return r;
}
static inline void irq_restore(uint32_t r) { __asm volatile("msr primask, %0" : : "r"(r) : "memory"); }
#define CBAR() __asm volatile("" ::: "memory")  /* compiler barrier: 0 instructions */
#endif
```

`CBAR()`는 이 노트의 주인공 중 하나다. 명령을 하나도 만들지 않고 컴파일러에게 "이 줄을 넘어 메모리 접근을 옮기지 마라"고만 말한다(EL F5 §1.4의 `barrier()`와 같다).

---

## 1. 문제 다시 보기 — 단일 코어에서 ISR ↔ 태스크가 깨지는 세 가지 방식

> 🔁 이 절은 Bare-Metal BM J2(메인 ↔ ISR 데이터 레이스 — 찢어진 읽기 · RMW)와 겹친다 (주인: 이 노트 — 락 없는 해법 부분. 레이스 재현과 크리티컬 섹션 해법은 C7 §1 · §2).

C7 §1에서 본 레이스를 "무엇이 쪼개지나"로 다시 나누면 세 종류다. 이 분류가 도구 선택의 출발점이다.

| 깨지는 방식 | 예 | 왜 생기나 | 락 없는 해법 (이 노트) | 락 해법 (C7) |
|---|---|---|---|---|
| RMW가 쪼개진다 | 공유 카운터 `n++`, 플래그 비트 OR 대입 | LDR → ADD → STR 사이에 ISR | LDREX/STREX, C11 `atomic_fetch_add` (§2 · §3) | PRIMASK/BASEPRI로 세 명령 감싸기 |
| 여러 워드 읽기가 쪼개진다 (tearing) | 64비트 타임스탬프 hi/lo, setpoint 구조체 | 읽는 명령이 2개 이상 | 재시도(hi-lo-hi), seqlock, 더블 버퍼 (§4) | 읽는 동안 IRQ 마스크 |
| 순서가 바뀐다 | "데이터 쓰고 → 플래그 세우기"가 "플래그 → 데이터"로 | 컴파일러 재배치(1코어), CPU 재배치(다코어) | 컴파일러 배리어, acquire/release, DMB (§5 · §7) | 크리티컬 섹션은 컴파일러 배리어를 품고 있다 |

세 번째가 가장 잘 숨는다. 앞의 둘은 "ISR이 사이에 끼면"이라는 타이밍 조건이 있지만, 세 번째는 **컴파일러가 코드를 다르게 낸 순간 언제든** 생긴다. 그리고 `-O0`에서는 안 생기고 `-O2`에서만 생긴다(§7.4 예제 11). BM J5의 "-O2에서만 깨짐의 다른 원인"이 바로 이것이다.

단일 코어 Cortex-M3/M4에서 꼭 기억할 하드웨어 사실 세 가지:

1. **정렬된 32비트(그리고 8 · 16비트) 단일 LDR/STR은 원자적이다** — single-copy atomicity. 그래서 "값 하나 · 플래그 하나"는 `volatile`(컴파일러가 레지스터에 캐시하지 못하게)만으로 된다.
2. **CPU는 자기 코어가 보는 순서로는 프로그램 순서를 지킨다.** 단일 코어에서 ISR은 같은 코어에서 돌기 때문에, ISR이 보는 메모리 순서 = 명령 순서다. 그래서 1코어 ISR ↔ 태스크에서는 **CPU 쪽 배리어(DMB)가 필요 없고 컴파일러 배리어만 있으면 된다**(§5.4, §7.1에서 근거와 예외).
3. **예외 진입/복귀는 exclusive monitor를 지운다**(Armv7-M) — 그래서 LDREX/STREX가 ISR에 대해 정확히 동작한다(§2).

---

## 2. LDREX / STREX — "끼어들었으면 다시 해" 를 하드웨어가 알려 준다

> 🔁 이 절은 Bare-Metal BM J4(원자 연산 — LDREX/STREX · CLREX)와 겹친다 (주인: 이 노트).

### 2.1 직관 — 은행 창구의 번호표

LDREX는 "이 주소를 내가 읽었다"고 **번호표를 뽑는** 명령이고, STREX는 "번호표가 아직 유효하면 쓰고, 아니면 쓰지 말고 실패라고 알려 달라"는 명령이다. 그 사이에 누가 끼어들었으면(예외가 났거나, 다른 코어가 썼거나) 번호표는 무효가 되고 STREX는 메모리를 건드리지 않은 채 1을 돌려준다. 프로그램은 처음부터 다시 한다. 인터럽트를 끄지 않으니 다른 ISR의 지연에 아무것도 더하지 않는다 — 대신 실패하면 몇 명령을 다시 한다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">STREX가 실패하는 이유 — 사이에 예외가 끼면 로컬 모니터가 지워진다</text>
<text x="10" y="65" font-size="12">태스크</text>
<text x="10" y="125" font-size="12">ISR</text>
<text x="10" y="189" font-size="12">모니터</text>
<rect x="70" y="40" width="70" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="105" y="64.2" font-size="12" text-anchor="middle">LDREX</text>
<rect x="145" y="40" width="58" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="174" y="64.2" font-size="12" text-anchor="middle">ADDS</text>
<rect x="400" y="40" width="100" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="450" y="56.2" font-size="12" text-anchor="middle">STREX</text>
<text x="450" y="72.2" font-size="12" text-anchor="middle">→ 1 실패</text>
<rect x="505" y="40" width="78" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="544" y="56.2" font-size="12" text-anchor="middle">LDREX</text>
<text x="544" y="72.2" font-size="12" text-anchor="middle">ADDS</text>
<rect x="588" y="40" width="86" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="631" y="56.2" font-size="12" text-anchor="middle">STREX</text>
<text x="631" y="72.2" font-size="12" text-anchor="middle">→ 0 성공</text>
<rect x="215" y="100" width="180" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="305" y="116.2" font-size="12" text-anchor="middle">예외 진입 → ISR 본문</text>
<text x="305" y="132.2" font-size="12" text-anchor="middle">→ 예외 복귀</text>
<line x1="203" y1="70" x2="220" y2="98" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="395" y1="110" x2="405" y2="82" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="70" y="170" width="145" height="28" rx="5" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/>
<text x="142.5" y="188.2" font-size="12" text-anchor="middle">Exclusive</text>
<rect x="215" y="170" width="290" height="28" rx="5" fill="#888" fill-opacity="0.2" stroke="#888"/>
<text x="360" y="188.2" font-size="12" text-anchor="middle">Open — 예외 진입이 지웠다</text>
<rect x="505" y="170" width="83" height="28" rx="5" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/>
<text x="546.5" y="188.2" font-size="12" text-anchor="middle">Excl</text>
<rect x="588" y="170" width="86" height="28" rx="5" fill="#888" fill-opacity="0.2" stroke="#888"/>
<text x="631" y="188.2" font-size="12" text-anchor="middle">Open</text>
<text x="10" y="230" font-size="12">ISR이 같은 변수를 안 건드려도 STREX는 실패하고 다시 돈다 (예제 1: 8888회 = 8888회)</text>
</svg>
```

그림 2 — 태스크가 LDREX와 STREX 사이에서 선점당하면 예외 진입이 로컬 모니터를 Open으로 되돌리고, 복귀한 태스크의 STREX는 실패(1)를 받아 다시 돈다. ISR이 그 변수를 건드렸는지와 무관하다.

### 2.2 정의 — 명령 세 개와 모니터 두 종류

| 명령 | 하는 일 | 결과 |
|---|---|---|
| `LDREX Rt, [Rn]` | 워드를 읽고, 로컬 모니터를 Exclusive로 (주소 표시) | Rt = 메모리 값 |
| `STREX Rd, Rt, [Rn]` | 모니터가 Exclusive면 쓰고 Open으로, 아니면 **쓰지 않음** | Rd = 0 성공, 1 실패 |
| `CLREX` | 모니터를 Open으로 (표시 지우기) | 없음 |
| `LDREXB/H`, `STREXB/H` | 8 · 16비트 버전 | 같음 |

- **로컬 모니터(local monitor)**: 코어 안에 있는 상태 기계 하나. "LDREX를 했고 아직 STREX/CLREX/예외가 없었다"를 기억한다. 주소까지 비교하는지는 구현에 맡겨져 있다 — 그래서 "LDREX A → STREX B"가 실패한다는 보장이 없다(성공할 수도 있다). **짝을 맞추는 것은 소프트웨어 책임**이다.
- **글로벌 모니터(global monitor)**: 코어 밖(버스 · 메모리 컨트롤러 쪽)에서 **다른 마스터**(다른 코어, 때로는 DMA)의 쓰기를 감시한다. 멀티코어 MCU(듀얼 M33, M7+M4)의 공유 메모리에서 LDREX/STREX가 코어 간에 동작하려면 이것이 있어야 하고, 있는지 · 어느 메모리 영역에 있는지는 **칩마다 다르다**(데이터시트 확인 사항).
- **Armv7-M 규칙**: 예외 진입과 예외 복귀 시퀀스가 로컬 모니터를 Open으로 만든다(Armv7-M ARM의 synchronization primitives 장, "context switch support" 항목). 이 규칙 덕분에 "LDREX → (ISR) → STREX"는 반드시 실패하고, 그래서 **RTOS 컨텍스트 스위치 코드(PendSV)가 CLREX를 따로 할 필요가 없다**(§2.4에서 소스로 확인).
- **64비트는 없다**: Armv7-M/Armv8-M에는 `LDREXD`/`STREXD`가 없다. 64비트 원자 RMW는 하드웨어로 못 한다(§3.2에서 컴파일러가 라이브러리 호출을 내는 것을 본다).
- **Armv6-M(M0/M0+/M1)에는 셋 다 없다**(B7). Armv8-M Baseline(M23)에는 있다.

```svg
<svg viewBox="0 0 620 280" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">로컬 exclusive 모니터 — 상태 두 개짜리 기계</text>
<rect x="40" y="70" width="170" height="54" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="125" y="93.2" font-size="12" text-anchor="middle">Open Access</text>
<text x="125" y="109.2" font-size="12" text-anchor="middle">(표시된 주소 없음)</text>
<rect x="410" y="70" width="170" height="54" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="495" y="93.2" font-size="12" text-anchor="middle">Exclusive Access</text>
<text x="495" y="109.2" font-size="12" text-anchor="middle">(LDREX 주소 표시)</text>
<line x1="210" y1="84" x2="408" y2="84" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="310" y="76" font-size="12" text-anchor="middle">LDREX</text>
<line x1="410" y1="112" x2="212" y2="112" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="310" y="134" font-size="12" text-anchor="middle">STREX 성공 · CLREX · 예외 진입/복귀</text>
<path d="M 95 124 C 95 175, 160 175, 160 127" fill="none" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="127" y="188" font-size="12" text-anchor="middle">STREX → 1 (메모리에 안 씀)</text>
<rect x="40" y="212" width="560" height="54" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="320" y="235.2" font-size="12" text-anchor="middle">글로벌 모니터는 코어 밖(버스 · 메모리 쪽)에 있고 칩마다 다르다:</text>
<text x="320" y="251.2" font-size="12" text-anchor="middle">다른 마스터(코어 · DMA)의 쓰기를 보는 것은 이쪽이다 — 단일 코어 로컬 모니터는 못 본다</text>
</svg>
```

그림 3 — 로컬 모니터는 상태가 두 개뿐이다. LDREX가 Exclusive로 만들고, STREX 성공 · CLREX · 예외 진입/복귀가 Open으로 되돌린다. Open에서 STREX를 하면 메모리에 쓰지 않고 1을 돌려준다.

### 2.3 예제 1 — C7의 레이스를 LDREX/STREX로 고치고, STREX 실패를 센다

무엇을 확인하나: C7 예제 1과 같은 조건(메인 루프 20만 번 `++`, TIMER0 ISR 97사이클마다)에서 ① 그냥 `++`, ② C11 `atomic_fetch_add`(relaxed), ③ 손으로 쓴 LDREX/STREX 루프(실패 횟수를 센다)를 비교하고, ④ ISR이 **다른 변수**를 건드릴 때도 STREX가 실패하는지 본다. ④가 "예외 진입/복귀가 모니터를 지운다"의 실험적 확인이다.

```c
/* ex1 — main <-> ISR RMW race: plain ++ vs C11 atomic vs hand LDREX/STREX (count STREX fails) */
#include <stdatomic.h>
#include "d8.h"
#define N 200000u
volatile uint32_t g_plain, g_other, g_isr_n;
_Atomic uint32_t g_atom;
volatile uint32_t g_ll;
static volatile int g_mode;                 /* what the ISR touches */

void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    g_isr_n++;
    switch (g_mode) {
    case 0: g_plain++; break;
    case 1: atomic_fetch_add_explicit(&g_atom, 1, memory_order_relaxed); break;
    case 2: g_ll++; break;                  /* ISR writes the SAME word (plain str) */
    case 3: g_other++; break;               /* ISR writes a DIFFERENT word */
    }
}
static inline void ll_inc(volatile uint32_t *p, uint32_t *fails)
{
    uint32_t v, st;
    for (;;) {
        __asm volatile("ldrex %0, [%1]" : "=r"(v) : "r"(p) : "memory");
        v++;
        __asm volatile("strex %0, %2, [%1]" : "=&r"(st) : "r"(p), "r"(v) : "memory");
        if (st == 0) return;
        (*fails)++;
    }
}
static void run(int mode, const char *name)
{
    uint32_t fails = 0, t0;
    g_plain = g_ll = g_isr_n = 0; atomic_store(&g_atom, 0); g_mode = mode;
    timer0_start(97, 0x40);                 /* IRQ every 97 cycles = 3.88 us */
    t0 = CYC();
    for (uint32_t i = 0; i < N; i++) {
        if (mode == 0) g_plain++;
        else if (mode == 1) atomic_fetch_add_explicit(&g_atom, 1, memory_order_relaxed);
        else ll_inc(&g_ll, &fails);
    }
    t0 = CYC() - t0;
    timer0_stop();
    uint32_t got = mode == 0 ? g_plain : mode == 1 ? atomic_load(&g_atom) : g_ll;
    uint32_t expect = N + (mode <= 2 ? g_isr_n : 0);
    board_printf_raw("%s: isr=%u expect=%u got=%u lost=%u strex_fail=%u  %u ns/iter\n",
                     name, g_isr_n, expect, got, expect - got, fails, t0 * 40u / N);
}
int main(void)
{
    run(0, "plain ++");
    run(1, "atomic_fetch_add");
    run(2, "ldrex/strex, ISR same");
    run(3, "ldrex/strex, ISR other");
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D8; O=/tmp/rtosD8; make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=$S/ex1_ldrex.c OUT=$O/ex1 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/ex1/app.elf 60'
```

```text
   text	   data	    bss	    dec	    hex	filename
   4756	     96	    352	   5204	   1454	/tmp/rtosD8/ex1/app.elf
plain ++: isr=9592 expect=209592 got=203838 lost=5754 strex_fail=0  186 ns/iter
atomic_fetch_add: isr=14034 expect=214034 got=214034 lost=0 strex_fail=0  272 ns/iter
ldrex/strex, ISR same: isr=17777 expect=217777 got=217777 lost=0 strex_fail=8888  344 ns/iter
ldrex/strex, ISR other: isr=17777 expect=200000 got=200000 lost=0 strex_fail=8888  344 ns/iter
[board_exit] code=0
```

출력에서 볼 것:

- 그냥 `++`는 ISR 9592번 중 **5754번(60%)을 잃었다** — C7 예제 1과 같은 3/5 비율이다(루프 5명령 중 3곳이 "읽고 아직 안 쓴" 창).
- `atomic_fetch_add`와 손 LDREX/STREX는 **lost=0**. 인터럽트를 한 번도 끄지 않았다.
- **같은 변수(③)든 다른 변수(④)든 STREX 실패가 정확히 8888번**이다. 실패의 원인이 "ISR이 값을 바꿔서"가 아니라 "예외가 사이에 끼어서"라는 뜻이다. QEMU가 Armv7-M 규칙대로 예외 진입/복귀에서 모니터를 지운다(QEMU에서 본 것이고, 실칩도 같은 규칙을 따라야 한다 — 아키텍처 요구 사항이기 때문이다).
- 실패율 8888/17777 = 50%는 icount의 결정적 위상 때문에 나온 숫자다(ISR 주기와 루프 길이의 관계). 아래처럼 `one-insn-per-tb`로 돌려도 같은 숫자가 나왔고, ICOUNT 없이 호스트 속도로 돌리면 루프가 순식간에 끝나 ISR이 165번만 와서 실패 0이었다. 실칩에서 ISR이 1 kHz라면 실패는 수천 번에 한 번이다 — **재시도 비용은 평균적으로 거의 0이고, 최악은 "한 번 더"**다.
- 비용: 순수 루프가 아니라 ISR 부하를 포함한 숫자라 절대값보다 순서를 본다. 일반 `++` 186 ns < atomic 272 ns < 손 루프 344 ns(실패 카운터 갱신 때문).

```sh
limactl shell elinux -- bash -lc 'cd /tmp/rtosD8/ex1; timeout 60 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio -semihosting-config enable=on,target=native -icount shift=5,align=off,sleep=off -accel tcg,one-insn-per-tb=on -kernel app.elf; timeout 60 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio -semihosting-config enable=on,target=native -kernel app.elf'
```

```text
plain ++: isr=9592 expect=209592 got=203838 lost=5754 strex_fail=0  186 ns/iter
atomic_fetch_add: isr=14034 expect=214034 got=214034 lost=0 strex_fail=0  272 ns/iter
ldrex/strex, ISR same: isr=17777 expect=217777 got=217777 lost=0 strex_fail=8888  344 ns/iter
ldrex/strex, ISR other: isr=17777 expect=200000 got=200000 lost=0 strex_fail=8888  344 ns/iter
[board_exit] code=0
plain ++: isr=0 expect=200000 got=200000 lost=0 strex_fail=0  3 ns/iter
atomic_fetch_add: isr=1 expect=200001 got=200001 lost=0 strex_fail=0  17 ns/iter
ldrex/strex, ISR same: isr=165 expect=200165 got=200165 lost=0 strex_fail=0  15 ns/iter
ldrex/strex, ISR other: isr=1 expect=200000 got=200000 lost=0 strex_fail=0  7 ns/iter
[board_exit] code=0
```

손으로 쓴 루프가 무엇으로 컴파일됐나 — 루프 본문이다.

```sh
limactl shell elinux -- bash -lc 'arm-none-eabi-objdump -d --no-show-raw-insn /tmp/rtosD8/ex1/app.elf | sed -n "/<run>:/,/^$/p" | grep -n -B3 -A4 "strex" | head -40'
```

```text
...
90-     1b2:	ldrex	r3, [lr]
91-     1b6:	adds	r3, #1
92:     1b8:	strex	r2, r3, [lr]
93-     1bc:	cmp	r2, #0
94-     1be:	bne.n	1b2 <run+0xf2>
95-     1c0:	subs	r7, #1
96-     1c2:	bne.n	1b2 <run+0xf2>
```

(앞쪽의 mode 2 루프는 생략했다.) LDREX와 STREX 사이에는 `adds` 하나만 있다. **이 사이를 짧게** 두는 것이 규칙이다 — 길게 두면 실패 확률이 올라가고, 사이에 다른 LDREX/STREX나 함수 호출이 들어가면 모니터 상태가 엉킨다.

### 2.4 CLREX는 언제 필요한가 — 포트 소스를 grep한다

무엇을 확인하나: Armv7-M 규칙이 맞다면 Cortex-M 컨텍스트 스위치는 CLREX가 필요 없다. 실제 RTOS 포트가 어떻게 하는지 본다.

```sh
limactl shell elinux -- bash -lc 'cd /var/rtoslab/src/FreeRTOS-Kernel; grep -rli clrex portable/GCC | sed "s#/[^/]*\$##" | sort -u | tr "\n" " "; echo; cd /var/rtoslab/zephyrproject/zephyr; grep -rli "clrex" . 2>/dev/null | head'
```

```text
portable/GCC/ARM_CM23/non_secure portable/GCC/ARM_CM23_NTZ/non_secure portable/GCC/ARM_CM33/non_secure portable/GCC/ARM_CM33_NTZ/non_secure portable/GCC/ARM_CM35P/non_secure portable/GCC/ARM_CM35P_NTZ/non_secure portable/GCC/ARM_CM52/non_secure portable/GCC/ARM_CM52_NTZ/non_secure portable/GCC/ARM_CM55/non_secure portable/GCC/ARM_CM55_NTZ/non_secure portable/GCC/ARM_CM85/non_secure portable/GCC/ARM_CM85_NTZ/non_secure portable/GCC/ARM_CR82 portable/GCC/ARM_STAR_MC3/non_secure portable/GCC/ARM_STAR_MC3_NTZ/non_secure 
./samples/modules/tflite-micro/hello_world/train/train_hello_world_model.ipynb
./boards/native/nrf_bsim/common/cmsis/cmsis_instr.h
./boards/native/nrf_bsim/common/cmsis/cmsis.c
```

출력에서 볼 것:

- **FreeRTOS ARM_CM3 · ARM_CM4F · ARM_CM7 포트에는 CLREX가 없다.** Zephyr의 `arch/`(Cortex-M `swap_helper.S` 포함)에도 없다(Zephyr에서 나온 것은 시뮬레이터용 CMSIS 흉내와 노트북 파일뿐). 둘 다 "예외 진입/복귀가 지운다"는 Armv7-M/Armv8-M 규칙에 기댄다.
- FreeRTOS에서 CLREX가 나오는 곳은 Armv8-M 포트(CM23 · CM33 · CM55 · CM85 …)의 `port.c` 한 곳이다. V11.3.2의 이 포트들에 있는 **SMP용 스핀락 trylock** `prvSpinTrylock()`이다.

```c
/* portable/GCC/ARM_CM33/non_secure/port.c (V11.3.2), prvSpinTrylock — 발췌 */
        __asm volatile (
            " ldrex %0, [%1]   \n"
            : "=r" ( ulVal )
            : "r" ( ulLock )
            : "memory"
        );

        if( ulVal != 0U )
        {
            __asm volatile ("clrex" ::: "memory");
            return 1U;
        }
        ...
        if( ulStatus != 0U )
        {
            return 1U;
        }
        __asm volatile ( "dmb" ::: "memory" );
        return 0U;
```

**CLREX가 필요한 진짜 장면이 이것이다**: LDREX를 했는데 **STREX를 하지 않고 빠져나갈 때**(락이 이미 잡혀 있음). 모니터를 Exclusive로 남겨 두면, 나중에 엉뚱한 STREX(다른 주소!)가 성공해 버릴 수 있다 — 로컬 모니터는 주소를 비교하지 않아도 되기 때문이다. 성공 경로 끝의 `dmb`는 락 획득을 acquire로 만든다(코어 간 순서, §7). 이 스핀락의 구조는 C10의 주제다.

### 2.5 예제 1b — M3/M4의 다른 원자 수단: 비트 밴딩

Cortex-M3/M4(옵션 기능, M7 · M33에는 없음)는 SRAM과 주변장치 영역의 **비트 하나를 워드 하나로 비춘 별칭 영역(bit-band alias)**을 가진다. 별칭 주소에 쓰기 하나를 하면 버스가 그 비트만 원자적으로 RMW한다 — LDREX도, 인터럽트 마스크도 없이.

무엇을 확인하나: QEMU AN385에서 비트 밴딩으로 비트를 세우고 지운다.

```c
/* ex2 — M3/M4 bit-band: set/clear one bit of SRAM with a single store (no LDREX, no IRQ mask) */
#include "d8.h"
#define BB_SRAM(addr, bit) REG32(0x22000000u + (((uint32_t)(addr) - 0x20000000u) * 32u) + (bit) * 4u)
volatile uint32_t g_flags = 0x10;
int main(void)
{
    BB_SRAM(&g_flags, 3) = 1;                  /* set bit 3   */
    BB_SRAM(&g_flags, 4) = 0;                  /* clear bit 4 */
    board_printf_raw("g_flags=0x%x  bit3 via alias reads %u\n", g_flags, BB_SRAM(&g_flags, 3));
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D8; O=/tmp/rtosD8; make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=$S/ex2_bitband.c OUT=$O/ex2 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/ex2/app.elf 10; arm-none-eabi-objdump -d --no-show-raw-insn $O/ex2/app.elf | sed -n "/<main>:/,/^$/p" | head -12'
```

```text
g_flags=0x8  bit3 via alias reads 1
[board_exit] code=0
000000c0 <main>:
      c0:	push	{r4, lr}
      c2:	movs	r1, #1
      c4:	movs	r4, #0
      c6:	ldr	r2, [pc, #24]	@ (e0 <main+0x20>)
      c8:	ldr	r0, [pc, #24]	@ (e4 <main+0x24>)
      ca:	lsls	r3, r2, #5
      cc:	add.w	r3, r3, #570425344	@ 0x22000000
      d0:	str	r1, [r3, #12]
      d2:	str	r4, [r3, #16]
      ...
```

출력에서 볼 것: 0x10에서 시작해 bit 3을 세우고 bit 4를 지우니 0x8이다. 생성 코드는 `str` 두 개뿐이다. 이식성이 없어서(M7 · M33 · M55에 없음) 새 코드에는 권하지 않지만, 오래된 STM32F1/F4 코드에서 `BITBAND_PERI(...) = 1` 같은 매크로를 보면 "원자적 비트 세트"라고 읽으면 된다.

### 2.6 LDREX/STREX의 함정

- **주변장치 레지스터(Device 메모리)에 LDREX를 쓰지 않는다.** 아키텍처는 exclusive 접근이 어느 메모리 타입에서 동작하는지를 구현에 맡기고, 주변장치 버스에는 글로벌 모니터가 보통 없다. 레지스터 비트 원자 조작은 그 주변장치가 주는 SET/CLR 레지스터(예: GPIO BSRR)나 비트 밴딩, 또는 IRQ 마스크로 한다.
- **DMA는 로컬 모니터를 지우지 못한다.** 단일 코어 MCU에서 "DMA가 쓰는 변수"를 LDREX/STREX로 보호한다는 생각은 틀렸다. DMA와의 공유는 소유권(owner 비트 · 더블 버퍼)으로 설계한다(§9.1).
- **LDREX와 STREX 사이에 메모리 접근 · 함수 호출 · 다른 LDREX를 넣지 않는다.** C 컴파일러가 만드는 `atomic_*`를 쓰면 이 규칙이 자동으로 지켜진다. 손으로 쓸 이유는 거의 없다(예제 1의 손 루프는 실패를 세기 위한 것이다).
- **재시도 루프의 최악은 이론상 무한**이다. 실제로는 "ISR이 STREX 직전마다 정확히 온다"는 병적인 경우뿐이고, LDREX ~ STREX가 2~3명령이면 ISR 주기가 그보다 길기만 해도 다음 시도는 성공한다. 하드 실시간 분석(N 모듈)에서는 "재시도 1회"를 WCET에 넣는다.

---

## 3. C11 `<stdatomic.h>` — M3/M4/M33에서는 명령, M0에서는 라이브러리 호출

> 🔁 이 절은 Bare-Metal BM J4(C11 atomics(M3 이상), M0에서의 대체)와 겹친다 (주인: 이 노트).

### 3.1 memory_order를 Cortex-M 말로 번역하기

`memory_order_relaxed` · `acquire` · `release` · `seq_cst`의 정의는 EL F5 §2와 A8에서 공부했다. Cortex-M에서 달라지는 점은 **"무엇으로 컴파일되나"**와 **"단일 코어에서 그게 필요한가"** 두 가지다.

| C11 연산 | Armv7-M (M3/M4/M7) | Armv8-M Mainline (M33/M55) | Armv6-M (M0/M0+) | 단일 코어 ISR ↔ 태스크에 필요한 것 |
|---|---|---|---|---|
| `atomic_load` relaxed | `ldr` | `ldr` | `ldr` | 그대로 충분 |
| `atomic_load` acquire | `ldr` + `dmb ish` | `lda` | `ldr` + `dmb ish` | 컴파일러 배리어면 충분 (DMB는 남는다) |
| `atomic_store` release | `dmb ish` + `str` | `stl` | `dmb ish` + `str` | 컴파일러 배리어면 충분 |
| `atomic_store` seq_cst | `dmb` + `str` + `dmb` | `stl` | `dmb` + `str` + `dmb` | 컴파일러 배리어면 충분 |
| `atomic_fetch_add` relaxed | `ldrex`/`strex` 루프 | `ldrex`/`strex` 루프 | `__atomic_fetch_add_4` 호출 | RMW의 원자성은 **필요** |
| `atomic_fetch_add` seq_cst | `dmb` + 루프 + `dmb` | `ldaex`/`stlex` 루프 | `__atomic_fetch_add_4` 호출 | 원자성 필요, 순서는 컴파일러 배리어로 충분 |
| 64비트 아무거나 | `__atomic_load_8` 등 호출 | 호출 | 호출 | 하드웨어로 불가 → 락 |

마지막 열이 이 노트의 요점이다. **단일 코어에서 C11 atomics가 주는 것 중 정말 필요한 것은 RMW의 원자성(LDREX/STREX)과 컴파일러 순서**이고, `dmb`는 1코어 ISR ↔ 태스크에서는 일을 하지 않는다(§7.1). 그래도 C11을 쓰는 것이 맞다. 같은 코드가 듀얼코어로 옮겨 가도 맞고, 컴파일러가 순서를 정확히 지키기 때문이다. DMB 몇 개의 비용은 §6.3에서 잰다(바이트당 약 95 ns 차이, QEMU icount 기준).

### 3.2 예제 2 — 같은 C 함수 7개를 M3 · M33 · M0으로 컴파일해서 본다

무엇을 확인하나: 위 표의 각 칸이 실제 생성 코드와 맞는지.

```c
/* atom_gen.c — what C11 atomics compile to on each Cortex-M (look at objdump) */
#include <stdatomic.h>
#include <stdint.h>
_Atomic uint32_t a32;
_Atomic uint64_t a64;
uint32_t fetch_add_sc(void)   { return atomic_fetch_add(&a32, 1); }
uint32_t fetch_add_rlx(void)  { return atomic_fetch_add_explicit(&a32, 1, memory_order_relaxed); }
_Bool    cas(uint32_t o, uint32_t n) { return atomic_compare_exchange_strong(&a32, &o, n); }
uint32_t load_acq(void)       { return atomic_load_explicit(&a32, memory_order_acquire); }
void     store_rel(uint32_t v){ atomic_store_explicit(&a32, v, memory_order_release); }
void     store_sc(uint32_t v) { atomic_store(&a32, v); }
uint64_t load64(void)         { return atomic_load(&a64); }
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D8; O=/tmp/rtosD8/gen; mkdir -p $O; for c in cortex-m3 cortex-m33 cortex-m0; do arm-none-eabi-gcc -mthumb -mcpu=$c -O2 -Wall -Wextra -c $S/atom_gen.c -o $O/$c.o || exit 1; echo "===== $c"; arm-none-eabi-objdump -d --no-show-raw-insn $O/$c.o | grep -v "^$\|file format\|Disassembly\|\.word"; done'
```

```text
===== cortex-m3
00000000 <fetch_add_sc>:
   0:	dmb	ish
   4:	ldr	r3, [pc, #20]	@ (1c <fetch_add_sc+0x1c>)
   6:	ldrex	r0, [r3]
   a:	adds	r2, r0, #1
   c:	strex	r1, r2, [r3]
  10:	cmp	r1, #0
  12:	bne.n	6 <fetch_add_sc+0x6>
  14:	dmb	ish
  18:	bx	lr
...
00000038 <cas>:
  38:	dmb	ish
  3c:	ldr	r3, [pc, #28]	@ (5c <cas+0x24>)
  3e:	ldrex	r2, [r3]
  42:	cmp	r2, r0
  44:	bne.n	50 <cas+0x18>
  46:	strex	ip, r1, [r3]
  4a:	cmp.w	ip, #0
  4e:	bne.n	3e <cas+0x6>
  50:	dmb	ish
...
00000060 <load_acq>:
  60:	ldr	r3, [pc, #8]	@ (6c <load_acq+0xc>)
  62:	ldr	r0, [r3, #0]
  64:	dmb	ish
  68:	bx	lr
00000070 <store_rel>:
  70:	ldr	r3, [pc, #8]	@ (7c <store_rel+0xc>)
  72:	dmb	ish
  76:	str	r0, [r3, #0]
  78:	bx	lr
...
00000094 <load64>:
  94:	movs	r1, #5
  96:	ldr	r0, [pc, #4]	@ (9c <load64+0x8>)
  98:	b.w	0 <__atomic_load_8>
===== cortex-m33
00000000 <fetch_add_sc>:
   0:	ldr	r3, [pc, #16]	@ (14 <fetch_add_sc+0x14>)
   2:	ldaex	r0, [r3]
   6:	adds	r2, r0, #1
   8:	stlex	r1, r2, [r3]
   c:	cmp	r1, #0
   e:	bne.n	2 <fetch_add_sc+0x2>
  10:	bx	lr
...
00000050 <load_acq>:
  50:	ldr	r3, [pc, #4]	@ (58 <load_acq+0x8>)
  52:	lda	r0, [r3]
  56:	bx	lr
0000005c <store_rel>:
  5c:	ldr	r3, [pc, #4]	@ (64 <store_rel+0x8>)
  5e:	stl	r0, [r3]
  62:	bx	lr
...
===== cortex-m0
00000000 <fetch_add_sc>:
   0:	push	{r4, lr}
   2:	movs	r2, #5
   4:	movs	r1, #1
   6:	ldr	r0, [pc, #8]	@ (10 <fetch_add_sc+0x10>)
   8:	bl	0 <__atomic_fetch_add_4>
   c:	pop	{r4, pc}
...
00000028 <cas>:
...
  38:	bl	0 <__atomic_compare_exchange_4>
...
00000044 <load_acq>:
  44:	ldr	r3, [pc, #8]	@ (50 <load_acq+0xc>)
  46:	ldr	r0, [r3, #0]
  48:	dmb	ish
  4c:	bx	lr
...
```

(`fetch_add_rlx` · `store_sc`와 반복되는 부분은 `...`로 줄였다.)

출력에서 볼 것:

- **M3**: seq_cst RMW = `dmb` + `ldrex`/`strex` 루프 + `dmb`. acquire load = `ldr` 뒤에 `dmb`, release store = `str` 앞에 `dmb` — 배리어 위치가 정의 그대로다("뒤의 접근이 앞으로 못 온다", "앞의 접근이 뒤로 못 간다").
- **M33**(Armv8-M Mainline): **`lda`/`stl`/`ldaex`/`stlex`** — acquire/release가 명령에 들어 있어서 `dmb`가 사라진다. Armv8-A의 `ldar`/`stlr`(EL F5)과 같은 아이디어가 MCU에 들어온 것이다.
- **M0**(Armv6-M): RMW와 CAS가 **`__atomic_fetch_add_4`, `__atomic_compare_exchange_4` 함수 호출**이 된다. 마지막 인자 `r2 = 5`는 `__ATOMIC_SEQ_CST`의 값이다. 반면 load/store는 `ldr`/`str` + `dmb`로 직접 나온다 — Armv6-M에도 `dmb`는 있고, 정렬된 32비트 load/store는 원래 원자적이기 때문이다.
- **64비트**는 M3 · M33에서도 `__atomic_load_8` 호출이다. `LDREXD`가 없으니까(§2.2). C7 §1.4에서 본 `ldrd`는 "우연히 한 명령"일 뿐 아키텍처가 원자성을 약속하지 않는다.
- Cortex-M에서 `dmb ish`의 `ish`는 의미가 없다. Armv7-M은 옵션으로 `SY`만 정의하고, 다른 값은 예약 인코딩이다(Armv7-M ARM은 이것들이 SY처럼 실행되지만 소프트웨어가 기대면 안 된다고 적는다). 손으로 쓸 때는 그냥 `dmb`(= `dmb sy`, CMSIS `__DMB()`)를 쓴다.

### 3.3 예제 3 — "lock-free인가?"를 묻는 세 가지 방법, 그리고 링크 에러

무엇을 확인하나: 컴파일 시점 매크로 `ATOMIC_INT_LOCK_FREE`, GCC 내장 `__atomic_always_lock_free()`, 실행 시점 `atomic_is_lock_free()`가 M3 · M0에서 무엇을 말하는지. 그리고 libatomic이 없는 `arm-none-eabi`에서 링크가 되는지.

```c
/* lf.c — is it lock-free? compile-time macros and atomic_is_lock_free() */
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
_Atomic uint8_t a8; _Atomic uint32_t a32; _Atomic uint64_t a64;
int main(void)
{
    printf("ATOMIC_INT_LOCK_FREE=%d ATOMIC_LLONG_LOCK_FREE=%d\n", ATOMIC_INT_LOCK_FREE, ATOMIC_LLONG_LOCK_FREE);
    printf("always_lock_free 1/4/8 = %d %d %d\n", __atomic_always_lock_free(1, 0),
           __atomic_always_lock_free(4, 0), __atomic_always_lock_free(8, 0));
    printf("is_lock_free u8/u32/u64 = %d %d %d\n", atomic_is_lock_free(&a8),
           atomic_is_lock_free(&a32), atomic_is_lock_free(&a64));
    return atomic_fetch_add(&a32, 1);
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D8; O=/tmp/rtosD8/lf; mkdir -p $O; for c in cortex-m3 cortex-m0; do echo "===== $c"; arm-none-eabi-gcc -mthumb -mcpu=$c -O2 -Wall -Wextra --specs=nano.specs --specs=nosys.specs $S/lf.c -o $O/$c.elf 2>&1 | grep -v "warning: .*is not implemented and will always fail\|_close\|_lseek\|_read\|_write\|_fstat\|_isatty\|_getpid\|_kill" | sed "s#.*/ld: ##" ; ls $O/$c.elf 2>/dev/null; done; find / -name "libatomic*" -path "*arm-none-eabi*" 2>/dev/null | head'
```

```text
===== cortex-m3
/tmp/ccLHMC6I.o: in function `main':
lf.c:(.text.startup+0x1c): undefined reference to `__atomic_is_lock_free'
(__atomic_is_lock_free): Unknown destination type (ARM/Thumb) in /tmp/ccLHMC6I.o
lf.c:(.text.startup+0x1c): dangerous relocation: unsupported relocation
collect2: error: ld returned 1 exit status
===== cortex-m0
/tmp/ccu2jgz3.o: in function `main':
lf.c:(.text.startup+0x1e): undefined reference to `__atomic_is_lock_free'
...
lf.c:(.text.startup+0x2a): undefined reference to `__atomic_is_lock_free'
...
lf.c:(.text.startup+0x36): undefined reference to `__atomic_is_lock_free'
...
lf.c:(.text.startup+0x4e): undefined reference to `__atomic_fetch_add_4'
(__atomic_fetch_add_4): Unknown destination type (ARM/Thumb) in /tmp/ccu2jgz3.o
lf.c:(.text.startup+0x4e): dangerous relocation: unsupported relocation
collect2: error: ld returned 1 exit status
```

(M0의 반복 줄 일부를 `...`로 줄였다. `find`는 아무것도 찾지 못했다 — 툴체인에 libatomic이 없다.)

출력에서 볼 것:

- **둘 다 링크가 안 된다.** M3에서는 `atomic_is_lock_free(&a64)` 하나(64비트는 항상 lock-free가 아니라서 실행 시점에 물어봐야 함)가, M0에서는 **세 개 전부와 `atomic_fetch_add`**가 라이브러리 함수를 부른다. Linux라면 libatomic이 받아 주지만 `arm-none-eabi`에는 **libatomic이 없다**. 그래서 M0 프로젝트에서 C11 atomics를 쓰면 처음 만나는 것이 이 링크 에러다.
- M0에서는 8비트(`a8`)조차 `atomic_is_lock_free`가 라이브러리 호출이다. load/store는 원자적이지만 RMW가 아니므로 "항상 lock-free"라고 컴파일러가 약속하지 못한다.
- 정리: **M3 이상에서 32비트 이하는 lock-free, 64비트는 아님. M0에서는 아무것도 컴파일러가 보장하지 않는다.**

### 3.4 예제 4 — M0에서 `__atomic_*`를 직접 공급하고 `microbit`(M0) QEMU에서 돌린다

> 🔁 이 소절은 Bare-Metal BM J4의 "M0에서의 대체"와 겹친다 (주인: 이 노트). C7 §3.6이 FreeRTOS ARM_CM0 포트의 PRIMASK 크리티컬 섹션을 다뤘다.

M0에는 LDREX가 없으니 선택지는 하나다. **인터럽트를 잠깐 끄고 그냥 RMW를 한다.** 단일 코어라서 이것으로 원자성이 생긴다(다른 코어나 DMA에 대해서는 아니다). 컴파일러가 부르는 이름으로 함수를 만들면 C11 코드는 그대로 둔 채 링크된다.

먼저 nRF51(QEMU `microbit`)용 최소 시작 코드다. 랩 템플릿은 M3 전용이라 직접 만들었다(벡터 테이블 · .data 복사 · .bss 0 · semihosting 출력).

```c
/* m0io.h — semihosting puts/putu/exit (works on any Cortex-M under QEMU) */
#ifndef M0IO_H
#define M0IO_H
#include <stdint.h>
static inline int sh_call(int op, const void *arg)
{
    register int r0 __asm("r0") = op;
    register const void *r1 __asm("r1") = arg;
    __asm volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
static inline void sh_puts(const char *s) { sh_call(0x04, s); }      /* SYS_WRITE0 */
static inline void sh_putu(uint32_t v)
{
    char b[12]; int i = 11; b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    sh_puts(&b[i]);
}
static inline void __attribute__((noreturn)) sh_exit(int code)
{
    sh_call(0x18, (const void *)(code ? 0x20024 : 0x20026));      /* SYS_EXIT */
    for (;;) { }
}
#endif
```

```c
/* m0_start.c — minimal Cortex-M0 startup + semihosting output for QEMU microbit */
#include <stdint.h>
#include "m0io.h"
extern uint32_t _etext, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);
void TIMER0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void Default_Handler(void) { sh_puts("unexpected exception\n"); sh_exit(1); }
void Reset_Handler(void)
{
    uint32_t *s = &_etext, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;
    for (d = &_sbss; d < &_ebss; ) *d++ = 0;
    sh_exit(main());
}
__attribute__((section(".vectors"), used)) static void (*const vectors[16 + 32])(void) = {
    (void (*)(void))&_estack, Reset_Handler, Default_Handler, Default_Handler,
    [16 + 8] = TIMER0_IRQHandler,                  /* nRF51 TIMER0 = IRQ 8 */
};
```

```text
/* nRF51822 (QEMU microbit): 256 KB flash @0, 16 KB RAM @0x20000000 */
MEMORY { FLASH (rx) : ORIGIN = 0x0, LENGTH = 256K  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 16K }
SECTIONS {
  .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > FLASH
  .data : AT(_etext) { _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM
  .bss  : { _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
  _estack = ORIGIN(RAM) + LENGTH(RAM);
}
```

이제 본체. `__atomic_fetch_add_4`, `__atomic_compare_exchange_4`, `__atomic_is_lock_free`를 PRIMASK로 구현하고, nRF51 TIMER0(IRQ 8, 3.9 µs마다) ISR과 메인이 같은 카운터를 올린다.

```c
/* ex3 — Cortex-M0 (Armv6-M, no LDREX/STREX): supply the __atomic_* libcalls with PRIMASK */
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include "m0io.h"
#define REG(a)  (*(volatile uint32_t *)(a))
#define T0      0x40008000u                    /* nRF51 TIMER0, IRQ 8 */
#define N       100000u

/* ---- the "libatomic" for a single-core M0: mask IRQs around a plain RMW ---- */
static inline uint32_t pm_save(void)
{ uint32_t k; __asm volatile("mrs %0, primask\n cpsid i" : "=r"(k) : : "memory"); return k; }
static inline void pm_restore(uint32_t k) { __asm volatile("msr primask, %0" : : "r"(k) : "memory"); }
unsigned int __atomic_fetch_add_4(volatile void *p, unsigned int v, int mo)
{
    (void)mo;
    uint32_t k = pm_save();
    unsigned int old = *(volatile unsigned int *)p;
    *(volatile unsigned int *)p = old + v;
    pm_restore(k);
    return old;
}
bool __atomic_compare_exchange_4(volatile void *p, void *exp, unsigned int des, bool weak, int s, int f)
{
    (void)weak; (void)s; (void)f;
    uint32_t k = pm_save();
    unsigned int cur = *(volatile unsigned int *)p;
    bool ok = (cur == *(unsigned int *)exp);
    if (ok) *(volatile unsigned int *)p = des; else *(unsigned int *)exp = cur;
    pm_restore(k);
    return ok;
}
bool __atomic_is_lock_free(size_t size, const volatile void *p) { (void)size; (void)p; return false; }

volatile uint32_t g_plain, g_isr;
_Atomic uint32_t g_atom;
static volatile int g_mode;
void TIMER0_IRQHandler(void)
{
    REG(T0 + 0x140) = 0;                       /* EVENTS_COMPARE[0] = 0 */
    g_isr++;
    if (g_mode == 0) g_plain++; else atomic_fetch_add(&g_atom, 1);
}
static void run(int mode)
{
    g_plain = g_isr = 0; atomic_store(&g_atom, 0); g_mode = mode;
    REG(T0 + 0x00C) = 1;                       /* TASKS_CLEAR */
    REG(T0 + 0x000) = 1;                       /* TASKS_START */
    for (uint32_t i = 0; i < N; i++) { if (mode == 0) g_plain++; else atomic_fetch_add(&g_atom, 1); }
    REG(T0 + 0x004) = 1;                       /* TASKS_STOP */
    uint32_t got = mode == 0 ? g_plain : atomic_load(&g_atom);
    sh_puts(mode == 0 ? "plain ++        : isr=" : "atomic_fetch_add: isr="); sh_putu(g_isr);
    sh_puts(" lost="); sh_putu(N + g_isr - got); sh_puts("\n");
}
int main(void)
{
    _Atomic uint32_t x = 5; uint32_t e = 5;
    sh_puts("is_lock_free(u32) = "); sh_putu(atomic_is_lock_free(&g_atom));
    sh_puts(", cas(5->9) = "); sh_putu(atomic_compare_exchange_strong(&x, &e, 9)); sh_puts(" x="); sh_putu(x); sh_puts("\n");
    REG(T0 + 0x504) = 0; REG(T0 + 0x508) = 3; REG(T0 + 0x510) = 0;   /* timer, 32-bit, 16 MHz */
    REG(T0 + 0x540) = 61;                      /* CC[0]: compare every 62 ticks = 3.9 us */
    REG(T0 + 0x200) = 1;                       /* SHORTS: COMPARE0 -> CLEAR */
    REG(T0 + 0x304) = 1u << 16;                /* INTENSET COMPARE0 */
    REG(0xE000E100u) = 1u << 8;                /* NVIC ISER: IRQ 8 */
    run(0); run(1);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D8/m0; O=/tmp/rtosD8/m0; mkdir -p $O; arm-none-eabi-gcc -mthumb -mcpu=cortex-m0 -O2 -g -std=gnu11 -Wall -Wextra -ffreestanding -nostdlib -T $S/m0.ld $S/m0_start.c $S/ex3_m0.c -lgcc -o $O/ex3.elf && arm-none-eabi-size $O/ex3.elf && timeout 20 qemu-system-arm -machine microbit -nographic -monitor none -serial null -semihosting-config enable=on,target=native -icount shift=5,align=off,sleep=off -kernel $O/ex3.elf; echo exit=$?'
```

```text
   text	   data	    bss	    dec	    hex	filename
   1296	      0	     16	   1312	    520	/tmp/rtosD8/m0/ex3.elf
is_lock_free(u32) = 0, cas(5->9) = 1 x=9
plain ++        : isr=5607 lost=2805
atomic_fetch_add: isr=8771 lost=0
exit=0
```

출력에서 볼 것:

- 경고 0개로 링크됐고 M0에서 실제로 돈다. 그냥 `++`는 ISR 5607번 중 2805번을 잃었고, 우리가 공급한 `atomic_fetch_add`는 0이다.
- `atomic_is_lock_free`는 0을 돌려주게 만들었다. **인터럽트 마스크 구현은 "lock-free"가 아니다** — 멀티코어나 DMA에 대해 아무것도 보장하지 않고, IRQ 지연에 RMW 길이만큼을 더한다. 이름에 정직하게 0을 돌려주는 것이 맞다.
- 이 구현은 **단일 코어 전용**이다. RP2040(듀얼 M0+)처럼 M0 코어가 둘인 칩에서는 두 코어가 동시에 PRIMASK를 끄고 같은 RMW를 할 수 있다. 그래서 RP2040은 하드웨어 스핀락 블록(SIO spinlock 레지스터)을 따로 둔다(B7 · C10).
- 함정: `__atomic_fetch_add_4` 시그니처는 GCC 내장 선언(`unsigned int`)과 맞아야 한다. `uint32_t`(= `unsigned long`)로 쓰면 "conflicting types for built-in function" 경고가 난다.

### 3.5 RTOS들은 어떻게 고르나 — Zephyr Kconfig와 FreeRTOS `atomic.h`

Zephyr는 같은 `atomic_t` API를 아키텍처에 따라 세 가지로 구현한다(`kernel/Kconfig`, v4.4.2).

```text
config ATOMIC_OPERATIONS_BUILTIN
	bool
	help
	  Use the compiler builtin functions for atomic operations. This is
	  the preferred method. However, support for all arches in GCC is
	  incomplete.
...
config ATOMIC_OPERATIONS_C
	bool
	help
	  Use atomic operations routines that are implemented entirely
	  in C by locking interrupts. Selected by architectures which either
	  do not have support for atomic operations in their instruction
	  set, ...
```

그리고 Cortex-M은 이렇게 고른다(`arch/arm/core/cortex_m/Kconfig`).

```text
config ARMV6_M_ARMV8_M_BASELINE
	bool
	select ATOMIC_OPERATIONS_BUILTIN if ARMV8_M_BASELINE
	select ATOMIC_OPERATIONS_C if !ARMV8_M_BASELINE
...
config ARMV7_M_ARMV8_M_MAINLINE
	bool
	select ATOMIC_OPERATIONS_BUILTIN
```

- **M3/M4/M7/M33/M55**: `BUILTIN` → `include/zephyr/sys/atomic_builtin.h`의 `__atomic_fetch_add(target, value, __ATOMIC_SEQ_CST)` 등. 인라인 LDREX/STREX가 된다(§8 예제 14에서 확인).
- **M0/M0+**: `C` → `kernel/atomic_c.c`. 파일 안의 전역 `static struct k_spinlock lock;` 하나로 모든 atomic 연산을 감싼다. 단일 코어에서 `k_spin_lock` = `irq_lock`이므로(C7 §8.2) 예제 4의 PRIMASK 구현과 같은 일을 한다.
- **M23**(Armv8-M Baseline): LDREX/STREX가 있으므로 `BUILTIN`.

Zephyr의 `qemu_cortex_m0` 보드로 같은 앱을 빌드해 보려 했지만 이 랩의 Zephyr 워크스페이스에는 nRF51 보드가 요구하는 `hal_nordic` 모듈이 없어 Kconfig 단계에서 실패했다(`NRFX_CLOCK ... has direct dependencies HAS_NRFX && 0`). 그래서 **Zephyr M0 경로는 소스로만 확인했고 실행하지 못했다.** M0 동작 자체는 예제 4의 bare-metal 실행으로 대신 보였다.

FreeRTOS에는 `include/atomic.h`(`Atomic_CompareAndSwap_u32`, `Atomic_Add_u32` …)가 있다. 이름이 "atomic"이지만 머리 주석이 정직하게 말한다.

```text
 * This file implements atomic functions by disabling interrupts globally.
 * Implementations with architecture specific atomic instructions can be
 * provided under each compiler directory.
```

V11.3.2의 구현은 `ATOMIC_ENTER_CRITICAL()` = `portSET_INTERRUPT_MASK_FROM_ISR()`(ARM_CM3에서는 BASEPRI 올리기) 안에서 평범한 RMW를 하는 것이다. **M3에서도 LDREX를 쓰지 않는다.** 그래서 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높은(숫자가 작은) zero-latency ISR과 공유하는 변수에는 FreeRTOS `Atomic_*`가 **보호가 되지 않는다** — BASEPRI가 그 ISR을 막지 못하기 때문이다. 그런 변수는 C11 atomics(LDREX)로 다룬다.

---

## 4. 여러 워드를 찢어지지 않게 — seqlock · 재시도 · 더블 버퍼

> 🔁 이 절은 Bare-Metal BM J2(찢어진 읽기 — tearing)와 겹친다 (주인: 이 노트 — 락 없는 해법 부분. 64비트 타임스탬프 레이스 재현과 IRQ 마스크 해법은 C7 §1.3).

### 4.1 직관 — 게시판의 "작성 중" 표시

모터 컨트롤러의 20 kHz ISR이 전류 setpoint `{id, iq, theta}` 세 워드를 계속 갱신하고, 느린 태스크가 가끔 그것을 읽어 텔레메트리로 보낸다고 하자. 세 워드를 읽는 도중에 ISR이 끼면 "id는 새 값, iq는 옛 값"을 읽는다. 해법 하나는 C7처럼 읽는 동안 인터럽트를 끄는 것이다. 다른 해법은 쓰는 쪽이 **시퀀스 번호(sequence counter)**를 "쓰기 전에 하나, 쓰고 나서 하나" 올리고, 읽는 쪽이 **읽기 전후의 번호가 같고 짝수일 때만** 그 값을 믿는 것이다 — **seqlock**. 쓰는 쪽은 절대 기다리지 않고, 읽는 쪽만 가끔 다시 읽는다. Linux 커널의 seqlock(EL F4 §12)과 같은 구조이고, C7 §1.3 ④의 hi-lo-hi 재시도는 "hi 워드 자체를 시퀀스 번호로 쓴" 특수한 경우다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">seqlock — 쓰는 쪽(ISR)은 seq를 홀수로 올렸다 짝수로 내리고, 읽는 쪽은 앞뒤 seq를 비교한다</text>
<text x="10" y="62" font-size="12">ISR (쓰기)</text>
<text x="10" y="122" font-size="12">1차 읽기</text>
<text x="10" y="202" font-size="12">2차 읽기</text>
<rect x="250" y="42" width="76" height="32" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="288" y="62.2" font-size="12" text-anchor="middle">seq → 5</text>
<rect x="330" y="42" width="100" height="32" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="380" y="62.2" font-size="12" text-anchor="middle">a·b·c 쓰기</text>
<rect x="434" y="42" width="76" height="32" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="472" y="62.2" font-size="12" text-anchor="middle">seq → 6</text>
<rect x="85" y="102" width="80" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="125" y="122.2" font-size="12" text-anchor="middle">s1 = 4</text>
<rect x="170" y="102" width="80" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="210" y="122.2" font-size="12" text-anchor="middle">a 읽기</text>
<rect x="515" y="102" width="78" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="554" y="122.2" font-size="12" text-anchor="middle">b·c 읽기</text>
<rect x="596" y="102" width="78" height="32" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="635" y="122.2" font-size="12" text-anchor="middle">s2 = 6</text>
<line x1="252" y1="118" x2="512" y2="118" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="380" y="148" font-size="12" text-anchor="middle">선점당함 (ISR 실행 중)</text>
<text x="672" y="158" font-size="12" text-anchor="end">s1(4) ≠ s2(6) → 버리고 다시</text>
<rect x="85" y="182" width="80" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="125" y="202.2" font-size="12" text-anchor="middle">s1 = 6</text>
<rect x="170" y="182" width="100" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="220" y="202.2" font-size="12" text-anchor="middle">a·b·c 읽기</text>
<rect x="275" y="182" width="80" height="32" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="315" y="202.2" font-size="12" text-anchor="middle">s2 = 6</text>
<text x="365" y="203" font-size="12">같고 짝수 → 이 값을 쓴다</text>
<text x="10" y="240" font-size="12">단일 코어 + 쓰는 쪽이 ISR이면 읽는 쪽이 홀수 seq를 볼 일은 없다 (ISR은 끝까지 돈다). 반대 방향은 §4.3.</text>
</svg>
```

그림 4 — 태스크가 a를 읽은 뒤 선점당하고 ISR이 seq를 4 → 5 → 6으로 바꾸며 세 워드를 고친다. 태스크는 나머지를 읽고 seq가 6인 것을 보고 버린 뒤, 2차 시도에서 일관된 값을 얻는다.

단일 코어에서 이 구조가 Linux seqlock과 다른 점:

- **쓰는 쪽 락이 없다.** Linux seqlock은 쓰는 쪽끼리 spinlock을 잡는다. ISR이 유일한 쓰는 쪽이면 필요 없다.
- **CPU 배리어가 없다.** 같은 코어라서 컴파일러 배리어(`CBAR()`)만으로 "seq → 데이터 → seq" 순서가 지켜진다. 듀얼코어(M7+M4 공유 RAM)라면 쓰는 쪽에 `dmb`(seq 홀수 뒤 · 짝수 앞), 읽는 쪽에 `dmb`(s1 읽은 뒤 · s2 읽기 전)가 들어간다 — EL F4 §12의 `smp_wmb`/`smp_rmb` 자리와 같다.
- **읽는 쪽이 홀수 seq를 볼 일이 없다.** ISR은 시작하면 끝까지 돈다. 그래도 홀수 검사는 넣는다 — 쓰는 쪽이 태스크로 바뀌거나 코어가 둘이 되는 순간 필요해진다.

### 4.2 예제 5 — 3워드 setpoint: 그냥 읽기 vs seqlock vs IRQ 마스크

무엇을 확인하나: 쓰는 ISR이 4.04 µs마다(실제 20 kHz보다 12배 빠르게 — 충돌을 자주 만들려고) 세 워드를 고칠 때, 읽기 20만 번 중 찢어진 값이 몇 번인지와 읽기 비용.

```c
/* ex4 — ISR publishes a 3-word setpoint; main reads it: naive vs seqlock vs IRQ mask */
#include "d8.h"
#define READS 200000u
struct sp { uint32_t id, iq, theta; };          /* consistent iff iq == ~id && theta == 3*id */
static struct sp g_sp;                           /* plain (non-volatile) payload */
static volatile uint32_t g_seq;                  /* even = stable, odd = writer inside */
static volatile uint32_t g_isr_n;

void TIMER0_IRQHandler(void)                     /* "20 kHz motor ISR", run much faster here */
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    uint32_t n = ++g_isr_n;
    g_seq++;              CBAR();                /* odd: write in progress */
    g_sp.id = n; g_sp.iq = ~n; g_sp.theta = 3u * n;
    CBAR();               g_seq++;               /* even: stable again */
}
static int bad(const struct sp *s) { return s->iq != ~s->id || s->theta != 3u * s->id; }

static void rd_naive(struct sp *o) { o->id = g_sp.id; CBAR(); o->iq = g_sp.iq; CBAR(); o->theta = g_sp.theta; }
static uint32_t rd_seq(struct sp *o)             /* returns number of retries */
{
    uint32_t s1, s2, r = 0;
    for (;;) {
        s1 = g_seq;       CBAR();
        o->id = g_sp.id; o->iq = g_sp.iq; o->theta = g_sp.theta;
        CBAR();           s2 = g_seq;
        if (s1 == s2 && !(s1 & 1u)) return r;
        r++;
    }
}
static void rd_mask(struct sp *o) { uint32_t k = irq_save(); *o = g_sp; irq_restore(k); }

static void run(int m, const char *name)
{
    struct sp v; uint32_t torn = 0, retry = 0, t0, i0 = g_isr_n;
    t0 = CYC();
    for (uint32_t i = 0; i < READS; i++) {
        if (m == 0) rd_naive(&v); else if (m == 1) retry += rd_seq(&v); else rd_mask(&v);
        torn += bad(&v);
    }
    t0 = CYC() - t0;
    board_printf_raw("%s: isr=%u torn=%u retries=%u  %u ns/read\n",
                     name, g_isr_n - i0, torn, retry, t0 * 40u / READS);
}
int main(void)
{
    timer0_start(101, 0x40);                     /* writer every 101 cycles = 4.04 us */
    run(0, "naive   "); run(1, "seqlock "); run(2, "irq mask");
    timer0_stop();                               /* same loops, no writer: pure read cost */
    run(0, "naive    (no ISR)"); run(1, "seqlock  (no ISR)"); run(2, "irq mask (no ISR)");
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D8; O=/tmp/rtosD8; make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=$S/ex4_seqlock.c OUT=$O/ex4 >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/ex4/app.elf 60'
```

```text
naive   : isr=28570 torn=23472 retries=0  577 ns/read
seqlock : isr=75000 torn=0 retries=2679  1515 ns/read
irq mask: isr=36451 torn=0 retries=0  736 ns/read
naive    (no ISR): isr=0 torn=0 retries=0  480 ns/read
seqlock  (no ISR): isr=0 torn=0 retries=0  1248 ns/read
irq mask (no ISR): isr=0 torn=0 retries=0  608 ns/read
[board_exit] code=0
```

출력에서 볼 것:

- 그냥 읽으면 20만 번 중 **23,472번(12%)이 찢어진 값**이다. seqlock과 IRQ 마스크는 0이다.
- seqlock은 2679번 다시 읽었다 — 충돌이 일어났을 때만 비용을 낸다. ISR은 단 한 번도 늦춰지지 않았다(마스크가 없으니까).
- 순수 읽기 비용(아래 세 줄, ISR 없음)은 seqlock 1248 ns > 마스크 608 ns > 그냥 480 ns다. 이 QEMU 숫자에서는 **seqlock이 마스크보다 비싸다**. seq를 두 번 읽고 비교하는 명령이 `cpsid`/`msr` 두 개보다 많기 때문이다(icount는 모든 명령을 같은 32 ns로 센다). 즉 **단일 코어에서 seqlock을 고르는 이유는 읽는 쪽의 속도가 아니라, 쓰는 ISR의 지연에 아무것도 더하지 않는다는 것**이다. 쓰는 ISR이 BASEPRI 위의 zero-latency ISR이라 마스크로는 막을 수조차 없을 때는 seqlock(또는 더블 버퍼)만 답이다.
- ISR 수가 줄마다 다른 것은 각 실행의 길이가 달라서다(같은 4.04 µs 주기).

### 4.3 방향이 반대면 — 태스크가 쓰고 ISR이 읽을 때는 seqlock을 쓰지 않는다

드론 FC에서 자세 제어 태스크가 PID 이득이나 모터 믹서 테이블을 바꾸고, 20 kHz 모터 ISR이 그것을 읽는 경우가 반대 방향이다. 여기서 seqlock을 쓰면 **ISR이 태스크의 쓰기 도중(seq 홀수)에 들어온다** — 그런데 ISR은 태스크가 쓰기를 끝낼 때까지 기다릴 수 없다(ISR이 끝나야 태스크가 돈다). 재시도 루프를 돌면 영원히 돈다. 이때는 **더블 버퍼(double buffer)**로 바꾼다.

```c
/* task writes, ISR reads: two copies + an index the ISR samples once (pattern) */
#include <stdint.h>
struct gains { float kp, ki, kd; };
static struct gains g_buf[2];
static volatile uint32_t g_live;                 /* index the ISR uses: 0 or 1 */

void task_update_gains(const struct gains *n);
void MOTOR_IRQHandler(void);
void task_update_gains(const struct gains *n)    /* only one writer task */
{
    uint32_t spare = g_live ^ 1u;
    g_buf[spare] = *n;                           /* fill the copy the ISR is NOT using */
    __asm volatile("" ::: "memory");             /* (dual-core: dmb) data before index */
    g_live = spare;                              /* one aligned 32-bit store = atomic switch */
}
void MOTOR_IRQHandler(void)
{
    const struct gains *g = &g_buf[g_live];      /* read the index once */
    /* ... use g->kp, g->ki, g->kd ... */
    (void)g;
}
```

(패턴 코드 — `arm-none-eabi-gcc -mcpu=cortex-m3 -O2 -Wall -Wextra -c`로 빌드만 확인했다, 경고 0개.) 조건이 하나 있다. 태스크가 **연속으로 두 번** 바꾸면 두 번째 쓰기가 ISR이 지금 읽고 있는 복사본을 덮을 수 있다(ISR이 첫 번째 교체 직후 들어와 그 복사본을 쓰는 중). 단일 코어에서는 ISR이 끝나야 태스크가 다시 돌기 때문에 이 일이 생기지 않는다. 코어가 둘이면 "ISR이 다 읽었다"는 확인(세 번째 버퍼, 또는 ISR이 올리는 ack 카운터)이 필요하다 — 이것이 Linux RCU가 grace period로 푸는 문제다(EL F4 §11).

### 4.4 64비트 타임스탬프는 무엇으로?

> 🔄 **2026-11-01 보충 (E5)**: hi-lo-hi 재시도는 크리티컬 섹션 안이나 더 높은 우선순위 ISR 안에서 실패한다(E5 실측). 어느 문맥에서든 맞는 읽기는 E5 참고.

C7 §1.3이 세 가지 해법(IRQ 마스크 + pending 보정, hi-lo-hi 재시도)을 이미 쟀다. 정리만 한다.

| 상황 | 추천 | 이유 |
|---|---|---|
| 하드웨어 카운터 + ISR이 올리는 상위 워드 | hi-lo-hi 재시도 (C7 예제 2 ④) | 하드웨어가 계속 변하므로 seq가 따로 필요 없다 |
| ISR이 64비트 값을 통째로 갱신 (예: 마지막 샘플 시각) | seqlock (예제 5와 같은 구조) | 쓰는 쪽이 기다리지 않는다 |
| 태스크가 갱신, ISR이 읽음 | 더블 버퍼 (§4.3) | ISR은 재시도할 수 없다 |
| 짧고 드문 읽기, zero-latency ISR 없음 | IRQ 마스크 (C7 §2) | 가장 단순하다 |
| `_Atomic uint64_t` | 쓰지 않는다 | M3에서도 `__atomic_load_8` 호출 → 링크 에러 또는 락 |

---

## 5. SPSC 링버퍼 — 설계: 소유권 둘, 순서 둘

> 🔁 이 절은 Bare-Metal BM J3(락 없는 링버퍼 — SPSC 인덱스 설계, 2의 거듭제곱 크기, 가득 참/빔 판정, 배리어)와 겹친다 (주인: 이 노트).

### 5.1 직관 — 컨베이어 벨트와 두 사람

UART RX ISR(생산자)은 벨트 끝에 상자를 올리고 "올린 개수" 카운터 `head`를 올린다. 태스크(소비자)는 앞에서 상자를 내리고 "내린 개수" 카운터 `tail`을 올린다. **각 카운터는 한 사람만 쓴다.** 다른 사람의 카운터는 읽기만 한다. 그래서 둘이 동시에 일해도 같은 변수를 동시에 고치는 일(RMW 레이스)이 원천적으로 없다 — **LDREX도, 인터럽트 마스크도 필요 없는 이유**가 이것이다. SPSC(single-producer single-consumer)는 "쓰는 사람 하나, 읽는 사람 하나"라는 **사용 규칙**이지 자료 구조의 마법이 아니다. 생산자가 둘이 되는 순간(UART ISR과 태스크가 같은 TX 링에 넣는다) `head`를 두 명이 고치게 되어 깨진다.

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">SPSC 링 (N = 16) — 인덱스는 계속 증가하고, 슬롯은 &amp; (N − 1)로 고른다</text>
<rect x="52" y="82" width="36" height="36" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="70" y="134" font-size="12" text-anchor="middle">0</text>
<rect x="88" y="82" width="36" height="36" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="106" y="134" font-size="12" text-anchor="middle">1</text>
<rect x="124" y="82" width="36" height="36" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="142" y="134" font-size="12" text-anchor="middle">2</text>
<rect x="160" y="82" width="36" height="36" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="178" y="134" font-size="12" text-anchor="middle">3</text>
<rect x="196" y="82" width="36" height="36" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="214" y="134" font-size="12" text-anchor="middle">4</text>
<rect x="232" y="82" width="36" height="36" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="250" y="134" font-size="12" text-anchor="middle">5</text>
<rect x="268" y="82" width="36" height="36" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="286" y="134" font-size="12" text-anchor="middle">6</text>
<rect x="304" y="82" width="36" height="36" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="322" y="134" font-size="12" text-anchor="middle">7</text>
<rect x="340" y="82" width="36" height="36" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="358" y="134" font-size="12" text-anchor="middle">8</text>
<rect x="376" y="82" width="36" height="36" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="394" y="134" font-size="12" text-anchor="middle">9</text>
<rect x="412" y="82" width="36" height="36" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="430" y="134" font-size="12" text-anchor="middle">10</text>
<rect x="448" y="82" width="36" height="36" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="466" y="134" font-size="12" text-anchor="middle">11</text>
<rect x="484" y="82" width="36" height="36" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="502" y="134" font-size="12" text-anchor="middle">12</text>
<rect x="520" y="82" width="36" height="36" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="538" y="134" font-size="12" text-anchor="middle">13</text>
<rect x="556" y="82" width="36" height="36" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="574" y="134" font-size="12" text-anchor="middle">14</text>
<rect x="592" y="82" width="36" height="36" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="610" y="134" font-size="12" text-anchor="middle">15</text>
<line x1="178" y1="172" x2="178" y2="140" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="126" y="188" font-size="12" text-anchor="middle">tail = 19 (19 &amp; 15 = 3)</text>
<text x="126" y="204" font-size="12" text-anchor="middle">소비자만 쓴다</text>
<line x1="394" y1="52" x2="394" y2="79" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="394" y="44" font-size="12" text-anchor="middle">head = 25 (25 &amp; 15 = 9) — 생산자만 쓴다</text>
<text x="268" y="74" font-size="12" text-anchor="middle">데이터 6개</text>
<rect x="40" y="214" width="620" height="58" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="350" y="239.2" font-size="12" text-anchor="middle">개수 = head − tail = 6 (unsigned 뺄셈이라 2^32에서 넘어가도 맞다)</text>
<text x="350" y="255.2" font-size="12" text-anchor="middle">빔: head == tail · 가득: head − tail == N · N개 슬롯을 전부 쓴다</text>
</svg>
```

그림 5 — 크기 16짜리 링. `head`와 `tail`은 슬롯 번호가 아니라 **지금까지의 누적 개수**다. 슬롯은 `인덱스 & 15`로 고른다. 개수는 뺄셈 한 번이다.

### 5.2 설계 결정 네 가지

**① 크기는 2의 거듭제곱.** `idx % N` 대신 `idx & (N − 1)`을 쓸 수 있다. Cortex-M3에서 `%`는 `udiv` + `mls`(2~12사이클대 나눗셈)지만 `&`는 1사이클이다. M0에는 나눗셈 명령 자체가 없어서 `%`가 라이브러리 호출(`__aeabi_uidivmod`)이 된다. 더 중요한 이유는 ②다.

**② 인덱스는 free-running(감싸지 않고 계속 증가).** `head`, `tail`을 0..N−1로 감싸지 않고 `uint32_t`로 그냥 올린다. 그러면

- 개수 = `head − tail` (unsigned 뺄셈은 2^32에서 넘어가도 맞다 — 2^32가 N의 배수이기 때문이고, 이것이 N이 2의 거듭제곱이어야 하는 진짜 이유다)
- 빔 = `head == tail`, 가득 = `head − tail == N`
- **N개 슬롯을 전부 쓴다.**

감싸는 인덱스(0..N−1)를 쓰면 "가득"과 "빔"이 둘 다 `head == tail`이 되어 구분할 수 없다. 그래서 고전 구현은 **한 칸을 비워 두고**(가득 = `(head + 1) % N == tail`) N−1개만 쓴다. 둘 다 맞는 설계다. 화이트보드에서는 둘 중 하나를 골라 **왜 그렇게 판정하는지**를 말하면 된다.

**③ 누가 무엇을 쓰나 — 소유권 표.**

| 필드 | 생산자 (RX ISR) | 소비자 (태스크) |
|---|---|---|
| `head` | 읽고 **쓴다** (자기 것 — relaxed로 읽어도 된다) | 읽기만 (acquire) |
| `tail` | 읽기만 (acquire) | 읽고 **쓴다** (자기 것) |
| `buf[head & M]` | 쓴다 (head를 올리기 **전**) | 손대지 않는다 |
| `buf[tail & M]` | 손대지 않는다 | 읽는다 (tail을 올리기 **전**) |

**④ 순서 — 데이터 다음에 인덱스.** 생산자는 "슬롯에 쓰기 → head 올리기", 소비자는 "head 읽기 → 슬롯 읽기 → tail 올리기". 이 순서가 바뀌면 소비자가 아직 안 쓴 슬롯을 읽거나(head가 먼저 보임), 생산자가 아직 안 읽은 슬롯을 덮어쓴다(tail이 먼저 보임). 이 순서를 지키는 데 무엇이 필요한지가 §5.4다.

### 5.3 코드 — `spsc.h`

무엇을 확인하나: 위 네 결정을 그대로 옮긴 50줄짜리 헤더. 순서를 지키는 방법을 `ORDER` 매크로로 바꿔 끼울 수 있게 했다 — 0은 C11 acquire/release(정답), 1은 `volatile`만(오답), 2는 `volatile` + 컴파일러 배리어(1코어에서만 정답), 3은 `volatile` + DMB(정답). 같은 헤더를 VM 호스트(arm64)와 QEMU M3에서 쓴다.

```c
/* spsc.h — lock-free single-producer/single-consumer ring (power-of-two, free-running indices) */
#ifndef SPSC_H
#define SPSC_H
#include <stdbool.h>
#include <stdint.h>
#ifndef SPSC_N
#define SPSC_N 256u                          /* capacity: must be a power of two */
#endif
#ifndef SPSC_T
#define SPSC_T uint8_t
#endif
#ifndef SPSC_ALIGN
#define SPSC_ALIGN 4                         /* host stress test uses 64 (own cache line) */
#endif
_Static_assert((SPSC_N & (SPSC_N - 1u)) == 0, "SPSC_N must be a power of two");
#ifndef ORDER
#define ORDER 0
#endif
/* ORDER 0: C11 acquire/release   1: volatile only   2: volatile + compiler barrier   3: volatile + DMB */
#if ORDER == 0
#define LD_OTHER(p)     __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define ST_MINE(p, v)   __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#else
#if ORDER == 1
#define BAR()           do { } while (0)
#elif ORDER == 2
#define BAR()           __asm volatile("" ::: "memory")
#else
#define BAR()           __atomic_thread_fence(__ATOMIC_SEQ_CST)     /* dmb ish */
#endif
#define LD_OTHER(p)     ({ uint32_t v_ = *(volatile uint32_t *)(p); BAR(); v_; })
#define ST_MINE(p, v)   do { BAR(); *(volatile uint32_t *)(p) = (v); } while (0)
#endif
typedef struct {
    _Alignas(SPSC_ALIGN) uint32_t head;      /* next slot to write — ONLY the producer stores it */
    _Alignas(SPSC_ALIGN) uint32_t tail;      /* next slot to read  — ONLY the consumer stores it */
    _Alignas(SPSC_ALIGN) SPSC_T buf[SPSC_N];
} spsc_t;

static inline bool spsc_put(spsc_t *r, SPSC_T v)        /* producer side (e.g. UART RX ISR) */
{
    uint32_t h = __atomic_load_n(&r->head, __ATOMIC_RELAXED);   /* my own index */
    uint32_t t = LD_OTHER(&r->tail);                             /* consumer's index */
    if (h - t == SPSC_N) return false;                           /* full */
    r->buf[h & (SPSC_N - 1u)] = v;                               /* 1. write the data ... */
    ST_MINE(&r->head, h + 1u);                                   /* 2. ... then publish it */
    return true;
}
static inline bool spsc_get(spsc_t *r, SPSC_T *v)       /* consumer side (e.g. task) */
{
    uint32_t t = __atomic_load_n(&r->tail, __ATOMIC_RELAXED);
    uint32_t h = LD_OTHER(&r->head);
    if (h == t) return false;                                    /* empty */
    *v = r->buf[t & (SPSC_N - 1u)];                              /* 1. read the data ... */
    ST_MINE(&r->tail, t + 1u);                                   /* 2. ... then free the slot */
    return true;
}
static inline uint32_t spsc_count(const spsc_t *r)
{
    return __atomic_load_n(&r->head, __ATOMIC_ACQUIRE) - __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE);
}
#endif
```

읽을 때 볼 곳:

- `spsc_put()`의 `h - t == SPSC_N`: 가득 판정. `spsc_get()`의 `h == t`: 빔 판정. 둘 다 free-running 인덱스라서 이렇게 단순하다.
- 자기 인덱스는 `__ATOMIC_RELAXED`로 읽는다. 자기만 쓰는 값이니 순서가 필요 없다(C11 규칙상 "atomic으로 쓰는 변수를 일반 접근으로 읽으면" 데이터 레이스가 되므로 relaxed atomic load로 적었다 — 생성 코드는 그냥 `ldr`이다).
- 상대 인덱스 읽기는 acquire, 자기 인덱스 쓰기는 release — 그림 6의 짝이다.
- `_Static_assert`로 2의 거듭제곱을 컴파일 시점에 강제한다. 크기를 100으로 바꾸면 빌드가 실패한다.
- `SPSC_ALIGN`: 호스트 실험에서는 `head` · `tail` · `buf`를 각각 64바이트 캐시 라인에 둔다(두 코어가 한 라인을 핑퐁하지 않게 — false sharing). MCU에서는 4로 둔다(RAM 낭비 방지). Cortex-M7 + DMA처럼 캐시 유지 단위가 32바이트인 칩에서 링을 DMA와 공유한다면 32로 맞춘다(B8).

### 5.4 어디에 무엇이 필요한가 — `volatile`이 모자라는 곳, 컴파일러 배리어로 충분한 곳, DMB가 필요한 곳

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">순서의 짝 — "데이터 다음에 head"를 쓰고, "head 다음에 데이터"를 읽는다</text>
<text x="160" y="44" font-size="12" text-anchor="middle">생산자 (RX ISR)</text>
<text x="520" y="44" font-size="12" text-anchor="middle">소비자 (태스크)</text>
<rect x="20" y="54" width="280" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="160" y="78.2" font-size="12" text-anchor="middle">① buf[head &amp; M] = 데이터</text>
<rect x="20" y="114" width="280" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="160" y="133.2" font-size="12" text-anchor="middle">release 배리어</text>
<text x="160" y="149.2" font-size="12" text-anchor="middle">M3: dmb ish · 1코어: 컴파일러 배리어</text>
<rect x="20" y="180" width="280" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="160" y="204.2" font-size="12" text-anchor="middle">② head = head + 1 (게시)</text>
<rect x="380" y="54" width="280" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="520" y="78.2" font-size="12" text-anchor="middle">① h = head (읽기)</text>
<rect x="380" y="114" width="280" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="520" y="133.2" font-size="12" text-anchor="middle">acquire 배리어</text>
<text x="520" y="149.2" font-size="12" text-anchor="middle">M3: dmb ish · 1코어: 컴파일러 배리어</text>
<rect x="380" y="180" width="280" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="520" y="204.2" font-size="12" text-anchor="middle">② 데이터 = buf[tail &amp; M]</text>
<line x1="160" y1="94" x2="160" y2="112" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="160" y1="160" x2="160" y2="178" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="520" y1="94" x2="520" y2="112" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="520" y1="160" x2="520" y2="178" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="300" y1="200" x2="378" y2="80" stroke="currentColor" marker-end="url(#ah6)"/>
<text x="340" y="246" font-size="12" text-anchor="middle">head의 새 값이 보이면 그 앞에 쓴 데이터도 보인다 — 한쪽만 있으면 소용없다</text>
<text x="340" y="272" font-size="12" text-anchor="middle">tail은 방향만 반대인 같은 짝: 소비자 (읽기 → release → tail++) / 생산자 (acquire tail → 쓰기)</text>
</svg>
```

그림 6 — 생산자의 "데이터 → release → head"와 소비자의 "head → acquire → 데이터"가 짝을 이뤄야 한다. 1코어에서는 두 배리어 자리가 컴파일러 배리어면 되고, 코어가 둘이면 DMB(또는 `lda`/`stl`)가 필요하다.

`ORDER`별로 무엇이 보장되나:

| ORDER | 인덱스 접근 | 배리어 | 1코어 ISR ↔ 태스크 (M3/M4) | 듀얼코어 / arm64 SMP |
|---|---|---|---|---|
| 1 | `volatile` | 없음 | **틀림** — 컴파일러가 `buf` 쓰기를 head 쓰기 뒤로 옮길 수 있다 | **틀림** |
| 2 | `volatile` | 컴파일러 배리어 | **맞음** | **틀림** — CPU가 store/load를 재배치 |
| 3 | `volatile` | DMB (`__atomic_thread_fence`) | 맞음 (DMB는 남는 비용) | 맞음 |
| 0 | C11 acquire/release | 컴파일러가 고름 (M3: DMB, M33: `lda`/`stl`) | 맞음 | 맞음 |

왜 `volatile`만으로는 모자라나 — `volatile`은 "**volatile 접근끼리**의 순서와 횟수"만 지킨다. `buf`는 volatile이 아니므로(`buf` 전체를 volatile로 만들면 memcpy · 루프 최적화가 다 꺼진다) 컴파일러는 `buf[...] = v`를 `head` 쓰기 뒤로 옮겨도 된다. 그런 일이 실제로 일어나는 것을 §7.4 예제 11에서 QEMU로 잡는다. **컴파일러 배리어**(`asm volatile("" ::: "memory")`)는 "모든 메모리 접근이 이 줄을 넘지 못한다"고 말하므로 그 이동을 막는다. 명령은 0개다.

왜 1코어에서는 DMB가 필요 없나 — ISR과 태스크는 **같은 코어**에서 돈다. 코어는 자기 명령 순서대로 메모리를 보고(아니면 단일 스레드 프로그램도 깨진다), ISR은 그 코어가 "앞의 명령을 다 끝낸 시점"에 들어온다. Cortex-M3/M4는 쓰기 버퍼가 있어도 **같은 코어의 뒤따르는 읽기는 버퍼 안의 값을 본다.** 그래서 순서는 "명령 순서 = 컴파일러가 낸 순서"로 정해지고, 컴파일러만 잡으면 된다. 코어가 둘이거나 상대가 DMA · 다른 버스 마스터면 이야기가 달라진다(§7).

### 5.5 예제 6 — `ORDER` 0과 2가 M3와 arm64에서 무엇이 되나

무엇을 확인하나: 같은 `spsc_put`/`spsc_get`을 밖으로 꺼낸 함수로 만들어 생성 코드를 비교한다.

```c
/* spsc_gen.c — out-of-line wrappers so objdump shows what each ORDER becomes */
#include "spsc.h"
bool put(spsc_t *r, uint8_t v) { return spsc_put(r, v); }
bool get(spsc_t *r, uint8_t *v) { return spsc_get(r, v); }
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D8; O=/tmp/rtosD8/gen; for m in 0 2; do echo "===== M3 ORDER=$m"; arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 -O2 -Wall -Wextra -DORDER=$m -c $S/spsc_gen.c -o $O/sg$m.o && arm-none-eabi-objdump -d --no-show-raw-insn $O/sg$m.o | sed -n "/<put>:/,/^$/p"; done; for m in 0 2; do echo "===== arm64 ORDER=$m"; gcc -O2 -Wall -Wextra -DORDER=$m -c $S/spsc_gen.c -o $O/hg$m.o && objdump -d --no-show-raw-insn $O/hg$m.o | sed -n "/<get>:/,/^$/p"; done'
```

```text
===== M3 ORDER=0
00000000 <put>:
   0:	ldr	r3, [r0, #0]
   2:	ldr	r2, [r0, #4]
   4:	dmb	ish
   8:	subs	r2, r3, r2
   a:	cmp.w	r2, #256	@ 0x100
   e:	beq.n	22 <put+0x22>
  10:	uxtb	r2, r3
  12:	add	r2, r0
  14:	strb	r1, [r2, #8]
  16:	adds	r3, #1
  18:	dmb	ish
  1c:	str	r3, [r0, #0]
  1e:	movs	r0, #1
  20:	bx	lr
  22:	movs	r0, #0
  24:	bx	lr
  26:	nop

===== M3 ORDER=2
00000000 <put>:
   0:	mov	r3, r0
   2:	ldr	r2, [r0, #0]
   4:	ldr	r0, [r0, #4]
   6:	subs	r0, r2, r0
   8:	cmp.w	r0, #256	@ 0x100
   c:	beq.n	1c <put+0x1c>
   e:	uxtb	r0, r2
  10:	add	r0, r3
  12:	strb	r1, [r0, #8]
  14:	adds	r2, #1
  16:	movs	r0, #1
  18:	str	r2, [r3, #0]
  1a:	bx	lr
  1c:	movs	r0, #0
  1e:	bx	lr

===== arm64 ORDER=0
0000000000000040 <get>:
  40:	add	x3, x0, #0x4
  44:	ldr	w2, [x3]
  48:	ldar	w4, [x0]
  4c:	cmp	w2, w4
  50:	b.eq	70 <get+0x30>  // b.none
  54:	add	x0, x0, w2, uxtb
  58:	add	w2, w2, #0x1
  5c:	ldrb	w0, [x0, #8]
  60:	strb	w0, [x1]
  64:	stlr	w2, [x3]
  68:	mov	w0, #0x1                   	// #1
  6c:	ret
  70:	mov	w0, #0x0                   	// #0
  74:	ret
===== arm64 ORDER=2
0000000000000040 <get>:
  40:	add	x2, x0, #0x4
  44:	ldr	w2, [x2]
  48:	ldr	w3, [x0]
  4c:	cmp	w2, w3
  50:	b.eq	70 <get+0x30>  // b.none
  54:	add	x3, x0, w2, uxtb
  58:	ldrb	w3, [x3, #8]
  5c:	strb	w3, [x1]
  60:	add	w2, w2, #0x1
  64:	str	w2, [x0, #4]
  68:	mov	w0, #0x1                   	// #1
  6c:	ret
  70:	mov	w0, #0x0                   	// #0
  74:	ret
```

출력에서 볼 것:

- **M3 ORDER=0**: `ldr tail` 뒤 `dmb`(acquire), `strb 데이터` 뒤 · `str head` 앞 `dmb`(release). 크기 256 · `uint8_t`라서 `& 255`가 `uxtb` 한 명령이 됐다 — 2의 거듭제곱의 덤이다.
- **M3 ORDER=2**: `dmb`가 없고, `strb`(데이터) → `str`(head) 순서가 소스 그대로다. 단일 코어 M3에서 이 코드는 맞다. 명령 2개(그리고 DMB의 실제 사이클)를 아낀다.
- **arm64 ORDER=0**: `ldar`(acquire load)로 head를 읽고 `stlr`(release store)로 tail을 쓴다.
- **arm64 ORDER=2**: 평범한 `ldr` → `ldrb` → `str`. 컴파일러는 순서를 지켰지만, **CPU가** head 읽기와 슬롯 읽기를 바꿀 수 있다. 그 결과가 다음 절의 13만 개다.

---

## 6. SPSC 링버퍼 — 검증하고, 실제 ISR로 돌리고, 잰다

### 6.1 예제 7 — 단위 테스트 + 두 스레드 스트레스 (VM 호스트 arm64)

무엇을 확인하나: ① 빔 · 가득(N개 전부 사용) · FIFO 순서 · **2^32에서 인덱스가 넘어갈 때**를 단위 테스트로, ② 생산자 · 소비자 스레드를 서로 다른 코어(vCPU 1, 2)에 묶어 2천만 개를 흘려 `ORDER`별로 틀린 값을 센다. 링을 16칸으로 작게 해서 소비자가 방금 쓴 슬롯을 자주 읽게 했다. arm64는 실제로 load/load · store/store 재배치를 하는 코어다(EL F5 §3.5에서 MP litmus로 확인).

```c
/* ex5 — SPSC ring: unit tests + 2-thread stress on the arm64 VM host (real reordering possible) */
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#define SPSC_N 16u
#define SPSC_T uint32_t
#define SPSC_ALIGN 64
#include "spsc.h"
static spsc_t q;
static uint32_t n_items;

static void pin(int cpu) { cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s); pthread_setaffinity_np(pthread_self(), sizeof s, &s); }
static void *producer(void *a)
{
    (void)a; pin(1);
    for (uint32_t i = 0; i < n_items; ) if (spsc_put(&q, i)) i++;
    return NULL;
}
static void unit_tests(void)
{
    uint32_t v;
    for (uint32_t start = 0; start < 2; start++) {
        q.head = q.tail = start ? 0xFFFFFFF8u : 0;   /* second pass: indices wrap past 2^32 */
        assert(!spsc_get(&q, &v));                   /* empty */
        for (uint32_t i = 0; i < SPSC_N; i++) assert(spsc_put(&q, i));
        assert(!spsc_put(&q, 99) && spsc_count(&q) == SPSC_N);   /* full: all N slots usable */
        for (uint32_t i = 0; i < SPSC_N; i++) assert(spsc_get(&q, &v) && v == i);
        assert(!spsc_get(&q, &v) && spsc_count(&q) == 0);
    }
    q.head = q.tail = 0;
    printf("unit tests passed (empty, full = %u items, FIFO order, index wrap at 2^32)\n", SPSC_N);
}
int main(int argc, char **argv)
{
    n_items = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 0) : 20000000u;
    unit_tests();
    pthread_t p; uint32_t expect = 0, bad = 0, v;
    pin(2);
    pthread_create(&p, NULL, producer, NULL);
    while (expect < n_items) {
        if (!spsc_get(&q, &v)) continue;
        if (v != expect) { bad++; expect = v; }      /* resync on the value we got */
        expect++;
    }
    pthread_join(p, NULL);
    printf("ORDER=%d items=%u wrong_values=%u\n", ORDER, n_items, bad);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D8; O=/tmp/rtosD8/host; mkdir -p $O; for m in 0 1 2 3; do gcc -O2 -std=gnu11 -Wall -Wextra -pthread -DORDER=$m $S/ex5_host.c -o $O/ex5_$m || exit 1; done; $O/ex5_0 1 | head -1; for m in 0 1 2 3; do for t in 1 2 3; do /usr/bin/time -f "%e s" $O/ex5_$m 20000000 2>&1 | tail -2 | tr "\n" " "; echo; done; done'
```

```text
unit tests passed (empty, full = 16 items, FIFO order, index wrap at 2^32)
ORDER=0 items=20000000 wrong_values=0 5.19 s 
ORDER=0 items=20000000 wrong_values=0 3.31 s 
ORDER=0 items=20000000 wrong_values=0 3.21 s 
ORDER=1 items=20000000 wrong_values=151272 0.44 s 
ORDER=1 items=20000000 wrong_values=201771 0.49 s 
ORDER=1 items=20000000 wrong_values=181126 0.37 s 
ORDER=2 items=20000000 wrong_values=152258 0.53 s 
ORDER=2 items=20000000 wrong_values=135678 0.48 s 
ORDER=2 items=20000000 wrong_values=110270 0.44 s 
ORDER=3 items=20000000 wrong_values=0 2.47 s 
ORDER=3 items=20000000 wrong_values=0 1.85 s 
ORDER=3 items=20000000 wrong_values=0 2.10 s 
```

(단위 테스트 줄은 한 번만 보였다 — 모든 실행이 같은 줄을 먼저 찍는다.)

출력에서 볼 것:

- 단위 테스트가 통과했다. 두 번째 패스는 인덱스를 `0xFFFFFFF8`에서 시작해 16개를 넣고 빼는 동안 2^32를 넘어간다 — 개수 계산(`head − tail`)이 넘어감에 상관없이 맞다.
- **ORDER 1(`volatile`만)과 2(+ 컴파일러 배리어)는 2천만 개 중 11만~20만 개(약 0.6~1%)를 틀렸다.** 소비자가 head가 올라간 것은 봤는데 슬롯에서는 **한 바퀴 전의 옛 값**을 읽은 것이다. 컴파일러는 순서를 지켰는데(ORDER 2) CPU가 바꿨다. 단일 코어 Cortex-M에서 정답이던 코드가 **코어가 둘이 되는 순간 틀린다**는 것이 이 숫자다.
- **ORDER 0(acquire/release)과 3(DMB)은 0개.**
- 시간: 틀린 쪽이 빠른 것은 당연하다(배리어 비용이 없다). 이 VM에서는 `ldar`/`stlr`(ORDER 0, 3.2~5.2 s)이 `dmb ish`(ORDER 3, 1.9~2.5 s)보다 느렸는데, 다른 작성자의 빌드가 같이 돌던 시간이라(load average 13.8) 이 차이를 일반화하지 않는다. 값이 0인지 아닌지가 이 실험의 결론이다.

### 6.2 예제 8 — FreeRTOS에서 "UART RX ISR → 태스크", "태스크 → UART TX ISR"

무엇을 확인하나: 같은 `spsc.h`(ORDER 0, 크기 256, `uint8_t`)를 실제 ISR 두 개와 태스크 하나로 돌린다. TIMER0 ISR이 10 µs마다 바이트 하나가 "도착"한 것처럼 RX 링에 넣고(약 1 Mbaud), DUALTIMER ISR이 13.3 µs마다 "송신 레지스터가 비었다"며 TX 링에서 하나를 꺼낸다. 태스크는 RX를 비우며 순서를 검사하고 TX를 채운다. 깨우기는 **링이 비어 있다가 처음 찼을 때만** task notification으로 한다.

```c
/* ex6 — FreeRTOS: "UART RX" ISR -> task and task -> "UART TX" ISR through lock-free SPSC rings */
#include "FreeRTOS.h"
#include "task.h"
#include "d8.h"
#include "spsc.h"                                  /* SPSC_N 256, uint8_t, ORDER from -D */
#define DT_LOAD   REG32(0x40002000u)
#define DT_CTRL   REG32(0x40002008u)
#define DT_INTCLR REG32(0x4000200Cu)
static spsc_t rx, tx;
static TaskHandle_t s_task;
static volatile uint32_t rx_n, rx_drop, tx_n, tx_bad, tx_idle;

void TIMER0_IRQHandler(void)                       /* RX: one byte "arrives" per IRQ */
{
    static uint8_t next;
    TIMER_INTCLR(TIMER0_BASE) = 1;
    BaseType_t woke = pdFALSE;
    bool was_empty = (spsc_count(&rx) == 0);
    if (spsc_put(&rx, next)) { next++; rx_n++; } else rx_drop++;
    if (was_empty) vTaskNotifyGiveFromISR(s_task, &woke);   /* wake only on empty -> non-empty */
    portYIELD_FROM_ISR(woke);
}
void DUALTIMER_IRQHandler(void)                    /* TX: "shift register empty", take one byte */
{
    static uint8_t want;
    uint8_t b;
    DT_INTCLR = 1;
    if (!spsc_get(&tx, &b)) { tx_idle++; return; }
    if (b != want) tx_bad++;
    want = (uint8_t)(b + 1u); tx_n++;
}
static void worker(void *arg)
{
    (void)arg;
    uint8_t want = 0, b, out = 0; uint32_t rx_bad = 0, got = 0;
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(200);
    while (xTaskGetTickCount() < end) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2));
        while (spsc_get(&rx, &b)) { if (b != want) rx_bad++; want = (uint8_t)(b + 1u); got++; }
        while (spsc_put(&tx, out)) out++;              /* refill TX ring */
    }
    TIMER_CTRL(TIMER0_BASE) = 0; DT_CTRL = 0;
    board_printf_raw("RX: isr put %u, task got %u, out-of-order %u, dropped %u\n", rx_n, got, rx_bad, rx_drop);
    board_printf_raw("TX: isr sent %u, out-of-order %u, isr found ring empty %u\n", tx_n, tx_bad, tx_idle);
    board_exit(0);
}
int main(void)
{
    xTaskCreate(worker, "worker", 512, NULL, 3, &s_task);
    timer0_start(250, 0x40);                       /* RX byte every 10 us (~1 Mbaud) */
    DT_LOAD = 333 - 1; NVIC_IPR(10) = 0x40; NVIC_ISER0 = 1u << 10;
    DT_CTRL = 0x80u | 0x40u | 0x20u | 0x02u;       /* TX IRQ every 13.3 us */
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D8; O=/tmp/rtosD8; make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/ex6_uart.c OUT=$O/ex6 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/ex6/app.elf 30'
```

```text
   text	   data	    bss	    dec	    hex	filename
  12900	    100	 135960	 148960	  245e0	/tmp/rtosD8/ex6/app.elf
RX: isr put 20003, task got 20003, out-of-order 0, dropped 0
TX: isr sent 15015, out-of-order 0, isr found ring empty 2
[board_exit] code=0
```

출력에서 볼 것:

- RX 200 ms 동안 20,003바이트(100 kB/s) — 하나도 잃지 않고(dropped 0) 순서도 맞다(out-of-order 0). TX 15,015바이트도 순서가 맞다. **두 방향 모두 인터럽트를 한 번도 끄지 않았다.**
- TX ISR이 링을 빈 채로 본 것은 2번(시작 직후 태스크가 아직 채우기 전)뿐이다.
- 깨우기 정책: ISR은 "넣기 전에 비어 있었으면" 알린다. 태스크는 알림을 받으면 **빌 때까지** 비운다. 태스크가 비우고 잠들기 직전에 ISR이 넣는 경쟁이 있어도 알림이 래치되므로(`ulTaskNotifyTake`가 바로 돌아온다) 잃지 않는다. 바이트마다 알리면 ISR마다 커널 호출(크리티컬 섹션 포함 — §8)을 하게 되어 락 없는 링의 장점이 사라진다. 실무에서는 "비었다가 찼을 때" 또는 "절반 찼을 때 · 줄바꿈이 왔을 때"처럼 **알리는 횟수를 줄이는 정책**을 함께 설계한다.
- 이 실험은 ORDER 2(컴파일러 배리어만)로 바꿔도 같은 결과가 나와야 한다 — 단일 코어이기 때문이다. 직접 해보기 2번 문제다.

### 6.3 예제 9 — 비용: 락 없는 링 vs 크리티컬 섹션 링 vs 뮤텍스 vs FreeRTOS 큐 · stream buffer

무엇을 확인하나: 경쟁 없는 상태(태스크 하나가 64바이트 넣고 64바이트 빼기를 64번)에서 바이트 하나를 넣고 빼는 경로 길이. `ORDER=0`과 `ORDER=2`로 두 번 빌드했다(크리티컬 섹션 · 커널 객체 행은 ORDER=2 빌드에서만 찍는다 — 잠금 안에서는 링 자체의 배리어가 필요 없기 때문이다).

```c
/* ex8 — cost per byte (put + get) of 8 ways to pass bytes; FreeRTOS on QEMU M3, ICOUNT */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "d8.h"
#include "spsc.h"
#define R 64u                                       /* rounds of 64 puts then 64 gets */
static spsc_t q;
static volatile uint32_t sink;
#define BENCH(name, PUT, GET) do {                                                   \
        uint32_t t0 = CYC(), sum = 0; uint8_t b = 0, c[64];                          \
        for (uint32_t r = 0; r < R; r++) {                                           \
            for (uint32_t i = 0; i < 64; i++) { b = (uint8_t)i; PUT; }               \
            for (uint32_t i = 0; i < 64; i++) { GET; sum += b; }                     \
        }                                                                            \
        t0 = CYC() - t0; sink = sum; (void)c;                                        \
        t0 = t0 * 400u / (R * 64u);                  /* 0.1 ns units */                \
        board_printf_raw("%s: %u.%u ns/byte (check %u)\n", name, t0 / 10u, t0 % 10u, sum); \
    } while (0)
static void bench(void *arg)
{
    (void)arg;
    BENCH("loop only (baseline)", sink = b, (void)0);
    BENCH("spsc lock-free", spsc_put(&q, b), spsc_get(&q, &b));
#if ORDER == 2
    SemaphoreHandle_t m = xSemaphoreCreateMutex();
    QueueHandle_t qu = xQueueCreate(64, 1);
    StreamBufferHandle_t sb = xStreamBufferCreate(128, 1);
    BENCH("PRIMASK ring", { uint32_t k = irq_save(); spsc_put(&q, b); irq_restore(k); },
                          { uint32_t k = irq_save(); spsc_get(&q, &b); irq_restore(k); });
    BENCH("taskENTER_CRITICAL ring", { taskENTER_CRITICAL(); spsc_put(&q, b); taskEXIT_CRITICAL(); },
                                     { taskENTER_CRITICAL(); spsc_get(&q, &b); taskEXIT_CRITICAL(); });
    BENCH("mutex ring", { xSemaphoreTake(m, portMAX_DELAY); spsc_put(&q, b); xSemaphoreGive(m); },
                        { xSemaphoreTake(m, portMAX_DELAY); spsc_get(&q, &b); xSemaphoreGive(m); });
    BENCH("xQueueSend/Receive", xQueueSend(qu, &b, 0), xQueueReceive(qu, &b, 0));
    BENCH("stream buffer 1 B", xStreamBufferSend(sb, &b, 1, 0), xStreamBufferReceive(sb, &b, 1, 0));
    BENCH("stream buffer 64 B", { c[i] = b; if (i == 63) xStreamBufferSend(sb, c, 64, 0); },
                                { if (i == 0) xStreamBufferReceive(sb, c, 64, 0); b = c[i]; });
#endif
    board_exit(0);
}
int main(void)
{
    xTaskCreate(bench, "bench", 512, NULL, 3, NULL);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D8; O=/tmp/rtosD8; for m in 0 2; do make -s -B -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/ex8_bench.c OUT=$O/ex8_$m EXTRA_CFLAGS=-DORDER=$m >/dev/null && echo "== ORDER=$m" && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/ex8_$m/app.elf 30; done'
```

```text
== ORDER=0
loop only (baseline): 129.5 ns/byte (check 258048)
spsc lock-free: 931.4 ns/byte (check 129024)
[board_exit] code=0
== ORDER=2
loop only (baseline): 161.5 ns/byte (check 258048)
spsc lock-free: 835.9 ns/byte (check 129024)
PRIMASK ring: 1027.9 ns/byte (check 129024)
taskENTER_CRITICAL ring: 2630.4 ns/byte (check 129024)
mutex ring: 12821.2 ns/byte (check 129024)
xQueueSend/Receive: 7020.9 ns/byte (check 129024)
stream buffer 1 B: 12533.1 ns/byte (check 129024)
stream buffer 64 B: 540.4 ns/byte (check 129024)
[board_exit] code=0
```

(`check`는 꺼낸 바이트의 합으로, 모든 방법이 같은 데이터를 돌려줬는지 확인한다. 기준선은 GET이 없어서 값이 다르다.)

Python으로 막대 좌표를 계산해 그렸다(`mk_svgs.py`의 SVG8 — 값은 위 출력 그대로).

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">바이트 하나를 넣고 빼는 비용 (ns/byte, put + get, 루프 포함) — QEMU M3 icount</text>
<text x="207" y="59" font-size="12" text-anchor="end">stream buffer 64 B 덩어리</text>
<rect x="215" y="47" width="15.7963" height="18" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<text x="236.796" y="60" font-size="12">540.4</text>
<text x="207" y="89" font-size="12" text-anchor="end">SPSC (컴파일러 배리어)</text>
<rect x="215" y="77" width="24.434" height="18" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<text x="245.434" y="90" font-size="12">835.9</text>
<text x="207" y="119" font-size="12" text-anchor="end">SPSC (C11 acq/rel = DMB)</text>
<rect x="215" y="107" width="27.2255" height="18" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<text x="248.226" y="120" font-size="12">931.4</text>
<text x="207" y="149" font-size="12" text-anchor="end">PRIMASK 링</text>
<rect x="215" y="137" width="30.0463" height="18" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="251.046" y="150" font-size="12">1,027.9</text>
<text x="207" y="179" font-size="12" text-anchor="end">taskENTER_CRITICAL 링</text>
<rect x="215" y="167" width="76.8886" height="18" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="297.889" y="180" font-size="12">2,630.4</text>
<text x="207" y="209" font-size="12" text-anchor="end">xQueueSend / Receive</text>
<rect x="215" y="197" width="205.226" height="18" fill="#888" fill-opacity="0.6" stroke="#888"/>
<text x="426.226" y="210" font-size="12">7,020.9</text>
<text x="207" y="239" font-size="12" text-anchor="end">stream buffer 1 B씩</text>
<rect x="215" y="227" width="366.352" height="18" fill="#888" fill-opacity="0.6" stroke="#888"/>
<text x="587.352" y="240" font-size="12">12,533.1</text>
<text x="207" y="269" font-size="12" text-anchor="end">mutex 링</text>
<rect x="215" y="257" width="374.774" height="18" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="595.774" y="270" font-size="12">12,821.2</text>
<line x1="215" y1="44" x2="215" y2="290" stroke="currentColor"/>
<line x1="215" y1="290" x2="615" y2="290" stroke="currentColor"/>
<line x1="215" y1="290" x2="215" y2="295" stroke="currentColor"/>
<text x="215" y="309" font-size="12" text-anchor="middle">0</text>
<line x1="331.923" y1="290" x2="331.923" y2="295" stroke="currentColor"/>
<text x="331.923" y="309" font-size="12" text-anchor="middle">4,000</text>
<line x1="448.846" y1="290" x2="448.846" y2="295" stroke="currentColor"/>
<text x="448.846" y="309" font-size="12" text-anchor="middle">8,000</text>
<line x1="565.769" y1="290" x2="565.769" y2="295" stroke="currentColor"/>
<text x="565.769" y="309" font-size="12" text-anchor="middle">12,000</text>
<text x="415" y="328" font-size="12" text-anchor="middle">ns per byte (작을수록 좋다). 기준선(빈 루프) 130~160 ns 포함</text>
</svg>
```

그림 7 — 바이트당 put + get 비용(QEMU M3 icount, 빈 루프 130~160 ns 포함). 초록 = 락 없음, 주황 = 인터럽트 마스크, 회색 = FreeRTOS 커널 객체, 빨강 = 뮤텍스.

출력에서 볼 것:

- **락 없는 SPSC 836 ns ≈ PRIMASK 링 1028 ns.** 단일 코어에서 "락 없음"의 장점은 **평균 속도가 아니다.** PRIMASK 링도 거의 같은 속도다. 장점은 ① 다른 ISR의 지연에 아무것도 더하지 않는다(PRIMASK는 매 바이트마다 모든 IRQ를 수십 ns 막는다 — C7 §6에서 L이 그대로 지연에 더해지는 것을 쟀다), ② BASEPRI 위의 zero-latency ISR에서도 쓸 수 있다(FreeRTOS API는 못 쓴다), ③ 듀얼코어로 그대로 옮겨 간다(ORDER 0일 때).
- **C11 acquire/release(ORDER 0)는 컴파일러 배리어(ORDER 2)보다 바이트당 95.5 ns 비싸다.** 두 빌드의 빈 루프 기준선(129.5 vs 161.5 ns)을 각각 빼면 801.9 − 674.4 = 127.5 ns = 정확히 명령 4개(32 ns × 4) — put과 get의 `dmb` 네 개다. 실칩에서 DMB는 쓰기 버퍼가 빌 때까지 기다리므로 명령 1개보다 비쌀 수 있다(QEMU는 1명령으로 센다 — 실칩 사이클은 확인 못 함).
- `taskENTER_CRITICAL` 링(2630 ns)은 PRIMASK 링의 2.6배다. 중첩 카운터 · `configASSERT` 검사 · BASEPRI `msr` + `isb`/`dsb` 때문이다(C7 §3.3).
- **커널 객체는 한 자릿수 µs다**: `xQueueSend/Receive` 7.0 µs, 뮤텍스 12.8 µs, stream buffer를 1바이트씩 쓰면 12.5 µs. 1 Mbaud UART(바이트당 10 µs)를 바이트마다 큐로 넘기면 CPU의 70%가 큐에 들어간다(QEMU 숫자로 계산한 대략치).
- **stream buffer를 64바이트 덩어리로 쓰면 540 ns/byte로 가장 빠르다.** 함수 호출 · 크리티컬 섹션 비용이 64바이트에 나뉘고 복사는 memcpy이기 때문이다. 교훈: **바이트마다 넘기지 말고 묶어서 넘긴다.** ISR에서 SPSC 링에 바이트로 쌓고, 태스크는 한 번 깨어나서 덩어리로 처리하는 것이 같은 이치다.

### 6.4 화이트보드에서 — "ISR과 태스크 사이의 링버퍼를 짜 보라"

Anduril C 문제은행의 SPSC 링버퍼 문제가 이 형태다. 면접관이 보는 체크리스트:

1. **크기와 인덱스**: 2의 거듭제곱 + free-running(N개 사용) 또는 감싸는 인덱스 + 한 칸 비우기(N−1개). 고른 이유를 말한다.
2. **가득/빔 판정식**을 정확히 쓴다. `count` 변수를 따로 두면 그 변수를 양쪽이 고치게 되어(RMW) **SPSC의 장점이 사라진다** — 가장 흔한 감점이다.
3. **소유권**: "head는 생산자만, tail은 소비자만 쓴다"를 말로 한다.
4. **순서**: "데이터를 쓰고 나서 head를 올린다"와 "그 사이에 무엇이 필요하냐 — 1코어 Cortex-M이면 컴파일러 배리어, 코어가 둘이면 release/acquire 또는 DMB". `volatile`만이면 왜 모자라는지(§5.4).
5. **가득 찼을 때의 정책**: 새 바이트를 버린다(RX의 보통 선택 — 드롭 카운터를 올린다) vs 가장 오래된 것을 덮는다(그러면 생산자가 tail을 고치게 되어 SPSC가 깨진다 — 덮어쓰기 링은 다른 설계다).
6. **알림**: 소비자를 언제 깨우나(§6.2).
7. 꼬리 질문 대비: "생산자가 둘이면?"(LDREX 기반 MPSC 또는 크리티컬 섹션), "DMA가 생산자면?"(인덱스 대신 DMA의 NDTR/카운터 레지스터를 head로 읽는다 — §9.1), "캐시가 있으면?"(M7: 링을 non-cacheable로 두거나 clean/invalidate — B8).

---

## 7. DMB / DSB / ISB — Cortex-M에서 정말 필요한 곳

> 🔁 이 절은 Bare-Metal BM J5(배리어가 필요한 순간)와 BM B6(메모리 순서 — 쓰기 버퍼 · write posting, "레지스터 쓰기가 늦게 반영")과 겹친다 (주인: 이 노트. Normal / Device 메모리 타입의 정의는 Bare-Metal B6에 남긴다).

### 7.1 세 명령의 차이 — 그리고 단일 코어 M3/M4에서 "보통은 필요 없다"는 말의 정확한 뜻

| 명령 | 이름 | 기다리는 것 | 한 줄 |
|---|---|---|---|
| `DMB` | Data Memory Barrier | 앞의 메모리 접근이 **다른 관찰자에게 보이는 순서** | "앞의 것이 뒤의 것보다 먼저 보이게" — 완료는 기다리지 않는다 |
| `DSB` | Data Synchronization Barrier | 앞의 메모리 접근 **완료** (쓰기 버퍼 비우기 포함) | "다 끝날 때까지 다음 명령을 실행하지 않는다" |
| `ISB` | Instruction Synchronization Barrier | 파이프라인 비우기 | "이미 가져온 다음 명령을 버리고 새 상태(CONTROL · MPU · 벡터 · 코드)로 다시 가져온다" |

Linux의 `smp_mb()`/`mb()`/`dma_wmb()`가 arm64에서 각각 무엇이 되는지는 EL F5 §2.3에서 봤다. Cortex-M에서 달라지는 점은 이렇다.

- **범위 옵션이 없다.** Armv7-M은 `SY`만 정의한다(`ish` · `osh`는 예약 — §3.2). "공유 도메인"을 고를 일이 없다.
- **M0/M3/M4는 파이프라인이 짧고 순서대로(in-order) 실행한다.** 자기 코어 관점의 일반 메모리 순서는 프로그램 순서와 같고(§5.4), 같은 주변장치에 대한 Device 접근끼리는 아키텍처가 순서를 지킨다. Arm의 Application Note 321("ARM Cortex-M Programming Guide to Memory Barrier Instructions")이 이 주제를 정리한 문서인데, 요지를 이 노트의 말로 옮기면 이렇다: 현재 M0/M3/M4 구현에서는 많은 경우 배리어를 빼도 동작하지만, **아키텍처가 요구하는 곳에는 넣어라** — 다른 구현(M7처럼 버스 · 쓰기 버퍼가 복잡한 코어)으로 옮겨도 맞도록.
- **"보통 필요 없다"의 예외 = 코어 밖의 관찰자가 있을 때와, 코어 자신의 상태가 바뀔 때.** 코어 밖의 관찰자: DMA, 다른 코어, 주변장치(쓰기가 버스 너머 도착해야 하는 것). 코어 자신의 상태: VTOR, CONTROL, MPU, 코드 메모리, 저전력 진입. 아래 그림의 7가지가 그 목록이다.

**쓰기 버퍼와 write posting — "레지스터 쓰기가 늦게 반영"되는 이유.** Cortex-M3/M4는 STR을 실행하면 데이터를 **쓰기 버퍼(write buffer)**에 넣고 곧바로 다음 명령으로 간다. 버스가 그 쓰기를 실제 주변장치까지 나르는 동안 CPU는 이미 몇 명령을 더 실행했다. 주변장치가 느린 APB 버스 너머에 있으면 그 사이에 AHB-APB 브리지가 쓰기를 한 번 더 붙잡아 두기도 한다(**posted write** — "받았다"고 먼저 응답하고 나중에 전달). Don이 PCIe에서 본 posted write(EL F5 §5.4)와 같은 현상이 칩 안의 버스에서도 일어난다. 그래서 "인터럽트 플래그를 지웠는데 ISR이 한 번 더 들어온다", "클록을 켰는데 바로 다음 레지스터 접근이 무시된다"가 생긴다.

```svg
<svg viewBox="0 0 680 386" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">Cortex-M에서 배리어가 실제로 필요한 곳 (단일 코어 M3/M4 기준)</text>
<text x="175" y="42" font-size="12" text-anchor="middle">상황</text>
<text x="430" y="42" font-size="12" text-anchor="middle">넣을 것</text>
<text x="590" y="42" font-size="12" text-anchor="middle">이유 · 덧붙임</text>
<rect x="10" y="52" width="330" height="34" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="175" y="73.2" font-size="12" text-anchor="middle">DMA 디스크립터 쓰기 → DMA 시작 레지스터</text>
<rect x="355" y="52" width="150" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="430" y="73.2" font-size="12" text-anchor="middle">DMB</text>
<rect x="515" y="52" width="155" height="34" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="592.5" y="73.2" font-size="12" text-anchor="middle">M7 캐시: clean + DSB</text>
<rect x="10" y="93" width="330" height="34" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="175" y="114.2" font-size="12" text-anchor="middle">ISR 끝에서 주변장치 IRQ 플래그 클리어</text>
<rect x="355" y="93" width="150" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="430" y="114.2" font-size="12" text-anchor="middle">DSB 또는 다시 읽기</text>
<rect x="515" y="93" width="155" height="34" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="592.5" y="114.2" font-size="12" text-anchor="middle">이중 진입 막기</text>
<rect x="10" y="134" width="330" height="34" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="175" y="155.2" font-size="12" text-anchor="middle">VTOR · CONTROL · MPU 설정을 바꾼 뒤</text>
<rect x="355" y="134" width="150" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="430" y="155.2" font-size="12" text-anchor="middle">DSB + ISB</text>
<rect x="515" y="134" width="155" height="34" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="592.5" y="155.2" font-size="12" text-anchor="middle">다음 명령부터 새 설정</text>
<rect x="10" y="175" width="330" height="34" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="175" y="196.2" font-size="12" text-anchor="middle">WFI / WFE 직전</text>
<rect x="355" y="175" width="150" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="430" y="196.2" font-size="12" text-anchor="middle">DSB</text>
<rect x="515" y="175" width="155" height="34" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="592.5" y="196.2" font-size="12" text-anchor="middle">쓰기를 끝내고 잔다</text>
<rect x="10" y="216" width="330" height="34" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="175" y="237.2" font-size="12" text-anchor="middle">다른 코어와 공유 메모리 (AMP 메일박스)</text>
<rect x="355" y="216" width="150" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="430" y="237.2" font-size="12" text-anchor="middle">DMB</text>
<rect x="515" y="216" width="155" height="34" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="592.5" y="237.2" font-size="12" text-anchor="middle">+ 캐시 유지 (M7)</text>
<rect x="10" y="257" width="330" height="34" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="175" y="278.2" font-size="12" text-anchor="middle">코드를 RAM에 복사 · 자기 수정 코드</text>
<rect x="355" y="257" width="150" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="430" y="278.2" font-size="12" text-anchor="middle">DSB + ISB</text>
<rect x="515" y="257" width="155" height="34" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="592.5" y="278.2" font-size="12" text-anchor="middle">+ I-cache 무효화</text>
<rect x="10" y="298" width="330" height="34" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="175" y="319.2" font-size="12" text-anchor="middle">NVIC_DisableIRQ 직후 "이제 안 온다"</text>
<rect x="355" y="298" width="150" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="430" y="319.2" font-size="12" text-anchor="middle">DSB + ISB</text>
<rect x="515" y="298" width="155" height="34" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="592.5" y="319.2" font-size="12" text-anchor="middle">CMSIS가 넣어 준다</text>
<rect x="10" y="339" width="330" height="34" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="175" y="360.2" font-size="12" text-anchor="middle">단일 코어 ISR ↔ 태스크 (일반 RAM)</text>
<rect x="355" y="339" width="150" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="430" y="360.2" font-size="12" text-anchor="middle">컴파일러 배리어</text>
<rect x="515" y="339" width="155" height="34" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="592.5" y="360.2" font-size="12" text-anchor="middle">DMB는 이식성 보험</text>
</svg>
```

그림 8 — 단일 코어 M3/M4에서 배리어가 실제로 필요한 곳. 맨 아래 행(1코어 ISR ↔ 태스크, 일반 RAM)만 컴파일러 배리어로 충분하다.

### 7.2 예제 10 — 장면별 코드, 빌드만 함 (QEMU는 쓰기 버퍼 · 버스 지연을 모델링하지 않는다)

무엇을 확인하나: 그림 8의 장면들을 함수 하나씩으로 쓰고, M4 · M7로 빌드해서 배리어가 어디에 들어가는지 본다. **QEMU는 쓰기가 즉시 도착하는 것으로 모델링하므로 배리어를 빼도 QEMU에서는 깨지지 않는다** — 이 예제는 실행하지 않고 빌드 · 디스어셈블만 했다. 레지스터 주소는 설명용으로 지어낸 것이다(SCB 주소만 실제).

```c
/* bar_pat.c — where Cortex-M needs DMB/DSB/ISB (build-only patterns; QEMU can't show the failures) */
#include <stdint.h>
#define REG32(a) (*(volatile uint32_t *)(a))
#define DMB() __asm volatile("dmb" ::: "memory")
#define DSB() __asm volatile("dsb" ::: "memory")
#define ISB() __asm volatile("isb" ::: "memory")
struct desc { uint32_t src, dst, len, ctrl; };          /* DMA descriptor in normal RAM */
#define DMA_DESC_ADDR REG32(0x40020000u)                /* made-up DMA controller registers */
#define DMA_START     REG32(0x40020004u)
#define TIM_SR        REG32(0x40000010u)                /* made-up timer status, write 0 to clear */
#define SCB_VTOR      REG32(0xE000ED08u)
#define SCB_SCR       REG32(0xE000ED10u)

void dma_kick(struct desc *d, uint32_t src, uint32_t dst, uint32_t n)
{
    d->src = src; d->dst = dst; d->len = n; d->ctrl = 1u; /* normal-memory stores */
    DMB();                    /* descriptor visible before the device is told about it */
    DMA_DESC_ADDR = (uint32_t)d;
    DMA_START = 1u;           /* Device -> Device: kept in order by the architecture */
}
void TIM_IRQHandler(void)
{
    TIM_SR = 0;               /* clear the source ...                                  */
    (void)TIM_SR;             /* ... and read it back so the write has reached the timer */
    DSB();                    /* (DSB alone: write left the core; read-back: arrived)   */
}
void relocate_vectors(const uint32_t *table)
{
    SCB_VTOR = (uint32_t)table;
    DSB();                    /* the write is done ...                    */
    ISB();                    /* ... and the next instruction sees it     */
}
void set_unprivileged_thread(void)
{
    uint32_t c;
    __asm volatile("mrs %0, control" : "=r"(c));
    __asm volatile("msr control, %0" : : "r"(c | 1u) : "memory");
    ISB();                    /* instructions after MSR CONTROL run with the new mode */
}
void deep_sleep(void)
{
    SCB_SCR |= 1u << 2;       /* SLEEPDEEP */
    DSB();                    /* all stores done before the core stops */
    __asm volatile("wfi");
    ISB();
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D8; O=/tmp/rtosD8/gen; for c in cortex-m4 cortex-m7; do arm-none-eabi-gcc -mthumb -mcpu=$c -O2 -Wall -Wextra -c $S/bar_pat.c -o $O/bp_$c.o && echo "$c: build ok, warnings 0"; done; arm-none-eabi-objdump -d --no-show-raw-insn $O/bp_cortex-m7.o | sed -n "/<dma_kick>:/,/^$/p;/<TIM_IRQHandler>:/,/^$/p"'
```

```text
cortex-m4: build ok, warnings 0
cortex-m7: build ok, warnings 0
00000000 <dma_kick>:
   0:	push	{r4}
   2:	movs	r4, #1
   4:	strd	r1, r2, [r0]
   8:	strd	r3, r4, [r0, #8]
   c:	dmb	sy
  10:	ldr	r3, [pc, #8]	@ (1c <dma_kick+0x1c>)
  12:	str	r0, [r3, #0]
  14:	str	r4, [r3, #4]
  16:	pop	{r4}
  18:	bx	lr
  1a:	nop
  1c:	.word	0x40020000

00000020 <TIM_IRQHandler>:
  20:	mov.w	r3, #1073741824	@ 0x40000000
  24:	movs	r2, #0
  26:	str	r2, [r3, #16]
  28:	ldr	r3, [r3, #16]
  2a:	dsb	sy
  2e:	bx	lr
```

장면별 해설:

1. **DMA 디스크립터 → 시작 레지스터 (DMB)**: 디스크립터는 일반 RAM(Normal), 시작 레지스터는 주변장치(Device)다. 아키텍처는 **Normal 접근과 Device 접근 사이의 순서를 보장하지 않는다.** `dmb`가 없으면 DMA 엔진이 "시작"을 받고 아직 버퍼에 있는 디스크립터를 읽을 수 있다. 출력에서 `strd` 두 개(디스크립터) 뒤, `str`(DMA 레지스터) 앞에 `dmb sy`가 있다. 시작 레지스터 두 개(Device → Device)끼리는 순서가 지켜지므로 그 사이에는 필요 없다. **M7처럼 D-cache가 있으면** 배리어로는 모자라고 디스크립터 영역을 clean(`SCB_CleanDCache_by_Addr`)한 뒤 `dsb`가 필요하다(B8, EL H9). Don의 SSD 펌웨어에서 "디스크립터 쓰고 → DMB → doorbell"과 같은 패턴이다.
2. **ISR 끝에서 주변장치 IRQ 플래그 클리어 (DSB 또는 다시 읽기)**: 플래그를 지우는 쓰기가 쓰기 버퍼 · 브리지에 머문 채로 ISR이 복귀하면, NVIC는 아직 서 있는 IRQ 선을 보고 **같은 ISR에 다시 들어간다**(tail-chaining으로 바로). 증상은 "ISR이 두 번씩 불린다, 두 번째는 할 일이 없다". 처방은 ① 플래그를 ISR **앞부분**에서 지운다, ② 지운 뒤 같은 레지스터를 **다시 읽는다**(그 주변장치까지 도착해야 읽기가 돌아오므로 가장 확실하다), ③ `dsb`(코어 쪽 쓰기 버퍼를 비운다 — 브리지가 posted write를 붙잡으면 이것만으로는 모자랄 수 있다). 출력의 `str` → `ldr`(같은 주소) → `dsb`가 ②+③이다.
3. **VTOR · CONTROL · MPU를 바꾼 뒤 (DSB + ISB)**: 다음 명령이 새 설정으로 실행되어야 한다. VTOR 쓰기 직후 인터럽트가 오면 옛 벡터 테이블을 쓸 수 있고, `MSR CONTROL`로 스택(PSP)이나 특권 수준을 바꾼 직후의 명령은 이미 파이프라인에 들어와 있을 수 있다. Zephyr `arch/arm/core/cortex_m/prep_c.c`가 `SCB->VTOR = ...; barrier_dsync_fence_full(); barrier_isync_fence_full();`, `arch/arm/core/mpu/arm_mpu.c`가 MPU 활성화 뒤 같은 두 줄을 넣는다(§7.6 예제 13).
4. **WFI 직전 (DSB)**: 잠들기 전에 진행 중인 쓰기(예: 주변장치를 끄는 쓰기, 웨이크업 소스 설정)를 끝낸다. FreeRTOS ARM_CM3 tickless 코드가 `dsb` → `wfi` → `isb` 순서다(§7.6).
5. **다른 코어와 공유 메모리 (DMB)**: 듀얼코어 MCU(STM32H7의 M7+M4, i.MX RT1170, 듀얼 M33)의 공유 SRAM 메일박스는 §6.1의 arm64 실험과 같은 상황이다. SPSC 링은 ORDER 0(acquire/release)으로 쓰고, 상대 코어에 알리는 하드웨어 세마포어 · 메일박스 인터럽트 레지스터에 쓰기 전에는 `dmb`(또는 `dsb`)를 둔다. M7 쪽에 D-cache가 있으면 공유 영역은 MPU로 non-cacheable로 두는 것이 보통이다(P4).
6. **코드를 RAM에 복사 · 자기 수정 코드 (DSB + ISB, 캐시가 있으면 I-cache 무효화)**: 플래시 프로그래밍 루틴을 RAM으로 복사해 실행하거나, 부트로더가 앱을 내려받아 점프할 때다. 복사가 끝났음(DSB)과 파이프라인이 옛 명령을 버렸음(ISB)을 보장한다.
7. **NVIC_DisableIRQ 직후 (DSB + ISB)**: "이 줄 뒤로는 그 IRQ가 안 들어온다"를 원하면 필요하다. CMSIS가 이미 넣어 준다 — `core_cm3.h`의 `__NVIC_DisableIRQ()`는 `NVIC->ICER[...] = ...; __DSB(); __ISB();`다.

### 7.3 1코어 ISR ↔ 태스크에 DMB를 넣어야 하나?

규칙으로 정리한다.

- **필요한 것은 컴파일러 배리어다**(또는 `volatile` 접근끼리의 순서, 또는 C11 atomic). §5.4의 근거.
- **C11 acquire/release를 쓰면 DMB가 따라온다.** 비용은 바이트당 수십~100 ns(§6.3). 고속 경로에서 그것이 아까우면 ORDER 2(컴파일러 배리어)를 쓰고, **"이 링은 단일 코어 전용"이라는 주석과 `_Static_assert`나 빌드 옵션 검사**를 함께 남긴다. 듀얼코어로 옮겨 갈 가능성이 있으면 처음부터 C11로 쓴다.
- **RTOS API 안에서는 이미 처리되어 있다.** `taskENTER_CRITICAL`은 BASEPRI 쓰기 뒤에 `isb`/`dsb`를 넣고, 커널 객체 호출은 함수 호출 경계 자체가 컴파일러 배리어 역할을 한다.

### 7.4 예제 11 — 컴파일러 재배치를 1코어 QEMU에서 잡는다 ("-O2에서만 깨짐")

무엇을 확인하나: CPU 재배치가 없는 단일 코어에서도 **컴파일러가 "데이터 → 플래그"를 "플래그 → 데이터"로 바꾸는지**, 그리고 그것이 실제로 ISR에 틀린 값을 보이게 하는지.

먼저 생성 코드. `g_slot`은 일반 변수, `g_ready`는 `volatile`이다.

```c
/* reorder.c — can the compiler move plain accesses across a volatile one? (look at objdump) */
#include <stdint.h>
struct msg { uint32_t a, b; };
struct msg g_slot;                       /* plain payload */
volatile uint32_t g_ready;               /* volatile flag */
#ifdef CBAR_ON
#define CBAR() __asm volatile("" ::: "memory")
#else
#define CBAR() do { } while (0)
#endif
uint32_t consume(void)                   /* reader: flag first, then payload */
{
    uint32_t r = g_ready;
    CBAR();
    return r ? g_slot.a + g_slot.b : 0;
}
void publish(uint32_t x)                 /* writer: payload first, then flag */
{
    g_slot.a = x;
    g_slot.b = x * 2u;
    CBAR();
    g_ready = 1;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D8; O=/tmp/rtosD8/ro; mkdir -p $O; for d in "" -DCBAR_ON; do echo "== -O2 $d"; arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 -O2 $d -Wall -Wextra -c $S/reorder.c -o $O/r.o && arm-none-eabi-objdump -d --no-show-raw-insn $O/r.o | sed -n "/<publish>:/,/^$/p" | grep -v "^$\|word"; done'
```

```text
== -O2 
00000014 <publish>:
  14:	movs	r2, #1
  16:	ldr	r3, [pc, #12]	@ (24 <publish+0x10>)
  18:	str	r2, [r3, #0]
  1a:	lsl.w	r2, r0, r2
  1e:	strd	r0, r2, [r3, #8]
  22:	bx	lr
== -O2 -DCBAR_ON
00000014 <publish>:
  14:	ldr	r3, [pc, #12]	@ (24 <publish+0x10>)
  16:	lsls	r2, r0, #1
  18:	strd	r0, r2, [r3, #8]
  1c:	movs	r2, #1
  1e:	str	r2, [r3, #0]
  20:	bx	lr
  22:	nop
```

출력에서 볼 것: 배리어가 없으면 GCC 14.2가 **`g_ready = 1`(`str r2, [r3, #0]`)을 페이로드 `strd`보다 먼저** 낸다. 상수 1을 레지스터에 만든 김에 `x * 2`의 시프트 양으로도 재사용하면서 store 순서를 바꿨다 — `volatile` 쓰기와 일반 쓰기 사이의 순서는 C 표준이 보장하지 않으므로 합법이다. `CBAR()`를 넣으면 순서가 소스대로 돌아온다.

이제 이것이 실제로 틀린 값을 보이는지 돌려 본다. 메인이 `publish()`하고, TIMER0 ISR이 `g_ready`를 보고 페이로드를 가져간 뒤 `g_ready = 0`으로 돌려준다. ISR은 "플래그는 새것이라는데 페이로드가 옛것"인 경우를 센다. 메인은 ISR 주기와 위상이 고정되지 않도록 지연을 섞는다.

```c
/* ex7 — compiler reordering on ONE core: main publishes {a,b} then ready=1; timer ISR consumes */
#include "d8.h"
#define N 200000u
struct msg { uint32_t a, b; };
struct msg g_slot;                        /* plain payload */
volatile uint32_t g_ready;                /* 1 = slot full (main sets, ISR clears) */
volatile uint32_t g_stale, g_got;
static uint32_t s_last;                   /* ISR-private: last a it consumed */
#ifdef CBAR_ON
#define PUB_BAR() CBAR()
#else
#define PUB_BAR() do { } while (0)
#endif
__attribute__((noinline)) void publish(uint32_t x)
{
    g_slot.a = x;
    g_slot.b = x * 2u;
    PUB_BAR();                            /* without it GCC -O2 may store g_ready first */
    g_ready = 1;
}
void TIMER0_IRQHandler(void)              /* consumer */
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    if (!g_ready) return;
    CBAR();
    uint32_t a = g_slot.a;
    if (a <= s_last) g_stale++;           /* flag said "new" but payload is old */
    s_last = a; g_got++;
    CBAR();
    g_ready = 0;
}
int main(void)
{
    timer0_start(97, 0x40);
    for (uint32_t i = 1; i <= N; i++) {
        while (g_ready) { }               /* wait until the ISR took the previous one */
        for (volatile uint32_t d = i % 31u; d; d--) { }   /* vary the phase vs. the timer: */
        switch ((i / 31u) % 4u) {                       /* coarse loop + 0..3 single nops */
        case 3: __asm volatile("nop"); /* fall through */
        case 2: __asm volatile("nop"); /* fall through */
        case 1: __asm volatile("nop"); /* fall through */
        default: break;
        }
        publish(i);
    }
    while (g_ready) { }
    timer0_stop();
    board_printf_raw("published=%u consumed=%u stale=%u\n", N, g_got, g_stale);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D8; O=/tmp/rtosD8; for v in nobar cbar; do X=; [ $v = cbar ] && X=-DCBAR_ON; echo "== $v"; make -s -B -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=$S/ex7_cro.c OUT=$O/ex7_$v EXTRA_CFLAGS="$X" >/dev/null && arm-none-eabi-objdump -d --no-show-raw-insn $O/ex7_$v/app.elf | sed -n "/<publish>:/,/^$/p" | grep -v "^$\|word\|\.\.\."; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/ex7_$v/app.elf 60; done'
```

```text
== nobar
000000c0 <publish>:
      c0:	movs	r1, #1
      c2:	ldr	r2, [pc, #16]	@ (d4 <publish+0x14>)
      c4:	ldr	r3, [pc, #16]	@ (d8 <publish+0x18>)
      c6:	str	r1, [r2, #0]
      c8:	lsl.w	r2, r0, r1
      cc:	strd	r0, r2, [r3]
      d0:	bx	lr
      d2:	nop
published=200000 consumed=200000 stale=4
[board_exit] code=0
== cbar
000000c0 <publish>:
      c0:	ldr	r3, [pc, #12]	@ (d0 <publish+0x10>)
      c2:	lsls	r2, r0, #1
      c4:	strd	r0, r2, [r3]
      c8:	movs	r2, #1
      ca:	ldr	r3, [pc, #8]	@ (d4 <publish+0x14>)
      cc:	str	r2, [r3, #0]
      ce:	bx	lr
published=200000 consumed=200000 stale=0
[board_exit] code=0
```

출력에서 볼 것:

- 재배치된 빌드(nobar)에서 ISR이 `str r1, [r2]`(플래그)와 `strd`(페이로드) 사이 **2명령 창**에 들어온 경우가 20만 번 중 4번 있었다 — 플래그가 "새것"이라는데 페이로드는 직전 값이었다. 컴파일러 배리어를 넣은 빌드(cbar)는 0번이다.
- 4/200,000은 작은 숫자다. 바로 그래서 위험하다 — 실칩에서 ISR 주기가 길면 며칠에 한 번이 되고, `-O0` 디버그 빌드에서는 재배치 자체가 일어나지 않는다. **"디버그 빌드에서는 멀쩡한데 릴리스에서 가끔 틀린다"의 대표 원인**이다.
- 처음 시도에서는 ISR이 매번 같은 위상에 들어와서 0번이었다(메인이 ISR 직후 바로 publish하므로). 지연 루프 + nop 0~3개로 위상을 섞자 창에 들어왔다. 레이스 재현은 "타이밍을 흔드는 것"이 절반이다.

### 7.5 예제 12 — 메시지 패싱 litmus를 arm64에서: "Cortex-M 스타일" 코드가 SMP에서 버티나

무엇을 확인하나: EL F5 §3.5의 MP litmus를 이 노트의 질문으로 다시 묻는다. 단일 코어 Cortex-M에서 정답인 "volatile + 컴파일러 배리어"(ORDER 2와 같은 코드)가 두 코어에서는 어떻게 되나. 슬롯 1000개를 쓰는 쪽은 앞에서부터, 읽는 쪽은 뒤에서부터 훑어 중간에서 만나게 하고, 라운드마다 세 번 만나서(시작 · 둘 다 끝 · 리셋 끝) 출발을 맞춘다. 정의와 LKMM 해석은 EL F5가 주인이다.

```c
/* ex9 — message-passing (MP) litmus on the arm64 VM host: does "Cortex-M style" code survive SMP? */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct slot { int data; char p1[60]; int flag; char p2[60]; } __attribute__((aligned(64)));
#define K 1000
static struct slot s[K];
static int rf[K], rd[K];
static atomic_int arrive;
static int V, ROUNDS;
#define VOL(x) (*(volatile int *)&(x))
#define CB()   __asm volatile("" ::: "memory")
static void pin(int c) { cpu_set_t m; CPU_ZERO(&m); CPU_SET(c, &m); pthread_setaffinity_np(pthread_self(), sizeof m, &m); }
static void meet(int n) { atomic_fetch_add(&arrive, 1); while (atomic_load(&arrive) < 2 * n) { } }  /* 2 threads */
static void *writer(void *a)
{
    (void)a; pin(1);
    for (int r = 1; r <= ROUNDS; r++) {
        meet(3 * r - 2);                                   /* start together */
        for (int i = 0; i < K; i++) {
            VOL(s[i].data) = 1;
            if (V == 1) CB();
            if (V == 2) __asm volatile("dmb ishst" ::: "memory");
            if (V == 3) __atomic_store_n(&s[i].flag, 1, __ATOMIC_RELEASE); else VOL(s[i].flag) = 1;
        }
        meet(3 * r - 1);                                   /* both done */
        meet(3 * r);                                       /* reader has reset the slots */
    }
    return NULL;
}
int main(int argc, char **argv)
{
    static const char *nm[] = { "volatile only", "volatile + compiler barrier", "dmb ishst / dmb ishld", "release / acquire" };
    if (argc < 3) return 2;
    V = atoi(argv[1]); ROUNDS = atoi(argv[2]);
    pthread_t w; long bad = 0;
    pin(2); pthread_create(&w, NULL, writer, NULL);
    for (int r = 1; r <= ROUNDS; r++) {
        meet(3 * r - 2);
        for (int i = K - 1; i >= 0; i--) {                 /* walk backwards: meet the writer mid-way */
            rf[i] = V == 3 ? __atomic_load_n(&s[i].flag, __ATOMIC_ACQUIRE) : VOL(s[i].flag);
            if (V == 1) CB();
            if (V == 2) __asm volatile("dmb ishld" ::: "memory");
            rd[i] = VOL(s[i].data);
        }
        meet(3 * r - 1);
        for (int i = 0; i < K; i++) { bad += (rf[i] == 1 && rd[i] == 0); s[i].data = 0; s[i].flag = 0; }
        meet(3 * r);
    }
    pthread_join(w, NULL);
    printf("%-28s %d x %d slots: flag=1 but data=0 seen %ld times\n", nm[V], ROUNDS, K, bad);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D8; O=/tmp/rtosD8/host; gcc -O2 -std=gnu11 -Wall -Wextra -pthread $S/ex9_mp.c -o $O/ex9 && for t in 1 2 3; do for v in 0 1 2 3; do $O/ex9 $v 10000; done; done'
```

```text
volatile only                10000 x 1000 slots: flag=1 but data=0 seen 576 times
volatile + compiler barrier  10000 x 1000 slots: flag=1 but data=0 seen 390 times
dmb ishst / dmb ishld        10000 x 1000 slots: flag=1 but data=0 seen 0 times
release / acquire            10000 x 1000 slots: flag=1 but data=0 seen 0 times
volatile only                10000 x 1000 slots: flag=1 but data=0 seen 495 times
volatile + compiler barrier  10000 x 1000 slots: flag=1 but data=0 seen 414 times
dmb ishst / dmb ishld        10000 x 1000 slots: flag=1 but data=0 seen 0 times
release / acquire            10000 x 1000 slots: flag=1 but data=0 seen 0 times
volatile only                10000 x 1000 slots: flag=1 but data=0 seen 521 times
volatile + compiler barrier  10000 x 1000 slots: flag=1 but data=0 seen 324 times
dmb ishst / dmb ishld        10000 x 1000 slots: flag=1 but data=0 seen 0 times
release / acquire            10000 x 1000 slots: flag=1 but data=0 seen 0 times
```

출력에서 볼 것:

- 1천만 번 중 **컴파일러 배리어만 있는 쪽이 324~414번** "flag는 1인데 data는 0"을 봤다. 컴파일러는 순서를 지켰다 — CPU가 바꿨다. `dmb ishst`/`dmb ishld` 짝과 release/acquire는 0이다.
- 이 실험의 첫 버전은 dmb · release/acquire에서도 위반이 나왔다. 원인은 litmus가 아니라 **실험 코드의 레이스**였다 — 읽는 쪽이 다 읽고 슬롯을 리셋하는 동안 쓰는 쪽이 아직 그 라운드의 쓰기를 끝내지 않아서, "data는 리셋됐는데 flag는 1"인 슬롯이 다음 라운드로 넘어갔다. 만나는 지점을 하나 더 넣어(둘 다 끝난 뒤에 리셋) 고쳤다. **락 없는 코드를 검증하는 테스트도 락 없는 코드라서 같은 실수를 한다**는 교훈이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">VM 호스트 arm64(실제 코어 2개)에서 관찰된 순서 위반 — 3회 중앙값</text>
<text x="10" y="48" font-size="12" font-weight="bold">SPSC 링 (N = 16) 2천만 개 전달 — 틀린 값을 받은 횟수</text>
<text x="222" y="73" font-size="12" text-anchor="end">volatile만</text>
<rect x="230" y="61" width="360" height="18" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="596" y="74" font-size="12">181,126</text>
<text x="222" y="101" font-size="12" text-anchor="end">volatile + 컴파일러 배리어</text>
<rect x="230" y="89" width="269.669" height="18" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="505.669" y="102" font-size="12">135,678</text>
<text x="222" y="129" font-size="12" text-anchor="end">volatile + DMB</text>
<text x="236" y="130" font-size="12">0</text>
<text x="222" y="157" font-size="12" text-anchor="end">C11 acquire/release</text>
<text x="236" y="158" font-size="12">0</text>
<line x1="230" y1="56" x2="230" y2="170" stroke="currentColor"/>
<text x="10" y="190" font-size="12" font-weight="bold">MP litmus 1천만 번 — flag = 1인데 data = 0을 본 횟수</text>
<text x="222" y="215" font-size="12" text-anchor="end">volatile만</text>
<rect x="230" y="203" width="360" height="18" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="596" y="216" font-size="12">521</text>
<text x="222" y="243" font-size="12" text-anchor="end">volatile + 컴파일러 배리어</text>
<rect x="230" y="231" width="269.482" height="18" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
<text x="505.482" y="244" font-size="12">390</text>
<text x="222" y="271" font-size="12" text-anchor="end">dmb ishst / ishld</text>
<text x="236" y="272" font-size="12">0</text>
<text x="222" y="299" font-size="12" text-anchor="end">release / acquire</text>
<text x="236" y="300" font-size="12">0</text>
<line x1="230" y1="198" x2="230" y2="312" stroke="currentColor"/>
</svg>
```

그림 9 — VM 호스트 arm64(실제 코어 두 개)에서 관찰한 순서 위반(3회 중앙값, 그룹마다 따로 정한 선형 축). 위: 예제 7의 SPSC 링, 아래: 예제 12의 MP litmus. 빨강 = 1코어 Cortex-M에서는 정답이던 코드(아래쪽 둘째 줄)와 `volatile`만, 초록 = 코어 간 배리어가 있는 코드.

### 7.6 예제 13 — 커널 소스는 어디에 배리어를 넣었나

무엇을 확인하나: §7.2의 장면들이 실제 RTOS 소스에 그대로 있는지.

```sh
limactl shell elinux -- bash -lc 'cd /var/rtoslab/zephyrproject/zephyr/arch/arm/core; grep -rn "barrier_dsync_fence_full\|barrier_isync_fence_full\|barrier_dmem_fence_full\|__DSB\|__ISB\|__DMB" cortex_m/*.c mpu/arm_mpu.c | head -24; cd /var/rtoslab/src/FreeRTOS-Kernel; grep -n "dsb\|isb" portable/GCC/ARM_CM3/port.c portable/GCC/ARM_CM3/portmacro.h'
```

```text
cortex_m/cpu_idle.c:45:			__DSB();                                                                   \
cortex_m/cpu_idle.c:54:		__DSB();                                                                           \
...
cortex_m/irq_manage.c:230:	barrier_dsync_fence_full();
cortex_m/irq_manage.c:231:	barrier_isync_fence_full();
cortex_m/prep_c.c:67:	barrier_dsync_fence_full();
cortex_m/prep_c.c:68:	barrier_isync_fence_full();
...
cortex_m/thread.c:571:	barrier_isync_fence_full();
mpu/arm_mpu.c:265:	barrier_dsync_fence_full();
mpu/arm_mpu.c:266:	barrier_isync_fence_full();
...
portable/GCC/ARM_CM3/port.c:604:                __asm volatile ( "dsb" ::: "memory" );
portable/GCC/ARM_CM3/port.c:606:                __asm volatile ( "isb" );
...
portable/GCC/ARM_CM3/portmacro.h:92:        __asm volatile ( "dsb" ::: "memory" );
portable/GCC/ARM_CM3/portmacro.h:93:        __asm volatile ( "isb" );
portable/GCC/ARM_CM3/portmacro.h:220:        "   isb                                                     \n"
portable/GCC/ARM_CM3/portmacro.h:221:        "   dsb                                                     \n"
...
```

(장면과 관련된 줄만 남기고 나머지는 `...`로 줄였다. Zephyr 쪽은 `head -24`로 자른 24줄, FreeRTOS 쪽은 18줄이다.)

장면과 짝지으면:

| 소스 위치 | 무엇 뒤에 | 그림 8의 장면 |
|---|---|---|
| Zephyr `cortex_m/prep_c.c:66~68` | `SCB->VTOR = VECTOR_ADDRESS & VTOR_MASK;` | VTOR 변경 → DSB + ISB |
| Zephyr `mpu/arm_mpu.c:265~266` 근처 | MPU 활성화 | MPU 변경 → DSB + ISB |
| Zephyr `cortex_m/thread.c:571` | FPU 공유 모드에서 `__set_CONTROL(...)` | CONTROL 변경 → ISB |
| Zephyr `cortex_m/cpu_idle.c:45 · 54` | `wfi`/`wfe` 직전 (주석: "Wait for all memory transaction to complete before entering low power state") | WFI 직전 → DSB |
| Zephyr `cortex_m/irq_manage.c:230~231` | 모든 IRQ의 NVIC `ICER` 쓰기 | NVIC 비활성화 → DSB + ISB |
| FreeRTOS `ARM_CM3/port.c:604~606` | tickless 진입 `dsb` → `wfi` → `isb` | WFI 직전 → DSB |
| FreeRTOS `ARM_CM3/portmacro.h:92~93` (`portYIELD`) | PendSV 세팅 (주석: "Barriers are normally not required but do ensure the code is completely within the specified behaviour for the architecture") | AN321의 "아키텍처 규칙대로" |
| FreeRTOS `ARM_CM3/portmacro.h:220 · 237` | `msr basepri` (크리티컬 섹션 진입) | 시스템 레지스터 변경 → ISB/DSB |

FreeRTOS `portYIELD`의 주석이 이 절의 요지를 한 줄로 말한다 — "보통은 필요 없지만 아키텍처 명세 안에 있도록". Zephyr의 `barrier_dsync_fence_full()` 등은 `include/zephyr/arch/arm/barrier.h`에서 CMSIS `__DSB()`/`__ISB()`/`__DMB()`로, `barrier_sync_synchronize()`는 `__sync_synchronize()`(= `dmb`)로 정의된다.

### 7.7 QEMU가 보여 주지 못하는 것 — 정직하게

- **쓰기 버퍼 · posted write · ISR 이중 진입**: QEMU의 주변장치 쓰기는 즉시 반영된다. §7.2의 DSB/읽어 되돌리기를 빼도 QEMU에서는 아무 일도 없다. 실칩(특히 APB 브리지 너머의 타이머 · UART를 쓰는 STM32 계열)에서만 재현된다.
- **CPU 재배치**: TCG는 게스트 명령을 순서대로 실행한다(단일 vCPU). 그래서 CPU 재배치 실험은 VM 호스트 arm64에서 했다(§6.1, §7.5).
- **DMB/DSB 비용**: icount는 모든 명령을 32 ns로 센다. 실칩의 DMB/DSB는 쓰기 버퍼가 빌 때까지 기다리므로 그보다 비쌀 수 있다(실측은 하지 못했다 — 확인 못 함).
- **캐시**: QEMU M7(`mps2-an500`)에는 D-cache 효과가 없다(B8). M7 DMA의 clean/invalidate 실험은 이 랩에서 불가능하다.
- QEMU에서 **보여 줄 수 있었던 것**: 컴파일러 재배치(예제 11), 예외가 exclusive monitor를 지우는 것(예제 1), 생성 코드의 배리어 위치(예제 2 · 6 · 10), 그리고 RTOS 소스의 배리어 위치(예제 13).

---

## 8. RTOS 커널 안에서는 무슨 일이 일어나나 — 무엇이 정말 락 없이 도나

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">FreeRTOS V11.3.2 · Zephyr v4.4.2 — 무엇이 정말 락 없이 도나 (소스 확인)</text>
<text x="223" y="44" font-size="12" text-anchor="middle">락 없음 (LDREX 또는 한쪽만 쓰는 인덱스)</text>
<text x="560" y="44" font-size="12" text-anchor="middle">IRQ 마스크 · 크리티컬 섹션 기반</text>
<text x="10" y="92" font-size="12" font-weight="bold">FreeRTOS</text>
<text x="10" y="197" font-size="12" font-weight="bold">Zephyr</text>
<rect x="92" y="56" width="262" height="66" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="223" y="77.2" font-size="12" text-anchor="middle">stream / message buffer의</text>
<text x="223" y="93.2" font-size="12" text-anchor="middle">데이터 경로 (쓰는 쪽 1 · 읽는 쪽 1)</text>
<text x="223" y="109.2" font-size="12" text-anchor="middle">배리어는 없다 (단일 코어 전제)</text>
<rect x="370" y="56" width="300" height="66" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="520" y="77.2" font-size="12" text-anchor="middle">queue · semaphore · task notification</text>
<text x="520" y="93.2" font-size="12" text-anchor="middle">atomic.h의 Atomic_* (이름과 달리</text>
<text x="520" y="109.2" font-size="12" text-anchor="middle">인터럽트 마스크로 구현)</text>
<rect x="92" y="140" width="262" height="120" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="223" y="164.2" font-size="12" text-anchor="middle">atomic_t (M3 이상 = BUILTIN</text>
<text x="223" y="180.2" font-size="12" text-anchor="middle">→ LDREX/STREX + dmb)</text>
<text x="223" y="196.2" font-size="12" text-anchor="middle">spsc_lockfree.h (atomic_t 인덱스)</text>
<text x="223" y="212.2" font-size="12" text-anchor="middle">ring_buf (1:1이면 락 불필요,</text>
<text x="223" y="228.2" font-size="12" text-anchor="middle">배리어 없음)</text>
<text x="223" y="244.2" font-size="12" text-anchor="middle">spsc_pbuf (배리어 + 캐시 유지)</text>
<rect x="370" y="140" width="300" height="120" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="520" y="172.2" font-size="12" text-anchor="middle">atomic_t on M0 = ATOMIC_OPERATIONS_C</text>
<text x="520" y="188.2" font-size="12" text-anchor="middle">(atomic_c.c, 전역 k_spinlock)</text>
<text x="520" y="204.2" font-size="12" text-anchor="middle">mpsc_pbuf (k_spinlock)</text>
<text x="520" y="220.2" font-size="12" text-anchor="middle">k_msgq · k_fifo · k_sem</text>
<text x="520" y="236.2" font-size="12" text-anchor="middle">(커널 객체 = irq lock)</text>
<text x="10" y="288" font-size="12">"락 없음"도 생산자 · 소비자 수 규칙을 지킬 때만이다. 규칙을 어기면 둘 다 깨진다.</text>
</svg>
```

그림 10 — FreeRTOS V11.3.2와 Zephyr v4.4.2 소스에서 확인한 분류. 왼쪽만 인터럽트를 끄지 않는다.

### 8.1 FreeRTOS

- **stream buffer · message buffer의 데이터 경로**(`stream_buffer.c`)는 SPSC 설계다. `StreamBuffer_t`의 `volatile size_t xTail;`(다음에 읽을 곳) · `volatile size_t xHead;`(다음에 쓸 곳)를 각각 한쪽만 쓰고, 쓰는 쪽은 `prvWriteBytesToBuffer()`로 바이트를 복사한 **다음** `pxStreamBuffer->xHead = ...`를 대입한다(line 1058). 그래서 헤더(`include/stream_buffer.h`)가 이렇게 경고한다: "unlike other FreeRTOS objects, it is not safe to have multiple different writers or multiple different readers … One way to achieve such serialization … is to place each API call inside a critical section and use a block time of 0." 인덱스 갱신에 DMB가 없다 — 단일 코어(또는 커널이 크리티컬 섹션으로 감싸는 SMP 경로)를 전제한다. 블로킹 · 깨우기 경로(`xTaskNotifyWait`, `sbSEND_COMPLETED`)는 크리티컬 섹션을 쓴다. API 사용법은 D5.
- **queue · semaphore · mutex · task notification**은 모두 `taskENTER_CRITICAL()` / `taskENTER_CRITICAL_FROM_ISR()`(BASEPRI) 안에서 돈다. 예: `queue.c` line 1205(`xQueueGenericSendFromISR`) · line 1381(`xQueueGiveFromISR`), `tasks.c`의 `vTaskGenericNotifyGiveFromISR()`. §6.3의 7 µs가 여기서 나온다.
- **`include/atomic.h`**(`Atomic_Add_u32` 등)는 §3.5에서 본 대로 인터럽트 마스크 구현이다.
- **`portMEMORY_BARRIER()`**: ARM_CM3 `portmacro.h`에서 `__asm volatile ( "" ::: "memory" )`, 즉 **컴파일러 배리어**다. `tasks.c`의 `vTaskSuspendAll()`(line 3890에서 시작, 배리어는 line 3932)이 `uxSchedulerSuspended` 증가 직후에 쓴다 — 주석: "Enforces ordering for ports and optimised compilers that may otherwise place the above increment elsewhere." 커널 자신도 1코어에서 **CPU 배리어가 아니라 컴파일러 배리어**로 순서를 잡는다는 증거다.
- **SMP 포트**(Armv8-M의 `prvSpinTrylock`)만 LDREX/STREX · CLREX · DMB를 쓴다(§2.4, C10).

### 8.2 Zephyr

- **`atomic_t` API**(`include/zephyr/sys/atomic.h`): `atomic_inc`, `atomic_add`, `atomic_cas`, `atomic_set_bit`, `atomic_test_and_set_bit`, `atomic_get`/`atomic_set` …. Cortex-M3 이상은 `atomic_builtin.h`(모두 `__ATOMIC_SEQ_CST`), M0는 `kernel/atomic_c.c`(전역 `k_spinlock` = 1코어에서 irq lock) — §3.5. `atomic_set_bit()`은 `atomic_or()` 위에 만든 인라인 함수다.
- **`include/zephyr/sys/spsc_lockfree.h`**: `SPSC_DEFINE(name, type, sz)`(크기는 2의 거듭제곱), 생산자 `spsc_acquire()` → `spsc_produce()`, 소비자 `spsc_consume()` → `spsc_release()`. 인덱스 `in` · `out`이 `atomic_t`이고 free-running(`z_spsc_mask()`로 `& mask`) — 이 노트의 `spsc.h`와 같은 설계다. 짝 헤더 `mpsc_lockfree.h`도 있다 — 머리 주석이 "A wait-free intrusive multi producer single consumer (MPSC) queue using a singly linked list"라고 설명한다.
- **`ring_buf`**(`include/zephyr/sys/ring_buffer.h`, `lib/utils/ring_buffer.c`, `CONFIG_RING_BUFFER`): 문서(`doc/kernel/data_structures/ring_buffers.rst`)가 "For the trivial case of one producer and one consumer, concurrency control shouldn't be needed"라고 한다. 소스에는 배리어도 atomic도 없다 — 인덱스(`struct ring_buf_index { head, tail, base }`)는 일반 변수다. **단일 코어에서 동작하는 이유는 API가 함수 호출이라(컴파일러가 호출 경계를 넘어 메모리 접근을 옮기지 못한다) 컴파일러 배리어가 저절로 생기기 때문**이고, 코어 간 공유에는 보장이 없다(소스에서 확인한 사실과 그로부터의 추론이다).
- **`spsc_pbuf`**(`lib/os/spsc_pbuf.c`): 가변 길이 패킷 SPSC. `barrier_sync_synchronize()`(= `dmb`)를 인덱스 갱신마다 넣고 `SPSC_PBUF_CACHE` 플래그면 `sys_cache_data_flush_range()`/`invd_range()`까지 한다 — **코어 간(IPC) 공유를 위해 설계된** 버전이다.
- **`mpsc_pbuf`**(`include/zephyr/sys/mpsc_pbuf.h`): 생산자가 여럿이라 `struct k_spinlock lock;`을 쓴다. 로깅 서브시스템의 버퍼가 이것이다.

### 8.3 예제 14 — 같은 Zephyr 앱: `atomic_t`, `spsc_lockfree`, `ring_buf`를 ISR ↔ 스레드로

무엇을 확인하나: Zephyr에서 어떤 atomic 구현이 골라졌는지, `atomic_inc`의 비용, 그리고 `k_timer` 콜백(시스템 클록 ISR 문맥) → `main` 스레드로 SPSC · `ring_buf`가 순서대로 전달되는지. 같은 앱을 M0 보드로도 빌드하려 했지만 §3.5대로 모듈이 없어 실패했다.

```text
# CMakeLists.txt
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(d8_zat)
target_sources(app PRIVATE src/main.c)
# prj.conf
CONFIG_RING_BUFFER=y
CONFIG_SYS_CLOCK_TICKS_PER_SEC=10000
CONFIG_QEMU_ICOUNT_SHIFT=5
```

```c
/* zat — Zephyr atomic_t + spsc_lockfree + ring_buf: same source on Cortex-M3 and Cortex-M0 */
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/spsc_lockfree.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/printk.h>
#define N 20000
static atomic_t a_cnt = ATOMIC_INIT(0), flags = ATOMIC_INIT(0);
static volatile uint32_t p_cnt;
SPSC_DEFINE(rxq, uint8_t, 64);                 /* lock-free, power-of-two */
RING_BUF_DECLARE(rb, 64);                      /* "no locking needed for 1 producer + 1 consumer" */
static uint32_t isr_n;

static void tick(struct k_timer *t)            /* runs in ISR context (sys clock IRQ) */
{
    ARG_UNUSED(t);
    atomic_inc(&a_cnt); p_cnt++;
    uint8_t *slot = spsc_acquire(&rxq);
    if (slot) { *slot = (uint8_t)isr_n; spsc_produce(&rxq); }
    uint8_t b = (uint8_t)isr_n; ring_buf_put(&rb, &b, 1);
    isr_n++;
}
K_TIMER_DEFINE(tmr, tick, NULL);

int main(void)
{
    printk("ATOMIC_OPERATIONS: %s\n", IS_ENABLED(CONFIG_ATOMIC_OPERATIONS_BUILTIN) ? "BUILTIN" :
           IS_ENABLED(CONFIG_ATOMIC_OPERATIONS_C) ? "C (irq lock)" : "ARCH");
    atomic_set_bit(&flags, 3);
    bool swapped = atomic_cas(&flags, 0x8, 0x9);
    printk("set_bit(3) -> cas(0x8 -> 0x9) = %d, flags = 0x%lx\n", swapped, (long)atomic_get(&flags));

    uint32_t c0 = k_cycle_get_32();
    for (int i = 0; i < N; i++) atomic_inc(&a_cnt);
    uint32_t c1 = k_cycle_get_32();
    for (int i = 0; i < N; i++) p_cnt++;
    uint32_t c2 = k_cycle_get_32();
    printk("cycles/op (hw %u Hz): atomic_inc %u.%02u  plain ++ %u.%02u\n", sys_clock_hw_cycles_per_sec(),
           (c1 - c0) / N, (c1 - c0) % N * 100 / N, (c2 - c1) / N, (c2 - c1) % N * 100 / N);

    atomic_set(&a_cnt, 0); p_cnt = 0;
    k_timer_start(&tmr, K_USEC(100), K_USEC(100));
    uint32_t got = 0, bad = 0, rgot = 0, rbad = 0; uint8_t want = 0, rwant = 0, b;
    for (int i = 0; i < N * 20; i++) {
        atomic_inc(&a_cnt); p_cnt++;
        uint8_t *v = spsc_consume(&rxq);
        if (v) { bad += (*v != want); want = (uint8_t)(*v + 1); spsc_release(&rxq); got++; }
        if (ring_buf_get(&rb, &b, 1)) { rbad += (b != rwant); rwant = (uint8_t)(b + 1); rgot++; }
    }
    k_timer_stop(&tmr);
    printk("ISR ran %u times: atomic lost %d, plain ++ lost %d\n", isr_n,
           (int)(N * 20 + isr_n - atomic_get(&a_cnt)), (int)(N * 20 + isr_n - p_cnt));
    printk("spsc got %u (order errors %u), ring_buf got %u (order errors %u)\n", got, bad, rgot, rbad);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/D8/zat; mkdir -p /tmp/rtosD8/zcache; XDG_CACHE_HOME=/tmp/rtosD8/zcache /var/rtoslab/bin/rtos-zbuild mps2/an385 $S /tmp/rtosD8/zat_m3 -- -DUSER_CACHE_DIR=/tmp/rtosD8/zcache && /var/rtoslab/bin/rtos-zrun /tmp/rtosD8/zat_m3 20'
```

```text
           FLASH:       14564 B         4 MB      0.35%
             RAM:        6352 B         4 MB      0.15%
qemu-system-arm: warning: nic lan9118.0 has no peer
*** Booting Zephyr OS build v4.4.2 ***
ATOMIC_OPERATIONS: BUILTIN
set_bit(3) -> cas(0x8 -> 0x9) = 1, flags = 0x9
cycles/op (hw 25000000 Hz): atomic_inc 7.20  plain ++ 4.00
ISR ran 16740 times: atomic lost 0, plain ++ lost 5
spsc got 16740 (order errors 0), ring_buf got 16740 (order errors 0)
```

출력에서 볼 것:

- M3 보드는 `ATOMIC_OPERATIONS_BUILTIN`. `atomic_inc`는 7.2사이클, 그냥 `++`는 4사이클(25 MHz 하드웨어 사이클, icount shift 5 — `CONFIG_QEMU_ICOUNT_SHIFT=5`).
- 100 µs `k_timer`가 16,740번 돌면서 같은 카운터를 올렸다. `atomic_inc`는 0개를 잃었고 **그냥 `++`는 5개를 잃었다** — 주기가 길어서(100 µs) 창에 들어갈 확률이 예제 1보다 훨씬 낮을 뿐 같은 레이스다.
- `spsc_lockfree`와 `ring_buf` 둘 다 16,740개를 순서 오류 없이 넘겼다.

`k_timer` 콜백이 무엇으로 컴파일됐나 — `spsc_acquire()`/`spsc_produce()` 부분이다.

```sh
limactl shell elinux -- bash -lc 'E=/tmp/rtosD8/zat_m3/zephyr/zephyr.elf; arm-none-eabi-objdump -d --no-show-raw-insn $E | sed -n "/<tick>:/,/^$/p" | head -60'
```

```text
000000c0 <tick>:
      c0:	push	{r0, r1, r4, r5, r6, lr}
      c2:	dmb	ish
      c6:	ldr	r3, [pc, #156]	@ (164 <CONFIG_IDLE_STACK_SIZE+0x64>)
      c8:	ldrex	r1, [r3]
      cc:	adds	r1, #1
      ce:	strex	r2, r1, [r3]
      d2:	cmp	r2, #0
      d4:	bne.n	c8 <tick+0x8>
      d6:	dmb	ish
      da:	ldr	r2, [pc, #140]	@ (168 <CONFIG_IDLE_STACK_SIZE+0x68>)
      dc:	ldr	r3, [r2, #0]
      de:	adds	r3, #1
      e0:	str	r3, [r2, #0]
      e2:	ldr	r3, [pc, #136]	@ (16c <CONFIG_IDLE_STACK_SIZE+0x6c>)
      e4:	mov	r1, r3
      e6:	dmb	ish
      ea:	ldr.w	r0, [r1], #-8
      ee:	dmb	ish
      f2:	ldr.w	r2, [r3, #-8]
      f6:	add	r0, r2
      f8:	dmb	ish
      fc:	ldr	r4, [r1, #12]
      fe:	dmb	ish
...
     130:	dmb	ish
     134:	ldrex	r1, [r3]
     138:	adds	r1, #1
     13a:	strex	r2, r1, [r3]
     13e:	cmp	r2, #0
     140:	bne.n	134 <CONFIG_IDLE_STACK_SIZE+0x34>
     142:	dmb	ish
     146:	ldr	r3, [r4, #0]
...
     154:	bl	2404 <ring_buf_put>
...
```

(가운데 슬롯 계산 부분을 `...`로 줄였다.)

출력에서 볼 것:

- `atomic_inc(&a_cnt)` = `dmb` + `ldrex`/`strex` 루프 + `dmb` — §3.2의 M3 seq_cst RMW 그대로다.
- `spsc_acquire()` 한 번에 `atomic_get()`이 두 번(in · out) 불리고, `atomic_get`이 seq_cst load라서 **`dmb` · `ldr` · `dmb`가 두 번** 나온다. 이어 `spsc_produce()`의 `atomic_add`가 또 `dmb` 두 개. **바이트 하나에 DMB 6개**다. 1코어에서는 전부 필요 없는 명령이고, Zephyr가 이식성(SMP · 다른 아키텍처)을 위해 치르는 비용이다. 20 kHz ISR이라면 상관없고, 1 MB/s 바이트 스트림이라면 이 노트의 `spsc.h`(ORDER 2) 쪽이 낫다.
- `ring_buf_put`은 함수 호출(`bl`)이다. 그 안에는 `dmb`가 없다 — 아래 명령으로 `ring_buf_area_finish`와 `ring_buf_put`의 `dmb`를 세면 둘 다 0이다.

```sh
limactl shell elinux -- bash -lc 'E=/tmp/rtosD8/zat_m3/zephyr/zephyr.elf; for f in ring_buf_area_finish ring_buf_put; do arm-none-eabi-objdump -d --no-show-raw-insn $E | sed -n "/<$f>:/,/^$/p" | grep -c dmb; done'
```

```text
0
0
```

### 8.4 연결

- `xStreamBufferSendFromISR` · Zephyr `k_pipe`로 바이트 스트림을 넘기는 API 수준의 사용법은 D5 · D10.
- ISR → 태스크 알림(언제 깨울지)은 D1(세마포어) · D5(task notification) · E1(FromISR 규칙).
- DMA 완료 → 태스크(버퍼 소유권 넘기기)는 E6, 듀얼코어 메일박스는 P4 · P5.

---

## 9. 실무에서 다시 보기

### 9.1 드론 비행 제어기 — 자이로 DMA 더블 버퍼 + 추정기로 가는 SPSC

IMU(예: 8 kHz 자이로)를 SPI DMA로 읽는 FC에서 흔한 구조다.

1. **DMA 더블 버퍼(ping-pong)**: DMA가 버퍼 A를 채우는 동안 CPU는 버퍼 B를 처리한다. DMA half-transfer / transfer-complete 인터럽트가 "어느 쪽이 다 찼는지"를 알려 준다. 여기서 **소유권은 하드웨어 이벤트가 넘긴다** — LDREX는 DMA를 보지 못하므로(§2.6) 아무 소용이 없다. M7(STM32H7)이면 CPU가 버퍼를 읽기 전에 그 영역을 `SCB_InvalidateDCache_by_Addr`(B8), DMA를 다시 시작하기 전에 `dmb`(그림 8 첫 행).
2. **DMA 완료 ISR → 추정기 태스크로 SPSC**: ISR은 샘플(타임스탬프 + 3축)을 SPSC 링에 넣는다. 원소가 16바이트 구조체여도 SPSC 규칙은 같다(슬롯을 다 쓴 **다음** head). 추정기 태스크는 깨어나서 쌓인 샘플을 한 번에 처리한다(§6.3의 "묶어서" 교훈).
3. **타임스탬프**: 샘플마다 찍는 64비트 µs 시각은 hi-lo-hi(C7 §1.3).
4. **가득 찼을 때**: 추정기가 늦으면 링이 찬다. 새 샘플을 버리고 드롭 카운터를 올린다 — 그 카운터는 ISR만 올리므로 `volatile uint32_t`로 충분하고, 태스크는 읽기만 한다(값 하나 · 쓰는 사람 하나).

### 9.2 로봇 관절 모터 컨트롤러 — 20 kHz FOC ISR이 setpoint를 seqlock으로 내보낸다

FOC 전류 루프 ISR(20 kHz, 50 µs 예산)은 BASEPRI 위 zero-latency 우선순위로 두는 경우가 많다(A7, C7 — FreeRTOS API를 못 쓴다). 이 ISR이 계산한 `{id, iq, theta, vbus}`를 CAN 텔레메트리 태스크가 1 kHz로 읽는다 → **seqlock**(예제 5). 반대로 CAN 명령 태스크가 새 토크 setpoint와 이득을 내려보내면 → **더블 버퍼 + 인덱스 하나**(§4.3). 이 ISR 안에서는 `taskENTER_CRITICAL`도 FreeRTOS `Atomic_*`도 보호가 되지 않는다는 점을 다시 강조한다(§3.5).

### 9.3 Linux SoC 옆의 센서 허브 (Zephyr)

센서 허브 MCU가 샘플을 모아 SPI/UART로 Linux에 넘기는 경우, ISR → 전송 스레드 경로는 `spsc_lockfree.h` 또는 `ring_buf`(1:1이면 락 불필요)로 충분하다. 허브가 Linux SoC와 **공유 메모리**(OpenAMP/rpmsg)로 통신하면 상대가 다른 코어이므로 `spsc_pbuf`처럼 배리어와 캐시 유지가 들어간 버전을 쓴다(P5, EL O1–O3).

### 9.4 Don의 SSD 펌웨어와 이어서

- SSD 컨트롤러의 **코어 간 메시지 링**(예: 호스트 인터페이스 코어 → FTL 코어)은 이 노트의 SPSC 링과 같은 구조다. 다른 점은 상대가 다른 코어라는 것 — 그래서 SSD 펌웨어에서는 "디스크립터 쓰기 → DMB → 인덱스(또는 doorbell) 쓰기"가 기본이었고, 그것이 §6.1의 ORDER 0/3이다. 1코어 RTOS로 오면 같은 링에서 DMB를 컴파일러 배리어로 줄일 수 있다(ORDER 2).
- **Doorbell 레지스터**(NVMe SQ tail doorbell처럼 하드웨어가 읽는 인덱스)는 Device 메모리 쓰기다. 디스크립터(Normal) → doorbell(Device) 사이의 `dmb`는 코어 수와 상관없이 필요하다(그림 8 첫 행). Don이 PCIe 쪽에서 본 posted write 문제는 칩 안의 APB 브리지에서도 "플래그 클리어 후 ISR 재진입"으로 나타난다(§7.2 장면 2).
- 통계 카운터를 여러 코어가 올리던 경우 SSD에서는 코어별 카운터(per-core counter)를 두고 읽을 때 합쳤을 것이다. 1코어 MCU에서도 같은 아이디어가 통한다 — **ISR 전용 카운터 + 태스크 전용 카운터**로 나누면 둘 다 "쓰는 사람 하나"가 되어 atomic이 필요 없다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 공유 카운터를 `volatile`로만 선언하고 `++` | 통계가 조금씩 모자란다 | RMW가 쪼개진다 (예제 1: 60% 손실) | `atomic_fetch_add` (M3+), M0는 IRQ 마스크 · 쓰는 사람을 하나로 |
| 링버퍼에 `count` 변수를 따로 둔다 | 가끔 데이터가 하나 사라지거나 두 번 나온다 | `count`를 양쪽이 고친다 (SPSC가 아님) | free-running head/tail, 개수 = head − tail |
| `buf`는 일반 변수, `head`만 `volatile` | 릴리스(-O2) 빌드에서만 가끔 옛 데이터 | 컴파일러가 데이터 쓰기를 head 뒤로 옮김 (예제 11) | 컴파일러 배리어 또는 C11 release/acquire |
| 1코어용 링(컴파일러 배리어)을 듀얼코어 공유 RAM에 그대로 | 0.5~1%의 옛 값 (예제 7: 13만/2천만) | CPU 재배치 | C11 acquire/release 또는 DMB, M7이면 캐시 유지 |
| M0 프로젝트에서 `<stdatomic.h>`의 `atomic_fetch_add` | 링크 에러 `undefined reference to __atomic_fetch_add_4` | Armv6-M에 LDREX 없음 + libatomic 없음 | `__atomic_*_4`를 PRIMASK로 공급 (예제 4) 또는 Zephyr `atomic_t`(atomic_c.c) |
| zero-latency ISR과 FreeRTOS `Atomic_*`로 공유 | 가끔 값이 틀린다 | `Atomic_*`가 BASEPRI 마스크라 그 ISR을 못 막음 | C11 atomics (LDREX) 또는 SPSC/seqlock |
| 태스크가 쓰고 ISR이 읽는 데이터에 seqlock | 시스템이 ISR 안에서 멈춘다 | ISR이 홀수 seq에서 영원히 재시도 | 더블 버퍼 + 인덱스 (§4.3) |
| ISR 맨 끝에서 주변장치 플래그 클리어 | ISR이 두 번씩 불린다 (두 번째는 빈손) | 쓰기 버퍼 · posted write로 클리어가 늦게 도착 | 앞에서 클리어, 다시 읽기, `dsb` |
| VTOR · CONTROL 변경 뒤 배리어 없음 | 드물게 엉뚱한 핸들러 · 스택 | 다음 명령이 옛 설정으로 실행 | `dsb` + `isb` |
| DMA 디스크립터 쓰기 직후 시작 비트 | M7에서만 DMA가 옛 디스크립터로 동작 | Normal → Device 순서 미보장 + D-cache | `dmb`(M3/M4), clean + `dsb`(M7) |

---

## 11. 면접에서 이렇게 말한다

**Q.** "Write a lock-free ring buffer between a UART RX ISR and a task. What makes it safe?"

**A.** 크기를 2의 거듭제곱으로 하고 head · tail을 free-running 32비트 카운터로 둔다. 개수는 head − tail, 빔은 같을 때, 가득은 차이가 N일 때다. 안전한 이유는 두 가지다. 소유권 — head는 ISR만, tail은 태스크만 쓴다, 그래서 RMW 레이스가 없다. 순서 — 슬롯에 쓴 다음 head를 올리고, head를 읽은 다음 슬롯을 읽는다. 단일 코어 Cortex-M에서는 그 순서를 컴파일러 배리어로, 코어가 둘이면 release/acquire로 지킨다.

> Power-of-two size, free-running 32-bit head and tail; count is head minus tail, empty when equal, full when the difference is N. It's safe for two reasons. Ownership: only the ISR writes head and only the task writes tail, so there's no read-modify-write race. Ordering: the producer writes the slot before publishing head, and the consumer reads head before reading the slot. On a single Cortex-M core a compiler barrier is enough to keep that order; across cores I use release/acquire, which is a DMB on M4 or STL/LDA on M33. I measured it: with only a compiler barrier, a two-core arm64 stress test got about 135 thousand wrong values out of 20 million; with acquire/release, zero.

**꼬리 질문**: "Why not keep a `count` field?" → 양쪽이 고치는 변수가 되어 SPSC가 깨진다. "Two producers?" → LDREX 기반 MPSC나 크리티컬 섹션.

**Q.** "Is `volatile` enough for sharing data with an ISR?"

**A.** 정렬된 32비트 값 하나를 한쪽만 쓸 때는 충분하다. RMW(`++`)는 원자적이지 않고, `volatile`은 volatile끼리의 순서만 지키므로 일반 변수(버퍼)와의 순서를 지키지 못한다. GCC -O2가 실제로 플래그 쓰기를 데이터 쓰기 앞으로 옮기는 것을 봤다.

> Only for a single aligned word with one writer. `volatile` doesn't make `count++` atomic, and it only orders volatile accesses among themselves, so the compiler may move a plain buffer write past the volatile flag. I've seen GCC 14 at -O2 emit the flag store before the payload store on a Cortex-M3; a compiler barrier fixed it, and on QEMU the ISR saw stale payloads only in the unfixed build.

**Q.** "How do LDREX and STREX work, and what happens if an interrupt comes in between?"

**A.** LDREX는 읽으면서 로컬 exclusive monitor를 Exclusive로 만들고, STREX는 모니터가 아직 Exclusive일 때만 쓰고 0을 돌려준다. Armv7-M에서는 예외 진입과 복귀가 모니터를 지우므로, 사이에 인터럽트가 끼면 STREX가 1을 받고 루프가 다시 돈다 — ISR이 그 변수를 건드렸든 아니든. 그래서 Cortex-M 컨텍스트 스위치에는 CLREX가 필요 없다. CLREX는 LDREX 뒤에 STREX 없이 빠져나갈 때 쓴다.

> LDREX loads and marks the local exclusive monitor; STREX stores only if the monitor is still exclusive and returns 0, otherwise it doesn't store and returns 1, and you retry. On Armv7-M, exception entry and return clear the local monitor, so an interrupt between them always forces a retry — I measured exactly the same 8,888 STREX failures whether the ISR touched the same variable or a different one. That's why FreeRTOS and Zephyr Cortex-M context switches don't issue CLREX; you need CLREX when you bail out after an LDREX without doing the STREX, like a trylock that finds the lock busy.

**꼬리 질문**: "Does it protect against DMA?" → 아니다. 로컬 모니터는 다른 마스터의 쓰기를 보지 못한다. 글로벌 모니터는 칩마다 다르다.

**Q.** "What do you do on a Cortex-M0, which has no LDREX/STREX?"

**A.** 단일 코어라서 PRIMASK를 저장하고 끄고 RMW하고 복원한다. C11 atomics를 쓰면 GCC가 `__atomic_fetch_add_4` 같은 호출을 내는데 arm-none-eabi에는 libatomic이 없어서 링크가 안 된다 — 그 함수들을 PRIMASK로 직접 공급한다. Zephyr는 M0에서 `ATOMIC_OPERATIONS_C`로 같은 일을 한다. 더 좋은 것은 설계로 RMW를 없애는 것(쓰는 사람 하나, SPSC).

> Mask interrupts around the read-modify-write, since it's a single core. With C11 atomics, GCC emits calls like `__atomic_fetch_add_4`, and there's no libatomic in arm-none-eabi, so you get a link error; I provide those functions myself using PRIMASK save and restore, and I report `atomic_is_lock_free` as false. Zephyr does the same through `CONFIG_ATOMIC_OPERATIONS_C`. Better still is designing the RMW away — single-writer counters and SPSC rings don't need atomics at all. On a dual-M0+ part like RP2040 that isn't enough, which is why it has hardware spinlocks.

**Q.** "When do you actually need DMB, DSB or ISB on a Cortex-M4?"

**A.** 1코어 ISR ↔ 태스크의 일반 RAM 공유에는 필요 없다(컴파일러 배리어면 된다). 필요한 것은 코어 밖의 관찰자와 코어 자신의 상태가 바뀔 때다: DMA 디스크립터를 쓰고 DMA를 시작하기 전 DMB, ISR에서 주변장치 플래그를 지운 뒤 DSB 또는 다시 읽기, VTOR · CONTROL · MPU를 바꾼 뒤 DSB + ISB, WFI 전 DSB, 다른 코어와의 공유 메모리에 DMB, 코드를 복사한 뒤 DSB + ISB.

> Not for ISR-to-task sharing in normal RAM on one core — the core sees its own accesses in program order, so a compiler barrier suffices. I need them when something outside the core observes memory or when the core's own configuration changes: DMB between writing a DMA descriptor and kicking the DMA; DSB or a read-back after clearing a peripheral interrupt flag so the ISR isn't re-entered; DSB plus ISB after writing VTOR, CONTROL or the MPU; DSB before WFI; DMB for shared memory with another core; DSB plus ISB after copying code. Arm's AN321 covers these, and the FreeRTOS and Zephyr ports put barriers in exactly those places.

**Q.** "An ISR publishes a multi-word value that a task reads. How do you avoid torn reads without disabling interrupts?"

**A.** seqlock. ISR이 seq를 올리고(홀수) 값을 쓰고 seq를 올린다(짝수). 태스크는 seq → 값 → seq를 읽고 둘이 같고 짝수일 때만 쓴다. 1코어에서는 컴파일러 배리어면 된다. 실측에서 그냥 읽기는 20만 번 중 2만 3천 번 찢어졌고 seqlock은 0번, 재시도 2679번이었다. 방향이 반대(태스크가 쓰고 ISR이 읽음)면 ISR은 재시도할 수 없으니 더블 버퍼로 한다.

> A sequence counter. The ISR increments it to odd, writes the fields, increments it to even; the task reads the counter, the fields, the counter again, and keeps the copy only if both reads match and are even. On one core, compiler barriers are enough. In my QEMU test the naive reader got 23 thousand torn values out of 200 thousand, the seqlock got zero with about 2.7 thousand retries, and the ISR was never delayed. If the task writes and the ISR reads, I use a double buffer with a single index switch instead, because an ISR can't spin waiting for a task.

**Q.** "Is a lock-free queue faster than a critical-section queue on a single-core MCU?"

**A.** 평균 경로는 거의 같다(실측 836 ns vs 1028 ns/byte, QEMU 기준). 차이는 지연이다. 크리티컬 섹션은 매 연산마다 다른 인터럽트를 막고, BASEPRI 위의 zero-latency ISR에서는 커널 API를 쓸 수 없다. 그리고 진짜 속도는 묶음에서 나온다 — stream buffer를 64바이트씩 쓰면 바이트당 540 ns, 바이트마다 큐로 넘기면 7 µs.

> Per operation it's roughly a wash — about 0.84 versus 1.03 microseconds per byte in my QEMU measurement. The win is latency and reach: the lock-free ring never masks other interrupts, and it works from a zero-latency ISR above BASEPRI where RTOS calls are forbidden. The bigger performance lever is batching: a FreeRTOS stream buffer moving 64-byte chunks cost 0.54 microseconds per byte, while a queue item per byte cost 7.

---

## 12. 직접 해보기

1. **개념** — 감싸는 인덱스(0..N−1) 링에서 N개를 모두 쓰고 싶다면 무엇을 더해야 하나? 그 추가가 SPSC를 깨는 이유는?
   정답/힌트: "가득" 플래그나 `count`가 필요해지는데, 그 변수는 생산자(가득으로)와 소비자(가득 해제로)가 모두 고친다 → RMW 레이스. 그래서 감싸는 인덱스는 한 칸을 비우고, N개를 다 쓰려면 free-running으로 간다.
2. **코드** — 예제 8(`ex6_uart.c`)을 `EXTRA_CFLAGS=-DORDER=2`와 `-DORDER=1`로 다시 빌드해 돌려 보라. 결과가 같은가? ORDER 1이 통과했다고 안전하다고 할 수 있나?
   정답/힌트: 둘 다 RX 20,002~20,003, 순서 오류 0으로 통과했다(직접 확인). ORDER 1은 이번 빌드에서 컴파일러가 우연히 순서를 지켰을 뿐이다 — 예제 11처럼 주변 코드가 바뀌면 깨질 수 있다. 생성 코드(`objdump`)에서 `strb` → `str head` 순서를 확인해야 "이 빌드는 맞다"까지만 말할 수 있다.
3. **코드** — `spsc.h`에 `spsc_write(r, src, n)` / `spsc_read(r, dst, n)`(여러 바이트를 한 번에, head/tail은 마지막에 한 번만 갱신)을 추가하고 예제 9에 행을 더해 바이트당 비용을 재 보라.
   정답/힌트: 슬롯이 끝에서 감기는 경우 memcpy를 두 번으로 나눈다. 배리어(ORDER 0이면 DMB)는 n바이트에 한 번이라 바이트당 비용이 stream buffer 64 B 덩어리(540 ns)보다 낮아져야 한다.
4. **측정** — 예제 1의 TIMER0 주기를 97에서 997, 9997로 늘려 STREX 실패 횟수가 어떻게 변하는지 재 보라. 실패율(실패 ÷ ISR 수)은?
   정답/힌트: ISR 수가 줄어드는 만큼 실패도 준다. 실패율은 "ISR이 LDREX~STREX 창에 들어올 확률"이라 위상에 따라 0~50% 사이에서 결정적으로 정해진다 — 주기마다 다르게 나오는 것이 정상이다.
5. **개념** — 듀얼코어 M7+M4 칩에서 M4의 ISR이 생산자, M7의 태스크가 소비자인 링을 공유 SRAM에 둔다. `spsc.h`의 어떤 ORDER를 써야 하고, 그 밖에 무엇이 더 필요한가?
   정답/힌트: ORDER 0(또는 3). M7에 D-cache가 있으면 링 영역을 MPU로 non-cacheable(또는 shareable)로 두거나 소비자가 읽기 전 invalidate, 생산자가 쓴 뒤 clean. 상대 코어를 깨우는 것은 하드웨어 세마포어/메일박스 인터럽트 — 그 레지스터 쓰기 전에 `dmb`.
6. **코드** — 예제 5의 seqlock을 "태스크가 쓰고 ISR이 읽는" 방향으로 바꿔 돌려 보라. 무슨 일이 일어나나? 그다음 §4.3의 더블 버퍼로 고쳐라.
   정답/힌트: ISR이 태스크의 쓰기 도중(홀수 seq)에 들어오면 재시도 루프에서 빠져나오지 못한다(태스크가 다시 돌 수 없으니까). QEMU가 멈추고 timeout으로 끝난다. 더블 버퍼로 바꾸면 ISR은 인덱스를 한 번 읽고 끝난다.
7. **조사** — 쓰는 칩(예: STM32F4 · H7, nRF52, RP2040)의 레퍼런스 매뉴얼에서 ① 글로벌 exclusive monitor가 있는 메모리 영역, ② 하드웨어 세마포어 · 스핀락 블록의 유무를 찾아 표로 만들어라.
   정답/힌트: 단일 코어 칩은 보통 ①에 대한 언급이 없고(로컬 모니터만), 멀티코어 칩은 HSEM(STM32H7) · SIO spinlock(RP2040) 같은 블록을 둔다.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| lock-free | 락 없는 | 누구도 다른 쪽을 기다리게 만들지 않고, 끼어들어도 일관된 값을 보는 설계 |
| SPSC | single-producer single-consumer | 쓰는 쪽 하나 · 읽는 쪽 하나라는 사용 규칙. 인덱스마다 쓰는 사람이 하나 |
| free-running index | 감싸지 않는 인덱스 | 누적 개수로 계속 증가, 슬롯은 `& (N − 1)` |
| RMW | read-modify-write | 읽고 고치고 쓰기. 명령 세 개라 사이에 끼어들 수 있다 |
| tearing | 찢어진 읽기 | 여러 명령으로 읽는 값의 앞뒤가 다른 시점의 것 |
| LDREX / STREX | load/store exclusive | 표시하고 읽기 / 표시가 살아 있으면 쓰기(0), 아니면 실패(1) |
| CLREX | clear exclusive | 로컬 모니터 표시 지우기. LDREX 뒤 STREX 없이 빠질 때 |
| local monitor | 로컬 exclusive 모니터 | 코어 안의 상태 기계. 예외 진입/복귀에 지워진다(v7-M) |
| global monitor | 글로벌 exclusive 모니터 | 코어 밖에서 다른 마스터의 쓰기를 본다. 칩마다 다름 |
| single-copy atomicity | 단일 복사 원자성 | 정렬된 워드 이하 단일 접근은 쪼개지지 않는다 |
| compiler barrier | 컴파일러 배리어 | `asm volatile("" ::: "memory")`. 명령 0개, 컴파일러 재배치만 막음 |
| DMB | data memory barrier | 앞 접근이 뒤 접근보다 먼저 보이게 |
| DSB | data synchronization barrier | 앞 접근이 끝날 때까지 다음 명령 실행 안 함 |
| ISB | instruction synchronization barrier | 파이프라인을 비우고 새 상태로 명령을 다시 가져옴 |
| acquire / release | 획득 / 해제 순서 | 뒤 접근이 앞으로 못 옴 / 앞 접근이 뒤로 못 감 (EL F5) |
| `lda` / `stl` | load-acquire / store-release | Armv8-M Mainline의 acquire/release 명령 |
| seqlock | 시퀀스 락 | 쓰는 쪽은 seq 홀짝으로 표시, 읽는 쪽은 앞뒤 seq 비교 후 재시도 |
| double buffer | 더블 버퍼 | 복사본 둘 + 인덱스 하나를 원자적으로 바꾸기 |
| write buffer | 쓰기 버퍼 | STR을 받아 두고 CPU는 다음 명령으로 가는 코어 쪽 버퍼 |
| posted write | 게시된 쓰기 | 버스 · 브리지가 먼저 "받았다"고 답하고 나중에 전달하는 쓰기 |
| bit-banding | 비트 밴딩 | M3/M4 옵션. 별칭 주소 쓰기 하나로 비트 하나를 원자적으로 RMW |
| libatomic | 원자 연산 라이브러리 | 컴파일러가 하드웨어로 못 하는 atomic을 부르는 대상. arm-none-eabi에는 없음 |
| `ATOMIC_OPERATIONS_C` | Zephyr Kconfig | atomic_t를 C + irq lock으로 구현 (Armv6-M) |

---

## 14. 요약 & 체크리스트

단일 코어 Cortex-M에서 ISR ↔ 태스크가 깨지는 방식은 셋이다 — RMW가 쪼개지고, 여러 워드 읽기가 찢어지고, 순서가 바뀐다. 락 없이 고치는 도구는 각각 LDREX/STREX(C11 atomics), seqlock · 더블 버퍼, 그리고 소유권 + 순서 규칙을 지키는 SPSC 링버퍼다. LDREX/STREX는 예외 진입/복귀가 로컬 모니터를 지우는 Armv7-M 규칙 덕분에 ISR에 대해 정확히 동작하고(예제 1: 같은 변수든 다른 변수든 실패 8888회), M0에는 없어서 C11 atomics가 라이브러리 호출이 되고 arm-none-eabi에서는 링크조차 안 된다(예제 3) — PRIMASK로 직접 공급한다(예제 4). SPSC 링의 순서 규칙은 1코어에서는 컴파일러 배리어로 충분하지만(CPU는 자기 순서를 지킨다), 컴파일러 배리어가 없으면 GCC -O2가 실제로 순서를 바꾸고(예제 11), 코어가 둘이면 컴파일러 배리어만으로는 2천만 개 중 13만 개가 틀린다(예제 7). DMB/DSB/ISB는 1코어 ISR ↔ 태스크가 아니라 DMA · 주변장치 · 다른 코어 · VTOR/CONTROL/MPU · WFI · 코드 복사에 필요하다. 단일 코어에서 락 없는 링의 평균 비용은 PRIMASK 링과 비슷하고(836 vs 1028 ns/byte), 진짜 이득은 다른 인터럽트의 지연을 늘리지 않는 것과 zero-latency ISR에서 쓸 수 있다는 것, 그리고 묶어서 넘기는 설계다.

- [ ] SPSC 링을 화이트보드에 쓰고 가득/빔 판정과 소유권 표를 말할 수 있다
- [ ] "데이터 → head" 순서에 1코어는 컴파일러 배리어, 다코어는 release/acquire가 필요한 이유를 말할 수 있다
- [ ] `volatile`이 모자라는 두 가지(RMW 원자성, 일반 변수와의 순서)를 예로 들 수 있다
- [ ] LDREX/STREX/CLREX와 로컬 · 글로벌 모니터, 예외가 모니터를 지우는 규칙을 설명할 수 있다
- [ ] CLREX가 필요한 장면(STREX 없이 빠지는 trylock)과 필요 없는 장면(Cortex-M 컨텍스트 스위치)을 구분할 수 있다
- [ ] C11 atomics가 M3 · M33 · M0에서 무엇으로 컴파일되는지 objdump로 보일 수 있다
- [ ] M0에서 `__atomic_*_4`를 PRIMASK로 공급하고 그 한계(멀티코어 · DMA)를 말할 수 있다
- [ ] seqlock과 더블 버퍼를 언제 쓰는지(쓰는 쪽이 ISR인가 태스크인가) 고를 수 있다
- [ ] DMB/DSB/ISB가 필요한 7가지 장면을 소스 위치와 함께 말할 수 있다
- [ ] "ISR 끝에서 플래그를 지우면 두 번 들어온다"의 원인(쓰기 버퍼 · posted write)과 처방을 말할 수 있다
- [ ] FreeRTOS · Zephyr 프리미티브 중 락 없이 도는 것과 IRQ 마스크 기반인 것을 구분할 수 있다
- [ ] QEMU가 보여 줄 수 없는 것(쓰기 버퍼, CPU 재배치, DMB 비용, 캐시)과 대신 무엇으로 검증하는지 말할 수 있다

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM J2 | 메인 ↔ ISR 데이터 레이스 (찢어진 읽기, RMW) | §1 분류표, §2.3 예제 1(LDREX로 고친 RMW), §4 seqlock · 더블 버퍼 · 64비트 타임스탬프 표 — 레이스 재현과 PRIMASK · BASEPRI는 RTOS C7 | C7 · D8을 링크하고, 슈퍼 루프에서 "메인 = 유일한 쓰는 쪽"으로 나눠 atomic 없이 끝내는 예 하나 |
| BM J3 | 락 없는 링버퍼 (SPSC, 2의 거듭제곱, 가득/빔, 배리어) | §5 설계 · `spsc.h`, §6 검증 · 측정, §6.4 화이트보드 체크리스트 | D8을 링크하고 RTOS 알림 없이 슈퍼 루프가 링을 폴링하는 최소 코드 |
| BM J4 | 원자 연산 (LDREX/STREX · CLREX, C11 atomics, M0 대체) | §2 전체, §3 전체(예제 2~4), §3.5 Zephyr · FreeRTOS 구현 | D8을 링크하고 CMSIS `__LDREXW`/`__STREXW` 이름 대응표 한 줄 |
| BM J5 | 배리어가 필요한 순간 (플래그 클리어 후 DSB, VTOR · CONTROL 후 ISB, DMA 전 DMB, -O2에서만 깨짐) | §7 전체(그림 8, §7.2 장면별 코드, §7.4 예제 11, §7.6 소스 위치) | D8을 링크하고 해당 칩 레퍼런스 매뉴얼의 errata · 버스 구조(APB 브리지) 확인 방법 |
| BM B6 | 메모리 타입 · 순서 (쓰기 버퍼 · write posting, "레지스터 쓰기가 늦게 반영") | §7.1 쓰기 버퍼 · posted write 문단, §7.2 장면 1 · 2 | **Normal / Device / Strongly-ordered 메모리 타입의 정의와 MPU 속성**은 Bare-Metal이 주인 — 순서 부분은 D8 링크 |

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스 (`/var/rtoslab/src/FreeRTOS-Kernel`): `include/stream_buffer.h`(단일 writer · reader 경고, line 40~48), `stream_buffer.c`(`xTail`/`xHead` line 234~235, `xHead` 갱신 line 1058), `include/atomic.h`(인터럽트 마스크 구현, `ATOMIC_ENTER_CRITICAL`), `portable/GCC/ARM_CM3/portmacro.h`(`portMEMORY_BARRIER` = 컴파일러 배리어 line 257, `portYIELD`의 dsb/isb와 주석 line 85~94), `portable/GCC/ARM_CM3/port.c`(tickless `dsb`/`wfi`/`isb` line 596~626), `portable/GCC/ARM_CM33/non_secure/port.c`(`prvSpinTrylock`의 ldrex/clrex/strex/dmb line 2443~2482), `tasks.c`(`vTaskSuspendAll`의 `portMEMORY_BARRIER` line 3932, `vTaskGenericNotifyGiveFromISR`), `queue.c`(FromISR 크리티컬 섹션), `History.txt`(V11.3.2 = 2026-10-16)
- Zephyr v4.4.2 소스 (`/var/rtoslab/zephyrproject/zephyr`): `kernel/Kconfig`(`ATOMIC_OPERATIONS_BUILTIN`/`_ARCH`/`_C`), `arch/arm/core/cortex_m/Kconfig`(`ARMV6_M_ARMV8_M_BASELINE`, `ARMV7_M_ARMV8_M_MAINLINE`의 select), `kernel/atomic_c.c`, `include/zephyr/sys/atomic.h` · `atomic_builtin.h`, `include/zephyr/sys/spsc_lockfree.h` · `mpsc_lockfree.h`, `include/zephyr/sys/ring_buffer.h` · `lib/utils/ring_buffer.c` · `doc/kernel/data_structures/ring_buffers.rst`(Concurrency 절), `include/zephyr/sys/spsc_pbuf.h` · `lib/os/spsc_pbuf.c`, `include/zephyr/sys/mpsc_pbuf.h`, `include/zephyr/arch/arm/barrier.h`, `arch/arm/core/cortex_m/prep_c.c` · `cpu_idle.c` · `irq_manage.c` · `thread.c`, `arch/arm/core/mpu/arm_mpu.c`
- CMSIS 6 (`modules/hal/cmsis_6/CMSIS/Core/Include/core_cm3.h`): `__NVIC_DisableIRQ`의 `__DSB(); __ISB();`, `__NVIC_SystemReset`
- Arm, "ARMv7-M Architecture Reference Manual" (DDI 0403) — synchronization primitives(LDREX/STREX/CLREX, local · global monitor, context switch 시 모니터 동작), single-copy atomicity, DMB/DSB/ISB(옵션 SY)
- Arm, "ARMv6-M Architecture Reference Manual" (DDI 0419) — exclusive 명령 없음
- Arm, "Armv8-M Architecture Reference Manual" (DDI 0553) — LDA/STL/LDAEX/STLEX
- Arm, "Application Note 321 — ARM Cortex-M Programming Guide to Memory Barrier Instructions" (DAI 0321)
- GCC 문서 "Built-in Functions for Memory Model Aware Atomic Operations" (`__atomic_*`, `__atomic_always_lock_free`) — https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html
- 이 노트의 선행 · 연결 노트: RTOS C7(크리티컬 섹션 — 레이스 재현), B3, B7, B8, C10, D5, E1, E6, P4, P5, EL F4(§11 RCU, §12 seqlock), EL F5(§1 컴파일러 배리어, §2 arm64 배리어, §3.5 MP litmus, §5.4 posted write, §6 DMA 순서)
- Anduril C 문제은행 — SPSC 링버퍼 문제 (Don의 인터뷰 준비 자료)
