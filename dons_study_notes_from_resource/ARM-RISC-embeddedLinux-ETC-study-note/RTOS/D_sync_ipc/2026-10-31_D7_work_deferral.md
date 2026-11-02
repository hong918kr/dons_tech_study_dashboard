# D7. 작업 위임 — ISR은 짧게 깨우기만, 일은 스레드로: 전용 태스크 · 타이머 daemon · Zephyr workqueue · 게이트키퍼

> **이 노트를 다 읽으면**: "ISR 안에서 하면 안 되는 일"을 측정 숫자(같은 우선순위 IMU IRQ가 298 µs 늦어짐)로 설명할 수 있다 · ISR → 스레드 위임 네 가지 패턴(전용 태스크, `xTimerPendFunctionCallFromISR`, Zephyr `k_work`, 게이트키퍼)을 코드로 쓰고 지연·RAM·사건 유실 기준으로 고를 수 있다 · `k_work_submit`이 0/1/2를 돌려주는 이유와 `k_work_schedule` vs `k_work_reschedule`의 차이를 v4.4.2 `kernel/work.c`로 설명할 수 있다 · "위임된 일의 지연은 패턴이 아니라 **실행 주체의 우선순위**가 정한다"를 차트로 보여 줄 수 있다
> **왜 필요한가**: 펌웨어 면접의 단골 "ISR에서 I2C 읽어도 되나? 안 되면 어떻게 하나?", "Zephyr workqueue에서 sleep하면?", "로그를 여러 태스크가 찍으면?"에 구조로 답해야 한다. 실무에서는 드론 FC의 IMU → estimator, 바로미터 I2C, 블랙박스 로깅, 모터 fault 보고가 전부 이 패턴 위에 서 있다
> **Don 기준 난이도**: SSD 펌웨어에서 "완료 ISR은 큐에 넣기만 하고 executive 루프가 처리"하는 구조는 이미 안다 / 새로 배울 부분은 RTOS가 주는 위임 수단 네 가지의 실제 비용(8~20 µs), **같은 우선순위면 1 tick을 기다린다**는 규칙, `portYIELD_FROM_ISR`를 빼먹었을 때의 증상, Zephyr system workqueue가 협력형(-1)이라는 사실과 그 대가
> **선행 노트**: A2(실행 모델 — M2 인터럽트+메인 루프, M4 ISR → task notify), C2(context switch), C5(tick · `+1 tick` 규칙), C7(크리티컬 섹션), D1(세마포어), D5(task notification · 병행 작성), D6(소프트웨어 타이머 · timer daemon 기본 · 병행 작성), EL F6(Linux top/bottom half · threaded IRQ), EL F7(softirq · tasklet · workqueue). 이어지는 노트: E1(FromISR API 규칙), E2(인터럽트 우선순위 배치), E4(ISR → 태스크 처리 경로 — ISR 설계 원칙 · 배치 처리 · 인터럽트 폭주), J(드라이버 패턴)

---

## 0. 큰 그림 — 왜 ISR 밖으로 일을 옮기나

Don이 SSD 컨트롤러 펌웨어에서 매일 보던 구조를 떠올려 보자. NAND 채널 DMA가 끝나면 완료 인터럽트가 온다. 그 ISR은 **완료 레지스터를 읽어 상태를 지우고, 완료 기록 하나를 링버퍼에 넣고, 끝난다.** ECC 결과 확인, 다음 명령 발행, 호스트 완료 큐(CQ) 기록 같은 "진짜 일"은 executive 루프가 링버퍼를 비우면서 한다. 이유는 경험으로 안다 — ISR이 길어지면 다른 채널의 완료가 밀리고, 타이머가 밀리고, 결국 한 채널이 막히면 전체 처리량이 무너진다.

RTOS에서도 원리는 똑같다. 다만 "나중에 처리하는 쪽"이 executive 루프 하나가 아니라 **우선순위를 가진 태스크(스레드)**가 된다는 점이 다르다. 이것을 **작업 위임(work deferral)** 또는 **deferred interrupt processing**이라고 부른다 — ISR은 하드웨어가 꼭 지금 필요로 하는 최소한만 하고, 나머지는 스레드 문맥으로 넘기는 설계다.

위임하는 이유는 세 가지다.

1. **다른 인터럽트의 지연을 짧고 예측 가능하게**: Cortex-M에서 ISR이 도는 동안 같은 우선순위나 더 낮은 우선순위의 IRQ는 기다린다(B3 · E2). SysTick도 보통 가장 낮은 우선순위라 ISR이 길면 tick이 밀린다. ISR 길이가 곧 다른 IRQ의 최악 지연에 더해진다.
2. **ISR에서 할 수 없는 일을 하려고**: I2C 트랜잭션 완료 기다리기, `printf`, 플래시 erase(수 ms), 뮤텍스 잡기, 메모리 할당 — 전부 **블록할 수 있는 일**이다. ISR은 블록할 수 없다(블록하면 "누구"를 재우나? ISR은 태스크가 아니다). FreeRTOS는 `FromISR` 이름이 붙은 API만, Zephyr는 문서에 `isr-ok`로 표시된 API만 ISR에서 허용한다(E1).
3. **일에 "스케줄러 우선순위"를 주려고**: NVIC 우선순위는 하드웨어 순서일 뿐이다. 바로미터 보정 계산이 자세 제어 루프보다 덜 중요하다는 정책은 **태스크 우선순위**로만 표현할 수 있다. 위임하면 그 일이 다른 태스크와 같은 규칙으로 경쟁한다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">같은 일, 두 가지 배치 — IMU IRQ가 언제 실행되나 (예제 1의 구조)</text>
<text x="60" y="45" font-size="12">A. 느린 일을 ISR 안에서 (예제 1: IMU 최대 298 µs 지연)</text>
<rect x="60" y="55" width="100" height="32" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="110" y="76" font-size="12" text-anchor="middle">app</text>
<rect x="160" y="55" width="300" height="32" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="310" y="76" font-size="12" text-anchor="middle">baro ISR: I2C 읽기 + 보정 300 µs</text>
<rect x="460" y="55" width="50" height="32" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="485" y="76" font-size="12" text-anchor="middle">IMU</text>
<rect x="510" y="55" width="150" height="32" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="585" y="76" font-size="12" text-anchor="middle">app</text>
<line x1="230" y1="50" x2="230" y2="98" stroke="#4a7bd0" stroke-dasharray="3,3"/>
<line x1="232" y1="104" x2="458" y2="104" stroke="currentColor" marker-start="url(#ah1)" marker-end="url(#ah1)"/>
<text x="345" y="121" font-size="12" text-anchor="middle">IMU IRQ 도착 → 같은 우선순위 ISR이 끝날 때까지 대기</text>
<text x="60" y="150" font-size="12">B. ISR은 깨우기만, 일은 태스크로 (예제 1: IMU 최대 2 µs)</text>
<rect x="60" y="160" width="100" height="32" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="110" y="181" font-size="12" text-anchor="middle">app</text>
<rect x="160" y="160" width="45" height="32" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="182" y="181" font-size="12" text-anchor="middle">ISR</text>
<rect x="205" y="160" width="25" height="32" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<rect x="230" y="160" width="50" height="32" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="255" y="181" font-size="12" text-anchor="middle">IMU</text>
<rect x="280" y="160" width="275" height="32" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="417" y="181" font-size="12" text-anchor="middle">baro 태스크 (나머지 처리)</text>
<rect x="555" y="160" width="105" height="32" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="607" y="181" font-size="12" text-anchor="middle">app</text>
<line x1="230" y1="155" x2="230" y2="200" stroke="#4a7bd0" stroke-dasharray="3,3"/>
<text x="360" y="218" font-size="12" text-anchor="middle">IMU는 도착 즉시 실행 — baro 일은 NVIC가 아니라 스케줄러 우선순위로 경쟁한다</text>
<rect x="60" y="238" width="12" height="10" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="78" y="247" font-size="12">앱 태스크</text>
<rect x="160" y="238" width="12" height="10" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="178" y="247" font-size="12">baro ISR</text>
<rect x="260" y="238" width="12" height="10" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="278" y="247" font-size="12">IMU ISR</text>
<rect x="360" y="238" width="12" height="10" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="378" y="247" font-size="12">baro 태스크 (위임된 일)</text>
<line x1="60" y1="272" x2="660" y2="272" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="600" y="290" font-size="12" text-anchor="middle">시간</text>
</svg>
```

그림 1 — 같은 300 µs짜리 일을 ISR 안에서 하면(A) 같은 NVIC 우선순위의 IMU IRQ가 그만큼 기다린다. ISR이 태스크를 깨우기만 하면(B) IMU ISR은 태스크를 바로 선점하고, baro 일은 태스크 우선순위 규칙을 따른다.

### 0.1 Linux의 top half / bottom half와 무엇이 같고 다른가

Linux 쪽은 EL F6(top/bottom half, threaded IRQ)과 EL F7(softirq · tasklet · workqueue)에서 이미 다뤘다. 대응만 짚는다. **top half(hardirq) = RTOS ISR**이고, **threaded IRQ = 이 노트의 패턴 1(전용 태스크)**, **Linux workqueue = Zephyr `k_work`(패턴 3)**다. softirq/tasklet에 정확히 대응하는 것은 RTOS에 없다 — 가장 가까운 것은 "높은 우선순위의 공용 스레드 하나가 함수 포인터를 받아 실행"하는 FreeRTOS timer daemon(패턴 2)과 Zephyr의 협력형 system workqueue다. 결정적 차이는 둘이다. 첫째, Linux는 genirq가 스레드·깨우기·`IRQF_ONESHOT` mask/unmask까지 만들어 주지만 RTOS에서는 **그 전부를 개발자가 직접 조립**한다(EL F6 §5.1의 "RTOS 비유"가 바로 이 노트의 패턴 1이다). 둘째, 숫자 규모가 다르다. EL F6에서 VM 위 Linux의 hard → thread 깨우기는 p50 116 µs, p99 2.98 ms였다. 이 노트의 QEMU Cortex-M3에서 같은 경로(ISR → 태스크 시작)는 **8.1 µs**이고 최대도 수십 µs 안에 묶인다(§2). 대신 RTOS는 우선순위를 잘못 주면 그 보장이 그대로 사라진다(§2.3, §6).

### 0.2 이 노트의 지도 — 위임 패턴 네 가지

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" font-size="13" font-weight="bold">ISR(또는 요청하는 태스크) → 일을 실행하는 곳: 네 가지 배치</text>
<text x="85" y="44" font-size="12" text-anchor="middle">패턴</text>
<text x="275" y="44" font-size="12" text-anchor="middle">ISR / 요청 쪽</text>
<text x="470" y="44" font-size="12" text-anchor="middle">실행하는 곳</text>
<text x="607" y="44" font-size="12" text-anchor="middle">특징</text>
<rect x="10" y="55" width="150" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="85" y="85" font-size="13" text-anchor="middle">① 전용 태스크</text>
<rect x="175" y="55" width="200" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="275" y="85" font-size="12" text-anchor="middle">notify / 세마포어 give</text>
<rect x="405" y="55" width="130" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="470" y="85" font-size="12" text-anchor="middle">핸들러 태스크</text>
<text x="607" y="85" font-size="12" text-anchor="middle">이벤트당 1태스크</text>
<line x1="375" y1="80" x2="403" y2="80" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="10" y="120" width="150" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="85" y="150" font-size="13" text-anchor="middle">② daemon 위임</text>
<rect x="175" y="120" width="200" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="275" y="141" font-size="12" text-anchor="middle">PendFunctionCallFromISR</text>
<text x="275" y="159" font-size="12" text-anchor="middle">→ timer 명령 큐</text>
<rect x="405" y="120" width="130" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="470" y="150" font-size="12" text-anchor="middle">timer daemon</text>
<text x="607" y="150" font-size="12" text-anchor="middle">공유 1태스크</text>
<line x1="375" y1="145" x2="403" y2="145" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="10" y="185" width="150" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="85" y="215" font-size="13" text-anchor="middle">③ workqueue</text>
<rect x="175" y="185" width="200" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="275" y="206" font-size="12" text-anchor="middle">k_work_submit (ISR 가능)</text>
<text x="275" y="224" font-size="12" text-anchor="middle">→ 큐의 pending 리스트</text>
<rect x="405" y="185" width="130" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="470" y="215" font-size="12" text-anchor="middle">workqueue 스레드</text>
<text x="607" y="215" font-size="12" text-anchor="middle">16 B, 합쳐짐</text>
<line x1="375" y1="210" x2="403" y2="210" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="10" y="250" width="150" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="85" y="280" font-size="13" text-anchor="middle">④ 게이트키퍼</text>
<rect x="175" y="250" width="200" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="275" y="280" font-size="12" text-anchor="middle">태스크들 → 요청 큐(값 복사)</text>
<rect x="405" y="250" width="130" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="470" y="280" font-size="12" text-anchor="middle">게이트키퍼 태스크</text>
<text x="607" y="280" font-size="12" text-anchor="middle">자원 단독 소유</text>
<line x1="375" y1="275" x2="403" y2="275" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="340" y="322" font-size="12" text-anchor="middle">①②③은 ISR → 스레드 위임, ④는 태스크 → 자원 소유자 위임 (같은 생각: "일을 한곳으로 모은다")</text>
</svg>
```

그림 2 — 이 노트가 다루는 네 패턴. ①은 FreeRTOS·Zephyr 공통, ②는 FreeRTOS 고유(`xTimerPendFunctionCallFromISR`), ③은 Zephyr 고유(`k_work`), ④는 RTOS와 무관한 설계 패턴이다.

| 절 | 패턴 | 예제 | 핵심 숫자 (QEMU icount) |
|---|---|---|---|
| §1 | 왜 미루나 | 예제 1 | ISR 안 300 µs 일 → 같은 우선순위 IMU 최대 298 µs 지연, 위임하면 2 µs |
| §2 | ① 전용 태스크 | 예제 2 | ISR → 태스크 8.1 µs, 같은 우선순위면 평균 757 µs, `portYIELD_FROM_ISR` 빠지면 같은 증상 |
| §3 | ② timer daemon | 예제 3 · 4 · 5 | ISR → 함수 13.6 µs, 느린 타이머 콜백 하나가 최대 1943 µs로, 큐 10칸에 14개 → 4개 실패 |
| §4 | ③ Zephyr workqueue | 예제 6 · 7 · 8 · 9 | sysworkq 13.2 µs, 같은 큐 항목이 20 ms sleep → 다른 항목 20.2 ms 지연 + 12건 합쳐짐 |
| §5 | ④ 게이트키퍼 | 예제 10 | 뮤텍스 log() 평균 1836 µs → 게이트키퍼 11 µs |
| §6 | 비교 | 예제 11 | 차트 두 장: 패턴 차이는 µs, 우선순위 차이는 ms |
| §8 | 실무 | 예제 12 | 모터 fault: PWM 차단 240 ns(ISR), 보고는 daemon — daemon 우선순위가 FOC 루프를 3 ms 막음 |

### 실습 환경 한 줄

Lima VM `elinux`의 RTOS 랩: QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) 위의 **FreeRTOS V11.3.2**(템플릿 `FreeRTOSConfig.h`: 1 kHz tick, 우선순위 8단계, `configTIMER_TASK_PRIORITY` = 7, `configTIMER_QUEUE_LENGTH` = 10)와 **Zephyr v4.4.2**(같은 보드 `mps2/an385`, `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000`으로 맞춤). 모든 타이밍은 `-icount shift=5`(명령 1개 = 32 ns 가상 시간 — FreeRTOS는 `ICOUNT=1`, Zephyr는 `qemu-system-arm`을 직접 같은 옵션으로 실행)에서 MPS2 FPGAIO 카운터(0x40028018, 25 MHz → 1 cycle = 40 ns)로 쟀다. **QEMU icount 기준이라 실제 칩과 절대값은 다르다**(캐시 · 플래시 wait state · 버스 · 예외 스태킹 사이클을 모델링하지 않는다 — 특히 IRQ → ISR 진입이 실칩의 12 cycle보다 짧게 나온다). 이 노트가 보려는 것은 **패턴과 우선순위 사이의 상대 차이**이고, 그것은 µs 대 ms로 갈릴 만큼 커서 절대값 오차에 묻히지 않는다. FreeRTOS 예제는 `-Wall -Wextra` 경고 0개, Zephyr 앱은 `-DEXTRA_CFLAGS="-Wall -Wextra"`로 빌드해 앱 소스 경고 0개다. (작성 시점에 VM 루트 파일시스템이 읽기 전용으로 바뀌어 있어서 빌드 산출물은 평소의 `/var/tmp/rtos/D7/` 대신 tmpfs인 `/tmp/rtos_D7/`에 두었다. Zephyr 빌드는 CMake 캐시를 쓰려고 `HOME`도 그 아래로 옮겼다.)

---

## 1. 왜 미루나 — 측정으로 보기

### 1.1 직관

ISR은 "모든 것을 멈추고 지금 당장" 실행된다. 정확히는 **자기보다 우선순위가 같거나 낮은 모든 것**을 멈춘다. 그래서 ISR의 길이는 그 ISR만의 문제가 아니라 시스템 전체의 문제다. 예를 들어 드론 비행 제어기에서 바로미터 변환 완료 IRQ와 IMU data-ready IRQ가 같은 NVIC 우선순위 그룹에 있다고 하자(실칩은 우선순위 비트가 3~4개뿐이라 그룹이 겹치는 일이 흔하다). 바로미터 ISR이 I2C로 6바이트를 읽고 보정 공식을 계산하느라 300 µs를 쓰면, 그사이 도착한 IMU IRQ는 300 µs를 기다린다. IMU 샘플 타임스탬프가 300 µs 틀어지면 1 kHz 자세 추정에서 위상 오차가 된다.

### 1.2 정의 — "ISR 안에 남길 것"과 "밖으로 보낼 것"

ISR 안에 남겨야 하는 것은 **하드웨어가 지금 필요로 하는 일**뿐이다.

- 인터럽트 원인 지우기(status clear) — 안 지우면 ISR을 빠져나가자마자 다시 들어온다(level-triggered).
- 시간에 민감한 데이터 건지기 — 하드웨어 FIFO가 넘치기 전에 비우기, 사건 타임스탬프 찍기.
- 안전 동작 — 과전류에서 PWM 출력 끄기처럼 µs 단위로 늦으면 하드웨어가 타는 일(§8.2).
- 누군가 깨우기 — 태스크에 알림, 큐에 넣기, work 제출.

밖으로 보내는 것은 그 나머지 전부다. 특히 **블록할 수 있는 일**(I2C/SPI 트랜잭션 완료 대기, 뮤텍스, `printf`, 플래시 쓰기, 동적 할당)과 **길이가 들쭉날쭉한 일**(파싱, 보정 계산, 프로토콜 상태 머신)이다.

### 1.3 예제 1 — 느린 일을 ISR 안에서 vs 태스크로

무엇을 확인하는 코드인지: IMU(DUALTIMER, IRQ10, 997 µs 주기)와 baro(TIMER0, IRQ8, 3 ms 주기)를 **같은 NVIC 우선순위 0x40**에 두고, baro의 300 µs 일을 ISR 안에서 할 때와 태스크로 넘길 때 IMU ISR의 진입 지연을 각각 600 ms 동안 잰다. IMU ISR은 자기 지연을 하드웨어로 잰다 — DUALTIMER는 0까지 세고 IRQ를 낸 뒤 LOAD 값에서 다시 내려가므로, ISR 첫 줄에서 `LOAD - VALUE`가 곧 "타이머 만료 → ISR 진입" 시간이다.

모든 FreeRTOS 예제가 함께 쓰는 헤더 `d7.h`:

```c
/* D7 공용: FPGAIO 25 MHz 카운터, CMSDK TIMER0, DUALTIMER, busy-wait */
#ifndef D7_H
#define D7_H
#include <stdint.h>
#include "board.h"
#define CYC()         REG32(0x40028018u)          /* 25 MHz, 1 cyc = 40 ns */
#define CYC2US(c)     ((c) / 25u)
#define DT_BASE       0x40002000u                 /* DUALTIMER timer1 (IRQ10) */
#define DT_LOAD       REG32(DT_BASE + 0x00)
#define DT_VALUE      REG32(DT_BASE + 0x04)
#define DT_CTRL       REG32(DT_BASE + 0x08)       /* 0xE2 = EN, periodic, IE, 32-bit */
#define DT_INTCLR     REG32(DT_BASE + 0x0C)
#define NVIC_ISPR0    REG32(0xE000E200u)
static inline void busy_us(uint32_t us)           /* CPU를 실제로 태우는 대기 */
{
    uint32_t t0 = CYC();
    while ((uint32_t)(CYC() - t0) < us * 25u) { }
}
static inline void timer0_start(uint32_t period_us, uint8_t prio)
{
    TIMER_RELOAD(TIMER0_BASE) = period_us * 25u - 1u;
    TIMER_VALUE(TIMER0_BASE)  = period_us * 25u - 1u;
    NVIC_IPR(8) = prio;
    NVIC_ISER0  = 1u << 8;
    TIMER_CTRL(TIMER0_BASE) = 0x9;                 /* EN + IRQ EN */
}
static inline void timer0_stop(void) { TIMER_CTRL(TIMER0_BASE) = 0; }
#endif
```

`ex1.c`:

```c
/* ex1: 느린 일을 ISR 안에서 하면 같은 우선순위 IRQ(IMU)가 얼마나 늦어지나 */
#include "FreeRTOS.h"
#include "task.h"
#include "d7.h"

#define IMU_PERIOD_US  997u          /* IMU data-ready ~1 kHz */
#define BARO_PERIOD_US 3000u         /* baro 변환 완료 */
#define BARO_WORK_US   300u          /* I2C 읽기 + 보정 계산 흉내 */
static volatile int defer_mode;      /* 0: ISR 안에서 처리, 1: 태스크로 위임 */
static TaskHandle_t baro_task;
static volatile uint32_t imu_n, imu_sum, imu_max, hist[4];  /* <5, <50, <200, >=200 us */

void DUALTIMER_IRQHandler(void)      /* IMU ISR: 자기 지연만 잰다 */
{
    uint32_t lat = (DT_LOAD - DT_VALUE) / 25u;   /* 타이머 만료 → ISR 진입 (us) */
    DT_INTCLR = 1;
    imu_n++; imu_sum += lat; if (lat > imu_max) imu_max = lat;
    hist[lat < 5 ? 0 : lat < 50 ? 1 : lat < 200 ? 2 : 3]++;
}
void TIMER0_IRQHandler(void)         /* baro ISR */
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    if (!defer_mode) { busy_us(BARO_WORK_US); return; }   /* 나쁜 예 */
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(baro_task, &woken);            /* 좋은 예 */
    portYIELD_FROM_ISR(woken);
}
static void baro_fn(void *p)
{
    (void)p;
    for (;;) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); busy_us(BARO_WORK_US); }
}
static void run(int mode, const char *name)
{
    defer_mode = mode;
    imu_n = imu_sum = imu_max = 0; for (int i = 0; i < 4; i++) hist[i] = 0;
    vTaskDelay(pdMS_TO_TICKS(600));
    board_printf_raw("%s: IMU n=%u avg=%u us max=%u us  hist[<5,<50,<200,>=200]=%u,%u,%u,%u\n",
        name, imu_n, imu_sum / imu_n, imu_max, hist[0], hist[1], hist[2], hist[3]);
}
static void main_fn(void *p)
{
    (void)p;
    DT_LOAD = IMU_PERIOD_US * 25u; NVIC_IPR(10) = 0x40; NVIC_ISER0 = 1u << 10;
    DT_CTRL = 0xE2;
    timer0_start(BARO_PERIOD_US, 0x40);              /* IMU와 같은 NVIC 우선순위 */
    run(0, "work in ISR ");
    run(1, "deferred    ");
    board_exit(0);
}
int main(void)
{
    xTaskCreate(baro_fn, "baro", 256, NULL, 3, &baro_task);
    xTaskCreate(main_fn, "main", 256, NULL, 4, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D7/f1/ex1.c OUT=/tmp/rtos_D7/ex1 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D7/ex1/app.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
  12876	    100	 135440	 148416	  243c0	/tmp/rtos_D7/ex1/app.elf
work in ISR : IMU n=601 avg=9 us max=298 us  hist[<5,<50,<200,>=200]=559,11,19,12
deferred    : IMU n=601 avg=0 us max=2 us  hist[<5,<50,<200,>=200]=601,0,0,0
[board_exit] code=0
```

출력에서 볼 것:

- **ISR 안에서 처리하면 IMU 최대 지연이 298 µs** — baro ISR 길이(300 µs) 그대로다. 601개 중 42개(11 + 19 + 12)가 5 µs 넘게 밀렸다. 평균(9 µs)만 보면 괜찮아 보인다는 점이 함정이다. 실시간 시스템은 평균이 아니라 최대로 판정한다(A1).
- **위임하면 최대 2 µs**, 601개 전부 5 µs 미만. baro ISR은 `vTaskNotifyGiveFromISR` 한 번(수백 ns)만 하고 나가므로 IMU가 기다릴 틈이 거의 없다. baro 태스크가 300 µs를 쓰는 동안 IMU IRQ가 오면 IMU ISR은 태스크를 그냥 선점한다 — 태스크는 NVIC 입장에서 "Thread mode 코드"일 뿐이다.
- 평균 0 µs는 QEMU가 예외 스태킹 사이클을 모델링하지 않아서다. 실칩에서는 최소 12 cycle(25 MHz에서 0.48 µs) + 플래시 wait state가 더해진다.

### 1.4 위임의 대가

공짜는 아니다. 위임하면 baro 일 자체는 **더 늦게 끝난다** — ISR → 태스크 전환 비용(§2에서 8.1 µs)에, 더 높은 우선순위 태스크가 있으면 그만큼 더 기다린다. 그리고 태스크 하나는 스택 RAM(이 예제는 256 word = 1 KB)과 TCB를 먹는다. 그래서 질문은 "위임할까 말까"가 아니라 **"누구의 지연을 줄이고 누구의 지연을 늘릴까"**다. 대개 답은 분명하다 — 하드웨어 마감이 짧은 쪽(IMU, 모터 전류)을 지키고, 마감이 긴 쪽(바로미터 50 Hz, 로깅)을 늦춘다.

흔한 함정: "ISR을 가장 높은 우선순위로 올리면 되지 않나?" 그러면 baro ISR이 IMU를 선점하지 않게 될 뿐, baro ISR보다 낮은 모든 것(SysTick, UART, 모든 태스크)은 여전히 300 µs씩 멈춘다. 그리고 FreeRTOS에서 `configMAX_SYSCALL_INTERRUPT_PRIORITY`보다 높은(숫자가 작은) ISR은 FreeRTOS API를 아예 못 부른다(B3 · E2) — 위임 수단 자체를 잃는다.

---

## 2. 패턴 1 — 전용 deferred-interrupt 태스크

### 2.1 직관과 정의

가장 단순하고 가장 많이 쓰는 패턴이다. 인터럽트 원인 하나마다 **전용 태스크** 하나를 둔다. 태스크는 평소 "알림이 올 때까지" 블록해 있고, ISR은 원인을 지운 뒤 그 태스크를 깨운다. bare-metal의 "ISR이 플래그 세우고 메인 루프가 폴링"과 같은 구조인데, 폴링 대신 **스케줄러가 깨워 준다**는 점과 **태스크 우선순위로 실행 순서를 정한다**는 점이 다르다. Linux의 threaded IRQ가 커널이 대신 만들어 주는 바로 이것이다(EL F6 §5.1).

깨우는 수단은 세 가지 중 고른다.

| 수단 | ISR 쪽 | 태스크 쪽 | 사건이 여러 번 오면 | 언제 |
|---|---|---|---|---|
| direct-to-task notification (D5) | `vTaskNotifyGiveFromISR` | `ulTaskNotifyTake(pdTRUE, ...)` | 값이 증가 → 반환값으로 개수를 안다 | 기본 선택. 가장 빠르고 RAM 0 |
| binary semaphore (D1) | `xSemaphoreGiveFromISR` | `xSemaphoreTake` | 이미 1이면 버려짐(합쳐짐) | 여러 태스크가 기다리거나, 옛 코드 |
| queue (D4) | `xQueueSendFromISR` | `xQueueReceive` | 큐 길이만큼 보존, 넘치면 실패 | 사건마다 데이터(타임스탬프, 상태값)가 다를 때 |

Zephyr라면 `k_sem_give`(ISR 가능) + `k_sem_take`, 또는 `k_event`, `k_msgq`(D10)를 같은 자리에 쓴다.

### 2.2 예제 2 — 핸들러 우선순위를 바쁜 앱보다 높게 · 같게 · 낮게

무엇을 확인하는 코드인지: TIMER0 IRQ(7 ms 주기)가 핸들러 태스크를 깨우고, 핸들러는 깨어난 첫 줄에서 "ISR 진입 → 핸들러 시작" 시간을 잰다. 방해꾼은 우선순위 2의 앱 태스크(3 ms 계산 + 1 tick 쉼)다. 핸들러 우선순위를 3 / 2 / 1로 바꿔 가며 50번씩 재고, 마지막에 notification 대신 binary semaphore로도 잰다. 같은 소스를 `-DNO_YIELD`로 빌드하면 `portYIELD_FROM_ISR`를 빼먹은 버전이 된다.

```c
/* ex2: 패턴 1 — 전용 deferred-interrupt 태스크. 핸들러 우선순위가 바쁜 앱 태스크보다 높/같/낮을 때 */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "d7.h"

static TaskHandle_t h_task;
static SemaphoreHandle_t sem;
static volatile int use_sem;
static volatile uint32_t t_isr, n, sum, mn, mx;

void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    t_isr = CYC();                                  /* ISR 진입 시각 */
    BaseType_t woken = pdFALSE;
    if (use_sem) xSemaphoreGiveFromISR(sem, &woken);
    else         vTaskNotifyGiveFromISR(h_task, &woken);
#ifndef NO_YIELD
    portYIELD_FROM_ISR(woken);                      /* -DNO_YIELD: 이 줄을 빼먹으면? */
#endif
}
static void handler_fn(void *p)                     /* "bottom half" 태스크 */
{
    (void)p;
    for (;;) {
        if (use_sem) xSemaphoreTake(sem, portMAX_DELAY);
        else         ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t d = CYC() - t_isr;                 /* ISR 진입 → 핸들러 시작 */
        n++; sum += d; if (d < mn) mn = d; if (d > mx) mx = d;
        busy_us(50);                                /* 실제 처리 흉내 */
    }
}
static void app_fn(void *p)                         /* 바쁜 앱: 3 ms 계산, 1 tick 쉼 */
{
    (void)p;
    for (;;) { busy_us(3000); vTaskDelay(1); }
}
#define T10(c) ((c) * 10u / 25u)                    /* cycles → 0.1 us */
static void phase(const char *name, int sem_mode, UBaseType_t prio)
{
    use_sem = sem_mode; vTaskPrioritySet(h_task, prio);
    xTaskNotifyGive(h_task); vTaskDelay(2);         /* 대기 중인 핸들러를 새 모드로 한 바퀴 돌림 */
    n = sum = mx = 0; mn = 0xFFFFFFFFu;
    timer0_start(7000, 0x40);
    vTaskDelay(pdMS_TO_TICKS(350));
    timer0_stop();
    uint32_t a = sum / n;
    board_printf_raw("%s prio=%u n=%u  min=%u.%u avg=%u.%u max=%u.%u us\n", name, (unsigned)prio, n,
        T10(mn) / 10, T10(mn) % 10, T10(a) / 10, T10(a) % 10, T10(mx) / 10, T10(mx) % 10);
}
static void main_fn(void *p)
{
    (void)p;
    phase("notify  handler>app", 0, 3);
    phase("notify  handler=app", 0, 2);
    phase("notify  handler<app", 0, 1);
    phase("binsem  handler>app", 1, 3);
    board_exit(0);
}
int main(void)
{
    sem = xSemaphoreCreateBinary();
    xTaskCreate(handler_fn, "irq_h", 256, NULL, 3, &h_task);
    xTaskCreate(app_fn, "app", 256, NULL, 2, NULL);
    xTaskCreate(main_fn, "main", 256, NULL, 5, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D7/f2/ex2.c OUT=/tmp/rtos_D7/ex2 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D7/ex2/app.elf'
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D7/f2/ex2.c OUT=/tmp/rtos_D7/ex2ny EXTRA_CFLAGS=-DNO_YIELD && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D7/ex2ny/app.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
  14832	    100	 135432	 150364	  24b5c	/tmp/rtos_D7/ex2/app.elf
notify  handler>app prio=3 n=49  min=8.1 avg=9.8 max=15.1 us
notify  handler=app prio=2 n=49  min=13.9 avg=757.2 max=998.3 us
notify  handler<app prio=1 n=50  min=13.9 avg=1551.1 max=3060.7 us
binsem  handler>app prio=3 n=49  min=9.0 avg=10.8 max=16.1 us
[board_exit] code=0
   text	   data	    bss	    dec	    hex	filename
  14808	    100	 135432	 150340	  24b44	/tmp/rtos_D7/ex2ny/app.elf
notify  handler>app prio=3 n=49  min=14.0 avg=757.2 max=998.3 us
notify  handler=app prio=2 n=49  min=13.8 avg=757.2 max=998.3 us
notify  handler<app prio=1 n=50  min=13.8 avg=1551.1 max=3060.6 us
binsem  handler>app prio=3 n=49  min=15.0 avg=757.5 max=998.3 us
[board_exit] code=0
```

출력에서 볼 것:

- **핸들러 > 앱: 최소 8.1 µs, 최대 15.1 µs.** 이것이 "ISR 진입 → 태스크 첫 줄"의 바닥 비용이다. 내용은 `vTaskNotifyGiveFromISR`(태스크를 ready 리스트로) → `portYIELD_FROM_ISR`(PendSV pending) → ISR 복귀 → PendSV에서 context switch(C2) → `ulTaskNotifyTake` 안의 나머지 코드 실행 → 복귀다. 최대가 15 µs로 튀는 것은 TIMER0 IRQ가 SysTick 처리나 커널 크리티컬 섹션(C7)과 겹친 경우다.
- **핸들러 = 앱: 평균 757 µs, 최대 998 µs** — 거의 1 tick(1 ms)이다. 같은 우선순위끼리는 선점하지 않는다. 핸들러는 ready가 되지만 앱의 time slice가 끝나는 다음 tick까지 기다린다(C4 round-robin).
- **핸들러 < 앱: 최대 3061 µs** — 앱이 3 ms 계산을 끝내고 `vTaskDelay`로 블록할 때까지 기다린다. 위임된 일의 지연은 이제 **앱의 계산 길이**가 결정한다.
- **binary semaphore는 notification보다 약 1 µs 느리다**(최소 9.0 vs 8.1). 세마포어는 길이 1짜리 큐라서 큐 구조체와 대기 리스트를 거친다. 예제 5의 코드 크기로 보면 give 쪽은 오히려 notify가 크지만(`vTaskGenericNotifyGiveFromISR` 296 B vs `xQueueGiveFromISR` 188 B), take 쪽은 `xQueueSemaphoreTake` 396 B vs `ulTaskGenericNotifyTake` 176 B로 세마포어가 두 배 넘게 길다.
- **`portYIELD_FROM_ISR`를 빼먹으면 핸들러 > 앱이어도 평균 757 µs**로, 같은 우선순위일 때와 똑같아진다. 깨우기는 됐지만 PendSV를 걸지 않았으니, 다음 SysTick이 "yield pending" 표시를 보고 전환해 줄 때까지 앱이 계속 돈다.

### 2.3 결과를 소스로 확인 — "더 높을 때만" 즉시 전환한다

같은 우선순위에서 즉시 전환하지 않는 이유는 `tasks.c`의 `vTaskGenericNotifyGiveFromISR`에 그대로 있다(단일 코어 경로).

```c
                #if ( configNUMBER_OF_CORES == 1 )
                {
                    if( pxTCB->uxPriority > pxCurrentTCB->uxPriority )
                    {
                        /* The notified task has a priority above the currently
                         * executing task so a yield is required. */
                        if( pxHigherPriorityTaskWoken != NULL )
                        {
                            *pxHigherPriorityTaskWoken = pdTRUE;
                        }

                        /* Mark that a yield is pending in case the user is not
                         * using the "xHigherPriorityTaskWoken" parameter in an ISR
                         * safe FreeRTOS function. */
                        xYieldPendings[ 0 ] = pdTRUE;
                    }
```

비교가 `>`(엄격히 큼)다. 같으면 `xHigherPriorityTaskWoken`이 `pdFALSE`로 남고 `portYIELD_FROM_ISR(pdFALSE)`는 아무것도 안 한다. 그리고 `-DNO_YIELD` 결과를 설명하는 줄이 `xTaskIncrementTick` 안에 있다.

```c
                if( xYieldPendings[ 0 ] != pdFALSE )
                {
                    xSwitchRequired = pdTRUE;
                }
```

ISR이 `portYIELD_FROM_ISR`를 안 불러도 `xYieldPendings[0]`이 세워져 있으므로, **다음 tick 인터럽트**가 전환을 대신 해 준다. 그래서 증상이 "가끔 1 ms쯤 늦다"로 나타난다 — 동작은 하니까 발견이 늦다. Zephyr는 이 실수가 구조적으로 불가능하다. ISR 안의 `k_sem_give`는 스위치하지 않고 ready 큐만 고쳐 두고, `IRQ_CONNECT`로 등록한 ISR을 감싸는 `_isr_wrapper`가 끝에서 부르는 `z_arm_exc_exit`(`arch/arm/core/cortex_m/exc_exit.c`)가 항상 `_kernel.ready_q.cache != current`를 검사해 PendSV를 걸기 때문이다(§7.2).

### 2.4 사건 유실 — 합쳐짐(coalescing)을 설계에 넣기

ISR이 두 번 깨우는 동안 핸들러가 한 번도 못 돌면 무슨 일이 생기나? binary semaphore는 이미 1이면 두 번째 give가 버려진다. notification은 `ulTaskNotifyTake(pdTRUE, ...)`가 **누적 값을 돌려주고 0으로 지운다** — 반환값이 2라면 사건이 두 번 왔다는 뜻이다. 그래서 장치가 FIFO를 갖고 있다면(IMU FIFO, UART RX FIFO, DMA 완료 기술자 링) 핸들러는 "깨어날 때마다 하나"가 아니라 **"깨어나면 비어 있을 때까지 전부"** 처리하도록 짠다. SSD 펌웨어의 완료 링 처리와 똑같은 습관이다. 사건마다 다른 데이터를 잃으면 안 되는 경우에만 큐를 쓴다(§3.3의 "큐가 차면?" 문제가 따라온다).

### 2.5 level-triggered 인터럽트와 "수동 ONESHOT"

외부 센서의 INT 핀처럼 **원인을 지우려면 I2C로 장치 레지스터를 읽어야 하는** 인터럽트가 있다. ISR에서는 I2C를 기다릴 수 없으니 원인을 못 지우고, 그대로 나가면 level IRQ가 즉시 다시 들어와 CPU가 ISR에 갇힌다(interrupt storm). Linux는 `IRQF_ONESHOT`으로 "스레드 핸들러가 끝날 때까지 라인을 mask"해 준다(EL F6 §4.4). RTOS에서는 직접 한다 — ISR에서 `NVIC_DisableIRQ(n)`(또는 GPIO EXTI mask)로 그 IRQ만 막고 태스크를 깨운 뒤, 태스크가 I2C로 원인을 지운 다음 `NVIC_EnableIRQ(n)`으로 연다. 아래는 개념 코드다(빌드하지 않음, 함수 이름은 CMSIS 기준).

```c
void EXTI3_IRQHandler(void)              /* 개념 코드: baro INT 핀, level-high */
{
    NVIC_DisableIRQ(EXTI3_IRQn);         /* 원인을 못 지우니 라인을 막는다 */
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(baro_task, &woken);
    portYIELD_FROM_ISR(woken);
}
/* baro_task: ulTaskNotifyTake → i2c_read(INT_STATUS) 로 원인 해제 → NVIC_EnableIRQ(EXTI3_IRQn) */
```

### 2.6 함정 정리

- 핸들러 우선순위를 "적당히" 앱과 같게 두면 지연이 1 tick 단위로 늘어난다. 마감이 있는 핸들러는 **그것을 방해할 수 있는 모든 태스크보다 엄격히 높게** 둔다.
- `xHigherPriorityTaskWoken`은 ISR마다 `pdFALSE`로 초기화하고, 여러 FromISR 호출이 같은 변수를 공유하게 한 뒤 마지막에 한 번 `portYIELD_FROM_ISR`.
- 이 패턴의 비용은 태스크 하나(스택 + TCB)다. 인터럽트 원인이 열 개면 태스크 열 개 — 그래서 §3, §4의 "공용 실행자" 패턴이 생겼다.

---

## 3. 패턴 2 — 중앙 집중 위임: `xTimerPendFunctionCallFromISR`

### 3.1 직관과 정의

FreeRTOS에는 소프트웨어 타이머를 처리하는 **timer service task(daemon)**가 이미 하나 있다(D6). daemon은 "타이머 명령 큐"를 읽으며 사는데, FreeRTOS는 이 큐에 **"이 함수를 이 인자로 불러 줘"**라는 명령도 넣을 수 있게 했다. 그것이 `xTimerPendFunctionCallFromISR`다. FreeRTOS 공식 책(Mastering the FreeRTOS Real Time Kernel)은 이것을 **centralized deferred interrupt processing**이라고 부른다 — 원인마다 태스크를 만들지 않고, 공용 daemon 하나가 모든 원인의 뒷일을 처리한다. Linux의 tasklet과 쓰임새가 가장 비슷하다.

`include/timers.h`의 선언(V11.3.2):

```c
typedef void (* PendedFunction_t)( void * arg1,
                                   uint32_t arg2 );

    BaseType_t xTimerPendFunctionCallFromISR( PendedFunction_t xFunctionToPend,
                                              void * pvParameter1,
                                              uint32_t ulParameter2,
                                              BaseType_t * pxHigherPriorityTaskWoken ) PRIVILEGED_FUNCTION;

    BaseType_t xTimerPendFunctionCall( PendedFunction_t xFunctionToPend,
                                       void * pvParameter1,
                                       uint32_t ulParameter2,
                                       TickType_t xTicksToWait ) PRIVILEGED_FUNCTION;
```

- 인자 두 개(`void *` 하나, `uint32_t` 하나)를 값으로 넘긴다. 예를 들어 `pvParameter1` = 장치 핸들, `ulParameter2` = 상태 레지스터 값이나 타임스탬프.
- 쓰려면 `INCLUDE_xTimerPendFunctionCall 1`과 `configUSE_TIMERS 1`이 둘 다 필요하다. `timers.c` 42~43행이 앞의 것만 켜고 뒤의 것을 끄면 `#error configUSE_TIMERS must be set to 1 to make the xTimerPendFunctionCall() function available.`로 빌드를 멈춘다.
- 함수는 **daemon 태스크 문맥**에서, daemon의 우선순위(`configTIMER_TASK_PRIORITY`)로 실행된다. 그래서 블록하면 안 된다 — 블록하는 동안 모든 소프트웨어 타이머와 다른 pended 함수가 멈춘다(D6의 "타이머 콜백 제약"이 그대로 적용).
- 태스크용 `xTimerPendFunctionCall`도 있다. 태스크가 "이 일을 daemon 문맥에서 해 줘"라고 부탁할 때 쓴다(예: 낮은 우선순위 태스크가 높은 우선순위로 짧게 실행돼야 하는 일을 넘길 때).

### 3.2 예제 3 — daemon 우선순위와 "같은 daemon을 쓰는 느린 타이머 콜백"

무엇을 확인하는 코드인지: TIMER0 ISR이 `ulParameter2`에 ISR 시각을 담아 함수를 pend하고, 함수는 실행 첫 줄에서 지연을 잰다. 세 상황을 비교한다 — (1) daemon 기본 우선순위 7, (2) 같은 daemon에 2 ms짜리 auto-reload 타이머 콜백(5 ms 주기)을 함께 걸었을 때, (3) daemon 우선순위를 1로 내려 앱(2)보다 낮게 했을 때.

```c
/* ex3: 패턴 2 — xTimerPendFunctionCallFromISR. daemon 우선순위와 "같은 daemon을 쓰는 느린 타이머 콜백"의 영향 */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "d7.h"

static volatile uint32_t n, sum, mn, mx;
static void deferred(void *p1, uint32_t t_isr)       /* PendedFunction_t: daemon 태스크에서 실행 */
{
    (void)p1;
    uint32_t d = CYC() - t_isr;                       /* ulParameter2로 ISR 시각을 넘겨받았다 */
    n++; sum += d; if (d < mn) mn = d; if (d > mx) mx = d;
}
void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    BaseType_t woken = pdFALSE;
    if (xTimerPendFunctionCallFromISR(deferred, NULL, CYC(), &woken) != pdPASS)
        board_printf_raw("pend failed\n");
    portYIELD_FROM_ISR(woken);
}
static void slow_cb(TimerHandle_t t) { (void)t; busy_us(2000); }   /* 나쁜 타이머 콜백 */
static void app_fn(void *p) { (void)p; for (;;) { busy_us(3000); vTaskDelay(1); } }
#define T10(c) ((c) * 10u / 25u)
static void phase(const char *name)
{
    n = sum = mx = 0; mn = 0xFFFFFFFFu;
    timer0_start(7000, 0x40);
    vTaskDelay(pdMS_TO_TICKS(350));
    timer0_stop(); vTaskDelay(5);
    uint32_t a = sum / n;
    board_printf_raw("%s  n=%u  min=%u.%u avg=%u.%u max=%u.%u us\n", name, n,
        T10(mn) / 10, T10(mn) % 10, T10(a) / 10, T10(a) % 10, T10(mx) / 10, T10(mx) % 10);
}
static void main_fn(void *p)
{
    (void)p;
    TaskHandle_t d = xTimerGetTimerDaemonTaskHandle();
    board_printf_raw("daemon prio=%u, app prio=2, queue len=%u\n",
        (unsigned)uxTaskPriorityGet(d), (unsigned)configTIMER_QUEUE_LENGTH);
    phase("daemon=7, no other timers");
    TimerHandle_t t = xTimerCreate("slow", pdMS_TO_TICKS(5), pdTRUE, NULL, slow_cb);
    xTimerStart(t, 0);
    phase("daemon=7, + 2ms timer cb/5ms");
    xTimerStop(t, 0);
    vTaskPrioritySet(d, 1);
    phase("daemon=1 (< app)");
    board_exit(0);
}
int main(void)
{
    xTaskCreate(app_fn, "app", 256, NULL, 2, NULL);
    xTaskCreate(main_fn, "main", 256, NULL, 6, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D7/f3/ex3.c OUT=/tmp/rtos_D7/ex3 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D7/ex3/app.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
  13916	    100	 135416	 149432	  247b8	/tmp/rtos_D7/ex3/app.elf
daemon prio=7, app prio=2, queue len=10
daemon=7, no other timers  n=49  min=13.6 avg=13.6 max=13.6 us
daemon=7, + 2ms timer cb/5ms  n=50  min=13.6 avg=584.8 max=1943.0 us
daemon=1 (< app)  n=49  min=13.6 avg=1434.5 max=2922.4 us
[board_exit] code=0
```

출력에서 볼 것:

- **daemon이 최고 우선순위이고 혼자일 때 13.6 µs, 흔들림 0** — 전용 태스크(8.1 µs)보다 5.5 µs 느리다. 16바이트 메시지를 큐에 복사해 넣고(`xQueueGenericSendFromISR`) daemon이 다시 복사해 꺼내며(`xQueueReceive`), daemon 루프가 "만료된 타이머가 있나"를 먼저 확인하기 때문이다(§7.1). 대신 태스크를 새로 만들지 않았다.
- **같은 daemon에 2 ms 타이머 콜백이 하나 끼면 최대 1943 µs.** 콜백이 도는 중에 pend된 함수는 콜백이 끝날 때까지 기다린다. daemon은 한 번에 하나만 실행하는 단일 스레드다. "중앙 집중"의 대가가 이것이다 — 모든 사용자가 서로의 최악 실행 시간을 나눠 갖는다.
- **daemon을 앱보다 낮추면 최대 2922 µs** — §2의 "핸들러 < 앱"과 같은 모양이다. 위임 수단이 바뀌어도 규칙은 같다.

### 3.3 예제 4 — 명령 큐가 차면: 한 ISR에서 14개를 pend

무엇을 확인하는 코드인지: ISR 한 번 안에서 `xTimerPendFunctionCallFromISR`를 14번 부르고(예: DMA 완료 14건이 한 번에 몰린 경우), 이어서 같은 ISR에서 `xTimerStartFromISR`도 부른다. ISR이 끝나기 전에는 daemon이 큐를 비울 수 없으므로, 큐 길이를 넘는 요청은 실패해야 한다. 템플릿 설정을 감싸 큐 길이만 바꾸는 `FreeRTOSConfig.h`를 앱 폴더에 둔다(앱 폴더가 include 경로 맨 앞이다).

```c
/* ex4용: 템플릿 설정을 그대로 쓰고 timer 큐 길이만 바꾼다 */
#ifndef D7_CFG_WRAP_H
#define D7_CFG_WRAP_H
#include "/var/rtoslab/templates/freertos-mps2/FreeRTOSConfig.h"
#ifdef D7_QLEN
#undef  configTIMER_QUEUE_LENGTH
#define configTIMER_QUEUE_LENGTH D7_QLEN
#endif
#endif
```

```c
/* ex4: ISR 한 번에 14개를 pend하면? timer 명령 큐(configTIMER_QUEUE_LENGTH)가 차면 무엇이 실패하나 */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "d7.h"

#define BURST 14
static volatile uint32_t ran_mask, ok_mask, start_isr_ok;
static TimerHandle_t tmr;
static void deferred(void *p1, uint32_t idx) { (void)p1; ran_mask |= 1u << idx; }
static void tmr_cb(TimerHandle_t t) { (void)t; board_printf_raw("  timer callback ran\n"); }
void TIMER0_IRQHandler(void)
{
    TIMER_INTCLR(TIMER0_BASE) = 1;
    timer0_stop();
    BaseType_t woken = pdFALSE;
    for (uint32_t i = 0; i < BURST; i++)              /* 예: DMA 완료 14건이 한 ISR에서 몰림 */
        if (xTimerPendFunctionCallFromISR(deferred, NULL, i, &woken) == pdPASS) ok_mask |= 1u << i;
    start_isr_ok = xTimerStartFromISR(tmr, &woken);  /* 같은 큐를 쓰는 타이머 명령 */
    portYIELD_FROM_ISR(woken);
}
static void main_fn(void *p)
{
    (void)p;
    board_printf_raw("configTIMER_QUEUE_LENGTH=%u, burst=%u\n", (unsigned)configTIMER_QUEUE_LENGTH, BURST);
    tmr = xTimerCreate("t", pdMS_TO_TICKS(3), pdFALSE, NULL, tmr_cb);
    timer0_start(500, 0x40);
    vTaskDelay(pdMS_TO_TICKS(10));
    board_printf_raw("  pend ok mask = 0x%x (%u ok)\n", ok_mask, (unsigned)__builtin_popcount(ok_mask));
    board_printf_raw("  ran     mask = 0x%x\n", ran_mask);
    board_printf_raw("  xTimerStartFromISR = %s\n", start_isr_ok == pdPASS ? "pdPASS" : "pdFAIL");
    board_exit(0);
}
int main(void)
{
    xTaskCreate(main_fn, "main", 256, NULL, 2, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'for q in 10 16; do make -s -B -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D7/f4/ex4.c OUT=/tmp/rtos_D7/ex4_q$q EXTRA_CFLAGS=-DD7_QLEN=$q | grep -v filename; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D7/ex4_q$q/app.elf; done'
```

```text
  12800	    100	 135416	 148316	  2435c	/tmp/rtos_D7/ex4_q10/app.elf
configTIMER_QUEUE_LENGTH=10, burst=14
  pend ok mask = 0x3ff (10 ok)
  ran     mask = 0x3ff
  xTimerStartFromISR = pdFAIL
[board_exit] code=0
  12800	    100	 135512	 148412	  243bc	/tmp/rtos_D7/ex4_q16/app.elf
configTIMER_QUEUE_LENGTH=16, burst=14
  timer callback ran
  pend ok mask = 0x3fff (14 ok)
  ran     mask = 0x3fff
  xTimerStartFromISR = pdPASS
[board_exit] code=0
```

(`size` 출력의 머리줄은 `grep -v filename`으로 걸렀다. 이하 반복 빌드하는 예제는 모두 같다.)

출력에서 볼 것:

- **큐 10칸이면 앞의 10개(mask 0x3ff)만 성공, 나머지 4개는 `pdFAIL`**(= `errQUEUE_FULL` = 0). 실패한 4개는 어디에도 기록되지 않는다. 반환값을 확인하지 않으면 "가끔 DMA 완료 처리가 빠진다"는 버그가 된다.
- **pended 함수와 타이머 명령이 같은 큐를 쓴다.** 큐가 차 있으니 `xTimerStartFromISR`도 실패했고, 타이머 콜백이 아예 돌지 않았다. 위임 폭주가 **관련 없는 소프트웨어 타이머까지 죽인다.** 태스크에서 `xTimerStart(t, 0)`을 부르는 코드도 같은 이유로 실패할 수 있다(D6).
- 큐를 16칸으로 늘리면 14개 모두 성공하고 타이머도 돈다. bss가 정확히 96바이트(135512 − 135416) 늘었다 = 6칸 × 16바이트. 메시지 하나가 16바이트라는 뜻이다(예제 5에서 확인).
- 크기 정하는 규칙: **한 ISR(또는 daemon이 못 도는 최악 구간) 동안 쌓일 수 있는 pend 수 + 그동안 태스크들이 보낼 타이머 명령 수**보다 크게.

### 3.4 예제 5 — 메시지 크기와 코드 크기를 ELF에서 확인

무엇을 확인하는 코드인지: `DaemonTaskMessage_t`는 `timers.c` 안의 private 타입이지만 `-g3` 디버그 정보에 남아 있으므로 gdb로 크기를 볼 수 있다. 큐 저장소(static 할당)와 관련 함수 크기를 `nm`으로 본다.

```sh
limactl shell elinux -- bash -lc 'cd /tmp/rtos_D7; for q in 10 16; do arm-none-eabi-nm -S --size-sort ex4_q$q/app.elf | grep -i "TimerQueue\|xActiveTimerList\|ucStaticTimer"; done; gdb -batch -ex "print sizeof(DaemonTaskMessage_t)" -ex "print sizeof(CallbackParameters_t)" -ex "print sizeof(TimerParameter_t)" -ex "ptype DaemonTaskMessage_t" ex4_q10/app.elf; arm-none-eabi-nm -S --size-sort ex3/app.elf | grep -i " prvTimerTask\| xTimerPendFunctionCall\| prvProcess\| xQueueGenericSendFromISR\| xQueueReceive$"; arm-none-eabi-nm -S --size-sort ex2/app.elf | grep -i " vTaskGenericNotifyGiveFromISR\| xQueueGiveFromISR\| ulTaskGenericNotifyTake\| xQueueSemaphoreTake"'
```

```text
20000fbc 00000004 b xTimerQueue
20000fdc 00000014 b xActiveTimerList1
20000fc8 00000014 b xActiveTimerList2
20000ec0 00000054 b xStaticTimerQueue.0
20000f14 000000a0 b ucStaticTimerQueueStorage.1
2000101c 00000004 b xTimerQueue
2000103c 00000014 b xActiveTimerList1
20001028 00000014 b xActiveTimerList2
20000ec0 00000054 b xStaticTimerQueue.0
20000f14 00000100 b ucStaticTimerQueueStorage.1
$1 = 16
$2 = 12
$3 = 8
type = struct tmrTimerQueueMessage {
    BaseType_t xMessageID;
    union {
        TimerParameter_t xTimerParameters;
        CallbackParameters_t xCallbackParameters;
    } u;
}
000021b4 00000030 T xTimerPendFunctionCallFromISR
00001d90 00000090 t prvProcessExpiredTimer
00001a08 000000e8 T xQueueGenericSendFromISR
00001af0 00000160 T xQueueReceive
00001e20 00000250 t prvTimerTask
000016e0 000000b0 T ulTaskGenericNotifyTake
00001d98 000000bc T xQueueGiveFromISR
000018b4 00000128 T vTaskGenericNotifyGiveFromISR
00001fb4 0000018c T xQueueSemaphoreTake
```

출력에서 볼 것:

- 메시지 16 B = `xMessageID` 4 B + union(타이머 명령 8 B, 함수 호출 12 B 중 큰 쪽). `INCLUDE_xTimerPendFunctionCall`을 끄면 union에서 `xCallbackParameters`가 빠져 **메시지가 12 B로 줄어든다** — `timers.c`의 주석 "Don't include xCallbackParameters if it is not going to be used as it makes the structure (and therefore the timer queue) larger."가 그 이유다.
- 큐 저장소 `ucStaticTimerQueueStorage`는 0xa0 = 160 B(10 × 16) → 0x100 = 256 B(16 × 16). 템플릿이 `configSUPPORT_STATIC_ALLOCATION 1`이라 힙이 아니라 bss에 잡힌다(`timers.c` 1135행).
- `xTimerPendFunctionCallFromISR` 자체는 48 B — 메시지를 채워 `xQueueSendFromISR`를 부르는 얇은 포장이다. 실행 쪽은 `prvTimerTask` 592 B 안에 `prvProcessReceivedCommands`가 인라인되어 있다(별도 심볼이 없다).
- 전용 태스크 하나의 RAM은 스택 1 KB(256 word) + TCB다. pend 방식은 메시지 16 B × 큐 길이만 추가로 든다. 인터럽트 원인이 많고 각각이 드물면 RAM에서 크게 이긴다.

### 3.5 언제 쓰나

| 상황 | daemon 위임이 맞나 | 이유 |
|---|---|---|
| 드문 사건의 짧은 뒷처리(fault 보고, 링크 up/down, 버튼) | 예 | 태스크를 아끼고, 13.6 µs면 충분 |
| 원인이 많은데 각각 드물다(센서 10종의 에러 IRQ) | 예 | 원인마다 1 KB 스택을 아낀다 |
| 고빈도 사건(IMU 1~8 kHz, 전류 루프) | 아니오 | 큐 복사 비용과 다른 콜백과의 간섭(최대 1943 µs) |
| 블록하는 일(I2C, 플래시) | 아니오 | daemon이 막히면 모든 소프트웨어 타이머가 멈춘다 |
| 마감이 엄격하고 우선순위를 따로 줘야 하는 일 | 아니오 | daemon 우선순위는 하나뿐이다 — 전용 태스크로 |

흔한 함정: `xTimerPendFunctionCall`(태스크용)에는 `configASSERT( xTimerQueue );`가 있지만 **`FromISR` 판에는 없다**(`timers.c` 1238~1261행을 보면 바로 `xQueueSendFromISR`를 부른다). 스케줄러 시작 전(타이머 큐가 만들어지기 전)에 IRQ가 켜져 ISR이 pend하면 `xQueueGenericSendFromISR`의 `configASSERT( pxQueue != NULL ...)`에서 걸린다 — `configASSERT`를 꺼 둔 양산 빌드라면 NULL 역참조다. IRQ enable은 스케줄러 시작 뒤로, 또는 `xTimerCreate`가 한 번 불린 뒤로 미룬다(`xTimerCreate`도 `prvCheckForValidListAndQueue`로 큐를 만든다).

---

## 4. 패턴 3 — Zephyr workqueue (`k_work`)

### 4.1 직관과 정의

Zephyr는 위임을 **일급 커널 객체**로 만들었다. `struct k_work`는 "할 일 하나"다 — 리스트 노드, 핸들러 함수 포인터, 마지막으로 제출된 큐, 상태 플래그, 이렇게 16 B(예제 9). `struct k_work_q`는 "그 일들을 순서대로 실행하는 스레드 하나 + pending 리스트"다. ISR이든 스레드든 `k_work_submit(&work)`으로 일을 큐에 넣으면, 큐 스레드가 깨어나 `handler(&work)`를 부른다. Linux workqueue(EL F7)와 생각이 같고, FreeRTOS의 pend 방식과 비교하면 두 가지가 다르다.

- **일 객체가 미리 존재한다.** 메시지를 복사하지 않고 객체를 리스트에 연결만 한다. 대신 같은 객체를 두 번 넣을 수 없다 — 이미 대기 중이면 두 번째 제출은 아무것도 안 하고 0을 돌려준다(**합쳐짐**, §4.4).
- **큐를 여러 개 만들 수 있다.** 커널이 만드는 **system workqueue**(`k_sys_work_q`) 하나가 기본이고, `k_work_queue_start`로 자기 우선순위·스택의 큐를 따로 만든다.

핸들러 안에서 원래 데이터에 접근하는 관용구는 `CONTAINER_OF`다 — `struct k_work`를 자기 구조체 안에 넣고, 핸들러가 받은 포인터에서 바깥 구조체를 복원한다.

```c
struct baro_dev {                    /* 개념 코드 — 예제 6~8은 전역 k_work를 쓴다 */
	struct k_work_delayable poll;
	const struct device *i2c;
	int32_t pa;
};
static void baro_poll(struct k_work *w)
{
	struct k_work_delayable *dw = k_work_delayable_from_work(w);
	struct baro_dev *b = CONTAINER_OF(dw, struct baro_dev, poll);
	/* b->i2c 로 읽기 (블록 가능 — 단, 전용 큐에서) ... */
	k_work_schedule(&b->poll, K_MSEC(20));   /* 50 Hz 주기 재예약 */
}
```

### 4.2 API 지도 (v4.4.2 `include/zephyr/kernel.h`)

| API | 하는 일 | ISR에서 | 반환 |
|---|---|---|---|
| `k_work_init(w, handler)` · `K_WORK_DEFINE(w, h)` | 일 객체 초기화 | — | — |
| `k_work_submit(w)` | system workqueue에 제출 | 가능 | 0 이미 대기 · 1 새로 대기 · 2 실행 중이라 같은 큐에 다시 대기 · 음수 실패 |
| `k_work_submit_to_queue(q, w)` | 지정 큐에 제출 | 가능 | 위와 같음 + `-ENODEV`(큐 미시작) · `-EBUSY`(취소 중, drain · plug) |
| `k_work_busy_get(w)` | 상태 플래그 읽기 | 가능 | `K_WORK_RUNNING` 0x1 · `CANCELING` 0x2 · `QUEUED` 0x4 · `DELAYED` 0x8 · `FLUSHING` 0x10 |
| `k_work_cancel(w)` | 대기 중이면 빼기(실행 중이면 못 멈춤) | 가능 | 남은 busy 플래그 |
| `k_work_cancel_sync(w, sync)` | 빼고, 실행 중이면 끝날 때까지 대기 | 불가 | 원래 busy였으면 true |
| `k_work_flush(w, sync)` | 대기 · 실행 중이면 끝날 때까지 대기 | 불가 | 기다렸으면 true |
| `k_work_init_delayable` · `K_WORK_DELAYABLE_DEFINE` | 지연 일 객체 | — | — |
| `k_work_schedule(dw, delay)` | 유휴 · 실행 중일 때만 예약(이미 예약 · 대기면 무시) | 가능 | 1 예약 · 0 이미 예약/대기 (delay가 `K_NO_WAIT`면 submit처럼 2 · 음수도) |
| `k_work_reschedule(dw, delay)` | 기존 예약 취소 후 새로 예약 | 가능 | 1 예약 (0 · 2 · 음수는 delay가 `K_NO_WAIT`일 때만) |
| `k_work_cancel_delayable(_sync)` | 예약 취소(sync는 실행 끝까지 대기) | async만 가능 | busy 플래그 / bool |
| `k_work_queue_start(q, stack, size, prio, cfg)` | 전용 큐 스레드 생성 | 불가 | — |
| `k_work_queue_drain(q, plug)` · `k_work_queue_unplug(q)` | 비울 때까지 대기 · 새 제출 막기/풀기 | 불가 | 0 또는 음수 |
| `k_work_queue_stop(q, timeout)` · `k_work_queue_run(q, cfg)` | 큐 스레드 끝내기 · 현재 스레드를 큐로 쓰기 | 불가 | — |
| `k_work_poll_submit(...)` | `k_poll` 이벤트가 오면 제출 | 불가 | — |

`k_work_queue_run`(지금 스레드를 workqueue로 전환)과 `k_work_queue_stop`은 최근 버전에 추가된 API다 — v4.4.2 `kernel/work.c`에 `k_work_queue_run`(815행), `k_work_queue_stop`(951행)이 있다.

### 4.3 system workqueue — 기본값을 Kconfig와 소스로 확인

`kernel/system_work_q.c`는 38줄짜리 파일이다. 핵심만 옮긴다.

```c
static K_KERNEL_STACK_DEFINE(sys_work_q_stack,
			     CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE);

struct k_work_q k_sys_work_q;

static int k_sys_work_q_init(void)
{
	static const struct k_work_queue_config cfg = {
		.name = "sysworkq",
		.no_yield = IS_ENABLED(CONFIG_SYSTEM_WORKQUEUE_NO_YIELD),
		.essential = true,
		.work_timeout_ms = CONFIG_SYSTEM_WORKQUEUE_WORK_TIMEOUT_MS,
	};

	k_work_queue_start(&k_sys_work_q,
			    sys_work_q_stack,
			    K_KERNEL_STACK_SIZEOF(sys_work_q_stack),
			    CONFIG_SYSTEM_WORKQUEUE_PRIORITY, &cfg);
	return 0;
}

SYS_INIT(k_sys_work_q_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
```

그리고 `kernel/Kconfig`의 기본값:

```text
config SYSTEM_WORKQUEUE_PRIORITY
	int "System workqueue priority"
	default -2 if COOP_ENABLED && !PREEMPT_ENABLED
	default  0 if !COOP_ENABLED
	default -1
	help
	  By default, system work queue priority is the lowest cooperative
	  priority. This means that any work handler, once started, won't
	  be preempted by any other thread until finished.
```

정리하면:

- **우선순위 -1 = 가장 낮은 협력형(cooperative) 우선순위**(협력형과 선점형이 둘 다 켜진 보통 설정). Zephyr에서 음수 우선순위 스레드는 다른 스레드에게 선점되지 않는다(C4) — 핸들러가 한 번 시작하면 스스로 블록하거나 끝날 때까지 **모든 선점형 스레드(우선순위 0 이상)를 막는다.** ISR은 여전히 끼어든다.
- 스택 기본 1024 B(`SYSTEM_WORKQUEUE_STACK_SIZE`, gcov나 특정 Wi-Fi 드라이버가 켜지면 더 크게).
- `.essential = true` — 이 스레드가 죽으면 커널 패닉이다.
- `SYSTEM_WORKQUEUE_NO_YIELD`를 켜지 않으면 항목 사이마다 `k_yield()`한다(§7.2).
- `SYSTEM_WORKQUEUE_WORK_TIMEOUT_MS`(ASSERT 빌드에서 기본 5000, 아니면 0)는 `CONFIG_WORKQUEUE_WORK_TIMEOUT`이 켜졌을 때만 의미가 있다 — 한 항목이 이 시간을 넘기면 **큐 스레드를 abort**한다(§7.2). 감시 장치이지 해결책이 아니다.
- `POST_KERNEL` 단계에서 시작된다. 그보다 이른 `PRE_KERNEL` 초기화 코드에서 `k_work_submit`하면 큐의 STARTED 플래그가 아직 0이라 `queue_submit_locked`가 `-ENODEV`를 돌려준다(§7.2의 코드).

### 4.4 예제 6 — 반환값과 상태 플래그를 직접 보기

무엇을 확인하는 코드인지: ISR에서 같은 일을 두 번 제출, 핸들러 안에서 자기 자신을 다시 제출, 스케줄러를 잠근 채 제출 후 취소, `k_work_schedule`을 두 번 vs `k_work_schedule` + `k_work_reschedule`, 예약된 일을 `k_work_cancel_delayable_sync`로 취소 — 각각의 반환값과 `k_work_busy_get` 플래그를 찍는다. 모든 Zephyr 예제는 같은 `CMakeLists.txt`(`find_package(Zephyr)` + `target_sources(app PRIVATE src/main.c)`)를 쓴다.

`prj.conf`:

```text
CONFIG_PRINTK=y
CONFIG_THREAD_NAME=y
CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000
```

`src/main.c`:

```c
/* z1: k_work / k_work_delayable 반환값과 상태 플래그를 v4.4.2에서 직접 확인 */
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#define T0_CTRL   (*(volatile uint32_t *)0x40000000u)
#define T0_RELOAD (*(volatile uint32_t *)0x40000008u)
#define T0_CLR    (*(volatile uint32_t *)0x4000000Cu)

static struct k_work w;
static struct k_work_delayable dw;
static volatile int r_isr1, r_isr2, runs;
static int64_t t_sched;

static void w_handler(struct k_work *wk)
{
	runs++;
	printk("  [w] run #%d in '%s' prio %d, busy=0x%x\n", runs,
	       k_thread_name_get(k_current_get()), k_thread_priority_get(k_current_get()),
	       k_work_busy_get(wk));
	if (runs == 1) {
		int r = k_work_submit(wk);        /* 실행 중인 자기 자신을 다시 제출 */
		printk("  [w] submit self while running -> %d, busy=0x%x\n", r, k_work_busy_get(wk));
	}
}
static void dw_handler(struct k_work *wk)
{
	ARG_UNUSED(wk);
	printk("  [dw] fired %lld ms after first schedule\n", k_uptime_get() - t_sched);
}
static void t0_isr(const void *arg)
{
	ARG_UNUSED(arg);
	T0_CLR = 1; T0_CTRL = 0;
	r_isr1 = k_work_submit(&w);              /* ISR에서 제출: 1 = 새로 큐에 들어감 */
	r_isr2 = k_work_submit(&w);              /* 아직 대기 중: 0 = 이미 큐에 있음 */
}
int main(void)
{
	printk("sysworkq prio=%d (CONFIG_SYSTEM_WORKQUEUE_PRIORITY=%d, NUM_COOP=%d), stack=%d\n",
	       k_thread_priority_get(&k_sys_work_q.thread), CONFIG_SYSTEM_WORKQUEUE_PRIORITY,
	       CONFIG_NUM_COOP_PRIORITIES, CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE);
	k_work_init(&w, w_handler);
	k_work_init_delayable(&dw, dw_handler);
	IRQ_CONNECT(8, 2, t0_isr, NULL, 0); irq_enable(8);
	T0_RELOAD = 25000; T0_CTRL = 0x9;         /* 1 ms 뒤 IRQ */
	k_msleep(5);
	printk("ISR submit x2 -> %d, %d ; total runs=%d\n", r_isr1, r_isr2, runs);

	k_sched_lock();                          /* sysworkq가 못 돌게 잡아 두고 */
	int s = k_work_submit(&w);
	int c = k_work_cancel(&w);               /* 큐에서 빼기: 반환 = 남은 busy 플래그 */
	k_sched_unlock();
	k_msleep(2);
	printk("submit=%d cancel->busy=0x%x ; runs still %d\n", s, c, runs);

	t_sched = k_uptime_get();
	printk("schedule(50ms)=%d, ", k_work_schedule(&dw, K_MSEC(50)));
	printk("schedule(10ms) again=%d  (마감 유지)\n", k_work_schedule(&dw, K_MSEC(10)));
	k_msleep(80);
	t_sched = k_uptime_get();
	printk("schedule(50ms)=%d, ", k_work_schedule(&dw, K_MSEC(50)));
	printk("reschedule(10ms)=%d  (마감 교체)\n", k_work_reschedule(&dw, K_MSEC(10)));
	k_msleep(80);
	struct k_work_sync sync;
	k_work_schedule(&dw, K_MSEC(50));
	printk("cancel_delayable_sync on pending -> %d, busy=0x%x\n",
	       k_work_cancel_delayable_sync(&dw, &sync), k_work_delayable_busy_get(&dw));
	k_msleep(80);
	printk("done\n");
	return 0;
}
```

빌드 · 실행 스크립트(`zb.sh`, 모든 Zephyr 예제 공용 — `rtos-zbuild`와 같은 환경 변수에 `-Wall -Wextra`를 더하고, 앱 소스의 경고 줄 수를 센 뒤, FreeRTOS와 같은 `-icount shift=5`로 실행한다):

```sh
#!/bin/bash
# zb.sh <app> : Zephyr mps2/an385 빌드(-Wall -Wextra) + icount shift=5로 실행
A=$1; TO=${2:-8}
export HOME=/tmp/rtos_D7/home XDG_CACHE_HOME=/tmp/rtos_D7/home/.cache
mkdir -p $XDG_CACHE_HOME
. /var/rtoslab/zephyr-venv/bin/activate
export ZEPHYR_BASE=/var/rtoslab/zephyrproject/zephyr ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb GNUARMEMB_TOOLCHAIN_PATH=/usr
cd /var/rtoslab/zephyrproject
west build -p always -b mps2/an385 /Users/donh/.rtos_scratch/D7/$A -d /tmp/rtos_D7/$A -- -DEXTRA_CFLAGS="-Wall -Wextra" > /tmp/rtos_D7/$A.log 2>&1
echo "app warnings: $(grep -c "D7/$A/src.*warning" /tmp/rtos_D7/$A.log)"; grep -E "FLASH:|RAM:|error:" /tmp/rtos_D7/$A.log
timeout $TO qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor none -serial stdio \
  -icount shift=5,align=off,sleep=off -kernel /tmp/rtos_D7/$A/zephyr/zephyr.elf
```

```sh
limactl shell elinux -- bash /Users/donh/.rtos_scratch/D7/zb.sh z1
```

Zephyr 앱은 스스로 QEMU를 끝내지 않으므로 `timeout`이 끝낸다(마지막 줄의 signal 15, 종료 코드 124). 이하 Zephyr 예제 모두 같다.

```text
app warnings: 0
           FLASH:       16204 B         4 MB      0.39%
             RAM:        7520 B         4 MB      0.18%
*** Booting Zephyr OS build v4.4.2 ***
sysworkq prio=-1 (CONFIG_SYSTEM_WORKQUEUE_PRIORITY=-1, NUM_COOP=16), stack=1024
  [w] run #1 in 'sysworkq' prio -1, busy=0x1
  [w] submit self while running -> 2, busy=0x5
  [w] run #2 in 'sysworkq' prio -1, busy=0x1
ISR submit x2 -> 1, 0 ; total runs=2
submit=1 cancel->busy=0x0 ; runs still 2
schedule(50ms)=1, schedule(10ms) again=0  (마감 유지)
  [dw] fired 51 ms after first schedule
schedule(50ms)=1, reschedule(10ms)=1  (마감 교체)
  [dw] fired 11 ms after first schedule
cancel_delayable_sync on pending -> 1, busy=0x0
done
qemu-system-arm: terminating on signal 15 from pid 790642 (timeout)
```

출력에서 볼 것 (한 줄씩):

- `sysworkq prio=-1 ... NUM_COOP=16` — Kconfig 기본값 그대로, 협력형 16단계 중 가장 낮은 -1이다.
- `ISR submit x2 -> 1, 0` — 첫 제출은 1(새로 대기), 두 번째는 0(**이미 대기 중이라 아무것도 안 함**). 핸들러는 두 번 제출에 한 번만 돈다. 이것이 합쳐짐이다.
- `run #1 ... busy=0x1` — 핸들러가 도는 동안 플래그는 RUNNING(0x1)뿐이다. 큐 스레드가 핸들러를 부르기 **직전에** QUEUED를 지우기 때문이다(§7.2).
- `submit self while running -> 2, busy=0x5` — 실행 중에 다시 제출하면 2를 돌려주고 플래그가 RUNNING + QUEUED(0x5)가 된다. 핸들러가 끝나면 **같은 큐에서** 한 번 더 돈다(`run #2`). 다른 큐로 보내도 같은 큐로 간다 — 핸들러가 두 스레드에서 동시에 돌지 않게 하려는 규칙이다(재진입 방지).
- `submit=1 cancel->busy=0x0 ; runs still 2` — `k_sched_lock()`으로 sysworkq가 못 돌게 잡아 둔 사이 제출 → 취소하면 남은 플래그 0, 핸들러는 끝내 돌지 않는다. (잠그지 않으면 sysworkq(-1)가 main(0)보다 높아서 `k_work_submit` 안에서 이미 실행돼 버린다 — 예제 8에서 이 효과가 다시 나온다.)
- `schedule(50ms)=1, schedule(10ms) again=0` → 51 ms 뒤 발사 — `k_work_schedule`은 **이미 예약된 일의 마감을 건드리지 않는다.** 첫 마감이 이긴다.
- `schedule(50ms)=1, reschedule(10ms)=1` → 11 ms 뒤 발사 — `k_work_reschedule`은 기존 예약을 지우고 **새 마감으로 교체한다.** 마지막 호출이 이긴다.
- 51과 11에 붙은 1 ms는 C12 · C5에서 본 "`z_add_timeout`이 1 tick을 더한다" 규칙이다(1 kHz tick).
- `cancel_delayable_sync on pending -> 1, busy=0x0` — 예약 중이던 일을 취소했고(true), 플래그가 0이 됐다.

`k_work_schedule`과 `k_work_reschedule`의 차이는 실무 용어로 **throttle vs debounce**다.

| 하고 싶은 일 | 호출 | 결과 |
|---|---|---|
| 버튼 채터링 제거 — 마지막 입력 후 30 ms 조용하면 처리 | 입력마다 `k_work_reschedule(dw, K_MSEC(30))` | 입력이 계속 오면 계속 미뤄진다(debounce) |
| 설정 변경 저장 — 처음 변경 후 최대 1초 안에 한 번 저장 | 변경마다 `k_work_schedule(dw, K_SECONDS(1))` | 첫 변경 기준 1초 후 한 번(throttle). 그사이 변경은 합쳐짐 |
| 주기 폴링(바로미터 50 Hz) | 핸들러 끝에서 `k_work_schedule(dw, K_MSEC(20))` | 핸들러 실행 시간만큼 주기가 늘어난다(drift) — 정확한 주기는 `k_timer`(D6) |

### 4.5 그림 — `k_work` 상태와 반환값

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="18" font-size="13" font-weight="bold">k_work 상태 플래그 (kernel/work.c, v4.4.2) — 화살표 위 숫자는 submit의 반환값</text>
<rect x="20" y="120" width="110" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="75" y="147" font-size="13" text-anchor="middle">idle 0x0</text>
<rect x="270" y="45" width="150" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="345" y="66" font-size="13" text-anchor="middle">QUEUED 0x4</text>
<text x="345" y="84" font-size="12" text-anchor="middle">다시 submit → 0</text>
<rect x="520" y="120" width="140" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="590" y="147" font-size="13" text-anchor="middle">RUNNING 0x1</text>
<rect x="265" y="225" width="170" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="350" y="252" font-size="13" text-anchor="middle">RUNNING+QUEUED 0x5</text>
<rect x="20" y="225" width="130" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="85" y="252" font-size="13" text-anchor="middle">DELAYED 0x8</text>
<line x1="130" y1="128" x2="268" y2="80" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="150" y="96" font-size="12">submit → 1</text>
<line x1="420" y1="75" x2="560" y2="118" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="500" y="84" font-size="12">큐 스레드가 꺼냄</text>
<line x1="520" y1="150" x2="132" y2="150" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="400" y="143" font-size="12" text-anchor="middle">handler 반환 (다시 제출 없음)</text>
<line x1="570" y1="164" x2="437" y2="232" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="520" y="215" font-size="12">submit → 2</text>
<line x1="330" y1="225" x2="330" y2="97" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="336" y="195" font-size="12">handler 반환 → 다시 대기</text>
<line x1="75" y1="164" x2="75" y2="223" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="82" y="200" font-size="12">schedule(d) → 1</text>
<line x1="150" y1="232" x2="275" y2="97" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="196" y="190" font-size="12">만료</text>
<text x="20" y="293" font-size="12">DELAYED에서 schedule → 0 (마감 유지), reschedule → 1 (마감 교체). 만료는 timer ISR 문맥에서 submit.</text>
<text x="20" y="311" font-size="12">cancel: QUEUED → idle, RUNNING → CANCELING 0x2 (실행은 못 멈춤, 끝날 때 정리)</text>
</svg>
```

그림 3 — `k_work` 한 객체의 상태. QUEUED는 큐 스레드가 핸들러를 부르기 직전에 지워지므로, 핸들러 안에서 자기 자신을 다시 제출하면 RUNNING + QUEUED가 되고 반환값은 2다.

### 4.6 예제 7 — Zephyr에서 ISR → 일 시작 지연: 전용 스레드 · sysworkq · 전용 큐

무엇을 확인하는 코드인지: 예제 2 · 3과 같은 조건(7 ms 주기 TIMER0, 우선순위 5의 바쁜 앱 = 3 ms 계산 + 1 ms 쉼)에서 네 가지 위임 수단의 지연을 잰다 — `k_sem` → 전용 스레드(우선순위 2), sysworkq(-1), 전용 workqueue 우선순위 2, 같은 큐를 우선순위 8(앱보다 낮게)로 바꿔서. Zephyr에서 숫자가 작을수록 높은 우선순위다.

```c
/* z2: Zephyr에서 ISR → 일 시작 지연. 전용 스레드 / sysworkq / 전용 workqueue(높게·낮게) */
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#define REG(a)  (*(volatile uint32_t *)(a))
#define CYC()   REG(0x40028018u)                      /* FPGAIO 25 MHz */
static void spin_us(uint32_t us) { uint32_t t = CYC(); while (CYC() - t < us * 25u) { } }

enum { M_SEM, M_SYSWQ, M_MYWQ };
static volatile int mode;
static volatile uint32_t t_isr, n, sum, mn, mx;
static K_SEM_DEFINE(sem, 0, 1);
static struct k_work w;
static struct k_work_q mywq;
static K_THREAD_STACK_DEFINE(mywq_stack, 1024);

static void record(void)
{
	uint32_t d = CYC() - t_isr;
	n++; sum += d; if (d < mn) mn = d; if (d > mx) mx = d;
	spin_us(50);
}
static void w_handler(struct k_work *wk) { ARG_UNUSED(wk); record(); }
static void t0_isr(const void *a)
{
	ARG_UNUSED(a);
	REG(0x4000000Cu) = 1;
	t_isr = CYC();
	if (mode == M_SEM)        k_sem_give(&sem);
	else if (mode == M_SYSWQ) k_work_submit(&w);
	else                      k_work_submit_to_queue(&mywq, &w);
}
static void irq_thread(void *a, void *b, void *c)    /* 전용 deferred-interrupt 스레드 */
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	for (;;) { k_sem_take(&sem, K_FOREVER); record(); }
}
K_THREAD_DEFINE(irq_tid, 1024, irq_thread, NULL, NULL, NULL, 2, 0, 0);
static void app(void *a, void *b, void *c)          /* 바쁜 앱: 3 ms 계산, 1 ms 쉼 */
{ ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c); for (;;) { spin_us(3000); k_msleep(1); } }
K_THREAD_DEFINE(app_tid, 1024, app, NULL, NULL, NULL, 5, 0, 0);

#define T10(c) ((c) * 10u / 25u)
static void phase(const char *name, int m)
{
	mode = m; n = sum = mx = 0; mn = UINT32_MAX;
	REG(0x40000008u) = 7000u * 25u - 1u; REG(0x40000000u) = 0x9;   /* TIMER0 7 ms */
	k_msleep(350);
	REG(0x40000000u) = 0; k_msleep(5);
	uint32_t a = sum / n;
	printk("%-26s n=%u min=%u.%u avg=%u.%u max=%u.%u us\n", name, n, T10(mn) / 10, T10(mn) % 10,
	       T10(a) / 10, T10(a) % 10, T10(mx) / 10, T10(mx) % 10);
}
int main(void)
{
	k_work_init(&w, w_handler);
	struct k_work_queue_config cfg = { .name = "mywq" };
	k_work_queue_start(&mywq, mywq_stack, K_THREAD_STACK_SIZEOF(mywq_stack), 2, &cfg);
	IRQ_CONNECT(8, 2, t0_isr, NULL, 0); irq_enable(8);
	phase("sem -> thread prio 2", M_SEM);
	phase("sysworkq (prio -1, coop)", M_SYSWQ);
	phase("my wq prio 2 (> app 5)", M_MYWQ);
	k_thread_priority_set(k_work_queue_thread_get(&mywq), 8);
	phase("my wq prio 8 (< app 5)", M_MYWQ);
	printk("done\n");
	return 0;
}
```

```sh
limactl shell elinux -- bash /Users/donh/.rtos_scratch/D7/zb.sh z2
```

```text
app warnings: 0
           FLASH:       15628 B         4 MB      0.37%
             RAM:       11072 B         4 MB      0.26%
*** Booting Zephyr OS build v4.4.2 ***
sem -> thread prio 2       n=50 min=10.5 avg=16.1 max=30.4 us
sysworkq (prio -1, coop)   n=50 min=9.7 avg=13.2 max=15.7 us
my wq prio 2 (> app 5)     n=50 min=13.7 avg=19.2 max=23.0 us
my wq prio 8 (< app 5)     n=50 min=13.7 avg=1087.4 max=3059.4 us
done
qemu-system-arm: terminating on signal 15 from pid 795282 (timeout)
```

출력에서 볼 것:

- **세 방식 모두 10~20 µs대**(앱보다 높을 때). FreeRTOS의 8.1 µs(notify), 13.6 µs(daemon)와 같은 규모다. Zephyr 쪽이 몇 µs 더 드는 것은 ISR 출구 경로(`_isr_wrapper` → `z_arm_exc_exit` → PendSV)와 더 일반적인 wait queue 구조 때문이다(C8 · C12).
- sysworkq(9.7 µs)가 `k_sem` → 스레드(10.5 µs)보다 오히려 빠르다. 일 객체를 리스트에 붙이고 큐 스레드 하나를 깨우는 경로(`submit_to_queue_locked` → `z_sched_wake`)가 세마포어 경로와 거의 같은 길이라는 뜻이다. "workqueue는 무겁다"는 직관은 이 규모에서는 틀렸다.
- `k_sem` → 스레드의 최대 30.4 µs는 IRQ가 SysTick 처리와 겹친 샘플이다(최소값 쪽이 패턴 비용).
- **전용 큐를 앱보다 낮게(8) 두면 최대 3059 µs** — 예제 2의 "핸들러 < 앱"(3061 µs), 예제 3의 "daemon < 앱"(2922 µs)과 같은 숫자다. RTOS가 달라도, 위임 수단이 달라도 규칙은 하나다.

### 4.7 예제 8 — system workqueue에서 블록하면

무엇을 확인하는 코드인지: TIMER0 ISR이 7 ms마다 짧은 일 B(예: IMU 샘플 후처리)를 sysworkq에 제출하고 B의 지연을 잰다. 그 사이 main이 50 ms마다 일 A를 제출한다. A가 (1) 100 µs로 짧을 때, (2) `k_msleep(20)`으로 블록할 때, (3) `k_busy_wait(20000)`으로 20 ms 계산할 때, (4)(5) 같은 A를 우선순위 7의 전용 큐로 옮겼을 때를 비교한다. 동시에 선점형 앱 스레드(우선순위 5, 1 ms 주기)의 최대 실행 간격도 잰다. ISR은 `k_work_submit`이 1을 돌려줄 때(새로 대기)의 시각만 기록하고, 0이면(합쳐짐) 유실 수를 센다. 각 단계 끝에서 `k_work_flush`로 A가 끝나기를 기다리는 시간도 잰다.

```c
/* z3: sysworkq 항목 하나가 블록하면 같은 큐의 다른 항목(B)과 다른 스레드가 얼마나 늦어지나 */
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#define REG(a)  (*(volatile uint32_t *)(a))
#define CYC()   REG(0x40028018u)
static void spin_us(uint32_t us) { uint32_t t = CYC(); while (CYC() - t < us * 25u) { } }

enum { A_QUICK, A_SLEEP, A_SPIN };
static volatile int a_kind;
static volatile uint32_t t_first, bn, bsum, bmx, coalesced, app_gap_max;
static struct k_work wa, wb;
static struct k_work_q slowq;
static K_THREAD_STACK_DEFINE(slowq_stack, 1024);

static void a_handler(struct k_work *w)               /* 예: 플래시 쓰기, I2C 재시도 */
{
	ARG_UNUSED(w);
	if (a_kind == A_QUICK)      spin_us(100);
	else if (a_kind == A_SLEEP) k_msleep(20);           /* 블로킹 호출 */
	else                        k_busy_wait(20000);     /* 20 ms 계산 */
}
static void b_handler(struct k_work *w)               /* 예: IMU 샘플 후처리 */
{
	ARG_UNUSED(w);
	uint32_t d = CYC() - t_first; bn++; bsum += d; if (d > bmx) bmx = d;
}
static void t0_isr(const void *p)
{
	ARG_UNUSED(p); REG(0x4000000Cu) = 1;
	uint32_t t = CYC();
	if (k_work_submit(&wb) == 1) t_first = t;           /* 새로 큐에 들어갈 때의 시각 */
	else coalesced++;                                   /* 0: 이미 대기 중 → 합쳐짐(사건 유실) */
}
static void app(void *a, void *b, void *c)            /* 선점형 앱 스레드 prio 5: 1 ms 주기 */
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	uint32_t last = CYC();
	for (;;) { k_msleep(1); uint32_t now = CYC(); if (now - last > app_gap_max) app_gap_max = now - last; last = now; }
}
K_THREAD_DEFINE(app_tid, 1024, app, NULL, NULL, NULL, 5, 0, 0);

static void phase(const char *name, int kind, struct k_work_q *qa)
{
	a_kind = kind; bn = bsum = bmx = coalesced = 0; k_msleep(2); app_gap_max = 0;
	REG(0x40000008u) = 7000u * 25u - 1u; REG(0x40000000u) = 0x9;
	for (int i = 0; i < 6; i++) { k_work_submit_to_queue(qa, &wa); k_msleep(50); }
	REG(0x40000000u) = 0;
	struct k_work_sync sync;
	uint32_t f0 = CYC(); k_work_submit_to_queue(qa, &wa);
	bool waited = k_work_flush(&wa, &sync);            /* A가 끝날 때까지 기다림 */
	printk("%-22s B n=%u lost=%u avg=%u max=%u us | app gap max=%u us | flush %d %u us\n", name,
	       bn, coalesced, bsum / bn / 25u, bmx / 25u, app_gap_max / 25u, waited, (CYC() - f0) / 25u);
}
int main(void)
{
	k_work_init(&wa, a_handler); k_work_init(&wb, b_handler);
	k_work_queue_start(&slowq, slowq_stack, K_THREAD_STACK_SIZEOF(slowq_stack), 7, NULL);
	IRQ_CONNECT(8, 2, t0_isr, NULL, 0); irq_enable(8);
	phase("A quick on sysworkq", A_QUICK, &k_sys_work_q);
	phase("A sleep20 on sysworkq", A_SLEEP, &k_sys_work_q);
	phase("A spin20 on sysworkq", A_SPIN, &k_sys_work_q);
	phase("A sleep20 on own q(7)", A_SLEEP, &slowq);
	phase("A spin20 on own q(7)", A_SPIN, &slowq);
	printk("done\n");
	return 0;
}
```

```sh
limactl shell elinux -- bash /Users/donh/.rtos_scratch/D7/zb.sh z3 12
```

```text
app warnings: 0
           FLASH:       15496 B         4 MB      0.37%
             RAM:        9704 B         4 MB      0.23%
*** Booting Zephyr OS build v4.4.2 ***
A quick on sysworkq    B n=43 lost=0 avg=9 max=15 us | app gap max=2169 us | flush 0 122 us
A sleep20 on sysworkq  B n=31 lost=12 avg=3258 max=20154 us | app gap max=2073 us | flush 1 21021 us
A spin20 on sysworkq   B n=49 lost=11 avg=1925 max=18295 us | app gap max=22075 us | flush 0 20028 us
A sleep20 on own q(7)  B n=43 lost=0 avg=9 max=15 us | app gap max=2049 us | flush 1 21084 us
A spin20 on own q(7)   B n=43 lost=0 avg=10 max=15 us | app gap max=2046 us | flush 1 20090 us
done
qemu-system-arm: terminating on signal 15 from pid 801032 (timeout)
```

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">sysworkq 항목 하나가 블록할 때 (z3 측정)</text>
<rect x="380" y="28" width="12" height="10" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/><text x="397" y="37" font-size="12">B 최대 지연</text>
<rect x="490" y="28" width="12" height="10" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/><text x="507" y="37" font-size="12">앱 최대 간격</text>
<line x1="70" y1="260.0" x2="650" y2="260.0" stroke="#888" stroke-dasharray="2,4"/>
<text x="64" y="264.0" font-size="12" text-anchor="end">0</text>
<line x1="70" y1="218.0" x2="650" y2="218.0" stroke="#888" stroke-dasharray="2,4"/>
<text x="64" y="222.0" font-size="12" text-anchor="end">5</text>
<line x1="70" y1="176.0" x2="650" y2="176.0" stroke="#888" stroke-dasharray="2,4"/>
<text x="64" y="180.0" font-size="12" text-anchor="end">10</text>
<line x1="70" y1="134.0" x2="650" y2="134.0" stroke="#888" stroke-dasharray="2,4"/>
<text x="64" y="138.0" font-size="12" text-anchor="end">15</text>
<line x1="70" y1="92.0" x2="650" y2="92.0" stroke="#888" stroke-dasharray="2,4"/>
<text x="64" y="96.0" font-size="12" text-anchor="end">20</text>
<line x1="70" y1="50.0" x2="650" y2="50.0" stroke="#888" stroke-dasharray="2,4"/>
<text x="64" y="54.0" font-size="12" text-anchor="end">25</text>
<text x="64" y="40" font-size="12" text-anchor="end">ms</text>
<rect x="88" y="259.9" width="34" height="0.1" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="128" y="241.8" width="34" height="18.2" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="105" y="255.9" font-size="12" text-anchor="middle">0.015</text>
<text x="145" y="237.8" font-size="12" text-anchor="middle">2.169</text>
<text x="125" y="276" font-size="12" text-anchor="middle">A 짧음</text>
<text x="125" y="291" font-size="12" text-anchor="middle">sysworkq</text>
<text x="125" y="306" font-size="12" text-anchor="middle">유실 0</text>
<rect x="204" y="90.7" width="34" height="169.3" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="244" y="242.6" width="34" height="17.4" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="221" y="86.7" font-size="12" text-anchor="middle">20.154</text>
<text x="261" y="238.6" font-size="12" text-anchor="middle">2.073</text>
<text x="241" y="276" font-size="12" text-anchor="middle">A sleep 20</text>
<text x="241" y="291" font-size="12" text-anchor="middle">sysworkq</text>
<text x="241" y="306" font-size="12" text-anchor="middle">유실 12</text>
<rect x="320" y="106.3" width="34" height="153.7" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="360" y="74.6" width="34" height="185.4" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="337" y="102.3" font-size="12" text-anchor="middle">18.295</text>
<text x="377" y="70.6" font-size="12" text-anchor="middle">22.075</text>
<text x="357" y="276" font-size="12" text-anchor="middle">A spin 20</text>
<text x="357" y="291" font-size="12" text-anchor="middle">sysworkq</text>
<text x="357" y="306" font-size="12" text-anchor="middle">유실 11</text>
<rect x="436" y="259.9" width="34" height="0.1" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="476" y="242.8" width="34" height="17.2" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="453" y="255.9" font-size="12" text-anchor="middle">0.015</text>
<text x="493" y="238.8" font-size="12" text-anchor="middle">2.049</text>
<text x="473" y="276" font-size="12" text-anchor="middle">A sleep 20</text>
<text x="473" y="291" font-size="12" text-anchor="middle">전용 q(7)</text>
<text x="473" y="306" font-size="12" text-anchor="middle">유실 0</text>
<rect x="552" y="259.9" width="34" height="0.1" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="592" y="242.8" width="34" height="17.2" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="569" y="255.9" font-size="12" text-anchor="middle">0.015</text>
<text x="609" y="238.8" font-size="12" text-anchor="middle">2.046</text>
<text x="589" y="276" font-size="12" text-anchor="middle">A spin 20</text>
<text x="589" y="291" font-size="12" text-anchor="middle">전용 q(7)</text>
<text x="589" y="306" font-size="12" text-anchor="middle">유실 0</text>
<line x1="70" y1="260" x2="650" y2="260" stroke="currentColor"/>
<line x1="70" y1="50" x2="70" y2="260" stroke="currentColor"/>
</svg>
```

그림 4 — 예제 8의 측정. sysworkq 항목 하나가 20 ms 블록(sleep)하면 같은 큐의 B가 최대 20.2 ms 늦고 12건이 합쳐져 사라진다. 20 ms 계산(spin)하면 협력형이라 선점형 앱 스레드까지 22 ms 멈춘다. 같은 A를 전용 큐로 옮기면 B와 앱 모두 원래대로 돌아온다.

출력에서 볼 것:

- **A가 sleep하면 B 최대 20154 µs, 유실 12건.** sleep은 CPU를 놓지만 **큐 스레드는 놓지 않는다.** 큐 스레드가 A 안에서 자고 있으니 B는 리스트에서 기다리고, 그사이 ISR이 다시 제출한 B는 "이미 대기 중"이라 0을 받고 사라진다. B가 IMU 샘플 처리였다면 샘플 12개가 처리되지 않은 것이다. 선점형 앱은 멀쩡하다(최대 간격 2073 µs = 평소와 같음).
- **A가 spin하면 앱 최대 간격 22075 µs.** sysworkq는 우선순위 -1(협력형)이라, 한 번 돌기 시작하면 선점형 스레드 누구도 끼어들 수 없다. 블록하지 않는 긴 계산이 sleep보다 더 나쁘다 — 같은 큐의 일뿐 아니라 **시스템의 모든 선점형 스레드**를 막는다.
- 평소 앱 간격이 1 ms가 아니라 약 2 ms인 것은 `k_msleep(1)`에 1 tick이 더해지는 규칙(C5) 때문이다.
- **A를 전용 큐(우선순위 7)로 옮기면 B는 9~15 µs로 돌아오고 유실 0.** spin하는 A조차 앱(5)보다 낮아서 앱도 2 ms 간격을 지킨다.
- **`k_work_flush`의 반환값이 두 경우에 다르다.** A가 sysworkq에 있을 때 main(우선순위 0)이 제출하면 sysworkq(-1)가 즉시 선점해서 `k_work_submit_to_queue` **호출 안에서** A가 끝나 버린다. 그래서 flush 시점에는 할 일이 없어 `false`(0)인데, 경과 시간은 이미 20 ms다(제출 자체가 20 ms 걸렸다). 전용 큐(7)는 main보다 낮아서 제출이 즉시 돌아오고, flush가 실제로 기다려 `true`(1)와 21 ms를 돌려준다. "submit은 금방 돌아온다"는 가정이 **제출하는 스레드보다 큐가 높으면 깨진다.**
- 규칙: **sysworkq 항목은 짧게, 블록 없이.** I2C 트랜잭션, 플래시 쓰기, 재시도 루프, `k_sleep`은 전용 큐로. Zephyr 드라이버도 sysworkq를 쓴다 — 센서 드라이버 다수가 `<드라이버>_TRIGGER_GLOBAL_THREAD` 옵션에서 인터럽트 뒷일을 sysworkq로 보낸다(v4.4.2 `drivers/sensor` 아래 이 이름이 나오는 파일 227개, 예: `bosch/bmi270/bmi270_trigger.c`의 `k_work_submit(&data->trig_work)`). 앱 하나의 블로킹 항목이 IMU 드라이버의 data-ready 처리까지 늦출 수 있다는 뜻이다.

### 4.8 예제 9 — 객체 크기와 설정을 ELF와 `.config`에서 확인

무엇을 확인하는 코드인지: `k_work`, `k_work_delayable`, `k_work_q`가 RAM을 얼마나 먹는지, sysworkq 스택과 객체가 어디에 있는지, 실제로 적용된 Kconfig 값이 무엇인지 예제 6의 빌드에서 본다.

```sh
limactl shell elinux -- bash -lc 'cd /tmp/rtos_D7; gdb -batch -ex "print sizeof(struct k_work)" -ex "print sizeof(struct k_work_delayable)" -ex "print sizeof(struct k_work_q)" -ex "print sizeof(struct k_sem)" z1/zephyr/zephyr.elf 2>&1 | grep "^\\$"; arm-none-eabi-nm -S --size-sort z1/zephyr/zephyr.elf | grep -E " (k_sys_work_q|sys_work_q_stack|work_queue_main|submit_to_queue_locked|k_work_submit_to_queue|k_work_submit|z_sched_wake|k_work_schedule_for_queue|k_work_reschedule_for_queue)$"; grep -E "^CONFIG_(SYSTEM_WORKQUEUE|NUM_COOP|NUM_PREEMPT|MAIN_THREAD_PRIORITY|WORKQUEUE|TICKLESS_KERNEL|SYS_CLOCK_TICKS|QEMU_ICOUNT)" z1/zephyr/.config'
```

```text
$1 = 16
$2 = 48
$3 = 192
$4 = 16
0000147c 0000000c T k_work_submit
00002f78 00000020 T k_work_submit_to_queue
00003176 00000046 T z_sched_wake
00001508 00000054 T k_work_schedule_for_queue
00001568 0000005c T k_work_reschedule_for_queue
000013d8 000000a4 t submit_to_queue_locked
20000220 000000c0 B k_sys_work_q
0000123c 00000160 t work_queue_main
20001960 00000400 b sys_work_q_stack
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=1024
CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000
CONFIG_TICKLESS_KERNEL=y
CONFIG_NUM_PREEMPT_PRIORITIES=15
CONFIG_QEMU_ICOUNT=y
CONFIG_QEMU_ICOUNT_SHIFT=7
CONFIG_NUM_COOP_PRIORITIES=16
CONFIG_MAIN_THREAD_PRIORITY=0
CONFIG_SYSTEM_WORKQUEUE_PRIORITY=-1
CONFIG_SYSTEM_WORKQUEUE_WORK_TIMEOUT_MS=0
```

출력에서 볼 것:

- **`k_work` 16 B = `k_sem` 16 B.** 일 하나를 위임 가능하게 만드는 비용이 세마포어 하나와 같다. delayable은 timeout 노드가 붙어 48 B. 큐 객체는 192 B(안에 `struct k_thread`가 들어 있다) + 스택. sysworkq는 192 B 객체(0xc0) + 1024 B 스택(0x400)이 이미 이미지에 있으므로, sysworkq에 일을 추가하는 비용은 **항목당 16 B**뿐이다.
- `k_work_submit`은 12 B짜리 포장(`k_work_submit_to_queue(&k_sys_work_q, w)`), 실제 판단은 `submit_to_queue_locked`(164 B)와 큐 스레드 본체 `work_queue_main`(352 B)에 있다.
- `.config`의 `CONFIG_QEMU_ICOUNT_SHIFT=7`은 보드 기본값이다(`west build -t run`이 명령당 128 ns로 돌린다). 이 노트는 `zb.sh`가 `qemu-system-arm`을 `-icount shift=5`로 직접 실행하므로 이 값과 무관하게 FreeRTOS 예제와 같은 32 ns/명령이다.

---

## 5. 패턴 4 — 게이트키퍼 태스크

### 5.1 직관과 정의

지금까지는 "ISR → 스레드" 위임이었다. 같은 생각을 **"여러 태스크 → 자원 하나"**에 적용한 것이 게이트키퍼(gatekeeper)다. 자원(UART 로그 포트, SPI 플래시, 디스플레이, 무선 모뎀)을 **오직 한 태스크만** 만진다. 다른 태스크(와 ISR)는 그 자원을 직접 쓰지 않고 요청을 큐에 넣는다. 자원에 대한 뮤텍스가 없으니 **우선순위 역전(D3)도, 데드락(D9)도 생길 자리가 없다.** FreeRTOS 공식 책이 자원 관리 장에서 "gatekeeper task"라는 이름으로 소개하는 패턴이고, 이벤트 구동 설계의 active object(A2 §6)와 같은 뿌리다.

Don의 SSD 펌웨어로 말하면, NAND 채널마다 **채널 관리자 하나만** 그 채널의 레지스터를 만지고 FTL · GC · 호스트 처리 쪽은 명령 기술자를 채널 큐에 넣는 구조가 바로 게이트키퍼다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="18" font-size="13" font-weight="bold">같은 UART를 두 가지로 공유 (예제 10)</text>
<text x="170" y="42" font-size="13" text-anchor="middle">뮤텍스</text>
<text x="510" y="42" font-size="13" text-anchor="middle">게이트키퍼</text>
<line x1="340" y1="30" x2="340" y2="250" stroke="#888" stroke-dasharray="3,4"/>
<rect x="20" y="60" width="90" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="65" y="83" font-size="12" text-anchor="middle">H prio 4</text>
<rect x="20" y="140" width="90" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="65" y="163" font-size="12" text-anchor="middle">L prio 1</text>
<rect x="150" y="100" width="70" height="36" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="185" y="123" font-size="12" text-anchor="middle">mutex</text>
<rect x="250" y="100" width="70" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="285" y="123" font-size="12" text-anchor="middle">UART</text>
<line x1="110" y1="80" x2="150" y2="110" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="110" y1="158" x2="150" y2="128" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="220" y1="118" x2="248" y2="118" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="170" y="210" font-size="12" text-anchor="middle">H는 L의 40자 출력이 끝나야 쓴다</text>
<text x="170" y="230" font-size="12" text-anchor="middle">log() 평균 1836 µs, 최대 3877 µs</text>
<rect x="360" y="60" width="90" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="405" y="83" font-size="12" text-anchor="middle">H prio 4</text>
<rect x="360" y="140" width="90" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="405" y="163" font-size="12" text-anchor="middle">L prio 1</text>
<rect x="480" y="100" width="80" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<line x1="500" y1="100" x2="500" y2="136" stroke="#888"/>
<line x1="520" y1="100" x2="520" y2="136" stroke="#888"/>
<line x1="540" y1="100" x2="540" y2="136" stroke="#888"/>
<text x="520" y="152" font-size="12" text-anchor="middle">큐 8칸</text>
<rect x="580" y="95" width="90" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="625" y="115" font-size="12" text-anchor="middle">게이트키퍼</text>
<text x="625" y="132" font-size="12" text-anchor="middle">prio 1</text>
<rect x="590" y="170" width="70" height="32" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="625" y="191" font-size="12" text-anchor="middle">UART</text>
<line x1="450" y1="80" x2="480" y2="108" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="450" y1="158" x2="480" y2="128" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="560" y1="118" x2="578" y2="118" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="625" y1="141" x2="625" y2="168" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="510" y="230" font-size="12" text-anchor="middle">H는 48 B 복사만: 평균 11 µs, 출력은 나중에</text>
</svg>
```

그림 5 — 왼쪽은 공유 자원 + 뮤텍스, 오른쪽은 게이트키퍼. 오른쪽에는 잠글 것이 없으므로 H가 L을 기다리는 경로 자체가 없다. 대신 "출력이 언제 나가나"와 "큐가 차면?"이라는 새 질문이 생긴다.

### 5.2 예제 10 — 게이트키퍼 vs 뮤텍스

무엇을 확인하는 코드인지: 115200 baud 폴링 UART(문자당 87 µs, 바쁜 대기로 흉내)를 세 태스크가 공유한다. H(우선순위 4)는 3 ms 제어 루프에서 `H_EVERY`번에 한 번 16자를 로그하고, L(1)은 10 ms마다 40자 상태 로그, M(2)은 5 ms마다 1 ms 계산하는 방해꾼이다. 뮤텍스 방식과 게이트키퍼 방식(우선순위 1, 큐 8칸, 메시지 48 B 값 복사, 보내는 쪽은 `xQueueSend(q, &m, 0)` — 절대 기다리지 않음)에서 **H의 `log()` 호출이 H를 붙잡는 시간**을 잰다. 게이트키퍼 방식에서는 H 메시지가 실제로 출력될 때까지의 end-to-end 시간과 버려진 수(drops)도 센다.

```c
/* ex5: 패턴 4 — 게이트키퍼 태스크 vs 뮤텍스. 높은 우선순위 제어 태스크가 log() 한 번에 얼마나 묶이나 */
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "d7.h"
#ifndef H_EVERY
#define H_EVERY 3                                          /* H가 몇 주기에 한 번 로그하나 */
#endif

typedef struct { uint32_t t; char s[44]; } logmsg_t;    /* 48 B, 값 복사 */
static SemaphoreHandle_t mtx;
static QueueHandle_t q;
static volatile int gk_mode, stop;
static volatile uint32_t n, sum, mx, drops, e2e_max;
static void uart_write_slow(const char *s) { while (*s++) busy_us(87); }  /* 115200 baud polled TX 흉내 */
static void log_msg(const char *s)
{
    if (gk_mode) {
        logmsg_t m; m.t = CYC(); strncpy(m.s, s, sizeof m.s - 1); m.s[sizeof m.s - 1] = 0;
        if (xQueueSend(q, &m, 0) != pdPASS) drops++;      /* 절대 기다리지 않는다 */
    } else { xSemaphoreTake(mtx, portMAX_DELAY); uart_write_slow(s); xSemaphoreGive(mtx); }
}
static void gatekeeper_fn(void *p)                        /* UART의 유일한 주인 */
{
    (void)p; logmsg_t m;
    for (;;) {
        xQueueReceive(q, &m, portMAX_DELAY);
        uart_write_slow(m.s);
        uint32_t e = CYC() - m.t; if (m.s[0] == 'H' && e > e2e_max) e2e_max = e;
    }
}
static void ctl_fn(void *p)                               /* H: 3 ms 제어 루프, prio 4 */
{
    (void)p; TickType_t last = xTaskGetTickCount(); uint32_t i = 0;
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(3));
        busy_us(200);                                     /* 제어 계산 */
        if (stop || (++i % H_EVERY) != 0) continue;       /* H_EVERY 번에 한 번 로그 */
        uint32_t t0 = CYC();
        log_msg("H att err=+0.02\n");                     /* 16 chars */
        uint32_t d = CYC() - t0; n++; sum += d; if (d > mx) mx = d;
    }
}
static void status_fn(void *p)   /* L: 10 ms마다 40자 상태 로그, prio 1 */
{ (void)p; for (;;) { if (!stop) log_msg("L bat=15.8V gps=12sat temp=41C mode=POS\n"); vTaskDelay(pdMS_TO_TICKS(10)); } }
static void mid_fn(void *p) { (void)p; for (;;) { busy_us(1000); vTaskDelay(pdMS_TO_TICKS(5)); } }  /* M, prio 2 */
static void run(int mode, const char *name)
{
    gk_mode = mode; stop = 0; n = sum = mx = drops = e2e_max = 0;
    vTaskDelay(pdMS_TO_TICKS(300)); stop = 1; vTaskDelay(pdMS_TO_TICKS(20));
    board_printf_raw("%s H_EVERY=%u: H log() n=%u avg=%u us max=%u us  drops=%u  H end-to-end max=%u us\n",
        name, H_EVERY, n, CYC2US(sum / n), CYC2US(mx), drops, CYC2US(e2e_max));
}
static void main_fn(void *p) { (void)p; run(0, "mutex     "); run(1, "gatekeeper"); board_exit(0); }
int main(void)
{
    mtx = xSemaphoreCreateMutex(); q = xQueueCreate(8, sizeof(logmsg_t));
    xTaskCreate(gatekeeper_fn, "gk", 256, NULL, 1, NULL);
    xTaskCreate(status_fn, "L", 256, NULL, 1, NULL);
    xTaskCreate(mid_fn, "M", 256, NULL, 2, NULL);
    xTaskCreate(ctl_fn, "H", 256, NULL, 4, NULL);
    xTaskCreate(main_fn, "main", 256, NULL, 6, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'for k in 3 1; do make -s -B -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D7/f5/ex5.c OUT=/tmp/rtos_D7/ex5_k$k EXTRA_CFLAGS=-DH_EVERY=$k | grep -v filename; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D7/ex5_k$k/app.elf; done'
```

```text
  14304	    100	 135440	 149844	  24954	/tmp/rtos_D7/ex5_k3/app.elf
mutex      H_EVERY=3: H log() n=33 avg=1836 us max=3877 us  drops=0  H end-to-end max=0 us
gatekeeper H_EVERY=3: H log() n=33 avg=11 us max=12 us  drops=0  H end-to-end max=5069 us
[board_exit] code=0
  14276	    100	 135440	 149816	  24938	/tmp/rtos_D7/ex5_k1/app.elf
mutex      H_EVERY=1: H log() n=99 avg=1854 us max=4488 us  drops=0  H end-to-end max=0 us
gatekeeper H_EVERY=1: H log() n=100 avg=10 us max=12 us  drops=1  H end-to-end max=21628 us
[board_exit] code=0
```

```svg
<svg viewBox="0 0 680 200" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">우선순위 4 제어 태스크의 log() 한 번이 막는 시간 (예제 5)</text>
<line x1="110.0" y1="44" x2="110.0" y2="150" stroke="#888" stroke-dasharray="2,4"/>
<text x="110.0" y="166" font-size="12" text-anchor="middle">1</text>
<line x1="202.5" y1="44" x2="202.5" y2="150" stroke="#888" stroke-dasharray="2,4"/>
<text x="202.5" y="166" font-size="12" text-anchor="middle">10</text>
<line x1="295.0" y1="44" x2="295.0" y2="150" stroke="#888" stroke-dasharray="2,4"/>
<text x="295.0" y="166" font-size="12" text-anchor="middle">100</text>
<line x1="387.5" y1="44" x2="387.5" y2="150" stroke="#888" stroke-dasharray="2,4"/>
<text x="387.5" y="166" font-size="12" text-anchor="middle">1 ms</text>
<line x1="480.0" y1="44" x2="480.0" y2="150" stroke="#888" stroke-dasharray="2,4"/>
<text x="480.0" y="166" font-size="12" text-anchor="middle">10 ms</text>
<text x="102" y="72" font-size="12" text-anchor="end">뮤텍스</text>
<rect x="110" y="54" width="301.9" height="14" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<rect x="110" y="70" width="331.9" height="14" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<text x="447.9" y="72" font-size="12">평균 1836 / 최대 3877 µs</text>
<text x="102" y="122" font-size="12" text-anchor="end">게이트키퍼</text>
<rect x="110" y="104" width="96.3" height="14" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="110" y="120" width="99.8" height="14" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
<text x="215.8" y="122" font-size="12">평균 11 / 최대 12 µs</text>
<line x1="110" y1="150" x2="480" y2="150" stroke="currentColor"/>
<text x="295.0" y="184" font-size="12" text-anchor="middle">µs (로그 축). 위 막대 = 평균, 아래 진한 막대 = 최대</text>
</svg>
```

그림 6 — 예제 10(`H_EVERY=3`)의 H `log()` 호출 시간. 뮤텍스 방식은 H가 자기 출력(16자 × 87 µs = 1.39 ms)과 L이 쥐고 있는 출력의 남은 부분을 기다린다. 게이트키퍼 방식은 48 B 복사 + 큐 연산만 한다.

출력에서 볼 것:

- **뮤텍스: 평균 1836 µs, 최대 3877 µs.** H 자신의 출력 1.39 ms에, L이 40자(3.5 ms)를 찍는 중이었다면 그 남은 부분이 더해진다. FreeRTOS 뮤텍스는 우선순위 상속(D3)을 하므로 M이 L을 밀어내는 무한 역전은 막아 주지만, **L이 쥔 임계 구역 길이만큼은 피할 수 없다.** 3 ms 제어 루프가 한 번의 로그로 4 ms를 잃으면 주기를 놓친다.
- **게이트키퍼: 평균 11 µs, 최대 12 µs, 흔들림 1 µs.** H는 이제 UART 속도와 무관하다.
- **대신 지연이 사라진 게 아니라 옮겨 갔다.** H 메시지가 실제로 UART에 나가기까지 최대 5069 µs(게이트키퍼가 우선순위 1이라 M과 H가 다 쓴 뒤에 돈다). 로그에는 괜찮지만, "지금 당장 나가야 하는 명령"(모터 정지 명령 같은)을 같은 게이트키퍼로 보내면 안 된다.
- **부하가 UART 대역폭을 넘으면 큐가 찬다.** `H_EVERY=1`(H가 3 ms마다 16자 = UART의 46%, L이 35%, M의 CPU 20%, 폴링 출력이라 UART 시간 = CPU 시간)에서는 1건이 버려졌고 end-to-end가 21.6 ms까지 늘었다. 게이트키퍼 설계에는 **큐 깊이, 넘칠 때 정책(버림 · 덮어쓰기 · 카운터), 게이트키퍼 우선순위**가 반드시 따라온다. 실칩이라면 게이트키퍼가 DMA나 TX 인터럽트로 UART를 돌려서 CPU를 놓는 것이 정석이다(J4).

### 5.3 게이트키퍼 vs 뮤텍스 — 고르는 기준

| 기준 | 뮤텍스(D2) | 게이트키퍼 |
|---|---|---|
| 호출자가 붙잡히는 시간 | 다른 사용자의 임계 구역 길이까지 | 큐 연산 시간(µs) |
| 우선순위 역전 · 데드락 | 상속으로 완화, 락 순서 규칙 필요 | 구조적으로 없음 |
| 결과를 바로 알아야 하나 | 호출이 끝나면 결과가 있다 | 응답 큐 · 알림이 따로 필요 |
| ISR에서 요청 | 불가(ISR은 뮤텍스 금지) | `xQueueSendFromISR`로 가능 |
| RAM | 뮤텍스 하나 | 태스크 하나(스택) + 큐 |
| 맞는 자원 | 짧게 쓰는 레지스터 묶음, 메모리 구조체 | 느린 장치(UART, 플래시, 디스플레이, 모뎀) |

---

## 6. 패턴 비교 — 무엇을 언제

### 6.1 예제 11 — 측정 숫자를 차트로

무엇을 확인하는 코드인지: 예제 2 · 3 · 7 · 8 · 10의 실제 출력 숫자를 Python으로 SVG 좌표로 바꾼다(이 노트의 측정 차트 네 장이 전부 이 스크립트에서 나왔다). 앞부분 13줄만 보인다(70줄 규칙 — 나머지는 같은 모양의 반복).

```python
# D7 측정 차트 생성: 예제 2·3·5·z2·z3의 실제 출력 숫자 → SVG
import math, sys
OUT = sys.argv[1]
B, O, G, R, GR = "#4a7bd0", "#e08a3c", "#3f9a6b", "#d0564a", "#888"
def save(name, s):
    open(f"{OUT}/{name}.svg", "w").write(s); print(name, "ok", len(s), "bytes")

# --- 차트 A: 핸들러가 앱보다 높을 때 (min/avg/max, us, 선형 0~35) ---
rows = [("FR notify→task", 8.1, 9.8, 15.1, B), ("FR binsem→task", 9.0, 10.8, 16.1, B),
        ("FR pend→daemon", 13.6, 13.6, 13.6, O), ("Z sem→thread", 10.5, 16.1, 30.4, G),
        ("Z sysworkq", 9.7, 13.2, 15.7, G), ("Z my wq(2)", 13.7, 19.2, 23.0, G)]
x0, x1, vmax, y0, rh = 150, 620, 35.0, 50, 36
X = lambda v: x0 + (x1 - x0) * v / vmax
# ... (축 · 막대 · whisker를 문자열로 만들어 chartA.svg로 저장. 차트 B · C · D도 같은 방식, 로그 축은 log10. 전체 101줄)
```

```sh
python3 ~/.rtos_scratch/D7/py/charts.py ~/.rtos_scratch/D7/svg
```

```text
chartA ok 4354 bytes
chartB ok 4078 bytes
chartC ok 4377 bytes
chartD ok 1777 bytes
```

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">ISR 진입 → 일 시작 (핸들러 &gt; 바쁜 앱), 막대=평균, 선=min~max</text>
<line x1="150.0" y1="42" x2="150.0" y2="266" stroke="#888" stroke-dasharray="2,4"/>
<text x="150.0" y="282" font-size="12" text-anchor="middle">0</text>
<line x1="217.1" y1="42" x2="217.1" y2="266" stroke="#888" stroke-dasharray="2,4"/>
<text x="217.1" y="282" font-size="12" text-anchor="middle">5</text>
<line x1="284.3" y1="42" x2="284.3" y2="266" stroke="#888" stroke-dasharray="2,4"/>
<text x="284.3" y="282" font-size="12" text-anchor="middle">10</text>
<line x1="351.4" y1="42" x2="351.4" y2="266" stroke="#888" stroke-dasharray="2,4"/>
<text x="351.4" y="282" font-size="12" text-anchor="middle">15</text>
<line x1="418.6" y1="42" x2="418.6" y2="266" stroke="#888" stroke-dasharray="2,4"/>
<text x="418.6" y="282" font-size="12" text-anchor="middle">20</text>
<line x1="485.7" y1="42" x2="485.7" y2="266" stroke="#888" stroke-dasharray="2,4"/>
<text x="485.7" y="282" font-size="12" text-anchor="middle">25</text>
<line x1="552.9" y1="42" x2="552.9" y2="266" stroke="#888" stroke-dasharray="2,4"/>
<text x="552.9" y="282" font-size="12" text-anchor="middle">30</text>
<line x1="620.0" y1="42" x2="620.0" y2="266" stroke="#888" stroke-dasharray="2,4"/>
<text x="620.0" y="282" font-size="12" text-anchor="middle">35</text>
<text x="142" y="67" font-size="12" text-anchor="end">FR notify→task</text>
<rect x="150" y="55" width="131.6" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<line x1="258.8" y1="64" x2="352.8" y2="64" stroke="currentColor" stroke-width="2"/>
<line x1="258.8" y1="58" x2="258.8" y2="70" stroke="currentColor"/>
<line x1="352.8" y1="58" x2="352.8" y2="70" stroke="currentColor"/>
<text x="358.8" y="68" font-size="12">9.8 / 15.1</text>
<text x="142" y="103" font-size="12" text-anchor="end">FR binsem→task</text>
<rect x="150" y="91" width="145.0" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<line x1="270.9" y1="100" x2="366.2" y2="100" stroke="currentColor" stroke-width="2"/>
<line x1="270.9" y1="94" x2="270.9" y2="106" stroke="currentColor"/>
<line x1="366.2" y1="94" x2="366.2" y2="106" stroke="currentColor"/>
<text x="372.2" y="104" font-size="12">10.8 / 16.1</text>
<text x="142" y="139" font-size="12" text-anchor="end">FR pend→daemon</text>
<rect x="150" y="127" width="182.6" height="18" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<line x1="332.6" y1="136" x2="332.6" y2="136" stroke="currentColor" stroke-width="2"/>
<line x1="332.6" y1="130" x2="332.6" y2="142" stroke="currentColor"/>
<line x1="332.6" y1="130" x2="332.6" y2="142" stroke="currentColor"/>
<text x="338.6" y="140" font-size="12">13.6 / 13.6</text>
<text x="142" y="175" font-size="12" text-anchor="end">Z sem→thread</text>
<rect x="150" y="163" width="216.2" height="18" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<line x1="291.0" y1="172" x2="558.2" y2="172" stroke="currentColor" stroke-width="2"/>
<line x1="291.0" y1="166" x2="291.0" y2="178" stroke="currentColor"/>
<line x1="558.2" y1="166" x2="558.2" y2="178" stroke="currentColor"/>
<text x="564.2" y="176" font-size="12">16.1 / 30.4</text>
<text x="142" y="211" font-size="12" text-anchor="end">Z sysworkq</text>
<rect x="150" y="199" width="177.3" height="18" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<line x1="280.3" y1="208" x2="360.8" y2="208" stroke="currentColor" stroke-width="2"/>
<line x1="280.3" y1="202" x2="280.3" y2="214" stroke="currentColor"/>
<line x1="360.8" y1="202" x2="360.8" y2="214" stroke="currentColor"/>
<text x="366.8" y="212" font-size="12">13.2 / 15.7</text>
<text x="142" y="247" font-size="12" text-anchor="end">Z my wq(2)</text>
<rect x="150" y="235" width="257.8" height="18" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<line x1="334.0" y1="244" x2="458.9" y2="244" stroke="currentColor" stroke-width="2"/>
<line x1="334.0" y1="238" x2="334.0" y2="250" stroke="currentColor"/>
<line x1="458.9" y1="238" x2="458.9" y2="250" stroke="currentColor"/>
<text x="464.9" y="248" font-size="12">19.2 / 23.0</text>
<line x1="150" y1="266" x2="620" y2="266" stroke="currentColor"/>
<text x="385.0" y="300" font-size="12" text-anchor="middle">µs (QEMU mps2-an385, icount 32 ns/명령) — 숫자는 평균 / 최대</text>
</svg>
```

그림 7 — 위임 수단 자체의 비용(실행자가 바쁜 앱보다 높을 때). 전부 8~31 µs 안이다. FreeRTOS notify가 가장 싸고, daemon 위임은 큐 복사 때문에 5 µs 더 들지만 흔들림이 없다. Zephyr 세 방식은 서로 비슷하다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13" font-weight="bold">ISR → 일 시작 지연: 패턴보다 우선순위가 지배한다 (로그 축)</text>
<rect x="420" y="28" width="12" height="10" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/><text x="437" y="37" font-size="12">평균</text>
<rect x="480" y="28" width="12" height="10" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/><text x="497" y="37" font-size="12">최대</text>
<line x1="170.0" y1="44" x2="170.0" y2="306" stroke="#888" stroke-dasharray="2,4"/>
<text x="170.0" y="322" font-size="12" text-anchor="middle">1</text>
<line x1="277.5" y1="44" x2="277.5" y2="306" stroke="#888" stroke-dasharray="2,4"/>
<text x="277.5" y="322" font-size="12" text-anchor="middle">10</text>
<line x1="385.0" y1="44" x2="385.0" y2="306" stroke="#888" stroke-dasharray="2,4"/>
<text x="385.0" y="322" font-size="12" text-anchor="middle">100</text>
<line x1="492.5" y1="44" x2="492.5" y2="306" stroke="#888" stroke-dasharray="2,4"/>
<text x="492.5" y="322" font-size="12" text-anchor="middle">1 ms</text>
<line x1="600.0" y1="44" x2="600.0" y2="306" stroke="#888" stroke-dasharray="2,4"/>
<text x="600.0" y="322" font-size="12" text-anchor="middle">10 ms</text>
<text x="162" y="68" font-size="12" text-anchor="end">FR notify, h&gt;app</text>
<rect x="170" y="54" width="106.6" height="11" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="170" y="66" width="126.7" height="11" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="301.7" y="76" font-size="12">15.1</text>
<text x="162" y="100" font-size="12" text-anchor="end">FR notify, h=app</text>
<rect x="170" y="86" width="309.5" height="11" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="170" y="98" width="322.4" height="11" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="497.4" y="108" font-size="12">998.3</text>
<text x="162" y="132" font-size="12" text-anchor="end">FR notify, h&lt;app</text>
<rect x="170" y="118" width="343.0" height="11" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="170" y="130" width="374.7" height="11" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="549.7" y="140" font-size="12">3060.7</text>
<text x="162" y="164" font-size="12" text-anchor="end">FR pend, d=7</text>
<rect x="170" y="150" width="121.9" height="11" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="170" y="162" width="121.9" height="11" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="296.9" y="172" font-size="12">13.6</text>
<text x="162" y="196" font-size="12" text-anchor="end">FR pend, d=7+느린cb</text>
<rect x="170" y="182" width="297.5" height="11" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="170" y="194" width="353.5" height="11" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="528.5" y="204" font-size="12">1943</text>
<text x="162" y="228" font-size="12" text-anchor="end">FR pend, d=1&lt;app</text>
<rect x="170" y="214" width="339.3" height="11" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="170" y="226" width="372.6" height="11" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="547.6" y="236" font-size="12">2922.4</text>
<text x="162" y="260" font-size="12" text-anchor="end">Z my wq 2&gt;app</text>
<rect x="170" y="246" width="138.0" height="11" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="170" y="258" width="146.4" height="11" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="321.4" y="268" font-size="12">23</text>
<text x="162" y="292" font-size="12" text-anchor="end">Z my wq 8&lt;app</text>
<rect x="170" y="278" width="326.4" height="11" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="170" y="290" width="374.7" height="11" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="549.7" y="300" font-size="12">3059.4</text>
<line x1="170" y1="306" x2="600" y2="306" stroke="currentColor"/>
<text x="385.0" y="340" font-size="12" text-anchor="middle">µs, 앱 = 3 ms 계산 + 1 tick 휴식, IRQ 7 ms 주기 (예제 2 · 3 · z2)</text>
</svg>
```

그림 8 — 같은 측정을 로그 축에 놓고 "실행자 우선순위"를 바꾼 경우를 더했다. 패턴 사이의 차이는 µs 단위지만, 실행자를 앱과 같게(1 tick까지) · 낮게(앱의 계산 길이까지) 두거나 공유 실행자에 느린 콜백이 끼면 ms 단위로 뛴다. **위임된 일의 지연은 패턴이 아니라 우선순위가 정한다.**

### 6.2 비교표

| 항목 | ① 전용 태스크 | ② daemon 위임 (FreeRTOS) | ③ workqueue (Zephyr) | ④ 게이트키퍼 |
|---|---|---|---|---|
| 실행 문맥 | 자기 태스크 | timer daemon 태스크 | 큐 스레드 (sysworkq 또는 전용) | 자원 소유 태스크 |
| 우선순위 | 원인마다 따로 | daemon 하나(`configTIMER_TASK_PRIORITY`) | 큐마다 (sysworkq는 -1 협력형) | 게이트키퍼 하나 |
| ISR → 시작 (측정, 실행자 > 앱) | 8.1 µs (notify) | 13.6 µs | 9.7~13.7 µs | 해당 없음 (요청 11 µs) |
| 추가 RAM | 스택 + TCB (예: 1 KB+) | 16 B × 큐 길이 | 16 B / 항목 (+전용 큐면 192 B + 스택) | 스택 + 큐 |
| 같은 사건이 겹치면 | notify는 개수 보존, 세마포어는 합쳐짐 | 메시지마다 보존, 큐 차면 실패 | 합쳐짐(submit → 0) | 큐 차면 실패 |
| 블록 가능 | 예 | 아니오 (타이머 전체가 멈춤) | 전용 큐면 예, sysworkq는 아니오 | 예 (그게 존재 이유) |
| 대표 용도 | IMU, 전류 센서, 통신 RX | 드문 fault · 상태 변화 | 센서 폴링, 드라이버 뒷일, debounce | 로그, 플래시, 디스플레이 |

### 6.3 결정 흐름

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="18" font-size="13" font-weight="bold">ISR 뒤의 일을 어디서 할까 — 위에서부터 묻는다</text>
<rect x="20" y="35" width="330" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="185" y="60" font-size="12" text-anchor="middle">ISR 안에서 끝낼 수 있나? (수 µs, 블록 없음)</text>
<rect x="400" y="35" width="270" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="535" y="60" font-size="12" text-anchor="middle">ISR에서 처리하고 끝</text>
<rect x="20" y="95" width="330" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="185" y="120" font-size="12" text-anchor="middle">마감이 빡빡한가? (제어 루프, IMU 후처리)</text>
<rect x="400" y="95" width="270" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="535" y="120" font-size="12" text-anchor="middle">① 전용 태스크 + notify (높은 우선순위)</text>
<rect x="20" y="155" width="330" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="185" y="180" font-size="12" text-anchor="middle">드물고 짧으며 블록하지 않나? (fault 보고)</text>
<rect x="400" y="155" width="270" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="535" y="180" font-size="12" text-anchor="middle">② PendFunctionCall / ③ sysworkq</text>
<rect x="20" y="215" width="330" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="185" y="240" font-size="12" text-anchor="middle">블록하거나 느린 I/O인가? (I2C, 플래시)</text>
<rect x="400" y="215" width="270" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="535" y="240" font-size="12" text-anchor="middle">③ 전용 workqueue / 전용 태스크 (낮게)</text>
<rect x="20" y="275" width="330" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="185" y="300" font-size="12" text-anchor="middle">여러 태스크가 느린 자원 하나를 쓰나?</text>
<rect x="400" y="275" width="270" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="535" y="300" font-size="12" text-anchor="middle">④ 게이트키퍼 태스크 + 요청 큐</text>
<line x1="350" y1="55" x2="398" y2="55" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="350" y1="115" x2="398" y2="115" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="350" y1="175" x2="398" y2="175" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="350" y1="235" x2="398" y2="235" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="350" y1="295" x2="398" y2="295" stroke="currentColor" marker-end="url(#ah6)"/>
<text x="374" y="50" font-size="12" text-anchor="middle">예</text>
<text x="374" y="110" font-size="12" text-anchor="middle">예</text>
<text x="374" y="170" font-size="12" text-anchor="middle">예</text>
<text x="374" y="230" font-size="12" text-anchor="middle">예</text>
<text x="374" y="290" font-size="12" text-anchor="middle">예</text>
<line x1="185" y1="75" x2="185" y2="93" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="185" y1="135" x2="185" y2="153" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="185" y1="195" x2="185" y2="213" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="185" y1="255" x2="185" y2="273" stroke="currentColor" marker-end="url(#ah6)"/>
<text x="192" y="88" font-size="12">아니오</text>
<text x="192" y="148" font-size="12">아니오</text>
<text x="192" y="208" font-size="12">아니오</text>
<text x="192" y="268" font-size="12">아니오</text>
<text x="20" y="333" font-size="12">어느 칸이든: 실행자 우선순위를 "그 일을 방해할 수 있는 모든 태스크"와 비교해서 정한다 (그림 8)</text>
</svg>
```

그림 9 — 위임 수단 고르기. 첫 질문이 "ISR에서 끝낼 수 있나"인 이유: 수 µs짜리 일을 위임하면 위임 비용(8~14 µs)이 일보다 크다.

---

## 7. RTOS 커널 안에서는 무슨 일이 일어나나

### 7.1 FreeRTOS — `xTimerPendFunctionCallFromISR`에서 함수 실행까지

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="18" font-size="13" font-weight="bold">FreeRTOS V11.3.2 timers.c — pend된 함수가 실행되는 길</text>
<rect x="20" y="35" width="320" height="46" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="180" y="54" font-size="12" text-anchor="middle">ISR 문맥</text>
<text x="180" y="72" font-size="12" text-anchor="middle">xTimerPendFunctionCallFromISR(f, p1, p2)</text>
<rect x="20" y="100" width="320" height="46" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="180" y="119" font-size="12" text-anchor="middle">xMessageID = -2 (FROM_ISR), 인자 채움</text>
<text x="180" y="137" font-size="12" text-anchor="middle">xQueueSendFromISR(xTimerQueue, &amp;msg)</text>
<rect x="20" y="170" width="320" height="70" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="180" y="189" font-size="12" text-anchor="middle">xTimerQueue (길이 10, 항목 16 B)</text>
<rect x="40" y="198" width="26" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="70" y="198" width="26" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="100" y="198" width="26" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="130" y="198" width="26" height="20" fill="none" stroke="#4a7bd0"/>
<rect x="160" y="198" width="26" height="20" fill="none" stroke="#4a7bd0"/>
<rect x="190" y="198" width="26" height="20" fill="none" stroke="#4a7bd0"/>
<rect x="220" y="198" width="26" height="20" fill="none" stroke="#4a7bd0"/>
<rect x="250" y="198" width="26" height="20" fill="none" stroke="#4a7bd0"/>
<rect x="280" y="198" width="26" height="20" fill="none" stroke="#4a7bd0"/>
<rect x="310" y="198" width="22" height="20" fill="none" stroke="#4a7bd0"/>
<text x="180" y="234" font-size="12" text-anchor="middle">타이머 명령과 함께 쓴다. 가득 차면 pdFAIL</text>
<line x1="180" y1="81" x2="180" y2="98" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="180" y1="146" x2="180" y2="168" stroke="currentColor" marker-end="url(#ah7)"/>
<rect x="380" y="35" width="280" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="520" y="60" font-size="12" text-anchor="middle">prvTimerTask (daemon) 루프</text>
<rect x="380" y="90" width="280" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="520" y="109" font-size="12" text-anchor="middle">1. prvGetNextExpireTime</text>
<text x="520" y="127" font-size="12" text-anchor="middle">2. prvProcessTimerOrBlockTask</text>
<rect x="380" y="150" width="280" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="520" y="169" font-size="12" text-anchor="middle">만료된 타이머 콜백을 먼저 실행,</text>
<text x="520" y="187" font-size="12" text-anchor="middle">없으면 큐에서 블록</text>
<rect x="380" y="210" width="280" height="64" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="520" y="229" font-size="12" text-anchor="middle">3. prvProcessReceivedCommands</text>
<text x="520" y="247" font-size="12" text-anchor="middle">xMessageID &lt; 0 이면</text>
<text x="520" y="265" font-size="12" text-anchor="middle">pxCallbackFunction(p1, p2)</text>
<line x1="520" y1="75" x2="520" y2="88" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="520" y1="136" x2="520" y2="148" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="520" y1="196" x2="520" y2="208" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="340" y1="215" x2="378" y2="240" stroke="currentColor" marker-end="url(#ah7)"/>
<path d="M660,242 L672,242 L672,55 L662,55" fill="none" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="20" y="300" font-size="12">큐가 빌 때까지 반복 (xQueueReceive, tmrNO_DELAY). 그 다음 다시 1로 — 만료 타이머가 늘 먼저다.</text>
</svg>
```

그림 10 — ISR 쪽(왼쪽)은 메시지를 큐에 넣고 끝난다. daemon 쪽(오른쪽)은 루프 한 바퀴마다 만료된 타이머를 먼저 처리한 뒤 명령 큐를 비운다. 그래서 느린 타이머 콜백이 pend된 함수를 기다리게 만든다(예제 3의 1943 µs).

ISR 쪽 코드 전체(`timers.c` 1238~1261행):

```c
        BaseType_t xTimerPendFunctionCallFromISR( PendedFunction_t xFunctionToPend,
                                                  void * pvParameter1,
                                                  uint32_t ulParameter2,
                                                  BaseType_t * pxHigherPriorityTaskWoken )
        {
            DaemonTaskMessage_t xMessage;
            BaseType_t xReturn;

            traceENTER_xTimerPendFunctionCallFromISR( xFunctionToPend, pvParameter1, ulParameter2, pxHigherPriorityTaskWoken );

            /* Complete the message with the function parameters and post it to the
             * daemon task. */
            xMessage.xMessageID = tmrCOMMAND_EXECUTE_CALLBACK_FROM_ISR;
            xMessage.u.xCallbackParameters.pxCallbackFunction = xFunctionToPend;
            xMessage.u.xCallbackParameters.pvParameter1 = pvParameter1;
            xMessage.u.xCallbackParameters.ulParameter2 = ulParameter2;

            xReturn = xQueueSendFromISR( xTimerQueue, &xMessage, pxHigherPriorityTaskWoken );

            tracePEND_FUNC_CALL_FROM_ISR( xFunctionToPend, pvParameter1, ulParameter2, xReturn );
            traceRETURN_xTimerPendFunctionCallFromISR( xReturn );

            return xReturn;
        }
```

명령 ID는 `include/timers.h`에 음수로 정의돼 있다 — `tmrCOMMAND_EXECUTE_CALLBACK_FROM_ISR` = -2, `tmrCOMMAND_EXECUTE_CALLBACK` = -1. 타이머 명령(start, stop, reset …)은 0 이상이다. daemon 루프(`prvTimerTask`, 746행)는 세 줄이다.

```c
        for( ; configCONTROL_INFINITE_LOOP(); )
        {
            /* Query the timers list to see if it contains any timers, and if so,
             * obtain the time at which the next timer will expire. */
            xNextExpireTime = prvGetNextExpireTime( &xListWasEmpty );

            /* If a timer has expired, process it.  Otherwise, block this task
             * until either a timer does expire, or a command is received. */
            prvProcessTimerOrBlockTask( xNextExpireTime, xListWasEmpty );

            /* Empty the command queue. */
            prvProcessReceivedCommands();
        }
```

그리고 `prvProcessReceivedCommands`(936행)가 음수 ID를 함수 호출로 해석하는 부분:

```c
        while( xQueueReceive( xTimerQueue, &xMessage, tmrNO_DELAY ) != pdFAIL )
        {
            #if ( INCLUDE_xTimerPendFunctionCall == 1 )
            {
                /* Negative commands are pended function calls rather than timer
                 * commands. */
                if( xMessage.xMessageID < ( BaseType_t ) 0 )
                {
                    const CallbackParameters_t * const pxCallback = &( xMessage.u.xCallbackParameters );

                    /* The timer uses the xCallbackParameters member to request a
                     * callback be executed.  Check the callback is not NULL. */
                    configASSERT( pxCallback );

                    /* Call the function. */
                    pxCallback->pxCallbackFunction( pxCallback->pvParameter1, pxCallback->ulParameter2 );
                }
```

읽고 나면 보이는 것:

- **ISR 쪽 비용은 16 B 메시지 하나를 큐에 복사하는 것**이 전부다. 큐가 차면 `xQueueSendFromISR`가 `errQUEUE_FULL`을 돌려주고 그대로 반환된다 — 재시도도 기록도 없다(예제 4).
- **FROM_ISR(-2)와 태스크용(-1) 명령은 daemon 쪽에서 구분하지 않는다.** 둘 다 "음수면 함수 호출"이다. ID를 나눈 것은 trace 용도로 보인다(실행 경로에서는 차이가 없다).
- daemon은 `prvProcessTimerOrBlockTask` 안에서 `vQueueWaitForMessageRestricted`로 "다음 만료 시각 또는 메시지 도착" 중 먼저 오는 것까지 블록한다. 그래서 ISR이 메시지를 넣으면 daemon이 깨어나고, `xHigherPriorityTaskWoken`이 세워진다(daemon이 지금 태스크보다 높을 때만 — §2.3과 같은 `>` 규칙).
- 큐 생성은 `prvCheckForValidListAndQueue`(1135~1141행) — static 할당이 켜져 있으면 `ucStaticTimerQueueStorage[configTIMER_QUEUE_LENGTH × sizeof(DaemonTaskMessage_t)]`를 bss에 잡는다(예제 5의 0xa0). daemon 태스크 자체는 `vTaskStartScheduler`가 `xTimerCreateTimerTask()`를 불러 만든다(`tasks.c` 3762행).

### 7.2 Zephyr — `kernel/work.c`의 제출 · 실행 · 지연 경로

**제출**: `k_work_submit` → `k_work_submit_to_queue` → `z_work_submit_to_queue`(스핀락) → `submit_to_queue_locked`. 반환값 0/1/2가 정해지는 곳이 여기다(320행).

```c
static int submit_to_queue_locked(struct k_work *work,
				  struct k_work_q **queuep)
{
	int ret = 0;

	if (flag_test(&work->flags, K_WORK_CANCELING_BIT)) {
		/* Disallowed */
		ret = -EBUSY;
	} else if (!flag_test(&work->flags, K_WORK_QUEUED_BIT)) {
		/* Not currently queued */
		ret = 1;

		/* If no queue specified resubmit to last queue.
		 */
		if (*queuep == NULL) {
			*queuep = work->queue;
		}

		/* If the work is currently running we have to use the
		 * queue it's running on to prevent handler
		 * re-entrancy.
		 */
		if (flag_test(&work->flags, K_WORK_RUNNING_BIT)) {
			__ASSERT_NO_MSG(work->queue != NULL);
			*queuep = work->queue;
			ret = 2;
		}

		int rc = queue_submit_locked(*queuep, work);

		if (rc < 0) {
			ret = rc;
		} else {
			flag_set(&work->flags, K_WORK_QUEUED_BIT);
			work->queue = *queuep;
		}
	} else {
		/* Already queued, do nothing. */
	}
```

- QUEUED가 이미 서 있으면 **아무것도 하지 않고 0**. 이것이 예제 6의 `1, 0`과 예제 8의 "유실 12"다.
- RUNNING이면 요청한 큐를 무시하고 **실행 중인 큐로** 보낸다(재진입 방지) → 2.
- `queue_submit_locked`(260행)는 큐 상태를 본다: 시작 안 됨 → `-ENODEV`, drain 중인데 자기 큐 스레드가 아닌 곳에서 제출 → `-EBUSY`, plug 상태 → `-EBUSY`. 통과하면 `sys_slist_append(&queue->pending, &work->node)` 후 `notify_queue_locked` → `z_sched_wake(&queue->notifyq, ...)`로 큐 스레드를 깨운다. 리스트 연결이므로 **복사가 없다**.
- 그 뒤 `k_work_submit_to_queue`가 `z_reschedule_unlocked()`를 부른다. ISR 안이면 `resched()`가 `!arch_is_in_isr()` 검사에서 false라 스위치하지 않고, 결정은 ISR 출구(`z_arm_exc_exit`의 `ready_q.cache` 검사 → PendSV)로 미뤄진다(FreeRTOS의 `portYIELD_FROM_ISR`를 사람이 부를 필요가 없는 이유). 스레드에서 불렀고 큐가 더 높으면 **그 자리에서 스위치**한다(예제 8의 flush 0).

**실행**: 큐 스레드 본체 `work_queue_main`(674행)의 핵심.

```c
		node = sys_slist_get(&queue->pending);
		if (node != NULL) {
			/* Mark that there's some work active that's
			 * not on the pending list.
			 */
			flag_set(&queue->flags, K_WORK_QUEUE_BUSY_BIT);
			work = CONTAINER_OF(node, struct k_work, node);
			flag_set(&work->flags, K_WORK_RUNNING_BIT);
			flag_clear(&work->flags, K_WORK_QUEUED_BIT);
			...
		k_spin_unlock(&lock, key);

		__ASSERT_NO_MSG(handler != NULL);
		handler(work);
		...
		flag_clear(&work->flags, K_WORK_RUNNING_BIT);
		if (flag_test(&work->flags, K_WORK_FLUSHING_BIT)) {
			finalize_flush_locked(work);
		}
		if (flag_test(&work->flags, K_WORK_CANCELING_BIT)) {
			finalize_cancel_locked(work);
		}

		flag_clear(&queue->flags, K_WORK_QUEUE_BUSY_BIT);
		yield = (!flag_test(&queue->flags, K_WORK_QUEUE_NO_YIELD_BIT) &&
			 !sys_slist_is_empty(&queue->pending));
		k_spin_unlock(&lock, key);

		/* Optionally yield to prevent the work queue from
		 * starving other threads.
		 */
		if (yield) {
			k_yield();
		}
```

(코드 중간의 `...`은 이 노트에서 줄인 부분이다.)

- RUNNING을 세우고 QUEUED를 지운 **뒤에** 락을 풀고 핸들러를 부른다. 그래서 핸들러 실행 중 들어온 제출은 "새 사건"으로 인정된다(2) — 처리 시작 이후의 사건은 잃지 않는다. 잃는 것은 **대기 중**에 겹친 사건이다.
- 핸들러는 락 밖에서, 큐 스레드의 우선순위로 돈다. 핸들러가 블록하면 이 루프 전체가 멈춘다(예제 8).
- 항목 사이 `k_yield()`는 **같거나 더 높은 우선순위**의 준비된 스레드에게만 기회를 준다. 협력형 -1의 sysworkq가 선점형 스레드(0 이상)에게 CPU를 넘기는 장치가 아니다 — 리스트가 비어 큐 스레드가 `z_sched_wait`로 잠들어야 비로소 선점형 스레드가 돈다.
- `CONFIG_WORKQUEUE_WORK_TIMEOUT`이 켜져 있으면 핸들러 시작 전에 `work_timeout_start_locked`로 타이머를 걸고, 시간이 넘으면 `work_timeout_handler`(606행)가 `LOG_ERR("queue %p%s%s blocked by work %p with handler %p", ...)`를 찍고 **`k_thread_abort(queue->thread_id)`** 한다. sysworkq는 `essential`이므로 이것은 곧 시스템 fatal error다 — "블로킹 항목을 조용히 견디는" 장치가 아니라 "개발 중에 크게 터뜨리는" 장치다.

**지연(delayable)**: `k_work_schedule_for_queue`(1155행)는 `(work_busy_get_locked(work) & ~K_WORK_RUNNING) == 0U`일 때만 예약한다 — 즉 유휴이거나 실행 중일 때만. 이미 DELAYED나 QUEUED면 0을 돌려주고 마감을 건드리지 않는다(예제 6의 "마감 유지"). `k_work_reschedule_for_queue`(1190행)는 먼저 `unschedule_locked`로 timeout을 지운 뒤 다시 예약한다(마감 교체). 예약은 `schedule_for_queue_locked`가 DELAYED를 세우고 `z_add_timeout(&dwork->timeout, work_timeout, delay)`를 거는 것이고, 만료 시 **timer ISR 문맥**에서 `work_timeout`(999행)이 DELAYED를 지우고 `submit_to_queue_locked`를 부른다. 그 함수의 주석이 솔직하다.

```c
	/* If the work is still marked delayed (should be) then clear that
	 * state and submit it to the queue.  If successful the queue will be
	 * notified of new work at the next reschedule point.
	 *
	 * If not successful there is no notification that the work has been
	 * abandoned.  Sorry.
	 */
```

만료 시점에 큐가 drain/plug 상태라 제출이 거부되면 **그 일은 조용히 버려진다.** delayable을 쓰는 코드에서 `k_work_queue_drain(q, true)`(plug)를 쓸 때 기억할 줄이다.

**sysworkq 시작**: `system_work_q.c`의 `SYS_INIT(k_sys_work_q_init, POST_KERNEL, ...)`(§4.3). `k_work_queue_start`(845행)는 큐 플래그를 STARTED로 세운 뒤 `k_thread_create(..., work_queue_main, queue, ..., prio, 0, K_FOREVER)`로 스레드를 만들고 `k_thread_start`한다 — 스레드가 실제로 돌기 전이라도 플래그가 서 있으므로 그 사이의 제출은 받아 둔다(주석: "It hasn't actually been started yet, but all the state is in place so we can submit things").

### 7.3 Linux와 한 표로

| 역할 | Linux (EL F6 · F7) | FreeRTOS | Zephyr |
|---|---|---|---|
| 하드웨어 직후 최소 처리 | top half (hardirq) | ISR + FromISR API | ISR + isr-ok API |
| 원인별 전용 실행자 | threaded IRQ (SCHED_FIFO 50) | 패턴 1: 태스크 + notify | 스레드 + `k_sem` |
| 공용 · 고우선 실행자 | softirq · tasklet | 패턴 2: timer daemon | sysworkq (협력형 -1) |
| 블록 가능한 공용 실행자 | workqueue (kworker) | 직접 만든 낮은 우선순위 태스크 + 큐 | 전용 `k_work_q` |
| mask until done | `IRQF_ONESHOT` | 수동 `NVIC_DisableIRQ` / `EnableIRQ` | 수동 `irq_disable` / `irq_enable` |
| 깨우기 → 실행 (측정) | p50 116 µs, p99 2.98 ms (VM, EL F6) | 8.1 µs (notify), 13.6 µs (daemon) | 9.7~13.7 µs |

---

## 8. 실무에서 다시 보기

### 8.1 드론 비행 제어기 — 한 펌웨어에 네 패턴이 다 있다

예를 들어 STM32H7급 드론 FC라면 이렇게 나뉜다.

- **IMU data-ready → estimator (패턴 1)**: IMU INT(EXTI) ISR은 SPI DMA로 FIFO 읽기를 시작하고 타임스탬프를 찍는다. DMA 완료 ISR이 estimator 태스크에 `vTaskNotifyGiveFromISR`. estimator는 앱 태스크 중 **가장 높은 우선순위**(rate loop 바로 아래 또는 같은 태스크)다. 예제 2에서 본 것처럼 같은 우선순위에 다른 태스크가 있으면 1 tick 지연이 생긴다. 반환값(누적 notify 수)이 1보다 크면 샘플을 놓친 것이므로 카운터를 올린다.
- **바로미터 I2C 읽기 (패턴 3 또는 1)**: 50 Hz, 블록하는 I2C 트랜잭션. Zephyr라면 전용 workqueue(우선순위는 estimator보다 낮게)에 `k_work_delayable`로 20 ms 주기 폴링. **sysworkq에 넣지 않는다** — I2C가 NACK로 재시도하면 수 ms 블록하고, 예제 8처럼 sysworkq의 다른 일(드라이버 뒷일 포함)을 막는다. FreeRTOS라면 낮은 우선순위 센서 태스크.
- **블랙박스 로깅 (패턴 4)**: SPI 플래시나 SD 카드는 게이트키퍼 하나가 소유한다. 모든 태스크는 고정 크기 레코드를 큐에 `timeout 0`으로 넣고, 넘치면 drop 카운터를 올린다(예제 10). 로그 손실은 허용, 제어 루프 지연은 불허 — 이 정책을 큐 send timeout 0이 코드로 표현한다.
- **텔레메트리 · 링크 상태 변화 (패턴 2)**: 무선 링크 손실 같은 드문 사건은 ISR에서 상태만 바꾸고 보고는 daemon 또는 sysworkq로.

### 8.2 예제 12 — 모터 컨트롤러 과전류 fault: 차단은 ISR에서, 보고는 위임

무엇을 확인하는 코드인지: 로봇 관절 모터 컨트롤러의 comparator fault IRQ(TIMER0로 흉내, 25 ms 뒤 한 번, FreeRTOS가 아는 최고 우선순위 0x20)가 **첫 줄에서 PWM 출력을 끄고**(STM32 TIM1 `BDTR.MOE` 같은 비트의 대역 변수), 원인을 지우고 자기 IRQ를 멈춘 뒤(래치), 보고 · 플래시 기록(2.5 ms)을 daemon으로 위임한다. 우선순위 5의 FOC 루프 태스크(1 kHz로 축소)의 최대 실행 간격을 함께 재고, daemon 우선순위를 7(기본)과 2(FOC보다 낮게)로 비교한다.

```c
/* ex6: 모터 컨트롤러 과전류 fault — PWM 차단은 ISR 안에서 즉시, 보고·기록은 daemon으로 위임 */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "d7.h"

static volatile uint32_t PWM_MOE = 1;             /* TIM1 BDTR.MOE 같은 "출력 enable" 비트의 대역 */
static volatile uint32_t t_fault, t_kill, foc_runs, foc_after_fault, foc_gap_max;
#ifndef D7_DPRIO
#define D7_DPRIO 7                                 /* daemon 우선순위 (기본 configTIMER_TASK_PRIORITY) */
#endif
static void report_fault(void *p1, uint32_t phase_ma)   /* daemon 태스크 문맥: 느려도 된다 */
{
    (void)p1;
    uint32_t t_rep = CYC();
    board_printf_raw("[report] overcurrent %u mA, fault->kill %u ns, fault->report %u us\n",
        phase_ma, (t_kill - t_fault) * 40u, CYC2US(t_rep - t_fault));
    busy_us(2500);                                 /* 플래시 섹터 erase+write 흉내 */
    board_printf_raw("[report] logged to flash, PWM_MOE=%u, FOC runs after fault=%u\n",
        PWM_MOE, foc_after_fault);
}
void TIMER0_IRQHandler(void)                      /* "comparator fault" IRQ */
{
    uint32_t elapsed = TIMER_RELOAD(TIMER0_BASE) - TIMER_VALUE(TIMER0_BASE);
    PWM_MOE = 0;                                   /* 1) 첫 줄: 출력 차단 */
    t_kill = CYC(); t_fault = t_kill - elapsed;    /* 하드웨어 사건 시각 복원 */
    TIMER_INTCLR(TIMER0_BASE) = 1; timer0_stop();  /* 2) 래치: 재발생 폭주 방지 */
    BaseType_t woken = pdFALSE;                    /* 3) 나머지는 위임 */
    xTimerPendFunctionCallFromISR(report_fault, NULL, 41250u, &woken);
    portYIELD_FROM_ISR(woken);
}
static void foc_fn(void *p)                       /* 전류 루프 태스크 (1 kHz로 축소) */
{
    (void)p; uint32_t last = CYC();
    for (;;) {
        uint32_t now = CYC(), g = now - last; last = now; if (g > foc_gap_max) foc_gap_max = g;
        if (PWM_MOE) foc_runs++; else foc_after_fault++;
        busy_us(100); vTaskDelay(1);
    }
}
static void main_fn(void *p)
{
    (void)p;
    vTaskPrioritySet(xTimerGetTimerDaemonTaskHandle(), D7_DPRIO);
    timer0_start(25000, 0x20);                     /* 25 ms 뒤 fault, FreeRTOS가 아는 최고 우선순위 */
    vTaskDelay(pdMS_TO_TICKS(40));
    board_printf_raw("daemon prio=%u: FOC runs before fault=%u, FOC max gap=%u us\n",
        D7_DPRIO, foc_runs, CYC2US(foc_gap_max));
    board_exit(0);
}
int main(void)
{
    xTaskCreate(foc_fn, "foc", 256, NULL, 5, NULL);
    xTaskCreate(main_fn, "main", 256, NULL, 6, NULL);
    vTaskStartScheduler();
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'for p in 7 2; do make -s -B -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/D7/f6/ex6.c OUT=/tmp/rtos_D7/ex6_p$p EXTRA_CFLAGS=-DD7_DPRIO=$p | grep -v filename; ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /tmp/rtos_D7/ex6_p$p/app.elf; done'
```

```text
  13036	    104	 135424	 148564	  24454	/tmp/rtos_D7/ex6_p7/app.elf
[report] overcurrent 41250 mA, fault->kill 240 ns, fault->report 14 us
[report] logged to flash, PWM_MOE=0, FOC runs after fault=0
daemon prio=7: FOC runs before fault=26, FOC max gap=3000 us
[board_exit] code=0
  13036	    104	 135424	 148564	  24454	/tmp/rtos_D7/ex6_p2/app.elf
[report] overcurrent 41250 mA, fault->kill 240 ns, fault->report 102 us
[report] logged to flash, PWM_MOE=0, FOC runs after fault=2
daemon prio=2: FOC runs before fault=26, FOC max gap=1000 us
[board_exit] code=0
```

출력에서 볼 것:

- **fault → PWM 차단 240 ns**(6 cycle) — 타이머 만료에서 ISR 첫 줄의 레지스터 읽기까지다. QEMU는 예외 스태킹을 모델링하지 않으므로 실칩에서는 12 cycle 이상 + 플래시 wait state가 더해진다. 그래도 µs 미만이다. 이 일을 위임했다면 최소 13.6 µs + 다른 태스크 사정만큼 늦었을 것이다. **안전 동작은 위임하지 않는다.** (실제 모터 드라이버는 이보다 한 단계 더 가서 comparator가 타이머의 break 입력을 직접 끊게 한다 — 소프트웨어가 0 ns가 되도록. ISR은 그 뒤처리다.)
- **daemon 우선순위 7(기본값)이면 FOC 최대 간격 3000 µs.** 보고 함수의 2.5 ms 플래시 기록이 FOC(우선순위 5)를 밀어냈다 — 1 kHz 루프가 두 번 빠졌다. fault 직후라 PWM은 이미 꺼져 있어 이 예제에서는 해가 없지만, 같은 daemon을 평상시 진단 보고에도 쓴다면 제어 루프가 흔들린다. 템플릿 기본 `configTIMER_TASK_PRIORITY = configMAX_PRIORITIES - 1`은 "타이머가 정확해야 한다"는 가정의 기본값이지 위임 실행자에 맞는 값이 아니다.
- **daemon을 2로 내리면 FOC 간격 1000 µs 유지**, 보고는 102 µs 뒤(FOC의 100 µs 계산이 끝난 뒤)에 시작했고, 보고 도중 FOC가 2번 돌았다(`FOC runs after fault=2`). 보고가 조금 늦는 대신 제어 루프가 지켜졌다.

### 8.3 센서 허브 MCU

Linux SoC 옆에서 IMU · 타임스탬프를 맡는 센서 허브라면, 호스트로 가는 SPI/UART 링크가 게이트키퍼(패턴 4)이고, 각 센서의 data-ready가 패턴 1이다. 호스트가 보낸 설정 명령(ODR 변경 등)은 링크 RX ISR → 명령 파서 태스크(패턴 1)로 넘기고, 파서가 센서 재설정을 각 센서 태스크에 요청 큐로 보낸다 — 센서 레지스터를 두 태스크가 동시에 만지지 않게 하는 것도 게이트키퍼 생각이다.

### 8.4 SSD 펌웨어와 대응

| SSD 컨트롤러 펌웨어 (Don 경험) | RTOS 위임 패턴 |
|---|---|
| NAND/DMA 완료 ISR이 완료 링에 기록만 | ISR → 패턴 1 (notify, 깨어나면 링을 끝까지 비움) |
| executive 루프가 완료 링 · 명령 큐를 순회 | 협력형 sysworkq와 닮음 (한 번에 하나, 끝까지 실행) |
| 채널 관리자만 채널 레지스터를 만짐 | 패턴 4 게이트키퍼 |
| NVMe interrupt coalescing (여러 완료에 IRQ 한 번) | 합쳐짐을 일부러 쓰는 것 — 깨어나면 전부 처리한다는 전제 |
| 온도 · 전원 이벤트는 저빈도 공용 핸들러 | 패턴 2 daemon 위임 |
| GC · wear leveling은 배경 우선순위 | 낮은 우선순위 전용 큐 / 태스크 |

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| ISR에서 I2C 읽기 · `printf` · 플래시 쓰기 | 다른 IRQ가 가끔 수백 µs 늦음, tick 밀림 | 같은 · 낮은 우선순위 IRQ가 ISR 길이만큼 대기 (예제 1: 298 µs) | ISR은 원인 해제 + 깨우기만, 일은 패턴 1~3으로 |
| 핸들러 태스크를 앱과 같은 우선순위로 | 위임된 일이 0~1 ms 랜덤 지연 | `vTaskGenericNotifyGiveFromISR`의 `>` 비교 — 같으면 다음 tick까지 (예제 2: 757 µs) | 방해할 수 있는 모든 태스크보다 엄격히 높게 |
| `portYIELD_FROM_ISR` 누락 | 우선순위가 높은데도 가끔 1 ms 늦음 | 다음 SysTick이 `xYieldPendings`를 보고서야 전환 (예제 2 `-DNO_YIELD`) | ISR 끝에서 항상 `portYIELD_FROM_ISR(woken)` |
| 세마포어 · `k_work`의 합쳐짐을 모름 | 고부하에서 사건 처리 누락, 카운터 불일치 | 대기 중 두 번째 give · submit은 버려짐 (예제 8: 12건) | 깨어나면 FIFO를 끝까지 비우기, notify 반환값 확인, 데이터가 다르면 큐 |
| `xTimerPendFunctionCallFromISR` 반환값 무시 | 폭주 때 일부 처리 누락, 타이머가 안 돎 | 명령 큐(기본 10칸)가 차면 `pdFAIL`, 타이머 명령도 같은 큐 (예제 4) | 반환값 검사 + 카운터, `configTIMER_QUEUE_LENGTH`를 최악 폭주 + 여유로 |
| sysworkq 항목에서 `k_msleep` · I2C · 긴 계산 | 다른 work 항목이 수십 ms 지연, 선점형 스레드 정지 | sysworkq는 단일 스레드 + 협력형 -1 (예제 8: 20 ms, 앱 22 ms 정지) | 블로킹 일은 `k_work_queue_start`로 만든 전용 큐로 |
| daemon 우선순위를 기본(최고)으로 둔 채 무거운 위임 | 제어 루프 주기 누락 | pended 함수가 모든 태스크를 선점 (예제 12: FOC 3 ms 공백) | 위임 실행자를 제어 루프보다 낮게, 무거운 일은 전용 태스크 |
| 게이트키퍼 큐에 `portMAX_DELAY`로 send | 로그가 밀리면 제어 태스크가 멈춤 | 큐가 차면 보내는 쪽이 블록 — 뮤텍스 문제가 되돌아옴 | `timeout 0` + drop 카운터, 큐 깊이 설계 (예제 10) |
| level-triggered IRQ를 원인 해제 없이 위임 | CPU가 ISR에 갇힘(interrupt storm), 태스크가 영영 안 돎 | 원인이 남아 ISR 복귀 즉시 재진입 | ISR에서 그 IRQ를 mask하고 핸들러가 원인 해제 후 unmask (§2.5) |
| `struct k_work`를 지역 변수로 두고 submit | 가끔 HardFault, 엉뚱한 함수 실행 | 함수가 반환한 뒤 큐 리스트가 사라진 스택을 가리킴 | static · 전역 · 장치 구조체 안에 두기, 해제 전 `k_work_cancel_sync` |

---

## 10. 면접에서 이렇게 말한다

**Q.** "Why shouldn't you do an I2C read inside an ISR? What do you do instead?"

**A.** I2C 트랜잭션은 수백 µs ~ ms가 걸리고 완료를 기다려야 하는데, ISR은 블록할 수 없고 그동안 같거나 낮은 우선순위의 모든 인터럽트와 tick이 밀린다. 측정해 보면 300 µs짜리 일을 ISR에 두면 같은 우선순위 IMU IRQ가 최대 298 µs 늦었다. ISR은 원인을 지우고 타임스탬프를 찍고 태스크를 깨우기만 하고, I2C는 그 태스크가 한다. 그러면 IMU 지연은 2 µs로 돌아온다.

> An I2C transaction takes hundreds of microseconds and has to wait for completion, but an ISR can't block, and while it runs every interrupt at the same or lower priority, including the tick, is held off. When I measured it, putting 300 µs of work in an ISR delayed a same-priority IMU interrupt by up to 298 µs. So the ISR only clears the source, timestamps, and wakes a task; the task does the I2C. The IMU latency went back to about 2 µs.

**꼬리 질문**: "그 태스크의 우선순위는?" → 그 일을 방해할 수 있는 모든 태스크보다 엄격히 높게. 같으면 FreeRTOS는 다음 tick까지 전환하지 않는다(`>` 비교).

**Q.** "How do you defer interrupt processing to a task in FreeRTOS, and what are the common mistakes?"

**A.** 기본은 direct-to-task notification이다 — ISR에서 `vTaskNotifyGiveFromISR`, 태스크는 `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)`로 블록. ISR 끝에서 `portYIELD_FROM_ISR(woken)`를 꼭 부른다. 실수는 셋이다. 핸들러 우선순위를 앱과 같게 두면 평균 0.75 ms 지연이 생기고, `portYIELD_FROM_ISR`를 빼먹으면 우선순위가 높아도 같은 증상이 나며(다음 tick이 대신 전환), binary semaphore는 사건이 겹치면 합쳐진다. notify는 반환값이 누적 개수라서 몇 번 왔는지 알 수 있다.

> The default is a direct-to-task notification: vTaskNotifyGiveFromISR in the ISR, ulTaskNotifyTake with clear-on-exit in the task, and always portYIELD_FROM_ISR at the end of the ISR. Three classic mistakes: giving the handler the same priority as a busy task, which adds up to one tick of latency; forgetting portYIELD_FROM_ISR, which shows exactly the same symptom because the next tick does the switch; and using a binary semaphore when events can overlap, because extra gives are lost. The notification value is a count, so the task knows how many events arrived.

**Q.** "What is xTimerPendFunctionCallFromISR and when would you use it instead of a dedicated task?"

**A.** ISR에서 "이 함수를 인자 두 개로 timer daemon 태스크에서 불러 줘"라는 16바이트 메시지를 timer 명령 큐에 넣는 API다. FreeRTOS 책은 centralized deferred interrupt processing이라고 부른다. 원인별 태스크 스택을 아낄 수 있어서 드물고 짧은 일 — fault 보고, 링크 상태 변화 — 에 쓴다. 대신 daemon은 하나뿐이라 모든 타이머 콜백과 지연을 나눠 가지고(2 ms 콜백 하나가 지연을 13.6 µs에서 1.9 ms로 늘렸다), 큐가 차면 `pdFAIL`이 나며 그 큐를 타이머 명령도 같이 쓴다. 고빈도이거나 블록하는 일, 별도 우선순위가 필요한 일은 전용 태스크로 간다.

> It posts a 16-byte message to the timer command queue asking the timer daemon task to call a function with two arguments. The FreeRTOS book calls it centralized deferred interrupt processing. It saves a stack per interrupt source, so I use it for rare, short work like fault reporting. The trade-offs: there is one daemon, so everything shares its worst case — one 2 ms timer callback pushed the latency from 13.6 µs to 1.9 ms in my test — and if the queue is full the call returns pdFAIL, and that same queue carries timer commands, so they fail too. High-rate, blocking, or priority-sensitive work gets its own task.

**Q.** "Explain Zephyr workqueues. What priority is the system workqueue, and why must you not block in it?"

**A.** `k_work`는 핸들러 포인터와 상태 플래그를 가진 16바이트 객체이고, workqueue는 그 객체들을 리스트로 받아 하나씩 실행하는 스레드다. `k_work_submit`은 ISR에서도 된다. system workqueue의 기본 우선순위는 `CONFIG_SYSTEM_WORKQUEUE_PRIORITY = -1`, 즉 가장 낮은 협력형이라 핸들러가 한 번 시작하면 선점형 스레드는 끼어들 수 없다. 항목 하나가 20 ms sleep하면 같은 큐의 다른 항목이 20 ms 늦고 그사이 다시 제출된 것은 합쳐져 사라진다. sleep 대신 20 ms 계산을 하면 선점형 스레드 전체가 22 ms 멈췄다. 그래서 블로킹 일은 `k_work_queue_start`로 우선순위를 직접 정한 전용 큐로 보낸다.

> A k_work is a 16-byte object — a handler pointer plus state flags — and a workqueue is a thread that pulls those objects off a list and runs them one at a time. k_work_submit is ISR-safe. The system workqueue defaults to priority minus one, the lowest cooperative priority, so once a handler starts, no preemptible thread can run. In my test, one item sleeping 20 ms delayed the other items by 20 ms and twelve resubmissions were coalesced away; an item that busy-waited 20 ms froze every preemptible thread for 22 ms. Blocking work goes to a dedicated queue created with k_work_queue_start at a priority I choose.

**꼬리 질문**: "`k_work_submit`이 0을 돌려주면?" → 이미 대기 중이라 아무것도 안 했다는 뜻. 실행 중에 제출하면 2(같은 큐에 다시 대기)다. QUEUED 비트는 핸들러 호출 직전에 지워지므로 처리 시작 후의 사건은 잃지 않는다.

**Q.** "What's the difference between k_work_schedule and k_work_reschedule?"

**A.** `k_work_schedule`은 이미 예약돼 있으면 아무것도 안 하고 0을 돌려준다 — 첫 마감이 이긴다(throttle). `k_work_reschedule`은 기존 예약을 지우고 새 마감으로 바꾼다 — 마지막 호출이 이긴다(debounce). 50 ms 예약 후 10 ms로 다시 부르면 schedule은 51 ms 뒤, reschedule은 11 ms 뒤에 돌았다(1 ms는 tick 올림).

> k_work_schedule does nothing if the item is already scheduled — the first deadline wins, which is throttling. k_work_reschedule cancels the pending timeout and sets the new one — the last call wins, which is debouncing. Scheduling 50 ms and then 10 ms fired at 51 ms with schedule and at 11 ms with reschedule; the extra millisecond is tick rounding.

**Q.** "What is a gatekeeper task? Why would you use one instead of a mutex?"

**A.** 자원 하나를 오직 한 태스크만 만지고, 다른 태스크는 요청을 큐에 넣는 설계다. 락이 없으니 우선순위 역전과 데드락이 구조적으로 없고, ISR에서도 요청할 수 있다. 115200 baud UART 로그로 재 보면 우선순위 높은 제어 태스크의 `log()`가 뮤텍스 방식에서 평균 1.8 ms, 최대 3.9 ms 붙잡혔는데 게이트키퍼 방식에서는 11 µs였다. 대신 출력은 나중에 나가고(최대 5 ms 뒤), 큐 깊이와 넘칠 때 정책을 설계해야 한다. 보내는 쪽은 timeout 0으로 보내고 버려진 수를 센다.

> One task owns the resource and everyone else sends requests through a queue. There's no lock, so priority inversion and deadlock can't happen, and ISRs can submit requests too. On a 115200-baud log UART, a high-priority control task's log call took 1.8 ms on average and 3.9 ms worst case with a mutex, versus 11 µs with a gatekeeper. The cost is that output is delayed — up to 5 ms here — and you must size the queue and decide what happens when it's full; I send with a zero timeout and count drops.

**Q.** "A motor controller detects overcurrent. What do you do in the ISR and what do you defer?"

**A.** ISR 첫 줄에서 PWM 출력을 끈다 — 측정하면 fault에서 차단까지 µs 미만이었고, 이걸 위임하면 최소 십수 µs에 스케줄링 사정이 더해진다. 그다음 원인을 지우고 그 IRQ를 래치해서 폭주를 막는다. 보고, fault 로그 플래시 기록, 재시작 정책은 위임한다. 이때 위임 실행자의 우선순위를 제어 루프보다 낮게 둬야 한다 — FreeRTOS daemon을 기본 최고 우선순위로 두었더니 2.5 ms 플래시 기록이 1 kHz FOC 루프를 3 ms 막았다. 더 나은 설계는 comparator가 타이머 break 입력을 하드웨어로 끊게 하고, ISR은 그 뒤처리만 하는 것이다.

> Kill the PWM outputs in the first line of the ISR — in my measurement that was well under a microsecond from the fault, while deferring it would cost at least a dozen microseconds plus scheduling. Then clear and latch the interrupt so it can't storm. Reporting, writing the fault log to flash, and restart policy are deferred — and the deferral executor must sit below the control loop. With the FreeRTOS daemon left at its default top priority, a 2.5 ms flash write blocked a 1 kHz FOC loop for 3 ms. Even better, route the comparator to the timer's break input so hardware cuts the PWM and the ISR is just the follow-up.

**Q.** "How does this map to Linux top half and bottom half?"

**A.** top half가 RTOS ISR, threaded IRQ가 전용 태스크 패턴, Linux workqueue가 Zephyr workqueue, softirq/tasklet에 가장 가까운 것이 FreeRTOS timer daemon 위임과 Zephyr 협력형 sysworkq다. 차이는 Linux는 genirq가 스레드와 ONESHOT masking까지 만들어 주지만 RTOS는 직접 조립한다는 것, 그리고 규모 — 같은 깨우기 경로가 VM 위 Linux에서 p50 116 µs였고 Cortex-M3 RTOS에서는 8 µs였다. 대신 RTOS는 우선순위를 잘못 주면 그 보장이 바로 ms로 무너진다.

> The top half is the RTOS ISR, a threaded IRQ is the dedicated-task pattern, the Linux workqueue is the Zephyr workqueue, and the closest things to softirqs and tasklets are the FreeRTOS timer-daemon deferral and Zephyr's cooperative system workqueue. The differences: Linux's genirq builds the thread and the oneshot masking for you, while on an RTOS you assemble it yourself; and the scale — the same wake-up path was 116 µs median on Linux in a VM and about 8 µs on a Cortex-M3 RTOS. But on an RTOS a wrong priority turns that into milliseconds immediately.

---

## 11. 직접 해보기

1. **개념**: 예제 2에서 핸들러 = 앱일 때 평균이 757 µs, 최대가 998 µs다. 이론적으로 최대는 얼마여야 하나? 평균이 500 µs가 아닌 이유는? 정답/힌트: 최대 ≈ 1 tick(1 ms) + 전환 비용. 같은 우선순위에서는 앱의 time slice가 끝나는 tick 경계까지 기다리는데, IRQ 주기 7 ms와 앱 주기(3 ms 계산 + 1 tick)의 위상 관계 때문에 IRQ가 tick 경계 직후에 떨어지는 경우가 많다. 주기를 7001 µs 같은 값으로 바꿔 분포가 어떻게 변하는지 보라.
2. **코드**: 예제 2의 핸들러를 `ulTaskNotifyTake`의 반환값을 기록하도록 바꾸고, TIMER0 주기를 500 µs로 줄여 핸들러 < 앱일 때 반환값 최대가 얼마인지 재라. binary semaphore 판에서는 같은 상황에서 몇 건이 사라지나? 정답/힌트: 앱이 3 ms를 붙잡으면 그동안 ISR이 약 6번 온다. notify는 반환값 6 근처, 세마포어는 "한 번 깨어남"만 남는다 — 차이가 곧 유실 수다.
3. **코드**: 예제 4에서 `xTimerPendFunctionCallFromISR`가 실패했을 때 실패 수를 세는 카운터와, 다음 ISR에서 "밀린 일 있음" 플래그로 한 번 더 시도하는 코드를 넣어라. 큐 10칸 그대로 14건이 모두 처리되는지 확인하라. 정답/힌트: 실패분을 ISR 쪽 비트마스크에 남기고, pend된 함수가 실행될 때 그 비트마스크도 처리하게 하면 큐 칸 수와 무관해진다(= 합쳐짐을 일부러 쓰는 설계).
4. **측정**: 예제 8에 `CONFIG_WORKQUEUE_WORK_TIMEOUT=y`, `CONFIG_SYSTEM_WORKQUEUE_WORK_TIMEOUT_MS=10`을 켜고(로그를 보려면 `CONFIG_LOG=y`) A sleep20 단계를 돌려 보라. 무엇이 찍히고 시스템은 어떻게 되나? 정답/힌트: `work_timeout_handler`가 `queue ... sysworkq blocked by work ... with handler ...`를 찍고 `k_thread_abort`한다. sysworkq는 essential이라 fatal error로 이어지는지 확인하라(§7.2).
5. **설계**: 드론 FC에서 바로미터(50 Hz, I2C, 가끔 NACK 재시도 5 ms), 자력계(100 Hz, I2C 같은 버스), 배터리 ADC(10 Hz)를 Zephyr로 처리한다. 큐 · 스레드 · 우선순위 배치를 그려라. 정답/힌트: 같은 I2C 버스를 쓰는 둘은 **한 전용 workqueue**(= 버스 게이트키퍼 역할)에 delayable 둘로 두면 버스 뮤텍스가 필요 없다. 우선순위는 estimator보다 낮게. ADC는 짧고 블록하지 않으면 sysworkq도 가능.
6. **측정**: 예제 10에서 게이트키퍼 우선순위를 1 → 5(H보다 높게)로 올려 H의 `log()` 시간과 H의 3 ms 주기 준수 여부를 재라. 무엇이 좋아지고 무엇이 나빠지나? 정답/힌트: end-to-end는 짧아지지만, 게이트키퍼가 폴링 UART로 CPU를 쓰는 동안 H가 밀린다. "게이트키퍼는 자원보다 빠르게 돌 필요가 없다 — 자원 속도로만 돌면 된다"가 결론. CPU를 놓는 DMA/IRQ 구동 UART라면 높여도 된다.
7. **소스 읽기**: Zephyr `k_work_cancel`이 RUNNING 중인 항목에 대해 무엇을 돌려주고, `k_work_cancel_sync`는 어떻게 기다리나? `kernel/work.c`의 `cancel_async_locked`(501행)와 `cancel_sync_locked`(537행)를 읽고 답하라. 정답/힌트: 실행 중이면 멈출 수 없으므로 CANCELING을 세우고 busy 플래그(RUNNING + CANCELING)를 돌려준다. sync 판은 canceller 세마포어를 큐에 걸어 두고, `work_queue_main`이 핸들러 반환 후 `finalize_cancel_locked`에서 그 세마포어를 준다.

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| work deferral | 작업 위임 | ISR은 최소한만 하고 나머지를 스레드 문맥으로 넘기는 설계 |
| deferred interrupt processing | 지연 인터럽트 처리 | FreeRTOS 책의 용어. 패턴 1(전용 태스크)과 패턴 2(centralized)로 나눈다 |
| top half / bottom half | Linux 용어 | hardirq 처리 / 나중에 하는 처리 (EL F6) |
| deferred-interrupt task | 전용 위임 태스크 | 인터럽트 원인 하나를 맡아 알림을 기다리는 태스크 |
| timer daemon | 타이머 서비스 태스크 | FreeRTOS 소프트웨어 타이머 콜백과 pended 함수를 실행하는 태스크 (D6) |
| `PendedFunction_t` | pend 함수 타입 | `void f(void *, uint32_t)` — daemon이 부를 함수 |
| `DaemonTaskMessage_t` | timer 명령 메시지 | ID 4 B + union, 16 B (pend 기능 끄면 12 B) |
| `k_work` | Zephyr 일 객체 | 노드 · 핸들러 · 큐 · 플래그, 16 B |
| `k_work_q` | Zephyr workqueue | 스레드 하나 + pending 리스트, 192 B + 스택 |
| system workqueue | sysworkq | 커널이 POST_KERNEL에 만드는 공용 큐, 기본 우선순위 -1(협력형), 스택 1024 B |
| `k_work_delayable` | 지연 일 | timeout이 붙은 `k_work`, 48 B. schedule(첫 마감 유지)과 reschedule(마감 교체) |
| coalescing | 합쳐짐 | 대기 중인 일을 다시 제출하면 한 번으로 합쳐지는 것 — 사건 유실의 원인이자 일부러 쓰는 최적화 |
| throttle / debounce | 묶기 / 미루기 | 첫 사건 기준 한 번 / 마지막 사건 후 조용해지면 한 번 |
| gatekeeper task | 게이트키퍼 | 자원을 단독 소유하고 요청 큐로만 접근을 받는 태스크 |
| `portYIELD_FROM_ISR` | ISR 끝 전환 요청 | 깨운 태스크가 더 높으면 PendSV를 걸어 ISR 복귀 즉시 전환 |
| `xYieldPendings` | 전환 보류 표시 | 전환이 필요한데 아직 안 한 상태. 다음 tick이 처리 |
| interrupt storm | 인터럽트 폭주 | 원인을 못 지운 level IRQ가 복귀 즉시 재진입하는 상태 |
| `IRQF_ONESHOT` | Linux 플래그 | 스레드 핸들러가 끝날 때까지 IRQ 라인을 mask. RTOS에서는 수동으로 |

---

## 13. 요약 & 체크리스트

ISR은 시스템 전체의 지연 예산을 쓴다. 300 µs짜리 일을 ISR에 두면 같은 우선순위 IRQ가 298 µs 늦고, 깨우기만 하면 2 µs로 돌아온다. 위임 수단은 네 가지다 — 원인별 **전용 태스크**(FreeRTOS notify 8.1 µs), FreeRTOS **timer daemon**에 함수를 맡기는 `xTimerPendFunctionCallFromISR`(13.6 µs, 16 B 메시지, 큐가 차면 실패하고 타이머 명령까지 막음), Zephyr **workqueue**(`k_work` 16 B, ISR에서 제출 가능, 대기 중 재제출은 합쳐짐, sysworkq는 협력형 -1이라 블록 금지), 그리고 자원을 한 태스크가 소유하는 **게이트키퍼**(호출자 1.8 ms → 11 µs, 대신 출력 지연과 큐 정책). 패턴 사이의 차이는 µs 단위지만, 실행자의 우선순위를 앱과 같게 두면 1 tick, 낮게 두면 앱의 계산 길이, 공유 실행자에 느린 일이 끼면 그 일의 길이만큼 — ms 단위로 뛴다. 그래서 위임 설계의 첫 질문은 언제나 "이 일을 실행하는 스레드의 우선순위는 무엇보다 높고 무엇보다 낮은가"다. 안전 동작(PWM 차단)은 위임하지 않는다.

- [ ] ISR에 남길 것(원인 해제, 타임스탬프, FIFO 비우기, 안전 동작, 깨우기)과 보낼 것을 구분할 수 있다
- [ ] 예제 1의 "ISR 안 300 µs → IMU 298 µs" 결과를 NVIC 우선순위로 설명할 수 있다
- [ ] FreeRTOS에서 같은 우선순위 태스크를 깨우면 즉시 전환하지 않는 이유를 `>` 비교로 설명할 수 있다
- [ ] `portYIELD_FROM_ISR`를 빼먹었을 때 "1 ms쯤 늦는" 증상이 생기는 경로(`xYieldPendings` → 다음 tick)를 말할 수 있다
- [ ] `xTimerPendFunctionCallFromISR`의 시그니처, 필요한 설정 두 개, daemon 문맥 제약을 말할 수 있다
- [ ] timer 명령 큐가 차면 pend와 타이머 명령이 같이 실패한다는 것과 큐 길이 정하는 규칙을 안다
- [ ] `k_work_submit`의 반환값 0 · 1 · 2 · 음수의 뜻과 합쳐짐이 생기는 조건을 말할 수 있다
- [ ] sysworkq 기본 우선순위(-1, 가장 낮은 협력형)와 그래서 블록 · 긴 계산이 왜 해로운지 측정으로 설명할 수 있다
- [ ] `k_work_schedule`(throttle)과 `k_work_reschedule`(debounce)을 용도에 맞게 고를 수 있다
- [ ] 게이트키퍼와 뮤텍스를 호출자 지연 · 역전 · ISR 사용 · 출력 지연 기준으로 비교할 수 있다
- [ ] 위임 실행자 우선순위가 제어 루프를 막은 예(FOC 3 ms 공백)를 들고 고치는 법을 말할 수 있다
- [ ] Linux top/bottom half · threaded IRQ · workqueue와 RTOS 패턴의 대응을 한 문장씩 말할 수 있다

## 참고 자료

- FreeRTOS-Kernel V11.3.2 소스 — `timers.c`(`xTimerPendFunctionCallFromISR` 1238행, `prvTimerTask` 746행, `prvProcessReceivedCommands` 936행, 42~43행 `#error`), `include/timers.h`(`PendedFunction_t`, `tmrCOMMAND_EXECUTE_CALLBACK_FROM_ISR`), `tasks.c`(`vTaskGenericNotifyGiveFromISR`, `xTaskIncrementTick`의 `xYieldPendings`), `queue.c`(`xQueueGenericSendFromISR`). 저장소: [FreeRTOS-Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel)
- Richard Barry, Mastering the FreeRTOS Real Time Kernel — Interrupt Management 장(deferred interrupt processing, centralized deferred interrupt processing), Resource Management 장(gatekeeper tasks). 저장소: [FreeRTOS-Kernel-Book](https://github.com/FreeRTOS/FreeRTOS-Kernel-Book)
- Zephyr v4.4.2 소스 — `kernel/work.c`(`submit_to_queue_locked`, `queue_submit_locked`, `work_queue_main`, `work_timeout_handler`, `schedule_for_queue_locked`, `work_timeout`), `kernel/system_work_q.c`, `kernel/Kconfig`(`SYSTEM_WORKQUEUE_*`, `WORKQUEUE_WORK_TIMEOUT`), `include/zephyr/kernel.h`(`K_WORK_*_BIT`, `struct k_work`, `struct k_work_queue_config`)
- Zephyr 문서 — [Workqueue Threads](https://docs.zephyrproject.org/latest/kernel/services/threads/workqueue.html)
- Linux 문서 — [Workqueue](https://docs.kernel.org/core-api/workqueue.html) (EL F7과 비교용)
- 이 노트 세트: A2(실행 모델), C2(context switch), C4(스케줄링 정책), C5(tick), C7(크리티컬 섹션), C12(`z_add_timeout` +1 tick), D1 · D2 · D3 · D4 · D5 · D6, EL F6 · F7
