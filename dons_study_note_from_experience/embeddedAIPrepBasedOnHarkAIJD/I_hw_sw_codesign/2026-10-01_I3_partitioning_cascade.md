# I3. 연산 분할과 Cascade 설계 — 단계, 임계값, 오프로드를 함께 최적화하기

> **이 노트를 다 읽으면**: wake cascade를 "검출기 여러 개로 된 시스템"으로 보고 단계별 (전력, 지연, ROC)에서 전체 miss·FA/day·평균 전력을 계산할 수 있다 · 단계 사이의 오류 상관이 왜 독립 가정의 "FPR 곱"을 수십 배 틀리게 만드는지 설명하고, 상관을 넣은 모델로 임계값 θ1·θ2·θ3를 **함께** 최적화하는 재사용 가능한 optimizer를 짤 수 있다 · 모델 하나를 MCU/DSP/NPU/cloud에 나누는 split 지점을 층별 MAC·activation 크기와 링크 비용으로 고르고, bottleneck이 언제 이기는지 말할 수 있다 · 기기 vs 클라우드 라우팅 정책을 하루치 시뮬레이션으로 비교하고, IMU 경로와 음성 경로가 엔진을 나눠 쓸 때의 스케줄링·테스트·필드 재튜닝 문제까지 설계 문서 수준으로 설명할 수 있다
> **JD 연결**: "Co-design model architectures that meet latency, memory, power, bandwidth", "hybrid edge-LLM", "Profile and optimize memory usage, power consumption, real-time performance" — study_prep_list **I3**: MCU → DSP → NPU → Cloud 단계별 역할 / wake cascade (VAD → KWS → ASR → LLM) / false wake 비용과 전력. 함께 닿는 행: **L3**(hybrid 라우팅), **L4**(음성 파이프라인 지연 예산), **I1**(예산), **I4**(전처리 배치), **J2**(실시간)
> **Don 기준 난이도**: 전력 상태별 평균 전류 계산, spurious wake 비용, 인터럽트 우선순위·deadline, 양산 불량률 상한 통계는 이미 안다 / 새로 배울 것은 "임계값을 여러 개 동시에 고르는 최적화 문제"로 cascade를 보는 법, 상관된 검출기의 확률 계산, 모델을 층 단위로 엔진에 나누는 비용 모델, 기기·클라우드 라우팅 정책 평가
> **선행 노트**: A2 9절(Bayes로 본 false wake), A4 9절(FAR·FRR·DET, 72시간 규칙), B5 3.4–3.6절·8절(KWS streaming, 2단 검증, false wake 전력), B9 4–5절(early-exit, cascade 기대 비용 공식, SLM→cloud 시뮬레이션), D7 6.3절(바이트당 무선 에너지, 오프로드 교차점), E8 4–5절(워크로드 배치표, wake cascade 이산 사건 시뮬레이션), E9 3.5·4절(전이 에너지, deep sleep 손익분기). 이 노트는 그 결과를 **다시 계산하지 않고** 가져다 쓴다.

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 이미 본 것과 이 노트가 새로 하는 것

앞 노트들에서 cascade를 여러 번 만났다. 정리하면 이렇다.

| 노트 | 이미 다룬 것 | I3에서 쓰는 방식 |
|---|---|---|
| A2 9절 | Bayes: 사전확률이 낮으면 "99% 정확한 KWS"도 false wake가 대부분 | negative 창이 압도적으로 많다는 전제 |
| A4 9절 | FAR(FA/hour), FRR, DET, "0회 관찰 → 72시간" 규칙 | 8.4절 cascade 전체 FAR 측정 계획 |
| B5 8절, B9 5절 | `E[cost] = c1 + p·c2`, 검증 단계 하나로 4.7배 절약 | 1절 공식의 출발점 |
| D7 6.3절 | 무선 바이트당 에너지, 고정비, 기기 vs 클라우드 교차점 | 5절 split 비용의 링크 항 |
| E8 4–5절 | 엔진별 배치표, 전원 상태 있는 24시간 cascade 시뮬레이터 | "주어진 임계값"에서의 전력 → 여기서는 임계값 자체를 고른다 |
| E9 3.5·4절 | power gating 전이 에너지, deep sleep 손익분기 | 단계 진입 비용 `E_next`에 포함 |

앞 노트들은 **임계값이 이미 정해진** cascade의 비용을 계산했다. 실제 설계 회의에서 나오는 질문은 그 반대 방향이다.

- "1단 임계값을 얼마로 해야 하나? 2단은? 둘을 따로 정해도 되나?"
- "DSP 검증기를 더 크게(정확하게) 만들까, 더 싸게 만들까?"
- "3단(AP 검증)을 넣을 가치가 있나?"
- "이 모델을 MCU와 NPU에 나눠 돌린다면 어디서 자르나?"
- "이 질의는 기기에서 답할까, 클라우드로 보낼까?"

이 질문들은 모두 같은 모양이다. **제약(정확도·지연·프라이버시) 아래에서 비용(전력·지연)을 최소화하는 변수(임계값·경계·경로)를 고른다.** I3는 이것을 최적화 문제로 쓰고, 다시 쓸 수 있는 코드로 푼다.

Don에게 익숙한 말로 하면: SSD에서 "GC를 언제 시작할지(임계값), 어느 코어에서 돌릴지(배치), host I/O 지연 SLA를 지키면서 전력을 최소로" 정하던 것과 같은 문제다. 다른 점은 검출기의 오류가 **확률적**이라서 기댓값과 꼬리 확률로 이야기해야 한다는 것이다.

### 0.2 공통 사례 (가정)

이 노트 전체에서 쓰는 가상의 기기다. 숫자는 모두 설명용 가정이며, 실제 Hark 제품의 사양이 아니다.

- 예를 들어 Hark 같은 음성 + 제스처 웨어러블이라면: **MCU**(always-on, VAD + KWS 1단), **오디오 DSP**(KWS 검증 2단), **앱 SoC의 AP/NPU**(ASR, SLM), **cloud LLM**.
- 배터리 300 mAh × 3.85 V = 1.155 Wh = **4,158 J**.
- 하루에 1단이 판정하는 "negative 창": 말소리·소음이 있는 시간이 하루의 30%라고 보고 1초에 1창 → `86,400 × 0.3 = 25,920 창/day`. 진짜 호출은 하루 20회.
- 목표: **miss(놓침) ≤ 5%**, **사용자에게 보이는 false wake ≤ 1회/day**.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 330">
<text x="340" y="20" font-size="14" text-anchor="middle">음성 + 제스처 웨어러블의 cascade (가정) — 오른쪽으로 갈수록 비싸고 드물다</text> <rect x="10" y="60" width="60" height="60" rx="6" fill="none" stroke="#888" stroke-width="2"/> <text x="40" y="95" font-size="13" text-anchor="middle">mic</text> <rect x="95" y="45" width="135" height="90" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="162" y="66" font-size="13" text-anchor="middle">1단 MCU</text> <text x="162" y="84" font-size="12" text-anchor="middle">VAD + KWS</text> <text x="162" y="102" font-size="12" text-anchor="middle">항상 1.0 mW</text>
<text x="162" y="120" font-size="12" text-anchor="middle">d' = 3.5</text> <rect x="270" y="45" width="120" height="90" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="330" y="66" font-size="13" text-anchor="middle">2단 DSP 검증</text> <text x="330" y="84" font-size="12" text-anchor="middle">15 mJ / 회</text> <text x="330" y="102" font-size="12" text-anchor="middle">0.4 s</text> <text x="330" y="120" font-size="12" text-anchor="middle">d' = 6</text> <rect x="430" y="45" width="130" height="90" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="495" y="66" font-size="13" text-anchor="middle">3단 AP / NPU</text> <text x="495" y="84" font-size="12" text-anchor="middle">ASR 검증 0.7 J</text> <text x="495" y="102" font-size="12" text-anchor="middle">통과 시 세션 2.3 J</text> <text x="495" y="120" font-size="12" text-anchor="middle">d' = 7</text> <rect x="595" y="60" width="78" height="60" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="634" y="87" font-size="13" text-anchor="middle">cloud</text> <text x="634" y="104" font-size="12" text-anchor="middle">LLM</text>
<line x1="70" y1="90" x2="95" y2="90" stroke="currentColor"/> <line x1="230" y1="90" x2="270" y2="90" stroke="currentColor"/><polygon points="270,90 262,85 262,95" fill="currentColor"/> <line x1="390" y1="90" x2="430" y2="90" stroke="currentColor"/><polygon points="430,90 422,85 422,95" fill="currentColor"/> <line x1="560" y1="90" x2="595" y2="90" stroke="currentColor" stroke-dasharray="4 3"/><polygon points="595,90 587,85 587,95" fill="currentColor"/> <text x="162" y="152" font-size="12" text-anchor="middle">negative 25,920 창/day</text> <text x="250" y="152" font-size="12" text-anchor="middle">936/day</text> <text x="410" y="152" font-size="12" text-anchor="middle">2.3/day</text>
<text x="563" y="56" font-size="12">L3 라우팅</text> <text x="505" y="152" font-size="12">false 0.06/day</text> <rect x="10" y="215" width="60" height="55" rx="6" fill="none" stroke="#888" stroke-width="2"/> <text x="40" y="247" font-size="13" text-anchor="middle">IMU</text> <rect x="95" y="210" width="135" height="65" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="5 3"/> <text x="162" y="235" font-size="12" text-anchor="middle">센서 내장 WoM</text> <text x="162" y="255" font-size="12" text-anchor="middle">wake-on-motion, µW</text>
<rect x="270" y="210" width="120" height="65" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="330" y="235" font-size="12" text-anchor="middle">MCU 제스처 분류</text> <text x="330" y="255" font-size="12" text-anchor="middle">30 ms, KWS와 공유</text> <line x1="70" y1="242" x2="95" y2="242" stroke="currentColor"/> <line x1="230" y1="242" x2="270" y2="242" stroke="currentColor"/><polygon points="270,242 262,237 262,247" fill="currentColor"/> <line x1="390" y1="242" x2="495" y2="242" stroke="currentColor"/> <line x1="495" y1="242" x2="495" y2="135" stroke="currentColor"/><polygon points="495,135 490,143 500,143" fill="currentColor"/>
<text x="505" y="200" font-size="12">동작 실행도 AP를 깨운다</text> <text x="162" y="300" font-size="12" text-anchor="middle">같은 MCU 위의 두 cascade → 7절 스케줄링</text> <text x="340" y="322" font-size="12" text-anchor="middle">숫자: 3절 최적화 결과(θ = 1.80 / 3.61 / 3.77)에서 하루 동안 각 단에 들어가는 negative 창 수</text>
</svg>
```

그림 1 — 이 노트의 공통 사례. 위는 음성 cascade, 아래는 제스처 cascade다. 각 단계는 (켜졌을 때의 비용, 지연, 검출 품질 d')로 요약된다. 화살표 아래 숫자는 3절에서 최적화한 임계값으로 하루 동안 그 단계에 도달하는 negative(호출어가 아닌) 창의 수다.

### 0.3 I3의 세 가지 질문

1. **Cascade 임계값** (1–4절): 단계가 정해졌을 때 θ를 어떻게 함께 고르나? 단계를 몇 개 둘까?
2. **모델 분할** (5절): 모델 하나를 엔진 둘에 나눌 때 어디서 자르나?
3. **경로 선택** (6절): 질의 하나를 기기에서 처리할까, 클라우드로 보낼까?

7–8절은 이 셋이 실제 펌웨어에서 부딪히는 문제(엔진 공유, hysteresis, pre-roll, 테스트, 필드 재튜닝)다.

---

## 1. Cascade를 검출기 시스템으로 보기

### 1.1 직관 — 공항 보안 검색대

공항 검색을 떠올리자. 1단은 금속 탐지기(싸고 빠르고 자주 울린다). 울린 사람만 2단 몸 수색(비싸고 느리다)을 받는다. 2단에서도 걸리면 3단 정밀 조사. 설계자가 정하는 것은 **각 단의 민감도(임계값)** 다. 1단 감도를 올리면 위험물을 덜 놓치지만 2단 인력이 더 필요하다. 1단 감도를 내리면 인력은 줄지만 놓친 것은 뒤에서 되살릴 수 없다.

wake cascade가 정확히 이 구조다. 단계 k는 세 숫자로 요약된다.

| 단계 속성 | 기호 | 예 (2단 DSP) | 어디서 오나 |
|---|---|---|---|
| 켜질 때 드는 에너지 | `E_next,k` (앞 단계 통과 시 지불) | 15 mJ ≈ 깨우기 2 mJ + 25 mW × 0.5 s (검증 + tail) | E9 3.5절 전이 에너지 + D7 추론 에너지 |
| 지연 | `L_k` | 깨우기 5 ms + 판정 0.4 s | D6, E8 |
| 검출 품질 | `TPR_k(θ_k)`, `FPR_k(θ_k)` | ROC 곡선 하나 | A4 8절 |

### 1.2 정의 — 전체 성능과 평균 전력

단계가 K개, 각 단 임계값이 θ_k일 때 (y = 1이면 진짜 호출어 창, y = 0이면 negative 창):

```
전체 TPR       = P(1단 통과, 2단 통과, …, K단 통과 | y = 1)
전체 miss      = 1 − 전체 TPR
FA/day         = N_neg × P(1..K단 모두 통과 | y = 0)
평균 전력      = P_always + (1 / 86,400 s) × Σ_k  N_neg × P(1..k단 모두 통과 | y = 0) × E_next,k

독립이라면     P(1..k 통과 | y) = Π_{j≤k} P(j단 통과 | y)   → TPR = Π TPR_k,  FPR = Π FPR_k
```

말로 하면: 전체 TPR과 FPR은 **모든 단을 통과할 확률**이다. 단계들이 서로 독립이면 곱이 되지만, 2절에서 보듯 실제로는 독립이 아니다. 평균 전력은 "항상 켜진 1단 전력"에 "k단을 통과한 창 수 × 다음 단계를 켜는 비용"을 더한 것이다. `E_next,k`에는 계산 에너지뿐 아니라 **전이 에너지**(power domain을 켜고, 클럭을 세우고, retention 상태를 복원하는 비용 — E9 3.5절)와 일을 마친 뒤 잠들기 전까지의 tail이 포함된다.

두 가지를 의도적으로 뺐다.

- **진짜 호출에 쓰는 에너지**: 사용자가 실제로 부른 20회의 세션 비용은 어느 설계든 거의 같은 "해야 하는 일"이다. 이것을 넣으면 최적화기가 "많이 놓칠수록 전력이 준다"는 엉뚱한 방향을 좋아하게 된다. 그래서 negative에 쓰인 **헛일**만 센다.
- **지연**: 진짜 호출의 지연은 `Σ L_k`로 거의 고정이다. 임계값이 아니라 단계 구조로 정해지므로 제약으로 따로 확인한다(8.2절).

### 1.3 손으로 계산 — 독립 가정 3단 cascade

각 단의 **조건부** 비율(앞 단을 통과한 창 중에서 이번 단도 통과하는 비율)을 가정한다. 1단 FPR 2%, 2단 2%, 3단 5%. TPR은 0.97, 0.99, 0.99.

```
1단 통과 negative = 25,920 × 0.02  = 518.4 /day   → DSP 15 mJ   × 518.4  = 7.78 J
2단 통과 negative = 518.4  × 0.02  = 10.37 /day   → AP 0.7 J    × 10.37  = 7.26 J
3단 통과 negative = 10.37  × 0.05  = 0.518 /day   → 세션 2.3 J  × 0.518  = 1.19 J
always-on         = 1 mW × 86,400 s                                     = 86.4 J
합계                                                                    = 102.6 J/day
평균 전력 = 102.6 J / 86,400 s = 1.188 mW
TPR = 0.97 × 0.99 × 0.99 = 0.9507  → miss 4.93 %
배터리 4,158 J / 102.6 J/day ≈ 40.5 일 (듣기만 할 때)
```

말로 하면: 이 설계에서 **헛일은 하루 16.2 J**로 always-on 86.4 J의 약 19%다. DSP 깨우기(7.8 J)와 AP 깨우기(7.3 J)가 비슷한 몫이다. 1단 FPR을 2% → 4%로 풀면 DSP 몫이 두 배가 되고, 3단 이후는 독립 가정에서 비율대로 따라 늘어난다.

### 1.4 코드로 확인 — 계산기

위 손계산을 그대로 코드로 옮긴다. 1.3절의 모든 숫자가 다시 나오는지 본다.

```python
# 3단 cascade의 하루 에너지·FA·miss 계산기 — 1.3절 손계산과 같은 숫자
N_NEG = 25_920                     # 하루 negative 판정창 (말소리·소음이 있는 시간 30%, 1초에 1창; 가정)
P_BASE_MW = 1.0                    # mic + VAD + MCU KWS, 항상 켜짐
stages = [  # (이름, 이 단을 통과하면 드는 에너지 mJ, 조건부 FPR, 조건부 TPR)  — 독립 가정
    ("MCU KWS",        15.0, 0.02, 0.97),   # 통과 → DSP 깨워 검증 (15 mJ)
    ("DSP verify",    700.0, 0.02, 0.99),   # 통과 → AP 깨워 ASR 검증 (0.7 J)
    ("AP ASR-verify", 2300.0, 0.05, 0.99),  # 통과 → 세션 나머지 (2.3 J) = 사용자에게 보이는 wake
]
e_day, neg, tpr = P_BASE_MW * 86_400, float(N_NEG), 1.0      # mJ/day
for name, e_pass, fpr, t in stages:
    neg, tpr = neg * fpr, tpr * t
    e_day += e_pass * neg
    print(f"{name:14s} 통과 neg {neg:9.3f}/day → 다음 비용 {e_pass * neg / 1000:6.3f} J/day")
print(f"하루 {e_day / 1000:.2f} J = 평균 {e_day / 86_400:.3f} mW,  FA {neg:.3f}/day,  miss {1 - tpr:.4f}")
batt_j = 0.300 * 3.85 * 3600       # 300 mAh × 3.85 V
print(f"배터리 {batt_j:.0f} J → 듣기만 하면 {batt_j / (e_day / 1000):.1f} 일")
```

```text
MCU KWS        통과 neg   518.400/day → 다음 비용  7.776 J/day
DSP verify     통과 neg    10.368/day → 다음 비용  7.258 J/day
AP ASR-verify  통과 neg     0.518/day → 다음 비용  1.192 J/day
하루 102.63 J = 평균 1.188 mW,  FA 0.518/day,  miss 0.0493
배터리 4158 J → 듣기만 하면 40.5 일
```

출력에서 볼 것: 손계산과 소수점까지 같다. 이 계산기의 약점은 "조건부 FPR 2%"를 **어디서 얻느냐**다. 2단의 FPR을 따로 측정하면 보통 "모든 negative에 대한 FPR"을 얻는다. 그러나 2단에 실제로 들어오는 창은 1단이 이미 "호출어 같다"고 고른 어려운 창이다. 그 창들에서 2단 FPR은 훨씬 높다. 이것이 2절의 주제다.

---

## 2. 점수 분포와 상관 — 왜 독립 가정이 위험한가

### 2.1 직관 — 같은 소리에 같이 속는다

"Hey Hark"(가정한 호출어)와 발음이 비슷한 "Hey Mark", TV 광고에서 나오는 진짜 호출어, 특정 사용자의 억양. 이런 소리는 1단 KWS만 속이는 게 아니라 **2단도 속인다**. 이유는 구조적이다.

- 두 단계가 **같은 front-end**(log-mel)를 본다. 특징 공간에서 가까운 소리는 둘 다에게 가깝다.
- 2단이 1단과 **같은 데이터**로, 혹은 같은 teacher에서 distillation(C5)으로 학습됐다.
- 같은 마이크·같은 잡음 환경에서 같은 순간의 오디오를 본다.

반대로 진짜 호출어도 상관된다. 또렷하게 말한 호출어는 모든 단계가 함께 통과시키고, 멀리서 웅얼거린 호출어는 함께 놓친다.

### 2.2 모델 — 공유 요인 하나

창마다 숨은 요인 z 하나를 두고 각 단계 점수를 이렇게 만든다.

```
s_k = μ_k · y + √ρ · z + √(1 − ρ) · e_k
z, e_1, e_2, … ~ N(0, 1) 서로 독립,   y ∈ {0 (negative), 1 (호출어)}
```

말로 하면: 점수는 "진짜 신호(μ_k·y)" + "모든 단계가 공유하는 이 창의 성질(z)" + "단계마다 따로인 잡음(e_k)"이다. ρ는 두 단계 점수의 상관계수다(A2 5절). 중요한 성질 두 개:

- **각 단계의 분포는 ρ와 무관하다.** s_k는 negative면 N(0, 1), 호출어면 N(μ_k, 1)이다(분산 = ρ + (1 − ρ) = 1). 그러니 μ_k가 곧 A4의 d'(분리도)이고, 단계 하나씩 따로 그린 ROC는 ρ가 0이든 0.9든 **똑같다**. 상관은 단계별 평가에서 **보이지 않는다**.
- **z가 주어지면 단계들은 독립이다.** 이 조건부 독립 덕분에 "모두 통과할 확률"을 z 하나에 대한 1차원 적분으로 계산할 수 있다(3.2절).

### 2.3 손으로 계산 — ρ = 0일 때와 극단

각 단의 단독 FPR을 1%로 맞추는 임계값은 θ = 2.326(표준정규 상위 1% 지점)이다.

```
ρ = 0 (독립)       : P(둘 다 통과) = 0.01 × 0.01 = 1 × 10⁻⁴
ρ = 1 (완전 상관)  : s1 = s2 이므로 P(둘 다 통과) = P(s1 > θ) = 1 × 10⁻²   ← 2단이 아무것도 거르지 못함
```

말로 하면: 상관이 0에서 1로 가면 2단의 거름 효과는 100배에서 1배로 줄어든다. 중간 값은 코드로 본다.

### 2.4 코드로 확인 — 상관이 joint FPR을 얼마나 키우나

두 단계 모두 단독 FPR 1%인 상황에서 ρ만 바꿔 400만 개의 negative 창을 뽑아 센다.

```python
# 두 단계가 같은 소리에 같이 속으면(상관), 독립 가정의 "FPR 곱"이 얼마나 틀리나
import numpy as np
from scipy.stats import norm
rng = np.random.default_rng(0)
N, th = 4_000_000, norm.isf(0.01)                  # 각 단 단독 FPR이 1%가 되는 임계값 2.326
for rho in (0.0, 0.3, 0.5, 0.8, 0.95):
    z = rng.standard_normal(N)                     # 공유 요인: 이 소리가 얼마나 "호출어 같은가"
    s1 = np.sqrt(rho) * z + np.sqrt(1 - rho) * rng.standard_normal(N)
    s2 = np.sqrt(rho) * z + np.sqrt(1 - rho) * rng.standard_normal(N)
    p1, p12 = (s1 > th).mean(), ((s1 > th) & (s2 > th)).mean()
    print(f"ρ={rho:4.2f}  FPR1={p1:.4f}  FPR2|1통과={p12 / p1:.3f}  "
          f"joint={p12:.2e}  독립가정 1e-04의 {p12 / 1e-4:5.1f}배")
```

```text
ρ=0.00  FPR1=0.0100  FPR2|1통과=0.010  joint=9.80e-05  독립가정 1e-04의   1.0배
ρ=0.30  FPR1=0.0101  FPR2|1통과=0.055  joint=5.57e-04  독립가정 1e-04의   5.6배
ρ=0.50  FPR1=0.0101  FPR2|1통과=0.129  joint=1.31e-03  독립가정 1e-04의  13.1배
ρ=0.80  FPR1=0.0100  FPR2|1통과=0.377  joint=3.77e-03  독립가정 1e-04의  37.7배
ρ=0.95  FPR1=0.0101  FPR2|1통과=0.661  joint=6.65e-03  독립가정 1e-04의  66.5배
```

출력에서 볼 것:

- `FPR1`은 모든 ρ에서 1%다. 단계 하나만 보면 차이가 없다.
- 그러나 "1단을 통과한 창 중 2단도 통과하는 비율"(`FPR2|1통과`)은 ρ = 0.5에서 12.9%, ρ = 0.8에서 37.7%다. 2단을 따로 평가했을 때의 1%와 전혀 다르다.
- joint FPR은 ρ = 0.5에서 독립 가정의 **13배**, ρ = 0.8에서 38배다. 하루 1회로 설계한 FA가 필드에서 하루 13회가 될 수 있다는 뜻이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 360">
<text x="340" y="20" font-size="14" text-anchor="middle">각 단 FPR 1%일 때 실제 joint FPR — Monte Carlo 400만 창 (예제 2)</text> <line x1="80" y1="280" x2="620" y2="280" stroke="currentColor"/> <line x1="80" y1="280" x2="80" y2="40" stroke="currentColor"/> <line x1="76" y1="280.0" x2="80" y2="280.0" stroke="currentColor"/><text x="72" y="284.0" font-size="12" text-anchor="end">1e-5</text> <line x1="76" y1="220.0" x2="80" y2="220.0" stroke="currentColor"/><text x="72" y="224.0" font-size="12" text-anchor="end">1e-4</text> <line x1="76" y1="160.0" x2="80" y2="160.0" stroke="currentColor"/><text x="72" y="164.0" font-size="12" text-anchor="end">1e-3</text> <line x1="76" y1="100.0" x2="80" y2="100.0" stroke="currentColor"/><text x="72" y="104.0" font-size="12" text-anchor="end">1e-2</text>
<line x1="76" y1="40.0" x2="80" y2="40.0" stroke="currentColor"/><text x="72" y="44.0" font-size="12" text-anchor="end">1e-1</text> <text x="350" y="318" font-size="13" text-anchor="middle">두 단계 점수 상관 ρ (negative끼리)</text> <text x="18" y="160" font-size="13" text-anchor="middle" transform="rotate(-90 18 160)">두 단계 모두 통과할 확률</text> <rect x="101.6" y="220.5" width="64.8" height="59.5" fill="#e08a3c" fill-opacity="0.75"/> <text x="134.0" y="298.0" font-size="12" text-anchor="middle">ρ=0.0</text> <text x="134.0" y="214.5" font-size="12" text-anchor="middle">1.0배</text> <rect x="209.6" y="175.2" width="64.8" height="104.8" fill="#e08a3c" fill-opacity="0.75"/>
<text x="242.0" y="298.0" font-size="12" text-anchor="middle">ρ=0.3</text> <text x="242.0" y="169.2" font-size="12" text-anchor="middle">5.6배</text> <rect x="317.6" y="153.0" width="64.8" height="127.0" fill="#e08a3c" fill-opacity="0.75"/> <text x="350.0" y="298.0" font-size="12" text-anchor="middle">ρ=0.5</text> <text x="350.0" y="147.0" font-size="12" text-anchor="middle">13.1배</text> <rect x="425.6" y="125.4" width="64.8" height="154.6" fill="#e08a3c" fill-opacity="0.75"/> <text x="458.0" y="298.0" font-size="12" text-anchor="middle">ρ=0.8</text>
<text x="458.0" y="119.4" font-size="12" text-anchor="middle">37.7배</text> <rect x="533.6" y="110.6" width="64.8" height="169.4" fill="#e08a3c" fill-opacity="0.75"/> <text x="566.0" y="298.0" font-size="12" text-anchor="middle">ρ=0.95</text> <text x="566.0" y="104.6" font-size="12" text-anchor="middle">66.5배</text> <line x1="80" y1="220.0" x2="620" y2="220.0" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="6 4"/> <text x="90.0" y="54.0" font-size="12" text-anchor="start">파란 점선 = 독립 가정 0.01 × 0.01 = 1e-4</text>
</svg>
```

그림 2 — 두 단계가 단독으로는 똑같이 FPR 1%인데, 점수 상관 ρ가 커질수록 "둘 다 통과"하는 확률이 독립 가정(파란 점선)보다 커진다. 막대 위 숫자가 배수다.

### 2.5 단계별 ROC — 이번 사례의 세 검출기

이 노트의 세 단계는 negative N(0, 1), 호출어 N(d', 1)인 binormal 모델로 둔다(d' = 3.5, 6, 7). 임계값 θ에서 `FPR = Q(θ)`, `TPR = Q(θ − d')`이고 Q는 표준정규의 오른쪽 꼬리 면적이다. 표로 보면 같은 FPR에서 단계마다 TPR이 얼마나 다른지 보인다.

```python
# 단계별 ROC(binormal): 같은 FPR에서 d'가 클수록 TPR이 높다 — 임계값 θ로 환산
import numpy as np
from scipy.stats import norm
STAGES = {"MCU KWS": 3.5, "DSP verify": 6.0, "AP ASR-verify": 7.0}   # negative N(0,1), positive N(d',1)
print(f"{'FPR':>8s}{'θ':>7s}" + "".join(f"{n + ' TPR':>18s}" for n in STAGES))
for fpr in (1e-1, 1e-2, 1e-3, 1e-4, 1e-5, 1e-6):
    th = norm.isf(fpr)                              # negative 꼬리 면적이 fpr인 지점
    print(f"{fpr:8.0e}{th:7.3f}" + "".join(f"{norm.sf(th - d):18.4f}" for d in STAGES.values()))
for n, d in STAGES.items():
    print(f"{n:14s} d'={d}  AUC = Φ(d'/√2) = {norm.cdf(d / np.sqrt(2)):.6f}")
```

```text
     FPR      θ       MCU KWS TPR    DSP verify TPR AP ASR-verify TPR
   1e-01  1.282            0.9867            1.0000            1.0000
   1e-02  2.326            0.8797            0.9999            1.0000
   1e-03  3.090            0.6590            0.9982            1.0000
   1e-04  3.719            0.4133            0.9887            0.9995
   1e-05  4.265            0.2222            0.9586            0.9969
   1e-06  4.753            0.1050            0.8937            0.9877
MCU KWS        d'=3.5  AUC = Φ(d'/√2) = 0.993336
DSP verify     d'=6.0  AUC = Φ(d'/√2) = 0.999989
AP ASR-verify  d'=7.0  AUC = Φ(d'/√2) = 1.000000
```

출력에서 볼 것:

- MCU KWS(d' = 3.5)는 FPR 10⁻⁴에서 TPR이 41%밖에 안 된다. **1단 혼자서는 하루 1회 FA 목표를 맞출 수 없다**(FA/day 1이려면 창당 FPR ≈ 3.9 × 10⁻⁵).
- DSP 검증기(d' = 6)는 FPR 10⁻⁴에서도 TPR 98.9%다. 대신 매번 15 mJ가 든다.
- AUC는 셋 다 0.99 이상이다. **AUC는 cascade 설계에 거의 쓸모가 없다** — 우리가 사는 곳은 FPR 10⁻⁴ ~ 10⁻⁶의 꼬리이고, AUC는 그 영역을 거의 반영하지 않는다(A4 8절).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 360">
<text x="340" y="20" font-size="14" text-anchor="middle">단계별 DET 곡선 — 아래·왼쪽일수록 좋은 검출기 (예제 3과 같은 binormal 모델)</text> <line x1="70" y1="290" x2="610" y2="290" stroke="currentColor"/> <line x1="70" y1="290" x2="70" y2="40" stroke="currentColor"/> <line x1="70.0" y1="290" x2="70.0" y2="294" stroke="currentColor"/><text x="70.0" y="308" font-size="12" text-anchor="middle">1e-6</text> <line x1="178.0" y1="290" x2="178.0" y2="294" stroke="currentColor"/><text x="178.0" y="308" font-size="12" text-anchor="middle">1e-5</text> <line x1="286.0" y1="290" x2="286.0" y2="294" stroke="currentColor"/><text x="286.0" y="308" font-size="12" text-anchor="middle">1e-4</text> <line x1="394.0" y1="290" x2="394.0" y2="294" stroke="currentColor"/><text x="394.0" y="308" font-size="12" text-anchor="middle">1e-3</text>
<line x1="502.0" y1="290" x2="502.0" y2="294" stroke="currentColor"/><text x="502.0" y="308" font-size="12" text-anchor="middle">1e-2</text> <line x1="610.0" y1="290" x2="610.0" y2="294" stroke="currentColor"/><text x="610.0" y="308" font-size="12" text-anchor="middle">1e-1</text> <line x1="66" y1="290.0" x2="70" y2="290.0" stroke="currentColor"/><text x="62" y="294.0" font-size="12" text-anchor="end">0.1%</text> <line x1="66" y1="206.7" x2="70" y2="206.7" stroke="currentColor"/><text x="62" y="210.7" font-size="12" text-anchor="end">1%</text> <line x1="66" y1="123.3" x2="70" y2="123.3" stroke="currentColor"/><text x="62" y="127.3" font-size="12" text-anchor="end">10%</text> <line x1="66" y1="40.0" x2="70" y2="40.0" stroke="currentColor"/><text x="62" y="44.0" font-size="12" text-anchor="end">100%</text> <text x="340" y="328" font-size="13" text-anchor="middle">단계 단독 FPR (negative 창이 통과할 확률, 로그)</text>
<text x="18" y="165" font-size="13" text-anchor="middle" transform="rotate(-90 18 165)">단계 단독 miss = 1 − TPR (로그)</text> <polyline points="70.0,44.0 83.5,44.5 97.0,45.0 110.5,45.5 124.0,46.1 137.5,46.7 151.0,47.5 164.5,48.2 178.0,49.1 191.5,50.0 205.0,51.0 218.5,52.1 232.0,53.3 245.5,54.7 259.0,56.1 272.5,57.6 286.0,59.3 299.5,61.1 313.0,63.1 326.5,65.2 340.0,67.6 353.5,70.1 367.0,72.8 380.5,75.7 394.0,78.9 407.5,82.4 421.0,86.2 434.5,90.2 448.0,94.7 461.5,99.5 475.0,104.7 488.5,110.4 502.0,116.7 515.5,123.5 529.0,130.9 542.5,139.2 556.0,148.2 569.5,158.3 583.0,169.6 596.5,182.2 610.0,196.4" fill="none" stroke="#3f9a6b" stroke-width="2.5"/> <polyline points="70.0,121.1 83.5,124.8 97.0,128.7 110.5,132.7 124.0,136.9 137.5,141.2 151.0,145.7 164.5,150.4 178.0,155.3 191.5,160.4 205.0,165.7 218.5,171.2 232.0,176.9 245.5,182.9 259.0,189.1 272.5,195.6 286.0,202.3 299.5,209.4 313.0,216.7 326.5,224.4 340.0,232.5 353.5,240.9 367.0,249.7 380.5,258.9 394.0,268.6 407.5,278.7 421.0,289.4" fill="none" stroke="#4a7bd0" stroke-width="2.5"/> <polyline points="70.0,199.1 83.5,204.6 97.0,210.3 110.5,216.3 124.0,222.4 137.5,228.7 151.0,235.2 164.5,241.9 178.0,248.8 191.5,256.0 205.0,263.5 218.5,271.1 232.0,279.1 245.5,287.3" fill="none" stroke="#e08a3c" stroke-width="2.5"/> <line x1="480" y1="228" x2="505" y2="228" stroke="#3f9a6b" stroke-width="3"/> <text x="512.0" y="232.0" font-size="12" text-anchor="start">MCU KWS d'=3.5</text> <line x1="480" y1="246" x2="505" y2="246" stroke="#4a7bd0" stroke-width="3"/>
<text x="512.0" y="250.0" font-size="12" text-anchor="start">DSP verify d'=6</text> <line x1="480" y1="264" x2="505" y2="264" stroke="#e08a3c" stroke-width="3"/> <text x="512.0" y="268.0" font-size="12" text-anchor="start">AP ASR-verify d'=7</text>
</svg>
```

그림 3 — 세 단계의 DET 곡선(로그-로그). 아래·왼쪽에 있을수록 좋다. MCU KWS는 FPR이 작은 쪽에서 miss가 급격히 커진다. 그래서 1단은 "꼬리"가 아니라 FPR 1–5% 근처에서 일하고, 꼬리는 뒤 단계가 맡는다.

### 2.6 실무에서 상관을 줄이는 법

- **다른 정보를 보는 2단**: 더 긴 문맥(호출어 뒤 0.5초까지), 다른 특징(더 높은 해상도의 mel, 화자 임베딩), 다른 구조(1단이 CNN이면 2단은 attention).
- **hard negative로 2단을 학습**: 1단이 실제로 통과시킨 창(필드 또는 녹음 데이터의 1단 trigger)을 2단의 negative 학습 데이터로 쓴다. 2단이 "1단이 헷갈리는 소리"를 전문으로 배우게 된다(H8 7절).
- **상관을 측정한다**: 1단 trigger 세트에서 2단 FPR을 재면 그것이 곧 조건부 FPR이다. 전체 negative에서 잰 2단 FPR을 곱하지 않는다.

---

## 3. 임계값 공동 최적화 — 재사용 가능한 optimizer

### 3.1 문제를 식으로 쓴다

```
변수        θ = (θ1, θ2, θ3)
최소화      P_avg(θ)                  평균 전력 (mW)
제약        miss(θ)   ≤ 5 %
            FA/day(θ) ≤ 1
```

왜 단계마다 따로 정하면 안 되나? θ1은 2단과 3단에 들어오는 **트래픽**을 정하고, θ2·θ3는 1단이 남긴 miss 예산을 나눠 쓴다. 한 단계의 임계값이 다른 단계의 최적값을 바꾼다. SSD에서 GC 시작 임계값과 write throttling 임계값을 따로 튜닝하면 서로 싸우던 것과 같다.

### 3.2 계산 트릭 — z로 조건을 걸면 곱이 된다

```
P(1..k단 모두 통과 | y) = ∫ φ(z) · Π_{j≤k} Q( (θ_j − μ_j·y − √ρ·z) / √(1 − ρ) ) dz
                       ≈ Σ_i w_i · Π_{j≤k} Q( (θ_j − μ_j·y − √ρ·z_i) / √(1 − ρ) )
```

말로 하면: z를 고정하면 단계들이 독립이므로 통과 확률을 곱하면 된다. z는 모르니 z의 대표값 60개(Gauss–Hermite 노드 z_i)에서 곱을 계산하고 가중 평균(w_i)한다. 펌웨어로 치면 "온도 구간별로 불량률을 계산해서 온도 분포로 가중 평균"하는 것과 같다. 이렇게 하면 FA/day ≈ 10⁻⁵ 수준의 꼬리 확률도 Monte Carlo 없이 정확하게, 그리고 **매끄러운 함수**로 얻는다. 매끄러우면 scipy의 gradient 기반 최적화를 쓸 수 있다.

### 3.3 코드로 확인 — optimizer

전략은 두 단계다. (1) 임계값 격자 46³ ≈ 9.7만 점을 numpy broadcasting으로 한 번에 평가해서 제약을 만족하는 가장 싼 점을 찾고, (2) 그 점에서 SLSQP(제약 있는 국소 최적화)로 다듬는다. FA 제약은 자릿수가 크게 변하므로 log를 씌워 넣는다.

```python
# 재사용 가능한 cascade 최적화기: 해석적 통과확률 + grid search + SLSQP 다듬기
import numpy as np
from scipy.stats import norm
from scipy.optimize import minimize
zq, wq = np.polynomial.hermite_e.hermegauss(60); wq = wq / wq.sum()   # 공유요인 z ~ N(0,1)
DAY = dict(n_neg=25_920, p_base_mw=1.0)            # 하루 negative 판정창, always-on 전력 (가정)

def pass_probs(th, mus, rho):                       # [P(1단 통과), P(1·2단 통과), ...]
    a, b, cond, out = np.sqrt(rho), np.sqrt(1 - rho), 1.0, []
    for t, mu in zip(th, mus):                      # z가 주어지면 단계들은 독립 → 곱한 뒤 z로 적분
        cond = cond * norm.sf((np.asarray(t, float)[..., None] - mu - a * zq) / b)
        out.append(cond @ wq)
    return out

def evaluate(th, st, rho, day=DAY):                 # st: mu(단계별 d'), e_next(k단 통과 시 드는 mJ)
    pn = pass_probs(th, [0.0] * len(st["mu"]), rho)
    pp = pass_probs(th, st["mu"], rho)
    e = day["p_base_mw"] * 86_400 + sum(ek * day["n_neg"] * p for ek, p in zip(st["e_next"], pn))
    return dict(p_mw=e / 86_400, miss=1 - pp[-1], fa_day=day["n_neg"] * pn[-1])

def optimize(st, rho, miss_max=0.05, fa_max=1.0, day=DAY, g=np.linspace(-1, 8, 46)):
    n = len(st["mu"])
    r = evaluate(np.meshgrid(*[g] * n, indexing="ij"), st, rho, day)
    ok = (r["miss"] <= miss_max) & (r["fa_day"] <= fa_max)
    if not ok.any():
        return None                                 # 이 구조로는 제약을 못 맞춘다
    x0 = g[list(np.unravel_index(np.argmin(np.where(ok, r["p_mw"], np.inf)), ok.shape))]
    f = lambda x, k: float(evaluate(x, st, rho, day)[k])
    cons = [{"type": "ineq", "fun": lambda x: miss_max - f(x, "miss")},
            {"type": "ineq", "fun": lambda x: np.log(fa_max) - np.log(f(x, "fa_day"))}]
    res = minimize(lambda x: f(x, "p_mw"), x0, method="SLSQP", constraints=cons)
    x = res.x if res.success and f(res.x, "miss") <= miss_max * 1.001 else x0
    return x, evaluate(x, st, rho, day)

D3 = dict(mu=[3.5, 6.0, 7.0], e_next=[15.0, 700.0, 2300.0])   # MCU → DSP → AP 검증 → 세션
x, r = optimize(D3, rho=0.5)
print("θ =", np.round(x, 3), {k: round(float(v), 4) for k, v in r.items()})
pn = pass_probs(x, [0, 0, 0], 0.5)
print("하루 단계 진입 (negative):", [round(DAY["n_neg"] * float(p), 2) for p in [1.0] + pn])
```

```text
θ = [1.798 3.605 3.774] {'p_mw': 1.1824, 'miss': 0.05, 'fa_day': 0.0599}
하루 단계 진입 (negative): [25920.0, 936.4, 2.26, 0.06]
```

출력에서 볼 것:

- 최적 임계값 θ = (1.80, 3.61, 3.77)에서 평균 전력 1.182 mW, miss 5.00%, FA 0.06/day.
- **묶인(binding) 제약은 miss다.** FA는 0.06/day로 목표 1/day보다 한참 아래다. 이 사례의 검증기(d' = 6, 7)가 강해서 FA는 "싸게" 맞춰지고, 남은 싸움은 전력 vs miss다.
- 하루 동안 negative 창이 DSP를 936번, AP를 2.26번 깨운다. 전력 분해: DSP 깨우기 0.163 mW, AP 검증 0.018 mW, false 세션 0.0016 mW. 헛일의 90%가 DSP 깨우기다.
- 단계별 누적 miss를 따로 계산해 보면 1단까지 4.43%, 2단까지 4.97%, 3단까지 5.00%다. 최적화기는 **miss 예산의 대부분을 1단에 쓴다** — θ1을 올리는 것이 DSP 깨우기 횟수를 줄이는 가장 큰 레버이기 때문이다.

마지막 관찰은 B9 5.4절의 "1단은 recall 우선"과 충돌하는 것처럼 보인다. 그렇지 않다. 1단은 여전히 negative의 3.6%(말소리 28초에 한 번꼴)를 통과시키는 **느슨한** 문턱이다. "recall 우선"은 방향을 말하고, 최적화기는 **얼마나** 느슨해야 하는지를 숫자로 준다. 1단이 놓친 4.4%를 줄이려면 1단 모델 자체(d')를 키워야 한다 — 이것이 4절 민감도 분석의 질문이다.

### 3.4 코드로 확인 — 해석 모델을 Monte Carlo로 검증

해석 계산(Gauss–Hermite)이 맞는지 같은 생성 모델에서 직접 뽑아 센다. negative 2,000만 개, 호출어 100만 개.

```python
# 해석 모델 검증: 같은 임계값에서 상관된 점수를 직접 뽑아 Monte Carlo로 센다
rng = np.random.default_rng(1)
def sample(n, mus, rho):
    z = rng.standard_normal((n, 1))                         # 창마다 하나의 공유 요인
    return np.asarray(mus) + np.sqrt(rho) * z + np.sqrt(1 - rho) * rng.standard_normal((n, len(mus)))
neg = sample(20_000_000, [0, 0, 0], 0.5)
pos = sample(1_000_000, D3["mu"], 0.5)
pn_mc = [(neg[:, :k + 1] > x[:k + 1]).all(1).mean() for k in range(3)]
pp_mc = (pos > x).all(1).mean()
pn_an = pass_probs(x, [0, 0, 0], 0.5)
for k in range(3):
    print(f"neg 1..{k + 1}단 통과: MC {pn_mc[k]:.3e}  해석 {float(pn_an[k]):.3e}")
print(f"miss: MC {1 - pp_mc:.4f}  해석 {float(r['miss']):.4f}")
print(f"FA/day: MC {DAY['n_neg'] * pn_mc[2]:.3f}  해석 {float(r['fa_day']):.3f}"
      f"   (MC 사건 수 = {int(round(pn_mc[2] * len(neg)))})")
```

```text
neg 1..1단 통과: MC 3.617e-02  해석 3.613e-02
neg 1..2단 통과: MC 8.625e-05  해석 8.701e-05
neg 1..3단 통과: MC 2.550e-06  해석 2.310e-06
miss: MC 0.0498  해석 0.0500
FA/day: MC 0.066  해석 0.060   (MC 사건 수 = 51)
```

출력에서 볼 것: 1단·2단 통과율과 miss는 세 자리까지 맞는다. 3단까지 통과는 사건이 51개뿐이라 MC 쪽 상대 오차가 약 ±14%(1/√51)이고, 해석값은 그 범위 안에 있다. **꼬리 확률을 MC로 재려면 사건 수가 필요하다** — 같은 문제가 8.4절에서 실제 녹음으로 FAR을 잴 때 다시 나온다.

### 3.5 1단 · 2단 · 3단 구조 비교와 Pareto front

같은 optimizer로 구조 자체를 비교한다. miss 목표를 1%에서 12%까지 바꾸며, FA ≤ 1/day를 지키는 최소 전력을 구한다.

- A: MCU KWS 1단만 (통과하면 바로 세션 3 J)
- B: 더 큰 KWS를 DSP에서 상시 실행 (1단, always-on 5 mW, d' = 6)
- C: MCU → DSP (2단)
- D: MCU → DSP → AP (3단, 기준 설계)

```python
# 1단·2단·3단 구조 비교 + miss 목표별 최소 전력 (Pareto front), FA ≤ 1/day, ρ = 0.5
DESIGNS = {  # 이름: (단계 정의, always-on mW)
    "A MCU 1단":         (dict(mu=[3.5], e_next=[3000.0]), 1.0),
    "B DSP 1단 상시":     (dict(mu=[6.0], e_next=[3000.0]), 5.0),
    "C MCU→DSP":         (dict(mu=[3.5, 6.0], e_next=[15.0, 3000.0]), 1.0),
    "D MCU→DSP→AP":      (dict(mu=[3.5, 6.0, 7.0], e_next=[15.0, 700.0, 2300.0]), 1.0),
}
targets = [0.01, 0.02, 0.03, 0.05, 0.08, 0.12]
print(f"{'design':16s}" + "".join(f"{'miss≤' + str(t):>11s}" for t in targets))
front = {}
for name, (st, base) in DESIGNS.items():
    day = dict(DAY, p_base_mw=base)
    vals = []
    for t in targets:
        out = optimize(st, 0.5, miss_max=t, fa_max=1.0, day=day)
        vals.append(None if out is None else float(out[1]["p_mw"]))
    front[name] = vals
    print(f"{name:16s}" + "".join(f"{'불가':>10s} " if v is None else f"{v:10.3f} " for v in vals))
```

```text
design            miss≤0.01  miss≤0.02  miss≤0.03  miss≤0.05  miss≤0.08  miss≤0.12
A MCU 1단                불가         불가         불가         불가         불가         불가 
B DSP 1단 상시             불가         불가      5.017      5.006      5.002      5.001 
C MCU→DSP               불가         불가      1.455      1.221      1.120      1.065 
D MCU→DSP→AP         1.755      1.445      1.310      1.182      1.102      1.056 
```

출력에서 볼 것:

- **A(MCU 1단)는 어떤 목표에서도 불가능**하다. FA 1/day는 창당 FPR `1 / 25,920 ≈ 3.9 × 10⁻⁵`, 즉 θ ≈ 3.95다. d' = 3.5면 TPR = Q(3.95 − 3.5) = Q(0.45) ≈ 0.33, **miss ≈ 67%** 다.
- **B(DSP 상시)는 가능하지만 5 mW**다. 배터리로 `4,158 / (5.006 × 86.4) ≈ 9.6일` vs D의 `4,158 / (1.182 × 86.4) ≈ 40.7일`. 좋은 검출기를 **항상** 돌리는 것보다 싼 검출기 뒤에 **가끔** 돌리는 게 4배 오래 간다.
- **C와 D의 차이는 목표가 빡빡할수록 커진다.** miss 5%에서는 1.221 vs 1.182(3%), 3%에서는 1.455 vs 1.310(10%), 2% 이하에서는 C가 불가능하다. 3단을 넣을 가치는 "느슨한 목표"에서는 작고 "빡빡한 목표"에서 결정적이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 360">
<text x="340" y="20" font-size="14" text-anchor="middle">Pareto front — miss 목표별 최소 전력 (ρ = 0.5, 실제 최적화 결과)</text> <line x1="70" y1="280" x2="610" y2="280" stroke="currentColor"/> <line x1="70" y1="280" x2="70" y2="40" stroke="currentColor"/> <line x1="70.0" y1="280" x2="70.0" y2="284" stroke="currentColor"/><text x="70.0" y="298" font-size="12" text-anchor="middle">0%</text> <line x1="142.0" y1="280" x2="142.0" y2="284" stroke="currentColor"/><text x="142.0" y="298" font-size="12" text-anchor="middle">2%</text> <line x1="214.0" y1="280" x2="214.0" y2="284" stroke="currentColor"/><text x="214.0" y="298" font-size="12" text-anchor="middle">4%</text> <line x1="286.0" y1="280" x2="286.0" y2="284" stroke="currentColor"/><text x="286.0" y="298" font-size="12" text-anchor="middle">6%</text>
<line x1="358.0" y1="280" x2="358.0" y2="284" stroke="currentColor"/><text x="358.0" y="298" font-size="12" text-anchor="middle">8%</text> <line x1="430.0" y1="280" x2="430.0" y2="284" stroke="currentColor"/><text x="430.0" y="298" font-size="12" text-anchor="middle">10%</text> <line x1="502.0" y1="280" x2="502.0" y2="284" stroke="currentColor"/><text x="502.0" y="298" font-size="12" text-anchor="middle">12%</text> <line x1="574.0" y1="280" x2="574.0" y2="284" stroke="currentColor"/><text x="574.0" y="298" font-size="12" text-anchor="middle">14%</text> <line x1="66" y1="280.0" x2="70" y2="280.0" stroke="currentColor"/><text x="62" y="284.0" font-size="12" text-anchor="end">1.0</text> <line x1="66" y1="232.0" x2="70" y2="232.0" stroke="currentColor"/><text x="62" y="236.0" font-size="12" text-anchor="end">1.2</text> <line x1="66" y1="184.0" x2="70" y2="184.0" stroke="currentColor"/><text x="62" y="188.0" font-size="12" text-anchor="end">1.4</text>
<line x1="66" y1="136.0" x2="70" y2="136.0" stroke="currentColor"/><text x="62" y="140.0" font-size="12" text-anchor="end">1.6</text> <line x1="66" y1="88.0" x2="70" y2="88.0" stroke="currentColor"/><text x="62" y="92.0" font-size="12" text-anchor="end">1.8</text> <line x1="66" y1="40.0" x2="70" y2="40.0" stroke="currentColor"/><text x="62" y="44.0" font-size="12" text-anchor="end">2.0</text> <text x="340" y="318" font-size="13" text-anchor="middle">허용 miss 목표 (%) — FA ≤ 1/day 고정</text> <text x="18" y="160" font-size="13" text-anchor="middle" transform="rotate(-90 18 160)">최소 평균 전력 (mW)</text> <polyline points="160.0,121.9 178.0,170.8 214.0,209.4 250.0,227.1 304.0,241.8 358.0,251.1 430.0,259.2 502.0,264.5 610.0,269.5" fill="none" stroke="#4a7bd0" stroke-width="2.5"/> <circle cx="160.0" cy="121.9" r="3" fill="#4a7bd0"/>
<circle cx="178.0" cy="170.8" r="3" fill="#4a7bd0"/> <circle cx="214.0" cy="209.4" r="3" fill="#4a7bd0"/> <circle cx="250.0" cy="227.1" r="3" fill="#4a7bd0"/> <circle cx="304.0" cy="241.8" r="3" fill="#4a7bd0"/> <circle cx="358.0" cy="251.1" r="3" fill="#4a7bd0"/> <circle cx="430.0" cy="259.2" r="3" fill="#4a7bd0"/> <circle cx="502.0" cy="264.5" r="3" fill="#4a7bd0"/>
<circle cx="610.0" cy="269.5" r="3" fill="#4a7bd0"/> <polyline points="106.0,98.9 115.0,125.5 124.0,145.3 142.0,173.1 160.0,191.9 178.0,205.6 214.0,224.2 250.0,236.2 304.0,248.0 358.0,255.6 430.0,262.3 502.0,266.7 610.0,270.9" fill="none" stroke="#e08a3c" stroke-width="2.5"/> <circle cx="106.0" cy="98.9" r="3" fill="#e08a3c"/> <circle cx="115.0" cy="125.5" r="3" fill="#e08a3c"/> <circle cx="124.0" cy="145.3" r="3" fill="#e08a3c"/> <circle cx="142.0" cy="173.1" r="3" fill="#e08a3c"/> <circle cx="160.0" cy="191.9" r="3" fill="#e08a3c"/>
<circle cx="178.0" cy="205.6" r="3" fill="#e08a3c"/> <circle cx="214.0" cy="224.2" r="3" fill="#e08a3c"/> <circle cx="250.0" cy="236.2" r="3" fill="#e08a3c"/> <circle cx="304.0" cy="248.0" r="3" fill="#e08a3c"/> <circle cx="358.0" cy="255.6" r="3" fill="#e08a3c"/> <circle cx="430.0" cy="262.3" r="3" fill="#e08a3c"/> <circle cx="502.0" cy="266.7" r="3" fill="#e08a3c"/>
<circle cx="610.0" cy="270.9" r="3" fill="#e08a3c"/> <circle cx="250.0" cy="236.2" r="7" fill="#e08a3c"/> <text x="260.0" y="226.2" font-size="12" text-anchor="start">선택점: miss 5%, 1.182 mW</text> <text x="91.6" y="56.8" font-size="12" text-anchor="start">C는 miss 2.5% 미만에서 불가능 (선이 끊김)</text> <text x="91.6" y="73.6" font-size="12" text-anchor="start">B(DSP 상시 1단)는 2.5% 이상에서 약 5.0 mW — 그림 위로 벗어남</text> <line x1="440" y1="60" x2="465" y2="60" stroke="#4a7bd0" stroke-width="3"/> <text x="472.0" y="64.0" font-size="12" text-anchor="start">C MCU→DSP</text>
<line x1="440" y1="78" x2="465" y2="78" stroke="#e08a3c" stroke-width="3"/> <text x="472.0" y="82.0" font-size="12" text-anchor="start">D MCU→DSP→AP</text>
</svg>
```

그림 4 — miss 목표별 최소 전력. 선 위의 점은 모두 같은 optimizer로 얻은 실제 최적값이다. 큰 주황 점이 기준 동작점(miss 5%, 1.182 mW). 왼쪽(엄격한 miss)으로 갈수록 전력이 가파르게 오르고, 2단 구조 C는 2.5% 미만에서 아예 끊긴다.

### 3.6 함정

- **격자만 믿지 말 것**: 격자 간격(여기서 0.2)보다 좁은 최적점을 놓친다. 격자로 출발점을 잡고 연속 최적화로 다듬는다. 반대로 SLSQP만 쓰면 국소해에 갇히거나 제약을 못 만족하는 점에서 멈출 수 있어서, 코드는 실패하면 격자 해로 돌아간다.
- **모델 상수가 틀리면 최적점도 틀린다**: d', ρ, 창 수, 에너지 모두 측정값으로 바꿔야 한다. 최적화기는 "측정한 상수를 넣으면 임계값을 주는 도구"이지 측정을 대신하지 않는다.
- **z 단위 vs 펌웨어 점수**: 최적화기의 θ는 표준화된 점수 단위다. 펌웨어는 보통 sigmoid 출력(0–1, Q8)을 비교한다. 검증 세트에서 "negative 점수의 상위 3.6% 지점"처럼 **분위수로 변환**해서 넣는다.

---

## 4. 민감도 — 2단을 더 좋게 만들까, 더 싸게 만들까

### 4.1 질문

DSP 검증기 팀이 두 안을 가져왔다. (a) 모델을 두 배 키워 d'를 6.0 → 6.5로 올리되 1회 비용이 15 → 45 mJ로 늘어난다. (b) 모델을 줄여 5 mJ로 만들되 d'가 5.5로 떨어진다. 어느 쪽인가? 정확도만 보면 (a), 비용만 보면 (b)다. 답은 **각 안에서 임계값을 다시 최적화한 뒤의 전력**을 비교해야 나온다.

### 4.2 코드로 확인 — d'2 × E2 격자

```python
# 민감도: 2단(DSP 검증기)의 품질 d'와 1회 비용 E2를 바꾸면 최적 전력은? (D 구조, miss ≤ 5%, FA ≤ 1/day)
print(f"{'d2':>5s}" + "".join(f"{'E2=' + str(e) + 'mJ':>12s}" for e in (5, 15, 45)) + f"{'AP진입/day(E2=15)':>20s}")
for d2 in (4.5, 5.0, 5.5, 6.0, 6.5, 7.0):
    row, ap = [], None
    for e2 in (5.0, 15.0, 45.0):
        st = dict(mu=[3.5, d2, 7.0], e_next=[e2, 700.0, 2300.0])
        x2, r2 = optimize(st, 0.5)
        row.append(float(r2["p_mw"]))
        if e2 == 15.0:
            ap = DAY["n_neg"] * float(pass_probs(x2, [0, 0, 0], 0.5)[1])
    print(f"{d2:5.1f}" + "".join(f"{v:12.3f}" for v in row) + f"{ap:20.2f}")
```

```text
   d2      E2=5mJ     E2=15mJ     E2=45mJ     AP진입/day(E2=15)
  4.5       1.519       1.719       2.190               54.38
  5.0       1.206       1.369       1.768               17.51
  5.5       1.105       1.236       1.581                6.06
  6.0       1.070       1.182       1.497                2.26
  6.5       1.057       1.160       1.458                0.86
  7.0       1.052       1.150       1.441                0.32
```

출력에서 볼 것:

- 기준(6.0, 15 mJ) = 1.182 mW. 안 (a)(6.5, 45 mJ) = 1.458 mW로 **나빠진다**. 안 (b)(5.5, 5 mJ) = 1.105 mW로 **좋아진다**.
- 하지만 (4.5, 5 mJ) = 1.519 mW로, 너무 약한 검증기는 싸도 진다. d'가 4.5면 AP에 하루 54번 negative가 올라간다(오른쪽 열). 검증기가 약하면 그 다음 단계(AP 0.7 J)가 대신 일한다.
- 열을 따라 보면(같은 E2), d'가 5 → 6일 때 크게 좋아지고 6 → 7일 때는 거의 그대로다. **수확 체감**: 2단이 충분히 좋아지면 남은 비용은 2단 자신의 호출 비용(936회 × E2)이다. 그때부터는 **E2를 줄이는 것**이 레버다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 360">
<text x="340" y="20" font-size="14" text-anchor="middle">2단 품질 vs 비용 — 같은 제약(miss ≤ 5%, FA ≤ 1/day)에서 최소 전력 (예제 7)</text> <line x1="70" y1="280" x2="610" y2="280" stroke="currentColor"/> <line x1="70" y1="280" x2="70" y2="40" stroke="currentColor"/> <line x1="70.0" y1="280" x2="70.0" y2="284" stroke="currentColor"/><text x="70.0" y="298" font-size="12" text-anchor="middle">4.5</text> <line x1="178.0" y1="280" x2="178.0" y2="284" stroke="currentColor"/><text x="178.0" y="298" font-size="12" text-anchor="middle">5.0</text> <line x1="286.0" y1="280" x2="286.0" y2="284" stroke="currentColor"/><text x="286.0" y="298" font-size="12" text-anchor="middle">5.5</text> <line x1="394.0" y1="280" x2="394.0" y2="284" stroke="currentColor"/><text x="394.0" y="298" font-size="12" text-anchor="middle">6.0</text>
<line x1="502.0" y1="280" x2="502.0" y2="284" stroke="currentColor"/><text x="502.0" y="298" font-size="12" text-anchor="middle">6.5</text> <line x1="610.0" y1="280" x2="610.0" y2="284" stroke="currentColor"/><text x="610.0" y="298" font-size="12" text-anchor="middle">7.0</text> <line x1="66" y1="280.0" x2="70" y2="280.0" stroke="currentColor"/><text x="62" y="284.0" font-size="12" text-anchor="end">1.0</text> <line x1="66" y1="240.0" x2="70" y2="240.0" stroke="currentColor"/><text x="62" y="244.0" font-size="12" text-anchor="end">1.2</text> <line x1="66" y1="200.0" x2="70" y2="200.0" stroke="currentColor"/><text x="62" y="204.0" font-size="12" text-anchor="end">1.4</text> <line x1="66" y1="160.0" x2="70" y2="160.0" stroke="currentColor"/><text x="62" y="164.0" font-size="12" text-anchor="end">1.6</text> <line x1="66" y1="120.0" x2="70" y2="120.0" stroke="currentColor"/><text x="62" y="124.0" font-size="12" text-anchor="end">1.8</text>
<line x1="66" y1="80.0" x2="70" y2="80.0" stroke="currentColor"/><text x="62" y="84.0" font-size="12" text-anchor="end">2.0</text> <line x1="66" y1="40.0" x2="70" y2="40.0" stroke="currentColor"/><text x="62" y="44.0" font-size="12" text-anchor="end">2.2</text> <text x="340" y="318" font-size="13" text-anchor="middle">2단(DSP 검증기) 분리도 d'</text> <text x="18" y="160" font-size="13" text-anchor="middle" transform="rotate(-90 18 160)">최적화된 평균 전력 (mW)</text> <polyline points="70.0,176.2 178.0,238.8 286.0,259.0 394.0,266.0 502.0,268.6 610.0,269.6" fill="none" stroke="#3f9a6b" stroke-width="2.5"/> <circle cx="70.0" cy="176.2" r="3" fill="#3f9a6b"/> <circle cx="178.0" cy="238.8" r="3" fill="#3f9a6b"/>
<circle cx="286.0" cy="259.0" r="3" fill="#3f9a6b"/> <circle cx="394.0" cy="266.0" r="3" fill="#3f9a6b"/> <circle cx="502.0" cy="268.6" r="3" fill="#3f9a6b"/> <circle cx="610.0" cy="269.6" r="3" fill="#3f9a6b"/> <polyline points="70.0,136.2 178.0,206.2 286.0,232.8 394.0,243.6 502.0,248.0 610.0,250.0" fill="none" stroke="#4a7bd0" stroke-width="2.5"/> <circle cx="70.0" cy="136.2" r="3" fill="#4a7bd0"/> <circle cx="178.0" cy="206.2" r="3" fill="#4a7bd0"/>
<circle cx="286.0" cy="232.8" r="3" fill="#4a7bd0"/> <circle cx="394.0" cy="243.6" r="3" fill="#4a7bd0"/> <circle cx="502.0" cy="248.0" r="3" fill="#4a7bd0"/> <circle cx="610.0" cy="250.0" r="3" fill="#4a7bd0"/> <polyline points="70.0,42.0 178.0,126.4 286.0,163.8 394.0,180.6 502.0,188.4 610.0,191.8" fill="none" stroke="#d0564a" stroke-width="2.5"/> <circle cx="70.0" cy="42.0" r="3" fill="#d0564a"/> <circle cx="178.0" cy="126.4" r="3" fill="#d0564a"/>
<circle cx="286.0" cy="163.8" r="3" fill="#d0564a"/> <circle cx="394.0" cy="180.6" r="3" fill="#d0564a"/> <circle cx="502.0" cy="188.4" r="3" fill="#d0564a"/> <circle cx="610.0" cy="191.8" r="3" fill="#d0564a"/> <line x1="470" y1="60" x2="495" y2="60" stroke="#3f9a6b" stroke-width="3"/> <text x="502.0" y="64.0" font-size="12" text-anchor="start">E2 = 5 mJ/회</text> <line x1="470" y1="78" x2="495" y2="78" stroke="#4a7bd0" stroke-width="3"/>
<text x="502.0" y="82.0" font-size="12" text-anchor="start">E2 = 15 mJ/회</text> <line x1="470" y1="96" x2="495" y2="96" stroke="#d0564a" stroke-width="3"/> <text x="502.0" y="100.0" font-size="12" text-anchor="start">E2 = 45 mJ/회</text> <circle cx="394.0" cy="243.6" r="7" fill="#4a7bd0"/> <text x="402.0" y="231.6" font-size="12" text-anchor="start">기준 (6.0, 15 mJ)</text> <circle cx="286.0" cy="259.0" r="7" fill="#3f9a6b"/>
</svg>
```

그림 5 — 2단 검증기의 분리도(가로)와 1회 비용(색)에 따른 최적 전력. 큰 파란 점이 기준, 큰 초록 점이 "더 싸고 조금 약한" 안 (5.5, 5 mJ)이다. 곡선이 오른쪽에서 평평해지는 지점부터는 정확도보다 비용을 줄이는 쪽이 이긴다.

### 4.3 코드로 확인 — 상관을 무시하고 설계하면

이번에는 상관 ρ를 모른 채(ρ = 0이라고 가정하고) 임계값을 정한 뒤, 실제 ρ가 다른 필드에 내보낸다.

```python
# 상관을 무시하고(ρ=0 가정) 임계값을 정한 뒤, 실제 ρ인 필드에 내보내면?
x0, r0 = optimize(D3, rho=0.0)
print("ρ=0 가정 설계 θ =", np.round(x0, 2), " 설계 시 FA/day = %.3f" % float(r0["fa_day"]))
print(f"{'실제 ρ':>7s}{'FA/day':>9s}{'miss':>8s}{'mW':>8s}   |  그 ρ로 다시 최적화: {'θ':>20s}{'mW':>8s}")
for rho in (0.0, 0.3, 0.5, 0.7, 0.9):
    rf = evaluate(x0, D3, rho)                       # 같은 임계값, 다른 현실
    out = optimize(D3, rho)
    txt = "불가" if out is None else f"{str(np.round(out[0], 2)):>20s}{float(out[1]['p_mw']):8.3f}"
    print(f"{rho:7.1f}{float(rf['fa_day']):9.2f}{float(rf['miss']):8.3f}{float(rf['p_mw']):8.3f}   |  {txt}")
```

```text
ρ=0 가정 설계 θ = [1.84 3.13 2.77]  설계 시 FA/day = 0.002
   실제 ρ   FA/day    miss      mW   |  그 ρ로 다시 최적화:                    θ      mW
    0.0     0.00   0.050   1.156   |      [1.84 3.13 2.77]   1.156
    0.3     0.28   0.050   1.193   |      [1.81 3.44 3.43]   1.172
    0.5     1.72   0.049   1.276   |      [1.8  3.61 3.77]   1.182
    0.7     6.20   0.049   1.457   |      [1.8  3.74 4.11]   1.182
    0.9    16.41   0.048   1.771   |      [1.83 3.88 4.55]   1.164
```

출력에서 볼 것:

- ρ = 0 가정으로 설계하면 "FA 0.002/day"라는 아름다운 숫자가 나온다.
- 같은 임계값이 실제 ρ = 0.5인 필드에서는 **FA 1.72/day**(목표 위반, 설계 숫자의 약 860배), ρ = 0.9면 16.4/day다.
- **miss는 거의 그대로(0.049)** 다. 즉 호출어 테스트(positive 테스트)만으로는 이 사고를 발견할 수 없다. negative를 길게 돌려야만 보인다(8.4절).
- 실제 ρ로 다시 최적화하면 θ3을 2.77 → 3.77로 올려야 한다. 재최적화한 전력은 ρ에 따라 단조롭게 늘지 않는다(1.156 → 1.182 → 1.164). 호출어 쪽도 상관이 있어서 "또렷한 호출어는 모든 단계를 함께 통과"하므로 miss 예산이 덜 깎이기 때문이다. 상관은 비용을 반드시 키우는 게 아니라 **설계를 틀리게** 만든다.

---

## 5. 모델 하나를 엔진 여러 개에 나누기 — split computing

### 5.1 직관 — 공장 라인의 어느 공정에서 트럭에 싣나

cascade는 "모델 여러 개를 순서대로" 두는 것이었다. split computing은 **모델 하나를 층 단위로 잘라** 앞부분은 엔진 A(MCU·DSP)에서, 뒷부분은 엔진 B(NPU·cloud)에서 돌린다. 공장에 비유하면 원자재를 트럭에 실어 큰 공장으로 보낼지, 동네 공장에서 반제품까지 만들어 보낼지, 완제품까지 만들지의 문제다. 트럭 운임(링크 비용)은 **보내는 화물 크기**에 비례하고, 공정마다 화물 크기가 다르다.

### 5.2 비용 모델

```
split k = 블록 0..k−1은 A에서, k..끝은 B에서. 경계에서 블록 k−1의 출력(activation)을 보낸다.

E(k) = Σ_{i<k} MAC_i · e_A  +  E_link,fixed + bytes(k) · e_link  +  Σ_{i≥k} MAC_i · e_B + E_wake,B
L(k) = Σ_{i<k} MAC_i / R_A  +  L_link,fixed + bytes(k) / BW      +  Σ_{i≥k} MAC_i / R_B + L_wake,B

제약: A가 들고 있는 weight ≤ A 메모리,  A의 피크 activation ≤ A SRAM,  L(k) ≤ deadline
```

말로 하면: 에너지와 지연 모두 "A의 계산 + 링크 + B의 계산(과 B를 깨우는 비용)"이다. e는 MAC당 에너지(pJ), R은 유효 처리율(MAC/s)이다. 핵심 변수는 `bytes(k)`다 — 층마다 activation 크기가 다르고, **단조롭게 줄지 않는다**.

### 5.3 코드로 확인 — forward hook으로 층별 MAC과 activation 재기

1초 log-mel(40 × 100)을 입력으로 받는 작은 오디오 CNN(conv-ReLU-maxpool 블록 4개 + 분류기)을 만든다. PyTorch의 `register_forward_hook`은 모듈의 forward가 끝날 때마다 호출되는 콜백이다 — 펌웨어의 trace hook처럼, 모델 코드를 고치지 않고 층마다 출력 shape를 볼 수 있다.

```python
# 작은 오디오 CNN의 블록별 MAC·출력 activation 크기를 forward hook으로 잰다
import torch, torch.nn as nn
def block(ci, co): return nn.Sequential(nn.Conv2d(ci, co, 3, padding=1), nn.ReLU(), nn.MaxPool2d(2))
net = nn.Sequential(block(1, 32), block(32, 64), block(64, 128), block(128, 256),
                    nn.Sequential(nn.AdaptiveAvgPool2d(1), nn.Flatten(), nn.Linear(256, 12)))
stats = [dict(mac=0, act=0, shape=None, w=0) for _ in net]
def leaf_hook(b):                                    # conv/linear: 출력 원소마다 (입력 fan-in)번 MAC
    def f(m, inp, out):
        if isinstance(m, nn.Conv2d):
            stats[b]["mac"] += out.numel() * m.in_channels * m.kernel_size[0] * m.kernel_size[1]
        elif isinstance(m, nn.Linear):
            stats[b]["mac"] += out.numel() * m.in_features
    return f
def block_hook(b):                                   # 블록 출력 = 여기서 자르면 보내야 할 텐서
    def f(m, inp, out):
        stats[b]["act"], stats[b]["shape"] = out.numel(), tuple(out.shape[1:])
    return f
for b, blk in enumerate(net):
    blk.register_forward_hook(block_hook(b))
    stats[b]["w"] = sum(p.numel() for p in blk.parameters())
    for m in blk.modules():
        if isinstance(m, (nn.Conv2d, nn.Linear)): m.register_forward_hook(leaf_hook(b))
with torch.no_grad():
    net(torch.zeros(1, 1, 40, 100))                  # 1 s log-mel: 40 mel × 100 frame
print(f"{'block':6s}{'출력 shape':>15s}{'act B(int8)':>12s}{'MAC':>13s}{'weight B':>10s}")
print(f"{'input':6s}{str((1, 40, 100)):>15s}{4000:>12d}")
for b, s in enumerate(stats):
    print(f"{b:<6d}{str(s['shape']):>15s}{s['act']:>12d}{s['mac']:>13,d}{s['w']:>10,d}")
print(f"total MAC = {sum(s['mac'] for s in stats):,}   total weights = {sum(s['w'] for s in stats):,}")
```

```text
block        출력 shape act B(int8)          MAC  weight B
input    (1, 40, 100)        4000
0        (32, 20, 50)       32000    1,152,000       320
1        (64, 10, 25)       16000   18,432,000    18,496
2        (128, 5, 12)        7680   18,432,000    73,856
3         (256, 2, 6)        3072   17,694,720   295,168
4               (12,)          12        3,072     3,084
total MAC = 55,713,792   total weights = 390,924
```

출력에서 볼 것:

- 총 5,570만 MAC, weight 39만 개(int8이면 약 391 KB).
- 블록 0의 출력(32 × 20 × 50 = 32,000 B)이 **입력(4,000 B)보다 8배 크다**. 채널을 1 → 32로 늘리는 첫 conv는 데이터를 부풀린다. pooling이 거듭되면서 16,000 → 7,680 → 3,072 B로 줄지만, 블록 3이 지나야 겨우 입력보다 작아진다.
- MAC은 블록 1·2·3에 고르게 몰려 있고(각 1,800만 정도), weight는 블록 3이 대부분(295 KB)이다.

### 5.4 코드로 확인 — split 지점별 에너지와 지연

엔진과 링크 상수는 모두 가정이다(자릿수만 현실적). 시나리오 세 개를 비교한다.

| 엔진 / 링크 | 유효 처리율 | 에너지 | 깨우기 · 고정비 |
|---|---|---|---|
| MCU (Cortex-M급) | 0.1 GMAC/s | 50 pJ/MAC | — |
| DSP | 2 GMAC/s | 10 pJ/MAC | — |
| NPU 잠든 상태 | 100 GMAC/s | 3 pJ/MAC | 3 mJ, 10 ms |
| NPU 깨어 있음(세션 중) | 100 GMAC/s | 3 pJ/MAC | 0.1 mJ, 0.5 ms |
| 공유 메모리(DSP → NPU) | 2 GB/s | 0.2 nJ/B | 0.02 mJ |
| BLE → 폰 → cloud | 100 KB/s | 0.6 µJ/B | 2 mJ, 150 ms (D7 6.3절과 같은 가정) |

```python
import numpy as np   # 앞 예제의 stats를 이어서 쓴다
# split k = 앞 k개 블록은 A에서, 나머지는 B에서. 모든 상수는 가정
ENG = {  # 이름: (유효 GMAC/s, pJ/MAC, 깨우기 mJ, 깨우기 ms)
    "MCU": (0.1, 50, 0, 0), "DSP": (2.0, 10, 0, 0),
    "NPU잠": (100, 3, 3.0, 10.0), "NPU깸": (100, 3, 0.1, 0.5), "cloud": (1000, 0, 0, 5.0)}
LINK = {"shm": (2000, 0.2, 0.02, 0.05), "BLE": (0.1, 600, 2.0, 150.0)}  # (MB/s, nJ/B, 고정 mJ, 고정 ms)
mac = [s["mac"] for s in stats]
act = [4000] + [s["act"] for s in stats]           # act[k] = split k에서 보낼 바이트 (int8)
wcum = np.cumsum([0] + [s["w"] for s in stats])     # A가 들고 있어야 할 weight 바이트

def split_cost(k, A, B, link):
    ga, ea, _, _ = ENG[A]; gb, eb, wake_e, wake_t = ENG[B]; bw, e_b, lf_e, lf_t = LINK[link]
    ma, mb = sum(mac[:k]), sum(mac[k:])
    e = ma * ea * 1e-9 + lf_e + act[k] * e_b * 1e-6 + mb * eb * 1e-9 + (wake_e if mb else 0)  # mJ
    t = ma / (ga * 1e6) + lf_t + act[k] / (bw * 1e3) + mb / (gb * 1e6) + (wake_t if mb else 0)  # ms
    return e, t

SCEN = [("DSP→NPU(잠)", "DSP", "NPU잠", "shm"), ("DSP→NPU(깸)", "DSP", "NPU깸", "shm"),
        ("MCU→cloud", "MCU", "cloud", "BLE")]
print(f"{'k':>2s}{'보낼 B':>8s}{'A weight':>10s}" + "".join(f"{n:>22s}" for n, *_ in SCEN))
for k in range(len(act)):
    cells = "".join("{:>13.3f} mJ {:>5.0f} ms".format(*split_cost(k, a, b, l)) for _, a, b, l in SCEN)
    print(f"{k:2d}{act[k]:8d}{int(wcum[k]):10,d}{cells}")
```

```text
 k    보낼 B  A weight            DSP→NPU(잠)            DSP→NPU(깸)             MCU→cloud
 0    4000         0        3.188 mJ    11 ms        0.288 mJ     1 ms        4.400 mJ   195 ms
 1   32000       320        3.202 mJ    11 ms        0.302 mJ     2 ms       21.258 mJ   487 ms
 2   16000    18,816        3.327 mJ    20 ms        0.427 mJ    11 ms       12.579 mJ   511 ms
 3    7680    92,672        3.455 mJ    29 ms        0.555 mJ    20 ms        8.509 mJ   612 ms
 4    3072   387,840        3.578 mJ    38 ms        0.678 mJ    28 ms        6.629 mJ   743 ms
 5      12   390,924        0.577 mJ    28 ms        0.577 mJ    28 ms        4.793 mJ   707 ms
```

손으로 한 줄 확인: DSP → NPU(잠), k = 0이면 `링크 0.02 + 4,000 B × 0.2 nJ ≈ 0.021 mJ`, `NPU 5,571만 MAC × 3 pJ = 0.167 mJ`, 깨우기 3 mJ → 합 3.188 mJ. k = 5(전부 DSP)면 `5,571만 × 10 pJ = 0.557 mJ` + 링크 0.02 → 0.577 mJ, 지연 `5,571만 / 2 G = 27.9 ms`. 표와 같다.

출력에서 볼 것:

- **DSP → NPU(잠)**: NPU 깨우기 3 mJ가 모든 것을 지배한다. 전부 DSP에서(k = 5) 0.58 mJ·28 ms, 전부 NPU에서(k = 0) 3.19 mJ·11 ms. 중간 split은 "깨우기도 내고 DSP 계산도 하는" 최악의 조합이다.
- **DSP → NPU(깸)**: 같은 모델, 같은 칩인데 NPU가 이미 깨어 있으면 k = 0(입력만 보내기)이 0.29 mJ·1 ms로 최선이다. **최적 split은 B 엔진의 전원 상태에 따라 바뀐다.** 그래서 배치는 컴파일 시점이 아니라 런타임 정책이 되기도 한다 — "세션 중이면 NPU, 아니면 DSP".
- **MCU → cloud(BLE)**: k = 0(특징 4 KB 전송)이 4.40 mJ로 최선. 중간 split은 activation이 입력보다 커서 링크 비용이 폭증한다(k = 1이면 32 KB → 21 mJ). 전부 MCU(k = 5)는 4.79 mJ지만 weight 391 KB가 필요해서 MCU SRAM(수백 KB, 그중 일부만 모델용)에 들어가기 어렵고, 지연이 0.7초다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 380">
<text x="340" y="20" font-size="14" text-anchor="middle">split 지점별 에너지 — 동그라미 = 최소점 (예제 9, 가정 상수)</text> <line x1="70" y1="270" x2="610" y2="270" stroke="currentColor"/> <line x1="70" y1="270" x2="70" y2="40" stroke="currentColor"/> <line x1="98.9" y1="270" x2="98.9" y2="274" stroke="currentColor"/><text x="98.9" y="288" font-size="12" text-anchor="middle">k=0</text> <line x1="195.4" y1="270" x2="195.4" y2="274" stroke="currentColor"/><text x="195.4" y="288" font-size="12" text-anchor="middle">k=1</text> <line x1="291.8" y1="270" x2="291.8" y2="274" stroke="currentColor"/><text x="291.8" y="288" font-size="12" text-anchor="middle">k=2</text> <line x1="388.2" y1="270" x2="388.2" y2="274" stroke="currentColor"/><text x="388.2" y="288" font-size="12" text-anchor="middle">k=3</text>
<line x1="484.6" y1="270" x2="484.6" y2="274" stroke="currentColor"/><text x="484.6" y="288" font-size="12" text-anchor="middle">k=4</text> <line x1="581.1" y1="270" x2="581.1" y2="274" stroke="currentColor"/><text x="581.1" y="288" font-size="12" text-anchor="middle">k=5</text> <line x1="66" y1="270.0" x2="70" y2="270.0" stroke="currentColor"/><text x="62" y="274.0" font-size="12" text-anchor="end">0.1</text> <line x1="66" y1="225.7" x2="70" y2="225.7" stroke="currentColor"/><text x="62" y="229.7" font-size="12" text-anchor="end">0.3</text> <line x1="66" y1="177.2" x2="70" y2="177.2" stroke="currentColor"/><text x="62" y="181.2" font-size="12" text-anchor="end">1</text> <line x1="66" y1="132.8" x2="70" y2="132.8" stroke="currentColor"/><text x="62" y="136.8" font-size="12" text-anchor="end">3</text> <line x1="66" y1="84.3" x2="70" y2="84.3" stroke="currentColor"/><text x="62" y="88.3" font-size="12" text-anchor="end">10</text>
<line x1="66" y1="40.0" x2="70" y2="40.0" stroke="currentColor"/><text x="62" y="44.0" font-size="12" text-anchor="end">30</text> <text x="340" y="308" font-size="13" text-anchor="middle">split 지점 k (앞 k블록을 A에서) — 아래 숫자는 보낼 바이트</text> <text x="18" y="155" font-size="13" text-anchor="middle" transform="rotate(-90 18 155)">추론 1회 에너지 (mJ, 로그)</text> <text x="98.9" y="322.0" font-size="12" text-anchor="middle">4000 B</text> <text x="195.4" y="322.0" font-size="12" text-anchor="middle">32000 B</text> <text x="291.8" y="322.0" font-size="12" text-anchor="middle">16000 B</text> <text x="388.2" y="322.0" font-size="12" text-anchor="middle">7680 B</text>
<text x="484.6" y="322.0" font-size="12" text-anchor="middle">3072 B</text> <text x="581.1" y="322.0" font-size="12" text-anchor="middle">12 B</text> <polyline points="98.9,130.4 195.4,130.2 291.8,128.7 388.2,127.2 484.6,125.7 581.1,199.3" fill="none" stroke="#d0564a" stroke-width="2.5"/> <circle cx="98.9" cy="130.4" r="3.5" fill="#d0564a"/> <circle cx="195.4" cy="130.2" r="3.5" fill="#d0564a"/> <circle cx="291.8" cy="128.7" r="3.5" fill="#d0564a"/> <circle cx="388.2" cy="127.2" r="3.5" fill="#d0564a"/>
<circle cx="484.6" cy="125.7" r="3.5" fill="#d0564a"/> <circle cx="581.1" cy="199.3" r="3.5" fill="#d0564a"/> <circle cx="581.1" cy="199.3" r="9" fill="none" stroke="#d0564a" stroke-width="2"/> <polyline points="98.9,227.3 195.4,225.4 291.8,211.5 388.2,200.9 484.6,192.8 581.1,199.3" fill="none" stroke="#3f9a6b" stroke-width="2.5"/> <circle cx="98.9" cy="227.3" r="3.5" fill="#3f9a6b"/> <circle cx="195.4" cy="225.4" r="3.5" fill="#3f9a6b"/> <circle cx="291.8" cy="211.5" r="3.5" fill="#3f9a6b"/>
<circle cx="388.2" cy="200.9" r="3.5" fill="#3f9a6b"/> <circle cx="484.6" cy="192.8" r="3.5" fill="#3f9a6b"/> <circle cx="581.1" cy="199.3" r="3.5" fill="#3f9a6b"/> <circle cx="98.9" cy="227.3" r="9" fill="none" stroke="#3f9a6b" stroke-width="2"/> <polyline points="98.9,117.4 195.4,53.9 291.8,75.0 388.2,90.8 484.6,100.9 581.1,114.0" fill="none" stroke="#4a7bd0" stroke-width="2.5"/> <circle cx="98.9" cy="117.4" r="3.5" fill="#4a7bd0"/> <circle cx="195.4" cy="53.9" r="3.5" fill="#4a7bd0"/>
<circle cx="291.8" cy="75.0" r="3.5" fill="#4a7bd0"/> <circle cx="388.2" cy="90.8" r="3.5" fill="#4a7bd0"/> <circle cx="484.6" cy="100.9" r="3.5" fill="#4a7bd0"/> <circle cx="581.1" cy="114.0" r="3.5" fill="#4a7bd0"/> <circle cx="98.9" cy="117.4" r="9" fill="none" stroke="#4a7bd0" stroke-width="2"/> <line x1="430" y1="56" x2="455" y2="56" stroke="#d0564a" stroke-width="3"/> <text x="462.0" y="60.0" font-size="12" text-anchor="start">DSP→NPU(잠)</text>
<line x1="430" y1="74" x2="455" y2="74" stroke="#3f9a6b" stroke-width="3"/> <text x="462.0" y="78.0" font-size="12" text-anchor="start">DSP→NPU(깸)</text> <line x1="430" y1="92" x2="455" y2="92" stroke="#4a7bd0" stroke-width="3"/> <text x="462.0" y="96.0" font-size="12" text-anchor="start">MCU→cloud(BLE)</text>
</svg>
```

그림 6 — split 지점별 추론 1회 에너지(로그 축). 동그라미가 각 시나리오의 최소점이다. 세 곡선 모두 최소점이 **양 끝**(k = 0 또는 k = 5)에 있다. 작은 CNN에서 중간 split이 이기기 어려운 이유는 앞쪽 층의 activation이 입력보다 크기 때문이다.

### 5.5 코드로 확인 — bottleneck으로 중간 split을 살리기

중간 split이 이기려면 "보내는 텐서가 입력보다 충분히 작고, 그 앞의 계산이 싸야" 한다. 첫 조건을 **설계로** 만드는 것이 bottleneck이다. 경계 직전에 1 × 1 conv로 채널을 64 → 8로 줄이고(학습 시 함께 넣는다), 저비트로 양자화한다. 비교를 위해 raw PCM을 그대로 보내는 경우와, A = DSP인 경우도 넣는다.

```python
# 이어서: 보내는 텐서를 줄이는 bottleneck(1×1 conv로 채널 축소 + 저비트) — A→cloud(BLE)에서 효과
FE_MAC = 1_500_000                                   # log-mel front-end 비용을 MAC 등가로 (가정)
def offload(A, k, nbytes, extra_mac=0, raw=False):   # k개 블록을 A에서, nbytes 보내고 나머지는 cloud
    ga, ea, _, _ = ENG[A]; bw, e_b, lf_e, lf_t = LINK["BLE"]
    ma = (0 if raw else FE_MAC) + sum(mac[:k]) + extra_mac
    e = ma * ea * 1e-9 + lf_e + nbytes * e_b * 1e-6
    t = ma / (ga * 1e6) + lf_t + nbytes / (bw * 1e3) + 5.0          # +5 ms cloud 계산
    return e, t
OPTS = [  # (설명, k, 보낼 바이트, 추가 MAC, raw?)
    ("raw PCM 1 s (16 kHz·16 bit)", 0, 32_000, 0, True),
    ("log-mel int8 (k=0)", 0, 4_000, 0, False),
    ("k=2 그대로 64ch int8", 2, 16_000, 0, False),
    ("k=2 + BN 64→8ch int8", 2, 8 * 250, 64 * 8 * 250, False),
    ("k=2 + BN 64→8ch int4", 2, 8 * 250 // 2, 64 * 8 * 250, False),
    ("k=3 + BN 128→8ch int8", 3, 8 * 60, 128 * 8 * 60, False),
]
print(f"{'옵션':28s}{'보낼 B':>8s}{'MCU→cloud':>22s}{'DSP→cloud':>22s}")
for name, k, nb, xm, raw in OPTS:
    cells = "".join("{:>12.3f} mJ {:>4.0f} ms".format(*offload(A, k, nb, xm, raw)) for A in ("MCU", "DSP"))
    print(f"{name:28s}{nb:8d}{cells}")
```

```text
옵션                              보낼 B             MCU→cloud             DSP→cloud
raw PCM 1 s (16 kHz·16 bit)    32000      21.200 mJ  475 ms      21.200 mJ  475 ms
log-mel int8 (k=0)              4000       4.475 mJ  210 ms       4.415 mJ  196 ms
k=2 그대로 64ch int8              16000      12.654 mJ  526 ms      11.811 mJ  326 ms
k=2 + BN 64→8ch int8            2000       4.261 mJ  387 ms       3.412 mJ  186 ms
k=2 + BN 64→8ch int4            1000       3.661 mJ  377 ms       2.812 mJ  176 ms
k=3 + BN 128→8ch int8            480       4.267 mJ  556 ms       2.684 mJ  180 ms
```

출력에서 볼 것:

- **가장 큰 절약은 raw PCM → log-mel**(21.2 → 4.4 mJ, 약 5배)이다. 특징 추출을 기기에서 하는 것 자체가 가장 싼 "split"이다(I4의 주제).
- **DSP에서는 bottleneck이 이긴다**: 블록 3까지 DSP에서 돌리고 8채널 480 B만 보내면 2.68 mJ·180 ms. log-mel 전송(4.42 mJ)보다 39% 싸고 지연도 비슷하다.
- **MCU에서는 이득이 작다**(4.26 vs 4.48 mJ) — MCU의 MAC이 비싸서(50 pJ) 앞부분 계산이 링크 절약분을 거의 다 먹고, 지연은 387 ms로 나빠진다. 같은 bottleneck이라도 **A 엔진의 MAC당 에너지**가 답을 바꾼다.
- int8 → int4는 바이트를 반으로 줄여 0.6 mJ를 더 아낀다. BLE 고정비(2 mJ)는 여전히 바닥에 남는다.

### 5.6 split computing의 함정

- **정확도는 공짜가 아니다**: bottleneck과 int4는 재학습(QAT, C2)이 필요하고 정확도를 다시 검증해야 한다(C8). 위 표는 비용만 계산했다.
- **두 반쪽의 버전이 묶인다**: 기기의 앞부분과 서버의 뒷부분은 같은 학습에서 나온 쌍이다. 기기 OTA(J5)와 서버 배포의 순서가 어긋나면 조용히 틀린 답이 나온다. 메시지에 모델 버전을 넣는다(9절).
- **중간 특징은 익명화가 아니다**: 중간 activation에서 원래 음성을 꽤 복원할 수 있다(H6 5.2절). 프라이버시 근거로 split을 쓰면 안 된다.
- **early-exit과 합친다**: A 쪽 split 지점에 작은 분류 head를 달아 확신이 높으면 거기서 끝낸다(B9 4절). 그러면 링크 비용은 "확신 없을 때만" 낸다 — split과 cascade가 하나가 된다.

---

## 6. 기기 vs 클라우드 — SLM/LLM 질의 라우팅

### 6.1 결정 함수

wake 이후, ASR이 텍스트를 만들면 질의 하나를 어디서 답할지 정해야 한다. L3에서 기준 목록을 다루고, 여기서는 **정책을 시뮬레이션으로 비교하는 법**에 집중한다. 결정 함수는 보통 규칙 몇 개 + 점수 하나다.

```
route(q):
    if q.private or net == offline        → device          (프라이버시 · 오프라인은 협상 불가)
    τ_eff = τ − 0.2 if net == poor else τ                    (연결이 나쁘면 기기 쪽으로 기울인다)
    if conf_easy(q) ≥ τ_eff               → device          (쉬워 보이는 질의)
    else                                  → cloud
    cloud 요청이 timeout                   → device로 fallback
```

말로 하면: 협상할 수 없는 조건(프라이버시, 연결 없음)을 먼저 거르고, 나머지는 "기기 SLM이 충분히 잘할 것 같은가"라는 점수 하나로 나눈다. 연결 상태가 나쁘면 클라우드의 지연 꼬리가 길어지므로 문턱을 낮춰 기기 쪽으로 더 보낸다.

### 6.2 코드로 확인 — 하루 120개 질의

질의 난이도 d(Beta 분포, 대부분 쉬움), 연결 상태(good 70% · poor 20% · offline 10%), 프라이버시 플래그(15%)를 섞는다. 기기 SLM은 첫 토큰이 빠르지만(0.35 s + α) 토큰당 30 mJ를 쓰고, 어려울수록 틀린다. 클라우드는 RTT(로그정규, poor면 길다)만큼 늦지만 무선 에너지만 쓰고 정확하다. 지연 지표는 **TTFT**(time to first token — 음성 비서에서 사용자가 "답이 시작됐다"고 느끼는 시점)다.

```python
# 하루 120개 질의: 난이도·연결 상태·프라이버시가 섞인 상황에서 라우팅 정책 비교 (모든 상수는 가정)
import numpy as np
rng = np.random.default_rng(7)
N = 120
d = rng.beta(1.2, 3.0, N)                                    # 난이도 0(쉬움) ~ 1(어려움)
net = rng.choice(["good", "poor", "off"], N, p=[0.7, 0.2, 0.1])
private = rng.random(N) < 0.15                               # 개인 데이터 → 기기 밖으로 못 나감
ntok = (30 + 70 * d).astype(int)                             # 답 길이 (토큰)
conf = np.clip(1 - d + rng.normal(0, 0.12, N), 0, 1)         # 라우터가 보는 "쉬워 보임" 점수
u = rng.random(N)                                            # 정답 여부용 공통 난수
rtt = np.where(net == "good", rng.lognormal(np.log(0.25), 0.4, N), rng.lognormal(np.log(1.2), 0.6, N))
dev_lat, dev_e, dev_ok = 0.35 + 0.002 * ntok, 0.30 + 0.030 * ntok, u < 0.97 - 0.75 * d    # NPU SLM: 첫 토큰(TTFT), J, 정답
cld_lat = rtt + 0.45                                        # 첫 토큰까지: RTT + 클라우드 TTFT
cld_e = np.where(net == "good", 0.25, 0.80) + 0.01 * ntok   # 무선 (poor = 재전송·긴 radio-on)
cld_ok = u < 0.97 - 0.10 * d
TIMEOUT = 2.0                                                # 오프라인 감지까지 기다리는 시간

def run(policy, tau=0.6):
    cloud = np.zeros(N, bool) if policy == "device" else (net != "off") & ~private
    if policy == "routed":
        tau_eff = np.where(net == "poor", tau - 0.2, tau)    # 연결 나쁘면 기기 쪽으로 기울인다
        cloud &= conf < tau_eff
    lat = np.where(cloud, cld_lat, dev_lat); e = np.where(cloud, cld_e, dev_e)
    ok = np.where(cloud, cld_ok, dev_ok)
    if policy == "cloud":                                    # 오프라인이면 timeout 뒤 실패, 프라이버시 위반
        off = net == "off"
        lat = np.where(off, TIMEOUT, cld_lat); e = np.where(off, 0.3, cld_e); ok = np.where(off, False, cld_ok)
    viol = int((policy == "cloud") * (private & (net != "off")).sum())
    return np.percentile(lat, 50), np.percentile(lat, 95), e.sum(), ok.mean(), cloud.mean(), viol
print(f"{'policy':12s}{'TTFT50':>7s}{'TTFT95':>7s}{'J/day':>7s}{'%batt':>6s}{'quality':>8s}{'cloud%':>7s}{'위반':>5s}")
rows = [(p, run(p)) for p in ("cloud", "device", "routed")] + [(f"routed τ={t}", run("routed", t)) for t in (0.4, 0.5, 0.7, 0.8)]
for name, (p50, p95, ej, q, cf, v) in rows:
    print(f"{name:12s}{p50:7.2f}{p95:7.2f}{ej:7.1f}{ej / 4158:6.1%}{q:8.3f}{cf:7.0%}{v:5d}")
```

```text
policy       TTFT50 TTFT95  J/day %batt quality cloud%   위반
cloud          0.74   2.39   95.3  2.3%   0.858    77%   18
device         0.44   0.51  208.7  5.0%   0.800     0%    0
routed         0.44   0.77  178.3  4.3%   0.867    19%    0
routed τ=0.4   0.44   0.64  198.4  4.8%   0.825     6%    0
routed τ=0.5   0.44   0.69  190.7  4.6%   0.842    11%    0
routed τ=0.7   0.44   0.85  172.4  4.1%   0.867    25%    0
routed τ=0.8   0.45   0.97  155.0  3.7%   0.875    41%    0
```

출력에서 볼 것:

- **cloud 전용**: 배터리는 가장 적게 쓴다(95 J/day, 2.3%). D7 6.3절의 결론(LLM은 오프로드가 배터리에 싸다)과 같다. 그러나 TTFT p95가 2.39 s(poor 연결의 RTT 꼬리 + 오프라인 timeout), 오프라인 10%는 실패, 프라이버시 질의 18건이 기기 밖으로 나간다.
- **device 전용**: TTFT p95 0.51 s로 가장 빠르고 위반 0이지만, 배터리를 두 배 넘게 쓰고(209 J, 5.0%) 어려운 질의를 틀려서 품질이 0.800이다.
- **routed(τ = 0.6)**: TTFT p95 0.77 s, 품질 0.867(셋 중 최고), 위반 0, 배터리 4.3%. 클라우드로는 19%만 간다.
- τ를 0.4 → 0.8로 올리면 클라우드 비율 6% → 41%, 배터리 4.8% → 3.7%, 품질 0.825 → 0.875, TTFT p95 0.64 → 0.97 s. **τ 하나가 세 축의 trade-off 손잡이**다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 360">
<text x="340" y="20" font-size="14" text-anchor="middle">하루 질의 라우팅 정책 비교 (예제 11, 가정 상수)</text> <line x1="70" y1="280" x2="610" y2="280" stroke="currentColor"/> <line x1="70" y1="280" x2="70" y2="40" stroke="currentColor"/> <line x1="70.0" y1="280" x2="70.0" y2="284" stroke="currentColor"/><text x="70.0" y="298" font-size="12" text-anchor="middle">80</text> <line x1="147.1" y1="280" x2="147.1" y2="284" stroke="currentColor"/><text x="147.1" y="298" font-size="12" text-anchor="middle">100</text> <line x1="224.3" y1="280" x2="224.3" y2="284" stroke="currentColor"/><text x="224.3" y="298" font-size="12" text-anchor="middle">120</text> <line x1="301.4" y1="280" x2="301.4" y2="284" stroke="currentColor"/><text x="301.4" y="298" font-size="12" text-anchor="middle">140</text>
<line x1="378.6" y1="280" x2="378.6" y2="284" stroke="currentColor"/><text x="378.6" y="298" font-size="12" text-anchor="middle">160</text> <line x1="455.7" y1="280" x2="455.7" y2="284" stroke="currentColor"/><text x="455.7" y="298" font-size="12" text-anchor="middle">180</text> <line x1="532.9" y1="280" x2="532.9" y2="284" stroke="currentColor"/><text x="532.9" y="298" font-size="12" text-anchor="middle">200</text> <line x1="610.0" y1="280" x2="610.0" y2="284" stroke="currentColor"/><text x="610.0" y="298" font-size="12" text-anchor="middle">220</text> <line x1="66" y1="280.0" x2="70" y2="280.0" stroke="currentColor"/><text x="62" y="284.0" font-size="12" text-anchor="end">0.78</text> <line x1="66" y1="240.0" x2="70" y2="240.0" stroke="currentColor"/><text x="62" y="244.0" font-size="12" text-anchor="end">0.80</text> <line x1="66" y1="200.0" x2="70" y2="200.0" stroke="currentColor"/><text x="62" y="204.0" font-size="12" text-anchor="end">0.82</text>
<line x1="66" y1="160.0" x2="70" y2="160.0" stroke="currentColor"/><text x="62" y="164.0" font-size="12" text-anchor="end">0.84</text> <line x1="66" y1="120.0" x2="70" y2="120.0" stroke="currentColor"/><text x="62" y="124.0" font-size="12" text-anchor="end">0.86</text> <line x1="66" y1="80.0" x2="70" y2="80.0" stroke="currentColor"/><text x="62" y="84.0" font-size="12" text-anchor="end">0.88</text> <line x1="66" y1="40.0" x2="70" y2="40.0" stroke="currentColor"/><text x="62" y="44.0" font-size="12" text-anchor="end">0.90</text> <text x="340" y="318" font-size="13" text-anchor="middle">기기 배터리 에너지 (J/day, 120 질의)</text> <text x="18" y="160" font-size="13" text-anchor="middle" transform="rotate(-90 18 160)">품질 (정답 비율)</text> <polyline points="526.7,190.0 497.0,156.0 449.2,106.0 426.4,106.0 359.3,90.0" fill="none" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5 4"/>
<circle cx="129.0" cy="124.0" r="7" fill="#4a7bd0"/> <text x="139.0" y="114.0" font-size="12" text-anchor="start">cloud · TTFT p95 2.39 s</text> <circle cx="566.4" cy="240.0" r="7" fill="#3f9a6b"/> <text x="556.4" y="230.0" font-size="12" text-anchor="end">device · TTFT p95 0.51 s</text> <circle cx="449.2" cy="106.0" r="7" fill="#e08a3c"/> <text x="459.2" y="96.0" font-size="12" text-anchor="start">routed τ=0.6 · TTFT p95 0.77 s</text> <text x="340.0" y="260.0" font-size="12" text-anchor="middle">점선: routed의 τ = 0.4 → 0.8 (오른쪽 아래 → 왼쪽 위)</text>
<text x="85.4" y="170.0" font-size="12" text-anchor="start">cloud: 프라이버시 위반 18건,</text> <text x="85.4" y="186.0" font-size="12" text-anchor="start">오프라인 10%는 실패</text>
</svg>
```

그림 7 — 정책별 (배터리 에너지, 품질). 점선은 routed 정책에서 τ를 바꾼 궤적이다. cloud는 왼쪽(에너지 적음)에 있지만 품질이 오프라인 실패로 깎이고 지연 꼬리가 길다. device는 오른쪽 아래. routed는 위쪽에서 τ로 에너지와 품질을 고른다.

### 6.3 이 시뮬레이션이 빼먹은 것

- **연결 상태는 시간적으로 뭉친다**: 지하철 10분은 오프라인이 연속된다. 질의마다 독립으로 뽑은 이 모델은 꼬리를 과소평가한다. 실제 라우터는 최근 RTT·실패를 EWMA로 추적하고, 상태 전환에 hysteresis를 둔다(H7 5.2절의 EWMA).
- **라우터 점수의 질이 상한을 정한다**: `conf_easy`가 잡음이 많으면 "어려운데 쉬워 보이는" 질의를 기기가 틀린다(B9 5.3절과 같은 결론).
- **배터리 잔량·발열**: 배터리 15% 이하나 피부 온도 한계(D7 7절)에서는 τ를 올려 클라우드로 더 보낸다.
- **speculative 실행**: 애매한 질의는 기기와 클라우드를 동시에 시작하고 먼저 온 좋은 답을 쓴다 — 지연은 줄지만 에너지는 둘 다 낸다(L6).

---

## 7. 제스처 / IMU 경로와 엔진 공유

### 7.1 같은 cascade, 다른 센서

IMU 제스처도 cascade다. 1단은 **센서 자체**에 있다. 대부분의 MEMS 가속도계는 wake-on-motion(WoM) 인터럽트를 갖고 있어서, 가속도 변화가 문턱을 넘으면 MCU를 깨운다(G2). 1단 비용은 센서 저전력 모드의 수 µA급이다(데이터시트 typical). 숫자로 따라가 보자(가정).

```
1단 센서 WoM          : 항상, 수 µA 급 (센서 전류에 포함)
2단 MCU 제스처 분류    : WoM 인터럽트 2,000회/day × (5 mW × 30 ms = 0.15 mJ) = 0.30 J/day ≈ 0.0035 mW
3단 AP 동작 실행       : 진짜 제스처 50회 + false 제스처 n회 × 0.5 J
   n = 10/day        → 30 J/day ≈ 0.35 mW
   n = 100/day       → 75 J/day ≈ 0.87 mW
```

말로 하면: 음성과 똑같이 **false accept가 비싼 단계(AP)를 깨우는 횟수**가 전력을 정한다. 3절 optimizer는 단계 정의(`mu`, `e_next`)만 바꾸면 제스처 cascade에도 그대로 쓴다.

두 cascade는 서로 정보를 줄 수도 있다. 기기를 벗어 놓았으면(착용 감지) KWS 1단 문턱을 올리거나 끄고, 손목을 들어 올린 직후 3초 동안은 "raise-to-speak"처럼 1단 문턱을 낮춘다. 맥락이 사전확률을 바꾸므로(A2 9절) 같은 FA 예산에서 miss를 줄일 수 있다.

### 7.2 코드로 확인 — MCU 하나에 두 일

MCU가 KWS를 20 ms 오디오 프레임마다 8 ms씩 돌린다(다음 프레임 전에 끝내야 한다 — 끝내지 못하면 DMA가 버퍼를 덮어쓴다). 여기에 움직임 인터럽트(평균 2초에 한 번)마다 30 ms짜리 제스처 분류를 얹는다. 세 가지 스케줄링을 비교한다.

- **superloop**: 시작한 일을 끝까지(run-to-completion). 흔한 bare-metal 구조.
- **chunked**: superloop이되 제스처 분류를 5 ms 조각으로 나눠 조각마다 양보한다(cooperative).
- **preemptive**: RTOS 우선순위 선점, KWS > gesture.

```python
# MCU 한 개에 KWS(20 ms마다 8 ms, deadline = 다음 프레임)와 제스처 분류(움직임 때 30 ms)를 같이 올리면?
import numpy as np
def sim(policy, T=60_000, seed=3):                      # 1 ms 단위, 60 s. superloop = run-to-completion
    rng = np.random.default_rng(seed)
    motion = set(np.flatnonzero(rng.random(T) < 1 / 2000))  # wake-on-motion 인터럽트, 평균 2 s에 한 번
    kws_left, kws_due, miss = 0, 0, 0
    g_queue, g_lat, running = [], [], None             # running: 비선점 정책에서 지금 잡고 있는 job
    for t in range(T):
        if t % 20 == 0:                                # 새 오디오 프레임
            if kws_left > 0: miss += 1                 # 이전 프레임을 못 끝냄 → 버퍼 덮어씀
            kws_left, kws_due = 8, t + 20
        if t in motion: g_queue.append([30, t])
        if policy == "preemptive" or running is None:  # 선점형: 매 tick 우선순위(KWS > gesture)로 다시 고름
            running = "kws" if kws_left else ("g" if g_queue else None)
        if running == "kws":
            kws_left -= 1
            if kws_left == 0: running = None
        elif running == "g":
            g_queue[0][0] -= 1
            if g_queue[0][0] == 0: g_lat.append(t + 1 - g_queue.pop(0)[1]); running = None
            elif policy == "chunked" and g_queue[0][0] % 5 == 0: running = None   # 5 ms마다 양보
    return miss, np.percentile(g_lat, 50), np.percentile(g_lat, 95), len(g_lat)
for p in ("superloop", "chunked", "preemptive"):
    m, p50, p95, n = sim(p)
    print(f"{p:11s} KWS 프레임 놓침 {m:3d}/3000   제스처 {n}회 지연 p50 {p50:.0f} ms  p95 {p95:.0f} ms")
```

```text
superloop   KWS 프레임 놓침  37/3000   제스처 37회 지연 p50 30 ms  p95 37 ms
chunked     KWS 프레임 놓침   0/3000   제스처 37회 지연 p50 46 ms  p95 53 ms
preemptive  KWS 프레임 놓침   0/3000   제스처 37회 지연 p50 47 ms  p95 54 ms
```

출력에서 볼 것:

- **superloop**: 제스처가 올 때마다(37회) KWS 프레임을 하나씩 놓친다. 30 ms 작업이 20 ms 주기 작업을 막기 때문이다. 놓친 프레임에 호출어 앞부분이 있었다면 그 호출은 miss가 된다 — **cascade의 miss가 모델이 아니라 스케줄러에서 생긴다.**
- **chunked / preemptive**: KWS 놓침 0. 대신 제스처 지연이 30 → 47 ms(p50)로 는다. 제스처 UX(100 ms 이내 반응 정도)에는 충분하다.
- 이용률로 확인: KWS만으로 `8 / 20 = 40%`, 제스처는 `30 ms / 2 s = 1.5%`. 평균 이용률은 낮은데도 superloop은 실패한다. **평균이 아니라 최악의 차단 시간(blocking time)** 이 deadline을 깬다 — Don이 ISR 안에서 오래 걸리는 일을 하지 말라고 하던 이유와 같다(J2).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 250">
<text x="340" y="20" font-size="14" text-anchor="middle">같은 사건 — 5 ms에 움직임 인터럽트, KWS 프레임은 0 / 20 / 40 ms</text> <text x="10" y="69" font-size="13">superloop</text> <text x="10" y="149" font-size="13">preemptive</text> <line x1="90" y1="40" x2="90" y2="185" stroke="#888" stroke-dasharray="4 3"/> <line x1="270" y1="40" x2="270" y2="185" stroke="#888" stroke-dasharray="4 3"/> <line x1="450" y1="40" x2="450" y2="185" stroke="#888" stroke-dasharray="4 3"/> <line x1="630" y1="40" x2="630" y2="185" stroke="#888" stroke-dasharray="4 3"/>
<rect x="90" y="55" width="72" height="24" fill="#4a7bd0" fill-opacity="0.6"/> <text x="126" y="72" font-size="12" text-anchor="middle">KWS 0</text> <rect x="162" y="55" width="270" height="24" fill="#e08a3c" fill-opacity="0.6"/> <text x="297" y="72" font-size="12" text-anchor="middle">gesture 30 ms (중간에 못 끊음)</text> <rect x="432" y="55" width="72" height="24" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="468" y="72" font-size="12" text-anchor="middle">KWS 20</text> <text x="450" y="50" font-size="13" text-anchor="middle">✕</text>
<text x="440" y="97" font-size="12">40 ms: 프레임 20 덮어씀 (miss)</text> <rect x="90" y="135" width="72" height="24" fill="#4a7bd0" fill-opacity="0.6"/> <text x="126" y="152" font-size="12" text-anchor="middle">KWS 0</text> <rect x="162" y="135" width="108" height="24" fill="#e08a3c" fill-opacity="0.6"/> <text x="216" y="152" font-size="12" text-anchor="middle">gesture</text> <rect x="270" y="135" width="72" height="24" fill="#4a7bd0" fill-opacity="0.6"/> <text x="306" y="152" font-size="12" text-anchor="middle">KWS 20</text>
<rect x="342" y="135" width="108" height="24" fill="#e08a3c" fill-opacity="0.6"/> <text x="396" y="152" font-size="12" text-anchor="middle">gesture</text> <rect x="450" y="135" width="72" height="24" fill="#4a7bd0" fill-opacity="0.6"/> <text x="486" y="152" font-size="12" text-anchor="middle">KWS 40</text> <rect x="522" y="135" width="54" height="24" fill="#e08a3c" fill-opacity="0.6"/> <text x="549" y="176" font-size="12" text-anchor="middle">54 ms 완료</text> <polygon points="135,92 130,100 140,100" fill="#3f9a6b"/>
<text x="145" y="101" font-size="12">움직임 5 ms</text> <line x1="90" y1="205" x2="630" y2="205" stroke="currentColor"/> <line x1="90" y1="205" x2="90" y2="210" stroke="currentColor"/><text x="90" y="225" font-size="12" text-anchor="middle">0</text> <line x1="180" y1="205" x2="180" y2="210" stroke="currentColor"/><text x="180" y="225" font-size="12" text-anchor="middle">10</text> <line x1="270" y1="205" x2="270" y2="210" stroke="currentColor"/><text x="270" y="225" font-size="12" text-anchor="middle">20</text> <line x1="360" y1="205" x2="360" y2="210" stroke="currentColor"/><text x="360" y="225" font-size="12" text-anchor="middle">30</text> <line x1="450" y1="205" x2="450" y2="210" stroke="currentColor"/><text x="450" y="225" font-size="12" text-anchor="middle">40</text>
<line x1="540" y1="205" x2="540" y2="210" stroke="currentColor"/><text x="540" y="225" font-size="12" text-anchor="middle">50</text> <line x1="630" y1="205" x2="630" y2="210" stroke="currentColor"/><text x="630" y="225" font-size="12" text-anchor="middle">60 ms</text>
</svg>
```

그림 8 — 한 번의 제스처 사건을 두 스케줄러로 본 타임라인. 위(superloop)는 30 ms 제스처 작업이 20 ms 프레임의 KWS를 밀어내 40 ms에 프레임을 잃는다. 아래(preemptive)는 KWS가 프레임마다 제스처를 선점하고, 제스처는 54 ms에 끝난다(도착 후 49 ms).

### 7.3 다른 엔진에서의 충돌

- **DSP**: 음성 세션 중에는 DSP가 beamforming·AEC·noise suppression을 돌린다(G5, B5 6절). 이때 2단 KWS 검증이 같은 DSP에서 돌면 세션 중 "다시 호출" 감지(barge-in)의 지연이 늘어난다. 우선순위와 메모리(TCM) 분할을 미리 정한다.
- **AP/NPU**: 두 cascade 모두 마지막에 AP를 깨운다. 이미 깨어 있으면 제스처 동작은 **편승**한다(깨우기 비용 0). 짧은 시간 안의 사건들을 묶어(batching) AP 깨우기를 합치는 것도 E8 5절의 tail 비용을 줄이는 방법이다.
- **공유 자원 잠금**: I2C/SPI 버스 하나에 IMU와 오디오 코덱 설정이 같이 있으면, 버스 트랜잭션도 스케줄링 대상이다(G2). Don이 버스 장애를 추적하던 경험이 그대로 쓰인다.

---

## 8. 실전 문제 — 펌웨어에서 cascade가 깨지는 곳

### 8.1 hysteresis, smoothing, refractory

점수는 프레임마다(10 ms) 나온다. 문턱 근처에서 점수가 흔들리면 같은 호출어가 여러 번 trigger된다. 세 가지 장치를 쓴다.

- **smoothing**: 최근 몇 프레임의 이동평균으로 판정한다(B5 3.4절).
- **hysteresis**: 켜짐 문턱(0.6)과 다시 무장(re-arm) 문턱(0.4)을 다르게 둔다. 점수가 0.4 아래로 충분히 내려가야 다음 trigger가 가능하다. Schmitt trigger와 같다.
- **refractory**: trigger 후 일정 시간(0.8 s) 동안의 새 trigger를 무시한다. 같은 발화의 메아리(반향)나 사용자가 호출어를 반복하는 경우를 막는다. 무시한 횟수도 세서 telemetry로 남긴다.

다음 C 코드는 이 세 가지를 갖춘 판정기를 단계 하나로 만들고, 단순 문턱 비교와 비교한다.

```c
#include <stdint.h>
#include <stdio.h>

typedef struct {                       /* 한 단계의 "통과 판정기" — 설정 + 상태 + telemetry */
    int16_t  th_on, th_off;            /* Q8 (256 = 1.0): 켜짐/다시 무장 임계값 (hysteresis) */
    uint16_t refractory;               /* 발화 후 무시할 frame 수 */
    int32_t  acc; int16_t hist[4]; uint8_t idx;   /* 4-frame 이동평균 */
    uint8_t  armed; uint16_t cooldown;
    uint32_t n_frames, n_fire, n_suppressed;      /* telemetry 카운터 */
} gate_t;

static int gate_step(gate_t *g, int16_t score_q8)
{
    g->acc += score_q8 - g->hist[g->idx];
    g->hist[g->idx] = score_q8;
    g->idx = (uint8_t)((g->idx + 1u) & 3u);
    int16_t s = (int16_t)(g->acc / 4);
    g->n_frames++;
    if (g->cooldown) g->cooldown--;
    if (g->armed && s >= g->th_on) {   /* rising edge 한 번만 */
        g->armed = 0;
        if (g->cooldown) { g->n_suppressed++; return 0; }
        g->cooldown = g->refractory; g->n_fire++;
        return 1;
    }
    if (!g->armed && s < g->th_off) g->armed = 1;   /* 충분히 내려가야 다시 무장 */
    return 0;
}

static int16_t score_at(int t)         /* 10 ms frame 점수 (Q8): 호출어 3번 + 경계에서 흔들림 */
{
    if (t >= 20 && t < 28) return 230;                       /* 진짜 호출어 */
    if (t >= 28 && t < 40) return (t & 1) ? 170 : 140;       /* 꼬리에서 0.55~0.66 사이 흔들림 */
    if (t >= 55 && t < 61) return 220;                       /* 0.35 s 뒤 같은 발화의 메아리 */
    if (t >= 150 && t < 158) return 235;                     /* 1.3 s 뒤 진짜 두 번째 호출 */
    return 60 + (t * 37 % 23);                               /* 배경 0.23~0.32 */
}

int main(void)
{
    gate_t g = { .th_on = 154, .th_off = 102, .refractory = 80, .armed = 1 };  /* 0.6 / 0.4, 0.8 s */
    int naive = 0, prev = 0;
    for (int t = 0; t < 200; t++) {
        int16_t x = score_at(t);
        if (gate_step(&g, x)) printf("fire @ frame %d (%d ms)\n", t, t * 10);
        int above = x >= 154;                                /* 비교: 평균·hysteresis·refractory 없음 */
        naive += above && !prev; prev = above;
    }
    printf("gate: fire=%u suppressed=%u frames=%u | naive rising edges=%d\n",
           (unsigned)g.n_fire, (unsigned)g.n_suppressed, (unsigned)g.n_frames, naive);
    return 0;
}
```

```text
fire @ frame 22 (220 ms)
fire @ frame 152 (1520 ms)
gate: fire=2 suppressed=1 frames=200 | naive rising edges=9
```

출력에서 볼 것: 같은 점수 열에서 단순 문턱은 rising edge를 **9번** 센다(호출어 꼬리의 흔들림 6번 포함). 판정기는 진짜 호출 두 번(220 ms, 1,520 ms)만 발화하고, 0.35초 뒤의 메아리 1건은 refractory로 막고 `suppressed`로 센다. 이 9 vs 2의 차이가 곧 2단 DSP를 깨우는 횟수 차이다.

함정: refractory를 너무 길게 하면 사용자가 "Hey Hark … Hey Hark"처럼 다시 부를 때 놓친다. 0.5–1 s 정도에서 시작해 필드 telemetry의 `retry` 패턴(8.5절)으로 조정한다.

### 8.2 단계 넘김 지연과 pre-roll 버퍼

단계가 깨어나는 동안에도 오디오는 계속 들어온다. 뒤 단계가 **호출어의 시작부터** 보려면 앞 단계가 오디오를 링버퍼에 들고 있어야 한다(B5 8.2절, H6 5.3절). 필요한 길이를 손으로 계산한다(가정).

```
t = 0.0 s        "Hey Hark" 시작 (0.8 s 길이)
t = 1.0 s        1단 결정 (발화 끝 + smoothing 지연 0.2 s)
t = 1.0 → 1.4 s  DSP 깨우기 5 ms + 검증 0.4 s
t = 1.4 → 1.7 s  AP resume 0.3 s
ASR이 필요한 오디오 = 호출어 0.3 s 전 ~ 지금 = −0.3 ~ 1.7 s = 2.0 s
16 kHz × 16 bit = 32 KB/s  →  2.0 s × 32 KB/s = 64 KB 링버퍼 (always-on SRAM)
```

```
시간 →   -0.3     0.0             0.8   1.0        1.4        1.7
오디오   [pre-roll][== Hey Hark ==][== 명령 "내일 날씨" 계속 … ======>
MCU                               └ 1단 결정
DSP                                     [ 깨우기+검증 ]
AP                                                  [resume]  └ ASR이 링버퍼를 -0.3 s부터 읽기 시작
링버퍼   <────────────────── 최소 2.0 s 보존 ──────────────────>
```

말로 하면: 단계가 많을수록, 각 단계의 깨우기가 느릴수록 링버퍼가 길어진다. 3단 cascade는 2단보다 전력이 싸지만 **SRAM 64 KB**라는 비용을 always-on 도메인에 남긴다. 줄이는 방법: (1) log-mel로 저장(40 B/frame × 100 frame/s = 4 KB/s → 8 KB, 단 ASR이 같은 특징을 쓸 때만), (2) AP를 2단 검증과 **병렬로** 미리 깨우기 시작(검증이 실패하면 깨우기 비용을 버린다 — 3절 최적화에 `E_next`가 바뀌는 선택지로 넣는다), (3) 코덱 압축.

지연 관점에서 사용자가 느끼는 것은 "호출어 끝 → 반응 시작"이다. 위 예에서 `1.7 − 0.8 = 0.9 s`에 ASR 시작, 그 뒤 ASR·LLM 지연이 더해진다(L4의 단계별 지연 예산). 3단 검증이 이 경로를 막는다면(검증 통과 전에 반응을 시작하지 않는다면) 그 시간도 더해진다. 그래서 3단 검증은 보통 **streaming ASR 자체**로 한다 — ASR 결과가 호출어로 시작하지 않으면 조용히 세션을 버린다.

### 8.3 상태 넘김 (state carry-over)

다음 단계가 처음부터 다시 계산하지 않도록 넘겨야 할 것들이다.

| 넘길 것 | 왜 | 없으면 |
|---|---|---|
| 호출어 시작·끝 시각 (공통 시간축) | 2·3단이 어디부터 볼지 | 3단이 링버퍼 전체를 다시 훑음 |
| 링버퍼 읽기 위치 | 오디오 복사 없이 공유 | 64 KB 복사 (E8 3.3절 SPSC 규칙으로 해결) |
| 이미 계산한 특징 (log-mel) | 2단이 같은 특징이면 재사용 | front-end 중복 계산 |
| 단계별 점수 | telemetry, 3단 판정의 사전 정보 | 필드에서 어느 단이 문제인지 모름 |
| 소음 대역, 사용한 임계값 버전 | 재현·디버깅 | "왜 켜졌나"를 설명 못 함 |
| beamformer 방향(DOA), 화자 임베딩 | ASR 전처리, 화자 확인 | 세션 시작 후 다시 수렴 |

9절의 C 구조체가 이 메시지의 예다.

### 8.4 cascade 테스트 — 시스템 수준 FAR은 길게 재야 한다

2절과 4.3절의 결론: **단계별 FPR을 곱해서 전체 FAR을 추정하면 안 된다.** 전체를 끝까지(end-to-end) 돌려서 잰다. 문제는 사건이 드물다는 것이다. A4 9.2절의 Poisson 상한을 cascade 목표에 적용한다.

```python
# cascade FAR 측정 계획: 얼마나 긴 negative 오디오가 필요한가 + trigger replay로 아끼는 계산
from scipy.stats import chi2
ACTIVE_H = 25_920 / 3600                              # 우리 모델의 하루 = 말소리·소음 7.2 h (= 하루의 30%)
target_per_h = 1 / ACTIVE_H                            # 목표: FA ≤ 1회/day → active 오디오 시간당
for k in (0, 1, 2, 5):                                 # 테스트 중 관찰된 FA 횟수
    ub_events = chi2.ppf(0.95, 2 * (k + 1)) / 2        # Poisson 95% 단측 상한 (사건 수)
    h = ub_events / target_per_h
    print(f"FA {k}회 관찰 → 상한 {ub_events:5.2f}회 → 필요 active {h:5.1f} h = 일상 녹음 {h / 0.3:5.0f} h")
# trigger replay: 1단(MCU)만 긴 오디오에 한 번 돌리고, 그 trigger 구간만 저장해 2·3단 튜닝에 반복 사용
hours, fpr1, win_per_h = 1000, 0.036, 3600            # 1000 h negative, 1단 통과율(앞 최적점 ≈ 3.6%)
trig = hours * win_per_h * fpr1
print(f"\n1단 trigger 수 = {trig:,.0f} 개 (전체 창 {hours * win_per_h:,} 의 {fpr1:.1%})")
print(f"2·3단 튜닝 1회당 평가할 창: 전체 재생 {hours * win_per_h:,} → replay {trig:,.0f} ({1 / fpr1:.0f}배 절약)")
print(f"주의: θ1을 낮추려면 replay 세트를 다시 만들어야 한다 (저장된 trigger는 θ1 이상만 포함)")
```

```text
FA 0회 관찰 → 상한  3.00회 → 필요 active  21.6 h = 일상 녹음    72 h
FA 1회 관찰 → 상한  4.74회 → 필요 active  34.2 h = 일상 녹음   114 h
FA 2회 관찰 → 상한  6.30회 → 필요 active  45.3 h = 일상 녹음   151 h
FA 5회 관찰 → 상한 10.51회 → 필요 active  75.7 h = 일상 녹음   252 h

1단 trigger 수 = 129,600 개 (전체 창 3,600,000 의 3.6%)
2·3단 튜닝 1회당 평가할 창: 전체 재생 3,600,000 → replay 129,600 (28배 절약)
주의: θ1을 낮추려면 replay 세트를 다시 만들어야 한다 (저장된 trigger는 θ1 이상만 포함)
```

출력에서 볼 것:

- FA 0회를 관찰해도 "하루 1회 이하"를 95% 신뢰로 말하려면 말소리 21.6시간 = 일상 녹음 **72시간**이 필요하다(A4의 72시간과 같은 숫자). FA를 1–2회 보면 114–151시간으로 늘어난다.
- **trigger replay**: 1단(MCU 모델의 bit-exact 시뮬레이션)만 1,000시간 녹음에 한 번 돌리고 trigger 구간(전체의 3.6%)만 저장하면, 2·3단 임계값을 바꿀 때마다 28배 적은 데이터로 end-to-end FAR을 다시 잴 수 있다. 상관도 그대로 보존된다 — 실제 1단이 고른 창이기 때문이다.
- 한계: θ1을 **낮추는** 실험은 저장된 trigger 밖의 창이 필요하므로 replay 세트를 다시 만든다. 그래서 replay 세트는 운영 θ1보다 조금 낮은 문턱으로 뽑아 둔다.

negative 녹음 구성도 중요하다. TV·팟캐스트·음악(가사), 다국어 대화, 호출어와 발음이 비슷한 단어 목록, 기기 착용자의 자기 목소리(골전도·근접 마이크 특성), 차·카페 잡음. 그리고 **실제 기기에서**(HIL, J6) 돌려야 전원 상태 전환·pre-roll·스케줄러 문제까지 잡힌다. 7.2절의 superloop miss는 PC 시뮬레이션에서는 절대 보이지 않는다.

### 8.5 필드 telemetry로 임계값 다시 맞추기

출시 후 사용자 환경은 학습 데이터와 다르다(H7 4절의 drift). 기기에 다음 **카운터**만 남겨도(오디오 없이) 많은 것을 알 수 있다.

| 카운터 (시간당) | 무엇의 proxy인가 |
|---|---|
| 1단 trigger 수 | 환경 소음·말소리 양, θ1이 너무 낮은지 |
| 2단 통과 수, 3단 통과 수 | 단계별 조건부 통과율 → 상관 추정 |
| 세션 시작 2초 안에 사용자가 취소 | false accept |
| 5초 안에 다시 호출 시도 (앞 시도는 1단 근처 점수로 실패) | miss |
| refractory로 막은 수 | 반향·반복 호출 |

재튜닝 루프: (1) 코호트별 카운터를 서버에서 집계(H8 3절 cohorting), (2) 3절 optimizer의 상수(창 수, 단계별 통과율)를 필드 값으로 갱신해 새 θ 계산, (3) 원격 config로 일부 코호트에만 배포(H8 2절 staged rollout + kill switch), (4) false accept proxy와 miss proxy가 둘 다 나빠지지 않는지 확인 후 확대. 환경별로 θ를 바꾸는 표(조용함·보통·시끄러움)를 두면 기기가 소음 수준에 따라 행을 고른다(9절). 집계 카운터에도 프라이버시 예산이 필요하면 H6 6절의 차등 프라이버시를 쓴다.

---

## 9. 임베디드 관점에서 다시 보기

펌웨어 쪽에서 cascade는 세 가지 데이터 구조로 드러난다. (1) OTA로 바꿀 수 있는 **임계값 표**, (2) 단계 사이를 오가는 **고정 크기 메시지**, (3) **telemetry 카운터**. 크기를 컴파일 타임에 고정해 IPC(E8 3절)와 원격 config(H8 2.6절)에 그대로 쓴다.

```c
#include <stdint.h>
#include <stdio.h>

typedef struct {                 /* OTA로 바꿀 수 있는 cascade 설정 (H8 원격 config) */
    uint16_t version;
    int16_t  th_q8[3][3];        /* [소음 대역 조용/보통/시끄러움][단계] Q8 임계값 */
    uint16_t refractory_ms, preroll_ms;
    uint8_t  noise_db_edge[2];   /* 소음 대역 경계 (dBA) */
    uint8_t  pad[2];
} cascade_cfg_t;

typedef struct {                 /* 단계 → 다음 단계로 넘기는 메시지 (재계산 방지) */
    uint32_t t_kw_start_ms, t_kw_end_ms;   /* 공통 시간축(G7) 기준 호출어 구간 */
    uint32_t ring_wr_idx;                  /* pre-roll 링버퍼에서 읽기 시작할 위치 */
    int16_t  score_q8[3];                  /* 지금까지 단계별 점수 (텔레메트리·로그용) */
    uint8_t  noise_band, stage;            /* 사용한 임계값 표의 행, 보낸 단계 */
} wake_msg_t;

typedef struct { uint32_t trig[3], pass_final, user_cancel_2s, retry_5s; } cascade_cnt_t;

_Static_assert(sizeof(wake_msg_t) == 20, "IPC 메시지 크기 고정");
_Static_assert(sizeof(cascade_cfg_t) % 4 == 0, "config는 4바이트 정렬");

int main(void)
{
    cascade_cfg_t cfg = { .version = 7, .th_q8 = { {120, 230, 240}, {140, 240, 250}, {170, 255, 265} },
                          .refractory_ms = 800, .preroll_ms = 300, .noise_db_edge = {45, 65} };
    uint8_t db = 58;                       /* 지금 측정한 소음 */
    uint8_t band = (uint8_t)((db >= cfg.noise_db_edge[0]) + (db >= cfg.noise_db_edge[1]));
    printf("cfg %zu B, msg %zu B, counters %zu B\n", sizeof cfg, sizeof(wake_msg_t), sizeof(cascade_cnt_t));
    printf("noise %u dB → band %u → θ = %.2f / %.2f / %.2f\n", db, band,
           cfg.th_q8[band][0] / 256.0, cfg.th_q8[band][1] / 256.0, cfg.th_q8[band][2] / 256.0);
    return 0;
}
```

```text
cfg 28 B, msg 20 B, counters 24 B
noise 58 dB → band 1 → θ = 0.55 / 0.94 / 0.98
```

출력에서 볼 것: config 28 B, 메시지 20 B, 카운터 24 B. `_Static_assert`로 메시지 크기를 고정해 두면 MCU·DSP·AP가 서로 다른 컴파일러로 빌드돼도 layout이 어긋나지 않는다. 소음 58 dB는 "보통" 행을 골라 θ = 0.55 / 0.94 / 0.98(Q8 → 실수)을 쓴다. 3절 optimizer가 낸 z 단위 θ는 검증 세트의 점수 분위수로 바꿔 이 표에 넣는다.

그 밖의 체크포인트:

- **임계값 비교는 정수로**: Q8 점수와 Q8 임계값을 정수 비교. float 변환 비용과 플랫폼마다 다른 반올림을 피한다.
- **단계마다 같은 시간축**: 1단 MCU 타임스탬프와 AP 타임스탬프가 다른 시계면 pre-roll 위치가 틀린다(G7).
- **전원 상태 머신과 함께 설계**: `E_next`의 대부분은 전이 비용이다. 다음 단계를 "깨울지 말지"뿐 아니라 "얼마나 깊이 재울지"(E9 4절 손익분기)도 cascade 트래픽(하루 936회 = 평균 92초 간격)에 맞춰 정한다.
- **bit-exact 1단 시뮬레이터**: 8.4절 trigger replay는 PC에서 돈 1단이 기기와 같은 trigger를 낼 때만 유효하다(J6 golden vector).

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 단계별 FPR을 곱해서 전체 FAR 추정 | 필드 FA가 설계보다 10배 이상 | 단계 오류 상관 (2절) | end-to-end로 측정, 조건부 FPR은 1단 trigger 세트에서 잰다 |
| 임계값을 단계마다 따로 EER에 맞춤 | 전력 또는 miss가 불필요하게 큼 | 단계 간 트래픽 상호작용 무시 | 3절처럼 공동 최적화 |
| 1단 문턱을 너무 낮게 (recall 100% 집착) | DSP가 수천 번 깨어 전력 폭증 | miss 예산을 1단에 안 씀 | optimizer로 miss 예산 배분 |
| 2단 검증기를 정확도만 보고 키움 | 정확도는 올랐는데 전력이 늘어남 | 1회 비용 증가가 수확 체감을 넘음 (4절) | 각 안을 재최적화한 전력으로 비교 |
| hysteresis·refractory 없음 | 한 번 말했는데 2단이 여러 번 깨어남 | 문턱 근처 점수 흔들림, 반향 | 이동평균 + 두 문턱 + refractory |
| pre-roll이 짧음 | ASR이 호출어 뒤 첫 단어를 자름 | 단계 깨우기 지연 합을 잘못 계산 | 8.2절처럼 최악 지연 합으로 링버퍼 크기 결정 |
| run-to-completion 루프에 긴 작업 추가 | 제스처 직후 호출어를 가끔 놓침 | KWS 프레임 deadline miss (7.2절) | 선점형 우선순위 또는 작업 쪼개기 |
| 중간층에서 split | 링크 에너지가 오히려 증가 | 앞쪽 층 activation이 입력보다 큼 | 층별 bytes 측정, bottleneck 설계, 양 끝 split 먼저 검토 |
| split 두 반쪽의 버전 불일치 | OTA 후 조용히 정확도 하락 | 기기·서버 모델 쌍이 어긋남 | 메시지에 모델 버전, 서버가 여러 버전 지원 |
| 클라우드 라우팅을 연결 상태 무시하고 고정 | 지하철에서 응답 2초 이상, 실패 | RTT 꼬리, 오프라인 timeout | 연결 추정 + τ 조정 + 기기 fallback |

---

## 11. 면접에서 이렇게 말한다

**Q.** "Design a wake-word cascade for a wearable and explain how you would choose the thresholds."

**A.** 단계를 비용과 품질로 요약한다: always-on MCU KWS(약 1 mW, 약한 검출기), DSP 검증기(회당 15 mJ, 강함), AP의 ASR 검증. 그다음 제약(miss ≤ 5%, FA ≤ 1/day)을 두고 평균 전력을 최소화하는 문제로 세 임계값을 **함께** 고른다. 전체 FA와 miss는 상관을 넣은 모델로 계산하고, 결과는 1단 trigger replay와 72시간 이상의 end-to-end 녹음으로 검증한다.

> "I treat the cascade as one optimization problem. Each stage is summarized by its activation energy, latency and ROC, and I pick all thresholds jointly to minimize average power subject to a miss-rate target and a false-wakes-per-day budget. In a typical design the first stage stays permissive — it passes a few percent of speech — because the cheap DSP verifier does the heavy rejection, and the optimizer tells me exactly how to split the miss budget across stages. Then I validate end to end on long negative audio rather than multiplying per-stage rates."

**Q.** "Why do correlated errors between stages matter?"

**A.** 단계들이 같은 특징·같은 데이터로 학습되면 같은 소리에 함께 속는다. 각 단 FPR이 1%여도 상관 0.5면 둘 다 통과할 확률이 독립 가정의 13배다. 단계별 ROC에는 상관이 안 보이므로, 독립 가정으로 정한 임계값은 필드에서 FA 목표를 수십~수백 배 넘길 수 있다. 측정은 1단을 통과한 창에서 2단 FPR을 재는 식으로 한다.

> "Because the stages see the same audio, the same front-end features, and often the same training data, they fail on the same confusable sounds. The per-stage ROC curves look identical whether the stages are independent or not, but the joint false-accept rate can be ten or more times higher. If I tune thresholds under an independence assumption, I can be off by orders of magnitude in the field, so I measure conditional rates on stage-one triggers and diversify the verifier with hard negatives."

**Q.** "How do you measure the false accept rate of a cascade?"

**A.** end-to-end로, 긴 negative 오디오에서 잰다. FA 0회로 하루 1회 이하를 95% 신뢰로 말하려면 일상 녹음 약 72시간이 필요하다. 반복 튜닝은 1단 trigger를 저장해 두고 2·3단만 다시 돌리는 replay로 28배 정도 줄인다. 최종은 실제 기기에서 돌려 전원 전환·스케줄링 문제까지 포함한다.

> "End to end, on long and diverse negative audio — TV, podcasts, music, conversations, near-miss words — never by multiplying per-stage rates. False accepts are Poisson, so seeing zero events in T hours only bounds the rate at about three over T; for one false wake per day I need roughly 72 hours of daily-life audio. For iteration I record stage-one triggers once and replay only those through the later stages, and the final sign-off runs on real hardware."

**Q.** "How do you decide where to split a model between an MCU or DSP and an NPU?"

**A.** forward hook으로 층별 MAC·activation 바이트·weight를 뽑고, `A 계산 + 링크(고정비 + 바이트 × 단가) + B 계산 + B 깨우기` 비용을 split 지점마다 계산해 메모리·deadline 제약 안에서 최소를 고른다. 작은 CNN은 앞쪽 activation이 입력보다 커서 보통 양 끝이 최적이고, B가 이미 깨어 있는지가 답을 바꾼다. 중간 split은 bottleneck을 학습시켜 보낼 텐서를 줄일 때 이긴다.

> "I profile the model per layer — MACs, activation bytes, weights — and evaluate a simple cost model at every cut: compute on engine A, plus link fixed cost and bytes times energy per byte, plus compute and wake-up cost on engine B, subject to A's memory and the deadline. For small CNNs the early activations are larger than the input, so the optimum is usually at one end, and whether the NPU is already awake often flips the answer. A middle split only wins when I train a bottleneck so the tensor I ship is much smaller than the input."

**Q.** "When should the device send a query to the cloud?"

**A.** 프라이버시와 오프라인은 무조건 기기. 나머지는 라우터 점수(쉬워 보이는지)와 연결 상태로 정하고, 연결이 나쁘면 문턱을 기기 쪽으로 옮긴다. 시뮬레이션에서 클라우드 전용은 배터리는 가장 싸지만 지연 꼬리·오프라인 실패·프라이버시 위반이 있고, 기기 전용은 빠르지만 배터리 두 배·품질 저하, 라우팅이 품질과 지연을 같이 잡았다.

> "Hard constraints first: private data or no connectivity means on-device. Otherwise I route on a cheap difficulty or confidence score, shifted by link quality, with a timeout fallback to the device. When I simulated a day of queries, always-cloud used the least battery but had a long latency tail, offline failures and privacy violations; always-device was fastest to first token but used twice the energy and lost quality on hard queries. A confidence-routed policy sent about a fifth of queries to the cloud and got the best quality with sub-second p95 time to first token."

**Q.** "Your IMU gesture classifier and the KWS run on the same MCU. What can go wrong?"

**A.** 평균 이용률이 낮아도 run-to-completion 루프에서 30 ms 제스처 작업이 20 ms 주기 KWS 프레임을 밀어내면 프레임을 잃고, 그 순간 호출어는 miss가 된다. KWS를 높은 우선순위로 선점시키거나 제스처 작업을 쪼갠다. 제스처 지연은 30 → 47 ms로 늘지만 UX에는 충분하다.

> "The failure is blocking time, not utilization. In a run-to-completion loop a 30-millisecond gesture inference can delay a KWS frame that must finish within 20 milliseconds, the audio buffer gets overwritten, and that shows up as a wake-word miss that no model metric explains. I give the audio path a higher preemptive priority or split the gesture work into small chunks, accepting a few tens of milliseconds more gesture latency."

---

## 12. 직접 해보기

1. (손계산) 1.3절에서 1단 조건부 FPR을 2% → 1%로 바꾸면(TPR은 0.97 → 0.94) 하루 헛일 에너지와 miss는? 정답: 1단 통과 259.2/day → DSP 3.89 J, AP 3.63 J, 세션 0.60 J, 합 94.5 J/day ≈ 1.094 mW, miss = 1 − 0.94 × 0.99 × 0.99 ≈ 7.9%.
2. (손계산) 2단 검증을 없애고 1단 통과(518.4/day)가 바로 AP 검증(0.7 J)으로 가면(AP 조건부 FPR 5%는 그대로라고 가정) 하루 에너지는? 정답: 518.4 × 0.7 J = 362.9 J, 세션 518.4 × 0.05 = 25.9회 × 2.3 J = 59.6 J, always-on 86.4 J → 합 508.9 J/day ≈ 5.89 mW. 배터리 수명 40.5일 → 약 8.2일, FA도 0.52 → 25.9/day.
3. (코드) 3.3절 optimizer에서 FA 목표를 1 → 0.1 → 0.02 → 0.01/day로 줄여 보라. 언제부터 FA 제약이 묶이고 전력은 얼마나 오르나? 정답: 0.1에서는 여전히 FA 0.06으로 안 묶인다(같은 해). 0.02에서 묶이며 θ3이 3.77 → 4.14로 오르고 전력은 1.182 → 1.185 mW, 0.01이면 θ3 4.34, 1.190 mW. 강한 3단 덕분에 FA를 10배 줄이는 비용이 0.7%뿐이다.
4. (코드) 4.3절에서 호출어 쪽 상관과 negative 쪽 상관을 다르게(예: 호출어 ρ = 0.2, negative ρ = 0.7) 두도록 `evaluate`를 고쳐 보라. 힌트: `pass_probs`를 두 번 부를 때 rho 인자를 따로 넘긴다. 상관이 negative 쪽에만 있으면 재최적화 전력이 단조 증가하는지 확인한다.
5. (코드) 5.4절에서 BLE 대신 Wi-Fi(고정 135 mJ, 0.12 µJ/B — D7 6.3절)를 넣으면 MCU → cloud의 최적 split은? 정답: 고정비가 모든 split에 같이 붙으므로 바이트 차이만 남는다. k = 0(4 KB)과 k = 5(12 B)를 비교해 MCU 계산 2.8 mJ vs 바이트 절약 0.48 mJ → 여전히 k = 0.
6. (설계) 8.2절 타임라인에서 AP를 2단 검증과 병렬로 미리 깨우면 pre-roll은 몇 KB로 줄고, 하루 에너지는 얼마나 느나? 정답: AP 준비가 1.4 s로 당겨져 −0.3 ~ 1.4 s = 1.7 s → 54.4 KB. 대신 2단 진입(936/day)마다 AP resume(약 90 mJ)을 내므로 약 84 J/day ≈ +0.98 mW — 대부분의 경우 손해다.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| cascade | 단계형 검출기 | 싼 단계가 먼저 보고 통과한 것만 비싼 단계로 넘긴다 |
| stage threshold θ_k | 단계 문턱 | 점수가 이보다 크면 다음 단계로 넘긴다 |
| E_next | 다음 단계 진입 비용 | 깨우기 전이 + 계산 + tail 에너지 |
| miss / FRR | 놓침 비율 | 진짜 호출어 중 끝까지 통과 못 한 비율 |
| FA/day | 하루 false wake 수 | negative가 모든 단계를 통과한 횟수 |
| d' (d-prime) | 분리도 | negative와 positive 점수 평균의 거리 (표준편차 단위) |
| binding constraint | 묶인 제약 | 최적점에서 등호로 붙어 있는 제약 — 완화하면 비용이 준다 |
| Pareto front | 파레토 경계 | 한 지표를 나쁘게 하지 않고는 다른 지표를 좋게 할 수 없는 점들 |
| 상관 ρ | 단계 점수 상관 | 같은 창에서 두 단계 점수가 함께 움직이는 정도 |
| Gauss–Hermite quadrature | 가우스 적분 공식 | 정규분포에 대한 기댓값을 대표점 몇 개의 가중합으로 계산 |
| SLSQP | 제약 있는 국소 최적화 | scipy의 순차 이차계획법, 부등식 제약 지원 |
| split computing | 모델 분할 실행 | 모델을 층 경계에서 잘라 두 엔진(또는 기기·서버)에서 돌린다 |
| bottleneck | 병목 층 | 보낼 텐서를 줄이려고 경계 앞에 넣는 좁은 층 |
| TTFT | time to first token | 질의 후 첫 토큰(첫 음성)이 나올 때까지 시간 |
| WoM | wake-on-motion | 센서가 움직임을 감지하면 MCU를 깨우는 인터럽트 |
| refractory | 불응기 | trigger 직후 새 trigger를 무시하는 시간 |
| hysteresis | 이력 | 켜짐 문턱과 다시 무장 문턱을 다르게 두어 흔들림 방지 |
| pre-roll | 앞 버퍼 | 결정 전의 오디오를 링버퍼에 보관해 뒤 단계가 처음부터 보게 함 |
| trigger replay | 트리거 재생 | 1단 trigger만 저장해 뒤 단계 튜닝을 빠르게 반복 |
| blocking time | 차단 시간 | 높은 우선순위 작업이 낮은 작업 때문에 기다리는 최악 시간 |

---

## 14. 요약 & 체크리스트

cascade는 "단계마다 (진입 비용, 지연, ROC)를 가진 검출기들의 직렬 시스템"이다. 전체 miss와 FA는 **모든 단계를 통과할 확률**이고, 단계 오류가 상관되면 FPR의 곱이 아니다. 상관을 공유 요인 하나로 모델링하면 통과 확률이 1차원 적분이 되어, 임계값 θ1·θ2·θ3를 격자 + SLSQP로 **함께** 최적화할 수 있다. 공통 사례에서 3단 구조는 miss 5%·FA ≤ 1/day를 1.18 mW로 맞췄고, 엄격한 miss 목표에서 2단 구조보다 크게 유리했으며, 검증기는 "더 정확하게"보다 "충분히 정확하면 더 싸게"가 이기는 구간이 있었다. 모델 분할은 층별 activation 크기와 B 엔진의 깨우기 비용이 답을 정하고, 작은 CNN에서는 양 끝이 보통 최적이며, bottleneck이 중간 split을 살린다. 기기·클라우드 라우팅은 프라이버시·연결을 먼저 거르고 τ 하나로 에너지·품질·지연을 고른다. 펌웨어에서는 hysteresis·refractory·pre-roll·스케줄링이 모델보다 먼저 miss와 FA를 만들 수 있고, 검증은 end-to-end 장시간 녹음과 필드 카운터로 한다.

- [ ] 단계별 조건부 FPR·TPR과 진입 비용으로 하루 헛일 에너지, 평균 mW, FA/day, miss를 손으로 계산할 수 있다
- [ ] 단계 상관이 단계별 ROC에는 안 보이고 joint FPR에만 나타나는 이유를 설명할 수 있다
- [ ] 공유 요인 모델과 Gauss–Hermite 적분으로 "모두 통과할 확률"을 계산하는 코드를 쓸 수 있다
- [ ] 제약(miss, FA/day) 아래 평균 전력을 최소화하는 임계값 공동 최적화를 격자 + SLSQP로 구현하고 Monte Carlo로 검증할 수 있다
- [ ] 1·2·3단 구조를 Pareto front로 비교하고 "3단의 가치는 엄격한 목표에서 커진다"를 숫자로 보일 수 있다
- [ ] 검증기 품질 vs 비용 민감도 표를 읽고 수확 체감 지점을 짚을 수 있다
- [ ] forward hook으로 층별 MAC·activation·weight를 뽑고 split 비용 모델로 최적 split을 고를 수 있다
- [ ] 기기·클라우드 라우팅 정책을 TTFT p50/p95, 에너지, 품질, 프라이버시로 비교할 수 있다
- [ ] superloop에서 긴 작업이 KWS deadline을 깨는 이유와 해결책(선점, 쪼개기)을 설명할 수 있다
- [ ] pre-roll 링버퍼 크기를 단계 지연 합으로 계산하고, cascade FAR 측정에 필요한 녹음 시간을 Poisson 상한으로 계산할 수 있다

---

## 참고 자료

- P. Viola, M. Jones, "Rapid Object Detection using a Boosted Cascade of Simple Features", CVPR 2001 — 단계형 검출기(cascade)의 고전. 단계별 detection·false positive rate의 곱으로 전체를 설계하는 사고방식의 출발점.
- Apple Machine Learning Research, "Hey Siri: An On-device DNN-powered Voice Trigger for Apple's Personal Assistant" (2017) — always-on 저전력 프로세서의 작은 검출기 + 메인 프로세서의 큰 검출기로 된 2단 voice trigger 설명. https://machinelearning.apple.com/research/hey-siri
- Y. Zhang et al., "Hello Edge: Keyword Spotting on Microcontrollers", arXiv:1711.07128 (2017) — MCU KWS 모델 크기·연산량 (B5 3.2절).
- Y. Kang et al., "Neurosurgeon: Collaborative Intelligence Between the Cloud and Mobile Edge", ASPLOS 2017 — 층 단위 split 지점을 지연·에너지 모델로 고르는 고전.
- Y. Matsubara, M. Levorato, F. Restuccia, "Split Computing and Early Exiting for Deep Learning Applications: Survey", ACM Computing Surveys (2022) — bottleneck 주입, split + early-exit 정리.
- S. Teerapittayanon, B. McDanel, H. T. Kung, "BranchyNet: Fast Inference via Early Exiting from Deep Neural Networks", ICPR 2016.
- L. Chen, M. Zaharia, J. Zou, "FrugalGPT: How to Use Large Language Models While Reducing Cost and Improving Performance", arXiv:2305.05176 (2023) — LLM cascade와 라우팅.
- I. Ong et al., "RouteLLM: Learning to Route LLMs with Preference Data", arXiv:2406.18665 (2024) — 강한/약한 모델 사이 라우터 학습.
- SciPy 문서 — `scipy.optimize.minimize`(SLSQP), `scipy.stats.norm`, `numpy.polynomial.hermite_e.hermegauss`. https://docs.scipy.org/doc/scipy/
- PyTorch 문서 — `torch.nn.Module.register_forward_hook`. https://pytorch.org/docs/stable/generated/torch.nn.Module.html
