# 🏭 S07 · Manufacturing · Factory test — 무엇을 만들고, 어떻게 테스트하나

> 공장 테스트는 "펌웨어가 맞게 동작하나"가 아니라 **"이 한 대가 설계대로 조립됐나"**를 묻는다. Neros는 주당 약 1,200대를 만들고 [확인됨 컨텍스트 [4][5]], Michael은 **factory test firmware가 아직 없고 필요하다**고 했다 [확인됨 10-01 Michael]. factory test FW 자체(별도 이미지 vs test mode, DUT 프로토콜, self-test, 캘리브레이션, takt, 수율)는 [onsitePrep N08](../../onsitePrep/site/notes/2026-10-01_N08_factory_test_firmware.html)에 이미 정리돼 있다. 이 노트는 **스테이션과 라인을 하나의 테스트 시스템으로 설계하는 관점**(스테이션 구조, 대수 산정, 리밋 관리, golden unit, 스테이션 자체의 신뢰성, 데이터 흐름)을 다룬다.

## 0. 한 장 요약

| 항목 | 내용 |
|---|---|
| 이 직군이 만드는 것 | 보드·조립품·완제품을 판정하는 **테스트 스테이션**(픽스처 + 계측기 + 스테이션 SW) + DUT 안의 **factory test FW** + 결과를 MES로 보내는 데이터 경로 |
| 주로 고장 나는 곳 | 조립 결함(냉납, 브리지, 커넥터 미삽입, 뒤집힌 부품), 부품 편차(IMU offset, 크리스털, RF PA), 캘리브레이션 실패, **스테이션 자체**(포고핀 마모, 케이블, 계측기 drift) |
| 대표 테스트베드 | ICT/FCT 포고핀 픽스처 · 최종 조립 FCT 지그 · RF conducted 스테이션(shield box) · EOL 시스템 테스트 스테이션 · golden unit 랙 |
| 합격 기준 예 | FCT 사이클 ≤ 60초 [추정], 스테이션 GR&R ≤ 10% of tolerance(10~30%는 조건부) [추정·업계 관행], first-pass yield ≥ 95% [추정], 재테스트 일치율 ≥ 99% [추정] |
| Don 연결 | Apple factory test-node 아키텍처, stress 기반 latent defect 발굴, bring-up→NPI→MP, HW safety margin sign-off / SK hynix 챔버 테스트 플랫폼 + SDK, chip reliability system |

## 1. 이 직군 이해하기 — Neros 공고 근거

- **규모**: Archer 주당 ~1,200대 생산 [확인됨 컨텍스트 [4][5]]. 연 100만 대를 향한다는 표현은 컨텍스트 파일의 "Why Neros" 문맥 [추정 수준으로만 사용]
- **factory test FW 부재**: "factory test firmware가 아직 없고, 나중에 필요하다" · "테스트 펌웨어가 가장 급하다 → 올해 안에 최대 5명" [확인됨 10-01 Michael]
- **RF 생산 테스트**: Lead RF Test & Integration Engineer — "NPI test readiness … **EVT/DVT/PVT** planning, **fixture readiness, station bring-up, GR&R, and factory handoff**", "manufacturing acceptance testing … **conducted RF tests, radiated tests, range/performance checks, and automated production test limits**", "**Python automation, and production test software**", root cause of "yield loss, **calibration drift, fixture issues**, and field-return failures" [확인됨 공고 5226790007]
- **MES·추적성**: Principal Software Engineer, Manufacturing Systems — "work orders, routing, material issue/consume, completion, **serialization, genealogy, rework**, and exception handling", "permissions, **auditability, traceability**" [확인됨 공고 5156414007]
- **Firmware Test Engineer JD**: "Test Framework Development", "Experience testing RF products"(nice) [확인됨 공고 4941340007] → 공장 스테이션 SW도 같은 프레임워크 위에 올라갈 수 있다 [추정]
- 해석 [추정]: RF 팀은 계측기와 RF 리밋을, Manufacturing Systems는 MES를, **FW Test 팀은 그 사이 — DUT 안의 test FW, 스테이션 SW 프레임워크, 결과 포맷**을 맡을 가능성이 크다

## 2. 이 펌웨어가 하는 일 — 데이터 흐름

```text
 MES (work order, serial 발급, routing)
   │  ① 스테이션이 바코드 스캔 → "이 serial 이 이 스테이션에 와도 되나?" (routing check)
   ▼
 Station PC (Python station SW) ──── limits.yaml (버전 관리, 제품·rev 별)
   │  ② DUT 에 test FW 플래시 (SWD) 또는 production FW 의 test mode 진입
   │  ③ 명령/응답: UART·USB CDC 텍스트 프로토콜  (onsitePrep N08 4절)
   ▼
 DUT: factory test FW ── self-test: IMU WHO_AM_I·노이즈, baro, flash, ADC 레일, 모터 출력, 라디오 PHY
   │  ④ 측정값 반환 (raw 값, 판정은 스테이션이 한다)
   ▼
 Station PC ── 계측기: PSU(전류), DMM, power meter / spectrum analyzer, 턴테이블(IMU cal)
   │  ⑤ limits 와 비교 → PASS/FAIL + 측정값 전부
   │  ⑥ provisioning: serial·config·키 기록 → 다시 읽어 검증
   ▼
 Result record (serial, station_id, fixture_id, sw_version, limits_version, fw_hash, measurements[])
   │  ⑦ 로컬 큐(오프라인 버퍼) → MES / results DB  (genealogy: 보드 serial ↔ 기체 serial)
   ▼
 Yield dashboard · Pareto · GR&R · SPC drift 알림 → 공정·설계 피드백
```

- 핵심 원칙: **DUT는 측정, 스테이션은 판정.** 리밋을 FW에 박으면 리밋 하나 바꾸는 데 FW 재배포가 필요하다 → [onsitePrep N08 3·4절](../../onsitePrep/site/notes/2026-10-01_N08_factory_test_firmware.html)

## 3. 무엇이 고장 나나

| failure mode | 증상 | 어느 테스트 레벨에서 잡나 |
|---|---|---|
| 냉납·브리지 (BGA, QFN IMU) | 간헐적 I2C/SPI NACK, 센서 미검출 | 보드 ICT/FCT (WHO_AM_I + 반복 read 100회), 진동 후 재테스트 |
| 커넥터 미삽입 (모터, 안테나 u.FL) | 모터 하나 안 돎, RF 출력 -20 dB | 최종 조립 FCT (모터 전류 측정), RF conducted/radiated |
| IMU 장착 각도·offset | 비행 drift, 수평 안 맞음 | EOL 캘리브레이션(6-face 또는 턴테이블) + 잔차 검사 |
| 크리스털 편차 | UART baud 오차, 라디오 주파수 오프셋 | FCT: 클럭 출력 측정 / RF: frequency error |
| RF PA 편차·안테나 손상 | 사거리 감소, desense | RF conducted power·sensitivity, radiated spot check [확인됨 공고 5226790007 문구] |
| 잘못된 FW·config·키 | 다른 제품 이미지, 바인딩 실패 | provisioning 후 **read-back 검증**, fw_hash 기록 |
| 배터리 커넥터·전원 레일 | 브라운아웃 리셋 | FCT: 레일 전압 + 부하 시 sag |
| latent defect (초기 고장) | 출하 후 수 시간 내 고장 | ESS·burn-in 샘플링, stress screen (Don: Apple stress 기반 경험) |
| **포고핀 마모·오염** | 정상 보드가 FAIL, 재테스트하면 PASS | golden unit 주기 검사, 재테스트 PASS율 추적 |
| 계측기 drift·케이블 손실 변화 | 전체 RF 측정값이 서서히 이동 | 일일 golden 측정 + path loss 재보정, SPC 관리도 |
| 스테이션 SW·limits 버전 불일치 | 라인마다 판정이 다름 | 결과 레코드에 sw/limits 버전, 시작 시 서버 버전 확인 |

## 4. 테스트 레벨 — 누가, 어디서, 언제

| 레벨 | 누가 | 무엇을 | 어디서 | CI 단계 |
|---|---|---|---|---|
| unit | FW 개발자 · test FW 개발자 | test FW 명령 파서, 판정 로직, limits 로더, 결과 직렬화 | 호스트 (pytest / C unit) | PR |
| component | FW Test | 실제 보드 1장 + test FW: 각 self-test 명령이 의미 있는 값을 주나 | 벤치 | PR (리그 있으면) |
| integration | FW Test + RF Test | 스테이션 SW ↔ 계측기 ↔ DUT 전체 시퀀스, 가짜 MES | 스테이션 복제본 1대 (lab station) | nightly |
| HIL (스테이션 회귀) | FW Test | **known-good / known-bad 보드 세트**로 판정 정확도 회귀 | lab station + golden 랙 | release (스테이션 SW 배포 전) |
| system | Manufacturing + FW Test | 라인 전체: takt, routing, 오프라인 동기화, 재작업 흐름 | 파일럿 라인 (PVT) | release · NPI 단계 |
| field | Quality | 출하 후 고장 ↔ 공장 측정값 상관 (genealogy로 역추적) | 필드 반품 분석 | 지속 |

- 공장 스테이션 SW도 **릴리즈 대상 소프트웨어**다: PR → lab station → 파일럿 스테이션 1대 → 전체 라인 단계 배포 [추정]

## 5. 테스트베드 설계

### 5.1 라인 구성 (예시) [추정]

```text
 SMT ─► [S1 보드 FCT] ─► 조립 ─► [S2 RF conducted] ─► [S3 최종 FCT + IMU cal] ─► [S4 EOL 시스템] ─► 포장
          포고핀 bed            shield box,             모터·전류·센서·          GCS 바인딩, 영상
          SWD 플래시            power meter/SA          턴테이블 cal              수신 확인, 최종
          레일·주변장치          conducted TX/RX                                   provisioning 검증
              │                     │                        │                       │
              └──────── station PC (Python) ── local queue ──┴──► results DB / MES ◄─┘
                                                                     ▲
                                golden unit 랙 (매 교대 시작 · 4시간마다) ┘
```

### 5.2 스테이션 하나의 내부 구조

```text
 station_app (operator UI: scan → start → PASS/FAIL 큰 글씨)
   ├─ sequencer (단계 정의 = YAML, 조건부 skip, 재시도 정책)
   ├─ limits (limits.yaml, product/rev 별, 버전 해시 기록)
   ├─ drivers:  dut_cli · swd_flasher · psu(SCPI) · dmm · power_meter · sa · turntable · relay_matrix
   ├─ station_health: fixture cycle count, golden 결과, path-loss 테이블 유효기간
   └─ results: 로컬 SQLite 큐 → MES/DB 업로더 (멱등, 재시도)
```

### 5.3 장비 표 (스테이션 1대 기준, 대략) [추정]

| 스테이션 | 주요 장비 | 대략 비용 |
|---|---|---|
| S1 보드 FCT | 포고핀 픽스처(제품별 맞춤), SWD 프로브, 프로그래머블 PSU, DMM/ADC 보드, relay matrix, 산업용 PC | $8–20K |
| S2 RF conducted | shield box, power meter 또는 저가 SA, 신호 발생기(sensitivity), 고정 attenuator·RF 스위치, 케이블 | $20–60K |
| S3 최종 FCT + cal | 지그, 모터 부하·전류 측정, 2축 턴테이블 또는 6-face 지그, 바코드 스캐너 | $10–30K |
| S4 EOL 시스템 | 실제 GCS/핸드셋 golden, 영상 캡처, 바인딩 자동화, shield tent | $5–15K |
| golden 랙 | 각 제품·rev 별 known-good 3대 + known-bad 세트(의도적 결함) | 보드 비용 |

### 5.4 자동화 구조 (pytest를 스테이션 SW 엔진으로 쓸 경우) [추정]

- fixture: `station` (session: 계측기 열기, self-check), `dut` (function: 바코드 → serial, 전원 ON, 플래시, 끝나면 전원 OFF), `limits` (session: product/rev 로딩), `meas` (측정값 기록 헬퍼 `meas.record("vbat_3v3", 3.31, "V")`)
- 각 단계 = 테스트 함수, 판정 = `limits.check(name, value)`, 결과 = JSON 레코드 → 업로더
- 대안: OpenHTF(구글 오픈소스 제조 테스트 프레임워크), 상용 TestStand. trade-off는 7절
- 같은 드라이버(`dut_cli`, `psu`)를 HIL 리그와 공유 → [N04 문제 A](../notes/2026-10-02_N04_system_design_test_tooling.md), [문제 04 conftest](../python/problems/04_hil_conftest.md)

### 5.5 스테이션 대수 산정 — takt 계산

- 주당 1,200대 [확인됨 컨텍스트], 1교대 8시간 × 5일 = 2,400분 → **takt ≈ 2.0분/대**
- 가동률 85%, 재테스트 5% 가정 [추정] → 유효 takt ≈ 2.0 × 0.85 / 1.05 ≈ **1.6분 = 97초**
- 스테이션별 사이클(로드·언로드 포함) [추정]: S1 60초 → 1대, S2 120초 → **2대**, S3(cal 포함) 150초 → **2대**, S4 90초 → 1대
- 2교대 또는 볼륨 2배가 되면 대수 2배가 아니라 **가장 긴 단계(병목)부터** 병렬화. 다중 DUT 픽스처(2-up, 4-up)가 대수보다 싸다
- 상세 takt 즉석 계산법: [onsitePrep N08 8절](../../onsitePrep/site/notes/2026-10-01_N08_factory_test_firmware.html)

## 6. 대표 테스트 케이스

| ID | 시나리오 | 자극 stimulus | 관측 | 합격 기준 (예) [추정] |
|---|---|---|---|---|
| F01 | 전원 레일 | PSU 5.0V/1A 제한, 부팅 | 3V3·1V8 레일, idle 전류 | 3.3V ±3%, idle 80–150 mA |
| F02 | IMU 존재·노이즈 | test FW `imu selftest` | WHO_AM_I, 정지 노이즈 RMS, built-in self-test 응답 | ID 일치, gyro noise < 0.05 dps RMS |
| F03 | 버스 신뢰성 | IMU·baro 1,000회 read | NACK·CRC 오류 수 | 0회 |
| F04 | 모터 출력 4채널 | DShot 낮은 throttle 순차 | 채널별 전류 상승, 순서 | 각 채널 전류 > X mA, 순서 일치 (배선 뒤바뀜 검출) |
| F05 | IMU 캘리브레이션 | 턴테이블 6자세 / 회전 | bias·scale 추정 → 기록 → 재측정 잔차 | 잔차 < 0.5°/s, cal 값이 모집단 ±4σ 안 |
| F06 | RF conducted TX | 고정 채널·전력으로 CW/변조 송신 | power meter 출력, frequency error | 목표 ±1.5 dB, freq error < ±20 ppm |
| F07 | RF sensitivity | 신호 발생기로 감쇠하며 패킷 송신 | PER at 기준 레벨 | 기준 레벨에서 PER < 1% |
| F08 | provisioning | serial·config·키 쓰기 | read-back, 서명 검증 | 100% 일치, 재쓰기 시 거부(쓰기 1회) |
| F09 | 바인딩·영상 (EOL) | golden 핸드셋과 바인딩, 영상 시작 | 링크 수립 시간, 영상 프레임 수신 | 바인딩 < 5초, 영상 5초간 drop 0 |
| F10 | routing 위반 | S2를 건너뛴 serial을 S3에 스캔 | 스테이션 동작 | 시작 거부 + 사유 표시 |
| F11 | 스테이션 self-check | 교대 시작 golden unit 측정 | golden 기준 대비 편차 | 모든 항목 기준 ± guard band, 실패 시 스테이션 lock |
| F12 | GR&R | 10대 × 3명 × 3회 | 측정 분산 분해 | %GR&R < 10% (10–30% 조건부) |

## 7. 면접 시나리오 — "Design a factory test system for our flight controller and drone"

### 요구사항 질문 (영어)

- "What volume and how many shifts? Is the goal to scale the current line or design for the next 5x?"
- "Which defects escape today — do we have field-return data by failure type?"
- "What has to be provisioned per unit: serial, radio binding keys, calibration, config?"
- "Do we flash a dedicated test image, or does production firmware have a test mode?"
- "Does the factory have reliable network, or must stations work offline?"
- "Who owns limits — RF test, firmware test, or manufacturing engineering?"

### 가정 숫자 [추정]

- 주 1,200대 (현재) → 5x 대비, takt ≈ 97초(1교대), 제품 2종 · board rev 2개
- 측정값: 대당 ~200개 × 4 스테이션 → 주당 ~100만 행 → DB 한 대로 충분 ([N04 문제 C](../notes/2026-10-02_N04_system_design_test_tooling.md) 스키마의 `kind='factory'`)

### 블록도

- 5.1 라인 구성 + 5.2 스테이션 내부 구조를 그대로 그린다

### 핵심 결정 & trade-off

| 결정 | 선택 | trade-off |
|---|---|---|
| DUT 이미지 | **별도 test FW → 마지막에 production 플래시** (초기) | 기능 노출이 깔끔·보안상 유리 / 플래시 2회 시간. production test mode는 빠르지만 필드에 test 명령이 남음 → [onsitePrep N08 3절](../../onsitePrep/site/notes/2026-10-01_N08_factory_test_firmware.html) |
| 판정 위치 | 스테이션 (limits.yaml, 버전 관리) | DUT FW 수정 없이 리밋 변경 / 스테이션 SW 배포 관리 필요 |
| 프레임워크 | pytest 기반 자체 (HIL과 드라이버 공유) vs OpenHTF vs TestStand | 팀이 이미 Python·pytest / 오퍼레이터 UI와 리포트는 직접 만들어야 |
| 결과 저장 | 스테이션 로컬 큐 → 멱등 업로드 | 네트워크 장애에도 라인 정지 없음 / 동기화 지연 |
| 스테이션 신뢰성 | golden unit + known-bad + SPC drift 알림 + 픽스처 cycle count | 일일 몇 분의 생산 시간 소모 / 스테이션이 "거짓 FAIL·거짓 PASS"를 내지 않게 하는 유일한 방법 |
| 테스트 범위 | 실패 데이터로 계속 줄이기 (Pareto 0건 1개월 → 샘플링 전환) | 시간 절약 / 저빈도 결함 놓칠 위험 → 샘플링으로 감시 유지 |

### 실패 모드

- 네트워크 다운 → 로컬 큐로 계속 테스트, 복구 후 업로드 (MES routing 확인은 캐시 + 사후 대조)
- 스테이션 SW 버그로 전 제품 FAIL → 단계 배포(파일럿 스테이션 먼저) + 즉시 롤백 경로
- 포고핀 마모로 거짓 FAIL 증가 → 재테스트 PASS율이 임계(예: 3%) 넘으면 알림 + 핀 교체 [추정]
- 거짓 PASS(가장 위험) → known-bad 세트 정기 투입: 반드시 FAIL해야 함

### 영어 2분 요약

> "I'd split the line into stations by what each can physically observe: a pogo-pin board test right after SMT, a conducted RF station in a shield box, a final functional test with IMU calibration on a turntable, and an end-of-line system check that actually binds to a ground unit and receives video. The device runs a small test firmware that only measures; the station owns pass/fail through a versioned limits file, so changing a limit never means reflashing. Every result — serial, station, fixture, software and limits version, and every raw measurement — goes into a local queue and syncs idempotently to MES, so the line keeps running when the network doesn't. Then I treat the stations themselves as things that fail: golden units at every shift start, a known-bad set that must fail, fixture cycle counts, and drift alarms on the measurements. Station count comes from takt: at twelve hundred a week on one shift that's about ninety-seven seconds per unit, so the long RF and calibration steps get two stations or multi-up fixtures first."

## 8. 꼬리 질문

**Q. How do you set the limits in the first place?**

> "Start from the design spec and the EVT/DVT data, then measure a large PVT population and set limits from the distribution — typically mean plus or minus several sigma, inside the spec, with a guard band for measurement uncertainty. Then revisit them with production data, never silently."

**Q. A station's failure rate suddenly doubles. What do you do?**

> "First decide if it's the product or the station: retest the failures on a second station and run the golden unit. If the golden drifts or retests pass, it's the station — fixture, cable or instrument. If failures are real and clustered by date code or supplier lot, it's the product, and genealogy tells me which units are affected."

**Q. How do you make sure the station never passes a bad unit?**

> "A known-bad set with deliberate defects goes through every station on a schedule and must fail on exactly the expected step. That, plus GR&R, is how I know the test detects what it claims to."

**Q. Test firmware is separate — how do you make sure the shipped unit has the right production firmware?**

> "The last step flashes production firmware, then reads back the hash and the provisioned data and records both. The end-of-line station verifies the unit boots the production image and refuses to pass anything still in test mode."

**Q. How do you keep station software consistent across lines?**

> "Station software and limits are versioned artifacts, deployed through the same CI as firmware — pilot station first, then the line. Each station checks it's on the approved version at startup, and every result records the versions it was judged with."

## 9. Don 경험 연결 (레쥬메 범위)

| 이 노트의 포인트 | Don 경험 | 말할 문장 |
|---|---|---|
| 스테이션 아키텍처 | Apple **factory test-node 아키텍처** | "I've worked on factory test-node architecture, so I think about what each node can observe and what it must decide." (Don: 노드 구성과 판정 위치의 실제 예) |
| latent defect | Apple **stress 기반 latent defect 발굴** | "Some defects only show up under stress, so I'd plan a stress screen or sampling burn-in, not just a functional pass." (Don: 어떤 stress였는지) |
| NPI 단계 | **bring-up → NPI → MP** | "I've carried products from bring-up through NPI to mass production, so EVT/DVT/PVT test readiness is familiar ground." |
| 리밋·마진 | **HW safety margin sign-off** | "Setting limits is a margin decision — I've signed off on hardware safety margins." |
| 스테이션 SW 플랫폼 | SK hynix **챔버 테스트 플랫폼 + SDK** (스케줄링, 상태, UART 시퀀스) | "I built a test platform and SDK other engineers ran every day — station software is the same problem with an operator in front of it." |
| 건강 모니터링 | SK hynix **shmoo · health monitoring debug FW**, chip reliability system | "Shmoo and health monitoring taught me to look at distributions and drift, not just pass/fail." |

- 더 깊게: [onsitePrep N08 13·14절 — Don 경험 매핑, 30/60/90일 계획](../../onsitePrep/site/notes/2026-10-01_N08_factory_test_firmware.html) · 연결 노트: [S08 시스템 통합·릴리즈](2026-10-03_S08_system_integration_release.md)

## 체크

- [ ] 5.5 takt 계산을 숫자 바꿔(주 2,400대, 2교대) 1분 안에 다시 해 보기
- [ ] 7절 영어 2분 요약을 소리 내어 두 번
- [ ] "DUT는 측정, 스테이션은 판정" + "known-bad 세트"를 각각 한 문장 영어로
- [ ] 3절 표에서 "스테이션 자체가 고장 나는" 3행을 보지 않고 말하기
- [ ] onsitePrep N08 3·8·9절과 이 노트 5·7절을 이어서 읽기
