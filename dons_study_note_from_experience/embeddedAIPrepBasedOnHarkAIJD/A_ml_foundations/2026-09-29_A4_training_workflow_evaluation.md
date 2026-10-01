# A4. 학습 워크플로와 모델 평가 — 데이터 분할부터 지표까지

> **이 노트를 다 읽으면**: 데이터를 train/val/test로 새는 곳 없이 나눌 수 있다 · overfitting을 loss 곡선에서 읽고 regularization으로 다룰 수 있다 · confusion matrix에서 precision/recall/F1/ROC-AUC를 손으로 계산할 수 있다 · wake word의 "하루 1회 이하 오작동" 같은 요구를 임계값으로 바꿀 수 있다
> **JD 연결**: "5+ years of ML engineering experience", "Build data collection and ingestion pipelines … various sensors" — study_prep_list A4: train/val/test split, overfitting, regularization, data augmentation, metrics(precision/recall, F1, ROC-AUC, confusion matrix, FAR/FRR), 시계열 data leakage
> **Don 기준 난이도**: 검증 계획·golden vector·통계적 판정(양산 테스트 margin)은 이미 강하다 / ML 용어(overfitting, regularization, AUC)와 "데이터를 어떻게 나누느냐가 숫자를 바꾼다"는 감각은 새로 배운다
> **선행 노트**: A0, A1, A2, A3

---

## 0. 큰 그림 — 이게 왜 필요한가

A3에서 모델을 **학습**시키는 법(loss를 줄이는 gradient descent)을 봤다. 그런데 loss가 줄었다고 해서 모델이 **좋은** 것은 아니다. 학습 데이터를 외워 버렸을 수도 있고, 평가 데이터가 학습 데이터와 사실상 같은 것이었을 수도 있다. 이 노트는 "이 모델이 **실제 기기에서, 처음 보는 사용자에게** 얼마나 잘 될까?"라는 질문에 정직하게 답하는 절차를 다룬다.

edge ML 엔지니어에게 이게 특히 중요한 이유는 세 가지다.

- 모델팀이 "validation accuracy 98%"라고 넘겨준 숫자를 **의심하고 검증**할 수 있어야 한다. 센서 데이터에서는 이 숫자가 부풀려지는 경우가 매우 흔하다(3절 data leakage).
- 양자화·pruning·다른 런타임으로 옮기면 정확도가 변한다. **같은 test set으로 다시 재는 것**은 배포 담당자의 몫이다(11절).
- wake word나 착용 감지 같은 always-on 기능은 accuracy가 아니라 "**하루에 몇 번 잘못 켜지나**"로 평가된다. 이것은 전력·UX와 직결되는 제품 스펙이다(9절).

전체 흐름을 블록도로 보면 이렇다.

```
 문제 정의 ──► 데이터 수집 ──► 분할 (train / val / test) ──► 베이스라인
     │                            │                           │
     │                            ▼                           ▼
     │                  ┌──── 학습 루프 (A3) ◄──── 하이퍼파라미터 튜닝 (val만 사용)
     │                  │        │
     │                  │        ▼
     │                  │   loss 곡선 확인 → overfitting? → regularization / augmentation
     │                  │        │
     │                  └────────┘
     ▼                           ▼
 지표 선택 ◄──────────── 최종 모델 1개 선택 ──► test set으로 딱 한 번 평가
 (precision/recall,                               │
  FAR/FRR, MAE …)                                 ▼
                                        양자화·배포 → 같은 test set으로 재평가 (C, K)
```

펌웨어로 비유하면: **train set = 개발 중에 계속 돌리는 unit test**, **validation set = 튜닝할 때 보는 bench 측정**, **test set = 봉인해 둔 golden test vector / 양산 qualification**이다. qualification 벡터를 보면서 펌웨어 파라미터를 튜닝하면 qualification이 의미를 잃는다. ML도 똑같다.

---

## 1. 문제 정의와 베이스라인 — 모델보다 먼저 할 일

### 1.1 무엇을 예측할지 정한다

모델을 만들기 전에 네 가지를 문장으로 적는다. Hark 같은 웨어러블에서 "손목 들기(wrist raise)로 화면 켜기"를 예로 들자 (제품 기능은 가정이다).

| 항목 | 질문 | 예시 답 |
|---|---|---|
| 출력(label) | 모델이 무엇을 말해야 하나? | 이 1초 윈도우가 "손목 들기"인가 아닌가 (이진 분류) |
| 입력(input window) | 무엇을 보고 판단하나? | 가속도 3축 + 자이로 3축, 100 Hz × 1 s = `[100, 6]` 텐서 |
| 판정 주기 | 얼마나 자주 판단하나? | 0.25 s마다 (hop 25 샘플, 75% overlap) |
| 성공 기준 | 무엇이 "좋다"인가? | 손목 들기 90% 이상 잡기, 오작동 시간당 1회 이하, MCU에서 5 ms 이내 |

여기서 새 용어 두 개. **윈도우(window)**는 연속 신호에서 잘라 낸 고정 길이 구간이다(링버퍼에서 N개 샘플을 꺼내는 것과 같다). **hop**은 다음 윈도우까지 건너뛰는 샘플 수다. hop이 윈도우 길이보다 작으면 윈도우끼리 겹친다(**overlap**). 이 overlap이 3절에서 data leakage의 원인이 된다.

### 1.2 베이스라인 — "항상 이것부터 이겨라"

**베이스라인(baseline)**은 ML 없이, 혹은 가장 멍청한 방법으로 얻는 성능이다. 두 가지를 꼭 만든다.

- **다수 클래스(majority class)**: 항상 가장 흔한 답을 내는 모델. 데이터의 90%가 "아님"이면 이 모델의 accuracy는 90%다.
- **간단한 규칙(threshold rule)**: 펌웨어 엔지니어가 10분 만에 짤 수 있는 if 문. 예: "pitch 변화가 25도 넘으면 손목 들기".

ML 모델이 이 둘을 **분명히** 이기지 못하면 모델을 쓸 이유가 없다. 규칙 기반은 메모리 몇 바이트, 연산 몇 사이클이고, 디버깅도 쉽다. 이 비교는 면접에서도 좋은 인상을 준다 — "모델을 넣기 전에 규칙 기반 베이스라인과 비교했다"는 말은 엔지니어링 판단력을 보여 준다.

다음 코드는 가상의 손목 들기 데이터에서 두 베이스라인의 accuracy와 recall(실제 양성 중 잡아낸 비율, 7절에서 정확히 정의)을 비교한다.

```python
import numpy as np
rng = np.random.default_rng(0)
# 가상의 "손목 들기(wrist raise)" 데이터: 윈도우 1000개, 양성 10%
n = 1000
y = (rng.random(n) < 0.10).astype(int)
# 특징 1개: 윈도우 안 pitch 변화량(도). 양성이면 평균 40, 음성이면 평균 10
pitch = np.where(y == 1, rng.normal(40, 12, n), rng.normal(10, 10, n))

# 베이스라인 1: 다수 클래스 — 항상 "아님(0)"
pred_major = np.zeros(n, dtype=int)
# 베이스라인 2: 임계값 규칙 — pitch 변화 > 25도면 "손목 들기"
pred_rule = (pitch > 25).astype(int)

for name, p in [("majority", pred_major), ("threshold", pred_rule)]:
    acc = (p == y).mean()
    tp = ((p == 1) & (y == 1)).sum()
    recall = tp / (y == 1).sum()
    print(f"{name:9s}  accuracy={acc:.3f}  recall(양성 잡은 비율)={recall:.3f}")
```

```text
majority   accuracy=0.911  recall(양성 잡은 비율)=0.000
threshold  accuracy=0.934  recall(양성 잡은 비율)=0.888
```

출력에서 볼 것: 아무것도 안 하는 모델이 accuracy 91.1%다. 즉 이 문제에서 "accuracy 92%" 모델은 사실상 쓸모가 없다. 그리고 if 문 하나가 이미 양성의 88.8%를 잡는다. ML 모델의 목표선은 0.5가 아니라 **이 두 줄**이다.

> 흔한 함정: 베이스라인 없이 "accuracy 95%!"를 보고하면, 듣는 사람은 그게 좋은지 나쁜지 판단할 수 없다. 숫자는 항상 베이스라인과 **나란히** 보여 준다.

---

## 2. Train / Validation / Test — 세 조각의 역할

### 2.1 직관

시험 공부에 비유하면 이렇다.

- **train set**: 교과서 연습문제. 모델은 이걸 보고 weight를 바꾼다.
- **validation set (val, dev set)**: 모의고사. 공부 방법(모델 크기, learning rate, 몇 epoch 할지)을 고를 때 본다. 모델은 이걸로 **직접 학습하지는 않지만**, 우리가 이 점수를 보고 선택을 하므로 간접적으로 정보가 샌다.
- **test set**: 수능 본시험. 모든 결정이 끝난 뒤 **딱 한 번** 본다. 결과가 나빠도 다시 튜닝하러 돌아가면 그 test set은 오염된다.

펌웨어식으로 말하면 test set은 **봉인된 golden test vector 세트**다. 개발자가 그 벡터를 보면서 코드를 고치기 시작하면, 벡터를 통과하는 것은 증명력이 없어진다. 그래서 양산 qualification 벡터는 따로 관리한다. ML에서도 test set은 파일 권한을 따로 두거나 해시로 버전을 고정해 두는 팀이 많다.

### 2.2 정의와 흔한 비율

| 조각 | 누가 쓰나 | 무엇을 결정하나 | 흔한 비율 |
|---|---|---|---|
| train | optimizer (gradient 계산) | weight 값 | 60–80% |
| validation | 엔지니어 (선택) | 하이퍼파라미터, epoch 수, 모델 구조, 임계값 | 10–20% |
| test | 최종 보고 | 아무것도 결정하지 않음 — 측정만 | 10–20% |

**하이퍼파라미터(hyperparameter)**는 학습으로 정해지지 않고 사람이 정하는 설정값이다(learning rate, 레이어 수, dropout 비율 등). 펌웨어의 컴파일 옵션이나 튜닝 레지스터 값과 비슷하다.

### 2.3 손으로 해 보기

사용자 50명의 IMU 로그가 있고 사용자당 평균 200개 윈도우가 있다고 하자. 총 10,000개 윈도우다. 70/15/15로 나누면:

```
사용자 단위로 나눈다 (3절 이유):
  train = 35명 ≈ 7,000 윈도우
  val   =  7~8명 ≈ 1,500 윈도우
  test  =  7~8명 ≈ 1,500 윈도우
```

말로 하면: 비율은 윈도우 수가 아니라 **사람 수**로 맞추고, 윈도우 수는 사람마다 다르니 대략 맞으면 된다.

### 2.4 함정

- test set이 너무 작으면 숫자가 요동친다. test에 양성이 20개뿐이면 recall은 5% 단위로만 움직인다(1개 = 1/20). 양성 개수를 먼저 세어 본다.
- 시간 순서가 중요한 문제(예: 기기 노화, 펌웨어 버전 변화)라면 **시간으로 나눈다**: 과거 데이터로 학습, 최신 데이터로 테스트. 미래 데이터가 학습에 들어가면 안 된다.

---

## 3. Data leakage — 센서 데이터에서 가장 흔한 사고

### 3.1 직관

**Data leakage(데이터 누수)**는 test(또는 val) 데이터의 정보가 학습에 새어 들어가서 평가 점수가 실제보다 좋게 나오는 현상이다. 센서·시계열 데이터에서는 거의 항상 같은 모양으로 일어난다.

1. **겹치는 윈도우**: hop이 윈도우보다 작으면 인접 윈도우는 샘플의 절반 이상을 공유한다. 윈도우를 랜덤으로 섞어 나누면, test 윈도우 바로 옆의 "거의 같은" 윈도우가 train에 있다. 모델은 그걸 외우기만 해도 정답을 맞힌다.
2. **같은 사용자·같은 기기·같은 세션**: 사람마다 착용 위치, 팔 길이, 걷는 리듬이 다르고, 기기마다 센서 offset·gain이 다르다. 같은 사람이 train과 test에 모두 있으면 모델은 "이 사람의 지문"을 외워서 맞힌다. 현장에서는 **처음 보는 사람**이 쓴다.

펌웨어 비유: 칩 한 개(같은 lot, 같은 보드)로 튜닝하고 **같은 칩**으로 검증한 것과 같다. 다른 lot의 칩이 들어오면 margin이 사라진다. 그래서 양산 검증은 여러 lot·여러 보드로 한다. ML에서는 여러 **사용자·기기**가 lot에 해당한다.

```svg
<svg viewBox="0 0 640 330" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="24" font-size="14">(a) 윈도우 단위 랜덤 split — 같은 사용자가 train/test 양쪽에 있다</text> <text x="20" y="52" font-size="13">사용자 A</text> <text x="20" y="82" font-size="13">사용자 B</text> <text x="20" y="112" font-size="13">사용자 C</text>
<g stroke="currentColor" stroke-width="0.5">
<rect x="100" y="40" width="36" height="18" fill="#4a7bd0"/><rect x="140" y="40" width="36" height="18" fill="#4a7bd0"/><rect x="180" y="40" width="36" height="18" fill="#e08a3c"/><rect x="220" y="40" width="36" height="18" fill="#4a7bd0"/><rect x="260" y="40" width="36" height="18" fill="#4a7bd0"/><rect x="300" y="40" width="36" height="18" fill="#4a7bd0"/><rect x="340" y="40" width="36" height="18" fill="#4a7bd0"/><rect x="380" y="40" width="36" height="18" fill="#e08a3c"/><rect x="420" y="40" width="36" height="18" fill="#4a7bd0"/><rect x="460" y="40" width="36" height="18" fill="#4a7bd0"/><rect x="500" y="40" width="36" height="18" fill="#4a7bd0"/><rect x="540" y="40" width="36" height="18" fill="#4a7bd0"/>
<rect x="100" y="70" width="36" height="18" fill="#4a7bd0"/><rect x="140" y="70" width="36" height="18" fill="#4a7bd0"/><rect x="180" y="70" width="36" height="18" fill="#4a7bd0"/><rect x="220" y="70" width="36" height="18" fill="#4a7bd0"/><rect x="260" y="70" width="36" height="18" fill="#e08a3c"/><rect x="300" y="70" width="36" height="18" fill="#4a7bd0"/><rect x="340" y="70" width="36" height="18" fill="#4a7bd0"/><rect x="380" y="70" width="36" height="18" fill="#4a7bd0"/><rect x="420" y="70" width="36" height="18" fill="#4a7bd0"/><rect x="460" y="70" width="36" height="18" fill="#e08a3c"/><rect x="500" y="70" width="36" height="18" fill="#e08a3c"/><rect x="540" y="70" width="36" height="18" fill="#4a7bd0"/>
<rect x="100" y="100" width="36" height="18" fill="#4a7bd0"/><rect x="140" y="100" width="36" height="18" fill="#e08a3c"/><rect x="180" y="100" width="36" height="18" fill="#4a7bd0"/><rect x="220" y="100" width="36" height="18" fill="#4a7bd0"/><rect x="260" y="100" width="36" height="18" fill="#4a7bd0"/><rect x="300" y="100" width="36" height="18" fill="#4a7bd0"/><rect x="340" y="100" width="36" height="18" fill="#e08a3c"/><rect x="380" y="100" width="36" height="18" fill="#4a7bd0"/><rect x="420" y="100" width="36" height="18" fill="#4a7bd0"/><rect x="460" y="100" width="36" height="18" fill="#4a7bd0"/><rect x="500" y="100" width="36" height="18" fill="#4a7bd0"/><rect x="540" y="100" width="36" height="18" fill="#4a7bd0"/>
</g> <path d="M 160 36 L 160 30 L 216 30 L 216 36" fill="none" stroke="#d0564a" stroke-width="1.5"/> <text x="222" y="33" font-size="12">인접 윈도우끼리 샘플 50% 공유</text> <text x="20" y="162" font-size="14">(b) 사용자 단위 split — test 사용자는 학습 때 한 번도 안 보인다</text>
<text x="20" y="190" font-size="13">사용자 A</text> <text x="20" y="220" font-size="13">사용자 B</text> <text x="20" y="250" font-size="13">사용자 C</text> <text x="20" y="280" font-size="13">사용자 D</text>
<g stroke="currentColor" stroke-width="0.5"> <rect x="100" y="178" width="476" height="18" fill="#4a7bd0"/> <rect x="100" y="208" width="476" height="18" fill="#4a7bd0"/> <rect x="100" y="238" width="476" height="18" fill="#4a7bd0"/>
<rect x="100" y="268" width="476" height="18" fill="#e08a3c"/> </g> <line x1="90" y1="262" x2="590" y2="262" stroke="currentColor" stroke-dasharray="5 4"/> <rect x="100" y="304" width="14" height="12" fill="#4a7bd0"/>
<text x="120" y="314" font-size="12">train 윈도우</text> <rect x="220" y="304" width="14" height="12" fill="#e08a3c"/> <text x="240" y="314" font-size="12">test 윈도우</text> <text x="340" y="314" font-size="12">예제 결과: (a) 94.6%  vs  (b) 48.9%</text>
</svg>
```

그림 1 — 같은 데이터라도 (a)처럼 윈도우를 섞어서 나누면 test 윈도우의 "쌍둥이"가 train에 있다. (b)처럼 사람 단위로 자르면 test는 처음 보는 사람이 되고, 이것이 현장 성능에 가깝다.

### 3.2 코드로 확인 — 사용자별 offset이 있는 합성 실험

다음 코드는 사용자 20명의 3축 신호를 만든다. 사용자마다 고유한 3축 offset(착용 위치·센서 편차)이 있고, 클래스(정지/걷기)는 z축 평균을 0.7만큼 올리는 약한 신호만 준다. 윈도우 50샘플, hop 25(50% overlap)로 자르고, 특징은 축별 평균·표준편차 6개다. 분류기는 가장 단순한 **1-NN**(가장 가까운 학습 샘플의 라벨을 그대로 쓰는 방법)을 쓴다 — 1-NN은 "외우기"만 하는 모델이라 누수를 가장 잘 드러낸다.

```python
import numpy as np
rng = np.random.default_rng(42)
U, N, WIN, HOP = 20, 600, 50, 25        # 사용자 20명, 세션당 600샘플, 윈도우 50, hop 25 (50% overlap)
X, y, user = [], [], []
for u in range(U):
    bias = rng.normal(0, 1.0, 3)        # 사용자별 3축 오프셋 (착용 위치·기울기·센서 편차)
    for c in (0, 1):                    # 0 = 정지, 1 = 걷기 (사용자당 클래스별 세션 1개)
        drift = np.cumsum(rng.normal(0, 0.02, (N, 3)), axis=0)  # 세션 고유의 느린 변화
        sig = bias + drift + rng.normal(0, 0.6, (N, 3))
        sig[:, 2] += 0.7 * c            # 걷기면 z축 평균이 조금 오른다 (진짜 신호)
        for s in range(0, N - WIN + 1, HOP):
            w = sig[s:s + WIN]
            X.append(np.r_[w.mean(0), w.std(0)]); y.append(c); user.append(u)
X, y, user = np.array(X), np.array(y), np.array(user)

def knn1_acc(tr, te):                   # 1-NN: 가장 가까운 학습 윈도우의 라벨을 그대로 쓴다
    d = ((X[te, None, :] - X[None, tr, :]) ** 2).sum(-1)
    return (y[tr][d.argmin(1)] == y[te]).mean()

idx = rng.permutation(len(y)); cut = int(0.8 * len(y))
acc_random = knn1_acc(idx[:cut], idx[cut:])          # (a) 윈도우 단위 랜덤 80/20
test_users = rng.permutation(U)[:4]                  # (b) 사용자 단위: 16명 학습 / 4명 테스트
te = np.isin(user, test_users)
acc_user = knn1_acc(np.where(~te)[0], np.where(te)[0])
print("windows:", len(y), " users:", U, " test users:", sorted(test_users.tolist()))
print(f"(a) random window split accuracy = {acc_random:.3f}")
print(f"(b) user-wise split accuracy     = {acc_user:.3f}")
```

```text
windows: 920  users: 20  test users: [5, 11, 15, 19]
(a) random window split accuracy = 0.946
(b) user-wise split accuracy     = 0.489
```

출력에서 볼 것: **같은 데이터, 같은 모델**인데 나누는 방법만 바꿨더니 94.6% → 48.9%(이진 분류에서 거의 동전 던지기)다. (a)의 94.6%는 "이 사람의 이 세션을 알아보는 능력"을 잰 것이고, (b)가 "처음 보는 사람에게서 걷기를 알아보는 능력"이다. 제품이 필요한 건 (b)다.

(b)가 이렇게 낮은 이유는 사용자 offset(표준편차 1.0)이 클래스 신호(0.7)보다 크기 때문이다. 해결책은 split을 속이는 게 아니라 모델·특징을 고치는 것이다: 사용자별 정규화(기기에서 running mean 빼기), offset에 둔감한 특징(예: 가속도 크기 `|a|`, 고역 통과 필터 후 에너지), 더 많은 사용자 수집. 이 판단은 (b)로 평가했을 때만 할 수 있다.

### 3.3 누수의 다른 얼굴들

| 누수 경로 | 예 | 막는 법 |
|---|---|---|
| overlap 윈도우 | hop이 윈도우보다 짧은데 랜덤 split | 세션·사용자 단위로 나눈 **뒤에** 윈도우를 자른다 |
| 같은 사용자/기기 | 한 사람이 train·test에 모두 | group split (user_id, device_id 기준) |
| 정규화 통계 | 전체 데이터로 mean/std 계산 후 split | mean/std는 **train에서만** 계산, val/test엔 그 값 적용 |
| 중복 녹음 | 같은 wake word 녹음이 파일명만 다르게 두 번 | 해시로 중복 제거 |
| 라벨에서 새는 특징 | 녹음 파일 길이가 클래스마다 다름 → 길이만으로 맞힘 | 특징 목록을 사람이 검토 |
| 미래 정보 | 이벤트 뒤의 샘플까지 윈도우에 포함 | 기기에서 실제로 쓸 수 있는 과거 샘플만 사용 (causal) |

> 흔한 함정: "사용자별로 나눴는데도 점수가 너무 좋다" → 같은 **사람**이 다른 user_id로 두 번 등록됐거나(기기 교체), 같은 방·같은 날 녹음이라 환경 소음이 지문 역할을 하는 경우가 있다. 이상하게 좋은 숫자는 버그로 의심한다 — 펌웨어에서 "너무 빠른 benchmark"를 의심하는 것과 같다.

---

## 4. Underfitting, Overfitting, 그리고 learning curve

### 4.1 직관

- **Underfitting(과소적합)**: 모델이 너무 단순해서 학습 데이터조차 못 맞힌다. train 오차도 크고 val 오차도 크다. 곡선을 직선으로 맞추려는 상황.
- **Overfitting(과적합)**: 모델이 학습 데이터의 **노이즈까지 외워서**, 학습 데이터에서는 완벽한데 새 데이터에서는 틀린다. train 오차는 작고 val 오차는 크다.

펌웨어 비유: 특정 테스트 벡터 10개만 통과하도록 threshold·타이밍을 미세하게 튜닝하면 그 10개는 100% 통과하지만, 다른 칩·다른 온도에서는 실패한다. 그게 overfitting이다. 반대로 모든 칩에 너무 보수적인 고정값을 쓰면 성능이 안 나오는데, 그게 underfitting에 가깝다.

### 4.2 Bias–variance 직관

val 오차는 대략 세 가지의 합으로 생각할 수 있다.

```
기대 오차 ≈ bias² + variance + 줄일 수 없는 노이즈
```

말로 하면: **bias**는 "모델이 원래 표현할 수 없는 부분" 때문에 생기는 체계적 오차(직선으로 사인파를 맞추면 생기는 오차), **variance**는 "학습 데이터가 조금만 바뀌어도 모델이 크게 흔들리는 정도", **노이즈**는 센서 노이즈처럼 어떤 모델로도 못 없애는 부분이다. 모델을 키우면 bias는 줄고 variance는 는다. 가장 좋은 지점은 그 사이에 있다.

### 4.3 코드로 확인 — 다항식 차수 1 / 3 / 15

진짜 관계가 `y = sin(2πx)`이고 노이즈(표준편차 0.2)가 섞인 학습 점 20개가 있다. 차수만 바꿔 가며 최소제곱으로 다항식을 맞추고 train/val RMSE(오차 제곱 평균의 제곱근, 10절)를 비교한다. `np.polynomial.Polynomial.fit`은 `np.polyfit`과 같은 최소제곱 적합인데 내부에서 x를 스케일링해서 수치적으로 더 안정적인, numpy가 권장하는 API다.

```python
import numpy as np
rng = np.random.default_rng(1)
f = lambda x: np.sin(2 * np.pi * x)            # 숨어 있는 "진짜" 관계
x_tr = np.sort(rng.random(20)); y_tr = f(x_tr) + rng.normal(0, 0.2, 20)   # 학습 20점
x_va = np.linspace(x_tr.min(), x_tr.max(), 200); y_va = f(x_va) + rng.normal(0, 0.2, 200) # 검증 200점 (학습 범위 안)

rmse = lambda a, b: np.sqrt(np.mean((a - b) ** 2))
for deg in (1, 3, 15):
    p = np.polynomial.Polynomial.fit(x_tr, y_tr, deg)   # 최소제곱 다항식 적합
    print(f"degree {deg:2d}: train RMSE = {rmse(p(x_tr), y_tr):.3f}   "
          f"val RMSE = {rmse(p(x_va), y_va):.3f}")
```

```text
degree  1: train RMSE = 0.424   val RMSE = 0.462
degree  3: train RMSE = 0.219   val RMSE = 0.198
degree 15: train RMSE = 0.101   val RMSE = 44.758
```

출력에서 볼 것:

- 차수 1: train·val 모두 0.4대 → **underfitting** (높은 bias).
- 차수 3: 둘 다 노이즈 수준(0.2) 근처 → 적당하다.
- 차수 15: train은 0.101로 노이즈(0.2)보다도 작다 — 노이즈까지 외웠다는 뜻이다. val은 44.8로 폭발 → **overfitting** (높은 variance). 16개 계수로 20개 점을 억지로 지나가려다 점 사이에서 크게 출렁인다.

핵심 규칙: **train 오차가 노이즈 수준보다 작으면 의심하라.** 측정 노이즈보다 정밀한 교정은 노이즈를 교정한 것이다.

### 4.4 Learning curve — epoch에 따른 train/val loss

신경망에서는 차수 대신 **학습 시간(epoch)**을 따라 같은 현상이 보인다. **epoch**은 학습 데이터 전체를 한 바퀴 도는 단위다. 아래 그림은 5.3절 예제(80개 점, 라벨 15% 노이즈, 은닉층 128×2 MLP)의 실제 loss 기록이다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="250" x2="620" y2="250" stroke="currentColor"/> <line x1="60" y1="30" x2="60" y2="250" stroke="currentColor"/> <g stroke="currentColor" stroke-opacity="0.25"> <line x1="60" y1="176.7" x2="620" y2="176.7"/><line x1="60" y1="103.3" x2="620" y2="103.3"/><line x1="60" y1="30" x2="620" y2="30"/>
</g> <text x="50" y="254" font-size="12" text-anchor="end">0.0</text> <text x="50" y="181" font-size="12" text-anchor="end">0.4</text> <text x="50" y="107" font-size="12" text-anchor="end">0.8</text>
<text x="50" y="34" font-size="12" text-anchor="end">1.2</text> <text x="60" y="268" font-size="12" text-anchor="middle">0</text> <text x="200.4" y="268" font-size="12" text-anchor="middle">100</text> <text x="340.8" y="268" font-size="12" text-anchor="middle">200</text>
<text x="481.2" y="268" font-size="12" text-anchor="middle">300</text> <text x="620" y="268" font-size="12" text-anchor="middle">399</text> <text x="340" y="290" font-size="13" text-anchor="middle">epoch</text> <text x="18" y="140" font-size="13" transform="rotate(-90 18 140)" text-anchor="middle">cross-entropy loss</text>
<polyline fill="none" stroke="#4a7bd0" stroke-width="2" points="60.0,124.7 71.2,136.4 82.5,145.0 93.7,154.8 104.9,164.2 116.1,171.5 127.4,176.0 138.6,178.6 149.8,180.4 161.1,181.8 172.3,183.0 183.5,184.1 194.7,185.0 206.0,185.9 217.2,186.9 228.4,187.7 239.6,188.6 250.9,189.4 262.1,190.3 273.3,191.2 284.6,192.2 295.8,193.4 307.0,194.5 318.2,195.8 329.5,197.0 340.7,198.4 351.9,199.8 363.2,201.2 374.4,202.7 385.6,204.2 396.8,205.7 408.1,207.2 419.3,208.7 430.5,210.1 441.8,211.6 453.0,213.0 464.2,214.3 475.4,215.5 486.7,217.0 497.9,218.1 509.1,219.2 520.4,220.2 531.6,221.2 542.8,222.2 554.0,223.1 565.3,223.9 576.5,224.7 587.7,225.5 598.9,226.3 610.2,227.1 620.0,227.7"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="2" points="60.0,127.1 71.2,130.2 82.5,136.4 93.7,145.6 104.9,152.6 116.1,156.3 127.4,156.8 138.6,155.0 149.8,152.6 161.1,150.5 172.3,148.8 183.5,147.3 194.7,146.1 206.0,144.9 217.2,143.9 228.4,143.0 239.6,141.5 250.9,140.1 262.1,138.7 273.3,137.3 284.6,135.8 295.8,134.3 307.0,132.8 318.2,131.0 329.5,129.2 340.7,127.5 351.9,125.4 363.2,123.4 374.4,121.2 385.6,118.6 396.8,115.8 408.1,112.8 419.3,110.1 430.5,107.1 441.8,103.9 453.0,100.8 464.2,97.6 475.4,93.7 486.7,90.9 497.9,87.8 509.1,84.3 520.4,80.8 531.6,77.2 542.8,73.6 554.0,70.0 565.3,66.2 576.5,62.3 587.7,58.8 598.9,55.1 610.2,52.0 620.0,48.6"/>
<line x1="123.2" y1="30" x2="123.2" y2="250" stroke="#3f9a6b" stroke-width="1.5" stroke-dasharray="6 4"/> <circle cx="123.2" cy="156.9" r="5" fill="#3f9a6b"/> <text x="130" y="46" font-size="12">early stopping: epoch 45</text> <text x="130" y="62" font-size="12">val loss 최소 0.508</text>
<text x="470" y="44" font-size="12">val loss 1.098</text> <text x="470" y="238" font-size="12">train loss 0.122</text> <line x1="400" y1="282" x2="424" y2="282" stroke="#4a7bd0" stroke-width="2"/> <text x="430" y="286" font-size="12">train</text>
<line x1="480" y1="282" x2="504" y2="282" stroke="#e08a3c" stroke-width="2"/> <text x="510" y="286" font-size="12">validation</text>
</svg>
```

그림 2 — train loss(파랑)는 끝까지 내려가지만 val loss(주황)는 epoch 45에서 바닥을 찍고 다시 오른다. 그 뒤로는 모델이 라벨 노이즈를 외우는 중이다. 초록 점선이 early stopping이 고를 지점이다.

곡선 읽는 법을 표로 정리한다.

| 모양 | 진단 | 다음 행동 |
|---|---|---|
| train↓ val↓ 둘 다 아직 내려가는 중 | 덜 학습됨 | epoch 늘리기, learning rate 확인 |
| train·val 둘 다 높은 곳에서 평평 | underfitting | 모델 키우기, 특징 개선, regularization 줄이기 |
| train↓ 계속, val은 U자로 반등 | overfitting | early stopping, regularization, 데이터·augmentation 늘리기 |
| val이 train보다 계속 **낮음** | dropout·augmentation이 train에만 걸림, 혹은 val이 쉬움/누수 | 정상일 수도 있지만 split 점검 |
| val이 epoch마다 크게 튐 | val set이 너무 작음, lr 과대 | val 늘리기, lr 줄이기 |

---

## 5. Regularization — overfitting을 누르는 도구들

**Regularization(정규화, 규제)**은 모델이 "너무 자유롭게" 데이터를 외우지 못하게 제약을 거는 모든 기법이다. (B1의 BatchNorm·LayerNorm의 "normalization"과는 다른 말이다.)

### 5.1 L2 regularization / weight decay

loss에 weight 크기의 벌점을 더한다.

```
L_total(w) = L_data(w) + λ · ‖w‖²       (‖w‖² = w₁² + w₂² + … )
```

말로 하면: "데이터를 잘 맞히되, weight를 크게 쓰는 건 비싸게 치르게 한다." λ(lambda)가 벌점 세기다. 차수 15 다항식이 20개 점을 억지로 지나가려면 계수가 수십만 단위로 커져야 하는데, 이 벌점이 그걸 막는다.

gradient descent에서 벌점 항의 미분은 `2λw`이므로 매 step마다 weight가 조금씩 0쪽으로 줄어든다. 그래서 **weight decay**라고도 부른다. (Adam 같은 적응형 optimizer에서는 "loss에 L2 더하기"와 "weight를 직접 줄이기"가 결과가 달라서, PyTorch의 `AdamW`는 후자 — decoupled weight decay — 를 쓴다.)

펌웨어 비유: 제어 루프에서 gain을 너무 크게 잡으면 측정 노이즈에 과민 반응하며 흔들린다. L2는 "gain이 크면 비용"을 걸어서 부드러운 해를 고르게 하는 것과 비슷하다.

다음 코드는 4.3절의 차수 15 다항식에 ridge(L2) 벌점을 λ만 바꿔 가며 건다. `[A; √λ·I]`로 행을 덧붙여 최소제곱을 풀면 `‖Aw − y‖² + λ‖w‖²`를 정확히 최소화한다(정규방정식 `AᵀA`를 직접 만드는 것보다 수치적으로 안정적이다).

```python
import numpy as np
rng = np.random.default_rng(1)                 # 예제 3과 같은 데이터
f = lambda x: np.sin(2 * np.pi * x)
x_tr = np.sort(rng.random(20)); y_tr = f(x_tr) + rng.normal(0, 0.2, 20)
x_va = np.linspace(x_tr.min(), x_tr.max(), 200); y_va = f(x_va) + rng.normal(0, 0.2, 200)

V = lambda x: np.vander(2 * x - 1, 16, increasing=True)   # 15차 다항식 특징 [1, t, t², …, t¹⁵]
rmse = lambda a, b: np.sqrt(np.mean((a - b) ** 2))
for lam in (0.0, 1e-6, 1e-3, 1e-1, 10.0):
    A = V(x_tr)
    # L2(ridge): ‖Aw − y‖² + λ‖w‖²  =  ‖[A; √λ·I] w − [y; 0]‖²  (같은 식을 안정적으로 푸는 형태)
    A_aug = np.vstack([A, np.sqrt(lam) * np.eye(16)])
    w = np.linalg.lstsq(A_aug, np.r_[y_tr, np.zeros(16)], rcond=None)[0]
    print(f"λ={lam:<7g} ‖w‖={np.linalg.norm(w):12.1f}  "
          f"train={rmse(A @ w, y_tr):.3f}  val={rmse(V(x_va) @ w, y_va):.3f}")
```

```text
λ=0       ‖w‖=   1442205.3  train=0.101  val=44.758
λ=1e-06   ‖w‖=       250.8  train=0.147  val=1.114
λ=0.001   ‖w‖=         3.7  train=0.193  val=0.276
λ=0.1     ‖w‖=         2.2  train=0.204  val=0.284
λ=10      ‖w‖=         0.4  train=0.539  val=0.616
```

출력에서 볼 것: λ=0이면 weight 노름이 144만이고 val이 폭발한다(4.3절과 같은 결과). λ를 0.001로 올리자 노름이 3.7로 줄고 val이 0.276으로 내려온다. λ=10은 너무 세서 다시 underfitting(train도 0.539). λ도 하이퍼파라미터라서 **val로** 골라야 한다. 그리고 weight가 작으면 양자화할 때 범위가 좁아져서 INT8 scale도 유리해진다(C1과 연결).

### 5.2 Dropout

**Dropout**은 학습 중에 각 뉴런 출력을 확률 p로 0으로 만드는 기법이다. 남은 값은 `1/(1−p)`배로 키워서 평균을 맞춘다. 매 step마다 무작위로 다른 부분망이 학습되므로, 특정 뉴런 몇 개에 의존해 외우는 것을 막는다.

- 추론(inference) 때는 **끈다**. PyTorch에서는 `model.eval()`이 이 스위치다. 이걸 잊으면 추론 결과가 매번 달라진다 — 배포 버그 단골이다.
- 배포 관점에서 dropout은 공짜다. export 시 사라지는 op이라 MCU 비용이 0이다.

### 5.3 Early stopping

**Early stopping**은 val loss가 일정 epoch(patience) 동안 개선되지 않으면 학습을 멈추고, **val loss가 가장 낮았던 시점의 weight**를 쓰는 방법이다. 그림 2의 초록 점선이 그것이다. 구현은 "best val loss를 갱신할 때마다 checkpoint 저장"이면 끝난다.

다음 코드는 그림 2를 만든 실험이다. 같은 MLP를 (1) 규제 없이, (2) weight decay 1.0으로, (3) dropout 0.5로 400 epoch 학습하고, val loss가 최소였던 epoch과 마지막 epoch의 loss를 비교한다.

```python
import torch, torch.nn as nn
def make(n, g):                                # 2D 점, 원 안이면 1 — 라벨 15%는 일부러 뒤집음(노이즈)
    x = torch.rand(n, 2, generator=g) * 2 - 1
    y = ((x ** 2).sum(1) < 0.5).long()
    flip = torch.rand(n, generator=g) < 0.15
    return x, torch.where(flip, 1 - y, y)
g = torch.Generator().manual_seed(0)
xtr, ytr = make(80, g); xva, yva = make(1000, g)

def run(wd=0.0, p=0.0, epochs=400):
    torch.manual_seed(0)
    m = nn.Sequential(nn.Linear(2, 128), nn.ReLU(), nn.Dropout(p),
                      nn.Linear(128, 128), nn.ReLU(), nn.Dropout(p), nn.Linear(128, 2))
    opt = torch.optim.AdamW(m.parameters(), lr=1e-3, weight_decay=wd)
    lossf, hist = nn.CrossEntropyLoss(), []
    for ep in range(epochs):
        m.train(); opt.zero_grad(); l = lossf(m(xtr), ytr); l.backward(); opt.step()
        m.eval()                                 # 평가 때는 dropout 끔
        with torch.no_grad(): hist.append((l.item(), lossf(m(xva), yva).item()))
    return hist
for name, kw in [("plain", {}), ("weight_decay=1.0", {"wd": 1.0}), ("dropout=0.5", {"p": 0.5})]:
    h = run(**kw); best = min(range(len(h)), key=lambda i: h[i][1])
    print(f"{name:17s} best epoch={best:3d} val={h[best][1]:.3f} | "
          f"final train={h[-1][0]:.3f} val={h[-1][1]:.3f}")
```

```text
plain             best epoch= 45 val=0.508 | final train=0.122 val=1.098
weight_decay=1.0  best epoch= 48 val=0.506 | final train=0.174 val=0.860
dropout=0.5       best epoch=132 val=0.529 | final train=0.404 val=0.585
```

출력에서 볼 것: 규제 없이 끝까지 학습하면 val loss가 0.508 → 1.098로 두 배 넘게 나빠진다. weight decay는 그 악화를 0.860으로, dropout은 0.585로 줄인다. 그런데 세 경우 모두 **best val**은 0.51 근처로 비슷하다 — 이 데이터에서는 라벨 15%가 뒤집혀 있어서 그보다 낮아질 수 없다(노이즈 바닥). 즉 early stopping 하나만으로도 대부분을 얻었다. 참고로 이 코드의 train loss는 optimizer step 직전, train 모드(dropout 켜짐)에서 잰 값이라 dropout 행의 train loss는 실제보다 높게 보인다.

### 5.4 Data augmentation — 센서·오디오용

**Data augmentation(데이터 증강)**은 학습 샘플에 "라벨을 바꾸지 않는 변형"을 무작위로 걸어서 데이터를 불리는 기법이다. 핵심은 **현장에서 실제로 생기는 변동을 흉내 내는 것**이다. 펌웨어로 말하면 corner case(온도·전압·lot)를 시뮬레이션해서 테스트 커버리지를 넓히는 것과 같다.

| 변형 | 흉내 내는 현실 | IMU | 오디오 |
|---|---|---|---|
| jitter (가우시안 노이즈) | 센서 노이즈, ADC 양자화 | ✓ | ✓ (배경 소음 섞기) |
| scaling (축별 gain) | 센서 gain 편차, 사람마다 힘 차이 | ✓ | ✓ (볼륨) |
| time shift | 이벤트가 윈도우 안 어디서든 시작 | ✓ | ✓ |
| rotation | 기기를 조금 돌려 찬 착용 방향 | ✓ | — |
| time/frequency masking | 일부 구간·대역 손실 | — | ✓ (SpecAugment) |
| room impulse response | 방 잔향 | — | ✓ |

오디오 쪽의 **SpecAugment**는 log-mel spectrogram(G4에서 다룬다)에서 무작위 시간 구간과 주파수 대역을 가려서(mask) 학습시키는 기법이다(Park et al., 2019). ASR·KWS에서 표준처럼 쓰인다.

다음 코드는 1초짜리 3축 가속도 윈도우에 네 가지 IMU 증강을 걸고, 가속도 크기 `|a|`의 평균과 원본과의 최대 차이를 출력한다.

```python
import numpy as np
rng = np.random.default_rng(7)
T = 100                                        # 100 Hz × 1초 윈도우, 3축 가속도 [T, 3] (g 단위)
t = np.arange(T) / 100
x = np.stack([0.3 * np.sin(2 * np.pi * 2 * t), 0.1 * np.cos(2 * np.pi * 2 * t),
              np.ones(T)], axis=1)             # z축에 중력 1 g

def jitter(x, sigma=0.02):   return x + rng.normal(0, sigma, x.shape)       # 센서 노이즈
def scaling(x, s=0.1):       return x * rng.normal(1, s, (1, x.shape[1]))    # 축별 gain 편차
def time_shift(x, max_s=10): return np.roll(x, rng.integers(-max_s, max_s + 1), axis=0)
def rotate_z(x, max_deg=30):                   # 기기를 손목에 조금 돌려 찬 효과 (z축 회전)
    a = np.deg2rad(rng.uniform(-max_deg, max_deg))
    R = np.array([[np.cos(a), -np.sin(a), 0], [np.sin(a), np.cos(a), 0], [0, 0, 1]])
    return x @ R.T                             # 각 샘플(행 벡터)에 R 적용

for name, fn in [("jitter", jitter), ("scaling", scaling), ("time_shift", time_shift), ("rotate_z", rotate_z)]:
    xa = fn(x)
    print(f"{name:10s} shape={xa.shape}  |a| mean: {np.linalg.norm(x, axis=1).mean():.3f} -> "
          f"{np.linalg.norm(xa, axis=1).mean():.3f}   max|diff|={np.abs(xa - x).max():.3f}")
```

```text
jitter     shape=(100, 3)  |a| mean: 1.025 -> 1.023   max|diff|=0.065
scaling    shape=(100, 3)  |a| mean: 1.025 -> 1.026   max|diff|=0.045
time_shift shape=(100, 3)  |a| mean: 1.025 -> 1.025   max|diff|=0.112
rotate_z   shape=(100, 3)  |a| mean: 1.025 -> 1.025   max|diff|=0.141
```

출력에서 볼 것: 회전은 축별 값을 최대 0.141 g나 바꾸지만 `|a|`의 평균은 정확히 그대로다 — 회전 행렬은 벡터 길이를 보존하기 때문이다(A1의 직교 행렬). 그래서 "착용 방향에 둔감한 모델"을 만들고 싶으면 회전 증강을 쓰거나, 아예 `|a|` 같은 회전 불변 특징을 쓴다. `np.roll`은 끝을 앞으로 감아 붙이므로 주기 신호가 아니면 경계에 불연속이 생긴다 — 실무에서는 더 긴 원본에서 윈도우 시작점을 무작위로 잡는 방식이 낫다.

> 흔한 함정: 증강은 **train에만** 건다. val/test에 걸면 현실과 다른 데이터로 평가하게 된다. 그리고 라벨을 바꾸는 변형은 금지다 — 예를 들어 "왼쪽으로 휘두르기" 제스처에 좌우 반전을 걸면 "오른쪽 휘두르기"가 된다.

---

## 6. 하이퍼파라미터 튜닝과 cross-validation

### 6.1 튜닝 방법

| 방법 | 방식 | 장단점 |
|---|---|---|
| 수동 | 곡선 보고 한두 개씩 바꿈 | 직관이 쌓이지만 느림 |
| grid search | 모든 조합(예: lr 3개 × wd 3개 = 9번) | 단순. 차원이 늘면 폭발 |
| random search | 범위 안에서 무작위 조합 N개 | 같은 예산이면 보통 grid보다 낫다(중요한 파라미터를 더 촘촘히 보게 됨, Bergstra & Bengio 2012) |
| Bayesian / 자동 (Optuna 등) | 앞 결과를 보고 다음 조합 선택 | 비싼 학습에 효율적 |

learning rate나 weight decay는 **로그 스케일**로 찾는다(1e-4, 3e-4, 1e-3, 3e-3 …). 값이 몇 배 차이 날 때 효과가 달라지기 때문이다.

절대 규칙: 튜닝은 **val**로만 한다. test는 마지막에 한 번. 튜닝 루프가 test 점수를 보는 순간 test는 val이 된다.

### 6.2 k-fold cross-validation

데이터가 작으면 val 하나로는 점수가 불안정하다. **k-fold cross-validation**은 데이터를 k개 묶음(fold)으로 나누고, 각 fold를 한 번씩 val로 쓰면서 k번 학습해 점수의 평균과 표준편차를 보는 방법이다.

```
k = 5:   [V][T][T][T][T]   → 점수 s1
         [T][V][T][T][T]   → 점수 s2
         [T][T][V][T][T]   → 점수 s3
         [T][T][T][V][T]   → 점수 s4
         [T][T][T][T][V]   → 점수 s5
보고:  mean(s) ± std(s)
```

말로 하면: 모든 데이터가 한 번씩 val이 되므로 "운 좋은 split"에 덜 속는다. 대신 학습 비용이 k배다.

센서 데이터에서는 fold를 **사용자 단위**로 만들어야 한다. 이것을 **group k-fold**라 한다(사용자 수가 적으면 한 명씩 빼는 leave-one-subject-out도 흔하다). 3절의 누수를 k-fold에서도 똑같이 막는 것이다.

다음 코드는 numpy만으로 group k-fold를 구현하고, 각 fold에서 train과 val에 동시에 나타나는 사용자가 0명인지 확인한다.

```python
import numpy as np
rng = np.random.default_rng(3)
n_users, K = 10, 5
user = np.repeat(np.arange(n_users), rng.integers(20, 60, n_users))  # 사용자마다 윈도우 수가 다름

def group_kfold(groups, k, rng):
    ug = rng.permutation(np.unique(groups))    # 사용자 ID를 섞어서
    for f in range(k):                         # 사용자 단위로 k 묶음에 나눈다
        te_users = ug[f::k]
        te = np.isin(groups, te_users)
        yield np.where(~te)[0], np.where(te)[0], te_users

for f, (tr, te, tu) in enumerate(group_kfold(user, K, rng)):
    overlap = np.intersect1d(user[tr], user[te])   # 학습·검증에 동시에 나오는 사용자 (0이어야 함)
    print(f"fold {f}: val users={sorted(tu.tolist())}  n_train={len(tr):3d}  "
          f"n_val={len(te):3d}  shared users={overlap.size}")
```

```text
fold 0: val users=[5, 9]  n_train=276  n_val= 75  shared users=0
fold 1: val users=[1, 4]  n_train=301  n_val= 50  shared users=0
fold 2: val users=[0, 8]  n_train=278  n_val= 73  shared users=0
fold 3: val users=[2, 3]  n_train=295  n_val= 56  shared users=0
fold 4: val users=[6, 7]  n_train=254  n_val= 97  shared users=0
```

출력에서 볼 것: 사용자 단위로 나누면 fold마다 val 크기가 50~97로 들쭉날쭉하다. 이건 정상이다. scikit-learn을 쓴다면 `GroupKFold`, `StratifiedGroupKFold`가 같은 일을 한다(클래스 비율까지 맞추려면 후자).

### 6.3 최종 절차 한 줄 요약

① test 사용자(예: 50명 중 8명)를 먼저 봉인 → ② 나머지 42명으로 group k-fold 해서 설정 비교 → ③ 고른 설정으로 42명 전체 재학습(early stopping용 val은 그중 일부 사용자) → ④ 봉인한 8명으로 test 1회 → ⑤ 양자화·배포 후 같은 8명으로 재측정(11절).

---

## 7. 분류 지표 — confusion matrix에서 전부 나온다

### 7.1 Confusion matrix

이진 분류의 결과는 네 칸으로 정리된다. "양성(positive)"은 우리가 찾고 싶은 것(손목 들기, wake word)이다.

| | 예측 양성 | 예측 음성 |
|---|---|---|
| 실제 양성 | **TP** (true positive, 맞게 잡음) | **FN** (false negative, 놓침) |
| 실제 음성 | **FP** (false positive, 헛잡음) | **TN** (true negative, 맞게 무시) |

펌웨어의 에러 검출 로직에 비유하면: FN은 "진짜 에러를 못 잡음(miss)", FP는 "정상인데 에러로 판정(false alarm)"이다. ECC나 watchdog 설계에서 이미 이 둘의 trade-off를 다뤄 봤을 것이다.

손계산용 예: 양성 10개, 음성 90개인 test set에서 모델이 양성 8개를 잡고(2개 놓침), 음성 중 5개를 양성으로 잘못 말했다.

```svg
<svg viewBox="0 0 620 300" xmlns="http://www.w3.org/2000/svg">
<text x="320" y="24" font-size="14" text-anchor="middle">예측 (모델 출력)</text> <text x="260" y="50" font-size="13" text-anchor="middle">양성</text> <text x="380" y="50" font-size="13" text-anchor="middle">음성</text> <text x="70" y="160" font-size="14" text-anchor="middle" transform="rotate(-90 70 160)">실제 (라벨)</text>
<text x="180" y="105" font-size="13" text-anchor="end">양성 (10)</text> <text x="180" y="205" font-size="13" text-anchor="end">음성 (90)</text> <rect x="200" y="60" width="120" height="90" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor"/> <rect x="320" y="60" width="120" height="90" fill="#d0564a" fill-opacity="0.45" stroke="currentColor"/>
<rect x="200" y="150" width="120" height="90" fill="#e08a3c" fill-opacity="0.45" stroke="currentColor"/> <rect x="320" y="150" width="120" height="90" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor"/> <text x="260" y="100" font-size="14" text-anchor="middle">TP</text> <text x="260" y="122" font-size="14" text-anchor="middle">8</text>
<text x="380" y="100" font-size="14" text-anchor="middle">FN</text> <text x="380" y="122" font-size="14" text-anchor="middle">2</text> <text x="260" y="190" font-size="14" text-anchor="middle">FP</text> <text x="260" y="212" font-size="14" text-anchor="middle">5</text>
<text x="380" y="190" font-size="14" text-anchor="middle">TN</text> <text x="380" y="212" font-size="14" text-anchor="middle">85</text> <text x="460" y="100" font-size="12">recall = 8/(8+2) = 0.80</text> <text x="460" y="200" font-size="12">specificity</text>
<text x="460" y="216" font-size="12">= 85/(85+5) = 0.944</text> <text x="200" y="266" font-size="12">precision = 8/(8+5) = 0.615  (왼쪽 열)</text> <text x="200" y="286" font-size="12">accuracy = (8+85)/100 = 0.93  (대각선)</text>
</svg>
```

그림 3 — 행은 실제, 열은 예측이다. recall은 "실제 양성 행"에서, precision은 "예측 양성 열"에서, accuracy는 대각선에서 읽는다. 초록은 맞힌 칸, 빨강·주황은 틀린 칸이다.

### 7.2 지표 정의와 손계산

```
accuracy    = (TP + TN) / 전체               = (8 + 85) / 100      = 0.930
precision   = TP / (TP + FP)                 = 8 / 13              = 0.615
recall      = TP / (TP + FN)                 = 8 / 10              = 0.800
specificity = TN / (TN + FP)                 = 85 / 90             = 0.944
F1          = 2 · P · R / (P + R)            = 2·0.615·0.8/1.415   = 0.696
```

말로 하면:

- **precision(정밀도)**: 모델이 "양성!"이라고 외쳤을 때 진짜일 확률. 낮으면 헛경보가 많다.
- **recall(재현율, sensitivity, TPR)**: 진짜 양성 중에 잡아낸 비율. 낮으면 놓치는 게 많다.
- **specificity(특이도, TNR)**: 진짜 음성을 음성으로 둔 비율. `1 − specificity`가 FPR(false positive rate)이다.
- **F1**: precision과 recall의 조화평균. 둘 중 하나라도 낮으면 F1이 낮다(산술평균이었다면 0.708, 조화평균은 작은 쪽으로 끌린다).

다음 코드는 위 손계산을 그대로 확인하고, 이어서 "정확도 역설"을 보여 준다.

```python
import numpy as np
def metrics(y, p):
    tp = int(((p == 1) & (y == 1)).sum()); fn = int(((p == 0) & (y == 1)).sum())
    fp = int(((p == 1) & (y == 0)).sum()); tn = int(((p == 0) & (y == 0)).sum())
    acc = (tp + tn) / len(y)
    prec = tp / (tp + fp) if tp + fp else 0.0     # 0으로 나누기 방지
    rec = tp / (tp + fn) if tp + fn else 0.0
    spec = tn / (tn + fp) if tn + fp else 0.0
    f1 = 2 * prec * rec / (prec + rec) if prec + rec else 0.0
    return dict(TP=tp, FN=fn, FP=fp, TN=tn, acc=round(acc, 3), prec=round(prec, 3),
                rec=round(rec, 3), spec=round(spec, 3), F1=round(f1, 3))

# 손계산 예제와 같은 숫자: 양성 10, 음성 90 → TP 8, FN 2, FP 5, TN 85
y = np.array([1] * 10 + [0] * 90)
p = np.array([1] * 8 + [0] * 2 + [1] * 5 + [0] * 85)
print("model   :", metrics(y, p))

# 정확도 역설: 양성 1%(10/1000) 데이터에서 "항상 0"이라고 답하는 모델
y2 = np.array([1] * 10 + [0] * 990)
print("always-0:", metrics(y2, np.zeros(1000, dtype=int)))
```

```text
model   : {'TP': 8, 'FN': 2, 'FP': 5, 'TN': 85, 'acc': 0.93, 'prec': 0.615, 'rec': 0.8, 'spec': 0.944, 'F1': 0.696}
always-0: {'TP': 0, 'FN': 10, 'FP': 0, 'TN': 990, 'acc': 0.99, 'prec': 0.0, 'rec': 0.0, 'spec': 1.0, 'F1': 0.0}
```

출력에서 볼 것: 두 번째 줄이 **정확도 역설(accuracy paradox)**이다. 양성이 1%뿐이면 아무것도 안 하는 모델이 accuracy 99%를 받는다. 그러나 recall 0, F1 0 — 쓸모가 없다. 낙상 감지, wake word, 이상 감지처럼 양성이 드문 문제에서는 accuracy를 주 지표로 쓰면 안 된다. (precision이 0/0일 때 0으로 두는 것은 관례이고, scikit-learn도 기본적으로 경고와 함께 0을 쓴다.)

### 7.3 클래스 불균형 다루기

- 지표: precision/recall/F1, PR-AUC, 클래스별 recall을 본다.
- 학습: 드문 클래스에 loss 가중치(`nn.CrossEntropyLoss(weight=…)`), 오버샘플링, 음성 언더샘플링.
- 평가 데이터의 양성 비율이 **현장 비율과 다르면** precision이 달라진다. recall과 FPR은 비율에 영향을 안 받지만, precision은 받는다. 예: 실험실 test에서 양성 50%로 precision 0.95였어도, 현장에서 양성이 0.1%라면 헛경보가 대부분일 수 있다.

### 7.4 임계값(threshold) 고르기

대부분의 분류기는 0~1 점수(또는 logit)를 내고, 우리가 임계값을 정해 양성/음성을 가른다. 임계값을 올리면 precision↑ recall↓, 내리면 반대다. 임계값은 모델의 일부가 아니라 **제품 결정**이다 — 그리고 val에서 고른다. 펌웨어의 comparator threshold나 hysteresis를 양산 데이터로 정하는 것과 같다.

---

## 8. ROC 곡선, AUC, PR 곡선

### 8.1 직관과 정의

임계값을 위에서 아래로 쓸어내리면서 각 임계값마다 (FPR, TPR) 점을 찍으면 **ROC 곡선**(receiver operating characteristic)이 된다.

```
TPR = TP / (TP + FN)   (= recall)
FPR = FP / (FP + TN)   (= 1 − specificity)
```

**AUC**(area under the ROC curve)는 그 곡선 아래 넓이다. 해석이 아주 깔끔하다.

```
AUC = P( 무작위 양성 샘플의 점수 > 무작위 음성 샘플의 점수 )
```

말로 하면: 양성 하나, 음성 하나를 뽑아 점수를 비교했을 때 양성이 더 높을 확률이다. 0.5는 동전 던지기, 1.0은 완벽한 순서. **임계값과 무관하게** 모델의 "순서 매기는 능력"만 본다.

손계산: 양성 점수 {0.9, 0.7, 0.4}, 음성 점수 {0.8, 0.3, 0.2}. 3×3 = 9쌍을 비교한다.

```
양성 0.9 > 0.8, 0.3, 0.2   → 3쌍 이김
양성 0.7 > 0.3, 0.2        → 2쌍 (0.8에는 짐)
양성 0.4 > 0.3, 0.2        → 2쌍
AUC = (3 + 2 + 2) / 9 = 7/9 ≈ 0.778
```

**PR 곡선**은 x축 recall, y축 precision이다. 양성이 드물 때 ROC는 낙관적으로 보이기 쉽다(음성이 아주 많으면 FP가 꽤 늘어도 FPR은 조금만 움직이므로). PR 곡선은 precision을 직접 보여 줘서 불균형 문제에 더 솔직하다. PR 곡선 아래 넓이의 흔한 추정치가 **AP**(average precision)다. 무작위 모델의 PR 기준선은 0.5가 아니라 **양성 비율**이다.

### 8.2 numpy로 직접 구현

다음 코드는 양성 200개(점수 평균 1.5), 음성 800개(평균 0)의 합성 점수로 ROC·PR을 sklearn 없이 계산하고, AUC를 사다리꼴 적분과 쌍 비교 두 방법으로 구해 일치하는지 확인한다.

```python
import numpy as np
rng = np.random.default_rng(0)
s = np.r_[rng.normal(1.5, 1, 200), rng.normal(0, 1, 800)]   # 모델 점수: 양성 200, 음성 800
y = np.r_[np.ones(200, int), np.zeros(800, int)]

def roc_pr(s, y):
    o = np.argsort(-s); ys = y[o]              # 점수 내림차순 = 임계값을 위에서부터 내린다
    tp = np.cumsum(ys); fp = np.cumsum(1 - ys) # 임계값 = 각 점수일 때의 누적 TP, FP
    tpr = np.r_[0, tp / y.sum()]; fpr = np.r_[0, fp / (1 - y).sum()]
    prec = tp / (tp + fp)
    return fpr, tpr, prec, s[o]

fpr, tpr, prec, thr = roc_pr(s, y)
auc = np.sum((fpr[1:] - fpr[:-1]) * (tpr[1:] + tpr[:-1]) / 2)          # 사다리꼴 적분
pos, neg = s[y == 1], s[y == 0]
auc_rank = (pos[:, None] > neg[None, :]).mean()   # P(양성 점수 > 음성 점수) — 같은 값이어야 함
ap = np.sum(np.diff(tpr) * prec)                  # PR 곡선 아래 넓이 (average precision)
print(f"ROC-AUC (trapezoid) = {auc:.4f}   AUC (pairwise) = {auc_rank:.4f}   AP = {ap:.4f}")
for t in (2.0, 1.0, 0.5, 0.0):
    k = (thr >= t).sum()                          # 이 임계값 이상을 양성으로 판정한 개수
    print(f"thr={t:3.1f}: TPR(recall)={tpr[k]:.3f}  FPR={fpr[k]:.3f}  precision={prec[k-1]:.3f}")
```

```text
ROC-AUC (trapezoid) = 0.8734   AUC (pairwise) = 0.8734   AP = 0.6786
thr=2.0: TPR(recall)=0.320  FPR=0.020  precision=0.800
thr=1.0: TPR(recall)=0.700  FPR=0.142  precision=0.551
thr=0.5: TPR(recall)=0.830  FPR=0.281  precision=0.425
thr=0.0: TPR(recall)=0.960  FPR=0.455  precision=0.345
```

출력에서 볼 것: 두 AUC가 소수 넷째 자리까지 같다 — "넓이 = 순서 확률"이 사실임을 확인했다(점수가 연속값이라 동점이 없어서 정확히 일치한다). 이론값은 `Φ(1.5/√2) ≈ 0.856`인데 샘플 1000개라 0.873이 나왔다. 임계값을 2.0 → 0.0으로 내리면 recall은 0.32 → 0.96으로 오르고 precision은 0.80 → 0.345로 떨어진다. 같은 모델, 다른 동작점이다. AUC 0.87이 좋아 보여도 AP는 0.68이다 — 양성이 20%라 PR 쪽이 더 엄격하다.

```svg
<svg viewBox="0 0 660 320" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="270" x2="300" y2="270" stroke="currentColor"/> <line x1="60" y1="30" x2="60" y2="270" stroke="currentColor"/> <line x1="60" y1="270" x2="300" y2="30" stroke="#888" stroke-dasharray="5 4"/> <text x="180" y="304" font-size="13" text-anchor="middle">FPR (1 − specificity)</text>
<text x="22" y="150" font-size="13" text-anchor="middle" transform="rotate(-90 22 150)">TPR (recall)</text> <text x="60" y="286" font-size="12" text-anchor="middle">0</text> <text x="300" y="286" font-size="12" text-anchor="middle">1</text> <text x="52" y="274" font-size="12" text-anchor="end">0</text>
<text x="52" y="34" font-size="12" text-anchor="end">1</text>
<polyline fill="none" stroke="#4a7bd0" stroke-width="2" points="60.0,270.0 60.0,256.8 66.0,186.0 72.0,144.0 78.0,129.6 84.0,117.6 90.0,111.6 96.0,97.2 102.0,84.0 108.0,79.2 114.0,73.2 120.0,72.0 126.0,70.8 132.0,66.0 138.0,63.6 144.0,60.0 150.0,52.8 156.0,46.8 162.0,45.6 168.0,39.6 174.0,39.6 180.0,39.6 186.0,39.6 192.0,39.6 198.0,39.6 204.0,39.6 210.0,39.6 216.0,37.2 222.0,36.0 228.0,36.0 234.0,36.0 240.0,36.0 246.0,33.6 252.0,31.2 258.0,30.0 264.0,30.0 270.0,30.0 276.0,30.0 282.0,30.0 288.0,30.0 294.0,30.0 300.0,30.0"/>
<circle cx="64.8" cy="193.2" r="4" fill="#e08a3c"/> <circle cx="94.2" cy="102.0" r="4" fill="#e08a3c"/> <circle cx="127.5" cy="70.8" r="4" fill="#e08a3c"/> <circle cx="169.2" cy="39.6" r="4" fill="#e08a3c"/>
<text x="72" y="206" font-size="12">thr 2.0</text> <text x="100" y="118" font-size="12">thr 1.0</text> <text x="134" y="86" font-size="12">thr 0.5</text> <text x="176" y="56" font-size="12">thr 0.0</text>
<text x="190" y="230" font-size="13">AUC = 0.873</text> <text x="190" y="250" font-size="12">점선 = 무작위 (0.5)</text> <line x1="380" y1="270" x2="620" y2="270" stroke="currentColor"/> <line x1="380" y1="30" x2="380" y2="270" stroke="currentColor"/>
<line x1="380" y1="222" x2="620" y2="222" stroke="#888" stroke-dasharray="5 4"/> <text x="500" y="304" font-size="13" text-anchor="middle">recall</text> <text x="352" y="150" font-size="13" text-anchor="middle" transform="rotate(-90 352 150)">precision</text> <text x="380" y="286" font-size="12" text-anchor="middle">0</text>
<text x="620" y="286" font-size="12" text-anchor="middle">1</text> <text x="372" y="274" font-size="12" text-anchor="end">0</text> <text x="372" y="34" font-size="12" text-anchor="end">1</text>
<polyline fill="none" stroke="#3f9a6b" stroke-width="2" points="386.0,30.0 392.0,30.0 398.0,45.0 404.0,41.4 410.0,47.8 416.0,45.0 422.0,48.9 428.0,56.7 434.0,70.0 440.0,76.5 446.0,73.0 452.0,78.0 458.0,84.3 464.0,83.3 470.0,84.4 476.0,83.6 482.0,89.5 488.0,95.8 494.0,96.0 500.0,96.1 506.0,96.2 512.0,102.9 518.0,111.4 524.0,116.0 530.0,122.2 536.0,124.2 542.0,135.0 548.0,137.2 554.0,139.7 560.0,140.5 566.0,142.6 572.0,150.7 578.0,157.5 584.0,169.3 590.0,176.9 596.0,180.0 602.0,180.1 608.0,186.9 614.0,204.6 620.0,213.4"/>
<circle cx="456.8" cy="78.0" r="4" fill="#e08a3c"/> <circle cx="548.0" cy="137.7" r="4" fill="#e08a3c"/> <circle cx="579.2" cy="168.1" r="4" fill="#e08a3c"/> <circle cx="610.4" cy="187.1" r="4" fill="#e08a3c"/>
<text x="400" y="240" font-size="12">점선 = 양성 비율 0.2 (무작위)</text> <text x="470" y="60" font-size="13">AP = 0.679</text>
</svg>
```

그림 4 — 왼쪽은 ROC, 오른쪽은 PR 곡선. 둘 다 예제 코드의 실제 계산값이고, 주황 점은 출력 표의 네 임계값이다. ROC에서 왼쪽 위로, PR에서 오른쪽 위로 갈수록 좋다.

> 흔한 함정: AUC는 "순서"만 본다. 점수가 확률로서 맞는지(calibration)나, 특정 임계값에서의 동작은 알려 주지 않는다. 제품은 한 임계값에서 돌아가므로, AUC와 함께 **동작점의 precision/recall**을 반드시 보고한다.

---

## 9. Wake word · 이벤트 검출 지표 — FAR, FRR, DET

### 9.1 왜 따로 다루나

wake word("Hey Hark" 같은 호출어, 가정)나 낙상 감지 같은 **이벤트 검출**은 연속 스트림 위에서 돈다. 음성 샘플이 "개수"로 정해져 있지 않다. 그래서 지표도 다르게 정의한다.

- **FRR (false rejection rate)**: 실제로 wake word를 말했는데 반응하지 않은 비율. `FRR = 놓친 발화 수 / 전체 발화 수`. recall의 반대(`1 − recall`)다.
- **FAR (false acceptance rate)**: 말하지 않았는데 켜진 횟수. 비율이 아니라 **시간당 횟수(FA/hour)**로 쓴다. `FAR = 오작동 횟수 / 음성(배경) 오디오 시간`.
- **DET 곡선 (detection error trade-off)**: 임계값을 바꿔 가며 x축에 FRR(또는 FA/hour), y축에 FA/hour(또는 FRR)를 찍은 곡선. 보통 로그 축을 쓴다. ROC와 같은 정보를 "오류 대 오류"로 보여 준다.

왜 시간당 횟수인가? 사용자는 "오작동 확률 0.0001%"를 체감하지 않는다. "하루에 한 번 혼자 켜진다"를 체감한다. 그리고 한 번 켜질 때마다 큰 코어·마이크 경로·무선이 깨어나므로 **배터리 예산**이 된다. 펌웨어로 치면 spurious interrupt가 시간당 몇 번 나느냐다.

### 9.2 오작동 예산을 임계값으로 바꾸는 산수

요구사항: "false wake ≤ 1회 / 24시간". 검출기는 100 ms마다 점수를 낸다(hop 100 ms, 초당 10회)고 하자.

```
하루 판정 횟수     = 10 /s × 86,400 s            = 864,000 회
시간당 예산        = 1 / 24                      ≈ 0.0417 FA/hour
판정당 허용 FP 확률 ≈ 1 / 864,000                ≈ 1.16 × 10⁻⁶
```

말로 하면: 판정 한 번의 오판 확률을 **100만분의 1 수준**으로 낮춰야 한다. 그래서 wake word 모델의 임계값은 "음성 점수 분포의 꼬리 끝"에 놓인다. (실제로는 연속 프레임이 한 번의 오작동으로 합쳐지고, 뒤에 2단계 검증 모델을 두는 경우가 많아 이 산수는 보수적인 근사다.)

검증 데이터의 크기 산수도 중요하다. 오작동은 드문 사건이라 **Poisson 통계**를 따른다. T시간 동안 오작동 0회를 관찰했을 때 95% 신뢰 상한은 약 `3/T` 회/시간이다("rule of three"). 따라서

```
3 / T ≤ 1/24   →   T ≥ 72 시간
```

말로 하면: 오작동 0회를 보고 "하루 1회 이하"라고 주장하려면 **최소 72시간**의 배경 오디오가 필요하다. 10시간 테스트로 0회였다는 건 거의 아무 증명이 안 된다. Don이 양산에서 "불량 0개 / N개 샘플"로 불량률 상한을 말하던 것과 똑같은 통계다.

### 9.3 코드로 확인 — 동작점 고르기

다음 코드는 배경 오디오 100시간(점수 360만 개, 표준정규)과 wake word 발화 1000개(점수 평균 6)를 흉내 내고, 임계값마다 FA 횟수, FA/hour, 24시간 환산, FRR을 계산한다. 오작동은 임계값을 **넘어가는 순간**(rising edge)만 1회로 센다 — 연속 프레임이 한 번의 wake로 합쳐지는 것을 흉내 낸 것이다.

```python
import numpy as np
rng = np.random.default_rng(0)
HOURS, RATE = 100, 10                          # 음성 아닌 배경 오디오 100시간, 초당 10번 점수(hop 100 ms)
neg = rng.normal(0, 1, HOURS * 3600 * RATE)    # 배경 구간 점수 (3,600,000개)
pos = rng.normal(6, 1, 1000)                   # wake word 발화 1000개의 최고 점수

def false_accepts(scores, thr):                # 임계값을 "넘어가는 순간"(rising edge)만 1회로 센다
    above = scores >= thr
    return int(np.count_nonzero(above[1:] & ~above[:-1]) + above[0])

print(" thr   FA  FA/hour  FA/24h   FRR")
for thr in (3.5, 4.0, 4.5, 4.6, 4.7, 4.8, 5.0):
    fa = false_accepts(neg, thr)
    frr = (pos < thr).mean()                   # 놓친 wake word 비율
    print(f"{thr:4.1f} {fa:4d}  {fa / HOURS:7.3f}  {24 * fa / HOURS:6.2f}  {frr:5.3f}")
print("budget: 1 false wake / 24 h =", round(1 / 24, 4), "FA/hour")
```

```text
 thr   FA  FA/hour  FA/24h   FRR
 3.5  834    8.340  200.16  0.013
 4.0  107    1.070   25.68  0.028
 4.5   11    0.110    2.64  0.060
 4.6    9    0.090    2.16  0.074
 4.7    2    0.020    0.48  0.091
 4.8    1    0.010    0.24  0.110
 5.0    0    0.000    0.00  0.161
budget: 1 false wake / 24 h = 0.0417 FA/hour
```

출력에서 볼 것:

- 임계값을 3.5 → 5.0으로 올리면 하루 오작동이 200회 → 0회로 줄고, 대신 놓치는 wake word가 1.3% → 16.1%로 는다. 이게 DET trade-off다.
- 표면적으로는 4.7(하루 0.48회, FRR 9.1%)이 예산을 만족한다. 그러나 100시간에 **2회**만 관찰된 숫자다. 이론값(표준정규 꼬리 `Q(4.7) ≈ 1.30×10⁻⁶` × 360만)은 약 4.7회, 즉 하루 1.12회로 **예산 초과**다. 2회가 나온 건 운이었다. 2회 관찰의 Poisson 95% 상한은 약 6.3회 → 하루 약 1.5회라서 이 데이터로는 4.7이 안전하다고 증명할 수 없다.
- 4.8(이론 하루 0.69회)이나 5.0(이론 0.25회)이 더 안전하다. 5.0은 100시간 동안 0회 → rule of three로 상한 0.03 FA/hour = 하루 0.72회 → 예산 증명 가능. 대가는 FRR 16%다. 이 FRR이 싫으면 모델 자체를 개선해야 한다(곡선을 왼쪽 아래로 밀기).

```svg
<svg viewBox="0 0 660 310" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="250" x2="610" y2="250" stroke="currentColor"/> <line x1="70" y1="30" x2="70" y2="250" stroke="currentColor"/> <g stroke="currentColor" stroke-opacity="0.2"> <line x1="70" y1="30" x2="610" y2="30"/><line x1="70" y1="85" x2="610" y2="85"/><line x1="70" y1="140" x2="610" y2="140"/>
</g> <text x="62" y="34" font-size="12" text-anchor="end">1000</text> <text x="62" y="89" font-size="12" text-anchor="end">100</text> <text x="62" y="144" font-size="12" text-anchor="end">10</text>
<text x="62" y="199" font-size="12" text-anchor="end">1</text> <text x="62" y="254" font-size="12" text-anchor="end">0.1</text> <text x="70" y="268" font-size="12" text-anchor="middle">0</text> <text x="205" y="268" font-size="12" text-anchor="middle">0.1</text>
<text x="340" y="268" font-size="12" text-anchor="middle">0.2</text> <text x="475" y="268" font-size="12" text-anchor="middle">0.3</text> <text x="610" y="268" font-size="12" text-anchor="middle">0.4</text> <text x="340" y="292" font-size="13" text-anchor="middle">FRR (놓친 wake word 비율)</text>
<text x="20" y="140" font-size="13" text-anchor="middle" transform="rotate(-90 20 140)">false wake / 24h (로그)</text> <line x1="70" y1="195" x2="610" y2="195" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="6 4"/> <text x="420" y="188" font-size="12">예산: 하루 1회</text>
<polyline fill="none" stroke="#4a7bd0" stroke-width="2" points="73.4,42.5 74.7,50.9 76.3,59.5 78.4,68.3 81.1,77.4 84.5,86.7 88.8,96.2 94.1,106.0 100.7,116.0 108.8,126.2 118.5,136.6 130.2,147.3 144.0,158.2 160.2,169.3 179.0,180.6 200.7,192.2 225.3,204.0 253.1,216.1 284.2,228.3 318.5,240.8"/>
<circle cx="107.8" cy="117.5" r="4" fill="#e08a3c"/> <circle cx="151.0" cy="171.8" r="4" fill="#e08a3c"/> <circle cx="169.9" cy="176.6" r="4" fill="#e08a3c"/> <circle cx="192.8" cy="212.5" r="4" fill="#e08a3c"/>
<circle cx="218.5" cy="229.1" r="4" fill="#e08a3c"/> <text x="116" y="114" font-size="12">4.0</text> <text x="140" y="164" font-size="12">4.5</text> <text x="178" y="172" font-size="12">4.6</text>
<text x="200" y="222" font-size="12">4.7</text> <text x="226" y="240" font-size="12">4.8</text> <text x="330" y="80" font-size="12">파랑 = 이론 곡선 (정규분포 꼬리)</text> <text x="330" y="98" font-size="12">주황 = 100시간 시뮬레이션 측정점</text>
</svg>
```

그림 5 — DET 곡선. 임계값을 올리면 왼쪽 위(오작동 많음)에서 오른쪽 아래(놓침 많음)로 이동한다. 4.7의 측정점은 운 좋게 예산선 아래에 찍혔지만 이론 곡선은 그 FRR에서 아직 예산선 위에 있다 — 측정 시간이 부족하면 이런 착시가 생긴다.

### 9.4 실무에서 더 보는 것

- 배경 오디오는 **현실적이어야** 한다: TV, 대화, 음악, 식당 소음. 조용한 방 100시간은 의미가 없다.
- FRR은 화자·거리·소음(SNR)별로 쪼개서 본다. 평균 FRR 5%가 "어린이 화자 FRR 30%"를 숨길 수 있다.
- 기기에서는 **refractory(불응 기간)**와 2단계 검증(작은 DSP 모델 → 큰 모델 확인)으로 FA를 더 줄인다. 13절 C 코드 참고.

---

## 10. 회귀 지표 — MAE와 RMSE

출력이 연속값(심박수, 걸음 수, 배터리 잔량 추정)이면 회귀 문제다.

```
MAE  = (1/n) · ∑ |ŷᵢ − yᵢ|
RMSE = √( (1/n) · ∑ (ŷᵢ − yᵢ)² )
```

말로 하면: MAE는 "평균적으로 몇 단위 틀리나", RMSE는 큰 오차를 제곱으로 더 세게 벌주는 평균이다. 항상 `RMSE ≥ MAE`이고, 둘의 차이가 크면 가끔 크게 틀리는 샘플이 있다는 뜻이다.

손계산: 실제 심박 {72, 75, 80, 90, 110}, 추정 {70, 78, 80, 85, 90}. 오차 {−2, 3, 0, −5, −20}.

```
MAE  = (2 + 3 + 0 + 5 + 20) / 5            = 30 / 5   = 6.00
RMSE = √((4 + 9 + 0 + 25 + 400) / 5)       = √87.6    ≈ 9.36
```

오차 −20 하나가 RMSE를 MAE보다 56% 크게 만들었다. 운동 중 고심박 구간에서만 크게 틀리는 모델이라는 신호다 — 이것도 "구간별로 쪼개 보기"가 필요한 이유다. (numpy로는 `np.abs(err).mean()`, `np.sqrt((err ** 2).mean())` 두 줄이다.)

---

## 11. 온디바이스 평가 — 배포 후에 다시 잰다

### 11.1 원칙

1. **같은 test set, 같은 지표**로 float 모델과 양자화/컴파일된 모델을 비교한다. 다른 데이터로 재면 차이가 모델 때문인지 데이터 때문인지 모른다.
2. 가능하면 **기기에서 실제로 돌린 출력**(또는 bit-exact 시뮬레이터)으로 잰다. PC의 fake-quant 결과와 NPU 실제 결과가 다를 수 있다(C2, C8).
3. 전체 평균만 보지 말고 **클래스별·사용자별·조건별(소음, 착용 위치)**로 쪼갠다. 양자화 손상은 평균보다 특정 부분집합에 몰린다.
4. 정확도 외에 **latency, 메모리 peak, 에너지/추론(µJ), 평균 전류**도 지표다. 정확도 0.5%p 올리려고 전류가 두 배가 되면 제품에서는 진다(D, K 모듈).

### 11.2 코드로 확인 — 평균이 숨기는 것

다음 코드는 사용자 4명 중 사용자 3의 신호가 1/50 크기로 아주 약한(예: 헐겁게 착용) 상황에서, 이미 학습된 선형 분류기를 int8로 양자화(입력 scale은 전체 데이터 최대값으로 calibration)했을 때 전체/사용자별/클래스별 정확도를 비교한다. 양자화 수식은 C1에서 자세히 다룬다.

```python
import numpy as np
rng = np.random.default_rng(5)
# 사용자 4명: 사용자 3은 신호가 아주 약하다 (예: 헐겁게 착용) → 특징 크기가 1/50
amp = np.array([1.0, 1.0, 1.0, 0.02])
user = np.repeat(np.arange(4), 250); y = rng.integers(0, 2, 1000)
X = (np.stack([y * 2 - 1, -(y * 2 - 1)], 1) * 0.6 + rng.normal(0, 0.5, (1000, 2))) * amp[user, None]
w = np.array([1.0, -1.0]); b = 0.0              # 이미 학습된 선형 분류기라고 가정 (float 기준)

def q8(v, scale):                               # 대칭 int8 양자화 → 다시 float로 (fake quant)
    return np.clip(np.round(v / scale), -127, 127) * scale
s_x = np.abs(X).max() / 127                     # 입력 scale: 전체 데이터의 최대값으로 calibration
p_f = (X @ w + b > 0).astype(int)
p_q = (q8(X, s_x) @ q8(w, np.abs(w).max() / 127) + b > 0).astype(int)

def rep(p, name):
    per_user = [(p[user == u] == y[user == u]).mean() for u in range(4)]
    per_cls = [(p[y == c] == c).mean() for c in (0, 1)]
    print(f"{name}: overall={np.mean(p == y):.3f}  per-user={np.round(per_user, 3).tolist()}  "
          f"per-class={np.round(per_cls, 3).tolist()}")
rep(p_f, "float32"); rep(p_q, "int8   ")
print("input scale =", round(s_x, 4), "  user3 |x| max =", round(np.abs(X[user == 3]).max(), 4))
```

```text
float32: overall=0.960  per-user=[0.96, 0.972, 0.94, 0.968]  per-class=[0.96, 0.96]
int8   : overall=0.949  per-user=[0.96, 0.972, 0.94, 0.924]  per-class=[0.963, 0.933]
input scale = 0.0177   user3 |x| max = 0.0469
```

출력에서 볼 것: 전체 정확도는 96.0% → 94.9%, 1.1%p만 떨어져서 "양자화 OK"로 넘어가기 쉽다. 그러나 사용자 0~2는 **전혀** 변하지 않았고, 손실은 전부 사용자 3(96.8% → 92.4%)과 클래스 1에 몰렸다. 이유는 사용자 3의 입력 최대값 0.047이 scale 0.0177의 2~3칸밖에 안 돼서 대부분의 값이 0, ±1, ±2 정도의 정수로 뭉개지고, 점수가 정확히 0이 되면 `> 0` 판정 때문에 클래스 0으로 쏠리기 때문이다. 펌웨어로 말하면 ADC full-scale을 가장 큰 신호에 맞췄더니 작은 신호가 LSB 몇 개로 떨어진 것과 똑같다.

---

## 12. 실험 위생 — 재현 가능해야 믿을 수 있다

펌웨어에서 "어떤 빌드, 어떤 보드, 어떤 설정으로 잰 숫자인지 모르는 측정값"은 쓸모가 없다. ML도 같다.

- **seed 고정**: `np.random.default_rng(seed)`, `torch.manual_seed(seed)`. GPU는 추가 설정 없이는 완전히 결정적이지 않을 수 있다(`torch.use_deterministic_algorithms(True)`). 중요한 결론은 seed 3~5개로 반복해 평균±표준편차로 말한다. seed 하나에서 0.3%p 이긴 건 이긴 게 아닐 수 있다.
- **config 기록**: 하이퍼파라미터, 데이터 버전, 코드 커밋 해시를 실험마다 파일(YAML/JSON)로 남긴다.
- **dataset versioning**: split 파일(어떤 user_id가 train/val/test인지)을 커밋해서 고정한다. 새 데이터가 들어오면 test를 몰래 바꾸지 않는다 — 새 버전 번호를 붙인다. 도구로는 DVC 같은 것을 쓰거나, 단순히 파일 목록 + 해시를 기록한다.
- **실험 추적**: TensorBoard, MLflow, Weights & Biases 같은 도구로 loss 곡선과 지표를 모은다. 도구보다 중요한 건 "모든 실행이 기록된다"는 습관이다.
- **checkpoint**: best val 시점의 weight + optimizer 상태 + config를 함께 저장한다(A5).

---

## 13. 임베디드 관점에서 다시 보기

평가 개념은 기기에서 이런 모습이 된다: 임계값은 펌웨어 상수나 NVM 파라미터(OTA로 바꿀 수 있게 두면 현장 FA 조정 가능), early stopping은 "어떤 weight 파일을 굽느냐", dropout·augmentation은 export 후 비용 0, 사용자별 편차는 기기 쪽 running mean 보정, FA/hour는 전력 예산 항목, 양자화 후 재평가는 같은 test 벡터를 기기에 흘려 로그를 비교하는 C8의 golden reference 흐름이다.

wake 판정은 모델 점수 뒤에 붙는 **작은 상태 기계**다. 아래 C 코드는 int8 점수 스트림에서 임계값을 넘는 순간 한 번만 트리거하고, 이후 1초(10프레임) 동안은 다시 트리거하지 않는 refractory 로직과 FA 카운터다. 9.3절 Python의 rising-edge 카운트를 기기 쪽에서 구현한 것이다.

```c
#include <stdint.h>
#include <stdio.h>

/* 스트리밍 wake 판정기: int8 점수가 임계값을 넘는 순간 1회 트리거,
   이후 REFRACTORY 프레임 동안은 다시 트리거하지 않는다 (디바운스). */
#define REFRACTORY 10  /* 10 프레임 = 1 s @ 100 ms hop */

typedef struct { int8_t thr; uint16_t hold; uint32_t triggers; } wake_det_t;

static int wake_step(wake_det_t *d, int8_t score) {
    if (d->hold) { d->hold--; return 0; }
    if (score >= d->thr) { d->hold = REFRACTORY; d->triggers++; return 1; }
    return 0;
}

int main(void) {
    /* 배경 소리 로그에서 뽑은 가짜 점수열: 70이 3프레임 연속 → 한 번만 세야 한다 */
    const int8_t log[] = {-80, -60, 70, 75, 72, -50, -90, -85, -40, -30,
                          -20, -60, -70, 90, -10, -100, -100, -100, 65, -5};
    wake_det_t d = { .thr = 64, .hold = 0, .triggers = 0 };
    for (unsigned i = 0; i < sizeof log; i++)
        if (wake_step(&d, log[i])) printf("trigger at frame %u (score %d)\n", i, log[i]);
    /* 이 로그가 2초(20프레임)라면: FA/hour = triggers × 3600 / 2 */
    printf("false accepts = %u  ->  %u per hour (if log = 2 s)\n",
           (unsigned)d.triggers, (unsigned)(d.triggers * 3600u / 2u));
    return 0;
}
```

```text
trigger at frame 2 (score 70)
trigger at frame 13 (score 90)
false accepts = 2  ->  3600 per hour (if log = 2 s)
```

출력에서 볼 것: frame 2~4의 연속 고점수는 한 번만 셌다. frame 13은 frame 2가 건 refractory(frame 3~12)가 끝난 뒤라 새로 트리거됐고, frame 18(65)은 frame 13의 refractory 안이라 무시됐다. `cc -std=c11 -Wall -Wextra -O2`로 경고 0개. 핵심: **평가에 쓰는 카운트 규칙과 기기의 판정 로직이 같아야** PC에서 잰 FA/hour가 현장 숫자와 맞는다. PC 평가는 rising edge만 세는데 기기는 refractory가 없으면, 현장 FA가 더 많이 나온다.

메모리·연산 관점에서 이 평가 로직은 거의 공짜다(구조체 8바이트, 비교 몇 개). 비싼 것은 모델 자체이고, 그 비용(MAC 수, arena 크기, 추론당 에너지)도 정확도와 같은 표에 올려야 한다 — D1·D2·K3에서 계산법을 다룬다.

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 윈도우 단위 랜덤 split | val 98%, 현장 60% | overlap 윈도우·같은 사용자가 양쪽에 | 사용자/기기/세션 단위 split, group k-fold |
| 전체 데이터로 정규화 통계 계산 | val이 살짝 좋게 나옴, 현장과 괴리 | test 통계가 전처리에 샘 | mean/std는 train에서만 계산해 저장 |
| test로 하이퍼파라미터 튜닝 | 보고 숫자 > 새 데이터 숫자 | test가 사실상 val이 됨 | val/k-fold로 튜닝, test는 1회 |
| 불균형 데이터에 accuracy만 보고 | 99%인데 아무것도 못 잡음 | 정확도 역설 | precision/recall/F1, PR-AUC, 클래스별 recall |
| 추론 때 `model.eval()` 누락 | 같은 입력에 출력이 매번 다름 | dropout·BN이 학습 모드 | 평가·export 전 `eval()`, 기기 출력과 비교 |
| val·test에 augmentation 적용 | 점수가 현실과 다름 | 인공 데이터로 평가 | 증강은 train 로더에만 |
| 짧은 배경 오디오로 FAR 주장 | 현장 false wake 폭증 | 드문 사건을 적은 시간으로 측정 | rule of three: 하루 1회 주장엔 ≥ 72 h, 다양한 소음 |
| 양자화 후 전체 평균만 비교 | 특정 사용자·클래스만 망가짐 | scale이 큰 신호에 맞춰짐 | 사용자·클래스별 표, per-channel quant, 입력 정규화 |
| 동작점 없이 AUC만 보고 | 제품 임계값에서 성능 미달 | AUC는 순서만 봄 | 임계값·precision/recall·FA/hour를 함께 |
| seed 1개로 결론 | 재실행하면 순위가 바뀜 | 학습의 무작위성 | seed 3~5개 평균±표준편차 |

---

## 15. 면접에서 이렇게 말한다

**Q.** "Your model is 98% accurate in validation but performs badly in the field. Why, and what do you do?"

**A.** 먼저 누수를 의심한다. 센서 데이터를 윈도우 단위로 섞어서 나누면 겹치는 윈도우와 같은 사용자가 train/val 양쪽에 있어 점수가 부풀려진다. 사용자 단위로 다시 나눠 재평가한다. 그다음 분포 차이(현장 소음, 착용 위치, 기기 편차, 펌웨어 전처리 차이)를 본다. 마지막으로 배포 경로(양자화, 전처리 구현이 PC와 기기에서 같은지)를 같은 입력으로 비교한다.

> "First I'd suspect leakage: if windows were randomly split, overlapping windows and the same users end up in both train and validation, so the number is inflated. I'd re-split by user or device and re-measure. Then I'd look for distribution shift between the lab data and the field — noise, wearing position, device-to-device sensor variation. Finally I'd feed identical inputs through the PC pipeline and the device pipeline, including preprocessing and quantization, and diff the outputs layer by layer."

**Q.** "How would you split IMU data collected from 50 users?"

**A.** 사용자 단위로 나눈다. 먼저 test 사용자 7~8명을 봉인하고, 나머지로 group k-fold를 해서 하이퍼파라미터를 고른다. 윈도우는 split **후에** 자른다. 성별·손목 위치·활동 강도 같은 분포가 fold마다 비슷하게 섞이게 층화하고, 같은 사람이 다른 ID로 들어갔는지도 확인한다.

> "I'd split by user, never by window. I'd hold out about 15% of the users — say 7 or 8 people — as a sealed test set, and run group k-fold over the remaining users for model selection. Windowing happens after the split, so overlapping windows can't cross the boundary. I'd also try to stratify so each fold has a similar mix of wrist side, activity level, and device revision."

**Q.** "Precision vs recall for a wake word — which matters more?"

**A.** 둘 다 중요하지만 always-on 기기에서는 보통 false accept 쪽이 더 비싸다. 혼자 켜지면 사용자 신뢰를 잃고 배터리·프라이버시 문제가 생긴다. 그래서 FA를 "하루 1회 이하" 같은 예산으로 먼저 고정하고, 그 제약 아래에서 FRR(=1−recall)을 최소화한다. 스트리밍이라 precision 대신 FA/hour로 말한다.

> "For an always-on wake word, false accepts are usually the harder constraint: a device that wakes on its own burns power, hurts trust, and raises privacy concerns. So I'd fix a false-accept budget first — for example at most one false wake per 24 hours, measured on at least 72 hours of realistic background audio — and then minimize the false rejection rate at that operating point. Because it's a stream, I'd talk in false accepts per hour rather than precision."

**Q.** "What is AUC?"

**A.** ROC 곡선(임계값을 바꾸며 찍은 FPR 대 TPR) 아래 넓이이고, "무작위 양성 하나가 무작위 음성 하나보다 높은 점수를 받을 확률"과 같다. 임계값과 무관하게 순서 매기는 능력을 본다. 단점은 제품이 쓰는 한 동작점의 성능을 말해 주지 않고, 불균형이 심하면 낙관적이라는 것 — 그래서 PR-AUC와 동작점 지표를 같이 본다.

> "AUC is the area under the ROC curve, which plots true positive rate against false positive rate as you sweep the threshold. It equals the probability that a randomly chosen positive gets a higher score than a randomly chosen negative, so it measures ranking quality independent of the threshold. But the product runs at a single threshold, and with heavy class imbalance ROC can look optimistic, so I'd also report PR-AUC and the precision and recall at the chosen operating point."

**Q.** "You quantized the model to int8 and accuracy dropped only 0.5%. Is it OK to ship?"

**A.** 평균만으로는 판단 못 한다. 같은 test set으로 클래스별·사용자별·조건별 표를 만들고, 손실이 특정 부분집합에 몰렸는지 본다. 실제 기기 출력으로도 확인하고, FA/hour 같은 제품 지표를 다시 측정한다.

> "Not from the average alone. I'd re-run the same test set on the actual device output and break the results down per class, per user, and per condition, because quantization error tends to concentrate — for example on users with weak signals whose inputs fall into a few quantization steps. I'd also re-measure the product metrics, like false wakes per day, since a small score shift near the threshold can move those a lot."

---

## 16. 직접 해보기

1. 양성 20, 음성 180인 test set에서 TP 15, FP 10이다. accuracy, precision, recall, specificity, F1을 손으로 구하라.
정답: FN 5, TN 170 → accuracy 185/200 = 0.925, precision 15/25 = 0.60, recall 15/20 = 0.75, specificity 170/180 ≈ 0.944, F1 = 2·0.6·0.75/1.35 ≈ 0.667.

2. 양성 점수 {0.8, 0.6, 0.3}, 음성 점수 {0.7, 0.5, 0.1, 0.05}의 AUC를 쌍 비교로 구하라.
정답: 12쌍 중 0.8 → 4, 0.6 → 3, 0.3 → 2 = 9/12 = 0.75.

3. 판정 주기 50 ms(초당 20회)인 검출기가 "하루 0.5회 이하" 오작동을 만족하려면 판정당 FP 확률이 대략 얼마 이하여야 하나? 그리고 오작동 0회 관찰로 이를 주장하려면 배경 오디오가 몇 시간 필요한가?
정답: 하루 1,728,000회 판정 → 0.5/1,728,000 ≈ 2.9×10⁻⁷. 3/T ≤ 0.5/24 → T ≥ 144시간.

4. 3.2절 코드에서 (b) 사용자 split을 유지한 채, 각 사용자의 특징에서 그 사용자 전체 윈도우 평균을 빼는 "사용자별 정규화"를 넣고 정확도가 어떻게 변하는지 확인하라. 기기에서는 이 평균을 어떻게 구할 수 있을까?
힌트: `X[user == u] -= X[user == u].mean(0)`. 기기에서는 최근 몇 분의 running mean(지수이동평균)으로 근사한다 — 단, 한 클래스만 계속 나오면 평균이 치우친다는 점도 생각해 보라.

5. 5.3절 코드에 patience 20의 early stopping을 넣어라(val loss가 20 epoch 연속 개선이 없으면 멈추고 best weight 복원). 몇 epoch에서 멈추나?
힌트: `copy.deepcopy(m.state_dict())`로 best를 저장하고 `m.load_state_dict(best)`로 복원한다. 그림 2 곡선을 보면 45 근처가 best이므로 65 근처에서 멈출 것으로 예상된다 — 실제로 돌려서 확인하라.


---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| baseline | 기준 성능 | 다수 클래스·규칙 기반 등 가장 단순한 방법의 점수. 모델은 이걸 이겨야 한다 |
| train / validation / test | 학습 / 검증 / 시험 데이터 | weight 학습 / 설정 선택 / 최종 1회 측정 |
| hyperparameter | 하이퍼파라미터 | 사람이 정하는 학습 설정 (lr, λ, dropout p, epoch) |
| data leakage | 데이터 누수 | 평가 데이터 정보가 학습에 새어 점수가 부풀려지는 것 |
| group split | 그룹 단위 분할 | 사용자·기기·세션 ID 단위로 나눠 같은 그룹이 양쪽에 없게 함 |
| underfitting / overfitting | 과소적합 / 과적합 | train도 못 맞힘(높은 bias) / train 노이즈까지 외워 새 데이터에서 틀림(높은 variance) |
| learning curve | 학습 곡선 | epoch에 따른 train/val loss 그래프 |
| regularization | 규제 | 모델의 자유도를 제한해 overfitting을 줄이는 기법들 |
| weight decay (L2) | 가중치 감쇠 | loss에 λ‖w‖²를 더해 weight를 작게 유지 |
| dropout | 드롭아웃 | 학습 중 뉴런 출력을 확률 p로 0으로 만듦, 추론 때는 끔 |
| early stopping | 조기 종료 | val 지표가 최고였던 시점의 weight를 사용 |
| data augmentation | 데이터 증강 | 라벨을 안 바꾸는 무작위 변형으로 학습 데이터를 불림 (오디오는 SpecAugment) |
| k-fold CV | 교차 검증 | k개 fold를 돌아가며 val로 써서 평균 점수를 봄 |
| confusion matrix | 혼동 행렬 | TP/FN/FP/TN 네 칸 표 |
| precision / recall (TPR) | 정밀도 / 재현율 | 양성 판정 중 진짜 비율 TP/(TP+FP) / 실제 양성 중 잡은 비율 TP/(TP+FN) |
| specificity | 특이도 | 실제 음성 중 음성 판정 비율 TN/(TN+FP) |
| F1 | F1 점수 | precision과 recall의 조화평균 |
| ROC / AUC | ROC 곡선 / 곡선 아래 넓이 | FPR 대 TPR 곡선, AUC = P(양성 점수 > 음성 점수) |
| PR curve / AP | PR 곡선 / 평균 정밀도 | recall 대 precision 곡선, 불균형에 솔직함 |
| FAR / FRR | 오수락 / 오거부 | 이벤트 없는데 트리거된 횟수(보통 FA/hour) / 실제 이벤트를 놓친 비율(1 − recall) |
| DET curve | 검출 오류 trade-off 곡선 | 임계값별 FRR 대 FAR, 보통 로그 축 |
| operating point | 동작점 | 제품에서 실제로 쓸 임계값과 그때의 지표 |
| MAE / RMSE | 평균 절대 오차 / 평균 제곱근 오차 | 회귀 오차. RMSE는 큰 오차를 더 벌줌 |
| rule of three | 3의 법칙 | T 동안 0회 관찰 → 95% 상한 ≈ 3/T |

---

## 18. 요약 & 체크리스트

모델 평가의 핵심은 "처음 보는 사용자·기기·환경에서의 성능"을 정직하게 추정하는 것이다. 베이스라인부터 만들고, 데이터를 사용자 단위로 나눠 누수를 막고, test는 봉인해 마지막에 한 번만 쓴다. loss 곡선에서 overfitting을 읽고 early stopping·weight decay·dropout·센서에 맞는 augmentation으로 누른다. 지표는 문제에 맞게 고른다: 불균형 분류는 precision/recall/F1·PR-AUC, 순서 능력은 ROC-AUC, 스트리밍 검출은 FA/hour와 FRR, 회귀는 MAE/RMSE. 동작점은 제품 예산(예: 하루 1회 오작동)에서 거꾸로 정하고, 드문 사건은 충분한 시간으로 통계적으로 증명한다. 배포 후에는 같은 test set으로 다시 재고, 평균이 아니라 사용자·클래스별로 쪼개 본다.

- [ ] 문제를 입력 윈도우·라벨·판정 주기·성공 기준으로 적고, 다수 클래스·규칙 베이스라인을 만들 수 있다
- [ ] train/val/test 각각의 역할을 설명하고, test를 봉인하는 이유를 golden vector에 빗대 말할 수 있다
- [ ] 센서 데이터에서 윈도우 랜덤 split이 왜 누수인지 설명하고, 사용자 단위 split·group k-fold를 구현할 수 있다
- [ ] train/val loss 곡선에서 underfitting·overfitting·early stopping 지점을 읽을 수 있다
- [ ] L2/weight decay, dropout, early stopping, IMU·오디오 augmentation이 각각 무엇을 막는지 말할 수 있다
- [ ] confusion matrix에서 accuracy, precision, recall, specificity, F1을 손으로 계산할 수 있다
- [ ] 정확도 역설을 예로 들어 불균형 데이터에서 accuracy가 위험한 이유를 설명할 수 있다
- [ ] 작은 예제의 AUC를 쌍 비교로 손계산하고, numpy로 ROC·PR 곡선을 만들 수 있다
- [ ] "하루 1회 이하 false wake"를 판정당 FP 확률과 필요한 테스트 시간(≥ 72 h)으로 바꿀 수 있다
- [ ] 양자화 후 같은 test set으로 재평가하고, 사용자·클래스별 표로 손실이 몰린 곳을 찾을 수 있다

---

## 참고 자료

- Goodfellow, Bengio, Courville, "Deep Learning" (MIT Press, 2016), 5장(Machine Learning Basics: capacity, overfitting, 하이퍼파라미터·validation), 7장(Regularization) — [deeplearningbook.org](https://www.deeplearningbook.org/)
- Zhang, Lipton, Li, Smola, "Dive into Deep Learning" — 모델 선택·underfitting/overfitting, weight decay, dropout 절 — [d2l.ai](https://d2l.ai/)
- scikit-learn User Guide — Cross-validation(`GroupKFold`, `StratifiedGroupKFold`), Model evaluation(precision/recall, ROC, PR) — [scikit-learn.org](https://scikit-learn.org/stable/user_guide.html)
- Park et al., "SpecAugment: A Simple Data Augmentation Method for Automatic Speech Recognition", Interspeech 2019 — [arXiv:1904.08779](https://arxiv.org/abs/1904.08779)
- Bergstra & Bengio, "Random Search for Hyper-Parameter Optimization", JMLR 13 (2012)
- Loshchilov & Hutter, "Decoupled Weight Decay Regularization" (AdamW), ICLR 2019 — [arXiv:1711.05101](https://arxiv.org/abs/1711.05101)
- Srivastava et al., "Dropout: A Simple Way to Prevent Neural Networks from Overfitting", JMLR 15 (2014)
- Fawcett, "An introduction to ROC analysis", Pattern Recognition Letters 27 (2006)
- PyTorch 문서 — `torch.optim.AdamW`, `nn.Dropout`, `Module.eval()` — [pytorch.org/docs](https://pytorch.org/docs/stable/index.html)
