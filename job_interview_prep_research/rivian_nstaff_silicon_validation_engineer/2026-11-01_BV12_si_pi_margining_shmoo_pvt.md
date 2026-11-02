# BV12 · SI/PI 기초 · margining · shmoo · PVT 특성화
<!-- theme: pastel -->

> **묶음**: E 측정·마진 · **우선**: P1 (단, Don 강점 카드 — SK hynix chamber·shmoo) · **선행 노트**: [BV11 랩 장비](2026-11-01_BV11_lab_instruments_measurement.html), [BV03 전원](2026-11-01_BV03_power_tree_sequencing_pmic.html), [BV09 DDR](2026-11-01_BV09_ddr_bringup_training_memtest.html) · **다음 노트**: [BV13 디버그 시나리오](2026-11-01_BV13_board_debug_scenarios.html) · **목록**: [BV 트랙 목록](2026-11-01_BV00_board_validation_track.html), [LP17 Gen5/Gen6 · Lane Margining](2026-10-31_LP17_gen5_gen6_retimer_automotive.html), [LP14 검증 방법론](2026-10-31_LP14_validation_methodology.html), [성능 메트릭 (주파수 스윕)](2026-10-30_perf_metrics_ssd_vs_soc.html), [BV15 자동차 보드](2026-11-01_BV15_automotive_board_considerations.html) · **코드**: [onsitePrep/bv12/shmoo_model.py](onsitePrep/bv12/shmoo_model.py) (모델 shmoo + 마진 + PVT 표 + Cpk, 실행 검증)

## 0. TL;DR

- 이 노트로 답할 수 있는 것: "**What's a shmoo plot and how do you read it?**", "**How would you characterize margin across PVT?**", "**What closes an eye?**", "**What are process corners and how do you get them post-silicon?**", "**What do you hand to design/product at the end?**"
- **외울 것 ① — "된다"가 아니라 "얼마나 여유 있게 된다"**: 검증의 결과물은 PASS 한 줄이 아니라 **margin 숫자**(Vmin, Fmax, eye width/height, BER)와 **어느 조건(P·V·T)에서 가장 나빴는지**.
- **외울 것 ② — shmoo 읽기**: 2축(예: Vcore × 주파수) 격자를 PASS/FAIL로 칠한 지도. 정상 모양은 **"전압 ↑ → 더 높은 주파수 통과"** 하는 대각선 경계. **벽(wall)** = 특정 전압에서 주파수와 무관하게 실패(SRAM Vmin 등), **구멍(hole)** = 통과 영역 한가운데 실패 → 먼저 **재현되는지**부터 확인(반복 실패 = 진짜 조건부 버그, 가끔 실패 = 노이즈/간헐 버그/장비 문제).
- **외울 것 ③ — PVT**: Process(FF/TT/SS 칩, **skew/corner lot**으로 일부러 만듦) × Voltage(nominal ± 5–10 %, PMIC margining) × Temperature(자동차 **−40 ~ 125 °C** 이상, 접합 온도 Tj 기준). 최악 코너는 항상 "SS + 고온"이 아니다 — 저전압에서는 **temperature inversion**으로 **저온**이 더 느릴 수 있다.
- **SI/PI 한 줄**: SI = 신호가 선을 지나며 **반사·손실·crosstalk·jitter**로 찌그러져 **eye가 닫히는** 문제, PI = 부하가 갑자기 늘 때 전원이 **droop**해서 칩이 느려지는(=타이밍 마진을 먹는) 문제. 둘 다 결국 **margin을 깎는다**.
- **BER 숫자**: 1e-12를 95 % 신뢰도로 주장하려면 **에러 0으로 약 3×10¹² 비트** (3/BER 규칙). 10 Gb/s면 5분, 1 Gb/s면 50분 [9].
- **30초 영어 (shmoo)**: "A shmoo is a two-dimensional pass/fail map — typically core voltage against frequency — where each point runs the same test several times. A healthy part shows a diagonal boundary: more voltage buys more frequency. I read the margin from the nominal point to that boundary in both directions, and then look for anything that isn't monotonic — a wall at a fixed voltage usually points to an SRAM Vmin or a hold issue, and a hole inside the passing region is either a real conditional bug or noise, so the first thing I do is rerun those points many times. Then I repeat it across process corners and temperature in a chamber, because the worst case moves."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — "다리가 서 있다" vs "트럭 몇 대까지 버티나"

새 다리를 지었다. 사람이 걸어서 건너 보니 **무너지지 않는다** → 이게 기능 검증(functional validation)의 "PASS".
그런데 다리 회사가 고객에게 말해야 하는 건 "트럭 **40톤까지는 여유 있게** 버팁니다, **영하 40도 겨울**에도, **강풍**이 불어도"다. 그러려면 일부러 **무거운 트럭을 올리고(전압 ↓·속도 ↑)**, **추운 날·더운 날**(온도) 다시 재고, **다리마다 콘크리트 품질 차이**(공정 편차)를 고려해야 한다.

- **margin(마진, 여유)** = "정상 사용 조건"과 "실제로 무너지는 조건" 사이의 거리.
- **margining** = 일부러 조건을 나쁘게 만들어 그 거리를 재는 행위.
- **shmoo plot** = 두 가지 조건(예: 트럭 무게 × 바람 세기)을 격자로 바꿔 가며 "버팀/무너짐"을 칠한 지도.
- **PVT** = 다리마다 다른 품질(Process) × 하중(Voltage에 해당) × 날씨(Temperature).

칩에서는 **"무너짐" = 계산이 틀리거나, 데이터가 깨지거나, 멈춤**. 원인은 대부분 **"신호가 제때 도착하지 못함"(타이밍 위반)** 이다. 전압이 낮으면 트랜지스터가 느려지고, 주파수가 높으면 허용 시간이 짧아지고, 온도가 바뀌면 속도가 바뀐다.

### 1.2 SI 비유 — 긴 수도관으로 "물결 신호" 보내기

고속 신호(PCIe·DDR·Ethernet·MIPI)는 "0과 1"을 **전압 계단**으로 보낸다. 선(PCB 배선, 커넥터, 케이블)이 길고 빠를수록 이 계단은 **물결처럼** 행동한다.

| 현상 | 수도관 비유 | 칩/보드에서 |
|---|---|---|
| **반사 (reflection)** | 관 굵기가 갑자기 바뀌는 곳에서 물결이 **되튐** | 임피던스가 바뀌는 곳(via, 커넥터, 패드, 끝이 열린 stub)에서 신호 일부가 되돌아와 다음 비트와 겹침 |
| **손실 (insertion loss)** | 관이 길수록 물결이 **약해지고 무뎌짐** | 높은 주파수 성분이 더 많이 깎여 계단이 둥글어짐 → 이웃 비트끼리 번짐(ISI) |
| **crosstalk** | 옆 관의 진동이 **벽을 타고** 넘어옴 | 옆 배선의 신호가 전자기적으로 섞여 들어옴 |
| **jitter** | 물결이 오는 **시각이 매번 조금씩** 다름 | 비트 경계(엣지)가 시간 축에서 흔들림 |
| **노이즈** | 관 전체가 흔들림 | 전원 노이즈·기준 전압 흔들림이 진폭을 흔듦 |

### 1.3 eye diagram 비유 — 수천 장의 사진을 겹쳐 그리기

수신기는 매 비트마다 **"지금 전압이 높으냐 낮으냐"** 를 한 순간에 판정(샘플링)한다. 비트 하나 길이(UI, unit interval)씩 잘라서 수천~수백만 비트를 **한 화면에 겹쳐** 그리면 가운데가 빈 **눈(eye)** 모양이 나온다.

```text
 전압
  ▲   ────────╲        ╱──────────╲        ╱────── 1 레벨
  │            ╲      ╱            ╲      ╱
  │             ╲    ╱   ┌──────┐   ╲    ╱
  │              ╲  ╱    │ eye  │ ← eye height (세로로 열린 정도: 노이즈·손실이 깎음)
  │               ╳      │ 열림 │     ╳
  │              ╱  ╲    │      │    ╱  ╲
  │             ╱    ╲   └──────┘   ╱    ╲
  │            ╱      ╲  ◀──────▶  ╱      ╲
  │   ────────╱        ╲ eye width╱        ╲────── 0 레벨
  │                     (가로로 열린 정도: jitter가 깎음)
  └──────────────────────────────────────────────▶ 시간 (1 UI = 비트 하나)
              엣지가 흔들리는 폭 = jitter      ★ 샘플링 점은 눈 한가운데가 이상적
```

- 눈이 **크게 열려 있으면** = 샘플링 시점·판정 전압이 조금 어긋나도 맞게 읽음 → **margin 큼**.
- 눈이 **닫히면** = 조금만 어긋나도 비트 에러 → BER ↑.
- **"What closes the eye?"** 의 답 = 손실·반사·crosstalk(→ 세로·가로 모두), jitter(→ 가로), 전원 노이즈(→ 세로·가로).

### 1.4 PI 비유 — 아파트 수압

아파트(칩) 전체가 같은 수도관(전원 rail)을 쓴다. 저녁 7시에 모두 동시에 샤워를 틀면(**부하 계단, load step** — 예: NPU가 갑자기 연산 시작) 수압이 **순간적으로 떨어진다**(**droop**). 각 층에 **물탱크**(decoupling capacitor, 디커플링 커패시터)를 두면 탱크가 먼저 물을 내줘서 수압 하락을 막는다. 관이 가늘고 길면(저항) 꼭대기 층 수압이 늘 낮다(**IR drop**).

- 칩에서 전압이 떨어지면 트랜지스터가 **느려진다** → 같은 클럭 주기 안에 계산이 안 끝남 → **타이밍 에러**.
- 그래서 **PI 문제는 "랜덤 hang", "부하 걸 때만 실패", "특정 주파수에서만 실패"** 처럼 보인다 (→ [BV13](2026-11-01_BV13_board_debug_scenarios.html) 시나리오 10).

### 1.5 전체 흐름 한 장

```text
 ┌───────────── 원인 (물리) ──────────────┐     ┌──────── 측정 (검증 엔지니어가 하는 일) ────────┐
 │ SI : 반사·손실·crosstalk·jitter         │     │ eye diagram · jitter 분해 · PRBS BER · bathtub │
 │ PI : IR drop · droop · PDN 공진        │ ──▶ │ rail scope(droop) · on-die droop/temp 센서     │
 │ 공정 편차(P) · 전압(V) · 온도(T)        │     │ margining: 전압(PMIC) · 타이밍 · 주파수(PLL)   │
 └───────────────────────────────────────┘     └───────────────────┬───────────────────────────┘
                                                                   ▼
                       ┌────────── shmoo (V × f 격자) × PVT 코너 × 샘플 N개 ──────────┐
                       │ chamber 온도 → soak → 전압 설정·readback → PLL 설정·lock 확인 │
                       │ → 테스트 n회 → PASS/FAIL 기록 → 다음 점 (자동화, 보드 팜)    │
                       └──────────────────────────────┬───────────────────────────────┘
                                                      ▼
            보고: Vmin/Fmax 분포·Cpk · 최악 코너 · 구멍/벽 디버그 결과 · 스펙 제안 ·
                  AVS/DVFS 표 · 양산 스크린 조건(전압·온도·guardband) · HW 수정 요청
```

### 1.6 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| SI | Signal Integrity | 신호가 **찌그러지지 않고** 도착하는 정도 | 고속 링크(PCIe·DDR·ETH·MIPI) 에러의 근본 원인 |
| PI | Power Integrity | 전원이 **흔들리지 않고** 공급되는 정도 | droop → 타이밍 실패, 랜덤 hang |
| 임피던스 (Z0) | characteristic impedance | 선이 신호에게 보이는 "관 굵기" (보통 단선 50 Ω, 차동 85–100 Ω) | 굵기가 바뀌면 반사 |
| 반사 계수 Γ | reflection coefficient | (Z_L − Z_0)/(Z_L + Z_0) — 되돌아오는 비율 | Γ=0이면 반사 없음 (정합) |
| insertion loss | 삽입 손실 | 선을 지나며 줄어든 신호 크기 (dB, 주파수별) | Gen5/Gen6처럼 빠를수록 크게 깎임 → equalization 필요 |
| ISI | Inter-Symbol Interference | 앞 비트가 번져 다음 비트를 방해 | 손실·반사의 결과, 패턴 의존 에러 |
| crosstalk (NEXT/FEXT) | near-/far-end crosstalk | 옆 선 신호가 섞여 들어옴 (가까운 쪽/먼 쪽) | 특정 패턴·특정 레인 조합에서만 실패 |
| equalization (CTLE/FFE/DFE) | 등화 | 손실을 미리/나중에 보정하는 필터 | 링크 트레이닝이 이 값을 찾음 ([LP03](2026-10-31_LP03_link_training_ltssm.html)) |
| UI | Unit Interval | 비트 하나의 시간 (예: 16 GT/s → 62.5 ps) | eye width는 UI 비율로 말함 |
| eye height / width | 눈 높이 / 폭 | 세로(전압)·가로(시간)로 열린 정도 | 링크 margin의 직접 지표 |
| jitter | 지터 | 엣지 시각의 흔들림 | eye width를 깎음 |
| RJ / DJ / TJ | Random / Deterministic / Total Jitter | 무작위(가우시안, 끝 없음) / 원인 있는 유한 지터 / BER 기준 전체 | TJ(BER) = DJ + 2Q·RJ |
| BER | Bit Error Ratio | 틀린 비트 수 ÷ 보낸 비트 수 | 링크 품질 최종 판정 (예: ≤ 1e-12) |
| PRBS | Pseudo-Random Binary Sequence | LFSR이 만드는 "랜덤처럼 보이는" 반복 패턴 (PRBS7/15/31) | 송신·수신이 같은 패턴을 알아 비트 단위로 에러를 셈 |
| bathtub curve | 욕조 곡선 | 샘플링 위치별 BER을 그린 U자 곡선 | 목표 BER에서의 eye width를 외삽 |
| IR drop | I × R 전압 강하 | 전류가 배선 저항을 지나며 생기는 일정한 전압 손실 | 칩 먼 곳의 실제 전압이 낮음 |
| droop | 전압 처짐 | 부하가 갑자기 늘 때 순간적으로 떨어지는 전압 | 순간 타이밍 실패 |
| PDN | Power Delivery Network | VRM → 보드 plane → 커패시터 → 패키지 → 다이까지 전원 경로 전체 | 주파수별 임피던스가 낮아야 droop 작음 |
| decoupling cap | 디커플링 커패시터 | 칩 가까이 붙은 "작은 물탱크" | 빠른 전류 변화를 먼저 공급 |
| VRM / PMIC | Voltage Regulator Module / Power Management IC | 전압 만드는 칩 | **voltage margining 손잡이** (PMBus/I2C) |
| margining | 마진 측정 | 조건을 일부러 나쁘게 해서 여유를 재기 | 특성화의 기본 동작 |
| LMR | Lane Margining at Receiver | PCIe Gen4+ 수신기 샘플 점을 옮겨 margin을 재는 표준 기능 | 링크 끊지 않고 margin 측정 ([LP17](2026-10-31_LP17_gen5_gen6_retimer_automotive.html)) |
| shmoo plot | 시무 플롯 | 2축 조건 격자의 PASS/FAIL 지도 | 동작 영역·마진·이상 패턴을 한눈에 |
| Vmin / Fmax | 최소 동작 전압 / 최대 동작 주파수 | 주어진 f에서 통과하는 최저 V / 주어진 V에서 통과하는 최고 f | 스펙·AVS·스크린의 기본 숫자 |
| PVT | Process · Voltage · Temperature | 칩 동작을 바꾸는 3대 변수 | 최악 코너를 찾는 축 |
| FF / TT / SS | Fast-Fast / Typical / Slow-Slow corner | NMOS·PMOS가 둘 다 빠른/보통/느린 칩 | SS는 느림(Fmax↓), FF는 빠르지만 누설·전력↑ |
| skew lot / corner lot | 코너 로트 | 팹이 공정 조건을 일부러 비틀어 만든 웨이퍼 묶음 | post-silicon에서 FF/SS 칩을 손에 넣는 방법 |
| process monitor / ring oscillator | 공정 모니터 | 칩 안 링 발진기 주파수로 "이 칩이 빠른가 느린가" 측정 | 칩별 코너 분류, AVS 입력 |
| Tj / Ta | Junction / Ambient Temperature | 실리콘 내부 온도 / 주변 공기 온도 | 스펙은 대개 Tj, 자동차 등급은 Ta ([BV15](2026-11-01_BV15_automotive_board_considerations.html)) |
| temperature inversion | 온도 역전 | 저전압에서는 **저온이 더 느린** 현상 | "최악 = 고온" 가정이 깨짐 |
| AVS / DVFS | Adaptive Voltage Scaling / Dynamic Voltage-Frequency Scaling | 칩별(AVS) 또는 상황별(DVFS)로 전압·주파수를 바꿈 | 특성화 결과로 테이블을 만든다 |
| Cpk | process capability index | 분포 평균이 스펙 한계에서 몇 "3σ" 떨어져 있나 | 1.33 / 1.67 이상이면 양산 안정 |
| guardband | 가드밴드 | 측정 불확실성·노화·편차를 위해 남겨 두는 추가 여유 | 스크린 한계 = 스펙 + guardband |
| chamber / thermostream | 항온 챔버 / 국부 온도 강제기 | 보드 전체 / 칩 하나를 정해진 온도로 | PVT의 T축 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 SI — 반사, 손실, crosstalk, ISI

- **반사**: 신호가 특성 임피던스 Z0인 선을 따라가다 부하/불연속 Z_L을 만나면 **Γ = (Z_L − Z_0)/(Z_L + Z_0)** 만큼 되돌아온다. Z_L = Z_0 이면 Γ = 0(정합), 열린 끝(Z_L = ∞)이면 Γ = +1, 단락이면 −1. 보드에서 흔한 불연속: **via stub**(층을 바꾸는 구멍의 남는 부분), 커넥터, BGA 패드, AC coupling cap 패드, 테스트 포인트 stub [추정: 일반 SI 교과서 수준].
- **손실(insertion loss, IL)**: 도체 손실(표피 효과)과 유전체 손실은 **주파수가 높을수록 커진다**. 그래서 채널 예산은 보통 **Nyquist 주파수(= 비트레이트/2, NRZ 기준)** 에서의 dB로 말한다. PCIe Gen5/Gen6 채널 예산·retimer 이야기는 [LP17 §2.6](2026-10-31_LP17_gen5_gen6_retimer_automotive.html) 참고 (여기서는 반복하지 않음).
- **ISI**: 손실·반사 때문에 한 비트의 에너지가 다음 비트로 번짐 → **패턴 의존 에러**(긴 0 연속 뒤 1 같은 패턴에서 실패). PRBS 길이가 긴 패턴(PRBS31)이 짧은 것(PRBS7)보다 ISI를 더 잘 자극하는 이유.
- **Crosstalk**: 인접 선 간 결합. 송신단 근처에서 보이면 NEXT, 먼 끝에서 보이면 FEXT. 증상은 "**옆 레인이 동시에 동작할 때만**" 에러 → 테스트 시 **모든 레인을 동시에** 트래픽 걸어야 하는 이유.
- **Equalization**: 송신 FFE(pre-/post-cursor emphasis), 수신 CTLE(고주파 증폭), DFE(이전 비트 결정으로 ISI 빼기). 링크 트레이닝이 이 값(preset/계수)을 자동으로 고른다 → 검증에선 **preset 스윕**이 margin 축이 되기도 함.

### 2.2 Eye diagram과 jitter 분해

- **eye height**: 샘플 시점에서 0과 1 레벨 사이의 세로 열림(mV). **eye width**: 판정 전압에서 가로 열림(ps 또는 UI 비율).
- **Jitter 분류**:

| 종류 | 성질 | 대표 원인 | 분포 |
|---|---|---|---|
| **RJ** (Random) | **끝이 없다**(unbounded) — 오래 볼수록 더 큰 값이 나옴 | 열잡음, PLL/발진기 위상 잡음 | 가우시안, σ(rms)로 표기 |
| **DJ** (Deterministic) | 유한(bounded), 원인 있음 | 아래 세부 | peak-to-peak |
| └ DDJ = ISI + DCD | 데이터 패턴에 따라 | 채널 손실·반사(ISI), duty cycle 왜곡(DCD) | 패턴 의존 |
| └ PJ (Periodic) | 주기적 | 스위칭 전원 리플, SSC, 클럭 커플링 | 사인파형 |
| └ BUJ (Bounded Uncorrelated) | 데이터와 무관 | crosstalk | 유한 |

- **TJ(BER) = DJ + 2·Q(BER)·RJ_rms** (dual-Dirac 모델). BER = 1e-12에서 Q ≈ 7.03 → **TJ ≈ DJ + 14.07 × RJ** [2][3]. 그리고 **eye width(BER) = 1 UI − TJ(BER)** [3].
  - 의미: RJ는 끝이 없어서 **"jitter가 얼마냐"는 BER을 같이 말해야** 의미가 있다. "TJ @ 1e-12 = 0.3 UI" 처럼.
- **Bathtub curve**: 샘플링 위치를 eye 왼쪽 끝에서 오른쪽 끝까지 옮기며 BER을 그리면 U자(욕조). 가운데는 BER이 아주 낮고 가장자리로 가면 0.5에 가까워짐. 실제 측정은 1e-6~1e-9 정도까지만 재고 **RJ 가우시안 꼬리를 외삽**해서 1e-12 지점의 폭을 구한다 [2][4].

```text
 log10(BER)
   0 ┤█                                        █
  -3 ┤ █                                      █
  -6 ┤  █                                    █      ← 여기까지는 실제로 측정
  -9 ┤   ██                                ██
 -12 ┤─────██──────────────────────────────██────── ← 목표 BER: 여기서의 폭 = eye width @1e-12
 -15 ┤       ███                       ███          ← 이 아래는 RJ 가우시안으로 외삽
     └──┬─────────────────┬──────────────────┬───▶ 샘플링 위치
       0 UI            0.5 UI              1 UI
```

### 2.3 BER 측정과 PRBS

- **BER = 에러 비트 수 / 전송 비트 수**. 1e-12 = "1조 비트에 1개".
- **신뢰도**: 에러 0으로 N비트를 봤을 때 "진짜 BER ≤ 목표"라고 말할 수 있는 신뢰도 CL = 1 − e^(−N·BER). **CL 95 % → N ≈ 3/BER** (정확히 −ln 0.05 ≈ 2.996) [9][10].

| 목표 BER | 필요한 비트 (95 %, 에러 0) | 1 Gb/s | 10 Gb/s | 16 Gb/s 1 레인 | 16 Gb/s × 4 레인 동시 |
|---|---|---|---|---|---|
| 1e-9 | 3e9 | 3 s | 0.3 s | 0.19 s | 레인별로 계산 |
| 1e-12 | 3e12 | **50 min** | **5 min** | 3.1 min | 레인마다 3.1 min (병렬) |
| 1e-15 | 3e15 | 35 일 | 3.5 일 | 2.2 일 | → 외삽(bathtub)이나 stressed 조건으로 가속 |

- 시간 계산은 위 공식으로 직접 한 것 [확인됨: 산수]. 그래서 PVT 전 코너 × 전 레인 × 1e-12는 **비싸다** → 실전은 "짧은 BER + margining(눈 가장자리로 일부러 옮겨서 BER을 높여 빨리 측정)" 조합.
- **PRBS**: n비트 LFSR이 만드는 주기 2ⁿ − 1 패턴. 흔한 다항식 예: PRBS7 = x⁷ + x⁶ + 1, PRBS31 = x³¹ + x²⁸ + 1 [추정: ITU-T O.150 계열로 널리 쓰이는 값, 원문 미확인]. 긴 PRBS일수록 긴 run-length를 포함 → ISI를 더 세게 자극. PHY(Ethernet PHY, SerDes, PCIe PHY)는 보통 **내장 PRBS 생성기/검사기**와 에러 카운터를 갖고 있어서 장비 없이도 BER을 잴 수 있다 → bring-up 테스트 FW가 이 레지스터를 다룬다 ([BV10](2026-11-01_BV10_board_interface_tests.html)).

### 2.4 PI — IR drop, droop, PDN 임피던스, decoupling

- **IR drop**: DC 전류 × 경로 저항. 예: 20 A가 1 mΩ 경로를 지나면 20 mV 손실 = 0.75 V rail의 2.7 %. 그래서 PMIC는 **remote sense**(칩 가까운 곳 전압을 따로 읽어 보정)를 쓴다 → 측정도 **칩 근처 sense 점**에서 해야 한다.
- **Load-step droop**: 전류가 ΔI만큼 빠르게 바뀌면 PDN 임피던스 Z(f) × ΔI 만큼 전압이 순간 하강. 시간 축으로 보면 다이/패키지 쪽에서 오는 아주 빠른 **1차 droop**(수십 ns급), 보드 커패시터가 메우는 2차, VRM 제어 루프가 따라오는 3차 droop으로 나눠 말한다 [추정: 업계 관용 표현].
- **PDN target impedance**: **Z_target = V_rail × (허용 리플 비율) / ΔI_transient** [5][6]. 예: 0.75 V rail, ±3 % 허용, 20 A 부하 계단 → 0.75 × 0.03 / 20 = **1.125 mΩ**. 이 값 아래로 DC부터 수백 MHz까지 PDN 임피던스를 유지하도록 VRM·벌크·세라믹 cap·패키지·온다이 cap을 계층적으로 배치한다.
- **Anti-resonance**: 서로 다른 커패시터(또는 cap과 plane 인덕턴스)가 특정 주파수에서 **공진 피크**를 만든다 → 그 주파수 근처로 부하 전류가 **주기적으로** 출렁이면(예: 워크로드 루프가 그 주파수) droop이 크게 증폭 [5][6]. → shmoo에서 **"특정 주파수에서만 실패하는 세로줄(finger)"** 의 후보 원인.
- **PI → 타이밍**: 게이트 지연은 전압에 민감 → droop 동안 경로 지연 ↑ → setup 위반. 그리고 전원 노이즈는 PLL/클럭 버퍼의 지연도 흔들어 **supply-induced jitter**가 된다. 결국 PI 문제는 **"전압 margin이 생각보다 작다"** 로 shmoo에 나타난다.
- 칩 쪽 대응: **on-die droop detector**, adaptive clocking(droop 감지 시 클럭 늦춤), AVS — 존재 여부는 칩마다 다름 [추정: RAP1 미공개].

### 2.5 Margining 세 가지

| 종류 | 손잡이 | 어떻게 | 결과 | 주의 |
|---|---|---|---|---|
| **전압 margining** | PMIC/VRM 출력 (PMBus/I2C, 또는 VID 핀, 또는 피드백 저항 trim) | rail을 nominal ± 5–10 % (또는 계단식 스윕) | rail별 Vmin/Vmax | **설정값이 아니라 칩 근처 실측(readback + DMM)** 으로 기록. 다른 rail과의 상대 순서·한계(abs max) 지킬 것 |
| **타이밍 margining** | DDR PHY delay 레지스터, PCIe LMR, SerDes 샘플 위상 | 샘플링 시점/판정 전압을 eye 중심에서 좌우·상하로 옮기며 에러가 날 때까지 | eye width/height (ps, mV, UI%) | 측정 중 링크 상태 변화(ASPM 등) 끄기. 측정 자체가 트래픽에 영향 |
| **주파수 margining** | PLL 분주비 (코어/NoC/DDR 클럭) | nominal 위로 계단식으로 올림 | Fmax, 주파수 margin % | 다른 블록의 클럭 비율·CDC 제약이 같이 바뀌는지 확인 |

- **PMBus 전압 margining (표준 명령 코드)** [7]: `OPERATION` (0x01) 의 margin 비트로 nominal / margin low / margin high 선택, 목표는 `VOUT_MARGIN_HIGH` (0x25) / `VOUT_MARGIN_LOW` (0x26), 직접 설정은 `VOUT_COMMAND` (0x21), 실측은 `READ_VOUT` (0x8B) · `READ_IOUT` (0x8C) · `READ_TEMPERATURE_1` (0x8D), 상태는 `STATUS_WORD` (0x79) · `STATUS_VOUT` (0x7A), 숫자 형식은 `VOUT_MODE` (0x20). OPERATION 비트 배치·fault 응답은 **벤더 데이터시트마다 다름** [8].
- **DDR 타이밍 margin**: training이 찾은 각 DQ/DQS의 **통과 창(window)의 왼쪽·오른쪽 끝과 중심**을 레지스터/로그로 덤프 → 창 폭이 곧 margin. 벤더 PHY 툴이나 FW에서 **"eye scan"**(read/write delay × VREF 2D 스윕) 제공 [추정: 벤더별 상이]. 세부 training 단계는 [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html).
- **PCIe Lane Margining at Receiver (Gen4+)**: 16 GT/s 이상에서 timing margin 필수, voltage margin은 16 GT/s 선택·32 GT/s 이상 필수. SW가 **error count limit**을 정하고 offset을 한 단계씩 늘리다 limit에 닿으면 멈추고 수신기는 원래 설정으로 복귀 [11][12]. 판정값 표·`pcilmr` 사용법은 [LP17 §2.8](2026-10-31_LP17_gen5_gen6_retimer_automotive.html) 참고.
- **주파수 margin과 성능 스윕의 관계**: 주파수를 올리며 **"되나"** 를 보면 shmoo, **"얼마나 빠른가"** 를 보면 성능 스윕 → [성능 메트릭 §6.1](2026-10-30_perf_metrics_ssd_vs_soc.html) ("Shmoo tells you where it works; the frequency sweep tells you what performance you get at each working point").

### 2.6 Shmoo — 정의, 실행 규칙, 모양 읽기

- **정의**: 조건 2개(가장 흔하게 전압 × 주파수, 그 외 전압 × 온도, DDR VREF × delay 등)를 격자로 스윕하며 같은 테스트를 돌려 PASS/FAIL을 칠한 그림 [13]. 1960년대 IBM 메모리 매뉴얼·IEEE 논문까지 거슬러 올라가는 오래된 용어 [13].
- **실행 규칙 (내가 정할 것)**:
  - 축 범위와 step (예: 25 mV × 100 MHz, 경계 근처는 더 촘촘히)
  - **점당 반복 횟수(loop count)** — 1회 PASS는 증거가 약함. 경계 근처는 PASS/FAIL이 섞임(~) → "몇 번 중 몇 번"을 기록
  - 테스트 내용 — 어떤 workload/패턴이 **critical path**를 자극하나 (단순 루프 vs 실제 워크로드 vs 전력 바이러스)
  - 점 사이 상태: 실패 후 **리셋/재초기화** 할지, 전압을 바꾸는 방향(위→아래, 아래→위; 히스테리시스 확인)
  - 기록 메타데이터: 보드 SN, 칩 ID/lot/코너, 온도(설정값·실측 Tj), 전압(설정값·readback), FW 버전, 테스트 버전
- **모양별 해석** (Design & Reuse 정리 기반 + 일반론) [14]:

| 모양 | 생김새 | 흔한 의미 | 다음 행동 |
|---|---|---|---|
| **정상** | 왼쪽 위가 PASS, 대각선 경계 | 전압 ↑ → 더 빠름 (지연이 V에 반비례) | nominal에서 경계까지 거리 = margin |
| **벽 (wall)** | 특정 전압 아래/위에서 **주파수 무관** 실패 | 저전압 쪽: SRAM Vmin(비트셀 안정성), 고전압 쪽: hold 위반·노이즈 커플링·race | 해당 블록 MBIST·별도 rail 확인 |
| **brick wall** | 같은 조건에서도 **첫 실행 실패·두 번째 통과** 같은 쌍안정 | 리셋 값 없는 레지스터 등 **초기화 문제** | 리셋/초기화 경로 점검, 매 점 cold reset으로 재실험 |
| **floor** | **낮은** 주파수에서 실패 | 누설·dynamic 노드 유지 문제(고온에서 악화) | 고온 shmoo 비교 |
| **finger** | 특정 주파수에서만 세로로 실패 | 커플링·공진(PDN anti-resonance)·클럭 비율 의존 버그 | 그 주파수 근처 촘촘히, droop scope, 클럭 비율 확인 |
| **구멍 (hole)** | 통과 영역 **안쪽**에 실패 점 | ① 재현됨 → 조건부 실제 버그(특정 V·f 조합의 설정/공진/CDC) ② 재현 안 됨 → 노이즈·간헐 버그·**장비/설정 미적용** | **20회 이상 재실행** → 반복성 판단, 설정 readback(전압·PLL lock) 확인 |
| **cliff / 들쭉날쭉 경계** | 경계가 들쭉날쭉, 보드마다 다름 | 측정 노이즈, 온도 불안정, 반복 횟수 부족 | soak 시간·반복 횟수 ↑, 온도 실측 |
| **경계가 전체 이동** | 다른 보드/칩에서 같은 모양이 평행이동 | 공정 코너 차이(정상) | 코너 분류(ring osc)와 상관 확인 |

- **정량화**: shmoo 통과 면적(V × MHz)을 "shmoo quality"로 쓰는 툴도 있다 [13]. 실무 보고는 대개 **Vmin@f, Fmax@V, nominal 기준 margin %**.

### 2.7 PVT — 코너를 손에 넣는 법

- **Process**:
  - 공정 편차 = 같은 설계라도 칩마다 트랜지스터 속도가 다름 (웨이퍼 간·웨이퍼 내·다이 내).
  - 코너 이름: NMOS/PMOS 각각 Fast/Typical/Slow → **FF, TT, SS** (+ FS, SF = 한쪽만 빠른 "skewed" 코너, 레벨 시프터·SRAM에 중요).
  - **Post-silicon에서 코너 칩 얻기**: 팹에 **skew lot / corner lot**(공정 조건을 일부러 FF·SS 쪽으로 비튼 웨이퍼)을 요청 → 이 칩들로 특성화. 또는 대량 샘플에서 **ring oscillator / process monitor** 값으로 빠른·느린 칩을 골라 냄 [추정: 업계 일반 관행, 회사마다 다름].
  - SS = 느림 → Fmax/Vmin 최악 후보. FF = 빠르지만 **누설 전류·전력·발열 최악** → 열·전력 한계 후보, hold 위반 후보.
- **Voltage**: nominal ± (PMIC 정확도 + 리플 + IR drop + droop 예산) 를 덮도록. 보통 ± 5–10 % 스윕 [BV00 기준]. **AVS가 있는 칩은 칩마다 nominal이 다르다** → "어느 nominal 기준 margin인가"를 명시.
- **Temperature**:
  - 자동차는 AEC-Q100 Grade로 **주변 온도(Ta)** 범위를 정한다 — Grade 1 = −40 ~ +125 °C, Grade 2 = −40 ~ +105 °C, Grade 0 = −40 ~ +150 °C [15]. 칩 내부 Tj는 자기 발열로 Ta보다 높다 → 특성화는 **Tj 기준**으로 기록하고, 칩 온도 센서·열전대로 실측. 상세는 [BV15](2026-11-01_BV15_automotive_board_considerations.html).
  - **Chamber**(보드 전체를 넣는 항온 챔버) vs **thermostream / thermal forcer**(칩 위에 공기를 쏘아 칩만 온도 강제). 챔버는 보드 위 다른 부품(PMIC, DRAM, 크리스탈)까지 온도가 바뀜 → **칩 문제인지 보드 부품 문제인지** 섞임. thermostream은 칩만 바꿔 분리 가능.
  - **Soak time**: 설정 온도 도달 후 실제 칩·보드가 평형에 이를 때까지 기다림. 안 기다리면 경계가 들쭉날쭉.
  - **Temperature inversion**: 높은 전압에서는 고온이 느림(이동도 ↓), **낮은 전압에서는 저온이 느림**(문턱 전압 Vth ↑ 효과가 더 큼). 그래서 **Vmin 최악 코너가 −40 °C**일 수 있다 → 코드 실행 결과 §6 출력 블록 5 표에서 재현 [추정: 모델; 실제 교차점은 공정마다 다름].
  - **결로**: 저온 → 상온 복귀 시 습기 응결 → 질소 퍼지·건조 공기 [추정: 랩 일반].

### 2.8 샘플 수와 통계

- **왜 여러 개?** 한 보드의 Vmin = 0.66 V는 **분포의 점 하나**. 고객/양산에 필요한 건 **수백만 개 중 최악**이 스펙을 지키느냐 → 분포(평균 μ, 표준편차 σ, 꼬리)를 추정해야 한다.
- **Cpk (공정 능력 지수)**: Cpk = min(USL − μ, μ − LSL) / (3σ). 한쪽 한계만 있으면 그쪽만. **Cpk 1.33 = 평균에서 한계까지 4σ, 1.67 = 5σ**. 자동차·반도체에서 1.33은 최소, 1.67이 흔한 목표 [16]. 정규분포 가정이 깨지면(쌍봉, 꼬리 두꺼움) Cpk가 거짓말함 → **히스토그램을 먼저 본다**.
- **"0 fail로 몇 개 봐야 하나" (success-run)**: n = ln(1 − C) / ln(R). 예: 신뢰도 R = 90 %를 신뢰수준 C = 95 %로 → n = ln 0.05 / ln 0.9 ≈ **29개**. AEC-Q100 신뢰성 시험이 흔히 **3 lot × 77개 = 231개, 0 fail** 을 쓰는 것도 같은 계열 논리(LTPD 1 % @ 90 % 신뢰 ≈ 230개) [17] [추정: LTPD 해석은 계산 기반].
- **실무 조합 예** [추정: 회사마다 다름]: 코너 칩(SS/TT/FF) 각 수 개~수십 개 × 3–5 온도 × 전압 스윕 × 여러 테스트 → 수천~수만 점 → **자동화 없이는 불가능** (→ §3.3, [BV14](2026-11-01_BV14_board_validation_plan_automation.html)).

### 2.9 특성화 결과로 무엇을 넘기나 (design / product / test 팀)

| 받는 쪽 | 무엇을 | 예 |
|---|---|---|
| **스펙 / 데이터시트** | 보장 동작 범위 | "1.5 GHz @ 0.75 V ± 5 %, Tj −40 ~ 125 °C" |
| **전원/PM 팀 (AVS·DVFS)** | 코너별 Vmin@f → **OPP 표**(주파수별 전압), 칩별 bin(ring osc 값 → 전압 offset) | SS bin은 +25 mV, FF bin은 −15 mV [예시] |
| **양산 테스트 (ATE/SLT)** | **스크린 조건**: 어떤 온도·전압·주파수에서 어떤 테스트를 몇 % guardband로 | "hot 125 °C, Vmin + 30 mV, 1.6 GHz에서 통과해야 출하" [예시] |
| **설계 팀** | 실패 모양·위치 → critical path·SRAM Vmin·hold 문제 리포트 | "SRAM 벽이 SS −40 °C에서 0.70 V까지 올라옴" |
| **보드 팀** | PI/SI 문제 증거 | "1.3 GHz finger — PDN 공진 의심, droop 캡처 첨부" |
| **errata / workaround** | 고칠 수 없는 경우의 제약 | "OPP 1.3 GHz 사용 금지 (B0에서 수정)" |

## 3. 보드에서 볼 것 · 잴 것

### 3.1 측정 체크리스트

| 항목 | 어디서 | 장비/방법 | 설정 포인트 | 통과 기준 (예) |
|---|---|---|---|---|
| 고속 링크 eye | 수신단 근처 (또는 칩 내부 eye scan) | 고대역 실시간/샘플링 scope + 차동 프로브, 또는 PHY 내장 eye scan, LMR | 프로브 부하·de-embedding, 컴플라이언스 패턴 | 표준 마스크 통과, LMR 값 ≥ 권장값 |
| jitter | refclk / 데이터 | scope jitter 분석(RJ/DJ 분리), phase noise analyzer | 측정 길이(RJ는 오래 볼수록 커짐) | TJ@1e-12 ≤ 스펙 |
| BER | 링크 전체 | PHY 내장 PRBS gen/checker, BERT | 모든 레인 동시, PRBS31, 3/BER 비트 | 0 에러 (95 % CL) |
| rail droop | **칩 근처 sense 점 / 패키지 cap** | scope + 짧은 ground spring, 20 MHz BW limit은 **ripple용**이지 droop용 아님 | 부하 계단을 만드는 테스트로 트리거 | droop + DC 오차 ≤ 예산 (예: ±3 %) |
| IR drop | sense 점 vs PMIC 출력 | DMM 2점 비교, PMIC READ_VOUT | 최대 부하 | 차이 ≤ 예산 |
| 전압 margining | PMIC | PMBus/I2C 명령 → **readback + DMM 확인** | 다른 rail 영향, OV/UV fault 한계 | 설정값 = 실측값 ± 정확도 |
| 온도 | 칩 온도 센서 + 패키지 열전대 + 챔버 | chamber/thermostream | soak, Tj 실측 기록 | 목표 Tj ± 2 °C [예시] |
| 코너 분류 | 칩 내부 ring osc / process monitor 레지스터 | FW로 읽기 | 같은 온도·전압에서 | 칩별 코드 기록 |

### 3.2 명령·로그 예시와 해석

**(a) PMBus로 rail 읽고 margin 걸기 — Linux i2c-tools** `[예시 출력]` (버스 3, PMIC 주소 0x40은 가정)

```bash
i2cget -y 3 0x40 0x20 b        # VOUT_MODE
i2cget -y 3 0x40 0x8b w        # READ_VOUT (word, LINEAR16)
i2cset -y 3 0x40 0x26 0x0170 w # VOUT_MARGIN_LOW = 0x170 (아래 해석)
i2cset -y 3 0x40 0x01 0x98 b   # OPERATION: ON + margin low (비트 배치는 데이터시트 확인!)
i2cget -y 3 0x40 0x8b w        # 다시 READ_VOUT 으로 실제 값 확인
i2cget -y 3 0x40 0x79 w        # STATUS_WORD: fault 없는지
```

```text
0x17          ← VOUT_MODE: 하위 5비트 10111b = -9 (2의 보수) → 전압 = mantissa × 2^-9
0x0180        ← READ_VOUT = 384 → 384 / 512 = 0.750 V   (nominal)
0x0170        ← margin low 후 READ_VOUT = 368 → 368 / 512 = 0.719 V  (-4.2 %)
0x0000        ← STATUS_WORD: fault 없음
```

- 해석: LINEAR16 형식에서 exponent는 VOUT_MODE에 있고 READ_VOUT은 mantissa만 준다. **설정했다고 끝이 아니라 readback + 칩 근처 DMM**으로 확인하고 그 **실측값**을 shmoo 축에 기록한다. OPERATION 값 0x98은 예시일 뿐 — margin 비트 위치와 fault 응답 비트는 PMIC마다 다르다 [8]. 실제 차량용 PMIC는 PMBus가 아니라 **자체 I2C/SPI 레지스터 맵**인 경우가 많다 [추정].

**(b) Linux hwmon으로 telemetry** — pmbus 드라이버가 붙으면 표준 sysfs로 보인다 (단위: 전압 mV, 전류 mA, 온도 m°C) [18] `[예시 출력]`

```bash
grep . /sys/class/hwmon/hwmon*/name
cat /sys/class/hwmon/hwmon4/in2_input /sys/class/hwmon/hwmon4/curr2_input /sys/class/hwmon/hwmon4/temp1_input
```

```text
/sys/class/hwmon/hwmon4/name:tps53xxx     ← 어떤 hwmon이 PMIC인지 이름으로 찾기 (이름은 예시)
719                                       ← in2_input: 719 mV (margin low 적용됨)
18250                                     ← curr2_input: 18.25 A (부하 중)
71000                                     ← temp1_input: 71.0 °C (PMIC 자체 온도)
```

**(c) DDR training margin 덤프** `[예시 출력]` (형식은 가상의 bring-up FW 로그 — 실제 필드명은 PHY 벤더마다 다름)

```text
[ddr] ch0 rank0 read-eye  DQ0  L=-31 R=+29 C=0  width=60 taps (=0.47 UI)
[ddr] ch0 rank0 read-eye  DQ5  L=-18 R=+27 C=+4 width=45 taps (=0.35 UI)  <- 가장 좁음
[ddr] ch0 rank0 write-eye DQ5  L=-25 R=+24 C=0  width=49 taps
[ddr] ch0 vref-read sweep  pass 61.0% .. 78.5% of VDDQ  center 69.8%
[ddr] ch1 rank0 read-eye  DQ12 L=-12 R=+15 C=+1 width=27 taps (=0.21 UI)  <- 경고: 한계 근처
```

- 줄별 해석: L/R = 중심에서 왼쪽·오른쪽으로 몇 tap(지연 단위)까지 통과했나, width = 통과 창 폭 = **timing margin**. VREF sweep = 판정 전압 방향 margin. **DQ12만 유독 좁다** → 그 비트의 레이아웃(길이·via·crosstalk)·솔더·DRAM 핀 문제 의심 → 같은 보드 다른 칩/다른 보드 같은 비트와 비교(→ [BV13](2026-11-01_BV13_board_debug_scenarios.html) "10장 중 2장" 패턴). 온도별로 이 표를 쌓으면 **"고온에서 어느 비트가 먼저 닫히나"** 가 보인다.

**(d) PCIe 링크 margin** — `sudo pcilmr <포트>` 레시피와 판정표는 [LP17 §3.2](2026-10-31_LP17_gen5_gen6_retimer_automotive.html) `[미실행]`.

### 3.3 Chamber 안 shmoo 자동화 흐름 (20장 보드 기준)

```text
 for T in [-40, 25, 85, 125] °C:                       ← 바깥 루프는 가장 느린 축(온도)
   chamber.set(T) → wait soak → 모든 보드 Tj 센서가 목표 ± 2 °C 인지 확인 (아니면 대기/경고)
   for board in boards (병렬):                         ← 보드 팜: 보드별 전원·UART·JTAG
     for V in V_grid (높은 쪽 → 낮은 쪽):
       pmic.set(V) → readback V (DMM/READ_VOUT) → 불일치면 점 무효 처리 (FAIL 아님!)
       for f in f_grid:
         pll.set(f) → lock 비트 확인 → 실패면 '설정 실패'로 기록
         repeat n:  run test (timeout) → PASS/FAIL/HANG
         HANG이면 → 전원 사이클(리셋) → 다음 점 전에 nominal에서 "건강 확인" 테스트
       record (board SN, chip lot/corner code, T_set, Tj, V_set, V_meas, f, n_pass/n, fw, test ver)
 → DB → 자동 리포트: 보드별 shmoo, Vmin/Fmax 분포, Cpk, 구멍 목록(재실행 큐에 자동 추가)
```

- **핵심 설계 포인트** (면접에서 말할 것):
  1. **"설정 실패"와 "칩 실패"를 구분** — 전압이 안 바뀐 점, PLL이 lock 안 된 점, 장비 통신 실패는 **INVALID**로. 이게 없으면 장비 문제가 shmoo 구멍으로 보인다.
  2. **HANG 복구** — 각 점 사이에 원격 전원 사이클 + nominal 건강 확인. 안 그러면 한 점의 hang이 이후 모든 점을 FAIL로 오염.
  3. **Full grid vs edge search** — 특성화 초기엔 full grid(구멍 발견), 경계를 아는 후기·양산엔 edge search(시간 절약). §6 출력 블록 4에서 edge search가 구멍을 놓치거나 경계로 오인하는 것을 실제로 보여 줌.
  4. **재실행 큐** — 구멍·경계 들쭉날쭉 점은 자동으로 20회 재실행 → 반복 실패율 기록.
  5. **메타데이터** — 칩 corner code, 보드 rework 이력, FW 해시 없이는 6개월 뒤 아무도 해석 못 함.

### 3.4 특성화 리포트 한 장 요약 `[예시]`

| 항목 | SS | TT | FF | 스펙 | 판정 |
|---|---|---|---|---|---|
| Vmin @ 1.5 GHz (최악 온도) | 0.73 V (−40 °C) | 0.69 V (−40 °C) | 0.65 V (25 °C) | ≤ 0.7125 V | 🟡 SS 초과 → AVS +25 mV bin |
| Fmax @ 0.75 V (최악 온도) | 1.77 GHz | 1.96 GHz | 2.15 GHz | ≥ 1.65 GHz (+10 %) | ✅ |
| DDR read eye 최소 폭 (125 °C) | 0.24 UI | 0.31 UI | 0.33 UI | ≥ 0.25 UI | 🟡 SS 1개 비트 경계 |
| PCIe Gen4 LMR timing (최악 레인) | 0.33 UI | 0.40 UI | 0.41 UI | ≥ 0.30 UI(Min) | ✅ |
| 구멍 | 1.3 GHz × 0.725–0.75 V, 전 보드 재현 | 동일 | 동일 | 없음 | ❌ 설계 팀 티켓, OPP 1.3 GHz 임시 제외 |

## 4. 꼬리 질문 체인

### 4.1 "What's a shmoo plot and how do you read it?"

<details>
<summary>Q → 꼬리1 (hole) → 꼬리2 (automate 20 boards in a chamber) → 꼬리3 (edge search)</summary>

- **Q. What's a shmoo plot and how do you read it?** — "It's a pass/fail map over two operating conditions, most often core voltage on one axis and frequency on the other, where every point runs the same test several times. A healthy part shows a diagonal boundary because more voltage makes the logic faster. I read the margin as the distance from the nominal point to that boundary — how much voltage I can drop at nominal frequency and how much frequency I can add at nominal voltage — and then I look for shapes that break monotonicity, like walls, fingers or holes." · 한국어: 2축 PASS/FAIL 지도 → nominal에서 경계까지 거리 = margin → 단조성 깨는 모양 찾기.
- **꼬리1. There's a hole in the middle of the passing region — what does that mean?** — "First I check whether it's real: rerun those points twenty or more times, and confirm the setting actually took effect — read back the rail voltage and the PLL lock — because a missed PMIC write looks exactly like a hole. If it fails every time, it's a conditional bug: something specific to that voltage-frequency combination, like a clock-ratio dependent crossing, a PDN resonance at that frequency, or an initialization race. If it fails only occasionally, it's an intermittent failure that happened to land there, and I treat it as a reliability issue with a measured failure rate." · 한국어: ① 재실행 ② 설정 readback ③ 반복 실패=조건부 버그, 가끔=간헐 이슈.
- **꼬리2. How would you automate a shmoo on 20 boards in a chamber?** — "Temperature is the outer loop because it's the slowest: set, soak, and verify the junction temperature from on-die sensors before testing. Inside, each board runs in parallel through a farm controller with remote power, UART and JTAG. For each point I set the rail, read it back, set the PLL, check lock, run the test N times with a timeout, and on a hang I power-cycle and run a health check at nominal before continuing. Setting failures are recorded as invalid, not as fails. Everything goes into a database keyed by board, chip corner, temperature and firmware version, and the report generator flags holes for automatic reruns." · 한국어: 온도 바깥 루프·soak·Tj 확인 → 보드 병렬 → set·readback·lock·N회·timeout → hang 복구 → INVALID 구분 → DB·자동 재실행.
- **꼬리3. A full grid takes too long. How do you speed it up, and what's the risk?** — "Edge search: for each frequency, step voltage until the result flips and stop, or binary-search the boundary. It cuts runtime a lot, but it assumes the map is monotonic. A top-down search will report a hole as the boundary, and a bottom-up search will never visit the hole at all. So I run a full grid on early silicon and a few sample boards per corner, and use edge search once the shape is known — and always on top of a couple of full columns as a sanity check." · 한국어: edge search는 단조성 가정 → 초기엔 full grid, 이후 edge search + 일부 full column.

</details>

### 4.2 "How would you characterize DDR margin across PVT?"

<details>
<summary>Q → 꼬리1 (how many samples) → 꼬리2 (what to report) → 꼬리3 (fails only at hot)</summary>

- **Q. How would you characterize DDR margin across PVT?** — "I'd take corner silicon — SS, TT and FF from skew lots — on several boards each, and run in a chamber at cold, room and hot. At each temperature I'd sweep the DRAM and controller rails with the PMIC, and at each point run training and dump the per-bit read and write eye widths and the VREF window, then run a memory stress test so I have both the measured margin and a functional result. The output is the minimum eye per bit across conditions, which bit and which corner is worst, and how margin trends with temperature." · 한국어: 코너 칩 × 보드 여러 장 × 온도 × 전압 → training eye 덤프 + 메모리 스트레스 → 비트별 최소 eye.
- **꼬리1. How many samples is enough?** — "Enough to estimate the distribution, not just the mean. A few parts per corner tells you where the corners are; to make a production statement you need tens of parts and a capability number like Cpk, or a zero-failure sample size from the confidence you need — for example about 29 parts with zero failures gives 90 percent reliability at 95 percent confidence. I'd also make sure the samples span lots and boards, because two boards from the same panel aren't independent." · 한국어: 분포를 추정할 만큼 — 코너당 수 개로 위치, 수십 개로 Cpk, success-run 공식.
- **꼬리2. What would you report to the design team?** — "The worst-case margin with its condition, the distribution with Cpk against the spec, which bits or lanes limit the margin and whether that tracks the board layout or the silicon corner, and any non-monotonic behavior with reproduction data. Then concrete recommendations: spec limits, a voltage or AVS adjustment, production screen conditions with guardband, or a board change." · 한국어: 최악 margin+조건, 분포·Cpk, 제한 비트/레인과 원인 추정, 권고(스펙·AVS·스크린·보드 수정).
- **꼬리3. It passes at room temperature but fails at hot.** — "Hot reduces mobility, increases leakage and shifts DRAM timing, and refresh needs to be faster above 85 °C on many DRAMs, so first I check whether training was done cold and reused hot — retraining or periodic recalibration at temperature may fix it. Then I compare the eye dumps at both temperatures to see which bits close first, and check refresh settings and the DRAM temperature sensor path." · 한국어: 고온 → 이동도↓·누설↑·refresh 요구 증가 → 저온 training 재사용 여부·재교정·비트별 eye 비교·refresh 설정 [추정: 85 °C 이상 refresh 2배는 DRAM 일반 관행, 디바이스별 확인].

</details>

### 4.3 "What's an eye diagram and what closes the eye?"

<details>
<summary>Q → 꼬리1 (RJ vs DJ) → 꼬리2 (why BER needs long tests) → 꼬리3 (equalization)</summary>

- **Q. What's an eye diagram and what closes the eye?** — "It's every bit of a serial signal overlaid on one unit interval, so you see the open area where the receiver can sample safely. The height is the voltage margin and the width is the timing margin. Channel loss and reflections cause inter-symbol interference that closes it vertically and horizontally, crosstalk adds noise, jitter narrows it horizontally, and power supply noise does both." · 한국어: 1 UI에 겹친 그림, 높이=전압 margin, 폭=타이밍 margin, 손실·반사(ISI)·crosstalk·jitter·전원 노이즈가 닫음.
- **꼬리1. What's the difference between random and deterministic jitter?** — "Random jitter comes from thermal and phase noise; it's Gaussian and unbounded, so the longer you look the bigger the peak gets — you quote it as RMS. Deterministic jitter has a cause and a bound: data-dependent from ISI and duty-cycle distortion, periodic from things like switching regulators, and bounded uncorrelated from crosstalk. Total jitter is only meaningful at a BER: at 1e-12 it's roughly DJ plus fourteen times RJ RMS." · 한국어: RJ=가우시안·무한·RMS, DJ=원인·유한(DDJ/PJ/BUJ), TJ@1e-12 ≈ DJ + 14×RJ.
- **꼬리2. How do you prove a BER of 1e-12?** — "With zero errors you need about three over the BER bits for 95 percent confidence, so three times ten to the twelve bits — about five minutes at 10 gigabits, fifty minutes at 1 gigabit, per lane. Across PVT and many lanes that's expensive, so I combine shorter runs with margining — moving the sampling point toward the edge raises the error rate so you can measure it quickly and extrapolate with the bathtub curve." · 한국어: 3/BER 비트, 시간 계산, margining + bathtub 외삽으로 가속.
- **꼬리3. Why do fast links need equalization, and how does it affect validation?** — "Loss is frequency-dependent, so high-frequency content is attenuated more and bits smear into each other. The transmitter pre-emphasizes and the receiver uses CTLE and DFE to undo it. Link training picks those settings automatically, so in validation I sweep presets, compare margins per preset, and check that the chosen setting is near the best one across temperature." · 한국어: 주파수 의존 손실 → FFE/CTLE/DFE → preset 스윕·온도별 비교.

</details>

### 4.4 "What are process corners and how do you get them in post-silicon?"

<details>
<summary>Q → 꼬리1 (skew lot) → 꼬리2 (worst corner) → 꼬리3 (AVS)</summary>

- **Q. What are process corners?** — "Manufacturing variation means transistors on different dies run at different speeds. We label the extremes by NMOS and PMOS speed: fast-fast, typical, slow-slow, plus the skewed fast-slow and slow-fast. Slow corners limit frequency and minimum voltage; fast corners limit leakage, power and sometimes hold timing." · 한국어: FF/TT/SS(+FS/SF), SS=Fmax·Vmin, FF=누설·전력·hold.
- **꼬리1. How do you get corner parts post-silicon?** — "You ask the fab for a skew or corner lot, where process parameters are intentionally pushed toward fast and slow, and you get dies labeled by corner. You also read on-die process monitors or ring oscillators on a large sample to sort parts, and correlate those codes with measured Vmin and Fmax so production can bin parts without a full characterization." · 한국어: skew/corner lot 요청 + ring osc로 분류·상관.
- **꼬리2. Which PVT corner is the worst?** — "It depends on what you're measuring. For speed at nominal voltage it's usually slow silicon at high temperature. But at low voltage, modern processes show temperature inversion — threshold voltage rises at cold and dominates, so slow silicon at cold can set Vmin. For leakage and thermal limits it's fast silicon at hot. So I don't assume; I measure all temperatures on every corner, at least on the first lots." · 한국어: 측정 대상에 따라 다름, 저전압에선 temperature inversion으로 저온이 최악일 수 있음.
- **꼬리3. How does characterization feed AVS?** — "If Vmin correlates with the process monitor code, you can set each part's voltage based on its code: slow parts get more voltage, fast parts less, which saves power on the fast ones without risking the slow ones. Characterization gives the table — monitor code to voltage offset per operating point — and the guardband for aging and measurement error." · 한국어: Vmin ↔ ring osc 상관 → 코드별 전압 offset 표 + guardband.

</details>

### 4.5 (추가) "How do you do voltage margining safely, and how do you trust the result?"

<details>
<summary>Q → 꼬리1 (setpoint vs measured) → 꼬리2 (droop vs DC)</summary>

- **Q.** — "I margin through the PMIC's control interface, within the abs-max ratings and the sequencing rules, and I margin one rail at a time unless the test plan requires combinations. Before trusting a point I read back the regulator telemetry and verify with a DMM at the chip-side sense point, because the regulator output and the voltage at the die differ by IR drop. I also make sure the PMIC's own under- and over-voltage fault thresholds won't trip during the sweep." · 한국어: PMIC로 rail 하나씩, abs max·순서 준수, readback + DMM(칩 근처), PMIC UV/OV fault 한계 확인.
- **꼬리1. The PMIC says 0.75 V but the chip behaves like it's at 0.70.** — "That's the gap between the regulator and the die: IR drop through the board and package and dynamic droop under load. I'd measure at the sense point and at a package decoupling cap with a short ground spring while running the workload, and check whether remote sense is connected. If the static drop is the issue, it's a remote-sense or layout fix; if it's droop, it's a PDN or load-step problem." · 한국어: IR drop + droop, sense 점·패키지 cap에서 실측, remote sense 확인.
- **꼬리2. How would you tell droop from a timing bug in a random-hang case?** — "Trigger the scope on the rail dipping below a threshold and correlate with the failure timestamp; raise the voltage a step and see if the failure rate falls steeply; and lower the workload's di/dt — for example by staggering core wake-up — to see if it disappears. A logic bug usually doesn't care about a 25 millivolt bump; a PI problem does." · 한국어: 전압 트리거 scope, 전압 올리면 실패율 급감?, di/dt 낮추기 → PI vs 로직 구분.

</details>

### 4.6 (추가) "A link passes at nominal but shows correctable errors at hot. What next?"

<details>
<summary>Q → 꼬리1 → 꼬리2</summary>

- **Q.** — "Correctable errors are a margin warning, not a pass. I'd run lane margining at hot and room on that link to see which lanes lost margin, compare with a golden board to separate silicon from board, and check whether equalization was trained cold and kept hot. Then a temperature sweep on just the chip with a thermostream versus the whole board in a chamber tells me whether it's the SoC, the channel or the other end." · 한국어: correctable = margin 경고 → 고온/상온 LMR 비교 → golden 보드 → EQ 재학습 여부 → thermostream vs chamber로 분리.
- **꼬리1. Why thermostream instead of the chamber?** — "A chamber heats everything — the PMIC, the crystal, the DRAM, the retimer — so you can't tell which part lost margin. A thermostream heats only one device, so if errors appear only when the SoC is hot, it's the SoC side." · 한국어: 칩만 가열해서 원인 분리.
- **꼬리2. How do you report it?** — "Per-lane margin at each temperature, the error counts and rates, golden-board comparison, and a recommendation — for example a retraining policy, an EQ preset change, or a board layout fix — with the evidence attached." · 한국어: 레인별 margin·에러율·비교·권고.

</details>

## 5. Don 경험 연결

> **강점 카드** — SK hynix chamber 신뢰성 자동화 + shmoo. 숫자는 Don이 채울 것.

- **Card 1 (shmoo / chamber automation, 30–45 s)**: "At SK hynix I built and automated the chamber reliability test platform for high-speed interfaces — NAND and PCIe Gen3 through Gen5. A typical campaign was [숫자 채우기: N] boards across [숫자 채우기: 온도 범위, 예) −40 to 125 °C] with voltage and frequency shmoo at each temperature. The part I'm proudest of is the automation: separating setting failures from real failures, recovering hangs automatically, and rerunning suspicious points, which took a campaign from [숫자 채우기: X days] to [숫자 채우기: Y days]. That's the same discipline I'd apply to SoC and board characterization here."
- **Card 2 (hole/edge story — 하나를 준비)**: "One example: the shmoo showed [숫자 채우기: 어떤 모양 — 구멍/벽/finger] at [숫자 채우기: 조건]. Rerunning showed it was [재현됨/간헐], and the root cause turned out to be [숫자 채우기: 장비 설정 미적용 / 특정 주파수 조합 / 온도 미평형 …]. We changed [숫자 채우기: 자동화/테스트/설계] so it couldn't hide again."
- **Card 3 (Solidigm → silicon validation 연결)**: "At Solidigm I worked with the silicon validation team on performance tuning of a PCIe Gen6 SSD controller, so I've seen how characterization results turn into operating points: [숫자 채우기: 예) 주파수/전압 OPP 결정에 기여한 사례]."
- **정직 라인 (과장 금지)**: SI/PI 설계 자체(채널 시뮬레이션, PDN 설계)는 **보드/SI 엔지니어의 일** — Don은 "I'm not the SI designer, but I know how to measure it, automate it, and turn the data into a decision" 로 포지셔닝.

## 6. 실습 / 코드

- 파일: [onsitePrep/bv12/shmoo_model.py](onsitePrep/bv12/shmoo_model.py) · 실행 출력 원본: [onsitePrep/bv12/run_output.txt](onsitePrep/bv12/run_output.txt) · PNG: [onsitePrep/bv12/shmoo_tt25.png](onsitePrep/bv12/shmoo_tt25.png) (matplotlib 3.2.2가 이미 설치돼 있어서 생성. 렌더러가 markdown 이미지를 지원하지 않아 **링크로만** 둠)
- **모델일 뿐, 실리콘 데이터 아님**: 경로 지연 = alpha-power law (d ∝ V / (μ(T)·(V − Vth(T))^1.3)), Vth는 온도에 따라 −1.5 mV/°C, 이동도 ∝ T^−1.5, 코너는 Vth·지연 계수 이동, SRAM Vmin 벽, 시행마다 1.5 % 가우시안 노이즈, **주입된 구멍**(1.3 GHz × 0.725–0.75 V), 0.4 % 확률의 간헐 실패.
- 실행 (stdlib + 선택적 matplotlib, macOS python 3.9):

```bash
cd onsitePrep/bv12 && python3 -I shmoo_model.py --png shmoo_tt25.png
```

- **실제 출력** (그대로 붙임):

```text
==============================================================================
BV12 modeled shmoo  (MODEL ONLY - not silicon data)
nominal point: Vcore=0.750 V, f=1.50 GHz, corner=TT, T=25 C, 3 trials/pt
==============================================================================

[1] Shmoo: TT corner, 25 C
  legend: * = pass all 3 trials   ~ = mixed   . = fail all   [N] = nominal point
  Vcore(V) |0.8     1.0     1.2     1.4     1.6     1.8     2.0     2.2     2.4
  ---------+--------------------------------------------------------------------
   0.900   | *   *   *   *   *   *   *   *   *   *   *   *   *   *   *   *   ~
   0.875   | *   *   *   *   *   *   *   *   *   *   *   *   *   *   *   *   ~
   0.850   | *   *   *   *   *   *   *   *   *   *   *   *   *   *   *   *   .
   0.825   | *   *   *   *   *   *   *   *   *   *   *   *   *   *   *   .   .
   0.800   | *   *   *   *   *   *   *   *   *   *   *   *   ~   *   .   .   .
   0.775   | *   *   *   *   *   *   *   *   *   *   *   *   *   ~   .   .   .
   0.750   | *   *   *   *   *   .   *  [N]  *   *   *   *   ~   .   .   .   .
   0.725   | *   *   *   *   *   .   *   *   *   *   *   *   .   .   .   .   .
   0.700   | *   *   *   *   *   *   *   *   *   *   *   .   .   .   .   .   .
   0.675   | *   *   *   *   *   *   *   *   *   *   .   .   .   .   .   .   .
   0.650   | *   *   *   *   *   *   *   *   ~   .   .   .   .   .   .   .   .
   0.625   | *   *   *   *   *   *   *   ~   .   .   .   .   .   .   .   .   .
   0.600   | *   *   *   *   *   *   ~   .   .   .   .   .   .   .   .   .   .
   0.575   | .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .
   0.550   | .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .   .
            freq (GHz) ->

[2] Margin at nominal (grid resolution 25 mV x 0.1 GHz)
  Vmin @ 1.50 GHz  = 0.650 V  -> voltage margin 13.3 % (spec >= 5 %) PASS
  Fmax @ 0.750 V    = 1.9 GHz -> frequency margin 26.7 % (spec >= 10 %) PASS
  fine (noiseless model, 1 mV / continuous): Vmin@1.5GHz = 0.624 V, Fmax@0.75V = 2.000 GHz

[3] Suspicious cells (fail/mixed with solid pass on both sides)
  V=0.725 f=1.3  passes 0/3
  V=0.750 f=1.3  passes 0/3
  V=0.800 f=2.0  passes 2/3
  -> rerun each suspicious cell 20x to separate repeatable vs intermittent:
  V=0.725 f=1.3  fails 20/20  REPEATABLE -> real conditional failure, debug it
  V=0.750 f=1.3  fails 20/20  REPEATABLE -> real conditional failure, debug it
  V=0.800 f=2.0  fails  0/20  did not reproduce

[4] Automation strategy vs. the hole (column f=1.3 GHz)
  full grid      :  765 test runs, sees the hole at 0.725-0.750 V
  edge search top-down (stop at first fail):  480 runs, reports Vmin@1.3 = 0.775 V
  edge search bottom-up (stop at first pass):  348 runs, reports Vmin@1.3 = 0.6 V
  true boundary (noiseless model, no hole): Vmin@1.3 = 0.595 V
  -> top-down mistakes the hole for the edge; bottom-up never visits it.

[5] PVT corner table (noiseless model): Vmin@1.50GHz / Fmax@0.750V
  corner |   T=  -40 C           T=   25 C           T=  125 C        
    SS   |  0.702 V / 1.77 GHz   0.671 V / 1.80 GHz   0.610 V / 1.80 GHz 
    TT   |  0.664 V / 2.01 GHz   0.624 V / 2.00 GHz   0.595 V / 1.96 GHz 
    FF   |  0.627 V / 2.27 GHz   0.585 V / 2.22 GHz   0.585 V / 2.15 GHz 
  worst Vmin : 0.702 V at SS -40 C
  worst Fmax : 1.77 GHz at SS -40 C
  temperature inversion check (TT, low V = 0.65 V):  Fmax(-40C)=1.413 GHz  Fmax(125C)=1.761 GHz
  high V = 0.85 V:  Fmax(-40C)=2.515 GHz  Fmax(125C)=2.137 GHz

[6] Die-to-die statistics: 40 dies per corner, die Vth sigma = 12 mV
  metric = Vmin @ 1.50 GHz at the die's worst temperature (-40/25/125 C)
  spec: Vmin <= 0.7125 V (one-sided, USL)  ->  Cpk = (USL - mean) / (3 sigma)
  SS: mean 0.702 V  sigma 0.0131 V  min 0.674  max 0.730  Cpk 0.27 (<1.33 -> guardband / screen needed)
  TT: mean 0.661 V  sigma 0.0170 V  min 0.624  max 0.687  Cpk 1.01 (<1.33 -> guardband / screen needed)
  FF: mean 0.629 V  sigma 0.0157 V  min 0.595  max 0.652  Cpk 1.78 (>=1.33 ok)

[png] saved shmoo_tt25.png
```

- **출력 읽는 법 (면접 연습용, 블록 번호 = 출력의 [n] 머리말)**:
  - **블록 1** 정상 대각선 경계 + **0.600 V 아래 수평 벽**(SRAM Vmin 모델) + **1.3 GHz × 0.725–0.75 V 구멍**. 경계의 `~`는 노이즈 때문에 3번 중 일부만 통과한 점 — 경계는 원래 "선"이 아니라 **확률 띠**다.
  - **블록 2** 격자 해상도(25 mV)와 노이즈 때문에 격자 Vmin(0.650 V)이 노이즈 없는 실제 경계(0.624 V)보다 보수적. → "margin을 말할 때 **step 크기와 반복 횟수**도 같이 말한다".
  - **블록 3** 자동 "구멍 탐지"가 3점을 잡았고, 20회 재실행으로 **진짜 구멍 2점(20/20 재현)** 과 **노이즈 1점(0/20)** 을 갈랐다. nominal 전압 줄(0.75 V)에 구멍이 있다는 건 **1.3 GHz를 DVFS 동작점으로 쓰면 안 된다**는 뜻 — 실제라면 설계 팀 티켓.
  - **블록 4** top-down edge search는 1.3 GHz의 Vmin을 0.775 V로 **잘못 보고**(구멍을 경계로 착각), bottom-up은 0.600 V로 맞게 보고하지만 **구멍 자체를 못 봄**. full grid는 765회, edge search는 348–480회.
  - **블록 5** Vmin 최악이 **SS −40 °C**(0.702 V) — 고온이 아니라 저온. 0.65 V에서는 −40 °C Fmax(1.41 GHz) < 125 °C Fmax(1.76 GHz)이고 0.85 V에서는 반대 → **temperature inversion** 재현 (모델 파라미터 기준).
  - **블록 6** 같은 스펙(Vmin ≤ 0.7125 V)에 대해 SS는 Cpk 0.27로 불합격 → "SS bin은 nominal 전압을 올리는 **AVS**가 필요하다"는 결론으로 이어짐. TT Cpk 1.01도 1.33 미달 → guardband 재검토 또는 스크린. (FF min 0.595 = SRAM 벽에 붙음.)
- 확장 아이디어 `[미실행]`: CSV로 저장해 보드별 shmoo 겹치기(통과 확률 heatmap), edge search를 이진 탐색으로 바꿔 실행 횟수 비교, `--corner SS --temp -40` 인자 추가.

## 7. 자주 틀리는 것

1. **PASS 한 번 = 통과** — 경계 근처는 확률적이다. 점당 반복 횟수와 "n중 k"를 기록하지 않으면 shmoo가 실행할 때마다 달라진다.
2. **설정값을 축에 기록** — PMIC에 0.70 V를 썼어도 칩 쪽은 0.68 V일 수 있다(IR drop) / 쓰기가 NACK돼 안 바뀌었을 수 있다. **readback + DMM 실측값**을 기록하고, 설정 실패는 INVALID로.
3. **"최악 코너 = SS + 125 °C"라고 단정** — 저전압에서는 temperature inversion으로 저온이 최악일 수 있고, 누설/전력/hold는 FF가 최악. 모든 온도를 재 본다.
4. **Ta와 Tj 혼동** — 챔버 125 °C ≠ 칩 125 °C. 자기 발열로 Tj는 더 높고, 저전력 테스트 중이면 거의 같다. 온도 센서 실측을 기록.
5. **soak 없이 측정** — 챔버 표시 온도에 도달해도 보드·칩은 아직. 경계가 들쭉날쭉하면 soak부터 의심.
6. **jitter를 BER 없이 말함** — RJ는 무한하므로 "TJ = 50 ps"는 BER 조건 없이는 의미 없음. "TJ@1e-12".
7. **BER 1e-12를 1e12 비트로 증명** — 95 % 신뢰도엔 약 3e12 비트, 에러 0. 그리고 **모든 레인 동시에**(crosstalk).
8. **ripple 측정 설정으로 droop을 잼** — 20 MHz BW limit은 스위칭 리플 측정용. 빠른 droop은 대역폭을 열고 **칩 가까운 곳에서** 짧은 ground로.
9. **edge search만 사용** — 단조성을 가정하므로 구멍을 놓치거나 경계로 착각. 초기엔 full grid.
10. **샘플 몇 개의 평균으로 스펙 제안** — 양산은 꼬리가 결정. 분포·Cpk·코너 칩·여러 lot를 근거로, guardband(노화·측정 오차)까지 더해서.

## 8. 출처

1. Tektronix, "Evaluating Stress Components Using BER-Based Jitter Measurements" (RJ/DJ, dual-Dirac) — https://www.tektronix.com/en/documents/primer/evaluating-stress-components-using-ber-based-jitter-measurements
2. Keysight application note 5988-9740EN (TJ(BER) = 2Q·RJ + DJ, Q-scale) — https://literature.cdn.keysight.com/litweb/pdf/5988-9740EN.pdf
3. Keysight FlexDCA help, "TJ (1E-12) Result" (EW = 1 UI − TJ) — https://helpfiles.keysight.com/scopes/FlexDCA-UG/Content/Topics/Jitter-Mode/Meas-Toolbar/tj.htm
4. TestConX 2022, jitter separation fitting-range caveat — https://www.testconx.org/premium/wp-content/uploads/2022/TestConX2022s7p2Prabakaran_5234.pdf
5. JLCPCB, "Mastering PDN Impedance in High-Performance PCBs" (target impedance example) — https://jlcpcb.com/blog/pdn-impedance-high-performance-pcb
6. RF Essentials, target impedance for PDN / anti-resonance — https://rfessentials.com/rf-knowledge-base/what-is-the-target-impedance-for-the-power-distribution-network-of-a-high-speed-/
7. Linux kernel `drivers/hwmon/pmbus/pmbus.h` (PMBus command codes 0x01, 0x20, 0x21, 0x25, 0x26, 0x79, 0x7A, 0x8B–0x8D) — https://git.almalinux.org/almalinux/kernel/src/branch/a8/drivers/hwmon/pmbus/pmbus.h
8. Microchip online docs, PMBus Commands (OPERATION margin bits, vendor-specific layout) — https://onlinedocs.microchip.com/oxy/GUID-FFBF0F44-3406-43AD-8E07-7AB81C3E1B56-en-US-3/GUID-C01A8F1D-F026-4EEB-A663-B9C2D51DC945.html
9. SiTime, BER Confidence-level Calculator (3/BER for 95 %) — https://www.sitime.com/support/design-development-tools/ber-confidence-level-calculator
10. Keysight blog, "BER – Is it Bit Error Rate or Bit Error Ratio?" (confidence level) — https://www.keysight.com/blogs/en/tech/2019/03/10/ber-is-it-bit-error-rate-or-bit-error-ratio
11. pcilmr(8) man page (Lane Margining requirements, error count limit) — https://www.man7.org/linux/man-pages/man8/pcilmr.8.html
12. Microchip, PCIe RX Lane Margining (margin commands, error count) — https://onlinedocs.microchip.com/oxy/GUID-DB02CFA4-56E0-46A5-A235-B78A43EA7D67-en-US-1/GUID-8A5B63BA-731E-49E4-81B7-BCA2EA2ACD6C.html
13. Wikipedia, "Shmoo plot" (definition, origin, shmoo quality) — https://en.wikipedia.org/wiki/Shmoo_plot
14. Design & Reuse, "Understanding Shmoo Plots and Various Terminology of Testers" (wall, brick wall, floor, finger, reverse speedpath) — https://www.design-reuse.com/articles/47330/understanding-shmoo-plots-and-various-terminology-of-testers.html
15. Renesas, Temperature Ranges (AEC-Q100 grades) — https://www.renesas.com/en/support/technical-resources/temperature-ranges
16. University of Waterloo, Steiner et al., process capability paper (AIAG Cpk ≥ 1.33 / Ppk ≥ 1.67) — https://sas.uwaterloo.ca/~shsteine/papers/cap.pdf
17. Power Integrations, SCALE-iDriver AEC-Q100 qualification report (3 lots × 77, HTOL 1000 h) — https://motor-driver.power.com/sites/default/files/documents/scale-idriver_sic11xxkq_aec-q100_qualification_report.pdf
18. Linux kernel docs, hwmon sysfs interface (in*_input mV, curr*_input mA, temp*_input m°C) — https://www.kernel.org/doc/html/latest/hwmon/sysfs-interface.html
