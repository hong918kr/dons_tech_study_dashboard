# 🏭 N08 · Factory Test Firmware — Neros에 없는 것, Don이 채울 수 있는 것

> Michael Honor(FW Test 리드)가 2026-10-01 인터뷰에서 말했다: **factory test firmware가 아직 없고 나중에 필요하다. 테스트 펌웨어가 가장 급하고, 올해 안에 최대 5명을 더 뽑는다** [확인됨]. 주당 약 1,200대를 만드는 회사에 공장 테스트 FW가 없다는 건 큰 빈자리이고, Apple factory test-node · SK hynix 챔버 테스트 플랫폼 · SSD MFG FW를 한 사람이 다 해 본 Don에게는 **가장 큰 기회**다. 이 노트는 개념 → 라인 구조 → 아키텍처 → 프로토콜 → 주변장치별 self-test → 프로비저닝 → 추적성 → takt 계산 → 수율 → 30/60/90일 계획 → 발표 슬라이드 순서로 정리했다.

## 0. 왜 이 노트가 온사이트의 무기인가

- 온사이트 발표 1시간의 주제가 "가장 큰 challenge, debugging, bring-up 경험"이다. 마지막 몇 장을 **"Neros에서 내가 할 일: factory test FW"** 로 끝내면 발표가 지원 동기와 연결된다
- 면접관 세 부류 모두에게 통한다: Adam(Director, 자동차 출신 — EOL 테스트 앱 개선 경력 [확인됨 LinkedIn, HM 준비 노트]), Michael(Test 리드 — 직접 언급), Platform HM(공통 런타임 위에 test mode가 올라감)
- 주의: Neros 내부 라인 구조·스테이션·MES는 **전부 모른다**. 아래 내용은 업계 일반론과 추정이다. 면접에서는 "Here's how I'd approach it — tell me what already exists"로 시작한다

## 1. Factory test firmware란

| 구분 | Production FW | Bootloader | **Factory test FW** |
|---|---|---|---|
| 목적 | 제품 기능 (비행) | 이미지 검증·선택·업데이트 | **이 보드가 제대로 만들어졌는지 증명**하고, 유닛별 데이터를 써 넣는다 |
| 사용자 | 조종사, GCS | 업데이트 도구 | **테스트 스테이션 PC**와 테크니션 |
| 수명 | 필드 전체 | 필드 전체 | 공장 안에서만 (또는 production 안의 잠긴 모드) |
| 성격 | 기능 중심 | 최소·불변 | **측정 중심**: 각 주변장치를 직접 두드리고 숫자를 보고 |
| 실패 시 | 안전 동작 | 이전 이미지 | 정확한 실패 코드와 측정값 |

- 핵심 문장: "Production firmware asks *does the drone fly?* Factory test firmware asks *was this unit built correctly, and can we prove it?*"
- 테스트 FW가 없으면 공장은 보통 production FW를 켜 보고 사람이 눈으로 확인한다 → 느리고, 재현성 없고, 기록이 안 남는다 [일반론]

## 2. 생산 라인에서 어디에 들어가나

| 단계 | 무엇을 | 테스트 FW 역할 | 비고 |
|---|---|---|---|
| ICT / flying probe | 베어 보드 납땜·부품 (오픈·쇼트, 저항값) | 없음 (전원 없이 또는 최소 전원) | 보통 CM이나 SMT 라인 [일반론] |
| 프로그래밍 | 부트로더 + 테스트 이미지 기록 | SWD/JTAG로 플래시 | 스테이션 시간에 포함 |
| **FCT (board functional test)** | 보드 단위 기능: 센서, 버스, 전원 레일, IO | **주역**. 각 주변장치 self-test + 측정 보고 | pogo-pin 픽스처 |
| 캘리브레이션 | 센서 오프셋·스케일, ADC 보정, RF 출력 보정 | 측정 → 계수 계산 → 저장 | 스테이션 또는 FW가 계산 |
| 시스템 / EOL test | 조립된 기체: 모터 회전 방향, 무선 링크, 영상 | 테스트 모드 또는 production FW + 진단 명령 | 라인 끝 |
| Burn-in / soak | 온도·장시간 구동으로 초기 불량 털어내기 | 반복 self-test + 에러 카운터 수집 | 비용 대비 효과 판단 필요 |
| 프로비저닝 | serial, 키, 설정, 최종 production FW | 쓰기 + 검증 + 잠금 | 보안 핵심 |

- Neros의 공장은 Torrance에 있고 직원의 다수가 테크니션이다 (전체 약 260명 중 개발 약 70명 [확인됨 2026-10-01 Michael]) → **테크니션이 쓰기 쉬운 pass/fail 스테이션**이 중요하다
- Neros 제품군: FC, 라디오, 핸드셋·헤드셋(STM32 + FPGA 영상 [확인됨 Tennessee 공고]), GCS → 보드 종류마다 테스트가 다르다 [추정]

## 3. 아키텍처 선택 — 별도 테스트 이미지 vs production FW 안의 test mode

| 기준 | 별도 test image | production FW 안의 test mode |
|---|---|---|
| 플래시 횟수 | 2번 (테스트 → production) | 1번 |
| 스테이션 시간 | 플래시 시간 추가 | 짧음 |
| production 코드 오염 | 없음 | 테스트 코드가 출하 이미지에 남음 |
| 보안 | 출하 이미지에 테스트 진입 경로가 없다 | **잠금 해제 경로가 공격면**이 된다 → 인증된 진입 + 프로비저닝 후 영구 비활성화 |
| 현장 진단 재사용 | 어렵다 | 수리·반품 분석에 재사용 가능 |
| 드라이버 공유 | 공통 HAL·드라이버 라이브러리를 링크해서 공유 가능 | 자연스럽게 공유 |
| 유지보수 | 두 이미지 빌드·버전 관리 | 하나 |

- **추천 (말할 것)**: "Start with a separate test image that links the same drivers as production — it's the fastest to build and keeps test hooks out of what ships. Keep a small, authenticated diagnostic subset in production firmware for repairs and returns."
- **STM32 보안 연결** [추정: 제품별 확인 필요]: 출하 전 디버그 포트를 잠그는 readout protection(RDP) 레벨, option bytes, 키 저장 위치(OTP 영역 등). 레벨에 따라 되돌릴 수 없으므로 **프로비저닝 마지막 단계**에 둔다. 테스트 FW가 이 순서를 강제해야 한다
- 공통 플랫폼과의 관계: Platform 팀의 logging·config·IPC 라이브러리 위에 test mode를 얹으면 FW 팀과 테스트 팀이 같은 드라이버를 쓴다 → Platform HM에게 어필 포인트

## 4. 스테이션 PC ↔ DUT 프로토콜

### 전송
- **USB CDC (가상 시리얼)** 또는 UART. STM32는 대부분 USB FS를 가진다 [일반론] → 픽스처 배선이 단순
- 링크가 하나뿐이면 테스트 명령과 로그가 섞이지 않게 프레이밍한다

### 형식: 처음엔 텍스트, 필요하면 이진

```text
 Station -> DUT : "IMU.SELFTEST\n"
 DUT -> Station : {"cmd":"IMU.SELFTEST","ok":true,"whoami":"0x..","gyro_st":[12.1,11.8,12.4],"t_ms":180}
 Station -> DUT : "ADC.READ VBAT\n"
 DUT -> Station : {"cmd":"ADC.READ","ok":true,"ch":"VBAT","mv":16742}
```

| 선택 | 장점 | 단점 |
|---|---|---|
| 텍스트 명령 + JSON 한 줄 응답 | 테크니션·엔지니어가 터미널로 직접 디버그, Python 파싱 쉬움 | 느림, 크다 |
| 이진 프레임 (길이 + CRC) | 빠름, 대량 데이터(센서 덤프) | 디버그 도구 필요 |

- 규칙: **DUT는 측정값만 보고하고, 합격 판정은 스테이션이 한다** → limit을 FW 재빌드 없이 바꾼다
- 모든 응답에 FW 버전·빌드 hash를 조회하는 명령 (`SYS.INFO`) → 추적성
- 명령마다 타임아웃, 모든 명령은 **멱등**(다시 보내도 안전)

## 5. 주변장치별 self-test (드론 FC · 라디오 · 핸드셋)

| 대상 | DUT 혼자 할 수 있는 것 | 픽스처가 필요한 것 | 합격 기준 예 |
|---|---|---|---|
| IMU (SPI) | WHO_AM_I, 레지스터 R/W, **내장 self-test**(자극 인가 시 출력 변화), 정지 상태 노이즈 | 정해진 방향으로 고정 (accel 1 g 축 확인) | ID 일치, self-test 응답이 데이터시트 범위, 노이즈 RMS < 한계 |
| 기압계 (I2C) | ID, 측정값 범위 | 기준 기압계 대비 비교 | 기준 대비 ±오차 |
| 자력계 | ID, self-test (있으면) | 자기장 환경 영향 주의 | 범위 |
| 외부 flash | JEDEC ID, 쓰기·읽기·지우기 패턴 | 없음 | 패턴 일치, 시간 |
| UART 포트 | — | **루프백 커넥터** (TX↔RX) | 바이트 패턴 일치, 여러 baud |
| 모터 출력 (DShot/PWM) | 출력 생성 | **픽스처 MCU나 로직 애널라이저로 캡처** | 채널 순서, 값, 타이밍 |
| ADC (배터리 전압·전류) | 읽기 | **정밀 전원·부하로 기준값 인가** | 기준 대비 오차 → 보정 계수 산출 |
| 전원 레일 | 내부 ADC 측정 (있으면) | DMM으로 측정 | 3.3 V ± 허용 |
| GPIO / LED / 버튼 | 출력 토글 | 픽스처 입력으로 읽기, 광센서 | 상태 일치 |
| RF (라디오) | 시험 모드에서 고정 주파수 송신, RSSI 보고 | **차폐 박스 + 파워미터 / 기준 송신기** | 출력 전력·주파수 오차·감도 범위 |
| 영상 (핸드셋·헤드셋) | 테스트 패턴 출력 | 카메라·캡처 장치 [추정] | 패턴 인식 |
| 리셋·부팅 | 부팅 시간, 리셋 원인 | 전원 사이클 | 시간 상한 |

- **"DUT 혼자" 테스트를 먼저**: 픽스처 없이 돌아가는 것만으로도 보드 불량의 상당 부분을 잡는다 → 1단계 목표
- **RF는 Don의 Apple RF 경험과 직결** — 공장 RF 테스트는 별도 팀(Lead RF Test & Integration Engineer 공고 [확인됨])과 경계를 맞춰야 한다

## 6. 캘리브레이션 · 프로비저닝

| 데이터 | 생성 | 저장 위치 | 주의 |
|---|---|---|---|
| Serial number | MES/서버가 발급 (중복 방지) | 보호된 flash 영역 또는 OTP [추정: 하드웨어 따라] | 라벨·바코드와 일치 검증 |
| 보정 계수 (IMU 오프셋, ADC 스케일, RF 출력) | 스테이션 측정 → 계산 | 사용자 설정과 **분리된** factory 영역 + CRC | 사용자 리셋으로 지워지면 안 됨 |
| 암호 키 / 인증서 | 서버·HSM | 보호 영역, 주입 후 읽기 차단 | **스테이션 로그에 절대 남기지 않음** |
| 하드웨어 revision | BOM 기준 | factory 영역 | FW가 보드 변형 판단에 사용 |
| 최종 production FW | 릴리즈 서버 (서명된 이미지) | 메인 슬롯 | 해시 검증 |
| 디버그 잠금 (RDP 등) | 마지막 단계 | option bytes [추정] | 되돌릴 수 없을 수 있음 → 모든 검증 후 |

- **순서가 곧 설계**: 테스트 → 보정 → serial → 키 → production FW → 검증 → 잠금. 테스트 FW나 스테이션이 이 순서를 강제한다
- 모든 단계는 **재실행 가능**해야 한다 (중간에 전원이 나가도 처음부터 다시)

## 7. 결과 기록과 추적성

- 유닛마다 저장: **serial ↔ 보드 revision ↔ 테스트 FW hash ↔ production FW hash ↔ 스테이션 ID ↔ 픽스처 ID ↔ limits 버전 ↔ 측정값 전부(pass든 fail이든) ↔ 시각 ↔ 작업자**
- 측정값을 pass/fail만이 아니라 **숫자로** 남기는 이유: 분포를 봐야 limit이 맞는지, 공정이 흘러가는지(drift) 안다
- MES(manufacturing execution system)가 있으면 연동, 없으면 처음엔 DB + 대시보드 [추정: Neros 현황 모름]
- 방산 고객(Army, USMC)에게는 **어떤 유닛이 어떤 테스트를 통과하고 출하됐는지** 증명이 중요하다 [추정]
- 필드 반품 → serial로 공장 측정값 조회 → "공장에서 경계선이었나?" → limit이나 설계 피드백

> "Every unit should carry its own history: which firmware, which station, which limits, and every measured value. When a unit comes back from the field, that's where root cause starts."

## 8. 테스트 시간 예산 — takt 계산 (즉석에서 할 수 있게)

| 항목 | 값 | 출처 |
|---|---|---|
| 생산량 | 주당 약 1,200대 | 공개 보도 [확인됨, 컨텍스트 파일 §2] |
| 근무 | 주 5일, 1교대 8시간 [가정] | |
| 하루 생산 | 1,200 / 5 = **240대/일** | |
| 가용 시간 | 8 h × 3,600 × 가동률 75% = **21,600초** | 가동률 가정 |
| takt | 21,600 / 240 = **90초/대** | |
| FCT 테스트가 180초라면 | 180 / 90 = **스테이션 2대** 필요 (여유 두면 3대) | |
| 2교대면 | takt 180초 → 1대로 가능 | |
| 2028 목표 연 100만 대 | 주 약 19,200대 = **현재의 약 16배** → takt 약 6초(1교대) | 회사 발표 [확인됨] |

- 결론 문장: "At today's rate the takt is about 90 seconds a unit on one shift, so test time is already a cost driver. At the 2028 target it's roughly sixteen times that, so the test architecture has to parallelize — multiple DUTs per fixture, tests running concurrently on the DUT, and only the tests that actually catch defects."
- 시간 줄이는 레버: DUT 안에서 테스트 병렬 실행, 한 픽스처에 여러 보드(panel test), 실패 이력 없는 테스트 샘플링, 플래시 시간 단축(이미지 크기, 고속 인터페이스)

## 9. 수율 · Pareto · GR&R

- **First pass yield (FPY)**: 첫 시도 통과 비율. 재테스트 통과는 따로 센다 (재테스트가 많으면 픽스처나 limit 문제)
- **Pareto**: 실패 코드별 빈도 → 상위 몇 개가 대부분. 매주 Pareto를 보고 상위 원인부터 설계·공정·픽스처 팀에 넘긴다
- **GR&R (gauge repeatability & reproducibility)**: 같은 보드를 여러 번, 여러 스테이션·작업자로 측정해서 **측정 시스템의 흔들림이 limit 폭에 비해 작은지** 확인. 흔들림이 크면 거짓 실패·거짓 합격 [일반론]
- **Golden unit / limit sample**: 교대 시작마다 알려진 정상 보드로 스테이션 자체 점검, 알려진 불량 보드로 실패 검출 확인
- **Limit 설정**: 초기엔 설계 사양 기반 → 수백 대 데이터 분포로 조정 (예: 평균 ± k·σ와 사양 중 더 엄격한 쪽 [일반론])

## 10. 픽스처 설계 고려사항

- pogo-pin 테스트 포인트를 **보드 설계 단계에서** 확보 (DFT: design for test) → EE 팀과 초기부터 협업
- 전원은 픽스처가 공급하고 전류를 측정 (쇼트 보드로 픽스처가 타지 않게 전류 제한)
- 루프백, 부하, 기준 신호를 픽스처에 내장
- 접촉 횟수 수명 관리(핀 교체 주기), 접촉 저항 점검
- 정전기(ESD), 테크니션 작업성 (한 손 장착, 실수 방지)
- 픽스처 ID를 결과에 기록 → 특정 픽스처의 불량률 이상 탐지

## 11. 스테이션 소프트웨어 (Python)

```python
# 개념 스케치 — 실제 구조는 Neros 현황에 맞춘다
def run_fct(serial, dut, instruments, limits):
    results = []
    for test in limits.tests:                    # 순서는 limits 파일이 정한다 (버전 관리)
        raw = dut.command(test.cmd, timeout=test.timeout_s)
        verdict = test.evaluate(raw)              # 판정은 스테이션에서
        results.append({"name": test.name, "raw": raw, "pass": verdict})
        if not verdict and test.stop_on_fail:
            break
    record(serial, dut.info(), limits.version, results)   # pass든 fail이든 전부 저장
    return all(r["pass"] for r in results)
```

- 구성: sequencer, DUT 드라이버(시리얼 프로토콜), 계측기 드라이버(PSU, DMM, 파워미터 — 보통 SCPI over USB/LAN [일반론]), limits 파일(YAML/JSON, 버전 관리), 결과 업로더, 테크니션 UI(큰 PASS/FAIL)
- **스테이션 코드도 CI로 테스트**: 가짜 DUT·가짜 계측기로 시퀀스 검증 (FW Test 준비 노트의 fake 객체 방식과 같음)
- HIL 프레임워크와 **드라이버를 공유**하면 FW 팀의 HIL 테스트와 공장 테스트가 같은 코드 위에 선다 → Michael 팀(FW Test)과 공장의 연결고리

## 12. 실패 분석 루프

```text
 [Station FAIL] -> [failure code + raw values] -> [Pareto weekly]
        |                                              |
        v                                              v
 [Repair/rework bench: diag commands]          [Top causes -> owner]
        |                                              |
        v                                              v
 [Root cause: component / assembly / design / fixture / limit]  -> [Fix] -> [Verify: FPY change]
```

- 실패 원인 분류: 부품 불량, 조립(납땜) 불량, 설계 마진, 픽스처 문제, limit 문제, **FW 버그**
- Don의 강점: 실패가 FW·보드·부품 중 어디인지 버스·신호 레벨에서 가르는 능력 (Apple 인터페이스 루트코즈)

## 13. Don 경험 매핑

| 레쥬메 | factory test FW에서의 의미 | 영어 한 줄 |
|---|---|---|
| Apple: factory test-node 아키텍처 설계·리드, bring-up → NPI → MP | 라인에 어떤 테스트를 어디에 둘지 설계 | "I designed factory test-node architecture for new silicon from bring-up through mass production." |
| Apple: scenario·stress 기반 case study로 MP 전 latent defect 발굴 | 기능 테스트가 놓치는 불량을 잡는 stress 설계 | "Functional tests miss latent defects; stress-based cases catch them before they ship." |
| Apple: HW safety margin sign-off | limit 설정의 근거 | "I've set pass/fail margins with data." |
| SK hynix: 챔버 테스트 플랫폼 + SDK (온도·스케줄링·상태·UART 시퀀스) | 스테이션 소프트웨어, 다수 DUT 동시 운용 | "I built station-style software that ran UART test sequences on many devices at once." |
| SK hynix: chip reliability system (NAND V6/7, PCIe 3/4/5) | 커버리지 설계, burn-in 성격 | "I designed reliability test coverage across interface generations." |
| SK hynix: shmoo·health monitoring debug FW | 마진 측정 FW, 자체 진단 | "I wrote firmware whose job was to measure margins and health, not to run the product." |
| Summary: SSD/MFG Firmware | 제조용 FW 직접 경험 | "Manufacturing firmware is in my background — (Don: 맡았던 MFG FW 범위 한 줄)" |
| Solidigm: error reporting/handling scheme | 실패 코드 체계 | "I built an error-reporting scheme in production firmware; failure codes are the backbone of a factory Pareto." |

- (Don: Apple test-node에서 공개 가능한 수준의 구체 사례 하나 — 예: 어떤 종류의 stress를 넣었는지)
- (Don: SSD MFG FW에서 했던 일 — 예: 제조 공정용 명령, 초기화, 스크리닝)

## 14. 30/60/90일 계획 — "If I joined, here's how I'd stand up factory test firmware"

| 기간 | 목표 | 산출물 |
|---|---|---|
| 1~30일 | **현황 파악 + 가장 빠른 가치** | 라인 관찰(지금 무엇을 어떻게 확인하나), 실패·반품 데이터 수집, FC 보드 하나에 대해 픽스처 없이 도는 self-test 이미지 v0 (ID 체크, flash, 전원 레일, UART 루프백), 텍스트 명령 프로토콜, Python 스테이션 스크립트 v0 |
| 31~60일 | **한 라인에서 실제 사용** | 픽스처 연동(모터 출력 캡처, ADC 기준 인가), limits 파일 버전 관리, 결과 DB와 serial 추적, 테크니션 UI, golden unit 점검, 첫 주간 Pareto |
| 61~90일 | **확장과 프로비저닝** | 두 번째 제품(라디오나 핸드셋), 캘리브레이션·serial·키 프로비저닝 순서 확립, 보안 잠금 단계, takt 측정과 병렬화, HIL 프레임워크와 드라이버 공유 |

**영어 talking script (약 90초)**

> Michael mentioned there's no factory test firmware yet, and that's the area I'd be most excited to own. Here's how I'd approach it. In the first month, I'd spend time on the line to see how boards are checked today and pull whatever failure and return data exists. In parallel I'd build a first test image for one board — self-tests that need no fixture: device IDs, flash, power rails, UART loopbacks — driven by a simple text protocol and a Python station script, so technicians get a clear pass or fail and we start recording numbers on day one. In the second month, I'd add the fixture side — motor-output capture, reference voltages for ADC calibration — versioned limits, and a results database keyed by serial number and firmware hash, and start a weekly failure Pareto. By three months, I'd extend it to a second product, add calibration and secure provisioning with the debug lock as the last step, and measure test time against takt — at roughly 240 units a day on one shift, that's about 90 seconds a unit, and the 2028 target is sixteen times that. I designed factory test-node architecture at Apple and built chamber test infrastructure at SK hynix, so this is the part of the job where I'd add value fastest.

- 주의: "Michael mentioned…"는 Michael이 그 자리에 없으면 "I heard from the team that…"으로 바꾼다
- 숫자(240대, 90초)는 가정이라고 한 번 말해 둔다: "assuming one shift, five days a week"

## 15. 1시간 발표에 끼워 넣을 슬라이드 (3~4장)

| # | 제목 | 내용 | 시간 |
|---|---|---|---|
| A | "From silicon to factory: what I learned about testing at scale" | Apple test-node + SK hynix 챔버 플랫폼을 한 장에. "기능 테스트는 latent defect를 못 잡는다", "숫자를 남겨야 limit을 고친다" | 3분 |
| B | "Factory test firmware: architecture" | 3절 표 요약 + 4절 프로토콜 + 블록도 (스테이션 PC ↔ DUT ↔ 픽스처 ↔ DB) | 3분 |
| C | "Takt and scale" | 8절 계산: 240대/일 → 90초, 2028 목표 16배 → 병렬화 전략 | 2분 |
| D | "My first 90 days" | 14절 표 | 2분 |

- 발표 전체 흐름에서 **마지막 섹션**("What I'd bring to Neros")으로 둔다. 앞 섹션의 디버깅·bring-up 스토리가 근거가 된다
- 슬라이드에 Neros 내부 사실처럼 보이는 문장을 쓰지 않는다. "Assumption" 라벨을 붙인다
- 발표 전체 구성은 발표 가이드 노트(N03)에서

## 16. 예상 질문과 답

| 질문 | 답의 뼈대 |
|---|---|
| "Separate test image or test mode?" | 별도 이미지로 시작(빠르고 출하 이미지 깨끗), 수리용 인증된 진단 subset만 production에. 3절 |
| "How do you set limits?" | 사양 기반 → 초기 수백 대 분포로 조정 → GR&R로 측정 시스템 확인. 9절 |
| "How do you keep test time down?" | takt부터 계산, DUT 내부 병렬, 다중 DUT 픽스처, 검출 이력 없는 테스트 샘플링. 8절 |
| "How do you protect keys?" | 서버·HSM에서 생성, 로그 금지, 주입 후 잠금, 잠금은 마지막. 6절 |
| "What if the station says fail but the board is fine?" | golden unit, 재테스트 통계, 픽스처 ID별 실패율, GR&R. 9·10절 |
| "How does this relate to HIL in CI?" | 같은 DUT 드라이버·프로토콜을 공유. CI의 HIL은 코드 회귀, 공장은 유닛 불량. 11절 |
| "What would you measure in the first week?" | 현재 라인의 검사 방법, 재작업률, 반품 원인 — 숫자가 없다면 그게 첫 번째 발견 |

## 17. 역질문 (factory test 관련)

- "How are boards checked on the line today, before shipping?"
- "Are boards built in-house end to end, or does a contract manufacturer do SMT and ICT?" [추정: 250,000 sqft Torrance 공장이 있다는 보도]
- "Is there an MES or a results database today?"
- "Who owns test fixtures — manufacturing engineering, or the firmware test team?"
- "For factory test firmware, would that sit with Michael's team or with manufacturing?"

## 체크

- [ ] 1절 표(production FW vs bootloader vs factory test FW)를 영어로 30초에 설명할 수 있다
- [ ] 2절 라인 단계(ICT → 프로그래밍 → FCT → 캘리브레이션 → EOL → burn-in → 프로비저닝)를 순서대로 말할 수 있다
- [ ] 3절 "별도 이미지 vs test mode" trade-off와 내 추천을 말할 수 있다
- [ ] 8절 takt 계산(1,200/주 → 240/일 → 약 90초)을 종이 없이 할 수 있다
- [ ] 14절 30/60/90일 talking script를 90초 안에 말할 수 있다
- [ ] 13절 (Don: …) 칸을 실제 사례로 채웠다 (특히 SSD MFG FW 범위)
- [ ] 15절 슬라이드 A~D를 발표 초안에 넣었다
- [ ] 17절 역질문 2개를 골랐다
