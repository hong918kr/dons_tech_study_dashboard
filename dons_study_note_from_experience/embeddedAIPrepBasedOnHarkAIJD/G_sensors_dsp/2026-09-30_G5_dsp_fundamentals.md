# G5. DSP 기초 — 샘플링·aliasing, FIR/IIR, window, FFT/STFT, mel·MFCC, 고정소수점 구현

> **이 노트를 다 읽으면**: 7 kHz 톤이 10 kHz 샘플링에서 왜 3 kHz로 보이는지 계산하고 48 → 16 kHz 리샘플링 필터를 설계할 수 있다 · FIR(windowed-sinc, `firwin`)과 IIR(biquad, `butter(..., output="sos")`)을 설계해서 C(float·Q15)로 구현하고 scipy와 숫자로 대조할 수 있다 · DFT/FFT의 bin·해상도·leakage·window·zero-padding을 설명하고 radix-2 FFT를 C로 짤 수 있다 · STFT → mel → log → (DCT) → 정규화 파이프라인을 스트리밍 C 코드로 만들고 frame당 연산량·메모리를 계산할 수 있다
> **JD 연결**: "(우대) Audio/Voice/Vision models", "Hands-on with IMUs, accelerometers, gyroscopes, **microphones**", "Optimizing models for MCUs & edge processors (Cortex-M/A, RISC-V, **DSP**)" — study_prep_list **G5**: 샘플링, Nyquist, aliasing / FIR/IIR 필터, windowing, **FFT/STFT** / **mel spectrogram, MFCC** (오디오 모델 입력) / 고정소수점 DSP 구현, CMSIS-DSP. 행 설명 그대로 "모델 전처리 = DSP"(P0). 프로젝트 **PJ2**(마이크 → MFCC → DS-CNN)의 앞단 전부
> **Don 기준 난이도**: Q15 곱셈·포화, 링버퍼, DMA 블록 처리, 사이클 측정은 이미 몸에 있다(E4에서 정리) / 신호를 "주파수"로 보는 눈 — z-평면의 극점·영점, leakage와 window, 시간-주파수 해상도 trade-off, mel·log·DCT가 왜 그렇게 생겼는지 — 는 새로 배운다. 응용수학의 Fourier 급수 기억을 다시 꺼내면 된다
> **선행 노트**: B5 1절(STFT·mel·MFCC 속성 강의, numpy vs torchaudio 함정), E4 2~3절(Q15·guard bit·FIR SNR·block floating point·원형 버퍼), C1(양자화 잡음 Δ²/12), A2(정규분포·분산, 표준화), B2(DL conv = correlation), B3(RNN 상태 = IIR, deadband), B7(IMU 특징). 같이 쓰는 노트: G4(PDM·CIC decimation, 병렬 작성)

---

## 0. 큰 그림 — 이게 왜 필요한가

edge ML 엔지니어가 "모델을 기기에 올린다"고 할 때, 모델 앞에는 거의 항상 **신호 처리(DSP)** 가 붙어 있다. 키워드 스포팅(KWS) 모델은 raw 오디오가 아니라 log-mel spectrogram을 먹고, IMU 제스처 모델은 필터링된 가속도와 RMS·주파수 특징을 먹는다. 학습할 때는 이 전처리를 Python 한 줄(`torchaudio.transforms.MelSpectrogram(...)`)로 했지만, 기기에서는 **Don이 C로 다시 짜야 한다**. 그리고 그 C 코드가 Python과 frame 위치·window·FFT 크기·mel 공식·log 밑·ε·정규화 상수까지 **전부 같아야** 모델이 학습 때 본 입력을 받는다.

B5 1절은 "log-mel이 무엇인지"를 속성으로 보여 줬고, E4는 "DSP 칩이 Q15 MAC을 어떻게 싸게 하는지"를 보여 줬다. 이 노트는 그 사이에 있는 **신호와 시스템(signals & systems)의 체계적인 기초**다. 왜 16 kHz인가, 왜 window를 곱하나, 왜 FIR은 선형 위상인데 IIR은 아닌가, 왜 고차 IIR은 2차 구간으로 쪼개나, 왜 zero-padding으로 해상도가 좋아지지 않나 — 면접에서 "왜?"를 두 번 물어도 버틸 수 있게 만드는 것이 목표다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="g5a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <text x="10" y="18" font-size="13">오디오 front-end = DSP 블록의 줄 (괄호 = 이 노트의 절 번호)</text> <rect x="10" y="35" width="90" height="56" rx="6" fill="#888" fill-opacity="0.15" stroke="#888"/> <text x="55" y="58" font-size="12" text-anchor="middle">마이크</text> <text x="55" y="76" font-size="12" text-anchor="middle">+ AAF (1)</text> <rect x="120" y="35" width="100" height="56" rx="6" fill="#888" fill-opacity="0.15" stroke="#888"/> <text x="170" y="58" font-size="12" text-anchor="middle">ADC / PDM</text> <text x="170" y="76" font-size="12" text-anchor="middle">→ PCM (G4)</text> <rect x="240" y="35" width="100" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/> <text x="290" y="58" font-size="12" text-anchor="middle">resample</text> <text x="290" y="76" font-size="12" text-anchor="middle">48→16 kHz (1)</text> <rect x="360" y="35" width="100" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/> <text x="410" y="58" font-size="12" text-anchor="middle">HPF · 등화</text> <text x="410" y="76" font-size="12" text-anchor="middle">FIR/IIR (3, 4)</text> <rect x="480" y="35" width="90" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/> <text x="525" y="58" font-size="12" text-anchor="middle">ring buffer</text> <text x="525" y="76" font-size="12" text-anchor="middle">frame (6, 8)</text> <rect x="590" y="35" width="80" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/> <text x="630" y="58" font-size="12" text-anchor="middle">window</text> <text x="630" y="76" font-size="12" text-anchor="middle">(5)</text> <rect x="590" y="140" width="80" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/> <text x="630" y="163" font-size="12" text-anchor="middle">FFT</text> <text x="630" y="181" font-size="12" text-anchor="middle">|X|² (5)</text> <rect x="480" y="140" width="90" height="56" rx="6" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/> <text x="525" y="163" font-size="12" text-anchor="middle">mel</text> <text x="525" y="181" font-size="12" text-anchor="middle">filterbank (7)</text> <rect x="360" y="140" width="100" height="56" rx="6" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/> <text x="410" y="163" font-size="12" text-anchor="middle">log / PCEN</text> <text x="410" y="181" font-size="12" text-anchor="middle">(7)</text> <rect x="240" y="140" width="100" height="56" rx="6" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/> <text x="290" y="163" font-size="12" text-anchor="middle">DCT → MFCC</text> <text x="290" y="181" font-size="12" text-anchor="middle">(선택, 7)</text> <rect x="120" y="140" width="100" height="56" rx="6" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/> <text x="170" y="163" font-size="12" text-anchor="middle">정규화</text>
<text x="170" y="181" font-size="12" text-anchor="middle">μ, σ (7)</text> <rect x="10" y="140" width="90" height="56" rx="6" fill="#d0564a" fill-opacity="0.15" stroke="#d0564a"/> <text x="55" y="163" font-size="12" text-anchor="middle">NN 모델</text> <text x="55" y="181" font-size="12" text-anchor="middle">(B5)</text> <line x1="100" y1="63" x2="118" y2="63" stroke="currentColor" marker-end="url(#g5a)"/> <line x1="220" y1="63" x2="238" y2="63" stroke="currentColor" marker-end="url(#g5a)"/> <line x1="340" y1="63" x2="358" y2="63" stroke="currentColor" marker-end="url(#g5a)"/> <line x1="460" y1="63" x2="478" y2="63" stroke="currentColor" marker-end="url(#g5a)"/> <line x1="570" y1="63" x2="588" y2="63" stroke="currentColor" marker-end="url(#g5a)"/> <line x1="630" y1="91" x2="630" y2="138" stroke="currentColor" marker-end="url(#g5a)"/> <line x1="590" y1="168" x2="572" y2="168" stroke="currentColor" marker-end="url(#g5a)"/> <line x1="480" y1="168" x2="462" y2="168" stroke="currentColor" marker-end="url(#g5a)"/> <line x1="360" y1="168" x2="342" y2="168" stroke="currentColor" marker-end="url(#g5a)"/> <line x1="240" y1="168" x2="222" y2="168" stroke="currentColor" marker-end="url(#g5a)"/> <line x1="120" y1="168" x2="102" y2="168" stroke="currentColor" marker-end="url(#g5a)"/> <text x="10" y="232" font-size="12">초록 = 샘플 단위 필터(시간 영역) · 파랑 = frame 단위 변환 · 주황 = 특징 공간 · 빨강 = 모델</text> <text x="10" y="252" font-size="12">IMU도 같은 틀: 50~200 Hz 샘플 → LPF → window → RMS·zero-crossing·자기상관·band energy (9절, B7)</text> <text x="10" y="272" font-size="12">학습 때 Python이 한 것을 기기 C 코드가 똑같이 해야 한다 — 하나라도 다르면 정확도가 조용히 떨어진다</text>
</svg>
```

그림 1 — 오디오 front-end를 DSP 블록으로 펼친 것. 괄호 안 숫자가 이 노트에서 그 블록을 다루는 절이다. 웨어러블(예를 들어 Hark 같은 기기라면 — 구조는 추정이다)에서는 이 줄의 앞쪽이 always-on DSP/MCU에서, 뒤쪽 모델이 DSP 또는 NPU에서 돈다.

펌웨어 관점의 비유 하나. Don이 SSD에서 NAND 읽기 경로를 디버깅할 때 "버스 트레이스 → 프로토콜 디코더 → 에러 통계"라는 파이프라인을 만들었다면, 여기서는 "샘플 → 필터 → 변환 → 특징"이다. 각 블록은 **입력 포맷·출력 포맷·지연·연산량·상태(state)** 를 가진 모듈이고, 하나라도 스펙이 어긋나면 다음 블록이 조용히 틀린 값을 받는다. DSP 이론은 이 블록들의 **스펙 시트를 읽고 쓰는 언어**다.

순서는 그림 1의 블록 순서를 거의 그대로 따른다: 샘플링(1) → 시스템 이론(2) → FIR(3) → IIR(4) → FFT(5) → STFT(6) → mel·MFCC(7) → 임베디드 파이프라인(8) → IMU 특징(9).

---

## 1. 샘플링 · aliasing · 양자화 · 리샘플링

### 1.1 직관: 바퀴가 거꾸로 도는 영화

영화에서 자동차 바퀴가 거꾸로 도는 것처럼 보일 때가 있다. 카메라가 초당 24장만 찍기 때문에, 바퀴살이 한 장 사이에 "거의 한 바퀴" 돌면 우리 눈에는 "조금 뒤로" 간 것처럼 보인다. **샘플링은 연속 신호를 일정 간격으로 찍는 것**이고, 너무 빠른 움직임은 느린 움직임으로 **둔갑(alias)** 한다.

### 1.2 정의: 샘플링 정리와 Nyquist

연속 신호 x(t)를 주기 Ts = 1/fs 로 찍으면 x[n] = x(n·Ts)다. **샘플링 정리(Nyquist–Shannon)**:

```
신호에 B Hz 넘는 성분이 전혀 없고(band-limited) fs > 2·B 이면
샘플 x[n]만으로 원래 x(t)를 완벽히 복원할 수 있다 (sinc 보간).
fs/2 를 Nyquist 주파수라고 부른다.
```

말로 하면: **한 주기에 두 점보다 많이** 찍어야 그 주파수를 알아볼 수 있다. 16 kHz 샘플링이면 8 kHz까지, 10 kHz면 5 kHz까지만 "진짜"로 표현된다. 음성의 명료도에 중요한 정보가 대부분 4 kHz 아래(전화는 8 kHz 샘플링), 자음의 마찰음 일부가 4~8 kHz에 있어서 음성 ML은 16 kHz를 표준으로 쓴다(B5 1.1).

Nyquist를 넘는 주파수 f는 다음 위치로 접혀(fold) 들어온다:

```
f_alias = | f − k·fs |   (k는 결과가 0 ~ fs/2 에 들어오게 하는 정수)
예) fs = 10 kHz:  7 kHz → |7 − 10| = 3 kHz,  13 kHz → |13 − 10| = 3 kHz,  9 kHz → 1 kHz
```

말로 하면: 주파수 축을 fs/2 에서 종이처럼 접고 또 접는다. 10 kHz 샘플링에서 7 kHz는 5 kHz 선을 기준으로 2 kHz 넘었으니 5 − 2 = 3 kHz로 접힌다.

### 1.3 손계산 → 코드: 7 kHz를 10 kHz로 찍으면

손으로 n = 1 하나만 해 보자. cos(2π·7000·1/10000) = cos(1.4π) = cos(252°) = −0.309. cos(2π·3000·1/10000) = cos(0.6π) = cos(108°) = −0.309. 같다. 일반적으로 cos(2π·7n/10) = cos(2πn − 2π·3n/10) = cos(2π·3n/10) 이므로 **모든 n에서** 같다.

이 코드는 7 kHz와 3 kHz 샘플이 똑같은지, FFT가 7 kHz 톤을 어디서 보는지 확인한다.

```python
import numpy as np
fs = 10_000                                   # 샘플링 10 kHz → Nyquist 5 kHz
n = np.arange(8)
x7 = np.cos(2 * np.pi * 7000 * n / fs)        # 7 kHz 톤을 샘플링
x3 = np.cos(2 * np.pi * 3000 * n / fs)        # 3 kHz 톤을 샘플링
print("7 kHz samples:", np.round(x7, 4))
print("3 kHz samples:", np.round(x3, 4))
print("max |diff|   :", np.abs(x7 - x3).max())
# 1000 샘플을 FFT해서 피크 위치 확인
N = 1000
x = np.cos(2 * np.pi * 7000 * np.arange(N) / fs)
X = np.abs(np.fft.rfft(x))
f = np.fft.rfftfreq(N, 1 / fs)
print("FFT peak at  :", f[np.argmax(X)], "Hz")
def alias(f0, fs):                            # 접힌(folded) 주파수 공식
    r = f0 % fs
    return min(r, fs - r)
for f0 in [3000, 7000, 13000, 17000, 9000]:
    print(f"{f0:>5} Hz -> {alias(f0, fs):>5} Hz")
```

```text
7 kHz samples: [ 1.    -0.309 -0.809  0.809  0.309 -1.     0.309  0.809]
3 kHz samples: [ 1.    -0.309 -0.809  0.809  0.309 -1.     0.309  0.809]
max |diff|   : 3.0531133177191805e-15
FFT peak at  : 3000.0 Hz
 3000 Hz ->  3000 Hz
 7000 Hz ->  3000 Hz
13000 Hz ->  3000 Hz
17000 Hz ->  3000 Hz
 9000 Hz ->  1000 Hz
```

출력에서 볼 것: 두 샘플 열이 1e-15(부동소수점 오차) 안에서 같다. FFT는 7 kHz 톤을 **3000 Hz**에 찍는다. 샘플만 보고는 원래가 3, 7, 13, 17 kHz 중 무엇이었는지 **절대로** 알 수 없다 — 정보가 사라진 것이라 나중에 디지털로 고칠 방법이 없다.

```svg
<svg viewBox="0 0 640 250" xmlns="http://www.w3.org/2000/svg">
<line x1="50" y1="110" x2="620" y2="110" stroke="currentColor" stroke-width="0.6"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="1.2" points="50.0,30.0 51.4,30.5 52.9,31.9 54.3,34.3 55.7,37.7 57.1,41.8 58.6,46.9 60.0,52.7 61.4,59.1 62.9,66.2 64.3,73.9 65.7,81.9 67.1,90.4 68.6,99.0 70.0,107.8 71.4,116.6 72.9,125.3 74.3,133.9 75.7,142.1 77.1,150.0 78.6,157.4 80.0,164.2 81.4,170.3 82.9,175.7 84.3,180.4 85.7,184.1 87.1,187.0 88.6,188.9 90.0,189.9 91.4,189.9 92.9,188.9 94.3,187.0 95.7,184.1 97.1,180.4 98.6,175.7 100.0,170.3 101.4,164.2 102.9,157.4 104.3,150.0 105.7,142.1 107.1,133.9 108.6,125.3 110.0,116.6 111.4,107.8 112.9,99.0 114.3,90.4 115.7,81.9 117.1,73.9 118.6,66.2 120.0,59.1 121.4,52.7 122.9,46.9 124.3,41.8 125.7,37.7 127.1,34.3 128.6,31.9 130.0,30.5 131.4,30.0 132.9,30.5 134.3,31.9 135.7,34.3 137.1,37.7 138.6,41.8 140.0,46.9 141.4,52.7 142.9,59.1 144.3,66.2 145.7,73.9 147.1,81.9 148.6,90.4 150.0,99.0 151.4,107.8 152.9,116.6 154.3,125.3 155.7,133.9 157.1,142.1 158.6,150.0 160.0,157.4 161.4,164.2 162.9,170.3 164.3,175.7 165.7,180.4 167.1,184.1 168.6,187.0 170.0,188.9 171.4,189.9 172.9,189.9 174.3,188.9 175.7,187.0 177.1,184.1 178.6,180.4 180.0,175.7 181.4,170.3 182.9,164.2 184.3,157.4 185.7,150.0 187.1,142.1 188.6,133.9 190.0,125.3 191.4,116.6 192.9,107.8 194.3,99.0 195.7,90.4 197.1,81.9 198.6,73.9 200.0,66.2 201.4,59.1 202.9,52.7 204.3,46.9 205.7,41.8 207.1,37.7 208.6,34.3 210.0,31.9 211.4,30.5 212.9,30.0 214.3,30.5 215.7,31.9 217.1,34.3 218.6,37.7 220.0,41.8 221.4,46.9 222.9,52.7 224.3,59.1 225.7,66.2 227.1,73.9 228.6,81.9 230.0,90.4 231.4,99.0 232.9,107.8 234.3,116.6 235.7,125.3 237.1,133.9 238.6,142.1 240.0,150.0 241.4,157.4 242.9,164.2 244.3,170.3 245.7,175.7 247.1,180.4 248.6,184.1 250.0,187.0 251.4,188.9 252.9,189.9 254.3,189.9 255.7,188.9 257.1,187.0 258.6,184.1 260.0,180.4 261.4,175.7 262.9,170.3 264.3,164.2 265.7,157.4 267.1,150.0 268.6,142.1 270.0,133.9 271.4,125.3 272.9,116.6 274.3,107.8 275.7,99.0 277.1,90.4 278.6,81.9 280.0,73.9 281.4,66.2 282.9,59.1 284.3,52.7 285.7,46.9 287.1,41.8 288.6,37.7 290.0,34.3 291.4,31.9 292.9,30.5 294.3,30.0 295.7,30.5 297.1,31.9 298.6,34.3 300.0,37.7 301.4,41.8 302.9,46.9 304.3,52.7 305.7,59.1 307.1,66.2 308.6,73.9 310.0,81.9 311.4,90.4 312.9,99.0 314.3,107.8 315.7,116.6 317.1,125.3 318.6,133.9 320.0,142.1 321.4,150.0 322.9,157.4 324.3,164.2 325.7,170.3 327.1,175.7 328.6,180.4 330.0,184.1 331.4,187.0 332.9,188.9 334.3,189.9 335.7,189.9 337.1,188.9 338.6,187.0 340.0,184.1 341.4,180.4 342.9,175.7 344.3,170.3 345.7,164.2 347.1,157.4 348.6,150.0 350.0,142.1 351.4,133.9 352.9,125.3 354.3,116.6 355.7,107.8 357.1,99.0 358.6,90.4 360.0,81.9 361.4,73.9 362.9,66.2 364.3,59.1 365.7,52.7 367.1,46.9 368.6,41.8 370.0,37.7 371.4,34.3 372.9,31.9 374.3,30.5 375.7,30.0 377.1,30.5 378.6,31.9 380.0,34.3 381.4,37.7 382.9,41.8 384.3,46.9 385.7,52.7 387.1,59.1 388.6,66.2 390.0,73.9 391.4,81.9 392.9,90.4 394.3,99.0 395.7,107.8 397.1,116.6 398.6,125.3 400.0,133.9 401.4,142.1 402.9,150.0 404.3,157.4 405.7,164.2 407.1,170.3 408.6,175.7 410.0,180.4 411.4,184.1 412.9,187.0 414.3,188.9 415.7,189.9 417.1,189.9 418.6,188.9 420.0,187.0 421.4,184.1 422.9,180.4 424.3,175.7 425.7,170.3 427.1,164.2 428.6,157.4 430.0,150.0 431.4,142.1 432.9,133.9 434.3,125.3 435.7,116.6 437.1,107.8 438.6,99.0 440.0,90.4 441.4,81.9 442.9,73.9 444.3,66.2 445.7,59.1 447.1,52.7 448.6,46.9 450.0,41.8 451.4,37.7 452.9,34.3 454.3,31.9 455.7,30.5 457.1,30.0 458.6,30.5 460.0,31.9 461.4,34.3 462.9,37.7 464.3,41.8 465.7,46.9 467.1,52.7 468.6,59.1 470.0,66.2 471.4,73.9 472.9,81.9 474.3,90.4 475.7,99.0 477.1,107.8 478.6,116.6 480.0,125.3 481.4,133.9 482.9,142.1 484.3,150.0 485.7,157.4 487.1,164.2 488.6,170.3 490.0,175.7 491.4,180.4 492.9,184.1 494.3,187.0 495.7,188.9 497.1,189.9 498.6,189.9 500.0,188.9 501.4,187.0 502.9,184.1 504.3,180.4 505.7,175.7 507.1,170.3 508.6,164.2 510.0,157.4 511.4,150.0 512.9,142.1 514.3,133.9 515.7,125.3 517.1,116.6 518.6,107.8 520.0,99.0 521.4,90.4 522.9,81.9 524.3,73.9 525.7,66.2 527.1,59.1 528.6,52.7 530.0,46.9 531.4,41.8 532.9,37.7 534.3,34.3 535.7,31.9 537.1,30.5 538.6,30.0 540.0,30.5 541.4,31.9 542.9,34.3 544.3,37.7 545.7,41.8 547.1,46.9 548.6,52.7 550.0,59.1 551.4,66.2 552.9,73.9 554.3,81.9 555.7,90.4 557.1,99.0 558.6,107.8 560.0,116.6 561.4,125.3 562.9,133.9 564.3,142.1 565.7,150.0 567.1,157.4 568.6,164.2 570.0,170.3 571.4,175.7 572.9,180.4 574.3,184.1 575.7,187.0 577.1,188.9 578.6,189.9 580.0,189.9 581.4,188.9 582.9,187.0 584.3,184.1 585.7,180.4 587.1,175.7 588.6,170.3 590.0,164.2 591.4,157.4 592.9,150.0 594.3,142.1 595.7,133.9 597.1,125.3 598.6,116.6 600.0,107.8 601.4,99.0 602.9,90.4 604.3,81.9 605.7,73.9 607.1,66.2 608.6,59.1 610.0,52.7 611.4,46.9 612.9,41.8 614.3,37.7 615.7,34.3 617.1,31.9 618.6,30.5 620.0,30.0"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="2" points="50.0,30.0 51.4,30.1 52.9,30.4 54.3,30.8 55.7,31.4 57.1,32.2 58.6,33.2 60.0,34.3 61.4,35.6 62.9,37.1 64.3,38.8 65.7,40.6 67.1,42.5 68.6,44.6 70.0,46.9 71.4,49.3 72.9,51.8 74.3,54.4 75.7,57.2 77.1,60.1 78.6,63.1 80.0,66.2 81.4,69.5 82.9,72.8 84.3,76.1 85.7,79.6 87.1,83.1 88.6,86.7 90.0,90.4 91.4,94.0 92.9,97.8 94.3,101.5 95.7,105.3 97.1,109.1 98.6,112.8 100.0,116.6 101.4,120.4 102.9,124.1 104.3,127.8 105.7,131.5 107.1,135.1 108.6,138.6 110.0,142.1 111.4,145.6 112.9,148.9 114.3,152.2 115.7,155.3 117.1,158.4 118.6,161.3 120.0,164.2 121.4,166.9 122.9,169.5 124.3,172.0 125.7,174.3 127.1,176.5 128.6,178.5 130.0,180.4 131.4,182.1 132.9,183.6 134.3,185.0 135.7,186.3 137.1,187.3 138.6,188.2 140.0,188.9 141.4,189.4 142.9,189.8 144.3,190.0 145.7,190.0 147.1,189.8 148.6,189.4 150.0,188.9 151.4,188.2 152.9,187.3 154.3,186.3 155.7,185.0 157.1,183.6 158.6,182.1 160.0,180.4 161.4,178.5 162.9,176.5 164.3,174.3 165.7,172.0 167.1,169.5 168.6,166.9 170.0,164.2 171.4,161.3 172.9,158.4 174.3,155.3 175.7,152.2 177.1,148.9 178.6,145.6 180.0,142.1 181.4,138.6 182.9,135.1 184.3,131.5 185.7,127.8 187.1,124.1 188.6,120.4 190.0,116.6 191.4,112.8 192.9,109.1 194.3,105.3 195.7,101.5 197.1,97.8 198.6,94.0 200.0,90.4 201.4,86.7 202.9,83.1 204.3,79.6 205.7,76.1 207.1,72.8 208.6,69.5 210.0,66.2 211.4,63.1 212.9,60.1 214.3,57.2 215.7,54.4 217.1,51.8 218.6,49.3 220.0,46.9 221.4,44.6 222.9,42.5 224.3,40.6 225.7,38.8 227.1,37.1 228.6,35.6 230.0,34.3 231.4,33.2 232.9,32.2 234.3,31.4 235.7,30.8 237.1,30.4 238.6,30.1 240.0,30.0 241.4,30.1 242.9,30.4 244.3,30.8 245.7,31.4 247.1,32.2 248.6,33.2 250.0,34.3 251.4,35.6 252.9,37.1 254.3,38.8 255.7,40.6 257.1,42.5 258.6,44.6 260.0,46.9 261.4,49.3 262.9,51.8 264.3,54.4 265.7,57.2 267.1,60.1 268.6,63.1 270.0,66.2 271.4,69.5 272.9,72.8 274.3,76.1 275.7,79.6 277.1,83.1 278.6,86.7 280.0,90.4 281.4,94.0 282.9,97.8 284.3,101.5 285.7,105.3 287.1,109.1 288.6,112.8 290.0,116.6 291.4,120.4 292.9,124.1 294.3,127.8 295.7,131.5 297.1,135.1 298.6,138.6 300.0,142.1 301.4,145.6 302.9,148.9 304.3,152.2 305.7,155.3 307.1,158.4 308.6,161.3 310.0,164.2 311.4,166.9 312.9,169.5 314.3,172.0 315.7,174.3 317.1,176.5 318.6,178.5 320.0,180.4 321.4,182.1 322.9,183.6 324.3,185.0 325.7,186.3 327.1,187.3 328.6,188.2 330.0,188.9 331.4,189.4 332.9,189.8 334.3,190.0 335.7,190.0 337.1,189.8 338.6,189.4 340.0,188.9 341.4,188.2 342.9,187.3 344.3,186.3 345.7,185.0 347.1,183.6 348.6,182.1 350.0,180.4 351.4,178.5 352.9,176.5 354.3,174.3 355.7,172.0 357.1,169.5 358.6,166.9 360.0,164.2 361.4,161.3 362.9,158.4 364.3,155.3 365.7,152.2 367.1,148.9 368.6,145.6 370.0,142.1 371.4,138.6 372.9,135.1 374.3,131.5 375.7,127.8 377.1,124.1 378.6,120.4 380.0,116.6 381.4,112.8 382.9,109.1 384.3,105.3 385.7,101.5 387.1,97.8 388.6,94.0 390.0,90.4 391.4,86.7 392.9,83.1 394.3,79.6 395.7,76.1 397.1,72.8 398.6,69.5 400.0,66.2 401.4,63.1 402.9,60.1 404.3,57.2 405.7,54.4 407.1,51.8 408.6,49.3 410.0,46.9 411.4,44.6 412.9,42.5 414.3,40.6 415.7,38.8 417.1,37.1 418.6,35.6 420.0,34.3 421.4,33.2 422.9,32.2 424.3,31.4 425.7,30.8 427.1,30.4 428.6,30.1 430.0,30.0 431.4,30.1 432.9,30.4 434.3,30.8 435.7,31.4 437.1,32.2 438.6,33.2 440.0,34.3 441.4,35.6 442.9,37.1 444.3,38.8 445.7,40.6 447.1,42.5 448.6,44.6 450.0,46.9 451.4,49.3 452.9,51.8 454.3,54.4 455.7,57.2 457.1,60.1 458.6,63.1 460.0,66.2 461.4,69.5 462.9,72.8 464.3,76.1 465.7,79.6 467.1,83.1 468.6,86.7 470.0,90.4 471.4,94.0 472.9,97.8 474.3,101.5 475.7,105.3 477.1,109.1 478.6,112.8 480.0,116.6 481.4,120.4 482.9,124.1 484.3,127.8 485.7,131.5 487.1,135.1 488.6,138.6 490.0,142.1 491.4,145.6 492.9,148.9 494.3,152.2 495.7,155.3 497.1,158.4 498.6,161.3 500.0,164.2 501.4,166.9 502.9,169.5 504.3,172.0 505.7,174.3 507.1,176.5 508.6,178.5 510.0,180.4 511.4,182.1 512.9,183.6 514.3,185.0 515.7,186.3 517.1,187.3 518.6,188.2 520.0,188.9 521.4,189.4 522.9,189.8 524.3,190.0 525.7,190.0 527.1,189.8 528.6,189.4 530.0,188.9 531.4,188.2 532.9,187.3 534.3,186.3 535.7,185.0 537.1,183.6 538.6,182.1 540.0,180.4 541.4,178.5 542.9,176.5 544.3,174.3 545.7,172.0 547.1,169.5 548.6,166.9 550.0,164.2 551.4,161.3 552.9,158.4 554.3,155.3 555.7,152.2 557.1,148.9 558.6,145.6 560.0,142.1 561.4,138.6 562.9,135.1 564.3,131.5 565.7,127.8 567.1,124.1 568.6,120.4 570.0,116.6 571.4,112.8 572.9,109.1 574.3,105.3 575.7,101.5 577.1,97.8 578.6,94.0 580.0,90.4 581.4,86.7 582.9,83.1 584.3,79.6 585.7,76.1 587.1,72.8 588.6,69.5 590.0,66.2 591.4,63.1 592.9,60.1 594.3,57.2 595.7,54.4 597.1,51.8 598.6,49.3 600.0,46.9 601.4,44.6 602.9,42.5 604.3,40.6 605.7,38.8 607.1,37.1 608.6,35.6 610.0,34.3 611.4,33.2 612.9,32.2 614.3,31.4 615.7,30.8 617.1,30.4 618.6,30.1 620.0,30.0"/>
<line x1="50.0" y1="110" x2="50.0" y2="30.0" stroke="#d0564a" stroke-width="1"/> <circle cx="50.0" cy="30.0" r="4" fill="#d0564a"/> <text x="50.0" y="215" font-size="12" text-anchor="middle">0</text> <line x1="107.0" y1="110" x2="107.0" y2="134.7" stroke="#d0564a" stroke-width="1"/> <circle cx="107.0" cy="134.7" r="4" fill="#d0564a"/> <text x="107.0" y="215" font-size="12" text-anchor="middle">1</text> <line x1="164.0" y1="110" x2="164.0" y2="174.7" stroke="#d0564a" stroke-width="1"/> <circle cx="164.0" cy="174.7" r="4" fill="#d0564a"/> <text x="164.0" y="215" font-size="12" text-anchor="middle">2</text> <line x1="221.0" y1="110" x2="221.0" y2="45.3" stroke="#d0564a" stroke-width="1"/> <circle cx="221.0" cy="45.3" r="4" fill="#d0564a"/> <text x="221.0" y="215" font-size="12" text-anchor="middle">3</text> <line x1="278.0" y1="110" x2="278.0" y2="85.3" stroke="#d0564a" stroke-width="1"/> <circle cx="278.0" cy="85.3" r="4" fill="#d0564a"/> <text x="278.0" y="215" font-size="12" text-anchor="middle">4</text> <line x1="335.0" y1="110" x2="335.0" y2="190.0" stroke="#d0564a" stroke-width="1"/> <circle cx="335.0" cy="190.0" r="4" fill="#d0564a"/> <text x="335.0" y="215" font-size="12" text-anchor="middle">5</text> <line x1="392.0" y1="110" x2="392.0" y2="85.3" stroke="#d0564a" stroke-width="1"/> <circle cx="392.0" cy="85.3" r="4" fill="#d0564a"/> <text x="392.0" y="215" font-size="12" text-anchor="middle">6</text> <line x1="449.0" y1="110" x2="449.0" y2="45.3" stroke="#d0564a" stroke-width="1"/> <circle cx="449.0" cy="45.3" r="4" fill="#d0564a"/> <text x="449.0" y="215" font-size="12" text-anchor="middle">7</text> <line x1="506.0" y1="110" x2="506.0" y2="174.7" stroke="#d0564a" stroke-width="1"/> <circle cx="506.0" cy="174.7" r="4" fill="#d0564a"/> <text x="506.0" y="215" font-size="12" text-anchor="middle">8</text> <line x1="563.0" y1="110" x2="563.0" y2="134.7" stroke="#d0564a" stroke-width="1"/> <circle cx="563.0" cy="134.7" r="4" fill="#d0564a"/> <text x="563.0" y="215" font-size="12" text-anchor="middle">9</text> <line x1="620.0" y1="110" x2="620.0" y2="30.0" stroke="#d0564a" stroke-width="1"/> <circle cx="620.0" cy="30.0" r="4" fill="#d0564a"/> <text x="620.0" y="215" font-size="12" text-anchor="middle">10</text> <text x="50" y="18" font-size="13">파랑: 7 kHz 원신호 · 주황: 3 kHz · 빨간 점: 10 kHz로 찍은 샘플 (둘 다 같은 점을 지난다)</text> <text x="335.0" y="244" font-size="12" text-anchor="middle">샘플 번호 n (간격 0.1 ms, 전체 1 ms)</text>
</svg>
```

그림 2 — 7 kHz(파랑)와 3 kHz(주황) 코사인을 10 kHz로 찍은 샘플(빨강 점). 실제 계산한 곡선이다. 두 곡선 모두 빨간 점을 정확히 지나므로 샘플만 보면 구분할 수 없다.

### 1.4 Anti-aliasing filter (AAF)

aliasing은 샘플링 **전에** 막아야 한다. 그래서 ADC 앞에 fs/2 위를 깎는 아날로그 low-pass filter, **anti-aliasing filter**를 둔다. 현실의 필터는 칼처럼 자를 수 없어서 통과대역(passband) → 전이대역(transition band) → 저지대역(stopband)이 생긴다. 그래서 실제로는 **oversampling**을 쓴다.

- 오디오 ADC는 대개 sigma-delta 방식이라 내부에서 수 MHz로 아주 빠르게 샘플링한다. 그러면 아날로그 AAF는 "수 MHz 근처만 깎는" 느슨한 RC 필터로 충분하고, 날카로운 필터링은 **디지털 decimation filter**가 한다.
- PDM MEMS 마이크도 같다. 1~3 MHz 1-bit 스트림을 CIC + FIR로 걸러 16 kHz PCM으로 줄인다 — 이것이 **G4**의 주제다.
- IMU도 내부에 디지털 LPF가 있고 ODR(output data rate)과 bandwidth 레지스터로 설정한다(G1·G2). ODR을 낮추면서 bandwidth를 그대로 두면 IMU 안에서 aliasing이 난다 — 데이터시트의 "ODR/2보다 bandwidth를 작게" 규칙이 이것이다.

Don의 RF 경험과 연결하면: RF 수신기의 IF 샘플링, 이미지 주파수(image frequency) 문제와 같은 수학이다. "원하지 않는 대역이 원하는 대역으로 접혀 들어온다."

### 1.5 양자화 잡음 — 6 dB per bit

샘플링이 시간 축을 자른다면, **양자화(quantization)** 는 값 축을 자른다. 간격 Δ로 반올림하면 오차 e는 대략 [−Δ/2, Δ/2] 균등분포이고 분산은 Δ²/12다 (A2의 균등분포 분산, C1의 NN 양자화 잡음과 **같은 공식**).

```
full-scale 사인파(진폭 1, 전력 1/2), N bit, 범위 [−1, 1) → Δ = 2 / 2^N
SNR = (1/2) / (Δ²/12) = 1.5 · 2^(2N)  →  10·log10 → 6.02·N + 1.76 dB
```

말로 하면: **비트 하나에 SNR 6 dB**. 16-bit 오디오는 이론상 98 dB다. 단, 신호가 full-scale보다 작으면 그만큼 SNR이 깎인다.

이 코드는 8~24 bit 양자화의 SNR을 측정해 공식과 비교하고, 작은 신호에서 SNR이 얼마나 줄어드는지 본다.

```python
import numpy as np
fs, N = 16000, 160000
t = np.arange(N) / fs
x = 0.999 * np.sin(2 * np.pi * 1000.3 * t)     # full-scale 근처 사인파 (±1)
for bits in [8, 12, 16, 24]:
    step = 2.0 / 2**bits                       # 전체 범위 [-1, 1)을 2^bits 칸으로
    xq = np.round(x / step) * step             # 양자화 (반올림)
    e = xq - x
    snr = 10 * np.log10(np.mean(x**2) / np.mean(e**2))
    print(f"{bits:2d} bit: SNR = {snr:6.2f} dB  (공식 6.02·N+1.76 = {6.02*bits+1.76:6.2f})"
          f"  e_rms/step = {e.std()/step:.4f}")
# 신호가 작으면? 16 bit에서 -40 dBFS 신호
xs = 0.01 * np.sin(2 * np.pi * 1000.3 * t); step = 2.0 / 2**16
e = np.round(xs / step) * step - xs
print(f"16 bit, -40 dBFS 신호: SNR = {10*np.log10(np.mean(xs**2)/np.mean(e**2)):.2f} dB")
```

```text
 8 bit: SNR =  49.93 dB  (공식 6.02·N+1.76 =  49.92)  e_rms/step = 0.2884
12 bit: SNR =  74.02 dB  (공식 6.02·N+1.76 =  74.00)  e_rms/step = 0.2881
16 bit: SNR =  98.06 dB  (공식 6.02·N+1.76 =  98.08)  e_rms/step = 0.2892
24 bit: SNR = 146.23 dB  (공식 6.02·N+1.76 = 146.24)  e_rms/step = 0.2893
16 bit, -40 dBFS 신호: SNR = 58.01 dB
```

출력에서 볼 것: 측정 SNR이 공식과 0.1 dB 안에서 맞는다. `e_rms/step ≈ 0.289 = 1/√12`. 16 bit라도 −40 dBFS 신호면 SNR이 98 − 40 = 58 dB로 떨어진다. 그래서 **마이크 gain 설계**(신호를 full-scale 가까이 쓰되 clipping은 피하기)가 중요하고, 이것은 C1에서 "activation 범위를 잘 잡아야 int8 양자화 잡음이 작다"와 똑같은 이야기다.

### 1.6 리샘플링 — 48 kHz → 16 kHz

마이크·코덱이 48 kHz로 주는데 모델은 16 kHz를 원하는 경우가 흔하다. 3개 중 1개만 남기는 **decimation**을 하면 되는데, 그 전에 **새 Nyquist(8 kHz) 위를 디지털 LPF로 깎아야** 한다. 깎지 않으면 1.3절과 같은 aliasing이 디지털 영역에서 다시 일어난다.

```
48 kHz → [LPF, cutoff 8 kHz] → [3개 중 1개] → 16 kHz
10 kHz 성분을 안 깎으면: 16 kHz 기준 |10 − 16| = 6 kHz 로 접힌다
```

**polyphase 구현**: 필터를 48 kHz에서 다 계산하고 3개 중 2개를 버리는 것은 낭비다. 남길 출력만 계산하면 MAC이 1/3이 된다. 일반적인 유리수 비율 L/M(예: 44.1 → 16 kHz는 160/441)은 "L배 업샘플(0 끼우기) → LPF → M배 다운샘플"인데, polyphase는 0과 곱하는 계산과 버릴 출력 계산을 모두 건너뛴다. `scipy.signal.resample_poly(x, up, down)`이 이것이다.

이 코드는 1 kHz + 10 kHz 신호를 필터 없이 줄였을 때와 `resample_poly`로 줄였을 때 6 kHz alias가 얼마나 생기는지 비교한다.

```python
import numpy as np
from scipy import signal
fs_in, fs_out = 48000, 16000
t = np.arange(48000) / fs_in                        # 1초
x = np.sin(2*np.pi*1000*t) + 0.5*np.sin(2*np.pi*10000*t)   # 1 kHz(살릴 것) + 10 kHz(새 Nyquist 8 kHz 위)
def amp_at(y, fs, f0):                               # f0 Hz 성분의 진폭 (1초 신호라 bin = 1 Hz)
    Y = np.abs(np.fft.rfft(y)) / (len(y) / 2)
    return round(float(Y[int(f0)]), 4)
naive = x[::3]                                      # 필터 없이 3개 중 1개만
poly = signal.resample_poly(x, up=1, down=3)        # polyphase: 필터 + decimate
for name, y in [("naive x[::3]", naive), ("resample_poly", poly)]:
    print(f"{name:14s} 1 kHz: {amp_at(y, fs_out, 1000):.4f}   6 kHz(alias): {amp_at(y, fs_out, 6000):.4f}")
# resample_poly 내부 필터: firwin(2*10*max(up,down)+1, 1/max(up,down), window=('kaiser', 5.0))
h = signal.firwin(2 * 10 * 3 + 1, 1.0 / 3, window=("kaiser", 5.0))
w, H = signal.freqz(h, worN=[1000, 8000, 10000], fs=fs_in)
print("filter taps:", len(h), " |H| at 1k/8k/10k Hz:", np.round(np.abs(H), 4))
print("len in/out:", len(x), len(poly))
```

```text
naive x[::3]   1 kHz: 1.0000   6 kHz(alias): 0.5000
resample_poly  1 kHz: 1.0010   6 kHz(alias): 0.0007
filter taps: 61  |H| at 1k/8k/10k Hz: [1.001  0.5002 0.0014]
len in/out: 48000 16000
```

출력에서 볼 것: 필터 없이 `x[::3]`을 하면 10 kHz 성분(진폭 0.5)이 **6 kHz에 진폭 0.5 그대로** 나타난다. `resample_poly`는 0.0007(약 −57 dB)로 눌렀다. 내부 필터는 61-tap Kaiser-window FIR, cutoff는 8 kHz이고 8 kHz에서 |H| = 0.5(−6 dB)다. 즉 7~9 kHz 근처는 "반쯤" 통과·반쯤 aliasing된다 — 음성에는 문제없지만, 8 kHz 근처가 중요하면 cutoff를 조금 낮추거나 tap을 늘린다.

연산량: 61 tap을 48 kHz 전체에서 계산하면 61 × 48000 ≈ 2.93 M MAC/s, polyphase로 남길 것만 계산하면 61 × 16000 ≈ 0.98 M MAC/s다.

### 1.7 함정

- **"디지털로 나중에 거르면 된다"**: 이미 접힌 성분은 원래 대역 성분과 섞여서 분리할 수 없다. aliasing은 샘플링·decimation 직전에만 막을 수 있다.
- **학습 데이터와 기기의 리샘플러가 다르다**: 학습은 `librosa`/`torchaudio`의 고품질 리샘플러, 기기는 코덱 내장 decimator. 4~8 kHz 대역 모양이 달라져 mel 상위 band 값이 다르다. 기기 녹음으로 만든 데이터로 검증해야 한다(C8).
- **IMU ODR만 낮추고 bandwidth 설정을 안 바꿈**: 진동·모터 소음이 저주파 가짜 움직임으로 둔갑한다.

---

## 2. LTI 시스템 · 컨볼루션 · 주파수 응답 · z-변환

### 2.1 직관: "임펄스 하나 넣어 보면 다 안다"

Don이 새 보드를 bring-up할 때 레지스터 하나를 토글하고 버스 응답을 오실로스코프로 보듯이, DSP에서는 시스템에 **임펄스**(δ[n] = 1, 0, 0, 0, …) 하나를 넣고 나오는 것을 본다. 이것이 **임펄스 응답 h[n]** 이다. 시스템이 **LTI**(Linear, Time-Invariant)라면 h[n] 하나로 모든 입력에 대한 출력을 알 수 있다.

- **Linear**: 입력을 a배 하고 두 입력을 더하면 출력도 a배 하고 더해진다.
- **Time-Invariant**: 입력을 k 샘플 늦추면 출력도 k 샘플 늦어질 뿐 모양은 같다.

FIR·IIR 필터, FFT 전의 window 곱셈을 제외한 대부분의 필터가 LTI다. (log, ReLU, 양자화, 포화는 비선형이다.)

### 2.2 정의: 컨볼루션

어떤 입력이든 "크기가 x[k]인 임펄스가 k 시점에 하나씩"의 합이다. 각 임펄스가 h를 하나씩 만들고 LTI라서 그걸 다 더하면 출력이다:

```
y[n] = Σ_k h[k] · x[n − k]        (컨볼루션, y = h ∗ x)
```

말로 하면: 출력 n번째 = 최근 입력들을 h로 가중합한 것. h를 **뒤집어서** 입력 위로 미끄러뜨린다. C로는 이중 루프 MAC이고, E4 1.1의 FIR 한 줄이 정확히 이것이다.

손계산: x = [1, 2, 3, 4], h = [1, 0, −1] (현재 − 2샘플 전, 간단한 미분기).

```
y[0] = h0·x0                  = 1
y[1] = h0·x1 + h1·x0          = 2
y[2] = h0·x2 + h1·x1 + h2·x0  = 3 − 1 = 2
y[3] = 4 − 2 = 2,  y[4] = h1·x3 + h2·x2 = −3,  y[5] = h2·x3 = −4
```

### 2.3 DL의 "convolution"은 correlation이다 (B2 복습)

PyTorch `conv1d`/`conv2d`는 커널을 **뒤집지 않고** 민다. 수학적으로는 cross-correlation이다. 학습되는 가중치라 뒤집든 말든 상관없어서 DL에서는 그냥 convolution이라 부른다. 하지만 **DSP 필터 계수를 NN conv 레이어로 옮기거나 그 반대를 할 때** 뒤집기를 잊으면 비대칭 필터에서 결과가 틀린다.

이 코드는 `np.convolve`, `np.correlate`, `torch.nn.functional.conv1d`를 비교하고 임펄스 응답을 확인한다.

```python
import numpy as np, torch
x = np.array([1., 2., 3., 4.])
h = np.array([1., 0., -1.])                    # 비대칭 h: 뒤집힘 여부가 보인다
y_conv = np.convolve(x, h)                     # y[n] = Σ h[k]·x[n-k]  (h를 뒤집어 민다)
y_corr = np.correlate(x, h, mode="full")       # Σ h[k]·x[n+k]        (뒤집지 않는다)
print("convolve :", y_conv)
print("correlate:", y_corr)
# PyTorch conv1d(DL의 'convolution')는 사실 correlation
yt = torch.nn.functional.conv1d(torch.tensor(x).view(1, 1, -1),
                                torch.tensor(h).view(1, 1, -1), padding=2)
print("torch conv1d (pad=2):", yt.flatten().numpy())
print("== correlate?", np.allclose(yt.flatten().numpy(), y_corr))
# 임펄스 응답: δ를 넣으면 h가 그대로 나온다
d = np.zeros(5); d[0] = 1
print("impulse response:", np.convolve(d, h)[:5])
```

```text
convolve : [ 1.  2.  2.  2. -3. -4.]
correlate: [-1. -2. -2. -2.  3.  4.]
torch conv1d (pad=2): [-1. -2. -2. -2.  3.  4.]
== correlate? True
impulse response: [ 1.  0. -1.  0.  0.]
```

출력에서 볼 것: `np.convolve`는 손계산과 같은 [1, 2, 2, 2, −3, −4]. `torch conv1d`는 `np.correlate`와 같고 부호가 뒤집혀 있다(h가 반대칭이라 뒤집으면 −h). 임펄스를 넣으면 h가 그대로 나온다. 선형 위상 FIR(대칭 h)은 뒤집어도 같아서 이 차이가 숨어 버린다는 것도 기억해 두자.

### 2.4 주파수 응답 — 사인파는 사인파로 나온다

LTI 시스템에 주파수 f의 사인파를 넣으면 **같은 주파수**의 사인파가 나오고, 크기와 위상만 바뀐다. 그 크기 비율과 위상 차이가 **주파수 응답**이다:

```
H(e^{jω}) = Σ_n h[n] · e^{−jωn},    ω = 2π·f / fs   (rad/sample, 0 ~ π 가 0 ~ fs/2)
출력 = |H| · 입력 진폭,   위상 = 입력 위상 + ∠H
```

말로 하면: 임펄스 응답을 Fourier 변환하면 "각 주파수를 몇 배로 통과시키나"의 표가 된다. 시간 영역의 컨볼루션 = 주파수 영역의 곱셈. 그래서 필터 설계는 "원하는 |H| 모양을 정하고 그걸 만드는 h를 찾는 일"이다.

손계산: 4-tap moving average h = [¼, ¼, ¼, ¼]. ω = π/2 (fs/4 = 4 kHz @ 16 kHz)에서:

```
H = ¼ (1 + e^{−jπ/2} + e^{−jπ} + e^{−j3π/2}) = ¼ (1 − j − 1 + j) = 0
```

말로 하면: fs/4 사인파는 4샘플이 한 주기라 4개를 평균하면 정확히 0이 된다.

### 2.5 z-변환 — z⁻¹은 레지스터 하나

z-변환은 수열을 다항식으로 바꾸는 장부 정리법이다: H(z) = Σ h[n]·z⁻ⁿ. 펌웨어 엔지니어에게 가장 쉬운 읽는 법은 **z⁻¹ = 한 샘플 지연 = 레지스터(플립플롭) 하나** 다.

```
차분 방정식:  y[n] = b0·x[n] + b1·x[n−1] + b2·x[n−2] − a1·y[n−1] − a2·y[n−2]
z-변환:       H(z) = (b0 + b1·z⁻¹ + b2·z⁻²) / (1 + a1·z⁻¹ + a2·z⁻²) = B(z) / A(z)
주파수 응답:  z = e^{jω} 를 대입 (단위원 위를 한 바퀴 돌면 0 → fs/2 → 0)
```

- **영점(zero)**: B(z) = 0 인 z. 단위원 위에 있으면 그 주파수를 **완전히** 죽인다.
- **극점(pole)**: A(z) = 0 인 z. 단위원에 가까울수록 그 주파수 근처를 크게 키운다(공진).
- **안정성**: 인과(causal) 시스템은 **모든 극점이 단위원 안**(|p| < 1)이어야 안정하다. 극점이 밖이면 임펄스 응답이 발산한다.

말로 하면: 분모 다항식의 근이 피드백 루프의 "감쇠율"이다. Don이 아는 제어 루프로 말하면, 이산 시간 루프 gain이 1을 넘으면 발산하는 것과 같다(A3에서 학습률을 루프 gain에 비유한 것과 같은 그림).

이 코드는 MA4의 주파수 응답·영점과, 1-pole IIR y[n] = x[n] + a·y[n−1] 의 극점 위치에 따른 안정성을 확인한다.

```python
import numpy as np
from scipy import signal
fs = 16000
b_ma = np.ones(4) / 4                                # 4-tap moving average (FIR)
f = np.array([0, 2000, 4000, 6000, 8000])
_, H = signal.freqz(b_ma, 1, worN=f, fs=fs)
print("MA4 |H| at", f, "Hz:", np.round(np.abs(H), 4))
z, p, k = signal.tf2zpk(b_ma, [1])
print("MA4 zeros:", np.round(z, 4))                  # 단위원 위의 영점 = 그 주파수를 완전히 죽인다
for a in [0.9, 1.0, 1.1]:                            # 1-pole IIR: y[n] = x[n] + a·y[n-1]
    d = np.zeros(40); d[0] = 1
    h = signal.lfilter([1], [1, -a], d)              # 분모 계수 부호: [1, -a]
    print(f"a={a}: pole at z={a}, h[10]={h[10]:.4f}, h[39]={h[39]:.4f}")
_, H1 = signal.freqz([1 - 0.9], [1, -0.9], worN=[0, 1000, 8000], fs=fs)
print("leaky integrator (a=0.9) |H| at 0/1k/8k:", np.round(np.abs(H1), 4))
```

```text
MA4 |H| at [   0 2000 4000 6000 8000] Hz: [1.     0.6533 0.     0.2706 0.    ]
MA4 zeros: [-1.+0.j  0.+1.j  0.-1.j]
a=0.9: pole at z=0.9, h[10]=0.3487, h[39]=0.0164
a=1.0: pole at z=1.0, h[10]=1.0000, h[39]=1.0000
a=1.1: pole at z=1.1, h[10]=2.5937, h[39]=41.1448
leaky integrator (a=0.9) |H| at 0/1k/8k: [1.     0.2608 0.0526]
```

출력에서 볼 것: MA4의 |H|는 4 kHz와 8 kHz에서 정확히 0이고, 영점이 z = −1(fs/2), ±j(fs/4) — 단위원 위에 있다. 1-pole IIR은 극점 0.9면 h[n] = 0.9ⁿ으로 줄고, 1.0이면 영원히 1(적분기, 경계 안정), 1.1이면 h[39] = 41로 발산한다. 극점 0.9인 "leaky integrator"는 DC 이득 1, 8 kHz 이득 0.05의 아주 싼 LPF다(B3의 RNN 상태 업데이트와 같은 구조, 7.5절 PCEN의 smoother도 이것).

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<circle cx="160" cy="150" r="100" fill="none" stroke="currentColor"/> <line x1="30" y1="150" x2="295" y2="150" stroke="currentColor" stroke-width="0.5"/> <line x1="160" y1="25" x2="160" y2="275" stroke="currentColor" stroke-width="0.5"/> <circle cx="60.0" cy="150.0" r="5" fill="none" stroke="#4a7bd0" stroke-width="2"/> <circle cx="160.0" cy="50.0" r="5" fill="none" stroke="#4a7bd0" stroke-width="2"/> <circle cx="160.0" cy="250.0" r="5" fill="none" stroke="#4a7bd0" stroke-width="2"/> <path d="M245.0,145.0 L255.0,155.0 M245.0,155.0 L255.0,145.0" stroke="#3f9a6b" stroke-width="2"/> <path d="M265.0,145.0 L275.0,155.0 M265.0,155.0 L275.0,145.0" stroke="#d0564a" stroke-width="2"/> <text x="266" y="144" font-size="12">z=1 (DC)</text> <text x="54" y="142" font-size="12" text-anchor="end">fs/2</text> <text x="166" y="44" font-size="12">fs/4</text> <text x="220" y="172" font-size="12" >0.9 안정</text> <text x="260" y="190" font-size="12" >1.1 발산</text> <text x="20" y="20" font-size="13">(a) z-평면: 원 = 단위원, ○ = MA4 영점, × = 1-pole 극점</text> <rect x="380" y="60" width="260" height="200" fill="none" stroke="currentColor" stroke-width="0.6"/> <polyline fill="none" stroke="currentColor" stroke-dasharray="4 3" points="602.9,233.3 602.9,226.4 602.8,219.5 602.8,212.6 602.8,205.7 602.8,198.9 602.7,192.0 602.7,185.1 602.6,178.2 602.5,171.3 602.5,164.4 602.4,157.5 602.3,150.6 602.2,143.7 602.1,136.8 602.0,129.9 601.8,123.0 601.7,116.1 601.6,109.2 601.4,102.3 601.3,95.4 601.1,88.5 600.9,81.6 600.8,74.7 600.6,67.8 600.4,60.9 600.2,54.0 600.0,47.2 599.7,40.3 599.5,33.4"/> <line x1="380" y1="233.3" x2="640" y2="233.3" stroke="currentColor" stroke-width="0.4"/> <path d="M494.7,137.1 L504.7,147.1 M494.7,147.1 L504.7,137.1" stroke="#4a7bd0" stroke-width="2"/> <text x="489.7" y="146.1" font-size="12" text-anchor="end">float</text> <path d="M494.7,140.5 L504.7,150.5 M494.7,150.5 L504.7,140.5" stroke="#3f9a6b" stroke-width="2"/> <text x="509.7" y="155.5" font-size="12" text-anchor="start">Q14</text> <path d="M494.5,113.9 L504.5,123.9 M494.5,123.9 L504.5,113.9" stroke="#e08a3c" stroke-width="2"/> <text x="509.5" y="122.9" font-size="12" text-anchor="start">Q12</text> <path d="M597.9,228.3 L607.9,238.3 M597.9,238.3 L607.9,228.3" stroke="#d0564a" stroke-width="2"/> <path d="M394.8,228.3 L404.8,238.3 M394.8,238.3 L404.8,228.3" stroke="#d0564a" stroke-width="2"/> <text x="405.8" y="251.3" font-size="12" >Q10: 실수 극점 2개 (하나는 z=1)</text> <text x="598.9" y="54" font-size="12" text-anchor="end">단위원(점선)</text> <text x="380" y="34" font-size="13">(b) 50 Hz 2차 LPF 극점을 확대 (Re 0.97~1.005)</text> <text x="380" y="280" font-size="12">계수 a1, a2를 Qn으로 반올림한 결과 (ex10 실제 값)</text>
</svg>
```

그림 3 — (a) z-평면. 단위원 위의 각도가 주파수다(오른쪽 z = 1이 DC, 왼쪽 z = −1이 fs/2). MA4의 영점 3개(○)는 단위원 위, 극점 0.9는 안(안정), 1.1은 밖(발산). (b) 4.5절에서 다룰 50 Hz 2차 LPF의 극점을 확대한 것 — 계수를 Q10으로 반올림하면 극점이 z = 1 위로 올라간다. 좌표는 ex5·ex10의 실제 값이다.

### 2.6 함정

- **scipy의 분모 부호**: `lfilter(b, a, x)`에서 a = [1, a1, a2]는 y[n] + a1·y[n−1] + … 꼴이다. y[n] = x[n] + 0.9·y[n−1] 은 a = [1, −0.9]. CMSIS-DSP biquad는 **반대 부호**(4.3절)라 그대로 복사하면 극점이 단위원 밖으로 간다.
- **"선형"의 의미**: 포화·양자화가 들어가는 순간 고정소수점 필터는 엄밀히 LTI가 아니다. limit cycle(4.6절)은 그 비선형성에서 나온다.

---

## 3. FIR 필터

### 3.1 직관과 정의

**FIR(Finite Impulse Response)** 필터는 피드백이 없는 필터다: y[n] = Σ_{k=0}^{N−1} h[k]·x[n−k]. 임펄스 응답이 N개로 끝나고, 극점이 전부 z = 0에 있어서 **항상 안정**하다. 계수 h[k]를 **tap**이라고 부른다.

### 3.2 설계 1: windowed-sinc를 손으로

이상적인 LPF(cutoff fc 아래는 1, 위는 0)의 임펄스 응답은 sinc 함수다:

```
h_ideal[n] = (2·fc/fs) · sinc(2·fc/fs · n),   sinc(u) = sin(πu)/(πu),   n = −∞ … +∞
```

무한히 길고 인과적이지 않으니 (1) 가운데 N개만 잘라 (2) (N−1)/2 만큼 오른쪽으로 옮기고 (3) 자른 경계가 날카롭지 않게 **window를 곱한다**. 그냥 자르면(=rectangular window) 주파수 응답에 출렁임(Gibbs 현상)이 생긴다.

손계산: fs = 16 kHz, fc = 4 kHz → 2·fc/fs = 0.5. 가운데 h_ideal[0] = 0.5. n = ±1: 0.5·sin(0.5π)/(0.5π) = 0.5·(1/1.571) = 0.318. n = ±2: sin(π) = 0 → **0**. fc = fs/4 이면 짝수 offset 계수가 전부 0이 된다 — 이런 필터를 **half-band filter**라 하고, MAC이 절반이라 2배 decimator에 즐겨 쓴다(G4).

이 코드는 손으로 만든 Hamming windowed-sinc가 `scipy.signal.firwin`과 같은지, 선형 위상과 group delay, 그리고 window 유무에 따른 저지대역 감쇠를 확인한다.

```python
import numpy as np
from scipy import signal
fs, fc, N = 16000, 4000, 31                    # 4 kHz low-pass, 31 taps
M = (N - 1) / 2                                # 가운데 = 15
n = np.arange(N)
ideal = 2 * fc / fs * np.sinc(2 * fc / fs * (n - M))   # 이상적 LPF 임펄스 응답을 잘라 옮김
h_rect = ideal / ideal.sum()                            # 창 없음(=rectangular), DC 이득 1로
h_hamm = ideal * np.hamming(N); h_hamm /= h_hamm.sum()  # Hamming 창 곱
h_firwin = signal.firwin(N, fc, fs=fs)                  # 기본 window='hamming'
print("hand vs firwin max |diff|:", np.abs(h_hamm - h_firwin).max())
print("symmetric (linear phase)?", np.allclose(h_firwin, h_firwin[::-1]))
print("h[13..17]:", np.round(h_firwin[13:18], 4))
w, gd = signal.group_delay((h_firwin, 1), w=[500, 2000, 3500], fs=fs)
print("group delay (samples):", np.round(gd, 3), "→", M / fs * 1e3, "ms")
for name, h in [("rect", h_rect), ("hamming", h_hamm)]:
    f, H = signal.freqz(h, worN=4096, fs=fs)
    stop = 20 * np.log10(np.abs(H[f >= 5000]).max())
    ripple = np.ptp(20 * np.log10(np.abs(H[f <= 3000])))
    print(f"{name:8s} passband ripple(0-3k) {ripple:5.2f} dB, worst stopband(>=5k) {stop:6.1f} dB")
```

```text
hand vs firwin max |diff|: 6.938893903907228e-18
symmetric (linear phase)? True
h[13..17]: [0.     0.3156 0.5008 0.3156 0.    ]
group delay (samples): [15. 15. 15.] → 0.9375 ms
rect     passband ripple(0-3k)  0.74 dB, worst stopband(>=5k)  -25.9 dB
hamming  passband ripple(0-3k)  0.04 dB, worst stopband(>=5k)  -51.4 dB
```

출력에서 볼 것: 손 설계와 `firwin`이 1e-17 안에서 같다 — `firwin`은 정확히 이 공식이다(기본 window Hamming, DC 이득 1로 정규화). 가운데 계수 0.5008, 이웃 0.3156(손계산 0.318에 Hamming 가중과 정규화가 곱해짐), 두 칸 옆은 0. 창 없이 자르면 저지대역이 −26 dB밖에 안 되고 통과대역 ripple 0.74 dB, Hamming은 −51 dB / 0.04 dB다.

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="250" x2="620" y2="250" stroke="currentColor"/> <line x1="60" y1="30" x2="60" y2="250" stroke="currentColor"/> <line x1="60" y1="30.0" x2="620" y2="30.0" stroke="#888" stroke-width="0.4" stroke-dasharray="3 3"/> <text x="54" y="34.0" font-size="12" text-anchor="end">0</text> <line x1="60" y1="74.0" x2="620" y2="74.0" stroke="#888" stroke-width="0.4" stroke-dasharray="3 3"/> <text x="54" y="78.0" font-size="12" text-anchor="end">-20</text> <line x1="60" y1="118.0" x2="620" y2="118.0" stroke="#888" stroke-width="0.4" stroke-dasharray="3 3"/> <text x="54" y="122.0" font-size="12" text-anchor="end">-40</text> <line x1="60" y1="162.0" x2="620" y2="162.0" stroke="#888" stroke-width="0.4" stroke-dasharray="3 3"/> <text x="54" y="166.0" font-size="12" text-anchor="end">-60</text> <line x1="60" y1="206.0" x2="620" y2="206.0" stroke="#888" stroke-width="0.4" stroke-dasharray="3 3"/> <text x="54" y="210.0" font-size="12" text-anchor="end">-80</text> <line x1="60" y1="250.0" x2="620" y2="250.0" stroke="#888" stroke-width="0.4" stroke-dasharray="3 3"/> <text x="54" y="254.0" font-size="12" text-anchor="end">-100</text> <text x="60.0" y="266" font-size="12" text-anchor="middle">0k</text> <text x="200.0" y="266" font-size="12" text-anchor="middle">2k</text> <text x="340.0" y="266" font-size="12" text-anchor="middle">4k</text> <text x="480.0" y="266" font-size="12" text-anchor="middle">6k</text> <text x="620.0" y="266" font-size="12" text-anchor="middle">8k</text>
<polyline fill="none" stroke="#e08a3c" stroke-width="1.6" points="60.0,30.0 61.4,30.0 62.8,30.0 64.2,30.0 65.6,30.0 67.0,29.9 68.4,29.9 69.8,29.9 71.2,29.8 72.6,29.8 74.0,29.7 75.4,29.7 76.8,29.6 78.2,29.6 79.6,29.5 81.0,29.5 82.4,29.5 83.8,29.4 85.2,29.4 86.6,29.3 88.0,29.3 89.4,29.3 90.8,29.3 92.2,29.2 93.6,29.2 95.0,29.2 96.4,29.2 97.8,29.2 99.2,29.3 100.6,29.3 102.0,29.3 103.4,29.3 104.8,29.4 106.2,29.4 107.6,29.5 109.0,29.5 110.4,29.6 111.8,29.6 113.2,29.6 114.6,29.7 116.0,29.7 117.4,29.8 118.8,29.8 120.2,29.9 121.6,29.9 123.0,30.0 124.4,30.0 125.8,30.0 127.2,30.0 128.6,30.0 130.0,30.0 131.4,30.0 132.8,30.0 134.2,30.0 135.6,30.0 137.0,30.0 138.4,29.9 139.8,29.9 141.2,29.8 142.6,29.8 144.0,29.7 145.4,29.7 146.8,29.6 148.2,29.6 149.6,29.5 151.0,29.5 152.4,29.4 153.8,29.4 155.2,29.3 156.6,29.3 158.0,29.3 159.4,29.2 160.8,29.2 162.2,29.2 163.6,29.2 165.0,29.2 166.4,29.2 167.8,29.2 169.2,29.2 170.6,29.2 172.0,29.3 173.4,29.3 174.8,29.3 176.2,29.4 177.6,29.4 179.0,29.5 180.4,29.6 181.8,29.6 183.2,29.7 184.6,29.7 186.0,29.8 187.4,29.9 188.8,29.9 190.2,30.0 191.6,30.0 193.0,30.1 194.4,30.1 195.8,30.1 197.2,30.1 198.6,30.2 200.0,30.2 201.4,30.2 202.8,30.1 204.2,30.1 205.6,30.1 207.0,30.0 208.4,30.0 209.8,29.9 211.2,29.9 212.6,29.8 214.0,29.8 215.4,29.7 216.8,29.6 218.2,29.5 219.6,29.5 221.0,29.4 222.4,29.3 223.8,29.2 225.2,29.2 226.6,29.1 228.0,29.1 229.4,29.0 230.8,29.0 232.2,29.0 233.6,29.0 235.0,29.0 236.4,29.0 237.8,29.0 239.2,29.0 240.6,29.0 242.0,29.1 243.4,29.1 244.8,29.2 246.2,29.3 247.6,29.4 249.0,29.5 250.4,29.6 251.8,29.7 253.2,29.8 254.6,29.9 256.0,30.0 257.4,30.1 258.8,30.2 260.2,30.2 261.6,30.3 263.0,30.4 264.4,30.5 265.8,30.5 267.2,30.6 268.6,30.6 270.0,30.6 271.4,30.6 272.8,30.6 274.2,30.5 275.6,30.5 277.0,30.4 278.4,30.3 279.8,30.2 281.2,30.1 282.6,29.9 284.0,29.8 285.4,29.6 286.8,29.5 288.2,29.3 289.6,29.1 291.0,29.0 292.4,28.8 293.8,28.7 295.2,28.5 296.6,28.4 298.0,28.3 299.4,28.2 300.8,28.1 302.2,28.0 303.6,28.0 305.0,28.0 306.4,28.0 307.8,28.0 309.2,28.1 310.6,28.2 312.0,28.4 313.4,28.6 314.8,28.8 316.2,29.1 317.6,29.4 319.0,29.7 320.4,30.2 321.8,30.6 323.2,31.1 324.6,31.7 326.0,32.4 327.4,33.1 328.8,33.8 330.2,34.7 331.6,35.6 333.0,36.6 334.4,37.6 335.8,38.8 337.2,40.0 338.6,41.4 340.0,42.9 341.4,44.5 342.8,46.2 344.2,48.1 345.6,50.1 347.0,52.4 348.4,54.8 349.8,57.6 351.2,60.6 352.6,64.1 354.0,68.1 355.4,72.9 356.8,78.7 358.2,86.5 359.6,98.3 361.0,126.7 362.4,112.7 363.8,96.7 365.2,89.0 366.6,84.3 368.0,81.1 369.4,78.9 370.8,77.3 372.2,76.3 373.6,75.8 375.0,75.6 376.4,75.8 377.8,76.3 379.2,77.1 380.6,78.2 382.0,79.7 383.4,81.5 384.8,83.8 386.2,86.6 387.6,90.1 389.0,94.4 390.4,100.2 391.8,108.5 393.2,123.2 394.6,164.7 396.0,120.1 397.4,108.4 398.8,101.7 400.2,97.1 401.6,93.9 403.0,91.5 404.4,89.7 405.8,88.4 407.2,87.5 408.6,87.1 410.0,86.9 411.4,87.1 412.8,87.5 414.2,88.3 415.6,89.4 417.0,90.9 418.4,92.7 419.8,95.1 421.2,98.0 422.6,101.7 424.0,106.6 425.4,113.3 426.8,123.9 428.2,149.4 429.6,138.9 431.0,121.2 432.4,112.6 433.8,107.1 435.2,103.1 436.6,100.2 438.0,97.9 439.4,96.3 440.8,95.1 442.2,94.2 443.6,93.8 445.0,93.6 446.4,93.8 447.8,94.2 449.2,95.0 450.6,96.1 452.0,97.6 453.4,99.5 454.8,101.9 456.2,104.9 457.6,108.8 459.0,114.0 460.4,121.4 461.8,134.0 463.2,184.0 464.6,137.4 466.0,123.6 467.4,116.0 468.8,110.9 470.2,107.2 471.6,104.4 473.0,102.3 474.4,100.7 475.8,99.5 477.2,98.7 478.6,98.2 480.0,98.1 481.4,98.2 482.8,98.7 484.2,99.4 485.6,100.6 487.0,102.0 488.4,104.0 489.8,106.4 491.2,109.5 492.6,113.6 494.0,119.0 495.4,127.0 496.8,141.3 498.2,182.5 499.6,137.6 501.0,125.5 502.4,118.5 503.8,113.6 505.2,110.0 506.6,107.3 508.0,105.2 509.4,103.7 510.8,102.5 512.2,101.7 513.6,101.2 515.0,101.1 516.4,101.2 517.8,101.7 519.2,102.5 520.6,103.6 522.0,105.1 523.4,107.0 524.8,109.5 526.2,112.7 527.6,116.8 529.0,122.5 530.4,131.0 531.8,147.0 533.2,169.0 534.6,137.8 536.0,126.8 537.4,120.1 538.8,115.4 540.2,111.9 541.6,109.2 543.0,107.2 544.4,105.6 545.8,104.5 547.2,103.7 548.6,103.2 550.0,103.1 551.4,103.2 552.8,103.7 554.2,104.4 555.6,105.6 557.0,107.1 558.4,109.0 559.8,111.6 561.2,114.8 562.6,119.1 564.0,124.9 565.4,133.8 566.8,151.6 568.2,163.2 569.6,137.7 571.0,127.3 572.4,120.9 573.8,116.3 575.2,112.9 576.6,110.3 578.0,108.3 579.4,106.7 580.8,105.6 582.2,104.8 583.6,104.3 585.0,104.2 586.4,104.3 587.8,104.8 589.2,105.6 590.6,106.7 592.0,108.2 593.4,110.2 594.8,112.8 596.2,116.1 597.6,120.4 599.0,126.4 600.4,135.8 601.8,155.5 603.2,159.2 604.6,137.0 606.0,127.2 607.4,121.0 608.8,116.5 610.2,113.2 611.6,110.6 613.0,108.6 614.4,107.1 615.8,105.9 617.2,105.2 618.6,104.7"/>
<line x1="440" y1="46" x2="465" y2="46" stroke="#e08a3c" stroke-width="2"/> <text x="470" y="50" font-size="12">창 없음(rect): 저지대역 약 -26 dB</text>
<polyline fill="none" stroke="#4a7bd0" stroke-width="1.6" points="60.0,30.0 61.4,30.0 62.8,30.0 64.2,30.0 65.6,30.0 67.0,30.0 68.4,30.0 69.8,30.0 71.2,30.0 72.6,30.0 74.0,30.0 75.4,30.0 76.8,30.0 78.2,30.0 79.6,30.0 81.0,30.0 82.4,30.0 83.8,30.0 85.2,30.0 86.6,29.9 88.0,29.9 89.4,29.9 90.8,29.9 92.2,29.9 93.6,29.9 95.0,29.9 96.4,29.9 97.8,29.9 99.2,29.9 100.6,29.9 102.0,29.9 103.4,29.9 104.8,29.9 106.2,30.0 107.6,30.0 109.0,30.0 110.4,30.0 111.8,30.0 113.2,30.0 114.6,30.0 116.0,30.0 117.4,30.0 118.8,30.0 120.2,30.0 121.6,30.0 123.0,30.0 124.4,30.0 125.8,30.0 127.2,30.0 128.6,30.0 130.0,30.0 131.4,30.0 132.8,30.0 134.2,30.0 135.6,30.0 137.0,30.0 138.4,30.0 139.8,30.0 141.2,30.0 142.6,30.0 144.0,30.0 145.4,30.0 146.8,30.0 148.2,30.0 149.6,30.0 151.0,30.0 152.4,30.0 153.8,30.0 155.2,30.0 156.6,29.9 158.0,29.9 159.4,29.9 160.8,29.9 162.2,29.9 163.6,29.9 165.0,29.9 166.4,29.9 167.8,29.9 169.2,29.9 170.6,29.9 172.0,29.9 173.4,29.9 174.8,29.9 176.2,29.9 177.6,29.9 179.0,30.0 180.4,30.0 181.8,30.0 183.2,30.0 184.6,30.0 186.0,30.0 187.4,30.0 188.8,30.0 190.2,30.0 191.6,30.0 193.0,30.0 194.4,30.0 195.8,30.0 197.2,30.0 198.6,30.0 200.0,30.0 201.4,30.0 202.8,30.0 204.2,30.0 205.6,30.0 207.0,30.0 208.4,30.0 209.8,30.0 211.2,30.0 212.6,30.0 214.0,30.0 215.4,30.0 216.8,30.0 218.2,30.0 219.6,30.0 221.0,30.0 222.4,30.0 223.8,30.0 225.2,30.0 226.6,30.0 228.0,30.0 229.4,30.0 230.8,30.0 232.2,29.9 233.6,29.9 235.0,29.9 236.4,29.9 237.8,29.9 239.2,29.9 240.6,29.9 242.0,29.9 243.4,29.9 244.8,29.9 246.2,29.9 247.6,29.9 249.0,29.9 250.4,29.9 251.8,29.9 253.2,29.9 254.6,29.9 256.0,29.9 257.4,29.9 258.8,29.9 260.2,29.9 261.6,29.9 263.0,29.9 264.4,29.9 265.8,29.9 267.2,29.9 268.6,29.9 270.0,29.9 271.4,30.0 272.8,30.0 274.2,30.0 275.6,30.0 277.0,30.0 278.4,30.0 279.8,30.1 281.2,30.1 282.6,30.1 284.0,30.2 285.4,30.2 286.8,30.3 288.2,30.3 289.6,30.4 291.0,30.5 292.4,30.6 293.8,30.7 295.2,30.8 296.6,30.9 298.0,31.0 299.4,31.1 300.8,31.3 302.2,31.5 303.6,31.6 305.0,31.8 306.4,32.0 307.8,32.3 309.2,32.5 310.6,32.8 312.0,33.0 313.4,33.3 314.8,33.6 316.2,34.0 317.6,34.3 319.0,34.7 320.4,35.1 321.8,35.5 323.2,35.9 324.6,36.4 326.0,36.9 327.4,37.4 328.8,37.9 330.2,38.5 331.6,39.1 333.0,39.7 334.4,40.3 335.8,41.0 337.2,41.7 338.6,42.4 340.0,43.2 341.4,44.0 342.8,44.8 344.2,45.7 345.6,46.6 347.0,47.5 348.4,48.5 349.8,49.5 351.2,50.6 352.6,51.6 354.0,52.8 355.4,53.9 356.8,55.1 358.2,56.4 359.6,57.7 361.0,59.0 362.4,60.4 363.8,61.9 365.2,63.4 366.6,64.9 368.0,66.5 369.4,68.2 370.8,69.9 372.2,71.6 373.6,73.5 375.0,75.4 376.4,77.4 377.8,79.4 379.2,81.5 380.6,83.8 382.0,86.1 383.4,88.5 384.8,91.0 386.2,93.6 387.6,96.3 389.0,99.2 390.4,102.2 391.8,105.4 393.2,108.8 394.6,112.4 396.0,116.2 397.4,120.5 398.8,125.1 400.2,130.3 401.6,136.4 403.0,143.8 404.4,153.5 405.8,169.2 407.2,220.7 408.6,170.2 410.0,159.4 411.4,153.7 412.8,150.1 414.2,147.7 415.6,146.0 417.0,144.9 418.4,144.1 419.8,143.6 421.2,143.3 422.6,143.1 424.0,143.1 425.4,143.2 426.8,143.3 428.2,143.6 429.6,143.9 431.0,144.3 432.4,144.7 433.8,145.3 435.2,145.9 436.6,146.6 438.0,147.5 439.4,148.5 440.8,149.6 442.2,150.9 443.6,152.5 445.0,154.3 446.4,156.5 447.8,159.1 449.2,162.3 450.6,166.5 452.0,171.9 453.4,179.9 454.8,194.2 456.2,233.3 457.6,189.8 459.0,177.7 460.4,170.5 461.8,165.4 463.2,161.6 464.6,158.6 466.0,156.2 467.4,154.3 468.8,152.8 470.2,151.6 471.6,150.7 473.0,150.1 474.4,149.7 475.8,149.6 477.2,149.8 478.6,150.2 480.0,150.9 481.4,151.9 482.8,153.2 484.2,154.8 485.6,157.0 487.0,159.6 488.4,163.1 489.8,167.6 491.2,173.8 492.6,183.5 494.0,205.1 495.4,203.3 496.8,183.0 498.2,173.6 499.6,167.5 501.0,163.2 502.4,159.8 503.8,157.3 505.2,155.3 506.6,153.7 508.0,152.5 509.4,151.7 510.8,151.1 512.2,150.9 513.6,150.9 515.0,151.3 516.4,151.9 517.8,152.8 519.2,154.1 520.6,155.7 522.0,157.8 523.4,160.5 524.8,164.0 526.2,168.6 527.6,175.0 529.0,185.1 530.4,208.7 531.8,201.6 533.2,182.9 534.6,173.8 536.0,167.9 537.4,163.6 538.8,160.4 540.2,157.9 541.6,155.9 543.0,154.4 544.4,153.3 545.8,152.6 547.2,152.1 548.6,151.9 550.0,152.0 551.4,152.5 552.8,153.2 554.2,154.2 555.6,155.6 557.0,157.5 558.4,159.8 559.8,162.8 561.2,166.8 562.6,172.0 564.0,179.8 565.4,193.6 566.8,244.5 568.2,191.1 569.6,178.7 571.0,171.4 572.4,166.4 573.8,162.7 575.2,159.8 576.6,157.6 578.0,155.8 579.4,154.5 580.8,153.6 582.2,153.0 583.6,152.6 585.0,152.6 586.4,152.9 587.8,153.5 589.2,154.4 590.6,155.6 592.0,157.2 593.4,159.4 594.8,162.1 596.2,165.6 597.6,170.2 599.0,176.8 600.4,187.4 601.8,214.2 603.2,200.2 604.6,182.9 606.0,174.2 607.4,168.5 608.8,164.3 610.2,161.1 611.6,158.7 613.0,156.7 614.4,155.3 615.8,154.2 617.2,153.4 618.6,153.0"/>
<line x1="440" y1="66" x2="465" y2="66" stroke="#4a7bd0" stroke-width="2"/> <text x="470" y="70" font-size="12">Hamming: 저지대역 약 -51 dB</text> <line x1="340.0" y1="30" x2="340.0" y2="250" stroke="#d0564a" stroke-dasharray="4 3"/> <text x="344.0" y="180" font-size="12">fc = 4 kHz</text> <text x="340.0" y="288" font-size="12" text-anchor="middle">주파수 (Hz) — 31-tap windowed-sinc LPF, fs = 16 kHz, 세로축 |H| (dB)</text>
</svg>
```

그림 4 — 같은 31-tap sinc를 그냥 자른 것(주황)과 Hamming window를 곱한 것(파랑)의 주파수 응답(dB). 실제 `freqz` 계산값. window는 전이대역을 조금 넓히는 대신 저지대역을 25 dB 더 깊게 판다. 이 trade-off는 5절 FFT window에서 그대로 다시 나온다.

### 3.3 설계 2: `firwin`과 tap 수 추정

실무에서는 사양(통과대역 끝, 저지대역 시작, 감쇠 dB)을 정하고 tap 수를 추정한다. `scipy.signal.kaiserord(감쇠dB, 전이폭/Nyquist)`가 Kaiser window 공식으로 tap 수와 β를 준다. 60 dB, 전이대역 1 kHz @ 16 kHz면 60 tap이 나온다(ex25 출력, 5.4절). 기억할 비례 관계: **tap 수 ∝ 감쇠(dB) / 전이폭**. 전이대역을 반으로 좁히면 tap이 두 배다. 다른 설계법으로 `scipy.signal.remez`(Parks-McClellan, equiripple)와 `firls`(최소제곱)가 있다.

### 3.4 선형 위상과 group delay

h가 가운데를 기준으로 **대칭**이면 위상이 주파수에 정비례한다(linear phase). 그러면 모든 주파수가 **똑같이 (N−1)/2 샘플** 늦게 나온다 — 파형 모양이 보존된다. 늦어지는 양을 **group delay**(−dφ/dω)라 한다. ex6에서 31 tap → 15 샘플 = 0.94 ms.

- 오디오 ML에서 선형 위상이 좋은 이유: 과도음(자음의 시작, click)이 주파수별로 흩어지지 않는다.
- 비용: 지연. 1000-tap FIR이면 500 샘플 = 31 ms 지연이 생긴다. always-on wake word의 지연 예산(D6)에 들어간다.
- 대칭이면 계수 저장을 절반으로, 곱셈을 절반으로 줄일 수 있다: h[k]·(x[n−k] + x[n−N+1+k]).

### 3.5 C 구현: direct-form FIR, float와 Q15

이 C 코드는 같은 31-tap 필터를 float와 Q15(64-bit 누산)로 구현하고, 누산기가 실제로 몇 비트까지 쓰였는지 잰다. E4 2.3~2.4절의 Q15 곱셈·guard bit를 그대로 쓴다.

```c
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#define NT 31
#define NX 2000
static float h[NT], x[NX], yf[NX];
static int16_t hq[NT], xq[NX], yq[NX];
static int16_t sat16(int64_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v; }
/* direct-form FIR, float: y[n] = Σ h[k]·x[n-k] (n-k<0 은 0) */
static void fir_f32(const float *c, const float *in, float *out, int n) {
    for (int i = 0; i < n; i++) {
        float acc = 0.0f;
        for (int k = 0; k < NT && k <= i; k++) acc += c[k] * in[i - k];
        out[i] = acc;
    }
}
/* Q15 FIR: Q15×Q15 = Q30 곱을 64-bit 누산 → 반올림 → >>15 → 포화 */
static int64_t acc_max = 0;
static void fir_q15(const int16_t *c, const int16_t *in, int16_t *out, int n) {
    for (int i = 0; i < n; i++) {
        int64_t acc = 0;
        for (int k = 0; k < NT && k <= i; k++) acc += (int32_t)c[k] * in[i - k];
        if (llabs(acc) > acc_max) acc_max = llabs(acc);
        out[i] = sat16((acc + (1 << 14)) >> 15);
    }
}
int main(void) {
    FILE *f = fopen("h.f32", "rb"); fread(h, 4, NT, f); fclose(f);
    f = fopen("x.f32", "rb"); fread(x, 4, NX, f); fclose(f);
    for (int k = 0; k < NT; k++) hq[k] = sat16((int64_t)(h[k] * 32768.0f + (h[k] >= 0 ? 0.5f : -0.5f)));
    for (int i = 0; i < NX; i++) xq[i] = sat16((int64_t)(x[i] * 32768.0f + (x[i] >= 0 ? 0.5f : -0.5f)));
    fir_f32(h, x, yf, NX);
    fir_q15(hq, xq, yq, NX);
    f = fopen("yf.f32", "wb"); fwrite(yf, 4, NX, f); fclose(f);
    f = fopen("yq.i16", "wb"); fwrite(yq, 2, NX, f); fclose(f);
    int bits = 0; while ((1LL << bits) <= acc_max) bits++;
    printf("hq[15]=%d  max|acc|=%lld (needs %d bits + sign)\n", hq[15], (long long)acc_max, bits);
    printf("MACs per output = %d, at 16 kHz = %d MAC/s\n", NT, NT * 16000);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 fir.c -o fir -lm     # 경고 0개
```

이 Python 코드는 계수와 입력(1 kHz + 6 kHz + 잡음)을 만들어 C에 넘기고, 결과를 `scipy.signal.lfilter`(float64) 기준값과 비교한다.

```python
import numpy as np, subprocess
from scipy import signal
fs = 16000
h = signal.firwin(31, 4000, fs=fs).astype(np.float32)
rng = np.random.default_rng(0)
t = np.arange(2000) / fs
x = (0.4*np.sin(2*np.pi*1000*t) + 0.4*np.sin(2*np.pi*6000*t) + 0.05*rng.standard_normal(2000)).astype(np.float32)
h.tofile("h.f32"); x.tofile("x.f32")
print(subprocess.run(["./fir"], capture_output=True, text=True).stdout, end="")
ref = signal.lfilter(h.astype(np.float64), 1, x.astype(np.float64))   # float64 기준값
yf = np.fromfile("yf.f32", np.float32); yq = np.fromfile("yq.i16", np.int16) / 32768
print("float C vs lfilter max |diff|:", float(np.abs(yf - ref).max()))
err = yq - ref
print("Q15  C vs lfilter max |diff|:", float(np.abs(err).max()), " SNR = %.1f dB" % (10*np.log10(np.mean(ref**2)/np.mean(err**2))))
```

```text
hq[15]=16410  max|acc|=538270167 (needs 30 bits + sign)
MACs per output = 31, at 16 kHz = 496000 MAC/s
float C vs lfilter max |diff|: 8.5768232127581e-08
Q15  C vs lfilter max |diff|: 3.714317914327303e-05  SNR = 87.0 dB
```

출력에서 볼 것:

- float C는 `lfilter`와 1e-7(float32 반올림) 안에서 같다 — **golden vector 검증**의 기본 형태다.
- Q15는 SNR 87 dB. 계수 31개와 입력을 각각 Q15로 반올림한 잡음이 쌓인 값이다. 16-bit 오디오(98 dB)보다 조금 낮다.
- 누산기는 최대 5.4억 ≈ 2²⁹ — 31개 Q30 곱의 합이라 이 입력에서는 32-bit에 들어갔지만, 최악의 입력(full-scale, 계수 부호와 맞는 패턴)에서는 Σ|h[k]| 배까지 커진다. 그래서 DSP는 **40-bit 누산기(guard bit 8개)** 를 두고, CMSIS-DSP의 `arm_fir_q15`는 문서상 64-bit 누산기(34.30 형식)를 쓴다. 더 빠른 `arm_fir_fast_q15`는 32-bit 누산이라 overflow 여유가 작다 — 이름에 "fast"가 붙은 함수는 정밀도를 판 것이다.

**비용**: tap × 샘플레이트. 31 tap @ 16 kHz = 0.5 M MAC/s. Cortex-M4의 SIMD 명령(`SMLAD`, 16-bit 곱 2개를 한 번에)을 쓰면 사이클당 2 MAC에 가깝게 갈 수 있다(E2). 60-tap 고품질 리샘플러 @ 48 kHz면 2.9 M MAC/s — 여전히 작지만, 마이크 4개 × 필터 여러 단이면 무시할 수 없다.

### 3.6 함정

- **Q15 계수 합이 1을 넘는 필터**(통과대역 이득 > 1, 또는 큰 중앙 계수) → 출력 포화. 계수를 줄이고 나중에 시프트로 복원한다.
- **delay line 초기화**: 스트림 시작에서 상태를 0으로 두면 처음 N−1개 출력은 transient다. 학습 데이터 잘라 쓰기와 기기 동작이 다르면 첫 frame 값이 다르다.

---

## 4. IIR 필터

### 4.1 직관과 정의: 피드백이 있는 필터

**IIR(Infinite Impulse Response)** 필터는 과거 **출력**도 다시 쓴다. 피드백 덕분에 임펄스 응답이 (이론상) 영원히 이어지고, **아주 적은 계수로 날카로운 응답**을 만든다. 대신 극점이 있어서 불안정해질 수 있고, 위상이 비선형이다.

가장 중요한 단위가 2차 IIR, **biquad**다:

```
H(z) = (b0 + b1·z⁻¹ + b2·z⁻²) / (1 + a1·z⁻¹ + a2·z⁻²)
y[n] = b0·x[n] + b1·x[n−1] + b2·x[n−2] − a1·y[n−1] − a2·y[n−2]
```

말로 하면: 곱 5번, 덧셈 4번, 상태 몇 개로 극점 한 쌍과 영점 한 쌍을 만든다. 4차, 8차 필터는 biquad를 줄줄이 이어(cascade) 만든다.

### 4.2 구조: DF1, DF2, DF2T

같은 H(z)라도 계산 순서(구조)에 따라 상태 수와 수치 특성이 다르다.

```
DF1 — 상태 4개 (x1, x2 = 과거 입력, y1, y2 = 과거 출력)
    y  = b0·x + b1·x1 + b2·x2 − a1·y1 − a2·y2      ← 곱 5개를 누산기 하나에
    x2 = x1;  x1 = x;  y2 = y1;  y1 = y

DF2T — 상태 2개 (s1, s2 = 부분합)
    y  = b0·x + s1
    s1 = b1·x − a1·y + s2
    s2 = b2·x − a2·y
```

| 구조 | 상태 수 | 장점 | 단점 | 주 사용처 |
|---|---|---|---|---|
| DF1 | 4 | 상태가 실제 입력·출력이라 범위 예측 쉬움, 누산기 하나에 다 모음 | 상태 메모리 2배 | **고정소수점**(CMSIS `arm_biquad_cascade_df1_q15/q31`) |
| DF2 (canonical) | 2 | 메모리 최소 | 내부 노드가 극점 이득만큼 커져 overflow | 거의 안 씀 |
| DF2T | 2 | 메모리 최소, float에서 수치 특성 좋음 | 고정소수점에서 상태 범위가 큼 | **부동소수점**(CMSIS `arm_biquad_cascade_df2T_f32`, scipy `sosfilt`) |

### 4.3 Butterworth 설계와 SOS — 고차 IIR은 쪼개라

**Butterworth**는 통과대역이 최대로 평탄한(ripple 없는) 고전 필터다. `scipy.signal.butter(order, cutoff, fs=..., output=...)`로 만든다. 핵심은 `output`이다.

- `output="ba"`(기본): 분자·분모 다항식 하나씩. 8차면 계수 9개짜리 다항식.
- `output="sos"`: **second-order sections**, biquad 4개의 계수 표 `[n_sections, 6] = [b0 b1 b2 a0 a1 a2]`.

고차 다항식의 근(극점)은 계수의 작은 오차에 **극도로 민감**하다. 특히 cutoff가 fs에 비해 낮으면 극점이 z = 1 근처에 몰려 있어서, 계수 하나의 마지막 자리가 극점을 단위원 밖으로 민다.

이 코드는 8차 100 Hz LPF를 ba와 sos로 만들고, ba 계수를 float32로 저장했을 때 무슨 일이 생기는지 본다.

```python
import numpy as np
from scipy import signal
fs = 16000
b, a = signal.butter(8, 100, fs=fs)                 # 8차 LPF, 100 Hz — 극점이 z=1 근처에 몰린다
sos = signal.butter(8, 100, fs=fs, output="sos")    # 같은 필터를 2차 구간 4개로
print("sos shape:", sos.shape)                      # (구간 수, 6) = [b0 b1 b2 a0 a1 a2]
print("a coeffs:", np.array2string(a[:4], precision=3), "...")
print("b[0] =", b[0])
p_true = np.sort_complex(signal.sos2zpk(sos)[1])
p_ba = np.sort_complex(np.roots(a))
print("max |pole| (sos):", np.abs(p_true).max().round(6), " (ba roots):", np.abs(p_ba).max().round(6))
a32 = a.astype(np.float32).astype(np.float64)       # 계수를 float32로 저장했다면?
print("max |pole| ba with float32 coeffs:", np.abs(np.roots(a32)).max().round(6))
x = np.ones(16000)                                  # step 입력 1초
y_ba = signal.lfilter(b, a, x); y_sos = signal.sosfilt(sos, x)
y_ba32 = signal.lfilter(b.astype(np.float32), a32.astype(np.float32), x)
print("step response end: sos %.6f  ba(f64) %.6f  ba(f32 coeffs) %.3e" % (y_sos[-1], y_ba[-1], y_ba32[-1]))
```

```text
sos shape: (4, 6)
a coeffs: [  1.     -7.799  26.611 -51.893] ...
b[0] = 1.9997340480065263e-14
max |pole| (sos): 0.99237  (ba roots): 0.992322
max |pole| ba with float32 coeffs: 1.140249
step response end: sos 1.000000  ba(f64) 1.001092  ba(f32 coeffs) nan
```

출력에서 볼 것: 분모 계수가 1, −7.8, 26.6, −51.9, …로 크고 부호가 번갈아 나온다 — 거대한 수들이 거의 상쇄되어 작은 결과를 내는 구조라 반올림에 약하다. 분자 b[0]은 2e-14. float64 ba는 그나마 step 응답이 1.001(0.1% 오차)이지만, **계수를 float32로만 저장해도 극점이 1.14로 단위원 밖**에 나가서 출력이 `nan`(발산 후 overflow)이 된다. sos는 같은 필터를 biquad 4개로 나눠 각 구간의 극점이 자기 계수 2개에만 의존하므로 훨씬 튼튼하다.

규칙: **IIR은 2차보다 높으면 무조건 SOS로 설계하고 SOS로 구현한다.** scipy 문서도 일반적인 필터링에 sos 사용을 권한다.

**CMSIS-DSP 부호 주의**: CMSIS biquad 함수의 계수 배열은 stage마다 `{b0, b1, b2, a1, a2}`이고 차분 방정식이 `y = b0·x[n] + … + a1·y[n−1] + a2·y[n−2]` — 즉 **a1, a2를 scipy와 반대 부호로** 넣어야 한다. Q15 DF1 버전은 SIMD 정렬 때문에 stage마다 `{b0, 0, b1, b2, a1, a2}`처럼 0이 하나 끼고, 계수 범위를 넘기 위한 `postShift` 인자가 있다. 정확한 배열 형식은 사용하는 CMSIS-DSP 버전 문서를 확인하자.

### 4.4 C 구현: DF2T biquad cascade, `sosfilt`와 대조

이 C 코드는 scipy sos 표를 읽어 DF2T biquad 4단을 샘플 단위로 통과시킨다.

```c
#include <stdio.h>
#define NS 4          /* 2차 구간 수 */
#define NX 4000
typedef struct { float b0, b1, b2, a1, a2; float s1, s2; } biquad_t;   /* s1,s2 = DF2T 상태 */
/* Direct Form II Transposed: 상태 2개, 곱 5개, 덧셈 4개 */
static inline float biquad_df2t(biquad_t *q, float x) {
    float y = q->b0 * x + q->s1;
    q->s1 = q->b1 * x - q->a1 * y + q->s2;
    q->s2 = q->b2 * x - q->a2 * y;
    return y;
}
int main(void) {
    static float sos[NS * 6], x[NX], y[NX];
    FILE *f = fopen("sos.f32", "rb"); size_t r = fread(sos, 4, NS * 6, f); fclose(f);
    f = fopen("xb.f32", "rb"); r += fread(x, 4, NX, f); fclose(f);
    biquad_t q[NS];
    for (int s = 0; s < NS; s++) {              /* scipy sos 행 = [b0 b1 b2 a0(=1) a1 a2] */
        const float *c = &sos[s * 6];
        q[s] = (biquad_t){ c[0], c[1], c[2], c[4], c[5], 0.0f, 0.0f };
    }
    for (int n = 0; n < NX; n++) {              /* 샘플 하나가 구간 4개를 차례로 통과 */
        float v = x[n];
        for (int s = 0; s < NS; s++) v = biquad_df2t(&q[s], v);
        y[n] = v;
    }
    f = fopen("yb.f32", "wb"); fwrite(y, 4, NX, f); fclose(f);
    printf("read %zu floats, MACs per sample = %d\n", r, NS * 5);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 biquad.c -o biquad -lm     # 경고 0개
```

이 Python 코드는 300~3400 Hz Butterworth band-pass(`butter(4, ...)`, 실제 8차 = 구간 4개)를 설계해 C 결과를 `sosfilt`(float64)와 비교한다.

```python
import numpy as np, subprocess
from scipy import signal
fs = 16000
sos = signal.butter(4, [300, 3400], btype="bandpass", fs=fs, output="sos")  # 8차 BPF = 4 구간
rng = np.random.default_rng(1)
x = rng.standard_normal(4000).astype(np.float32) * 0.3
sos.astype(np.float32).tofile("sos.f32"); x.tofile("xb.f32")
print(subprocess.run(["./biquad"], capture_output=True, text=True).stdout, end="")
ref = signal.sosfilt(sos, x.astype(np.float64))
yb = np.fromfile("yb.f32", np.float32)
print("C DF2T vs sosfilt max |diff|: %.2e  (rms of y = %.3f)" % (np.abs(yb - ref).max(), ref.std()))
```

```text
read 4024 floats, MACs per sample = 20
C DF2T vs sosfilt max |diff|: 8.90e-07  (rms of y = 0.187)
```

출력에서 볼 것: float32 C와 float64 `sosfilt`의 차이가 9e-7 — 출력 RMS 0.19 대비 float32 정밀도 수준이다. 비용은 샘플당 곱 20번. 같은 날카로움을 FIR로 만들려면 수십~수백 tap이 필요하다(4.7절 표).

`butter(4, [300, 3400], btype="bandpass")`가 **8차**(구간 4개)라는 점도 기억하자: band-pass는 차수를 두 배로 만든다. 전화 대역 300~3400 Hz는 고전적인 음성 대역 예시다.

### 4.5 고정소수점 IIR 함정 1: 계수 양자화가 극점을 옮긴다

Q15 FIR에서 계수 반올림은 "약간의 잡음"이었지만, IIR에서는 **극점 위치 자체**가 바뀐다. cutoff가 낮을수록 극점이 z = 1에 가까워져서 더 민감하다. 또 a1은 −2 ~ 2 범위라 Q15(−1 ~ 1)에 안 들어간다 — Q1.14(소수 14비트) 같은 형식으로 저장하고 결과를 1비트 더 시프트한다(CMSIS의 `postShift`).

이 코드는 50 Hz @ 16 kHz 2차 Butterworth LPF의 a1, a2를 소수 14/12/10 비트로 반올림했을 때 극점이 어디로 가는지, 그리고 Q15 정수 연산의 limit cycle을 본다.

```python
import numpy as np
from scipy import signal
fs = 16000
b, a = signal.butter(2, 50, fs=fs)                   # 50 Hz LPF biquad — 극점이 z=1에 아주 가깝다
def pole_info(a1, a2):
    p = np.roots([1, a1, a2])
    return np.round(p, 5)
print("float a1=%.6f a2=%.6f  poles=%s" % (a[1], a[2], pole_info(a[1], a[2])))
for frac_bits in [14, 12, 10]:                        # 계수를 소수 frac_bits 비트로 반올림
    q = lambda v: np.round(v * 2**frac_bits) / 2**frac_bits
    print(f"Q{frac_bits:<2d}   a1={q(a[1]):.6f} a2={q(a[2]):.6f}  poles={pole_info(q(a[1]), q(a[2]))}")
print("b0 =", b[0], "→ Q15 정수:", round(b[0] * 32768))
# limit cycle: 입력 0, y[n] = round(a·y[n-1]) 정수 연산 (Q15 계수)
for a_f in [0.9, -0.9]:
    aq = round(a_f * 32768); y = 100; hist = []
    for n in range(60):
        y = (aq * y + (1 << 14)) >> 15                # 곱 → 반올림 → 시프트
        hist.append(y)
    print(f"a={a_f:+.1f}: last 6 outputs {hist[-6:]}  (deadband 0.5/(1-|a|) = {0.5/(1-abs(a_f)):.1f})")
```

```text
float a1=-1.972234 a2=0.972614  poles=[0.98612+0.01369j 0.98612-0.01369j]
Q14   a1=-1.972229 a2=0.972595  poles=[0.98611+0.01317j 0.98611-0.01317j]
Q12   a1=-1.972168 a2=0.972656  poles=[0.98608+0.01716j 0.98608-0.01716j]
Q10   a1=-1.972656 a2=0.972656  poles=[1.      0.97266]
b0 = 9.506002944922903e-05 → Q15 정수: 3
a=+0.9: last 6 outputs [4, 4, 4, 4, 4, 4]  (deadband 0.5/(1-|a|) = 5.0)
a=-0.9: last 6 outputs [-4, 4, -4, 4, -4, 4]  (deadband 0.5/(1-|a|) = 5.0)
```

출력에서 볼 것(앞 4줄):

- float 극점 0.98612 ± 0.01369j. Q14로 반올림해도 거의 같다(허수부 0.01369 → 0.01317, 즉 공진 주파수가 약 4% 낮아짐).
- Q12면 허수부가 0.01716으로 **25% 커진다** — cutoff가 크게 바뀐 다른 필터다.
- Q10이면 a1 = −1.972656, a2 = 0.972656이 되어 1 + a1 + a2 = 0 → **극점 하나가 정확히 z = 1**(적분기)로 간다. DC 입력이 들어오면 출력이 끝없이 커진다(그림 3 b).
- b0 = 9.5e-5는 Q15 정수로 **3**이다. 3/32768 = 9.16e-5 → 이득 오차 약 −3.7%, 그리고 출력 해상도도 거의 없다. 낮은 cutoff 필터는 이득과 극점 둘 다 Q15에 맞지 않는다.

해결책(실무 순서):

1. **Q31 계수 + 64-bit 누산**: CMSIS의 `arm_biquad_cascade_df1_q31`, 더 정밀한 `arm_biquad_cas_df1_32x64_q31`(32-bit 계수 × 64-bit 상태)이 이 용도다.
2. **먼저 decimate 하고 저주파 필터**: 50 Hz 필터를 16 kHz에서 돌리지 말고, 1 kHz로 줄인 뒤 돌리면 같은 50 Hz가 fs의 5%라 극점이 z = 1에서 멀어진다. IMU 처리에서도 같은 원리다.
3. **float로**: Cortex-M4F/M33/M55, HiFi, Hexagon 모두 단정밀도 FPU가 있다. FPU가 있으면 IIR은 float DF2T가 가장 덜 위험하다.
4. 그래도 고정소수점이면 이득을 구간에 나눠 분배하고(각 구간 출력이 포화하지 않게), 극점이 z = 1에 가장 가까운 구간을 **마지막**에 두는 등 구간 순서·스케일링을 설계한다.

### 4.6 고정소수점 IIR 함정 2: limit cycle과 deadband

ex10의 마지막 두 줄을 보자. 입력이 0인데 y[n] = round(0.9·y[n−1])를 정수로 돌리면 100에서 시작해 줄어들다가 **4에서 멈춘다**. round(0.9 × 4) = round(3.6) = 4라서 더 이상 줄지 않는다. a = −0.9면 +4, −4를 영원히 오간다. 이것이 **zero-input limit cycle**이고, 반올림 때문에 "더 줄어들 수 없는" 구간을 **deadband**라 한다:

```
|y| ≤ 0.5 / (1 − |a|)  이면 round(a·y) 가 y 와 같은 크기로 남을 수 있다
a = 0.9 → 5 LSB 이내에서 멈춘다 (실제 4)
```

말로 하면: 반올림 오차(최대 0.5 LSB)가 감쇠량((1 − |a|)·y)보다 커지면 감쇠가 멈춘다. B3에서 RNN 상태가 양자화 후 특정 값에 들러붙는 현상을 deadband로 설명했던 것과 같은 메커니즘이다.

증상: 무음인데 오디오 출력에 작은 톤(극점 주파수)이 남거나, VAD 에너지가 0으로 내려가지 않는다. 대처: 상태 비트 수 늘리기(Q31 상태), 0 쪽으로 자르는 magnitude truncation(1차에서는 limit cycle 제거), 작은 dither 추가, 무음 구간에서 상태 리셋. 또 하나 — **overflow는 반드시 포화(saturation)로**. 2의 보수 wrap-around로 넘치면 큰 진폭의 overflow oscillation이 생긴다.

### 4.7 FIR vs IIR — MCU에서 무엇을 쓰나

| 항목 | FIR | IIR (biquad cascade) |
|---|---|---|
| 안정성 | 항상 안정 | 극점이 단위원 안이어야. 계수 양자화로 깨질 수 있음 |
| 위상 | 대칭이면 선형 위상(파형 보존) | 비선형 위상(주파수마다 지연 다름) |
| 같은 날카로움의 비용 | 수십~수백 tap | 구간당 곱 5번, 몇 구간 |
| 지연 | (N−1)/2 샘플 고정, tap이 많으면 큼 | 같은 사양이면 보통 FIR보다 작음(cutoff 근처에서는 커짐) |
| 고정소수점 | 쉬움(누산기 guard bit만 신경) | 어려움(계수 형식, limit cycle, 구간 스케일링) |
| 멀티레이트 | decimation·interpolation과 궁합 좋음(polyphase) | 버릴 출력도 상태 때문에 다 계산해야 함 |
| 대표 용도 | 리샘플링, PDM decimation(CIC 뒤), 선형 위상이 필요한 front-end | DC 제거 HPF, pre-emphasis 대체, IMU LPF, 이퀄라이저, 저전력 band 에너지 |

면접용 한 줄: "결정 기준은 위상·지연·연산량·수치 위험이다. 리샘플링과 선형 위상이 필요하면 FIR, 싸고 날카로운 필터가 필요하면 SOS로 쪼갠 IIR, 고정소수점이면 IIR은 DF1 + Q31 또는 float를 먼저 고려한다."

참고로 음성 front-end의 **pre-emphasis** y[n] = x[n] − 0.97·x[n−1] 은 2-tap FIR 고역 강조 필터다(영점 z = 0.97). 고전 MFCC 파이프라인에 들어가는데, torchaudio `MelSpectrogram`에는 없다 — 학습 때 했는지 확인해야 하는 또 하나의 스펙이다.

---

## 5. DFT와 FFT

### 5.1 직관: 신호를 "주파수 성분의 합"으로 다시 쓰기

DFT는 N개 샘플을 N개의 복소 사인파(주파수 0, fs/N, 2fs/N, …)로 분해한다. 각 성분과 신호의 **내적(dot product)** 을 구하는 것이라고 생각하면 쉽다 — "이 신호에 k번째 사인파가 얼마나 들어 있나?" A1의 "행렬 × 벡터 = 기저 바꾸기"가 정확히 이것이다.

```
X[k] = Σ_{n=0}^{N−1} x[n] · e^{−j·2π·k·n / N},   k = 0 … N−1
bin k 의 주파수 = k · fs / N,   간격 Δf = fs / N
```

말로 하면: X = W·x, W는 N×N 복소 행렬(W[k,n] = e^{−j2πkn/N}). 실수 입력이면 X[N−k] = X[k]의 켤레라 앞쪽 N/2 + 1개만 의미 있다(`rfft`).

### 5.2 손계산: 8점 DFT

fs = 8 kHz, N = 8 → Δf = 1 kHz. x[n] = cos(2π·1000·n/8000) + 0.5.

```
DC: X[0] = Σ x[n] = Σ cos(…) + 8·0.5 = 0 + 4 = 4
k=1: cos = (e^{jθ} + e^{−jθ})/2 → e^{jθ} 성분이 W[1,·]와 정확히 맞아 N/2 = 4
k=7: 켤레 성분 → 4.   나머지 bin은 직교성 때문에 0
```

이 코드는 DFT 행렬로 직접 계산해 `np.fft.fft`와 비교하고 bin 주파수를 출력한다.

```python
import numpy as np
N, fs = 8, 8000                                    # 8점, 8 kHz → Δf = 1 kHz
n = np.arange(N)
x = np.cos(2 * np.pi * 1000 * n / fs) + 0.5        # 1 kHz 코사인 + DC 0.5
k = n.reshape(-1, 1)
Wmat = np.exp(-2j * np.pi * k * n / N)             # DFT 행렬 W[k,n] = e^{-j2πkn/N}
X = Wmat @ x                                       # X[k] = Σ x[n]·e^{-j2πkn/N}
print("x      :", np.round(x, 3))
print("|X[k]| :", np.round(np.abs(X), 3))
print("bin Hz :", np.fft.fftfreq(N, 1 / fs))
print("matches np.fft.fft?", np.allclose(X, np.fft.fft(x)))
print("rfft len:", len(np.fft.rfft(x)), " bins:", np.fft.rfftfreq(N, 1 / fs))
```

```text
x      : [ 1.5    1.207  0.5   -0.207 -0.5   -0.207  0.5    1.207]
|X[k]| : [4. 4. 0. 0. 0. 0. 0. 4.]
bin Hz : [    0.  1000.  2000.  3000. -4000. -3000. -2000. -1000.]
matches np.fft.fft? True
rfft len: 5  bins: [   0. 1000. 2000. 3000. 4000.]
```

출력에서 볼 것: |X| = [4, 4, 0, 0, 0, 0, 0, 4] — 손계산 그대로. `fftfreq`는 뒤쪽 절반을 음수 주파수로 표시한다(k = 7 ↔ −1 kHz). `rfft`는 0 ~ 4 kHz의 5개 bin만 준다. 진폭 A인 코사인은 bin에서 A·N/2 크기로 보인다 — 진폭으로 바꾸려면 2/N을 곱한다(window를 쓰면 2/Σw).

### 5.3 Leakage — bin 사이에 있는 톤

DFT는 N개 샘플이 **무한히 반복된다고 가정**한다. 톤이 정확히 bin 위(N 샘플 안에 정수 주기)면 이음매가 매끈해서 한 bin에 모이지만, bin 사이면 이음매에 불연속이 생기고 에너지가 모든 bin으로 번진다. 이것이 **spectral leakage**다. window(양 끝을 0 쪽으로 줄이는 함수)를 곱하면 이음매가 부드러워져 번짐이 줄어든다.

이 코드는 512점(Δf = 31.25 Hz)에서 bin 정중앙 톤(1000 Hz)과 bin 사이 톤(1015.625 Hz)을 네 window로 분석하고, 2 bin 떨어진 두 톤을 구분하는지 본다.

```python
import numpy as np
from scipy.signal import get_window
fs, N = 16000, 512                                 # Δf = 31.25 Hz
n = np.arange(N)
for f0 in [1000.0, 1015.625]:                      # 32번 bin 정중앙 vs bin 사이(32.5)
    x = np.sin(2 * np.pi * f0 * n / fs)
    for name in ["boxcar", "hann", "hamming", "blackman"]:
        w = get_window(name, N)                    # periodic(DFT-even) 창
        X = np.abs(np.fft.rfft(x * w)) / w.sum() * 2   # 진폭으로 정규화
        far = 20 * np.log10(X[60:].max() + 1e-300) # 1.9 kHz 이상에서 가장 큰 누설
        print(f"f0={f0:8.3f}  {name:8s} peak={X.max():.3f}  leakage(>=bin60)={far:7.1f} dB")
# 메인로브 폭: 두 톤 1000 Hz와 1062.5 Hz (2 bin 차이)를 구분하나?
x2 = np.sin(2*np.pi*1000*n/fs) + np.sin(2*np.pi*1062.5*n/fs)
for name in ["boxcar", "hann", "blackman"]:
    X = np.abs(np.fft.rfft(x2 * get_window(name, N)))
    print(f"{name:8s} bins 31..35:", np.round(X[31:36] / X.max(), 2))
```

```text
f0=1000.000  boxcar   peak=1.000  leakage(>=bin60)= -296.1 dB
f0=1000.000  hann     peak=1.000  leakage(>=bin60)= -295.9 dB
f0=1000.000  hamming  peak=1.000  leakage(>=bin60)= -296.0 dB
f0=1000.000  blackman peak=1.000  leakage(>=bin60)= -295.7 dB
f0=1015.625  boxcar   peak=0.641  leakage(>=bin60)=  -41.5 dB
f0=1015.625  hann     peak=0.849  leakage(>=bin60)=  -96.5 dB
f0=1015.625  hamming  peak=0.818  leakage(>=bin60)=  -58.2 dB
f0=1015.625  blackman peak=0.881  leakage(>=bin60)= -103.9 dB
boxcar   bins 31..35: [0. 1. 0. 1. 0.]
hann     bins 31..35: [0.5 1.  1.  1.  0.5]
blackman bins 31..35: [0.5  0.92 1.   0.92 0.5 ]
```

출력에서 볼 것:

- bin 위의 톤은 어떤 window든 −296 dB(= 부동소수점 바닥, 사실상 0) — leakage가 없다. 현실 신호는 이런 행운이 없다.
- bin 사이 톤: rect는 멀리 떨어진 bin에도 −41 dB가 새고, Hann −97, Blackman −104 dB. 반면 Hamming은 −58 dB로 Hann보다 나쁘다 — 첫 사이드로브는 낮지만 멀리 갈수록 덜 줄어드는 창이다(그림 5).
- peak 높이: rect 0.641(−3.9 dB), Hann 0.849(−1.4 dB). 톤이 bin 사이에 있으면 peak가 낮아 보이는 것을 **scalloping loss**라 한다.
- 두 톤 2 bin 간격: rect는 [0, 1, 0, 1, 0]으로 깔끔히 구분, Hann은 [0.5, 1, 1, 1, 0.5]로 경계선, Blackman은 33번 bin 하나로 **합쳐 버린다**. window는 leakage를 줄이는 대가로 **main lobe를 넓혀 해상도를 잃는다**.

```svg
<svg viewBox="0 0 660 350" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="250" x2="640" y2="250" stroke="currentColor"/> <line x1="60" y1="30" x2="60" y2="250" stroke="currentColor"/> <line x1="60" y1="30.0" x2="640" y2="30.0" stroke="#888" stroke-width="0.4" stroke-dasharray="3 3"/> <text x="54" y="34.0" font-size="12" text-anchor="end">0</text> <line x1="60" y1="66.7" x2="640" y2="66.7" stroke="#888" stroke-width="0.4" stroke-dasharray="3 3"/> <text x="54" y="70.7" font-size="12" text-anchor="end">-20</text> <line x1="60" y1="103.3" x2="640" y2="103.3" stroke="#888" stroke-width="0.4" stroke-dasharray="3 3"/> <text x="54" y="107.3" font-size="12" text-anchor="end">-40</text> <line x1="60" y1="140.0" x2="640" y2="140.0" stroke="#888" stroke-width="0.4" stroke-dasharray="3 3"/> <text x="54" y="144.0" font-size="12" text-anchor="end">-60</text> <line x1="60" y1="176.7" x2="640" y2="176.7" stroke="#888" stroke-width="0.4" stroke-dasharray="3 3"/> <text x="54" y="180.7" font-size="12" text-anchor="end">-80</text> <line x1="60" y1="213.3" x2="640" y2="213.3" stroke="#888" stroke-width="0.4" stroke-dasharray="3 3"/> <text x="54" y="217.3" font-size="12" text-anchor="end">-100</text> <line x1="60" y1="250.0" x2="640" y2="250.0" stroke="#888" stroke-width="0.4" stroke-dasharray="3 3"/> <text x="54" y="254.0" font-size="12" text-anchor="end">-120</text> <text x="60.0" y="266" font-size="12" text-anchor="middle">0</text> <text x="132.5" y="266" font-size="12" text-anchor="middle">2</text> <text x="205.0" y="266" font-size="12" text-anchor="middle">4</text> <text x="277.5" y="266" font-size="12" text-anchor="middle">6</text> <text x="350.0" y="266" font-size="12" text-anchor="middle">8</text> <text x="422.5" y="266" font-size="12" text-anchor="middle">10</text> <text x="495.0" y="266" font-size="12" text-anchor="middle">12</text> <text x="567.5" y="266" font-size="12" text-anchor="middle">14</text> <text x="640.0" y="266" font-size="12" text-anchor="middle">16</text>
<polyline fill="none" stroke="#888" stroke-width="1.4" points="60.0,30.0 61.1,30.0 62.3,30.1 63.4,30.2 64.5,30.4 65.7,30.6 66.8,30.9 67.9,31.3 69.1,31.7 70.2,32.1 71.3,32.6 72.5,33.2 73.6,33.9 74.7,34.6 75.9,35.4 77.0,36.2 78.1,37.2 79.3,38.2 80.4,39.4 81.5,40.6 82.7,42.0 83.8,43.5 84.9,45.2 86.1,47.1 87.2,49.2 88.3,51.5 89.5,54.3 90.6,57.5 91.7,61.4 92.9,66.4 94.0,73.2 95.1,84.7 96.2,250.0 97.4,85.7 98.5,75.2 99.6,69.3 100.8,65.4 101.9,62.5 103.0,60.3 104.2,58.6 105.3,57.3 106.4,56.3 107.6,55.5 108.7,54.9 109.8,54.5 111.0,54.3 112.1,54.3 113.2,54.4 114.4,54.7 115.5,55.1 116.6,55.6 117.8,56.3 118.9,57.2 120.0,58.2 121.2,59.5 122.3,60.9 123.4,62.6 124.6,64.6 125.7,67.0 126.8,69.9 128.0,73.5 129.1,78.2 130.2,84.8 131.4,96.0 132.5,250.0 133.6,96.5 134.8,85.8 135.9,79.7 137.0,75.5 138.2,72.4 139.3,70.0 140.4,68.1 141.6,66.6 142.7,65.4 143.8,64.5 145.0,63.8 146.1,63.2 147.2,62.9 148.4,62.7 149.5,62.7 150.6,62.8 151.8,63.1 152.9,63.5 154.0,64.1 155.2,64.8 156.3,65.7 157.4,66.9 158.6,68.2 159.7,69.8 160.8,71.7 162.0,74.0 163.1,76.8 164.2,80.3 165.4,84.9 166.5,91.4 167.6,102.5 168.8,250.0 169.9,102.8 171.0,92.0 172.1,85.8 173.3,81.6 174.4,78.4 175.5,76.0 176.7,74.0 177.8,72.4 178.9,71.2 180.1,70.2 181.2,69.4 182.3,68.8 183.5,68.4 184.6,68.1 185.7,68.0 186.9,68.1 188.0,68.3 189.1,68.7 190.3,69.2 191.4,69.9 192.5,70.8 193.7,71.9 194.8,73.2 195.9,74.7 197.1,76.6 198.2,78.8 199.3,81.6 200.5,85.0 201.6,89.5 202.7,96.0 203.9,107.1 205.0,250.0 206.1,107.3 207.3,96.5 208.4,90.3 209.5,86.0 210.7,82.8 211.8,80.3 212.9,78.3 214.1,76.7 215.2,75.4 216.3,74.3 217.5,73.5 218.6,72.9 219.7,72.4 220.9,72.1 222.0,72.0 223.1,72.1 224.3,72.2 225.4,72.6 226.5,73.1 227.7,73.7 228.8,74.6 229.9,75.6 231.1,76.9 232.2,78.4 233.3,80.2 234.5,82.5 235.6,85.2 236.7,88.6 237.9,93.1 239.0,99.5 240.1,110.6 241.2,250.0 242.4,110.8 243.5,99.9 244.6,93.7 245.8,89.4 246.9,86.2 248.0,83.6 249.2,81.6 250.3,80.0 251.4,78.7 252.6,77.6 253.7,76.7 254.8,76.1 256.0,75.6 257.1,75.3 258.2,75.2 259.4,75.2 260.5,75.3 261.6,75.7 262.8,76.1 263.9,76.8 265.0,77.6 266.2,78.6 267.3,79.9 268.4,81.4 269.6,83.2 270.7,85.4 271.8,88.1 273.0,91.5 274.1,96.0 275.2,102.4 276.4,113.4 277.5,250.0 278.6,113.6 279.8,102.7 280.9,96.5 282.0,92.1 283.2,88.9 284.3,86.4 285.4,84.3 286.6,82.7 287.7,81.3 288.8,80.3 290.0,79.4 291.1,78.7 292.2,78.2 293.4,77.9 294.5,77.8 295.6,77.8 296.8,77.9 297.9,78.2 299.0,78.7 300.2,79.3 301.3,80.1 302.4,81.1 303.6,82.4 304.7,83.9 305.8,85.7 307.0,87.8 308.1,90.5 309.2,93.9 310.4,98.4 311.5,104.8 312.6,115.8 313.8,250.0 314.9,116.0 316.0,105.1 317.1,98.8 318.3,94.5 319.4,91.2 320.5,88.7 321.7,86.6 322.8,85.0 323.9,83.6 325.1,82.5 326.2,81.6 327.3,81.0 328.5,80.5 329.6,80.1 330.7,80.0 331.9,80.0 333.0,80.1 334.1,80.4 335.3,80.8 336.4,81.5 337.5,82.3 338.7,83.3 339.8,84.5 340.9,86.0 342.1,87.8 343.2,89.9 344.3,92.6 345.5,96.0 346.6,100.4 347.7,106.8 348.9,117.9 350.0,250.0 351.1,118.0 352.3,107.1 353.4,100.8 354.5,96.5 355.7,93.2 356.8,90.6 357.9,88.6 359.1,86.9 360.2,85.6 361.3,84.4 362.5,83.6 363.6,82.9 364.7,82.4 365.9,82.0 367.0,81.9 368.1,81.8 369.3,82.0 370.4,82.3 371.5,82.7 372.7,83.3 373.8,84.1 374.9,85.1 376.1,86.3 377.2,87.8 378.3,89.6 379.5,91.7 380.6,94.4 381.7,97.8 382.9,102.2 384.0,108.6 385.1,119.6 386.2,250.0 387.4,119.7 388.5,108.8 389.6,102.5 390.8,98.2 391.9,94.9 393.0,92.4 394.2,90.3 395.3,88.6 396.4,87.3 397.6,86.1 398.7,85.3 399.8,84.6 401.0,84.1 402.1,83.7 403.2,83.5 404.4,83.5 405.5,83.6 406.6,83.9 407.8,84.3 408.9,85.0 410.0,85.7 411.2,86.7 412.3,87.9 413.4,89.4 414.6,91.2 415.7,93.3 416.8,96.0 418.0,99.4 419.1,103.8 420.2,110.2 421.4,121.2 422.5,250.0 423.6,121.3 424.8,110.4 425.9,104.1 427.0,99.7 428.2,96.5 429.3,93.9 430.4,91.8 431.6,90.1 432.7,88.8 433.8,87.6 435.0,86.7 436.1,86.0 437.2,85.5 438.4,85.2 439.5,85.0 440.6,85.0 441.8,85.1 442.9,85.4 444.0,85.8 445.2,86.4 446.3,87.2 447.4,88.2 448.6,89.4 449.7,90.8 450.8,92.6 452.0,94.7 453.1,97.4 454.2,100.8 455.4,105.2 456.5,111.6 457.6,122.6 458.8,250.0 459.9,122.7 461.0,111.7 462.1,105.4 463.3,101.1 464.4,97.8 465.5,95.2 466.7,93.2 467.8,91.5 468.9,90.1 470.1,89.0 471.2,88.1 472.3,87.4 473.5,86.9 474.6,86.5 475.7,86.3 476.9,86.3 478.0,86.4 479.1,86.7 480.3,87.1 481.4,87.7 482.5,88.5 483.7,89.4 484.8,90.6 485.9,92.1 487.1,93.9 488.2,96.0 489.3,98.7 490.5,102.0 491.6,106.5 492.7,112.8 493.9,123.8 495.0,250.0 496.1,123.9 497.3,113.0 498.4,106.7 499.5,102.3 500.7,99.0 501.8,96.4 502.9,94.4 504.1,92.7 505.2,91.3 506.3,90.2 507.5,89.3 508.6,88.6 509.7,88.0 510.9,87.7 512.0,87.5 513.1,87.4 514.3,87.5 515.4,87.8 516.5,88.2 517.7,88.8 518.8,89.6 519.9,90.6 521.1,91.8 522.2,93.2 523.3,95.0 524.5,97.1 525.6,99.8 526.7,103.1 527.9,107.6 529.0,113.9 530.1,124.9 531.2,250.0 532.4,125.0 533.5,114.1 534.6,107.8 535.8,103.4 536.9,100.1 538.0,97.5 539.2,95.5 540.3,93.8 541.4,92.4 542.6,91.2 543.7,90.3 544.8,89.6 546.0,89.1 547.1,88.7 548.2,88.5 549.4,88.5 550.5,88.6 551.6,88.9 552.8,89.3 553.9,89.9 555.0,90.6 556.2,91.6 557.3,92.8 558.4,94.3 559.6,96.0 560.7,98.2 561.8,100.8 563.0,104.2 564.1,108.6 565.2,114.9 566.4,125.9 567.5,250.0 568.6,126.0 569.8,115.1 570.9,108.8 572.0,104.4 573.2,101.1 574.3,98.5 575.4,96.4 576.6,94.7 577.7,93.3 578.8,92.2 580.0,91.3 581.1,90.6 582.2,90.1 583.4,89.7 584.5,89.5 585.6,89.4 586.8,89.5 587.9,89.8 589.0,90.2 590.2,90.8 591.3,91.6 592.4,92.6 593.6,93.7 594.7,95.2 595.8,96.9 597.0,99.1 598.1,101.7 599.2,105.1 600.4,109.5 601.5,115.9 602.6,126.8 603.8,250.0 604.9,126.9 606.0,116.0 607.1,109.7 608.3,105.3 609.4,102.0 610.5,99.4 611.7,97.3 612.8,95.6 613.9,94.2 615.1,93.1 616.2,92.2 617.3,91.5 618.5,90.9 619.6,90.6 620.7,90.4 621.9,90.3 623.0,90.4 624.1,90.7 625.3,91.1 626.4,91.7 627.5,92.4 628.7,93.4 629.8,94.6 630.9,96.0 632.1,97.8 633.2,99.9 634.3,102.6 635.5,105.9 636.6,110.3 637.7,116.7 638.9,127.7 640.0,250.0"/>
<line x1="70" y1="296" x2="95" y2="296" stroke="#888" stroke-width="2"/> <text x="100" y="300" font-size="12">rect: 첫 null 1.0 bin, 사이드로브 -13 dB</text>
<polyline fill="none" stroke="#3f9a6b" stroke-width="1.4" points="60.0,30.0 61.1,30.0 62.3,30.0 63.4,30.1 64.5,30.2 65.7,30.3 66.8,30.4 67.9,30.6 69.1,30.8 70.2,31.0 71.3,31.2 72.5,31.5 73.6,31.8 74.7,32.1 75.9,32.4 77.0,32.8 78.1,33.2 79.3,33.6 80.4,34.1 81.5,34.6 82.7,35.1 83.8,35.6 84.9,36.2 86.1,36.8 87.2,37.4 88.3,38.0 89.5,38.7 90.6,39.5 91.7,40.2 92.9,41.0 94.0,41.8 95.1,42.7 96.2,43.6 97.4,44.5 98.5,45.5 99.6,46.5 100.8,47.6 101.9,48.7 103.0,49.9 104.2,51.1 105.3,52.3 106.4,53.6 107.6,55.0 108.7,56.4 109.8,57.9 111.0,59.5 112.1,61.2 113.2,62.9 114.4,64.7 115.5,66.6 116.6,68.6 117.8,70.7 118.9,73.0 120.0,75.4 121.2,78.0 122.3,80.8 123.4,83.8 124.6,87.1 125.7,90.8 126.8,95.0 128.0,99.9 129.1,105.8 130.2,113.7 131.4,126.3 132.5,250.0 133.6,129.5 134.8,120.2 135.9,115.5 137.0,112.9 138.2,111.3 139.3,110.5 140.4,110.2 141.6,110.4 142.7,110.9 143.8,111.8 145.0,113.0 146.1,114.5 147.2,116.3 148.4,118.2 149.5,120.4 150.6,122.6 151.8,124.6 152.9,126.2 154.0,127.3 155.2,127.7 156.3,127.7 157.4,127.6 158.6,127.5 159.7,127.6 160.8,128.2 162.0,129.1 163.1,130.7 164.2,133.1 165.4,136.6 166.5,142.1 167.6,152.4 168.8,250.0 169.9,151.1 171.0,139.6 172.1,132.8 173.3,127.9 174.4,124.2 175.5,121.2 176.7,118.7 177.8,116.7 178.9,115.0 180.1,113.5 181.2,112.3 182.3,111.4 183.5,110.6 184.6,110.0 185.7,109.6 186.9,109.3 188.0,109.3 189.1,109.4 190.3,109.6 191.4,110.1 192.5,110.7 193.7,111.5 194.8,112.6 195.9,113.9 197.1,115.6 198.2,117.6 199.3,120.2 200.5,123.4 201.6,127.8 202.7,134.0 203.9,145.0 205.0,250.0 206.1,144.9 207.3,133.9 208.4,127.6 209.5,123.1 210.7,119.8 211.8,117.2 212.9,115.0 214.1,113.3 215.2,111.9 216.3,110.7 217.5,109.8 218.6,109.0 219.7,108.5 220.9,108.1 222.0,107.9 223.1,107.8 224.3,107.9 225.4,108.2 226.5,108.6 227.7,109.2 228.8,109.9 229.9,110.9 231.1,112.1 232.2,113.5 233.3,115.3 234.5,117.4 235.6,120.0 236.7,123.4 237.9,127.8 239.0,134.2 240.1,145.2 241.2,250.0 242.4,145.2 243.5,134.3 244.6,128.0 245.8,123.7 246.9,120.4 248.0,117.8 249.2,115.7 250.3,114.0 251.4,112.7 252.6,111.5 253.7,110.6 254.8,109.9 256.0,109.4 257.1,109.1 258.2,108.9 259.4,108.9 260.5,109.0 261.6,109.3 262.8,109.7 263.9,110.3 265.0,111.1 266.2,112.1 267.3,113.3 268.4,114.7 269.6,116.5 270.7,118.7 271.8,121.3 273.0,124.7 274.1,129.1 275.2,135.5 276.4,146.5 277.5,250.0 278.6,146.6 279.8,135.7 280.9,129.4 282.0,125.1 283.2,121.8 284.3,119.2 285.4,117.2 286.6,115.5 287.7,114.1 288.8,113.0 290.0,112.1 291.1,111.4 292.2,110.9 293.4,110.6 294.5,110.4 295.6,110.4 296.8,110.5 297.9,110.8 299.0,111.2 300.2,111.8 301.3,112.6 302.4,113.6 303.6,114.8 304.7,116.3 305.8,118.1 307.0,120.2 308.1,122.9 309.2,126.3 310.4,130.7 311.5,137.1 312.6,148.1 313.8,250.0 314.9,148.2 316.0,137.3 317.1,131.0 318.3,126.6 319.4,123.4 320.5,120.8 321.7,118.7 322.8,117.1 323.9,115.7 325.1,114.6 326.2,113.7 327.3,113.0 328.5,112.5 329.6,112.1 330.7,112.0 331.9,111.9 333.0,112.1 334.1,112.3 335.3,112.8 336.4,113.4 337.5,114.2 338.7,115.2 339.8,116.4 340.9,117.8 342.1,119.6 343.2,121.8 344.3,124.4 345.5,127.8 346.6,132.2 347.7,138.6 348.9,149.6 350.0,250.0 351.1,149.7 352.3,138.8 353.4,132.5 354.5,128.2 355.7,124.9 356.8,122.3 357.9,120.3 359.1,118.6 360.2,117.2 361.3,116.1 362.5,115.2 363.6,114.5 364.7,114.0 365.9,113.6 367.0,113.5 368.1,113.4 369.3,113.5 370.4,113.8 371.5,114.3 372.7,114.9 373.8,115.6 374.9,116.6 376.1,117.8 377.2,119.3 378.3,121.1 379.5,123.2 380.6,125.9 381.7,129.2 382.9,133.7 384.0,140.1 385.1,151.1 386.2,250.0 387.4,151.2 388.5,140.2 389.6,134.0 390.8,129.6 391.9,126.3 393.0,123.7 394.2,121.7 395.3,120.0 396.4,118.6 397.6,117.5 398.7,116.6 399.8,115.9 401.0,115.4 402.1,115.0 403.2,114.8 404.4,114.8 405.5,114.9 406.6,115.2 407.8,115.6 408.9,116.2 410.0,117.0 411.2,118.0 412.3,119.2 413.4,120.7 414.6,122.4 415.7,124.6 416.8,127.2 418.0,130.6 419.1,135.0 420.2,141.4 421.4,152.4 422.5,250.0 423.6,152.5 424.8,141.6 425.9,135.3 427.0,130.9 428.2,127.6 429.3,125.0 430.4,123.0 431.6,121.3 432.7,119.9 433.8,118.8 435.0,117.9 436.1,117.2 437.2,116.7 438.4,116.3 439.5,116.1 440.6,116.1 441.8,116.2 442.9,116.5 444.0,116.9 445.2,117.5 446.3,118.3 447.4,119.2 448.6,120.4 449.7,121.9 450.8,123.7 452.0,125.8 453.1,128.5 454.2,131.8 455.4,136.3 456.5,142.6 457.6,153.6 458.8,250.0 459.9,153.7 461.0,142.8 462.1,136.5 463.3,132.1 464.4,128.8 465.5,126.2 466.7,124.2 467.8,122.5 468.9,121.1 470.1,120.0 471.2,119.1 472.3,118.4 473.5,117.8 474.6,117.5 475.7,117.3 476.9,117.2 478.0,117.3 479.1,117.6 480.3,118.0 481.4,118.6 482.5,119.4 483.7,120.4 484.8,121.6 485.9,123.0 487.1,124.8 488.2,126.9 489.3,129.6 490.5,132.9 491.6,137.4 492.7,143.7 493.9,154.7 495.0,250.0 496.1,154.8 497.3,143.9 498.4,137.6 499.5,133.2 500.7,129.9 501.8,127.3 502.9,125.3 504.1,123.6 505.2,122.2 506.3,121.0 507.5,120.1 508.6,119.4 509.7,118.9 510.9,118.5 512.0,118.3 513.1,118.3 514.3,118.4 515.4,118.7 516.5,119.1 517.7,119.7 518.8,120.5 519.9,121.4 521.1,122.6 522.2,124.1 523.3,125.8 524.5,128.0 525.6,130.6 526.7,134.0 527.9,138.4 529.0,144.8 530.1,155.7 531.2,250.0 532.4,155.8 533.5,144.9 534.6,138.6 535.8,134.2 536.9,130.9 538.0,128.3 539.2,126.2 540.3,124.5 541.4,123.2 542.6,122.0 543.7,121.1 544.8,120.4 546.0,119.9 547.1,119.5 548.2,119.3 549.4,119.3 550.5,119.4 551.6,119.6 552.8,120.1 553.9,120.6 555.0,121.4 556.2,122.4 557.3,123.6 558.4,125.0 559.6,126.8 560.7,128.9 561.8,131.6 563.0,134.9 564.1,139.3 565.2,145.7 566.4,156.7 567.5,250.0 568.6,156.7 569.8,145.8 570.9,139.5 572.0,135.1 573.2,131.8 574.3,129.2 575.4,127.2 576.6,125.5 577.7,124.1 578.8,122.9 580.0,122.0 581.1,121.3 582.2,120.8 583.4,120.4 584.5,120.2 585.6,120.2 586.8,120.3 587.9,120.5 589.0,120.9 590.2,121.5 591.3,122.3 592.4,123.2 593.6,124.4 594.7,125.9 595.8,127.6 597.0,129.8 598.1,132.4 599.2,135.8 600.4,140.2 601.5,146.5 602.6,157.5 603.8,250.0 604.9,157.6 606.0,146.6 607.1,140.3 608.3,136.0 609.4,132.7 610.5,130.1 611.7,128.0 612.8,126.3 613.9,124.9 615.1,123.8 616.2,122.8 617.3,122.1 618.5,121.6 619.6,121.2 620.7,121.0 621.9,121.0 623.0,121.1 624.1,121.3 625.3,121.7 626.4,122.3 627.5,123.1 628.7,124.0 629.8,125.2 630.9,126.7 632.1,128.4 633.2,130.6 634.3,133.2 635.5,136.5 636.6,141.0 637.7,147.3 638.9,158.3 640.0,250.0"/>
<line x1="370" y1="296" x2="395" y2="296" stroke="#3f9a6b" stroke-width="2"/> <text x="400" y="300" font-size="12">Hamming: 첫 null 2.0 bin, 사이드로브 -42 dB</text>
<polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="60.0,30.0 61.1,30.0 62.3,30.0 63.4,30.1 64.5,30.2 65.7,30.3 66.8,30.4 67.9,30.5 69.1,30.6 70.2,30.8 71.3,31.0 72.5,31.2 73.6,31.5 74.7,31.7 75.9,32.0 77.0,32.3 78.1,32.6 79.3,33.0 80.4,33.3 81.5,33.7 82.7,34.1 83.8,34.6 84.9,35.0 86.1,35.5 87.2,36.0 88.3,36.5 89.5,37.1 90.6,37.7 91.7,38.3 92.9,38.9 94.0,39.6 95.1,40.3 96.2,41.0 97.4,41.8 98.5,42.6 99.6,43.4 100.8,44.3 101.9,45.2 103.0,46.1 104.2,47.1 105.3,48.1 106.4,49.2 107.6,50.3 108.7,51.5 109.8,52.7 111.0,54.0 112.1,55.3 113.2,56.8 114.4,58.2 115.5,59.8 116.6,61.5 117.8,63.2 118.9,65.1 120.0,67.1 121.2,69.3 122.3,71.6 123.4,74.2 124.6,77.0 125.7,80.2 126.8,83.9 128.0,88.2 129.1,93.6 130.2,100.9 131.4,112.8 132.5,250.0 133.6,114.6 134.8,104.6 135.9,99.1 137.0,95.5 138.2,93.1 139.3,91.2 140.4,89.9 141.6,89.0 142.7,88.3 143.8,87.9 145.0,87.7 146.1,87.7 147.2,87.9 148.4,88.2 149.5,88.6 150.6,89.2 151.8,90.0 152.9,90.9 154.0,91.9 155.2,93.1 156.3,94.5 157.4,96.0 158.6,97.8 159.7,99.8 160.8,102.1 162.0,104.8 163.1,108.0 164.2,111.9 165.4,116.9 166.5,123.8 167.6,135.3 168.8,250.0 169.9,136.4 171.0,125.9 172.1,120.1 173.3,116.2 174.4,113.4 175.5,111.3 176.7,109.7 177.8,108.5 178.9,107.5 180.1,106.9 181.2,106.4 182.3,106.1 183.5,106.0 184.6,106.1 185.7,106.3 186.9,106.7 188.0,107.2 189.1,107.9 190.3,108.8 191.4,109.8 192.5,110.9 193.7,112.3 194.8,113.9 195.9,115.7 197.1,117.9 198.2,120.4 199.3,123.4 200.5,127.1 201.6,131.9 202.7,138.7 203.9,150.0 205.0,250.0 206.1,150.8 207.3,140.2 208.4,134.3 209.5,130.3 210.7,127.3 211.8,125.1 212.9,123.3 214.1,122.0 215.2,120.9 216.3,120.1 217.5,119.5 218.6,119.1 219.7,118.9 220.9,118.9 222.0,119.0 223.1,119.3 224.3,119.7 225.4,120.3 226.5,121.0 227.7,121.9 228.8,123.0 229.9,124.2 231.1,125.7 232.2,127.5 233.3,129.5 234.5,131.9 235.6,134.9 236.7,138.5 237.9,143.2 239.0,149.9 240.1,161.1 241.2,250.0 242.4,161.8 243.5,151.1 244.6,145.1 245.8,141.0 246.9,138.0 248.0,135.6 249.2,133.8 250.3,132.4 251.4,131.3 252.6,130.4 253.7,129.7 254.8,129.3 256.0,129.0 257.1,128.9 258.2,128.9 259.4,129.1 260.5,129.5 261.6,130.0 262.8,130.7 263.9,131.5 265.0,132.5 266.2,133.7 267.3,135.1 268.4,136.8 269.6,138.8 270.7,141.2 271.8,144.1 273.0,147.7 274.1,152.3 275.2,158.9 276.4,170.1 277.5,250.0 278.6,170.6 279.8,159.9 280.9,153.8 282.0,149.7 283.2,146.6 284.3,144.2 285.4,142.4 286.6,140.9 287.7,139.7 288.8,138.8 290.0,138.1 291.1,137.6 292.2,137.3 293.4,137.1 294.5,137.1 295.6,137.3 296.8,137.6 297.9,138.1 299.0,138.7 300.2,139.5 301.3,140.4 302.4,141.6 303.6,143.0 304.7,144.6 305.8,146.6 307.0,148.9 308.1,151.8 309.2,155.3 310.4,159.9 311.5,166.5 312.6,177.6 313.8,250.0 314.9,178.1 316.0,167.3 317.1,161.2 318.3,157.0 319.4,153.9 320.5,151.5 321.7,149.6 322.8,148.1 323.9,146.9 325.1,145.9 326.2,145.2 327.3,144.7 328.5,144.3 329.6,144.1 330.7,144.1 331.9,144.2 333.0,144.5 334.1,144.9 335.3,145.5 336.4,146.3 337.5,147.2 338.7,148.4 339.8,149.7 340.9,151.3 342.1,153.2 343.2,155.6 344.3,158.4 345.5,161.9 346.6,166.5 347.7,173.0 348.9,184.1 350.0,250.0 351.1,184.5 352.3,173.7 353.4,167.6 354.5,163.4 355.7,160.3 356.8,157.8 357.9,155.9 359.1,154.4 360.2,153.1 361.3,152.1 362.5,151.4 363.6,150.8 364.7,150.4 365.9,150.2 367.0,150.2 368.1,150.3 369.3,150.5 370.4,150.9 371.5,151.5 372.7,152.2 373.8,153.2 374.9,154.3 376.1,155.6 377.2,157.2 378.3,159.1 379.5,161.4 380.6,164.2 381.7,167.7 382.9,172.2 384.0,178.7 385.1,189.9 386.2,250.0 387.4,190.2 388.5,179.4 389.6,173.2 390.8,169.0 391.9,165.8 393.0,163.4 394.2,161.4 395.3,159.9 396.4,158.6 397.6,157.6 398.7,156.9 399.8,156.3 401.0,155.9 402.1,155.6 403.2,155.6 404.4,155.7 405.5,155.9 406.6,156.3 407.8,156.8 408.9,157.5 410.0,158.4 411.2,159.5 412.3,160.9 413.4,162.4 414.6,164.3 415.7,166.6 416.8,169.3 418.0,172.8 419.1,177.4 420.2,183.9 421.4,195.0 422.5,250.0 423.6,195.3 424.8,184.5 425.9,178.3 427.0,174.0 428.2,170.9 429.3,168.4 430.4,166.4 431.6,164.8 432.7,163.6 433.8,162.6 435.0,161.8 436.1,161.2 437.2,160.8 438.4,160.5 439.5,160.4 440.6,160.5 441.8,160.7 442.9,161.1 444.0,161.6 445.2,162.3 446.3,163.2 447.4,164.3 448.6,165.6 449.7,167.2 450.8,169.0 452.0,171.3 453.1,174.0 454.2,177.5 455.4,182.0 456.5,188.5 457.6,199.6 458.8,250.0 459.9,199.9 461.0,189.0 462.1,182.9 463.3,178.6 464.4,175.4 465.5,172.9 466.7,170.9 467.8,169.4 468.9,168.1 470.1,167.0 471.2,166.2 472.3,165.6 473.5,165.2 474.6,164.9 475.7,164.8 476.9,164.9 478.0,165.1 479.1,165.5 480.3,166.0 481.4,166.7 482.5,167.6 483.7,168.6 484.8,169.9 485.9,171.5 487.1,173.3 488.2,175.6 489.3,178.3 490.5,181.8 491.6,186.3 492.7,192.7 493.9,203.8 495.0,250.0 496.1,204.1 497.3,193.2 498.4,187.0 499.5,182.8 500.7,179.6 501.8,177.1 502.9,175.1 504.1,173.5 505.2,172.2 506.3,171.2 507.5,170.3 508.6,169.7 509.7,169.3 510.9,169.0 512.0,168.9 513.1,169.0 514.3,169.2 515.4,169.5 516.5,170.0 517.7,170.7 518.8,171.6 519.9,172.6 521.1,173.9 522.2,175.4 523.3,177.3 524.5,179.5 525.6,182.3 526.7,185.7 527.9,190.2 529.0,196.7 530.1,207.7 531.2,250.0 532.4,208.0 533.5,197.1 534.6,190.9 535.8,186.6 536.9,183.4 538.0,180.9 539.2,178.9 540.3,177.3 541.4,176.0 542.6,175.0 543.7,174.1 544.8,173.5 546.0,173.1 547.1,172.8 548.2,172.7 549.4,172.7 550.5,172.9 551.6,173.2 552.8,173.8 553.9,174.4 555.0,175.3 556.2,176.3 557.3,177.6 558.4,179.1 559.6,181.0 560.7,183.2 561.8,185.9 563.0,189.4 564.1,193.9 565.2,200.3 566.4,211.4 567.5,250.0 568.6,211.6 569.8,200.7 570.9,194.5 572.0,190.2 573.2,187.0 574.3,184.5 575.4,182.5 576.6,180.9 577.7,179.6 578.8,178.5 580.0,177.7 581.1,177.1 582.2,176.6 583.4,176.3 584.5,176.2 585.6,176.2 586.8,176.4 587.9,176.7 589.0,177.2 590.2,177.9 591.3,178.8 592.4,179.8 593.6,181.1 594.7,182.6 595.8,184.4 597.0,186.6 598.1,189.4 599.2,192.8 600.4,197.3 601.5,203.7 602.6,214.8 603.8,250.0 604.9,215.0 606.0,204.1 607.1,197.9 608.3,193.6 609.4,190.4 610.5,187.9 611.7,185.9 612.8,184.2 613.9,182.9 615.1,181.9 616.2,181.0 617.3,180.4 618.5,179.9 619.6,179.6 620.7,179.5 621.9,179.5 623.0,179.7 624.1,180.0 625.3,180.5 626.4,181.2 627.5,182.0 628.7,183.1 629.8,184.3 630.9,185.9 632.1,187.7 633.2,189.9 634.3,192.6 635.5,196.0 636.6,200.5 637.7,206.9 638.9,218.0 640.0,250.0"/>
<line x1="70" y1="314" x2="95" y2="314" stroke="#4a7bd0" stroke-width="2"/> <text x="100" y="318" font-size="12">Hann: 첫 null 2.0 bin, 사이드로브 -31 dB</text>
<polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="60.0,30.0 61.1,30.0 62.3,30.0 63.4,30.1 64.5,30.1 65.7,30.2 66.8,30.3 67.9,30.4 69.1,30.5 70.2,30.6 71.3,30.8 72.5,30.9 73.6,31.1 74.7,31.3 75.9,31.5 77.0,31.8 78.1,32.0 79.3,32.3 80.4,32.6 81.5,32.9 82.7,33.2 83.8,33.5 84.9,33.8 86.1,34.2 87.2,34.6 88.3,35.0 89.5,35.4 90.6,35.8 91.7,36.3 92.9,36.7 94.0,37.2 95.1,37.7 96.2,38.3 97.4,38.8 98.5,39.4 99.6,40.0 100.8,40.6 101.9,41.2 103.0,41.8 104.2,42.5 105.3,43.2 106.4,43.9 107.6,44.6 108.7,45.4 109.8,46.1 111.0,46.9 112.1,47.8 113.2,48.6 114.4,49.5 115.5,50.4 116.6,51.3 117.8,52.2 118.9,53.2 120.0,54.2 121.2,55.3 122.3,56.3 123.4,57.4 124.6,58.5 125.7,59.7 126.8,60.9 128.0,62.1 129.1,63.4 130.2,64.7 131.4,66.1 132.5,67.4 133.6,68.9 134.8,70.4 135.9,71.9 137.0,73.5 138.2,75.1 139.3,76.8 140.4,78.5 141.6,80.3 142.7,82.2 143.8,84.1 145.0,86.2 146.1,88.3 147.2,90.5 148.4,92.8 149.5,95.2 150.6,97.7 151.8,100.4 152.9,103.2 154.0,106.1 155.2,109.3 156.3,112.6 157.4,116.3 158.6,120.2 159.7,124.4 160.8,129.2 162.0,134.4 163.1,140.4 164.2,147.5 165.4,156.0 166.5,167.2 167.6,184.2 168.8,250.0 169.9,206.8 171.0,215.4 172.1,183.8 173.3,171.0 174.4,162.8 175.5,156.8 176.7,152.2 177.8,148.6 178.9,145.8 180.1,143.4 181.2,141.5 182.3,140.0 183.5,138.8 184.6,137.9 185.7,137.2 186.9,136.8 188.0,136.6 189.1,136.6 190.3,136.8 191.4,137.2 192.5,137.8 193.7,138.7 194.8,139.8 195.9,141.2 197.1,143.0 198.2,145.1 199.3,147.8 200.5,151.2 201.6,155.7 202.7,162.1 203.9,173.1 205.0,250.0 206.1,173.4 207.3,162.6 208.4,156.4 209.5,152.1 210.7,149.0 211.8,146.5 212.9,144.6 214.1,143.1 215.2,141.8 216.3,140.8 217.5,140.1 218.6,139.6 219.7,139.2 220.9,139.0 222.0,139.0 223.1,139.1 224.3,139.4 225.4,139.8 226.5,140.4 227.7,141.2 228.8,142.2 229.9,143.3 231.1,144.7 232.2,146.4 233.3,148.3 234.5,150.6 235.6,153.5 236.7,157.0 237.9,161.6 239.0,168.2 240.1,179.4 241.2,250.0 242.4,179.8 243.5,169.1 244.6,163.0 245.8,158.8 246.9,155.7 248.0,153.3 249.2,151.5 250.3,150.0 251.4,148.8 252.6,147.8 253.7,147.1 254.8,146.6 256.0,146.3 257.1,146.1 258.2,146.1 259.4,146.2 260.5,146.6 261.6,147.0 262.8,147.6 263.9,148.4 265.0,149.4 266.2,150.5 267.3,151.9 268.4,153.6 269.6,155.5 270.7,157.8 271.8,160.7 273.0,164.2 274.1,168.8 275.2,175.4 276.4,186.6 277.5,250.0 278.6,187.0 279.8,176.2 280.9,170.1 282.0,165.9 283.2,162.8 284.3,160.4 285.4,158.5 286.6,157.0 287.7,155.8 288.8,154.9 290.0,154.1 291.1,153.6 292.2,153.2 293.4,153.1 294.5,153.0 295.6,153.2 296.8,153.4 297.9,153.9 299.0,154.5 300.2,155.2 301.3,156.2 302.4,157.3 303.6,158.7 304.7,160.3 305.8,162.2 307.0,164.6 308.1,167.4 309.2,170.9 310.4,175.5 311.5,182.0 312.6,193.2 313.8,250.0 314.9,193.6 316.0,182.8 317.1,176.7 318.3,172.5 319.4,169.3 320.5,166.9 321.7,165.0 322.8,163.4 323.9,162.2 325.1,161.2 326.2,160.5 327.3,159.9 328.5,159.6 329.6,159.4 330.7,159.3 331.9,159.4 333.0,159.7 334.1,160.1 335.3,160.7 336.4,161.4 337.5,162.4 338.7,163.5 339.8,164.8 340.9,166.4 342.1,168.3 343.2,170.6 344.3,173.4 345.5,176.9 346.6,181.5 347.7,188.0 348.9,199.1 350.0,250.0 351.1,199.5 352.3,188.7 353.4,182.5 354.5,178.3 355.7,175.2 356.8,172.7 357.9,170.8 359.1,169.2 360.2,168.0 361.3,167.0 362.5,166.2 363.6,165.7 364.7,165.3 365.9,165.0 367.0,165.0 368.1,165.1 369.3,165.3 370.4,165.7 371.5,166.3 372.7,167.0 373.8,167.9 374.9,169.0 376.1,170.3 377.2,171.9 378.3,173.8 379.5,176.1 380.6,178.9 381.7,182.3 382.9,186.9 384.0,193.4 385.1,204.5 386.2,250.0 387.4,204.8 388.5,194.0 389.6,187.9 390.8,183.6 391.9,180.4 393.0,178.0 394.2,176.0 395.3,174.5 396.4,173.2 397.6,172.2 398.7,171.4 399.8,170.8 401.0,170.4 402.1,170.2 403.2,170.1 404.4,170.2 405.5,170.4 406.6,170.8 407.8,171.3 408.9,172.0 410.0,172.9 411.2,174.0 412.3,175.3 413.4,176.9 414.6,178.8 415.7,181.0 416.8,183.8 418.0,187.3 419.1,191.8 420.2,198.3 421.4,209.4 422.5,250.0 423.6,209.7 424.8,198.9 425.9,192.7 427.0,188.4 428.2,185.2 429.3,182.8 430.4,180.8 431.6,179.2 432.7,177.9 433.8,176.9 435.0,176.1 436.1,175.5 437.2,175.1 438.4,174.8 439.5,174.8 440.6,174.8 441.8,175.0 442.9,175.4 444.0,175.9 445.2,176.6 446.3,177.5 447.4,178.6 448.6,179.9 449.7,181.4 450.8,183.3 452.0,185.5 453.1,188.3 454.2,191.7 455.4,196.3 456.5,202.7 457.6,213.8 458.8,250.0 459.9,214.1 461.0,203.3 462.1,197.1 463.3,192.8 464.4,189.6 465.5,187.1 466.7,185.2 467.8,183.6 468.9,182.3 470.1,181.2 471.2,180.4 472.3,179.8 473.5,179.4 474.6,179.1 475.7,179.0 476.9,179.1 478.0,179.3 479.1,179.6 480.3,180.2 481.4,180.8 482.5,181.7 483.7,182.8 484.8,184.1 485.9,185.6 487.1,187.5 488.2,189.7 489.3,192.4 490.5,195.9 491.6,200.4 492.7,206.9 493.9,217.9 495.0,250.0 496.1,218.2 497.3,207.4 498.4,201.2 499.5,196.9 500.7,193.7 501.8,191.2 502.9,189.2 504.1,187.6 505.2,186.3 506.3,185.2 507.5,184.4 508.6,183.8 509.7,183.4 510.9,183.1 512.0,183.0 513.1,183.0 514.3,183.2 515.4,183.6 516.5,184.1 517.7,184.8 518.8,185.6 519.9,186.7 521.1,187.9 522.2,189.5 523.3,191.3 524.5,193.6 525.6,196.3 526.7,199.7 527.9,204.2 529.0,210.7 530.1,221.8 531.2,250.0 532.4,222.0 533.5,211.1 534.6,204.9 535.8,200.6 536.9,197.4 538.0,194.9 539.2,192.9 540.3,191.3 541.4,190.0 542.6,189.0 543.7,188.1 544.8,187.5 546.0,187.1 547.1,186.8 548.2,186.7 549.4,186.7 550.5,186.9 551.6,187.2 552.8,187.7 553.9,188.4 555.0,189.3 556.2,190.3 557.3,191.6 558.4,193.1 559.6,194.9 560.7,197.2 561.8,199.9 563.0,203.3 564.1,207.8 565.2,214.3 566.4,225.3 567.5,250.0 568.6,225.5 569.8,214.7 570.9,208.5 572.0,204.2 573.2,201.0 574.3,198.4 575.4,196.4 576.6,194.8 577.7,193.5 578.8,192.5 580.0,191.6 581.1,191.0 582.2,190.5 583.4,190.2 584.5,190.1 585.6,190.1 586.8,190.3 587.9,190.7 589.0,191.2 590.2,191.8 591.3,192.7 592.4,193.7 593.6,195.0 594.7,196.5 595.8,198.3 597.0,200.5 598.1,203.3 599.2,206.7 600.4,211.2 601.5,217.6 602.6,228.7 603.8,250.0 604.9,228.9 606.0,218.0 607.1,211.8 608.3,207.5 609.4,204.3 610.5,201.8 611.7,199.7 612.8,198.1 613.9,196.8 615.1,195.7 616.2,194.9 617.3,194.3 618.5,193.8 619.6,193.5 620.7,193.4 621.9,193.4 623.0,193.6 624.1,193.9 625.3,194.4 626.4,195.1 627.5,195.9 628.7,196.9 629.8,198.2 630.9,199.7 632.1,201.5 633.2,203.7 634.3,206.5 635.5,209.9 636.6,214.4 637.7,220.8 638.9,231.9 640.0,250.0"/>
<line x1="370" y1="314" x2="395" y2="314" stroke="#e08a3c" stroke-width="2"/> <text x="400" y="318" font-size="12">Blackman: 첫 null 3.0 bin, 사이드로브 -58 dB</text> <text x="350.0" y="284" font-size="12" text-anchor="middle">중심에서 떨어진 거리 (bin) — 64점 창의 스펙트럼, 0 dB로 정규화</text>
</svg>
```

그림 5 — 64점 window 네 개의 스펙트럼(0 dB 정규화, 실제 FFT 계산값). 가로축은 중심에서 몇 bin 떨어졌는지. rect는 main lobe가 가장 좁지만(첫 null 1 bin) 사이드로브가 −13 dB로 높고 천천히 준다. Blackman은 main lobe가 3 bin으로 넓은 대신 −58 dB부터 시작한다.

### 5.4 Window 고르기 — 표와 관례

이 코드는 window 정의 관례(대칭 vs periodic)의 차이, 각 window의 coherent gain과 ENBW(등가 잡음 대역폭), 그리고 3.3절의 `kaiserord` 추정을 확인한다.

```python
import numpy as np, torch
from scipy import signal
N = 400
w_np = np.hanning(N)                                   # symmetric: cos(2πn/(N-1))
w_sp = signal.get_window("hann", N)                    # periodic(fftbins=True 기본): cos(2πn/N)
w_pt = torch.hann_window(N).double().numpy()           # periodic=True 기본
print("scipy vs torch (둘 다 periodic) max|diff| = %.1e" % np.abs(w_sp - w_pt).max(), "  np.hanning vs periodic max|diff| = %.2e" % np.abs(w_np - w_sp).max())
for name in ["boxcar", "hann", "hamming", "blackman"]:
    w = signal.get_window(name, 512)
    enbw = len(w) * np.sum(w**2) / np.sum(w)**2        # 등가 잡음 대역폭 (bin 단위)
    print(f"{name:8s} coherent gain {w.mean():.3f}  ENBW {enbw:.2f} bins")
numtaps, beta = signal.kaiserord(60, 1000 / 8000)      # 60 dB 감쇠, 전이대역 1 kHz (Nyquist=8 kHz 기준 정규화)
print("kaiserord: 60 dB, 1 kHz transition @16 kHz ->", numtaps, "taps, beta =", round(beta, 2))
```

```text
scipy vs torch (둘 다 periodic) max|diff| = 2.5e-07   np.hanning vs periodic max|diff| = 6.03e-03
boxcar   coherent gain 1.000  ENBW 1.00 bins
hann     coherent gain 0.500  ENBW 1.50 bins
hamming  coherent gain 0.540  ENBW 1.36 bins
blackman coherent gain 0.420  ENBW 1.73 bins
kaiserord: 60 dB, 1 kHz transition @16 kHz -> 60 taps, beta = 5.65
```

| window | 첫 null (bin) | 최대 사이드로브 | ENBW (bin) | 언제 |
|---|---|---|---|---|
| rectangular | 1 | −13 dB | 1.00 | 정수 주기가 보장될 때, 과도 신호 |
| Hann | 2 | −31 dB | 1.50 | **음성 STFT 기본**(torchaudio·librosa 기본) |
| Hamming | 2 | −42 dB | 1.36 | 고전 음성/MFCC(HTK, Kaldi 기본 povey는 변형), FIR 설계 |
| Blackman | 3 | −58 dB | 1.73 | 큰 신호 옆 작은 신호 찾기(동적 범위) |

(사이드로브 값은 그림 5의 64점 periodic 창 계산값. 교과서의 Hamming −43 dB와 소수점 차이가 있다.)

- **periodic vs symmetric**: `np.hanning(N)`은 cos(2πn/(N−1)) 대칭형, `scipy.signal.get_window("hann", N)`과 `torch.hann_window(N)`은 cos(2πn/N) periodic형이다. 차이는 최대 0.006으로 작지만 **golden vector가 안 맞는 흔한 원인**이다. STFT에는 periodic(겹쳐 더할 때 COLA가 정확히 성립), FIR 설계에는 대칭이 관례다.
- **coherent gain**(window 평균): Hann 0.5. 진폭을 읽을 때 Σw로 나눠야 하는 이유다.
- **ENBW**: 잡음이 window를 통과하며 몇 bin 폭만큼 모이나. Hann은 1.5 bin이라 rect보다 잡음 바닥이 1.76 dB 높다.

### 5.5 Zero-padding은 해상도를 올리지 않는다

FFT 길이 nfft를 데이터 길이 N보다 크게 하고 뒤를 0으로 채우면 bin 간격 fs/nfft는 촘촘해진다. 하지만 **실제로 구분할 수 있는 주파수 간격(해상도)은 데이터 길이 N(관측 시간 N/fs)이 정한다**. zero-padding은 같은 곡선(DTFT)을 더 촘촘히 **보간**해서 보여 줄 뿐이다.

이 코드는 40 Hz 떨어진 두 톤을 데이터 길이와 nfft를 바꿔 가며 분석한다.

```python
import numpy as np
from scipy.signal import find_peaks
fs = 16000
def tones(N):
    n = np.arange(N)
    return np.sin(2*np.pi*1000*n/fs) + np.sin(2*np.pi*1040*n/fs)   # 40 Hz 떨어진 두 톤
def count_peaks(x, nfft):
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)), n=nfft))         # n > len(x) 이면 뒤를 0으로 채움
    f = np.fft.rfftfreq(nfft, 1 / fs)
    band = (f > 900) & (f < 1150)
    pk, _ = find_peaks(X[band], prominence=0.05 * X.max())
    return np.round(f[band][pk], 1)
for N, nfft in [(256, 256), (256, 4096), (1024, 1024), (1024, 4096)]:
    print(f"data N={N:5d} ({N/fs*1e3:5.1f} ms), nfft={nfft:5d}, bin={fs/nfft:6.2f} Hz -> peaks {count_peaks(tones(N), nfft)}")
```

```text
data N=  256 ( 16.0 ms), nfft=  256, bin= 62.50 Hz -> peaks [1000.]
data N=  256 ( 16.0 ms), nfft= 4096, bin=  3.91 Hz -> peaks [1019.5]
data N= 1024 ( 64.0 ms), nfft= 1024, bin= 15.62 Hz -> peaks [1000.  1046.9]
data N= 1024 ( 64.0 ms), nfft= 4096, bin=  3.91 Hz -> peaks [1000.  1039.1]
```

출력에서 볼 것: 데이터 256개(16 ms)는 nfft를 4096으로 늘려도 peak가 **하나**(1019.5 Hz, 두 톤의 중간)다. 데이터를 1024개(64 ms)로 늘려야 둘로 갈라진다. nfft = 4096은 peak 위치를 1039.1 Hz처럼 더 정확히 읽게(보간) 해 줄 뿐이다. 음성 front-end에서 400 샘플(25 ms) 창을 512점 FFT로 하는 것도 zero-padding이다 — bin은 31.25 Hz지만 해상도는 400 샘플 기준(16000/400 = 40 Hz, Hann이면 main lobe ±80 Hz)이다. 512로 하는 이유는 radix-2 FFT를 쓰기 위해서다.

### 5.6 FFT — O(N²)를 O(N log N)으로

DFT를 그대로 하면 N²번 복소 곱이다(512점이면 262,144번). **FFT**(Cooley–Tukey radix-2)는 짝수/홀수 샘플로 나눠 재귀한다:

```
X[k]       = E[k] + W^k · O[k]        E = 짝수 샘플의 N/2점 DFT
X[k + N/2] = E[k] − W^k · O[k]        O = 홀수 샘플의 N/2점 DFT,  W = e^{−j2π/N}
```

말로 하면: N/2점 DFT 두 개를 복소 곱 N/2번과 덧셈으로 합친다. 이 "a ± W·b" 한 쌍이 **butterfly**다. 반복하면 log₂N 단계 × N/2 butterfly = **(N/2)·log₂N** 복소 곱. 512점이면 256 × 9 = 2,304번 — DFT보다 114배 적다.

```
8점 radix-2 DIT
입력 순서(bit-reversal): x0 x4 x2 x6 x1 x5 x3 x7     (1 = 001 → 100 = 4,  3 = 011 → 110 = 6)
단계 1 (len 2): 이웃 쌍 (x0,x4) (x2,x6) (x1,x5) (x3,x7) 에 butterfly, twiddle W⁰
단계 2 (len 4): 간격 2인 쌍, twiddle W⁰, W²
단계 3 (len 8): 간격 4인 쌍, twiddle W⁰, W¹, W², W³  → X0 … X7 (자연 순서로 나온다)
butterfly 하나:  a' = a + W·b,   b' = a − W·b      (복소 곱 1번, 복소 덧셈 2번)
```

### 5.7 C 구현: iterative radix-2 FFT

이 헤더는 bit-reversal 재배치와 log₂N 단계 butterfly로 in-place FFT를 한다. 8절 스트리밍 log-mel에서 다시 쓴다.

```c
#include <math.h>
#include <complex.h>
typedef float complex cf;
#define PI_D 3.14159265358979323846
/* 복소수 곱을 직접 푼다 (C의 '*'는 NaN/Inf 처리용 느린 경로를 부를 수 있다) */
static inline cf cmul(cf a, cf b) {
    return CMPLXF(crealf(a) * crealf(b) - cimagf(a) * cimagf(b),
                  crealf(a) * cimagf(b) + cimagf(a) * crealf(b));
}
/* tw[k] = e^{-j2πk/N}, k < N/2 — double로 계산해서 float로 저장 */
static void fft_init(cf *tw, int N) {
    for (int k = 0; k < N / 2; k++)
        tw[k] = CMPLXF((float)cos(-2 * PI_D * k / N), (float)sin(-2 * PI_D * k / N));
}
/* in-place iterative radix-2 DIT FFT. N은 2의 거듭제곱 */
static void fft_radix2(cf *x, const cf *tw, int N) {
    for (int i = 1, j = 0; i < N; i++) {                /* 1) bit-reversal 순서로 재배치 */
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j |= bit;
        if (i < j) { cf t = x[i]; x[i] = x[j]; x[j] = t; }
    }
    for (int len = 2; len <= N; len <<= 1) {            /* 2) log2(N) 단계의 butterfly */
        int half = len >> 1, step = N / len;
        for (int s = 0; s < N; s += len)
            for (int k = 0; k < half; k++) {
                cf a = x[s + k], t = cmul(tw[k * step], x[s + k + half]);
                x[s + k] = a + t;                       /* 위   = a + W·b */
                x[s + k + half] = a - t;                /* 아래 = a − W·b */
            }
    }
}
```

이 C 코드는 512점 FFT 결과를 파일로 쓰고, 2만 번 반복해 호스트(Apple M 계열 Mac)에서 시간을 잰다.

```c
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <time.h>
#include "fft.h"
#define N 512
int main(void) {
    static float xin[N]; static cf x[N], tw[N / 2];
    FILE *f = fopen("xfft.f32", "rb"); size_t r = fread(xin, 4, N, f); fclose(f);
    fft_init(tw, N);
    for (int i = 0; i < N; i++) x[i] = xin[i];
    fft_radix2(x, tw, N);
    f = fopen("Xc.f32", "wb"); fwrite(x, sizeof(cf), N, f); fclose(f);
    struct timespec t0, t1; const int R = 20000; volatile float sink = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int r2 = 0; r2 < R; r2++) {
        for (int i = 0; i < N; i++) x[i] = xin[i];
        fft_radix2(x, tw, N); sink += crealf(x[1]);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / R;
    printf("read %zu, N=%d: %.2f us per FFT (copy 포함), butterflies = %d\n", r, N, us, N / 2 * 9);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 fftmain.c -o fftmain -lm     # 경고 0개
```

이 Python 코드는 입력을 만들고 C 결과를 `np.fft.fft`(float64)와 비교한다.

```python
import numpy as np, subprocess
rng = np.random.default_rng(2)
x = rng.standard_normal(512).astype(np.float32)
x.tofile("xfft.f32")
print(subprocess.run(["./fftmain"], capture_output=True, text=True).stdout, end="")
Xc = np.fromfile("Xc.f32", np.complex64)
Xn = np.fft.fft(x.astype(np.float64))
print("max |C - numpy| = %.2e, relative = %.2e" % (np.abs(Xc - Xn).max(), np.abs(Xc - Xn).max() / np.abs(Xn).max()))
```

```text
read 512, N=512: 3.02 us per FFT (copy 포함), butterflies = 2304
max |C - numpy| = 5.86e-06, relative = 1.14e-07
```

출력에서 볼 것: 최대 오차 / 최대 크기 = 1e-7 — float32 정밀도 그대로다. 호스트에서 512점 FFT 한 번이 약 3 µs(실행마다 3~5 µs로 흔들렸다). Mac의 수 GHz 코어 기준이라 MCU 시간을 직접 말해 주지는 않는다. MCU에서는 DWT cycle counter로 직접 재야 한다(D6).

`cmul`을 직접 풀어 쓴 이유: C의 복소수 `*`는 Inf/NaN 규칙을 지키려고 느린 라이브러리 함수(`__mulsc3`)를 부를 수 있다. 펌웨어에서는 어차피 `float re, im` 구조체로 직접 쓰는 경우가 많다.

### 5.8 Real FFT 트릭 — 실수 N점을 복소 N/2점으로

오디오는 실수다. 실수 N개를 짝수 → 실수부, 홀수 → 허수부로 묶어 N/2점 **복소** FFT 한 번을 하고, 한 단계 후처리로 N점 실수 FFT를 얻는다. 연산이 거의 절반이다.

이 코드는 그 후처리 공식이 `np.fft.rfft`와 같은 결과를 내는지 확인한다.

```python
import numpy as np
rng = np.random.default_rng(3)
N = 16
x = rng.standard_normal(N)
z = x[0::2] + 1j * x[1::2]                 # 실수 N개 → 복소수 N/2개로 포장 (짝수=실수부, 홀수=허수부)
Z = np.fft.fft(z)                          # N/2점 복소 FFT 한 번
k = np.arange(N // 2 + 1)
Zk = Z[k % (N // 2)]
Zc = np.conj(Z[(-k) % (N // 2)])           # Z*[N/2 - k]
E = 0.5 * (Zk + Zc)                        # 짝수 샘플의 FFT
O = -0.5j * (Zk - Zc)                      # 홀수 샘플의 FFT
X = E + np.exp(-2j * np.pi * k / N) * O    # 한 단계 butterfly로 합침
print("N/2-point complex FFT + post-process == rfft?", np.allclose(X, np.fft.rfft(x)))
print("output bins:", len(X), "(DC ... Nyquist), DC and Nyquist imag:", np.round(X[0].imag, 12), np.round(X[-1].imag, 12))
```

```text
N/2-point complex FFT + post-process == rfft? True
output bins: 9 (DC ... Nyquist), DC and Nyquist imag: 0.0 0.0
```

출력에서 볼 것: N/2점 복소 FFT + 후처리 = `rfft` (True). 결과는 N/2 + 1개 bin이고, DC와 Nyquist bin은 허수부가 0이다 — 그래서 라이브러리들이 이 두 실수를 한 복소수 자리에 **포장(pack)** 한다.

### 5.9 CMSIS-DSP와 고정소수점 FFT

ARM Cortex-M에서 쓰는 CMSIS-DSP의 실수 FFT:

```c
arm_rfft_fast_instance_f32 S;
arm_rfft_fast_init_f32(&S, 512);          /* 길이: 32 ~ 4096 의 2의 거듭제곱 */
arm_rfft_fast_f32(&S, in, out, 0);        /* 마지막 인자 0 = forward, 1 = inverse */
/* out[0] = X[0].re (DC), out[1] = X[N/2].re (Nyquist), 그 뒤 re, im 쌍 — 포장 형식 */
arm_cmplx_mag_squared_f32(out + 2, pw + 1, 255);   /* bin 1..255 의 |X|² */
```

(정확한 시그니처와 지원 길이는 CMSIS-DSP 버전 문서로 확인. 최근 버전에서 `in` 버퍼는 계산 중에 **덮어써진다**는 점이 자주 걸린다.) DC와 Nyquist는 `out[0]`, `out[1]`에 따로 있으므로 magnitude 함수를 배열 전체에 그냥 돌리면 bin 0이 "DC + j·Nyquist"로 섞인다 — 흔한 버그다.

고정소수점 FFT(`arm_cfft_q15`, `arm_rfft_q15`, q31 버전)는 각 butterfly 단계에서 overflow를 막기 위해 **단계마다 1/2로 스케일 다운**한다. 결과는 대략 1/N로 줄어든 값이고 출력 Q 형식이 FFT 길이마다 다르다(문서에 길이별 표가 있다). 작은 신호는 9단계 동안 하위 비트가 깎여 SNR이 크게 떨어진다. 대안:

- **Block floating point**(E4 2.5절): 단계마다 블록 최대값을 보고 필요할 때만 시프트하고, 시프트 횟수를 지수로 기록한다. HiFi·Hexagon 라이브러리의 고정소수점 FFT가 흔히 쓰는 방식이다.
- 입력 정규화: frame마다 최대값을 보고 FFT 전에 왼쪽 시프트(그 지수는 나중에 log 영역에서 빼면 된다 — log(2^e·P) = e·ln2 + log P).
- FPU가 있으면 그냥 `arm_rfft_fast_f32`.

### 5.10 함정

- 진폭을 읽을 때 2/N(또는 2/Σw) 보정을 잊음, DC와 Nyquist에는 2를 곱하지 않아야 함.
- `rfft` 길이 N/2 + 1을 N/2로 착각 → mel 행렬 shape 257 vs 256 불일치.
- Zero-padding으로 해상도가 좋아졌다고 착각 (5.5절).
- 고정소수점 FFT의 출력 스케일(1/N)을 잊고 Python 결과와 비교 → "값이 512배 작다".

---

## 6. STFT — 시간에 따라 변하는 스펙트럼

### 6.1 직관: 악보 만들기

음성은 수십 ms마다 성질이 바뀐다. 1초 전체를 FFT 하면 "어떤 주파수가 있었나"는 알지만 "언제"는 사라진다. 그래서 짧은 구간(frame)을 잘라 window를 곱하고 FFT를 하고, 조금(hop) 옮겨 또 한다. 이것이 **STFT(Short-Time Fourier Transform)** 이고, 결과를 시간 × 주파수 그림으로 그린 것이 **spectrogram** — 소리의 악보다.

```
X[m, k] = Σ_{n=0}^{N−1} x[n + m·H] · w[n] · e^{−j2πkn/N}
m = frame 번호, H = hop, N = 창 길이 (nfft ≥ N 이면 zero-pad)
frame 수 (center=False) = 1 + ⌊(L − N) / H⌋
```

말로 하면: "링버퍼에서 N개 꺼내 → window 곱 → FFT"를 H 샘플마다 반복. B5 1.2~1.3에서 본 것을 공식으로 쓴 것이다.

### 6.2 창 길이 trade-off — 시간 해상도 vs 주파수 해상도

창이 길면 주파수 해상도(fs/N)가 좋아지지만 그 창 안의 사건이 시간적으로 뭉개진다. 창이 짧으면 반대다. 둘을 동시에 좋게 할 수는 없다(시간-주파수 불확정성: Δt·Δf ≥ 상수).

흔한 설정을 계산해 보면(16 kHz, Hann main lobe = 첫 null까지 ±2 bin):

| 창 (ms) | N | hop | frame/s | Δf = fs/N (Hz) | Hann main lobe 폭 (Hz) |
|---|---|---|---|---|---|
| 8 | 128 | 64 | 250 | 125 | ±250 |
| 25 | 400 | 160 | 100 | 40 | ±80 |
| 32 | 512 | 160 | 100 | 31.25 | ±62.5 |
| 64 | 1024 | 256 | 62.5 | 15.6 | ±31.3 |

음성 표준 25 ms / 10 ms(N = 400, H = 160)는 초당 100 frame, 해상도 40 Hz(Hann main lobe ±80 Hz). 사람 목소리의 기본 주파수(pitch) 배음 간격이 100~250 Hz라 25 ms면 배음이 대략 구분되고, 성대·입 모양(formant)은 20~30 ms 동안 거의 안 변하므로 그 안에서 "정지 신호"로 볼 수 있다. 이것이 25 ms의 근거다. hop 10 ms는 음소 변화(수십 ms)를 놓치지 않을 만큼 촘촘하면서 frame 수를 감당할 만한 값이다.

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
<text x="60" y="28" font-size="12">짧은 창: 64샘플 = 8 ms, Δf = 125 Hz</text> <rect x="60.0" y="271.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.07"/> <rect x="60.0" y="262.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.26"/> <rect x="60.0" y="254.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.73"/> <rect x="60.0" y="245.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="60.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.84"/> <rect x="60.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="60.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.20"/> <rect x="65.0" y="271.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.09"/> <rect x="65.0" y="262.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.26"/> <rect x="65.0" y="254.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.61"/> <rect x="65.0" y="245.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.86"/> <rect x="65.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="65.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.71"/> <rect x="65.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="70.0" y="254.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.28"/> <rect x="70.0" y="245.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.79"/> <rect x="70.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="70.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.81"/> <rect x="70.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.40"/> <rect x="70.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.12"/> <rect x="75.0" y="262.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.06"/> <rect x="75.0" y="254.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="75.0" y="245.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.66"/> <rect x="75.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="75.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="75.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.66"/> <rect x="75.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="80.0" y="254.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.13"/> <rect x="80.0" y="245.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.42"/> <rect x="80.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.82"/> <rect x="80.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="80.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.79"/> <rect x="80.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/>
<rect x="85.0" y="245.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="85.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.71"/> <rect x="85.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="85.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.86"/> <rect x="85.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.59"/> <rect x="90.0" y="245.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.21"/> <rect x="90.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.52"/> <rect x="90.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.84"/> <rect x="90.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="90.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="95.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="95.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="95.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="95.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.84"/> <rect x="95.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.21"/> <rect x="100.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.24"/> <rect x="100.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.59"/> <rect x="100.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.86"/> <rect x="100.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="100.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="105.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="105.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.79"/> <rect x="105.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="105.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.42"/> <rect x="105.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="110.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="110.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.66"/> <rect x="110.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="110.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.66"/> <rect x="110.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="115.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.12"/> <rect x="115.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.40"/> <rect x="115.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="115.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.79"/>
<rect x="115.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.27"/> <rect x="120.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="120.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="120.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.86"/> <rect x="120.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.61"/> <rect x="120.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="125.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.20"/> <rect x="125.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.84"/> <rect x="125.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="125.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="125.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="130.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="130.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="130.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.84"/> <rect x="130.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="130.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.20"/> <rect x="135.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.61"/> <rect x="135.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.86"/> <rect x="135.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="135.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="135.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="140.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.27"/> <rect x="140.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.79"/> <rect x="140.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="140.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.81"/> <rect x="140.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.40"/> <rect x="140.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.12"/> <rect x="145.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="145.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.66"/> <rect x="145.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="145.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="145.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.66"/> <rect x="145.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="150.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/>
<rect x="150.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.42"/> <rect x="150.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.82"/> <rect x="150.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="150.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.79"/> <rect x="150.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="155.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="155.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.71"/> <rect x="155.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="155.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.86"/> <rect x="155.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.59"/> <rect x="155.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.24"/> <rect x="160.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.21"/> <rect x="160.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.52"/> <rect x="160.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.84"/> <rect x="160.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="160.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="160.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="165.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="165.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="165.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="165.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.84"/> <rect x="165.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.52"/> <rect x="165.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.21"/> <rect x="170.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.24"/> <rect x="170.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.59"/> <rect x="170.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.86"/> <rect x="170.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="170.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.71"/> <rect x="170.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="175.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="175.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.79"/> <rect x="175.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="175.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.82"/> <rect x="175.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.42"/>
<rect x="180.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="180.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.66"/> <rect x="180.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="180.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="180.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.66"/> <rect x="185.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.12"/> <rect x="185.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.40"/> <rect x="185.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.81"/> <rect x="185.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="185.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.79"/> <rect x="190.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="190.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="190.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="190.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.86"/> <rect x="190.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="195.0" y="271.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="262.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="254.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="245.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.84"/> <rect x="195.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.95"/> <rect x="195.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/>
<rect x="195.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="65.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="57.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="48.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="195.0" y="40.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="200.0" y="271.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="262.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="254.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="245.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.95"/> <rect x="200.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="1.00"/> <rect x="200.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="65.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/>
<rect x="200.0" y="57.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="48.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="200.0" y="40.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="205.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="205.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.61"/> <rect x="205.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="205.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="205.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="210.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.27"/> <rect x="210.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="210.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.81"/> <rect x="210.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.40"/> <rect x="210.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.12"/> <rect x="215.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="215.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="215.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="215.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.66"/> <rect x="215.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="220.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="220.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.82"/> <rect x="220.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="220.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.79"/> <rect x="220.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="225.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.71"/> <rect x="225.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="225.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.86"/> <rect x="225.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.59"/> <rect x="225.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.24"/> <rect x="230.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.52"/> <rect x="230.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.84"/> <rect x="230.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="230.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="230.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="235.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/>
<rect x="235.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="235.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="235.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.84"/> <rect x="235.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.52"/> <rect x="235.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.21"/> <rect x="240.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.24"/> <rect x="240.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.59"/> <rect x="240.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.86"/> <rect x="240.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="240.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.71"/> <rect x="240.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="245.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="245.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.79"/> <rect x="245.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="245.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.82"/> <rect x="245.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.42"/> <rect x="245.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="250.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="250.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.66"/> <rect x="250.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="250.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="250.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.66"/> <rect x="250.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="255.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.12"/> <rect x="255.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.40"/> <rect x="255.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.81"/> <rect x="255.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="255.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.79"/> <rect x="255.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.27"/> <rect x="260.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="260.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="260.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="260.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.86"/> <rect x="260.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.61"/>
<rect x="260.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="265.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.20"/> <rect x="265.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="265.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.84"/> <rect x="265.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="265.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="265.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="270.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="270.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="270.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="270.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.84"/> <rect x="270.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="275.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="275.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.61"/> <rect x="275.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.86"/> <rect x="275.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="275.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="280.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.27"/> <rect x="280.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.79"/> <rect x="280.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="280.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.81"/> <rect x="280.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.12"/> <rect x="285.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="285.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.66"/> <rect x="285.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="285.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="285.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="290.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="290.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.42"/> <rect x="290.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.82"/> <rect x="290.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="290.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="295.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="295.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.71"/>
<rect x="295.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="295.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.59"/> <rect x="295.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.24"/> <rect x="300.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.21"/> <rect x="300.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.52"/> <rect x="300.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="300.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="300.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="305.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="305.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="305.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.84"/> <rect x="305.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.52"/> <rect x="305.0" y="65.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.21"/> <rect x="310.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.24"/> <rect x="310.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.86"/> <rect x="310.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="310.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.71"/> <rect x="310.0" y="65.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="315.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.79"/> <rect x="315.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="315.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.82"/> <rect x="315.0" y="65.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.42"/> <rect x="315.0" y="57.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.13"/> <rect x="320.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.66"/> <rect x="320.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="320.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="320.0" y="65.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.66"/> <rect x="320.0" y="57.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="320.0" y="48.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.06"/> <rect x="325.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.40"/> <rect x="325.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.81"/> <rect x="325.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="325.0" y="65.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.79"/> <rect x="325.0" y="57.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.28"/>
<rect x="330.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="330.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.71"/> <rect x="330.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="330.0" y="65.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.86"/> <rect x="330.0" y="57.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.61"/> <rect x="330.0" y="48.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.26"/> <rect x="330.0" y="40.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.09"/> <rect x="335.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.20"/> <rect x="335.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="335.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.84"/> <rect x="335.0" y="65.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="335.0" y="57.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.73"/> <rect x="335.0" y="48.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.26"/> <rect x="335.0" y="40.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.07"/> <rect x="60" y="40" width="280" height="240" fill="none" stroke="currentColor" stroke-width="0.8"/> <text x="56" y="284.0" font-size="12" text-anchor="end">0k</text> <text x="56" y="224.0" font-size="12" text-anchor="end">1k</text> <text x="56" y="164.0" font-size="12" text-anchor="end">2k</text> <text x="56" y="104.0" font-size="12" text-anchor="end">3k</text> <text x="56" y="44.0" font-size="12" text-anchor="end">4k</text> <text x="60.0" y="296" font-size="12" text-anchor="middle">0s</text> <text x="200.0" y="296" font-size="12" text-anchor="middle">0.5s</text> <text x="340.0" y="296" font-size="12" text-anchor="middle">1s</text> <text x="370" y="28" font-size="12">긴 창: 512샘플 = 64 ms, Δf = 15.6 Hz</text> <rect x="375.0" y="245.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.91"/> <rect x="375.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="375.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.26"/> <rect x="380.0" y="245.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.61"/> <rect x="380.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="380.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.67"/> <rect x="385.0" y="245.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.21"/> <rect x="385.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="385.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.94"/> <rect x="390.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.90"/> <rect x="390.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="390.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.29"/>
<rect x="395.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.59"/> <rect x="395.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="395.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.69"/> <rect x="400.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.18"/> <rect x="400.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="400.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.94"/> <rect x="405.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.89"/> <rect x="405.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="405.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.32"/> <rect x="410.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.56"/> <rect x="410.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="410.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.95"/> <rect x="415.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="415.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="415.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.22"/> <rect x="420.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.53"/> <rect x="420.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="420.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.62"/> <rect x="425.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.13"/> <rect x="425.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="425.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.92"/> <rect x="430.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.92"/> <rect x="430.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="430.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.25"/> <rect x="435.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.63"/> <rect x="435.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="435.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.65"/> <rect x="440.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.23"/> <rect x="440.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="440.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.93"/> <rect x="445.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.91"/> <rect x="445.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="445.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.27"/> <rect x="450.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.60"/>
<rect x="450.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="450.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.68"/> <rect x="455.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.20"/> <rect x="455.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="455.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="455.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.30"/> <rect x="460.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.58"/> <rect x="460.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="460.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="465.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.17"/> <rect x="465.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="465.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.95"/> <rect x="470.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="470.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="470.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.33"/> <rect x="475.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.55"/> <rect x="475.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="475.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.73"/> <rect x="480.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="480.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="480.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.95"/> <rect x="485.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.86"/> <rect x="485.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="485.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.36"/> <rect x="490.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.52"/> <rect x="490.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="490.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="495.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.12"/> <rect x="495.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="495.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="495.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.39"/> <rect x="500.0" y="271.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="262.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="254.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/>
<rect x="500.0" y="245.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.53"/> <rect x="500.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.96"/> <rect x="500.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.80"/> <rect x="500.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="65.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="57.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="48.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="500.0" y="40.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="505.0" y="271.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="262.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="254.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="245.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/>
<rect x="505.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.71"/> <rect x="505.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="1.00"/> <rect x="505.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.99"/> <rect x="505.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="65.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="57.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="48.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="505.0" y="40.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.70"/> <rect x="510.0" y="271.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="262.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="254.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="245.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.87"/> <rect x="510.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.99"/>
<rect x="510.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.65"/> <rect x="510.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="65.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="57.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="48.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="510.0" y="40.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.64"/> <rect x="515.0" y="271.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="262.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="254.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="245.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="237.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="228.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="220.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="211.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="202.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="194.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="185.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="177.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="168.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.45"/> <rect x="515.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="515.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.69"/> <rect x="515.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/>
<rect x="515.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="65.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="57.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="48.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="515.0" y="40.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.14"/> <rect x="520.0" y="160.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.07"/> <rect x="520.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="520.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.94"/> <rect x="525.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.89"/> <rect x="525.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="525.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.31"/> <rect x="530.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.57"/> <rect x="530.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="530.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.71"/> <rect x="535.0" y="151.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.16"/> <rect x="535.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="535.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.95"/> <rect x="540.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.88"/> <rect x="540.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="540.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.74"/> <rect x="545.0" y="142.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.13"/> <rect x="545.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="545.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.96"/> <rect x="550.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.86"/> <rect x="550.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="550.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.37"/> <rect x="555.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.51"/> <rect x="555.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="555.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.76"/> <rect x="560.0" y="134.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.11"/> <rect x="560.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="560.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.96"/>
<rect x="565.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.84"/> <rect x="565.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="565.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.40"/> <rect x="570.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.48"/> <rect x="570.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="570.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.78"/> <rect x="575.0" y="125.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.09"/> <rect x="575.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="575.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.96"/> <rect x="580.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.82"/> <rect x="580.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="580.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.43"/> <rect x="585.0" y="117.1" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.45"/> <rect x="585.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="585.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="585.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.07"/> <rect x="590.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.80"/> <rect x="590.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="590.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.46"/> <rect x="595.0" y="108.6" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.42"/> <rect x="595.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="595.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.83"/> <rect x="600.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.96"/> <rect x="600.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="605.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.78"/> <rect x="605.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="605.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.35"/> <rect x="610.0" y="100.0" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.39"/> <rect x="610.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="610.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="615.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="615.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.96"/> <rect x="620.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.85"/> <rect x="620.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/>
<rect x="620.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.38"/> <rect x="625.0" y="91.4" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.50"/> <rect x="625.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="625.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.96"/> <rect x="630.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.83"/> <rect x="630.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="630.0" y="65.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.41"/> <rect x="635.0" y="82.9" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.47"/> <rect x="635.0" y="74.3" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.97"/> <rect x="635.0" y="65.7" width="5.3" height="8.9" fill="#4a7bd0" fill-opacity="0.79"/> <rect x="370" y="40" width="280" height="240" fill="none" stroke="currentColor" stroke-width="0.8"/> <text x="366" y="284.0" font-size="12" text-anchor="end">0k</text> <text x="366" y="224.0" font-size="12" text-anchor="end">1k</text> <text x="366" y="164.0" font-size="12" text-anchor="end">2k</text> <text x="366" y="104.0" font-size="12" text-anchor="end">3k</text> <text x="366" y="44.0" font-size="12" text-anchor="end">4k</text> <text x="370.0" y="296" font-size="12" text-anchor="middle">0s</text> <text x="510.0" y="296" font-size="12" text-anchor="middle">0.5s</text> <text x="650.0" y="296" font-size="12" text-anchor="middle">1s</text> <text x="330" y="322" font-size="12" text-anchor="middle">세로 = 주파수(Hz), 가로 = 시간 — 진할수록 큰 에너지(0 ~ -50 dB). 신호: 500→3500 Hz chirp + 0.5 s click</text>
</svg>
```

그림 6 — 같은 신호(500 → 3500 Hz linear chirp + 0.5초 지점 click, fs = 8 kHz)를 두 설정으로 그린 spectrogram. `scipy.signal.stft` 실제 값(dB)을 격자로 모아 그렸다. 왼쪽(8 ms 창)은 click이 얇은 세로선이지만 chirp가 두껍게 번진다. 오른쪽(64 ms 창)은 chirp가 가는 선인데 click이 64 ms 폭으로 퍼진다.

웨어러블 관점: 박수·문 닫힘·탭 제스처 같은 **과도음 검출**이 목적이면 짧은 창, 음높이·화자 특징처럼 **주파수 정밀도**가 필요하면 긴 창. 같은 마이크 스트림에서 두 종류 특징을 따로 뽑기도 한다.

### 6.3 COLA와 overlap-add — 다시 소리로 되돌리기

특징 추출만 하면 STFT를 되돌릴 일이 없지만, **잡음 제거·beamforming·AEC**(G4)처럼 STFT 영역에서 신호를 고치고 다시 소리로 만드는 처리는 **ISTFT**가 필요하다. 각 frame을 IFFT 해서 원래 위치에 겹쳐 더하는(overlap-add, OLA) 것이 기본이고, 그때 window들이 겹쳐 더해진 합이 상수여야 원래 크기가 복원된다:

```
COLA (Constant Overlap-Add):   Σ_m w[n − m·H] = 상수   (모든 n)
```

이 코드는 여러 window/hop 조합의 COLA 여부를 `scipy.signal.check_COLA`와 직접 합으로 확인하고, STFT → ISTFT 복원 오차를 잰다.

```python
import numpy as np
from scipy import signal
N = 400
for name, hop in [("hann", 200), ("hann", 160), ("hamming", 200), ("boxcar", 400), ("hann", 100)]:
    w = signal.get_window(name, N)                       # periodic 창
    ok = signal.check_COLA(w, N, N - hop)                # Σ_m w[n - m·hop] 가 상수인가?
    s = np.zeros(N * 6)
    for m in range(0, len(s) - N, hop): s[m:m+N] += w    # 직접 겹쳐 더해 보기
    mid = s[N:-2*N]
    print(f"{name:8s} N={N} hop={hop}: COLA={ok!s:5s}  overlap-sum min/max = {mid.min():.3f}/{mid.max():.3f}")
# STFT → ISTFT 재합성 (hann, 50% overlap)
rng = np.random.default_rng(4); x = rng.standard_normal(16000)
f, t, Z = signal.stft(x, fs=16000, window="hann", nperseg=512, noverlap=256)
_, xr = signal.istft(Z, fs=16000, window="hann", nperseg=512, noverlap=256)
print("ISTFT reconstruction max |err| =", np.abs(xr[:len(x)] - x).max())
```

```text
hann     N=400 hop=200: COLA=True   overlap-sum min/max = 1.000/1.000
hann     N=400 hop=160: COLA=False  overlap-sum min/max = 1.191/1.309
hamming  N=400 hop=200: COLA=True   overlap-sum min/max = 1.080/1.080
boxcar   N=400 hop=400: COLA=True   overlap-sum min/max = 1.000/1.000
hann     N=400 hop=100: COLA=True   overlap-sum min/max = 2.000/2.000
ISTFT reconstruction max |err| = 8.881784197001252e-16
```

출력에서 볼 것: periodic Hann은 hop = N/2(50%)이나 N/4(75%)에서 합이 정확히 1, 2로 일정하다. 그런데 **음성 표준 400/160은 COLA가 아니다**(합이 1.19~1.31로 출렁인다). 특징 추출에는 아무 문제가 없지만, 같은 설정으로 잡음 제거 후 재합성하면 10 ms 주기로 진폭이 흔들린다. `scipy.signal.istft`는 window 제곱합으로 나눠 주는 WOLA 방식이라 NOLA(겹친 window 제곱합이 0이 아님) 조건만 맞으면 복원되고, 여기서 오차 9e-16으로 완벽 복원됐다.

---

## 7. mel · log-mel · MFCC — 모델 입력 만들기

B5 1.4~1.8에서 log-mel 파이프라인을 numpy로 만들고 torchaudio와 맞췄다(frame 위치, `center`, shape 순서 함정). 여기서는 각 단계의 **설계 선택지와 그 수치적 의미**를 깊게 본다.

### 7.1 mel scale — HTK와 Slaney 두 가지가 있다

사람 귀의 음높이 지각을 흉내 낸 주파수 축이다. 문제는 **공식이 두 개**라는 것이다.

```
HTK:     mel(f) = 2595 · log10(1 + f/700)          (= 1127 · ln(1 + f/700), Kaldi도 이 꼴)
Slaney:  mel(f) = 3f / 200                         (f < 1000 Hz, 선형)
         mel(f) = 15 + 27 · ln(f/1000) / ln(6.4)   (f ≥ 1000 Hz, 로그)
```

말로 하면: 둘 다 "1 kHz 아래는 거의 선형, 위는 로그"이지만 꺾이는 모양과 단위가 다르다. HTK는 1000 Hz ↔ 1000 mel, Slaney(Malcolm Slaney의 Auditory Toolbox)는 1000 Hz ↔ 15 mel이다. 단위는 상관없다(경계점을 mel 축에서 균등하게 찍고 Hz로 되돌리니까). 중요한 건 **경계점 위치가 달라진다**는 것.

| 라이브러리 | 기본 mel 공식 | 기본 필터 정규화 |
|---|---|---|
| `torchaudio.transforms.MelSpectrogram` | `mel_scale="htk"` | `norm=None` (꼭짓점 1) |
| `librosa.filters.mel` | Slaney (`htk=False`) | `norm="slaney"` (면적 정규화) |
| Kaldi / `torchaudio.compliance.kaldi.fbank` | 1127·ln(1 + f/700) | 꼭짓점 1 |
| TensorFlow `tf.signal.linear_to_mel_weight_matrix` | HTK 꼴 | 꼭짓점 1 |

(librosa 기본값은 문서 기준. OpenAI Whisper의 mel 필터는 librosa로 만든 것으로 알려져 있어 Slaney 꼴이다 — 모델을 가져다 쓸 때는 그 저장소의 전처리 코드를 직접 확인하자.)

이 코드는 두 공식을 numpy로 구현하고, 같은 공식으로 만든 삼각 필터뱅크가 torchaudio의 `melscale_fbanks(mel_scale=..., norm=...)`와 같은지 확인한다.

```python
import numpy as np, torchaudio.functional as AF
def hz2mel_htk(f):    return 2595.0 * np.log10(1.0 + f / 700.0)
def mel2hz_htk(m):    return 700.0 * (10 ** (m / 2595.0) - 1.0)
def hz2mel_slaney(f):                      # 1 kHz 아래 선형, 위는 로그 (Auditory Toolbox / librosa 기본)
    f = np.asarray(f, float)
    return np.where(f < 1000, 3 * f / 200, 15 + np.log(np.maximum(f, 1e-9) / 1000) / (np.log(6.4) / 27))
def mel2hz_slaney(m):
    m = np.asarray(m, float)
    return np.where(m < 15, 200 * m / 3, 1000 * np.exp((m - 15) * np.log(6.4) / 27))
for f in [500, 1000, 4000, 8000]:
    print(f"{f:5d} Hz: HTK {hz2mel_htk(f):8.2f} mel   Slaney {float(hz2mel_slaney(f)):6.3f} mel")
def fbank(n_fft, sr, n_mels, h2m, m2h, norm):
    hz = m2h(np.linspace(h2m(0.0), h2m(sr / 2), n_mels + 2))     # mel 축 균등 → Hz로
    bins = np.fft.rfftfreq(n_fft, 1 / sr)
    up = (bins[:, None] - hz[None, :-2]) / (hz[1:-1] - hz[:-2])   # 올라가는 변
    dn = (hz[None, 2:] - bins[:, None]) / (hz[2:] - hz[1:-1])     # 내려가는 변
    fb = np.maximum(0, np.minimum(up, dn))                         # [n_freqs, n_mels]
    if norm == "slaney": fb *= 2.0 / (hz[2:] - hz[:-2])            # 면적(대역폭) 정규화
    return fb
for scale, norm, h2m, m2h in [("htk", None, hz2mel_htk, mel2hz_htk), ("slaney", "slaney", hz2mel_slaney, mel2hz_slaney)]:
    mine = fbank(512, 16000, 40, h2m, m2h, norm)
    ta = AF.melscale_fbanks(257, 0.0, 8000.0, 40, 16000, norm=norm, mel_scale=scale).numpy()
    print(f"mel_scale={scale:6s} norm={norm!s:6s}: max|mine - torchaudio| = {np.abs(mine - ta).max():.2e}, "
          f"peak of filter 0 / 39 = {ta[:,0].max():.4f} / {ta[:,39].max():.4f}")
```

```text
  500 Hz: HTK   607.45 mel   Slaney  7.500 mel
 1000 Hz: HTK   999.99 mel   Slaney 15.000 mel
 4000 Hz: HTK  2146.06 mel   Slaney 35.164 mel
 8000 Hz: HTK  2840.02 mel   Slaney 45.246 mel
mel_scale=htk    norm=None  : max|mine - torchaudio| = 2.99e-06, peak of filter 0 / 39 = 0.7042 / 0.9741
mel_scale=slaney norm=slaney: max|mine - torchaudio| = 1.80e-08, peak of filter 0 / 39 = 0.0115 / 0.0017
```

출력에서 볼 것:

- HTK 1000 Hz = 999.99 mel, Slaney 1000 Hz = 15 mel. 8000 Hz는 2840 vs 45.25.
- numpy 구현이 torchaudio와 HTK 3e-6, Slaney 2e-8 안에서 같다 — 즉 **torchaudio 옵션 두 개(`mel_scale`, `norm`)만 맞추면 librosa 스타일도 재현된다**.
- HTK 필터 0의 최대값이 1이 아니라 0.70이다. 512점 FFT bin(31.25 Hz 간격)이 삼각형 꼭짓점에 정확히 떨어지지 않기 때문. 맨 아래 필터들은 폭이 bin 2~3개뿐이라 매우 거칠다.
- Slaney 정규화(`norm="slaney"`)는 각 삼각형에 2/(오른쪽 끝 − 왼쪽 끝 Hz)를 곱해 **면적을 같게** 만든다. 그래서 넓은 고주파 필터는 높이가 낮다(필터 39의 꼭짓점 0.0017 vs 필터 0의 0.0115). 정규화 없이는 넓은 필터가 더 많은 bin을 더해서 고주파 band 값이 체계적으로 크다.

```svg
<svg viewBox="0 0 660 400" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="170" x2="640" y2="170" stroke="currentColor"/> <text x="60" y="28" font-size="13">(a) HTK mel, norm=None — 꼭짓점 높이 ≤ 1</text> <polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="60.0,170.0 62.3,133.7 64.5,97.3 66.8,61.0 69.1,53.1 71.3,84.5 73.6,115.8 75.9,147.1 78.1,170.0"/> <polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="66.8,170.0 69.1,156.8 71.3,125.5 73.6,94.2 75.9,62.8 78.1,47.2 80.4,74.3 82.7,101.3 84.9,128.3 87.2,155.3 89.5,170.0"/> <polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="75.9,170.0 78.1,162.7 80.4,135.7 82.7,108.7 84.9,81.7 87.2,54.7 89.5,50.6 91.7,73.9 94.0,97.1 96.2,120.4 98.5,143.7 100.8,167.0 103.0,170.0"/> <polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="87.2,170.0 89.5,159.4 91.7,136.1 94.0,112.8 96.2,89.5 98.5,66.2 100.8,42.9 103.0,57.5 105.3,77.5 107.6,97.6 109.8,117.7 112.1,137.8 114.4,157.9 116.6,170.0"/> <polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="100.8,170.0 103.0,152.5 105.3,132.4 107.6,112.3 109.8,92.3 112.1,72.2 114.4,52.1 116.6,46.8 118.9,64.1 121.2,81.4 123.4,98.8 125.7,116.1 128.0,133.4 130.2,150.7 132.5,168.0 134.8,170.0"/> <polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="114.4,170.0 116.6,163.1 118.9,145.8 121.2,128.5 123.4,111.2 125.7,93.9 128.0,76.6 130.2,59.3 132.5,41.9 134.8,53.2 137.0,68.1 139.3,83.0 141.6,98.0 143.8,112.9 146.1,127.8 148.4,142.7 150.6,157.7 152.9,170.0"/> <polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="132.5,170.0 134.8,156.8 137.0,141.9 139.3,126.9 141.6,112.0 143.8,97.1 146.1,82.1 148.4,67.2 150.6,52.3 152.9,42.2 155.2,55.1 157.4,67.9 159.7,80.8 162.0,93.7 164.2,106.6 166.5,119.4 168.8,132.3 171.0,145.2 173.3,158.1 175.5,170.0"/> <polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="150.6,170.0 152.9,167.8 155.2,154.9 157.4,142.0 159.7,129.1 162.0,116.3 164.2,103.4 166.5,90.5 168.8,77.6 171.0,64.8 173.3,51.9 175.5,40.8 177.8,51.9 180.1,63.0 182.3,74.1 184.6,85.2 186.9,96.3 189.1,107.3 191.4,118.4 193.7,129.5 195.9,140.6 198.2,151.7 200.5,162.8 202.7,170.0"/> <polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="173.3,170.0 175.5,169.2 177.8,158.1 180.1,147.0 182.3,135.9 184.6,124.8 186.9,113.7 189.1,102.6 191.4,91.5 193.7,80.4 195.9,69.3 198.2,58.2 200.5,47.1 202.7,43.4 205.0,52.9 207.3,62.5 209.5,72.1 211.8,81.6 214.1,91.2 216.3,100.8 218.6,110.3 220.9,119.9 223.1,129.5 225.4,139.1 227.7,148.6 229.9,158.2 232.2,167.8 234.5,170.0"/> <polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="200.5,170.0 202.7,166.6 205.0,157.0 207.3,147.5 209.5,137.9 211.8,128.3 214.1,118.8 216.3,109.2 218.6,99.6 220.9,90.0 223.1,80.5 225.4,70.9 227.7,61.3 229.9,51.8 232.2,42.2 234.5,46.3 236.7,54.5 239.0,62.8 241.2,71.0 243.5,79.3 245.8,87.5 248.0,95.8 250.3,104.0 252.6,112.3 254.8,120.5 257.1,128.8 259.4,137.0 261.6,145.3 263.9,153.5 266.2,161.8 268.4,170.0"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="232.2,170.0 234.5,163.7 236.7,155.4 239.0,147.2 241.2,138.9 243.5,130.7 245.8,122.4 248.0,114.2 250.3,105.9 252.6,97.7 254.8,89.4 257.1,81.2 259.4,72.9 261.6,64.7 263.9,56.4 266.2,48.2 268.4,40.0 270.7,47.1 273.0,54.2 275.2,61.3 277.5,68.5 279.8,75.6 282.0,82.7 284.3,89.8 286.6,96.9 288.8,104.0 291.1,111.1 293.4,118.3 295.6,125.4 297.9,132.5 300.2,139.6 302.4,146.7 304.7,153.8 307.0,160.9 309.2,168.1 311.5,170.0"/> <polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="266.2,170.0 268.4,170.0 270.7,162.8 273.0,155.7 275.2,148.6 277.5,141.5 279.8,134.4 282.0,127.3 284.3,120.2 286.6,113.0 288.8,105.9 291.1,98.8 293.4,91.7 295.6,84.6 297.9,77.5 300.2,70.4 302.4,63.2 304.7,56.1 307.0,49.0 309.2,41.9 311.5,44.4 313.8,50.6 316.0,56.7 318.3,62.8 320.5,69.0 322.8,75.1 325.1,81.2 327.3,87.4 329.6,93.5 331.9,99.6 334.1,105.8 336.4,111.9 338.7,118.0 340.9,124.2 343.2,130.3 345.5,136.4 347.7,142.6 350.0,148.7 352.3,154.8 354.5,161.0 356.8,167.1 359.1,170.0"/> <polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="309.2,170.0 311.5,165.5 313.8,159.4 316.0,153.3 318.3,147.1 320.5,141.0 322.8,134.9 325.1,128.7 327.3,122.6 329.6,116.5 331.9,110.3 334.1,104.2 336.4,98.1 338.7,91.9 340.9,85.8 343.2,79.7 345.5,73.5 347.7,67.4 350.0,61.3 352.3,55.1 354.5,49.0 356.8,42.8 359.1,42.8 361.3,48.0 363.6,53.3 365.9,58.6 368.1,63.9 370.4,69.2 372.7,74.5 374.9,79.8 377.2,85.1 379.5,90.4 381.7,95.6 384.0,100.9 386.2,106.2 388.5,111.5 390.8,116.8 393.0,122.1 395.3,127.4 397.6,132.7 399.8,138.0 402.1,143.2 404.4,148.5 406.6,153.8 408.9,159.1 411.2,164.4 413.4,169.7 415.7,170.0"/> <polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="356.8,170.0 359.1,167.2 361.3,161.9 363.6,156.6 365.9,151.3 368.1,146.0 370.4,140.8 372.7,135.5 374.9,130.2 377.2,124.9 379.5,119.6 381.7,114.3 384.0,109.0 386.2,103.7 388.5,98.4 390.8,93.2 393.0,87.9 395.3,82.6 397.6,77.3 399.8,72.0 402.1,66.7 404.4,61.4 406.6,56.1 408.9,50.8 411.2,45.6 413.4,40.3 415.7,44.3 418.0,48.8 420.2,53.4 422.5,57.9 424.8,62.5 427.0,67.1 429.3,71.6 431.6,76.2 433.8,80.7 436.1,85.3 438.4,89.9 440.6,94.4 442.9,99.0 445.2,103.5 447.4,108.1 449.7,112.7 452.0,117.2 454.2,121.8 456.5,126.3 458.8,130.9 461.0,135.5 463.3,140.0 465.5,144.6 467.8,149.1 470.1,153.7 472.3,158.3 474.6,162.8 476.9,167.4 479.1,170.0"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="413.4,170.0 415.7,165.7 418.0,161.1 420.2,156.6 422.5,152.0 424.8,147.5 427.0,142.9 429.3,138.3 431.6,133.8 433.8,129.2 436.1,124.7 438.4,120.1 440.6,115.5 442.9,111.0 445.2,106.4 447.4,101.9 449.7,97.3 452.0,92.7 454.2,88.2 456.5,83.6 458.8,79.1 461.0,74.5 463.3,69.9 465.5,65.4 467.8,60.8 470.1,56.3 472.3,51.7 474.6,47.1 476.9,42.6 479.1,41.6 481.4,45.6 483.7,49.5 485.9,53.4 488.2,57.4 490.5,61.3 492.7,65.2 495.0,69.2 497.3,73.1 499.5,77.0 501.8,81.0 504.1,84.9 506.3,88.8 508.6,92.8 510.9,96.7 513.1,100.6 515.4,104.6 517.7,108.5 519.9,112.4 522.2,116.3 524.5,120.3 526.7,124.2 529.0,128.1 531.2,132.1 533.5,136.0 535.8,139.9 538.0,143.9 540.3,147.8 542.6,151.7 544.8,155.7 547.1,159.6 549.4,163.5 551.6,167.5 553.9,170.0"/> <polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="476.9,170.0 479.1,168.3 481.4,164.4 483.7,160.5 485.9,156.5 488.2,152.6 490.5,148.7 492.7,144.7 495.0,140.8 497.3,136.9 499.5,132.9 501.8,129.0 504.1,125.1 506.3,121.1 508.6,117.2 510.9,113.3 513.1,109.3 515.4,105.4 517.7,101.5 519.9,97.5 522.2,93.6 524.5,89.7 526.7,85.7 529.0,81.8 531.2,77.9 533.5,73.9 535.8,70.0 538.0,66.1 540.3,62.2 542.6,58.2 544.8,54.3 547.1,50.4 549.4,46.4 551.6,42.5 553.9,41.2 556.2,44.6 558.4,47.9 560.7,51.3 563.0,54.7 565.2,58.1 567.5,61.5 569.8,64.9 572.0,68.3 574.3,71.7 576.6,75.1 578.8,78.5 581.1,81.8 583.4,85.2 585.6,88.6 587.9,92.0 590.2,95.4 592.4,98.8 594.7,102.2 597.0,105.6 599.2,109.0 601.5,112.4 603.8,115.8 606.0,119.1 608.3,122.5 610.5,125.9 612.8,129.3 615.1,132.7 617.3,136.1 619.6,139.5 621.9,142.9 624.1,146.3 626.4,149.7 628.7,153.0 630.9,156.4 633.2,159.8 635.5,163.2 637.7,166.6 640.0,170.0"/> <text x="54" y="44" font-size="12" text-anchor="end">1</text> <text x="54" y="174" font-size="12" text-anchor="end">0</text> <text x="60.0" y="186" font-size="12" text-anchor="middle">0k</text> <text x="132.5" y="186" font-size="12" text-anchor="middle">1k</text> <text x="205.0" y="186" font-size="12" text-anchor="middle">2k</text> <text x="277.5" y="186" font-size="12" text-anchor="middle">3k</text> <text x="350.0" y="186" font-size="12" text-anchor="middle">4k</text> <text x="422.5" y="186" font-size="12" text-anchor="middle">5k</text> <text x="495.0" y="186" font-size="12" text-anchor="middle">6k</text> <text x="567.5" y="186" font-size="12" text-anchor="middle">7k</text> <text x="640.0" y="186" font-size="12" text-anchor="middle">8k</text> <line x1="60" y1="365" x2="640" y2="365" stroke="currentColor"/> <text x="60" y="223" font-size="13">(b) Slaney mel, norm='slaney' — 면적이 같도록 높이를 낮춤</text> <polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="60.0,365.0 62.3,342.0 64.5,318.9 66.8,295.9 69.1,272.9 71.3,249.8 73.6,241.6 75.9,264.7 78.1,287.7 80.4,310.7 82.7,333.8 84.9,356.8 87.2,365.0"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="71.3,365.0 73.6,357.6 75.9,334.5 78.1,311.5 80.4,288.5 82.7,265.5 84.9,242.4 87.2,249.1 89.5,272.1 91.7,295.1 94.0,318.2 96.2,341.2 98.5,364.2 100.8,365.0"/> <polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="84.9,365.0 87.2,350.2 89.5,327.1 91.7,304.1 94.0,281.1 96.2,258.0 98.5,235.0 100.8,256.5 103.0,279.5 105.3,302.5 107.6,325.6 109.8,348.6 112.1,365.0"/> <polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="98.5,365.0 100.8,342.7 103.0,319.7 105.3,296.7 107.6,273.6 109.8,250.6 112.1,240.9 114.4,263.9 116.6,286.9 118.9,310.0 121.2,333.0 123.4,356.0 125.7,365.0"/> <polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="109.8,365.0 112.1,358.4 114.4,335.7 116.6,312.9 118.9,290.2 121.2,267.4 123.4,244.6 125.7,249.4 128.0,271.6 130.2,293.8 132.5,316.0 134.8,338.2 137.0,360.5 139.3,365.0"/> <polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="123.4,365.0 125.7,352.7 128.0,332.6 130.2,312.4 132.5,292.3 134.8,272.2 137.0,252.0 139.3,261.5 141.6,278.5 143.8,295.6 146.1,312.6 148.4,329.7 150.6,346.7 152.9,363.8 155.2,365.0"/> <polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="137.0,365.0 139.3,353.6 141.6,339.3 143.8,325.0 146.1,310.7 148.4,296.4 150.6,282.1 152.9,267.8 155.2,277.8 157.4,289.7 159.7,301.6 162.0,313.5 164.2,325.5 166.5,337.4 168.8,349.3 171.0,361.2 173.3,365.0"/> <polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="152.9,365.0 155.2,355.8 157.4,345.9 159.7,336.0 162.0,326.0 164.2,316.1 166.5,306.2 168.8,296.3 171.0,286.4 173.3,288.8 175.5,297.1 177.8,305.3 180.1,313.6 182.3,321.8 184.6,330.1 186.9,338.4 189.1,346.6 191.4,354.9 193.7,363.2 195.9,365.0"/> <polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="171.0,365.0 173.3,360.3 175.5,353.4 177.8,346.6 180.1,339.7 182.3,332.8 184.6,325.9 186.9,319.0 189.1,312.2 191.4,305.3 193.7,298.4 195.9,301.3 198.2,307.0 200.5,312.8 202.7,318.5 205.0,324.2 207.3,330.0 209.5,335.7 211.8,341.4 214.1,347.2 216.3,352.9 218.6,358.6 220.9,364.3 223.1,365.0"/> <polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="193.7,365.0 195.9,361.3 198.2,356.5 200.5,351.7 202.7,347.0 205.0,342.2 207.3,337.4 209.5,332.7 211.8,327.9 214.1,323.1 216.3,318.3 218.6,313.6 220.9,308.8 223.1,311.8 225.4,315.7 227.7,319.7 229.9,323.7 232.2,327.7 234.5,331.6 236.7,335.6 239.0,339.6 241.2,343.6 243.5,347.5 245.8,351.5 248.0,355.5 250.3,359.5 252.6,363.4 254.8,365.0"/> <polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="220.9,365.0 223.1,362.1 225.4,358.8 227.7,355.5 229.9,352.1 232.2,348.8 234.5,345.5 236.7,342.2 239.0,338.9 241.2,335.6 243.5,332.3 245.8,329.0 248.0,325.7 250.3,322.4 252.6,319.0 254.8,319.4 257.1,322.2 259.4,324.9 261.6,327.7 263.9,330.4 266.2,333.2 268.4,336.0 270.7,338.7 273.0,341.5 275.2,344.2 277.5,347.0 279.8,349.7 282.0,352.5 284.3,355.2 286.6,358.0 288.8,360.8 291.1,363.5 293.4,365.0"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="252.6,365.0 254.8,363.6 257.1,361.3 259.4,359.0 261.6,356.7 263.9,354.4 266.2,352.1 268.4,349.8 270.7,347.5 273.0,345.2 275.2,343.0 277.5,340.7 279.8,338.4 282.0,336.1 284.3,333.8 286.6,331.5 288.8,329.2 291.1,326.9 293.4,326.5 295.6,328.4 297.9,330.3 300.2,332.3 302.4,334.2 304.7,336.1 307.0,338.0 309.2,339.9 311.5,341.8 313.8,343.7 316.0,345.6 318.3,347.5 320.5,349.5 322.8,351.4 325.1,353.3 327.3,355.2 329.6,357.1 331.9,359.0 334.1,360.9 336.4,362.8 338.7,364.8 340.9,365.0"/> <polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="291.1,365.0 293.4,364.3 295.6,362.7 297.9,361.1 300.2,359.5 302.4,357.9 304.7,356.3 307.0,354.7 309.2,353.1 311.5,351.5 313.8,349.9 316.0,348.4 318.3,346.8 320.5,345.2 322.8,343.6 325.1,342.0 327.3,340.4 329.6,338.8 331.9,337.2 334.1,335.6 336.4,334.0 338.7,332.4 340.9,333.4 343.2,334.7 345.5,336.0 347.7,337.4 350.0,338.7 352.3,340.0 354.5,341.3 356.8,342.7 359.1,344.0 361.3,345.3 363.6,346.6 365.9,348.0 368.1,349.3 370.4,350.6 372.7,351.9 374.9,353.3 377.2,354.6 379.5,355.9 381.7,357.2 384.0,358.6 386.2,359.9 388.5,361.2 390.8,362.5 393.0,363.9 395.3,365.0"/> <polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="338.7,365.0 340.9,364.0 343.2,362.9 345.5,361.8 347.7,360.7 350.0,359.6 352.3,358.5 354.5,357.4 356.8,356.3 359.1,355.2 361.3,354.1 363.6,353.0 365.9,351.9 368.1,350.8 370.4,349.7 372.7,348.6 374.9,347.5 377.2,346.4 379.5,345.3 381.7,344.2 384.0,343.1 386.2,342.0 388.5,340.9 390.8,339.8 393.0,338.7 395.3,337.8 397.6,338.8 399.8,339.7 402.1,340.6 404.4,341.5 406.6,342.4 408.9,343.4 411.2,344.3 413.4,345.2 415.7,346.1 418.0,347.0 420.2,348.0 422.5,348.9 424.8,349.8 427.0,350.7 429.3,351.6 431.6,352.6 433.8,353.5 436.1,354.4 438.4,355.3 440.6,356.2 442.9,357.1 445.2,358.1 447.4,359.0 449.7,359.9 452.0,360.8 454.2,361.7 456.5,362.7 458.8,363.6 461.0,364.5 463.3,365.0"/> <polyline fill="none" stroke="#4a7bd0" stroke-width="1.4" points="393.0,365.0 395.3,364.9 397.6,364.1 399.8,363.4 402.1,362.6 404.4,361.8 406.6,361.1 408.9,360.3 411.2,359.5 413.4,358.8 415.7,358.0 418.0,357.2 420.2,356.5 422.5,355.7 424.8,354.9 427.0,354.2 429.3,353.4 431.6,352.6 433.8,351.9 436.1,351.1 438.4,350.3 440.6,349.6 442.9,348.8 445.2,348.0 447.4,347.3 449.7,346.5 452.0,345.7 454.2,345.0 456.5,344.2 458.8,343.5 461.0,342.7 463.3,342.6 465.5,343.2 467.8,343.8 470.1,344.5 472.3,345.1 474.6,345.8 476.9,346.4 479.1,347.0 481.4,347.7 483.7,348.3 485.9,348.9 488.2,349.6 490.5,350.2 492.7,350.9 495.0,351.5 497.3,352.1 499.5,352.8 501.8,353.4 504.1,354.0 506.3,354.7 508.6,355.3 510.9,356.0 513.1,356.6 515.4,357.2 517.7,357.9 519.9,358.5 522.2,359.1 524.5,359.8 526.7,360.4 529.0,361.1 531.2,361.7 533.5,362.3 535.8,363.0 538.0,363.6 540.3,364.2 542.6,364.9 544.8,365.0"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="1.4" points="461.0,365.0 463.3,364.8 465.5,364.2 467.8,363.7 470.1,363.2 472.3,362.6 474.6,362.1 476.9,361.6 479.1,361.0 481.4,360.5 483.7,360.0 485.9,359.4 488.2,358.9 490.5,358.4 492.7,357.9 495.0,357.3 497.3,356.8 499.5,356.3 501.8,355.7 504.1,355.2 506.3,354.7 508.6,354.1 510.9,353.6 513.1,353.1 515.4,352.5 517.7,352.0 519.9,351.5 522.2,351.0 524.5,350.4 526.7,349.9 529.0,349.4 531.2,348.8 533.5,348.3 535.8,347.8 538.0,347.2 540.3,346.7 542.6,346.2 544.8,346.4 547.1,346.9 549.4,347.3 551.6,347.8 553.9,348.2 556.2,348.6 558.4,349.1 560.7,349.5 563.0,350.0 565.2,350.4 567.5,350.9 569.8,351.3 572.0,351.7 574.3,352.2 576.6,352.6 578.8,353.1 581.1,353.5 583.4,353.9 585.6,354.4 587.9,354.8 590.2,355.3 592.4,355.7 594.7,356.2 597.0,356.6 599.2,357.0 601.5,357.5 603.8,357.9 606.0,358.4 608.3,358.8 610.5,359.3 612.8,359.7 615.1,360.1 617.3,360.6 619.6,361.0 621.9,361.5 624.1,361.9 626.4,362.3 628.7,362.8 630.9,363.2 633.2,363.7 635.5,364.1 637.7,364.6 640.0,365.0"/> <text x="54" y="239" font-size="12" text-anchor="end">0.0056</text> <text x="54" y="369" font-size="12" text-anchor="end">0</text> <text x="60.0" y="381" font-size="12" text-anchor="middle">0k</text> <text x="132.5" y="381" font-size="12" text-anchor="middle">1k</text> <text x="205.0" y="381" font-size="12" text-anchor="middle">2k</text> <text x="277.5" y="381" font-size="12" text-anchor="middle">3k</text> <text x="350.0" y="381" font-size="12" text-anchor="middle">4k</text> <text x="422.5" y="381" font-size="12" text-anchor="middle">5k</text> <text x="495.0" y="381" font-size="12" text-anchor="middle">6k</text> <text x="567.5" y="381" font-size="12" text-anchor="middle">7k</text> <text x="640.0" y="381" font-size="12" text-anchor="middle">8k</text> <text x="350" y="394" font-size="12" text-anchor="middle">주파수 (Hz) — 16개 필터, 512점 FFT bin(31.25 Hz)에서 실제로 계산한 가중치</text>
</svg>
```

그림 7 — 16개 mel 필터를 512점 FFT bin에서 실제로 계산한 가중치(torchaudio). (a) HTK, 정규화 없음: 꼭짓점 높이가 1 근처, 저주파 삼각형은 bin 몇 개짜리라 뾰족하게 깎였다. (b) Slaney, 면적 정규화: 넓은 고주파 필터일수록 낮다.

**n_mels를 너무 크게 잡으면**: n_fft = 512에서 n_mels = 128이면 저주파 필터 폭이 bin 간격보다 좁아져 **비어 있는 필터**(모든 가중치 0)가 생긴다. torchaudio는 "At least one mel filterbank has all zero values" 경고를 낸다. 그 band의 log 값은 항상 log(ε)라 모델에 쓸모없는 상수 채널이 된다. 해결: n_fft를 키우거나, n_mels를 줄이거나, f_min을 올린다.

### 7.2 삼각 필터 곱 = 행렬곱, 그리고 sparse

mel 에너지 = power spectrum [T, 257] × 필터뱅크 [257, 40]. 하지만 각 bin은 이웃한 필터 최대 2개에만 속하므로 행렬의 대부분이 0이다. 8.1절에서 세어 보면 40-mel HTK는 0이 아닌 가중치가 493개뿐(10,280개 중 4.8%). 펌웨어에서는 필터마다 (시작 bin, 길이, 가중치 배열)로 저장한다(8.2절 C 코드).

### 7.3 log 압축 — log(x + ε) vs dB

mel 에너지는 무음과 큰 소리 사이에 10⁶배 이상 차이가 난다. log로 눌러 "곱셈 차이 → 덧셈 차이"로 바꾼다. 마이크 gain이 2배가 되면 log-mel 전체가 ln 4 = 1.39만큼 평행이동할 뿐이다 — 이 성질을 7.6절 정규화가 이용한다.

이 코드는 ε 값에 따라 작은 에너지가 어떻게 바닥에 깔리는지, dB와 `top_db` 클리핑, torchaudio `AmplitudeToDB`를 비교한다.

```python
import numpy as np, torch, torchaudio
E = np.array([1e-12, 1e-8, 1e-4, 1e-2, 1.0, 100.0])      # mel band 에너지 (power) 예시
for eps in [1e-10, 1e-6, 1e-3]:
    print(f"ln(E+{eps:g}) :", np.round(np.log(E + eps), 2))
db = 10 * np.log10(np.maximum(E, 1e-10))                  # power → dB (amin=1e-10)
print("dB          :", np.round(db, 1))
print("dB top_db=80:", np.round(np.maximum(db, db.max() - 80), 1))   # 최대값보다 80 dB 아래는 잘라낸다
ta = torchaudio.transforms.AmplitudeToDB(stype="power", top_db=80)(torch.tensor(E).view(1, -1, 1))   # [ch, n_mels, T] 모양이어야 한다
print("torchaudio  :", np.round(ta.flatten().numpy(), 1))
print("dB = ln × 10/ln(10) = ln ×", round(10 / np.log(10), 4))
```

```text
ln(E+1e-10) : [-23.02 -18.41  -9.21  -4.61   0.     4.61]
ln(E+1e-06) : [-13.82 -13.81  -9.2   -4.61   0.     4.61]
ln(E+0.001) : [-6.91 -6.91 -6.81 -4.51  0.    4.61]
dB          : [-100.  -80.  -40.  -20.    0.   20.]
dB top_db=80: [-60. -60. -40. -20.   0.  20.]
torchaudio  : [-60. -60. -40. -20.   0.  20.]
dB = ln × 10/ln(10) = ln × 4.3429
```

출력에서 볼 것:

- ε = 1e-10이면 1e-12 에너지가 −23, ε = 1e-6이면 1e-12와 1e-8이 둘 다 −13.8로 **같아진다**. ε는 "이보다 작은 에너지는 구분하지 않겠다"는 **바닥(floor)** 이다. 학습과 기기에서 ε가 다르면 조용한 구간의 특징값이 통째로 달라진다 — VAD·wake word의 오탐률에 직접 영향을 준다.
- dB = 10·log10(power)이고 ln과는 10/ln10 = 4.34배 차이뿐이다. 모델은 스케일만 다른 같은 정보로 학습하지만, **학습과 기기가 같은 밑을 써야** 한다.
- `top_db = 80`은 "spectrogram 최대값보다 80 dB 낮은 값은 잘라라" — **입력 전체의 최대값**에 의존하므로 스트리밍(frame 단위)에서는 재현하기 어렵다. torchaudio `AmplitudeToDB`가 `[..., freq, time]` 모양 입력을 요구하는 이유도 이 최대값을 spectrogram 단위로 구하기 때문이다(1-D 텐서를 넣으면 에러가 났다). 기기용 모델이라면 학습 때 `top_db=None` + 고정 ε를 쓰는 것이 안전하다.

### 7.4 MFCC — DCT-II와 liftering

**MFCC**는 log-mel에 **DCT-II**(이산 코사인 변환)를 하고 앞쪽 K개만 남긴 것이다.

```
c[k] = α_k · Σ_{m=0}^{M−1} logmel[m] · cos( π·k·(2m + 1) / (2M) ),   k = 0 … K−1
α_0 = √(1/M),  α_k = √(2/M)  (norm="ortho")
```

말로 하면: log-mel 40개를 "코사인 모양 40개"의 합으로 다시 쓰고, 천천히 변하는 모양(낮은 k)만 남긴다. c[0]은 전체 에너지(평균 레벨), c[1]은 저주파 vs 고주파 기울기(spectral tilt), 더 높은 k는 더 세밀한 굴곡(formant)이다. 이웃 mel band끼리 강하게 상관된 것을 풀어 주고(decorrelation) 적은 수로 요약한다. "cepstrum"은 spectrum의 철자를 뒤집은 말로, "스펙트럼의 스펙트럼"이라는 뜻이다.

이 코드는 DCT-II 행렬을 직접 만들어 `scipy.fft.dct`와 비교하고, 앞쪽 계수에 에너지가 얼마나 모이는지, torchaudio MFCC가 무엇을 하는지, HTK liftering 가중치를 확인한다.

```python
import numpy as np, torch, torchaudio
from scipy.fft import dct
M, K = 40, 13                                        # log-mel 40개 → MFCC 13개
m = np.arange(M); k = np.arange(K)[:, None]
D = np.sqrt(2 / M) * np.cos(np.pi * k * (2 * m + 1) / (2 * M))   # DCT-II 행렬 [K, M]
D[0] /= np.sqrt(2)                                   # 'ortho' 정규화: 0번 행만 1/√M
rng = np.random.default_rng(5)
logmel = np.cumsum(rng.standard_normal(M)) * 0.3     # 이웃끼리 상관된 매끈한 곡선 하나
c_mat = D @ logmel
c_scipy = dct(logmel, type=2, norm="ortho")[:K]
print("matrix vs scipy dct max|diff|:", np.abs(c_mat - c_scipy).max())
energy = np.cumsum(dct(logmel, type=2, norm="ortho") ** 2) / np.sum(logmel ** 2)
print("energy kept in first 5/13 coeffs: %.3f / %.3f" % (energy[4], energy[12]))
# torchaudio MFCC = power mel → dB(top_db=80) → DCT-II ortho
x = torch.randn(16000, generator=torch.Generator().manual_seed(0))
mk = dict(n_fft=512, win_length=400, hop_length=160, n_mels=40, center=False)
mfcc_ta = torchaudio.transforms.MFCC(16000, n_mfcc=13, melkwargs=mk)(x)          # [13, T]
mel = torchaudio.transforms.MelSpectrogram(16000, **mk)(x)                        # [40, T]
db = torchaudio.transforms.AmplitudeToDB("power", top_db=80)(mel.unsqueeze(0))[0]
mine = dct(db.numpy().T, type=2, norm="ortho", axis=1)[:, :13].T
print("torchaudio MFCC vs dB + scipy dct max|diff|:", float(np.abs(mfcc_ta.numpy() - mine).max()))
L = 22                                               # HTK 기본 lifter 파라미터
lifter = 1 + (L / 2) * np.sin(np.pi * np.arange(K) / L)
print("lifter weights:", np.round(lifter, 2))
```

```text
matrix vs scipy dct max|diff|: 3.552713678800501e-15
energy kept in first 5/13 coeffs: 0.985 / 0.991
torchaudio MFCC vs dB + scipy dct max|diff|: 6.604194641113281e-05
lifter weights: [ 1.    2.57  4.1   5.57  6.95  8.2   9.31 10.25 11.01 11.55 11.89 12.
 11.89]
```

출력에서 볼 것:

- 직접 만든 DCT 행렬 = `scipy.fft.dct(type=2, norm="ortho")`. 펌웨어에서는 이 [13, 40] 행렬(또는 [10, 40])을 ROM 상수로 박고 MAC 520번이면 된다.
- 매끈한(상관된) 곡선은 앞 5개 계수에 에너지의 98.5%가 모인다 — 압축이 되는 이유.
- `torchaudio.transforms.MFCC`는 기본으로 **log가 아니라 dB(top_db=80)** 를 쓴 뒤 DCT를 한다(`log_mels=False` 기본). dB + scipy dct로 재현하면 7e-5 안에서 맞는다. "MFCC"라는 이름만 같고 라이브러리마다 log 종류·DCT 정규화·c0 처리·lifter가 다르다.
- **liftering**: c'[k] = c[k]·(1 + (L/2)·sin(πk/L)). HTK 기본 L = 22이고 가중치가 1에서 12까지 커진다. 크기가 작은 고차 계수를 키워 계수들의 크기를 비슷하게 맞추는 용도였다. 지금은 뒤에 정규화(7.6절)를 하므로 큰 의미는 없지만, 레거시 모델을 이식할 때는 들어갔는지 꼭 확인한다.

log-mel vs MFCC 선택: 큰 CNN/Transformer는 log-mel(정보 손실 없음, 2D 구조 유지)을 쓰고, 아주 작은 KWS(B5 3.2 DS-CNN의 49 × 10 입력)나 고전 GMM 모델은 MFCC를 쓴다. MFCC는 DCT 한 번(400~520 MAC)이 더 들지만 모델 입력이 4배 작아진다.

### 7.5 PCEN — 학습 가능한 "자동 이득 조절"

**PCEN(Per-Channel Energy Normalization)** 은 log 대신 쓰는 압축이다(Wang et al., ICASSP 2017, far-field KWS 논문). 각 mel 채널마다 최근 에너지의 이동 평균 M으로 나눠 **배경 레벨을 자동으로 맞춘다**(AGC).

```
M[t] = (1 − s)·M[t−1] + s·E[t]                     ← 1-pole IIR smoother (2.5절 leaky integrator)
PCEN[t] = ( E[t] / (ε + M[t])^α + δ )^r − δ^r      흔한 값: α=0.98, δ=2, r=0.5, s≈0.025 (100 frame/s에서 시정수 약 0.4 s)
```

말로 하면: 지금 에너지를 "최근 평균 에너지"로 나눠 상대적 크기만 남기고(α ≈ 1이면 gain에 거의 무관), 그 결과를 제곱근 같은 완만한 압축(r)으로 누른다. 원래 논문은 α, δ, r, s를 학습 가능한 파라미터로 둔다.

이 코드는 배경 잡음 + burst 신호를 gain 1배와 100배(20 dB)로 넣었을 때 log-mel, PCEN, 발화 단위 CMVN이 각각 어떻게 반응하는지 본다.

```python
import numpy as np
rng = np.random.default_rng(6)
T, M = 300, 40
E = rng.gamma(2.0, 1e-4, size=(T, M))                 # 배경 잡음 에너지 [T, n_mels]
E[100:140, 5:15] += 5e-2                              # 말소리 같은 burst
def pcen(E, s=0.025, alpha=0.98, delta=2.0, r=0.5, eps=1e-6):
    Msm = np.empty_like(E); Msm[0] = E[0]
    for t in range(1, len(E)):                        # 1차 IIR smoother (B3의 leaky integrator)
        Msm[t] = (1 - s) * Msm[t - 1] + s * E[t]
    return (E / (eps + Msm) ** alpha + delta) ** r - delta ** r
def cmvn(F): return (F - F.mean(0)) / (F.std(0) + 1e-8)  # 발화 단위 mean/var 정규화 (band별)
for g in [1.0, 100.0]:                                # 마이크 gain 20 dB 차이 (power 100배)
    lm = np.log(g * E + 1e-6); pc = pcen(g * E); cm = cmvn(np.log(g * E + 1e-6))
    print(f"gain x{g:5.0f}: log-mel mean {lm.mean():6.2f} | PCEN mean(t>=50) {pc[50:].mean():.4f}"
          f" | CMVN mean {cm.mean():+.3f} std {cm.std():.3f} | burst/bg PCEN {pc[100:140,5:15].mean()/pc[50:100,5:15].mean():.1f}")
d = np.abs(pcen(100 * E)[50:] - pcen(E)[50:]).max()
print("PCEN max |diff| between gains (t>=50): %.4f" % d)
c1, c2 = cmvn(np.log(E + 1e-6)), cmvn(np.log(100 * E + 1e-6))
print("CMVN  max |diff| between gains: %.4f" % np.abs(c1 - c2).max())
```

```text
gain x    1: log-mel mean  -8.59 | PCEN mean(t>=50) 0.2506 | CMVN mean +0.000 std 1.000 | burst/bg PCEN 3.4
gain x  100: log-mel mean  -3.99 | PCEN mean(t>=50) 0.2728 | CMVN mean -0.000 std 1.000 | burst/bg PCEN 3.4
PCEN max |diff| between gains (t>=50): 0.2564
CMVN  max |diff| between gains: 0.5729
```

출력에서 볼 것:

- log-mel 평균은 gain 100배에 −8.59 → −3.99로 4.6(= ln 100) 이동한다. 모델 입장에서는 완전히 다른 입력이다.
- PCEN 평균은 0.25 → 0.27로 거의 그대로, burst 대 배경 비율은 둘 다 3.4. α = 0.98이 1보다 약간 작아 완벽한 불변은 아니다.
- CMVN(7.6절)도 평균 0, 표준편차 1로 맞춰 주지만 두 gain 결과가 최대 0.57 다르다 — ε = 1e-6이 작은 배경 에너지(약 2e-4)와 비교해 무시할 수 없어서 log(gE + ε)가 정확한 평행이동이 아니기 때문이다. ε가 특징에 주는 영향이 여기서도 보인다.

임베디드 관점: PCEN은 frame마다 채널당 곱 몇 번 + 거듭제곱 2번(pow는 비싸므로 r = 0.5면 sqrt로, α는 exp/log 근사 또는 LUT). **상태(M)를 가진 스트리밍 연산**이라 리셋 정책(기기 부팅 직후, 마이크 재시작 후 처음 몇 초)이 학습 데이터와 맞아야 한다.

### 7.6 특징 정규화 — μ, σ는 어디서 오나

모델 입력은 대개 평균 0, 분산 1 근처로 맞춘다(A2 4절의 표준화, A6의 데이터 파이프라인).

| 방식 | 계산 | 장점 | 기기에서의 문제 |
|---|---|---|---|
| 전역(global) 정규화 | 학습 세트 전체의 band별 μ, σ를 상수로 | 스트리밍 가능, 구현 쉬움 | 마이크·gain이 학습과 다르면 이동 |
| 발화 단위 CMVN | 한 발화 전체의 band별 μ, σ | gain·채널 차이 제거 | **발화가 끝나야** μ를 앎 → always-on에 부적합 |
| 이동 평균 CMVN | μ[t] = (1−s)·μ[t−1] + s·x[t] | 스트리밍 가능, 적응 | 초기 수 초 동안 불안정, 학습도 같은 방식이어야 |
| PCEN | 7.5절 | 압축과 AGC를 한 번에 | 파라미터·상태 일치 필요 |

핵심 규칙(A2와 같다): **학습 때 쓴 정규화 방식과 상수를 기기에 그대로 옮긴다.** 학습은 발화 단위 CMVN, 기기는 전역 μ, σ로 하면 정확도가 "조금" 떨어지는 형태로 나타나 찾기 어렵다. INT8 모델이라면 정규화와 입력 양자화(scale, zero-point, C1)를 한 번의 affine 변환으로 합쳐 버릴 수 있다: q = round((x − μ)/(σ·scale)) + zp.

---

## 8. 임베디드 feature 파이프라인 — 비용과 스트리밍 구현

### 8.1 frame당 연산량 세기

설정: 16 kHz, 25 ms 창(400), 10 ms hop(160), 512점 FFT, 40 mel, 10 MFCC. 손으로 먼저:

```
window 곱                 400 곱
512점 real FFT            256점 복소 FFT: (256/2)·log2(256) = 1024 butterfly
                          butterfly당 복소 곱 1 (곱 4, 덧셈 2) + 복소 덧셈 2 (덧셈 4)
                          → 곱 4096, 덧셈 6144  + real 후처리 256 bin 분
power |X|²                257 × (곱 2, 덧셈 1)
mel (sparse)              0이 아닌 가중치 수만큼 MAC
log                       40번
DCT 40 → 10               400 MAC
frame rate                16000 / 160 = 100 frame/s
```

이 코드는 위 항목을 실제 필터뱅크의 nonzero 수로 세고, 메모리와 frame당 deadline을 계산한다.

```python
import numpy as np, torchaudio.functional as AF
fs, win, hop, nfft, n_mels, n_mfcc = 16000, 400, 160, 512, 40, 10
fb = AF.melscale_fbanks(nfft // 2 + 1, 0.0, fs / 2, n_mels, fs, norm=None, mel_scale="htk").numpy()
nnz = int((fb > 0).sum())
Nc = nfft // 2                                         # real FFT = 256점 복소 FFT + 후처리
bfly = Nc // 2 * int(np.log2(Nc))                      # radix-2 butterfly 수
ops = {                                                # (곱셈, 덧셈) 대략치
    "window (400)":              (win, 0),
    "256-pt cFFT (butterflies)": (4 * bfly, 6 * bfly),
    "real-FFT post-process":     (4 * Nc, 8 * Nc),
    "power |X|^2 (257)":         (2 * (Nc + 1), Nc + 1),
    f"mel (nnz={nnz})":          (nnz, nnz),
    "log (40)":                  (0, 0),
    "DCT 40->10":                (n_mels * n_mfcc, n_mels * n_mfcc),
}
tot_m = sum(v[0] for v in ops.values()); tot_a = sum(v[1] for v in ops.values())
for k, (m, a) in ops.items(): print(f"{k:28s} mul {m:6d}  add {a:6d}")
print(f"{'total per frame':28s} mul {tot_m:6d}  add {tot_a:6d}   (+ log 40회)")
fps = fs // hop
print(f"frames/s = {fps}, total ≈ {(tot_m + tot_a) * fps / 1e6:.2f} M flop/s; dense mel would be {257*40} MAC vs nnz {nnz}")
mem = {"window": win * 4, "twiddles(256 complex)": Nc * 8, "fft buffer(512 f32)": nfft * 4,
       "mel weights + idx": nnz * 4 + n_mels * 4, "ring buffer (400 int16)": win * 2, "DCT 10x40": 400 * 4}
print("RAM/ROM bytes:", mem, "total", sum(mem.values()))
print("deadline per hop = %.1f ms; at 64 MHz that is %d cycles" % (hop / fs * 1e3, 64e6 * hop / fs))
```

```text
window (400)                 mul    400  add      0
256-pt cFFT (butterflies)    mul   4096  add   6144
real-FFT post-process        mul   1024  add   2048
power |X|^2 (257)            mul    514  add    257
mel (nnz=493)                mul    493  add    493
log (40)                     mul      0  add      0
DCT 40->10                   mul    400  add    400
total per frame              mul   6927  add   9342   (+ log 40회)
frames/s = 100, total ≈ 1.63 M flop/s; dense mel would be 10280 MAC vs nnz 493
RAM/ROM bytes: {'window': 1600, 'twiddles(256 complex)': 2048, 'fft buffer(512 f32)': 2048, 'mel weights + idx': 2132, 'ring buffer (400 int16)': 800, 'DCT 10x40': 1600} total 10228
deadline per hop = 10.0 ms; at 64 MHz that is 640000 cycles
```

출력에서 볼 것:

- frame당 곱 약 6,900, 덧셈 약 9,300 + log 40번. 초당 약 1.6 M flop. 지배적인 것은 FFT(약 2/3)다.
- mel을 dense 행렬로 하면 10,280 MAC인데 sparse로는 493 — **20배 차이**. dense로 하면 FFT보다 mel이 더 비싸진다.
- 상수 테이블·버퍼를 다 합쳐 약 10 KB. Cortex-M급 SRAM에도 충분히 들어간다.
- deadline은 hop = 10 ms. 64 MHz 코어라면 64만 cycle 예산이다. FPU가 있는 Cortex-M4F에서 CMSIS-DSP로 이 계산은 예산의 작은 일부일 것으로 예상하지만, 실제 cycle은 컴파일러·메모리 대기·log 구현에 따라 다르므로 DWT `CYCCNT`로 **측정해야 한다**(D6). E4 9.1절에서 본 것처럼 front-end는 보통 뒤의 NN보다 훨씬 싸다.

### 8.2 스트리밍 C 구현 — 링버퍼에서 frame 단위로 log-mel

기기에서는 1초짜리 배열이 없다. DMA가 블록(예: 1 ms = 16 샘플, 또는 코덱 설정에 따라 다른 크기)씩 넣어 주고, 펌웨어는 **hop만큼 새 샘플이 모일 때마다** frame 하나를 계산한다. 블록 크기와 hop이 맞을 필요는 없다.

이 C 코드는 입력을 37 샘플(일부러 hop과 안 맞는 크기)씩 밀어 넣으면서 원형 버퍼에서 400 샘플 창을 꺼내 log-mel을 계산한다. 5.7절의 `fft.h`를 쓰고, mel 필터는 C 안에서 HTK 공식으로 sparse하게 만든다.

```c
#include <stdio.h>
#include <string.h>
#include "fft.h"
#define FS 16000
#define WIN 400          /* 25 ms */
#define HOP 160          /* 10 ms */
#define NFFT 512
#define NBIN (NFFT / 2 + 1)
#define NMEL 40
#define CHUNK 37         /* DMA 블록 크기 — 일부러 hop과 안 맞춘다 */
static float ring[WIN]; static int wpos = 0; static long total = 0;   /* wpos = 가장 오래된 샘플 위치 */
static float win_tab[WIN]; static cf tw[NFFT / 2], buf[NFFT];
static int mel_lo[NMEL], mel_n[NMEL]; static float mel_w[NMEL][NBIN];  /* sparse: 시작 bin, 길이, 가중치 */
static double hz2mel(double f) { return 2595.0 * log10(1.0 + f / 700.0); }
static double mel2hz(double m) { return 700.0 * (pow(10.0, m / 2595.0) - 1.0); }
static void setup(void) {
    for (int n = 0; n < WIN; n++) win_tab[n] = (float)(0.5 - 0.5 * cos(2 * PI_D * n / WIN));  /* periodic Hann */
    fft_init(tw, NFFT);
    double hz[NMEL + 2], top = hz2mel(FS / 2.0);
    for (int i = 0; i < NMEL + 2; i++) hz[i] = mel2hz(top * i / (NMEL + 1));
    for (int m = 0; m < NMEL; m++) {
        mel_lo[m] = -1; mel_n[m] = 0;
        for (int k = 0; k < NBIN; k++) {
            double f = (double)k * FS / NFFT;
            double up = (f - hz[m]) / (hz[m + 1] - hz[m]), dn = (hz[m + 2] - f) / (hz[m + 2] - hz[m + 1]);
            double w = up < dn ? up : dn;
            if (w > 0) { if (mel_lo[m] < 0) mel_lo[m] = k; mel_w[m][mel_n[m]++] = (float)w; }
        }
    }
}
static void compute_frame(float *out) {           /* ring의 최근 WIN 샘플 → log-mel NMEL개 */
    for (int n = 0; n < WIN; n++) buf[n] = ring[(wpos + n) % WIN] * win_tab[n];  /* 오래된 것부터 */
    for (int n = WIN; n < NFFT; n++) buf[n] = 0;                                  /* zero-pad */
    fft_radix2(buf, tw, NFFT);
    float pw[NBIN];
    for (int k = 0; k < NBIN; k++) pw[k] = crealf(buf[k]) * crealf(buf[k]) + cimagf(buf[k]) * cimagf(buf[k]);
    for (int m = 0; m < NMEL; m++) {
        float acc = 0;
        for (int j = 0; j < mel_n[m]; j++) acc += mel_w[m][j] * pw[mel_lo[m] + j];
        out[m] = logf(acc + 1e-6f);
    }
}
static int push(const float *x, int n, float (*out)[NMEL], int nout) {   /* DMA 블록 하나 처리 */
    for (int i = 0; i < n; i++) {
        ring[wpos] = x[i]; wpos = (wpos + 1) % WIN; total++;      /* 원형 버퍼에 쓰기 */
        if (total >= WIN && (total - WIN) % HOP == 0)             /* hop마다 frame 하나 */
            compute_frame(out[nout++]);
    }
    return nout;
}
int main(void) {
    static float x[FS], out[200][NMEL];
    FILE *f = fopen("xs.f32", "rb"); size_t r = fread(x, 4, FS, f); fclose(f);
    setup();
    int nout = 0;
    for (int i = 0; i < FS; i += CHUNK) nout = push(&x[i], (FS - i < CHUNK) ? FS - i : CHUNK, out, nout);
    f = fopen("logmel_c.f32", "wb"); fwrite(out, sizeof(float) * NMEL, nout, f); fclose(f);
    printf("read %zu samples in chunks of %d -> %d frames x %d mels\n", r, CHUNK, nout, NMEL);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 logmel_stream.c -o logmel_stream -lm     # 경고 0개
```

이 Python 코드는 시험 신호(0.5초 440 Hz → 0.5초 2500 Hz + 잡음)를 만들어 C에 넘기고, numpy **배치** 계산(같은 frame 위치, periodic Hann, 512점 rfft, torchaudio HTK 필터뱅크, ln(x + 1e-6))과 비교한다.

```python
import numpy as np, subprocess, torchaudio.functional as AF
fs = 16000; t = np.arange(fs) / fs
rng = np.random.default_rng(7)
x = (0.3 * np.sin(2 * np.pi * 440 * t) * (t < 0.5) + 0.2 * np.sin(2 * np.pi * 2500 * t) * (t >= 0.5)
     + 0.01 * rng.standard_normal(fs)).astype(np.float32)
x.tofile("xs.f32")
print(subprocess.run(["./logmel_stream"], capture_output=True, text=True).stdout, end="")
c = np.fromfile("logmel_c.f32", np.float32).reshape(-1, 40)
# numpy 배치 기준: 같은 frame 위치, periodic Hann 400, 512점 rfft, HTK mel(norm=None), ln(x+1e-6)
T = 1 + (fs - 400) // 160
frames = np.stack([x[i*160 : i*160 + 400] for i in range(T)]).astype(np.float64)
hann = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(400) / 400)
pw = np.abs(np.fft.rfft(frames * hann, n=512)) ** 2
fb = AF.melscale_fbanks(257, 0.0, 8000.0, 40, fs, norm=None, mel_scale="htk").numpy().astype(np.float64)
ref = np.log(pw @ fb + 1e-6)
print("frames C/numpy:", c.shape[0], T, "  max |diff| = %.2e   mean |diff| = %.2e" % (np.abs(c - ref).max(), np.abs(c - ref).mean()))
print("frame 10 mel[0:5] C    :", np.round(c[10, :5], 4))
print("frame 10 mel[0:5] numpy:", np.round(ref[10, :5], 4))
```

```text
read 16000 samples in chunks of 37 -> 98 frames x 40 mels
frames C/numpy: 98 98   max |diff| = 2.92e-05   mean |diff| = 1.46e-06
frame 10 mel[0:5] C    : [-3.8309 -3.8597 -3.6155 -4.2451 -3.5129]
frame 10 mel[0:5] numpy: [-3.8309 -3.8597 -3.6155 -4.2451 -3.5129]
```

출력에서 볼 것: frame 수가 98로 같고(1 + ⌊(16000 − 400)/160⌋), log-mel 최대 차이 2.9e-5, 평균 1.5e-6. float32 FFT와 float64 numpy의 차이가 log 영역에서 이 정도다. 이 숫자가 **golden vector 허용 오차**를 정하는 근거가 된다 — 예를 들어 "float 구현은 log-mel 1e-4 이내, 고정소수점 구현은 0.05 이내" 같은 식. 블록 크기(37)와 hop(160)이 안 맞아도 결과가 같다는 것이 스트리밍 구현의 핵심 시험이다.

구현에서 짚을 점:

- **wpos 하나로 끝나는 원형 버퍼**: 크기가 창 길이(400)와 같으면 다음에 쓸 위치 = 가장 오래된 샘플 위치다. frame을 꺼낼 때 `(wpos + n) % WIN`으로 오래된 것부터 읽는다. `%`가 아까우면 버퍼를 2배로 잡고 각 샘플을 두 곳에 써서(mirror) 항상 연속 400개를 읽게 하거나, DSP의 modulo addressing(E4 3절)을 쓴다.
- **frame 판정은 샘플 카운터로**: `total >= WIN && (total − WIN) % HOP == 0`. 블록 경계와 무관하다.
- **compute_frame을 ISR에서 하지 말 것**: DMA 완료 ISR은 링버퍼 쓰기만 하고, frame 계산은 태스크/메인 루프에서 한다. 계산이 10 ms를 넘으면 frame이 밀리므로 E4 3.5절의 ping-pong + 처리 시간 측정이 필요하다.
- 실제 기기라면 FFT를 `arm_rfft_fast_f32`로 바꾸고(real FFT로 절반), 입력은 int16 → float 변환을 window 곱과 합친다.

### 8.3 고정소수점으로 간다면

FPU가 없는 코어(Cortex-M0+, 일부 RISC-V)나 고정소수점 벡터 DSP에서는 전 구간을 정수로 해야 한다. 단계별 주의점:

| 단계 | 형식 예 | 주의 |
|---|---|---|
| 입력 | int16 (Q15) | 마이크 gain으로 dynamic range 확보 (1.5절) |
| window 곱 | Q15 × Q15 → Q15 | 반올림·포화 (E4 2.2) |
| FFT | Q15/Q31, 단계별 1/2 스케일 또는 BFP | 작은 신호의 비트 손실, 출력 스케일 기록 (5.9절) |
| power | Q15² → Q30을 32-bit, 또는 64-bit | re² + im² 덧셈 overflow |
| mel | 32-bit 가중치 × 32-bit power → 64-bit 누산 | 가중치 Q15로도 대개 충분 |
| log | log2 = (최상위 비트 위치) + LUT(가수) | `__builtin_clz` + 32~256 엔트리 표 + 선형 보간, ln = log2 × ln2 |
| 스케일 보정 | log 영역에서 상수 덧셈 | FFT 스케일 2^e는 log에서 e·ln2를 더하면 복원 |
| 모델 입력 | int8 (scale, zero-point) | 정규화와 합쳐 affine 한 번 (7.6절) |

log 영역의 좋은 점: 앞 단계에서 생긴 **곱셈 스케일 오차가 전부 덧셈 상수**가 된다. 그래서 FFT 단계별 시프트 횟수만 정확히 기록하면, 마지막에 상수 하나 더해서 float 결과와 맞출 수 있다. 검증은 8.2절처럼 같은 wav를 float numpy 기준값과 frame별 diff로 한다.

### 8.4 지연(latency) 예산

frame 하나가 나오려면 창 25 ms가 다 차야 한다. 여기에 계산 시간, 모델이 보는 문맥(예: 1초 = 98 frame), 후처리 smoothing이 더해진다. wake word "응답성"을 말할 때 front-end 지연은 25 ms + 계산 시간이고, 대부분은 모델 문맥 길이와 smoothing(B5 3.4)에서 온다. 선형 위상 FIR을 앞에 붙이면 (N−1)/2 샘플이 더해진다(3.4절).

---

## 9. IMU 특징도 DSP다

### 9.1 무엇을 뽑나

IMU(가속도·자이로) 모델(B7)은 raw 샘플을 직접 쓰기도 하지만, 작은 모델이나 고전 분류기(결정 트리, ST MLC 같은 센서 내장 ML core)는 **손으로 설계한 특징**을 먹는다. 전부 이 노트의 DSP 도구다.

| 특징 | 계산 | DSP 관점 | 쓰임 |
|---|---|---|---|
| 평균, 중력 성분 | LPF(예: 1-pole IIR, 0.3 Hz) | 2.5절 leaky integrator | 자세, 착용 방향 |
| RMS / 에너지 | √(Σx²/N), DC 제거 후 | Parseval: 시간 에너지 = 주파수 에너지 | 활동 강도, wake-on-motion |
| zero-crossing rate | 부호 바뀐 횟수 / 시간 | 주파수의 싼 근사, 잡음에 약함 | 진동, 탭 |
| 자기상관 peak | r[lag] = Σ x[n]·x[n+lag] | 주기 검출(pitch 검출과 같은 원리) | 걸음 주파수, 반복 동작 |
| band energy | FFT 후 band별 합, 또는 IIR band-pass + RMS | 5절 FFT, 4절 biquad | 걷기/뛰기/떨림 구분 |
| peak-to-peak, 첨도 등 통계 | 창 안의 통계 | A2 | 낙상, 충격 |

### 9.2 코드: 걸음 신호에서 특징 뽑기

이 코드는 50 Hz IMU의 가속도 크기(1.8 걸음/s 기본파 + 2배음 + 잡음)에서 RMS, zero-crossing, 자기상관 기반 걸음 주파수, band energy를 계산한다.

```python
import numpy as np
fs = 50                                               # IMU 50 Hz
t = np.arange(0, 10, 1 / fs)                          # 10초 = 500 샘플
rng = np.random.default_rng(8)
f_step = 1.8                                          # 초당 1.8걸음
acc = (1.0 + 0.30 * np.sin(2*np.pi*f_step*t) + 0.10 * np.sin(2*np.pi*2*f_step*t + 0.7)
       + 0.05 * rng.standard_normal(len(t)))          # |a| (g 단위), 중력 1 g 포함
x = acc - acc.mean()                                  # DC(중력) 제거
rms = np.sqrt(np.mean(x ** 2))
zc = np.count_nonzero(np.signbit(x[1:]) != np.signbit(x[:-1]))
print(f"RMS = {rms:.4f} g, zero-crossings = {zc} in 10 s -> {zc/10/2:.2f} Hz (교차 2번 = 1주기)")
r = np.correlate(x, x, mode="full")[len(x) - 1:]      # 자기상관 r[lag], lag >= 0
r /= r[0]
lo, hi = int(0.3 * fs), int(1.5 * fs)                 # 걸음 주기 0.3~1.5초만 탐색
lag = lo + np.argmax(r[lo:hi])
print(f"autocorr peak lag = {lag} samples = {lag/fs:.2f} s -> step freq {fs/lag:.3f} Hz (r={r[lag]:.3f})")
d = 0.5 * (r[lag-1] - r[lag+1]) / (r[lag-1] - 2*r[lag] + r[lag+1])   # 포물선 보간 (sub-sample)
print(f"parabolic refine: lag = {lag + d:.2f} samples -> {fs/(lag + d):.3f} Hz")
X = np.abs(np.fft.rfft(x[:256] * np.hanning(256))) ** 2  # 256점 = 5.12 s, Δf = 0.195 Hz
f = np.fft.rfftfreq(256, 1 / fs)
bands = [(0.5, 1.0), (1.0, 3.0), (3.0, 8.0), (8.0, 25.0)]
E = [X[(f >= a) & (f < b)].sum() for a, b in bands]
print("band energy share:", {f"{a}-{b}Hz": round(float(e / sum(E)), 3) for (a, b), e in zip(bands, E)})
np.save("acf.npy", r[:hi + 10])
```

```text
RMS = 0.2273 g, zero-crossings = 44 in 10 s -> 2.20 Hz (교차 2번 = 1주기)
autocorr peak lag = 28 samples = 0.56 s -> step freq 1.786 Hz (r=0.895)
parabolic refine: lag = 27.77 samples -> 1.801 Hz
band energy share: {'0.5-1.0Hz': 0.001, '1.0-3.0Hz': 0.848, '3.0-8.0Hz': 0.115, '8.0-25.0Hz': 0.036}
```

출력에서 볼 것:

- zero-crossing은 2.20 Hz로 **틀렸다**(정답 1.8). 2배음과 잡음 때문에 0 근처에서 부호가 여러 번 바뀐다. 실전에서는 hysteresis(±임계값을 넘어야 교차로 인정)나 LPF를 먼저 건다.
- 자기상관 peak는 lag 28 샘플 = 0.56 s → 1.786 Hz. 정수 lag라 해상도가 거칠다(lag 27 ↔ 1.85 Hz, 28 ↔ 1.79 Hz). 이웃 세 점으로 **포물선 보간**하면 27.77 → 1.801 Hz로 정답에 붙는다. 5.5절 zero-padding이 FFT peak를 보간하는 것과 같은 아이디어다.
- band energy는 1~3 Hz에 85%, 3~8 Hz에 11.5%(2배음 3.6 Hz). 걷기(1.5~2.5 Hz)와 뛰기(2.5~3.5 Hz), 떨림(4~12 Hz)을 이런 band 비율로 가른다.

```svg
<svg viewBox="0 0 640 260" xmlns="http://www.w3.org/2000/svg">
<rect x="160.0" y="25.5" width="400.0" height="189.0" fill="#3f9a6b" fill-opacity="0.10" stroke="none"/> <line x1="60" y1="120" x2="620" y2="120" stroke="currentColor" stroke-width="0.6"/> <line x1="60" y1="25.5" x2="60" y2="214.5" stroke="currentColor" stroke-width="0.6"/> <polyline fill="none" stroke="#4a7bd0" stroke-width="1.6" points="60.0,30.0 66.7,37.4 73.3,45.7 80.0,58.5 86.7,74.9 93.3,93.4 100.0,111.9 106.7,129.2 113.3,145.1 120.0,158.2 126.7,168.0 133.3,175.7 140.0,180.7 146.7,183.9 153.3,184.3 160.0,183.5 166.7,180.5 173.3,175.4 180.0,167.1 186.7,156.7 193.3,143.4 200.0,127.2 206.7,110.1 213.3,91.7 220.0,74.1 226.7,59.7 233.3,48.0 240.0,41.1 246.7,39.5 253.3,43.8 260.0,52.4 266.7,65.7 273.3,81.9 280.0,99.3 286.7,116.6 293.3,133.0 300.0,147.0 306.7,158.3 313.3,167.4 320.0,173.8 326.7,177.8 333.3,180.2 340.0,180.4 346.7,178.9 353.3,176.0 360.0,170.2 366.7,162.7 373.3,151.9 380.0,138.4 386.7,123.3 393.3,106.9 400.0,89.5 406.7,73.7 413.3,60.1 420.0,50.4 426.7,45.1 433.3,45.4 440.0,49.5 446.7,59.3 453.3,72.4 460.0,87.3 466.7,103.2 473.3,119.9 480.0,134.3 486.7,147.2 493.3,158.0 500.0,165.8 506.7,171.1 513.3,174.7 520.0,176.6 526.7,176.9 533.3,174.7 540.0,171.1 546.7,165.8 553.3,157.8 560.0,147.2 566.7,134.8 573.3,120.2 580.0,104.1 586.7,88.4 593.3,74.3 600.0,62.0 606.7,53.6 613.3,49.4 620.0,50.3"/> <circle cx="246.7" cy="39.5" r="5" fill="#d0564a"/> <text x="254.7" y="33.5" font-size="12">peak: lag 28 = 0.56 s → 1.79 걸음/s</text> <text x="54" y="34.0" font-size="12" text-anchor="end">1</text> <text x="54" y="124.0" font-size="12" text-anchor="end">0</text> <text x="54" y="214.0" font-size="12" text-anchor="end">-1</text> <text x="60.0" y="230.5" font-size="12" text-anchor="middle">0s</text> <text x="226.7" y="230.5" font-size="12" text-anchor="middle">0.5s</text> <text x="393.3" y="230.5" font-size="12" text-anchor="middle">1s</text> <text x="560.0" y="230.5" font-size="12" text-anchor="middle">1.5s</text> <text x="360.0" y="196.5" font-size="12" text-anchor="middle">탐색 구간 0.3 ~ 1.5 s (걸음 주기로 그럴듯한 범위)</text> <text x="340.0" y="252" font-size="12" text-anchor="middle">lag (초) — 정규화된 자기상관 r[lag] = Σ x[n]·x[n+lag] / Σ x[n]²</text>
</svg>
```

그림 8 — ex24의 정규화된 자기상관(실제 값). lag 0에서 1, 걸음 주기 0.56 s에서 첫 peak(0.895). 탐색 구간을 0.3~1.5 s로 제한해야 lag 0 근처나 2배 주기 peak를 잘못 고르지 않는다.

### 9.3 임베디드 연결

- 자기상관을 직접 하면 창 N × lag L 번의 MAC(500 × 75 ≈ 3.8만)이다. 길어지면 FFT로 한다: r = IFFT(|FFT(x)|²) (Wiener–Khinchin, 원형 상관을 피하려면 2N으로 zero-pad).
- 50 Hz IMU면 hop 1초에 이런 특징 수십 개를 뽑아도 연산량이 미미하다. 진짜 비용은 **센서와 MCU를 깨우는 횟수**다 — IMU FIFO + watermark 인터럽트(G2)로 묶어 읽고, 특징 계산은 묶음 단위로 한다.
- IMU 내장 필터·ODR 설정(1.4절 aliasing)과 MCU 쪽 필터를 **학습 데이터 수집 때와 똑같이** 맞춰야 한다. 데이터 수집 펌웨어와 추론 펌웨어가 다른 ODR이면 특징 분포가 달라진다(G7 시간 동기화·리샘플링).

---

## 10. 임베디드 관점에서 다시 보기

이 노트의 모든 블록을 "기기에 올릴 때 무엇을 정하고 무엇을 재나"로 정리하면 다음과 같다.

| 블록 | 정할 것 | 잴 것 | 어디서 도나 (일반론) |
|---|---|---|---|
| 샘플링·AAF | fs, ADC/PDM 설정, IMU ODR·bandwidth | alias 대역 잡음, SNR | 코덱·센서 HW |
| 리샘플링 | 비율, 필터 tap, polyphase | 통과대역 평탄도, alias 감쇠 | DSP/MCU, 또는 코덱 내장 |
| FIR/IIR | 구조(DF1/DF2T), 형식(float/Q15/Q31), 구간 순서 | golden diff, SNR, limit cycle 유무 | DSP/MCU |
| FFT | 길이, real FFT, 스케일링(BFP) | cycle, 오차 | DSP(HiFi/Hexagon 라이브러리), Cortex-M(CMSIS-DSP) |
| mel·log | 공식(HTK/Slaney), norm, ε, 밑, sparse 저장 | 학습 코드와 frame별 diff | DSP/MCU |
| 정규화 | 방식(전역/이동/PCEN), 상수 | 분포(μ, σ) 비교 | DSP/MCU, int8 변환과 합침 |
| 모델 | 입력 shape [T, n_mels] vs [n_mels, T] | 정확도, 지연 | DSP 또는 NPU |

Don이 가져갈 세 가지 습관:

1. **스펙 표를 먼저 쓴다**. fs, 창, hop, nfft, window 종류(periodic/symmetric), center, mel 공식·norm·f_min·f_max, power(1/2), log 밑·ε, top_db, DCT norm·lifter, 정규화 상수, 출력 shape. 모델팀의 Python 코드에서 이 값들을 **하나씩 뽑아** 펌웨어 헤더의 상수로 만든다.
2. **golden vector로 블록별 diff를 낸다**. 같은 wav → Python 중간값(frame, power, mel, log-mel)을 파일로 → 펌웨어 중간값과 frame별 비교. 차이가 생기면 어느 블록에서 처음 생겼는지 이분 탐색한다. 버스 트레이스로 장애 지점을 좁히는 것과 같은 방법이다.
3. **cycle을 잰다**. 블록마다 DWT cycle counter로 frame당 cycle을 재서 hop deadline 대비 점유율을 문서화한다. 최적화는 측정된 가장 큰 블록(대개 FFT 또는 dense mel)부터.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| decimation 전에 LPF 없음 | 고주파 잡음이 저주파 가짜 톤으로 나타남 | aliasing (1.6절) | `resample_poly`/polyphase FIR, IMU bandwidth < ODR/2 |
| 고차 IIR을 ba 다항식으로 구현 | 출력이 발산하거나 `nan`, 정밀도 따라 동작이 다름 | 극점 민감도 (4.3절) | `butter(..., output="sos")` + biquad cascade |
| scipy a 계수를 CMSIS biquad에 그대로 복사 | 필터가 발산 | CMSIS는 a1, a2 부호 반대 (4.3절) | 부호 뒤집기, 단위 테스트로 임펄스 응답 비교 |
| 낮은 cutoff IIR을 Q15 계수로 | cutoff가 엉뚱하거나 DC에서 발산, 무음에서 작은 톤 | 계수 양자화·limit cycle (4.5~4.6절) | Q31/float, decimate 후 필터, 포화 산술 |
| `np.hanning`과 `torch.hann_window` 혼용 | golden diff가 1e-3 수준에서 안 맞음 | 대칭 vs periodic (5.4절) | 학습 코드의 window 정의를 그대로 |
| CMSIS rfft 출력의 out[0], out[1]을 일반 bin으로 처리 | DC bin 값이 이상함 | DC·Nyquist 포장 형식 (5.9절) | 두 값을 따로 처리 |
| zero-padding으로 해상도를 올리려 함 | 가까운 두 톤이 여전히 하나로 보임 | 해상도는 데이터 길이가 결정 (5.5절) | 창을 길게 (시간 해상도와 trade-off) |
| mel 공식·norm 불일치 (HTK vs Slaney) | 정확도가 몇 % 조용히 떨어짐 | 필터 경계·높이가 다름 (7.1절) | `mel_scale`, `norm` 명시, 필터뱅크 diff |
| ε·log 밑·top_db 불일치 | 조용한 구간 오탐 증가 | 바닥 값이 다름 (7.3절) | 상수를 헤더로 공유, top_db 대신 고정 ε |
| 학습은 발화 CMVN, 기기는 전역 μ, σ | 마이크·gain 따라 성능 편차 | 정규화 불일치 (7.6절) | 학습도 스트리밍 가능한 방식으로 |
| dense mel 행렬 | 특징 계산이 FFT보다 오래 걸림 | 95% 0을 곱함 (8.1절) | (시작 bin, 길이, 가중치) sparse 저장 |

---

## 12. 면접에서 이렇게 말한다

**Q.** What is aliasing and how do you prevent it?

**A.** 샘플링 주파수의 절반(Nyquist)을 넘는 성분이 낮은 주파수로 접혀 들어와 원래 신호와 구분할 수 없게 되는 현상이다. 10 kHz로 7 kHz를 찍으면 3 kHz와 샘플이 완전히 같다. 샘플링 후에는 고칠 수 없으므로 ADC 앞의 anti-aliasing 필터, oversampling + 디지털 decimation 필터, 그리고 리샘플링·decimation 직전의 디지털 LPF로 막는다. IMU도 ODR 대비 bandwidth 설정으로 같은 문제를 다룬다.

> Aliasing is when content above Nyquist, fs/2, folds back into the baseband and becomes indistinguishable from real low-frequency content — a 7 kHz tone sampled at 10 kHz produces exactly the same samples as a 3 kHz tone. You can't undo it after sampling, so you prevent it before every rate reduction: an analog anti-aliasing filter or, more commonly, oversampling with a sigma-delta or PDM front end followed by a digital decimation filter, and a proper low-pass in every resampler — for example a polyphase FIR when going from 48 to 16 kHz. On IMUs the same rule shows up as keeping the digital filter bandwidth below ODR/2.

**Q.** FIR vs IIR for an MCU?

**A.** FIR은 항상 안정하고 대칭이면 선형 위상이며 고정소수점 구현이 쉽고 polyphase 리샘플링과 궁합이 좋지만, 날카로운 필터는 tap이 많고 지연이 (N−1)/2다. IIR은 biquad 몇 개로 같은 날카로움을 훨씬 싸게 얻지만 위상이 비선형이고, 계수 양자화로 극점이 움직이며 limit cycle이 있다. 그래서 리샘플링·선형 위상이 필요하면 FIR, DC 제거나 band 에너지처럼 싸고 날카로운 필터는 SOS biquad, 고정소수점이면 DF1 + Q31 또는 FPU가 있으면 float DF2T를 쓴다.

> It's a trade-off between phase, latency, compute and numerical risk. FIR is always stable, linear-phase when symmetric, easy in Q15 with a wide accumulator, and fits polyphase resampling — but sharp responses need many taps and add (N−1)/2 samples of delay. IIR gets the same sharpness with a few biquads, so it's far cheaper, but the phase is nonlinear and in fixed point the coefficient quantization moves the poles and you can get limit cycles. My default is FIR for resampling and anything that must preserve waveform shape, and a cascade of second-order sections for cheap filters like DC removal or band energies — float DF2T if there's an FPU, DF1 with Q31 coefficients otherwise.

**Q.** Why window before an FFT?

**A.** DFT은 frame이 주기적으로 반복된다고 가정해서, 톤이 bin 사이에 있으면 frame 경계의 불연속 때문에 에너지가 모든 bin으로 번진다(leakage). window로 양 끝을 줄이면 사이드로브가 rect −13 dB에서 Hann −31 dB, Blackman −58 dB로 내려간다. 대가로 main lobe가 넓어져 가까운 두 톤을 구분하는 능력이 떨어지고 잡음 대역폭이 늘어난다. 음성 STFT는 Hann이 표준이고, periodic/symmetric 정의까지 학습 코드와 맞춰야 한다.

> The DFT implicitly treats the frame as one period of a periodic signal, so any tone that isn't bin-centered sees a discontinuity at the frame edges and its energy leaks into every bin. A taper like Hann brings the sidelobes from −13 dB for a rectangular window down to about −31 dB, and Blackman to around −58, at the cost of a wider main lobe — so less ability to separate close tones — and a higher noise bandwidth. For speech features Hann is standard, and on device I match the exact definition, periodic versus symmetric, because that alone breaks golden-vector comparisons.

**Q.** Walk me through computing log-mel features and their cost per frame.

**A.** 16 kHz에서 25 ms 창 400 샘플을 10 ms마다 링버퍼에서 꺼내 Hann을 곱하고, 512로 zero-pad 해서 real FFT, 257 bin의 power를 구하고, 40개 삼각 mel 필터를 sparse하게 곱하고, log(x + ε)를 취한다. MFCC면 DCT-II로 10~13개를 남긴다. frame당 FFT가 곱 약 5천 개로 지배적이고, sparse mel은 약 500 MAC, DCT 400 MAC, 합쳐 1만 6천 flop 정도, 100 frame/s면 초당 약 1.6 M flop이고 상수·버퍼는 10 KB 정도다. 핵심은 연산이 아니라 학습 코드와의 일치(window, center, mel 공식, ε, 정규화)와 golden vector 검증이다.

> At 16 kHz I take a 400-sample, 25 ms window every 160 samples from a ring buffer, apply a periodic Hann, zero-pad to 512 and run a real FFT, take the power of the 257 bins, apply 40 triangular mel filters stored sparsely, and take log of x plus epsilon; for MFCCs I add a DCT-II and keep 10 to 13 coefficients. Per frame that's roughly 7k multiplies and 9k adds — the FFT dominates, the sparse mel stage is about 500 MACs versus 10k if you store it dense — so around 1.6 MFLOP/s at 100 frames per second and about 10 KB of tables and buffers. The hard part isn't compute, it's matching the training pipeline exactly — window definition, centering, mel formula and normalization, epsilon, log base, feature normalization — which I verify with per-frame golden vectors from the Python reference.

**Q.** How do you implement a stable high-order IIR in fixed point?

**A.** 첫째, 다항식 하나로 만들지 않고 2차 구간(SOS)으로 쪼갠다 — 고차 다항식의 근은 계수 오차에 극도로 민감하다. 둘째, 구조는 DF1(상태가 실제 입·출력이라 범위 예측 쉬움)에 넓은 누산기, 계수는 a1 범위(±2) 때문에 Q14나 Q31로, 포화 산술. 셋째, 구간별 이득을 나눠 내부 overflow를 막고 구간 순서를 설계한다. 넷째, cutoff가 fs에 비해 낮으면 먼저 decimate 해서 극점을 z = 1에서 떼어 낸다. 마지막으로 양자화된 계수로 극점을 다시 계산해 단위원 안인지 확인하고, 무음 입력으로 limit cycle을 시험한다.

> I never implement it as one high-order polynomial — the roots of a high-order denominator are extremely sensitive to coefficient rounding, so I design it as second-order sections. Each biquad goes in Direct Form I with a wide accumulator and saturating arithmetic, coefficients in Q14 or ideally Q31 since a1 spans ±2, with the gain distributed across sections so no internal node overflows. If the cutoff is very low relative to fs, I decimate first so the poles move away from z = 1. Then I verify: recompute the poles from the quantized coefficients to make sure they're inside the unit circle, compare against the float reference, and run a zero-input test to check for limit cycles.

---

## 13. 직접 해보기

1. 손계산: fs = 16 kHz에서 9 kHz, 15 kHz, 25 kHz 톤은 각각 몇 Hz로 보이나? 정답: 7 kHz, 1 kHz, 7 kHz (|f − k·16|를 0~8 kHz로).
2. 손계산: 3-tap 필터 h = [1, 2, 1]/4의 주파수 응답을 ω = 0, π/2, π에서 구하라. 이 필터는 LPF인가 HPF인가? 정답: H = (1 + 2e^{−jω} + e^{−j2ω})/4 → 크기 1, 0.5, 0 → LPF (영점이 z = −1에 2개).
3. 손계산: 16 kHz, nfft = 512에서 1 kHz는 몇 번 bin인가? 2 kHz 톤과 2.02 kHz 톤을 Hann으로 구분하려면 창이 최소 몇 샘플이어야 하나? 정답: 1000/31.25 = 32번 bin. Hann은 약 2 bin 간격이 필요하므로 fs/N ≤ 10 Hz → N ≥ 1600 샘플(100 ms).
4. 코드 과제: ex10의 50 Hz LPF를 16 kHz → 1 kHz로 decimate 한 뒤 다시 설계하고(fs = 1000), Q14로 반올림했을 때 극점 이동이 얼마나 줄어드는지 비교하라. 힌트: `butter(2, 50, fs=1000)`의 극점은 |p| ≈ 0.80으로 z = 1에서 훨씬 멀다.
5. 코드 과제: 8.2절 C 코드의 FFT를 5.8절 real FFT 트릭(256점 복소 FFT + 후처리)으로 바꾸고, 결과가 그대로인지 ex23으로 확인한 뒤 frame당 시간을 비교하라. 힌트: 후처리에는 N/2 + 1개 bin마다 twiddle e^{−j2πk/N} 곱 하나가 필요하다.
6. 코드 과제: ex23의 numpy 기준을 `torchaudio.transforms.MelSpectrogram(..., center=False, mel_scale="htk", norm=None)`로 바꾸면 frame이 몇 개 나오고 왜 C와 정렬이 안 맞는가? 정답: 97개. `win_length < n_fft`이면 torch는 512 샘플 frame 가운데에 400 창을 두므로 56 샘플 offset이 생긴다(B5 1.8).

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| Nyquist frequency | 나이퀴스트 주파수 | fs/2. 샘플링으로 표현 가능한 최고 주파수 |
| aliasing | 앨리어싱, 접힘 | fs/2 위 성분이 아래로 접혀 들어와 구분 불가 |
| polyphase | 다상 분해 | 남길 출력만 계산하는 멀티레이트 필터 구현 |
| quantization noise | 양자화 잡음 | 분산 Δ²/12, SNR ≈ 6.02N + 1.76 dB |
| LTI | 선형 시불변 시스템 | 임펄스 응답 하나로 완전히 기술되는 시스템 |
| convolution | 컨볼루션 | y[n] = Σ h[k]·x[n−k] (h를 뒤집어 민다) |
| frequency response | 주파수 응답 | 사인파의 크기·위상 변화, h의 Fourier 변환 |
| z-transform | z-변환 | z⁻¹ = 한 샘플 지연. H(z) = B(z)/A(z) |
| pole / zero | 극점 / 영점 | A(z)=0 / B(z)=0. 극점은 단위원 안이어야 안정 |
| FIR / IIR | 유한 / 무한 임펄스 응답 필터 | 피드백 없음(항상 안정) / 피드백 있음(싸고 날카로움) |
| linear phase / group delay | 선형 위상 / 군지연 | 모든 주파수가 같은 시간 지연 / −dφ/dω |
| windowed-sinc | 창 씌운 sinc | 이상적 LPF 임펄스 응답을 잘라 window 곱 |
| biquad / SOS | 2차 구간 | 고차 IIR을 2차 필터 cascade로 |
| DF1 / DF2T | 직접형 I / 전치 직접형 II | 상태 4개(고정소수점) / 상태 2개(float) |
| limit cycle / deadband | 리밋 사이클 / 불감대 | 입력 0인데 반올림 때문에 남는 진동·잔류값 |
| DFT / FFT | 이산 푸리에 변환 / 고속 알고리즘 | N² → (N/2)·log₂N 복소 곱 |
| bin / Δf | 주파수 칸 / 간격 | k·fs/N, fs/N |
| leakage | 누설 | bin 사이 톤의 에너지가 다른 bin으로 번짐 |
| zero-padding | 0 채우기 | DTFT 보간. 해상도는 안 올라감 |
| butterfly / twiddle | 버터플라이 / 회전 인자 | a ± W·b / W = e^{−j2πk/N} |
| block floating point (BFP) | 블록 부동소수점 | 블록당 공통 지수, 고정소수점 FFT 스케일링 |
| STFT / spectrogram | 단시간 푸리에 변환 | frame마다 window + FFT / 그 크기 그림 |
| COLA / NOLA | 일정 겹침 합 / 0 아닌 겹침 합 | OLA·WOLA 재합성 조건 |
| mel scale (HTK / Slaney) | 멜 척도 | 지각 주파수 축. 공식 두 가지 |
| log-mel | 로그 멜 | log(mel 에너지 + ε) |
| MFCC / DCT-II | 멜 켑스트럼 계수 / 이산 코사인 변환 | log-mel을 DCT 해서 앞 K개 |
| PCEN | 채널별 에너지 정규화 | 이동 평균으로 나누는 AGC + 압축 |
| CMVN | 켑스트럼 평균·분산 정규화 | band별 평균 0, 분산 1 |
| autocorrelation | 자기상관 | 신호와 지연된 자신의 내적, 주기 검출 |
| golden vector | 골든 벡터 | 기준 구현(Python)의 입력·출력 쌍, 펌웨어 검증용 |

---

## 15. 요약 & 체크리스트

샘플링은 fs/2 위를 접어 넣으므로(10 kHz에서 7 kHz → 3 kHz) 매 샘플링·decimation 직전에 LPF가 있어야 하고, 48 → 16 kHz는 polyphase FIR로 한다. 필터는 LTI 시스템이고 임펄스 응답·주파수 응답·z-평면의 극점/영점으로 읽는다. FIR(windowed-sinc, `firwin`)은 안정하고 선형 위상이며 Q15 + 넓은 누산기로 쉽게 구현되고, IIR은 biquad 몇 개로 싸지만 반드시 SOS로 쪼개고 고정소수점에서는 계수 양자화·limit cycle을 조심한다. FFT는 N점을 (N/2)·log₂N 복소 곱으로 계산하며, 해상도는 데이터 길이가, leakage는 window가 정하고, zero-padding은 보간일 뿐이다. STFT의 창 길이는 시간-주파수 trade-off이고, mel(HTK/Slaney, norm) → log(ε, 밑) → DCT(MFCC) → 정규화(전역/CMVN/PCEN)의 모든 선택이 학습 코드와 같아야 한다. 16 kHz/25 ms/10 ms/512/40 mel 파이프라인은 frame당 약 1만 6천 flop, 10 KB 수준이고, 스트리밍 C 구현은 numpy 배치와 3e-5 안에서 맞았다. IMU의 RMS·zero-crossing·자기상관·band energy도 같은 DSP 도구다.

- [ ] 주어진 fs에서 임의의 톤이 몇 Hz로 alias 되는지 손으로 계산할 수 있다
- [ ] 양자화 SNR 6.02N + 1.76 dB를 유도하고, 작은 신호에서 SNR이 줄어드는 이유를 말할 수 있다
- [ ] 48 → 16 kHz 리샘플링 필터의 cutoff와 polyphase로 줄어드는 연산량을 계산할 수 있다
- [ ] 간단한 FIR의 주파수 응답과 영점을 손으로 구하고, 극점 위치로 IIR 안정성을 판정할 수 있다
- [ ] windowed-sinc FIR을 설계해 C(float, Q15)로 구현하고 `lfilter`와 대조할 수 있다
- [ ] 고차 IIR을 SOS로 설계해 DF2T C 코드로 구현하고 `sosfilt`와 대조할 수 있다
- [ ] Q15 IIR의 계수 양자화·limit cycle 위험과 그 대처 4가지를 설명할 수 있다
- [ ] bin, Δf, leakage, window 선택, zero-padding을 숫자 예로 설명하고 radix-2 FFT를 C로 짤 수 있다
- [ ] HTK와 Slaney mel 공식, norm, ε, top_db 차이를 알고 torchaudio 옵션으로 재현할 수 있다
- [ ] log-mel 파이프라인의 frame당 연산량·메모리를 세고, 스트리밍 C 구현을 golden vector로 검증할 수 있다

---

## 참고 자료

- Alan V. Oppenheim, Ronald W. Schafer, "Discrete-Time Signal Processing" (Pearson) — 샘플링, z-변환, FIR/IIR, FFT의 표준 교재
- Richard G. Lyons, "Understanding Digital Signal Processing" (Prentice Hall) — 직관 위주, 엔지니어 친화적
- Steven W. Smith, "The Scientist and Engineer's Guide to Digital Signal Processing" — 무료 온라인 책: https://www.dspguide.com
- Julius O. Smith III, "Spectral Audio Signal Processing" (CCRMA, Stanford) — STFT, window, COLA: https://ccrma.stanford.edu/~jos/sasp/
- fred harris, "On the Use of Windows for Harmonic Analysis with the Discrete Fourier Transform", Proceedings of the IEEE, 1978 — window 비교의 고전
- SciPy 신호 처리 문서 (`scipy.signal.firwin`, `butter`, `sosfilt`, `resample_poly`, `stft`): https://docs.scipy.org/doc/scipy/reference/signal.html
- torchaudio 문서 (`MelSpectrogram`, `MFCC`, `melscale_fbanks`, `AmplitudeToDB`): https://pytorch.org/audio/stable/
- CMSIS-DSP (Arm) — `arm_rfft_fast_f32`, `arm_fir_q15`, `arm_biquad_cascade_df1_q15`, `arm_biquad_cascade_df2T_f32`: https://github.com/ARM-software/CMSIS-DSP
- Yuxuan Wang et al., "Trainable Frontend for Robust and Far-Field Keyword Spotting", ICASSP 2017 — PCEN
- S. Young et al., "The HTK Book" (Cambridge University Engineering Department) — HTK mel·MFCC·liftering 정의
- 이 노트와 연결: B5(오디오 front-end 속성 강의), E4(Q15·누산기·원형 버퍼·BFP), C1(양자화 잡음), A2(표준화), B2(conv = correlation), B3(deadband), B7(IMU 특징), G4(PDM·CIC)
