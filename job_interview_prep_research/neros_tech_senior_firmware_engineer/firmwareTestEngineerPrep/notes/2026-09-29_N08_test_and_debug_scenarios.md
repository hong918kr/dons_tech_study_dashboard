# 🧯 N08 · 테스트 시나리오 · 디버깅 시나리오 모음 (스토리형)

> "UART 통신을 어떻게 테스트하겠나?", "I2C가 가끔 NACK이 난다, 어떻게 디버그하나?" 같은 질문에 이야기로 답하기 위한 노트다. 앞부분은 **테스트 시나리오 10개**(무엇을 어떻게 검증할지), 뒷부분은 **디버깅 시나리오 12개**(증상 → 가설 → 도구 → 원인 → 재발 방지 테스트)다. 시나리오마다 끝에 "내 경험으로 바꾸기" 칸이 있다. Don이 실제로 겪은 비슷한 일을 떠올려 채워 두면 면접에서 자기 이야기로 말할 수 있다.

## 0. 모든 답의 뼈대 — 이 순서로 말한다

어떤 시나리오가 나와도 아래 두 틀 중 하나에 넣어서 말하면 구조가 선다.

**테스트 질문 ("How would you test X?")**

1. **요구사항과 실패 모드부터**: 이 기능이 무엇을 보장해야 하고, 어떻게 깨질 수 있나
2. **정상 경로 (happy path)**: 기본 동작 확인
3. **경계값**: 최소·최대, 속도·길이·타이밍의 한계
4. **부정 케이스와 고장 주입**: 끊기, 깨뜨리기, 늦추기, 전원 빼기
5. **스트레스와 soak**: 반복, 장시간, 온도, 부하 동시 인가
6. **자동화와 CI 배치**: 어떤 건 MR마다 smoke, 어떤 건 야간 회귀
7. **합격 기준**: 숫자로 (에러율, 지연, 복구 시간)

**디버깅 질문 ("X fails, how do you debug?")**

1. **재현**: 얼마나 자주? 같은 커밋에서 N번 돌려 실패율을 잰다
2. **범위 좁히기**: FW 문제인가, 테스트 코드 문제인가, 리그·환경 문제인가
3. **관찰**: 로그, 리셋 원인, 버스 캡처(로직 애널라이저·프로토콜 애널라이저), 스코프로 신호 품질
4. **가설 → 검증**: 한 번에 변수 하나만 바꾼다
5. **수정**: 원인 쪽 팀과 함께
6. **재발 방지**: 그 버그를 잡는 테스트를 추가한다. **이 마지막 단계를 꼭 말한다.** 테스트 엔지니어 인터뷰에서 가장 점수가 되는 문장이다

> 영어 골격: "First I'd make it reproducible and measure the failure rate. Then I'd split the problem: is it the firmware, the test, or the rig? I'd capture the bus and the logs at the moment of failure, change one variable at a time, and once we find the root cause, I'd add a regression test so it can't come back silently."

## Part A · 테스트 시나리오

### T1. UART 통신 테스트 (예: FC ↔ 수신기 CRSF, FC ↔ 컴패니언 컴퓨터)

**📖 스토리**: 새 보드에서 FC와 ELRS 수신기 사이 CRSF 링크(420000 baud, 8N1)를 검증해야 한다. "잘 된다"를 넘어서 **어떤 조건에서 깨지는지**를 알아야 한다.

| 단계 | 무엇을 | 어떻게 (Python + 리그) |
|---|---|---|
| 정상 | 프레임 송수신, 내용 일치 | USB-UART 어댑터로 알려진 CRSF 프레임을 주입하고, MSP나 CLI로 FC가 읽은 채널 값을 확인 |
| 경계 | baud 오차 허용 범위 | 주입하는 쪽 baud를 ±1%, ±2%, ±3%로 바꿔 에러율 곡선을 그림 (UART는 대략 양쪽 합산 수 % 이내여야 안정 [일반론]) |
| 경계 | 최대 처리량 | 연속 burst로 보내 RX overrun이 나는지. **카운터 패턴**(0,1,2,…)을 보내면 빠진 바이트를 바로 찾을 수 있다 |
| 부정 | 깨진 프레임 | CRC 틀린 프레임, 길이 틀린 프레임, 중간에 잘린 프레임 → 파서가 **버리고 재동기화하는지** |
| 부정 | 링크 끊김 | 선을 끊거나(릴레이) 송신을 멈춤 → 설정된 시간 안에 failsafe로 가는지, 다시 연결하면 복구되는지 |
| 부정 | 노이즈 | 쓰레기 바이트를 섞어 보냄 → 오동작 없이 무시하는지 |
| soak | 장시간 | 몇 시간 동안 돌리며 프레임 에러율과 지연 분포를 기록 |

- **합격 기준 예시**: 프레임 에러율 < 0.01%, 링크 끊김 후 failsafe 진입 < N ms, 재연결 후 복구 < N ms (숫자는 제품 요구사항에서 가져온다)
- **관련 코드**: [문제 02 CRSF 파서](../python/problems/02_crsf_frame_parser.md), [문제 04 시리얼 드라이버](../python/problems/04_serial_dut_driver.md)
- **내 경험으로 바꾸기**: SK hynix 챔버 플랫폼에서 **UART 기반 테스트 시퀀스를 자동으로** 돌렸다. (Don: 그때 UART에서 겪은 문제 — 응답 없음, 깨진 문자, 포트가 바뀜 등 — 하나를 떠올려 적기)

> "For a UART link I'd test four layers: correct frames, limits like baud tolerance and burst throughput, fault handling like bad CRC and truncated frames, and link loss — making sure the flight controller goes to failsafe in time and recovers cleanly. I like sending a counter pattern for throughput tests because a missing byte shows up immediately."

### T2. I2C 센서 테스트 (예: 기압계, 자력계)

**📖 스토리**: 새 기압계가 I2C로 붙었다. 드라이버가 "읽힌다"는 건 확인됐지만, 드론은 진동·모터 노이즈·전원 변동 속에서 날아간다.

| 단계 | 무엇을 | 어떻게 |
|---|---|---|
| 정상 | 존재 확인 | WHO_AM_I 레지스터가 기대값인지 |
| 정상 | 설정 | 설정 레지스터 read-modify-write 후 다른 비트가 안 바뀌었는지 |
| 정상 | 데이터 | 값이 물리적으로 말이 되는 범위인지 (기압 → 고도가 현실적인지) |
| 부정 | 장치 없음 | 센서를 빼거나 주소를 틀리게 → NACK 처리, 에러 보고, **FC가 멈추지 않는지** |
| 부정 | 버스 hang | 트랜잭션 중간에 리셋 → SDA가 low로 잡힌 채 멈추는 상황에서 복구하는지 (D1 참고) |
| 경계 | 속도 | 100 kHz / 400 kHz. 풀업 저항과 버스 용량 때문에 rise time이 느려지면 고속에서 깨진다 |
| 스트레스 | 모터 구동 중 | 모터를 돌리면서 읽기 에러율을 잰다 (EMI, 전원 노이즈) |
| soak | 장시간·온도 | 에러 카운터를 텔레메트리로 수집 |

- **관련 코드**: [문제 05 레지스터 비트필드](../python/problems/05_register_bitfields.md)
- **내 경험으로 바꾸기**: Apple에서 **I2C 인터페이스 레벨 실패**를 루트코즈했고, SK hynix에서 FPGA 단계 SoC verification으로 I2C를 bring-up했다. (Don: 가장 기억나는 I2C 문제 하나)

> "Beyond reading the right values, I'd test the failure paths: a missing device, a NACK mid-transaction, and a stuck bus after reset — the firmware has to recover without hanging the flight loop. Then I'd repeat the read test with motors spinning, because that's where noise and power droop show up."

### T3. SPI IMU 테스트 (gyro/accel — FC의 심장)

**📖 스토리**: FC의 IMU는 SPI로 kHz 단위로 읽힌다. 여기가 흔들리면 비행 자체가 흔들린다.

| 단계 | 무엇을 | 어떻게 |
|---|---|---|
| 정상 | ID, 설정, 데이터 | WHO_AM_I, 정지 상태에서 accel ≈ 1 g, gyro ≈ 0 |
| 타이밍 | 샘플 주기 | data-ready 인터럽트 간격을 로직 애널라이저로 측정, 지터 분포 |
| 경계 | 최대 SPI 클럭 | 클럭을 올려 가며 읽기 오류가 시작되는 지점 → 마진 확보 |
| 부정 | 이상 값 | 0x00 또는 0xFF만 읽히면 (D10 참고) FC가 감지하고 경고하는지 |
| 스트레스 | 진동 | 진동대나 모터 구동 중 데이터가 포화(clipping)되지 않는지 |

> "For the IMU I care about timing as much as values — I'd capture the data-ready interrupt and the SPI transactions on a logic analyzer to measure jitter, and I'd find the max SPI clock where reads start failing so we know our margin."

### T4. Arm / Failsafe 상태 머신 테스트

**📖 스토리**: 안전에 직결되는 기능이다. 조건 하나라도 빠지면 땅에서 모터가 돌거나, 공중에서 멈추지 않는다.

- **상태 전이 표를 먼저 그린다**: DISARMED → ARMED → FAILSAFE → DISARMED
- **arm 거부 조건을 하나씩**: 스로틀이 높음, RX 링크 없음, failsafe 중, (실제 Betaflight에는 arming disable flag가 훨씬 많다)
- **failsafe 타이밍 경계**: 링크 끊김이 임계값 직전(복구돼야 함)과 직후(failsafe여야 함)
- **복구**: 링크가 돌아와도 **자동으로 다시 arm되면 안 된다** (의도적인 재-arm 필요)
- **관련 코드**: [문제 11 HIL 테스트 설계](../python/problems/11_hil_test_design.md)

> "Safety state machines get a transition table first. Then I test every guard condition in isolation, the timing boundary on either side of the failsafe threshold, and the recovery rule — the link coming back must not re-arm the motors on its own."

### T5. 펌웨어 업데이트 테스트

**📖 스토리**: 수천 대가 현장에 있다. 업데이트 한 번 잘못되면 기체가 벽돌이 된다.

| 시나리오 | 확인할 것 |
|---|---|
| 정상 업데이트 | 버전이 바뀌고, 설정이 보존되는지 |
| **업데이트 중 전원 차단** | 릴레이로 쓰기 도중 여러 시점에 전원을 끊음 → 부팅되는지 (A/B 슬롯이나 bootloader 복구) |
| 잘못된 이미지 | 서명·CRC가 틀린 이미지를 거부하는지 |
| 다운그레이드 | 허용 정책대로 동작하는지 |
| 반복 | 수백 번 연속 업데이트 soak |

- **내 경험으로 바꾸기**: SSD 펌웨어도 필드 업데이트와 전원 차단 복구가 핵심 요구사항이다. (Don: SSD FW에서 power-loss 관련 테스트나 이슈 경험이 있으면 연결)

> "The update test I'd automate first is power loss during the write — a relay cutting power at randomized points across the update, hundreds of times. A drone in the field has to survive a bad update, and that bug only shows up statistically."

### T6. 설정(파라미터) 저장 테스트

- 저장 → 전원 재인가 → 값 유지
- 저장 도중 전원 차단 → 이전 값 또는 새 값 둘 중 하나, **깨진 값은 절대 안 됨** (CRC, 이중 저장)
- 저장 영역이 깨졌을 때 기본값으로 안전하게 부팅하는지
- FW 버전이 올라가서 파라미터가 추가·삭제됐을 때 마이그레이션
- 플래시 쓰기 횟수 (자주 저장하는 기능이 있다면 wear)

> "For persistent config, the invariant is: after any power loss you get either the old value or the new value, never a corrupt one. I'd test that with power cuts during save, and a corrupted-storage case that must boot to safe defaults."

### T7. 텔레메트리·무선 링크 테스트 (RF 우대 사항과 연결)

| 시나리오 | 방법 |
|---|---|
| rate와 지연 | 메시지 종류별 수신 rate, 지터, 끊김 구간 측정 ([문제 06](../python/problems/06_telemetry_rate_check.md)) |
| 거리 대신 감쇠 | 차폐 박스 + **programmable attenuator**로 감쇠를 올려 가며 LQ/RSSI와 패킷 손실 곡선 |
| 간섭 | 같은 대역 간섭원을 넣었을 때 동작 |
| 링크 품질 저하 시 동작 | 대역폭이 줄 때 중요한 텔레메트리가 먼저 살아남는지 |

- **내 경험으로 바꾸기**: Apple **RF-Hardware Chipset Integration**, RFFE 버스, factory test-node. (Don: RF 파트 테스트에서 자동화한 항목 중 공개 가능한 수준으로 하나)

> "Range testing outdoors is slow and not repeatable, so in the lab I'd use a shielded box with a programmable attenuator and sweep the path loss, logging link quality and packet loss at each step. That gives a repeatable curve we can compare across firmware versions."

### T8. 전원·브라운아웃 테스트

- 배터리 전압 범위 전체를 프로그래머블 전원으로 스윕 (저전압 경고, 컷오프 동작)
- **모터 급가속 순간 전압 강하**: 스코프로 MCU 전원 레일을 보면서 풀 스로틀 스텝
- 전원 on/off 반복 수천 번: 매번 정상 부팅하는지, 리셋 원인 레지스터 기록
- 역전압·과전압은 HW팀과 합의한 범위에서만

> "I'd sweep supply voltage across the battery range, then add fast throttle steps while scoping the MCU rail — a brownout during a punch-out is exactly the kind of failure you never see on a bench supply."

### T9. Soak·스트레스 테스트

- 몇 시간~며칠 연속 운전: 메모리 누수, 카운터 overflow(예: 32비트 ms 카운터는 약 49.7일), 로그 버퍼 가득 참
- 온도 챔버: 저온·고온에서 센서, 클럭, 통신 에러율
- 모든 부하 동시: 최대 텔레메트리 + 로깅 + 제어 루프 + 무선
- **내 경험으로 바꾸기**: 이 부분이 Don의 본진이다. SK hynix **챔버 신뢰성 테스트**, Apple **stress 기반 latent defect 발굴**

> "Soak testing is where I've spent a lot of my career — thermal chambers at SK hynix and stress-based test cases at Apple. The bugs it finds are the ones that never appear in a ten-minute test: leaks, counter wraps, buffers filling up, and timing drift at temperature."

### T10. Ethernet/UDP GCS 링크 테스트

- 정상: 명령 송신 → 응답, 텔레메트리 수신
- 네트워크 열화 주입 (Linux): `tc qdisc add dev eth0 root netem loss 5% delay 50ms 10ms reorder 5%` → 패킷 손실·지연·순서 뒤바뀜에서 GCS와 기체가 어떻게 동작하나
- 연결 끊김과 재연결, 여러 GCS 동시 접속
- Python `socket`으로 가짜 GCS나 가짜 기체를 만들어 대량 메시지를 주입

> "For the ground link I'd use Linux netem to inject loss, delay, and reordering in a controlled way, and a small Python UDP client to act as a fake GCS — so we can test degraded-network behavior on every build instead of waiting for a field test."

## Part B · 디버깅 시나리오 (스토리)

각 시나리오는 **증상 → 가설 → 확인 방법 → 원인 → 수정과 재발 방지 테스트** 순서다.

### D1. I2C 버스가 리셋 후에 영원히 멈춘다 (SDA stuck low)

**📖 스토리**: HIL에서 FC를 전원 재인가 없이 소프트 리셋하는 테스트를 돌렸더니, 수십 번에 한 번 기압계가 안 잡히고 이후 모든 I2C 통신이 타임아웃난다. 전원을 완전히 껐다 켜면 멀쩡해진다.

| 단계 | 내용 |
|---|---|
| 가설 | MCU는 리셋됐지만 **슬레이브는 리셋되지 않았다**. 슬레이브가 바이트를 보내던 도중이라 SDA를 low로 잡고 다음 클럭을 기다리고 있다 |
| 확인 | 로직 애널라이저로 리셋 직후 SDA가 low에 붙어 있는지 확인. 전원 사이클로는 해결되고 소프트 리셋으로는 안 된다는 것 자체가 강한 증거 |
| 원인 | 트랜잭션 중간 리셋 → 슬레이브 상태 머신이 중간에 멈춤 |
| 수정 | I2C 초기화 때 **bus recovery**: SDA가 low면 SCL을 GPIO로 최대 9번 토글해 슬레이브가 바이트를 끝내게 하고, 그다음 STOP 조건을 만든다 |
| 재발 방지 | "읽기 도중 랜덤 시점 리셋" 테스트를 HIL 회귀에 추가 (수백 회) |

- **내 경험으로 바꾸기**: (Don: Apple이나 SK hynix에서 리셋·전원 시퀀스 때문에 버스가 이상해진 경험이 있다면 여기에)

> "The clue was that a power cycle fixed it but a soft reset didn't — that points to a slave that wasn't reset, still holding SDA low mid-byte. The fix is the standard bus recovery: clock SCL up to nine times until SDA releases, then issue a STOP. And the regression test is resets injected at random points during reads."

### D2. I2C NACK이 가끔 난다 — 모터를 돌릴 때만

**📖 스토리**: 벤치에서는 0건인데 모터를 돌리는 HIL 테스트에서 기압계 읽기가 가끔 NACK이 난다.

| 단계 | 내용 |
|---|---|
| 가설 | ① EMI나 그라운드 노이즈 ② 전원 강하로 센서가 순간 리셋 ③ 풀업이 약해 rise time이 느려 노이즈에 약함 ④ FW 타이밍 (인터럽트가 I2C 처리를 늦춤) |
| 확인 | 스코프로 SCL/SDA 파형: 모터 PWM 주기와 맞물린 스파이크가 있나, rise time은 스펙 안인가. 센서 전원 레일도 같이 본다. 모터를 켜고 I2C 속도를 400 → 100 kHz로 낮추면 사라지는가 |
| 원인 예시 | 긴 배선 + 약한 풀업 + 모터 노이즈 → 400 kHz에서 마진 부족 |
| 수정 | HW: 풀업 강화, 배선·그라운드 개선. FW: 재시도 정책과 에러 카운터, 속도 조정 |
| 재발 방지 | "모터 구동 중 센서 에러율" 테스트를 HIL에 추가, 에러율 임계값을 합격 기준으로 |

- **내 경험으로 바꾸기**: Apple에서 **DSO와 protocol analyzer로 신호·버스 레벨 디버그**를 했고, 하드웨어 **safety margin**을 sign-off했다. 이 시나리오와 가장 가깝다. (Don: 실제 신호 품질 문제를 잡은 사례 하나)

> "When a failure only happens with motors running, I stop trusting the firmware logs alone and put a scope on the bus. I look at rise times and whether glitches line up with the motor PWM. If slowing the bus makes it disappear, it's a margin problem — and margins are something I've signed off on at Apple."

### D3. UART에서 깨진 문자가 나온다

**📖 스토리**: 새로 받은 수신기를 붙였더니 FC가 RC 신호를 전혀 인식하지 못하고, 시리얼 캡처에는 쓰레기 바이트만 보인다.

| 가설 | 확인 방법 |
|---|---|
| baud rate 불일치 | 로직 애널라이저로 비트 폭을 측정해 실제 baud 계산 (420000 baud면 비트 폭 약 2.38 µs) |
| **신호 반전** | 프로토콜 확인: SBUS는 반전된 UART(100000 baud, 8E2)이고 CRSF는 반전되지 않는다. 애널라이저에서 idle 레벨이 low면 반전 신호 |
| 프레임 형식 | 8N1 vs 8E2, stop bit 수 |
| 전압 레벨 | 3.3 V vs 5 V, 레벨 시프터 |
| TX/RX 교차 | 배선. 의외로 흔하다 |
| 설정 | FC 포트 설정에서 프로토콜이 맞게 지정됐는지 |

- **재발 방지**: 보드 bring-up 체크리스트에 "UART 포트별 루프백과 프로토콜 확인" 추가, HIL에서 수신기 종류별 smoke 테스트

> "Garbage on a UART is almost always configuration before it's a bug: baud rate, frame format, inversion, or voltage level. I'd measure the bit width on a logic analyzer to get the real baud rate and check the idle level — SBUS, for example, is inverted while CRSF isn't."

### D4. UART가 부하가 걸리면 바이트를 잃는다

**📖 스토리**: 텔레메트리를 높은 rate로 켜면 컴패니언 컴퓨터 쪽 파서가 CRC 에러를 가끔 보고한다. 낮은 rate에서는 0건이다.

| 단계 | 내용 |
|---|---|
| 가설 | ① 수신 측 RX FIFO overrun (인터럽트 지연) ② 소프트웨어 링 버퍼가 가득 참 ③ 송신 측이 버퍼를 덮어씀 ④ 리눅스 쪽 읽기 루프가 느림 |
| 확인 | **카운터 패턴**을 보내 어디서 몇 바이트가 빠졌는지 확인. MCU의 overrun 에러 플래그 카운터 확인. 빠지는 시점이 다른 인터럽트(로깅, 플래시 쓰기)와 겹치는지 타임스탬프 비교 |
| 원인 예시 | 플래시 쓰기 중 인터럽트가 막혀 RX FIFO가 넘침 |
| 수정 | UART RX를 DMA로, 링 버퍼 크기를 최악 지연 기준으로 산정, 드롭 카운터를 텔레메트리로 노출 |
| 재발 방지 | 최대 rate + 플래시 쓰기 동시 부하 테스트, 드롭 카운터가 0인지 확인 |

- **내 경험으로 바꾸기**: SSD FW에서 멀티코어, DMA, 인터럽트 동시성을 다뤘다. (Don: 버퍼 overrun이나 인터럽트 지연으로 데이터가 빠졌던 경험)

> "I'd send a counter pattern so every missing byte is visible, then check the UART overrun flags. If drops line up with something like flash writes, it's interrupt latency — the fix is DMA on RX and a buffer sized for the worst-case latency, plus a drop counter exported in telemetry so we'd see it in the field."

### D5. 내 자리에서는 통과, CI에서는 실패

**📖 스토리**: 새로 짠 HIL 테스트가 로컬에서는 계속 통과하는데, CI의 HIL runner에서는 가끔 "포트를 열 수 없음"이나 타임아웃으로 실패한다.

| 가설 | 확인 |
|---|---|
| USB 장치 이름이 바뀜 (`/dev/ttyACM0` ↔ `ttyACM1`) | FC 리셋 후 재열거 순서 확인 → **udev 규칙으로 시리얼 번호 기반 고정 이름** 사용 |
| 포트 경쟁 | 다른 job이 같은 리그를 쓰는 중 → GitLab `resource_group`이나 lock으로 리그 독점 |
| 재부팅 후 대기 부족 | FC 리부트 후 USB 재열거까지 시간이 걸림 → 고정 sleep 대신 **포트가 나타날 때까지 wait_until** |
| 환경 차이 | Python·라이브러리 버전, 권한(dialout 그룹) |
| 이전 테스트의 잔여 상태 | fixture teardown이 리그를 초기 상태로 안 돌려놓음 |

- **재발 방지**: 테스트 시작 시 리그 헬스체크 fixture, 실패 시 "인프라 실패"와 "DUT 실패"를 분리해 리포트
- **관련 코드**: [문제 08 retry와 wait_until](../python/problems/08_retry_and_wait.md)

> "Works-locally-fails-in-CI on a HIL rig is usually the environment: USB devices re-enumerating under a different name, another job using the same rig, or a fixed sleep that's too short after a reboot. I'd pin device names with udev rules by serial number, lock the rig per job, and replace sleeps with wait-until-port-appears."

### D6. 20번에 1번 실패하는 테스트 (flaky)

**📖 스토리**: 야간 회귀에서 failsafe 테스트가 가끔 실패한다. 다시 돌리면 통과해서 다들 무시하고 있다.

| 단계 | 내용 |
|---|---|
| 측정 | 같은 커밋으로 100번 돌려 실패율 확인. 실패한 회차의 로그와 타이밍을 모음 |
| 분류 | ① 테스트 코드: 타이밍 가정(고정 sleep), 순서 의존 ② 리그: 전원, USB, RF 환경 ③ **FW: 진짜 race condition** |
| 도구 | 실패 시 자동으로 로그·버스 캡처·FW 버전을 아티팩트로 저장하도록 프레임워크를 바꾼다 |
| 정책 | quarantine해서 머지를 막지 않게 하되, **담당자와 기한을 붙여 추적**. 무작정 자동 재시도는 금지 |
| 핵심 메시지 | flaky 테스트는 종종 **진짜 간헐 버그**다. 드론에서 간헐 버그는 가장 위험한 버그다 |

- **관련 코드**: [문제 07 CI 결과 분석](../python/problems/07_ci_results_analyzer.md)

> "A flaky test is data, not noise. I'd rerun the same commit a hundred times to get a failure rate, make the framework save logs and captures automatically on failure, and classify it: test, rig, or firmware. I'd quarantine it with an owner, but never just auto-retry — in a drone, an intermittent bug is the most dangerous kind."

### D7. 모터를 돌리면 FC가 리부트된다

**📖 스토리**: HIL에서 스로틀 스텝 테스트 중 가끔 FC가 리부트되고 텔레메트리가 끊긴다.

| 단계 | 내용 |
|---|---|
| 첫 질문 | **왜 리셋됐나?** 부팅 직후 리셋 원인 레지스터를 읽어 로그로 남긴다 (STM32 계열이면 RCC_CSR의 brownout·watchdog·pin·software 리셋 플래그) |
| 갈래 | brownout → 전원 문제 / watchdog → FW가 멈췄거나 루프가 너무 오래 걸림 / HardFault → FW 크래시 |
| 확인 | 전원 레일에 스코프와 전류 프로브, 스로틀 스텝과 리셋 시점 비교. HardFault면 fault handler가 스택·PC를 noinit RAM에 남기게 해서 다음 부팅 때 읽음 |
| 재발 방지 | 모든 테스트 시작 시 리셋 원인과 부팅 횟수를 기록하는 fixture → **예상치 못한 리부트는 무조건 실패로 처리** |

- **내 경험으로 바꾸기**: SK hynix/Solidigm에서 **error reporting/handling scheme**을 production FW에 구현했고, health monitoring debug FW를 만들었다. 리셋 원인과 크래시 정보 수집은 그 연장선이다. (Don: 실제 사례)

> "The first question for any unexpected reboot is why — so I'd make every boot log the reset-cause register. Brownout, watchdog, and a hard fault lead to completely different investigations. And the test framework should treat any unexpected reboot as a failure, even if the test's own assertions passed."

### D8. 어제는 통과, 오늘은 실패 (회귀)

**📖 스토리**: 야간 회귀에서 지난 50개 커밋 사이 어딘가부터 텔레메트리 rate 테스트가 실패하기 시작했다.

- **`git bisect run`**: 좋은 커밋과 나쁜 커밋을 지정하고, "빌드 → 플래시 → 해당 테스트 실행 → 결과를 exit code로 반환"하는 스크립트를 넘기면 약 log2(50) ≈ 6번 만에 범인 커밋을 찾는다
- flaky한 테스트라면 스크립트 안에서 여러 번 돌려 다수결로 판단한다
- 빌드가 안 되는 커밋은 exit code 125로 건너뛴다
- **재발 방지**: 이 테스트를 MR마다 도는 smoke로 올릴지 검토한다 (빠르다면)

> "For a regression across fifty commits I'd use git bisect run with a script that builds, flashes the rig, and runs just that test — about six iterations. If the test is at all flaky, the script runs it several times and takes a majority, otherwise bisect goes the wrong way."

### D9. 텔레메트리 rate가 기대보다 낮고 들쭉날쭉하다

**📖 스토리**: 50 Hz로 나와야 하는 자세 텔레메트리가 평균 43 Hz이고, 가끔 200 ms 공백이 생긴다.

| 가설 | 확인 |
|---|---|
| FC 스케줄러 과부하 | FC의 태스크별 CPU 사용량(Betaflight라면 CLI `tasks` 명령 [추정]) |
| 링크 대역폭 부족 | 전체 텔레메트리 바이트/초 vs 링크 용량. 텔레메트리 비율 설정 |
| 수신 측 문제 | 리눅스 쪽 읽기 스레드가 막힘 (GIL, 느린 로깅, 동기 I/O) |
| 공백이 주기적 | 공백 간격이 일정하면 다른 주기 작업(플래시 쓰기, GPS)과 충돌 |

- **관련 코드**: [문제 06 텔레메트리 rate 체크](../python/problems/06_telemetry_rate_check.md)

> "I'd first characterize it: per-message rates, the gap distribution, and whether the gaps are periodic. Periodic gaps usually mean a collision with another scheduled task on the flight controller; random gaps point more toward the link or the receiver side."

### D10. SPI IMU가 전부 0xFF 또는 0x00만 읽는다

| 증상 | 가장 흔한 원인 |
|---|---|
| 전부 0xFF | MISO가 떠 있음(장치가 응답 안 함): CS가 안 내려감, 장치 전원 없음, 배선 |
| 전부 0x00 | MISO가 low에 붙음, 장치가 리셋 상태, 잘못된 SPI mode |
| 한 비트씩 밀린 값 | CPOL/CPHA mode 불일치 |
| 가끔 틀린 값 | 클럭이 너무 빠름, 신호 품질 |
| 읽기는 되는데 쓰기가 안 됨 | 읽기/쓰기 비트 규칙 (많은 IMU가 주소 MSB=1이면 읽기 [추정: 데이터시트 확인]) |

- **확인**: 로직 애널라이저로 CS, SCLK, MOSI, MISO 네 선을 동시에 보고 WHO_AM_I 트랜잭션 한 번을 해독한다

> "All 0xFF on SPI usually means nobody is driving MISO — the chip select isn't asserting, or the device isn't powered. Values shifted by a bit point to a wrong SPI mode. I'd capture one WHO_AM_I transaction with all four lines on a logic analyzer; that usually answers it in minutes."

### D11. 로그를 켜면 버그가 사라진다 (heisenbug)

- **해석**: 로깅이 타이밍을 바꿨다 → **race condition이나 타이밍 의존 버그**라는 강한 신호
- **접근**: 로깅 비용을 줄인다 (포맷팅 없이 이진 로그를 RAM 링 버퍼에, 나중에 덤프). GPIO 토글과 로직 애널라이저로 타이밍을 **개입 없이** 관찰. 공유 변수와 인터럽트 경계를 검토
- **재발 방지**: 실제 타이밍에 가까운 조건(최대 부하)에서 도는 스트레스 테스트

> "If logging makes a bug disappear, the logging changed the timing — so it's almost certainly a race. I'd switch to low-overhead observation: binary logs into a RAM buffer, or toggling a GPIO and watching it on a logic analyzer, which barely disturbs timing."

### D12. 업데이트 50번에 1번 기체가 부팅을 못 한다

- **재현**: 업데이트 테스트를 자동으로 수백 번 반복 → 실패율과 실패 시점 수집
- **가설**: 쓰기 도중 전원 불안정, 플래시 erase/write 타이밍, 검증 없는 부트 전환, bootloader가 불완전한 이미지로 점프
- **확인**: 실패한 기체의 플래시를 덤프해서 어디까지 써졌는지 확인. 업데이트 단계마다 로그
- **수정 방향**: 이미지 검증(CRC/서명) 후에만 부트 전환, A/B 슬롯, 롤백
- **내 경험으로 바꾸기**: SSD FW는 전원 차단 복구가 기본 요구사항이다. (Don: 관련 경험)

> "One-in-fifty means I need automation to see it at all — a loop that updates hundreds of times and captures where each failure happened. Then dump the flash on a failed unit to see how far the write got. The design fix is usually to only switch the boot target after the new image is fully written and verified."

## Part C · 내 경험 → 시나리오 연결표

> 반대 방향(레쥬메 bullet → 스토리 전체)은 [N09 스토리라인 2탄](2026-09-29_N09_resume_storylines.md)에 있다.

면접에서 시나리오 질문을 받으면 "제가 비슷한 걸 했을 때…"로 시작할 수 있게 연결해 둔다.

| Don의 경험 (레쥬메) | 바로 연결되는 시나리오 | 한 줄 브리지 |
|---|---|---|
| 챔버 테스트 플랫폼 SDK, UART 테스트 시퀀스 자동화 | T1, T9, D5, D6 | "I automated UART test sequences across many devices in thermal chambers." |
| chip reliability system (NAND, PCIe 3/4/5 커버리지) | T9, D6 | "I designed interface coverage for a reliability system." |
| shmoo testing, health monitoring debug FW | T8, D2, D7 | "Shmoo is margin testing — the same idea as sweeping voltage or clock until it breaks." |
| Apple I2C/PCIe/SPMI/RFFE 루트코즈, DSO·protocol analyzer | T2, D1, D2, D10 | "I've root-caused interface failures at the signal level with scopes and protocol analyzers." |
| Apple factory test-node, stress 기반 latent defect | T7, T9, D6 | "Latent defects show up under stacked stress, not in functional tests." |
| Apple safety margin sign-off | D2, T3 | "I signed off hardware margins — reliability versus performance and power." |
| error reporting/handling scheme (production FW) | D7, D12 | "I built error reporting into production firmware, so I think about what the device should record when it fails." |
| NVMe telemetry 기반 디버그 기능 | T7, D9 | "I designed telemetry for debugging data-center drives — what to capture and how often." |
| 멀티코어 Cortex-R/M, Xtensa, DMA, 인터럽트 | D4, D11 | "I've debugged concurrency across cores, DMA, and interrupts in production firmware." |

- **shmoo 연결은 특히 좋다**: shmoo는 전압·클럭·타이밍을 스윕해 통과/실패 경계를 그리는 것이다. baud 허용 범위(T1), SPI 최대 클럭(T3), 전압 스윕(T8), 감쇠 스윕(T7)이 전부 같은 발상이다. "I'd shmoo it"이라고 말하면 경험이 바로 전달된다

## Part D · 연습 방법

1. 시나리오 하나를 고른다
2. 표를 보지 않고 **0절의 틀**로 2분 동안 영어로 말한다
3. "내 경험으로 바꾸기" 칸의 실제 사례를 한 문장 끼워 넣는다
4. 마지막에 "and I'd add a regression test for…"로 끝낸다
5. 녹음해서 들어 본다. 2분이 넘으면 가설 목록을 줄인다

## 체크

- [ ] 0절 두 틀(테스트 질문, 디버깅 질문)을 보지 않고 말할 수 있다
- [ ] T1(UART), T2(I2C), T4(failsafe), T5(업데이트 전원 차단)를 각 2분에 말할 수 있다
- [ ] D1(I2C stuck), D2(모터 구동 중 NACK), D3(UART 쓰레기), D6(flaky), D7(리부트)을 각 2분에 말할 수 있다
- [ ] "내 경험으로 바꾸기" 칸을 최소 5개 실제 사례로 채웠다
- [ ] shmoo 브리지 문장을 자연스럽게 말할 수 있다
- [ ] 모든 디버깅 답을 "regression test 추가"로 끝내는 습관이 들었다
