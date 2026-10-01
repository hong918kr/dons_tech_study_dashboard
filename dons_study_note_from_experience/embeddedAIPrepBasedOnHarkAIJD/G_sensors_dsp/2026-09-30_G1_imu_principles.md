# G1. IMU 원리 — MEMS 가속도계·자이로·지자기, 그리고 데이터시트 읽는 법

> **이 노트를 다 읽으면**: 가속도계·자이로·지자기가 각각 "무엇을" 재는지(정지한 가속도계가 왜 +1 g를 읽는지 포함)를 부호까지 설명할 수 있다 · full-scale range, sensitivity, noise density, ODR, bias, 온도 drift, cross-axis 같은 데이터시트 항목을 숫자로 읽고 RMS noise를 손으로 계산할 수 있다 · Allan deviation을 직접 구현해 ARW와 bias instability를 곡선에서 읽을 수 있다 · 적분하면 왜 drift가 폭발하는지, 그리고 과제별(제스처·HAR·낙상)로 range와 ODR을 어떻게 고르는지 근거를 댈 수 있다
> **JD 연결**: "Hands-on experience with IMUs and other sensor types including accelerometers, gyroscopes" — study_prep_list G1: MEMS 가속도계·자이로·지자기, 6축/9축, full-scale range, noise density, bias, drift, ODR, bandwidth, 예: Bosch BMI270, ST LSM6DSx, TDK ICM-4xxxx
> **Don 기준 난이도**: 데이터시트 읽기, ADC·필터·샘플링, 레지스터 단위의 bring-up, 측정 기반 디버깅은 이미 강하다(RF 칩 통합 경험이 그대로 통한다) / MEMS 기계 구조의 직관, specific force라는 개념, noise density와 Allan deviation, 적분 drift의 크기 감각, "ML 과제가 센서 설정을 어떻게 정하는가"는 새로 배운다
> **선행 노트**: A2(센서 noise·Gaussian·표준화), A6(바이너리 IMU 로그·raw count → 물리 단위), B7(IMU 모델·중력 분해) — 함께 읽을 노트: G2(인터페이스·FIFO), G3(calibration·fusion), G7(시간 동기화)

---

## 0. 큰 그림 — 이게 왜 필요한가

웨어러블 ML 엔지니어가 IMU를 만나는 순간은 대개 이렇다. 모델팀이 "제스처 인식 정확도가 실기기에서만 떨어진다"고 한다. 로그를 열어 보니 손목을 휘두르는 구간에서 가속도 값이 ±4.000 g에 딱 붙어 평평하다(포화). 또는 "고개 방향 추정이 1분 뒤에 20°씩 돌아가 있다"(자이로 bias 적분). 또는 "같은 모델인데 A 공장 보드에서만 착용 감지가 오작동한다"(PCB 응력으로 인한 offset 이동). 셋 다 **모델 문제가 아니라 센서 물리와 설정 문제**다. 그걸 알아보려면 센서가 무엇을 재고, 데이터시트의 숫자가 무슨 뜻인지 알아야 한다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 230">
<defs><marker id="g1ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <rect x="10" y="40" width="130" height="140" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="75" y="30" font-size="13" text-anchor="middle">물리 (G1)</text> <text x="75" y="70" font-size="12" text-anchor="middle">MEMS 구조</text> <text x="75" y="95" font-size="12" text-anchor="middle">→ C-to-V, ADC</text> <text x="75" y="120" font-size="12" text-anchor="middle">→ 디지털 LPF</text> <text x="75" y="145" font-size="12" text-anchor="middle">→ ODR 솎기</text> <text x="75" y="170" font-size="12" text-anchor="middle">int16 레지스터</text> <line x1="140" y1="110" x2="180" y2="110" stroke="currentColor" stroke-width="1.5" marker-end="url(#g1ah4)"/> <rect x="180" y="40" width="120" height="140" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="240" y="30" font-size="13" text-anchor="middle">인터페이스 (G2)</text> <text x="240" y="80" font-size="12" text-anchor="middle">SPI / I2C</text> <text x="240" y="105" font-size="12" text-anchor="middle">FIFO + watermark</text> <text x="240" y="130" font-size="12" text-anchor="middle">인터럽트</text> <line x1="300" y1="110" x2="340" y2="110" stroke="currentColor" stroke-width="1.5" marker-end="url(#g1ah4)"/> <rect x="340" y="40" width="140" height="140" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="410" y="30" font-size="13" text-anchor="middle">처리 (G3, G7)</text> <text x="410" y="75" font-size="12" text-anchor="middle">count → SI 단위</text> <text x="410" y="100" font-size="12" text-anchor="middle">calibration</text> <text x="410" y="125" font-size="12" text-anchor="middle">fusion / timestamp</text>
<text x="410" y="150" font-size="12" text-anchor="middle">윈도잉 (A6)</text> <line x1="480" y1="110" x2="520" y2="110" stroke="currentColor" stroke-width="1.5" marker-end="url(#g1ah4)"/> <rect x="520" y="40" width="130" height="140" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="585" y="30" font-size="13" text-anchor="middle">ML / 로깅</text> <text x="585" y="85" font-size="12" text-anchor="middle">모델 (B7)</text> <text x="585" y="110" font-size="12" text-anchor="middle">로그 저장 (H1)</text> <text x="585" y="135" font-size="12" text-anchor="middle">품질 감시 (H7)</text> <text x="330" y="215" font-size="12" text-anchor="middle">G1에서 정한 range·ODR·noise가 오른쪽 모든 단계의 상한을 정한다</text>
</svg>
```

그림 1 — IMU 데이터가 모델까지 가는 길. 이 노트(G1)는 맨 왼쪽 상자, 즉 "센서 안에서 무슨 일이 일어나고 데이터시트 숫자가 무엇을 약속하는가"를 다룬다. 레지스터·FIFO는 G2, calibration·자세 추정은 G3, 타임스탬프는 G7이 이어받는다.

펌웨어 엔지니어 관점의 비유를 하나 들면, IMU 데이터시트는 **ADC 데이터시트 + 기계 부품 사양서**를 합친 것이다. Don이 RF 칩 통합에서 보던 항목들이 거의 그대로 나온다.

| RF·ADC에서 보던 것 | IMU 데이터시트에서 대응하는 것 | 이 노트의 절 |
|---|---|---|
| ADC full-scale, bit 수 | full-scale range(±g, ±dps), int16 출력 | §4 |
| LSB 크기, gain 오차 | sensitivity(LSB/g), sensitivity tolerance | §4, §7 |
| noise floor (nV/√Hz, dBm/Hz) | noise density (µg/√Hz, mdps/√Hz) | §5 |
| 샘플링 레이트, anti-aliasing 필터 | ODR, 디지털 LPF bandwidth | §6 |
| DC offset, 온도 계수 | zero-g / zero-rate offset, 온도 drift | §7 |
| 위상 잡음·장기 안정도 (Allan deviation은 원래 오실레이터 세계의 도구) | bias instability, angle random walk | §9 |
| LO 누설, 크로스토크 | cross-axis sensitivity, 지자기 hard/soft iron | §7, §8 |

이미 아는 것을 짧게 복습하고 시작한다.

- A2 §1에서 "책상 위 가속도계"로 평균·분산·Gaussian noise를 배웠다. 이 노트는 그 noise의 **크기가 데이터시트 어디에 적혀 있고 대역폭에 따라 어떻게 변하는지**를 다룬다.
- A6 §4.6에서 `value = raw × R / 32768`로 int16 count를 g, dps로 바꿨고, ±FS 끝에 붙은 샘플은 포화라고 배웠다. 이 노트 §4는 "데이터시트 감도 표가 32768/FS와 다를 수 있다"는 함정을 더한다.
- B7 §2.2에서 "가속도계는 중력 + 움직임을 함께 잰다", 크기 `|a|`는 회전 불변이라고 배웠다. 이 노트 §1은 그 **부호와 이유**(specific force)를 정확히 한다.

---

## 1. 세 센서가 재는 것

### 1.1 가속도계 — "가속도"가 아니라 specific force를 잰다

**직관**: 엘리베이터 안에서 체중계에 올라섰다고 하자. 엘리베이터가 멈춰 있으면 체중계는 평소 몸무게를 가리킨다. 출발하며 위로 가속하면 더 무겁게, 내려가기 시작하면 더 가볍게 나온다. 케이블이 끊어져 자유낙하하면 0이 된다. 체중계는 "중력"을 재는 게 아니라 **바닥이 나를 미는 힘**을 잰다. 가속도계도 똑같다. 안에 있는 작은 질량(proof mass, §2)이 스프링에 매달려 있고, 센서는 **스프링이 질량을 미는 힘**을 잰다.

**정의**: 가속도계 출력은 specific force다.

```
f = a − g⃗

a   : 센서의 실제 가속도 (관성 좌표계 기준)        [m/s²]
g⃗   : 중력 가속도 벡터. 지표면에서 아래를 향하고 크기 ≈ 9.80665 m/s²
f   : 가속도계가 읽는 값 = (중력이 아닌 힘의 합) / 질량
```

말로 하면, "가속도계는 중력을 뺀 나머지 힘을 잰다. 그런데 정지해 있으면 그 나머지 힘은 **중력을 버티는 바닥의 힘**이므로 위쪽 1 g가 찍힌다." 그래서:

- 책상 위 정지: `a = 0` → `f = −g⃗` = 위쪽 1 g. 위를 향한 축이 **+1 g**를 읽는다.
- 자유낙하: `a = g⃗` → `f = 0`. 세 축 모두 0 g. 낙상 감지가 "0 g 구간"을 찾는 이유다.
- 위로 0.2 g 가속: `f` = 위쪽 1.2 g.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 300">
<defs><marker id="g1ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <text x="110" y="24" font-size="13" text-anchor="middle">(1) 책상 위 정지</text> <text x="330" y="24" font-size="13" text-anchor="middle">(2) 자유낙하</text> <text x="550" y="24" font-size="13" text-anchor="middle">(3) 위로 가속 (엘리베이터 출발)</text> <line x1="40" y1="200" x2="180" y2="200" stroke="currentColor" stroke-width="2"/> <rect x="80" y="150" width="60" height="50" fill="none" stroke="currentColor" stroke-width="1.5"/> <text x="110" y="180" font-size="12" text-anchor="middle">IMU</text> <line x1="60" y1="140" x2="60" y2="210" stroke="#d0564a" stroke-width="2.5" marker-end="url(#g1ah1)"/> <text x="56" y="232" font-size="12" text-anchor="middle">중력 g ↓</text> <line x1="160" y1="140" x2="160" y2="70" stroke="#3f9a6b" stroke-width="2.5" marker-end="url(#g1ah1)"/>
<text x="166" y="62" font-size="12">책상이 미는 힘 ↑</text> <text x="110" y="260" font-size="13" text-anchor="middle">a = 0, 읽음 = +1 g (위)</text> <rect x="300" y="110" width="60" height="50" fill="none" stroke="currentColor" stroke-width="1.5"/> <text x="330" y="140" font-size="12" text-anchor="middle">IMU</text> <line x1="280" y1="100" x2="280" y2="180" stroke="#d0564a" stroke-width="2.5" marker-end="url(#g1ah1)"/> <text x="276" y="200" font-size="12" text-anchor="middle">중력만</text> <line x1="380" y1="100" x2="380" y2="180" stroke="#4a7bd0" stroke-width="2.5" marker-end="url(#g1ah1)"/> <text x="384" y="200" font-size="12">a = g ↓</text> <text x="330" y="260" font-size="13" text-anchor="middle">a − g = 0, 읽음 = 0 g</text> <line x1="470" y1="200" x2="630" y2="200" stroke="currentColor" stroke-width="2"/>
<rect x="520" y="150" width="60" height="50" fill="none" stroke="currentColor" stroke-width="1.5"/> <text x="550" y="180" font-size="12" text-anchor="middle">IMU</text> <line x1="500" y1="140" x2="500" y2="210" stroke="#d0564a" stroke-width="2.5" marker-end="url(#g1ah1)"/> <line x1="600" y1="140" x2="600" y2="55" stroke="#3f9a6b" stroke-width="2.5" marker-end="url(#g1ah1)"/> <text x="606" y="100" font-size="12">더 세게 밂</text> <text x="550" y="260" font-size="13" text-anchor="middle">a = +0.2 g ↑, 읽음 = +1.2 g</text> <text x="330" y="290" font-size="12" text-anchor="middle">가속도계 읽음 = specific force f = a − g⃗  = (중력 외의 힘) / 질량</text>
</svg>
```

그림 2 — specific force의 세 경우. 빨강은 중력, 초록은 바닥이 미는 힘, 파랑은 실제 가속도. 가속도계는 초록(중력이 아닌 힘)만 느낀다.

**부호 규약(sign convention) — 데이터시트에서 반드시 확인할 것**

- 칩 데이터시트에는 패키지 그림 위에 x, y, z 화살표가 그려져 있다. 보통 "그 축이 **위(하늘)를 향하면** +1 g"가 되도록 정의된다. 이게 우리가 위에서 유도한 specific force 규약이다.
- 모바일 OS도 같은 규약을 쓴다. 예를 들어 Android 문서는 기기를 화면이 위로 오게 책상에 놓으면 z축 가속도가 약 +9.81 m/s²라고 설명한다.
- 반대로 항법(navigation) 문헌은 NED(North-East-Down) 좌표계를 자주 쓴다. 이때 z가 아래를 향하므로, 정지 상태에서 z 성분은 −1 g로 표현된다. **값이 틀린 게 아니라 좌표계가 다른 것**이다.
- 보드에 칩이 90°나 180° 돌아서 붙어 있으면 축이 바뀐다. 펌웨어(또는 드라이버의 orientation matrix)에서 "칩 축 → 기기 축" 변환을 한 번 해 주고, 그 행렬을 로그 헤더에 남긴다. G3에서 다시 다룬다.

**손계산**: 시계가 x축을 기준으로 30° 기울어진 채 정지해 있다. 중력의 반대 방향(위쪽 1 g)이 기기 좌표계에서 `(0, sin 30°, cos 30°) = (0, 0.5, 0.866) g`로 찍힌다. 거꾸로, 정지 상태라면 이 값에서 기울기를 역산할 수 있다.

```
roll = atan2(fy, fz) = atan2(0.5, 0.866) = 30°
|f|  = √(0² + 0.5² + 0.866²) = 1.000 g          ← 정지면 기울기와 무관하게 1 g
```

말로 하면, "정지한 가속도계는 기울기계(inclinometer)다." 움직이는 동안에는 `a`가 섞이므로 이 역산이 틀어진다. 그래서 자세 추정에는 자이로를 섞는다(G3).

이 코드는 specific force 정의로 네 가지 상황의 가속도계 출력을 계산하고, 기울기 역산을 확인한다.

```python
import numpy as np
g = 9.80665                                   # 표준 중력 [m/s²]
G_WORLD = np.array([0, 0, -g])                # 중력 가속도: 아래(-z)를 향한다

def accel_reading(a_world):
    """가속도계가 재는 것 = specific force f = a - g_vec (센서가 수평으로 놓였다고 가정)"""
    return (a_world - G_WORLD) / g            # g 단위로

cases = {
    "책상 위 정지      ": np.array([0, 0, 0.0]),
    "자유낙하          ": np.array([0, 0, -g]),
    "엘리베이터 위로 2 m/s²": np.array([0, 0, 2.0]),
    "앞으로 3 m/s² 가속 ": np.array([3.0, 0, 0]),
}
for name, a in cases.items():
    f = accel_reading(a)
    print(f"{name}: f = {np.round(f, 3)} g, |f| = {np.linalg.norm(f):.3f} g")

# 기울기: x축 기준 30° 기울인 채 정지 → 중력 성분이 y, z로 나뉜다
th = np.deg2rad(30)
f_tilt = np.array([0, np.sin(th), np.cos(th)])
roll = np.degrees(np.arctan2(f_tilt[1], f_tilt[2]))
print("30° 기울인 정지:", np.round(f_tilt, 3), "→ 역산 roll =", round(roll, 2), "deg")
```

```text
책상 위 정지      : f = [0. 0. 1.] g, |f| = 1.000 g
자유낙하          : f = [0. 0. 0.] g, |f| = 0.000 g
엘리베이터 위로 2 m/s²: f = [0.    0.    1.204] g, |f| = 1.204 g
앞으로 3 m/s² 가속 : f = [0.306 0.    1.   ] g, |f| = 1.046 g
30° 기울인 정지: [0.    0.5   0.866] → 역산 roll = 30.0 deg
```

출력에서 볼 것: 정지 +1 g, 자유낙하 0 g. 수평으로 3 m/s² 가속하면 x에 0.306 g가 더해지고 z의 1 g는 그대로라서 크기가 1.046 g가 된다. 이 상태에서 "정지했다고 가정하고" 기울기를 역산하면 atan2(0.306, 1) ≈ 17°나 기울어졌다고 착각한다 — 움직이는 동안 가속도계만으로 자세를 믿으면 안 되는 이유다.

> 함정: "가속도계로 중력을 빼면 순수 움직임이 나온다"는 말은 **중력 방향을 정확히 알 때만** 맞다. 방향을 1° 틀리면 0.017 g가 움직임으로 둔갑한다(§10에서 이것이 위치 적분을 망치는 주범임을 본다).

### 1.2 자이로스코프 — 각속도(angular rate)를 잰다

**정의**: 자이로는 센서 자신이 축을 중심으로 **얼마나 빨리 돌고 있는가**, 즉 각속도 ω를 잰다. 단위는 dps(degree per second, °/s) 또는 rad/s. `1 rad/s = 57.2958 dps`.

- 방향은 **오른손 법칙**: 엄지를 +축 방향으로 두고 나머지 손가락이 감기는 방향으로 돌면 +.
- 각도가 아니라 **각도의 변화율**이다. 각도를 얻으려면 적분해야 하고, 그 순간 bias가 drift가 된다(§10).
- 정지해 있으면 이상적으로는 0을 읽는다. 실제로는 bias(zero-rate offset) + noise가 나온다. 지구 자전도 회전이지만 15.04 °/h ≈ 0.0042 dps라서 소비자용 MEMS 자이로의 bias·noise에 완전히 묻힌다(그래서 MEMS 자이로로 "북쪽 찾기(gyrocompassing)"는 사실상 불가능하다).
- 가속도계와 달리 **중력에 반응하지 않는다**(이상적으로는). 그래서 둘을 섞으면 서로의 약점을 보완한다: 자이로는 짧은 시간 정확하지만 drift, 가속도계는 길게 보면 중력 방향을 알려 주지만 순간순간 시끄럽다. 이 조합이 complementary filter·Madgwick·Kalman이다(G3).

**손계산**: 손목을 0.5초 동안 90° 돌렸다면 평균 각속도는 `90 / 0.5 = 180 dps`. 빠른 제스처(손목 튕기기)는 수백~1000 dps를 넘길 수 있다고 알려져 있어, 자이로 range를 ±125 dps로 두면 쉽게 포화한다(§11).

### 1.3 지자기 센서(magnetometer) — 자기장 벡터를 잰다

**정의**: 지자기 센서는 센서 위치의 **자기장 벡터 B**를 잰다. 단위는 µT(microtesla) 또는 gauss(`1 G = 100 µT`). 지구 자기장의 세기는 지역에 따라 대략 25~65 µT이고, 수평 성분이 북쪽(자북)을 가리킨다. 그래서 수평으로 놓고 `atan2`로 heading(방위)을 계산할 수 있다.

- 기기가 기울어지면 수직 성분이 섞이므로, 가속도계로 기울기를 알아내 수평면으로 투영하는 **tilt compensation**이 필요하다.
- 가장 큰 문제는 **주변 자성체**다. 기기 안의 스피커 자석·진동 모터·배터리 케이스·자석 충전 커넥터는 지구 자기장보다 큰 offset을 만들 수 있다. 이를 hard iron(고정 offset)과 soft iron(장을 휘게 만드는 왜곡)으로 나눈다. §8에서 시뮬레이션한다.
- 자북과 진북의 차이(편각, declination)는 위치마다 다르다. heading을 지도와 맞추려면 별도 보정이 필요하다.

### 1.4 3축 · 6축 · 9축, IMU · AHRS · INS

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| 3축 가속도계 | x, y, z 가속도 | 가장 싸고 전력이 낮다. 걸음 수·착용 감지·HAR에 흔하다 |
| 6축 IMU | 가속도 3 + 자이로 3 | 웨어러블 IMU의 표준 구성. BMI270, LSM6DSx, ICM-42688-P 등 |
| 9축 | 6축 + 지자기 3 | heading까지 필요할 때. 별도 지자기 칩을 붙이거나 9축 모듈 사용 |
| IMU | Inertial Measurement Unit | 원시 측정값(가속도·각속도)을 주는 센서 묶음 |
| AHRS | Attitude and Heading Reference System | IMU(+지자기) 값을 퓨전해서 자세(roll, pitch)와 heading을 출력하는 시스템 |
| INS | Inertial Navigation System | 자세뿐 아니라 속도·위치까지 적분하는 시스템. 소비자 MEMS로는 단독 사용이 어렵다(§10) |

> 말 습관: "IMU"라고 하면 보통 6축 센서 칩을 가리키고, "IMU 값으로 자세를 낸다"는 순간 AHRS 얘기가 된다. 면접에서 둘을 구분해서 말하면 좋다.

---

## 2. MEMS 가속도계의 물리 — 스프링에 매달린 실리콘 조각

### 2.1 구조: proof mass + spring + 정전용량 감지

MEMS(Micro-Electro-Mechanical Systems) 가속도계는 실리콘을 깎아 만든 **작은 질량(proof mass)**을 **실리콘 스프링**으로 고정점(anchor)에 매단 구조다. 질량에는 빗살(comb) 모양 손가락이 달려 있고, 그 사이에 고정 전극이 끼어 있다. 질량이 밀리면 손가락과 전극 사이 간격이 변하고, 간격이 변하면 정전용량이 변한다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 330">
<defs><marker id="g1ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <rect x="30" y="120" width="30" height="80" fill="#888" fill-opacity="0.35" stroke="currentColor"/> <text x="45" y="220" font-size="12" text-anchor="middle">anchor</text> <rect x="430" y="120" width="30" height="80" fill="#888" fill-opacity="0.35" stroke="currentColor"/> <text x="445" y="220" font-size="12" text-anchor="middle">anchor</text> <polyline points="60,160 75,145 90,175 105,145 120,175 135,145 150,160 170,160" fill="none" stroke="#3f9a6b" stroke-width="2"/> <polyline points="320,160 340,160 355,145 370,175 385,145 400,175 415,145 430,160" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="115" y="130" font-size="12" text-anchor="middle">spring k</text> <text x="375" y="130" font-size="12" text-anchor="middle">spring k</text> <rect x="170" y="110" width="150" height="100" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0" stroke-width="2"/>
<text x="245" y="150" font-size="13" text-anchor="middle">proof mass m</text> <text x="245" y="170" font-size="12" text-anchor="middle">(실리콘, µg 수준)</text> <line x1="200" y1="210" x2="200" y2="250" stroke="#4a7bd0" stroke-width="3"/> <line x1="245" y1="210" x2="245" y2="250" stroke="#4a7bd0" stroke-width="3"/> <line x1="290" y1="210" x2="290" y2="250" stroke="#4a7bd0" stroke-width="3"/> <line x1="185" y1="225" x2="185" y2="280" stroke="#e08a3c" stroke-width="3"/> <line x1="230" y1="225" x2="230" y2="280" stroke="#e08a3c" stroke-width="3"/> <line x1="275" y1="225" x2="275" y2="280" stroke="#e08a3c" stroke-width="3"/> <line x1="215" y1="225" x2="215" y2="280" stroke="#d0564a" stroke-width="3"/> <line x1="260" y1="225" x2="260" y2="280" stroke="#d0564a" stroke-width="3"/>
<line x1="305" y1="225" x2="305" y2="280" stroke="#d0564a" stroke-width="3"/> <line x1="180" y1="280" x2="310" y2="280" stroke="currentColor" stroke-width="1"/> <text x="245" y="300" font-size="12" text-anchor="middle">빗살(comb): 움직이는 손가락(파랑) 사이 고정 전극 C1(주황)·C2(빨강)</text> <line x1="170" y1="70" x2="320" y2="70" stroke="currentColor" stroke-width="1.5" marker-start="url(#g1ah2)" marker-end="url(#g1ah2)"/> <text x="245" y="60" font-size="12" text-anchor="middle">가속 a → 관성 때문에 mass가 x = m·a/k 만큼 밀림</text> <text x="490" y="120" font-size="12">x가 생기면</text> <text x="490" y="140" font-size="12">C1 = ε·A/(d − x) ↑</text> <text x="490" y="160" font-size="12">C2 = ε·A/(d + x) ↓</text> <text x="490" y="185" font-size="12">차동 ΔC ≈ 2·C0·x/d</text> <text x="490" y="210" font-size="12">→ C-to-V 증폭 → ADC</text>
<text x="490" y="230" font-size="12">→ 디지털 필터 → 레지스터</text>
</svg>
```

그림 3 — MEMS 가속도계 한 축의 개념도. 가속하면 질량이 관성 때문에 뒤처지며(= 스프링이 늘어나며) x만큼 밀리고, 차동 정전용량 ΔC가 x에 비례해서 생긴다. 실제 칩은 축마다 이런 구조가 있고, 형상은 회사마다 다르다.

**정의 (정적 상태)**: 스프링 힘 = 관성력이므로

```
k · x = m · a        →        x = m · a / k = a / ω0²,     ω0 = √(k / m),  f0 = ω0 / 2π
```

말로 하면, "변위는 가속도를 공진 각주파수의 제곱으로 나눈 것"이다. 질량 m이 약분된다는 게 재미있다: 감도를 정하는 것은 질량 자체가 아니라 **공진 주파수**다. 공진 주파수가 낮을수록(스프링이 무를수록) 같은 가속도에 더 많이 움직여 감도가 좋지만, 대역폭이 줄고 충격에 약해진다.

**차동 정전용량**: 평행판 커패시터 `C = ε·A/d`. 질량이 x만큼 움직이면 한쪽 간격은 d − x, 반대쪽은 d + x.

```
C1 = ε·A/(d − x),   C2 = ε·A/(d + x)
ΔC = C1 − C2 ≈ 2·C0·x/d      (x ≪ d 일 때,  C0 = ε·A/d)
```

말로 하면, "두 커패시터의 차이를 재면 x에 선형이고, 온도 등으로 둘이 같이 변하는 성분은 상쇄된다." 차동(differential) 구조는 Don이 아는 차동 신호(LVDS, 차동 ADC 입력)와 같은 이유로 쓴다. 그리고 근사식에서 버린 고차항(x²/d² …)이 §7의 **nonlinearity**가 된다.

### 2.2 손계산: 1 g에서 질량은 얼마나 움직이나

공진 주파수를 3 kHz라고 가정하자(실제 값은 제품마다 다르고 데이터시트에 잘 안 나온다).

```
ω0 = 2π × 3000 = 18 850 rad/s
x  = 9.80665 / 18 850² = 9.80665 / 3.553×10⁸ ≈ 2.76×10⁻⁸ m = 27.6 nm
```

27.6 nm는 원자 100여 개 크기다. 그리고 noise floor가 수백 µg라면 그에 해당하는 변위는 **피코미터(pm)** 단위다. 이걸 정전용량으로 읽어 내는 것이 MEMS 회로(C-to-V 증폭기, 변조·복조, ADC)의 일이다. 칩 안에서는 아날로그 프런트엔드 → ADC → 디지털 필터 → ODR로 솎기 → 출력 레지스터 순서로 처리된다는 정도만 기억하면 된다. 구체적인 회로 방식(예: ΣΔ 변환기 사용 여부, 변조 주파수)은 제조사마다 다르고 대부분 공개하지 않는다.

### 2.3 공진과 대역폭 — 2차계 주파수 응답

스프링-질량-댐퍼는 2차 저역통과 시스템이다(제어공학의 2차계와 같다). 공진 아래에서는 응답이 평탄하고, 공진 근처에서 Q(감쇠의 역수 같은 값)에 따라 피크가 생기며, 공진 위에서는 −40 dB/decade로 떨어진다.

```
|H(f)| = 1 / √( (1 − r²)² + (r/Q)² ),     r = f / f0
```

말로 하면, "r ≪ 1(사용 대역)이면 |H| ≈ 1, r = 1(공진)이면 |H| = Q, r ≫ 1이면 |H| ≈ 1/r²." MEMS 가속도계 내부에서는 공기 감쇠(squeeze-film damping)가 Q를 정한다.

이 코드는 공진 주파수별 1 g 변위와 Q별 주파수 응답을 계산한다.

```python
import numpy as np
g = 9.80665
# 스프링-질량 계: 정적 변위 x = m·a/k = a/ω0²  (질량 m은 약분된다!)
for f0 in [1_000, 3_000, 10_000]:             # 공진 주파수 [Hz] (가정값)
    w0 = 2 * np.pi * f0
    x = g / w0**2                             # 1 g 에서 변위 [m]
    print(f"f0 = {f0:>6} Hz → 1 g 변위 = {x*1e9:8.2f} nm")

# 2차계 주파수 응답: |H(f)| = 1 / sqrt((1-r²)² + (r/Q)²),  r = f/f0
f0 = 3_000.0
for Q in [0.5, 0.707, 5.0]:
    row = []
    for f in [10, 100, 1000, 2000, 3000, 6000]:
        r = f / f0
        H = 1 / np.sqrt((1 - r**2)**2 + (r / Q)**2)
        row.append(f"{H:6.3f}")
    print(f"Q={Q:<5}", " ".join(row))
print("f[Hz]   ", " ".join(f"{f:>6}" for f in [10, 100, 1000, 2000, 3000, 6000]))
```

```text
f0 =   1000 Hz → 1 g 변위 =   248.41 nm
f0 =   3000 Hz → 1 g 변위 =    27.60 nm
f0 =  10000 Hz → 1 g 변위 =     2.48 nm
Q=0.5    1.000  0.999  0.900  0.692  0.500  0.200
Q=0.707  1.000  1.000  0.994  0.914  0.707  0.243
Q=5.0    1.000  1.001  1.122  1.750  5.000  0.330
f[Hz]        10    100   1000   2000   3000   6000
```

출력에서 볼 것: f0가 10배 오르면 변위는 100배 줄어든다(1/f0²). Q=5면 공진에서 5배(+14 dB) 증폭되고, Q=0.707은 공진 근처까지 평탄하다(Butterworth 특성). 우리 관심 대역(수십~수백 Hz)에서는 세 경우 모두 응답이 1.000이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 310">
<line x1="70.0" y1="260.0" x2="620.0" y2="260.0" stroke="currentColor" stroke-width="1"/> <line x1="70.0" y1="260.0" x2="70.0" y2="40.0" stroke="currentColor" stroke-width="1"/> <line x1="70.0" y1="260.0" x2="70.0" y2="265.0" stroke="currentColor" stroke-width="1"/> <text x="70.0" y="278.0" font-size="12" text-anchor="middle">10</text> <line x1="70.0" y1="260.0" x2="70.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="228.2" y1="260.0" x2="228.2" y2="265.0" stroke="currentColor" stroke-width="1"/> <text x="228.2" y="278.0" font-size="12" text-anchor="middle">100</text> <line x1="228.2" y1="260.0" x2="228.2" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="386.4" y1="260.0" x2="386.4" y2="265.0" stroke="currentColor" stroke-width="1"/> <text x="386.4" y="278.0" font-size="12" text-anchor="middle">1k</text>
<line x1="386.4" y1="260.0" x2="386.4" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="461.8" y1="260.0" x2="461.8" y2="265.0" stroke="currentColor" stroke-width="1"/> <text x="461.8" y="278.0" font-size="12" text-anchor="middle">3k</text> <line x1="461.8" y1="260.0" x2="461.8" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="544.5" y1="260.0" x2="544.5" y2="265.0" stroke="currentColor" stroke-width="1"/> <text x="544.5" y="278.0" font-size="12" text-anchor="middle">10k</text> <line x1="544.5" y1="260.0" x2="544.5" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="620.0" y1="260.0" x2="620.0" y2="265.0" stroke="currentColor" stroke-width="1"/> <text x="620.0" y="278.0" font-size="12" text-anchor="middle">30k</text> <line x1="620.0" y1="260.0" x2="620.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/>
<line x1="65.0" y1="260.0" x2="70.0" y2="260.0" stroke="currentColor" stroke-width="1"/> <text x="62.0" y="264.0" font-size="12" text-anchor="end">−40</text> <line x1="70.0" y1="260.0" x2="620.0" y2="260.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="65.0" y1="186.7" x2="70.0" y2="186.7" stroke="currentColor" stroke-width="1"/> <text x="62.0" y="190.7" font-size="12" text-anchor="end">−20</text> <line x1="70.0" y1="186.7" x2="620.0" y2="186.7" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="65.0" y1="113.3" x2="70.0" y2="113.3" stroke="currentColor" stroke-width="1"/> <text x="62.0" y="117.3" font-size="12" text-anchor="end">0</text> <line x1="70.0" y1="113.3" x2="620.0" y2="113.3" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="65.0" y1="40.0" x2="70.0" y2="40.0" stroke="currentColor" stroke-width="1"/>
<text x="62.0" y="44.0" font-size="12" text-anchor="end">+20</text> <line x1="70.0" y1="40.0" x2="620.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <text x="345.0" y="296.0" font-size="13" text-anchor="middle">주파수 [Hz] (log)</text> <text x="64.0" y="30.0" font-size="12" text-anchor="start">|H| [dB]</text> <polyline points="70.0,113.3 73.5,113.3 76.9,113.3 80.4,113.3 83.8,113.3 87.3,113.3 90.8,113.3 94.2,113.3 97.7,113.3 101.1,113.3 104.6,113.3 108.1,113.3 111.5,113.3 115.0,113.3 118.4,113.3 121.9,113.3 125.3,113.3 128.8,113.3 132.3,113.3 135.7,113.3 139.2,113.3 142.6,113.3 146.1,113.3 149.6,113.3 153.0,113.3 156.5,113.3 159.9,113.3 163.4,113.3 166.9,113.3 170.3,113.3 173.8,113.3 177.2,113.3 180.7,113.3 184.2,113.3 187.6,113.3 191.1,113.3 194.5,113.3 198.0,113.3 201.4,113.3 204.9,113.4 208.4,113.4 211.8,113.4 215.3,113.4 218.7,113.4 222.2,113.4 225.7,113.4 229.1,113.4 232.6,113.4 236.0,113.4 239.5,113.4 243.0,113.4 246.4,113.4 249.9,113.4 253.3,113.4 256.8,113.4 260.3,113.4 263.7,113.4 267.2,113.4 270.6,113.5 274.1,113.5 277.5,113.5 281.0,113.5 284.5,113.5 287.9,113.5 291.4,113.6 294.8,113.6 298.3,113.6 301.8,113.6 305.2,113.7 308.7,113.7 312.1,113.7 315.6,113.8 319.1,113.8 322.5,113.9 326.0,113.9 329.4,114.0 332.9,114.1 336.4,114.1 339.8,114.2 343.3,114.3 346.7,114.4 350.2,114.5 353.6,114.7 357.1,114.8 360.6,115.0 364.0,115.1 367.5,115.3 370.9,115.5 374.4,115.7 377.9,116.0 381.3,116.3 384.8,116.5 388.2,116.9 391.7,117.2 395.2,117.6 398.6,118.0 402.1,118.5 405.5,119.0 409.0,119.5 412.5,120.1 415.9,120.8 419.4,121.5 422.8,122.2 426.3,123.0 429.7,123.9 433.2,124.8 436.7,125.8 440.1,126.9 443.6,128.1 447.0,129.3 450.5,130.6 454.0,132.0 457.4,133.4 460.9,135.0 461.8,135.4 464.3,136.6 467.8,138.3 471.3,140.1 474.7,141.9 478.2,143.9 481.6,145.9 485.1,148.0 488.6,150.2 492.0,152.4 495.5,154.7 498.9,157.0 502.4,159.5 505.8,162.0 509.3,164.5 512.8,167.1 516.2,169.7 519.7,172.4 523.1,175.1 526.6,177.9 530.1,180.7 533.5,183.5 537.0,186.4 540.4,189.3 543.9,192.2 547.4,195.2 550.8,198.2 554.3,201.1 557.7,204.2 561.2,207.2 564.7,210.2 568.1,213.3 571.6,216.4 575.0,219.5 578.5,222.6 581.9,225.7 585.4,228.8 588.9,231.9 592.3,235.0 595.8,238.2 599.2,241.3 602.7,244.5 606.2,247.6 609.6,250.8 613.1,254.0 616.5,257.1 620.0,260.0" fill="none" stroke="#4a7bd0" stroke-width="2"/> <polyline points="70.0,113.3 73.5,113.3 76.9,113.3 80.4,113.3 83.8,113.3 87.3,113.3 90.8,113.3 94.2,113.3 97.7,113.3 101.1,113.3 104.6,113.3 108.1,113.3 111.5,113.3 115.0,113.3 118.4,113.3 121.9,113.3 125.3,113.3 128.8,113.3 132.3,113.3 135.7,113.3 139.2,113.3 142.6,113.3 146.1,113.3 149.6,113.3 153.0,113.3 156.5,113.3 159.9,113.3 163.4,113.3 166.9,113.3 170.3,113.3 173.8,113.3 177.2,113.3 180.7,113.3 184.2,113.3 187.6,113.3 191.1,113.3 194.5,113.3 198.0,113.3 201.4,113.3 204.9,113.3 208.4,113.3 211.8,113.3 215.3,113.3 218.7,113.3 222.2,113.3 225.7,113.3 229.1,113.3 232.6,113.3 236.0,113.3 239.5,113.3 243.0,113.3 246.4,113.3 249.9,113.3 253.3,113.3 256.8,113.3 260.3,113.3 263.7,113.3 267.2,113.3 270.6,113.3 274.1,113.3 277.5,113.3 281.0,113.3 284.5,113.3 287.9,113.3 291.4,113.3 294.8,113.3 298.3,113.3 301.8,113.3 305.2,113.3 308.7,113.3 312.1,113.3 315.6,113.3 319.1,113.3 322.5,113.3 326.0,113.3 329.4,113.3 332.9,113.3 336.4,113.3 339.8,113.3 343.3,113.3 346.7,113.4 350.2,113.4 353.6,113.4 357.1,113.4 360.6,113.4 364.0,113.4 367.5,113.4 370.9,113.4 374.4,113.4 377.9,113.5 381.3,113.5 384.8,113.5 388.2,113.6 391.7,113.6 395.2,113.7 398.6,113.7 402.1,113.8 405.5,113.9 409.0,114.1 412.5,114.2 415.9,114.4 419.4,114.6 422.8,114.9 426.3,115.2 429.7,115.6 433.2,116.1 436.7,116.6 440.1,117.3 443.6,118.1 447.0,119.0 450.5,120.0 454.0,121.1 457.4,122.5 460.9,123.9 461.8,124.4 464.3,125.6 467.8,127.4 471.3,129.3 474.7,131.4 478.2,133.7 481.6,136.1 485.1,138.6 488.6,141.2 492.0,143.9 495.5,146.6 498.9,149.5 502.4,152.4 505.8,155.3 509.3,158.3 512.8,161.4 516.2,164.4 519.7,167.5 523.1,170.6 526.6,173.8 530.1,176.9 533.5,180.1 537.0,183.2 540.4,186.4 543.9,189.6 547.4,192.8 550.8,195.9 554.3,199.1 557.7,202.3 561.2,205.5 564.7,208.7 568.1,211.9 571.6,215.1 575.0,218.3 578.5,221.5 581.9,224.7 585.4,227.9 588.9,231.1 592.3,234.3 595.8,237.6 599.2,240.8 602.7,244.0 606.2,247.2 609.6,250.4 613.1,253.6 616.5,256.8 620.0,260.0" fill="none" stroke="#3f9a6b" stroke-width="2"/> <polyline points="70.0,113.3 73.5,113.3 76.9,113.3 80.4,113.3 83.8,113.3 87.3,113.3 90.8,113.3 94.2,113.3 97.7,113.3 101.1,113.3 104.6,113.3 108.1,113.3 111.5,113.3 115.0,113.3 118.4,113.3 121.9,113.3 125.3,113.3 128.8,113.3 132.3,113.3 135.7,113.3 139.2,113.3 142.6,113.3 146.1,113.3 149.6,113.3 153.0,113.3 156.5,113.3 159.9,113.3 163.4,113.3 166.9,113.3 170.3,113.3 173.8,113.3 177.2,113.3 180.7,113.3 184.2,113.3 187.6,113.3 191.1,113.3 194.5,113.3 198.0,113.3 201.4,113.3 204.9,113.3 208.4,113.3 211.8,113.3 215.3,113.3 218.7,113.3 222.2,113.3 225.7,113.3 229.1,113.3 232.6,113.3 236.0,113.3 239.5,113.3 243.0,113.3 246.4,113.3 249.9,113.3 253.3,113.3 256.8,113.3 260.3,113.2 263.7,113.2 267.2,113.2 270.6,113.2 274.1,113.2 277.5,113.2 281.0,113.2 284.5,113.2 287.9,113.1 291.4,113.1 294.8,113.1 298.3,113.1 301.8,113.0 305.2,113.0 308.7,113.0 312.1,112.9 315.6,112.9 319.1,112.8 322.5,112.8 326.0,112.7 329.4,112.7 332.9,112.6 336.4,112.5 339.8,112.4 343.3,112.3 346.7,112.2 350.2,112.1 353.6,112.0 357.1,111.8 360.6,111.7 364.0,111.5 367.5,111.3 370.9,111.0 374.4,110.8 377.9,110.5 381.3,110.2 384.8,109.8 388.2,109.5 391.7,109.0 395.2,108.5 398.6,108.0 402.1,107.3 405.5,106.6 409.0,105.9 412.5,105.0 415.9,103.9 419.4,102.8 422.8,101.4 426.3,99.9 429.7,98.1 433.2,96.0 436.7,93.6 440.1,90.6 443.6,87.2 447.0,82.9 450.5,77.8 454.0,71.8 457.4,65.5 460.9,61.9 461.8,62.1 464.3,65.2 467.8,73.8 471.3,83.4 474.7,92.2 478.2,100.1 481.6,107.2 485.1,113.6 488.6,119.5 492.0,125.0 495.5,130.1 498.9,135.0 502.4,139.7 505.8,144.1 509.3,148.4 512.8,152.6 516.2,156.7 519.7,160.6 523.1,164.5 526.6,168.3 530.1,172.0 533.5,175.7 537.0,179.3 540.4,182.9 543.9,186.4 547.4,190.0 550.8,193.4 554.3,196.9 557.7,200.3 561.2,203.7 564.7,207.1 568.1,210.4 571.6,213.8 575.0,217.1 578.5,220.4 581.9,223.8 585.4,227.1 588.9,230.4 592.3,233.6 595.8,236.9 599.2,240.2 602.7,243.4 606.2,246.7 609.6,250.0 613.1,253.2 616.5,256.4 620.0,259.7" fill="none" stroke="#e08a3c" stroke-width="2"/> <line x1="461.8" y1="260.0" x2="461.8" y2="40.0" stroke="#d0564a" stroke-width="1" stroke-dasharray="4 3"/> <text x="466.8" y="55.0" font-size="12" text-anchor="start">f0 = 3 kHz (공진)</text> <text x="90.0" y="60.0" font-size="12" text-anchor="start">Q=5 (감쇠 작음): 공진 피크 +14 dB</text>
<text x="90.0" y="78.0" font-size="12" text-anchor="start">Q=0.707: 평탄하다가 꺾임</text> <text x="90.0" y="96.0" font-size="12" text-anchor="start">Q=0.5: 일찍 처짐</text> <line x1="330.0" y1="56.0" x2="350.0" y2="56.0" stroke="#e08a3c" stroke-width="3"/> <line x1="330.0" y1="74.0" x2="350.0" y2="74.0" stroke="#3f9a6b" stroke-width="3"/> <line x1="330.0" y1="92.0" x2="350.0" y2="92.0" stroke="#4a7bd0" stroke-width="3"/> <text x="110.0" y="150.0" font-size="12" text-anchor="start">사용 대역(수십~수백 Hz): 응답 = 1 (평탄)</text>
</svg>
```

그림 4 — 2차 기계계의 주파수 응답(f0 = 3 kHz 가정, 실제 계산값). 데이터시트의 "bandwidth"는 대개 이 기계 공진이 아니라 그 뒤의 **디지털 LPF**가 정한다(§6). 하지만 기계 공진 근처의 진동(예: 수 kHz 모터 소음)은 증폭되어 포화나 aliasing을 일으킬 수 있다.

**임베디드 연결**: 기계 공진은 펌웨어로 바꿀 수 없다. 펌웨어가 정하는 것은 그 뒤의 디지털 필터와 ODR이다. 그래서 "센서 출력에 이상한 고주파 톤이 보인다"면 (1) 기구 진동원이 있는지, (2) 디지털 LPF가 충분히 낮은지, (3) aliasing인지(§6)를 순서대로 의심한다.

---

## 3. MEMS 자이로와 지자기의 물리

### 3.1 Coriolis 효과 — 회전판 위에서 공 던지기

**직관**: 돌고 있는 회전목마 중심에서 가장자리로 공을 굴리면, 회전목마 위 사람 눈에는 공이 옆으로 휘어 보인다. 이 "가짜 힘"이 Coriolis 힘이다. 핵심은 **물체가 움직이고 있어야** 생긴다는 것: 멈춘 물체에는 Coriolis 힘이 없다.

MEMS 자이로는 이 원리를 쓴다. 질량을 일부러 **계속 진동**시켜(drive mode) 속도를 만들어 두고, 칩이 회전하면 그 속도에 수직인 방향으로 Coriolis 힘이 생겨 질량이 옆으로 흔들린다(sense mode). 옆 흔들림의 크기를 정전용량으로 재면 회전 속도가 나온다.

```
F_c = −2 · m · (Ω⃗ × v⃗)        크기: 2 · m · Ω · v   (Ω ⟂ v 일 때)
a_c = F_c / m = 2 · Ω · v
```

말로 하면, "Coriolis 가속도는 회전 속도 × drive 속도 × 2다." drive 속도 v가 클수록 같은 회전에 큰 신호가 생기므로, drive 진동을 크고 빠르게(공진 주파수에서) 유지한다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 340">
<defs><marker id="g1ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <rect x="60" y="60" width="300" height="230" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="5 4"/> <text x="210" y="50" font-size="12" text-anchor="middle">칩 프레임 (z축으로 Ω 회전 중)</text> <rect x="160" y="130" width="100" height="90" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0" stroke-width="2"/> <text x="210" y="180" font-size="13" text-anchor="middle">mass m</text> <line x1="150" y1="175" x2="90" y2="175" stroke="#3f9a6b" stroke-width="3" marker-end="url(#g1ah3)"/> <line x1="270" y1="175" x2="330" y2="175" stroke="#3f9a6b" stroke-width="3" marker-end="url(#g1ah3)"/> <text x="210" y="250" font-size="12" text-anchor="middle">drive mode: x 방향으로 수~수십 kHz 공진 진동</text> <text x="210" y="268" font-size="12" text-anchor="middle">v(t) = v0·cos(ωd·t)</text> <line x1="210" y1="125" x2="210" y2="75" stroke="#d0564a" stroke-width="3" marker-end="url(#g1ah3)"/>
<text x="218" y="92" font-size="12">Coriolis 힘 (y)</text> <circle cx="105" cy="100" r="14" fill="none" stroke="currentColor" stroke-width="1.5"/> <text x="105" y="105" font-size="13" text-anchor="middle">Ω</text> <text x="105" y="132" font-size="12" text-anchor="middle">z축 회전</text> <text x="390" y="90" font-size="13">F_c = −2·m·(Ω⃗ × v⃗)</text> <text x="390" y="115" font-size="12">크기 = 2·m·Ω·v  (Ω ⟂ v일 때)</text> <text x="390" y="145" font-size="12">v가 cos(ωd·t)로 흔들리므로</text> <text x="390" y="165" font-size="12">sense 변위 = Ω × (반송파 ωd)</text> <text x="390" y="185" font-size="12">= AM 변조된 신호</text> <text x="390" y="215" font-size="12">→ drive 기준 신호와 곱해</text>
<text x="390" y="235" font-size="12">동기 복조(demod) + LPF</text> <text x="390" y="255" font-size="12">→ Ω (dps) 출력</text> <text x="390" y="290" font-size="12">drive를 계속 공진시켜야 해서</text> <text x="390" y="308" font-size="12">자이로는 accel보다 전력이 크다</text>
</svg>
```

그림 5 — 진동형 MEMS 자이로의 개념. drive 축(x)으로 수~수십 kHz 공진 진동을 유지하고, z축 회전이 있으면 y축으로 Coriolis 힘이 생긴다. drive 속도가 cos로 흔들리므로 Coriolis 힘도 같은 주파수로 부호가 바뀐다 — 즉 회전 속도 Ω가 drive 주파수 반송파에 **AM 변조**되어 나온다.

**Don과 연결**: 이것은 RF 수신기의 **동기 검파(coherent demodulation)**와 똑같다. drive 루프가 반송파(LO) 역할을 하고, sense 신호는 Ω로 변조된 RF 신호, 칩 안 복조기는 mixer + LPF다. drive와 sense 위상이 어긋나면 I/Q 불균형처럼 quadrature error가 생기는데, 이것이 자이로 bias의 한 원인으로 알려져 있다.

### 3.2 숫자로 보기 + 복조 데모

drive 주파수 20 kHz, 진폭 5 µm라고 가정하자(실제 값은 제품마다 다르다).

```
v_pk = 2π × 20 000 × 5×10⁻⁶ = 0.628 m/s
Ω = 1 dps = 0.01745 rad/s 일 때 a_c = 2 × 0.01745 × 0.628 = 0.0219 m/s² ≈ 2.2 mg
```

말로 하면, "1 dps 회전은 진동하는 질량에 겨우 2 mg 정도의 옆 가속도를 만든다." 이걸 noise 위로 읽어 내야 하니 자이로 회로는 가속도계보다 까다롭다.

이 코드는 Coriolis 가속도 크기를 계산하고, "Ω × 반송파 + 잡음"인 sense 신호를 기준 신호와 곱한 뒤 저역통과해서 Ω를 복원한다.

```python
import numpy as np
from scipy.signal import butter, sosfiltfilt
fd, x0 = 20_000.0, 5e-6                  # drive 주파수 20 kHz, 진폭 5 µm (가정값)
v_pk = 2 * np.pi * fd * x0               # drive 최대 속도
for dps in [1, 100, 1000]:
    a_c = 2 * np.deg2rad(dps) * v_pk     # Coriolis 가속도 크기 2·Ω·v
    print(f"Ω={dps:>4} dps → a_c = {a_c:8.4f} m/s² = {a_c/9.80665*1000:8.2f} mg")

# 복조 데모: sense 신호 = Ω(t) × cos(ωd t) (drive 속도와 같은 위상) + 잡음
fs = 400_000.0
t = np.arange(0, 0.05, 1 / fs)
omega = 50 * np.sin(2 * np.pi * 20 * t)  # 실제 회전: 20 Hz로 ±50 dps 흔들기
ref = np.cos(2 * np.pi * fd * t)         # drive 속도 기준 신호 (drive 루프가 알고 있다)
rng = np.random.default_rng(0)
sense = omega * ref + 5 * rng.standard_normal(t.size)
mixed = sense * ref                       # 곱하면 Ω/2 + Ω/2·cos(2ωd t)
sos = butter(4, 200, fs=fs, output="sos")
est = 2 * sosfiltfilt(sos, mixed)          # 저역통과 후 ×2
err = est[2000:-2000] - omega[2000:-2000]
print("복조 오차 RMS =", round(float(np.sqrt(np.mean(err**2))), 3), "dps")
print("t=12.5ms 실제/추정:", round(float(omega[5000]), 2), round(float(est[5000]), 2))
```

```text
Ω=   1 dps → a_c =   0.0219 m/s² =     2.24 mg
Ω= 100 dps → a_c =   2.1932 m/s² =   223.65 mg
Ω=1000 dps → a_c =  21.9325 m/s² =  2236.49 mg
복조 오차 RMS = 0.267 dps
t=12.5ms 실제/추정: 50.0 49.94
```

출력에서 볼 것: Coriolis 가속도는 Ω에 정확히 비례한다(1 → 100 → 1000 dps에서 100배씩). 복조 후 20 Hz로 흔들리는 ±50 dps 회전이 오차 RMS 약 0.27 dps로 복원된다. `mixed = sense × ref`에서 `cos²(ωt) = (1 + cos 2ωt)/2`이므로 LPF 후 ×2를 해 준다 — 믹서 수식 그대로다.

### 3.3 왜 자이로가 가속도계보다 전력을 많이 먹나

- 가속도계는 **수동적**이다. 질량이 가만히 있다가 가속에 반응해 밀릴 뿐이고, 회로는 그 변위만 읽는다. 그래서 낮은 ODR 저전력 모드에서는 회로를 듀티 사이클로 켰다 끌 수 있다.
- 자이로는 **능동적**이다. drive 진동을 공진 주파수에서 일정한 진폭으로 계속 유지해야 하고(자동 이득 제어가 있는 발진 루프), 복조기도 계속 돌아야 한다. 진동을 멈추면 다시 안정되기까지 시간이 걸리므로 쉽게 껐다 켤 수도 없다.
- 결과적으로 같은 칩에서도 자이로를 켜면 전류가 **자릿수 단위로** 늘어나는 경우가 많다. 대략적인 감각으로 가속도계 저전력 모드는 수 µA~수십 µA, 자이로를 켠 6축 모드는 수백 µA~1 mA 안팎인 제품이 흔하다 — 정확한 값은 반드시 해당 데이터시트의 "current consumption" 표에서 ODR·모드별로 확인한다.
- 자이로는 켜진 직후 안정되기까지 **turn-on time**(데이터시트에 명시, 보통 수십 ms 단위인 경우가 많다)이 있다. "움직임이 감지되면 그때 자이로를 켠다" 설계에서는 이 시간 동안의 데이터가 없다는 것을 감안해야 한다.

**임베디드 결론**: 웨어러블에서는 가속도계만 상시 켜 두고(any-motion·wrist-tilt 같은 내장 기능으로 MCU를 깨우고), 자이로는 필요한 순간에만 켜는 것이 기본 패턴이다. G2에서 레지스터 수준으로 다룬다.

### 3.4 지자기 센서의 감지 원리 (한 단락)

지자기 센서는 질량이 아니라 **자기 효과를 쓰는 소자**로 만든다. 대표적으로 Hall 효과, AMR(anisotropic magnetoresistance), TMR(tunnel magnetoresistance), fluxgate 계열 원리가 있고, 제품마다 다르다(어떤 원리를 쓰는지는 데이터시트 개요나 제조사 자료에서 확인). 응용 관점에서 중요한 것은 원리보다 (1) 측정 범위(지구 자기장보다 훨씬 넓어야 주변 자석에 포화되지 않는다), (2) noise와 해상도(µT 단위), (3) 강한 자기장에 노출된 뒤 offset이 변하는 현상과 그걸 되돌리는 기능(set/reset 등, 제품에 따라 다름), (4) ODR과 전력이다.

---

## 4. 데이터시트 읽기 (1) — full-scale range, sensitivity, resolution

### 4.1 정의

- **Full-scale range (FS)**: 센서가 포화 없이 잴 수 있는 범위. 가속도계는 보통 ±2/±4/±8/±16 g, 자이로는 ±125/±250/±500/±1000/±2000 dps 중에서 레지스터로 고른다(제품에 따라 더 작거나 큰 range가 추가된 것도 있다).
- **Sensitivity**: 1 단위 물리량이 몇 LSB인가. 데이터시트는 두 방식 중 하나로 쓴다: `LSB/g`, `LSB/dps` (Bosch·TDK가 흔히 씀) 또는 그 역수 `mg/LSB`, `mdps/LSB` (ST가 흔히 씀).
- **Resolution**: 1 LSB가 몇 g인가 = 1 / sensitivity. 단, **실제로 구분 가능한 최소 변화는 LSB가 아니라 noise가 정한다**(§5). 16-bit 출력이라고 16-bit 정밀도인 것은 아니다.

### 4.2 손계산

```
±4 g, 16-bit:   sensitivity = 32768 / 4 = 8192 LSB/g,   resolution = 1/8192 g = 0.122 mg
±2000 dps:      sensitivity = 32768 / 2000 = 16.384 LSB/dps,  resolution = 61.0 mdps
양자화 잡음 RMS = LSB / √12      (±4 g: 0.122 / 3.464 = 0.035 mg)
```

말로 하면, "range를 2배 넓히면 LSB가 2배 커진다." 그런데 noise가 1 mg 수준이면 LSB 0.122 mg든 0.488 mg든 큰 차이가 없다. 즉 **넓은 range의 대가는 생각보다 작은 경우가 많다** — 그래서 포화가 걱정되면 range를 넓히는 쪽이 대개 안전하다(단, 일부 센서는 range에 따라 noise 사양도 달라지니 표를 확인).

이 코드는 range별 sensitivity, resolution, 양자화 잡음을 표로 만들고, "데이터시트 감도 표 값 대신 32768/FS를 쓰면" 생기는 오차를 계산한다.

```python
import numpy as np
print("accel  FS   LSB/g    mg/LSB   양자화잡음 RMS(mg) = LSB/√12")
for fs_g in [2, 4, 8, 16]:
    lsb_per_g = 32768 / fs_g
    mg_per_lsb = 1000 / lsb_per_g
    print(f"     ±{fs_g:<3}  {lsb_per_g:7.1f}  {mg_per_lsb:7.4f}  {mg_per_lsb/np.sqrt(12):7.4f}")
print("gyro   FS     LSB/dps   mdps/LSB(=FS/32768)")
for fs_d in [125, 250, 500, 1000, 2000]:
    print(f"     ±{fs_d:<5} {32768/fs_d:8.3f}  {1000*fs_d/32768:8.3f}")
# 데이터시트 감도를 무시하고 32768/FS 로 계산하면? (예: 표 값이 70 mdps/LSB 인 경우)
raw = 10_000
print("raw 10000 →", round(raw * 0.070, 1), "dps (표 값 70 mdps/LSB) vs",
      round(raw * 2000 / 32768, 1), "dps (32768/FS 가정) → 오차",
      round(100 * (0.070 / (2000 / 32768) - 1), 1), "%")
```

```text
accel  FS   LSB/g    mg/LSB   양자화잡음 RMS(mg) = LSB/√12
     ±2    16384.0   0.0610   0.0176
     ±4     8192.0   0.1221   0.0352
     ±8     4096.0   0.2441   0.0705
     ±16    2048.0   0.4883   0.1410
gyro   FS     LSB/dps   mdps/LSB(=FS/32768)
     ±125    262.144     3.815
     ±250    131.072     7.629
     ±500     65.536    15.259
     ±1000    32.768    30.518
     ±2000    16.384    61.035
raw 10000 → 700.0 dps (표 값 70 mdps/LSB) vs 610.4 dps (32768/FS 가정) → 오차 14.7 %
```

출력에서 볼 것: 가속도 ±16 g에서도 양자화 잡음은 0.14 mg RMS로, 소비자용 가속도계의 noise(대략 1 mg 단위, §5)보다 작다. 마지막 줄이 중요한 함정이다. **감도는 반드시 데이터시트 표에서 가져온다.** 예를 들어 ST LSM6DS 계열 데이터시트의 자이로 감도 표는 ±2000 dps에서 70 mdps/LSB로 적혀 있는 것으로 알려져 있는데, 이는 2000/32768 = 61.0 mdps/LSB와 약 15% 다르다(즉 실제 코드 범위가 공칭 FS보다 조금 넓게 설계됨). Bosch·TDK 계열은 16.4 LSB/dps처럼 32768/FS에 가까운 값을 적는 경우가 많다. 어느 쪽이든 **"내 칩의 데이터시트 표 값"**을 쓰면 문제가 없다.

### 4.3 sensitivity tolerance — 감도 자체의 오차

데이터시트는 감도의 공칭값(typ)과 함께 **허용 오차**(예: ±몇 %)를 적는다. 이는 칩마다 감도가 조금씩 다르다는 뜻이다. 1% 감도 오차는 1 g 축에서 10 mg 오차이고, 자이로 1%는 360° 회전 후 3.6° 오차다. 공장에서 칩 단위로 trim 하지만, 정밀이 필요하면 기기 단위 calibration(6면 테스트, 턴테이블)을 한다(§7, G3).

---

## 5. 데이터시트 읽기 (2) — noise density와 RMS noise

### 5.1 왜 "√Hz"인가

데이터시트의 noise는 보통 **noise density**(spectral density)로 적힌다: 가속도계 `µg/√Hz`, 자이로 `mdps/√Hz` 또는 `dps/√Hz`. 이것은 "1 Hz 대역폭당 noise"다. 백색잡음(white noise)은 모든 주파수에 고르게 퍼져 있으므로, **대역폭을 넓게 열수록 더 많은 noise를 받아들인다.** 그래서 RMS noise는 대역폭의 제곱근에 비례한다.

```
RMS noise ≈ ND × √(BW_noise)

ND        : noise density  [µg/√Hz]
BW_noise  : 등가 잡음 대역폭(ENBW) [Hz] ≈ 필터 cutoff × 보정 계수
            (벽돌 필터: 1.0, 1차 RC: π/2 ≈ 1.57, 4차 Butterworth: ≈ 1.03)
```

말로 하면, "noise power는 대역폭에 비례하고, RMS는 power의 제곱근이니 √대역폭에 비례한다." Don에게는 익숙한 공식이다: RF의 thermal noise `kTB`(power ∝ B)와 같은 구조다.

### 5.2 손계산 — 면접 단골 문제

**문제**: noise density 120 µg/√Hz, bandwidth 100 Hz. RMS noise는?

```
RMS = 120 µg/√Hz × √100 Hz = 120 × 10 = 1200 µg = 1.2 mg  (≈ 0.0118 m/s²)
peak-to-peak ≈ 6.6 × RMS ≈ 7.9 mg     (Gaussian에서 99.9%가 ±3.3σ 안)
1차 필터라면 ENBW = 1.57 × 100 = 157 Hz → RMS = 120 × √157 ≈ 1.50 mg
```

자이로도 같다. 예: 5 mdps/√Hz × √50 Hz = 35.4 mdps RMS. 이 값이 "정지했을 때 자이로 출력이 흔들리는 폭"이다.

**대역폭을 줄이면 noise가 줄어든다** — 그래서 HAR처럼 느린 과제는 ODR과 LPF를 낮춰 noise와 전력을 같이 줄인다. 반대로 빠른 충격을 잡으려고 대역폭을 열면 noise가 늘어난다. 이것이 range·ODR·noise 사이의 기본 trade-off다.

### 5.3 코드로 확인

이 코드는 120 µg/√Hz 백색잡음을 만들고(Welch로 density를 다시 추정), 100 Hz 저역통과 필터를 거친 RMS가 `ND × √ENBW`와 맞는지 확인한다.

```python
import numpy as np
from scipy.signal import butter, sosfilt, welch
ND = 120e-6                         # noise density 120 µg/√Hz (g 단위)
fs = 3200.0                         # 내부 샘플링 (가정)
rng = np.random.default_rng(1)
n = int(fs * 600)                   # 10분
x = ND * np.sqrt(fs / 2) * rng.standard_normal(n)   # 단측 PSD = ND² 인 백색잡음
f, pxx = welch(x, fs=fs, nperseg=4096)              # 단측 PSD [g²/Hz]
print("Welch로 추정한 density:", round(1e6 * np.sqrt(np.median(pxx)), 1), "µg/√Hz")
print("필터 없음(대역 fs/2=1600 Hz) RMS:", round(1e3 * x.std(), 3), "mg",
      "| 이론", round(1e3 * ND * np.sqrt(fs / 2), 3), "mg")
for order, enbw_k in [(1, np.pi / 2), (4, 1.026)]:
    y = sosfilt(butter(order, 100, fs=fs, output="sos"), x)
    print(f"Butterworth {order}차, fc=100 Hz: RMS {1e3*y.std():.3f} mg | "
          f"ND·√100 = {1e3*ND*10:.3f} mg | ND·√(ENBW={enbw_k*100:.0f} Hz) = "
          f"{1e3*ND*np.sqrt(enbw_k*100):.3f} mg")
y = sosfilt(butter(4, 100, fs=fs, output="sos"), x)
print("peak-to-peak ≈ 6.6σ 규칙:", round(1e3 * 6.6 * y.std(), 2), "mg, 실제 99.9% 범위:",
      round(1e3 * (np.percentile(y, 99.95) - np.percentile(y, 0.05)), 2), "mg")
```

```text
Welch로 추정한 density: 119.8 µg/√Hz
필터 없음(대역 fs/2=1600 Hz) RMS: 4.795 mg | 이론 4.8 mg
Butterworth 1차, fc=100 Hz: RMS 1.436 mg | ND·√100 = 1.200 mg | ND·√(ENBW=157 Hz) = 1.504 mg
Butterworth 4차, fc=100 Hz: RMS 1.215 mg | ND·√100 = 1.200 mg | ND·√(ENBW=103 Hz) = 1.215 mg
peak-to-peak ≈ 6.6σ 규칙: 8.02 mg, 실제 99.9% 범위: 8.0 mg
```

출력에서 볼 것: (1) Welch 추정이 설정한 120 µg/√Hz를 되찾는다 — 실제 로그에서 noise density를 측정하는 방법이 바로 이것이다. (2) 4차 Butterworth는 `ND·√fc`와 거의 같다(ENBW ≈ 1.03 fc). (3) 1차 필터는 `ND·√fc`보다 확실히 크다. 이론 ENBW(1.57 fc) 값 1.50 mg보다 약간 작은 1.44 mg가 나온 것은 디지털 1차 필터(bilinear 변환)가 Nyquist 주파수에 zero를 가져 고주파 noise를 조금 더 깎기 때문이다. (4) "pk-pk ≈ 6.6σ"는 99.9% 범위와 잘 맞는다.

> 데이터시트 읽기 팁: noise는 **측정 조건**(ODR, 필터 설정, 전력 모드)과 함께 적혀 있다. 저전력 모드는 내부 평균 횟수가 적어 noise density가 더 큰 경우가 흔하다. "typ" 값만 있고 "max"가 없는 경우도 많다. 같은 조건끼리 비교해야 한다.

### 5.4 noise가 ML에 미치는 영향

- 제스처·HAR의 움직임 신호는 보통 수십~수백 mg, 수십~수백 dps라서 1 mg·50 mdps 수준의 noise는 대개 문제가 아니다. **문제가 되는 것은 작은 신호**다: 착용 감지의 미세한 맥박·호흡 진동, 정지 판정 문턱, 미세한 머리 끄덕임.
- 문턱(threshold) 기반 기능(any-motion 등)은 noise pk-pk보다 문턱이 충분히 커야 오작동이 없다. A2에서 배운 Gaussian 꼬리 계산이 그대로 쓰인다.
- 학습 데이터 augmentation(B7)에서 "Gaussian noise 추가"를 할 때 σ를 아무렇게나 정하지 말고 **데이터시트 noise RMS** 근처로 정하면 물리적으로 그럴듯하다.

---

## 6. 데이터시트 읽기 (3) — ODR, anti-aliasing, 디지털 LPF

### 6.1 정의

- **ODR (Output Data Rate)**: 출력 레지스터(또는 FIFO)가 새 샘플을 내놓는 속도. 예: 12.5, 25, 50, 100, 200, 400, 800, 1600 Hz … (제품마다 목록이 다르다).
- **Bandwidth**: 출력 신호가 통과시키는 주파수 범위. 대부분의 디지털 IMU는 내부에서 높은 속도로 샘플링한 뒤 **디지털 LPF**를 거쳐 ODR로 솎아낸다(decimation). LPF cutoff는 보통 ODR에 연동되어(예: ODR/2, ODR/4 근처) 설정되거나, 별도 레지스터로 고른다.
- **Anti-aliasing**: Nyquist(= ODR/2)보다 높은 주파수가 솎는 순간 낮은 주파수로 접혀 들어오는 현상(aliasing)을 막는 필터. G5에서 일반 이론을 다루고, 여기서는 IMU 설정 관점만 본다.

```
alias 주파수 = | f − k · ODR |  중 [0, ODR/2] 안에 들어오는 값   (k는 정수)
예) 37 Hz 진동, ODR 50 Hz → |37 − 50| = 13 Hz 로 보인다
```

말로 하면, "ODR의 정수배 근처에 있는 성분은 모두 저주파로 둔갑한다." 접힌 성분은 진짜 13 Hz 움직임과 구분할 방법이 없다 — 샘플링한 뒤에는 되돌릴 수 없다.

### 6.2 코드로 확인

이 코드는 2 Hz 손 움직임 + 37 Hz 진동(가정: 모터·차량 진동 등)을 1600 Hz로 만든 뒤, (a) 그냥 50 Hz로 솎기, (b) 20 Hz LPF 후 솎기를 비교한다.

```python
import numpy as np
from scipy.signal import butter, sosfilt
fs_in, odr, T = 1600.0, 50.0, 8.0             # 센서 내부 1600 Hz → 출력 ODR 50 Hz
t = np.arange(0, T, 1 / fs_in)
x = 0.5 * np.sin(2 * np.pi * 2 * t) + 0.3 * np.sin(2 * np.pi * 37 * t)  # 2 Hz 동작 + 37 Hz 진동(가정)
step = int(fs_in / odr)

def peaks(y, fs, k=2):
    Y = np.abs(np.fft.rfft(y)) * 2 / len(y)
    f = np.fft.rfftfreq(len(y), 1 / fs)
    idx = np.argsort(Y)[::-1][:k]
    return ", ".join(f"{f[i]:.2f} Hz (amp {Y[i]:.3f})" for i in sorted(idx))

print("원신호 (1600 Hz)          :", peaks(x, fs_in))
print("그냥 솎아내기 (50 Hz)      :", peaks(x[::step], odr))
sos = butter(6, 0.4 * odr, fs=fs_in, output="sos")       # 20 Hz 저역통과 후 솎기
print("LPF 20 Hz 후 솎기 (50 Hz) :", peaks(sosfilt(sos, x)[::step], odr))
print("Nyquist =", odr / 2, "Hz → 37 Hz는 |37 − 50| =", abs(37 - 50), "Hz로 접힌다")
```

```text
원신호 (1600 Hz)          : 2.00 Hz (amp 0.500), 37.00 Hz (amp 0.300)
그냥 솎아내기 (50 Hz)      : 2.00 Hz (amp 0.500), 13.00 Hz (amp 0.300)
LPF 20 Hz 후 솎기 (50 Hz) : 2.00 Hz (amp 0.500), 13.00 Hz (amp 0.007)
Nyquist = 25.0 Hz → 37 Hz는 |37 − 50| = 13 Hz로 접힌다
```

출력에서 볼 것: 그냥 솎으면 37 Hz 진동이 **진폭 그대로 13 Hz 가짜 성분**이 된다. LPF를 먼저 거치면 13 Hz 성분이 0.300 → 0.007로 40배 넘게 줄어든다. 2 Hz 진짜 움직임은 둘 다 보존된다.

### 6.3 IMU 설정에서의 실전 규칙

- **센서 내부 필터를 믿되 확인하라.** 대부분의 IMU는 ODR을 바꾸면 내부 LPF도 같이 바뀐다. 하지만 일부 저전력 모드는 내부 평균만 하거나 필터가 약해 aliasing이 남을 수 있다. 데이터시트의 "filter bandwidth vs ODR" 표와 블록도를 본다.
- **MCU에서 솎지 말 것** — 예를 들어 센서를 400 Hz로 두고 MCU에서 8개 중 1개만 골라 50 Hz를 만들면, 센서 내부 LPF는 400 Hz용이라 aliasing이 그대로 들어온다. MCU에서 솎으려면 그 전에 디지털 LPF(FIR/IIR)를 직접 걸어야 한다(G5).
- **필터에는 지연(group delay)이 있다.** cutoff를 낮출수록 지연이 커진다. 실시간 제스처 반응이나 머리 추적처럼 지연이 중요한 과제는 이 지연을 latency 예산(D6)에 넣는다.
- **학습 데이터와 기기 설정이 같아야 한다.** 학습 로그를 400 Hz로 모으고 기기에서는 50 Hz로 돌리면, 리샘플 방법·필터가 다르면 입력 분포가 달라진다(A4의 train/serve skew). 로그 헤더에 ODR·필터 설정을 남긴다(H3).

---

## 7. 데이터시트 읽기 (4) — offset, 온도 drift, 감도 오차, 비선형성, cross-axis

### 7.1 항목별 정의

| 항목 | 데이터시트 이름 예 | 뜻 | 보정 가능성 |
|---|---|---|---|
| zero-g offset | "Zero-g level offset accuracy" | 0 g일 때 나오는 값. 가속도계 bias | 공장·기기 calibration으로 대부분 제거 |
| zero-rate offset | "Zero-rate level" | 회전이 없을 때 자이로 출력. 자이로 bias | 정지 구간에서 추정해 빼기 (G3) |
| offset 온도 계수 | "Zero-g level change vs temperature", "TCO" | 온도 1 °C당 offset 변화 | 온도 센서로 보상 가능하지만 칩마다 다름 |
| 감도 오차 | "Sensitivity tolerance" | 감도 공칭값 대비 오차 (%) | 6면 테스트·턴테이블로 보정 |
| 감도 온도 계수 | "Sensitivity change vs temperature" | 온도 1 °C당 감도 변화 (%/°C) | 온도 보상 |
| 비선형성 | "Nonlinearity" (%FS) | 입력-출력 직선에서 벗어나는 정도 | 보통 무시하거나 다항식 보정 |
| cross-axis | "Cross-axis sensitivity" (%) | 다른 축 입력이 이 축에 새는 비율. 축 비직교 + 패키징 오차 | 3×3 행렬 보정 (G3) |
| 기판 실장 후 offset 변화 | 제품에 따라 "after soldering" 등으로 언급 | 리플로우·PCB 응력으로 offset 이동 | 기기 단위 calibration |

말로 하면, 센서 출력은 대략 다음 모델로 쓸 수 있다.

```
측정값 = M · 참값 + b + n
M : 3×3 행렬 (대각 = 1 + 감도 오차, 비대각 = cross-axis / misalignment)
b : bias (offset) — 온도 T의 함수 b(T)
n : noise (§5)
```

이 모델이 G3 calibration의 출발점이다. 여기서는 각 항이 **얼마나 큰 오차를 만드는지** 감을 잡는다.

### 7.2 손계산 — 온도 drift

offset 온도 계수가 ±0.1 mg/°C라고 **가정**하자(실제 값은 데이터시트에서 확인). 웨어러블은 책상(22 °C)에서 손목(30 °C 이상)으로, 겨울 밖(0 °C)으로 오간다.

```
22 °C → 32 °C (손목 착용, ΔT = 10 °C):   Δoffset = 0.1 × 10 = 1 mg
22 °C → 0 °C  (겨울 실외, ΔT = −22 °C):  Δoffset = 0.1 × 22 = 2.2 mg
자이로 offset 온도 계수 0.01 dps/°C 가정, ΔT = 20 °C → 0.2 dps → 1분이면 12° 적분 오차
```

말로 하면, "가속도계 온도 drift는 ML 입력으로는 대개 작지만, 자이로 drift는 적분하는 순간 커진다." 그래서 자이로 bias는 **기동 시 한 번 보정하고 끝**이 아니라 정지 구간마다 다시 추정한다(G3).

### 7.3 코드로 확인 — 오차 모델이 기울기 추정에 미치는 영향

이 코드는 감도 오차·cross-axis·offset이 있는 가속도계로 기울기(roll)를 추정했을 때의 오차를 보고, 6면 정지 테스트로 bias와 감도를 거꾸로 구한다.

```python
import numpy as np
# 측정 = M · 참값 + bias,  M = (1 + 감도 오차) 대각 + cross-axis 비대각
M = np.array([[1.02, 0.01, 0.00],      # x 감도 +2%, y→x cross-axis 1%
              [0.00, 0.99, 0.01],      # y 감도 −1%
              [0.01, 0.00, 1.00]])
bias = np.array([0.020, -0.015, 0.030])   # zero-g offset [g] (수십 mg 수준 가정)
for deg in [0, 45, 80]:                    # x축 기준으로 기울인 채 정지
    r = np.deg2rad(deg)
    f = np.array([0, np.sin(r), np.cos(r)])
    m = M @ f + bias
    roll_m = np.degrees(np.arctan2(m[1], m[2]))
    print(f"roll {deg:>2}°: 측정 {np.round(m, 3)} |m|={np.linalg.norm(m):.3f} g"
          f" → 측정 roll {roll_m:5.1f}°")
# 6면 정지 테스트(각 축 +1 g / −1 g)로 bias와 감도를 거꾸로 구하기 (G3에서 일반화)
up = M @ np.eye(3) + bias[:, None]          # 열 j = j축을 위로 놓았을 때
down = -M @ np.eye(3) + bias[:, None]       # 열 j = j축을 아래로 놓았을 때
print("추정 bias =", np.round((np.diag(up) + np.diag(down)) / 2, 3),
      " 추정 감도 =", np.round((np.diag(up) - np.diag(down)) / 2, 3))
```

```text
roll  0°: 측정 [ 0.02  -0.005  1.03 ] |m|=1.030 g → 측정 roll  -0.3°
roll 45°: 측정 [0.027 0.692 0.737] |m|=1.011 g → 측정 roll  43.2°
roll 80°: 측정 [0.03  0.962 0.204] |m|=0.983 g → 측정 roll  78.0°
추정 bias = [ 0.02  -0.015  0.03 ]  추정 감도 = [1.02 0.99 1.  ]
```

출력에서 볼 것: 평평할 때 크기가 1.030 g — 정지했는데 1 g가 아니면 offset·감도 오차의 신호다(H7 데이터 품질 체크에 쓸 수 있다). roll 45°에서 1.8° 오차가 난다. 6면 테스트(각 축을 위·아래로 한 번씩)는 `(up + down)/2 = bias`, `(up − down)/2 = 감도`로 대각 성분을 정확히 복원한다. 비대각(cross-axis)까지 구하려면 더 많은 자세가 필요하다(G3).

### 7.4 비선형성과 cross-axis — 크기 감각

- **비선형성 0.5 %FS** (가정): ±8 g range면 FS의 0.5% = 40 mg. 큰 가속도 근처에서만 의미가 있고, 대부분의 ML 과제에서는 무시한다.
- **cross-axis 1%**: z에 1 g가 걸려 있으면 x에 10 mg가 샌다. 기울기로 환산하면 atan(0.01) ≈ 0.57°. 자이로 cross-axis는 빠른 회전에서 문제가 된다: z축으로 500 dps 회전하면 x축에 5 dps가 새어 나온다.

---

## 8. 지자기 데이터시트와 방해 — hard iron, soft iron

### 8.1 정의

- **Hard iron**: 기기에 붙어 **같이 움직이는** 영구자석·자화된 부품(스피커, 진동 모터, 자석 충전 커넥터, 나사 등)이 만드는 고정 자기장. 측정값에 **일정한 offset 벡터**로 더해진다. 기기를 한 바퀴 돌리면 원의 중심이 원점에서 벗어난다.
- **Soft iron**: 주변의 연자성체(철판, 실드 캔 등)가 지구 자기장을 **휘게** 만드는 효과. 방향에 따라 감도가 달라져 원이 **타원**이 된다(3×3 행렬로 모델링).
- **외부 방해**: 기기 밖의 자석·전류(노트북 스피커, 전철, 철근)는 기기와 같이 움직이지 않으므로 calibration으로 제거되지 않는다. 일시적인 heading 오류가 생기고, 퓨전 알고리즘은 "자기장 크기가 평소(지구 자기장)와 다르면 지자기를 덜 믿는다" 같은 방어 로직을 둔다.

### 8.2 손계산

지구 자기장 수평 성분이 20 µT(가정)인데 스피커가 x 방향으로 15 µT offset을 만든다면, 북쪽을 볼 때 `(20 + 15, 0)`, 동쪽을 볼 때 `(15, −20)`이 측정된다. 동쪽을 볼 때 계산된 heading은 `atan2(20, 15) = 53°`로, 참값 90°와 37° 차이 난다. **offset이 지구 자기장과 비슷한 크기면 heading이 완전히 망가진다.**

### 8.3 코드로 확인

이 코드는 기기를 수평으로 한 바퀴 돌릴 때 hard iron + soft iron이 섞인 측정값을 만들고, 가장 단순한 보정(min/max 중점 빼기, 축별 스케일 맞추기)이 heading 오차를 얼마나 줄이는지 본다.

```python
import numpy as np
H = 20.0                                       # 지구 자기장 수평 성분 [µT] (지역마다 다름, 가정)
psi = np.deg2rad(np.arange(0, 360, 5))         # 기기를 수평으로 한 바퀴 돌린다 (참 heading)
m_true = np.stack([H * np.cos(psi), -H * np.sin(psi)], axis=1)   # 기기 좌표계에서 본 북쪽 자기장
hard = np.array([15.0, -6.0])                  # hard iron: 기기에 붙은 자석(스피커 등) → 일정 offset
soft = np.array([[1.20, 0.05], [0.05, 0.90]])  # soft iron: 주변 철판이 장을 휘게 함 → 타원
m_meas = m_true @ soft.T + hard

def heading(m):                                # 북쪽=0°, 시계방향 + (이 예제의 약속)
    return np.degrees(np.arctan2(-m[:, 1], m[:, 0])) % 360

def err(h):                                    # 각도 차이를 -180~180으로 감싸기
    return np.abs((h - np.degrees(psi) + 180) % 360 - 180)

print("보정 전        최대 heading 오차:", round(err(heading(m_meas)).max(), 1), "deg")
center = (m_meas.max(0) + m_meas.min(0)) / 2   # 가장 단순한 hard-iron 추정: min/max 중점
radius = (m_meas.max(0) - m_meas.min(0)) / 2
print("추정 hard iron:", np.round(center, 2), "µT (참값", hard, ")")
m_hi = m_meas - center
print("hard iron만 보정 최대 오차   :", round(err(heading(m_hi)).max(), 1), "deg")
m_si = m_hi / radius * radius.mean()           # 축별 스케일만 맞추는 간단 soft-iron 보정
print("축 스케일까지 보정 최대 오차 :", round(err(heading(m_si)).max(), 1), "deg")
```

```text
보정 전        최대 heading 오차: 56.2 deg
추정 hard iron: [15. -6.] µT (참값 [15. -6.] )
hard iron만 보정 최대 오차   : 8.7 deg
축 스케일까지 보정 최대 오차 : 3.2 deg
```

출력에서 볼 것: 보정 전 최대 오차 56°는 heading으로 쓸 수 없는 수준이다. hard iron만 빼도 8.7°로 줄고, 축 스케일까지 맞추면 3.2°. 남은 오차는 soft iron 행렬의 비대각 성분(0.05) 때문이고, 이것은 타원체 fitting(G3)으로 제거한다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 440">
<line x1="120.0" y1="390.0" x2="495.0" y2="390.0" stroke="currentColor" stroke-width="1"/> <line x1="120.0" y1="390.0" x2="120.0" y2="40.0" stroke="currentColor" stroke-width="1"/> <line x1="170.0" y1="390.0" x2="170.0" y2="395.0" stroke="currentColor" stroke-width="1"/> <text x="170.0" y="408.0" font-size="12" text-anchor="middle">−20</text> <line x1="170.0" y1="390.0" x2="170.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="270.0" y1="390.0" x2="270.0" y2="395.0" stroke="currentColor" stroke-width="1"/> <text x="270.0" y="408.0" font-size="12" text-anchor="middle">0</text> <line x1="270.0" y1="390.0" x2="270.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="370.0" y1="390.0" x2="370.0" y2="395.0" stroke="currentColor" stroke-width="1"/> <text x="370.0" y="408.0" font-size="12" text-anchor="middle">20</text>
<line x1="370.0" y1="390.0" x2="370.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="470.0" y1="390.0" x2="470.0" y2="395.0" stroke="currentColor" stroke-width="1"/> <text x="470.0" y="408.0" font-size="12" text-anchor="middle">40</text> <line x1="470.0" y1="390.0" x2="470.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="115.0" y1="315.0" x2="120.0" y2="315.0" stroke="currentColor" stroke-width="1"/> <text x="112.0" y="319.0" font-size="12" text-anchor="end">−20</text> <line x1="120.0" y1="315.0" x2="495.0" y2="315.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="115.0" y1="215.0" x2="120.0" y2="215.0" stroke="currentColor" stroke-width="1"/> <text x="112.0" y="219.0" font-size="12" text-anchor="end">0</text> <line x1="120.0" y1="215.0" x2="495.0" y2="215.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/>
<line x1="115.0" y1="115.0" x2="120.0" y2="115.0" stroke="currentColor" stroke-width="1"/> <text x="112.0" y="119.0" font-size="12" text-anchor="end">20</text> <line x1="120.0" y1="115.0" x2="495.0" y2="115.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <text x="307.5" y="426.0" font-size="13" text-anchor="middle">mx [µT]</text> <text x="114.0" y="30.0" font-size="12" text-anchor="start">my [µT]</text> <polyline points="370.0,215.0 369.6,223.7 368.5,232.4 366.6,240.9 364.0,249.2 360.6,257.3 356.6,265.0 351.9,272.4 346.6,279.3 340.7,285.7 334.3,291.6 327.4,296.9 320.0,301.6 312.3,305.6 304.2,309.0 295.9,311.6 287.4,313.5 278.7,314.6 270.0,315.0 261.3,314.6 252.6,313.5 244.1,311.6 235.8,309.0 227.7,305.6 220.0,301.6 212.6,296.9 205.7,291.6 199.3,285.7 193.4,279.3 188.1,272.4 183.4,265.0 179.4,257.3 176.0,249.2 173.4,240.9 171.5,232.4 170.4,223.7 170.0,215.0 170.4,206.3 171.5,197.6 173.4,189.1 176.0,180.8 179.4,172.7 183.4,165.0 188.1,157.6 193.4,150.7 199.3,144.3 205.7,138.4 212.6,133.1 220.0,128.4 227.7,124.4 235.8,121.0 244.1,118.4 252.6,116.5 261.3,115.4 270.0,115.0 278.7,115.4 287.4,116.5 295.9,118.4 304.2,121.0 312.3,124.4 320.0,128.4 327.4,133.1 334.3,138.4 340.7,144.3 346.6,150.7 351.9,157.6 356.6,165.0 360.6,172.7 364.0,180.8 366.6,189.1 368.5,197.6 369.6,206.3 370.0,215.0" fill="none" stroke="#4a7bd0" stroke-width="2"/> <polyline points="465.0,240.0 464.1,247.9 462.3,255.7 459.6,263.5 456.1,271.1 451.6,278.5 446.4,285.7 440.4,292.5 433.7,299.0 426.3,305.1 418.3,310.7 409.7,315.9 400.7,320.4 391.2,324.5 381.3,327.9 371.2,330.6 360.9,332.8 350.5,334.2 340.0,335.0 329.6,335.1 319.2,334.5 309.1,333.2 299.3,331.3 289.8,328.7 280.7,325.4 272.1,321.6 264.0,317.2 256.6,312.2 249.9,306.7 243.8,300.7 238.6,294.3 234.1,287.6 230.5,280.5 227.8,273.1 226.0,265.6 225.0,257.8 225.0,250.0 225.9,242.1 227.7,234.3 230.4,226.5 233.9,218.9 238.4,211.5 243.6,204.3 249.6,197.5 256.3,191.0 263.7,184.9 271.7,179.3 280.3,174.1 289.3,169.6 298.8,165.5 308.7,162.1 318.8,159.4 329.1,157.2 339.5,155.8 350.0,155.0 360.4,154.9 370.8,155.5 380.9,156.8 390.7,158.7 400.2,161.3 409.3,164.6 417.9,168.4 426.0,172.8 433.4,177.8 440.1,183.3 446.2,189.3 451.4,195.7 455.9,202.4 459.5,209.5 462.2,216.9 464.0,224.4 465.0,232.2 465.0,240.0" fill="none" stroke="#e08a3c" stroke-width="2"/> <circle cx="465.0" cy="240.0" r="2.5" fill="#e08a3c"/> <circle cx="459.6" cy="263.5" r="2.5" fill="#e08a3c"/> <circle cx="446.4" cy="285.7" r="2.5" fill="#e08a3c"/>
<circle cx="426.3" cy="305.1" r="2.5" fill="#e08a3c"/> <circle cx="400.7" cy="320.4" r="2.5" fill="#e08a3c"/> <circle cx="371.2" cy="330.6" r="2.5" fill="#e08a3c"/> <circle cx="340.0" cy="335.0" r="2.5" fill="#e08a3c"/> <circle cx="309.1" cy="333.2" r="2.5" fill="#e08a3c"/> <circle cx="280.7" cy="325.4" r="2.5" fill="#e08a3c"/> <circle cx="256.6" cy="312.2" r="2.5" fill="#e08a3c"/> <circle cx="238.6" cy="294.3" r="2.5" fill="#e08a3c"/> <circle cx="227.8" cy="273.1" r="2.5" fill="#e08a3c"/> <circle cx="225.0" cy="250.0" r="2.5" fill="#e08a3c"/>
<circle cx="230.4" cy="226.5" r="2.5" fill="#e08a3c"/> <circle cx="243.6" cy="204.3" r="2.5" fill="#e08a3c"/> <circle cx="263.7" cy="184.9" r="2.5" fill="#e08a3c"/> <circle cx="289.3" cy="169.6" r="2.5" fill="#e08a3c"/> <circle cx="318.8" cy="159.4" r="2.5" fill="#e08a3c"/> <circle cx="350.0" cy="155.0" r="2.5" fill="#e08a3c"/> <circle cx="380.9" cy="156.8" r="2.5" fill="#e08a3c"/> <circle cx="409.3" cy="164.6" r="2.5" fill="#e08a3c"/> <circle cx="433.4" cy="177.8" r="2.5" fill="#e08a3c"/> <circle cx="451.4" cy="195.7" r="2.5" fill="#e08a3c"/>
<circle cx="462.2" cy="216.9" r="2.5" fill="#e08a3c"/> <circle cx="270.0" cy="215.0" r="4" fill="#4a7bd0"/> <circle cx="345.0" cy="245.0" r="4" fill="#e08a3c"/> <line x1="270.0" y1="215.0" x2="345.0" y2="245.0" stroke="#d0564a" stroke-width="1.5"/> <text x="310.0" y="265.0" font-size="12" text-anchor="middle">hard iron (15, −6)</text> <line x1="520.0" y1="60.0" x2="540.0" y2="60.0" stroke="#4a7bd0" stroke-width="3"/> <text x="520.0" y="80.0" font-size="12" text-anchor="start">이상적: 원점 중심 원</text> <line x1="520.0" y1="110.0" x2="540.0" y2="110.0" stroke="#e08a3c" stroke-width="3"/> <text x="520.0" y="130.0" font-size="12" text-anchor="start">측정: 이동 + 타원</text>
</svg>
```

그림 6 — 기기를 한 바퀴 돌릴 때 지자기 측정값의 궤적(실제 계산). 파랑은 이상적인 원(원점 중심, 반지름 20 µT), 주황은 hard iron으로 (15, −6) µT 이동하고 soft iron으로 타원이 된 측정값. calibration은 주황을 파랑으로 되돌리는 변환을 찾는 일이다.

**웨어러블에서의 의미**: 귀에 거는 기기나 이어폰처럼 **스피커 바로 옆**에 지자기 센서가 있으면 hard iron이 지구 자기장보다 클 수도 있다. 이 경우 range 안에 들어오는지(포화 여부)부터 확인해야 하고, 스피커 구동 전류가 만드는 **시간에 따라 변하는** 자기장은 calibration으로 제거되지 않는다. 그래서 heading이 필요 없는 과제라면 6축만으로 설계하는 것이 흔하다. 9축이 꼭 필요하면 기구 설계 단계에서 센서 위치를 자석에서 떨어뜨린다.

---

## 9. Allan deviation — bias instability와 angle random walk를 한 그래프에서 읽기

### 9.1 왜 표준편차 하나로는 부족한가

정지한 자이로를 1시간 기록해서 표준편차를 구하면 하나의 숫자가 나온다. 그런데 그 숫자는 **어떤 시간 척도의 흔들림인지** 말해 주지 않는다. 자이로 오차에는 성격이 다른 성분이 섞여 있다.

- 빠르게 흔들리는 **백색잡음**: 오래 평균하면 줄어든다.
- 천천히 떠도는 **bias 흔들림**(flicker, 1/f 성격): 평균해도 어느 수준 아래로는 안 줄어든다.
- 아주 천천히 걷는 **rate random walk**: 오래 평균할수록 오히려 커진다.

Allan deviation은 "**τ초 동안 평균한 값이, 그다음 τ초 평균과 얼마나 다른가**"를 τ를 바꿔 가며 계산한 것이다. 원래 원자시계·오실레이터의 주파수 안정도를 평가하려고 만든 도구라서, Don이 RF에서 봤을 수도 있다(오실레이터 Allan deviation 그래프).

### 9.2 정의

```
1. 샘플 주기 dt, 각속도 ω_k 를 적분해 각도 θ_k = Σ ω · dt 를 만든다
2. 평균 시간 τ = m · dt 에 대해 (overlapping Allan variance)
   σ²(τ) = 1 / (2 τ² (N − 2m)) · Σ_k ( θ_(k+2m) − 2 θ_(k+m) + θ_k )²
3. σ(τ) = √σ²(τ) 를 log-log 그래프로 그린다
```

말로 하면, "θ_(k+m) − θ_k 는 τ 동안의 평균 각속도 × τ 다. 연속된 두 τ 구간 평균의 차이를 제곱해서 평균하고 반으로 나눈 것이 Allan variance다." 2차 차분이라서 **고정 bias는 자동으로 사라진다** — Allan deviation은 bias 크기가 아니라 bias의 **안정성**을 본다.

log-log 그래프에서 각 잡음은 고유한 기울기를 가진다.

| 잡음 종류 | Allan deviation 기울기 | 읽는 법 | 단위 |
|---|---|---|---|
| angle random walk (ARW, 백색 rate noise) | −1/2 | 기울기 −1/2 직선을 τ = 1 s에서 읽은 값 = N | dps/√Hz = °/√s, ×60 → °/√h |
| bias instability (flicker) | 0 (바닥) | 곡선의 최솟값 / 0.664 = B | °/h 또는 dps |
| rate random walk (RRW) | +1/2 | 기울기 +1/2 직선을 τ = 3 s에서 읽은 값 = K | °/s/√s |
| 양자화 잡음 | −1 | τ = √3 s에서 읽음 | ° |

(표의 0.664, τ = 3 s, τ = √3 s 같은 상수는 IEEE Std 952의 정의에서 나온다.)

### 9.3 코드 — Allan deviation 직접 구현

이 코드는 6시간짜리 정지 자이로 신호(고정 bias 0.5 dps + 백색잡음 + flicker 잡음 + rate random walk)를 만들고, overlapping Allan deviation을 numpy로 계산해 ARW와 bias instability를 읽는다.

```python
import numpy as np
fs, T = 100.0, 6 * 3600                     # 100 Hz, 6시간 정지 로그 (시뮬레이션)
n = int(fs * T); rng = np.random.default_rng(42)
ND = 0.007                                  # 데이터시트식 rate noise density [dps/√Hz] (단측)
B, K, bias = 0.002, 1e-4, 0.5               # 바이어스 불안정성 [dps], RRW [dps/√s], 고정 bias [dps]
white = ND * np.sqrt(fs / 2) * rng.standard_normal(n)        # RMS = ND·√(fs/2)
# flicker(1/f) 잡음: 양측 PSD = B²/(2πf) → 주파수 영역에서 모양 만들기
F = np.fft.rfft(rng.standard_normal(n) * np.sqrt(fs / 2))
f = np.fft.rfftfreq(n, 1 / fs); f[0] = f[1]
flick = np.fft.irfft(F * np.sqrt(2 * B**2 / (2 * np.pi * f)), n)   # 단측 = 2×양측
rrw = np.cumsum(K / np.sqrt(fs) * rng.standard_normal(n))         # rate random walk
w = bias + white + flick + rrw              # 자이로 출력 [dps]

def allan_dev(w, fs, taus):
    th = np.concatenate([[0.0], np.cumsum(w) / fs])          # 적분된 각도 [deg]
    out = []
    for tau in taus:
        m = int(round(tau * fs))
        d = th[2*m:] - 2 * th[m:-m] + th[:-2*m]              # 2차 차분 (overlapping)
        out.append(np.sqrt(np.mean(d**2) / (2 * (m / fs)**2)))
    return np.array(out)

taus = np.logspace(-1.5, np.log10(T / 10), 30)
ad = allan_dev(w, fs, taus)
for t_, a_ in zip(taus[::3], ad[::3]):
    print(f"tau={t_:9.2f} s  σ={a_*3600:8.2f} deg/h")
s1 = np.interp(0.0, np.log10(taus), np.log10(ad))            # τ=1 s 값
print("σ(1s) =", round(10**s1, 5), "dps → ARW =", round(10**s1 * 60, 3), "deg/√h",
      "(설정 N = ND/√2 =", round(ND / np.sqrt(2) * 60, 3), ")")
i = np.argmin(ad)
print("최솟값 σ =", round(ad[i] * 3600, 2), "deg/h @ τ =", round(taus[i], 1),
      "s → BI ≈ σmin/0.664 =", round(ad[i] / 0.664 * 3600, 2), "deg/h (설정 B =", B * 3600, ")")
np.save("allan.npy", np.vstack([taus, ad]))   # 그림 7을 그리려고 저장
```

```text
tau=     0.03 s  σ=  103.08 deg/h
tau=     0.10 s  σ=   56.67 deg/h
tau=     0.32 s  σ=   31.74 deg/h
tau=     1.00 s  σ=   18.41 deg/h
tau=     3.17 s  σ=   11.11 deg/h
tau=    10.01 s  σ=    7.22 deg/h
tau=    31.67 s  σ=    5.68 deg/h
tau=   100.19 s  σ=    5.37 deg/h
tau=   316.90 s  σ=    5.87 deg/h
tau=  1002.40 s  σ=    8.62 deg/h
σ(1s) = 0.00511 dps → ARW = 0.307 deg/√h (설정 N = ND/√2 = 0.297 )
최솟값 σ = 5.33 deg/h @ τ = 68.3 s → BI ≈ σmin/0.664 = 8.02 deg/h (설정 B = 7.2 )
```

출력에서 볼 것:

- τ가 작을 때 σ는 τ가 10배 늘 때 약 √10 ≈ 3.2배씩 준다(0.03 s 103 → 0.32 s 31.7 deg/h) — 기울기 −1/2, 백색잡음 구간이다. τ가 10 s를 넘으면 줄어드는 속도가 느려지며 바닥으로 들어간다.
- τ = 1 s에서 읽은 σ = 0.0051 dps → ARW 0.307 °/√h. 설정값 0.297과 3% 차이인 것은 τ = 1 s에서도 flicker 성분이 조금 섞이기 때문이다.
- 바닥은 τ ≈ 68 s에서 5.33 °/h. 0.664로 나누면 8.0 °/h로, 설정한 B = 7.2 °/h보다 조금 크다. 바닥 근처에는 백색잡음과 RRW도 남아 있어서 바닥이 살짝 들리기 때문이다 — **그래프에서 읽는 값은 근사**라는 것을 기억한다.
- 고정 bias 0.5 dps(= 1800 °/h)는 그래프 어디에도 나타나지 않는다. 2차 차분이 지웠다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 340">
<line x1="80.0" y1="290.0" x2="620.0" y2="290.0" stroke="currentColor" stroke-width="1"/> <line x1="80.0" y1="290.0" x2="80.0" y2="40.0" stroke="currentColor" stroke-width="1"/> <line x1="80.0" y1="290.0" x2="80.0" y2="295.0" stroke="currentColor" stroke-width="1"/> <text x="80.0" y="308.0" font-size="12" text-anchor="middle">0.01</text> <line x1="80.0" y1="290.0" x2="80.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="170.0" y1="290.0" x2="170.0" y2="295.0" stroke="currentColor" stroke-width="1"/> <text x="170.0" y="308.0" font-size="12" text-anchor="middle">0.1</text> <line x1="170.0" y1="290.0" x2="170.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="260.0" y1="290.0" x2="260.0" y2="295.0" stroke="currentColor" stroke-width="1"/> <text x="260.0" y="308.0" font-size="12" text-anchor="middle">1</text>
<line x1="260.0" y1="290.0" x2="260.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="350.0" y1="290.0" x2="350.0" y2="295.0" stroke="currentColor" stroke-width="1"/> <text x="350.0" y="308.0" font-size="12" text-anchor="middle">10</text> <line x1="350.0" y1="290.0" x2="350.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="440.0" y1="290.0" x2="440.0" y2="295.0" stroke="currentColor" stroke-width="1"/> <text x="440.0" y="308.0" font-size="12" text-anchor="middle">100</text> <line x1="440.0" y1="290.0" x2="440.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="530.0" y1="290.0" x2="530.0" y2="295.0" stroke="currentColor" stroke-width="1"/> <text x="530.0" y="308.0" font-size="12" text-anchor="middle">1000</text> <line x1="530.0" y1="290.0" x2="530.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/>
<line x1="620.0" y1="290.0" x2="620.0" y2="295.0" stroke="currentColor" stroke-width="1"/> <text x="620.0" y="308.0" font-size="12" text-anchor="middle">10⁴</text> <line x1="620.0" y1="290.0" x2="620.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="75.0" y1="290.0" x2="80.0" y2="290.0" stroke="currentColor" stroke-width="1"/> <text x="72.0" y="294.0" font-size="12" text-anchor="end">1</text> <line x1="80.0" y1="290.0" x2="620.0" y2="290.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="75.0" y1="206.7" x2="80.0" y2="206.7" stroke="currentColor" stroke-width="1"/> <text x="72.0" y="210.7" font-size="12" text-anchor="end">10</text> <line x1="80.0" y1="206.7" x2="620.0" y2="206.7" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="75.0" y1="123.3" x2="80.0" y2="123.3" stroke="currentColor" stroke-width="1"/>
<text x="72.0" y="127.3" font-size="12" text-anchor="end">100</text> <line x1="80.0" y1="123.3" x2="620.0" y2="123.3" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="75.0" y1="40.0" x2="80.0" y2="40.0" stroke="currentColor" stroke-width="1"/> <text x="72.0" y="44.0" font-size="12" text-anchor="end">1000</text> <line x1="80.0" y1="40.0" x2="620.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <text x="350.0" y="326.0" font-size="13" text-anchor="middle">averaging time τ [s] (log)</text> <text x="74.0" y="30.0" font-size="12" text-anchor="start">σ(τ) [deg/h] (log)</text> <polyline points="125.0,122.2 140.0,131.5 155.0,137.5 170.0,143.9 185.0,151.2 200.0,158.2 215.0,164.9 230.0,171.2 245.0,178.0 260.0,184.6 275.0,191.0 290.0,197.0 305.0,202.8 320.0,208.5 335.0,213.6 350.1,218.5 365.1,222.6 380.1,225.1 395.1,227.2 410.1,228.9 425.1,229.5 440.1,229.2 455.1,228.5 470.1,227.7 485.1,226.0 500.1,222.1 515.1,216.8 530.1,212.1 545.1,210.4 560.1,208.7" fill="none" stroke="#4a7bd0" stroke-width="2.5"/> <polyline points="80.0,101.2 392.9,246.1" fill="none" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="6 4"/> <polyline points="482.9,244.4 611.3,185.0" fill="none" stroke="#3f9a6b" stroke-width="1.5" stroke-dasharray="6 4"/>
<line x1="350.0" y1="229.5" x2="572.9" y2="229.5" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="6 4"/> <circle cx="260.0" cy="184.6" r="5" fill="#e08a3c"/> <circle cx="425.1" cy="229.5" r="5" fill="#d0564a"/> <text x="268.0" y="176.6" font-size="12" text-anchor="start">τ=1 s: σ=18.4 deg/h = 0.0051 dps → ARW</text> <text x="87.1" y="90.2" font-size="12" text-anchor="start">기울기 −1/2: 백색잡음 (ARW)</text> <text x="425.1" y="251.5" font-size="12" text-anchor="middle">바닥 5.3 deg/h → BI ≈ 바닥/0.664</text> <text x="615.9" y="141.8" font-size="12" text-anchor="end">기울기 +1/2: rate random walk</text>
</svg>
```

그림 7 — 시뮬레이션한 자이로의 Allan deviation (실제 계산한 곡선). 주황 점선은 기울기 −1/2(백색잡음, τ = 1 s에서 ARW를 읽는다), 빨강 점선은 바닥(bias instability), 초록 점선은 기울기 +1/2(rate random walk).

### 9.4 단위 함정 — √2와 60

- **60**: `1 √h = 60 √s` 이므로 `ARW [°/√h] = N [°/√s] × 60`. dps/√Hz와 °/√s는 같은 단위다.
- **√2**: 데이터시트의 noise density는 보통 "RMS = ND × √BW"로 정의된 **단측(one-sided)** 값이다. IEEE식 Allan 분석의 N은 양측(two-sided) PSD 기준이라 `N = ND / √2`가 된다. 위 코드에서 설정 N을 `ND/√2`로 비교한 이유다. 실무 문서에서는 이 √2를 무시하고 "ARW ≈ ND × 60"으로 쓰는 경우도 흔하다. **비교할 때 같은 규약인지**를 확인하는 습관이 중요하다(이 노트는 단측 ND 기준으로 일관되게 계산했다).
- bias instability를 °/h로 쓰는 문서와 dps로 쓰는 문서가 섞여 있다. `1 dps = 3600 °/h`.

### 9.5 실제로 측정하는 법

1. 기기를 진동 없는 곳에 **고정**하고, 온도가 안정될 때까지 기다린다(워밍업).
2. 최종 제품과 같은 ODR·필터·전력 모드로 **수 시간** 기록한다. 읽고 싶은 최대 τ의 10배 이상 길이가 필요하다(위 예: 6시간 → τ 최대 약 36분).
3. raw count를 그대로 저장한다(A6, H1). 샘플 누락이 있으면 θ 적분이 틀어지므로 타임스탬프로 검사한다(G7).
4. 축마다 Allan deviation을 그리고, 같은 모델의 여러 칩을 비교한다(칩 간 편차).

**ML 엔지니어에게 의미**: Allan 그래프는 "평균 몇 초로 bias를 추정하면 가장 좋은가"를 알려 준다(바닥의 τ). G3의 정지 구간 bias 추정 창 길이를 정할 때 쓴다. 또 데이터시트끼리 칩을 비교할 때, 데이터시트에 ARW·bias instability가 없으면 직접 재야 한다.

---

## 10. 적분의 결과 — 왜 IMU만으로 위치를 알 수 없나

### 10.1 자이로 → 각도: bias는 선형으로, noise는 √t로 자란다

```
각도 오차(t) = b · t   +   N · √t
              (bias)       (angle random walk)
```

말로 하면, "bias는 시간에 비례해 쌓이고, 백색잡음은 random walk라서 √t로 쌓인다." 긴 시간에서는 bias 항이 압도한다.

**손계산**: bias 0.5 dps(보정 안 한 소비자 자이로에서 나올 수 있는 크기, 가정)

```
1분 = 60 s → 0.5 × 60 = 30°           ← 1분이면 방향이 30° 틀어진다
기동 시 bias를 0.05 dps까지 보정했다면 → 3°/분
```

이 코드는 bias별 1분 각도 오차를 계산하고, bias 없이 noise만 적분했을 때 각도 오차가 √t로 자라는지 Monte Carlo로 확인한다.

```python
import numpy as np
fs, T, runs = 100.0, 60.0, 500
n = int(fs * T); dt = 1 / fs
ND = 0.007                                   # rate noise density [dps/√Hz] (단측)
rng = np.random.default_rng(7)
for b in [0.5, 0.05, 0.005]:                 # 남은 bias [dps]
    print(f"bias {b:>5} dps → 1분 뒤 각도 오차 {b*60:6.2f} deg  (= {b*3600:6.0f} deg/h)")
# 잡음만 적분: 각도는 random walk → 표준편차 ∝ √t
w = ND * np.sqrt(fs / 2) * rng.standard_normal((runs, n))   # bias 0, 잡음만
angle = np.cumsum(w, axis=1) * dt
for t in [1, 10, 60]:
    k = int(t * fs) - 1
    print(f"t={t:>2} s: 각도 std 실측 {angle[:, k].std():.4f} deg, "
          f"이론 (ND/√2)·√t = {ND/np.sqrt(2)*np.sqrt(t):.4f} deg")
```

```text
bias   0.5 dps → 1분 뒤 각도 오차  30.00 deg  (=   1800 deg/h)
bias  0.05 dps → 1분 뒤 각도 오차   3.00 deg  (=    180 deg/h)
bias 0.005 dps → 1분 뒤 각도 오차   0.30 deg  (=     18 deg/h)
t= 1 s: 각도 std 실측 0.0051 deg, 이론 (ND/√2)·√t = 0.0049 deg
t=10 s: 각도 std 실측 0.0151 deg, 이론 (ND/√2)·√t = 0.0157 deg
t=60 s: 각도 std 실측 0.0378 deg, 이론 (ND/√2)·√t = 0.0383 deg
```

출력에서 볼 것: noise만으로는 60초 뒤에도 0.04° 수준이다. 각도 drift의 주범은 noise가 아니라 **bias**다. 그래서 자이로에서 가장 중요한 숫자는 noise density보다 bias(와 그 안정성, 온도 drift)인 경우가 많다.

### 10.2 가속도계 → 위치: 두 번 적분하면 t²

```
위치 오차(t) = ½ · b_a · t²      (가속도 bias b_a)
             + (ND/√2) · t^1.5 / √3   (백색잡음, 1σ)
```

말로 하면, "속도는 bias × t로, 위치는 ½ × bias × t²로 자란다." 그리고 더 무서운 것은 **자세 오차로 새는 중력**이다. 자세를 1° 틀리면 `g × sin 1° = 0.171 m/s²`(17 mg)가 수평 가속도로 둔갑한다. 이것은 1 mg bias의 17배다.

이 코드는 noise만 두 번 적분했을 때의 위치 오차(Monte Carlo vs 이론)와, bias 1 mg·자세 오차 1°의 위치 오차를 계산한다.

```python
import numpy as np
g, fs = 9.80665, 100.0
dt = 1 / fs
rng = np.random.default_rng(8)
ND = 120e-6 * g                              # 120 µg/√Hz → m/s²/√Hz
runs, T = 300, 30.0
n = int(T * fs)
a = ND * np.sqrt(fs / 2) * rng.standard_normal((runs, n))   # 잡음만 (bias 0)
v = np.cumsum(a, axis=1) * dt                # 1번 적분 → 속도
p = np.cumsum(v, axis=1) * dt                # 2번 적분 → 위치
for t in [1, 10, 30]:
    k = int(t * fs) - 1
    th = ND / np.sqrt(2) * t**1.5 / np.sqrt(3)
    print(f"잡음만 t={t:>2}s: 위치 std {p[:, k].std()*100:7.3f} cm (이론 {th*100:7.3f} cm)")
for name, acc_err in [("bias 1 mg      ", 1e-3 * g),
                      ("자세 오차 1° → 중력 누설", g * np.sin(np.deg2rad(1)))]:
    print(name, "→", ", ".join(f"{t}s: {0.5*acc_err*t**2:8.3f} m" for t in [1, 10, 30, 60]))
```

```text
잡음만 t= 1s: 위치 std   0.048 cm (이론   0.048 cm)
잡음만 t=10s: 위치 std   1.534 cm (이론   1.519 cm)
잡음만 t=30s: 위치 std   7.638 cm (이론   7.894 cm)
bias 1 mg       → 1s:    0.005 m, 10s:    0.490 m, 30s:    4.413 m, 60s:   17.652 m
자세 오차 1° → 중력 누설 → 1s:    0.086 m, 10s:    8.557 m, 30s:   77.017 m, 60s:  308.069 m
```

출력에서 볼 것: noise만이면 30초에 8 cm 정도지만, bias 1 mg는 10초에 0.49 m, 1분에 17.7 m. 자세 1° 오차는 10초 만에 8.6 m, 1분이면 300 m. 소비자 MEMS IMU로 "실내 위치를 적분해서 추적"하는 것이 수 초 이상 불가능한 이유다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 340">
<line x1="80.0" y1="290.0" x2="620.0" y2="290.0" stroke="currentColor" stroke-width="1"/> <line x1="80.0" y1="290.0" x2="80.0" y2="40.0" stroke="currentColor" stroke-width="1"/> <line x1="80.0" y1="290.0" x2="80.0" y2="295.0" stroke="currentColor" stroke-width="1"/> <text x="80.0" y="308.0" font-size="12" text-anchor="middle">0.1</text> <line x1="80.0" y1="290.0" x2="80.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="274.4" y1="290.0" x2="274.4" y2="295.0" stroke="currentColor" stroke-width="1"/> <text x="274.4" y="308.0" font-size="12" text-anchor="middle">1</text> <line x1="274.4" y1="290.0" x2="274.4" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="468.7" y1="290.0" x2="468.7" y2="295.0" stroke="currentColor" stroke-width="1"/> <text x="468.7" y="308.0" font-size="12" text-anchor="middle">10</text>
<line x1="468.7" y1="290.0" x2="468.7" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="620.0" y1="290.0" x2="620.0" y2="295.0" stroke="currentColor" stroke-width="1"/> <text x="620.0" y="308.0" font-size="12" text-anchor="middle">60</text> <line x1="620.0" y1="290.0" x2="620.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="75.0" y1="290.0" x2="80.0" y2="290.0" stroke="currentColor" stroke-width="1"/> <text x="72.0" y="294.0" font-size="12" text-anchor="end">10 µm</text> <line x1="80.0" y1="290.0" x2="620.0" y2="290.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="75.0" y1="227.5" x2="80.0" y2="227.5" stroke="currentColor" stroke-width="1"/> <text x="72.0" y="231.5" font-size="12" text-anchor="end">1 mm</text> <line x1="80.0" y1="227.5" x2="620.0" y2="227.5" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/>
<line x1="75.0" y1="165.0" x2="80.0" y2="165.0" stroke="currentColor" stroke-width="1"/> <text x="72.0" y="169.0" font-size="12" text-anchor="end">10 cm</text> <line x1="80.0" y1="165.0" x2="620.0" y2="165.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="75.0" y1="102.5" x2="80.0" y2="102.5" stroke="currentColor" stroke-width="1"/> <text x="72.0" y="106.5" font-size="12" text-anchor="end">10 m</text> <line x1="80.0" y1="102.5" x2="620.0" y2="102.5" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="75.0" y1="40.0" x2="80.0" y2="40.0" stroke="currentColor" stroke-width="1"/> <text x="72.0" y="44.0" font-size="12" text-anchor="end">1 km</text> <line x1="80.0" y1="40.0" x2="620.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <text x="350.0" y="326.0" font-size="13" text-anchor="middle">시간 t [s] (log)</text>
<text x="74.0" y="30.0" font-size="12" text-anchor="start">위치 오차 (log)</text> <polyline points="80.0,229.6 93.8,225.2 107.7,220.7 121.5,216.3 135.4,211.8 149.2,207.4 163.1,202.9 176.9,198.4 190.8,194.0 204.6,189.5 218.5,185.1 232.3,180.6 246.2,176.2 260.0,171.7 273.8,167.3 287.7,162.8 301.5,158.4 315.4,153.9 329.2,149.5 343.1,145.0 356.9,140.6 370.8,136.1 384.6,131.7 398.5,127.2 412.3,122.8 426.2,118.3 440.0,113.9 453.8,109.4 467.7,105.0 481.5,100.5 495.4,96.0 509.2,91.6 523.1,87.1 536.9,82.7 550.8,78.2 564.6,73.8 578.5,69.3 592.3,64.9 606.2,60.4 620.0,56.0" fill="none" stroke="#d0564a" stroke-width="2.5"/> <polyline points="80.0,268.4 93.8,264.0 107.7,259.5 121.5,255.1 135.4,250.6 149.2,246.2 163.1,241.7 176.9,237.3 190.8,232.8 204.6,228.4 218.5,223.9 232.3,219.4 246.2,215.0 260.0,210.5 273.8,206.1 287.7,201.6 301.5,197.2 315.4,192.7 329.2,188.3 343.1,183.8 356.9,179.4 370.8,174.9 384.6,170.5 398.5,166.0 412.3,161.6 426.2,157.1 440.0,152.7 453.8,148.2 467.7,143.8 481.5,139.3 495.4,134.9 509.2,130.4 523.1,126.0 536.9,121.5 550.8,117.0 564.6,112.6 578.5,108.1 592.3,103.7 606.2,99.2 620.0,94.8" fill="none" stroke="#e08a3c" stroke-width="2.5"/> <polyline points="80.0,284.3 93.8,281.0 107.7,277.6 121.5,274.3 135.4,271.0 149.2,267.6 163.1,264.3 176.9,261.0 190.8,257.6 204.6,254.3 218.5,250.9 232.3,247.6 246.2,244.3 260.0,240.9 273.8,237.6 287.7,234.2 301.5,230.9 315.4,227.6 329.2,224.2 343.1,220.9 356.9,217.5 370.8,214.2 384.6,210.9 398.5,207.5 412.3,204.2 426.2,200.8 440.0,197.5 453.8,194.2 467.7,190.8 481.5,187.5 495.4,184.2 509.2,180.8 523.1,177.5 536.9,174.1 550.8,170.8 564.6,167.5 578.5,164.1 592.3,160.8 606.2,157.4 620.0,154.1" fill="none" stroke="#4a7bd0" stroke-width="2.5"/> <line x1="100.0" y1="60.0" x2="120.0" y2="60.0" stroke="#d0564a" stroke-width="3"/> <text x="126.0" y="64.0" font-size="12" text-anchor="start">자세 1° 오차 → 중력 누설 0.17 m/s² (∝ t²)</text> <line x1="100.0" y1="80.0" x2="120.0" y2="80.0" stroke="#e08a3c" stroke-width="3"/> <text x="126.0" y="84.0" font-size="12" text-anchor="start">accel bias 1 mg (∝ t²)</text> <line x1="100.0" y1="100.0" x2="120.0" y2="100.0" stroke="#4a7bd0" stroke-width="3"/> <text x="126.0" y="104.0" font-size="12" text-anchor="start">noise 120 µg/√Hz, 1σ (∝ t^1.5)</text>
<line x1="468.7" y1="290.0" x2="468.7" y2="40.0" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/> <text x="472.7" y="285.0" font-size="12" text-anchor="start">10 s: 0.5 m / 8.6 m</text>
</svg>
```

그림 8 — 이중 적분 위치 오차 (log-log, 실제 계산). 기울기 2(빨강·주황, bias와 중력 누설)가 기울기 1.5(파랑, noise)를 금방 압도한다. 세로축이 log라서 한 칸이 100배다.

### 10.3 그럼 어떻게 하나 — 그리고 ML이 raw 창을 쓰는 이유

- **외부 기준으로 리셋**: 발에 단 IMU는 발이 땅에 닿는 순간 속도가 0이라는 사실(ZUPT, zero-velocity update)로 매 걸음 속도 오차를 리셋한다. 스마트폰 위치는 GPS·Wi-Fi·카메라와 퓨전한다. 자세는 중력(가속도계)과 지자기로 roll·pitch·heading을 계속 붙잡는다(G3).
- **적분하지 않는다**: 제스처·HAR·낙상 같은 분류 과제는 "손이 어디까지 갔나"가 아니라 "움직임의 **모양**이 어떤 패턴인가"를 묻는다. 1~3초 창 안의 raw 가속도·각속도 파형에는 그 모양이 이미 들어 있다. 적분하면 drift만 더해진다. 그래서 B7의 모델들은 **raw(또는 가볍게 전처리한) 창**을 입력으로 받고, 모델이 필요한 특징을 학습하게 한다.
- 짧은 창 안에서 bias는 거의 상수라서 모델 입장에서는 "DC offset"일 뿐이다. 창 평균을 빼거나(B7 §2.2의 중력 제거), 학습 때 bias를 랜덤으로 더하는 augmentation으로 강건하게 만든다.

---

## 11. 과제별로 range와 ODR 고르기

### 11.1 출발점 표

아래 숫자는 **출발점**이다. 실제로는 대상 사용자의 데이터를 높은 ODR·넓은 range로 먼저 모아 본 뒤(H1), 스펙트럼과 최대값을 보고 줄인다.

| 과제 | 가속도 range | 자이로 | ODR 출발점 | 근거 |
|---|---|---|---|---|
| HAR (걷기·뛰기·앉기) | ±4 ~ ±8 g | 선택 (accel만으로도 흔함) | 25 ~ 50 Hz | 사람 몸 움직임의 에너지는 대부분 20 Hz 아래로 알려져 있다. UCI HAR 데이터셋은 50 Hz |
| 걸음 수 | ±4 ~ ±8 g | 불필요 | 25 ~ 50 Hz | 걸음 주파수는 약 1~3 Hz. 센서 내장 step counter도 많다 (G2) |
| 손목 제스처 (튕기기·회전) | ±8 ~ ±16 g | ±1000 ~ ±2000 dps | 100 Hz 이상 | 빠른 손목 동작은 수백 dps를 넘고 수십 ms 단위로 끝난다 |
| 낙상 감지 | ±16 g 권장 | 선택 | 50 ~ 100 Hz 이상 | 충격 피크가 수 g~10 g 넘게 나올 수 있다. 0 g 자유낙하 구간 + 충격 + 이후 정지 |
| 착용 감지 | ±2 ~ ±4 g | 불필요 | 낮게 (10 ~ 25 Hz) | 작은 움직임·미세 진동이 신호라서 noise가 중요 |
| 머리 움직임 (공간 음향 head tracking) | ±4 ~ ±8 g | ±500 ~ ±2000 dps | 수백 Hz | 지연이 짧아야 소리 위치가 자연스럽다 (요구 수준은 제품마다 다름 — 추정) |

> Hark 같은 웨어러블이라면(추정): 상시 켜진 가속도계는 저 ODR(예: 25 Hz)로 착용·움직임을 보고, 제스처나 머리 추적이 필요한 순간에만 자이로와 높은 ODR을 켜는 2단 구성이 자연스럽다.

### 11.2 range가 작으면: 포화(clipping)

낙상 순간을 가정해 보자. 0.4초 자유낙하(0 g) 후 20 ms 동안 피크 10 g 충격(가정)이 온다. 이 코드는 같은 신호를 ±4 g와 ±16 g, ODR 400/100/50 Hz로 샘플링해(int16 양자화 + 포화 포함) 피크와 충격량(Δv)을 비교한다.

```python
import numpy as np
def fall_signal(t):
    """수직축 가속도계 [g] 가정 모델: 정지 1 g → 자유낙하 0 g(0.4 s) → 충격(반-사인, 피크 10 g, 20 ms) → 정지"""
    a = np.ones_like(t)
    a[(t >= 0.5) & (t < 0.9)] = 0.0
    hit = (t >= 0.9) & (t < 0.92)
    a[hit] = 1 + 9 * np.sin(np.pi * (t[hit] - 0.9) / 0.02)
    return a

def sample(odr, fs_g, t0=0.0):
    t = np.arange(t0, 1.5, 1 / odr)
    lsb = 32768 / fs_g
    raw = np.clip(np.round(fall_signal(t) * lsb), -32768, 32767).astype(np.int16)
    return t, raw / lsb, raw

for odr in [400, 100, 50]:
    for fs_g in [4, 16]:
        t, a, raw = sample(odr, fs_g, t0=0.0013)
        sat = np.sum((raw == 32767) | (raw == -32768))
        dv = np.sum(a[(t > 0.89) & (t < 0.93)] - 1) / odr * 9.80665   # 충격 구간 Δv [m/s]
        print(f"ODR {odr:3} Hz, ±{fs_g:>2} g: 피크 {a.max():6.2f} g, 포화 샘플 {sat}, 충격 Δv {dv:5.2f} m/s")
tt = np.linspace(0.9, 0.92, 20001)
print("참값: 피크 10.00 g, 충격 Δv", round(np.trapezoid(fall_signal(tt) - 1, tt) * 9.80665, 2), "m/s")
```

```text
ODR 400 Hz, ± 4 g: 피크   4.00 g, 포화 샘플 6, 충격 Δv  0.43 m/s
ODR 400 Hz, ±16 g: 피크   9.84 g, 포화 샘플 0, 충격 Δv  1.03 m/s
ODR 100 Hz, ± 4 g: 피크   4.00 g, 포화 샘플 1, 충격 Δv  0.38 m/s
ODR 100 Hz, ±16 g: 피크   9.81 g, 포화 샘플 0, 충격 Δv  0.95 m/s
ODR  50 Hz, ± 4 g: 피크   2.83 g, 포화 샘플 0, 충격 Δv  0.36 m/s
ODR  50 Hz, ±16 g: 피크   2.83 g, 포화 샘플 0, 충격 Δv  0.36 m/s
참값: 피크 10.00 g, 충격 Δv 1.12 m/s
```

출력에서 볼 것:

- ±4 g에서는 피크가 4.00 g에 붙고(포화 샘플 6개), 충격 동안 속도 변화 Δv가 0.43 m/s로 참값 1.12 m/s의 40% 정도만 남는다. 충격 크기를 특징으로 쓰는 낙상 모델은 정보를 잃는다.
- ±16 g는 피크 9.84 g로 거의 맞다.
- ODR 50 Hz에서는 range와 상관없이 피크 2.83 g — 20 ms 충격 사이에 샘플이 1개뿐이라 피크를 놓친다. (이 예제는 내부 LPF 없이 순간값을 찍었다. 실제 센서는 LPF가 충격을 뭉개서 피크가 더 낮게, 대신 더 넓게 나온다.)
- 결론: 짧은 충격은 **range와 ODR 둘 다** 필요하다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 320">
<line x1="70.0" y1="270.0" x2="620.0" y2="270.0" stroke="currentColor" stroke-width="1"/> <line x1="70.0" y1="270.0" x2="70.0" y2="40.0" stroke="currentColor" stroke-width="1"/> <line x1="161.7" y1="270.0" x2="161.7" y2="275.0" stroke="currentColor" stroke-width="1"/> <text x="161.7" y="288.0" font-size="12" text-anchor="middle">0.88</text> <line x1="161.7" y1="270.0" x2="161.7" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="253.3" y1="270.0" x2="253.3" y2="275.0" stroke="currentColor" stroke-width="1"/> <text x="253.3" y="288.0" font-size="12" text-anchor="middle">0.90</text> <line x1="253.3" y1="270.0" x2="253.3" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="345.0" y1="270.0" x2="345.0" y2="275.0" stroke="currentColor" stroke-width="1"/> <text x="345.0" y="288.0" font-size="12" text-anchor="middle">0.92</text>
<line x1="345.0" y1="270.0" x2="345.0" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="436.7" y1="270.0" x2="436.7" y2="275.0" stroke="currentColor" stroke-width="1"/> <text x="436.7" y="288.0" font-size="12" text-anchor="middle">0.94</text> <line x1="436.7" y1="270.0" x2="436.7" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="528.3" y1="270.0" x2="528.3" y2="275.0" stroke="currentColor" stroke-width="1"/> <text x="528.3" y="288.0" font-size="12" text-anchor="middle">0.96</text> <line x1="528.3" y1="270.0" x2="528.3" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="65.0" y1="250.8" x2="70.0" y2="250.8" stroke="currentColor" stroke-width="1"/> <text x="62.0" y="254.8" font-size="12" text-anchor="end">0</text> <line x1="70.0" y1="250.8" x2="620.0" y2="250.8" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/>
<line x1="65.0" y1="212.5" x2="70.0" y2="212.5" stroke="currentColor" stroke-width="1"/> <text x="62.0" y="216.5" font-size="12" text-anchor="end">2</text> <line x1="70.0" y1="212.5" x2="620.0" y2="212.5" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="65.0" y1="174.2" x2="70.0" y2="174.2" stroke="currentColor" stroke-width="1"/> <text x="62.0" y="178.2" font-size="12" text-anchor="end">4</text> <line x1="70.0" y1="174.2" x2="620.0" y2="174.2" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="65.0" y1="135.8" x2="70.0" y2="135.8" stroke="currentColor" stroke-width="1"/> <text x="62.0" y="139.8" font-size="12" text-anchor="end">6</text> <line x1="70.0" y1="135.8" x2="620.0" y2="135.8" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="65.0" y1="97.5" x2="70.0" y2="97.5" stroke="currentColor" stroke-width="1"/>
<text x="62.0" y="101.5" font-size="12" text-anchor="end">8</text> <line x1="70.0" y1="97.5" x2="620.0" y2="97.5" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <line x1="65.0" y1="59.2" x2="70.0" y2="59.2" stroke="currentColor" stroke-width="1"/> <text x="62.0" y="63.2" font-size="12" text-anchor="end">10</text> <line x1="70.0" y1="59.2" x2="620.0" y2="59.2" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/> <text x="345.0" y="306.0" font-size="13" text-anchor="middle">시간 [s]</text> <text x="64.0" y="30.0" font-size="12" text-anchor="start">수직 가속도 [g]</text> <polyline points="70.0,250.8 71.4,250.8 72.8,250.8 74.1,250.8 75.5,250.8 76.9,250.8 78.3,250.8 79.6,250.8 81.0,250.8 82.4,250.8 83.8,250.8 85.2,250.8 86.5,250.8 87.9,250.8 89.3,250.8 90.7,250.8 92.1,250.8 93.4,250.8 94.8,250.8 96.2,250.8 97.6,250.8 98.9,250.8 100.3,250.8 101.7,250.8 103.1,250.8 104.5,250.8 105.8,250.8 107.2,250.8 108.6,250.8 110.0,250.8 111.4,250.8 112.7,250.8 114.1,250.8 115.5,250.8 116.9,250.8 118.2,250.8 119.6,250.8 121.0,250.8 122.4,250.8 123.8,250.8 125.1,250.8 126.5,250.8 127.9,250.8 129.3,250.8 130.7,250.8 132.0,250.8 133.4,250.8 134.8,250.8 136.2,250.8 137.5,250.8 138.9,250.8 140.3,250.8 141.7,250.8 143.1,250.8 144.4,250.8 145.8,250.8 147.2,250.8 148.6,250.8 149.9,250.8 151.3,250.8 152.7,250.8 154.1,250.8 155.5,250.8 156.8,250.8 158.2,250.8 159.6,250.8 161.0,250.8 162.4,250.8 163.7,250.8 165.1,250.8 166.5,250.8 167.9,250.8 169.2,250.8 170.6,250.8 172.0,250.8 173.4,250.8 174.8,250.8 176.1,250.8 177.5,250.8 178.9,250.8 180.3,250.8 181.7,250.8 183.0,250.8 184.4,250.8 185.8,250.8 187.2,250.8 188.5,250.8 189.9,250.8 191.3,250.8 192.7,250.8 194.1,250.8 195.4,250.8 196.8,250.8 198.2,250.8 199.6,250.8 201.0,250.8 202.3,250.8 203.7,250.8 205.1,250.8 206.5,250.8 207.8,250.8 209.2,250.8 210.6,250.8 212.0,250.8 213.4,250.8 214.7,250.8 216.1,250.8 217.5,250.8 218.9,250.8 220.3,250.8 221.6,250.8 223.0,250.8 224.4,250.8 225.8,250.8 227.1,250.8 228.5,250.8 229.9,250.8 231.3,250.8 232.7,250.8 234.0,250.8 235.4,250.8 236.8,250.8 238.2,250.8 239.5,250.8 240.9,250.8 242.3,250.8 243.7,250.8 245.1,250.8 246.4,250.8 247.8,250.8 249.2,250.8 250.6,250.8 252.0,250.8 253.3,231.7 254.7,223.5 256.1,215.4 257.5,207.3 258.8,199.3 260.2,191.3 261.6,183.4 263.0,175.7 264.4,168.0 265.7,160.5 267.1,153.2 268.5,146.0 269.9,139.0 271.3,132.3 272.6,125.7 274.0,119.4 275.4,113.3 276.8,107.5 278.1,102.0 279.5,96.8 280.9,91.9 282.3,87.3 283.7,83.0 285.0,79.0 286.4,75.4 287.8,72.1 289.2,69.2 290.6,66.6 291.9,64.4 293.3,62.6 294.7,61.2 296.1,60.1 297.4,59.5 298.8,59.2 300.2,59.3 301.6,59.8 303.0,60.6 304.3,61.9 305.7,63.5 307.1,65.5 308.5,67.9 309.8,70.6 311.2,73.7 312.6,77.1 314.0,80.9 315.4,85.1 316.7,89.5 318.1,94.3 319.5,99.4 320.9,104.8 322.3,110.4 323.6,116.3 325.0,122.5 326.4,129.0 327.8,135.6 329.1,142.5 330.5,149.6 331.9,156.8 333.3,164.2 334.7,171.8 336.0,179.5 337.4,187.3 338.8,195.3 340.2,203.3 341.6,211.3 342.9,219.5 344.3,227.6 345.7,231.7 347.1,231.7 348.4,231.7 349.8,231.7 351.2,231.7 352.6,231.7 354.0,231.7 355.3,231.7 356.7,231.7 358.1,231.7 359.5,231.7 360.9,231.7 362.2,231.7 363.6,231.7 365.0,231.7 366.4,231.7 367.7,231.7 369.1,231.7 370.5,231.7 371.9,231.7 373.3,231.7 374.6,231.7 376.0,231.7 377.4,231.7 378.8,231.7 380.2,231.7 381.5,231.7 382.9,231.7 384.3,231.7 385.7,231.7 387.0,231.7 388.4,231.7 389.8,231.7 391.2,231.7 392.6,231.7 393.9,231.7 395.3,231.7 396.7,231.7 398.1,231.7 399.4,231.7 400.8,231.7 402.2,231.7 403.6,231.7 405.0,231.7 406.3,231.7 407.7,231.7 409.1,231.7 410.5,231.7 411.9,231.7 413.2,231.7 414.6,231.7 416.0,231.7 417.4,231.7 418.7,231.7 420.1,231.7 421.5,231.7 422.9,231.7 424.3,231.7 425.6,231.7 427.0,231.7 428.4,231.7 429.8,231.7 431.2,231.7 432.5,231.7 433.9,231.7 435.3,231.7 436.7,231.7 438.0,231.7 439.4,231.7 440.8,231.7 442.2,231.7 443.6,231.7 444.9,231.7 446.3,231.7 447.7,231.7 449.1,231.7 450.5,231.7 451.8,231.7 453.2,231.7 454.6,231.7 456.0,231.7 457.3,231.7 458.7,231.7 460.1,231.7 461.5,231.7 462.9,231.7 464.2,231.7 465.6,231.7 467.0,231.7 468.4,231.7 469.7,231.7 471.1,231.7 472.5,231.7 473.9,231.7 475.3,231.7 476.6,231.7 478.0,231.7 479.4,231.7 480.8,231.7 482.2,231.7 483.5,231.7 484.9,231.7 486.3,231.7 487.7,231.7 489.0,231.7 490.4,231.7 491.8,231.7 493.2,231.7 494.6,231.7 495.9,231.7 497.3,231.7 498.7,231.7 500.1,231.7 501.5,231.7 502.8,231.7 504.2,231.7 505.6,231.7 507.0,231.7 508.3,231.7 509.7,231.7 511.1,231.7 512.5,231.7 513.9,231.7 515.2,231.7 516.6,231.7 518.0,231.7 519.4,231.7 520.8,231.7 522.1,231.7 523.5,231.7 524.9,231.7 526.3,231.7 527.6,231.7 529.0,231.7 530.4,231.7 531.8,231.7 533.2,231.7 534.5,231.7 535.9,231.7 537.3,231.7 538.7,231.7 540.1,231.7 541.4,231.7 542.8,231.7 544.2,231.7 545.6,231.7 546.9,231.7 548.3,231.7 549.7,231.7 551.1,231.7 552.5,231.7 553.8,231.7 555.2,231.7 556.6,231.7 558.0,231.7 559.3,231.7 560.7,231.7 562.1,231.7 563.5,231.7 564.9,231.7 566.2,231.7 567.6,231.7 569.0,231.7 570.4,231.7 571.8,231.7 573.1,231.7 574.5,231.7 575.9,231.7 577.3,231.7 578.6,231.7 580.0,231.7 581.4,231.7 582.8,231.7 584.2,231.7 585.5,231.7 586.9,231.7 588.3,231.7 589.7,231.7 591.1,231.7 592.4,231.7 593.8,231.7 595.2,231.7 596.6,231.7 597.9,231.7 599.3,231.7 600.7,231.7 602.1,231.7 603.5,231.7 604.8,231.7 606.2,231.7 607.6,231.7 609.0,231.7 610.4,231.7 611.7,231.7 613.1,231.7 614.5,231.7 615.9,231.7 617.2,231.7 618.6,231.7 620.0,231.7" fill="none" stroke="#888" stroke-width="1.5"/> <polyline points="76.0,250.8 87.4,250.8 98.9,250.8 110.3,250.8 121.8,250.8 133.3,250.8 144.7,250.8 156.2,250.8 167.6,250.8 179.1,250.8 190.5,250.8 202.0,250.8 213.5,250.8 224.9,250.8 236.4,250.8 247.8,250.8 259.3,196.7 270.8,134.7 282.2,87.5 293.7,62.2 305.1,62.8 316.6,89.0 328.0,137.0 339.5,199.3 351.0,231.7 362.4,231.7 373.9,231.7 385.3,231.7 396.8,231.7 408.3,231.7 419.7,231.7 431.2,231.7 442.6,231.7 454.1,231.7 465.5,231.7 477.0,231.7 488.5,231.7 499.9,231.7 511.4,231.7 522.8,231.7 534.3,231.7 545.8,231.7 557.2,231.7 568.7,231.7 580.1,231.7 591.6,231.7 603.0,231.7 614.5,231.7" fill="none" stroke="#3f9a6b" stroke-width="2"/> <circle cx="76.0" cy="250.8" r="3" fill="#3f9a6b"/>
<circle cx="87.4" cy="250.8" r="3" fill="#3f9a6b"/> <circle cx="98.9" cy="250.8" r="3" fill="#3f9a6b"/> <circle cx="110.3" cy="250.8" r="3" fill="#3f9a6b"/> <circle cx="121.8" cy="250.8" r="3" fill="#3f9a6b"/> <circle cx="133.3" cy="250.8" r="3" fill="#3f9a6b"/> <circle cx="144.7" cy="250.8" r="3" fill="#3f9a6b"/> <circle cx="156.2" cy="250.8" r="3" fill="#3f9a6b"/> <circle cx="167.6" cy="250.8" r="3" fill="#3f9a6b"/> <circle cx="179.1" cy="250.8" r="3" fill="#3f9a6b"/> <circle cx="190.5" cy="250.8" r="3" fill="#3f9a6b"/>
<circle cx="202.0" cy="250.8" r="3" fill="#3f9a6b"/> <circle cx="213.5" cy="250.8" r="3" fill="#3f9a6b"/> <circle cx="224.9" cy="250.8" r="3" fill="#3f9a6b"/> <circle cx="236.4" cy="250.8" r="3" fill="#3f9a6b"/> <circle cx="247.8" cy="250.8" r="3" fill="#3f9a6b"/> <circle cx="259.3" cy="196.7" r="3" fill="#3f9a6b"/> <circle cx="270.8" cy="134.7" r="3" fill="#3f9a6b"/> <circle cx="282.2" cy="87.5" r="3" fill="#3f9a6b"/> <circle cx="293.7" cy="62.2" r="3" fill="#3f9a6b"/> <circle cx="305.1" cy="62.8" r="3" fill="#3f9a6b"/>
<circle cx="316.6" cy="89.0" r="3" fill="#3f9a6b"/> <circle cx="328.0" cy="137.0" r="3" fill="#3f9a6b"/> <circle cx="339.5" cy="199.3" r="3" fill="#3f9a6b"/> <circle cx="351.0" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="362.4" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="373.9" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="385.3" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="396.8" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="408.3" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="419.7" cy="231.7" r="3" fill="#3f9a6b"/>
<circle cx="431.2" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="442.6" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="454.1" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="465.5" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="477.0" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="488.5" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="499.9" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="511.4" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="522.8" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="534.3" cy="231.7" r="3" fill="#3f9a6b"/>
<circle cx="545.8" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="557.2" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="568.7" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="580.1" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="591.6" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="603.0" cy="231.7" r="3" fill="#3f9a6b"/> <circle cx="614.5" cy="231.7" r="3" fill="#3f9a6b"/> <polyline points="76.0,250.8 87.4,250.8 98.9,250.8 110.3,250.8 121.8,250.8 133.3,250.8 144.7,250.8 156.2,250.8 167.6,250.8 179.1,250.8 190.5,250.8 202.0,250.8 213.5,250.8 224.9,250.8 236.4,250.8 247.8,250.8 259.3,196.7 270.8,174.2 282.2,174.2 293.7,174.2 305.1,174.2 316.6,174.2 328.0,174.2 339.5,199.3 351.0,231.7 362.4,231.7 373.9,231.7 385.3,231.7 396.8,231.7 408.3,231.7 419.7,231.7 431.2,231.7 442.6,231.7 454.1,231.7 465.5,231.7 477.0,231.7 488.5,231.7 499.9,231.7 511.4,231.7 522.8,231.7 534.3,231.7 545.8,231.7 557.2,231.7 568.7,231.7 580.1,231.7 591.6,231.7 603.0,231.7 614.5,231.7" fill="none" stroke="#d0564a" stroke-width="2"/> <circle cx="76.0" cy="250.8" r="3" fill="#d0564a"/> <circle cx="87.4" cy="250.8" r="3" fill="#d0564a"/>
<circle cx="98.9" cy="250.8" r="3" fill="#d0564a"/> <circle cx="110.3" cy="250.8" r="3" fill="#d0564a"/> <circle cx="121.8" cy="250.8" r="3" fill="#d0564a"/> <circle cx="133.3" cy="250.8" r="3" fill="#d0564a"/> <circle cx="144.7" cy="250.8" r="3" fill="#d0564a"/> <circle cx="156.2" cy="250.8" r="3" fill="#d0564a"/> <circle cx="167.6" cy="250.8" r="3" fill="#d0564a"/> <circle cx="179.1" cy="250.8" r="3" fill="#d0564a"/> <circle cx="190.5" cy="250.8" r="3" fill="#d0564a"/> <circle cx="202.0" cy="250.8" r="3" fill="#d0564a"/>
<circle cx="213.5" cy="250.8" r="3" fill="#d0564a"/> <circle cx="224.9" cy="250.8" r="3" fill="#d0564a"/> <circle cx="236.4" cy="250.8" r="3" fill="#d0564a"/> <circle cx="247.8" cy="250.8" r="3" fill="#d0564a"/> <circle cx="259.3" cy="196.7" r="3" fill="#d0564a"/> <circle cx="270.8" cy="174.2" r="3" fill="#d0564a"/> <circle cx="282.2" cy="174.2" r="3" fill="#d0564a"/> <circle cx="293.7" cy="174.2" r="3" fill="#d0564a"/> <circle cx="305.1" cy="174.2" r="3" fill="#d0564a"/> <circle cx="316.6" cy="174.2" r="3" fill="#d0564a"/>
<circle cx="328.0" cy="174.2" r="3" fill="#d0564a"/> <circle cx="339.5" cy="199.3" r="3" fill="#d0564a"/> <circle cx="351.0" cy="231.7" r="3" fill="#d0564a"/> <circle cx="362.4" cy="231.7" r="3" fill="#d0564a"/> <circle cx="373.9" cy="231.7" r="3" fill="#d0564a"/> <circle cx="385.3" cy="231.7" r="3" fill="#d0564a"/> <circle cx="396.8" cy="231.7" r="3" fill="#d0564a"/> <circle cx="408.3" cy="231.7" r="3" fill="#d0564a"/> <circle cx="419.7" cy="231.7" r="3" fill="#d0564a"/> <circle cx="431.2" cy="231.7" r="3" fill="#d0564a"/>
<circle cx="442.6" cy="231.7" r="3" fill="#d0564a"/> <circle cx="454.1" cy="231.7" r="3" fill="#d0564a"/> <circle cx="465.5" cy="231.7" r="3" fill="#d0564a"/> <circle cx="477.0" cy="231.7" r="3" fill="#d0564a"/> <circle cx="488.5" cy="231.7" r="3" fill="#d0564a"/> <circle cx="499.9" cy="231.7" r="3" fill="#d0564a"/> <circle cx="511.4" cy="231.7" r="3" fill="#d0564a"/> <circle cx="522.8" cy="231.7" r="3" fill="#d0564a"/> <circle cx="534.3" cy="231.7" r="3" fill="#d0564a"/> <circle cx="545.8" cy="231.7" r="3" fill="#d0564a"/>
<circle cx="557.2" cy="231.7" r="3" fill="#d0564a"/> <circle cx="568.7" cy="231.7" r="3" fill="#d0564a"/> <circle cx="580.1" cy="231.7" r="3" fill="#d0564a"/> <circle cx="591.6" cy="231.7" r="3" fill="#d0564a"/> <circle cx="603.0" cy="231.7" r="3" fill="#d0564a"/> <circle cx="614.5" cy="231.7" r="3" fill="#d0564a"/> <line x1="70.0" y1="174.2" x2="620.0" y2="174.2" stroke="#d0564a" stroke-width="1" stroke-dasharray="5 4"/> <text x="597.1" y="168.2" font-size="12" text-anchor="end">±4 g 천장</text> <line x1="400.0" y1="60.0" x2="420.0" y2="60.0" stroke="#888" stroke-width="3"/> <text x="426.0" y="64.0" font-size="12" text-anchor="start">참 신호 (피크 10 g)</text>
<line x1="400.0" y1="80.0" x2="420.0" y2="80.0" stroke="#3f9a6b" stroke-width="3"/> <text x="426.0" y="84.0" font-size="12" text-anchor="start">±16 g, 400 Hz 샘플</text> <line x1="400.0" y1="100.0" x2="420.0" y2="100.0" stroke="#d0564a" stroke-width="3"/> <text x="426.0" y="104.0" font-size="12" text-anchor="start">±4 g, 400 Hz (평평하게 잘림)</text>
</svg>
```

그림 9 — 낙상 충격 구간 확대 (실제 계산). 회색은 참 신호, 초록은 ±16 g, 빨강은 ±4 g로 400 Hz 샘플링한 값. 빨강은 4 g 천장에서 평평하게 잘린다. 로그에서 이런 "평평한 고원"이 보이면 포화를 의심한다(H7).

### 11.3 ODR이 낮으면: aliasing과 정보 손실

§6.2에서 봤듯이 Nyquist(ODR/2)보다 빠른 성분은 사라지거나 가짜 저주파로 접힌다. 과제 신호의 최고 주파수를 먼저 추정하고, 그 2배보다 넉넉히(보통 2.5~5배) ODR을 잡는다. 그리고 ODR을 낮추면 센서 내부 LPF가 같이 낮아지는지 반드시 확인한다.

### 11.4 선택 절차 요약

```
1. 과제 정의: 무엇을, 얼마나 빨리 감지해야 하나 (지연 예산 D6)
2. 탐색 수집: 최대 range·높은 ODR로 실제 사용자 데이터를 모은다 (H1)
3. 분석: 축별 최댓값 분포(포화 여유) + 스펙트럼(에너지가 몇 Hz까지?) 
4. 결정: range = 최댓값의 99.9 percentile × 여유 / ODR = 최고 주파수 × 2.5~5
5. 검증: 낮춘 설정으로 재샘플한 데이터로 모델 정확도가 유지되는지 (C8처럼 회귀 비교)
6. 전력 확인: 그 ODR·모드의 전류를 데이터시트와 실측으로 (D7, E9)
```

---

## 12. 대표 부품 — 무엇을 데이터시트에서 찾아볼까

숫자는 일부러 적지 않는다. 같은 칩이라도 리비전·모드·조건에 따라 값이 다르고, 기억에 의존한 숫자는 틀리기 쉽다. 대신 **각 부품이 어떤 용도로 알려져 있는지**와 **데이터시트에서 찾아볼 항목**을 정리한다. 실제 값은 제조사 웹사이트의 최신 데이터시트에서 확인한다.

| 부품 | 종류 | 알려진 특징 (확인된 범위만) | 데이터시트에서 찾아볼 것 |
|---|---|---|---|
| Bosch BMI270 | 6축 IMU | 웨어러블용 초저전력 IMU로 소개된다. 걸음 수·손목 제스처·any/no-motion 같은 내장 기능. 초기화 때 MCU가 설정 파일(config blob)을 칩에 올려야 하는 것으로 유명하다. 지자기 센서를 붙이는 보조(aux) 인터페이스가 있다 | accel·gyro range 목록, 모드별 전류 표, noise density, 내장 기능 목록과 필요한 ODR, FIFO 크기, config 로딩 절차 |
| ST LSM6DSO | 6축 IMU | always-on 용도로 소개된다. FIFO, 걸음 수·tilt 등 내장 기능, 프로그래머블 FSM(finite state machine) | 자이로 감도 표(§4.2 함정), 모드별 noise·전류, FIFO 크기·batching 옵션, FSM 설명 앱노트 |
| ST LSM6DSOX | 6축 IMU | LSM6DSO 계열 + **MLC(Machine Learning Core)**: 센서 안에서 특징 계산 + decision tree를 돌린다(B7, 앱노트 AN5259) | MLC가 쓸 수 있는 특징 종류, tree 크기 제한, MLC ODR, 전류 |
| TDK InvenSense ICM-42688-P | 6축 IMU | 저잡음·온도 안정성을 내세우는 고성능 소비자급 IMU로 알려져 있고, 드론·로보틱스 보드에서 자주 보인다 | noise density, offset 온도 계수, AAF(anti-alias filter) 설정, 고ODR 옵션, 전류 |
| Bosch BMM150 | 3축 지자기 | 소형 웨어러블·모바일용 지자기 센서. BMI 계열 IMU의 aux 포트에 붙여 9축을 구성하는 예가 많다 | 측정 범위(µT), 분해능, noise, ODR별 전류, 권장 calibration 절차 |
| ST LIS2MDL | 3축 지자기 | 저전력 3축 지자기 센서 | 측정 범위(gauss), offset 상쇄(set/reset) 기능, 온도 보상, ODR·전류 |

**데이터시트 읽는 순서 (어떤 IMU든)**

1. 첫 페이지 features 목록과 블록도 — 내부 경로(ADC → 필터 → FIFO), 내장 기능 파악
2. "Mechanical characteristics" 표 — range, sensitivity, tolerance, offset, noise, 온도 계수, cross-axis
3. "Electrical characteristics" — 전원, 모드별 전류, turn-on time
4. 필터·ODR 표 — ODR별 bandwidth, 필터 지연
5. 축 방향 그림 — 부호 규약(§1.1)
6. 레지스터 맵·FIFO·인터럽트 — G2
7. 앱노트 — 실장 가이드(PCB 응력), calibration, 내장 ML·FSM

> "typ"와 "min/max"를 구분한다. noise처럼 typ만 있는 값은 양산 편차가 클 수 있고, offset처럼 ±max가 있는 값은 **최악의 칩**을 기준으로 calibration 필요 여부를 판단한다.

---

## 13. 그 밖의 오차원 — 실장·환경이 만드는 것

| 효과 | 무슨 일이 일어나나 | 증상 | 대응 |
|---|---|---|---|
| 온도 | offset·감도가 온도에 따라 변함 (§7.2) | 착용 직후 몇 분 동안 bias가 흐름. 실외에서 heading drift 증가 | 온도 보상 테이블, 정지 구간 bias 재추정, 로그에 칩 온도 기록 |
| 진동 정류 (vibration rectification) | 비대칭·비선형 때문에 고주파 진동이 **DC offset**으로 바뀜 | 모터가 돌거나 차 안에서만 평균값이 이동 | 기구적 진동 격리, 큰 range로 포화 방지, 진동 환경에서 calibration 확인 |
| 기계적 충격 | 떨어뜨림 등 큰 충격. 정격(데이터시트 "absolute maximum" 충격 g)을 넘으면 손상 가능 | 충격 후 offset 영구 이동, 한 축이 고착 | 낙하 시험 후 self-test·offset 점검, fleet 데이터에서 이상 칩 탐지 (H7) |
| 실장·PCB 응력 | 리플로우 납땜, 보드 휨, 케이스 나사 조임이 MEMS 구조에 응력을 줌 | 공장·로트별로 offset이 다름, 케이스 조립 전후 값이 다름 | 제조사 실장 가이드(패드 설계, 칩 근처 나사·보강재 피하기), **조립 완료 후** calibration |
| 자기 방해 | 스피커·모터·자석 커넥터·전류 루프 (§8) | heading 튐, 지자기 포화 | 배치 설계, hard/soft iron calibration, 자기장 크기 이상 시 지자기 무시 |
| 전원 잡음 | 공급 전압 리플이 아날로그 프런트엔드에 섞임 | 특정 주파수 톤(예: 무선 송신 버스트 주기) | 전원 필터, 레이아웃, 센서 샘플 타이밍 분리 — Don의 RF 통합 경험이 그대로 통하는 영역 |
| 노화 | 장기간 offset 이동 | 수개월 뒤 calibration이 안 맞음 | 사용 중 자동 재보정(정지 구간 이용) |

**웨어러블 특유의 상황** (Hark 같은 기기라면 — 추정):

- 이어폰·귀걸이형: 스피커 자석과 구동 전류가 지자기 센서 바로 옆. 음악 재생 중에만 지자기가 흔들린다면 이것이다.
- 손목형: 피부 온도로 칩이 워밍업되며 처음 몇 분간 bias가 이동한다. 착용 직후 데이터를 학습에 넣을 때 주의.
- 진동 모터(햅틱): 모터가 도는 동안 가속도에 수십~수백 Hz 톤이 섞이고, ODR이 낮으면 aliasing으로 저주파에 나타난다(§6). 햅틱 구동 구간을 로그에 표시해 두면 디버깅이 쉽다.

---

## 14. 임베디드 관점에서 다시 보기

### 14.1 count → SI 단위 변환 (C)

A6 §4.6의 Python 변환을 펌웨어 쪽에서 구현하면 이렇다. 핵심은 (1) 리틀엔디언 바이트 조립, (2) 감도는 데이터시트 표 값을 상수로, (3) 포화 값 검출, (4) 단위는 SI(m/s², rad/s)로 통일 — 대부분의 퓨전 라이브러리와 ML 학습 코드가 SI를 가정한다.

이 코드는 12바이트 원시 데이터(자이로 3축 + 가속도 3축, 리틀엔디언 int16)를 SI 단위로 바꾸고 포화 축을 비트마스크로 돌려준다.

```c
#include <stdint.h>
#include <stdio.h>

/* 데이터시트 표에서 가져온 감도 (예: ±4 g → 0.122 mg/LSB, ±2000 dps → 70 mdps/LSB) */
typedef struct { float acc_mg_per_lsb; float gyr_mdps_per_lsb; } imu_scale_t;

static int16_t le16(const uint8_t *p) {             /* 리틀엔디언 2바이트 → int16 */
    return (int16_t)(uint16_t)(p[0] | (p[1] << 8));
}

/* 12바이트 [gx gy gz ax ay az] → SI 단위. 포화 축이 있으면 비트마스크 반환 */
static unsigned imu_convert(const uint8_t raw[12], const imu_scale_t *s,
                            float gyr_rad_s[3], float acc_m_s2[3]) {
    const float G = 9.80665f, DEG2RAD = 0.017453293f;
    unsigned sat = 0;
    for (int i = 0; i < 6; i++) {
        int16_t v = le16(&raw[2 * i]);
        if (v == INT16_MAX || v == INT16_MIN) sat |= 1u << i;
        if (i < 3) gyr_rad_s[i] = v * s->gyr_mdps_per_lsb * 1e-3f * DEG2RAD;
        else       acc_m_s2[i - 3] = v * s->acc_mg_per_lsb * 1e-3f * G;
    }
    return sat;
}

int main(void) {
    const imu_scale_t s = { 0.122f, 70.0f };
    /* gx=+1000, gy=-1000, gz=0, ax=0, ay=0x2000(8192), az=0x7FFF(포화) */
    const uint8_t raw[12] = { 0xE8,0x03, 0x18,0xFC, 0x00,0x00,
                              0x00,0x00, 0x00,0x20, 0xFF,0x7F };
    float w[3], a[3];
    unsigned sat = imu_convert(raw, &s, w, a);
    printf("gyro [rad/s] = %.4f %.4f %.4f\n", w[0], w[1], w[2]);
    printf("acc  [m/s^2] = %.4f %.4f %.4f\n", a[0], a[1], a[2]);
    printf("saturation mask = 0x%02X\n", sat);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 conv.c -o conv -lm && ./conv
```

```text
gyro [rad/s] = 1.2217 -1.2217 0.0000
acc  [m/s^2] = 0.0000 9.8010 39.2028
saturation mask = 0x20
```

출력에서 볼 것: gx = +1000 count × 70 mdps = 70 dps = 1.2217 rad/s. ay = 8192 × 0.122 mg = 999.4 mg → 9.801 m/s²(1 g보다 0.06% 작은 것은 감도 0.122가 반올림 값이라서). az = 32767은 포화로 표시된다(마스크 비트 5 = 0x20). 경고 0개로 컴파일된다.

> 함정: 레지스터 순서(gyro 먼저인지 accel 먼저인지), 바이트 순서(LSB 먼저인지)는 칩마다 다르다. "에러 없이 그럴듯한 숫자"가 나오기 때문에 더 위험하다(A6 §4.4). 첫 bring-up 때 **정지 상태 1 g 축 확인 + 손으로 돌려서 부호 확인**을 반드시 한다.

고정소수점 MCU라면 float 변환을 하지 않고 int16 그대로 모델에 넣는 방법도 있다. 이때 int16 count와 INT8 모델 입력의 scale을 맞추는 문제는 C1(양자화)의 scale/zero-point와 같은 계산이다.

### 14.2 전력 — ODR × 모드 × 어떤 센서를 켜나

- 전류는 대략 **ODR과 모드에 비례**해서 늘어난다. 저전력 모드는 내부 회로를 듀티 사이클로 돌려 전류를 줄이는 대신 noise가 커지는 경우가 많다(§5.3 팁).
- 자이로가 가장 비싸다(§3.3). "가속도계 상시 + 자이로는 이벤트 때만"이 기본.
- 센서 전류보다 **MCU를 깨우는 횟수**가 더 클 수 있다. 샘플마다 인터럽트로 MCU를 깨우지 말고 FIFO에 모았다가 watermark로 한 번에 읽는다(G2). 아래 코드의 두 번째 표가 그 계산이다.
- 센서 내장 기능(any-motion, step counter, wrist-tilt, ST MLC)을 쓰면 MCU가 거의 잠든 채로 판단할 수 있다. 정확도가 충분한 1단 판단은 센서에, 정밀한 2단 판단은 MCU/DSP에 두는 cascade가 웨어러블의 흔한 구조다(B7, A2 §9).

### 14.3 로깅 데이터율 (H1과 연결)

이 코드는 설정별 원시 데이터율과 하루 데이터량, 그리고 FIFO 1 KiB 단위로 읽을 때 MCU wake 빈도를 계산한다.

```python
# 로깅 데이터율: 축 수 × 2 바이트(int16) × ODR  (+ 타임스탬프·헤더는 별도)
configs = [
    ("HAR: accel만 50 Hz        ", 3, 50),
    ("제스처: 6축 100 Hz         ", 6, 100),
    ("제스처 고속: 6축 400 Hz     ", 6, 400),
    ("9축 100 Hz (mag 포함 가정) ", 9, 100),
    ("진동 분석: accel 1600 Hz   ", 3, 1600),
]
for name, axes, odr in configs:
    bps = axes * 2 * odr
    day = bps * 86400 / 2**20
    print(f"{name}: {bps:6d} B/s = {bps*8/1000:6.1f} kbit/s, 하루 {day:8.1f} MiB")
# FIFO watermark 주기: 예) FIFO에서 1 KiB 마다 MCU를 깨운다면
for odr in [50, 100, 400]:
    frame = 12                                   # 6축 × 2 B
    print(f"6축 {odr:>3} Hz: 1024 B 채우는 데 {1024/(frame*odr)*1000:6.1f} ms → MCU wake {frame*odr/1024:5.2f} 회/s")
```

```text
HAR: accel만 50 Hz        :    300 B/s =    2.4 kbit/s, 하루     24.7 MiB
제스처: 6축 100 Hz         :   1200 B/s =    9.6 kbit/s, 하루     98.9 MiB
제스처 고속: 6축 400 Hz     :   4800 B/s =   38.4 kbit/s, 하루    395.5 MiB
9축 100 Hz (mag 포함 가정) :   1800 B/s =   14.4 kbit/s, 하루    148.3 MiB
진동 분석: accel 1600 Hz   :   9600 B/s =   76.8 kbit/s, 하루    791.0 MiB
6축  50 Hz: 1024 B 채우는 데 1706.7 ms → MCU wake  0.59 회/s
6축 100 Hz: 1024 B 채우는 데  853.3 ms → MCU wake  1.17 회/s
6축 400 Hz: 1024 B 채우는 데  213.3 ms → MCU wake  4.69 회/s
```

출력에서 볼 것: 6축 100 Hz는 1.2 KB/s, 하루 약 99 MiB다. BLE로 실시간 전송하기에는 작은 값이지만(H2), 하루 종일 플래시에 쌓으면 웨어러블 저장 공간에서는 부담이 될 수 있다 — 그래서 데이터 수집 펌웨어는 "이벤트 전후만 저장", "압축", "다운샘플" 같은 정책을 둔다(H1, H8). 타임스탬프·헤더 오버헤드는 별도다(G7). FIFO watermark를 1 KiB로 두면 6축 100 Hz에서 MCU는 1초에 약 1번만 깨면 된다.

---

## 15. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 정지 상태 1 g를 "오류"로 보고 빼 버림 | 기울기 정보가 사라짐, 방향 추정 불가 | specific force 개념 부족 | 중력은 신호다. 빼려면 방향까지 추정해서 뺀다 (B7 §2.2, G3) |
| range를 너무 작게 (±2/±4 g, ±250 dps) | 로그에 평평한 고원, 낙상·제스처 정확도 저하 | 포화 | 탐색 수집으로 최대값 분포 확인 후 결정, 포화 샘플 수를 품질 지표로 (H7) |
| 감도를 32768/FS로 계산 | 자이로 각도가 수십 % 틀림 | 데이터시트 표 값과 다름 (§4.2) | 감도 상수는 데이터시트 표에서, 턴테이블로 검증 |
| ODR만 낮추고 필터 확인 안 함 / MCU에서 그냥 솎기 | 이상한 저주파 성분, 진동 환경에서만 오작동 | aliasing | 센서 LPF 설정 확인, MCU에서 솎을 땐 LPF 먼저 (G5) |
| 자이로 bias를 한 번만 보정 | 수 분 뒤 heading·자세 drift | 온도·시간에 따른 bias 이동 | 정지 구간마다 재추정, 온도 기록, Allan 바닥의 τ로 창 길이 결정 |
| 가속도 이중 적분으로 거리·위치 추정 | 수 초 만에 수 m 오차 | bias·중력 누설이 t²로 자람 | 외부 기준(ZUPT·GPS) 퓨전, 또는 적분 대신 raw 창 분류 |
| 축 방향·부호를 확인 안 함 | 좌우 손목·기기 간 모델 성능 차이, 회전 방향 반대 | 칩 실장 방향, 좌표계 규약 차이 | bring-up 때 6면 정지 + 손 회전 테스트, orientation 행렬을 로그 헤더에 |
| 지자기를 스피커 옆에 두고 calibration만 믿음 | 음악 재생 중 heading 튐 | 시간에 따라 변하는 자기 방해 | 배치 변경, 6축 설계 검토, 자기장 크기 이상 시 지자기 무시 |
| 학습 로그와 기기 설정(ODR·range·필터·모드)이 다름 | 실기기에서만 정확도 하락 | 입력 분포 차이 (train/serve skew) | 센서 설정을 로그 헤더·데이터셋 메타데이터에 남기고 비교 (H3, H5) |
| 조립 전 보드에서 calibration | 완제품에서 offset이 다름 | 케이스 조립·나사 응력 | 최종 조립 후 calibration, 공정 단계별 측정 비교 |

---

## 16. 면접에서 이렇게 말한다

**Q.** What does an accelerometer read when it's lying flat on a table, and why?

**A.** 위를 향한 축이 +1 g를 읽는다. 가속도계는 specific force, 즉 중력을 제외한 힘을 질량으로 나눈 값을 재는데, 정지한 기기에서 그 힘은 중력을 버티는 책상의 반력이라 위쪽이다. 자유낙하하면 0 g가 된다. 좌표계 규약(ENU vs NED)에 따라 부호 표기가 다를 수 있으니 데이터시트 축 그림을 확인한다고 덧붙인다.

> An accelerometer measures specific force — acceleration minus gravity — so at rest it reads about +1 g on the axis pointing up, because what it actually senses is the table pushing the proof mass upward. In free fall it reads zero. That's why I always check the axis diagram and the sign convention in the datasheet before trusting tilt estimates.

**Q.** The accelerometer noise density is 120 µg/√Hz and the bandwidth is 100 Hz. What's the RMS noise?

**A.** 백색잡음이면 RMS = density × √bandwidth = 120 × 10 = 1.2 mg. 필터가 벽돌 필터가 아니면 등가 잡음 대역폭을 쓴다: 1차 필터면 ×1.57이라 약 1.5 mg. pk-pk는 대략 6.6배라 8 mg 정도. 그래서 대역폭을 줄이면 noise가 √로 준다.

> For white noise, RMS equals the density times the square root of the noise bandwidth: 120 µg times √100 is 1.2 mg RMS, roughly 8 mg peak-to-peak. If the filter is first-order I'd use the equivalent noise bandwidth, about 1.57 times the cutoff, which gives about 1.5 mg.

**Q.** What does an Allan deviation plot tell you about a gyro?

**A.** τ초 평균의 안정도를 τ별로 본 것이다. log-log에서 기울기 −1/2 구간은 백색잡음이라 τ = 1 s에서 angle random walk를 읽고, 바닥은 bias instability(최솟값/0.664), +1/2 구간은 rate random walk다. 바닥의 τ는 bias를 추정할 때 몇 초 평균하는 게 최적인지 알려 준다. 고정 bias는 2차 차분이라 그래프에 안 나온다.

> It shows how stable the averaged output is as a function of averaging time. The −1/2 slope region is white noise, and reading it at one second gives the angle random walk. The flat bottom is bias instability, and the +1/2 slope is rate random walk. The location of the minimum tells me the best averaging window for bias estimation. A constant bias doesn't show up at all — Allan deviation measures stability, not offset.

**Q.** Why does integrating a gyroscope drift, and how fast?

**A.** 자이로는 각속도를 재기 때문에 각도를 얻으려면 적분해야 하고, 잔여 bias가 시간에 비례해 쌓인다. 0.05 dps만 남아도 1분에 3°. 백색잡음은 random walk라 √t로 자라지만 보통 bias가 지배한다. 그래서 가속도계(중력)와 지자기로 자세를 계속 붙잡는 퓨전을 하고, 정지 구간마다 bias를 다시 추정한다.

> Because the gyro measures rate, you have to integrate to get angle, and any residual bias integrates linearly with time — even 0.05 degrees per second is 3 degrees per minute. White noise grows only as the square root of time, so bias dominates. In practice I correct it with sensor fusion against gravity and the magnetometer, and I re-estimate bias whenever the device is stationary.

**Q.** How would you choose the IMU range and ODR for fall detection versus gesture recognition?

**A.** 낙상은 짧고 큰 충격이라 ±16 g와 충분한 ODR(최소 50~100 Hz 이상)이 필요하다. ±4 g면 피크가 잘려 충격량 정보를 잃는다. 손목 제스처는 각속도가 커서 자이로 ±1000~2000 dps, ODR 100 Hz 이상이 출발점. HAR은 25~50 Hz면 충분한 경우가 많다. 결정은 먼저 넓은 range·높은 ODR로 모은 데이터에서 최댓값 분포와 스펙트럼을 보고 낮추는 순서로 하고, 낮춘 설정으로 재샘플한 데이터에서 정확도를 다시 확인한다. 전력과 FIFO wake 빈도도 같이 본다.

> For fall detection I'd pick ±16 g and at least 50 to 100 Hz, because impacts are short and can exceed several g — at ±4 g the peak clips and you lose the impact energy. For wrist gestures, the gyro matters more, so ±1000 to 2000 dps at 100 Hz or more. Activity recognition is usually fine at 25 to 50 Hz. I'd validate by collecting with wide range and high ODR first, looking at peak distributions and spectra, then downsampling and confirming accuracy holds — and checking power at the chosen settings.

**Q.** Why is dead reckoning with a consumer IMU so hard?

**A.** 위치는 가속도를 두 번 적분해야 해서 bias가 t²로 자란다. 1 mg bias면 10초에 0.5 m, 자세를 1° 틀리면 중력이 17 mg 새어 10초에 8.6 m. 그래서 ZUPT나 GPS 같은 외부 기준 없이는 수 초를 못 간다. 제스처·HAR은 적분하지 않고 raw 창의 모양을 분류하는 이유다.

> Position needs double integration, so errors grow with t squared. A 1 mg bias gives half a meter after ten seconds, and a 1-degree attitude error leaks 17 mg of gravity, about 8.6 meters in ten seconds. Without an external reference like zero-velocity updates or GPS, it diverges within seconds — which is why for gestures and activity recognition we classify raw windows instead of integrating.

**Q.** Why do gyros consume more power than accelerometers?

**A.** MEMS 자이로는 Coriolis 효과를 쓰기 위해 질량을 공진 주파수에서 계속 진동시켜야 하고, 그 drive 루프와 복조기가 항상 돌아야 한다. 가속도계는 질량이 수동적으로 밀리는 것만 읽으면 되니 듀티 사이클링이 쉽다. 그래서 웨어러블은 가속도계만 상시 켜고 자이로는 필요할 때만 켠다. 자이로 turn-on time도 고려한다.

> A MEMS gyro needs a continuously driven resonator to create the velocity that Coriolis force acts on, plus a demodulator running all the time. An accelerometer just reads a passively deflected proof mass, so it can be duty-cycled. That's why wearables keep the accelerometer always on and wake the gyro only when needed, accounting for its turn-on time.

---

## 17. 직접 해보기

1. **손계산**: 가속도계를 x축 기준으로 60° 기울여 정지시켰다. (fx, fy, fz)는? 정답: (0, sin 60°, cos 60°) = (0, 0.866, 0.5) g, 크기 1 g.
2. **손계산**: 자이로 noise density 4 mdps/√Hz, 4차 Butterworth LPF 50 Hz. RMS noise와 대략적인 pk-pk는? 정답: 4 × √(1.03 × 50) ≈ 4 × 7.18 ≈ 28.7 mdps RMS, pk-pk ≈ 6.6 × 28.7 ≈ 190 mdps.
3. **손계산**: 가속도 bias 2 mg가 남았다. 5초 뒤 속도 오차와 위치 오차는? 정답: b = 0.0196 m/s² → 속도 0.098 m/s, 위치 ½ × 0.0196 × 25 = 0.245 m.
4. **손계산**: ODR 100 Hz에서 내부 필터 없이 솎을 때 130 Hz 진동은 몇 Hz로 보이나? 정답: |130 − 100| = 30 Hz (Nyquist 50 Hz 안).
5. **코드 과제**: §9.3 코드에서 flicker 항을 빼고(B = 0) 다시 돌려 보라. 바닥이 어떻게 변하나? 힌트: 백색잡음 −1/2 직선과 RRW +1/2 직선이 V자로 만나 바닥이 더 낮고 뾰족해진다 — "바닥이 있다고 무조건 flicker는 아니다".
6. **코드 과제**: §11.2의 `fall_signal`을 3200 Hz로 만든 뒤 4차 Butterworth LPF를 걸고 16개 중 1개씩 솎아(200 Hz) 피크를 보라. cutoff를 100, 40, 20 Hz로 바꾸면 피크가 어떻게 변하나? 정답: 직접 돌려 보면 약 9.9 g, 8.8 g, 6.0 g — 20 ms 충격은 수십 Hz까지 성분이 있어서 낮은 ODR용 LPF(예: ODR 50 Hz에 cutoff 20 Hz)를 지나면 피크가 크게 깎인다. 그래서 낙상 모델은 피크 하나보다 충격량(적분)이나 창 전체 모양을 보는 편이 설정 변화에 강건하다.

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| specific force | 비력 | 중력을 뺀 힘 / 질량. 가속도계가 실제로 재는 양 (f = a − g⃗) |
| proof mass | 검증 질량 | 스프링에 매달려 관성으로 밀리는 MEMS 실리콘 조각 |
| Coriolis force | 코리올리 힘 | 회전 좌표계에서 움직이는 물체에 생기는 겉보기 힘. 자이로의 감지 원리 |
| drive / sense mode | 구동·감지 모드 | 자이로가 일부러 진동시키는 축 / Coriolis로 흔들리는 축 |
| full-scale range (FS) | 측정 범위 | 포화 없이 잴 수 있는 최대값. ±g, ±dps |
| sensitivity | 감도 | 단위 물리량당 LSB 수 (LSB/g) 또는 그 역수 (mg/LSB) |
| noise density | 잡음 밀도 | 1 Hz 대역폭당 noise. µg/√Hz, mdps/√Hz |
| ENBW | 등가 잡음 대역폭 | noise 계산에 쓰는 대역폭. 필터 차수에 따라 cutoff × 1.0~1.57 |
| ODR | 출력 데이터율 | 센서가 새 샘플을 내는 속도 (Hz) |
| aliasing | 앨리어싱 | Nyquist보다 높은 성분이 저주파로 접혀 보이는 현상 |
| zero-g / zero-rate offset | 영점 오차 | 입력 0일 때의 출력. 가속도계·자이로 bias |
| TCO | offset 온도 계수 | 온도 1 °C당 offset 변화 |
| cross-axis sensitivity | 축간 감도 | 다른 축 입력이 새어 들어오는 비율 (%) |
| nonlinearity | 비선형성 | 입출력이 직선에서 벗어나는 정도 (%FS) |
| Allan deviation | 앨런 편차 | 평균 시간 τ별 안정도. 잡음 종류를 기울기로 구분 |
| ARW | angle random walk | 백색 rate noise가 적분되어 생기는 각도 random walk. °/√h |
| bias instability | 바이어스 불안정성 | Allan 그래프 바닥 / 0.664. 최선으로 추정 가능한 bias 안정도 |
| RRW | rate random walk | bias가 천천히 random walk 하는 성분. Allan 기울기 +1/2 |
| hard / soft iron | 경철·연철 왜곡 | 지자기에 더해지는 고정 offset / 장을 휘게 하는 행렬 왜곡 |
| AHRS | 자세·방위 기준 시스템 | IMU(+지자기) 퓨전으로 roll·pitch·heading 출력 |
| ZUPT | zero-velocity update | 정지 순간(발 착지 등) 속도를 0으로 리셋해 적분 drift를 억제 |
| vibration rectification | 진동 정류 | 진동이 비선형성 때문에 DC offset으로 바뀌는 현상 |

---

## 19. 요약 & 체크리스트

가속도계는 specific force(중력 제외 힘/질량)를 재서 정지 시 위쪽 +1 g를 읽고, 자이로는 drive 진동에 생기는 Coriolis 힘으로 각속도를 재며(그래서 전력이 크다), 지자기는 µT 단위 자기장을 재지만 hard/soft iron에 취약하다. 데이터시트 숫자는 ADC 사양처럼 읽으면 된다: range와 sensitivity가 포화와 LSB를 정하고, noise density × √(대역폭)이 RMS noise를, ODR과 내부 LPF가 aliasing을, offset·온도 계수·감도 오차·cross-axis가 calibration 필요성을 정한다. Allan deviation은 τ별 안정도로 ARW·bias instability·RRW를 구분한다. 적분하면 자이로 bias는 t로, 가속도 bias와 중력 누설은 t²로 자라서 IMU 단독 위치 추정은 수 초 만에 무너진다 — 그래서 ML은 raw 창의 모양을 분류한다. 과제별 range·ODR은 넓게 모은 데이터의 최댓값·스펙트럼을 보고 정하고, 전력은 "가속도계 상시 + 자이로 필요할 때 + FIFO batching"으로 아낀다.

- [ ] 정지·자유낙하·위로 가속 상황에서 가속도계 출력을 부호까지 손으로 계산할 수 있다
- [ ] MEMS 가속도계(질량·스프링·차동 정전용량)와 자이로(drive·sense·Coriolis·복조)를 그림으로 설명할 수 있다
- [ ] range·16-bit에서 sensitivity, resolution, 양자화 잡음을 손으로 계산하고, 감도를 데이터시트 표에서 가져와야 하는 이유를 말할 수 있다
- [ ] noise density와 대역폭(ENBW 포함)으로 RMS·pk-pk noise를 계산할 수 있다
- [ ] ODR과 alias 주파수를 계산하고, MCU에서 솎을 때 LPF가 필요한 이유를 말할 수 있다
- [ ] 측정 = M·참값 + b + n 모델의 각 항이 데이터시트의 어느 항목인지 짝지을 수 있다
- [ ] numpy로 overlapping Allan deviation을 구현하고 ARW·bias instability를 읽을 수 있다
- [ ] 자이로 bias와 가속도 bias·중력 누설의 적분 오차를 1분 단위로 손계산할 수 있다
- [ ] 낙상·제스처·HAR의 range·ODR 출발점을 근거와 함께 말하고, 포화를 로그에서 찾을 수 있다
- [ ] int16 → SI 변환과 포화 검출을 C로 구현하고, 로깅 데이터율·FIFO wake 빈도를 계산할 수 있다

---

## 참고 자료

- IEEE Std 952-1997, "IEEE Standard Specification Format Guide and Test Procedure for Single-Axis Interferometric Fiber Optic Gyros" — 부록의 Allan variance 잡음 항 정의(ARW, bias instability 0.664, RRW)
- N. El-Sheimy, H. Hou, X. Niu, "Analysis and Modeling of Inertial Sensors Using Allan Variance", IEEE Transactions on Instrumentation and Measurement, 2008
- O. J. Woodman, "An introduction to inertial navigation", University of Cambridge Computer Laboratory Technical Report UCAM-CL-TR-696, 2007 — 적분 drift, Allan variance를 쉽게 정리
- D. W. Allan, "Statistics of Atomic Frequency Standards", Proceedings of the IEEE, 1966 — Allan variance의 원전
- Bosch Sensortec BMI270·BMM150, STMicroelectronics LSM6DSO·LSM6DSOX·LIS2MDL, TDK InvenSense ICM-42688-P 데이터시트 — 각 제조사 웹사이트의 최신판
- ST 앱노트 AN5259 "LSM6DSOX: Machine Learning Core" — 센서 내장 ML
- Android Developers — Motion sensors / SensorEvent 문서 (가속도계 좌표계와 정지 시 +9.81 규약): https://developer.android.com/reference/android/hardware/SensorEvent
- 관련 노트: A2(noise·Gaussian), A6(바이너리 로그·단위 변환), B7(IMU 모델), G2(인터페이스·FIFO), G3(calibration·퓨전), G5(DSP·aliasing), G7(시간 동기화), H1·H7(로깅·품질 모니터링)
