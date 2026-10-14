# 🔧 M3 HW design / Test design 45분 — 고장 난 PDB, 테스트 안 된 서브시스템

> 리크루터가 든 예: (1) **고장 난 PDB와 회로도를 주고 어떻게 고칠지 · 어떻게 접근할지**, (2) **새 서브시스템을 통합해야 하는데 테스트가 안 됐다 — 어디서부터, 어떤 순서로, 결과를 어떻게 정리하나**. 정답보다 **생각의 순서**를 본다.

> 🆕 10-14 확정 일정: 1:1 **Michael Honar 45분 (13:00~13:45)** — FW Test Manager, embedded/FPGA 배경이라 이 주제일 가능성이 가장 높다 [추정]. 투어(10:30)도 그와 함께 하므로 **투어에서 본 공장 · 테스트 장비를 답에 끌어오면** 좋다 ("the station we saw on the tour ...").

## 1. PDB가 뭐고 무엇이 들어 있나 (드론 기준)

| 블록 | 하는 일 | 흔한 고장 |
|---|---|---|
| 배터리 입력 (XT60/XT90 패드) | 팩 전압 입력. 6S면 만충 25.2 V | 납땜 불량 · 역극성 연결 |
| 큰 구리 면 / 패드 | ESC 4개로 수십~백 A 분배 | 단면적 부족 → 발열 · 전압 강하 |
| 벌크 커패시터 (전해) | ESC 스위칭 · 모터 역기전력 스파이크 흡수 | 없음/용량 부족/정격 전압 부족 → 스파이크로 레귤레이터 사망 |
| TVS 다이오드 | 과전압 클램프 | 잘못된 방향 · 정격 |
| BEC (buck 레귤레이터 5 V / 9~12 V) | FC · 영상 · 수신기 전원 | 입력 최대 전압 초과 · 피드백 저항 오류 · 인덕터 포화 · 발열 |
| 전류 센서 (shunt + 증폭기 또는 홀 센서) | 전류 → 전압 → FC ADC | 스케일/오프셋 틀림 · 노이즈 |
| VBAT 분압기 | 팩 전압 → FC ADC 범위로 | 비율 틀림 → 배터리 경고 오작동 |
| 필터 (LC) | 영상 · 아날로그 노이즈 감소 | 공진 · 노이즈 |

### 회로도 볼 때 계산 몇 개 (외워 두면 바로 쓴다)

| 항목 | 식 | 예 |
|---|---|---|
| VBAT 분압 | V_adc = V_bat × R2/(R1+R2) | 10k/1k → 25.2 × 1/11 = 2.29 V (< 3.3 V ADC ✓). 스파이크 35 V면 3.18 V — **여유가 거의 없음** |
| buck 출력 | V_out = V_ref × (1 + R_top/R_bottom) | V_ref 0.8 V, 5 V 원하면 R_top/R_bottom = 5.25 |
| shunt 전압 | V = I × R | 0.25 mΩ × 100 A = 25 mV → 증폭기 gain이 맞아야 ADC 범위에 맞음. 손실 P = I²R = 2.5 W |
| 정격 여유 | 부품 정격 ≥ 최대치 × 1.5~2 [관례] | 6S(25.2 V) 입력 레귤레이터는 최소 35~40 V 이상 정격 |

## 2. 고장 난 PDB — 접근 순서 (말로 30초 버전)

> "First I don't power it. I read the schematic for obvious rating mistakes, inspect the board, and check for shorts with a meter. Then I power it from a current-limited bench supply, ramp the voltage, and measure each rail against the schematic. When something is off I isolate that block — lift a part, cut the load — and confirm the root cause with a measurement before I change the design. Finally I add the check that would have caught it to the board test."

| 단계 | 할 일 | 왜 |
|---|---|---|
| 0. 증상 확인 | 어떤 증상? 언제부터? 몇 장 중 몇 장? 다른 보드는? | 설계 문제(전부) vs 조립 문제(일부) 구분 |
| 1. 회로도 리뷰 | 정격 전압 · 전류 · 극성 · 피드백 저항 계산 · enable 핀 · 데이터시트 권장 회로와 비교 | 전원 넣기 전 가장 싼 단계 |
| 2. 육안 · 현미경 | 납땜 브리지 · 부품 방향(다이오드, 전해) · 빠진 부품 · 탄 자국 | |
| 3. 무전원 측정 | 입력-GND, 각 레일-GND 저항 (단락?) · 다이오드 모드 | 단락이면 전원 넣는 순간 더 망가짐 |
| 4. 전류 제한 전원 | 벤치 파워서플라이 전류 제한 낮게, 전압을 천천히 올리며 전류 관찰 | 단락/이상 소모를 안전하게 찾음 |
| 5. 레일 측정 | 각 레일 전압 · 리플(스코프) · 부하 걸었을 때 | 회로도 기대값과 비교 |
| 6. 열 | 열화상 카메라 또는 손가락 대신 열전대 | 단락 지점은 뜨겁다 |
| 7. 부하 · 과도 | 전자 부하로 정격 전류, 스로틀 급변 흉내 → 스코프로 스파이크 | 실제 비행 조건 |
| 8. 분리 | 의심 블록만 떼어 내기(부품 제거, 0Ω 점퍼 제거) | 원인을 하나로 좁힘 |
| 9. 확인 · 기록 | 원인 수정 후 같은 측정으로 재확인, 정상 보드와 비교 | |

### 증상별 첫 의심

| 증상 | 먼저 볼 것 |
|---|---|
| 배터리 연결하자마자 스파크 · 연기 | 입력 단락, 역극성, 전해 커패시터 방향 |
| 5 V 레일이 0 V | 레귤레이터 enable 핀, 입력 도달 여부, 출력 단락, 레귤레이터 사망(입력 정격 초과) |
| 5 V가 아니라 6.x V | 피드백 저항 값/위치 바뀜 |
| 스로틀 올리면 FC 리셋 | 전압 강하(brownout) · 스파이크 → 벌크 커패시터, 레일 리플 |
| 배터리 전압 표시가 틀림 | 분압 비율, ADC 기준 전압, 펌웨어 스케일 값 |
| 전류 값이 0 또는 엉뚱함 | shunt 방향, 증폭기 gain, 오프셋, 펌웨어 scale/offset |
| 영상에 줄 · 노이즈 | 필터, 접지 경로, 영상 전원 레일 리플 |
| 레귤레이터 뜨거움 | 출력 과부하, 스위칭 주파수/인덕터, 입력 전압 너무 높음(LDO면 손실 = (Vin−Vout)×I) |

> **테스트 직군 마무리:** "Then I'd add a board-level test: rail voltages under load, ripple limit, and current-sensor calibration at two points — so the factory catches this, not the pilot."

## 3. "고친다"는 것을 정리하는 법 (보고)

| 칸 | 내용 |
|---|---|
| 증상 | 측정값 vs 기대값 (숫자 + 단위 + 조건) |
| 영향 범위 | 몇 장 / 어떤 리비전 / 어떤 조건 |
| 원인 | 하나의 문장 + 증거(사진 · 파형 · 측정) |
| 임시 조치 | 리워크 방법 |
| 영구 조치 | 회로도/BOM/레이아웃 변경 |
| 검증 | 같은 측정을 다시 · N장 · 조건 |
| 재발 방지 | 추가한 테스트 항목 · 설계 리뷰 체크리스트 |

## 4. 테스트 안 된 새 서브시스템 통합 — 생각의 순서

> 예: 새 GPS 모듈, 새 ESC, 새 무선 모듈, 새 카메라 · 짐벌. 질문: "We need to integrate it and nobody has tested it. Where do you start?"

1. **무엇을 보장해야 하나** — 요구사항/사용 방식: 시스템이 이걸 어떻게 쓰나, 무엇이 깨지면 위험한가 (안전 > 기능 > 성능)
2. **인터페이스 정의서(ICD) 확인** — 전원, 핀, 프로토콜, 타이밍, 데이터 형식. 없으면 이게 첫 산출물
3. **위험 순서로 계획** — 가장 크게 망가지는 것 · 가장 모르는 것을 먼저
4. **아래에서 위로 쌓는다** (5절 표)
5. **결과를 남기는 형식을 먼저 정한다** (6절)
6. **자동화할 부분 결정** — 반복할 것은 HIL/스크립트로, 한 번만 할 것은 수동 + 기록

## 5. 아래에서 위로 — 테스트 단계

| 단계 | 하는 일 | 통과 기준 예 |
|---|---|---|
| 1. 입고 · 육안 | 리비전, 커넥터, 핀아웃이 문서와 같은가 | 불일치 0 |
| 2. 전원 (smoke) | 전류 제한 전원으로 단독 전원, 소모 전류 | 데이터시트 범위 안 |
| 3. 통신 연결 | 버스에서 응답하나 (I2C 주소 스캔, ID 레지스터, UART 기본 메시지) | ID 일치, 에러 0 |
| 4. 기능 | 기본 기능 하나씩 (GPS fix, ESC 회전, 카메라 영상) | 기능별 기준 |
| 5. 성능 · 특성화 | 지연, 처리량, 정확도, 온도 · 전압 범위 | 기준이 없으면 **측정해서 기준을 만든다** |
| 6. 경계 · 고장 주입 | 전원 흔들기(brownout), 케이블 뽑기, 깨진 데이터, 버스 노이즈, 재부팅 중 명령 | 안전한 상태로 가고 복구한다 |
| 7. 시스템 통합 | 실제 기체에 붙여서: 모터 켠 상태 EMI, 진동, 다른 서브시스템과 타이밍 | 다른 기능 회귀 없음 |
| 8. 내구 · 환경 | 장시간, 온도 사이클, 진동 | 시간당 에러율 |
| 9. 회귀 자동화 | HIL에 넣어서 FW 바뀔 때마다 | CI에서 녹색 |

> 영어 한 줄: "I'd go bottom-up — power, then talking on the bus, then function, then performance and fault injection — but I'd order the work by risk, so the thing that could crash the aircraft gets tested first."

## 6. 결과 정리 — 아티팩트

| 아티팩트 | 내용 |
|---|---|
| 테스트 계획 (1쪽) | 범위 · 위험 · 단계 · 장비 · 진입/종료 기준 |
| 테스트 케이스 표 | ID · 목적 · 절차 · 기대값 · 결과 · 증거 링크 |
| 원시 데이터 | 로그 · 파형 · 사진 — run id · DUT serial · FW 버전 · 날짜로 묶음 |
| 요약 리포트 | pass/fail 수 · **커버리지 표**(요구사항 × 테스트) · 열린 이슈(심각도) · 권고(go / no-go / 조건부) |
| 대시보드 | run별 결과 추세, 실패 상위 항목 |
| 버그 리포트 | 재현 절차 · 기대 vs 실제 · 빈도 · 환경 · 증거 |

> **"done의 의미"**(이전 라운드 질문) 와 연결: "Done means the requirement has a test, the test ran on the real build, the result is stored where anyone can find it, and it runs again automatically next time."

## 7. 연습 시나리오 (각 10분, 말로)

| # | 상황 | 체크 포인트 |
|---|---|---|
| 1 | 새 PDB rev B: 일부 보드에서 스로틀 급상승 시 FC 리셋 | 일부 vs 전부, brownout 측정, 벌크 cap, 스코프 트리거 |
| 2 | VBAT가 실제보다 0.8 V 낮게 표시 | 분압 오차, ADC 기준, 펌웨어 scale, 두 점 보정 |
| 3 | 새 GPS 모듈 통합, 아무도 테스트 안 함 | ICD, UART 설정, fix 시간 측정, 모터 EMI 영향, 끊김 시 동작 |
| 4 | 새 ESC 펌웨어 버전 받음 | 프로토콜 호환, 모터 응답 지연, 고장 시 정지, 회귀 |
| 5 | 공장에서 10%가 전류 센서 테스트 실패 | 설계 vs 조립 vs 테스트 자체 문제, 기준 재검토 |
