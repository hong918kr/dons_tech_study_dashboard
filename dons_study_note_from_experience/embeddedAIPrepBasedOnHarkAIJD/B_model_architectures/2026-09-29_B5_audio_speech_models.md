# B5. 오디오 · 음성 모델 — wake word, VAD, ASR, 화자 인식, TTS

> **이 노트를 다 읽으면**: 웨어러블의 always-listening 음성 파이프라인(mic → VAD → wake word → ASR → LLM → TTS)을 단계별 위치·전력·지연으로 설명할 수 있다 · 16 kHz 오디오에서 frame → STFT → log-mel → MFCC를 numpy와 torchaudio로 직접 계산하고 shape을 맞출 수 있다 · wake word 모델(DS-CNN)의 파라미터·MAC을 세고, streaming 검출(smoothing·threshold·refractory)과 FRR/FA-per-hour 평가를 할 수 있다 · CTC·RNN-T·Whisper·Conformer가 왜 다르고 어느 것이 기기에 맞는지 말할 수 있다
> **JD 연결**: (우대) "Audio/Voice/Vision models", "Hands-on with … microphones", "Optimizing models for MCUs & edge processors" — study_prep_list B5: KWS/wake word(DS-CNN), VAD, ASR(CTC, RNN-T, Conformer, Whisper), speaker verification, speech enhancement, TTS 기초, speech tokenizer/audio codec
> **Don 기준 난이도**: 프레임 단위 처리·DMA ping-pong·고정소수점·clz 트릭·전력 예산은 이미 강하다 / mel·MFCC의 의미, CTC blank, RNN-T, encoder-decoder ASR, 음성 모델의 평가 방식은 새로 배운다
> **선행 노트**: A2 (Bayes로 본 오경보), A4 (FAR/FRR/DET), B2 (CNN, depthwise separable), B3 (RNN, streaming state), B4 (Transformer). G5(DSP 기초)는 나중에 나오므로 이 노트 1절에 필요한 만큼만 압축해서 넣었다.

---

## 0. 큰 그림 — 귀에 걸린 기기가 "항상 듣는다"는 것의 의미

Hark 같은 음성 중심 웨어러블(가정)을 생각해 보자. 사용자는 기기를 하루 종일 착용하고, 아무 때나 "Hey Hark"(가상의 호출어) 하고 부른 뒤 질문한다. 기기는 대답을 음성으로 돌려준다. 겉보기엔 "음성 비서 하나"지만, 펌웨어 입장에서는 **전력 등급이 완전히 다른 여러 단계가 이어진 cascade(계단식 파이프라인)** 이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="a0" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="100" y="52" width="222" height="112" rx="8" fill="none" stroke="#3f9a6b" stroke-width="1.5" stroke-dasharray="6 4"/>
<text x="110" y="46" font-size="12">always-on 영역 (MCU / 저전력 DSP)</text>
<rect x="336" y="52" width="334" height="112" rx="8" fill="none" stroke="#4a7bd0" stroke-width="1.5" stroke-dasharray="6 4"/>
<text x="346" y="46" font-size="12">깨어난 뒤 (SoC: CPU · DSP · NPU, 일부 cloud)</text>
<rect x="12" y="84" width="76" height="46" rx="6" fill="#888" fill-opacity="0.15" stroke="currentColor"/>
<text x="50" y="104" font-size="13" text-anchor="middle">Mic</text>
<text x="50" y="121" font-size="12" text-anchor="middle">PDM→PCM</text>
<rect x="112" y="84" width="88" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/>
<text x="156" y="104" font-size="13" text-anchor="middle">VAD</text>
<text x="156" y="121" font-size="12" text-anchor="middle">말소리 있나?</text>
<rect x="222" y="84" width="88" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/>
<text x="266" y="104" font-size="13" text-anchor="middle">KWS</text>
<text x="266" y="121" font-size="12" text-anchor="middle">호출어인가?</text>
<rect x="348" y="84" width="92" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/>
<text x="394" y="104" font-size="13" text-anchor="middle">ASR</text>
<text x="394" y="121" font-size="12" text-anchor="middle">음성→텍스트</text>
<rect x="460" y="84" width="92" height="46" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/>
<text x="506" y="104" font-size="13" text-anchor="middle">LLM</text>
<text x="506" y="121" font-size="12" text-anchor="middle">SLM 또는 cloud</text>
<rect x="572" y="84" width="88" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/>
<text x="616" y="104" font-size="13" text-anchor="middle">TTS</text>
<text x="616" y="121" font-size="12" text-anchor="middle">텍스트→음성</text>
<line x1="88" y1="107" x2="110" y2="107" stroke="currentColor" marker-end="url(#a0)"/>
<line x1="200" y1="107" x2="220" y2="107" stroke="currentColor" marker-end="url(#a0)"/>
<line x1="310" y1="107" x2="346" y2="107" stroke="#d0564a" stroke-width="2" marker-end="url(#a0)"/>
<line x1="440" y1="107" x2="458" y2="107" stroke="currentColor" marker-end="url(#a0)"/>
<line x1="552" y1="107" x2="570" y2="107" stroke="currentColor" marker-end="url(#a0)"/>
<text x="329" y="180" font-size="12" text-anchor="middle">빨간 화살표 = wake IRQ</text>
<line x1="616" y1="130" x2="616" y2="152" stroke="currentColor" marker-end="url(#a0)"/>
<text x="628" y="150" font-size="12">speaker</text>
<text x="12" y="196" font-size="12">전력:</text>
<text x="112" y="196" font-size="12">수십 µW ~ 수 mW, 24시간 내내</text>
<text x="348" y="196" font-size="12">수백 mW ~ 1 W 이상, 대화하는 몇 초 동안만</text>
<text x="12" y="220" font-size="12">주기 / 지연:</text>
<text x="112" y="220" font-size="12">10 ms 프레임</text>
<text x="222" y="220" font-size="12">~100 ms마다</text>
<text x="348" y="220" font-size="12">streaming 수백 ms</text>
<text x="460" y="220" font-size="12">첫 토큰 0.3~2 s</text>
<text x="572" y="220" font-size="12">첫 소리 수백 ms</text>
<text x="12" y="244" font-size="12">모델 크기:</text>
<text x="112" y="244" font-size="12">VAD 0 ~ 수백 KB</text>
<text x="112" y="262" font-size="12">KWS 수십 ~ 수백 KB</text>
<text x="348" y="244" font-size="12">수십~수백 MB</text>
<text x="460" y="244" font-size="12">0.5~수 GB</text>
<text x="572" y="244" font-size="12">수십~수백 MB</text>
<text x="12" y="282" font-size="12">(숫자는 모두 typical 자릿수)</text>
<text x="12" y="300" font-size="12">원칙: 앞 단계일수록 작고 항상 켜져 있고, 뒤 단계일수록 크고 드물게 켜진다. 앞 단계의 오경보 = 뒤 단계 전력 낭비.</text>
</svg>
```

그림 1 — always-listening 음성 파이프라인. 초록 점선 안은 24시간 도는 부분, 파란 점선 안은 호출된 뒤 몇 초만 도는 부분이다. 전력·지연·크기 숫자는 업계에서 흔한 자릿수(typical)이고 Hark의 실제 값이 아니다.

단계마다 "어디서 도는가"를 표로 정리하면 이렇다. 오른쪽 열은 Hark 같은 기기를 가정한 **추측**이다.

| 단계 | 하는 일 | 흔한 위치 (typical) | 왜 거기서 | Hark라면 (추측) |
|---|---|---|---|---|
| Mic + PDM→PCM | 마이크 비트스트림을 16 kHz PCM으로 | 코덱 / 저전력 DSP / MCU 주변장치 | 항상 켜져 있어야 함 | always-on 도메인 |
| VAD | 프레임마다 "말소리가 있나" | 마이크 칩 내장, MCU, 저전력 DSP | 초저전력, 뒤 단계의 문지기 | MCU 또는 SoC의 저전력 섬(island) |
| KWS (wake word) | 1초 창에서 호출어 판정 | 저전력 DSP (예: Hexagon/HiFi 계열), MCU | 수십~수백 KB 모델, 수십 M MAC/s | 저전력 DSP 또는 MCU, 2단 검증은 SoC |
| ASR | 음성 → 텍스트, streaming | SoC NPU/CPU 또는 cloud | 수십 MB 이상 | on-device streaming + cloud 보강 |
| LLM | 질문 이해·응답 생성 | SoC NPU (SLM) 또는 cloud | GB급 메모리, 대역폭 | hybrid (L3) |
| TTS | 응답 텍스트 → 음성 | SoC 또는 cloud | 첫 음성까지 지연이 UX를 좌우 | hybrid |

펌웨어 비유로 보면: VAD는 **GPIO wake interrupt의 debounce 회로**, KWS는 **저전력 코어에서 도는 패킷 필터**, ASR 이후는 **메인 AP를 깨워서 하는 본 작업**이다. Don이 SSD에서 host 명령이 없을 때 컨트롤러를 저전력 상태로 두고, 특정 조건에서만 큰 코어를 깨우던 설계와 같은 구조다. 앞 단계가 잘못 깨우면(오경보) 뒤 단계의 큰 전력이 헛되이 쓰이고, 앞 단계가 놓치면(미검출) 사용자가 "말을 안 듣는다"고 느낀다. 이 노트 전체가 이 trade-off 이야기다.

이 노트의 순서: 1절 오디오 특징(모든 음성 모델의 입력) → 2절 VAD → 3절 KWS → 4절 ASR → 5절 화자 인식 → 6절 잡음 제거·에코 제거 → 7절 TTS와 audio codec → 8절 cascade 예산 → 9절 임베디드 관점(DMA double buffer, 고정소수점, streaming state).

---

## 1. 오디오 특징(feature) 속성 강의 — 샘플에서 log-mel까지

음성 모델은 거의 전부 **raw 파형이 아니라 log-mel spectrogram**(또는 그 변형인 MFCC)을 입력으로 받는다. 이 절은 G5(DSP 기초)의 핵심만 먼저 당겨 온다.

### 1.1 샘플링 — 왜 16 kHz인가

**샘플링 레이트(sample rate)** 는 초당 샘플 수다. Nyquist 정리에 따라 16 kHz로 샘플링하면 **8 kHz까지**의 주파수를 표현할 수 있다. 사람 말소리의 알아듣기 위한 정보(모음의 formant, 자음의 마찰음 대부분)는 대략 4~8 kHz 아래에 있어서, 음성 인식·wake word 모델은 거의 표준처럼 **16 kHz, mono, 16-bit PCM**을 쓴다. 음악용 48 kHz보다 데이터가 3배 적고, 전화 품질 8 kHz보다는 자음이 선명하다.

숫자 감각: 16 kHz × 2 byte = **32 KB/s**. 1초 버퍼는 32 KB, 10 ms 블록은 160 샘플 = 320 byte. MCU SRAM에서 "1초 분량 원본 오디오"가 이미 32 KB라는 사실이 뒤에서 계속 중요해진다.

### 1.2 frame, window, hop — 링버퍼에서 조각 꺼내기

음성은 시간에 따라 특성이 바뀌지만, **20~30 ms 정도의 짧은 구간 안에서는 거의 일정하다**(quasi-stationary)고 본다. 그래서 신호를 짧은 **frame**으로 자른다. 표준 설정은 **25 ms window, 10 ms hop**이다.

```
16 kHz 샘플 스트림 (1칸 = 80 샘플 = 5 ms)
|....|....|....|....|....|....|....|....|....|
[ frame 0: 샘플 0 ~ 399 (25 ms) ]
          [ frame 1: 160 ~ 559     ]
                    [ frame 2: 320 ~ 719     ]
 ◄─ hop 160 (10 ms) ─►
 인접 frame은 400 − 160 = 240 샘플(15 ms)이 겹친다
```

손으로 계산해 보자. 1초(16,000 샘플)에서 frame은 몇 개인가?

```
n_frames = 1 + ⌊(N − win) / hop⌋ = 1 + ⌊(16000 − 400) / 160⌋ = 1 + 97 = 98
```

말로 하면: 첫 frame 하나를 놓고, 남은 15,600 샘플 동안 160씩 97번 더 밀 수 있다. 그래서 **1초 ≈ 100 frame**이라는 감각이 생긴다(10 ms hop이니까). 펌웨어로 보면 hop = DMA 블록 크기, window = "최근 N개를 들고 있는 링버퍼 길이"다(9절에서 C로 구현).

각 frame에는 **window 함수**(보통 Hann: `w[n] = 0.5 − 0.5·cos(2πn/N)`)를 곱한다. 양 끝을 0으로 부드럽게 줄여서, frame을 잘라낸 경계가 가짜 고주파(spectral leakage)를 만들지 않게 하는 것이다.

### 1.3 STFT — frame마다 FFT

**STFT(short-time Fourier transform)** 는 "frame마다 FFT"다. 400 샘플 frame을 0으로 채워 `n_fft = 512`로 FFT하면, 실수 신호라 대칭이므로 **257개 bin**(0 ~ 8 kHz)만 쓰면 된다.

```
bin 개수      = n_fft / 2 + 1 = 257
bin 간격      = fs / n_fft = 16000 / 512 = 31.25 Hz
power spectrum = |FFT(frame · window)|²      → shape [T, 257] = [98, 257]
```

말로 하면: 1초 오디오가 "98개 시점 × 257개 주파수 칸"의 에너지 표로 바뀐다. 이것이 spectrogram이다.

### 1.4 mel scale — 사람 귀처럼 주파수 축 구부리기

사람 귀는 100 Hz와 200 Hz의 차이는 크게 느끼지만 7,000 Hz와 7,100 Hz 차이는 거의 못 느낀다. 저주파는 촘촘하게, 고주파는 성기게 듣는다. 이것을 흉내 낸 축이 **mel scale**이다. 흔히 쓰는 HTK 공식:

```
mel(f) = 2595 · log10(1 + f / 700)
f(mel) = 700 · (10^(mel / 2595) − 1)
```

손계산:

```
mel(1000 Hz) = 2595 · log10(1 + 1000/700) = 2595 · log10(2.4286) = 2595 · 0.38535 ≈ 1000
mel(8000 Hz) = 2595 · log10(12.4286)       = 2595 · 1.09442      ≈ 2840
```

말로 하면: 1 kHz 아래는 거의 선형(1 Hz ≈ 1 mel), 그 위로는 로그처럼 눌린다. 0 ~ 2840 mel 구간에 40개 필터를 놓으려면 경계점 42개를 mel 축에서 **균등하게**(간격 2840/41 ≈ 69.3 mel) 찍는다. 첫 필터 중심은 69.3 mel → `700·(10^(69.3/2595) − 1) ≈ 44 Hz`다. 이 경계점들로 만든 **삼각형 필터(mel filterbank)** 를 power spectrum에 곱하면(행렬곱 `[T,257] × [257,40]`) 40개 band 에너지가 나온다.

```svg
<svg viewBox="0 0 640 210" xmlns="http://www.w3.org/2000/svg">
<line x1="50" y1="140" x2="610" y2="140" stroke="currentColor"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="1.5" points="50.0,140 56.2,20 63.3,140"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="1.5" points="56.2,140 63.3,20 71.2,140"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="1.5" points="63.3,140 71.2,20 80.2,140"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="1.5" points="71.2,140 80.2,20 90.3,140"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="1.5" points="80.2,140 90.3,20 101.7,140"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="1.5" points="90.3,140 101.7,20 114.5,140"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="1.5" points="101.7,140 114.5,20 129.0,140"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="1.5" points="114.5,140 129.0,20 145.3,140"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="1.5" points="129.0,140 145.3,20 163.7,140"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="1.5" points="145.3,140 163.7,20 184.4,140"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="1.5" points="163.7,140 184.4,20 207.8,140"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="1.5" points="184.4,140 207.8,20 234.2,140"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="1.5" points="207.8,140 234.2,20 263.9,140"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="1.5" points="234.2,140 263.9,20 297.4,140"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="1.5" points="263.9,140 297.4,20 335.2,140"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="1.5" points="297.4,140 335.2,20 377.8,140"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="1.5" points="335.2,140 377.8,20 425.9,140"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="1.5" points="377.8,140 425.9,20 480.1,140"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="1.5" points="425.9,140 480.1,20 541.1,140"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="1.5" points="480.1,140 541.1,20 610.0,140"/>
<text x="50.0" y="156" font-size="12" text-anchor="middle">0</text>
<text x="120.0" y="156" font-size="12" text-anchor="middle">1k</text>
<text x="190.0" y="156" font-size="12" text-anchor="middle">2k</text>
<text x="330.0" y="156" font-size="12" text-anchor="middle">4k</text>
<text x="470.0" y="156" font-size="12" text-anchor="middle">6k</text>
<text x="610.0" y="156" font-size="12" text-anchor="middle">8k</text>
<text x="330.0" y="176" font-size="13" text-anchor="middle">주파수 (Hz, 선형 축) — 삼각형 20개, 아래쪽은 촘촘하고 위쪽은 넓다</text>
<text x="44" y="24" font-size="12" text-anchor="end">1</text>
<text x="44" y="144" font-size="12" text-anchor="end">0</text>
</svg>
```

그림 2 — HTK mel 필터 20개(설명용으로 개수를 줄임)를 선형 Hz 축에 그린 것. 실제 좌표를 계산해서 그렸다. 저주파 쪽 삼각형은 폭이 수십 Hz, 8 kHz 근처는 1 kHz가 넘는다.

### 1.5 log — dB처럼 눌러 주기

band 에너지는 조용한 소리와 큰 소리 사이에 수천~수만 배 차이가 난다. 그대로 넣으면 큰 값이 학습을 지배하므로 **log**를 취한다(`log(mel + ε)`, ε은 log(0) 방지용 작은 수). 에너지 1000배 차이가 ln으로 6.9 차이로 줄어든다. 이렇게 만든 `[T, n_mels]` 표가 **log-mel spectrogram**이다. 오늘날 KWS·ASR·Whisper·화자 인식 모델의 표준 입력이다.

### 1.6 MFCC — log-mel을 한 번 더 압축

**MFCC(mel-frequency cepstral coefficients)** 는 log-mel 40개에 **DCT(discrete cosine transform)** 를 적용해 앞쪽 10~13개 계수만 남긴 것이다. 이웃 mel band끼리는 값이 강하게 상관되어 있는데, DCT가 이 상관을 풀어(decorrelate) 적은 계수로 요약해 준다. 옛날 GMM-HMM 시절에 중요했고, 지금도 아주 작은 KWS 모델(예: 3절 Hello Edge의 49×10 입력)에서 입력 크기를 줄이는 용도로 쓴다. 딥러닝 모델이 커질수록 log-mel을 그대로 쓰는 쪽이 흔하다.

| 표현 | shape (1초, 10 ms hop) | 특징 | 주 사용처 |
|---|---|---|---|
| raw PCM | 16000 | 정보 전부, 너무 길다 | 일부 end-to-end 모델, codec |
| power spectrum | 98 × 257 | 선형 주파수 축 | 중간 단계 |
| log-mel | 98 × 40 (또는 80) | 귀 모양 축, log 압축 | KWS, ASR, Whisper(80), 화자 인식 |
| MFCC | 98 × 13 | band 간 상관 제거, 작다 | 초소형 KWS, 고전 모델 |

### 1.7 코드로 확인 — numpy로 처음부터

440 Hz tone(0~0.5 s) 뒤에 1.8 → 3.3 kHz로 올라가는 chirp(0.5~1 s)를 합성하고, frame → STFT → mel → log → MFCC를 numpy만으로 계산한다.

```python
import numpy as np
fs = 16000                                   # 16 kHz: 음성 모델의 표준 샘플레이트
t = np.arange(fs) / fs                       # 1초
rng = np.random.default_rng(0)
x = 0.5 * np.sin(2 * np.pi * 440 * t)        # 440 Hz tone (0~0.5 s)
x[t >= 0.5] = 0.5 * np.sin(2 * np.pi * (300 * t + 1500 * t**2))[t >= 0.5]  # 0.5~1 s: 1800→3300 Hz chirp
x += 0.01 * rng.standard_normal(fs)

win, hop, nfft = 400, 160, 512               # 25 ms window, 10 ms hop
n_frames = 1 + (len(x) - win) // hop
frames = np.stack([x[i*hop : i*hop + win] for i in range(n_frames)])   # [T, 400]
hann = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(win) / win)           # periodic Hann
spec = np.abs(np.fft.rfft(frames * hann, n=nfft)) ** 2                 # power, [T, 257]
print("frames", frames.shape, "-> power spectrum", spec.shape)

def hz2mel(f): return 2595 * np.log10(1 + f / 700)                     # HTK mel
def mel2hz(m): return 700 * (10 ** (m / 2595) - 1)
n_mels = 40
mel_pts = np.linspace(hz2mel(0), hz2mel(fs / 2), n_mels + 2)           # 42개 경계점
hz_pts = mel2hz(mel_pts)
bins = np.fft.rfftfreq(nfft, 1 / fs)                                   # 257개 bin 주파수
fb = np.zeros((len(bins), n_mels))
for m in range(n_mels):                                                # 삼각형 필터
    lo, c, hi = hz_pts[m], hz_pts[m + 1], hz_pts[m + 2]
    fb[:, m] = np.clip(np.minimum((bins - lo) / (c - lo), (hi - bins) / (hi - c)), 0, None)
mel = spec @ fb                                                        # [T,257]x[257,40]
logmel = np.log(mel + 1e-6)
print("mel filterbank", fb.shape, "-> log-mel", logmel.shape)
print("filter centers (Hz), first 5:", np.round(hz_pts[1:6]).astype(int))
print("filter centers (Hz), last 3 :", np.round(hz_pts[-4:-1]).astype(int))
k = np.arange(n_mels)
dct = np.cos(np.pi / n_mels * (k[None, :] + 0.5) * np.arange(13)[:, None])  # DCT-II (13x40)
mfcc = logmel @ dct.T
print("MFCC", mfcc.shape)
print("frame 10  (tone)  loudest mel band:", logmel[10].argmax(), f"(~{hz_pts[logmel[10].argmax()+1]:.0f} Hz)")
print("frame 60  (chirp) loudest mel band:", logmel[60].argmax(), f"(~{hz_pts[logmel[60].argmax()+1]:.0f} Hz)")
print("frame 95  (chirp) loudest mel band:", logmel[95].argmax(), f"(~{hz_pts[logmel[95].argmax()+1]:.0f} Hz)")
np.save("x.npy", x); np.save("logmel.npy", logmel)
```

```text
frames (98, 400) -> power spectrum (98, 257)
mel filterbank (257, 40) -> log-mel (98, 40)
filter centers (Hz), first 5: [ 44  92 142 195 252]
filter centers (Hz), last 3 : [6535 6994 7481]
MFCC (98, 13)
frame 10  (tone)  loudest mel band: 7 (~445 Hz)
frame 60  (chirp) loudest mel band: 22 (~2178 Hz)
frame 95  (chirp) loudest mel band: 27 (~3213 Hz)
```

출력에서 볼 것: 손계산과 같이 98 frame, 257 bin, 첫 필터 중심 44 Hz다. 필터 간격이 저주파에선 약 50 Hz, 고주파에선 약 500 Hz로 벌어진다. frame 60의 시작은 샘플 9,600(0.6 s)이고 창 중심은 0.6125 s인데, chirp 순간 주파수 `300 + 3000·t`에 넣으면 약 2,140 Hz라서 가장 센 band(~2,178 Hz)와 맞는다.

이 log-mel을 grid로 줄여서(4 frame 평균 × 2 band 평균) 그리면 다음과 같다.

```svg
<svg viewBox="0 0 640 330" xmlns="http://www.w3.org/2000/svg">
<rect x="70" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="70" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="70" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="70" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.97"/><rect x="70" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.46"/><rect x="70" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="70" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="70" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="70" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="70" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="70" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="70" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="70" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="70" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="70" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="70" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="70" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="70" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="70" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="70" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/>
<rect x="90" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="90" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="90" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.16"/><rect x="90" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.97"/><rect x="90" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.45"/><rect x="90" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="90" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="90" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="90" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="90" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="90" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="90" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="90" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="90" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="90" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="90" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="90" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="90" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="90" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="90" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/>
<rect x="110" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="110" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="110" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="110" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.97"/><rect x="110" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.46"/><rect x="110" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="110" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="110" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="110" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="110" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="110" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="110" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="110" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="110" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="110" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="110" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="110" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="110" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="110" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="110" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/>
<rect x="130" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="130" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="130" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="130" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.97"/><rect x="130" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.45"/><rect x="130" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="130" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="130" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="130" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="130" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="130" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="130" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="130" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="130" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="130" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="130" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="130" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="130" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="130" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="130" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/>
<rect x="150" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="150" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="150" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.16"/><rect x="150" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.97"/><rect x="150" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.45"/><rect x="150" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="150" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="150" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="150" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="150" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="150" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="150" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="150" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="150" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="150" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="150" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="150" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="150" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="150" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="150" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/>
<rect x="170" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="170" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="170" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="170" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.97"/><rect x="170" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.46"/><rect x="170" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="170" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="170" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="170" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="170" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="170" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="170" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="170" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="170" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="170" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="170" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="170" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="170" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="170" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="170" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/>
<rect x="190" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="190" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="190" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="190" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.97"/><rect x="190" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.46"/><rect x="190" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="190" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="190" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="190" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="190" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="190" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="190" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="190" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="190" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="190" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="190" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="190" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="190" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="190" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="190" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.16"/>
<rect x="210" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="210" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="210" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="210" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.97"/><rect x="210" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.46"/><rect x="210" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="210" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="210" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="210" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="210" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="210" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="210" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="210" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="210" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="210" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="210" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="210" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="210" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="210" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="210" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/>
<rect x="230" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="230" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="230" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.16"/><rect x="230" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.97"/><rect x="230" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.45"/><rect x="230" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="230" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="230" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="230" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="230" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="230" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="230" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="230" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="230" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="230" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="230" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="230" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="230" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="230" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="230" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/>
<rect x="250" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="250" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="250" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.16"/><rect x="250" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.97"/><rect x="250" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.46"/><rect x="250" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="250" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="250" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="250" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="250" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="250" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="250" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="250" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="250" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="250" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="250" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="250" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="250" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="250" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="250" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/>
<rect x="270" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="270" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="270" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.16"/><rect x="270" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.97"/><rect x="270" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.46"/><rect x="270" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="270" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="270" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="270" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="270" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="270" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="270" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="270" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="270" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="270" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="270" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="270" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="270" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="270" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="270" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/>
<rect x="290" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="290" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="290" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.16"/><rect x="290" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.97"/><rect x="290" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.46"/><rect x="290" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="290" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="290" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="290" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="290" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="290" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="290" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="290" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="290" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="290" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="290" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="290" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="290" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="290" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="290" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/>
<rect x="310" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.16"/><rect x="310" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.17"/><rect x="310" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.23"/><rect x="310" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.36"/><rect x="310" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.28"/><rect x="310" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.21"/><rect x="310" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.18"/><rect x="310" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.17"/><rect x="310" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.21"/><rect x="310" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.46"/><rect x="310" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.77"/><rect x="310" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.19"/><rect x="310" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="310" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="310" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="310" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="310" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="310" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="310" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="310" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/>
<rect x="330" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="330" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="330" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="330" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="330" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="330" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="330" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="330" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="330" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="330" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="330" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.99"/><rect x="330" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.19"/><rect x="330" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="330" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="330" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="330" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="330" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="330" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="330" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="330" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/>
<rect x="350" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="350" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="350" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="350" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="350" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="350" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="350" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="350" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="350" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="350" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="350" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.68"/><rect x="350" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.49"/><rect x="350" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="350" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="350" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="350" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="350" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="350" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="350" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="350" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/>
<rect x="370" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="370" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="370" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="370" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="370" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="370" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="370" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="370" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="370" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="370" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="370" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.34"/><rect x="370" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.9"/><rect x="370" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="370" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="370" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="370" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="370" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="370" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="370" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="370" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.16"/>
<rect x="390" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="390" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="390" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="390" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="390" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="390" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="390" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="390" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="390" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="390" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="390" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="390" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.99"/><rect x="390" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.21"/><rect x="390" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="390" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="390" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="390" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="390" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="390" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="390" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/>
<rect x="410" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="410" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="410" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="410" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="410" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="410" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="410" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="410" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="410" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="410" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="410" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="410" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.66"/><rect x="410" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.48"/><rect x="410" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="410" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="410" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="410" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="410" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="410" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="410" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/>
<rect x="430" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="430" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="430" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="430" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="430" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="430" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="430" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="430" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="430" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="430" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="430" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="430" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.37"/><rect x="430" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.84"/><rect x="430" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="430" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="430" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="430" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="430" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="430" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="430" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/>
<rect x="450" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="450" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="450" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="450" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="450" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="450" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="450" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="450" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="450" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="450" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="450" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="450" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="450" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="1.0"/><rect x="450" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="450" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="450" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="450" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="450" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="450" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="450" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/>
<rect x="470" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="470" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="470" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="470" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="470" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="470" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="470" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="470" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="470" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="470" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="470" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="470" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="470" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.81"/><rect x="470" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.39"/><rect x="470" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="470" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="470" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="470" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="470" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="470" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.17"/>
<rect x="490" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="490" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="490" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="490" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="490" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="490" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="490" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="490" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="490" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="490" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="490" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="490" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="490" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.46"/><rect x="490" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.6"/><rect x="490" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="490" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="490" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="490" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="490" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="490" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/>
<rect x="510" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="510" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="510" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="510" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="510" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="510" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="510" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="510" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="510" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="510" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="510" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="510" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.1"/><rect x="510" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.24"/><rect x="510" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.96"/><rect x="510" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.12"/><rect x="510" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="510" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="510" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.13"/><rect x="510" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/><rect x="510" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.15"/>
<rect x="530" y="252" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="530" y="240" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="530" y="228" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="530" y="216" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="530" y="204" width="20" height="12" fill="#4a7bd0" fill-opacity="0.05"/><rect x="530" y="192" width="20" height="12" fill="#4a7bd0" fill-opacity="0.06"/><rect x="530" y="180" width="20" height="12" fill="#4a7bd0" fill-opacity="0.07"/><rect x="530" y="168" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="530" y="156" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="530" y="144" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="530" y="132" width="20" height="12" fill="#4a7bd0" fill-opacity="0.09"/><rect x="530" y="120" width="20" height="12" fill="#4a7bd0" fill-opacity="0.08"/><rect x="530" y="108" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="530" y="96" width="20" height="12" fill="#4a7bd0" fill-opacity="0.99"/><rect x="530" y="84" width="20" height="12" fill="#4a7bd0" fill-opacity="0.18"/><rect x="530" y="72" width="20" height="12" fill="#4a7bd0" fill-opacity="0.11"/><rect x="530" y="60" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="530" y="48" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="530" y="36" width="20" height="12" fill="#4a7bd0" fill-opacity="0.14"/><rect x="530" y="24" width="20" height="12" fill="#4a7bd0" fill-opacity="0.16"/>
<rect x="70" y="24" width="480" height="240" fill="none" stroke="currentColor"/>
<line x1="310.0" y1="24" x2="310.0" y2="264" stroke="#e08a3c" stroke-dasharray="4 3"/>
<text x="70" y="282" font-size="12" text-anchor="middle">0</text>
<text x="310.0" y="282" font-size="12" text-anchor="middle">0.5</text>
<text x="550.0" y="282" font-size="12" text-anchor="middle">0.96 s</text>
<text x="310.0" y="300" font-size="13" text-anchor="middle">time (frame 4개 평균 = 40 ms 단위)</text>
<text x="62" y="264" font-size="12" text-anchor="end">0 Hz</text>
<text x="62" y="36" font-size="12" text-anchor="end">8 kHz</text>
<text x="62" y="144.0" font-size="12" text-anchor="end">mel</text>
<text x="190.0" y="18" font-size="12" text-anchor="middle">440 Hz tone</text>
<text x="430.0" y="18" font-size="12" text-anchor="middle">chirp 1.8→3.3 kHz</text>
<text x="562" y="44" font-size="12">진할수록</text>
<text x="562" y="60" font-size="12">log 에너지 큼</text>
</svg>
```

그림 3 — 위 코드가 실제로 계산한 log-mel 값으로 그린 spectrogram(24 × 20 칸으로 평균). 앞 절반은 440 Hz에 수평선, 뒤 절반은 올라가는 사선(chirp)이다. 모델은 이런 "그림"을 CNN으로 본다 — 그래서 KWS 모델이 이미지용 CNN 구조를 그대로 빌려 쓴다(B2).

### 1.8 torchaudio로 같은 계산 — 그리고 숨은 함정 두 개

실무에서는 `torchaudio.transforms.MelSpectrogram`을 쓴다. 같은 설정이면 같은 값이 나와야 한다. 확인해 보자.

```python
import numpy as np, torch, torchaudio
x = np.load("x.npy"); logmel_np = np.load("logmel.npy")           # 예제 1의 신호와 결과 [98, 40]
xt = torch.from_numpy(x).float()
mel_tf = torchaudio.transforms.MelSpectrogram(
    sample_rate=16000, n_fft=512, win_length=400, hop_length=160,
    n_mels=40, f_min=0.0, f_max=8000.0, power=2.0,
    center=False, mel_scale="htk", norm=None)                      # 예제 1과 같은 설정
logmel_t = torch.log(mel_tf(xt) + 1e-6)                            # [n_mels, T] (채널 먼저!)
print("numpy log-mel:", logmel_np.shape, " torchaudio:", tuple(logmel_t.shape))

# 1) 필터뱅크 자체 비교: 예제 1의 삼각형 공식 vs torchaudio
fb_t = torchaudio.functional.melscale_fbanks(257, 0.0, 8000.0, 40, 16000, None, "htk").numpy()
def hz2mel(f): return 2595 * np.log10(1 + f / 700)
hz = 700 * (10 ** (np.linspace(0, hz2mel(8000), 42) / 2595) - 1)
bins = np.fft.rfftfreq(512, 1 / 16000)
fb = np.stack([np.clip(np.minimum((bins - hz[m]) / (hz[m+1] - hz[m]),
               (hz[m+2] - bins) / (hz[m+2] - hz[m+1])), 0, None) for m in range(40)], 1)
print("filterbank max |diff|:", float(np.abs(fb - fb_t).max()))

# 2) torch.stft는 프레임을 n_fft(512) 길이로 잡고 400짜리 창을 가운데(56 샘플 뒤)에 둔다
off, T = (512 - 400) // 2, logmel_t.shape[1]
hann = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(400) / 400)
frames = np.stack([x[off + i*160 : off + i*160 + 400] for i in range(T)])
logmel_np2 = np.log((np.abs(np.fft.rfft(frames * hann, n=512)) ** 2) @ fb + 1e-6)
print("aligned log-mel max |diff|:", float(np.abs(logmel_np2 - logmel_t.T.numpy()).max()))
print("center=True (default) shape:", tuple(torchaudio.transforms.MelSpectrogram(
      16000, n_fft=512, win_length=400, hop_length=160, n_mels=40)(xt).shape))
```

```text
numpy log-mel: (98, 40)  torchaudio: (40, 97)
filterbank max |diff|: 2.986680954206178e-06
aligned log-mel max |diff|: 3.416820291413103e-05
center=True (default) shape: (40, 101)
```

출력에서 볼 것:

- **shape 순서가 다르다.** numpy 쪽은 `[T, n_mels]`, torchaudio는 `[n_mels, T]`(채널 먼저)다. 모델에 넣을 때 transpose를 잊으면 에러 없이 틀린 입력이 들어간다.
- **frame 수가 98 vs 97이다.** `win_length(400) < n_fft(512)`일 때 torch.stft는 frame을 **512 샘플**로 잡고 400짜리 창을 그 **가운데**(56 샘플 뒤)에 놓는다. 그래서 `1 + ⌊(16000 − 512)/160⌋ = 97`개. 시작 위치를 56 샘플 밀어 주면 log-mel 차이가 3.4 × 10⁻⁵(float32 반올림 수준)로 사라진다. 필터뱅크 자체는 3 × 10⁻⁶ 이내로 같다.
- 기본값 `center=True`는 양 끝을 n_fft/2씩 padding해서 `1 + 16000/160 = 101` frame이 나온다. 학습은 `center=True`로, 기기 펌웨어는 "들어온 만큼만" 계산하는 `center=False`식으로 하면 **frame 위치가 어긋난다**.

이것이 Don이 기기 쪽 feature extractor를 C로 짤 때 가장 먼저 맞춰야 할 부분이다. 모델팀의 Python 전처리와 **bit 단위는 아니어도 frame 위치·창·필터·log 밑·ε·정규화가 전부 같아야** 한다. A2 4절의 "학습 때 쓴 μ, σ를 펌웨어에 그대로 박는다"와 같은 규칙이다. 검증 방법도 같다: 같은 입력 wav를 넣고 Python 출력과 C 출력을 frame별로 diff한다(golden vector).

### 1.9 임베디드 연결과 함정

- **연산량**: frame 하나 = 400 샘플 창 곱 + 512점 real FFT + `257 × 40` 필터(대부분 0이라 실제로는 필터당 수~수십 개 bin만) + log 40번. 100 frame/s면 Cortex-M4급에서 CMSIS-DSP로 충분히 돌리는 크기다(정확한 cycle 수는 구현마다 다르므로 측정해야 한다).
- **sparse filterbank**: `[257,40]` 행렬을 통째로 저장하지 말고, 필터마다 (시작 bin, 길이, 가중치 배열)로 저장한다. 삼각형이라 각 bin은 최대 2개 필터에만 속한다.
- **log는 lookup 또는 clz**: 9절 C 코드에서 log2를 `__builtin_clzll` + 선형 보간으로 구한다.
- **함정**: `log` 밑(자연로그 vs log10 vs dB), `power=1`(magnitude) vs `power=2`(power), 필터 정규화(`norm="slaney"`), mel 공식(HTK vs Slaney) 중 하나만 달라도 값이 체계적으로 틀어진다. 모델 정확도가 "조금" 떨어지는 형태로 나타나서 찾기 어렵다.

---

## 2. VAD — "지금 말소리가 있나?"

### 2.1 직관

**VAD(voice activity detection)** 는 frame마다 "말소리 있음/없음"을 판단하는 이진 분류기다. 파이프라인의 맨 앞 문지기다. VAD가 "없음"이라고 하는 동안은 KWS조차 돌릴 필요가 없다(8절 duty cycling). 반대로 VAD가 말소리를 놓치면 wake word도 놓친다. 그래서 VAD는 **놓치지 않는 쪽(높은 recall)** 으로 튜닝하고, 오경보는 뒤의 KWS가 걸러 주길 기대한다.

### 2.2 가장 단순한 VAD — frame 에너지와 적응형 noise floor

frame 에너지(샘플 제곱의 평균)를 계산해서 **배경 잡음 수준(noise floor)보다 충분히 크면** 말소리로 본다. 핵심 부품 세 개:

1. **noise floor 추적**: 에너지가 floor보다 작으면 바로 내려가고, 크면 아주 천천히 따라 올라간다(1차 IIR, `floor += (E − floor) >> k`). 말소리는 짧게 크고, 배경은 길게 일정하다는 가정이다.
2. **onset 조건**: 2 frame 연속 floor + 9 dB를 넘어야 시작(클릭 잡음 무시). 스위치 debounce와 같다.
3. **hangover**: 말이 끝나도 80 ms 동안 "있음"을 유지한다. 단어 사이 짧은 쉼과 약한 끝 자음을 자르지 않기 위해서다.

C로 짜 보자. 16-bit PCM을 int64로 누산하고, log2는 `__builtin_clzll`로 정수부를 구하고 그 아래 8비트로 소수부를 선형 근사한다(Q8, 1.0 = 256). 말소리(0.8~1.4 s, 2.0~2.4 s)는 150 Hz 기본음 + 배음으로 흉내 내고, 2.6 s부터 팬 소음이 켜진다.

```c
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#define FS 16000
#define HOP 160                              /* 10 ms frame */
#define NFR 400                              /* 4 s */
#define CH 5                                 /* 출력 1글자 = 5프레임 = 50 ms */
static int16_t pcm[FS * 4];
static int32_t lg[NFR];                      /* 프레임별 log2(평균 파워), Q8 */

static uint32_t lcg = 12345u;                /* 결정적 난수: 배경 잡음 */
static int noise(int amp) { lcg = lcg * 1664525u + 1013904223u; return (int)((lcg >> 16) % (2u * amp + 1)) - amp; }
static int is_speech(double t) { return (t >= 0.8 && t < 1.4) || (t >= 2.0 && t < 2.4); }

/* log2(x) in Q8 (1.0 == 256): clz로 정수부, 그 아래 8비트로 소수부를 선형 근사 */
static int32_t log2_q8(uint64_t x) {
    if (x == 0) return 0;
    int ip = 63 - __builtin_clzll(x);
    uint32_t fr = ip >= 8 ? (uint32_t)(x >> (ip - 8)) & 0xFF : (uint32_t)(x << (8 - ip)) & 0xFF;
    return ip * 256 + (int32_t)fr;
}

static void vad(int shift, char *out) {      /* 적응형 noise floor + onset 2프레임 + hangover 80 ms */
    int32_t fl = lg[0]; int hang = 0, onset = 0;
    for (int f = 0; f < NFR; f++) {
        if (lg[f] < fl) fl = lg[f];          /* floor는 빠르게 내려가고 */
        else fl += (lg[f] - fl) >> shift;    /* 천천히 올라간다 (1/2^shift) */
        onset = (lg[f] > fl + 3 * 256) ? onset + 1 : 0;   /* floor + 3 (log2) ≈ +9 dB */
        if (onset >= 2) hang = 8; else if (hang > 0) hang--;
        if (f % CH == CH - 1) out[f / CH] = hang ? '#' : '.';
    }
}

int main(void) {
    for (int n = 0; n < FS * 4; n++) {
        double t = (double)n / FS, s = 0;
        if (is_speech(t)) s = 3000 * (sin(2 * M_PI * 150 * t) + 0.6 * sin(2 * M_PI * 450 * t) + 0.3 * sin(2 * M_PI * 900 * t));
        pcm[n] = (int16_t)(s + noise(t >= 2.6 ? 2500 : 200));   /* 2.6 s부터 팬 소음 */
    }
    for (int f = 0; f < NFR; f++) {
        uint64_t e = 0;                      /* int16² 합 → int64 누산 (overflow 없음) */
        for (int i = 0; i < HOP; i++) { int32_t v = pcm[f * HOP + i]; e += (uint64_t)((int64_t)v * v); }
        lg[f] = log2_q8(e / HOP);
    }
    char truth[NFR / CH + 1] = {0}, fixed[NFR / CH + 1] = {0}, fast[NFR / CH + 1] = {0}, slow[NFR / CH + 1] = {0};
    for (int f = CH - 1; f < NFR; f += CH) {
        truth[f / CH] = is_speech(f * 0.01) ? '#' : '.';
        fixed[f / CH] = lg[f] > 20 * 256 ? '#' : '.';           /* 고정 임계: 평균 파워 > 2^20 */
    }
    vad(4, fast); vad(7, slow);
    printf("log2 power: silence %.2f  speech %.2f  fan %.2f\n", lg[50] / 256.0, lg[100] / 256.0, lg[300] / 256.0);
    printf("truth      %s\nfixed      %s\nfloor 1/16 %s\nfloor 1/128%s\n", truth, fixed, fast, slow);
    printf("%s\n", "           0s                  1s                  2s          ^fan    3s                  4s");
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 vad.c -o vad -lm && ./vad
```

```text
log2 power: silence 13.58  speech 22.56  fan 21.00
truth      ................############............########................................
fixed      ................############............########....############################
floor 1/16 ................####....................####........####........................
floor 1/128................#############...........#########...##########################..
           0s                  1s                  2s          ^fan    3s                  4s
```

출력에서 볼 것 (한 글자 = 50 ms):

- log2 파워가 조용할 때 13.6, 말소리 22.6, 팬 21.0이다. 팬 소음이 말소리와 겨우 1.6 (log2) ≈ 5 dB 차이다.
- **고정 임계**(`fixed`)는 조용한 방에선 완벽하지만 팬이 켜지면 **영원히 "말소리"** 라고 한다.
- **floor를 빨리 따라가면**(1/16) 팬엔 금방 적응하지만, 긴 말소리 도중에 floor가 따라 올라와 **말을 200 ms만 잡고 잘라 버린다**.
- **floor를 천천히 따라가면**(1/128) 말은 온전히 잡지만 팬에 적응하는 데 **약 1.3초**가 걸린다.

이것이 energy VAD의 본질적인 한계다. "크다 = 말소리"라는 가정은 팬, 차 소리, 음악, 다른 사람의 목소리, 옷 스치는 소리(웨어러블에서 특히!) 앞에서 무너진다. 적응 속도 하나로는 두 요구를 동시에 만족할 수 없다.

### 2.3 더 나은 고전 VAD와 ML VAD

- **band 에너지 + 통계 모델**: 말소리는 특정 band(대략 300 Hz ~ 4 kHz)에 에너지가 몰리고 배음 구조(pitch)가 있다. WebRTC의 고전 VAD는 여러 subband 에너지에 GMM(Gaussian mixture model)을 써서 speech/noise likelihood를 비교하는 방식으로 알려져 있다. 여전히 가볍다.
- **작은 신경망 VAD**: log-mel 몇 frame을 입력으로 받는 작은 CNN 또는 GRU(B3)가 frame마다 "speech 확률"을 낸다. 공개 모델로 **Silero VAD**가 널리 쓰이는데, 수십 ms 단위 청크마다 확률을 내는 작은 신경망이고 CPU에서 가볍게 돈다는 정도만 기억하자(세부 구조·크기는 버전마다 다르니 쓸 때 확인한다). GRU 기반이면 hidden state를 frame 사이에 들고 다니는 **stateful streaming**이 된다(B3, 9.3절).
- **HW VAD**: 일부 MEMS 마이크·오디오 코덱은 칩 안에 초저전력 acoustic activity detector를 넣어, 조용할 땐 SoC/MCU를 아예 재운다. 이 경우 "VAD 알고리즘"은 레지스터 몇 개(임계값, hold time) 설정이 된다 — Don의 G2(센서 wake-on-motion)와 똑같은 패턴이다.

| VAD 종류 | 크기 | 잡음 강인성 | 전력 | 비고 |
|---|---|---|---|---|
| energy + floor (위 C 코드) | 수십 byte state | 약함 | 최소 | 조용한 환경의 1차 문지기 |
| subband + GMM (WebRTC 방식) | 수 KB | 중간 | 작음 | 고전, 튜닝 쉬움 |
| 작은 CNN/GRU (Silero 계열) | 수십 KB ~ 수 MB (typical) | 강함 | 중간 | log-mel 필요, NPU/DSP |
| 마이크 내장 HW detector | 0 (레지스터) | 약함 | 가장 작음 | 임계·hold time만 설정 |

---

## 3. KWS (keyword spotting) / wake word

### 3.1 문제 정의

**KWS**는 짧은 오디오 창(보통 1초)이 "정해진 단어" 중 무엇인지(또는 아무것도 아닌지) 분류하는 문제다. wake word는 그중 단어 하나("Hey Hark")만 찾는 특수한 경우다. 입력은 1초의 log-mel 또는 MFCC, 출력은 class별 확률(softmax, A2 6절)이다.

학술 벤치마크로는 Google **Speech Commands** 데이터셋의 "12-class"(단어 10개 + silence + unknown) 설정이 흔하다. 제품 wake word는 이보다 훨씬 어렵다: 하나의 단어를 수천 명의 목소리·억양·잡음·거리에서 잡되, 하루 종일 흘러가는 대화에서는 **거의 절대 반응하지 않아야** 한다(A2 9절, A4 9절).

### 3.2 DS-CNN — Hello Edge (Zhang et al., 2017)

ARM 연구진의 논문 "Hello Edge: Keyword Spotting on Microcontrollers"(Zhang, Suda, Lai, Chandra, 2017)는 DNN, CNN, LSTM, CRNN, **DS-CNN**(depthwise separable CNN) 등을 **MCU 메모리·연산 예산**에서 비교했고, DS-CNN이 같은 예산에서 가장 좋은 정확도를 보였다. 이 노트가 기억하는 핵심 설정:

- 입력: 40 ms frame, 20 ms hop으로 1초 → 49 frame, frame당 MFCC 10개 → **49 × 10** "이미지"
- 첫 층: 일반 conv(64 채널, 10 × 4 kernel, stride 2), 이어서 depthwise 3 × 3 + pointwise 1 × 1 블록 4개(B2), global average pooling, FC → 12 class
- 결론: 작은 DS-CNN이 Speech Commands 12-class에서 약 94~95% 정확도를 수십 KB 모델로 달성

직접 만들어 파라미터와 MAC를 세 보자. conv의 MAC는 "출력 원소 수 × (입력 채널/groups × kH × kW)"다(B2).

```python
import torch, torch.nn as nn
def ds_block(c):                                   # depthwise 3x3 + pointwise 1x1 (B2)
    return nn.Sequential(nn.Conv2d(c, c, 3, padding=1, groups=c), nn.BatchNorm2d(c), nn.ReLU(),
                         nn.Conv2d(c, c, 1), nn.BatchNorm2d(c), nn.ReLU())
class DSCNN(nn.Module):                            # Hello Edge DS-CNN-S 모양 (64ch, DS 4개)
    def __init__(self, c=64, n_cls=12):
        super().__init__()
        self.stem = nn.Sequential(nn.Conv2d(1, c, (10, 4), stride=2, padding=(5, 1)),
                                  nn.BatchNorm2d(c), nn.ReLU())
        self.ds = nn.Sequential(*[ds_block(c) for _ in range(4)])
        self.fc = nn.Linear(c, n_cls)
    def forward(self, x):                          # x: [B, 1, 49 frames, 10 MFCC]
        x = self.ds(self.stem(x))
        return self.fc(x.mean(dim=(2, 3)))         # global average pooling
m = DSCNN().eval()
macs = {}
def hook(mod, inp, out):                           # conv MAC = 출력 원소 수 × (Cin/groups × kH × kW)
    k = mod.in_channels // mod.groups * mod.kernel_size[0] * mod.kernel_size[1]
    macs[mod] = out.numel() * k
for mod in m.modules():
    if isinstance(mod, nn.Conv2d): mod.register_forward_hook(hook)
x = torch.randn(1, 1, 49, 10)
print("stem out:", tuple(m.stem(x).shape), " logits:", tuple(m(x).shape))
total_p = sum(p.numel() for p in m.parameters())
bn_p = sum(p.numel() for mod in m.modules() if isinstance(mod, nn.BatchNorm2d) for p in mod.parameters())
print(f"params total      : {total_p:,}  (BN folding 후 {total_p - bn_p:,})")
dw = sum(v for k, v in macs.items() if k.groups > 1); pw = sum(v for k, v in macs.items() if k.kernel_size == (1, 1))
stem = sum(macs.values()) - dw - pw; fc = 64 * 12
print(f"MACs stem/dw/pw/fc: {stem:,} / {dw:,} / {pw:,} / {fc:,}")
print(f"MACs total        : {sum(macs.values()) + fc:,}  (~{2 * (sum(macs.values()) + fc) / 1e6:.1f} M ops)")
print(f"int8 weights ~{(total_p - bn_p) / 1024:.1f} KB, largest activation 64x25x5 int8 = {64 * 25 * 5 / 1024:.1f} KB")
```

```text
stem out: (1, 64, 25, 5)  logits: (1, 12)
params total      : 23,756  (BN folding 후 22,604)
MACs stem/dw/pw/fc: 320,000 / 288,000 / 2,048,000 / 768
MACs total        : 2,656,768  (~5.3 M ops)
int8 weights ~22.1 KB, largest activation 64x25x5 int8 = 7.8 KB
```

출력에서 볼 것:

- 손계산 확인: stride 2로 49 × 10 → 25 × 5. stem MAC = `25·5·64 × (1·10·4)` = 125 · 64 · 40 = **320,000**. pointwise 한 층 = `125 · 64 × 64` = 512,000, 4층이면 2,048,000. depthwise 한 층 = `125 · 64 × 9` = 72,000, 4층이면 288,000. 연산의 **77%가 1×1 pointwise**다 — MobileNet과 같은 패턴(B2).
- 가중치는 int8로 약 22 KB, 가장 큰 activation은 7.8 KB. **"수십 KB"라는 KWS 크기 감각**이 여기서 나온다. 논문 표의 DS-CNN small 행은 약 5.4 M ops와 38.6 KB "memory"로 기억하는데, 이 코드의 ~5.3 M ops와 맞고, memory 열은 가중치 + activation 버퍼를 합친 값으로 보면 대략 맞는다(22 + 2 × 7.8 ≈ 38). 정확한 정의는 논문 표를 직접 확인하자.
- BN은 배포 전에 conv에 접어 넣는다(B1 BN folding). 파라미터 1,152개가 사라지고 연산도 준다.

### 3.3 직접 학습해 보기 — 합성 "Hey Hark"

진짜 음성 데이터 없이 구조를 체험하기 위해, **올라가는 chirp 두 개**(두 음절, 음높이는 화자마다 ±15%)를 호출어로 정한다. 비호출어는 헷갈리게 5종류를 만든다: 첫 음절만, 내려가는 chirp, 떨리는 tone, 잡음만, 그리고 **둘째 음절이 다른 "비슷한 말"**. 잡음 크기와 소리 크기를 넓게 흔들어 SNR이 낮은 샘플도 섞는다. 아래 파일을 `kwsdata.py`로 저장한다.

```python
# kwsdata.py — 합성 KWS 데이터 (다음 두 예제가 import 한다)
import numpy as np, torch, torchaudio
FS = 16000
MEL = torchaudio.transforms.MelSpectrogram(FS, n_fft=512, win_length=400, hop_length=160,
                                           n_mels=40, center=False)
def chirp(f0, f1, dur):                           # 선형 chirp, Hann envelope
    t = np.arange(int(dur * FS)) / FS
    return np.sin(2 * np.pi * (f0 * t + (f1 - f0) / (2 * dur) * t**2)) * np.hanning(len(t))
def keyword(rng):                                 # "hey-hark" 흉내: 올라가는 chirp 2개 (음절 2개)
    s = rng.uniform(0.85, 1.15)                   # 화자마다 음높이가 다르다
    d = rng.uniform(0.15, 0.22)
    return np.concatenate([chirp(400*s, 1200*s, d), np.zeros(int(0.05*FS)), chirp(600*s, 1800*s, d)])
def non_keyword(rng):                             # 헷갈리게 만드는 5종류
    k, s = rng.integers(5), rng.uniform(0.85, 1.15)
    d = rng.uniform(0.15, 0.22)
    if k == 0: return chirp(400*s, 1200*s, d)                     # 첫 음절만 ("hey"만)
    if k == 1: return np.concatenate([chirp(1200*s, 400*s, d), chirp(1800*s, 600*s, d)])  # 내려가는 chirp
    if k == 2: return chirp(900*s, 900*s, 2*d) * (1 + 0.5*np.sin(2*np.pi*6*np.arange(int(2*d*FS))/FS))  # 떨리는 tone
    if k == 3: return np.zeros(int(0.3 * FS))                     # 잡음만
    return np.concatenate([chirp(400*s, 1200*s, d), np.zeros(int(0.05*FS)), chirp(900*s, 1300*s, d)])  # 비슷한 말
def clip(rng, pos, n=FS):                         # 1초 윈도우에 무작위 위치로 넣고 잡음 추가
    x = rng.normal(0, rng.uniform(0.01, 0.3), n)
    ev = (keyword(rng) if pos else non_keyword(rng)) * rng.uniform(0.1, 1.0)
    st = rng.integers(0, n - len(ev)); x[st:st+len(ev)] += ev
    return x
def feats(x):                                     # [..., n] → log-mel [..., 1, 40, 97]
    return torch.log(MEL(torch.as_tensor(x, dtype=torch.float32)) + 1e-6).unsqueeze(-3)
```

이제 작은 DS-CNN 스타일 모델(파라미터 약 2.5천 개)을 3,000개로 학습하고 1,000개로 평가한다. 정규화 통계(μ, σ)는 train에서만 구해 test에 그대로 쓴다(A2 4.4절).

```python
import time, numpy as np, torch, torch.nn as nn
from kwsdata import clip, feats
torch.manual_seed(0); torch.set_num_threads(4); rng = np.random.default_rng(0)
def make(n):
    y = rng.integers(0, 2, n)
    return feats(np.stack([clip(rng, p) for p in y])), torch.tensor(y)
Xtr, ytr = make(3000); Xte, yte = make(1000)
mu, sd = Xtr.mean(), Xtr.std(); Xtr, Xte = (Xtr - mu) / sd, (Xte - mu) / sd
def dws(ci, co, s): return [nn.Conv2d(ci, ci, 3, s, 1, groups=ci), nn.ReLU(), nn.Conv2d(ci, co, 1), nn.BatchNorm2d(co), nn.ReLU()]
model = nn.Sequential(nn.Conv2d(1, 16, 3, 2, 1), nn.BatchNorm2d(16), nn.ReLU(),
                      *dws(16, 32, 2), *dws(32, 32, 1), nn.AdaptiveAvgPool2d(1), nn.Flatten(), nn.Linear(32, 2))
print("input", tuple(Xtr.shape[1:]), "params", sum(p.numel() for p in model.parameters()))
opt = torch.optim.Adam(model.parameters(), 3e-3); t0 = time.time()
for ep in range(5):
    model.train()
    for i in torch.randperm(3000).split(64):
        loss = nn.functional.cross_entropy(model(Xtr[i]), ytr[i]); opt.zero_grad(); loss.backward(); opt.step()
    model.eval()
    with torch.no_grad(): p = model(Xte).softmax(1)[:, 1]
    print(f"epoch {ep} loss {loss.item():.3f} test acc {((p > 0.5).long() == yte).float().mean():.3f}")
print(f"train time {time.time() - t0:.1f} s")
print(" thr   FRR    FPR/window")
for thr in (0.2, 0.3, 0.5, 0.7, 0.8, 0.9):
    print(f"{thr:4.2f}  {(p[yte == 1] < thr).float().mean():.3f}  {(p[yte == 0] >= thr).float().mean():.3f}")
torch.save({"model": model.state_dict(), "mu": mu, "sd": sd}, "kws.pt")
```

```text
input (1, 40, 97) params 2466
epoch 0 loss 0.498 test acc 0.794
epoch 1 loss 0.291 test acc 0.855
epoch 2 loss 0.265 test acc 0.916
epoch 3 loss 0.163 test acc 0.951
epoch 4 loss 0.128 test acc 0.959
train time 31.6 s
 thr   FRR    FPR/window
0.20  0.015  0.130
0.30  0.028  0.070
0.50  0.068  0.017
0.70  0.123  0.000
0.80  0.183  0.000
0.90  0.316  0.000
```

출력에서 볼 것 (학습 시간은 기기마다 다르다):

- 파라미터 2,466개짜리 모델이 CPU에서 30여 초 만에 test accuracy 95.9%에 도달한다. 입력은 `[1, 40 mel, 97 frame]` "흑백 이미지"다.
- **threshold를 올리면** 놓치는 호출어(FRR)가 1.5% → 31.6%로 늘고, 잘못 반응하는 창(FPR)은 13% → 0%로 준다. A4 9절의 DET trade-off 그대로다.
- "FPR 0.000"은 **음성 창 약 500개 중 0개**라는 뜻일 뿐이다. A2 9절 계산대로 하루 1회 오경보 예산은 창당 약 10⁻⁶이다. 500개로는 그 근처도 증명할 수 없다(A4 9.2절 rule of three: 최소 72시간 분량의 배경 오디오가 필요).

### 3.4 Streaming 검출 — sliding window, smoothing, threshold, refractory

기기에서는 "잘라 놓은 1초 clip"이 오지 않는다. 끝없는 스트림에 **1초 창을 100 ms마다 밀면서** 판정한다. 그러면 호출어 하나가 창 여러 개에 걸쳐 잡히므로 후처리가 필요하다.

- **posterior smoothing**: 최근 N개 창의 확률을 평균한다(causal moving average). 한 창짜리 우연한 spike를 누른다. 대가는 지연(N·hop의 절반쯤).
- **threshold**: smoothing된 확률이 임계를 넘으면 trigger.
- **refractory period(불응기)**: trigger 후 일정 시간(예: 1초) 동안은 다시 trigger하지 않는다. 인터럽트 핸들러에서 같은 이벤트로 여러 번 들어오지 않게 막는 것과 같다.

앞에서 저장한 모델로 20초 스트림(호출어 3번, "비슷한 말" 3번, 잡음)을 처리해 본다.

```python
import numpy as np, torch, torch.nn as nn
from kwsdata import FS, keyword, chirp, feats
rng = np.random.default_rng(1)
x = rng.normal(0, 0.1, 20 * FS)                          # 20초 스트림, 배경 잡음
events = [(2.0, 1), (5.0, 0), (8.0, 1), (11.0, 0), (13.0, 0), (16.0, 1)]  # (시각 s, keyword?)
for t, pos in events:
    ev = keyword(rng) if pos else np.concatenate([chirp(400, 1200, .2), np.zeros(800), chirp(700, 1600, .2)])  # 비슷한 말
    x[int(t * FS): int(t * FS) + len(ev)] += 0.6 * ev
def dws(ci, co, s): return [nn.Conv2d(ci, ci, 3, s, 1, groups=ci), nn.ReLU(), nn.Conv2d(ci, co, 1), nn.BatchNorm2d(co), nn.ReLU()]
model = nn.Sequential(nn.Conv2d(1, 16, 3, 2, 1), nn.BatchNorm2d(16), nn.ReLU(),
                      *dws(16, 32, 2), *dws(32, 32, 1), nn.AdaptiveAvgPool2d(1), nn.Flatten(), nn.Linear(32, 2))
ck = torch.load("kws.pt"); model.load_state_dict(ck["model"]); model.eval()
hop = FS // 10                                             # 100 ms마다 1초 윈도우 판정
starts = range(0, len(x) - FS + 1, hop)
win = np.stack([x[s:s + FS] for s in starts])
with torch.no_grad(): post = model((feats(win) - ck["mu"]) / ck["sd"]).softmax(1)[:, 1].numpy()
t_end = (np.array(starts) + FS) / FS                       # 윈도우가 끝나는 시각 = 판정 시각
smooth = np.convolve(post, np.ones(3) / 3, mode="full")[:len(post)]   # causal 이동평균 (3개 = 300 ms)
def detect(p, thr, refractory_s):
    out, last = [], -1e9
    for t, v in zip(t_end, p):
        if v >= thr and t - last >= refractory_s: out.append(round(float(t), 1)); last = t
    return out
print("windows judged:", len(post), " windows with raw p>=0.5:", int((post >= 0.5).sum()))
d = detect(post, 0.5, 0); print("raw, thr 0.5, no refractory  :", len(d), "triggers, first 9 =", d[:9])
print("raw, thr 0.5, refractory 1 s :", detect(post, 0.5, 1.0))
print("raw, thr 0.7, refractory 1 s :", detect(post, 0.7, 1.0))
print("smooth3, thr 0.7, refr 1 s   :", detect(smooth, 0.7, 1.0))
print("keyword at", [t for t, p in events if p], " look-alike at", [t for t, p in events if not p])
np.save("post.npy", np.stack([t_end, post, smooth]))
```

```text
windows judged: 191  windows with raw p>=0.5: 40
raw, thr 0.5, no refractory  : 40 triggers, first 9 = [2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 3.0, 3.1, 3.2]
raw, thr 0.5, refractory 1 s : [2.4, 5.5, 8.4, 11.6, 13.6, 16.4]
raw, thr 0.7, refractory 1 s : [2.5, 8.4, 16.5]
smooth3, thr 0.7, refr 1 s   : [2.6, 8.5, 16.6]
keyword at [2.0, 8.0, 16.0]  look-alike at [5.0, 11.0, 13.0]
```

```svg
<svg viewBox="0 0 640 290" xmlns="http://www.w3.org/2000/svg">
<rect x="88.3" y="20" width="29.5" height="200" fill="#3f9a6b" fill-opacity="0.15"/>
<rect x="265.2" y="20" width="29.5" height="200" fill="#3f9a6b" fill-opacity="0.15"/>
<rect x="500.9" y="20" width="29.5" height="200" fill="#3f9a6b" fill-opacity="0.15"/>
<rect x="176.7" y="20" width="29.5" height="200" fill="#d0564a" fill-opacity="0.12"/>
<rect x="353.6" y="20" width="29.5" height="200" fill="#d0564a" fill-opacity="0.12"/>
<rect x="412.5" y="20" width="29.5" height="200" fill="#d0564a" fill-opacity="0.12"/>
<line x1="50" y1="220" x2="610" y2="220" stroke="currentColor"/>
<line x1="50" y1="20" x2="50" y2="220" stroke="currentColor"/>
<line x1="50" y1="80.0" x2="610" y2="80.0" stroke="#e08a3c" stroke-dasharray="5 4"/>
<polyline fill="none" stroke="#888" stroke-width="1.2" points="50.0,217.4 52.9,216.9 55.9,217.6 58.8,217.3 61.8,217.3 64.7,217.4 67.7,217.5 70.6,217.6 73.6,217.6 76.5,217.6 79.5,217.5 82.4,214.3 85.4,205.4 88.3,200.2 91.3,104.8 94.2,33.0 97.2,31.6 100.1,35.6 103.1,30.7 106.0,36.2 108.9,33.9 111.9,45.7 114.8,114.7 117.8,197.7 120.7,216.3 123.7,218.1 126.6,217.9 129.6,218.0 132.5,217.9 135.5,218.1 138.4,217.9 141.4,218.0 144.3,217.6 147.3,217.8 150.2,217.3 153.2,217.9 156.1,217.2 159.1,217.8 162.0,217.4 164.9,217.8 167.9,217.6 170.8,213.9 173.8,191.1 176.7,198.5 179.7,147.2 182.6,92.7 185.6,83.8 188.5,91.2 191.5,80.0 194.4,88.7 197.4,95.8 200.3,157.2 203.3,193.6 206.2,213.2 209.2,215.4 212.1,217.4 215.1,216.8 218.0,217.5 220.9,217.3 223.9,217.2 226.8,217.5 229.8,217.4 232.7,217.8 235.7,217.6 238.6,217.8 241.6,217.6 244.5,217.6 247.5,217.7 250.4,217.4 253.4,217.6 256.3,217.4 259.3,216.4 262.2,175.4 265.2,136.1 268.1,24.0 271.1,23.2 274.0,23.5 276.9,23.2 279.9,23.8 282.8,23.7 285.8,24.6 288.7,56.0 291.7,84.7 294.6,188.8 297.6,217.3 300.5,217.4 303.5,217.4 306.4,217.4 309.4,217.1 312.3,217.3 315.3,217.4 318.2,217.7 321.2,217.3 324.1,217.7 327.1,217.7 330.0,217.5 332.9,217.7 335.9,217.3 338.8,217.3 341.8,217.2 344.7,217.8 347.7,213.6 350.6,199.8 353.6,200.4 356.5,158.5 359.5,121.0 362.4,95.4 365.4,119.0 368.3,94.3 371.3,119.8 374.2,101.3 377.2,156.8 380.1,198.7 383.1,215.9 386.0,216.1 388.9,217.5 391.9,217.1 394.8,217.5 397.8,217.3 400.7,217.7 403.7,217.2 406.6,213.8 409.6,194.7 412.5,200.7 415.5,161.5 418.4,133.5 421.4,106.4 424.3,130.2 427.3,112.7 430.2,129.9 433.2,133.4 436.1,164.1 439.1,206.4 442.0,215.9 444.9,216.6 447.9,217.4 450.8,217.9 453.8,217.2 456.7,217.8 459.7,217.4 462.6,217.7 465.6,217.3 468.5,217.8 471.5,217.7 474.4,218.1 477.4,217.8 480.3,218.1 483.3,217.7 486.2,218.0 489.2,217.5 492.1,218.0 495.1,215.3 498.0,199.7 500.9,206.5 503.9,80.8 506.8,40.1 509.8,31.8 512.7,40.6 515.7,32.7 518.6,40.8 521.6,34.0 524.5,77.9 527.5,113.8 530.4,190.0 533.4,215.8 536.3,217.5 539.3,217.4 542.2,217.5 545.2,217.6 548.1,217.7 551.1,217.7 554.0,217.6 556.9,217.7 559.9,217.7 562.8,217.8 565.8,217.8 568.7,217.8 571.7,217.9 574.6,217.7 577.6,217.9 580.5,217.8 583.5,217.8 586.4,217.3 589.4,217.7 592.3,217.6 595.3,217.8 598.2,218.0 601.2,217.8 604.1,217.9 607.1,217.6 610.0,217.8"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="2" points="50.0,219.1 52.9,218.1 55.9,217.3 58.8,217.3 61.8,217.4 64.7,217.3 67.7,217.4 70.6,217.5 73.6,217.6 76.5,217.6 79.5,217.6 82.4,216.5 85.4,212.4 88.3,206.6 91.3,170.1 94.2,112.6 97.2,56.4 100.1,33.4 103.1,32.6 106.0,34.2 108.9,33.6 111.9,38.6 114.8,64.8 117.8,119.4 120.7,176.3 123.7,210.7 126.6,217.5 129.6,218.0 132.5,218.0 135.5,218.0 138.4,218.0 141.4,218.0 144.3,217.8 147.3,217.8 150.2,217.6 153.2,217.7 156.1,217.5 159.1,217.6 162.0,217.5 164.9,217.7 167.9,217.6 170.8,216.4 173.8,207.5 176.7,201.2 179.7,178.9 182.6,146.1 185.6,107.9 188.5,89.2 191.5,85.0 194.4,86.7 197.4,88.2 200.3,113.9 203.3,148.9 206.2,188.0 209.2,207.4 212.1,215.4 215.1,216.6 218.0,217.3 220.9,217.2 223.9,217.3 226.8,217.3 229.8,217.4 232.7,217.6 235.7,217.6 238.6,217.7 241.6,217.7 244.5,217.7 247.5,217.6 250.4,217.6 253.4,217.6 256.3,217.5 259.3,217.1 262.2,203.1 265.2,176.0 268.1,111.8 271.1,61.1 274.0,23.6 276.9,23.3 279.9,23.5 282.8,23.6 285.8,24.0 288.7,34.8 291.7,55.1 294.6,109.9 297.6,163.6 300.5,207.9 303.5,217.4 306.4,217.4 309.4,217.3 312.3,217.3 315.3,217.3 318.2,217.5 321.2,217.5 324.1,217.6 327.1,217.6 330.0,217.7 332.9,217.6 335.9,217.5 338.8,217.5 341.8,217.3 344.7,217.5 347.7,216.2 350.6,210.4 353.6,204.6 356.5,186.2 359.5,160.0 362.4,125.0 365.4,111.8 368.3,102.9 371.3,111.0 374.2,105.1 377.2,126.0 380.1,152.3 383.1,190.5 386.0,210.3 388.9,216.5 391.9,216.9 394.8,217.4 397.8,217.3 400.7,217.5 403.7,217.4 406.6,216.2 409.6,208.6 412.5,203.1 415.5,185.6 418.4,165.2 421.4,133.8 424.3,123.4 427.3,116.4 430.2,124.3 433.2,125.3 436.1,142.5 439.1,168.0 442.0,195.5 444.9,213.0 447.9,216.6 450.8,217.3 453.8,217.5 456.7,217.6 459.7,217.5 462.6,217.7 465.6,217.5 468.5,217.6 471.5,217.6 474.4,217.8 477.4,217.8 480.3,218.0 483.3,217.9 486.2,217.9 489.2,217.7 492.1,217.8 495.1,216.9 498.0,211.0 500.9,207.2 503.9,162.3 506.8,109.1 509.8,50.9 512.7,37.5 515.7,35.1 518.6,38.1 521.6,35.9 524.5,50.9 527.5,75.2 530.4,127.2 533.4,173.2 536.3,207.8 539.3,216.9 542.2,217.5 545.2,217.5 548.1,217.6 551.1,217.7 554.0,217.7 556.9,217.7 559.9,217.7 562.8,217.7 565.8,217.7 568.7,217.8 571.7,217.9 574.6,217.8 577.6,217.9 580.5,217.8 583.5,217.8 586.4,217.6 589.4,217.6 592.3,217.5 595.3,217.7 598.2,217.8 601.2,217.9 604.1,217.9 607.1,217.8 610.0,217.8"/>
<text x="44" y="224.0" font-size="12" text-anchor="end">0</text>
<text x="44" y="124.0" font-size="12" text-anchor="end">0.5</text>
<text x="44" y="24.0" font-size="12" text-anchor="end">1</text>
<text x="50.0" y="236" font-size="12" text-anchor="middle">1s</text>
<text x="167.9" y="236" font-size="12" text-anchor="middle">5s</text>
<text x="315.3" y="236" font-size="12" text-anchor="middle">10s</text>
<text x="462.6" y="236" font-size="12" text-anchor="middle">15s</text>
<text x="610.0" y="236" font-size="12" text-anchor="middle">20s</text>
<text x="70.6" y="76.0" font-size="12">threshold 0.7</text>
<text x="50" y="260" font-size="12">회색 = raw posterior · 파랑 = 3개 이동평균 · 초록 칸 = keyword · 빨강 칸 = 비슷한 말</text>
<text x="50" y="278" font-size="12">(칸은 윈도우 끝 시각 기준 대략의 위치)</text>
</svg>
```

그림 4 — 위 코드가 계산한 20초 스트림의 posterior. 회색은 창별 raw 확률, 파랑은 3개 이동평균, 점선은 threshold 0.7. 진짜 호출어(초록)는 0.9 이상 plateau, "비슷한 말"(빨강)은 0.5~0.7 근처 plateau를 만든다.

출력과 그림에서 볼 것:

- **refractory가 없으면** 호출어 하나가 창 9개에 걸쳐 잡혀서 20초에 40번 trigger한다. 1초 refractory를 넣으면 이벤트당 1번이 된다.
- threshold 0.5에서는 "비슷한 말" 3개도 전부 trigger한다(오경보 3). 0.7로 올리면 호출어 3개만 남는다. 이 스트림에서는 threshold만으로 충분했고, smoothing은 판정을 0.1~0.2 s 늦추는 대신 한 창짜리 spike에 대한 보험 역할을 한다. 어느 쪽이 나은지는 **긴 배경 오디오에서 FA/hour를 재서** 정한다.
- 판정 시각(2.4~2.6 s)은 호출어 시작(2.0 s)보다 0.4~0.6 s 늦다. 창 끝이 호출어 끝을 지나야 판정이 가능하기 때문이다. 이것이 "wake word 지연"의 주성분이다.

### 3.5 평가 — FRR과 FA per hour (A4 복습)

A4 9절의 정의를 그대로 쓴다.

- **FRR** = 놓친 호출어 발화 / 전체 호출어 발화. 조용한 방, 잡음, 먼 거리, 여러 억양 조건별로 따로 잰다.
- **FA/hour** = 오경보 횟수 / 호출어가 없는 배경 오디오 시간. 위 코드처럼 **refractory를 적용한 이벤트 단위**로 센다(창 단위로 세면 수치가 부풀려진다).
- **동작점**: 제품 요구는 보통 "FA ≤ x회/24h에서 FRR ≤ y%"의 형태다. DET 곡선에서 그 점을 찾는다.

현실 체크: 긴 배경 오디오(TV, 대화, 팟캐스트, 음악)를 수십~수백 시간 확보해야 하고, 웨어러블은 **착용자 자신의 목소리**(골전도·가까운 입)와 옷 스침이 특이한 음성 분포를 만든다. 스마트 스피커 데이터로 만든 모델을 그대로 가져오면 분포가 다르다(A4 3절 data shift).

### 3.6 2단 검증 (cascade inside KWS)

한 단계로 FA 10⁻⁶/창을 맞추기는 어렵다. 그래서 흔한 구조는:

```
1단 (저전력 DSP/MCU, 수십 KB): 낮은 threshold로 거의 다 통과 → 높은 recall
          │ trigger (예: 시간당 수 회)
          ▼
2단 (SoC 깨움, 수백 KB ~ 수 MB): 더 큰 모델 + 더 긴 문맥으로 재검증
          │ 통과
          ▼
ASR 시작 (+ 필요하면 cloud에서 3단 재검증)
```

A2 9.2절 계산처럼 각 단의 FPR이 곱해진다(독립 가정은 낙관적). 대신 1단 FA마다 SoC가 잠깐 깨는 전력 비용이 든다 — 8절에서 숫자로 본다.

---

## 4. ASR — 음성을 텍스트로

### 4.1 핵심 난제: 길이가 안 맞는다

1초 음성은 100 frame인데 "hello"는 5글자다. 어느 frame이 어느 글자에 해당하는지(**alignment**)는 학습 데이터에 적혀 있지 않다. 사람이 frame마다 라벨을 달 수는 없다. 현대 end-to-end ASR의 세 갈래는 이 문제를 푸는 방법이 다르다.

| 방식 | alignment를 푸는 법 | 한 줄 |
|---|---|---|
| CTC | blank 토큰 + 가능한 모든 정렬을 합산 | frame마다 독립적으로 글자(또는 blank)를 낸다 |
| RNN-T (Transducer) | CTC + 이전 출력 토큰을 보는 predictor | streaming의 표준 |
| Attention encoder-decoder (AED) | decoder가 cross-attention으로 알아서 본다 | Whisper. 정확하지만 streaming이 어렵다 |

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="a4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="110" y="20" font-size="14" text-anchor="middle">CTC</text>
<text x="340" y="20" font-size="14" text-anchor="middle">RNN-T</text>
<text x="570" y="20" font-size="14" text-anchor="middle">Encoder-Decoder (Whisper)</text>
<rect x="40" y="200" width="140" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/>
<text x="110" y="225" font-size="13" text-anchor="middle">Encoder</text>
<rect x="40" y="120" width="140" height="40" rx="6" fill="#888" fill-opacity="0.15" stroke="currentColor"/>
<text x="110" y="138" font-size="12" text-anchor="middle">Linear + softmax</text>
<text x="110" y="154" font-size="12" text-anchor="middle">(글자 + blank)</text>
<rect x="40" y="50" width="140" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/>
<text x="110" y="75" font-size="12" text-anchor="middle">collapse → 텍스트</text>
<line x1="110" y1="200" x2="110" y2="162" stroke="currentColor" marker-end="url(#a4)"/>
<line x1="110" y1="120" x2="110" y2="92" stroke="currentColor" marker-end="url(#a4)"/>
<text x="110" y="262" font-size="12" text-anchor="middle">log-mel frames</text>
<line x1="110" y1="250" x2="110" y2="242" stroke="currentColor"/>
<rect x="250" y="200" width="80" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/>
<text x="290" y="225" font-size="13" text-anchor="middle">Encoder</text>
<rect x="350" y="200" width="80" height="40" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/>
<text x="390" y="225" font-size="13" text-anchor="middle">Predictor</text>
<rect x="290" y="120" width="100" height="40" rx="6" fill="#888" fill-opacity="0.15" stroke="currentColor"/>
<text x="340" y="138" font-size="12" text-anchor="middle">Joiner</text>
<text x="340" y="154" font-size="12" text-anchor="middle">+ softmax</text>
<rect x="290" y="50" width="100" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/>
<text x="340" y="67" font-size="12" text-anchor="middle">토큰 또는 blank</text>
<text x="340" y="83" font-size="12" text-anchor="middle">(blank면 다음 frame)</text>
<line x1="290" y1="200" x2="325" y2="162" stroke="currentColor" marker-end="url(#a4)"/>
<line x1="390" y1="200" x2="355" y2="162" stroke="currentColor" marker-end="url(#a4)"/>
<line x1="340" y1="120" x2="340" y2="92" stroke="currentColor" marker-end="url(#a4)"/>
<path d="M392,70 C450,70 450,270 395,258 L392,244" fill="none" stroke="#d0564a" stroke-dasharray="4 3" marker-end="url(#a4)"/>
<text x="468" y="186" font-size="12" text-anchor="middle">직전 토큰</text>
<text x="290" y="262" font-size="12" text-anchor="middle">log-mel</text>
<rect x="490" y="200" width="160" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/>
<text x="570" y="218" font-size="12" text-anchor="middle">Encoder (30 s 통째로)</text>
<text x="570" y="233" font-size="12" text-anchor="middle">conv 2층 + Transformer</text>
<rect x="490" y="120" width="160" height="40" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/>
<text x="570" y="138" font-size="12" text-anchor="middle">Decoder (autoregressive)</text>
<text x="570" y="154" font-size="12" text-anchor="middle">cross-attention</text>
<rect x="490" y="50" width="160" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/>
<text x="570" y="75" font-size="12" text-anchor="middle">다음 토큰 (BPE)</text>
<line x1="570" y1="200" x2="570" y2="162" stroke="currentColor" marker-end="url(#a4)"/>
<line x1="570" y1="120" x2="570" y2="92" stroke="currentColor" marker-end="url(#a4)"/>
<path d="M652,70 C672,70 672,140 652,140" fill="none" stroke="#d0564a" stroke-dasharray="4 3" marker-end="url(#a4)"/>
<text x="570" y="262" font-size="12" text-anchor="middle">80 × 3000 log-mel</text>
<text x="20" y="292" font-size="12">빨간 점선 = 자기 출력을 다시 입력으로 (순차 의존). CTC만 frame별 출력이 서로 독립이다.</text>
</svg>
```

그림 5 — ASR 세 갈래의 구조. CTC는 encoder 위에 선형층 하나, RNN-T는 encoder와 predictor(언어 모델 역할)를 joiner로 합치고, encoder-decoder는 decoder가 encoder 출력 전체를 cross-attention으로 본다.

### 4.2 CTC — blank와 collapse 규칙

**CTC(connectionist temporal classification, Graves et al., 2006)** 의 아이디어: encoder가 frame마다 "글자 또는 **blank(`-`, 아무것도 아님)**" 확률을 낸다. frame별 출력 열(path)을 텍스트로 바꾸는 규칙은 두 단계다.

```
① 연속으로 반복된 기호를 하나로 합친다
② blank를 지운다
```

손으로 해 보자. 13 frame path `hh-e-ll-ll-oo`:

```
h h - e - l l - l l - o o
① 반복 합치기 → h - e - l - l - o
② blank 지우기 → h e l l o   = "hello"
```

왜 blank가 필요한가? "hello"에는 l이 두 번 연속이다. blank 없이 `hhellloo`라고 쓰면 ①에서 `l`이 하나로 합쳐져 "helo"가 된다. **같은 글자가 연속으로 두 번 나오려면 사이에 blank가 반드시 있어야 한다.** 그리고 blank는 "이 frame엔 새 글자가 없다"(묵음, 글자 사이 전이)를 표현하는 자리이기도 하다.

**greedy decoding**: frame마다 가장 확률 높은 기호(argmax)를 고른 뒤 collapse한다. 가장 단순하고 빠르다(beam search나 언어 모델을 붙이면 더 정확해진다).

```python
import numpy as np
def ctc_collapse(path, blank="-"):                  # 규칙: ① 연속 중복 합치기 → ② blank 제거
    out, prev = [], None
    for s in path:
        if s != prev and s != blank: out.append(s)
        prev = s
    return "".join(out)
for p in ["hh-e-ll-ll-oo", "hhe-lll-lo", "hhelllloo", "h-e-l-l-o-", "--hello--"]:
    print(f"{p:15s} -> {ctc_collapse(p)}")

# 모델 출력(프레임별 확률)에서 greedy decoding: 프레임마다 argmax → collapse
vocab = ["-", "e", "h", "l", "o"]                   # index 0 = blank
rng = np.random.default_rng(0)
target_path = "hh-e-ll-ll-oo"                       # 13 프레임
logits = rng.normal(0, 1, (len(target_path), len(vocab)))
for t, s in enumerate(target_path): logits[t, vocab.index(s)] += 3.0   # 정답 쪽을 크게
probs = np.exp(logits) / np.exp(logits).sum(1, keepdims=True)
best = "".join(vocab[i] for i in probs.argmax(1))
print("argmax path:", best, " -> ", ctc_collapse(best))
print("frame-wise max prob:", np.round(probs.max(1), 2))
```

```text
hh-e-ll-ll-oo   -> hello
hhe-lll-lo      -> hello
hhelllloo       -> helo
h-e-l-l-o-      -> hello
--hello--       -> helo
argmax path: hh-e-ll-ll-oo  ->  hello
frame-wise max prob: [0.91 0.9  0.83 0.68 0.68 0.8  0.91 0.78 0.81 0.88 0.89 0.66 0.79]
```

출력에서 볼 것: 서로 다른 path 여러 개가 같은 "hello"가 된다. 이것이 CTC의 핵심이다 — 정답 텍스트 하나에 대응하는 **정렬(path)이 매우 많고**, 학습은 그 전부의 확률을 합한 값을 최대화한다. `--hello--`처럼 l 사이에 blank가 없으면 "helo"로 무너진다.

### 4.3 CTC loss — 모든 정렬의 합 (brute force로 확인)

```
P(y | x) = ∑  ∏ₜ p(πₜ | x)       (π = collapse하면 y가 되는 모든 path)
CTC loss = −log P(y | x)
```

말로 하면: "정답으로 collapse되는 모든 frame별 path"의 확률을 다 더한 것이 정답 확률이고, 그 −log가 loss다. frame별 출력이 **서로 독립**이라고 가정하므로 path 확률은 frame 확률의 곱이다. 실제 구현은 동적 계획법(forward-backward, HMM과 같은 구조)으로 O(T·U)에 계산한다. 아주 작은 예에서 전부 나열해서 PyTorch와 비교해 보자.

```python
import itertools, torch
torch.manual_seed(0)
T, V = 4, 3                                          # 4 프레임, vocab = {0:blank, 1:'a', 2:'b'}
logp = torch.randn(T, V).log_softmax(-1)             # 프레임별 log 확률 (모델 출력이라고 치자)
target = [1, 2]                                      # "ab"
def collapse(path):
    out, prev = [], None
    for s in path:
        if s != prev and s != 0: out.append(s)
        prev = s
    return out
good = [p for p in itertools.product(range(V), repeat=T) if collapse(p) == target]
p_sum = sum(torch.exp(sum(logp[t, s] for t, s in enumerate(p))) for p in good)
print("all paths:", V ** T, " paths that collapse to 'ab':", len(good))
print("e.g.", ["".join("-ab"[s] for s in p) for p in good[:6]])
print("brute force  P(ab|x) =", round(p_sum.item(), 6))
loss = torch.nn.functional.ctc_loss(logp.unsqueeze(1), torch.tensor([target]),
        input_lengths=torch.tensor([T]), target_lengths=torch.tensor([2]), blank=0, reduction="sum")
print("torch ctc_loss -> exp(-loss) =", round(torch.exp(-loss).item(), 6))
```

```text
all paths: 81  paths that collapse to 'ab': 15
e.g. ['--ab', '-a-b', '-aab', '-ab-', '-abb', 'a--b']
brute force  P(ab|x) = 0.272651
torch ctc_loss -> exp(-loss) = 0.272651
```

출력에서 볼 것: 4 frame, 기호 3개면 path가 3⁴ = 81개이고 그중 15개가 "ab"로 collapse된다. 15개 확률의 합이 `torch.nn.functional.ctc_loss`와 6자리까지 같다. 실제 ASR은 T가 수백, vocab이 수십~수천이라 나열은 불가능하고 DP로 계산한다.

**CTC의 장단점**: 구조가 단순하고(encoder + 선형층), 출력이 frame별 독립이라 **병렬·streaming이 쉽다**. 대신 그 독립 가정 때문에 "앞에 무슨 글자를 냈는지"를 모른다 — 철자·문법 일관성은 외부 언어 모델(beam search + LM)로 보강하는 경우가 많다.

### 4.4 RNN-T (Transducer) — streaming ASR의 표준

**RNN-T(Graves, 2012)** 는 CTC에 **predictor(prediction network)** 를 붙인다.

- **encoder**: 음향 frame → 음향 표현 (CTC의 encoder와 같다). streaming이면 미래를 조금만(또는 전혀) 보지 않는다.
- **predictor**: 지금까지 출력한 토큰 열 → 표현. 작은 언어 모델 역할. 이름은 RNN-T지만 predictor/encoder가 꼭 RNN일 필요는 없다(LSTM, Conformer, 심지어 embedding 몇 개짜리 stateless predictor도 쓴다).
- **joiner**: 두 표현을 더하고(보통 선형층 + tanh) softmax → "토큰 하나 또는 blank". blank면 **다음 frame으로 넘어가고**, 토큰이면 **같은 frame에서** 토큰을 하나 더 낼 수 있다.

말로 하면: CTC가 frame마다 "무조건 기호 하나"를 내는 것과 달리, RNN-T는 frame과 토큰이라는 두 축의 격자(T × U)를 걸어간다. 이전 토큰을 보므로 CTC의 독립 가정이 풀리고, frame이 들어오는 대로 출력하므로 **자연스럽게 streaming**이다. Google이 2019년에 휴대폰 온디바이스 streaming ASR로 RNN-T를 발표한 이후(He et al., 2019), on-device streaming ASR의 사실상 표준이 되었다.

임베디드 관점의 비용: joiner는 **frame × 출력 후보마다** 돌기 때문에 joiner를 가볍게 만드는 것이 중요하고, predictor는 이전 토큰마다 한 번 도는 순차 연산이다. 학습 시에는 `[B, T, U, vocab]` 크기 텐서가 생겨서 메모리를 많이 먹는다(추론에는 해당 없음).

### 4.5 Attention encoder-decoder — Whisper

**Whisper(Radford et al., 2022, "Robust Speech Recognition via Large-Scale Weak Supervision")** 는 웹에서 모은 약 68만 시간의 (음성, 자막) 쌍으로 학습한 encoder-decoder Transformer다(B4).

- 입력: 16 kHz 오디오를 **30초 단위**로 자르거나 padding → 25 ms window / 10 ms hop의 **80채널 log-mel**(`80 × 3000`). (최신 large-v3는 128채널 mel을 쓴다.)
- encoder: conv1d 2층(두 번째가 stride 2 → 3000 frame이 **1500 토큰**) + sinusoidal 위치 인코딩 + Transformer 블록.
- decoder: 텍스트 토큰(BPE)을 하나씩 생성하는 autoregressive Transformer. cross-attention으로 encoder 출력 1500개 전체를 본다. 특수 토큰으로 언어, 작업(전사/번역), timestamp를 지정한다(multitask).
- 공개 크기(OpenAI 발표 기준): tiny 39M, base 74M, small 244M, medium 769M, large 1550M 파라미터.

구조만 만들어(랜덤 가중치, 다운로드 없음) 파라미터를 세고, 30초 encoder 한 번의 MAC를 대략 계산해 본다. Transformer 한 층의 선형층 MAC는 토큰당 약 `12·d²`(attention 투영 4d² + FFN 8d²), attention 점수 계산은 `2·N²·d`다(B4).

```python
import warnings; warnings.filterwarnings("ignore")
import torch
from transformers import WhisperConfig, WhisperForConditionalGeneration
dims = {"tiny": (384, 4, 6), "base": (512, 6, 8), "small": (768, 12, 12)}   # (d_model, layers, heads)
for name, (d, L, h) in dims.items():
    cfg = WhisperConfig(vocab_size=51865, num_mel_bins=80, d_model=d,
                        encoder_layers=L, decoder_layers=L, encoder_attention_heads=h,
                        decoder_attention_heads=h, encoder_ffn_dim=4 * d, decoder_ffn_dim=4 * d)
    m = WhisperForConditionalGeneration(cfg)          # 랜덤 초기화 — 다운로드 없음, 구조만 센다
    n = sum(p.numel() for p in m.parameters())
    enc = sum(p.numel() for p in m.model.encoder.parameters())
    # 인코더 30초 한 번: 1500 토큰 × (레이어당 선형층 파라미터 ≈ 12·d²) + attention 1500²·d·2
    macs = L * (1500 * 12 * d * d + 2 * 1500 * 1500 * d)
    print(f"{name:5s} params {n/1e6:6.1f} M (encoder {enc/1e6:5.1f} M)  "
          f"int8 {n/2**20:6.1f} MiB  encoder 30 s ~{macs/1e9:5.1f} GMAC")
with torch.no_grad():
    x = torch.randn(1, 80, 3000)                      # 30 s log-mel: 80 ch × 3000 프레임 (10 ms hop)
    print("encoder output:", tuple(m.model.encoder(x).last_hidden_state.shape), "(small)")
```

```text
tiny  params   37.8 M (encoder   8.2 M)  int8   36.0 MiB  encoder 30 s ~ 17.5 GMAC
base  params   72.6 M (encoder  20.6 M)  int8   69.2 MiB  encoder 30 s ~ 42.1 GMAC
small params  241.7 M (encoder  88.2 M)  int8  230.5 MiB  encoder 30 s ~168.9 GMAC
encoder output: (1, 1500, 768) (small)
```

출력에서 볼 것:

- 세어 본 값(37.8M / 72.6M / 241.7M)이 발표 값(39M / 74M / 244M)과 1~2M 차이 나는 것은 세는 방식(위치 임베딩 포함 여부, 반올림 등)의 차이로 보인다. 자릿수는 확실히 맞다.
- tiny조차 **파라미터의 약 78%가 decoder** 쪽이다. vocab 51,865 × 384 embedding만 약 20M이다. 작은 모델일수록 vocab 크기가 비싸다(B8 tokenizer 비용과 같은 이야기).
- encoder 출력은 `[1, 1500, d]` — 30초 = 1500 토큰이다. 3초만 말해도 기본 사용법에서는 30초로 padding해서 encoder를 **통째로** 돈다.

**"Whisper가 MCU에서 못 도는 이유"** 를 숫자로 말할 수 있다.

```
메모리: tiny int8 가중치 ≈ 36 MiB   vs   MCU SRAM 수백 KB ~ 수 MB, flash 수 MB   → 10~100배 초과
연산:   tiny encoder 30 s ≈ 17.5 GMAC (+ decoder 토큰마다)
        MCU(수백 MHz, cycle당 MAC 1~몇 개) ≈ 초당 0.1~1 GMAC  → encoder 한 번에 수십~수백 초
구조:   30 s 고정 창 + autoregressive decoder → 첫 글자까지 지연이 크고 streaming이 아니다
        attention이 N² (1500² = 225만 쌍/층/head)
```

그래서 Whisper류는 기기라면 **SoC의 NPU/GPU**(가능은 하다 — tiny/base급, 양자화), 아니면 cloud에서 돈다. 웨어러블 ASR의 on-device 후보는 보통 streaming CTC/RNN-T 계열의 작은 모델이다.

### 4.6 Conformer — conv + attention

**Conformer(Gulati et al., 2020)** 는 ASR encoder 블록 설계다. Transformer 블록 안에 **convolution 모듈**을 넣었다.

```
x → ½·FFN → Multi-head self-attention → Conv module → ½·FFN → LayerNorm → y
                                           │
            pointwise conv + GLU → depthwise conv 1D → BatchNorm → Swish → pointwise conv
(각 모듈에 residual 연결, FFN 두 개가 반씩 기여하는 "macaron" 구조)
```

직관: **attention은 멀리 떨어진 frame 사이의 관계**(문장 전체 문맥)를 잘 잡고, **depthwise conv는 가까운 frame 사이의 국소 패턴**(음소의 모양, formant 전이)을 싸게 잡는다. 둘을 같이 쓰면 둘 중 하나만 쓸 때보다 같은 크기에서 정확도가 좋다고 보고되었다. Conformer는 CTC, RNN-T, AED 어디에나 encoder로 붙는다 — "Conformer-Transducer"처럼 조합해서 부른다.

streaming 관점: depthwise conv를 **causal**로 만들고(과거 frame만 보기, 9.3절), attention을 **chunk 단위**로 제한하면(현재 chunk + 과거 몇 chunk만 보기) streaming Conformer가 된다. 이때 conv의 과거 frame 버퍼와 attention의 과거 K/V가 **streaming state**가 된다 — LLM의 KV-cache(B4, D)와 같은 개념이다.

### 4.7 비교표 — on-device 관점

| 모델 계열 | streaming? | 지연 | 크기 (typical) | on-device 적합성 | 메모 |
|---|---|---|---|---|---|
| CTC (Conformer/CNN encoder) | 예 (causal/chunk encoder면) | 낮음 (chunk 크기 + look-ahead) | 수~수십 M 파라미터 | 좋음 — NPU 친화적, frame 병렬 | LM 없으면 철자 약함 |
| RNN-T / Transducer | 예 (설계 목적) | 낮음 | 수십~100M대 | 좋음 — 휴대폰 온디바이스 표준 | joiner/predictor가 순차 루프 |
| AED (Whisper) | 기본은 아니오 (30 s 창) | 높음 (창 + 디코딩) | 39M ~ 1.5B | SoC NPU/GPU에서 small 이하만 현실적 | 정확·다국어·잡음 강인 |
| Conformer | encoder 블록일 뿐 — 위와 조합 | 조합에 따라 | 조합에 따라 | depthwise conv + attention 둘 다 필요 | chunk attention으로 streaming |

"typical" 열은 흔히 보는 자릿수이며, 특정 제품의 값이 아니다.

---

## 5. 화자 인식 — speaker verification

### 5.1 직관과 정의

**화자 검증(speaker verification)** 은 "지금 말한 사람이 등록된 주인인가?"를 묻는 이진 판단이다. 웨어러블이라면 "착용자 본인의 명령만 받기", "옆 사람 TV 소리에 반응하지 않기"에 쓸 수 있다(가정).

구조는 거의 항상 **embedding + cosine similarity**다.

1. 모델(예: d-vector, x-vector, ECAPA-TDNN 계열)이 발화 하나를 **고정 길이 벡터**(수백 차원)로 요약한다. 같은 사람의 발화끼리 가깝게, 다른 사람끼리 멀게 학습된다.
2. **등록(enrollment)**: 주인의 발화 몇 개의 embedding 평균을 저장한다.
3. **검증**: 새 발화 embedding과 등록 벡터의 cosine similarity(A1)를 계산해 threshold와 비교한다.

```
cos(a, b) = a · b / (‖a‖ ‖b‖)
```

손계산: a = (1, 2, 2), b = (2, 1, 2) → a·b = 2 + 2 + 4 = 8, ‖a‖ = ‖b‖ = 3 → cos = 8/9 ≈ 0.89. c = (2, −1, −2)이면 a·c = 2 − 2 − 4 = −4 → cos = −4/9 ≈ −0.44. 말로 하면: 방향이 같을수록 1, 무관하면 0 근처, 반대면 −1이다. 크기(목소리 크기)는 무시한다. 벡터를 미리 L2 정규화해 두면 cosine = 내적 한 번 = `D`번의 MAC다.

### 5.2 코드로 확인 — threshold와 EER

가상의 화자 40명, 64차원 embedding으로 target(본인) / non-target(타인) 점수 분포를 만들고, threshold별 FRR·FAR와 **EER(equal error rate, FRR = FAR가 되는 점)** 을 구한다.

```python
import numpy as np
rng = np.random.default_rng(0)
D, S = 64, 40                                         # 임베딩 차원 64, 화자 40명
spk = rng.normal(0, 1, (S, D))                        # 화자별 "목소리 중심" (가상)
def utt(s, n):                                        # 발화 임베딩 = 중심 + 발화마다의 흔들림
    e = spk[s] + rng.normal(0, 1.2, (n, D))
    return e / np.linalg.norm(e, axis=1, keepdims=True)   # L2 정규화 → 내적 = cosine
enroll = np.stack([utt(s, 3).mean(0) for s in range(S)])  # 등록: 발화 3개 평균
enroll /= np.linalg.norm(enroll, axis=1, keepdims=True)
tgt, non = [], []
for s in range(S):
    test = utt(s, 20)                                  # 본인 테스트 발화 20개
    tgt += list(test @ enroll[s])
    others = rng.choice([o for o in range(S) if o != s], 20)
    non += [utt(o, 1)[0] @ enroll[s] for o in others]   # 다른 사람 발화 20개
tgt, non = np.array(tgt), np.array(non)
print(f"target cos mean {tgt.mean():.3f}   non-target cos mean {non.mean():.3f}")
print(" thr   FRR    FAR")
for thr in (0.2, 0.3, 0.4, 0.5):
    print(f"{thr:.1f}  {(tgt < thr).mean():.3f}  {(non >= thr).mean():.3f}")
ths = np.linspace(-1, 1, 2001)
frr = np.array([(tgt < t).mean() for t in ths]); far = np.array([(non >= t).mean() for t in ths])
i = np.argmin(np.abs(frr - far))
print(f"EER ~ {(frr[i] + far[i]) / 2:.3f} at thr {ths[i]:.3f}")
```

```text
target cos mean 0.525   non-target cos mean 0.000
 thr   FRR    FAR
0.2  0.001  0.060
0.3  0.014  0.009
0.4  0.072  0.000
0.5  0.367  0.000
EER ~ 0.011 at thr 0.284
```

출력에서 볼 것: 무관한 사람의 cosine은 평균 0(고차원에서 무작위 벡터는 거의 직교), 본인은 0.5 근처다. threshold를 올리면 본인을 거부(FRR↑), 내리면 타인을 수락(FAR↑) — wake word와 똑같은 trade-off다. 보안이 중요한 기능(결제 등)은 FAR 쪽을 훨씬 낮게 잡는다. 실제 시스템에서는 녹음 재생 공격(replay)·합성 음성 공격 대응(anti-spoofing)이 별도 과제다.

임베디드 연결: embedding 모델은 수 MB 규모가 흔해 SoC에서 돈다(typical). 하지만 **비교 자체는 64~256차원 내적 하나**라서 MCU에서도 된다. 등록 벡터는 개인정보이므로 secure storage에 둔다(L5).

---

## 6. 음성 향상, 잡음 제거, 에코 제거 — DSP냐 ML이냐

### 6.1 speech enhancement / noise suppression

목표: 잡음 섞인 음성에서 음성만 남기기. 두 세대가 있다.

- **고전 DSP**: 잡음 스펙트럼을 추정해(2절의 noise floor를 band별로) 빼거나(spectral subtraction) Wiener filter로 band gain을 곱한다. 가볍지만 비정상(non-stationary) 잡음(키보드, 사람 말소리)에 약하고 "musical noise" 잡음이 남는다.
- **ML**: 작은 신경망이 **band별(또는 bin별) gain mask**를 frame마다 예측한다. 신호 경로는 여전히 STFT → mask 곱 → inverse STFT(overlap-add)라서 DSP 골격 위에 ML이 "gain 계산기"로 들어간 형태다. 대표적인 예로 **RNNoise**(Valin, 2018)는 약 22개 band 에너지 등의 특징을 GRU에 넣어 band gain을 예측하는 매우 작은 하이브리드 모델이다. 더 큰 모델(DTLN, DeepFilterNet 등)은 품질이 좋고 연산이 더 든다.

설계 포인트: 향상된 음성이 **사람 귀에 좋은 것**과 **ASR/KWS 정확도에 좋은 것**은 다를 수 있다. 과한 잡음 제거는 ASR 정확도를 오히려 떨어뜨리기도 한다. 그래서 KWS/ASR 입력에는 가벼운 처리만 하거나, 모델을 잡음 섞인 데이터로 학습(augmentation, A4 5.4절)해서 강인하게 만드는 쪽을 택하기도 한다.

### 6.2 AEC — 에코 제거

기기가 TTS로 말하는 동안 사용자가 끼어들면(**barge-in**, L4), 마이크에는 사용자 목소리 + **기기 자신의 스피커 소리**가 섞인다. 이 스피커 소리를 지우는 것이 **AEC(acoustic echo cancellation)** 다.

- **고전**: 스피커로 보낸 신호(reference)를 알고 있으므로, 스피커 → 마이크 경로의 임펄스 응답을 **적응 FIR 필터**(NLMS 등)로 추정해서 예측 에코를 뺀다. Don이 아는 "알려진 송신 신호의 누설을 추정해서 빼는" 구조다(RF의 self-interference cancellation과 비슷한 발상).
- **ML**: 선형 AEC 뒤에 남은 비선형 잔여 에코(작은 스피커의 찌그러짐, 진동)를 신경망 mask로 누르는 **residual echo suppression**이 흔한 조합이다.
- **펌웨어 핵심**: reference 신호와 마이크 신호의 **시간 정렬**. 두 경로의 버퍼 지연이 흔들리면(클럭 drift, 버퍼 underrun) 적응 필터가 수렴하지 못한다. G7(시간 동기화)과 9절의 DMA 설계가 AEC 품질을 좌우한다.

| 처리 | 고전 DSP | ML | 웨어러블에서 흔한 선택 (typical) |
|---|---|---|---|
| noise suppression | spectral subtraction, Wiener | GRU/CNN이 band gain 예측 (RNNoise 등) | 저전력 DSP의 하이브리드 |
| AEC | NLMS 적응 필터 (reference 필요) | 잔여 에코 suppression | 선형 AEC + ML 후처리 |
| beamforming | delay-and-sum, MVDR (마이크 2개 이상) | neural beamforming | 마이크 2개 DSP beamforming (G4) |
| dereverberation | WPE 등 | mask 기반 | 잘 안 함 (근접 마이크) |

---

## 7. TTS와 neural audio codec — 그리고 speech-LLM으로

### 7.1 TTS의 3단 구조

```
텍스트 ──► 텍스트 정규화 + G2P ──► acoustic model ──► vocoder ──► 파형
          ("3시" → "세 시",         (음소 → mel           (mel → 16~24 kHz
           글자 → 음소)              spectrogram)           PCM)
```

- **acoustic model**: 음소열을 mel spectrogram으로 바꾼다. Tacotron 2(autoregressive, attention)가 대표적인 초기 모델이고, FastSpeech 2처럼 **각 음소의 길이(duration)를 예측해 한 번에 병렬 생성**하는 non-autoregressive 모델이 지연이 짧아 실시간에 유리하다.
- **vocoder**: mel spectrogram(1절에서 만든 것과 같은 종류!)을 파형으로 되돌린다. WaveNet(느리지만 고품질) → WaveRNN → HiFi-GAN(GAN 기반, 빠름) 순으로 가벼워졌다. 1절의 log-mel은 위상을 버렸기 때문에 "되돌리기"가 쉬운 문제가 아니고, vocoder가 그 빈 정보를 그럴듯하게 채운다.
- **end-to-end**: VITS처럼 두 단계를 하나로 합친 모델도 있다.

임베디드 관점: TTS에서 사용자가 느끼는 지연은 **첫 오디오 조각이 나오기까지**다. 문장 전체를 만들고 재생하지 않고, 앞부분 mel → vocoder → 스피커로 **streaming**한다. LLM이 토큰을 흘려 주면 TTS가 구(phrase) 단위로 받아 바로 합성하는 파이프라인이 L4의 주제다.

### 7.2 neural audio codec과 speech token

**neural audio codec**은 오디오를 encoder로 압축해 **이산 토큰**으로 만들고 decoder로 복원하는 모델이다. 대표 예: **SoundStream**(Google, Zeghidour et al., 2021), **EnCodec**(Meta, Défossez et al., 2022).

- 구조: conv encoder → **RVQ(residual vector quantization)** → conv decoder. RVQ는 codebook 하나로 벡터를 근사하고, 남은 오차를 두 번째 codebook으로 또 근사하고… 를 반복한다. codebook을 몇 단 쓰느냐로 bitrate를 조절한다.
- 손계산(EnCodec 24 kHz 모델 기준으로 기억하는 설정, 원 논문에서 확인할 것): 초당 75 frame, codebook 1024개 항목 = 10 bit. codebook 1단이면 75 × 10 = 750 bit/s, 2단이면 **1.5 kbps**, 8단이면 6 kbps. 16-bit 24 kHz PCM(384 kbps)보다 수백 배 작다.

왜 중요한가: 오디오가 **토큰 열**이 되면, 텍스트 토큰을 다루던 **LLM이 그대로 오디오를 읽고 쓸 수 있다.** AudioLM, VALL-E 같은 연구가 이 방식으로 음성을 "언어 모델링"했고, 최근의 **speech-LLM**(음성을 직접 입력받거나 음성으로 직접 답하는 모델)은 audio encoder(Whisper encoder 같은) 출력이나 codec 토큰을 LLM 입력에 붙이는 구조를 쓴다. 이렇게 되면 ASR → LLM → TTS의 cascade가 하나의 모델로 합쳐질 수 있다(지연·표현력 이점, 대신 모델이 커진다). 자세한 입력 구조는 B8, 파이프라인 설계는 L4에서 다룬다.

| 방식 | 구성 | 장점 | 단점 |
|---|---|---|---|
| cascade | ASR → 텍스트 LLM → TTS | 모듈별 교체·디버깅 쉬움, 텍스트 로그 | 단계별 지연 누적, 억양·감정 정보 손실 |
| speech-LLM | audio encoder 또는 codec 토큰 → LLM → (codec 토큰 → 음성) | 지연 짧을 수 있음, 비언어 정보 보존 | 모델 큼, 평가·디버깅 어려움 |

---

## 8. Cascade와 예산 — 숫자로 설계하기

### 8.1 false wake의 전력 비용 (A2 확장)

A2 9.2절에서 "창당 FPR 1%면 시간당 360회 오경보"를 계산했다. 이것을 **배터리**로 바꿔 보자. 숫자는 모두 설명용 가정(typical 자릿수)이다.

```
배터리        : 300 mAh × 3.7 V ≈ 1.11 Wh
always-on 체인 : mic + VAD + KWS ≈ 1 mW
                → 24 h × 1 mW = 24 mWh = 배터리의 약 2.2 %/일
false wake 1회 : SoC가 깨어나 2단 검증 + 복귀 ≈ 300 mW × 3 s = 0.9 J = 0.25 mWh
  하루 10회     → 2.5 mWh (0.2 %)                       ← 괜찮다
  시간당 360회  → 8,640회/일 × 0.25 mWh = 2,160 mWh     ← 배터리(1,110 mWh)의 2배. 반나절이면 방전
```

말로 하면: KWS의 오경보율은 UX 문제이기 전에 **전력 예산 항목**이다. 1단 KWS가 "시간당 몇 번까지 SoC를 깨워도 되는지"는 SoC wake 에너지로부터 역산된다. Don이 SSD에서 "spurious wake 한 번 = 얼마의 idle 전력 손실"을 계산하던 것과 같다.

### 8.2 duty cycling — VAD가 문지기일 때

VAD가 "말소리 없음"이면 KWS를 재운다. 하루 중 주변에 말소리가 있는 비율이 30%라고 가정하면:

```
KWS 전력 (항상 켜짐)   : 0.6 mW × 24 h            = 14.4 mWh
KWS 전력 (VAD gating)  : 0.6 mW × 24 h × 0.3      =  4.3 mWh
VAD 비용               : 0.05 mW × 24 h           =  1.2 mWh
절약                   : 14.4 − (4.3 + 1.2)       ≈  8.9 mWh/일
```

대가: VAD가 말소리 시작을 늦게 알면 호출어 앞부분이 잘린다. 그래서 **pre-roll 버퍼**(VAD가 켜기 전 수백 ms 오디오를 링버퍼에 항상 저장해 두고, KWS가 켜지면 그것부터 먹인다)를 둔다. 마찬가지로 KWS trigger 후 SoC가 깨어나는 동안의 오디오도 버퍼링해서 ASR에 넘겨야 "Hey Hark, 내일 날씨"의 "내일"이 잘리지 않는다.

### 8.3 왜 VAD/KWS는 "수십 KB"여야 하나

| 제약 | always-on 도메인 (typical) | 결과 |
|---|---|---|
| SRAM | 수십 KB ~ 수백 KB (전원 유지되는 영역은 더 작다) | 가중치 + activation + 오디오 버퍼가 전부 들어가야 함 |
| 연산 | 수십~수백 MHz 코어 또는 작은 DSP | 초당 수십 M MAC 정도 |
| 전력 | 수백 µW ~ 1 mW대 | 외부 DRAM 접근 금지 (DRAM refresh·접근 에너지가 예산을 넘음) |
| 판정 주기 | 수십~100 ms | 창당 연산 × 초당 판정 수 |

손계산: 3.2절 DS-CNN(2.66 M MAC)을 100 ms마다 1초 창 전체에 다시 돌리면 **26.6 M MAC/s**다. cycle당 1 MAC 정도의 int8 커널이라면 약 27 MHz 분량의 cycle이 필요하다 — Cortex-M급 always-on 코어가 감당할 만한 수준이다. 가중치 22 KB + activation 수 KB ~ 16 KB + 1초 오디오 링버퍼 32 KB(또는 feature 링버퍼 49 × 10 byte)도 SRAM에 들어간다. 반면 Whisper tiny는 가중치만 36 MiB, 30초 encoder에 17.5 GMAC — 자릿수가 3개 다르다.

더 줄이는 기술: 매 hop마다 1초 창 전체를 다시 계산하지 말고 **새 column만 계산하는 streaming conv**(9.3절)로 바꾸면 연산이 크게 준다. 양자화는 C1, 성능 모델은 D에서 다룬다.

---

## 9. 임베디드 관점에서 다시 보기

### 9.1 frame-synchronous 처리와 DMA double buffer

오디오 파이프라인은 **frame-synchronous**다: 10 ms마다 DMA 블록 하나가 차고, 다음 블록이 차기 전에(10 ms 안에) 그 블록의 처리가 끝나야 한다. 이 deadline을 넘으면 샘플을 잃거나 덮어쓴다. 표준 구조는 ping-pong(double) buffer다.

```svg
<svg viewBox="0 0 660 230" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="44" font-size="13">DMA가 채움</text>
<text x="10" y="94" font-size="13">CPU가 처리</text>
<rect x="110" y="26" width="130" height="28" fill="#4a7bd0" fill-opacity="0.3" stroke="currentColor"/>
<text x="175" y="45" font-size="12" text-anchor="middle">buf A (160)</text>
<rect x="240" y="26" width="130" height="28" fill="#e08a3c" fill-opacity="0.3" stroke="currentColor"/>
<text x="305" y="45" font-size="12" text-anchor="middle">buf B (160)</text>
<rect x="370" y="26" width="130" height="28" fill="#4a7bd0" fill-opacity="0.3" stroke="currentColor"/>
<text x="435" y="45" font-size="12" text-anchor="middle">buf A (160)</text>
<rect x="500" y="26" width="130" height="28" fill="#e08a3c" fill-opacity="0.3" stroke="currentColor"/>
<text x="565" y="45" font-size="12" text-anchor="middle">buf B (160)</text>
<rect x="240" y="76" width="80" height="28" fill="#4a7bd0" fill-opacity="0.3" stroke="currentColor"/>
<text x="280" y="95" font-size="12" text-anchor="middle">A 처리</text>
<rect x="370" y="76" width="80" height="28" fill="#e08a3c" fill-opacity="0.3" stroke="currentColor"/>
<text x="410" y="95" font-size="12" text-anchor="middle">B 처리</text>
<rect x="500" y="76" width="80" height="28" fill="#4a7bd0" fill-opacity="0.3" stroke="currentColor"/>
<text x="540" y="95" font-size="12" text-anchor="middle">A 처리</text>
<line x1="240" y1="18" x2="240" y2="120" stroke="#d0564a" stroke-dasharray="4 3"/>
<line x1="370" y1="18" x2="370" y2="120" stroke="#d0564a" stroke-dasharray="4 3"/>
<line x1="500" y1="18" x2="500" y2="120" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="240" y="134" font-size="12" text-anchor="middle">IRQ: A 참</text>
<text x="370" y="134" font-size="12" text-anchor="middle">IRQ: B 참</text>
<text x="500" y="134" font-size="12" text-anchor="middle">IRQ: A 참</text>
<line x1="322" y1="100" x2="368" y2="100" stroke="currentColor"/>
<text x="345" y="94" font-size="12" text-anchor="middle">여유</text>
<line x1="110" y1="160" x2="630" y2="160" stroke="currentColor"/>
<text x="110" y="176" font-size="12" text-anchor="middle">0</text>
<text x="240" y="176" font-size="12" text-anchor="middle">10 ms</text>
<text x="370" y="176" font-size="12" text-anchor="middle">20 ms</text>
<text x="500" y="176" font-size="12" text-anchor="middle">30 ms</text>
<text x="630" y="176" font-size="12" text-anchor="middle">40 ms</text>
<text x="10" y="206" font-size="12">처리 = 링버퍼 이동 (240 유지 + 새 160) → 창 400에 Hann 곱 → FFT → mel → log → (N frame마다) 모델</text>
<text x="10" y="224" font-size="12">규칙: CPU 처리 시간 &lt; 10 ms (DMA가 같은 buffer를 다시 채우기 전), 모델 추론이 길면 별도 task로 분리</text>
</svg>
```

그림 6 — ping-pong DMA의 시간표. DMA가 한 버퍼를 채우는 동안 CPU는 방금 찬 다른 버퍼를 처리한다. 빨간 점선은 half/full-transfer 인터럽트 시점이다.

C로 흉내 내 보자. "하드웨어"가 160 샘플 블록을 번갈아 채우고, ISR이 번호를 알리면, main loop가 400 샘플 이력 버퍼를 갱신하고 **Q15 Hann 창**을 곱해 frame 에너지를 고정소수점으로 계산한다. 같은 값을 double로도 계산해 오차를 잰다.

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define HOP 160                                 /* 10 ms @ 16 kHz = DMA 블록 하나 */
#define WIN 400                                 /* 25 ms 분석 창 */
static int16_t dma_buf[2][HOP];                 /* ping-pong: DMA가 한쪽을 채우는 동안 CPU는 다른 쪽 */
static volatile int ready = -1;                 /* ISR이 세팅: 방금 다 찬 버퍼 번호 */
static int16_t hist[WIN];                       /* 최근 400 샘플 (240 겹침 + 새 160) */
static int16_t hann_q15[WIN];

static void dma_isr(int which) { ready = which; }            /* half/full-transfer 인터럽트 흉내 */
static int32_t log2_q8(uint64_t x) {                         /* VAD 예제와 같은 함수 */
    if (x == 0) return 0;
    int ip = 63 - __builtin_clzll(x);
    uint32_t fr = ip >= 8 ? (uint32_t)(x >> (ip - 8)) & 0xFF : (uint32_t)(x << (8 - ip)) & 0xFF;
    return ip * 256 + (int32_t)fr;
}

int main(void) {
    for (int i = 0; i < WIN; i++) hann_q15[i] = (int16_t)lround(32767.0 * (0.5 - 0.5 * cos(2 * M_PI * i / WIN)));
    long n = 0; int frames = 0, filled = 0; double max_err = 0;
    for (int blk = 0; blk < 50; blk++) {                     /* 0.5 s 분량의 DMA 블록 50개 */
        int16_t *dst = dma_buf[blk & 1];                     /* "하드웨어"가 채운다: 1 kHz, 점점 커지는 tone */
        for (int i = 0; i < HOP; i++, n++) dst[i] = (int16_t)((200 + 60 * blk) * sin(2 * M_PI * 1000 * n / 16000.0));
        dma_isr(blk & 1);
        /* ---- main loop: 프레임 동기 처리 (다음 블록이 차기 전 10 ms 안에 끝나야 한다) ---- */
        int b = ready; ready = -1;
        memmove(hist, hist + HOP, (WIN - HOP) * sizeof hist[0]);   /* 오래된 160개 밀어내기 */
        memcpy(hist + WIN - HOP, dma_buf[b], HOP * sizeof hist[0]);
        filled += HOP;
        if (filled < WIN) continue;                          /* 처음 400개가 쌓일 때까지 대기 */
        uint64_t e = 0; double ef = 0;
        for (int i = 0; i < WIN; i++) {
            int32_t v = ((int32_t)hist[i] * hann_q15[i]) >> 15;   /* Q15 창 곱: int16 × Q15 → int16 범위 */
            e += (uint64_t)((int64_t)v * v);
            double vf = hist[i] * (0.5 - 0.5 * cos(2 * M_PI * i / WIN));
            ef += vf * vf;                                   /* float 기준값 */
        }
        double err = fabs(log2_q8(e) / 256.0 - log2(ef));
        if (err > max_err) max_err = err;
        if (frames % 16 == 0) printf("frame %2d (blk %2d): log2 E fixed %.3f  float %.3f\n", frames, blk, log2_q8(e) / 256.0, log2(ef));
        frames++;
    }
    printf("blocks 50 -> frames %d, max |log2 err| = %.4f (= %.2f dB)\n", frames, max_err, max_err * 10 * log10(2.0));
    printf("RAM: dma 2x%d + hist %d + hann %d int16 = %zu bytes\n", HOP, WIN, WIN, sizeof dma_buf + sizeof hist + sizeof hann_q15);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 pingpong.c -o pingpong -lm && ./pingpong
```

```text
frame  0 (blk  2): log2 E fixed 22.352  float 22.436
frame 16 (blk 18): log2 E fixed 26.699  float 26.767
frame 32 (blk 34): log2 E fixed 28.344  float 28.428
blocks 50 -> frames 48, max |log2 err| = 0.0889 (= 0.27 dB)
RAM: dma 2x160 + hist 400 + hann 400 int16 = 2240 bytes
```

출력에서 볼 것:

- 블록 50개(0.5 s)에서 frame은 48개다. 첫 frame은 블록 3개(480 샘플 ≥ 400)가 쌓인 뒤(blk 2)에 나온다 — 1.2절 공식 `1 + ⌊(8000 − 400)/160⌋ = 48`과 같다.
- 고정소수점과 float의 차이는 최대 0.089 (log2) = **0.27 dB**다. 대부분은 Q15 창이 아니라 `log2_q8`의 **선형 소수부 근사**에서 온다(`log2(1 + f)`를 `f`로 근사하면 최대 오차가 약 0.086). 더 정확해야 하면 소수부를 작은 lookup table로 보정한다. 모델이 학습 때 본 float log-mel과의 차이가 이 정도면 대부분 허용되지만, **반드시 모델 정확도로 재확인**한다(C 장 양자화 평가와 같은 절차).
- RAM은 2.2 KB. `memmove` 대신 400칸 링버퍼 + 인덱스로 복사를 없앨 수 있지만, 그러면 창 곱할 때 wrap-around 처리가 필요하다. 240개 × 2 byte 복사는 10 ms 예산에서 무시할 만하다 — 측정하고 결정한다.
- 음수의 `>> 15`는 C 표준상 implementation-defined(대부분 산술 시프트)다. 이식성이 중요하면 CMSIS-DSP의 `__SSAT`/Q15 곱 intrinsic을 쓰거나 명시적으로 처리한다.

### 9.2 고정소수점 feature 추출 체크리스트

| 단계 | float 기준 | 고정소수점 구현 (typical) | 주의 |
|---|---|---|---|
| 창 곱 | `x · w` | int16 × Q15 → >>15 | 반올림 bias, 음수 시프트 |
| FFT | complex float | Q15/Q31 FFT (CMSIS-DSP 등), stage마다 scaling | 스케일 누적으로 작은 소리 정밀도 손실 |
| power | `re² + im²` | int32/int64 누산 | overflow, block floating point |
| mel | `spec @ fb` | sparse 삼각형, Q15 가중치 | 필터 정의(HTK/Slaney) 일치 |
| log | `ln(x + ε)` | clz + LUT, Q 형식 | 밑(ln/log2/log10) 변환 상수, ε 동일 |
| 정규화 | `(x − μ)/σ` | 미리 계산한 Q 상수 곱 | 학습 통계 그대로 (A2 4.4) |
| 모델 입력 | float | int8 양자화 (scale, zero-point) | 양자화 파라미터 일치 (C1) |

### 9.3 streaming state — conv와 RNN을 frame 단위로 돌리기

1초 창을 100 ms마다 통째로 다시 계산하면 90%는 이미 한 계산을 반복하는 것이다. **streaming 추론**은 새 frame이 들어올 때 그 frame과 관련된 계산만 한다. 이때 각 층이 과거 정보를 **state**로 들고 있어야 한다.

- **causal conv 1D** (B2): kernel 길이 K면 과거 K−1 frame을 state로 보관 — HW의 shift register, FIR 필터의 delay line과 똑같다.
- **RNN/GRU** (B3): hidden state 벡터 하나를 frame마다 갱신.
- **chunk attention** (B4): 과거 chunk의 K/V를 cache로 보관.

아래 코드는 kernel 3짜리 depthwise causal conv를 "전체를 한 번에"와 "frame 하나씩 + state"로 계산해 같은지 확인하고, 흔한 실수(학습은 가운데 정렬 `same` padding, 기기는 causal)를 보여 준다.

```python
import numpy as np
rng = np.random.default_rng(0)
C, K, T = 4, 3, 12                                   # 채널 4, kernel 3 (시간축), 프레임 12개
w = rng.normal(0, 1, (C, K)); x = rng.normal(0, 1, (T, C))   # depthwise causal conv 1D
def offline(x):                                      # 전체 시퀀스를 한 번에 (학습 때): 왼쪽 K-1개 0 padding
    xp = np.vstack([np.zeros((K - 1, C)), x])
    return np.stack([(xp[t:t + K] * w.T).sum(0) for t in range(T)])
class Streaming:                                     # 기기에서: 프레임 1개씩, 과거 K-1 프레임을 state로 보관
    def __init__(self): self.state = np.zeros((K - 1, C))
    def step(self, f):
        buf = np.vstack([self.state, f[None]])       # [K, C] = 과거 2 + 현재 1
        self.state = buf[1:]                         # 가장 오래된 프레임을 버린다 (shift register)
        return (buf * w.T).sum(0)
s = Streaming()
y_stream = np.stack([s.step(x[t]) for t in range(T)])
print("state size per layer:", s.state.shape, "=", s.state.size, "values")
print("max |offline - streaming| =", np.abs(offline(x) - y_stream).max())
# 흔한 실수: 학습은 'same'(가운데 정렬, 미래 1프레임 사용)으로 했는데 기기에서 causal로 돌림
xp = np.vstack([np.zeros((1, C)), x, np.zeros((1, C))])
y_same = np.stack([(xp[t:t + K] * w.T).sum(0) for t in range(T)])
print("max |same-padding - streaming| =", round(float(np.abs(y_same - y_stream).max()), 3))
print("same-padding[t] == streaming[t+1]?", np.allclose(y_same[:-1], y_stream[1:]))
```

```text
state size per layer: (2, 4) = 8 values
max |offline - streaming| = 0.0
max |same-padding - streaming| = 3.067
same-padding[t] == streaming[t+1]? True
```

출력에서 볼 것: causal conv는 state 8개(과거 2 frame × 4 채널)만 들고 있으면 offline과 **정확히 같다**(오차 0). 반면 학습을 `same` padding으로 했다면 출력이 **1 frame 어긋난다**(`same[t] == stream[t+1]`). 즉 `same` 모델은 미래 frame 1개를 보고 있었고, streaming으로 돌리려면 그만큼 **look-ahead 지연**을 받아들이거나(출력을 1 frame 늦게 내기) 모델을 causal로 다시 학습해야 한다. 층이 쌓이면 look-ahead도 층마다 더해진다. 모델팀과 "이 모델의 총 look-ahead는 몇 ms인가?"를 반드시 확인하는 이유다.

state 메모리 계산 습관: 층마다 `(K − 1) × 채널`을 더한다. 예를 들어 채널 64, kernel 3짜리 causal conv 10층이면 `2 × 64 × 10 = 1,280`개 값(int8이면 1.25 KB)이다. 이 state는 **기기 전원 상태 전환(sleep/wake) 때 보존하거나 리셋 정책을 정해야** 한다 — 리셋하면 처음 몇 frame은 "과거 = 0"으로 계산되어 출력이 불안정하다(warm-up).

### 9.4 단계별 메모리·연산 표 (typical 자릿수)

| 단계 | 가중치 | activation / state | 오디오 버퍼 | 연산 | 실행 위치 (typical) |
|---|---|---|---|---|---|
| PDM→PCM + feature | 창·필터 LUT 수 KB | 수 KB | DMA 2 × 320 B + 이력 800 B | 수 M ops/s | MCU / 저전력 DSP |
| VAD (energy) | 없음 | 수십 B | frame 1개 | 무시 가능 | MCU / 마이크 칩 |
| VAD (small NN) | 수십 KB ~ 수 MB | 수 KB (GRU state) | 수십 ms | 수 M MAC/s | 저전력 DSP / MCU |
| KWS 1단 | 20 ~ 200 KB (int8) | 수 KB ~ 수십 KB | pre-roll 0.5 ~ 2 s (16 ~ 64 KB) | 수십 M MAC/s | 저전력 DSP / MCU |
| KWS 2단 | 수백 KB ~ 수 MB | 수십 KB | 2 ~ 3 s | 짧게 수백 M MAC | SoC DSP/NPU |
| streaming ASR | 수십 ~ 수백 MB | 수 MB (state, cache) | chunk 단위 | 수 GMAC/s 대 | SoC NPU/CPU |
| Whisper급 AED | 39M ~ 1.5B 파라미터 | 수십 MB 이상 | 30 s 창 | 창당 수십~수천 GMAC | SoC NPU/GPU 또는 cloud |
| TTS | 수십 ~ 수백 MB | 수 MB | 출력 streaming | 수 GMAC/s 대 | SoC 또는 cloud |

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 기기 feature와 학습 feature 불일치 | 오프라인 정확도 95%, 기기에서 80% | n_fft/창 위치/center/log 밑/ε/mel 공식 차이 | 같은 wav로 Python vs C frame별 diff (golden vector) |
| `[T, n_mels]` vs `[n_mels, T]` 혼동 | 에러 없이 정확도 급락 | torchaudio는 채널 먼저 | shape assert, 입력 텐서 덤프 |
| 샘플레이트 불일치 (48 kHz를 16 kHz 모델에) | 모든 소리가 저음으로 이동한 것처럼 동작 | resampling 누락 | 파이프라인 입구에서 rate 검사, 제대로 된 anti-alias resampler |
| 창 단위로 FA 셈 | FA/hour가 수 배 부풀려짐 | 한 사건이 여러 창에 걸림 | refractory 적용 후 이벤트 단위로 센다 |
| 짧은 배경 오디오로 FA 주장 | 출시 후 "혼자 켜진다" 민원 | 드문 사건의 통계 부족 | rule of three: 0회 관찰이면 T ≥ 3/목표율 시간 (A4 9.2) |
| energy VAD 고정 임계 | 시끄러운 곳에서 항상 on, 전력 폭증 | 잡음 수준 변화 | 적응형 floor + ML VAD, 필드 로그로 튜닝 |
| pre-roll 없음 | 호출어·명령 첫 음절 잘림, FRR 상승 | VAD/KWS/SoC wake 지연 | 링버퍼로 수백 ms ~ 수 초 과거 오디오 보관 |
| `same` padding 모델을 streaming으로 | 출력 1 frame씩 어긋남, 정확도 저하 | 학습 모델이 미래를 봄 | causal로 재학습 또는 look-ahead 지연 수용 |
| streaming state 리셋 정책 없음 | wake 직후 몇 frame 오판 | state가 0에서 시작 (warm-up) | 리셋 후 N frame 무시, 또는 state 보존 |
| AEC reference 정렬 흔들림 | barge-in 시 자기 TTS에 반응 | 버퍼 지연·클럭 drift | reference/마이크 timestamp 정렬, 지연 측정 (G7) |

---

## 11. 면접에서 이렇게 말한다

**Q.** "Design an always-on wake word pipeline for a wearable."

**A.** 전력 등급별 cascade로 설계한다. 마이크 PDM을 저전력 도메인에서 16 kHz PCM으로 바꾸고, 10 ms frame마다 VAD가 문지기를 한다. 말소리가 있으면 저전력 DSP/MCU의 수십 KB int8 DS-CNN KWS가 100 ms마다 1초 창을 판정하고, posterior smoothing + threshold + refractory로 이벤트를 만든다. trigger되면 SoC를 깨워 더 큰 2단 모델로 재검증하고, pre-roll 버퍼에 있던 오디오부터 streaming ASR에 넘긴다. 목표 스펙은 "FA ≤ 하루 x회에서 FRR ≤ y%"와 always-on 전력 mW 예산이고, FA 목표는 SoC wake 에너지로부터 역산한다.

> I'd build it as a power-tiered cascade. The mic and a cheap VAD run in the always-on domain on 10 ms frames; when there's speech, a tens-of-kilobytes int8 DS-CNN on the low-power DSP or MCU scores a one-second log-mel window every 100 ms, with posterior smoothing, a threshold, and a refractory period. A trigger wakes the SoC, a larger second-stage model re-verifies, and the pre-roll buffer is streamed into ASR so the first words aren't clipped. I'd spec it as false accepts per 24 hours at a target false reject rate, plus a milliwatt budget, and derive the allowed wake rate from the SoC's energy per wake.

**Q.** "What is a log-mel spectrogram?"

**A.** 16 kHz 오디오를 25 ms 창, 10 ms hop으로 자르고, 창마다 FFT해서 power spectrum을 구한 뒤, 사람 귀처럼 저주파는 촘촘하고 고주파는 넓은 삼각형 mel 필터 40~80개로 묶고, log로 동적 범위를 압축한 `[frame, mel]` 표다. 1초면 약 100 × 40이다. 거의 모든 음성 모델의 입력이고, MFCC는 여기에 DCT를 한 번 더 한 것이다. 기기에서는 이 전처리를 모델팀 Python과 frame 단위로 일치시키는 것이 핵심이다.

> It's the short-time power spectrum warped onto a perceptual frequency axis and log-compressed. You frame 16 kHz audio into 25 ms windows every 10 ms, FFT each frame, pool the power into 40 to 80 triangular mel filters that are narrow at low frequencies and wide at high ones, and take the log, so one second becomes roughly a 100 by 40 image. On device, the critical part is matching the training front-end exactly — window placement, FFT size, mel formula, log base, epsilon.

**Q.** "CTC vs RNN-T vs Whisper for on-device?"

**A.** CTC는 encoder + 선형층이라 가장 단순하고 frame별 출력이 독립이라 병렬·streaming에 좋지만, 출력 간 의존성이 없어 외부 LM이 있으면 좋다. RNN-T는 이전 토큰을 보는 predictor와 joiner를 더해 정확도를 올리면서도 태생적으로 streaming이라 휴대폰 on-device ASR의 표준이다. Whisper는 30초 창의 encoder-decoder라 정확·강인하지만 streaming이 아니고 decoder가 autoregressive라 지연과 메모리가 크다. 웨어러블이면 on-device는 streaming CTC/RNN-T, 긴 받아쓰기나 고품질은 SoC NPU의 작은 Whisper나 cloud로 나눈다.

> CTC is the simplest — an encoder plus a linear layer with frame-independent outputs, so it's parallel and easy to stream, though it benefits from an external language model. RNN-T adds a prediction network and a joiner, so it conditions on previous tokens while staying naturally streaming; that's why it became the standard for on-device phone ASR. Whisper is an attention encoder-decoder over 30-second windows: very robust, but not streaming by default and autoregressive, so latency and memory are high. On a wearable I'd run a streaming CTC or transducer locally and send long-form or high-accuracy transcription to a small Whisper on the SoC NPU or to the cloud.

**Q.** "How do you measure a wake word?"

**A.** 두 축이다. FRR은 호출어 발화 중 놓친 비율로, 조용함·잡음·거리·화자 그룹별로 잰다. FA는 호출어가 없는 긴 배경 오디오에서 refractory를 적용한 이벤트 수를 시간으로 나눈 FA/hour다. threshold를 쓸면서 DET 곡선을 그리고 "하루 1회 FA에서 FRR"로 동작점을 고른다. FA는 드문 사건이라 0회 관찰로 하루 1회를 주장하려면 최소 72시간이 필요하다(rule of three). 그리고 양자화·C 포팅 후에 같은 셋으로 다시 잰다.

> Two axes: false reject rate on keyword utterances, broken down by noise, distance and speaker group, and false accepts per hour on long keyword-free background audio, counted as events after the refractory period. I sweep the threshold to get a DET curve and pick the operating point, like FRR at one false accept per 24 hours. Because false accepts are rare, claiming one per day from zero observed events needs at least about 72 hours of audio — the rule of three. And I re-measure after quantization and porting, on the same sets.

**Q.** "Why can't Whisper run on an MCU?"

**A.** 자릿수가 안 맞는다. 가장 작은 tiny도 약 39M 파라미터라 int8로 36 MiB인데 MCU SRAM은 수백 KB ~ 수 MB다. 30초 encoder 한 번이 약 17 GMAC라 수백 MHz MCU로는 수십 초 이상 걸린다. 게다가 30초 고정 창과 autoregressive decoder, 1500 토큰 attention 구조라 streaming 저지연과 맞지 않는다. MCU에는 수십 KB KWS/VAD를 두고, ASR은 SoC NPU나 cloud에서 돈다.

> It's off by orders of magnitude. Even Whisper tiny is about 39 million parameters — roughly 36 MiB in int8 — while an MCU has hundreds of kilobytes to a few megabytes of SRAM. One 30-second encoder pass is on the order of 17 GMACs, which is tens of seconds or more on a few-hundred-MHz core. Architecturally it's a fixed 30-second window with an autoregressive decoder and attention over 1,500 tokens, which fights low-latency streaming. The MCU runs the tens-of-kilobytes VAD and wake word; ASR belongs on the SoC NPU or in the cloud.

**Q.** "Why does CTC need a blank token?"

**A.** frame 수가 글자 수보다 훨씬 많아서 frame마다 뭔가를 내야 하는데, 반복을 합치는 규칙 때문에 "ll" 같은 연속 글자를 표현할 방법이 필요하다. blank가 사이에 끼면 두 l이 따로 살아남는다. 또 blank는 묵음과 글자 사이 전이 구간을 "새 글자 없음"으로 표현하는 자리다. greedy 디코딩은 frame별 argmax → 반복 합치기 → blank 제거다.

> Because there are far more frames than characters and the decoding rule merges repeats, you need a separator to express doubled letters — "l blank l" survives as two l's, while "l l" collapses to one. Blank also absorbs silence and transitions, meaning "no new symbol here." Greedy decoding is argmax per frame, merge repeats, then drop blanks.

---

## 12. 직접 해보기

1. 손계산: 8 kHz 샘플링, 32 ms 창(256 샘플), 16 ms hop(128 샘플)일 때 2초 오디오의 frame 수와 `n_fft = 256`의 bin 개수·bin 간격은? 정답: frame = 1 + ⌊(16000 − 256)/128⌋ = 124, bin = 129개, 간격 31.25 Hz.
2. 손계산: HTK 공식으로 mel(700 Hz)와 mel(4000 Hz)를 구하라. 정답: 2595·log10(2) ≈ 781.2, 2595·log10(6.714) ≈ 2146.1.
3. CTC collapse: path `aa-ab-bb--c`와 `a-a-bbc`를 collapse하라. 그리고 "aab"를 만드는 가장 짧은 path 길이는? 정답: "aabbc"(`a a - a b - b b - - c` → 반복 합치기 `a - a b - b - c` → blank 제거), "aabc" / 최소 4 frame (`a-ab`, 두 a 사이에 blank 1개 필요).
4. 코드 과제: 1.7절 코드에 `torchaudio.transforms.MFCC`를 적용해 shape을 확인하고, 1.8절처럼 frame 위치를 맞춰 numpy DCT 결과와 비교하라. 힌트: torchaudio MFCC는 기본적으로 `norm="ortho"` DCT를 쓰고 log 대신 dB(`AmplitudeToDB`)를 쓰므로, 상수배·오프셋 차이가 난다 — 무엇이 다른지 찾아내는 것이 과제다.
5. 코드 과제: 3.4절 `detect()`에서 smoothing 창을 1, 3, 5로, refractory를 0.5 / 1 / 2 s로 바꿔 보고 "검출 지연"(호출어 시작 대비)과 오경보 수를 표로 만들어라. 힌트: 지연은 smoothing 창이 클수록 늘고, refractory가 너무 길면 연속 명령("Hey Hark… Hey Hark")을 놓친다.
6. 예산 문제: always-on 체인이 1.5 mW이고, SoC wake 1회가 400 mW × 2 s라면, 하루 전력 중 false wake 비중이 always-on과 같아지는 FA/day는? 정답: 1.5 mW × 24 h = 36 mWh, wake 1회 = 0.8 J ≈ 0.222 mWh → 약 162회/일(시간당 약 6.75회).

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| sample rate | 샘플링 레이트 | 초당 샘플 수. 음성은 16 kHz가 표준 |
| frame / window / hop | 프레임 / 창 / 이동 간격 | 25 ms 조각을 10 ms씩 밀며 자른다 |
| STFT | 단시간 푸리에 변환 | frame마다 FFT → 시간 × 주파수 표 |
| mel scale | 멜 척도 | 사람 귀를 흉내 낸 주파수 축 (저주파 촘촘) |
| log-mel spectrogram | 로그 멜 스펙트로그램 | mel 필터 에너지의 log, 음성 모델 표준 입력 |
| MFCC | 멜 켑스트럼 계수 | log-mel에 DCT, 앞 10~13개 |
| VAD | 음성 구간 검출 | frame마다 말소리 유무 판정 |
| hangover | 행오버 | 말 끝난 뒤 잠시 "있음"을 유지하는 시간 |
| KWS / wake word | 키워드 검출 / 호출어 | 정해진 단어를 찾는 분류기 |
| DS-CNN | depthwise separable CNN | Hello Edge의 MCU용 KWS 구조 |
| posterior smoothing | 사후확률 평활 | 최근 창들의 확률 평균 |
| refractory period | 불응기 | trigger 후 재trigger 금지 시간 |
| FRR / FA per hour | 미검출률 / 시간당 오경보 | wake word의 두 핵심 지표 (A4) |
| pre-roll | 프리롤 버퍼 | trigger 이전 오디오를 보관하는 링버퍼 |
| ASR | 음성 인식 | 음성 → 텍스트 |
| CTC | connectionist temporal classification | blank + collapse, 모든 정렬의 합으로 학습 |
| blank | 공백 기호 | "새 글자 없음", 반복 글자 구분자 |
| RNN-T | recurrent neural network transducer | encoder + predictor + joiner, streaming 표준 |
| AED | attention encoder-decoder | decoder가 cross-attention으로 encoder를 봄 (Whisper) |
| Conformer | convolution-augmented Transformer | attention + depthwise conv 블록 |
| look-ahead | 미래 참조 | 출력에 필요한 미래 frame 수 = 구조적 지연 |
| speaker verification | 화자 검증 | embedding cosine으로 본인 여부 판단 |
| EER | 동일 오류율 | FRR = FAR가 되는 점의 오류율 |
| AEC | 음향 에코 제거 | 기기 스피커 소리를 마이크 신호에서 제거 |
| barge-in | 끼어들기 | TTS 재생 중 사용자가 말하는 것 |
| vocoder | 보코더 | mel spectrogram → 파형 |
| neural audio codec | 신경망 오디오 코덱 | 오디오 ↔ 이산 토큰 (SoundStream, EnCodec) |
| RVQ | residual vector quantization | 잔차를 여러 codebook으로 차례로 양자화 |
| ping-pong buffer | 이중 버퍼 | DMA가 한쪽을 채우는 동안 CPU가 다른 쪽 처리 |

---

## 14. 요약 & 체크리스트

음성 기기는 전력 등급이 다른 단계의 cascade다: 항상 켜진 mic·VAD·KWS는 수십 KB·mW 이하로 작아야 하고, 호출된 뒤의 ASR·LLM·TTS는 SoC나 cloud에서 크게 돈다. 모든 음성 모델의 입력은 16 kHz 오디오를 25 ms/10 ms로 자른 log-mel spectrogram이며, 기기에서는 이 전처리를 학습 코드와 frame 단위로 일치시키는 것이 첫 번째 일이다. wake word는 DS-CNN 같은 작은 CNN을 sliding window로 돌리고 smoothing·threshold·refractory로 이벤트를 만들며, FRR과 FA/hour로 평가한다. ASR은 CTC(단순, streaming), RNN-T(on-device streaming 표준), Whisper(정확하지만 30 s 창·대형)로 나뉘고 Conformer는 그 encoder 블록이다. 펌웨어 관점의 핵심은 DMA ping-pong으로 10 ms deadline을 지키는 frame-synchronous 처리, 고정소수점 feature, 그리고 causal conv/RNN의 streaming state다.

- [ ] 1초 16 kHz 오디오의 frame 수(25 ms/10 ms)와 FFT bin 수·간격을 손으로 계산할 수 있다
- [ ] mel(1000 Hz) ≈ 1000을 HTK 공식으로 확인하고 mel 필터가 왜 삼각형·비균등인지 설명할 수 있다
- [ ] numpy log-mel과 torchaudio 결과가 다를 때 shape 순서·center·창 위치를 먼저 의심할 수 있다
- [ ] energy VAD의 noise floor 적응 속도 trade-off를 설명할 수 있다
- [ ] DS-CNN의 파라미터와 MAC(stem, depthwise, pointwise)를 손으로 셀 수 있다
- [ ] streaming wake word 검출에서 smoothing·threshold·refractory의 역할과 지연 비용을 말할 수 있다
- [ ] CTC path를 collapse할 수 있고 blank가 필요한 이유를 말할 수 있다
- [ ] CTC / RNN-T / Whisper / Conformer의 streaming 여부와 on-device 적합성을 비교할 수 있다
- [ ] false wake 횟수를 배터리 mWh로 환산할 수 있다
- [ ] causal conv의 streaming state 크기를 계산하고 `same` padding 모델의 look-ahead 문제를 설명할 수 있다

## 참고 자료

- Y. Zhang, N. Suda, L. Lai, V. Chandra, "Hello Edge: Keyword Spotting on Microcontrollers", 2017 — [arXiv:1711.07128](https://arxiv.org/abs/1711.07128)
- P. Warden, "Speech Commands: A Dataset for Limited-Vocabulary Speech Recognition", 2018 — [arXiv:1804.03209](https://arxiv.org/abs/1804.03209)
- A. Graves et al., "Connectionist Temporal Classification", ICML 2006
- A. Graves, "Sequence Transduction with Recurrent Neural Networks", 2012 — [arXiv:1211.3711](https://arxiv.org/abs/1211.3711)
- Y. He et al., "Streaming End-to-end Speech Recognition for Mobile Devices", ICASSP 2019 — [arXiv:1811.06621](https://arxiv.org/abs/1811.06621)
- A. Gulati et al., "Conformer: Convolution-augmented Transformer for Speech Recognition", 2020 — [arXiv:2005.08100](https://arxiv.org/abs/2005.08100)
- A. Radford et al., "Robust Speech Recognition via Large-Scale Weak Supervision" (Whisper), 2022 — [arXiv:2212.04356](https://arxiv.org/abs/2212.04356), [github.com/openai/whisper](https://github.com/openai/whisper)
- J.-M. Valin, "A Hybrid DSP/Deep Learning Approach to Real-Time Full-Band Speech Enhancement" (RNNoise), 2018 — [arXiv:1709.08243](https://arxiv.org/abs/1709.08243)
- N. Zeghidour et al., "SoundStream: An End-to-End Neural Audio Codec", 2021 — [arXiv:2107.03312](https://arxiv.org/abs/2107.03312)
- A. Défossez et al., "High Fidelity Neural Audio Compression" (EnCodec), 2022 — [arXiv:2210.13438](https://arxiv.org/abs/2210.13438)
- torchaudio 문서 — [MelSpectrogram](https://pytorch.org/audio/stable/generated/torchaudio.transforms.MelSpectrogram.html), [ctc_loss](https://pytorch.org/docs/stable/generated/torch.nn.functional.ctc_loss.html)
- 이 노트 세트: A2 9절(Bayes 오경보), A4 9절(FAR/FRR/DET), B2(depthwise separable), B3(streaming RNN), B4(Transformer), 다음 단계 G4(마이크 HW), G5(DSP), L4(음성 파이프라인 end-to-end)
