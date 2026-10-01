# C5. Knowledge Distillation — 큰 모델의 지식을 작은 모델로

> **이 노트를 다 읽으면**: teacher의 soft label에 들어 있는 "dark knowledge"를 숫자로 보여 줄 수 있다 · Hinton KD loss `α·CE + (1−α)·T²·KL`을 손으로 계산하고 T²가 왜 붙는지 gradient로 설명할 수 있다 · 라벨이 적은 센서 과제에서 무라벨 로그 + teacher로 작은 모델을 끌어올리는 실험을 직접 돌릴 수 있다 · LLM의 token-level / sequence-level / on-policy distillation을 구분하고, 양자화 손실 회복에 KD를 쓰는 법을 안다
> **JD 연결**: "Co-design model architectures that meet latency, memory, power, bandwidth" · "(우대) Lightweight LLM models / hybrid edge-LLM" — study_prep_list **C5**: teacher–student, soft label, 온도, 대형 모델 → 온디바이스 모델 ("Hark 서버 모델 → 기기 모델 경로")
> **Don 기준 난이도**: softmax·KL·cross-entropy는 A2에서 봤다(복습) · "golden reference로 싼 회로를 맞춘다"는 발상은 익숙하다 / 온도와 T² 스케일, 무라벨 데이터 활용, LLM distillation 변형들, KD + QAT 조합은 새로 배울 부분
> **선행 노트**: A2 (softmax·온도·KL), A3 (gradient·backprop), B2 (CNN), B7 (IMU 모델), B8 (SLM) — 참고: B5 (음성), B9 (cascade), C1–C3 (양자화)

---

## 0. 큰 그림 — 이게 왜 필요한가

edge ML 엔지니어가 매일 부딪히는 모순이 있다. **서버에서는 큰 모델이 잘 된다. 기기에는 작은 모델만 들어간다.** 작은 모델을 처음부터 같은 데이터로 학습시키면 보통 큰 모델보다 몇 %p 떨어진다. 이 차이를 줄이는 가장 값싼 도구가 **knowledge distillation(KD, 지식 증류)** 이다.

한 문장 정의: **KD는 큰 모델(teacher)이 내는 출력 확률분포를 작은 모델(student)이 따라 하도록 학습시키는 방법**이다. 정답 라벨 하나("이건 tap이다")만 보는 대신, teacher가 "tap 0.53, double-tap 0.32, walk 0.15"처럼 **오답끼리의 상대적 거리까지** 알려 준다.

Don의 경험으로 비유하면 이렇다.

- **teacher = 벤치 위의 golden reference.** 비싼 계측기, 레퍼런스 보드, float 모델. 느리고 크지만 "정답에 가장 가까운 행동"을 한다.
- **student = 양산 회로.** 싸고 작고 전력이 적다. 목표는 golden의 **출력 파형 전체**를 최대한 흉내 내는 것이다. 양산 보드를 튜닝할 때 pass/fail 한 비트만 보지 않고 레퍼런스와의 **파형 차이(오차 곡선)** 를 보고 맞추는 것과 같다. pass/fail(hard label)은 정보가 1비트 수준이지만, 파형(soft label)은 훨씬 많은 정보를 준다.
- **배포되는 것은 student뿐이다.** golden reference는 양산 라인에 따라가지 않는다. KD는 **학습 때만 비용**이 들고, 기기 쪽 추론 비용은 0만큼도 늘지 않는다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300">
<rect x="10" y="30" width="440" height="210" fill="none" stroke="currentColor" stroke-dasharray="6 4"/> <text x="20" y="50" font-size="13">학습 (서버 · 워크스테이션)</text> <rect x="25" y="115" width="140" height="50" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="95" y="136" font-size="12" text-anchor="middle">입력 창 x</text> <text x="95" y="154" font-size="12" text-anchor="middle">(라벨 적음 + 무라벨 많음)</text> <rect x="215" y="60" width="150" height="44" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="290" y="80" font-size="12" text-anchor="middle">Teacher (크다, 동결)</text> <text x="290" y="96" font-size="12" text-anchor="middle">예: 74k params</text>
<rect x="215" y="170" width="225" height="50" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="327" y="190" font-size="12" text-anchor="middle">Student 학습</text> <text x="327" y="208" font-size="12" text-anchor="middle">loss = α·CE + (1−α)·T²·KL</text> <line x1="165" y1="130" x2="212" y2="90" stroke="currentColor" stroke-width="1.5"/> <polygon points="215,88 205,90 210,96" fill="currentColor"/> <line x1="165" y1="152" x2="212" y2="185" stroke="currentColor" stroke-width="1.5"/> <polygon points="215,187 205,187 209,180" fill="currentColor"/> <line x1="290" y1="104" x2="290" y2="166" stroke="#3f9a6b" stroke-width="2"/> <polygon points="290,170 285,160 295,160" fill="#3f9a6b"/>
<text x="298" y="140" font-size="12">soft label p_T</text> <text x="120" y="200" font-size="12" text-anchor="middle">정답 y (있으면)</text> <rect x="470" y="30" width="200" height="210" fill="none" stroke="currentColor" stroke-dasharray="6 4"/> <text x="480" y="50" font-size="13">배포 (MCU · DSP · NPU)</text> <rect x="490" y="170" width="160" height="50" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="570" y="190" font-size="12" text-anchor="middle">Student 가중치만</text> <text x="570" y="208" font-size="12" text-anchor="middle">(예: 808 params, int8)</text> <line x1="440" y1="195" x2="486" y2="195" stroke="currentColor" stroke-width="1.5"/>
<polygon points="490,195 480,190 480,200" fill="currentColor"/> <text x="463" y="186" font-size="11" text-anchor="middle">export</text> <text x="570" y="90" font-size="12" text-anchor="middle">센서 → student → 판단</text> <text x="570" y="110" font-size="12" text-anchor="middle">teacher는 기기에 없다</text> <text x="10" y="270" font-size="13">추론 비용 증가 0 — KD는 "작은 모델을 더 잘 학습시키는 방법"이지 새 연산이 아니다</text>
</svg>
```

그림 1 — KD의 데이터 흐름. teacher는 학습 파이프라인 안에서만 돈다. 기기로 가는 것은 student의 가중치 파일 하나다.

Hark 같은 웨어러블이라면(추정) 이 경로는 여러 곳에 나온다: 서버의 큰 음성 모델 → 귀걸이 기기의 always-on wake word 모델, 큰 IMU/HAR 모델 → MCU의 작은 제스처 모델, 클라우드 LLM → 기기 SLM. 이 노트는 dark knowledge(1절) → Hinton 공식과 T²(2절) → 두 실험(3–4절) → 한계와 변형(5–7절) → LLM(8절) → cascade·레시피·임베디드(9–11절) 순서로 간다.

---

## 1. Dark knowledge — soft label에는 무엇이 들어 있나

### 1.1 직관: tap 창 하나를 보는 두 선생

손목 기기의 제스처 분류기를 생각한다. class는 `tap`, `double-tap`, `walk` 셋이다. 어떤 IMU 창의 정답은 `tap`이다.

- **라벨 파일**은 `[1, 0, 0]`이라고만 말한다. "double-tap이 아니다"와 "walk가 아니다"를 **똑같은 무게**로 말한다.
- **잘 학습된 teacher**는 logits `[5, 3, 0]`을 낸다. "tap이 가장 그럴듯하지만, 굳이 틀린다면 double-tap 쪽이지 walk는 아니다"라는 정보가 들어 있다.

두 번째 정보가 Hinton이 말한 **dark knowledge**다. 오답 class들 사이의 **순서와 비율**이 teacher가 학습 중에 알아낸 "class 간 유사도 구조"를 담고 있다. tap과 double-tap은 가속도 파형이 비슷하고, walk는 완전히 다르다는 사실이다. student는 이것을 라벨만으로는 스스로 알아내야 하지만, teacher의 출력을 보면 **공짜로** 얻는다.

### 1.2 문제: T = 1의 softmax는 너무 확신한다

logits `[5, 3, 0]`에 보통 softmax(A2 6절)를 쓰면:

```
e^5 = 148.41,  e^3 = 20.09,  e^0 = 1.00      합 = 169.50
p = [148.41/169.50, 20.09/169.50, 1.00/169.50] = [0.876, 0.118, 0.006]
```

말로 하면: 정답이 0.876을 가져가고, 오답 두 개는 0.118과 0.006이다. 둘의 비율은 20배로 정보는 있지만, **값 자체가 작아서** loss에 거의 기여하지 못한다. cross-entropy gradient는 `q − p` 꼴이라(A2 7절), 0.006과 0.000의 차이는 학습 신호로 거의 0이다.

### 1.3 해결: 온도 T로 분포를 부드럽게 (A2 10절 복습)

```
p_i(T) = exp(z_i / T) / ∑_j exp(z_j / T)
```

말로 하면: logits를 T로 나눈 뒤 softmax한다. T > 1이면 logit 차이가 줄어들어 분포가 평평해지고, 작은 확률들이 "보이는 크기"로 올라온다. **argmax(1등)는 T가 무엇이든 바뀌지 않는다** — T는 양수로 나누는 것이라 순서를 보존한다.

손으로 T = 4:

```
z / 4 = [1.25, 0.75, 0.00]
e^1.25 = 3.490,  e^0.75 = 2.117,  e^0 = 1.000     합 = 6.607
p(T=4) = [0.528, 0.320, 0.151]
```

이제 double-tap(0.320)과 walk(0.151)의 차이가 0.17로, student가 따라 할 만한 크기의 신호다. 비율은 20배에서 2.1배로 줄었지만 **순서는 그대로**다.

### 1.4 코드로 확인 — 온도별 분포와 entropy

이 코드는(예제 1) 1.2–1.3의 손계산을 확인하고, T가 커질수록 분포가 얼마나 퍼지는지(entropy)를 본다.

```python
import numpy as np
def softmax_T(z, T):
    e = np.exp((z - z.max()) / T)                   # 최댓값을 빼서 overflow 방지 (A2 6.3)
    return e / e.sum()
names = ["tap", "double-tap", "walk"]
z_t = np.array([5.0, 3.0, 0.0])                     # teacher logits: 이 창은 사실 tap
for T in [1, 2, 4, 8]:
    p = softmax_T(z_t, T)
    H = -(p * np.log(p)).sum()                      # entropy [nat]: 얼마나 퍼졌나
    print(f"T={T}: p = {np.round(p, 3)}  entropy {H:.3f} nat   p(double-tap)/p(walk) = {p[1]/p[2]:6.2f}")
print("hard label  :", np.eye(3)[0], " → double-tap과 walk가 똑같이 0")
print("argmax at every T:", [names[softmax_T(z_t, T).argmax()] for T in [1, 2, 4, 8]])
```

```text
T=1: p = [0.876 0.118 0.006]  entropy 0.399 nat   p(double-tap)/p(walk) =  20.09
T=2: p = [0.69  0.254 0.057]  entropy 0.767 nat   p(double-tap)/p(walk) =   4.48
T=4: p = [0.528 0.32  0.151]  entropy 0.988 nat   p(double-tap)/p(walk) =   2.12
T=8: p = [0.432 0.337 0.231]  entropy 1.068 nat   p(double-tap)/p(walk) =   1.45
hard label  : [1. 0. 0.]  → double-tap과 walk가 똑같이 0
argmax at every T: ['tap', 'tap', 'tap', 'tap']
```

출력에서 볼 것: 손계산 값(T=1의 0.876/0.118/0.006, T=4의 0.528/0.320/0.151)과 일치한다. entropy는 0.399에서 1.068 nat으로 커지고(3-class 최대는 ln 3 = 1.099), argmax는 모든 T에서 `tap`이다. T = 8쯤이면 거의 균등 분포에 가까워져 오히려 정보가 흐려지기 시작한다 — T는 "너무 작지도 크지도 않게" 고르는 값이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 310">
<line x1="70" y1="250" x2="620" y2="250" stroke="currentColor"/><line x1="70" y1="250" x2="70" y2="50" stroke="currentColor"/> <line x1="66" y1="250.0" x2="70" y2="250.0" stroke="currentColor"/><text x="62" y="254.0" font-size="12" text-anchor="end">0</text> <line x1="66" y1="155.0" x2="70" y2="155.0" stroke="currentColor"/><text x="62" y="159.0" font-size="12" text-anchor="end">0.5</text> <line x1="66" y1="60.0" x2="70" y2="60.0" stroke="currentColor"/><text x="62" y="64.0" font-size="12" text-anchor="end">1</text> <rect x="90" y="60.0" width="40" height="190.0" fill="#888"/><text x="110.0" y="56.0" font-size="12" text-anchor="middle">1.000</text>
<rect x="136" y="83.6" width="40" height="166.4" fill="#4a7bd0"/><text x="156.0" y="79.6" font-size="12" text-anchor="middle">0.876</text> <rect x="182" y="149.6" width="40" height="100.4" fill="#e08a3c"/><text x="202.0" y="145.6" font-size="12" text-anchor="middle">0.528</text> <text x="156.0" y="268" font-size="13" text-anchor="middle">tap</text> <rect x="260" y="250.0" width="40" height="0.0" fill="#888"/><text x="280.0" y="246.0" font-size="12" text-anchor="middle">0.000</text> <rect x="306" y="227.5" width="40" height="22.5" fill="#4a7bd0"/><text x="326.0" y="223.5" font-size="12" text-anchor="middle">0.118</text>
<rect x="352" y="189.1" width="40" height="60.9" fill="#e08a3c"/><text x="372.0" y="185.1" font-size="12" text-anchor="middle">0.320</text> <text x="326.0" y="268" font-size="13" text-anchor="middle">double-tap</text> <rect x="430" y="250.0" width="40" height="0.0" fill="#888"/><text x="450.0" y="246.0" font-size="12" text-anchor="middle">0.000</text> <rect x="476" y="248.9" width="40" height="1.1" fill="#4a7bd0"/><text x="496.0" y="244.9" font-size="12" text-anchor="middle">0.006</text> <rect x="522" y="221.2" width="40" height="28.8" fill="#e08a3c"/><text x="542.0" y="217.2" font-size="12" text-anchor="middle">0.151</text>
<text x="496.0" y="268" font-size="13" text-anchor="middle">walk</text> <rect x="90" y="282" width="14" height="14" fill="#888"/><text x="110" y="294" font-size="12">hard label</text> <rect x="240" y="282" width="14" height="14" fill="#4a7bd0"/><text x="260" y="294" font-size="12">T = 1</text> <rect x="390" y="282" width="14" height="14" fill="#e08a3c"/><text x="410" y="294" font-size="12">T = 4</text> <text x="70" y="26" font-size="13">teacher logits [5, 3, 0] (정답 tap) — 온도를 올리면 오답끼리의 순서가 보인다</text>
</svg>
```

그림 2 — 같은 teacher logits [5, 3, 0]을 hard label, T = 1, T = 4로 본 확률(예제 1의 실제 값). hard label은 오답 둘을 똑같이 0으로 만든다. T = 1은 walk를 0.006으로 거의 지운다. T = 4에서 "double-tap(0.320) > walk(0.151)"이라는 구조가 뚜렷하게 보인다.

### 1.5 soft label이 hard label보다 나은 이유 — 세 가지 관점

| 관점 | hard label `[1,0,0]` | soft label `[0.53, 0.32, 0.15]` |
|---|---|---|
| 정보량 | 샘플당 "어느 class인가" 하나 | class 간 유사도, 이 샘플이 얼마나 애매한지까지 |
| 애매한 샘플 | 경계 근처 샘플도 100% 확신하라고 강요 | "이건 반반쯤"이라고 알려 줌 → 과적합 완화 |
| gradient | 샘플마다 방향이 들쭉날쭉 (라벨 noise 포함) | teacher가 평균 내 준 매끈한 목표 → 분산이 작다 |

마지막 줄이 중요하다. Hinton et al.은 soft target이 샘플당 훨씬 많은 정보를 담고 gradient 분산이 작아서, student를 **더 적은 데이터와 더 큰 learning rate**로 학습시킬 수 있다고 설명했다. 펌웨어 비유로는 "정답 비트 하나 대신 레퍼런스 파형 전체를 받아서 튜닝한다"는 것이다.

**label smoothing과의 차이**: label smoothing은 `[1,0,0]`을 `[0.9, 0.05, 0.05]`처럼 **모든 오답에 똑같이** 확률을 나눠 준다. 오답 간 구조가 없다. KD의 soft label은 오답마다 다른 값을 준다. 그래서 둘은 비슷해 보이지만 정보량이 다르다.

### 1.6 함정

- teacher가 **틀린** 샘플에서는 soft label도 틀린 방향을 가리킨다. 그래서 라벨이 있는 샘플에서는 hard label을 조금 섞는다(다음 절의 α).
- T를 올리면 argmax는 그대로지만 **확률값(confidence)** 은 바뀐다. 펌웨어가 "확률 > 0.8이면 trigger" 같은 threshold를 쓴다면 T는 추론에서 무시하면 안 되는 값이다 (11절).

---

## 2. Hinton 2015 공식 — 온도, α, 그리고 T²

### 2.1 loss 정의

Hinton, Vinyals, Dean의 "Distilling the Knowledge in a Neural Network"(2015)의 loss를 요즘 흔히 쓰는 형태로 쓰면:

```
z_s = student logits,  z_t = teacher logits (teacher는 동결),  y = 정답 라벨

p_T = softmax(z_t / T)          ← teacher의 soft target
q_T = softmax(z_s / T)          ← student의 softened 출력
q_1 = softmax(z_s)              ← student의 보통 출력 (T = 1)

L = α · CE(y, q_1)  +  (1 − α) · T² · KL(p_T ‖ q_T)
    └ hard loss ┘      └──── soft (distillation) loss ────┘
```

말로 하면: 두 가지를 섞는다. (1) 정답 라벨에 대한 보통 cross-entropy(온도 없음), (2) teacher와 student를 **같은 온도 T**로 부드럽게 만든 뒤 두 분포의 KL divergence. α는 둘 사이의 비중이고, T²는 soft 항의 gradient 크기를 맞추는 보정 계수다(2.3절).

용어를 한 번 정리한다.

- **KL(p ‖ q) = ∑ p_i · log(p_i / q_i)**: p를 기준으로 q가 얼마나 다른지(A2 8.2). 0 이상이고, p = q일 때만 0이다. 대칭이 아니다.
- **CE(p, q) = −∑ p_i · log q_i = H(p) + KL(p ‖ q)**. teacher가 고정이면 H(p)는 상수라서 soft cross-entropy와 KL은 **gradient가 똑같다**. 구현마다 둘 중 하나를 쓴다. 다만 loss 값은 H(p)만큼 다르다 — KL로 쓰면 완벽히 따라 할 때 0이 되지만, CE로 쓰면 0이 되지 않는다.
- 요즘 흔한 α 표기(α가 hard 비중)와 논문마다 표기가 반대인 경우가 있다. 코드를 볼 때 **어느 항에 α가 붙었는지** 확인해야 한다. Hinton 논문은 hard 항에 "상당히 작은 가중치"를 줄 때 결과가 가장 좋았다고 했다.

### 2.2 gradient 유도 — 왜 T²인가

student logit 하나 `z_s,j`에 대한 soft 항의 gradient를 구한다. teacher는 상수다.

```
KL(p_T ‖ q_T) = ∑_i p_i log p_i  −  ∑_i p_i log q_i          (앞 항은 z_s와 무관)

∂/∂z_s,j [ −∑_i p_i log q_i ]  =  (q_j − p_j) · (1/T)
                                    └ softmax+CE의 익숙한 q−p ┘  └ z_s/T의 chain rule ┘
```

말로 하면: 온도가 없는 softmax + cross-entropy의 gradient는 `q − p`였다(A2 7.5, A3). 온도가 있으면 logits를 T로 나눴으니 chain rule로 `1/T`가 한 번 더 곱해진다.

그런데 `q − p` 자체도 T가 크면 작아진다. T가 크면 `exp(z/T) ≈ 1 + z/T`로 근사할 수 있어서(K는 class 수):

```
q_j ≈ 1/K + (z_s,j − z̄_s) / (K·T)          z̄ = logits 평균
p_j ≈ 1/K + (z_t,j − z̄_t) / (K·T)

q_j − p_j ≈ [ (z_s,j − z̄_s) − (z_t,j − z̄_t) ] / (K·T)

∂KL/∂z_s,j = (q_j − p_j)/T ≈ [ (z_s,j − z̄_s) − (z_t,j − z̄_t) ] / (K·T²)
```

말로 하면: T가 크면 soft 항의 gradient는 **1/T²에 비례해서** 줄어든다. T = 4면 1/16, T = 8이면 1/64. 그대로 두면 T를 바꿀 때마다 hard 항과 soft 항의 **상대 비중이 저절로 바뀌어서** α의 의미가 흔들린다. 그래서 soft 항에 T²를 곱해 gradient 크기를 T와 무관하게 만든다. 제어 루프로 치면 센서 gain을 바꿨을 때 loop gain이 같도록 controller gain을 반대로 보정하는 것과 같다.

덤으로 마지막 식은 **T → ∞에서 KD가 "평균을 뺀 logits끼리 맞추기(logit matching)"가 된다**는 것도 보여 준다. Hinton 논문이 지적한 점이고, Ba & Caruana(2014)가 logits에 MSE를 걸어 작은 모델을 학습시킨 방법이 그 극한이다.

### 2.3 코드로 확인 — gradient 크기 vs T

이 코드는(예제 2) (1) autograd gradient가 해석식 `(q_T − p_T)/T`와 같은지, (2) gradient 크기가 정말 1/T²로 줄고 T²를 곱하면 일정해지는지 본다. class 10개, 임의 logits.

```python
import torch, torch.nn.functional as F
torch.manual_seed(0)
z_t = torch.randn(10) * 3                            # teacher logits (class 10개)
z_s0 = torch.randn(10) * 3                           # student logits (아직 덜 배움)
print(" T   |grad| KL only   |grad| × T²   (q_T - p_T)/T 공식과 차이")
for T in [1, 2, 4, 8, 16, 32]:
    z_s = z_s0.clone().requires_grad_(True)
    p = F.softmax(z_t / T, -1)                       # teacher soft target
    kl = F.kl_div(F.log_softmax(z_s / T, -1), p, reduction="sum")   # KL(p_T ‖ q_T)
    kl.backward()
    q = F.softmax(z_s0 / T, -1)
    err = (z_s.grad - (q - p) / T).abs().max()       # 해석적 gradient와 autograd 비교
    g = z_s.grad.norm()
    print(f"{T:3d}   {g:12.6f}   {g * T * T:10.4f}     {err:.1e}")
lim = ((z_s0 - z_s0.mean()) - (z_t - z_t.mean())) / 10   # T→∞ 극한: 평균 뺀 logit 차이 / K
print(f"T→∞ 극한 |(z_s - z_t 평균 제거)/K| = {lim.norm():.4f}")
```

```text
 T   |grad| KL only   |grad| × T²   (q_T - p_T)/T 공식과 차이
  1       1.114876       1.1149     6.0e-08
  2       0.304082       1.2163     3.7e-09
  4       0.069479       1.1117     3.7e-09
  8       0.016367       1.0475     1.9e-09
 16       0.004007       1.0257     1.9e-09
 32       0.000995       1.0190     4.7e-10
T→∞ 극한 |(z_s - z_t 평균 제거)/K| = 1.0153
```

출력에서 볼 것: 오른쪽 열의 차이가 1e-8 이하라 해석식이 맞다. KL만의 gradient는 T가 2배 될 때마다 약 1/4로 줄어(0.0695 → 0.0164 → 0.0040 → 0.0010), T = 32에서는 T = 1의 1/1000 수준이다. T²를 곱하면 1.02~1.22 사이로 거의 일정하고, T가 커질수록 극한값 1.0153에 다가간다. `F.kl_div`의 인자 순서도 확인하자: **첫 인자는 student의 log-확률, 둘째 인자는 teacher의 확률**이고 결과는 `KL(teacher ‖ student)`이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 300">
<line x1="80" y1="250" x2="600" y2="250" stroke="currentColor"/><line x1="80" y1="250" x2="80" y2="40" stroke="currentColor"/> <line x1="80.0" y1="250" x2="80.0" y2="255" stroke="currentColor"/><text x="80.0" y="269" font-size="12" text-anchor="middle">1</text> <line x1="184.0" y1="250" x2="184.0" y2="255" stroke="currentColor"/><text x="184.0" y="269" font-size="12" text-anchor="middle">2</text> <line x1="288.0" y1="250" x2="288.0" y2="255" stroke="currentColor"/><text x="288.0" y="269" font-size="12" text-anchor="middle">4</text> <line x1="392.0" y1="250" x2="392.0" y2="255" stroke="currentColor"/><text x="392.0" y="269" font-size="12" text-anchor="middle">8</text>
<line x1="496.0" y1="250" x2="496.0" y2="255" stroke="currentColor"/><text x="496.0" y="269" font-size="12" text-anchor="middle">16</text> <line x1="600.0" y1="250" x2="600.0" y2="255" stroke="currentColor"/><text x="600.0" y="269" font-size="12" text-anchor="middle">32</text> <line x1="75" y1="223.8" x2="80" y2="223.8" stroke="currentColor"/><text x="72" y="227.8" font-size="12" text-anchor="end">1e-3</text> <line x1="75" y1="171.2" x2="80" y2="171.2" stroke="currentColor"/><text x="72" y="175.2" font-size="12" text-anchor="end">1e-2</text> <line x1="75" y1="118.8" x2="80" y2="118.8" stroke="currentColor"/><text x="72" y="122.8" font-size="12" text-anchor="end">1e-1</text>
<line x1="75" y1="66.2" x2="80" y2="66.2" stroke="currentColor"/><text x="72" y="70.2" font-size="12" text-anchor="end">1e0</text> <polyline points="80.0,63.8 184.0,93.4 288.0,127.1 392.0,160.0 496.0,192.1 600.0,223.9" fill="none" stroke="#4a7bd0" stroke-width="2.5"/> <circle cx="80.0" cy="63.8" r="3.5" fill="#4a7bd0"/> <circle cx="184.0" cy="93.4" r="3.5" fill="#4a7bd0"/> <circle cx="288.0" cy="127.1" r="3.5" fill="#4a7bd0"/> <circle cx="392.0" cy="160.0" r="3.5" fill="#4a7bd0"/> <circle cx="496.0" cy="192.1" r="3.5" fill="#4a7bd0"/> <circle cx="600.0" cy="223.9" r="3.5" fill="#4a7bd0"/>
<polyline points="80.0,63.8 184.0,61.8 288.0,63.8 392.0,65.2 496.0,65.7 600.0,65.8" fill="none" stroke="#e08a3c" stroke-width="2.5"/> <circle cx="80.0" cy="63.8" r="3.5" fill="#e08a3c"/> <circle cx="184.0" cy="61.8" r="3.5" fill="#e08a3c"/> <circle cx="288.0" cy="63.8" r="3.5" fill="#e08a3c"/> <circle cx="392.0" cy="65.2" r="3.5" fill="#e08a3c"/> <circle cx="496.0" cy="65.7" r="3.5" fill="#e08a3c"/> <circle cx="600.0" cy="65.8" r="3.5" fill="#e08a3c"/> <text x="298.0" y="53.8" font-size="13">T² × KL: 1.11 → 1.02 (거의 일정)</text> <text x="95" y="215" font-size="13">KL만: 1.11 → 0.001 (1/T²로 줄어듦)</text>
<text x="340.0" y="288" font-size="12" text-anchor="middle">온도 T (log 축)</text><text x="80" y="24" font-size="13">student logits에 대한 gradient 크기 (log 축) — 예제 2 실측</text>
</svg>
```

그림 3 — 예제 2의 gradient 크기를 log-log로 그렸다. 파랑(KL만)은 기울기 −2인 직선, 즉 1/T²로 떨어진다. 주황(T² × KL)은 거의 수평이다. 그래서 "T를 바꿔도 α의 의미가 유지되도록" T²를 곱한다.

### 2.4 손으로 계산 — 샘플 하나의 KD loss와 gradient (C로 확인)

teacher logits `[5, 3, 0]`(정답 tap), 아직 덜 배운 student logits `[2, 0, 1]`(walk와 헷갈림), T = 4, α = 0.1.

```
student q_T: (z_s − max)/4 = [0, −0.5, −0.25] → e = [1, 0.6065, 0.7788], 합 2.3853
             q_T = [0.419, 0.254, 0.326]
teacher p_T = [0.528, 0.320, 0.151]                             (1.3절)

KL = 0.528·ln(0.528/0.419) + 0.320·ln(0.320/0.254) + 0.151·ln(0.151/0.326)
   = 0.528·0.231 + 0.320·0.231 + 0.151·(−0.770)
   ≈ 0.122 + 0.074 − 0.116 = 0.080

q_1 = softmax([2, 0, 1]) = [7.389, 1, 2.718]/11.107 = [0.665, 0.090, 0.245]
CE  = −ln 0.665 = 0.408

L = 0.1 · 0.408 + 0.9 · 16 · 0.080 ≈ 0.041 + 1.149 = 1.190

∂L/∂z_s,i = α·(q_1,i − y_i) + (1 − α)·T·(q_T,i − p_T,i)       (T²·(1/T) = T)
i=0: 0.1·(0.665 − 1) + 3.6·(0.419 − 0.528) = −0.034 − 0.392 = −0.426
```

말로 하면: gradient descent는 `−∂L/∂z`로 움직이니 tap(i=0) logit은 올라가고, walk(i=2)는 내려간다. double-tap(i=1)도 올라간다(−0.229) — hard label만 쓰면 double-tap logit은 내려가야 했을 것이다(`q_1 − 0 = +0.09`). **teacher가 "double-tap은 tap과 비슷하니 너무 누르지 마라"라고 가르치는 장면**이다.

같은 계산을 펌웨어식 C로 한다(예제 3). 이 코드는 안정한 온도 softmax, KL, KD loss, gradient 해석식을 구현해 손계산과 비교한다.

```c
#include <math.h>
#include <stdio.h>
#define K 3
/* temperature softmax: 최댓값을 빼서 expf overflow 방지 */
static void softmax_T(const float *z, float T, float *p) {
    float m = z[0], s = 0.0f;
    for (int i = 1; i < K; i++) if (z[i] > m) m = z[i];
    for (int i = 0; i < K; i++) { p[i] = expf((z[i] - m) / T); s += p[i]; }
    for (int i = 0; i < K; i++) p[i] /= s;
}
int main(void) {
    const float zt[K] = {5.0f, 3.0f, 0.0f};          /* teacher logits (tap 창) */
    const float zs[K] = {2.0f, 0.0f, 1.0f};          /* student logits: walk와 헷갈림 */
    const float T = 4.0f, alpha = 0.1f;
    const int y = 0;                                  /* 정답 = tap */
    float pt[K], qs[K], q1[K], kl = 0.0f;
    softmax_T(zt, T, pt); softmax_T(zs, T, qs); softmax_T(zs, 1.0f, q1);
    for (int i = 0; i < K; i++) kl += pt[i] * logf(pt[i] / qs[i]);
    float ce = -logf(q1[y]);
    float loss = alpha * ce + (1.0f - alpha) * T * T * kl;
    printf("p_T(teacher) = %.3f %.3f %.3f\n", pt[0], pt[1], pt[2]);
    printf("q_T(student) = %.3f %.3f %.3f\n", qs[0], qs[1], qs[2]);
    printf("CE = %.4f  KL = %.4f  loss = %.4f\n", ce, kl, loss);
    for (int i = 0; i < K; i++) {                     /* dL/dz_s[i] 해석식 */
        float g = alpha * (q1[i] - (i == y)) + (1.0f - alpha) * T * (qs[i] - pt[i]);
        printf("dL/dz_s[%d] = %+.4f\n", i, g);
    }
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 kd.c -o kd -lm && ./kd
```

```text
p_T(teacher) = 0.528 0.320 0.151
q_T(student) = 0.419 0.254 0.326
CE = 0.4076  KL = 0.0798  loss = 1.1900
dL/dz_s[0] = -0.4260
dL/dz_s[1] = -0.2291
dL/dz_s[2] = +0.6550
```

PyTorch로 같은 값을 확인한다 (`kd_train.py`의 `kd_loss`는 3.2절에 있다).

```python
import torch, torch.nn.functional as F
from kd_train import kd_loss
z_s = torch.tensor([[2.0, 0.0, 1.0]], requires_grad=True)
loss = kd_loss(z_s, torch.tensor([0]), torch.tensor([[5.0, 3.0, 0.0]]), T=4.0, alpha=0.1)
loss.backward()
print(f"loss = {loss.item():.4f}  grad = {z_s.grad.numpy().round(4)}")
```

```text
loss = 1.1900  grad = [[-0.426  -0.2291  0.655 ]]
```

출력에서 볼 것: 손계산(1.190, −0.426), C, PyTorch가 모두 같다. 경고 0개로 컴파일됐다. soft 항(0.9 × 16 × 0.080 ≈ 1.15)이 hard 항(0.04)보다 훨씬 크다 — α = 0.1이면 학습은 대부분 teacher를 따라 하는 데 쓰인다.

### 2.5 함정 — 구현에서 자주 틀리는 곳

| 함정 | 무엇이 틀리나 |
|---|---|
| `F.kl_div(q, p)` 인자 순서 | 첫 인자는 **log**-확률(student), 둘째는 확률(teacher). 반대로 넣으면 조용히 다른 값 |
| `reduction="mean"` | class 수로도 나눠서 loss가 K배 작아진다. `"batchmean"`이 수학적 KL의 batch 평균 |
| T를 student에만 적용 | teacher와 student **둘 다** 같은 T로 나눠야 한다 |
| T² 빠뜨림 | T를 바꿀 때마다 α의 실효값이 바뀐다 → sweep 결과 해석 불가 |
| teacher를 train 모드로 둠 | dropout·BatchNorm 통계가 바뀌어 soft label이 매번 흔들린다. `teacher.eval()` + `torch.no_grad()` |
| hard 항에도 T 적용 | CE(y, q_T)로 쓰면 정답 쪽 gradient도 1/T로 줄어든다. hard 항은 T = 1 |

---

## 3. 실험 1 — 라벨 300개, 같은 데이터로 teacher와 student

### 3.1 데이터: 합성 "제스처 feature" 과제

B7처럼 IMU 창마다 feature를 뽑았다고 치고, 그 feature 벡터를 직접 합성한다. 실제 데이터가 아니라 **통제된 합성 데이터**라서 "무엇이 어려운지"를 우리가 안다.

- 8 class, **비슷한 것끼리 4쌍**(예: tap / double-tap, swipe-left / swipe-left-fast 같은 느낌). 쌍 안의 두 class는 가깝고 쌍끼리는 멀다 → dark knowledge가 있을 만한 구조.
- 6차원 잠재 변수(움직임의 "원인") → `tanh`와 `sin`으로 비선형 변환 → 24차원 feature + 측정 noise.
- 라벨 있는 학습 데이터는 **300개뿐**(기기 팀이 손으로 라벨링한 양이라고 가정). test는 20,000개로 크게 잡아 test 쪽 noise를 줄인다.

다음 두 파일을 저장하면 뒤의 예제들이 import한다.

```python
# kd_data.py — 합성 "제스처 feature" 데이터: 8 class = 비슷한 것끼리 4쌍, 24차원 feature
import numpy as np
K, D, LAT = 8, 24, 6
def _world(seed=123):                       # 데이터 "물리" (모든 split이 공유)
    r = np.random.default_rng(seed)
    pair = r.normal(0, 2.2, (K // 2, LAT))  # 쌍 중심은 멀리
    mu = np.repeat(pair, 2, axis=0) + r.normal(0, 0.9, (K, LAT))  # 쌍 안의 두 class는 가깝게
    A, B = r.normal(0, 0.6, (D, LAT)), r.normal(0, 0.8, (D, LAT))
    return mu, A, B
def make(n, seed, world=123):
    mu, A, B = _world(world)
    r = np.random.default_rng(seed)
    y = r.integers(0, K, n)
    z = mu[y] + r.normal(0, 1.0, (n, LAT))
    x = np.tanh(z @ A.T) + 0.5 * np.sin(z @ B.T) + r.normal(0, 0.15, (n, D))  # 센서 → feature 비선형
    return x.astype(np.float32), y
```

### 3.2 학습 도구: MLP, KD loss, 학습 루프

`kd_loss`가 2.1절 공식 그대로다. `fit`은 `teacher_logits`가 없으면 보통 CE 학습, 있으면 KD 학습을 한다. `mlp(h)`는 24 → h → h → 8 구조의 MLP(B1)다.

```python
# kd_train.py — MLP, KD loss, 학습 루프 (예제 4~9가 import)
import numpy as np, torch, torch.nn as nn, torch.nn.functional as F
from kd_data import K, D
torch.set_num_threads(4)

def mlp(h, depth=2, seed=0):                         # D → h → h → K (seed로 초기값 고정)
    torch.manual_seed(seed); layers, d = [], D
    for _ in range(depth):
        layers += [nn.Linear(d, h), nn.ReLU()]; d = h
    return nn.Sequential(*layers, nn.Linear(d, K))

def kd_loss(z_s, y, z_t, T, alpha):                  # Hinton 2015
    hard = F.cross_entropy(z_s, y) if alpha > 0 else 0.0
    soft = F.kl_div(F.log_softmax(z_s / T, 1), F.softmax(z_t / T, 1), reduction="batchmean")
    return alpha * hard + (1 - alpha) * T * T * soft

def fit(model, x, y, teacher_logits=None, T=4.0, alpha=1.0, epochs=300, lr=3e-3, wd=1e-4, seed=0):
    torch.manual_seed(seed)
    opt = torch.optim.Adam(model.parameters(), lr=lr, weight_decay=wd)
    x, y = torch.as_tensor(x), torch.as_tensor(y)
    for _ in range(epochs):
        for b in torch.randperm(len(x)).split(64):
            z = model(x[b])
            loss = F.cross_entropy(z, y[b]) if teacher_logits is None else \
                   kd_loss(z, y[b], teacher_logits[b], T, alpha)
            opt.zero_grad(); loss.backward(); opt.step()
    return model

@torch.no_grad()
def logits(model, x): return model(torch.as_tensor(x))
def accuracy(model, x, y): return (logits(model, x).argmax(1).numpy() == y).mean()
def n_params(model): return sum(p.numel() for p in model.parameters())
```

한 가지 설계 포인트: teacher logits는 **학습 전에 한 번만 계산해서 배열로 들고 있다**(`teacher_logits[b]`). teacher가 동결이고 데이터 증강(augmentation)이 없으면 매 step마다 teacher를 다시 돌릴 이유가 없다. 증강을 쓰면 얘기가 달라진다(10절).

### 3.3 기준선 — teacher와 student를 각자 학습

이 코드는(예제 4) 같은 300개로 큰 teacher(hidden 256)와 작은 student(hidden 16)를 각자 CE로 학습시켜 출발점을 잰다.

```python
import numpy as np, time
from kd_data import make
from kd_train import mlp, fit, accuracy, n_params
x_te, y_te = make(20000, seed=999)                  # 큰 test set (noise 줄이기)
x_l, y_l = make(300, seed=1)                        # 라벨 있는 데이터: 300개뿐
t0 = time.time()
teacher = fit(mlp(256, seed=1), x_l, y_l, epochs=200, wd=1e-3, seed=1)
student = fit(mlp(16, seed=1), x_l, y_l, seed=1)
print(f"teacher: {n_params(teacher):6d} params  test acc {accuracy(teacher, x_te, y_te):.3f}")
print(f"student: {n_params(student):6d} params  test acc {accuracy(student, x_te, y_te):.3f}")
print(f"student train acc {accuracy(student, x_l, y_l):.3f}   ({time.time()-t0:.1f} s)")
```

```text
teacher:  74248 params  test acc 0.793
student:    808 params  test acc 0.776
student train acc 1.000   (1.5 s)
```

출력에서 볼 것: 파라미터가 92배 차이(74,248 vs 808)인데 정확도 차이는 1.7%p뿐이다. student는 train 정확도 100%로 300개를 **외웠다**(A4의 overfitting). 참고로 이 합성 과제에서 라벨을 20,000개 주면 hidden 16 MLP도 86% 정도까지 간다(따로 확인한 값) — 즉 student의 병목은 **용량이 아니라 데이터**다. KD가 가장 잘 먹히는 조건이다.

808 파라미터면 int8로 1 KB도 안 된다. Cortex-M0+의 SRAM에도 들어간다.

### 3.4 T와 α sweep — seed 5개로 정직하게

이 코드는(예제 5) T ∈ {1, 2, 4, 8}, α ∈ {0.5, 0.1}의 8가지 KD 설정과 CE 기준선을 **seed 5개**에서 비교한다. seed마다 라벨 300개, teacher, student 초기값이 모두 바뀐다. 약 35초 걸린다.

```python
import numpy as np, itertools
from kd_data import make
from kd_train import mlp, fit, accuracy, logits
x_te, y_te = make(20000, seed=999)
Ts, alphas, seeds = [1, 2, 4, 8], [0.5, 0.1], [1, 2, 3, 4, 5]
res = {k: [] for k in ["teacher", "CE"] + [f"T{T} a{a}" for T, a in itertools.product(Ts, alphas)]}
for s in seeds:                                     # seed마다 라벨 300개·teacher·student 초기값이 다르다
    x_l, y_l = make(300, seed=s)
    teacher = fit(mlp(256, seed=s), x_l, y_l, epochs=200, wd=1e-3, seed=s)
    z_t = logits(teacher, x_l)                      # teacher의 soft target은 한 번만 계산
    res["teacher"].append(accuracy(teacher, x_te, y_te))
    res["CE"].append(accuracy(fit(mlp(16, seed=s), x_l, y_l, seed=s), x_te, y_te))
    for T, a in itertools.product(Ts, alphas):
        st = fit(mlp(16, seed=s), x_l, y_l, teacher_logits=z_t, T=T, alpha=a, seed=s)
        res[f"T{T} a{a}"].append(accuracy(st, x_te, y_te))
for k, v in res.items():
    v = np.array(v) * 100
    print(f"{k:10s} mean {v.mean():5.1f}%  std {v.std(ddof=1):4.1f}   seeds {np.round(v, 1)}")
```

```text
teacher    mean  79.7%  std  0.7   seeds [79.3 79.2 79.7 81.  79.3]
CE         mean  78.0%  std  0.9   seeds [77.6 77.3 78.7 79.1 77.2]
T1 a0.5    mean  78.0%  std  0.7   seeds [77.9 77.8 78.4 79.  77.1]
T1 a0.1    mean  78.0%  std  0.8   seeds [77.  77.8 78.6 79.  77.5]
T2 a0.5    mean  78.5%  std  1.2   seeds [78.2 77.5 79.1 80.3 77.5]
T2 a0.1    mean  78.5%  std  0.9   seeds [77.8 77.7 79.4 79.5 78. ]
T4 a0.5    mean  79.1%  std  0.7   seeds [78.4 78.8 79.5 80.1 78.6]
T4 a0.1    mean  79.1%  std  0.8   seeds [78.7 78.7 79.5 80.3 78.5]
T8 a0.5    mean  79.0%  std  1.0   seeds [79.  78.2 79.4 80.4 78. ]
T8 a0.1    mean  78.9%  std  1.0   seeds [78.5 78.7 79.3 80.2 77.6]
```

출력에서 볼 것:

- **T = 1은 효과가 없다**(78.0% = CE 78.0%). 1.2절에서 본 대로 T = 1의 soft label은 거의 hard label이다.
- **T = 4가 가장 좋다**: 79.1%, CE보다 +1.1%p, teacher(79.7%)와의 차이 1.7%p 중 약 3분의 2를 메웠다. T = 8도 비슷하다.
- α(0.5 vs 0.1)는 T²를 곱했기 때문에 큰 차이가 없다.
- **정직하게 말하면 효과는 작다.** seed 간 표준편차가 0.7~1.2%p다. 그런데 seed별로 보면 T = 4, α = 0.1은 5개 seed 모두에서 CE 이상이다(77.6→78.7, 77.3→78.7, 78.7→79.5, 79.1→80.3, 77.2→78.5). 같은 seed끼리 짝지어(paired) 보면 효과가 일관되다는 뜻이다. 한 번 돌려서 +1%p가 나왔다고 보고하면 안 되는 이유다.

왜 효과가 작은가? teacher와 student가 **같은 300개**를 봤다. teacher가 아는 것은 그 300개에서 배운 것이 전부고, teacher 자신도 79.7%에 불과하다. student는 teacher보다 잘 될 수 없다(대부분의 경우). 다음 절에서 이 두 제약을 푼다.

---

## 4. 실험 2 — 강한 teacher + 무라벨 로그 (edge에서 가장 실용적인 이득)

### 4.1 시나리오

실제 회사에서 KD가 크게 이기는 상황은 보통 이렇다.

- **teacher는 이미 강하다.** 서버 팀이 더 많은 데이터(여기서는 라벨 3,000개)로 학습한 큰 모델, 또는 공개 사전학습 모델이다.
- **기기 팀의 라벨은 적다.** 손으로 라벨링한 창 300개.
- **무라벨 데이터는 많다.** dogfood 기기가 매일 로그를 올린다(H1 온디바이스 로깅, H3 ingestion). 라벨링은 비싸지만(H4) 로그 자체는 싸다.

KD에서 student가 보는 데이터를 **transfer set**이라 부른다. transfer set에는 라벨이 필요 없다 — teacher가 soft label을 붙여 주기 때문이다. 이것이 KD의 가장 실용적인 면이다: **teacher = 자동 라벨러**.

### 4.2 코드 — 무라벨 창 수를 늘려 가며

이 코드는(예제 6) (1) 라벨 3,000개로 강한 teacher를 한 번 학습하고, (2) student를 CE(라벨 300), KD(300개 + 무라벨 0/1k/4k/16k)로 학습한다. KD는 soft label(T = 4)과 hard pseudo-label(teacher argmax만) 두 가지로 비교한다. 약 30초.

```python
import numpy as np
from kd_data import make
from kd_train import mlp, fit, accuracy, logits
x_te, y_te = make(20000, seed=999)
x_big, y_big = make(3000, seed=50)                  # 서버 팀의 큰 라벨 데이터 (teacher 전용)
teacher = fit(mlp(256, seed=0), x_big, y_big, epochs=60, wd=1e-3, seed=0)
print(f"teacher (3000 labels, 74k params): {100*accuracy(teacher, x_te, y_te):.1f}%")
out = {}
for s in [1, 2, 3]:
    x_l, y_l = make(300, seed=s)                    # 기기 팀이 직접 라벨링한 300개
    x_pool, _ = make(16000, seed=100 + s)           # 라벨 없는 기기 로그 (정답은 버림)
    out.setdefault("CE 300 labels", []).append(accuracy(fit(mlp(16, seed=s), x_l, y_l, seed=s), x_te, y_te))
    for n_u in [0, 1000, 4000, 16000]:
        x = np.concatenate([x_l, x_pool[:n_u]])
        z_t = logits(teacher, x)                    # teacher가 모든 창에 soft label을 붙인다
        ep = int(np.clip(150_000 / len(x), 15, 300))   # 데이터가 늘면 epoch을 줄여 step 수를 비슷하게
        soft = fit(mlp(16, seed=s), x, z_t.argmax(1), teacher_logits=z_t, T=4, alpha=0.0, epochs=ep, seed=s)
        hard = fit(mlp(16, seed=s), x, z_t.argmax(1).numpy(), epochs=ep, seed=s)   # teacher argmax만 (hard pseudo-label)
        out.setdefault(f"KD soft  +{n_u:5d} unlabeled", []).append(accuracy(soft, x_te, y_te))
        out.setdefault(f"KD hard  +{n_u:5d} unlabeled", []).append(accuracy(hard, x_te, y_te))
for k, v in out.items():
    print(f"{k:28s} {100*np.mean(v):5.1f}%  (seeds {np.round(100*np.array(v), 1)})")
```

```text
teacher (3000 labels, 74k params): 83.7%
CE 300 labels                 77.9%  (seeds [77.6 77.3 78.7])
KD soft  +    0 unlabeled     81.8%  (seeds [82.5 81.9 81.1])
KD hard  +    0 unlabeled     79.2%  (seeds [80.1 78.  79.6])
KD soft  + 1000 unlabeled     83.4%  (seeds [83.6 83.5 83.2])
KD hard  + 1000 unlabeled     82.3%  (seeds [82.2 82.1 82.5])
KD soft  + 4000 unlabeled     83.4%  (seeds [83.5 83.  83.7])
KD hard  + 4000 unlabeled     83.4%  (seeds [83.2 83.4 83.6])
KD soft  +16000 unlabeled     83.6%  (seeds [83.6 83.6 83.6])
KD hard  +16000 unlabeled     84.1%  (seeds [84.2 84.2 84. ])
```

(α = 0이라 라벨 300개의 정답도 쓰지 않고 teacher 출력만 따라 했다. 라벨이 있는 샘플에 hard 항을 섞는 변형은 연습문제로 남긴다.)

출력에서 볼 것:

1. **같은 300개로도 +3.9%p**(77.9 → 81.8). 실험 1(+1.1%p)보다 훨씬 크다. 차이는 teacher의 품질뿐이다. teacher가 300개 밖의 지식(3,000개)을 가지고 있으면 soft label로 그 지식이 넘어온다.
2. **무라벨 1,000개만 더해도 83.4%**, teacher(83.7%)와 거의 같다. 라벨링 비용은 0이다.
3. **soft vs hard**: transfer set이 작을 때(300개)는 soft가 2.6%p 앞선다 — dark knowledge가 샘플 부족을 메운다. transfer set이 커지면 차이가 사라지고, 16,000개에서는 hard pseudo-label이 오히려 0.5%p 높다. 이 과제에서는 teacher의 argmax가 이미 좋고 데이터가 충분하면 soft label의 추가 정보가 덜 중요해진다(또 epoch을 줄인 탓에 soft 쪽 학습이 덜 수렴했을 수도 있다). "soft가 항상 이긴다"는 믿음 대신 **둘 다 재 보는 것**이 맞다.
4. **student는 teacher를 넘지 못했다**(최고 84.1% vs 83.7%로 사실상 같음). 이 설정에서 student의 상한은 teacher다. teacher의 실수까지 배우기 때문이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 360">
<text x="10" y="44" font-size="13">A: 같은 300개로 학습</text> <text x="240" y="68" font-size="12" text-anchor="end">student CE only</text> <line x1="250" y1="64" x2="620" y2="64" stroke="currentColor" stroke-opacity="0.15"/> <line x1="321.2" y1="64" x2="379.3" y2="64" stroke="#888" stroke-width="3"/> <circle cx="350.2" cy="64" r="6" fill="#888"/><text x="350.2" y="55" font-size="12" text-anchor="middle">78.0</text> <text x="240" y="96" font-size="12" text-anchor="end">student KD T=4, α=0.1</text> <line x1="250" y1="92" x2="620" y2="92" stroke="currentColor" stroke-opacity="0.15"/> <line x1="363.9" y1="92" x2="414.6" y2="92" stroke="#4a7bd0" stroke-width="3"/>
<circle cx="389.3" cy="92" r="6" fill="#4a7bd0"/><text x="389.3" y="83" font-size="12" text-anchor="middle">79.1</text> <text x="240" y="124" font-size="12" text-anchor="end">teacher (74k params)</text> <line x1="250" y1="120" x2="620" y2="120" stroke="currentColor" stroke-opacity="0.15"/> <line x1="382.8" y1="120" x2="433.4" y2="120" stroke="#3f9a6b" stroke-width="3"/> <circle cx="408.1" cy="120" r="6" fill="#3f9a6b"/><text x="408.1" y="111" font-size="12" text-anchor="middle">79.7</text> <text x="10" y="152" font-size="13">B: 강한 teacher (라벨 3000)</text> <text x="240" y="176" font-size="12" text-anchor="end">student CE only (300)</text>
<line x1="250" y1="172" x2="620" y2="172" stroke="currentColor" stroke-opacity="0.15"/> <line x1="321.6" y1="172" x2="371.2" y2="172" stroke="#888" stroke-width="3"/> <circle cx="346.4" cy="172" r="6" fill="#888"/><text x="346.4" y="163" font-size="12" text-anchor="middle">77.9</text> <text x="240" y="204" font-size="12" text-anchor="end">KD soft, 300개</text> <line x1="250" y1="200" x2="620" y2="200" stroke="currentColor" stroke-opacity="0.15"/> <line x1="456.2" y1="200" x2="503.5" y2="200" stroke="#4a7bd0" stroke-width="3"/> <circle cx="479.8" cy="200" r="6" fill="#4a7bd0"/><text x="479.8" y="191" font-size="12" text-anchor="middle">81.8</text>
<text x="240" y="232" font-size="12" text-anchor="end">KD hard pseudo, 300개</text> <line x1="250" y1="228" x2="620" y2="228" stroke="currentColor" stroke-opacity="0.15"/> <line x1="355.5" y1="228" x2="429.3" y2="228" stroke="#d0564a" stroke-width="3"/> <circle cx="392.4" cy="228" r="6" fill="#d0564a"/><text x="392.4" y="219" font-size="12" text-anchor="middle">79.2</text> <text x="240" y="260" font-size="12" text-anchor="end">KD soft, 300 + 4000 무라벨</text> <line x1="250" y1="256" x2="620" y2="256" stroke="currentColor" stroke-opacity="0.15"/> <line x1="520.4" y1="256" x2="544.7" y2="256" stroke="#e08a3c" stroke-width="3"/>
<circle cx="532.5" cy="256" r="6" fill="#e08a3c"/><text x="532.5" y="247" font-size="12" text-anchor="middle">83.4</text> <text x="240" y="288" font-size="12" text-anchor="end">teacher 단일 (83.7%)</text> <line x1="250" y1="284" x2="620" y2="284" stroke="currentColor" stroke-opacity="0.15"/> <circle cx="542.6" cy="284" r="6" fill="#3f9a6b"/><text x="542.6" y="275" font-size="12" text-anchor="middle">83.7</text> <line x1="250" y1="312" x2="620" y2="312" stroke="currentColor"/> <line x1="283.6" y1="312" x2="283.6" y2="317" stroke="currentColor"/><text x="283.6" y="331" font-size="12" text-anchor="middle">76%</text>
<line x1="350.9" y1="312" x2="350.9" y2="317" stroke="currentColor"/><text x="350.9" y="331" font-size="12" text-anchor="middle">78%</text> <line x1="418.2" y1="312" x2="418.2" y2="317" stroke="currentColor"/><text x="418.2" y="331" font-size="12" text-anchor="middle">80%</text> <line x1="485.5" y1="312" x2="485.5" y2="317" stroke="currentColor"/><text x="485.5" y="331" font-size="12" text-anchor="middle">82%</text> <line x1="552.7" y1="312" x2="552.7" y2="317" stroke="currentColor"/><text x="552.7" y="331" font-size="12" text-anchor="middle">84%</text> <line x1="620.0" y1="312" x2="620.0" y2="317" stroke="currentColor"/><text x="620.0" y="331" font-size="12" text-anchor="middle">86%</text>
<text x="435.0" y="350" font-size="12" text-anchor="middle">test 정확도 (20,000개) — 점 = seed 평균, 막대 = ±1 표준편차</text>
</svg>
```

그림 4 — 두 실험의 정확도(점 = seed 평균, 가로 막대 = ±1 표준편차). A(같은 데이터)에서 KD 이득은 +1.1%p로 작다. B(강한 teacher)에서는 같은 300개로도 +3.9%p, 무라벨 4,000개를 더하면 83.4%로 teacher(83.7%)에 거의 붙는다. 가로축은 75%에서 시작한다.

### 4.3 임베디드 연결 — 이것은 "데이터 파이프라인" 문제다

실험 2의 교훈은 **KD의 이득 대부분이 알고리즘이 아니라 데이터에서 나온다**는 것이다. Don이 할 일로 번역하면:

- 기기에서 **무라벨 창을 대량으로** 모은다: 링버퍼 → flash → BLE/Wi-Fi 배치 업로드 (H1, H2).
- 서버에서 teacher로 **soft label을 일괄 생성**하고, teacher 버전과 함께 저장한다 (H3, H5). logits를 저장해 두면 student 실험을 여러 번 해도 teacher를 다시 돌릴 필요가 없다.
- 무라벨 데이터의 **분포가 실제 기기 분포**라는 점이 크다. 연구실에서 모은 라벨 데이터보다 실사용 분포(착용 방향, 사용자, 잡음)를 더 잘 덮는다(B7의 사용자별 차이).
- 음성처럼 사적인 데이터는 동의, 익명화, 보관 기간을 먼저 설계해야 한다 (H6).

---

## 5. Student가 너무 작으면 — capacity gap

### 5.1 직관

KD는 student에게 **teacher의 함수 모양을 흉내 낼 기회**를 준다. 하지만 흉내 낼 **능력(용량)** 자체가 없으면 소용없다. 1차 RC 필터로 8차 elliptic 필터의 응답을 흉내 내라고 레퍼런스 파형을 아무리 줘도 안 되는 것과 같다.

### 5.2 코드 — student 폭을 바꿔 가며

이 코드는(예제 7) hidden 폭 h ∈ {2, 4, 8, 16, 64}의 student를 CE(라벨 300)와 KD(300 + 무라벨 4,000)로 학습해 비교한다. teacher는 예제 6과 같다.

```python
import numpy as np
from kd_data import make
from kd_train import mlp, fit, accuracy, logits, n_params
x_te, y_te = make(20000, seed=999)
x_big, y_big = make(3000, seed=50)
teacher = fit(mlp(256, seed=0), x_big, y_big, epochs=60, wd=1e-3, seed=0)   # 예제 6과 같은 teacher
acc_t = accuracy(teacher, x_te, y_te)
for h in [2, 4, 8, 16, 64]:
    ce, kd = [], []
    for s in [1, 2, 3]:
        x_l, y_l = make(300, seed=s)
        x = np.concatenate([x_l, make(4000, seed=100 + s)[0]])
        z_t = logits(teacher, x)
        ce.append(accuracy(fit(mlp(h, seed=s), x_l, y_l, seed=s), x_te, y_te))
        kd.append(accuracy(fit(mlp(h, seed=s), x, z_t.argmax(1), teacher_logits=z_t, T=4, alpha=0.0,
                               epochs=35, seed=s), x_te, y_te))
    print(f"h={h:3d} ({n_params(mlp(h)):5d} params)  CE {100*np.mean(ce):5.1f}%  "
          f"KD+4000 {100*np.mean(kd):5.1f}%  gain {100*(np.mean(kd)-np.mean(ce)):+5.1f}  "
          f"teacher gap {100*(np.mean(kd)-acc_t):+5.1f}")
```

```text
h=  2 (   80 params)  CE  56.2%  KD+4000  55.0%  gain  -1.2  teacher gap -28.7
h=  4 (  160 params)  CE  74.0%  KD+4000  78.8%  gain  +4.8  teacher gap  -5.0
h=  8 (  344 params)  CE  76.7%  KD+4000  82.8%  gain  +6.2  teacher gap  -0.9
h= 16 (  808 params)  CE  77.9%  KD+4000  83.6%  gain  +5.7  teacher gap  -0.1
h= 64 ( 6280 params)  CE  79.0%  KD+4000  83.7%  gain  +4.7  teacher gap  -0.0
```

출력에서 볼 것: **h = 2(80 params)는 KD로도 55%**, 오히려 1.2%p 떨어졌다. 2차원 병목으로는 8 class를 가를 수 없다. h = 4부터 KD 이득이 생기고(+4.8), h = 8(344 params)이면 teacher와 0.9%p 차이까지 온다. h = 16 이상은 teacher에 붙는다. 흥미로운 점: **KD student h = 8(344 params, 82.8%)이 CE student h = 64(6,280 params, 79.0%)보다 18배 작으면서 3.8%p 높다.** "모델을 키우는 것"보다 "작은 모델을 더 잘 가르치는 것"이 싼 경우가 많다는 뜻이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 310">
<line x1="80" y1="250" x2="600" y2="250" stroke="currentColor"/><line x1="80" y1="250" x2="80" y2="40" stroke="currentColor"/> <line x1="75" y1="250.0" x2="80" y2="250.0" stroke="currentColor"/><text x="72" y="254.0" font-size="12" text-anchor="end">50%</text> <line x1="75" y1="197.5" x2="80" y2="197.5" stroke="currentColor"/><text x="72" y="201.5" font-size="12" text-anchor="end">60%</text> <line x1="75" y1="145.0" x2="80" y2="145.0" stroke="currentColor"/><text x="72" y="149.0" font-size="12" text-anchor="end">70%</text> <line x1="75" y1="92.5" x2="80" y2="92.5" stroke="currentColor"/><text x="72" y="96.5" font-size="12" text-anchor="end">80%</text>
<line x1="75" y1="40.0" x2="80" y2="40.0" stroke="currentColor"/><text x="72" y="44.0" font-size="12" text-anchor="end">90%</text> <line x1="126.1" y1="250" x2="126.1" y2="255" stroke="currentColor"/><text x="126.1" y="269" font-size="12" text-anchor="middle">80</text><text x="126.1" y="283" font-size="11" text-anchor="middle">h=2</text> <line x1="194.2" y1="250" x2="194.2" y2="255" stroke="currentColor"/><text x="194.2" y="269" font-size="12" text-anchor="middle">160</text><text x="194.2" y="283" font-size="11" text-anchor="middle">h=4</text>
<line x1="269.3" y1="250" x2="269.3" y2="255" stroke="currentColor"/><text x="269.3" y="269" font-size="12" text-anchor="middle">344</text><text x="269.3" y="283" font-size="11" text-anchor="middle">h=8</text> <line x1="353.1" y1="250" x2="353.1" y2="255" stroke="currentColor"/><text x="353.1" y="269" font-size="12" text-anchor="middle">808</text><text x="353.1" y="283" font-size="11" text-anchor="middle">h=16</text> <line x1="554.3" y1="250" x2="554.3" y2="255" stroke="currentColor"/><text x="554.3" y="269" font-size="12" text-anchor="middle">6280</text><text x="554.3" y="283" font-size="11" text-anchor="middle">h=64</text>
<line x1="80" y1="73.1" x2="600" y2="73.1" stroke="#3f9a6b" stroke-dasharray="6 4" stroke-width="2"/><text x="600" y="67.1" font-size="12" text-anchor="end">teacher 83.7% (74,248 params)</text> <polyline points="126.1,217.4 194.2,124.0 269.3,109.8 353.1,103.5 554.3,97.8" fill="none" stroke="#888" stroke-width="2.5"/> <circle cx="126.1" cy="217.4" r="4" fill="#888"/> <circle cx="194.2" cy="124.0" r="4" fill="#888"/> <circle cx="269.3" cy="109.8" r="4" fill="#888"/> <circle cx="353.1" cy="103.5" r="4" fill="#888"/> <circle cx="554.3" cy="97.8" r="4" fill="#888"/> <polyline points="126.1,223.8 194.2,98.8 269.3,77.8 353.1,73.6 554.3,73.1" fill="none" stroke="#e08a3c" stroke-width="2.5"/>
<circle cx="126.1" cy="223.8" r="4" fill="#e08a3c"/> <circle cx="194.2" cy="98.8" r="4" fill="#e08a3c"/> <circle cx="269.3" cy="77.8" r="4" fill="#e08a3c"/> <circle cx="353.1" cy="73.6" r="4" fill="#e08a3c"/> <circle cx="554.3" cy="73.1" r="4" fill="#e08a3c"/> <rect x="330" y="186" width="14" height="14" fill="#e08a3c"/><text x="350" y="198" font-size="12">KD soft (300 + 무라벨 4000)</text><rect x="330" y="208" width="14" height="14" fill="#888"/><text x="350" y="220" font-size="12">CE only (라벨 300)</text> <text x="340.0" y="300" font-size="12" text-anchor="middle">student 파라미터 수 (log 축)</text><text x="80" y="24" font-size="13">student 크기별 정확도 — 너무 작으면 KD도 소용없다 (예제 7, 3 seed 평균)</text>
</svg>
```

그림 5 — student 크기(파라미터 수, log 축)별 정확도. 회색(CE)은 크기를 키워도 천천히 오른다. 주황(KD)은 h = 8에서 이미 teacher 점선(83.7%)에 가깝다. 맨 왼쪽 h = 2에서는 두 선이 모두 55% 근처로 KD가 소용없다.

### 5.3 teacher와 student의 차이가 너무 크면 — teacher assistant

"더 좋은 teacher가 항상 더 좋은 student를 만든다"는 **틀린** 경우가 보고되어 있다. Cho & Hariharan(2019, "On the Efficacy of Knowledge Distillation")은 teacher가 너무 크고 정확하면 작은 student가 오히려 덜 배우는 현상을 보였고, Mirzadeh et al.(2020, "Improved Knowledge Distillation via Teacher Assistant")은 **중간 크기 모델(teacher assistant)** 을 거쳐 두 단계로 증류하면 낫다고 보고했다.

```
큰 teacher (서버)  ──KD──►  중간 모델 (TA)  ──KD──►  MCU student
 예: 수 MB                    예: 수백 KB               예: 수 KB
```

펌웨어로 치면 레퍼런스 → 개발 보드 → 양산 보드로 단계를 나눠 맞추는 것과 같다. 한 번에 차이가 너무 크면 중간 기준이 필요하다.

---

## 6. Feature distillation — FitNets hint

### 6.1 아이디어: 출력뿐 아니라 중간 과정도 따라 하게

지금까지는 **마지막 출력(logits)** 만 맞췄다. FitNets(Romero et al., 2015)는 teacher의 **중간 층 출력(feature)** 도 따라 하라고 가르친다. teacher의 중간 feature를 "hint", 그것을 흉내 내는 student 층을 "guided layer"라 부른다.

문제는 차원이 다르다는 것이다. teacher 중간층은 256차원, student는 16차원이다. 그래서 **학습용 projection 층**(regressor)을 하나 붙인다.

```
            x
     ┌──────┴───────┐
 teacher (동결)   student
  h_t [256]        h_s [16] ──► proj: Linear(16→256) ──► ĥ [256]
     │                │                                   │
  z_t [8]           z_s [8]            hint loss = MSE(ĥ, h_t)
     └── KD loss ─────┘

 L = (CE 또는 KD) + β · MSE(proj(h_s), h_t)
 proj는 학습 후 버린다 — 기기에 가는 것은 여전히 student뿐
```

말로 하면: student의 16차원 중간 표현을 선형 변환해서 teacher의 256차원 표현을 재구성할 수 있게 만든다. 즉 student 내부에 "teacher가 쓰는 정보"가 담기도록 압박한다.

### 6.2 코드 — hint가 도움이 되나?

이 코드는(예제 8) 강한 teacher(예제 6)로 라벨 300개에서 네 가지를 비교한다: CE만, CE + hint, KD만, KD + hint. `teacher[:4]`는 두 번째 ReLU 뒤의 256차원 feature다.

```python
import numpy as np, torch, torch.nn as nn, torch.nn.functional as F
from kd_data import make
from kd_train import mlp, fit, accuracy, logits, kd_loss
x_te, y_te = make(20000, seed=999)
x_big, y_big = make(3000, seed=50)
teacher = fit(mlp(256, seed=0), x_big, y_big, epochs=60, wd=1e-3, seed=0)
def fit_hint(x, y, use_kd, beta, seed, epochs=300):
    torch.manual_seed(seed)
    st, proj = mlp(16, seed=seed), nn.Linear(16, 256)             # proj: 16차원 → teacher의 256차원 (학습 후 버림)
    opt = torch.optim.Adam([*st.parameters(), *proj.parameters()], lr=3e-3, weight_decay=1e-4)
    x, y = torch.as_tensor(x), torch.as_tensor(y)
    with torch.no_grad(): h_t, z_t = teacher[:4](x), teacher(x)   # teacher 중간 feature(ReLU 뒤)와 logits
    for _ in range(epochs):
        for b in torch.randperm(len(x)).split(64):
            h_s = st[:4](x[b]); z_s = st[4](h_s)                   # student 중간 feature와 logits
            base = kd_loss(z_s, y[b], z_t[b], T=4, alpha=0.1) if use_kd else F.cross_entropy(z_s, y[b])
            loss = base + beta * F.mse_loss(proj(h_s), h_t[b])
            opt.zero_grad(); loss.backward(); opt.step()
    return st
for use_kd, beta in [(False, 0.0), (False, 1.0), (True, 0.0), (True, 1.0)]:
    acc = [accuracy(fit_hint(*make(300, seed=s), use_kd, beta, s), x_te, y_te) for s in [1, 2, 3, 4, 5]]
    print(f"{'KD' if use_kd else 'CE'} + hint beta={beta:3.1f}  acc {100*np.mean(acc):5.1f}%  (seeds {np.round(100*np.array(acc), 1)})")
```

```text
CE + hint beta=0.0  acc  78.0%  (seeds [77.8 76.6 78.4 79.6 77.8])
CE + hint beta=1.0  acc  78.2%  (seeds [77.6 77.4 78.7 79.3 78.2])
KD + hint beta=0.0  acc  82.3%  (seeds [82.8 82.1 81.7 83.  81.8])
KD + hint beta=1.0  acc  82.3%  (seeds [82.8 81.9 81.8 83.  81.9])
```

출력에서 볼 것: **이 과제에서는 hint가 거의 도움이 되지 않는다.** CE에 hint를 더하면 +0.2%p(seed noise 수준), KD에 더하면 0.0%p다. logit KD(82.3%)가 이득의 거의 전부다.

이 결과를 그대로 받아들이는 것이 중요하다. hint가 효과를 보는 경우는 보통 이렇다.

- student가 **깊고 좁을 때**: FitNets 원 논문의 설정이다. 깊은 네트워크는 출력 신호만으로는 앞쪽 층까지 학습 신호가 잘 안 간다. 중간에 목표를 하나 더 주면 도움이 된다.
- **출력이 class 확률이 아닐 때**: detection, segmentation, embedding 모델, 음성 encoder처럼 출력이 복잡하거나 feature 자체가 산출물일 때.
- 비슷한 계열로 attention map을 맞추는 attention transfer(Zagoruyko & Komodakis, 2017), transformer 층별 hidden state와 attention을 맞추는 방법들이 있다.

우리 2층 MLP는 얕고 출력이 단순해서, logit KD가 이미 필요한 정보를 다 전달한다. 함정 하나: teacher와 student의 **층 대응을 어떻게 고를지**(어느 층이 어느 층을 흉내 낼지)는 설계 선택이고, 잘못 고르면 오히려 해가 된다. hint 항의 스케일(β)도 feature 크기에 따라 다르므로 sweep이 필요하다.

---

## 7. KD + 양자화 · pruning

### 7.1 왜 같이 쓰나

KD, 양자화(C1–C3), pruning(C4)은 서로 다른 축이다.

| 기법 | 무엇을 줄이나 | 정확도를 무엇으로 되찾나 |
|---|---|---|
| KD | 아키텍처 크기 (층·폭) | teacher의 soft label |
| 양자화 | 값당 비트 수 | QAT (fake quant + STE, C2) |
| pruning | 0이 아닌 가중치 수 | fine-tuning |

실무에서는 **"작은 구조로 KD → int8 QAT"** 순서가 흔하다. 그리고 QAT나 pruning 후 fine-tuning 단계의 loss로 CE 대신 **KD loss를 쓰는 것**이 표준적인 트릭이다. 이때 teacher는 (a) 원래의 큰 모델, 또는 (b) **같은 모델의 float 버전**(self-distillation)이다. (b)는 "양자화 전의 나를 따라 하라"는 뜻이라 라벨 없이도 할 수 있다.

### 7.2 코드 — int4 QAT student에 KD 얹기

이 코드는(예제 9) student(h = 16)에 대해 다섯 가지를 비교한다: float CE, PTQ int4(학습 후 그냥 자름), QAT int4 CE, QAT int4 + self-KD(teacher = 자기 float 버전), QAT int4 + 큰 teacher + 무라벨 4,000개. fake quant는 per-tensor symmetric int4이고, `w + (q(w) − w).detach()`가 STE(C2)다.

```python
import numpy as np, torch, torch.nn as nn, torch.nn.functional as F
from kd_data import make
from kd_train import mlp, fit, accuracy, logits
def fq4(w):                                         # int4 symmetric per-tensor fake quant, STE
    s = w.detach().abs().max() / 7
    return w + (torch.clamp(torch.round(w / s), -8, 7) * s - w).detach()
class QLinear(nn.Linear):
    def forward(self, x): return F.linear(x, fq4(self.weight), self.bias)
def qmlp(h, seed):                                      # mlp(h)와 같은 구조, Linear만 QLinear로
    m = mlp(h, seed=seed)
    for i, l in enumerate(m):
        if isinstance(l, nn.Linear):
            q = QLinear(l.in_features, l.out_features); q.load_state_dict(l.state_dict()); m[i] = q
    return m
x_te, y_te = make(20000, seed=999)
teacher = fit(mlp(256, seed=0), *make(3000, seed=50), epochs=60, wd=1e-3, seed=0)
r = {}
for s in [1, 2, 3]:
    x_l, y_l = make(300, seed=s); x_u = make(4000, seed=100 + s)[0]
    fp = fit(mlp(16, seed=s), x_l, y_l, seed=s)                          # float student (CE)
    ptq = qmlp(16, s); ptq.load_state_dict(fp.state_dict())        # 학습 후 그냥 int4로 자름
    qce = qmlp(16, s); qce.load_state_dict(fp.state_dict()); fit(qce, x_l, y_l, epochs=100, lr=1e-3, seed=s)
    qself = qmlp(16, s); qself.load_state_dict(fp.state_dict())    # teacher = 자기 자신의 float 버전
    fit(qself, x_l, y_l, teacher_logits=logits(fp, x_l), T=2, alpha=0.0, epochs=100, lr=1e-3, seed=s)
    x = np.concatenate([x_l, x_u]); z = logits(teacher, x)
    qkd = qmlp(16, s); fit(qkd, x, z.argmax(1), teacher_logits=z, T=4, alpha=0.0, epochs=35, seed=s)
    for k, m in [("float CE", fp), ("PTQ int4", ptq), ("QAT int4 CE", qce),
                 ("QAT int4 + self-KD (float)", qself), ("QAT int4 + big teacher +4000", qkd)]:
        r.setdefault(k, []).append(accuracy(m, x_te, y_te))
for k, v in r.items(): print(f"{k:30s} {100*np.mean(v):5.1f}%  (seeds {np.round(100*np.array(v), 1)})")
```

```text
float CE                        77.9%  (seeds [77.6 77.3 78.7])
PTQ int4                        75.8%  (seeds [76.5 73.7 77.2])
QAT int4 CE                     77.8%  (seeds [77.9 78.  77.4])
QAT int4 + self-KD (float)      78.0%  (seeds [77.4 77.2 79.3])
QAT int4 + big teacher +4000    82.3%  (seeds [83.1 81.1 82.5])
```

출력에서 볼 것:

- PTQ int4는 2.1%p 잃고(77.9 → 75.8), QAT는 거의 다 되찾는다(77.8).
- **self-KD는 여기서 추가 이득이 없다**(78.0, noise 수준). 808 파라미터짜리 작은 MLP는 int4 손실이 작아서 되찾을 것이 별로 없다. self-KD가 빛나는 것은 양자화 손실이 **클 때**다 — 8.5절의 LLM 데모에서 본다.
- **KD 이득은 int4에서도 살아남는다**: int4 + 큰 teacher = 82.3%. float CE student(77.9%)보다 4.4%p 높다. 기기에 가는 것은 **int4 가중치 808개**(약 400 B)인데 float 74k 파라미터 teacher의 정확도에 1.4%p 차이까지 왔다.

### 7.3 pruning + distillation — LLM 업계의 표준 레시피

큰 모델을 **pruning으로 잘라서 student의 초기값을 만들고, 원래 모델을 teacher로 KD** 하는 방법이 LLM에서 널리 쓰인다. 무작위 초기값에서 시작하는 것보다 훨씬 적은 학습으로 좋은 작은 모델을 얻는다.

- NVIDIA의 Minitron(Muralidharan et al., 2024, "Compact Language Models via Pruning and Knowledge Distillation")이 이 방법을 체계적으로 보고했다.
- Meta는 Llama 3.2 1B/3B를 Llama 3.1 8B에서 pruning하고, 8B와 70B 모델의 logits를 token-level target으로 써서 학습했다고 발표했다(B8 2절).

순서 규칙(경험칙): **구조 줄이기(pruning/작은 아키텍처) → KD로 회복 → 양자화(QAT에서도 KD loss)**. 양자화를 먼저 하면 이후 단계가 모두 낮은 정밀도에서 돌아 디버깅이 어려워진다. 펌웨어로 치면 기능 검증 후 최적화 빌드를 켜는 순서와 같다.

---

## 8. LLM distillation — token-level, sequence-level, on-policy

### 8.1 token-level KD: 다음 token 분포를 따라 하기

LLM(B8)은 위치마다 vocab 전체(SmolLM2는 49,152개)에 대한 다음 token 확률을 낸다. 분류기의 KD를 **위치마다** 하면 된다.

```
x = token 열,  t = 위치 1..L,  V = vocab

L_token-KD = (1/L) ∑_t  KL( p_teacher(· | x_<t) ‖ p_student(· | x_<t) )
           = (1/L) ∑_t ∑_{v∈V} p_t(v) · log( p_t(v) / q_t(v) )
```

말로 하면(`x_<t`는 "t 앞의 token들"): 문장의 매 위치에서 "다음에 올 수 있는 49,152개 token 각각의 확률"을 teacher와 맞춘다. 정답 텍스트 한 줄로 학습하는 보통 pre-training(다음 token 하나만 정답)과 비교하면, 위치마다 수만 개 숫자의 soft label을 받는 셈이다.

비용이 문제다. 두 가지 방식이 있다.

| 방식 | 방법 | 비용 |
|---|---|---|
| online | 매 학습 step마다 teacher forward | teacher 가중치를 GPU 메모리에 같이 올려야 하고, step당 연산이 teacher만큼 늘어남 |
| offline | teacher logits를 미리 계산해 저장 | 저장 용량이 엄청남 → 보통 top-k만 저장 |

### 8.2 코드 — SmolLM2-135M에서 token 분포 들여다보기

"작은 student" 대신 **int4로 양자화한 복사본**을 student로 쓴다(C3). 구조가 같으니 분포 비교가 쉽고, 8.5절의 "양자화 손실 회복"으로 이어진다. 먼저 공용 모듈이다. 블록 안의 Linear 가중치만 64개 그룹 단위 symmetric 양자화한다(C1의 per-group). embedding과 lm_head는 그대로 둔다.

```python
# llm_kd.py — SmolLM2-135M teacher, fake-quant student, token-level KL (예제 10, 11이 import)
import copy, torch, torch.nn.functional as F
from transformers import AutoTokenizer, AutoModelForCausalLM
torch.manual_seed(0); torch.set_num_threads(8)
NAME = "HuggingFaceTB/SmolLM2-135M-Instruct"
tok = AutoTokenizer.from_pretrained(NAME, local_files_only=True)
teacher = AutoModelForCausalLM.from_pretrained(NAME, local_files_only=True, dtype=torch.float32).eval()

def fake_quant(w, bits, g=64):                      # per-group symmetric (C1): 64개마다 scale 하나
    o, i = w.shape; w2 = w.reshape(o, i // g, g); qmax = 2 ** (bits - 1) - 1
    s = w2.abs().amax(-1, keepdim=True) / qmax
    return (torch.clamp(torch.round(w2 / s), -qmax - 1, qmax) * s).reshape(o, i)

def quantized_copy(bits):                           # transformer 블록 안의 Linear 가중치만 양자화
    m = copy.deepcopy(teacher)
    with torch.no_grad():
        for n, mod in m.named_modules():
            if isinstance(mod, torch.nn.Linear) and ".layers." in n:
                mod.weight.copy_(fake_quant(mod.weight, bits))
    return m

def token_kl(z_s, z_t, T=1.0):                      # 위치마다 KL(teacher_T ‖ student_T), 위치 평균
    return F.kl_div(F.log_softmax(z_s / T, -1), F.log_softmax(z_t / T, -1),
                    log_target=True, reduction="none").sum(-1).mean()

HELD = ("The wearable device listens for a wake word using a tiny model on a low-power microcontroller. "
        "When the wake word is detected, a larger model on the application processor transcribes the speech "
        "and a small language model decides how to respond. Battery life depends on how often the large model "
        "runs. Engineers measure current draw with a power analyzer and compare it against the budget.")
```

이 코드는(예제 10) teacher(float)와 student(int4)의 위치별 분포를 비교하고, 온도에 따른 entropy와 KL, teacher가 한 위치에서 보는 후보 token들, logits 저장 비용을 계산한다. transformers의 경고 메시지는 stderr로 나오므로 `2>/dev/null`로 실행했다.

```python
import torch
from llm_kd import tok, teacher, quantized_copy, token_kl, HELD
ids = tok(HELD, return_tensors="pt").input_ids
student = quantized_copy(bits=4)                    # "작은 모델" 대신 int4 복사본을 student로
with torch.no_grad():
    z_t, z_s = teacher(ids).logits[0], student(ids).logits[0]   # [위치 수, vocab]
print("positions x vocab:", tuple(z_t.shape))
for T in [1.0, 2.0]:
    p = torch.softmax(z_t / T, -1)
    H = -(p * p.clamp_min(1e-30).log()).sum(-1).mean()
    print(f"T={T}: teacher entropy {H:.2f} nat   KL(teacher‖student) {token_kl(z_s, z_t, T):.4f}")
print(f"top-1 next-token agreement: {(z_s.argmax(-1) == z_t.argmax(-1)).float().mean():.3f}")
top = torch.softmax(z_t[10], -1).topk(5)            # 11번째 위치에서 teacher가 본 후보 5개
print("context:", repr(tok.decode(ids[0, :11])))
print("teacher top-5:", [(tok.decode(int(i)), round(float(v), 3)) for v, i in zip(top.values, top.indices)])
V, n_tok = z_t.shape[1], 1_000_000_000
print(f"full logits for 1B tokens, fp16: {n_tok * V * 2 / 1e12:.1f} TB   top-32 (fp16 value + int32 id): "
      f"{n_tok * 32 * 6 / 1e9:.0f} GB")
```

```text
positions x vocab: (73, 49152)
T=1.0: teacher entropy 3.04 nat   KL(teacher‖student) 0.3333
T=2.0: teacher entropy 8.40 nat   KL(teacher‖student) 0.1536
top-1 next-token agreement: 0.616
context: 'The wearable device listens for a wake word using a tiny'
teacher top-5: [(' microphone', 0.259), (',', 0.082), (' radio', 0.07), (' sensor', 0.059), (' receiver', 0.042)]
full logits for 1B tokens, fp16: 98.3 TB   top-32 (fp16 value + int32 id): 192 GB
```

출력에서 볼 것:

- **dark knowledge가 텍스트에서는 이렇게 생겼다**: "…using a tiny" 다음에 teacher는 ` microphone` 0.259, ` radio`, ` sensor`, ` receiver`에 확률을 나눠 준다. 실제 문장의 다음 단어(` model`)는 top-5에도 없다. 정답 token 하나로 학습하면 "microphone, sensor, receiver가 비슷한 부류"라는 정보는 사라진다.
- int4 student는 teacher와 top-1이 61.6%만 일치하고, KL은 token당 0.333 nat이다. 양자화 손실이 작지 않다.
- **LLM에서 T를 올리면 너무 평평해진다**: T = 2에서 entropy가 8.40 nat, 균등 분포의 ln 49152 ≈ 10.8 nat에 가깝다. 수만 개의 꼬리 token이 올라와 신호를 덮는다. 그래서 LLM KD는 보통 **T = 1** 근처를 쓴다. 분류기에서 T = 4가 좋았던 것은 class가 8개뿐이라서다.
- **저장 비용**: 1B token의 전체 logits를 fp16으로 저장하면 98 TB다. top-32만 저장하면 192 GB. 그래서 offline KD는 top-k만 저장하고 나머지 확률질량은 버리거나 하나로 묶는다.

### 8.3 sequence-level KD: teacher가 쓴 텍스트로 학습

Kim & Rush(2016, "Sequence-Level Knowledge Distillation")는 번역 모델에서 **teacher가 생성한 출력 문장(beam search 결과)을 정답처럼** 써서 student를 보통 CE로 학습시켰다. logits가 필요 없다.

| | token-level KD | sequence-level KD |
|---|---|---|
| student가 보는 것 | 위치마다 teacher 분포 전체 | teacher가 생성한 텍스트 (token 하나씩 정답) |
| teacher 접근 | logits 필요 (가중치 보유 또는 logits 제공) | 생성 결과만 있으면 됨 (API로도 가능) |
| 저장 | 크다 (top-k logits) | 작다 (텍스트) |
| vocab | teacher와 student의 tokenizer가 **같아야** 쉽다 | 달라도 된다 |
| 정보량 | 많다 | 적다 (샘플 하나당 한 경로) |

요즘 흔한 "큰 모델로 합성 데이터를 만들어 작은 모델을 SFT"는 넓게 보면 sequence-level distillation이다. DeepSeek-R1 보고서(2025)는 R1이 생성한 추론 샘플(약 80만 개로 보고)로 Qwen·Llama 계열의 작은 모델들을 fine-tuning해서 "distilled" 모델들을 공개했다. 음성에서는 Distil-Whisper(Gandhi et al., 2023)가 큰 Whisper로 대량의 음성에 pseudo-label 전사문을 붙이고(WER로 품질이 나쁜 것을 걸러), 더 작은 decoder를 가진 student를 학습시켜 훨씬 빠르면서 WER 차이는 작다고 보고했다. Hark 같은 음성 기기에 가장 가까운 사례다(추정).

### 8.4 on-policy distillation과 KL의 방향

token-level이든 sequence-level이든, 위의 방법들은 student가 **teacher(또는 사람)가 쓴 텍스트** 위에서 학습한다. 그런데 추론 때 student는 **자기가 쓴 텍스트** 위에서 다음 token을 고른다. 한 번 teacher와 다른 길로 새면 학습 때 본 적 없는 문맥에 놓인다(exposure bias — 학습과 실행 분포의 불일치).

- **on-policy KD**: student가 직접 텍스트를 생성하고, **그 텍스트의 위치마다 teacher 분포**를 target으로 준다. Agarwal et al.(2024, "On-Policy Distillation of Language Models: Learning from Self-Generated Mistakes", GKD)이 대표적이다. 펌웨어로 치면 레퍼런스가 정해 준 입력 벡터만이 아니라 **양산 보드가 실제로 들어가는 상태**에서 레퍼런스와 비교하는 것이다.
- **KL의 방향**: `KL(p ‖ q)`(forward, 지금까지 쓴 것)는 teacher가 확률을 주는 곳마다 student도 확률을 주라고 강요한다 → student가 넓게 퍼진다(mode-covering). `KL(q ‖ p)`(reverse)는 student가 확률을 준 곳에서 teacher도 높아야 한다 → student가 teacher의 큰 봉우리 하나에 집중한다(mode-seeking). MiniLLM(Gu et al., 2024)은 생성 모델에서 reverse KL이 낫다고 주장했다. 용량이 작은 student가 teacher의 모든 가능성을 다 흉내 낼 수 없으니 "확실한 것 하나를 잘"이 나을 수 있다는 논리다.

### 8.5 코드 — 양자화 손실을 self-distillation으로 되찾기

C3에서 LLM을 int4로 줄이면 품질이 떨어진다. **teacher = 양자화 전의 자기 자신**으로 KD를 하면 라벨 없이 일부를 되찾을 수 있다. LLM-QAT(Liu et al., 2023)은 이 아이디어를 쓴다: 모델이 스스로 생성한 데이터로, float 모델의 logits를 따라 하게 QAT를 한다.

여기서는 CPU에서 1분 안에 끝나도록 아주 작게 한다.

1. teacher(float)가 프롬프트 5개로 텍스트를 생성한다 — **sequence-level 데이터**.
2. 그 텍스트의 위치마다 teacher 분포를 target으로 둔다 — **token-level KD**.
3. 양자화된 가중치는 고정하고, **RMSNorm의 scale 파라미터(35,136개)만** 학습한다. 채널별 gain을 다시 맞추는 것이라 펌웨어의 "채널별 gain calibration"과 같은 발상이다.
4. 학습에 쓰지 않은, 사람이 쓴 문장(HELD)에서 KL과 top-1 일치율을 잰다.

예제 11 (약 30~40초, 예제 10처럼 `2>/dev/null`로 실행):

```python
import torch, time
from llm_kd import tok, teacher, quantized_copy, token_kl, HELD
t0 = time.time()
prompts = ["The battery in a wearable device", "Firmware engineers debug hardware by",
           "A microcontroller reads sensor data", "Speech recognition on small devices", "The history of the bicycle"]
with torch.no_grad():                               # 1) teacher가 학습 텍스트를 직접 만든다 (sequence-level 데이터)
    gen = [teacher.generate(**tok(p, return_tensors="pt"), max_new_tokens=80, do_sample=True,
                            temperature=0.8, top_p=0.95, pad_token_id=tok.eos_token_id) for p in prompts]
    z_gen = [teacher(g).logits[0] for g in gen]     # 2) 그 텍스트에서 teacher의 token별 분포 (token-level target)
held = tok(HELD, return_tensors="pt").input_ids     # 평가용: 학습에 안 쓴 사람이 쓴 문장
with torch.no_grad(): z_held = teacher(held).logits[0]
print(f"generated {sum(g.shape[1] for g in gen)} tokens in {time.time()-t0:.0f} s")
for bits in [4, 3]:
    st = quantized_copy(bits)
    norms = [p for n, p in st.named_parameters() if "norm" in n]   # RMSNorm scale만 학습 (양자화된 가중치는 고정)
    for p in st.parameters(): p.requires_grad_(False)
    for p in norms: p.requires_grad_(True)
    opt = torch.optim.Adam(norms, lr=2e-3)
    with torch.no_grad():
        z = st(held).logits[0]; kl0 = token_kl(z, z_held); a0 = (z.argmax(-1) == z_held.argmax(-1)).float().mean()
    for step in range(50):
        i = step % len(gen)
        loss = token_kl(st(gen[i]).logits[0], z_gen[i])            # student ← teacher 분포 (T = 1)
        opt.zero_grad(); loss.backward(); opt.step()
    with torch.no_grad():
        z = st(held).logits[0]; kl1 = token_kl(z, z_held); agree = (z.argmax(-1) == z_held.argmax(-1)).float().mean()
    print(f"int{bits}: trainable {sum(p.numel() for p in norms)} params | held-out KL {kl0:.3f} -> {kl1:.3f} "
          f"| top-1 agree {a0:.3f} -> {agree:.3f} | last train KL {loss.item():.3f} | {time.time()-t0:.0f} s")
```

```text
generated 351 tokens in 8 s
int4: trainable 35136 params | held-out KL 0.333 -> 0.235 | top-1 agree 0.616 -> 0.671 | last train KL 0.130 | 22 s
int3: trainable 35136 params | held-out KL 2.120 -> 1.329 | top-1 agree 0.274 -> 0.384 | last train KL 0.362 | 32 s
```

출력에서 볼 것:

- int4: 학습에 안 쓴 문장에서 KL 0.333 → 0.235(약 30% 감소), top-1 일치 61.6% → 67.1%. **라벨 0개, 생성 텍스트 351 token, 파라미터 35k개(전체의 0.03%)만** 건드렸다.
- int3: 손실이 훨씬 크고(KL 2.12, 일치 27%), 회복 폭도 크다(KL 1.33, 일치 38%). 하지만 여전히 int4 직후보다 나쁘다 — **KD는 양자화가 부순 정보를 새로 만들어 내지 못한다.** 비트가 너무 적으면 norm scale만으로는 한계가 있다.
- train KL(0.130, 0.362)이 held-out KL(0.235, 1.329)보다 훨씬 작다. 텍스트 5개에 과적합하고 있다. 실제로는 수천~수만 개의 생성 샘플과 더 많은 학습 파라미터(예: LoRA, 또는 STE로 가중치 자체)를 쓴다.
- 시간(초)은 이 Mac의 CPU 기준 실측이며 실행할 때마다 조금 다르다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 226">
<text x="180" y="65" font-size="12" text-anchor="end">int4 양자화 직후</text><rect x="190" y="50" width="56.9" height="22" fill="#888"/><text x="252.9" y="65" font-size="12">0.333</text> <text x="180" y="95" font-size="12" text-anchor="end">int4 + KD norm 튜닝</text><rect x="190" y="80" width="40.1" height="22" fill="#4a7bd0"/><text x="236.1" y="95" font-size="12">0.235</text> <text x="180" y="129" font-size="12" text-anchor="end">int3 양자화 직후</text><rect x="190" y="114" width="362.2" height="22" fill="#888"/><text x="558.2" y="129" font-size="12">2.120</text>
<text x="180" y="159" font-size="12" text-anchor="end">int3 + KD norm 튜닝</text><rect x="190" y="144" width="227.0" height="22" fill="#4a7bd0"/><text x="423.0" y="159" font-size="12">1.329</text> <line x1="190" y1="40" x2="190" y2="178" stroke="currentColor"/><line x1="190" y1="178" x2="600" y2="178" stroke="currentColor"/> <line x1="190.0" y1="178" x2="190.0" y2="183" stroke="currentColor"/><text x="190.0" y="197" font-size="12" text-anchor="middle">0</text> <line x1="275.4" y1="178" x2="275.4" y2="183" stroke="currentColor"/><text x="275.4" y="197" font-size="12" text-anchor="middle">0.5</text>
<line x1="360.8" y1="178" x2="360.8" y2="183" stroke="currentColor"/><text x="360.8" y="197" font-size="12" text-anchor="middle">1</text> <line x1="446.2" y1="178" x2="446.2" y2="183" stroke="currentColor"/><text x="446.2" y="197" font-size="12" text-anchor="middle">1.5</text> <line x1="531.7" y1="178" x2="531.7" y2="183" stroke="currentColor"/><text x="531.7" y="197" font-size="12" text-anchor="middle">2</text> <text x="395.0" y="216" font-size="12" text-anchor="middle">held-out 문장에서 KL(teacher ‖ student) [nat/token] — 작을수록 teacher와 같다</text><text x="10" y="24" font-size="13">SmolLM2-135M: 양자화 손실을 teacher(자기 float 버전)로 되찾기 — 예제 11</text>
</svg>
```

그림 6 — 예제 11의 held-out KL(token당 nat). 회색은 양자화 직후, 파랑은 teacher(float 자기 자신)로 RMSNorm scale만 50 step 증류한 뒤. int4는 0.333 → 0.235, int3는 2.120 → 1.329로 줄었지만 int3는 여전히 int4보다 훨씬 나쁘다.

### 8.6 업계는 어떻게 쓰나 (공개된 것만)

| 모델 | 공개된 내용 | KD 종류 |
|---|---|---|
| DistilBERT (Sanh et al., 2019) | BERT보다 40% 작고 60% 빠르면서 GLUE에서 BERT 성능의 97%를 유지했다고 보고 | 출력 KD + hidden state 정렬(cosine) |
| Gemma 2 (Google, 2024 기술 보고서) | 2B·9B 모델을 다음 token 예측 대신 더 큰 모델의 확률분포로 knowledge distillation 했다고 기술 | token-level |
| Llama 3.2 1B/3B (Meta, 2024) | 8B에서 pruning 후 8B·70B의 logits를 token-level target으로 사용했다고 발표 | pruning + token-level |
| Minitron (NVIDIA, 2024) | 큰 모델을 pruning하고 원 모델로 distillation해 작은 모델을 적은 학습량으로 얻었다고 보고 | pruning + logit KD |
| DeepSeek-R1 distilled (2025) | R1이 생성한 추론 데이터로 작은 Qwen·Llama 모델을 fine-tuning | sequence-level |
| Distil-Whisper (2023) | 큰 Whisper의 pseudo-label 전사문으로 작은 decoder 모델 학습 | sequence-level (pseudo-label) |

공통점: **작은 모델의 품질은 "무엇을 teacher로, 어떤 데이터 위에서 증류했나"에 크게 좌우된다**고 각 보고서가 말한다. 구체적인 비율, 온도, 데이터 양은 보고서마다 다르고 전부 공개되지는 않는다.

---

## 9. Cascade와 KD — 기기의 student, 클라우드의 teacher

B9 5절의 cascade와 L3의 hybrid 라우팅에서 기기 모델과 클라우드 모델은 **운영 중에도 teacher–student 관계**가 된다.

```
          기기 (student)                               클라우드 (teacher)
 입력 ─► 작은 모델 ─► confidence ≥ τ ? ── 예 ─► 기기에서 처리
                           │
                           └─ 아니오 ─► 큰 모델 ─► 응답
                                              │
                                              └─► (동의 하에) 입력 + teacher 출력 저장
                                                        │
                    다음 버전 student 학습 ◄── KD transfer set ┘
```

말로 하면: 기기가 어려워서 넘긴 입력이 바로 **student가 가장 약한 지점**이다. 그 입력에 대한 클라우드 teacher의 답(분포 또는 텍스트)을 모으면, 다음 student 학습에 딱 필요한 hard example 모음이 된다. 이렇게 되면 버전이 오를수록 escalate 비율 p가 줄고, B9의 기대 비용 `c1 + p·c2`가 내려간다.

주의할 점(추정 포함):

- 이 루프는 **escalate된 입력만** 모으므로 분포가 치우쳐 있다. 쉬운 입력도 일부 섞어야 student가 잘하던 것을 잊지 않는다.
- escalate 기준인 confidence는 student의 **calibration**(확률값이 실제 정답률과 맞는지)에 의존한다. KD 후 student의 확률값 분포가 바뀌므로 τ는 다시 맞춰야 한다(11절).
- 음성·개인 데이터면 수집 동의, 온디바이스 익명화, 보관 정책이 먼저다(H6).

L6의 speculative decoding도 비슷한 짝이다. 작은 draft 모델이 큰 모델을 잘 흉내 낼수록 수락률이 올라가므로, draft 모델을 target 모델로 증류하는 것이 자연스러운 선택이다.

---

## 10. 실전 레시피

### 10.1 순서

1. **teacher를 고른다.** 가장 정확한 모델이 아니라 **student와 입력·전처리가 같은** 모델이 좋다. teacher가 16 kHz log-mel을 보고 student가 8 kHz MFCC를 본다면 teacher 출력을 student 입력에 맞추기 어렵다. 가능하면 같은 feature 파이프라인을 쓴다.
2. **teacher logits를 저장한다.** transfer set(라벨 + 무라벨) 전체에 대해 teacher 버전과 함께 저장한다. 증강을 쓰면 아래 4번을 본다.
3. **기준선을 먼저 잰다.** student CE-only, teacher. seed 3~5개. 이 둘의 차이가 KD로 메울 수 있는 최대치에 가깝다.
4. **KD를 켠다.** 분류기는 T ∈ {2, 4, 8}, α ∈ {0.1, 0.5}부터. LLM은 T = 1부터. T²는 항상 곱한다.
5. **transfer set을 키운다.** 무라벨 데이터를 추가하는 것이 T·α 튜닝보다 보통 효과가 크다(4절).
6. **양자화는 마지막.** QAT에서도 KD loss를 쓴다(teacher = 큰 모델 또는 float student).
7. **배포 기준으로 평가한다.** int8 student를 기기와 같은 전처리로 평가하고, threshold를 다시 정한다(11절).

### 10.2 데이터 증강과 "일관된 teacher"

Beyer et al.(2022, "Knowledge distillation: A good teacher is patient and consistent")은 이미지 분류에서 (1) teacher와 student에게 **똑같이 증강한 같은 입력**을 주고(consistent), (2) **아주 오래** 학습시키면(patient) KD가 훨씬 잘 된다고 보고했다. 이 경우 증강된 입력마다 teacher 출력이 달라지므로 logits를 미리 저장할 수 없고 teacher를 online으로 돌려야 한다. IMU라면 회전·스케일 증강(B7), 음성이라면 잡음·잔향 증강(B5)을 teacher와 student에 같이 적용하는 것에 해당한다.

### 10.3 KD가 도움이 안 되는 경우

| 상황 | 왜 | 대안 |
|---|---|---|
| student가 너무 작다 (5절 h = 2) | 흉내 낼 용량이 없다 | 구조를 키우거나 과제를 단순화, teacher assistant |
| teacher가 student와 너무 다르다 | 입력·해상도·receptive field가 달라 teacher 함수를 student가 표현 못 함 | 같은 입력의 teacher, 중간 크기 teacher |
| teacher가 student보다 별로 안 좋다 (3절) | 줄 지식이 적다 | 더 많은 데이터로 teacher를 먼저 강화 |
| (참고) teacher와 student가 같은 크기 | Furlanello et al.(2018, "Born-Again Neural Networks")은 같은 크기로 증류해도 성능이 오를 수 있다고 보고 — soft label이 정규화처럼 작동 | 크기를 줄이지 않고 정확도·calibration을 다듬는 선택지 |
| teacher가 label smoothing으로 학습됨 | Müller et al.(2019, "When Does Label Smoothing Help?")에 따르면 오답 간 구조 정보가 지워져 KD 효과가 줄어든다 | label smoothing 없는 teacher |
| 라벨이 이미 충분히 많다 | student가 CE만으로도 용량 한계까지 간다 | KD 이득 작음 — 그래도 calibration은 좋아질 수 있음 |
| teacher 분포와 기기 입력 분포가 다르다 | teacher가 모르는 영역에서 자신 있게 틀린 soft label | 기기 로그를 transfer set으로 |

---

## 11. 임베디드 관점에서 다시 보기

### 11.1 배포물은 student 하나 — 런타임 비용 0

KD는 **학습 방법**이다. 기기에서 도는 것은 그냥 작은 모델이다. 그래서 펌웨어 쪽에서 볼 때 KD student는 CE student와 **같은 바이너리 구조**다: 같은 op, 같은 메모리, 같은 MAC 수. TFLite Micro, CMSIS-NN, 벤더 NPU 컴파일러(F 모듈) 어디에도 "KD 지원"은 필요 없다. 이 점이 pruning(하드웨어가 sparsity를 지원해야 빨라짐, C4)이나 MoE(메모리에 전부 있어야 함, B9)와 다르다.

| 항목 | KD student (h = 16, int8) | 비고 |
|---|---|---|
| 가중치 | 808개 → 808 B (int8) | int4면 약 404 B |
| MAC/추론 | 24·16 + 16·16 + 16·8 = 768 | Cortex-M4 100 MHz급에서 대략 10 μs 안팎 (추정) |
| 추가 런타임 | 없음 | teacher, T, α는 기기에 없다 |
| 정확도 (실험 2) | 83.6% vs CE 77.9% | 같은 비용으로 +5.7%p |

### 11.2 T는 추론에서 무시해도 되나? — argmax는 예, threshold는 아니오

KD 학습에 쓴 T는 기기 추론에서 쓰지 않는다. student는 T = 1로 추론한다. argmax만 쓴다면 T는 아무 상관이 없다(1.3절). 그러나 **확률 threshold**를 쓰는 경우(wake word "p > 0.9면 깨운다", cascade "confidence < τ면 클라우드로")는 다르다.

- KD student는 teacher의 **부드러운 분포를 흉내 내도록** 학습됐으므로 CE student와 확률값 분포가 다르다. 같은 threshold를 그대로 쓰면 FRR/FA(B5 3.5)가 바뀐다. 아래 예제 12에서 실제로 잰다.
- 그래서 KD 후에는 **validation set에서 threshold를 다시 정한다.** 필요하면 temperature scaling(logits를 상수 T_cal로 나누는 calibration)을 적용하는데, 이것은 펌웨어에서 logits에 상수 하나 곱하는 것이라 비용이 없다. int8 모델이면 출력 requantization scale(C1)에 흡수할 수도 있다.

이 코드는(예제 12) 실험 2의 teacher, CE student, KD student(무라벨 4,000개)가 test set에서 얼마나 확신하는지(최대 확률의 평균, 0.9 초과 비율) 정확도와 나란히 본다.

```python
import numpy as np, torch
from kd_data import make
from kd_train import mlp, fit, accuracy, logits
x_te, y_te = make(20000, seed=999)
teacher = fit(mlp(256, seed=0), *make(3000, seed=50), epochs=60, wd=1e-3, seed=0)
x_l, y_l = make(300, seed=1); x = np.concatenate([x_l, make(4000, seed=101)[0]]); z = logits(teacher, x)
ce = fit(mlp(16, seed=1), x_l, y_l, seed=1)
kd = fit(mlp(16, seed=1), x, z.argmax(1), teacher_logits=z, T=4, alpha=0.0, epochs=35, seed=1)
for name, m in [("teacher", teacher), ("CE student", ce), ("KD student", kd)]:
    p = torch.softmax(logits(m, x_te), 1); conf = p.max(1).values.numpy()
    acc = (p.argmax(1).numpy() == y_te).mean()
    print(f"{name:11s} acc {acc:.3f}  mean max-prob {conf.mean():.3f}  frac(max-prob>0.9) {(conf>0.9).mean():.3f}")
```

```text
teacher     acc 0.837  mean max-prob 0.906  frac(max-prob>0.9) 0.699
CE student  acc 0.776  mean max-prob 0.948  frac(max-prob>0.9) 0.837
KD student  acc 0.836  mean max-prob 0.899  frac(max-prob>0.9) 0.673
```

출력에서 볼 것: **CE student는 과신한다** — 정확도 77.6%인데 평균 최대 확률이 0.948이고, 83.7%의 샘플에서 0.9를 넘는다. 300개를 외워서(train 정확도 100%, 3.3절) 모든 것에 자신 있게 답한다. KD student는 정확도 83.6%에 평균 확신 0.899로 teacher(0.837 / 0.906)와 거의 같은 모양이다. 즉 KD는 정확도뿐 아니라 **확신의 정도(calibration)까지** teacher에게서 물려받는다. 펌웨어 입장에서는 "p > 0.9면 trigger"의 의미가 모델마다 다르다는 뜻이다. CE 모델에서 맞춘 threshold를 KD 모델에 그대로 쓰면 0.9를 넘는 비율이 83.7%에서 67.3%로 바뀐다.

이 코드는 int8 student 출력에 calibration 온도 `T_cal`을 적용했을 때 같은 threshold(0.8)에서 판단이 어떻게 바뀌는지 본다 (예제 13).

```c
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#define K 3
/* KD student의 int8 출력 → 확률 → threshold 판단. out_scale/out_zp는 모델의 출력 양자화 파라미터 */
static float prob_class0(const int8_t *lq, float out_scale, int out_zp, float inv_tcal) {
    float z[K], m, s = 0.0f;
    for (int i = 0; i < K; i++) z[i] = (float)(lq[i] - out_zp) * out_scale * inv_tcal;
    m = z[0];
    for (int i = 1; i < K; i++) if (z[i] > m) m = z[i];
    for (int i = 0; i < K; i++) s += expf(z[i] - m);
    return expf(z[0] - m) / s;
}
int main(void) {
    const int8_t lq[K] = {40, 25, -10};               /* int8 logits: class 0 = wake */
    const float out_scale = 0.1f, thr = 0.8f;         /* z = [4.0, 2.5, -1.0] */
    const float tcal[] = {1.0f, 0.7f, 1.5f};          /* validation에서 고른 T_cal 후보 */
    for (int k = 0; k < 3; k++) {
        float p = prob_class0(lq, out_scale, 0, 1.0f / tcal[k]);
        printf("T_cal=%.1f  p(wake)=%.3f  -> %s\n", tcal[k], p, p > thr ? "TRIGGER" : "ignore");
    }
    return 0;
}
```

```text
T_cal=1.0  p(wake)=0.813  -> TRIGGER
T_cal=0.7  p(wake)=0.894  -> TRIGGER
T_cal=1.5  p(wake)=0.712  -> ignore
```

출력에서 볼 것: 같은 int8 logits `[40, 25, −10]`(z = [4.0, 2.5, −1.0])인데 `T_cal`에 따라 p(wake)가 0.712~0.894로 바뀌고, 1.5에서는 판단이 뒤집힌다. 손으로: z = [4, 2.5, −1]이면 `1/(1 + e^−1.5 + e^−5) = 1/(1 + 0.223 + 0.007) = 0.813`. **KD로 모델을 바꾸면 이 확률 분포 자체가 바뀌므로**, threshold와 `T_cal`은 새 모델마다 validation에서 다시 정하고 펌웨어 설정값(버전과 함께)으로 관리한다. 곱셈 하나라서 기기 비용은 없다.

### 11.3 진짜 비용은 데이터 파이프라인에 있다

KD를 하려면 기기 쪽에서 해야 할 일이 생긴다.

- **무라벨 창 수집**(H1): 모델 입력과 **똑같은 전처리 지점**(예: log-mel 직후, 또는 raw IMU)에서 로그를 떠야 teacher와 student가 같은 입력을 본다. 전처리 버전을 로그 헤더에 기록한다.
- **업로드와 저장**(H2, H3): teacher logits는 입력보다 클 수 있다(분류기 8 class면 작지만 LLM은 top-k만).
- **버전 관리**(H5): "student v3 = teacher v7 + transfer set 2026-09 + T=4, α=0.1"처럼 증류 계보를 남긴다. 펌웨어의 golden reference 버전을 test report에 적는 것과 같다. teacher가 바뀌면 student 회귀 테스트를 다시 돈다(C8).
- **검증**(C8): student를 teacher와 비교하는 지표(top-1 일치율, KL)도 정확도와 함께 CI에 넣는다. teacher와의 일치율이 갑자기 떨어지면 전처리 불일치를 먼저 의심한다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `F.kl_div(p, log q)`처럼 인자 반대 | loss가 음수이거나 학습이 이상하게 느림 | 첫 인자는 student log-확률, 둘째는 teacher 확률 | `F.kl_div(F.log_softmax(z_s/T), F.softmax(z_t/T))` |
| T² 누락 | T를 올릴수록 KD 효과가 사라짐 | soft 항 gradient가 1/T²로 작아짐 | soft 항에 `T·T` 곱하기 |
| `reduction="mean"` | KD 항이 K배 작아 거의 무시됨 | class 수로도 나눔 | `"batchmean"` |
| teacher가 train 모드 | 같은 입력에 soft label이 매번 다름, 결과 재현 안 됨 | dropout, BN 통계 갱신 | `teacher.eval()`, `torch.no_grad()` |
| teacher와 student의 전처리가 다름 | KD 이득 없음, 기기에서 teacher 일치율 낮음 | 정규화 통계·샘플레이트·feature 설정 불일치 | 같은 전처리 코드, 버전 기록 |
| seed 한 번으로 결론 | "T=8이 T=4보다 0.3%p 좋다" 같은 noise 해석 | seed 간 표준편차 ~1%p | seed 3~5개, 같은 seed끼리 비교 |
| LLM KD에 분류기처럼 T = 4 | student가 흐릿한 분포를 학습, 품질 저하 | vocab이 크면 꼬리가 신호를 덮음 | T = 1 근처, top-k |
| KD 후 threshold 재사용 | 필드에서 FA/FRR이 바뀜 | student의 확률값 분포가 달라짐 | validation에서 threshold·T_cal 재설정 |
| student가 너무 작음 | KD를 해도 CE와 같거나 나쁨 | 용량 부족 | 폭·깊이를 늘리거나 teacher assistant |

---

## 13. 면접에서 이렇게 말한다

**Q.** "Explain knowledge distillation."

**A.** 큰 teacher 모델의 출력 확률분포를 작은 student가 따라 하도록 학습시키는 방법이다. 정답 라벨은 "tap이다"라는 정보뿐이지만, teacher의 분포는 "굳이 틀리면 double-tap이지 walk는 아니다"라는 class 간 유사도까지 준다. loss는 정답에 대한 CE와 온도를 올린 teacher·student 분포 사이의 KL을 섞는다. 배포되는 것은 student뿐이라 런타임 비용이 늘지 않는다. 실험에서 라벨 300개짜리 과제에서 808 파라미터 student가 CE로 77.9%였는데 강한 teacher와 무라벨 데이터로 증류하니 83.6%, teacher와 거의 같아졌다.

> Knowledge distillation trains a small student to match the output distribution of a larger teacher instead of only the one-hot labels. The teacher's soft probabilities carry what Hinton called dark knowledge: which wrong classes are similar to the right one. The loss is a weighted sum of cross-entropy on the hard labels and a KL divergence between teacher and student softmax outputs at a raised temperature. At deployment you only ship the student, so there is zero runtime overhead. In a small experiment I ran, an 800-parameter student went from 78 to about 84 percent, matching a 74k-parameter teacher, when I distilled on the labeled set plus a few thousand unlabeled windows.

**Q.** "Why the temperature, and why multiply by T squared?"

**A.** T = 1이면 teacher가 너무 확신해서 오답 확률이 0.006처럼 작아 학습 신호가 없다. T로 logits를 나누면 argmax는 그대로면서 오답 간 순서가 보이는 크기로 올라온다. 그런데 온도를 넣으면 KL의 gradient가 `(q_T − p_T)/T`이고, T가 크면 `q − p` 자체도 1/T로 작아져서 전체가 1/T²로 줄어든다. 그대로 두면 T를 바꿀 때마다 hard 항과 soft 항의 비중이 바뀐다. T²를 곱하면 gradient 크기가 T와 무관해져 α의 의미가 유지된다. 직접 재 보니 T를 1에서 32로 올릴 때 KL gradient는 1000배 줄었고 T²를 곱하면 거의 일정했다. T가 아주 크면 KD는 평균을 뺀 logits끼리 맞추는 것과 같아진다.

> At temperature one a confident teacher puts almost zero probability on the wrong classes, so there's no signal in them. Dividing the logits by T keeps the argmax but lifts the relative ordering of the wrong classes to a usable scale. The catch is that the gradient of the soft loss with respect to the student logits is (q minus p) over T, and for large T the difference q minus p itself shrinks like 1 over T, so the gradient scales as 1 over T squared. Multiplying the soft term by T squared keeps its gradient magnitude roughly constant, so changing T doesn't silently change the balance with the hard-label term. In the high-temperature limit, distillation reduces to matching the zero-mean logits.

**Q.** "How would you get a server speech model's quality onto an always-on MCU model?"

**A.** 네 단계다. 첫째, 서버 모델을 teacher로 쓰되 MCU 모델과 **같은 feature 파이프라인**(같은 log-mel 설정)의 입력을 보게 맞춘다. 둘째, dogfood 기기에서 동의받은 무라벨 오디오를 대량으로 모으고 teacher로 frame 단위 soft label을 붙인다 — 라벨 비용 없이 실제 기기 분포를 덮는 transfer set이 생긴다. 셋째, DS-CNN 같은 student를 KD로 학습하고, 차이가 너무 크면 중간 크기 모델을 거쳐 두 단계로 증류한다. 넷째, int8 QAT를 하면서도 KD loss를 유지하고, 마지막에 FRR과 시간당 FA로 threshold를 다시 정한다. 운영 중에는 cascade에서 escalate된 샘플이 다음 버전의 hard example이 된다.

> I'd treat the server model as a teacher and make sure it sees the same front end as the device model, same log-mel settings, so their outputs are comparable. Then I'd use dogfood devices to collect a large pool of consented, unlabeled audio and have the teacher produce frame-level soft labels offline. That gives a transfer set that matches the real device distribution without labeling cost. I'd distill into a DS-CNN-style student sized for the MCU budget, possibly through an intermediate teacher-assistant model if the gap is large, then run int8 quantization-aware training with the distillation loss still on. Finally I'd re-tune the trigger threshold on false rejects and false accepts per hour, because distilled students tend to have softer probabilities. In production, the samples the device escalates to the cloud are exactly the hard examples for the next student.

**Q.** "Knowledge distillation for LLMs — logits or generated data?"

**A.** 두 가지가 있다. token-level은 위치마다 teacher의 vocab 전체 분포를 KL로 맞춘다. 정보가 가장 많지만 teacher logits가 필요하고 tokenizer가 같아야 하며, 저장하면 1B token에 fp16 전체 logits가 약 98 TB라 top-k만 저장한다. sequence-level은 teacher가 생성한 텍스트로 보통 SFT를 한다. 텍스트만 있으면 되고 API teacher도 가능하지만 정보가 적다. 둘 다 teacher가 쓴 텍스트 위에서 학습하므로 추론 때와 분포가 다른 문제가 있어, student가 생성한 텍스트에 teacher 분포를 주는 on-policy 방식(GKD)이 나왔다. LLM에서는 vocab이 커서 온도는 보통 1 근처다. 공개된 예로 Gemma 2의 작은 모델, Llama 3.2 1B/3B가 logits 기반 distillation을 썼다고 밝혔다.

> There are two main flavors. Token-level distillation matches the teacher's full next-token distribution at every position with a KL loss. It's the richest signal but needs teacher logits and a shared tokenizer, and storing them is expensive, about 98 terabytes for a billion tokens at fp16 with a 49k vocab, so people keep only the top-k. Sequence-level distillation just fine-tunes the student on text the teacher generated, which works even through an API and with different tokenizers but carries less information. Both train on teacher-written text, so on-policy methods like GKD have the teacher score the student's own generations to fix the train-inference mismatch. Public examples include Gemma 2's smaller models and Llama 3.2 1B and 3B, which used logit-based distillation from larger models.

**Q.** "Does the student ever beat the teacher? When doesn't KD help?"

**A.** 보통 student의 상한은 teacher다 — 내 실험에서도 무라벨 데이터를 16배 늘려도 teacher 정확도에서 멈췄다. 같은 크기 증류(born-again)나 teacher가 못 본 라벨을 함께 쓰는 경우에 가끔 넘는다는 보고가 있다. KD가 안 먹히는 경우는 student가 너무 작을 때(80 파라미터 student는 KD로도 55%였다), teacher가 student보다 별로 좋지 않을 때(같은 데이터로 학습하면 이득이 1%p 정도), teacher와 student의 입력이 다를 때, 그리고 teacher가 label smoothing으로 학습돼 오답 간 구조가 지워졌을 때다.

> Usually the teacher is the ceiling: in my experiment, adding sixteen times more unlabeled data plateaued right at teacher accuracy. Same-size born-again distillation can occasionally surpass the teacher because the soft labels act as a regularizer. Distillation doesn't help much when the student lacks capacity, when the teacher isn't much better than the student, which is what happens if both are trained on the same small dataset, when the two models see different inputs, or when the teacher was trained with label smoothing, which erases the similarity structure among wrong classes.

**Q.** "How does distillation interact with quantization?"

**A.** 서로 다른 축이라 잘 합쳐진다. 흔한 순서는 작은 구조로 KD → QAT인데, QAT fine-tuning의 loss로 CE 대신 KD loss를 쓴다. teacher는 큰 모델이거나 같은 모델의 float 버전이다. 후자는 라벨 없이도 되므로 LLM 양자화 회복에 쓴다. 직접 해 보니 SmolLM2-135M int4에서 float 모델을 teacher로 RMSNorm scale만 50 step 학습했더니 held-out KL이 0.333에서 0.235로 줄었다. 다만 int3처럼 손실이 크면 회복 후에도 int4보다 나빴다 — KD는 비트가 부순 정보를 새로 만들지 못한다.

> They're orthogonal and compose well. A common pipeline is distill into a small architecture, then do quantization-aware training where the fine-tuning loss is the distillation loss instead of plain cross-entropy. The teacher can be the big model or the float version of the same model, and the latter needs no labels, which is why it's used to recover quality in quantized LLMs. I tried a tiny version on SmolLM2-135M: with int4 group-wise weights, tuning only the RMSNorm scales against the float model's logits cut the held-out KL from 0.33 to 0.24 nats per token. At int3 it recovered a lot but stayed worse than plain int4, so distillation can't recreate information the bits destroyed.

---

## 14. 직접 해보기

1. 손계산: teacher logits `[2, 1, −1]`의 T = 1, T = 2 softmax를 구하라. p(class 1)/p(class 2) 비율은 각각 얼마인가?
정답: T=1: e² = 7.389, e¹ = 2.718, e⁻¹ = 0.368, 합 10.475 → [0.705, 0.259, 0.035], 비율 e² = 7.39. T=2: e¹ = 2.718, e^0.5 = 1.649, e^−0.5 = 0.607, 합 4.973 → [0.547, 0.331, 0.122], 비율 e¹ = 2.72. (비율은 항상 exp((z₁ − z₂)/T)다.)

2. 손계산: 2.4절 예제에서 α = 0.5로 바꾸면 loss와 `∂L/∂z_s,0`은?
정답: L = 0.5·0.408 + 0.5·16·0.0798 ≈ 0.204 + 0.638 = 0.842. ∂L/∂z_s,0 = 0.5·(0.665 − 1) + 0.5·4·(0.419 − 0.528) ≈ −0.168 − 0.218 = −0.386.

3. 유도: class 수 K = 2(이진 분류)에서 T가 클 때 KD gradient가 `(Δz_s − Δz_t)/(4T²)`에 가까워짐을 보여라. 여기서 Δz = z₁ − z₀.
정답/힌트: 이진 softmax는 sigmoid(Δz/T)이고 σ(u) ≈ 1/2 + u/4 (u 작을 때). q₁ − p₁ ≈ (Δz_s − Δz_t)/(4T), 여기에 1/T를 곱한다.

4. 코드 과제: 예제 6에서 라벨 300개에는 hard 항을 섞고(α = 0.3), 무라벨 샘플에는 soft 항만 쓰도록 loss를 나눠 구현하라. 300 + 1,000에서 정확도가 달라지는가? seed 3개로 비교하라.
정답/힌트: batch마다 "라벨 있음" 마스크를 두고 CE는 마스크된 샘플에만 평균한다. 결과는 직접 확인할 것 — 이 과제에서는 teacher가 강해서 차이가 작을 가능성이 높다. 차이가 seed 표준편차보다 큰지부터 본다.

5. 코드 과제: 예제 10에서 `bits=8`, `bits=4`, `bits=3`의 held-out KL과 top-1 일치율을 표로 만들고, 8.5절처럼 norm 튜닝으로 각각 얼마나 회복되는지 비교하라.
정답/힌트: int8은 KL이 매우 작아(회복할 것이 거의 없음) 7.2절의 "self-KD는 손실이 클 때 의미가 있다"를 LLM에서 다시 확인하게 된다. 실행 시간은 bits 하나당 약 15초.

6. 설계 문제: 귀걸이형 기기에서 VAD → KWS(MCU) → ASR(SoC) → 클라우드 LLM cascade가 있다. KD를 적용할 수 있는 teacher–student 쌍을 세 개 적고, 각각 transfer set을 어디서 얻을지 한 줄씩 써라.
정답/힌트: (1) 서버 KWS/ASR → MCU KWS: 동의받은 dogfood 오디오. (2) 서버 대형 ASR → SoC ASR: pseudo-label 전사문(Distil-Whisper 방식). (3) 클라우드 LLM → 기기 SLM: escalate된 질의와 클라우드 응답(sequence-level) 또는 오픈 모델이면 logits.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| knowledge distillation (KD) | 지식 증류 | 큰 모델의 출력 분포를 작은 모델이 따라 하도록 학습 |
| teacher / student | 선생 / 학생 모델 | teacher는 동결된 큰 모델, student는 배포할 작은 모델 |
| soft label (soft target) | 부드러운 라벨 | teacher가 낸 class별 확률, 오답 확률도 0이 아님 |
| dark knowledge | 숨은 지식 | 오답 class들 사이의 상대적 확률 = class 간 유사도 정보 |
| temperature T | 온도 | logits를 T로 나눠 분포를 평평하게, argmax는 보존 |
| T² 보정 | — | 온도 때문에 1/T²로 줄어드는 soft gradient를 원래 크기로 되돌림 |
| α | 혼합 비율 | hard CE 항과 soft KL 항 사이의 가중치 |
| transfer set | 증류용 데이터 | student가 teacher를 따라 하는 입력들, 라벨 불필요 |
| hint / FitNets | 중간 feature 증류 | teacher 중간층 출력을 projection을 거쳐 맞춤 |
| teacher assistant | 중간 teacher | 크기 차이가 클 때 중간 크기 모델을 거쳐 두 단계로 증류 |
| self-distillation | 자기 증류 | 같은 구조(또는 float 버전)의 자신을 teacher로 씀 |
| token-level KD | — | LLM의 위치마다 vocab 분포를 KL로 맞춤 |
| sequence-level KD | — | teacher가 생성한 텍스트로 student를 SFT |
| on-policy KD | — | student가 생성한 텍스트에서 teacher 분포를 target으로 |
| forward / reverse KL | KL 방향 | `KL(p‖q)`는 mode-covering, `KL(q‖p)`는 mode-seeking |
| calibration | 확률 보정 | 모델 확률값이 실제 정답률과 맞는 정도, threshold 설계에 중요 |

---

## 16. 요약 & 체크리스트

knowledge distillation은 큰 teacher의 **확률분포 전체**를 작은 student에게 가르치는 학습 방법이다. soft label에는 오답 class 간 유사도(dark knowledge)가 들어 있고, 온도 T로 그것을 보이는 크기로 키운다. 온도 때문에 soft 항의 gradient가 1/T²로 줄어드므로 T²를 곱해 보정한다. 같은 소량 데이터로 학습한 teacher에서는 이득이 작지만(+1.1%p), **강한 teacher + 무라벨 기기 로그**를 transfer set으로 쓰면 작은 student가 teacher 수준까지 올라간다(+5.7%p) — edge에서 KD의 진짜 가치는 "teacher = 자동 라벨러"다. student가 너무 작으면 소용없고, student의 상한은 대개 teacher다. KD는 양자화·pruning과 잘 합쳐지며, LLM에서는 token-level(logits), sequence-level(생성 텍스트), on-policy 변형이 있고 float 자신을 teacher로 한 self-distillation은 양자화 손실 회복에 쓰인다. 배포물은 student 하나라 런타임 비용은 0이고, 진짜 비용은 데이터 파이프라인과 threshold 재보정에 있다.

- [ ] logits `[5, 3, 0]`의 T = 1, 4 softmax를 손으로 계산하고 dark knowledge가 무엇인지 설명할 수 있다
- [ ] KD loss `α·CE + (1−α)·T²·KL(p_T ‖ q_T)`를 샘플 하나에 대해 손으로 계산할 수 있다
- [ ] soft 항의 gradient `(q_T − p_T)/T`를 유도하고 T가 클 때 1/T²가 되는 이유를 말할 수 있다
- [ ] `F.kl_div`의 인자 순서, `batchmean`, teacher `eval()` 함정을 안다
- [ ] 같은 데이터 teacher와 강한 teacher에서 KD 이득이 왜 다른지 설명할 수 있다
- [ ] 무라벨 데이터를 transfer set으로 쓰는 파이프라인을 기기 로그 수집(H1–H3)과 연결해 설계할 수 있다
- [ ] student 용량이 너무 작을 때와 teacher–student 차이가 클 때의 대처를 말할 수 있다
- [ ] FitNets hint loss와 projection 층의 역할을 그림으로 설명할 수 있다
- [ ] LLM의 token-level / sequence-level / on-policy KD를 비용과 정보량으로 비교할 수 있다
- [ ] KD 후 threshold와 calibration을 다시 맞춰야 하는 이유를 펌웨어 관점에서 설명할 수 있다

---

## 참고 자료

- Hinton, Vinyals, Dean, "Distilling the Knowledge in a Neural Network", 2015 — [arXiv:1503.02531](https://arxiv.org/abs/1503.02531)
- Ba, Caruana, "Do Deep Nets Really Need to be Deep?", NeurIPS 2014 — [arXiv:1312.6184](https://arxiv.org/abs/1312.6184)
- Romero et al., "FitNets: Hints for Thin Deep Nets", ICLR 2015 — [arXiv:1412.6550](https://arxiv.org/abs/1412.6550)
- Zagoruyko, Komodakis, "Paying More Attention to Attention", ICLR 2017 — [arXiv:1612.03928](https://arxiv.org/abs/1612.03928)
- Kim, Rush, "Sequence-Level Knowledge Distillation", EMNLP 2016 — [arXiv:1606.07947](https://arxiv.org/abs/1606.07947)
- Furlanello et al., "Born-Again Neural Networks", ICML 2018
- Cho, Hariharan, "On the Efficacy of Knowledge Distillation", ICCV 2019
- Müller, Kornblith, Hinton, "When Does Label Smoothing Help?", NeurIPS 2019
- Mirzadeh et al., "Improved Knowledge Distillation via Teacher Assistant", AAAI 2020 — [arXiv:1902.03393](https://arxiv.org/abs/1902.03393)
- Beyer et al., "Knowledge Distillation: A Good Teacher is Patient and Consistent", CVPR 2022 — [arXiv:2106.05237](https://arxiv.org/abs/2106.05237)
- Sanh et al., "DistilBERT, a distilled version of BERT", 2019 — [arXiv:1910.01108](https://arxiv.org/abs/1910.01108)
- Agarwal et al., "On-Policy Distillation of Language Models: Learning from Self-Generated Mistakes" (GKD), ICLR 2024 — [arXiv:2306.13649](https://arxiv.org/abs/2306.13649)
- Gu et al., "MiniLLM: Knowledge Distillation of Large Language Models", ICLR 2024 — [arXiv:2306.08543](https://arxiv.org/abs/2306.08543)
- Liu et al., "LLM-QAT: Data-Free Quantization Aware Training for Large Language Models", 2023 — [arXiv:2305.17888](https://arxiv.org/abs/2305.17888)
- Gandhi et al., "Distil-Whisper: Robust Knowledge Distillation via Large-Scale Pseudo Labelling", 2023 — [arXiv:2311.00430](https://arxiv.org/abs/2311.00430)
- Gemma Team, "Gemma 2: Improving Open Language Models at a Practical Size", 2024 — [arXiv:2408.00118](https://arxiv.org/abs/2408.00118)
- Muralidharan et al., "Compact Language Models via Pruning and Knowledge Distillation" (Minitron), 2024 — [arXiv:2407.14679](https://arxiv.org/abs/2407.14679)
- PyTorch 문서 — [torch.nn.KLDivLoss](https://pytorch.org/docs/stable/generated/torch.nn.KLDivLoss.html) (인자 순서와 `batchmean`)
- MIT 6.5940 TinyML and Efficient Deep Learning (Song Han) — knowledge distillation 강의
- 이 시리즈: A2 (softmax·KL·온도), A3 (gradient), B5 (KWS·ASR), B7 (IMU), B8 (SLM), B9 (cascade), C1–C3 (양자화), C8 (검증), H1–H6 (데이터 파이프라인), L3 (hybrid 라우팅)
