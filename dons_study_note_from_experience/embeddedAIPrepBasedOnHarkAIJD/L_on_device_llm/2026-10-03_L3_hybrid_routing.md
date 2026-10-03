# L3. Hybrid 라우팅 — 기기에서 할까, 클라우드로 보낼까를 실제 신호로 결정하기

> **이 노트를 다 읽으면**: 작은 LLM이 답과 함께 내놓는 신호(token log-prob, entropy, self-consistency, 형식 검증)를 llama-server에서 직접 꺼내 80개 실제 질의로 측정하고, AUROC·reliability diagram·ECE로 "이 신호를 믿어도 되나"를 판정할 수 있다 · 신호들을 tiny logistic regression으로 묶고 cross-validation으로 정직하게 문턱을 고른 뒤, 측정된 신호로 정확도 vs 클라우드 비율 곡선을 그리고 지연·에너지·비용·프라이버시를 함께 따질 수 있다 · pre-generation router(질의만 보고 결정)와 post-generation router(SLM 답을 보고 결정)를 실제로 만들어 비교하고, 클라우드 timeout·오프라인·speculative 병렬·circuit breaker 같은 실패 처리를 설계할 수 있다 · 기기와 클라우드의 답이 다를 때의 일관성 문제와 라우팅 로그로 hard example을 모아 distillation으로 되돌리는 루프를 설명할 수 있다
> **JD 연결**: JD 우대 "hybrid edge-LLM pipelines", "integrating small language models on device" — study_prep_list **L3**: 기기 vs 클라우드 결정 기준(지연, 프라이버시, 비용, 연결 상태, confidence), 오프라인 fallback. 함께 닿는 행: **L2**(온디바이스 LLM 스택), **L4**(음성 파이프라인 지연 예산), **L5**(온디바이스 메모리), **L6**(speculative decoding), **H6**(프라이버시), **C5**(distillation)
> **Don 기준 난이도**: timeout·retry·fallback·link 상태 머신, 측정 기반 판단, 양산 수율의 통계적 신뢰 구간은 이미 안다 / 새로 배울 것은 LLM이 내놓는 확률 신호의 의미와 한계, calibration(신뢰도와 실제 정답률을 맞추는 일), router를 분류기로 보고 평가하는 법(AUROC, coverage-precision), 신호마다 다른 비용(self-consistency는 에너지 4배)
> **선행 노트**: B8(SLM), B9 5절(cascade 기대 비용 공식과 SLM→cloud 시뮬레이션), D7 6.3절(기기 vs 클라우드 에너지 교차점), I3 6절(라우팅 정책 하루치 시뮬레이션), H6(프라이버시 · 동의), C5 9절(기기 student, 클라우드 teacher), F3(llama.cpp), A4(ROC·AUROC). 이 노트는 그 결과를 **다시 계산하지 않고** 가져다 쓴다.

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 이미 본 것과 이 노트가 새로 하는 것

기기 vs 클라우드 질문은 이미 여러 노트에서 다뤘다. 겹치지 않게 경계를 먼저 긋는다.

| 노트 | 이미 다룬 것 | 그때 쓴 "confidence" |
|---|---|---|
| B9 5절 | cascade 기대 비용 공식, SLM→cloud 라우팅 시뮬레이션 | 난이도에서 합성한 가짜 점수 |
| D7 6.3절 | 바이트당 무선 에너지, LLM은 오프로드가 배터리에 싸다는 교차점 | 없음 (에너지만) |
| I3 6절 | 하루 120개 질의, always-cloud / always-device / routed 정책 비교 | `conf = 1 − d + 잡음` (합성) |
| H6 | 데이터 분류, 동의, 기기 밖으로 못 나가는 데이터 | 없음 |
| C5 9절 | 클라우드 teacher의 답으로 기기 student를 키우는 구조 | 없음 |
| **L3 (이 노트)** | **진짜 SLM이 내는 진짜 신호**를 재고, calibration하고, router를 만들어 평가 | **측정값**: log-prob, entropy, self-consistency, 형식 검증 |

앞 노트들의 시뮬레이션은 모두 "라우터가 보는 점수가 난이도를 어느 정도 반영한다"고 **가정**했다. I3 6.3절은 "라우터 점수의 질이 상한을 정한다"고 끝났다. 이 노트는 바로 그 가정을 실제로 확인한다. 작은 모델은 자기가 틀릴 때를 알까? 안다면 얼마나 정확히 알까? 그 신호를 얻는 데 비용은 얼마인가?

### 0.2 Don이 이미 아는 그림 — SSD read path의 soft decision

Don이 만든 SSD 펌웨어의 read path가 이 노트의 구조와 거의 같다.

```
NAND read (hard decision, 빠름)
   └─ LDPC decode 성공? ── 예 ──▶ host로 반환          ← 기기 SLM 답 사용
         │ 아니오 (syndrome weight가 크다 = "자신 없음")
         ▼
   read-retry (Vref 이동) → soft-decision LDPC (느림, 전력 큼)   ← 클라우드로 보냄
         │ 그래도 실패
         ▼
   RAID / XOR 복구 (가장 느림)                         ← 오프라인 fallback, 사과 메시지
```

| SSD read path | Hybrid LLM 라우팅 |
|---|---|
| syndrome weight, iteration 수 | token log-prob, entropy, self-consistency |
| decode 성공/실패 판정 (CRC) | 형식 검증 (JSON schema, 숫자 파싱), verifier |
| read-retry 표의 단계별 지연 | 기기 → 클라우드 → fallback 단계별 지연 |
| retry 비율 telemetry | 클라우드 라우팅 비율 telemetry |
| 필드 불량 블록을 모아 Vref 표 재튜닝 | 라우팅 로그로 hard example을 모아 distillation |

차이 하나가 결정적이다. LDPC는 **CRC로 정답 여부를 확실히 안다**. LLM은 대부분의 질의에서 답이 맞았는지 확인할 CRC가 없다. 그래서 "신뢰도 신호"가 필요하고, 그 신호가 얼마나 믿을 만한지(calibration)를 따로 재야 한다. 이 노트의 대부분이 그 일이다.

### 0.3 실험 설정 — 무엇을 "기기", 무엇을 "클라우드"라고 부르나

| 역할 | 모델 | 실행 | 비고 |
|---|---|---|---|
| 기기 SLM | SmolLM2-360M-Instruct Q8_0 (약 386 MB) | llama-server, Apple M2 CPU | 웨어러블 NPU 대신 노트북 CPU. 지연 수치는 상대 비교용 |
| 더 약한 기기 후보 | SmolLM2-135M-Instruct Q8_0 | 같음 | 비교용 (너무 약해서 기기 역할에서 탈락) |
| **클라우드 대역(stand-in)** | Qwen2.5-0.5B-Instruct Q8_0 + 짧은 chain-of-thought 허용 | 같은 노트북 | **진짜 클라우드 LLM이 아니다.** 더 큰 로컬 모델에 더 긴 토큰 예산을 준 "강한 쪽" 대역 |
| pre-router 임베딩 | all-MiniLM-L6-v2 (22.7 M params) | transformers, CPU | 질의 텍스트만 보고 판단 |

정직하게 밝혀 둘 것: 클라우드 대역은 0.5B짜리라서 진짜 클라우드 LLM(수십~수백 B)보다 훨씬 약하다. 실측 정확도는 80% 정도이고 한국어는 절반만 맞힌다. 그래서 이 노트의 **절대 수치**(예: 정확도 0.81)는 의미가 없고, **방법과 상대 비교**(신호 A가 B보다 낫다, 라우팅이 random보다 낫다)가 의미가 있다. 진짜 제품이라면 같은 harness에 클라우드 API를 꽂으면 된다.

모든 지연은 같은 M2 노트북에서 다른 작업이 함께 돌던 상태로 쟀다. 수십 % 흔들릴 수 있다.

---

## 1. 라우팅의 차원 — 무엇을 보고 결정하나

### 1.1 일곱 개의 축

라우터가 보는 정보는 성격이 다르다. 어떤 것은 **협상 불가 제약**(프라이버시, 연결 없음)이고, 어떤 것은 **점수**(신뢰도)이고, 어떤 것은 **점수의 문턱을 움직이는 손잡이**(배터리, 비용 한도)다.

| 축 | 질문 | 무엇으로 아나 | 언제 알 수 있나 | 성격 | 관련 노트 |
|---|---|---|---|---|---|
| 능력 (capability) | 이 유형을 SLM이 할 수 있나? | 질의 분류기, 언어 감지, 오프라인 평가표 | 생성 전 | 점수 | B8, L1 |
| 신뢰도 (confidence) | 방금 만든 답이 맞을까? | log-prob, entropy, self-consistency, 검증기 | 생성 후 | 점수 | 이 노트 3–5절 |
| 프라이버시 등급 | 이 내용이 기기 밖으로 나가도 되나? | 데이터 분류 규칙, 연락처·건강·금융 감지 | 생성 전 | 제약 | H6 |
| 연결 · 지연 예산 | 지금 링크로 예산 안에 답이 오나? | RTT EWMA, 최근 실패, 링크 종류 | 생성 전 (계속 갱신) | 제약 + 손잡이 | I3 6.3, L4 |
| 비용 | 클라우드 토큰 $를 쓸 가치가 있나? | 토큰 수 × 단가, 사용자별 한도 | 생성 전 | 손잡이 | — |
| 에너지 · 발열 | 배터리·피부 온도 여유가 있나? | 배터리 %, 온도 센서, D7 모델 | 생성 전 | 손잡이 | D7, K4 |
| 최신성 (freshness) | 인터넷의 최신 정보가 필요한가? | 의도 분류 (날씨, 뉴스, 주가) | 생성 전 | 제약 | — |

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 500">
<text x="340" y="20" font-size="14" text-anchor="middle">라우팅 결정 순서 — 협상 불가 조건 먼저, 점수는 마지막</text> <rect x="30" y="45" width="300" height="40" rx="6" fill="none" stroke="currentColor"/> <text x="180.0" y="70" font-size="13" text-anchor="middle">① 프라이버시 등급: 기기 밖 금지?</text> <line x1="330.0" y1="65.0" x2="400.0" y2="65.0" stroke="#3f9a6b" stroke-width="1.5"/><polygon points="400.0,65.0 392.6,68.1 392.6,61.9" fill="#3f9a6b"/> <text x="365.0" y="59.0" font-size="12" text-anchor="middle">예</text> <rect x="402" y="49" width="252" height="32" rx="6" fill="#3f9a6b" fill-opacity="0.18" stroke="#3f9a6b"/> <text x="528" y="70" font-size="12" text-anchor="middle">기기 (강제) — H6</text> <line x1="180.0" y1="85.0" x2="180.0" y2="117.0" stroke="currentColor" stroke-width="1.5"/><polygon points="180.0,117.0 176.9,109.6 183.1,109.6" fill="currentColor"/> <text x="188.0" y="105" font-size="12">아니오</text>
<rect x="30" y="117" width="300" height="40" rx="6" fill="none" stroke="currentColor"/> <text x="180.0" y="142" font-size="13" text-anchor="middle">② 최신성: 인터넷 정보가 필요?</text> <line x1="330.0" y1="137.0" x2="400.0" y2="137.0" stroke="#e08a3c" stroke-width="1.5"/><polygon points="400.0,137.0 392.6,140.1 392.6,133.9" fill="#e08a3c"/> <text x="365.0" y="131.0" font-size="12" text-anchor="middle">예</text> <rect x="402" y="121" width="252" height="32" rx="6" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/> <text x="528" y="142" font-size="12" text-anchor="middle">클라우드/도구 (오프라인: 기기+안내)</text> <line x1="180.0" y1="157.0" x2="180.0" y2="189.0" stroke="currentColor" stroke-width="1.5"/><polygon points="180.0,189.0 176.9,181.6 183.1,181.6" fill="currentColor"/> <text x="188.0" y="177" font-size="12">아니오</text>
<rect x="30" y="189" width="300" height="40" rx="6" fill="none" stroke="currentColor"/> <text x="180.0" y="214" font-size="13" text-anchor="middle">③ 연결: 오프라인?</text> <line x1="330.0" y1="209.0" x2="400.0" y2="209.0" stroke="#3f9a6b" stroke-width="1.5"/><polygon points="400.0,209.0 392.6,212.1 392.6,205.9" fill="#3f9a6b"/> <text x="365.0" y="203.0" font-size="12" text-anchor="middle">예</text> <rect x="402" y="193" width="252" height="32" rx="6" fill="#3f9a6b" fill-opacity="0.18" stroke="#3f9a6b"/> <text x="528" y="214" font-size="12" text-anchor="middle">기기 (fallback 모드)</text> <line x1="180.0" y1="229.0" x2="180.0" y2="261.0" stroke="currentColor" stroke-width="1.5"/><polygon points="180.0,261.0 176.9,253.6 183.1,253.6" fill="currentColor"/> <text x="188.0" y="249" font-size="12">아니오</text> <rect x="30" y="261" width="300" height="40" rx="6" fill="none" stroke="currentColor"/>
<text x="180.0" y="286" font-size="13" text-anchor="middle">④ 능력 pre-router: SLM이 못 하는 유형?</text> <line x1="330.0" y1="281.0" x2="400.0" y2="281.0" stroke="#4a7bd0" stroke-width="1.5"/><polygon points="400.0,281.0 392.6,284.1 392.6,277.9" fill="#4a7bd0"/> <text x="365.0" y="275.0" font-size="12" text-anchor="middle">예</text> <rect x="402" y="265" width="252" height="32" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/> <text x="528" y="286" font-size="12" text-anchor="middle">클라우드 직행 (SLM 생략)</text> <line x1="180.0" y1="301.0" x2="180.0" y2="333.0" stroke="currentColor" stroke-width="1.5"/><polygon points="180.0,333.0 176.9,325.6 183.1,325.6" fill="currentColor"/> <text x="188.0" y="321" font-size="12">아니오</text> <rect x="30" y="333" width="300" height="40" rx="6" fill="none" stroke="currentColor"/>
<text x="180.0" y="358" font-size="13" text-anchor="middle">⑤ SLM 실행 → 신뢰도 p ≥ τ ?</text> <line x1="330.0" y1="353.0" x2="400.0" y2="353.0" stroke="#3f9a6b" stroke-width="1.5"/><polygon points="400.0,353.0 392.6,356.1 392.6,349.9" fill="#3f9a6b"/> <text x="365.0" y="347.0" font-size="12" text-anchor="middle">예</text> <rect x="402" y="337" width="252" height="32" rx="6" fill="#3f9a6b" fill-opacity="0.18" stroke="#3f9a6b"/> <text x="528" y="358" font-size="12" text-anchor="middle">기기 답 사용</text> <rect x="30" y="405" width="300" height="40" rx="6" fill="#d0564a" fill-opacity="0.15" stroke="#d0564a"/> <text x="180.0" y="430" font-size="13" text-anchor="middle">클라우드 (timeout → 기기 답으로 fallback)</text> <line x1="180.0" y1="373.0" x2="180.0" y2="405.0" stroke="currentColor" stroke-width="1.5"/><polygon points="180.0,405.0 176.9,397.6 183.1,397.6" fill="currentColor"/>
<text x="188.0" y="393" font-size="12">아니오</text> <text x="402" y="440" font-size="12">τ를 움직이는 손잡이:</text> <text x="402" y="458" font-size="12">링크 나쁨·지연 예산 빡빡 → τ↓ (기기 쪽)</text> <text x="402" y="476" font-size="12">배터리 낮음·발열(D7) → τ↑ (클라우드 쪽)</text> <text x="402" y="494" font-size="12">클라우드 비용 한도 초과 → τ↓</text>
</svg>
```

그림 1 — 라우팅 결정 순서. 프라이버시·최신성·연결 같은 협상 불가 조건을 먼저 거르고, 능력(pre-router)으로 SLM이 못 하는 유형을 걸러 낸 다음, 남은 질의만 SLM을 돌려 신뢰도로 나눈다. τ(문턱)는 링크·배터리·비용이 움직인다.

### 1.2 순서가 중요한 이유

펌웨어의 보호 로직과 같다. 과열 보호(thermal shutdown)는 PID 제어기의 출력보다 **먼저** 본다. 제어기가 아무리 "더 돌려도 된다"고 해도 interlock이 이긴다. 라우터도 같다.

- 프라이버시 규칙이 confidence 점수보다 뒤에 있으면, "기기가 자신 없으니 클라우드로" 하는 순간 건강 기록이 나간다.
- 최신성을 confidence로 판단하면 안 된다. SLM은 "오늘 서울 날씨"에 **자신 있게** 지어낸 답을 한다. 모르는 걸 모른다는 신호가 약하기 때문이다(3.2절에서 실측으로 본다).
- 연결이 없는데 confidence가 낮다고 클라우드로 보내면 timeout만 기다린다. 연결 상태는 confidence보다 먼저 본다.

### 1.3 한 질의의 결정 — 기대 효용으로 쓰면

질의 하나에 대해 기기 답이 맞을 확률을 p_d, 클라우드 답이 맞을 확률을 p_c, 맞는 답의 가치를 V, 각 경로의 비용(지연·에너지·$를 같은 단위로 환산한 것)을 C_d, C_c라고 하자.

```
기기 효용     U_d = p_d · V − C_d
클라우드 효용  U_c = p_c · V − C_c          (post-generation이면 C_c에 C_d가 이미 포함)

클라우드로 보낸다  ⇔  U_c > U_d  ⇔  p_d < p_c − (C_c − C_d) / V
```

말로 하면: 기기 답이 맞을 확률이 "클라우드가 맞을 확률에서 추가 비용만큼 깎은 값"보다 낮으면 보낸다. 그래서 문턱 τ = p_c − ΔC/V다. 비용이 비싸지면(ΔC↑) τ가 내려가서 기기 쪽으로 기울고, 정답의 가치가 크면(V↑, 예: 약 복용 시간 설정) τ가 올라가서 클라우드 쪽으로 기운다.

손계산: p_c = 0.95, V = 1, 클라우드 추가 비용 ΔC = 0.2(지연 불편 + $를 정답 가치의 20%로 환산했다고 가정)이면 τ = 0.75. 기기 신뢰도 p_d = 0.80이면 기기, 0.60이면 클라우드. 링크가 나빠서 ΔC = 0.4로 오르면 τ = 0.55가 되어 0.60짜리도 기기에서 답한다.

이 식이 쓸모 있으려면 **p_d가 진짜 확률이어야** 한다. "점수 0.8"이 실제로 80% 맞는다는 뜻이어야 τ와 비교할 수 있다. 이것이 4절 calibration의 이유다. B9 5절의 cascade 기대 비용은 전체 평균을 다뤘고, 여기서는 질의 하나에 대한 결정 규칙을 다룬다는 점만 다르다.

---

## 2. 실험 장치 — 실제 질의 80개와 실제 모델

### 2.1 질의 세트 설계

웨어러블 음성 비서가 받을 법한 질의를 다섯 범주 × 16개로 만들었다. 각 질의에 **자동 채점기**(checker)가 붙어 있어야 신호와 정답 여부를 짝지을 수 있다.

| 범주 | 예 | 채점 | 의도 |
|---|---|---|---|
| json | "Set a timer for 10 minutes" → `{"intent": "timer", "value": 10}` | JSON 파싱 + intent·value 비교 | 기기에서 해야 할 대표 작업 (도구 호출) |
| arith | "What is 13 times 13?" | 마지막 숫자 (또는 `Answer: N`) | 짧은 계산 |
| know | "What is the capital of Canada?" | 키워드 포함 | 상식 (SLM의 기억 한계) |
| multi | "A pen costs 3 dollars. I buy 5 pens and pay with 20…" | 숫자 | 다단계 추론 (SLM이 약함) |
| ko | "한국의 수도는 어디인가요?" | 키워드 포함 | 학습 분포 밖 언어 (SmolLM2는 영어 중심) |

7개 질의에 `private = 1` 표시를 했다 (엄마에게 전화, 약 복용 알람, 혈압 평균, 카드값 등 — H6의 "기기 밖 금지" 등급 예시). 아래가 전체 세트다. 노트의 모든 예제는 이 파일을 `l3_queries.py`로 저장했다고 가정한다.

```python
# L3 labeled query set: (cat, text, gold, private)
# cat: json / arith / know / multi / ko — gold: json -> (intent, value), 나머지 -> 허용 답 목록
Q = [
 ("json", "Set a timer for 10 minutes", ('timer', 10), 0), ("json", "Timer for 25 minutes please", ('timer', 25), 0),
 ("json", "Start a 3 minute timer", ('timer', 3), 0), ("json", "Turn the volume to 40", ('volume', 40), 0),
 ("json", "Set volume to 75 percent", ('volume', 75), 0), ("json", "Set the volume to zero", ('volume', 0), 0),
 ("json", "Turn it up to 90", ('volume', 90), 0), ("json", "Call my mom", ('call', 'mom'), 1),
 ("json", "Call Dr. Kim about my test results", ('call', 'kim'), 1), ("json", "Phone my therapist Jane", ('call', 'jane'), 1),
 ("json", "What's the weather in Seattle", ('weather', 'seattle'), 0), ("json", "Weather in Tokyo today", ('weather', 'tokyo'), 0),
 ("json", "Is it raining in Paris", ('weather', 'paris'), 0), ("json", "Wake me up at 6:30", ('alarm', '6:30'), 0),
 ("json", "Set an alarm for 07:15 to take my medication", ('alarm', '7:15'), 1), ("json", "Alarm at 9 pm", ('alarm', '21:00'), 0),
 ("arith", "What is 7 + 5?", [12], 0), ("arith", "What is 9 times 8?", [72], 0),
 ("arith", "What is 15 + 27?", [42], 0), ("arith", "What is 100 - 37?", [63], 0),
 ("arith", "What is 12 times 12?", [144], 0), ("arith", "What is 81 divided by 9?", [9], 0),
 ("arith", "I spent 23 dollars on lunch and 48 on groceries. How much in total?", [71], 1), ("arith", "What is 6 times 7?", [42], 0),
 ("arith", "What is 250 + 175?", [425], 0), ("arith", "What is 17 times 3?", [51], 0),
 ("arith", "What is 1000 - 1?", [999], 0), ("arith", "What is 45 divided by 5?", [9], 0),
 ("arith", "What is 13 times 13?", [169], 0), ("arith", "What is 64 + 36?", [100], 0),
 ("arith", "What is 11 times 11?", [121], 0), ("arith", "What is 56 - 19?", [37], 0),
 ("know", "What is the capital of France?", ['paris'], 0), ("know", "What is the capital of Japan?", ['tokyo'], 0),
 ("know", "What is the largest planet in our solar system?", ['jupiter'], 0), ("know", "What is the chemical symbol for gold?", ['au'], 0),
 ("know", "At what temperature in Celsius does water boil at sea level?", ['100'], 0), ("know", "Who wrote Romeo and Juliet?", ['shakespeare'], 0),
 ("know", "How many continents are there?", ['7', 'seven'], 0), ("know", "What is the fastest land animal?", ['cheetah'], 0),
 ("know", "What is the capital of Australia?", ['canberra'], 0), ("know", "Who wrote the novel 1984?", ['orwell'], 0),
 ("know", "What is the smallest prime number?", ['2', 'two'], 0), ("know", "What gas do plants absorb from the air for photosynthesis?", ['carbon dioxide', 'co2'], 0),
 ("know", "What is the capital of Canada?", ['ottawa'], 0), ("know", "What is the currency of Japan?", ['yen'], 0),
 ("know", "What is the longest river in Africa?", ['nile'], 0), ("know", "Which planet is known as the Red Planet?", ['mars'], 0),
 ("multi", "Tom has 3 apples, buys 4 more, then eats 2. How many apples does he have?", [5], 0),
 ("multi", "A box has 6 rows of 4 eggs. 3 eggs break. How many eggs are unbroken?", [21], 0),
 ("multi", "A train leaves at 2 PM and the trip takes 3 hours. At what hour PM does it arrive?", [5], 0),
 ("multi", "A pen costs 3 dollars. I buy 5 pens and pay with 20 dollars. How much change do I get?", [5], 0),
 ("multi", "Sara is 4 years older than Ben. Ben is 9. How old will Sara be in 2 years?", [15], 0),
 ("multi", "24 students are split into groups of 4, and each group gets 2 balls. How many balls are needed?", [12], 0),
 ("multi", "A book has 80 pages. I read 12 pages a day for 5 days. How many pages are left?", [20], 0),
 ("multi", "The temperature is 5 degrees and drops by 8 degrees. What is the temperature now?", [-3], 0),
 ("multi", "Two numbers add up to 10 and differ by 4. What is the larger number?", [7], 0),
 ("multi", "If 3 cats catch 3 mice in 3 minutes, how many cats are needed to catch 100 mice in 100 minutes?", [3], 0),
 ("multi", "A car drives at 60 km per hour for 2.5 hours. How many km does it travel?", [150], 0),
 ("multi", "Three shirts cost 12 dollars each and there is a 10 percent discount on the total. How much do I pay?", [32.4], 0),
 ("multi", "What is half of 30 plus a third of 30?", [25], 0),
 ("multi", "My blood pressure readings this week were 120, 130 and 125. What is the average?", [125], 1),
 ("multi", "How many days are in 3 weeks and 2 days?", [23], 0),
 ("multi", "A bat and a ball cost 1.10 dollars in total. The bat costs 1.00 dollar more than the ball. How many cents does the ball cost?", [5], 0),
 ("ko", "한국의 수도는 어디인가요?", ['서울', 'seoul'], 0), ("ko", "일본의 수도는 어디인가요?", ['도쿄', 'tokyo', '동경'], 0),
 ("ko", "3 더하기 4는 얼마인가요?", ['7'], 0), ("ko", "10 빼기 6은 얼마인가요?", ['4'], 0),
 ("ko", "일주일은 며칠인가요?", ['7', '일곱'], 0), ("ko", "하루는 몇 시간인가요?", ['24', '스물네'], 0),
 ("ko", "'사과'를 영어로 하면?", ['apple'], 0), ("ko", "'고양이'를 영어로 하면?", ['cat'], 0),
 ("ko", "'물'을 영어로 하면?", ['water'], 0), ("ko", "'감사합니다'를 영어로 하면?", ['thank'], 0),
 ("ko", "태양계에서 가장 큰 행성은?", ['목성', 'jupiter'], 0), ("ko", "1년은 몇 달인가요?", ['12', '열두'], 0),
 ("ko", "5 곱하기 6은 얼마인가요?", ['30', '삼십'], 0), ("ko", "대한민국의 화폐 단위는?", ['원', 'won'], 0),
 ("ko", "내 카드값이 이번 달 32만원, 지난달 28만원이야. 합치면 얼마야?", ['60만', '600,000', '600000'], 1), ("ko", "김치의 주재료 채소는?", ['배추', 'cabbage'], 0),
]
```

채점 규칙에 대해 하나 짚는다. 처음에는 "첫 번째 숫자"를 답으로 뽑았는데, 135M 모델이 "To add 7 and 5, we need…"처럼 문제를 되풀이하는 바람에 7을 답으로 읽었다. **채점기 버그가 곧 라벨 잡음**이고, 라벨 잡음은 모든 신호의 AUROC를 깎는다. 마지막 숫자 또는 `Answer:` 뒤 숫자로 바꾼 뒤 저장된 답을 전부 다시 채점했다. 펌웨어 테스트에서 "pass/fail 판정 스크립트가 틀려서 양산 수율이 이상하게 나온" 경험과 같다.

### 2.2 llama-server에서 신호 꺼내기

llama.cpp의 `llama-server`는 `/completion` 요청에 `n_probs: k`를 주면, 생성한 토큰마다 그 토큰의 log-prob과 상위 k개 후보의 log-prob을 `completion_probabilities`로 돌려준다. 먼저 실제로 나오는지 확인한다. 서버는 이렇게 띄웠다 (모델마다 포트 하나).

```sh
B=.tools/llama.cpp/build/bin/llama-server
$B -m .tools/models/smollm2-135m-Q8_0.gguf          --host 127.0.0.1 --port 18731 -c 4096 -np 4 &
$B -m .tools/models/qwen2.5-0.5b-Q8_0.gguf          --host 127.0.0.1 --port 18732 -c 4096 -np 4 &
$B -m .tools/models/smollm2-360m-instruct-Q8_0.gguf --host 127.0.0.1 --port 18733 -c 4096 -np 4 &
curl -s 127.0.0.1:18733/health      # {"status":"ok"} 가 나올 때까지 기다린다
```

기기 SLM(360M)에게 캐나다의 수도를 물어보고, 토큰별 확률을 출력하는 코드다.

```python
# llama-server /completion이 토큰별 log-prob과 top-k 후보를 돌려주는지 확인
import json, math, urllib.request
def call(port, body):
    req = urllib.request.Request(f"http://127.0.0.1:{port}/completion", data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req, timeout=60).read())
p = ("<|im_start|>system\nYou are a helpful assistant. Answer in a few words.<|im_end|>\n"
     "<|im_start|>user\nWhat is the capital of Canada?<|im_end|>\n<|im_start|>assistant\n")
r = call(18733, dict(prompt=p, n_predict=8, temperature=0, n_probs=5, stop=["<|im_end|>"], cache_prompt=False))
print("answer:", repr(r["content"]))
for t in r["completion_probabilities"]:
    top = ", ".join(f"{c['token']!r}:{math.exp(c['logprob']):.2f}" for c in t["top_logprobs"][:3])
    print(f"{t['token']!r:12s} logprob={t['logprob']:6.3f}  top3=[{top}]")
print("timings:", {k: round(r["timings"][k], 1) for k in ("prompt_n", "prompt_ms", "predicted_n", "predicted_ms")})
```

```text
answer: 'Montreal'
'Mont'       logprob=-1.238  top3=['Mont':0.29, 'O':0.23, 'Tor':0.10]
'real'       logprob=-0.028  top3=['real':0.97, 'ré':0.03, 're':0.00]
''           logprob=-0.246  top3=['':0.78, '.':0.18, ' is':0.03]
timings: {'prompt_n': 33, 'prompt_ms': 89.1, 'predicted_n': 3, 'predicted_ms': 18.9}
```

출력에서 볼 것:

- 답은 **Montreal — 틀렸다**. 그런데 첫 토큰의 후보를 보면 `'Mont'` 0.29, `'O'`(Ottawa의 시작) 0.23, `'Tor'`(Toronto) 0.10이다. 모델은 세 도시 사이에서 망설였고, greedy가 그중 가장 높은 것을 골랐을 뿐이다. 이 "망설임"이 우리가 잡으려는 신호다.
- 둘째 토큰 `'real'`은 0.97이다. 일단 "Mont"를 쓰고 나면 "real"은 거의 확실하다. 그래서 **토큰 평균**은 망설임을 희석한다 (3.1절).
- 셋째 토큰 `''`은 대화 끝 토큰(`<|im_end|>`)이다. stop 문자열에 걸린 토큰도 확률 목록에 들어온다.
- `cache_prompt=False`로 해야 prompt 33 토큰을 매번 다시 처리한다. 기본값(true)이면 같은 prefix는 KV cache를 재사용한다. 처음 이 코드를 기본값으로 두 번째 실행했을 때 `prompt_n`이 33이 아니라 1로 나왔다 — prefill 지연을 잴 때 함정이다.
- `logprob`은 sampler가 고르기 **전** 모델 분포의 값이다. temperature 0(greedy)이어도 분포 자체를 돌려준다. (`post_sampling_probs: true`를 주면 top-k/top-p를 거친 **후**의 확률을 주는데, greedy에서는 1.0이 되어 쓸모가 없다 — 실제로 확인했다.)

### 2.3 측정 harness

질의 하나마다 (1) greedy로 답을 만들고 토큰 신호를 계산하고, (2) temperature 0.7로 seed를 바꿔 3번 더 샘플링해서 self-consistency를 잰다. 공통 함수 파일이다.

```python
import json, re, math, urllib.request
from l3_queries import Q
SYS = {
 "json": 'You turn voice commands into JSON. Output only one line of JSON with keys "intent" and "value". '
         'Intents: timer (value = minutes, integer), volume (value = 0-100, integer), call (value = contact name), '
         'weather (value = city), alarm (value = "HH:MM", 24-hour). '
         'Example: "Set a timer for 5 minutes" -> {"intent": "timer", "value": 5}',
 "num": "You are a helpful assistant. Answer with only the final number.",
 "kw": "You are a helpful assistant. Answer in a few words.",
}
def kind(cat): return {"json": "json", "arith": "num", "multi": "num"}.get(cat, "kw")
SYS_COT = "You are a helpful assistant. Think step by step in at most 3 short sentences, then write the final line as 'Answer: <number>'."
def prompt(cat, text, cot=False):
    sysmsg = SYS_COT if (cot and kind(cat) == "num") else SYS[kind(cat)]
    return f"<|im_start|>system\n{sysmsg}<|im_end|>\n<|im_start|>user\n{text}<|im_end|>\n<|im_start|>assistant\n"
def call(port, body, timeout=120):   # llama-server /completion
    req = urllib.request.Request(f"http://127.0.0.1:{port}/completion", data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r: return json.loads(r.read())
NUM = re.compile(r"-?\d+(?:\.\d+)?")
def first_num(s):   # "Answer: N" if present, else the LAST number in the text
    s = s.replace(",", "")
    m = re.search(r"Answer:\s*\$?(-?\d+(?:\.\d+)?)", s)
    if m: return float(m.group(1))
    xs = NUM.findall(s); return float(xs[-1]) if xs else None
def parse_json(s):
    m = re.search(r"\{.*?\}", s, re.S)
    try: d = json.loads(m.group()) if m else None
    except Exception: return None
    return d if isinstance(d, dict) and "intent" in d and "value" in d else None
def norm_val(v):
    s = str(v).strip().lower(); m = re.fullmatch(r"0?(\d{1,2}):(\d{2})", s)   # "06:30" -> "6:30"
    return f"{int(m.group(1))}:{m.group(2)}" if m else s
def key(cat, ans):   # canonical answer used for self-consistency and checking
    k = kind(cat)
    if k == "num": return first_num(ans)
    if k == "json":
        d = parse_json(ans)
        return None if d is None else (str(d["intent"]).lower(), norm_val(d["value"]))
    return ans.strip().lower()
def correct(cat, ans, gold):
    k = kind(cat)
    if k == "num":
        x = first_num(ans); return x is not None and any(abs(x - g) < 1e-6 for g in gold)
    if k == "json":
        kk, (gi, gv) = key(cat, ans), gold
        if kk is None: return False
        return kk[0] == gi and (kk[1] == str(gv) or (isinstance(gv, str) and gv in kk[1]))
    a = ans.lower(); return any(g.lower() in a for g in gold)
def format_ok(cat, ans):
    k = kind(cat)
    if k == "num": return first_num(ans) is not None
    if k == "json":
        d = parse_json(ans); return d is not None and str(d["intent"]).lower() in ("timer", "volume", "call", "weather", "alarm")
    return 0 < len(ans.split()) <= 12
def agree(cat, a, b):
    ka, kb = key(cat, a), key(cat, b)
    if kind(cat) != "kw": return ka is not None and ka == kb
    wa, wb = set(re.findall(r"\w+", ka)), set(re.findall(r"\w+", kb))
    return len(wa | wb) > 0 and len(wa & wb) / len(wa | wb) >= 0.5
def token_signals(probs):
    lp = [t["logprob"] for t in probs]
    ents, margins = [], []
    for t in probs:
        p = [math.exp(x["logprob"]) for x in t["top_logprobs"]]
        r = max(1e-12, 1 - sum(p))
        ents.append(-sum(q * math.log(q) for q in p if q > 0) - r * math.log(r))
        margins.append(p[0] - (p[1] if len(p) > 1 else 0))
    return dict(mean_lp=sum(lp) / len(lp), min_lp=min(lp), sum_lp=sum(lp), first_lp=lp[0],
                mean_ent=sum(ents) / len(ents), first_margin=margins[0], ntok=len(lp))
```

신호 정의를 코드에서 다시 짚는다.

- `mean_lp` = 토큰 log-prob 평균. 길이로 정규화한 NLL(negative log-likelihood)에 마이너스를 붙인 것과 같다.
- `sum_lp` = 답 전체의 log-prob 합 = log P(답 전체). 길이 정규화를 하지 않은 값.
- `min_lp` = 가장 자신 없던 토큰 하나.
- `mean_ent` = 토큰마다 상위 10개 확률로 계산한 entropy의 평균. 상위 10개 밖의 확률 r은 한 덩어리로 취급한다 (−r·ln r). 진짜 entropy보다 **작게** 나오는 하한이다. llama-server가 전체 vocabulary(49k) 분포를 주지 않기 때문이다.
- `first_margin` = 첫 토큰의 1등과 2등 확률 차.
- `sc` (self-consistency) = 샘플 3개 중 greedy 답과 같은 정규화 답을 낸 비율 (0, 1/3, 2/3, 1). 숫자는 숫자끼리, JSON은 (intent, value)끼리, 자유 텍스트는 단어 집합 Jaccard ≥ 0.5를 "같다"로 본다.
- `fmt` = 형식 검증: 숫자 질의에 숫자가 있나, JSON 질의에 파싱 가능한 JSON이 있나.

실행 스크립트다. greedy 호출은 **순서대로** 한다 (다른 slot과 동시에 돌면 timings가 섞인다). 샘플링은 4개 slot으로 병렬 처리한다.

```python
import json, sys, time
from concurrent.futures import ThreadPoolExecutor
from l3_common import *
port, tag = int(sys.argv[1]), sys.argv[2]
COT = len(sys.argv) > 3 and sys.argv[3] == "cot"
STOP = ["<|im_end|>"] if COT else ["<|im_end|>", "\n\n"]
NP = 320 if COT else 48
def greedy(i):
    cat, text, gold, priv = Q[i]
    t0 = time.perf_counter()
    r = call(port, dict(prompt=prompt(cat, text, COT), n_predict=NP, temperature=0, n_probs=10, stop=STOP, cache_prompt=False))
    wall = time.perf_counter() - t0
    ans = r["content"].strip()
    probs = r.get("completion_probabilities") or []
    sig = token_signals(probs) if probs else None
    tm = r["timings"]
    return dict(i=i, cat=cat, priv=priv, ans=ans, ok=correct(cat, ans, gold), fmt=format_ok(cat, ans), sig=sig,
                wall=wall, prompt_n=tm["prompt_n"], prompt_ms=tm["prompt_ms"], pred_n=tm["predicted_n"], pred_ms=tm["predicted_ms"])
def samples(i):
    cat, text, gold, priv = Q[i]
    return [call(port, dict(prompt=prompt(cat, text, COT), n_predict=NP, temperature=0.7, seed=s, stop=STOP))["content"].strip() for s in (1, 2, 3)]
rows = [greedy(i) for i in range(len(Q))]          # sequential: timings not polluted by other slots
with ThreadPoolExecutor(4) as ex:
    S = list(ex.map(samples, range(len(Q))))
for r, s in zip(rows, S):
    r["samples"] = s
    r["sc"] = sum(agree(r["cat"], r["ans"], x) for x in s) / 3
json.dump(rows, open(f"res_{tag}.json", "w"), ensure_ascii=False, indent=0)
print(tag, "acc", sum(r["ok"] for r in rows) / len(rows))
```

```sh
P=.venv/bin/python
$P l3_run.py 18731 s135          # 기기 후보 1: smollm2-135m
$P l3_run.py 18733 s360          # 기기: smollm2-360m
$P l3_run.py 18732 q05           # qwen2.5-0.5b, 짧은 답
$P l3_run.py 18732 q05cot cot    # 클라우드 대역: qwen2.5-0.5b + CoT, 토큰 예산 320
```

분석 예제는 모두 아래 `l3_data.py`로 저장된 결과를 읽는다. 채점 규칙을 고쳤을 때 모델을 다시 돌리지 않고 다시 채점할 수 있게, 저장된 답 문자열에서 정답 여부와 self-consistency를 매번 다시 계산한다.

```python
import json, re, numpy as np
from l3_common import correct, format_ok, agree
from l3_queries import Q
def load(tag):
    R = json.load(open(f"res_{tag}.json"))
    for r in R:
        cat, text, gold, priv = Q[r["i"]]
        r["ok"], r["fmt"] = correct(cat, r["ans"], gold), format_ok(cat, r["ans"])   # 채점 규칙을 고치면 다시 채점
        r["sc"] = sum(agree(cat, r["ans"], x) for x in r["samples"]) / 3
    return R
dev, cld = load("s360"), load("q05cot")
cats = np.array([r["cat"] for r in dev]); priv = np.array([r["priv"] for r in dev], bool)
y = np.array([r["ok"] for r in dev], int); yc = np.array([r["ok"] for r in cld], int)
hangul = np.array([bool(re.search("[가-힣]", q[1])) for q in Q])
S = {k: np.array([r["sig"][k] for r in dev]) for k in dev[0]["sig"]}
S["sc"] = np.array([r["sc"] for r in dev]); S["fmt"] = np.array([r["fmt"] for r in dev], float)
FEATS = ["mean_lp", "min_lp", "mean_ent", "sc", "fmt", "ntok"]
X = np.column_stack([S[k] for k in FEATS])
dev_ms = np.array([r["prompt_ms"] + r["pred_ms"] for r in dev]); cld_ms = np.array([r["prompt_ms"] + r["pred_ms"] for r in cld])
dev_ntok = np.array([r["pred_n"] for r in dev]); cld_ntok = np.array([r["pred_n"] for r in cld])
```

### 2.4 결과 — 누가 무엇을 맞히나

```python
# 네 가지 설정의 카테고리별 정답 수 (저장된 결과에서 정답을 다시 채점)
from l3_data import load, Q
import numpy as np
cats = ["json", "arith", "know", "multi", "ko"]
print(f"{'config':26s}" + "".join(f"{c:>7s}" for c in cats) + "  total  ms/q(p50)")
for tag, name in [("s135", "smollm2-135m (device)"), ("s360", "smollm2-360m (device)"),
                  ("q05", "qwen2.5-0.5b short"), ("q05cot", "qwen2.5-0.5b CoT (cloud)")]:
    R = load(tag)
    row = [sum(r["ok"] for r in R if r["cat"] == c) for c in cats]
    ms = np.median([r["prompt_ms"] + r["pred_ms"] for r in R])
    print(f"{name:26s}" + "".join(f"{v:>5d}/16" for v in row) + f"  {sum(row):3d}/80  {ms:6.0f}")
```

```text
config                       json  arith   know  multi     ko  total  ms/q(p50)
smollm2-135m (device)         2/16    5/16   14/16    1/16    0/16   22/80     200
smollm2-360m (device)        13/16   16/16   15/16    2/16    1/16   47/80     190
qwen2.5-0.5b short           14/16   16/16   12/16    2/16    8/16   52/80     112
qwen2.5-0.5b CoT (cloud)     14/16   16/16   12/16   14/16    8/16   64/80     398
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 360">
<text x="340" y="20" font-size="14" text-anchor="middle">카테고리별 정답 수 (각 16문항, 실측)</text> <line x1="70" y1="270" x2="650" y2="270" stroke="currentColor"/> <line x1="70" y1="270" x2="70" y2="50" stroke="currentColor"/> <line x1="66" y1="270.0" x2="70" y2="270.0" stroke="currentColor"/><text x="62" y="274.0" font-size="12" text-anchor="end">0</text> <line x1="66" y1="215.0" x2="70" y2="215.0" stroke="currentColor"/><text x="62" y="219.0" font-size="12" text-anchor="end">4</text> <line x1="66" y1="160.0" x2="70" y2="160.0" stroke="currentColor"/><text x="62" y="164.0" font-size="12" text-anchor="end">8</text> <line x1="66" y1="105.0" x2="70" y2="105.0" stroke="currentColor"/><text x="62" y="109.0" font-size="12" text-anchor="end">12</text> <line x1="66" y1="50.0" x2="70" y2="50.0" stroke="currentColor"/><text x="62" y="54.0" font-size="12" text-anchor="end">16</text>
<rect x="84.0" y="242.5" width="19" height="27.5" fill="#888"/> <text x="93.5" y="238.5" font-size="11" text-anchor="middle">2</text> <rect x="106.0" y="91.2" width="19" height="178.8" fill="#4a7bd0"/> <text x="115.5" y="87.2" font-size="11" text-anchor="middle">13</text> <rect x="128.0" y="77.5" width="19" height="192.5" fill="#3f9a6b"/> <text x="137.5" y="73.5" font-size="11" text-anchor="middle">14</text> <rect x="150.0" y="77.5" width="19" height="192.5" fill="#e08a3c"/> <text x="159.5" y="73.5" font-size="11" text-anchor="middle">14</text> <text x="128.0" y="288" font-size="13" text-anchor="middle">JSON 명령</text> <rect x="200.0" y="201.2" width="19" height="68.8" fill="#888"/> <text x="209.5" y="197.2" font-size="11" text-anchor="middle">5</text> <rect x="222.0" y="50.0" width="19" height="220.0" fill="#4a7bd0"/> <text x="231.5" y="46.0" font-size="11" text-anchor="middle">16</text>
<rect x="244.0" y="50.0" width="19" height="220.0" fill="#3f9a6b"/> <text x="253.5" y="46.0" font-size="11" text-anchor="middle">16</text> <rect x="266.0" y="50.0" width="19" height="220.0" fill="#e08a3c"/> <text x="275.5" y="46.0" font-size="11" text-anchor="middle">16</text> <text x="244.0" y="288" font-size="13" text-anchor="middle">산수</text> <rect x="316.0" y="77.5" width="19" height="192.5" fill="#888"/> <text x="325.5" y="73.5" font-size="11" text-anchor="middle">14</text> <rect x="338.0" y="63.8" width="19" height="206.2" fill="#4a7bd0"/> <text x="347.5" y="59.8" font-size="11" text-anchor="middle">15</text> <rect x="360.0" y="105.0" width="19" height="165.0" fill="#3f9a6b"/> <text x="369.5" y="101.0" font-size="11" text-anchor="middle">12</text> <rect x="382.0" y="105.0" width="19" height="165.0" fill="#e08a3c"/>
<text x="391.5" y="101.0" font-size="11" text-anchor="middle">12</text> <text x="360.0" y="288" font-size="13" text-anchor="middle">상식</text> <rect x="432.0" y="256.2" width="19" height="13.8" fill="#888"/> <text x="441.5" y="252.2" font-size="11" text-anchor="middle">1</text> <rect x="454.0" y="242.5" width="19" height="27.5" fill="#4a7bd0"/> <text x="463.5" y="238.5" font-size="11" text-anchor="middle">2</text> <rect x="476.0" y="242.5" width="19" height="27.5" fill="#3f9a6b"/> <text x="485.5" y="238.5" font-size="11" text-anchor="middle">2</text> <rect x="498.0" y="77.5" width="19" height="192.5" fill="#e08a3c"/> <text x="507.5" y="73.5" font-size="11" text-anchor="middle">14</text> <text x="476.0" y="288" font-size="13" text-anchor="middle">다단계 추론</text> <rect x="548.0" y="270.0" width="19" height="0.0" fill="#888"/>
<text x="557.5" y="266.0" font-size="11" text-anchor="middle">0</text> <rect x="570.0" y="256.2" width="19" height="13.8" fill="#4a7bd0"/> <text x="579.5" y="252.2" font-size="11" text-anchor="middle">1</text> <rect x="592.0" y="160.0" width="19" height="110.0" fill="#3f9a6b"/> <text x="601.5" y="156.0" font-size="11" text-anchor="middle">8</text> <rect x="614.0" y="160.0" width="19" height="110.0" fill="#e08a3c"/> <text x="623.5" y="156.0" font-size="11" text-anchor="middle">8</text> <text x="592.0" y="288" font-size="13" text-anchor="middle">한국어</text> <rect x="80" y="302" width="14" height="12" fill="#888"/><text x="100" y="312" font-size="12">smollm2-135m</text> <rect x="380" y="302" width="14" height="12" fill="#4a7bd0"/><text x="400" y="312" font-size="12">smollm2-360m (기기)</text>
<rect x="80" y="322" width="14" height="12" fill="#3f9a6b"/><text x="100" y="332" font-size="12">qwen2.5-0.5b 짧게</text> <rect x="380" y="322" width="14" height="12" fill="#e08a3c"/><text x="400" y="332" font-size="12">qwen2.5-0.5b CoT (클라우드 대역)</text>
</svg>
```

그림 2 — 범주별 정답 수(실측). 360M은 JSON 명령·산수·상식은 잘하지만 다단계 추론과 한국어는 거의 못 한다. 클라우드 대역(주황)은 CoT 덕에 다단계 추론이 2 → 14로 뛰지만 상식은 오히려 360M보다 낮다.

출력에서 볼 것:

- **135M은 기기 후보에서 탈락**이다. 80개 중 22개, JSON은 2개. 시스템 프롬프트를 그대로 따라 쓰거나 "I'm sorry, as an AI…"로 답한다. 이 정도면 라우터가 할 일이 "거의 다 클라우드로"가 되어 버린다. 그래서 기기 = 360M으로 정했다.
- **360M의 능력 지도가 뚜렷하다**: JSON 13, 산수 16, 상식 15, 다단계 2, 한국어 1. 실제 질의 분포에서 JSON 명령과 짧은 질문이 대부분이라면 기기에서 상당 부분을 처리할 수 있다.
- **CoT가 다단계 추론을 바꾼다**: 같은 qwen2.5-0.5b가 "숫자만 답하라"면 2/16, "3문장 이내로 생각하고 `Answer:`로 끝내라"면 14/16. 대신 답이 길어져서 p50 지연이 112 → 398 ms. 클라우드 쪽에 토큰 예산을 더 줄 수 있다는 것 자체가 hybrid의 장점 중 하나다.
- **클라우드가 항상 더 낫지 않다**: 상식은 360M 15 vs 대역 12 (대역이 캐나다 수도를 Toronto, 호주 수도를 Sydney라고 답했다). 그래서 6절에서 하이브리드가 클라우드 전용보다 **높은** 정확도를 낸다.

### 2.5 형식 검증과 문법 제약 — "valid"는 "correct"가 아니다

JSON 명령에는 공짜 검증기가 있다: 파싱되는가, intent가 허용 목록에 있는가. llama-server는 `json_schema`를 주면 grammar로 디코딩을 제약해서 **항상** 스키마에 맞는 JSON을 내게 할 수도 있다. 둘 다 해 본다.

```python
# JSON intent: free generation vs json_schema-constrained generation (same model, same prompt)
from l3_common import *
SCHEMA = {"type": "object", "properties": {
    "intent": {"enum": ["timer", "volume", "call", "weather", "alarm"]},
    "value": {"type": ["integer", "string"]}}, "required": ["intent", "value"], "additionalProperties": False}
J = [(i, q) for i, q in enumerate(Q) if q[0] == "json"]
for port, name in ((18731, "smollm2-135m"), (18733, "smollm2-360m")):
    for use in (False, True):
        valid = ok = 0
        for i, (cat, text, gold, _) in J:
            body = dict(prompt=prompt(cat, text), n_predict=48, temperature=0, stop=["<|im_end|>"])
            if use: body["json_schema"] = SCHEMA
            a = call(port, body)["content"].strip()
            valid += format_ok(cat, a); ok += correct(cat, a, gold)
        print(f"{name:13s} schema={str(use):5s}  valid JSON {valid:2d}/16  correct {ok:2d}/16")
```

```text
smollm2-135m  schema=False  valid JSON  7/16  correct  2/16
smollm2-135m  schema=True   valid JSON 16/16  correct  4/16
smollm2-360m  schema=False  valid JSON 14/16  correct 13/16
smollm2-360m  schema=True   valid JSON 16/16  correct 15/16
```

출력에서 볼 것:

- 문법 제약을 걸면 두 모델 모두 **16/16 valid**. 그러나 135M의 정답은 4/16뿐이다. `{"intent": "timer", "value": 5}`처럼 예시를 그대로 베끼거나 엉뚱한 값을 넣는다. **문법은 형식을 보장할 뿐, 내용을 보장하지 않는다.**
- 360M은 문법 제약으로 13 → 15. 제약이 없을 때 실패한 두 개("Is it raining in Paris" → 자연어 답, Seattle → 엉뚱한 키)가 스키마 안으로 들어오면서 맞았다. 형식 실패를 제거하는 데 효과가 크다.
- 라우팅 관점의 교훈: 문법 제약을 쓰면 `fmt` 신호가 **항상 1이 되어 정보가 사라진다**. "JSON이 깨졌으니 클라우드로"라는 검증기 신호와 "문법으로 무조건 valid하게"는 서로 바꿔 쓰는 관계다. 실제로 이 실험의 다른 범주에서도 `fmt`의 AUROC는 0.53(거의 무작위)이다 — 360M이 형식은 거의 항상 지키기 때문이다.

---

## 3. 신호 하나씩 뜯어보기

### 3.1 손으로 계산 — Montreal 답의 신호

2.2절의 출력(토큰 3개: −1.238, −0.028, −0.246)으로 직접 계산한다.

```
sum_lp   = −1.238 − 0.028 − 0.246           = −1.512
mean_lp  = −1.512 / 3                        = −0.504
min_lp   = −1.238  (첫 토큰 'Mont')
P(답 전체) = e^(−1.512)                      ≈ 0.22
토큰 기하평균 확률 = e^(−0.504)               ≈ 0.60

첫 토큰 entropy (top-3만, 나머지 0.38은 한 덩어리):
H ≈ −(0.29·ln0.29 + 0.23·ln0.23 + 0.10·ln0.10 + 0.38·ln0.38)
  = 0.359 + 0.338 + 0.230 + 0.368            ≈ 1.29 nats
```

말로 하면: 이 답 전체가 나올 확률은 22%, 토큰 하나하나는 평균 60% 확신이었다. "Montreal이 맞을 확률 22%"와 "60%" 중 어느 쪽이 실제 정답률에 가까운지는 데이터로만 알 수 있다 (4절). entropy 하한 1.29 nats는 "균등하게 e^1.29 ≈ 3.6개 후보 중 고르는 정도"의 망설임이다.

### 3.2 범주별로 보면 — 자신 있게 틀린다

```python
# 정답/오답별 신호 평균, 그리고 "자신 있게 틀린" 질의
from l3_data import *
print(f"{'cat':6s}{'n_ok':>5s} | mean_lp ok/wrong | sum_lp ok/wrong | sc ok/wrong")
for c in ["json", "arith", "know", "multi", "ko"]:
    m = cats == c
    f = lambda k, v: np.mean(S[k][m & (y == v)]) if (m & (y == v)).any() else float("nan")
    print(f"{c:6s}{y[m].sum():5d} | {f('mean_lp',1):6.2f} / {f('mean_lp',0):6.2f} | {f('sum_lp',1):6.2f} / {f('sum_lp',0):6.2f}"
          f" | {f('sc',1):4.2f} / {f('sc',0):4.2f}")
wrong = np.where(y == 0)[0]
top = wrong[np.argsort(-S["mean_lp"][wrong])][:6]
print("\nconfident-but-wrong (highest mean_lp among wrong):")
for i in top:
    print(f"  #{i:2d} {cats[i]:5s} mean_lp={S['mean_lp'][i]:6.2f} sc={S['sc'][i]:.2f}  {dev[i]['ans'][:34]!r}")
```

```text
cat    n_ok | mean_lp ok/wrong | sum_lp ok/wrong | sc ok/wrong
json     13 |  -0.08 /  -0.23 |  -1.17 /  -4.42 | 0.62 / 0.33
arith    16 |  -0.14 /    nan |  -1.60 /    nan | 0.75 /  nan
know     15 |  -0.39 /  -0.50 |  -1.49 /  -1.51 | 0.69 / 0.67
multi     2 |  -0.23 /  -0.35 |  -5.69 /  -5.10 | 0.67 / 0.38
ko        1 |  -0.21 /  -0.28 |  -3.34 /  -3.27 | 0.00 / 0.18

confident-but-wrong (highest mean_lp among wrong):
  #15 json  mean_lp= -0.07 sc=1.00  '{"intent": "alarm", "value": "9:00'
  #65 ko    mean_lp= -0.12 sc=0.00  '내리지 않는 수도는 어디인가요?'
  #74 ko    mean_lp= -0.12 sc=0.33  '태양계에서 가장 큰 행성은 이번에 있는 �'
  #69 ko    mean_lp= -0.12 sc=0.00  '하루는 오래에 시간을 지낸다.'
  #57 multi mean_lp= -0.16 sc=0.33  '100 cats are needed to catch 100 m'
  #66 ko    mean_lp= -0.16 sc=0.00  '얼마인가요.'
```

출력에서 볼 것:

- **JSON**: 맞은 답의 sum_lp −1.17, 틀린 답 −4.42. 틀린 답은 형식이 깨지면서 길어지고 확률이 떨어진다. 신호가 잘 갈린다.
- **상식(know)**: 맞음 −1.49, 틀림 −1.51. **거의 같다.** Canberra(정답)와 Montreal(오답)은 모델에게 똑같이 "그럴듯한 도시 이름"이다. 모델 내부 확률은 "기억이 정확한가"를 거의 모른다.
- **한국어**: 360M의 한국어 답은 질문을 되풀이하거나 ("얼마인가요."), 문법은 맞지만 말이 안 되는 문장이다. 그런데 mean_lp가 −0.12 ~ −0.16으로 **매우 자신 있다**. 질문 문장을 따라 쓰는 것은 LM에게 쉬운 일이기 때문이다. 토큰 확률은 "다음 토큰이 그럴듯한가"를 재지, "질문에 답했는가"를 재지 않는다.
- 같은 한국어 질의의 self-consistency는 0.00 ~ 0.33이다. 샘플마다 다른 엉터리를 내니 서로 일치하지 않는다. **분포 밖 입력에서는 토큰 확률보다 샘플 간 일치가 더 정직하다.**
- #15 "Alarm at 9 pm" → `"9:00"`(정답은 21:00): mean_lp −0.07, sc 1.00. 세 샘플 모두 같은 실수를 했다. **체계적 오류(systematic error)는 어떤 신호로도 안 잡힌다.** 이것은 검증기(예: "pm이 있는데 시가 12 미만이면 의심")나 데이터로 고쳐야 한다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 370">
<text x="340" y="20" font-size="14" text-anchor="middle">smollm2-360m 80문항: sum_lp × self-consistency (실측)</text> <line x1="70" y1="290" x2="630" y2="290" stroke="currentColor"/> <line x1="70" y1="290" x2="70" y2="50" stroke="currentColor"/> <line x1="70.0" y1="290" x2="70.0" y2="294" stroke="currentColor"/><text x="70.0" y="308" font-size="12" text-anchor="middle">-10↓</text> <line x1="182.0" y1="290" x2="182.0" y2="294" stroke="currentColor"/><text x="182.0" y="308" font-size="12" text-anchor="middle">-8</text> <line x1="294.0" y1="290" x2="294.0" y2="294" stroke="currentColor"/><text x="294.0" y="308" font-size="12" text-anchor="middle">-6</text> <line x1="406.0" y1="290" x2="406.0" y2="294" stroke="currentColor"/><text x="406.0" y="308" font-size="12" text-anchor="middle">-4</text>
<line x1="518.0" y1="290" x2="518.0" y2="294" stroke="currentColor"/><text x="518.0" y="308" font-size="12" text-anchor="middle">-2</text> <line x1="630.0" y1="290" x2="630.0" y2="294" stroke="currentColor"/><text x="630.0" y="308" font-size="12" text-anchor="middle">0</text> <line x1="66" y1="273.2" x2="70" y2="273.2" stroke="currentColor"/><text x="62" y="277.2" font-size="12" text-anchor="end">0</text> <line x1="66" y1="204.4" x2="70" y2="204.4" stroke="currentColor"/><text x="62" y="208.4" font-size="12" text-anchor="end">1/3</text> <line x1="66" y1="135.6" x2="70" y2="135.6" stroke="currentColor"/><text x="62" y="139.6" font-size="12" text-anchor="end">2/3</text> <line x1="66" y1="66.8" x2="70" y2="66.8" stroke="currentColor"/><text x="62" y="70.8" font-size="12" text-anchor="end">1</text>
<polyline points="322.0,71.8 350.0,87.6 378.0,103.4 406.0,119.2 434.0,135.0 462.0,150.8 490.0,166.6 518.0,182.4 546.0,198.2 574.0,214.0 602.0,229.8 630.0,245.6" fill="none" stroke="#888" stroke-width="1.5" stroke-dasharray="6 4"/> <circle cx="609.3" cy="128.1" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="610.5" cy="62.1" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="574.0" cy="141.0" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="580.1" cy="205.9" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="575.1" cy="128.3" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="586.3" cy="203.2" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="554.0" cy="66.4" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="519.0" cy="267.1" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="566.6" cy="139.8" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/>
<circle cx="549.8" cy="59.8" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <path d="M101.3,267.2 L109.3,275.2 M101.3,275.2 L109.3,267.2" stroke="#d0564a" stroke-width="2"/> <circle cx="497.3" cy="273.5" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <path d="M475.4,268.0 L483.4,276.0 M475.4,276.0 L483.4,268.0" stroke="#d0564a" stroke-width="2"/> <circle cx="535.4" cy="137.2" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="577.9" cy="71.1" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <path d="M558.3,71.0 L566.3,79.0 M558.3,79.0 L566.3,71.0" stroke="#d0564a" stroke-width="2"/> <circle cx="563.0" cy="131.7" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="517.3" cy="138.3" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="584.2" cy="70.3" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="580.3" cy="131.9" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/>
<circle cx="491.2" cy="57.8" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="504.4" cy="144.1" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="550.6" cy="200.8" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="492.7" cy="132.3" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="568.2" cy="142.7" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="551.8" cy="137.1" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="572.8" cy="135.1" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="527.3" cy="140.5" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="514.4" cy="58.3" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="563.4" cy="139.3" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="509.9" cy="64.5" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="554.6" cy="59.4" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/>
<circle cx="598.2" cy="138.5" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="594.7" cy="74.6" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="570.9" cy="130.3" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="494.6" cy="206.7" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="544.3" cy="132.0" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="464.1" cy="140.0" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="514.8" cy="139.6" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="519.0" cy="130.5" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="543.4" cy="141.5" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="601.0" cy="138.4" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="520.7" cy="138.9" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="542.6" cy="141.4" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/>
<path d="M541.4,130.3 L549.4,138.3 M541.4,138.3 L549.4,130.3" stroke="#d0564a" stroke-width="2"/> <circle cx="566.9" cy="71.5" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="551.7" cy="211.2" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <circle cx="572.9" cy="59.6" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <path d="M471.5,137.9 L479.5,145.9 M471.5,145.9 L479.5,137.9" stroke="#d0564a" stroke-width="2"/> <path d="M454.5,129.7 L462.5,137.7 M454.5,137.7 L462.5,129.7" stroke="#d0564a" stroke-width="2"/> <path d="M447.0,200.0 L455.0,208.0 M447.0,208.0 L455.0,200.0" stroke="#d0564a" stroke-width="2"/> <path d="M224.9,262.8 L232.9,270.8 M224.9,270.8 L232.9,262.8" stroke="#d0564a" stroke-width="2"/> <path d="M372.5,135.2 L380.5,143.2 M372.5,143.2 L380.5,135.2" stroke="#d0564a" stroke-width="2"/> <path d="M66.0,265.5 L74.0,273.5 M66.0,273.5 L74.0,265.5" stroke="#d0564a" stroke-width="2"/>
<path d="M232.5,207.1 L240.5,215.1 M232.5,215.1 L240.5,207.1" stroke="#d0564a" stroke-width="2"/> <circle cx="150.3" cy="62.8" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <path d="M538.8,132.7 L546.8,140.7 M538.8,140.7 L546.8,132.7" stroke="#d0564a" stroke-width="2"/> <path d="M439.5,198.6 L447.5,206.6 M439.5,206.6 L447.5,198.6" stroke="#d0564a" stroke-width="2"/> <circle cx="472.9" cy="206.4" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <path d="M216.7,263.7 L224.7,271.7 M216.7,271.7 L224.7,263.7" stroke="#d0564a" stroke-width="2"/> <path d="M285.9,194.6 L293.9,202.6 M285.9,202.6 L293.9,194.6" stroke="#d0564a" stroke-width="2"/> <path d="M309.1,136.0 L317.1,144.0 M309.1,144.0 L317.1,136.0" stroke="#d0564a" stroke-width="2"/> <path d="M365.4,204.9 L373.4,212.9 M365.4,212.9 L373.4,204.9" stroke="#d0564a" stroke-width="2"/>
<path d="M528.7,201.6 L536.7,209.6 M528.7,209.6 L536.7,201.6" stroke="#d0564a" stroke-width="2"/> <path d="M530.3,276.8 L538.3,284.8 M530.3,284.8 L538.3,276.8" stroke="#d0564a" stroke-width="2"/> <path d="M525.5,263.9 L533.5,271.9 M525.5,271.9 L533.5,263.9" stroke="#d0564a" stroke-width="2"/> <path d="M562.0,275.5 L570.0,283.5 M562.0,283.5 L570.0,275.5" stroke="#d0564a" stroke-width="2"/> <path d="M528.3,263.2 L536.3,271.2 M528.3,271.2 L536.3,263.2" stroke="#d0564a" stroke-width="2"/> <path d="M66.0,277.6 L74.0,285.6 M66.0,285.6 L74.0,277.6" stroke="#d0564a" stroke-width="2"/> <path d="M531.1,271.4 L539.1,279.4 M531.1,279.4 L539.1,271.4" stroke="#d0564a" stroke-width="2"/> <path d="M443.4,271.1 L451.4,279.1 M443.4,279.1 L451.4,271.1" stroke="#d0564a" stroke-width="2"/> <path d="M480.6,140.1 L488.6,148.1 M480.6,148.1 L488.6,140.1" stroke="#d0564a" stroke-width="2"/>
<path d="M531.7,136.8 L539.7,144.8 M531.7,144.8 L539.7,136.8" stroke="#d0564a" stroke-width="2"/> <path d="M344.3,205.6 L352.3,213.6 M344.3,213.6 L352.3,205.6" stroke="#d0564a" stroke-width="2"/> <path d="M518.0,192.4 L526.0,200.4 M518.0,200.4 L526.0,192.4" stroke="#d0564a" stroke-width="2"/> <circle cx="443.2" cy="270.8" r="4.5" fill="#3f9a6b" fill-opacity="0.8"/> <path d="M517.8,192.9 L525.8,200.9 M517.8,200.9 L525.8,192.9" stroke="#d0564a" stroke-width="2"/> <path d="M431.9,263.7 L439.9,271.7 M431.9,271.7 L439.9,263.7" stroke="#d0564a" stroke-width="2"/> <path d="M441.0,195.2 L449.0,203.2 M441.0,203.2 L449.0,195.2" stroke="#d0564a" stroke-width="2"/> <path d="M332.4,275.7 L340.4,283.7 M332.4,283.7 L340.4,275.7" stroke="#d0564a" stroke-width="2"/> <text x="350.0" y="328" font-size="13" text-anchor="middle">sum_lp = 답 전체 log-prob 합 (오른쪽일수록 확신, -10 이하는 -10에 표시)</text>
<text x="20" y="170.0" font-size="13" text-anchor="middle" transform="rotate(-90 20 170.0)">self-consistency</text> <circle cx="90" cy="352" r="4.5" fill="#3f9a6b"/><text x="100" y="356" font-size="12">기기 답 정답</text> <path d="M206,348 L214,356 M206,356 L214,348" stroke="#d0564a" stroke-width="2"/><text x="222" y="356" font-size="12">기기 답 오답</text> <line x1="320" y1="352" x2="350" y2="352" stroke="#888" stroke-width="1.5" stroke-dasharray="6 4"/><text x="356" y="356" font-size="12">LR p = 0.6 경계 (위·오른쪽 = 기기에서 답함)</text>
</svg>
```

그림 3 — 80개 질의의 (sum_lp, self-consistency). 초록 점은 기기 답이 맞음, 빨간 ×는 틀림. 오른쪽 위에 정답이 몰려 있고, 점선(6절에서 쓰는 LR 경계, p = 0.6)의 왼쪽 아래가 클라우드로 간다. 오른쪽 아래(확신 있는데 일치 안 함)와 sc = 2/3 줄의 오른쪽에 섞인 ×들이 "자신 있게 틀린" 질의다.

### 3.3 AUROC — 신호 하나의 분리 능력

라우터는 사실상 **이진 분류기**다: "기기 답이 맞을까(1), 틀릴까(0)". 신호 하나가 얼마나 잘 가르는지는 AUROC(A4)로 잰다. 정의를 손으로 다시 해 본다.

```
AUROC = P(무작위로 뽑은 정답 질의의 점수 > 무작위로 뽑은 오답 질의의 점수)

예: 정답 점수 {0.9, 0.6}, 오답 점수 {0.7, 0.2}
쌍 4개: (0.9>0.7 ✓) (0.9>0.2 ✓) (0.6>0.7 ✗) (0.6>0.2 ✓)  → 3/4 = 0.75
```

말로 하면: 맞는 것과 틀린 것을 하나씩 뽑았을 때 점수가 순서를 맞게 매길 확률이다. 0.5는 동전 던지기, 1.0은 완벽. 문턱을 정하지 않고도 비교할 수 있어서 신호를 고를 때 쓴다. 80개는 작은 표본이라 bootstrap(표본을 복원 추출로 2000번 다시 뽑아 AUROC를 다시 계산)으로 신뢰 구간도 붙인다.

```python
# 신호 하나하나가 "기기 답이 맞을지"를 얼마나 잘 가르나: AUROC + bootstrap 95% CI
from l3_data import *
from sklearn.metrics import roc_auc_score
rng = np.random.default_rng(0)
cands = {"mean_lp": S["mean_lp"], "min_lp": S["min_lp"], "sum_lp (=-NLL)": S["sum_lp"],
         "first_lp": S["first_lp"], "-mean_ent": -S["mean_ent"], "first_margin": S["first_margin"],
         "self-consistency": S["sc"], "format_ok": S["fmt"], "-ntok": -S["ntok"].astype(float)}
print(f"{'signal':18s} AUROC   95% CI")
for name, s in cands.items():
    bs = []
    for _ in range(2000):
        idx = rng.integers(0, len(y), len(y))
        if y[idx].min() != y[idx].max(): bs.append(roc_auc_score(y[idx], s[idx]))
    lo, hi = np.percentile(bs, [2.5, 97.5])
    print(f"{name:18s} {roc_auc_score(y, s):.3f}  [{lo:.2f}, {hi:.2f}]")
nonko = cats != "ko"
print("\nwithout Korean:  sum_lp %.3f   sc %.3f" % (roc_auc_score(y[nonko], S["sum_lp"][nonko]), roc_auc_score(y[nonko], S["sc"][nonko])))
```

```text
signal             AUROC   95% CI
mean_lp            0.729  [0.62, 0.83]
min_lp             0.773  [0.66, 0.87]
sum_lp (=-NLL)     0.830  [0.73, 0.91]
first_lp           0.664  [0.53, 0.79]
-mean_ent          0.720  [0.60, 0.83]
first_margin       0.680  [0.55, 0.80]
self-consistency   0.811  [0.71, 0.90]
format_ok          0.530  [0.50, 0.58]
-ntok              0.621  [0.49, 0.75]

without Korean:  sum_lp 0.884   sc 0.761
```

출력에서 볼 것:

- 가장 좋은 단일 신호는 **sum_lp 0.830**과 **self-consistency 0.811**. 하지만 95% 구간이 [0.73, 0.91]과 [0.71, 0.90]으로 겹친다. 80개로는 둘의 우열을 말할 수 없다. Don이 양산 데이터를 볼 때 "샘플 수가 적으면 차이를 주장하지 않는다"는 원칙 그대로다.
- **mean_lp(0.729)가 sum_lp(0.830)보다 나쁘다**. 길이 정규화가 오히려 손해다. 이 세트에서는 틀린 답이 길다(다단계 추론에서 장황하게 헤맴, 깨진 JSON). sum은 길이를 벌점으로 포함하고 mean은 그 정보를 버린다. 하지만 이것은 **세트 의존적**이다. 긴 설명이 정답인 작업(요약 등)에서는 sum이 긴 정답을 부당하게 벌한다 — 그림 3 왼쪽 위의 초록 점(sum_lp ≈ −8.6, sc = 1)이 그런 예다.
- **first_lp(0.664), first_margin(0.680)**: 첫 토큰만 보면 싸지만(생성 직후 결정 가능 — 스트리밍 handoff에 유리) 정보가 적다. **format_ok(0.530)**는 360M이 형식을 거의 항상 지켜서 정보가 없다 (2.5절).
- **한국어를 빼면** sum_lp 0.884, sc 0.761. sum_lp는 영어에서 더 좋아지고, sc는 나빠진다. sc의 상당 부분이 "한국어 = 불일치"를 잡는 데서 왔다는 뜻이다. 신호마다 **잘 잡는 실패 유형이 다르다** — 그래서 묶는다(5절).

### 3.4 AUROC의 상당 부분은 "범주 감지"다

정직하게 짚어야 할 점이 있다. 범주 안에서 보면(3.2절 표) 상식·다단계·한국어는 맞음/틀림의 신호 평균이 거의 같다. 전체 AUROC 0.83의 큰 부분은 "JSON·산수는 맞고 다단계·한국어는 틀린다"는 **범주 간 차이**를 신호가 따라간 결과다. 이것은 나쁜 소식이자 좋은 소식이다.

- 나쁜 소식: 같은 범주 안의 "Canberra vs Montreal"은 거의 못 가른다. 모델이 모르는 걸 모른다.
- 좋은 소식: 범주 차이라면 **생성 전에** 질의만 보고도 알 수 있다. 7절의 pre-router가 바로 이것을 이용한다.

---

## 4. Calibration — 신뢰도를 확률로 믿어도 되나

### 4.1 정의와 손계산

AUROC는 **순서**만 본다. 1.3절의 τ 비교처럼 점수를 **확률**로 쓰려면, "신뢰도 0.8이라고 말한 질의들은 실제로 80%가 맞는가"를 따로 봐야 한다. 이것이 calibration이다.

reliability diagram: 신뢰도를 구간(bin)으로 나누고, 각 bin에서 (평균 신뢰도, 실제 정답률)을 점으로 찍는다. 완벽하면 대각선 위에 놓인다. ECE(expected calibration error)는 그 차이를 샘플 수로 가중 평균한 값이다.

```
ECE = Σ_b (n_b / N) · | 평균신뢰도_b − 정답률_b |

예: 10개 질의, bin 두 개
bin A (신뢰도 0.9로 말한 6개): 실제 4개 정답 → 0.67   차이 0.23
bin B (신뢰도 0.3으로 말한 4개): 실제 2개 정답 → 0.50  차이 0.20
ECE = 0.6·0.23 + 0.4·0.20 = 0.138 + 0.080 = 0.218
```

말로 하면: 평균적으로 신뢰도가 실제 정답률에서 22%p 어긋나 있다. bin A는 **과대확신**(overconfident, 점이 대각선 아래), bin B는 **과소확신**(underconfident, 위)이다. 펌웨어 비유로는 센서 offset·gain 보정이다. ADC 값의 **순서**는 맞아도(단조 증가) 절대값이 틀리면 문턱 비교가 틀린다. AUROC는 단조성, calibration은 절대값이다.

### 4.2 코드로 확인 — 네 가지 "신뢰도"

```python
# Calibration: "신뢰도 0.8이면 정말 80%가 맞나?" — reliability 표와 ECE (5 bins)
from l3_data import *
from sklearn.linear_model import LogisticRegression
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.model_selection import StratifiedKFold, cross_val_predict
lr = make_pipeline(StandardScaler(), LogisticRegression(max_iter=1000))
Z = np.column_stack([S["sum_lp"], S["sc"]])
p_lr = cross_val_predict(lr, Z, y, cv=StratifiedKFold(5, shuffle=True, random_state=0), method="predict_proba")[:, 1]
np.save("p_lr.npy", p_lr)
def reliability(conf, name, nb=5):
    edges = np.linspace(0, 1, nb + 1); ece = 0; out = []
    for a, b in zip(edges[:-1], edges[1:]):
        m = (conf >= a) & ((conf < b) if b < 1 else (conf <= b))
        if m.any():
            ece += m.mean() * abs(conf[m].mean() - y[m].mean())
            out.append(f"{conf[m].mean():.2f}→{y[m].mean():.2f}(n={m.sum()})")
    print(f"{name:22s} ECE={ece:.3f}  " + "  ".join(out))
reliability(np.exp(S["mean_lp"]), "exp(mean_lp)")
reliability(np.exp(S["sum_lp"]), "exp(sum_lp)=P(answer)")
reliability(S["sc"], "self-consistency")
reliability(p_lr, "LR(sum_lp, sc), CV")
```

```text
exp(mean_lp)           ECE=0.213  0.37→0.50(n=2)  0.48→0.50(n=4)  0.72→0.37(n=27)  0.88→0.72(n=47)
exp(sum_lp)=P(answer)  ECE=0.394  0.08→0.38(n=47)  0.29→0.83(n=24)  0.49→1.00(n=7)  0.70→1.00(n=2)
self-consistency       ECE=0.096  0.00→0.18(n=17)  0.33→0.38(n=16)  0.67→0.76(n=33)  1.00→0.93(n=14)
LR(sum_lp, sc), CV     ECE=0.058  0.08→0.15(n=13)  0.32→0.23(n=13)  0.53→0.38(n=8)  0.74→0.79(n=19)  0.88→0.89(n=27)
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 380">
<text x="320" y="20" font-size="14" text-anchor="middle">Reliability diagram — 신뢰도 vs 실제 정답률 (5 bins, 실측)</text> <line x1="80" y1="300" x2="340" y2="300" stroke="currentColor"/><line x1="80" y1="300" x2="80" y2="40" stroke="currentColor"/> <line x1="80" y1="300" x2="340" y2="40" stroke="#888" stroke-dasharray="4 4"/> <line x1="80.0" y1="300" x2="80.0" y2="304" stroke="currentColor"/><text x="80.0" y="318" font-size="12" text-anchor="middle">0.0</text> <line x1="76" y1="300.0" x2="80" y2="300.0" stroke="currentColor"/><text x="72" y="304.0" font-size="12" text-anchor="end">0.0</text> <line x1="132.0" y1="300" x2="132.0" y2="304" stroke="currentColor"/><text x="132.0" y="318" font-size="12" text-anchor="middle">0.2</text> <line x1="76" y1="248.0" x2="80" y2="248.0" stroke="currentColor"/><text x="72" y="252.0" font-size="12" text-anchor="end">0.2</text>
<line x1="184.0" y1="300" x2="184.0" y2="304" stroke="currentColor"/><text x="184.0" y="318" font-size="12" text-anchor="middle">0.4</text> <line x1="76" y1="196.0" x2="80" y2="196.0" stroke="currentColor"/><text x="72" y="200.0" font-size="12" text-anchor="end">0.4</text> <line x1="236.0" y1="300" x2="236.0" y2="304" stroke="currentColor"/><text x="236.0" y="318" font-size="12" text-anchor="middle">0.6</text> <line x1="76" y1="144.0" x2="80" y2="144.0" stroke="currentColor"/><text x="72" y="148.0" font-size="12" text-anchor="end">0.6</text> <line x1="288.0" y1="300" x2="288.0" y2="304" stroke="currentColor"/><text x="288.0" y="318" font-size="12" text-anchor="middle">0.8</text> <line x1="76" y1="92.0" x2="80" y2="92.0" stroke="currentColor"/><text x="72" y="96.0" font-size="12" text-anchor="end">0.8</text>
<line x1="340.0" y1="300" x2="340.0" y2="304" stroke="currentColor"/><text x="340.0" y="318" font-size="12" text-anchor="middle">1.0</text> <line x1="76" y1="40.0" x2="80" y2="40.0" stroke="currentColor"/><text x="72" y="44.0" font-size="12" text-anchor="end">1.0</text> <polyline points="175.8,170.0 205.1,170.0 267.1,203.7 308.3,111.9" fill="none" stroke="#4a7bd0" stroke-width="2"/> <circle cx="175.8" cy="170.0" r="4.7" fill="#4a7bd0" fill-opacity="0.55"/> <circle cx="205.1" cy="170.0" r="5.4" fill="#4a7bd0" fill-opacity="0.55"/> <circle cx="267.1" cy="203.7" r="9.2" fill="#4a7bd0" fill-opacity="0.55"/> <circle cx="308.3" cy="111.9" r="11.2" fill="#4a7bd0" fill-opacity="0.55"/> <line x1="380" y1="86" x2="404" y2="86" stroke="#4a7bd0" stroke-width="2"/><text x="410" y="90" font-size="12">exp(mean_lp)  ECE 0.213</text>
<polyline points="80.0,254.1 166.7,202.5 253.3,103.0 340.0,58.6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <circle cx="80.0" cy="254.1" r="7.9" fill="#3f9a6b" fill-opacity="0.55"/> <circle cx="166.7" cy="202.5" r="7.8" fill="#3f9a6b" fill-opacity="0.55"/> <circle cx="253.3" cy="103.0" r="9.9" fill="#3f9a6b" fill-opacity="0.55"/> <circle cx="340.0" cy="58.6" r="7.5" fill="#3f9a6b" fill-opacity="0.55"/> <line x1="380" y1="112" x2="404" y2="112" stroke="#3f9a6b" stroke-width="2"/><text x="410" y="116" font-size="12">self-consistency  ECE 0.096</text> <polyline points="101.7,260.0 163.2,240.0 216.6,202.5 271.1,94.7 307.8,68.9" fill="none" stroke="#e08a3c" stroke-width="2"/> <circle cx="101.7" cy="260.0" r="7.3" fill="#e08a3c" fill-opacity="0.55"/> <circle cx="163.2" cy="240.0" r="7.3" fill="#e08a3c" fill-opacity="0.55"/>
<circle cx="216.6" cy="202.5" r="6.4" fill="#e08a3c" fill-opacity="0.55"/> <circle cx="271.1" cy="94.7" r="8.2" fill="#e08a3c" fill-opacity="0.55"/> <circle cx="307.8" cy="68.9" r="9.2" fill="#e08a3c" fill-opacity="0.55"/> <line x1="380" y1="138" x2="404" y2="138" stroke="#e08a3c" stroke-width="2"/><text x="410" y="142" font-size="12">LR(sum_lp, sc), CV  ECE 0.058</text> <polyline points="100.5,200.4 155.5,83.3 206.9,40.0 261.6,40.0" fill="none" stroke="#d0564a" stroke-width="2"/> <circle cx="100.5" cy="200.4" r="11.2" fill="#d0564a" fill-opacity="0.55"/> <circle cx="155.5" cy="83.3" r="8.9" fill="#d0564a" fill-opacity="0.55"/> <circle cx="206.9" cy="40.0" r="6.2" fill="#d0564a" fill-opacity="0.55"/> <circle cx="261.6" cy="40.0" r="4.7" fill="#d0564a" fill-opacity="0.55"/>
<line x1="380" y1="164" x2="404" y2="164" stroke="#d0564a" stroke-width="2"/><text x="410" y="168" font-size="12">exp(sum_lp)  ECE 0.394</text> <text x="380" y="210" font-size="12">원 크기 = bin의 샘플 수</text> <text x="380" y="230" font-size="12">대각선 위 = 과소확신, 아래 = 과대확신</text> <text x="210.0" y="338" font-size="13" text-anchor="middle">신호가 말하는 신뢰도</text> <text x="26" y="170.0" font-size="13" text-anchor="middle" transform="rotate(-90 26 170.0)">실제 정답률</text>
</svg>
```

그림 4 — reliability diagram (실측, 5 bins). 파랑(토큰 기하평균 확률)은 대각선 아래 = 과대확신, 빨강(답 전체 확률)은 위 = 심한 과소확신, 초록(self-consistency)과 주황(LR)이 대각선에 가깝다.

출력에서 볼 것:

- **exp(mean_lp)는 과대확신**이다: "0.88"이라고 말한 47개 중 실제 정답은 72%, "0.72"라고 말한 27개는 37%. 토큰 하나하나는 확신하지만 답 전체가 맞는 것은 다른 문제다. ECE 0.213.
- **exp(sum_lp) = P(이 답 문자열)은 심한 과소확신**이다: "0.08"이라고 말한 47개가 실제로 38% 맞는다. 같은 정답을 말하는 방법이 수없이 많기 때문이다("Paris", "Paris.", "The capital is Paris"). 한 문자열의 확률은 정답 확률의 일부만 잰다. AUROC는 가장 좋았는데(0.830) calibration은 가장 나쁘다(0.394) — **순서와 확률은 다른 문제**라는 4.1절의 요점이 그대로 나왔다.
- **self-consistency는 꽤 정직하다** (ECE 0.096): 3개 모두 일치(1.0)하면 93%, 하나도 일치 안 하면(0.0) 18%.
- **LR(sum_lp, sc)는 ECE 0.058로 가장 좋다**. logistic regression은 정답 라벨에 맞춰 확률을 출력하도록 학습하므로, calibration을 **학습으로 얻는다** (Platt scaling과 같은 원리). 단, cross-validation으로 만든 예측(`cross_val_predict`)이라 학습에 쓰지 않은 질의에 대한 확률이다. 같은 데이터로 학습·평가하면 ECE가 실제보다 좋게 나온다.

### 4.3 왜 LLM 확률은 calibration이 깨지나

- **instruction tuning·RLHF**가 출력 분포를 날카롭게 만든다. "도움이 되는 확신 있는 답"을 보상받았기 때문이다. 사전학습(base) 모델은 다음 토큰 예측에서 비교적 잘 calibration되어 있다는 보고가 있다 (참고 자료의 Kadavath et al. 2022).
- **양자화**도 logit을 바꾼다 (C3). Q8_0은 거의 영향이 없지만 Q4 이하에서는 같은 답이어도 확률이 달라진다. **모델을 바꾸면(양자화 포함) calibration을 다시 해야 한다.** 펌웨어로 말하면 센서 부품이 바뀌면 보정 테이블을 다시 굽는 것과 같다.
- **분포 밖 입력**(한국어)에서는 확률이 아무 의미 없이 높을 수 있다 (3.2절).

---

## 5. 신호 묶기 — tiny logistic regression

### 5.1 왜 logistic regression인가

신호 몇 개를 받아 "맞을 확률"을 내는 가장 작은 모델이다.

```
z = b + w₁·sum_lp + w₂·sc
p = σ(z) = 1 / (1 + e^(−z))
```

말로 하면: 신호들의 가중합을 sigmoid로 0~1에 눌러 넣는다. 파라미터가 3개이고, 기기에서 곱셈 2번 + `expf` 1번이면 된다(5.4절의 C 코드). 80개 샘플로 학습할 수 있는 크기의 모델은 이 정도가 상한이다. 신호마다 단위가 다르므로(sum_lp는 −20~0, sc는 0~1) 표준화(평균 0, 표준편차 1)한 뒤 학습하고, 기기에 넣을 때는 원래 단위의 계수로 되돌린다.

검증은 **5-fold cross-validation을 20번 반복**한다. 80개를 5등분해서 4/5로 학습, 1/5로 평가하는 것을 다섯 번, 그 분할 방식을 20번 바꿔 평균·표준편차를 낸다. 샘플이 적을 때 한 번의 분할 운에 결론이 좌우되지 않게 하는 방법이다.

### 5.2 코드로 확인 — 어떤 신호를 묶을까

```python
# 신호 묶기: tiny logistic regression, 5-fold CV를 20번 반복한 AUROC
from l3_data import *
from sklearn.linear_model import LogisticRegression
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.model_selection import RepeatedStratifiedKFold, cross_val_score
lr = lambda: make_pipeline(StandardScaler(), LogisticRegression(max_iter=1000))
S["hangul"] = hangul.astype(float)
sets = {"sum_lp": ["sum_lp"], "sc": ["sc"], "sum_lp+sc": ["sum_lp", "sc"],
        "all 6 (mean,min,ent,sc,fmt,ntok)": FEATS, "sum_lp+sc+hangul": ["sum_lp", "sc", "hangul"]}
cv = RepeatedStratifiedKFold(n_splits=5, n_repeats=20, random_state=0)
for name, F in sets.items():
    a = cross_val_score(lr(), np.column_stack([S[k] for k in F]), y, cv=cv, scoring="roc_auc")
    print(f"{name:34s} AUROC {a.mean():.3f} ± {a.std():.3f}")
m = lr().fit(np.column_stack([S["sum_lp"], S["sc"]]), y)
w, b = m[-1].coef_[0], m[-1].intercept_[0]; mu, sd = m[0].mean_, m[0].scale_
print("fitted on all 80 (standardized): w =", np.round(w, 3), " b =", round(b, 3))
print("raw-unit form: z = %.3f + %.4f*sum_lp + %.3f*sc" % (b - (w * mu / sd).sum(), w[0] / sd[0], w[1] / sd[1]))
```

```text
sum_lp                             AUROC 0.834 ± 0.091
sc                                 AUROC 0.815 ± 0.102
sum_lp+sc                          AUROC 0.871 ± 0.093
all 6 (mean,min,ent,sc,fmt,ntok)   AUROC 0.853 ± 0.093
sum_lp+sc+hangul                   AUROC 0.901 ± 0.084
fitted on all 80 (standardized): w = [1.219 1.082]  b = 0.326
raw-unit form: z = -0.024 + 0.4916*sum_lp + 3.211*sc
```

출력에서 볼 것:

- **sum_lp + sc = 0.871**로 둘 각각(0.834, 0.815)보다 낫다. 3.3절에서 본 대로 둘은 잘 잡는 실패가 다르다(sum_lp는 영어 장황한 오답, sc는 한국어 엉터리).
- **신호 6개를 다 넣으면 0.853으로 오히려 떨어진다**. 80개 샘플에 특징 6개는 과적합 쪽이다. 펌웨어 튜닝 파라미터를 테스트 벡터 몇 개에 다 맞추면 필드에서 깨지는 것과 같다(A4). 표준편차 ±0.09도 크다.
- **hangul(질의에 한글이 있나) 하나를 더하면 0.901**. 생성 전에 알 수 있는 특징이 생성 후 신호에 정보를 보탠다 — 7절 pre-router의 예고편이다.
- 원래 단위 식: `z = −0.024 + 0.4916·sum_lp + 3.211·sc`. 해석: sc가 1/3 오를 때 z가 약 1.07 오른다(odds가 약 2.9배). sum_lp가 2 nats 내려가면 z가 약 0.98 내려간다.

### 5.3 문턱 고르기 — 목표 precision과 coverage

제품 요구는 보통 이렇게 온다: "기기에서 답한 것은 90% 이상 맞아야 한다". 여기서

- **precision** = 기기에서 답하기로 한 질의 중 맞은 비율
- **coverage** = 전체 중 기기에서 답한 비율 (나머지는 클라우드)

문턱 τ를 올리면 precision은 오르고 coverage는 내려간다. 목표 precision을 만족하는 가장 낮은 τ를 고른다. 문제는 **같은 데이터로 τ를 고르고 같은 데이터로 precision을 재면 낙관적**이라는 것이다. 그래서 nested 방식(바깥 fold의 학습 부분 안에서만 τ를 고르고, 바깥 test fold에 적용)도 함께 잰다.

```python
# 목표 precision(기기가 답한 것 중 맞는 비율)을 만족하는 문턱 τ — 같은 데이터로 고르면 낙관적이다
from l3_data import *
from sklearn.linear_model import LogisticRegression
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.model_selection import StratifiedKFold, cross_val_predict
lr = lambda: make_pipeline(StandardScaler(), LogisticRegression(max_iter=1000))
Z = np.column_stack([S["sum_lp"], S["sc"]])
def pick_tau(p, yy, target):          # precision ≥ target 인 가장 낮은 τ (= coverage 최대)
    for t in np.sort(np.unique(p)):
        keep = p >= t
        if yy[keep].mean() >= target: return t
    return 1.01
p_cv = np.load("p_lr.npy")
for target in (0.85, 0.90, 0.95):
    t = pick_tau(p_cv, y, target); keep = p_cv >= t
    # nested: 바깥 fold의 train에서만 τ를 고르고 test fold에 적용
    kept, hit = 0, 0
    for tr, te in StratifiedKFold(5, shuffle=True, random_state=1).split(Z, y):
        pin = cross_val_predict(lr(), Z[tr], y[tr], cv=StratifiedKFold(4, shuffle=True, random_state=0), method="predict_proba")[:, 1]
        tt = pick_tau(pin, y[tr], target)
        pt = lr().fit(Z[tr], y[tr]).predict_proba(Z[te])[:, 1] >= tt
        kept += pt.sum(); hit += y[te][pt].sum()
    print(f"target {target:.2f}: τ={t:.2f} same-data precision {y[keep].mean():.2f} coverage {keep.mean():.2f}"
          f" | nested precision {hit / max(kept, 1):.2f} coverage {kept / len(y):.2f}")
```

```text
target 0.85: τ=0.58 same-data precision 0.85 coverage 0.60 | nested precision 0.87 coverage 0.57
target 0.90: τ=0.82 same-data precision 0.92 coverage 0.31 | nested precision 0.88 coverage 0.33
target 0.95: τ=0.84 same-data precision 0.95 coverage 0.26 | nested precision 0.75 coverage 0.05
```

출력에서 볼 것:

- 목표 0.85: 같은 데이터로 고르면 coverage 60%, nested로도 precision 0.87 / coverage 57%. **달성 가능**하다.
- 목표 0.90: 같은 데이터에서 0.92 / 31%. nested에서는 0.88 / 33%로 목표에 살짝 못 미친다.
- 목표 0.95: 같은 데이터에서는 0.95 / 26%라고 나오지만, nested에서는 **0.75 / 5%**. 이 데이터로는 0.95를 약속할 수 없다. 학습 fold마다 τ가 크게 흔들리고, 남는 질의가 몇 개 안 되어 precision이 운에 좌우된다.
- 얼마나 많은 데이터가 필요할까? precision 0.92를 관측했을 때 Wilson 95% 구간은 샘플 25개면 [0.75, 0.98], 200개면 [0.87, 0.95], 1000개면 [0.90, 0.94]다. **"95% precision"을 사인오프하려면 기기가 답한 질의만 수백~천 개의 라벨이 필요하다.** 양산 불량률 상한을 보증할 때 필요한 샘플 수 계산과 같은 문제다.

### 5.4 기기 쪽 구현 — C로 쓴 라우터

학습은 PC에서, 추론(라우팅 결정)은 기기에서 한다. 결정 함수 전체가 C 몇십 줄이다. 1.2절의 순서(프라이버시 → 연결·최신성 → 손잡이 → 신뢰도)를 그대로 코드로 옮겼다.

```c
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

typedef enum { NET_GOOD, NET_POOR, NET_OFFLINE } net_t;
typedef enum { R_DEVICE, R_CLOUD, R_DEVICE_STALE } route_t;   /* STALE: 기기 답 + "최신 정보 확인 불가" 안내 */
typedef struct {
    float logprob[32]; int n;   /* 기기 SLM 답의 토큰별 log-prob */
    float sc;                   /* self-consistency (0, 1/3, 2/3, 1) */
    bool is_private, needs_fresh;
    net_t net; int battery_pct;
} query_t;

static float sum_lp(const query_t *q) { float s = 0; for (int i = 0; i < q->n; i++) s += q->logprob[i]; return s; }
static float p_correct(const query_t *q) {              /* ex7에서 학습한 LR, raw 단위 */
    float z = -0.024f + 0.4916f * sum_lp(q) + 3.211f * q->sc;
    return 1.0f / (1.0f + expf(-z));
}
static route_t route(const query_t *q, float tau) {
    if (q->is_private) return R_DEVICE;                       /* 1. 협상 불가: 기기 밖으로 안 나감 */
    if (q->net == NET_OFFLINE) return q->needs_fresh ? R_DEVICE_STALE : R_DEVICE;
    if (q->needs_fresh) return R_CLOUD;                       /* 2. 기기 모델이 알 수 없는 정보 */
    if (q->net == NET_POOR) tau -= 0.15f;                     /* 3. 링크가 나쁘면 기기 쪽으로 기울임 */
    if (q->battery_pct < 15) tau += 0.10f;                    /*    배터리가 낮으면 클라우드 쪽으로 */
    return p_correct(q) >= tau ? R_DEVICE : R_CLOUD;          /* 4. 신뢰도 */
}
int main(void) {
    static const char *name[] = {"DEVICE", "CLOUD", "DEVICE_STALE"};
    query_t t[] = {
        {{-1.238f, -0.028f, -0.246f}, 3, 0.667f, false, false, NET_GOOD, 80},    /* #44 Montreal (오답) */
        {{-1.238f, -0.028f, -0.246f}, 3, 0.000f, false, false, NET_GOOD, 80},    /* 같은 답, 샘플이 다 다름 */
        {{-1.982f}, 1, 0.0f, true, false, NET_GOOD, 80},                          /* #7 call mom: 개인 */
        {{-3.33f}, 1, 0.333f, false, false, NET_POOR, 80},                        /* #57 고양이 문제, 링크 나쁨 */
        {{-0.5f}, 1, 1.0f, false, true, NET_OFFLINE, 80},                         /* 날씨, 오프라인 */
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
        printf("case %u: sum_lp=%6.3f sc=%.2f p=%.3f -> %s\n", i, sum_lp(&t[i]), t[i].sc,
               p_correct(&t[i]), name[route(&t[i], 0.6f)]);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 router.c -o router -lm && ./router
```

```text
case 0: sum_lp=-1.512 sc=0.67 p=0.798 -> DEVICE
case 1: sum_lp=-1.512 sc=0.00 p=0.317 -> CLOUD
case 2: sum_lp=-1.982 sc=0.00 p=0.269 -> DEVICE
case 3: sum_lp=-3.330 sc=0.33 p=0.356 -> CLOUD
case 4: sum_lp=-0.500 sc=1.00 p=0.950 -> DEVICE_STALE
```

출력에서 볼 것:

- case 0은 2.2절의 Montreal 답이다(log-prob 3개를 그대로 넣었다). 샘플 3개가 Calgary, Montreal., Montreal이라 sc = 2/3, p = 0.798 → **기기에서 답한다 — 틀린 답을**. 3.4절의 한계가 C 코드에서도 그대로 나온다. 라우터는 확률적으로 옳은 결정을 할 뿐, 개별 오답을 다 막지 못한다.
- case 1은 같은 답인데 샘플이 모두 달랐다면(sc = 0) p = 0.317로 클라우드다. sc 한 항의 무게가 크다.
- case 2(엄마에게 전화)는 p = 0.269로 낮지만 **private이라서 기기에 남는다**. 규칙이 점수를 이긴다.
- case 3(링크 나쁨)은 τ가 0.45로 내려갔지만 p = 0.356이라 여전히 클라우드다.
- case 4(오프라인 + 날씨)는 DEVICE_STALE: 기기에서 답하되 "지금은 최신 날씨를 확인할 수 없다"는 안내를 붙인다.
- 실제 기기에서는 `logprob` 배열을 고정 크기 링버퍼로 받고, `expf` 대신 sigmoid 근사표를 써도 된다. 결정에 필요한 연산은 SLM 토큰 하나 생성 비용의 수백만 분의 일이다.

---

## 6. 정책 시뮬레이션 — 측정된 신호로

### 6.1 정확도 vs 클라우드로 보낸 비율

I3 6절은 합성 점수로 정책을 비교했다. 여기서는 **측정된** 신호와 **측정된** 정답(기기 360M, 클라우드 대역 0.5B+CoT)으로 같은 질문을 한다. 점수가 낮은 순서로 질의를 클라우드로 보내면서, 보낸 비율 f에 따라 전체 정확도가 어떻게 변하는지 본다.

- random: 아무 질의나 f만큼 보낸다. 기대 정확도 = (1 − f)·0.588 + f·0.800 (직선).
- oracle: "기기 오답이면서 클라우드 정답"인 질의를 먼저 보낸다. 라우터가 도달할 수 있는 상한.

```python
# 점수가 낮은 순서로 f만큼 "클라우드"(qwen2.5-0.5b CoT)로 보낼 때 전체 정확도
from l3_data import *
import json
p_lr = np.load("p_lr.npy"); N = len(y)
oracle = np.where((y == 0) & (yc == 1), 0, np.where(y == 0, 1, 2)) + np.linspace(0, 0.1, N)  # 기기 오답&클라우드 정답 먼저
scores = {"sum_lp": S["sum_lp"], "self-cons": S["sc"] + 1e-3 * S["sum_lp"],   # sc 동점은 sum_lp로 깨기
          "LR(cv)": p_lr, "oracle": oracle}
def acc_at(score, k):
    route = np.zeros(N, bool); route[np.argsort(score, kind="stable")[:k]] = True
    return np.where(route, yc, y).mean()
curves = {n: [acc_at(s, k) for k in range(N + 1)] for n, s in scores.items()}
curves["random"] = [(1 - k / N) * y.mean() + k / N * yc.mean() for k in range(N + 1)]
json.dump(curves, open("curves.json", "w"))
print("routed% " + "".join(f"{n:>10s}" for n in curves))
for f in (0, 10, 20, 30, 40, 50, 60, 100):
    k = round(f / 100 * N)
    print(f"{f:6d}% " + "".join(f"{curves[n][k]:10.3f}" for n in curves))
for n in curves:   # 클라우드 정확도(0.800)에 처음 도달하는 routed 비율
    k = next((k for k in range(N + 1) if curves[n][k] >= yc.mean() - 1e-9), None)
    print(f"{n:10s} reaches cloud-only accuracy {yc.mean():.3f} at {k / N:.0%} routed")
```

```text
routed%     sum_lp self-cons    LR(cv)    oracle    random
     0%      0.588     0.588     0.588     0.588     0.588
    10%      0.650     0.613     0.637     0.688     0.609
    20%      0.688     0.662     0.688     0.787     0.630
    30%      0.750     0.750     0.738     0.863     0.651
    40%      0.762     0.775     0.787     0.863     0.673
    50%      0.762     0.825     0.812     0.863     0.694
    60%      0.787     0.812     0.800     0.850     0.715
   100%      0.800     0.800     0.800     0.800     0.800
sum_lp     reaches cloud-only accuracy 0.800 at 54% routed
self-cons  reaches cloud-only accuracy 0.800 at 45% routed
LR(cv)     reaches cloud-only accuracy 0.800 at 44% routed
oracle     reaches cloud-only accuracy 0.800 at 21% routed
random     reaches cloud-only accuracy 0.800 at 100% routed
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 380">
<text x="340" y="20" font-size="14" text-anchor="middle">클라우드로 보낸 비율 vs 전체 정확도 (실측 정답, 80문항)</text> <line x1="70" y1="290" x2="610" y2="290" stroke="currentColor"/> <line x1="70" y1="290" x2="70" y2="40" stroke="currentColor"/> <line x1="70.0" y1="290" x2="70.0" y2="294" stroke="currentColor"/><text x="70.0" y="308" font-size="12" text-anchor="middle">0%</text> <line x1="178.0" y1="290" x2="178.0" y2="294" stroke="currentColor"/><text x="178.0" y="308" font-size="12" text-anchor="middle">20%</text> <line x1="286.0" y1="290" x2="286.0" y2="294" stroke="currentColor"/><text x="286.0" y="308" font-size="12" text-anchor="middle">40%</text> <line x1="394.0" y1="290" x2="394.0" y2="294" stroke="currentColor"/><text x="394.0" y="308" font-size="12" text-anchor="middle">60%</text>
<line x1="502.0" y1="290" x2="502.0" y2="294" stroke="currentColor"/><text x="502.0" y="308" font-size="12" text-anchor="middle">80%</text> <line x1="610.0" y1="290" x2="610.0" y2="294" stroke="currentColor"/><text x="610.0" y="308" font-size="12" text-anchor="middle">100%</text> <line x1="66" y1="290.0" x2="70" y2="290.0" stroke="currentColor"/><text x="62" y="294.0" font-size="12" text-anchor="end">0.55</text> <line x1="66" y1="254.3" x2="70" y2="254.3" stroke="currentColor"/><text x="62" y="258.3" font-size="12" text-anchor="end">0.60</text> <line x1="66" y1="218.6" x2="70" y2="218.6" stroke="currentColor"/><text x="62" y="222.6" font-size="12" text-anchor="end">0.65</text> <line x1="66" y1="182.9" x2="70" y2="182.9" stroke="currentColor"/><text x="62" y="186.9" font-size="12" text-anchor="end">0.70</text>
<line x1="66" y1="147.1" x2="70" y2="147.1" stroke="currentColor"/><text x="62" y="151.1" font-size="12" text-anchor="end">0.75</text> <line x1="66" y1="111.4" x2="70" y2="111.4" stroke="currentColor"/><text x="62" y="115.4" font-size="12" text-anchor="end">0.80</text> <line x1="66" y1="75.7" x2="70" y2="75.7" stroke="currentColor"/><text x="62" y="79.7" font-size="12" text-anchor="end">0.85</text> <line x1="66" y1="40.0" x2="70" y2="40.0" stroke="currentColor"/><text x="62" y="44.0" font-size="12" text-anchor="end">0.90</text> <line x1="70" y1="111.4" x2="610" y2="111.4" stroke="#888" stroke-width="1" stroke-dasharray="2 3"/>
<polyline points="70.0,263.2 76.8,261.3 83.5,259.4 90.2,257.5 97.0,255.6 103.8,253.7 110.5,251.8 117.2,249.9 124.0,248.0 130.8,246.1 137.5,244.2 144.2,242.3 151.0,240.4 157.8,238.5 164.5,236.7 171.2,234.8 178.0,232.9 184.8,231.0 191.5,229.1 198.2,227.2 205.0,225.3 211.8,223.4 218.5,221.5 225.2,219.6 232.0,217.7 238.8,215.8 245.5,213.9 252.2,212.0 259.0,210.1 265.8,208.2 272.5,206.3 279.2,204.4 286.0,202.5 292.8,200.6 299.5,198.7 306.2,196.8 313.0,194.9 319.8,193.0 326.5,191.1 333.2,189.2 340.0,187.3 346.8,185.4 353.5,183.5 360.2,181.6 367.0,179.7 373.8,177.8 380.5,175.9 387.2,174.0 394.0,172.1 400.8,170.2 407.5,168.3 414.2,166.5 421.0,164.6 427.8,162.7 434.5,160.8 441.2,158.9 448.0,157.0 454.8,155.1 461.5,153.2 468.2,151.3 475.0,149.4 481.8,147.5 488.5,145.6 495.2,143.7 502.0,141.8 508.8,139.9 515.5,138.0 522.2,136.1 529.0,134.2 535.8,132.3 542.5,130.4 549.2,128.5 556.0,126.6 562.8,124.7 569.5,122.8 576.2,120.9 583.0,119.0 589.8,117.1 596.5,115.2 603.2,113.3 610.0,111.4" fill="none" stroke="#888" stroke-width="2" stroke-dasharray="6 4"/>
<polyline points="70.0,263.2 76.8,254.3 83.5,254.3 90.2,245.4 97.0,245.4 103.8,236.4 110.5,236.4 117.2,227.5 124.0,218.6 130.8,218.6 137.5,218.6 144.2,209.6 151.0,200.7 157.8,191.8 164.5,191.8 171.2,200.7 178.0,191.8 184.8,191.8 191.5,182.9 198.2,173.9 205.0,165.0 211.8,165.0 218.5,165.0 225.2,156.1 232.0,147.1 238.8,138.2 245.5,138.2 252.2,138.2 259.0,138.2 265.8,138.2 272.5,138.2 279.2,138.2 286.0,138.2 292.8,147.1 299.5,147.1 306.2,147.1 313.0,147.1 319.8,147.1 326.5,138.2 333.2,138.2 340.0,138.2 346.8,129.3 353.5,120.4 360.2,111.4 367.0,111.4 373.8,111.4 380.5,120.4 387.2,111.4 394.0,120.4 400.8,111.4 407.5,120.4 414.2,120.4 421.0,120.4 427.8,120.4 434.5,120.4 441.2,120.4 448.0,120.4 454.8,120.4 461.5,120.4 468.2,120.4 475.0,120.4 481.8,120.4 488.5,111.4 495.2,111.4 502.0,111.4 508.8,111.4 515.5,111.4 522.2,111.4 529.0,111.4 535.8,111.4 542.5,111.4 549.2,111.4 556.0,111.4 562.8,111.4 569.5,111.4 576.2,111.4 583.0,111.4 589.8,111.4 596.5,111.4 603.2,111.4 610.0,111.4" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<polyline points="70.0,263.2 76.8,263.2 83.5,254.3 90.2,245.4 97.0,236.4 103.8,227.5 110.5,227.5 117.2,227.5 124.0,227.5 130.8,236.4 137.5,227.5 144.2,218.6 151.0,218.6 157.8,209.6 164.5,209.6 171.2,200.7 178.0,191.8 184.8,191.8 191.5,182.9 198.2,173.9 205.0,173.9 211.8,173.9 218.5,165.0 225.2,165.0 232.0,156.1 238.8,156.1 245.5,156.1 252.2,156.1 259.0,147.1 265.8,138.2 272.5,138.2 279.2,129.3 286.0,120.4 292.8,120.4 299.5,120.4 306.2,111.4 313.0,111.4 319.8,111.4 326.5,102.5 333.2,102.5 340.0,102.5 346.8,102.5 353.5,93.6 360.2,93.6 367.0,93.6 373.8,102.5 380.5,111.4 387.2,111.4 394.0,111.4 400.8,111.4 407.5,120.4 414.2,120.4 421.0,120.4 427.8,129.3 434.5,129.3 441.2,120.4 448.0,120.4 454.8,120.4 461.5,120.4 468.2,111.4 475.0,111.4 481.8,111.4 488.5,111.4 495.2,111.4 502.0,111.4 508.8,111.4 515.5,111.4 522.2,111.4 529.0,111.4 535.8,111.4 542.5,111.4 549.2,111.4 556.0,111.4 562.8,111.4 569.5,111.4 576.2,111.4 583.0,111.4 589.8,111.4 596.5,111.4 603.2,111.4 610.0,111.4" fill="none" stroke="#e08a3c" stroke-width="2"/>
<polyline points="70.0,263.2 76.8,272.1 83.5,263.2 90.2,263.2 97.0,263.2 103.8,254.3 110.5,254.3 117.2,245.4 124.0,236.4 130.8,227.5 137.5,227.5 144.2,227.5 151.0,218.6 157.8,209.6 164.5,209.6 171.2,200.7 178.0,191.8 184.8,191.8 191.5,191.8 198.2,182.9 205.0,182.9 211.8,182.9 218.5,173.9 225.2,165.0 232.0,156.1 238.8,147.1 245.5,147.1 252.2,147.1 259.0,138.2 265.8,129.3 272.5,129.3 279.2,120.4 286.0,111.4 292.8,102.5 299.5,111.4 306.2,102.5 313.0,111.4 319.8,111.4 326.5,111.4 333.2,111.4 340.0,111.4 346.8,111.4 353.5,102.5 360.2,102.5 367.0,93.6 373.8,93.6 380.5,93.6 387.2,93.6 394.0,93.6 400.8,93.6 407.5,93.6 414.2,93.6 421.0,93.6 427.8,93.6 434.5,93.6 441.2,93.6 448.0,93.6 454.8,93.6 461.5,93.6 468.2,93.6 475.0,93.6 481.8,102.5 488.5,102.5 495.2,111.4 502.0,111.4 508.8,111.4 515.5,111.4 522.2,111.4 529.0,111.4 535.8,111.4 542.5,111.4 549.2,111.4 556.0,111.4 562.8,111.4 569.5,111.4 576.2,111.4 583.0,111.4 589.8,111.4 596.5,111.4 603.2,111.4 610.0,111.4" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<polyline points="70.0,263.2 76.8,254.3 83.5,245.4 90.2,236.4 97.0,227.5 103.8,218.6 110.5,209.6 117.2,200.7 124.0,191.8 130.8,182.9 137.5,173.9 144.2,165.0 151.0,156.1 157.8,147.1 164.5,138.2 171.2,129.3 178.0,120.4 184.8,111.4 191.5,102.5 198.2,93.6 205.0,84.6 211.8,75.7 218.5,66.8 225.2,66.8 232.0,66.8 238.8,66.8 245.5,66.8 252.2,66.8 259.0,66.8 265.8,66.8 272.5,66.8 279.2,66.8 286.0,66.8 292.8,66.8 299.5,66.8 306.2,66.8 313.0,66.8 319.8,66.8 326.5,66.8 333.2,66.8 340.0,66.8 346.8,66.8 353.5,66.8 360.2,66.8 367.0,66.8 373.8,75.7 380.5,75.7 387.2,75.7 394.0,75.7 400.8,75.7 407.5,75.7 414.2,75.7 421.0,75.7 427.8,75.7 434.5,75.7 441.2,75.7 448.0,75.7 454.8,75.7 461.5,75.7 468.2,75.7 475.0,75.7 481.8,75.7 488.5,75.7 495.2,75.7 502.0,75.7 508.8,75.7 515.5,75.7 522.2,75.7 529.0,75.7 535.8,84.6 542.5,84.6 549.2,93.6 556.0,93.6 562.8,93.6 569.5,102.5 576.2,102.5 583.0,102.5 589.8,102.5 596.5,102.5 603.2,102.5 610.0,111.4" fill="none" stroke="#d0564a" stroke-width="2" stroke-dasharray="3 3"/>
<line x1="80" y1="342" x2="104" y2="342" stroke="#d0564a" stroke-width="2"/><text x="110" y="346" font-size="12">oracle (상한)</text> <line x1="280" y1="342" x2="304" y2="342" stroke="#e08a3c" stroke-width="2"/><text x="310" y="346" font-size="12">LR(sum_lp, sc) post</text> <line x1="480" y1="342" x2="504" y2="342" stroke="#3f9a6b" stroke-width="2"/><text x="510" y="346" font-size="12">MiniLM pre-router</text> <line x1="80" y1="362" x2="104" y2="362" stroke="#4a7bd0" stroke-width="2"/><text x="110" y="366" font-size="12">sum_lp post</text> <line x1="280" y1="362" x2="304" y2="362" stroke="#888" stroke-width="2"/><text x="310" y="366" font-size="12">random</text> <text x="78" y="105.4" font-size="12">cloud only 0.800</text> <text x="340.0" y="324" font-size="13" text-anchor="middle">클라우드로 보낸 질의 비율</text>
</svg>
```

그림 5 — 클라우드 비율 vs 전체 정확도 (실측 정답). 점선 회색이 random, 빨간 점선이 oracle. 실제 라우터들(파랑·주황·초록)은 random보다 확실히 위에 있고, 40~55% 구간에서 클라우드 전용(0.800)을 **넘는다**.

출력에서 볼 것:

- **random은 클라우드 전용 정확도에 도달하려면 100%를 보내야 한다.** 라우터들은 44~54%만 보내고 도달한다. 클라우드 호출(=$, 무선 에너지)을 절반으로 줄인다는 뜻이다.
- **50%를 보내면 self-consistency 0.825, LR 0.812 — 클라우드 전용(0.800)보다 높다.** 2.4절에서 본 대로 상식 질문 몇 개는 기기만 맞혔다(기기만 정답 5개). 하이브리드가 둘 중 나은 쪽을 고르면 어느 한쪽보다 좋아질 수 있다. 진짜 클라우드 LLM이었다면 이 효과는 작겠지만, "작은 모델이 특정 영역(사용자 개인 데이터, 기기 명령)에서 더 낫다"는 상황은 실제로 흔하다.
- **oracle은 21%만 보내고 0.800, 27%에서 0.863**. 실제 라우터와 oracle 사이의 틈(약 20%p의 클라우드 비율)이 3.4절의 "모르는 걸 모르는" 비용이다.

### 6.2 지연 · 에너지 · 비용 · 프라이버시를 함께

정확도만 보면 self-consistency가 좋아 보인다. 그러나 sc는 **샘플 3개를 더 생성**해야 얻는다. 비용을 붙여 본다. 측정값과 가정을 구분해 둔다.

| 항목 | 값 | 출처 |
|---|---|---|
| 기기 지연 | 질의별 prompt_ms + predicted_ms (평균 0.22 s, 중앙값 0.19 s) | **측정** (M2 CPU, 360M Q8_0) |
| 기기 에너지 | 11 mJ/token × (생성 토큰 + 0.1 × prompt 토큰) | 가정: D7의 1B LLM 31.8 mJ/token을 파라미터 수로 환산 |
| sc 비용 | 샘플 3개: 에너지 3배 추가, 지연은 batch로 1회분 추가 | 가정 |
| 무선 에너지 | Wi-Fi 질의 1회 135 mJ | D7 6.3절의 가정값 |
| 클라우드 지연 | RTT 0.25 s + 첫 토큰 0.15 s + 생성 토큰 ÷ 80 tok/s | 가정 (토큰 수는 **측정**) |
| 클라우드 $ | 입력 $0.5 / 1M token, 출력 $1.5 / 1M token | 가정 |

```python
# 정책별 정확도 · 지연 · 기기 에너지 · 클라우드 비용 · 프라이버시 위반 (측정 + 가정 상수)
from l3_data import *
p_lr = np.load("p_lr.npy")
prm_d = np.array([r["prompt_n"] for r in dev]); prm_c = np.array([r["prompt_n"] for r in cld])
MJ_TOK, RADIO_MJ = 11.0, 135.0           # 가정: 360M SLM 11 mJ/token (D7의 1B 31.8을 크기로 환산), Wi-Fi 질의 135 mJ (D7)
RTT, C_TTFT, C_TPS = 0.25, 0.15, 80.0    # 가정: RTT 0.25 s, 클라우드 첫 토큰 0.15 s, 80 tok/s
USD_IN, USD_OUT = 0.5e-6, 1.5e-6         # 가정: $/token (입력, 출력)
dev_s = dev_ms / 1000                    # 측정: M2 CPU, smollm2-360m Q8_0 (prefill + 답 생성)
e_dev = MJ_TOK * (dev_ntok + 0.1 * prm_d)
cld_s = RTT + C_TTFT + cld_ntok / C_TPS  # CoT는 답이 끝에 나오므로 전체 생성 시간
usd = USD_IN * prm_c + USD_OUT * cld_ntok
mid = (S["sum_lp"] >= -4) & (S["sum_lp"] < -1)                         # 애매한 구간에서만 sc를 잰다
def policy(route, pre=False, sc=False):  # sc: self-consistency 샘플 3개를 더 돌린 질의 (bool 또는 마스크)
    sc = np.broadcast_to(sc, route.shape); ran = ~(route & pre)          # pre=True면 라우팅된 질의는 기기 생성 안 함
    lat = np.where(route, cld_s, 0) + ran * dev_s + (ran & sc) * dev_s   # 가정: 샘플 3개는 batch로 1회 시간
    e = np.where(route, RADIO_MJ, 0) + ran * e_dev * np.where(sc, 4, 1)
    acc = np.where(route, yc, y).mean()
    return acc, route.mean(), np.percentile(lat, 50), np.percentile(lat, 95), e.mean(), usd[route].sum() / len(y) * 1e6, (route & priv).sum()
rows = {"device only": policy(np.zeros(80, bool)), "cloud only": policy(np.ones(80, bool), pre=True),
        "post: LR(sum_lp,sc)<0.6": policy(p_lr < 0.6, sc=True),
        "post: sum_lp < -2.0": policy(S["sum_lp"] < -2.0),
        "  + privacy override": policy((S["sum_lp"] < -2.0) & ~priv),
        "LR + privacy override": policy((p_lr < 0.6) & ~priv, sc=True),
        "staged: sum_lp, then sc": policy((S["sum_lp"] < -4) | (mid & (S["sc"] < 0.5)), sc=mid)}
print(f"{'policy':26s}{'acc':>6s}{'cloud':>7s}{'p50 s':>7s}{'p95 s':>7s}{'mJ/q':>7s}{'$/1M q':>8s}{'priv':>5s}")
for n, (a, c, l5, l95, e, u, v) in rows.items():
    print(f"{n:26s}{a:6.3f}{c:7.0%}{l5:7.2f}{l95:7.2f}{e:7.0f}{u:8.0f}{v:5d}")
```

```text
policy                       acc  cloud  p50 s  p95 s   mJ/q  $/1M q priv
device only                0.588     0%   0.19   0.50    238       0    0
cloud only                 0.800   100%   0.59   2.14    135      86    7
post: LR(sum_lp,sc)<0.6    0.787    42%   0.50   2.33   1011      39    4
post: sum_lp < -2.0        0.750    42%   0.28   2.08    296      46    2
  + privacy override       0.750    40%   0.26   2.08    292      45    0
LR + privacy override      0.787    38%   0.42   2.33   1004      36    0
staged: sum_lp, then sc    0.787    42%   0.42   2.32    764      39    4
```

출력에서 볼 것:

- **device only**: 가장 빠르고(p50 0.19 s) $0, 그러나 정확도 0.588.
- **cloud only**: 정확도 0.800, 기기 에너지 135 mJ/q로 **가장 적다** (D7 6.3절 결론과 같다: LLM은 오프로드가 배터리에 싸다). 그러나 p95 2.14 s, $86/1M 질의, **개인 질의 7개가 기기 밖으로 나갔다**.
- **post: LR(sum_lp, sc)**: 정확도 0.787, 클라우드 42%, $ 절반 이하. 그런데 **에너지 1011 mJ/q** — 클라우드 전용의 7.5배다. sc를 위해 모든 질의에서 샘플 3개를 더 만들었기 때문이다. **가장 좋은 신호가 가장 비싼 신호다.**
- **post: sum_lp < −2**: sc 없이 sum_lp만 쓰면 정확도 0.750으로 떨어지지만 에너지는 296 mJ/q. sum_lp는 greedy 생성에서 **공짜**로 나온다.
- **staged**: sum_lp가 애매한 구간(−4 ~ −1)에서만 sc를 잰다. 정확도는 LR과 같은 0.787, 에너지 764 mJ/q. 확실한 질의에서 비싼 신호를 아낀다. SSD에서 hard-decision이 실패한 블록에만 soft-decision read를 거는 것과 같은 구조다.
- **privacy override**: private 질의를 라우팅 대상에서 빼도 정확도가 **변하지 않았다** (0.750 → 0.750, 0.787 → 0.787). 아래에서 이유를 본다.

### 6.3 프라이버시 override의 실제 비용

7개 private 질의에서 기기 정답률은 5/7, 클라우드 대역도 5/7이고 **틀린 질의가 같다** (혈압 평균, 카드값 합). 그래서 override가 정확도를 깎지 않았다. 이것은 이 세트의 우연이지만 방향은 일반적이다.

- 개인 질의는 **기기 명령**(전화, 알람)이 많다. 기기가 원래 잘하는 범주다.
- 개인 질의의 "어려운 부분"은 대개 **개인 데이터 자체**(연락처, 일정, 건강 기록)이고, 그 데이터는 원래 기기에 있다. 클라우드가 더 똑똑해도 그 데이터 없이는 못 푼다.
- 정말 클라우드의 추론이 필요한 개인 질의(예: 혈압 추세 해석)에는 H6의 도구를 쓴다. 개인 식별 부분을 지우거나 가명화한 뒤(redaction) 계산 부분만 보낸다: "120, 130, 125의 평균"은 그 자체로는 개인 정보가 아니다. 단 이 판단을 하는 분류기·규칙도 기기에서 돌아야 한다.

override의 최악 비용은 이렇게 한 줄로 계산할 수 있다: (private이면서 기기 오답인 질의 비율) × (클라우드 정답률). 이 세트에서 2/80 × 1.0 = 2.5%p가 상한이다. **이 값을 측정해서 제품 문서에 적어 두는 것**이 "프라이버시 때문에 품질을 얼마나 포기했나"라는 질문에 대한 엔지니어의 답이다.

---

## 7. Pre-generation vs post-generation 라우팅

### 7.1 두 방식의 시간선

- **post-generation**: SLM이 답을 다 만든 뒤 신호를 보고 결정한다. 신호가 풍부하다(이 노트 3–5절). 하지만 클라우드로 보낼 질의에서는 **SLM 생성 시간이 통째로 낭비**되고, 지연이 "기기 + 클라우드"로 직렬이 된다.
- **pre-generation**: 질의 텍스트만 보고 생성 전에 결정한다. 클라우드로 갈 질의는 SLM을 아예 안 돌린다. 판단이 빠르고 싸다(임베딩 수 ms). 하지만 "이 질의에 대한 이 모델의 답"을 보지 못한다.
- **speculative**: 둘 다 동시에 시작하고, 기기 답이 자신 있으면 클라우드를 취소한다. 지연은 짧지만 클라우드 비용을 거의 모든 질의에서 낸다 (8.4절).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 330">
<text x="340" y="20" font-size="14" text-anchor="middle">라우팅된 질의 하나의 시간선 (기기 0.22 s · 클라우드 0.60 s 예시)</text> <text x="140" y="72" font-size="13" text-anchor="end">pre-gen</text> <rect x="150.0" y="50" width="2.3" height="18" fill="#888" fill-opacity="0.8"/><text x="151.2" y="63" font-size="11" text-anchor="middle"></text> <rect x="152.3" y="50" width="346.7" height="18" fill="#e08a3c" fill-opacity="0.8"/><text x="325.6" y="63" font-size="11" text-anchor="middle">클라우드 0.60</text> <line x1="499.0" y1="46" x2="499.0" y2="94" stroke="#d0564a" stroke-width="2"/><text x="503.0" y="94" font-size="11">답 0.60 s</text> <text x="140" y="152" font-size="13" text-anchor="end">post-gen</text> <rect x="150.0" y="130" width="127.1" height="18" fill="#4a7bd0" fill-opacity="0.8"/><text x="213.6" y="143" font-size="11" text-anchor="middle">SLM 0.22</text>
<rect x="277.1" y="152" width="346.7" height="18" fill="#e08a3c" fill-opacity="0.8"/><text x="450.4" y="165" font-size="11" text-anchor="middle">클라우드 0.60</text> <line x1="623.8" y1="126" x2="623.8" y2="174" stroke="#d0564a" stroke-width="2"/><text x="627.8" y="174" font-size="11">답 0.82 s</text> <text x="140" y="232" font-size="13" text-anchor="end">speculative</text> <rect x="150.0" y="210" width="127.1" height="18" fill="#4a7bd0" fill-opacity="0.8"/><text x="213.6" y="223" font-size="11" text-anchor="middle">SLM 0.22</text> <rect x="150.0" y="232" width="346.7" height="18" fill="#e08a3c" fill-opacity="0.8"/><text x="323.3" y="245" font-size="11" text-anchor="middle">클라우드 0.60</text> <line x1="496.7" y1="206" x2="496.7" y2="254" stroke="#d0564a" stroke-width="2"/><text x="500.7" y="254" font-size="11">답 0.60 s</text> <line x1="150" y1="300" x2="670.0" y2="300" stroke="currentColor"/>
<line x1="150.0" y1="300" x2="150.0" y2="304" stroke="currentColor"/><text x="150.0" y="318" font-size="12" text-anchor="middle">0.0 s</text> <line x1="265.6" y1="300" x2="265.6" y2="304" stroke="currentColor"/><text x="265.6" y="318" font-size="12" text-anchor="middle">0.2 s</text> <line x1="381.1" y1="300" x2="381.1" y2="304" stroke="currentColor"/><text x="381.1" y="318" font-size="12" text-anchor="middle">0.4 s</text> <line x1="496.7" y1="300" x2="496.7" y2="304" stroke="currentColor"/><text x="496.7" y="318" font-size="12" text-anchor="middle">0.6 s</text> <line x1="612.2" y1="300" x2="612.2" y2="304" stroke="currentColor"/><text x="612.2" y="318" font-size="12" text-anchor="middle">0.8 s</text> <text x="670.0" y="318" font-size="12" text-anchor="end">시간</text> <text x="20" y="282" font-size="11">회색 = pre-router (MiniLM 약 4 ms)</text>
</svg>
```

그림 6 — 클라우드로 가는 질의 하나의 시간선 (기기 0.22 s는 측정 평균, 클라우드 0.60 s는 가정 모델의 중앙값). post-gen은 0.82 s, pre-gen과 speculative는 0.60 s.

### 7.2 코드로 확인 — MiniLM pre-router vs 키워드 vs post-gen

pre-router는 질의 텍스트만 입력으로 받아 "기기 SLM이 맞힐까"를 예측한다. 두 가지를 만든다.

- 키워드 특징 4개: 한글 포함 여부, 숫자 개수, 단어 수, "how many / average / discount…" 같은 문장제 단서.
- all-MiniLM-L6-v2 문장 임베딩(384차원, mean pooling + 정규화) 위의 logistic regression.

둘 다 5-fold cross-validation 예측으로 평가한다.

```python
# Pre-generation router: 질의 텍스트만 보고 "기기 SLM이 맞힐까"를 예측 (MiniLM 임베딩 vs 키워드 특징)
import re, time, torch
from l3_data import *
from transformers import AutoTokenizer, AutoModel
from sklearn.linear_model import LogisticRegression
from sklearn.model_selection import StratifiedKFold, cross_val_predict
from sklearn.metrics import roc_auc_score
name = "sentence-transformers/all-MiniLM-L6-v2"
tok, enc = AutoTokenizer.from_pretrained(name, local_files_only=True), AutoModel.from_pretrained(name, local_files_only=True).eval()
def embed(texts):
    with torch.no_grad():
        b = tok(texts, padding=True, return_tensors="pt"); h = enc(**b).last_hidden_state
        m = b["attention_mask"].unsqueeze(-1).float()
        return torch.nn.functional.normalize((h * m).sum(1) / m.sum(1), dim=1).numpy()
texts = [q[1] for q in Q]; E = embed(texts)
t0 = time.perf_counter(); [embed([t]) for t in texts]; emb_ms = (time.perf_counter() - t0) / len(texts) * 1000
K = np.array([[bool(re.search("[가-힣]", t)), len(re.findall(r"\d+", t)), len(t.split()),
               bool(re.search(r"(?i)how many|how much|average|older|left|change|discount", t))] for t in texts], float)
cv = StratifiedKFold(5, shuffle=True, random_state=0)
p_kw = cross_val_predict(LogisticRegression(max_iter=1000), K, y, cv=cv, method="predict_proba")[:, 1]
p_emb = cross_val_predict(LogisticRegression(C=10, max_iter=1000), E, y, cv=cv, method="predict_proba")[:, 1]
p_post = np.load("p_lr.npy")
def show(n, route, direct):                            # direct: 기기 생성 없이 바로 클라우드로 간 질의
    waste = dev_ms[route & ~direct].sum() / route.sum()
    print(f"{n:26s} acc {np.where(route, yc, y).mean():.3f}  routed {route.mean():.0%}  device ms wasted/routed q {waste:4.0f}")
for n, p in [("keyword pre-router", p_kw), ("MiniLM pre-router", p_emb), ("post-gen LR(sum_lp,sc)", p_post)]:
    route = np.zeros(80, bool); route[np.argsort(p, kind="stable")[:33]] = True     # 약 41%를 클라우드로
    print(f"AUROC {roc_auc_score(y, p):.3f} |", end=" "); show(n, route, route if "pre" in n else ~route)
d = p_emb < 0.25; show("2-stage: MiniLM<0.25, LR<0.6", d | (p_post < 0.6), d)
print(f"MiniLM-L6 embed: {emb_ms:.1f} ms/query on M2 CPU, {sum(p.numel() for p in enc.parameters()) / 1e6:.1f} M params")
np.save("p_emb.npy", p_emb); np.save("p_kw.npy", p_kw)
```

```text
AUROC 0.848 | keyword pre-router         acc 0.787  routed 41%  device ms wasted/routed q    0
AUROC 0.859 | MiniLM pre-router          acc 0.812  routed 41%  device ms wasted/routed q    0
AUROC 0.854 | post-gen LR(sum_lp,sc)     acc 0.787  routed 41%  device ms wasted/routed q  309
2-stage: MiniLM<0.25, LR<0.6 acc 0.812  routed 45%  device ms wasted/routed q  133
MiniLM-L6 embed: 4.5 ms/query on M2 CPU, 22.7 M params
```

출력에서 볼 것:

- **pre-router가 post-gen만큼 좋다** (AUROC 0.848 · 0.859 vs 0.854). 같은 41%를 보낼 때 MiniLM pre-router의 정확도는 0.812로 오히려 높다. 3.4절에서 본 대로 이 세트의 정답 여부는 상당 부분 **범주**로 결정되고, 범주는 질의 텍스트에서 보인다(한국어인가, 문장제인가).
- **post-gen은 클라우드로 보낸 질의마다 기기 시간 309 ms를 낭비**한다 (sc 샘플 시간은 빼고). pre-router는 0.
- **2-stage**(MiniLM 점수 < 0.25면 바로 클라우드, 나머지는 SLM을 돌리고 LR로 판단): 정확도 0.812를 유지하면서 낭비를 133 ms로 줄인다. 확실히 못 하는 질의는 앞에서 빼고, 애매한 질의만 SLM 답을 본다.
- MiniLM 임베딩은 질의당 **4.5 ms**(M2 CPU, 22.7 M params). SLM(360M) 생성 0.2 s에 비하면 작다. 크기는 fp32 약 91 MB, int8로 양자화해도 약 23 MB라서 웨어러블에서는 작지 않다. 키워드 pre-router(AUROC 0.848, 메모리 0)가 생각보다 강하다는 점도 기억해 둘 것. 그리고 L5(온디바이스 메모리·RAG)에서 쓰는 임베딩 모델과 **같은 모델을 공유**할 수 있다 — 라우터를 위해 따로 모델을 올릴 필요가 없다.

### 7.3 pre-router의 함정

- **이 세트는 범주가 너무 깨끗하다.** 실제 사용자 질의는 "Paris 날씨 어때? 그리고 15+27은?"처럼 섞이고, 범주 경계가 흐리다. pre-router 성능은 실제 로그 분포에서 다시 재야 한다.
- **pre-router는 모델 업데이트를 모른다.** OTA로 SLM을 바꾸면(J5) 능력 지도가 바뀌는데, pre-router는 옛 모델의 능력 지도를 기억한다. SLM과 pre-router를 **같은 버전 번호로 묶어** 배포하고 함께 재학습해야 한다.
- 같은 이유로 pre-router는 **"질의 유형"을 배우지 "답의 질"을 배우지 않는다**. 범주 안의 개별 오답(Montreal)은 post-gen 신호만 잡을 수 있다 — 그것도 일부만. 그래서 실무에서는 2-stage가 기본형이다.

---

## 8. 실패 처리 — 클라우드가 느리거나 죽었을 때

### 8.1 실패 모드 목록

| 실패 | 증상 | 감지 | 처리 |
|---|---|---|---|
| 링크 없음 (오프라인) | 연결 시도 즉시 실패 또는 무응답 | OS 링크 상태, 최근 실패 | 클라우드 시도 자체를 생략 (circuit breaker) |
| 느린 링크 | RTT 수 초, 패킷 유실 | RTT EWMA, timeout | timeout 후 기기 답으로 fallback |
| 서버 오류 | 5xx, rate limit(429) | HTTP 코드 | 재시도 금지(음성 비서의 지연 예산 안에서는), fallback |
| 스트림 중단 | 답 절반에서 끊김 | 토큰 간 간격 timeout | 받은 부분 + 기기가 이어서 완성, 또는 사과 |
| 인증 만료 | 401 | HTTP 코드 | 백그라운드 갱신, 이번 질의는 fallback |
| 기기 자원 부족 | 메모리 압박, 발열 throttling | L2의 메모리 감시, 온도 | 반대로 클라우드 쪽으로 (τ↑), 또는 더 작은 모델 |

### 8.2 코드로 확인 — 네 가지 처리 방식 × 세 가지 네트워크

같은 41% 라우팅 결정(post-gen LR)을 두고, 처리 방식만 바꿔서 비교한다. 기기 지연과 정답은 **측정값**, 네트워크는 가정이다.

- good: RTT 중앙값 0.25 s, 유실 없음
- degraded: RTT 중앙값 1.0 s(로그정규 σ 0.6), 15% 유실
- outage: 100% 유실

처리 방식:

- no fallback: 클라우드 답을 끝까지 기다린다. 안 오면 10 s 뒤 에러 (정답 0으로 침).
- pre+timeout: 클라우드에 바로 보내고, 3 s 안에 안 오면 그때 기기 생성 시작.
- post+timeout: 기기 답을 먼저 만들고 클라우드에 보낸다. 3 s 안에 안 오면 **이미 있는 기기 답**을 쓴다.
- speculative: 처음부터 둘 다 시작. 라우팅 대상이면 3 s까지 클라우드를 기다린다.
- post+breaker: post+timeout + circuit breaker (연속 2번 실패하면 이후 라우팅 질의 16개당 1개만 probe).

```python
# 클라우드가 느리거나 죽었을 때의 처리 방식 비교 — 측정 기기 지연 + 가정 네트워크 (같은 41% 라우팅 마스크)
from l3_data import *
rng = np.random.default_rng(0)
rt = np.zeros(80, bool); rt[np.argsort(np.load("p_lr.npy"), kind="stable")[:33]] = True
dev_s, T, HANG = dev_ms / 1000, 3.0, 10.0                     # timeout 3 s, fallback 없으면 10 s 뒤 에러
NET = {"good": (0.25, 0.4, 0.00), "degraded": (1.0, 0.6, 0.15), "outage": (1.0, 0.6, 1.00)}  # RTT 중앙값, σ, 유실률 (가정)
def breaker(c):          # 연속 2번 실패하면 회로 open: 라우팅 질의 16개마다 1개만 probe로 보낸다
    lat, use, fails = np.copy(dev_s), np.zeros(80, bool), 0
    for j, i in enumerate(np.flatnonzero(rt)):
        if fails >= 2 and j % 16: continue                    # open: 시도하지 않고 기기 답
        ok = c[i] <= T; fails = 0 if ok else fails + 1
        lat[i] = dev_s[i] + (c[i] if ok else T); use[i] = ok
    return lat, use
def sim(net, mode, R=200):
    med, sig, loss = NET[net]; L, A, U = [], [], []
    for _ in range(R):
        c = rng.lognormal(np.log(med), sig, 80) + 0.15 + cld_ntok / 80.0
        c[rng.random(80) < loss] = np.inf; ok = c <= T; use = rt & ok
        if mode == "no fallback":   lat = np.where(rt, dev_s + np.minimum(c, HANG), dev_s); use = rt & (c < HANG)
        elif mode == "pre+timeout": lat = np.where(rt, np.where(ok, c, T + dev_s), dev_s)
        elif mode == "post+timeout": lat = np.where(rt, dev_s + np.where(ok, c, T), dev_s)
        elif mode == "speculative": lat = np.where(rt, np.where(ok, np.maximum(c, dev_s), T), dev_s)
        else:                       lat, use = breaker(c)
        acc = np.where(use, yc, np.where(rt & (mode == "no fallback"), 0, y))
        L.append(lat); A.append(acc.mean())
    L = np.concatenate(L); return np.mean(A), L.mean(), np.percentile(L, 95)
print(f"{'network':9s}{'mode':14s}{'acc':>6s}{'mean s':>7s}{'p95 s':>7s}")
for net in NET:
    for mode in ("no fallback", "pre+timeout", "post+timeout", "speculative", "post+breaker"):
        a, lm, l95 = sim(net, mode); print(f"{net:9s}{mode:14s}{a:6.3f}{lm:7.2f}{l95:7.2f}")
```

```text
network  mode             acc mean s  p95 s
good     no fallback    0.787   0.62   2.32
good     pre+timeout    0.774   0.49   1.90
good     post+timeout   0.775   0.61   2.29
good     speculative    0.775   0.49   1.89
good     post+breaker   0.774   0.61   2.29
degraded no fallback    0.742   1.52  10.21
degraded pre+timeout    0.719   0.92   3.28
degraded post+timeout   0.723   1.02   3.28
degraded speculative    0.719   0.90   3.00
degraded post+breaker   0.672   0.74   3.22
outage   no fallback    0.500   4.35  10.50
outage   pre+timeout    0.588   1.46   3.50
outage   post+timeout   0.588   1.46   3.50
outage   speculative    0.588   1.33   3.00
outage   post+breaker   0.588   0.37   0.83
```

출력에서 볼 것:

- **good 네트워크에서는 no fallback이 정확도가 가장 높다** (0.787 vs 0.775). timeout 3 s가 **느리지만 맞는** CoT 답 일부를 잘랐기 때문이다(최대 258 토큰 → 3.6 s). timeout은 공짜가 아니다. 클라우드 쪽 토큰 예산과 함께 정해야 한다.
- **degraded에서 no fallback은 p95 10.2 s**. 라우팅된 질의의 15%가 응답을 못 받고 10 s를 기다리다 에러가 난다(전체의 약 6%라서 p95에 걸린다). 음성 비서에서는 쓸 수 없다. timeout을 두면 p95가 3.0~3.3 s로 묶인다.
- **outage에서 no fallback은 정확도 0.500**(기기 정답률 0.588보다 낮다!). 클라우드에 보낸 질의를 전부 잃었기 때문이다. fallback이 있으면 최소한 기기 정확도 0.588은 지킨다.
- **pre+timeout과 post+timeout은 outage에서 지연이 같다** (둘 다 T + 기기 시간 = 3.5 s p95). 차이는 good 네트워크에서 나온다: pre는 기기 시간을 안 써서 평균 0.49 s, post는 0.61 s.
- **speculative**는 모든 네트워크에서 p95가 가장 짧다(T = 3.0 s에서 묶임). 대신 6.2절의 비용 표에서 클라우드 호출이 100%가 된다.
- **circuit breaker는 outage에서 결정적이다**: 평균 1.46 s → 0.37 s, p95 3.5 s → 0.83 s. 죽은 링크에 매번 3 s씩 물리지 않는다. 그런데 **degraded에서는 정확도를 0.723 → 0.672로 깎는다**. 15% 유실에서 연속 2번 실패는 흔해서 회로가 너무 자주 열린다. breaker의 "연속 실패 수"와 probe 주기는 링크 통계에 맞춰 튜닝해야 한다 — Don이 PCIe link retrain 정책이나 I2C bus recovery의 재시도 한도를 정할 때와 같은 문제다.

### 8.3 Circuit breaker — 실패를 기억하는 상태 머신

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 260">
<text x="340" y="20" font-size="14" text-anchor="middle">클라우드 링크 circuit breaker — 실패를 기억해서 timeout을 반복해 물지 않는다</text> <ellipse cx="120" cy="130" rx="78" ry="34" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b" stroke-width="2"/> <text x="120" y="127" font-size="13" text-anchor="middle">CLOSED</text><text x="120" y="145" font-size="12" text-anchor="middle">클라우드 시도</text> <ellipse cx="340" cy="130" rx="78" ry="34" fill="#d0564a" fill-opacity="0.15" stroke="#d0564a" stroke-width="2"/> <text x="340" y="127" font-size="13" text-anchor="middle">OPEN</text><text x="340" y="145" font-size="12" text-anchor="middle">기기만 사용</text> <ellipse cx="560" cy="130" rx="78" ry="34" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="2"/> <text x="560" y="127" font-size="13" text-anchor="middle">HALF-OPEN</text><text x="560" y="145" font-size="12" text-anchor="middle">probe 1개</text>
<line x1="198.0" y1="118.0" x2="262.0" y2="118.0" stroke="currentColor" stroke-width="1.5"/><polygon points="262.0,118.0 254.6,121.1 254.6,114.9" fill="currentColor"/> <text x="230" y="80" font-size="12" text-anchor="middle">연속 실패 ≥ 2</text><text x="230" y="96" font-size="12" text-anchor="middle">(timeout·5xx)</text> <line x1="418.0" y1="118.0" x2="482.0" y2="118.0" stroke="currentColor" stroke-width="1.5"/><polygon points="482.0,118.0 474.6,121.1 474.6,114.9" fill="currentColor"/> <text x="450" y="88" font-size="12" text-anchor="middle">N초 / N질의 경과</text> <line x1="482.0" y1="144.0" x2="418.0" y2="144.0" stroke="currentColor" stroke-width="1.5"/><polygon points="418.0,144.0 425.4,140.9 425.4,147.1" fill="currentColor"/> <text x="450" y="168" font-size="12" text-anchor="middle">probe 실패</text>
<path d="M560,164 C560,240 120,240 120,164" fill="none" stroke="currentColor" stroke-width="1.5"/> <polygon points="120,164 115.5,172 124.5,172" fill="currentColor"/> <text x="340" y="236" font-size="12" text-anchor="middle">probe 성공 (RTT EWMA가 기준 이하) → 다시 CLOSED</text>
</svg>
```

그림 7 — 클라우드 링크 circuit breaker. CLOSED에서 연속 실패가 쌓이면 OPEN으로 가서 클라우드 시도를 멈춘다. 일정 시간(또는 질의 수)이 지나면 HALF-OPEN에서 probe 하나만 보내 보고, 성공하면 CLOSED로 돌아간다.

펌웨어 구현 메모:

- 상태는 3개짜리 enum과 카운터 2개다. 재부팅·sleep 사이에 유지되도록 retained RAM에 둔다. 하루에 수십 번 sleep/wake하는 웨어러블에서 깨어날 때마다 breaker가 리셋되면 매번 timeout을 한 번씩 문다.
- probe 주기는 질의 수가 아니라 **시간**으로 하는 편이 낫다(질의가 드문 시간대에 링크가 회복돼도 모른다). 백그라운드 heartbeat(연결 유지 패킷)의 RTT를 EWMA로 추적하면 probe를 질의에 태우지 않아도 된다. 단 heartbeat 자체가 무선 에너지를 쓴다 (D7).
- 링크 종류가 바뀌면(Wi-Fi → 폰 BLE 중계) 통계를 리셋한다. 다른 링크의 RTT 분포를 섞으면 안 된다.

### 8.4 speculative 병렬 — 언제 값을 하나

```
라우팅 비율 f, 기기 시간 L_d, 무선 고정 에너지 E_r, 클라우드 단가 $_c

post-gen 대비 speculative의
  이득: 라우팅된 질의마다 L_d 단축       → 평균 f · L_d
  비용: 라우팅 안 된 질의도 클라우드 시작 → 평균 (1 − f) · (E_r + $_c 일부)
```

손계산 (이 노트의 수치): f = 0.41, L_d = 0.22 s, E_r = 135 mJ. 이득은 질의당 평균 0.41 × 0.22 ≈ 0.09 s. 비용은 질의당 평균 0.59 × 135 ≈ 80 mJ의 추가 무선 에너지와 클라우드 입력 토큰 비용. 기기 SLM 생성 에너지가 질의당 약 240 mJ였으므로(6.2절) 에너지가 3분의 1 늘어나는 셈이다. 평균 90 ms를 위해 쓰기에는 비싸다.

speculative가 값을 하는 경우는 **L_d가 클 때**다: 기기 SLM이 느리거나(긴 답, 큰 모델, throttling) 지연 예산이 빡빡한 질의(대화 중 끼어들기, L4의 barge-in). 그래서 전부가 아니라 **pre-router가 "애매하다"고 본 질의에만** speculative를 거는 것이 보통이다. 또 클라우드 쪽은 취소 시 과금이 어떻게 되는지(입력 토큰은 이미 과금, 출력은 취소 시점까지) 확인해야 한다.

### 8.5 부분 스트리밍 handoff

음성 비서에서 사용자가 느끼는 지연은 **첫 소리**까지의 시간(TTFT + TTS 첫 청크)이다 (L4). 그래서 "기기가 먼저 말을 시작하고 클라우드가 이어받는" 설계가 있다.

```
t=0     질의 끝 (VAD)
t=0.05  기기: "음, 확인해 볼게요" 또는 답의 앞부분을 TTS로 시작   ← 사용자는 이미 반응을 들음
t=0.05  클라우드 요청 시작 (질의 + 기기가 이미 말한 prefix를 함께 보냄)
t=0.60  클라우드 첫 토큰 도착 → 기기 TTS 큐에 이어 붙임
        클라우드가 timeout → 기기가 자기 답을 이어서 끝까지 말함
```

주의할 점:

- **prefix 일관성**: 기기가 "캐나다의 수도는 몬트리올…"까지 말했는데 클라우드가 "오타와"라고 하면 되돌릴 수 없다. 그래서 기기가 먼저 말하는 부분은 **내용이 없는 문장**(인정, 반복)으로 제한하거나, 기기 신뢰도가 높을 때만 내용을 말하게 한다. 첫 토큰 margin(3.3절, AUROC 0.68)처럼 생성 초반에 얻을 수 있는 신호가 여기서 쓸모 있다.
- **이어받기 문맥**: 클라우드에 "기기가 이미 이렇게 말했다"는 prefix를 넘겨야 문장이 자연스럽게 이어진다. 토크나이저가 달라도 텍스트로 넘기면 된다.
- **반대 방향**: 클라우드 스트림이 중간에 끊기면 기기가 받은 텍스트를 prefix로 이어 완성한다. 이때 기기 모델이 그 내용을 감당할 수 있는지(능력 밖이면 "연결이 끊겨서 나머지는 나중에 알려 드릴게요")를 판단해야 한다.

### 8.6 오프라인 모드의 사용자 경험

오프라인은 예외가 아니라 **정상 상태 중 하나**다 (지하철, 비행기, 산). 설계 원칙:

- 기기에서 할 수 있는 일(타이머, 알람, 볼륨, 전화, 메모)은 오프라인에서도 **똑같이** 동작해야 한다. 이 노트의 JSON 명령 범주가 그것이다 — 360M이 13~15/16을 맞힌다.
- 최신성이 필요한 질의(날씨, 뉴스)는 **지어내지 말고** 솔직하게 말한다: "지금은 인터넷에 연결되어 있지 않아요. 연결되면 알려 드릴까요?" 그리고 요청을 큐에 넣는다. 5.4절 C 코드의 `R_DEVICE_STALE`이 그 경로다.
- 기기 신뢰도가 낮은데 오프라인이면, 답을 하되 **불확실함을 말한다**("정확하지 않을 수 있어요"). calibration이 된 p가 있어야 이 문장을 언제 붙일지 정할 수 있다.

---

## 9. 일관성 — 기기와 클라우드가 다른 말을 할 때

### 9.1 얼마나 자주 다른가

```python
# 기기 답과 클라우드 답은 얼마나 자주 다른가 + 라우팅 로그로 distillation 데이터를 모으면 품질은?
from l3_data import *
from l3_common import agree
same = np.array([agree(d["cat"], d["ans"], c["ans"]) for d, c in zip(dev, cld)])
print(f"device/cloud canonical answers agree: {same.mean():.0%}")
for c in ["json", "arith", "know", "multi", "ko"]:
    m = cats == c; print(f"  {c:5s} agree {same[m].mean():4.0%}   both right {((y == 1) & (yc == 1))[m].sum():2d}"
                         f"  only device {((y == 1) & (yc == 0))[m].sum():2d}  only cloud {((y == 0) & (yc == 1))[m].sum():2d}")
rt = np.zeros(80, bool); rt[np.argsort(np.load("p_lr.npy"), kind="stable")[:33]] = True
log = rt & ~priv                                           # 개인 질의는 로그/학습에서 제외 (H6)
print(f"\nrouted & loggable: {log.sum()}  | cloud pseudo-label correct {yc[log].mean():.0%}"
      f" | device was wrong on {(y[log] == 0).mean():.0%} of them")
print(f"hard examples captured: {((y == 0) & (yc == 1) & log).sum()} of {((y == 0) & (yc == 1)).sum()} teachable"
      f"  | 'cloud wrong' noise in set: {((yc == 0) & log).sum()}")
print("noise examples:", [(int(i), cld[i]["ans"][-28:]) for i in np.flatnonzero(log & (yc == 0))][:3])
```

```text
device/cloud canonical answers agree: 49%
  json  agree  75%   both right 12  only device  1  only cloud  2
  arith agree 100%   both right 16  only device  0  only cloud  0
  know  agree  56%   both right 12  only device  3  only cloud  0
  multi agree  12%   both right  2  only device  0  only cloud 12
  ko    agree   0%   both right  0  only device  1  only cloud  8

routed & loggable: 29  | cloud pseudo-label correct 72% | device was wrong on 83% of them
hard examples captured: 17 of 22 teachable  | 'cloud wrong' noise in set: 8
noise examples: [(51, 'd. Answer: The answer is 17.'), (64, '대구입니다.'), (68, '14天')]
```

출력에서 볼 것:

- 정규화한 답이 같은 비율은 **49%뿐**이다. 산수는 100% 같고, 다단계 추론은 12%, 한국어는 0%.
- 상식은 56%인데 대부분은 **표현 차이**다("Paris" vs "The capital of France is Paris" — 단어 집합 Jaccard가 0.5 미만). 사용자에게는 같은 답이지만 로그 분석에서는 "불일치"로 잡힌다. 답을 비교하려면 **정규화(canonicalization) 규칙**이 먼저다.
- 같은 질문에 **링크 상태에 따라** 다른 답이 나온다는 뜻이기도 하다. 사용자가 지하철에서 물으면 기기 답, 집에서 물으면 클라우드 답. 둘이 다르면 사용자는 제품을 믿지 않는다.

### 9.2 일관성을 지키는 설계

| 문제 | 예 | 대책 |
|---|---|---|
| 같은 질문, 다른 답 | 오프라인에서는 "Montreal", 온라인에서는 "Ottawa" | 클라우드 답을 기기 캐시에 저장(자주 묻는 질문), 다음 distillation에 반영 |
| 말투 · 형식 차이 | 기기는 "12", 클라우드는 장황한 풀이 | 같은 시스템 프롬프트·출력 스키마, 클라우드 답을 TTS 전에 기기 형식으로 요약 |
| 도구 실행 위치 | 클라우드가 "알람 7:15" JSON을 냈는데 알람은 기기에 있어야 함 | **해석은 어디서든, 실행은 항상 기기**: 클라우드는 JSON만 돌려주고 기기가 검증 후 실행. 날씨처럼 외부 데이터가 필요한 도구는 클라우드에서 실행 |
| 개인화 · 메모리 | 기기 메모리에 "엄마 = 김OO"가 있는데 클라우드는 모름 | L5: 메모리는 기기가 주인. 필요한 조각만 동의 범위 안에서 질의에 첨부, 클라우드 쪽 저장 금지 또는 TTL |
| 대화 문맥 | 앞 턴은 클라우드, 이번 턴은 기기 — 기기가 앞 턴을 모름 | 대화 이력(텍스트)은 기기가 보관하고 어느 쪽이든 같은 이력을 붙여 보냄. 기기 context 한계(L2)에 맞춰 요약 |
| 버전 차이 | 기기 SLM v3, 라우터 v2, 클라우드 모델이 몰래 바뀜 | 로그마다 (SLM 버전, 라우터 버전, 클라우드 모델 ID) 기록. 라우터 calibration은 버전 조합마다 다시 |

### 9.3 라우팅 로그로 모델을 키우기 — hard example mining

라우팅 결정 자체가 공짜 라벨 공장이다. 기기가 자신 없어서 클라우드로 보낸 질의는 정확히 "기기가 못 하는 질의"이고, 클라우드의 답은 teacher 답이다. C5 9절의 기기 student / 클라우드 teacher 구조, H8의 fleet 수집과 연결된다.

```
기기: 질의 → SLM 답 + 신호 → 라우터 ──(낮은 p)──▶ 클라우드 답
                                         │
            로그 (동의 · private 제외, H6) ▼
   { 질의, 기기 답, 신호, 클라우드 답, 버전, 링크 상태 }
                                         │
   서버: 필터(검증기, 다수결, 큰 모델 재채점) → distillation 세트 (C5)
                                         │
   새 SLM + 새 라우터 calibration ──OTA (J5)──▶ 기기
```

ex13의 숫자가 이 루프의 현실을 보여 준다.

- 41%를 라우팅하고 private을 빼면 로그에 남는 질의는 29개. 그중 기기가 틀린 것이 83% — **hard example이 잘 모인다**. 학습으로 고칠 수 있는 질의(기기 오답 & 클라우드 정답) 22개 중 17개를 잡았다.
- 그러나 클라우드 답의 정답률은 72%뿐이고, **오답 8개가 섞인다** ("17", "대구입니다", "14天"). 클라우드 답을 그대로 정답으로 쓰면 student가 teacher의 실수를 배운다. 진짜 클라우드 LLM이면 이 비율이 훨씬 낮겠지만 0은 아니다. 그래서 필터가 필요하다: 숫자는 계산기로 재검증, JSON은 스키마 검증, 나머지는 더 큰 모델로 재채점하거나 여러 샘플의 다수결.
- **private 질의는 이 루프에 들어가지 않는다.** 7개 중 2개가 기기 오답이었지만 학습 데이터로 쓸 수 없다. 이런 질의는 사용자 동의를 받은 합성 데이터(같은 구조, 가짜 숫자)로 대신한다 (H6).
- 루프가 돌면 **라우팅 비율이 줄어든다**. 기기가 좋아지면 덜 보내고, 덜 보내면 로그가 줄고, 남는 로그는 더 어려운 질의뿐이다. 분포가 계속 바뀌므로 라우터 calibration을 OTA마다 다시 해야 한다.

---

## 10. 임베디드 관점에서 다시 보기

라우터의 각 부품이 기기에서 얼마나 드는지 정리한다.

| 부품 | 연산 | 메모리 | 지연 | 메모 |
|---|---|---|---|---|
| sum_lp, min_lp | 토큰마다 덧셈·비교 1회 | float 2개 | 0 | 샘플러가 어차피 softmax를 계산한다. log-prob을 버리지 말고 누적만 하면 된다 |
| entropy (top-k) | 토큰마다 vocab(49k)에서 top-k 선택 + k번 곱셈 | k개 | 작음 | top-k 선택은 샘플러에 이미 있다. 전체 entropy는 vocab 전체를 한 번 더 훑어야 해서 비싸다 |
| self-consistency | **SLM 생성 3회 추가** | KV cache 3배 (batch) 또는 순차 | +생성 1~3회 | 에너지 4배 (6.2절). staged로 애매한 질의에만 |
| 형식 검증 / grammar | 파서, 또는 grammar 제약 디코딩 | 작음 | 작음 | grammar를 쓰면 형식 신호가 사라진다 (2.5절) |
| LR 결정 | 곱셈 2 + expf 1 | float 3개 | ns 단위 | 5.4절 C 코드 |
| pre-router (MiniLM) | 22.7 M params 인코더 1회 | int8 약 23 MB | 수 ms | L5 임베딩 모델과 공유 가능 |
| circuit breaker | 상태 머신 | 수십 바이트, retained RAM | 0 | 8.3절 |

텔레메트리 레코드는 이렇게 고정 크기 구조체로 만들어 H1의 링버퍼 로그에 넣는다. 질의 원문은 넣지 않는다(H6) — 넣더라도 동의가 있는 경우만, 별도 경로로.

```c
typedef struct __attribute__((packed)) {
    uint32_t t_ms;            /* 질의 시각 (부팅 후) */
    uint16_t slm_ver, router_ver;
    int16_t  sum_lp_q8;       /* sum_lp × 256, Q8.8 고정소수점 */
    uint8_t  sc_x3;           /* 일치 샘플 수 0..3 */
    uint8_t  p_q8;            /* 라우터 확률 × 255 */
    uint8_t  route;           /* DEVICE / CLOUD / DEVICE_STALE / FALLBACK */
    uint8_t  net_state;       /* GOOD / POOR / OFFLINE, breaker 상태 비트 */
    uint16_t dev_ms, cloud_ms;/* 0이면 해당 경로 안 씀 */
    uint8_t  flags;           /* private, needs_fresh, timeout, grammar_used */
} route_log_t;                /* 19 bytes — 하루 200 질의면 3.8 KB */
```

`printf("%zu", sizeof(route_log_t))`로 확인하면 packed 구조체는 19 bytes다 (`cc -std=c11 -Wall -Wextra`, 경고 0). packed를 빼면 정렬 padding 때문에 커진다 — 레코드 형식은 서버 파서와 공유하므로 packed + 명시적 버전 필드로 고정한다.

이 레코드만 있으면 서버에서 "버전별 라우팅 비율", "timeout 비율", "링크 상태별 정확도 추정", "sum_lp 분포의 drift"(H7)를 모두 볼 수 있다. Don이 SSD에서 read-retry 단계별 횟수를 telemetry로 올려 펌웨어 튜닝에 쓴 것과 같은 구조다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 토큰 평균 확률을 그대로 "정답 확률"로 씀 | "90% 확신" 답이 70%만 맞음 | 과대확신 (ECE 0.21) | 라벨 데이터로 calibration (LR, temperature scaling) |
| 답 문자열 확률 P(답)을 확률로 씀 | 맞는 답도 확률 0.1 | 같은 정답의 표현이 많음 | 순위 신호로만 쓰거나 LR 입력으로 |
| 분포 밖 입력(다른 언어)에 log-prob 문턱만 씀 | 한국어 엉터리 답이 기기에서 나감 | 질문을 따라 쓰는 답이 고확률 | 언어 감지 pre-router, self-consistency |
| 같은 데이터로 τ를 고르고 precision을 보고 | 필드에서 precision 목표 미달 | 선택 편향 | nested CV, 별도 hold-out, 필드 telemetry로 재확인 |
| self-consistency를 모든 질의에 켬 | 배터리 소모 4배 | 샘플 3개 추가 생성 | staged: 애매한 구간에서만 |
| 프라이버시 규칙을 confidence 뒤에 둠 | 자신 없는 개인 질의가 클라우드로 | 결정 순서 | 협상 불가 규칙을 맨 앞에 (그림 1) |
| timeout 없이 클라우드 대기 | 링크 나쁠 때 10 s 무응답 | fallback 없음 | timeout + 기기 답 fallback |
| breaker 없이 timeout만 | 오프라인에서 매 질의 3 s 지연 | 실패를 기억 안 함 | circuit breaker, 링크 상태 EWMA |
| SLM만 OTA하고 라우터는 그대로 | 라우팅 비율·정확도가 갑자기 변함 | calibration이 옛 모델 기준 | SLM·pre-router·LR을 한 번들로 버전 관리 |
| 클라우드 답을 그대로 distillation 라벨로 | student가 teacher 실수를 학습 | teacher도 틀림 (이 실험 28%) | 검증기·다수결·재채점 필터 |

---

## 12. 면접에서 이렇게 말한다

**Q.** How do you decide whether to answer on device or in the cloud?

**A.** 결정은 순서가 있는 파이프라인이다. 먼저 협상 불가 조건 — 프라이버시 등급, 오프라인 여부, 최신 정보 필요 여부 — 를 규칙으로 거른다. 다음에 질의 텍스트만 보는 싼 pre-router로 기기 모델이 못 하는 유형(다른 언어, 다단계 추론)을 생성 전에 보낸다. 남은 질의는 SLM을 돌리고 calibration된 신뢰도 p를 문턱 τ와 비교한다. τ는 고정값이 아니라 링크 품질·배터리·비용 한도가 움직인다. 실측으로 80개 질의에서 이 방식은 클라우드 호출을 절반 이하로 줄이면서 클라우드 전용 정확도에 도달했다.

> I treat it as an ordered pipeline. Hard constraints first: privacy class, offline, and whether the query needs fresh internet data — those are rules, not scores. Then a cheap pre-generation router on the query text sends obviously out-of-scope queries, like other languages or multi-step reasoning, straight to the cloud. For the rest, the small model answers and I compare a calibrated confidence against a threshold that moves with link quality, battery and cost budget. On an 80-query benchmark I measured, that reached cloud-only accuracy while routing under half the traffic.

**Q.** What confidence signals can a small LLM give you, and are they calibrated?

**A.** 공짜 신호는 생성 중에 나오는 token log-prob이다: 합(sum), 최소, 평균, top-k entropy, 첫 토큰 margin. 비싼 신호는 self-consistency(여러 번 샘플링해서 일치하는지)와 검증기(JSON 스키마, 계산 재검증)다. 실측하면 순위 능력(AUROC)은 sum log-prob 0.83, self-consistency 0.81 정도였지만 calibration은 나쁘다. 토큰 평균 확률은 과대확신(ECE 0.21), 답 전체 확률은 과소확신(0.39)이다. 그리고 분포 밖 입력(한국어)에서는 엉터리 답도 고확률이다. 그래서 신호를 라벨 데이터로 logistic regression에 넣어 calibration하고(ECE 0.06), 모델이나 양자화가 바뀌면 다시 한다.

> The free signals come out of decoding: summed and minimum token log-prob, top-k entropy, first-token margin. The expensive ones are self-consistency across a few samples and external verifiers like a JSON schema. In my measurements they rank well — AUROC around 0.83 for summed log-prob and 0.81 for self-consistency — but they are not calibrated: mean token probability is overconfident, whole-sequence probability is badly underconfident, and on out-of-distribution input like Korean the model is confidently wrong. So I fit a tiny logistic regression on labeled data, which brought ECE to about 0.06, and I redo it whenever the model or its quantization changes.

**Q.** How do you evaluate a router?

**A.** 라우터는 "기기 답이 맞을까"를 예측하는 이진 분류기로 보고 세 단계로 본다. (1) AUROC로 순위 능력, bootstrap 신뢰 구간과 함께. (2) reliability diagram과 ECE로 확률이 믿을 만한지. (3) 시스템 수준: 클라우드로 보낸 비율 vs 전체 정확도 곡선을 random과 oracle 사이에 그리고, 같은 지점에서 지연 p95, 기기 에너지, 클라우드 비용, 프라이버시 위반 수를 표로 본다. 문턱은 nested cross-validation으로 고른다. 같은 데이터로 고르면 precision 0.95가 실제로는 0.75였다.

> I treat the router as a binary classifier for "will the on-device answer be correct" and evaluate it at three levels: AUROC with bootstrap intervals for ranking, a reliability diagram and ECE for whether its probabilities can be used directly, and system level — accuracy versus fraction routed, plotted between the random and oracle curves, with p95 latency, device energy, cloud cost and privacy violations at the operating point. I choose the threshold with nested cross-validation; picking it on the same data made a 0.95 precision target look achievable when it was really about 0.75.

**Q.** Cloud is down — what happens?

**A.** 사용자는 기기 기능을 그대로 쓴다. 구현은 세 가지다. timeout으로 지연 상한을 묶고 기기 답으로 fallback한다 — post-generation 라우팅이면 기기 답이 이미 손에 있다. circuit breaker가 연속 실패를 기억해서 이후 질의는 클라우드 시도 없이 바로 기기로 간다. 실측 기반 시뮬레이션에서 outage 때 p95가 3.5 s에서 0.8 s로 줄었다. 최신 정보가 필요한 질의는 지어내지 않고 연결되면 알려 주겠다고 말한다. breaker 문턱은 링크 통계에 맞춰야 한다 — 15% 유실 링크에서는 너무 자주 열려 정확도가 5%p 떨어졌다.

> The user keeps every on-device capability. A timeout bounds latency and falls back to the local answer — with post-generation routing that answer already exists. A circuit breaker remembers consecutive failures so later queries skip the cloud entirely; in a simulation driven by my measured device latencies, that cut outage p95 from 3.5 seconds to 0.8. Queries that need fresh data get an honest "I can't check right now" instead of a hallucination. The breaker thresholds need tuning against real link statistics — on a lossy link it opened too often and cost about five points of accuracy.

**Q.** How do privacy constraints change routing?

**A.** 프라이버시는 점수가 아니라 규칙이고, 결정 순서의 맨 앞에 둔다. confidence 뒤에 두면 "자신 없는 개인 질의"가 정확히 클라우드로 간다. 그 비용은 측정할 수 있다: private이면서 기기가 틀리는 질의 비율 × 클라우드 정답률이 상한이다. 내 실험에서는 0이었는데, 개인 질의는 대부분 기기 명령이고 필요한 데이터가 원래 기기에 있기 때문이다. 정말 클라우드 추론이 필요하면 식별 정보를 지우고 계산 부분만 보내고, 그 판단 자체도 기기에서 한다. 그리고 private 질의는 라우팅 로그와 distillation 데이터에서도 빠진다.

> Privacy is a rule, not a score, and it goes first in the decision order — otherwise the uncertain personal queries are exactly the ones you send out. Its cost is measurable: the fraction of private queries the device gets wrong, times cloud accuracy, is the upper bound. In my run it was zero, because private queries were mostly device commands whose data lives on the device anyway. When cloud reasoning is really needed, I redact identifiers on device and send only the computation. Private queries are also excluded from routing logs and from the distillation set.

**Q.** Pre-generation or post-generation routing?

**A.** 둘 다 쓴다. pre-router는 질의 임베딩(MiniLM, 수 ms)으로 유형을 보고 결정해서, 클라우드로 갈 질의의 SLM 생성 시간을 아낀다. 실측에서 pre-router의 AUROC가 post-generation 신호와 비슷했는데, 이 데이터에서 정답 여부가 범주로 많이 결정되었기 때문이다. 그러나 같은 범주 안의 개별 오답은 생성 후 신호만 잡는다. 그래서 2-stage — 확실히 못 하는 질의는 앞에서 보내고, 나머지는 SLM 답을 보고 판단 — 로 같은 정확도에서 낭비 시간을 309 ms에서 133 ms로 줄였다.

> Both, in two stages. A query-embedding pre-router costs a few milliseconds and skips local generation for queries the small model clearly can't handle. In my data it matched post-generation signals on AUROC, because correctness was largely determined by query type. But only post-generation signals catch an individual wrong answer inside a category the model usually handles. Combining them kept the same accuracy and cut wasted on-device time per routed query from about 309 to 133 milliseconds.

---

## 13. 직접 해보기

1. 손계산: 토큰 log-prob이 (−0.10, −0.05, −2.30, −0.02)인 답의 sum_lp, mean_lp, min_lp, P(답 전체), 토큰 기하평균 확률을 구하라. 정답: sum −2.47, mean −0.6175, min −2.30, P ≈ 0.085, 기하평균 ≈ 0.54.
2. 손계산: 정답 질의 점수 {0.8, 0.5, 0.4}, 오답 질의 점수 {0.6, 0.3}의 AUROC를 쌍 세기로 구하라. 정답: 쌍 6개 중 (0.8>0.6, 0.8>0.3, 0.5>0.3, 0.4>0.3) 4개 → 0.667.
3. 손계산: 1.3절의 식에서 p_c = 0.9, V = 1, 오프라인 직전이라 ΔC = 0.5일 때 τ는? 기기 p = 0.45면 어디로? 정답: τ = 0.4, 0.45 ≥ 0.4이므로 기기.
4. 코드: ex6에 temperature scaling을 추가하라. 신뢰도 c = exp(mean_lp)를 logit z = ln(c / (1 − c))로 바꾸고, σ(z / T)의 T를 학습 fold에서 log loss가 최소가 되게 고른 뒤 test fold의 ECE가 0.213에서 얼마나 줄어드는지 재 보라. 힌트: `scipy.optimize.minimize_scalar`, T > 1이면 과대확신을 누그러뜨린다.
5. 코드: ex12의 circuit breaker에서 "연속 실패 수"를 2 → 3, probe 주기를 16 → 8로 바꿔 degraded와 outage 두 네트워크에서 정확도와 p95가 어떻게 움직이는지 표로 만들어라. 힌트: 두 네트워크 사이의 trade-off 곡선이 나온다. 정답은 하나가 아니다.
6. 실험: 기기를 smollm2-360m **f16**(같은 모델, 다른 정밀도)로 바꿔 l3_run.py를 다시 돌리고, ex7의 LR 계수를 Q8_0에서 학습한 것 그대로 쓸 때 ECE가 변하는지 확인하라. 힌트: 4.3절 — 모델(양자화 포함)이 바뀌면 calibration을 다시 하는가?

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| router | 라우팅 결정기 | "기기 답이 맞을까"를 예측하는 이진 분류기로 평가 |
| sum_lp / mean_lp | log-prob 합 / 평균 | 합 = log P(답 전체), 평균 = −(길이 정규화 NLL) |
| entropy (top-k) | 다음 토큰 분포의 불확실성 | 상위 k개만으로 계산하면 하한 |
| self-consistency | 샘플 간 답 일치도 | 분포 밖 입력에 강하지만 생성 비용이 배로 든다 |
| verifier | 답 검증기 | JSON 스키마, 계산 재검증 — 체계적 오류도 잡는 유일한 신호 |
| grammar-constrained decoding | 문법 제약 디코딩 | 형식은 보장, 내용은 보장 안 함 |
| AUROC | ROC 곡선 아래 면적 | 정답·오답 한 쌍의 순서를 맞힐 확률 |
| calibration | 신뢰도 보정 | "0.8이라고 하면 80% 맞는다"가 성립하게 만드는 일 |
| reliability diagram | 신뢰도 bin별 정답률 그림 | 대각선 아래 = 과대확신 |
| ECE | expected calibration error | bin별 (신뢰도 − 정답률) 절대값의 샘플 수 가중 평균 |
| precision / coverage | 기기 답의 정답률 / 기기 처리 비율 | 문턱 τ로 서로 맞바꾼다 |
| nested CV | 이중 교차검증 | 문턱 선택까지 학습 fold 안에서 해서 낙관 편향 제거 |
| pre-generation router | 생성 전 라우터 | 질의 텍스트(임베딩)만 보고 결정 |
| post-generation router | 생성 후 라우터 | SLM 답의 신호를 보고 결정 |
| speculative parallel | 기기·클라우드 동시 시작 | 지연 ↓, 클라우드 비용 ↑ |
| circuit breaker | 실패 기억 상태 머신 | CLOSED / OPEN / HALF-OPEN |
| hard example mining | 어려운 샘플 수집 | 라우팅 로그 = 기기가 못 하는 질의 목록 |

---

## 15. 요약 & 체크리스트

Hybrid 라우팅은 "기기 답이 맞을까"를 예측하는 분류기와, 그 앞에 놓인 협상 불가 규칙(프라이버시, 오프라인, 최신성)과, 문턱을 움직이는 손잡이(링크, 배터리, 비용)로 이루어진다. 작은 LLM은 생성하면서 공짜 신호(token log-prob 합·최소·entropy)를 내고, 돈을 더 내면 self-consistency와 검증기 신호를 얻는다. 80개 실제 질의로 재 보니 이 신호들은 순위는 꽤 잘 매기지만(AUROC 0.8대) 확률로서는 틀려 있었고(과대·과소확신), 분포 밖 입력과 체계적 오류에는 눈이 멀었다. tiny logistic regression으로 묶으면 calibration이 좋아지고(ECE 0.06), 측정된 신호로 라우팅하면 클라우드 호출을 절반으로 줄이고도 클라우드 전용 정확도에 도달한다. 생성 전 pre-router는 유형을 싸게 거르고, 생성 후 신호는 개별 답을 본다. 실패 처리는 timeout·fallback·circuit breaker가 기본이고, speculative 병렬은 지연을 위해 비용을 산다. 마지막으로 라우팅 로그는 기기 모델을 키우는 hard example 공장이지만, teacher의 오답과 프라이버시를 걸러야 한다.

- [ ] 라우팅의 일곱 축을 규칙 / 점수 / 손잡이로 나누고 결정 순서를 그릴 수 있다
- [ ] llama-server `n_probs`로 토큰 log-prob을 받아 sum·mean·min·top-k entropy를 손으로 계산할 수 있다
- [ ] AUROC를 쌍 세기로 손계산하고, 80개 표본에서 bootstrap 신뢰 구간이 얼마나 넓은지 말할 수 있다
- [ ] reliability diagram과 ECE를 계산하고 과대확신·과소확신을 구별할 수 있다
- [ ] 신호를 logistic regression으로 묶고, nested CV로 목표 precision의 문턱을 정직하게 고를 수 있다
- [ ] 정확도 vs 클라우드 비율 곡선을 random·oracle과 함께 그리고 해석할 수 있다
- [ ] self-consistency의 에너지 비용을 계산하고 staged 신호 설계를 제안할 수 있다
- [ ] pre-generation과 post-generation 라우팅의 지연·정확도 trade-off를 설명하고 2-stage를 설계할 수 있다
- [ ] timeout fallback, speculative 병렬, circuit breaker의 장단점을 네트워크 상태별로 말할 수 있다
- [ ] 기기·클라우드 답의 불일치, 도구 실행 위치, 라우팅 로그 기반 distillation의 함정을 설명할 수 있다

---

## 참고 자료

- llama.cpp server 문서 (`/completion`의 `n_probs`, `json_schema`, `cache_prompt`): https://github.com/ggml-org/llama.cpp/tree/master/tools/server
- Guo, Pleiss, Sun, Weinberger, "On Calibration of Modern Neural Networks", ICML 2017 — https://arxiv.org/abs/1706.04599
- Kadavath et al., "Language Models (Mostly) Know What They Know", 2022 — https://arxiv.org/abs/2207.05221
- Wang et al., "Self-Consistency Improves Chain of Thought Reasoning in Language Models", 2022 — https://arxiv.org/abs/2203.11171
- Kuhn, Gal, Farquhar, "Semantic Uncertainty: Linguistic Invariances for Uncertainty Estimation in Natural Language Generation", ICLR 2023 — https://arxiv.org/abs/2302.09664
- Manakul, Liusie, Gales, "SelfCheckGPT", 2023 — https://arxiv.org/abs/2303.08896
- Chen, Zaharia, Zou, "FrugalGPT: How to Use Large Language Models While Reducing Cost and Improving Performance", 2023 — https://arxiv.org/abs/2305.05176
- Ding et al., "Hybrid LLM: Cost-Efficient and Quality-Aware Query Routing", ICLR 2024 — https://arxiv.org/abs/2404.14618
- Ong et al., "RouteLLM: Learning to Route LLMs with Preference Data", 2024 — https://arxiv.org/abs/2406.18665
- Martin Fowler, "CircuitBreaker" — https://martinfowler.com/bliki/CircuitBreaker.html
- scikit-learn 문서: Probability calibration — https://scikit-learn.org/stable/modules/calibration.html
- 모델 카드: HuggingFaceTB/SmolLM2-360M-Instruct, Qwen/Qwen2.5-0.5B-Instruct, sentence-transformers/all-MiniLM-L6-v2 (huggingface.co)
- 이 노트와 연결된 노트: B9 5절, D7 6.3절, I3 6절, H6, C5 9절, F3, L2, L4, L5, L6
