# ⚡ K02 마지막 이틀 정리 — 1:1 세 개 (system design · debugging/test design · Python 코딩)

> 10-20(화) · 10-21(수) 이틀용 압축본. M1 · M2 · M3 · C01~C07과 Devin 콜(10-12) · 확정 메일(10-14)에서 들은 것만 모았다. **주제별 15~30분 단위**로 끊어서 읽고, 각 절의 **영어 한 줄**은 소리 내어 말해 본다. 발표는 이 페이지에서 다루지 않는다.

| 세션 [추정 매핑] | 시간 | 들은 주제 (Devin 10-12) | 이 페이지 |
|---|---|---|---|
| Adam Kibit (FW Director) | 12:30 · **30분** | embedded system design: scheduler · task 수 · entire process · stack · proof · pstack · rx/tx · motor · "배우는 걸 좋아하나" | 2 · 3절 |
| Michael Honar (FW Test Mgr) | 13:00 · 45분 | HW/test design: 고장 난 PDB + 회로도 · 테스트 안 된 서브시스템 통합 · 결과 정리 | 4 · 5 · 6절 |
| Jon Kotowski | 13:45 · 45분 | 코딩, 웬만하면 Python, ring buffer라고 말 안 해 줌. 지난번 **overflow case 누락** | 7절 |

> 누가 무엇을 낼지는 확인 못 했다. **세 주제 모두 누구에게든** 나올 수 있다고 보고 준비.

## 0. 이틀 시간표 (권장)

| 언제 | 길이 | 할 것 |
|---|---|---|
| 10-20 오전 | 30분 | 1절 공통 틀 → 2절 task 표를 **빈 종이에** 10분 안에 |
| 10-20 오전 | 30분 | 3절 stack · 타이밍 증명 숫자를 말로 · 3.5절 테스트 시스템 설계 |
| 10-20 오후 | 45분 | **모의 코딩 1회** (C02 → 남는 시간 C01), 빈 파일 + 터미널만 |
| 10-20 오후 | 30분 | 4절 디버깅 전략 · 5절 PDB 30초 답 소리 내어 3번 |
| 10-20 저녁 | 30분 | 6절 서브시스템 통합 · 8절 모를 때 답하는 법 |
| 10-21 오전 | 45분 | **모의 코딩 2회** (C04, 피곤한 상태 가정) |
| 10-21 오후 | 20분 | 이 페이지 영어 한 줄만 전부 소리 내어 · 9절 체크리스트 |
| 10-22 아침 | 15분 | C02 빈 파일에서 한 번 (손풀기) |

## 1. 세 세션 공통 틀 — 어떤 질문이든 이 순서

| 순서 | 할 일 | 영어 한 줄 |
|---|---|---|
| ① 질문 | 범위 · 제약 · 실패했을 때 동작 | "Before I start — a couple of questions about constraints." |
| ② 가정 말하기 | 답이 없으면 내가 정하고 적어 둔다 | "I'll assume X; tell me if that's wrong." |
| ③ 그림 먼저 | 블록도 / 데이터 흐름 / 상태 | "Let me draw the data flow first." |
| ④ 깊게 하나 | 면접관이 고르게 한다 | "Which part do you want me to go deeper on?" |
| ⑤ 고장 모드 | 무엇이 깨지면 어떻게 되나 | "Now, what happens when things go wrong." |
| ⑥ 검증 | 각 주장을 어떻게 테스트하나 (**테스트 직군이므로 항상 마지막에**) | "Here's how I'd prove each of these." |

> 발표에서 보여 준 내 흐름 **repro → isolate/capture/resolve → itemize → stress → scale (+ WAR in parallel)** 을 1:1에서도 같은 단어로 쓰면 하루 전체가 하나의 이야기가 된다.

## 2. Embedded system design (Adam 30분 압축판)

### 2.1 30분 진행

| 분 | 할 일 |
|---|---|
| 0~3 | 질문 2개만: "What's the control-loop rate?" · "What must happen when the radio link drops?" |
| 3~8 | 블록도 6박스: 센서(IMU) → 제어 → 모터 / RX → 제어 / 텔레메트리 TX / 로깅 |
| 8~15 | task 표 5줄 (2.2) |
| 15~22 | 깊게 하나: stack 증명 **또는** 타이밍 증명 **또는** RX 경로 (3절) |
| 22~27 | 고장 모드 3개 + 각각의 테스트 (3.4) |
| 27~30 | 역질문 1개 |

### 2.2 Task 표 — 외워서 그릴 5줄 (비행 제어기, Cortex-M7 급 가정)

| task | 트리거 | 주기 | 우선순위 |
|---|---|---|---|
| 자이로 → 필터 → PID → 믹서 → **모터** | 자이로 data-ready 인터럽트 → SPI DMA 완료 | 4 kHz (250 µs) | 최상 |
| RX 파싱 · 채널 갱신 | UART idle-line 인터럽트 | 패킷마다 (250 Hz~1 kHz) | 2 |
| failsafe 감시 | 1 ms tick | 1 kHz | 2 |
| 배터리 ADC · 텔레메트리 TX | timer | 10~100 Hz | 3~4 |
| 로깅 (flash) · CLI/USB | 큐 / 이벤트 | — | 최하 (막혀도 루프에 영향 없게) |

- **"How many tasks?"** → "About five or six. Everything at the loop rate lives in one task so there's no hand-off jitter between gyro, PID and motor. Slower work is split by rate, and anything that can block — flash, USB — gets its own low-priority task."
- **"Which scheduler?"** → "Preemptive fixed-priority RTOS, priorities by rate-monotonic — shorter period, higher priority. ISRs stay short and hand work to tasks. On the control path I avoid locks entirely with a latest-value double buffer; elsewhere mutexes with priority inheritance."
- **superloop은 언제?** 일이 2~3개이고 마감이 하나일 때. 느린 일이 빠른 일을 막는 게 단점.

### 2.3 "Entire process" — 전원 켜고 비행까지

부트로더(이미지 CRC 검사) → 클럭 · MPU · 메모리 → 드라이버 초기화 → **센서 self-test (WHO_AM_I, 값 범위)** → 설정 로드(CRC) → task 시작 → **무장 전 점검** (스로틀 low · link alive · 센서 OK · 배터리 OK) → 무장 → 루프. watchdog은 루프 맨 끝에서만 킥.

## 3. 깊게 들어갈 하나 — 숫자까지 말할 수 있게

### 3.1 Stack "증명" (stack · proof · pstack)

| 단계 | 방법 |
|---|---|
| 정적 | `gcc -fstack-usage` → 함수별 크기, 호출 그래프 **최악 경로** 합. 재귀 · 함수 포인터 · VLA는 금지 (분석이 깨짐) |
| 인터럽트 몫 | 예외 진입 8워드(32 B), FPU 문맥 포함 26워드(104 B) + ISR 사용량 × 중첩 깊이 → **MSP**에 더한다 |
| 동적 | 시작 시 stack을 `0xA5`로 칠하고(painting) 스트레스 후 안 지워진 깊이 = high-water mark |
| 여유 | 상한/실측의 1.25~1.5배 [관례] |
| 보호 | MPU guard · canary · RTOS overflow hook → 넘치면 조용히 깨지지 않고 **즉시 fault** |

- **PSP vs MSP**: MSP = 리셋 후 기본 · ISR · 커널. PSP = RTOS task(thread mode) 각자의 stack. 문맥 전환은 PendSV에서 PSP를 바꿔 끼우는 것. → "ISR stack usage goes on the MSP, not on every task stack."
- **Linux `pstack <pid>`**: 실행 중 프로세스의 모든 스레드 stack trace (gdb 기반). companion computer에서 멈춘 프로세스 볼 때.
- 영어: "I size it statically from the worst-case call path plus interrupt frames, confirm with stack painting under stress, keep 25–50% margin, and put an MPU guard below it so an overflow faults instead of corrupting memory."

### 3.2 타이밍 "증명"

1. 최악 실행 시간 C를 **측정**: 시작/끝 GPIO 토글 → 스코프, 또는 DWT cycle counter
2. 이용률 U = Σ C/T. rate monotonic 충분조건 U ≤ n(2^(1/n) − 1) → n=3이면 0.78, n이 크면 0.69
3. 애매하면 응답 시간 분석: R = C + Σ⌈R/T_j⌉·C_j (높은 우선순위 j) 반복 → R ≤ 마감이면 OK

| 예 | C | T | C/T |
|---|---|---|---|
| 제어 루프 | 80 µs | 250 µs | 0.32 |
| RX | 30 µs | 1 ms | 0.03 |
| 텔레메트리 | 200 µs | 20 ms | 0.01 |
| 합 | | | **0.36 ≤ 0.78 ✓** |

RX 응답: R = 30 + ⌈R/250⌉·80 = 110 µs ≤ 1 ms ✓. 실측 아티팩트: **루프 지터 히스토그램 · CPU load(idle 카운터)**.

### 3.3 RX · TX · Motor

| 경로 | 구조 | 함정 |
|---|---|---|
| RX | UART **DMA 순환 버퍼** + idle-line 인터럽트 → task가 새 바이트만 파싱 (sync · len · CRC) → **최신값 mailbox** + 수신 시각 | 큐로 쌓으면 오래된 조종 명령이 늦게 적용됨 |
| failsafe | 마지막 **정상** 프레임 시각 vs now, 100 ms 넘으면 failsafe | CRC 실패 프레임은 "수신"이 아니다 |
| TX | 고정 크기 큐 (가득 차면 오래된 것 버림 + **카운트**) → DMA 송신 | 송신 때문에 루프가 기다리면 안 됨 |
| Motor | 믹서 → saturation → timer + DMA (PWM/DShot) | 무장 전 · 고장 · watchdog 리셋 시 **기본값 0** 보장 |

### 3.4 고장 모드 → 테스트 (마지막 5분용 표)

| 주장 | 검증 |
|---|---|
| 루프가 4 kHz 지터 없이 돈다 | GPIO + 스코프, 지터 히스토그램을 HIL CI에서 매번 |
| stack이 안 넘친다 | 스트레스 후 high-water 리포트, 여유 25% 미만이면 fail |
| link loss → 100 ms 안 failsafe | HIL에서 RX를 끊고 모터 출력 변화 시각 측정 |
| 깨진 패킷은 무시 | 퍼저로 랜덤 / CRC 틀린 프레임 주입 |
| 과부하에도 루프는 지킨다 | 로깅 · 텔레메트리 최대 → 루프 주기 측정 |

### 3.5 "Generic system design"이 **테스트 시스템**으로 나오면 (테스트 직군이라 가능성 있음 [추정])

예: "Design the test setup for our flight controller boards" / "factory test firmware" / "HIL regression".

```text
 [DUT] ── fixture(pogo pins, 전원 제어, 전류 측정) ── [station PC: test runner]
   │  factory test FW (UART/USB CLI: self-test, rails, sensors, IDs)        │
   └────────────── results (run id · serial · FW ver · 측정값 · pass/fail) ──→ [DB] → [dashboard]
```

| 결정 | 내 답 |
|---|---|
| DUT 쪽 | **factory test FW**: 얇은 CLI, 명령 하나 = 측정 하나, 결과는 기계가 읽는 형식 (발표 ② 검증 FW와 같은 생각: 필요한 층만 남기고 분리) |
| station 쪽 | Python runner, 테스트 = 데이터(한계값 표) + 코드, 장비 드라이버 분리 |
| 결과 | 한 줄 = 한 측정, **run id · DUT serial · FW 버전 · station id**로 묶음 → 추세 · 수율 · 실패 상위 |
| 확장 | 포트 수를 늘려도 코드가 안 바뀌게 (발표 ① 1–2 → 40 → 80 포트) |
| 신뢰 | 테스트 자체의 오류: golden unit, 측정 반복성(GR&R), 한계값은 데이터로 정한다 |
| 속도 | 병렬 측정, 실패 시 빠른 종료 vs 전부 측정(디버그용) 모드 분리 |

## 4. Debugging strategy — 어떤 버그든 이 순서

### 4.1 틀 (발표와 같은 단어)

| 단계 | 할 일 | 영어 한 줄 |
|---|---|---|
| 0. 증상 정리 | 무엇이 · 언제부터 · 몇 대 중 몇 대 · 무엇이 바뀌었나 (FW/HW rev/환경) | "What changed, and how many units?" |
| 1. **Repro** | 재현 조건 찾기, 재현율 숫자로. 안 되면 조건을 극단으로 (온도 · 전압 · 부하 · 반복) | "First I make it fail on demand." |
| 2. **Isolate** | 반으로 나누기: FW vs HW, 보드 바꾸기, FW 버전 `git bisect`, 기능 끄기, 층 분리 | "Then I split the system in half until one block is left." |
| 3. **Capture** | 실패 순간의 증거: 로그 · 레지스터 · 스코프/로직 분석기 트리거 · fault 덤프 | "I capture evidence at the moment of failure, not after." |
| 4. **Resolve** | 원인 하나를 **측정으로 확인**한 뒤 고친다. 고친 뒤 같은 측정으로 재확인 | "I confirm the root cause with a measurement before I change anything." |
| ↳ **WAR** (병렬) | 고치는 데 오래 걸리면 work-around로 빌드/라인을 먼저 풀어 준다 | "If the fix takes time, a work-around unblocks the build in parallel." |
| 5. **Itemize** | 이 버그를 잡는 테스트 항목을 만든다 | "Then the check that would have caught it becomes a test item." |
| 6. **Stress · Scale** | 같은 종류를 스트레스로 더 찾고, 다른 라인/제품에 적용 | "And I roll it out to the other lines." |

### 4.2 증상 → 먼저 볼 것 (FW · 보드)

| 증상 | 먼저 볼 것 |
|---|---|
| HardFault | `CFSR`(0xE000ED28) · `HFSR`(0xE000ED2C) · `MMFAR`/`BFAR` 읽기. stacked frame(R0–R3, R12, LR, **PC**, xPSR)에서 PC → addr2line. `EXC_RETURN` bit 2로 MSP/PSP 중 어느 stack인지 |
| 랜덤 리셋 | **리셋 원인 레지스터** (STM32 `RCC_CSR`: watchdog · brownout · pin · software) → 원인별로 나눈다. brownout이면 전원 레일 스코프 |
| 가끔 데이터가 깨짐 | race (ISR ↔ task 공유 변수, `volatile`/atomic/critical section), DMA 버퍼와 **캐시 일관성**(Cortex-M7), stack overflow로 옆 메모리 덮음 |
| 디버거 붙이면 안 생김 / 릴리스에서만 생김 | 타이밍 의존(race), 최적화로 드러나는 UB, 초기화 안 된 변수, `volatile` 누락 |
| 시간이 지나면 멈춤 | 메모리 누수 · 큐 포화 · 카운터 overflow(32비트 ms는 49.7일) · 우선순위 역전 · deadlock |
| 성능/지터 | GPIO 토글 + 스코프로 구간 측정, ISR 길이, 높은 우선순위 task가 CPU 독점 |

### 4.3 버스 디버그 — 30초 답

| 버스 | 먼저 볼 것 |
|---|---|
| I2C | 주소 NACK → **7비트/8비트 주소 혼동**, pull-up 유무 · 값(상승 시간), SDA가 low에 붙음 → SCL 9펄스로 복구, 클럭 스트레칭 |
| SPI | **mode(CPOL/CPHA)**, CS 타이밍, 클럭 속도 낮춰 보기, MISO가 tri-state인지 |
| UART | baud 불일치 → framing error (스코프로 비트 폭 재기), TX/RX 교차, 전압 레벨(3.3 V vs 5 V vs RS-232), **overrun**(읽기가 늦음 → DMA로) |
| 공통 | 로직 분석기로 디코드 → "보낸 것이 선 위에 있나?"부터. 있으면 받는 쪽, 없으면 보내는 쪽 |

> 영어: "I start at the wire. If the bytes are on the bus, the problem is on the receiving side; if they aren't, it's the sender or the configuration."

## 5. 고장 난 PDB + 회로도

### 5.1 30초 답 (외우기)

> "First I don't power it. I read the schematic for rating mistakes, inspect the board, and check for shorts with a meter. Then I power it from a current-limited bench supply, ramp the voltage, and measure each rail against the schematic. When something is off I isolate that block — lift a part, cut the load — and confirm the root cause with a measurement before I change the design. Finally I add the check that would have caught it to the board test."

순서: 증상(전부 vs 일부 보드?) → 회로도 리뷰 → 육안/현미경 → **무전원 저항 측정(단락)** → 전류 제한 전원으로 천천히 → 레일 전압 · 리플 → 열화상 → 전자 부하 · 과도 → 블록 분리 → 같은 측정으로 재확인 · 기록.

### 5.2 PDB 블록과 흔한 고장

| 블록 | 흔한 고장 |
|---|---|
| 배터리 입력 패드 | 납땜 불량, 역극성 |
| 큰 구리 면 | 단면적 부족 → 발열 · 전압 강하 |
| 벌크 전해 커패시터 | 없음/용량/정격 부족 → 스파이크로 레귤레이터 사망 |
| TVS | 방향 · 정격 |
| BEC (buck 5 V / 9~12 V) | 입력 정격 초과 · 피드백 저항 오류 · 인덕터 포화 · 발열 |
| 전류 센서 (shunt + 증폭기) | 방향 · gain · 오프셋 |
| VBAT 분압기 | 비율 틀림 → 배터리 경고 오작동 |

### 5.3 증상별 첫 의심

| 증상 | 먼저 |
|---|---|
| 연결하자마자 스파크 · 연기 | 입력 단락, 역극성, 전해 커패시터 방향 |
| 5 V 레일 0 V | enable 핀, 입력 도달, 출력 단락, 레귤레이터 사망 |
| 5 V가 6.x V | 피드백 저항 값/위치 |
| 스로틀 올리면 FC 리셋 | brownout · 스파이크 → 벌크 커패시터, 레일 리플 (스코프 트리거) |
| 배터리 전압 표시 틀림 | 분압 비율, ADC 기준 전압, FW scale |
| 전류 값 0 / 엉뚱 | shunt 방향, 증폭기 gain, 오프셋, FW scale/offset |
| 레귤레이터 뜨거움 | 과부하, LDO면 손실 = (Vin − Vout) × I |

### 5.4 손으로 할 계산 4개

| 항목 | 식 | 예 |
|---|---|---|
| VBAT 분압 | V_adc = V_bat × R2/(R1+R2) | 10k/1k: 25.2 V(6S 만충) → 2.29 V ✓. 35 V 스파이크면 3.18 V — **여유 거의 없음** |
| buck 출력 | V_out = V_ref × (1 + R_top/R_bottom) | V_ref 0.8 V, 5 V → 비율 5.25 |
| shunt | V = I·R, P = I²R | 0.25 mΩ × 100 A = 25 mV, 손실 2.5 W |
| 정격 여유 | 정격 ≥ 최대 × 1.5~2 [관례] | 6S 입력 레귤레이터는 35~40 V 이상 |

## 6. 테스트 안 된 새 서브시스템 통합 + 결과 정리

> 영어: "I'd go bottom-up — power, then talking on the bus, then function, then performance and fault injection — but I'd order the work by risk, so the thing that could crash the aircraft gets tested first."

| 단계 | 하는 일 | 통과 기준 예 |
|---|---|---|
| 0 | 요구사항 · **ICD**(전원 · 핀 · 프로토콜 · 타이밍) 확인, 없으면 첫 산출물 | |
| 1 입고 | 리비전 · 커넥터 · 핀아웃이 문서와 같은가 | 불일치 0 |
| 2 전원 | 전류 제한 전원으로 단독, 소모 전류 | 데이터시트 범위 |
| 3 통신 | I2C 스캔 · ID 레지스터 · 기본 메시지 | ID 일치, 에러 0 |
| 4 기능 | 기능 하나씩 | 기능별 기준 |
| 5 성능 | 지연 · 처리량 · 정확도 · 온도/전압 범위 | 기준 없으면 **측정해서 만든다** |
| 6 고장 주입 | brownout · 케이블 뽑기 · 깨진 데이터 · 노이즈 · 재부팅 중 명령 | 안전 상태 → 복구 |
| 7 시스템 | 실기체: 모터 켠 EMI · 진동 · 다른 서브시스템 타이밍 | 회귀 없음 |
| 8 내구 | 장시간 · 온도 사이클 · 진동 | 시간당 에러율 |
| 9 자동화 | HIL에 넣어 FW 바뀔 때마다 | CI 녹색 |

**결과 정리 (아티팩트)**: 1쪽 테스트 계획(범위 · 위험 · 진입/종료 기준) → 테스트 케이스 표(ID · 목적 · 절차 · 기대 · 결과 · 증거 링크) → 원시 데이터(run id · serial · FW 버전으로 묶음) → 요약(pass/fail · **요구사항 × 테스트 커버리지** · 열린 이슈 심각도 · **go / no-go / 조건부**) → 대시보드.

**버그 보고 한 장**: 증상(측정값 vs 기대값 · 조건) · 영향 범위 · 원인 + 증거 · 임시 조치 · 영구 조치 · 검증 · 재발 방지(추가한 테스트).

> "Done means the requirement has a test, the test ran on the real build, the result is stored where anyone can find it, and it runs again automatically next time."

## 7. Python 코딩 (Jon 45분)

### 7.1 시작 30초

> "Last time I missed the full-buffer case, so I'll call out overflow behavior up front this time." — 한 번, 짧게.

### 7.2 코드 쓰기 전에 — 질문 3개 + 경계 4개

- 질문: **크기 한계** · **꽉 찼을 때 정책 (reject+count vs overwrite oldest)** · **잘못된 입력 처리** · 시간 단위 ("Can I pass `now` in?")
- 경계 4개를 말로: **empty · full · 1개 · wrap-around** → 마지막에 이 4개 테스트를 직접 쓴다
- 시간은 **정수 ms** (float 경계 버그: `1.10 + 0.05 != 1.15`)

### 7.3 문장 → 자료구조

| 들리면 | 꺼낼 것 |
|---|---|
| fixed memory · ISR hands bytes · main loop falls behind | 고정 FIFO (C02) |
| bytes in chunks · serial · sync byte | `bytearray` + 파서, 깨지면 **1바이트만 버리고** 재동기 (C01) |
| last N readings · moving average · K of N | `deque(maxlen=N)` + 누적 합 (C03) |
| timeout · retry · ACK | id → (마감, 시도 수) dict + `now` 주입 (C04) |
| lost · sequence · counter wraps | `(seq - expected) % 65536`, 32768 기준 앞/뒤 (C05) |
| only arm when · safe mode | 상태 머신 + 전이 로그 (C06) |
| binary record · little-endian | `struct.unpack("<HhhhHB", ...)` (C07) |
| earliest deadline | `heapq` |

### 7.4 45분 배분

0~5 질문 · 5~8 손으로 예시 · 8~10 접근 + 복잡도 · 10~30 코딩(말하면서) · **30~40 테스트 직접 작성** (happy · empty · full/overflow · wrap · 잘못된 입력) · 40~45 후속 ("C라면?" → 고정 배열 + mask, "스레드 둘이면?" → SPSC는 head는 생산자만, tail은 소비자만 갱신).

### 7.5 C02를 손이 기억하게 (overflow 두 정책)

```python
class Fifo:
    def __init__(self, cap, overwrite=False):
        if cap <= 0:
            raise ValueError("cap must be > 0")
        self.buf = [None] * cap
        self.cap, self.overwrite = cap, overwrite
        self.head = self.tail = self.count = 0   # head: next write, tail: next read
        self.dropped = self.overwritten = 0

    def put(self, x):
        if self.count == self.cap:
            if not self.overwrite:
                self.dropped += 1
                return False
            self.tail = (self.tail + 1) % self.cap   # drop oldest: move tail too!
            self.count -= 1
            self.overwritten += 1
        self.buf[self.head] = x
        self.head = (self.head + 1) % self.cap
        self.count += 1
        return True

    def get(self):
        if self.count == 0:
            return None
        x = self.buf[self.tail]
        self.tail = (self.tail + 1) % self.cap
        self.count -= 1
        return x

if __name__ == "__main__":
    f = Fifo(3)
    assert f.get() is None                              # empty
    assert all(f.put(i) for i in (1, 2, 3))
    assert f.put(4) is False and f.dropped == 1         # full -> reject + count
    assert [f.get() for _ in range(3)] == [1, 2, 3]     # order kept
    g = Fifo(3, overwrite=True)
    for i in (1, 2, 3, 4):
        g.put(i)
    assert g.overwritten == 1 and g.get() == 2          # oldest gone, tail moved
    for i in range(10):                                  # wrap many times
        g.put(i)
    assert [g.get() for _ in range(3)] == [7, 8, 9]
    print("OK")
```

### 7.6 맨손 치트시트

```python
from collections import deque, defaultdict, Counter
import struct, heapq, math
buf = bytearray(); buf += chunk; i = buf.find(0xAA); del buf[:i]
buf[1]            # int,  buf[1:2] -> bytes
q = deque(maxlen=5)
(seq - exp) % 65536
struct.unpack("<Hh", b"\x01\x00\xff\xff") == (1, -1)
math.isnan(v)     # NaN < x is always False -> check first; None < x -> TypeError
for k in list(d): # copy keys before deleting while iterating
```

## 8. 모르는 질문이 나오면 ("배우는 걸 좋아하나" 보는 지점)

1. 아는 범위를 정확히: "I haven't used X directly; I've done Y, which is similar in …"
2. 첫 원리로 추론: "If it works like …, the constraint would be …"
3. 확인 방법: "I'd check the reference manual, or measure it on the bench by …"
4. 되묻기: "How does your team handle it today?"

## 9. D-1 체크리스트

- [ ] task 표 5줄 + 블록도를 빈 종이에 10분 안에
- [ ] stack 증명 영어 한 줄 · 이용률 숫자 예(0.36 ≤ 0.78) 말하기
- [ ] 디버깅 틀 7단계(repro → … → scale + WAR)를 30초에
- [ ] PDB 30초 답 · 서브시스템 통합 영어 한 줄
- [ ] C02 빈 파일 15분, C01 · C04 각 20분
- [ ] 역질문 면접관별 2개 ([M0](../modules/2026-10-12_M0_presentation_40min.md) 7절)
- [ ] 신분증 · 10:05 도착 · 1100 Knox St

## 10. 더 깊게 볼 곳

[M1 system design](../modules/2026-10-12_M1_embedded_system_design.md) · [M3 HW/test design](../modules/2026-10-12_M3_hw_test_design.md) · [M2 coding](../modules/2026-10-12_M2_coding_python.md) · [C01](../coding/2026-10-12_C01_packet_stream.md) · [C02](../coding/2026-10-12_C02_fixed_fifo.md) · [C04](../coding/2026-10-12_C04_retry_queue.md) · [K01 일정](2026-10-14_K01_onsite_day_schedule_panel.md)
