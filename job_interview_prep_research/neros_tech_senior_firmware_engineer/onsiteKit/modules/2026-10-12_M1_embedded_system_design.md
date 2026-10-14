# 🧩 M1 Embedded system design 45분 — scheduler · task 수 · stack · rx/tx · motor

> 필기 키워드: **scheduler, number of process?, entire process, stack, proof, pstack, rx, tx, motor**, 그리고 "**배우는 걸 좋아하는 사람인지** 볼 것". 이 페이지는 MCU 하나에 들어가는 실시간 펌웨어(드론 비행 제어기 류)를 화이트보드에 처음부터 설계하는 연습이다.

> 🆕 10-14 확정 일정에서 1:1은 Adam 30분 · Michael Honar 45분 · Jon 45분. 이 주제는 **Adam 30분** 세션일 가능성이 가장 높다 [추정] → **11절 30분 버전**을 먼저 연습.

## 1. 키워드 해석 — 무엇을 물을까

| 필기 | 가장 그럴듯한 질문 [추정] | 답의 핵심 |
|---|---|---|
| scheduler | "How do you decide what runs when?" | superloop vs cooperative vs preemptive RTOS. 고정 우선순위 + rate monotonic |
| number of process? | "How many tasks would you create, and why?" | **주기와 중요도로 묶는다**. 태스크마다 stack RAM · 문맥 전환 · 동기화 비용이 든다 → 5~8개 |
| entire process | "Walk me through the whole thing, from power-on." | 부트 → 초기화 순서 → 센서 확인 → 무장 전 점검 → 루프 |
| stack | "How big is each stack?" | 정적 분석 + 실측 high-water + 여유 |
| proof | "How do you **prove** it fits / meets deadlines?" | stack: `-fstack-usage` 최악 경로 + 페인팅. 타이밍: 이용률 · 응답 시간 분석 + 실측 |
| pstack | Cortex-M **PSP**(process stack pointer) vs MSP? 또는 Linux `pstack` | 둘 다 짧게 말할 수 있게 (5절) |
| rx, tx | 무선 수신 → 파싱 → 제어, 텔레메트리 송신 | DMA + idle-line 인터럽트 + ring buffer + 최신값 mailbox |
| motor | 제어 루프 → 모터 출력의 지연과 결정성 | timer + DMA 출력, 루프 맨 끝, 고장 시 즉시 0 |

> "learn 좋아하는지" → 모르는 질문이 일부러 나온다. **아는 것 → 첫 원리로 추론 → 어떻게 확인하겠다**의 순서로 답하면 된다 (8절).

## 2. 45분 진행 순서 (화이트보드)

| 분 | 할 일 | 말할 문장 |
|---|---|---|
| 0~5 | 요구사항 질문 | "What's the control loop rate? Which MCU class? What must happen when the radio link drops?" |
| 5~12 | 블록도: 센서 · 무선 RX · 제어 · 모터 · 텔레메트리 TX · 로깅 | "Let me draw the data flow first, then decide tasks." |
| 12~22 | **task 표** (3절) | "I group by rate and criticality." |
| 22~32 | 깊게 하나: RX 경로 또는 stack/타이밍 증명 | 면접관이 고르게 한다: "Which part do you want me to go deeper on?" |
| 32~40 | 고장 모드: link loss · 센서 고장 · 과부하 · stack overflow · watchdog | |
| 40~45 | **어떻게 테스트하나** (테스트 직군이므로 반드시) | "Here's how I'd verify each of these claims." |

## 3. Task 표 — 예시 (비행 제어기, Cortex-M7 급 [가정])

| 일 | 트리거 | 주기 | 마감 | 우선순위 | 어디서 |
|---|---|---|---|---|---|
| 자이로 읽기 | 자이로 data-ready 인터럽트 → SPI DMA | 4 kHz (250 µs) | 다음 샘플 전 | 최상 | ISR + DMA 완료 콜백 |
| 필터 + PID + 믹서 | 자이로 데이터 도착 | 4 kHz | 같은 주기 안 | 1 | 최상위 task (또는 ISR 연쇄) |
| 모터 출력 | PID 직후 | 4 kHz | PID 끝 즉시 | 1 | 같은 task 마지막 줄 (timer DMA 시작) |
| RX 파싱 · 채널 갱신 | UART idle-line 인터럽트 | 패킷 주기 (예: 250 Hz~1 kHz) | 다음 패킷 전 | 2 | task |
| failsafe 감시 | 1 ms tick | 1 kHz | 100 ms 안에 반응 | 2 | 같은 task 또는 tick |
| 배터리 · 전류 ADC | timer | 100 Hz | 느슨 | 3 | task |
| 텔레메트리 TX | timer | 10~50 Hz | 느슨 | 4 | task |
| 블랙박스 로깅 | 큐에 데이터 있음 | 1~2 kHz 기록 | 큐 넘치기 전 | 5 | task (SPI flash) |
| CLI · 설정 · USB | 이벤트 | — | 없음 | 최하 | idle 근처 |

> **task 수 질문의 답:** "About five or six. Everything at the control-loop rate lives in one task so there is no hand-off jitter between gyro, PID and motor. Slower work is split by rate, and anything that can block — flash writes, USB — gets its own low-priority task so it can never delay the loop."

## 4. Scheduler 선택

| 방식 | 장점 | 단점 | 언제 |
|---|---|---|---|
| superloop | 단순, 디버그 쉬움 | 느린 일이 빠른 일을 막음 | 일이 2~3개 |
| cooperative (시간 기반) | 공유 자원 문제 적음 | 한 task가 오래 돌면 전체 지연 | Betaflight 류 [추정 수준 비교] |
| **preemptive 고정 우선순위 RTOS** | 마감 분석 가능, 느린 일과 분리 | stack 여러 개, 동기화 · priority inversion | 일이 많고 마감이 다양할 때 |
| Linux (companion) | 네트워크 · 영상 · 파일 | 실시간성 약함 | 비행 제어 바깥 |

- **우선순위 정하는 법: rate monotonic** — 주기가 짧을수록 높은 우선순위
- ISR은 짧게: 플래그/데이터만 넘기고 처리는 task로 (deferred work)
- 공유 자원: mutex + **priority inheritance**. 제어 루프 경로에선 lock을 아예 안 잡는 구조(최신값 double buffer)가 가장 좋다

## 5. Stack — 크기를 정하고 "증명"하기

| 단계 | 방법 | 무엇을 얻나 |
|---|---|---|
| 정적 | `gcc -fstack-usage` → 함수별 크기, 호출 그래프에서 **최악 경로** 합 | 상한 (재귀 · 함수 포인터 · VLA가 있으면 깨짐 → 금지) |
| 인터럽트 몫 | Cortex-M 예외 진입 시 8워드(32 B), FPU 문맥 포함 시 26워드(104 B) + ISR 자체 사용량, **중첩** 깊이만큼 | 인터럽트는 MSP를 쓰므로 MSP 크기에 더함 |
| 동적 | 시작 시 stack을 `0xA5` 등으로 칠하고(painting), 스트레스 후 안 지워진 깊이 측정 = high-water mark | 실측값 |
| 여유 | 정적 상한 또는 실측의 1.25~1.5배 [관례] | |
| 보호 | MPU guard 영역, 끝에 canary 값, RTOS overflow hook | 넘치면 조용히 깨지지 않고 **즉시 멈춤** |

- **PSP vs MSP (pstack 해석 1)**: Cortex-M은 stack pointer가 둘. **MSP**는 리셋 후 기본 · 예외/ISR · 커널, **PSP**는 RTOS task(thread mode)용. task마다 PSP가 자기 stack을 가리키고, 문맥 전환은 PendSV에서 PSP를 바꿔 끼우는 것. → "ISR stack 사용량은 task stack이 아니라 MSP에 잡는다"가 핵심
- **Linux `pstack <pid>` (해석 2)**: 돌고 있는 프로세스의 모든 스레드 stack trace를 찍는 도구(gdb 기반). companion computer에서 멈춘 프로세스 원인 볼 때
- 영어 한 줄: "I size it statically from the worst-case call path plus interrupt frames, confirm with stack painting under stress, keep 25–50% margin, and put an MPU guard below it so an overflow faults instead of corrupting memory."

## 6. 타이밍 "증명"

1. 각 일의 최악 실행 시간 C를 **측정**: 시작/끝에 GPIO 토글 → 스코프, 또는 DWT cycle counter
2. 이용률 U = Σ C/T. rate monotonic 충분조건: U ≤ n(2^(1/n) − 1) (n=3이면 0.78, n이 크면 0.69)
3. 넘거나 애매하면 **응답 시간 분석**: R = C + Σ(높은 우선순위 j) ⌈R/T_j⌉·C_j 를 반복 계산 → R ≤ 마감이면 OK

| 예시 task | C | T | C/T |
|---|---|---|---|
| 제어 루프 | 80 µs | 250 µs | 0.32 |
| RX | 30 µs | 1000 µs | 0.03 |
| 텔레메트리 | 200 µs | 20000 µs | 0.01 |
| 합 | | | **0.36 ≤ 0.78 → OK** |

- RX 응답 시간: R = 30 + ⌈R/250⌉·80 → R=110 → ⌈110/250⌉=1 → 30+80=110 ✓ (마감 1000 µs 안)
- 실측도 남긴다: 루프 주기 지터 히스토그램, CPU load 카운터 (idle task에서 증가) → **그 자체가 테스트 아티팩트**

## 7. RX · TX · Motor 경로

| 경로 | 구조 | 함정 |
|---|---|---|
| RX | UART **DMA 순환 버퍼** + idle-line 인터럽트 → task가 새 바이트만 파싱(sync · 길이 · CRC) → 채널 값은 **최신값 하나**(mailbox)에 저장 + 수신 시각 | 큐로 쌓으면 오래된 조종 명령이 늦게 적용됨. 최신값만 의미 있음 |
| failsafe | 마지막 정상 프레임 시각과 현재 비교, 100 ms 넘으면 failsafe | CRC 실패 프레임은 "수신"이 아니다 |
| TX | 텔레메트리 생산자 → **크기 고정 큐**(가득 차면 오래된 것 버림 + 카운트) → DMA 송신 | half-duplex 링크면 송신 슬롯 규칙. 송신 때문에 제어 루프가 기다리면 안 됨 |
| Motor | 믹서 → 범위 제한(saturation) → timer + DMA로 ESC 신호(PWM/DShot) | 무장 안 됐으면 항상 0. 고장 · watchdog 리셋 시 **기본값 0**이 보장되게 |

## 8. 모르는 질문이 나왔을 때 (learn 태도)

1. 아는 범위를 정확히: "I haven't used X directly; I've done Y, which is similar in ..."
2. 첫 원리로 추론: "If it works like ..., then the constraint would be ..."
3. 확인 방법: "I'd check the reference manual / measure it on the bench by ..."
4. 되묻기: "How does your team handle it today?" — 배우려는 사람으로 보인다

## 9. 이 설계를 어떻게 테스트하나 (마지막 5분)

| 주장 | 검증 |
|---|---|
| 루프가 4 kHz로 지터 없이 돈다 | GPIO 토글 + 스코프, 지터 히스토그램을 CI(HIL)에서 매번 |
| stack이 넘치지 않는다 | 스트레스 시나리오 후 high-water 리포트, 여유 25% 미만이면 실패 |
| link loss → 100 ms 안 failsafe | HIL에서 RX 신호를 끊고 모터 출력 변화 시각 측정 |
| 깨진 패킷은 무시된다 | 퍼저로 랜덤/CRC 틀린 프레임 주입 |
| 과부하에도 제어 루프는 지킨다 | 로깅 · 텔레메트리를 최대로 올리고 루프 주기 측정 |

## 10. 혼자 연습 — 다른 주제로 같은 틀

- 배터리 관리 보드(BMS) 펌웨어: 셀 전압 측정 · 보호 · 통신
- 짐벌 모터 제어기: 센서 · FOC 루프 · 명령 수신
- 텔레메트리 라디오 모듈: 패킷 큐 · 재전송 · 저전력
- 각각 **task 표 · stack 증명 · 고장 모드 · 테스트 표** 네 장을 15분 안에

## 11. 30분 압축판 (Adam 세션 대비)

| 분 | 할 일 | 줄일 것 |
|---|---|---|
| 0~3 | 요구사항 질문 2개만: 루프 주기 · link loss 동작 | 나머지는 "I'll assume ..."로 말하고 진행 |
| 3~8 | 블록도 (센서 → 제어 → 모터, RX → 제어, 텔레메트리 TX, 로깅) | 박스 6개 이내 |
| 8~15 | task 표 5줄 (3절에서 제어 · RX · failsafe · 텔레메트리 · 로깅) | 주기 · 우선순위 열만 |
| 15~22 | 면접관이 고른 하나를 깊게: stack 증명 **또는** 타이밍 증명 **또는** RX 경로 | 하나만 |
| 22~27 | 고장 모드 3개 + 각각 어떻게 테스트하나 | 9절 표에서 3줄 |
| 27~30 | 질문 받기 / 역질문 1개 | |

> 디렉터라 설계 대신 **동기 · 레벨 · 팀 비전** 대화가 될 수도 있다 → M0 7절 Q&A 표의 "Why move from FW dev to test?", "What would you automate first here?"를 30초 답으로 준비.
