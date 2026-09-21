# J03. Optimize power consumption and thermal performance for always-on, battery-powered operation

> **분류**: Responsibility 3/7 · **관련 개념 노트**: C05(저전력·발열 교과서), C06(무선 전력), C08(추론 예산), S03(면접 드릴)
> **Don 현재 상태**: 🟡 부분 — 레쥬메에 "Power Analyzer"와 "sign off on hardware safety margins (reliability vs performance/power)"가 있어 **측정과 마진 판단**은 증거가 있지만, 배터리 기기의 µA 예산·sleep 설계·thermal 정책 소유 경험은 명시되어 있지 않다.
> **이 노트를 다 읽으면**: ① 제품 목표(사용 시간)에서 시작해 서브시스템별 전력 예산을 top-down으로 만들고 숫자로 협상할 수 있다 ② 전력을 "레일"이 아니라 "소프트웨어 상태"에 귀속시키는 계측·회귀 테스트 체계를 설명할 수 있다 ③ thermal throttling 정책을 누가 왜 소유하는지, 정책 표를 어떻게 설계하는지 말할 수 있다.

---

## 0. 문장 뜯어보기

| 구(句) | 표면적 의미 | 채용담당자가 이 단어를 고른 이유 |
|---|---|---|
| `Optimize` | 최적화 | "구현"이 아니라 "최적화"다. 즉 **이미 동작하는 기기의 숫자를 계속 깎는 반복 작업**이 본업이라는 뜻. 한 번 짜고 끝나는 기능이 아니다. |
| `power consumption` | 전력 소모 | 단위가 mA/µA/mJ인 정량 업무. 측정·예산·회귀 관리가 따라온다. |
| `and thermal performance` | 그리고 발열 | 전력과 발열을 **한 사람이** 본다는 신호. 웨어러블에서 발열은 성능 상한(throttling)이자 안전·규격 문제라 기구·시스템 팀과 붙어 일한다. |
| `for always-on` | 상시 동작 | "쓸 때만 켜지는" 기기가 아니다. 마이크/센서/라디오 중 일부가 24시간 살아 있어야 하고, 그래서 **sleep floor(바닥 전류)** 관리가 핵심이 된다. |
| `battery-powered` | 배터리 구동 | 전원이 유한하다. 용량·전압·충전·노화·보호 회로가 모두 펌웨어 제약으로 들어온다. |
| `operation` | 운용 | "설계"가 아니라 "운용". 출하 후 필드에서도 전력 지표를 본다는 뜻(→ telemetry, OTA로 배포되는 정책 변경). J04와 연결된다. |

**한 문장 요약**: "이 기기가 며칠 가는지, 얼마나 뜨거워지는지에 대한 **숫자의 주인**이 되어라."

이 문장이 JD의 Responsibilities 중 3번째로 올라와 있다는 것도 정보다. 컨슈머 웨어러블에서 배터리 수명과 표면 온도는 **제품 스펙 시트에 찍히는 숫자**이고, 못 맞추면 출시가 밀린다. 즉 이 항목은 "있으면 좋은" 최적화가 아니라 **출시 게이트**다.

---

## 1. Hark에서 실제로 하게 될 일 (추정)

context 파일의 제품 구조 추정(Qualcomm SoC + Android, always-on 저전력 MCU, Cellular/Wi-Fi/BT/GNSS/NFC/UWB 멀티 라디오, 마이크·센서·햅틱)을 근거로 한다. 아래는 모두 `[추정]`이다.

### 1.1 하루 단위로 보면

```
 오전  야간 전력 회귀 리그 결과 확인 (어젯밤 빌드 12개 시나리오 자동 측정)
       -> "sleep floor 가 6.2 uA -> 9.1 uA 로 올랐다" 알림이 떠 있음
       -> git bisect 대신 빌드별 결과 표에서 후보 커밋 2개 특정
 오전  후보 커밋 리버트 후 보드에서 재측정, GPIO 상태 마커와 전류 파형 겹쳐 보기
       -> 센서 드라이버가 suspend 에서 I2C pull-up 을 계속 먹고 있었음 -> 드라이버 수정 PR
 오후  AI 팀 미팅: wake word 모델 v7 이 v6 보다 정확도 1.5%p 높지만 추론이 6 ms -> 10 ms
       -> 그 자리에서 mJ/inference 와 하루 mAh 로 환산해 "예산 안에 들어온다/안 들어온다" 답
 오후  기구/열 담당과 DVT 보드 열화상 세션: 표면 온도 vs 내부 NTC 상관식 갱신
       -> throttle trip point 와 hysteresis 조정, 정책 표 리뷰
 늦은  주간 power budget 표 갱신해서 팀에 공유 (항목별 실측치, 목표 대비 초과분, 소유자)
```

### 1.2 주/월 단위로 보면

- **보드 리비전마다 baseline 다시 재기**: EVT1 → EVT2 → DVT에서 PMIC 설정, 저항값, 리크 경로가 바뀐다. "저번 보드에서 5 µA였다"는 근거가 안 된다.
- **예산 재배분 회의**: 라디오 팀이 "셀룰러 PSM 주기를 줄여야 응답성이 산다"고 하고, AI 팀이 "모델을 더 자주 돌려야 한다"고 할 때 **같은 단위(mAh/day)로 환산해서 조정**하는 게 이 역할의 일이다.
- **전력 버그 트리아지**: "어떤 사용자 기기만 하루 만에 방전된다" → 필드 telemetry(상태별 residency, wake 원인 히스토그램)로 좁힌다.
- **출시 게이트 문서 작성**: "48시간 사용 시나리오에서 평균 X mA, 표면 온도 Y°C 이하" 같은 sign-off 문서. Don이 Apple에서 한 "reliability vs performance/power margin sign-off"와 같은 종류의 산출물이다.

### 1.3 이 역할이 소유하게 될 산출물

| 산출물 | 내용 | 왜 펌웨어가 소유하나 |
|---|---|---|
| Power budget 표 | 서브시스템별 목표·실측·소유자 | 소프트웨어 상태를 아는 사람만 "이 전류가 누구 것인지" 귀속시킬 수 있다 |
| 전력 회귀 리그 + CI 잡 | 야간 자동 측정, 임계 초과 시 알림 | 회귀는 코드에서 오고, 코드 변경을 막을 수 있는 건 CI뿐 |
| 전력 상태 계측 API | residency 카운터, wake 원인 로그, GPIO 마커 | 펌웨어 내부 상태라 외부에서 볼 수 없다 |
| Thermal 정책 표 | trip point, hysteresis, 완화 단계 순서 | 어떤 기능을 먼저 줄일지는 펌웨어가 실행하고, 제품 팀과 합의한다 |
| Sleep/wake 아키텍처 문서 | 무엇이 always-on이고 무엇이 깨우는가 | 드라이버·RTOS 설계와 분리 불가 |

---

## 2. 핵심 개념

### 2.1 전력 예산은 스프레드시트가 아니라 "계약"이다

주니어는 "전력 줄이기"를 코드 최적화로 이해하고, 시니어는 **배분 문제**로 이해한다. 시작점은 항상 제품 목표다.

```
 제품 목표: "완충에서 2일(48 h) 사용"
        │
        ▼
 배터리: 500 mAh, 공칭 3.85 V, 사용 가능 85% (cut-off·노화·저온 derating)
        │
        ▼
 허용 총 전하: 500 × 0.85 = 425 mAh  →  425 / 48 h = 8.85 mA 평균
        │
        ▼
 ┌───────────── 서브시스템 배분 (예시, 합계 8.85 mA) ─────────────┐
 │ SoC suspend (DRAM self-refresh, 모뎀 idle)       3.0 mA  34%  │ ← 플랫폼 팀
 │ SoC active (음성 세션)  250 mA × duty            5.0 mA  56%  │ ← 제품/AI 팀
 │ 라디오 유지 (BLE 연결 + 셀룰러 PSM paging)        0.5 mA   6%  │ ← 무선 팀
 │ always-on MCU 도메인 (센서+마이크+VAD+추론)       0.3 mA   3%  │ ← 나(이 역할)
 │ 누설·PMIC 자체 소모·fuel gauge                    0.05 mA  1%  │ ← HW 팀
 └───────────────────────────────────────────────────────────────┘
```

**여기서 나오는 협상 카드**가 중요하다. SoC active에 5.0 mA 평균을 배정했다는 말은:

```
 5.0 mA × 48 h = 240 mAh
 240 mAh / 250 mA = 0.96 h  = 약 58 분 (2일 기준)  →  하루 약 29 분
```

즉 **"하루에 음성 세션 총 29분"**이 제품 팀에게 줄 수 있는 문장이다. "전력을 줄여 주세요"가 아니라 "세션이 하루 40분이면 배터리는 1.4일이 된다, 둘 중 뭘 고르겠나"로 대화가 바뀐다. 면접에서 이 전환을 보여 주면 레벨이 올라간다.

C05 §7.3에 비슷한 예산표가 있지만, 그건 **bottom-up(항목을 더해서 수명을 구함)**이다. 실무에서 먼저 하는 건 **top-down(수명에서 항목 한도를 나눠줌)**이고, 둘을 맞춰 가는 게 프로젝트 내내 하는 일이다.

### 2.2 모든 전력은 세 개의 숫자로 요약된다

```
 I_avg  =  I_floor  +  Σ ( Q_event,k × f_event,k )

   I_floor      : 아무 일도 안 할 때 바닥 전류 (µA)
   Q_event      : 이벤트 1회가 쓰는 전하 (µC 또는 mAh)
   f_event      : 이벤트 발생률 (회/s, 회/h, 회/day)
```

이 식이 유용한 이유는 **개선 수단이 세 가지밖에 없다**는 걸 바로 보여 주기 때문이다: 바닥을 낮추거나, 1회 비용을 낮추거나, 횟수를 줄인다. 그리고 보통 **횟수 줄이기가 가장 크다.**

**워크드 예제 B — wake word 오탐(false accept) 1회의 가격**

```
 가정(가상): 오탐 1회 = SoC resume + 오디오 업로드 + 모델 응답 + suspend 까지 8 초,
             그 구간 평균 250 mA

 Q_event = 250 mA × 8 s = 2000 mA·s = 2000 / 3600 mAh = 0.556 mAh

 오탐률 1 회/시간  → 24 회/일 → 13.3 mAh/일
 오탐률 1 회/10분  → 144 회/일 → 80.0 mAh/일

 하루 예산(위 2.1에서 425 mAh / 2일 = 212 mAh/일) 대비
   1 회/시간  → 6.3%
   1 회/10분  → 37.7%   ← 배터리 수명이 2일에서 1.3일로 떨어진다
```

결론: **모델 정확도는 전력 지표다.** wake word의 false accept rate를 1시간에 1회 이하로 유지하는 것은 AI 팀의 정확도 문제가 아니라 펌웨어의 배터리 문제이기도 하다. 이게 JD의 "Collaborate with the on-device AI team"(J05)과 이 문장이 붙어 있는 이유다.

**워크드 예제 C — DMA 버퍼 크기 하나가 만드는 56 µA**

```
 가정(가상): PDM 마이크 16 kHz, DMA half-buffer 인터럽트마다 MCU 가 깨어
             250 µs 동안 평균 3 mA 를 쓴다 (wake 오버헤드 + 전처리 포함)

 half-buffer = 10 ms  → 100 회/s
   Q = 3 mA × 250 µs = 0.75 µC,  100 회/s → 75 µC/s = 75 µA 평균

 half-buffer = 40 ms  → 25 회/s
   Q 동일,  25 회/s → 18.75 µC/s ≈ 19 µA 평균

 차이 = 56 µA  ← MCU 도메인 예산(2.1에서 300 µA)의 19%
 대가: 오디오 경로 레이턴시가 최소 30 ms 늘고, 버퍼 SRAM 이 4배 필요
```

이런 계산을 회의 중에 30초 안에 할 수 있어야 한다. 단위 암기: `mA × ms = µC`, `µA 를 하루로 = µA × 24 / 1000 mAh`, `1 µA 1년 ≈ 8.8 mAh`(C05 §7.4).

### 2.3 전력을 "레일"이 아니라 "소프트웨어 상태"에 귀속시키기

전류계는 "지금 12 mA가 흐른다"까지만 알려 준다. 고쳐야 할 대상은 **어떤 코드 상태가 그걸 켰는가**다. 그래서 실무에서는 전류 파형에 소프트웨어 상태를 겹쳐 본다.

```
 전류 ▲
 (mA) │        ┌──┐                    ┌─────────┐
   12 │        │  │                    │         │
    6 │  ┌─┐   │  │    ┌─┐             │         │   ┌─┐
  0.01│──┘ └───┘  └────┘ └─────────────┘         └───┘ └──
      └────────────────────────────────────────────────────▶ t
 GPIO  ▁▁▔▁▁▁▁▔▔▔▁▁▁▁▁▔▁▁▁▁▁▁▁▁▁▁▁▁▁▁▔▔▔▔▔▔▔▔▔▔▁▁▁▁▔▁▁▁
 마커   IMU    INFER    IMU           BLE_TX+RADIO     IMU
       ─────────────────────────────────────────────────────
       "6 mA 짜리 짧은 펄스는 IMU FIFO 읽기, 12 mA 긴 구간은 BLE TX" 라고
       말할 수 있게 되는 순간 디버깅이 시작된다.
```

방법은 세 가지이고, 셋 다 쓴다.

| 방법 | 어떻게 | 얻는 것 | 한계 |
|---|---|---|---|
| GPIO 상태 마커 | 상태 진입/이탈 시 GPIO 토글, 전류 캡처와 동시 측정 | 파형과 코드의 시간 정렬 | 핀 개수 한정, 출하 보드에는 test point 필요 |
| Residency 카운터 | 각 전력 상태에 머문 누적 시간·진입 횟수를 RAM에 적산 | 장시간 통계, 필드 telemetry로 전송 가능 | 순간 파형은 못 봄 |
| Wake 원인 로그 | 깨어날 때 wake source(인터럽트 번호/이벤트)를 링버퍼에 기록 | "누가 깨웠나" 히스토그램 | 기록 자체가 전력을 씀(적게 설계해야) |

Don의 SSD 경험과의 연결: NVMe telemetry와 error reporting을 설계한 것과 **정확히 같은 구조**다. 기기 내부 상태를 적산해서 외부에서 진단 가능하게 만드는 일. 면접에서는 "저전력 경험이 없다"가 아니라 "관측 가능성을 펌웨어에 넣는 일은 해 봤고, 대상이 전력 상태로 바뀔 뿐"으로 프레이밍한다.

### 2.4 예산을 런타임에 지키는 구조: vote / lock

어떤 코드든 "지금은 자면 안 된다"고 말할 수 있어야 하고, **누가 말했는지 추적 가능해야** 한다. 이름 없는 락은 필드에서 못 잡는다.

| 플랫폼 | 메커니즘 | 실제 API |
|---|---|---|
| Zephyr | 전력 상태 락 | `pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_RAM, PM_ALL_SUBSTATES)` / `..._put(...)` |
| Zephyr | 디바이스 단위 참조 카운트 | `pm_device_runtime_get(dev)` / `pm_device_runtime_put(dev)` |
| FreeRTOS | tickless idle 진입 훅에서 거부 | `configPRE_SLEEP_PROCESSING`, `eTaskConfirmSleepModeStatus()` |
| Android (SoC 쪽) | wakelock | `PowerManager.WakeLock`, 커널 `wakeup_source` (`/sys/kernel/debug/wakeup_sources`) |

**함정**: Zephyr의 `pm_policy_state_lock_get()`은 "어떤 상태로는 들어가지 마라"를 **참조 카운트**로 관리한다. get/put 짝이 안 맞으면 기기는 영원히 깊은 잠을 못 잔다. 증상은 "sleep floor가 10 µA에서 900 µA로"다. 이런 락에는 반드시 **소유자 태그와 카운트를 덤프하는 디버그 명령**을 붙인다. Android 쪽도 같다. 필드 배터리 이슈의 상당수가 "누가 wakelock을 안 놨다"이고, `/sys/kernel/debug/wakeup_sources`를 읽는 게 첫 수순이다.

### 2.5 발열: "피크 전력"이 아니라 "지속 전력"이 표면 온도를 정한다

C05 §10.2에 물리가 있다. 여기서는 **업무 관점의 결론**만 다시 쓴다.

```
 정상상태 온도 상승  ΔT_ss = P_sustained × Rth
 시간 응답          ΔT(t) = ΔT_ss × (1 − e^(−t/τ)),  τ = Rth × Cth
```

**워크드 예제 D — 지속 전력 상한 구하기 (모든 수치 가상)**

```
 측정으로 얻은 값: Rth(기기 → 주변) = 25 °C/W,  τ = 6 분 (실측, 계산으로 구하지 말 것)
 조건: 주변 25 °C, 피부 접촉 표면 목표 43 °C  → 허용 ΔT = 18 °C

 (1) 지속 상한
     P_sustained ≤ 18 / 25 = 0.72 W
     3.85 V 기준 전류로는 약 187 mA. SoC active 250 mA 는 "지속" 으로 불가능하다.

 (2) 버스트는 얼마나 되나 — 3 분 동안 1.5 W
     ΔT(3분) = 1.5 × 25 × (1 − e^(−3/6)) = 37.5 × 0.393 = 14.7 °C
     → 표면 39.7 °C.  한계 43 °C 아래 = 허용

 (3) 같은 1.5 W 를 12 분 지속하면
     ΔT(12분) = 37.5 × (1 − e^(−2)) = 37.5 × 0.865 = 32.4 °C  → 57 °C. 불가.
```

이 계산에서 **throttling 정책의 형태**가 바로 나온다: 순간 온도가 아니라 **추세(적분값)**로 판단해야 하고, 짧은 고성능 버스트는 허용하되 누적 에너지를 제한해야 한다. 그래서 정책은 보통 "온도 trip point"와 "에너지 예산(thermal budget)" 두 가지를 섞는다.

### 2.6 Throttle ladder — 무엇을 먼저 줄일 것인가는 제품 결정이다

기술적으로는 아무거나 줄일 수 있다. 어느 순서로 줄일지는 **제품 팀과 합의해야 하는 정책**이고, 그 표를 펌웨어가 들고 있는다.

| 단계 | 트리거(예시, 가상) | 완화 조치 | 사용자 체감 |
|---|---|---|---|
| 0 Normal | 표면 추정 < 38 °C | 없음 | 없음 |
| 1 Light | ≥ 38 °C, 해제 36 °C | 충전 전류 제한, 디스플레이/LED 밝기 감소 | 충전이 느려짐 |
| 2 Moderate | ≥ 41 °C, 해제 39 °C | SoC 주파수 상한, 모뎀 TX 전력·데이터율 제한, 세션 오디오 품질 하향 | 응답이 약간 느려짐 |
| 3 Severe | ≥ 44 °C, 해제 41 °C | 장시간 세션 종료, 카메라/고전력 기능 비활성, 충전 중단 | 기능이 꺼짐(사용자 알림 필요) |
| 4 Critical | ≥ 48 °C | 안전 종료(배터리 보호 우선) | 기기가 꺼짐 |

정책 설계 원칙 다섯 가지:

- **Hysteresis는 필수**. 진입/해제 임계가 같으면 기능이 깜빡인다(초당 수 회 on/off). 최소 2~3 °C 벌린다.
- **센서 실패 시 fail-safe 방향을 정한다**. 온도를 못 읽을 때 "정책 유지"인지 "강제 throttle"인지 제품 결정이다. 충전 중이면 보수적으로 가는 게 보통이다.
- **조용히 성능만 깎으면 버그 리포트가 된다**. 사용자가 알아채는 단계(3 이상)는 UI로 알린다.
- **배터리 온도는 별도 경로**다. Li-ion 충전 허용 범위(대략 0~45 °C, 셀·벤더마다 다름, JEITA 가이드라인 기반)는 안전 문제라 throttle ladder와 별개로 항상 강제된다.
- **표면 온도는 직접 못 잰다**. 보드 NTC 여러 개의 가중합으로 추정하는 virtual sensor를 쓰고, 그 계수는 DVT에서 열화상 카메라와 상관시켜 정한다(C05 §10.3, C09 calibration).

### 2.7 "Always-on"이 실제로 뜻하는 것

always-on은 "전부 켜져 있다"가 아니라 **"계층을 나눠서 최소한만 켜 둔다"**는 설계다.

```
 계층 0 (항상, µA 단위)   RTC, PMIC, 전원 버튼, 리셋, 배터리 보호
 계층 1 (항상, 수십 µA)   MCU 저전력 코어 + RAM retention, IMU FIFO, PDM mic + VAD
 계층 2 (조건부, 수 mA)   MCU 풀 동작 + wake word 추론, BLE 연결 유지
 계층 3 (요청 시, 수십~수백 mA)  SoC resume, Wi-Fi/셀룰러 데이터, 대형 모델
 ───────────────────────────────────────────────────────────────────
 규칙: 아래 계층은 위 계층을 "깨울 수만" 있고, 위 계층이 켜져 있는 시간을
       최소화하는 것이 이 역할의 KPI 다.
```

면접에서 "always-on 기기의 전력 설계"를 물으면 이 계층 그림을 그리고 시작하면 된다. C05 §6(always-on sensor hub)이 이 구조의 하드웨어적 근거를 다룬다.

---

## 3. 실무 패턴과 함정

### 3.1 계측 — 측정할 수 없으면 최적화도 없다

**보드 설계 단계에서 요청해야 하는 것들** (HW 팀에게 EVT 전에 말해야 하고, 늦으면 다음 리비전까지 못 잰다):

| 요청 | 이유 |
|---|---|
| 주요 레일마다 0 Ω 점퍼 또는 sense 저항 자리 | 레일별로 잘라서 전류를 따로 잴 수 있다 |
| 배터리 경로에 전류 측정용 커넥터/2핀 헤더 | PPK2/Joulescope를 직렬로 넣을 수 있다 |
| 여유 GPIO 2~4개를 test point로 | 소프트웨어 상태 마커 |
| PMIC/fuel gauge의 전류 측정 레지스터 접근 경로 | 출하 기기에서도 대략적 전류를 읽을 수 있다(정밀도는 낮음) |
| 디버그 UART를 완전히 끌 수 있는 구조 | 로그 UART가 켜져 있으면 sleep floor가 수백 µA 뜬다 |

**도구 선택** (자세한 비교는 C05 §8):

- 바닥 전류(µA~nA)와 버스트(수백 mA)를 **동시에** 봐야 하므로 동적 범위가 넓은 전용 장비가 필요하다. Nordic PPK2, Joulescope, Keysight/Keithley급 DC power analyzer가 흔한 선택이다. 구체 사양(분해능, 최대 전류, 샘플레이트)은 **모델마다 다르니 제품 데이터시트를 확인**한다.
- 멀티미터는 평균만 보여 주고 버스트를 놓친다. "왜 계산보다 배터리가 빨리 닳지?"의 단골 원인이다.

### 3.2 소프트웨어 상태 마커 (C, 어느 RTOS에서도 동작)

```c
#include <stdint.h>
#include <stdbool.h>

/* 가상의 GPIO 레지스터. 실제 보드에서는 HAL/devicetree 로 대체한다. */
#define PWR_MARK_SET_REG   (*(volatile uint32_t *)0x50000508u)
#define PWR_MARK_CLR_REG   (*(volatile uint32_t *)0x5000050Cu)

/* 상태를 비트로 인코딩해서 2~4 핀으로 여러 상태를 구분한다. */
typedef enum {
    PWR_MARK_CPU_ACTIVE = 1u << 0,   /* GPIO n   */
    PWR_MARK_RADIO_TX   = 1u << 1,   /* GPIO n+1 */
    PWR_MARK_INFERENCE  = 1u << 2,   /* GPIO n+2 */
    PWR_MARK_SENSOR_IO  = 1u << 3,   /* GPIO n+3 */
} pwr_mark_t;

static inline void pwr_mark_enter(pwr_mark_t m) { PWR_MARK_SET_REG = (uint32_t)m; }
static inline void pwr_mark_exit(pwr_mark_t m)  { PWR_MARK_CLR_REG = (uint32_t)m; }

/* 사용 예 */
void wake_word_step(const int16_t *pcm, size_t n)
{
    pwr_mark_enter(PWR_MARK_INFERENCE);
    (void)run_inference(pcm, n);      /* 실제 추론 */
    pwr_mark_exit(PWR_MARK_INFERENCE);
}
```

- 마커 토글 자체는 수 사이클이라 전력에 거의 영향이 없다. 다만 **출하 빌드에서는 컴파일 타임에 제거**할 수 있게 매크로로 감싼다(핀이 플로팅이거나 다른 용도로 쓰일 수 있다).
- 로직 분석기와 전류 캡처를 **같은 트리거**로 묶으면 정렬이 쉽다. PPK2/Joulescope 계열은 디지털 입력을 함께 캡처하는 기능이 있는 모델이 있다(모델마다 다름).
- 대안: SEGGER SystemView나 Percepio Tracealyzer로 RTOS 이벤트를 타임스탬프와 함께 뽑고 전류 캡처와 수동 정렬. 단 트레이스 전송(RTT/SWO) 자체가 전력을 쓰므로 **저전력 측정 중에는 켜지 않는다**(이건 실제로 자주 틀리는 부분이다).

### 3.3 Residency 카운터 — 필드에서도 보이는 통계

```c
#include <stdint.h>

typedef enum { PS_DEEP, PS_IDLE, PS_ACTIVE, PS_RADIO, PS_COUNT } power_state_t;

typedef struct {
    uint64_t ticks_in_state[PS_COUNT];   /* 누적 체류 시간 (RTC tick) */
    uint32_t entries[PS_COUNT];          /* 진입 횟수 */
    uint32_t wake_by_source[32];         /* wake source(IRQ 번호 등)별 히스토그램 */
} power_stats_t;

static power_stats_t g_stats;
static power_state_t g_cur = PS_DEEP;
static uint64_t      g_last_tick;

extern uint64_t rtc_tick_now(void);      /* 항상 도는 저전력 카운터 */

void power_state_set(power_state_t next)
{
    uint64_t now = rtc_tick_now();
    g_stats.ticks_in_state[g_cur] += (now - g_last_tick);
    g_last_tick = now;
    g_cur = next;
    g_stats.entries[next]++;
}

void power_note_wake(uint32_t source_id)
{
    if (source_id < 32u) {
        g_stats.wake_by_source[source_id]++;
    }
}
```

- `rtc_tick_now()`는 sleep 중에도 도는 클럭이어야 한다. SysTick은 deep sleep에서 멈추므로 쓰면 안 된다(흔한 버그: 통계상 deep sleep 시간이 0으로 나온다).
- 이 구조체를 **OTA telemetry로 올리면**(J04와 연결) "필드에서 실제로 어떤 wake source가 지배적인가"를 알 수 있다. 랩에서는 절대 재현 안 되는 것들이 여기서 보인다.
- Linux/Android 쪽 대응물: `/sys/kernel/debug/wakeup_sources`, `cpuidle` residency(`/sys/devices/system/cpu/cpu0/cpuidle/state*/time`), `batterystats`/`suspend_stats`.

### 3.4 CI에서 전력을 회귀 테스트하기

이게 이 JD 문장에서 **"엔지니어처럼" 답할 수 있는 가장 강한 포인트**다. 대부분의 후보는 "저전력 팁"을 나열하고 끝난다.

```
 ┌──────────────────────── 야간 전력 회귀 리그 ────────────────────────┐
 │  CI 러너 (x86 PC)                                                   │
 │    │  1. 오늘 빌드 아티팩트 받기 (J04 파이프라인과 같은 빌드)        │
 │    │  2. USB 릴레이로 DUT 전원/부트 모드 제어                        │
 │    │  3. J-Link 로 flash → 리셋                                      │
 │    │  4. 테스트 하네스(UART/RPC)로 시나리오 진입 명령                │
 │    │  5. 전류 측정기(Joulescope/PPK2)로 N 초 캡처                    │
 │    │  6. 구간별 평균/적분 전하 계산 → baseline 과 비교 → 판정        │
 │    ▼                                                                │
 │  [USB 릴레이] ── 전원 ──▶ [DUT 보드] ◀── SWD ── [J-Link]            │
 │        전류계(직렬) ────────┘   GPIO 마커 ──▶ 전류계 디지털 입력     │
 │  RF 차폐 박스 안에 넣으면 무선 시나리오 반복성이 올라간다            │
 └─────────────────────────────────────────────────────────────────────┘
```

**시나리오 세트 예시** (각각 임계값을 따로 둔다):

| 시나리오 | 측정 | 임계(예시, 가상) |
|---|---|---|
| S1 sleep floor | 라디오 off, 센서 off, 60 s 평균 | ≤ 8 µA, 전일 대비 +15% 이상이면 실패 |
| S2 always-on idle | IMU + mic VAD 동작, 라디오 idle, 120 s 평균 | ≤ 320 µA |
| S3 BLE 연결 유지 | 연결 상태 300 s 평균 | ≤ 50 µA (연결 간격 고정) |
| S4 wake word 1회 | 트리거 주입 후 SoC suspend 복귀까지 적분 전하 | ≤ 0.60 mAh/회 |
| S5 음성 세션 60 s | 세션 동안 평균 전류 + 종료 후 복귀 시간 | ≤ 260 mA, 복귀 ≤ 3 s |
| S6 부팅 1회 | cold boot 적분 전하 | ≤ 1.2 mAh |

**판정 규칙에서 자주 틀리는 것들**:

- **절대 임계만 두면 서서히 나빠지는 걸 놓친다.** 매일 +2%씩 늘면 한 달 뒤 +80%다. "전일 대비 변화율" 알림을 같이 둔다.
- **노이즈 관리**. 같은 빌드를 3회 반복 측정해서 표준편차를 먼저 알아야 임계를 정할 수 있다. 반복성이 나쁜 원인: 온도, RF 환경, 무선 재연결 타이밍, flash 상태, 배터리 대신 전원공급기를 쓸 때의 전압 차이.
- **시나리오는 결정적(deterministic)이어야 한다.** "10초 기다렸다가 측정"은 부팅 시간이 바뀌면 깨진다. GPIO 마커나 UART 토큰으로 **구간을 표시하고 그 구간만 적분**한다.
- **빌드 설정이 다르면 비교 불가**. 로그 레벨, assert, 디버그 심볼, 최적화 옵션이 전력에 직접 영향을 준다. CI는 **출하와 동일한 설정**으로 측정한다.

측정 자동화 코드는 장비 SDK에 의존한다. Joulescope는 파이썬 드라이버(`pyjoulescope`)를 제공하고, Nordic PPK2는 공식적으로 nRF Connect for Desktop의 Power Profiler 앱이 GUI다(스크립팅은 커뮤니티 라이브러리를 쓰는 경우가 많다). **두 경우 모두 API가 버전마다 바뀌므로** 리그 코드는 얇은 어댑터 계층 뒤에 숨긴다.

```python
# 개념 예시 — 실제 API 이름/시그니처는 드라이버 버전마다 다르므로 확인 필요
class PowerMeter:
    def capture(self, seconds: float) -> "Trace": ...

def measure_scenario(meter, dut, name, seconds):
    dut.flash(build_artifact)
    dut.reset()
    dut.wait_for_token("READY")
    dut.send(f"scenario {name}")
    trace = meter.capture(seconds)
    return {
        "mean_uA":   trace.mean_current_uA(),
        "charge_uC": trace.integrate_charge_uC(),
        "p99_mA":    trace.percentile_current_mA(99),
    }
```

### 3.5 예산 협상 — AI 팀·무선 팀과 같은 단위로 말하기

이 JD는 "on-device AI 팀과 협업"(J05)을 따로 적어 놓았다. 전력 담당자로서 그 협업은 **번역 작업**이다.

| 상대가 말하는 것 | 내가 번역해서 돌려주는 것 |
|---|---|
| "모델 v7이 정확도가 1.5%p 높다. 추론이 6 ms → 10 ms." | "추론 duty가 10%면 MCU 평균이 100 µA → 167 µA. MCU 예산 300 µA 중 67 µA를 더 쓴다. 대신 오탐이 시간당 1회 → 0.4회로 줄면 SoC wake 비용이 13.3 → 5.3 mAh/일 줄어 **순이득**이다." |
| "모델을 항상 SRAM에 올려 두고 싶다." | "그러면 RAM retention 도메인이 커져 sleep floor가 6 µA → 11 µA. 하루 0.12 mAh니까 수용 가능. 다만 retention 영역이 늘면 부팅 시 초기화 전략을 바꿔야 한다." |
| "셀룰러 paging 주기를 짧게 하고 싶다."(무선 팀) | "PSM/eDRX 주기를 반으로 줄이면 라디오 유지 예산 0.5 mA → 0.9 mA. 총예산 8.85 mA에서 0.4 mA를 어디서 가져올지 정해야 한다." |
| "세션 길이를 늘리면 사용자가 좋아한다."(제품 팀) | "하루 29분이 현재 한도. 40분이면 배터리는 2일 → 1.4일. 스펙 시트에 뭘 쓸지 먼저 정하자." |

**핵심 습관**: 모든 요구를 `mAh/일` 또는 `평균 mA`로 환산해서 되돌려 준다. 그러면 논쟁이 취향에서 산수로 바뀌고, 결정이 빨라진다. 이게 "전력 예산의 주인"이라는 말의 실제 의미다.

### 3.6 Thermal 정책을 소유한다는 것

정책 코드 자체는 길지 않다(C05 §10.4에 Zephyr 예시 코드가 있다). 소유한다는 것은 **정책 표와 그 근거를 관리하고, 회귀를 막는다**는 뜻이다.

- **정책 표를 코드에 하드코딩하지 말고 데이터로 둔다.** 상수 테이블 + 버전 번호. 그래야 OTA로 조정할 수 있고(J04), 보드 리비전별로 바꿀 수 있다.
- **모든 완화 조치는 되돌릴 수 있어야 한다.** "충전 전류 제한"을 걸었다가 해제 로직이 없으면 배터리가 영원히 느리게 충전된다. 필드 버그 단골.
- **테스트는 온도 센서를 주입해서 한다.** 실제로 기기를 데워서 테스트하면 느리고 반복성이 없다. 가상 온도 소스(테스트 훅)로 ladder 전 단계를 자동 테스트하고, 실기 검증은 열화상 세션에서 몇 번만 한다.
- **로그를 남긴다.** throttle 진입/해제 시각과 그때의 온도·전력·활성 기능. 필드에서 "왜 느려졌나" 문의가 오면 이 로그가 유일한 증거다. **기구 팀과의 경계**: Rth를 줄이는 것(그라파이트 시트, 열확산판, 배치)은 기구 팀 일이다. 펌웨어는 **P_sustained를 제어**한다. 회의에서 이 경계를 명확히 말하면 신뢰를 얻는다.

### 3.7 함정 표

| 함정 | 증상 | 원인 | 대응 |
|---|---|---|---|
| 디버그 UART/RTT를 켠 채 측정 | sleep floor가 수백 µA | UART 클럭·핀 pull, RTT 폴링 | 출하 설정으로 측정, CI 빌드 설정 고정 |
| GPIO 플로팅 입력 | 보드마다 µA가 제각각, 온도에 따라 변함 | 미사용 핀이 중간 전압에서 관통 전류 | 미사용 핀을 출력 low 또는 pull 설정 |
| I2C/SPI 라인이 sleep 중 low | pull-up 저항으로 상시 전류 | 드라이버 suspend에서 핀 상태 미정리 | suspend 훅에서 핀을 Hi-Z/pull 정리 |
| `pm_policy_state_lock_get()` 누수 | deep sleep 진입 0회 | get/put 짝 불일치, 에러 경로 early return | 락 소유자 덤프 명령, RAII 유사 매크로 |
| SysTick으로 residency 측정 | deep sleep 시간이 0으로 집계 | SysTick이 sleep에서 멈춤 | RTC/항시 동작 타이머 사용 |
| 평균만 보고 판단 | 계산보다 배터리가 빨리 닳음 | 짧은 고전류 버스트를 멀티미터가 놓침 | 적분 전하(µC/mAh)로 판정 |
| 임계를 절대값으로만 관리 | 한 달 뒤 20% 악화 | 매일의 작은 증가 누적 | 전일 대비 변화율 알림 병행 |
| throttle 히스테리시스 없음 | 기능이 초당 수 회 깜빡임 | 진입/해제 임계 동일 | 2~3 °C 이상 벌리고 최소 유지 시간 부여 |
| 온도 센서 실패 처리 미정의 | 과열 또는 상시 throttle | fail-safe 방향 미합의 | 충전 중엔 보수적, 정책을 표에 명시 |
| 전력 회귀를 사람이 수동 측정 | 리비전마다 재발 | 자동화 없음 | 야간 CI 리그 |

---

## 4. 리서치 — 근거 자료

| 자료 | 무엇을 담고 있나 | 어디를 읽어야 하나 | URL |
|---|---|---|---|
| Zephyr Power Management | 시스템 PM 상태, 정책, 상태 락 API | "Power Management" 개요와 `pm_policy` 절 | https://docs.zephyrproject.org/latest/services/pm/index.html |
| Zephyr Device Runtime PM | 디바이스 단위 참조 카운트 PM | `pm_device_runtime_get/put` 사용 규칙 | https://docs.zephyrproject.org/latest/services/pm/device_runtime.html |
| FreeRTOS Low Power / Tickless | tickless idle, pre/post sleep 훅 | `configUSE_TICKLESS_IDLE`, `eTaskConfirmSleepModeStatus` | https://www.freertos.org/low-power-tickless-rtos.html (사이트 개편으로 경로가 바뀔 수 있음) |
| Arm v7-M Architecture Reference Manual | SCR(SLEEPDEEP/SLEEPONEXIT), WFI/WFE 정의 | System Control Block, Power management | https://developer.arm.com/documentation/ddi0403/latest/ |
| Nordic Power Profiler Kit II | 저전력 전류 측정기(소스미터 모드 포함) | 측정 범위·사용법 (사양은 제품 문서 기준) | https://www.nordicsemi.com/Products/Development-hardware/Power-Profiler-Kit-2 |
| Joulescope | 넓은 동적 범위 전류/전력 측정기 | 모델별 사양, 적분 전하 측정 | https://www.joulescope.com/ |
| pyjoulescope (드라이버) | 파이썬 자동화 API | 캡처/통계 API — **버전마다 다름** | https://github.com/jetperch/pyjoulescope |
| Linux Thermal Framework | thermal zone, trip point, cooling device 모델 | 전체 개요 | https://www.kernel.org/doc/html/latest/driver-api/thermal/index.html |
| Linux IPA (power allocator) | 전력 예산 기반 thermal governor | PID 기반 배분 방식 | https://www.kernel.org/doc/html/latest/driver-api/thermal/power_allocator.html |
| Android Power (AOSP) | wakelock, suspend, 배터리 통계 구조 | Power management 섹션 | https://source.android.com/docs/core/power |
| Android ADPF / Thermal API | 앱·게임이 thermal headroom을 읽는 방식 | `getThermalHeadroom()` 개념 | https://developer.android.com/games/optimize/adpf |
| EEMBC ULPMark | 저전력 MCU 벤치마크 방법론 | 측정 절차(반복성 설계 참고) | https://www.eembc.org/ulpmark/ |
| SEGGER SystemView | RTOS 이벤트 타임라인 트레이스 | 오버헤드와 전송 경로(RTT) | https://www.segger.com/products/development-tools/systemview/ |
| Percepio Tracealyzer | RTOS 트레이스 시각화 | 상태 체류 시간 분석 | https://percepio.com/tracealyzer/ |
| Memfault 문서 | 필드 기기 관측성(배터리·리셋·크래시 지표) | 배터리/디바이스 메트릭 설계 | https://docs.memfault.com/ |

**표준·규격 참고 (무료 URL 없음, 이름만 알아 둘 것)**:

- **IEC 62368-1** — 오디오/비디오·정보기기 안전 규격. 접촉 가능 표면의 온도 한계를 재질·접촉 시간별로 규정한다. 구체 수치는 유료 규격이므로 인용하지 말고 "규격과 제품 기준에 따라 정한다"고 말하는 게 안전하다.
- **JEITA 배터리 충전 가이드라인** — 온도 구간별 충전 전류/전압 제한. 셀 제조사 데이터시트가 최종 근거다. 벤더(Qualcomm, Ambiq 등)의 전력 상태·thermal 레지스터 문서는 NDA인 경우가 많으므로 면접에서는 "벤더 문서 기준"이라고만 말한다.

**버전 의존 주의**:

- Zephyr의 PM 통계/디버그 기능(상태 체류 통계 등)은 **릴리스마다 Kconfig 이름과 존재 여부가 다르다.** 특정 심볼 이름을 단정하지 말 것.
- FreeRTOS tickless 구현은 포트(`portSUPPRESS_TICKS_AND_SLEEP`)마다 다르고, 벤더 SDK가 자체 구현으로 덮어쓰는 경우가 많다.
- 측정 장비의 파이썬 API는 메이저 버전에서 자주 바뀐다. CI 리그는 어댑터 뒤에 숨긴다.

---

## 5. 예상 면접 질문

### Q01. Our device has to run two days on a 500 mAh battery. How do you turn that into engineering work?

**왜 묻나**: 전력을 "팁 모음"이 아니라 **예산 문제**로 다루는지 본다. 이 JD 문장에 가장 직접적으로 대응하는 질문.

**30초 답변**: 먼저 top-down 예산을 만든다. 500 mAh에 사용 가능 85%면 425 mAh, 48시간이면 평균 8.85 mA가 한도다. 그걸 SoC suspend, SoC active, 라디오, always-on MCU, 누설로 나누고 각 항목에 **소유자**를 붙인다. 그다음 보드에서 항목별로 실측해 bottom-up 표를 만들고, 목표와 실측 차이가 큰 순서로 공략한다. 마지막으로 그 측정을 야간 CI로 자동화해 회귀를 막는다.

**English answer**: I start by converting the product goal into a single number. Five hundred milliamp-hours at eighty-five percent usable is four hundred twenty-five milliamp-hours, and over forty-eight hours that is an average budget of about nine milliamps. Then I split that budget across subsystems — SoC suspend, SoC active sessions, radios, the always-on MCU domain, and leakage — and give every line an owner. Next I measure each line on real hardware and build the bottom-up table next to the top-down one. The gaps tell me where to work, in order of size. Finally I automate those measurements in a nightly power rig so the numbers cannot regress silently.

**꼬리질문**: "Which line usually dominates?" → 대부분의 웨어러블에서 SoC active 시간과 SoC suspend 전류다. MCU에서 µA를 깎는 건 보통 전체의 몇 %밖에 안 된다. / "How do you get the 85% number?" → cut-off 전압, 저온 특성, 노화, 보호 IC 자체 소모를 근거로 정하고 문서에 남긴다. / "What if the budget is impossible?" → 그건 엔지니어링 문제가 아니라 제품 결정이라 배터리 용량·세션 시간·기능 중 무엇을 바꿀지 데이터를 들고 올린다.

### Q02. How do you find out which piece of software is responsible for a given current?

**왜 묻나**: 계측 설계 능력. 전류계는 원인을 안 알려 준다는 걸 아는지.

**30초 답변**: 세 가지를 같이 쓴다. GPIO 상태 마커를 전류 캡처와 동시에 찍어 파형과 코드를 시간 정렬하고, 전력 상태별 residency 카운터로 장시간 통계를 내고, wake source 히스토그램으로 "누가 깨웠나"를 본다. 랩에서는 첫 번째, 필드에서는 두세 번째가 유일한 수단이다.

**English answer**: A current meter tells me how much, never who. So I add three things. First, GPIO state markers that toggle when firmware enters a state, captured on the same timebase as the current trace, so I can point at a pulse and name it. Second, residency counters per power state plus entry counts, accumulated from an always-on RTC, not SysTick. Third, a wake-source histogram so I know which interrupt is pulling the system out of sleep. In the lab the markers are fastest; in the field the counters are the only thing I have, so I ship them in telemetry.

**꼬리질문**: "Why not SysTick for residency?" → deep sleep에서 멈춰서 sleep 시간이 0으로 집계된다. / "Doesn't logging cost power?" → 그래서 적산만 하고 전송은 이미 깨어 있는 기회에 묶는다.

### Q03. The sleep current regressed from 6 µA to 9 µA overnight. Walk me through your debug.

**왜 묻나**: 회귀 디버깅 절차. 실제로 자주 일어나는 일.

**30초 답변**: 먼저 측정이 진짜인지 확인한다(같은 보드·같은 설정·3회 반복). 그다음 어제와 오늘 빌드 사이 커밋 목록을 보고 전력 리그에서 이진 탐색한다. 후보가 좁혀지면 GPIO 마커와 레일별 측정으로 어느 도메인인지 가른다. 흔한 원인은 드라이버 suspend에서 핀을 정리 안 한 것, PM 락 누수, 로그 설정 변경이다.

**English answer**: First I confirm the measurement is real: same board, same build configuration, three repeats, so I know the noise floor. Then I bisect across the commits between the two builds using the automated rig, which is much faster than reasoning about it. Once I have a candidate, I split by rail if the board has sense jumpers, and I check the usual suspects: a driver that left an I2C line low against a pull-up, a power-management lock that was taken and never released on an error path, or a change in logging configuration. Then I add a regression test for that specific failure so it cannot come back.

**꼬리질문**: "Three microamps — is that even worth chasing?" → 3 µA면 하루 0.072 mAh로 작지만, 추세를 방치하면 누적된다. 그리고 MCU 예산이 µA 단위인 기기에서는 비율로 50% 악화다. / "How do you bisect if the rig takes 10 minutes per build?" → 커밋 수가 적으면 그냥 전부 돌리고, 많으면 의심 영역(드라이버/PM) 커밋만 먼저 본다.

### Q04. How would you build power regression testing into CI?

**왜 묻나**: JD의 "optimize ... operation"에서 운영 관점을 보는지. 이걸 설계할 수 있는 후보는 드물다.

**30초 답변**: DUT 보드, 전원 릴레이, 디버그 프로브, 전류 측정기를 묶은 리그를 만들고 야간에 돌린다. 시나리오는 sleep floor, always-on idle, BLE 연결 유지, wake word 1회, 세션 60초, 부팅 1회처럼 결정적으로 구간을 표시할 수 있는 것들로 고정한다. 판정은 절대 임계와 전일 대비 변화율 둘 다 본다. 반복성을 먼저 측정해서 임계를 정하는 게 핵심이다.

**English answer**: I build a rig: a relay for power, a debug probe for flashing, a current meter in series with the battery input, and the device under test in a shielded box if radios are involved. Nightly, CI flashes the same artifact the release pipeline produces, drives the device into a fixed set of scenarios over a serial harness, and integrates charge over marked windows rather than fixed delays. I gate on two things, an absolute threshold and a day-over-day delta, because slow drift is what actually kills a battery target. Before setting any threshold I measure the same build three times to know the repeatability, otherwise the job just cries wolf and people start ignoring it.

**꼬리질문**: "What makes these tests flaky?" → 온도, RF 환경, 무선 재연결 타이밍, 빌드 설정 차이, 배터리 대신 전원공급기 사용. / "Why integrate charge instead of averaging?" → 구간 길이가 조금씩 달라도 비교 가능하고, 버스트를 놓치지 않는다.

### Q05. The AI team wants a bigger wake-word model. How do you decide?

**왜 묻나**: 부서 간 예산 협상. JD가 AI 팀 협업을 따로 적어 놓았다.

**30초 답변**: 정확도 변화와 추론 비용을 **같은 단위로 환산**한다. 추론이 길어지면 MCU 평균 전류가 얼마나 오르는지, 오탐률이 낮아지면 SoC wake 비용이 얼마나 줄어드는지 계산해서 순이득을 본다. 오탐 1회가 0.5 mAh 수준이면 정확도 개선이 추론 비용을 이기는 경우가 많다.

**English answer**: I translate both sides into milliamp-hours per day. A longer inference raises the MCU domain average — for example six to ten milliseconds at ten percent duty takes it from a hundred to about a hundred and sixty-seven microamps, which is a fraction of a milliamp-hour a day. A false accept, on the other hand, wakes the application SoC for several seconds at a couple hundred milliamps, so one false accept can cost half a milliamp-hour. If the bigger model cuts the false accept rate meaningfully, it usually pays for itself. I bring that arithmetic to the meeting rather than an opinion, and I also check the memory and latency side, because a model that no longer fits in SRAM changes the retention budget too.

**꼬리질문**: "What if it no longer fits in SRAM?" → 가중치를 flash에서 XIP하거나 스트리밍하면 flash 접근 전력과 레이턴시가 늘어난다. 그것도 mAh로 환산해서 비교. / "Who has final say?" → 예산 초과면 제품 팀 결정 사항으로 올린다. 나는 숫자와 선택지를 제공한다.

### Q06. What sets the surface temperature of a wearable — peak power or average power?

**왜 묻나**: 발열의 기본 물리를 업무 판단으로 연결할 수 있는지.

**30초 답변**: 지속 평균 전력이다. 정상상태 온도 상승은 `P × Rth`이고, 시간 상수 `τ = Rth × Cth` 안의 짧은 버스트는 열용량이 흡수한다. 그래서 throttle 정책은 순간 온도가 아니라 추세와 누적 에너지로 판단해야 한다.

**English answer**: Sustained power. Steady-state rise is power times thermal resistance, so with a device thermal resistance of twenty-five degrees per watt and an eighteen degree allowance, my sustained ceiling is about seven hundred twenty milliwatts. Short bursts are absorbed by the thermal mass: with a time constant of around six minutes, three minutes at one and a half watts only gets me about fifteen degrees of rise, which is fine, while twelve minutes at the same power gets me thirty-two degrees, which is not. That is exactly why a throttling policy has to look at a trend or an accumulated energy budget, not the instantaneous sensor reading.

**꼬리질문**: "Where do Rth and tau come from?" → 계산이 아니라 실측이다. 기구 팀과 열화상·열전대로 측정해서 얻는다. / "What if ambient is 35 °C?" → 허용 ΔT가 8 °C로 줄어 지속 상한이 0.32 W가 된다. 정책은 주변 온도를 반영해야 한다.

### Q07. Design the thermal throttling policy for this device. Who decides what gets cut first?

**왜 묻나**: 정책 소유권과 제품 감각. 기술 문제가 아니라 조율 문제라는 걸 아는지.

**30초 답변**: 단계별 ladder를 만든다. 낮은 단계에서는 사용자가 못 느끼는 것(충전 전류, 밝기)부터 줄이고, 높은 단계에서 기능을 끈다. 각 단계에 히스테리시스와 최소 유지 시간을 둔다. **무엇을 먼저 줄일지는 제품 결정**이라 제품 팀과 합의해서 표로 만들고, 펌웨어는 그 표를 실행하고 로그를 남긴다. 배터리 온도 보호는 ladder와 별개로 항상 강제한다.

**English answer**: I write it as a ladder with explicit trip points and hysteresis. The early steps are invisible to the user — reduce charge current, dim indicators. The middle steps cap SoC frequency and limit radio transmit power or data rate. Only the severe step ends a session or disables a feature, and that step has to tell the user, otherwise it shows up as a bug report. Every step is reversible and every transition is logged with the temperature and the active features, because in the field that log is my only evidence. I own the mechanism and the table format, but the ordering is a product decision, so I get it signed off rather than choosing it alone. Battery charge temperature limits sit outside the ladder and are always enforced, since that is a safety requirement.

**꼬리질문**: "How do you test the ladder?" → 온도 소스를 주입하는 테스트 훅으로 전 단계를 자동 테스트하고, 실기는 열화상 세션에서 몇 번만 확인. / "Where does the temperature come from?" → 직접 못 재므로 보드 NTC 여러 개의 가중합 virtual sensor, 계수는 DVT에서 캘리브레이션.

### Q08. What is the very first thing you measure on a brand-new EVT board?

**왜 묻나**: bring-up 감각. Don의 강점 영역으로 연결되는 질문.

**30초 답변**: 아무 코드도 안 도는 상태의 전류(리셋 홀드, 펌웨어 최소)부터 잰다. 그게 하드웨어 바닥이다. 그다음 부팅만 시키고 sleep에 넣은 값, 그다음 주변장치를 하나씩 켜면서 차분을 기록해 초기 예산표를 만든다. 첫 보드에서 이 baseline을 안 남기면 나중에 회귀를 판정할 기준이 없다.

**English answer**: The floor with nothing running — reset held or a minimal image that immediately sleeps. That number is the hardware's leakage plus the PMIC's own consumption, and no amount of firmware work goes below it. Then I boot and sleep and record that. Then I enable one peripheral at a time and log the delta, which gives me the first version of the budget table and, just as important, the baseline that every later regression is measured against. I also check that the unused pins are configured, because floating inputs are the classic reason two identical boards read differently.

**꼬리질문**: "What if two boards differ by 30%?" → 플로팅 핀, 조립 편차, 부품 벤더 차이, 리크가 있는 부품. 여러 대를 재서 분포를 본다. / "How many boards?" → 최소 3대 이상, 가능하면 리비전별로.

### Q09. How do you keep an always-on microphone path cheap?

**왜 묻나**: Hark 기기의 핵심 경로. I2S/PDM과 전력이 만나는 지점.

**30초 답변**: 계층으로 나눈다. 가장 아래는 하드웨어 또는 초저전력 VAD가 소리 유무만 보고, 그 위에서 MCU가 wake word를 돌리고, 그 위에서 SoC가 깨어난다. MCU 쪽에서는 DMA 버퍼를 크게 잡아 wake 횟수를 줄이고(예제 C처럼 버퍼 10 ms → 40 ms가 56 µA를 아낀다), 샘플레이트·비트폭을 필요한 최소로 낮춘다.

**English answer**: I treat it as a cascade. The cheapest stage runs all the time — hardware or a very small voice activity detector that only asks "is there sound". The next stage is the wake word model on the MCU, and only that stage is allowed to wake the application SoC. On the MCU side the biggest lever is usually how often the CPU wakes for audio: going from a ten millisecond DMA half-buffer to forty milliseconds cuts wake-ups from a hundred a second to twenty-five, which in one device I would size at tens of microamps, at the cost of extra latency and SRAM. I also keep sample rate and bit depth at the minimum the detector needs, and let the DMA run without CPU involvement between interrupts.

**꼬리질문**: "What is the latency cost?" → 버퍼 크기만큼 최소 지연이 늘고, wake word 응답 체감에 영향을 준다. 제품이 허용하는 범위에서 최대로 키운다. / "Where does the pre-roll audio come from?" → 링버퍼에 항상 최근 오디오를 유지해 SoC가 깨면 그 앞부분부터 넘긴다.

### Q10. Firmware sees battery draining fast only on some users' devices. How do you approach it?

**왜 묻나**: 필드 운영 관점. JD의 "operation"과 J04(telemetry)로 이어진다.

**30초 답변**: 랩에서 재현 안 되는 문제라 telemetry에 의존한다. 전력 상태별 residency, wake source 히스토그램, 라디오 재연결 횟수, throttle 진입 횟수를 보고 정상 기기 분포와 비교한다. 대개 특정 wake source가 비정상적으로 많거나, 무선 환경이 나빠 재연결을 반복하거나, 특정 하드웨어 리비전 문제다.

**English answer**: I do not try to reproduce it first; I look at the distribution. If the fleet reports per-state residency, wake-source counts, radio reconnect counts and throttle entries, I can compare the affected devices against the healthy population and usually one counter stands out. The common causes are a wake source firing far more than designed — a noisy sensor interrupt, for example — a radio that keeps reconnecting in a bad RF environment, or a specific hardware revision or component vendor. Once I have a hypothesis I can ship a targeted diagnostic or a mitigation through OTA to a small cohort and watch whether the metric moves.

**꼬리질문**: "What if telemetry doesn't have the counter you need?" → 그게 OTA 인프라가 필요한 이유다. 진단 빌드를 소수 코호트에 배포한다. / "Privacy?" → 집계 카운터만 올리고 오디오·개인 데이터는 올리지 않는다.

### Q11. Race to idle or run slower — which is better?

**왜 묻나**: 고전 질문이지만 답이 "상황에 따라"인 걸 근거와 함께 말하는지 본다.

**30초 답변**: 대부분의 MCU에서는 race-to-idle이 낫다. 누설과 주변장치 상시 소모가 있어서 깨어 있는 시간이 길면 그만큼 손해고, 깊은 sleep의 전류가 active보다 훨씬 작기 때문이다. 다만 주파수를 올리려고 전압을 올려야 하면(동적 전력이 V²에 비례) 역전될 수 있고, 발열 관점에서는 버스트가 유리하다.

**English answer**: On a typical microcontroller, race to idle wins, because leakage and always-on peripherals keep costing you while you are awake, and the deep sleep state is orders of magnitude below active. The exception is when reaching the higher frequency requires a higher core voltage, since dynamic power scales with voltage squared, so you can end up paying more energy per unit of work. I would measure energy per operation at each operating point rather than argue about it. Thermally, short bursts are also better, because surface temperature follows sustained power, not peak.

**꼬리질문**: "How would you measure energy per operation?" → 작업 구간을 GPIO로 표시하고 그 구간의 적분 전하를 재서 작업 수로 나눈다. / "Does this apply to the application SoC too?" → SoC는 suspend/resume 비용이 커서 오히려 "덜 깨우기"가 더 중요하다.

### Q12. What does the always-on MCU do that the SoC cannot, from a power standpoint?

**왜 묻나**: Hark 기기 구조 추정과 직결. 시스템 분담 감각.

**30초 답변**: SoC는 suspend 전류가 mA 단위이고 resume 비용이 크다. MCU는 µA 단위로 상시 동작하면서 센서 FIFO 수집, VAD, wake word, 타이머, 버튼·햅틱 같은 저지연 반응을 담당하고, **SoC를 깨울지 말지를 결정**한다. 즉 MCU의 진짜 역할은 연산이 아니라 게이트키퍼다.

**English answer**: The application SoC idles in the milliamp range and costs real energy every time it resumes, so the design goal is to keep it asleep. The microcontroller sits at microamps and stays up permanently: it batches sensor data through FIFOs, runs voice activity detection and the wake word, handles button and haptic latency, and keeps timekeeping. Its most valuable function is not computation, it is being the gatekeeper that decides whether an event is worth waking the SoC for. Every false wake it prevents is worth far more than the microamps it spends deciding.

**꼬리질문**: "How do they communicate?" → 저전력 인터럽트 라인 + UART/SPI IPC. 깨울 때는 GPIO 인터럽트, 데이터는 배치로. / "Who owns time?" → MCU의 RTC가 기준. SoC가 자는 동안에도 흐른다.

### Q13. Your power numbers look fine in the lab but users complain. What is likely different?

**왜 묻나**: 랩과 필드의 간극을 아는지. 시니어 신호.

**30초 답변**: 랩 시나리오가 실제 사용을 대표하지 않는 경우가 대부분이다. 실제로는 무선 환경이 나쁘고(재전송·재연결), 온도가 높고, 알림이 자주 오고, 오탐이 잦고, 여러 앱이 동시에 깨운다. 그래서 랩 시나리오를 필드 telemetry 분포로 보정하고, 최악 조건 시나리오를 따로 둔다.

**English answer**: The lab scenario is usually too clean. Real users have weak signal, so the radio retransmits and reconnects, which is far more expensive than a clean link. Ambient temperature is higher, so throttling engages and sessions take longer. Notifications and false wakes arrive at rates my scripted scenario never produces. My fix is to calibrate the lab scenarios against field telemetry distributions — use the ninetieth percentile wake rate rather than the design value — and to keep a deliberately hostile scenario in the rig, for example a marginal RF condition, so that the number I report is defensible.

**꼬리질문**: "Give an example of a hostile scenario." → 차폐 박스에서 감쇠를 넣어 신호를 약하게 한 상태의 연결 유지. / "How do you report battery life externally?" → 시나리오와 가정을 명시한 조건부 수치로.

### Q14. How do you stop a power optimization from breaking functionality?

**왜 묻나**: 전력 작업은 위험한 변경이다. 리스크 관리 감각.

**30초 답변**: 전력 최적화는 대부분 "덜 깨우기"라 레이턴시와 놓친 이벤트로 나타난다. 그래서 전력 리그에 **기능 판정**을 같이 넣는다. 예를 들어 wake word 1회 시나리오에서 전하뿐 아니라 응답 시간과 성공 여부를 같이 본다. 그리고 단계적 롤아웃으로 필드 지표를 확인한다.

**English answer**: Power work is almost always "wake up less often", and the failure mode is a missed or late event, not a crash, so it slips through normal tests. I make every power scenario also assert functional outcomes — the wake word scenario checks response latency and detection success, the BLE scenario checks that no connection was dropped during the window. Beyond that, I ship power changes through the same staged rollout as any other change and watch the field metrics, because some interactions only appear with real radio conditions and real user behaviour.

**꼬리질문**: "Which optimizations are riskiest?" → 인터럽트를 마스킹하거나 클럭을 끄는 것, 그리고 DMA 버퍼를 키워 레이턴시를 늘리는 것. / "How would you roll back?" → 정책 파라미터를 데이터로 두면 OTA로 값만 되돌릴 수 있다.

### Q15. How would you present a power problem to leadership?

**왜 묻나**: 시니어에게 묻는 커뮤니케이션 질문. Don의 sign-off 경험과 연결된다.

**30초 답변**: 목표 대비 현재 숫자, 가장 큰 기여 항목 3개, 각각의 가능한 개선폭과 비용(레이턴시·기능·일정), 그리고 결정이 필요한 trade-off를 한 장으로 만든다. "열심히 하고 있다"가 아니라 "이 중에 뭘 고를지 정해 달라"로 끝낸다.

**English answer**: One page. Current number against the target, the top three contributors with their share, and for each one the realistic saving and what it costs — latency, a feature, or schedule. Then the decision I need from them, stated as options rather than a complaint: for example, keep two-day battery and cap sessions at thirty minutes a day, or accept one and a half days and leave sessions open. I also state the confidence and how it was measured, because a power number without its scenario and its measurement method is not a number.

**꼬리질문**: "What if the answer is 'do both'?" → 그때는 배터리 용량이나 기구 설계 같은 하드웨어 변경이 필요하고, 그건 일정 문제로 올린다. / "How often do you report?" → 주간. 추세가 핵심이라 매주 같은 형식으로.

---

## 6. Don 매핑

### 6.1 쓸 수 있는 근거 (context 3.1절 레쥬메 기반만)

| 레쥬메 근거 | 이 JD 문장과의 연결 | 어떻게 말할지 |
|---|---|---|
| "JTAG, Oscilloscope, Logic Analyzer, **Power Analyzer**" | 전력 측정 장비 실사용 경험이 명시되어 있다 | "전류 파형을 보고 판단하는 일은 익숙하다. 대상이 SSD 레일에서 배터리 기기로 바뀔 뿐" |
| "sign off on hardware safety margins (**reliability vs performance/power**)" | 전력을 다른 지표와 **trade-off해서 결정한 경험** = 예산 협상의 핵심 | "성능·신뢰성·전력 사이에서 마진 기준을 정하고 문서로 sign-off한 경험이 있다. Hark에서는 그 축이 배터리 수명과 표면 온도가 된다" |
| "NVMe **telemetry** 디버그 기능", "error reporting/handling 설계" | residency 카운터·wake source 히스토그램 = 같은 종류의 관측성 설계 | "필드에서 보이지 않는 것을 펌웨어가 적산해서 내보내게 만드는 일을 양산 제품에서 해 봤다" |
| "designing and leading **factory test-node architecture**" | 전력 회귀 리그 = 자동 측정 리그. 구조가 같다 | "DUT·계측기·자동화를 묶은 테스트 노드를 설계해 본 경험이 야간 전력 회귀 리그로 그대로 이어진다" |
| "Silicon/system bring up → NPI → MP", "shmoo·health monitoring (SI 협업)" | 보드 리비전마다 baseline 다시 재는 일, HW 팀과의 협업 | "새 보드가 나올 때마다 기준을 다시 세우는 일은 bring-up에서 늘 하던 일" |
| "ARM Cortex R8/R82/M0+ bring-up", bare-metal C/C++ | sleep/클럭 제어는 레지스터 레벨 작업 | "저전력 API를 안 써 봤을 뿐, SCB/클럭 제어 레지스터를 직접 다루는 건 해 오던 일" |

### 6.2 강점 스토리 (STAR 골격)

**스토리 A — "마진 sign-off"를 전력 예산 이야기로 변환**

- Situation: 새 실리콘/플랫폼에서 신뢰성과 성능·전력 마진을 동시에 만족시켜야 했다.
- Task: 어디까지를 허용 마진으로 볼지 기준을 정하고 sign-off해야 했다.
- Action: 측정(Power Analyzer, shmoo 등)으로 분포를 얻고, 조건별로 기준을 정해 문서화했다.
- Result: 양산 판정 기준이 되었다.
- **연결 문장**: "Hark에서 같은 방식으로 배터리 수명과 표면 온도의 출시 게이트를 정의하겠다."
- <확인 필요: 이 sign-off에서 전력 항목이 구체적으로 어떤 지표였는지(전류·전압 마진·온도?), 그리고 Don이 기준값을 제안한 쪽이었는지 검토한 쪽이었는지. 면접에서 구체 수치를 말하려면 필요하다. NDA 범위도 확인.>

**스토리 B — factory test-node → 전력 회귀 리그**

- 자동 측정 리그를 설계·리드한 경험은 이 JD 문장에서 가장 차별화되는 답이다. "전력 최적화"를 물었는데 "CI 회귀 리그"로 답하는 후보는 드물다.
- <확인 필요: test-node에서 전류/전력 측정을 항목으로 포함했는지. 포함했다면 이 스토리가 훨씬 강해진다.>

### 6.3 갭과 프레이밍

| 갭 | 솔직한 현실 | 프레이밍 |
|---|---|---|
| 배터리 기기의 µA 예산 설계 | 엔터프라이즈 SSD는 전원이 무제한에 가깝고 관심사가 다르다 | "배터리 예산을 소유해 본 적은 없다. 다만 제한된 자원을 예산으로 나누고 회귀를 막는 일은 성능·신뢰성 축에서 해 왔다" + 사이드 프로젝트(nRF52840 DK + PPK2로 sleep floor 측정) 언급 |
| RTOS tickless / PM 프레임워크 | FreeRTOS/Zephyr PM API 실무 없음 | "bare-metal에서 WFI와 클럭 게이팅은 다뤘다. Zephyr의 `pm_policy_state_lock_get()`이나 device runtime PM은 그 위의 추상화로 이해하고 있다" (실제로 C05를 읽고 말할 것) |
| Thermal 정책 소유 | 웨어러블 skin temp 경험 없음 | <확인 필요: 엔터프라이즈 SSD의 thermal throttling(NVMe는 composite temperature와 thermal management temperature 기반 throttling을 규정한다)을 다뤄 봤는지. 다뤘다면 "throttle ladder와 히스테리시스는 SSD에서 같은 구조를 봤다"고 말할 수 있어 매우 강하다.> |
| 무선 전력(DTIM, PSM, connection interval) | 칩셋 통합은 했지만 프로토콜 전력 파라미터는 아님 | "라디오를 호스트에 붙이는 쪽을 했고, 전력 파라미터는 지금 공부 중"이라고 솔직하게. C06/S03으로 보완 |

### 6.4 절대 하지 말 것

배터리 수명 프로젝트를 해 봤다고 말하지 않는다(레쥬메에 근거가 없다). 이 노트의 µA·mAh 수치를 "내가 달성한 값"으로 말하지 않는다(전부 설명용 가상 값이다). Apple/Solidigm의 비공개 수치 대신 방법론만 말한다.

---

## 7. 준비 체크리스트

- [ ] 2.1의 top-down 예산 계산(500 mAh → 8.85 mA → 항목 배분 → "하루 29분")을 종이 없이 재현한다.
- [ ] 2.2의 세 숫자 공식 `I_avg = I_floor + Σ(Q × f)`로 예제 B(오탐 비용)와 예제 C(DMA 버퍼)를 60초 안에 계산한다.
- [ ] 2.5의 열 계산 3단계(지속 상한, 3분 버스트, 12분 지속)를 손으로 풀어 본다.
- [ ] nRF52840 DK + PPK2(또는 Joulescope)로 sleep floor를 실제로 측정하고, GPIO 마커를 넣어 파형과 코드를 정렬해 본다 — 면접에서 "직접 재 봤다"고 말할 수 있는 유일한 방법.
- [ ] Zephyr `pm_policy_state_lock_get/put`과 `pm_device_runtime_get/put`를 샘플에서 실제로 호출해 보고, 락을 일부러 누수시켜 sleep이 사라지는 걸 관찰한다.
- [ ] 3.4의 CI 리그 구성도를 화이트보드에 그리고 시나리오 6개와 판정 규칙을 설명하는 연습(2분).
- [ ] 2.6 throttle ladder 표를 자기 버전으로 다시 작성한다(트리거·조치·사용자 체감·히스테리시스).
- [ ] 6.3의 <확인 필요> 항목 3개를 실제 경력과 대조해 정리한다(전력 sign-off 지표, test-node의 전력 측정 포함 여부, SSD thermal throttling 경험).
- [ ] Q01·Q04·Q07의 English answer를 소리 내어 읽고 각 60초 안에 말한다.
- [ ] 역질문 준비: "always-on MCU와 SoC의 전력 분담은 이미 정해졌나요? 지금 배터리 목표와 실측 격차는 얼마인가요?"

---

## 8. 더 읽기

- **C05 §1~§2** 전력의 물리(CV²f, leakage)와 Cortex-M sleep 메커니즘(WFI/WFE, SLEEPDEEP, SLEEPONEXIT) — 이 노트가 생략한 근거이자 면접 단골.
- **C05 §4** tickless idle과 Zephyr/FreeRTOS PM 프레임워크 코드, **§6** always-on sensor hub 구조(2.7 계층 그림의 하드웨어 근거).
- **C05 §7.2~7.4** 배터리 수명 계산 예제와 단위 변환 요령, **§8** 측정 도구 비교, **§9** µA 누설 사냥 체크리스트.
- **C05 §10** 발열 관리 전체(시간 상수, thermal zone, virtual sensor, 정책 코드), **§11** PMIC·충전 곡선·fuel gauge.
- **S03 Q01~Q08** 저전력 문항, **Q09~Q10** 배터리·발열, **E1~E4** 계산 연습(배터리 수명, BLE 평균 전류, FIFO 배칭, DTIM).
- **C06** BLE connection interval·Wi-Fi DTIM/TWT 등 라디오 전력 파라미터, **C08** on-device ML 추론 예산(2.2의 오탐 비용과 연결).
- **J04** OTA — 전력·thermal 정책을 데이터로 두고 필드에서 조정하는 경로. **J05** on-device AI 예산 협상 — 3.5의 번역 표를 더 깊게.
