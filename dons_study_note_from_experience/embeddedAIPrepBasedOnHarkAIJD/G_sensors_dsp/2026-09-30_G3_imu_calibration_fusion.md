# G3. IMU 보정과 센서 퓨전 — bias·scale·misalignment, 회전 표현, quaternion, complementary·Madgwick·Kalman

> **이 노트를 다 읽으면**: 가속도계·자이로·지자기계의 오차 모델(bias·scale·misalignment·hard/soft iron·온도)을 세우고 최소제곱으로 보정 파라미터를 뽑을 수 있다 · 회전행렬·Euler·axis-angle·quaternion을 서로 바꾸고 quaternion 연산을 직접 구현해 scipy와 맞춰 볼 수 있다 · complementary·Mahony·Madgwick·Kalman(1축 KF, ESKF)을 같은 데이터에서 구현·비교하고 gain을 근거 있게 고를 수 있다 · MCU에서의 연산 비용과 "퓨전 출력 vs raw를 모델에" 판단을 설명할 수 있다
> **JD 연결**: "Hands-on with IMUs, accelerometers, gyroscopes, microphones", "Build data collection and ingestion pipelines … various sensors" — study_prep_list G3 행: calibration (bias, scale, misalignment) · 좌표계, 회전 행렬, **quaternion** · complementary filter, Madgwick/Mahony, Kalman/EKF (전처리·특징, 로보틱스 연결)
> **Don 기준 난이도**: 선형대수·미적분·최소제곱·제어 루프(PI)·고정소수점·측정 기반 디버깅은 이미 있다 / 회전을 다루는 표기(quaternion 곱의 순서, frame 방향), 필터들 사이의 관계(complementary = Mahony의 P항 = 정상상태 Kalman), 그리고 "어떤 오차가 어떤 증상을 내는가"는 새로 배운다
> **선행 노트**: A1(행렬·직교행렬·SVD), A2(Gaussian noise·평균의 분산), B7(중력 제거·방향 불변 특징), G1(IMU 물리·noise·Allan variance), G2(드라이버·FIFO)

---

## 0. 큰 그림 — 이게 왜 필요한가

IMU 칩이 SPI로 내 주는 숫자는 **"물리량"이 아니라 "물리량에 가까운 무언가"**다. 가속도계는 기울기에 따라 1 g를 3축에 나눠 찍는데, 그 값에는 영점 오차(bias), 감도 오차(scale), 축이 정확히 90°가 아닌 오차(misalignment), 온도에 따른 흔들림이 섞여 있다. 자이로는 가만히 있어도 0이 아닌 각속도를 내고, 이걸 적분하면 자세가 계속 흘러간다(drift). 지자기계는 기판 위 스피커 자석과 배터리 때문에 지구 자기장을 찌그러진 모양으로 본다.

이 노트는 그 숫자를 **믿을 수 있는 자세(orientation)·중력 방향·선가속도**로 바꾸는 두 단계를 다룬다.

1. **보정(calibration)** — 센서마다 고정된(또는 온도에 따라 천천히 변하는) 오차를 모델로 세우고 파라미터를 측정해 빼 준다. 펌웨어로 말하면 **공장 trim 값**이다. ADC offset/gain trim, RF 칩의 IQ imbalance 보정과 같은 종류의 일이다.
2. **센서 퓨전(sensor fusion)** — 서로 다른 약점을 가진 센서를 섞는다. 자이로는 짧게는 정확하지만 길게는 흘러가고, 가속도계는 길게는 정확(중력은 안 변하니까)하지만 짧게는 움직임에 흔들린다. 둘을 주파수 대역별로 섞는 것이 핵심이다. 펌웨어로 말하면 **PLL**과 같다: 짧은 시간은 VCO(자이로)를 믿고, 긴 시간은 기준 클럭(중력·자기장)에 천천히 끌어당긴다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="g3a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <rect x="10" y="40" width="120" height="70" rx="6" fill="#888" fill-opacity="0.2" stroke="currentColor"/> <text x="70" y="64" font-size="13" text-anchor="middle">IMU raw</text> <text x="70" y="82" font-size="12" text-anchor="middle">accel · gyro · mag</text> <text x="70" y="98" font-size="12" text-anchor="middle">int16 LSB (G1·G2)</text>
  <rect x="160" y="30" width="150" height="90" rx="6" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/> <text x="235" y="52" font-size="13" text-anchor="middle">① 보정 (§1–2)</text> <text x="235" y="70" font-size="12" text-anchor="middle">bias · scale · misalign</text> <text x="235" y="86" font-size="12" text-anchor="middle">온도 보상</text> <text x="235" y="102" font-size="12" text-anchor="middle">hard/soft iron</text> <rect x="340" y="40" width="120" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/>
  <text x="400" y="64" font-size="13" text-anchor="middle">② 축 remap (§3)</text> <text x="400" y="82" font-size="12" text-anchor="middle">sensor → body</text> <text x="400" y="98" font-size="12" text-anchor="middle">det = +1 확인</text> <rect x="490" y="30" width="180" height="90" rx="6" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/> <text x="580" y="52" font-size="13" text-anchor="middle">③ 퓨전 필터 (§5–8)</text> <text x="580" y="70" font-size="12" text-anchor="middle">complementary</text> <text x="580" y="86" font-size="12" text-anchor="middle">Mahony · Madgwick</text>
  <text x="580" y="102" font-size="12" text-anchor="middle">Kalman · ESKF</text> <line x1="130" y1="75" x2="158" y2="75" stroke="currentColor" stroke-width="1.5" marker-end="url(#g3a)"/> <line x1="310" y1="75" x2="338" y2="75" stroke="currentColor" stroke-width="1.5" marker-end="url(#g3a)"/> <line x1="460" y1="75" x2="488" y2="75" stroke="currentColor" stroke-width="1.5" marker-end="url(#g3a)"/> <rect x="400" y="170" width="120" height="44" rx="6" fill="none" stroke="currentColor"/> <text x="460" y="190" font-size="12" text-anchor="middle">자세 q (UI·AR·로봇)</text>
  <text x="460" y="205" font-size="12" text-anchor="middle">heading</text> <rect x="540" y="170" width="130" height="44" rx="6" fill="none" stroke="currentColor"/> <text x="605" y="190" font-size="12" text-anchor="middle">중력 벡터 g_b</text> <text x="605" y="205" font-size="12" text-anchor="middle">선가속도 a − g</text> <line x1="560" y1="120" x2="480" y2="168" stroke="currentColor" stroke-width="1.5" marker-end="url(#g3a)"/> <line x1="600" y1="120" x2="604" y2="168" stroke="currentColor" stroke-width="1.5" marker-end="url(#g3a)"/>
  <rect x="300" y="250" width="370" height="40" rx="6" fill="#d0564a" fill-opacity="0.2" stroke="currentColor"/> <text x="485" y="275" font-size="13" text-anchor="middle">④ 특징 · ML 모델 (B7) — 제스처 · HAR · 낙상 (§10)</text> <line x1="605" y1="214" x2="605" y2="248" stroke="currentColor" stroke-width="1.5" marker-end="url(#g3a)"/> <path d="M235,120 L235,270 L298,270" fill="none" stroke="#888" stroke-width="1.5" stroke-dasharray="5 4" marker-end="url(#g3a)"/> <text x="120" y="200" font-size="12">보정만 하고 raw를 그대로</text> <text x="120" y="216" font-size="12">모델에 넣는 길도 있다 (§10)</text>
</svg>
```

그림 1 — 이 노트의 파이프라인. raw → ① 보정 → ② 축 맞추기 → ③ 퓨전 → 자세·중력·선가속도 → ④ 특징/모델(B7). 회색 점선처럼 퓨전 없이 보정된 raw를 모델에 바로 넣는 선택지도 있다(§10).

edge ML 엔지니어 입장에서 이 주제가 중요한 이유:

- **모델 입력의 품질**: B7에서 본 것처럼 IMU 모델은 착용 방향에 약하다. 중력 방향을 정확히 알면 "수직/수평 성분" 같은 방향 불변 특징을 만들 수 있다. 중력을 정확히 알려면 퓨전이 필요하다.
- **데이터 수집 파이프라인**: 보정 안 된 센서로 모은 데이터는 기기마다 bias가 달라서 모델이 "기기 ID"를 배운다. 보정 파라미터는 로그 메타데이터(H3)에 같이 남겨야 한다.
- **로보틱스·AR**: Don이 관심 있는 로봇·웨어러블 칩 쪽에서는 자세 추정 자체가 제품 기능이다. 면접에서 "quaternion 왜 쓰냐", "complementary filter alpha 어떻게 고르냐"는 단골 질문이다.

이 노트의 모든 숫자는 **합성 데이터**에서 나온다. 정답(ground truth) 자세를 알고 있으므로 각 방법의 오차를 정확히 잴 수 있다. 실제 센서는 G1의 noise 특성(Allan variance)과 G2의 드라이버 설정에 따라 숫자가 달라진다.

예제 코드는 스크래치 폴더 한 곳에 모아 두고 실행한다. 모듈 파일(`g3_quat.py`, `g3_sim.py`, `g3_filters.py`, `g3_eskf.py`, `g3_ahrs.py`, `g3_magsim.py`)을 먼저 저장하고, 예제 스크립트가 그것을 import한다.

---

## 1. 센서 오차 모델과 보정 기초

### 1.1 직관 — 자로 재는데 자가 틀렸다

자(ruler)가 세 가지 방식으로 틀릴 수 있다고 생각해 보자.

- 0 눈금이 1 mm 밀려 있다 → **bias(offset)**. 무엇을 재든 1 mm씩 더 나온다.
- 눈금 간격이 2% 넓다 → **scale factor 오차**. 긴 것을 잴수록 오차가 커진다.
- 자를 비스듬히 대고 잰다 → **misalignment**. 재려던 방향 말고 다른 방향 성분이 섞인다.

3축 센서는 자가 세 개 붙어 있는 것이고, 셋이 서로 정확히 직각이 아닐 수도 있다(MEMS 공정·패키지·PCB 납땜 기울기).

### 1.2 정의 — 3축 오차 모델

```
y = S · M · x + b + n

  x ∈ ℝ³   : 참값 (가속도면 specific force [m/s²], 자이로면 각속도 [rad/s])
  y ∈ ℝ³   : 센서 출력 (LSB를 단위로 바꾼 값)
  S = diag(s_x, s_y, s_z)          : 축별 감도 (이상적이면 1)
  M = [ 1    m_xy  m_xz ]          : 축 어긋남 (cross-axis). 대각은 1, 비대각은 작은 각도 [rad]
      [ m_yx  1    m_yz ]
      [ m_zx m_zy  1    ]
  b ∈ ℝ³   : bias (영점 오차). 온도 T의 함수 b(T)일 수 있다
  n        : noise (white noise + bias instability 등, G1)
```

말로 하면: 참값을 살짝 비틀고(M), 축마다 늘이고(S), 밀어 놓은(b) 것에 noise가 얹힌 것이 센서 출력이다. 보정은 이 식을 거꾸로 푸는 것이다.

```
x̂ = (S·M)⁻¹ · (y − b̂)     ← 펌웨어에서 매 샘플 적용 (3×3 곱 1번 + 뺄셈 3번)
```

`K = S·M`을 한 덩어리 3×3 행렬로 보면 미지수는 `K`의 9개 + `b`의 3개 = **12개**다. 보정 절차는 결국 "12개 숫자를 측정으로 알아내는 일"이다. `K`를 S와 M으로 나눌 필요는 보통 없다. 펌웨어는 `K⁻¹` 하나만 저장하면 된다.

> 함정: `M`의 "회전 부분"(세 축이 직교인 채로 통째로 돌아간 것)은 센서 칩이 PCB에 비스듬히 붙은 것과 구별이 안 된다. 이것은 보정이 아니라 **장착 회전(mounting, §3)** 문제로 따로 다룬다. 6-position 보정은 "기준 자세(지그)"에 대한 상대 각도를 재므로, 지그가 기기 외곽과 맞으면 둘이 한꺼번에 흡수된다.

**자이로**도 같은 모델이지만 실무 우선순위가 다르다. 자이로 scale 오차 1%는 360° 돌렸을 때 3.6° 오차라서 중요하지만, 그걸 재려면 **회전 테이블(rate table)**이 필요하다. 반면 자이로 bias는 가만히 두기만 하면 잴 수 있고, 적분되어 각도 오차가 시간에 비례해 커지므로 **가장 먼저, 가장 자주** 보정한다.

### 1.3 자이로 bias — 가만히 두고 평균

가만히 있을 때 `x = 0`이므로 `y = b + n`. 평균을 내면 `b̂ = mean(y)`. 평균의 표준편차는 A2에서 본 대로 `σ/√N`이다.

**손계산**: 자이로 noise σ = 0.10 dps (100 Hz 샘플당), 1초(N = 100) 평균이면 `0.10/√100 = 0.010 dps`. 10초면 `0.10/√1000 ≈ 0.0032 dps`. 0.01 dps bias 오차는 1분 적분하면 0.6°다.

이 √N 이득은 **white noise일 때만** 성립한다. 실제 MEMS 자이로는 수십~수백 초 이상 평균하면 bias instability(flicker)와 rate random walk 때문에 더 좋아지지 않는다. 몇 초가 적당한지는 Allan deviation 곡선의 바닥(G1)이 알려 준다.

### 1.4 온도 보상 — bias는 온도의 함수

MEMS 자이로·가속도계의 bias는 온도에 따라 수 mdps/°C ~ 수십 mdps/°C 수준으로 움직인다(부품마다 다르므로 데이터시트의 "zero-rate level change vs temperature" 항목을 본다). 웨어러블은 손목에 차는 순간 수 분에 걸쳐 피부 온도 쪽으로 데워지고, 충전 중에는 더 뜨거워진다. 그래서 **25°C에서 잰 bias 하나로는 부족**하다.

절차는 Don이 챔버 자동화로 해 본 것과 같다: 온도를 스윕하며 정지 상태 평균을 기록 → `b(T)`를 다항식으로 fit → 계수를 NVM에 저장 → 런타임에 IMU 내장 온도 센서로 `b(T)`를 계산해 뺀다.

아래 코드는 정지 평균의 √N 법칙을 확인하고, 13개 온도에서 잰 bias를 1차·2차로 fit했을 때 남는 오차를 비교한다.

```python
# 자이로 bias: 정지 평균으로 추정 + 온도에 대한 1차 보상
import numpy as np
rng = np.random.default_rng(4); fs = 100
def bias_true(T): return 0.50 + 0.012 * (T - 25) + 0.00015 * (T - 25)**2   # dps, 약간 휘어 있음
def still_mean(T, sec):                                 # 정지 상태에서 sec초 평균
    n = int(sec * fs)
    return (bias_true(T) + rng.normal(0, 0.10, n)).mean()

for sec in [0.1, 1, 10]:
    est = [still_mean(25, sec) for _ in range(200)]
    print(f"정지 {sec:4.1f} s 평균: bias 추정 표준편차 = {np.std(est):.4f} dps (이론 {0.10/np.sqrt(sec*fs):.4f})")

T = np.arange(-10, 51, 5.0)                             # 챔버에서 온도 스윕
b_meas = np.array([still_mean(t, 2) for t in T])
k1, k0 = np.polyfit(T - 25, b_meas, 1)                  # b(T) ≈ k0 + k1·(T−25)
print(f"fit: b(T) = {k0:.4f} + {k1:.5f}·(T−25) dps")
c2 = np.polyfit(T - 25, b_meas, 2)                      # 2차 fit도 비교
print("잔여 bias [dps] (1분 적분 시 각도 오차 = ×60°)")
for t in [0, 25, 45]:
    r0 = bias_true(t) - b_meas[T == 25][0]              # 25°C 값 하나만 뺐을 때
    r1 = bias_true(t) - (k0 + k1 * (t - 25))
    r2 = bias_true(t) - np.polyval(c2, t - 25)
    print(f"  T={t:2.0f}°C  고정 {r0:+.3f}  1차 {r1:+.3f}  2차 {r2:+.3f}")
```

```text
정지  0.1 s 평균: bias 추정 표준편차 = 0.0315 dps (이론 0.0316)
정지  1.0 s 평균: bias 추정 표준편차 = 0.0102 dps (이론 0.0100)
정지 10.0 s 평균: bias 추정 표준편차 = 0.0033 dps (이론 0.0032)
fit: b(T) = 0.5465 + 0.01047·(T−25) dps
잔여 bias [dps] (1분 적분 시 각도 오차 = ×60°)
  T= 0°C  고정 -0.199  1차 +0.009  2차 +0.002
  T=25°C  고정 +0.008  1차 -0.046  2차 +0.001
  T=45°C  고정 +0.308  1차 +0.044  2차 +0.004
```

출력에서 볼 것: (1) 평균 시간의 √ 에 반비례해 bias 추정이 정확해진다(이론값과 일치). (2) 25°C 값 하나로 고정 보정하면 45°C에서 0.308 dps가 남는다 — 1분이면 18.5°가 흘러간다. 1차 fit은 0.044 dps, 2차 fit은 0.004 dps까지 줄인다. 여기서 실제 bias를 일부러 살짝 휘게 만들었기 때문에 1차 fit이 25°C 근처에서 오히려 0.046 dps를 남긴 점도 눈여겨보자. 모델 차수는 **데이터가 정한다**.

```svg
<svg viewBox="0 0 640 360" xmlns="http://www.w3.org/2000/svg">
  <line x1="80.0" y1="300.0" x2="600.0" y2="300.0" stroke="currentColor" stroke-width="1"/> <line x1="80.0" y1="40.0" x2="80.0" y2="300.0" stroke="currentColor" stroke-width="1"/> <line x1="80.0" y1="300.0" x2="80.0" y2="305.0" stroke="currentColor" stroke-width="1"/> <text x="80.0" y="320.0" font-size="12" text-anchor="middle">-10</text> <line x1="166.7" y1="300.0" x2="166.7" y2="305.0" stroke="currentColor" stroke-width="1"/> <text x="166.7" y="320.0" font-size="12" text-anchor="middle">0</text> <line x1="253.3" y1="300.0" x2="253.3" y2="305.0" stroke="currentColor" stroke-width="1"/>
  <text x="253.3" y="320.0" font-size="12" text-anchor="middle">10</text> <line x1="340.0" y1="300.0" x2="340.0" y2="305.0" stroke="currentColor" stroke-width="1"/> <text x="340.0" y="320.0" font-size="12" text-anchor="middle">20</text> <line x1="426.7" y1="300.0" x2="426.7" y2="305.0" stroke="currentColor" stroke-width="1"/> <text x="426.7" y="320.0" font-size="12" text-anchor="middle">30</text> <line x1="513.3" y1="300.0" x2="513.3" y2="305.0" stroke="currentColor" stroke-width="1"/> <text x="513.3" y="320.0" font-size="12" text-anchor="middle">40</text>
  <line x1="600.0" y1="300.0" x2="600.0" y2="305.0" stroke="currentColor" stroke-width="1"/> <text x="600.0" y="320.0" font-size="12" text-anchor="middle">50</text> <line x1="75.0" y1="300.0" x2="80.0" y2="300.0" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="304.0" font-size="12" text-anchor="end">0.2</text> <line x1="75.0" y1="235.0" x2="80.0" y2="235.0" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="239.0" font-size="12" text-anchor="end">0.4</text> <line x1="75.0" y1="170.0" x2="80.0" y2="170.0" stroke="currentColor" stroke-width="1"/>
  <text x="70.0" y="174.0" font-size="12" text-anchor="end">0.6</text> <line x1="75.0" y1="105.0" x2="80.0" y2="105.0" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="109.0" font-size="12" text-anchor="end">0.8</text> <line x1="75.0" y1="40.0" x2="80.0" y2="40.0" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="44.0" font-size="12" text-anchor="end">1.0</text>
  <polyline fill="none" stroke="#888" stroke-width="1.5" stroke-dasharray="4 3" points="80.0,279.3 88.7,278.7 97.3,278.1 106.0,277.4 114.7,276.6 123.3,275.6 132.0,274.6 140.7,273.5 149.3,272.3 158.0,270.9 166.7,269.5 175.3,268.0 184.0,266.4 192.7,264.7 201.3,262.9 210.0,261.0 218.7,259.0 227.3,256.9 236.0,254.7 244.7,252.4 253.3,250.0 262.0,247.5 270.7,245.0 279.3,242.3 288.0,239.5 296.7,236.6 305.3,233.7 314.0,230.6 322.7,227.4 331.3,224.1 340.0,220.8 348.7,217.3 357.3,213.8 366.0,210.1 374.7,206.4 383.3,202.5 392.0,198.6 400.7,194.5 409.3,190.4 418.0,186.1 426.7,181.8 435.3,177.3 444.0,172.8 452.7,168.2 461.3,163.5 470.0,158.6 478.7,153.7 487.3,148.7 496.0,143.6 504.7,138.3 513.3,133.0 522.0,127.6 530.7,122.1 539.3,116.5 548.0,110.8 556.7,105.0 565.3,99.1 574.0,93.1 582.7,87.0 591.3,80.8 600.0,74.5"/>
  <polyline fill="none" stroke="#4a7bd0" stroke-width="2" points="80.0,306.5 88.7,303.1 97.3,299.7 106.0,296.3 114.7,292.9 123.3,289.5 132.0,286.1 140.7,282.7 149.3,279.3 158.0,275.9 166.7,272.5 175.3,269.1 184.0,265.7 192.7,262.3 201.3,258.9 210.0,255.5 218.7,252.1 227.3,248.7 236.0,245.3 244.7,241.9 253.3,238.4 262.0,235.0 270.7,231.6 279.3,228.2 288.0,224.8 296.7,221.4 305.3,218.0 314.0,214.6 322.7,211.2 331.3,207.8 340.0,204.4 348.7,201.0 357.3,197.6 366.0,194.2 374.7,190.8 383.3,187.4 392.0,184.0 400.7,180.6 409.3,177.2 418.0,173.8 426.7,170.4 435.3,167.0 444.0,163.6 452.7,160.2 461.3,156.8 470.0,153.4 478.7,149.9 487.3,146.5 496.0,143.1 504.7,139.7 513.3,136.3 522.0,132.9 530.7,129.5 539.3,126.1 548.0,122.7 556.7,119.3 565.3,115.9 574.0,112.5 582.7,109.1 591.3,105.7 600.0,102.3"/>
  <line x1="80.0" y1="204.9" x2="600.0" y2="204.9" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="6 4"/> <circle cx="80.0" cy="282.1" r="4" fill="#d0564a"/> <circle cx="123.3" cy="276.8" r="4" fill="#d0564a"/> <circle cx="166.7" cy="266.2" r="4" fill="#d0564a"/> <circle cx="210.0" cy="261.5" r="4" fill="#d0564a"/> <circle cx="253.3" cy="249.5" r="4" fill="#d0564a"/> <circle cx="296.7" cy="237.2" r="4" fill="#d0564a"/> <circle cx="340.0" cy="223.5" r="4" fill="#d0564a"/> <circle cx="383.3" cy="204.9" r="4" fill="#d0564a"/> <circle cx="426.7" cy="180.8" r="4" fill="#d0564a"/>
  <circle cx="470.0" cy="160.3" r="4" fill="#d0564a"/> <circle cx="513.3" cy="131.6" r="4" fill="#d0564a"/> <circle cx="556.7" cy="105.5" r="4" fill="#d0564a"/> <circle cx="600.0" cy="77.2" r="4" fill="#d0564a"/> <text x="340.0" y="345.0" font-size="13" text-anchor="middle">온도 [°C]</text> <text x="20.0" y="30.0" font-size="13" text-anchor="start">gyro bias [dps]</text> <circle cx="110" cy="62" r="4" fill="#d0564a"/> <text x="120.0" y="66.0" font-size="12" text-anchor="start">챔버 측정 (각 2 s 평균)</text> <line x1="102.0" y1="84.0" x2="118.0" y2="84.0" stroke="#4a7bd0" stroke-width="2"/>
  <text x="124.0" y="88.0" font-size="12" text-anchor="start">1차 fit: 0.546 + 0.0105·(T−25)</text> <line x1="102.0" y1="106.0" x2="118.0" y2="106.0" stroke="#888" stroke-width="1.5" stroke-dasharray="4 3"/> <text x="124.0" y="110.0" font-size="12" text-anchor="start">실제 bias (약간 휜 곡선)</text> <line x1="102.0" y1="128.0" x2="118.0" y2="128.0" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="6 4"/> <text x="124.0" y="132.0" font-size="12" text-anchor="start">25°C 값 하나로 고정 보정</text>
</svg>
```

그림 2 — 온도 스윕으로 잰 gyro bias(빨간 점)와 1차 fit(파랑). 주황 점선(25°C 값 고정)은 온도가 멀어질수록 오차가 커진다. 회색 점선은 시뮬레이션의 실제 곡선이다.

> 함정: 온도 보상은 **IMU 칩 내부 온도**로 해야 한다. MCU나 배터리 온도는 지연과 오프셋이 있다. 또 온도가 빠르게 변하는 중에는(충전 시작 직후) 칩 내부에 온도 구배가 생겨 정적 fit이 안 맞는 hysteresis가 생긴다. 챔버에서 "올리면서"와 "내리면서" 둘 다 재 보면 드러난다.

### 1.5 가속도계 6-position 보정 — 12개 미지수를 최소제곱으로

가속도계는 **중력이라는 공짜 기준**이 있다. 기기를 6방향(+x 위, −x 위, +y, −y, +z, −z)으로 놓으면 참값 `x`는 각각 `(±g, 0, 0)`, `(0, ±g, 0)`, `(0, 0, ±g)`로 알려져 있다.

**1축 손계산 (닫힌 형태)**: x축의 scale이 1.02, bias가 0.15 m/s²라면

```
+x 위:  y₊ = 1.02 · 9.81 + 0.15 = 10.1562
−x 위:  y₋ = 1.02 · (−9.81) + 0.15 = −9.8562
b̂  = (y₊ + y₋) / 2      = (10.1562 − 9.8562) / 2 = 0.15
ŝ  = (y₊ − y₋) / (2g)   = 20.0124 / 19.62       = 1.02
```

말로 하면: 반대 방향 두 자세의 **합**에서 중력은 상쇄되고 bias만 두 번 남는다. **차**에서는 bias가 상쇄되고 감도만 남는다. 3축이면 각 자세에서 다른 두 축 값은 cross-axis 항(`K`의 비대각)을 알려 준다.

**행렬 형태 (최소제곱)**: 자세 i의 측정 `y_i = K·x_i + b`를 행 벡터로 쓰면

```
y_iᵀ = [x_iᵀ  1] · Θ          Θ = [ Kᵀ ]  (4×3 행렬, 미지수 12개)
                                   [ bᵀ ]

Y (6×3) = X₁ (6×4) · Θ (4×3)   →   Θ̂ = (X₁ᵀX₁)⁻¹ X₁ᵀ Y   (정규방정식, lstsq)
```

방정식이 6 × 3 = 18개, 미지수가 12개라서 과결정(overdetermined)이고, 최소제곱이 noise를 평균낸다. 자세를 더 많이(예: 12~24개 임의 자세) 쓰면 더 튼튼해진다. 6-position에서는 최소제곱 해가 위의 닫힌 형태와 **정확히 같다**는 것도 코드로 확인한다.

```python
# 가속도계 6-position 보정: y = S·M·x + b 의 12개 파라미터를 최소제곱으로 복원
import numpy as np
rng = np.random.default_rng(1); g = 9.81
S = np.diag([1.02, 0.97, 1.01])                          # scale (1이 이상적)
M = np.array([[1, 0.010, -0.006], [0.004, 1, 0.012], [-0.008, 0.005, 1]])  # 축 어긋남
b = np.array([0.15, -0.08, 0.25])                        # bias [m/s²]
K_true = S @ M

poses = g * np.vstack([np.eye(3), -np.eye(3)])           # +x,+y,+z,−x,−y,−z 가 위를 향함
Y = np.array([(K_true @ x + b + rng.normal(0, 0.05, (200, 3))).mean(0) for x in poses])
X1 = np.hstack([poses, np.ones((6, 1))])                 # [x, 1] (6×4)
Theta, *_ = np.linalg.lstsq(X1, Y, rcond=None)           # Y ≈ X1·Θ, Θ = [Kᵀ; bᵀ] (4×3)
K_est, b_est = Theta[:3].T, Theta[3]
print("b 참값  =", b, "\nb 추정  =", np.round(b_est, 4))
print("S 추정  =", np.round(np.diag(K_est), 4), " (M 대각=1 로 두면 S_ii = K_ii)")
print("M 추정  =\n", np.round(K_est / np.diag(K_est)[:, None], 4))

# 닫힌 형태: b = (y₊ + y₋)/2, K의 i열 = (y₊ − y₋)/(2g)
b_cf = (Y[:3] + Y[3:]).mean(0) / 2                      # 세 쌍의 평균
K_cf = (Y[:3] - Y[3:]).T / (2 * g)
print("닫힌형태 b =", np.round(b_cf, 4), "| lstsq와 K 차이 max =", np.abs(K_cf - K_est).max())
y_raw = K_true @ np.array([0, 0, g]) + b                 # 평평하게 둔 기기의 raw
x_cal = np.linalg.solve(K_est, y_raw - b_est)            # 보정 적용: x = K⁻¹(y − b)
print("raw |a| =", round(np.linalg.norm(y_raw), 4), "→ 보정 후 |a| =", round(np.linalg.norm(x_cal), 4))
```

```text
b 참값  = [ 0.15 -0.08  0.25] 
b 추정  = [ 0.1515 -0.0789  0.2466]
S 추정  = [1.0201 0.9698 1.0106]  (M 대각=1 로 두면 S_ii = K_ii)
M 추정  =
 [[ 1.      0.0098 -0.0064]
 [ 0.004   1.      0.0121]
 [-0.0087  0.0047  1.    ]]
닫힌형태 b = [ 0.1515 -0.0789  0.2466] | lstsq와 K 차이 max = 6.661338147750939e-16
raw |a| = 10.1586 → 보정 후 |a| = 9.808
```

출력에서 볼 것: bias를 약 0.003 m/s² 이내, scale을 0.001 이내, cross-axis 항을 0.001 이내로 복원했다(각 자세 200샘플 평균, noise σ = 0.05). 최소제곱과 닫힌 형태의 차이는 6.7e-16(부동소수점 반올림)뿐이다. 보정 전에는 평평하게 둔 기기가 |a| = 10.16 m/s²(1.036 g)를 냈는데, 보정 후 9.808로 돌아왔다.

**임베디드 연결**: 공장 라인에서는 6면 지그(정육면체 블록)나 2축 짐벌로 자세를 바꾸고, 자세마다 정지 후 수백 ms 평균을 낸다. Don의 factory test 경험 그대로다 — 자세 전환 후 **settle 시간**, 정지 판정(자이로 크기 < 문턱), 결과 sanity check(보정 후 6자세 모두 |a| ≈ g, 범위를 벗어나면 FAIL) 순서다. 계산된 `K⁻¹`(9 float)와 `b`(3 float) = 48 B를 NVM에 저장한다.

> 함정: 6-position은 **지그가 정확히 수평**이라고 가정한다. 책상이 1° 기울어 있으면 "참값" 자체가 틀려서 그 오차가 misalignment 추정으로 들어간다. 지그 없이 하려면 "모든 정지 자세에서 |K⁻¹(y − b)| = g"라는 조건만 쓰는 **multi-position(타원체) 방법**을 쓴다 — 다음 절의 지자기계 타원체 피팅과 같은 수학이다(Tedaldi et al. 2014).

---

## 2. 지자기계 보정 — hard iron, soft iron, 타원체 피팅

### 2.1 직관 — 구가 찌그러진 타원체가 된다

지구 자기장은 (짧은 거리에서) 크기가 일정한 벡터다. 기기를 이리저리 돌리면 body 좌표에서 본 자기장 벡터의 끝점은 **반지름이 일정한 구** 위를 돈다. 그런데 기기 안에는 자석이 있다.

- **Hard iron**: 스피커·진동 모터의 영구자석, 자화된 나사. 기기와 같이 돌아가므로 body 좌표에서 **고정된 벡터를 더한다** → 구의 **중심이 이동**한다.
- **Soft iron**: 배터리 캔·차폐 판 같은 연자성체. 외부 자기장을 휘게 만들어 방향에 따라 감도가 달라진다 → 구가 **타원체로 늘어나고 기울어진다**.

```
m_raw = A_soft · m_body + h_hard + n

보정:  m̂ = W · (m_raw − ĉ)        (ĉ ≈ h_hard,  W ≈ A_soft⁻¹)
```

말로 하면: 중심을 원점으로 끌어오고(hard iron), 타원체를 다시 구로 눌러 펴면(soft iron) 된다. 가속도계 오차 모델 `y = K·x + b`와 똑같은 모양이다.

### 2.2 타원체 피팅 유도

점 `p`가 타원체 위에 있다는 조건은 이차형식(quadric)으로 쓸 수 있다.

```
(p − c)ᵀ A (p − c) = k            A: 3×3 대칭 양의정치, c: 중심

전개하면  pᵀAp − 2cᵀAp + (cᵀAc − k) = 0
→ 계수로 쓰면  a·x² + b·y² + c·z² + 2d·xy + 2e·xz + 2f·yz + 2g·x + 2h·y + 2i·z = 1
   (상수항을 1로 정규화. 미지수 9개, 점 하나당 방정식 1개 → lstsq)

복원:  A = [[a d e] [d b f] [e f c]]
       [g h i]ᵀ = −A·c   →   c = −A⁻¹ [g h i]ᵀ
       k = 1 + cᵀAc
       W = √(A/k) · F     (F = 지구 자기장 세기. 행렬 제곱근은 대칭 해를 고른다)
```

말로 하면: 9개 계수는 **선형** 최소제곱으로 한 번에 풀린다(반복 최적화가 필요 없다). 그 계수에서 중심과 모양을 대수적으로 꺼낸다. `W`가 대칭 행렬 제곱근이라는 선택 때문에, soft iron에 섞인 **회전 성분은 복원할 수 없다**는 점은 기억해 둔다(자이로·가속도계와 맞춰 보는 단계에서 따로 잡는다).

**2D 손계산 직관**: 책상 위에서 기기를 한 바퀴 돌리면서 mx, my의 최대·최소를 기록한다. mx가 −25 ~ +75, my가 −32 ~ +8이면 hard iron 중심 추정은 `((−25 + 75)/2, (−32 + 8)/2) = (25, −12)`. 반지름이 x는 50, y는 20이면 soft iron(또는 기울어진 평면) 때문에 축마다 감도가 다른 것이다. 최소·최대법은 가장 싼 보정이고, 타원체 피팅은 그것을 3D·모든 점으로 일반화한 것이다.

### 2.3 코드 — 왜곡된 데이터를 만들고 되돌리기

먼저 왜곡 모델을 담은 모듈. 기기를 손에 들고 이리저리 돌린 400개 자세와, 책상 위에서 10°씩 한 바퀴 돌린 36개 자세를 만든다.

```python
# g3_magsim.py — 왜곡된 지자기계 데이터 (hard iron + soft iron + noise)
import numpy as np
from scipy.spatial.transform import Rotation as R
m_world = np.array([0.0, 20.0, -45.0])                   # ENU: 북쪽(+y) 20 µT, 아래로 45 µT
A_soft = np.array([[1.10, 0.08, 0.02], [0.08, 0.90, -0.05], [0.02, -0.05, 1.00]])
h_hard = np.array([25.0, -12.0, 40.0])                   # µT, 기판 위 자석·스피커 등
def distort(m_body, rng):
    return m_body @ A_soft.T + h_hard + rng.normal(0, 0.5, m_body.shape)
def body_field(rots):                                    # 여러 자세에서 본 지구 자기장 (body)
    return rots.inv().apply(m_world)
rng = np.random.default_rng(2)
raw3d = distort(body_field(R.random(400, random_state=3)), rng)          # 손에 들고 이리저리
yaws = np.deg2rad(np.arange(0, 360, 10))
flat = R.from_euler("z", yaws)                                            # 책상 위에서 한 바퀴
raw_flat = distort(body_field(flat), rng)
```

이제 9개 계수 lstsq → 중심 → 모양 → 보정, 그리고 평평한 상태에서 heading(북쪽 기준 방위) 오차를 비교한다.

```python
# 타원체 피팅: hard iron(중심) + soft iron(모양) 복원 → 구로 되돌리기
import numpy as np
from scipy.linalg import sqrtm
from g3_magsim import raw3d, raw_flat, yaws, m_world, h_hard, A_soft
x, y, z = raw3d.T
D = np.column_stack([x*x, y*y, z*z, 2*x*y, 2*x*z, 2*y*z, 2*x, 2*y, 2*z])
p, *_ = np.linalg.lstsq(D, np.ones(len(x)), rcond=None)  # 9개 계수, 우변 = 1
A = np.array([[p[0], p[3], p[4]], [p[3], p[1], p[5]], [p[4], p[5], p[2]]])
c = -np.linalg.solve(A, p[6:9])                          # 중심 = hard iron
k = 1 + c @ A @ c                                        # (x−c)ᵀA(x−c) = k
F = np.linalg.norm(m_world)
W = np.real(sqrtm(A / k)) * F                            # soft iron 역행렬 (대칭 선택)
cal = (raw3d - c) @ W.T
print("hard iron 참값 =", h_hard, " 추정 =", np.round(c, 2))
print(f"|m| raw  : mean {np.linalg.norm(raw3d,axis=1).mean():6.2f}  std {np.linalg.norm(raw3d,axis=1).std():5.2f} µT")
print(f"|m| 보정 : mean {np.linalg.norm(cal,axis=1).mean():6.2f}  std {np.linalg.norm(cal,axis=1).std():5.2f} µT")
def heading(m): return np.rad2deg(np.arctan2(m[:, 0], m[:, 1]))          # 평평할 때 북쪽 기준 방위
wrap = lambda d: (d + 180) % 360 - 180
true_h = np.rad2deg(yaws)                                # 정답 heading = 돌린 yaw
for name, m in [("raw", raw_flat), ("hard만", raw_flat - c), ("hard+soft", (raw_flat - c) @ W.T)]:
    err = wrap(heading(m) - true_h)
    print(f"heading 오차 {name:9s}: RMS {np.sqrt((err**2).mean()):6.2f}°, max {np.abs(err).max():6.2f}°")
print("W vs A_soft⁻¹ 차이 max =", np.round(np.abs(W - np.linalg.inv(A_soft)).max(), 4))
```

```text
hard iron 참값 = [ 25. -12.  40.]  추정 = [ 24.82 -11.88  39.71]
|m| raw  : mean  64.10  std 23.33 µT
|m| 보정 : mean  49.12  std  0.60 µT
heading 오차 raw      : RMS  73.52°, max 179.12°
heading 오차 hard만    : RMS   6.56°, max  13.05°
heading 오차 hard+soft: RMS   1.98°, max   3.82°
W vs A_soft⁻¹ 차이 max = 0.0094
```

출력에서 볼 것: (1) hard iron 중심을 0.3 µT 이내로 찾았다. (2) |m|의 표준편차가 23.3 µT → 0.60 µT로 줄었다 — 점들이 다시 구 위에 올라왔다는 뜻이고, 이 "보정 후 반지름 표준편차"가 현장에서 쓰는 **보정 품질 지표**다. (3) heading 오차는 raw에서 사실상 쓸 수 없는 수준(최대 179°), hard iron만 빼면 RMS 6.6°, soft iron까지 하면 2.0°(남은 것은 대부분 수평 성분 20 µT 대비 0.5 µT noise).

```svg
<svg viewBox="0 0 660 362" xmlns="http://www.w3.org/2000/svg">
  <line x1="10.0" y1="170.0" x2="290.0" y2="170.0" stroke="#888" stroke-width="0.7"/> <line x1="150.0" y1="30.0" x2="150.0" y2="310.0" stroke="#888" stroke-width="0.7"/> <text x="150.0" y="22.0" font-size="13" text-anchor="middle">보정 전 (raw, x–y 투영)</text> <line x1="20.0" y1="167.0" x2="20.0" y2="173.0" stroke="currentColor" stroke-width="1"/> <text x="20.0" y="186.0" font-size="12" text-anchor="middle">-100</text> <line x1="85.0" y1="167.0" x2="85.0" y2="173.0" stroke="currentColor" stroke-width="1"/> <text x="85.0" y="186.0" font-size="12" text-anchor="middle">-50</text>
  <line x1="215.0" y1="167.0" x2="215.0" y2="173.0" stroke="currentColor" stroke-width="1"/> <text x="215.0" y="186.0" font-size="12" text-anchor="middle">50</text> <line x1="280.0" y1="167.0" x2="280.0" y2="173.0" stroke="currentColor" stroke-width="1"/> <text x="280.0" y="186.0" font-size="12" text-anchor="middle">100</text> <circle cx="174.3" cy="136.2" r="1.6" fill="#888"/> <circle cx="132.6" cy="215.4" r="1.6" fill="#888"/> <circle cx="241.1" cy="150.0" r="1.6" fill="#888"/> <circle cx="250.8" cy="177.9" r="1.6" fill="#888"/> <circle cx="204.3" cy="213.4" r="1.6" fill="#888"/>
  <circle cx="139.6" cy="228.4" r="1.6" fill="#888"/> <circle cx="235.1" cy="167.9" r="1.6" fill="#888"/> <circle cx="245.4" cy="159.2" r="1.6" fill="#888"/> <circle cx="163.2" cy="221.6" r="1.6" fill="#888"/> <circle cx="226.9" cy="184.6" r="1.6" fill="#888"/> <circle cx="189.3" cy="241.7" r="1.6" fill="#888"/> <circle cx="181.1" cy="228.9" r="1.6" fill="#888"/> <circle cx="132.7" cy="197.9" r="1.6" fill="#888"/> <circle cx="229.9" cy="152.6" r="1.6" fill="#888"/> <circle cx="166.7" cy="132.3" r="1.6" fill="#888"/> <circle cx="157.2" cy="194.1" r="1.6" fill="#888"/>
  <circle cx="145.6" cy="143.9" r="1.6" fill="#888"/> <circle cx="136.0" cy="176.5" r="1.6" fill="#888"/> <circle cx="174.6" cy="226.9" r="1.6" fill="#888"/> <circle cx="185.3" cy="174.5" r="1.6" fill="#888"/> <circle cx="168.4" cy="218.8" r="1.6" fill="#888"/> <circle cx="190.6" cy="241.1" r="1.6" fill="#888"/> <circle cx="251.5" cy="166.6" r="1.6" fill="#888"/> <circle cx="148.6" cy="214.3" r="1.6" fill="#888"/> <circle cx="135.0" cy="198.9" r="1.6" fill="#888"/> <circle cx="201.5" cy="128.3" r="1.6" fill="#888"/> <circle cx="148.8" cy="190.4" r="1.6" fill="#888"/>
  <circle cx="140.5" cy="195.0" r="1.6" fill="#888"/> <circle cx="178.2" cy="145.6" r="1.6" fill="#888"/> <circle cx="196.9" cy="218.4" r="1.6" fill="#888"/> <circle cx="214.1" cy="205.0" r="1.6" fill="#888"/> <circle cx="132.4" cy="160.2" r="1.6" fill="#888"/> <circle cx="215.9" cy="173.1" r="1.6" fill="#888"/> <circle cx="223.2" cy="138.3" r="1.6" fill="#888"/> <circle cx="154.8" cy="224.4" r="1.6" fill="#888"/> <circle cx="143.4" cy="157.5" r="1.6" fill="#888"/> <circle cx="122.7" cy="222.8" r="1.6" fill="#888"/> <circle cx="150.8" cy="155.6" r="1.6" fill="#888"/>
  <circle cx="184.4" cy="168.4" r="1.6" fill="#888"/> <circle cx="182.3" cy="199.2" r="1.6" fill="#888"/> <circle cx="159.1" cy="241.8" r="1.6" fill="#888"/> <circle cx="117.0" cy="194.7" r="1.6" fill="#888"/> <circle cx="222.8" cy="166.7" r="1.6" fill="#888"/> <circle cx="214.3" cy="131.3" r="1.6" fill="#888"/> <circle cx="220.1" cy="181.1" r="1.6" fill="#888"/> <circle cx="175.6" cy="130.3" r="1.6" fill="#888"/> <circle cx="113.8" cy="201.0" r="1.6" fill="#888"/> <circle cx="171.8" cy="138.6" r="1.6" fill="#888"/> <circle cx="170.5" cy="189.6" r="1.6" fill="#888"/>
  <circle cx="190.2" cy="167.3" r="1.6" fill="#888"/> <circle cx="125.9" cy="222.1" r="1.6" fill="#888"/> <circle cx="230.0" cy="221.5" r="1.6" fill="#888"/> <circle cx="123.0" cy="165.1" r="1.6" fill="#888"/> <circle cx="127.9" cy="191.3" r="1.6" fill="#888"/> <circle cx="179.4" cy="197.0" r="1.6" fill="#888"/> <circle cx="120.8" cy="218.4" r="1.6" fill="#888"/> <circle cx="150.8" cy="191.6" r="1.6" fill="#888"/> <circle cx="230.0" cy="197.6" r="1.6" fill="#888"/> <circle cx="241.7" cy="148.6" r="1.6" fill="#888"/> <circle cx="201.3" cy="141.4" r="1.6" fill="#888"/>
  <circle cx="235.5" cy="174.4" r="1.6" fill="#888"/> <circle cx="202.5" cy="232.7" r="1.6" fill="#888"/> <circle cx="189.6" cy="144.8" r="1.6" fill="#888"/> <circle cx="237.5" cy="157.2" r="1.6" fill="#888"/> <circle cx="211.7" cy="132.9" r="1.6" fill="#888"/> <circle cx="170.5" cy="194.8" r="1.6" fill="#888"/> <circle cx="123.8" cy="162.5" r="1.6" fill="#888"/> <circle cx="137.8" cy="235.9" r="1.6" fill="#888"/> <circle cx="134.6" cy="221.7" r="1.6" fill="#888"/> <circle cx="164.0" cy="228.7" r="1.6" fill="#888"/> <circle cx="218.8" cy="204.6" r="1.6" fill="#888"/>
  <circle cx="190.8" cy="225.7" r="1.6" fill="#888"/> <circle cx="170.5" cy="238.5" r="1.6" fill="#888"/> <circle cx="185.5" cy="202.4" r="1.6" fill="#888"/> <circle cx="183.6" cy="130.5" r="1.6" fill="#888"/> <circle cx="186.1" cy="235.9" r="1.6" fill="#888"/> <circle cx="159.5" cy="134.8" r="1.6" fill="#888"/> <circle cx="228.6" cy="210.6" r="1.6" fill="#888"/> <circle cx="239.0" cy="152.6" r="1.6" fill="#888"/> <circle cx="140.0" cy="149.3" r="1.6" fill="#888"/> <circle cx="193.5" cy="128.4" r="1.6" fill="#888"/> <circle cx="115.5" cy="186.0" r="1.6" fill="#888"/>
  <circle cx="157.0" cy="135.4" r="1.6" fill="#888"/> <circle cx="218.3" cy="216.9" r="1.6" fill="#888"/> <circle cx="222.3" cy="172.3" r="1.6" fill="#888"/> <circle cx="189.0" cy="215.8" r="1.6" fill="#888"/> <circle cx="145.2" cy="147.2" r="1.6" fill="#888"/> <circle cx="114.8" cy="199.9" r="1.6" fill="#888"/> <circle cx="116.3" cy="215.1" r="1.6" fill="#888"/> <circle cx="153.6" cy="166.6" r="1.6" fill="#888"/> <circle cx="223.3" cy="152.4" r="1.6" fill="#888"/> <circle cx="229.7" cy="209.2" r="1.6" fill="#888"/> <circle cx="253.3" cy="166.5" r="1.6" fill="#888"/>
  <circle cx="125.3" cy="159.6" r="1.6" fill="#888"/> <circle cx="118.3" cy="178.2" r="1.6" fill="#888"/> <circle cx="127.1" cy="197.0" r="1.6" fill="#888"/> <circle cx="140.1" cy="222.5" r="1.6" fill="#888"/> <circle cx="216.1" cy="207.3" r="1.6" fill="#888"/> <circle cx="132.9" cy="154.9" r="1.6" fill="#888"/> <circle cx="132.9" cy="185.3" r="1.6" fill="#888"/> <circle cx="241.6" cy="180.9" r="1.6" fill="#888"/> <circle cx="243.9" cy="149.8" r="1.6" fill="#888"/> <circle cx="186.5" cy="215.3" r="1.6" fill="#888"/> <circle cx="162.6" cy="199.3" r="1.6" fill="#888"/>
  <circle cx="161.4" cy="136.1" r="1.6" fill="#888"/> <circle cx="224.2" cy="196.5" r="1.6" fill="#888"/> <circle cx="139.0" cy="148.5" r="1.6" fill="#888"/> <circle cx="112.8" cy="197.6" r="1.6" fill="#888"/> <circle cx="191.7" cy="183.3" r="1.6" fill="#888"/> <circle cx="131.7" cy="231.7" r="1.6" fill="#888"/> <circle cx="173.1" cy="136.4" r="1.6" fill="#888"/> <circle cx="127.8" cy="211.4" r="1.6" fill="#888"/> <circle cx="168.6" cy="233.4" r="1.6" fill="#888"/> <circle cx="209.7" cy="159.9" r="1.6" fill="#888"/> <circle cx="220.5" cy="186.5" r="1.6" fill="#888"/>
  <circle cx="225.7" cy="177.7" r="1.6" fill="#888"/> <circle cx="230.9" cy="146.2" r="1.6" fill="#888"/> <circle cx="122.8" cy="182.9" r="1.6" fill="#888"/> <circle cx="167.7" cy="219.1" r="1.6" fill="#888"/> <circle cx="203.6" cy="226.3" r="1.6" fill="#888"/> <circle cx="135.5" cy="176.1" r="1.6" fill="#888"/> <circle cx="229.0" cy="208.7" r="1.6" fill="#888"/> <circle cx="224.6" cy="199.2" r="1.6" fill="#888"/> <circle cx="121.0" cy="219.6" r="1.6" fill="#888"/> <circle cx="224.4" cy="188.9" r="1.6" fill="#888"/> <circle cx="184.9" cy="158.5" r="1.6" fill="#888"/>
  <circle cx="139.0" cy="181.1" r="1.6" fill="#888"/> <circle cx="166.7" cy="132.8" r="1.6" fill="#888"/> <circle cx="185.7" cy="218.0" r="1.6" fill="#888"/> <circle cx="137.8" cy="171.7" r="1.6" fill="#888"/> <circle cx="157.2" cy="139.6" r="1.6" fill="#888"/> <circle cx="118.9" cy="218.6" r="1.6" fill="#888"/> <circle cx="144.9" cy="153.4" r="1.6" fill="#888"/> <circle cx="134.7" cy="234.8" r="1.6" fill="#888"/> <circle cx="174.5" cy="130.8" r="1.6" fill="#888"/> <circle cx="171.0" cy="230.2" r="1.6" fill="#888"/> <circle cx="210.5" cy="193.3" r="1.6" fill="#888"/>
  <circle cx="180.3" cy="150.1" r="1.6" fill="#888"/> <circle cx="241.3" cy="146.3" r="1.6" fill="#888"/> <circle cx="206.8" cy="157.5" r="1.6" fill="#888"/> <circle cx="195.5" cy="197.7" r="1.6" fill="#888"/> <circle cx="179.9" cy="220.0" r="1.6" fill="#888"/> <circle cx="130.9" cy="190.8" r="1.6" fill="#888"/> <circle cx="205.7" cy="220.2" r="1.6" fill="#888"/> <circle cx="116.0" cy="176.5" r="1.6" fill="#888"/> <circle cx="123.1" cy="222.0" r="1.6" fill="#888"/> <circle cx="180.7" cy="159.8" r="1.6" fill="#888"/> <circle cx="122.1" cy="193.6" r="1.6" fill="#888"/>
  <circle cx="235.4" cy="208.0" r="1.6" fill="#888"/> <circle cx="187.0" cy="233.9" r="1.6" fill="#888"/> <circle cx="237.2" cy="211.4" r="1.6" fill="#888"/> <circle cx="186.9" cy="196.3" r="1.6" fill="#888"/> <circle cx="181.2" cy="148.5" r="1.6" fill="#888"/> <circle cx="200.4" cy="129.9" r="1.6" fill="#888"/> <circle cx="170.5" cy="134.1" r="1.6" fill="#888"/> <circle cx="218.9" cy="219.4" r="1.6" fill="#888"/> <circle cx="227.9" cy="153.1" r="1.6" fill="#888"/> <circle cx="221.7" cy="140.3" r="1.6" fill="#888"/> <circle cx="199.1" cy="153.0" r="1.6" fill="#888"/>
  <circle cx="136.1" cy="153.5" r="1.6" fill="#888"/> <circle cx="253.7" cy="172.4" r="1.6" fill="#888"/> <circle cx="186.8" cy="172.6" r="1.6" fill="#888"/> <circle cx="237.7" cy="197.8" r="1.6" fill="#888"/> <circle cx="143.5" cy="151.2" r="1.6" fill="#888"/> <circle cx="215.7" cy="219.7" r="1.6" fill="#888"/> <circle cx="136.3" cy="235.1" r="1.6" fill="#888"/> <circle cx="114.9" cy="177.0" r="1.6" fill="#888"/> <circle cx="122.1" cy="219.0" r="1.6" fill="#888"/> <circle cx="133.1" cy="213.3" r="1.6" fill="#888"/> <circle cx="141.9" cy="186.5" r="1.6" fill="#888"/>
  <circle cx="136.3" cy="183.3" r="1.6" fill="#888"/> <circle cx="224.8" cy="151.7" r="1.6" fill="#888"/> <circle cx="238.4" cy="188.5" r="1.6" fill="#888"/> <circle cx="118.3" cy="172.2" r="1.6" fill="#888"/> <circle cx="192.1" cy="132.7" r="1.6" fill="#888"/> <circle cx="187.2" cy="230.1" r="1.6" fill="#888"/> <circle cx="209.4" cy="235.6" r="1.6" fill="#888"/> <circle cx="209.7" cy="181.0" r="1.6" fill="#888"/> <circle cx="238.5" cy="156.4" r="1.6" fill="#888"/> <circle cx="131.8" cy="202.3" r="1.6" fill="#888"/> <circle cx="200.4" cy="129.2" r="1.6" fill="#888"/>
  <circle cx="162.9" cy="137.0" r="1.6" fill="#888"/> <circle cx="148.9" cy="239.6" r="1.6" fill="#888"/> <circle cx="142.0" cy="186.7" r="1.6" fill="#888"/> <circle cx="229.7" cy="222.3" r="1.6" fill="#888"/> <circle cx="200.4" cy="237.9" r="1.6" fill="#888"/> <circle cx="143.9" cy="221.7" r="1.6" fill="#888"/> <circle cx="245.8" cy="187.3" r="1.6" fill="#888"/> <circle cx="211.2" cy="227.8" r="1.6" fill="#888"/> <circle cx="251.2" cy="167.7" r="1.6" fill="#888"/> <circle cx="111.9" cy="202.0" r="1.6" fill="#888"/> <circle cx="118.6" cy="211.7" r="1.6" fill="#888"/>
  <circle cx="241.3" cy="160.4" r="1.6" fill="#888"/> <circle cx="233.3" cy="142.4" r="1.6" fill="#888"/> <circle cx="239.3" cy="207.2" r="1.6" fill="#888"/> <circle cx="238.2" cy="154.4" r="1.6" fill="#888"/> <circle cx="115.9" cy="174.8" r="1.6" fill="#888"/> <circle cx="171.0" cy="175.9" r="1.6" fill="#888"/> <circle cx="151.1" cy="157.2" r="1.6" fill="#888"/> <circle cx="190.6" cy="153.9" r="1.6" fill="#888"/>
  <polyline fill="none" stroke="#e08a3c" stroke-width="2" points="182.5,158.7 187.6,159.0 193.5,159.9 198.0,160.0 202.2,162.8 204.8,166.7 207.2,169.3 208.0,173.0 209.7,175.9 210.4,181.1 209.7,185.0 207.5,188.9 203.8,193.1 201.9,197.2 198.4,200.2 194.1,203.1 189.0,204.1 184.4,206.0 180.5,207.1 174.3,206.6 169.6,205.0 165.9,203.3 162.1,201.8 159.2,199.3 155.5,197.1 153.2,193.2 153.3,189.4 151.6,185.2 154.2,180.2 156.0,176.2 157.0,173.9 161.3,169.1 165.0,165.8 168.3,162.8 173.4,162.4 177.4,159.1 182.5,158.7"/>
  <circle cx="182.5" cy="158.7" r="2.5" fill="#e08a3c"/> <circle cx="187.6" cy="159.0" r="2.5" fill="#e08a3c"/> <circle cx="193.5" cy="159.9" r="2.5" fill="#e08a3c"/> <circle cx="198.0" cy="160.0" r="2.5" fill="#e08a3c"/> <circle cx="202.2" cy="162.8" r="2.5" fill="#e08a3c"/> <circle cx="204.8" cy="166.7" r="2.5" fill="#e08a3c"/> <circle cx="207.2" cy="169.3" r="2.5" fill="#e08a3c"/> <circle cx="208.0" cy="173.0" r="2.5" fill="#e08a3c"/> <circle cx="209.7" cy="175.9" r="2.5" fill="#e08a3c"/> <circle cx="210.4" cy="181.1" r="2.5" fill="#e08a3c"/>
  <circle cx="209.7" cy="185.0" r="2.5" fill="#e08a3c"/> <circle cx="207.5" cy="188.9" r="2.5" fill="#e08a3c"/> <circle cx="203.8" cy="193.1" r="2.5" fill="#e08a3c"/> <circle cx="201.9" cy="197.2" r="2.5" fill="#e08a3c"/> <circle cx="198.4" cy="200.2" r="2.5" fill="#e08a3c"/> <circle cx="194.1" cy="203.1" r="2.5" fill="#e08a3c"/> <circle cx="189.0" cy="204.1" r="2.5" fill="#e08a3c"/> <circle cx="184.4" cy="206.0" r="2.5" fill="#e08a3c"/> <circle cx="180.5" cy="207.1" r="2.5" fill="#e08a3c"/> <circle cx="174.3" cy="206.6" r="2.5" fill="#e08a3c"/>
  <circle cx="169.6" cy="205.0" r="2.5" fill="#e08a3c"/> <circle cx="165.9" cy="203.3" r="2.5" fill="#e08a3c"/> <circle cx="162.1" cy="201.8" r="2.5" fill="#e08a3c"/> <circle cx="159.2" cy="199.3" r="2.5" fill="#e08a3c"/> <circle cx="155.5" cy="197.1" r="2.5" fill="#e08a3c"/> <circle cx="153.2" cy="193.2" r="2.5" fill="#e08a3c"/> <circle cx="153.3" cy="189.4" r="2.5" fill="#e08a3c"/> <circle cx="151.6" cy="185.2" r="2.5" fill="#e08a3c"/> <circle cx="154.2" cy="180.2" r="2.5" fill="#e08a3c"/> <circle cx="156.0" cy="176.2" r="2.5" fill="#e08a3c"/>
  <circle cx="157.0" cy="173.9" r="2.5" fill="#e08a3c"/> <circle cx="161.3" cy="169.1" r="2.5" fill="#e08a3c"/> <circle cx="165.0" cy="165.8" r="2.5" fill="#e08a3c"/> <circle cx="168.3" cy="162.8" r="2.5" fill="#e08a3c"/> <circle cx="173.4" cy="162.4" r="2.5" fill="#e08a3c"/> <circle cx="177.4" cy="159.1" r="2.5" fill="#e08a3c"/> <circle cx="150" cy="170" r="64.0" fill="none" stroke="#4a7bd0" stroke-width="1" stroke-dasharray="2 3"/> <line x1="340.0" y1="170.0" x2="620.0" y2="170.0" stroke="#888" stroke-width="0.7"/>
  <line x1="480.0" y1="30.0" x2="480.0" y2="310.0" stroke="#888" stroke-width="0.7"/> <text x="480.0" y="22.0" font-size="13" text-anchor="middle">보정 후 (hard+soft iron)</text> <line x1="350.0" y1="167.0" x2="350.0" y2="173.0" stroke="currentColor" stroke-width="1"/> <text x="350.0" y="186.0" font-size="12" text-anchor="middle">-100</text> <line x1="415.0" y1="167.0" x2="415.0" y2="173.0" stroke="currentColor" stroke-width="1"/> <text x="415.0" y="186.0" font-size="12" text-anchor="middle">-50</text> <line x1="545.0" y1="167.0" x2="545.0" y2="173.0" stroke="currentColor" stroke-width="1"/>
  <text x="545.0" y="186.0" font-size="12" text-anchor="middle">50</text> <line x1="610.0" y1="167.0" x2="610.0" y2="173.0" stroke="currentColor" stroke-width="1"/> <text x="610.0" y="186.0" font-size="12" text-anchor="middle">100</text> <circle cx="468.1" cy="112.6" r="1.6" fill="#888"/> <circle cx="438.0" cy="201.9" r="1.6" fill="#888"/> <circle cx="531.6" cy="135.9" r="1.6" fill="#888"/> <circle cx="541.5" cy="165.7" r="1.6" fill="#888"/> <circle cx="503.7" cy="205.9" r="1.6" fill="#888"/> <circle cx="445.1" cy="216.4" r="1.6" fill="#888"/> <circle cx="525.7" cy="151.9" r="1.6" fill="#888"/>
  <circle cx="535.2" cy="144.3" r="1.6" fill="#888"/> <circle cx="466.8" cy="211.9" r="1.6" fill="#888"/> <circle cx="519.2" cy="169.3" r="1.6" fill="#888"/> <circle cx="490.8" cy="233.6" r="1.6" fill="#888"/> <circle cx="480.9" cy="215.6" r="1.6" fill="#888"/> <circle cx="437.0" cy="182.9" r="1.6" fill="#888"/> <circle cx="519.9" cy="134.6" r="1.6" fill="#888"/> <circle cx="462.0" cy="110.0" r="1.6" fill="#888"/> <circle cx="459.6" cy="181.4" r="1.6" fill="#888"/> <circle cx="443.8" cy="121.7" r="1.6" fill="#888"/> <circle cx="438.3" cy="159.3" r="1.6" fill="#888"/>
  <circle cx="474.8" cy="212.8" r="1.6" fill="#888"/> <circle cx="480.1" cy="154.1" r="1.6" fill="#888"/> <circle cx="471.4" cy="209.4" r="1.6" fill="#888"/> <circle cx="492.1" cy="233.3" r="1.6" fill="#888"/> <circle cx="541.5" cy="153.4" r="1.6" fill="#888"/> <circle cx="449.9" cy="196.6" r="1.6" fill="#888"/> <circle cx="439.3" cy="184.3" r="1.6" fill="#888"/> <circle cx="493.0" cy="107.2" r="1.6" fill="#888"/> <circle cx="451.4" cy="176.4" r="1.6" fill="#888"/> <circle cx="441.0" cy="174.2" r="1.6" fill="#888"/> <circle cx="474.6" cy="128.1" r="1.6" fill="#888"/>
  <circle cx="497.3" cy="211.0" r="1.6" fill="#888"/> <circle cx="509.0" cy="191.0" r="1.6" fill="#888"/> <circle cx="433.3" cy="139.6" r="1.6" fill="#888"/> <circle cx="511.5" cy="162.2" r="1.6" fill="#888"/> <circle cx="514.5" cy="121.7" r="1.6" fill="#888"/> <circle cx="456.6" cy="208.8" r="1.6" fill="#888"/> <circle cx="443.5" cy="138.2" r="1.6" fill="#888"/> <circle cx="428.7" cy="207.6" r="1.6" fill="#888"/> <circle cx="450.3" cy="136.8" r="1.6" fill="#888"/> <circle cx="478.9" cy="147.3" r="1.6" fill="#888"/> <circle cx="482.9" cy="189.3" r="1.6" fill="#888"/>
  <circle cx="462.8" cy="230.6" r="1.6" fill="#888"/> <circle cx="421.9" cy="176.9" r="1.6" fill="#888"/> <circle cx="517.1" cy="155.3" r="1.6" fill="#888"/> <circle cx="505.4" cy="112.4" r="1.6" fill="#888"/> <circle cx="512.6" cy="164.7" r="1.6" fill="#888"/> <circle cx="469.4" cy="107.3" r="1.6" fill="#888"/> <circle cx="418.3" cy="181.4" r="1.6" fill="#888"/> <circle cx="467.8" cy="118.8" r="1.6" fill="#888"/> <circle cx="467.6" cy="169.8" r="1.6" fill="#888"/> <circle cx="484.1" cy="146.6" r="1.6" fill="#888"/> <circle cx="431.7" cy="207.6" r="1.6" fill="#888"/>
  <circle cx="526.3" cy="213.8" r="1.6" fill="#888"/> <circle cx="423.9" cy="141.9" r="1.6" fill="#888"/> <circle cx="429.5" cy="170.0" r="1.6" fill="#888"/> <circle cx="476.3" cy="178.7" r="1.6" fill="#888"/> <circle cx="426.9" cy="203.2" r="1.6" fill="#888"/> <circle cx="450.0" cy="170.8" r="1.6" fill="#888"/> <circle cx="525.8" cy="189.8" r="1.6" fill="#888"/> <circle cx="532.1" cy="134.5" r="1.6" fill="#888"/> <circle cx="492.7" cy="119.7" r="1.6" fill="#888"/> <circle cx="526.6" cy="159.1" r="1.6" fill="#888"/> <circle cx="502.8" cy="225.8" r="1.6" fill="#888"/>
  <circle cx="484.9" cy="127.9" r="1.6" fill="#888"/> <circle cx="529.3" cy="144.7" r="1.6" fill="#888"/> <circle cx="502.2" cy="112.3" r="1.6" fill="#888"/> <circle cx="471.8" cy="183.3" r="1.6" fill="#888"/> <circle cx="425.0" cy="140.5" r="1.6" fill="#888"/> <circle cx="443.0" cy="222.5" r="1.6" fill="#888"/> <circle cx="440.2" cy="209.0" r="1.6" fill="#888"/> <circle cx="465.3" cy="214.3" r="1.6" fill="#888"/> <circle cx="513.4" cy="191.2" r="1.6" fill="#888"/> <circle cx="489.5" cy="212.9" r="1.6" fill="#888"/> <circle cx="474.0" cy="229.9" r="1.6" fill="#888"/>
  <circle cx="482.4" cy="185.4" r="1.6" fill="#888"/> <circle cx="476.6" cy="107.8" r="1.6" fill="#888"/> <circle cx="486.5" cy="225.0" r="1.6" fill="#888"/> <circle cx="455.3" cy="111.7" r="1.6" fill="#888"/> <circle cx="525.2" cy="203.7" r="1.6" fill="#888"/> <circle cx="528.6" cy="136.1" r="1.6" fill="#888"/> <circle cx="439.2" cy="127.6" r="1.6" fill="#888"/> <circle cx="486.3" cy="107.8" r="1.6" fill="#888"/> <circle cx="418.5" cy="164.4" r="1.6" fill="#888"/> <circle cx="453.4" cy="112.7" r="1.6" fill="#888"/> <circle cx="516.4" cy="210.1" r="1.6" fill="#888"/>
  <circle cx="514.0" cy="155.1" r="1.6" fill="#888"/> <circle cx="486.9" cy="201.1" r="1.6" fill="#888"/> <circle cx="442.3" cy="122.7" r="1.6" fill="#888"/> <circle cx="419.0" cy="179.9" r="1.6" fill="#888"/> <circle cx="422.0" cy="198.1" r="1.6" fill="#888"/> <circle cx="450.8" cy="143.4" r="1.6" fill="#888"/> <circle cx="516.2" cy="138.9" r="1.6" fill="#888"/> <circle cx="526.2" cy="202.1" r="1.6" fill="#888"/> <circle cx="543.4" cy="154.3" r="1.6" fill="#888"/> <circle cx="426.1" cy="137.1" r="1.6" fill="#888"/> <circle cx="421.6" cy="158.3" r="1.6" fill="#888"/>
  <circle cx="429.3" cy="176.3" r="1.6" fill="#888"/> <circle cx="443.1" cy="205.7" r="1.6" fill="#888"/> <circle cx="514.0" cy="200.0" r="1.6" fill="#888"/> <circle cx="433.1" cy="133.2" r="1.6" fill="#888"/> <circle cx="433.5" cy="163.2" r="1.6" fill="#888"/> <circle cx="534.9" cy="171.6" r="1.6" fill="#888"/> <circle cx="533.7" cy="135.0" r="1.6" fill="#888"/> <circle cx="487.8" cy="207.0" r="1.6" fill="#888"/> <circle cx="464.9" cy="187.7" r="1.6" fill="#888"/> <circle cx="456.5" cy="111.9" r="1.6" fill="#888"/> <circle cx="517.6" cy="182.5" r="1.6" fill="#888"/>
  <circle cx="438.2" cy="126.6" r="1.6" fill="#888"/> <circle cx="417.2" cy="177.8" r="1.6" fill="#888"/> <circle cx="486.6" cy="164.3" r="1.6" fill="#888"/> <circle cx="437.5" cy="218.1" r="1.6" fill="#888"/> <circle cx="468.7" cy="116.2" r="1.6" fill="#888"/> <circle cx="433.2" cy="196.8" r="1.6" fill="#888"/> <circle cx="472.3" cy="224.8" r="1.6" fill="#888"/> <circle cx="501.5" cy="140.3" r="1.6" fill="#888"/> <circle cx="513.4" cy="170.8" r="1.6" fill="#888"/> <circle cx="517.6" cy="161.5" r="1.6" fill="#888"/> <circle cx="520.6" cy="128.1" r="1.6" fill="#888"/>
  <circle cx="426.3" cy="164.5" r="1.6" fill="#888"/> <circle cx="467.7" cy="203.2" r="1.6" fill="#888"/> <circle cx="503.7" cy="219.5" r="1.6" fill="#888"/> <circle cx="435.2" cy="153.1" r="1.6" fill="#888"/> <circle cx="523.4" cy="197.3" r="1.6" fill="#888"/> <circle cx="521.2" cy="191.5" r="1.6" fill="#888"/> <circle cx="426.2" cy="202.6" r="1.6" fill="#888"/> <circle cx="520.2" cy="180.2" r="1.6" fill="#888"/> <circle cx="482.0" cy="143.5" r="1.6" fill="#888"/> <circle cx="438.6" cy="158.8" r="1.6" fill="#888"/> <circle cx="461.5" cy="109.4" r="1.6" fill="#888"/>
  <circle cx="484.0" cy="203.4" r="1.6" fill="#888"/> <circle cx="437.0" cy="148.6" r="1.6" fill="#888"/> <circle cx="452.9" cy="115.3" r="1.6" fill="#888"/> <circle cx="424.8" cy="202.4" r="1.6" fill="#888"/> <circle cx="444.5" cy="133.4" r="1.6" fill="#888"/> <circle cx="440.3" cy="221.5" r="1.6" fill="#888"/> <circle cx="468.4" cy="107.6" r="1.6" fill="#888"/> <circle cx="474.3" cy="221.7" r="1.6" fill="#888"/> <circle cx="508.1" cy="184.5" r="1.6" fill="#888"/> <circle cx="474.0" cy="127.4" r="1.6" fill="#888"/> <circle cx="531.1" cy="130.9" r="1.6" fill="#888"/>
  <circle cx="498.7" cy="137.4" r="1.6" fill="#888"/> <circle cx="491.2" cy="180.8" r="1.6" fill="#888"/> <circle cx="482.0" cy="211.6" r="1.6" fill="#888"/> <circle cx="432.2" cy="169.4" r="1.6" fill="#888"/> <circle cx="505.4" cy="213.2" r="1.6" fill="#888"/> <circle cx="418.8" cy="155.1" r="1.6" fill="#888"/> <circle cx="428.2" cy="205.2" r="1.6" fill="#888"/> <circle cx="478.4" cy="144.9" r="1.6" fill="#888"/> <circle cx="424.7" cy="172.5" r="1.6" fill="#888"/> <circle cx="531.0" cy="200.8" r="1.6" fill="#888"/> <circle cx="489.1" cy="226.6" r="1.6" fill="#888"/>
  <circle cx="531.6" cy="202.0" r="1.6" fill="#888"/> <circle cx="483.1" cy="178.5" r="1.6" fill="#888"/> <circle cx="474.8" cy="125.7" r="1.6" fill="#888"/> <circle cx="491.9" cy="108.5" r="1.6" fill="#888"/> <circle cx="464.6" cy="110.3" r="1.6" fill="#888"/> <circle cx="517.1" cy="212.9" r="1.6" fill="#888"/> <circle cx="518.1" cy="135.1" r="1.6" fill="#888"/> <circle cx="511.8" cy="120.9" r="1.6" fill="#888"/> <circle cx="494.5" cy="138.3" r="1.6" fill="#888"/> <circle cx="436.2" cy="132.3" r="1.6" fill="#888"/> <circle cx="544.3" cy="161.0" r="1.6" fill="#888"/>
  <circle cx="485.0" cy="159.9" r="1.6" fill="#888"/> <circle cx="530.6" cy="185.9" r="1.6" fill="#888"/> <circle cx="442.9" cy="130.7" r="1.6" fill="#888"/> <circle cx="512.0" cy="208.4" r="1.6" fill="#888"/> <circle cx="441.7" cy="221.9" r="1.6" fill="#888"/> <circle cx="418.0" cy="155.9" r="1.6" fill="#888"/> <circle cx="427.0" cy="201.8" r="1.6" fill="#888"/> <circle cx="436.1" cy="195.0" r="1.6" fill="#888"/> <circle cx="444.8" cy="171.3" r="1.6" fill="#888"/> <circle cx="436.3" cy="161.0" r="1.6" fill="#888"/> <circle cx="517.5" cy="138.1" r="1.6" fill="#888"/>
  <circle cx="532.6" cy="179.8" r="1.6" fill="#888"/> <circle cx="420.5" cy="150.1" r="1.6" fill="#888"/> <circle cx="484.2" cy="110.1" r="1.6" fill="#888"/> <circle cx="489.0" cy="222.7" r="1.6" fill="#888"/> <circle cx="508.6" cy="228.0" r="1.6" fill="#888"/> <circle cx="502.9" cy="163.5" r="1.6" fill="#888"/> <circle cx="528.2" cy="140.0" r="1.6" fill="#888"/> <circle cx="433.8" cy="182.3" r="1.6" fill="#888"/> <circle cx="492.6" cy="109.2" r="1.6" fill="#888"/> <circle cx="458.0" cy="113.1" r="1.6" fill="#888"/> <circle cx="453.5" cy="227.5" r="1.6" fill="#888"/>
  <circle cx="441.7" cy="165.0" r="1.6" fill="#888"/> <circle cx="526.4" cy="215.1" r="1.6" fill="#888"/> <circle cx="500.5" cy="229.9" r="1.6" fill="#888"/> <circle cx="446.3" cy="204.9" r="1.6" fill="#888"/> <circle cx="538.9" cy="178.3" r="1.6" fill="#888"/> <circle cx="510.4" cy="221.1" r="1.6" fill="#888"/> <circle cx="541.4" cy="155.1" r="1.6" fill="#888"/> <circle cx="416.9" cy="182.9" r="1.6" fill="#888"/> <circle cx="424.2" cy="195.3" r="1.6" fill="#888"/> <circle cx="532.9" cy="148.4" r="1.6" fill="#888"/> <circle cx="523.9" cy="126.9" r="1.6" fill="#888"/>
  <circle cx="534.3" cy="199.8" r="1.6" fill="#888"/> <circle cx="527.9" cy="137.8" r="1.6" fill="#888"/> <circle cx="418.6" cy="153.1" r="1.6" fill="#888"/> <circle cx="470.8" cy="162.3" r="1.6" fill="#888"/> <circle cx="448.1" cy="133.5" r="1.6" fill="#888"/> <circle cx="483.6" cy="132.1" r="1.6" fill="#888"/>
  <polyline fill="none" stroke="#e08a3c" stroke-width="2" points="479.9,143.8 484.6,144.4 490.0,145.9 494.2,146.4 498.2,149.8 500.9,154.5 503.3,157.4 504.3,161.7 506.1,165.1 507.2,170.9 506.8,175.2 505.1,179.5 502.1,183.9 500.7,188.3 497.7,191.5 494.0,194.4 489.4,195.1 485.3,196.8 481.9,197.7 476.1,196.6 471.7,194.6 468.2,192.4 464.6,190.4 461.8,187.4 458.2,184.6 455.7,179.9 455.5,175.8 453.7,171.0 455.7,165.6 457.0,161.2 457.8,158.8 461.3,153.6 464.4,150.2 467.3,147.2 471.8,147.1 475.3,143.8 479.9,143.8"/>
  <circle cx="479.9" cy="143.8" r="2.5" fill="#e08a3c"/> <circle cx="484.6" cy="144.4" r="2.5" fill="#e08a3c"/> <circle cx="490.0" cy="145.9" r="2.5" fill="#e08a3c"/> <circle cx="494.2" cy="146.4" r="2.5" fill="#e08a3c"/> <circle cx="498.2" cy="149.8" r="2.5" fill="#e08a3c"/> <circle cx="500.9" cy="154.5" r="2.5" fill="#e08a3c"/> <circle cx="503.3" cy="157.4" r="2.5" fill="#e08a3c"/> <circle cx="504.3" cy="161.7" r="2.5" fill="#e08a3c"/> <circle cx="506.1" cy="165.1" r="2.5" fill="#e08a3c"/> <circle cx="507.2" cy="170.9" r="2.5" fill="#e08a3c"/>
  <circle cx="506.8" cy="175.2" r="2.5" fill="#e08a3c"/> <circle cx="505.1" cy="179.5" r="2.5" fill="#e08a3c"/> <circle cx="502.1" cy="183.9" r="2.5" fill="#e08a3c"/> <circle cx="500.7" cy="188.3" r="2.5" fill="#e08a3c"/> <circle cx="497.7" cy="191.5" r="2.5" fill="#e08a3c"/> <circle cx="494.0" cy="194.4" r="2.5" fill="#e08a3c"/> <circle cx="489.4" cy="195.1" r="2.5" fill="#e08a3c"/> <circle cx="485.3" cy="196.8" r="2.5" fill="#e08a3c"/> <circle cx="481.9" cy="197.7" r="2.5" fill="#e08a3c"/> <circle cx="476.1" cy="196.6" r="2.5" fill="#e08a3c"/>
  <circle cx="471.7" cy="194.6" r="2.5" fill="#e08a3c"/> <circle cx="468.2" cy="192.4" r="2.5" fill="#e08a3c"/> <circle cx="464.6" cy="190.4" r="2.5" fill="#e08a3c"/> <circle cx="461.8" cy="187.4" r="2.5" fill="#e08a3c"/> <circle cx="458.2" cy="184.6" r="2.5" fill="#e08a3c"/> <circle cx="455.7" cy="179.9" r="2.5" fill="#e08a3c"/> <circle cx="455.5" cy="175.8" r="2.5" fill="#e08a3c"/> <circle cx="453.7" cy="171.0" r="2.5" fill="#e08a3c"/> <circle cx="455.7" cy="165.6" r="2.5" fill="#e08a3c"/> <circle cx="457.0" cy="161.2" r="2.5" fill="#e08a3c"/>
  <circle cx="457.8" cy="158.8" r="2.5" fill="#e08a3c"/> <circle cx="461.3" cy="153.6" r="2.5" fill="#e08a3c"/> <circle cx="464.4" cy="150.2" r="2.5" fill="#e08a3c"/> <circle cx="467.3" cy="147.2" r="2.5" fill="#e08a3c"/> <circle cx="471.8" cy="147.1" r="2.5" fill="#e08a3c"/> <circle cx="475.3" cy="143.8" r="2.5" fill="#e08a3c"/> <circle cx="480" cy="170" r="26.0" fill="none" stroke="#4a7bd0" stroke-width="1.5" stroke-dasharray="4 3"/> <circle cx="480" cy="170" r="64.0" fill="none" stroke="#4a7bd0" stroke-width="1" stroke-dasharray="2 3"/>
  <text x="20.0" y="334.0" font-size="12" text-anchor="start">회색: 손으로 이리저리 돌린 400점의 x–y 투영 · 주황: 책상 위에서 한 바퀴(36점)</text> <text x="20.0" y="352.0" font-size="12" text-anchor="start">파랑 점선: 반지름 20 µT(수평 성분) · 49 µT(전체 세기)</text> <text x="170.0" y="312.0" font-size="12" text-anchor="middle">mx [µT] →</text> <text x="500.0" y="312.0" font-size="12" text-anchor="middle">mx [µT] →</text>
</svg>
```

그림 3 — 보정 전(왼쪽)과 후(오른쪽)의 x–y 투영. 보정 전 책상 위 한 바퀴(주황)는 중심이 (25, −12)로 밀린 기울어진 타원이고, 보정 후에는 원점 중심, 반지름 20 µT(수평 성분)의 원이 된다. 회색 점(3D로 돌린 점의 투영)은 반지름 49 µT 원 안을 채운다.

### 2.4 임베디드 연결과 함정

- **언제 보정하나**: hard iron은 기기 조립 상태에서 정해지므로 공장에서 한 번 + 사용 중 백그라운드 보정(사용자가 손목을 돌릴 때 점이 충분히 퍼지면 재피팅)을 같이 쓴다. 스마트폰 나침반 앱이 "8자 그리기"를 시키는 이유가 점을 구 전체에 퍼뜨리기 위해서다.
- **점 분포**: 점이 구의 일부(예: 손목을 거의 수평으로만 돌림)에만 있으면 9개 계수가 잘 결정되지 않는다(정규방정식의 condition number가 커진다). 펌웨어는 "구면을 몇 개 구역으로 나눠 각 구역에 점이 몇 개 있는지"를 세어 피팅 시작 여부를 정한다.
- **움직이는 자기 교란**: 노트북 스피커, 철제 책상, 자동차는 기기와 같이 돌지 않으므로 보정으로 못 없앤다. 런타임 gating(§9.3)으로 처리한다.
- **MCU 비용**: 9×9 정규방정식(`DᵀD`, `Dᵀ1`)을 점이 들어올 때마다 누적하면 메모리는 점 개수와 무관하게 81 + 9개 float. 피팅은 가끔(수 초에 한 번) 9×9 선형계를 풀면 된다. Cholesky로 수천 FLOP 수준이다.

---

## 3. 좌표계 — sensor, body, world, 그리고 handedness

### 3.1 세 개의 좌표계

- **sensor frame**: IMU 칩 데이터시트에 그려진 x, y, z 축. 칩 모서리의 점(pin 1 표시)을 기준으로 정의된다.
- **body frame**: 제품(시계·이어버드·로봇)의 기준 축. 예를 들어 손목 기기라면 x = 손가락 쪽, z = 화면 밖으로.
- **world frame**: 땅에 붙은 기준. 두 관례가 있다 (아래 두 항목).
- **ENU** (x = East, y = North, z = Up): ROS(REP 103), 많은 로보틱스·AR 코드.
- **NED** (x = North, y = East, z = Down): 항공·드론(PX4 등) 관례.

```svg
<svg viewBox="0 0 660 260" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="g3b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <text x="110" y="24" font-size="13" text-anchor="middle">ENU (world, 로보틱스·ROS 관례)</text> <line x1="110" y1="150" x2="190" y2="150" stroke="#4a7bd0" stroke-width="2.5" marker-end="url(#g3b)"/> <line x1="110" y1="150" x2="160" y2="105" stroke="#3f9a6b" stroke-width="2.5" marker-end="url(#g3b)"/> <line x1="110" y1="150" x2="110" y2="60" stroke="#d0564a" stroke-width="2.5" marker-end="url(#g3b)"/>
  <text x="196" y="155" font-size="12">x = East</text> <text x="164" y="100" font-size="12">y = North</text> <text x="116" y="62" font-size="12">z = Up</text> <text x="110" y="200" font-size="12" text-anchor="middle">정지 accel → (0, 0, +g)</text> <text x="330" y="24" font-size="13" text-anchor="middle">NED (world, 항공·드론 관례)</text> <line x1="330" y1="110" x2="380" y2="65" stroke="#4a7bd0" stroke-width="2.5" marker-end="url(#g3b)"/> <line x1="330" y1="110" x2="410" y2="110" stroke="#3f9a6b" stroke-width="2.5" marker-end="url(#g3b)"/>
  <line x1="330" y1="110" x2="330" y2="185" stroke="#d0564a" stroke-width="2.5" marker-end="url(#g3b)"/> <text x="384" y="60" font-size="12">x = North</text> <text x="416" y="115" font-size="12">y = East</text> <text x="336" y="190" font-size="12">z = Down</text> <text x="330" y="220" font-size="12" text-anchor="middle">정지 accel → (0, 0, −g)</text> <text x="550" y="24" font-size="13" text-anchor="middle">body (손목 기기, 예시)</text> <rect x="510" y="110" width="80" height="50" rx="10" fill="#888" fill-opacity="0.2" stroke="currentColor"/>
  <line x1="550" y1="135" x2="630" y2="135" stroke="#4a7bd0" stroke-width="2.5" marker-end="url(#g3b)"/> <line x1="550" y1="135" x2="590" y2="85" stroke="#3f9a6b" stroke-width="2.5" marker-end="url(#g3b)"/> <line x1="550" y1="135" x2="550" y2="55" stroke="#d0564a" stroke-width="2.5" marker-end="url(#g3b)"/> <text x="600" y="152" font-size="12">x 손가락 쪽</text> <text x="594" y="82" font-size="12">y</text> <text x="556" y="58" font-size="12">z 화면 밖</text> <text x="550" y="200" font-size="12" text-anchor="middle">IMU 칩 축 ≠ body 축일 수 있다</text>
  <text x="550" y="218" font-size="12" text-anchor="middle">→ R_bs (mounting)로 맞춘다</text> <text x="330" y="250" font-size="12" text-anchor="middle">세 좌표계 모두 오른손 좌표계 (x × y = z). ENU ↔ NED는 x·y 교환 + z 반전 (det = +1)</text>
</svg>
```

그림 4 — ENU, NED, 손목 기기 body 좌표. 셋 다 오른손 좌표계다. ENU에서 정지 가속도계는 (0, 0, +g), NED에서는 (0, 0, −g)로 쓴다.

**가속도계 부호 주의**: 가속도계는 "중력 가속도"가 아니라 **specific force** `f = a − g_vec`를 잰다. 책상 위에 정지해 있으면 `a = 0`이고 중력 벡터는 아래를 향하므로 `f = −g_vec` = **위쪽 +1 g**. 자유낙하하면 `a = g_vec`이라 `f = 0`. 이 노트는 ENU를 쓰므로 정지 시 `Rᵀ·(0, 0, 9.81)`이 가속도계 출력이다.

### 3.2 장착 회전(mounting)과 축 remap

IMU 칩은 PCB 레이아웃 때문에 90° 돌아가 있거나 기판 뒷면에 붙는다. 그러면 펌웨어가 `body = R_bs · sensor`로 축을 바꿔 줘야 한다. `R_bs`는 보통 0과 ±1만 있는 행렬(축 순서 바꾸기와 부호 뒤집기)이다.

규칙은 하나다: **`R_bs`는 회전행렬이어야 한다 — 직교이고 det = +1**. 축을 바꾸거나 뒤집을 때 det가 −1이 되면(거울상) 가속도계 값은 멀쩡해 보이지만, 자이로는 **각속도가 pseudovector**(외적으로 정의되는 양)라서 거울상 좌표에서 회전 방향이 반대로 해석된다. 그러면 "가속도계가 말하는 자세"와 "자이로를 적분한 자세"가 서로 반대로 움직여서 퓨전이 싸운다.

아래 코드는 칩이 PCB 뒷면에 90° 돌아 붙은 상황을 만든 뒤, 올바른 remap과 "y 부호를 하나 더 뒤집은" 실수 remap으로 같은 Mahony 필터(§7)를 돌린다. 이 예제는 §5.4의 시뮬레이션 모듈과 §7의 필터를 쓰므로, 처음 읽을 때는 결과만 보고 나중에 실행해도 된다.

```python
# 장착 회전(mounting)과 handedness: remap 행렬의 det가 −1이면 accel은 멀쩡해 보여도 퓨전이 망가진다
import numpy as np
from g3_sim import t, dt, gyro, acc, up_true
from g3_filters import mahony
up = lambda Q: np.array([[2*(x*z - w*y), 2*(y*z + w*x), 1 - 2*(x*x + y*y)] for w, x, y, z in Q])
R_bs = np.array([[0, 1, 0], [1, 0, 0], [0, 0, -1]])     # IMU가 PCB 뒷면에 90° 돌아 붙음 (sensor→body)
print("det(R_bs) =", round(np.linalg.det(R_bs)), "| R_bsᵀR_bs = I ?", np.allclose(R_bs.T @ R_bs, np.eye(3)))
acc_s, gyro_s = acc @ R_bs, gyro @ R_bs                  # 센서가 실제로 내는 값 = R_bsᵀ·(body 값)
D = np.diag([1, -1, 1])                                  # 실수: y 부호 하나만 더 뒤집음 → det −1
for name, M in [("정답 remap", R_bs), ("y 부호 실수", D @ R_bs)]:
    a_b, g_b = acc_s @ M.T, gyro_s @ M.T                 # 펌웨어: body = M · sensor
    Q, _ = mahony(g_b, a_b, dt, np.array([1.0, 0, 0, 0]), 1.0, 0.05)    # 초기값 모름 → identity
    ref = up_true @ (M @ R_bs.T).T                       # 그 좌표계에서 accel이 가리키는 '위'
    c = np.sum(up(Q) * ref, 1); e = np.rad2deg(np.arccos(np.clip(c, -1, 1)))[t > 10]
    print(f"{name:10s} det {np.linalg.det(M):+.0f} | |a| 평균 {np.linalg.norm(a_b, axis=1).mean():5.2f}"
          f" | tilt 오차 RMS {np.sqrt((e**2).mean()):6.2f}°, max {e.max():6.2f}°")
ENU2NED = np.array([[0, 1, 0], [1, 0, 0], [0, 0, -1]])  # (E,N,U) → (N,E,D)
print("ENU→NED det =", round(np.linalg.det(ENU2NED)), "| ENU 위쪽 [0,0,1] → NED", ENU2NED @ [0, 0, 1])
```

```text
det(R_bs) = 1 | R_bsᵀR_bs = I ? True
정답 remap   det +1 | |a| 평균  9.85 | tilt 오차 RMS   0.69°, max   2.59°
y 부호 실수    det -1 | |a| 평균  9.85 | tilt 오차 RMS  63.41°, max 127.34°
ENU→NED det = 1 | ENU 위쪽 [0,0,1] → NED [ 0  0 -1]
```

출력에서 볼 것: 실수 remap에서도 |a| 평균은 9.85로 정상이다 — 가속도계만 보면 아무 이상이 없다. 그런데 tilt 오차 RMS가 0.69° → 63°로 망가진다. ENU → NED 변환 행렬도 det = +1인 회전이다(x·y 교환 + z 반전).

**Don 경험과 연결**: 버스 bring-up에서 "레지스터 값은 읽히는데 의미가 틀린" 버그와 같다. 축 remap 검증은 데이터시트 그림을 믿지 말고 **물리 테스트**로 한다: (1) 기기를 화면 위로 놓고 az ≈ +g 확인, (2) 오른쪽을 들어 ax/ay 부호 확인, (3) 위에서 봤을 때 반시계로 돌려 gz > 0 확인(오른손 법칙). 세 개를 자동 테스트로 만들어 factory test에 넣는다.

> 함정: 지자기계는 같은 패키지 안에 있어도 **가속도계·자이로와 축 방향이 다른 칩**이 있다(예: 일부 9축 칩은 지자기 die가 따로 있다). 센서마다 `R_bs`를 따로 둔다.

---

## 4. 회전 표현 — 회전행렬, Euler, axis-angle, quaternion

### 4.1 회전행렬 — 기준이 되는 표현

회전행렬 `R ∈ ℝ³ˣ³`은 A1에서 본 직교행렬 중 det = +1인 것이다(SO(3)).

```
RᵀR = I     (열벡터들이 서로 수직인 단위벡터)
det R = +1  (거울상이 아님)
R⁻¹ = Rᵀ    (역회전은 전치 — 계산이 공짜)
```

이 노트의 약속: **`R`은 body 벡터를 world 좌표로 바꾼다** (`v_world = R · v_body`). 따라서 world에 고정된 벡터(중력, 자기장)를 body에서 보면 `v_body = Rᵀ · v_world`. `Rᵀ`의 셋째 열 = `R`의 셋째 행 = **body 좌표로 본 "위" 방향**이다. 이 한 줄이 이후 모든 필터의 핵심 재료다.

**합성**: body가 먼저 `R₂`만큼 돌고 그 상태에서 다시 자기 축 기준으로 `R₁`... 같은 말은 헷갈리기 쉽다. 이 노트에서는 "자세 `R`에서 body 축 기준으로 작은 회전 `ΔR`을 더 하면 새 자세는 `R · ΔR`"(오른쪽에 곱한다)로 통일한다. 자이로가 body 축 기준 각속도를 주므로 적분은 항상 오른쪽 곱이다.

**손계산**: z축 기준 90° 회전.

```
Rz(90°) = [ cos90 −sin90  0 ]   [ 0 −1  0 ]
          [ sin90  cos90  0 ] = [ 1  0  0 ]
          [ 0      0      1 ]   [ 0  0  1 ]
Rz · (1, 0, 0)ᵀ = (0, 1, 0)ᵀ    ← x축이 y축으로
```

장점: 벡터 회전이 행렬곱 한 번(9 MAC), 합성이 행렬곱, 특이점이 없다. 단점: 숫자 9개로 자유도 3개를 표현하니 중복이 많고, 적분하다 보면 직교성이 깨져서 주기적으로 재직교화(Gram-Schmidt 등)가 필요하다.

### 4.2 Euler 각 — 사람이 읽기 좋지만 특이점이 있다

yaw(ψ, z축)·pitch(θ, y축)·roll(φ, x축) 세 각도로 자세를 쓴다. 항공 관례 ZYX(intrinsic)는 `R = Rz(ψ)·Ry(θ)·Rx(φ)`. 숫자 3개라 로그·UI에 좋다.

문제는 **gimbal lock**: pitch = ±90°이면

```
R = Rz(ψ) · Ry(90°) · Rx(φ)  는  (ψ − φ)  에만 의존한다
```

말로 하면: pitch가 90°이면 yaw 축과 roll 축이 같은 방향을 가리키게 되어, 두 각도가 따로 의미를 갖지 못한다. 자세는 멀쩡히 존재하는데 **좌표(표현)가 망가진 것**이다 — 지구 북극에서 경도가 정의되지 않는 것과 같다.

더 실무적인 문제는 자이로 적분이다. Euler 각의 시간 미분과 body 각속도의 관계는

```
[φ̇]   [ 1   sinφ·tanθ   cosφ·tanθ ] [ωx]
[θ̇] = [ 0   cosφ        −sinφ     ] [ωy]
[ψ̇]   [ 0   sinφ/cosθ   cosφ/cosθ ] [ωz]
```

이고 `cos θ → 0`이면 행렬 원소가 무한대로 간다. 코드로 두 가지를 확인한다.

```python
# gimbal lock: ZYX(yaw-pitch-roll)에서 pitch = 90°이면 yaw와 roll이 같은 축이 된다
import numpy as np, warnings
from scipy.spatial.transform import Rotation as R

A = R.from_euler("ZYX", [30, 90, 10], degrees=True).as_matrix()   # yaw 30, roll 10
B = R.from_euler("ZYX", [50, 90, 30], degrees=True).as_matrix()   # yaw 50, roll 30
print("서로 다른 (yaw, roll)인데 행렬 차이 max =", np.abs(A - B).max())   # yaw−roll = 20 동일
with warnings.catch_warnings(record=True) as w:
    warnings.simplefilter("always")
    back = R.from_matrix(A).as_euler("ZYX", degrees=True)
    print("as_euler 결과 =", np.round(back, 3), "| 경고:", w[0].message if w else None)

# Euler 각속도 = T(roll, pitch) · ω_body. pitch → 90°에서 T가 폭발한다
def T(roll, pitch):
    sr, cr, tp, cp = np.sin(roll), np.cos(roll), np.tan(pitch), np.cos(pitch)
    return np.array([[1, sr*tp, cr*tp], [0, cr, -sr], [0, sr/cp, cr/cp]])
for p in [0, 60, 85, 89, 89.9]:
    Tm = T(np.deg2rad(10), np.deg2rad(p))
    print(f"pitch {p:5.1f}°: max|T| = {np.abs(Tm).max():9.2f}, cond = {np.linalg.cond(Tm):10.1f}")
```

```text
서로 다른 (yaw, roll)인데 행렬 차이 max = 4.440892098500626e-16
as_euler 결과 = [20. 90.  0.] | 경고: Gimbal lock detected. Setting third angle to zero since it is not possible to uniquely determine all angles.
pitch   0.0°: max|T| =      1.00, cond =        1.0
pitch  60.0°: max|T| =      1.97, cond =        3.7
pitch  85.0°: max|T| =     11.30, cond =       22.9
pitch  89.0°: max|T| =     56.43, cond =      114.6
pitch  89.9°: max|T| =    564.25, cond =     1145.9
```

출력에서 볼 것: yaw 30/roll 10과 yaw 50/roll 30이 **같은 행렬**(차이 4e-16)이 된다. scipy는 역변환에서 "Gimbal lock detected" 경고를 내고 셋째 각을 0으로 정한다. 변환 행렬은 pitch 89.9°에서 원소가 564까지 커진다 — 자이로 noise가 수백 배 증폭된다는 뜻이다. 손목 기기는 팔을 들어 화면을 볼 때 pitch가 크게 움직이므로 이 문제가 실제로 생긴다.

> 함정: Euler 각은 **12가지 순서**(ZYX, ZXZ, XYZ…)와 intrinsic/extrinsic 구분이 있다. scipy에서 대문자 `"ZYX"`는 intrinsic, 소문자 `"zyx"`는 extrinsic이다. 다른 팀/라이브러리와 Euler 각을 주고받을 때는 순서·방향·단위를 문서에 명시하거나, 아예 quaternion으로 주고받는다.

### 4.3 Axis-angle과 회전벡터

모든 3D 회전은 "어떤 단위축 `k` 둘레로 각 `θ`만큼"으로 쓸 수 있다(Euler의 회전 정리). 둘을 곱한 **회전벡터** `r = θ·k`(숫자 3개)가 자주 쓰인다. 자이로 샘플 하나 `ω·dt`가 바로 회전벡터다.

```
Rodrigues 공식:  R = I + sinθ · [k×] + (1 − cosθ) · [k×]²

  [k×] = [  0   −kz   ky ]      (외적 행렬: [k×]·v = k × v)
         [  kz   0   −kx ]
         [ −ky   kx   0  ]
```

말로 하면: 축 방향 성분은 그대로 두고, 축에 수직인 성분만 평면에서 θ만큼 돌린다. 회전벡터는 작은 회전(필터의 오차 상태, §8.3)을 표현하기에 이상적이지만, 두 회전을 합성하는 연산이 간단하지 않아서 "누적 자세"를 저장하는 용도로는 쓰지 않는다.

### 4.4 Quaternion — 자세 추정의 표준

**정의**: 숫자 4개 `q = (w, x, y, z) = (w, u)`, `w`는 스칼라부, `u = (x, y, z)`는 벡터부. 회전을 나타내는 것은 **단위 quaternion**(`w² + x² + y² + z² = 1`)이고, 축 `k` 둘레 각 `θ` 회전은

```
q = ( cos(θ/2),  sin(θ/2) · k )
```

말로 하면: 각도의 **절반**이 들어간다. 그래서 `q`와 `−q`는 같은 회전이다(θ와 θ + 360°). 이 이중성 때문에 두 quaternion을 비교할 때는 `|q₁·q₂|`(내적의 절댓값)을 쓴다.

**곱 (Hamilton product)** — 회전의 합성:

```
p ⊗ q = ( p_w·q_w − p_u·q_u ,   p_w·q_u + q_w·p_u + p_u × q_u )

성분으로:
w = pw·qw − px·qx − py·qy − pz·qz
x = pw·qx + px·qw + py·qz − pz·qy
y = pw·qy − px·qz + py·qw + pz·qx
z = pw·qz + px·qy − py·qx + pz·qw
```

외적 항 때문에 **교환법칙이 성립하지 않는다**(`p ⊗ q ≠ q ⊗ p`). 회전행렬 곱과 같은 성질이다. 곱 한 번은 곱셈 16번 + 덧셈 12번.

**벡터 회전**: `v' = q ⊗ (0, v) ⊗ q*`, 여기서 `q* = (w, −u)`(켤레 = 단위 quaternion의 역). 계산을 줄인 동등한 식은 `v' = v + 2w(u × v) + 2u × (u × v)`.

**손계산 1 — 벡터 회전**: z축 90°, `q₁ = (cos45°, 0, 0, sin45°) = (0.7071, 0, 0, 0.7071)`, `v = (1, 0, 0)`.

```
u = (0, 0, 0.7071),  w = 0.7071
u × v       = (0, 0.7071, 0)
2w(u × v)   = 2 · 0.7071 · (0, 0.7071, 0) = (0, 1, 0)
u × (u × v) = (0, 0, 0.7071) × (0, 0.7071, 0) = (−0.5, 0, 0)  → ×2 = (−1, 0, 0)
v' = (1, 0, 0) + (0, 1, 0) + (−1, 0, 0) = (0, 1, 0)     ← Rz(90°)와 같다
```

**손계산 2 — 곱**: `q₂` = x축 30° = `(cos15°, sin15°, 0, 0) = (0.9659, 0.2588, 0, 0)`.

```
q₁ ⊗ q₂:
w = 0.7071·0.9659 − 0 − 0 − 0.7071·0      = 0.683
x = 0.7071·0.2588 + 0 + 0 − 0.7071·0      = 0.183
y = 0 − 0 + 0 + 0.7071·0.2588             = 0.183
z = 0 + 0 − 0 + 0.7071·0.9659             = 0.683
```

**각속도 적분**: body 각속도 `ω`(자이로)로 자세가 변하는 미분방정식은

```
q̇ = ½ · q ⊗ (0, ω)
```

말로 하면: 지금 자세 `q`의 **오른쪽에** body 축 기준 작은 회전을 계속 곱해 간다. 이산화는 두 가지:

```
(1) 1차 Euler:  q ← q + ½ · q ⊗ (0, ω) · dt,  그 다음 q ← q / |q|   (정규화 필수)
(2) 지수맵:     q ← q ⊗ exp(ω·dt),   exp(r) = (cos(|r|/2), sin(|r|/2) · r/|r|)
```

(1)은 곱셈·덧셈만 쓰므로 MCU에서 싸고, `dt`가 작으면 (2)와 거의 같다. 정규화를 빼먹으면 |q|가 1에서 점점 멀어져 회전에 크기 변화가 섞인다.

이제 이 연산들을 numpy로 직접 구현한다. 이 노트의 모든 필터가 이 모듈을 쓴다.

```python
# g3_quat.py — Hamilton quaternion, scalar-first [w, x, y, z], q = body→world 회전
import numpy as np

def qmul(p, q):                       # p ⊗ q
    pw, px, py, pz = p; qw, qx, qy, qz = q
    return np.array([pw*qw - px*qx - py*qy - pz*qz,
                     pw*qx + px*qw + py*qz - pz*qy,
                     pw*qy - px*qz + py*qw + pz*qx,
                     pw*qz + px*qy - py*qx + pz*qw])

def qconj(q):  return q * np.array([1, -1, -1, -1])
def qnorm(q):  return q / np.linalg.norm(q)

def qrot(q, v):                       # q ⊗ (0,v) ⊗ q*  : body 벡터 → world
    return qmul(qmul(q, np.r_[0.0, v]), qconj(q))[1:]

def qexp(rotvec):                     # 회전벡터(axis·angle) → 단위 quaternion
    th = np.linalg.norm(rotvec)
    if th < 1e-12: return qnorm(np.r_[1.0, 0.5 * rotvec])
    return np.r_[np.cos(th / 2), np.sin(th / 2) * rotvec / th]

def qmat(q):                          # 회전행렬 R (body→world)
    w, x, y, z = q
    return np.array([[1-2*(y*y+z*z), 2*(x*y-w*z),   2*(x*z+w*y)],
                     [2*(x*y+w*z),   1-2*(x*x+z*z), 2*(y*z-w*x)],
                     [2*(x*z-w*y),   2*(y*z+w*x),   1-2*(x*x+y*y)]])

def qangle(p, q):                     # 두 자세 사이 각도 [rad]
    return 2 * np.arccos(min(1.0, abs(float(np.dot(p, q)))))
```

그리고 scipy `Rotation`과 맞춰 본다. **scipy의 `as_quat()`은 스칼라가 마지막인 `[x, y, z, w]` 순서**라는 점에 주의한다(이 노트와 Eigen의 생성자, 많은 펌웨어 코드는 스칼라가 앞이다).

```python
# 우리 quaternion 함수 vs scipy Rotation — 곱, 벡터 회전, 행렬, 적분
import numpy as np
from scipy.spatial.transform import Rotation as R
from g3_quat import qmul, qrot, qexp, qmat, qnorm, qangle

q1 = qexp(np.deg2rad(90) * np.array([0, 0, 1]))       # z축 90°
q2 = qexp(np.deg2rad(30) * np.array([1, 0, 0]))       # x축 30°
print("q1 =", np.round(q1, 4), " q2 =", np.round(q2, 4))
v = np.array([1.0, 0, 0])
print("q1로 x축 돌리기:", np.round(qrot(q1, v), 4))
q12 = qmul(q1, q2)                                   # 먼저 q2, 그 다음 q1 (body→world 합성)
s12 = R.from_quat(q1[[1,2,3,0]]) * R.from_quat(q2[[1,2,3,0]])   # scipy는 [x,y,z,w]!
print("q1⊗q2      =", np.round(q12, 4))
print("scipy as_quat (xyzw)=", np.round(s12.as_quat(), 4), "→ wxyz", np.round(s12.as_quat()[[3,0,1,2]], 4))
print("행렬 차이 max =", np.abs(qmat(q12) - s12.as_matrix()).max())
print("q2⊗q1 ≠ q1⊗q2:", np.round(qmul(q2, q1), 4))

# 각속도 적분: 몸체 좌표 ω = (0.3, -0.2, 0.5) rad/s 를 2초 동안, dt = 0.01
w = np.array([0.3, -0.2, 0.5]); dt = 0.01
q_euler = np.array([1.0, 0, 0, 0]); q_exp = q_euler.copy()
for _ in range(200):
    q_euler = q_euler + 0.5 * qmul(q_euler, np.r_[0.0, w]) * dt   # q̇ = ½ q⊗ω (1차 Euler)
    q_exp = qmul(q_exp, qexp(w * dt))                            # 정확한 지수맵
truth = R.from_rotvec(w * 2.0).as_quat()[[3, 0, 1, 2]]
print("|q| Euler 적분(정규화 전) =", round(np.linalg.norm(q_euler), 6))
print("오차 Euler+정규화 [deg] =", np.rad2deg(qangle(qnorm(q_euler), truth)))
print("오차 지수맵 [deg]       =", np.rad2deg(qangle(q_exp, truth)))
```

```text
q1 = [0.7071 0.     0.     0.7071]  q2 = [0.9659 0.2588 0.     0.    ]
q1로 x축 돌리기: [0. 1. 0.]
q1⊗q2      = [0.683 0.183 0.183 0.683]
scipy as_quat (xyzw)= [0.183 0.183 0.683 0.683] → wxyz [0.683 0.183 0.183 0.683]
행렬 차이 max = 2.220446049250313e-16
q2⊗q1 ≠ q1⊗q2: [ 0.683  0.183 -0.183  0.683]
|q| Euler 적분(정규화 전) = 1.00095
오차 Euler+정규화 [deg] = 0.00022369521257908985
오차 지수맵 [deg]       = 0.0
```

출력에서 볼 것: (1) 손계산과 같은 `[0.683, 0.183, 0.183, 0.683]`. (2) scipy의 xyzw 순서를 wxyz로 바꾸면 같고, 회전행렬 차이는 2e-16. (3) 순서를 바꾼 곱은 y 성분 부호가 달라 다른 회전이다. (4) 1차 Euler 적분은 200스텝 후 |q| = 1.00095로 커졌지만, 정규화하면 정확한 해와 0.0002° 차이뿐이다(각속도가 일정한 경우).

### 4.5 표현 비교

| 표현 | 숫자 수 | 특이점 | 합성 | 벡터 회전 비용 | 주 용도 |
|---|---|---|---|---|---|
| 회전행렬 | 9 | 없음 | 행렬곱 27 MAC | 9 MAC | 벡터 변환, 수식 유도 |
| Euler 각 | 3 | gimbal lock (pitch ±90°) | 직접 불가 (행렬 경유) | 삼각함수 필요 | UI·로그·사람이 읽기 |
| 회전벡터 (axis-angle) | 3 | 180° 근처에서 불연속 | 직접 불가 | Rodrigues | 작은 회전, 오차 상태, 자이로 증분 |
| 단위 quaternion | 4 | 없음 (q와 −q 이중성만) | 곱 16 MUL + 12 ADD | 약 18 MUL (최적화 식) | 자세 저장·적분·보간 |

quaternion이 자세 추정의 표준인 이유: 특이점이 없고, 숫자 4개로 제약이 하나(|q| = 1)뿐이라 정규화가 싸고(제곱합·√·곱 4번), 적분식이 곱셈·덧셈뿐이며, 두 자세 사이 보간(slerp)이 자연스럽다.

---

## 5. 가속도계로 기울기, 자이로로 적분 — 각자의 한계

### 5.1 가속도계 tilt (roll, pitch)

정지 상태에서 가속도계는 body 좌표로 본 "위" 방향 × g를 잰다. ZYX Euler 관례에서

```
a_body = Rᵀ · (0, 0, g) = g · ( −sinθ,  sinφ·cosθ,  cosφ·cosθ )

→  roll  φ = atan2(a_y, a_z)
   pitch θ = atan2(−a_x, √(a_y² + a_z²))
```

말로 하면: 중력 벡터가 body 축에 어떻게 나뉘어 찍혔는지에서 두 기울기를 역산한다. `atan2`는 사분면을 구분해 주고, pitch 식의 분모에 `√(a_y² + a_z²)`를 쓰면 roll과 무관하게 −90°~90°가 나온다.

**손계산**: `a = (−3.355, −5.287, 7.551)` m/s².

```
roll  = atan2(−5.287, 7.551):  5.287 / 7.551 = 0.7002  → tan 35° = 0.7002  → −35.0°
pitch = atan2(3.355, √(5.287² + 7.551²)) = atan2(3.355, √(27.95 + 57.02)) = atan2(3.355, 9.218)
        3.355 / 9.218 = 0.364 → tan 20° = 0.364  → 20.0°
```

**yaw는 안 보인다**: 중력은 수직축 둘레 회전에 대해 대칭이다. 위를 향한 벡터를 수직축 둘레로 아무리 돌려도 같은 벡터다. 그래서 yaw(heading)는 가속도계로 알 수 없고, **자이로 적분**(상대적, drift) 또는 **지자기계**(절대적, 교란에 약함)가 필요하다.

```python
# 가속도계로 roll/pitch 구하기 — 그리고 yaw는 왜 안 보이는가
import numpy as np
from scipy.spatial.transform import Rotation as R
g_up = np.array([0, 0, 9.81])                      # 정지 가속도계가 world에서 느끼는 specific force (ENU, 위로 +1 g)

def accel_reading(yaw, pitch, roll):               # 기기 자세(ZYX, deg) → 3축 가속도 측정 [m/s²]
    Rbw = R.from_euler("ZYX", [yaw, pitch, roll], degrees=True)   # body→world
    return Rbw.inv().apply(g_up)                    # world 벡터를 body 좌표로: Rᵀ·g

def tilt(a):
    roll  = np.arctan2(a[1], a[2])
    pitch = np.arctan2(-a[0], np.hypot(a[1], a[2]))
    return np.rad2deg([roll, pitch])

for ypr in [(0, 0, 0), (0, 20, -35), (120, 20, -35), (-75, 20, -35), (0, 89, 30)]:
    a = accel_reading(*ypr)
    print(f"yaw {ypr[0]:4d} pitch {ypr[1]:3d} roll {ypr[2]:4d} → a = {np.round(a, 3)} "
          f"→ roll {tilt(a)[0]:7.2f}, pitch {tilt(a)[1]:6.2f}")

rng = np.random.default_rng(0)                     # 같은 noise(σ = 0.02 m/s²)에서 roll 흔들림
for pitch in [0, 60, 85, 89]:
    a = accel_reading(0, pitch, 30) + rng.normal(0, 0.02, (1000, 3))
    rolls = np.array([tilt(x)[0] for x in a])
    print(f"pitch {pitch:2d}°: roll 표준편차 = {rolls.std():6.3f}°")
```

```text
yaw    0 pitch   0 roll    0 → a = [0.   0.   9.81] → roll    0.00, pitch  -0.00
yaw    0 pitch  20 roll  -35 → a = [-3.355 -5.287  7.551] → roll  -35.00, pitch  20.00
yaw  120 pitch  20 roll  -35 → a = [-3.355 -5.287  7.551] → roll  -35.00, pitch  20.00
yaw  -75 pitch  20 roll  -35 → a = [-3.355 -5.287  7.551] → roll  -35.00, pitch  20.00
yaw    0 pitch  89 roll   30 → a = [-9.809  0.086  0.148] → roll   30.00, pitch  89.00
pitch  0°: roll 표준편차 =  0.115°
pitch 60°: roll 표준편차 =  0.233°
pitch 85°: roll 표준편차 =  1.297°
pitch 89°: roll 표준편차 =  6.511°
```

출력에서 볼 것: yaw가 0, 120, −75로 달라도 가속도 값이 **완전히 같다** — yaw는 관측 불가다. 그리고 같은 noise(σ = 0.02 m/s²)에서 roll 추정의 흔들림이 pitch 0°에서 0.115°, 89°에서 6.5°로 커진다. pitch가 90°에 가까우면 roll을 결정하는 `a_y`, `a_z`가 둘 다 0에 가까워지기 때문이다(Euler 특이점의 또 다른 얼굴).

### 5.2 자이로 적분 — 짧게는 완벽, 길게는 drift

자이로는 각속도를 재므로 자세를 얻으려면 적분해야 한다. bias `b`가 남아 있으면 각도 오차가 `b · t`로 선형 증가한다(0.6 dps → 1분에 36°). white noise도 적분되면 random walk가 되어 `σ·√t`로 커진다(ARW, G1). 즉 자이로는 **고주파(빠른 변화)에는 정확하고 저주파(긴 시간)에는 나쁘다**. 가속도계는 반대로 **저주파(평균)에는 정확하고 고주파(움직임·진동)에는 나쁘다**. 이 대칭이 퓨전의 출발점이다.

### 5.3 테스트 벤치 — 정답이 있는 60초 시뮬레이션

이후 모든 필터를 같은 데이터로 비교하기 위해, 정답 자세를 아는 손목 움직임을 만든다.

- 100 Hz, 60 s. 처음 5 s는 정지, 이후 여러 주파수 사인파가 섞인 body 각속도(최대 약 2 rad/s).
- 정답 자세: 샘플 간격을 10배 잘게 나눠 지수맵으로 정확히 적분.
- 자이로: 샘플 구간 평균 각속도 + bias (0.6, −0.4, 0.3) dps + noise 0.05 dps. "구간 평균"은 실제 센서의 디지털 필터·적분형 ADC를 흉내 낸 것이다(§9.4의 타이밍 함정 참고).
- 가속도계: `Rᵀ(a_lin + g)` + noise 0.03 m/s². 20–26 s에는 **수평으로 손 흔들기**(4 m/s², 2 Hz), 40–43 s에는 **위아래 점프**(6 m/s², 1 Hz).
- 지자기계: 보정이 끝난 상태 + noise 0.3 µT. 30–36 s에 **철제 책상 같은 외부 교란**(world 기준 15, 0, 10 µT).

```python
# g3_sim.py — 정답 자세가 있는 60 s 손목 움직임 + 6/9축 센서 측정 (100 Hz)
import numpy as np
from g3_quat import qmul, qexp, qmat
fs, T = 100, 60.0; dt = 1 / fs
t = np.arange(0, T, dt); N = len(t)
g_up = np.array([0, 0, 9.81])                            # ENU, 정지 시 가속도계 = +1 g 위쪽
m_world = np.array([0.0, 20.0, -45.0])                   # µT, 북(+y)·아래
def omega(tt):                                           # body 각속도 [rad/s], 처음 5 s 정지
    env = np.clip((tt - 5) / 2, 0, 1)
    return env * np.array([1.2*np.sin(2*np.pi*0.31*tt) + 0.5*np.sin(2*np.pi*1.3*tt),
                           0.9*np.sin(2*np.pi*0.23*tt + 1) + 0.4*np.sin(2*np.pi*0.9*tt),
                           0.7*np.sin(2*np.pi*0.17*tt + 2) + 0.3*np.sin(2*np.pi*0.7*tt)])
q_true = np.zeros((N, 4)); q = np.array([0.94, 0.17, 0.08, 0.28]); q /= np.linalg.norm(q)
w_avg = np.zeros((N, 3))                                 # 샘플 구간 [t_k, t_k+1] 평균 각속도
for k in range(N):                                       # 10배 촘촘히 정확 적분 → 정답
    q_true[k] = q
    for j in range(10):
        wj = omega(t[k] + (j + 0.5) * dt / 10); w_avg[k] += wj / 10
        q = qmul(q, qexp(wj * dt / 10))
a_lin = np.zeros((N, 3))                                 # world 선가속도 [m/s²]
s1 = (t > 20) & (t < 26); a_lin[s1, 0] = 4.0 * np.sin(2*np.pi*2.0*t[s1])   # 손 흔들기
s2 = (t > 40) & (t < 43); a_lin[s2, 2] = 6.0 * np.sin(2*np.pi*1.0*t[s2])   # 위아래 점프
md = (t > 30) & (t < 36)                                 # 자기 교란 (철제 책상 근처)
rng = np.random.default_rng(7)
b_gyro = np.deg2rad([0.6, -0.4, 0.3])                    # 남은 자이로 bias [rad/s]
Rt = np.array([qmat(q) for q in q_true])                 # body→world
gyro = w_avg + b_gyro + rng.normal(0, np.deg2rad(0.05), (N, 3))
acc = np.einsum("nji,nj->ni", Rt, a_lin + g_up) + rng.normal(0, 0.03, (N, 3))   # Rᵀ(a + g)
mag_w = np.tile(m_world, (N, 1)); mag_w[md] += [15.0, 0.0, 10.0]
mag = np.einsum("nji,nj->ni", Rt, mag_w) + rng.normal(0, 0.3, (N, 3))
up_true = Rt[:, 2, :]                                    # body 좌표로 본 '위' = Rᵀ e_z
def tilt_err_deg(up_est):                                # 추정 '위' 방향과 정답 사이 각도
    c = np.sum(up_est * up_true, 1) / np.linalg.norm(up_est, axis=1)
    return np.rad2deg(np.arccos(np.clip(c, -1, 1)))
```

```python
# 시뮬레이션이 맞게 만들어졌는지: 정지 구간 |a| = g, 정답 자세의 범위
import numpy as np
from g3_sim import t, acc, gyro, mag, q_true, up_true, s1, s2, md
from scipy.spatial.transform import Rotation as R
print("샘플 수", len(t), "| 정지 구간 |a| 평균 =", np.linalg.norm(acc[t < 5], axis=1).mean().round(3))
print("정지 구간 gyro 평균 [dps] =", np.rad2deg(gyro[t < 5].mean(0)).round(3))
eul = R.from_quat(q_true[:, [1, 2, 3, 0]]).as_euler("ZYX", degrees=True)
print("roll 범위", eul[:, 2].min().round(1), eul[:, 2].max().round(1), "| pitch 범위", eul[:, 1].min().round(1), eul[:, 1].max().round(1))
print("|a| 범위: 흔들기", np.linalg.norm(acc[s1], axis=1).min().round(2), np.linalg.norm(acc[s1], axis=1).max().round(2),
      "| 점프", np.linalg.norm(acc[s2], axis=1).min().round(2), np.linalg.norm(acc[s2], axis=1).max().round(2))
print("|m| 평소", np.linalg.norm(mag[~md], axis=1).mean().round(1), "교란 중", np.linalg.norm(mag[md], axis=1).mean().round(1))
```

```text
샘플 수 6000 | 정지 구간 |a| 평균 = 9.811
정지 구간 gyro 평균 [dps] = [ 0.596 -0.403  0.298]
roll 범위 -179.4 179.1 | pitch 범위 -47.0 89.5
|a| 범위: 흔들기 9.74 10.65 | 점프 3.75 15.84
|m| 평소 49.2 교란 중 43.0
```

출력에서 볼 것: 정지 구간 |a| = 9.811, 자이로 평균은 bias 그대로(0.596, −0.403, 0.298 dps). 자세는 roll이 ±180°를 넘나들고 pitch가 89.5°까지 간다 — Euler 기반 필터라면 특이점에 걸리는 궤적이다. 그리고 중요한 관찰: **수평 흔들기 동안 |a|는 9.74–10.65로 거의 안 변한다**. 수평 4 m/s²는 크기를 `√(9.81² + 4²) = 10.6`으로 조금만 키우지만 방향은 `atan(4/9.81) = 22°`나 기울인다. 반대로 위아래 점프는 |a|를 3.75–15.84로 크게 바꾸지만 방향은 그대로다. §9의 gating에서 이 차이가 결정적이다.

---

## 6. Complementary filter — 저역은 가속도계, 고역은 자이로

### 6.1 직관 — 두 센서를 주파수로 나눠 쓴다

자이로 적분은 "방금 전 각도 + 그 사이 변화"를 정확히 알지만 기준점이 천천히 흘러간다. 가속도계는 기준점(중력)을 알지만 매 순간 흔들린다. 그래서

```
θ_k = α · (θ_{k−1} + ω_k · dt)  +  (1 − α) · θ_acc,k        (0 < α < 1, 보통 0.95~0.995)
        └──── 자이로로 예측 ────┘      └─ 가속도계 쪽으로 조금 끌어당김 ─┘
```

말로 하면: 매 샘플 자이로로 각도를 앞으로 굴리고, 가속도계가 말하는 각도 쪽으로 `(1 − α)`만큼만 끌어당긴다. 펌웨어 관점에서는 **1차 IIR 필터 두 개를 더한 것**이고, 곱셈 몇 번이면 끝난다.

**손계산 (1 스텝)**: α = 0.98, dt = 0.01 s, 이전 각도 10°, 자이로 5 dps, 가속도계 각도 12°.

```
예측:  10 + 5 · 0.01 = 10.05°
섞기:  0.98 · 10.05 + 0.02 · 12 = 9.849 + 0.24 = 10.089°
```

### 6.2 주파수 영역 — 왜 "complementary(상보)"인가

z-변환으로 쓰면(초기값 무시)

```
Θ(z) = H_lp(z) · Θ_acc(z)  +  H_hp(z) · [Ω(z)·dt / (1 − z⁻¹)]
                                          └ 자이로 적분 = 각도 ┘
H_lp(z) = (1 − α) / (1 − α·z⁻¹)                     ← 1차 저역통과
H_hp(z) = α · (1 − z⁻¹) / (1 − α·z⁻¹)                ← 1차 고역통과
H_lp + H_hp = (1 − α + α − α·z⁻¹) / (1 − α·z⁻¹) = 1   ← 모든 주파수에서 합이 1
```

말로 하면: 가속도계 각도는 저역통과, 자이로 각도는 고역통과를 거친 뒤 더해지는데, 두 필터의 합이 정확히 1이라서 **참 각도 신호는 왜곡 없이 통과**하고, 각 센서의 나쁜 대역(가속도계의 고주파 흔들림, 자이로의 저주파 drift)만 걸러진다. 그래서 "상보" 필터다.

시정수와 차단 주파수는

```
τ  = α · dt / (1 − α)          α = 0.98, dt = 0.01 → τ = 0.49 s
f_c = 1 / (2π·τ)                                 → f_c ≈ 0.32 Hz
α  = τ / (τ + dt)              (τ를 먼저 정하고 α를 계산하는 식)
```

```svg
<svg viewBox="0 0 640 345" xmlns="http://www.w3.org/2000/svg">
  <line x1="80.0" y1="290.0" x2="600.0" y2="290.0" stroke="currentColor" stroke-width="1"/> <line x1="80.0" y1="50.0" x2="80.0" y2="290.0" stroke="currentColor" stroke-width="1"/> <line x1="80.0" y1="290.0" x2="80.0" y2="295.0" stroke="currentColor" stroke-width="1"/> <text x="80.0" y="310.0" font-size="12" text-anchor="middle">0.001</text> <line x1="190.7" y1="290.0" x2="190.7" y2="295.0" stroke="currentColor" stroke-width="1"/> <text x="190.7" y="310.0" font-size="12" text-anchor="middle">0.01</text> <line x1="301.3" y1="290.0" x2="301.3" y2="295.0" stroke="currentColor" stroke-width="1"/>
  <text x="301.3" y="310.0" font-size="12" text-anchor="middle">0.1</text> <line x1="412.0" y1="290.0" x2="412.0" y2="295.0" stroke="currentColor" stroke-width="1"/> <text x="412.0" y="310.0" font-size="12" text-anchor="middle">1</text> <line x1="522.7" y1="290.0" x2="522.7" y2="295.0" stroke="currentColor" stroke-width="1"/> <text x="522.7" y="310.0" font-size="12" text-anchor="middle">10</text> <line x1="600.0" y1="290.0" x2="600.0" y2="295.0" stroke="currentColor" stroke-width="1"/> <text x="600.0" y="310.0" font-size="12" text-anchor="middle">50</text>
  <line x1="75.0" y1="290.0" x2="80.0" y2="290.0" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="294.0" font-size="12" text-anchor="end">0.0</text> <line x1="80.0" y1="290.0" x2="600.0" y2="290.0" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/> <line x1="75.0" y1="175.0" x2="80.0" y2="175.0" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="179.0" font-size="12" text-anchor="end">0.5</text> <line x1="80.0" y1="175.0" x2="600.0" y2="175.0" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/>
  <line x1="75.0" y1="60.0" x2="80.0" y2="60.0" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="64.0" font-size="12" text-anchor="end">1.0</text> <line x1="80.0" y1="60.0" x2="600.0" y2="60.0" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/>
  <polyline fill="none" stroke="#e08a3c" stroke-width="2" points="80.0,60.0 84.4,60.0 88.7,60.0 93.1,60.0 97.5,60.0 101.8,60.0 106.2,60.0 110.6,60.0 115.0,60.0 119.3,60.0 123.7,60.0 128.1,60.0 132.4,60.0 136.8,60.0 141.2,60.0 145.5,60.0 149.9,60.0 154.3,60.0 158.7,60.0 163.0,60.0 167.4,60.0 171.8,60.1 176.1,60.1 180.5,60.1 184.9,60.1 189.2,60.1 193.6,60.1 198.0,60.2 202.4,60.2 206.7,60.2 211.1,60.3 215.5,60.3 219.8,60.4 224.2,60.4 228.6,60.5 232.9,60.6 237.3,60.8 241.7,60.9 246.1,61.1 250.4,61.3 254.8,61.6 259.2,61.9 263.5,62.3 267.9,62.7 272.3,63.2 276.6,63.9 281.0,64.6 285.4,65.5 289.7,66.6 294.1,67.8 298.5,69.3 302.9,71.0 307.2,73.0 311.6,75.4 316.0,78.1 320.3,81.2 324.7,84.8 329.1,88.8 333.4,93.4 337.8,98.4 342.2,104.0 346.6,110.1 350.9,116.7 355.3,123.7 359.7,131.1 364.0,138.8 368.4,146.7 372.8,154.7 377.1,162.8 381.5,170.8 385.9,178.6 390.3,186.2 394.6,193.6 399.0,200.7 403.4,207.4 407.7,213.8 412.1,219.7 416.5,225.3 420.8,230.6 425.2,235.4 429.6,239.9 433.9,244.1 438.3,247.9 442.7,251.5 447.1,254.7 451.4,257.7 455.8,260.5 460.2,263.0 464.5,265.3 468.9,267.4 473.3,269.4 477.6,271.2 482.0,272.8 486.4,274.3 490.8,275.6 495.1,276.8 499.5,278.0 503.9,279.0 508.2,279.9 512.6,280.8 517.0,281.6 521.3,282.3 525.7,282.9 530.1,283.5 534.5,284.1 538.8,284.5 543.2,285.0 547.6,285.4 551.9,285.7 556.3,286.1 560.7,286.4 565.0,286.6 569.4,286.9 573.8,287.1 578.2,287.2 582.5,287.4 586.9,287.5 591.3,287.6 595.6,287.7 600.0,287.7"/>
  <polyline fill="none" stroke="#4a7bd0" stroke-width="2" points="80.0,289.3 84.4,289.2 88.7,289.2 93.1,289.1 97.5,289.0 101.8,288.9 106.2,288.8 110.6,288.7 115.0,288.5 119.3,288.4 123.7,288.2 128.1,288.1 132.4,287.9 136.8,287.7 141.2,287.5 145.5,287.2 149.9,287.0 154.3,286.7 158.7,286.4 163.0,286.0 167.4,285.6 171.8,285.2 176.1,284.8 180.5,284.3 184.9,283.7 189.2,283.1 193.6,282.5 198.0,281.8 202.4,281.0 206.7,280.1 211.1,279.2 215.5,278.2 219.8,277.0 224.2,275.8 228.6,274.5 232.9,273.0 237.3,271.4 241.7,269.6 246.1,267.7 250.4,265.6 254.8,263.3 259.2,260.8 263.5,258.1 267.9,255.1 272.3,251.9 276.6,248.3 281.0,244.5 285.4,240.4 289.7,235.9 294.1,231.1 298.5,225.9 302.9,220.4 307.2,214.5 311.6,208.2 316.0,201.5 320.3,194.5 324.7,187.2 329.1,179.7 333.4,171.9 337.8,164.0 342.2,156.0 346.6,148.1 350.9,140.3 355.3,132.7 359.7,125.3 364.0,118.4 368.4,111.9 372.8,105.8 377.1,100.3 381.5,95.3 385.9,90.8 390.3,86.8 394.6,83.3 399.0,80.2 403.4,77.5 407.7,75.2 412.1,73.2 416.5,71.5 420.8,70.0 425.2,68.8 429.6,67.8 433.9,66.9 438.3,66.2 442.7,65.5 447.1,65.0 451.4,64.6 455.8,64.2 460.2,63.9 464.5,63.6 468.9,63.4 473.3,63.2 477.6,63.1 482.0,63.0 486.4,62.8 490.8,62.8 495.1,62.7 499.5,62.6 503.9,62.6 508.2,62.5 512.6,62.5 517.0,62.5 521.3,62.4 525.7,62.4 530.1,62.4 534.5,62.4 538.8,62.4 543.2,62.4 547.6,62.4 551.9,62.4 556.3,62.3 560.7,62.3 565.0,62.3 569.4,62.3 573.8,62.3 578.2,62.3 582.5,62.3 586.9,62.3 591.3,62.3 595.6,62.3 600.0,62.3"/>
  <polyline fill="none" stroke="#3f9a6b" stroke-width="1.5" stroke-dasharray="5 4" points="80.0,60.0 84.4,60.0 88.7,60.0 93.1,60.0 97.5,60.0 101.8,60.0 106.2,60.0 110.6,60.0 115.0,60.0 119.3,60.0 123.7,60.0 128.1,60.0 132.4,60.0 136.8,60.0 141.2,60.0 145.5,60.0 149.9,60.0 154.3,60.0 158.7,60.0 163.0,60.0 167.4,60.0 171.8,60.0 176.1,60.0 180.5,60.0 184.9,60.0 189.2,60.0 193.6,60.0 198.0,60.0 202.4,60.0 206.7,60.0 211.1,60.0 215.5,60.0 219.8,60.0 224.2,60.0 228.6,60.0 232.9,60.0 237.3,60.0 241.7,60.0 246.1,60.0 250.4,60.0 254.8,60.0 259.2,60.0 263.5,60.0 267.9,60.0 272.3,60.0 276.6,60.0 281.0,60.0 285.4,60.0 289.7,60.0 294.1,60.0 298.5,60.0 302.9,60.0 307.2,60.0 311.6,60.0 316.0,60.0 320.3,60.0 324.7,60.0 329.1,60.0 333.4,60.0 337.8,60.0 342.2,60.0 346.6,60.0 350.9,60.0 355.3,60.0 359.7,60.0 364.0,60.0 368.4,60.0 372.8,60.0 377.1,60.0 381.5,60.0 385.9,60.0 390.3,60.0 394.6,60.0 399.0,60.0 403.4,60.0 407.7,60.0 412.1,60.0 416.5,60.0 420.8,60.0 425.2,60.0 429.6,60.0 433.9,60.0 438.3,60.0 442.7,60.0 447.1,60.0 451.4,60.0 455.8,60.0 460.2,60.0 464.5,60.0 468.9,60.0 473.3,60.0 477.6,60.0 482.0,60.0 486.4,60.0 490.8,60.0 495.1,60.0 499.5,60.0 503.9,60.0 508.2,60.0 512.6,60.0 517.0,60.0 521.3,60.0 525.7,60.0 530.1,60.0 534.5,60.0 538.8,60.0 543.2,60.0 547.6,60.0 551.9,60.0 556.3,60.0 560.7,60.0 565.0,60.0 569.4,60.0 573.8,60.0 578.2,60.0 582.5,60.0 586.9,60.0 591.3,60.0 595.6,60.0 600.0,60.0"/>
  <line x1="357.9" y1="50.0" x2="357.9" y2="290.0" stroke="#888" stroke-width="1" stroke-dasharray="2 3"/> <text x="361.9" y="150.0" font-size="12" text-anchor="start">f_c ≈ 0.32 Hz</text> <line x1="380.0" y1="64.0" x2="396.0" y2="64.0" stroke="#e08a3c" stroke-width="2"/> <text x="402.0" y="68.0" font-size="12" text-anchor="start">accel 경로: 저역통과 (1−α)/(1−α·z⁻¹)</text> <line x1="380.0" y1="84.0" x2="396.0" y2="84.0" stroke="#4a7bd0" stroke-width="2"/> <text x="402.0" y="88.0" font-size="12" text-anchor="start">gyro 경로: 고역통과</text>
  <line x1="380.0" y1="104.0" x2="396.0" y2="104.0" stroke="#3f9a6b" stroke-width="1.5" stroke-dasharray="5 4"/> <text x="402.0" y="108.0" font-size="12" text-anchor="start">합 = 1 (모든 주파수)</text> <text x="340.0" y="335.0" font-size="13" text-anchor="middle">주파수 [Hz] (log)</text> <text x="20.0" y="40.0" font-size="13" text-anchor="start">|H|</text>
</svg>
```

그림 5 — α = 0.98, fs = 100 Hz의 두 경로 크기 응답(실제 계산). 약 0.32 Hz 아래는 가속도계, 위는 자이로가 담당하고, 두 응답의 합(초록 점선)은 모든 주파수에서 1이다.

**α 고르는 법** (교과서 답 + 실무 감각):

- 가속도계를 믿으면 안 되는 움직임의 주파수(걷기 1–2 Hz, 손 흔들기 2–5 Hz)보다 `f_c`가 **충분히 낮아야** 한다 → τ를 길게.
- 그런데 τ가 길면 자이로 bias가 그대로 각도 오차로 남는다. 1축에서 정상상태 오차를 계산해 보면 `e = α(e + b·dt)` → `e = b · α·dt/(1 − α) = b · τ`. 0.6 dps bias에 τ = 1 s면 0.6° 오차가 계속 남는다.
- 그래서 τ는 "움직임을 걸러낼 만큼 길게, bias × τ가 허용 오차보다 작게". 보통 0.5–2 s 근처에서 고른다. bias가 크면 Mahony의 적분항(§7)이 필요하다.

### 6.3 3D로 일반화 — "위" 벡터를 직접 추정

Euler 각으로 complementary를 하면 §4.2의 특이점에 걸린다. 대신 **body 좌표로 본 "위" 방향 단위벡터 `v`**(= 중력 방향, = `Rᵀe_z`)를 추정하면 특이점이 없다. world에 고정된 벡터를 회전하는 body에서 보면

```
v̇ = −ω × v
```

이므로 자이로로 `v`를 굴리고, 정규화한 가속도 `a/|a|` 쪽으로 `(1 − α)`만큼 섞는다. 아래 모듈의 `complementary` 함수가 이것이고, 같은 파일에 gyro 적분만 하는 기준선도 넣는다. **`g3_filters.py`의 앞부분**이다(§7의 두 함수를 같은 파일 뒤에 이어 붙인다).

```python
# g3_filters.py — 자세 추정 필터들 (입력: gyro [rad/s], acc [m/s²], dt). q = body→world
import numpy as np
from g3_quat import qmul, qexp, qnorm, qmat

def gyro_only(gyro, dt, q0):
    q = q0.copy(); out = []
    for w in gyro:
        out.append(q); q = qnorm(qmul(q, qexp(w * dt)))
    return np.array(out)

def complementary(gyro, acc, dt, v0, alpha):            # body 좌표 '위' 방향 벡터 v를 추정
    v = v0.copy(); out = []
    for w, a in zip(gyro, acc):
        out.append(v)
        v = alpha * v + (1 - alpha) * a / np.linalg.norm(a)   # 같은 시각의 측정과 섞고
        v = v - np.cross(w, v) * dt                      # 다음 시각으로: v̇ = −ω × v
        v = v / np.linalg.norm(v)
    return np.array(out)
```

출력 `v`가 바로 **중력 벡터 추정**이다. `g·v`를 가속도에서 빼면 선가속도(움직임만)가 남는다 — B7의 중력 제거를 창 평균보다 정확하게 하는 방법이다. 이 필터는 yaw를 모른다(필요 없다). 결과는 §9에서 다른 필터와 함께 본다.

> 함정: `v`를 섞은 뒤 **정규화**하지 않으면 길이가 1에서 벗어나 다음 스텝의 섞기 비율이 틀어진다. 그리고 "섞기"와 "굴리기"의 시각이 맞아야 한다 — `a_k`는 시각 k의 측정이므로 시각 k의 `v`와 섞은 뒤 `ω_k·dt`로 k+1로 넘어간다(§9.4).

---

## 7. Mahony와 Madgwick — quaternion 위의 비선형 complementary filter

### 7.1 Mahony — 오차를 각속도로 되먹임하는 PI 제어기

Mahony et al.(2008)의 아이디어는 제어 엔지니어에게 아주 익숙하다. 추정 자세가 말하는 "위" 방향 `v̂ = R̂ᵀe_z`와 가속도계가 말하는 "위" 방향 `â = a/|a|`가 다르면, 그 차이를 **회전축 × 각도**로 바꿔서 자이로 값에 더한다.

```
e   = â × v̂                          (두 단위벡터의 외적 = 회전 오차, 크기 ≈ sin(각도))
ω'  = ω_gyro + Kp · e + Ki · ∫e dt    (PI 보정)
q   ← q ⊗ exp(ω' · dt)               (보정된 각속도로 적분)
```

말로 하면: 추정이 틀어진 만큼 "반대로 돌려 주는 가상의 각속도"를 넣는다. P항은 complementary filter와 같은 역할이고, **I항이 자이로 bias를 학습**한다 — 오차가 한 방향으로 계속 쌓이면 적분값이 커져서 결국 `−b`에 수렴한다. Don이 아는 말로 하면 **PLL의 loop filter(PI)**다: 위상 오차(여기서는 자세 오차)를 보고, P는 빠른 추종, I는 주파수 오프셋(여기서는 bias) 제거.

**complementary filter와의 관계**: 1축 작은 각도에서 `θ̂̇ = ω + Kp(θ_acc − θ̂)`를 라플라스 변환하면

```
θ̂ = [Kp / (s + Kp)] · θ_acc  +  [s / (s + Kp)] · (ω / s)
     └── 저역통과, τ = 1/Kp ──┘   └── 고역통과 ──────┘
```

즉 **Mahony의 P항 = 시정수 τ = 1/Kp인 complementary filter**다(Kp = 1 ↔ τ = 1 s ↔ 100 Hz에서 α ≈ 0.99). 거기에 I항이 bias를 없애 주는 것이 차이다.

```python
def mahony(gyro, acc, dt, q0, kp=1.0, ki=0.05, gate=None):
    q = q0.copy(); e_int = np.zeros(3); out = []
    for w, a in zip(gyro, acc):
        out.append(q)
        an = np.linalg.norm(a)
        if gate is None or abs(an - 9.81) < gate:       # |a| ≈ g 일 때만 보정
            v = qmat(q)[2]                               # 추정 '위' (body) = Rᵀ e_z
            e = np.cross(a / an, v)                      # 측정 × 추정 = 회전 오차
            e_int += ki * e * dt
            w = w + kp * e + e_int                       # PI 보정 (e_int가 bias를 먹는다)
        q = qnorm(qmul(q, qexp(w * dt)))
    return np.array(out), -e_int                         # -e_int ≈ gyro bias 추정
```

**gain 고르기**: Kp는 complementary의 1/τ이다(0.5–2 rad/s 근처). Ki는 bias 학습 속도다. PI 루프의 고유 진동수는 대략 `√Ki`, 감쇠비는 `Kp / (2√Ki)`이므로 Kp = 1, Ki = 0.05면 감쇠비 ≈ 2.2(과감쇠, 출렁임 없음), bias 수렴 시간 수십 초다. Ki를 너무 키우면 움직임 중의 가짜 오차(선가속도)까지 bias로 배워 버린다. 공개된 레퍼런스 C 코드(MahonyAHRS.c)의 기본값은 `twoKp = 2·0.5`, `twoKi = 0`이다 — 즉 기본은 P만이다.

### 7.2 Madgwick — 경사하강 한 스텝으로 보정

Madgwick(2010 보고서; 2011 ICORR 논문)은 같은 문제를 **최적화**로 본다. "추정 자세로 예측한 중력 방향"과 "측정 방향"의 차이를 목적함수로 두고, 매 샘플 경사하강을 **딱 한 스텝**만 한다.

```
f(q) = R(q)ᵀ·e_z − â = [ 2(q1·q3 − q0·q2)       − ax ]
                       [ 2(q0·q1 + q2·q3)       − ay ]
                       [ 2(½ − q1² − q2²)       − az ]

J(q) = ∂f/∂q = [ −2q2   2q3  −2q0   2q1 ]
               [  2q1   2q0   2q3   2q2 ]
               [  0    −4q1  −4q2   0   ]

∇ = Jᵀ · f               (½|f|² 의 gradient, A3의 경사하강과 같다)
q̇ = ½ · q ⊗ (0, ω)  −  β · ∇/|∇|
q ← normalize(q + q̇ · dt)
```

말로 하면: 자이로로 자세를 굴리되, 가속도계와 어긋난 방향(gradient)으로 **고정된 속도 β [rad/s]**만큼 끌어당긴다. gradient를 정규화하므로 보정 속도는 오차 크기와 무관하게 β로 일정하다(Mahony는 오차에 비례). 보고서는 β를 "자이로 측정 오차 크기"와 연결해 `β = √(3/4) · ω̃_β`로 설명한다. β가 bias보다 커야 bias로 인한 drift를 따라잡을 수 있지만, 크면 움직임에 흔들린다.

```python
def madgwick(gyro, acc, dt, q0, beta=0.05):            # Madgwick 2010, IMU 버전
    q = q0.copy(); out = []
    for w, a in zip(gyro, acc):
        out.append(q)
        q0_, q1, q2, q3 = q; ax, ay, az = a / np.linalg.norm(a)
        f = np.array([2*(q1*q3 - q0_*q2) - ax, 2*(q0_*q1 + q2*q3) - ay, 2*(0.5 - q1*q1 - q2*q2) - az])
        J = np.array([[-2*q2, 2*q3, -2*q0_, 2*q1], [2*q1, 2*q0_, 2*q3, 2*q2], [0, -4*q1, -4*q2, 0]])
        step = J.T @ f; step /= np.linalg.norm(step) + 1e-12
        qdot = 0.5 * qmul(q, np.r_[0.0, w]) - beta * step
        q = qnorm(q + qdot * dt)
    return np.array(out)
```

> 함정: Madgwick 보고서는 "센서 frame 기준으로 본 지구 frame" 표기를 쓰고, 인터넷의 파생 구현들은 frame 방향·quaternion 성분 순서가 제각각이다. 공식을 옮겨 올 때는 **정지 테스트**(평평하게 놓으면 추정 '위'가 +z, 오른쪽을 들면 roll이 예상 부호로 변하는지)로 반드시 검증한다. 위 함수의 `f`는 `g3_quat.qmat`의 `R`과 같은 관례(body → world)로 맞춰 놓았다.

### 7.3 Mahony vs Madgwick 한눈에

| 항목 | Mahony | Madgwick |
|---|---|---|
| 보정 방식 | 외적 오차 × Kp를 각속도에 더함 (PI) | gradient 방향으로 β만큼 quaternion을 끌어당김 |
| 보정 크기 | 오차에 비례 | 오차와 무관하게 일정 (정규화된 gradient) |
| gyro bias | Ki 적분항이 학습 | IMU 기본판에는 없음 (MARG 확장판에 bias 보상 항이 있다) |
| 튜닝 파라미터 | Kp, Ki | β (하나) |
| 연산량 (6축) | 약 120 명령어 (§11 실측) | 비슷한 자릿수 (gradient 계산이 조금 더) |
| 직관 | PLL / PI 제어 | 경사하강 1스텝 |

둘 다 결국 "**자이로 적분 + 중력·자기장 쪽으로의 작은 되먹임**"이고, 정상상태에서는 1차 complementary filter처럼 동작한다. 실무에서는 팀이 익숙한 쪽 + bias 처리 필요 여부로 고른다.

---

## 8. Kalman filter — 불확실성을 숫자로 들고 다니는 퓨전

### 8.1 직관 — gain을 "고르는" 대신 "계산한다"

complementary와 Mahony에서는 α, Kp, Ki를 사람이 골랐다. Kalman filter(KF)는 같은 구조(예측 → 측정으로 보정)를 쓰되, **각 추정값의 불확실성(공분산 P)**을 같이 추적해서 매 순간 최적의 gain을 계산한다. 자이로 noise가 얼마이고 가속도계 noise가 얼마인지(Q, R)만 알려 주면 된다.

```
예측 (predict):   x ← F·x + B·u           P ← F·P·Fᵀ + Q       (모델로 앞으로 굴리기, 불확실성 증가)
갱신 (update):    y = z − H·x             (innovation: 측정 − 예측된 측정)
                  S = H·P·Hᵀ + R          (innovation의 분산)
                  K = P·Hᵀ·S⁻¹            (칼만 이득: 예측과 측정 중 누구를 얼마나 믿을지)
                  x ← x + K·y             P ← (I − K·H)·P     (보정, 불확실성 감소)
```

말로 하면: 예측이 불확실할수록(P 큼) 측정을 더 믿고(K 큼), 측정이 noisy할수록(R 큼) 예측을 더 믿는다. 선형·Gaussian 가정에서 이것이 평균제곱오차를 최소화하는 추정기다.

**스칼라 손계산**: 예측 분산 P = 1.0, 과정잡음 Q = 0.01, 측정잡음 R = 4.

```
P⁻ = 1.0 + 0.01 = 1.01
K  = 1.01 / (1.01 + 4) = 0.2016     ← 측정을 약 20% 반영
P  = (1 − 0.2016) · 1.01 = 0.806     ← 불확실성이 줄었다
```

이 과정을 반복하면 P와 K는 일정한 값으로 수렴한다(정상상태). 정상상태 KF는 **고정 gain 필터 = complementary filter**와 같은 모양이 된다. 다음 예제에서 숫자로 확인한다.

### 8.2 1축 KF — 상태 [각도, gyro bias]

한 축(roll)만 생각한다. 상태는 각도 θ와 자이로 bias b 두 개.

```
상태:   x = [θ, b]ᵀ
모델:   θ_k+1 = θ_k + (ω_meas − b_k) · dt        (bias를 뺀 자이로로 적분)
        b_k+1 = b_k                               (bias는 천천히 변하는 상수 + 작은 random walk)

F = [ 1  −dt ]     B·u = [ ω_meas · dt ]     Q = diag(σ_θ²·dt², σ_b²·dt)
    [ 0   1  ]           [ 0           ]

측정:   z = θ_acc = θ + noise        H = [1  0],   R = σ_acc²
```

말로 하면: bias는 **직접 측정되지 않지만**, F의 `−dt` 항 때문에 각도 예측에 영향을 주고, 가속도계 각도와의 차이(innovation)가 계속 한쪽으로 쏠리면 KF가 그것을 bias 탓으로 돌린다 — Mahony의 I항이 하는 일을 공분산이 자동으로 해 준다.

```python
# 1축 Kalman filter: 상태 x = [각도 θ, gyro bias b], 측정 = 가속도계 각도
import numpy as np
rng = np.random.default_rng(5); dt = 0.01; t = np.arange(0, 60, dt); N = len(t)
theta = np.deg2rad(30) * np.sin(2*np.pi*0.2*t) * (t > 5)            # 정답 roll
rate = np.gradient(theta, dt)
b_true = np.deg2rad(1.0)                                             # 1 dps bias
gyro = rate + b_true + rng.normal(0, np.deg2rad(0.05), N)
z_acc = theta + rng.normal(0, np.deg2rad(1.0), N)                    # 가속도계 각도 (σ 1°)
z_acc[(t > 20) & (t < 26)] += np.deg2rad(15) * np.sin(2*np.pi*2*t[(t > 20) & (t < 26)])  # 흔들기

F = np.array([[1, -dt], [0, 1]]); H = np.array([[1.0, 0]])
Q = np.diag([np.deg2rad(0.5)**2 * dt**2, np.deg2rad(0.01)**2 * dt])   # 과정 잡음 (넉넉히)
Rm = np.deg2rad(2.0)**2                                              # 측정 잡음 (조금 크게)
x = np.zeros(2); P = np.diag([np.deg2rad(10)**2, np.deg2rad(2)**2])
est, bias = np.zeros(N), np.zeros(N)
for k in range(N):
    x = np.array([x[0] + (gyro[k] - x[1]) * dt, x[1]])              # 예측
    P = F @ P @ F.T + Q
    S = H @ P @ H.T + Rm; K = P @ H.T / S                            # 칼만 이득 (2×1)
    x = x + (K * (z_acc[k] - x[0])).ravel(); P = (np.eye(2) - K @ H) @ P   # 갱신
    est[k], bias[k] = x
comp = np.zeros(N); c = 0.0
for k in range(N):
    c = 0.98 * (c + gyro[k] * dt) + 0.02 * z_acc[k]; comp[k] = c
rms = lambda e: np.rad2deg(np.sqrt(np.mean(e[t > 10]**2)))
print(f"RMS[deg] gyro만 {rms(np.cumsum(gyro)*dt - theta):6.2f} | accel만 {rms(z_acc - theta):5.2f} | "
      f"compl. {rms(comp - theta):5.2f} | KF {rms(est - theta):5.2f}")
for s in [1, 5, 10, 30, 59.99]:
    print(f"t={s:5.2f}s  bias 추정 {np.rad2deg(bias[int(s/dt)]):6.3f} dps (참값 1.000)")
print("정상상태 칼만 이득 K =", K.ravel().round(5))
np.save("kf1d.npy", np.vstack([t, theta, est, comp, z_acc, bias]))
```

```text
RMS[deg] gyro만  37.92 | accel만  3.83 | compl.  0.82 | KF  0.23
t= 1.00s  bias 추정  0.795 dps (참값 1.000)
t= 5.00s  bias 추정  1.004 dps (참값 1.000)
t=10.00s  bias 추정  1.015 dps (참값 1.000)
t=30.00s  bias 추정  1.016 dps (참값 1.000)
t=59.99s  bias 추정  1.001 dps (참값 1.000)
정상상태 칼만 이득 K = [ 0.00402 -0.0005 ]
```

출력에서 볼 것: (1) 1 dps bias 때문에 자이로만 적분하면 RMS 38°, 가속도계만 쓰면 흔들기 때문에 3.8°, complementary(α = 0.98)는 0.82°, KF는 **0.23°**. (2) bias 추정이 5초 만에 1.004 dps에 도달한다(정지 구간에서도 가속도계 각도가 고정되어 있으니 자이로 적분이 흘러가는 속도 = bias를 바로 알 수 있다). (3) 정상상태 이득 `K = [0.00402, −0.0005]`. 첫 성분은 complementary의 `1 − α`에 해당한다 — `α ≈ 0.996`, `τ ≈ 0.01 · 0.996/0.004 ≈ 2.5 s`. 둘째 성분은 innovation을 bias로 적분하는 gain이다. 즉 **정상상태 KF = complementary filter + bias 적분기 = Mahony의 PI 구조**다. 세 필터가 한 가족이라는 것이 이 노트의 핵심 메시지 중 하나다.

```svg
<svg viewBox="0 0 640 380" xmlns="http://www.w3.org/2000/svg">
  <line x1="80.0" y1="190.0" x2="620.0" y2="190.0" stroke="currentColor" stroke-width="1"/> <line x1="80.0" y1="30.0" x2="80.0" y2="190.0" stroke="currentColor" stroke-width="1"/> <line x1="75.0" y1="170.0" x2="80.0" y2="170.0" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="174.0" font-size="12" text-anchor="end">-30°</text> <line x1="75.0" y1="110.0" x2="80.0" y2="110.0" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="114.0" font-size="12" text-anchor="end">0°</text> <line x1="75.0" y1="50.0" x2="80.0" y2="50.0" stroke="currentColor" stroke-width="1"/>
  <text x="70.0" y="54.0" font-size="12" text-anchor="end">30°</text> <rect x="440.0" y="30" width="108.0" height="330" fill="#888" fill-opacity="0.15"/>
  <polyline fill="none" stroke="#e08a3c" stroke-width="0.8" points="80.0,115.6 80.7,112.0 81.4,108.0 82.2,110.7 82.9,109.0 83.6,111.8 84.3,109.0 85.0,110.5 85.8,108.4 86.5,107.9 87.2,111.6 87.9,111.6 88.6,108.5 89.4,109.5 90.1,111.2 90.8,108.7 91.5,109.0 92.2,106.6 93.0,110.2 93.7,107.5 94.4,110.1 95.1,112.2 95.8,108.1 96.6,112.4 97.3,106.1 98.0,108.9 98.7,110.4 99.4,109.6 100.2,109.5 100.9,112.1 101.6,108.3 102.3,112.7 103.0,111.6 103.8,106.1 104.5,108.7 105.2,106.4 105.9,111.9 106.6,110.4 107.4,109.6 108.1,110.7 108.8,109.7 109.5,110.7 110.2,107.9 111.0,110.5 111.7,108.8 112.4,111.5 113.1,109.1 113.8,108.5 114.6,111.0 115.3,108.7 116.0,111.7 116.7,110.7 117.4,111.4 118.2,111.7 118.9,111.2 119.6,112.1 120.3,112.5 121.0,109.3 121.8,109.4 122.5,109.6 123.2,114.4 123.9,107.5 124.6,116.6 125.4,107.0 126.1,108.0 126.8,112.3 127.5,112.7 128.2,110.2 129.0,110.4 129.7,107.1 130.4,110.7 131.1,107.5 131.8,110.3 132.6,109.5 133.3,113.6 134.0,114.3 134.7,108.8 135.4,111.3 136.2,107.7 136.9,111.8 137.6,108.3 138.3,112.4 139.0,106.5 139.8,105.8 140.5,109.6 141.2,112.6 141.9,109.5 142.6,112.0 143.4,109.2 144.1,109.8 144.8,110.2 145.5,110.7 146.2,108.1 147.0,115.4 147.7,111.4 148.4,108.4 149.1,110.1 149.8,108.2 150.6,109.7 151.3,108.7 152.0,108.6 152.7,111.9 153.4,109.1 154.2,106.7 154.9,106.9 155.6,113.4 156.3,110.5 157.0,109.9 157.8,106.3 158.5,109.0 159.2,107.5 159.9,108.7 160.6,112.1 161.4,109.2 162.1,112.3 162.8,108.4 163.5,107.6 164.2,110.0 165.0,109.3 165.7,106.5 166.4,107.5 167.1,110.2 167.8,111.1 168.6,111.1 169.3,110.8 170.0,108.1 170.7,106.4 171.4,104.2 172.2,103.1 172.9,94.6 173.6,98.5 174.3,92.6 175.0,87.7 175.8,89.2 176.5,85.9 177.2,86.0 177.9,78.6 178.6,74.9 179.4,73.4 180.1,72.1 180.8,70.5 181.5,66.6 182.2,63.8 183.0,61.8 183.7,62.4 184.4,59.0 185.1,56.0 185.8,58.0 186.6,56.3 187.3,55.6 188.0,53.9 188.7,52.7 189.4,51.6 190.2,54.6 190.9,51.0 191.6,50.3 192.3,51.4 193.0,48.6 193.8,48.0 194.5,47.4 195.2,50.7 195.9,52.6 196.6,52.5 197.4,56.3 198.1,52.6 198.8,57.6 199.5,58.1 200.2,59.7 201.0,61.3 201.7,63.0 202.4,63.9 203.1,64.4 203.8,65.3 204.6,73.4 205.3,73.4 206.0,74.2 206.7,76.2 207.4,80.3 208.2,81.7 208.9,87.5 209.6,89.6 210.3,89.5 211.0,93.7 211.8,92.9 212.5,98.5 213.2,103.9 213.9,102.9 214.6,110.4 215.4,111.7 216.1,114.7 216.8,119.0 217.5,120.5 218.2,123.6 219.0,125.8 219.7,128.7 220.4,132.9 221.1,135.6 221.8,137.2 222.6,142.1 223.3,138.8 224.0,145.1 224.7,147.4 225.4,151.7 226.2,154.1 226.9,151.4 227.6,160.1 228.3,157.5 229.0,159.6 229.8,160.0 230.5,161.2 231.2,164.9 231.9,165.9 232.6,165.0 233.4,167.1 234.1,164.7 234.8,171.1 235.5,172.1 236.2,167.8 237.0,169.5 237.7,169.7 238.4,172.0 239.1,171.0 239.8,168.5 240.6,170.9 241.3,168.6 242.0,166.5 242.7,168.1 243.4,165.7 244.2,162.5 244.9,162.2 245.6,166.4 246.3,158.0 247.0,159.2 247.8,153.2 248.5,155.9 249.2,151.8 249.9,151.4 250.6,145.3 251.4,139.8 252.1,140.4 252.8,141.0 253.5,137.6 254.2,133.7 255.0,133.3 255.7,126.8 256.4,127.3 257.1,120.4 257.8,119.0 258.6,117.1 259.3,115.7 260.0,109.5 260.7,102.8 261.4,106.1 262.2,99.0 262.9,103.0 263.6,93.5 264.3,92.5 265.0,88.8 265.8,84.0 266.5,82.5 267.2,79.8 267.9,79.3 268.6,74.1 269.4,75.2 270.1,71.4 270.8,68.2 271.5,66.3 272.2,65.2 273.0,62.9 273.7,59.7 274.4,60.3 275.1,61.0 275.8,53.4 276.6,53.0 277.3,52.0 278.0,53.0 278.7,50.0 279.4,53.5 280.2,47.9 280.9,51.6 281.6,48.3 282.3,49.6 283.0,49.7 283.8,50.4 284.5,54.5 285.2,49.5 285.9,49.5 286.6,55.3 287.4,55.5 288.1,54.9 288.8,58.2 289.5,58.2 290.2,55.1 291.0,57.8 291.7,64.3 292.4,65.3 293.1,66.0 293.8,67.2 294.6,70.5 295.3,70.9 296.0,75.0 296.7,79.1 297.4,78.2 298.2,84.0 298.9,84.1 299.6,89.2 300.3,88.2 301.0,95.2 301.8,95.7 302.5,100.1 303.2,103.7 303.9,103.9 304.6,107.6 305.4,112.9 306.1,116.2 306.8,119.4 307.5,119.9 308.2,122.7 309.0,124.1 309.7,132.6 310.4,134.1 311.1,130.1 311.8,140.7 312.6,143.3 313.3,141.3 314.0,147.5 314.7,146.4 315.4,150.2 316.2,154.0 316.9,157.0 317.6,156.8 318.3,158.1 319.0,156.6 319.8,158.6 320.5,162.3 321.2,165.7 321.9,163.4 322.6,165.9 323.4,165.7 324.1,168.4 324.8,168.9 325.5,167.0 326.2,170.3 327.0,170.6 327.7,169.1 328.4,168.9 329.1,171.9 329.8,170.9 330.6,170.2 331.3,164.6 332.0,165.4 332.7,170.1 333.4,167.0 334.2,165.9 334.9,162.1 335.6,159.6 336.3,156.1 337.0,158.5 337.8,157.7 338.5,153.8 339.2,149.6 339.9,149.1 340.6,150.8 341.4,144.2 342.1,141.2 342.8,139.7 343.5,135.3 344.2,133.2 345.0,128.2 345.7,129.2 346.4,126.5 347.1,118.0 347.8,119.6 348.6,114.0 349.3,114.4 350.0,110.7 350.7,104.9 351.4,101.2 352.2,100.6 352.9,98.6 353.6,96.2 354.3,91.8 355.0,91.9 355.8,88.6 356.5,84.2 357.2,81.0 357.9,79.3 358.6,78.6 359.4,71.9 360.1,72.9 360.8,68.1 361.5,68.3 362.2,60.6 363.0,62.9 363.7,59.0 364.4,58.0 365.1,59.7 365.8,58.6 366.6,57.5 367.3,56.2 368.0,52.6 368.7,50.0 369.4,50.9 370.2,48.4 370.9,46.4 371.6,47.4 372.3,45.7 373.0,47.0 373.8,48.5 374.5,47.8 375.2,55.0 375.9,48.9 376.6,51.3 377.4,51.4 378.1,50.2 378.8,55.9 379.5,56.6 380.2,56.5 381.0,58.7 381.7,65.4 382.4,66.3 383.1,61.9 383.8,62.2 384.6,69.9 385.3,73.7 386.0,77.5 386.7,75.5 387.4,77.6 388.2,80.9 388.9,84.0 389.6,88.7 390.3,90.0 391.0,94.6 391.8,95.8 392.5,103.2 393.2,104.0 393.9,108.0 394.6,106.2 395.4,115.2 396.1,114.4 396.8,115.7 397.5,120.8 398.2,122.6 399.0,126.3 399.7,130.2 400.4,132.9 401.1,135.1 401.8,134.8 402.6,141.4 403.3,144.8 404.0,144.5 404.7,149.9 405.4,150.7 406.2,153.1 406.9,152.1 407.6,154.6 408.3,158.8 409.0,155.8 409.8,162.8 410.5,163.0 411.2,165.9 411.9,166.1 412.6,166.2 413.4,165.1 414.1,166.6 414.8,169.3 415.5,167.9 416.2,168.0 417.0,171.2 417.7,169.3 418.4,170.1 419.1,170.9 419.8,169.3 420.6,169.6 421.3,168.0 422.0,166.0 422.7,165.0 423.4,168.7 424.2,163.3 424.9,159.2 425.6,158.7 426.3,162.1 427.0,159.0 427.8,155.4 428.5,150.3 429.2,148.9 429.9,148.5 430.6,145.7 431.4,145.4 432.1,138.6 432.8,135.7 433.5,133.4 434.2,134.8 435.0,132.1 435.7,128.6 436.4,123.4 437.1,121.2 437.8,119.7 438.6,112.2 439.3,110.6 440.0,112.0 440.7,92.0 441.4,78.4 442.2,72.0 442.9,70.1 443.6,81.2 444.3,86.8 445.0,101.2 445.8,106.8 446.5,113.7 447.2,110.3 447.9,98.8 448.6,82.4 449.4,63.8 450.1,53.1 450.8,41.4 451.5,39.1 452.2,40.9 453.0,50.8 453.7,63.9 454.4,77.8 455.1,88.2 455.8,84.1 456.6,81.2 457.3,68.8 458.0,53.7 458.7,37.3 459.4,22.1 460.2,19.3 460.9,23.8 461.6,34.0 462.3,46.3 463.0,60.9 463.8,73.8 464.5,80.3 465.2,84.9 465.9,73.5 466.6,58.9 467.4,44.4 468.1,33.6 468.8,27.7 469.5,26.4 470.2,38.5 471.0,50.7 471.7,66.1 472.4,82.3 473.1,91.1 473.8,98.4 474.6,97.0 475.3,84.1 476.0,70.1 476.7,64.2 477.4,52.7 478.2,54.8 478.9,56.5 479.6,70.3 480.3,86.9 481.0,103.3 481.8,119.1 482.5,128.7 483.2,130.4 483.9,128.6 484.6,115.9 485.4,105.8 486.1,93.8 486.8,91.9 487.5,93.7 488.2,104.1 489.0,110.3 489.7,136.2 490.4,149.5 491.1,164.4 491.8,167.0 492.6,165.4 493.3,159.0 494.0,141.0 494.7,137.0 495.4,124.9 496.2,119.9 496.9,129.2 497.6,141.6 498.3,153.9 499.0,172.1 499.8,183.3 500.5,194.0 501.2,192.9 501.9,185.1 502.6,170.6 503.4,160.4 504.1,148.7 504.8,141.4 505.5,144.2 506.2,146.3 507.0,158.0 507.7,171.0 508.4,186.4 509.1,197.0 509.8,197.9 510.6,193.3 511.3,184.9 512.0,165.0 512.7,154.5 513.4,139.7 514.2,135.4 514.9,134.4 515.6,144.0 516.3,155.1 517.0,168.4 517.8,177.4 518.5,181.5 519.2,176.2 519.9,166.5 520.6,153.2 521.4,136.9 522.1,120.2 522.8,111.9 523.5,106.0 524.2,109.7 525.0,120.8 525.7,133.2 526.4,142.1 527.1,147.6 527.8,153.1 528.6,141.6 529.3,128.1 530.0,111.0 530.7,93.5 531.4,77.4 532.2,66.7 532.9,72.7 533.6,77.9 534.3,88.1 535.0,102.7 535.8,108.8 536.5,116.1 537.2,107.1 537.9,96.9 538.6,83.6 539.4,67.5 540.1,52.6 540.8,42.7 541.5,35.6 542.2,40.2 543.0,54.2 543.7,65.9 544.4,77.0 545.1,85.4 545.8,86.0 546.6,77.5 547.3,66.8 548.0,50.6 548.7,53.8 549.4,51.6 550.2,51.3 550.9,50.9 551.6,49.1 552.3,48.1 553.0,50.8 553.8,48.8 554.5,49.1 555.2,51.3 555.9,49.3 556.6,54.5 557.4,53.1 558.1,51.5 558.8,54.0 559.5,55.9 560.2,58.0 561.0,61.8 561.7,61.0 562.4,65.7 563.1,64.2 563.8,70.8 564.6,70.9 565.3,73.7 566.0,74.3 566.7,76.9 567.4,75.8 568.2,81.9 568.9,86.5 569.6,89.3 570.3,89.0 571.0,92.5 571.8,96.1 572.5,100.3 573.2,102.6 573.9,106.7 574.6,107.9 575.4,113.0 576.1,116.0 576.8,117.6 577.5,117.4 578.2,124.5 579.0,127.7 579.7,127.5 580.4,131.2 581.1,136.6 581.8,141.8 582.6,139.0 583.3,142.8 584.0,145.7 584.7,144.6 585.4,151.6 586.2,150.9 586.9,154.7 587.6,156.8 588.3,157.3 589.0,162.7 589.8,158.5 590.5,163.9 591.2,164.3 591.9,166.1 592.6,164.1 593.4,164.1 594.1,167.3 594.8,166.9 595.5,167.0 596.2,167.5 597.0,168.6 597.7,175.5 598.4,169.0 599.1,170.2 599.8,170.1 600.6,168.2 601.3,167.0 602.0,168.7 602.7,161.9 603.4,166.6 604.2,163.9 604.9,162.6 605.6,161.4 606.3,159.0 607.0,156.8 607.8,158.4 608.5,154.1 609.2,152.6 609.9,149.4 610.6,144.8 611.4,143.3 612.1,140.9 612.8,138.2 613.5,133.7 614.2,131.6 615.0,130.8 615.7,126.3 616.4,125.3 617.1,119.6 617.8,119.4 618.6,116.4 619.3,115.4"/>
  <polyline fill="none" stroke="#888" stroke-width="2.5" points="80.0,110.0 80.7,110.0 81.4,110.0 82.2,110.0 82.9,110.0 83.6,110.0 84.3,110.0 85.0,110.0 85.8,110.0 86.5,110.0 87.2,110.0 87.9,110.0 88.6,110.0 89.4,110.0 90.1,110.0 90.8,110.0 91.5,110.0 92.2,110.0 93.0,110.0 93.7,110.0 94.4,110.0 95.1,110.0 95.8,110.0 96.6,110.0 97.3,110.0 98.0,110.0 98.7,110.0 99.4,110.0 100.2,110.0 100.9,110.0 101.6,110.0 102.3,110.0 103.0,110.0 103.8,110.0 104.5,110.0 105.2,110.0 105.9,110.0 106.6,110.0 107.4,110.0 108.1,110.0 108.8,110.0 109.5,110.0 110.2,110.0 111.0,110.0 111.7,110.0 112.4,110.0 113.1,110.0 113.8,110.0 114.6,110.0 115.3,110.0 116.0,110.0 116.7,110.0 117.4,110.0 118.2,110.0 118.9,110.0 119.6,110.0 120.3,110.0 121.0,110.0 121.8,110.0 122.5,110.0 123.2,110.0 123.9,110.0 124.6,110.0 125.4,110.0 126.1,110.0 126.8,110.0 127.5,110.0 128.2,110.0 129.0,110.0 129.7,110.0 130.4,110.0 131.1,110.0 131.8,110.0 132.6,110.0 133.3,110.0 134.0,110.0 134.7,110.0 135.4,110.0 136.2,110.0 136.9,110.0 137.6,110.0 138.3,110.0 139.0,110.0 139.8,110.0 140.5,110.0 141.2,110.0 141.9,110.0 142.6,110.0 143.4,110.0 144.1,110.0 144.8,110.0 145.5,110.0 146.2,110.0 147.0,110.0 147.7,110.0 148.4,110.0 149.1,110.0 149.8,110.0 150.6,110.0 151.3,110.0 152.0,110.0 152.7,110.0 153.4,110.0 154.2,110.0 154.9,110.0 155.6,110.0 156.3,110.0 157.0,110.0 157.8,110.0 158.5,110.0 159.2,110.0 159.9,110.0 160.6,110.0 161.4,110.0 162.1,110.0 162.8,110.0 163.5,110.0 164.2,110.0 165.0,110.0 165.7,110.0 166.4,110.0 167.1,110.0 167.8,110.0 168.6,110.0 169.3,110.0 170.0,110.0 170.7,107.0 171.4,104.0 172.2,101.0 172.9,98.0 173.6,95.1 174.3,92.2 175.0,89.3 175.8,86.5 176.5,83.8 177.2,81.1 177.9,78.5 178.6,76.0 179.4,73.5 180.1,71.2 180.8,68.9 181.5,66.8 182.2,64.7 183.0,62.8 183.7,61.0 184.4,59.3 185.1,57.8 185.8,56.4 186.6,55.1 187.3,53.9 188.0,52.9 188.7,52.1 189.4,51.4 190.2,50.8 190.9,50.4 191.6,50.1 192.3,50.0 193.0,50.0 193.8,50.2 194.5,50.6 195.2,51.1 195.9,51.7 196.6,52.5 197.4,53.4 198.1,54.5 198.8,55.7 199.5,57.1 200.2,58.5 201.0,60.2 201.7,61.9 202.4,63.8 203.1,65.7 203.8,67.8 204.6,70.0 205.3,72.3 206.0,74.7 206.7,77.2 207.4,79.8 208.2,82.4 208.9,85.1 209.6,87.9 210.3,90.7 211.0,93.6 211.8,96.5 212.5,99.5 213.2,102.5 213.9,105.5 214.6,108.5 215.4,111.5 216.1,114.5 216.8,117.5 217.5,120.5 218.2,123.5 219.0,126.4 219.7,129.3 220.4,132.1 221.1,134.9 221.8,137.6 222.6,140.2 223.3,142.8 224.0,145.3 224.7,147.7 225.4,150.0 226.2,152.2 226.9,154.3 227.6,156.2 228.3,158.1 229.0,159.8 229.8,161.5 230.5,162.9 231.2,164.3 231.9,165.5 232.6,166.6 233.4,167.5 234.1,168.3 234.8,168.9 235.5,169.4 236.2,169.8 237.0,170.0 237.7,170.0 238.4,169.9 239.1,169.6 239.8,169.2 240.6,168.6 241.3,167.9 242.0,167.1 242.7,166.1 243.4,164.9 244.2,163.6 244.9,162.2 245.6,160.7 246.3,159.0 247.0,157.2 247.8,155.3 248.5,153.2 249.2,151.1 249.9,148.8 250.6,146.5 251.4,144.0 252.1,141.5 252.8,138.9 253.5,136.2 254.2,133.5 255.0,130.7 255.7,127.8 256.4,124.9 257.1,122.0 257.8,119.0 258.6,116.0 259.3,113.0 260.0,110.0 260.7,107.0 261.4,104.0 262.2,101.0 262.9,98.0 263.6,95.1 264.3,92.2 265.0,89.3 265.8,86.5 266.5,83.8 267.2,81.1 267.9,78.5 268.6,76.0 269.4,73.5 270.1,71.2 270.8,68.9 271.5,66.8 272.2,64.7 273.0,62.8 273.7,61.0 274.4,59.3 275.1,57.8 275.8,56.4 276.6,55.1 277.3,53.9 278.0,52.9 278.7,52.1 279.4,51.4 280.2,50.8 280.9,50.4 281.6,50.1 282.3,50.0 283.0,50.0 283.8,50.2 284.5,50.6 285.2,51.1 285.9,51.7 286.6,52.5 287.4,53.4 288.1,54.5 288.8,55.7 289.5,57.1 290.2,58.5 291.0,60.2 291.7,61.9 292.4,63.8 293.1,65.7 293.8,67.8 294.6,70.0 295.3,72.3 296.0,74.7 296.7,77.2 297.4,79.8 298.2,82.4 298.9,85.1 299.6,87.9 300.3,90.7 301.0,93.6 301.8,96.5 302.5,99.5 303.2,102.5 303.9,105.5 304.6,108.5 305.4,111.5 306.1,114.5 306.8,117.5 307.5,120.5 308.2,123.5 309.0,126.4 309.7,129.3 310.4,132.1 311.1,134.9 311.8,137.6 312.6,140.2 313.3,142.8 314.0,145.3 314.7,147.7 315.4,150.0 316.2,152.2 316.9,154.3 317.6,156.2 318.3,158.1 319.0,159.8 319.8,161.5 320.5,162.9 321.2,164.3 321.9,165.5 322.6,166.6 323.4,167.5 324.1,168.3 324.8,168.9 325.5,169.4 326.2,169.8 327.0,170.0 327.7,170.0 328.4,169.9 329.1,169.6 329.8,169.2 330.6,168.6 331.3,167.9 332.0,167.1 332.7,166.1 333.4,164.9 334.2,163.6 334.9,162.2 335.6,160.7 336.3,159.0 337.0,157.2 337.8,155.3 338.5,153.2 339.2,151.1 339.9,148.8 340.6,146.5 341.4,144.0 342.1,141.5 342.8,138.9 343.5,136.2 344.2,133.5 345.0,130.7 345.7,127.8 346.4,124.9 347.1,122.0 347.8,119.0 348.6,116.0 349.3,113.0 350.0,110.0 350.7,107.0 351.4,104.0 352.2,101.0 352.9,98.0 353.6,95.1 354.3,92.2 355.0,89.3 355.8,86.5 356.5,83.8 357.2,81.1 357.9,78.5 358.6,76.0 359.4,73.5 360.1,71.2 360.8,68.9 361.5,66.8 362.2,64.7 363.0,62.8 363.7,61.0 364.4,59.3 365.1,57.8 365.8,56.4 366.6,55.1 367.3,53.9 368.0,52.9 368.7,52.1 369.4,51.4 370.2,50.8 370.9,50.4 371.6,50.1 372.3,50.0 373.0,50.0 373.8,50.2 374.5,50.6 375.2,51.1 375.9,51.7 376.6,52.5 377.4,53.4 378.1,54.5 378.8,55.7 379.5,57.1 380.2,58.5 381.0,60.2 381.7,61.9 382.4,63.8 383.1,65.7 383.8,67.8 384.6,70.0 385.3,72.3 386.0,74.7 386.7,77.2 387.4,79.8 388.2,82.4 388.9,85.1 389.6,87.9 390.3,90.7 391.0,93.6 391.8,96.5 392.5,99.5 393.2,102.5 393.9,105.5 394.6,108.5 395.4,111.5 396.1,114.5 396.8,117.5 397.5,120.5 398.2,123.5 399.0,126.4 399.7,129.3 400.4,132.1 401.1,134.9 401.8,137.6 402.6,140.2 403.3,142.8 404.0,145.3 404.7,147.7 405.4,150.0 406.2,152.2 406.9,154.3 407.6,156.2 408.3,158.1 409.0,159.8 409.8,161.5 410.5,162.9 411.2,164.3 411.9,165.5 412.6,166.6 413.4,167.5 414.1,168.3 414.8,168.9 415.5,169.4 416.2,169.8 417.0,170.0 417.7,170.0 418.4,169.9 419.1,169.6 419.8,169.2 420.6,168.6 421.3,167.9 422.0,167.1 422.7,166.1 423.4,164.9 424.2,163.6 424.9,162.2 425.6,160.7 426.3,159.0 427.0,157.2 427.8,155.3 428.5,153.2 429.2,151.1 429.9,148.8 430.6,146.5 431.4,144.0 432.1,141.5 432.8,138.9 433.5,136.2 434.2,133.5 435.0,130.7 435.7,127.8 436.4,124.9 437.1,122.0 437.8,119.0 438.6,116.0 439.3,113.0 440.0,110.0 440.7,107.0 441.4,104.0 442.2,101.0 442.9,98.0 443.6,95.1 444.3,92.2 445.0,89.3 445.8,86.5 446.5,83.8 447.2,81.1 447.9,78.5 448.6,76.0 449.4,73.5 450.1,71.2 450.8,68.9 451.5,66.8 452.2,64.7 453.0,62.8 453.7,61.0 454.4,59.3 455.1,57.8 455.8,56.4 456.6,55.1 457.3,53.9 458.0,52.9 458.7,52.1 459.4,51.4 460.2,50.8 460.9,50.4 461.6,50.1 462.3,50.0 463.0,50.0 463.8,50.2 464.5,50.6 465.2,51.1 465.9,51.7 466.6,52.5 467.4,53.4 468.1,54.5 468.8,55.7 469.5,57.1 470.2,58.5 471.0,60.2 471.7,61.9 472.4,63.8 473.1,65.7 473.8,67.8 474.6,70.0 475.3,72.3 476.0,74.7 476.7,77.2 477.4,79.8 478.2,82.4 478.9,85.1 479.6,87.9 480.3,90.7 481.0,93.6 481.8,96.5 482.5,99.5 483.2,102.5 483.9,105.5 484.6,108.5 485.4,111.5 486.1,114.5 486.8,117.5 487.5,120.5 488.2,123.5 489.0,126.4 489.7,129.3 490.4,132.1 491.1,134.9 491.8,137.6 492.6,140.2 493.3,142.8 494.0,145.3 494.7,147.7 495.4,150.0 496.2,152.2 496.9,154.3 497.6,156.2 498.3,158.1 499.0,159.8 499.8,161.5 500.5,162.9 501.2,164.3 501.9,165.5 502.6,166.6 503.4,167.5 504.1,168.3 504.8,168.9 505.5,169.4 506.2,169.8 507.0,170.0 507.7,170.0 508.4,169.9 509.1,169.6 509.8,169.2 510.6,168.6 511.3,167.9 512.0,167.1 512.7,166.1 513.4,164.9 514.2,163.6 514.9,162.2 515.6,160.7 516.3,159.0 517.0,157.2 517.8,155.3 518.5,153.2 519.2,151.1 519.9,148.8 520.6,146.5 521.4,144.0 522.1,141.5 522.8,138.9 523.5,136.2 524.2,133.5 525.0,130.7 525.7,127.8 526.4,124.9 527.1,122.0 527.8,119.0 528.6,116.0 529.3,113.0 530.0,110.0 530.7,107.0 531.4,104.0 532.2,101.0 532.9,98.0 533.6,95.1 534.3,92.2 535.0,89.3 535.8,86.5 536.5,83.8 537.2,81.1 537.9,78.5 538.6,76.0 539.4,73.5 540.1,71.2 540.8,68.9 541.5,66.8 542.2,64.7 543.0,62.8 543.7,61.0 544.4,59.3 545.1,57.8 545.8,56.4 546.6,55.1 547.3,53.9 548.0,52.9 548.7,52.1 549.4,51.4 550.2,50.8 550.9,50.4 551.6,50.1 552.3,50.0 553.0,50.0 553.8,50.2 554.5,50.6 555.2,51.1 555.9,51.7 556.6,52.5 557.4,53.4 558.1,54.5 558.8,55.7 559.5,57.1 560.2,58.5 561.0,60.2 561.7,61.9 562.4,63.8 563.1,65.7 563.8,67.8 564.6,70.0 565.3,72.3 566.0,74.7 566.7,77.2 567.4,79.8 568.2,82.4 568.9,85.1 569.6,87.9 570.3,90.7 571.0,93.6 571.8,96.5 572.5,99.5 573.2,102.5 573.9,105.5 574.6,108.5 575.4,111.5 576.1,114.5 576.8,117.5 577.5,120.5 578.2,123.5 579.0,126.4 579.7,129.3 580.4,132.1 581.1,134.9 581.8,137.6 582.6,140.2 583.3,142.8 584.0,145.3 584.7,147.7 585.4,150.0 586.2,152.2 586.9,154.3 587.6,156.2 588.3,158.1 589.0,159.8 589.8,161.5 590.5,162.9 591.2,164.3 591.9,165.5 592.6,166.6 593.4,167.5 594.1,168.3 594.8,168.9 595.5,169.4 596.2,169.8 597.0,170.0 597.7,170.0 598.4,169.9 599.1,169.6 599.8,169.2 600.6,168.6 601.3,167.9 602.0,167.1 602.7,166.1 603.4,164.9 604.2,163.6 604.9,162.2 605.6,160.7 606.3,159.0 607.0,157.2 607.8,155.3 608.5,153.2 609.2,151.1 609.9,148.8 610.6,146.5 611.4,144.0 612.1,141.5 612.8,138.9 613.5,136.2 614.2,133.5 615.0,130.7 615.7,127.8 616.4,124.9 617.1,122.0 617.8,119.0 618.6,116.0 619.3,113.0"/>
  <polyline fill="none" stroke="#4a7bd0" stroke-width="1.5" points="80.0,115.4 80.7,110.5 81.4,109.7 82.2,109.6 82.9,109.7 83.6,109.6 84.3,109.8 85.0,109.9 85.8,109.6 86.5,109.6 87.2,109.4 87.9,109.6 88.6,109.5 89.4,109.4 90.1,109.5 90.8,109.4 91.5,109.6 92.2,109.6 93.0,109.6 93.7,109.3 94.4,109.4 95.1,109.4 95.8,109.5 96.6,109.5 97.3,109.4 98.0,109.6 98.7,109.9 99.4,110.0 100.2,110.0 100.9,110.0 101.6,109.8 102.3,110.0 103.0,110.1 103.8,110.0 104.5,109.8 105.2,109.8 105.9,109.8 106.6,109.9 107.4,109.8 108.1,109.9 108.8,110.0 109.5,110.0 110.2,109.9 111.0,109.9 111.7,110.0 112.4,110.0 113.1,110.0 113.8,109.9 114.6,110.0 115.3,110.1 116.0,110.1 116.7,110.1 117.4,110.2 118.2,110.4 118.9,110.3 119.6,110.3 120.3,110.4 121.0,110.4 121.8,110.4 122.5,110.3 123.2,110.4 123.9,110.3 124.6,110.5 125.4,110.4 126.1,110.4 126.8,110.4 127.5,110.4 128.2,110.4 129.0,110.4 129.7,110.4 130.4,110.4 131.1,110.3 131.8,110.3 132.6,110.3 133.3,110.4 134.0,110.3 134.7,110.2 135.4,110.2 136.2,110.1 136.9,110.2 137.6,110.1 138.3,110.2 139.0,110.2 139.8,110.1 140.5,110.1 141.2,110.1 141.9,110.2 142.6,110.2 143.4,110.1 144.1,110.1 144.8,110.2 145.5,110.2 146.2,110.2 147.0,110.2 147.7,110.2 148.4,110.2 149.1,110.3 149.8,110.2 150.6,110.2 151.3,110.2 152.0,110.2 152.7,110.3 153.4,110.3 154.2,110.3 154.9,110.2 155.6,110.2 156.3,110.1 157.0,110.2 157.8,110.1 158.5,110.1 159.2,110.1 159.9,110.1 160.6,110.1 161.4,110.2 162.1,110.2 162.8,110.2 163.5,110.1 164.2,110.1 165.0,110.1 165.7,110.0 166.4,110.1 167.1,110.0 167.8,110.0 168.6,110.0 169.3,110.0 170.0,109.6 170.7,106.7 171.4,103.7 172.2,100.7 172.9,97.7 173.6,94.9 174.3,92.0 175.0,89.0 175.8,86.3 176.5,83.6 177.2,81.0 177.9,78.4 178.6,75.9 179.4,73.4 180.1,71.1 180.8,68.9 181.5,66.8 182.2,64.8 183.0,62.9 183.7,61.1 184.4,59.4 185.1,57.9 185.8,56.5 186.6,55.2 187.3,54.1 188.0,53.1 188.7,52.3 189.4,51.6 190.2,51.0 190.9,50.6 191.6,50.4 192.3,50.2 193.0,50.2 193.8,50.4 194.5,50.7 195.2,51.3 195.9,51.9 196.6,52.8 197.4,53.7 198.1,54.8 198.8,56.0 199.5,57.4 200.2,58.9 201.0,60.5 201.7,62.3 202.4,64.1 203.1,66.2 203.8,68.2 204.6,70.5 205.3,72.8 206.0,75.2 206.7,77.6 207.4,80.2 208.2,82.8 208.9,85.6 209.6,88.3 210.3,91.2 211.0,94.0 211.8,97.0 212.5,99.9 213.2,102.8 213.9,105.8 214.6,108.8 215.4,111.8 216.1,114.8 216.8,117.8 217.5,120.8 218.2,123.7 219.0,126.6 219.7,129.5 220.4,132.3 221.1,135.1 221.8,137.8 222.6,140.4 223.3,142.9 224.0,145.4 224.7,147.8 225.4,150.1 226.2,152.3 226.9,154.3 227.6,156.3 228.3,158.2 229.0,159.9 229.8,161.5 230.5,162.9 231.2,164.2 231.9,165.4 232.6,166.4 233.4,167.3 234.1,168.1 234.8,168.7 235.5,169.2 236.2,169.5 237.0,169.7 237.7,169.7 238.4,169.6 239.1,169.3 239.8,168.9 240.6,168.3 241.3,167.6 242.0,166.7 242.7,165.7 243.4,164.5 244.2,163.2 244.9,161.8 245.6,160.3 246.3,158.6 247.0,156.8 247.8,154.9 248.5,152.8 249.2,150.7 249.9,148.4 250.6,146.1 251.4,143.7 252.1,141.1 252.8,138.5 253.5,135.9 254.2,133.1 255.0,130.4 255.7,127.5 256.4,124.7 257.1,121.7 257.8,118.8 258.6,115.8 259.3,112.8 260.0,109.8 260.7,106.8 261.4,103.8 262.2,100.8 262.9,97.8 263.6,94.9 264.3,92.0 265.0,89.1 265.8,86.3 266.5,83.6 267.2,80.9 267.9,78.4 268.6,75.8 269.4,73.4 270.1,71.1 270.8,68.9 271.5,66.7 272.2,64.7 273.0,62.8 273.7,60.9 274.4,59.3 275.1,57.8 275.8,56.3 276.6,55.1 277.3,53.9 278.0,52.9 278.7,52.1 279.4,51.4 280.2,50.8 280.9,50.5 281.6,50.2 282.3,50.1 283.0,50.1 283.8,50.3 284.5,50.7 285.2,51.2 285.9,51.9 286.6,52.7 287.4,53.7 288.1,54.7 288.8,56.0 289.5,57.4 290.2,58.9 291.0,60.5 291.7,62.2 292.4,64.1 293.1,66.1 293.8,68.2 294.6,70.4 295.3,72.7 296.0,75.1 296.7,77.6 297.4,80.1 298.2,82.8 298.9,85.5 299.6,88.3 300.3,91.1 301.0,94.0 301.8,96.9 302.5,99.9 303.2,102.9 303.9,105.8 304.6,108.8 305.4,111.9 306.1,114.9 306.8,117.9 307.5,120.9 308.2,123.8 309.0,126.7 309.7,129.6 310.4,132.4 311.1,135.1 311.8,137.8 312.6,140.4 313.3,143.0 314.0,145.5 314.7,147.8 315.4,150.1 316.2,152.3 316.9,154.4 317.6,156.3 318.3,158.1 319.0,159.8 319.8,161.4 320.5,162.9 321.2,164.2 321.9,165.4 322.6,166.5 323.4,167.4 324.1,168.1 324.8,168.8 325.5,169.3 326.2,169.6 327.0,169.8 327.7,169.8 328.4,169.7 329.1,169.4 329.8,169.0 330.6,168.4 331.3,167.7 332.0,166.8 332.7,165.8 333.4,164.6 334.2,163.3 334.9,161.9 335.6,160.3 336.3,158.6 337.0,156.9 337.8,154.9 338.5,152.9 339.2,150.7 339.9,148.5 340.6,146.1 341.4,143.7 342.1,141.1 342.8,138.5 343.5,135.8 344.2,133.1 345.0,130.3 345.7,127.4 346.4,124.5 347.1,121.5 347.8,118.5 348.6,115.5 349.3,112.6 350.0,109.5 350.7,106.5 351.4,103.5 352.2,100.6 352.9,97.6 353.6,94.7 354.3,91.8 355.0,88.9 355.8,86.1 356.5,83.4 357.2,80.8 357.9,78.2 358.6,75.6 359.4,73.2 360.1,70.9 360.8,68.7 361.5,66.6 362.2,64.5 363.0,62.6 363.7,60.8 364.4,59.2 365.1,57.7 365.8,56.3 366.6,55.0 367.3,53.9 368.0,52.9 368.7,52.1 369.4,51.4 370.2,50.8 370.9,50.4 371.6,50.1 372.3,50.0 373.0,50.1 373.8,50.3 374.5,50.6 375.2,51.2 375.9,51.8 376.6,52.6 377.4,53.6 378.1,54.7 378.8,55.9 379.5,57.2 380.2,58.7 381.0,60.4 381.7,62.1 382.4,64.0 383.1,66.0 383.8,68.1 384.6,70.3 385.3,72.6 386.0,75.0 386.7,77.5 387.4,80.0 388.2,82.6 388.9,85.3 389.6,88.1 390.3,91.0 391.0,93.9 391.8,96.8 392.5,99.7 393.2,102.8 393.9,105.8 394.6,108.7 395.4,111.7 396.1,114.7 396.8,117.7 397.5,120.7 398.2,123.6 399.0,126.5 399.7,129.4 400.4,132.3 401.1,135.0 401.8,137.7 402.6,140.4 403.3,142.9 404.0,145.4 404.7,147.8 405.4,150.1 406.2,152.3 406.9,154.3 407.6,156.2 408.3,158.1 409.0,159.8 409.8,161.4 410.5,162.9 411.2,164.2 411.9,165.4 412.6,166.5 413.4,167.4 414.1,168.1 414.8,168.7 415.5,169.2 416.2,169.5 417.0,169.7 417.7,169.7 418.4,169.6 419.1,169.3 419.8,168.9 420.6,168.3 421.3,167.6 422.0,166.7 422.7,165.7 423.4,164.5 424.2,163.3 424.9,161.8 425.6,160.2 426.3,158.6 427.0,156.8 427.8,154.8 428.5,152.8 429.2,150.6 429.9,148.3 430.6,145.9 431.4,143.5 432.1,140.9 432.8,138.3 433.5,135.6 434.2,132.9 435.0,130.1 435.7,127.2 436.4,124.3 437.1,121.4 437.8,118.4 438.6,115.4 439.3,112.4 440.0,109.4 440.7,106.3 441.4,102.9 442.2,99.5 442.9,96.1 443.6,92.8 444.3,89.8 445.0,87.1 445.8,84.6 446.5,82.3 447.2,80.1 447.9,77.9 448.6,75.6 449.4,73.2 450.1,70.6 450.8,67.9 451.5,65.3 452.2,62.9 453.0,60.7 453.7,58.9 454.4,57.5 455.1,56.4 455.8,55.5 456.6,54.7 457.3,53.9 458.0,53.0 458.7,52.0 459.4,51.0 460.2,49.9 460.9,49.1 461.6,48.5 462.3,48.3 463.0,48.5 463.8,49.0 464.5,49.8 465.2,50.9 465.9,51.9 466.6,52.9 467.4,53.8 468.1,54.7 468.8,55.5 469.5,56.4 470.2,57.4 471.0,58.8 471.7,60.6 472.4,62.7 473.1,65.1 473.8,67.7 474.6,70.4 475.3,73.0 476.0,75.4 476.7,77.8 477.4,80.0 478.2,82.2 478.9,84.4 479.6,86.8 480.3,89.6 481.0,92.6 481.8,95.8 482.5,99.2 483.2,102.6 483.9,106.0 484.6,109.2 485.4,112.3 486.1,115.0 486.8,117.6 487.5,120.1 488.2,122.6 489.0,125.3 489.7,128.1 490.4,131.2 491.1,134.3 491.8,137.5 492.6,140.6 493.3,143.4 494.0,146.0 494.7,148.2 495.4,150.2 496.2,151.9 496.9,153.5 497.6,155.1 498.3,156.8 499.0,158.7 499.8,160.6 500.5,162.5 501.2,164.3 501.9,165.9 502.6,167.1 503.4,168.0 504.1,168.5 504.8,168.7 505.5,168.7 506.2,168.7 507.0,168.6 507.7,168.6 508.4,168.7 509.1,168.8 509.8,168.8 510.6,168.7 511.3,168.3 512.0,167.5 512.7,166.4 513.4,164.9 514.2,163.1 514.9,161.2 515.6,159.3 516.3,157.5 517.0,155.7 517.8,154.1 518.5,152.5 519.2,150.8 519.9,148.9 520.6,146.8 521.4,144.3 522.1,141.5 522.8,138.5 523.5,135.3 524.2,132.1 525.0,129.1 525.7,126.2 526.4,123.5 527.1,121.0 527.8,118.5 528.6,116.0 529.3,113.3 530.0,110.3 530.7,107.2 531.4,103.8 532.2,100.3 532.9,96.9 533.6,93.6 534.3,90.6 535.0,87.9 535.8,85.4 536.5,83.2 537.2,81.0 537.9,78.8 538.6,76.5 539.4,74.0 540.1,71.4 540.8,68.8 541.5,66.2 542.2,63.7 543.0,61.6 543.7,59.8 544.4,58.3 545.1,57.2 545.8,56.3 546.6,55.4 547.3,54.6 548.0,53.7 548.7,52.9 549.4,52.2 550.2,51.6 550.9,51.2 551.6,50.9 552.3,50.8 553.0,50.9 553.8,51.1 554.5,51.4 555.2,51.9 555.9,52.6 556.6,53.4 557.4,54.3 558.1,55.4 558.8,56.6 559.5,57.9 560.2,59.4 561.0,61.1 561.7,62.8 562.4,64.7 563.1,66.6 563.8,68.8 564.6,71.0 565.3,73.3 566.0,75.7 566.7,78.2 567.4,80.8 568.2,83.4 568.9,86.2 569.6,88.9 570.3,91.7 571.0,94.6 571.8,97.5 572.5,100.5 573.2,103.5 573.9,106.5 574.6,109.5 575.4,112.5 576.1,115.5 576.8,118.5 577.5,121.5 578.2,124.4 579.0,127.3 579.7,130.2 580.4,133.0 581.1,135.8 581.8,138.5 582.6,141.1 583.3,143.6 584.0,146.1 584.7,148.4 585.4,150.7 586.2,152.9 586.9,154.9 587.6,156.8 588.3,158.7 589.0,160.4 589.8,162.0 590.5,163.5 591.2,164.8 591.9,165.9 592.6,167.0 593.4,167.9 594.1,168.7 594.8,169.3 595.5,169.7 596.2,170.0 597.0,170.2 597.7,170.2 598.4,170.1 599.1,169.8 599.8,169.4 600.6,168.8 601.3,168.0 602.0,167.2 602.7,166.1 603.4,165.0 604.2,163.7 604.9,162.3 605.6,160.7 606.3,159.0 607.0,157.2 607.8,155.2 608.5,153.2 609.2,151.0 609.9,148.8 610.6,146.4 611.4,144.0 612.1,141.5 612.8,138.9 613.5,136.1 614.2,133.4 615.0,130.6 615.7,127.7 616.4,124.8 617.1,121.9 617.8,118.9 618.6,115.9 619.3,112.9"/>
  <line x1="80.0" y1="350.0" x2="620.0" y2="350.0" stroke="currentColor" stroke-width="1"/> <line x1="80.0" y1="220.0" x2="80.0" y2="350.0" stroke="currentColor" stroke-width="1"/> <line x1="75.0" y1="350.0" x2="80.0" y2="350.0" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="354.0" font-size="12" text-anchor="end">0</text> <line x1="75.0" y1="300.0" x2="80.0" y2="300.0" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="304.0" font-size="12" text-anchor="end">0.5</text> <line x1="75.0" y1="250.0" x2="80.0" y2="250.0" stroke="currentColor" stroke-width="1"/>
  <text x="70.0" y="254.0" font-size="12" text-anchor="end">1.0</text> <line x1="80.0" y1="250.0" x2="620.0" y2="250.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
  <polyline fill="none" stroke="#d0564a" stroke-width="2" points="80.0,349.9 80.7,355.5 81.4,364.6 82.2,363.5 82.9,358.9 83.6,351.0 84.3,326.8 85.0,314.6 85.8,342.0 86.5,338.4 87.2,353.7 87.9,313.3 88.6,326.4 89.4,330.7 90.1,307.3 90.8,310.6 91.5,285.3 92.2,285.6 93.0,281.0 93.7,310.5 94.4,292.4 95.1,290.5 95.8,280.3 96.6,282.7 97.3,282.4 98.0,270.5 98.7,248.9 99.4,243.4 100.2,242.4 100.9,240.2 101.6,253.3 102.3,244.9 103.0,235.9 103.8,245.8 104.5,253.6 105.2,254.9 105.9,252.2 106.6,249.9 107.4,252.2 108.1,247.0 108.8,246.2 109.5,242.9 110.2,249.2 111.0,247.2 111.7,243.9 112.4,244.4 113.1,246.4 113.8,248.2 114.6,243.7 115.3,243.3 116.0,240.4 116.7,243.0 117.4,237.2 118.2,233.4 118.9,236.9 119.6,235.4 120.3,232.8 121.0,233.9 121.8,232.7 122.5,236.4 123.2,236.5 123.9,237.3 124.6,233.9 125.4,236.2 126.1,236.2 126.8,235.4 127.5,237.0 128.2,238.6 129.0,238.0 129.7,238.8 130.4,239.3 131.1,240.1 131.8,241.6 132.6,241.2 133.3,240.2 134.0,242.3 134.7,243.6 135.4,245.5 136.2,245.9 136.9,244.9 137.6,246.5 138.3,245.1 139.0,245.8 139.8,246.6 140.5,246.4 141.2,246.2 141.9,245.9 142.6,246.1 143.4,246.5 144.1,246.8 144.8,246.6 145.5,245.5 146.2,245.1 147.0,245.1 147.7,245.0 148.4,246.0 149.1,245.1 149.8,245.6 150.6,246.3 151.3,245.6 152.0,245.9 152.7,244.7 153.4,244.7 154.2,245.0 154.9,246.3 155.6,246.7 156.3,247.4 157.0,247.1 157.8,248.4 158.5,248.0 159.2,248.3 159.9,247.7 160.6,247.5 161.4,247.1 162.1,246.2 162.8,247.5 163.5,248.0 164.2,248.1 165.0,248.3 165.7,249.2 166.4,249.1 167.1,249.9 167.8,250.1 168.6,249.6 169.3,249.9 170.0,249.6 170.7,249.0 171.4,249.1 172.2,248.2 172.9,248.6 173.6,247.9 174.3,248.1 175.0,249.0 175.8,248.1 176.5,248.1 177.2,247.0 177.9,246.7 178.6,247.2 179.4,247.5 180.1,247.0 180.8,246.5 181.5,246.7 182.2,246.8 183.0,246.9 183.7,246.7 184.4,246.8 185.1,246.8 185.8,246.7 186.6,247.0 187.3,246.9 188.0,246.5 188.7,246.4 189.4,247.1 190.2,246.8 190.9,247.2 191.6,247.3 192.3,247.6 193.0,248.3 193.8,248.5 194.5,249.0 195.2,248.9 195.9,248.9 196.6,248.5 197.4,248.3 198.1,248.1 198.8,248.5 199.5,248.7 200.2,248.9 201.0,248.6 201.7,248.7 202.4,248.9 203.1,248.6 203.8,248.8 204.6,248.8 205.3,249.1 206.0,249.2 206.7,249.2 207.4,249.5 208.2,249.7 208.9,249.6 209.6,249.6 210.3,249.6 211.0,249.8 211.8,249.9 212.5,250.3 213.2,250.4 213.9,250.5 214.6,250.7 215.4,250.8 216.1,250.8 216.8,250.8 217.5,250.9 218.2,251.2 219.0,251.3 219.7,251.4 220.4,251.5 221.1,251.8 221.8,251.5 222.6,251.2 223.3,251.7 224.0,251.7 224.7,251.9 225.4,251.8 226.2,251.6 226.9,251.5 227.6,251.5 228.3,251.4 229.0,251.6 229.8,251.6 230.5,252.1 231.2,252.3 231.9,252.3 232.6,252.3 233.4,252.6 234.1,252.6 234.8,252.5 235.5,252.4 236.2,252.6 237.0,252.2 237.7,252.2 238.4,252.1 239.1,252.2 239.8,252.2 240.6,252.3 241.3,252.1 242.0,252.0 242.7,252.1 243.4,251.9 244.2,251.8 244.9,251.6 245.6,251.3 246.3,251.6 247.0,251.4 247.8,251.1 248.5,251.0 249.2,250.8 249.9,250.9 250.6,250.6 251.4,250.5 252.1,250.3 252.8,250.3 253.5,250.0 254.2,249.9 255.0,249.6 255.7,249.4 256.4,248.9 257.1,248.9 257.8,248.9 258.6,248.7 259.3,248.5 260.0,248.5 260.7,248.7 261.4,248.7 262.2,248.7 262.9,248.4 263.6,248.7 264.3,248.7 265.0,248.6 265.8,248.7 266.5,248.5 267.2,248.6 267.9,248.5 268.6,248.5 269.4,248.6 270.1,248.3 270.8,248.4 271.5,248.4 272.2,248.4 273.0,248.7 273.7,249.1 274.4,248.9 275.1,248.8 275.8,248.9 276.6,248.9 277.3,249.0 278.0,249.1 278.7,249.2 279.4,249.1 280.2,249.3 280.9,249.1 281.6,249.3 282.3,249.3 283.0,249.4 283.8,249.4 284.5,249.2 285.2,249.3 285.9,249.4 286.6,249.4 287.4,249.2 288.1,249.3 288.8,249.2 289.5,249.1 290.2,249.2 291.0,249.3 291.7,249.2 292.4,249.3 293.1,249.3 293.8,249.4 294.6,249.4 295.3,249.4 296.0,249.5 296.7,249.6 297.4,249.7 298.2,249.8 298.9,249.9 299.6,249.9 300.3,250.0 301.0,249.9 301.8,250.0 302.5,249.9 303.2,250.0 303.9,250.1 304.6,250.1 305.4,250.1 306.1,249.9 306.8,249.8 307.5,250.0 308.2,250.1 309.0,250.3 309.7,250.2 310.4,250.3 311.1,250.6 311.8,250.8 312.6,250.7 313.3,250.6 314.0,250.6 314.7,250.8 315.4,250.9 316.2,250.7 316.9,250.9 317.6,250.9 318.3,251.0 319.0,251.3 319.8,251.4 320.5,251.3 321.2,251.3 321.9,251.2 322.6,251.2 323.4,251.3 324.1,251.4 324.8,251.3 325.5,251.2 326.2,251.1 327.0,251.1 327.7,251.0 328.4,251.0 329.1,250.9 329.8,250.5 330.6,250.5 331.3,250.7 332.0,250.8 332.7,250.8 333.4,250.8 334.2,250.7 334.9,250.8 335.6,250.6 336.3,250.6 337.0,250.2 337.8,250.3 338.5,250.2 339.2,250.3 339.9,250.3 340.6,250.1 341.4,250.1 342.1,250.2 342.8,250.2 343.5,250.3 344.2,250.2 345.0,250.2 345.7,250.3 346.4,250.1 347.1,250.3 347.8,250.4 348.6,250.5 349.3,250.3 350.0,250.3 350.7,250.4 351.4,250.3 352.2,250.2 352.9,250.1 353.6,250.1 354.3,250.1 355.0,250.1 355.8,249.9 356.5,249.9 357.2,249.9 357.9,249.9 358.6,249.9 359.4,249.9 360.1,249.7 360.8,249.7 361.5,249.6 362.2,249.6 363.0,249.6 363.7,249.7 364.4,249.7 365.1,249.5 365.8,249.3 366.6,249.2 367.3,249.1 368.0,249.2 368.7,249.3 369.4,249.3 370.2,249.4 370.9,249.6 371.6,249.7 372.3,249.7 373.0,249.8 373.8,249.9 374.5,249.8 375.2,249.7 375.9,249.7 376.6,249.7 377.4,249.6 378.1,249.6 378.8,249.7 379.5,249.9 380.2,249.9 381.0,250.0 381.7,249.9 382.4,249.8 383.1,249.9 383.8,250.0 384.6,250.1 385.3,250.1 386.0,250.1 386.7,250.2 387.4,250.3 388.2,250.5 388.9,250.6 389.6,250.6 390.3,250.5 391.0,250.5 391.8,250.6 392.5,250.5 393.2,250.4 393.9,250.4 394.6,250.6 395.4,250.6 396.1,250.7 396.8,250.8 397.5,250.7 398.2,250.9 399.0,250.9 399.7,251.0 400.4,250.8 401.1,251.0 401.8,251.0 402.6,250.8 403.3,250.7 404.0,250.9 404.7,250.8 405.4,250.7 406.2,250.8 406.9,250.9 407.6,251.1 408.3,250.9 409.0,251.1 409.8,251.0 410.5,251.0 411.2,251.0 411.9,251.0 412.6,251.0 413.4,251.1 414.1,251.2 414.8,251.2 415.5,251.3 416.2,251.3 417.0,251.2 417.7,251.3 418.4,251.0 419.1,251.0 419.8,251.0 420.6,251.0 421.3,250.8 422.0,250.9 422.7,251.0 423.4,250.8 424.2,250.7 424.9,250.8 425.6,250.9 426.3,250.8 427.0,250.6 427.8,250.7 428.5,250.8 429.2,250.8 429.9,251.0 430.6,251.0 431.4,251.0 432.1,251.0 432.8,251.2 433.5,251.3 434.2,251.1 435.0,250.9 435.7,250.9 436.4,250.8 437.1,250.8 437.8,250.7 438.6,250.8 439.3,250.9 440.0,250.9 440.7,251.6 441.4,253.8 442.2,256.6 442.9,259.2 443.6,261.1 444.3,261.7 445.0,261.1 445.8,259.0 446.5,256.0 447.2,253.0 447.9,250.6 448.6,249.3 449.4,249.4 450.1,251.0 450.8,253.6 451.5,256.5 452.2,258.9 453.0,260.4 453.7,260.5 454.4,258.9 455.1,256.2 455.8,253.2 456.6,250.3 457.3,248.2 458.0,247.6 458.7,248.6 459.4,250.8 460.2,253.8 460.9,256.6 461.6,258.6 462.3,259.3 463.0,258.5 463.8,256.3 464.5,253.5 465.2,250.4 465.9,247.9 466.6,246.7 467.4,246.8 468.1,248.4 468.8,251.0 469.5,254.2 470.2,256.7 471.0,258.1 471.7,258.2 472.4,256.5 473.1,254.0 473.8,250.9 474.6,248.2 475.3,246.4 476.0,246.0 476.7,247.0 477.4,249.2 478.2,252.1 478.9,255.1 479.6,257.2 480.3,258.0 481.0,257.2 481.8,255.3 482.5,252.5 483.2,249.6 483.9,247.2 484.6,246.0 485.4,246.0 486.1,247.6 486.8,250.3 487.5,253.1 488.2,255.8 489.0,257.5 489.7,257.6 490.4,256.2 491.1,253.8 491.8,250.9 492.6,247.9 493.3,246.2 494.0,245.8 494.7,246.7 495.4,248.8 496.2,251.7 496.9,254.5 497.6,256.7 498.3,257.4 499.0,256.7 499.8,254.8 500.5,251.9 501.2,249.0 501.9,246.6 502.6,245.5 503.4,245.7 504.1,247.4 504.8,249.9 505.5,252.8 506.2,255.3 507.0,256.7 507.7,256.8 508.4,255.3 509.1,252.8 509.8,249.9 510.6,247.1 511.3,245.1 512.0,244.4 512.7,245.4 513.4,247.6 514.2,250.5 514.9,253.3 515.6,255.3 516.3,256.1 517.0,255.5 517.8,253.5 518.5,250.9 519.2,247.9 519.9,245.5 520.6,244.3 521.4,244.4 522.1,246.1 522.8,248.7 523.5,251.7 524.2,254.3 525.0,255.8 525.7,255.8 526.4,254.4 527.1,252.0 527.8,248.9 528.6,246.0 529.3,244.2 530.0,243.7 530.7,244.6 531.4,246.8 532.2,249.8 532.9,252.6 533.6,254.7 534.3,255.4 535.0,254.6 535.8,252.6 536.5,249.7 537.2,246.7 537.9,244.3 538.6,243.1 539.4,243.3 540.1,245.0 540.8,247.5 541.5,250.5 542.2,253.0 543.0,254.6 543.7,254.6 544.4,253.3 545.1,250.8 545.8,247.8 546.6,245.1 547.3,243.3 548.0,242.9 548.7,242.9 549.4,242.9 550.2,243.0 550.9,243.1 551.6,243.3 552.3,243.4 553.0,243.3 553.8,243.6 554.5,243.8 555.2,243.8 555.9,243.8 556.6,243.8 557.4,243.9 558.1,244.0 558.8,244.2 559.5,244.4 560.2,244.6 561.0,244.5 561.7,244.7 562.4,244.7 563.1,244.9 563.8,244.7 564.6,244.7 565.3,244.6 566.0,244.7 566.7,244.8 567.4,245.0 568.2,244.9 568.9,244.9 569.6,245.0 570.3,245.1 571.0,245.3 571.8,245.3 572.5,245.4 573.2,245.3 573.9,245.3 574.6,245.4 575.4,245.5 576.1,245.4 576.8,245.5 577.5,245.5 578.2,245.6 579.0,245.7 579.7,245.8 580.4,245.9 581.1,245.8 581.8,245.8 582.6,245.9 583.3,246.2 584.0,246.3 584.7,246.6 585.4,246.7 586.2,246.9 586.9,247.1 587.6,247.3 588.3,247.4 589.0,247.4 589.8,247.6 590.5,247.5 591.2,247.5 591.9,247.8 592.6,247.8 593.4,247.9 594.1,248.0 594.8,248.1 595.5,248.2 596.2,248.3 597.0,248.4 597.7,248.3 598.4,248.4 599.1,248.4 599.8,248.4 600.6,248.4 601.3,248.5 602.0,248.3 602.7,248.4 603.4,248.5 604.2,248.4 604.9,248.3 605.6,248.4 606.3,248.5 607.0,248.3 607.8,248.3 608.5,248.3 609.2,248.2 609.9,248.3 610.6,248.2 611.4,248.2 612.1,248.1 612.8,248.1 613.5,248.3 614.2,248.2 615.0,248.3 615.7,248.4 616.4,248.2 617.1,248.3 617.8,248.3 618.6,248.3 619.3,248.3"/>
  <line x1="80.0" y1="350.0" x2="80.0" y2="355.0" stroke="currentColor" stroke-width="1"/> <text x="80.0" y="370.0" font-size="12" text-anchor="middle">0 s</text> <line x1="170.0" y1="350.0" x2="170.0" y2="355.0" stroke="currentColor" stroke-width="1"/> <text x="170.0" y="370.0" font-size="12" text-anchor="middle">5 s</text> <line x1="260.0" y1="350.0" x2="260.0" y2="355.0" stroke="currentColor" stroke-width="1"/> <text x="260.0" y="370.0" font-size="12" text-anchor="middle">10 s</text> <line x1="350.0" y1="350.0" x2="350.0" y2="355.0" stroke="currentColor" stroke-width="1"/>
  <text x="350.0" y="370.0" font-size="12" text-anchor="middle">15 s</text> <line x1="440.0" y1="350.0" x2="440.0" y2="355.0" stroke="currentColor" stroke-width="1"/> <text x="440.0" y="370.0" font-size="12" text-anchor="middle">20 s</text> <line x1="530.0" y1="350.0" x2="530.0" y2="355.0" stroke="currentColor" stroke-width="1"/> <text x="530.0" y="370.0" font-size="12" text-anchor="middle">25 s</text> <line x1="620.0" y1="350.0" x2="620.0" y2="355.0" stroke="currentColor" stroke-width="1"/> <text x="620.0" y="370.0" font-size="12" text-anchor="middle">30 s</text>
  <line x1="380.0" y1="40.0" x2="396.0" y2="40.0" stroke="#e08a3c" stroke-width="1.5"/> <text x="402.0" y="44.0" font-size="12" text-anchor="start">accel 각도 측정</text> <line x1="380.0" y1="58.0" x2="396.0" y2="58.0" stroke="#888" stroke-width="2.5"/> <text x="402.0" y="62.0" font-size="12" text-anchor="start">정답 θ</text> <line x1="500.0" y1="40.0" x2="516.0" y2="40.0" stroke="#4a7bd0" stroke-width="2"/> <text x="522.0" y="44.0" font-size="12" text-anchor="start">KF θ 추정</text> <line x1="380.0" y1="236.0" x2="396.0" y2="236.0" stroke="#d0564a" stroke-width="2"/>
  <text x="402.0" y="240.0" font-size="12" text-anchor="start">KF bias 추정 [dps] (참값 1.0)</text> <text x="494.0" y="210.0" font-size="12" text-anchor="middle">흔들기</text>
</svg>
```

그림 6 — 1축 KF의 처음 30초. 위: 가속도계 각도(주황, 20–26 s 흔들기 구간에서 크게 출렁임), 정답(회색 굵은 선), KF 추정(파랑, 정답에 겹침). 아래: bias 추정(빨강)이 수 초 안에 참값 1.0 dps(점선)에 붙는다.

> 함정: KF의 성능은 Q, R이 실제와 맞을 때의 이야기다. 여기서 흔들기 구간은 R(σ = 2°)보다 훨씬 큰 15° 교란이라 "모델 위반"이다. KF는 그래도 잘 버텼지만, 실제로는 |a|나 innovation 크기로 R을 키우거나 갱신을 건너뛰는 처리가 필요하다(§9.3, χ² 검정).

### 8.3 3D 자세의 EKF와 error-state KF (ESKF)

3D 자세에서는 두 가지가 비선형이다: quaternion 적분(곱셈)과 측정 모델(`Rᵀe_z`). 방법은 두 가지다.

- **EKF**: 상태에 quaternion 4개 + bias 3개를 넣고, 매 스텝 비선형 함수를 Jacobian으로 선형화한다. 문제는 quaternion이 4개 숫자에 제약(|q| = 1)이 하나 있어서, 4×4 공분산이 특이(singular)해지고 정규화가 KF 갱신과 충돌한다.
- **ESKF (error-state, multiplicative)**: 자세 자체(명목 상태 q)는 quaternion으로 그냥 적분하고, KF는 **작은 오차** `δθ`(회전벡터 3개) + `δb`(3개)만 추정한다. 갱신 후 오차를 명목 상태에 주입(`q ← q ⊗ exp(δθ)`)하고 오차를 0으로 리셋한다. 오차는 항상 작으니 선형화가 정확하고, 3×3 블록은 특이점이 없다. 드론·VIO·AR 같은 실무 자세 추정기의 표준이다(Solà 2017 해설 참고).

6상태 ESKF의 식:

```
명목 적분:  q ← q ⊗ exp((ω − b̂)·dt)
오차 전파:  δθ̇ = −[(ω − b̂)×]·δθ − δb        δḃ = noise
            F = [ I − [(ω − b̂)×]·dt   −I·dt ]       (6×6)
                [ 0                    I     ]
측정 모델:  h(q) = Rᵀe_z (예측된 '위'),  z = a/|a|
            참 자세 R(I + [δθ×]) → Rᵀ_true·e_z ≈ v − δθ × v = v + [v×]·δθ
            H = [ [v×]   0 ]                       (3×6)
갱신 후:    q ← q ⊗ exp(δθ̂),  b̂ ← b̂ + δb̂,  δx ← 0
```

말로 하면: 1축 KF와 똑같은 [자세, bias] 구조를 3D로 키우고, "자세"를 직접 들고 다니는 대신 "자세의 작은 오차"를 들고 다닌다. 측정 noise `R`은 `|a|`가 g에서 벗어날수록 키워서 움직임 중에는 가속도계를 덜 믿게 했다(적응형 R).

```python
# g3_eskf.py — error-state KF (6 상태: 자세 오차 δθ 3 + gyro bias 오차 δb 3), 가속도계로 갱신
import numpy as np
from g3_quat import qmul, qexp, qnorm, qmat
def skew(v): return np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
def eskf(gyro, acc, dt, q0, sg=np.deg2rad(0.05), sb=np.deg2rad(0.01), sa=0.01):
    q, b = q0.copy(), np.zeros(3)
    P = np.diag([1e-4] * 3 + [np.deg2rad(1)**2] * 3)
    Qd = np.diag([sg**2 * dt**2] * 3 + [sb**2 * dt] * 3); out = []
    for w, a in zip(gyro, acc):
        out.append(q)
        an = np.linalg.norm(a); v = qmat(q)[2]                # 1) 갱신: 예측 '위' v vs 측정 a/|a|
        Rm = (sa**2 + ((an - 9.81) / 9.81)**2) * np.eye(3)    # |a|≠g 이면 측정을 덜 믿음
        H = np.hstack([skew(v), np.zeros((3, 3))])            # ∂(Rᵀe_z)/∂δθ = [v×]
        K = P @ H.T @ np.linalg.inv(H @ P @ H.T + Rm)
        dx = K @ (a / an - v)
        q = qnorm(qmul(q, qexp(dx[:3]))); b = b + dx[3:]      # 오차 주입 후 리셋(δx = 0)
        P = (np.eye(6) - K @ H) @ P
        wc = w - b                                            # 2) 예측: 다음 샘플 시각으로
        q = qnorm(qmul(q, qexp(wc * dt)))
        F = np.eye(6); F[:3, :3] -= skew(wc) * dt; F[:3, 3:] = -np.eye(3) * dt
        P = F @ P @ F.T + Qd
    return np.array(out), b
```

결과는 다음 절에서 다른 필터들과 같이 본다. 비용은 6×6 공분산 연산 때문에 Mahony보다 한 자릿수 크다(§11).

---

## 9. 같은 데이터로 비교하고, 튜닝하고, 망가뜨려 보기

### 9.1 6가지 방법의 tilt 오차

§5.3의 60초 데이터에 지금까지의 방법을 모두 돌린다. 평가 지표는 **tilt 오차** = 추정한 '위' 방향과 정답 '위' 방향 사이 각도(yaw와 무관). 모든 필터는 정답 초기 자세에서 출발한다(초기화 문제는 §9.4).

```python
# 같은 60 s 데이터에 6가지 방법 → tilt 오차 RMS 비교
import numpy as np
from g3_sim import t, dt, gyro, acc, q_true, up_true, tilt_err_deg
from g3_filters import gyro_only, complementary, mahony, madgwick
from g3_eskf import eskf
q0 = q_true[0]
up = lambda Q: np.array([[2*(x*z - w*y), 2*(y*z + w*x), 1 - 2*(x*x + y*y)] for w, x, y, z in Q])
res = {
  "gyro 적분만":        tilt_err_deg(up(gyro_only(gyro, dt, q0))),
  "accel tilt만":       tilt_err_deg(acc),
  "complementary α=.99": tilt_err_deg(complementary(gyro, acc, dt, up_true[0], 0.99)),
  "Mahony Kp=1 Ki=.05":   tilt_err_deg(up(mahony(gyro, acc, dt, q0, 1.0, 0.05)[0])),
  "Madgwick β=0.05":    tilt_err_deg(up(madgwick(gyro, acc, dt, q0, 0.05))),
  "ESKF (6상태)":        tilt_err_deg(up(eskf(gyro, acc, dt, q0)[0])),
}
quiet = (t > 5) & ~((t > 20) & (t < 26)) & ~((t > 40) & (t < 43))
print(f"{'필터':22s} {'RMS 전체':>8s} {'RMS 조용':>8s} {'max':>7s} {'t=60s':>7s}  [deg]")
for k, e in res.items():
    print(f"{k:22s} {np.sqrt((e**2).mean()):8.2f} {np.sqrt((e[quiet]**2).mean()):8.2f} {e.max():7.2f} {e[-1]:7.2f}")
np.save("res_tilt.npy", np.array(list(res.values())))
```

```text
필터                       RMS 전체   RMS 조용     max   t=60s  [deg]
gyro 적분만                  18.64    19.92   32.39   32.39
accel tilt만                5.03     0.25   22.55    0.01
complementary α=.99        0.78     0.66    3.11    0.77
Mahony Kp=1 Ki=.05         0.54     0.32    3.04    0.09
Madgwick β=0.05            0.31     0.11    1.62    0.04
ESKF (6상태)                 0.06     0.03    0.45    0.01
```

| 방법 | RMS 전체 | RMS 조용한 구간 | 최대 | 무엇이 오차를 만드나 |
|---|---|---|---|---|
| gyro 적분만 | 18.64° | 19.92° | 32.39° | bias가 적분되어 drift (끝없이 커짐) |
| accel tilt만 | 5.03° | 0.25° | 22.55° | 수평 흔들기 = 22° 기울어진 가짜 중력 |
| complementary α=0.99 | 0.78° | 0.66° | 3.11° | 조용할 때 bias × τ ≈ 0.7° 고정 오차 |
| Mahony Kp=1, Ki=0.05 | 0.54° | 0.32° | 3.04° | 흔들기 구간 (I항이 bias를 일부 학습) |
| Madgwick β=0.05 | 0.31° | 0.11° | 1.62° | 흔들기 구간 (보정 속도 상한 β) |
| ESKF 6상태 | 0.06° | 0.03° | 0.45° | 적응형 R + bias 추정 |

```svg
<svg viewBox="0 0 640 430" xmlns="http://www.w3.org/2000/svg">
  <rect x="260.0" y="30" width="54.0" height="380" fill="#888" fill-opacity="0.15"/> <text x="287.0" y="24.0" font-size="12" text-anchor="middle">흔들기</text> <rect x="440.0" y="30" width="27.0" height="380" fill="#888" fill-opacity="0.15"/> <text x="453.5" y="24.0" font-size="12" text-anchor="middle">점프</text> <line x1="80.0" y1="190.0" x2="620.0" y2="190.0" stroke="currentColor" stroke-width="1"/> <line x1="80.0" y1="40.0" x2="80.0" y2="190.0" stroke="currentColor" stroke-width="1"/> <line x1="75.0" y1="190.0" x2="80.0" y2="190.0" stroke="currentColor" stroke-width="1"/>
  <text x="70.0" y="194.0" font-size="12" text-anchor="end">0°</text> <line x1="75.0" y1="147.1" x2="80.0" y2="147.1" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="151.1" font-size="12" text-anchor="end">10°</text> <line x1="75.0" y1="104.3" x2="80.0" y2="104.3" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="108.3" font-size="12" text-anchor="end">20°</text> <line x1="75.0" y1="61.4" x2="80.0" y2="61.4" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="65.4" font-size="12" text-anchor="end">30°</text>
  <polyline fill="none" stroke="#888" stroke-width="1.5" points="80.0,189.4 81.8,188.7 83.6,188.1 85.4,187.4 87.2,186.7 89.0,186.1 90.8,185.4 92.6,184.8 94.4,184.1 96.2,183.5 98.0,182.8 99.8,182.1 101.6,181.5 103.4,180.8 105.2,180.1 107.0,179.5 108.8,178.8 110.6,178.1 112.4,177.5 114.2,176.8 116.0,176.1 117.8,175.5 119.6,174.8 121.4,174.1 123.2,173.5 125.0,172.8 126.8,172.1 128.6,171.5 130.4,170.8 132.2,170.2 134.0,169.6 135.8,169.0 137.6,168.4 139.4,167.9 141.2,167.3 143.0,166.8 144.8,166.1 146.6,165.5 148.4,164.8 150.2,164.2 152.0,163.5 153.8,162.9 155.6,162.2 157.4,161.6 159.2,161.0 161.0,160.4 162.8,159.7 164.6,159.1 166.4,158.6 168.2,158.0 170.0,157.5 171.8,157.0 173.6,156.6 175.4,156.2 177.2,155.8 179.0,155.4 180.8,155.0 182.6,154.5 184.4,153.9 186.2,153.3 188.0,152.8 189.8,152.4 191.6,151.9 193.4,151.5 195.2,150.9 197.0,150.4 198.8,149.8 200.6,149.2 202.4,148.6 204.2,148.2 206.0,147.9 207.8,147.8 209.6,147.7 211.4,147.6 213.2,147.6 215.0,147.5 216.8,147.2 218.6,146.8 220.4,146.4 222.2,145.8 224.0,145.2 225.8,144.6 227.6,143.9 229.4,143.3 231.2,142.7 233.0,142.1 234.8,141.5 236.6,141.0 238.4,140.4 240.2,140.0 242.0,139.6 243.8,139.3 245.6,139.0 247.4,138.7 249.2,138.4 251.0,138.2 252.8,137.9 254.6,137.5 256.4,137.0 258.2,136.4 260.0,135.8 261.8,135.1 263.6,134.5 265.4,133.9 267.2,133.3 269.0,132.8 270.8,132.3 272.6,131.8 274.4,131.3 276.2,130.7 278.0,130.1 279.8,129.5 281.6,129.0 283.4,128.6 285.2,128.3 287.0,128.0 288.8,127.8 290.6,127.8 292.4,127.7 294.2,127.7 296.0,127.6 297.8,127.5 299.6,127.2 301.4,126.8 303.2,126.4 305.0,125.9 306.8,125.3 308.6,124.8 310.4,124.3 312.2,123.9 314.0,123.5 315.8,123.3 317.6,123.3 319.4,123.3 321.2,123.5 323.0,123.8 324.8,124.3 326.6,124.9 328.4,125.4 330.2,125.9 332.0,126.3 333.8,126.7 335.6,126.9 337.4,126.7 339.2,126.4 341.0,125.9 342.8,125.4 344.6,124.9 346.4,124.4 348.2,123.9 350.0,123.5 351.8,123.1 353.6,122.7 355.4,122.2 357.2,121.8 359.0,121.5 360.8,121.2 362.6,120.9 364.4,120.5 366.2,120.1 368.0,119.6 369.8,119.1 371.6,118.5 373.4,117.9 375.2,117.3 377.0,116.7 378.8,116.3 380.6,115.8 382.4,115.3 384.2,114.8 386.0,114.4 387.8,113.9 389.6,113.3 391.4,112.7 393.2,112.1 395.0,111.4 396.8,110.8 398.6,110.2 400.4,109.5 402.2,109.0 404.0,108.5 405.8,108.0 407.6,107.6 409.4,107.2 411.2,106.8 413.0,106.4 414.8,106.0 416.6,105.5 418.4,105.0 420.2,104.4 422.0,104.0 423.8,103.5 425.6,103.0 427.4,102.6 429.2,102.2 431.0,101.9 432.8,101.6 434.6,101.5 436.4,101.4 438.2,101.4 440.0,101.4 441.8,101.4 443.6,101.2 445.4,100.9 447.2,100.5 449.0,100.0 450.8,99.5 452.6,98.9 454.4,98.3 456.2,97.7 458.0,97.2 459.8,96.8 461.6,96.3 463.4,95.8 465.2,95.2 467.0,94.6 468.8,94.0 470.6,93.3 472.4,92.7 474.2,92.1 476.0,91.6 477.8,91.0 479.6,90.4 481.4,89.8 483.2,89.3 485.0,88.7 486.8,88.0 488.6,87.4 490.4,86.8 492.2,86.1 494.0,85.5 495.8,84.8 497.6,84.2 499.4,83.5 501.2,82.8 503.0,82.2 504.8,81.6 506.6,81.0 508.4,80.6 510.2,80.1 512.0,79.7 513.8,79.2 515.6,78.6 517.4,78.0 519.2,77.3 521.0,76.7 522.8,76.0 524.6,75.5 526.4,75.0 528.2,74.7 530.0,74.4 531.8,74.2 533.6,74.0 535.4,73.8 537.2,73.6 539.0,73.5 540.8,73.3 542.6,73.1 544.4,72.8 546.2,72.5 548.0,72.1 549.8,71.7 551.6,71.2 553.4,70.8 555.2,70.4 557.0,70.0 558.8,69.6 560.6,69.1 562.4,68.5 564.2,67.9 566.0,67.3 567.8,66.6 569.6,66.0 571.4,65.4 573.2,64.8 575.0,64.3 576.8,63.9 578.6,63.6 580.4,63.3 582.2,62.9 584.0,62.4 585.8,62.0 587.6,61.5 589.4,61.0 591.2,60.5 593.0,59.9 594.8,59.3 596.6,58.7 598.4,58.0 600.2,57.3 602.0,56.7 603.8,56.1 605.6,55.5 607.4,54.9 609.2,54.3 611.0,53.7 612.8,53.1 614.6,52.5 616.4,51.8 618.2,51.2"/>
  <polyline fill="none" stroke="#e08a3c" stroke-width="1.2" points="80.0,187.8 81.8,187.8 83.6,188.1 85.4,187.7 87.2,188.7 89.0,188.6 90.8,188.2 92.6,188.1 94.4,188.3 96.2,187.6 98.0,188.5 99.8,188.3 101.6,188.7 103.4,187.5 105.2,188.2 107.0,188.6 108.8,188.5 110.6,188.1 112.4,187.6 114.2,187.8 116.0,188.6 117.8,187.7 119.6,188.1 121.4,188.5 123.2,188.2 125.0,188.4 126.8,188.1 128.6,187.9 130.4,188.3 132.2,188.1 134.0,188.0 135.8,187.4 137.6,188.0 139.4,188.6 141.2,188.2 143.0,188.3 144.8,188.4 146.6,187.5 148.4,188.4 150.2,188.1 152.0,187.6 153.8,187.7 155.6,187.7 157.4,187.9 159.2,187.7 161.0,187.8 162.8,188.5 164.6,188.2 166.4,188.3 168.2,188.3 170.0,187.8 171.8,187.9 173.6,188.3 175.4,187.3 177.2,188.2 179.0,188.3 180.8,188.4 182.6,187.4 184.4,188.4 186.2,188.5 188.0,188.2 189.8,188.0 191.6,187.5 193.4,188.0 195.2,188.3 197.0,188.5 198.8,188.0 200.6,188.6 202.4,188.4 204.2,188.4 206.0,188.2 207.8,187.3 209.6,187.7 211.4,187.3 213.2,187.9 215.0,188.0 216.8,187.8 218.6,187.9 220.4,188.4 222.2,187.6 224.0,188.3 225.8,188.0 227.6,188.2 229.4,188.0 231.2,187.6 233.0,188.1 234.8,188.4 236.6,188.1 238.4,188.1 240.2,188.3 242.0,188.3 243.8,186.9 245.6,188.3 247.4,187.8 249.2,187.6 251.0,187.1 252.8,187.9 254.6,187.2 256.4,188.1 258.2,187.8 260.0,94.7 261.8,94.7 263.6,100.7 265.4,94.0 267.2,95.5 269.0,94.4 270.8,94.2 272.6,99.9 274.4,95.3 276.2,95.2 278.0,94.1 279.8,94.8 281.6,97.9 283.4,94.5 285.2,95.1 287.0,93.3 288.8,94.7 290.6,98.5 292.4,94.3 294.2,95.0 296.0,95.1 297.8,94.1 299.6,99.0 301.4,94.8 303.2,95.0 305.0,94.2 306.8,95.0 308.6,99.9 310.4,94.0 312.2,94.9 314.0,187.8 315.8,188.0 317.6,188.3 319.4,187.8 321.2,188.2 323.0,187.7 324.8,188.4 326.6,187.6 328.4,188.0 330.2,187.9 332.0,187.3 333.8,187.4 335.6,187.9 337.4,188.1 339.2,187.8 341.0,187.9 342.8,188.1 344.6,188.1 346.4,188.1 348.2,187.7 350.0,188.2 351.8,187.6 353.6,187.9 355.4,188.0 357.2,188.0 359.0,187.9 360.8,187.6 362.6,188.1 364.4,187.9 366.2,188.4 368.0,188.3 369.8,187.7 371.6,188.0 373.4,188.2 375.2,188.2 377.0,188.1 378.8,187.9 380.6,188.5 382.4,188.3 384.2,187.8 386.0,188.1 387.8,187.6 389.6,188.4 391.4,187.7 393.2,188.3 395.0,187.6 396.8,188.7 398.6,187.8 400.4,187.9 402.2,188.2 404.0,188.3 405.8,187.9 407.6,187.7 409.4,188.2 411.2,188.4 413.0,188.3 414.8,188.1 416.6,187.8 418.4,188.2 420.2,188.4 422.0,187.9 423.8,188.6 425.6,187.4 427.4,188.1 429.2,188.0 431.0,187.8 432.8,188.2 434.6,187.0 436.4,188.6 438.2,188.1 440.0,188.5 441.8,188.5 443.6,188.0 445.4,184.2 447.2,187.2 449.0,188.5 450.8,188.4 452.6,188.0 454.4,185.2 456.2,187.2 458.0,188.5 459.8,188.1 461.6,187.0 463.4,186.0 465.2,186.4 467.0,188.2 468.8,188.1 470.6,188.2 472.4,187.7 474.2,188.4 476.0,187.7 477.8,188.0 479.6,188.0 481.4,188.0 483.2,187.6 485.0,188.4 486.8,188.2 488.6,188.3 490.4,187.9 492.2,187.9 494.0,188.4 495.8,187.4 497.6,188.3 499.4,187.9 501.2,188.2 503.0,188.0 504.8,187.2 506.6,187.7 508.4,188.6 510.2,188.1 512.0,188.5 513.8,188.2 515.6,188.3 517.4,187.7 519.2,187.8 521.0,188.4 522.8,188.3 524.6,188.1 526.4,188.2 528.2,188.5 530.0,187.9 531.8,187.5 533.6,188.1 535.4,187.9 537.2,187.8 539.0,188.2 540.8,188.0 542.6,188.4 544.4,188.3 546.2,188.3 548.0,188.2 549.8,188.1 551.6,188.4 553.4,187.6 555.2,188.0 557.0,188.2 558.8,188.1 560.6,187.5 562.4,187.8 564.2,188.2 566.0,188.2 567.8,188.1 569.6,188.0 571.4,188.0 573.2,188.0 575.0,188.0 576.8,188.3 578.6,187.8 580.4,188.6 582.2,187.9 584.0,187.7 585.8,188.0 587.6,188.1 589.4,187.8 591.2,188.4 593.0,188.3 594.8,187.9 596.6,187.5 598.4,187.8 600.2,188.2 602.0,188.1 603.8,187.7 605.6,187.7 607.4,188.0 609.2,188.4 611.0,187.8 612.8,187.6 614.6,188.0 616.4,187.9 618.2,188.4"/>
  <line x1="80.0" y1="400.0" x2="620.0" y2="400.0" stroke="currentColor" stroke-width="1"/> <line x1="80.0" y1="240.0" x2="80.0" y2="400.0" stroke="currentColor" stroke-width="1"/> <line x1="75.0" y1="400.0" x2="80.0" y2="400.0" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="404.0" font-size="12" text-anchor="end">0°</text> <line x1="75.0" y1="354.3" x2="80.0" y2="354.3" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="358.3" font-size="12" text-anchor="end">1°</text> <line x1="75.0" y1="308.6" x2="80.0" y2="308.6" stroke="currentColor" stroke-width="1"/>
  <text x="70.0" y="312.6" font-size="12" text-anchor="end">2°</text> <line x1="75.0" y1="262.9" x2="80.0" y2="262.9" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="266.9" font-size="12" text-anchor="end">3°</text>
  <polyline fill="none" stroke="#e08a3c" stroke-width="1.5" points="80.0,393.9 81.8,389.1 83.6,384.9 85.4,381.9 87.2,378.3 89.0,375.7 90.8,373.5 92.6,371.8 94.4,371.1 96.2,370.1 98.0,368.9 99.8,367.2 101.6,366.8 103.4,366.5 105.2,365.7 107.0,365.8 108.8,366.1 110.6,366.5 112.4,366.0 114.2,365.5 116.0,365.7 117.8,365.1 119.6,365.3 121.4,365.1 123.2,365.2 125.0,364.6 126.8,364.6 128.6,364.7 130.4,364.9 132.2,364.3 134.0,364.1 135.8,364.4 137.6,365.1 139.4,365.5 141.2,366.4 143.0,367.1 144.8,367.2 146.6,366.7 148.4,366.6 150.2,366.5 152.0,366.9 153.8,366.8 155.6,366.5 157.4,366.4 159.2,366.1 161.0,365.3 162.8,365.3 164.6,366.2 166.4,366.5 168.2,366.8 170.0,366.5 171.8,365.0 173.6,365.2 175.4,367.4 177.2,369.1 179.0,370.5 180.8,372.3 182.6,372.4 184.4,372.3 186.2,372.8 188.0,373.1 189.8,374.0 191.6,373.8 193.4,373.2 195.2,372.7 197.0,372.4 198.8,372.5 200.6,372.0 202.4,370.6 204.2,368.0 206.0,366.9 207.8,367.3 209.6,370.1 211.4,373.2 213.2,375.3 215.0,373.0 216.8,367.8 218.6,366.3 220.4,365.3 222.2,364.7 224.0,364.9 225.8,365.7 227.6,366.3 229.4,367.3 231.2,366.8 233.0,366.3 234.8,366.2 236.6,367.9 238.4,369.0 240.2,371.2 242.0,373.5 243.8,371.9 245.6,371.2 247.4,371.7 249.2,369.9 251.0,368.8 252.8,368.2 254.6,368.0 256.4,368.2 258.2,369.1 260.0,276.4 261.8,258.0 263.6,351.9 265.4,274.9 267.2,292.0 269.0,303.3 270.8,283.4 272.6,340.4 274.4,286.5 276.2,303.3 278.0,310.6 279.8,290.8 281.6,329.1 283.4,301.8 285.2,318.7 287.0,321.9 288.8,310.1 290.6,316.3 292.4,319.3 294.2,309.1 296.0,308.4 297.8,328.8 299.6,304.2 301.4,328.5 303.2,309.3 305.0,308.9 306.8,321.6 308.6,314.6 310.4,312.3 312.2,323.1 314.0,322.8 315.8,339.7 317.6,354.2 319.4,365.2 321.2,375.2 323.0,383.1 324.8,390.1 326.6,393.8 328.4,392.0 330.2,389.9 332.0,389.2 333.8,389.3 335.6,391.4 337.4,390.4 339.2,388.5 341.0,385.7 342.8,382.4 344.6,380.1 346.4,377.4 348.2,377.0 350.0,377.0 351.8,378.2 353.6,379.7 355.4,377.9 357.2,377.5 359.0,377.0 360.8,374.1 362.6,372.6 364.4,372.3 366.2,372.7 368.0,372.2 369.8,371.3 371.6,369.7 373.4,369.3 375.2,369.8 377.0,372.4 378.8,372.7 380.6,372.4 382.4,371.8 384.2,371.2 386.0,370.7 387.8,369.6 389.6,366.7 391.4,364.7 393.2,363.9 395.0,363.5 396.8,363.5 398.6,365.0 400.4,367.1 402.2,368.9 404.0,370.0 405.8,368.4 407.6,367.4 409.4,367.6 411.2,368.7 413.0,369.5 414.8,371.4 416.6,373.3 418.4,372.9 420.2,372.9 422.0,371.9 423.8,371.4 425.6,371.1 427.4,371.5 429.2,371.3 431.0,369.5 432.8,367.1 434.6,364.7 436.4,364.6 438.2,364.7 440.0,365.3 441.8,365.9 443.6,368.8 445.4,370.6 447.2,370.8 449.0,369.8 450.8,367.4 452.6,365.1 454.4,364.9 456.2,365.0 458.0,366.1 459.8,367.5 461.6,369.3 463.4,370.3 465.2,369.3 467.0,368.9 468.8,369.0 470.6,369.3 472.4,369.2 474.2,369.3 476.0,369.9 477.8,369.1 479.6,367.1 481.4,367.2 483.2,368.4 485.0,369.0 486.8,368.8 488.6,368.4 490.4,367.7 492.2,367.7 494.0,367.6 495.8,367.1 497.6,366.8 499.4,366.8 501.2,366.3 503.0,365.9 504.8,365.7 506.6,367.2 508.4,370.2 510.2,372.3 512.0,373.4 513.8,372.8 515.6,371.7 517.4,370.3 519.2,368.8 521.0,367.0 522.8,365.1 524.6,363.1 526.4,363.1 528.2,364.4 530.0,366.4 531.8,368.1 533.6,368.9 535.4,369.3 537.2,369.8 539.0,371.3 540.8,373.4 542.6,375.0 544.4,375.9 546.2,376.4 548.0,376.8 549.8,374.3 551.6,371.0 553.4,369.8 555.2,369.9 557.0,370.8 558.8,371.9 560.6,372.3 562.4,370.8 564.2,370.1 566.0,370.4 567.8,369.7 569.6,369.0 571.4,369.2 573.2,369.2 575.0,369.6 576.8,370.4 578.6,371.5 580.4,372.0 582.2,371.9 584.0,370.8 585.8,370.2 587.6,369.3 589.4,369.0 591.2,368.3 593.0,368.3 594.8,367.0 596.6,365.0 598.4,364.3 600.2,365.0 602.0,366.3 603.8,367.6 605.6,367.8 607.4,367.7 609.2,368.0 611.0,367.6 612.8,366.9 614.6,366.8 616.4,365.2 618.2,364.5"/>
  <polyline fill="none" stroke="#4a7bd0" stroke-width="1.5" points="80.0,393.9 81.8,389.1 83.6,385.0 85.4,382.0 87.2,378.5 89.0,376.0 90.8,373.9 92.6,372.4 94.4,371.8 96.2,371.0 98.0,370.1 99.8,368.6 101.6,368.4 103.4,368.2 105.2,367.8 107.0,368.0 108.8,368.6 110.6,369.3 112.4,369.3 114.2,369.0 116.0,369.4 117.8,369.3 119.6,369.6 121.4,369.8 123.2,370.1 125.0,369.9 126.8,370.2 128.6,370.4 130.4,371.2 132.2,371.4 134.0,371.4 135.8,372.2 137.6,373.3 139.4,373.8 141.2,374.6 143.0,375.2 144.8,375.0 146.6,374.6 148.4,374.8 150.2,374.5 152.0,374.3 153.8,374.4 155.6,374.4 157.4,374.6 159.2,374.8 161.0,375.3 162.8,375.3 164.6,375.9 166.4,376.2 168.2,376.5 170.0,377.0 171.8,378.0 173.6,378.2 175.4,379.4 177.2,380.1 179.0,380.9 180.8,381.7 182.6,381.7 184.4,381.5 186.2,381.0 188.0,380.7 189.8,380.8 191.6,381.1 193.4,381.9 195.2,382.2 197.0,382.4 198.8,382.8 200.6,382.7 202.4,382.2 204.2,382.3 206.0,382.7 207.8,383.8 209.6,385.0 211.4,386.3 213.2,386.9 215.0,386.9 216.8,386.5 218.6,386.2 220.4,385.6 222.2,384.9 224.0,384.4 225.8,384.1 227.6,384.0 229.4,384.5 231.2,384.0 233.0,384.2 234.8,384.1 236.6,384.3 238.4,384.6 240.2,385.4 242.0,385.9 243.8,386.9 245.6,387.6 247.4,388.2 249.2,387.3 251.0,386.6 252.8,385.6 254.6,385.3 256.4,384.8 258.2,384.7 260.0,279.8 261.8,261.2 263.6,362.0 265.4,281.5 267.2,299.4 269.0,313.9 270.8,293.2 272.6,338.8 274.4,300.2 276.2,317.4 278.0,327.1 279.8,306.0 281.6,327.4 283.4,310.7 285.2,324.5 287.0,323.9 288.8,314.8 290.6,319.9 292.4,319.9 294.2,316.3 296.0,315.7 297.8,323.1 299.6,314.6 301.4,322.7 303.2,316.6 305.0,316.0 306.8,320.6 308.6,318.9 310.4,316.7 312.2,322.6 314.0,322.1 315.8,337.0 317.6,349.8 319.4,359.3 321.2,367.6 323.0,373.6 324.8,379.1 326.6,382.5 328.4,384.9 330.2,387.5 332.0,389.2 333.8,390.4 335.6,392.1 337.4,394.0 339.2,396.0 341.0,397.6 342.8,397.3 344.6,396.8 346.4,395.0 348.2,394.5 350.0,394.2 351.8,394.5 353.6,394.7 355.4,393.6 357.2,393.2 359.0,393.2 360.8,392.9 362.6,392.7 364.4,392.2 366.2,391.5 368.0,391.0 369.8,390.8 371.6,390.6 373.4,390.8 375.2,391.2 377.0,391.4 378.8,390.7 380.6,390.7 382.4,390.9 384.2,390.7 386.0,390.5 387.8,390.3 389.6,390.1 391.4,390.2 393.2,390.3 395.0,390.2 396.8,390.0 398.6,390.2 400.4,390.2 402.2,390.1 404.0,390.0 405.8,390.4 407.6,390.5 409.4,391.1 411.2,391.6 413.0,391.9 414.8,393.0 416.6,392.9 418.4,392.6 420.2,392.6 422.0,392.1 423.8,391.7 425.6,391.4 427.4,391.6 429.2,391.6 431.0,391.7 432.8,392.0 434.6,391.5 436.4,391.7 438.2,391.6 440.0,391.3 441.8,391.5 443.6,392.4 445.4,393.8 447.2,394.9 449.0,394.5 450.8,394.3 452.6,393.4 454.4,393.2 456.2,393.3 458.0,393.2 459.8,393.2 461.6,393.3 463.4,391.9 465.2,391.4 467.0,391.5 468.8,391.5 470.6,391.2 472.4,391.2 474.2,391.7 476.0,392.4 477.8,393.0 479.6,392.9 481.4,393.0 483.2,393.8 485.0,393.9 486.8,393.7 488.6,393.7 490.4,393.8 492.2,394.0 494.0,394.2 495.8,393.8 497.6,393.9 499.4,393.7 501.2,393.2 503.0,393.3 504.8,393.3 506.6,393.3 508.4,393.5 510.2,394.0 512.0,394.4 513.8,394.3 515.6,394.0 517.4,393.6 519.2,393.4 521.0,393.1 522.8,393.3 524.6,393.7 526.4,394.0 528.2,394.0 530.0,394.0 531.8,394.1 533.6,394.0 535.4,394.2 537.2,394.1 539.0,394.6 540.8,394.9 542.6,395.3 544.4,395.0 546.2,395.0 548.0,395.4 549.8,394.8 551.6,394.3 553.4,394.1 555.2,394.1 557.0,394.5 558.8,395.3 560.6,395.2 562.4,395.9 564.2,396.2 566.0,396.3 567.8,396.0 569.6,395.7 571.4,395.8 573.2,395.6 575.0,395.0 576.8,394.6 578.6,395.0 580.4,395.1 582.2,394.9 584.0,394.7 585.8,394.6 587.6,394.1 589.4,394.2 591.2,394.7 593.0,395.2 594.8,395.3 596.6,395.0 598.4,395.6 600.2,395.5 602.0,395.4 603.8,395.6 605.6,395.5 607.4,395.5 609.2,395.9 611.0,395.8 612.8,395.4 614.6,395.4 616.4,395.7 618.2,395.7"/>
  <polyline fill="none" stroke="#3f9a6b" stroke-width="1.5" points="80.0,393.9 81.8,392.6 83.6,392.6 85.4,391.6 87.2,389.5 89.0,394.2 90.8,392.3 92.6,392.9 94.4,390.4 96.2,394.3 98.0,393.2 99.8,389.4 101.6,390.5 103.4,395.6 105.2,389.1 107.0,392.0 108.8,393.2 110.6,391.4 112.4,390.7 114.2,393.3 116.0,393.8 117.8,394.0 119.6,392.9 121.4,393.0 123.2,392.6 125.0,393.8 126.8,393.7 128.6,394.1 130.4,392.7 132.2,393.3 134.0,393.1 135.8,390.2 137.6,393.2 139.4,393.7 141.2,394.3 143.0,390.8 144.8,392.1 146.6,389.4 148.4,393.6 150.2,390.6 152.0,393.4 153.8,390.7 155.6,391.3 157.4,392.2 159.2,391.6 161.0,392.2 162.8,393.3 164.6,394.3 166.4,392.9 168.2,391.2 170.0,394.0 171.8,392.0 173.6,393.3 175.4,392.4 177.2,393.9 179.0,394.2 180.8,391.3 182.6,389.3 184.4,394.6 186.2,395.1 188.0,392.8 189.8,393.3 191.6,394.0 193.4,389.4 195.2,393.5 197.0,394.6 198.8,391.4 200.6,395.0 202.4,392.7 204.2,394.7 206.0,392.5 207.8,392.2 209.6,392.8 211.4,392.7 213.2,393.6 215.0,392.1 216.8,393.9 218.6,390.9 220.4,392.9 222.2,390.4 224.0,395.2 225.8,394.1 227.6,392.1 229.4,394.2 231.2,394.8 233.0,391.0 234.8,392.4 236.6,394.0 238.4,392.8 240.2,392.9 242.0,393.5 243.8,393.4 245.6,395.4 247.4,393.4 249.2,390.4 251.0,391.6 252.8,393.0 254.6,391.7 256.4,395.2 258.2,394.9 260.0,357.3 261.8,342.9 263.6,376.5 265.4,340.4 267.2,351.7 269.0,350.5 270.8,336.6 272.6,365.5 274.4,336.3 276.2,345.9 278.0,347.8 279.8,338.1 281.6,358.7 283.4,343.6 285.2,351.3 287.0,359.0 288.8,354.5 290.6,358.6 292.4,367.6 294.2,348.3 296.0,346.5 297.8,357.8 299.6,332.9 301.4,351.4 303.2,328.1 305.0,326.1 306.8,349.0 308.6,327.4 310.4,348.3 312.2,339.5 314.0,338.2 315.8,378.0 317.6,390.7 319.4,393.2 321.2,394.7 323.0,392.3 324.8,396.3 326.6,393.0 328.4,390.6 330.2,392.5 332.0,392.8 333.8,395.0 335.6,394.3 337.4,393.8 339.2,392.7 341.0,395.6 342.8,390.6 344.6,393.8 346.4,389.0 348.2,393.3 350.0,393.6 351.8,393.6 353.6,393.1 355.4,392.7 357.2,393.3 359.0,392.5 360.8,391.3 362.6,393.8 364.4,390.8 366.2,390.1 368.0,392.4 369.8,393.9 371.6,394.4 373.4,391.1 375.2,391.3 377.0,392.6 378.8,390.1 380.6,391.3 382.4,390.2 384.2,393.0 386.0,394.0 387.8,395.4 389.6,394.1 391.4,396.2 393.2,395.0 395.0,393.8 396.8,393.6 398.6,394.7 400.4,393.7 402.2,394.6 404.0,393.9 405.8,391.5 407.6,392.0 409.4,392.2 411.2,394.0 413.0,393.9 414.8,393.3 416.6,392.3 418.4,391.9 420.2,391.5 422.0,389.2 423.8,393.6 425.6,393.0 427.4,387.6 429.2,392.6 431.0,395.2 432.8,394.9 434.6,391.2 436.4,391.8 438.2,390.1 440.0,389.9 441.8,394.9 443.6,391.7 445.4,387.2 447.2,390.9 449.0,391.8 450.8,393.5 452.6,389.2 454.4,391.7 456.2,391.1 458.0,393.1 459.8,393.2 461.6,393.7 463.4,384.5 465.2,384.2 467.0,391.7 468.8,393.0 470.6,390.4 472.4,395.0 474.2,393.3 476.0,393.0 477.8,394.7 479.6,393.6 481.4,394.1 483.2,392.0 485.0,394.2 486.8,393.3 488.6,393.1 490.4,393.9 492.2,394.1 494.0,392.6 495.8,393.2 497.6,393.3 499.4,391.3 501.2,389.4 503.0,394.7 504.8,394.0 506.6,393.7 508.4,392.5 510.2,394.4 512.0,394.0 513.8,395.3 515.6,393.3 517.4,388.1 519.2,390.3 521.0,390.7 522.8,394.3 524.6,392.8 526.4,391.2 528.2,393.0 530.0,389.1 531.8,389.8 533.6,391.3 535.4,391.8 537.2,391.6 539.0,394.9 540.8,392.8 542.6,392.9 544.4,391.4 546.2,391.9 548.0,393.6 549.8,393.9 551.6,392.2 553.4,392.7 555.2,395.1 557.0,394.4 558.8,394.9 560.6,393.1 562.4,393.3 564.2,391.8 566.0,394.5 567.8,392.8 569.6,393.9 571.4,395.3 573.2,393.0 575.0,391.6 576.8,391.3 578.6,390.1 580.4,392.0 582.2,393.4 584.0,394.7 585.8,393.0 587.6,392.1 589.4,394.6 591.2,394.0 593.0,395.0 594.8,391.1 596.6,391.0 598.4,393.3 600.2,395.1 602.0,392.6 603.8,394.1 605.6,393.6 607.4,394.2 609.2,392.9 611.0,393.2 612.8,389.1 614.6,393.4 616.4,394.9 618.2,395.4"/>
  <polyline fill="none" stroke="#d0564a" stroke-width="1.5" points="80.0,392.8 81.8,394.7 83.6,396.7 85.4,395.9 87.2,396.4 89.0,398.1 90.8,398.1 92.6,398.2 94.4,398.3 96.2,399.1 98.0,399.4 99.8,397.9 101.6,397.9 103.4,398.2 105.2,398.3 107.0,398.4 108.8,399.5 110.6,399.2 112.4,398.8 114.2,398.9 116.0,399.3 117.8,399.4 119.6,398.9 121.4,398.9 123.2,399.0 125.0,399.1 126.8,399.4 128.6,399.3 130.4,399.1 132.2,398.9 134.0,398.2 135.8,396.4 137.6,396.3 139.4,398.3 141.2,398.6 143.0,398.2 144.8,397.6 146.6,397.5 148.4,398.7 150.2,398.6 152.0,398.8 153.8,398.3 155.6,397.7 157.4,398.7 159.2,398.1 161.0,398.0 162.8,398.3 164.6,398.7 166.4,398.6 168.2,398.7 170.0,398.8 171.8,398.4 173.6,398.4 175.4,398.1 177.2,398.1 179.0,398.4 180.8,398.4 182.6,398.4 184.4,398.6 186.2,399.2 188.0,399.1 189.8,399.3 191.6,399.5 193.4,399.5 195.2,399.6 197.0,399.4 198.8,398.9 200.6,398.7 202.4,398.6 204.2,398.7 206.0,398.8 207.8,398.7 209.6,398.6 211.4,398.7 213.2,398.9 215.0,399.1 216.8,398.9 218.6,398.2 220.4,398.2 222.2,398.3 224.0,398.5 225.8,398.5 227.6,398.3 229.4,398.3 231.2,398.3 233.0,398.0 234.8,398.0 236.6,398.1 238.4,398.3 240.2,398.4 242.0,398.7 243.8,398.7 245.6,399.0 247.4,399.0 249.2,399.1 251.0,399.3 252.8,399.2 254.6,399.2 256.4,399.5 258.2,399.5 260.0,394.0 261.8,389.1 263.6,394.3 265.4,386.3 267.2,391.9 269.0,393.0 270.8,386.8 272.6,395.0 274.4,385.3 276.2,391.6 278.0,389.8 279.8,380.9 281.6,392.5 283.4,380.5 285.2,390.1 287.0,391.7 288.8,381.4 290.6,393.4 292.4,384.2 294.2,390.9 296.0,389.1 297.8,381.8 299.6,389.6 301.4,381.6 303.2,389.6 305.0,390.8 306.8,379.4 308.6,393.3 310.4,382.2 312.2,391.9 314.0,390.4 315.8,392.0 317.6,393.5 319.4,393.8 321.2,394.4 323.0,394.7 324.8,395.8 326.6,396.0 328.4,396.3 330.2,396.9 332.0,397.2 333.8,397.2 335.6,397.4 337.4,397.5 339.2,397.6 341.0,397.8 342.8,398.3 344.6,398.5 346.4,398.5 348.2,398.9 350.0,398.8 351.8,399.2 353.6,399.3 355.4,399.4 357.2,399.3 359.0,399.3 360.8,399.3 362.6,399.3 364.4,399.2 366.2,398.8 368.0,398.6 369.8,398.6 371.6,398.5 373.4,398.7 375.2,399.0 377.0,399.3 378.8,398.9 380.6,398.9 382.4,399.0 384.2,398.9 386.0,398.9 387.8,398.9 389.6,398.8 391.4,399.1 393.2,399.4 395.0,399.5 396.8,399.5 398.6,399.7 400.4,399.7 402.2,399.5 404.0,399.4 405.8,399.4 407.6,399.2 409.4,399.2 411.2,399.3 413.0,399.4 414.8,399.8 416.6,399.4 418.4,399.3 420.2,399.3 422.0,399.6 423.8,399.6 425.6,399.4 427.4,399.3 429.2,399.2 431.0,399.2 432.8,399.1 434.6,399.0 436.4,398.8 438.2,398.7 440.0,398.7 441.8,398.5 443.6,398.3 445.4,398.1 447.2,398.0 449.0,397.8 450.8,397.8 452.6,397.5 454.4,397.5 456.2,397.6 458.0,397.6 459.8,397.7 461.6,397.5 463.4,397.4 465.2,397.4 467.0,397.4 468.8,398.6 470.6,398.8 472.4,398.9 474.2,399.0 476.0,399.0 477.8,399.3 479.6,399.6 481.4,399.4 483.2,398.9 485.0,398.9 486.8,399.3 488.6,399.2 490.4,399.0 492.2,398.9 494.0,398.9 495.8,399.1 497.6,399.0 499.4,399.1 501.2,399.5 503.0,399.4 504.8,399.4 506.6,399.3 508.4,399.4 510.2,399.0 512.0,399.0 513.8,399.0 515.6,399.1 517.4,399.4 519.2,399.5 521.0,399.5 522.8,399.7 524.6,399.6 526.4,399.3 528.2,399.4 530.0,399.2 531.8,399.4 533.6,399.2 535.4,399.1 537.2,399.1 539.0,399.5 540.8,399.6 542.6,399.7 544.4,399.5 546.2,399.3 548.0,399.3 549.8,399.4 551.6,399.0 553.4,398.9 555.2,398.8 557.0,398.9 558.8,399.2 560.6,399.0 562.4,399.1 564.2,399.4 566.0,399.2 567.8,399.4 569.6,399.6 571.4,399.4 573.2,399.5 575.0,399.5 576.8,399.6 578.6,399.5 580.4,399.3 582.2,399.3 584.0,399.7 585.8,399.8 587.6,399.4 589.4,399.4 591.2,399.6 593.0,399.8 594.8,399.5 596.6,399.3 598.4,399.4 600.2,399.2 602.0,399.3 603.8,399.6 605.6,399.6 607.4,399.7 609.2,399.3 611.0,399.2 612.8,399.4 614.6,399.7 616.4,399.6 618.2,399.5"/>
  <line x1="80.0" y1="400.0" x2="80.0" y2="405.0" stroke="currentColor" stroke-width="1"/> <text x="80.0" y="420.0" font-size="12" text-anchor="middle">0 s</text> <line x1="170.0" y1="400.0" x2="170.0" y2="405.0" stroke="currentColor" stroke-width="1"/> <text x="170.0" y="420.0" font-size="12" text-anchor="middle">10 s</text> <line x1="260.0" y1="400.0" x2="260.0" y2="405.0" stroke="currentColor" stroke-width="1"/> <text x="260.0" y="420.0" font-size="12" text-anchor="middle">20 s</text> <line x1="350.0" y1="400.0" x2="350.0" y2="405.0" stroke="currentColor" stroke-width="1"/>
  <text x="350.0" y="420.0" font-size="12" text-anchor="middle">30 s</text> <line x1="440.0" y1="400.0" x2="440.0" y2="405.0" stroke="currentColor" stroke-width="1"/> <text x="440.0" y="420.0" font-size="12" text-anchor="middle">40 s</text> <line x1="530.0" y1="400.0" x2="530.0" y2="405.0" stroke="currentColor" stroke-width="1"/> <text x="530.0" y="420.0" font-size="12" text-anchor="middle">50 s</text> <line x1="620.0" y1="400.0" x2="620.0" y2="405.0" stroke="currentColor" stroke-width="1"/> <text x="620.0" y="420.0" font-size="12" text-anchor="middle">60 s</text>
  <line x1="90.0" y1="52.0" x2="106.0" y2="52.0" stroke="#888" stroke-width="2"/> <text x="112.0" y="56.0" font-size="12" text-anchor="start">gyro 적분만 (drift)</text> <line x1="250.0" y1="52.0" x2="266.0" y2="52.0" stroke="#e08a3c" stroke-width="2"/> <text x="272.0" y="56.0" font-size="12" text-anchor="start">accel tilt만 (흔들기에 22°)</text> <line x1="90.0" y1="252.0" x2="106.0" y2="252.0" stroke="#e08a3c" stroke-width="2"/> <text x="112.0" y="256.0" font-size="12" text-anchor="start">complementary α=0.99</text> <line x1="270.0" y1="252.0" x2="286.0" y2="252.0" stroke="#4a7bd0" stroke-width="2"/>
  <text x="292.0" y="256.0" font-size="12" text-anchor="start">Mahony</text> <line x1="360.0" y1="252.0" x2="376.0" y2="252.0" stroke="#3f9a6b" stroke-width="2"/> <text x="382.0" y="256.0" font-size="12" text-anchor="start">Madgwick</text> <line x1="460.0" y1="252.0" x2="476.0" y2="252.0" stroke="#d0564a" stroke-width="2"/> <text x="482.0" y="256.0" font-size="12" text-anchor="start">ESKF</text> <text x="20.0" y="120.0" font-size="12" text-anchor="start">tilt</text> <text x="20.0" y="135.0" font-size="12" text-anchor="start">오차</text>
  <text x="20.0" y="320.0" font-size="12" text-anchor="start">tilt</text> <text x="20.0" y="335.0" font-size="12" text-anchor="start">오차</text>
</svg>
```

그림 7 — tilt 오차의 시간 변화(20샘플 구간 최댓값). 위 패널: 자이로만(회색)은 계속 증가하고, 가속도계만(주황)은 흔들기 구간에서 22°까지 튄다. 아래 패널(축 10배 확대): 퓨전 필터들은 흔들기 구간에서만 1–3° 튀고 나머지는 1° 아래에 머문다. 회색 띠는 흔들기(20–26 s)와 점프(40–43 s) 구간.

```svg
<svg viewBox="0 0 640 290" xmlns="http://www.w3.org/2000/svg">
  <line x1="170.0" y1="30.0" x2="170.0" y2="234.0" stroke="currentColor" stroke-width="1"/> <line x1="170.0" y1="234.0" x2="170.0" y2="239.0" stroke="currentColor" stroke-width="1"/> <text x="170.0" y="254.0" font-size="12" text-anchor="middle">0.01°</text> <line x1="170.0" y1="30.0" x2="170.0" y2="234.0" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/> <line x1="280.0" y1="234.0" x2="280.0" y2="239.0" stroke="currentColor" stroke-width="1"/> <text x="280.0" y="254.0" font-size="12" text-anchor="middle">0.1°</text>
  <line x1="280.0" y1="30.0" x2="280.0" y2="234.0" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/> <line x1="390.0" y1="234.0" x2="390.0" y2="239.0" stroke="currentColor" stroke-width="1"/> <text x="390.0" y="254.0" font-size="12" text-anchor="middle">1°</text> <line x1="390.0" y1="30.0" x2="390.0" y2="234.0" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/> <line x1="500.0" y1="234.0" x2="500.0" y2="239.0" stroke="currentColor" stroke-width="1"/> <text x="500.0" y="254.0" font-size="12" text-anchor="middle">10°</text>
  <line x1="500.0" y1="30.0" x2="500.0" y2="234.0" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/> <line x1="610.0" y1="234.0" x2="610.0" y2="239.0" stroke="currentColor" stroke-width="1"/> <text x="610.0" y="254.0" font-size="12" text-anchor="middle">100°</text> <line x1="610.0" y1="30.0" x2="610.0" y2="234.0" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/> <rect x="170" y="38" width="359.7" height="22" fill="#888"/> <text x="162.0" y="54.0" font-size="13" text-anchor="end">gyro만</text> <text x="535.7" y="54.0" font-size="12" text-anchor="start">18.64°</text>
  <rect x="170" y="72" width="297.2" height="22" fill="#e08a3c"/> <text x="162.0" y="88.0" font-size="13" text-anchor="end">accel만</text> <text x="473.2" y="88.0" font-size="12" text-anchor="start">5.03°</text> <rect x="170" y="106" width="208.3" height="22" fill="#e08a3c"/> <text x="162.0" y="122.0" font-size="13" text-anchor="end">compl.</text> <text x="384.3" y="122.0" font-size="12" text-anchor="start">0.78°</text> <rect x="170" y="140" width="190.8" height="22" fill="#4a7bd0"/> <text x="162.0" y="156.0" font-size="13" text-anchor="end">Mahony</text>
  <text x="366.8" y="156.0" font-size="12" text-anchor="start">0.54°</text> <rect x="170" y="174" width="164.4" height="22" fill="#3f9a6b"/> <text x="162.0" y="190.0" font-size="13" text-anchor="end">Madgwick</text> <text x="340.4" y="190.0" font-size="12" text-anchor="start">0.31°</text> <rect x="170" y="208" width="87.6" height="22" fill="#d0564a"/> <text x="162.0" y="224.0" font-size="13" text-anchor="end">ESKF</text> <text x="263.6" y="224.0" font-size="12" text-anchor="start">0.06°</text> <text x="390.0" y="280.0" font-size="13" text-anchor="middle">tilt 오차 RMS, 60 s 전체 (log 축)</text>
</svg>
```

그림 8 — 60초 전체 tilt 오차 RMS (log 축). 자이로만 → 퓨전 → ESKF로 갈 때 각각 한 자릿수 이상 좋아진다.

읽는 법:

- **가속도계만으로도 조용할 때는 0.25°**로 꽤 정확하다. 퓨전이 필요한 이유는 움직일 때다.
- **complementary와 Mahony의 차이(0.66° vs 0.32°, 조용한 구간)는 I항**이다. bias를 학습하지 않으면 bias × τ가 남는다.
- ESKF가 가장 좋지만 **공정한 비교가 아니다**: 이 시뮬레이션의 noise 크기를 정확히 알려 줬고, |a| 기반 적응형 R까지 넣었다. 실제 센서에서는 Q, R을 맞추는 데 시간이 들고, 모델이 틀리면 이 차이는 줄어든다. 표의 숫자는 "이 합성 데이터에서"의 순위다.
- 시뮬레이션은 bias가 모든 body 축에서 관측된다(손목이 여러 방향으로 돌기 때문에 시간이 지나면 모든 축이 한 번씩 수평이 된다). 기기가 거의 항상 같은 방향으로 놓인다면(예: 책상 위 기기) 수직축 bias는 가속도계로 학습할 수 없다.

### 9.2 yaw — 지자기계가 필요한 이유, 그리고 자기 교란

tilt만 필요한 과제(중력 제거, 손목 들기 감지)는 6축으로 충분하다. heading이 필요하면(보행 방향, AR, 로봇 내비게이션) 지자기계를 더한 **9축(MARG, AHRS)** 필터가 필요하다. Mahony에 자기장 항을 더한다.

```
h   = R̂ · m̂                         (측정 자기장을 world로)
ref = R̂ᵀ · (0, √(h_x² + h_y²), h_z)   (수평 성분을 북쪽 +y로 돌린 기준을 다시 body로; ENU 기준)
e_m = m̂ × ref                         (자기장 방향 오차)
e  += k_m · (e_m · v̂) · v̂             (수직축 성분만 남긴다 → yaw만 고친다)
```

말로 하면: 자기장의 **수평 성분이 북쪽을 가리키도록** yaw만 돌린다. 자기장의 수직 성분(복각, dip)은 위치마다 다르므로 측정값에서 그대로 가져오고, 자기 오차가 tilt를 건드리지 못하게 수직축으로 투영한다(자기 교란이 tilt까지 망치는 것을 막는 실무 기법). 여기에 **|m| gating**을 넣는다: 측정 자기장 크기가 보정 때의 지구 자기장 세기(여기서는 49.2 µT)에서 3 µT 이상 벗어나면 그 샘플은 자기장 보정을 건너뛴다.

```python
# g3_ahrs.py — Mahony 9축: 가속도(tilt) + 지자기(yaw) 보정, |a|·|m| gating
import numpy as np
from g3_quat import qmul, qexp, qnorm, qmat
def mahony9(gyro, acc, mag, dt, q0, kp=1.0, ki=0.05, km=0.5, gate_a=None, gate_m=None, m_ref=49.2):
    q = q0.copy(); e_int = np.zeros(3); out = []
    for w, a, m in zip(gyro, acc, mag):
        out.append(q)
        Rm = qmat(q); up_b = Rm[2]; e = np.zeros(3)
        an, mn = np.linalg.norm(a), np.linalg.norm(m)
        if gate_a is None or abs(an - 9.81) < gate_a:
            e += np.cross(a / an, up_b)                       # tilt 오차
        if km > 0 and (gate_m is None or abs(mn - m_ref) < gate_m):
            h = Rm @ (m / mn)                                 # 측정 자기장을 world로
            ref = Rm.T @ np.array([0.0, np.hypot(h[0], h[1]), h[2]])   # 북쪽(+y)으로 돌린 기준
            e_m = np.cross(m / mn, ref)
            e += km / kp * np.dot(e_m, up_b) * up_b           # 수직축 성분만 → yaw만 고친다
        e_int += ki * e * dt
        q = qnorm(qmul(q, qexp((w + kp * e + e_int) * dt)))
    return np.array(out)
```

```python
# yaw는 지자기만이 잡는다 — 그리고 자기 교란은 gating으로 막는다
import numpy as np
from g3_sim import t, dt, gyro, acc, mag, q_true, md
from g3_filters import gyro_only, mahony
from g3_ahrs import mahony9
from g3_quat import qangle
q0 = q_true[0]
err = lambda Q: np.rad2deg([qangle(p, q) for p, q in zip(Q, q_true)])   # 전체 자세 오차 (yaw 포함)
cases = {"gyro 적분만": gyro_only(gyro, dt, q0),
         "Mahony 6축": mahony(gyro, acc, dt, q0, 1.0, 0.05)[0],
         "Mahony 9축": mahony9(gyro, acc, mag, dt, q0),
         "9축 + |m| gate 3µT": mahony9(gyro, acc, mag, dt, q0, gate_m=3.0)}
after = (t > 36) & (t < 46)
print(f"{'':20s} {'RMS':>6s} {'교란 중 max':>10s} {'교란 후 10s RMS':>14s} {'t=60s':>6s}  [deg]")
for k, Q in cases.items():
    e = err(Q)
    print(f"{k:20s} {np.sqrt((e**2).mean()):6.2f} {e[md].max():10.2f} {np.sqrt((e[after]**2).mean()):14.2f} {e[-1]:6.2f}")
    np.save(f"ahrs_{list(cases).index(k)}.npy", e)
```

```text
                        RMS   교란 중 max   교란 후 10s RMS  t=60s  [deg]
gyro 적분만              19.83      20.60          23.28  33.94
Mahony 6축              2.59       3.04           3.39   3.96
Mahony 9축              9.31      23.92          17.65   3.26
9축 + |m| gate 3µT      1.12       2.06           1.51   0.09
```

출력에서 볼 것 (지표는 yaw를 포함한 **전체 자세 오차**):

- 자이로만: 60초 후 34°.
- Mahony 6축: 3.96°. I항이 bias를 학습해서 yaw drift도 꽤 줄었다 — 하지만 절대 heading 기준이 없으므로 장시간이면 결국 흘러간다.
- Mahony 9축, gate 없음: 교란 구간(30–36 s)에 **24°까지 끌려가고**, 교란이 끝난 뒤 10초 동안도 RMS 17.7°로 회복이 느리다. 잘못된 자기장이 I항(bias 추정)까지 오염시켰기 때문이다.
- 9축 + |m| gate: 교란 중 최대 2.1°, 끝날 때 0.09°. 교란 동안은 자이로로 버티고, 교란이 끝나면 다시 자기장을 쓴다.

```svg
<svg viewBox="0 0 640 290" xmlns="http://www.w3.org/2000/svg">
  <rect x="350.0" y="30" width="54.0" height="230" fill="#888" fill-opacity="0.15"/> <text x="377.0" y="24.0" font-size="12" text-anchor="middle">자기 교란</text> <line x1="80.0" y1="260.0" x2="620.0" y2="260.0" stroke="currentColor" stroke-width="1"/> <line x1="80.0" y1="40.0" x2="80.0" y2="260.0" stroke="currentColor" stroke-width="1"/> <line x1="75.0" y1="260.0" x2="80.0" y2="260.0" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="264.0" font-size="12" text-anchor="end">0°</text> <line x1="75.0" y1="186.7" x2="80.0" y2="186.7" stroke="currentColor" stroke-width="1"/>
  <text x="70.0" y="190.7" font-size="12" text-anchor="end">10°</text> <line x1="75.0" y1="113.3" x2="80.0" y2="113.3" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="117.3" font-size="12" text-anchor="end">20°</text> <line x1="75.0" y1="40.0" x2="80.0" y2="40.0" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="44.0" font-size="12" text-anchor="end">30°</text> <line x1="80.0" y1="260.0" x2="80.0" y2="265.0" stroke="currentColor" stroke-width="1"/> <text x="80.0" y="280.0" font-size="12" text-anchor="middle">0 s</text>
  <line x1="170.0" y1="260.0" x2="170.0" y2="265.0" stroke="currentColor" stroke-width="1"/> <text x="170.0" y="280.0" font-size="12" text-anchor="middle">10 s</text> <line x1="260.0" y1="260.0" x2="260.0" y2="265.0" stroke="currentColor" stroke-width="1"/> <text x="260.0" y="280.0" font-size="12" text-anchor="middle">20 s</text> <line x1="350.0" y1="260.0" x2="350.0" y2="265.0" stroke="currentColor" stroke-width="1"/> <text x="350.0" y="280.0" font-size="12" text-anchor="middle">30 s</text> <line x1="440.0" y1="260.0" x2="440.0" y2="265.0" stroke="currentColor" stroke-width="1"/>
  <text x="440.0" y="280.0" font-size="12" text-anchor="middle">40 s</text> <line x1="530.0" y1="260.0" x2="530.0" y2="265.0" stroke="currentColor" stroke-width="1"/> <text x="530.0" y="280.0" font-size="12" text-anchor="middle">50 s</text> <line x1="620.0" y1="260.0" x2="620.0" y2="265.0" stroke="currentColor" stroke-width="1"/> <text x="620.0" y="280.0" font-size="12" text-anchor="middle">60 s</text>
  <polyline fill="none" stroke="#888" stroke-width="1.8" points="80.0,259.0 81.8,257.8 83.6,256.7 85.4,255.5 87.2,254.4 89.0,253.2 90.8,252.1 92.6,251.0 94.4,249.9 96.2,248.7 98.0,247.6 99.8,246.4 101.6,245.3 103.4,244.1 105.2,243.0 107.0,241.8 108.8,240.7 110.6,239.5 112.4,238.4 114.2,237.2 116.0,236.1 117.8,234.9 119.6,233.8 121.4,232.6 123.2,231.5 125.0,230.3 126.8,229.2 128.6,228.1 130.4,226.9 132.2,225.8 134.0,224.7 135.8,223.6 137.6,222.6 139.4,221.6 141.2,220.6 143.0,219.5 144.8,218.4 146.6,217.2 148.4,216.1 150.2,215.0 152.0,213.9 153.8,212.8 155.6,211.8 157.4,210.8 159.2,209.8 161.0,208.8 162.8,207.8 164.6,206.9 166.4,205.9 168.2,205.0 170.0,204.2 171.8,203.4 173.6,202.8 175.4,202.1 177.2,201.5 179.0,200.8 180.8,200.0 182.6,199.2 184.4,198.2 186.2,197.2 188.0,196.4 189.8,195.6 191.6,194.9 193.4,194.0 195.2,193.2 197.0,192.3 198.8,191.3 200.6,190.2 202.4,189.2 204.2,188.4 206.0,188.0 207.8,187.7 209.6,187.5 211.4,187.3 213.2,187.2 215.0,187.0 216.8,186.5 218.6,185.8 220.4,185.0 222.2,184.1 224.0,183.1 225.8,182.0 227.6,180.9 229.4,179.9 231.2,178.8 233.0,177.7 234.8,176.7 236.6,175.7 238.4,174.8 240.2,173.9 242.0,173.2 243.8,172.6 245.6,172.1 247.4,171.5 249.2,171.0 251.0,170.6 252.8,170.1 254.6,169.5 256.4,168.6 258.2,167.7 260.0,166.6 261.8,165.5 263.6,164.4 265.4,163.3 267.2,162.2 269.0,161.3 270.8,160.4 272.6,159.4 274.4,158.4 276.2,157.3 278.0,156.2 279.8,155.2 281.6,154.3 283.4,153.4 285.2,152.7 287.0,152.1 288.8,151.7 290.6,151.4 292.4,151.2 294.2,150.9 296.0,150.7 297.8,150.2 299.6,149.6 301.4,148.7 303.2,147.7 305.0,146.7 306.8,145.7 308.6,144.6 310.4,143.7 312.2,142.9 314.0,142.1 315.8,141.5 317.6,141.2 319.4,141.2 321.2,141.2 323.0,141.5 324.8,142.1 326.6,142.8 328.4,143.5 330.2,144.0 332.0,144.4 333.8,144.7 335.6,144.3 337.4,143.7 339.2,142.8 341.0,141.8 342.8,140.7 344.6,139.6 346.4,138.6 348.2,137.5 350.0,136.5 351.8,135.6 353.6,134.7 355.4,133.8 357.2,132.9 359.0,132.2 360.8,131.4 362.6,130.7 364.4,129.9 366.2,129.0 368.0,128.1 369.8,127.1 371.6,125.9 373.4,124.8 375.2,123.6 377.0,122.5 378.8,121.5 380.6,120.5 382.4,119.5 384.2,118.5 386.0,117.5 387.8,116.5 389.6,115.6 391.4,114.6 393.2,113.6 395.0,112.7 396.8,111.8 398.6,110.9 400.4,109.9 402.2,108.9 404.0,108.0 405.8,107.1 407.6,106.1 409.4,105.1 411.2,104.2 413.0,103.2 414.8,102.2 416.6,101.1 418.4,100.1 420.2,99.3 422.0,98.5 423.8,97.9 425.6,97.3 427.4,96.8 429.2,96.4 431.0,95.9 432.8,95.5 434.6,95.1 436.4,94.9 438.2,94.6 440.0,94.2 441.8,93.7 443.6,93.1 445.4,92.2 447.2,91.2 449.0,90.3 450.8,89.3 452.6,88.5 454.4,87.7 456.2,87.0 458.0,86.5 459.8,86.2 461.6,85.7 463.4,84.9 465.2,84.0 467.0,83.0 468.8,82.0 470.6,80.9 472.4,79.7 474.2,78.6 476.0,77.5 477.8,76.4 479.6,75.2 481.4,74.1 483.2,73.0 485.0,71.8 486.8,70.7 488.6,69.6 490.4,68.5 492.2,67.3 494.0,66.2 495.8,65.0 497.6,63.9 499.4,62.8 501.2,61.8 503.0,60.8 504.8,59.9 506.6,59.2 508.4,58.8 510.2,58.4 512.0,58.0 513.8,57.4 515.6,56.7 517.4,55.9 519.2,55.0 521.0,53.9 522.8,52.8 524.6,51.8 526.4,50.8 528.2,50.0 530.0,49.4 531.8,48.9 533.6,48.5 535.4,48.2 537.2,48.1 539.0,48.1 540.8,48.1 542.6,48.1 544.4,47.9 546.2,47.5 548.0,47.0 549.8,46.3 551.6,45.5 553.4,44.7 555.2,43.8 557.0,42.9 558.8,41.9 560.6,40.8 562.4,40.0 564.2,40.0 566.0,40.0 567.8,40.0 569.6,40.0 571.4,40.0 573.2,40.0 575.0,40.0 576.8,40.0 578.6,40.0 580.4,40.0 582.2,40.0 584.0,40.0 585.8,40.0 587.6,40.0 589.4,40.0 591.2,40.0 593.0,40.0 594.8,40.0 596.6,40.0 598.4,40.0 600.2,40.0 602.0,40.0 603.8,40.0 605.6,40.0 607.4,40.0 609.2,40.0 611.0,40.0 612.8,40.0 614.6,40.0 616.4,40.0 618.2,40.0"/>
  <polyline fill="none" stroke="#e08a3c" stroke-width="1.8" points="80.0,259.0 81.8,258.2 83.6,257.6 85.4,257.1 87.2,256.5 89.0,256.1 90.8,255.7 92.6,255.4 94.4,255.3 96.2,255.1 98.0,255.0 99.8,254.7 101.6,254.6 103.4,254.6 105.2,254.4 107.0,254.4 108.8,254.5 110.6,254.5 112.4,254.4 114.2,254.3 116.0,254.3 117.8,254.2 119.6,254.2 121.4,254.1 123.2,254.1 125.0,253.9 126.8,253.9 128.6,253.8 130.4,253.8 132.2,253.6 134.0,253.5 135.8,253.3 137.6,253.0 139.4,252.7 141.2,252.3 143.0,251.9 144.8,251.6 146.6,251.3 148.4,251.2 150.2,251.1 152.0,251.1 153.8,251.2 155.6,251.3 157.4,251.4 159.2,251.6 161.0,251.8 162.8,251.9 164.6,252.2 166.4,252.5 168.2,252.8 170.0,253.1 171.8,253.6 173.6,254.0 175.4,254.6 177.2,255.0 179.0,255.6 180.8,256.1 182.6,256.3 184.4,256.5 186.2,256.6 188.0,256.5 189.8,256.3 191.6,256.2 193.4,256.0 195.2,255.7 197.0,255.5 198.8,255.4 200.6,255.3 202.4,255.3 204.2,255.4 206.0,255.7 207.8,256.2 209.6,256.7 211.4,257.2 213.2,257.6 215.0,257.8 216.8,257.8 218.6,257.8 220.4,257.7 222.2,257.6 224.0,257.5 225.8,257.4 227.6,257.4 229.4,257.4 231.2,257.4 233.0,257.4 234.8,257.4 236.6,257.5 238.4,257.5 240.2,257.6 242.0,257.6 243.8,257.5 245.6,257.4 247.4,257.2 249.2,257.0 251.0,256.9 252.8,256.7 254.6,256.7 256.4,256.7 258.2,256.7 260.0,240.6 261.8,237.6 263.6,253.5 265.4,240.8 267.2,243.6 269.0,245.8 270.8,242.5 272.6,249.4 274.4,243.5 276.2,246.1 278.0,247.4 279.8,244.2 281.6,247.2 283.4,244.6 285.2,246.5 287.0,246.4 288.8,244.8 290.6,245.4 292.4,245.1 294.2,244.4 296.0,244.3 297.8,245.1 299.6,243.6 301.4,244.4 303.2,243.3 305.0,243.2 306.8,243.6 308.6,243.1 310.4,242.8 312.2,243.2 314.0,243.2 315.8,244.6 317.6,245.6 319.4,246.1 321.2,246.4 323.0,246.4 324.8,246.3 326.6,246.2 328.4,246.0 330.2,245.8 332.0,245.5 333.8,245.2 335.6,244.9 337.4,244.6 339.2,244.3 341.0,244.1 342.8,243.9 344.6,243.6 346.4,243.3 348.2,243.0 350.0,242.7 351.8,242.4 353.6,242.2 355.4,242.0 357.2,241.7 359.0,241.5 360.8,241.2 362.6,241.0 364.4,240.7 366.2,240.5 368.0,240.3 369.8,240.1 371.6,239.9 373.4,239.6 375.2,239.4 377.0,239.2 378.8,239.0 380.6,238.8 382.4,238.6 384.2,238.4 386.0,238.1 387.8,238.0 389.6,237.9 391.4,237.9 393.2,237.8 395.0,237.9 396.8,237.9 398.6,237.9 400.4,237.8 402.2,237.7 404.0,237.5 405.8,237.3 407.6,237.0 409.4,236.8 411.2,236.5 413.0,236.3 414.8,236.0 416.6,235.8 418.4,235.6 420.2,235.6 422.0,235.6 423.8,235.6 425.6,235.6 427.4,235.8 429.2,235.9 431.0,236.0 432.8,236.1 434.6,236.1 436.4,236.0 438.2,235.8 440.0,235.6 441.8,235.3 443.6,235.1 445.4,234.8 447.2,234.6 449.0,234.5 450.8,234.4 452.6,234.4 454.4,234.4 456.2,234.4 458.0,234.5 459.8,234.7 461.6,234.8 463.4,235.0 465.2,235.0 467.0,235.0 468.8,234.9 470.6,234.8 472.4,234.7 474.2,234.5 476.0,234.3 477.8,234.1 479.6,233.9 481.4,233.8 483.2,233.6 485.0,233.4 486.8,233.3 488.6,233.2 490.4,233.1 492.2,233.0 494.0,232.9 495.8,232.7 497.6,232.6 499.4,232.6 501.2,232.6 503.0,232.5 504.8,232.6 506.6,232.6 508.4,232.7 510.2,232.8 512.0,232.9 513.8,233.0 515.6,233.1 517.4,233.1 519.2,233.1 521.0,233.1 522.8,233.1 524.6,232.9 526.4,232.8 528.2,232.7 530.0,232.6 531.8,232.5 533.6,232.5 535.4,232.5 537.2,232.5 539.0,232.5 540.8,232.7 542.6,232.8 544.4,233.0 546.2,233.1 548.0,233.2 549.8,233.3 551.6,233.2 553.4,233.2 555.2,233.0 557.0,232.9 558.8,232.7 560.6,232.5 562.4,232.4 564.2,232.3 566.0,232.2 567.8,232.1 569.6,232.1 571.4,232.2 573.2,232.2 575.0,232.2 576.8,232.3 578.6,232.3 580.4,232.4 582.2,232.3 584.0,232.3 585.8,232.2 587.6,232.1 589.4,232.0 591.2,231.9 593.0,231.8 594.8,231.6 596.6,231.5 598.4,231.5 600.2,231.4 602.0,231.3 603.8,231.2 605.6,231.2 607.4,231.1 609.2,231.0 611.0,231.0 612.8,231.0 614.6,231.0 616.4,231.0 618.2,231.0"/>
  <polyline fill="none" stroke="#d0564a" stroke-width="1.8" points="80.0,259.0 81.8,258.2 83.6,257.5 85.4,257.0 87.2,256.5 89.0,256.0 90.8,255.7 92.6,255.4 94.4,255.3 96.2,255.1 98.0,254.9 99.8,254.7 101.6,254.6 103.4,254.5 105.2,254.4 107.0,254.4 108.8,254.5 110.6,254.5 112.4,254.4 114.2,254.3 116.0,254.4 117.8,254.3 119.6,254.3 121.4,254.3 123.2,254.3 125.0,254.2 126.8,254.1 128.6,254.1 130.4,254.1 132.2,254.0 134.0,253.9 135.8,253.8 137.6,253.7 139.4,253.5 141.2,253.2 143.0,252.9 144.8,252.8 146.6,252.6 148.4,252.6 150.2,252.5 152.0,252.6 153.8,252.7 155.6,252.9 157.4,253.1 159.2,253.4 161.0,253.7 162.8,253.8 164.6,254.1 166.4,254.4 168.2,254.7 170.0,255.1 171.8,255.5 173.6,255.8 175.4,256.3 177.2,256.6 179.0,256.9 180.8,257.1 182.6,257.0 184.4,257.0 186.2,256.9 188.0,256.8 189.8,256.9 191.6,257.0 193.4,257.1 195.2,257.0 197.0,257.0 198.8,256.9 200.6,256.8 202.4,256.7 204.2,256.8 206.0,257.0 207.8,257.3 209.6,257.6 211.4,257.8 213.2,257.7 215.0,257.5 216.8,257.3 218.6,257.3 220.4,257.3 222.2,257.3 224.0,257.3 225.8,257.3 227.6,257.3 229.4,257.5 231.2,257.4 233.0,257.5 234.8,257.4 236.6,257.4 238.4,257.4 240.2,257.4 242.0,257.4 243.8,257.2 245.6,257.1 247.4,256.9 249.2,256.7 251.0,256.6 252.8,256.4 254.6,256.4 256.4,256.3 258.2,256.3 260.0,240.5 261.8,237.6 263.6,253.7 265.4,240.9 267.2,243.7 269.0,246.0 270.8,242.7 272.6,250.0 274.4,243.8 276.2,246.5 278.0,248.0 279.8,244.7 281.6,248.1 283.4,245.4 285.2,247.5 287.0,247.4 288.8,245.9 290.6,246.6 292.4,246.4 294.2,245.8 296.0,245.7 297.8,246.7 299.6,245.3 301.4,246.2 303.2,245.3 305.0,245.2 306.8,245.7 308.6,245.4 310.4,245.1 312.2,245.8 314.0,245.7 315.8,247.4 317.6,248.7 319.4,249.4 321.2,249.8 323.0,250.0 324.8,250.1 326.6,250.0 328.4,250.0 330.2,249.8 332.0,249.7 333.8,249.6 335.6,249.5 337.4,249.4 339.2,249.3 341.0,249.3 342.8,249.3 344.6,249.3 346.4,249.3 348.2,249.2 350.0,249.2 351.8,254.7 353.6,246.5 355.4,238.4 357.2,230.4 359.0,222.5 360.8,214.8 362.6,207.3 364.4,200.2 366.2,193.2 368.0,186.4 369.8,179.8 371.6,173.5 373.4,167.2 375.2,161.1 377.0,155.1 378.8,149.0 380.6,143.2 382.4,137.4 384.2,131.7 386.0,126.1 387.8,120.6 389.6,115.3 391.4,110.3 393.2,105.5 395.0,101.0 396.8,96.8 398.6,92.7 400.4,88.6 402.2,84.6 404.0,84.4 405.8,86.3 407.6,88.0 409.4,89.8 411.2,91.6 413.0,93.4 414.8,95.3 416.6,97.3 418.4,99.2 420.2,101.2 422.0,103.3 423.8,105.7 425.6,108.2 427.4,111.0 429.2,113.9 431.0,116.6 432.8,119.2 434.6,121.8 436.4,124.2 438.2,126.4 440.0,128.5 441.8,130.6 443.6,132.5 445.4,134.3 447.2,135.9 449.0,137.4 450.8,139.0 452.6,140.6 454.4,142.3 456.2,144.1 458.0,145.9 459.8,147.6 461.6,149.2 463.4,150.7 465.2,152.0 467.0,153.3 468.8,154.5 470.6,155.6 472.4,156.8 474.2,157.8 476.0,159.0 477.8,160.2 479.6,161.5 481.4,162.9 483.2,164.5 485.0,166.0 486.8,167.7 488.6,169.3 490.4,170.9 492.2,172.3 494.0,173.7 495.8,174.9 497.6,175.9 499.4,177.0 501.2,177.9 503.0,178.9 504.8,179.8 506.6,180.8 508.4,181.8 510.2,183.0 512.0,184.2 513.8,185.6 515.6,187.0 517.4,188.4 519.2,189.7 521.0,191.1 522.8,192.3 524.6,193.4 526.4,194.4 528.2,195.4 530.0,196.5 531.8,197.6 533.6,198.7 535.4,199.8 537.2,201.0 539.0,202.2 540.8,203.4 542.6,204.6 544.4,205.9 546.2,207.1 548.0,208.3 549.8,209.6 551.6,210.8 553.4,211.9 555.2,212.8 557.0,213.7 558.8,214.5 560.6,215.3 562.4,215.9 564.2,216.6 566.0,217.5 567.8,218.4 569.6,219.3 571.4,220.4 573.2,221.4 575.0,222.4 576.8,223.3 578.6,224.1 580.4,224.8 582.2,225.4 584.0,225.9 585.8,226.4 587.6,226.7 589.4,227.1 591.2,227.6 593.0,228.1 594.8,228.8 596.6,229.5 598.4,230.2 600.2,231.0 602.0,231.8 603.8,232.5 605.6,233.3 607.4,233.9 609.2,234.5 611.0,234.9 612.8,235.3 614.6,235.6 616.4,235.8 618.2,236.0"/>
  <polyline fill="none" stroke="#4a7bd0" stroke-width="1.8" points="80.0,259.0 81.8,258.2 83.6,257.5 85.4,257.0 87.2,256.5 89.0,256.0 90.8,255.7 92.6,255.4 94.4,255.3 96.2,255.1 98.0,254.9 99.8,254.7 101.6,254.6 103.4,254.5 105.2,254.4 107.0,254.4 108.8,254.5 110.6,254.5 112.4,254.4 114.2,254.3 116.0,254.4 117.8,254.3 119.6,254.3 121.4,254.3 123.2,254.3 125.0,254.2 126.8,254.1 128.6,254.1 130.4,254.1 132.2,254.0 134.0,253.9 135.8,253.8 137.6,253.7 139.4,253.5 141.2,253.2 143.0,252.9 144.8,252.8 146.6,252.6 148.4,252.6 150.2,252.5 152.0,252.6 153.8,252.7 155.6,252.9 157.4,253.1 159.2,253.4 161.0,253.7 162.8,253.8 164.6,254.1 166.4,254.4 168.2,254.7 170.0,255.1 171.8,255.5 173.6,255.8 175.4,256.3 177.2,256.6 179.0,256.9 180.8,257.1 182.6,257.0 184.4,257.0 186.2,256.9 188.0,256.8 189.8,256.9 191.6,257.0 193.4,257.1 195.2,257.0 197.0,257.0 198.8,256.9 200.6,256.8 202.4,256.7 204.2,256.8 206.0,257.0 207.8,257.3 209.6,257.6 211.4,257.8 213.2,257.7 215.0,257.5 216.8,257.3 218.6,257.3 220.4,257.3 222.2,257.3 224.0,257.3 225.8,257.3 227.6,257.3 229.4,257.5 231.2,257.4 233.0,257.5 234.8,257.4 236.6,257.4 238.4,257.4 240.2,257.4 242.0,257.4 243.8,257.2 245.6,257.1 247.4,256.9 249.2,256.7 251.0,256.6 252.8,256.4 254.6,256.4 256.4,256.3 258.2,256.3 260.0,240.5 261.8,237.6 263.6,253.7 265.4,240.9 267.2,243.7 269.0,246.0 270.8,242.7 272.6,250.0 274.4,243.8 276.2,246.5 278.0,248.0 279.8,244.7 281.6,248.1 283.4,245.4 285.2,247.5 287.0,247.4 288.8,245.9 290.6,246.6 292.4,246.4 294.2,245.8 296.0,245.7 297.8,246.7 299.6,245.3 301.4,246.2 303.2,245.3 305.0,245.2 306.8,245.7 308.6,245.4 310.4,245.1 312.2,245.8 314.0,245.7 315.8,247.4 317.6,248.7 319.4,249.4 321.2,249.8 323.0,250.0 324.8,250.1 326.6,250.0 328.4,250.0 330.2,249.8 332.0,249.7 333.8,249.6 335.6,249.5 337.4,249.4 339.2,249.3 341.0,249.3 342.8,249.3 344.6,249.3 346.4,249.3 348.2,249.2 350.0,248.9 351.8,248.7 353.6,248.5 355.4,248.3 357.2,248.1 359.0,247.8 360.8,247.6 362.6,247.4 364.4,247.2 366.2,247.0 368.0,246.8 369.8,246.7 371.6,246.5 373.4,246.3 375.2,246.2 377.0,246.0 378.8,245.8 380.6,245.6 382.4,245.5 384.2,245.3 386.0,245.1 387.8,245.0 389.6,244.9 391.4,244.9 393.2,244.9 395.0,245.0 396.8,245.0 398.6,245.0 400.4,245.0 402.2,244.9 404.0,244.9 405.8,245.0 407.6,245.1 409.4,245.1 411.2,245.2 413.0,245.2 414.8,245.2 416.6,245.3 418.4,245.4 420.2,245.6 422.0,245.9 423.8,246.1 425.6,246.5 427.4,246.9 429.2,247.3 431.0,247.7 432.8,248.0 434.6,248.3 436.4,248.5 438.2,248.7 440.0,248.8 441.8,248.8 443.6,248.9 445.4,248.9 447.2,248.9 449.0,249.1 450.8,249.2 452.6,249.4 454.4,249.7 456.2,250.0 458.0,250.3 459.8,250.7 461.6,251.1 463.4,251.4 465.2,251.7 467.0,251.9 468.8,252.0 470.6,252.1 472.4,252.2 474.2,252.3 476.0,252.4 477.8,252.4 479.6,252.5 481.4,252.6 483.2,252.6 485.0,252.7 486.8,252.8 488.6,252.9 490.4,253.0 492.2,253.1 494.0,253.2 495.8,253.2 497.6,253.4 499.4,253.4 501.2,253.5 503.0,253.7 504.8,253.8 506.6,254.0 508.4,254.2 510.2,254.5 512.0,254.8 513.8,255.0 515.6,255.2 517.4,255.4 519.2,255.6 521.0,255.7 522.8,255.8 524.6,255.9 526.4,256.0 528.2,256.0 530.0,256.1 531.8,256.1 533.6,256.2 535.4,256.4 537.2,256.6 539.0,256.8 540.8,257.1 542.6,257.3 544.4,257.6 546.2,257.8 548.0,258.0 549.8,258.2 551.6,258.2 553.4,258.3 555.2,258.4 557.0,258.4 558.8,258.4 560.6,258.4 562.4,258.4 564.2,258.4 566.0,258.5 567.8,258.5 569.6,258.6 571.4,258.8 573.2,258.9 575.0,259.1 576.8,259.2 578.6,259.3 580.4,259.4 582.2,259.4 584.0,259.4 585.8,259.3 587.6,259.2 589.4,259.2 591.2,259.2 593.0,259.3 594.8,259.3 596.6,259.2 598.4,259.3 600.2,259.4 602.0,259.3 603.8,259.4 605.6,259.3 607.4,259.3 609.2,259.4 611.0,259.4 612.8,259.3 614.6,259.3 616.4,259.3 618.2,259.3"/>
  <line x1="90.0" y1="48.0" x2="106.0" y2="48.0" stroke="#888" stroke-width="2"/> <text x="112.0" y="52.0" font-size="12" text-anchor="start">gyro 적분만</text> <line x1="200.0" y1="48.0" x2="216.0" y2="48.0" stroke="#e08a3c" stroke-width="2"/> <text x="222.0" y="52.0" font-size="12" text-anchor="start">Mahony 6축 (yaw는 drift)</text> <line x1="90.0" y1="68.0" x2="106.0" y2="68.0" stroke="#d0564a" stroke-width="2"/> <text x="112.0" y="72.0" font-size="12" text-anchor="start">Mahony 9축, gate 없음</text> <line x1="260.0" y1="68.0" x2="276.0" y2="68.0" stroke="#4a7bd0" stroke-width="2"/>
  <text x="282.0" y="72.0" font-size="12" text-anchor="start">9축 + |m| gate</text> <text x="20.0" y="150.0" font-size="12" text-anchor="start">자세</text> <text x="20.0" y="165.0" font-size="12" text-anchor="start">오차</text>
</svg>
```

그림 9 — 전체 자세 오차(yaw 포함). 9축 gate 없음(빨강)은 자기 교란(회색 띠)에서 크게 끌려가고 오래 회복하지 못한다. gate를 넣은 9축(파랑)은 바닥에 붙어 있다. 6축(주황)은 천천히 yaw drift가 쌓인다.

> 함정: |m| gating은 **세기가 변하는 교란만** 잡는다. 교란 벡터가 지구 자기장과 수직에 가까우면 세기는 거의 안 변하고 방향만 틀어진다. 실무에서는 세기 + 복각(dip angle = 자기장과 수평면 사이 각) 두 가지를 함께 본다. 그래도 못 잡는 교란이 있으므로 heading의 신뢰도(uncertainty)를 같이 출력하는 것이 좋다.

### 9.3 선가속도 gating — |a| ≈ g 검사의 한계

같은 아이디어를 가속도계에 쓰면 "|a|가 g에서 벗어나면 tilt 보정을 건너뛴다". 효과를 gain 스윕과 같이 본다.

```python
# 튜닝: gain이 크면 noise·선가속도에 흔들리고, 작으면 bias·drift를 못 잡는다
import numpy as np
from g3_sim import t, dt, gyro, acc, q_true, up_true, tilt_err_deg, s1, s2
from g3_filters import complementary, mahony, madgwick
q0 = q_true[0]
up = lambda Q: np.array([[2*(x*z - w*y), 2*(y*z + w*x), 1 - 2*(x*x + y*y)] for w, x, y, z in Q])
quiet = (t > 5) & ~s1 & ~s2
def row(name, e):
    print(f"{name:30s} 조용 RMS {np.sqrt((e[quiet]**2).mean()):5.2f} | 흔들기 max {e[s1].max():5.2f} | 점프 max {e[s2].max():5.2f}")
for a in [0.9, 0.98, 0.99, 0.995, 0.999]:
    row(f"compl. α={a} (τ={a*dt/(1-a):.2f}s)", tilt_err_deg(complementary(gyro, acc, dt, up_true[0], a)))
for b in [0.01, 0.05, 0.3]:
    row(f"Madgwick β={b}", tilt_err_deg(up(madgwick(gyro, acc, dt, q0, b))))
for kp, ki, gate in [(1, 0.05, None), (5, 0.05, None), (1, 0.05, 0.5), (1, 0.05, 0.2)]:
    row(f"Mahony Kp={kp} Ki={ki} gate={gate}", tilt_err_deg(up(mahony(gyro, acc, dt, q0, kp, ki, gate)[0])))
```

```text
compl. α=0.9 (τ=0.09s)         조용 RMS  0.40 | 흔들기 max 15.84 | 점프 max  0.40
compl. α=0.98 (τ=0.49s)        조용 RMS  0.41 | 흔들기 max  5.72 | 점프 max  0.45
compl. α=0.99 (τ=0.99s)        조용 RMS  0.66 | 흔들기 max  3.11 | 점프 max  0.77
compl. α=0.995 (τ=1.99s)       조용 RMS  1.23 | 흔들기 max  2.11 | 점프 max  1.39
compl. α=0.999 (τ=9.99s)       조용 RMS  5.19 | 흔들기 max  5.87 | 점프 max  5.44
Madgwick β=0.01                조용 RMS  0.37 | 흔들기 max  2.23 | 점프 max  0.34
Madgwick β=0.05                조용 RMS  0.11 | 흔들기 max  1.62 | 점프 max  0.35
Madgwick β=0.3                 조용 RMS  0.29 | 흔들기 max  7.28 | 점프 max  0.83
Mahony Kp=1 Ki=0.05 gate=None  조용 RMS  0.32 | 흔들기 max  3.04 | 점프 max  0.19
Mahony Kp=5 Ki=0.05 gate=None  조용 RMS  0.39 | 흔들기 max 11.03 | 점프 max  0.23
Mahony Kp=1 Ki=0.05 gate=0.5   조용 RMS  0.35 | 흔들기 max  1.37 | 점프 max  1.77
Mahony Kp=1 Ki=0.05 gate=0.2   조용 RMS  0.36 | 흔들기 max  1.16 | 점프 max  1.90
```

출력에서 볼 것:

- **complementary α 스윕**: α를 키우면(τ 증가) 흔들기 최대 오차가 15.8° → 2.1°로 줄지만, 조용한 구간 RMS가 0.40° → 1.23° → 5.19°로 커진다(bias × τ). 이 데이터에서는 τ ≈ 0.5–1 s가 균형점이다. §6.2의 이야기가 숫자로 나온다.
- **Madgwick β**: 0.01은 bias를 못 따라가고(조용 0.37°), 0.3은 흔들기에 7.3°까지 끌려간다. 0.05 근처가 가장 좋다. Madgwick 보고서의 실험에서도 IMU 버전은 0.03 근처 값이 좋은 결과를 냈다고 보고했는데, 최적값은 센서 noise·bias·움직임에 따라 달라진다.
- **Mahony Kp=5**: 가속도계를 너무 믿어서 흔들기에 11°.
- **gating (gate=0.5 m/s²)**: 흔들기 최대가 3.04° → 1.37°로 줄었다. 하지만 **점프 구간에서는 오히려 0.19° → 1.77°로 나빠졌다**. 이유는 §5.3에서 본 그대로다. 수직 점프는 |a|를 크게 바꾸지만 방향은 정확해서 원래 보정에 문제가 없었는데, gate가 보정을 꺼 버렸다(그 사이 자이로 bias로 흘러감). 반대로 수평 흔들기는 방향을 22° 틀지만 |a|는 조금만 바뀌어 gate를 일부 통과한다.

**정리**: |a| gating은 싸고 효과가 있지만 만능이 아니다. 실무에서는 (1) |a|와 g의 차이에 비례해 gain을 부드럽게 줄이는 **적응형 gain**(ESKF의 적응형 R과 같은 생각), (2) 자이로 크기가 클 때(빠른 회전 중) 보정 줄이기, (3) 보행처럼 주기적인 움직임은 창 평균 후 보정, (4) innovation 크기가 너무 크면 버리는 **χ² 검정**(KF 계열)을 조합한다.

### 9.4 초기화와 타이밍

- **초기화**: 필터를 identity quaternion에서 시작하면 수렴까지 Kp나 β에 따라 수 초가 걸린다. 표준 방법은 첫 정지 구간(또는 첫 수십 샘플 평균)의 가속도로 roll/pitch를 직접 계산하고(§5.1), 지자기계가 있으면 tilt 보정한 자기장으로 yaw를 계산해 초기 quaternion을 만드는 것이다. 또는 처음 1–2초만 gain을 크게 했다가 줄인다(fast convergence 모드). 자이로 bias도 부팅 직후 정지 구간이 있으면 평균으로 초기화한다(§1.3).
- **샘플 타이밍**: 이 노트를 만들면서 실제로 겪은 버그가 있다. ESKF에서 "자이로로 다음 시각까지 예측한 자세"를 "이전 시각의 가속도 측정"과 비교하는 순서로 짰더니, 다른 것은 모두 같은데 tilt RMS가 **0.06° → 0.33°**로 5배 나빠졌다. 측정과 예측의 시각이 한 샘플(10 ms) 어긋난 것이다. 손목이 1.5 rad/s로 돌면 10 ms에 0.86°가 돌아간다. 같은 이유로 가속도계와 자이로 FIFO의 타임스탬프가 어긋나 있거나(G7), 자이로 샘플이 "순간값"인지 "구간 평균"인지(센서 내부 디지털 필터 지연, G1) 모르면 같은 크기의 오차가 생긴다.
- **dt**: 고정 `dt = 1/ODR`을 쓰지 말고 실제 타임스탬프 차이를 쓴다. 센서 ODR은 내부 발진기 오차로 수 % 어긋날 수 있다(G2, G7). 1% 틀린 dt는 자이로 scale 1% 오차와 같다.

### 9.5 튜닝 요약

| 증상 | 의심 | 조치 |
|---|---|---|
| 정지 상태에서 tilt가 천천히 한쪽으로 흐른다 | gyro bias가 크고 보정 gain이 작다 | Ki 추가(Mahony), β 증가, 부팅 시 bias 평균, 온도 보상 |
| 정지 상태에서 tilt가 지글거린다 | 가속도계를 너무 믿는다 (Kp·β 큼, α 작음) | gain 감소, 가속도 저역통과 |
| 걷거나 흔들 때 tilt가 출렁인다 | 선가속도가 가짜 중력으로 들어간다 | gain 감소, ‖a‖ 기반 적응형 gain, gating |
| 빠르게 돌린 직후 오차가 크다 | 자이로 saturation, scale 오차, dt 오류 | FSR 확인(G1), scale 보정, 타임스탬프 사용 |
| heading이 특정 장소에서 틀어진다 | 외부 자기 교란 | ‖m‖·dip gating, 수직축 투영 |
| heading이 방향에 따라 다르게 틀린다 | hard/soft iron 보정 부족 | 타원체 재피팅, 보정 반지름 표준편차 확인 |
| 부팅 직후 몇 초 엉뚱한 값 | 초기화 없음 | 가속도·지자기로 초기 자세 계산 |

---

## 10. 퓨전할까, 모델이 raw에서 배우게 할까

edge ML 엔지니어가 자주 받는 질문: "퓨전 필터를 넣어야 하나, 그냥 raw IMU를 CNN에 넣으면 되나?"

### 10.1 퓨전 출력이 좋은 특징이 되는 경우

- **착용 방향이 사람마다 다를 때**: 중력 벡터 `g_b`를 알면 가속도를 "수직 성분 / 수평 성분 크기"로 분해할 수 있다(B7 §2.2의 방향 불변 특징). B7은 창 평균으로 중력을 추정했지만, 격한 움직임에서는 평균이 중력과 어긋난다. complementary 출력 `v`(§6.3)를 쓰면 매 샘플 정확한 중력 방향을 얻는다.
- **선가속도가 필요할 때**: `a_lin = a − g·v`. 낙상(자유낙하 → 충격), 걸음 수, 제스처의 "힘"은 중력이 빠진 신호가 더 깨끗하다.
- **world 좌표가 의미 있을 때**: "손을 위로 든다", "고개를 왼쪽으로 돌린다"는 world 기준 동작이다. `R·a`로 world 좌표 가속도를 만들면 기기 방향과 무관해진다(yaw까지 필요하면 9축).
- **데이터가 적을 때**: 회전 불변성을 모델이 데이터에서 배우려면 많은 사용자·방향 데이터(또는 회전 augmentation)가 필요하다. 물리 지식(퓨전)을 넣으면 작은 데이터·작은 모델로도 된다 — B7의 "고전 ML 먼저"와 같은 철학이다.

### 10.2 raw를 그대로 넣는 게 나은 경우

- **모델이 이미 크고 데이터가 많을 때**: CNN/GRU가 자이로·가속도의 미세한 패턴(진동, 미세 떨림)에서 직접 배울 수 있다. 퓨전 필터는 저역통과 성질이 있어서 고주파 정보를 지운다.
- **퓨전 필터의 실패가 모델을 오염시킬 때**: 자기 교란에 끌려간 heading, 초기 수렴 중의 엉터리 자세가 특징으로 들어가면 모델이 이상한 것을 배운다.
- **학습–배포 불일치**: 학습 데이터에서 쓴 퓨전 파라미터(α, β, 초기화)와 기기 펌웨어의 파라미터가 다르면 A2 §4(학습 때 통계를 펌웨어에 그대로)·C8(golden 비교)에서 본 train/serve skew가 생긴다. 퓨전 코드를 **Python과 C에서 같은 구현**(golden 비교, §11)으로 유지해야 한다.

실무 절충: **보정은 항상**(bias·scale·축 remap은 모델이 배울 이유가 없는 기기별 오차), **퓨전은 과제에 따라**. 보정된 raw 6채널 + 중력 벡터 3채널을 같이 넣고, 학습 시 무작위 회전 augmentation을 쓰는 조합이 흔하다.

### 10.3 센서 허브와 칩 내장 퓨전

많은 IMU와 센서 허브가 퓨전을 **칩 안에서** 해서 quaternion·중력·선가속도를 바로 내 준다. 예를 들어 Bosch BNO055(내장 MCU + 퓨전 펌웨어), Bosch BHI260 계열(프로그래머블 센서 허브), TDK InvenSense의 DMP가 있는 칩들, ST LSM6DSV 계열의 저전력 sensor fusion 블록 등이 있다(기능·정확도·전류는 제품마다 다르므로 데이터시트에서 확인해야 한다). Android는 `TYPE_GRAVITY`, `TYPE_LINEAR_ACCELERATION`, `TYPE_ROTATION_VECTOR`, `TYPE_GAME_ROTATION_VECTOR`(지자기 없는 6축 버전) 같은 가상 센서로 퓨전 결과를 표준화해 제공한다.

예를 들어 Hark 같은 웨어러블이라면(추정): always-on 구간에서는 IMU 내장 퓨전/제스처 블록이 µA 수준으로 돌고, 메인 MCU는 깨어났을 때만 결과를 읽는 구조가 배터리에 유리하다. 대신 **블랙박스**라서 gain·gating을 바꿀 수 없고, 칩 펌웨어 버전에 따라 동작이 달라질 수 있다. 모델 학습 데이터를 모을 때는 "칩 퓨전 출력 + raw"를 둘 다 로깅해 두면 나중에 선택지가 남는다.

---

## 11. 임베디드 관점에서 다시 보기

### 11.1 Mahony 업데이트를 C로 — numpy와 golden 비교

MCU에 올릴 형태(float32, 1차 Euler 적분 + 정규화, 상태 36 B)로 Mahony 6축 업데이트를 쓴다. 호스트에서 numpy 구현과 같은 입력을 넣어 결과를 비교하고, 시간도 잰다.

```c
/* mahony.c — Mahony 6축 업데이트 1회 (float32, Cortex-M4F를 염두에 둔 형태) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct { float q0, q1, q2, q3, ix, iy, iz, kp, ki; } mahony_t;

static inline float inv_sqrt(float x) { return 1.0f / sqrtf(x); }   /* M4F: VSQRT + VDIV */

void mahony_update(mahony_t *s, float gx, float gy, float gz,
                   float ax, float ay, float az, float dt)
{
    float q0 = s->q0, q1 = s->q1, q2 = s->q2, q3 = s->q3;
    float n = ax * ax + ay * ay + az * az;
    if (n > 0.0f) {
        n = inv_sqrt(n); ax *= n; ay *= n; az *= n;
        float vx = 2.0f * (q1 * q3 - q0 * q2);          /* 추정 '위' = Rᵀ e_z */
        float vy = 2.0f * (q0 * q1 + q2 * q3);
        float vz = 1.0f - 2.0f * (q1 * q1 + q2 * q2);
        float ex = ay * vz - az * vy;                    /* e = â × v */
        float ey = az * vx - ax * vz;
        float ez = ax * vy - ay * vx;
        s->ix += s->ki * ex * dt; s->iy += s->ki * ey * dt; s->iz += s->ki * ez * dt;
        gx += s->kp * ex + s->ix; gy += s->kp * ey + s->iy; gz += s->kp * ez + s->iz;
    }
    float h = 0.5f * dt;                                 /* q += ½ q⊗ω dt */
    s->q0 = q0 + h * (-q1 * gx - q2 * gy - q3 * gz);
    s->q1 = q1 + h * ( q0 * gx + q2 * gz - q3 * gy);
    s->q2 = q2 + h * ( q0 * gy - q1 * gz + q3 * gx);
    s->q3 = q3 + h * ( q0 * gz + q1 * gy - q2 * gx);
    n = inv_sqrt(s->q0 * s->q0 + s->q1 * s->q1 + s->q2 * s->q2 + s->q3 * s->q3);
    s->q0 *= n; s->q1 *= n; s->q2 *= n; s->q3 *= n;
}

int main(void)
{
    enum { N = 6000 };
    static float in[N][6], out[N][4];
    FILE *f = fopen("imu_in.bin", "rb");
    if (!f || fread(in, sizeof in, 1, f) != 1) { puts("input?"); return 1; }
    fclose(f);
    mahony_t s0 = { 0.9400f, 0.1700f, 0.0800f, 0.2800f, 0, 0, 0, 1.0f, 0.05f }, s;
    float n = inv_sqrt(s0.q0*s0.q0 + s0.q1*s0.q1 + s0.q2*s0.q2 + s0.q3*s0.q3);
    s0.q0 *= n; s0.q1 *= n; s0.q2 *= n; s0.q3 *= n;
    const int REP = 200;
    clock_t c0 = clock();
    for (int r = 0; r < REP; r++) {
        s = s0;
        for (int k = 0; k < N; k++) {
            out[k][0] = s.q0; out[k][1] = s.q1; out[k][2] = s.q2; out[k][3] = s.q3;
            mahony_update(&s, in[k][0], in[k][1], in[k][2], in[k][3], in[k][4], in[k][5], 0.01f);
        }
    }
    double ns = 1e9 * (double)(clock() - c0) / CLOCKS_PER_SEC / ((double)REP * N);
    printf("host: %.1f ns/update, sizeof(mahony_t) = %zu B\n", ns, sizeof(mahony_t));
    printf("final q = [%.5f %.5f %.5f %.5f], bias est = [%.3f %.3f %.3f] dps\n", s.q0, s.q1, s.q2, s.q3,
           -s.ix * 57.29578f, -s.iy * 57.29578f, -s.iz * 57.29578f);
    f = fopen("q_c.bin", "wb"); fwrite(out, sizeof out, 1, f); fclose(f);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 mahony.c -o mahony -lm     # 경고 0개
```

numpy 쪽에서 입력을 float32 바이너리로 내보내고, C 결과를 읽어 비교한다.

```python
# 같은 입력을 C로 넘기고(float32), C 결과를 numpy Mahony와 비교
import numpy as np, subprocess
from g3_sim import dt, gyro, acc, q_true, up_true, tilt_err_deg
from g3_filters import mahony
from g3_quat import qangle
np.hstack([gyro, acc]).astype(np.float32).tofile("imu_in.bin")
print(subprocess.run(["./mahony"], capture_output=True, text=True).stdout, end="")
qc = np.fromfile("q_c.bin", np.float32).reshape(-1, 4).astype(float)
qp, b = mahony(gyro, acc, dt, q_true[0], 1.0, 0.05)
d = np.rad2deg([qangle(a, c) for a, c in zip(qp, qc)])
print(f"numpy(지수맵, float64) vs C(1차, float32): 자세 차이 max {d.max():.4f}°, 평균 {d.mean():.4f}°")
up = lambda Q: np.array([[2*(x*z - w*y), 2*(y*z + w*x), 1 - 2*(x*x + y*y)] for w, x, y, z in Q])
print(f"C 결과 tilt RMS = {np.sqrt((tilt_err_deg(up(qc))**2).mean()):.2f}° | numpy bias 추정 {np.rad2deg(b).round(3)} dps")
```

```text
host: 58.0 ns/update, sizeof(mahony_t) = 36 B
final q = [0.52896 0.80957 -0.18444 0.17542], bias est = [0.525 -0.332 0.295] dps
numpy(지수맵, float64) vs C(1차, float32): 자세 차이 max 0.0488°, 평균 0.0056°
C 결과 tilt RMS = 0.54° | numpy bias 추정 [ 0.525 -0.332  0.295] dps
```

출력에서 볼 것: (1) C(float32, 1차 적분)와 numpy(float64, 지수맵)의 자세 차이는 60초 동안 최대 0.049°, 평균 0.006° — 퓨전 오차(0.5°)보다 한 자릿수 작다. bias 추정은 소수 셋째 자리까지 같다. (2) 이 Mac 호스트에서 업데이트 1회에 약 50–60 ns. 이 숫자는 MCU 성능의 근거가 아니다 — 아래처럼 명령어 수로 추정한다.

### 11.2 Cortex-M4F 명령어 수 세기

같은 함수(`mahony_update`만 떼어 `sqrtf → __builtin_sqrtf`로 바꾼 `m4fn.c`)를 Cortex-M4F용으로 크로스 컴파일해 어셈블리의 명령어를 센다. Apple clang은 ARM 타깃 코드 생성이 되므로 별도 툴체인 없이 가능하다(링크는 안 한다).

```sh
cc --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
   -O2 -fno-math-errno -ffreestanding -std=c11 -Wall -Wextra -S m4fn.c -o m4.s
awk '/^mahony_update:/,/\.Lfunc_end/' m4.s | grep -oE '^\s+v[a-z]+(\.f32)?' | sort | uniq -c | sort -rn | head -9 | tr '\t' ' '
echo "전체 명령어 수: $(awk '/^mahony_update:/,/\.Lfunc_end/' m4.s | grep -cE '^\s+[a-z]')"
```

```text
  51  vmul.f32
  27  vadd.f32
   9  vsub.f32
   9  vldr
   7  vstr
   2  vsqrt.f32
   2  vneg.f32
   2  vmov.f32
   2  vdiv.f32
전체 명령어 수: 119
```

`-fno-math-errno`가 없으면 `sqrtf`가 errno 처리를 위해 라이브러리 호출(`bl sqrtf`)로 남는다 — 실제로 처음 컴파일했을 때 `bl` 2개가 나왔다. GCC/Clang 임베디드 빌드에서 자주 놓치는 옵션이다.

**손으로 센 연산 수와 맞춰 보기**:

```
가속도 정규화        곱 3 + 곱 3, 덧셈 2, sqrt 1, div 1
추정 '위' v          곱 9, 덧셈 4
외적 e               곱 6, 뺄셈 3
적분항 Ki·e·dt       곱 6, 덧셈 3
보정 gx += Kp·e + i  곱 3, 덧셈 6
q += ½ q⊗ω dt        곱 17, 덧셈/뺄셈 12 (+ 부호 반전)
정규화               곱 8, 덧셈 3, sqrt 1, div 1
합계                 곱 ≈ 55, 덧셈/뺄셈 ≈ 33, sqrt 2, div 2
컴파일러              vmul 51 + vnmul 1, vadd 27 + vsub 9, vsqrt 2, vdiv 2   ← 거의 일치
```

**사이클 추정** (Cortex-M4 TRM 기준: VADD/VSUB/VMUL 1 사이클, VDIV·VSQRT 14 사이클, load/store 추가 사이클과 파이프라인 stall은 무시한 하한):

```
(119 − 4) · 1  +  4 · 14  ≈ 171 사이클   → 여유를 두고 약 200–250 사이클
64 MHz MCU:  ≈ 3–4 µs / 업데이트
100 Hz 업데이트:  ≈ 0.03–0.04 % CPU
```

말로 하면: 6축 퓨전 자체는 MCU에서 거의 공짜다. 비용의 대부분은 퓨전이 아니라 **센서를 깨워 두고 데이터를 읽는 전력**(G2의 FIFO 배치, ODR 선택)에 있다. 실제 숫자는 DWT `CYCCNT`로 재야 한다 — 위 값은 하한 추정이다.

### 11.3 필터별 비용 비교

| 필터 | 상태 메모리 | 업데이트당 연산 (대략) | 비고 |
|---|---|---|---|
| complementary (벡터) | 3 float | 곱 약 20, sqrt 1, div 1 | 중력 벡터만 필요할 때 최저 비용 |
| Mahony 6축 | 7 float + gain | 위 실측 119 명령어 | bias 학습 포함 |
| Madgwick 6축 | 4 float + β | Mahony와 같은 자릿수 | 공개 C 구현이 최적화돼 있다 |
| 1축 KF (2상태) | 2 + 4 float | 수십 연산 | 축별 독립 가정 |
| ESKF 6상태 | 4 + 3 + 36 float | 약 1,100 MAC (구조 미활용 시) | F의 희소성·대칭 이용 시 수백 MAC |
| Mahony 9축 | 7 float + gain | 6축의 약 1.5–2배 | 자기장 회전 2번 + gating |

ESKF의 대략 계산: 예측 `F·P·Fᵀ` = 2 · 6³ = 432 MAC, 갱신 `H·P·Hᵀ`·`P·Hᵀ`·`K`·`(I − KH)·P` ≈ 600 MAC, 3×3 역행렬 수십 연산 → 약 1,100 MAC. F와 H의 0 블록을 활용하고 P의 대칭성을 쓰면 크게 줄어든다. M4F에서 수천 사이클이어도 100 Hz면 1% 미만이지만, 여러 센서를 다루는 큰 상태(VIO는 수십~수백 상태)로 가면 O(n³)이 커진다.

### 11.4 고정소수점과 정밀도

- Cortex-M4F/M33/M55처럼 단정밀도 FPU가 있으면 float32가 정답이다. M0+처럼 FPU가 없으면 Q15/Q31 quaternion 구현(또는 칩 내장 퓨전)을 고려한다.
- quaternion 성분은 [−1, 1]이라 Q1.30 같은 형식에 잘 맞는다. 단, `½·q⊗ω·dt` 항은 매우 작다(2 rad/s · 0.005 = 0.01). Q15로 하면 해상도 3e-5라서 정지 상태의 작은 bias 보정 항(1e-5 rad 수준)이 **0으로 반올림되어 사라진다** — 고정소수점 누산에서 작은 증분이 반올림에 먹히는 현상이다. 적분 상태만 Q31처럼 더 넓은 형식으로 두거나 반올림 잔차를 누적해서 해결한다.
- 정규화는 매 스텝 할 필요 없이 몇 스텝마다 해도 되지만, float32에서는 매 스텝 해도 싸다. `1/√x`를 빠른 근사(Newton 1회)로 바꿀 때는 정확도를 golden 비교로 확인한다.
- 보정 행렬(`K⁻¹`, 지자기 `W`)은 float로 저장하고, raw int16 → 물리 단위 변환에서 같이 적용하면 곱셈이 추가되지 않는다(scale을 행렬에 접어 넣기 — B1의 Conv+BN folding과 같은 발상).

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 축 remap 행렬의 det가 −1 | 가속도는 정상인데 퓨전이 엉뚱하게 돈다 (예제에서 tilt RMS 0.69° → 63°) | 거울상 좌표에서 자이로(pseudovector) 회전 방향이 반대 | `R_bs`의 det = +1 확인, 물리 테스트 3종 (평평·한쪽 들기·반시계 회전) |
| scipy quaternion 순서 혼동 | 회전이 완전히 엉뚱함 | scipy `as_quat()`은 `[x, y, z, w]`, 펌웨어는 보통 `[w, x, y, z]` | 경계에서 명시적으로 재배열, 단위 테스트로 90° 회전 확인 |
| quaternion 곱 순서 반대 | 한 축만 돌릴 때는 맞고 여러 축이면 틀림 | body 축 증분은 오른쪽 곱 `q ⊗ Δq` | 두 축 연속 회전 테스트 (예제의 q1⊗q2 vs q2⊗q1) |
| 정규화 누락 | 수 분 후 자세가 이상하게 커지거나 작아짐 | 1차 적분이 ‖q‖를 1에서 밀어냄 | 매 스텝 (또는 N스텝마다) 정규화 |
| 25°C bias 하나로 고정 보정 | 착용·충전 후 drift 증가 | bias 온도계수 | 온도 스윕 fit, 칩 내부 온도 사용 |
| 6-position 지그 기울어짐 | 보정 후에도 특정 자세에서 ‖a‖ ≠ g | 참값 가정이 틀림 | 지그 수평 확인, multi-position 타원체 방법 |
| 지자기 점이 한쪽에만 분포 | 피팅 결과가 기기마다 들쭉날쭉 | 정규방정식이 ill-conditioned | 구면 커버리지 검사 후 피팅, 보정 반지름 표준편차로 품질 판정 |
| gain을 정지 상태에서만 튜닝 | 걷거나 흔들 때 tilt가 출렁임 | 선가속도가 가짜 중력 | 움직임 데이터로 튜닝, 적응형 gain, gating |
| 측정과 예측 시각 불일치 | 원인 모를 고정 오차 (예제에서 0.06° → 0.33°) | update/predict 순서, FIFO 타임스탬프 어긋남 | 같은 시각끼리 비교, 타임스탬프 기반 dt |
| 자기 교란 무시 | 실내 특정 위치에서 heading 수십 도 오류, 이후 회복 느림 | 외부 자기장이 I항까지 오염 | ‖m‖·dip gating, 자기 항을 수직축으로 투영 |

---

## 13. 면접에서 이렇게 말한다

**Q.** How do you calibrate accelerometer bias and scale?

**A.** 오차 모델을 `y = K·x + b`(K = scale × misalignment)로 세우고, 중력을 기준으로 쓴다. 6방향으로 놓으면 참값이 ±g로 알려져 있으니 반대 자세 쌍의 합에서 bias, 차에서 scale이 나오고, 12개 파라미터를 최소제곱으로 한 번에 풀 수 있다. 지그가 없으면 "모든 정지 자세에서 보정 후 크기가 g"라는 조건으로 타원체를 피팅한다. 공장에서 자세별 settle·정지 판정·보정 후 sanity check를 넣고, 결과 48 B를 NVM에 저장한다. 온도계수는 챔버 스윕으로 따로 fit한다.

> "I model the accelerometer as y = K·x + b, where K combines scale and misalignment. Gravity is a free reference: in a six-position test the true input is ±1 g along each axis, so the sum of opposite poses gives the bias and the difference gives the scale, and a least-squares solve recovers all twelve parameters at once. Without a precise fixture I fit an ellipsoid using the constraint that the corrected magnitude equals 1 g in every static pose. In production I add settle time, a stillness check, and a post-calibration sanity check, and I characterize the temperature coefficient separately in a chamber sweep."

**Q.** Why use quaternions instead of Euler angles?

**A.** Euler 각은 pitch ±90°에서 gimbal lock이 생겨서 yaw와 roll이 구별되지 않고, 자이로 적분식이 1/cos(pitch)로 발산한다(89.9°에서 계수 564). quaternion은 특이점이 없고, 숫자 4개에 제약 하나라 정규화가 싸고, 적분이 곱셈·덧셈뿐이고, 합성·보간이 자연스럽다. 회전행렬도 특이점은 없지만 숫자 9개라 재직교화 비용이 크다. Euler 각은 로그·UI 표시용으로만 마지막에 변환한다.

> "Euler angles have a singularity at ±90 degrees of pitch — gimbal lock — where yaw and roll become indistinguishable and the Euler-rate equations blow up as one over cosine of pitch. Unit quaternions have no singularity, need only a cheap renormalization, integrate with multiplies and adds, and compose and interpolate cleanly. Rotation matrices are also singularity-free but carry nine numbers and need re-orthogonalization. I keep the state as a quaternion and convert to Euler angles only for display or logging."

**Q.** Explain a complementary filter and how you choose alpha.

**A.** 자이로 적분은 고주파에 강하고 저주파에서 drift하고, 가속도계 tilt는 반대다. 그래서 가속도계 각도는 저역통과, 자이로 각도는 고역통과로 걸러 더하는데, 두 필터의 합이 1이라 신호는 그대로 통과한다. α는 시정수 τ = α·dt/(1 − α)로 정한다. 차단 주파수가 걷기·손동작(1–5 Hz)보다 충분히 낮도록 τ를 길게 하되, 남은 gyro bias × τ가 허용 오차보다 작도록 한다. 보통 τ 0.5–2 s, 100 Hz면 α 0.98–0.995. bias가 크면 적분항(Mahony)을 추가한다.

> "A gyro integrates well at high frequency but drifts at low frequency, while accelerometer tilt is accurate on average but corrupted by motion. The complementary filter low-passes the accelerometer angle and high-passes the integrated gyro angle; the two transfer functions sum to one, so the true angle passes undistorted. I pick alpha from a time constant tau equal to alpha times dt over one minus alpha: long enough that the crossover sits well below the motion band, about one to five hertz for wrist motion, but short enough that residual gyro bias times tau stays within the error budget. That usually means tau between half a second and two seconds; if bias is significant I add an integral term, which is essentially Mahony's filter."

**Q.** Mahony or Madgwick versus a Kalman filter — how do you choose?

**A.** Mahony·Madgwick은 고정 gain 비선형 complementary filter라서 연산이 100여 명령어로 작고 튜닝 파라미터가 1–2개다. Kalman 계열(ESKF)은 공분산으로 gain을 매 순간 계산하고 bias 같은 추가 상태와 다른 센서(GPS, 카메라)를 원칙적으로 붙일 수 있지만, 6×6 이상 행렬 연산과 Q·R 튜닝이 필요하다. 정상상태 KF는 complementary + bias 적분기와 같은 구조라서, 센서가 IMU뿐이고 MCU 예산이 빠듯하면 Mahony/Madgwick, 여러 센서를 합치거나 불확실성 출력이 필요하면 ESKF를 고른다.

> "Mahony and Madgwick are fixed-gain nonlinear complementary filters: on the order of a hundred FPU instructions per update and one or two tuning knobs. A Kalman filter, typically an error-state EKF for attitude, computes the gain from covariances, naturally estimates extra states like gyro bias, and can fuse other sensors such as GPS or vision, at the cost of six-by-six or larger matrix math and Q/R tuning. In steady state a Kalman filter reduces to a complementary filter with a bias integrator, so for an IMU-only wearable on a small MCU I'd start with Mahony or Madgwick, and move to an ESKF when I need multi-sensor fusion or a calibrated uncertainty."

**Q.** How would you estimate gravity to remove it from accelerometer data?

**A.** 가장 싼 방법은 가속도의 저역통과(또는 창 평균)지만, 격한 움직임에서는 평균이 중력과 어긋나고 회전 중에는 지연이 생긴다. 더 정확한 방법은 자이로로 중력 방향을 회전시켜 따라가고 가속도계로 천천히 보정하는 퓨전이다 — body 좌표 '위' 벡터 v에 대해 v̇ = −ω × v로 예측하고 a/|a| 쪽으로 섞는다. 그러면 선가속도는 a − g·v. |a|가 g에서 많이 벗어날 때 gain을 줄여 선가속도가 중력 추정에 새지 않게 한다. 칩이나 OS가 gravity 가상 센서를 제공하면 그것도 후보지만 동작을 바꿀 수 없다.

> "The cheapest approach is a low-pass filter or window mean of the accelerometer, but that lags during rotation and is biased during vigorous motion. Better is to track the gravity direction with the gyro and correct it slowly with the accelerometer: propagate the body-frame up vector with v-dot equals minus omega cross v, blend toward the normalized accelerometer, and subtract g times v to get linear acceleration. I reduce the correction gain when the accelerometer norm deviates from 1 g so motion doesn't leak into the gravity estimate. If the IMU or the OS provides a gravity virtual sensor I'd evaluate it too, but I can't tune it."

**Q.** Your heading drifts by 30 degrees when the user sits at a certain desk. What's going on?

**A.** 외부 자기 교란(철제 책상, 노트북 스피커)일 가능성이 높다. 보정(hard/soft iron)은 기기와 같이 도는 왜곡만 없앨 수 있고, 외부 교란은 런타임에 감지해야 한다. 자기장 세기와 복각이 보정 때 값에서 벗어나면 자기 보정을 끄고 자이로로 버틴다. 자기 오차 항은 수직축으로 투영해서 tilt에 새지 않게 하고, 적분항(bias 추정)도 그동안 멈춘다. 예제에서 gating 없이 24° 끌려가던 것이 gating 후 2° 이내로 줄었다.

> "Most likely an external magnetic disturbance — a steel desk or laptop speaker. Hard- and soft-iron calibration only removes distortion that rotates with the device; external fields need runtime detection. I gate the magnetometer update when the field magnitude or dip angle departs from the calibrated reference, coast on the gyro in the meantime, project the magnetic correction onto the vertical axis so it can't corrupt tilt, and freeze the bias integrator during the disturbance. In my simulation that took the worst-case error from about 24 degrees down to about 2."

**Q.** How expensive is attitude fusion on a Cortex-M4F?

**A.** Mahony 6축 업데이트를 M4F로 컴파일해 보면 약 119 명령어(곱 52, 덧셈 36, sqrt·div 각 2)라서 200여 사이클, 64 MHz에서 수 µs다. 100 Hz면 CPU 0.05% 미만. ESKF는 공분산 연산으로 한 자릿수 더 크다. 그래서 퓨전 비용보다 센서를 켜 두는 전력과 데이터 읽기(FIFO 배치)가 지배적이고, `-fno-math-errno` 같은 컴파일 옵션 확인과 DWT 사이클 카운터 실측을 한다.

> "I compiled a six-axis Mahony update for Cortex-M4F: about 119 instructions — roughly 52 multiplies, 36 adds, two square roots and two divides — so around two hundred cycles, a few microseconds at 64 MHz, well under a tenth of a percent of CPU at 100 hertz. An ESKF is roughly an order of magnitude more because of covariance math. So the real cost is keeping the sensor powered and reading it efficiently via FIFO batching; I'd confirm compiler flags like -fno-math-errno and measure with the DWT cycle counter."

---

## 14. 직접 해보기

1. **손계산**: 어떤 가속도계의 z축이 +z 위일 때 9.95, −z 위일 때 −9.71 m/s²를 냈다. bias와 scale을 구하라. — 정답: b = (9.95 − 9.71)/2 = 0.12 m/s², s = (9.95 + 9.71)/(2 · 9.81) = 19.66/19.62 ≈ 1.002.
2. **손계산**: 100 Hz, α = 0.995인 complementary filter의 시정수와 차단 주파수는? gyro bias가 0.3 dps면 정상상태 tilt 오차는? — 정답: τ = 0.995 · 0.01/0.005 = 1.99 s, f_c = 1/(2π · 1.99) ≈ 0.08 Hz, 오차 ≈ 0.3 · 1.99 ≈ 0.6°.
3. **손계산**: `q = (cos30°, 0, sin30°, 0)`은 어떤 회전인가? 이 q로 (0, 0, 1)을 회전하면? — 정답: y축 둘레 60° 회전. (sin60°, 0, cos60°) = (0.866, 0, 0.5). 힌트: `v' = v + 2w(u × v) + 2u × (u × v)`.
4. **코드**: `ex_6pos.py`에서 "지그가 x축 둘레로 1° 기울어진" 상황을 만들어라(6개 자세의 참값 벡터를 모두 `Rx(1°)`로 돌려서 측정을 만들고, 피팅은 기울어지지 않은 참값으로). 추정된 M의 어떤 원소가 얼마나 틀어지는가? — 힌트: 1° ≈ 0.0175 rad 크기의 cross-axis 오차가 y–z 항에 나타난다. 그 다음 모든 정지 자세에서 |K⁻¹(y − b)| = g 조건만 쓰는 비선형 최소제곱(`scipy.optimize.least_squares`)으로 바꿔 보라.
5. **코드**: `ex_compare.py`의 Mahony를 identity quaternion에서 시작하도록 바꾸고, Kp = 1과 Kp = 5에서 tilt 오차가 1° 아래로 들어오는 시간을 재라. 그 다음 "처음 2초는 Kp = 5, 이후 Kp = 1"로 바꿔 수렴 시간과 흔들기 구간 최대 오차를 함께 비교하라. — 힌트: 수렴 시정수는 대략 1/Kp. 큰 gain은 초기화에만 쓴다.
6. **코드**: `g3_ahrs.py`의 |m| gate에 복각(dip angle) 검사를 추가하라: `dip = asin(−(m̂ · v̂))`가 보정 때 값(이 시뮬레이션에서 `asin(45/49.24) ≈ 66°`)에서 5° 이상 벗어나면 건너뛴다. 교란 벡터를 지구 자기장과 수직인 방향으로 바꿔 세기 gate만으로는 못 잡는 경우를 만들고 비교하라. — 힌트: 수직 교란은 세기를 √(F² + d²)로만 조금 바꾸지만 방향(복각)은 바로 바꾼다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| bias (offset) | 영점 오차 | 입력이 0일 때의 출력. 자이로에서는 적분되어 drift가 된다 |
| scale factor | 감도 오차 | 입력 대비 출력 비율의 오차. 1%면 360° 회전에 3.6° |
| misalignment (cross-axis) | 축 어긋남 | 감지 축이 서로 정확히 직교하지 않아 다른 축 성분이 섞이는 것 |
| 6-position calibration | 6방향 보정 | ±x, ±y, ±z를 중력 쪽으로 놓고 bias·scale·cross-axis를 푸는 방법 |
| hard iron | 경자성 왜곡 | 기기와 같이 도는 영구자석 → 지자기 구의 중심 이동 |
| soft iron | 연자성 왜곡 | 연자성체가 자기장을 휘게 함 → 구가 타원체로 |
| ellipsoid fitting | 타원체 피팅 | 이차형식 계수 9개를 선형 최소제곱으로 풀어 중심·모양을 복원 |
| specific force | 비력 | 가속도계가 실제로 재는 양 a − g. 정지 시 위쪽 1 g |
| ENU / NED | world 좌표 관례 | East-North-Up(로보틱스), North-East-Down(항공) |
| mounting rotation | 장착 회전 | sensor 축을 body 축으로 바꾸는 회전 `R_bs` (det = +1) |
| pseudovector | 축벡터 | 외적으로 정의되는 벡터(각속도, 자기장). 거울상에서 부호가 반대로 해석된다 |
| rotation matrix (SO(3)) | 회전행렬 | RᵀR = I, det = +1인 3×3 행렬 |
| Euler angles | 오일러 각 | yaw·pitch·roll 세 각. 순서 관례가 12가지 |
| gimbal lock | 짐벌 락 | pitch ±90°에서 yaw와 roll 축이 겹쳐 자유도 하나가 사라지는 표현 특이점 |
| axis-angle / rotation vector | 축-각 / 회전벡터 | 단위축 k와 각 θ, 또는 θ·k. 자이로 증분 ω·dt가 회전벡터 |
| unit quaternion | 단위 사원수 | (cos θ/2, sin θ/2 · k). q와 −q는 같은 회전 |
| Hamilton product | 해밀턴 곱 | quaternion 곱 ⊗. 회전 합성, 교환법칙 불성립 |
| tilt | 기울기 | roll·pitch. 가속도계의 중력 방향으로 관측 가능 |
| drift | 표류 | 자이로 bias·noise가 적분되어 자세 오차가 시간에 따라 커지는 것 |
| complementary filter | 상보 필터 | 가속도계 각도 저역통과 + 자이로 각도 고역통과, 두 필터 합 = 1 |
| time constant τ | 시정수 | τ = α·dt/(1 − α). 저역/고역 경계를 정한다 |
| Mahony filter | 마호니 필터 | 외적 오차를 PI로 자이로에 되먹임하는 SO(3) 위의 complementary filter |
| Madgwick filter | 매드윅 필터 | 중력(·자기장) 정렬 목적함수의 경사하강 1스텝을 매 샘플 적용, gain β |
| Kalman gain | 칼만 이득 | K = P·Hᵀ·S⁻¹. 예측과 측정의 신뢰도 비로 정해지는 보정 비율 |
| process / measurement noise (Q / R) | 과정 / 측정 잡음 | 모델이 얼마나 틀릴 수 있나 / 측정이 얼마나 noisy한가 |
| innovation | 혁신(잔차) | 측정 − 예측된 측정. 크기가 비정상이면 이상치 |
| EKF / ESKF | 확장 / 오차상태 칼만 필터 | 비선형 모델을 선형화. ESKF는 작은 오차 δθ만 추정하고 명목 상태에 주입 |
| AHRS / MARG | 자세·방위 기준 시스템 | 자이로+가속도+지자기 9축으로 heading까지 추정 |
| gating | 게이팅 | 측정이 모델 가정을 벗어나면(‖a‖ ≠ g, ‖m‖ ≠ F) 그 보정을 건너뛰는 것 |
| linear acceleration | 선가속도 | a − g·v. 중력을 뺀 움직임 성분 |
| sensor hub | 센서 허브 | 센서 데이터 처리·퓨전을 저전력 전용 코어에서 하는 칩/블록 |

---

## 16. 요약 & 체크리스트

IMU의 raw 값은 bias·scale·misalignment·온도·자기 왜곡이 섞인 "물리량 근사치"다. 보정은 이 오차를 `y = K·x + b`(지자기는 `A·m + h`) 모델로 세우고, 중력이나 지구 자기장 같은 공짜 기준으로 최소제곱 피팅해 펌웨어에 trim 값으로 넣는 일이다. 자세는 quaternion으로 들고 다닌다 — Euler 각은 gimbal lock이 있고, 회전행렬은 비싸다. 퓨전 필터들은 한 가족이다: complementary filter는 가속도계를 저역통과·자이로를 고역통과로 섞고, Mahony는 거기에 bias를 배우는 I항을 더하고, Madgwick은 같은 일을 경사하강으로 하며, 정상상태 Kalman filter는 complementary + bias 적분기와 같은 구조다. 같은 합성 데이터에서 tilt RMS가 gyro만 18.6°, accel만 5.0°, complementary 0.78°, Mahony 0.54°, Madgwick 0.31°, ESKF 0.06°였다. 실패는 대부분 모델 가정 위반에서 온다 — 선가속도(|a| gating의 한계), 자기 교란(gating과 수직축 투영), 축 remap의 handedness, 측정·예측 시각 불일치. 6축 Mahony는 M4F에서 약 120 명령어라 비용은 무시할 만하고, 진짜 비용은 센서 전력과 검증이다. ML 쪽에서는 보정은 항상, 퓨전 출력(중력·선가속도)은 데이터가 적거나 착용 방향이 다양할 때 좋은 특징이 된다.

- [ ] 6-position 측정값에서 bias와 scale을 손으로 계산하고, 12개 파라미터 최소제곱을 행렬로 쓸 수 있다
- [ ] 자이로 bias 평균의 정확도가 √N으로 좋아지는 이유와 그 한계(Allan, G1)를 설명할 수 있다
- [ ] hard iron과 soft iron이 지자기 구를 어떻게 바꾸는지 그리고, 타원체 피팅으로 되돌리는 절차를 말할 수 있다
- [ ] ENU/NED, specific force 부호, 장착 회전의 det = +1 조건을 설명하고 물리 테스트로 검증할 수 있다
- [ ] quaternion 곱과 벡터 회전을 손으로 계산하고, scipy의 xyzw 순서와 맞춰 볼 수 있다
- [ ] gimbal lock을 "표현의 특이점"으로 설명하고 수치로 보여 줄 수 있다
- [ ] 가속도계로 roll/pitch를 계산하고 yaw가 관측 불가인 이유를 말할 수 있다
- [ ] complementary filter의 α ↔ τ ↔ f_c 관계와 bias × τ 정상상태 오차를 계산할 수 있다
- [ ] Mahony(PI), Madgwick(경사하강), KF(공분산 기반 gain)의 관계와 선택 기준을 설명할 수 있다
- [ ] 2상태 [각도, bias] KF의 F, H, Q, R을 쓰고 구현할 수 있다
- [ ] |a|·|m| gating이 잡는 것과 못 잡는 것을 예로 설명할 수 있다
- [ ] 퓨전 업데이트의 MCU 비용을 명령어 수로 추정하고, 퓨전 출력을 모델 특징으로 쓸지 판단할 수 있다

---

## 참고 자료

- R. Mahony, T. Hamel, J.-M. Pflimlin, "Nonlinear Complementary Filters on the Special Orthogonal Group," IEEE Transactions on Automatic Control, 53(5), 2008 — Mahony filter 원논문.
- S. O. H. Madgwick, "An efficient orientation filter for inertial and inertial/magnetic sensor arrays," internal report, University of Bristol / x-io Technologies, 2010; S. O. H. Madgwick, A. J. L. Harrison, R. Vaidyanathan, "Estimation of IMU and MARG orientation using a gradient descent algorithm," IEEE ICORR 2011.
- J. Solà, "Quaternion kinematics for the error-state Kalman filter," arXiv:1711.02508, 2017 — quaternion 관례(Hamilton vs JPL)와 ESKF 유도를 가장 친절하게 정리한 문서. [arXiv](https://arxiv.org/abs/1711.02508)
- J. Diebel, "Representing Attitude: Euler Angles, Unit Quaternions, and Rotation Vectors," Stanford University, 2006 — 회전 표현 사이 변환 공식 모음.
- D. Tedaldi, A. Pretto, E. Menegatti, "A robust and easy to implement method for IMU calibration without external equipments," IEEE ICRA 2014 — 지그 없는 multi-position 보정.
- NXP(Freescale) Application Note AN4246, "Calibrating an eCompass in the Presence of Hard- and Soft-Iron Interference" — 지자기 타원체 보정의 실무 해설.
- G. Welch, G. Bishop, "An Introduction to the Kalman Filter," UNC-Chapel Hill TR 95-041 — KF 입문.
- P. D. Groves, "Principles of GNSS, Inertial, and Multisensor Integrated Navigation Systems," 2nd ed., Artech House, 2013 — 관성 항법·센서 오차 모델의 표준 교재.
- scipy `Rotation` 문서 — quaternion 순서(scalar-last), Euler 순서 표기. [docs.scipy.org](https://docs.scipy.org/doc/scipy/reference/generated/scipy.spatial.transform.Rotation.html)
- ROS REP 103, "Standard Units of Measure and Coordinate Conventions" — ENU 관례. [ros.org](https://www.ros.org/reps/rep-0103.html)
- Android 센서 문서 — gravity, linear acceleration, rotation vector 가상 센서. [developer.android.com](https://developer.android.com/guide/topics/sensors/sensors_motion)
- Arm Cortex-M4 Technical Reference Manual — FPU 명령어 사이클 수(VDIV/VSQRT 14 사이클).
- 이 노트의 연결: A1(직교행렬·SVD), A2(평균의 분산), A3(경사하강), B7(중력 제거·방향 불변 특징), G1(noise·Allan variance), G2(FIFO·타임스탬프), G7(시간 동기화).
