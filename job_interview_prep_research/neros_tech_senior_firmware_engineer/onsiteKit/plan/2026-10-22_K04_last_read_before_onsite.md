# 🚗 K04 출발 전 마지막 읽기 — Adam · Michael · Jon 1:1

> 10-22(목) 출발 전 20~30분용. 세 명이 **지난 라운드에 실제로 물어본 것**을 먼저 복습하고, 오늘 나올 것(Adam · Michael = 임베디드, Jon = 코딩)을 짧게 정리했다. 깊게 볼 곳은 끝에 링크. **새로 외우지 말고 이미 아는 것을 꺼내기 쉽게** 만드는 페이지다.

## 0. 오늘 기억할 5가지

1. **모든 답의 틀**: 질문 → 가정 말하기 → 그림 → 깊게 하나 → 고장 → **어떻게 검증하나** (테스트 직군이라 항상 마지막)
2. **퀴즈형 답의 틀** (Michael 스타일): **한 문장 정의 → 왜 → 예시** 30초
3. **코딩 첫 마디** (Jon): "Last time I missed the full-buffer case, so I'll call out overflow behavior up front." → 꽉 찼을 때 정책을 **묻고**, 잃은 것은 **세고**, overflow 테스트를 **쓴다**
4. **모르면**: "I haven't done that directly; the closest I've done is … I'd approach it by … How does your team do it today?"
5. **일관성**: SSD FW "about seven years" · Apple RF 통합 · 트랙(개발 vs 테스트) 이야기는 **오늘 꺼내지 않는다** (오퍼 단계에서)

| 시간 | 세션 | 지난번 | 오늘 예상 [추정] |
|---|---|---|---|
| 12:30 · 30분 | **Adam Kibit** (FW Director) | 09-18 HM 45분 기술 인터뷰 | 임베디드 system design 압축판 + 동기 · 팀 |
| 13:00 · 45분 | **Michael Honar** (FW Test Mgr) | 10-01 퀴즈형 30분 | 임베디드 퀴즈 + HW/test design (PDB · 서브시스템 통합 · 디버깅) |
| 13:45 · 45분 | **Jon Kotowski** | 10-08 C ring buffer + extract_packet | Python 코딩 (힌트 없음) |

---

## 1. Adam Kibit — 12:30, 30분

### 1.1 이 사람

- Director of Firmware. 경력: Johnson Controls **Test Eng → SW Eng → Tech Lead**, 이후 텔레매틱스 · EV 플랫폼 리더. 이력서 키워드: **IPC 프로토콜 · BSP + Jenkins CI · EOL(양산 끝단) 테스트 앱 throughput +20% · 자동 테스트 시스템 · OTA** [LinkedIn 09-16 열람]
- → 테스트에서 시작한 사람이다. **양산 · 테스트 · 플랫폼 재사용** 언어가 통한다
- 09-18 인터뷰에서 **실제로 받은 질문은 기록이 없다**. 결과: 테스트 트랙으로 방향 전환 → 이후 통과. 그때 준비했던 질문 골격이 아래다

### 1.2 그때 준비했던 질문 — 다시 나올 수 있다

| 질문 | 30초 답 골격 |
|---|---|
| Hardest bring-up? | 첫 전원부터 순서 → 무엇이 안 됐나 → silicon / board / FW를 어떻게 가렸나 → 남긴 것(체크리스트 · 자동화). 발표 ② 검증 FW와 연결 |
| A bug nobody could crack? | 증상 → 가설 → 계측(DSO · 로직 분석기) → 근본 원인 → 팀 간 fix → **재발 방지 테스트** |
| Factory test-node work? | 커버리지 vs 테스트 시간(= 원가). 빌드마다 더 촘촘히 → MP에서 최소 비용으로 최대 필터 (발표 ④) |
| Board B: 같은 기능, 다른 MCU/센서 | HAL 경계를 칩이 아니라 **기능** 단위로 · 보드 설정은 **데이터** · 런타임 보드 ID 또는 빌드 variant · **두 보드 모두 매 커밋 CI + HIL** · 드라이버 차이는 ops 테이블 |
| MCU ↔ companion Linux 프로토콜 | 전송(UART/SPI/Eth) → 프레이밍(길이 + CRC 또는 COBS) → 메시지 ID · 버전 → ACK 필요한 것 vs **최신값만 중요한 것** → 흐름 제어 → 시간 동기 → 한쪽이 죽었을 때 |
| 필드 FW 업데이트(OTA) | A/B 슬롯 · 서명 · 부트 검증 · 롤백 · 전원 차단 복구 · 단계 배포. **직접 해 본 적 없다고 솔직히** + 원리 |
| 팀 빌드 / CI | 툴체인 고정 → 모든 타깃 매 커밋 빌드 → 유닛 + HIL 스모크 → 바이너리에 커밋 해시 → 어떤 FW가 어떤 기체에 실렸나 추적 |
| 양산 라인에서 FW가 할 일 | 프로비저닝(시리얼 · 키 · 보정) → 기능 테스트 모드 → 결과를 MES로 → 불량 데이터를 설계로 되먹임 (**factory test FW가 아직 없다** — Michael 10-01) |

### 1.3 오늘 30분 system design이 나오면 (임베디드)

| 분 | 할 일 |
|---|---|
| 0~3 | 질문 2개: "What's the control-loop rate?" · "What must happen when the radio link drops?" |
| 3~8 | 블록도 6박스: IMU → 제어 → 모터 · RX → 제어 · 텔레메트리 TX · 로깅 |
| 8~15 | task 표 5줄 ↓ |
| 15~22 | 깊게 하나 (stack · 타이밍 · RX 중 면접관이 고름) |
| 22~27 | 고장 모드 3개 + 각각의 테스트 |
| 27~30 | 역질문 |

| task | 트리거 | 주기 | 우선순위 |
|---|---|---|---|
| 자이로 → 필터 → PID → 믹서 → **모터** | 자이로 data-ready IRQ → SPI DMA | 4 kHz | 최상 |
| RX 파싱 | UART idle-line IRQ | 패킷마다 | 2 |
| failsafe 감시 | 1 ms tick | 1 kHz | 2 |
| 배터리 · 텔레메트리 | timer | 10~100 Hz | 3~4 |
| 로깅 · CLI/USB | 큐 / 이벤트 | — | 최하 (막혀도 루프 영향 없게) |

- **task 수**: "About five or six. Everything at the loop rate in one task — no hand-off jitter. Slower work split by rate; anything that can block gets its own low-priority task."
- **scheduler**: 고정 우선순위 preemptive RTOS, **rate monotonic**(주기 짧을수록 높게). ISR은 짧게 → task로. 제어 경로는 lock 없는 최신값 double buffer, 그 밖은 mutex + **priority inheritance**
- **stack 증명**: `-fstack-usage` 최악 경로 + 인터럽트 프레임(8워드 / FPU 26워드, **MSP**에) → stack painting으로 실측 high-water → 25~50% 여유 → MPU guard. **PSP**는 task, **MSP**는 ISR · 커널
- **타이밍 증명**: C를 GPIO + 스코프 / DWT로 측정 → U = ΣC/T (예 0.36 ≤ 0.78) → 애매하면 응답 시간 분석
- **RX**: DMA 순환 버퍼 + idle-line → 파싱 → **최신값 mailbox** + 시각. 큐로 쌓으면 오래된 조종 명령이 늦게 적용됨
- **Motor**: 무장 전 · 고장 · watchdog 리셋 시 **기본값 0** 보장
- **검증**: 지터 히스토그램을 HIL CI에서 · 스트레스 후 high-water · RX 끊고 failsafe 시간 측정 · 퍼저로 깨진 프레임

### 1.4 디렉터 대화가 되면

| 질문 | 답 방향 |
|---|---|
| Why test? | "What I've done all along is prove that hardware and firmware are ready. Test infrastructure makes the whole team faster." |
| What would you automate first? | 사람이 매일 반복하는 것 · 실패 비용이 큰 것 순서. **먼저 1~2주 현재 흐름을 본다** |
| 역질문 | "What does the test team own today, and what do you want it to own in a year?" · "What slows FW releases down the most right now?" |

---

## 2. Michael Honar — 13:00, 45분

### 2.1 이 사람 · 지난번 스타일

- FW Test Manager, **이 자리의 실제 매니저**. 리크루터 필기 "embedded / FPGA 배경". 오늘 투어 · 발표 · 점심 · 1:1까지 **2시간 15분**을 함께 보낸다
- 10-01: 15분 늦게 들어와 약 30분, **퀴즈형으로 연달아** 질문. "done" 답에 반응이 좋았다
- 투어에서 본 장비 · 스테이션을 1:1 답에 끌어오면 좋다: "Like the station we saw on the tour…"

### 2.2 지난번 질문 — 더 좋은 답으로 다시

**① UART vs I2C vs SPI**

| | UART | I2C | SPI |
|---|---|---|---|
| 선 | TX, RX (+GND) | SDA, SCL (open-drain + **pull-up**) | SCLK, MOSI, MISO, CS(장치마다) |
| 클럭 | 없음 (비동기, **baud 약속**, start/stop 비트) | 동기 | 동기 |
| 장치 | 1:1 | 다수, **7비트 주소** | 다수, **CS 선**으로 선택 |
| 방향 | full duplex | half duplex | full duplex |
| 확인 | 없음 (parity 선택) | **ACK/NACK** 비트마다 | 없음 |
| 속도 | ~115 k~수 Mbps | 100 k / 400 k / 1 M | 수십 MHz |
| 흔한 문제 | baud 불일치 → framing error, overrun | 주소 7/8비트 혼동, pull-up, **SDA stuck → SCL 9펄스** | **mode (CPOL/CPHA)**, CS 타이밍 |

> 한 줄: "UART is two wires with no clock — both sides agree on a baud rate. I2C is two shared open-drain wires with addresses and an ACK per byte, slower. SPI has a clock and a chip-select per device, full duplex and fast, but no acknowledgment."

**② PCIe를 SW에서 어떻게 쓰나** — ⚠️ 지난번 HW(핀 · SerDes)로 답함. **SW 흐름**으로:

1. **Enumeration**: 부팅 때 BIOS/OS가 버스를 훑어 각 장치의 **config space**(vendor/device ID, class)를 읽는다
2. **BAR**: config space의 BAR로 장치 레지스터 영역 크기를 알아내고 주소를 배정 (모두 1을 써서 크기 확인)
3. **MMIO**: 드라이버는 그 주소를 매핑해서 **레지스터를 메모리처럼** 읽고 쓴다 (doorbell 쓰기 등)
4. **DMA**: 큰 데이터는 장치가 호스트 메모리를 직접 읽고 쓴다 — 드라이버는 **descriptor / 큐**만 준비
5. **인터럽트**: **MSI/MSI-X** (메모리 쓰기 형태의 인터럽트, 큐마다 벡터)
6. **드라이버 바인딩**: ID로 드라이버가 붙는다. 예: **NVMe** = submission / completion 큐 + doorbell + MSI-X
7. 디버그 관점: link 속도 · 폭, **AER**(에러 보고), `lspci -vvv`

> 한 줄: "From software, PCIe looks like memory: the OS enumerates devices through config space, maps their BARs, the driver talks to registers through MMIO, moves bulk data with DMA through descriptor queues, and gets MSI-X interrupts back. NVMe is the example I know best."

**③ 8b/10b** — 목적 위주로

- 8비트 데이터 → 10비트 심볼. 오버헤드 25%
- 왜: **DC balance**(running disparity로 0과 1 개수를 맞춤 → AC 결합 가능) · **run length 최대 5** → 천이가 충분해서 **수신측이 클럭을 복원(CDR)** · **제어 심볼**(K28.5 comma로 심볼 경계 정렬) · 없는 코드 = **에러 검출**
- PCIe Gen1/2는 8b/10b → Gen3~5는 **128b/130b**(오버헤드 ~1.5%) → Gen6는 PAM4 + FLIT + FEC
- 한 줄: "It trades 25% bandwidth for DC balance, enough transitions for clock recovery, and special symbols for alignment — that's why Gen3 moved to 128b/130b to get the bandwidth back."

**④ Git · PR · rebase**

- PR: 브랜치 → 커밋 → push → 리뷰 + **CI 통과** → merge
- **rebase**: 내 커밋을 최신 main 위로 다시 쌓음 → 이력이 일직선. **공유된 브랜치는 rebase 금지**(남의 이력이 바뀜). `git pull --rebase`, `git rebase -i`로 커밋 정리(squash)
- merge: 이력 보존, merge 커밋 생김
- 테스트 직군 포인트: **`git bisect`** — 회귀가 어느 커밋에서 들어왔는지 반씩 좁히기 (자동 테스트와 같이 쓰면 `git bisect run`)

**⑤ 정의 안 된 것의 테스트 케이스** — 지난번 "unit test부터 쌓는다" + 보강

1. 의도를 묻는다: 누가 어떻게 쓰나, 무엇이 깨지면 위험한가
2. **특성화(characterization)**: 지금 동작을 측정해서 기준선을 만든다
3. 경계값 · 동치 분할 · 잘못된 입력 · 고장 주입
4. 내 가정을 **임시 사양**으로 적고 리뷰받는다 → 테스트가 곧 사양이 된다
5. 아래(유닛)에서 위(시스템)로 쌓는다

**⑥ done의 의미** — 지난번 반응 좋았음, **같은 말로**: 80% 상태로 일찍 내보내고 고객 · 다른 팀 피드백으로 다듬는다. 덧붙일 한 줄: "And for test, done means the requirement has a test, it ran on the real build, the result is stored where anyone can find it, and it runs again automatically."

### 2.3 오늘 HW/test design이 나오면

**고장 난 PDB — 30초 (외우기)**

> "First I don't power it. I read the schematic for rating mistakes, inspect the board, and check for shorts with a meter. Then I power it from a current-limited bench supply, ramp the voltage, and measure each rail against the schematic. When something is off I isolate that block and confirm the root cause with a measurement before I change the design. Finally I add the check that would have caught it to the board test."

| 증상 | 먼저 |
|---|---|
| 연결 즉시 스파크 · 연기 | 입력 단락, 역극성, 전해 커패시터 방향 |
| 5 V 레일 0 V | enable 핀, 입력 도달, 출력 단락, 레귤레이터 입력 정격 초과 |
| 5 V가 6.x V | 피드백 저항 |
| 스로틀 올리면 FC 리셋 | brownout · 스파이크 → 벌크 커패시터, 레일 리플 스코프 트리거 |
| 배터리 전압 표시 틀림 | 분압비, ADC 기준, FW scale |
| 전류 값 엉뚱 | shunt 방향, 증폭기 gain, 오프셋 |

계산: VBAT 분압 10k/1k → 25.2 V(6S) = 2.29 V ✓ · buck V_out = V_ref × (1 + R_top/R_bot) · shunt 0.25 mΩ × 100 A = 25 mV, 2.5 W

**테스트 안 된 새 서브시스템 — HW + FW 같이**

> "I'd go bottom-up — power, then talking on the bus, then function, then performance and fault injection — but I'd order the work by risk, so the thing that could crash the aircraft gets tested first."

| HW 쪽 | FW 쪽 (테스트 FW) |
|---|---|
| ICD 확인 · 핀아웃 · 전류 제한 전원으로 단독 | ① 보드 설정(핀 · 클럭 · IRQ · DMA, **부팅 때 모터 0**) |
| 통신 연결(버스 스캔 · ID) | ② 버스 드라이버 ③ 디바이스 드라이버: ID · **설정 readback** · 알려진 입력 → 알려진 값 |
| 기능 · 성능 · 온도 · 전압 | ④ 데이터 경로: 주기 · 유실 카운터 · 지연 · 타임스탬프 |
| 고장 주입(케이블 · brownout · 노이즈) | ⑥ 고장 처리: 안전 상태 → 보고 → 복구 |
| 실기체 EMI · 진동 | ⑤ 제품 FW에 넣고 지터 · CPU · stack 회귀 비교 |

테스트 FW 원칙: **명령 하나 = 측정 하나**, 판정은 station PC(한계값을 바꿔도 FW 재빌드 없음), 결과는 기계가 읽는 한 줄 (`RESULT imu.whoami PASS value=0x47`), 가능하면 **제품 드라이버 재사용**.

**결과 정리**: 1쪽 계획 → 케이스 표(ID · 기대 · 결과 · 증거) → 원시 데이터(run id · serial · FW 버전) → 요약(커버리지 · 열린 이슈 · **go / no-go / 조건부**)

### 2.4 디버깅 전략 — 발표와 같은 단어로

**repro → isolate → capture → resolve (+ WAR in parallel) → itemize → stress → scale**

| 증상 | 먼저 |
|---|---|
| HardFault | `CFSR` · `HFSR` · `MMFAR`/`BFAR`, stacked PC → addr2line, `EXC_RETURN` bit 2 = MSP/PSP |
| 랜덤 리셋 | **리셋 원인 레지스터**(STM32 `RCC_CSR`: watchdog · brownout · pin · SW) |
| 가끔 데이터 깨짐 | ISR ↔ task race, DMA와 캐시(M7), stack overflow |
| 디버거 붙이면 안 생김 | 타이밍 race, 초기화 안 된 변수, `volatile` 누락 |
| 오래 돌면 멈춤 | 큐 포화 · 누수 · 32비트 ms 카운터(49.7일) · 우선순위 역전 |
| 버스 | 로직 분석기: **선 위에 바이트가 있나?** 있으면 받는 쪽, 없으면 보내는 쪽 |

### 2.5 역질문

"What does a typical day of the test team look like — bench, HIL, factory?" · "Where do test results live today?" · (투어 후) "Which of the stations we saw does the test team own?"

---

## 3. 임베디드 퀵파이어 — Adam · Michael 공통 (각 30초)

| 질문 | 핵심 |
|---|---|
| `volatile` | 컴파일러가 읽기/쓰기를 없애거나 캐싱하지 못하게. **원자성 · 순서는 보장 안 함** (그건 atomic / critical section) |
| ISR에서 하면 안 되는 것 | 블로킹 · malloc · printf · 긴 루프 → 플래그/큐만 넣고 task로 |
| priority inversion | 낮은 task가 lock 보유 중 중간 task가 선점 → 높은 task가 기다림 → **priority inheritance** |
| watchdog kick 위치 | 타이머 ISR에서 무조건 kick은 무의미 → **모든 task의 alive 비트가 모였을 때만** |
| tick wraparound | `(now - start) >= timeout`은 안전 · `now > deadline`은 깨짐 (unsigned 빼기) |
| 구조체를 그대로 전송 | 패딩 · 엔디안 · 정렬 → 필드 단위 직렬화 |
| stack overflow 탐지 | 카나리 · painting(high-water) · MPU guard |
| 부팅 순서 | 리셋 벡터 → SP · 클럭 → `.data` 복사 · `.bss` 0 → 주변장치 → main |
| DMA + 캐시 | 송신 전 **clean**, 수신 후 **invalidate**, 또는 캐시 안 되는 영역 |
| mutex vs semaphore | mutex = 소유자가 있는 lock(상속 가능) · semaphore = 개수/신호(ISR → task 알림) |
| polling vs interrupt vs DMA | 빈도 낮고 단순 = polling · 이벤트 반응 = IRQ · 대량 데이터 = DMA + 완료 IRQ |
| 엔디안 | little = 낮은 바이트 먼저(ARM 기본) · 네트워크는 big |
| bare-metal vs RTOS | 일 2~3개 · 마감 하나 = superloop · 마감이 다양하면 RTOS |
| STM32 / RTOS 써 봤나 | 레쥬메에 직접 없음 → **Cortex-M/R bare-metal 경험으로 연결**, 모르면 모른다고 + 배우는 방법 |

---

## 4. Jon Kotowski — 13:45, 45분 (마지막, 피곤할 때)

### 4.1 지난번

- 10-08: 시작하자마자 "Python or C?" → **C**. 행동 질문 없이 코딩만
- Task 1: 외부 버퍼 ring buffer — `rb_init(rb, uint8_t *buf, uint8_t capacity)`, push/pop byte, **bulk push/pop all-or-nothing**, `rb_peek(offset)`
- Task 2(보너스): `extract_packet` + 제공된 `CRC.c`(테이블 방식)
- 피드백(Devin): **거의 다 했지만 overflow case 누락** → 오늘은 웬만하면 **Python**, "ring buffer"라고 말해 주지 않을 것
- 이 사람은 full stack [추정] → **코드 품질 · 테스트 · 말로 설명**을 본다

### 4.2 첫 3분 (그대로 말하기)

```text
"Let me repeat it back: ..."
"Questions: is there a size limit? What should happen when it's FULL —
 reject and count, or overwrite the oldest? How do you want bad input handled?
 Can I pass the current time in, in integer milliseconds?"
"Edge cases I'll test: empty, full, exactly one, wrap-around, garbage."
"I'll write it, then plain asserts at the bottom."
```

### 4.3 문장 → 자료구조

| 들리면 | 꺼낼 것 |
|---|---|
| fixed memory · ISR hands bytes · main loop falls behind | 고정 FIFO (T1) |
| bytes in chunks · serial · sync byte | `bytearray` 파서, 깨지면 **1바이트만** 버리고 재동기 (T2) |
| timeout · retry · ACK | id → (마감, 시도 수) dict + `now` 주입 (T3) |
| last N · moving average | `deque` + 누적 합 (T4) |
| sequence · counter wraps | `(seq - expected) % 65536` (T5) |
| only arm when · safe mode | 상태 머신 + 전이 로그 (T6) |
| binary record · little-endian | `struct.unpack("<…")` + CRC (T7) |

### 4.4 손이 기억해야 할 overflow 코드 (T1 핵심 10줄)

```python
def put(self, x):
    if self.count == self.cap:                  # OVERFLOW: decide, then count
        if not self.overwrite:
            self.dropped += 1
            return False
        self.tail = (self.tail + 1) % self.cap  # drop oldest: move tail too
        self.count -= 1
        self.overwritten += 1
    self.buf[self.head] = x
    self.head = (self.head + 1) % self.cap
    self.count += 1
    return True
```

```python
assert f.put(4) is False and f.dropped == 1        # full -> reject + count
assert [f.get(), f.get(), f.get()] == [1, 2, 3]    # old data and order kept
```

- bulk: **쓰기 전에** `if len(items) > self.cap - self.count: return False` (C면 빼기로 — `count + len`은 wrap)
- T2 파서 핵심: `find(SYNC)` → 앞 쓰레기 버림 → 길이 바이트 기다림 → 길이 이상하면 1바이트 버림 → 모자라면 기다림 → checksum 틀리면 **1바이트만** 버림 → 맞으면 꺼내고 프레임만큼 삭제
- 시간은 **정수 ms**, 순회 중 삭제는 `for k in list(d)`, NaN은 먼저 `math.isnan`

### 4.5 후속 질문

| 질문 | 답 |
|---|---|
| "In C?" | 고정 배열 + head/tail/count, `% cap`(2의 거듭제곱이면 mask), 반환 코드 |
| "ISR producer, main consumer?" | count를 둘 다 고치면 race → **SPSC**: head는 생산자만, tail은 소비자만 + atomic / critical section |
| "How would you test it with pytest?" | 같은 assert를 `test_` 함수로, `@pytest.mark.parametrize`로 정책 두 개, 랜덤 연산을 `deque` 모델과 비교 |
| "Faster bulk?" | wrap 지점까지 한 번 + 나머지 한 번 (C면 `memcpy` 두 번) |

---

## 5. 마지막 체크

- [ ] 신분증 · 법적 이름 · 1100 Knox St, Torrance · **10:05 도착**
- [ ] 투어(Michael Honar) 질문: "Which of these stations does the test team own today?" · "Where does a board go when it fails a test here?"
- [ ] 점심도 면접: 발표 직후 "Was there anything in the talk you'd like me to go deeper on?"
- [ ] 1:1 사이 물 한 잔 · 직전 세션 잊기
- [ ] 마지막 Jon 세션 전 1분: 이 페이지 4.2 · 4.4만 다시

## 6. 더 볼 곳 (시간 남으면)

[K02 1:1 압축 정리](2026-10-20_K02_final_cram_three_1on1s.md) · [K03 따라 치는 코딩](2026-10-21_K03_type_along_coding.md) · [Q01 서브시스템 FW](../qa/2026-10-20_Q01_fw_side_of_subsystem_integration.md) · [Q02 overflow](../qa/2026-10-20_Q02_screen_missed_overflow_case.md) · [K01 일정 · 패널](2026-10-14_K01_onsite_day_schedule_panel.md)
