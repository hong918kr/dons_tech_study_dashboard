# 🏗️ N06 · Generic System Design — 임베디드 시스템 설계 1:1

> 리크루터 안내: 온사이트 1:1 중 하나는 **system design**이고, 드론 개발자가 많지 않아서 **드론 설계가 아니라 generic한 문제**가 나온다 [확인됨 2026-09-16 Devin]. 이 노트는 ① 어떤 문제에도 쓰는 45분 진행 틀 ② ASCII 블록도 그리는 법 ③ 나올 만한 문제 7개의 풀이(요구사항 질문 → 가정 숫자 → 블록도 → 설계 결정과 trade-off → 실패 모드 → 테스트 → 꼬리 질문 → 영어 2분 요약) ④ Neros형 "STM32 + Linux companion" 참조 아키텍처로 구성했다.

## 0. 이 인터뷰가 보는 것

- **정답이 아니라 사고 과정**을 본다. 요구사항을 먼저 묻고, 숫자로 가정하고, trade-off를 말로 드러내는지
- 임베디드 system design은 웹 system design과 다르다. 서버 증설이 아니라 **고정된 RAM·flash·CPU·대역폭·전력 안에서** 설계한다. 그리고 **전원이 갑자기 꺼지고, 링크가 끊기고, 하드웨어가 거짓말을 한다**
- 시니어 신호: ① 숫자로 예산을 세운다 ② 실패 모드를 먼저 꺼낸다 ③ 관측성(로그·텔레메트리·카운터)을 설계에 넣는다 ④ **어떻게 테스트할지**까지 말한다
- 이전 면접관 성향: Adam Kibit(Director of Firmware)은 자동차 임베디드 플랫폼 출신이고, Michael Honor(FW Test 리드)는 테스트 관점이다 → **테스트 가능성과 실패 처리**를 꼭 넣는다

## 1. 45분 진행 틀 (모든 문제에 적용)

| 단계 | 시간 | 할 일 | 영어 문장 |
|---|---|---|---|
| 1. 요구사항 확인 | 5분 | 기능, 숫자(rate, 크기, 지연), 환경, 제약을 묻는다 | "Before I draw anything, let me pin down the requirements and a few numbers." |
| 2. 가정 선언 | 2분 | 모르는 숫자는 내가 정하고 화이트보드 구석에 적는다 | "I'll assume X; tell me if that's off and I'll adjust." |
| 3. 블록도 | 5분 | 큰 상자 4~7개, 화살표에 데이터 이름과 rate | "Here's the high-level picture — data flows left to right." |
| 4. 데이터 흐름과 인터페이스 | 8분 | 버스, 버퍼, 프로토콜, 소유권(누가 쓰고 누가 읽나) | "Let me walk one sample through the system end to end." |
| 5. 자원 예산 | 5분 | CPU %, RAM, flash, 대역폭, 전력을 숫자로 | "Let me sanity-check the budget: ..." |
| 6. 실패 모드와 복구 | 8분 | 전원 차단, 링크 끊김, 버퍼 넘침, 손상, 리셋 | "Now the interesting part — what happens when things go wrong." |
| 7. 관측성 | 3분 | 카운터, 로그, 리셋 원인, 텔레메트리 | "I'd want counters for every drop and every retry, so the field tells us what's happening." |
| 8. 테스트와 검증 | 4분 | 유닛 → SIL → HIL → 고장 주입 → soak | "Here's how I'd prove it works, including the failure paths." |
| 9. 확장과 진화 | 3분 | 버전 관리, 새 하드웨어, 10배 규모 | "If the rate goes up 10x or we add a new board, here's what changes." |

- 면접관이 중간에 방향을 바꾸면 **틀을 버리고 따라간다**. 틀은 길을 잃었을 때 돌아오는 곳이다
- 2분마다 "Does this match what you had in mind?"로 확인한다
- 모르는 건 "I haven't built that specific piece, but here's how I'd reason about it"

### 1단계에서 거의 항상 묻는 질문 (영어)

- "What's the data rate and sample size? Peak versus average?"
- "What's the latency requirement — is this hard real-time, or best effort?"
- "What hardware do we have — MCU class, RAM, flash, any external storage?"
- "Can power be lost at any moment? Is there a hold-up capacitor or a power-fail interrupt?"
- "How many units in the field, and how do we update them?"
- "What happens if this subsystem fails — is it safety-critical, or degrade gracefully?"
- "Who consumes the output — a person, another team's software, a ground station?"

## 2. ASCII 블록도 스타일 가이드

화이트보드든 공유 문서든 같은 규칙으로 그린다.

- 상자는 **역할 이름**으로 (칩 이름 아님): `[IMU]`, `[Ring Buffer]`, `[Flash Writer Task]`
- 화살표 위에 **무엇이 얼마나**: `--(1 kHz x 32 B)-->`
- 경계(칩, 프로세스, 무선 링크)는 `|` 또는 `====`로 구분
- ISR / 태스크 / 하드웨어를 구분: `(ISR)`, `(task, prio 3)`, `(DMA)`
- 왼쪽 → 오른쪽 = 데이터 흐름, 위 → 아래 = 제어/우선순위

```text
  [Sensor] --SPI(DMA)--> [Double Buf] --(ISR: swap)--> [Filter task] --> [Ring Buf] --> [Logger task] --> [Flash]
                                                                             |
                                                                             +--> [Telemetry task] --UART--> [Radio]
```

## 3. 문제 1 · 고속 센서 데이터 로깅 시스템

**문제 (영어로 들을 말)**: "Design a data logger for a device that samples sensors at a high rate and stores the data on flash. It must survive sudden power loss."

### 요구사항 질문
- "How many channels, what sample size, what rate? Do all channels share one rate?"
- "How long must we record — minutes, hours, the whole mission?"
- "Is losing the last few hundred milliseconds acceptable on power loss, or must every sample survive?"
- "What storage — internal flash, SPI NOR, NAND, SD card? Any file system?"
- "Does logging have to coexist with a control loop that must never be delayed?"

### 가정 숫자 (화이트보드 구석에 적기)

| 항목 | 가정 | 계산 |
|---|---|---|
| 센서 | IMU 6축 int16 + 타임스탬프 4 B | 레코드 16 B |
| rate | 1 kHz | 16 KB/s |
| 기타 이벤트 | 100 Hz × 32 B | 3.2 KB/s |
| 합계 | 약 20 KB/s | 1시간 = 72 MB |
| 저장장치 | SPI NOR 128 MB 또는 SD | 페이지 256 B, 섹터 erase 4 KB |
| 쓰기 지연 | page program 수백 µs, sector erase 수십 ms | erase 동안 버퍼가 버텨야 한다 |
| 전원 차단 후 유지 | hold-up 약 5 ms [가정] | 마지막 버퍼만 flush 가능 |

### 블록도

```text
 (ISR/DMA, 1 kHz)          (lock-free SPSC)            (task, low prio)            (SPI DMA)
 [Sensor read] --16B--> [RAM Ring Buffer 64 KB] --> [Logger: batch to 4 KB block] --> [Flash]
       |                       |                         |   + header(seq, crc, ver)
       |                       +-- drop counter          +-- [Erase-ahead: keep N free sectors]
 [Power-fail IRQ] ----------------------------------------> flush partial block + mark
```

### 핵심 설계 결정과 trade-off

| 결정 | 선택 | 대안 | 이유 |
|---|---|---|---|
| 생산자-소비자 분리 | **SPSC ring buffer** (ISR 생산, 태스크 소비) | 큐 + mutex | ISR에서 lock 금지, O(1), 제어 루프 지연 없음 |
| 버퍼 크기 | 최악 flash 지연 × 입력 rate × 2 | 평균 기준 | erase 50 ms × 20 KB/s = 1 KB → 여유 두고 수 KB~수십 KB |
| 넘칠 때 정책 | **새 데이터 drop + 카운터** (또는 overwrite-oldest) | 생산자 블록 | 제어 루프를 절대 막지 않는다. 블랙박스 용도면 overwrite-oldest가 더 유용할 수 있다 |
| 쓰기 단위 | 4 KB 블록, 헤더에 seq/CRC/포맷 버전 | 레코드마다 쓰기 | 쓰기 횟수·wear 감소, 블록 단위 무결성 검사 |
| 파일 시스템 | **raw append-only 로그 구조** | FAT on SD | FAT은 전원 차단 시 FAT 테이블 손상 위험. 필요하면 littlefs 같은 power-safe FS [추정: 선택지 예시] |
| erase | 미리 지워 둔 섹터 풀 (erase-ahead) | 필요할 때 erase | erase 지연을 쓰기 경로에서 분리 |
| wear | 순환 기록 (log-structured), 섹터 순서대로 돌기 | 같은 위치 덮어쓰기 | 자연스러운 wear leveling |
| 포맷 | 이진 + 스키마 버전 | 텍스트 | 대역폭·저장 효율. 파서는 PC 쪽 Python |

### 실패 모드

| 실패 | 결과 | 대응 |
|---|---|---|
| 쓰는 도중 전원 차단 | 마지막 블록이 반쯤 써짐 | 블록 CRC로 탐지 → 부팅 시 마지막 유효 seq 다음부터 재개 |
| flash 지연 폭증 (erase, 노화) | 링 버퍼 넘침 | drop 카운터, 텔레메트리 경고, 버퍼 크기 재산정 |
| flash 가득 참 | 기록 중단 | 정책 결정: 가장 오래된 것부터 덮기 vs 정지 |
| 비트 오류, bad block | 읽기 실패 | 블록 CRC, bad block 테이블 (NAND면 ECC 필수) |
| 시간 역행 (RTC 리셋) | 정렬 꼬임 | 단조 증가 seq + 부팅 카운터를 키로 사용 |

### 테스트
- 유닛: 링 버퍼 wrap/full/empty, 블록 인코더, CRC
- SIL: flash를 RAM 시뮬레이터로 바꾸고 **임의 시점 전원 차단**을 수천 번 → 복구 후 유효 seq가 연속인지
- HIL: 실제 보드에서 릴레이로 전원 차단 반복, 최대 rate + erase 동시 부하에서 drop 카운터 0 확인
- Soak: 저장장치 한 바퀴 이상 순환 기록 (wrap 경계)

### 꼬리 질문
- "Why not just use FAT on an SD card?" → 전원 차단 시 메타데이터 손상. 쓰려면 저널링/power-safe FS 또는 append-only 구조 + 주기적 sync
- "How do you size the ring buffer?" → 최악 소비 지연 × 생산 rate × 안전 계수. **최악 지연을 측정해서** 숫자를 확정한다
- "Overwrite-oldest or drop-newest?" → 용도에 따라. 사고 직전 데이터가 중요한 블랙박스는 overwrite-oldest, 연속성이 중요한 분석 로그는 drop-newest + 카운터
- "How do you keep logging from hurting the control loop?" → ISR에서는 복사만, 포맷팅·쓰기는 저우선 태스크, 측정으로 루프 지터 확인

### 영어 2분 요약
> I'd split it into a fast producer and a slow consumer. The sensor ISR or DMA completion writes fixed-size binary records into a lock-free single-producer, single-consumer ring buffer — no locks, constant time, so the control loop is never delayed. A low-priority logger task drains it in 4 KB blocks, each with a sequence number, format version, and CRC, and writes them append-only to flash, with a pool of pre-erased sectors so erase latency never sits in the write path. I size the ring buffer from the measured worst-case flash latency times the input rate, with margin. On power loss, the power-fail interrupt flushes what it can, and on boot I scan for the last block with a valid CRC and resume after it, so we lose at most a partial block. Every drop is counted and reported in telemetry. To test it, I'd inject power cuts at random points thousands of times in simulation and on a relay-controlled rig, and check that sequence numbers are continuous after recovery.

- **Don 연결**: SK hynix에서 **NVMe telemetry 기반 time-sensitive 디버그 기능** — "문제 순간의 상태를 잃기 전에 캡처" (Don: 캡처 트리거와 버퍼 크기를 어떻게 정했는지). SSD FW 자체가 **전원 차단에도 데이터를 지키는** 시스템이다

## 4. 문제 2 · 대규모 fleet 펌웨어 업데이트 (OTA)

**문제**: "Design the firmware update system for tens of thousands of devices in the field. An update must never brick a device."

### 요구사항 질문
- "How do updates reach the device — over a radio link, USB at a depot, or through a companion computer?"
- "How big is the image, and how much flash do we have — room for two copies?"
- "Do we need to verify authenticity — is a malicious image a threat?" (방산이면 **예**)
- "Can we roll back? Are there versions we must never downgrade to?"
- "How do we update thousands of units — all at once, or staged?"

### 가정 숫자

| 항목 | 가정 |
|---|---|
| 이미지 | 512 KB (MCU) |
| flash | 2 MB → A/B 두 슬롯 + bootloader + 설정 영역 가능 |
| 링크 | 저속 무선 수십 kbps 또는 USB/companion 경유 |
| fleet | 수만 대, 업데이트는 정비·충전 중 [추정] |

### 블록도

```text
 [Build server] --sign(private key)--> [Signed image + manifest(ver, hash, min_ver)]
        |
 [Update server / depot tool] --chunks + resume--> [Device]
                                                      |
                     +--------------------------------+--------------------------+
                     |                                |                          |
               [Bootloader (immutable)]        [Slot A: running]          [Slot B: download target]
                verify sig + hash                                         write -> verify -> mark "pending"
                choose slot, watchdog trial boot  <---- reboot ----
                confirm or roll back
```

### 핵심 설계 결정과 trade-off

| 결정 | 선택 | 대안 | 이유 |
|---|---|---|---|
| 슬롯 | **A/B (dual bank)** | 단일 슬롯 + 복구 이미지 | 다운로드 중 전원 차단에도 기존 이미지가 살아 있다. 대가는 flash 2배 |
| 검증 | **서명(공개키) + 해시**, bootloader가 검증 | CRC만 | CRC는 우연한 손상만 막는다. 악의적 이미지는 서명으로 |
| 전환 | **trial boot**: pending → 새 이미지가 스스로 헬스체크 후 confirm → 실패하면 watchdog 리셋 → bootloader가 이전 슬롯으로 | 즉시 영구 전환 | 부팅은 되지만 기능이 깨진 이미지에서 복구 |
| 롤백 방지 | 최소 허용 버전(anti-rollback 카운터) | 무제한 롤백 | 보안 취약 버전으로 되돌리는 공격 차단 |
| 전송 | 청크 + 재개(resume) + 전체 해시 | 한 번에 | 저속·끊기는 링크 |
| 배포 | **staged rollout**: 내부 → 1% → 10% → 전체, 지표로 게이트 | 일괄 | 문제를 소수에서 먼저 잡는다 |
| bootloader | 작고 거의 안 바꿈, 자체 업데이트는 매우 신중 | 자주 업데이트 | bootloader가 깨지면 벽돌 |

### 실패 모드

| 실패 | 대응 |
|---|---|
| 다운로드 중 전원 차단 | 기존 슬롯 그대로. 재개 |
| 쓰기 완료, 검증 실패 | pending 표시 안 함 |
| 새 이미지가 부팅 루프 | trial boot 횟수 초과 → 이전 슬롯 |
| 새 이미지가 부팅은 되지만 핵심 기능 실패 | confirm 전 자체 헬스체크(센서, 링크) |
| 설정 포맷이 바뀜 | 설정 마이그레이션은 confirm 이후, 이전 포맷도 읽을 수 있게 |
| 키 유출 | 키 회전 계획, HSM에 개인키 [일반론] |

### 테스트
- 업데이트 중 **랜덤 시점 전원 차단** 수백~수천 회 (HIL 릴레이)
- 손상 이미지, 서명 틀린 이미지, 다운그레이드 시도 → 거부 확인
- 부팅은 되지만 헬스체크 실패하는 이미지를 일부러 만들어 롤백 확인
- 구버전 → 신버전, 2단계 건너뛴 업그레이드 경로 매트릭스

### 꼬리 질문
- "What if we don't have flash for two slots?" → 외부 flash에 다운로드 후 bootloader가 복사(복사 중 전원 차단 대비 상태 기록), 또는 최소 복구 이미지 + 메인 슬롯
- "How does the device know the new image is good?" → trial boot + 자체 헬스체크 + 명시적 confirm
- "Secure boot on an MCU?" → bootloader에서 서명 검증, 디버그 포트 잠금(STM32라면 RDP 같은 readout protection [추정: 제품별 확인]), 키는 OTP/보호 영역
- "Staged rollout metrics?" → 부팅 성공률, 롤백 횟수, 리셋 원인, 에러 카운터

### 영어 2분 요약
> The core rule is that the device always has a known-good image to fall back to. I'd use A/B slots: download into the inactive slot in resumable chunks, verify a signature and hash, then mark it pending and reboot. A small, rarely-changed bootloader verifies the signature again and does a trial boot under a watchdog. The new image runs a health check — sensors, links — and only then confirms itself. If it boot-loops or never confirms, the bootloader goes back to the previous slot. I'd add an anti-rollback minimum version so nobody can install an old vulnerable image. For the fleet, a staged rollout gated by boot success and rollback rates. And the test that matters most is power loss at random points during the update, hundreds of times on a relay-controlled rig.

## 5. 문제 3 · MCU ↔ companion computer 통신 프로토콜

**문제**: "Design the communication protocol between a microcontroller and a Linux computer over UART."

### 요구사항 질문
- "What traffic — high-rate sensor streaming, low-rate commands, or both?"
- "What baud rate or link? Full duplex?"
- "Do commands need guaranteed delivery? Does streaming data?"
- "Will both sides be updated independently — do we need version compatibility?"
- "What's the latency requirement for commands?"

### 가정 숫자

| 항목 | 가정 | 계산 |
|---|---|---|
| 링크 | UART 921600 baud, 8N1 | 약 92 KB/s (10비트/바이트) |
| 스트림 | 상태 메시지 200 Hz × 64 B | 12.8 KB/s (약 14%) |
| 명령 | 10 Hz × 32 B, ack 필요 | 작음 |
| 여유 | 50% 이하 사용 목표 | 재전송·버스트 대비 |

### 프레임 형식

```text
 | SYNC 0xA5 | LEN (2B) | VER (1B) | TYPE (1B) | SEQ (1B) | FLAGS (1B) | PAYLOAD (LEN B) | CRC16 (2B) |
   ^ resync    ^ bounded   ^ proto     ^ msg id    ^ dup/loss  ^ ack-req                       ^ over VER..PAYLOAD
```

- 대안 framing: **COBS** (0x00을 구분자로, 바이트 스터핑 오버헤드 최대 약 0.4%) 또는 SLIP. sync 바이트 방식은 payload 안의 sync 값 때문에 **길이 + CRC로 검증**해야 한다

### 핵심 설계 결정과 trade-off

| 결정 | 선택 | 이유 |
|---|---|---|
| framing | 길이 필드 + CRC16, 또는 COBS | 임의 지점에서 재동기화 가능해야 한다 |
| 무결성 | CRC-16 (또는 CRC-32) | UART 비트 오류 탐지. 체크섬보다 강함 |
| 신뢰성 | **메시지 종류별 정책**: 명령 = seq + ack + 재전송, 스트림 = fire-and-forget + 최신값만 의미 | 스트림을 재전송하면 오래된 데이터로 링크가 막힌다 |
| 순서·중복 | seq 번호, 수신 측에서 중복 제거, 손실 카운트 | 손실률 관측 |
| 버전 | VER 필드 + TYPE별 payload에 **뒤에만 필드 추가**, 길이로 구분 | 양쪽 독립 업데이트 |
| 흐름 제어 | 수신 측 버퍼 상태를 주기적으로 알림 또는 HW flow control (RTS/CTS) | 리눅스 쪽이 바쁠 때 |
| heartbeat | 양방향 주기 메시지, 타임아웃 시 안전 동작 | 링크 끊김 탐지 |
| 직렬화 | 명시적 little-endian 인코딩 (packed struct 직접 전송 금지) | 컴파일러·아키텍처 차이 제거 |

### 실패 모드
- 노이즈로 바이트 깨짐 → CRC 실패 → 프레임 버림 + 카운터, 다음 SYNC에서 재동기화
- 바이트 손실(overrun) → 길이 불일치 → 재동기화
- 리눅스 프로세스 재시작 → heartbeat 끊김 → MCU는 안전 모드, 재연결 시 버전 handshake
- 버전 불일치 → handshake에서 탐지, 모르는 TYPE은 무시하고 카운트

### 테스트
- 퍼징: 랜덤 바이트, 잘린 프레임, CRC 틀린 프레임 → 파서가 절대 크래시하지 않고 재동기화
- 최대 부하: 스트림 + 명령 동시, ack 지연 분포
- 고장 주입: 바이트 drop/flip을 주입하는 프록시 (Python으로 쉽게)
- 버전 호환 매트릭스: 구 MCU + 신 리눅스, 그 반대

### 꼬리 질문
- "Why not just send C structs?" → padding, endianness, 컴파일러 차이. 명시적 인코딩 또는 스키마 기반 코드 생성
- "Would you use MAVLink or protobuf instead?" → 기존 생태계(GCS, PX4)와 맞추려면 MAVLink가 합리적. 커스텀이 필요하면 작은 스키마 + 코드 생성. **build vs adopt** 판단
- "How do you handle a command that must not execute twice?" → seq 기반 중복 제거, 멱등 명령 설계

### 영어 2분 요약
> I'd make every frame self-delimiting and verifiable: a sync byte, a bounded length, a version and message type, a sequence number, then the payload and a CRC-16. The receiver can drop into the stream at any point and resynchronize. Reliability depends on the message: commands get sequence numbers, acks, and retries with de-duplication; streaming state is fire-and-forget, because a retransmitted stale sample just clogs the link. Payloads are explicitly encoded little-endian, never raw structs, and new fields are only appended, so both sides can be updated independently. A bidirectional heartbeat detects link loss, and the MCU falls back to a safe behavior. Every CRC failure, resync, and drop is counted. I'd fuzz the parser with random and truncated frames — it must never crash — and run a fault-injecting proxy in CI.

- **Don 연결**: Solidigm **멀티코어(R8/R82/M0+) FW** — 코어 간 메시지 전달 (Don: mailbox/shared memory 방식). SK hynix 챔버 SDK의 **UART 시퀀스 자동화**

## 6. 문제 4 · 센서 데이터 acquisition 파이프라인

**문제**: "Design the pipeline that reads several sensors at different rates, filters the data, and delivers time-aligned samples to an application."

### 요구사항 질문
- "Which sensors, which buses, which rates? Do they have FIFOs and data-ready interrupts?"
- "How tight must time alignment be — microseconds or milliseconds?"
- "What latency from sample to consumer is acceptable?"
- "What does the consumer need — every sample, or the latest value?"

### 가정 숫자

| 센서 | 버스 | rate | 비고 |
|---|---|---|---|
| IMU (gyro/accel) | SPI | 8 kHz 내부, 1~4 kHz 읽기 [가정] | data-ready IRQ, FIFO |
| 기압계 | I2C | 50 Hz | 느림, 변환 시간 있음 |
| 자력계 | I2C | 100 Hz | |
| GPS | UART | 10 Hz | NMEA/UBX, PPS 핀 |

### 블록도

```text
 [IMU] --DRDY IRQ--> (ISR: timestamp=TIM counter) --SPI DMA burst--> [Double buffer A/B]
                                                                         | (DMA complete ISR: swap)
                                                                         v
 [Baro/Mag] <--I2C (scheduled task, 50/100 Hz)            [Filter task: LPF/notch, decimate]
 [GPS] --UART DMA circular--> [NMEA/UBX parser] --+                      |
 [PPS pin] --capture timer--> [time base sync] ---+-----> [Sample bus: latest-value + ring per sensor] --> [Consumers]
```

### 핵심 설계 결정과 trade-off

| 결정 | 선택 | 이유 |
|---|---|---|
| 읽기 트리거 | data-ready 인터럽트 + DMA | 폴링 대비 지연·지터 감소, CPU 절약 |
| 버퍼 | **double buffer (ping-pong)** 또는 DMA circular + half/complete 인터럽트 | DMA가 한쪽을 채우는 동안 CPU가 다른 쪽 처리 |
| 타임스탬프 | **인터럽트 시점에 하드웨어 타이머 값**, 버스 전송 완료 시점 아님 | 전송 지연·지터 제거 |
| 시간 기준 | 하나의 단조 증가 타이머, GPS PPS로 보정 | 센서 간 정렬 |
| rate 불일치 | 센서별 최신값 + 타임스탬프, 소비자가 보간/보유(sample-and-hold) | 느린 센서를 기다리지 않는다 |
| 필터 | 고정소수점 또는 FPU float, 고정 실행 시간 | Cortex-M4F/M7은 FPU 있음 [일반론] |
| 소비자 인터페이스 | "latest value" (seqlock/더블 버퍼) + 필요 시 전체 스트림용 ring | 제어 루프는 최신값, 로거는 전체 |

### 실패 모드
- 센서 무응답/고정값 → 타임아웃·변화 없음 탐지, 헬스 플래그
- DMA overrun (처리 늦음) → 카운터, 처리 시간 측정
- I2C bus hang → bus recovery (SCL 9클럭 토글 후 STOP)
- 시간 점프 (PPS 손실) → 자체 타이머로 계속, 동기 상태 플래그를 데이터에 기록

### 테스트
- GPIO 토글 + 로직 애널라이저로 ISR → DMA 완료 → 필터 → 소비자 지연·지터 측정
- 센서 시뮬레이터(HIL 주입)로 알려진 신호(사인파)를 넣고 필터 출력 진폭·위상 확인
- 고장 주입: 센서 분리, I2C 라인 low 고정, 잘못된 값

### 꼬리 질문
- "Where do you timestamp, and why?" → data-ready 인터럽트 진입 시점. 버스 전송은 가변 지연
- "What if the consumer is slower than the producer?" → 최신값만 필요하면 덮어쓰기, 전부 필요하면 ring + drop 카운터
- "How do you know the filter is right?" → 오프라인 Python으로 같은 계수 검증, HIL 주입 신호로 주파수 응답

### 영어 2분 요약
> Each fast sensor is interrupt-driven: the data-ready interrupt captures a hardware timer value as the timestamp, then kicks off a DMA burst read into a ping-pong buffer, so the CPU processes one half while DMA fills the other. Slow sensors on I2C are read by a scheduled task. Everything is timestamped against one monotonic timer, corrected by GPS PPS when available. Rates differ, so I don't force them into lockstep — each sensor publishes a latest value with its timestamp, and consumers interpolate or hold. Control loops read latest values through a lock-free double buffer; loggers get the full stream through a ring buffer with drop counters. I'd verify latency and jitter with GPIO toggles on a logic analyzer, and the filters by injecting known signals on a HIL bench.

- **Don 연결**: Solidigm **SoC verification — DMA, I2C, SPI, SRAM/DRAM bring-up**, SK hynix **Xtensa NAND 데이터패스 FW** (Don: 데이터패스 동시성에서 기억나는 이슈)

## 7. 문제 5 · 디바이스 config / parameter 저장 시스템

**문제**: "Design a configuration store for an embedded device. Parameters change over firmware versions, and the device can lose power at any time."

### 요구사항 질문
- "How many parameters, what types, how big in total?"
- "How often are they written — once at the factory, or frequently at runtime?"
- "Who writes them — the user over a ground station, factory tools, the firmware itself?"
- "What must happen if the stored config is corrupt?"

### 가정 숫자
- 파라미터 수백 개, 총 4 KB 이하, 쓰기는 사용자 변경 시(하루 수 회) → 내부 flash 섹터 2개
- STM32 내부 flash는 섹터 단위 erase, 쓰기 횟수 제한(대략 1만 회급 [추정: 제품별 데이터시트])

### 블록도

```text
 [App / GCS / CLI] --set(key,val)--> [Param API: type+range check] --> [RAM shadow copy]
                                                                           | commit()
                                                                           v
                                     [Serializer: TLV + schema_ver + CRC32] --> [Slot A | Slot B]  (alternate, seq#)
 Boot: read both slots -> pick highest valid seq -> migrate if schema_ver old -> else defaults
```

### 핵심 설계 결정과 trade-off

| 결정 | 선택 | 이유 |
|---|---|---|
| 원자성 | **두 슬롯 번갈아 쓰기 + seq + CRC** | 쓰는 도중 전원 차단 → 다른 슬롯이 유효. "old or new, never corrupt" |
| 형식 | **TLV (tag-length-value)** 또는 key ID + 타입 | 모르는 tag는 건너뛰기 → 전후방 호환 |
| 스키마 버전 | 헤더에 schema_ver, 부팅 시 마이그레이션 함수 체인 | FW 업그레이드에서 파라미터 추가·이름 변경 |
| 기본값 | 코드에 테이블, 저장값 없거나 범위 밖이면 기본값 | 손상 시 안전 부팅 |
| 검증 | set 시점 타입·범위 체크 | 잘못된 값이 저장되지 않게 |
| wear | 쓰기 빈도 제한(디바운스), 섹터 안에서 append 후 가득 차면 compaction | 쓰기 횟수 제한 |
| 출하 데이터 | **calibration·serial은 별도 영역**, 사용자 리셋으로 지워지지 않게 | factory 데이터 보호 (N08) |

### 실패 모드와 테스트
- 쓰는 도중 전원 차단 → 부팅 시 이전 슬롯 사용. **테스트: 랜덤 시점 전원 차단 수천 회**
- 양쪽 슬롯 모두 손상 → 기본값 + 경고 플래그 + 텔레메트리 보고
- 구 FW 스키마 → 마이그레이션 테스트 매트릭스 (v1→v3, v2→v3)
- 다운그레이드 → 구 FW가 새 tag를 무시하고 동작하는지

### 꼬리 질문
- "Why not a key-value file system?" → 가능(littlefs 등). 파라미터가 적으면 두 슬롯이 더 단순하고 검증 쉽다
- "How do you rename a parameter?" → tag ID는 절대 재사용하지 않고, 마이그레이션에서 옛 tag → 새 tag

### 영어 2분 요약
> I'd keep a RAM shadow copy that the application reads, and commit it as a single serialized blob — TLV entries, a schema version, a sequence number, and a CRC — alternating between two flash slots. On boot I read both, take the highest sequence with a valid CRC, and run migrations if the schema is older. That gives the invariant that after any power loss you get the old config or the new one, never a corrupt mix. Unknown tags are skipped, so older and newer firmware can read each other's data. Defaults live in code, and factory data like calibration and serial numbers live in a separate protected region that a user reset never touches. The test is randomized power cuts during commit, plus a migration matrix across firmware versions.

- **Don 연결**: Solidigm **error reporting/handling scheme** — 여러 모듈이 쓰는 인터페이스를 버전 변화 속에서 유지 (Don: 실제 확장 사례)

## 8. 문제 6 · Factory test & provisioning 시스템

**문제**: "We build over a thousand units a week. Design the system that tests and provisions each board on the production line."

이 문제는 **Neros가 실제로 필요한 것**이다. Michael Honor: "factory test firmware가 아직 없고 나중에 필요하다" [확인됨 2026-10-01]. 자세한 내용은 [N08 factory test firmware](2026-10-01_N08_factory_test_firmware.md).

### 요구사항 질문
- "What's the line rate and how many shifts? What's the test time budget per station?"
- "What gets tested at each stage — bare board, assembled vehicle, radio?"
- "What has to be written to each unit — serial number, keys, calibration?"
- "Where do results go — is there an MES or a database?"
- "Who runs the station — technicians, so it must be simple pass/fail?"

### 가정 숫자

| 항목 | 계산 |
|---|---|
| 주당 1,200대 [확인됨: 공개 보도 기준 생산량] | 주 5일 → 하루 240대 |
| 1교대 8시간, 가동률 75% | 21,600초 / 240 = **takt 약 90초** |
| FCT 테스트 180초라면 | 병렬 스테이션 2대 이상 |
| 2028 목표 연 100만 대 [확인됨: 회사 발표] | 주 약 19,000대 → 약 16배 → 스테이션 수와 테스트 시간이 핵심 비용 |

### 블록도

```text
 [Barcode scan] --> [Station PC (Python): sequencer, limits, UI pass/fail]
                         |  USB CDC / UART (test protocol)          | instruments: PSU, DMM, RF power meter, fixture IO
                         v                                          v
                    [DUT: factory test FW]  <-- pogo-pin fixture --> [Fixture: loopbacks, loads, motor-out capture]
                         |
                    self-tests -> results JSON --> [Station] --> [Results DB / MES]: serial, FW hash, station id, limits ver, measurements
                    provisioning: serial, calibration, keys --> protected flash / OTP
```

### 핵심 설계 결정과 trade-off

| 결정 | 선택지 | trade-off |
|---|---|---|
| 테스트 FW 형태 | 별도 test image → 테스트 후 production image 플래시 / production FW 안의 test mode | 별도 이미지는 깔끔하고 production에 테스트 코드가 안 남지만 플래시 2번. test mode는 빠르지만 **잠금 해제 경로가 보안 위험** |
| 판정 위치 | 측정값을 PC로 보내 PC가 limit 판정 | limit을 FW 재빌드 없이 바꿀 수 있다 |
| 프로토콜 | 텍스트 명령(사람이 디버그 가능) vs 이진(빠름) | 초기엔 텍스트 + 구조화 응답(JSON 한 줄), 병목이면 이진 |
| 추적성 | serial ↔ FW hash ↔ station ↔ limits 버전 ↔ 측정값 전부 저장 | 필드 반품 분석, 방산 추적성 |
| 시간 | 병렬 가능한 테스트는 동시에, 느린 테스트는 별도 스테이션 | takt 맞추기 |

### 실패 모드와 테스트
- 픽스처 접촉 불량 → 거짓 실패. **스테이션 자체 점검(golden unit)** 을 교대 시작마다
- limit이 너무 빡빡/느슨 → 거짓 실패/불량 출하. 초기 데이터로 분포 보고 조정, GR&R
- 프로비저닝 중 전원 차단 → 재실행 가능하게(멱등), 중복 serial 방지는 서버에서
- 테스트 스테이션 소프트웨어 자체 회귀 → 스테이션 코드도 CI, 시뮬레이션 DUT로 테스트

### 꼬리 질문
- "How do you keep test time under the takt?" → 테스트별 시간·검출력 측정, 실패 이력 없는 테스트는 샘플링, 병렬화
- "How do you protect keys during provisioning?" → 키는 서버/HSM에서 생성·주입, 스테이션 로그에 남기지 않음, 주입 후 디버그 포트 잠금

### 영어 2분 요약
> A Python sequencer on a station PC drives the line: scan the barcode, talk to factory test firmware on the board over USB or UART, and control the fixture and instruments. The DUT firmware runs self-tests and reports raw measurements; the station compares them against versioned limits, so limits change without a firmware rebuild. After passing, the station provisions the serial number, calibration, and keys into protected storage. Every result is stored with the unit serial, firmware hash, station ID, and limits version — that's what makes field returns traceable. The design constraint is takt time: at about 240 units a day on one shift, that's roughly 90 seconds per unit, so test time and parallel stations are the main levers. At Apple I designed factory test-node architecture, and the lesson was to put stress-based cases where they find the most defects per second.

- **Don 연결**: Apple **factory test-node 아키텍처**, SK hynix **챔버 테스트 플랫폼 SDK**, 레쥬메 Summary의 **SSD/MFG Firmware** (Don: MFG FW에서 맡은 범위)

## 9. 문제 7 · HIL 테스트 팜 + CI

**문제**: "Design the infrastructure that runs hardware-in-the-loop tests for every firmware change across several product variants."

### 요구사항 질문
- "How many product variants and board revisions? How many rigs can we afford?"
- "How many merge requests a day, and what's the acceptable feedback time?"
- "What can be simulated (SITL) versus what needs real hardware?"
- "Who owns rig maintenance?"

### 가정 숫자

| 항목 | 가정 | 계산 |
|---|---|---|
| MR | 하루 30개 | |
| HIL smoke | 10분/회 | 30 × 10 = 300 rig-분 → 리그 1대면 5시간, **변형 3종 × 리그 2대**면 피드백 지연 감소 |
| 야간 회귀 | 3시간/변형 | 야간 시간대 안에 |
| 실패율 목표 | 인프라 실패 < 1% | 신뢰가 무너지면 아무도 결과를 안 본다 |

### 블록도

```text
 [Git MR] --> [CI: build all targets] --> [Unit + SITL (cloud runners)] --> [HIL queue]
                                                                              |  resource lock per rig
                       +-------------------------+------------------------------+
                       v                         v                              v
                 [Rig A: FC rev2]          [Rig B: FC rev3]              [Rig C: radio/handset]
                 PSU relay, USB hub,       RC injection (CRSF),          RF shield box + attenuator
                 logic analyzer            motor-out capture
                       |
                 [Artifacts: logs, captures, FW hash, junit] --> [Dashboard: pass rate, flaky list, rig health]
```

### 핵심 설계 결정과 trade-off

| 결정 | 선택 | 이유 |
|---|---|---|
| 테스트 계층 | 유닛 → SITL → HIL smoke(MR) → HIL 전체(야간) → 비행 시험 | 비싼 자원을 필요한 곳에만 |
| 리그 할당 | 리그별 lock(예: GitLab resource_group), 라벨로 변형 매칭 | 동시 사용 방지 |
| 리그 헬스체크 | 테스트 전 자동 점검, 실패 시 **인프라 실패로 분류** + 리그 격리 | DUT 실패와 섞이면 신뢰 붕괴 |
| flaky 정책 | 실패율 측정, quarantine + 담당자, 자동 재시도 금지(또는 결과에 표시) | 간헐 실패는 진짜 버그일 수 있다 |
| 아티팩트 | 실패 시 로그·버스 캡처·FW hash 자동 보관 | 재현 없이 분석 |
| 복구 | 전원 사이클, 강제 재플래시(SWD) 자동화 | 벽돌 된 DUT를 사람 없이 복구 |

### 테스트 (테스트 인프라의 테스트)
- 알려진 나쁜 커밋을 넣어서 파이프라인이 잡는지 (canary)
- 리그 장애 주입(USB 분리, 전원 차단) → 인프라 실패로 정확히 분류되는지

### 꼬리 질문
- "How do you scale when you have 10x more MRs?" → smoke를 줄이고(영향 분석으로 관련 테스트만), 리그 추가, SITL 비중 확대
- "How do you know a red build is the firmware's fault?" → 리그 헬스체크, golden FW로 리그 재검증, 같은 커밋 재실행 통계

### 영어 2분 요약
> I'd layer it so expensive hardware only runs what simulation can't: unit tests and SITL on regular CI runners for every merge request, a short HIL smoke test per variant on real rigs, and the full HIL regression and soak tests nightly. Each rig is a locked resource with labels for its board variant, and it runs a health check before every job — a rig problem is reported as an infrastructure failure and the rig is pulled, never blamed on the firmware. On any failure the framework automatically saves logs, bus captures, and the firmware hash. Flaky tests get measured, quarantined with an owner, and never silently retried. Rigs must self-recover — power cycle and reflash over SWD — so the farm runs overnight without people.

- **Don 연결**: SK hynix **챔버 테스트 자동화 플랫폼 + SDK** (스케줄링, 디바이스 상태 모니터링, 다른 엔지니어가 사용) → 리그 팜과 거의 같은 구조. 자세한 스토리: [FW Test 준비 사이트 N09 S12](../../firmwareTestEngineerPrep/site/notes/2026-09-29_N09_resume_storylines.html)

## 10. Neros형 참조 아키텍처 — "STM32 + Linux companion"

Neros 공고와 면접에서 확인된 조각을 모으면 이런 구조가 그려진다. **세부는 전부 추정**이고, 면접에서 "이런 구조로 가정해도 될까요?"라고 확인하는 용도다.

- 확인된 사실: STM32 사용 [확인됨 2026-10-01 Michael], Tennessee FW 공고 "STM32 family" 필수 · Ukraine 공고 "ARM Cortex-M e.g. STM32" [확인됨 공고], Betaflight·PX4 포팅 (Flight Software 공고), embedded Linux 배포판·BSP·secure boot (Senior Embedded Linux 공고), autonomy runtime on embedded Linux + 가속기 (Autonomy Platform 공고), Bazel·cross-compile (Platform 공고), video FPGA (Tennessee 공고), ExpressLRS·HIL·GitLab CI (FW Test 공고)

```text
                       ===================== AIRCRAFT =====================
 [IMU/Baro/Mag/GPS] --SPI/I2C/UART--> [Flight Controller MCU (STM32, bare-metal/RTOS, Betaflight/PX4-class)]
                                              |  PWM/DShot --> [ESCs] --> motors
                                              |  UART (CRSF) <-- [RC/C2 radio receiver]
                                              |  UART/Ethernet (MAVLink or custom) 
                                              v
                                [Companion: embedded Linux SoM (autonomy, video, logging)]
                                              |  camera (MIPI/USB) --> [accelerator: inference]
                                              |  video encoder --> [video radio]
                       ===================================================
                                 RF links (C2 / video / telemetry)
                       ===================== GROUND =======================
 [Handset/Headset MCU+FPGA (STM32 + video FPGA)] <--> [Ground Control Station (Linux)]
```

| 계층 | 자원 | 설계 질문에서 쓰는 성질 |
|---|---|---|
| FC (STM32) | 수백 KB~수 MB flash, 수백 KB RAM, 수백 MHz Cortex-M [추정: F4/F7/H7급] | 결정적 타이밍, ISR, DMA, 동적 할당 회피 |
| Companion (Linux) | GB급 RAM, 멀티코어, 가속기 | 유연하지만 비결정적 → 안전 기능은 FC에 둔다 |
| 링크 | 저대역, 손실, 재밍 | 우선순위, degrade, heartbeat |
| 지상 | MCU + FPGA (영상), Linux GCS | 영상 지연, 사용자 입력 |

**설계 원칙 (영어로 말할 것)**
- "Safety-critical behavior — arming, failsafe, motor output — stays on the MCU. The Linux side can crash and restart without the aircraft falling."
- "The boundary between MCU and Linux is a versioned protocol with heartbeats, so either side can be updated independently."
- "One firmware platform, many variants: board differences live in configuration, not forks."
- "Every boundary gets counters and timestamps, because the bugs live at the boundaries."

**자체 실리콘이 없다는 것의 의미** (Michael: STM 사용): Neros의 "하드웨어 bring-up"은 칩 bring-up이 아니라 **보드 bring-up**(새 보드 revision, second-source 부품, 새 센서)이다. Don의 실리콘 bring-up 경험은 "새 보드가 들어왔을 때 버스별로 검증하는 순서"로 번역해서 말한다. 자세한 내용은 [N02 STM32 노트](2026-10-01_N02_stm32_what_it_means.md)

## 11. 문제가 다르게 나와도 쓰는 "만능 부품" 7개

| 부품 | 언제 꺼내나 | 한 줄 |
|---|---|---|
| SPSC ring buffer | 속도가 다른 생산자·소비자 | "ISR produces, task consumes, no locks." |
| Double buffer / seqlock | 최신값만 필요한 소비자 | "Readers always get a consistent latest sample." |
| A/B 슬롯 + CRC + seq | 전원 차단에도 무결성 | "Old or new, never corrupt." |
| 길이 + CRC framing | 모든 직렬 링크 | "Resync from any byte." |
| Watchdog + reset cause 기록 | 모든 실패 복구 | "Every reboot tells us why." |
| 카운터 + 텔레메트리 | 관측성 | "Every drop is counted." |
| 버전 필드 | 모든 인터페이스 | "Append-only fields, explicit versions." |

## 12. 시작과 마무리 대사

- 시작: "Let me start by asking a few questions so I design for the right constraints, and I'll write my assumptions in the corner."
- 막혔을 때: "Let me step back to the requirements — the real constraint here is X."
- trade-off: "There are two reasonable options. A gives us X at the cost of Y. Given what you said about Z, I'd pick A."
- 마무리: "To summarize: the key decisions were A, B, C; the main risks are D and E; and here's how I'd test it. If I had more time, I'd look at F."

## 13. Don 경험 → 설계 근거 문장

| 경험 (레쥬메) | 설계 문제 | 영어 브리지 |
|---|---|---|
| NVMe telemetry 디버그 기능 (SK hynix) | 1 로깅, 7 HIL 아티팩트 | "I designed telemetry capture for data-center drives — capturing state at the moment of failure without hurting performance." |
| error reporting/handling scheme (Solidigm) | 5 config, 3 프로토콜 | "I built an error scheme that many firmware modules depended on, so I think about interfaces that must stay stable." |
| 멀티코어 R8/R82/M0+ FW, DMA, SoC verification | 3, 4 | "I've debugged data paths across cores, DMA, and interrupts before RTL freeze." |
| 챔버 테스트 SDK (SK hynix) | 7 HIL 팜 | "My first job was essentially a test farm: thermal chambers, many devices, scheduling, and an SDK for other engineers." |
| factory test-node (Apple) | 6 factory test | "I designed factory test-node architecture from bring-up through mass production." |
| HW safety margin sign-off (Apple) | 4, 6 limits | "I set pass/fail margins with data — reliability versus performance and power." |

- (Don: 각 브리지에 붙일 실제 숫자나 사례 하나씩)

## 14. 연습 방법

1. 문제 하나를 고른다. 타이머 35분
2. 빈 종이에 1절의 틀 순서대로: 질문 5개 → 가정 숫자 → 블록도 → trade-off 2개 → 실패 모드 3개 → 테스트
3. 영어로 소리 내며. 녹음해서 다시 듣는다
4. 노트의 풀이와 비교: 빠진 실패 모드, 빠진 숫자를 체크
5. 영어 2분 요약을 외우지 말고 **구조만** 기억해서 내 말로

## 체크

- [ ] 1절 45분 틀 9단계를 보지 않고 순서대로 말할 수 있다
- [ ] 요구사항 질문 7개를 영어로 바로 꺼낼 수 있다
- [ ] 문제 1(로깅)과 문제 2(OTA)를 35분 안에 화이트보드로 끝까지 풀어 봤다
- [ ] 문제 3(프로토콜) 프레임 형식을 외워서 그릴 수 있다
- [ ] 문제 6(factory test)의 takt 계산(240대/일 → 약 90초)을 즉석에서 할 수 있다
- [ ] 10절 "STM32 + Linux companion" 그림과 설계 원칙 4문장을 말할 수 있다
- [ ] 11절 만능 부품 7개의 한 줄 설명을 영어로 말할 수 있다
- [ ] 13절 브리지에 (Don: …) 실제 사례를 채웠다
