# A6. 데이터 도구 — numpy·pandas·matplotlib으로 센서 로그 다루기

> **이 노트를 다 읽으면**: 펌웨어가 쓴 packed 바이너리 IMU 로그를 numpy structured dtype으로 한 줄에 읽을 수 있다 · pandas로 드롭 샘플 검출·재샘플·라벨 병합을 할 수 있다 · 로그를 `X [N, C, L]`, `y [N]` 학습 데이터셋으로 바꾸고 leakage 없이 split할 수 있다 · 특징·정규화 통계를 뽑아 펌웨어 C 헤더로 넘길 수 있다
> **JD 연결**: "Build data collection and ingestion pipelines … various sensors, at scale", "Experience building sensor data collection pipelines", "Hands-on with IMUs, accelerometers, gyroscopes" — study_prep_list A6(numpy, pandas, matplotlib, Jupyter로 센서 로그 시각화), G7(샘플 누락 검출·resampling), H1(로그 포맷), H7(clipping·saturation 모니터링)
> **Don 기준 난이도**: 이미 아는 것 — C 구조체 레이아웃·endianness·링버퍼·로그 파싱 스크립트 / 새로 배울 것 — numpy의 axis·broadcasting·view 관용구, pandas 시계열 인덱스, 학습용 윈도잉과 split 규칙
> **선행 노트**: A0, A1

---

## 0. 큰 그림 — 이게 왜 필요한가

edge ML 엔지니어의 하루는 모델보다 **데이터**에 더 많은 시간을 쓴다. 예를 들어 Hark 같은 웨어러블(추정)에서 "손목 flick 제스처"를 인식하는 작은 모델을 만든다고 하자. 모델 학습 코드는 50줄이면 끝나지만, 그 전에 해야 할 일이 훨씬 많다.

- 펌웨어가 flash에 쓴 바이너리 로그를 PC에서 읽는다.
- raw count를 g, dps 같은 물리 단위로 바꾼다.
- FIFO overflow로 빠진 샘플, 포화(saturation), 멈춘 센서를 찾아낸다.
- 불규칙한 timestamp를 고정 50 Hz 격자로 맞춘다.
- 사람이 만든 라벨 구간표("8.0~10.0 s = flick")를 샘플에 붙인다.
- 2초짜리 창(window)으로 잘라 `X`, `y`를 만들고, 사용자 단위로 train/test를 나눈다.
- 특징(feature)과 정규화 통계를 계산해서 펌웨어에 **똑같이** 넣는다.

이 전부를 하는 도구가 numpy(배열 계산), pandas(시간 인덱스가 있는 표), matplotlib(그림)이다.

```svg
<svg viewBox="0 0 680 200" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="a6p-ar" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="60" y="22" font-size="12" text-anchor="middle">§4 (C)</text> <text x="197" y="22" font-size="12" text-anchor="middle">§1~4</text> <text x="334" y="22" font-size="12" text-anchor="middle">§5</text>
  <text x="471" y="22" font-size="12" text-anchor="middle">§6</text> <text x="608" y="22" font-size="12" text-anchor="middle">§7</text>
  <rect x="5" y="30" width="110" height="72" rx="6" fill="none" stroke="#888" stroke-width="1.5"/> <text x="60" y="52" font-size="13" text-anchor="middle">펌웨어 로거</text>
  <text x="60" y="70" font-size="12" text-anchor="middle">FIFO → 링버퍼</text> <text x="60" y="88" font-size="12" text-anchor="middle">→ flash .bin</text>
  <rect x="142" y="30" width="110" height="72" rx="6" fill="none" stroke="#4a7bd0" stroke-width="1.5"/> <text x="197" y="52" font-size="13" text-anchor="middle">numpy</text>
  <text x="197" y="70" font-size="12" text-anchor="middle">np.fromfile</text> <text x="197" y="88" font-size="12" text-anchor="middle">int16 → g, dps</text>
  <rect x="279" y="30" width="110" height="72" rx="6" fill="none" stroke="#4a7bd0" stroke-width="1.5"/> <text x="334" y="52" font-size="13" text-anchor="middle">pandas</text>
  <text x="334" y="70" font-size="12" text-anchor="middle">드롭·재샘플</text> <text x="334" y="88" font-size="12" text-anchor="middle">라벨 병합</text>
  <rect x="416" y="30" width="110" height="72" rx="6" fill="none" stroke="#3f9a6b" stroke-width="1.5"/> <text x="471" y="52" font-size="13" text-anchor="middle">윈도잉</text>
  <text x="471" y="70" font-size="12" text-anchor="middle">X [N, C, L]</text> <text x="471" y="88" font-size="12" text-anchor="middle">y [N]</text>
  <rect x="553" y="30" width="110" height="72" rx="6" fill="none" stroke="#e08a3c" stroke-width="1.5"/> <text x="608" y="52" font-size="13" text-anchor="middle">특징·학습</text>
  <text x="608" y="70" font-size="12" text-anchor="middle">mean/std/RMS</text> <text x="608" y="88" font-size="12" text-anchor="middle">정규화 통계</text>
  <line x1="115" y1="66" x2="140" y2="66" stroke="currentColor" stroke-width="1.5" marker-end="url(#a6p-ar)"/> <line x1="252" y1="66" x2="277" y2="66" stroke="currentColor" stroke-width="1.5" marker-end="url(#a6p-ar)"/>
  <line x1="389" y1="66" x2="414" y2="66" stroke="currentColor" stroke-width="1.5" marker-end="url(#a6p-ar)"/> <line x1="526" y1="66" x2="551" y2="66" stroke="currentColor" stroke-width="1.5" marker-end="url(#a6p-ar)"/>
  <polyline points="608,102 608,160 60,160 60,106" fill="none" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="5 4" marker-end="url(#a6p-ar)"/>
  <text x="334" y="152" font-size="12" text-anchor="middle">norm_stats.json → norm_stats.h (C 헤더) → 펌웨어가 같은 전처리를 한다</text>
  <text x="334" y="185" font-size="12" text-anchor="middle">Python 파이프라인과 펌웨어 전처리가 비트 단위로 같아야 모델이 현장에서 동작한다</text>
</svg>
```

그림 1 — 이 노트가 다루는 흐름. 왼쪽에서 오른쪽으로 로그가 데이터셋이 되고, 점선처럼 정규화 통계가 다시 펌웨어로 돌아간다. 각 박스 위의 번호가 이 노트의 절 번호다.

Don에게 익숙한 말로 바꾸면 이렇다.

| 도구 | 한 줄 정의 | 펌웨어 비유 |
|---|---|---|
| numpy `ndarray` | 같은 타입 원소가 연속 메모리에 놓인 N차원 배열 | `int16_t buf[N][6]` + shape/stride 메타데이터 |
| numpy 벡터화 | 원소별 연산을 C 루프 한 번으로 처리 | 원소마다 CPU가 만지는 대신 DMA/SIMD로 블록 처리 |
| pandas `DataFrame` | 이름 붙은 열 + 인덱스(여기선 시간)가 있는 표 | timestamp 열이 키인 로그 분석용 스프레드시트 |
| matplotlib | 그림을 그려 PNG로 저장 | 로직 분석기/오실로스코프 화면 캡처 |

이 노트의 모든 예제는 같은 데이터를 쓴다. §4.2의 C 프로그램이 만든 30초짜리 IMU 로그 3개(`imu_s1.bin`, `imu_s2.bin`, `imu_s3.bin`)다. 50 Hz, ±4 g, ±500 dps, 8~10 s에 flick(ax 3 Hz), 20~22 s에 shake(ay 5 Hz, 일부러 포화)가 들어 있고, s1에는 FIFO overflow로 샘플 6개가 빠져 있다.

---

## 1. numpy의 기본 — ndarray와 dtype

### 1.1 ndarray = 포인터 + dtype + shape + strides

numpy 배열은 마법이 아니다. C로 치면 아래 구조체와 거의 같다.

```
struct ndarray {              // 개념도 (실제 CPython 구조체를 단순화)
    char   *data;             // 연속 버퍼의 시작 주소
    dtype   dtype;            // 원소 하나의 타입: int16, float32 ...
    int     ndim;             // 차원 수
    ssize_t shape[ndim];      // 각 축의 길이       예: (1500, 6)
    ssize_t strides[ndim];    // 각 축으로 1칸 갈 때 건너뛸 바이트 예: (12, 2)
};

int16 배열 shape (1500, 6), C order(row-major):
주소 = data + i·strides[0] + j·strides[1] = data + i·12 + j·2
       ┌─ax─┬─ay─┬─az─┬─gx─┬─gy─┬─gz─┐┌─ax─┬─ay─ ...
       │ 0  │ 2  │ 4  │ 6  │ 8  │ 10 ││ 12 │ 14 │   ← 바이트 오프셋
       └────┴────┴────┴────┴────┴────┘└────┴────
        ◄──────── 행 0 (12 B) ───────►◄── 행 1 ...
```

말로 하면, `a[i, j]`는 C의 `buf[i][j]`와 같은 주소 계산이다. 다른 점은 strides를 **바꿀 수 있다**는 것이다. transpose, slicing, sliding window는 전부 데이터를 복사하지 않고 strides만 바꾼다 (§3에서 본다).

### 1.2 dtype — float64 기본값과 int16 raw count

dtype은 원소 하나의 타입이다. ML 작업에서 중요한 것은 세 가지다.

| dtype | 크기 | 어디서 나오나 | 주의 |
|---|---|---|---|
| `int16` | 2 B | IMU/마이크 raw count (16-bit ADC) | 곱하면 쉽게 overflow (wrap-around) |
| `float64` | 8 B | 파이썬 float, `np.zeros()` 등 **numpy 기본값** | 메모리 2배, PyTorch 모델과 dtype 불일치 |
| `float32` | 4 B | PyTorch 기본, NPU/DSP float 경로 | 학습·배포 표준. 일부러 선택해야 한다 |

손으로 먼저 계산해 보자. int16은 −32768~32767이고 넘치면 65536으로 나눈 나머지로 돌아간다(C의 `int16_t` 캐스트와 같다).

```
16384² = 268,435,456 = 2²⁸       → 2²⁸ mod 2¹⁶ = 0
(−8192)² = 67,108,864 = 2²⁶      → 0
32767² = 1,073,676,289           → 1,073,676,289 mod 65,536 = 1
```

말로 하면, int16 배열끼리 곱하면 결과도 int16이라서 제곱 에너지나 RMS 계산이 조용히 망가진다. 코드로 확인한다.

이 코드는 dtype 기본값, int16 overflow, float64→PyTorch 불일치를 한 번에 확인한다.

```python
import numpy as np

a = np.array([0.1, 0.2, 0.3])            # 파이썬 float → float64
b = np.zeros(3)                          # 기본 dtype도 float64
raw = np.array([16384, -8192, 32767], dtype=np.int16)   # 센서 raw count
print(a.dtype, b.dtype, raw.dtype, raw.itemsize, "B/elem")

n = 1_000_000
print("1M float64:", np.zeros(n).nbytes, "B")
print("1M float32:", np.zeros(n, dtype=np.float32).nbytes, "B")

# 함정 1: int16끼리 곱하면 int16에서 wrap-around
print("raw*raw        :", raw * raw)
print("int32로 올려서 :", raw.astype(np.int32) ** 2)

# 함정 2: int16 → g 변환 후 dtype은 float64로 '승격'된다
g = raw / 8192
print("raw/8192 dtype :", g.dtype, g)
g32 = raw.astype(np.float32) / np.float32(8192)
print("float32 유지   :", g32.dtype, g32)

# 함정 3: float64 텐서를 float32 모델에 넣으면 PyTorch가 거부한다
import torch
lin = torch.nn.Linear(3, 1)
try:
    lin(torch.from_numpy(g))
except RuntimeError as e:
    print("RuntimeError:", e)
print("OK:", lin(torch.from_numpy(g32)).dtype)
```

```text
float64 float64 int16 2 B/elem
1M float64: 8000000 B
1M float32: 4000000 B
raw*raw        : [0 0 1]
int32로 올려서 : [ 268435456   67108864 1073676289]
raw/8192 dtype : float64 [ 2.         -1.          3.99987793]
float32 유지   : float32 [ 2.       -1.        3.999878]
RuntimeError: mat1 and mat2 must have the same dtype, but got Double and Float
OK: torch.float32
```

출력에서 볼 것: `raw*raw`가 손계산대로 `[0 0 1]`이다. 그리고 `raw / 8192`처럼 평범하게 나누면 결과가 float64가 되어, PyTorch `Linear`(float32 가중치)가 "Double and Float" 에러를 낸다.

### 1.3 왜 float32인가

| 이유 | 설명 |
|---|---|
| 메모리·대역폭 절반 | 1M 샘플 × 6채널: float64 48 MB vs float32 24 MB. 학습 시 GPU 전송량도 절반 |
| 프레임워크 기본값 | PyTorch 파라미터는 float32. 입력을 맞추지 않으면 에러 또는 조용한 업캐스트 |
| 정밀도가 충분 | 16-bit 센서의 유효 정보는 약 5자리. float32 가수부는 24 bit(약 7자리)라 손실이 없다 |
| 배포 경로와 일치 | MCU/DSP/NPU는 float32 또는 int8. float64로 만든 기준값은 펌웨어와 미세하게 달라진다 |

임베디드 연결: Cortex-M4F/M7의 FPU는 기본이 single precision이다(M7은 double 옵션이 있지만 느리다). 즉 PC에서 float32로 전처리해 두면 펌웨어 결과와 비교할 golden vector를 그대로 쓸 수 있다.

---

## 2. shape와 axis, broadcasting, mask

### 2.1 axis — "어느 축을 접어서 없애는가"

ML 데이터는 보통 `(샘플 수, 채널 수)` 모양이다. `mean(axis=k)`는 **k번 축을 따라 평균을 내고 그 축을 없앤다**. 헷갈리면 이렇게 외운다: axis=0은 "행들을 위아래로 눌러 한 줄로", axis=1은 "열들을 옆으로 눌러 한 칸으로".

4개 샘플 × 3채널 행렬로 손계산한다.

```
          ax  ay  az      axis=1 평균 (샘플별)
샘플0  [   2,  1,  9 ]    (2+1+9)/3 = 4
샘플1  [   4, -1,  9 ]    (4−1+9)/3 = 4
샘플2  [   6,  0,  9 ]    (6+0+9)/3 = 5
샘플3  [   0,  0,  9 ]    (0+0+9)/3 = 3

axis=0 평균 (채널별): ax=(2+4+6+0)/4=3, ay=(1−1+0+0)/4=0, az=9
```

```svg
<svg viewBox="0 0 600 280" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="a6x-ar" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="175" y="32" font-size="13" text-anchor="middle">ax</text> <text x="225" y="32" font-size="13" text-anchor="middle">ay</text> <text x="275" y="32" font-size="13" text-anchor="middle">az</text>
  <text x="140" y="62" font-size="12" text-anchor="end">샘플0</text> <text x="140" y="96" font-size="12" text-anchor="end">샘플1</text> <text x="140" y="130" font-size="12" text-anchor="end">샘플2</text>
  <text x="140" y="164" font-size="12" text-anchor="end">샘플3</text> <rect x="150" y="40" width="150" height="136" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <line x1="200" y1="40" x2="200" y2="176" stroke="currentColor" stroke-width="0.8"/> <line x1="250" y1="40" x2="250" y2="176" stroke="currentColor" stroke-width="0.8"/>
  <line x1="150" y1="74" x2="300" y2="74" stroke="currentColor" stroke-width="0.8"/> <line x1="150" y1="108" x2="300" y2="108" stroke="currentColor" stroke-width="0.8"/>
  <line x1="150" y1="142" x2="300" y2="142" stroke="currentColor" stroke-width="0.8"/>
  <text x="175" y="62" font-size="14" text-anchor="middle">2</text><text x="225" y="62" font-size="14" text-anchor="middle">1</text><text x="275" y="62" font-size="14" text-anchor="middle">9</text>
  <text x="175" y="96" font-size="14" text-anchor="middle">4</text><text x="225" y="96" font-size="14" text-anchor="middle">-1</text><text x="275" y="96" font-size="14" text-anchor="middle">9</text>
  <text x="175" y="130" font-size="14" text-anchor="middle">6</text><text x="225" y="130" font-size="14" text-anchor="middle">0</text><text x="275" y="130" font-size="14" text-anchor="middle">9</text>
  <text x="175" y="164" font-size="14" text-anchor="middle">0</text><text x="225" y="164" font-size="14" text-anchor="middle">0</text><text x="275" y="164" font-size="14" text-anchor="middle">9</text>
  <line x1="175" y1="180" x2="175" y2="208" stroke="#4a7bd0" stroke-width="2" marker-end="url(#a6x-ar)"/> <line x1="225" y1="180" x2="225" y2="208" stroke="#4a7bd0" stroke-width="2" marker-end="url(#a6x-ar)"/>
  <line x1="275" y1="180" x2="275" y2="208" stroke="#4a7bd0" stroke-width="2" marker-end="url(#a6x-ar)"/> <rect x="150" y="212" width="150" height="34" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/>
  <line x1="200" y1="212" x2="200" y2="246" stroke="#4a7bd0" stroke-width="0.8"/> <line x1="250" y1="212" x2="250" y2="246" stroke="#4a7bd0" stroke-width="0.8"/>
  <text x="175" y="234" font-size="14" text-anchor="middle">3</text><text x="225" y="234" font-size="14" text-anchor="middle">0</text><text x="275" y="234" font-size="14" text-anchor="middle">9</text>
  <text x="225" y="268" font-size="12" text-anchor="middle">mean(axis=0) → shape (3,) 채널별 평균</text> <line x1="304" y1="108" x2="352" y2="108" stroke="#e08a3c" stroke-width="2" marker-end="url(#a6x-ar)"/>
  <rect x="360" y="40" width="50" height="136" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/> <line x1="360" y1="74" x2="410" y2="74" stroke="#e08a3c" stroke-width="0.8"/>
  <line x1="360" y1="108" x2="410" y2="108" stroke="#e08a3c" stroke-width="0.8"/> <line x1="360" y1="142" x2="410" y2="142" stroke="#e08a3c" stroke-width="0.8"/>
  <text x="385" y="62" font-size="14" text-anchor="middle">4</text><text x="385" y="96" font-size="14" text-anchor="middle">4</text><text x="385" y="130" font-size="14" text-anchor="middle">5</text><text x="385" y="164" font-size="14" text-anchor="middle">3</text>
  <text x="420" y="100" font-size="12">mean(axis=1)</text> <text x="420" y="118" font-size="12">→ shape (4,)</text> <text x="420" y="136" font-size="12">샘플별 평균</text>
  <text x="225" y="14" font-size="12" text-anchor="middle">X.shape = (4, 3)</text>
</svg>
```

그림 2 — 같은 (4, 3) 행렬에서 axis=0은 행 방향을 접어 채널별 값 3개(파랑)를, axis=1은 열 방향을 접어 샘플별 값 4개(주황)를 남긴다.

센서 데이터에서 의미: axis=0 평균은 "ax 채널의 평균 bias", axis=1 평균은 "한 시점의 세 축 평균"이다. 후자는 물리적으로 거의 쓸 데가 없다. **정규화 통계는 거의 항상 채널별(axis=0)**이다. axis를 잘못 주면 에러 없이 틀린 숫자가 나오므로 shape을 항상 찍어 본다.

### 2.2 broadcasting — shape이 다른 배열끼리 연산

broadcasting은 크기가 다른 배열을 연산할 때 작은 쪽을 **복사하지 않고** 늘린 것처럼 취급하는 규칙이다.

```
규칙: 두 shape을 오른쪽 끝부터 한 축씩 비교한다.
      각 축은 (1) 길이가 같거나 (2) 한쪽이 1이거나 (3) 한쪽에 축이 없으면 OK.

(4, 3) − (3,)    →  (4, 3) − (1, 3)  →  OK, 각 행에서 같은 3개를 뺀다
(4, 3) − (4,)    →  끝 축 3 vs 4      →  에러
(4, 3) − (4, 1)  →  끝 축 3 vs 1      →  OK, 각 열에서 같은 4개를 뺀다
```

말로 하면, "뒤에서부터 맞춘다"가 전부다. C로 치면 `for i: for j: X[i][j] -= mu[j];`의 이중 루프를 numpy가 대신 돌려 주는 것이다.

이 코드는 axis 평균과 broadcasting 센터링, 그리고 흔한 shape 에러를 확인한다.

```python
import numpy as np

# 4개 샘플(행) × 3개 채널(열: ax, ay, az) — 단위는 임의의 정수
X = np.array([[2,  1, 9],
              [4, -1, 9],
              [6,  0, 9],
              [0,  0, 9]])
print("shape:", X.shape)
print("axis=0 평균 (채널별):", X.mean(axis=0))
print("axis=1 평균 (샘플별):", X.mean(axis=1))
print("keepdims:", X.mean(axis=0, keepdims=True).shape, X.mean(axis=1, keepdims=True).shape)

# broadcasting: (4,3) - (3,) → 채널별 평균을 빼서 센터링
Xc = X - X.mean(axis=0)
print("centered:\n", Xc)

# (4,3) - (4,) 는 안 된다 — 뒤쪽 축부터 맞추기 때문
try:
    X - X.mean(axis=1)
except ValueError as e:
    print("ValueError:", e)
print("keepdims로 해결:\n", X - X.mean(axis=1, keepdims=True))
```

```text
shape: (4, 3)
axis=0 평균 (채널별): [3. 0. 9.]
axis=1 평균 (샘플별): [4. 4. 5. 3.]
keepdims: (1, 3) (4, 1)
centered:
 [[-1.  1.  0.]
 [ 1. -1.  0.]
 [ 3.  0.  0.]
 [-3.  0.  0.]]
ValueError: operands could not be broadcast together with shapes (4,3) (4,) 
keepdims로 해결:
 [[-2. -3.  5.]
 [ 0. -5.  5.]
 [ 1. -5.  4.]
 [-3. -3.  6.]]
```

출력에서 볼 것: 손계산과 같은 `[3, 0, 9]`, `[4, 4, 5, 3]`. `keepdims=True`는 접은 축을 길이 1로 남겨서 broadcasting이 의도대로 되게 한다.

함정: 반대로 **우연히 shape이 맞아서** 틀린 broadcasting이 에러 없이 도는 경우가 더 위험하다. 예를 들어 (3, 3) 행렬에서 axis를 잘못 주면 shape이 둘 다 (3,)이라 아무 경고도 없다. 정사각 shape은 디버깅용 테스트 데이터에서 피하는 게 좋다.

### 2.3 boolean mask, fancy indexing, `np.where`

- **boolean mask**: 같은 shape의 True/False 배열. `a[mask]`는 True 위치의 값만 뽑는다. C의 `if (cond) out[k++] = a[i];` 루프다.
- **fancy indexing**: 정수 배열로 인덱스를 지정해 골라 온다. `a[[5, 0, 2]]`. C의 gather(`out[k] = a[idx[k]]`).
- **`np.where(cond, x, y)`**: 원소별 삼항 연산자 `cond ? x : y`. 인자 하나만 주면 True 위치의 인덱스를 돌려준다.

이 코드는 가속도 값에서 큰 움직임만 골라내고, deadband와 clip을 벡터화해서 적용한다.

```python
import numpy as np

# 6개 샘플의 ax (단위 g)
ax = np.array([0.02, -0.01, 1.30, 1.45, -1.20, 0.03])

mask = np.abs(ax) > 1.0                  # boolean mask: 같은 shape의 True/False
print("mask        :", mask)
print("ax[mask]    :", ax[mask])         # 조건을 만족하는 값만 (복사본)
print("개수, 비율  :", mask.sum(), mask.mean())
print("인덱스      :", np.flatnonzero(mask))

idx = np.array([5, 0, 2])                # fancy indexing: 정수 배열로 골라 오기
print("ax[idx]     :", ax[idx])

# np.where(조건, 참일 때, 거짓일 때) — 벡터화된 if/else
print("deadband    :", np.where(np.abs(ax) < 0.05, 0.0, ax))
print("clip ±1.25  :", np.clip(ax, -1.25, 1.25))

# 2D에서 boolean mask로 '행' 고르기: 움직임이 큰 샘플만
X = np.stack([ax, ax * 0.5, np.ones(6)], axis=1)     # (6, 3)
moving = np.abs(X[:, 0]) > 1.0
print("X[moving].shape:", X[moving].shape)
```

```text
mask        : [False False  True  True  True False]
ax[mask]    : [ 1.3   1.45 -1.2 ]
개수, 비율  : 3 0.5
인덱스      : [2 3 4]
ax[idx]     : [0.03 0.02 1.3 ]
deadband    : [ 0.    0.    1.3   1.45 -1.2   0.  ]
clip ±1.25  : [ 0.02 -0.01  1.25  1.25 -1.2   0.03]
X[moving].shape: (3, 3)
```

출력에서 볼 것: `mask.sum()`은 True 개수, `mask.mean()`은 비율이다(True=1, False=0). 이 두 줄은 "포화 샘플 비율", "드롭 비율" 같은 품질 지표를 계산할 때 계속 쓴다.

---

## 3. 벡터화, view vs copy, sliding window

### 3.1 벡터화 — 파이썬 루프를 쓰지 않는 이유

파이썬 `for` 루프는 원소 하나마다 인터프리터가 타입 확인·객체 생성을 한다. numpy 연산은 같은 일을 컴파일된 C 루프(대부분 SIMD) 한 번으로 한다. 펌웨어로 치면 "바이트마다 인터럽트를 받는 것"과 "DMA로 블록을 한 번에 옮기는 것"의 차이다.

이 코드는 100만 샘플의 가속도 크기 `|a| = √(ax² + ay² + az²)`를 두 방식으로 계산하고 시간을 잰다.

```python
import time
import numpy as np

rng = np.random.default_rng(0)
N = 1_000_000
raw = rng.integers(-8192, 8192, size=(N, 3), dtype=np.int16)   # 1M 샘플 × 3축

def mag_loop(raw):
    out = [0.0] * len(raw)
    for i in range(len(raw)):
        x, y, z = raw[i]
        out[i] = (float(x) ** 2 + float(y) ** 2 + float(z) ** 2) ** 0.5 / 8192
    return out

def mag_vec(raw):
    g = raw.astype(np.float32) / np.float32(8192)
    return np.sqrt((g * g).sum(axis=1))

for f in (mag_loop, mag_vec):
    t0 = time.perf_counter(); m = f(raw); dt = time.perf_counter() - t0
    print(f"{f.__name__:9s}: {dt*1e3:8.1f} ms   ({dt/N*1e9:6.1f} ns/sample)")

print("같은 결과?", np.allclose(mag_loop(raw[:1000]), mag_vec(raw[:1000]), atol=1e-6))
```

```text
mag_loop :    794.3 ms   ( 794.3 ns/sample)
mag_vec  :     14.7 ms   (  14.7 ns/sample)
같은 결과? True
```

출력에서 볼 것: Apple M2에서 약 54배 차이다(시간은 머신마다 다르다). 50 Hz × 6채널 × 하루치 로그(약 2,600만 값)를 루프로 처리하면 수십 초, 벡터화하면 1초 미만이다. 또 `mag_vec`에서 `g * g`를 쓴 이유는 int16 제곱 overflow(§1.2)를 피하려고 float로 먼저 바꿨기 때문이다.

### 3.2 view vs copy — slicing은 복사가 아니다

numpy에서 `x[a:b]` 같은 basic slicing은 **view**를 돌려준다. view는 같은 버퍼를 가리키는 새 헤더(포인터 + shape + strides)일 뿐이다. C로 치면 `int16_t *head = &x[0];`와 같아서, `head`를 고치면 `x`도 바뀐다.

| 연산 | 결과 | C 비유 |
|---|---|---|
| `x[2:8]`, `x[::2]`, `x.T`, `x.reshape(...)`(가능하면) | view | 포인터 + stride 변경 |
| `x[[0, 1, 2]]`, `x[mask]` | copy | gather해서 새 버퍼에 memcpy |
| `x.copy()`, `np.array(x)` | copy | `malloc` + `memcpy` |
| `x.astype(np.float32)` | copy (dtype이 바뀌므로) | 변환 루프 |

이 코드는 view를 통한 수정이 원본에 번지는 것과, strides가 어떻게 바뀌는지 확인한다.

```python
import numpy as np

x = np.arange(10, dtype=np.int16)        # 0..9, 2 B씩
print("strides x:", x.strides)

head = x[:4]                             # slicing → view (같은 메모리)
head[0] = 99                             # view를 고치면 원본도 바뀐다!
print("x after head[0]=99:", x)
print("shares_memory(x, head):", np.shares_memory(x, head))

safe = x[:4].copy()                      # 독립 버퍼가 필요하면 .copy()
safe[1] = -1
print("x after safe[1]=-1 :", x)

picked = x[[0, 1, 2]]                    # fancy indexing → 항상 copy
print("shares_memory(x, picked):", np.shares_memory(x, picked))

M = x.reshape(2, 5)                      # reshape → 가능하면 view
print("M strides:", M.strides, " M.T strides:", M.T.strides)

# sliding_window_view: 복사 없이 stride만 바꿔 창을 만든다
w = np.lib.stride_tricks.sliding_window_view(x, window_shape=4)
print("windows shape:", w.shape, "strides:", w.strides)
print(w[::2])                            # hop=2 (50% overlap)
print("shares_memory(x, w):", np.shares_memory(x, w), " writeable:", w.flags.writeable)
```

```text
strides x: (2,)
x after head[0]=99: [99  1  2  3  4  5  6  7  8  9]
shares_memory(x, head): True
x after safe[1]=-1 : [99  1  2  3  4  5  6  7  8  9]
shares_memory(x, picked): False
M strides: (10, 2)  M.T strides: (2, 10)
windows shape: (7, 4) strides: (2, 2)
[[99  1  2  3]
 [ 2  3  4  5]
 [ 4  5  6  7]
 [ 6  7  8  9]]
shares_memory(x, w): True  writeable: False
```

출력에서 볼 것: `head[0] = 99`가 `x`를 바꿨다. `M.T`는 strides만 `(10, 2)`에서 `(2, 10)`으로 바꾼 view다. 실무 함정: 전처리 함수 안에서 `seg = sig[a:b]; seg -= seg.mean()`처럼 in-place로 빼면 **원본 로그가 망가진다**. 함수가 입력을 바꾸면 안 될 때는 `seg = sig[a:b] - sig[a:b].mean()`(새 배열) 또는 `.copy()`를 쓴다.

### 3.3 `sliding_window_view` — stride 트릭으로 만든 창

길이 T인 신호에서 길이 L짜리 창을 모두 만들면 원소가 L배로 늘어나야 할 것 같지만, `sliding_window_view`는 strides를 `(2, 2)`로 만들어서 **메모리 0바이트 추가**로 `(T−L+1, L)` 배열처럼 보이게 한다.

```
x 버퍼:  [99][ 1][ 2][ 3][ 4][ 5][ 6][ 7][ 8][ 9]    (int16, 2 B씩)
w[0] →    ^---------------^                          시작 주소 +0
w[1] →        ^---------------^                      시작 주소 +2  (strides[0] = 2)
w[2] →            ^---------------^                  시작 주소 +4
         창 안에서 다음 원소: +2 B                   (strides[1] = 2)
```

말로 하면, "창 번호가 1 늘면 1원소 뒤로, 창 안에서도 1원소 뒤로"라는 strides다. 펌웨어로 치면 링버퍼 위에 창 시작 포인터만 옮겨 가며 같은 데이터를 다시 읽는 것과 같다. hop은 `w[::hop]`으로 준다(이것도 view). 결과는 읽기 전용(`writeable: False`)이다. 겹친 창에 쓰면 여러 창이 동시에 바뀌기 때문이다. 모델에 넣기 전에 연속 버퍼가 필요하면 `np.ascontiguousarray()`로 복사한다(§6.2).

---

## 4. 펌웨어 바이너리 로그 읽기

### 4.1 로그 포맷 정의

펌웨어가 쓰는 로그는 보통 "고정 헤더 + 고정 크기 레코드 배열"이다. 이 노트에서는 아래 포맷을 쓴다(실제 제품 포맷은 회사마다 다르다).

```
오프셋  크기  필드            설명
─── 헤더 16 B ───────────────────────────────────────────
0       4     magic           "IMU1" — 파일 종류 확인
4       2     version         포맷 버전 (필드가 바뀌면 올린다)
6       2     odr_hz          50
8       2     acc_fs_g        4   (±4 g)
10      2     gyr_fs_dps      500 (±500 dps)
12      4     n_records       레코드 개수 (잘린 파일 검출용)
─── 레코드 16 B × n ─────────────────────────────────────
0       4     timestamp_us    uint32, 마이크로초
4       2×6   ax ay az gx gy gz   int16 raw count
모두 little-endian
```

헤더에 FS range와 ODR을 넣는 이유: 단위 변환에 꼭 필요한 정보를 로그 **안에** 두어야, 6개월 뒤 누가 파일만 받아도 올바르게 해석할 수 있다. 센서 설정을 바꾼 FW 빌드가 섞여 있어도 안전하다.

### 4.2 C 로거 — 펌웨어가 쓰는 것처럼 로그 만들기

이 프로그램은 packed 구조체를 `fwrite`해서 위 포맷의 로그를 만든다. flick/shake 제스처, 노이즈, timestamp jitter(±100 µs), 그리고 seed별 FIFO overflow(샘플 손실)를 흉내 낸다.

```c
/* imu_logger.c — 펌웨어가 flash에 쓰는 것과 같은 packed 바이너리 IMU 로그를 흉내 낸다 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ODR_HZ   50
#define N_SAMP   1500              /* 30 s */
#define ACC_FS_G 4                 /* ±4 g   -> 8192 LSB/g   */
#define GYR_FS   500               /* ±500 dps -> 65.536 LSB/dps */
static const double PI = 3.14159265358979323846;

typedef struct __attribute__((packed)) {
    char     magic[4];             /* "IMU1" */
    uint16_t version, odr_hz, acc_fs_g, gyr_fs_dps;
    uint32_t n_records;
} log_header_t;                    /* 16 bytes */

typedef struct __attribute__((packed)) {
    uint32_t timestamp_us;
    int16_t  ax, ay, az, gx, gy, gz;
} imu_rec_t;                       /* 16 bytes */

static uint32_t rng = 1;
static double urand(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; }
static double nrand(void) { double s = 0; for (int k = 0; k < 12; k++) s += urand(); return s - 6.0; }
static int16_t sat16(double v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)lround(v); }

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s out.bin seed\n", argv[0]); return 1; }
    int seed = atoi(argv[2]);
    rng = (uint32_t)seed;
    FILE *f = fopen(argv[1], "wb");
    if (!f) { perror("fopen"); return 1; }
    log_header_t h = { {'I','M','U','1'}, 1, ODR_HZ, ACC_FS_G, GYR_FS, 0 };
    fwrite(&h, sizeof h, 1, f);                       /* n_records는 나중에 채움 */
    const double lsb_g = 32768.0 / ACC_FS_G, lsb_dps = 32768.0 / GYR_FS;
    uint32_t written = 0;
    for (int i = 0; i < N_SAMP; i++) {
        double t = (double)i / ODR_HZ;
        /* FIFO overflow 흉내: seed별로 몇 샘플을 잃는다 */
        if (seed == 1 && ((i >= 700 && i < 705) || i == 1250)) continue;
        if (seed == 3 && (i >= 250 && i < 252)) continue;
        double ax = 0.02 * nrand(), ay = 0.02 * nrand(), az = 1.0 + 0.02 * nrand();
        double gx = 0.8 * nrand(), gy = 0.8 * nrand(), gz = 0.8 * nrand();
        if (t >= 8.0 && t < 10.0) {                   /* flick: 3 Hz, 1.5 g */
            double s = sin(2 * PI * 3.0 * (t - 8.0));
            ax += 1.5 * s; gy += 200.0 * s;
        }
        if (t >= 20.0 && t < 22.0) {                  /* shake: 5 Hz, 4.5 g -> 포화 */
            double s = sin(2 * PI * 5.0 * (t - 20.0));
            ay += 4.5 * s; gz += 300.0 * s;
        }
        imu_rec_t r;
        r.timestamp_us = (uint32_t)(1000 + i * 20000 + (int)(200 * (urand() - 0.5)));
        r.ax = sat16(ax * lsb_g);  r.ay = sat16(ay * lsb_g);  r.az = sat16(az * lsb_g);
        r.gx = sat16(gx * lsb_dps); r.gy = sat16(gy * lsb_dps); r.gz = sat16(gz * lsb_dps);
        fwrite(&r, sizeof r, 1, f);
        written++;
    }
    h.n_records = written;
    fseek(f, 0, SEEK_SET);
    fwrite(&h, sizeof h, 1, f);
    fclose(f);
    printf("%s: header %zu B + %u records x %zu B = %ld B\n", argv[1], sizeof h,
           written, sizeof(imu_rec_t), (long)(sizeof h + written * sizeof(imu_rec_t)));
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 imu_logger.c -o imu_logger -lm
./imu_logger imu_s1.bin 1 && ./imu_logger imu_s2.bin 2 && ./imu_logger imu_s3.bin 3
xxd imu_s1.bin | head -3
```

```text
imu_s1.bin: header 16 B + 1494 records x 16 B = 23920 B
imu_s2.bin: header 16 B + 1500 records x 16 B = 24016 B
imu_s3.bin: header 16 B + 1498 records x 16 B = 23984 B
00000000: 494d 5531 0100 3200 0400 f401 d605 0000  IMU1..2.........
00000010: 0a04 0000 4bff d8ff 1d20 3600 bfff 1500  ....K.... 6.....
00000020: c051 0000 6600 6b00 251f 5500 b8ff 6300  .Q..f.k.%.U...c.
```

출력에서 볼 것: 경고 0개로 컴파일된다. hexdump를 손으로 읽어 보자. `3200` = 0x0032 = 50 (ODR), `f401` = 0x01F4 = 500 (dps), `d605 0000` = 0x05D6 = 1494 (레코드 수). 첫 레코드 timestamp `0a04 0000` = 0x040A = 1034 µs, az `1d20` = 0x201D = 8221 count ≈ 1.004 g(중력). 바이트가 뒤집혀 보이는 것이 little-endian이다.

### 4.3 numpy structured dtype + `np.fromfile`

structured dtype은 C 구조체를 numpy에 설명하는 방법이다. 필드 이름, 타입, 바이트 순서를 적으면 파일 전체를 **한 번에** 구조체 배열로 읽는다. 필드 하나(`rec["ax"]`)를 꺼내면 stride가 16 B인 int16 view가 된다.

타입 문자 읽는 법: `<`는 little-endian, `u`/`i`는 unsigned/signed, 숫자는 바이트 수. `"<u4"` = `uint32_t` LE, `"<i2"` = `int16_t` LE, `"S4"` = 4바이트 문자열.

아래 `imu_io.py`는 이후 모든 예제가 import하는 작은 모듈이다(뒤 절에서 함수를 몇 개 더 붙인다).

```python
# imu_io.py (1/3)
import numpy as np
import pandas as pd

# C의 log_header_t / imu_rec_t와 1:1로 대응하는 structured dtype ('<' = little-endian)
HDR_DT = np.dtype([("magic", "S4"), ("version", "<u2"), ("odr_hz", "<u2"),
                   ("acc_fs_g", "<u2"), ("gyr_fs_dps", "<u2"), ("n_records", "<u4")])
REC_DT = np.dtype([("timestamp_us", "<u4"),
                   ("ax", "<i2"), ("ay", "<i2"), ("az", "<i2"),
                   ("gx", "<i2"), ("gy", "<i2"), ("gz", "<i2")])
assert HDR_DT.itemsize == 16 and REC_DT.itemsize == 16   # C sizeof와 같아야 한다

def load_log(path):
    hdr = np.fromfile(path, dtype=HDR_DT, count=1)[0]
    if hdr["magic"] != b"IMU1":
        raise ValueError(f"bad magic {hdr['magic']!r}")
    rec = np.fromfile(path, dtype=REC_DT, offset=HDR_DT.itemsize)
    if len(rec) != hdr["n_records"]:
        raise ValueError("truncated log")
    return hdr, rec
```

이 코드는 structured dtype과 `struct` 모듈 두 방식으로 같은 레코드를 읽고, endianness와 alignment 실수를 일부러 재현한다.

```python
import struct
import numpy as np
from imu_io import HDR_DT, REC_DT, load_log

hdr, rec = load_log("imu_s1.bin")
print("header:", hdr)
print("records:", rec.shape, rec.dtype.itemsize, "B each")
print("first 2:", rec[:2])
print("ax column:", rec["ax"][:5], rec["ax"].dtype)

# 같은 레코드를 struct로 한 개씩 읽기 (포맷 문자열 = C 구조체 기술)
FMT = "<I6h"                                   # uint32 + int16×6
print("struct.calcsize:", struct.calcsize(FMT))
with open("imu_s1.bin", "rb") as f:
    f.seek(16)
    print("struct first  :", struct.unpack(FMT, f.read(16)))

# endianness를 틀리면? 에러 없이 '그럴듯한 쓰레기'가 나온다
bad = np.fromfile("imu_s1.bin", dtype=REC_DT.newbyteorder(">"), offset=16, count=1)
print("big-endian로 잘못 읽음:", bad)

# 정렬(alignment): C의 {uint8 seq; uint32 ts; int16 ax,ay,az;}
fields = [("seq", "u1"), ("ts", "<u4"), ("ax", "<i2"), ("ay", "<i2"), ("az", "<i2")]
print("packed itemsize :", np.dtype(fields).itemsize)
print("aligned itemsize:", np.dtype(fields, align=True).itemsize,
      "offsets:", [np.dtype(fields, align=True).fields[n][1] for n in ("seq", "ts", "ax")])
```

```text
header: (b'IMU1', 1, 50, 4, 500, 1494)
records: (1494,) 16 B each
first 2: [( 1034, -181, -40, 8221, 54, -65, 21)
 (20928,  102, 107, 7973, 85, -72, 99)]
ax column: [-181  102 -186 -141   82] int16
struct.calcsize: 16
struct first  : (1034, -181, -40, 8221, 54, -65, 21)
big-endian로 잘못 읽음: [(168034304, 19455, -9985, 7456, 13824, -16385, 5376)]
packed itemsize : 11
aligned itemsize: 16 offsets: [0, 4, 8]
```

출력에서 볼 것: 두 방식의 첫 레코드가 `(1034, -181, …)`로 같다. `struct`는 레코드 하나씩 파이썬 튜플을 만들므로 큰 파일에서 느리다(§3.1의 루프 문제). 헤더처럼 한 번만 읽는 것은 `struct`, 수백만 레코드는 `np.fromfile`이 맞다.

| 방법 | 장점 | 단점 | 쓰는 곳 |
|---|---|---|---|
| `struct.unpack` | 표준 라이브러리, 가변 길이 파싱 쉬움 | 레코드마다 파이썬 객체 → 느림 | 헤더, TLV 패킷, 가변 길이 레코드 |
| `np.fromfile` + structured dtype | 한 번에 전체, 열 단위 view | 고정 크기 레코드만 | 대용량 샘플 로그 |
| `np.frombuffer(bytes, dtype)` | 메모리 버퍼(BLE 패킷, zip 안의 파일)에서 바로 | 같음 | 네트워크·압축 데이터 |

### 4.4 endianness — 에러가 안 나서 더 위험하다

위 출력의 `168034304`를 손으로 확인해 보자. 파일 바이트는 `0a 04 00 00`이다.

```
little-endian (<): 0x0000040A = 1034        ← 올바름
big-endian    (>): 0x0A040000 = 168,034,304 ← 쓰레기지만 '숫자'이긴 함
```

말로 하면, 바이트 순서를 틀려도 파서는 아무 불평 없이 값을 낸다. Cortex-M, x86, Apple Silicon은 모두 little-endian이라 평소엔 문제가 없지만, 네트워크 바이트 순서(big-endian)로 보내는 프로토콜이나 일부 DSP/센서 레지스터 덤프에서 터진다. 그래서 dtype 문자열에 **항상 `<` 또는 `>`를 명시**한다(`"i2"`만 쓰면 "이 PC의 순서"라서 이식성이 없다).

### 4.5 alignment와 padding — `__attribute__((packed))`

C 컴파일러는 멤버를 자기 크기의 배수 주소에 놓으려고 padding을 넣는다. 로그 레코드를 `fwrite(&r, sizeof r, …)`로 그대로 쓰면 padding 바이트까지 파일에 들어간다. 이 코드는 같은 필드 구성의 packed/비packed 레이아웃을 비교한다.

```c
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

typedef struct { uint8_t seq; uint32_t ts; int16_t ax, ay, az; } rec_natural_t;
typedef struct __attribute__((packed)) { uint8_t seq; uint32_t ts; int16_t ax, ay, az; } rec_packed_t;
typedef struct { uint32_t ts; int16_t ax, ay, az, gx, gy, gz; } imu_rec_natural_t;

int main(void) {
    printf("rec_natural_t : sizeof=%2zu  offsetof(ts)=%zu offsetof(ax)=%zu\n",
           sizeof(rec_natural_t), offsetof(rec_natural_t, ts), offsetof(rec_natural_t, ax));
    printf("rec_packed_t  : sizeof=%2zu  offsetof(ts)=%zu offsetof(ax)=%zu\n",
           sizeof(rec_packed_t), offsetof(rec_packed_t, ts), offsetof(rec_packed_t, ax));
    printf("imu_rec (no packed) sizeof=%zu  <- 4+6x2=16, 우연히 패딩 없음\n",
           sizeof(imu_rec_natural_t));
    return 0;
}
```

```text
rec_natural_t : sizeof=16  offsetof(ts)=4 offsetof(ax)=8
rec_packed_t  : sizeof=11  offsetof(ts)=1 offsetof(ax)=5
imu_rec (no packed) sizeof=16  <- 4+6x2=16, 우연히 패딩 없음
```

```
rec_natural_t (16 B):  [seq][pad][pad][pad][ ts ts ts ts ][ax ax][ay ay][az az][pad pad]
                        0    1    2    3    4           8     10    12    14
rec_packed_t  (11 B):  [seq][ ts ts ts ts ][ax ax][ay ay][az az]
                        0    1            5     7     9
```

출력에서 볼 것: 비packed는 `ts`를 4의 배수(오프셋 4)로 밀고, 구조체 전체를 4의 배수(16)로 맞추려고 끝에도 2바이트를 붙인다. numpy에서는 `np.dtype(fields, align=True)`가 C 컴파일러의 규칙을 흉내 내고(16 B, 오프셋 `[0, 4, 8]`), 기본값은 packed(11 B)다. §4.3 예제의 마지막 두 줄이 C 출력과 정확히 같다.

규칙: **로그 포맷은 packed로 정의하고 Python 쪽에 `assert itemsize == sizeof`를 둔다.** 반대로 packed 구조체의 `uint32_t` 필드를 Cortex-M0/M0+에서 직접 읽으면 unaligned access로 HardFault가 날 수 있다(M3 이상은 LDR의 unaligned 접근을 하드웨어가 처리하지만 느리다). 그래서 이 노트의 로그처럼 필드 순서를 "큰 것 먼저"로 두어 packed여도 자연 정렬이 되게 하는 것이 가장 좋다.

### 4.6 raw count → 물리 단위

16-bit 센서는 full-scale range(FS) ±R을 −32768~32767에 대응시킨다.

```
sensitivity [LSB/unit] = 32768 / R
value [unit] = raw / (32768 / R) = raw × R / 32768

±4 g    : 32768 / 4   = 8192 LSB/g        예) raw 16384 → 2.000 g,  raw −12288 → −1.5 g
±500 dps: 32768 / 500 = 65.536 LSB/dps    예) raw 6554 → 100.006 dps
```

말로 하면, "FS를 32768칸으로 나눈 것이 한 칸의 크기"다. 데이터시트는 보통 이 값을 반올림해서 적는다(예: ±500 dps에서 65.5 LSB/dps, ±4 g에서 0.122 mg/LSB). 실제 칩은 공장 오차가 있어 calibration(G3)으로 보정한다. 양 끝값 +32767 → 3.99988 g, −32768 → −4.0 g에 붙은 샘플은 실제 값이 더 컸다는 뜻, 즉 **포화(saturation)**다.

이 코드는 헤더의 FS 정보로 단위를 바꾸고, 정지 구간으로 변환이 맞는지 확인하고, 포화 샘플을 센다.

```python
import numpy as np
from imu_io import load_log

hdr, rec = load_log("imu_s1.bin")
acc_lsb = 32768 / hdr["acc_fs_g"]        # ±4 g   → 8192 LSB/g
gyr_lsb = 32768 / hdr["gyr_fs_dps"]      # ±500 dps → 65.536 LSB/dps
print("LSB/g =", acc_lsb, " LSB/dps =", gyr_lsb)

raw_acc = np.stack([rec["ax"], rec["ay"], rec["az"]], axis=1)   # (N, 3) int16
raw_gyr = np.stack([rec["gx"], rec["gy"], rec["gz"]], axis=1)
acc_g   = raw_acc.astype(np.float32) / np.float32(acc_lsb)
gyr_dps = raw_gyr.astype(np.float32) / np.float32(gyr_lsb)
print("acc shape/dtype:", acc_g.shape, acc_g.dtype)

print("정지 구간(0~2 s) 평균 [g]  :", acc_g[:100].mean(axis=0).round(3))
print("정지 구간 |a| 평균 [g]      :", np.linalg.norm(acc_g[:100], axis=1).mean().round(3))
print("정지 구간 gyro std [dps]    :", gyr_dps[:100].std(axis=0).round(2))

# 포화(saturation) 검사: full-scale 끝값에 붙은 샘플 수
sat = (raw_acc >= 32767) | (raw_acc <= -32768)
print("포화 샘플 수 (ax, ay, az)   :", sat.sum(axis=0))
t_s = (rec["timestamp_us"] - rec["timestamp_us"][0]) / 1e6
print("포화 시각 범위 [s]          :", t_s[sat.any(axis=1)].min().round(2),
      "~", t_s[sat.any(axis=1)].max().round(2))
```

```text
LSB/g = 8192.0  LSB/dps = 65.536
acc shape/dtype: (1494, 3) float32
정지 구간(0~2 s) 평균 [g]  : [ 0.003 -0.004  0.999]
정지 구간 |a| 평균 [g]      : 0.999
정지 구간 gyro std [dps]    : [0.84 0.83 0.81]
포화 샘플 수 (ax, ay, az)   : [ 0 40  0]
포화 시각 범위 [s]          : 20.04 ~ 21.96
```

출력에서 볼 것: 가만히 있을 때 `|a| ≈ 1 g`이면 단위 변환이 맞다는 가장 강력한 증거다. 0.5나 2.0이 나오면 FS 설정을 잘못 읽은 것이다. 포화 40개는 전부 shake 구간(20~22 s)의 ay에 있다.

로그를 그림으로 보면 아래와 같다(6~24 s 구간, s1의 실제 샘플 895개로 그린 것).

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
  <rect x="116.7" y="20" width="66.7" height="220" fill="#e08a3c" fill-opacity="0.15"/> <rect x="516.7" y="20" width="66.7" height="220" fill="#e08a3c" fill-opacity="0.15"/>
  <text x="150" y="44" font-size="12" text-anchor="middle">flick</text> <text x="550" y="16" font-size="12" text-anchor="middle">shake (ay 포화)</text>
  <line x1="50" y1="32.2" x2="650" y2="32.2" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/> <line x1="50" y1="227.8" x2="650" y2="227.8" stroke="#888" stroke-width="1" stroke-dasharray="4 3"/>
  <text x="646" y="28" font-size="12" text-anchor="end">+FS 3.9999 g</text> <line x1="50" y1="20" x2="50" y2="240" stroke="currentColor" stroke-width="1"/>
  <line x1="50" y1="240" x2="650" y2="240" stroke="currentColor" stroke-width="1"/> <line x1="50" y1="130" x2="650" y2="130" stroke="currentColor" stroke-width="0.4"/> <text x="44" y="36" font-size="12" text-anchor="end">4</text>
  <text x="44" y="85" font-size="12" text-anchor="end">2</text> <text x="44" y="134" font-size="12" text-anchor="end">0</text> <text x="44" y="183" font-size="12" text-anchor="end">-2</text>
  <text x="44" y="232" font-size="12" text-anchor="end">-4</text> <text x="14" y="134" font-size="12" text-anchor="middle">g</text> <text x="50" y="256" font-size="12" text-anchor="middle">6</text>
  <text x="150" y="256" font-size="12" text-anchor="middle">9</text> <text x="250" y="256" font-size="12" text-anchor="middle">12</text> <text x="350" y="256" font-size="12" text-anchor="middle">15</text>
  <text x="450" y="256" font-size="12" text-anchor="middle">18</text> <text x="550" y="256" font-size="12" text-anchor="middle">21</text> <text x="650" y="256" font-size="12" text-anchor="middle">24</text>
  <text x="350" y="276" font-size="12" text-anchor="middle">time [s]</text>
  <polyline points="50.0,129.6 50.7,130.0 51.4,129.7 52.0,129.9 52.7,129.8 53.4,129.5 54.0,129.6 54.7,129.8 55.4,129.7 56.0,129.8 56.7,129.7 57.4,130.3 58.0,129.3 58.7,130.1 59.4,129.8 60.0,129.9 60.7,130.0 61.4,130.9 62.0,129.3 62.7,130.3 63.4,129.5 64.0,131.0 64.7,130.6 65.4,130.5 66.0,129.2 66.7,129.0 67.4,130.8 68.0,129.6 68.7,130.8 69.4,129.7 70.0,129.8 70.7,130.9 71.4,129.9 72.0,129.8 72.7,129.2 73.4,129.8 74.0,130.3 74.7,129.9 75.4,131.3 76.0,129.9 76.7,129.8 77.4,129.7 78.0,130.3 78.7,130.0 79.4,129.5 80.0,130.1 80.7,131.1 81.4,130.2 82.0,129.8 82.7,130.0 83.4,129.6 84.0,130.5 84.7,130.1 85.4,130.9 86.0,130.9 86.7,130.1 87.4,129.4 88.0,129.5 88.7,130.7 89.4,130.8 90.0,130.4 90.7,130.7 91.4,130.2 92.0,130.2 92.7,130.7 93.4,130.3 94.0,130.4 94.7,130.5 95.4,129.8 96.0,130.4 96.7,130.1 97.4,130.1 98.0,129.4 98.7,130.6 99.4,129.7 100.0,130.1 100.7,130.0 101.4,130.2 102.0,130.2 102.7,129.9 103.4,130.3 104.0,130.2 104.7,130.5 105.4,129.7 106.0,130.1 106.7,129.8 107.4,130.2 108.0,130.0 108.7,130.3 109.4,130.3 110.0,130.0 110.7,130.6 111.4,129.2 112.0,130.0 112.7,129.9 113.4,130.1 114.0,129.6 114.7,130.1 115.4,130.2 116.0,130.2 116.7,129.1 117.4,116.1 118.0,105.6 118.7,96.2 119.4,93.7 120.0,94.6 120.7,101.4 121.4,112.9 122.0,125.5 122.7,138.9 123.4,151.5 124.0,160.5 124.7,166.6 125.4,166.3 126.0,160.9 126.7,150.3 127.4,138.6 128.0,125.8 128.7,112.7 129.4,102.5 130.0,95.8 130.7,93.1 131.4,96.4 132.0,104.7 132.7,116.3 133.4,129.9 134.0,143.8 134.7,155.0 135.4,163.6 136.0,165.8 136.7,165.4 137.4,158.0 138.0,147.6 138.7,134.3 139.4,121.7 140.0,107.8 140.7,99.5 141.4,93.9 142.0,93.9 142.7,99.3 143.4,109.4 144.0,120.2 144.7,134.3 145.4,147.8 146.0,158.5 146.7,165.2 147.4,167.0 148.0,162.8 148.7,154.8 149.4,142.7 150.0,130.1 150.7,116.5 151.4,105.0 152.0,96.9 152.7,93.5 153.4,95.9 154.0,101.9 154.7,112.1 155.4,125.0 156.0,139.4 156.7,151.6 157.4,160.6 158.0,165.5 158.7,166.2 159.4,161.1 160.0,151.9 160.7,139.4 161.4,126.1 162.0,112.3 162.7,101.4 163.4,94.7 164.0,94.1 164.7,96.5 165.4,104.7 166.0,116.1 166.7,130.1 167.4,144.0 168.0,155.6 168.7,163.5 169.4,166.6 170.0,165.3 170.7,157.9 171.4,147.7 172.0,135.2 172.7,122.0 173.4,108.6 174.0,98.9 174.7,93.5 175.4,93.6 176.0,99.2 176.7,108.2 177.4,120.9 178.0,135.6 178.7,147.5 179.4,158.6 180.0,164.5 180.7,166.7 181.4,163.6 182.0,155.0 182.7,143.3 183.4,129.9 184.0,130.1 184.7,130.5 185.4,130.0 186.0,129.5 186.7,130.0 187.4,129.5 188.0,129.8 188.7,130.0 189.4,129.7 190.0,129.8 190.7,130.2 191.4,129.5 192.0,129.1 192.7,130.6 193.4,130.9 194.0,129.9 194.7,130.7 195.4,130.4 196.0,129.8 196.7,129.4 197.4,130.1 198.0,130.3 198.7,129.6 199.4,130.3 200.0,130.7 200.7,130.3 201.4,129.3 202.0,130.1 202.7,129.8 203.4,130.3 204.0,129.6 204.7,130.0 205.4,130.2 206.0,130.6 206.7,130.1 207.4,130.1 208.0,130.3 208.7,130.5 209.4,130.3 210.0,129.8 210.7,129.2 211.4,129.6 212.0,130.2 212.7,130.6 213.4,129.6 214.0,129.7 214.7,130.4 215.4,129.3 216.0,129.6 216.7,129.8 217.4,129.8 218.0,130.7 218.7,129.1 219.4,130.0 220.0,129.8 220.7,130.2 221.4,130.2 222.0,130.0 222.7,129.8 223.4,130.6 224.0,130.2 224.7,130.4 225.4,129.8 226.0,130.1 226.7,129.9 227.4,131.2 228.0,129.3 228.7,130.3 229.4,130.6 230.0,129.9 230.7,130.3 231.4,130.6 232.0,130.3 232.7,130.1 233.4,129.9 234.0,129.9 234.7,129.8 235.4,129.9 236.0,129.4 236.7,130.1 237.4,129.7 238.0,129.1 238.7,129.4 239.4,130.8 240.0,130.4 240.7,130.4 241.4,130.1 242.0,130.4 242.7,129.2 243.4,130.0 244.0,129.8 244.7,129.9 245.4,129.8 246.0,130.1 246.7,129.9 247.4,129.5 248.0,129.7 248.7,129.6 249.4,130.7 250.0,129.6 250.7,129.6 251.4,129.7 252.0,130.2 252.7,129.8 253.4,130.2 254.0,130.5 254.7,130.0 255.4,130.4 256.0,129.9 256.7,129.6 257.4,129.7 258.0,129.8 258.7,129.9 259.4,130.1 260.0,129.7 260.7,129.6 261.4,130.2 262.0,129.9 262.7,130.2 263.4,130.3 264.0,129.0 264.7,130.3 265.4,129.8 266.0,130.4 266.7,130.4 267.4,130.4 268.0,130.6 268.7,129.9 269.4,129.5 270.0,130.7 270.7,130.2 271.4,130.1 272.0,129.7 272.7,130.7 273.4,130.7 274.0,130.3 274.7,130.1 275.4,130.0 276.0,130.6 276.7,130.1 277.4,131.4 278.0,130.2 278.7,130.1 279.4,130.4 280.0,130.3 280.7,129.6 281.4,130.4 282.0,129.3 282.7,128.6 283.4,129.9 284.0,130.0 284.7,131.3 285.4,130.3 286.0,130.2 286.7,130.0 287.4,130.9 288.0,129.6 288.7,129.9 289.4,130.4 290.0,130.8 290.7,130.5 291.4,130.9 292.0,129.3 292.7,129.6 293.4,130.4 294.0,130.1 294.7,130.3 295.4,130.0 296.0,129.6 296.7,130.1 297.4,130.8 298.0,129.9 298.7,130.8 299.4,130.9 300.0,129.9 300.7,129.7 301.4,130.4 302.0,130.2 302.7,129.9 303.4,129.1 304.0,130.2 304.7,129.4 305.4,130.1 306.0,130.5 306.7,129.6 307.4,129.3 308.0,129.8 308.7,129.5 309.4,130.6 310.0,129.4 310.7,129.2 311.4,130.8 312.0,129.6 312.7,130.6 313.4,129.2 314.0,130.2 314.7,130.1 315.4,130.2 316.0,130.0 320.0,129.6 320.7,129.9 321.4,130.0 322.0,129.9 322.7,130.0 323.4,130.6 324.0,130.1 324.7,129.8 325.4,129.7 326.0,129.9 326.7,130.1 327.4,129.5 328.0,129.9 328.7,130.0 329.4,129.4 330.0,130.0 330.7,129.5 331.4,130.0 332.0,128.7 332.7,129.4 333.4,129.9 334.0,129.5 334.7,130.1 335.4,130.1 336.0,129.9 336.7,130.2 337.4,129.6 338.0,130.5 338.7,130.1 339.4,130.3 340.0,129.5 340.7,130.6 341.4,129.7 342.0,130.1 342.7,129.4 343.4,130.1 344.0,130.8 344.7,129.9 345.4,130.7 346.0,130.0 346.7,129.9 347.4,129.2 348.0,130.7 348.7,130.5 349.4,129.6 350.0,130.0 350.7,129.7 351.4,130.4 352.0,130.5 352.7,129.6 353.4,130.5 354.0,130.4 354.7,130.1 355.4,129.4 356.0,129.0 356.7,128.9 357.4,130.3 358.0,130.2 358.7,129.5 359.4,129.9 360.0,129.4 360.7,129.9 361.4,130.5 362.0,130.8 362.7,129.6 363.4,130.3 364.0,129.4 364.7,130.2 365.4,130.9 366.0,130.5 366.7,130.4 367.4,130.6 368.0,130.2 368.7,129.3 369.4,131.0 370.0,130.0 370.7,129.7 371.4,130.9 372.0,129.6 372.7,130.5 373.4,128.9 374.0,129.4 374.7,130.6 375.4,130.6 376.0,130.2 376.7,130.3 377.4,129.5 378.0,130.5 378.7,129.6 379.4,129.5 380.0,129.9 380.7,129.9 381.4,130.1 382.0,129.2 382.7,130.1 383.4,130.2 384.0,130.8 384.7,129.9 385.4,129.2 386.0,129.8 386.7,130.4 387.4,129.9 388.0,129.8 388.7,130.4 389.4,129.1 390.0,129.8 390.7,129.1 391.4,130.3 392.0,130.9 392.7,129.9 393.4,129.9 394.0,130.1 394.7,130.0 395.4,131.2 396.0,129.8 396.7,129.2 397.4,129.8 398.0,130.8 398.7,130.2 399.4,129.3 400.0,130.2 400.7,130.2 401.4,130.1 402.0,129.9 402.7,130.5 403.4,131.0 404.0,129.9 404.7,129.9 405.4,129.6 406.0,130.1 406.7,130.5 407.4,130.1 408.0,130.4 408.7,129.9 409.4,128.7 410.0,129.4 410.7,129.8 411.4,129.0 412.0,130.5 412.7,129.2 413.4,130.0 414.0,130.2 414.7,129.6 415.4,130.4 416.0,129.0 416.7,130.4 417.4,130.3 418.0,129.8 418.7,129.6 419.4,129.8 420.0,129.9 420.7,129.7 421.4,129.5 422.0,129.9 422.7,129.9 423.4,129.5 424.0,131.2 424.7,129.7 425.4,130.0 426.0,130.5 426.7,130.1 427.4,130.2 428.0,129.5 428.7,129.6 429.4,129.4 430.0,129.9 430.7,130.3 431.4,129.6 432.0,129.4 432.7,130.4 433.4,129.5 434.0,129.5 434.7,129.7 435.4,129.5 436.0,129.3 436.7,130.8 437.4,129.4 438.0,129.8 438.7,130.3 439.4,129.8 440.0,130.6 440.7,130.3 441.4,129.8 442.0,130.5 442.7,130.0 443.4,129.7 444.0,129.6 444.7,131.1 445.4,129.2 446.0,130.5 446.7,130.2 447.4,129.5 448.0,129.5 448.7,130.3 449.4,129.8 450.0,129.9 450.7,130.5 451.4,129.7 452.0,130.3 452.7,130.1 453.4,129.2 454.0,130.3 454.7,130.5 455.4,130.2 456.0,129.8 456.7,129.7 457.4,130.0 458.0,130.7 458.7,130.4 459.4,130.8 460.0,129.8 460.7,130.5 461.4,130.1 462.0,130.2 462.7,129.7 463.4,129.9 464.0,131.3 464.7,130.9 465.4,130.3 466.0,129.8 466.7,130.8 467.4,129.6 468.0,130.8 468.7,130.5 469.4,130.5 470.0,129.6 470.7,130.3 471.4,130.2 472.0,129.4 472.7,129.6 473.4,129.6 474.0,130.0 474.7,130.6 475.4,130.2 476.0,130.3 476.7,129.1 477.4,130.0 478.0,130.2 478.7,130.2 479.4,129.7 480.0,128.9 480.7,129.6 481.4,129.4 482.0,130.3 482.7,130.7 483.4,130.9 484.0,129.6 484.7,131.1 485.4,129.7 486.0,130.1 486.7,129.2 487.4,129.7 488.0,129.9 488.7,130.7 489.4,129.7 490.0,129.0 490.7,131.4 491.4,130.7 492.0,130.0 492.7,129.8 493.4,130.0 494.0,129.4 494.7,129.8 495.4,130.7 496.0,129.8 496.7,130.1 497.4,129.5 498.0,129.8 498.7,129.9 499.4,130.3 500.0,130.0 500.7,129.6 501.4,131.0 502.0,130.4 502.7,129.5 503.4,130.3 504.0,130.9 504.7,129.3 505.4,130.6 506.0,130.5 506.7,130.2 507.4,129.1 508.0,130.7 508.7,130.4 509.4,129.6 510.0,130.5 510.7,129.9 511.4,130.4 512.0,129.4 512.7,130.9 513.4,129.3 514.0,130.3 514.7,129.6 515.4,129.6 516.0,129.6 516.7,130.6 517.4,130.1 518.0,130.1 518.7,130.2 519.4,129.9 520.0,130.0 520.7,130.6 521.4,131.0 522.0,130.5 522.7,130.1 523.4,129.6 524.0,130.0 524.7,129.9 525.4,129.6 526.0,129.7 526.7,129.8 527.4,130.2 528.0,130.5 528.7,130.4 529.4,129.5 530.0,129.7 530.7,129.7 531.4,130.1 532.0,129.6 532.7,130.1 533.4,130.2 534.0,130.5 534.7,130.0 535.4,129.6 536.0,130.0 536.7,129.7 537.4,129.5 538.0,128.7 538.7,129.8 539.4,130.3 540.0,130.1 540.7,130.4 541.4,130.3 542.0,129.2 542.7,131.2 543.4,130.1 544.0,129.9 544.7,130.1 545.4,129.1 546.0,130.5 546.7,129.9 547.4,129.9 548.0,130.0 548.7,130.3 549.4,128.9 550.0,130.3 550.7,130.0 551.4,130.8 552.0,130.0 552.7,130.2 553.4,130.3 554.0,130.0 554.7,129.6 555.4,130.7 556.0,129.6 556.7,130.2 557.4,129.9 558.0,129.2 558.7,129.7 559.4,130.3 560.0,129.7 560.7,129.5 561.4,130.1 562.0,129.1 562.7,130.0 563.4,130.5 564.0,129.7 564.7,130.6 565.4,129.8 566.0,129.8 566.7,129.7 567.4,130.1 568.0,130.3 568.7,129.7 569.4,130.0 570.0,130.4 570.7,130.1 571.4,129.3 572.0,129.6 572.7,130.1 573.4,129.9 574.0,129.8 574.7,130.6 575.4,130.5 576.0,129.8 576.7,130.4 577.4,130.1 578.0,129.9 578.7,130.3 579.4,130.2 580.0,129.9 580.7,129.9 581.4,130.6 582.0,129.7 582.7,129.9 583.4,129.6 584.0,130.1 584.7,129.9 585.4,130.2 586.0,130.0 586.7,130.0 587.4,130.6 588.0,129.3 588.7,130.4 589.4,129.8 590.0,130.3 590.7,129.7 591.4,130.1 592.0,130.0 592.7,130.8 593.4,130.6 594.0,130.2 594.7,130.6 595.4,129.4 596.0,129.6 596.7,130.0 597.4,130.0 598.0,130.2 598.7,129.3 599.4,130.1 600.0,129.4 600.7,129.8 601.4,130.5 602.0,130.2 602.7,130.5 603.4,129.8 604.0,130.1 604.7,129.7 605.4,129.3 606.0,129.9 606.7,130.6 607.4,129.5 608.0,129.9 608.7,130.3 609.4,129.4 610.0,130.0 610.7,130.1 611.4,130.0 612.0,129.7 612.7,131.0 613.4,130.2 614.0,130.0 614.7,130.3 615.4,130.4 616.0,129.4 616.7,129.6 617.4,129.5 618.0,130.1 618.7,129.7 619.4,131.0 620.0,130.3 620.7,131.1 621.4,130.1 622.0,128.7 622.7,129.5 623.4,129.5 624.0,128.9 624.7,129.3 625.4,129.9 626.0,129.6 626.7,130.2 627.4,129.2 628.0,131.2 628.7,130.0 629.4,129.8 630.0,129.8 630.7,130.3 631.4,129.5 632.0,129.6 632.7,130.2 633.4,130.4 634.0,129.2 634.7,129.8 635.4,130.3 636.0,130.3 636.7,129.4 637.4,129.8 638.0,130.3 638.7,130.3 639.4,129.7 640.0,130.5 640.7,130.2 641.4,130.5 642.0,130.2 642.7,129.7 643.4,130.8 644.0,129.8 644.7,130.0 645.4,130.1 646.0,130.3 646.7,130.7 647.4,130.4 648.0,130.0 648.7,130.7 649.4,130.6" fill="none" stroke="#4a7bd0" stroke-width="1"/>
  <polyline points="50.0,129.7 50.7,130.5 51.4,129.9 52.0,130.2 52.7,130.3 53.4,130.3 54.0,130.1 54.7,130.2 55.4,129.2 56.0,130.3 56.7,130.0 57.4,129.8 58.0,129.6 58.7,130.6 59.4,130.4 60.0,130.5 60.7,130.6 61.4,129.2 62.0,130.0 62.7,129.6 63.4,129.9 64.0,130.3 64.7,130.2 65.4,130.1 66.0,130.1 66.7,130.6 67.4,129.7 68.0,129.0 68.7,130.1 69.4,130.5 70.0,130.2 70.7,130.1 71.4,129.8 72.0,130.1 72.7,129.7 73.4,130.1 74.0,129.8 74.7,130.1 75.4,130.8 76.0,129.8 76.7,130.4 77.4,129.6 78.0,130.5 78.7,129.9 79.4,129.9 80.0,129.8 80.7,129.4 81.4,130.2 82.0,129.6 82.7,129.3 83.4,130.0 84.0,129.5 84.7,128.7 85.4,129.5 86.0,130.1 86.7,129.8 87.4,129.8 88.0,129.3 88.7,129.0 89.4,129.4 90.0,129.8 90.7,130.6 91.4,129.3 92.0,129.9 92.7,130.1 93.4,129.2 94.0,130.5 94.7,130.0 95.4,129.9 96.0,129.8 96.7,130.5 97.4,130.0 98.0,130.4 98.7,130.0 99.4,130.9 100.0,129.9 100.7,130.4 101.4,129.5 102.0,129.7 102.7,130.8 103.4,130.4 104.0,130.1 104.7,129.8 105.4,130.2 106.0,130.0 106.7,130.5 107.4,130.3 108.0,130.6 108.7,129.0 109.4,130.1 110.0,131.0 110.7,129.6 111.4,129.7 112.0,130.1 112.7,129.1 113.4,129.7 114.0,130.5 114.7,130.3 115.4,130.6 116.0,129.8 116.7,130.1 117.4,130.0 118.0,130.7 118.7,129.2 119.4,130.0 120.0,129.8 120.7,128.9 121.4,129.9 122.0,129.6 122.7,130.0 123.4,129.6 124.0,129.1 124.7,130.0 125.4,129.4 126.0,130.2 126.7,130.2 127.4,130.3 128.0,130.1 128.7,129.6 129.4,131.0 130.0,129.0 130.7,130.4 131.4,130.2 132.0,129.4 132.7,130.4 133.4,130.2 134.0,130.3 134.7,130.4 135.4,130.5 136.0,130.5 136.7,130.0 137.4,128.7 138.0,129.9 138.7,129.0 139.4,129.6 140.0,130.7 140.7,129.9 141.4,130.3 142.0,129.8 142.7,130.0 143.4,129.8 144.0,129.7 144.7,129.8 145.4,129.9 146.0,130.3 146.7,129.7 147.4,130.3 148.0,130.2 148.7,129.6 149.4,130.2 150.0,129.9 150.7,130.3 151.4,129.9 152.0,130.3 152.7,130.6 153.4,129.6 154.0,130.3 154.7,129.8 155.4,130.1 156.0,129.6 156.7,130.3 157.4,129.9 158.0,130.5 158.7,129.7 159.4,130.5 160.0,130.0 160.7,130.0 161.4,129.1 162.0,130.5 162.7,129.9 163.4,129.6 164.0,130.2 164.7,131.2 165.4,130.0 166.0,130.6 166.7,129.7 167.4,130.3 168.0,129.8 168.7,130.8 169.4,130.6 170.0,129.7 170.7,130.5 171.4,130.4 172.0,129.4 172.7,130.0 173.4,129.5 174.0,129.8 174.7,130.5 175.4,129.6 176.0,130.2 176.7,129.7 177.4,130.1 178.0,130.4 178.7,130.7 179.4,130.8 180.0,130.4 180.7,130.6 181.4,130.7 182.0,130.1 182.7,130.3 183.4,129.3 184.0,129.9 184.7,129.3 185.4,129.5 186.0,130.3 186.7,129.4 187.4,130.0 188.0,129.9 188.7,129.4 189.4,130.0 190.0,129.4 190.7,130.0 191.4,130.4 192.0,130.4 192.7,130.7 193.4,129.9 194.0,129.5 194.7,130.7 195.4,131.0 196.0,129.8 196.7,130.4 197.4,129.9 198.0,128.9 198.7,128.8 199.4,130.3 200.0,130.3 200.7,129.6 201.4,130.6 202.0,130.1 202.7,130.2 203.4,129.9 204.0,130.8 204.7,129.4 205.4,130.0 206.0,129.6 206.7,130.0 207.4,129.2 208.0,129.9 208.7,130.5 209.4,129.3 210.0,130.2 210.7,130.8 211.4,130.5 212.0,129.7 212.7,130.5 213.4,129.5 214.0,129.0 214.7,130.2 215.4,129.8 216.0,130.0 216.7,131.1 217.4,130.3 218.0,130.5 218.7,129.2 219.4,129.8 220.0,129.6 220.7,130.3 221.4,130.6 222.0,130.0 222.7,129.5 223.4,129.9 224.0,129.7 224.7,130.0 225.4,130.3 226.0,129.9 226.7,129.9 227.4,129.2 228.0,130.0 228.7,129.7 229.4,129.6 230.0,130.1 230.7,130.9 231.4,130.0 232.0,129.3 232.7,129.5 233.4,130.4 234.0,130.6 234.7,129.1 235.4,129.5 236.0,130.0 236.7,130.8 237.4,130.8 238.0,129.9 238.7,131.0 239.4,129.3 240.0,129.7 240.7,129.2 241.4,129.8 242.0,129.9 242.7,129.5 243.4,129.0 244.0,129.9 244.7,130.1 245.4,130.0 246.0,129.5 246.7,129.5 247.4,130.1 248.0,130.3 248.7,130.0 249.4,129.9 250.0,129.7 250.7,130.0 251.4,130.2 252.0,131.0 252.7,129.4 253.4,129.3 254.0,129.9 254.7,129.8 255.4,129.7 256.0,130.2 256.7,129.8 257.4,129.9 258.0,130.6 258.7,129.5 259.4,129.9 260.0,130.3 260.7,129.9 261.4,130.6 262.0,130.5 262.7,129.7 263.4,129.7 264.0,130.4 264.7,130.1 265.4,129.2 266.0,129.7 266.7,129.9 267.4,130.0 268.0,129.5 268.7,129.4 269.4,129.4 270.0,130.2 270.7,129.7 271.4,130.4 272.0,129.5 272.7,129.9 273.4,129.3 274.0,129.4 274.7,129.5 275.4,130.4 276.0,130.9 276.7,130.5 277.4,130.3 278.0,130.5 278.7,130.1 279.4,130.3 280.0,129.8 280.7,130.1 281.4,130.7 282.0,129.6 282.7,130.3 283.4,130.6 284.0,129.7 284.7,129.4 285.4,129.5 286.0,130.1 286.7,129.7 287.4,130.5 288.0,130.8 288.7,130.4 289.4,130.6 290.0,130.1 290.7,130.4 291.4,129.5 292.0,129.7 292.7,130.1 293.4,130.7 294.0,130.3 294.7,129.2 295.4,130.4 296.0,130.6 296.7,129.2 297.4,130.3 298.0,130.2 298.7,130.7 299.4,130.0 300.0,129.8 300.7,130.8 301.4,129.4 302.0,129.9 302.7,130.2 303.4,129.8 304.0,130.5 304.7,129.7 305.4,129.8 306.0,129.6 306.7,129.8 307.4,130.0 308.0,130.4 308.7,130.4 309.4,129.7 310.0,130.1 310.7,130.2 311.4,129.9 312.0,129.7 312.7,129.9 313.4,131.2 314.0,129.9 314.7,129.8 315.4,129.8 316.0,130.2 320.0,131.1 320.7,130.2 321.4,130.5 322.0,129.3 322.7,130.3 323.4,129.7 324.0,129.9 324.7,130.4 325.4,130.3 326.0,130.0 326.7,130.4 327.4,130.0 328.0,129.6 328.7,129.7 329.4,129.4 330.0,129.4 330.7,129.9 331.4,129.7 332.0,129.2 332.7,130.4 333.4,129.6 334.0,129.1 334.7,130.1 335.4,130.1 336.0,129.9 336.7,130.6 337.4,129.7 338.0,130.1 338.7,129.8 339.4,129.8 340.0,130.4 340.7,130.1 341.4,129.6 342.0,130.6 342.7,130.4 343.4,130.6 344.0,130.1 344.7,129.7 345.4,129.7 346.0,129.8 346.7,130.2 347.4,130.8 348.0,129.2 348.7,129.6 349.4,130.0 350.0,129.8 350.7,129.8 351.4,129.7 352.0,130.1 352.7,128.9 353.4,130.2 354.0,129.6 354.7,129.7 355.4,129.8 356.0,130.0 356.7,130.1 357.4,129.8 358.0,130.9 358.7,129.6 359.4,130.5 360.0,129.4 360.7,130.0 361.4,129.9 362.0,129.6 362.7,129.5 363.4,130.6 364.0,130.3 364.7,130.0 365.4,130.7 366.0,130.8 366.7,129.9 367.4,129.6 368.0,130.2 368.7,130.1 369.4,130.3 370.0,130.8 370.7,131.2 371.4,130.3 372.0,130.5 372.7,130.2 373.4,129.8 374.0,130.1 374.7,130.4 375.4,129.6 376.0,129.3 376.7,131.1 377.4,129.5 378.0,130.0 378.7,130.4 379.4,130.3 380.0,130.5 380.7,129.7 381.4,129.9 382.0,129.7 382.7,129.5 383.4,130.3 384.0,129.8 384.7,129.5 385.4,130.2 386.0,129.8 386.7,129.3 387.4,129.9 388.0,129.8 388.7,130.6 389.4,130.2 390.0,128.8 390.7,131.3 391.4,129.9 392.0,129.2 392.7,130.1 393.4,130.3 394.0,130.1 394.7,130.7 395.4,131.0 396.0,130.3 396.7,129.3 397.4,129.1 398.0,129.2 398.7,129.7 399.4,129.5 400.0,130.6 400.7,130.6 401.4,130.3 402.0,130.5 402.7,129.8 403.4,129.7 404.0,130.1 404.7,130.9 405.4,129.0 406.0,129.7 406.7,130.0 407.4,129.9 408.0,130.1 408.7,129.6 409.4,130.8 410.0,130.1 410.7,130.1 411.4,130.5 412.0,130.4 412.7,129.6 413.4,130.5 414.0,129.7 414.7,129.8 415.4,131.4 416.0,129.9 416.7,130.3 417.4,129.6 418.0,130.2 418.7,129.4 419.4,130.1 420.0,129.7 420.7,130.2 421.4,130.0 422.0,129.9 422.7,130.3 423.4,129.9 424.0,129.9 424.7,129.8 425.4,130.1 426.0,130.8 426.7,129.4 427.4,128.9 428.0,130.7 428.7,129.3 429.4,129.4 430.0,130.2 430.7,130.4 431.4,129.7 432.0,130.1 432.7,130.9 433.4,129.2 434.0,129.8 434.7,129.9 435.4,129.8 436.0,130.6 436.7,130.3 437.4,129.9 438.0,129.9 438.7,129.9 439.4,130.0 440.0,130.5 440.7,129.8 441.4,129.4 442.0,130.0 442.7,130.6 443.4,129.9 444.0,129.5 444.7,130.6 445.4,130.7 446.0,130.6 446.7,130.1 447.4,130.6 448.0,130.9 448.7,130.7 449.4,130.1 450.0,129.4 450.7,129.1 451.4,130.3 452.0,130.3 452.7,129.7 453.4,130.1 454.0,129.4 454.7,129.3 455.4,129.9 456.0,129.3 456.7,130.3 457.4,129.9 458.0,130.3 458.7,130.9 459.4,130.5 460.0,129.4 460.7,129.9 461.4,128.9 462.0,129.9 462.7,129.8 463.4,129.8 464.0,129.3 464.7,129.8 465.4,129.7 466.0,129.7 466.7,129.4 467.4,130.0 468.0,129.3 468.7,129.8 469.4,130.0 470.0,129.8 470.7,131.7 471.4,129.5 472.0,130.2 472.7,129.0 473.4,130.2 474.0,130.0 474.7,130.2 475.4,130.9 476.0,129.7 476.7,128.9 477.4,129.0 478.0,129.1 478.7,129.5 479.4,129.2 480.0,129.2 480.7,129.7 481.4,129.8 482.0,130.8 482.7,130.1 483.4,129.7 484.0,130.1 484.7,129.8 485.4,130.6 486.0,130.6 486.7,130.1 487.4,129.8 488.0,130.2 488.7,129.9 489.4,129.3 490.0,130.4 490.7,129.2 491.4,129.6 492.0,129.9 492.7,129.4 493.4,130.4 494.0,129.9 494.7,130.5 495.4,129.8 496.0,129.9 496.7,130.6 497.4,129.9 498.0,130.3 498.7,130.1 499.4,130.2 500.0,130.0 500.7,130.2 501.4,129.6 502.0,129.9 502.7,130.2 503.4,130.5 504.0,129.4 504.7,130.3 505.4,130.1 506.0,130.2 506.7,130.1 507.4,130.0 508.0,129.9 508.7,129.9 509.4,129.4 510.0,129.6 510.7,130.0 511.4,130.9 512.0,129.7 512.7,130.3 513.4,129.6 514.0,130.4 514.7,129.9 515.4,130.0 516.0,130.1 516.7,129.7 517.4,65.7 518.0,32.2 518.7,32.2 519.4,65.4 520.0,130.0 520.7,194.3 521.4,227.8 522.0,227.8 522.7,194.3 523.4,129.6 524.0,65.4 524.7,32.2 525.4,32.2 526.0,65.3 526.7,130.5 527.4,194.5 528.0,227.8 528.7,227.8 529.4,195.1 530.0,130.0 530.7,65.7 531.4,32.2 532.0,32.2 532.7,65.4 533.4,130.2 534.0,194.6 534.7,227.8 535.4,227.8 536.0,194.6 536.7,129.4 537.4,65.3 538.0,32.2 538.7,32.2 539.4,65.7 540.0,130.4 540.7,194.6 541.4,227.8 542.0,227.8 542.7,194.7 543.4,129.7 544.0,65.7 544.7,32.2 545.4,32.2 546.0,66.0 546.7,129.6 547.4,194.5 548.0,227.8 548.7,227.8 549.4,194.2 550.0,130.2 550.7,64.9 551.4,32.2 552.0,32.2 552.7,64.8 553.4,130.4 554.0,195.0 554.7,227.8 555.4,227.8 556.0,195.3 556.7,130.7 557.4,65.7 558.0,32.2 558.7,32.2 559.4,65.1 560.0,130.4 560.7,193.8 561.4,227.8 562.0,227.8 562.7,194.8 563.4,130.3 564.0,66.4 564.7,32.2 565.4,32.2 566.0,65.8 566.7,130.7 567.4,194.9 568.0,227.8 568.7,227.8 569.4,194.8 570.0,130.2 570.7,65.0 571.4,32.2 572.0,32.2 572.7,65.6 573.4,129.7 574.0,194.9 574.7,227.8 575.4,227.8 576.0,195.1 576.7,129.9 577.4,65.6 578.0,32.2 578.7,32.2 579.4,65.2 580.0,130.4 580.7,195.1 581.4,227.8 582.0,227.8 582.7,195.0 583.4,129.8 584.0,130.6 584.7,130.4 585.4,130.5 586.0,130.0 586.7,130.2 587.4,129.9 588.0,129.7 588.7,131.3 589.4,129.6 590.0,129.9 590.7,130.5 591.4,129.8 592.0,130.4 592.7,129.1 593.4,130.0 594.0,130.7 594.7,130.3 595.4,130.1 596.0,130.9 596.7,130.3 597.4,129.6 598.0,130.3 598.7,130.0 599.4,130.1 600.0,129.1 600.7,129.9 601.4,130.1 602.0,130.6 602.7,129.7 603.4,129.3 604.0,130.3 604.7,131.1 605.4,129.2 606.0,130.8 606.7,131.0 607.4,129.7 608.0,129.5 608.7,129.9 609.4,130.8 610.0,130.3 610.7,130.5 611.4,129.8 612.0,130.6 612.7,130.1 613.4,130.6 614.0,130.4 614.7,129.6 615.4,130.4 616.0,130.2 616.7,130.7 617.4,130.4 618.0,129.5 618.7,130.7 619.4,130.1 620.0,129.7 620.7,130.2 621.4,130.2 622.0,129.9 622.7,129.6 623.4,129.1 624.0,130.2 624.7,129.8 625.4,129.8 626.0,129.7 626.7,130.2 627.4,130.1 628.0,130.8 628.7,129.9 629.4,130.2 630.0,130.4 630.7,130.4 631.4,129.2 632.0,130.9 632.7,129.6 633.4,129.9 634.0,129.5 634.7,130.3 635.4,129.9 636.0,129.2 636.7,130.7 637.4,129.4 638.0,130.2 638.7,128.7 639.4,130.0 640.0,129.6 640.7,130.5 641.4,129.8 642.0,129.1 642.7,130.2 643.4,130.4 644.0,129.8 644.7,129.4 645.4,130.1 646.0,130.1 646.7,129.9 647.4,129.5 648.0,130.2 648.7,130.8 649.4,130.3" fill="none" stroke="#e08a3c" stroke-width="1"/>
  <polyline points="50.0,105.7 50.7,105.5 51.4,106.6 52.0,105.4 52.7,105.7 53.4,106.1 54.0,105.8 54.7,105.8 55.4,105.8 56.0,106.0 56.7,105.5 57.4,106.0 58.0,105.2 58.7,105.2 59.4,105.6 60.0,105.4 60.7,105.0 61.4,104.6 62.0,105.9 62.7,105.7 63.4,105.9 64.0,105.8 64.7,104.9 65.4,105.3 66.0,105.5 66.7,105.6 67.4,105.6 68.0,105.6 68.7,105.3 69.4,105.9 70.0,105.4 70.7,105.4 71.4,105.9 72.0,105.7 72.7,104.9 73.4,106.0 74.0,106.5 74.7,106.0 75.4,105.4 76.0,105.4 76.7,105.6 77.4,105.3 78.0,104.8 78.7,104.9 79.4,105.5 80.0,105.0 80.7,105.3 81.4,105.1 82.0,104.6 82.7,106.2 83.4,104.7 84.0,105.8 84.7,105.4 85.4,104.6 86.0,105.0 86.7,106.5 87.4,105.6 88.0,105.4 88.7,105.0 89.4,105.7 90.0,104.8 90.7,105.8 91.4,105.7 92.0,105.3 92.7,105.7 93.4,104.2 94.0,105.0 94.7,105.9 95.4,105.8 96.0,105.2 96.7,105.8 97.4,105.4 98.0,105.6 98.7,105.2 99.4,105.5 100.0,105.1 100.7,105.6 101.4,105.6 102.0,104.1 102.7,105.5 103.4,105.6 104.0,105.0 104.7,105.0 105.4,106.1 106.0,106.0 106.7,106.1 107.4,105.0 108.0,106.0 108.7,105.3 109.4,105.8 110.0,105.7 110.7,106.0 111.4,106.4 112.0,105.8 112.7,106.0 113.4,105.4 114.0,104.7 114.7,105.4 115.4,105.2 116.0,106.2 116.7,105.5 117.4,105.6 118.0,105.3 118.7,106.1 119.4,105.9 120.0,106.0 120.7,106.1 121.4,105.6 122.0,104.8 122.7,105.9 123.4,105.4 124.0,105.2 124.7,105.9 125.4,105.7 126.0,105.4 126.7,104.8 127.4,105.3 128.0,105.1 128.7,105.7 129.4,106.5 130.0,105.3 130.7,105.6 131.4,106.2 132.0,105.0 132.7,106.1 133.4,105.5 134.0,105.1 134.7,106.0 135.4,105.6 136.0,105.4 136.7,106.6 137.4,105.1 138.0,105.2 138.7,105.3 139.4,105.5 140.0,105.6 140.7,105.4 141.4,105.9 142.0,105.7 142.7,105.9 143.4,105.3 144.0,105.3 144.7,105.7 145.4,105.5 146.0,106.0 146.7,105.9 147.4,105.6 148.0,105.4 148.7,105.6 149.4,105.7 150.0,105.5 150.7,105.0 151.4,106.6 152.0,106.0 152.7,105.0 153.4,105.5 154.0,106.1 154.7,105.4 155.4,105.4 156.0,104.7 156.7,106.2 157.4,105.6 158.0,105.7 158.7,105.4 159.4,105.4 160.0,106.1 160.7,106.0 161.4,105.7 162.0,105.0 162.7,105.4 163.4,106.0 164.0,105.8 164.7,106.0 165.4,105.4 166.0,105.8 166.7,106.7 167.4,104.9 168.0,106.3 168.7,105.7 169.4,105.3 170.0,106.4 170.7,106.4 171.4,106.1 172.0,105.3 172.7,105.2 173.4,105.4 174.0,105.1 174.7,105.1 175.4,106.0 176.0,106.2 176.7,105.9 177.4,106.1 178.0,106.2 178.7,106.0 179.4,105.6 180.0,105.8 180.7,105.3 181.4,105.6 182.0,105.1 182.7,105.7 183.4,105.6 184.0,105.8 184.7,105.5 185.4,106.3 186.0,105.6 186.7,106.0 187.4,105.5 188.0,106.2 188.7,105.8 189.4,104.9 190.0,106.1 190.7,105.2 191.4,104.7 192.0,106.3 192.7,105.5 193.4,106.3 194.0,105.5 194.7,106.3 195.4,105.6 196.0,105.9 196.7,105.1 197.4,105.9 198.0,104.9 198.7,105.3 199.4,105.4 200.0,106.5 200.7,106.5 201.4,105.7 202.0,105.0 202.7,106.0 203.4,106.0 204.0,104.9 204.7,106.0 205.4,105.5 206.0,105.8 206.7,105.1 207.4,105.9 208.0,105.8 208.7,105.0 209.4,105.9 210.0,104.4 210.7,105.6 211.4,105.3 212.0,106.3 212.7,106.1 213.4,104.9 214.0,106.4 214.7,106.1 215.4,105.2 216.0,105.6 216.7,105.3 217.4,105.0 218.0,105.3 218.7,105.3 219.4,105.8 220.0,105.8 220.7,105.9 221.4,104.9 222.0,105.9 222.7,105.0 223.4,105.5 224.0,105.0 224.7,105.7 225.4,106.1 226.0,105.3 226.7,105.2 227.4,105.5 228.0,106.1 228.7,105.8 229.4,105.1 230.0,105.3 230.7,105.4 231.4,105.7 232.0,106.4 232.7,105.5 233.4,105.3 234.0,105.4 234.7,105.2 235.4,105.3 236.0,105.8 236.7,106.6 237.4,106.0 238.0,105.6 238.7,105.8 239.4,105.6 240.0,106.5 240.7,105.1 241.4,106.6 242.0,105.8 242.7,106.1 243.4,106.5 244.0,105.2 244.7,106.6 245.4,105.1 246.0,106.1 246.7,105.7 247.4,104.9 248.0,105.9 248.7,106.0 249.4,106.0 250.0,105.5 250.7,105.3 251.4,106.0 252.0,106.1 252.7,106.0 253.4,105.4 254.0,105.2 254.7,105.5 255.4,105.2 256.0,106.6 256.7,106.0 257.4,105.3 258.0,105.5 258.7,105.4 259.4,105.9 260.0,106.0 260.7,106.0 261.4,106.0 262.0,105.0 262.7,105.8 263.4,105.5 264.0,105.3 264.7,104.6 265.4,106.1 266.0,104.9 266.7,105.3 267.4,106.0 268.0,106.2 268.7,105.5 269.4,105.1 270.0,106.5 270.7,105.8 271.4,105.8 272.0,105.8 272.7,105.7 273.4,105.7 274.0,105.9 274.7,106.3 275.4,105.7 276.0,105.7 276.7,106.1 277.4,105.5 278.0,105.6 278.7,105.6 279.4,105.9 280.0,105.6 280.7,106.1 281.4,105.6 282.0,105.8 282.7,105.8 283.4,105.7 284.0,105.2 284.7,105.6 285.4,105.6 286.0,105.5 286.7,105.1 287.4,105.9 288.0,105.3 288.7,105.3 289.4,105.5 290.0,105.2 290.7,106.1 291.4,106.1 292.0,105.8 292.7,105.8 293.4,106.4 294.0,106.1 294.7,106.2 295.4,106.0 296.0,104.8 296.7,105.1 297.4,106.3 298.0,105.0 298.7,105.9 299.4,105.6 300.0,105.6 300.7,105.2 301.4,105.5 302.0,105.5 302.7,104.9 303.4,105.7 304.0,105.8 304.7,105.1 305.4,105.3 306.0,105.7 306.7,105.8 307.4,105.2 308.0,105.5 308.7,104.1 309.4,105.0 310.0,105.0 310.7,105.5 311.4,106.0 312.0,105.5 312.7,104.8 313.4,104.5 314.0,105.9 314.7,104.9 315.4,106.2 316.0,105.4 320.0,105.1 320.7,106.4 321.4,106.0 322.0,105.5 322.7,105.9 323.4,106.4 324.0,104.8 324.7,105.2 325.4,105.2 326.0,105.9 326.7,104.3 327.4,106.3 328.0,105.5 328.7,105.5 329.4,105.5 330.0,105.4 330.7,105.7 331.4,105.5 332.0,105.8 332.7,105.1 333.4,105.2 334.0,106.3 334.7,104.9 335.4,106.6 336.0,106.2 336.7,105.7 337.4,105.6 338.0,105.9 338.7,104.5 339.4,105.5 340.0,105.2 340.7,106.0 341.4,105.4 342.0,105.3 342.7,105.7 343.4,105.7 344.0,105.6 344.7,106.2 345.4,104.9 346.0,105.3 346.7,105.0 347.4,105.5 348.0,105.2 348.7,105.4 349.4,105.2 350.0,106.1 350.7,105.4 351.4,104.9 352.0,106.4 352.7,104.7 353.4,106.3 354.0,106.2 354.7,105.2 355.4,105.2 356.0,105.0 356.7,106.2 357.4,105.0 358.0,105.0 358.7,105.9 359.4,105.8 360.0,106.0 360.7,105.6 361.4,105.6 362.0,105.9 362.7,105.9 363.4,105.4 364.0,106.0 364.7,106.1 365.4,105.8 366.0,105.8 366.7,105.3 367.4,104.7 368.0,105.9 368.7,104.6 369.4,105.1 370.0,105.9 370.7,106.3 371.4,106.2 372.0,106.5 372.7,105.7 373.4,105.8 374.0,104.7 374.7,106.1 375.4,105.4 376.0,105.2 376.7,105.2 377.4,105.7 378.0,105.5 378.7,105.3 379.4,105.2 380.0,104.9 380.7,105.6 381.4,105.6 382.0,105.8 382.7,104.4 383.4,105.7 384.0,105.6 384.7,105.3 385.4,105.2 386.0,105.4 386.7,105.8 387.4,105.7 388.0,105.7 388.7,105.1 389.4,104.9 390.0,105.3 390.7,105.2 391.4,105.2 392.0,106.0 392.7,104.9 393.4,105.0 394.0,105.9 394.7,105.4 395.4,105.9 396.0,105.5 396.7,106.0 397.4,105.4 398.0,105.9 398.7,106.4 399.4,105.4 400.0,106.4 400.7,104.8 401.4,105.7 402.0,106.4 402.7,105.4 403.4,105.3 404.0,105.7 404.7,105.1 405.4,104.1 406.0,105.6 406.7,105.9 407.4,105.3 408.0,105.3 408.7,105.3 409.4,106.3 410.0,105.4 410.7,105.4 411.4,104.7 412.0,105.3 412.7,104.5 413.4,106.0 414.0,105.2 414.7,105.1 415.4,105.5 416.0,105.3 416.7,105.5 417.4,105.8 418.0,106.1 418.7,105.1 419.4,105.5 420.0,105.2 420.7,105.0 421.4,106.0 422.0,105.8 422.7,105.5 423.4,105.8 424.0,105.7 424.7,106.7 425.4,105.7 426.0,106.3 426.7,105.5 427.4,105.5 428.0,105.2 428.7,105.4 429.4,105.1 430.0,105.6 430.7,105.7 431.4,105.3 432.0,105.1 432.7,105.7 433.4,105.8 434.0,105.1 434.7,106.1 435.4,106.0 436.0,105.2 436.7,105.6 437.4,105.9 438.0,105.1 438.7,106.1 439.4,106.4 440.0,105.9 440.7,106.0 441.4,106.1 442.0,105.7 442.7,105.7 443.4,105.2 444.0,105.8 444.7,105.3 445.4,105.9 446.0,105.2 446.7,105.8 447.4,105.1 448.0,105.2 448.7,105.1 449.4,105.3 450.0,104.9 450.7,105.4 451.4,105.1 452.0,105.3 452.7,106.5 453.4,105.6 454.0,106.1 454.7,105.9 455.4,105.8 456.0,106.3 456.7,105.4 457.4,105.3 458.0,105.3 458.7,106.4 459.4,105.8 460.0,105.1 460.7,105.0 461.4,106.9 462.0,106.3 462.7,105.3 463.4,106.0 464.0,106.2 464.7,104.9 465.4,105.5 466.0,105.3 466.7,104.9 467.4,106.3 468.0,105.7 468.7,106.1 469.4,105.5 470.0,105.7 470.7,104.4 471.4,106.2 472.0,104.6 472.7,105.1 473.4,105.7 474.0,105.2 474.7,105.6 475.4,104.8 476.0,105.1 476.7,106.2 477.4,105.0 478.0,105.4 478.7,104.6 479.4,106.0 480.0,104.3 480.7,105.9 481.4,105.9 482.0,105.3 482.7,105.8 483.4,105.7 484.0,105.5 484.7,105.7 485.4,106.4 486.0,105.9 486.7,105.1 487.4,105.3 488.0,105.9 488.7,105.1 489.4,105.7 490.0,105.9 490.7,105.0 491.4,105.3 492.0,106.5 492.7,105.1 493.4,105.8 494.0,104.9 494.7,106.2 495.4,105.0 496.0,105.7 496.7,105.5 497.4,104.6 498.0,105.6 498.7,105.3 499.4,105.7 500.0,105.0 500.7,105.1 501.4,106.0 502.0,105.5 502.7,106.3 503.4,106.0 504.0,105.9 504.7,105.4 505.4,105.8 506.0,105.4 506.7,105.1 507.4,105.7 508.0,105.7 508.7,105.1 509.4,105.8 510.0,105.6 510.7,105.6 511.4,105.9 512.0,105.7 512.7,105.0 513.4,105.9 514.0,104.9 514.7,106.0 515.4,105.9 516.0,106.0 516.7,105.5 517.4,105.7 518.0,105.4 518.7,106.1 519.4,106.0 520.0,105.5 520.7,106.3 521.4,105.0 522.0,104.9 522.7,105.3 523.4,105.5 524.0,105.2 524.7,105.7 525.4,105.2 526.0,105.4 526.7,105.9 527.4,105.7 528.0,105.0 528.7,105.0 529.4,106.1 530.0,105.2 530.7,106.1 531.4,105.8 532.0,105.3 532.7,105.8 533.4,105.7 534.0,104.8 534.7,105.4 535.4,105.7 536.0,105.8 536.7,105.5 537.4,106.1 538.0,106.4 538.7,105.2 539.4,105.9 540.0,105.3 540.7,105.1 541.4,105.8 542.0,105.1 542.7,105.2 543.4,105.8 544.0,104.9 544.7,105.4 545.4,105.2 546.0,105.1 546.7,104.8 547.4,105.6 548.0,105.1 548.7,105.1 549.4,105.5 550.0,105.7 550.7,105.7 551.4,105.3 552.0,105.5 552.7,105.7 553.4,105.3 554.0,106.4 554.7,106.3 555.4,106.3 556.0,106.0 556.7,105.9 557.4,105.7 558.0,106.2 558.7,105.3 559.4,104.6 560.0,105.8 560.7,105.1 561.4,106.4 562.0,106.3 562.7,105.6 563.4,105.8 564.0,105.8 564.7,106.4 565.4,106.4 566.0,105.9 566.7,105.3 567.4,104.9 568.0,104.3 568.7,104.9 569.4,104.9 570.0,105.8 570.7,105.4 571.4,106.0 572.0,105.3 572.7,105.4 573.4,106.5 574.0,106.1 574.7,105.8 575.4,106.1 576.0,105.9 576.7,106.0 577.4,105.0 578.0,105.1 578.7,105.5 579.4,106.5 580.0,104.9 580.7,105.6 581.4,104.4 582.0,105.7 582.7,105.2 583.4,105.5 584.0,105.5 584.7,104.8 585.4,105.7 586.0,104.9 586.7,104.9 587.4,105.5 588.0,105.1 588.7,105.3 589.4,105.1 590.0,105.6 590.7,106.1 591.4,105.5 592.0,105.5 592.7,105.4 593.4,106.4 594.0,105.1 594.7,105.5 595.4,105.5 596.0,106.0 596.7,105.2 597.4,105.9 598.0,105.3 598.7,106.2 599.4,105.0 600.0,105.4 600.7,105.5 601.4,106.4 602.0,105.0 602.7,105.2 603.4,105.7 604.0,104.8 604.7,106.3 605.4,105.9 606.0,105.3 606.7,104.2 607.4,104.5 608.0,106.2 608.7,105.9 609.4,105.5 610.0,105.8 610.7,105.7 611.4,104.7 612.0,105.5 612.7,106.1 613.4,105.7 614.0,105.9 614.7,106.2 615.4,104.8 616.0,105.4 616.7,105.7 617.4,105.2 618.0,105.6 618.7,105.4 619.4,107.0 620.0,105.1 620.7,105.5 621.4,105.9 622.0,105.3 622.7,106.6 623.4,106.7 624.0,105.7 624.7,105.4 625.4,105.0 626.0,105.5 626.7,105.6 627.4,105.5 628.0,104.7 628.7,105.7 629.4,105.6 630.0,104.5 630.7,105.3 631.4,105.7 632.0,104.0 632.7,105.1 633.4,104.6 634.0,105.5 634.7,105.4 635.4,105.9 636.0,105.3 636.7,105.9 637.4,106.5 638.0,105.6 638.7,105.6 639.4,105.3 640.0,104.9 640.7,105.8 641.4,105.2 642.0,106.0 642.7,105.5 643.4,106.0 644.0,105.5 644.7,104.7 645.4,106.4 646.0,105.7 646.7,106.1 647.4,105.9 648.0,104.8 648.7,106.1 649.4,105.8" fill="none" stroke="#3f9a6b" stroke-width="1"/>
  <line x1="480" y1="272" x2="500" y2="272" stroke="#4a7bd0" stroke-width="2"/><text x="504" y="276" font-size="12">ax</text>
  <line x1="530" y1="272" x2="550" y2="272" stroke="#e08a3c" stroke-width="2"/><text x="554" y="276" font-size="12">ay</text>
  <line x1="580" y1="272" x2="600" y2="272" stroke="#3f9a6b" stroke-width="2"/><text x="604" y="276" font-size="12">az</text>
</svg>
```

그림 3 — s1 로그의 3축 가속도(6~24 s). 주황 음영이 라벨 구간이다. az는 중력 때문에 1 g 근처에 있고, flick 구간에는 ax가 ±1.5 g 사인파, shake 구간에는 ay가 점선(±FS)에 잘려 평평한 봉우리를 만든다. 14.0~14.1 s에는 드롭된 5샘플 때문에 선이 직선으로 이어진다.

---

## 5. pandas로 센서 로그 다루기

### 5.1 DataFrame과 datetime index

pandas `DataFrame`은 열마다 이름이 있는 2D 표이고, 행에는 **인덱스**가 붙는다. 센서 로그에서는 인덱스를 시간(`DatetimeIndex`)으로 두면 "14.0~14.2 s 구간", "100 ms 단위 평균", "최근 0.5 s 이동 평균" 같은 연산이 한 줄이 된다.

```python
# imu_io.py (2/3) — 앞 부분에 이어서
def to_frame(hdr, rec):
    acc_lsb = 32768 / hdr["acc_fs_g"]; gyr_lsb = 32768 / hdr["gyr_fs_dps"]
    df = pd.DataFrame({c: rec[c].astype(np.float32) / np.float32(acc_lsb) for c in ("ax", "ay", "az")})
    for c in ("gx", "gy", "gz"):
        df[c] = rec[c].astype(np.float32) / np.float32(gyr_lsb)
    df.index = pd.to_datetime(rec["timestamp_us"].astype(np.int64), unit="us")
    df.index.name = "t"
    return df
```

`pd.to_datetime(…, unit="us")`는 정수를 "1970-01-01부터의 µs"로 해석한다. 기기 부팅 후 경과 시간이라 날짜는 의미 없지만, 시간 연산 기능을 쓰려고 이렇게 둔다. 실제 파이프라인에서는 세션 시작 wall-clock(호스트가 기록)을 더해서 진짜 시각으로 바꾸기도 한다. `astype(np.int64)`는 uint32를 먼저 넓혀 두는 습관이다(§5.2의 wrap-around 참고).

### 5.2 드롭 샘플 검출 — timestamp 차분

FIFO overflow, 버스 경합, 태스크 지연으로 샘플이 빠지면 timestamp 간격이 튄다. 50 Hz의 정상 간격은 20,000 µs다.

```
dt[k] = t[k+1] − t[k]
누락 판정: dt > 1.5 × period       (jitter는 통과시키고, 1샘플 누락(2 × period)은 잡는 문턱)
누락 개수: round(dt / period) − 1
예) dt = 119,933 µs → 119,933 / 20,000 = 6.0 → 6 − 1 = 5개 누락
```

말로 하면, "간격이 두 칸이면 한 개가 빠진 것"이다. 1.5를 쓰는 이유는 jitter(±수백 µs)에는 반응하지 않으면서 가장 작은 누락(2 × period)을 확실히 잡는 중간값이기 때문이다.

이 코드는 DataFrame을 만들고 드롭 위치·개수·비율을 계산한다.

```python
import numpy as np
import pandas as pd
from imu_io import load_log, to_frame

df = to_frame(*load_log("imu_s1.bin"))
print(df.head(3).round(3))
print(df.dtypes.unique(), len(df))

period_us = 1e6 / 50                                   # 20000 µs
dt_us = np.diff(df.index.asi8) / 1e3                   # ns → µs
print("dt 통계 [µs]: median=%.0f min=%.0f max=%.0f" % (np.median(dt_us), dt_us.min(), dt_us.max()))

gap = dt_us > 1.5 * period_us                          # 주기의 1.5배를 넘으면 누락
missing = np.round(dt_us[gap] / period_us).astype(int) - 1
for t, d, m in zip(df.index[1:][gap], dt_us[gap], missing):
    print(f"gap at {t.strftime('%S.%f')[:-3]} s: dt={d:.0f} µs -> {m} sample(s) lost")
expected = round((df.index[-1] - df.index[0]).total_seconds() * 50) + 1
print("expected", expected, "got", len(df), "-> drop rate %.2f%%" % (100 * (1 - len(df) / expected)))
```

```text
                               ax     ay     az     gx     gy     gz
t                                                                   
1970-01-01 00:00:00.001034 -0.022 -0.005  1.004  0.824 -0.992  0.320
1970-01-01 00:00:00.020928  0.012  0.013  0.973  1.297 -1.099  1.511
1970-01-01 00:00:00.040989 -0.023 -0.012  1.028 -0.656 -0.931  0.473
[dtype('float32')] 1494
dt 통계 [µs]: median=20002 min=19808 max=119933
gap at 14.101 s: dt=119933 µs -> 5 sample(s) lost
gap at 25.020 s: dt=39841 µs -> 1 sample(s) lost
expected 1500 got 1494 -> drop rate 0.40%
```

출력에서 볼 것: C 로거에서 일부러 뺀 샘플(700~704번, 1250번)이 정확히 5개, 1개로 잡혔다. jitter(19,808~20,194 µs)는 문턱에 걸리지 않는다. `df.index.asi8`은 DatetimeIndex를 int64 ns로 보는 view다.

```svg
<svg viewBox="0 0 680 245" xmlns="http://www.w3.org/2000/svg">
  <line x1="60" y1="20" x2="60" y2="200" stroke="currentColor" stroke-width="1"/> <line x1="60" y1="200" x2="650" y2="200" stroke="currentColor" stroke-width="1"/> <text x="54" y="204" font-size="12" text-anchor="end">0</text>
  <text x="54" y="176" font-size="12" text-anchor="end">20</text> <text x="54" y="148" font-size="12" text-anchor="end">40</text> <text x="54" y="121" font-size="12" text-anchor="end">60</text>
  <text x="54" y="93" font-size="12" text-anchor="end">80</text> <text x="54" y="65" font-size="12" text-anchor="end">100</text> <text x="54" y="38" font-size="12" text-anchor="end">120</text>
  <text x="20" y="110" font-size="12" text-anchor="middle">ms</text> <line x1="60" y1="158.5" x2="650" y2="158.5" stroke="#d0564a" stroke-width="1" stroke-dasharray="5 4"/>
  <text x="646" y="153" font-size="12" text-anchor="end">문턱 30 ms (1.5 × period)</text> <text x="60" y="216" font-size="12" text-anchor="middle">0</text> <text x="158.3" y="216" font-size="12" text-anchor="middle">5</text>
  <text x="256.7" y="216" font-size="12" text-anchor="middle">10</text> <text x="355" y="216" font-size="12" text-anchor="middle">15</text> <text x="453.3" y="216" font-size="12" text-anchor="middle">20</text>
  <text x="551.7" y="216" font-size="12" text-anchor="middle">25</text> <text x="650" y="216" font-size="12" text-anchor="middle">30</text> <text x="355" y="235" font-size="12" text-anchor="middle">time [s]</text>
  <polyline points="61.6,172.2 63.2,172.2 65.5,172.2 67.1,172.2 69.5,172.2 71.0,172.2 72.2,172.2 73.8,172.2 76.5,172.1 78.5,172.2 80.1,172.0 82.4,172.1 84.4,172.2 86.4,172.2 87.6,172.1 89.5,172.2 91.5,172.2 94.2,172.1 95.4,172.0 99.0,172.2 100.9,172.1 102.1,172.2 104.5,172.2 106.0,172.2 108.8,172.2 110.0,172.2 111.9,172.1 113.9,172.2 116.7,172.2 117.8,172.1 119.0,172.2 122.2,172.1 124.5,172.2 126.5,172.2 127.3,172.3 128.9,172.2 132.0,172.2 133.6,172.2 135.9,172.2 136.7,172.1 139.5,172.2 140.7,172.1 142.6,172.2 146.2,172.1 146.9,172.2 148.5,172.1 151.7,172.1 153.6,172.2 155.6,172.2 156.4,172.2 158.7,172.2 160.7,172.1 163.5,172.1 165.0,172.1 167.8,172.1 168.6,172.2 170.2,172.1 173.3,172.2 174.9,172.1 176.8,172.1 178.0,172.1 180.8,172.1 183.5,172.3 185.1,172.2 186.3,172.3 187.9,172.1 190.2,172.2 191.8,172.1 195.3,172.2 196.1,172.1 198.9,172.1 199.7,172.2 202.8,172.3 205.2,172.2 205.9,172.2 209.1,172.2 209.9,172.2 212.6,172.1 214.6,172.0 217.0,172.2 218.9,172.2 219.7,172.1 222.9,172.1 224.4,172.2 226.4,172.2 228.8,172.1 230.3,172.3 232.3,172.1 234.7,172.2 235.1,172.2 238.2,172.1 240.2,172.2 242.1,172.1 244.5,172.1 245.7,172.1 246.9,172.2 249.6,172.2 252.0,172.2 252.8,172.1 255.1,172.2 256.7,172.2 258.7,172.2 261.0,172.1 262.6,172.1 264.6,172.1 267.7,172.2 269.3,172.1 270.8,172.2 272.8,172.1 276.0,172.2 277.9,172.2 279.5,172.2 280.3,172.2 283.4,172.2 284.6,172.1 287.0,172.1 289.3,172.2 291.7,172.0 292.9,172.1 294.1,172.2 296.4,172.2 299.2,172.1 301.5,172.1 302.7,172.2 304.3,172.0 307.0,172.3 307.8,172.1 310.2,172.1 311.8,172.2 314.9,172.1 316.5,172.2 318.8,172.1 319.6,172.2 322.4,172.2 324.3,172.2 325.5,172.1 328.7,172.2 330.2,172.1 331.8,172.1 333.8,172.2 337.3,33.9 340.5,172.2 341.6,172.1 344.8,172.2 345.6,172.2 347.2,172.2 349.5,172.2 352.7,172.1 354.2,172.2 355.8,172.2 358.2,172.2 360.5,172.1 361.3,172.1 364.5,172.2 366.4,172.1 367.6,172.2 369.6,172.2 371.9,172.2 373.1,172.1 375.9,172.2 377.0,172.3 380.2,172.2 381.0,172.1 383.3,172.1 386.1,172.2 387.3,172.2 390.0,172.2 391.6,172.2 392.8,172.2 394.4,172.2 397.9,172.2 399.5,172.1 401.0,172.3 403.0,172.2 404.2,172.1 406.2,172.1 408.9,172.1 410.1,172.2 413.6,172.2 414.4,172.1 416.8,172.3 418.0,172.2 421.1,172.2 423.5,172.1 425.4,172.2 426.6,172.2 428.2,172.1 429.8,172.1 432.9,172.2 434.1,172.1 436.8,172.2 438.0,172.2 441.2,172.1 443.1,172.2 445.1,172.2 445.9,172.2 447.8,172.1 449.4,172.2 453.0,172.2 454.5,172.1 456.1,172.2 458.1,172.2 460.0,172.1 462.0,172.2 463.2,172.2 465.5,172.3 468.7,172.1 470.3,172.2 472.6,172.2 474.6,172.2 475.8,172.2 477.0,172.1 480.1,172.2 481.7,172.3 482.9,172.2 485.6,172.2 487.6,172.2 489.1,172.1 490.7,172.2 492.7,172.1 495.0,172.2 497.0,172.2 499.4,172.1 501.3,172.2 504.1,172.1 505.7,172.2 508.0,172.2 509.6,172.2 512.0,172.3 512.4,172.1 515.1,172.2 517.5,172.2 518.6,172.1 520.2,172.2 522.6,172.1 524.5,172.1 526.9,172.2 529.3,172.1 530.4,172.2 532.4,172.1 534.4,172.2 536.0,172.2 538.7,172.2 540.7,172.1 542.2,172.2 545.4,172.2 547.0,172.2 548.5,172.1 551.3,172.1 552.1,144.8 555.2,172.1 556.4,172.2 559.2,172.1 560.7,172.2 562.3,172.2 563.9,172.2 565.5,172.1 569.0,172.1 569.8,172.3 572.1,172.2 574.5,172.2 576.5,172.1 577.6,172.1 580.8,172.2 581.6,172.2 583.5,172.1 585.1,172.1 587.5,172.2 589.8,172.1 591.4,172.1 593.4,172.2 595.3,172.1 597.7,172.2 599.3,172.3 600.9,172.2 603.2,172.2 606.0,172.2 606.8,172.1 609.1,172.1 610.7,172.2 612.7,172.1 615.4,172.1 618.2,172.2 619.7,172.1 620.5,172.2 622.5,172.2 626.0,172.1 627.6,172.2 629.2,172.2 631.5,172.2 632.7,172.2 635.9,172.1 637.8,172.2 639.0,172.1 640.2,172.2 642.5,172.1 645.3,172.2 646.5,172.1 649.2,172.2" fill="none" stroke="#4a7bd0" stroke-width="1.2"/>
  <circle cx="337.3" cy="33.9" r="3.5" fill="#d0564a"/> <circle cx="552.1" cy="144.8" r="3.5" fill="#d0564a"/> <text x="345" y="38" font-size="12">14.10 s: dt = 119.9 ms → 5개 누락</text>
  <text x="545" y="128" font-size="12" text-anchor="end">25.02 s: 39.8 ms → 1개 누락</text> <text x="120" y="190" font-size="12">정상: 20 ms ± 0.2 ms (jitter)</text>
</svg>
```

그림 4 — s1 로그의 timestamp 간격 dt. 0.1 s 구간마다 최댓값을 이은 선이다. 정상 샘플은 20 ms 선에 붙어 있고, 빨간 점선(30 ms)을 넘는 두 봉우리가 누락 지점이다.

**uint32 µs 카운터의 wrap-around.** 펌웨어의 32-bit µs 타이머는 2³² µs ≈ 71.6분마다 0으로 돌아간다. 1시간 넘는 세션 로그에서 차분이 갑자기 −42억이 되면 이것이다. C처럼 **uint32 모듈러 산술로 차분**을 내면 wrap을 자연스럽게 흡수한다.

이 코드는 wrap이 있는 timestamp에서 순진한 차분과 uint32 차분을 비교하고, 연속된 64-bit 시간으로 펼친다(unwrap).

```python
import numpy as np

# uint32 µs 카운터는 2^32 µs ≈ 71.6분마다 0으로 돌아간다
ts = np.array([4294927296, 4294947296, 4294967295 - 3, 12, 20012], dtype=np.uint32)
print("naive diff (int64):", np.diff(ts.astype(np.int64)))
print("uint32 diff       :", np.diff(ts))                 # 모듈러 산술 = C와 같은 동작
d = np.diff(ts).astype(np.int64)                         # wrap을 흡수한 간격
t_unwrapped = ts[0].astype(np.int64) + np.concatenate([[0], np.cumsum(d)])
print("unwrapped [µs]    :", t_unwrapped)
print("2^32 µs = %.1f min" % (2**32 / 1e6 / 60))
```

```text
naive diff (int64): [      20000       19996 -4294967280       20000]
uint32 diff       : [20000 19996    16 20000]
unwrapped [µs]    : [4294927296 4294947296 4294967292 4294967308 4294987308]
2^32 µs = 71.6 min
```

출력에서 볼 것: `12 − 4294967292`를 uint32로 계산하면 16이 된다(C의 `uint32_t now - prev`와 같은 트릭). 이 방법은 두 샘플 사이 간격이 71.6분보다 짧을 때만 맞는다. 그래서 로거는 긴 공백 뒤에 64-bit 시간이나 wall-clock을 가끔 기록해 두는 것이 좋다.

### 5.3 고정 rate로 재샘플·보간

모델은 "100개 = 정확히 2초"를 가정한다. 그래서 timestamp를 **고정 격자**(0, 20, 40 ms …)에 맞춘다. 두 단계로 한다.

1. 격자를 만들고 각 격자점에 가장 가까운 샘플(±반 주기 이내)을 붙인다. 없으면 NaN.
2. 짧은 구멍(예: 5샘플 = 100 ms 이하)만 선형 보간하고, 긴 구멍은 NaN으로 남겨서 그 구간을 학습에서 뺀다.

손으로 먼저: 13.98 s의 az ≈ 1.004, 14.10 s의 az ≈ 1.020이고 그 사이 5칸이 비었다. 6칸에 걸쳐 0.016만큼 오르므로 칸당 약 0.0027씩 올라 1.007, 1.009~1.010, 1.012, 1.015, 1.017이 된다.

이 코드는 드롭 구간을 격자에 맞추고 보간한 결과를 확인한다. 마지막 줄은 평균 기반 downsample(50 Hz → 10 Hz)이다.

```python
import pandas as pd
from imu_io import load_log, to_frame

df = to_frame(*load_log("imu_s1.bin"))
T = lambda s: pd.Timestamp(0) + pd.Timedelta(seconds=s)     # 초 → Timestamp
print(df.loc[T(13.95):T(14.13), ["az"]].round(3))

# 1) 고정 20 ms 격자로 재샘플: 격자 위치에 가장 가까운 샘플(±10 ms)을 붙이고, 없으면 NaN
grid = pd.date_range(df.index[0].floor("20ms"), df.index[-1], freq="20ms")
fixed = df.reindex(grid, method="nearest", tolerance=pd.Timedelta("10ms"))
print("grid len:", len(fixed), " NaN rows:", fixed["az"].isna().sum())

# 2) 짧은 구멍만 선형 보간 (limit=5 샘플 = 100 ms)
filled = fixed.interpolate(method="time", limit=5)
part = filled.loc[T(13.95):T(14.13), "az"]
print("t [s]:", (part.index - T(0)).total_seconds().to_numpy())
print("az   :", part.round(3).to_numpy())

# 3) 평균 기반 downsample: 50 Hz → 10 Hz
print(df["az"].resample("100ms").mean().head(3).round(4))
```

```text
                               az
t                                
1970-01-01 00:00:13.961048  0.972
1970-01-01 00:00:13.981086  1.004
1970-01-01 00:00:14.101019  1.020
1970-01-01 00:00:14.121010  0.965
grid len: 1500  NaN rows: 6
t [s]: [13.96 13.98 14.   14.02 14.04 14.06 14.08 14.1  14.12]
az   : [0.972 1.004 1.007 1.01  1.012 1.015 1.017 1.02  0.965]
t
1970-01-01 00:00:00.000    1.0118
1970-01-01 00:00:00.100    0.9971
1970-01-01 00:00:00.200    0.9850
Freq: 100ms, Name: az, dtype: float32
```

출력에서 볼 것: 격자는 정확히 1500점(30 s × 50 Hz)이고 NaN 6개가 드롭 6개와 일치한다. 보간값이 손계산과 같다. `resample("100ms").mean()`은 5샘플씩 평균하는 downsample인데, 평균이 간단한 low-pass filter 역할을 해서 aliasing을 약간 줄여 준다(제대로 하려면 G5의 anti-aliasing 필터를 먼저 건다).

함정 두 가지. 첫째, `nearest`로 격자에 붙이면 각 샘플의 시각이 최대 반 주기(여기선 jitter ±0.1 ms 수준)만큼 옮겨진다. 50 Hz IMU에선 무시할 만하지만 16 kHz 오디오에서 이렇게 하면 안 된다(오디오는 샘플 카운트가 곧 시간이다). 둘째, 긴 구멍까지 보간하면 존재하지 않던 "매끈한 직선" 데이터를 모델이 학습한다. `limit`으로 막는다.

### 5.4 rolling — 이동 창 통계

`rolling`은 각 시점에서 "최근 창 안의 값"으로 통계를 낸다. `rolling("500ms")`는 시간 기반(최근 0.5 s), `rolling(25)`는 개수 기반이다. 기본은 **causal**(과거와 현재만 봄)이라 펌웨어의 이동 평균 필터와 같고, `center=True`는 미래 샘플도 보므로 오프라인 분석에서만 쓴다.

이 코드는 가속도 크기의 0.5 s 이동 평균·표준편차로 간단한 움직임 검출기를 만든다.

```python
import numpy as np
import pandas as pd
from imu_io import load_log, to_frame

df = to_frame(*load_log("imu_s1.bin"))
df["amag"] = np.sqrt(df.ax**2 + df.ay**2 + df.az**2)

# rolling: 최근 0.5 s(시간 기반) 창의 평균·표준편차 — 'causal'(과거만 봄)
r = df["amag"].rolling("500ms")
df["amag_mean"], df["amag_std"] = r.mean(), r.std()
T = lambda s: pd.Timestamp(0) + pd.Timedelta(seconds=s)
for s in (5.0, 9.0, 21.0):
    row = df.loc[df.index.asof(T(s))]
    print(f"t={s:4.1f}s  |a|={row.amag:.3f}  mean500={row.amag_mean:.3f}  std500={row.amag_std:.3f}")

# 샘플 개수 기반 창 + center=True (오프라인 분석용, 미래 샘플도 봄)
c = df["amag"].rolling(25, center=True).std()
print("center=True NaN 개수 (앞/뒤 가장자리):", c.isna().sum())

# 간단한 움직임 검출기: std > 0.2 g 인 구간
active = df["amag_std"] > 0.2
edges = active.astype(int).diff().fillna(0)
starts = (df.index[edges == 1] - T(0)).total_seconds()
ends = (df.index[edges == -1] - T(0)).total_seconds()
print("검출된 움직임 구간 [s]:", list(zip(starts.round(2), ends.round(2))))
```

```text
t= 5.0s  |a|=1.012  mean500=1.004  std500=0.023
t= 9.0s  |a|=1.122  mean500=1.417  std500=0.285
t=21.0s  |a|=2.812  mean500=2.980  std500=1.170
center=True NaN 개수 (앞/뒤 가장자리): 24
검출된 움직임 구간 [s]: [(8.08, 10.44), (20.02, 22.48)]
```

출력에서 볼 것: 정지 시 std는 0.023 g, 제스처 중에는 0.28~1.17 g이다. 검출된 구간이 실제(8~10 s, 20~22 s)보다 **끝이 약 0.45 s 늦다**. causal 창이 과거 0.5 s를 기억하기 때문이며, 펌웨어에서 같은 검출기를 돌려도 똑같이 늦는다. center=True의 NaN 24개는 창(25)이 양 끝에서 12개씩 모자라서 생긴다. 참고로 pandas `std()`의 기본은 ddof=1이다(§7.3).

### 5.5 라벨 구간 붙이기 + groupby

라벨은 보통 "샘플마다"가 아니라 **구간표**로 온다(영상 보며 라벨링 도구에서 찍은 start/end). 이것을 샘플에 붙이는 방법이 `pd.merge_asof`다. merge_asof는 "정렬된 키에 대해, 내 시각 이하인 **가장 가까운** 행"을 찾아 붙이는 조인이다. 각 샘플에 "직전에 시작한 구간"을 붙인 뒤, 그 구간의 `end_s` 전인지 확인하면 된다.

`groupby`는 "키별로 나눠서(split) 통계를 내고(apply) 다시 합친다(combine)". SQL의 `GROUP BY`와 같다. 여기서는 세션·사용자·라벨별 통계를 본다.

이 코드는 세 세션 로그에 라벨을 붙이고 세션·라벨·사용자별로 요약한다.

```python
import numpy as np
import pandas as pd
from imu_io import load_log, to_frame

# 라벨 도구가 내보낸 구간표 (세션 공통이라고 가정)
labels = pd.DataFrame({"start_s": [8.0, 20.0], "end_s": [10.0, 22.0],
                       "label": ["flick", "shake"]})

def attach_labels(df, labels):
    s = pd.DataFrame({"t_s": (df.index - pd.Timestamp(0)).total_seconds()})
    # 각 샘플에 대해 'start_s <= t_s' 인 마지막 구간을 찾고, end_s 전인지 확인
    m = pd.merge_asof(s, labels, left_on="t_s", right_on="start_s", direction="backward")
    inside = m["t_s"] < m["end_s"]
    return np.where(inside, m["label"], "idle")

sessions = {"s1": ("userA", "imu_s1.bin"), "s2": ("userA", "imu_s2.bin"),
            "s3": ("userB", "imu_s3.bin")}
frames = []
for sid, (user, path) in sessions.items():
    d = to_frame(*load_log(path))
    d["label"], d["session"], d["user"] = attach_labels(d, labels), sid, user
    frames.append(d)
all_df = pd.concat(frames)
print(all_df.shape)
print(all_df.groupby(["session", "label"]).size().unstack())
print(all_df.groupby("label")[["ax", "ay", "gy", "gz"]].std().astype("float64").round(2))
print(all_df.groupby("user")["az"].agg(["mean", "std", "count"]).astype({"mean": "float64", "std": "float64"}).round(4))
```

```text
(4492, 9)
label    flick  idle  shake
session                    
s1         100  1294    100
s2         100  1300    100
s3         100  1298    100
         ax    ay      gy      gz
label                            
flick  1.06  0.02  141.71    0.81
idle   0.02  0.02    0.80    0.80
shake  0.02  3.04    0.75  212.42
         mean     std  count
user                        
userA  0.9997  0.0201   2994
userB  1.0002  0.0201   1498
```

출력에서 볼 것: 라벨별 std를 손으로 검증할 수 있다. 진폭 A인 사인파의 std는 A/√2다. flick ax: 1.5/√2 = 1.061 ✓, gy: 200/√2 = 141.4 ✓, shake gz: 300/√2 = 212.1 ✓. 그런데 shake ay는 4.5/√2 = 3.18이어야 하는데 3.04다. **포화가 봉우리를 잘라서 에너지가 줄었기** 때문이다. 이런 "손계산과 안 맞는 숫자"가 데이터 문제를 찾는 단서다. 또 idle 샘플이 세션마다 1294/1300/1298로 다른 것은 드롭 때문이다.

함정: `merge_asof`는 양쪽이 키로 **정렬**되어 있어야 하고, 이 방식은 구간이 겹치지 않는다고 가정한다(겹치면 나중에 시작한 구간만 붙는다). 라벨 구간이 겹칠 수 있는 데이터(예: "걷기" 중 "flick")는 다중 라벨 열을 따로 만든다.

---

## 6. 학습용 윈도잉 — `X [N, C, L]`, `y [N]`

### 6.1 창 개수 공식

모델 입력은 고정 길이 창이다. 50 Hz에서 2 s 창이면 L = 100 샘플, 50% overlap이면 hop = 50 샘플이다.

```
N = ⌊(T − L) / hop⌋ + 1

T = 1500 (30 s), L = 100, hop = 50
N = ⌊(1500 − 100) / 50⌋ + 1 = ⌊28⌋ + 1 = 29
마지막 창: 시작 28 × 50 = 1400, 끝 1499  → 버려지는 샘플 0개
```

말로 하면, "첫 창을 놓고 남은 길이에 hop이 몇 번 들어가는가 + 1"이다. 창 i는 샘플 `[i·hop, i·hop + L)`, 즉 시간 `[i·1 s, i·1 s + 2 s)`를 덮는다.

```svg
<svg viewBox="0 0 680 262" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="44" font-size="12">라벨</text> <rect x="60" y="30" width="160" height="20" fill="#888" fill-opacity="0.25" stroke="#888" stroke-width="1"/>
  <rect x="220" y="30" width="160" height="20" fill="#e08a3c" fill-opacity="0.45" stroke="#e08a3c" stroke-width="1"/> <rect x="380" y="30" width="240" height="20" fill="#888" fill-opacity="0.25" stroke="#888" stroke-width="1"/>
  <text x="140" y="45" font-size="12" text-anchor="middle">idle</text> <text x="300" y="45" font-size="12" text-anchor="middle">flick (8~10 s)</text> <text x="500" y="45" font-size="12" text-anchor="middle">idle</text>
  <rect x="60" y="64" width="160" height="18" fill="#888" fill-opacity="0.2" stroke="#888" stroke-width="1.2"/> <text x="66" y="77" font-size="12">win 6: idle 100%</text>
  <rect x="140" y="89" width="160" height="18" fill="none" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="5 3"/> <text x="146" y="102" font-size="12">win 7: 50/50 → 버림</text>
  <rect x="220" y="114" width="160" height="18" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c" stroke-width="1.2"/> <text x="226" y="127" font-size="12">win 8: flick 100%</text>
  <rect x="300" y="139" width="160" height="18" fill="none" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="5 3"/> <text x="306" y="152" font-size="12">win 9: 50/50 → 버림</text>
  <rect x="380" y="164" width="160" height="18" fill="#888" fill-opacity="0.2" stroke="#888" stroke-width="1.2"/> <text x="386" y="177" font-size="12">win 10: idle 100%</text>
  <rect x="460" y="189" width="160" height="18" fill="#888" fill-opacity="0.2" stroke="#888" stroke-width="1.2"/> <text x="466" y="202" font-size="12">win 11: idle 100%</text>
  <line x1="60" y1="60" x2="60" y2="215" stroke="#4a7bd0" stroke-width="1" stroke-dasharray="2 2"/> <line x1="140" y1="60" x2="140" y2="215" stroke="#4a7bd0" stroke-width="1" stroke-dasharray="2 2"/>
  <text x="100" y="97" font-size="12" text-anchor="middle">hop</text> <text x="630" y="77" font-size="12">L = 2 s</text> <text x="630" y="93" font-size="12">= 100</text> <text x="622" y="115" font-size="12">hop=1 s</text>
  <text x="630" y="131" font-size="12">= 50</text> <line x1="60" y1="222" x2="620" y2="222" stroke="currentColor" stroke-width="1"/>
  <line x1="60" y1="218" x2="60" y2="226" stroke="currentColor"/><text x="60" y="240" font-size="12" text-anchor="middle">6</text>
  <line x1="140" y1="218" x2="140" y2="226" stroke="currentColor"/><text x="140" y="240" font-size="12" text-anchor="middle">7</text>
  <line x1="220" y1="218" x2="220" y2="226" stroke="currentColor"/><text x="220" y="240" font-size="12" text-anchor="middle">8</text>
  <line x1="300" y1="218" x2="300" y2="226" stroke="currentColor"/><text x="300" y="240" font-size="12" text-anchor="middle">9</text>
  <line x1="380" y1="218" x2="380" y2="226" stroke="currentColor"/><text x="380" y="240" font-size="12" text-anchor="middle">10</text>
  <line x1="460" y1="218" x2="460" y2="226" stroke="currentColor"/><text x="460" y="240" font-size="12" text-anchor="middle">11</text>
  <line x1="540" y1="218" x2="540" y2="226" stroke="currentColor"/><text x="540" y="240" font-size="12" text-anchor="middle">12</text>
  <line x1="620" y1="218" x2="620" y2="226" stroke="currentColor"/><text x="620" y="240" font-size="12" text-anchor="middle">13</text> <text x="340" y="257" font-size="12" text-anchor="middle">time [s]</text>
</svg>
```

그림 5 — 2 s 창, 1 s hop(50% overlap). 이웃한 창은 샘플 절반을 공유한다. flick 경계에 걸친 창 7, 9는 idle과 flick이 반반이라 라벨이 애매하다(아래 예제의 실제 출력과 같다).

### 6.2 코드 — sliding_window_view로 `[N, C, L]` 만들기

먼저 `imu_io.py`에 격자화와 샘플별 라벨 함수를 붙인다(§5.3, §5.5를 함수로 정리한 것).

```python
# imu_io.py (3/3)
CLASSES = ["idle", "flick", "shake"]
LABELS = pd.DataFrame({"start_s": [8.0, 20.0], "end_s": [10.0, 22.0], "label": ["flick", "shake"]})

def to_grid(df, fs=50):
    step = pd.Timedelta(seconds=1 / fs)
    grid = pd.date_range(df.index[0].floor(step), df.index[-1], freq=step)
    return df.reindex(grid, method="nearest", tolerance=step / 2).interpolate(method="time", limit=5)

def grid_labels(g, labels=LABELS):
    t = (g.index - pd.Timestamp(0)).total_seconds().to_numpy()
    y = np.zeros(len(t), dtype=np.int64)
    for s, e, lab in labels.itertuples(index=False):
        y[(t >= s) & (t < e)] = CLASSES.index(lab)
    return y
```

`(T, C)` 배열에 `sliding_window_view(sig, L, axis=0)`을 적용하면 창 축이 **맨 뒤에** 붙어서 `(T−L+1, C, L)`이 된다. PyTorch `Conv1d`가 기대하는 `[N, C, L]`(배치, 채널, 길이) 순서와 정확히 같다.

이 코드는 창을 만들고, 창 라벨을 두 규칙(다수결, 중앙)으로 정하고, 애매한 창을 버린다.

```python
import numpy as np
from numpy.lib.stride_tricks import sliding_window_view
from imu_io import load_log, to_frame, to_grid, grid_labels, CLASSES

g = to_grid(to_frame(*load_log("imu_s1.bin")))            # 50 Hz 고정 격자, 1500행
sig = g[["ax", "ay", "az", "gx", "gy", "gz"]].to_numpy()  # (T, C) = (1500, 6)
y_s = grid_labels(g)                                      # 샘플별 라벨 (T,)

L, hop = 100, 50                                          # 2 s 창, 50% overlap
X = sliding_window_view(sig, L, axis=0)[::hop]            # (N, C, L) — view!
Yw = sliding_window_view(y_s, L)[::hop]                   # (N, L)
print("T =", len(sig), " N = (T-L)//hop + 1 =", (len(sig) - L) // hop + 1)
print("X:", X.shape, X.dtype, " Yw:", Yw.shape)

counts = (Yw[:, :, None] == np.arange(len(CLASSES))).sum(axis=1)   # (N, K)
y_major = counts.argmax(axis=1)                           # 동률이면 앞 클래스(idle)
y_center = Yw[:, L // 2]
purity = counts.max(axis=1) / L
for i in (7, 8, 9, 19, 20, 21):
    print(f"win {i:2d} [{i*hop/50:4.1f}~{(i*hop+L)/50:4.1f}s] counts={counts[i]} "
          f"major={CLASSES[y_major[i]]:5s} center={CLASSES[y_center[i]]:5s} purity={purity[i]:.2f}")

keep = purity >= 0.75                                     # 애매한 창은 버린다
X_train = np.ascontiguousarray(X[keep], dtype=np.float32)
print("kept:", keep.sum(), "/", len(keep), " X_train:", X_train.shape, X_train.flags.c_contiguous)
print("class counts:", np.bincount(y_major[keep], minlength=3))
```

```text
T = 1500  N = (T-L)//hop + 1 = 29
X: (29, 6, 100) float32  Yw: (29, 100)
win  7 [ 7.0~ 9.0s] counts=[50 50  0] major=idle  center=flick purity=0.50
win  8 [ 8.0~10.0s] counts=[  0 100   0] major=flick center=flick purity=1.00
win  9 [ 9.0~11.0s] counts=[50 50  0] major=idle  center=idle  purity=0.50
win 19 [19.0~21.0s] counts=[50  0 50] major=idle  center=shake purity=0.50
win 20 [20.0~22.0s] counts=[  0   0 100] major=shake center=shake purity=1.00
win 21 [21.0~23.0s] counts=[50  0 50] major=idle  center=idle  purity=0.50
kept: 25 / 29  X_train: (25, 6, 100) True
class counts: [23  1  1]
```

출력에서 볼 것: N = 29가 손계산과 같다. 창 7에서 다수결은 동률이라 `argmax`가 첫 클래스(idle)를 고르고, 중앙 라벨은 flick을 고른다. **규칙에 따라 같은 창의 라벨이 바뀐다.** 그리고 남은 25개 중 23개가 idle이다. 이것이 센서 데이터의 전형적인 class imbalance다. 창을 더 촘촘하게(hop 작게) 자르거나 제스처 구간만 oversample하는 식으로 보완한다(A4).

`X`는 view라서 메모리가 추가로 들지 않지만, `X[keep]`(boolean mask)는 copy다. `np.ascontiguousarray`로 연속 float32 버퍼를 만들어 두면 `torch.from_numpy`가 복사 없이 텐서를 만든다. 크기: 25 × 6 × 100 × 4 B = 60,000 B.

### 6.3 창 라벨 규칙 비교

| 규칙 | 계산 | 장점 | 단점 |
|---|---|---|---|
| 다수결 (majority) | 창 안에서 가장 많은 라벨 | 단순 | 동률 처리 임의, 짧은 제스처가 묻힘 |
| 중앙 (center) | `y[i·hop + L/2]` | 실시간 추론의 "지금"과 대응 | 경계 창에서 라벨 노이즈 |
| 순도 문턱 (purity) | 최다 라벨 비율 ≥ 0.75만 사용 | 깨끗한 학습 데이터 | 데이터 손실, 경계 상황을 못 배움 |
| 이벤트 포함 | 이벤트가 창 안에 완전히 들어가면 양성 | 짧은 이벤트(탭, wake word)에 적합 | 창 길이 ≥ 이벤트 길이 필요 |

펌웨어 연결: 실시간 추론은 "최근 L개 샘플 링버퍼"에서 hop마다 모델을 돌린다. 즉 §3.3의 sliding window가 펌웨어의 링버퍼 + 타이머와 같은 구조다. 학습 때의 hop과 추론 때의 hop이 달라도 되지만, **L과 전처리는 같아야 한다.**

### 6.4 겹치는 창과 leakage (A4 연결)

50% overlap이면 창 i와 i+1은 샘플 50개를 공유한다. 창 단위로 무작위 split하면 test 창의 절반이 train에 **그대로** 들어 있다. 모델이 외운 것을 맞히는 것이라 test 점수가 부풀려진다. A4의 data leakage가 바로 이것이다. 해결은 split 단위를 창이 아니라 **세션·사용자·기기**로 올리는 것이다.

이 코드는 세 세션의 창에 대해 두 split 방식에서 "train과 샘플을 공유하는 test 창"의 비율을 잰다.

```python
import numpy as np
from imu_io import load_log, to_frame, to_grid

L, hop = 100, 50
meta = []                                    # (session, start_idx) per window
for sid in ("s1", "s2", "s3"):
    T = len(to_grid(to_frame(*load_log(f"imu_{sid}.bin"))))
    meta += [(sid, s) for s in range(0, T - L + 1, hop)]
sess = np.array([m[0] for m in meta]); start = np.array([m[1] for m in meta])
print("windows per session:", {s: int((sess == s).sum()) for s in ("s1", "s2", "s3")})

def leak_ratio(test_mask):
    """test 창 중에서 train 창과 샘플을 하나라도 공유하는 비율"""
    tr, te = np.flatnonzero(~test_mask), np.flatnonzero(test_mask)
    same = sess[te][:, None] == sess[tr][None, :]
    overlap = np.abs(start[te][:, None] - start[tr][None, :]) < L
    return (same & overlap).any(axis=1).mean()

rng = np.random.default_rng(42)
rand_test = rng.random(len(meta)) < 0.2      # 창 단위 무작위 20%
sess_test = sess == "s3"                     # 세션(사용자) 단위 split
print("random window split : test=%2d  leak=%.0f%%" % (rand_test.sum(), 100 * leak_ratio(rand_test)))
print("session split (s3)  : test=%2d  leak=%.0f%%" % (sess_test.sum(), 100 * leak_ratio(sess_test)))
```

```text
windows per session: {'s1': 29, 's2': 29, 's3': 29}
random window split : test=18  leak=78%
session split (s3)  : test=29  leak=0%
```

출력에서 볼 것: 무작위 split에서는 test 창의 78%가 train 창과 샘플을 공유한다. 세션 단위 split은 0%다. `leak_ratio`의 `[:, None]`, `[None, :]`은 broadcasting으로 (test 수 × train 수) 쌍 비교표를 한 번에 만든 것이다(§2.2). 실무에서는 한 발 더 나가 **사용자 단위**(userB는 통째로 test)로 나눈다. 같은 사람의 다른 세션도 동작 습관이 비슷해서 약한 leakage가 되기 때문이다. `sklearn.model_selection.GroupKFold`가 이 용도의 표준 도구다.

---

## 7. 창별 특징 추출과 정규화 통계

### 7.1 특징 정의

작은 MCU 모델(예: 작은 MLP, decision tree)은 raw 창 대신 창별 요약값(특징)을 입력으로 쓰기도 한다. 모두 축 L(`axis=-1`)을 따라 접는 reduce 연산이다. x₁ … x_L은 창 하나, 채널 하나의 값이다.

```
mean  μ    = (1/L) ∑ xᵢ
std   σ    = √( (1/L) ∑ (xᵢ − μ)² )           ← ddof=0 (모집단 표준편차)
RMS        = √( (1/L) ∑ xᵢ² )                  (RMS² = σ² + μ²)
p2p        = max(x) − min(x)                   (peak-to-peak)
ZC         = #{ i : sign(xᵢ − μ) ≠ sign(xᵢ₋₁ − μ) }   (평균 기준 zero-crossing 수)
|a|ᵢ       = √(axᵢ² + ayᵢ² + azᵢ²)             → mean, std  (자세와 무관한 특징)
```

말로 하면: mean은 자세(중력 방향), std와 RMS는 움직임 에너지, p2p는 진폭, ZC는 대략적인 주파수(ZC ≈ 2 × 주파수 × 창 길이), |a|는 기기를 어떻게 차든 같은 값이 나오는 방향 불변 특징이다. flick 창(3 Hz, 2 s)이면 ZC ≈ 2 × 3 × 2 = 12, std ≈ 1.5/√2 = 1.06, p2p ≈ 3.0이 기대값이다.

### 7.2 코드 — 벡터화된 특징 + train 통계 저장

이 코드는 6채널 × 5특징 + |a| 2특징 = 32개 특징을 모든 창에 대해 한 번에 계산하고, **train 세션에서만** 정규화 통계를 뽑아 JSON으로 저장한다.

```python
import json
import numpy as np
from numpy.lib.stride_tricks import sliding_window_view
from imu_io import load_log, to_frame, to_grid

def features(X):
    """X: (N, C, L) → (N, F). 모든 연산이 축 L(axis=-1) 방향 reduce."""
    mean = X.mean(axis=-1)
    std = X.std(axis=-1)
    rms = np.sqrt((X ** 2).mean(axis=-1))
    p2p = X.max(axis=-1) - X.min(axis=-1)
    xc = X - mean[..., None]                                  # 평균 기준 zero-crossing
    zc = (np.signbit(xc[..., 1:]) != np.signbit(xc[..., :-1])).sum(axis=-1)
    amag = np.sqrt((X[:, :3] ** 2).sum(axis=1))               # (N, L) 가속도 크기
    extra = np.stack([amag.mean(-1), amag.std(-1)], axis=1)
    return np.concatenate([mean, std, rms, p2p, zc, extra], axis=1).astype(np.float32)

def windows(path, L=100, hop=50):
    g = to_grid(to_frame(*load_log(path)))
    return sliding_window_view(g[["ax", "ay", "az", "gx", "gy", "gz"]].to_numpy(), L, axis=0)[::hop]

F_train = features(np.concatenate([windows("imu_s1.bin"), windows("imu_s2.bin")]))
F_test = features(windows("imu_s3.bin"))
print("F_train:", F_train.shape, " F_test:", F_test.shape)
print("win 8 (flick) ax: mean=%.3f std=%.3f rms=%.3f p2p=%.3f zc=%d" % tuple(F_train[8, [0, 6, 12, 18, 24]]))

mu = F_train.mean(axis=0); sd = F_train.std(axis=0) + 1e-6   # train 통계만!
Z_test = (F_test - mu) / sd
print("Z_test mean(첫 4개):", Z_test.mean(axis=0)[:4].round(2))
with open("norm_stats.json", "w") as f:
    json.dump({"feature_count": int(F_train.shape[1]), "mean": mu.tolist(), "std": sd.tolist(),
               "window": 100, "hop": 50, "fs_hz": 50}, f)
print(open("norm_stats.json").read()[:120], "...")
```

```text
F_train: (58, 32)  F_test: (29, 32)
win 8 (flick) ax: mean=-0.001 std=1.061 rms=1.061 p2p=3.022 zc=11
Z_test mean(첫 4개): [-0.38  0.25  0.34  0.15]
{"feature_count": 32, "mean": [-0.00015500819426961243, -0.00024367761216126382, 0.9997050762176514, -0.0001026022000587 ...
```

출력에서 볼 것: flick 창의 std 1.061, p2p 3.022가 손계산과 같다. ZC가 12가 아니라 11인 것은 사인파가 창 시작점(8.0 s)에서 정확히 0에서 출발해 첫 교차가 창 경계에 걸리기 때문이다. 경계 효과는 흔하니 "대략 맞는지"로 본다. `Z_test`의 평균이 0이 아닌 것은 정상이다. test는 train 통계로 정규화하므로 분포 차이가 그대로 드러난다. **test 통계로 정규화하면 그 자체가 leakage**다.

### 7.3 ddof 함정 — numpy와 pandas의 std는 다르다

이 코드는 같은 4개 값의 표준편차를 세 방법으로 계산한다.

```python
import numpy as np
import pandas as pd

v = np.array([1.0, 2.0, 3.0, 4.0])
print("np.std      (ddof=0):", v.std())
print("pd.Series.std (ddof=1):", pd.Series(v).std())
print("np.std(ddof=1)       :", v.std(ddof=1))
```

```text
np.std      (ddof=0): 1.118033988749895
pd.Series.std (ddof=1): 1.2909944487358056
np.std(ddof=1)       : 1.2909944487358056
```

손계산: 평균 2.5, 편차² 합 = 2.25 + 0.25 + 0.25 + 2.25 = 5. ddof=0이면 √(5/4) = 1.118, ddof=1이면 √(5/3) = 1.291. 말로 하면 numpy는 N으로, pandas는 N−1로 나눈다. 창 길이 100이면 차이는 약 0.5%지만, 펌웨어가 N으로 나누는데 학습 전처리가 pandas(N−1)였다면 모든 std 특징이 조금씩 어긋난다. **펌웨어에서 할 계산과 같은 ddof를 명시**한다.

### 7.4 C로 같은 특징 계산 — golden vector 비교

이 C 프로그램은 같은 로그의 같은 창(s1, 400번째 레코드부터 100개, 8.0 s)에서 ax 특징을 펌웨어 스타일로 계산한다.

```c
/* feat.c — Python features()와 같은 계산을 펌웨어 스타일 C로: 창 하나, 채널 하나 */
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#define L 100
typedef struct __attribute__((packed)) { uint32_t ts; int16_t a[3], g[3]; } imu_rec_t;

int main(void) {
    FILE *f = fopen("imu_s1.bin", "rb");
    if (!f) { perror("fopen"); return 1; }
    imu_rec_t r[L];
    fseek(f, 16 + 400 * (long)sizeof(imu_rec_t), SEEK_SET);   /* 헤더 + 400번째 레코드(8.0 s) */
    if (fread(r, sizeof r[0], L, f) != L) { fclose(f); return 1; }
    fclose(f);

    float x[L], sum = 0, sq = 0, mn = 1e9f, mx = -1e9f;
    for (int i = 0; i < L; i++) {
        x[i] = r[i].a[0] / 8192.0f;                            /* ax: count → g */
        sum += x[i]; sq += x[i] * x[i];
        if (x[i] < mn) mn = x[i];
        if (x[i] > mx) mx = x[i];
    }
    float mean = sum / L;
    float var = 0;
    for (int i = 0; i < L; i++) var += (x[i] - mean) * (x[i] - mean);
    float stdv = sqrtf(var / L);                               /* ddof=0 — numpy 기본과 같게 */
    int zc = 0;
    for (int i = 1; i < L; i++) zc += ((x[i] - mean) < 0) != ((x[i - 1] - mean) < 0);
    printf("C   ax: mean=%.3f std=%.3f rms=%.3f p2p=%.3f zc=%d\n",
           mean, stdv, sqrtf(sq / L), mx - mn, zc);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 feat.c -o feat -lm && ./feat
```

```text
C   ax: mean=-0.001 std=1.061 rms=1.061 p2p=3.022 zc=11
```

출력에서 볼 것: Python(§7.2의 `win 8`)과 소수점 셋째 자리까지 같다. 이 비교가 **golden vector 테스트**다. 실제 프로젝트에서는 Python이 수백 개 창의 입력·기대 출력을 파일로 떨궈 두고, 펌웨어 유닛 테스트(또는 호스트 빌드)가 같은 입력으로 계산해 허용 오차(예: 1e-5 상대 오차) 안인지 확인한다. 이 창은 s1의 드롭(14 s) 이전이라 레코드 인덱스가 격자 인덱스와 같아서 바로 비교할 수 있었다.

---

## 8. matplotlib 기본 — 그림을 PNG로 남기기

matplotlib은 `Figure`(캔버스) 안에 `Axes`(좌표계 하나)를 여러 개 두고 각 Axes에 그린다. `plt.subplots(2, 1, sharex=True)`는 x축을 공유하는 위아래 두 칸을 만든다. 자주 쓰는 것은 네 가지다.

| 함수 | 용도 | 센서 로그에서 |
|---|---|---|
| `ax.plot(t, y)` | 선 그래프 | 3축 시계열 |
| `ax.axvspan(t0, t1, alpha=…)` | 세로 음영 구간 | 라벨 구간 표시 |
| `ax.axhline(y, ls="--")` | 가로 기준선 | ±FS, 문턱값 |
| `ax.hist(v, bins=…)` | 히스토그램 | 값 분포, 포화 확인 |

이 코드는 §4.6의 그림 3과 §5.2의 그림 4를 합친 개요 그림, 그리고 ay 히스토그램을 PNG 파일로 저장한다.

```python
import matplotlib
matplotlib.use("Agg")                     # 화면 없이 파일로만 (서버·CI에서도 동작)
import matplotlib.pyplot as plt
import numpy as np
from imu_io import load_log, to_frame, LABELS

df = to_frame(*load_log("imu_s1.bin"))
t = (df.index - df.index[0]).total_seconds()

fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 5), sharex=True)
for c in ("ax", "ay", "az"):
    ax1.plot(t, df[c], lw=0.8, label=c)
for s, e, lab in LABELS.itertuples(index=False):
    ax1.axvspan(s, e, alpha=0.15, color="tab:orange")        # 라벨 구간 음영
    ax1.text((s + e) / 2, 4.2, lab, ha="center")
ax1.axhline(32767 / 8192, ls="--", lw=0.8, color="gray")      # full-scale 선
ax1.set_ylabel("accel [g]"); ax1.legend(loc="upper right", ncol=3)
dt_ms = np.diff(df.index.asi8) / 1e6
ax2.plot(t[1:], dt_ms, ".", ms=2)
ax2.set_ylabel("dt [ms]"); ax2.set_xlabel("time [s]")
fig.tight_layout()
fig.savefig("imu_s1_overview.png", dpi=120)

fig2, ax = plt.subplots(figsize=(5, 3))
ax.hist(df["ay"], bins=80, log=True)                          # 포화는 양 끝 막대로 보인다
ax.set_xlabel("ay [g]"); ax.set_ylabel("count (log)")
fig2.tight_layout(); fig2.savefig("imu_s1_ay_hist.png", dpi=120)
plt.close("all")
import os
print({p: os.path.getsize(p) > 0 for p in ("imu_s1_overview.png", "imu_s1_ay_hist.png")})
```

```text
{'imu_s1_overview.png': True, 'imu_s1_ay_hist.png': True}
```

출력에서 볼 것: 파일 두 개가 생긴다(이 노트에는 PNG를 넣지 않고, 같은 데이터로 그린 SVG를 그림 3, 4로 넣었다). 히스토그램은 log 스케일로 그려야 ±4 g 양 끝에 쌓인 포화 막대가 보인다. 선형 스케일이면 idle의 거대한 0 근처 막대에 묻힌다.

팁:
- `matplotlib.use("Agg")`는 창을 띄우지 않고 파일만 만든다. CI나 원격 서버에서 로그를 자동 리포트할 때 필수다.
- 반복 실행하는 스크립트에서는 `plt.close("all")`로 figure를 닫는다. 안 닫으면 메모리가 계속 는다.
- 점이 수백만 개면 `plot`이 느리다. 구간을 잘라 그리거나, 그림 4처럼 bin마다 min/max만 그린다(오실로스코프의 peak-detect 모드와 같은 아이디어).

---

## 9. Jupyter 워크플로와 데이터 sanity 체크리스트

### 9.1 Jupyter 사용 팁

Jupyter notebook은 코드 셀을 하나씩 실행하며 결과(표, 그림)를 바로 보는 대화형 환경이다. 센서 로그 탐색에 좋지만 습관이 나쁘면 재현이 안 된다.

- 파일 로딩은 맨 위 셀 한 곳에서. 셀 순서를 뒤섞어 실행한 상태를 믿지 말고, 결론을 내기 전에 "Restart & Run All"로 처음부터 다시 돌린다.
- 검증된 함수는 노트북에서 `imu_io.py` 같은 모듈로 옮긴다. `%load_ext autoreload` + `%autoreload 2`를 쓰면 모듈을 고치면 바로 반영된다.
- `%timeit f(x)`로 §3.1 같은 시간 측정을 한 줄로 한다.
- `%matplotlib inline`(기본)은 그림을 셀 아래에 붙인다. 확대가 필요하면 `%matplotlib widget`(ipympl 설치 필요)을 쓴다.
- 노트북은 탐색용, 파이프라인은 `.py` 스크립트로. 결과 그림은 `savefig`로 파일로 남겨 리포트에 붙인다.

### 9.2 데이터 sanity 체크리스트

학습 전에 모든 로그에 자동으로 돌리는 검사다. Don이 SSD telemetry에서 하던 health check와 같은 발상이다.

| 검사 | 증상 | 벡터화된 검사 방법 |
|---|---|---|
| 포화 / clipping | 값이 ±32767/−32768에 붙음, 사인파 봉우리가 평평 | `np.isin(raw, [32767, -32768]).mean(axis=0)` |
| 멈춘 센서 (stuck) | 일정 시간 값이 완전히 같음, std = 0 | `df.rolling(25).std() == 0` |
| NaN / 결측 | 보간 한계를 넘은 구멍 | `df.isna().sum()` |
| 드롭 샘플 | dt > 1.5 × period | `np.diff(ts) > 1.5 × period` |
| 비단조 timestamp | dt ≤ 0 (재부팅, wrap, 버퍼 순서 꼬임) | `(np.diff(ts) > 0).all()` |
| 클럭 drift | 기기 50 Hz가 실제로는 50.2 Hz → 1시간에 약 14 s 어긋남 | `1e6 / median(dt)` vs 호스트 시계, 긴 세션의 기울기 |
| 단위 오류 | 정지 시 ‖a‖ ≠ 1 g, gyro가 57.3배 차이(rad/s vs dps) | 정지 구간 `norm(acc).mean()` ≈ 1 |
| 축 방향 | 기기를 뒤집어 차면 중력 축·부호가 바뀜 | 정지 구간 평균이 가장 큰 축과 부호 |
| 잘린 파일 | 레코드 수 ≠ 헤더의 n_records | `len(rec) == hdr["n_records"]` |

이 코드는 위 체크 중 일부를 한 함수로 묶어 로그마다 딕셔너리 리포트를 낸다.

```python
import numpy as np
from imu_io import load_log, to_frame

def sanity_report(hdr, rec, fs=50):
    df = to_frame(hdr, rec)
    ts = rec["timestamp_us"].astype(np.int64)
    dt = np.diff(ts)
    acc = np.stack([rec[c] for c in ("ax", "ay", "az")], axis=1)
    rest = np.linalg.norm(df[["ax", "ay", "az"]].to_numpy()[:100], axis=1).mean()
    stuck = (df.rolling(25).std() == 0).any()               # 0.5 s 동안 값이 완전히 같음
    return {
        "monotonic": bool((dt > 0).all()),
        "odr_est_hz": round(float(1e6 / np.median(dt)), 2),
        "drop_events": int((dt > 1.5e6 / fs).sum()),
        "sat_frac_%": np.round(100 * ((acc >= 32767) | (acc <= -32768)).mean(axis=0), 2).tolist(),
        "nan_count": int(df.isna().sum().sum()),
        "stuck_channels": stuck[stuck].index.tolist(),
        "rest_|a|_g": round(float(rest), 3),                 # 1.0 근처가 아니면 단위/FS 의심
        "rest_gravity_axis": ["ax", "ay", "az"][int(np.abs(df[["ax", "ay", "az"]].iloc[:100].mean()).argmax())],
    }

for p in ("imu_s1.bin", "imu_s3.bin"):
    print(p, sanity_report(*load_log(p)))
```

```text
imu_s1.bin {'monotonic': True, 'odr_est_hz': 50.0, 'drop_events': 2, 'sat_frac_%': [0.0, 2.68, 0.0], 'nan_count': 0, 'stuck_channels': [], 'rest_|a|_g': 0.999, 'rest_gravity_axis': 'az'}
imu_s3.bin {'monotonic': True, 'odr_est_hz': 50.0, 'drop_events': 1, 'sat_frac_%': [0.0, 2.67, 0.0], 'nan_count': 0, 'stuck_channels': [], 'rest_|a|_g': 1.0, 'rest_gravity_axis': 'az'}
```

출력에서 볼 것: ay 포화 2.7%(40/1494), s1 드롭 이벤트 2회, s3 1회가 잡혔다. fleet 규모(H7)에서는 이 딕셔너리를 로그마다 DB에 쌓고, FW 버전별로 `groupby`해서 "v1.3부터 드롭이 늘었다" 같은 회귀를 찾는다. 정지 구간을 "처음 100샘플"로 가정한 것은 이 데이터에서만 맞는 단순화다. 실제로는 §5.4의 rolling std가 작은 구간을 찾아서 쓴다.

---

## 10. 임베디드 관점에서 다시 보기

### 10.1 로그 포맷은 펌웨어와 데이터 파이프라인의 계약이다

- 헤더에 magic, version, ODR, FS range, 센서 모델, FW 버전을 넣는다. 파이썬 파서는 version을 보고 dtype을 고른다.
- 레코드는 packed + "큰 필드 먼저" 순서로 자연 정렬을 유지한다(§4.5).
- 샘플마다 timestamp를 넣는 대신 "FIFO 배치마다 timestamp 1개 + 샘플 N개"로 쓰면 저장 용량이 줄어든다. 이 경우 Python 쪽에서 `np.repeat`와 `np.arange`로 샘플 시각을 복원한다. 샘플 카운터(seq) 필드가 있으면 드롭 검출이 timestamp보다 확실하다.
- 용량 계산 예: 레코드 16 B × 50 Hz = 800 B/s = 약 2.9 MB/시간. 하루 종일(16시간) 모으면 약 46 MB다. flash 예산(H1)과 BLE 업로드 시간(H2)을 이 숫자로 따진다.

### 10.2 정규화 통계를 펌웨어로 — JSON → C 헤더

학습 파이프라인이 만든 통계를 사람이 손으로 옮기면 반드시 틀린다. 빌드 과정에서 헤더를 **자동 생성**한다. 이 코드는 §7.2의 `norm_stats.json`을 C 헤더로 바꾼다.

```python
import json

st = json.load(open("norm_stats.json"))
n = st["feature_count"]
fmt = lambda xs: ", ".join(f"{x:.7g}f" for x in xs)
lines = [
    "/* auto-generated from norm_stats.json — do not edit */",
    "#pragma once",
    f"#define FEAT_COUNT {n}",
    f"#define WIN_LEN {st['window']}",
    f"#define WIN_HOP {st['hop']}",
    f"static const float FEAT_MEAN[FEAT_COUNT] = {{ {fmt(st['mean'])} }};",
    f"static const float FEAT_INV_STD[FEAT_COUNT] = {{ {fmt(1.0 / s for s in st['std'])} }};",
]
open("norm_stats.h", "w").write("\n".join(lines) + "\n")
for l in lines:
    print(l[:96] + (" ..." if len(l) > 96 else ""))
```

```text
/* auto-generated from norm_stats.json — do not edit */
#pragma once
#define FEAT_COUNT 32
#define WIN_LEN 100
#define WIN_HOP 50
static const float FEAT_MEAN[FEAT_COUNT] = { -0.0001550082f, -0.0002436776f, 0.9997051f, -0.0001 ...
static const float FEAT_INV_STD[FEAT_COUNT] = { 597.3713f, 454.8561f, 511.923f, 12.51024f, 10.08 ...
```

펌웨어는 이 헤더를 include해서 나눗셈 대신 곱셈으로 정규화한다(MCU에서 FP 나눗셈은 곱셈보다 수 배 느리다).

```c
#include <stdio.h>
#include "norm_stats.h"
static void normalize(const float *f, float *z) {
    for (int i = 0; i < FEAT_COUNT; i++) z[i] = (f[i] - FEAT_MEAN[i]) * FEAT_INV_STD[i];
}
int main(void) {
    float f[FEAT_COUNT] = {0}, z[FEAT_COUNT];
    f[2] = 1.0f;                                 /* az mean = 1 g, 나머지 0 */
    normalize(f, z);
    printf("z[2] = %.3f, sizeof tables = %zu B\n", z[2], sizeof FEAT_MEAN + sizeof FEAT_INV_STD);
    return 0;
}
```

```text
z[2] = 0.151, sizeof tables = 256 B
```

출력에서 볼 것: 테이블은 32 × 4 B × 2 = 256 B로 flash에 들어간다. 그리고 `FEAT_INV_STD` 첫 값이 597이다. train 창들의 "ax 평균" 특징이 거의 변하지 않아 std가 0.0017 g밖에 안 되기 때문이다. 이런 특징은 작은 bias 변화(온도, 다른 기기)에도 z값이 수십으로 튄다. 분산이 너무 작은 특징은 빼거나 std에 하한(floor)을 둔다. 이것도 데이터를 **숫자로 들여다봐야** 보이는 문제다.

### 10.3 메모리와 연산량

| 항목 | 계산 | 크기 |
|---|---|---|
| raw 링버퍼 (int16, 6ch, L=100) | 100 × 6 × 2 B | 1,200 B |
| float32 창 (모델 입력 `[1, 6, 100]`) | 100 × 6 × 4 B | 2,400 B |
| 특징 32개 + 정규화 테이블 | 32 × 4 + 256 | 384 B |
| 특징 계산 연산량 | 6ch × 100 × (mean, 제곱, 비교 …) 수 회 | 창당 수천 FLOP |

50 Hz에 hop 50이면 초당 1번 추론이다. 특징 계산은 창당 수천 번의 FP 연산이라 Cortex-M4F(수십~100 MHz급)에서 대략 수십 µs 수준으로 예상되고(실측은 K 모듈에서), 전력 예산에 거의 영향이 없다. 추론 주기와 hop을 정하는 것이 곧 duty cycle 설계다(D, K 모듈). 또한 int16 링버퍼를 유지하고 창을 꺼낼 때만 float로 바꾸면 SRAM을 절반으로 쓴다. 이것이 Python 쪽에서 raw를 int16으로 오래 들고 다니는 이유와 같다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| int16 raw를 그대로 제곱 | RMS·에너지가 이상하게 작거나 음수 | int16 wrap-around | `astype(np.float32)` 후 계산 |
| float64로 학습 입력 생성 | PyTorch "Double and Float" 에러, 메모리 2배 | numpy 기본 dtype | 변환 시점부터 float32 명시 |
| dtype에 endianness 생략 | 다른 기기 로그에서 쓰레기 값 (에러 없음) | `"i2"` = 호스트 순서 | `"<i2"`처럼 항상 명시 |
| 비packed 구조체를 fwrite | 파서 오프셋이 몇 바이트씩 밀림 | 컴파일러 padding | packed + `assert itemsize == sizeof` |
| 창 전처리를 view에 in-place | 원본 로그가 바뀌어 다음 창 결과가 이상 | slicing은 view | 새 배열 반환 또는 `.copy()` |
| 드롭을 무시하고 샘플 개수로 창 자름 | 창 길이가 실제로 2 s보다 김, 제스처 속도 왜곡 | timestamp 미사용 | 고정 격자 재샘플 후 윈도잉 |
| 창 단위 무작위 split | test 정확도는 높은데 실사용에서 급락 | overlap leakage | 세션·사용자 단위 split (GroupKFold) |
| test 포함 전체로 정규화 | 오프라인 점수 과대평가 | 통계 leakage | train 통계만 저장해서 재사용 |
| pandas std와 펌웨어 std 혼용 | golden vector가 0.5% 어긋남 | ddof=1 vs 0 | ddof를 명시하고 C와 맞춤 |
| uint32 timestamp를 int64로 차분 | 71.6분 지점에서 dt = −42억 | 카운터 wrap | uint32 차분 후 cumsum으로 unwrap |

---

## 12. 면접에서 이렇게 말한다

**Q.** "How would you turn raw IMU logs into a training dataset?"

**A.** 단계를 순서대로 말한다. 헤더로 포맷·FS 확인 → 벡터화된 파싱과 단위 변환 → sanity 체크(드롭, 포화, stuck, 단위) → 고정 rate 격자로 재샘플하고 짧은 구멍만 보간 → 라벨 구간 병합 → 고정 창 + hop으로 윈도잉하고 순도 규칙으로 라벨 → 사용자 단위 split → train 통계로 정규화하고 그 통계를 펌웨어로 export.

> I parse the binary logs with a structured dtype that mirrors the firmware struct, convert counts to physical units using the full-scale range stored in the header, and run sanity checks for dropped samples, saturation and stuck channels. Then I resample onto a fixed-rate grid, interpolating only short gaps, attach labels from the interval table with an as-of join, and cut fixed-length windows with a hop, for example 2-second windows at 50 Hz with 50 percent overlap, which gives an X of shape N by channels by length. I split by user or device, never by window, and I export the normalization stats from the training split so the firmware applies exactly the same preprocessing.

**Q.** "How do you detect dropped samples?"

**A.** timestamp 차분이 1.5 × period를 넘으면 드롭이고, round(dt/period) − 1이 빠진 개수다. 가능하면 seq 카운터를 로그에 넣는 것이 더 확실하다. uint32 µs 카운터는 71.6분마다 wrap하므로 모듈러 차분을 쓴다. 드롭 비율은 품질 지표로 FW 버전별로 모니터링한다.

> I diff the timestamps and flag any interval above 1.5 times the nominal period; round of dt over period minus one gives the number of lost samples. A sequence counter in each record is even more robust than timestamps. I handle 32-bit microsecond counter wrap with modular differences, and I track the drop rate per firmware version, because a jump usually means a FIFO watermark or bus-contention problem on the device.

**Q.** "Why float32 over float64?"

**A.** 메모리·대역폭이 절반이고, PyTorch와 배포 타깃(MCU FPU, DSP, NPU)이 float32 또는 int8이다. 16-bit 센서의 정보는 float32의 24-bit 가수부 안에 충분히 들어가므로 정밀도 손실이 없다. 무엇보다 펌웨어와 같은 정밀도로 golden vector를 만들 수 있다.

> Float32 halves memory and bandwidth, matches PyTorch's default parameter type, and matches what the target actually runs, since a Cortex-M FPU, a DSP or an NPU uses single precision or int8. A 16-bit sensor has far less information than float32's 24-bit mantissa, so nothing is lost, and keeping the host pipeline in float32 lets me generate golden vectors that the firmware should reproduce bit-for-bit or within a tight tolerance.

**Q.** "What goes wrong if windows overlap across train and test?"

**A.** 50% overlap이면 이웃 창이 샘플 절반을 공유한다. 창 단위로 무작위 split하면 test 창 대부분이 train에 있는 샘플을 포함해서(실험에서 78%) 점수가 부풀려지고, 실사용에서 성능이 급락한다. 세션·사용자·기기 단위로 split한다.

> Neighboring windows share samples, so a random window-level split puts most test windows next to, and partly inside, training windows. In a quick experiment with 50 percent overlap, 78 percent of test windows shared samples with the training set. The model is then graded on data it has effectively seen, and the metric collapses on new users. I split by session, user or device, typically with a group k-fold.

**Q.** "How do you make sure your Python preprocessing matches the firmware?"

**A.** 같은 정의를 한 곳에서 관리한다. 창 길이·hop·정규화 통계는 JSON에서 C 헤더로 자동 생성하고, ddof·단위·축 순서를 명시한다. Python이 golden vector(입력 창 + 기대 특징)를 만들고 펌웨어 유닛 테스트가 허용 오차 안인지 확인한다.

> I treat preprocessing as a contract: window length, hop and normalization constants are generated into a C header from the training artifacts, and details like the standard-deviation convention and units are explicit. Python emits golden vectors, raw window in and expected features out, and a host-built firmware unit test checks that the C implementation matches within a small tolerance.

**Q.** "What sanity checks do you run on sensor logs?"

**A.** 포화 비율, stuck(rolling std = 0), NaN, 드롭, timestamp 단조성, 추정 ODR과 클럭 drift, 정지 시 |a| ≈ 1 g(단위 확인), 중력 축(착용 방향) 확인. 로그마다 리포트를 만들어 FW 버전·기기별로 집계한다.

> For every log I compute saturation fraction per axis, stuck channels via a rolling standard deviation of zero, NaNs, drop events, timestamp monotonicity, the estimated output data rate, and physical checks like the accelerometer magnitude being about 1 g at rest and gravity on the expected axis. Those go into a per-log report that I aggregate by firmware version and device to catch regressions early.

---

## 13. 직접 해보기

1. 손계산: 60 s, 50 Hz 로그(T = 3000)에서 L = 128, hop = 64로 창을 자르면 창은 몇 개이고, 끝에서 버려지는 샘플은 몇 개인가?
정답: N = ⌊(3000 − 128)/64⌋ + 1 = 44 + 1 = 45개. 마지막 창 끝 = 44 × 64 + 128 = 2944이므로 56샘플이 버려진다.

2. 손계산: ±4 g 설정에서 raw ax = −12288, ±500 dps에서 raw gz = 13107이다. 물리값은? 또 실제 센서는 ±8 g였는데 스크립트가 ±4 g로 변환했다면 정지 시 |a|는 얼마로 보이는가?
정답: −12288/8192 = −1.5 g, 13107/65.536 ≈ 200.0 dps. ±8 g면 1 g = 4096 count인데 8192로 나누므로 |a| ≈ 0.5 g로 보인다.

3. 손계산 + C: `struct { uint8_t flags; int16_t ax; uint32_t ts; }`의 비packed sizeof와 각 오프셋, packed sizeof는? numpy `align=True` dtype으로 확인하라.
정답: flags 0, (pad 1), ax 2, ts 4 → sizeof 8. packed는 7. `np.dtype([("flags","u1"),("ax","<i2"),("ts","<u4")], align=True).itemsize == 8`.

4. 코드: §6.2 예제에서 hop을 25(75% overlap)로 바꾸면 s1의 창은 몇 개인가? §6.4의 `leak_ratio`로 무작위 split의 leak 비율이 어떻게 변하는지 확인하라.
정답: N = ⌊1400/25⌋ + 1 = 57개. 실행해 보면 세션당 57개, 무작위 split의 leak은 78%에서 100%로 오르고 세션 split은 여전히 0%다. overlap이 커질수록 test 창 하나가 샘플을 공유하는 train 창이 ±1개에서 ±3개로 늘기 때문이다.

5. 코드: `imu_s2.bin`을 읽은 뒤 `rec = rec.copy(); rec["az"][500:550] = 8190`으로 1 s 동안 az를 고정시키고 `sanity_report`를 돌려라. 무엇이 잡히는가? `.copy()`를 먼저 하는 이유는?
정답: `stuck_channels`에 `['az']`가 나온다. `np.fromfile` 결과는 쓰기 가능해서 `.copy()` 없이도 동작하지만, 같은 배열을 다른 분석에서 계속 쓰고 있다면 조작한 값이 거기까지 번진다(§3.2 view vs copy). 테스트용 결함 주입은 항상 복사본에 한다.

6. 코드: §5.4의 움직임 검출기를 `rolling(25, center=True)`로 바꾸면 검출 구간의 끝 지연이 어떻게 변하는가? 이것을 펌웨어에서 그대로 쓸 수 없는 이유는?
정답: 실행하면 구간이 (7.84, 10.18), (19.78, 22.24) s가 된다. 끝 지연이 약 0.44 s에서 0.18 s로 줄고, 대신 시작이 약 0.2 s 일찍 잡힌다(미래 샘플을 보므로). 펌웨어에서 같은 결과를 내려면 결과를 반 창(0.24 s)만큼 늦게 내야 하므로 실시간 이득은 없다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| ndarray | numpy N차원 배열 | 포인터 + dtype + shape + strides |
| dtype | 원소 타입 | `int16`, `float32`, structured dtype 등 |
| structured dtype | 필드가 있는 dtype | C 구조체를 numpy에 기술, 파일을 레코드 배열로 읽음 |
| strides | 축별 바이트 간격 | 주소 = base + ∑ index × stride |
| axis | 연산이 접는 축 | axis=0은 샘플 방향, 채널별 통계 |
| broadcasting | shape 자동 확장 규칙 | 뒤 축부터 비교, 1이면 늘림 |
| boolean mask | True/False 배열로 선택 | `a[a > 1]`, 결과는 copy |
| fancy indexing | 정수 배열로 선택 | gather, 결과는 copy |
| view | 버퍼를 공유하는 배열 | slicing, transpose, reshape |
| vectorization | 루프를 배열 연산으로 | C/SIMD 루프 한 번 |
| endianness | 바이트 순서 | `<` little, `>` big |
| padding | 정렬용 빈 바이트 | `__attribute__((packed))`로 제거 |
| full-scale range (FS) | 센서 측정 범위 | ±4 g → 8192 LSB/g |
| saturation | 범위 초과로 잘림 | ±32767 근처에 값이 붙음 |
| DatetimeIndex | 시간 인덱스 | 시간 구간 선택·resample·rolling |
| resample | 시간 간격 재조정 | 불규칙 → 고정 격자, 다운샘플 |
| rolling | 이동 창 통계 | causal 기본, `center=True`는 오프라인용 |
| merge_asof | 가장 가까운 이전 키로 조인 | 라벨 구간을 샘플에 붙임 |
| groupby | 키별 분할·집계 | 세션·사용자·라벨별 통계 |
| window / hop | 창 길이 / 이동 간격 | overlap = 1 − hop/L |
| purity | 창 안 최다 라벨 비율 | 애매한 창 거르기 |
| data leakage | test 정보가 train에 새는 것 | 겹친 창, test 통계로 정규화 |
| ddof | 분산 분모 보정 | numpy 0, pandas 1 |
| golden vector | 기준 입력·출력 쌍 | Python과 펌웨어 결과 비교 |

---

## 15. 요약 & 체크리스트

numpy 배열은 포인터·dtype·shape·strides로 된 C 배열이고, slicing·transpose·sliding window는 strides만 바꾸는 view다. ML 데이터는 float32로 다루고, raw int16은 제곱 전에 올린다. 펌웨어 로그는 packed 구조체와 1:1인 structured dtype(`<` 명시)으로 `np.fromfile` 한 번에 읽고, 헤더의 FS로 `raw × R / 32768` 단위 변환을 한다. pandas의 시간 인덱스로 드롭을 찾고(dt > 1.5 × period), 고정 격자에 맞춰 짧은 구멍만 보간하고, `merge_asof`로 라벨을 붙인다. `sliding_window_view(sig, L, axis=0)[::hop]`이 바로 `[N, C, L]`을 주며, 창 수는 ⌊(T−L)/hop⌋ + 1이다. 겹친 창 때문에 split은 반드시 세션·사용자 단위로 하고, 정규화 통계는 train에서만 뽑아 C 헤더로 펌웨어에 넘긴 뒤 golden vector로 일치를 확인한다.

- [ ] int16 곱셈 overflow 결과(예: 32767² → 1)를 손으로 계산할 수 있다
- [ ] (4, 3) 행렬의 axis=0, axis=1 평균을 손으로 구하고 shape을 말할 수 있다
- [ ] broadcasting이 되는 shape 쌍과 안 되는 쌍을 구별할 수 있다
- [ ] slicing(view)과 fancy indexing(copy)의 차이와 in-place 함정을 설명할 수 있다
- [ ] C 구조체의 padding을 손으로 계산하고 structured dtype(`align=True`)으로 맞출 수 있다
- [ ] raw count를 FS range로 g, dps로 바꾸는 공식을 쓸 수 있다
- [ ] timestamp 차분으로 드롭 위치와 개수를 계산하고 uint32 wrap을 처리할 수 있다
- [ ] 창 개수 N = ⌊(T−L)/hop⌋ + 1을 계산하고 `[N, C, L]` 배열을 만들 수 있다
- [ ] 겹친 창이 leakage를 만드는 이유와 사용자 단위 split을 설명할 수 있다
- [ ] 특징·정규화 통계를 JSON → C 헤더로 넘기고 ddof를 맞출 수 있다

---

## 참고 자료

- NumPy 공식 문서 — "NumPy: the absolute basics for beginners", "Broadcasting", "Copies and views", "Structured arrays": https://numpy.org/doc/stable/
- `numpy.lib.stride_tricks.sliding_window_view` API 문서: https://numpy.org/doc/stable/reference/generated/numpy.lib.stride_tricks.sliding_window_view.html
- pandas 공식 문서 — "Time series / date functionality", "Window operations", `merge_asof`: https://pandas.pydata.org/docs/
- Matplotlib 공식 튜토리얼 — "Quick start guide": https://matplotlib.org/stable/users/explain/quick_start.html
- Jake VanderPlas, "Python Data Science Handbook" (O'Reilly) — numpy·pandas·matplotlib 입문, 온라인 무료판: https://jakevdp.github.io/PythonDataScienceHandbook/
- Wes McKinney, "Python for Data Analysis" 3판 (O'Reilly) — pandas 저자의 책, 온라인판: https://wesmckinney.com/book/
- Python `struct` 모듈 문서: https://docs.python.org/3/library/struct.html
- scikit-learn `GroupKFold` (그룹 단위 교차검증): https://scikit-learn.org/stable/modules/generated/sklearn.model_selection.GroupKFold.html
