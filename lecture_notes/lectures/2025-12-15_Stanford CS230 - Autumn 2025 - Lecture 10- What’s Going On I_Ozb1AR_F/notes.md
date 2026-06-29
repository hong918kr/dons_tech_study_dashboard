# Stanford CS230 | Autumn 2025 | Lecture 10: What’s Going On Inside My Model?

> Stanford · 2025-12-15 · 01:46:53 · [영상 링크](https://www.youtube.com/watch?v=Ozb1AR_F5MU) · 강사: Kian Katanforoosh

## 📌 한 줄 요약

이 강의는 "내 모델 안에서 무슨 일이 일어나고 있는가?"라는 질문을 두 영역으로 나눠 다룬다. 전반부는 합성곱 신경망(CNN)을 깊게 파헤쳐, 입력-출력 관계부터 개별 뉴런·특징 맵(feature map)까지 해석하는 고전적 해석가능성(interpretability) 기법들(saliency map, occlusion, CAM, class model visualization, dataset search, deconvolution)을 다룬다. 후반부는 프런티어 모델(frontier model)로 시야를 넓혀 attention/embedding 시각화, 학습·스케일링 진단(scaling laws), 능력·안전 벤치마킹, 데이터 진단(data diagnostics)을 연구 영역으로 소개한다. 핵심은 CNN에서 배운 해석 기술이 아직 미해결인 프런티어 모델 해석 연구의 기반 직관을 길러준다는 점이다.

## 🎯 핵심 메시지

> CNN은 시각적이라 입력-출력 관계, 특정 뉴런, 특징 맵까지 거의 완전히 해석할 수 있다. 이 해석 기법들 자체는 오늘날 프런티어 랩이 쓰는 방법은 아니지만, 거대 모델 내부를 들여다보는 연구자의 직관과 기술을 길러주는 발판이다.

**보조 메시지**
- 강의 제목이 과거 "신경망 해석가능성(neural network interpretability)"이었으나, 프런티어 모델 영역까지 포함하도록 범위를 넓혔다. 대부분의 프런티어 모델 해석법은 **아직 확립되지 않은 연구 영역**이다.
- "모델 트레이너"가 문제를 진단할 때 보는 증거는 크게 4가지 버킷으로 나뉜다: ① 학습·스케일링(training & scaling), ② 표현·내부(representation & internals), ③ 데이터·분포(data & distribution), ④ 다층위 능력 분석(capability levels).
- CNN의 saliency map ↔ Transformer의 attention pattern처럼, 두 패러다임의 해석 기법은 **유사한 대응 관계**를 갖는다. CNN은 지역적 정보(edge/texture/shape), LLM은 토큰 간 관계와 의미를 시각화한다.
- 프런티어 랩의 진단 대시보드는 대부분 IP(지적재산)로 비공개이며, 보통 3~4년 뒤에야 공개된다.

---

## 🧩 도입 케이스 스터디: "What is going on?" (프런티어 모델 진단)

당신은 200B 파라미터 모델을 학습시키는 **모델 트레이너**다. 밤사이 새 체크포인트가 sanity check는 통과했지만:
- reasoning 벤치마크에서 성능 하락
- 일부 safety eval 실패
- 에이전트 워크플로의 tool use에서 latency 급증

VP가 묻는다. "무슨 일이야?" 코드를 만지거나 재학습하기 **전에** 어떤 증거를 봐야 하는가?

브레인스토밍에서 나온 진단 아이디어를 강사는 **4가지 버킷**으로 정리한다.

| 버킷 | 살펴볼 항목 |
|---|---|
| **① 학습·스케일링** | loss curve(train/val), gradient(폭발/소실), learning rate schedule, MoE 라우팅, scaling laws |
| **② 표현·내부** | attention head/map, embedding(토큰이 의미상 가까운지), 뉴런 단위 동작(거대 모델에선 매우 어려움) |
| **③ 데이터·분포** | 벤치마크 오염(contamination), 마지막 batch 데이터 품질(poisoned/biased), train↔test 분포 차이 |
| **④ 다층위 능력 분석** | 언어 모델 자체 벤치마크 vs. 에이전트 워크플로 벤치마크 — 두 층위를 모두 검사 |

> 핵심 통찰: 대부분의 첫 반응은 **전역적(global)** 진단이다. 모델 **내부를 정밀하게** 들여다보려면 attention map, embedding, 체크포인트 추적 같은 표현 수준 분석이 필요하다.

세부 직관:
- **training/validation loss**: 수렴(convergence)이 매끄러운지, spike가 없는지. val loss는 train loss를 따라가되 보통 약간 높음. spike는 데이터/하드웨어 문제 신호.
- **체크포인트 추적**: 초기엔 잘 되다가 특정 시점에 saturate(포화)되거나 gradient 폭발/소실이 발생한 지점을 pinpoint.
- **MoE 실패**: 일부 expert가 죽거나, router가 항상 같은 expert만 선택하면 200B 모델이 실질적으로 더 작은 모델처럼 동작.

---

## 🐾 CNN 해석 1: 입력-출력 관계

동물원을 위한 **동물 분류기**(CNN + softmax)를 만들었는데, 동물원 측이 모델의 의사결정 과정을 이해하지 못해 사용을 꺼린다. 어떻게 신뢰를 줄까?

### Saliency Maps (현저성 맵)

특정 클래스(예: dog)의 **pre-softmax score**를 입력 이미지 X로 미분한다.

```
saliency = ∂(score_dog) / ∂X      # X = 입력 이미지 픽셀
```

- 의미: "픽셀을 바꾸면 dog 점수가 얼마나 변하는가?" → 밝은(gradient 큰) 픽셀 = 모델이 보고 있는 곳.
- **왜 pre-softmax인가?** post-softmax 점수는 다른 모든 클래스에 의존한다(분모의 지수합). 배경의 panda 점수가 끼어들어 dog 분석을 오염시킬 수 있다. pre-softmax는 해당 클래스만 반영.
- 구현 매우 간단(Python 몇 줄). 과거엔 빠른 segmentation sanity check로도 사용.
- **한계**: 픽셀 단위라 의미론적(semantic) 해석엔 부적합. 모델은 픽셀 하나만 다른 입력을 본 적이 없다.

### Integrated Gradients (적분 그래디언트)

- saliency map의 확장. 완전 검은 이미지(zeros)에서 실제 동물 이미지까지 **여러 단계의 보간 이미지**를 만들고, 그 경로를 따라 gradient를 적분.
- saliency보다 훨씬 해석력이 좋고 더 흔히 사용됨.
- 예시: 망막(retina) 이미지에서 적분 그래디언트가 실제 병변(lesion) 주석 위치와 정확히 일치 → 모델이 올바른 곳을 본다는 증거.

### Occlusion Sensitivity (가림 민감도)

- 원본 이미지로 dog 점수를 얻은 뒤, **어두운 사각형(mask)**을 이미지 위로 옮기며 점수 변화를 추적.
- 사각형이 진짜 객체를 가릴 때 점수가 떨어지면 모델이 그곳을 보고 있다는 증거. → "true class 확률 맵" 생성.
- 매우 직관적이지만 **계산 비용 큼**(이미지를 모델에 여러 번 통과).

| 예시 | True label | 관찰 |
|---|---|---|
| 강아지 | Pomeranian | 사각형이 **얼굴 중앙**을 가릴 때 점수 하락 (품종 식별엔 얼굴 필요) |
| 자동차 부품 | car wheel | 사각형이 **바퀴**를 가릴 때 점수 하락 |
| Afghan hound | Afghan hound | 개를 가리면 점수↓, 그런데 **왼쪽 사람 얼굴을 가리면 점수↑** (불필요 정보 제거) — 모델이 올바르게 본다는 신호 |

---

## 🗺️ CNN 해석 2: 실시간 결정 시각화 — Class Activation Map (CAM)

동물원이 분류 출력과 함께 **실시간 의사결정 시각화**를 원하고, 하루 안에 보여줘야 한다. 사후(post-hoc) 분석이 아니라 네트워크에 **꽂아 넣을 수 있는 모듈**이 필요하다.

**문제 지점**: 마지막의 3개 **fully connected(완전연결) 층**. 모든 픽셀을 동시에 섞어버려 지역적(localized) 정보가 사라진다.

**해결책: Global Average Pooling (GAP, 전역 평균 풀링)**
- 3개 FC 층 → **GAP 1개 + FC 1개**로 변환 (마지막 FC는 분류용 softmax 직전에 필요, 그 층만 재학습).
- 마지막 conv 블록의 볼륨(예: 4×4×6 = 6개 feature map)에서, 각 feature map을 **단일 숫자로 평균** → 크기 6 벡터.
- 핵심: 평균값을 할당해도 **이전 볼륨의 지역 정보는 보존**된다. 섞이지 않는다.

```
CAM_class = Σ_k  w_k · feature_map_k
# w_k: 마지막 FC 층에서 해당 feature map → class score 로 가는 가중치
```

- 마지막 6개 feature map을, 각 클래스에 대한 FC 가중치로 **가중합**하여 겹쳐 올림 → 클래스별 활성 지도.
- 클래스가 다르면(dog vs. cat) 가중치가 달라 각 feature map의 기여도가 달라진다.
- 출처: Berkeley 연구진. 개선판으로 **Grad-CAM**이 있음.
- 주의: 구세대 모델이라 종종 무의미한 곳을 보기도 하지만, 핵심 객체(cat/dog)에 대해선 빠르게 추적함을 검증 가능.

---

## 🔍 CNN 해석 3: 모델이 "개"를 어떻게 이해하는가 — Class Model Visualization

동물원이 묻는다. "모델이 진짜로 개가 뭔지 이해하는가, 아니면 패턴 매칭일 뿐인가?" → 모델에게 "네가 생각하는 개의 모습을 보여줘"라고 물어본다.

### Gradient Ascent로 입력 이미지 생성

```
maximize:  score_class(pre-softmax)  -  λ · R(X)     # X에 대해 경사 상승
```

- **pre-softmax score**를 최대화 (post-softmax를 쓰면 다른 클래스를 *낮춰서* 점수를 올릴 수 있어 의도와 다름).
- **정규화 항 R(X)**: 픽셀이 0~255 범위에 머물러 사람 눈에 자연스럽게 보이도록.
- 무작위 이미지에서 시작 → forward → objective 계산 → backprop을 픽셀까지 → 픽셀 업데이트, 반복.

**관찰 (Jason Yosinski 등의 연구)**
- **Dalmatian** → 흰 배경에 검은 점들. 모델은 개 자체보다 "흰 바탕의 검은 점"으로 이해.
- **Goose / Flamingo** → 모델에겐 "여러 마리"로 보임. 학습 데이터에서 거위·플라밍고가 항상 무리로 등장해, 한 마리 라벨을 "무리"로 학습.
- 정규화 기법을 개선하면 색이 풍부해지고 사람 눈에 더 잘 보여 다양한 클래스를 질의하기 쉬워짐.

> 이 gradient ascent는 클래스 수준뿐 아니라 **네트워크 내부 임의의 활성(activation)/뉴런**에도 적용 가능 — "이 활성을 최대로 만드는 (가상의) 입력은 무엇인가?"

---

## 📂 CNN 해석 4: Dataset Search (오늘날 가장 흔히 쓰임)

가장 간단하고 직관적이라 **현재도 가장 많이 사용**되는 방법.

- 특정 filter의 feature map을 하나 고른다(예: 어떤 conv 층의 256개 filter 중 하나).
- 전체 validation set에서 **그 feature map을 가장 강하게 활성화시킨 top-5(또는 top-9) 이미지**를 찾는다.
- top 이미지들이 모두 셔츠면 → "이 filter는 셔츠 검출기", 모두 edge면 → "edge 검출기".

### 수용 영역(receptive field)과 잘린(cropped) 이미지
- top 이미지들이 잘려 있는 이유: 깊은 층의 단일 활성은 **입력 이미지 전체가 아니라 일부**만 본다.
- **깊이가 깊어질수록** 하나의 활성이 입력 이미지의 **더 넓은 영역**을 봄 (마지막 출력은 전체 이미지 접근).
- 예: 64×64×3 입력 → 5층 conv → 256 filter → 13×13 feature map. 그 활성을 입력 공간으로 역추적하면 일부 영역에만 대응. 한 블록 더 깊어지면 더 넓은(추상화된) 영역을 봄.
- 그래서 활성이 본 영역만 잘라(crop) 보여준다 — 계산상 단순.

---

## 🔄 CNN 해석 5: Deconvolution (역공학)으로 뉴런 추적

특정 활성이 **왜** 높았는지 역추적하기 위해 **deconvolution(=transposed convolution) 모듈**을 추가한다. 이 아이디어는 CNN뿐 아니라 미래의 어떤 네트워크에서든 특정 뉴런의 활성 원인을 역공학할 때 핵심.

### 1D 합성곱을 행렬곱으로 재작성

- 입력 X(패딩 포함 12), filter 크기 4, stride 2 → 출력 Y 크기 5. (공식: floor((n+2p-f)/s)+1)
- conv 1D는 **연립방정식**으로 표현 가능 → 즉 **하나의 가중치 행렬 W (5×12)** 와 입력의 곱: `Y = W·X`. (대각선에 값, 나머지 0인 행렬)

### 역공학 가정
- W가 **가역(invertible)**이라 가정 → `X = H·Y`, H = W⁻¹. (딥러닝은 공학이므로 항상 참은 아니어도 충분히 동작)
- 더 나아가 W가 **직교(orthogonal)**라 가정하면 W⁻¹ = Wᵀ. (예: edge detector filter는 실제로 이런 성질을 가짐)
- → 그래서 deconvolution을 **transposed convolution**이라 부른다. 2D는 더 복잡하지만 같은 아이디어.

### 구현 트릭 (subpixel convolution)
stride-2 transposed conv ≡ stride-1/2 subpixel convolution. 즉 deconv를 **또 다른 conv**로 구현:
1. filter를 **뒤집는다(flip)**.
2. 입력에 값 사이마다 **0을 삽입**(subpixel 버전) + 패딩.
3. **stride를 2로 나눈다**.
4. 일반 conv 코드를 재사용 → 역공학 완료.

### Unpooling과 ReLU 처리
- **Max pooling은 비가역**: 최댓값이 2×2 중 어디 있었는지 모름. → **switches**(이진 위치 행렬)를 forward 때 저장했다가 unpooling 때 복원.
- **ReLU**: 엄밀한 backward는 불가능(switches 필요). 실무적으로 재구성 시 그냥 ReLU를 다시 적용해 양의 신호를 입력 공간으로 전달. (이론보단 경험적)

### 전체 절차
강아지 이미지 → conv net → 한 feature map 선택 → **최대 활성 위치**만 남기고 나머지 0 → unpool/ deconv를 블록 수만큼(예: 3회) 역적용 → 그 활성이 최대로 반응한 **잘린 입력 픽셀**을 복원.

### Zeiler & Fergus 시각화
- 50,000장 validation set으로 학습된 네트워크 분석.
- **Layer 1**: filter당 top-9 강한 활성 패치 = dataset search. raw filter를 직접 출력하면 (1층이라) edge detector 모양이 보임. 2층 이상은 raw filter가 해석 불가.
- **Layer 2~3**: deconv로 top-1/top-9 활성을 역추적 → 원/특정 형태/색을 검출하는 filter 확인. **깊어질수록 더 복잡한 특징**(첫 강의의 "깊이가 깊을수록 정보가 누적된다"의 증명).

### Jason Yosinski의 Deep Visualization Toolbox
앞서 배운 기법들을 종합한 인터랙티브 툴킷:
- 좌상단: 입력(웹캠/이미지), 중앙: 한 층의 활성.
- Layer 1: 명→암 edge에 반응하는 뉴런, 그 옆은 반대 방향.
- Layer 5(추상 개념): **얼굴에 반응하는 뉴런** — class model visualization(합성 이미지), dataset search(학습셋 이미지), deconv(픽셀 책임도)를 함께 표시. 더 어두운 눈/붉은 입술에 더 강하게 반응하고, 고양이 얼굴에도 일부 반응. deconv로 이전 층(conv4의 몇 유닛, conv3의 십여 유닛) 의존성 추적.

| 질문 | 사용 가능한 기법 |
|---|---|
| 입력의 어느 부분이 출력에 책임? | occlusion sensitivity, CAM |
| 뉴런/filter/층의 역할은? | dataset search, deconv, class model visualization |
| 주어진 입력에서 무엇에 집중? | saliency, occlusion, CAM |
| 모델은 세상을 어떻게 보는가? | gradient ascent (class model visualization) |

---

## 🤖 프런티어 모델: CNN과의 비교 & 표현 분석

| 구분 | CNN | 현대 LLM (Transformer) |
|---|---|---|
| 다루는 정보 | 지역적(localized) | 관계와 의미(relationships & meanings) |
| 시각화 대상 | edge, texture, shape | 토큰/개념 간 관계 |
| 기반 메커니즘 | convolution | **attention** ("Attention Is All You Need") |

Transformer가 언어를 표현하는 **2가지 시각화 가능한 아이디어**:

1. **Attention Pattern**
   - 토큰(단어/서브워드/음절) 간 관계를 본다. 각 attention head가 다른 패턴 학습: 대명사→명사 연결, 구조 추적, 순서 강제 등.
   - Jesse Vig (2019)의 시각화: 고정된 토큰과 주변 토큰의 연결. → **CNN의 saliency map에 대응하는 Transformer 버전**.

2. **Embedding**
   - pre-training 단계에서 학습. 의미가 비슷한 토큰은 임베딩 공간에서 가깝고, 무관한 토큰은 멀어야 함 → 의미 있는 표현 학습의 sanity check.
   - **t-SNE**(차원 축소) 같은 도구로 시각화 (Garden 2021 블로그). 바이오텍/헬스케어에서 널리 사용.

> 한계: 현대 transformer는 너무 복잡해, 최첨단 연구조차 **2-layer transformer** 수준에서만 관계를 해석한다. 최고 수준은 **Anthropic의 연구**:
> - "A Mathematical Framework for Transformer Circuits" — 컴포넌트 상호작용, **circuit** 개념 도입.
> - "In-context Learning and Induction Heads" — **induction head**가 transformer 내부를 들여다보는 현재 최선의 도구. (강의에선 시간상 링크만)

---

## 📈 학습·스케일링 진단 (Training & Scaling Diagnostics)

### Loss Curves
- train/val loss가 매끄러운 궤적인지 확인. 갑작스러운 jump = batch 손상, 또는 비정상적으로 잘 맞히면(코드 버그) 경고 신호.
- gradient 폭발/소실도 loss 수준에서 시각화 가능.
- 전역(global) loss뿐 아니라 데이터 **부분집합별** loss도 추적.

### Training Telemetry
- gradient norm, learning rate schedule, 하드웨어 효율 지표(compute 미활용 여부)를 대시보드로 추적.
- 대부분 **IP라 비공개**.

### Scaling Laws (스케일링 법칙)
모델 성능 ↔ 모델 용량(크기)·compute·데이터셋 크기의 관계.

- **Chinchilla (DeepMind, 2022)**: GPT-3와 비교. GPT-3는 크기에 비해 성능이 부족 = **충분히 오래 학습되지 않음**(모델이 과소 활용됨). 더 오래 학습했다면 훨씬 좋았을 것.
  - 점선 = 2020~21년에 생각한 power law, 실선 = Chinchilla의 재분석. 별이 선 **위**에 있으면 "더 오래 학습해야 함".
  - Chinchilla(70B) < GPT-3(175B) 파라미터지만 **더 좋은 성능**.
- power law 확립 방법: test loss(세로축) vs. compute/데이터셋 크기/파라미터(가로축). 셋 중 둘을 고정하고 셋째를 변화시켜 법칙이 성립하는지 확인.
- **재무적 중요성**: GPT-5 학습 비용은 수억 달러로 추정. "2배 더 오래 학습할까?"는 큰 의사결정 → compute/데이터/모델 크기 중 어디에 투자할지 scaling law가 안내.

---

## 🏅 능력 & 안전 벤치마킹

### Capability Benchmark
- reasoning, coding, math, multilingual 등 과제별 평가. 체크포인트 간 비교로 개선 추이 파악. **error cluster**로 약점 영역 식별(예: "checkpoint 5는 reasoning이 약함").
- 예: 2025 AIME(수학 경시) — OpenAI GPT-5, (강의 당일 발표된) Mistral 3세대 모델 비교.

### 벤치마크 오염 (Contamination)
- test set이 학습 데이터에 섞이면 발생. 블로그·GitHub의 음지 등에서 유입될 수 있음.
- **탐지 방법**:
  - **n-gram 검색**: 크기 7~8 토큰 시퀀스가 train/test 양쪽에 나타나는지.
  - **hash / embedding 검색**: 글자 그대로가 아니라 **의미상(semantically)** 유사한 test 예제가 train에 있는지.
- **대응**: 오염된 (작은) test set 예제를 제거하고, 오프라인 별도 폴더에 보관된 **완전히 새로운 예제**로 교체.
- 강사 의견: 파운데이션 모델 제공자의 벤치마크는 **절대값보다 모델 간 상대값**으로 보고, 커뮤니티의 실제 에이전트 워크플로 검증을 기다린다. (예: Llama 4는 벤치마크는 좋았으나 실사용은 별로; Claude의 코딩 실력은 시간이 지나 커뮤니티가 체감.)

### Safety Evaluation
- adversarial attack, jailbreaking, social engineering, 오용, 유해 콘텐츠 생성, hallucination, privacy leakage 등 stress test. 에이전트 워크플로 내 동작도 평가.
- 예: **OpenAI ↔ Anthropic 공동 안전 평가** — 비밀번호/문구 보호 등. 모델에서 password를 탈취하려는 prompt로 누출 여부 테스트.
- 이 대시보드가 **출시 go/no-go** 결정과 **RLHF 집중 영역**을 결정. 실패하는 eval에 SFT/RLHF를 집중해 비용·시간 절약.

---

## 🗃️ 데이터 진단 (Data Diagnostics)

### 분포 점검 (Distribution Checks)
- **The Pile (2020)**: 800GB 다양한 텍스트 데이터셋 (Free Law, Wikipedia, Stack Exchange, GitHub 등 도메인 보존).
- 전역 loss뿐 아니라 **도메인별 loss**를 그려 어디서 실패/작동하는지 파악.
- **도메인 비율(domain proportion)** 추적이 중요: 특정 도메인이 과소 대표되면 그 도메인 성능이 떨어짐(speech recognition에서 0이 너무 많고 1이 적으면 1을 못 배우는 것과 동일).
- **온라인 학습 문제**: 프런티어 모델은 실시간으로 데이터를 받는데, 지난달 batch에 coding 데이터가 적었다면 특정 도메인 성능이 하락. → **smart sampling**(RL의 experience replay처럼 replay memory에서 샘플링)으로 도메인 빈도 유지.

### 토큰 통계 (Token Statistics)
- 핵심 토큰의 **빈도 변화** 추적. 예: math/미분 기호 토큰이 과소 대표되면 미분 과제 성능이 크게 하락.
- token drift/분포/토큰별 빈도 모니터링 → sampling으로 보정.
- 예: "새 web crawl 후 비영어 토큰이 12%→19% 증가" → 특정 언어 성능의 상승 또는 하락 가능.

### 종합: 프런티어 랩이 모니터링하는 항목
| 영역 | 항목 |
|---|---|
| 학습/스케일링 | 전역·도메인별 train/val loss, scaling curve 정렬, gradient norm, learning rate |
| MoE | router 동작, expert 사용 균형. router가 같은 expert만 쓰거나 죽은 expert 발생 → **load balancing**으로 방지 |
| 능력 | 체크포인트 간 eval 벤치마크 |
| 데이터 | token 분포, tokenizer 통계, 오염 검사 |

> 이런 대시보드는 IP라 거의 공개되지 않으며, 보통 3~4년 뒤에야 정보가 공유된다.

---

## 💬 Q&A에서 나온 추가 인사이트

- **코딩 모델의 데이터 구성**: 인접 도메인(math가 functional programming을 돕는 등)을 섞는 게 도움이 될 수 있음. Python→C++→Java처럼 언어 간 전이. 단, web crawl 전체를 넣으면 잡음이 많아 성능 저하. Rust 인기 상승으로 토큰 빈도가 바뀌면 다른 언어 성능에 영향을 줄 수 있음(추정).
- **합성 데이터(synthetic data)**: 데이터 증강·합성은 일반적으로 좋지만, 너무 싸서 특정 도메인을 과대 생성하면 나머지 성능을 잠식. 합성 데이터 수익은 정체(plateau) 가능성. 현재는 합성보다 **고품질 데이터 부족**이 더 큰 병목(DeepMind 연구).
- **데이터 고갈 (Epoch AI 연구)**: ~2025 저품질 텍스트 데이터 고갈, ~2027 오디오/이미지/비디오 저품질 데이터 고갈, ~2030 고품질 데이터 고갈. 그 이후엔 데이터가 아니라 **모델 아키텍처**가 병목이 될 가능성.
- **데이터 정체의 본질**: 우리가 짠 Python 코드의 99%는 이미 온라인에 존재 → 모델이 새로 배울 게 적음. "더 많은 데이터"이지 "더 높은 품질"이 아니다. AI가 생성한 데이터가 다시 학습에 피드백되는 것도 학습에 별로 유익하지 않음.

---

## 🔑 핵심 용어

| 용어 | 설명 |
|---|---|
| 해석가능성(interpretability) | 모델 내부의 의사결정 과정을 이해·시각화하려는 연구 영역 |
| Saliency Map(현저성 맵) | pre-softmax score를 입력 픽셀로 미분 → 어느 픽셀이 점수에 영향 주는지 |
| Integrated Gradients(적분 그래디언트) | 검은 이미지→실제 이미지 경로의 gradient를 적분, saliency의 개선판 |
| Occlusion Sensitivity(가림 민감도) | mask 사각형을 옮기며 점수 변화를 추적해 결정 영역 파악 |
| Global Average Pooling (GAP) | feature map 전체를 단일 평균값으로 축소, 지역 정보 보존 |
| Class Activation Map (CAM) | feature map을 FC 가중치로 가중합한 클래스별 활성 지도 (Grad-CAM은 개선판) |
| Class Model Visualization | gradient ascent로 특정 클래스/활성을 최대화하는 입력 이미지를 합성 |
| Dataset Search | feature map을 가장 강하게 활성화한 top-k 이미지로 filter 해석 (현재 최다 사용) |
| 수용 영역(receptive field) | 한 활성이 입력에서 보는 영역. 깊을수록 넓음 |
| Deconvolution / Transposed Convolution | conv를 행렬곱(W)으로 보고 Wᵀ로 역공학. flip+subpixel+stride/2로 구현 |
| Switches | max pooling/ReLU의 위치를 forward 때 저장해 unpool/역전파에 사용하는 이진 행렬 |
| Attention Pattern | 토큰 간 관계 시각화 (CNN saliency map의 LLM 대응물) |
| Embedding | 토큰의 의미 표현 벡터, t-SNE로 시각화해 sanity check |
| Induction Head | transformer 내부 in-context learning을 들여다보는 Anthropic의 해석 도구 |
| Scaling Laws | 성능 ↔ 모델 크기·compute·데이터 크기의 power law (Chinchilla) |
| Chinchilla | DeepMind(2022). GPT-3는 과소 학습됐고, 더 작아도 더 오래 학습하면 우수 |
| Contamination(오염) | test set이 학습 데이터에 누출된 상태. n-gram/embedding 검색으로 탐지 |
| Mixture of Experts (MoE) | 여러 expert + router. router 실패/불균형을 load balancing으로 방지 |
| Data Diagnostics | 도메인 비율, 토큰 분포 drift, 오염을 추적하는 데이터 점검 |
| The Pile | 800GB 다양한 도메인 텍스트 데이터셋 (2020) |

---

## ✍️ 학습 메모

- 이번 강의의 큰 그림: **"내부를 본다"의 두 세계** — (1) CNN은 거의 완전히 시각화·해석 가능, (2) 프런티어 모델은 아직 미해결 연구 영역. CNN 기법을 도구함(toolkit)에 쌓아두면, 거대 모델 해석 연구를 따라갈 직관이 생긴다.
- **pre-softmax vs post-softmax**는 saliency, class model visualization 양쪽에서 반복되는 핵심 함정. softmax는 분모(다른 클래스)에 의존하므로 "원하는 클래스만" 분석하려면 항상 pre-softmax score를 써야 한다.
- 5가지 CNN 기법의 관계도: saliency(픽셀) → integrated gradients(경로) → occlusion(영역 가림) → CAM(구조 수정으로 실시간) → class model viz(역질의: 모델이 보는 세상) → dataset search(가장 단순·실용) → deconv(역공학 추적). **deconv의 "conv = 행렬곱 Wᵀ로 역전" 직관**이 가장 일반화 가능한 통찰.
- **깊이 ↔ 수용 영역 ↔ 추상화**의 일관된 테마: 깊은 층의 활성은 더 넓은 영역을 보고 더 복잡한 특징을 잡는다. Zeiler-Fergus 시각화가 이를 경험적으로 증명.
- 프런티어 진단의 4버킷(학습·스케일링 / 표현·내부 / 데이터·분포 / 능력 층위)은 실무에서 "무슨 일이야?" 질문을 체계적으로 분해하는 **체크리스트**로 활용 가능.
- scaling laws와 data 고갈(Epoch AI) 논의는 "다음 병목은 데이터가 아니라 아키텍처일 수 있다"는 미래 전망으로 연결 — 프로젝트 주제 선정 시 참고할 만함.
- 강사의 실무 조언: 벤치마크는 **상대값**으로 보고 커뮤니티 실사용 검증을 기다려라. RLHF/SFT는 **실패하는 eval에 집중**해 비용을 아껴라. CS230 프로젝트에 시간을 투자하라(취업·창업·인맥으로 이어진 사례 多).

---
<sub>Claude Code가 자막 전문(영어)을 읽고 작성한 강의 노트 · video_id: `Ozb1AR_F5MU`</sub>
