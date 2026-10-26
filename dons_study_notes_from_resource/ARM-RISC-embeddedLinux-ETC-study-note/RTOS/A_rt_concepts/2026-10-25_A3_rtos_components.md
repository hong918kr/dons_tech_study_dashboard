# A3. RTOS 구성 요소 지도 — 커널·포트·HAL·미들웨어를 바이트 단위로 해부하기

> **이 노트를 다 읽으면**: RTOS 펌웨어를 앱·미들웨어·커널 코어·포트·HAL 층으로 나누고 각 층이 어느 소스 파일에 있는지 짚을 수 있다 · 라이브러리형 커널과 마이크로커널(QNX·seL4·Hubris)의 차이를 비용과 격리로 설명할 수 있다 · FreeRTOSConfig.h와 Zephyr Kconfig가 바이너리를 어떻게 바꾸는지 직접 재서 보여 줄 수 있다 · map 파일로 "FLASH 18 KB 중 커널은 몇 KB, printf는 몇 KB"를 답할 수 있다
> **왜 필요한가**: 면접의 "What does an RTOS consist of?", "How small can FreeRTOS be?", "Our image doesn't fit in flash — where do you look?" 질문과, 입사 첫 주에 받는 "이 보드 펌웨어 구조 좀 파악해 와"에 답하는 지도다.
> **Don 기준 난이도**: 링커·map 파일·섹션·스택·인터럽트 벡터는 SSD 펌웨어에서 이미 안다 / FreeRTOS·Zephyr 소스 트리가 어떻게 나뉘는지, 설정 매크로 하나가 TCB·바이너리를 얼마나 바꾸는지, Zephyr의 Kconfig·DT가 커널 파일 선택까지 정한다는 점이 새롭다
> **선행 노트**: A1(실시간이란), A2(실행 모델의 진화), EL E1(커널 구조 — monolithic vs 마이크로커널), EL E2(Kconfig), EL B1–B4(툴체인·ELF). 이어지는 노트: A4(태스크 모델·TCB), C3(첫 태스크 시작), G1(FreeRTOS 구조), H2(Zephyr 빌드), B11(map 파일·코드 크기)

---

## 0. 큰 그림 — RTOS는 "부품 상자"다

Don이 SSD 컨트롤러 펌웨어에서 만든 실행부(executive)를 떠올려 보자. 메인 루프가 호스트 명령 큐를 보고, NAND 작업을 스케줄하고, 타이머 인터럽트로 타임아웃을 잡고, 코어 사이에 메시지를 넘긴다. 그 코드는 **제품 코드와 한 덩어리로 링크된 하나의 이미지**였다. 어디까지가 "OS"이고 어디부터가 "앱"인지 경계는 팀 규칙으로만 존재했다.

범용 RTOS도 본질은 같다. FreeRTOS나 Zephyr는 **"스케줄러·큐·세마포어·타이머·메모리 할당기"를 미리 만들어 둔 C 라이브러리**이고, 우리 앱과 함께 링크되어 이미지 하나가 된다. 다른 점은 두 가지다.

- 부품의 **경계와 이름이 표준화**되어 있다. 칩을 바꾸면 바뀌는 부분(포트 계층)과 바뀌지 않는 부분(커널 코어)이 파일 단위로 나뉜다.
- 무엇을 넣을지 **빌드 시점 설정**(FreeRTOSConfig.h, Zephyr Kconfig)으로 고른다. 안 쓰는 부품은 바이너리에 들어오지도 않는다.

이 노트는 그 부품 상자를 열어서, 각 부품이 **어느 파일에 있고, 서로 누구를 부르고, 몇 바이트를 차지하는지**를 실제로 빌드하고 재 본다.

```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10.0" y="10.0" width="112.0" height="46.0" rx="4" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="66" y="38" font-size="13" text-anchor="middle" font-weight="bold">애플리케이션</text>
<rect x="130.0" y="14.0" width="129.0" height="38.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="194" y="38" font-size="13" text-anchor="middle">자세 제어 태스크</text>
<rect x="267.0" y="14.0" width="129.0" height="38.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="332" y="38" font-size="13" text-anchor="middle">센서 태스크</text>
<rect x="404.0" y="14.0" width="129.0" height="38.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="468" y="38" font-size="13" text-anchor="middle">텔레메트리 태스크</text>
<rect x="541.0" y="14.0" width="129.0" height="38.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="606" y="38" font-size="13" text-anchor="middle">main()</text>
<rect x="10.0" y="64.0" width="112.0" height="46.0" rx="4" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="66" y="92" font-size="13" text-anchor="middle" font-weight="bold">미들웨어</text>
<rect x="130.0" y="68.0" width="101.6" height="38.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="181" y="92" font-size="13" text-anchor="middle">TCP/IP</text>
<rect x="239.6" y="68.0" width="101.6" height="38.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="290" y="92" font-size="13" text-anchor="middle">파일시스템</text>
<rect x="349.2" y="68.0" width="101.6" height="38.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="400" y="92" font-size="13" text-anchor="middle">USB</text>
<rect x="458.8" y="68.0" width="101.6" height="38.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="510" y="92" font-size="13" text-anchor="middle">로깅</text>
<rect x="568.4" y="68.0" width="101.6" height="38.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="619" y="92" font-size="13" text-anchor="middle">셸</text>
<rect x="10.0" y="118.0" width="112.0" height="28.0" rx="4" fill="#888" fill-opacity="0.2" stroke="#888" stroke-width="1.2"/>
<text x="66" y="137" font-size="13" text-anchor="middle" font-weight="bold">API 경계</text>
<rect x="10.0" y="154.0" width="112.0" height="112.0" rx="4" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="66" y="206" font-size="12" text-anchor="middle" font-weight="bold">커널 코어</text>
<text x="66" y="222" font-size="11" text-anchor="middle">칩과 무관한 C</text>
<rect x="10.0" y="274.0" width="112.0" height="56.0" rx="4" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c" stroke-width="1.2"/>
<text x="66" y="298" font-size="12" text-anchor="middle" font-weight="bold">포트 계층</text>
<text x="66" y="314" font-size="11" text-anchor="middle">칩·컴파일러별</text>
<rect x="130.0" y="278.0" width="129.0" height="48.0" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="194" y="298" font-size="12" text-anchor="middle" font-weight="bold">컨텍스트 스위치</text>
<text x="194" y="314" font-size="11" text-anchor="middle">PendSV</text>
<rect x="267.0" y="278.0" width="129.0" height="48.0" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="332" y="298" font-size="12" text-anchor="middle" font-weight="bold">크리티컬 섹션</text>
<text x="332" y="314" font-size="11" text-anchor="middle">BASEPRI</text>
<rect x="404.0" y="278.0" width="129.0" height="48.0" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="468" y="298" font-size="12" text-anchor="middle" font-weight="bold">tick 타이머</text>
<text x="468" y="314" font-size="11" text-anchor="middle">SysTick</text>
<rect x="541.0" y="278.0" width="129.0" height="48.0" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="606" y="298" font-size="12" text-anchor="middle" font-weight="bold">첫 태스크 시작</text>
<text x="606" y="314" font-size="11" text-anchor="middle">SVC</text>
<rect x="10.0" y="338.0" width="112.0" height="46.0" rx="4" fill="#888" fill-opacity="0.2" stroke="#888" stroke-width="1.2"/>
<text x="66" y="357" font-size="12" text-anchor="middle" font-weight="bold">HAL·BSP</text>
<text x="66" y="373" font-size="11" text-anchor="middle">드라이버</text>
<rect x="130.0" y="342.0" width="174.7" height="38.0" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="217" y="366" font-size="13" text-anchor="middle">startup · 링커</text>
<rect x="312.7" y="342.0" width="174.7" height="38.0" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="400" y="366" font-size="13" text-anchor="middle">CMSIS · 벤더 HAL</text>
<rect x="495.3" y="342.0" width="174.7" height="38.0" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="583" y="366" font-size="13" text-anchor="middle">UART · SPI · CAN</text>
<rect x="10.0" y="392.0" width="112.0" height="38.0" rx="4" fill="#888" fill-opacity="0.2" stroke="#888" stroke-width="1.2"/>
<text x="66" y="416" font-size="13" text-anchor="middle" font-weight="bold">하드웨어</text>
<rect x="130.0" y="118.0" width="540.0" height="28.0" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="400" y="136" font-size="12" text-anchor="middle">RTOS API — xTaskCreate · xQueueSend · k_sem_take · k_msgq_put …</text>
<rect x="130.0" y="158.0" width="174.7" height="48.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="217" y="178" font-size="13" text-anchor="middle" font-weight="bold">스케줄러</text>
<text x="217" y="195" font-size="12" text-anchor="middle">tasks.c · sched.c</text>
<rect x="312.7" y="158.0" width="174.7" height="48.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="400" y="178" font-size="13" text-anchor="middle" font-weight="bold">태스크 관리</text>
<text x="400" y="195" font-size="12" text-anchor="middle">tasks.c · thread.c</text>
<rect x="495.3" y="158.0" width="174.7" height="48.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="583" y="178" font-size="13" text-anchor="middle" font-weight="bold">시간 · tick</text>
<text x="583" y="195" font-size="12" text-anchor="middle">tasks.c · timeout.c</text>
<rect x="130.0" y="212.0" width="174.7" height="48.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="217" y="232" font-size="13" text-anchor="middle" font-weight="bold">IPC 객체</text>
<text x="217" y="249" font-size="12" text-anchor="middle">queue.c · msg_q.c</text>
<rect x="312.7" y="212.0" width="174.7" height="48.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="400" y="232" font-size="13" text-anchor="middle" font-weight="bold">소프트웨어 타이머</text>
<text x="400" y="249" font-size="12" text-anchor="middle">timers.c · timer.c</text>
<rect x="495.3" y="212.0" width="174.7" height="48.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="583" y="232" font-size="13" text-anchor="middle" font-weight="bold">메모리 할당자</text>
<text x="583" y="249" font-size="12" text-anchor="middle">heap_4.c · kheap.c</text>
<rect x="130.0" y="396.0" width="540.0" height="30.0" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="400" y="415" font-size="12" text-anchor="middle">Cortex-M3 · NVIC · SysTick · UART · TIMER · Flash · SRAM</text>
</svg>
```

그림 1 — RTOS 기반 펌웨어의 층. 초록 상자의 파일 이름은 "FreeRTOS 파일 · Zephyr 파일" 순서다. 위로 갈수록 칩과 무관하고, 아래로 갈수록 칩·보드마다 다시 써야 한다.

층을 한 줄씩 정의하면:

- **애플리케이션**: 우리가 쓰는 태스크들과 `main()`. 드론이라면 자세 제어 rate loop, IMU 읽기, 텔레메트리 송신.
- **미들웨어**: RTOS 위에서 도는 재사용 라이브러리. TCP/IP 스택(lwIP, FreeRTOS+TCP, Zephyr net), 파일시스템(LittleFS, FatFS), USB 스택(TinyUSB), 로깅, 셸. 커널이 아니다 — 커널 API를 쓰는 "큰 앱"이다(K 모듈).
- **커널 코어**: 스케줄러, 태스크 관리, 시간(tick), IPC 객체(큐·세마포어·뮤텍스·이벤트), 소프트웨어 타이머, 메모리 할당자. 순수 C이고 칩을 모른다.
- **포트 계층(port layer)**: CPU 아키텍처와 컴파일러에 묶인 작은 코드. 컨텍스트 스위치(Cortex-M은 PendSV), 크리티컬 섹션(BASEPRI), tick 타이머(SysTick), 첫 태스크 시작(SVC), 태스크 초기 스택 프레임 만들기.
- **HAL·BSP·드라이버**: 칩 레지스터를 만지는 코드. startup·링커 스크립트, CMSIS, 벤더 HAL, UART·SPI·CAN 드라이버. FreeRTOS는 이 층을 **제공하지 않고**(벤더 SDK를 같이 쓴다), Zephyr는 디바이스 모델과 드라이버까지 **포함한다**.

### 0.1 이 노트의 지도

| 절 | 내용 | 실행 예제 |
|---|---|---|
| §1 | 층별 부품과 "누가 누구를 부르나" | 예제 1 (오브젝트 의존 그래프) |
| §2 | 소스 트리 투어 — 파일별 줄 수 | 예제 2 |
| §3 | 커널 구조 스타일: 라이브러리형 vs 마이크로커널 | — |
| §4 | **footprint 해부** — bare-metal · FreeRTOS · Zephyr를 바이트로 | 예제 3, 4, 5, 6 |
| §5 | 컴파일 시점 구성 — 설정 하나가 바꾸는 바이트 | 예제 7, 8 |
| §6 | 커널 객체 = RAM 속 구조체 | 예제 9, 10 |
| §7 | tick과 idle — 심장 박동과 "아무 일도 없을 때" | 예제 11 |
| §8 | RTOS 커널 안: `main()`부터 첫 태스크까지 | 예제 12 (gdb 추적) |

### 실습 환경 한 줄

Lima VM `elinux`의 RTOS 랩: QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) 위에 bare-metal · FreeRTOS V11.3.2(ARM_CM3 포트) · Zephyr v4.4.2(같은 가상 보드)를 올린다. 툴체인은 arm-none-eabi-gcc 14.2, `-O2`, newlib-nano(FreeRTOS·bare-metal) / picolibc(Zephyr 기본). 시간 출력이 있는 FreeRTOS 실행은 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간, 매번 같은 숫자)로 돌렸다 — QEMU icount 기준이라 캐시·플래시 wait state를 모델링하지 않으므로 실제 칩과 절대값은 다르다. 이 노트의 핵심 숫자(바이트 크기)는 에뮬레이션과 무관하게 실제 칩 빌드와 같다.

---

## 1. RTOS 해부도 — 층마다 무엇이 있나

### 1.1 커널 코어의 여섯 부품

커널 코어는 "무엇을 관리하나"로 나누면 여섯 부품이다. 같은 부품이 FreeRTOS와 Zephyr에서 어느 파일·함수에 있는지, 그리고 Don의 SSD 펌웨어에서는 무엇에 해당했는지 표로 본다.

| 부품 | 하는 일 | FreeRTOS (파일 · 대표 함수) | Zephyr (파일 · 대표 함수) | SSD 펌웨어에서는 |
|---|---|---|---|---|
| 스케줄러 | 다음에 돌 태스크 고르기, 선점 | `tasks.c` · `vTaskSwitchContext`, `pxReadyTasksLists` | `kernel/sched.c` · `z_swap`(kswap.h), `next_up` | 메인 루프의 "다음 작업 고르기" 분기 |
| 태스크 관리 | 생성·삭제·상태·우선순위 | `tasks.c` · `xTaskCreate`, `prvInitialiseNewTask` | `kernel/thread.c` · `z_setup_new_thread` | 코어별 작업 디스크립터 테이블 |
| 시간 · tick | tick 카운트, 지연·타임아웃 | `tasks.c` · `xTaskIncrementTick`, `vTaskDelay` | `kernel/timeout.c` · `sys_clock_announce` | 1 ms 타이머 ISR + 타임아웃 배열 |
| IPC 객체 | 큐·세마포어·뮤텍스·이벤트·스트림 | `queue.c`(세마포어·뮤텍스도 큐), `event_groups.c`, `stream_buffer.c` | `sem.c`, `mutex.c`, `msg_q.c`, `events.c`, `queue.c` | 코어 간 메일박스, 완료 플래그 |
| 소프트웨어 타이머 | 콜백을 정해진 시각에 | `timers.c` · 타이머 서비스 태스크 `prvTimerTask` | `kernel/timer.c` · `k_timer` (만료 콜백은 ISR 문맥) | 타임아웃 콜백 테이블 |
| 메모리 할당자 | 커널 객체·스택용 메모리 | `portable/MemMang/heap_1~5.c` · `pvPortMalloc` | `kheap.c`, `mem_slab.c` + `lib/heap` | 고정 크기 버퍼 풀 |

두 가지가 눈에 띈다.

- FreeRTOS는 **`tasks.c` 하나에 스케줄러·태스크·tick이 다 들어 있다**(§2에서 8,967줄). Zephyr는 같은 일을 `sched.c`·`thread.c`·`timeout.c`·`timeslicing.c`로 쪼갰다.
- FreeRTOS의 세마포어와 뮤텍스는 **`queue.c`의 큐**다. 항목 크기 0인 큐가 세마포어이고, 소유자와 우선순위 상속을 붙인 큐가 뮤텍스다. 그래서 `StaticSemaphore_t`와 `StaticQueue_t`의 크기가 같다(§6에서 측정, 둘 다 84 B).

### 1.2 포트 계층 — 칩마다 다시 쓰는 유일한 부분

포트 계층은 "커널 코어가 CPU에게 부탁해야 하는 일"의 목록이다. FreeRTOS에서는 `portable/<컴파일러>/<아키텍처>/port.c` + `portmacro.h` 두 파일, Zephyr에서는 `arch/arm/core/cortex_m/` 디렉터리다.

| 포트가 제공하는 것 | 왜 칩 의존인가 | FreeRTOS ARM_CM3 | Zephyr Cortex-M |
|---|---|---|---|
| 초기 스택 프레임 | 예외 복귀 프레임 모양이 아키텍처마다 다름 | `pxPortInitialiseStack` | `arch_new_thread` (thread.c) |
| 컨텍스트 스위치 | 레지스터 저장·복원은 어셈블리 | `xPortPendSVHandler` (PendSV) | `z_arm_pendsv` (swap_helper.S) |
| 크리티컬 섹션 | 인터럽트 마스킹 방식 (BASEPRI, PRIMASK) | `vPortEnterCritical`, `portDISABLE_INTERRUPTS` | `arch_irq_lock` |
| tick 타이머 | SysTick 같은 타이머 레지스터 | `vPortSetupTimerInterrupt`, `xPortSysTickHandler` | `drivers/timer/cortex_m_systick.c` |
| 첫 태스크 시작 | MSP/PSP 전환, SVC | `prvPortStartFirstTask`, `vPortSVCHandler` | `arch_switch_to_main_thread` |
| 타입·매크로 | 스택 폭, tick 타입, 정렬 | `portmacro.h` (`portSTACK_TYPE`, `portBYTE_ALIGNMENT`) | arch 헤더 |

Don이 아는 말로 하면: **포트 계층 = "예외 진입·복귀 하드웨어를 스케줄러가 쓸 수 있게 감싼 얇은 막"**이다. 이 막이 얼마나 얇은지는 §4에서 잰다 — FreeRTOS 최소 구성에서 `port.c`는 FLASH **416 B**다. 자세한 어셈블리는 C2(컨텍스트 스위치)와 C3(첫 태스크 시작), 새 칩 포팅 절차는 G2.

### 1.3 HAL · BSP · 드라이버 — RTOS마다 "어디까지 주나"가 다르다

- **FreeRTOS**: 커널만 준다. UART를 쓰려면 ST의 HAL, NXP의 MCUXpresso SDK, 또는 직접 쓴 레지스터 코드가 필요하다. 이 랩에서는 `templates/common/board.c`(UART 출력, us 시계)와 `startup.c`(벡터 테이블, `.data` 복사)가 그 역할이다.
- **Zephyr**: 커널 + **디바이스 모델 + 드라이버 + 보드 정의**를 한 트리에 담는다. `mps2/an385` 보드를 고르면 devicetree가 UART·GPIO·타이머를 선언하고, 그에 맞는 드라이버(`uart_cmsdk_apb.c`, `gpio_cmsdk_ahb.c`, `cortex_m_systick.c`)가 자동으로 링크된다. §4에서 Zephyr hello의 FLASH 12.5 KB 중 **드라이버가 2.9 KB**인 것을 보게 된다. 이 차이가 "Zephyr hello가 FreeRTOS 최소보다 큰" 주된 이유다 — 공짜가 아니라 기능이다.

### 1.4 미들웨어 — 커널이 아닌 큰 부품

로깅, 셸, 네트워크, 파일시스템은 RTOS "배포판"에 같이 들어 있어도 **커널이 아니다**. 커널 API(스레드, 큐, 뮤텍스)를 쓰는 라이브러리다. 그래서 크기도 커널과 별개로 커진다 — §4에서 Zephyr에 `CONFIG_LOG=y`를 켜면 FLASH가 +9.1 KB, `CONFIG_SHELL=y`까지 켜면 다시 +30.3 KB가 되는데, 그때 커널 자체는 5.1 KB → 7.6 KB로만 는다.

### 1.5 누가 누구를 부르나 — 오브젝트 파일로 확인하기

층 그림은 "위가 아래를 부른다"고 말한다. 정말 그런지, 거꾸로 부르는 곳은 없는지 링커가 가진 정보로 확인해 보자. 각 `.o`의 **정의한 심볼**(`nm --defined-only`)과 **필요한 심볼**(`nm -u`)을 맞추면 파일 사이의 호출 그래프가 나온다.

#### 예제 1 — FreeRTOS 오브젝트 사이의 의존 그래프

무엇을 확인하나: §4의 예제 5(큐·타이머·이벤트 그룹을 쓰는 FreeRTOS 앱)를 파일마다 따로 컴파일한 `.o`들 사이에서, 어느 파일이 어느 파일의 심볼을 몇 개 필요로 하는지.

```python
#!/usr/bin/env python3
"""예제 1: 오브젝트 파일끼리 '누가 누구를 부르나' — nm -u(필요한 심볼)와 nm --defined-only(제공 심볼)로 그래프를 만든다"""
import subprocess, sys, os, collections
d = sys.argv[1]
objs = ['main', 'tasks', 'queue', 'list', 'timers', 'event_groups', 'port', 'heap_4', 'hooks', 'board', 'startup']
def nm(args, o):
    out = subprocess.run(['arm-none-eabi-nm'] + args + [os.path.join(d, o + '.o')],
                         capture_output=True, text=True).stdout
    return {l.split()[-1] for l in out.splitlines() if l.strip()}
defs = {o: nm(['-g', '--defined-only'], o) for o in objs}
owner = {s: o for o, ss in defs.items() for s in ss}
for o in objs:
    need = collections.defaultdict(list)
    for s in sorted(nm(['-u'], o)):
        need[owner.get(s, 'libc/libgcc')].append(s)
    if not need: continue
    print(f'{o}.o ->')
    for tgt, ss in sorted(need.items(), key=lambda kv: -len(kv[1])):
        show = ', '.join(ss[:4]) + (f' ... (+{len(ss)-4})' if len(ss) > 4 else '')
        print(f'    {tgt:12s} {len(ss):2d}  {show}')
```

```sh
limactl shell elinux -- bash -lc 'python3 -I /Users/donh/.rtos_scratch/A3/deps.py /var/tmp/rtos/A3/fr_full/obj'
```

```text
main.o ->
    tasks         5  vTaskGetRunTimeStatistics, vTaskListTasks, vTaskStartScheduler, xTaskCreate ... (+1)
    queue         5  xQueueCreateMutex, xQueueGenericCreate, xQueueGenericSend, xQueueReceive ... (+1)
    board         3  board_exit, board_printf_raw, board_puts
    event_groups  3  xEventGroupCreate, xEventGroupSetBits, xEventGroupWaitBits
    timers        2  xTimerCreate, xTimerGenericCommandFromTask
    heap_4        1  xPortGetFreeHeapSize
tasks.o ->
    port          6  pxPortInitialiseStack, vPortEndScheduler, vPortEnterCritical, vPortExitCritical ... (+2)
    list          5  uxListRemove, vListInitialise, vListInitialiseItem, vListInsert ... (+1)
    libc/libgcc   4  memset, snprintf, stpcpy, strlen
    board         3  board_exit, board_now_us, board_printf_raw
    heap_4        2  pvPortMalloc, vPortFree
    timers        2  xTimerCreateTimerTask, xTimerGetTimerDaemonTaskHandle
    hooks         1  vApplicationStackOverflowHook
queue.o ->
    tasks        15  pvTaskIncrementMutexHeldCount, uxTaskGetNumberOfTasks, vTaskInternalSetTimeOutState, vTaskMissedYield ... (+11)
    port          3  vPortEnterCritical, vPortExitCritical, vPortValidateInterruptPriority
    board         2  board_exit, board_printf_raw
    heap_4        2  pvPortMalloc, vPortFree
    libc/libgcc   1  memcpy
    list          1  vListInitialise
timers.o ->
    tasks         6  vApplicationGetTimerTaskMemory, vTaskSuspendAll, xTaskCreateStatic, xTaskGetSchedulerState ... (+2)
    queue         6  vQueueAddToRegistry, vQueueWaitForMessageRestricted, xQueueGenericCreateStatic, xQueueGenericSend ... (+2)
    list          4  uxListRemove, vListInitialise, vListInitialiseItem, vListInsert
    board         2  board_exit, board_printf_raw
    heap_4        2  pvPortMalloc, vPortFree
    port          2  vPortEnterCritical, vPortExitCritical
event_groups.o ->
    tasks         6  uxTaskResetEventItemValue, vTaskPlaceOnUnorderedEventList, vTaskRemoveFromUnorderedEventList, vTaskSuspendAll ... (+2)
    board         2  board_exit, board_printf_raw
    heap_4        2  pvPortMalloc, vPortFree
    port          2  vPortEnterCritical, vPortExitCritical
    list          1  vListInitialise
    timers        1  xTimerPendFunctionCallFromISR
port.o ->
    tasks         3  pxCurrentTCB, vTaskSwitchContext, xTaskIncrementTick
    board         2  board_exit, board_printf_raw
heap_4.o ->
    board         2  board_exit, board_printf_raw
    port          2  vPortEnterCritical, vPortExitCritical
    tasks         2  vTaskSuspendAll, xTaskResumeAll
    libc/libgcc   1  memset
    hooks         1  vApplicationMallocFailedHook
hooks.o ->
    board         2  board_exit, board_printf_raw
board.o ->
    libc/libgcc   4  __heap_end, __heap_start, _impure_ptr, setvbuf
startup.o ->
    libc/libgcc   8  _ebss, _edata, _estack, _sbss ... (+4)
    board         4  board_exit, board_hardfault, board_init, board_printf_raw
    main          1  main
```

출력에서 볼 것: `queue.o`가 `tasks.o`의 함수를 **15개** 필요로 한다 — 큐는 "기다리는 태스크를 이벤트 리스트에 걸고 깨우는" 일을 전부 `tasks.c`에 맡긴다(`vTaskPlaceOnEventList`, `xTaskRemoveFromEventList` 같은 것). 그리고 `port.o`가 `tasks.o`에서 딱 3개(`pxCurrentTCB`, `vTaskSwitchContext`, `xTaskIncrementTick`)만 가져간다 — 포트 계층이 커널 코어에 거꾸로 닿는 **유일한 통로**다.

```svg
<svg viewBox="0 0 680 462" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="280.0" y="10.0" width="120.0" height="38.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="340" y="34" font-size="13" text-anchor="middle">main.c (앱)</text>
<rect x="40.0" y="98.0" width="150.0" height="38.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="115" y="122" font-size="13" text-anchor="middle">event_groups.c</text>
<rect x="250.0" y="98.0" width="140.0" height="38.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="320" y="122" font-size="13" text-anchor="middle">timers.c</text>
<rect x="490.0" y="98.0" width="150.0" height="38.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="565" y="122" font-size="13" text-anchor="middle">queue.c</text>
<rect x="200.0" y="192.0" width="280.0" height="50.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="340" y="213" font-size="13" text-anchor="middle" font-weight="bold">tasks.c</text>
<text x="340" y="230" font-size="12" text-anchor="middle">스케줄러 · 태스크 · tick · 이벤트 리스트</text>
<rect x="20.0" y="306.0" width="120.0" height="38.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="80" y="330" font-size="13" text-anchor="middle">list.c</text>
<rect x="170.0" y="306.0" width="120.0" height="38.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="230" y="330" font-size="13" text-anchor="middle">heap_4.c</text>
<rect x="330.0" y="306.0" width="150.0" height="38.0" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="405" y="330" font-size="13" text-anchor="middle">port.c</text>
<rect x="520.0" y="306.0" width="140.0" height="38.0" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.2"/>
<text x="590" y="330" font-size="13" text-anchor="middle">libc</text>
<rect x="290.0" y="410.0" width="230.0" height="38.0" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="405" y="434" font-size="13" text-anchor="middle">SysTick · PendSV · SVC 예외</text>
<line x1="300" y1="48" x2="115" y2="96" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="204" y="65" font-size="12" text-anchor="start">3</text>
<line x1="320" y1="48" x2="320" y2="96" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="326" y="77" font-size="12" text-anchor="start">2</text>
<line x1="380" y1="48" x2="565" y2="96" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="476" y="65" font-size="12" text-anchor="start">5</text>
<line x1="398" y1="48" x2="452" y2="190" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="429" y="164" font-size="12" text-anchor="start">5</text>
<line x1="115" y1="136" x2="260" y2="190" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="184" y="156" font-size="12" text-anchor="start">6</text>
<line x1="320" y1="136" x2="320" y2="190" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="326" y="168" font-size="12" text-anchor="start">6</text>
<line x1="565" y1="136" x2="470" y2="190" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="536" y="166" font-size="12" text-anchor="start">15</text>
<line x1="390" y1="117" x2="488" y2="117" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="434" y="111" font-size="12" text-anchor="start">6</text>
<line x1="230" y1="242" x2="80" y2="304" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="161" y="278" font-size="12" text-anchor="start">5</text>
<line x1="270" y1="242" x2="230" y2="304" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="236" y="278" font-size="12" text-anchor="start">2</text>
<line x1="380" y1="242" x2="380" y2="304" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="386" y="278" font-size="12" text-anchor="start">6</text>
<line x1="450" y1="242" x2="590" y2="304" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="526" y="278" font-size="12" text-anchor="start">4</text>
<path d="M460,306 C470,280 470,262 455,244" fill="none" stroke="currentColor" stroke-width="1.2" stroke-dasharray="4,3" marker-end="url(#ah2)"/>
<text x="474" y="272" font-size="12" text-anchor="start">3</text>
<line x1="405" y1="410" x2="405" y2="346" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<text x="412" y="384" font-size="12" text-anchor="start">벡터 테이블이 직접 부름</text>
<text x="20" y="372" font-size="12" text-anchor="start">port.c → tasks.c 3개: pxCurrentTCB · vTaskSwitchContext ·</text>
<text x="20" y="390" font-size="12" text-anchor="start">xTaskIncrementTick (점선). 숫자 = 필요한 심볼 수</text>
</svg>
```

그림 2 — 예제 1의 결과를 그린 호출 그래프. 화살표 숫자는 "필요한 심볼 수". 서비스(event_groups·timers·queue)는 모두 `tasks.c`로 모이고, `tasks.c`만 포트·리스트·힙을 부른다. 점선은 포트가 커널로 올라오는 세 심볼, 맨 아래는 하드웨어 예외가 벡터 테이블을 통해 포트를 직접 부르는 길이다.

이 그래프에서 기억할 구조:

- **위에서 아래로**: 앱 → 서비스(큐·타이머·이벤트) → `tasks.c` → `list.c`·힙·포트. 
- **아래에서 위로는 딱 두 통로**: (1) 하드웨어 예외(SysTick, PendSV, SVC)가 벡터 테이블로 포트의 핸들러를 부르고, (2) 포트가 `xTaskIncrementTick`(tick마다)과 `vTaskSwitchContext`(전환마다)로 커널에게 "결정해 달라"고 묻는다. 결정(어느 태스크?)은 커널이, 실행(레지스터 저장·복원)은 포트가 한다.
- `timers.c`는 `xTaskCreateStatic`과 큐 API를 쓴다 — 소프트웨어 타이머는 **커널 위에 지은 서비스**(전용 태스크 + 명령 큐)라는 증거다. §5에서 `configUSE_TIMERS=1` 하나가 FLASH 2 KB를 더하는 이유가 이것이다.

### 1.6 흔한 함정

- 커널 파일이 `board_printf_raw`·`board_exit`를 부르는 것은 커널 설계가 아니라 **랩 템플릿의 `configASSERT` 매크로** 때문이다(FreeRTOSConfig.h에서 `configASSERT(x)`를 "출력 후 종료"로 정의했다). 실제 제품에서도 `configASSERT`가 무엇을 부르느냐에 따라 커널이 printf·로그 코드를 끌고 들어온다. 크기가 중요한 빌드라면 이 매크로부터 본다.
- `tasks.o`가 `snprintf`를 필요로 하는 것도 커널 본질이 아니다. `configUSE_STATS_FORMATTING_FUNCTIONS=1`이 켜는 `vTaskListTasks`·`vTaskGetRunTimeStatistics`가 원인이다(§4.4에서 이것이 libc를 2.8 KB 늘리는 것을 잰다).

---

## 2. 소스 트리 투어 — 실제 줄 수로

### 2.1 FreeRTOS-Kernel의 생김새

FreeRTOS 커널은 **놀랄 만큼 작은 트리**다. 랩의 V11.3.2(2026-10-16 릴리스, `History.txt` 첫 항목) 기준:

```
FreeRTOS-Kernel/
  tasks.c            scheduler, TCB, ready/delayed lists, tick, idle task, notifications
  queue.c            queues + semaphores + mutexes (all built on one queue type)
  list.c             doubly linked list used by every kernel list
  timers.c           software timers (timer service task + command queue)
  event_groups.c     event bits
  stream_buffer.c    stream / message buffers
  croutine.c         co-routines (legacy, not built in this lab)
  include/           FreeRTOS.h (config defaults), task.h, queue.h, semphr.h ...
  portable/
    GCC/ARM_CM3/     port.c + portmacro.h   <- the only CPU-specific code
    GCC/ARM_CM4F/, ARM_CM33/, RISC-V/ ...   (one folder per compiler x arch)
    MemMang/         heap_1.c .. heap_5.c   <- pick exactly one
```

| 파일 | 하는 일 | 언제 링크되나 |
|---|---|---|
| `tasks.c` | 스케줄러, TCB, ready/delayed/suspended 리스트, tick 처리, idle 태스크, 태스크 알림 | 항상 |
| `list.c` | 모든 커널 리스트의 이중 연결 리스트 연산(`vListInsert`, `uxListRemove`) | 항상 |
| `queue.c` | 큐, 바이너리·카운팅 세마포어, 뮤텍스, 재귀 뮤텍스, 큐 셋 | 큐·세마포어·뮤텍스를 쓰거나 타이머를 켜면 |
| `timers.c` | 소프트웨어 타이머, 타이머 서비스(데몬) 태스크 | `configUSE_TIMERS=1` |
| `event_groups.c` | 이벤트 비트 대기·설정 | `configUSE_EVENT_GROUPS=1`(기본 1)이고 API를 부르면 |
| `stream_buffer.c` | 스트림·메시지 버퍼(태스크 하나 ↔ 하나 바이트 흐름) | `configUSE_STREAM_BUFFERS=1`(기본 1)이고 API를 부르면 |
| `portable/.../port.c` | 포트 계층(§1.2) | 항상, 정확히 하나 |
| `portable/MemMang/heap_N.c` | `pvPortMalloc`·`vPortFree` 구현 | 동적 할당을 쓰면 정확히 하나 (F1) |

"API를 부르면 링크된다"는 말은 `-ffunction-sections`(함수마다 섹션)와 `--gc-sections`(안 쓰는 섹션 버리기) 조합 덕분이다. 파일을 빌드 목록에 넣어도 부르지 않는 함수는 바이너리에 남지 않는다. 랩 Makefile은 6개 커널 파일을 **항상** 컴파일하지만, §4의 최소 구성 바이너리에는 `tasks.c`·`list.c`·`port.c`·`heap_1.c` 코드만 남는다.

### 2.2 예제 2 — 파일별 줄 수 세기

무엇을 확인하나: 각 커널 파일의 전체 줄 수(wc -l과 같음)와, 빈 줄·주석만 있는 줄을 뺀 "코드 줄". FreeRTOS 소스는 주석이 아주 많아서 둘의 차이가 크다.

```python
#!/usr/bin/env python3
"""예제 2: 커널 소스 파일별 줄 수 — 전체 줄(wc -l)과 '코드 줄'(빈 줄·주석만 있는 줄 제외)"""
import re, sys, glob, os
def count(fn):
    src = open(fn, errors='replace').read()
    total = src.count('\n')
    nocom = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), src, flags=re.S)
    nocom = re.sub(r'//[^\n]*', '', nocom)
    code = sum(1 for l in nocom.splitlines() if l.strip())
    return total, code
def table(title, base, pats):
    files = sorted({f for p in pats for f in glob.glob(os.path.join(base, p))})
    rows = [(os.path.relpath(f, base),) + count(f) for f in files]
    rows.sort(key=lambda r: -r[2])
    print(f'## {title}')
    for name, t, c in rows: print(f'  {name:34s} {t:6d} {c:6d}')
    print(f'  {"TOTAL (" + str(len(rows)) + " files)":34s} {sum(r[1] for r in rows):6d} {sum(r[2] for r in rows):6d}')
K = '/var/rtoslab/src/FreeRTOS-Kernel'
table('FreeRTOS kernel core (.c)        lines   code', K, ['*.c'])
table('FreeRTOS port + heap', K, ['portable/GCC/ARM_CM3/*', 'portable/MemMang/heap_[14].c'])
table('FreeRTOS public headers', K, ['include/*.h'])
Z = '/var/rtoslab/zephyrproject/zephyr'
if '--zephyr' in sys.argv:
    table('Zephyr kernel/*.c (top 12 shown below)', Z, ['kernel/*.c'])
    table('Zephyr arch/arm/core/cortex_m', Z, ['arch/arm/core/cortex_m/*.c', 'arch/arm/core/cortex_m/*.S'])
```

```sh
limactl shell elinux -- bash -lc 'python3 -I /Users/donh/.rtos_scratch/A3/loc.py --zephyr'
```

```text
## FreeRTOS kernel core (.c)        lines   code
  tasks.c                              8967   5540
  queue.c                              3400   2192
  stream_buffer.c                      1757   1139
  timers.c                             1345    816
  event_groups.c                        887    530
  croutine.c                            407    206
  list.c                                248     95
  TOTAL (7 files)                     17011  10518
## FreeRTOS port + heap
  portable/MemMang/heap_4.c             638    376
  portable/GCC/ARM_CM3/port.c           822    373
  portable/GCC/ARM_CM3/portmacro.h      265    147
  portable/MemMang/heap_1.c             178     82
  TOTAL (4 files)                      1903    978
## FreeRTOS public headers
  include/FreeRTOS.h                   3371   2319
  include/task.h                       3899    470
  include/mpu_prototypes.h              498    404
  ...
  include/semphr.h                     1215     67
  include/message_buffer.h              967     58
  ...
  TOTAL (21 files)                    18562   4989
## Zephyr kernel/*.c (top 12 shown below)
  kernel/mmu.c                         1837   1169
  kernel/sched.c                       1637   1100
  kernel/thread.c                      1367    957
  kernel/userspace.c                   1158    771
  kernel/work.c                        1322    726
  kernel/poll.c                         820    583
  kernel/init.c                         652    425
  kernel/msg_q.c                        536    401
  kernel/queue.c                        505    345
  kernel/usage.c                        484    318
  kernel/mem_domain.c                   424    306
  kernel/events.c                       448    290
  ...
  TOTAL (51 files)                    17856  11660
## Zephyr arch/arm/core/cortex_m
  arch/arm/core/cortex_m/fault.c       1156    606
  arch/arm/core/cortex_m/thread.c       744    418
  arch/arm/core/cortex_m/arm-m-switch.c    610    346
  arch/arm/core/cortex_m/svc.S          483    241
  arch/arm/core/cortex_m/swap_helper.S    369    195
  ...
  TOTAL (26 files)                     5917   3035
```

(헤더 목록과 Zephyr 파일 목록은 중간을 `...`로 줄였다.)

출력에서 볼 것: FreeRTOS 커널 전체가 **코드 1만 줄**, 포트는 **373줄**이다. `task.h`는 3,899줄인데 코드는 470줄 — 나머지는 API 문서 주석이다. `FreeRTOS.h`가 코드 2,319줄로 큰 것은 **모든 설정 매크로의 기본값과 정합성 검사**(`#ifndef configUSE_TIMERS` … `#error`)가 여기 있기 때문이다. 즉 FreeRTOS의 "설정 시스템"은 이 헤더 하나다(§5).

```svg
<svg viewBox="0 0 680 486" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" text-anchor="start" font-weight="bold">FreeRTOS V11.3.2 커널 (코드 줄 = 빈 줄·주석 제외)</text>
<text x="172" y="45" font-size="12" text-anchor="end">tasks.c</text>
<rect x="180.0" y="34.0" width="415.5" height="14.0" rx="1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="600" y="46" font-size="12" text-anchor="start">5,540</text>
<text x="172" y="65" font-size="12" text-anchor="end">queue.c</text>
<rect x="180.0" y="54.0" width="164.4" height="14.0" rx="1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="349" y="66" font-size="12" text-anchor="start">2,192</text>
<text x="172" y="85" font-size="12" text-anchor="end">stream_buffer.c</text>
<rect x="180.0" y="74.0" width="85.4" height="14.0" rx="1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="270" y="86" font-size="12" text-anchor="start">1,139</text>
<text x="172" y="105" font-size="12" text-anchor="end">timers.c</text>
<rect x="180.0" y="94.0" width="61.2" height="14.0" rx="1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="246" y="106" font-size="12" text-anchor="start">816</text>
<text x="172" y="125" font-size="12" text-anchor="end">event_groups.c</text>
<rect x="180.0" y="114.0" width="39.8" height="14.0" rx="1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="225" y="126" font-size="12" text-anchor="start">530</text>
<text x="172" y="145" font-size="12" text-anchor="end">heap_4.c</text>
<rect x="180.0" y="134.0" width="28.2" height="14.0" rx="1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="213" y="146" font-size="12" text-anchor="start">376</text>
<text x="172" y="165" font-size="12" text-anchor="end">port.c (ARM_CM3)</text>
<rect x="180.0" y="154.0" width="28.0" height="14.0" rx="1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="213" y="166" font-size="12" text-anchor="start">373</text>
<text x="172" y="185" font-size="12" text-anchor="end">list.c</text>
<rect x="180.0" y="174.0" width="7.1" height="14.0" rx="1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="192" y="186" font-size="12" text-anchor="start">95</text>
<text x="10" y="214" font-size="13" text-anchor="start" font-weight="bold">Zephyr v4.4.2 kernel/ 와 Cortex-M arch</text>
<text x="172" y="239" font-size="12" text-anchor="end">arch/arm/core/cortex_m</text>
<rect x="180.0" y="228.0" width="227.6" height="14.0" rx="1" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c" stroke-width="1.2"/>
<text x="413" y="240" font-size="12" text-anchor="start">3,035</text>
<text x="172" y="259" font-size="12" text-anchor="end">sched.c</text>
<rect x="180.0" y="248.0" width="82.5" height="14.0" rx="1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="268" y="260" font-size="12" text-anchor="start">1,100</text>
<text x="172" y="279" font-size="12" text-anchor="end">thread.c</text>
<rect x="180.0" y="268.0" width="71.8" height="14.0" rx="1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="257" y="280" font-size="12" text-anchor="start">957</text>
<text x="172" y="299" font-size="12" text-anchor="end">work.c</text>
<rect x="180.0" y="288.0" width="54.4" height="14.0" rx="1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="239" y="300" font-size="12" text-anchor="start">726</text>
<text x="172" y="319" font-size="12" text-anchor="end">init.c</text>
<rect x="180.0" y="308.0" width="31.9" height="14.0" rx="1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="217" y="320" font-size="12" text-anchor="start">425</text>
<text x="172" y="339" font-size="12" text-anchor="end">msg_q.c</text>
<rect x="180.0" y="328.0" width="30.1" height="14.0" rx="1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="215" y="340" font-size="12" text-anchor="start">401</text>
<text x="172" y="359" font-size="12" text-anchor="end">events.c</text>
<rect x="180.0" y="348.0" width="21.8" height="14.0" rx="1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="207" y="360" font-size="12" text-anchor="start">290</text>
<text x="172" y="379" font-size="12" text-anchor="end">timer.c</text>
<rect x="180.0" y="368.0" width="21.5" height="14.0" rx="1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="207" y="380" font-size="12" text-anchor="start">287</text>
<text x="172" y="399" font-size="12" text-anchor="end">timeout.c</text>
<rect x="180.0" y="388.0" width="19.5" height="14.0" rx="1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="204" y="400" font-size="12" text-anchor="start">260</text>
<text x="172" y="419" font-size="12" text-anchor="end">mutex.c</text>
<rect x="180.0" y="408.0" width="15.3" height="14.0" rx="1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="200" y="420" font-size="12" text-anchor="start">204</text>
<text x="172" y="439" font-size="12" text-anchor="end">sem.c</text>
<rect x="180.0" y="428.0" width="12.2" height="14.0" rx="1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="197" y="440" font-size="12" text-anchor="start">162</text>
<text x="172" y="459" font-size="12" text-anchor="end">idle.c</text>
<rect x="180.0" y="448.0" width="3.7" height="14.0" rx="1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="189" y="460" font-size="12" text-anchor="start">49</text>
<text x="180" y="482" font-size="12" text-anchor="start">FreeRTOS 커널 .c 7개 = 10,518줄 · Zephyr kernel/*.c 51개 = 11,660줄</text>
</svg>
```

그림 3 — 예제 2의 코드 줄 수. FreeRTOS는 `tasks.c` 하나가 커널의 절반이고, Zephyr는 비슷한 총량을 여러 파일로 나눴다. 주황 막대(Cortex-M arch 전체 3,035줄)는 Zephyr 포트 계층이 FreeRTOS `port.c`(373줄)보다 8배 큰 것을 보여 준다 — fault 해독, MPU, TrustZone, 전원 관리까지 포함하기 때문이다.

### 2.3 Zephyr 트리에서 커널이 차지하는 자리

Zephyr 저장소는 커널보다 훨씬 크다. 커널 관련 위치만 추리면:

| 위치 | 내용 |
|---|---|
| `kernel/` | 스케줄러(`sched.c`), 스레드(`thread.c`), 부팅(`init.c`: `z_cstart`), 타임아웃(`timeout.c`), IPC(`sem.c`, `mutex.c`, `msg_q.c`, `events.c`, `queue.c`, `pipe.c`), 타이머(`timer.c`), 워크큐(`work.c`), 힙·슬랩(`kheap.c`, `mem_slab.c`), idle(`idle.c`) |
| `kernel/include/` | 커널 내부 헤더: `ksched.h`(스케줄러 내부 API), `kswap.h`(`z_swap` — 문맥 전환 진입점), `wait_q.h`, `timeout_q.h` |
| `include/zephyr/kernel.h` 등 | 공개 API (`k_sem_take`, `K_THREAD_DEFINE` …) |
| `arch/arm/core/cortex_m/` | 포트 계층: `reset.S`, `prep_c.c`, `swap_helper.S`(PendSV), `thread.c`(`arch_new_thread`), `fault.c`, `svc.S` |
| `drivers/` | 디바이스 드라이버 (`drivers/timer/cortex_m_systick.c`가 tick 타이머) |
| `subsys/`, `lib/` | 미들웨어(logging, shell, net, fs, usb, settings …)와 공용 라이브러리(cbprintf, sys_heap) |
| `boards/`, `soc/`, `dts/` | 보드·SoC 정의와 devicetree |

Zephyr에서 중요한 사실 하나: **51개 커널 파일 중 실제로 링크되는 것은 설정과 사용에 따라 다르다.** §4의 Zephyr hello는 `init.c`·`sched.c`·`timeout.c`·`timeslicing.c`·`thread.c`·`fatal.c`·`device.c`·`idle.c`·`banner.c` 9개만 바이트를 냈다. `sem.c`는 아무도 세마포어를 쓰지 않아서 0바이트다. 큐·뮤텍스·이벤트·타이머를 쓰는 예제 6의 Zephyr 앱에서는 `msg_q.c`·`mutex.c`·`events.c`·`timer.c`·`kheap.c`가 추가된다.

Linux와 비교하면 규모 감각이 생긴다: EL E1에서 Linux `drivers/`만 2,518만 줄이었다. FreeRTOS 커널 코드 1만 줄은 그 0.04%다. "RTOS 커널은 한 사람이 일주일에 다 읽을 수 있다"는 말이 과장이 아니다 — C12에서 실제로 읽는다.

---

## 3. 커널 구조 스타일 — 라이브러리형 vs 마이크로커널

monolithic(Linux)·마이크로커널·RTOS의 기본 비교는 **EL E1 §1**에서 했다. 여기서는 MCU 세계에서 실제로 만나는 세 가지 스타일을 **"UART에 한 줄 쓰기"가 어떤 길을 지나는가**로 다시 본다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="110" y="22" font-size="13" text-anchor="middle" font-weight="bold">라이브러리형 커널</text>
<text x="110" y="40" font-size="12" text-anchor="middle">FreeRTOS · Zephyr 기본</text>
<text x="340" y="22" font-size="13" text-anchor="middle" font-weight="bold">마이크로커널</text>
<text x="340" y="40" font-size="12" text-anchor="middle">QNX · seL4</text>
<text x="570" y="22" font-size="13" text-anchor="middle" font-weight="bold">정적 분리형</text>
<text x="570" y="40" font-size="12" text-anchor="middle">Hubris (Oxide)</text>
<rect x="10.0" y="52.0" width="200.0" height="196.0" rx="4" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="110" y="70" font-size="12" text-anchor="middle">한 이미지 · 한 주소 공간</text>
<rect x="30.0" y="80.0" width="160.0" height="32.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="110" y="101" font-size="13" text-anchor="middle">태스크 A</text>
<line x1="110" y1="112" x2="110" y2="148" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<text x="116" y="134" font-size="12" text-anchor="start">uart_write() 호출</text>
<rect x="30.0" y="150.0" width="160.0" height="32.0" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="110" y="171" font-size="13" text-anchor="middle">UART 드라이버 함수</text>
<rect x="30.0" y="196.0" width="160.0" height="40.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="110" y="221" font-size="13" text-anchor="middle">커널: 스케줄러 · 큐</text>
<rect x="250.0" y="60.0" width="90.0" height="44.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="295" y="78" font-size="12" text-anchor="middle">클라이언트</text>
<text x="295" y="94" font-size="12" text-anchor="middle">프로세스</text>
<rect x="360.0" y="60.0" width="100.0" height="44.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="410" y="78" font-size="12" text-anchor="middle">UART 서버</text>
<text x="410" y="94" font-size="12" text-anchor="middle">프로세스</text>
<line x1="240" y1="128" x2="460" y2="128" stroke="currentColor" stroke-width="1.2" stroke-dasharray="5,4"/>
<text x="458" y="122" font-size="12" text-anchor="end">특권 경계</text>
<rect x="250.0" y="176.0" width="210.0" height="44.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="355" y="202" font-size="12" text-anchor="middle">커널: IPC · 스케줄 · MMU</text>
<line x1="290" y1="104" x2="290" y2="174" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<line x1="330" y1="176" x2="395" y2="106" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<text x="296" y="162" font-size="12" text-anchor="start">send</text>
<text x="370" y="150" font-size="12" text-anchor="start">전달</text>
<text x="355" y="240" font-size="12" text-anchor="middle">응답도 커널을 거쳐 돌아온다</text>
<rect x="480.0" y="60.0" width="85.0" height="44.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="522" y="78" font-size="12" text-anchor="middle">태스크 A</text>
<text x="522" y="94" font-size="12" text-anchor="middle">MPU 영역</text>
<rect x="580.0" y="60.0" width="90.0" height="44.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="625" y="78" font-size="12" text-anchor="middle">UART 태스크</text>
<text x="625" y="94" font-size="12" text-anchor="middle">MPU 영역</text>
<rect x="480.0" y="176.0" width="190.0" height="44.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="575" y="202" font-size="12" text-anchor="middle">작은 커널: IPC · MPU</text>
<line x1="522" y1="104" x2="522" y2="174" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<line x1="560" y1="176" x2="620" y2="106" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah3)"/>
<text x="528" y="162" font-size="12" text-anchor="start">send</text>
<text x="575" y="240" font-size="12" text-anchor="middle">태스크 목록은 빌드 때 고정</text>
<text x="110" y="280" font-size="12" text-anchor="middle">가장 빠름: 함수 호출 1번</text>
<text x="110" y="300" font-size="12" text-anchor="middle">격리 없음: 버그 = 전체 리셋</text>
<text x="340" y="280" font-size="12" text-anchor="middle">격리 강함: 서버만 재시작</text>
<text x="340" y="300" font-size="12" text-anchor="middle">IPC마다 문맥 전환 + 복사</text>
<text x="570" y="280" font-size="12" text-anchor="middle">MPU 격리 + 동적 생성 없음</text>
<text x="570" y="300" font-size="12" text-anchor="middle">런타임에 태스크 추가 불가</text>
</svg>
```

그림 4 — MCU에서 만나는 커널 구조 세 가지. 라이브러리형은 드라이버 호출이 그냥 함수 호출이다. 마이크로커널은 같은 일이 "보내기 → 커널 → 서버 → 응답"이 된다. Hubris 같은 정적 분리형은 MPU로 태스크를 격리하되 태스크 목록을 빌드 때 고정한다.

### 3.1 라이브러리형 커널 (FreeRTOS, Zephyr 기본, ThreadX, embOS …)

- **정의**: 커널이 앱과 같은 이미지, 같은 주소 공간, 보통 같은 특권 레벨에서 도는 C 라이브러리. "시스템 콜"은 그냥 함수 호출이다(`xQueueSend`는 `queue.c`의 함수일 뿐).
- **장점**: 호출 비용이 함수 호출 + 크리티컬 섹션 수준으로 가장 싸다. 메모리가 가장 적다. 구조가 단순해서 디버거로 따라가기 쉽다.
- **단점**: 격리가 없다. 텔레메트리 태스크의 포인터 버그가 자세 제어 태스크의 스택을 덮어쓴다. 결함 = 시스템 전체 리셋.
- **보완책**: MPU를 쓰는 선택 기능 — FreeRTOS-MPU(restricted task, G4), Zephyr `CONFIG_USERSPACE`(memory domain, syscall 검증, F5). 켜는 순간 라이브러리형에서 "부분 격리형"으로 옮겨 가고, API 호출이 SVC를 통하는 진짜 시스템 콜이 된다(C8).

### 3.2 마이크로커널 (QNX Neutrino, seL4 …)

- **정의**: 커널에는 스레드·주소 공간·IPC·스케줄링만 남기고, 드라이버·파일시스템·네트워크를 **별도 프로세스(서버)**로 돌린다. 통신은 메시지 전달(message passing).
- **장점**: 드라이버가 죽어도 그 서버만 재시작한다. 커널이 작아 검증이 쉽다 — seL4는 형식 검증(formal verification)된 마이크로커널로 유명하다.
- **단점**: 서버 호출마다 IPC + 문맥 전환(+ 주소 공간 전환)이 든다. MMU가 있는 프로세서(Cortex-A/R급)가 전제인 경우가 많다.
- **어디서 만나나**: 차량(QNX 기반 인포테인먼트·ADAS 도메인 컨트롤러), 국방·항공 고신뢰 시스템. Rivian 같은 회사의 SoC 쪽 소프트웨어 대화에서 나온다.

### 3.3 정적 분리형 — Hubris (개념만)

Oxide Computer가 서버 관리 컨트롤러(Cortex-M)용으로 만든 Rust RTOS다. 마이크로커널처럼 태스크끼리 **동기식 메시지(send/receive/reply)**로만 통신하고 MPU로 서로를 격리한다. 그런데 **태스크 목록·메모리 배치를 빌드 때 고정**하고 런타임 태스크 생성이 없다. 죽은 태스크는 감독(supervisor) 태스크가 재시작한다. "MCU에서도 마이크로커널의 격리를 얻되, 동적인 부분을 전부 빌드 시점으로 옮긴다"는 설계다. 이 노트에서는 개념만 다루고, Rust RTOS는 I 모듈에서 본다.

### 3.4 비교 표

| 항목 | 라이브러리형 (FreeRTOS · Zephyr 기본) | 마이크로커널 (QNX · seL4) | 정적 분리형 (Hubris) |
|---|---|---|---|
| 주소 공간 | 하나 (전부 공유) | 프로세스마다 (MMU) | 태스크마다 MPU 영역 |
| 드라이버 위치 | 앱과 같은 이미지의 함수 | 유저 공간 서버 프로세스 | 별도 태스크 |
| 서비스 호출 | 함수 호출 | IPC + 문맥 전환 | IPC + 문맥 전환 |
| 결함의 영향 | 시스템 전체 (리셋) | 그 서버만, 재시작 가능 | 그 태스크만, 재시작 가능 |
| 런타임 태스크 생성 | 가능 (`xTaskCreate`) | 가능 (프로세스 생성) | 없음 (빌드 때 고정) |
| 메모리 비용 | 가장 작다 (수 KB) | 크다 (MMU, 페이지 테이블) | 중간 (MPU 정렬 낭비) |
| 인증·검증 | 작아서 쉬운 편 (SafeRTOS 등 인증판) | seL4는 형식 검증 | 설계상 분석 쉬움 |
| 대표 사용처 | 대부분의 MCU 제품 | 차량·항공·고신뢰 | 서버 관리 컨트롤러 |

### 3.5 Don 경험과 연결

Don의 멀티코어 SSD 컨트롤러는 사실 두 스타일이 섞여 있었다: **한 코어 안에서는 라이브러리형**(같은 이미지, 함수 호출)이고, **코어 사이에서는 메시지 전달**(호스트 인터페이스 코어 → FTL 코어 → NAND 코어로 디스크립터를 넘기는 메일박스)이었다. 코어 사이 메시지 방식이 마이크로커널의 IPC와 같은 장단점을 가진다 — 격리와 병렬성을 얻고, 메시지 오버헤드와 지연을 낸다. 면접에서 "마이크로커널이 뭐냐"를 물으면 이 경험으로 설명하면 된다.

### 3.6 흔한 함정

- "Zephyr는 마이크로커널"이라는 오해. 초기 Zephyr(1.x 초반)에 nanokernel·microkernel이라는 이름의 API가 있었고 1.6에서 통합 커널로 합쳐졌다(소스 트리의 1.6 릴리스 노트). 이름과 별개로 기본 구성은 **한 주소 공간의 라이브러리형**이다. `CONFIG_USERSPACE`를 켜야 커널/유저 경계가 생긴다.
- "RTOS는 시스템 콜이 없다"는 반만 맞다. 기본은 함수 호출이지만, MPU 격리를 켜면 SVC 기반 시스템 콜이 생기고 호출 비용이 크게 는다(C8, F5).

---

## 4. Footprint 해부 — 이 노트의 핵심

"FreeRTOS는 몇 KB냐"는 질문의 정직한 답은 **"설정과 앱에 따라 2 KB부터 수십 KB까지, 그리고 커널보다 printf가 더 크다"**다. 이 절에서 그 문장을 숫자로 증명한다.

### 4.1 FLASH와 RAM을 무엇으로 재나

`arm-none-eabi-size`가 보여 주는 세 숫자(EL B 모듈에서 ELF 섹션은 이미 봤다)를 MCU 메모리에 대응시키면:

- **text**: 코드 + 읽기 전용 데이터(`.rodata`, 벡터 테이블). FLASH에만 산다.
- **data**: 초기값 있는 전역 변수. 초기값이 FLASH에 실리고, 부팅 때 `Reset_Handler`가 RAM으로 복사한다. **FLASH와 RAM 둘 다** 쓴다.
- **bss**: 0으로 시작하는 전역 변수. RAM만 쓴다. **커널 객체·태스크 스택·힙 배열이 대부분 여기**다.

그래서 이 노트의 정의는 **FLASH = text + data**, **RAM = data + bss**다. 한 가지 주의: 랩 링커 스크립트는 `main()`과 인터럽트용 MSP 스택 4 KB(`_Main_Stack_Size = 0x1000`)를 RAM 맨 끝에 **심볼로만** 예약하므로 `size`의 bss에 잡히지 않는다. 반면 Zephyr는 인터럽트 스택(4 KB)과 main 스레드 스택(1 KB)을 bss/noinit 변수로 선언하므로 RAM 숫자에 들어간다. 두 RTOS의 RAM을 비교할 때 FreeRTOS 쪽에 4 KB를 더해서 봐야 공정하다.

```
mps2-an385 memory as the lab linker script lays it out
0x00000000 +-----------------------+  FLASH (4 MB, SSRAM1)
           | .isr_vector  192 B    |
           | .text  code+rodata    |
           | .data load image      |  <- copied to RAM at reset
           +-----------------------+
0x20000000 +-----------------------+  RAM (4 MB)
           | .data                 |
           | .bss   kernel vars,   |
           |        ucHeap[],      |
           |        static stacks  |
           | (newlib sbrk heap)    |
0x203FF000 | MSP stack 4 KB        |  <- not counted by size(1)
0x20400000 +-----------------------+  _estack
```

### 4.2 예제 3 — bare-metal hello 세 가지와 printf 함정

무엇을 확인하나: RTOS 없이 "hello" 한 줄을 찍는 데 드는 크기를 세 가지 출력 방법으로 비교한다. (a) 랩의 작은 포맷터 `board_printf_raw`, (b) newlib-nano `printf`, (c) `printf`로 `%f`(float)까지.

```c
/* 예제 3-a: RTOS 없는 bare-metal hello — 랩의 작은 포맷터(board_printf_raw)만 쓴다 */
#include "board.h"

int main(void)
{
    board_printf_raw("bare-metal hello, now=%u us\n", board_now_us());
    return 0;
}
```

```c
/* 예제 3-b: 같은 hello를 newlib-nano printf로. PRINT_FLOAT=1이면 %f까지 */
#include <stdio.h>
#include "board.h"

int main(void)
{
#if PRINT_FLOAT
    volatile float t = 36.5f;
    printf("bare-metal hello, now=%lu us, temp=%.2f\n",
           (unsigned long)board_now_us(), (double)t);
#else
    printf("bare-metal hello, now=%lu us\n", (unsigned long)board_now_us());
#endif
    return 0;
}
```

```sh
S=/Users/donh/.rtos_scratch/A3; O=/var/tmp/rtos/A3; M=/var/rtoslab/templates/baremetal-mps2/Makefile
make -s -f $M SRCS=$S/bm_hello.c OUT=$O/bm_hello && /var/rtoslab/bin/rtos-run-mps2 $O/bm_hello/app.elf
make -s -f $M SRCS=$S/bm_printf.c OUT=$O/bm_printf && /var/rtoslab/bin/rtos-run-mps2 $O/bm_printf/app.elf
make -s -f $M SRCS=$S/bm_printf.c OUT=$O/bm_printf_f EXTRA_CFLAGS=-DPRINT_FLOAT=1 \
     EXTRA_LDFLAGS="-u _printf_float" && /var/rtoslab/bin/rtos-run-mps2 $O/bm_printf_f/app.elf
```

```text
   text	   data	    bss	    dec	    hex	filename
   4152	     96	    328	   4576	   11e0	/var/tmp/rtos/A3/bm_hello/app.elf
bare-metal hello, now=161 us
[board_exit] code=0
   text	   data	    bss	    dec	    hex	filename
   7628	     96	    328	   8052	   1f74	/var/tmp/rtos/A3/bm_printf/app.elf
bare-metal hello, now=176 us
[board_exit] code=0
   text	   data	    bss	    dec	    hex	filename
  21000	    460	    332	  21792	   5520	/var/tmp/rtos/A3/bm_printf_f/app.elf
bare-metal hello, now=172 us, temp=36.50
[board_exit] code=0
```

출력에서 볼 것: 같은 한 줄인데 FLASH가 **4.2 KB → 7.7 KB → 21.5 KB**다. `%f` 하나가 13.7 KB를 더했다. 무엇이 그렇게 큰지 `nm`으로 보자(`-t d`는 주소·크기를 10진수로).

```sh
arm-none-eabi-nm -S --size-sort -t d $O/bm_printf_f/app.elf | grep -iE " [tT] " | tail -10
```

```text
00008924 00000588 T _vfprintf_r
00018496 00000596 T __aeabi_dmul
00018496 00000596 T __muldf3
00000980 00000630 T __adddf3
00000980 00000630 T __aeabi_dadd
00000976 00000634 T __aeabi_dsub
00000976 00000634 T __subdf3
00010576 00000652 T _printf_i
00016384 00000728 T __sfvwrite_r
00004204 00004200 T _dtoa_r
```

가장 큰 것은 `_dtoa_r`(double → 10진 문자열 변환) **4,200 B**, 그다음이 `__aeabi_dmul`·`__aeabi_dadd` 같은 **소프트웨어 double 연산**(libgcc)이다. Cortex-M3에는 FPU가 없고, C의 가변 인자 규칙상 `float`은 `double`로 올라가서 넘어가므로 printf의 `%f`는 double 소프트웨어 연산 라이브러리 전체를 끌고 온다. (같은 이름이 두 번 나오는 것은 별칭(alias) 심볼이다 — 주소가 같다.)

그리고 (a)의 4.2 KB도 순수하지 않다. 아래에서 소개할 `fp.py`로 쪼개 보면:

```text
== bm_hello
component                      FLASH     RAM
libc                            3140     420
board/startup                    904       4
strings(merged)                  180       0
app                               24       0
TOTAL                           4248     424
```

printf를 안 썼는데 libc가 **3,140 B**다. 범인은 랩 `board.c`의 `board_init()` 안에 있는 `setvbuf(stdout, NULL, _IONBF, 0)` 한 줄이다(printf 출력을 즉시 내보내려고 넣은 것). 이 한 줄이 newlib의 stdio 초기화(`__sinit`, `__sfp`), `fflush`, 그리고 `malloc`·`free`까지 끌고 온다. 예제 1에서 `board.o → setvbuf` 의존이 보였던 그것이다. 교훈: **libc 함수 하나가 "그 함수 크기"만큼만 드는 게 아니라 그 함수가 부르는 그래프 전체만큼 든다.** 이 libc 3.1 KB는 아래 FreeRTOS 빌드에도 모두 똑같이 들어 있으니, 커널 크기를 읽을 때 빼고 봐야 한다.

### 4.3 측정 도구 — map 파일을 컴포넌트별로 합산하기

`size`는 총합만 준다. 컴포넌트별로 나누려면 **링커 map 파일**을 읽어야 한다. map 파일의 "Linker script and memory map" 부분은 출력 섹션(`.text`, `.bss` …) 안에 어떤 입력 섹션이 어느 오브젝트에서 몇 바이트로 들어왔는지 한 줄씩 적는다(map 읽기 자체는 B11에서 자세히). 이것을 오브젝트 이름으로 묶으면 된다.

한 가지 준비가 필요했다: 랩의 FreeRTOS Makefile은 모든 소스를 gcc 한 번에 넘겨서, map에 오브젝트 이름 대신 `/tmp/ccXXXX.o` 같은 임시 이름이 찍힌다. 그래서 **같은 플래그로 파일마다 `obj/<이름>.o`를 만드는** 작은 빌드 스크립트를 썼다.

```sh
#!/bin/bash
# 템플릿 Makefile과 같은 플래그로 빌드하되, 파일마다 obj/<이름>.o를 만들어 map에 파일 이름이 남게 한다.
#   fbuild.sh <OUT> <rtos|bm> "<app srcs>" [HEAP=heap_4] [EXTRA_CFLAGS...]
set -e
OUT=$1; KIND=$2; SRCS=$3; HEAP=${HEAP:-heap_4}; shift 3
TPL=/var/rtoslab/templates; K=/var/rtoslab/src/FreeRTOS-Kernel
CF="-mthumb -mcpu=cortex-m3 -O2 -g3 -std=gnu11 -Wall -Wextra -ffunction-sections -fdata-sections"
INC="$(for s in $SRCS; do echo -I$(dirname $s); done | sort -u) -I$TPL/common"
ALL="$SRCS $TPL/common/startup.c $TPL/common/board.c"
if [ "$KIND" = rtos ]; then
  INC="$INC -I$TPL/freertos-mps2 -I$K/include -I$K/portable/GCC/ARM_CM3"
  ALL="$ALL $TPL/freertos-mps2/hooks.c $K/tasks.c $K/list.c $K/queue.c $K/timers.c $K/event_groups.c"
  ALL="$ALL $K/stream_buffer.c $K/portable/MemMang/$HEAP.c $K/portable/GCC/ARM_CM3/port.c"
fi
rm -rf $OUT; mkdir -p $OUT/obj
for s in $ALL; do arm-none-eabi-gcc $CF $INC "$@" -c $s -o $OUT/obj/$(basename ${s%.c}).o; done
arm-none-eabi-gcc $CF $OUT/obj/*.o -T $TPL/common/mps2_m3.ld -nostartfiles --specs=nano.specs \
  -Wl,--gc-sections -Wl,-Map=$OUT/app.map $LDX -o $OUT/app.elf
arm-none-eabi-size $OUT/app.elf | tail -1
```

그리고 map 파서. 핵심 규칙은 네 줄이다: (1) 디버그 섹션은 건너뛴다, (2) 주소가 `0x20000000` 아래면 FLASH, 위면 RAM, (3) RAM에 있으면서 NOBITS가 아닌 섹션(`.data`)은 FLASH에도 센다, (4) 문자열 리터럴은 링커가 병합(`.str1.1`)해서 한 오브젝트에 몰아 적으므로 따로 `strings(merged)`로 센다.

```python
#!/usr/bin/env python3
"""map 파일을 읽어 FLASH/RAM 바이트를 '컴포넌트'별로 합산한다.
   usage: fp.py app.map [--detail]  -> 컴포넌트, FLASH, RAM 표 (내림차순)"""
import re, sys, collections
RAM0 = 0x20000000
SKIP = ('.debug', '.comment', '.ARM.attributes', '.stab', '.rel', '.note')
def comp(path, sec):
    p = path
    if path.startswith('*fill*'): return 'padding'
    m = re.search(r'\(([^)]+)\)$', p)               # archive(member)
    mem = m.group(1) if m else p.rsplit('/', 1)[-1]
    mem = re.sub(r'\.(c\.obj|S\.obj|obj|o)$', '', mem)
    if 'libgcc' in p: return 'libgcc'
    if 'libc_nano' in p or 'libg_nano' in p or 'picolibc' in p or '/libc.a' in p or 'lib/libc/' in p: return 'libc'
    if '/obj/' in p:                                  # 내 FreeRTOS/bare-metal 빌드
        if mem.startswith('heap_'): return 'heap(' + mem + ')'
        if mem in ('startup', 'board', 'hooks'): return 'board/startup'
        if mem in ('tasks', 'queue', 'list', 'timers', 'event_groups', 'stream_buffer', 'port'):
            return mem + '.c'
        return 'app'
    if 'kernel/libkernel.a' in p: return 'kernel'
    if '/arch/' in p: return 'arch(port)'
    if '/drivers/' in p: return 'drivers'
    if 'app/libapp.a' in p: return 'app'
    if 'libzephyr.a' in p:                            # Zephyr 공통 라이브러리: 파일 이름으로 분류
        for k in ('log', 'shell', 'cbprintf', 'printk', 'heap', 'rb', 'timeutil'):
            if mem.startswith(k): return 'lib:' + k
        return 'lib:other'
    if 'subsys' in p: return 'subsys'
    return 'other'
def nobits(fn):
    import subprocess
    elf = fn[:-4] + '.elf'
    out = subprocess.run(['arm-none-eabi-readelf', '-SW', elf], capture_output=True, text=True).stdout
    return {l.split(']')[1].split()[0] for l in out.splitlines() if 'NOBITS' in l and ']' in l}
def parse(fn, detail):
    flash = collections.Counter(); ram = collections.Counter(); nb = nobits(fn)
    out_sec = None; loaded = False; vma = 0; pend = None
    started = False
    for line in open(fn, errors='replace'):
        if line.startswith('Linker script and memory map'): started = True; continue
        if not started: continue
        m = re.match(r'^([._a-zA-Z][^\s]*)(?:\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)(.*))?$', line)
        if m and not line.startswith(' '):
            out_sec = m.group(1); vma = int(m.group(2) or '0', 16)
            loaded = out_sec not in nb
            continue
        if out_sec is None or out_sec.startswith(SKIP): continue
        m = re.match(r'^ (\S+)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$', line)
        if not m:
            m2 = re.match(r'^ (\S+)\s*$', line); pend = m2.group(1) if m2 else pend; continue
        sec = m.group(1) or pend or ''
        addr, size, path = int(m.group(2), 16), int(m.group(3), 16), m.group(4).strip()
        if size == 0: continue
        if sec == '*fill*': path = '*fill*'
        c = 'strings(merged)' if '.str1.' in sec else comp(path, sec)
        if detail and c in ('libc', 'kernel', 'lib:other', 'other', 'arch(port)', 'drivers', 'subsys'):
            mm = re.search(r'\(([^)]+)\)$', path); c += ':' + (mm.group(1) if mm else path.rsplit('/',1)[-1])
        if addr < RAM0: flash[c] += size
        else:
            ram[c] += size
            if loaded: flash[c] += size
    return flash, ram
if __name__ == '__main__':
    f, r = parse(sys.argv[1], '--detail' in sys.argv)
    keys = sorted(set(f) | set(r), key=lambda k: -(f[k] + r[k]))
    print(f"{'component':28s} {'FLASH':>7s} {'RAM':>7s}")
    for k in keys: print(f"{k:28s} {f[k]:7d} {r[k]:7d}")
    print(f"{'TOTAL':28s} {sum(f.values()):7d} {sum(r.values()):7d}")
```

검증: Zephyr hello에서 이 스크립트의 FLASH 합계가 `west build`가 출력한 FLASH(12,556 B)와 정확히 같았고, RAM은 6,107 B로 `size`(6,111 B)와 4바이트 차이(정렬 채움)였다. 실제로 만들면서 밟은 함정 두 개: (1) Ubuntu의 newlib-nano는 `libc_nano.a`가 아니라 **`libg_nano.a`**로 링크되어 처음엔 libc가 "other"로 분류됐다. (2) GNU ld map은 `.bss` 줄에도 `load address`를 찍어서, 그걸로 data/bss를 가르면 힙 배열 4 KB가 FLASH로 잘못 셌다 — 그래서 ELF의 섹션 타입(NOBITS)으로 판정한다.

### 4.4 예제 4 — FreeRTOS 최소 구성

무엇을 확인하나: 선점 스케줄러 + 태스크 2개 + `vTaskDelay`만 있는 FreeRTOS가 실제로 몇 바이트이고, 그중 커널 몫이 얼마인지. 앱 폴더에 자기 `FreeRTOSConfig.h`를 두면 템플릿 설정 대신 그것이 쓰인다.

```c
/* A3 최소 구성: 선점 스케줄러 + 태스크 + vTaskDelay만. 나머지 기능은 꺼 둔다.
 * 실험용으로 각 항목을 #ifndef로 감싸 -D로 하나씩 켤 수 있게 했다. */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include "board.h"
#define configCPU_CLOCK_HZ                      BOARD_CPU_HZ
#define configTICK_RATE_HZ                      1000
#define configUSE_PREEMPTION                    1
#define configMAX_PRIORITIES                    4
#define configMINIMAL_STACK_SIZE                128         /* words = 512 B */
#define configMAX_TASK_NAME_LEN                 8
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#ifndef configTOTAL_HEAP_SIZE
#define configTOTAL_HEAP_SIZE                   ( 4 * 1024 )
#endif
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0
#ifndef configUSE_TIMERS
#define configUSE_TIMERS                        0
#endif
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                4
#define configTIMER_TASK_STACK_DEPTH            128
#ifndef configUSE_MUTEXES
#define configUSE_MUTEXES                       0
#endif
#ifndef configUSE_TRACE_FACILITY
#define configUSE_TRACE_FACILITY                0
#endif
#ifndef configUSE_TASK_NOTIFICATIONS
#define configUSE_TASK_NOTIFICATIONS            0
#endif
#ifndef configCHECK_FOR_STACK_OVERFLOW
#define configCHECK_FOR_STACK_OVERFLOW          0
#endif
#ifndef configGENERATE_RUN_TIME_STATS
#define configGENERATE_RUN_TIME_STATS           0
#endif
#define portGET_RUN_TIME_COUNTER_VALUE()        board_now_us()
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
#define configUSE_EVENT_GROUPS                  0
#define configUSE_STREAM_BUFFERS                0
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configKERNEL_INTERRUPT_PRIORITY         255
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    0x20
#define configCHECK_HANDLER_INSTALLATION        0   /* configASSERT 없이 켜 두면 경고 */
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelete                     0
#define INCLUDE_vTaskSuspend                    0
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
#endif
```

```c
/* 예제 4: FreeRTOS 최소 구성 — 태스크 2개가 번갈아 깨어나며 출력하고 끝낸다 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static void worker(void *arg)
{
    const char *name = arg;
    for (int i = 0; i < 3; i++) {
        board_printf_raw("[%u ms] %s i=%d\n", (unsigned)xTaskGetTickCount(), name, i);
        vTaskDelay(pdMS_TO_TICKS(name[0] == 'A' ? 10 : 15));
    }
    if (name[0] == 'B') {
        board_printf_raw("free heap = %u B\n", (unsigned)xPortGetFreeHeapSize());
        board_exit(0);
    }
    for (;;) vTaskDelay(portMAX_DELAY);       /* 태스크는 return하면 안 된다 (C3) */
}

int main(void)
{
    xTaskCreate(worker, "A", configMINIMAL_STACK_SIZE, "A", 2, NULL);
    xTaskCreate(worker, "B", configMINIMAL_STACK_SIZE, "B", 1, NULL);
    vTaskStartScheduler();                    /* 여기서 돌아오지 않는다 */
    return 1;
}
```

```sh
S=/Users/donh/.rtos_scratch/A3
HEAP=heap_1 $S/fbuild.sh /var/tmp/rtos/A3/fr_min rtos $S/fr_min/main.c
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A3/fr_min/app.elf
python3 -I $S/fp.py /var/tmp/rtos/A3/fr_min/app.map
```

```text
   6656	    100	   4628	  11384	   2c78	/var/tmp/rtos/A3/fr_min/app.elf
[0 ms] A i=0
[0 ms] B i=0
[10 ms] A i=1
[15 ms] B i=1
[20 ms] A i=2
[30 ms] B i=2
free heap = 2360 B
[board_exit] code=0
component                      FLASH     RAM
heap(heap_1)                     132    4104
libc                            3140     420
tasks.c                         1728     196
board/startup                    880       4
port.c                           416       4
strings(merged)                  196       0
app                              148       0
list.c                           116       0
TOTAL                           6756    4728
```

출력에서 볼 것: 전체 FLASH 6,756 B 중 **커널 코드는 `tasks.c` 1,728 + `port.c` 416 + `list.c` 116 + `heap_1.c` 132 = 2,392 B**다. 나머지는 bare-metal에도 있던 libc(3,140)와 보드 코드다. RAM 4,728 B의 대부분은 `heap_1`의 정적 배열 `ucHeap[4096]`이고, 커널 자체 변수(`tasks.c`의 ready 리스트·tick 카운터·현재 TCB 포인터)는 **196 B**뿐이다. 그 힙에서 태스크 A·B와 IDLE이 각자 스택 512 B + TCB를 받아 가고 2,360 B가 남았다(§6.4에서 배치를 그린다).

첫 빌드에서 경고가 하나 났었다: `port.c:284: warning: unused variable 'pxVectorTable'`. V11의 `configCHECK_HANDLER_INSTALLATION`(기본 1)은 벡터 테이블에 FreeRTOS 핸들러가 제대로 들어 있는지 `configASSERT`로 검사하는데, 최소 구성에서 `configASSERT`를 정의하지 않으니 검사용 변수만 남은 것이다. 위 설정처럼 0으로 끄거나 `configASSERT`를 정의하면 사라진다. 경고 0개가 규칙이므로 끄고 다시 빌드했다.

### 4.5 예제 5 — FreeRTOS "많이 켠" 구성: 큐 + 타이머 + 이벤트 그룹 + 뮤텍스

무엇을 확인하나: 실제 제품에 가까운 구성 — 랩 템플릿 `FreeRTOSConfig.h`(타이머·뮤텍스·trace·런타임 통계·스택 검사 켬, heap_4) — 에서 커널 각 파일의 몫. 앱 구조는 드론의 센서 → 제어 → 텔레메트리 흐름을 흉내 냈다.

```c
/* 예제 5: FreeRTOS "많이 켠" 구성(랩 템플릿 config) — 큐 + 소프트웨어 타이머 + 이벤트 그룹 + 뮤텍스.
 * 센서 타이머(10 ms)가 큐로 샘플을 보내고, 제어 태스크가 받아 처리, 5개마다 이벤트 비트를 세운다. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "event_groups.h"
#include "semphr.h"
#include "board.h"

static QueueHandle_t q;
static EventGroupHandle_t ev;
static SemaphoreHandle_t log_mtx;
#define EV_BATCH (1u << 0)

static void sensor_cb(TimerHandle_t t)                 /* 타이머 서비스 태스크 안에서 돈다 */
{
    static uint32_t n;
    (void)t;
    uint32_t sample = n++;
    xQueueSend(q, &sample, 0);
}

static void control_task(void *arg)
{
    (void)arg;
    uint32_t s;
    for (;;) {
        xQueueReceive(q, &s, portMAX_DELAY);           /* 데이터가 올 때까지 Blocked */
        if ((s % 5) == 4) xEventGroupSetBits(ev, EV_BATCH);
    }
}

static void telemetry_task(void *arg)
{
    (void)arg;
    static char buf[512];
    for (int k = 0; k < 4; k++) {
        xEventGroupWaitBits(ev, EV_BATCH, pdTRUE, pdTRUE, portMAX_DELAY);
        xSemaphoreTake(log_mtx, portMAX_DELAY);
        board_printf_raw("[%u ms] batch %d\n", (unsigned)xTaskGetTickCount(), k);
        xSemaphoreGive(log_mtx);
    }
    vTaskListTasks(buf, sizeof buf);                    /* configUSE_TRACE_FACILITY + snprintf */
    board_puts("Name          State Prio Stack Num\n"); board_puts(buf);
    vTaskGetRunTimeStatistics(buf, sizeof buf);         /* configGENERATE_RUN_TIME_STATS */
    board_puts("Task          Abs(us)   %\n"); board_puts(buf);
    board_printf_raw("free heap = %u B, ticks = %u\n",
                     (unsigned)xPortGetFreeHeapSize(), (unsigned)xTaskGetTickCount());
    board_exit(0);
}

int main(void)
{
    q = xQueueCreate(8, sizeof(uint32_t));
    ev = xEventGroupCreate();
    log_mtx = xSemaphoreCreateMutex();
    TimerHandle_t t = xTimerCreate("sensor", pdMS_TO_TICKS(10), pdTRUE, NULL, sensor_cb);
    xTimerStart(t, 0);
    xTaskCreate(control_task, "control", 256, NULL, 3, NULL);
    xTaskCreate(telemetry_task, "telem", 512, NULL, 1, NULL);
    vTaskStartScheduler();
    return 1;
}
```

먼저 템플릿 그대로(heap_4, 힙 128 KB) 빌드·실행:

```sh
HEAP=heap_4 $S/fbuild.sh /var/tmp/rtos/A3/fr_full rtos $S/fr_full/main.c
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A3/fr_full/app.elf
```

```text
  18824	    100	 135928	 154852	  25ce4	/var/tmp/rtos/A3/fr_full/app.elf
[50 ms] batch 0
[100 ms] batch 1
[150 ms] batch 2
[200 ms] batch 3
Name          State Prio Stack Num
telem          	X	1	464	2
IDLE           	R	0	230	3
Tmr Svc        	B	7	474	4
control        	B	3	224	1
Task          Abs(us)   %
telem          	1155		<1%
IDLE           	198924		98%
Tmr Svc        	784		<1%
control        	437		<1%
free heap = 127440 B, ticks = 202
[board_exit] code=0
```

출력에서 볼 것: 앱이 만든 적 없는 태스크가 둘 있다 — **`IDLE`**(스케줄러가 만든 idle 태스크)과 **`Tmr Svc`**(`configUSE_TIMERS=1`이 만든 타이머 서비스 태스크, 우선순위 7 = 최고). 센서 콜백은 이 `Tmr Svc` 안에서 돈다. 그리고 200 ms 동안 CPU의 **98%를 IDLE이** 썼다(§7). RAM 135,928 B bss의 대부분은 템플릿의 128 KB 힙 배열이고 실제로는 3.6 KB만 썼다(128 KB − 127,440 B). 현실적인 비교를 위해 힙을 8 KB로 줄여 다시 빌드하고 나눠 본다.

```sh
HEAP=heap_4 $S/fbuild.sh /var/tmp/rtos/A3/fp/fr_full8k rtos $S/fr_full/main.c -DconfigTOTAL_HEAP_SIZE=8192
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A3/fp/fr_full8k/app.elf | tail -2
python3 -I $S/fp.py /var/tmp/rtos/A3/fp/fr_full8k/app.map
```

```text
  18824	    100	  13048	  31972	   7ce4	/var/tmp/rtos/A3/fp/fr_full8k/app.elf
free heap = 4560 B, ticks = 202
[board_exit] code=0
component                      FLASH     RAM
tasks.c                         5401    3592
heap(heap_4)                     688    8220
libc                            5992     420
queue.c                         2208      64
timers.c                        1156     304
board/startup                    972       4
app                              432     528
strings(merged)                  781       0
port.c                          712       9
event_groups.c                   460       0
list.c                           116       0
TOTAL                           18918   13141
```

출력에서 볼 것:

- 커널(포트 포함) FLASH = 5,401 + 2,208 + 1,156 + 688 + 712 + 460 + 116 = **10,741 B**. 최소 구성(2,392 B)의 4.5배다. `tasks.c`만 1,728 → 5,401 B로 컸는데, trace·통계·알림·뮤텍스 상속·스택 검사 코드가 다 `tasks.c` 안에 `#if`로 들어 있기 때문이다.
- `tasks.c`의 RAM 3,592 B의 대부분은 `configKERNEL_PROVIDED_STATIC_MEMORY=1`일 때 커널이 **정적으로 잡아 두는 IDLE 태스크와 타이머 태스크의 스택·TCB**다(스택 256 words = 1,024 B, 512 words = 2,048 B, TCB 96 B × 2). 나머지는 ready·delayed 리스트 같은 커널 변수다. 정적이라 힙에서 빠지지 않는다.
- libc가 3,140 → **5,992 B**. 늘어난 2,852 B는 `vTaskListTasks`가 부르는 `snprintf` 계열이다. 커널 기능 하나(`configUSE_STATS_FORMATTING_FUNCTIONS`)가 libc를 끌고 온 사례다.

### 4.6 예제 6 — Zephyr: hello, 같은 앱, 그리고 로깅·셸

무엇을 확인하나: 같은 가상 보드에서 Zephyr의 크기. 먼저 `printk` 한 줄짜리 hello, 다음으로 예제 5와 **같은 구조의 앱**(타이머 → 메시지 큐 → 제어 → 이벤트 → 텔레메트리)을 Zephyr API로, 마지막으로 거기에 로깅과 셸을 켠 것.

hello의 `main.c`(prj.conf는 빈 파일 — 보드 defconfig만 적용):

```c
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void)
{
	printk("hello from Zephyr main thread, tick=%u Hz\n",
	       CONFIG_SYS_CLOCK_TICKS_PER_SEC);
	return 0;
}
```

```sh
/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/A3/z_hello /var/tmp/rtos/A3/z_hello
arm-none-eabi-size /var/tmp/rtos/A3/z_hello/zephyr/zephyr.elf
timeout 5 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none \
    -serial stdio -kernel /var/tmp/rtos/A3/z_hello/zephyr/zephyr.elf
```

```text
           FLASH:       12556 B         4 MB      0.30%
             RAM:        6112 B         4 MB      0.15%
   text	   data	    bss	    dec	    hex	filename
  12388	    168	   5943	  18499	   4843	/var/tmp/rtos/A3/z_hello/zephyr/zephyr.elf
*** Booting Zephyr OS build v4.4.2 ***
hello from Zephyr main thread, tick=100 Hz
qemu-system-arm: terminating on signal 15 from pid 762490 (timeout)
```

`fp.py`로 나누면(커널·드라이버는 `--detail`로 파일까지):

```text
component                      FLASH     RAM
kernel                          2836    5850
drivers                         2946     216
arch(port)                      2522       5
libc                            1982      32
strings(merged)                 1268       0
lib:heap                         454       0
other                            384       0
lib:printk                       106       4
lib:other                         38       0
app                               20       0
TOTAL                          12556    6107
```

출력에서 볼 것: Zephyr hello의 **커널 코드는 2,836 B로 FreeRTOS 최소 구성의 `tasks.c`+`list.c`+`heap`(1,976 B)과 같은 급**이다. 전체가 12.5 KB로 큰 이유는 드라이버 2.9 KB(UART, GPIO 4포트, pinctrl, SysTick), arch 2.5 KB(MPU 설정 632 B, fault 해독 920 B 포함), 그리고 `printk`가 끌고 온 picolibc 포맷터(`vflprintf.c` 1,808 B)다. "tick=100 Hz"는 Kconfig의 `default 100 if QEMU_TARGET` 때문이다 — 실제 칩에서 tickless 커널의 기본값은 10,000이다(`kernel/Kconfig`의 `SYS_CLOCK_TICKS_PER_SEC`).

RAM 6,111 B는 거의 전부 스택이다. Zephyr의 `ram_report` 타깃으로 확인하면:

```sh
west build -d /var/tmp/rtos/A3/zv/default -t ram_report    # (rtos-zbuild와 같은 venv·환경 변수에서)
```

```text
├── kernel                                                     5850  95.73%  - 
│   ├── init.c                                                 5665  92.70%  - 
│   │   ├── _kernel                                              32   0.52%  0x200002b0 bss
│   │   ├── z_idle_stacks                                       256   4.19%  0x200012e0 noinit
│   │   ├── z_idle_threads                                      128   2.09%  0x200000a8 bss
│   │   ├── z_interrupt_stacks                                 4096  67.03%  0x200002e0 noinit
│   │   ├── z_main_stack                                       1024  16.76%  0x200013e0 noinit
│   │   ├── z_main_thread                                       128   2.09%  0x20000128 bss
│   │   └── z_sys_post_kernel                                     1   0.02%  0x200002dd bss
│   ├── sched.c                                                 128   2.09%  - 
│   │   └── _thread_dummy                                       128   2.09%  0x200001a8 bss
...
```

(트리의 앞뒤를 줄였고, 열 사이 공백도 일부 잘랐다.)

인터럽트 스택 4 KB + main 스레드 스택 1 KB + idle 스택 256 B = **5,376 B, RAM의 88%**. 커널 객체 자체(`_kernel` 32 B, 스레드 구조체 128 B씩)는 작다. **RTOS RAM의 주인은 커널이 아니라 스택이다** — F3(스택 크기 산정)이 현장 버그 1순위인 이유와 같은 이야기다.

이제 예제 5와 같은 구조의 앱:

```c
/* 예제 6: FreeRTOS 예제 5와 같은 구조를 Zephyr로 — k_timer → k_msgq → control → k_event → telemetry.
 * 커널 객체는 모두 매크로로 "정적 정의" (빌드 시점에 RAM 자리가 정해진다). */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#ifdef CONFIG_LOG
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(a3, LOG_LEVEL_INF);
#define OUT(...) LOG_INF(__VA_ARGS__)
#else
#define OUT(fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
#endif

K_MSGQ_DEFINE(q, sizeof(uint32_t), 8, 4);
K_EVENT_DEFINE(ev);
K_MUTEX_DEFINE(log_mtx);
#define EV_BATCH BIT(0)

static void sensor_cb(struct k_timer *t)              /* ISR 문맥(시스템 타이머 인터럽트)에서 돈다 */
{
	static uint32_t n;
	ARG_UNUSED(t);
	uint32_t s = n++;
	k_msgq_put(&q, &s, K_NO_WAIT);
}
K_TIMER_DEFINE(sensor, sensor_cb, NULL);

static void control(void *a, void *b, void *c)
{
	uint32_t s;
	for (;;) {
		k_msgq_get(&q, &s, K_FOREVER);
		if ((s % 5) == 4) k_event_post(&ev, EV_BATCH);
	}
}
K_THREAD_DEFINE(control_tid, 512, control, NULL, NULL, NULL, 3, 0, 0);

static void telemetry(void *a, void *b, void *c)
{
	for (int k = 0; k < 4; k++) {
		k_event_wait(&ev, EV_BATCH, true, K_FOREVER);   /* true = 기다리기 전에 비트 초기화 */
		k_mutex_lock(&log_mtx, K_FOREVER);
		OUT("[%u ms] batch %d", (unsigned)k_uptime_get_32(), k);
		k_mutex_unlock(&log_mtx);
	}
	OUT("done: sizeof(struct k_thread)=%u", (unsigned)sizeof(struct k_thread));
}
K_THREAD_DEFINE(telem_tid, 1024, telemetry, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
	k_timer_start(&sensor, K_MSEC(10), K_MSEC(10));
	return 0;                                   /* main 스레드는 끝나도 다른 스레드는 계속 돈다 */
}
```

`prj.conf`는 `CONFIG_EVENTS=y` 한 줄, `log.conf`는 `CONFIG_LOG=y`, `shell.conf`는 `CONFIG_LOG=y`·`CONFIG_SHELL=y`·`CONFIG_KERNEL_SHELL=y`다. 설정 조각만 바꿔 세 번 빌드한다.

```sh
#!/bin/bash
# 예제 6 빌드·실행: z_app을 기본 / +LOG / +LOG+SHELL 로 빌드
A=/Users/donh/.rtos_scratch/A3/z_app; O=/var/tmp/rtos/A3/za
for v in ${VARS:-base log shell}; do
  extra=(); [ $v != base ] && extra=(-- -DEXTRA_CONF_FILE=$v.conf)
  /var/rtoslab/bin/rtos-zbuild mps2/an385 $A $O/$v "${extra[@]}" | grep -E "warning|error"
  E=$O/$v/zephyr/zephyr.elf; [ -f $E ] || { echo "$v FAILED"; continue; }
  echo "== $v: $(arm-none-eabi-size $E | tail -1 | awk '{printf "text=%d data=%d bss=%d FLASH=%d RAM=%d",$1,$2,$3,$1+$2,$2+$3}')"
  timeout 2 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none \
      -serial stdio -kernel $E 2>/dev/null | tr -d '\r' | head -8
done
```

```text
== base: text=14968 data=296 bss=7811 FLASH=15264 RAM=8107
*** Booting Zephyr OS build v4.4.2 ***
[50 ms] batch 0
[100 ms] batch 1
[150 ms] batch 2
[200 ms] batch 3
done: sizeof(struct k_thread)=136
== log: text=23984 data=384 bss=9870 FLASH=24368 RAM=10254
*** Booting Zephyr OS build v4.4.2 ***
[00:00:00.050,000] <inf> a3: [50 ms] batch 0
[00:00:00.100,000] <inf> a3: [100 ms] batch 1
[00:00:00.150,000] <inf> a3: [150 ms] batch 2
[00:00:00.200,000] <inf> a3: [200 ms] batch 3
[00:00:00.200,000] <inf> a3: done: sizeof(struct k_thread)=136
== shell: text=54172 data=500 bss=14436 FLASH=54672 RAM=14936
uart:~$ *** Booting Zephyr OS build v4.4.2 ***
uart:~$ uart:~$ [00:00:00.050,000] <inf> a3: [50 ms] batch 0
uart:~$ [00:00:00.100,000] <inf> a3: [100 ms] batch 1
uart:~$ [00:00:00.150,000] <inf> a3: [150 ms] batch 2
uart:~$ [00:00:00.200,000] <inf> a3: [200 ms] batch 3
uart:~$ [00:00:00.200,000] <inf> a3: done: sizeof(struct k_thread)=184
```

(로그·셸 출력의 ANSI 색 코드는 `sed`로 지웠다. 경고 줄은 세 빌드 모두 0개.)

출력에서 볼 것: 앱 동작은 세 빌드가 같은데 크기는 **15.3 KB → 24.4 KB → 54.7 KB**다. `fp.py`로 나누면:

```text
== z_app base
kernel                          5082    5874
drivers                         2946     216
arch(port)                      2522       5
app                              448    1972
libc                            1982      32
...
TOTAL                          15261    8103
== z_app log
kernel                          6116    5874
lib:log                         3670    2147
lib:cbprintf                    2004       0
...
TOTAL                          24424   10310
== z_app shell
lib:shell                      12824    4306
kernel                          7550    6026
strings(merged)                 7265       0
lib:other                       6236      85
lib:log                         4124    2158
...
TOTAL                          54765   15032
```

(각 표에서 큰 항목만 남겼다.)

- 같은 기능의 FreeRTOS 앱(예제 5, 10.7 KB 커널)과 Zephyr 앱(커널 5.1 KB + arch 2.5 KB)은 커널 크기가 같은 급이다. FreeRTOS 쪽이 큰 것은 trace·통계 포매팅을 켰기 때문이다.
- 로깅을 켜면 로그 코어(3.7 KB) + 포맷 엔진 cbprintf(2.0 KB) + 로그 처리 스레드와 버퍼(RAM 2.1 KB)가 붙는다. 로그 스레드가 하나 늘었으므로 커널도 +1 KB.
- 셸은 **셸 코어만 12.8 KB**, 명령 도움말 문자열이 7.3 KB, 그리고 셸이 쓰는 각종 라이브러리(`lib:other` 6.2 KB)다. 그리고 `CONFIG_KERNEL_SHELL`이 `THREAD_MONITOR`·`THREAD_NAME`을 켜서 `struct k_thread`가 136 → **184 B**로 커졌다(마지막 줄) — 미들웨어 하나가 커널 구조체 모양까지 바꾼다.
- Zephyr 앱의 `app` RAM 1,972 B는 `K_THREAD_DEFINE`이 만든 두 스레드의 스택(512 + 1,024 B)과 스레드 구조체, 메시지 큐 버퍼다. 정적 정의라서 앱 오브젝트의 bss에 들어간다.

### 4.7 종합 — 측정 차트 두 장

```svg
<svg viewBox="0 0 680 416" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="150.0" y="10.0" width="12.0" height="12.0" rx="4" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="168" y="21" font-size="12" text-anchor="start">FLASH (text+data)</text>
<rect x="310.0" y="10.0" width="12.0" height="12.0" rx="4" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c" stroke-width="1.2"/>
<text x="328" y="21" font-size="12" text-anchor="start">RAM (data+bss)</text>
<line x1="150" y1="42" x2="150" y2="366" stroke="#888" stroke-width="0.6" stroke-dasharray="2,3"/>
<text x="150" y="382" font-size="12" text-anchor="middle">0</text>
<line x1="229" y1="42" x2="229" y2="366" stroke="#888" stroke-width="0.6" stroke-dasharray="2,3"/>
<text x="229" y="382" font-size="12" text-anchor="middle">10K</text>
<line x1="307" y1="42" x2="307" y2="366" stroke="#888" stroke-width="0.6" stroke-dasharray="2,3"/>
<text x="307" y="382" font-size="12" text-anchor="middle">20K</text>
<line x1="386" y1="42" x2="386" y2="366" stroke="#888" stroke-width="0.6" stroke-dasharray="2,3"/>
<text x="386" y="382" font-size="12" text-anchor="middle">30K</text>
<line x1="464" y1="42" x2="464" y2="366" stroke="#888" stroke-width="0.6" stroke-dasharray="2,3"/>
<text x="464" y="382" font-size="12" text-anchor="middle">40K</text>
<line x1="543" y1="42" x2="543" y2="366" stroke="#888" stroke-width="0.6" stroke-dasharray="2,3"/>
<text x="543" y="382" font-size="12" text-anchor="middle">50K</text>
<text x="142" y="64" font-size="12" text-anchor="end">bare-metal raw</text>
<rect x="150.0" y="50.0" width="33.4" height="11.0" rx="1" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="188" y="60" font-size="12" text-anchor="start">4,248</text>
<rect x="150.0" y="63.0" width="3.3" height="11.0" rx="1" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c" stroke-width="1.2"/>
<text x="158" y="73" font-size="12" text-anchor="start">424</text>
<text x="142" y="96" font-size="12" text-anchor="end">bare-metal printf</text>
<rect x="150.0" y="82.0" width="60.7" height="11.0" rx="1" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="216" y="92" font-size="12" text-anchor="start">7,724</text>
<rect x="150.0" y="95.0" width="3.3" height="11.0" rx="1" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c" stroke-width="1.2"/>
<text x="158" y="105" font-size="12" text-anchor="start">424</text>
<text x="142" y="128" font-size="12" text-anchor="end">bare-metal %f</text>
<rect x="150.0" y="114.0" width="168.6" height="11.0" rx="1" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="324" y="124" font-size="12" text-anchor="start">21,460</text>
<rect x="150.0" y="127.0" width="6.2" height="11.0" rx="1" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c" stroke-width="1.2"/>
<text x="161" y="137" font-size="12" text-anchor="start">792</text>
<text x="142" y="160" font-size="12" text-anchor="end">FreeRTOS 최소</text>
<rect x="150.0" y="146.0" width="53.1" height="11.0" rx="1" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="208" y="156" font-size="12" text-anchor="start">6,756</text>
<rect x="150.0" y="159.0" width="37.1" height="11.0" rx="1" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c" stroke-width="1.2"/>
<text x="192" y="169" font-size="12" text-anchor="start">4,728</text>
<text x="142" y="192" font-size="12" text-anchor="end">FreeRTOS 풀</text>
<rect x="150.0" y="178.0" width="148.7" height="11.0" rx="1" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="304" y="188" font-size="12" text-anchor="start">18,924</text>
<rect x="150.0" y="191.0" width="103.3" height="11.0" rx="1" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c" stroke-width="1.2"/>
<text x="258" y="201" font-size="12" text-anchor="start">13,148</text>
<text x="142" y="224" font-size="12" text-anchor="end">Zephyr 스레드 없음</text>
<rect x="150.0" y="210.0" width="73.5" height="11.0" rx="1" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="228" y="220" font-size="12" text-anchor="start">9,352</text>
<rect x="150.0" y="223.0" width="43.3" height="11.0" rx="1" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c" stroke-width="1.2"/>
<text x="198" y="233" font-size="12" text-anchor="start">5,513</text>
<text x="142" y="256" font-size="12" text-anchor="end">Zephyr hello</text>
<rect x="150.0" y="242.0" width="98.7" height="11.0" rx="1" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="254" y="252" font-size="12" text-anchor="start">12,556</text>
<rect x="150.0" y="255.0" width="48.0" height="11.0" rx="1" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c" stroke-width="1.2"/>
<text x="203" y="265" font-size="12" text-anchor="start">6,111</text>
<text x="142" y="288" font-size="12" text-anchor="end">Zephyr 앱</text>
<rect x="150.0" y="274.0" width="119.9" height="11.0" rx="1" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="275" y="284" font-size="12" text-anchor="start">15,264</text>
<rect x="150.0" y="287.0" width="63.7" height="11.0" rx="1" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c" stroke-width="1.2"/>
<text x="219" y="297" font-size="12" text-anchor="start">8,107</text>
<text x="142" y="320" font-size="12" text-anchor="end">Zephyr 앱+log</text>
<rect x="150.0" y="306.0" width="191.5" height="11.0" rx="1" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="346" y="316" font-size="12" text-anchor="start">24,368</text>
<rect x="150.0" y="319.0" width="80.6" height="11.0" rx="1" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c" stroke-width="1.2"/>
<text x="236" y="329" font-size="12" text-anchor="start">10,254</text>
<text x="142" y="352" font-size="12" text-anchor="end">Zephyr 앱+shell</text>
<rect x="150.0" y="338.0" width="429.6" height="11.0" rx="1" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="585" y="348" font-size="12" text-anchor="start">54,672</text>
<rect x="150.0" y="351.0" width="117.4" height="11.0" rx="1" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c" stroke-width="1.2"/>
<text x="272" y="361" font-size="12" text-anchor="start">14,936</text>
<text x="150" y="408" font-size="12" text-anchor="start">바이트 · QEMU mps2-an385(Cortex-M3) · -O2 · newlib-nano / picolibc</text>
</svg>
```

그림 5 — 예제 3~6과 §5의 예제 8에서 잰 FLASH·RAM. FreeRTOS 풀은 힙 8 KB 빌드(템플릿의 128 KB 힙이면 RAM 136,028 B). FreeRTOS 쪽 RAM에는 링커가 따로 예약한 MSP 스택 4 KB가 빠져 있다(§4.1).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10.0" y="10.0" width="12.0" height="12.0" rx="4" fill="#3f9a6b" fill-opacity="0.8" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="27" y="21" font-size="12" text-anchor="start">커널</text>
<rect x="69.0" y="10.0" width="12.0" height="12.0" rx="4" fill="#e08a3c" fill-opacity="0.8" stroke="#e08a3c" stroke-width="1.2"/>
<text x="86" y="21" font-size="12" text-anchor="start">포트/arch</text>
<rect x="164.0" y="10.0" width="12.0" height="12.0" rx="4" fill="#888" fill-opacity="0.8" stroke="#888" stroke-width="1.2"/>
<text x="181" y="21" font-size="12" text-anchor="start">드라이버·보드</text>
<rect x="281.8" y="10.0" width="12.0" height="12.0" rx="4" fill="#d0564a" fill-opacity="0.8" stroke="#d0564a" stroke-width="1.2"/>
<text x="299" y="21" font-size="12" text-anchor="start">libc</text>
<rect x="345.6" y="10.0" width="12.0" height="12.0" rx="4" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="363" y="21" font-size="12" text-anchor="start">OS 라이브러리</text>
<rect x="459.0" y="10.0" width="12.0" height="12.0" rx="4" fill="none" stroke="#888" stroke-width="1.2" stroke-dasharray="2,2"/>
<text x="476" y="21" font-size="12" text-anchor="start">앱·문자열·기타</text>
<text x="112" y="59" font-size="12" text-anchor="end">bare-metal raw</text>
<rect x="120.0" y="44.0" width="7.7" height="22.0" rx="0" fill="#888" fill-opacity="0.75" stroke="#888" stroke-width="1.2"/>
<rect x="127.7" y="44.0" width="26.9" height="22.0" rx="0" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a" stroke-width="1.2"/>
<rect x="154.7" y="44.0" width="1.7" height="22.0" rx="0" fill="none" stroke="#888" stroke-width="1.2" stroke-dasharray="2,2"/>
<text x="161" y="60" font-size="12" text-anchor="start">4,248</text>
<text x="112" y="95" font-size="12" text-anchor="end">FreeRTOS 최소</text>
<rect x="120.0" y="80.0" width="16.9" height="22.0" rx="0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<rect x="136.9" y="80.0" width="3.6" height="22.0" rx="0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c" stroke-width="1.2"/>
<rect x="140.5" y="80.0" width="7.5" height="22.0" rx="0" fill="#888" fill-opacity="0.75" stroke="#888" stroke-width="1.2"/>
<rect x="148.0" y="80.0" width="26.9" height="22.0" rx="0" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a" stroke-width="1.2"/>
<rect x="175.0" y="80.0" width="2.9" height="22.0" rx="0" fill="none" stroke="#888" stroke-width="1.2" stroke-dasharray="2,2"/>
<text x="183" y="96" font-size="12" text-anchor="start">6,756</text>
<text x="112" y="131" font-size="12" text-anchor="end">FreeRTOS 풀</text>
<rect x="120.0" y="116.0" width="86.0" height="22.0" rx="0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="163" y="132" font-size="12" text-anchor="middle">10.0K</text>
<rect x="206.0" y="116.0" width="6.1" height="22.0" rx="0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c" stroke-width="1.2"/>
<rect x="212.1" y="116.0" width="8.3" height="22.0" rx="0" fill="#888" fill-opacity="0.75" stroke="#888" stroke-width="1.2"/>
<rect x="220.4" y="116.0" width="51.4" height="22.0" rx="0" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a" stroke-width="1.2"/>
<text x="246" y="132" font-size="12" text-anchor="middle">6.0K</text>
<rect x="271.8" y="116.0" width="10.4" height="22.0" rx="0" fill="none" stroke="#888" stroke-width="1.2" stroke-dasharray="2,2"/>
<text x="287" y="132" font-size="12" text-anchor="start">18,918</text>
<text x="112" y="167" font-size="12" text-anchor="end">Zephyr hello</text>
<rect x="120.0" y="152.0" width="24.3" height="22.0" rx="0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<rect x="144.3" y="152.0" width="21.6" height="22.0" rx="0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c" stroke-width="1.2"/>
<rect x="165.9" y="152.0" width="25.3" height="22.0" rx="0" fill="#888" fill-opacity="0.75" stroke="#888" stroke-width="1.2"/>
<rect x="191.2" y="152.0" width="17.0" height="22.0" rx="0" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a" stroke-width="1.2"/>
<rect x="208.2" y="152.0" width="5.1" height="22.0" rx="0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0" stroke-width="1.2"/>
<rect x="213.3" y="152.0" width="14.3" height="22.0" rx="0" fill="none" stroke="#888" stroke-width="1.2" stroke-dasharray="2,2"/>
<text x="233" y="168" font-size="12" text-anchor="start">12,556</text>
<text x="112" y="203" font-size="12" text-anchor="end">Zephyr 앱</text>
<rect x="120.0" y="188.0" width="43.6" height="22.0" rx="0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<rect x="163.6" y="188.0" width="21.6" height="22.0" rx="0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c" stroke-width="1.2"/>
<rect x="185.2" y="188.0" width="25.3" height="22.0" rx="0" fill="#888" fill-opacity="0.75" stroke="#888" stroke-width="1.2"/>
<rect x="210.4" y="188.0" width="17.0" height="22.0" rx="0" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a" stroke-width="1.2"/>
<rect x="227.4" y="188.0" width="5.1" height="22.0" rx="0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0" stroke-width="1.2"/>
<rect x="232.5" y="188.0" width="18.3" height="22.0" rx="0" fill="none" stroke="#888" stroke-width="1.2" stroke-dasharray="2,2"/>
<text x="256" y="204" font-size="12" text-anchor="start">15,261</text>
<text x="112" y="239" font-size="12" text-anchor="end">Zephyr 앱+log</text>
<rect x="120.0" y="224.0" width="52.4" height="22.0" rx="0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="146" y="240" font-size="12" text-anchor="middle">6.1K</text>
<rect x="172.4" y="224.0" width="23.8" height="22.0" rx="0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c" stroke-width="1.2"/>
<rect x="196.2" y="224.0" width="25.3" height="22.0" rx="0" fill="#888" fill-opacity="0.75" stroke="#888" stroke-width="1.2"/>
<rect x="221.5" y="224.0" width="18.6" height="22.0" rx="0" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a" stroke-width="1.2"/>
<rect x="240.1" y="224.0" width="64.0" height="22.0" rx="0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="272" y="240" font-size="12" text-anchor="middle">7.5K</text>
<rect x="304.1" y="224.0" width="25.3" height="22.0" rx="0" fill="none" stroke="#888" stroke-width="1.2" stroke-dasharray="2,2"/>
<text x="334" y="240" font-size="12" text-anchor="start">24,424</text>
<text x="112" y="275" font-size="12" text-anchor="end">Zephyr 앱+shell</text>
<rect x="120.0" y="260.0" width="64.7" height="22.0" rx="0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="152" y="276" font-size="12" text-anchor="middle">7.5K</text>
<rect x="184.7" y="260.0" width="26.6" height="22.0" rx="0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c" stroke-width="1.2"/>
<rect x="211.3" y="260.0" width="27.1" height="22.0" rx="0" fill="#888" fill-opacity="0.75" stroke="#888" stroke-width="1.2"/>
<rect x="238.4" y="260.0" width="38.0" height="22.0" rx="0" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a" stroke-width="1.2"/>
<rect x="276.4" y="260.0" width="230.9" height="22.0" rx="0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="392" y="276" font-size="12" text-anchor="middle">26.9K</text>
<rect x="507.3" y="260.0" width="82.1" height="22.0" rx="0" fill="none" stroke="#888" stroke-width="1.2" stroke-dasharray="2,2"/>
<text x="548" y="276" font-size="12" text-anchor="middle">9.6K</text>
<text x="594" y="276" font-size="12" text-anchor="start">54,765</text>
<text x="120" y="302" font-size="12" text-anchor="start">map 파일의 입력 섹션을 오브젝트별로 합산 (예제 4의 fp.py). FreeRTOS 풀은 힙 8 KB 빌드</text>
<text x="120" y="320" font-size="12" text-anchor="start">커널 = tasks/queue/list/timers/event_groups/heap 또는 Zephyr kernel/</text>
</svg>
```

그림 6 — 같은 빌드의 FLASH를 컴포넌트별로 쌓았다. 초록(커널)은 어느 구성에서도 2~10 KB에 머문다. 막대를 길게 만드는 것은 libc(빨강)와 OS 라이브러리·미들웨어(파랑), 그리고 셸의 도움말 문자열(점선)이다.

숫자를 한 표로:

| 구성 | FLASH | RAM | 커널+포트 FLASH | 가장 큰 덩어리 |
|---|---|---|---|---|
| bare-metal raw (예제 3a) | 4,248 | 424 | 0 | libc 3,140 (`setvbuf` 하나 때문) |
| bare-metal printf (3b) | 7,724 | 424 | 0 | libc |
| bare-metal `%f` (3c) | 21,460 | 792 | 0 | libc 17,657 + 소프트 float 2,344 |
| FreeRTOS 최소 (예제 4) | 6,756 | 4,728 | 2,392 | libc 3,140, 힙 배열 4 KB |
| FreeRTOS 풀, 힙 8 KB (예제 5) | 18,924 | 13,148 | 10,741 | `tasks.c` 5,401, libc 5,992 |
| Zephyr 스레드 없음 (예제 8) | 9,352 | 5,513 | — | 드라이버, 스택 |
| Zephyr hello (예제 6) | 12,556 | 6,111 | 5,358 | 드라이버 2,946, 스택 5,376 |
| Zephyr 앱 (예제 6) | 15,264 | 8,107 | 7,604 | 커널 5,082 |
| Zephyr 앱 + log | 24,368 | 10,254 | 8,894 | log + cbprintf 5,674 |
| Zephyr 앱 + log + shell | 54,672 | 14,936 | 10,648 | 셸 12,824 + 문자열 7,265 |

(바이트. 커널+포트 = FreeRTOS는 커널 파일·힙·`port.c`, Zephyr는 `kernel` + `arch(port)`.)

### 4.8 정리 — footprint에서 기억할 다섯 문장

1. **커널은 작다.** 스케줄러 + 태스크 + 지연만이면 FreeRTOS 2.4 KB, 웬만한 서비스를 다 켜도 10 KB 근처다.
2. **RAM은 스택과 힙이 먹는다.** 커널 변수는 수백 바이트, Zephyr hello RAM의 88%가 스택이었다.
3. **printf가 커널보다 크다.** `%f` 하나가 FreeRTOS 커널 전체보다 큰 13.7 KB를 더했다. 크기가 빠듯한 MCU에서는 정수 전용 포맷터를 쓰거나 Zephyr의 딕셔너리 로깅·지연 로깅(K6)처럼 "포맷은 호스트에서" 하는 방식을 쓴다.
4. **libc 함수 하나는 그래프 전체만큼 든다.** `setvbuf` 한 줄 = 3.1 KB.
5. **미들웨어가 막대를 길게 만든다.** 셸 하나가 FLASH 30 KB. 개발 빌드에서만 켜고 양산 빌드에서는 끈다(§9).

---

## 5. 컴파일 시점 구성 — RTOS의 표지

### 5.1 직관 — "런타임 모듈"이 없는 세계

Linux는 커널을 빌드한 뒤에도 `insmod`로 기능을 더한다(EL E3). RTOS에는 그런 길이 거의 없다. **무엇을 넣을지는 전부 빌드 입력**이고, 결과는 하나의 고정된 이미지다. 그래서 RTOS의 설정 파일은 Linux의 `.config`보다 훨씬 많은 것을 정한다: 기능의 유무뿐 아니라 **구조체의 모양(TCB 크기), 우선순위 개수, tick 주기, 힙 크기, 어떤 C 파일을 컴파일할지**까지.

```svg
<svg viewBox="0 0 680 352" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="170" y="20" font-size="13" text-anchor="middle" font-weight="bold">FreeRTOS</text>
<text x="510" y="20" font-size="13" text-anchor="middle" font-weight="bold">Zephyr</text>
<rect x="20.0" y="32.0" width="300.0" height="46.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="170" y="51" font-size="12" text-anchor="middle" font-weight="bold">FreeRTOSConfig.h</text>
<text x="170" y="67" font-size="11" text-anchor="middle">#define configUSE_TIMERS 0</text>
<line x1="170" y1="78" x2="170" y2="92" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="20.0" y="94.0" width="300.0" height="46.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="170" y="113" font-size="12" text-anchor="middle" font-weight="bold">timers.c 본문 전체가 #if 안</text>
<text x="170" y="129" font-size="11" text-anchor="middle">#if ( configUSE_TIMERS == 1 )</text>
<line x1="170" y1="140" x2="170" y2="154" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="20.0" y="156.0" width="300.0" height="34.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="170" y="177" font-size="12" text-anchor="middle">timers.o = 사실상 빈 오브젝트</text>
<line x1="170" y1="190" x2="170" y2="204" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="20.0" y="206.0" width="300.0" height="46.0" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="170" y="225" font-size="12" text-anchor="middle" font-weight="bold">ld --gc-sections</text>
<text x="170" y="241" font-size="11" text-anchor="middle">안 부른 함수의 섹션도 버림</text>
<line x1="170" y1="252" x2="170" y2="266" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="20.0" y="268.0" width="300.0" height="34.0" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="170" y="289" font-size="12" text-anchor="middle">app.elf</text>
<rect x="340.0" y="32.0" width="140.0" height="46.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="410" y="51" font-size="12" text-anchor="middle" font-weight="bold">prj.conf</text>
<text x="410" y="67" font-size="11" text-anchor="middle">+ 보드 defconfig</text>
<rect x="490.0" y="32.0" width="180.0" height="46.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="580" y="51" font-size="12" text-anchor="middle" font-weight="bold">보드 .dts</text>
<text x="580" y="67" font-size="11" text-anchor="middle">+ overlay</text>
<line x1="410" y1="78" x2="410" y2="92" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<line x1="580" y1="78" x2="580" y2="92" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="340.0" y="94.0" width="140.0" height="46.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="410" y="113" font-size="12" text-anchor="middle" font-weight="bold">Kconfig 해석</text>
<text x="410" y="129" font-size="11" text-anchor="middle">→ .config</text>
<rect x="490.0" y="94.0" width="180.0" height="46.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="580" y="113" font-size="12" text-anchor="middle" font-weight="bold">gen_defines.py</text>
<text x="580" y="129" font-size="11" text-anchor="middle">DT 해석</text>
<line x1="410" y1="140" x2="410" y2="154" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<line x1="580" y1="140" x2="580" y2="154" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="340.0" y="156.0" width="140.0" height="46.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="410" y="175" font-size="12" text-anchor="middle" font-weight="bold">autoconf.h</text>
<text x="410" y="191" font-size="11" text-anchor="middle">CONFIG_* 매크로</text>
<rect x="490.0" y="156.0" width="180.0" height="46.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="580" y="175" font-size="12" text-anchor="middle" font-weight="bold">devicetree_generated.h</text>
<text x="580" y="191" font-size="11" text-anchor="middle">DT_* 매크로</text>
<line x1="410" y1="202" x2="470" y2="228" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<line x1="580" y1="202" x2="550" y2="228" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="340.0" y="230.0" width="330.0" height="46.0" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="505" y="249" font-size="12" text-anchor="middle" font-weight="bold">CMake: 어떤 .c를 넣을지 + 컴파일</text>
<text x="505" y="265" font-size="11" text-anchor="middle">kernel_sources_ifdef(CONFIG_TIMESLICING …)</text>
<line x1="505" y1="276" x2="505" y2="290" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<rect x="340.0" y="292.0" width="330.0" height="34.0" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.2"/>
<text x="505" y="313" font-size="12" text-anchor="middle">zephyr.elf</text>
<text x="340" y="346" font-size="12" text-anchor="middle">두 RTOS 모두 &quot;설정 = 빌드 입력&quot;. 런타임에 켜고 끄는 기능은 거의 없다</text>
</svg>
```

그림 7 — 설정이 바이너리가 되는 길. FreeRTOS는 헤더 하나(`FreeRTOSConfig.h`)와 전처리기 `#if`가 전부다. Zephyr는 Kconfig(기능)와 devicetree(하드웨어)라는 두 입력을 C 매크로 헤더로 바꾸고, CMake가 그 값으로 컴파일할 파일 목록까지 정한다.

### 5.2 FreeRTOSConfig.h — 헤더 하나가 설정 시스템 전부

FreeRTOS에는 설정 도구가 없다. 앱이 `FreeRTOSConfig.h`에 `#define`을 쓰면, `FreeRTOS.h`가 빠진 항목에 기본값을 채우고(예제 2에서 본 2,319줄의 대부분), 커널 소스가 `#if`로 코드와 구조체 필드를 넣고 뺀다. 자주 만지는 항목:

| 항목 | 무엇을 바꾸나 | 크기·동작 영향 |
|---|---|---|
| `configUSE_PREEMPTION` | 선점형(1) / 협력형(0) | 스케줄링 동작 (C4) |
| `configMAX_PRIORITIES` | 우선순위 개수 | ready 리스트 배열 = 개수 × 20 B |
| `configTICK_RATE_HZ` | tick 주기 | 시간 해상도와 tick 인터럽트 부하 (C5) |
| `configMINIMAL_STACK_SIZE` | idle 태스크 스택 (word 단위!) | RAM |
| `configTOTAL_HEAP_SIZE` | heap_1/2/4의 정적 배열 크기 | RAM (bss) |
| `configUSE_TIMERS` | 소프트웨어 타이머 + 서비스 태스크 | FLASH +2 KB, 태스크 하나 더 |
| `configUSE_MUTEXES` | 뮤텍스·우선순위 상속 | TCB +8 B |
| `configUSE_TASK_NOTIFICATIONS` | 태스크 알림 (가장 가벼운 IPC) | TCB +8 B (배열 1칸 기준) |
| `configUSE_TRACE_FACILITY` | 태스크·큐 번호, `vTaskListTasks` 지원 | TCB +8 B, 큐 +8 B |
| `configGENERATE_RUN_TIME_STATS` | 태스크별 실행 시간 누적 | TCB +4 B, 전환마다 타이머 읽기 |
| `configCHECK_FOR_STACK_OVERFLOW` | 전환 때 스택 끝 검사 (1 또는 2) | 전환마다 비교 비용 (F3) |
| `configSUPPORT_STATIC_ALLOCATION` / `_DYNAMIC_` | `xTaskCreateStatic` 가능 여부 / 힙 사용 | 힙 없는 설계 가능 (F1) |
| `configMAX_SYSCALL_INTERRUPT_PRIORITY` | FreeRTOS API를 부를 수 있는 최고 인터럽트 우선순위 | BASEPRI 마스크 값 (B3, E1) |
| `INCLUDE_vTaskDelete` 등 | 개별 API 포함 여부 | 해당 함수 |

힙 구현은 설정 매크로가 아니라 **어느 `heap_N.c`를 링크하느냐**로 고른다(랩 Makefile의 `HEAP=`). heap_1은 할당만 되고 해제가 없다, heap_4는 해제와 인접 블록 병합이 있다 — 자세한 비교는 F1.

### 5.3 예제 7 — 설정을 하나씩 켜서 바이트 재기

무엇을 확인하나: 예제 4의 최소 구성에서 설정 하나만 켜고 빌드·실행해서, text/data/bss와 **`sizeof(StaticTask_t)`(= TCB 크기)**, 그리고 실행 끝의 남은 힙이 어떻게 변하는지. TCB 크기는 실행하지 않고 재는 요령을 썼다 — 크기가 `sizeof(T)`인 배열을 정의한 파일을 컴파일만 하고 `nm -S`로 그 배열 크기를 읽는다.

```c
/* 컴파일만 한다: 배열 크기 = sizeof(...). nm -S로 읽는다 */
#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>
__attribute__((used)) uint8_t sz_StaticTask_t[sizeof(StaticTask_t)];
__attribute__((used)) uint8_t sz_StaticQueue_t[sizeof(StaticQueue_t)];
```

```sh
#!/bin/bash
# 예제 7: fr_min에서 설정을 하나씩 켜고 text/data/bss와 sizeof(StaticTask_t) 변화를 잰다
S=/Users/donh/.rtos_scratch/A3; O=/var/tmp/rtos/A3/tog; K=/var/rtoslab/src/FreeRTOS-Kernel
INC="-I$S/fr_min -I/var/rtoslab/templates/common -I$K/include -I$K/portable/GCC/ARM_CM3"
run() {  # $1=label $2=HEAP $3..=-D flags
  local lab=$1 heap=$2; shift 2
  local r=$(HEAP=$heap $S/fbuild.sh $O/$lab rtos $S/fr_min/main.c "$@" 2>&1 | tail -1)
  arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 -O2 $INC "$@" -c $S/probe.c -o $O/$lab/probe.o
  local tcb=$(arm-none-eabi-nm -S $O/$lab/probe.o | awk '/sz_StaticTask_t/{print strtonum("0x"$2)}')
  local qs=$(arm-none-eabi-nm -S $O/$lab/probe.o | awk '/sz_StaticQueue_t/{print strtonum("0x"$2)}')
  local fh=$(ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/$lab/app.elf 2>&1 | awk '/free heap/{print $4}')
  echo "$r" | awk -v l=$lab -v t=$tcb -v q=$qs -v fh=$fh '{printf "%-16s text=%5d data=%3d bss=%5d  TCB=%2d Q=%2d free_heap=%s\n", l,$1,$2,$3,t,q,fh}'
}
run base            heap_1
run +MUTEXES        heap_1 -DconfigUSE_MUTEXES=1
run +TASK_NOTIFY    heap_1 -DconfigUSE_TASK_NOTIFICATIONS=1
run +TRACE_FACILITY heap_1 -DconfigUSE_TRACE_FACILITY=1
run +STACK_CHECK=2  heap_1 -DconfigCHECK_FOR_STACK_OVERFLOW=2
run +RUNTIME_STATS  heap_1 -DconfigGENERATE_RUN_TIME_STATS=1
run +TIMERS         heap_1 -DconfigUSE_TIMERS=1
run heap_4          heap_4
run heap_4+16KB     heap_4 -DconfigTOTAL_HEAP_SIZE=16384
```

```text
base             text= 6656 data=100 bss= 4628  TCB=60 Q=72 free_heap=2360
+MUTEXES         text= 6656 data=100 bss= 4628  TCB=68 Q=72 free_heap=2336
+TASK_NOTIFY     text= 6656 data=100 bss= 4628  TCB=68 Q=72 free_heap=2336
+TRACE_FACILITY  text= 6668 data=100 bss= 4628  TCB=68 Q=80 free_heap=2336
+STACK_CHECK=2   text= 6776 data=100 bss= 4628  TCB=60 Q=72 free_heap=2360
+RUNTIME_STATS   text= 6724 data=100 bss= 4636  TCB=64 Q=72 free_heap=2360
+TIMERS          text= 8692 data=100 bss= 4692  TCB=60 Q=72 free_heap=1664
heap_4           text= 6980 data=100 bss= 4652  TCB=60 Q=72 free_heap=2312
heap_4+16KB      text= 6980 data=100 bss=16940  TCB=60 Q=72 free_heap=14600
```

```svg
<svg viewBox="0 0 680 268" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="18" font-size="13" text-anchor="start" font-weight="bold">FreeRTOS 최소 구성에서 하나씩 켰을 때 text 증가 (바이트)</text>
<text x="162" y="48" font-size="12" text-anchor="end">+MUTEXES</text>
<text x="176" y="49" font-size="12" text-anchor="start">+0</text>
<text x="220" y="49" font-size="12" text-anchor="start">TCB 60→68 B</text>
<text x="162" y="78" font-size="12" text-anchor="end">+TASK_NOTIFICATIONS</text>
<text x="176" y="79" font-size="12" text-anchor="start">+0</text>
<text x="220" y="79" font-size="12" text-anchor="start">TCB 60→68 B</text>
<text x="162" y="108" font-size="12" text-anchor="end">+TRACE_FACILITY</text>
<rect x="170.0" y="97.0" width="2.0" height="16.0" rx="1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="177" y="109" font-size="12" text-anchor="start">+12</text>
<text x="221" y="109" font-size="12" text-anchor="start">TCB 68 B, 큐 72→80 B</text>
<text x="162" y="138" font-size="12" text-anchor="end">+RUN_TIME_STATS</text>
<rect x="170.0" y="127.0" width="8.3" height="16.0" rx="1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="184" y="139" font-size="12" text-anchor="start">+68</text>
<text x="228" y="139" font-size="12" text-anchor="start">TCB 64 B</text>
<text x="162" y="168" font-size="12" text-anchor="end">+STACK_OVERFLOW=2</text>
<rect x="170.0" y="157.0" width="14.7" height="16.0" rx="1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="191" y="169" font-size="12" text-anchor="start">+120</text>
<text x="235" y="169" font-size="12" text-anchor="start">TCB 그대로</text>
<text x="162" y="198" font-size="12" text-anchor="end">heap_1 → heap_4</text>
<rect x="170.0" y="187.0" width="39.7" height="16.0" rx="1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="216" y="199" font-size="12" text-anchor="start">+324</text>
<text x="260" y="199" font-size="12" text-anchor="start">free()가 생김</text>
<text x="162" y="228" font-size="12" text-anchor="end">+TIMERS</text>
<rect x="170.0" y="217.0" width="249.5" height="16.0" rx="1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="426" y="229" font-size="12" text-anchor="start">+2036</text>
<text x="470" y="229" font-size="12" text-anchor="start">타이머 태스크+큐: 힙 −696 B</text>
<text x="10" y="256" font-size="12" text-anchor="start">앱이 그 기능을 부르지 않아도 TCB·큐 구조체는 커진다 (모든 태스크에 곱해짐)</text>
</svg>
```

그림 8 — 예제 7의 text 증가분. 대부분의 설정은 코드를 거의 늘리지 않지만(앱이 그 API를 부르지 않으면 `--gc-sections`가 버린다), **구조체는 무조건 커진다**. `configUSE_TIMERS`만 예외적으로 2 KB — 타이머 서비스 태스크와 명령 큐를 실제로 만들기 때문이다.

출력에서 볼 것:

- **`+MUTEXES`는 text가 0바이트 늘었는데 TCB는 60 → 68 B**다. 앱이 뮤텍스를 안 쓰니 우선순위 상속 코드는 링커가 버렸지만, TCB 구조체의 필드(`uxBasePriority`, `uxMutexesHeld`)는 모든 태스크에 생긴다. 태스크 3개(A, B, IDLE)이므로 남은 힙이 2,360 → 2,336 B, 정확히 3 × 8 = 24 B 줄었다.
- `+RUNTIME_STATS`는 TCB가 60 → 64 B로 커졌는데 남은 힙은 그대로다. heap_1이 할당 크기를 8바이트 정렬로 올리기 때문이다(60도 64로 할당된다). 구조체 크기와 실제 메모리 소비가 다를 수 있다는 예다.
- `+TIMERS`: text +2,036 B, bss +64 B, 남은 힙 −696 B(타이머 태스크의 TCB + 스택 512 B + 명령 큐). 앱이 타이머를 하나도 안 만들어도 이만큼 든다.
- heap_1 → heap_4: text +324 B(`vPortFree`와 블록 병합 코드), 블록 헤더 때문에 남은 힙 −48 B.
- `heap_4+16KB`: bss가 정확히 12,288 B(= 16,384 − 4,096) 늘었다. **힙 크기는 그대로 RAM 크기다.**

Don 경험으로 번역하면: SSD 펌웨어에서 명령 디스크립터에 디버그 필드 하나를 더하면 "디스크립터 수 × 필드 크기"만큼 SRAM이 사라졌던 것과 같다. **커널 설정 하나는 "모든 태스크·모든 큐에 곱해지는 필드"**다.

### 5.4 Zephyr — Kconfig + devicetree, 그리고 파일 선택

Kconfig 언어 자체(`depends on`, `select`, `.config` → `autoconf.h`)는 EL E2에서, devicetree 문법은 EL D 모듈에서 이미 봤다. Zephyr에서 달라지는 점만:

- **입력이 여러 겹이다**: 보드의 `<board>_defconfig` → 앱의 `prj.conf` → `EXTRA_CONF_FILE`로 넘기는 조각(.conf). 뒤의 것이 앞의 것을 덮는다. 이 랩의 `mps2_an385_defconfig`는 `CONFIG_GPIO=y`, `CONFIG_SERIAL=y`, `CONFIG_UART_CONSOLE=y`, `CONFIG_ARM_MPU=y` 등을 켠다.
- **DT도 빌드 시점이다**: Linux는 DTB를 부팅 때 커널에 넘기지만, Zephyr는 빌드할 때 `gen_defines.py`가 DT를 `devicetree_generated.h`의 `DT_*` 매크로로 바꾼다(H3). 이 빌드에서 그 헤더는 `#define DT_` 줄이 3,548개였다.
- **Kconfig가 커널 파일 선택까지 한다**: `kernel/CMakeLists.txt`의 일부.

```cmake
kernel_sources_ifdef(CONFIG_MULTITHREADING
  idle.c
  mailbox.c
  msg_q.c
  mutex.c
  queue.c
  sem.c
  stack.c
  system_work_q.c
  work.c
  condvar.c
  thread.c
  sched.c
  pipe.c
  )
...
else() # CONFIG_MULTITHREADING
kernel_sources(nothread.c)
endif() # CONFIG_MULTITHREADING

kernel_sources_ifdef(CONFIG_TIMESLICING timeslicing.c)
```

`CONFIG_MULTITHREADING=n`이면 스케줄러(`sched.c`)와 스레드(`thread.c`)가 빌드 목록에서 아예 빠지고 `nothread.c`로 바뀐다 — "RTOS인데 스레드가 없는" super-loop 모드다(A2의 실행 모델 사다리 맨 아래).

### 5.5 예제 8 — 같은 hello, Kconfig 조각만 바꿔 다섯 번

무엇을 확인하나: 앱 코드는 그대로 두고 설정 조각만 바꿨을 때 크기가 어떻게 변하는지. 조각 파일은 한두 줄짜리다:

```text
lean.conf      CONFIG_ARM_MPU=n
nothread.conf  CONFIG_ARM_MPU=n  CONFIG_MULTITHREADING=n
noslice.conf   CONFIG_TIMESLICING=n
noprintk.conf  CONFIG_PRINTK=n  CONFIG_BOOT_BANNER=n  CONFIG_CONSOLE=n  CONFIG_UART_CONSOLE=n
```

(실제 파일에서는 한 줄에 하나씩 썼다.)

```sh
#!/bin/bash
# 예제 8: 같은 hello 앱을 Kconfig 조각(EXTRA_CONF_FILE)만 바꿔 빌드하고 크기를 잰다
A=/Users/donh/.rtos_scratch/A3/z_hello; O=/var/tmp/rtos/A3/zv
for v in ${VARS:-default lean nothread noslice noprintk}; do
  extra=(); [ $v != default ] && extra=(-- -DEXTRA_CONF_FILE=$v.conf)
  /var/rtoslab/bin/rtos-zbuild mps2/an385 $A $O/$v "${extra[@]}" > /dev/null || { echo "$v FAILED"; continue; }
  E=$O/$v/zephyr/zephyr.elf
  out=$(timeout 3 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none \
        -serial stdio -kernel $E 2>/dev/null | tr -d '\r' | grep -c hello)
  arm-none-eabi-size $E | tail -1 | awk -v v=$v -v o=$out '{printf "%-9s text=%6d data=%4d bss=%5d  FLASH=%6d RAM=%5d  hello_lines=%s\n", v,$1,$2,$3,$1+$2,$2+$3,o}'
done
```

```text
default   text= 12388 data= 168 bss= 5943  FLASH= 12556 RAM= 6111  hello_lines=1
lean      text= 11640 data= 168 bss= 5894  FLASH= 11808 RAM= 6062  hello_lines=1
nothread  text=  9188 data= 164 bss= 5349  FLASH=  9352 RAM= 5513  hello_lines=1
noslice   text= 11980 data= 164 bss= 5910  FLASH= 12144 RAM= 6074  hello_lines=1
noprintk  text=  8776 data= 148 bss= 5939  FLASH=  8924 RAM= 6087  hello_lines=0
```

(`lean`·`nothread` 두 줄은 아래 함정을 고친 뒤 다시 돌린 결과다.)

생성된 헤더에서 설정이 어떻게 보이는지도 확인했다:

```sh
grep -E "CONFIG_TIMESLICING|CONFIG_MULTITHREADING " \
  /var/tmp/rtos/A3/zv/{default,noslice,nothread}/zephyr/include/generated/zephyr/autoconf.h
```

```text
/var/tmp/rtos/A3/zv/default/zephyr/include/generated/zephyr/autoconf.h:#define CONFIG_MULTITHREADING 1
/var/tmp/rtos/A3/zv/default/zephyr/include/generated/zephyr/autoconf.h:#define CONFIG_TIMESLICING 1
/var/tmp/rtos/A3/zv/noslice/zephyr/include/generated/zephyr/autoconf.h:#define CONFIG_MULTITHREADING 1
```

출력에서 볼 것:

- 꺼진 옵션은 `autoconf.h`에 **아예 없다**(`#define ... 0`이 아니다). 그래서 Zephyr 코드는 `#ifdef CONFIG_X`나 `IS_ENABLED(CONFIG_X)`로 검사한다. `nothread`의 헤더에는 두 줄 다 없다.
- `CONFIG_MULTITHREADING=n`: FLASH −2.5 KB(MPU를 뺀 lean 대비), RAM −549 B. 그래도 hello는 찍힌다 — main이 스레드가 아니라 그냥 부팅 코드의 연속으로 돈다. RAM이 크게 안 준 것은 인터럽트 스택 4 KB와 main 스택 1 KB가 그대로이기 때문이다.
- `CONFIG_TIMESLICING=n`: −412 B. 같은 우선순위 스레드끼리 시간 조각으로 돌리는 기능(C4)을 뺀 것.
- `CONFIG_PRINTK=n` + 콘솔 끄기: **−3.6 KB**, 그리고 출력 0줄. 콘솔 하나가 커널 전체(2.8 KB)보다 크다.

함정 하나를 실제로 밟았다: 처음 `lean.conf`에 `CONFIG_GPIO=n`을 넣었더니 링크가 실패했다.

```text
/var/rtoslab/zephyrproject/zephyr/drivers/pinctrl/pinctrl_arm_mps2.c:42:(.text.pinctrl_configure_pins+0x30): undefined reference to `cmsdk_ahb_gpio_config'
.../ld.bfd: zephyr/drivers/pinctrl/libdrivers__pinctrl.a(pinctrl_arm_mps2.c.obj):(.rodata.gpio_ports+0x0): undefined reference to `__device_dts_ord_28'
collect2: error: ld returned 1 exit status
```

(경로 앞부분은 줄였다.) 이 보드의 pinctrl 드라이버가 GPIO 드라이버의 함수와 GPIO 장치를 직접 참조하는데, `CONFIG_PINCTRL=n`을 함께 줘도 다른 곳에서 select되어 다시 `y`가 됐다(`.config`에서 확인). 결국 이 보드에서는 GPIO를 끌 수 없어서 MPU만 끄는 것으로 바꿨다. **Kconfig 조각은 "요청"이고, 최종 값은 의존성 해석 결과다** — 빌드 뒤 `build/zephyr/.config`를 확인하는 습관이 필요하다(H2).

### 5.6 흔한 함정 (설정)

- **`FreeRTOSConfig.h`를 여러 개 두기**: include 경로 순서에 따라 엉뚱한 것이 잡힌다. 랩 Makefile도 "앱 폴더를 include 경로 맨 앞에" 두는 방식으로 덮어쓰기를 구현한다. 의심되면 `#error`를 한 줄 넣어 어느 파일이 쓰이는지 확인한다.
- **설정과 오브젝트가 어긋나기**: FreeRTOS 라이브러리를 한 설정으로 미리 빌드해 두고 앱은 다른 설정 헤더로 빌드하면, 같은 `TCB_t`가 파일마다 크기가 달라진다. 컴파일 에러 없이 메모리가 깨진다. **커널과 앱은 반드시 같은 설정으로 함께 빌드한다.**
- **버전 매크로 믿기**: 예제 9에서 보겠지만 V11.3.2 소스의 `tskKERNEL_VERSION_NUMBER`는 `"V11.1.0+"`로 남아 있다. 버전은 `History.txt`나 git 태그로 확인한다.

---

## 6. 커널 객체는 RAM 속 구조체일 뿐

### 6.1 직관 — 핸들 = 구조체 포인터

`QueueHandle_t q = xQueueCreate(...)`의 `q`는 마법의 번호가 아니다. **RAM 어딘가에 있는 큐 구조체의 주소**다. 태스크 핸들은 TCB의 주소다. 동적 생성이면 그 구조체가 힙에서, 정적 생성이면 우리가 준 전역 변수에 놓인다. Zephyr의 `K_SEM_DEFINE`은 컴파일 시점에 그 전역 변수를 만들어 링커 섹션(`k_sem_area`)에 모은다. Don의 SSD 펌웨어에서 "명령 디스크립터 풀의 n번째 슬롯"을 핸들로 넘기던 것과 같다.

### 6.2 예제 9 — FreeRTOS 객체의 크기와 위치

무엇을 확인하나: FreeRTOS가 정적 할당용으로 공개하는 "불투명 크기 타입"(`StaticTask_t` 등 — 내부 구조체 `TCB_t`와 크기가 같도록 맞춘 더미 구조체)의 `sizeof`, 그리고 정적으로 만든 객체의 핸들이 정말 우리 전역 변수의 주소인지. 랩 템플릿 설정(trace·뮤텍스·알림·통계 켬)으로 빌드했다.

```c
/* 예제 9: 커널 객체 = RAM 속 구조체. 정적 할당 타입의 sizeof와, 정적으로 만든 객체의 주소를 본다 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
#include "event_groups.h"
#include "stream_buffer.h"
#include "board.h"

#define SZ(T) board_printf_raw("  %s%u\n", #T " = ", (unsigned)sizeof(T))

static StaticTask_t   blinky_tcb;                      /* TCB도 그냥 전역 변수 */
static StackType_t    blinky_stack[128];
static StaticQueue_t  q_obj;
static uint8_t        q_store[8 * sizeof(uint32_t)];   /* 큐 항목 저장소는 별도 */
static StaticSemaphore_t sem_obj;

static void blinky(void *arg)
{
    (void)arg;
    board_printf_raw("blinky running: TCB @0x%x, stack 0x%x..0x%x\n",
                     (unsigned)&blinky_tcb, (unsigned)blinky_stack,
                     (unsigned)(blinky_stack + 128));
    board_exit(0);
}

int main(void)
{
    board_printf_raw("FreeRTOS %s, configUSE_TRACE_FACILITY=%d, configUSE_MUTEXES=%d\n",
                     tskKERNEL_VERSION_NUMBER, configUSE_TRACE_FACILITY, configUSE_MUTEXES);
    SZ(StaticTask_t);  SZ(StaticQueue_t);  SZ(StaticSemaphore_t);
    SZ(StaticEventGroup_t);  SZ(StaticTimer_t);  SZ(StaticStreamBuffer_t);
    SZ(StaticListItem_t);  SZ(StackType_t);
    QueueHandle_t q = xQueueCreateStatic(8, sizeof(uint32_t), q_store, &q_obj);
    SemaphoreHandle_t s = xSemaphoreCreateBinaryStatic(&sem_obj);
    board_printf_raw("queue handle 0x%x == &q_obj 0x%x, sem handle 0x%x\n",
                     (unsigned)q, (unsigned)&q_obj, (unsigned)s);
    TaskHandle_t h = xTaskCreateStatic(blinky, "blinky", 128, NULL, 1,
                                       blinky_stack, &blinky_tcb);
    board_printf_raw("task handle 0x%x == &blinky_tcb\n", (unsigned)h);
    vTaskStartScheduler();
    return 1;
}
```

```sh
make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=$S/fr_obj/main.c OUT=/var/tmp/rtos/A3/fr_obj
/var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A3/fr_obj/app.elf
arm-none-eabi-nm -S /var/tmp/rtos/A3/fr_obj/app.elf | grep -E "blinky_tcb|blinky_stack|q_obj|q_store|sem_obj"
```

```text
   text	   data	    bss	    dec	    hex	filename
  11656	    100	 136200	 147956	  241f4	/var/tmp/rtos/A3/fr_obj/app.elf
FreeRTOS V11.1.0+, configUSE_TRACE_FACILITY=1, configUSE_MUTEXES=1
  StaticTask_t = 96
  StaticQueue_t = 84
  StaticSemaphore_t = 84
  StaticEventGroup_t = 32
  StaticTimer_t = 44
  StaticStreamBuffer_t = 40
  StaticListItem_t = 20
  StackType_t = 4
queue handle 0x200000dc == &q_obj 0x200000dc, sem handle 0x20000068
task handle 0x20000330 == &blinky_tcb
blinky running: TCB @0x20000330, stack 0x20000130..0x20000330
[board_exit] code=0
20000130 00000200 b blinky_stack
20000330 00000060 b blinky_tcb
200000dc 00000054 b q_obj
200000bc 00000020 b q_store
20000068 00000054 b sem_obj
```

출력에서 볼 것:

- **핸들 == 우리가 준 변수의 주소**다(큐 0x200000dc, 태스크 0x20000330). 커널 객체는 정말로 bss의 구조체다. `nm` 크기 0x60 = 96 = `sizeof(StaticTask_t)`.
- 버전 문자열이 `V11.1.0+`로 찍혔다 — 소스는 V11.3.2인데 `task.h`의 매크로가 갱신되지 않았다(§5.6 함정).
- 세마포어(84 B)와 큐(84 B)가 같은 크기 — §1.1에서 말한 "세마포어 = 항목 크기 0인 큐"의 증거다.
- 같은 타입이 예제 7의 최소 구성에서는 TCB 60 B, 큐 72 B였다. 96 − 60 = 36 B 차이는 이름 길이(`configMAX_TASK_NAME_LEN` 16 vs 8 → +8)와 예제 7에서 하나씩 잰 증가분 — 뮤텍스 +8, 알림 +8, trace +8, 런타임 통계 +4 — 의 합과 같다. 템플릿이 추가로 켜는 1바이트 필드들(정적·동적 할당 구분 플래그, `xTaskAbortDelay`용 플래그)은 기존 패딩 자리에 들어가서 크기를 더 늘리지 않았다.
- 태스크 스택이 TCB 바로 아래 주소(0x20000130..0x20000330)에 놓였다. 스택은 아래로 자라므로, 넘치면 TCB가 아니라 그 아래 변수를 덮는다 — 여기서는 우연히 이렇게 배치됐지만, 동적 생성에서 FreeRTOS가 일부러 "스택 먼저, TCB 나중"으로 할당하는 이유이기도 하다(§6.4).

### 6.3 예제 10 — Zephyr 객체의 크기 (설정에 따라)

무엇을 확인하나: Zephyr 커널 구조체의 크기, 그리고 스레드 관련 Kconfig 몇 개를 켰을 때 `struct k_thread`가 얼마나 커지는지. 기본 `prj.conf`는 `CONFIG_EVENTS=y`, `rich.conf`는 `THREAD_NAME`·`THREAD_MONITOR`·`THREAD_RUNTIME_STATS`를 켜고 MPU를 끈다.

```c
/* 예제 10: Zephyr 커널 객체의 크기. 설정에 따라 struct가 커지고 작아진다 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define SZ(T) printk("  %-22s = %3u\n", #T, (unsigned)sizeof(T))
K_SEM_DEFINE(irq_sem, 0, 1);                 /* 정적 정의: 링커가 k_sem_area에 모은다 */

int main(void)
{
	printk("THREAD_NAME=%d MONITOR=%d RUNTIME_STATS=%d MPU=%d\n",
	       IS_ENABLED(CONFIG_THREAD_NAME), IS_ENABLED(CONFIG_THREAD_MONITOR),
	       IS_ENABLED(CONFIG_THREAD_RUNTIME_STATS), IS_ENABLED(CONFIG_ARM_MPU));
	SZ(struct k_thread);   SZ(struct _thread_base);  SZ(struct _callee_saved);
	SZ(struct k_sem);      SZ(struct k_mutex);       SZ(struct k_msgq);
	SZ(struct k_timer);    SZ(struct k_event);       SZ(struct k_queue);
	SZ(struct k_mem_slab); SZ(struct k_work);        SZ(_wait_q_t);
	printk("irq_sem @%p, main thread @%p\n", &irq_sem, k_current_get());
	return 0;
}
```

```text
*** Booting Zephyr OS build v4.4.2 ***
THREAD_NAME=0 MONITOR=0 RUNTIME_STATS=0 MPU=1
  struct k_thread        = 136
  struct _thread_base    =  48
  struct _callee_saved   =  36
  struct k_sem           =  16
  struct k_mutex         =  20
  struct k_msgq          =  40
  struct k_timer         =  56
  struct k_event         =  12
  struct k_queue         =  16
  struct k_mem_slab      =  28
  struct k_work          =  16
  _wait_q_t              =   8
irq_sem @0x200000a4, main thread @0x20000140
*** Booting Zephyr OS build v4.4.2 ***
THREAD_NAME=1 MONITOR=1 RUNTIME_STATS=1 MPU=0
  struct k_thread        = 192
  struct _thread_base    =  64
  struct _callee_saved   =  36
  struct k_sem           =  16
  struct k_mutex         =  20
  struct k_msgq          =  40
  struct k_timer         =  56
  struct k_event         =  12
  struct k_queue         =  16
  struct k_mem_slab      =  28
  struct k_work          =  16
  _wait_q_t              =   8
irq_sem @0x200000a4, main thread @0x20000178
```

```sh
arm-none-eabi-nm -S /var/tmp/rtos/A3/zo/base/zephyr/zephyr.elf | grep -E "irq_sem|_k_sem_list|z_main_thread"
```

```text
200000b4 D _k_sem_list_end
200000a4 D _k_sem_list_start
200000a4 00000010 D irq_sem
20000140 00000088 B z_main_thread
```

출력에서 볼 것:

- `struct k_thread`가 기본 136 B → 이름·모니터·통계를 켜면 192 B. (예제 6의 hello 빌드에서는 `z_main_thread`가 128 B였다 — `CONFIG_EVENTS=y`가 스레드마다 `events`·`event_options` 필드 8 B를 더한다. `include/zephyr/kernel/thread.h`의 `#if defined(CONFIG_EVENTS)` 블록.)
- `_callee_saved` 36 B = R4–R11 8개 + PSP 1개 = 9 워드. 문맥 전환 때 소프트웨어가 저장하는 레지스터가 스레드 구조체 안에 산다(C2).
- `k_sem`은 16 B — 대기 큐(8 B) + count + limit. FreeRTOS 세마포어(84 B)보다 훨씬 작다. FreeRTOS는 세마포어를 범용 큐로 구현했고, Zephyr는 객체마다 전용 구조체를 둔 설계 차이다.
- `irq_sem`이 `_k_sem_list_start`와 같은 주소다: `K_SEM_DEFINE`이 객체를 **iterable section**에 넣고, 링커 스크립트가 시작·끝 심볼을 만들어 커널이 모든 정적 세마포어를 순회할 수 있게 한다(커널 객체 추적·셸 명령이 이걸 쓴다).

### 6.4 두 RTOS의 객체 크기 한눈에, 그리고 힙 속 배치

| 객체 | FreeRTOS 최소 (예제 7) | FreeRTOS 템플릿 (예제 9) | Zephyr 기본 (예제 10) | Zephyr 옵션 켬 |
|---|---|---|---|---|
| 태스크/스레드 제어 블록 | 60 | 96 | 136 (EVENTS 없으면 128) | 192 |
| 큐 / 메시지 큐 | 72 | 84 | 40 (`k_msgq`) | 40 |
| 세마포어 | 72 (큐와 같음) | 84 | 16 | 16 |
| 뮤텍스 | — (꺼짐) | 84 | 20 | 20 |
| 이벤트 그룹 / 이벤트 | — | 32 | 12 | 12 |
| 소프트웨어 타이머 | — | 44 | 56 | 56 |

(바이트. 큐·메시지 큐는 항목 저장 버퍼를 뺀 제어 구조체만.)

예제 4(최소 구성, heap_1)의 힙 안을 예제 12의 gdb가 찍은 주소로 그리면 다음과 같다. `ucHeap`은 0x2000006c에서 시작하고 heap_1이 8바이트로 정렬해서 0x20000070부터 쓴다.

```
ucHeap (heap_1, 4096 B) as allocated in example 4
0x20000070 +---------------------------+
           | stack of task A  512 B    |  grows down from 0x20000268
0x20000270 | TCB of task A    64 B     |  (60 B rounded up to 8)
0x200002b0 | stack of task B  512 B    |
0x200004b0 | TCB of task B    64 B     |
0x200004f0 | stack of IDLE    512 B    |  created inside vTaskStartScheduler
0x200006f0 | TCB of IDLE      64 B     |
0x20000730 +---------------------------+
           | free: 2360 B              |  = 4096 - 8 (alignment) - 3 x 576
0x2000106c +---------------------------+
```

동적 생성에서 FreeRTOS는 **스택을 먼저, TCB를 나중에** 할당한다(스택이 아래로 자라는 포트에서). 스택이 넘쳐도 자기 TCB가 아니라 아래쪽 메모리를 먼저 덮게 하려는 배치다. 하지만 위 그림처럼 태스크 B의 스택 바로 아래에는 **태스크 A의 TCB**가 있다 — 결국 넘치면 누군가의 TCB를 덮는다. 스택 오버플로 탐지(F3)가 필요한 이유가 이 그림 한 장에 있다.

커널 자체의 전역 변수도 몇 개 안 된다(예제 4 바이너리):

```sh
arm-none-eabi-nm -S /var/tmp/rtos/A3/fr_min/app.elf | grep -E "ucHeap|pxReadyTasksLists|xDelayedTaskList1|pxCurrentTCB|uxTopReadyPriority|xTickCount"
```

```text
2000112c 00000004 B pxCurrentTCB
200010dc 00000050 b pxReadyTasksLists
2000006c 00001000 b ucHeap
2000108c 00000004 b uxTopReadyPriority
200010c8 00000014 b xDelayedTaskList1
20001090 00000004 b xTickCount
```

`pxReadyTasksLists` = 0x50 = 80 B = 우선순위 4개 × `List_t` 20 B. `pxCurrentTCB` 하나가 "지금 누가 돌고 있나"의 전부다. 이 변수들의 의미와 O(1) 선택(`uxTopReadyPriority` 비트맵 + CLZ)은 C1에서, TCB의 필드와 상태 전이는 A4에서 본다.

---

## 7. 심장 박동과 "아무 일도 없을 때" — tick과 idle

이 둘은 C5(tick)와 C9(idle 태스크·훅)에서 깊게 다룬다. 여기서는 "구성 요소 지도" 위에서 자리만 잡는다.

- **tick**: 주기 타이머 인터럽트(Cortex-M은 SysTick). 커널의 시간은 tick 개수로 센다. tick마다 커널은 (1) tick 카운터를 올리고, (2) 지연·타임아웃이 끝난 태스크를 ready로 옮기고, (3) 더 높은 우선순위가 깨어났거나 시간 조각이 끝났으면 문맥 전환을 **예약**(PendSV pend)한다.
- **idle 태스크**: 가장 낮은 우선순위에서 "아무도 ready가 아닐 때" 도는 태스크. 스케줄러는 항상 누군가를 돌려야 하므로 반드시 하나 있다. 삭제된 태스크의 메모리 정리, idle 훅, 저전력 진입(WFI, tickless — C6)이 여기서 일어난다.

### 7.1 예제 11 — tick 핸들러와 idle 태스크의 실제 기계어

무엇을 확인하나: 포트 계층의 tick 핸들러가 정말 "커널에게 묻고 PendSV를 거는" 얇은 막인지, 그리고 최소 구성의 idle 태스크가 무엇을 하는지.

```sh
E=/var/tmp/rtos/A3/fr_min/app.elf
arm-none-eabi-objdump -d --no-show-raw-insn $E | awk "/<SysTick_Handler>:/,/^$/"
arm-none-eabi-objdump -d --no-show-raw-insn $E | awk "/<prvIdleTask>:/,/^$/"
arm-none-eabi-nm -S $E | grep -E "SysTick_Handler|PendSV_Handler|SVC_Handler|xTaskIncrementTick|vTaskSwitchContext|prvIdleTask"
```

```text
00000574 <SysTick_Handler>:
     574:	push	{r3, lr}
     576:	mov.w	r3, #32
     57a:	msr	BASEPRI, r3
     57e:	isb	sy
     582:	dsb	sy
     586:	bl	cd4 <xTaskIncrementTick>
     58a:	cbz	r0, 598 <SysTick_Handler+0x24>
     58c:	mov.w	r3, #3758153728	@ 0xe000e000
     590:	mov.w	r2, #268435456	@ 0x10000000
     594:	str.w	r2, [r3, #3332]	@ 0xd04
     598:	movs	r3, #0
     59a:	msr	BASEPRI, r3
     59e:	pop	{r3, pc}

00000694 <prvIdleTask>:
     694:	mov.w	r0, #3758153728	@ 0xe000e000
     698:	mov.w	r1, #268435456	@ 0x10000000
     69c:	ldr	r2, [pc, #20]	@ (6b4 <prvIdleTask+0x20>)
     69e:	b.n	6ac <prvIdleTask+0x18>
     6a0:	str.w	r1, [r0, #3332]	@ 0xd04
     6a4:	dsb	sy
     6a8:	isb	sy
     6ac:	ldr	r3, [r2, #0]
     6ae:	cmp	r3, #1
     6b0:	bhi.n	6a0 <prvIdleTask+0xc>
     6b2:	b.n	6b2 <prvIdleTask+0x1e>
     6b4:	.word	0x200010dc
```

```text
00000530 00000044 T PendSV_Handler
000004d8 00000024 T SVC_Handler
00000574 0000002c T SysTick_Handler
00000694 00000024 t prvIdleTask
00000cf4 0000005c T vTaskSwitchContext
00000cd4 00000020 T xTaskIncrementTick
00000730 0000019c t xTaskIncrementTick.part.0
```

출력에서 볼 것:

- `SysTick_Handler`(= `xPortSysTickHandler`) 전체가 **44 B, 명령 13개**다. BASEPRI를 0x20(`configMAX_SYSCALL_INTERRUPT_PRIORITY`)으로 올려 크리티컬 섹션에 들어가고 → `xTaskIncrementTick()`에 "전환이 필요한가?"를 묻고 → 필요하면 `0xE000ED04`(SCB ICSR)에 `0x10000000`(PENDSVSET 비트)을 써서 PendSV를 **예약만** 하고 → BASEPRI를 0으로 돌린다. 실제 전환은 SysTick이 끝난 뒤 가장 낮은 우선순위의 PendSV에서 일어난다(B4, C2). 결정은 커널(`tasks.c`), 실행은 포트 — 그림 2의 점선 그대로다.
- 포트의 예외 핸들러 셋(SysTick 44 B, PendSV 68 B, SVC 36 B)을 다 합쳐도 150 B 정도다.
- `prvIdleTask`는 36 B. `0x200010dc`는 `pxReadyTasksLists[0]`(우선순위 0 리스트)의 항목 수 필드다. "우선순위 0에 나 말고 다른 태스크가 있으면(항목 수 > 1) PendSV로 양보"(`configIDLE_SHOULD_YIELD`)만 한다. 이 최소 구성은 idle 훅·태스크 삭제·tickless가 모두 꺼져 있어서 할 일이 그것뿐이다. 마지막 `b.n 6b2`(자기 자신으로 점프)는 흥미롭다: 리스트의 항목 수 필드가 기본적으로 `volatile`이 아니라서(`list.h`의 `configLIST_VOLATILE` 기본값이 빈 매크로) 컴파일러가 "1 이하이면 다시 읽을 필요 없다"고 보고 루프를 접었다. 선점형 커널에서는 SysTick이 어차피 idle을 끊고 들어오므로 동작에는 문제없다 — 이런 디테일은 C9에서 다시 본다.
- 예제 5의 런타임 통계에서 IDLE이 **98%**였던 것을 기억하자. 드론 비행 제어기에서 "CPU 사용률 = 100% − idle 비율"로 부하를 재는 것이 이 숫자다(M 모듈).

Zephyr 쪽 대응: tick 인터럽트는 `drivers/timer/cortex_m_systick.c`의 `sys_clock_isr` → `sys_clock_announce()`(타임아웃 리스트 처리, `kernel/timeout.c`의 `sys_clock_announce_locked`) 경로이고, idle은 `kernel/idle.c`의 `idle()`이 `k_cpu_idle()`(WFI)로 잠든다. Zephyr는 기본이 **tickless**라서 주기 tick 대신 "다음 타임아웃 시각"에 맞춰 타이머를 다시 건다(C6).

---

## 8. RTOS 커널 안에서는 무슨 일이 일어나나 — `main()`부터 첫 태스크까지

지금까지 본 부품들이 부팅 때 어떤 순서로 불리는지 따라가면 지도가 완성된다. 각 함수의 깊은 내용(가짜 예외 프레임의 레이아웃, SVC로 첫 태스크에 들어가는 이유, `main()` 스택의 운명)은 C3이 맡는다. 여기서는 **"어느 파일의 어느 함수가 어느 순서로"**만 확인한다.

### 8.1 예제 12 — gdb 중단점으로 호출 순서 찍기

무엇을 확인하나: 예제 4의 바이너리를 QEMU에서 멈춘 상태로 띄우고(`-s -S`), gdb에서 경로상의 함수 7개에 중단점을 걸어 각 지점의 호출 스택(backtrace)을 찍는다.

```text
set pagination off
set confirm off
target remote :1234
break xTaskCreate
break pxPortInitialiseStack
break vTaskStartScheduler
break xPortStartScheduler
break vPortSetupTimerInterrupt
break SVC_Handler
break worker
commands 1-7
  silent
  printf "---- hit: "
  bt 4
  continue
end
continue
```

```sh
E=/var/tmp/rtos/A3/fr_min/app.elf
(timeout 8 qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none \
   -serial file:/var/tmp/rtos/A3/trace_uart.txt -semihosting-config enable=on,target=native \
   -kernel $E -s -S &) ; sleep 1
timeout 6 gdb -q -batch -x /Users/donh/.rtos_scratch/A3/trace.gdb $E
```

```text
---- hit: #0  xTaskCreate (pxTaskCode=pxTaskCode@entry=0x3e1 <worker>, pcName=pcName@entry=0x19c0 "A", uxStackDepth=uxStackDepth@entry=128, pvParameters=pvParameters@entry=0x19c0, uxPriority=uxPriority@entry=2, pxCreatedTask=pxCreatedTask@entry=0x0 <g_vectors>) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:1735
#1  0x0000044e in main () at /Users/donh/.rtos_scratch/A3/fr_min/main.c:22
---- hit: #0  pxPortInitialiseStack (pxTopOfStack=pxTopOfStack@entry=0x20000268 <ucHeap+508>, pxCode=pxCode@entry=0x3e1 <worker>, pvParameters=pvParameters@entry=0x19c0) at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:188
#1  0x00000958 in prvInitialiseNewTask (xRegions=0x0 <g_vectors>, pxTaskCode=0x3e1 <worker>, pcName=0x19c0 "A", uxStackDepth=128, pvParameters=0x19c0, uxPriority=2, pxCreatedTask=0x0 <g_vectors>, pxNewTCB=0x20000270 <ucHeap+516>) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:1996
#2  prvCreateTask (pxTaskCode=0x3e1 <worker>, pcName=0x19c0 "A", uxStackDepth=128, pvParameters=0x19c0, uxPriority=2, pxCreatedTask=0x0 <g_vectors>) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:1722
#3  xTaskCreate (...) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:1741
---- hit: #0  xTaskCreate (... pcName=pcName@entry=0x19c4 "B", ... uxPriority=uxPriority@entry=1 ...) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:1735
...
---- hit: #0  vTaskStartScheduler () at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:3592
#1  0x00000462 in main () at /Users/donh/.rtos_scratch/A3/fr_min/main.c:24
---- hit: #0  xTaskCreate (pxTaskCode=pxTaskCode@entry=0x695 <prvIdleTask>, pcName=pcName@entry=0x203fffc8 "IDLE", uxStackDepth=uxStackDepth@entry=128, pvParameters=pvParameters@entry=0x0 <g_vectors>, uxPriority=uxPriority@entry=0, pxCreatedTask=pxCreatedTask@entry=0x20001070 <xIdleTaskHandles>) at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:1735
#1  0x00000aaa in prvCreateIdleTasks () at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:3702
#2  vTaskStartScheduler () at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:3756
#3  0x00000462 in main () at /Users/donh/.rtos_scratch/A3/fr_min/main.c:24
...
---- hit: #0  xPortStartScheduler () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:390
#1  0x00000ad2 in vTaskStartScheduler () at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:3818
#2  0x00000462 in main () at /Users/donh/.rtos_scratch/A3/fr_min/main.c:24
---- hit: #0  vPortSetupTimerInterrupt () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:755
#1  0x000005e2 in xPortStartScheduler () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:396
#2  0x00000ad2 in vTaskStartScheduler () at /var/rtoslab/src/FreeRTOS-Kernel/tasks.c:3818
#3  0x00000462 in main () at /Users/donh/.rtos_scratch/A3/fr_min/main.c:24
---- hit: #0  SVC_Handler () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:230
#1  <signal handler called>
#2  0x000004ac in prvPortStartFirstTask () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:249
#3  0x000005ea in xPortStartScheduler () at /var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c:402
---- hit: #0  worker (arg=0x19c0) at /Users/donh/.rtos_scratch/A3/fr_min/main.c:8
#1  0x00000474 in main () at /Users/donh/.rtos_scratch/A3/fr_min/main.c:25
---- hit: #0  worker (arg=0x19c4) at /Users/donh/.rtos_scratch/A3/fr_min/main.c:8
#1  0x00000474 in main () at /Users/donh/.rtos_scratch/A3/fr_min/main.c:25
```

(같은 경로를 반복하는 B·IDLE의 `pxPortInitialiseStack` 적중과 긴 인자 목록 일부를 `...`로 줄였다.)

출력에서 볼 것:

- `pxPortInitialiseStack`의 `pxTopOfStack=ucHeap+508`, `pxNewTCB=ucHeap+516` — 스택 바로 위에 TCB. §6.4의 그림이 이 숫자에서 나왔다.
- IDLE 태스크는 우리가 만들지 않았다. `vTaskStartScheduler` → `prvCreateIdleTasks` → `xTaskCreate(prvIdleTask, "IDLE", …, 우선순위 0)`. 타이머를 켰다면 이 다음에 `xTimerCreateTimerTask`가 불린다.
- 첫 태스크는 **함수 호출이 아니라 예외 복귀로** 시작한다: `prvPortStartFirstTask` → `svc 0` → `SVC_Handler`(= `vPortSVCHandler`) 안에서 `#1 <signal handler called>` — gdb가 예외 진입을 이렇게 표시한다.
- 함정: `worker`의 backtrace가 `main () at main.c:25`라고 나오지만 거짓이다. `worker`는 `main`이 부른 적이 없다. 가짜 예외 프레임의 LR에는 `prvTaskExitError`(0x474)가 들어 있고, gdb는 "복귀 주소 − 1"(0x473)이 속한 함수를 찾는데 그게 바로 앞에 붙어 있는 `main`의 끝이라서 이렇게 보인다. 확인:

```sh
arm-none-eabi-nm $E | grep -iE "prvTaskExitError| main$"
```

```text
00000434 T main
00000474 t prvTaskExitError
```

RTOS 태스크의 backtrace는 **태스크 함수에서 끝나는 게 정상**이다. 그 아래에 무언가 보이면 가짜 프레임의 흔적으로 읽는다(RTOS-aware 디버깅은 M2).

```svg
<svg viewBox="0 0 680 486" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10.0" y="8.0" width="100.0" height="26.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="60" y="25" font-size="12" text-anchor="middle">app</text>
<rect x="130.0" y="8.0" width="540.0" height="26.0" rx="4" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="400" y="25" font-size="12" text-anchor="middle"></text>
<text x="142" y="26" font-size="12" text-anchor="start"> 1. main()</text>
<line x1="60" y1="34" x2="60" y2="40" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<rect x="10.0" y="41.0" width="100.0" height="26.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="60" y="58" font-size="12" text-anchor="middle">tasks.c</text>
<rect x="130.0" y="41.0" width="540.0" height="26.0" rx="4" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="400" y="58" font-size="12" text-anchor="middle"></text>
<text x="142" y="59" font-size="12" text-anchor="start"> 2. xTaskCreate(&quot;A&quot;) → prvCreateTask</text>
<line x1="60" y1="67" x2="60" y2="73" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<rect x="10.0" y="74.0" width="100.0" height="26.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="60" y="91" font-size="12" text-anchor="middle">heap_1.c</text>
<rect x="130.0" y="74.0" width="540.0" height="26.0" rx="4" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="400" y="91" font-size="12" text-anchor="middle"></text>
<text x="142" y="92" font-size="12" text-anchor="start"> 3. pvPortMalloc: 스택 512 B, 그다음 TCB</text>
<line x1="60" y1="100" x2="60" y2="106" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<rect x="10.0" y="107.0" width="100.0" height="26.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="60" y="124" font-size="12" text-anchor="middle">tasks.c</text>
<rect x="130.0" y="107.0" width="540.0" height="26.0" rx="4" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="400" y="124" font-size="12" text-anchor="middle"></text>
<text x="142" y="125" font-size="12" text-anchor="start"> 4. prvInitialiseNewTask: 이름·우선순위·리스트 항목</text>
<line x1="60" y1="133" x2="60" y2="139" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<rect x="10.0" y="140.0" width="100.0" height="26.0" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="60" y="157" font-size="12" text-anchor="middle">port.c</text>
<rect x="130.0" y="140.0" width="540.0" height="26.0" rx="4" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-width="1.2"/>
<text x="400" y="157" font-size="12" text-anchor="middle"></text>
<text x="142" y="158" font-size="12" text-anchor="start"> 5. pxPortInitialiseStack: 가짜 예외 프레임 (PC=worker)</text>
<line x1="60" y1="166" x2="60" y2="172" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<rect x="10.0" y="173.0" width="100.0" height="26.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="60" y="190" font-size="12" text-anchor="middle">tasks.c</text>
<rect x="130.0" y="173.0" width="540.0" height="26.0" rx="4" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="400" y="190" font-size="12" text-anchor="middle"></text>
<text x="142" y="191" font-size="12" text-anchor="start"> 6. prvAddNewTaskToReadyList → pxReadyTasksLists[2]</text>
<line x1="60" y1="199" x2="60" y2="205" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<rect x="10.0" y="206.0" width="100.0" height="26.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="60" y="223" font-size="12" text-anchor="middle">tasks.c</text>
<rect x="130.0" y="206.0" width="540.0" height="26.0" rx="4" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="400" y="223" font-size="12" text-anchor="middle"></text>
<text x="142" y="224" font-size="12" text-anchor="start"> 7. xTaskCreate(&quot;B&quot;) — 같은 경로</text>
<line x1="60" y1="232" x2="60" y2="238" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<rect x="10.0" y="239.0" width="100.0" height="26.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="60" y="256" font-size="12" text-anchor="middle">tasks.c</text>
<rect x="130.0" y="239.0" width="540.0" height="26.0" rx="4" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="400" y="256" font-size="12" text-anchor="middle"></text>
<text x="142" y="257" font-size="12" text-anchor="start"> 8. vTaskStartScheduler → prvCreateIdleTasks (IDLE)</text>
<line x1="60" y1="265" x2="60" y2="271" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<rect x="10.0" y="272.0" width="100.0" height="26.0" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="60" y="289" font-size="12" text-anchor="middle">timers.c</text>
<rect x="130.0" y="272.0" width="540.0" height="26.0" rx="4" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-width="1.2"/>
<text x="400" y="289" font-size="12" text-anchor="middle"></text>
<text x="142" y="290" font-size="12" text-anchor="start"> 9. (configUSE_TIMERS=1이면) xTimerCreateTimerTask</text>
<line x1="60" y1="298" x2="60" y2="304" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<rect x="10.0" y="305.0" width="100.0" height="26.0" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="60" y="322" font-size="12" text-anchor="middle">port.c</text>
<rect x="130.0" y="305.0" width="540.0" height="26.0" rx="4" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-width="1.2"/>
<text x="400" y="322" font-size="12" text-anchor="middle"></text>
<text x="142" y="323" font-size="12" text-anchor="start">10. xPortStartScheduler: PendSV·SysTick 최저 우선순위</text>
<line x1="60" y1="331" x2="60" y2="337" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<rect x="10.0" y="338.0" width="100.0" height="26.0" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="60" y="355" font-size="12" text-anchor="middle">port.c</text>
<rect x="130.0" y="338.0" width="540.0" height="26.0" rx="4" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-width="1.2"/>
<text x="400" y="355" font-size="12" text-anchor="middle"></text>
<text x="142" y="356" font-size="12" text-anchor="start">11. vPortSetupTimerInterrupt: SysTick 1 kHz 시작</text>
<line x1="60" y1="364" x2="60" y2="370" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<rect x="10.0" y="371.0" width="100.0" height="26.0" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="60" y="388" font-size="12" text-anchor="middle">port.c</text>
<rect x="130.0" y="371.0" width="540.0" height="26.0" rx="4" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-width="1.2"/>
<text x="400" y="388" font-size="12" text-anchor="middle"></text>
<text x="142" y="389" font-size="12" text-anchor="start">12. prvPortStartFirstTask: MSP 리셋 → svc 0</text>
<line x1="60" y1="397" x2="60" y2="403" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<rect x="10.0" y="404.0" width="100.0" height="26.0" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.2"/>
<text x="60" y="421" font-size="12" text-anchor="middle">port.c</text>
<rect x="130.0" y="404.0" width="540.0" height="26.0" rx="4" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-width="1.2"/>
<text x="400" y="421" font-size="12" text-anchor="middle"></text>
<text x="142" y="422" font-size="12" text-anchor="start">13. vPortSVCHandler: 첫 TCB 스택 복원 → 예외 복귀</text>
<line x1="60" y1="430" x2="60" y2="436" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah8)"/>
<rect x="10.0" y="437.0" width="100.0" height="26.0" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="60" y="454" font-size="12" text-anchor="middle">app</text>
<rect x="130.0" y="437.0" width="540.0" height="26.0" rx="4" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-width="1.2"/>
<text x="400" y="454" font-size="12" text-anchor="middle"></text>
<text x="142" y="455" font-size="12" text-anchor="start">14. worker(&quot;A&quot;) 첫 명령 실행</text>
<text x="130" y="476" font-size="12" text-anchor="start">예제 12에서 gdb 중단점으로 실제 호출 순서를 확인했다 (FreeRTOS 최소 구성)</text>
</svg>
```

그림 9 — 예제 12로 확인한 `main()` → 첫 태스크 경로. 왼쪽 상자가 그 단계를 담당하는 파일이다. 초록(커널 코어)이 "무엇을 만들지"를 정하고, 주황(포트)이 "CPU를 어떻게 세팅할지"를 실행한다.

### 8.2 Zephyr에서는 순서가 다르다 — main도 스레드다

FreeRTOS에서 `main()`은 스케줄러 **이전**의 평범한 C 함수이고, `vTaskStartScheduler()`에서 돌아오지 않는다. Zephyr에서는 커널이 먼저 뜨고 **`main()`이 main 스레드 안에서** 불린다(그래서 예제 6에서 `main`이 `return 0`으로 끝나도 다른 스레드가 계속 돌았다). 소스로 확인한 경로:

| 단계 | 파일 · 함수 | 하는 일 |
|---|---|---|
| 1 | `arch/arm/core/cortex_m/reset.S` · `z_arm_reset` | 리셋 벡터, 스택 설정 |
| 2 | `arch/arm/core/cortex_m/prep_c.c` · `z_prep_c` | `.data` 복사, `.bss` 0, 벡터 재배치 → `z_cstart()` |
| 3 | `kernel/init.c` · `z_cstart` | 초기화 레벨(EARLY, PRE_KERNEL_1/2) 실행 — 드라이버 init이 여기서 |
| 4 | `kernel/init.c` · `prepare_multithreading` | main 스레드·idle 스레드를 `z_setup_new_thread`로 만든다 |
| 5 | `kernel/init.c` · `switch_to_main_thread` → `arch_switch_to_main_thread` | 첫 문맥 전환 |
| 6 | `kernel/init.c` · `bg_thread_main` | POST_KERNEL·APPLICATION 레벨 init, `z_init_static_threads()`(`K_THREAD_DEFINE` 스레드 시작) |
| 7 | 앱 · `main()` | main 스레드에서 실행. return하면 main 스레드만 끝난다 |

부팅 중간에 "드라이버 초기화 레벨"이 끼어 있는 것이 Zephyr의 특징이다 — 커널·드라이버·앱이 한 트리에 있어서 커널이 드라이버 init 순서까지 관리한다(H5).

---

## 9. 실무에서 다시 보기

### 9.1 드론 비행 제어기 — "FLASH는 남는데 RAM이 모자라다"

STM32H7급 비행 제어기는 FLASH가 1~2 MB라 커널 10 KB는 문제가 아니다. 문제는 RAM, 그것도 **빠른 RAM(DTCM 등)**이다. 이 노트의 숫자로 보면 RAM의 주인은 스택이다. 그래서 첫 번째 일은 태스크 수 × 스택 크기 예산표를 만들고(F3), 커널 설정 중 TCB를 키우는 것(trace, 통계)은 개발 빌드에서만 켜는 것이다. 예제 7처럼 "설정 하나 = 모든 태스크에 곱해지는 필드"라는 감각이 있으면 리뷰에서 바로 짚을 수 있다.

### 9.2 모터 컨트롤러 — 64 KB FLASH에 FOC + CAN + RTOS

작은 Cortex-M4 모터 컨트롤러(FLASH 64~128 KB)에서는 printf가 진짜 적이다. 예제 3에서 `%f` 하나가 13.7 KB였다. 전류·속도 로그를 float로 찍고 싶으면 (1) 정수 고정소수점으로 바꿔 찍거나, (2) 바이너리 그대로 CAN/UART로 보내고 호스트에서 포맷한다. 커널은 FreeRTOS 최소 구성 + 큐·뮤텍스 정도면 5 KB 안쪽이다.

### 9.3 센서 허브 MCU (Zephyr) — 개발 빌드와 양산 빌드를 설정 조각으로 나누기

예제 6에서 셸이 30 KB였다. 개발 중에는 셸로 센서 레지스터를 들여다보는 게 너무 편하니 켜 두고, 양산은 끈다. Zephyr에서는 앱 코드를 건드리지 않고 조각 파일로 나눈다:

```text
# prj.conf      (공통)
CONFIG_EVENTS=y
# debug.conf    (개발)  west build ... -- -DEXTRA_CONF_FILE=debug.conf
CONFIG_LOG=y
CONFIG_SHELL=y
CONFIG_THREAD_NAME=y
# prod.conf     (양산)  west build ... -- -DEXTRA_CONF_FILE=prod.conf
CONFIG_LOG=n
CONFIG_PRINTK=n
CONFIG_ASSERT=n
```

(설정 예시이며 이 노트에서 양산용 조합을 따로 빌드해 보지는 않았다. 각 심볼의 존재는 v4.4.2 Kconfig에서 확인했다.) CI에서 두 빌드의 FLASH·RAM을 매번 기록하고, 직전 대비 몇 % 이상 늘면 실패시키는 "footprint 게이트"를 두는 팀이 많다. Zephyr는 `west build -t rom_report` / `ram_report`가 예제 6처럼 트리 형태 보고서를 준다.

### 9.4 SSD 펌웨어 경험을 RTOS 말로

Don이 SSD 컨트롤러에서 했던 "펌웨어 이미지가 SRAM 영역을 넘었다 → map 파일에서 가장 큰 심볼을 찾고 → 디버그 문자열을 걷어내고 → 버퍼 풀 크기를 조정"이 그대로 RTOS footprint 작업이다. 면접에서 "코드 크기를 줄여 본 경험"을 물으면 이 경험 + "RTOS에서는 커널 설정 매크로와 libc 의존 그래프가 추가 손잡이"라고 말하면 된다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 디버그용 `printf("%f")` 하나 추가 | FLASH가 갑자기 10 KB 이상 늘어 링크 실패 | `_printf_float` + `_dtoa_r` + 소프트 double 연산 | 정수 포맷, 고정소수점, 호스트 측 포맷(딕셔너리 로깅) |
| trace·런타임 통계를 양산에도 켜 둠 | RAM 부족, 전환 시간 증가 | TCB 필드 증가 × 태스크 수, 전환마다 타이머 읽기 | 개발/양산 설정 분리 |
| `configUSE_TIMERS=1`인데 타이머 안 씀 | 모르는 태스크 `Tmr Svc`가 RAM을 씀 | 타이머 서비스 태스크 + 명령 큐가 자동 생성 | 안 쓰면 0. 쓰면 스택 깊이(`configTIMER_TASK_STACK_DEPTH`)를 산정 |
| 커널 라이브러리와 앱을 다른 `FreeRTOSConfig.h`로 빌드 | 랜덤 크래시, 리스트 손상 | 파일마다 `TCB_t` 크기가 다름 | 커널을 앱과 같은 설정으로 함께 빌드 |
| 힙 크기를 "넉넉히" 128 KB | RAM 대부분이 놀고 있음 (예제 5에서 3.6 KB만 사용) | `ucHeap`은 통째로 bss | `xPortGetMinimumEverFreeHeapSize()`로 재고 줄이기, 정적 할당 고려 (F1) |
| `configASSERT`를 무거운 로그 함수로 정의 | 커널 전체가 로그·printf를 끌고 옴 | assert가 모든 커널 파일에 들어감 | 가벼운 `__BKPT` 또는 리셋 사유 기록 함수로 (M1) |
| Zephyr에서 Kconfig 조각이 안 먹음 | 끈 기능이 여전히 링크됨 / 링크 에러 | 다른 심볼이 `select`함, 보드 의존성 | `build/zephyr/.config` 확인, `menuconfig`로 누가 select하는지 확인 |
| map 없이 "커널이 크다"고 판단 | 엉뚱한 곳 최적화 | 총합만 보고 컴포넌트를 안 봄 | map/`nm --size-sort`/`rom_report`로 쪼개 보기 |
| ISR 스택·main 스택을 계산에서 빼먹음 | RAM 계산은 맞는데 실제로 넘침 | MSP 스택은 `size`에 안 잡힘(랩 링커) | 링커 스크립트의 예약 영역까지 예산에 포함 |

---

## 11. 면접에서 이렇게 말한다

**Q.** "What are the main components of an RTOS?"

**A.** "층으로 말한다. 맨 아래 칩별 포트 계층 — 컨텍스트 스위치(PendSV), 크리티컬 섹션(BASEPRI), tick 타이머, 첫 태스크 시작. 그 위에 칩과 무관한 커널 코어 — 스케줄러, 태스크 관리, tick 기반 시간, 큐·세마포어·뮤텍스 같은 IPC, 소프트웨어 타이머, 메모리 할당자. 그 위에 미들웨어(TCP/IP, 파일시스템, 로깅)와 앱이 있고, 옆에 HAL·드라이버가 있다. FreeRTOS는 커널과 포트만 주고 Zephyr는 드라이버와 미들웨어까지 준다."

> I think of it in layers. At the bottom is a small, CPU-specific port layer: the context-switch handler, which on Cortex-M is PendSV, critical sections using BASEPRI, the tick timer, and the code that launches the first task. Above it is the portable kernel core: the scheduler, task management, tick-based timekeeping, IPC objects like queues, semaphores and mutexes, software timers, and a memory allocator. On top sit middleware — networking, file systems, logging, a shell — and the application tasks, with the HAL and drivers alongside. FreeRTOS gives you essentially the kernel and the port; Zephyr also ships the device model, drivers and middleware.

**꼬리 질문**: "Which part do you rewrite when you move to a new chip?" → 포트 계층(`port.c` + `portmacro.h`, FreeRTOS ARM_CM3는 코드 373줄)과 BSP. 커널 코어는 그대로.

**Q.** "How big is FreeRTOS? How much flash and RAM does it need?"

**A.** "설정에 달렸고 직접 재 봤다. Cortex-M3, `-O2`에서 선점 스케줄러 + 태스크 + delay만 쓰면 커널 코드가 약 2.4 KB, 커널 변수는 200 B 정도다. 큐·타이머·이벤트 그룹·뮤텍스·trace를 다 켜면 커널이 약 10.7 KB. RAM은 커널이 아니라 태스크 스택과 힙이 먹는다. 그리고 실제 이미지에서 커널보다 큰 건 보통 libc다 — printf에 `%f`를 넣으면 혼자 13 KB가 넘게 붙었다."

> It depends on configuration, so I measured it. On a Cortex-M3 at -O2, a minimal build — preemptive scheduler, tasks and delays — is about 2.4 KB of kernel code and a couple hundred bytes of kernel data. Turning on queues, software timers, event groups, mutexes and the trace facility brings the kernel to roughly 10.7 KB. RAM is dominated not by the kernel but by task stacks and the heap. And in a real image the C library is often bigger than the kernel: adding a single printf with %f pulled in over 13 KB of float formatting and soft-float code.

**꼬리 질문**: "And Zephyr?" → 같은 보드에서 hello 12.5 KB(커널 2.8 KB + 드라이버 2.9 KB + arch 2.5 KB + 콘솔), 비슷한 앱 15.3 KB. 로깅 +9 KB, 셸 +30 KB.

**Q.** "Our firmware no longer fits in flash. How do you find out what's taking space?"

**A.** "추측하지 않고 쪼갠다. `size`로 text/data/bss를 보고, map 파일을 오브젝트·라이브러리 단위로 합산하고, `nm --size-sort`로 가장 큰 심볼을 본다. Zephyr면 `rom_report`가 트리로 준다. 그러면 보통 범인이 나온다 — float printf, 셸·로그 문자열, 안 쓰는데 켜 둔 기능, libc 함수 하나가 끌고 온 그래프. 그다음 `-ffunction-sections`/`--gc-sections`, 설정 정리, 개발·양산 빌드 분리, 필요하면 `-Os`나 LTO."

> I don't guess; I break it down. size gives text, data and bss; then I aggregate the linker map by object file and library, and nm --size-sort shows the largest symbols. On Zephyr, the rom_report target gives the same as a tree. The culprit is usually obvious after that — float printf, shell or log strings, features enabled but unused, or one libc call dragging in a whole dependency graph. Then I make sure function-sections and gc-sections are on, trim the config, split debug and production builds, and only then reach for -Os or LTO.

**Q.** "What's the difference between a library-style RTOS kernel and a microkernel?"

**A.** "FreeRTOS나 기본 Zephyr는 라이브러리형이다 — 커널이 앱과 한 이미지, 한 주소 공간에 링크되고 커널 호출은 그냥 함수 호출이다. 빠르고 작지만 격리가 없어서 한 태스크의 포인터 버그가 전체를 죽인다. 마이크로커널(QNX, seL4)은 드라이버와 서비스를 별도 프로세스로 돌리고 메시지로 통신한다. 격리와 재시작이 가능한 대신 호출마다 IPC와 문맥 전환 비용이 든다. MCU에서는 Hubris처럼 MPU로 태스크를 격리하고 태스크 목록을 빌드 때 고정하는 중간 형태도 있다. Linux의 monolithic 구조와의 비교는 같은 축의 다른 점이다."

> In a library-style kernel like FreeRTOS or default Zephyr, the kernel is linked with the application into one image and one address space, and a kernel call is just a function call. It's fast and tiny, but there's no isolation, so a stray pointer in one task can take down the whole system. A microkernel such as QNX or seL4 moves drivers and services into separate processes that talk by message passing, so you get isolation and restartable services at the cost of IPC and context switches on every service call. On microcontrollers there's a middle ground, like Hubris, which isolates tasks with the MPU and fixes the task set at build time.

**꼬리 질문**: "Can you get isolation with FreeRTOS or Zephyr?" → FreeRTOS-MPU의 restricted task, Zephyr `CONFIG_USERSPACE`(memory domain + syscall). 켜면 API 호출이 SVC 시스템 콜이 되어 비용이 는다(C8, F5).

**Q.** "Why is RTOS configuration done at compile time? What does FreeRTOSConfig.h actually do?"

**A.** "MCU에는 런타임 모듈 로더가 없고, 메모리가 빠듯하고, 인증·분석을 위해 결정적인 고정 이미지가 필요해서다. FreeRTOSConfig.h는 `#define` 모음이고 커널 소스가 `#if`로 코드와 구조체 필드를 넣고 뺀다. 그래서 설정이 코드뿐 아니라 TCB 모양까지 바꾼다 — 실측으로 `configUSE_MUTEXES` 하나에 TCB가 60에서 68 B가 됐고, 앱이 뮤텍스를 안 써도 모든 태스크에 붙는다. `configUSE_TIMERS`는 타이머 서비스 태스크를 만들어서 2 KB가 늘었다. Zephyr는 같은 역할을 Kconfig와 빌드 시점 devicetree가 하고, Kconfig가 컴파일할 커널 파일 목록까지 정한다."

> Microcontrollers have no module loader, memory is tight, and for certification and timing analysis you want a fixed, deterministic image. FreeRTOSConfig.h is just a set of defines; the kernel sources use #if to include or exclude code and even struct fields. So configuration changes data layout, not just code: when I measured it, enabling configUSE_MUTEXES grew the TCB from 60 to 68 bytes for every task even though the app never used a mutex, and configUSE_TIMERS added about 2 KB because it creates the timer service task and its command queue. Zephyr does the same with Kconfig and a build-time devicetree, and Kconfig even decides which kernel source files get compiled.

**Q.** "Walk me through what happens when you call vTaskStartScheduler()."

**A.** "`tasks.c`에서 idle 태스크를 만들고(타이머를 켰으면 타이머 서비스 태스크도), 포트의 `xPortStartScheduler`로 넘어간다. 포트는 PendSV와 SysTick을 최저 우선순위로 설정하고 SysTick을 tick 주기로 시작한 뒤, MSP를 벡터 테이블의 초기값으로 되돌리고 `svc 0`을 실행한다. SVC 핸들러가 `pxCurrentTCB`의 저장된 스택 포인터에서 R4–R11을 복원하고 PSP를 세팅한 뒤 예외 복귀하면, 하드웨어가 생성 때 만들어 둔 가짜 예외 프레임을 꺼내서 태스크 함수의 첫 명령으로 점프한다. 그래서 첫 태스크는 호출이 아니라 예외 복귀로 시작되고, `vTaskStartScheduler`는 돌아오지 않는다. gdb 중단점으로 이 순서를 직접 확인했다."

> In tasks.c it creates the idle task — and the timer service task if timers are enabled — then hands off to the port's xPortStartScheduler. The port sets PendSV and SysTick to the lowest priority, starts SysTick at the tick rate, resets MSP to the initial value from the vector table, and executes svc 0. The SVC handler loads the saved stack pointer from pxCurrentTCB, restores R4 to R11, sets PSP, and does an exception return; the hardware then unstacks the fake exception frame that was built when the task was created and lands on the first instruction of the task function. So the first task starts via an exception return, not a call, and vTaskStartScheduler never returns. I traced exactly this sequence with gdb breakpoints.

**꼬리 질문**: "What happens if a task function returns?" → 가짜 프레임의 LR이 `prvTaskExitError`라서 거기로 간다 — assert 또는 무한 루프(C3).

**Q.** "Where does the memory for a task come from?"

**A.** "TCB와 스택 두 덩어리다. 동적 생성(`xTaskCreate`)이면 `pvPortMalloc`으로 FreeRTOS 힙에서 — 스택을 먼저, TCB를 나중에 — 받고, 정적 생성(`xTaskCreateStatic`)이면 우리가 준 전역 배열과 `StaticTask_t` 변수를 그대로 쓴다. 핸들은 TCB의 주소일 뿐이다. 실측으로 최소 구성에서 태스크 하나가 스택 512 B + TCB 64 B를 힙에서 가져갔다. 인증 프로젝트는 보통 정적 할당만 쓴다."

> Two pieces: the TCB and the stack. With xTaskCreate both come from the FreeRTOS heap via pvPortMalloc — stack first, then TCB, on a downward-growing stack port. With xTaskCreateStatic the kernel uses the StaticTask_t variable and stack array you pass in, and the handle is simply the address of that TCB. In my minimal build each task took a 512-byte stack plus a 64-byte TCB from the heap. Safety-critical projects usually go fully static.

**꼬리 질문**: "Which heap scheme would you pick?" → F1에서. 할당만 하고 해제하지 않으면 heap_1(결정적), 해제가 필요하면 heap_4, 여러 RAM 영역이면 heap_5.

---

## 12. 직접 해보기

1. **개념 — 층 분류**: 다음 함수가 어느 층(앱/미들웨어/커널 코어/포트/HAL)에 속하는지 적어라: `xQueueSend`, `xPortPendSVHandler`, `HAL_UART_Transmit`, `LOG_INF`, `k_msgq_put`, `z_arm_pendsv`, `vListInsert`, `pvPortMalloc`, `sys_clock_isr`.
   정답/힌트: 커널(`xQueueSend`, `k_msgq_put`, `vListInsert`, `pvPortMalloc`은 커널 쪽 메모리 할당자), 포트(`xPortPendSVHandler`, `z_arm_pendsv`), HAL(`HAL_UART_Transmit`), 미들웨어(`LOG_INF`), `sys_clock_isr`은 Zephyr에서 드라이버(`drivers/timer`)지만 역할은 tick — Zephyr는 tick 타이머를 드라이버로 둔다.

2. **측정 — 이벤트 그룹의 값**: 예제 4의 최소 구성에 `-DconfigUSE_EVENT_GROUPS=1`이 먹도록 설정 헤더를 고치고(`#ifndef`로 감싸기), 태스크 A가 `xEventGroupSetBits`, B가 `xEventGroupWaitBits`로 동기화하게 바꿔라. `fp.py`로 `event_groups.c`와 `tasks.c`가 각각 얼마 늘었는지 재라.
   정답/힌트: 이벤트 그룹은 `tasks.c`의 unordered event list 함수들(`vTaskPlaceOnUnorderedEventList` 등)을 끌고 온다 — 예제 1의 `event_groups.o -> tasks` 6개. 늘어난 양을 두 파일로 나눠 보라.

3. **측정 — printf 함정 더 보기**: 예제 3-b를 `%f`로 바꾸되 `-u _printf_float` 링크 옵션을 **빼고** 빌드·실행하라. 크기와 출력이 어떻게 되는가?
   정답/힌트: newlib-nano는 기본으로 float 포맷 코드를 빼 둔다. 크기는 작게 유지되지만 출력이 기대와 다르다 — 무엇이 찍히는지 직접 보고, "작은 빌드가 조용히 틀린 출력을 내는" 위험을 기록하라.

4. **측정 — Zephyr 로깅 모드**: 예제 6의 `log.conf`에 `CONFIG_LOG_MODE_MINIMAL=y`를 더해(다른 모드 대신) 빌드하고 FLASH·RAM을 비교하라. 출력 형식은 어떻게 바뀌나?
   정답/힌트: 세 모드(`LOG_MODE_DEFERRED`, `LOG_MODE_IMMEDIATE`, `LOG_MODE_MINIMAL`)는 `subsys/logging/Kconfig.mode`에 있다. minimal은 로그 처리 스레드와 버퍼가 없어 RAM이 크게 준다. 대가는 무엇인지(타임스탬프·필터·지연 처리) 정리하라.

5. **코드 — 힙 없는 FreeRTOS**: 예제 4를 `configSUPPORT_DYNAMIC_ALLOCATION=0`, `configSUPPORT_STATIC_ALLOCATION=1`, `configKERNEL_PROVIDED_STATIC_MEMORY=1`로 바꾸고 `xTaskCreateStatic`만 쓰게 고쳐라. 힙 파일은 무엇을 링크하나? RAM과 FLASH는?
   정답/힌트: 힙이 필요 없으므로 `ucHeap` 4 KB가 사라지고 그만큼(정확히는 태스크 스택·TCB 크기만큼 덜) 줄어든다. 랩 Makefile은 heap 파일을 항상 하나 링크하지만 아무도 `pvPortMalloc`을 부르지 않으면 `--gc-sections`가 버린다 — `fp.py`로 heap 항목이 0이 되는지 확인하라.

6. **gdb — tick 세기**: 예제 12의 방식으로 `xTaskIncrementTick`에 중단점을 걸고 `ignore`와 `info breakpoints`의 hit count로 "첫 태스크 시작부터 `board_exit`까지 tick이 몇 번 왔나"를 세라. 출력의 `[30 ms]`와 맞는가?
   정답/힌트: 1 kHz tick이므로 30 ms 근처면 30여 번. 정확히 맞추려면 `board_exit`에도 중단점을 건다.

7. **설계 — 예산표**: FLASH 128 KB / RAM 32 KB인 Cortex-M4 모터 컨트롤러에 FreeRTOS + CAN 드라이버 + FOC + 간단한 명령 셸을 넣는다. 이 노트의 숫자로 커널·libc·스택(태스크 4개 + ISR)·힙·셸에 예산을 나누고, 가장 위험한 항목 두 개를 적어라.
   정답/힌트: 커널 5~11 KB, libc는 printf 정책에 따라 3~20 KB, 스택이 RAM의 대부분. 위험 1순위는 float printf와 셸 문자열(FLASH), 2순위는 ISR·태스크 스택 과소 산정(RAM, F3).

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| kernel core | 커널 코어 | 스케줄러·태스크·시간·IPC·타이머·할당자. 칩과 무관한 C 코드 |
| port layer | 포트 계층 | 문맥 전환·크리티컬 섹션·tick·첫 태스크 시작을 맡는 CPU별 코드 (`port.c`, `arch/`) |
| HAL / BSP | 하드웨어 추상화 계층 / 보드 지원 패키지 | 레지스터를 만지는 드라이버·startup·링커 스크립트·보드 정의 |
| middleware | 미들웨어 | 커널 API 위에서 도는 재사용 라이브러리 (TCP/IP, FS, USB, 로깅, 셸) |
| library-style kernel | 라이브러리형 커널 | 앱과 한 이미지·한 주소 공간으로 링크되는 커널. 커널 호출 = 함수 호출 |
| microkernel | 마이크로커널 | 서비스를 별도 프로세스로 두고 메시지로 통신하는 커널 (QNX, seL4) |
| footprint | 메모리 점유 | 이미지가 차지하는 FLASH(text+data)와 RAM(data+bss) |
| text / data / bss | ELF 섹션 묶음 | 코드·상수 / 초기값 있는 변수 / 0으로 시작하는 변수 |
| map file | 링커 맵 파일 | 어느 오브젝트의 어느 섹션이 어디에 몇 바이트로 놓였는지 적은 링커 출력 |
| `--gc-sections` | 섹션 가비지 컬렉션 | 참조되지 않는 섹션(함수)을 링크 때 버리는 옵션. `-ffunction-sections`와 짝 |
| FreeRTOSConfig.h | FreeRTOS 설정 헤더 | `#define`으로 커널 기능·구조체 모양·크기를 정하는 앱 쪽 헤더 |
| Kconfig / prj.conf | Zephyr 설정 언어 / 앱 설정 파일 | 기능 심볼을 정하고 `autoconf.h`의 `CONFIG_` 매크로가 된다 |
| devicetree (build-time) | 빌드 시점 장치 트리 | Zephyr에서 하드웨어 기술을 `DT_` 매크로 헤더로 바꿔 컴파일에 쓰는 것 |
| TCB | Task Control Block | 태스크마다 하나씩 있는 커널 구조체 (A4) |
| `StaticTask_t` | 정적 TCB 타입 | 내부 `TCB_t`와 크기를 맞춘 공개 더미 구조체. 정적 생성 때 메모리로 준다 |
| handle | 핸들 | 커널 객체 구조체의 주소. 태스크 핸들 = TCB 주소 |
| iterable section | 반복 가능 섹션 | Zephyr가 같은 종류의 정적 객체를 한 링커 섹션에 모아 시작·끝 심볼로 순회하는 방식 |
| tick | 틱 | 커널 시간의 단위가 되는 주기 타이머 인터럽트 |
| idle task | idle 태스크 | 아무 태스크도 ready가 아닐 때 도는 최저 우선순위 태스크 |
| timer service task | 타이머 서비스 태스크 | FreeRTOS 소프트웨어 타이머 콜백을 실행하는 커널 태스크 (`Tmr Svc`) |
| PendSV / SVC / SysTick | Cortex-M 시스템 예외 | 문맥 전환 / 첫 태스크 시작(·시스템 콜) / tick 타이머 |
| fake exception frame | 가짜 예외 프레임 | 새 태스크 스택에 미리 쌓아 두는, 예외 복귀가 꺼낼 레지스터 값 (C3) |

---

## 14. 요약 & 체크리스트

RTOS는 앱과 함께 링크되는 부품 상자다. 칩과 무관한 **커널 코어**(스케줄러·태스크·tick·IPC·타이머·할당자) 아래에 아주 얇은 **포트 계층**(PendSV·BASEPRI·SysTick·SVC)이 있고, 그 사이의 통로는 "tick마다·전환마다 커널에게 묻는" 세 심볼뿐이다. FreeRTOS는 커널과 포트만, Zephyr는 드라이버·미들웨어까지 준다. 구조는 대부분 라이브러리형(한 주소 공간, 함수 호출)이고, 격리가 필요하면 MPU 옵션이나 마이크로커널·Hubris 같은 다른 설계로 간다. 무엇을 넣을지는 전부 **빌드 시점 설정**이 정하며, 설정 하나가 코드뿐 아니라 **모든 태스크에 곱해지는 구조체 필드**를 바꾼다. 측정해 보면 커널은 2.4~10.7 KB(FreeRTOS)로 작고, RAM은 스택과 힙이, FLASH의 큰 덩어리는 libc(특히 float printf)와 미들웨어(셸 30 KB)가 차지한다. 크기 문제는 추측하지 말고 map 파일로 쪼갠다.

- [ ] RTOS 펌웨어의 층(앱·미들웨어·커널 코어·포트·HAL)을 그리고 각 층의 FreeRTOS·Zephyr 파일을 하나씩 댈 수 있다
- [ ] 커널 코어의 여섯 부품과 FreeRTOS에서 그것들이 대부분 `tasks.c`·`queue.c`에 있다는 것을 안다
- [ ] 포트 계층이 제공하는 다섯 가지와 포트 → 커널 통로(`xTaskIncrementTick`, `vTaskSwitchContext`)를 설명할 수 있다
- [ ] 라이브러리형 커널과 마이크로커널(QNX·seL4), Hubris의 차이를 비용·격리·동적성으로 비교할 수 있다
- [ ] text/data/bss를 FLASH/RAM으로 바꿔 읽고, `size`에 안 잡히는 메모리(MSP 스택)를 안다
- [ ] map 파일이나 `nm --size-sort`로 컴포넌트별 크기를 직접 낼 수 있다 (예제 4의 `fp.py`)
- [ ] FreeRTOS 최소 커널 ≈ 2.4 KB, 풀 구성 ≈ 10.7 KB, Zephyr hello 12.5 KB·셸 +30 KB 같은 실측 감각이 있다
- [ ] float printf가 왜 13 KB를 끌고 오는지(`_dtoa_r`, 소프트 double) 설명할 수 있다
- [ ] `FreeRTOSConfig.h` 항목 하나가 TCB 크기를 바꾸고 그것이 태스크 수만큼 곱해진다는 것을 숫자로 보여 줄 수 있다
- [ ] Zephyr에서 Kconfig 조각(`EXTRA_CONF_FILE`)으로 기능을 끄고 `.config`·`autoconf.h`로 결과를 확인할 수 있다
- [ ] 커널 객체 핸들이 구조체 주소라는 것과, 동적 생성 태스크의 힙 배치(스택 먼저, TCB 나중)를 그릴 수 있다
- [ ] `main()` → `xTaskCreate` → `vTaskStartScheduler` → SVC → 첫 태스크까지 함수 이름으로 따라갈 수 있다

---

## 참고 자료

- FreeRTOS-Kernel 소스 V11.3.2 (랩 `/var/rtoslab/src/FreeRTOS-Kernel`): `tasks.c`, `queue.c`, `list.c`, `timers.c`, `event_groups.c`, `stream_buffer.c`, `include/FreeRTOS.h`(설정 기본값), `portable/GCC/ARM_CM3/port.c`, `portable/MemMang/heap_1.c`·`heap_4.c`, `History.txt`(V11.3.2 릴리스 2026-10-16) — [github.com/FreeRTOS/FreeRTOS-Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel)
- FreeRTOS 공식 문서 (설정 항목·정적 할당·힙 구현) — [freertos.org](https://www.freertos.org/)
- Zephyr v4.4.2 소스 (랩 `/var/rtoslab/zephyrproject/zephyr`): `kernel/CMakeLists.txt`, `kernel/init.c`(`z_cstart`, `bg_thread_main`), `kernel/sched.c`, `kernel/thread.c`, `kernel/timeout.c`, `kernel/include/kswap.h`, `include/zephyr/kernel/thread.h`(`struct k_thread`), `arch/arm/core/cortex_m/`, `drivers/timer/cortex_m_systick.c`, `kernel/Kconfig`(`SYS_CLOCK_TICKS_PER_SEC`), `subsys/logging/Kconfig.mode`
- Zephyr 문서: Kconfig — [docs.zephyrproject.org/latest/build/kconfig/index.html](https://docs.zephyrproject.org/latest/build/kconfig/index.html), Devicetree — [docs.zephyrproject.org/latest/build/dts/index.html](https://docs.zephyrproject.org/latest/build/dts/index.html), 크기 최적화와 `rom_report`/`ram_report` — 소스 트리 `doc/develop/optimizations/footprint.rst`, `tools.rst`
- Hubris (Oxide Computer) — [hubris.oxide.computer](https://hubris.oxide.computer/), [github.com/oxidecomputer/hubris](https://github.com/oxidecomputer/hubris)
- seL4 — [sel4.systems](https://sel4.systems/)
- Arm, Armv7-M Architecture Reference Manual — SysTick, SCB ICSR(PENDSVSET), 예외 진입·복귀
- Embedded Linux 노트: EL E1(커널 구조), EL E2(Kconfig), EL B1–B4(툴체인·ELF)
