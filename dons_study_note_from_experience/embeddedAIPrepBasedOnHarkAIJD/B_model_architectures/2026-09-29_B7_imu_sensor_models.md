# B7. IMU · 센서 모델 — 활동 인식, 제스처, 착용 감지, 이상 탐지, 고전 ML

> **이 노트를 다 읽으면**: 웨어러블 IMU 과제(HAR·제스처·착용·낙상·이상 탐지)를 입력·창·지연·모델 크기로 분해할 수 있다 · 특징 추출 + decision tree/random forest를 사용자 단위로 평가하고 C 코드로 내보내 golden 비교할 수 있다 · 같은 데이터에서 1D-CNN/GRU와 정확도·크기·MAC을 비교해 "MCU에서 무엇을 쓸지" 근거를 댈 수 있다 · 후처리 FSM, 낙상의 드문 사건 평가, autoencoder 이상 탐지, 착용 방향 문제를 설명할 수 있다
> **JD 연결**: "Hands-on with IMUs, accelerometers, gyroscopes, microphones", "Build data collection and ingestion pipelines … various sensors" — study_prep_list B7: HAR, gesture, wear/on-body detection, fall detection, anomaly detection(autoencoder), **고전 ML**(특징 추출 + decision tree / random forest / gradient boosting)
> **Don 기준 난이도**: 센서 bring-up, FIFO·인터럽트, 고정소수점, FSM·debounce, golden vector 검증은 이미 강하다 / 특징 설계, 트리 계열 모델의 학습 원리, 사용자 단위 평가, 드문 사건 지표, "모델 대신 규칙으로 충분한가"라는 판단은 새로 배운다
> **선행 노트**: A4(평가·leakage·FAR), A6(윈도잉·특징·바이너리 로그), B2(CNN·1D conv), B3(GRU·streaming)

---

## 0. 큰 그림 — 이게 왜 필요한가

웨어러블에서 **가장 오래 켜져 있는 모델은 가장 작은 모델**이다. 음성 비서나 LLM은 사용자가 부를 때만 깨어나지만, "지금 차고 있나?", "손목을 들었나?", "걷는 중인가?", "넘어졌나?"는 **하루 24시간** 판단해야 한다. 그래서 IMU 모델은 수백 바이트~수십 KB, 창 하나에 수백~수십만 연산 규모로 설계되고, 종종 MCU도 아니라 **IMU 칩 안**에서 돈다.

예를 들어 Hark 같은 웨어러블이라면(추정), 전력 계층은 대략 이렇게 생겼을 것이다.

```
 전류 ↑                                    "깨울지 말지"를 단계마다 작은 모델이 결정한다
 ┌────────────────────────────────────────────────────────────────────────────┐
 │ stage 3  SoC / NPU / 무선      mA~수백 mA   음성 AI, LLM, 업로드          │ ← 드물게
 │ stage 2  DSP / 큰 MCU 코어     수 mA        CNN 제스처, KWS              │
 │ stage 1  always-on MCU         수백 µA 이하  특징 + tree/forest, FSM      │ ← 이 노트의 주 무대
 │ stage 0  IMU 내장 로직         µA 단위       wake-on-motion, 내장 ML core  │ ← 항상
 └────────────────────────────────────────────────────────────────────────────┘
   (전류 수치는 부품·모드마다 크게 다르다 — 자릿수 감각용)
```

펌웨어로 말하면 이것은 **인터럽트 계층**이다. 센서의 threshold 인터럽트가 MCU를 깨우고, MCU의 판단이 큰 코어를 깨운다. 각 단계의 모델은 "다음 단계를 깨울지"만 결정하는 **필터**다. 필터가 너무 예민하면(false positive) 배터리가 녹고, 너무 둔하면(false negative) 기능이 안 된다. 이 노트는 그 필터를 만드는 법이다.

이 노트에서 할 일:

1. 과제 카탈로그 — 무엇을, 어떤 창·지연·크기로 (§1)
2. 파이프라인 — raw → 보정·방향 → 창 → 특징/raw → 모델 → 후처리 → 이벤트 (§2~3)
3. **고전 ML 먼저** — 특징 + tree/forest/boosting/logreg/kNN, 사용자 단위 평가, C로 내보내기 (§4~5)
4. 딥러닝과 비교 — 1D-CNN, GRU (§6)
5. 후처리 FSM (§7), 낙상 = 드문 사건 (§8), 이상 탐지 (§9), 견고성 (§10), 임베디드 비용 (§11)

모든 숫자는 합성 데이터에서 나온다. 합성 데이터는 **개념을 보이는 용도**이고, 실제 제품 정확도가 아니다.

---

## 1. 웨어러블 IMU 과제 카탈로그

### 1.1 과제 표

먼저 "무슨 모델을 쓸까"보다 **과제의 모양**을 본다. 입력 채널, 창 길이, 판단 주기, 지연 요구, always-on 여부가 모델 크기를 결정한다.

| 과제 | 입력 | 창 · rate | 지연 요구 | always-on? | 전형적 모델 (대략) |
|---|---|---|---|---|---|
| HAR (still/walk/run/stairs) | 가속도 3축 (+자이로) | 2~5 s 창, 25~100 Hz | 수 초 OK | 예 (저 duty) | 특징 + tree/forest 수 KB, 또는 작은 CNN 수십 KB |
| Step counting | 가속도 크기 (magnitude) | 연속, 25~100 Hz | 수 초 OK | 예 | 규칙(peak 검출 + 주기 검사), 많은 IMU에 내장 |
| Tap / double-tap | 가속도 1축 이상 | 수십~수백 ms, 400 Hz 이상 | 수백 ms | 예 | 규칙(임계값 + 시간창), IMU 내장 기능 흔함 |
| Flick · wrist gesture | 가속도 + 자이로 | 0.5~2 s, 50~100 Hz | 수백 ms | 예 또는 stage 1 뒤 | tree 또는 작은 CNN 수~수십 KB |
| Raise-to-wake | 가속도 (+자이로) | 0.5~1.5 s | 200~500 ms (화면 켜짐 체감) | 예 | 규칙/FSM 또는 작은 tree, IMU 내장 기능도 있음 |
| 착용 감지 (on-body) | 가속도 미세 움직임 + 근접/PPG/온도 | 수 초~수십 초 | 수 초 | 예 | 규칙 + hysteresis, 작은 tree |
| 낙상 감지 | 가속도 (+자이로, 기압) | trigger 전후 수 초 | 수 초~수십 초 (알림) | 예 | trigger 규칙 → 작은 분류기 → 사용자 확인 |
| 이상 탐지 (센서 고장·이상 동작) | 특징 또는 raw | 수 초~수 분 | 느려도 됨 | 선택 | z-score, 작은 autoencoder 수백 파라미터~ |

표를 읽는 법:

- **지연 요구가 짧을수록 창이 짧아지고**, 창이 짧으면 정보가 적어 모델이 어려워진다. raise-to-wake는 사용자가 손목을 들고 0.5초 안에 화면이 켜져야 "빠르다"고 느낀다(체감 기준, 제품마다 다르다).
- **always-on이면 연산 × 호출 빈도가 곧 전류**다. 창마다 1만 연산인 모델을 1초마다 돌리는 것과 10만 연산 모델을 이벤트 때만 돌리는 것은 전혀 다른 예산이다.
- "규칙"이라고 쓴 칸이 많다. **IMU 과제의 상당수는 ML 없이 규칙과 FSM으로 충분**하다. ML은 규칙이 감당 못 하는 변동(사람마다, 착용 방향마다)이 있을 때 들어온다.

### 1.2 분류(classification) vs 이벤트 검출(event detection)

IMU 과제는 두 부류로 나뉜다. 이 구분이 평가 방법을 바꾼다 (A4 §7, §9).

| | 분류 (HAR, 착용 상태) | 이벤트 검출 (제스처, 낙상, tap) |
|---|---|---|
| 출력 | 창마다 라벨 하나 | "지금 이벤트가 일어났다" 한 번 |
| 데이터 | 클래스가 비교적 균형 | 이벤트는 드물고 배경이 압도적 |
| 지표 | accuracy, per-class recall, confusion matrix | recall(검출률) + **false alarm / 시간(또는 /일)** |
| 후처리 | smoothing, 다수결 | hysteresis, refractory(재발동 금지 시간), debounce |

---

## 2. 파이프라인 — raw에서 이벤트까지

### 2.1 블록도

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="b7p-ar" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="80" y="20" font-size="12" text-anchor="middle">G2 · A6 §4</text> <text x="250" y="20" font-size="12" text-anchor="middle">§2.2 · §10</text> <text x="420" y="20" font-size="12" text-anchor="middle">§2.4 · A6 §6</text>
  <text x="590" y="20" font-size="12" text-anchor="middle">§3 · §6</text> <rect x="10" y="28" width="140" height="78" rx="6" fill="none" stroke="#888" stroke-width="1.5"/>
  <text x="80" y="52" font-size="13" text-anchor="middle">raw IMU</text> <text x="80" y="72" font-size="12" text-anchor="middle">FIFO · 50 Hz</text> <text x="80" y="90" font-size="12" text-anchor="middle">int16 → g</text>
  <rect x="180" y="28" width="140" height="78" rx="6" fill="none" stroke="#4a7bd0" stroke-width="1.5"/> <text x="250" y="52" font-size="13" text-anchor="middle">보정 · 방향</text>
  <text x="250" y="72" font-size="12" text-anchor="middle">bias 빼기</text> <text x="250" y="90" font-size="12" text-anchor="middle">크기 · 중력 분해</text>
  <rect x="350" y="28" width="140" height="78" rx="6" fill="none" stroke="#4a7bd0" stroke-width="1.5"/> <text x="420" y="52" font-size="13" text-anchor="middle">윈도잉</text>
  <text x="420" y="72" font-size="12" text-anchor="middle">창 2.56 s</text> <text x="420" y="90" font-size="12" text-anchor="middle">hop 1.28 s</text>
  <rect x="520" y="28" width="150" height="78" rx="6" fill="none" stroke="#4a7bd0" stroke-width="1.5"/> <text x="595" y="52" font-size="13" text-anchor="middle">특징 18개</text>
  <text x="595" y="72" font-size="12" text-anchor="middle">또는 raw 창</text> <text x="595" y="90" font-size="12" text-anchor="middle">[3, 128]</text>
  <line x1="150" y1="67" x2="176" y2="67" stroke="currentColor" stroke-width="1.5" marker-end="url(#b7p-ar)"/> <line x1="320" y1="67" x2="346" y2="67" stroke="currentColor" stroke-width="1.5" marker-end="url(#b7p-ar)"/>
  <line x1="490" y1="67" x2="516" y2="67" stroke="currentColor" stroke-width="1.5" marker-end="url(#b7p-ar)"/> <line x1="595" y1="106" x2="595" y2="164" stroke="currentColor" stroke-width="1.5" marker-end="url(#b7p-ar)"/>
  <rect x="520" y="168" width="150" height="78" rx="6" fill="none" stroke="#3f9a6b" stroke-width="1.5"/> <text x="595" y="192" font-size="13" text-anchor="middle">모델</text>
  <text x="595" y="212" font-size="12" text-anchor="middle">tree · forest · CNN</text> <text x="595" y="230" font-size="12" text-anchor="middle">→ 클래스 확률 p</text>
  <rect x="350" y="168" width="140" height="78" rx="6" fill="none" stroke="#e08a3c" stroke-width="1.5"/> <text x="420" y="192" font-size="13" text-anchor="middle">후처리</text>
  <text x="420" y="212" font-size="12" text-anchor="middle">평균 · 투표</text> <text x="420" y="230" font-size="12" text-anchor="middle">hysteresis FSM</text>
  <rect x="180" y="168" width="140" height="78" rx="6" fill="none" stroke="#d0564a" stroke-width="1.5"/> <text x="250" y="192" font-size="13" text-anchor="middle">이벤트</text>
  <text x="250" y="212" font-size="12" text-anchor="middle">상태가 바뀔 때만</text> <text x="250" y="230" font-size="12" text-anchor="middle">IRQ · 큐 · BLE</text>
  <line x1="520" y1="207" x2="494" y2="207" stroke="currentColor" stroke-width="1.5" marker-end="url(#b7p-ar)"/> <line x1="350" y1="207" x2="324" y2="207" stroke="currentColor" stroke-width="1.5" marker-end="url(#b7p-ar)"/>
  <rect x="10" y="168" width="140" height="78" rx="6" fill="none" stroke="#888" stroke-width="1.5" stroke-dasharray="5 3"/> <text x="80" y="192" font-size="13" text-anchor="middle">stage 0</text>
  <text x="80" y="212" font-size="12" text-anchor="middle">wake-on-motion</text> <text x="80" y="230" font-size="12" text-anchor="middle">IMU 칩 안에서</text>
  <line x1="80" y1="168" x2="80" y2="110" stroke="currentColor" stroke-width="1.2" stroke-dasharray="4 3" marker-end="url(#b7p-ar)"/> <text x="88" y="145" font-size="12">MCU 깨움</text>
  <text x="420" y="284" font-size="12" text-anchor="middle">모델 §4~6 · 후처리 §7 · 이벤트 §7~8</text>
</svg>
```

그림 1 — IMU 모델 파이프라인. 위 줄은 신호 처리, 아래 줄은 판단. 왼쪽 아래 점선 상자는 IMU 칩 안에서 도는 stage 0(wake-on-motion)으로, 움직임이 있을 때만 MCU 파이프라인을 깨운다. 각 상자 위의 § 번호가 이 노트의 해당 절이다.

펌웨어 관점에서 각 단계가 무엇인지:

1. **raw**: IMU FIFO에서 watermark 인터럽트로 읽은 int16 샘플 (G2, A6 §4). 단위 변환 int16 → g.
2. **보정·방향 처리**: bias 빼기, 중력 처리. 이 단계가 "착용 방향이 사람마다 다르다"는 문제를 얼마나 풀어 주느냐가 모델 성능을 크게 좌우한다 (§2.2, §10).
3. **윈도잉**: 연속 스트림을 겹치는 창으로 자른다 (A6 §6). 창 길이 = 정보량, hop = 판단 주기.
4. **특징 또는 raw**: 고전 ML은 창 하나를 숫자 수십 개로 요약하고(§3), 딥러닝은 창을 그대로 넣는다(§6).
5. **모델**: 클래스 확률(또는 점수)을 낸다.
6. **후처리**: 창마다 흔들리는 출력을 안정화한다 (§7). Don이 제일 잘하는 FSM 영역이다.
7. **이벤트**: 상태가 바뀔 때만 상위 계층에 알린다 (인터럽트, 메시지 큐, BLE notify).

### 2.2 보정과 방향 처리 — 중력은 신호이자 잡음이다

가속도계는 **중력 + 움직임 가속도**를 함께 잰다. 가만히 있어도 `|a| = 1 g`이 나온다. 그런데 중력이 3축에 어떻게 나뉘어 찍히는지는 **기기가 어떤 방향으로 붙어 있느냐**에 달려 있다. 손목에 시계를 조금 돌려 찬 사람, 반대 손목에 찬 사람은 같은 걷기를 해도 축별 값이 전혀 다르다.

이걸 다루는 세 가지 도구:

```
(1) 크기(magnitude)     |a| = √(ax² + ay² + az²)               ← 회전해도 안 변함
(2) 중력 제거           g_est = 저역통과(a)  또는 창 평균
                        a_dyn = a − g_est                     ← 움직임만 남음
(3) 중력 기준 분해      ĝ = g_est / |g_est|                    ← 중력 방향 단위벡터
                        v = a · ĝ           (수직 성분, 스칼라)
                        h = |a − v·ĝ|       (수평 성분의 크기)   ← v, h 모두 회전 불변
```

말로 하면: (1) 벡터 길이는 좌표축을 어떻게 돌려도 같다(A1의 직교 행렬). (2) 중력은 거의 DC이므로 느린 성분을 빼면 움직임만 남는다. (3) 중력 방향을 "위"로 삼아 새 좌표계를 만들면, 기기가 어떻게 달려 있든 "위아래 흔들림"과 "수평 흔들림"을 따로 볼 수 있다.

**손계산**: 시계가 x축 기준으로 30° 기울어져 있고 가만히 있다. 중력은 `(0, sin30°, cos30°) = (0, 0.5, 0.866) g`로 찍힌다.

```
|a| = √(0² + 0.5² + 0.866²) = √(0.25 + 0.75) = √1.0 = 1.0 g        ← 기울기와 무관
ĝ   = (0, 0.5, 0.866)
이제 위아래로 0.2 g 흔들림이 더해져 a = 1.2 · ĝ = (0, 0.6, 1.039) 가 됐다면
v   = a · ĝ = 0·0 + 0.6·0.5 + 1.039·0.866 = 0.3 + 0.9 = 1.2 g       ← 수직 1.2 g (중력 1 + 움직임 0.2)
h   = |a − v·ĝ| = |(0, 0.6, 1.039) − (0, 0.6, 1.039)| = 0           ← 수평 움직임 없음
```

축별로 보면 ay가 0.5 → 0.6, az가 0.866 → 1.039로 "둘 다 조금씩" 변한 것처럼 보이지만, 분해하면 "수직으로 0.2 g"라는 물리적 사실이 그대로 나온다. §10에서 이 분해가 착용 방향 문제를 거의 해결하는 것을 코드로 본다.

> 함정: 창 평균으로 중력을 추정하면 **달리기처럼 격한 움직임에서는 평균이 중력과 조금 어긋난다**. 더 정확하게는 자이로를 섞은 센서 퓨전(complementary/Madgwick 필터, G3)으로 자세를 추정한다. MCU 예산이 빠듯하면 창 평균이나 1차 IIR 저역통과로도 충분한 경우가 많다.

bias(영점 오차)는 공장 캘리브레이션 값(G1, G3)을 먼저 빼고, 남는 온도 drift는 §10.4에서 다룬다.

### 2.3 합성 데이터 — 이 노트 전체에서 쓸 손목 IMU

실제 데이터셋 대신 **통제된 합성 데이터**를 만든다. 무엇이 사용자마다 다른지 우리가 정확히 알기 때문에, "왜 이 모델이 새 사용자에서 떨어지는가"를 설명할 수 있다.

설계:

- 50 Hz, 가속도 3축, 단위 g. 창 128 샘플(2.56 s), hop 64(50% 겹침).
- 클래스 4개: still(가만히 — 가끔 손을 움직임), walk(약 1.85 Hz), run(약 2.7 Hz, 큰 충격), stairs(약 1.7 Hz, 고조파가 걷기와 다름).
- **사용자마다 다른 것**: 착용 방향(임의 축으로 최대 40° 회전), 걸음 주파수(±20%), 힘(진폭 ×0.6~1.4), 고조파 비율, 축별 bias(σ 30 mg), 노이즈(10~40 mg).
- 세션마다 자세가 15° 이내로 조금씩 다르고, 걷는 속도가 8~20 s 주기로 천천히 변한다.

다음 모듈이 데이터를 만든다. 뒤의 모든 예제가 이것을 import한다.

```python
# imu_synth.py — 합성 손목 IMU: 가속도 3축 [g], 50 Hz, 사용자별 착용 방향·보행 주파수·힘·bias·노이즈
import numpy as np
FS, WIN, HOP = 50, 128, 64                      # 128 샘플 = 2.56 s 창, 50% 겹침
CLASSES = ["still", "walk", "run", "stairs"]
GAIT = {0: (0.0, 0.00, 0.0, 0.00),              # (보행 주파수 Hz, 수직 진폭 g, 2차 고조파 비, 전후 진폭 g)
        1: (1.85, 0.30, 0.3, 0.15), 2: (2.7, 0.70, 0.6, 0.35), 3: (1.7, 0.35, 0.5, 0.22)}

def rot(rng, max_deg):                          # 임의 축으로 ±max_deg 회전 (Rodrigues 공식)
    k = rng.normal(size=3); k /= np.linalg.norm(k)
    a = np.deg2rad(rng.uniform(-max_deg, max_deg))
    K = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    return np.eye(3) + np.sin(a) * K + (1 - np.cos(a)) * K @ K

def make_user(uid, seed=0, max_tilt=40):
    rng = np.random.default_rng(seed * 1000 + uid)
    return dict(R=rot(rng, max_tilt), fmul=rng.uniform(0.8, 1.2), amul=rng.uniform(0.6, 1.4),
                dh2=rng.uniform(-0.2, 0.2), bias=rng.normal(0, 0.03, 3), noise=rng.uniform(0.01, 0.04))

def session(user, cls, secs, rng, fs=FS):
    t = np.arange(int(secs * fs)) / fs
    f0, A, h2, Af = GAIT[cls]
    f, h2 = f0 * user["fmul"] * rng.uniform(0.95, 1.05), h2 + user["dh2"]
    A = A * user["amul"] * (1 + 0.25 * np.sin(2 * np.pi * t / rng.uniform(8, 20)))  # 속도가 천천히 변함
    body = np.zeros((len(t), 3)); body[:, 2] = 1.0            # 몸 좌표계: z = 위, 중력 1 g
    if cls:
        w = 2 * np.pi * f * t + rng.uniform(0, 2 * np.pi)
        body[:, 2] += A * (np.sin(w) + h2 * np.sin(2 * w + 0.5))  # 수직: 발 디딤
        body[:, 0] += Af * np.sin(w + 1.0)                        # 전후
        body[:, 1] += 0.5 * Af * np.sin(0.5 * w)                  # 좌우: stride(두 걸음) 주기
    else:                                                         # 앉아서 가끔 손을 움직임
        for s0 in rng.integers(0, len(t) - 100, size=int(secs / 10)):
            body[s0:s0 + 75, rng.integers(3)] += rng.uniform(0.1, 0.3) * np.sin(2 * np.pi * rng.uniform(1, 4) * t[:75])
    R = rot(rng, 15) @ user["R"]                                  # 세션마다 자세가 조금 다름
    acc = body @ R.T + user["bias"] + rng.normal(0, user["noise"], body.shape)
    return acc.astype(np.float32)

def dataset(n_users=12, secs=60, seed=0, max_tilt=40):
    X, y, g = [], [], []
    for u in range(n_users):
        user, rng = make_user(u, seed, max_tilt), np.random.default_rng(seed * 7919 + u)
        for c in range(4):
            s = session(user, c, secs, rng)
            idx = np.arange(0, len(s) - WIN + 1, HOP)
            X += [s[i:i + WIN].T for i in idx]; y += [c] * len(idx); g += [u] * len(idx)
    return np.stack(X), np.array(y), np.array(g)                  # X [N, 3, WIN]
```

`rot`의 Rodrigues 공식 `R = I + sin θ·K + (1 − cos θ)·K²`는 "단위축 k를 중심으로 θ만큼 돌리는 회전 행렬"이다. `K`는 외적 `k × v`를 행렬로 쓴 것. 지금은 "임의 방향으로 기기를 돌려 찬다"를 만드는 도구로만 쓰면 된다 (G3에서 자세히).

이 코드는 데이터를 만들고, 사용자별 착용 방향 차이와 클래스별 움직임 크기를 확인한다.

```python
import numpy as np
from imu_synth import dataset, CLASSES, WIN, FS
X, y, g = dataset()
print("X", X.shape, X.dtype, " y", y.shape, " users", np.unique(g).size)
print("windows per class:", np.bincount(y), f" window = {WIN/FS:.2f} s")
mag = np.linalg.norm(X, axis=1)                      # [N, L]
for u in [0, 1, 2]:
    s = (g == u) & (y == 0)                          # 가만히 있는 창만
    print(f"user {u} still: mean(ax,ay,az) = {np.round(X[s].mean((0, 2)), 2)}  mean|a| = {mag[s].mean():.3f}")
for c, name in enumerate(CLASSES):
    print(f"{name:6s} std|a| = {mag[y == c].std(-1).mean():.3f} g")
```

```text
X (2160, 3, 128) float32  y (2160,)  users 12
windows per class: [540 540 540 540]  window = 2.56 s
user 0 still: mean(ax,ay,az) = [0.26 0.05 0.93]  mean|a| = 0.972
user 1 still: mean(ax,ay,az) = [ 0.53 -0.02  0.87]  mean|a| = 1.018
user 2 still: mean(ax,ay,az) = [0.42 0.13 0.89]  mean|a| = 0.999
still  std|a| = 0.036 g
walk   std|a| = 0.243 g
run    std|a| = 0.547 g
stairs std|a| = 0.302 g
```

출력에서 볼 것: 세 사용자가 똑같이 가만히 있는데 축별 평균(중력이 찍히는 방향)은 `ax` 0.26~0.53으로 크게 다르다. 반면 `|a|`는 모두 1 g 근처다. 클래스별 `|a|`의 흔들림(std)은 still < walk < stairs < run 순서지만 walk(0.243)와 stairs(0.302)는 가깝다 — 이 둘을 가르는 것이 이 데이터에서 제일 어려운 부분이 된다.

```svg
<svg viewBox="0 0 640 400" xmlns="http://www.w3.org/2000/svg">
  <line x1="90" y1="63.6" x2="620" y2="63.6" stroke="currentColor" stroke-width="0.5" stroke-dasharray="3 3"/> <line x1="90" y1="20" x2="90" y2="100" stroke="currentColor" stroke-width="1"/>
  <text x="82" y="67.6" font-size="12" text-anchor="end">1 g</text> <text x="82" y="31.3" font-size="12" text-anchor="end">2 g</text> <text x="10" y="56" font-size="13">still</text> <text x="10" y="74" font-size="12">std 0.07</text>
  <polyline fill="none" stroke="#888" stroke-width="1.5" points="90.0,65.3 94.2,64.8 98.3,65.3 102.5,64.1 106.7,64.0 110.9,62.5 115.0,63.9 119.2,62.3 123.4,63.5 127.6,64.3 131.7,64.6 135.9,65.4 140.1,62.8 144.3,65.2 148.4,63.3 152.6,64.6 156.8,62.3 160.9,64.1 165.1,64.3 169.3,66.3 173.5,64.4 177.6,62.3 181.8,65.2 186.0,63.7 190.2,66.3 194.3,63.9 198.5,63.8 202.7,64.4 206.9,66.1 211.0,64.5 215.2,62.8 219.4,63.0 223.5,64.3 227.7,66.1 231.9,65.9 236.1,64.8 240.2,64.5 244.4,62.9 248.6,64.9 252.8,64.1 256.9,66.9 261.1,63.5 265.3,65.9 269.4,66.7 273.6,66.1 277.8,65.8 282.0,62.7 286.1,66.6 290.3,61.3 294.5,61.4 298.7,62.7 302.8,60.9 307.0,60.6 311.2,61.6 315.4,63.1 319.5,66.8 323.7,66.4 327.9,67.8 332.0,69.4 336.2,68.0 340.4,67.7 344.6,65.9 348.7,66.4 352.9,63.4 357.1,63.9 361.3,62.8 365.4,61.7 369.6,60.5 373.8,62.5 378.0,60.9 382.1,66.6 386.3,67.2 390.5,65.5 394.6,67.7 398.8,68.8 403.0,69.0 407.2,66.6 411.3,66.3 415.5,66.1 419.7,64.4 423.9,63.1 428.0,62.5 432.2,60.7 436.4,61.5 440.6,63.4 444.7,62.9 448.9,68.1 453.1,67.2 457.2,68.9 461.4,67.7 465.6,69.2 469.8,67.3 473.9,66.4 478.1,67.3 482.3,64.8 486.5,63.7 490.6,60.4 494.8,61.5 499.0,60.7 503.1,61.1 507.3,61.3 511.5,63.9 515.7,64.1 519.8,66.9 524.0,66.2 528.2,68.7 532.4,68.3 536.5,68.7 540.7,68.3 544.9,66.1 549.1,64.2 553.2,61.5 557.4,60.7 561.6,62.4 565.7,61.8 569.9,61.6 574.1,63.4 578.3,63.1 582.4,66.6 586.6,63.7 590.8,63.3 595.0,60.9 599.1,57.6 603.3,59.6 607.5,61.8 611.7,65.5 615.8,71.4 620.0,70.0"/>
  <line x1="90" y1="153.6" x2="620" y2="153.6" stroke="currentColor" stroke-width="0.5" stroke-dasharray="3 3"/> <line x1="90" y1="110" x2="90" y2="190" stroke="currentColor" stroke-width="1"/>
  <text x="82" y="157.6" font-size="12" text-anchor="end">1 g</text> <text x="82" y="121.3" font-size="12" text-anchor="end">2 g</text> <text x="10" y="146" font-size="13">walk</text>
  <text x="10" y="164" font-size="12">std 0.35</text>
  <polyline fill="none" stroke="#4a7bd0" stroke-width="1.5" points="90.0,172.5 94.2,169.9 98.3,165.4 102.5,159.2 106.7,154.3 110.9,145.6 115.0,144.6 119.2,139.8 123.4,138.9 127.6,137.6 131.7,140.7 135.9,142.5 140.1,145.6 144.3,148.9 148.4,147.3 152.6,149.5 156.8,152.3 160.9,151.8 165.1,151.7 169.3,158.6 173.5,161.2 177.6,167.6 181.8,171.7 186.0,174.2 190.2,177.3 194.3,173.3 198.5,169.1 202.7,162.7 206.9,157.5 211.0,153.0 215.2,144.0 219.4,139.2 223.5,138.7 227.7,138.1 231.9,141.2 236.1,142.6 240.2,145.9 244.4,149.4 248.6,150.1 252.8,149.8 256.9,149.5 261.1,152.7 265.3,156.2 269.4,157.8 273.6,161.3 277.8,167.4 282.0,169.3 286.1,174.4 290.3,176.4 294.5,174.7 298.7,170.4 302.8,168.7 307.0,157.9 311.2,151.3 315.4,143.0 319.5,139.9 323.7,137.2 327.9,137.4 332.0,138.6 336.2,139.4 340.4,143.5 344.6,144.8 348.7,146.1 352.9,147.7 357.1,150.5 361.3,150.6 365.4,153.2 369.6,155.5 373.8,160.7 378.0,165.9 382.1,169.7 386.3,173.9 390.5,175.9 394.6,174.3 398.8,174.8 403.0,168.8 407.2,160.7 411.3,153.1 415.5,147.5 419.7,141.1 423.9,139.4 428.0,137.2 432.2,136.6 436.4,135.9 440.6,143.6 444.7,144.7 448.9,146.3 453.1,150.7 457.2,151.3 461.4,148.6 465.6,152.8 469.8,154.2 473.9,160.5 478.1,164.9 482.3,167.3 486.5,174.5 490.6,175.8 494.8,173.9 499.0,175.3 503.1,170.6 507.3,163.5 511.5,155.0 515.7,147.2 519.8,141.2 524.0,135.6 528.2,137.9 532.4,135.3 536.5,139.0 540.7,142.4 544.9,143.7 549.1,145.4 553.2,149.1 557.4,148.7 561.6,147.1 565.7,151.8 569.9,155.2 574.1,159.0 578.3,162.3 582.4,167.6 586.6,174.8 590.8,174.8 595.0,174.9 599.1,174.3 603.3,171.9 607.5,166.1 611.7,159.1 615.8,149.7 620.0,144.2"/>
  <line x1="90" y1="243.6" x2="620" y2="243.6" stroke="currentColor" stroke-width="0.5" stroke-dasharray="3 3"/> <line x1="90" y1="200" x2="90" y2="280" stroke="currentColor" stroke-width="1"/>
  <text x="82" y="247.6" font-size="12" text-anchor="end">1 g</text> <text x="82" y="211.3" font-size="12" text-anchor="end">2 g</text> <text x="10" y="236" font-size="13">run</text>
  <text x="10" y="254" font-size="12">std 0.62</text>
  <polyline fill="none" stroke="#d0564a" stroke-width="1.5" points="90.0,240.1 94.2,214.4 98.3,202.6 102.5,202.5 106.7,217.0 110.9,230.3 115.0,240.4 119.2,238.2 123.4,235.4 127.6,236.2 131.7,244.5 135.9,264.3 140.1,265.9 144.3,264.2 148.4,271.0 152.6,262.3 156.8,229.5 160.9,207.8 165.1,197.0 169.3,208.2 173.5,222.8 177.6,235.1 181.8,237.9 186.0,233.7 190.2,229.8 194.3,236.2 198.5,253.0 202.7,269.4 206.9,264.6 211.0,261.9 215.2,274.7 219.4,249.7 223.5,222.0 227.7,200.9 231.9,200.6 236.1,212.3 240.2,226.7 244.4,236.0 248.6,237.5 252.8,232.9 256.9,232.6 261.1,240.1 265.3,257.8 269.4,269.6 273.6,259.8 277.8,263.3 282.0,270.4 286.1,242.6 290.3,212.5 294.5,195.2 298.7,200.2 302.8,213.5 307.0,231.7 311.2,239.9 315.4,237.7 319.5,230.7 323.7,232.4 327.9,247.9 332.0,267.8 336.2,266.1 340.4,257.6 344.6,266.2 348.7,264.4 352.9,230.6 357.1,205.9 361.3,193.7 365.4,202.7 369.6,222.4 373.8,234.9 378.0,239.0 382.1,235.0 386.3,231.6 390.5,234.7 394.6,252.8 398.8,268.9 403.0,259.8 407.2,258.3 411.3,272.3 415.5,253.1 419.7,221.2 423.9,194.6 428.0,194.6 432.2,207.1 436.4,226.2 440.6,238.0 444.7,237.1 448.9,234.8 453.1,231.2 457.2,242.0 461.4,259.6 465.6,267.7 469.8,259.0 473.9,259.3 478.1,271.7 482.3,242.1 486.5,212.5 490.6,194.9 494.8,197.8 499.0,212.9 503.1,229.2 507.3,238.0 511.5,236.5 515.7,231.9 519.8,233.5 524.0,247.0 528.2,266.2 532.4,261.2 536.5,252.8 540.7,263.9 544.9,264.7 549.1,230.3 553.2,206.0 557.4,194.6 561.6,200.7 565.7,217.3 569.9,231.8 574.1,237.9 578.3,233.5 582.4,229.5 586.6,235.5 590.8,253.1 595.0,271.2 599.1,258.1 603.3,255.5 607.5,266.9 611.7,251.0 615.8,218.8 620.0,196.4"/>
  <line x1="90" y1="333.6" x2="620" y2="333.6" stroke="currentColor" stroke-width="0.5" stroke-dasharray="3 3"/> <line x1="90" y1="290" x2="90" y2="370" stroke="currentColor" stroke-width="1"/>
  <text x="82" y="337.6" font-size="12" text-anchor="end">1 g</text> <text x="82" y="301.3" font-size="12" text-anchor="end">2 g</text> <text x="10" y="326" font-size="13">stairs</text>
  <text x="10" y="344" font-size="12">std 0.44</text>
  <polyline fill="none" stroke="#e08a3c" stroke-width="1.5" points="90.0,329.7 94.2,330.9 98.3,328.7 102.5,328.9 106.7,328.2 110.9,331.3 115.0,337.4 119.2,340.1 123.4,347.8 127.6,352.3 131.7,357.3 135.9,360.9 140.1,358.3 144.3,353.0 148.4,346.1 152.6,337.5 156.8,329.0 160.9,318.0 165.1,312.9 169.3,311.4 173.5,314.3 177.6,316.0 181.8,320.7 186.0,325.0 190.2,327.7 194.3,331.8 198.5,331.9 202.7,330.4 206.9,330.1 211.0,330.1 215.2,330.2 219.4,331.6 223.5,339.1 227.7,345.5 231.9,354.2 236.1,355.8 240.2,360.8 244.4,363.1 248.6,355.1 252.8,348.5 256.9,340.2 261.1,329.3 265.3,321.6 269.4,316.2 273.6,309.9 277.8,310.3 282.0,315.5 286.1,319.2 290.3,319.6 294.5,326.0 298.7,329.7 302.8,330.4 307.0,332.1 311.2,326.7 315.4,328.0 319.5,330.2 323.7,331.3 327.9,336.3 332.0,343.6 336.2,351.8 340.4,354.6 344.6,362.7 348.7,364.2 352.9,360.4 357.1,351.5 361.3,346.3 365.4,335.8 369.6,325.5 373.8,316.1 378.0,311.7 382.1,309.1 386.3,311.0 390.5,311.8 394.6,319.3 398.8,323.6 403.0,326.6 407.2,328.4 411.3,328.4 415.5,331.6 419.7,328.4 423.9,328.1 428.0,331.5 432.2,332.8 436.4,343.4 440.6,348.3 444.7,355.7 448.9,359.4 453.1,364.0 457.2,364.8 461.4,359.5 465.6,350.1 469.8,340.6 473.9,329.4 478.1,320.6 482.3,312.3 486.5,309.3 490.6,309.9 494.8,311.9 499.0,316.3 503.1,321.1 507.3,328.5 511.5,330.4 515.7,329.6 519.8,329.6 524.0,330.7 528.2,329.7 532.4,329.8 536.5,332.4 540.7,339.5 544.9,344.4 549.1,353.5 553.2,357.6 557.4,362.8 561.6,365.0 565.7,359.5 569.9,354.2 574.1,348.1 578.3,336.8 582.4,324.1 586.6,316.2 590.8,311.5 595.0,310.1 599.1,310.0 603.3,314.3 607.5,319.1 611.7,324.7 615.8,325.7 620.0,330.3"/>
  <line x1="90" y1="380" x2="620" y2="380" stroke="currentColor" stroke-width="1"/> <line x1="90.0" y1="380" x2="90.0" y2="385" stroke="currentColor"/><text x="90.0" y="397" font-size="12" text-anchor="middle">0 s</text>
  <line x1="194.3" y1="380" x2="194.3" y2="385" stroke="currentColor"/><text x="194.3" y="397" font-size="12" text-anchor="middle">0.5 s</text>
  <line x1="298.7" y1="380" x2="298.7" y2="385" stroke="currentColor"/><text x="298.7" y="397" font-size="12" text-anchor="middle">1 s</text>
  <line x1="403.0" y1="380" x2="403.0" y2="385" stroke="currentColor"/><text x="403.0" y="397" font-size="12" text-anchor="middle">1.5 s</text>
  <line x1="507.3" y1="380" x2="507.3" y2="385" stroke="currentColor"/><text x="507.3" y="397" font-size="12" text-anchor="middle">2 s</text>
  <line x1="611.7" y1="380" x2="611.7" y2="385" stroke="currentColor"/><text x="611.7" y="397" font-size="12" text-anchor="middle">2.5 s</text>
</svg>
```

그림 2 — 사용자 0의 클래스별 첫 창에서 `|a|` 파형(실제 합성 값, 2.56 s). 점선은 1 g. still은 1 g 근처에서 거의 평평하고, run은 진폭과 주파수가 모두 크다. walk와 stairs는 주파수가 비슷하고 파형 모양(고조파)이 조금 다르다.

### 2.4 창 길이와 hop 고르기

A6 §6에서 창 개수 공식과 `[N, C, L]` 만들기를 다뤘다. 여기서는 **길이를 어떻게 정하느냐**만 보탠다.

- **창에 주기가 최소 2번 들어가야** 주파수 특징이 의미가 있다. 걸음 주기는 약 0.5 s(2 Hz)이므로 2.56 s 창에 약 5걸음이 들어간다. 가장 느린 계단 오르기(약 1.4 Hz)도 3번 이상 들어간다.
- **FFT 해상도** `Δf = fs / L = 50 / 128 = 0.39 Hz`. walk(1.85)와 stairs(1.7)의 차이 0.15 Hz는 이 해상도보다 작다 — 주파수 하나만으로는 둘을 못 가른다는 뜻이다(§3에서 확인).
- **판단 지연** ≈ 창 길이 + 처리 시간. 2.56 s 창이면 "걷기 시작"을 알아채는 데 1~2.5 s가 걸린다. HAR에는 괜찮고, raise-to-wake에는 너무 길다.
- **hop** = 판단 주기 = 모델 호출 빈도 = 전류. hop 1.28 s면 초당 0.78회 호출이다.

> 함정: 창이 겹치면(50%) 이웃 창이 샘플의 절반을 공유한다. 그래서 창 단위로 무작위 split을 하면 test 창의 절반이 train에 들어 있다 — A4 §3, A6 §6.4의 leakage. §4에서 숫자로 본다.

---

## 3. 특징 추출 — 시간 영역 + 간단한 주파수 영역

### 3.1 무엇을 뽑나

고전 ML은 창 하나(3 × 128 = 384개 숫자)를 **사람이 설계한 요약값** 수십 개로 줄여서 쓴다. 특징 하나하나가 물리적 의미를 가진다는 것이 장점이다. mean/std/RMS/zero-crossing의 정의와 벡터화 코드는 A6 §7에 있으므로, 여기서는 이 노트에서 쓰는 18개를 표로만 정리한다.

| 특징 | 개수 | 물리적 의미 | 착용 방향에 |
|---|---|---|---|
| `mean_x/y/z` | 3 | 중력이 어느 축에 찍히나 = 자세 | 민감 |
| `std_x/y/z` | 3 | 축별 움직임 에너지 | 민감 |
| `p2p_x/y/z` | 3 | 축별 진폭 (max − min) | 민감 |
| `mag_mean`, `mag_std`, `mag_p2p` | 3 | 가속도 크기(mag)의 평균·흔들림·진폭 | **불변** |
| `mag_zc` | 1 | mag가 평균을 가로지른 횟수 ≈ 2 × 주파수 × 창 길이 | 불변 |
| `dom_freq` | 1 | mag 스펙트럼에서 가장 센 주파수 | 불변 |
| `band_lo/mid/hi` | 3 | 0.5–1.8 / 1.8–2.6 / 2.6–6 Hz 대역 에너지 비율 | 불변 |
| `log_energy` | 1 | 0.5 Hz 이상 전체 에너지(log10) | 불변 |

### 3.2 주파수 특징 — FFT 한 번으로 얻는 것

**FFT**(fast Fourier transform)는 길이 L 신호를 L/2 + 1개 주파수 성분으로 바꾼다(G5에서 자세히). 여기서 필요한 것은 세 가지뿐이다.

```
bin k 의 주파수         f_k = k · fs / L          (k = 0 … L/2)
power spectrum         P_k = |FFT(x · w)_k|²     (w = Hann window, 창 경계 불연속 완화)
대역 에너지 비율        band(lo, hi) = ∑_{lo ≤ f_k < hi} P_k / ∑_{f_k ≥ 0.5} P_k
```

말로 하면: 신호를 "어느 주파수에 에너지가 얼마나 있나"로 바꾸고, 사람 움직임이 사는 0.5~6 Hz를 세 칸으로 나눠 비율을 본다. 비율을 쓰면 진폭(사람의 힘)과 무관해진다.

**손계산**: `|a| = 1 + 0.3·sin(2π·2t)` (2 Hz, 0.3 g 흔들림)를 2.56 s 창에 넣으면 기대값은 이렇다.

```
mean      = 1.0 g                                    (sin의 평균 ≈ 0)
std       = 0.3 / √2 = 0.212 g                       (사인파 RMS = 진폭 / √2)
p2p       = 0.6 g
zc        ≈ 2 × 2 Hz × 2.56 s = 10.24 회
dom_freq  = 2 Hz 에 가장 가까운 bin: k = 2 / 0.390625 = 5.12 → k = 5 → 1.953 Hz
```

이 모듈이 18개 특징을 한 번에 계산하고, 다음 코드가 위 손계산을 확인한다.

```python
# imu_feat.py — 창 하나당 18개 hand-crafted 특징 (시간 영역 13 + 주파수 영역 5)
import numpy as np
from imu_synth import FS
NAMES = ["mean_x", "mean_y", "mean_z", "std_x", "std_y", "std_z", "p2p_x", "p2p_y", "p2p_z",
         "mag_mean", "mag_std", "mag_p2p", "mag_zc", "dom_freq",
         "band_lo", "band_mid", "band_hi", "log_energy"]

def features(X, fs=FS):                                   # X [N, 3, L] → F [N, 18]
    mag = np.linalg.norm(X, axis=1)                        # |a| [N, L] — 착용 방향과 무관
    m = mag - mag.mean(-1, keepdims=True)                  # 중력(DC) 제거
    zc = (np.diff(np.sign(m), axis=-1) != 0).sum(-1)       # 평균 기준 zero-crossing 수
    spec = np.abs(np.fft.rfft(m * np.hanning(m.shape[-1]), axis=-1)) ** 2   # power spectrum
    freqs = np.fft.rfftfreq(m.shape[-1], 1 / fs)           # Δf = fs / L = 0.39 Hz
    tot = spec[:, freqs >= 0.5].sum(-1) + 1e-9
    band = lambda lo, hi: spec[:, (freqs >= lo) & (freqs < hi)].sum(-1) / tot
    dom = freqs[1:][np.argmax(spec[:, 1:], -1)]            # 가장 센 주파수 (DC 제외)
    return np.column_stack([X.mean(-1), X.std(-1), np.ptp(X, -1),
                            mag.mean(-1), mag.std(-1), np.ptp(mag, -1), zc, dom,
                            band(0.5, 1.8), band(1.8, 2.6), band(2.6, 6.0), np.log10(tot)]).astype(np.float32)
```

```python
import numpy as np
from imu_feat import features, NAMES
t = np.arange(128) / 50                                  # 2.56 s @ 50 Hz
x = np.zeros((1, 3, 128), np.float32)
x[0, 2] = 1.0 + 0.3 * np.sin(2 * np.pi * 2.0 * t)        # 중력 1 g + 2 Hz, 0.3 g 수직 흔들림
f = features(x)[0]
for n in ["mean_z", "std_z", "p2p_z", "mag_mean", "mag_std", "mag_zc", "dom_freq", "band_mid", "band_lo"]:
    print(f"{n:9s} = {f[NAMES.index(n)]:.3f}")
print("frequency resolution Δf =", 50 / 128, "Hz → bins near 2 Hz:", [round(k * 50 / 128, 3) for k in (4, 5, 6)])
```

```text
mean_z    = 1.002
std_z     = 0.210
p2p_z     = 0.599
mag_mean  = 1.002
mag_std   = 0.210
mag_zc    = 11.000
dom_freq  = 1.953
band_mid  = 0.884
band_lo   = 0.115
frequency resolution Δf = 0.390625 Hz → bins near 2 Hz: [1.562, 1.953, 2.344]
```

출력에서 볼 것: std 0.210(손계산 0.212), zc 11(손계산 10.24), dom_freq 1.953 Hz(bin 5)로 손계산과 맞는다. 작은 차이는 창에 사인파가 정수 주기(5.12 주기)로 들어가지 않아서 생긴다. 순수한 2 Hz인데도 에너지의 11.5%가 `band_lo`(0.5~1.8 Hz)로 새어 나갔다 — **spectral leakage**다. 2 Hz가 bin 정중앙에 있지 않고 Hann window의 main lobe가 이웃 bin까지 퍼지기 때문이다. 대역 경계를 신호 주파수 바로 옆에 두면 특징이 불안정해진다.

### 3.3 특징 설계의 함정

- **축별 특징은 착용 방향을 외운다**. `mean_x`가 좋은 특징으로 뽑히면, 그건 "이 사용자들이 이 방향으로 찼다"를 배운 것일 수 있다 (§4.4, §10).
- **주파수 해상도보다 가까운 클래스는 dom_freq로 못 가른다** (walk 1.85 vs stairs 1.7 Hz, Δf 0.39 Hz). 창을 늘리거나(지연↑), 파형 모양(고조파 비율, 대역 비율)을 본다.
- **train과 펌웨어의 특징 정의가 한 비트라도 다르면** 모델이 조용히 틀린다: std의 ddof(A6 §7.3), Hann window 유무, 창 정렬, 단위(g vs m/s²). 특징 코드는 golden vector로 비교한다 (A6 §7.4, §11.2).

---

## 4. 고전 ML 먼저 — 특징 + 트리 계열 모델

### 4.1 왜 "고전 ML 먼저"인가

IMU 과제에서 첫 모델은 거의 항상 **특징 + 작은 트리**다. 이유는 네 가지다.

1. **데이터가 적다**. 센서 데이터는 사람을 모아서 녹화하고 라벨을 붙여야 한다(H4). 수십 명 수준의 데이터에서는 사람이 설계한 특징이 딥러닝보다 강한 경우가 많다 (§6에서 직접 본다).
2. **모델이 작다**. decision tree 하나는 수백 바이트다. RAM이 수십 KB인 MCU, 심지어 IMU 칩 안에서도 돈다.
3. **곱셈이 없다**. 트리 추론은 "비교 → 분기"의 반복이다. FPU가 없어도, 정수 비교로도 된다.
4. **읽을 수 있다**. "mag_std ≤ 0.15 g면 가만히 있음"은 펌웨어 엔지니어가 검토하고 디버깅할 수 있는 규칙이다.

### 4.2 다섯 가지 모델 — 한 줄 직관

| 모델 | 예측하는 방법 | 저장하는 것 | 추론 비용 | MCU 적합도 |
|---|---|---|---|---|
| Decision tree | 특징 하나와 임계값을 비교하며 트리를 내려감 → leaf의 클래스 | 노드마다 (특징 번호, 임계값, 자식 2개) | 깊이 d번 비교 | 매우 좋음 |
| Random forest | 서로 다르게 학습한 트리 T개가 투표 (확률 평균) | 트리 T개 | T × d번 비교 | 좋음 (T·깊이 조절) |
| Gradient boosting | 앞 트리의 오차를 다음 트리가 보정, 점수를 더함 | 트리 (클래스 수 × 라운드 수)개 | 트리 수 × d 비교 + 덧셈 | 좋음 |
| Logistic regression | 특징의 가중합 → softmax | 가중치 행렬 (클래스 × 특징) | 클래스 × 특징 MAC | 매우 좋음 |
| kNN | 학습 데이터 중 가장 가까운 k개의 다수결 | **학습 데이터 전체** | N × 특징 거리 계산 | 나쁨 |

- **Random forest**는 각 트리를 학습 데이터의 무작위 부분집합(bootstrap)과 노드마다 무작위 특징 부분집합으로 학습시킨다. 트리 하나하나는 overfit하지만 서로 다르게 틀리기 때문에 평균하면 분산이 줄어든다. 펌웨어로 치면 **독립적인 센서 여러 개의 다수결**이다.
- **Gradient boosting**은 얕은 트리(깊이 3 정도)를 순서대로 쌓는다. 트리 m은 "지금까지의 합이 틀린 방향"(loss의 gradient)을 맞추도록 학습된다. 제어 루프로 치면 **잔차를 조금씩 보정하는 반복**이다. 다중 클래스면 라운드마다 클래스 수만큼 트리를 만든다.
- **kNN**은 학습이 없다(데이터 저장만). 대신 추론 때 저장된 모든 점과 거리를 잰다. MCU에는 거의 맞지 않지만, "특징 공간에서 가까운 것은 같은 클래스"라는 좋은 기준선이 된다.

### 4.3 Decision tree는 어떻게 자라나 — Gini impurity 손계산

트리는 노드마다 "어떤 특징을 어떤 임계값으로 나누면 가장 **깨끗하게** 갈리나"를 탐욕적으로(greedy) 고른다. 깨끗함의 척도가 **Gini impurity**다.

```
Gini(노드) = 1 − ∑_c p_c²          p_c = 노드 안에서 클래스 c 의 비율
분할 이득   = Gini(부모) − [ (n_L/n)·Gini(왼쪽) + (n_R/n)·Gini(오른쪽) ]
```

말로 하면: 노드 안이 한 클래스뿐이면 Gini = 0(완전히 깨끗), 반반 섞이면 두 클래스일 때 0.5(가장 더러움). 분할 후 자식들의 가중 평균 Gini가 가장 많이 줄어드는 (특징, 임계값)을 고른다.

**손계산**: 창 10개(still 4, walk 6)가 있다.

```
부모:  p = (0.4, 0.6)   Gini = 1 − (0.16 + 0.36) = 0.48
후보 분할 "mag_std ≤ 0.1":
  왼쪽  5개 = still 4, walk 1   Gini = 1 − (0.8² + 0.2²) = 1 − 0.68 = 0.32
  오른쪽 5개 = still 0, walk 5   Gini = 0
  가중 평균 = 0.5·0.32 + 0.5·0 = 0.16
  이득 = 0.48 − 0.16 = 0.32
```

학습기는 모든 특징 × 모든 후보 임계값(정렬된 값 사이의 중간점)에 대해 이 이득을 계산하고 최대인 것을 고른 뒤, 자식 노드에서 같은 일을 반복한다. `max_depth`, `min_samples_leaf`로 멈추지 않으면 leaf마다 샘플 하나가 남을 때까지 자라서 **학습 데이터를 외운다**(overfitting, A4 §4).

### 4.4 다섯 모델 비교 — 사용자 단위 평가 (GroupKFold)

A4 §3에서 본 것처럼 센서 데이터는 **같은 사람의 창이 train과 test에 동시에 들어가면 점수가 부풀려진다**. scikit-learn의 `GroupKFold`는 `groups`(여기서는 사용자 ID)가 같은 샘플을 항상 같은 fold에 넣는다. 12명을 4-fold로 나누면 매번 9명으로 학습하고 **처음 보는 3명**으로 평가한다.

이 코드는 다섯 모델을 사용자 단위 4-fold와 창 단위 무작위 4-fold로 각각 평가한다.

```python
import numpy as np, time
from sklearn.model_selection import GroupKFold, KFold, cross_val_score
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.tree import DecisionTreeClassifier
from sklearn.ensemble import RandomForestClassifier, GradientBoostingClassifier
from sklearn.linear_model import LogisticRegression
from sklearn.neighbors import KNeighborsClassifier
from imu_synth import dataset
from imu_feat import features
X, y, g = dataset(); F = features(X)
models = {
    "tree(d=6)":   DecisionTreeClassifier(max_depth=6, random_state=0),
    "forest(30)":  RandomForestClassifier(n_estimators=30, max_depth=8, random_state=0),
    "gboost(50)":  GradientBoostingClassifier(n_estimators=50, max_depth=3, random_state=0),
    "logreg":      make_pipeline(StandardScaler(), LogisticRegression(max_iter=2000)),
    "kNN(k=5)":    make_pipeline(StandardScaler(), KNeighborsClassifier(5)),
}
user_cv, rand_cv = GroupKFold(n_splits=4), KFold(4, shuffle=True, random_state=0)
for name, m in models.items():
    acc_u = cross_val_score(m, F, y, groups=g, cv=user_cv)
    acc_r = cross_val_score(m, F, y, cv=rand_cv)
    print(f"{name:11s} user-split {acc_u.mean():.3f} ± {acc_u.std():.3f}   random-split {acc_r.mean():.3f}")
```

```text
tree(d=6)   user-split 0.833 ± 0.075   random-split 0.997
forest(30)  user-split 0.929 ± 0.026   random-split 1.000
gboost(50)  user-split 0.919 ± 0.043   random-split 0.999
logreg      user-split 0.891 ± 0.031   random-split 0.996
kNN(k=5)    user-split 0.895 ± 0.047   random-split 0.998
```

출력에서 볼 것:

- **무작위 split은 모든 모델이 99.6~100%**다. 겹치는 창과 같은 사람의 착용 방향·걸음 주파수가 train에 이미 있기 때문이다. 이 숫자를 보고 "완벽하다"고 보고하면 기기에 올리는 순간 무너진다.
- **사용자 단위에서는 83~93%**로 떨어지고 모델 간 차이가 드러난다. 단일 트리(0.833)가 가장 약하고, forest(0.929)가 가장 강하다. 트리 하나의 분산을 여러 트리의 평균으로 줄인 효과다.
- **± 값(fold 간 표준편차)이 크다**. 어떤 3명이 test에 걸리느냐에 따라 정확도가 몇 %씩 흔들린다. 사용자 12명 데이터로 1~2% 차이를 두고 "모델 A가 낫다"고 말하면 안 된다.
- `cross_val_score`는 fold마다 모델을 새로 학습시키고, `StandardScaler`를 pipeline 안에 넣었으므로 정규화 통계도 fold의 train 부분에서만 계산된다 — 정규화 통계가 test에서 새는 것을 막는 관용구다.

### 4.5 모델 크기와 feature importance

MCU에 올릴 때는 정확도만큼 **바이트**가 중요하다. 다음 코드는 사용자 0~8로 학습하고 9~11로 평가하는 고정 hold-out에서, 각 모델의 크기와 random forest의 feature importance를 출력한다. 노드 1개는 (float 임계값 4 B + 특징 번호 1 B + 자식 인덱스 2 × 2 B + 패딩) ≈ 12 B로 가정했다.

```python
import numpy as np
from sklearn.tree import DecisionTreeClassifier
from sklearn.ensemble import RandomForestClassifier, GradientBoostingClassifier
from sklearn.linear_model import LogisticRegression
from sklearn.neighbors import KNeighborsClassifier
from imu_synth import dataset
from imu_feat import features, NAMES
X, y, g = dataset(); F = features(X)
tr, te = g < 9, g >= 9                                 # 사용자 0~8 학습, 9~11 테스트 (고정 hold-out)
NODE_B = 12                                            # 노드 1개 = float thr + uint8 feat + 2×uint16 child (+pad) 가정
tree = DecisionTreeClassifier(max_depth=6, random_state=0).fit(F[tr], y[tr])
rf = RandomForestClassifier(n_estimators=30, max_depth=8, random_state=0).fit(F[tr], y[tr])
gb = GradientBoostingClassifier(n_estimators=50, max_depth=3, random_state=0).fit(F[tr], y[tr])
lr = LogisticRegression(max_iter=5000).fit((F[tr] - F[tr].mean(0)) / F[tr].std(0), y[tr])
n_rf = sum(e.tree_.node_count for e in rf.estimators_)
n_gb = sum(e.tree_.node_count for e in gb.estimators_.ravel())
print(f"tree  : {tree.tree_.node_count:5d} nodes, depth {tree.get_depth()}  ≈ {tree.tree_.node_count*NODE_B/1024:6.1f} KB  acc {tree.score(F[te], y[te]):.3f}")
print(f"forest: {n_rf:5d} nodes in 30 trees        ≈ {n_rf*NODE_B/1024:6.1f} KB  acc {rf.score(F[te], y[te]):.3f}")
print(f"gboost: {n_gb:5d} nodes in {gb.estimators_.size} trees       ≈ {n_gb*NODE_B/1024:6.1f} KB  acc {gb.score(F[te], y[te]):.3f}")
acc_lr = lr.score((F[te] - F[tr].mean(0)) / F[tr].std(0), y[te])
print(f"logreg: {lr.coef_.size + lr.intercept_.size:5d} floats (+정규화 36)   ≈ {(lr.coef_.size + 4 + 36)*4/1024:6.1f} KB  acc {acc_lr:.3f}")
print(f"kNN   : {tr.sum()}×{F.shape[1]} floats (학습셋 전체 저장) ≈ {tr.sum()*F.shape[1]*4/1024:6.1f} KB")
imp = rf.feature_importances_; order = np.argsort(imp)[::-1]
print("forest top-6 importance:", ", ".join(f"{NAMES[i]}={imp[i]:.3f}" for i in order[:6]))
print("forest bottom-3        :", ", ".join(f"{NAMES[i]}={imp[i]:.3f}" for i in order[-3:]))
```

```text
tree  :    17 nodes, depth 5  ≈    0.2 KB  acc 0.852
forest:   944 nodes in 30 trees        ≈   11.1 KB  acc 0.943
gboost:  2800 nodes in 200 trees       ≈   32.8 KB  acc 0.969
logreg:    76 floats (+정규화 36)   ≈    0.4 KB  acc 0.885
kNN   : 1620×18 floats (학습셋 전체 저장) ≈  113.9 KB
forest top-6 importance: mag_std=0.154, std_y=0.130, p2p_z=0.102, mag_p2p=0.098, band_hi=0.082, std_z=0.072
forest bottom-3        : mean_y=0.007, mean_z=0.005, mag_mean=0.003
```

출력에서 볼 것:

- 크기가 **세 자릿수**씩 차이 난다: 트리 0.2 KB, forest 11 KB, boosting 33 KB, kNN 114 KB. boosting은 50 라운드 × 4 클래스 = 200개 트리다.
- 이 hold-out(사용자 9~11)에서는 boosting(0.969)이 forest(0.943)보다 높지만, §4.4의 4-fold 평균에서는 순서가 반대였다. **test 사용자 3명짜리 한 번의 split은 순위를 믿을 근거가 못 된다** — 그래서 cross-validation을 한다.
- max_depth=6을 허용했는데 트리는 깊이 5, 노드 17개에서 멈췄다. leaf가 이미 순수해져 더 나눌 게 없었다는 뜻이다.
- **feature importance**(impurity 기반)는 각 특징이 분할에서 줄인 Gini의 합을 정규화한 것이다. `mag_std`(움직임 에너지)가 1위로 물리적 직관과 맞다. `mean_*`(자세)는 거의 안 쓰였다 — 이 데이터에서는 좋은 신호다.

> 함정: impurity 기반 importance는 **값의 종류가 많은(연속) 특징을 과대평가**하고, 상관된 특징끼리는 중요도를 나눠 가진다 (`std_z`와 `p2p_z`처럼). 특징을 빼도 되는지 판단할 때는 `sklearn.inspection.permutation_importance`(test 데이터에서 특징 하나를 섞었을 때 정확도가 얼마나 떨어지나)나 "빼고 다시 학습해서 user-split으로 재기"를 쓴다.

### 4.6 트리가 MCU와 센서 칩에서 사랑받는 이유

- **곱셈 0회**: 추론 = `if (f[k] <= thr)` 비교의 연쇄. 깊이 5 트리는 비교 5번이다.
- **특징도 싸다**: mean, variance, peak-to-peak, zero-crossing, 에너지는 누적 합과 비교만으로 계산된다 (§11.2에서 정수 C 코드).
- **고정소수점 친화**: 임계값을 센서 LSB 단위 정수로 바꾸면 전부 정수 비교가 된다.
- **해석 가능·검토 가능**: 트리를 그대로 코드 리뷰할 수 있고, 오분류가 나오면 "어느 노드에서 갈렸나"를 추적할 수 있다. 신경망에서는 어렵다.
- **센서 칩 안에서 돈다**: ST의 일부 IMU(예: LSM6DSOX 계열)에는 **Machine Learning Core(MLC)**가 있어서, 센서 데이터에서 계산한 특징(mean, variance, energy, peak-to-peak, zero-crossing, min/max 등)을 입력으로 **decision tree**를 센서 내부에서 실행하고 결과를 레지스터·인터럽트로 알려 준다. 트리는 PC에서 학습해 ST 도구로 센서 설정으로 변환해 넣는다. 지원 특징·트리 개수·노드 수 한도는 부품과 세대마다 다르므로 해당 데이터시트와 앱노트로 확인한다. Bosch 등 다른 IMU 벤더도 step counter, any/no-motion, wrist gesture 같은 기능을 센서에 내장한 제품이 있다.

그림 3은 §4.5에서 학습된 단일 트리(노드 17개)의 위쪽 부분이다. 임계값은 실제 학습된 값이다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="b7t-ar" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="255" y="15" width="170" height="44" rx="6" fill="none" stroke="#4a7bd0" stroke-width="1.5"/> <text x="340" y="34" font-size="13" text-anchor="middle">mag_std ≤ 0.151 g ?</text>
  <text x="340" y="51" font-size="12" text-anchor="middle">root · 1620 창</text> <line x1="300" y1="59" x2="150" y2="105" stroke="currentColor" stroke-width="1.2" marker-end="url(#b7t-ar)"/>
  <line x1="380" y1="59" x2="470" y2="105" stroke="currentColor" stroke-width="1.2" marker-end="url(#b7t-ar)"/> <text x="205" y="76" font-size="12" text-anchor="middle">예</text>
  <text x="440" y="76" font-size="12" text-anchor="middle">아니오</text> <rect x="85" y="107" width="130" height="36" rx="18" fill="#888" fill-opacity="0.25" stroke="#888"/>
  <text x="150" y="130" font-size="13" text-anchor="middle">still</text> <rect x="385" y="107" width="170" height="44" rx="6" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
  <text x="470" y="126" font-size="13" text-anchor="middle">mag_p2p ≤ 1.359 g ?</text> <text x="470" y="143" font-size="12" text-anchor="middle">움직이는 창</text>
  <line x1="430" y1="151" x2="300" y2="197" stroke="currentColor" stroke-width="1.2" marker-end="url(#b7t-ar)"/> <line x1="510" y1="151" x2="580" y2="197" stroke="currentColor" stroke-width="1.2" marker-end="url(#b7t-ar)"/>
  <text x="350" y="168" font-size="12" text-anchor="middle">예</text> <text x="565" y="168" font-size="12" text-anchor="middle">아니오</text>
  <rect x="210" y="199" width="180" height="44" rx="6" fill="none" stroke="#4a7bd0" stroke-width="1.5"/> <text x="300" y="218" font-size="13" text-anchor="middle">std_y ≤ 0.077 g ?</text>
  <text x="300" y="235" font-size="12" text-anchor="middle">walk vs stairs 구역</text> <rect x="500" y="199" width="160" height="44" rx="6" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
  <text x="580" y="218" font-size="13" text-anchor="middle">band_hi ≤ 0.272 ?</text> <text x="580" y="235" font-size="12" text-anchor="middle">큰 진폭</text>
  <line x1="260" y1="243" x2="200" y2="270" stroke="currentColor" stroke-width="1.2" marker-end="url(#b7t-ar)"/> <line x1="340" y1="243" x2="400" y2="270" stroke="currentColor" stroke-width="1.2" marker-end="url(#b7t-ar)"/>
  <rect x="80" y="272" width="240" height="36" rx="6" fill="none" stroke="#888" stroke-dasharray="4 3"/> <text x="200" y="295" font-size="12" text-anchor="middle">band_mid, mean_x … → stairs/still/walk</text>
  <rect x="330" y="272" width="150" height="36" rx="6" fill="none" stroke="#888" stroke-dasharray="4 3"/> <text x="405" y="295" font-size="12" text-anchor="middle">band_mid, std_x …</text>
  <line x1="555" y1="243" x2="530" y2="270" stroke="currentColor" stroke-width="1.2" marker-end="url(#b7t-ar)"/> <line x1="605" y1="243" x2="630" y2="270" stroke="currentColor" stroke-width="1.2" marker-end="url(#b7t-ar)"/>
  <rect x="490" y="272" width="80" height="36" rx="18" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/> <text x="530" y="295" font-size="13" text-anchor="middle">stairs</text>
  <rect x="590" y="272" width="80" height="36" rx="18" fill="#d0564a" fill-opacity="0.25" stroke="#d0564a"/> <text x="630" y="295" font-size="13" text-anchor="middle">run</text>
</svg>
```

그림 3 — 학습된 decision tree의 위쪽 3단. 첫 질문이 "움직임 에너지가 0.151 g 이하인가"(가만히 있음)라는 것은 사람이 짜도 그렇게 짤 규칙이다. 점선 상자는 접어 둔 하위 트리. 왼쪽 하위 트리에 `mean_x`(착용 방향 특징)가 등장한다 — §10에서 이것이 문제를 일으킨다.

---

## 5. Decision tree를 C로 내보내기 — golden 비교까지

### 5.1 sklearn 트리의 내부 배열

학습된 `DecisionTreeClassifier`의 `tree_` 속성은 노드를 **평행 배열**로 저장한다. C 구조체 배열(SoA)과 같다.

| 배열 | 뜻 | leaf일 때 |
|---|---|---|
| `children_left[n]` | "≤"일 때 갈 노드 번호 | -1 |
| `children_right[n]` | ">"일 때 갈 노드 번호 | -1 |
| `feature[n]` | 비교할 특징 번호 | -2 |
| `threshold[n]` | 임계값 (float64) | -2 |
| `value[n]` | 노드에 도달한 클래스별 (가중) 비율 | 이것의 argmax가 예측 |

sklearn은 `x[feature] <= threshold`이면 **왼쪽**으로 간다. 이것만 알면 두 가지 방식으로 C 코드를 만들 수 있다.

1. **if/else 코드 생성**: 트리를 재귀로 돌며 중첩 if문을 출력. 컴파일러가 분기로 최적화한다. 트리를 바꾸면 **재컴파일**이 필요하다.
2. **테이블 + walker**: 배열을 `const` 테이블로 출력하고, 고정된 5줄짜리 루프가 테이블을 따라 내려간다. 트리를 바꾸면 **데이터만** 바꾸면 되므로 OTA로 모델만 갱신하기 쉽다.

### 5.2 코드 — 생성, 컴파일, 540개 전부 비교

이 코드는 §4.5의 트리를 두 방식으로 C 헤더로 만들고, test 특징과 sklearn 예측을 바이너리 파일로 쓴 뒤, C 프로그램을 컴파일·실행해 결과를 비교한다.

```python
import numpy as np, subprocess
from sklearn.tree import DecisionTreeClassifier
from imu_synth import dataset
from imu_feat import features, NAMES
X, y, g = dataset(); F = features(X); tr, te = g < 9, g >= 9
clf = DecisionTreeClassifier(max_depth=6, random_state=0).fit(F[tr], y[tr])
t = clf.tree_                                    # 배열 기반 트리: children_left/right, feature, threshold, value

def emit(n, d=1):                                # 노드 n을 if/else C 코드로 (재귀)
    pad = "    " * d
    if t.children_left[n] == -1:                 # leaf: 가장 많은 클래스 반환
        return f"{pad}return {int(np.argmax(t.value[n]))};\n"
    return (f"{pad}if (f[{t.feature[n]}] <= {t.threshold[n]:.17g}) {{  /* {NAMES[t.feature[n]]} */\n"
            + emit(t.children_left[n], d + 1) + f"{pad}}} else {{\n"
            + emit(t.children_right[n], d + 1) + f"{pad}}}\n")

open("har_tree.h", "w").write("static int har_tree(const float *f) {\n" + emit(0) + "}\n")
arr = lambda name, typ, v: f"static const {typ} {name}[{len(v)}] = {{" + ", ".join(v) + "};\n"
open("har_tree_tbl.h", "w").write(                               # 같은 트리를 '데이터 테이블'로
    arr("T_FEAT", "int8_t", [str(v) for v in t.feature]) + arr("T_THR", "float", [f"{v:.8e}f" for v in t.threshold])
    + arr("T_LEFT", "int16_t", [str(v) for v in t.children_left]) + arr("T_RIGHT", "int16_t", [str(v) for v in t.children_right])
    + arr("T_CLASS", "uint8_t", [str(int(np.argmax(v))) for v in t.value]))
F[te].astype("<f4").tofile("x_test.f32")                      # 테스트 특징 (float32, little-endian)
clf.predict(F[te]).astype("<i4").tofile("y_skl.i32")          # sklearn 예측 = golden
subprocess.run("cc -std=c11 -Wall -Wextra -O2 tree_main.c -o tree_main -lm", shell=True, check=True)
print(subprocess.run(["./tree_main"], capture_output=True, text=True).stdout, end="")
print("har_tree.h:", sum(1 for _ in open("har_tree.h")), "lines,", t.node_count, "nodes,",
      (t.children_left == -1).sum(), "leaves")
```

C 쪽 `tree_main.c`는 두 헤더를 include하고, 테스트 파일을 레코드 단위로 읽어 비교한다.

```c
#include <stdio.h>
#include <stdint.h>
#include "har_tree.h"            /* Python이 생성한 if/else 트리 */
#include "har_tree_tbl.h"        /* 같은 트리의 배열(테이블) 버전 */
#define NF 18

static int har_tree_tbl(const float *f) {          /* 테이블 walker: 코드는 고정, 트리는 데이터 */
    int n = 0;
    while (T_LEFT[n] >= 0) n = (f[T_FEAT[n]] <= T_THR[n]) ? T_LEFT[n] : T_RIGHT[n];
    return T_CLASS[n];
}
int main(void) {
    FILE *fx = fopen("x_test.f32", "rb"), *fy = fopen("y_skl.i32", "rb");
    if (!fx || !fy) return 1;
    float f[NF]; int32_t golden; int n = 0, agree = 0, agree_tbl = 0, hist[4] = {0};
    while (fread(f, sizeof(float), NF, fx) == NF && fread(&golden, 4, 1, fy) == 1) {
        int p = har_tree(f);
        agree += (p == golden); agree_tbl += (har_tree_tbl(f) == golden); hist[p]++; n++;
    }
    printf("if/else tree vs sklearn: %d / %d identical\n", agree, n);
    printf("table   tree vs sklearn: %d / %d identical (table = %zu B)\n", agree_tbl, n,
           sizeof T_FEAT + sizeof T_THR + sizeof T_LEFT + sizeof T_RIGHT + sizeof T_CLASS);
    printf("C predictions per class: still %d, walk %d, run %d, stairs %d\n", hist[0], hist[1], hist[2], hist[3]);
    fclose(fx); fclose(fy);
    return 0;
}
```

```text
if/else tree vs sklearn: 540 / 540 identical
table   tree vs sklearn: 540 / 540 identical (table = 170 B)
C predictions per class: still 149, walk 121, run 127, stairs 143
har_tree.h: 35 lines, 17 nodes, 9 leaves
```

출력에서 볼 것: 두 C 구현 모두 test 540창에서 sklearn과 **100% 같은 클래스**를 낸다. 테이블 버전은 노드 17개에 170 B(노드당 10 B)다. 이것이 Don이 익숙한 **golden vector 검증**의 ML 버전이다 — 모델을 옮길 때마다 "같은 입력 → 같은 출력"을 전수 비교한다.

### 5.3 함정 — float32, 비교 방향, 특징 정의

- **sklearn 트리는 입력을 float32로 바꿔서 비교한다**. 그래서 C에서도 특징을 `float`로 두고 비교해야 같은 결과가 나온다. 임계값은 학습 데이터의 인접한 두 float32 값의 중간점(float64)이다. 테이블 버전처럼 임계값을 float로 반올림하면, 특징 값이 **정확히 그 이웃 값과 같을 때** 드물게 방향이 바뀔 수 있다. 이번 test에서는 일치했지만, 안전하게 하려면 임계값을 double로 두거나 정수 LSB 단위로 양자화한 뒤 전수 비교로 확인한다.
- **`<=`와 `<`를 바꾸면** 경계값에서만 틀린다 — 정수 특징(`mag_zc` 같은 카운트)에서 잘 터진다. 임계값이 10.5처럼 중간점이라 이번엔 괜찮지만, 정수화하면서 `thr = 10`으로 내리면 `≤`/`<`가 결과를 바꾼다.
- **모델보다 특징이 더 자주 틀린다**. C 트리가 sklearn과 100% 같아도, 펌웨어의 특징 추출이 Python과 다르면 기기에서 틀린다. 특징 코드도 같은 raw 창으로 golden 비교한다 (A6 §7.4).
- Random forest는 트리 T개를 같은 방식으로 내보내고 **확률(leaf의 클래스 비율)을 평균**해야 sklearn과 같다. 단순 다수결은 동점·경계에서 sklearn과 다를 수 있다.

---

## 6. 딥러닝 on raw windows — 1D-CNN과 GRU

### 6.1 무엇이 다른가

딥러닝은 특징을 사람이 설계하지 않는다. raw 창 `[3, 128]`을 그대로 넣고 **특징 추출기 자체를 학습**한다.

- **1D-CNN** (B2): 시간 축으로 미끄러지는 필터. 첫 층 필터는 "짧은 충격 모양", "특정 주파수의 흔들림" 같은 국소 패턴 검출기가 된다. stride로 길이를 줄여 가며 넓은 문맥을 본다.
- **GRU** (B3): 샘플을 하나씩 읽으며 hidden state를 갱신한다. 스트리밍에 자연스럽지만 **순차 의존성** 때문에 병렬화가 안 된다.

두 모델을 §4.4와 **같은 사용자 단위 4-fold**로 평가한다. 추가로, 축별 raw 값은 착용 방향에 직접 노출되므로 **회전 증강**(학습 창을 무작위로 최대 60° 돌린 사본 3개 추가, A4 §5.4)을 건 CNN도 함께 본다.

```python
import numpy as np, torch, torch.nn as nn
from sklearn.model_selection import GroupKFold
from imu_synth import dataset, rot
X, y, g = dataset(); torch.manual_seed(0); rng = np.random.default_rng(0)

class CNN(nn.Module):                     # 1D-CNN (B2): 3→16→32→32 채널, stride 2로 길이 128→16
    def __init__(s):
        super().__init__()
        c = lambda i, o: [nn.Conv1d(i, o, 5, stride=2, padding=2), nn.ReLU()]
        s.f = nn.Sequential(*c(3, 16), *c(16, 32), *c(32, 32)); s.fc = nn.Linear(32, 4)
    def forward(s, x): return s.fc(s.f(x).mean(-1))            # global average pooling
class GRU(nn.Module):                     # GRU (B3): 매 샘플마다 hidden 32 갱신, 마지막 state로 분류
    def __init__(s):
        super().__init__(); s.rnn = nn.GRU(3, 32, batch_first=True); s.fc = nn.Linear(32, 4)
    def forward(s, x): return s.fc(s.rnn(x.transpose(1, 2))[1][0])

def run(Model, aug=0, epochs=30):
    accs = []
    for tr, te in GroupKFold(4).split(X, y, g):                 # 사용자 단위 4-fold (ex3과 동일)
        Xtr, ytr = X[tr], y[tr]
        for _ in range(aug):                                    # 회전 증강: 임의 회전(≤60°) 사본 추가
            Xtr = np.concatenate([Xtr, np.einsum("ij,njl->nil", rot(rng, 60), X[tr]).astype(np.float32)])
            ytr = np.concatenate([ytr, y[tr]])
        mu, sd = Xtr.mean((0, 2), keepdims=True), Xtr.std((0, 2), keepdims=True)
        xt, xv = torch.tensor((Xtr - mu) / sd), torch.tensor((X[te] - mu) / sd)
        yt, m = torch.tensor(ytr), Model(); opt = torch.optim.Adam(m.parameters(), 3e-3)
        for ep in range(epochs):
            for b in torch.randperm(len(yt)).split(64):
                opt.zero_grad(); nn.functional.cross_entropy(m(xt[b]), yt[b]).backward(); opt.step()
        with torch.no_grad(): accs.append((m(xv).argmax(1).numpy() == y[te]).mean())
    return np.array(accs), sum(p.numel() for p in m.parameters())

if __name__ == "__main__":
  for name, M, aug, ep in [("1D-CNN", CNN, 0, 30), ("1D-CNN+rot", CNN, 3, 20), ("GRU-32", GRU, 0, 30)]:
    acc, n = run(M, aug, ep)
    print(f"{name:10s} user-split {acc.mean():.3f} ± {acc.std():.3f}  folds {np.round(acc, 3)}  params {n}")
```

```text
1D-CNN     user-split 0.778 ± 0.143  folds [0.557 0.748 0.893 0.915]  params 8132
1D-CNN+rot user-split 0.906 ± 0.090  folds [0.767 0.976 0.889 0.993]  params 8132
GRU-32     user-split 0.893 ± 0.057  folds [0.824 0.856 0.97  0.922]  params 3684
```

출력에서 볼 것 (이 파일을 `ex6.py`로 저장했다고 가정한다 — 뒤 예제가 import한다):

- **증강 없는 1D-CNN(0.778)은 단일 트리(0.833)보다도 낮다**. 첫 fold는 0.557 — 이 fold의 test 사용자 3명의 착용 방향이 train 9명과 달랐다. raw 축 값을 보는 CNN은 착용 방향을 그대로 외운다.
- **회전 증강만으로 0.778 → 0.906**. 모델은 같고 데이터만 바뀌었다. "모델을 키우기 전에 현실의 변동을 데이터로 보여 줘라"는 A4 §5.4의 교훈이 숫자로 나온다.
- GRU(0.893)는 CNN보다 파라미터가 적은데 이 데이터에서는 증강 없이도 더 안정적이었다. 다만 fold 표준편차가 0.06~0.14로 커서, 이 차이를 일반화하면 안 된다. 신경망 결과는 seed와 학습 순서에도 흔들린다.

### 6.2 파라미터와 MAC — 손계산 후 hook으로 확인

Conv1d 한 층의 비용(B2):

```
params = C_out · (C_in · k) + C_out                     (가중치 + bias)
MACs   = L_out · C_out · (C_in · k)                     (출력 원소마다 C_in·k 번 곱셈-누적)
L_out  = ⌊(L_in + 2·pad − k) / stride⌋ + 1

conv1: 3→16,  k=5, L 128→64:  MACs = 64 · 16 · (3·5)   = 15,360     params = 16·15 + 16  =   256
conv2: 16→32, k=5, L 64→32:   MACs = 32 · 32 · (16·5)  = 81,920     params = 32·80 + 32  = 2,592
conv3: 32→32, k=5, L 32→16:   MACs = 16 · 32 · (32·5)  = 81,920     params = 32·160 + 32 = 5,152
fc:    32→4:                  MACs = 128                            params = 128 + 4     =   132
합계                          MACs = 179,328                        params = 8,132
```

GRU(B3)는 time step마다 게이트 3개가 입력과 hidden을 각각 행렬곱한다.

```
step당 MACs = 3 · H · (I + H) = 3 · 32 · (3 + 32) = 3,360
창당 MACs   = 128 step · 3,360 = 430,080  (+ fc 128)
params      = 3 · (H·I + H·H + 2H) + fc = 3 · (96 + 1024 + 64) + 132 = 3,684
```

말로 하면: GRU는 파라미터가 CNN의 절반도 안 되지만 **창당 연산은 2.4배**다. 같은 가중치를 128번 재사용하기 때문이다. 그리고 이 128 step은 앞 step이 끝나야 다음을 계산할 수 있다.

이 코드는 forward hook으로 층별 MAC을 세어 손계산을 확인한다.

```python
import torch, torch.nn as nn
from ex6 import CNN, GRU
macs = []
def conv_hook(m, i, o):   # MAC = 출력 원소 수 × (C_in/groups × k)
    macs.append(o.numel() * m.in_channels // m.groups * m.kernel_size[0])
def lin_hook(m, i, o):    macs.append(o.numel() * m.in_features)
def gru_hook(m, i, o):    # 매 time step: 3개 gate × (W_ih·x + W_hh·h)
    T = i[0].shape[1]; macs.append(T * 3 * m.hidden_size * (m.input_size + m.hidden_size))
for name, model in [("1D-CNN", CNN()), ("GRU-32", GRU())]:
    macs.clear()
    for mod in model.modules():
        if isinstance(mod, nn.Conv1d): mod.register_forward_hook(conv_hook)
        if isinstance(mod, nn.Linear): mod.register_forward_hook(lin_hook)
        if isinstance(mod, nn.GRU):    mod.register_forward_hook(gru_hook)
    model(torch.zeros(1, 3, 128))
    n = sum(p.numel() for p in model.parameters())
    print(f"{name}: params {n:5d} ({n*4/1024:.1f} KB fp32, {n/1024:.1f} KB int8)  MACs/window {sum(macs):,}  per-layer {macs}")
```

```text
1D-CNN: params  8132 (31.8 KB fp32, 7.9 KB int8)  MACs/window 179,328  per-layer [15360, 81920, 81920, 128]
GRU-32: params  3684 (14.4 KB fp32, 3.6 KB int8)  MACs/window 430,208  per-layer [430080, 128]
```

출력에서 볼 것: 손계산과 한 자리까지 같다. int8 양자화(C1) 후 크기는 CNN 7.9 KB, GRU 3.6 KB로 forest(11 KB)와 같은 자릿수다. **크기로는 비슷하고, 연산 종류가 다르다** — 트리는 비교 수백 번, CNN은 MAC 18만 번.

### 6.3 데이터가 많아지면 — 사용자 수에 따른 역전

"딥러닝은 데이터가 많을 때 이긴다"를 직접 본다. 다른 사람 60명을 만들고, test 12명을 고정한 채 학습 사용자 수를 9 → 24 → 48로 늘린다.

```python
import numpy as np, torch, torch.nn as nn
from sklearn.ensemble import RandomForestClassifier
from imu_synth import dataset
from imu_feat import features
from ex6 import CNN            # 위 예제의 모델 정의 재사용
torch.manual_seed(0)
X, y, g = dataset(n_users=60, seed=1)                  # 사용자 60명 (다른 seed = 다른 사람들)
te = g >= 48                                            # 테스트 사용자 12명 고정
for n_tr in [9, 24, 48]:
    tr = g < n_tr
    rf = RandomForestClassifier(n_estimators=30, max_depth=8, random_state=0).fit(features(X[tr]), y[tr])
    mu, sd = X[tr].mean((0, 2), keepdims=True), X[tr].std((0, 2), keepdims=True)
    xt, xv, yt = torch.tensor((X[tr] - mu) / sd), torch.tensor((X[te] - mu) / sd), torch.tensor(y[tr])
    m = CNN(); opt = torch.optim.Adam(m.parameters(), 3e-3)
    for ep in range(int(30 * 9 / n_tr) + 5):               # 사용자 수가 늘면 epoch 수를 줄여 step 수를 비슷하게
        for b in torch.randperm(len(yt)).split(64):
            opt.zero_grad(); nn.functional.cross_entropy(m(xt[b]), yt[b]).backward(); opt.step()
    with torch.no_grad(): acc_cnn = (m(xv).argmax(1).numpy() == y[te]).mean()
    print(f"train users {n_tr:2d}: forest {rf.score(features(X[te]), y[te]):.3f}   1D-CNN {acc_cnn:.3f}")
```

```text
train users  9: forest 0.909   1D-CNN 0.903
train users 24: forest 0.933   1D-CNN 0.916
train users 48: forest 0.970   1D-CNN 0.976
```

출력에서 볼 것: 둘 다 사용자가 늘수록 좋아지지만, CNN은 9명일 때 forest보다 낮다가 48명에서 따라잡는다(0.976 vs 0.970). 증강 없이도 **사람이 많아지면 착용 방향의 다양성이 데이터에 자연히 들어오기** 때문이다. 합성 데이터라 격차가 작다. 실제 데이터에서는 특징으로 표현하기 어려운 미세 패턴(제스처의 궤적 모양 등)이 있을수록 DL 쪽 이득이 커진다.

### 6.4 비교표 — MCU에 무엇을 올릴까

| 모델 | user-split 정확도 (4-fold) | 크기 | 추론 연산 / 창 | 비고 |
|---|---|---|---|---|
| Decision tree (d≤6) | 0.833 ± 0.075 | 17 노드, 0.2 KB | 비교 ≤ 5 | + 특징 추출 |
| Random forest (30) | **0.929 ± 0.026** | 944 노드, 약 11 KB | 비교 ≤ 30 × 8 = 240 | + 특징 추출, fold 간 가장 안정 |
| Gradient boosting (50) | 0.919 ± 0.043 | 2,800 노드, 약 33 KB | 비교 ≤ 200 × 3 = 600, 덧셈 | + 특징 추출 |
| Logistic regression | 0.891 ± 0.031 | 76 + 36 floats, 0.4 KB | MAC 72 | + 특징 추출 |
| 1D-CNN (raw) | 0.778 ± 0.143 | 8,132 params, 7.9 KB int8 | MAC 179,328 | 착용 방향에 취약 |
| 1D-CNN + 회전 증강 | 0.906 ± 0.090 | 같음 | 같음 | 증강이 12%p |
| GRU-32 (raw) | 0.893 ± 0.057 | 3,684 params, 3.6 KB int8 | MAC 430,208 (순차 128 step) | 스트리밍 가능 |

고전 ML의 "특징 추출" 비용은 창당 대략 raw 384개 값에 대한 누적 합·제곱합·min/max, `|a|` 128번(제곱근 포함), 128점 FFT 한 번이다. 합쳐서 수천 번의 곱셈 수준이라 CNN의 18만 MAC보다 **한두 자릿수 작다** (§11.2).

```svg
<svg viewBox="0 0 680 345" xmlns="http://www.w3.org/2000/svg">
  <text x="162" y="35" font-size="12" text-anchor="end">decision tree d=6</text> <rect x="170" y="23" width="259.7" height="18" fill="#3f9a6b" fill-opacity="0.8"/>
  <line x1="371.2" y1="32" x2="488.2" y2="32" stroke="currentColor" stroke-width="1.2"/> <text x="568" y="35" font-size="12">0.833 · 0.2 KB</text> <text x="162" y="69" font-size="12" text-anchor="end">logistic reg.</text>
  <rect x="170" y="57" width="305.0" height="18" fill="#3f9a6b" fill-opacity="0.8"/> <line x1="450.8" y1="66" x2="499.2" y2="66" stroke="currentColor" stroke-width="1.2"/> <text x="568" y="69" font-size="12">0.891 · 0.4 KB</text>
  <text x="162" y="103" font-size="12" text-anchor="end">kNN k=5</text> <rect x="170" y="91" width="308.1" height="18" fill="#3f9a6b" fill-opacity="0.8"/>
  <line x1="441.4" y1="100" x2="514.8" y2="100" stroke="currentColor" stroke-width="1.2"/> <text x="568" y="103" font-size="12">0.895 · 114 KB</text> <text x="162" y="137" font-size="12" text-anchor="end">grad. boosting</text>
  <rect x="170" y="125" width="326.8" height="18" fill="#3f9a6b" fill-opacity="0.8"/> <line x1="463.3" y1="134" x2="530.4" y2="134" stroke="currentColor" stroke-width="1.2"/> <text x="568" y="137" font-size="12">0.919 · 33 KB</text>
  <text x="162" y="171" font-size="12" text-anchor="end">random forest</text> <rect x="170" y="159" width="334.6" height="18" fill="#3f9a6b" fill-opacity="0.8"/>
  <line x1="484.3" y1="168" x2="524.9" y2="168" stroke="currentColor" stroke-width="1.2"/> <text x="568" y="171" font-size="12">0.929 · 11 KB</text> <text x="162" y="205" font-size="12" text-anchor="end">1D-CNN raw</text>
  <rect x="170" y="193" width="216.8" height="18" fill="#4a7bd0" fill-opacity="0.8"/> <line x1="275.3" y1="202" x2="498.4" y2="202" stroke="currentColor" stroke-width="1.2"/>
  <text x="568" y="205" font-size="12">0.778 · 8 KB int8</text> <text x="162" y="239" font-size="12" text-anchor="end">1D-CNN + rot aug</text> <rect x="170" y="227" width="316.7" height="18" fill="#4a7bd0" fill-opacity="0.8"/>
  <line x1="416.5" y1="236" x2="556.9" y2="236" stroke="currentColor" stroke-width="1.2"/> <text x="568" y="239" font-size="12">0.906 · 8 KB int8</text> <text x="162" y="273" font-size="12" text-anchor="end">GRU-32 raw</text>
  <rect x="170" y="261" width="306.5" height="18" fill="#4a7bd0" fill-opacity="0.8"/> <line x1="432.1" y1="270" x2="521.0" y2="270" stroke="currentColor" stroke-width="1.2"/>
  <text x="568" y="273" font-size="12">0.893 · 3.6 KB int8</text> <line x1="170" y1="296" x2="560" y2="296" stroke="currentColor"/>
  <line x1="170.0" y1="296" x2="170.0" y2="301" stroke="currentColor"/><text x="170.0" y="315" font-size="12" text-anchor="middle">0.5</text>
  <line x1="248.0" y1="296" x2="248.0" y2="301" stroke="currentColor"/><text x="248.0" y="315" font-size="12" text-anchor="middle">0.6</text>
  <line x1="326.0" y1="296" x2="326.0" y2="301" stroke="currentColor"/><text x="326.0" y="315" font-size="12" text-anchor="middle">0.7</text>
  <line x1="404.0" y1="296" x2="404.0" y2="301" stroke="currentColor"/><text x="404.0" y="315" font-size="12" text-anchor="middle">0.8</text>
  <line x1="482.0" y1="296" x2="482.0" y2="301" stroke="currentColor"/><text x="482.0" y="315" font-size="12" text-anchor="middle">0.9</text>
  <line x1="560.0" y1="296" x2="560.0" y2="301" stroke="currentColor"/><text x="560.0" y="315" font-size="12" text-anchor="middle">1.0</text>
  <text x="365.0" y="333" font-size="12" text-anchor="middle">user-split 4-fold 정확도 (막대 = 평균, 선 = ±1 std)</text>
</svg>
```

그림 4 — 같은 데이터·같은 사용자 단위 4-fold에서의 정확도(막대 = 평균, 가로선 = ±1 std). 초록 = 특징 + 고전 ML, 파랑 = raw 창 딥러닝. 오른쪽 숫자는 정확도와 모델 크기.

### 6.5 언제 DL이 이기고, 언제 고전 ML이 이기나

| 상황 | 유리한 쪽 | 이유 |
|---|---|---|
| 사용자 수십 명, 라벨 수천 창 | 고전 ML | 특징이 사전 지식(물리)을 주입한다 |
| 클래스가 에너지·주파수로 갈림 (HAR 기본 4종) | 고전 ML | 특징 몇 개로 충분하다 |
| 파형 **모양**이 중요 (제스처 궤적, 글씨 쓰기, 미세 떨림) | DL | 손으로 특징을 설계하기 어렵다 |
| 사용자 수백 명 이상, 다양한 착용 | DL | 데이터가 변동을 가르쳐 준다 |
| IMU 내장 코어·초저전력 MCU | 고전 ML | 곱셈 없는 비교, 수백 바이트 |
| DSP/NPU가 이미 깨어 있음 (stage 2) | DL | MAC이 싸고 가속기가 있다 |
| 설명·인증이 필요 (의료성 기능) | 고전 ML | 규칙을 검토할 수 있다 |

실무의 흔한 답은 **둘 다**다: stage 1에서 forest가 "제스처일 가능성 있음"을 싸게 거르고, stage 2에서 CNN이 정밀 판정한다 (cascade, B9).

---

## 7. 후처리 — 투표, 확률 평균, hysteresis, debounce

### 7.1 왜 필요한가

창마다 독립적으로 판단하면 출력이 **깜빡인다**. 걷는 중에 속도가 잠깐 느려진 창 하나가 still로 찍히면, 앱은 "걷기 종료 → 걷기 시작" 이벤트를 두 번 받는다. 펌웨어로 말하면 **바운싱하는 GPIO를 debounce 없이 인터럽트에 물린 것**과 같다. 해결책도 같다: 시간적 문맥을 써서 안정화한다.

| 방법 | 동작 | 비용 | 대가 |
|---|---|---|---|
| 다수결 (majority vote) | 최근 k개 창의 예측 중 최빈값 | 정수 카운터 k개 | 지연 약 (k−1)/2 창 |
| 확률 평균 | 최근 k개 창의 클래스 확률 평균의 argmax | float 누적 C·k개 | 지연 약 (k−1)/2 창 |
| 지수 평활 (EMA) | `p̄ ← α·p + (1−α)·p̄` | 클래스당 float 1개 | 시정수 ≈ 1/α 창 |
| Hysteresis | 켜는 문턱 > 끄는 문턱 | 비교 2개 | 경계 근처 반응 느림 |
| Debounce (연속 N회) | 조건이 N번 연속이어야 전이 | 카운터 1개 | 지연 N 창 |
| Refractory (재발동 금지) | 이벤트 후 T초간 같은 이벤트 무시 | 타이머 1개 | T 안의 진짜 반복을 놓침 |

### 7.2 코드 — 연속 스트림에서 투표 vs 확률 평균

이 코드는 처음 보는 사용자 10의 연속 활동(still → walk → stairs → walk → run → still, 230 s)에 forest를 돌리고, 원래 출력·다수결·확률 평균을 비교한다. 모두 **causal**(과거 창만 봄)이다 — 기기에서는 미래 창을 볼 수 없다.

```python
import numpy as np
from sklearn.ensemble import RandomForestClassifier
from imu_synth import dataset, make_user, session, WIN, HOP
from imu_feat import features
X, y, g = dataset(); F = features(X)
rf = RandomForestClassifier(n_estimators=30, max_depth=8, random_state=0).fit(F[g < 9], y[g < 9])
# 테스트 사용자 10의 연속 스트림: still 30 s → walk 60 → stairs 40 → walk 30 → run 40 → still 30
user, rng = make_user(10), np.random.default_rng(123)
plan = [(0, 30), (1, 60), (3, 40), (1, 30), (2, 40), (0, 30)]
sig = np.concatenate([session(user, c, s, rng) for c, s in plan])
lab = np.concatenate([[c] * int(s * 50) for c, s in plan])
idx = np.arange(0, len(sig) - WIN + 1, HOP)
Xs = np.stack([sig[i:i + WIN].T for i in idx]); ys = lab[idx + WIN // 2]   # 창 중앙 라벨
P = rf.predict_proba(features(Xs)); raw = P.argmax(1)

def majority(p, k=5):                         # causal: 최근 k개 창의 최빈값 (미래를 보지 않음)
    return np.array([np.bincount(p[max(0, i - k + 1):i + 1], minlength=4).argmax() for i in range(len(p))])
vote = majority(raw)
pavg = np.array([P[max(0, i - 4):i + 1].mean(0).argmax() for i in range(len(P))])  # 확률 평균 k=5
flips = lambda p: int((np.diff(p) != 0).sum())
print(f"windows {len(idx)}, true transitions {flips(ys)}")
print(f"raw     : acc {np.mean(raw == ys):.3f}, label changes {flips(raw)}")
print(f"vote k=5: acc {np.mean(vote == ys):.3f}, label changes {flips(vote)}")
print(f"pavg k=5: acc {np.mean(pavg == ys):.3f}, label changes {flips(pavg)}")
print("raw  :", "".join("SWRU"[c] for c in raw[20:75]))
print("vote :", "".join("SWRU"[c] for c in vote[20:75]))
print("pavg :", "".join("SWRU"[c] for c in pavg[20:75]))
print("truth:", "".join("SWRU"[c] for c in ys[20:75]))
```

```text
windows 178, true transitions 5
raw     : acc 0.888, label changes 18
vote k=5: acc 0.876, label changes 12
pavg k=5: acc 0.933, label changes 8
raw  : SSSWWWWWWSSWWWWWWWSSSWWWWWWSSSWWWWWWWSSWWWWWWWSSSUUUUUU
vote : SSSSSWWWWWWWWWWWWWWWSSSWWWWWWSSSWWWWWWWWWWWWWWWWSSSUUUU
pavg : SSSSSWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWSUUUU
truth: SSSWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWUUUUU
```

(S = still, W = walk, R = run, U = stairs up)

출력에서 볼 것:

- 원래 출력은 실제 전이 5번에 대해 라벨이 18번 바뀐다. 이 사용자는 걷는 힘이 약해서, 걷기 속도가 느려지는 구간마다 still로 2~3창씩 떨어진다.
- **다수결은 깜빡임을 18 → 12로 줄였지만 정확도는 오히려 떨어졌다**(0.888 → 0.876). 오류가 한 창짜리 잡음이 아니라 2~3창 연속이라 5창 다수결로는 못 지우고, 대신 진짜 전이를 2창 늦게 따라가며 경계에서 틀린다.
- **확률 평균은 18 → 8, 정확도 0.933**. 다수결은 "still 0.51 / walk 0.49"와 "still 0.99"를 똑같이 한 표로 세지만, 확률 평균은 **확신의 크기**를 반영한다. 애매한 still 창들이 확신 있는 walk 창들에 묻힌다.
- 대가는 지연이다. k=5, hop 1.28 s면 평균 지연이 약 2창 × 1.28 s ≈ 2.6 s 늘어난다. HAR에는 괜찮고 제스처에는 안 된다.

### 7.3 Hysteresis + debounce FSM — C 구현

이벤트성 판단(착용 감지, "걷는 중" 알림 등)은 상태 기계로 만든다. 설계 규칙은 두 개다.

1. **문턱을 둘로 나눈다** (hysteresis): OFF → ON은 `p > 0.7`, ON → OFF는 `p < 0.3`. 0.3~0.7 사이에서는 현재 상태를 유지한다. 비교기의 Schmitt trigger와 같다.
2. **연속 N회를 요구한다** (debounce): 조건이 N번 **연속** 만족해야 전이한다. 한 번이라도 끊기면 카운터를 0으로 되돌린다.

이 C 코드는 1초마다 들어오는 착용 확률 26개에 FSM을 돌리고, 단일 문턱(0.5) 비교기와 이벤트 수를 비교한다.

```c
#include <stdio.h>
#include <stdint.h>
/* 착용 감지 확률 스트림 (1 s마다 1개) — 착용 → 흔들림 → 벗음 → 잠깐 스침 */
static const float p[] = {0.10f,0.20f,0.60f,0.40f,0.80f,0.90f,0.85f,0.95f,0.45f,0.90f,
                          0.88f,0.92f,0.55f,0.35f,0.91f,0.90f,0.20f,0.25f,0.10f,0.15f,
                          0.05f,0.10f,0.75f,0.20f,0.10f,0.05f};
#define N (int)(sizeof p / sizeof p[0])
typedef enum { OFF_BODY, ON_BODY } wear_t;
typedef struct { wear_t s; uint8_t cnt; } wear_fsm_t;
#define TH_ON   0.70f   /* 켜질 때는 높은 문턱   */
#define TH_OFF  0.30f   /* 꺼질 때는 낮은 문턱   */
#define N_ON    3       /* 3회 연속이어야 ON (debounce)  */
#define N_OFF   4       /* 4회 연속이어야 OFF            */

static wear_t wear_step(wear_fsm_t *f, float prob) {
    int want_flip = (f->s == OFF_BODY) ? (prob > TH_ON) : (prob < TH_OFF);
    f->cnt = want_flip ? (uint8_t)(f->cnt + 1) : 0;         /* 조건이 끊기면 카운터 리셋 */
    if (f->cnt >= ((f->s == OFF_BODY) ? N_ON : N_OFF)) {
        f->s = (f->s == OFF_BODY) ? ON_BODY : OFF_BODY;      /* 상태 전이 = 이벤트 1회 */
        f->cnt = 0;
    }
    return f->s;
}
int main(void) {
    wear_fsm_t f = { OFF_BODY, 0 };
    int naive_prev = 0, naive_evt = 0, fsm_evt = 0; wear_t prev = OFF_BODY;
    for (int t = 0; t < N; t++) {
        int naive = p[t] > 0.5f;                              /* 문턱 하나짜리 비교기 */
        naive_evt += (naive != naive_prev); naive_prev = naive;
        wear_t s = wear_step(&f, p[t]);
        if (s != prev) { printf("t=%2d s  p=%.2f  -> %s\n", t, (double)p[t], s == ON_BODY ? "ON_BODY" : "OFF_BODY"); fsm_evt++; }
        prev = s;
    }
    printf("naive threshold 0.5: %d events,  hysteresis+debounce FSM: %d events\n", naive_evt, fsm_evt);
    printf("state size: %zu bytes\n", sizeof(wear_fsm_t));
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 hyst.c -o hyst -lm && ./hyst
```

```text
t= 6 s  p=0.85  -> ON_BODY
t=19 s  p=0.15  -> OFF_BODY
naive threshold 0.5: 10 events,  hysteresis+debounce FSM: 2 events
state size: 8 bytes
```

출력에서 볼 것: 단일 문턱은 이벤트 10번(착용했다 벗었다를 5번 반복한 것처럼 보임), FSM은 실제 사건 2번(t=6 착용, t=19 벗음)만 낸다. t=8의 0.45, t=13의 0.35는 hysteresis 구간이라 무시됐고, t=22의 0.75(잠깐 스침)는 debounce 3회를 못 채워 무시됐다. 상태 크기 8 B는 `enum`이 `int`(4 B)이기 때문이다 — `uint8_t`로 저장하면 2 B가 된다.

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
  <line x1="60" y1="170" x2="620" y2="170" stroke="currentColor"/><line x1="60" y1="30" x2="60" y2="170" stroke="currentColor"/>
  <line x1="60" y1="72.0" x2="620" y2="72.0" stroke="#3f9a6b" stroke-dasharray="4 3"/><text x="620" y="68.0" font-size="12" text-anchor="end">TH_ON 0.7</text>
  <line x1="60" y1="100.0" x2="620" y2="100.0" stroke="#888" stroke-dasharray="4 3"/><text x="620" y="96.0" font-size="12" text-anchor="end">단일 문턱 0.5</text>
  <line x1="60" y1="128.0" x2="620" y2="128.0" stroke="#d0564a" stroke-dasharray="4 3"/><text x="620" y="124.0" font-size="12" text-anchor="end">TH_OFF 0.3</text> <text x="54" y="174.0" font-size="12" text-anchor="end">0</text>
  <text x="54" y="104.0" font-size="12" text-anchor="end">0.5</text> <text x="54" y="34.0" font-size="12" text-anchor="end">1</text>
  <polyline fill="none" stroke="#4a7bd0" stroke-width="1.5" points="60.0,156.0 82.4,142.0 104.8,86.0 127.2,114.0 149.6,58.0 172.0,44.0 194.4,51.0 216.8,37.0 239.2,107.0 261.6,44.0 284.0,46.8 306.4,41.2 328.8,93.0 351.2,121.0 373.6,42.6 396.0,44.0 418.4,142.0 440.8,135.0 463.2,156.0 485.6,149.0 508.0,163.0 530.4,156.0 552.8,65.0 575.2,142.0 597.6,156.0 620.0,163.0"/>
  <circle cx="60.0" cy="156.0" r="2.5" fill="#4a7bd0"/> <circle cx="82.4" cy="142.0" r="2.5" fill="#4a7bd0"/> <circle cx="104.8" cy="86.0" r="2.5" fill="#4a7bd0"/> <circle cx="127.2" cy="114.0" r="2.5" fill="#4a7bd0"/>
  <circle cx="149.6" cy="58.0" r="2.5" fill="#4a7bd0"/> <circle cx="172.0" cy="44.0" r="2.5" fill="#4a7bd0"/> <circle cx="194.4" cy="51.0" r="2.5" fill="#4a7bd0"/> <circle cx="216.8" cy="37.0" r="2.5" fill="#4a7bd0"/>
  <circle cx="239.2" cy="107.0" r="2.5" fill="#4a7bd0"/> <circle cx="261.6" cy="44.0" r="2.5" fill="#4a7bd0"/> <circle cx="284.0" cy="46.8" r="2.5" fill="#4a7bd0"/> <circle cx="306.4" cy="41.2" r="2.5" fill="#4a7bd0"/>
  <circle cx="328.8" cy="93.0" r="2.5" fill="#4a7bd0"/> <circle cx="351.2" cy="121.0" r="2.5" fill="#4a7bd0"/> <circle cx="373.6" cy="42.6" r="2.5" fill="#4a7bd0"/> <circle cx="396.0" cy="44.0" r="2.5" fill="#4a7bd0"/>
  <circle cx="418.4" cy="142.0" r="2.5" fill="#4a7bd0"/> <circle cx="440.8" cy="135.0" r="2.5" fill="#4a7bd0"/> <circle cx="463.2" cy="156.0" r="2.5" fill="#4a7bd0"/> <circle cx="485.6" cy="149.0" r="2.5" fill="#4a7bd0"/>
  <circle cx="508.0" cy="163.0" r="2.5" fill="#4a7bd0"/> <circle cx="530.4" cy="156.0" r="2.5" fill="#4a7bd0"/> <circle cx="552.8" cy="65.0" r="2.5" fill="#4a7bd0"/> <circle cx="575.2" cy="142.0" r="2.5" fill="#4a7bd0"/>
  <circle cx="597.6" cy="156.0" r="2.5" fill="#4a7bd0"/> <circle cx="620.0" cy="163.0" r="2.5" fill="#4a7bd0"/>
  <polyline fill="none" stroke="#e08a3c" stroke-width="2" points="60.0,270 82.4,270 82.4,270 104.8,270 104.8,270 127.2,270 127.2,270 149.6,270 149.6,270 172.0,270 172.0,270 194.4,270 194.4,215 216.8,215 216.8,215 239.2,215 239.2,215 261.6,215 261.6,215 284.0,215 284.0,215 306.4,215 306.4,215 328.8,215 328.8,215 351.2,215 351.2,215 373.6,215 373.6,215 396.0,215 396.0,215 418.4,215 418.4,215 440.8,215 440.8,215 463.2,215 463.2,215 485.6,215 485.6,270 508.0,270 508.0,270 530.4,270 530.4,270 552.8,270 552.8,270 575.2,270 575.2,270 597.6,270 597.6,270 620.0,270 620.0,270 620.0,270"/>
  <text x="54" y="219" font-size="12" text-anchor="end">ON</text><text x="54" y="274" font-size="12" text-anchor="end">OFF</text> <text x="60.0" y="186" font-size="12" text-anchor="middle">0 s</text>
  <text x="172.0" y="186" font-size="12" text-anchor="middle">5 s</text> <text x="284.0" y="186" font-size="12" text-anchor="middle">10 s</text> <text x="396.0" y="186" font-size="12" text-anchor="middle">15 s</text>
  <text x="508.0" y="186" font-size="12" text-anchor="middle">20 s</text> <text x="620.0" y="186" font-size="12" text-anchor="middle">25 s</text>
  <text x="64" y="22" font-size="12">착용 확률 p (모델 출력)</text><text x="64" y="205" font-size="12">FSM 상태 (이벤트 2회)</text>
</svg>
```

그림 5 — 위: 모델이 낸 착용 확률(파랑)과 세 문턱. 아래: FSM 상태(주황). 확률이 0.5를 여러 번 가로지르지만 상태는 두 번만 바뀐다. ON으로 바뀌는 시점(t=6)은 0.7을 처음 넘은 t=4보다 2초 늦다 — debounce의 지연이다.

### 7.4 설계 팁

- **지연 예산에서 거꾸로 N을 정한다**. 착용 감지는 수 초 늦어도 되므로 N을 크게, raise-to-wake는 300 ms 안이어야 하므로 N을 1~2로 작게 하고 대신 문턱을 엄격하게 둔다.
- **ON과 OFF의 비용이 다르면 비대칭으로** 둔다. 잘못 OFF하면 알림이 끊기고(사용자 불만), 잘못 ON하면 전력이 샌다. 위 코드는 N_ON 3, N_OFF 4로 "벗었다"를 더 신중하게 판정한다.
- 이벤트형(제스처)에는 **refractory**를 더한다: 한 번 발동하면 1초간 같은 이벤트를 무시한다. A4 §13에 C 예제가 있다.
- 후처리 파라미터도 **튜닝 대상이자 누수 지점**이다. validation 사용자에서 정하고 test 사용자에서는 건드리지 않는다.

---

## 8. 낙상 감지 — 드문 사건 문제

### 8.1 신호의 모양

낙상은 보통 세 단계로 보인다.

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
  <rect x="287.9" y="30" width="53.5" height="220" fill="#4a7bd0" fill-opacity="0.15"/> <rect x="369.5" y="30" width="250.5" height="220" fill="#3f9a6b" fill-opacity="0.12"/>
  <line x1="60" y1="250" x2="620" y2="250" stroke="currentColor"/><line x1="60" y1="30" x2="60" y2="250" stroke="currentColor"/> <text x="54" y="254.0" font-size="12" text-anchor="end">0</text>
  <text x="54" y="220.2" font-size="12" text-anchor="end">1</text> <text x="54" y="186.3" font-size="12" text-anchor="end">2</text> <text x="54" y="118.6" font-size="12" text-anchor="end">4</text>
  <text x="54" y="50.9" font-size="12" text-anchor="end">6</text>
  <line x1="60" y1="192.5" x2="620" y2="192.5" stroke="#d0564a" stroke-width="1" stroke-dasharray="4 3"/><text x="620" y="187.5" font-size="12" text-anchor="end">trigger 1.7 g</text>
  <line x1="60" y1="229.7" x2="620" y2="229.7" stroke="#4a7bd0" stroke-width="1" stroke-dasharray="2 3"/>
  <polyline fill="none" stroke="#e08a3c" stroke-width="1.8" points="60.0,215.3 62.8,216.8 65.6,215.5 68.4,215.3 71.3,216.3 74.1,216.9 76.9,217.1 79.7,215.5 82.5,215.9 85.3,216.0 88.1,216.2 91.0,216.1 93.8,215.2 96.6,216.0 99.4,215.9 102.2,217.0 105.0,216.4 107.8,215.7 110.7,215.2 113.5,217.4 116.3,216.2 119.1,216.8 121.9,216.0 124.7,216.8 127.5,216.3 130.4,216.5 133.2,215.7 136.0,216.1 138.8,215.3 141.6,217.3 144.4,215.9 147.2,216.0 150.1,216.9 152.9,218.0 155.7,217.9 158.5,214.9 161.3,216.9 164.1,216.0 166.9,215.6 169.7,217.0 172.6,215.9 175.4,215.5 178.2,216.3 181.0,214.9 183.8,215.7 186.6,216.4 189.4,216.4 192.3,216.4 195.1,216.0 197.9,215.8 200.7,217.0 203.5,216.4 206.3,216.1 209.1,216.5 212.0,214.7 214.8,215.9 217.6,215.8 220.4,215.8 223.2,215.2 226.0,216.3 228.8,215.3 231.7,215.1 234.5,215.3 237.3,216.3 240.1,216.2 242.9,216.9 245.7,216.0 248.5,214.4 251.4,216.4 254.2,216.2 257.0,216.0 259.8,216.4 262.6,216.9 265.4,216.9 268.2,216.6 271.1,216.0 273.9,216.1 276.7,216.6 279.5,215.5 282.3,215.7 285.1,214.9 287.9,243.6 290.8,243.4 293.6,243.4 296.4,242.9 299.2,242.9 302.0,242.9 304.8,242.8 307.6,243.2 310.5,242.8 313.3,243.8 316.1,242.9 318.9,242.3 321.7,242.8 324.5,244.8 327.3,242.8 330.2,242.4 333.0,243.7 335.8,241.9 338.6,242.4 341.4,122.4 344.2,122.4 347.0,121.9 349.8,120.4 352.7,216.1 355.5,215.7 358.3,217.3 361.1,215.9 363.9,215.9 366.7,217.7 369.5,216.0 372.4,216.2 375.2,215.5 378.0,216.6 380.8,215.5 383.6,215.7 386.4,216.7 389.2,215.8 392.1,215.7 394.9,216.3 397.7,216.3 400.5,216.6 403.3,215.7 406.1,215.8 408.9,216.2 411.8,216.1 414.6,215.6 417.4,216.9 420.2,217.1 423.0,216.3 425.8,216.0 428.6,216.1 431.5,214.9 434.3,216.4 437.1,215.6 439.9,216.8 442.7,215.7 445.5,215.8 448.3,216.2 451.2,216.2 454.0,215.6 456.8,216.2 459.6,216.3 462.4,215.8 465.2,216.9 468.0,216.5 470.9,216.8 473.7,215.5 476.5,216.4 479.3,216.3 482.1,216.5 484.9,215.2 487.7,215.9 490.6,216.5 493.4,215.3 496.2,216.4 499.0,217.3 501.8,216.9 504.6,217.3 507.4,216.0 510.3,216.9 513.1,215.8 515.9,216.3 518.7,216.3 521.5,217.0 524.3,216.3 527.1,216.1 529.9,215.9 532.8,216.5 535.6,216.5 538.4,216.5 541.2,214.7 544.0,215.5 546.8,216.4 549.6,216.0 552.5,216.4 555.3,218.2 558.1,217.0 560.9,216.4 563.7,216.2 566.5,216.0 569.3,216.1 572.2,216.3 575.0,216.1 577.8,216.2 580.6,216.9 583.4,216.4 586.2,215.7 589.0,216.4 591.9,216.7 594.7,217.0 597.5,216.6 600.3,216.3 603.1,216.5 605.9,215.6 608.7,216.7 611.6,216.5 614.4,216.2 617.2,217.6 620.0,215.6"/>
  <text x="314.7" y="44" font-size="12" text-anchor="middle">자유낙하 0.38 s</text> <text x="349.4" y="124.4" font-size="12">충격 3.8 g</text> <text x="494.8" y="44" font-size="12" text-anchor="middle">누운 채 정지 (std 0.017 g)</text>
  <text x="60.0" y="266" font-size="12" text-anchor="middle">0 s</text> <text x="200.7" y="266" font-size="12" text-anchor="middle">1 s</text> <text x="341.4" y="266" font-size="12" text-anchor="middle">2 s</text>
  <text x="482.1" y="266" font-size="12" text-anchor="middle">3 s</text> <text x="620.0" y="266" font-size="12" text-anchor="middle">4 s</text> <text x="18" y="140" font-size="12" text-anchor="middle">|a| g</text>
</svg>
```

그림 6 — 합성 낙상 하나의 `|a|`(실제 생성 값, 4 s). 파란 구간: 자유낙하(`|a|`가 0.6 g 아래로, 0.38 s). 충격: 3.8 g 스파이크. 초록 구간: 누운 채 거의 정지(std 0.017 g). 빨간 점선은 후보를 잡는 trigger 1.7 g.

1. **자유낙하**: 몸이 떨어지는 동안 가속도계는 무중력에 가까워진다(`|a|` → 0). 가속도계는 중력을 직접 재는 게 아니라 "떨어지지 않게 받쳐 주는 힘"을 재기 때문이다.
2. **충격**: 바닥에 닿는 순간 수 g의 스파이크.
3. **충격 후 자세 변화 + 정지**: 중력 방향이 바뀌고(누움), 한동안 움직임이 적다.

문제는 **일상 동작(ADL, activities of daily living)도 이 중 일부를 가진다**는 것이다: 의자에 털썩 앉기(충격, 자유낙하 없음), 점프(자유낙하 + 충격, 이후 계속 움직임), 손목을 책상에 부딪히기(큰 스파이크), 침대에 눕기(자세 변화 + 약한 충격).

### 8.2 드문 사건의 산수

낙상은 **드물다**. 이게 평가를 완전히 바꾼다 (A2 기저율, A4 §7·§9).

- **정확도는 무의미하다**. 후보 3,050개 중 낙상이 50개(1.64%)면, "항상 아니오"가 정확도 98.4%다.
- **recall(검출률)이 1순위**다. 놓친 낙상은 사용자가 도움을 못 받는다는 뜻이다.
- **false alarm은 "비율"이 아니라 "하루 몇 번"으로** 잰다. 사용자는 "정밀도 72%"를 체감하지 않고 "이틀에 한 번 괜히 알람이 울린다"를 체감한다.
- **검증 데이터 크기의 산수** (A4 §9.2의 rule of three): 관찰 T user-day 동안 오경보 0회면 95% 상한은 약 `3 / T` 회/일이다. "한 달에 1회 이하(1/30 per day)"를 주장하려면 `T ≥ 3 / (1/30) = 90` user-day 동안 오경보 0회가 필요하다.
- **recall의 불확실성도 크다**. 50건 중 48건 검출(0.96)의 95% 신뢰구간은 Wilson 구간으로 약 0.87~0.99다. 낙상 녹화는 비싸고(보통 매트 위에서 연기한 낙상), 연기한 낙상과 실제 낙상(특히 고령자)은 다를 수 있다는 한계도 기억해야 한다.

### 8.3 코드 — trigger 후보 + 작은 분류기

설계는 **cascade**다: (1) `|a| > 1.7 g` trigger가 후보 구간을 잡고(싸고 항상 켜짐), (2) 후보마다 특징 5개를 계산해 작은 분류기가 판정한다. 다음 모듈이 후보 구간을 만든다.

```python
# fall_synth.py — trigger(|a| > 1.7 g)를 넘은 4 s 후보 구간: 진짜 낙상 vs 일상 동작(ADL)
import numpy as np
from imu_synth import rot
def event(kind, rng, fs=50):
    n, k = 4 * fs, 2 * fs                                   # 200 샘플, 충격은 t = 2 s
    a = np.tile([0.0, 0.0, 1.0], (n, 1))                    # 자세: 중력만
    ff = {"fall": (0.25, 0.5), "jump": (0.15, 0.35), "lie": (0.1, 0.3)}.get(kind)
    if ff:                                                  # 자유낙하: |a|가 0에 가까워짐
        d = int(rng.uniform(*ff) * fs); a[k - d:k] *= rng.uniform(0.05, 0.5 if kind != "lie" else 0.7)
    peak = {"fall": (2.5, 6), "sit": (1.8, 3.2), "jump": (2.5, 5), "bump": (2, 6), "lie": (1.8, 2.6)}[kind]
    a[k:k + 4, 2] += rng.uniform(*peak) - 1                 # 충격 스파이크 80 ms
    lo, hi = {"fall": (30, 100), "lie": (40, 90), "sit": (0, 30)}.get(kind, (0, 20))
    t = np.deg2rad(rng.uniform(lo, hi)); c, s = np.cos(t), np.sin(t)   # 충격 후 몸이 기울어진 각
    a[k + 4:] = a[k + 4:] @ np.array([[1, 0, 0], [0, c, -s], [0, s, c]]).T
    if kind in ("jump", "bump") or rng.random() < 0.1:       # 이후에도 계속 움직임
        a[k + 10:] += rng.normal(0, rng.uniform(0.1, 0.4), (n - k - 10, 3))
    return a @ rot(rng, 180).T + rng.normal(0, 0.02, (n, 3)) # 임의 착용 방향 + 노이즈

def fall_features(a, fs=50):
    m = np.linalg.norm(a, axis=1); k = int(np.argmax(m > 1.7))  # trigger: 처음 1.7 g를 넘은 샘플
    pre, post = a[max(0, k - fs):max(1, k - fs // 2)].mean(0), a[min(k + fs, len(a) - 1):].mean(0)
    cosang = pre @ post / (np.linalg.norm(pre) * np.linalg.norm(post) + 1e-9)
    return [m[max(0, k - fs):k].min(), (m[max(0, k - fs):k] < 0.6).sum() / fs,   # 최소 |a|, 자유낙하 시간
            m[k:k + 5].max(), m[k + 10:].std(), np.degrees(np.arccos(np.clip(cosang, -1, 1)))]  # 최대 |a|, 이후 움직임, 자세 변화각
```

특징 5개는 모두 **착용 방향과 무관**하다: `|a|`의 최솟값·자유낙하 시간·최댓값·충격 후 흔들림, 그리고 충격 전후 중력 벡터 사이의 각도(두 벡터 모두 같은 기기 좌표계라 회전이 상쇄된다).

이 코드는 100 user-day 분량의 ADL 후보(하루 30회 trigger)와 낙상 50건으로 손 규칙, class weight 유무, 두 문턱을 비교한다.

```python
import numpy as np
from sklearn.model_selection import GroupKFold, cross_val_predict
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.linear_model import LogisticRegression
from fall_synth import event, fall_features
rng = np.random.default_rng(0)
DAYS = 100                                   # ADL 녹화: 사용자 10명 × 10일 = 100 user-day
adl_kinds = rng.choice(["sit", "jump", "bump", "lie"], size=30 * DAYS, p=[0.5, 0.1, 0.3, 0.1])  # 하루 30회 trigger
kinds = ["fall"] * 50 + list(adl_kinds)      # 낙상은 50건뿐 (연구실 매트 위에서 연기)
F = np.array([fall_features(event(k, rng)) for k in kinds]); y = np.array([k == "fall" for k in kinds], int)
g = rng.integers(0, 10, len(y))              # 사용자 ID
print(f"candidates {len(y)}, falls {y.sum()} ({100*y.mean():.2f} %)  → 'always no' accuracy {1 - y.mean():.4f}")
rule = (F[:, 1] >= 0.2) & (F[:, 2] > 2.5) & (F[:, 3] < 0.1) & (F[:, 4] > 45)   # 손으로 만든 규칙
print(f"rule  : recall {rule[y == 1].mean():.2f}, false alarms {rule[y == 0].sum():3d} → {rule[y == 0].sum() / DAYS:.2f} /day")
for cw in [None, "balanced"]:
    clf = make_pipeline(StandardScaler(), LogisticRegression(class_weight=cw))
    p = cross_val_predict(clf, F, y, groups=g, cv=GroupKFold(5), method="predict_proba")[:, 1]
    for th in [0.5, 0.1]:
        fa = ((p >= th) & (y == 0)).sum()
        print(f"logreg cw={str(cw):8s} th={th}: recall {(p[y == 1] >= th).mean():.2f}, false alarms {fa:3d} → {fa / DAYS:.2f} /day")
```

```text
candidates 3050, falls 50 (1.64 %)  → 'always no' accuracy 0.9836
rule  : recall 0.78, false alarms  11 → 0.11 /day
logreg cw=None     th=0.5: recall 0.78, false alarms   1 → 0.01 /day
logreg cw=None     th=0.1: recall 0.96, false alarms  28 → 0.28 /day
logreg cw=balanced th=0.5: recall 0.96, false alarms  75 → 0.75 /day
logreg cw=balanced th=0.1: recall 0.98, false alarms 170 → 1.70 /day
```

출력에서 볼 것:

- "항상 아니오"가 **98.36% 정확도**다. 낙상 감지에 accuracy를 보고하면 안 되는 이유.
- 손 규칙(자유낙하 ≥ 0.2 s AND 충격 > 2.5 g AND 이후 정지 AND 자세 변화 > 45°)은 낙상의 78%만 잡는다. 조건 네 개를 AND로 묶으면 하나만 빗나가도 놓친다 — 규칙이 엄격할수록 recall이 떨어진다.
- 같은 logistic regression이라도 **문턱 하나로 동작점이 크게 움직인다**: th 0.5는 recall 0.78 / 0.01회/일, th 0.1은 recall 0.96 / 0.28회/일. 모델을 바꾸기 전에 동작점부터 고른다.
- `class_weight="balanced"`는 드문 클래스의 오류에 큰 가중치를 줘서 확률 자체를 위로 민다. 효과는 **문턱을 낮추는 것과 비슷하다** (balanced th 0.5 ≈ 기본 th 0.1 근처). 불균형 처리는 "특별한 마법"이 아니라 결국 동작점 이동이다.
- 0.28회/일은 **사용자 한 명이 3~4일에 한 번 괜한 알람**을 받는다는 뜻이다. 제품으로는 너무 많을 수 있다. 그래서 실제 제품은 흔히 모델 뒤에 **사용자 확인 단계**(예: "괜찮으세요?" 알림 후 일정 시간 응답이 없으면 연락)를 둔다. 오경보의 비용을 UX로 낮추는 것도 시스템 설계다.

### 8.4 낙상 평가 체크리스트

- 후보 단위가 아니라 **시간 단위**로 오경보를 보고한다 (/일, /주).
- 사용자 단위 split (한 사람의 낙상 연기 5번이 train과 test에 나뉘면 안 된다).
- ADL은 **어려운 음성(hard negative)**을 의도적으로 모은다: 털썩 앉기, 점프, 침대에 눕기, 기기를 떨어뜨리기.
- recall은 신뢰구간과 함께, 오경보는 관찰 시간과 함께 쓴다.
- trigger 문턱이 recall의 **상한**이다. trigger가 놓친 낙상은 뒤의 모델이 볼 기회도 없다. trigger 자체의 recall을 따로 잰다.

---

## 9. 이상 탐지 — autoencoder와 통계 baseline

### 9.1 직관

라벨이 없거나 "이상"의 종류를 미리 다 알 수 없을 때 쓴다. 센서 고장(축 멈춤, glitch, FIFO 언더런), 처음 보는 움직임, 기기 손상 등이다. 아이디어는 **"정상만 배워 두고, 정상과 다르면 이상"**이다.

- **통계 baseline (z-score)**: 정상 데이터에서 특징마다 평균 μ와 표준편차 σ를 저장하고, 새 창의 `max_j |f_j − μ_j| / σ_j`가 크면 이상. 특징 하나하나를 **따로** 본다.
- **Autoencoder (AE)**: 입력을 좁은 bottleneck으로 압축했다가 복원하도록 **정상 데이터만으로** 학습한다. 정상은 잘 복원되고, 본 적 없는 패턴은 복원이 틀린다. 점수 = **reconstruction error** `‖x − x̂‖²`. 특징들 사이의 **관계**(예: "std가 크면 p2p도 크다")를 배우므로, 각각은 정상 범위인데 조합이 이상한 경우를 잡을 수 있다.

```
입력 x (특징 15개) ─▶ [15→8→3] ─▶ bottleneck 3 ─▶ [3→8→15] ─▶ x̂        점수 = mean((x − x̂)²)
                       encoder                      decoder
```

문턱은 **정상 데이터 점수의 높은 퍼센타일**(여기서는 99%)로 잡는다. 정의상 정상의 약 1%가 오경보가 된다.

### 9.2 코드 — 고장 5종 주입, 두 번의 시도

test 사용자(9~11)의 정상 창 540개에 고장을 하나씩 주입해 이상 창 540개를 만든다. 시도 A는 순진하게, 시도 B는 A4의 원칙대로 한다.

```python
import numpy as np, torch, torch.nn as nn
from sklearn.metrics import roc_auc_score
from imu_synth import dataset
from imu_feat import features
X, y, g = dataset(); rng = np.random.default_rng(0)
def inject(x, kind):                                 # 센서 고장 흉내 (x: [3, 128] 한 창)
    x = x.copy(); ax = rng.integers(3); s = rng.integers(20, 100)
    if kind == "stuck":   x[ax, s:] = x[ax, s]                  # 축 하나가 값에 멈춤
    if kind == "spike":   x[ax, s] += rng.choice([-1, 1]) * 6   # 1샘플 glitch (버스 오류)
    if kind == "bias":    x[ax, s:] += 0.3                      # 갑작스러운 offset 점프
    if kind == "dropout": x[:, s:s + 20] = 0                    # FIFO 언더런: 0으로 채워짐
    if kind == "tremor":  x += 0.15 * np.sin(2 * np.pi * 8 * np.arange(128) / 50)   # 처음 보는 8 Hz 진동
    return x
KINDS = ["stuck", "spike", "bias", "dropout", "tremor"]
te = g >= 9; Xa = np.stack([inject(x, KINDS[i % 5]) for i, x in enumerate(X[te])]); ka = np.arange(te.sum()) % 5

def run(cols, fit_users, thr_users):
    torch.manual_seed(0)
    Ffit, Fthr, Fte, Fan = [features(A)[:, cols] for A in (X[fit_users], X[thr_users], X[te], Xa)]
    mu, sd = Ffit.mean(0), Ffit.std(0) + 1e-6; d = Ffit.shape[1]
    z = lambda F: torch.tensor((F - mu) / sd, dtype=torch.float32)
    ae = nn.Sequential(nn.Linear(d, 8), nn.ReLU(), nn.Linear(8, 3), nn.ReLU(), nn.Linear(3, 8), nn.ReLU(), nn.Linear(8, d))
    opt = torch.optim.Adam(ae.parameters(), 1e-2)
    for ep in range(500):                            # 정상 창만으로 학습: 입력 = 정답
        opt.zero_grad(); ((ae(z(Ffit)) - z(Ffit)) ** 2).mean().backward(); opt.step()
    with torch.no_grad(): ae_s = [((ae(z(F)) - z(F)) ** 2).mean(1).numpy() for F in (Fthr, Fte, Fan)]
    zs = [np.abs((F - mu) / sd).max(1) for F in (Fthr, Fte, Fan)]       # baseline: 가장 큰 |z|
    for name, (s_thr, s_te, s_an) in [("AE", ae_s), ("z-max", zs)]:
        th = np.percentile(s_thr, 99)                                   # 정상의 99 퍼센타일 = 문턱
        auc = roc_auc_score(np.r_[0 * s_te, 0 * s_an + 1], np.r_[s_te, s_an])
        det = " ".join(f"{k}={np.mean(s_an[ka == i] > th):.2f}" for i, k in enumerate(KINDS))
        print(f"  {name:5s} FPR {np.mean(s_te > th):.3f} AUC {auc:.3f} | {det}")
    return ae_s, sum(p.numel() for p in ae.parameters())

print("A) 18 features, threshold from TRAIN users 0-8")
run(slice(None), g < 9, g < 9)
print("B) 15 features (no mean_x/y/z), fit users 0-5, threshold from HELD-OUT users 6-8")
(s_thr, s_te, s_an), n = run(list(range(3, 18)), g < 6, (g >= 6) & (g < 9))
print(f"AE params {n}"); np.savez("ae_err.npz", te=s_te, an=s_an, th=np.percentile(s_thr, 99))
```

```text
A) 18 features, threshold from TRAIN users 0-8
  AE    FPR 0.385 AUC 0.739 | stuck=0.48 spike=1.00 bias=0.49 dropout=0.99 tremor=0.46
  z-max FPR 0.019 AUC 0.686 | stuck=0.09 spike=1.00 bias=0.06 dropout=0.73 tremor=0.03
B) 15 features (no mean_x/y/z), fit users 0-5, threshold from HELD-OUT users 6-8
  AE    FPR 0.009 AUC 0.820 | stuck=0.13 spike=1.00 bias=0.12 dropout=1.00 tremor=0.00
  z-max FPR 0.022 AUC 0.682 | stuck=0.07 spike=1.00 bias=0.06 dropout=0.65 tremor=0.03
AE params 322
```

출력의 해석은 바로 아래 §9.3에서 한다.

### 9.3 결과 읽기 — AE가 "새 사용자"를 이상으로 본 이유

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
  <rect x="60.0" y="248.7" width="12.7" height="1.3" fill="#4a7bd0" fill-opacity="0.8"/> <rect x="85.5" y="238.4" width="12.7" height="11.6" fill="#4a7bd0" fill-opacity="0.8"/>
  <rect x="98.2" y="248.7" width="12.7" height="1.3" fill="#d0564a" fill-opacity="0.8"/> <rect x="110.9" y="208.6" width="12.7" height="41.4" fill="#4a7bd0" fill-opacity="0.8"/>
  <rect x="123.6" y="246.1" width="12.7" height="3.9" fill="#d0564a" fill-opacity="0.8"/> <rect x="136.4" y="184.0" width="12.7" height="66.0" fill="#4a7bd0" fill-opacity="0.8"/>
  <rect x="149.1" y="242.2" width="12.7" height="7.8" fill="#d0564a" fill-opacity="0.8"/> <rect x="161.8" y="168.5" width="12.7" height="81.5" fill="#4a7bd0" fill-opacity="0.8"/>
  <rect x="174.5" y="230.6" width="12.7" height="19.4" fill="#d0564a" fill-opacity="0.8"/> <rect x="187.3" y="151.6" width="12.7" height="98.4" fill="#4a7bd0" fill-opacity="0.8"/>
  <rect x="200.0" y="208.6" width="12.7" height="41.4" fill="#d0564a" fill-opacity="0.8"/> <rect x="212.7" y="94.7" width="12.7" height="155.3" fill="#4a7bd0" fill-opacity="0.8"/>
  <rect x="225.5" y="167.2" width="12.7" height="82.8" fill="#d0564a" fill-opacity="0.8"/> <rect x="238.2" y="42.9" width="12.7" height="207.1" fill="#4a7bd0" fill-opacity="0.8"/>
  <rect x="250.9" y="76.6" width="12.7" height="173.4" fill="#d0564a" fill-opacity="0.8"/> <rect x="263.6" y="218.9" width="12.7" height="31.1" fill="#4a7bd0" fill-opacity="0.8"/>
  <rect x="276.4" y="186.6" width="12.7" height="63.4" fill="#d0564a" fill-opacity="0.8"/> <rect x="289.1" y="244.8" width="12.7" height="5.2" fill="#4a7bd0" fill-opacity="0.8"/>
  <rect x="301.8" y="176.2" width="12.7" height="73.8" fill="#d0564a" fill-opacity="0.8"/> <rect x="327.3" y="160.7" width="12.7" height="89.3" fill="#d0564a" fill-opacity="0.8"/>
  <rect x="352.7" y="246.1" width="12.7" height="3.9" fill="#d0564a" fill-opacity="0.8"/> <rect x="378.2" y="237.1" width="12.7" height="12.9" fill="#d0564a" fill-opacity="0.8"/>
  <rect x="403.6" y="220.2" width="12.7" height="29.8" fill="#d0564a" fill-opacity="0.8"/> <rect x="429.1" y="204.7" width="12.7" height="45.3" fill="#d0564a" fill-opacity="0.8"/>
  <rect x="454.5" y="248.7" width="12.7" height="1.3" fill="#d0564a" fill-opacity="0.8"/> <rect x="480.0" y="203.4" width="12.7" height="46.6" fill="#d0564a" fill-opacity="0.8"/>
  <rect x="505.5" y="247.4" width="12.7" height="2.6" fill="#d0564a" fill-opacity="0.8"/> <line x1="60" y1="250" x2="620" y2="250" stroke="currentColor"/><line x1="60" y1="30" x2="60" y2="250" stroke="currentColor"/>
  <text x="110.9" y="266" font-size="12" text-anchor="middle">0.01</text> <text x="212.7" y="266" font-size="12" text-anchor="middle">0.1</text> <text x="314.5" y="266" font-size="12" text-anchor="middle">1</text>
  <text x="416.4" y="266" font-size="12" text-anchor="middle">10</text> <text x="518.2" y="266" font-size="12" text-anchor="middle">100</text> <text x="620.0" y="266" font-size="12" text-anchor="middle">1000</text>
  <text x="54" y="254.0" font-size="12" text-anchor="end">0</text> <text x="54" y="189.3" font-size="12" text-anchor="end">50</text> <text x="54" y="124.6" font-size="12" text-anchor="end">100</text>
  <text x="54" y="59.9" font-size="12" text-anchor="end">150</text> <line x1="285.3" y1="30" x2="285.3" y2="250" stroke="currentColor" stroke-width="1.5" stroke-dasharray="5 3"/>
  <text x="291.3" y="42" font-size="12">문턱 0.52 (held-out 정상 99%)</text> <rect x="420" y="60" width="12" height="12" fill="#4a7bd0"/><text x="438" y="71" font-size="12">정상 (새 사용자 540창)</text>
  <rect x="420" y="80" width="12" height="12" fill="#d0564a"/><text x="438" y="91" font-size="12">고장 주입 (540창)</text> <text x="340.0" y="284" font-size="12" text-anchor="middle">reconstruction error (MSE, log 축)</text>
</svg>
```

그림 7 — 시도 B의 reconstruction error 분포(log 축, 실제 값). 파랑: 처음 보는 사용자 9~11의 정상 창 540개, 빨강: 고장을 주입한 창 540개. 점선은 held-out 정상 사용자 6~8에서 정한 99% 문턱(0.52). 빨강의 오른쪽 봉우리들(spike, dropout)은 문턱을 훨씬 넘지만, 빨강의 상당 부분은 파랑과 겹친다(stuck, bias, tremor).

**시도 A가 실패한 이유**: AE가 새 사용자의 **정상 창 38.5%를 이상으로** 판정했다. `mean_x/y/z`(착용 방향)가 입력에 있어서 AE는 "학습 사용자 9명의 착용 방향"을 정상으로 외웠고, 방향이 다른 새 사용자는 전부 이상해 보였다. 게다가 문턱을 **학습 데이터의 점수**로 정했다 — 학습 데이터는 AE가 가장 잘 복원하는 데이터라 점수가 낙관적으로 낮다. 이상 탐지에서도 A4의 규칙이 그대로 적용된다: 문턱은 **학습에 안 쓴 사용자**로 정한다.

**시도 B**: 방향 특징을 빼고, AE는 사용자 0~5로 학습, 문턱은 6~8로 정했다. 새 사용자의 오경보가 0.9%로 설계값(1%)과 맞는다. 이것이 문턱이 제대로 교정됐다는 신호다.

**고장별 결과**:

| 고장 | AE (B) | z-max (B) | 왜 |
|---|---|---|---|
| spike (6 g glitch 1샘플) | 1.00 | 1.00 | p2p·std가 폭발 — 누구나 잡는다 |
| dropout (0 g 20샘플) | 1.00 | 0.65 | mag가 1 → 0으로 떨어짐. 여러 특징이 **함께** 어긋나는 것을 AE가 잡는다 |
| stuck (축 하나 정지) | 0.13 | 0.07 | 이 특징 세트에서 거의 안 보인다 |
| bias (+0.3 g offset) | 0.12 | 0.06 | 방향 특징을 뺐으니 offset이 안 보이는 게 당연하다 |
| tremor (8 Hz 진동) | 0.00 | 0.03 | 대역 특징이 6 Hz까지만 본다 — 8 Hz는 입력에 없다 |

핵심 교훈: **AE는 입력 표현에 없는 이상은 절대 못 잡는다**. AE가 z-score보다 AUC가 높은(0.820 vs 0.682) 것은 특징 사이의 관계를 보기 때문이지만, "stuck"이나 "8 Hz"는 특징에서 이미 지워졌다. 센서 고장 검출은 흔히 **전용 규칙**이 더 낫다: 같은 값 연속 N개(stuck), full-scale 값 연속(saturation, A6 §4.6), 타임스탬프 간격(drop, A6 §5.2), 고대역 에너지(진동). ML 이상 탐지는 그 규칙들이 못 잡는 "처음 보는 패턴"용 안전망이다.

### 9.4 임베디드에서의 AE

- AE(B)는 파라미터 322개, MAC 약 300개다. 특징 추출 뒤에 붙여도 forest보다 싸다.
- 기기에서는 점수만 계산하고 **문턱 초과 창을 로그로 올려**(H1, H7) 서버에서 사람이 본다. fleet 규모 데이터 품질 모니터링의 입구가 된다.
- 정상 분포는 FW 버전·센서 설정이 바뀌면 이동한다. 문턱은 버전별로 다시 교정한다.

---

## 10. 견고성 — 착용 위치·방향, ODR, 사용자, drift

### 10.1 문제 목록

| 문제 | 현장에서의 모습 | 모델에 생기는 일 | 대책 |
|---|---|---|---|
| 착용 방향 | 시계를 돌려 참, 반대 손목, 거꾸로 낀 이어버드 | 축별 특징·raw CNN이 방향을 외움 | 크기·중력 분해 특징, 회전 증강, 좌우 미러 증강(라벨 보존 시) |
| 착용 위치 | 손목 vs 주머니 vs 귀 | 신호 모양 자체가 다름 | 위치별 데이터 수집, 위치 분류기 선행 |
| ODR 불일치 | 설정 실수(100 Hz로 켬), 부품 간 클럭 오차 | 주파수 특징이 이동 | 타임스탬프 기반 재샘플링(A6 §5.3), ODR을 로그 헤더에 기록 |
| 사용자 차이 | 걸음 주파수·힘·체형 | 새 사용자에서 정확도 하락 | 사용자 단위 split, 사람 수 늘리기, 진폭 scaling·time-warp 증강 |
| bias drift | 온도 변화, 납땜 스트레스, 노화 | mean 기반 특징이 서서히 이동 | 정지 구간에서 bias 재추정, std·주파수 같은 DC 무관 특징 |
| 샘플 드롭 | FIFO overflow, 버스 오류 | 창 안 파형 왜곡 | 드롭 검출 후 창 폐기 (A6 §5.2) |

### 10.2 코드 — 착용 방향과 ODR 불일치

이 코드는 (A) 모두 "표준 방향(≤10°)"으로 찬 12명으로 forest를 학습시키고, 새 사용자 12명을 표준 방향과 제각각 방향(≤90°)으로 평가한다. 특징 세트 네 가지를 비교하는데, 넷째는 §2.2의 **중력 기준 분해**(수직 v, 수평 h) 특징 4개를 추가한 것이다. (B) 모델은 50 Hz를 가정하는데 실제 ODR이 다를 때를 본다.

```python
import numpy as np
from sklearn.ensemble import RandomForestClassifier
from imu_synth import dataset, make_user, session, rot, WIN, HOP
from imu_feat import features
rng = np.random.default_rng(0)
Xtr, ytr, _ = dataset(n_users=12, seed=0, max_tilt=10)          # 학습: 모두 '표준 방향'으로 착용
Xs0, ys0, _ = dataset(n_users=12, seed=5, max_tilt=10)          # 테스트: 새 사용자, 표준 방향
Xo, yo, _ = dataset(n_users=12, seed=5, max_tilt=90)            # 테스트 A: 같은 새 사용자, 착용 방향 제각각
def stream_test(fs_true, resample):                             # 테스트 B: 실제 ODR ≠ 50 Hz
    Xs, ys, rng = [], [], np.random.default_rng(1)             # 매번 같은 난수 → 조건만 다름
    for u in range(12):
        user = make_user(u, seed=7, max_tilt=10)
        for c in range(4):
            s = session(user, c, 60, rng, fs=fs_true)
            if resample:                                        # 타임스탬프 기준으로 50 Hz 격자에 보간
                t_new = np.arange(0, len(s) / fs_true, 1 / 50)
                s = np.stack([np.interp(t_new, np.arange(len(s)) / fs_true, s[:, i]) for i in range(3)], 1)
            idx = np.arange(0, len(s) - WIN + 1, HOP)
            Xs += [s[i:i + WIN].T for i in idx]; ys += [c] * len(idx)
    return np.stack(Xs).astype(np.float32), np.array(ys)
def grav(X):                                                    # 중력 방향 기준 분해: 수직 v, 수평 크기 h
    gh = X.mean(-1) / np.linalg.norm(X.mean(-1), axis=1, keepdims=True)   # 창 평균 ≈ 중력 방향 ĝ
    v = np.einsum("nc,ncl->nl", gh, X)                                    # v = a·ĝ
    h = np.linalg.norm(X - v[:, None, :] * gh[:, :, None], axis=1)        # h = |a − v·ĝ|
    return np.column_stack([v.std(-1), np.ptp(v, -1), h.mean(-1), h.std(-1)])
feat = {"all": lambda X: features(X), "inv": lambda X: features(X)[:, 9:],   # |a|·주파수 9개 (회전 불변)
        "inv+grav": lambda X: np.column_stack([features(X)[:, 9:], grav(X)])}
def fit(X, y, kind):
    return RandomForestClassifier(50, max_depth=8, random_state=0).fit(feat[kind](X), y)
aug = np.concatenate([Xtr] + [np.einsum("ij,njl->nil", rot(rng, 90), Xtr) for _ in range(3)]).astype(np.float32)
print("orientation shift (train tilt ≤10°, test tilt ≤90°)")
for name, m, k in [("all 18", fit(Xtr, ytr, "all"), "all"), ("all 18 + rot aug", fit(aug, np.tile(ytr, 4), "all"), "all"),
                   ("invariant 9", fit(Xtr, ytr, "inv"), "inv"), ("invariant 9 + grav 4", fit(Xtr, ytr, "inv+grav"), "inv+grav")]:
    print(f"  {name:20s} new users, same tilt {m.score(feat[k](Xs0), ys0):.3f}   rotated {m.score(feat[k](Xo), yo):.3f}")
m = fit(Xtr, ytr, "all")
print("ODR mismatch (model assumes 50 Hz)")
for fs_true, rs in [(50, False), (45, False), (100, False), (100, True)]:
    Xs, ys = stream_test(fs_true, rs)
    print(f"  true ODR {fs_true:3d} Hz, resample={rs!s:5s}: acc {m.score(features(Xs), ys):.3f}")
```

```text
orientation shift (train tilt ≤10°, test tilt ≤90°)
  all 18               new users, same tilt 0.989   rotated 0.912
  all 18 + rot aug     new users, same tilt 0.905   rotated 0.866
  invariant 9          new users, same tilt 0.876   rotated 0.874
  invariant 9 + grav 4 new users, same tilt 0.989   rotated 0.988
ODR mismatch (model assumes 50 Hz)
  true ODR  50 Hz, resample=False: acc 0.969
  true ODR  45 Hz, resample=False: acc 0.961
  true ODR 100 Hz, resample=False: acc 0.873
  true ODR 100 Hz, resample=True : acc 0.969
```

출력에서 볼 것:

- **축별 특징(all 18)**: 같은 방향의 새 사용자 0.989 → 돌려 찬 사용자 0.912. 방향만 바뀌었는데 8%p 떨어진다.
- **회전 불변 9개만**: 방향이 바뀌어도 0.874로 거의 그대로지만, 원래 성능도 0.876으로 낮다. `|a|` 하나로 합치면 "수직으로 흔들리나 수평으로 흔들리나"라는 정보가 사라지기 때문이다 — **불변성에는 정보 손실이라는 값이 붙는다**.
- **불변 9 + 중력 분해 4**: 0.989 / 0.988. 방향과 무관하면서 수직·수평 정보를 살렸다. **물리를 아는 특징 4개가 증강보다 낫다**. 이것이 "orientation을 어떻게 다루나"라는 면접 질문의 핵심 답이다.
- **회전 증강 + 축별 특징**은 이 설정에서 오히려 나빴다(0.905 / 0.866). 축별 특징이 방향을 담고 있는데 방향을 무작위로 섞으면, 트리가 쓸 수 있는 신호가 흐려진다. 증강은 raw CNN(§6.1, 0.778 → 0.906)처럼 **모델이 불변성을 스스로 배울 수 있을 때** 효과가 크다.
- **ODR**: 45 Hz(10% 오차)에서는 0.961로 거의 영향이 없지만, 100 Hz로 잘못 켜면 0.873. 128 샘플 창이 1.28 s가 되고 모든 주파수가 절반으로 보인다. 진폭 특징이 있어서 완전히 무너지지는 않는다. **타임스탬프로 50 Hz에 재샘플링하면 0.969로 회복**한다. 로그 헤더에 실제 ODR과 타임스탬프를 남기는 것(A6 §4)이 이래서 중요하다.

### 10.3 증강 도구 — rotation, scaling, time-warp

A4 §5.4에서 jitter·scaling·time shift·rotation을 봤다. IMU에서 추가로 중요한 것은 **time-warp**(시간축 늘이기·줄이기 = 걷는 속도 변화)다. 이 코드는 1.9 Hz 걷기 흉내에 세 가지 증강을 걸고 특징이 어떻게 변하는지 본다.

```python
import numpy as np
from imu_synth import rot
from imu_feat import features, NAMES
rng = np.random.default_rng(0)
t = np.arange(128) / 50
x = np.zeros((1, 3, 128)); x[0, 2] = 1 + 0.3 * np.sin(2 * np.pi * 1.9 * t)   # 1.9 Hz 걷기 흉내
def rotate(x, deg=60):   return np.einsum("ij,njl->nil", rot(rng, deg), x)     # 착용 방향
def scale(x, s=0.2):     return x * rng.uniform(1 - s, 1 + s)                   # 센서 gain·사람 힘
def time_warp(x, r):                                   # 보행 속도: 시간축을 r배로 늘이거나 줄임
    src = np.clip(np.arange(128) * r, 0, 127)          # 새 샘플 i ← 원래 시간 i·r 위치 (선형 보간)
    return np.stack([[np.interp(src, np.arange(128), ch) for ch in w] for w in x])
for name, xa in [("original", x), ("rotate 60°", rotate(x)), ("scale ±20%", scale(x)),
                 ("warp r=0.8", time_warp(x, 0.8)), ("warp r=1.2", time_warp(x, 1.2))]:
    f = features(xa.astype(np.float32))[0]
    print(f"{name:11s} mean_z {f[2]:+.3f}  mag_std {f[10]:.3f}  dom_freq {f[13]:.3f} Hz")
```

```text
original    mean_z +1.004  mag_std 0.213  dom_freq 1.953 Hz
rotate 60°  mean_z +0.969  mag_std 0.213  dom_freq 1.953 Hz
scale ±20%  mean_z +1.130  mag_std 0.240  dom_freq 1.953 Hz
warp r=0.8  mean_z +1.003  mag_std 0.213  dom_freq 1.562 Hz
warp r=1.2  mean_z +0.959  mag_std 0.218  dom_freq 2.344 Hz
```

출력에서 볼 것:

- 회전은 `mean_z`를 바꾸지만 `mag_std`는 소수점 셋째 자리까지 그대로다.
- scaling은 **중력까지** 1.13배로 키웠다(`mean_z` 1.130). 센서 gain 오차 흉내로는 맞지만 "사람의 힘" 흉내라면 **움직임 성분만** 키워야 한다 — 중력은 사람과 무관하게 1 g다. 증강이 흉내 내는 물리를 정확히 정해야 한다.
- time-warp r=0.8은 1.9 Hz를 1.52 Hz로(→ bin 1.562), r=1.2는 2.28 Hz로(→ bin 2.344) 옮긴다. 단 r=1.2는 원래 창 끝(127/1.2 ≈ 106번째 이후)을 넘어가서 마지막 21개 샘플이 끝값으로 **복사**된다(`np.clip`). 실무에서는 더 긴 원본 녹화에서 잘라 와야 이런 경계 아티팩트가 없다.
- 증강은 **train에만** 건다 (A4 §5.4).

### 10.4 bias drift

가속도계 bias는 온도에 따라 수 mg 단위로 움직인다(부품마다 데이터시트의 temperature coefficient 참조, G1). 대책:

- **mean 기반 특징을 줄인다**. std, p2p, 주파수 특징은 DC offset에 무관하다.
- **정지 구간에서 재교정**: `mag_std`가 매우 작은(가만히 있는) 창에서 `|a|`는 1 g여야 한다. 1 g과의 차이를 누적해 bias를 천천히 추정한다(완전한 3축 bias 추정에는 여러 자세의 정지 데이터가 필요하다 — G3의 6면 캘리브레이션).
- **fleet 모니터링**: 기기별 정지 구간 `|a|` 평균을 telemetry로 올리면(H7), bias가 튄 기기를 서버에서 찾을 수 있다. Don의 SSD telemetry 경험과 같은 구조다.

---

## 11. 임베디드 관점에서 다시 보기

### 11.1 always-on 예산 산수

가정(자릿수 감각용, 실제는 부품 데이터시트로): MCU active 64 MHz에서 3 mA, 창 hop 1.28 s.

```
특징 + forest   : 약 2만 cycle / 창   → 20,000 / 64 MHz = 0.31 ms
                  duty = 0.31 ms / 1.28 s = 0.024 %   → 평균 3 mA × 0.00024 ≈ 0.7 µA
1D-CNN (int8)   : 18만 MAC ≈ 20만 cycle 가정(약 1 MAC/cycle) → 3.1 ms
                  duty = 0.24 %                         → 평균 ≈ 7 µA
```

말로 하면: 이 규모에서는 **모델 연산 자체보다 "MCU를 깨우는 횟수"와 센서 전류가 더 크다**. IMU를 저전력 모드로 켜 두는 것만으로 수~수십 µA가 들 수 있고, MCU가 매 샘플 인터럽트로 깨면 wake-up 오버헤드가 누적된다. 그래서:

1. **FIFO + watermark**로 샘플을 모아 한 번에 읽는다 (G2) — 50 Hz × 64 샘플이면 1.28 s에 한 번 깬다.
2. **wake-on-motion**(stage 0): 가속도 변화가 문턱을 넘을 때만 IMU가 인터럽트를 낸다. 가만히 있는 동안 MCU는 HAR를 돌리지도 않는다 — "움직임 없음 = still"은 공짜 판정이다.
3. **판정이 안 바뀌면 보고하지 않는다** (§7의 FSM). 큰 코어·무선을 깨우는 것이 전력의 대부분이다.

### 11.2 특징 추출을 정수로 — C 구현

IMU raw는 int16 count다. 특징을 **변환 없이 정수로** 계산하면 FPU 없는 코어(Cortex-M0+)나 IMU 내장 코어 수준에서도 돈다. 이 코드는 ±4 g(8192 LSB/g) int16 창에서 평균·분산·`|a|`(정수 제곱근)·zero-crossing을 정수로 계산하고 double 결과와 비교한다.

```c
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#define L 128                     /* 2.56 s @ 50 Hz */
#define LSB_PER_G 8192            /* ±4 g full-scale, int16 */
#define PI 3.14159265358979323846

static uint32_t isqrt32(uint32_t v) {          /* 비트 단위 정수 제곱근: 곱셈 없음, 16회 반복 */
    uint32_t r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return r;
}
typedef struct { int32_t mean[3]; uint32_t var[3]; int32_t mag_mean, mag_p2p; uint32_t mag_var; int zc; } featq_t;

static void features_q(const int16_t a[L][3], featq_t *f) {
    int64_t s[3] = {0}, ss[3] = {0}, ms = 0, mss = 0; uint16_t mag[L]; int32_t mn = INT32_MAX, mx = 0;
    for (int i = 0; i < L; i++) {
        uint32_t sq = 0;
        for (int c = 0; c < 3; c++) { int32_t v = a[i][c]; s[c] += v; ss[c] += v * v; sq += (uint32_t)(v * v); }
        mag[i] = (uint16_t)isqrt32(sq);                           /* |a| in LSB */
        ms += mag[i]; mss += (int64_t)mag[i] * mag[i];
        if (mag[i] < mn) mn = mag[i];
        if (mag[i] > mx) mx = mag[i];
    }
    for (int c = 0; c < 3; c++) { f->mean[c] = (int32_t)(s[c] >> 7); f->var[c] = (uint32_t)((ss[c] >> 7) - (int64_t)f->mean[c] * f->mean[c]); }
    f->mag_mean = (int32_t)(ms >> 7); f->mag_var = (uint32_t)((mss >> 7) - (int64_t)f->mag_mean * f->mag_mean);
    f->mag_p2p = mx - mn; f->zc = 0;
    for (int i = 1; i < L; i++) f->zc += ((mag[i] > f->mag_mean) != (mag[i - 1] > f->mag_mean));
}
int main(void) {
    static int16_t a[L][3]; double fa[L][3];
    for (int i = 0; i < L; i++) {                 /* 걷기 흉내: 1.9 Hz 수직 흔들림 + 기울어진 중력 */
        double t = i / 50.0, w = 2 * PI * 1.9 * t;
        fa[i][0] = 0.34 + 0.15 * sin(w + 1.0); fa[i][1] = 0.12 + 0.07 * sin(0.5 * w);
        fa[i][2] = 0.93 + 0.30 * sin(w) + 0.09 * sin(2 * w + 0.5);
        for (int c = 0; c < 3; c++) a[i][c] = (int16_t)lrint(fa[i][c] * LSB_PER_G);
    }
    featq_t f; features_q(a, &f);
    double m = 0, m2 = 0, mag[L];                 /* float 기준값 (golden) */
    for (int i = 0; i < L; i++) { mag[i] = sqrt(fa[i][0]*fa[i][0] + fa[i][1]*fa[i][1] + fa[i][2]*fa[i][2]); m += mag[i]; m2 += mag[i]*mag[i]; }
    m /= L; double sd = sqrt(m2 / L - m * m);
    printf("mag_mean  fixed %.5f g   float %.5f g\n", f.mag_mean / (double)LSB_PER_G, m);
    printf("mag_std   fixed %.5f g   float %.5f g\n", sqrt((double)f.mag_var) / LSB_PER_G, sd);
    printf("mean_z    fixed %.5f g   std_z fixed %.5f g   mag_zc %d\n", f.mean[2] / (double)LSB_PER_G, sqrt((double)f.var[2]) / LSB_PER_G, f.zc);
    printf("state: raw window %zu B, feature struct %zu B\n", sizeof a, sizeof f);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 featq.c -o featq -lm && ./featq
```

```text
mag_mean  fixed 1.00891 g   float 1.00900 g
mag_std   fixed 0.22580 g   float 0.22566 g
mean_z    fixed 0.93542 g   std_z fixed 0.22179 g   mag_zc 9
state: raw window 768 B, feature struct 40 B
```

출력에서 볼 것:

- 정수 버전과 double 버전의 차이는 `mag_mean` 0.09 mg, `mag_std` 0.14 mg다. 1 LSB = 0.12 mg이므로 **LSB 한두 개 수준**이고, 트리 임계값(수십~수백 mg)에 비하면 무시할 만하다. 단, 임계값 바로 옆의 창은 방향이 바뀔 수 있으니 §5처럼 전수 비교로 확인한다.
- `mag_zc` 9 ≈ 2 × 1.9 Hz × 2.56 s = 9.7. 
- 창 버퍼 768 B, 특징 40 B. 특징 18개를 계산한 뒤 raw 창은 버려도 된다(hop만큼만 남기면 됨).
- **연산 수**: 샘플당 곱셈 3번(제곱) + 제곱근 1번(곱셈 없는 16회 반복) + 누적 덧셈 약 8번. 창당 곱셈 약 500번. FFT를 더해도 창당 수천 cycle 수준이다 — §11.1의 "2만 cycle"은 넉넉한 가정이다.

> 함정: `s[c] >> 7`은 128로 나누기지만, 음수에서는 C의 `/`(0 방향 절삭)와 달리 **음의 무한대 방향**으로 내림된다(부호 있는 음수의 오른쪽 shift는 구현 정의이며 흔한 컴파일러는 산술 shift). Python의 `np.mean`과 LSB 하나가 다를 수 있다. 분산은 `E[x²] − E[x]²` 형태라 값이 크고 비슷한 두 수의 뺄셈이다 — float32에서는 정밀도 손실(catastrophic cancellation)이 나지만, 여기서는 int64로 정확히 계산했다. float로 한다면 평균을 먼저 빼는 2-pass나 Welford 알고리즘을 쓴다.

### 11.3 어디서 돌릴까 — IMU 코어 vs MCU vs DSP

| 위치 | 돌릴 수 있는 것 | 장점 | 한계 |
|---|---|---|---|
| IMU 내장 로직 (wake-on-motion, 내장 기능, ST MLC 같은 ML core) | 문턱·FSM, 제한된 특징 + 작은 decision tree | 가장 낮은 전력, MCU를 재울 수 있음 | 특징·트리 크기가 고정 한도, 벤더 도구 종속, 디버깅 어려움 |
| always-on MCU (Cortex-M 계열) | 특징 + tree/forest/boosting, 작은 CNN, FSM | 유연, C로 무엇이든, OTA 쉬움 | RAM·flash 수십~수백 KB, 켜 있는 시간만큼 전류 |
| DSP (예: Hexagon, HiFi — E4) | CNN, GRU, 오디오와 결합한 멀티모달 | MAC이 싸고 SIMD | 깨우는 비용이 큼, 툴체인 복잡 |
| SoC / NPU | 큰 모델 | 성능 | always-on에는 전력이 안 맞음 |

실무 설계는 **cascade**다: IMU가 움직임을 잡고(stage 0) → MCU가 forest로 "제스처 후보"를 고르고(stage 1) → 필요하면 DSP의 CNN이 정밀 판정(stage 2). 각 단계의 **false positive rate가 다음 단계의 깨우는 빈도**가 된다. 예를 들어 stage 1이 창의 1%만 통과시키면 stage 2의 평균 전류는 1/100이 된다.

### 11.4 모델 배포와 OTA

- 트리 **테이블 형식**(§5.2)은 코드와 데이터가 분리돼 있어서, 모델 갱신이 flash의 데이터 영역 교체로 끝난다. 버전·CRC·특징 정의 버전을 테이블 헤더에 넣는다 (J 모듈의 OTA 패턴).
- 특징 정의(창 길이, 대역 경계, 단위)가 바뀌면 **모델과 특징 코드가 함께** 바뀌어야 한다. 둘의 버전을 하나로 묶는다.
- IMU 설정(ODR, full-scale, 필터)도 모델 입력의 일부다. 센서 드라이버가 설정을 바꾸면 모델이 조용히 틀린다(§10.2의 100 Hz 사례).

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 창 단위 무작위 split | 오프라인 99~100%, 기기에서 80%대 | 겹친 창·같은 사람이 train/test에 동시에 | `GroupKFold`로 사용자 단위 평가 (§4.4) |
| 축별 특징·raw만 사용 | 특정 사용자·착용 방향에서만 무너짐 | 모델이 착용 방향을 외움 | 크기·중력 분해 특징, 회전 증강 (§10.2) |
| 낙상 등 드문 사건에 accuracy 보고 | 98% "정확도"인데 쓸모없음 | 기저율 | recall + false alarm/일, 신뢰구간 (§8.2) |
| 단일 문턱으로 이벤트 발생 | 상태가 깜빡이고 알림 폭주 | 경계 근처 잡음 | hysteresis + debounce FSM (§7.3) |
| 문턱을 학습 데이터 점수로 결정 | 새 사용자에서 오경보 폭증 (38%) | 학습 데이터는 점수가 낙관적 | held-out 사용자로 문턱 교정 (§9.3) |
| C 특징 정의가 Python과 다름 | C 트리는 sklearn과 100% 같은데 기기 정확도가 낮음 | ddof, 창, 단위, Hann window 차이 | raw 창 golden vector로 특징까지 전수 비교 |
| 임계값을 float로 반올림·`≤`를 `<`로 | 극소수 창에서만 다른 클래스 | 경계값 처리 | 임계값 정밀도 유지, 전수 비교 (§5.3) |
| ODR 설정 변경을 모델에 반영 안 함 | 업데이트 후 정확도 서서히·갑자기 하락 | 주파수 특징 이동 | ODR·타임스탬프 로깅, 재샘플링 (§10.2) |
| impurity importance만 보고 특징 제거 | 특징을 뺐더니 새 사용자에서 하락 | 상관 특징의 중요도 분할, 편향 | permutation importance, 빼고 재학습 |
| 증강이 흉내 내는 물리를 틀림 | 증강했더니 성능 하락 | 중력까지 scaling, 라벨 바뀌는 반전 | 움직임 성분만 scaling, 라벨 보존 검토 (§10.3) |

---

## 13. 면접에서 이렇게 말한다

**Q.** "Design a raise-to-wake gesture detector for a wrist wearable."

**A.** 지연 예산(약 300 ms 안에 화면)과 오작동 예산(하루 몇 번까지 허용, 걸을 때 팔 흔들기는 오작동)을 먼저 정한다. stage 0은 IMU의 any-motion 인터럽트로 MCU를 깨우고, stage 1은 중력 벡터의 방향 변화(손목이 눈높이로 돌아옴 = 특정 축으로의 pitch/roll 변화)와 그 후 짧은 정지를 보는 FSM 또는 작은 트리다. 걷기 중 팔 흔들기는 hard negative로 모아 학습하고, 평가는 사용자 단위로 recall과 "하루 오작동 횟수"로 한다. 후처리는 refractory 1~2초.

> "I'd start from the budgets: roughly 300 ms latency and a false-wake budget per day. Stage zero is the IMU's any-motion interrupt; stage one on the MCU tracks the gravity vector — a rotation of the wrist toward the face followed by a short hold — using a small state machine or decision tree on orientation-invariant features. I'd collect arm swings while walking as hard negatives, evaluate per user with recall and false wakes per day, and add a refractory period after each wake."

**Q.** "Random forest or CNN for HAR on an MCU?"

**A.** 데이터가 수십 명 수준이고 클래스가 에너지·주파수로 갈리는 HAR이면 특징 + forest가 먼저다. 수 KB, 곱셈 없는 비교, 해석 가능하고, 실험에서도 사용자 단위로 forest가 증강 없는 CNN보다 좋았다. 사용자 수가 커지거나 파형 모양이 중요한 제스처라면 CNN이 따라잡거나 이긴다. 결정은 같은 사용자 단위 평가에서 정확도·크기·창당 연산·전류로 한다.

> "With tens of users and classes separated by energy and frequency, I'd ship features plus a random forest first: a few kilobytes, comparisons only, interpretable. A small 1D-CNN catches up when there are many more users or when waveform shape matters, like gestures. I'd decide with the same per-user cross-validation and compare accuracy, bytes, ops per window and average current — often the answer is a cascade: forest always on, CNN on the DSP only for candidates."

**Q.** "How do you evaluate a fall detector?"

**A.** accuracy는 쓰지 않는다. recall(신뢰구간 포함)과 false alarm/일을 쓰고, 사용자 단위 split으로 평가한다. ADL에서 hard negative(털썩 앉기, 점프, 눕기)를 의도적으로 모으고, "0회 관찰 → 95% 상한 3/T"로 필요한 관찰 시간을 계산한다. trigger 단계의 recall을 따로 재고, 연기한 낙상과 실제 낙상의 차이를 한계로 명시한다.

> "Not with accuracy. I report recall with a confidence interval and false alarms per user-day, split by user. I deliberately collect hard negatives like sitting down hard, jumping and lying down, and I size the negative data with the rule of three — zero false alarms in T days bounds the rate at about 3 over T. I also measure the trigger stage's recall separately, since it caps the whole system."

**Q.** "How do you handle device orientation?"

**A.** 세 가지 층위로. 특징에서는 가속도 크기와 중력 기준 분해(수직 성분 v = a·ĝ, 수평 크기 h)를 쓴다 — 실험에서 축별 특징은 돌려 찬 사용자에서 8%p 떨어졌지만 중력 분해 특징은 그대로였다. raw 모델에는 회전 증강을 건다. 그리고 평가 데이터에 다양한 착용 방향이 들어가게 수집한다.

> "At three levels. Features: magnitude plus a gravity-aligned decomposition — vertical component a dot g-hat and horizontal magnitude — which are invariant to how the device is mounted. For raw-input models, rotation augmentation during training. And data: make sure test users wear it in different orientations, because per-axis models silently memorize the mounting."

**Q.** "Why are decision trees popular in sensor hubs and IMU ML cores?"

**A.** 추론이 비교와 분기뿐이라 곱셈기·FPU가 필요 없고, 노드당 10 B 안팎이라 수백 바이트에 들어간다. 특징(mean, variance, peak-to-peak, zero-crossing)도 누적 합과 비교로 계산된다. 규칙을 사람이 검토할 수 있다. 그래서 ST의 MLC처럼 센서 안에서 트리를 돌리는 제품이 있다.

> "Inference is just compare-and-branch, so no multiplier or FPU is needed, and a tree is on the order of ten bytes per node. The features it consumes — mean, variance, peak-to-peak, zero crossings — are running sums and comparisons. And the rules are reviewable. That's why some IMUs, like ST parts with a machine learning core, run decision trees inside the sensor."

**Q.** "How would you detect sensor faults or anomalies without labels?"

**A.** 먼저 규칙: stuck(같은 값 연속), saturation, 타임스탬프 gap, glitch. 그다음 정상 데이터로 z-score나 작은 autoencoder를 학습하고, 문턱은 held-out 사용자의 정상 점수 퍼센타일로 정한다. AE는 입력 표현에 없는 이상은 못 잡으므로 표현을 고장 모드에 맞게 설계한다.

> "Rules first — stuck values, saturation, timestamp gaps, glitches. Then a statistical baseline and a small autoencoder trained on normal windows only, with the threshold calibrated on held-out users. An autoencoder only sees what its input representation contains, so I design features around the failure modes I care about."

---

## 14. 직접 해보기

1. 손계산: 창 20개(walk 10, stairs 10)를 `band_mid ≤ 0.5`로 나눴더니 왼쪽이 (walk 2, stairs 8), 오른쪽이 (walk 8, stairs 2)다. 분할 이득은? 정답: 부모 Gini 0.5, 자식 각각 1 − (0.04 + 0.64) = 0.32, 가중 평균 0.32 → 이득 0.18.
2. 손계산: 1D conv `Conv1d(6, 24, k=7, stride=1, padding=3)`에 길이 100 입력. params와 MAC은? 정답: params = 24·(6·7) + 24 = 1,032, L_out = 100, MACs = 100·24·42 = 100,800.
3. 손계산: 오경보 목표가 "주당 1회 이하"다. 오경보 0회로 이것을 95% 수준에서 주장하려면 몇 user-day의 ADL 데이터가 필요한가? 정답: 3 / (1/7) = 21 user-day.
4. 코드: §4.5의 random forest(30 트리)를 테이블 형식으로 내보내고 C에서 **확률 평균**으로 예측해 sklearn `predict`와 540개 전부 비교하라. 힌트: leaf의 `value[n]`(클래스 비율)을 `float[4]`로 저장하고 30개 트리의 leaf 비율을 더한 뒤 argmax.
5. 코드: `imu_feat.py`에 "6~20 Hz 대역 에너지 비율"과 "축별 최대 연속 동일값 길이"를 추가하고 §9 시도 B를 다시 돌려라. tremor와 stuck의 검출률이 어떻게 바뀌나? 힌트: 입력에 없던 정보가 들어오면 AE와 z-score 모두 크게 오른다 — 표현이 먼저다.
6. 코드: §7.3 FSM에 refractory(ON 전이 후 5초간 OFF 전이 금지)를 추가하고, 확률 스트림을 바꿔 가며 이벤트 수가 어떻게 달라지는지 확인하라. 힌트: `uint16_t hold` 카운터를 상태 구조체에 추가하고 `wear_step` 첫 줄에서 감소시킨다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| HAR | Human Activity Recognition | 창마다 활동(가만히·걷기·달리기…)을 분류 |
| ADL | Activities of Daily Living | 낙상 감지에서 "낙상이 아닌 일상 동작" |
| ODR | Output Data Rate | IMU가 샘플을 내는 속도 (Hz) |
| Wake-on-motion | 움직임 인터럽트 | 가속도 변화가 문턱을 넘으면 IMU가 MCU를 깨움 |
| Hand-crafted feature | 사람이 설계한 특징 | mean, std, p2p, zero-crossing, 대역 에너지 등 |
| Magnitude | 벡터 크기 | 3축을 하나로 합친 회전 불변 값 |
| Gravity-aligned decomposition | 중력 기준 분해 | 추정한 중력 방향으로 수직/수평 성분을 나눔 |
| Decision tree | 결정 트리 | 특징-임계값 비교를 반복해 leaf의 클래스를 냄 |
| Gini impurity | 지니 불순도 | 1 − ∑p², 노드가 얼마나 섞였나 |
| Random forest | 랜덤 포레스트 | 무작위로 다르게 학습한 트리들의 확률 평균 |
| Gradient boosting | 그래디언트 부스팅 | 얕은 트리를 순서대로 쌓아 잔차를 보정 |
| kNN | k-nearest neighbors | 가장 가까운 k개 학습 샘플의 다수결 |
| GroupKFold | 그룹 단위 교차검증 | 같은 그룹(사용자)을 한 fold에 몰아넣음 |
| Feature importance | 특징 중요도 | impurity 감소 합 또는 permutation 시 성능 하락 |
| Hysteresis | 이력 현상 | 켜는 문턱과 끄는 문턱을 다르게 |
| Debounce | 디바운스 | 조건이 N회 연속일 때만 전이 |
| Refractory period | 재발동 금지 시간 | 이벤트 후 일정 시간 같은 이벤트 무시 |
| Autoencoder | 자기부호화기 | 압축 후 복원을 배워, 복원 오차로 이상 판정 |
| Reconstruction error | 복원 오차 | 입력과 복원 출력의 차이 제곱 평균 |
| Time-warp | 시간 왜곡 증강 | 시간축을 늘이거나 줄여 속도 변화를 흉내 |
| MLC | Machine Learning Core | 일부 ST IMU 안에서 decision tree를 돌리는 블록 |
| Cascade | 단계형 검출 | 싼 단계가 비싼 단계를 깨울지 결정 |

---

## 16. 요약 & 체크리스트

IMU 모델은 웨어러블에서 가장 오래 켜져 있는 가장 작은 모델이다. 과제를 입력·창·지연·always-on 여부로 먼저 분해하고, 규칙과 FSM으로 충분한지부터 본다. ML이 필요하면 **특징 + 트리 계열**이 첫 선택이다: 수백 바이트~수십 KB, 곱셈 없는 추론, C로 내보내 golden 비교가 쉽고, 일부 IMU는 센서 안에서 트리를 돌린다. 평가는 반드시 **사용자 단위**로 한다 — 무작위 split은 99~100%를 보여 주며 거짓말한다. 딥러닝(1D-CNN, GRU)은 raw 파형을 직접 보지만 데이터가 적으면 착용 방향 같은 변동을 외우며, 증강이나 사용자 수가 늘어나야 따라잡는다. 출력은 투표·확률 평균·hysteresis·debounce로 안정화하고, 낙상처럼 드문 사건은 recall과 false alarm/일, 관찰 시간 산수로 평가한다. 이상 탐지 AE는 정상만으로 배우지만 입력 표현에 없는 이상은 못 잡고, 문턱은 held-out 사용자로 정한다. 착용 방향은 크기·중력 기준 분해 특징으로, ODR은 타임스탬프 재샘플링으로 다룬다. 전력은 모델 연산보다 "무엇을 얼마나 자주 깨우나"가 결정한다.

- [ ] 웨어러블 IMU 과제 6종을 입력·창·지연·always-on·모델 크기로 표를 그릴 수 있다
- [ ] `|a|`와 중력 기준 분해(v = a·ĝ, h = |a − v·ĝ|)를 손으로 계산하고 왜 회전 불변인지 설명할 수 있다
- [ ] 2 Hz 사인파의 std, zero-crossing, FFT bin을 손으로 예측할 수 있다 (Δf = fs/L)
- [ ] Gini impurity와 분할 이득을 손으로 계산할 수 있다
- [ ] `GroupKFold`로 사용자 단위 평가를 하고 무작위 split과의 차이를 설명할 수 있다
- [ ] sklearn 트리의 `tree_` 배열을 if/else와 테이블 C 코드로 내보내고 전수 비교할 수 있다
- [ ] Conv1d와 GRU의 params·MAC을 손으로 계산할 수 있다
- [ ] hysteresis + debounce FSM을 C로 짜고 지연과 안정성의 trade-off를 말할 수 있다
- [ ] 낙상 감지를 recall·false alarm/일·rule of three로 평가하는 법을 말할 수 있다
- [ ] autoencoder 이상 탐지의 문턱을 올바르게 정하고, 표현의 한계를 설명할 수 있다

---

## 참고 자료

- scikit-learn User Guide — Decision Trees, Ensembles, Cross-validation(`GroupKFold`), Permutation importance: https://scikit-learn.org/stable/user_guide.html
- PyTorch 문서 — `nn.Conv1d`, `nn.GRU`, forward hooks: https://pytorch.org/docs/stable/nn.html
- Anguita et al., "A Public Domain Dataset for Human Activity Recognition Using Smartphones", ESANN 2013 — UCI HAR 데이터셋 (UCI Machine Learning Repository에서 "Human Activity Recognition Using Smartphones")
- Hammerla, Halloran, Plötz, "Deep, Convolutional, and Recurrent Models for Human Activity Recognition Using Wearables", IJCAI 2016
- Ordóñez, Roggen, "Deep Convolutional and LSTM Recurrent Neural Networks for Multimodal Wearable Activity Recognition", Sensors 2016 (DeepConvLSTM)
- Um et al., "Data Augmentation of Wearable Sensor Data for Parkinson's Disease Monitoring using Convolutional Neural Networks", ICMI 2017 — rotation, scaling, time-warp 등 IMU 증강
- Pete Warden, Daniel Situnayake, "TinyML" (O'Reilly, 2019) — 가속도계 제스처(magic wand) 예제
- ST 앱노트 AN5259 "LSM6DSOX: Machine Learning Core" 및 해당 IMU 데이터시트 — 센서 내장 decision tree
- 관련 노트: A4(평가·leakage·FAR), A6(윈도잉·특징·바이너리 로그), B2(CNN), B3(GRU), G1~G3(IMU 원리·드라이버·센서 퓨전), H1·H7(로깅·품질 모니터링)
