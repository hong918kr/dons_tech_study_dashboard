# G4. 마이크와 오디오 하드웨어 — MEMS 마이크, PDM vs I2S/TDM, CIC decimation, 마이크 어레이·beamforming·AEC

> **이 노트를 다 읽으면**: MEMS 마이크 데이터시트(sensitivity, SNR, AOP, PSRR, 전류)를 읽고 dBFS ↔ dB SPL 변환과 noise floor·dynamic range를 손으로 계산할 수 있다 · PDM 1비트 비트열이 ΣΔ 변조로 어떻게 만들어지는지, 그리고 CIC + 보상 FIR로 16 kHz PCM을 되찾는 decimation 체인을 bit growth까지 포함해 C로 짤 수 있다 · 마이크 2개의 TDOA를 GCC-PHAT로 추정하고, 1~2 cm 간격 어레이의 delay-and-sum·차동(differential)·MVDR beamforming이 저주파에서 왜 약한지 숫자로 설명할 수 있다 · NLMS AEC를 직접 돌려 ERLE와 double-talk 문제를 보이고, 마이크 → DMA → decimation → 링버퍼 → VAD/KWS 펌웨어 경로의 연산량·메모리·전력을 견적낼 수 있다
> **JD 연결**: "Hands-on with IMUs, accelerometers, gyroscopes, **microphones**", "Build data collection and ingestion pipelines … various sensors", (우대) "Audio/Voice … models" — study_prep_list **G4**: MEMS 마이크, **PDM vs I2S/TDM**, SNR, AOP, sensitivity / PDM → PCM decimation (CIC 필터) / 마이크 어레이, beamforming, AEC, noise suppression. Hark는 음성 중심 기기로 추정되므로 P0. 프로젝트 **PJ2**(마이크 → MFCC → DS-CNN)의 맨 앞단.
> **Don 기준 난이도**: I2S/SPI 같은 직렬 버스 bring-up, DMA ping-pong, 고정소수점, 오버플로·wrap 디버깅, 측정 기반 사고(RF의 dB 감각, self-interference cancellation)는 이미 강하다 / 음향 단위(dB SPL, dBFS, dB(A)), ΣΔ 변조와 noise shaping, CIC의 bit growth와 droop, 마이크 어레이의 공간 필터링, 적응 필터(NLMS)와 ERLE는 새로 배운다
> **선행 노트**: B5 (오디오 특징·VAD·KWS, DMA ping-pong, 6.2절 AEC 개요), E4 (Q15·FIR·누산기 guard bit·원형 버퍼), E8 (1.3절 always-on 오디오 경로, SoundWire). 같이 보는 노트: **G5** (샘플링·aliasing·FFT·mel — 이 노트는 "마이크에서 PCM까지"와 "마이크 여러 개"에 집중하고, PCM 이후의 스펙트럼 처리는 G5가 맡는다), G1 (가속도계 — 골전도 음성 검출과 연결), G7 (시간 동기화 — AEC reference 정렬)

---

## 0. 큰 그림 — 이게 왜 필요한가

B5에서 wake word 모델은 "16 kHz, int16 PCM을 25 ms/10 ms로 자른 log-mel"을 입력으로 받는다고 했다. 그런데 그 PCM은 하늘에서 떨어지지 않는다. 공기의 압력 변화가 실리콘 진동판을 흔들고, 그 움직임이 1비트짜리 1 MHz 비트열이 되고, 그 비트열을 필터로 걸러 16 kHz 숫자열로 만드는 **하드웨어 + 펌웨어 체인**이 있다. 이 체인이 틀리면 — 이득이 6 dB 틀어지거나, DC가 남거나, 4 kHz 위가 3 dB 깎이거나, 두 마이크가 한 샘플 어긋나면 — 모델은 학습 때와 다른 세상을 보게 된다. "Python에선 되는데 기기에선 안 된다"의 단골 원인이 여기 있다.

웨어러블(예를 들어 Hark 같은 귀/몸에 거는 음성 AI 기기라면 — 실제 구성은 모르고 추정이다)에서는 이 체인이 하루 종일 켜져 있어야 하므로 전력도 이 체인이 결정한다. 그리고 기기가 말을 하는 동안(TTS) 사용자가 끼어들려면 자기 스피커 소리를 지워야 하고(AEC), 바람·옷 스침·주변 사람 말소리를 줄이려면 마이크 여러 개를 엮어야 한다(beamforming).

```svg
<svg viewBox="0 0 680 210" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="g4a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="60" y="24" font-size="12" text-anchor="middle">§1–2</text><text x="172" y="24" font-size="12" text-anchor="middle">§1–2</text><text x="284" y="24" font-size="12" text-anchor="middle">§3–4</text><text x="396" y="24" font-size="12" text-anchor="middle">§5</text><text x="508" y="24" font-size="12" text-anchor="middle">§5</text><text x="620" y="24" font-size="12" text-anchor="middle">§6</text><rect x="10" y="32" width="100" height="52" rx="6" fill="#888" fill-opacity="0.15" stroke="currentColor"/><text x="60" y="54" font-size="12" text-anchor="middle">소리</text><text x="60" y="72" font-size="12" text-anchor="middle">음압 (Pa)</text><rect x="122" y="32" width="100" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.15" stroke="currentColor"/><text x="172" y="54" font-size="12" text-anchor="middle">MEMS 마이크</text><text x="172" y="72" font-size="12" text-anchor="middle">진동판 · ASIC</text>
<rect x="234" y="32" width="100" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.15" stroke="currentColor"/><text x="284" y="54" font-size="12" text-anchor="middle">PDM 비트열</text><text x="284" y="72" font-size="12" text-anchor="middle">1비트 · 1.024 MHz</text><rect x="346" y="32" width="100" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="396" y="54" font-size="12" text-anchor="middle">decimation</text><text x="396" y="72" font-size="12" text-anchor="middle">CIC + FIR</text><rect x="458" y="32" width="100" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="508" y="54" font-size="12" text-anchor="middle">PCM</text><text x="508" y="72" font-size="12" text-anchor="middle">16 kHz · int16</text><rect x="570" y="32" width="100" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="620" y="54" font-size="12" text-anchor="middle">전처리</text><text x="620" y="72" font-size="12" text-anchor="middle">DC 제거 · gain</text>
<line x1="110" y1="58" x2="120" y2="58" stroke="currentColor" marker-end="url(#g4a)"/><line x1="222" y1="58" x2="232" y2="58" stroke="currentColor" marker-end="url(#g4a)"/><line x1="334" y1="58" x2="344" y2="58" stroke="currentColor" marker-end="url(#g4a)"/><line x1="446" y1="58" x2="456" y2="58" stroke="currentColor" marker-end="url(#g4a)"/><line x1="558" y1="58" x2="568" y2="58" stroke="currentColor" marker-end="url(#g4a)"/><path d="M620,84 L620,98 L60,98 L60,114" fill="none" stroke="currentColor" marker-end="url(#g4a)"/><rect x="10" y="116" width="100" height="52" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/><text x="60" y="138" font-size="12" text-anchor="middle">마이크 여러 개</text><text x="60" y="156" font-size="12" text-anchor="middle">beamforming</text><rect x="122" y="116" width="100" height="52" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/><text x="172" y="138" font-size="12" text-anchor="middle">AEC</text><text x="172" y="156" font-size="12" text-anchor="middle">에코 제거</text>
<rect x="234" y="116" width="100" height="52" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/><text x="284" y="138" font-size="12" text-anchor="middle">NS · AGC</text><text x="284" y="156" font-size="12" text-anchor="middle">잡음 · 레벨</text><rect x="346" y="116" width="100" height="52" rx="6" fill="#888" fill-opacity="0.15" stroke="currentColor"/><text x="396" y="138" font-size="12" text-anchor="middle">링버퍼</text><text x="396" y="156" font-size="12" text-anchor="middle">pre-roll</text><rect x="458" y="116" width="100" height="52" rx="6" fill="#d0564a" fill-opacity="0.2" stroke="currentColor"/><text x="508" y="138" font-size="12" text-anchor="middle">VAD · KWS</text><text x="508" y="156" font-size="12" text-anchor="middle">(B5)</text><rect x="570" y="116" width="100" height="52" rx="6" fill="#d0564a" fill-opacity="0.2" stroke="currentColor"/><text x="620" y="138" font-size="12" text-anchor="middle">ASR · LLM</text><text x="620" y="156" font-size="12" text-anchor="middle">SoC · NPU</text>
<line x1="110" y1="142" x2="120" y2="142" stroke="currentColor" marker-end="url(#g4a)"/><line x1="222" y1="142" x2="232" y2="142" stroke="currentColor" marker-end="url(#g4a)"/><line x1="334" y1="142" x2="344" y2="142" stroke="currentColor" marker-end="url(#g4a)"/><line x1="446" y1="142" x2="456" y2="142" stroke="currentColor" marker-end="url(#g4a)"/><line x1="558" y1="142" x2="568" y2="142" stroke="currentColor" marker-end="url(#g4a)"/><text x="60" y="186" font-size="12" text-anchor="middle">§7</text><text x="172" y="186" font-size="12" text-anchor="middle">§8</text><text x="284" y="186" font-size="12" text-anchor="middle">§8</text><text x="396" y="186" font-size="12" text-anchor="middle">§10</text><text x="508" y="186" font-size="12" text-anchor="middle">B5 · E8</text><text x="620" y="186" font-size="12" text-anchor="middle">B5 · B8</text><text x="340" y="204" font-size="12" text-anchor="middle">위 줄 = 이 노트의 "마이크 → PCM" 절반 · 아래 줄 = "마이크 여러 개 → 깨끗한 음성" 절반</text>
</svg>
```

그림 1 — 마이크에서 모델까지. 위 줄은 마이크 한 개가 PCM을 만드는 길(1~6절), 아래 줄은 여러 마이크와 스피커가 얽힌 처리(7~8절)와 그 뒤의 ML 경로다. 각 상자 위·아래 숫자는 이 노트의 절 번호다.

Don의 경험으로 번역하면 이렇다.

| 이 노트의 개념 | Don이 이미 아는 것 |
|---|---|
| PDM 비트열 → decimation → PCM | ΣΔ ADC가 들어간 RF 수신기 baseband, oversampling ADC |
| CIC 적분기 오버플로가 "괜찮은" 이유 | 2의 보수 wrap-around 산술, 카운터 차분 (`now - prev`가 wrap 돼도 맞는 이유) |
| I2S/TDM 프레임과 슬롯 | SPI/I2C 프레임, 버스 타이밍 다이어그램, logic analyzer |
| AEC = 알려진 송신 신호의 누설을 추정해서 빼기 | RF self-interference cancellation, TX leakage 보정 |
| 마이크 어레이 TDOA | 안테나 어레이 위상차, AoA |
| dB SPL / dBFS / dBV | dBm, dBFS(ADC), link budget 계산 |

이 표의 오른쪽 열이 Don에게 있다는 것이 이 노트의 출발점이다. 새로 익힐 것은 "음향" 쪽 단위와 직관, 그리고 펌웨어에 들어가는 몇 개의 고전 DSP 알고리즘이다.

---

## 1. MEMS 마이크의 몸속

### 1.1 직관 — 공기가 콘덴서를 민다

**MEMS(Micro-Electro-Mechanical Systems) 마이크**는 실리콘 위에 만든 아주 작은 **콘덴서(capacitor)** 다. 한쪽 판은 얇아서 소리에 흔들리는 **진동판(diaphragm, membrane)**, 다른 쪽 판은 구멍이 숭숭 뚫린 단단한 **backplate**다. 소리(공기 압력 변화)가 진동판을 밀고 당기면 두 판 사이 간격 d가 변하고, 용량 C = ε·A/d가 따라 변한다. 판에 bias 전압(charge pump로 만든 수 V~십수 V — typical, 제품마다 다름)을 걸어 두면 용량 변화가 전압 변화가 된다.

```
  C = ε·A / d          (평행판 콘덴서)
  Q = C·V_bias  ≈ 일정  (큰 저항으로 충전해 둔 상태, 소리 주파수에서는 전하가 거의 못 움직인다)
  V = Q / C = Q·d / (ε·A)   →  V 변화 ∝ d 변화 ∝ 음압
```

말로 하면: 전하를 가둬 둔 콘덴서의 간격이 흔들리면 전압이 간격에 비례해서 흔들린다. 그래서 출력 전압이 음압에 (작은 신호에서) 선형이다.

진동판의 움직임은 나노미터 단위라서 신호가 아주 작다. 그래서 같은 패키지 안에 **ASIC**이 함께 들어 있다. ASIC은 bias(charge pump), 고임피던스 프리앰프, 그리고 디지털 마이크라면 **ΣΔ 변조기**(4절)와 클럭·모드 제어까지 맡는다. Don에게 익숙한 말로 하면, MEMS 마이크는 "센서 다이 + AFE ASIC을 한 캔에 넣은 SiP"다.

```svg
<svg viewBox="0 0 660 270" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="g4b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="30" y="205" width="130" height="22" fill="#888" fill-opacity="0.3" stroke="currentColor"/><rect x="180" y="205" width="230" height="22" fill="#888" fill-opacity="0.3" stroke="currentColor"/><text x="300" y="221" font-size="12" text-anchor="middle">기기 PCB</text><rect x="60" y="190" width="100" height="15" fill="#888" fill-opacity="0.15" stroke="currentColor"/><rect x="180" y="190" width="200" height="15" fill="#888" fill-opacity="0.15" stroke="currentColor"/><path d="M60,190 L60,60 L380,60 L380,190" fill="none" stroke="currentColor" stroke-width="2"/><text x="300" y="52" font-size="12" text-anchor="middle">금속 뚜껑 (lid)</text><rect x="110" y="145" width="35" height="45" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><rect x="195" y="145" width="35" height="45" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><line x1="145" y1="145" x2="195" y2="145" stroke="#e08a3c" stroke-width="3"/><line x1="115" y1="132" x2="225" y2="132" stroke="#4a7bd0" stroke-width="3" stroke-dasharray="6 4"/>
<rect x="270" y="160" width="80" height="30" rx="3" fill="#3f9a6b" fill-opacity="0.3" stroke="currentColor"/><text x="310" y="180" font-size="12" text-anchor="middle">ASIC</text><path d="M222,132 Q250,110 285,160" fill="none" stroke="currentColor"/><text x="220" y="95" font-size="12" text-anchor="middle">back volume (뒤 공기 방)</text><line x1="170" y1="258" x2="170" y2="150" stroke="#d0564a" stroke-width="2" marker-end="url(#g4b)"/><text x="170" y="252" font-size="12" text-anchor="start" dx="8">소리 (bottom port)</text><line x1="400" y1="132" x2="228" y2="132" stroke="currentColor" stroke-opacity="0.5"/><text x="405" y="136" font-size="12">backplate (구멍 뚫린 고정판)</text><line x1="400" y1="150" x2="175" y2="146" stroke="currentColor" stroke-opacity="0.5"/><text x="405" y="154" font-size="12">진동판 (diaphragm) — 음압에 흔들림</text><line x1="400" y1="175" x2="350" y2="175" stroke="currentColor" stroke-opacity="0.5"/><text x="405" y="179" font-size="12">bias · preamp · ΣΔ · 클럭 모드</text><text x="405" y="90" font-size="12">top port: 구멍이 뚜껑 쪽</text><text x="405" y="108" font-size="12">bottom port: 구멍이 PCB 쪽 (PCB에도 구멍)</text>
</svg>
```

그림 2 — bottom-port MEMS 마이크의 단면(개념도, 비율은 맞지 않다). 소리는 PCB 구멍 → 패키지 기판 구멍 → 진동판 순서로 들어온다. 진동판 위의 점선이 구멍 뚫린 backplate, 뚜껑 안 공기 공간이 back volume이다. 와이어로 연결된 ASIC이 신호를 키우고 디지털로 바꾼다.

### 1.2 acoustic port — top vs bottom

- **bottom port**: 소리 구멍이 패키지 바닥(PCB 쪽)에 있다. PCB에도 구멍을 뚫고, 기기 외곽 구멍 → gasket → PCB 구멍 → 마이크로 **음향 통로(acoustic channel)** 를 만든다. 스마트폰·웨어러블에서 흔하다.
- **top port**: 구멍이 뚜껑에 있다. PCB 구멍이 필요 없지만 기기 외곽이 마이크 바로 위에 있어야 한다.

어느 쪽이든 마이크 성능은 **마이크 단품 + 기구 설계**의 합이다. 구멍 크기, 통로 길이, gasket 누설, 방수 membrane이 주파수 응답과 SNR을 바꾼다(9.3절). 데이터시트 숫자는 "이상적인 테스트 지그 위의 단품" 숫자다.

### 1.3 analog vs digital 출력

| 구분 | analog MEMS | digital MEMS (PDM) | digital MEMS (I2S/TDM) |
|---|---|---|---|
| 출력 | 아날로그 전압 (수 mV~수백 mV) | 1비트 ΣΔ 비트열 | PCM 워드 (예: 24비트) |
| 필요한 쪽 | codec·SoC의 ADC + 프리앰프 | SoC/MCU의 PDM 수신기 + decimation | I2S/TDM 수신기만 |
| 장점 | 최고 SNR 제품군, codec 유연성 | 핀 2개, 잡음에 강한 디지털 배선, 칩 쪽 처리 유연 | 호스트 처리 최소 |
| 단점 | 배선이 잡음 안테나, 별도 ADC | 호스트가 decimation 해야 함 | 마이크가 decimation 하느라 전류↑, 선택지 적음 |
| sensitivity 단위 | dBV/Pa | dBFS @ 94 dB SPL | dBFS @ 94 dB SPL |

Don에게 익숙한 비유: analog 마이크는 "RF front-end가 아날로그 IQ를 내보내고 SoC ADC가 받는 구조", PDM 마이크는 "ΣΔ ADC를 센서 쪽에 붙이고 decimation은 baseband에 맡긴 구조", I2S 마이크는 "ADC와 decimation까지 센서 안에 다 넣은 구조"다. 웨어러블은 배선이 짧고 잡음원(BLE 라디오, DC-DC)이 가깝기 때문에 **PDM 디지털 마이크**가 흔한 선택이다(typical).

### 1.4 흔한 함정

- PCB 구멍 위치를 마이크 port와 어긋나게 그리거나, solder paste가 구멍을 막는다 → 감도가 크게 떨어진다. 첫 보드 bring-up에서 "마이크가 이상하게 작다"면 기구·PCB 구멍부터 본다.
- 리플로 후 세척액·먼지가 port로 들어가 진동판이 손상된다 — 공정 가이드(데이터시트 handling 절)를 따른다.

---

## 2. 데이터시트 읽기 — 음향 dB 산수

### 2.1 단위부터: dB SPL, dBFS, dBV

- **dB SPL(Sound Pressure Level)**: 음압 p를 사람이 겨우 듣는 20 µPa에 대한 비로 쓴 것. `L = 20·log10(p / 20 µPa)`. 말로 하면: 20 µPa가 0 dB, 10배 압력이 +20 dB.
- **94 dB SPL = 1 Pa**: 마이크 업계의 기준 레벨. 모든 sensitivity는 "1 kHz 사인, 94 dB SPL에서 출력이 얼마냐"로 적는다.
- **dBFS(dB Full Scale)**: 디지털 출력이 표현할 수 있는 최대값에 대한 비. 0 dBFS가 최대, 그 아래는 음수. (사인의 peak를 full scale에 맞춘 것을 0 dBFS로 볼지 RMS 기준으로 볼지 관례가 둘이라 3 dB 차이가 날 수 있다 — 데이터시트의 정의를 확인한다.)
- **dBV**: 1 V rms 대비 전압. analog 마이크 sensitivity가 `-38 dBV/Pa`면 1 Pa에서 출력 10^(−38/20) V = 12.6 mV rms.
- **dB(A)**: 사람 귀의 주파수 감도를 흉내 낸 **A-weighting** 필터를 거친 값. 저주파·초고주파 잡음을 덜 센다. 마이크 SNR은 거의 항상 A-weighted로 적는다.

### 2.2 핵심 사양 표

| 사양 | 뜻 | typical 범위 (제품마다 다름 — 데이터시트 확인) | 펌웨어/ML에 주는 영향 |
|---|---|---|---|
| sensitivity | 94 dB SPL, 1 kHz에서의 출력 | digital −26 ~ −38 dBFS, analog −38 ~ −44 dBV/Pa | 디지털 gain 설정, 마이크 간 매칭 |
| sensitivity tolerance | 개체 간 편차 | ±1 dB (고급) ~ ±3 dB | beamforming 성능, 기기 간 KWS 편차 |
| SNR | 94 dB SPL 신호 대 자체 잡음 (A-weighted, 20 Hz–20 kHz) | 60 ~ 70 dB(A) 이상 | 조용한 환경에서의 원거리 음성 |
| EIN (equivalent input noise) | 자체 잡음을 입력 음압으로 환산 | = 94 − SNR dB SPL(A) | noise floor |
| AOP (acoustic overload point) | THD가 10%가 되는 음압 | 120 ~ 135 dB SPL | 큰 소리·바람·가까운 입에서 clipping |
| THD | 고조파 왜곡 (예: 94 dB SPL에서) | 0.1 ~ 1% 수준 | 보통 문제 안 됨, AOP 근처에서 급증 |
| 주파수 응답 | 100 Hz ~ 10 kHz 평탄도, 저역 roll-off, 고역 공진 | 저역 −3 dB 수십 Hz, 고역 공진 수~십수 kHz | 기구가 크게 바꾼다 |
| PSRR | 전원 리플이 출력으로 새는 정도 | 수십 dB (예: −70 ~ −100 dBFS 출력) | DC-DC 스위칭·BLE TX 버스트 잡음 |
| 전류 | 동작 모드별 소비 | 고성능 모드 수백 µA ~ 1 mA, 저전력 모드 수십~수백 µA | always-on 전력 예산 |
| 클럭 범위 | 모드별 PDM 클럭 | 저전력 모드 수백 kHz, 고성능 1 ~ 3.3 MHz | 클럭으로 모드 선택 |

"typical" 열의 숫자는 시장 제품의 흔한 범위를 감 잡기 위한 것이다. 실제 설계에서는 반드시 선택한 부품의 데이터시트(min/typ/max, 측정 조건)를 본다.

### 2.3 손으로 계산 — sensitivity −26 dBFS, SNR 64 dB(A), AOP 120 dB SPL

이 숫자 조합은 흔한 디지털 마이크의 사양 예다(특정 제품을 단정하지 않는다).

1. **0 dBFS는 몇 dB SPL인가?** 94 dB SPL에서 −26 dBFS이므로 0 dBFS까지 26 dB 더 크다 → **120 dB SPL**. 말로 하면: 이 마이크의 디지털 출력은 120 dB SPL에서 가득 찬다. AOP와 같다 — 디지털 마이크는 보통 그렇게 맞춰 설계한다.
2. **noise floor(EIN)는?** SNR은 94 dB SPL 신호와 잡음의 차이이므로 잡음 = 94 − 64 = **30 dB SPL(A)**. dBFS로는 30 − 120 = **−90 dBFS**.
3. **dynamic range는?** AOP − noise floor = 120 − 30 = **90 dB**.
4. **조용한 대화(1 m에서 약 60 dB SPL)** 는? 60 − 120 = **−60 dBFS**. int16 full scale 32767의 1/1000 → peak 약 33 LSB.

4번이 펌웨어 엔지니어에게 중요한 숫자다. 1 m 떨어진 대화는 int16 기준 **±33 LSB** 정도밖에 안 된다. 그대로 int16으로 잘라 저장하면 하위 비트만 쓰는 셈이고, 거기에 6절의 디지털 gain이나 log 특징의 정규화가 필요해진다. 반대로 decimation 출력을 24비트나 32비트로 유지하면 이 문제가 줄어든다.

```python
# 데이터시트 숫자로 dBFS ↔ dB SPL, noise floor, int16 코드를 계산하는 코드
import numpy as np
p0 = 20e-6                                   # 0 dB SPL 기준 음압 (Pa)
print(f"94 dB SPL = {p0*10**(94/20):.4f} Pa")
# 디지털 마이크: sensitivity -26 dBFS @ 94 dB SPL, SNR 64 dB(A), AOP 120 dB SPL
sens_dbfs, snr, aop = -26.0, 64.0, 120.0
fs_spl = 94 - sens_dbfs                      # 0 dBFS에 해당하는 SPL
noise_spl = 94 - snr                         # 등가 입력 잡음 (dB SPL, A)
print(f"0 dBFS          = {fs_spl:.0f} dB SPL")
print(f"noise floor     = {noise_spl:.0f} dB SPL(A) = {noise_spl - fs_spl:.0f} dBFS")
print(f"dynamic range   = AOP - noise = {aop - noise_spl:.0f} dB")
for spl in (40, 60, 70, 94, 110):            # 조용한 방, 대화(1 m), 가까운 대화, 기준, 아주 큼
    dbfs = spl + sens_dbfs - 94
    amp = 10**(dbfs/20) * 32767              # 사인 peak 기준 int16 코드
    print(f"{spl:3d} dB SPL -> {dbfs:6.1f} dBFS -> peak ≈ {amp:7.0f} LSB (int16)")
# 아날로그 마이크: -38 dBV/Pa, SNR 65 dB(A)
s = 10**(-38/20)
print(f"analog -38 dBV/Pa = {s*1e3:.2f} mV/Pa, noise = {s*10**(-65/20)*1e6:.2f} uVrms")
```

```text
94 dB SPL = 1.0024 Pa
0 dBFS          = 120 dB SPL
noise floor     = 30 dB SPL(A) = -90 dBFS
dynamic range   = AOP - noise = 90 dB
 40 dB SPL ->  -80.0 dBFS -> peak ≈       3 LSB (int16)
 60 dB SPL ->  -60.0 dBFS -> peak ≈      33 LSB (int16)
 70 dB SPL ->  -50.0 dBFS -> peak ≈     104 LSB (int16)
 94 dB SPL ->  -26.0 dBFS -> peak ≈    1642 LSB (int16)
110 dB SPL ->  -10.0 dBFS -> peak ≈   10362 LSB (int16)
analog -38 dBV/Pa = 12.59 mV/Pa, noise = 7.08 uVrms
```

출력에서 볼 것: 94 dB SPL이 정확히 1 Pa(1.0024)이고, 40 dB SPL 조용한 방 소리는 int16으로 겨우 3 LSB다. analog 마이크의 자체 잡음은 7 µV rms — 이 신호를 PCB 위로 수 cm 끌고 가면 DC-DC·라디오 잡음이 쉽게 더 커진다. 이게 웨어러블이 디지털 마이크를 좋아하는 이유다.

```svg
<svg viewBox="0 0 640 290" xmlns="http://www.w3.org/2000/svg">
<line x1="200" y1="20" x2="200" y2="260" stroke="currentColor"/><text x="190" y="14" font-size="12" text-anchor="end">dB SPL</text><text x="212" y="14" font-size="12">dBFS (이 마이크)</text><rect x="200" y="54" width="40" height="155" fill="#3f9a6b" fill-opacity="0.2" stroke="none"/><line x1="194" y1="260" x2="206" y2="260" stroke="currentColor"/><text x="190" y="264" font-size="12" text-anchor="end">0</text><text x="250" y="264" font-size="12">사람이 듣는 한계 (20 µPa)</text><line x1="194" y1="209" x2="246" y2="209" stroke="#d0564a" stroke-width="2"/><text x="190" y="213" font-size="12" text-anchor="end">30</text><text x="250" y="213" font-size="12">마이크 자체 잡음 = 94 − SNR = −90 dBFS</text><line x1="194" y1="191" x2="206" y2="191" stroke="currentColor"/><text x="190" y="195" font-size="12" text-anchor="end">40</text><text x="250" y="195" font-size="12">조용한 방 (−80 dBFS, int16 ±3 LSB)</text><line x1="194" y1="157" x2="206" y2="157" stroke="currentColor"/><text x="190" y="161" font-size="12" text-anchor="end">60</text><text x="250" y="161" font-size="12">1 m 대화 (−60 dBFS, ±33 LSB)</text>
<line x1="194" y1="99" x2="246" y2="99" stroke="#4a7bd0" stroke-width="2"/><text x="190" y="103" font-size="12" text-anchor="end">94</text><text x="250" y="103" font-size="12">기준 1 Pa → sensitivity −26 dBFS</text><line x1="194" y1="54" x2="246" y2="54" stroke="#e08a3c" stroke-width="2"/><text x="190" y="58" font-size="12" text-anchor="end">120</text><text x="250" y="58" font-size="12">AOP = 0 dBFS (여기서부터 clipping)</text><line x1="194" y1="20" x2="206" y2="20" stroke="currentColor"/><text x="190" y="24" font-size="12" text-anchor="end">140</text><text x="120" y="135" font-size="12" text-anchor="middle">dynamic</text><text x="120" y="151" font-size="12" text-anchor="middle">range 90 dB</text><line x1="150" y1="54" x2="150" y2="209" stroke="#3f9a6b" stroke-width="2"/>
</svg>
```

그림 3 — dB 사다리. 왼쪽 눈금은 dB SPL, 오른쪽 설명은 sensitivity −26 dBFS · SNR 64 dB · AOP 120 dB SPL 마이크에서의 dBFS다. 초록 띠(30~120 dB SPL)가 이 마이크가 표현할 수 있는 dynamic range 90 dB다.

### 2.4 PSRR과 전원 — RF 엔지니어의 직감이 맞는 곳

PSRR(power supply rejection ratio)은 VDD에 들어간 리플이 출력에 얼마나 새느냐다. 웨어러블에서 BLE 송신 버스트(예: 수 ms 주기의 TX)나 DC-DC 스위칭이 마이크 VDD를 흔들면, 그 주기의 "윙" 또는 "따닥" 소리가 오디오에 섞인다(GSM 시대의 "TDMA buzz"와 같은 현상). KWS 모델은 이런 주기성 잡음을 학습 데이터에서 본 적이 없으니 오탐·미탐이 늘 수 있다. 해결은 Don이 아는 그대로다: 마이크 전용 LDO나 RC 필터, 접지 분리, 그리고 **라디오 TX 타이밍과 오디오 캡처를 함께 찍어 보는 측정**.

### 2.5 전류와 모드 — always-on 예산의 첫 줄

많은 디지털 마이크는 **입력 클럭 주파수로 동작 모드를 고른다**(typical): 클럭을 낮추면(예: 수백 kHz) 저전력 모드, 높이면(예: 1~3 MHz대) 고성능 모드. 저전력 모드는 전류가 작지만 SNR이 조금 낮거나 대역이 좁을 수 있다. 일부 제품은 sleep 모드(클럭을 끊으면 수 µA)도 있다. 그래서 always-on 단계에서는 저전력 모드로 VAD만 돌리고, 말소리가 감지되면 클럭을 올려 고성능 모드로 전환하는 설계가 가능하다 — 모드 전환 시 마이크 출력이 안정될 때까지 걸리는 시간(데이터시트의 mode switch / startup time, 수 ms~수십 ms 수준)을 링버퍼 설계에 넣어야 한다.

어떤 마이크·codec은 **하드웨어 VAD / wake-on-sound** 기능을 내장해 호스트를 깨우는 인터럽트를 낸다(제품에 따라 다름). 그러면 호스트 SoC의 PDM 블록까지 꺼 둘 수 있다.

---

## 3. 인터페이스 — PDM, I2S, TDM, SoundWire

### 3.1 PDM — 두 선, 마이크 두 개

**PDM(Pulse Density Modulation)** 은 1비트 비트열에서 **1의 밀도**가 신호 값을 나타내는 방식이다(4절에서 만드는 법을 본다). 선은 두 개뿐이다.

- **CLK**: 호스트(SoC/MCU)가 마이크에 공급. typical 1~3.25 MHz (저전력 모드는 더 낮게).
- **DATA**: 마이크가 클럭에 맞춰 1비트씩 내보낸다.

마이크 두 개가 **같은 CLK와 같은 DATA 선을 공유**할 수 있다. 각 마이크에는 L/R(또는 SELECT) 핀이 있어서, 하나는 CLK의 한 반주기 동안 DATA를 구동하고 나머지 반주기에는 출력을 **high-Z**로 놓는다. 다른 마이크는 반대 반주기를 쓴다. 호스트는 상승 에지와 하강 에지에서 각각 샘플해 두 채널을 분리한다. 어느 L/R 설정이 어느 에지에 대응하는지, 데이터가 에지 후 몇 ns 뒤에 유효한지는 마이크 데이터시트와 호스트 PDM 블록 문서를 맞춰 봐야 한다 — bring-up에서 L/R이 뒤바뀌는 일이 흔하다.

### 3.2 I2S — 오디오 PCM의 표준 직렬 버스

**I2S(Inter-IC Sound)** 는 PCM 샘플을 실어 나르는 3선 버스다.

- **BCLK(bit clock, SCK)**: 비트마다 한 클럭.
- **WS(word select) = LRCLK**: 0이면 왼쪽, 1이면 오른쪽 채널. WS 한 주기 = 샘플링 주기 1개.
- **SD(serial data)**: MSB부터. 원조 Philips I2S 형식에서는 WS가 바뀐 뒤 **BCLK 1개 늦게** MSB가 나온다. left-justified 형식은 지연이 없다. 이 1비트 차이를 잘못 맞추면 샘플이 2배 커지거나 반으로 줄고 부호가 깨진다 — 고전적인 bring-up 버그다.

손계산: 16 kHz 스테레오, 슬롯당 32 BCLK면 `BCLK = 16,000 × 2 × 32 = 1.024 MHz`. 48 kHz 스테레오 32비트 슬롯이면 3.072 MHz.

### 3.3 TDM — 슬롯 여러 개

**TDM**은 I2S를 일반화해 한 프레임에 슬롯을 N개(4, 8, 16) 넣는다. 프레임 시작은 FS(frame sync) 펄스로 알린다. 마이크 4개 + AEC reference 2채널을 codec에서 SoC로 한 줄에 실어 보낼 때 쓴다. `BCLK = fs × 슬롯 수 × 슬롯 비트`: 48 kHz × 8 × 32 = 12.288 MHz.

일부 디지털 마이크는 PDM 대신 I2S/TDM 출력을 직접 낸다(마이크 내부에서 decimation). 호스트가 편해지는 대신 마이크 전류가 커지는 경향이 있다(typical).

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13">(a) PDM 스테레오 — 클럭 1개, 데이터선 1개, 마이크 2개</text><text x="10" y="50" font-size="12">CLK</text>
<polyline points="100,40 130,40 130,60 160,60 160,40 190,40 190,60 220,60 220,40 250,40 250,60 280,60 280,40 310,40 310,60 340,60 340,40 370,40 370,60 400,60 400,40 430,40 430,60 460,60 460,40 490,40 490,60 520,60 520,40 550,40 550,60 580,60 580,40 610,40 610,60 640,60" fill="none" stroke="currentColor"/>
<text x="10" y="90" font-size="12">DATA</text><path d="M100,85 L104,76 L126,76 L130,85 L126,94 L104,94 Z" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="115.0" y="89" font-size="12" text-anchor="middle">L</text><path d="M130,85 L134,76 L156,76 L160,85 L156,94 L134,94 Z" fill="#e08a3c" fill-opacity="0.18" stroke="currentColor"/><text x="145.0" y="89" font-size="12" text-anchor="middle">R</text><path d="M160,85 L164,76 L186,76 L190,85 L186,94 L164,94 Z" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="175.0" y="89" font-size="12" text-anchor="middle">L</text><path d="M190,85 L194,76 L216,76 L220,85 L216,94 L194,94 Z" fill="#e08a3c" fill-opacity="0.18" stroke="currentColor"/><text x="205.0" y="89" font-size="12" text-anchor="middle">R</text><path d="M220,85 L224,76 L246,76 L250,85 L246,94 L224,94 Z" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="235.0" y="89" font-size="12" text-anchor="middle">L</text><path d="M250,85 L254,76 L276,76 L280,85 L276,94 L254,94 Z" fill="#e08a3c" fill-opacity="0.18" stroke="currentColor"/><text x="265.0" y="89" font-size="12" text-anchor="middle">R</text>
<path d="M280,85 L284,76 L306,76 L310,85 L306,94 L284,94 Z" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="295.0" y="89" font-size="12" text-anchor="middle">L</text><path d="M310,85 L314,76 L336,76 L340,85 L336,94 L314,94 Z" fill="#e08a3c" fill-opacity="0.18" stroke="currentColor"/><text x="325.0" y="89" font-size="12" text-anchor="middle">R</text><path d="M340,85 L344,76 L366,76 L370,85 L366,94 L344,94 Z" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="355.0" y="89" font-size="12" text-anchor="middle">L</text><path d="M370,85 L374,76 L396,76 L400,85 L396,94 L374,94 Z" fill="#e08a3c" fill-opacity="0.18" stroke="currentColor"/><text x="385.0" y="89" font-size="12" text-anchor="middle">R</text><path d="M400,85 L404,76 L426,76 L430,85 L426,94 L404,94 Z" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="415.0" y="89" font-size="12" text-anchor="middle">L</text><path d="M430,85 L434,76 L456,76 L460,85 L456,94 L434,94 Z" fill="#e08a3c" fill-opacity="0.18" stroke="currentColor"/><text x="445.0" y="89" font-size="12" text-anchor="middle">R</text>
<path d="M460,85 L464,76 L486,76 L490,85 L486,94 L464,94 Z" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="475.0" y="89" font-size="12" text-anchor="middle">L</text><path d="M490,85 L494,76 L516,76 L520,85 L516,94 L494,94 Z" fill="#e08a3c" fill-opacity="0.18" stroke="currentColor"/><text x="505.0" y="89" font-size="12" text-anchor="middle">R</text><path d="M520,85 L524,76 L546,76 L550,85 L546,94 L524,94 Z" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="535.0" y="89" font-size="12" text-anchor="middle">L</text><path d="M550,85 L554,76 L576,76 L580,85 L576,94 L554,94 Z" fill="#e08a3c" fill-opacity="0.18" stroke="currentColor"/><text x="565.0" y="89" font-size="12" text-anchor="middle">R</text><path d="M580,85 L584,76 L606,76 L610,85 L606,94 L584,94 Z" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="595.0" y="89" font-size="12" text-anchor="middle">L</text><path d="M610,85 L614,76 L636,76 L640,85 L636,94 L614,94 Z" fill="#e08a3c" fill-opacity="0.18" stroke="currentColor"/><text x="625.0" y="89" font-size="12" text-anchor="middle">R</text>
<text x="100" y="115" font-size="12">마이크 A(L)는 CLK high 반주기에, 마이크 B(R)는 low 반주기에 데이터를 내고 나머지 반주기엔 high-Z</text><text x="10" y="148" font-size="13">(b) I2S — BCLK, WS(LRCLK), SD. WS 한 주기 = 샘플 1개(L+R)</text><text x="10" y="176" font-size="12">BCLK</text>
<polyline points="100,184 110,184 110,166 120,166 120,184 130,184 130,166 140,166 140,184 150,184 150,166 160,166 160,184 170,184 170,166 180,166 180,184 190,184 190,166 200,166 200,184 210,184 210,166 220,166 220,184 230,184 230,166 240,166 240,184 250,184 250,166 260,166 260,184 270,184 270,166 280,166 280,184 290,184 290,166 300,166 300,184 310,184 310,166 320,166 320,184 330,184 330,166 340,166 340,184 350,184 350,166 360,166 360,184 370,184 370,166 380,166 380,184 390,184 390,166 400,166 400,184 410,184 410,166 420,166 420,184 430,184 430,166 440,166 440,184 450,184 450,166 460,166 460,184 470,184 470,166 480,166 480,184 490,184 490,166 500,166 500,184 510,184 510,166 520,166 520,184 530,184 530,166 540,166 540,184 550,184 550,166 560,166 560,184 570,184 570,166 580,166 580,184 590,184 590,166 600,166 600,184 610,184 610,166 620,166 620,184 630,184 630,166 640,166 640,184 650,184 650,166 660,166" fill="none" stroke="currentColor"/>
<text x="10" y="208" font-size="12">WS</text><polyline points="100,212 110,212 110,198 390,198 390,212 660,212" fill="none" stroke="currentColor"/><text x="250" y="194" font-size="12" text-anchor="middle">L 채널 (WS=0)</text><text x="525" y="194" font-size="12" text-anchor="middle">R 채널 (WS=1)</text><text x="10" y="238" font-size="12">SD</text><path d="M100,234 L104,225 L126,225 L130,234 L126,243 L104,243 Z" fill="none" fill-opacity="0.18" stroke="currentColor"/><path d="M130,234 L134,225 L146,225 L150,234 L146,243 L134,243 Z" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor"/><text x="140.0" y="238" font-size="12" text-anchor="middle">MSB</text><path d="M150,234 L154,225 L166,225 L170,234 L166,243 L154,243 Z" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><path d="M170,234 L174,225 L186,225 L190,234 L186,243 L174,243 Z" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><path d="M190,234 L194,225 L206,225 L210,234 L206,243 L194,243 Z" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><path d="M210,234 L214,225 L226,225 L230,234 L226,243 L214,243 Z" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/>
<path d="M230,234 L234,225 L386,225 L390,234 L386,243 L234,243 Z" fill="none" fill-opacity="0.18" stroke="currentColor"/><text x="310.0" y="238" font-size="12" text-anchor="middle">… LSB, 남는 비트</text><path d="M390,234 L394,225 L406,225 L410,234 L406,243 L394,243 Z" fill="none" fill-opacity="0.18" stroke="currentColor"/><path d="M410,234 L414,225 L426,225 L430,234 L426,243 L414,243 Z" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor"/><path d="M430,234 L434,225 L656,225 L660,234 L656,243 L434,243 Z" fill="#e08a3c" fill-opacity="0.18" stroke="currentColor"/><text x="545.0" y="238" font-size="12" text-anchor="middle">R 샘플 (MSB부터)</text><text x="140" y="260" font-size="12">WS가 바뀐 뒤 BCLK 1개 늦게 MSB (Philips I2S). left-justified는 지연 없음</text><text x="10" y="290" font-size="13">(c) TDM — FS 펄스 하나에 슬롯 N개 (여기선 8개, 마이크·채널마다 슬롯 1개)</text><text x="10" y="318" font-size="12">FS</text><polyline points="100,322 110,322 110,306 130,306 130,322 660,322" fill="none" stroke="currentColor"/><text x="10" y="342" font-size="12">SD</text>
<path d="M130,338 L134,329 L191,329 L195,338 L191,347 L134,347 Z" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="162.5" y="342" font-size="12" text-anchor="middle">slot 0</text><path d="M195,338 L199,329 L256,329 L260,338 L256,347 L199,347 Z" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="227.5" y="342" font-size="12" text-anchor="middle">slot 1</text><path d="M260,338 L264,329 L321,329 L325,338 L321,347 L264,347 Z" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="292.5" y="342" font-size="12" text-anchor="middle">slot 2</text><path d="M325,338 L329,329 L386,329 L390,338 L386,347 L329,347 Z" fill="#d0564a" fill-opacity="0.25" stroke="currentColor"/><text x="357.5" y="342" font-size="12" text-anchor="middle">slot 3</text><path d="M390,338 L394,329 L451,329 L455,338 L451,347 L394,347 Z" fill="#888" fill-opacity="0.25" stroke="currentColor"/><text x="422.5" y="342" font-size="12" text-anchor="middle">slot 4</text><path d="M455,338 L459,329 L516,329 L520,338 L516,347 L459,347 Z" fill="#888" fill-opacity="0.25" stroke="currentColor"/><text x="487.5" y="342" font-size="12" text-anchor="middle">slot 5</text>
<path d="M520,338 L524,329 L581,329 L585,338 L581,347 L524,347 Z" fill="#888" fill-opacity="0.25" stroke="currentColor"/><text x="552.5" y="342" font-size="12" text-anchor="middle">slot 6</text><path d="M585,338 L589,329 L646,329 L650,338 L646,347 L589,347 Z" fill="#888" fill-opacity="0.25" stroke="currentColor"/><text x="617.5" y="342" font-size="12" text-anchor="middle">slot 7</text>
</svg>
```

그림 4 — 세 인터페이스의 타이밍(개념도). (a) PDM: 두 마이크가 CLK의 서로 다른 반주기에 같은 DATA 선을 구동한다. (b) I2S: WS가 채널을 고르고, Philips 형식은 WS 전환 후 BCLK 1개 뒤에 MSB가 온다. (c) TDM: FS 펄스 뒤에 슬롯이 줄지어 온다. 마이크·codec마다 에지 극성과 지연이 다르니 데이터시트로 맞춘다.

### 3.4 SoundWire와 그 밖의 것

**SoundWire**는 MIPI Alliance가 정의한 2선(clock + data) 멀티드롭 오디오 버스로, 여러 codec·앰프·마이크를 한 버스에 달고 제어와 오디오를 같이 보낸다(E8 1.3절). 일부 모바일 플랫폼이 쓴다. 웨어러블에서 쓸지는 부품 생태계에 달려 있고 Hark의 구성은 알 수 없다. analog 마이크는 codec의 ADC로 들어가고, codec이 I2S/TDM/SoundWire로 SoC에 보낸다.

### 3.5 SoC/MCU 쪽 블록 — 이름은 달라도 하는 일은 같다

| 계열 (예시) | 블록 이름 (예) | 하는 일 |
|---|---|---|
| 범용 MCU (STM32 계열 등) | DFSDM, MDF/ADF (제품군마다 다름) | PDM 비트를 받아 sinc(CIC) 필터 + 적분기로 decimation, DMA로 PCM 출력 |
| BLE SoC (Nordic nRF52 계열 등) | PDM 주변장치 | PDM 클럭 생성 + 내장 decimation으로 16 kHz 근처 PCM을 EasyDMA로 RAM에 |
| 응용 프로세서 (NXP i.MX 계열 등) | PDM 인터페이스 (MICFIL 등) | 여러 채널 PDM decimation, FIFO + DMA |
| 모바일 SoC (Qualcomm 등) | 저전력 오디오 서브시스템의 DMIC 입력 (구체적 구조는 비공개 부분이 많다) | always-on island에서 마이크 수신과 전처리 |
| 범용 (어디나) | I2S/SAI/TDM 컨트롤러 | PCM 워드 수신, 슬롯 마스크, DMA |

표의 블록 이름은 대표적인 예이고 세부 기능(필터 차수, 내장 HPF, gain 레지스터)은 칩마다 다르다. PDM 블록이 없으면 SPI나 I2S 수신기로 PDM 비트를 그냥 받아서 소프트웨어로 decimation 하는 방법도 있다(10절의 비용 계산).

### 3.6 흔한 함정

- PDM 클럭을 바꿨는데 decimation 비율을 안 바꿔서 PCM 샘플률이 틀어진다 → 모델 입력이 "피치가 바뀐 목소리"가 된다. 예: nRF 계열 PDM의 기본 클럭이 1.032 MHz 근처라 64분주하면 16 kHz가 아니라 약 16.1 kHz가 되는 식의 미세한 차이도 오래 쌓이면 G7의 drift 문제가 된다.
- L/R 선택 핀을 floating으로 둔다 → 두 마이크가 같은 반주기에 DATA를 구동해 충돌.
- I2S의 1비트 지연 형식 불일치 → 값이 2배/절반, 부호 뒤집힘.

---

## 4. ΣΔ 변조와 PDM — 1비트로 소리를 담는 법

### 4.1 직관 — "빨리, 자주, 틀린 만큼 기억하기"

1비트로 0.37 같은 값을 표현하려면? 한 번에는 못 한다. 대신 **아주 빨리 여러 번** 1과 −1을 내되, 그 평균이 0.37이 되게 하면 된다. **ΣΔ(sigma-delta) 변조기**는 "지금까지 낸 출력과 입력의 차이(Δ)를 누적(Σ)해 두었다가, 누적 오차가 양수면 +1, 음수면 −1을 내는" 피드백 루프다. 오차를 잊지 않기 때문에 장기 평균이 입력을 정확히 따라간다.

Don에게 익숙한 비유: PWM 디밍에서 duty를 10비트 정밀도로 만들기 위해 매 주기 남은 오차를 다음 주기에 더해 주는 **error diffusion / dithering** 트릭, 혹은 분수 분주 PLL의 ΣΔ 분주비 제어와 같은 원리다.

```
1차 ΣΔ (한 샘플마다):
   q[n] = sign(v[n])            비교기: 누적 오차의 부호 → +1 또는 −1
   v[n+1] = v[n] + x[n] − q[n]  적분기: (입력 − 출력)을 누적
```

말로 하면: 누적 오차 v가 양수면 "지금까지 덜 냈다"는 뜻이니 +1을 내고, 음수면 −1을 낸다.

손계산 (x = 0.5로 일정, v[0] = 0):

| n | v[n] | q[n] | v[n+1] = v + 0.5 − q |
|---|---|---|---|
| 0 | 0 | +1 | −0.5 |
| 1 | −0.5 | −1 | 1.0 |
| 2 | 1.0 | +1 | 0.5 |
| 3 | 0.5 | +1 | 0.0 |
| 4 | 0.0 | +1 | −0.5 |

n=1~4의 출력은 −1, +1, +1, +1 → 평균 0.5, 그리고 v가 n=1의 상태(−0.5)로 돌아왔으므로 이 4개 패턴이 반복된다. 1의 밀도 = (1 + 0.5)/2 = 75%. 비트로 쓰면 "1110" 반복이다.

### 4.2 코드로 확인 — 1차·2차 ΣΔ로 PDM 비트열 만들기

아래 모듈을 이후 예제들이 import 한다. 2차 변조기는 적분기를 두 개 쓰는 표준 구조(CIFB의 한 형태)다.

```python
# sd.py — 1차/2차 ΣΔ 변조기 (입력 x는 -1..1, 출력은 ±1)
import numpy as np
def sd1(x):                                   # 1차 ΣΔ: 적분기 1개 + 1비트 비교기
    v, y = 0.0, np.empty(len(x))
    for n, xn in enumerate(x):
        q = 1.0 if v >= 0 else -1.0           # 비교기 (이전 적분값의 부호)
        y[n] = q
        v += xn - q                           # 적분기: 입력 - 피드백(오차 누적)
    return y
def sd2(x):                                   # 2차 ΣΔ: 적분기 2개 (CIFB 구조)
    v1 = v2 = 0.0; y = np.empty(len(x))
    for n, xn in enumerate(x):
        q = 1.0 if v2 >= 0 else -1.0
        y[n] = q
        v1 += xn - q
        v2 += v1 - 2*q
    return y
```

1 kHz 사인(진폭 0.5)을 16 kHz × 64 = 1.024 MHz로 변조해서 실제 비트를 본다.

```python
# PDM 비트열에서 '1의 밀도'가 신호 값을 따라가는지 확인하는 코드
import numpy as np
from sd import sd1, sd2
fs, osr = 1_024_000, 64                        # PDM 클럭 1.024 MHz = 16 kHz × 64
n = np.arange(2**16)
x = 0.5*np.sin(2*np.pi*1000*n/fs)              # 1 kHz, 진폭 0.5 (= -6 dBFS), 한 주기 = 1024 비트
for name, f in (("1st", sd1), ("2nd", sd2)):
    bits = ((f(x) + 1)//2).astype(int)         # +1 -> 1, -1 -> 0 (선 위의 실제 비트)
    print(name, "x≈0   bits[0:32]   :", "".join(map(str, bits[:32])))
    print(name, "x=+0.5 bits[256:288]:", "".join(map(str, bits[256:288])))
    print(name, "x=-0.5 bits[768:800]:", "".join(map(str, bits[768:800])))
    print(f"{name} ones density, 1st half-period {bits[:512].mean():.3f}, 2nd half {bits[512:1024].mean():.3f}")
print("expected (1 + mean x)/2 :", round((1 + x[:512].mean())/2, 3), round((1 + x[512:1024].mean())/2, 3))
```

```text
1st x≈0   bits[0:32]   : 10101010101010101010101010110101
1st x=+0.5 bits[256:288]: 11101110111011101110111011101110
1st x=-0.5 bits[768:800]: 01000100010001000100010001000100
1st ones density, 1st half-period 0.658, 2nd half 0.342
2nd x≈0   bits[0:32]   : 10011001100110101001101100110011
2nd x=+0.5 bits[256:288]: 11011101110111011101110111011101
2nd x=-0.5 bits[768:800]: 10001000100010001000100010001000
2nd ones density, 1st half-period 0.660, 2nd half 0.340
expected (1 + mean x)/2 : 0.659 0.341
```

출력에서 볼 것: 사인이 +0.5인 부근에서 1차 변조기는 손계산과 똑같은 "1110" 반복을, −0.5 부근에서는 "0100"(1의 밀도 25%)을 낸다. 반주기 평균 밀도 0.658/0.342가 이론값 0.659/0.341과 맞는다. 2차 변조기의 패턴은 덜 규칙적이다 — 그 "덜 규칙적임"이 바로 다음 절의 noise shaping이다.

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
<line x1="40" y1="80" x2="630" y2="80" stroke="currentColor" stroke-opacity="0.3"/>
<polyline points="42.3,80.0 51.5,76.1 60.7,72.2 69.9,68.4 79.1,64.7 88.3,61.1 97.5,57.8 106.7,54.6 115.9,51.7 125.1,49.1 134.3,46.7 143.5,44.7 152.7,43.0 161.9,41.7 171.1,40.8 180.3,40.2 189.5,40.0 198.7,40.2 207.9,40.8 217.1,41.7 226.3,43.0 235.5,44.7 244.7,46.7 253.9,49.1 263.1,51.7 272.3,54.6 281.5,57.8 290.7,61.1 299.9,64.7 309.1,68.4 318.3,72.2 327.5,76.1 336.7,80.0 345.9,83.9 355.1,87.8 364.3,91.6 373.5,95.3 382.7,98.9 391.9,102.2 401.1,105.4 410.3,108.3 419.5,110.9 428.7,113.3 437.9,115.3 447.1,117.0 456.3,118.3 465.5,119.2 474.7,119.8 483.9,120.0 493.1,119.8 502.3,119.2 511.5,118.3 520.7,117.0 529.9,115.3 539.1,113.3 548.3,110.9 557.5,108.3 566.7,105.4 575.9,102.2 585.1,98.9 594.3,95.3 603.5,91.6 612.7,87.8 621.9,83.9" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<polyline points="42.3,86.2 51.5,86.2 60.7,73.8 69.9,73.8 79.1,67.5 88.3,67.5 97.5,61.2 106.7,55.0 115.9,55.0 125.1,48.8 134.3,55.0 143.5,48.8 152.7,48.8 161.9,42.5 171.1,42.5 180.3,42.5 189.5,42.5 198.7,42.5 207.9,36.2 217.1,42.5 226.3,42.5 235.5,42.5 244.7,48.8 253.9,48.8 263.1,48.8 272.3,55.0 281.5,55.0 290.7,55.0 299.9,61.2 309.1,67.5 318.3,67.5 327.5,73.8 336.7,73.8 345.9,73.8 355.1,80.0 364.3,86.2 373.5,92.5 382.7,92.5 391.9,92.5 401.1,98.8 410.3,105.0 419.5,111.2 428.7,111.2 437.9,111.2 447.1,111.2 456.3,117.5 465.5,117.5 474.7,117.5 483.9,117.5 493.1,117.5 502.3,123.8 511.5,117.5 520.7,117.5 529.9,111.2 539.1,117.5 548.3,117.5 557.5,111.2 566.7,105.0 575.9,105.0 585.1,105.0 594.3,98.8 603.5,98.8 612.7,92.5 621.9,86.2" fill="none" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="4 3"/>
<polyline points="40.0,190 44.6,190 44.6,150 53.8,150 53.8,190 58.4,190 58.4,150 63.0,150 63.0,190 67.6,190 67.6,150 72.2,150 72.2,190 76.8,190 76.8,150 81.4,150 81.4,190 86.0,190 86.0,150 99.8,150 99.8,190 104.4,190 104.4,150 113.6,150 113.6,190 118.2,190 118.2,150 141.2,150 141.2,190 145.8,190 145.8,150 164.2,150 164.2,190 168.8,190 168.8,150 219.4,150 219.4,190 224.0,190 224.0,150 247.0,150 247.0,190 251.6,190 251.6,150 274.6,150 274.6,190 279.2,190 279.2,150 293.0,150 293.0,190 297.6,190 297.6,150 302.2,150 302.2,190 306.8,190 306.8,150 320.6,150 320.6,190 325.2,190 325.2,150 329.8,150 329.8,190 334.4,190 334.4,150 339.0,150 339.0,190 343.6,190 343.6,150 348.2,150 348.2,190 352.8,190 352.8,150 357.4,150 357.4,190 366.6,190 366.6,150 371.2,150 371.2,190 375.8,190 375.8,150 380.4,150 380.4,190 385.0,190 385.0,150 389.6,150 389.6,190 408.0,190 408.0,150 412.6,150 412.6,190 421.8,190 421.8,150 426.4,150 426.4,190 454.0,190 454.0,150 458.6,150 458.6,190 495.4,190 495.4,150 500.0,150 500.0,190 536.8,190 536.8,150 541.4,150 541.4,190 559.8,190 559.8,150 564.4,150 564.4,190 582.8,190 582.8,150 587.4,150 587.4,190 592.0,190 592.0,150 596.6,150 596.6,190 610.4,190 610.4,150 615.0,150 615.0,190 619.6,190 619.6,150 624.2,150 624.2,190 628.8,190" fill="none" stroke="currentColor"/>
<text x="10" y="84" font-size="12">x</text><text x="10" y="154" font-size="12">1</text><text x="10" y="194" font-size="12">0</text><text x="340" y="215" font-size="12" text-anchor="middle">PDM 비트 128개 (2차 ΣΔ) — 위가 1, 아래가 0</text><line x1="172" y1="26" x2="194" y2="26" stroke="#4a7bd0" stroke-width="2"/><line x1="432" y1="26" x2="454" y2="26" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="4 3"/><text x="200" y="30" font-size="12">입력 x (진폭 0.8)</text><text x="460" y="30" font-size="12">비트 16개 이동평균</text>
</svg>
```

그림 5 — 2차 ΣΔ가 만든 PDM 비트열(아래, 실제 계산값)과 입력 사인(파랑). 입력이 클 때는 1이 빽빽하고 작을 때는 0이 빽빽하다. 비트 16개를 단순 평균만 해도(주황 점선) 사인 모양이 대충 돌아온다 — decimation 필터는 이 "평균"을 아주 잘 하는 필터다.

### 4.3 noise shaping — 잡음을 높은 주파수로 밀어내기

1비트 양자화는 오차가 엄청나다(신호 −1..1을 ±1로 표현). 그런데 ΣΔ는 그 **양자화 잡음의 스펙트럼 모양을 바꾼다**. z 영역으로 보면 1차 ΣΔ의 출력은

```
Y(z) = X(z) + (1 − z⁻¹)·E(z)          (E = 비교기의 양자화 오차, 대략 백색이라고 가정)
|1 − e^(−jω)| = 2·|sin(ω/2)|  →  ω가 0 근처면 거의 0, ω = π(fs/2)에서 2
```

말로 하면: 신호는 그대로 통과하고, 양자화 잡음은 **미분기(고역통과)** 를 거쳐 나온다. 그래서 저주파(우리가 관심 있는 0~8 kHz)에는 잡음이 거의 없고, 수백 kHz 쪽에 잡음이 몰린다. 2차 ΣΔ는 잡음에 (1 − z⁻¹)²가 곱해져서 저주파가 더 깨끗하다. 이걸 **noise shaping**이라 한다.

oversampling만 해도(잡음을 넓은 대역에 고르게 펴는 것) OSR이 2배 될 때마다 대역 내 잡음이 3 dB 준다. noise shaping이 더해지면 이상적인 L차 변조기는 OSR 2배당 약 (6L + 3) dB씩 좋아진다 — 1차 9 dB, 2차 15 dB. 실제 디지털 마이크 ASIC은 더 높은 차수(4차 등 — typical, 비공개인 경우가 많다)를 쓴다.

### 4.4 코드로 확인 — 대역 내 SNR

```python
# 1차 vs 2차 ΣΔ의 대역 내(0~8 kHz) SNR을 FFT로 재는 코드
import numpy as np
from sd import sd1, sd2
fs, N = 1_024_000, 2**16                       # bin 간격 = 15.625 Hz, 1 kHz = bin 64 (정수 주기)
n = np.arange(N); x = 0.5*np.sin(2*np.pi*1000*n/fs)
w = np.hanning(N)
def inband_snr(y, fb=8000):
    P = np.abs(np.fft.rfft(y*w))**2
    kb = int(fb/(fs/N))                        # 8 kHz까지 = bin 512
    sig = P[61:68].sum()                       # 1 kHz 주변 (Hann 누설 포함)
    noise = P[1:kb+1].sum() - sig              # DC 제외, 0~8 kHz 나머지 = 잡음
    return 10*np.log10(sig/noise), P
for name, f in (("1st-order", sd1), ("2nd-order", sd2)):
    y = f(x)
    snr, P = inband_snr(y)
    print(f"{name}: in-band (0-8 kHz) SNR = {snr:5.1f} dB")
    for fb in (4000, 2000):
        print(f"   if band were 0-{fb//1000} kHz (OSR {fs//(2*fb)}): {inband_snr(y, fb)[0]:5.1f} dB")
```

```text
1st-order: in-band (0-8 kHz) SNR =  46.7 dB
   if band were 0-4 kHz (OSR 128):  53.3 dB
   if band were 0-2 kHz (OSR 256):  76.7 dB
2nd-order: in-band (0-8 kHz) SNR =  65.4 dB
   if band were 0-4 kHz (OSR 128):  82.6 dB
   if band were 0-2 kHz (OSR 256):  97.0 dB
```

출력에서 볼 것: 같은 1비트, 같은 64배 oversampling인데 2차가 1차보다 약 19 dB 좋다. 대역을 절반으로 줄일 때(= OSR 2배) 2차는 +17, +14 dB로 이론값 15 dB 근처로 좋아진다. 1차는 +6.6 dB 뒤 +23 dB로 들쭉날쭉한데, 1차 변조기는 입력이 단순하면 잡음이 백색이 아니라 **특정 주파수의 tone(idle tone)** 으로 몰리기 때문이다(다음 그림의 뾰족한 봉우리들). 실제 마이크가 2차 이상을 쓰는 이유 중 하나다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="250" x2="640" y2="250" stroke="currentColor"/><line x1="60" y1="30" x2="60" y2="250" stroke="currentColor"/><line x1="60" y1="93" x2="640" y2="93" stroke="currentColor" stroke-opacity="0.15"/><line x1="60" y1="156" x2="640" y2="156" stroke="currentColor" stroke-opacity="0.15"/><line x1="60" y1="219" x2="640" y2="219" stroke="currentColor" stroke-opacity="0.15"/><text x="54" y="34" font-size="12" text-anchor="end">0</text><text x="54" y="97" font-size="12" text-anchor="end">−40</text><text x="54" y="160" font-size="12" text-anchor="end">−80</text><text x="54" y="223" font-size="12" text-anchor="end">−120</text><text x="60" y="268" font-size="12" text-anchor="middle">100</text><text x="216" y="268" font-size="12" text-anchor="middle">1k</text><text x="373" y="268" font-size="12" text-anchor="middle">10k</text><text x="529" y="268" font-size="12" text-anchor="middle">100k</text><text x="640" y="268" font-size="12" text-anchor="middle">512k</text><text x="350" y="290" font-size="12" text-anchor="middle">주파수 (Hz, 로그 축) · 세로: 신호 peak 대비 dB</text>
<line x1="358" y1="30" x2="358" y2="250" stroke="#3f9a6b" stroke-dasharray="5 4"/><text x="362" y="44" font-size="12">8 kHz (음성 대역 끝)</text><line x1="216" y1="30" x2="216" y2="250" stroke="currentColor" stroke-width="2"/><text x="222" y="44" font-size="12">1 kHz 신호</text>
<polyline points="65,233 75,246 85,235 94,233 104,229 114,226 124,228 134,224 144,220 153,217 163,221 173,220 183,211 193,212 203,211 242,203 252,199 262,166 271,194 281,195 291,134 301,190 311,153 321,184 330,142 340,150 350,133 360,172 370,134 379,126 389,134 399,130 409,130 419,119 429,133 438,123 448,121 458,124 468,117 478,116 488,110 497,111 507,112 517,113 527,104 537,105 547,104 556,101 566,97 576,97 586,94 596,79 606,82 615,82 625,82 635,80" fill="none" stroke="#e08a3c" stroke-width="1.5"/>
<polyline points="65,250 75,250 85,250 94,250 104,250 114,250 124,250 134,250 144,250 153,250 163,247 173,250 183,220 193,237 203,227 242,201 252,208 262,217 271,193 281,199 291,187 301,187 311,186 321,178 330,172 340,167 350,163 360,162 370,160 379,160 389,153 399,147 409,147 419,140 429,137 438,136 448,130 458,127 468,120 478,119 488,116 497,109 507,107 517,105 527,101 537,98 547,94 556,92 566,90 576,90 586,89 596,79 606,82 615,82 625,82 635,81" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<line x1="440" y1="196" x2="464" y2="196" stroke="#e08a3c" stroke-width="1.5"/><line x1="440" y1="214" x2="464" y2="214" stroke="#4a7bd0" stroke-width="2"/><text x="470" y="200" font-size="12">1차 ΣΔ (idle tone 봉우리)</text><text x="470" y="218" font-size="12">2차 ΣΔ (더 가파름)</text>
</svg>
```

그림 6 — 실제 계산한 PDM 비트열의 스펙트럼(로그 주파수 구간 평균, Hann 창). 1 kHz 신호 아래 대역에서는 잡음이 −100 dB 근처 이하로 깔려 있다가, 주파수가 올라갈수록 잡음이 커져 수백 kHz에서 −50 dB에 이른다. 2차(파랑)는 0~8 kHz에서 1차(주황)보다 낮고, 1차는 idle tone 봉우리가 보인다. decimation 필터의 일은 초록 점선 오른쪽의 이 거대한 잡음 산을 깎아 내는 것이다.

### 4.5 임베디드 연결과 함정

- PDM 비트열 자체에는 "샘플 값"이 없다. 비트 하나를 떼어 보고 소리를 판단할 수 없고, **반드시 저역통과 + decimation**을 거쳐야 한다. 디버깅할 때 PDM 원시 비트를 캡처했다면 호스트에서 numpy로 이 노트의 체인을 돌려 PCM으로 바꿔 듣는다.
- PDM 클럭 지터는 ΣΔ의 샘플 시점을 흔들어 잡음이 된다. 클럭은 깨끗한 소스(분수 분주의 큰 지터 주의)에서 만든다.
- PDM 선은 MHz급 디지털 신호라 **방사 잡음원**이기도 하다. 라디오 안테나 근처로 길게 끌면 RF 감도가 떨어진다 — Don의 RF 경험이 정확히 쓰이는 곳.

---

## 5. Decimation — 1비트 1 MHz에서 16비트 16 kHz로

### 5.1 직관 — 평균 내고 솎아 내기

decimation은 두 동작의 합이다.

1. **저역통과 필터**: 8 kHz 위(특히 그림 6의 거대한 잡음 산)를 깎는다.
2. **솎아 내기(downsampling)**: 샘플을 R개 중 1개만 남긴다.

순서가 중요하다. 필터 없이 솎으면 높은 주파수의 잡음이 낮은 주파수로 **접혀(aliasing)** 들어온다(G5에서 자세히). 1.024 MHz의 잡음이 16 kHz 출력의 대역 안으로 접히면 다시는 뺄 수 없다.

문제는 1.024 MHz에서 날카로운 FIR을 돌리면 너무 비싸다는 것이다. 그래서 거의 모든 PDM 수신기는 **여러 단**으로 나눈다.

```
 PDM 1비트 @ 1.024 MHz
   │  CIC (sinc⁵), R = 32      ← 곱셈 없음, 덧셈만. 싸고 거칠다
   ▼
 27비트 정수 @ 32 kHz
   │  보상(compensation) FIR, 63탭, ↓2   ← CIC가 깎은 고역을 되살리고 8 kHz에서 날카롭게 자른다
   ▼
 PCM @ 16 kHz  (int16 또는 int24/32로 저장)
   │  (선택) DC 제거 HPF, 디지털 gain   ← 6절
   ▼
 링버퍼 → VAD/KWS
```

실제 칩은 CIC 뒤에 **halfband 필터**(차단 주파수가 정확히 fs/4라 계수의 거의 절반이 0인 FIR — 곱셈이 절반)를 여러 개 이어 2배씩 줄이고, 마지막에 보상 FIR을 두는 구성이 흔하다. 이 노트에서는 개념을 보이기 위해 "CIC → 보상+저역통과 FIR(↓2)" 두 단으로 줄였다.

### 5.2 CIC 필터 — 곱셈 없는 이동평균

**CIC(Cascaded Integrator-Comb)** 필터(Hogenauer, 1981)는 "길이 R의 이동합(boxcar)"을 N번 겹친 필터를 곱셈 없이 구현한다.

```
이동합 한 단:   y[n] = x[n] + x[n−1] + … + x[n−R+1]
              = (적분기)  s[n] = s[n−1] + x[n]
                (comb)    y[n] = s[n] − s[n−R]
전달함수:      H(z) = (1 − z^(−R)) / (1 − z⁻¹)
N단:          H(z) = [ (1 − z^(−RM)) / (1 − z⁻¹) ]^N    (M = differential delay, 보통 1)
```

말로 하면: "지난 R개의 합"은 "누적합의 현재 값 − R개 전 값"과 같다. Don이 펌웨어에서 쓰는 "free-running 카운터 두 번 읽고 빼기"와 같은 구조다.

핵심 트릭: comb의 `s[n] − s[n−R]`은 결과를 R개마다 하나만 쓸 거라면 **솎은 뒤에** 계산해도 된다(noble identity). 그래서 구조가

```
x ─▶ [∫]─[∫]─[∫]─[∫]─[∫] ─▶ ↓R ─▶ [comb]─[comb]─[comb]─[comb]─[comb] ─▶ y
     적분기 N개: 1.024 MHz        comb N개: 32 kHz,  comb = u[m] − u[m−M]
```

가 된다. 적분기는 입력 비트마다 덧셈 N번, comb은 출력 샘플마다 뺄셈 N번. 곱셈도 계수 메모리도 없다. 하드웨어로 만들기 아주 싸다.

주파수 응답은

```
|H(f)| = | sin(π·f·R·M / fs) / sin(π·f / fs) |^N        (DC 이득 = (R·M)^N)
```

말로 하면: sinc 함수를 N제곱한 모양이다. 출력 샘플률(fs/R)의 정수배 주파수마다 **영점(null)** 이 있어서, 접혀 들어올 대역(32 kHz, 64 kHz, … 근처)을 깊게 누른다. 대신 통과대역 안에서도 주파수가 올라갈수록 이득이 처진다 — 이것이 **droop**이다(5.5절).

### 5.3 bit growth — 레지스터는 몇 비트여야 하나

DC 이득이 (RM)^N이므로 출력은 입력보다 log2((RM)^N) = N·log2(RM) 비트 커진다.

```
B_out = B_in + N · log2(R · M)
       = 2 + 5 · log2(32)     (입력 ±1 → 2비트 signed, N = 5, R = 32, M = 1)
       = 2 + 25 = 27 비트
```

말로 하면: 5단짜리 CIC가 32배 솎으면 25비트가 늘어나서 27비트 레지스터가 필요하다.

Hogenauer의 두 번째 핵심은 이것이다: **적분기는 오버플로가 나도 된다.** 입력에 DC가 조금만 있어도 적분기는 끝없이 커지지만, 2의 보수 산술은 모듈러(mod 2^W) 산술이고 comb의 뺄셈이 정확히 그 wrap을 되돌린다. 조건은 단 하나, 레지스터 폭 W가 최종 출력 범위(B_out)를 담을 만큼이어야 한다. Don이 아는 "32비트 타이머가 wrap 돼도 `now − prev`는 맞다"와 같은 원리다(단, C에서는 signed 오버플로가 UB이므로 반드시 `uint32_t`로 계산한다).

### 5.4 코드로 확인 — numpy CIC

```python
# cic.py — 정수 CIC decimator (numpy, int64라 오버플로 없는 '정답' 구현)
import numpy as np
def cic_decimate(x, R, N, M=1):
    """정수 CIC: 적분기 N개(고속) -> R배 솎기 -> comb N개(저속). x는 정수 배열."""
    y = np.asarray(x, dtype=np.int64)
    for _ in range(N):
        y = np.cumsum(y)                      # 적분기: y[n] = y[n-1] + x[n]
    y = y[R-1::R]                             # R개 중 1개만 남김
    for _ in range(N):
        y = y - np.concatenate((np.zeros(M, np.int64), y[:-M]))  # comb: y[m] - y[m-M]
    return y
```

```python
# CIC의 이득·bit growth·출력 크기를 확인하는 코드
import numpy as np
from sd import sd2
from cic import cic_decimate
fs, R, N, M = 1_024_000, 32, 5, 1
Bin = 2                                         # 입력 ±1 → 2비트 signed
Bout = Bin + N*np.log2(R*M)
print(f"CIC N={N}, R={R}, M={M}: DC gain (RM)^N = {(R*M)**N} = 2^{N*np.log2(R*M):.0f}, Bout = {Bout:.0f} bits")
n = np.arange(2**16)
x = 0.5*np.sin(2*np.pi*1000*n/fs)
pdm = sd2(x).astype(np.int64)                   # ±1 비트열
y = cic_decimate(pdm, R, N) / (R*M)**N          # 32 kHz, 정규화
print("out rate:", fs//R, "Hz, samples:", len(y))
print("max |y| (after settling):", round(np.abs(y[20:]).max(), 4), " (input amplitude 0.5, droop at 1 kHz is small)")
y_all1 = cic_decimate(np.ones(400, np.int64), R, N)
print("all-ones input -> max raw output:", y_all1.max(), "=> needs", int(np.ceil(np.log2(y_all1.max()+1)))+1, "bits signed")
```

```text
CIC N=5, R=32, M=1: DC gain (RM)^N = 33554432 = 2^25, Bout = 27 bits
out rate: 32000 Hz, samples: 2048
max |y| (after settling): 0.4947  (input amplitude 0.5, droop at 1 kHz is small)
all-ones input -> max raw output: 33554432 => needs 27 bits signed
```

출력에서 볼 것: 이득으로 나누면 진폭 0.5 사인이 0.4947로 돌아온다(1 kHz에서의 CIC droop 0.07 dB와 남은 잡음 때문에 조금 작다). 입력이 전부 1인 최악의 경우 출력은 정확히 2^25이고, 이 값을 부호 있는 정수로 담으려면 27비트가 필요하다 — 공식과 일치한다.

### 5.5 C로 같은 계산 — 레지스터 폭을 바꿔 가며

펌웨어(또는 RTL) 관점의 구현이다. 레지스터 폭 W를 인자로 받아 모든 덧셈 후 하위 W비트만 남겨서 "W비트 하드웨어"를 흉내 낸다. 입력 `pdm.bin`은 numpy가 만든 비트열(비트 하나를 바이트 하나로 — 실제 하드웨어는 32비트 워드에 32개씩 들어 있다).

```c
/* cic.c — W비트 레지스터 CIC decimator (N=5, R=32). 사용: ./cic W out.txt */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#define R 32
#define N 5
/* W비트 레지스터를 흉내: 모든 덧셈/뺄셈 후 하위 W비트만 남긴다 (2의 보수 wrap) */
static uint32_t wmask(int W) { return W == 32 ? 0xFFFFFFFFu : ((1u << W) - 1u); }
static int32_t sext(uint32_t v, int W) {          /* W비트 → int32 부호 확장 */
    uint32_t s = 1u << (W - 1);
    return (int32_t)(((v & wmask(W)) ^ s) - s);
}
int main(int argc, char **argv) {
    int W = argc > 1 ? atoi(argv[1]) : 32;
    uint32_t m = wmask(W), integ[N] = {0}, prev[N] = {0};
    FILE *fi = fopen("pdm.bin", "rb"), *fo = fopen(argc > 2 ? argv[2] : "c_out.txt", "w");
    int c, phase = 0;
    while ((c = fgetc(fi)) != EOF) {
        uint32_t v = c ? 1u : (uint32_t)-1;      /* 비트 1 → +1, 0 → -1 */
        for (int i = 0; i < N; i++) { integ[i] = (integ[i] + v) & m; v = integ[i]; }
        if (++phase == R) {                       /* R 샘플마다 한 번: comb 단 */
            phase = 0;
            uint32_t u = integ[N - 1];
            for (int i = 0; i < N; i++) { uint32_t d = (u - prev[i]) & m; prev[i] = u; u = d; }
            fprintf(fo, "%d\n", sext(u, W));
        }
    }
    fclose(fi); fclose(fo);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 cic.c -o cic -lm     # 경고 0개
```

numpy가 비트열을 만들고, C를 W = 32, 27, 26, 24비트로 돌려 numpy(int64) 결과와 비교한다. 비트열 끝에 "1"만 640개 붙여 최악의 full-scale 입력도 넣었다.

```python
# C CIC(W비트 레지스터)와 numpy CIC를 비트 단위로 비교하는 코드
import numpy as np, subprocess
from sd import sd2
from cic import cic_decimate
n = np.arange(2**16)
pdm = sd2(0.5*np.sin(2*np.pi*1000*n/1_024_000))
pdm = np.concatenate((pdm, np.ones(640)))               # 끝에 '1'만 640개: 최악의 full-scale 입력
((pdm + 1)//2).astype(np.uint8).tofile("pdm.bin")      # 1비트를 바이트 하나씩 (가독성 위해)
ref = cic_decimate(pdm.astype(np.int64), 32, 5)         # numpy int64 = 오버플로 없는 정답
for W in (32, 27, 26, 24):
    subprocess.run(["./cic", str(W), f"c_{W}.txt"], check=True)
    c = np.loadtxt(f"c_{W}.txt", dtype=np.int64)
    bad = np.flatnonzero(c != ref)
    print(f"W={W:2d}: {len(c)} outputs, mismatches = {len(bad):3d}"
          + (f", first at #{bad[0]}: C={c[bad[0]]} numpy={ref[bad[0]]}" if len(bad) else ""))
print("numpy ref max =", ref.max(), "= 2^25 ->", "fits signed 26-bit max", 2**25 - 1, "?", ref.max() <= 2**25 - 1)
```

```text
W=32: 2068 outputs, mismatches =   0
W=27: 2068 outputs, mismatches =   0
W=26: 2068 outputs, mismatches =  16, first at #2052: C=-33554432 numpy=33554432
W=24: 2068 outputs, mismatches = 1298, first at #5: C=-6255118 numpy=10522098
numpy ref max = 33554432 = 2^25 -> fits signed 26-bit max 33554431 ? False
```

출력에서 볼 것: 세 가지다.

1. W = 27(= B_out)이면 적분기가 수없이 wrap 되는데도 **2068개 출력이 전부 bit-exact**다. 오버플로를 막을 필요가 없다는 Hogenauer의 결론이 실제로 성립한다.
2. W = 26은 사인 구간에서는 맞다가 마지막 full-scale 구간(#2052부터 16개)에서만 +2^25가 −2^25로 뒤집힌다. 정상 신호로 테스트하면 통과하고, 큰 소리나 바람 같은 극단 입력에서만 "딱" 소리가 나는 버그 — 펌웨어에서 가장 찾기 어려운 종류다. **bit growth 공식대로 폭을 잡고, 최악 입력 테스트 벡터를 반드시 넣는다.**
3. W = 24는 진폭 0.5 사인조차 담지 못해 거의 모든 출력이 깨진다.

### 5.6 CIC droop과 보상 FIR

5.2절 공식에 숫자를 넣어 보자. 1.024 MHz, R = 32, N = 5에서 4 kHz의 이득은

```
π·f·R/fs = π·4000·32/1,024,000 = π/8 = 0.3927     sin = 0.3827
R·sin(π·f/fs) = 32·sin(π/256) = 32·0.012272 = 0.39267
한 단 = 0.3827 / 0.39267 = 0.97453   →  5단 = 0.97453⁵ = 0.8790  →  20·log10 = −1.12 dB
```

말로 하면: 4 kHz에서 CIC는 이미 1.1 dB를 깎는다. 7 kHz면 3.5 dB다. 모델이 학습한 데이터(보통 평탄한 녹음)와 기기 입력의 스펙트럼 기울기가 달라지는 것이다. 그래서 CIC 뒤에 **역 sinc 모양으로 고역을 살짝 올리는 보상(compensation) FIR**을 둔다. 이 FIR은 32 kHz에서 돌기 때문에 계산이 싸고, 동시에 8 kHz 근처에서 날카롭게 잘라 ↓2의 안티에일리어싱도 맡는다.

```python
# chain.py — CIC(5단, ÷32) + 63탭 보상 FIR(÷2) decimation 체인 (scipy.signal 사용)
import numpy as np
from scipy import signal
from cic import cic_decimate
FS, R, N = 1_024_000, 32, 5
FS2 = FS // R                                          # 32 kHz
b_cic = np.ones(1)
for _ in range(N):
    b_cic = np.convolve(b_cic, np.ones(R))
b_cic /= R**N                                          # CIC = 길이 R 이동합을 N번 = sinc^N
def cic_mag(f):                                        # 닫힌 식 |H(f)|
    f = np.asarray(f, float); s = np.sin(np.pi*f/FS)
    with np.errstate(invalid="ignore", divide="ignore"):
        h = np.abs(np.sin(np.pi*f*R/FS)/(R*s))**N
    return np.where(f == 0, 1.0, h)
fp = np.arange(0, 6501, 250.0)                         # 통과대역 0~6.5 kHz: 역-sinc 목표
fg = np.concatenate((fp, [8000, 16000]))               # 8 kHz부터 0 (6.5~8 kHz는 전이대역)
gain = np.concatenate((1/cic_mag(fp), [0, 0]))
b_comp = signal.firwin2(63, fg, gain, fs=FS2)          # 63탭 보상+저역통과 FIR (32 kHz에서 동작)
def decimate_chain(pdm):
    y = cic_decimate(pdm, R, N) / R**N                 # 1.024 MHz -> 32 kHz
    y = signal.lfilter(b_comp, 1, y)                   # 보상 + 안티에일리어싱
    return y[::2]                                      # 32 kHz -> 16 kHz
```

```python
# CIC droop과 보상 FIR의 효과를 scipy.signal.freqz로 재는 코드
import numpy as np
from scipy import signal
from chain import b_cic, b_comp, FS, FS2, cic_mag
f = np.array([1000, 4000, 6000, 7000, 8000, 24000, 40000])
_, Hc = signal.freqz(b_cic, worN=f, fs=FS)              # CIC (1.024 MHz 기준)
_, Hf = signal.freqz(b_comp, worN=f[:5], fs=FS2)        # 보상 FIR (32 kHz 기준)
for i, (fi, hc) in enumerate(zip(f, Hc)):
    line = f"{fi/1000:5.1f} kHz: CIC {20*np.log10(abs(hc)):7.2f} dB (closed form {20*np.log10(cic_mag(fi)):7.2f})"
    if i < 5:
        line += f" | FIR {20*np.log10(abs(Hf[i])):6.2f} | total {20*np.log10(abs(hc*Hf[i])):6.2f} dB"
    print(line)
fp = np.linspace(0, 6000, 121)
_, Hp = signal.freqz(b_comp, worN=fp, fs=FS2)
tot = 20*np.log10(np.abs(Hp)*cic_mag(fp))
print(f"0-6 kHz ripple: CIC alone {20*np.log10(cic_mag(fp)).min():.2f} dB, CIC+FIR {tot.min():.3f} .. {tot.max():.3f} dB")
_, Hs = signal.freqz(b_comp, worN=np.linspace(10000, 16000, 121), fs=FS2)
print(f"FIR worst gain 10-16 kHz (folds onto 0-6 kHz after /2): {20*np.log10(np.abs(Hs).max()):.1f} dB")
```

```text
  1.0 kHz: CIC   -0.07 dB (closed form   -0.07) | FIR   0.08 | total   0.01 dB
  4.0 kHz: CIC   -1.12 dB (closed form   -1.12) | FIR   1.13 | total   0.01 dB
  6.0 kHz: CIC   -2.54 dB (closed form   -2.54) | FIR   2.50 | total  -0.04 dB
  7.0 kHz: CIC   -3.47 dB (closed form   -3.47) | FIR  -0.62 | total  -4.09 dB
  8.0 kHz: CIC   -4.56 dB (closed form   -4.56) | FIR -17.70 | total -22.26 dB
 24.0 kHz: CIC  -52.23 dB (closed form  -52.23)
 40.0 kHz: CIC  -74.35 dB (closed form  -74.35)
0-6 kHz ripple: CIC alone -2.54 dB, CIC+FIR -0.043 .. 0.019 dB
FIR worst gain 10-16 kHz (folds onto 0-6 kHz after /2): -65.9 dB
```

출력에서 볼 것: (1) freqz 결과와 닫힌 식이 소수점 둘째 자리까지 같다 — CIC가 정말 sinc⁵다. 4 kHz의 −1.12 dB는 위 손계산 그대로다. (2) 보상 FIR이 4 kHz에서 +1.13 dB, 6 kHz에서 +2.50 dB를 올려 0~6 kHz 합계가 ±0.05 dB 안으로 평탄해진다. (3) 6.5~8 kHz는 전이대역이라 7 kHz는 −4 dB, 8 kHz는 −22 dB다. 63탭으로는 이 정도가 한계이고, 탭을 늘리거나 halfband 단을 쓰면 전이대역이 좁아진다. (4) 24 kHz(= 32 kHz − 8 kHz, CIC 출력에서 음성 대역으로 접혀 들어올 수 있는 주파수)에서 CIC가 이미 52 dB를 눌러 준다.

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="250" x2="620" y2="250" stroke="currentColor"/><line x1="60" y1="30" x2="60" y2="250" stroke="currentColor"/><line x1="60" y1="43" x2="620" y2="43" stroke="currentColor" stroke-opacity="0.2"/><line x1="60" y1="95" x2="620" y2="95" stroke="currentColor" stroke-opacity="0.15"/><line x1="60" y1="146" x2="620" y2="146" stroke="currentColor" stroke-opacity="0.15"/><line x1="60" y1="198" x2="620" y2="198" stroke="currentColor" stroke-opacity="0.15"/><text x="54" y="47" font-size="12" text-anchor="end">0</text><text x="54" y="99" font-size="12" text-anchor="end">−20</text><text x="54" y="150" font-size="12" text-anchor="end">−40</text><text x="54" y="202" font-size="12" text-anchor="end">−60</text><text x="54" y="254" font-size="12" text-anchor="end">−80</text><text x="60" y="268" font-size="12" text-anchor="middle">0</text><text x="200" y="268" font-size="12" text-anchor="middle">4k</text><text x="340" y="268" font-size="12" text-anchor="middle">8k</text><text x="480" y="268" font-size="12" text-anchor="middle">12k</text><text x="620" y="268" font-size="12" text-anchor="middle">16k</text>
<text x="340" y="286" font-size="12" text-anchor="middle">주파수 (Hz) — CIC 출력(32 kHz)의 Nyquist까지 · 세로: dB</text><line x1="340" y1="30" x2="340" y2="250" stroke="currentColor" stroke-dasharray="4 4" stroke-opacity="0.5"/>
<polyline points="60,43 69,43 78,43 86,43 95,43 104,43 112,43 121,43 130,44 139,44 148,44 156,44 165,45 174,45 182,45 191,45 200,46 209,46 218,47 226,47 235,47 244,48 252,48 261,49 270,50 279,50 288,51 296,51 305,52 314,53 322,53 331,54 340,55 349,55 358,56 366,57 375,58 384,59 392,60 401,61 410,62 419,63 428,64 436,65 445,66 454,67 462,68 471,69 480,70 489,71 498,73 506,74 515,75 524,77 532,78 541,79 550,81 559,82 568,84 576,85 585,87 594,89 602,90 611,92 620,94" fill="none" stroke="#e08a3c" stroke-width="2"/>
<polyline points="60,43 69,43 78,43 86,43 95,43 104,43 112,43 121,42 130,42 139,42 148,42 156,42 165,41 174,41 182,41 191,40 200,40 209,40 218,39 226,39 235,38 244,38 252,37 261,37 270,36 279,37 288,38 296,40 305,45 314,51 322,60 331,72 340,89 349,114 358,153 366,228 375,221 384,218 392,241 401,215 410,229 419,217 428,226 436,220 445,225 454,224 462,224 471,227 480,224 489,231 498,224 506,235 515,224 524,240 532,224 541,245 550,224 559,250 568,224 576,250 585,224 594,250 602,224 611,250 620,224" fill="none" stroke="#888" stroke-width="1.2" stroke-dasharray="4 3"/>
<polyline points="60,43 69,43 78,43 86,43 95,43 104,43 112,43 121,43 130,43 139,43 148,43 156,43 165,43 174,43 182,43 191,43 200,43 209,43 218,43 226,43 235,43 244,43 252,43 261,43 270,43 279,44 288,45 296,49 305,54 314,60 322,70 331,83 340,101 349,126 358,166 366,242 375,236 384,234 392,250 401,233 410,247 419,237 428,246 436,242 445,247 454,248 462,249 471,250 480,250 489,250 498,250 506,250 515,250 524,250 532,250 541,250 550,250 559,250 568,250 576,250 585,250 594,250 602,250 611,250 620,250" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<line x1="430" y1="110" x2="460" y2="110" stroke="#e08a3c" stroke-width="2"/><text x="466" y="114" font-size="12">CIC (sinc⁵) — 서서히 처짐</text><line x1="430" y1="130" x2="460" y2="130" stroke="#888" stroke-dasharray="4 3"/><text x="466" y="134" font-size="12">보상 FIR (63탭)</text><line x1="430" y1="150" x2="460" y2="150" stroke="#4a7bd0" stroke-width="2"/><text x="466" y="154" font-size="12">합계 — 평탄 후 급강하</text>
</svg>
```

그림 7 — scipy.signal.freqz로 계산한 CIC, 보상 FIR, 합계의 응답(0~16 kHz, −80 dB에서 잘라 그림). CIC(주황)는 저주파부터 서서히 처지고, 보상 FIR(회색 점선)은 6.5 kHz까지 그만큼 올렸다가 8 kHz 근처에서 급히 떨어진다. 합계(파랑)는 6 kHz까지 평탄하다.

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="220" x2="620" y2="220" stroke="currentColor"/><line x1="60" y1="20" x2="60" y2="220" stroke="currentColor"/><line x1="60" y1="100" x2="620" y2="100" stroke="currentColor" stroke-opacity="0.3"/><line x1="60" y1="60" x2="620" y2="60" stroke="currentColor" stroke-opacity="0.12"/><line x1="60" y1="140" x2="620" y2="140" stroke="currentColor" stroke-opacity="0.12"/><line x1="60" y1="180" x2="620" y2="180" stroke="currentColor" stroke-opacity="0.12"/><text x="54" y="24" font-size="12" text-anchor="end">+4</text><text x="54" y="64" font-size="12" text-anchor="end">+2</text><text x="54" y="104" font-size="12" text-anchor="end">0</text><text x="54" y="144" font-size="12" text-anchor="end">−2</text><text x="54" y="184" font-size="12" text-anchor="end">−4</text><text x="54" y="224" font-size="12" text-anchor="end">−6</text><text x="60" y="238" font-size="12" text-anchor="middle">0</text><text x="209" y="238" font-size="12" text-anchor="middle">2k</text><text x="359" y="238" font-size="12" text-anchor="middle">4k</text><text x="508" y="238" font-size="12" text-anchor="middle">6k</text><text x="620" y="238" font-size="12" text-anchor="middle">7.5k</text>
<text x="340" y="256" font-size="12" text-anchor="middle">통과대역 확대 (Hz) · 세로: dB</text><line x1="545" y1="20" x2="545" y2="220" stroke="currentColor" stroke-dasharray="4 4" stroke-opacity="0.5"/><text x="549" y="34" font-size="12">6.5k</text>
<polyline points="60,100 79,100 97,100 116,101 135,101 153,102 172,103 191,104 209,106 228,107 247,109 265,111 284,113 303,115 321,117 340,120 359,122 377,125 396,128 415,132 433,135 452,139 471,143 489,147 508,151 527,155 545,160 564,164 583,169 601,175 620,180" fill="none" stroke="#e08a3c" stroke-width="2"/>
<polyline points="60,100 79,100 97,100 116,99 135,98 153,98 172,97 191,96 209,94 228,93 247,91 265,89 284,87 303,85 321,83 340,80 359,77 377,74 396,71 415,68 433,65 452,61 471,57 489,53 508,50 527,51 545,60 564,80 583,112 601,161 620,220" fill="none" stroke="#888" stroke-width="1.5" stroke-dasharray="4 3"/>
<polyline points="60,100 79,100 97,100 116,100 135,100 153,100 172,100 191,100 209,100 228,100 247,100 265,100 284,100 303,100 321,100 340,100 359,100 377,100 396,100 415,100 433,100 452,100 471,100 489,100 508,101 527,106 545,120 564,144 583,182 601,220 620,220" fill="none" stroke="#4a7bd0" stroke-width="2.5"/>
<text x="250" y="150" font-size="12">CIC droop (−1.1 dB @4k, −2.5 dB @6k)</text><text x="200" y="76" font-size="12">보상 FIR (역 sinc)</text><text x="120" y="92" font-size="12">합계 ≈ 0 dB</text>
</svg>
```

그림 8 — 그림 7의 통과대역(0~7.5 kHz)을 ±dB 단위로 확대한 것. 주황(CIC)이 내려가는 만큼 회색(보상 FIR)이 올라가서 파랑(합계)이 6 kHz까지 0 dB에 붙어 있다.

### 5.7 체인 전체 — SNR과 지연

PDM 비트열 → CIC → 보상 FIR → 16 kHz PCM을 끝까지 돌려 실제 SNR과 **group delay**(필터가 신호를 늦추는 시간)를 잰다.

```python
# PDM → 16 kHz PCM 체인의 SNR과 group delay를 재는 코드
import numpy as np
from sd import sd1, sd2
from chain import decimate_chain, b_comp, R, N, FS
n = np.arange(2**18)                                   # 0.256 s @ 1.024 MHz
x = 0.5*np.sin(2*np.pi*1000*n/FS)
def snr16k(y):                                         # 16 kHz 출력의 마지막 2048 샘플 (1 kHz = bin 128)
    y = y[-2048:]; P = np.abs(np.fft.rfft(y*np.hanning(2048)))**2
    sig = P[125:132].sum(); return 10*np.log10(sig/(P[1:].sum()-sig)), y
for name, f in (("1st-order", sd1), ("2nd-order", sd2)):
    snr, y = snr16k(decimate_chain(f(x).astype(np.int64)))
    print(f"{name} PDM -> CIC5/32 -> FIR63/2 -> 16 kHz: SNR = {snr:.1f} dB, peak = {np.abs(y).max():.3f}")
q = np.round(decimate_chain(sd2(x).astype(np.int64))*32767)/32767   # int16로 저장했을 때
print(f"after rounding to int16: SNR = {snr16k(q)[0]:.1f} dB")
gd_cic = N*(R-1)/2 / FS                                # CIC: 선형위상, (N(RM-1)/2) 입력 샘플
gd_fir = (len(b_comp)-1)/2 / 32000                     # 대칭 FIR: (taps-1)/2 샘플 @ 32 kHz
print(f"group delay: CIC {gd_cic*1e6:.1f} us + FIR {gd_fir*1e6:.0f} us = {(gd_cic+gd_fir)*1e3:.3f} ms")
```

```text
1st-order PDM -> CIC5/32 -> FIR63/2 -> 16 kHz: SNR = 48.6 dB, peak = 0.499
2nd-order PDM -> CIC5/32 -> FIR63/2 -> 16 kHz: SNR = 68.7 dB, peak = 0.498
after rounding to int16: SNR = 68.7 dB
group delay: CIC 75.7 us + FIR 969 us = 1.044 ms
```

출력에서 볼 것: (1) 2차 PDM에서 16 kHz PCM까지 68.7 dB가 나온다. 4.4절의 "이상적 8 kHz 벽돌 필터" 측정(65.4 dB)보다 오히려 높은데, 보상 FIR이 6.5~8 kHz(잡음이 가장 많은 대역 끝)를 일부 깎아 버리기 때문이다. 즉 체인이 ΣΔ의 잡음을 거의 다 걸러 냈다. (2) int16으로 반올림해도 SNR이 그대로다 — −6 dBFS 신호에서는 int16 양자화 잡음(약 −98 dBFS)이 훨씬 작다. 하지만 2.3절처럼 −60 dBFS 신호라면 int16의 여유가 크게 줄어든다. (3) 지연은 약 1 ms이고 대부분이 FIR이다. CIC는 대칭 FIR과 같아서(선형위상) group delay가 N·(RM−1)/2 입력 샘플로 고정이다.

1 ms는 KWS·ASR에는 무시해도 되는 지연이지만, **ANC(능동 소음 제거)** 처럼 수십 µs 단위의 지연이 중요한 경로에는 너무 길다. 그래서 ANC용 마이크 경로는 별도의 저지연 decimation(짧은 필터, 높은 출력 샘플률)이나 전용 하드웨어를 쓰는 것이 일반적이다(typical). AEC에서는 이 지연이 reference 정렬(8.4절)에 들어간다.

### 5.8 함정

- CIC 레지스터 폭을 B_out보다 작게 잡는다 → 최악 입력에서만 깨진다(5.5절 W = 26).
- CIC 이득 (RM)^N이 2의 거듭제곱이 아닐 때(예: R = 48) 정규화를 시프트로만 하면 이득이 틀어진다 → 시프트 + 곱셈 보정, 또는 보상 FIR 계수에 흡수.
- 보상 FIR을 빼먹고 "대충 평탄하겠지" → 고역이 3~4 dB 처진 데이터. 학습 데이터를 같은 기기로 수집했다면 문제가 덜하지만, 공개 데이터셋으로 학습한 모델과는 스펙트럼 기울기가 다르다.
- HW PDM 블록의 필터 설정(차수, 분주비, 시프트)이 데이터시트 예제와 달라 이득이 6 dB 단위로 틀어진다 → 1 kHz 기준 톤(94 dB SPL 캘리브레이터 또는 알려진 스피커 레벨)으로 end-to-end 이득을 측정해 확인한다.

---

## 6. PCM 이후 — gain, DC 제거, clipping, 샘플률

### 6.1 디지털 gain과 그 함정

2.3절에서 1 m 대화가 −60 dBFS라고 했다. ML 특징이 log-mel이면 gain은 로그 영역에서 **상수 더하기**일 뿐이라서(log(g·x) = log g + log x), 학습 때 입력 정규화(평균·분산)나 랜덤 gain augmentation(A4)을 했다면 모델은 gain에 꽤 강하다. 그래도 펌웨어에서 gain을 다룰 때 지킬 것:

- gain은 **decimation 출력의 높은 정밀도(24~32비트)에서** 곱하고 마지막에 int16으로 포화(saturate)시킨다. int16으로 먼저 자르고 gain을 곱하면 하위 비트가 없어진 신호를 키우는 셈이다.
- gain 변경은 프레임 경계에서, 혹은 짧은 ramp로 한다. 갑자기 바꾸면 "툭" 소리가 나고 VAD가 오검출한다.
- 고정 gain과 AGC(8.6절)는 다르다. KWS 입력에는 고정 gain + 모델의 강건성이 흔하고, 통화/녹음 출력에는 AGC를 쓴다.

### 6.2 DC 제거 — 1차 high-pass

디지털 MEMS 마이크 출력에는 작은 **DC offset**이 있을 수 있고, 일부 PDM 블록은 HPF를 내장한다. DC가 남으면 (1) 에너지 VAD의 noise floor가 올라가고(B5 2절), (2) FFT의 0번 bin과 그 옆이 커지며, (3) 디지털 gain 뒤 clipping이 한쪽으로만 일어난다.

가장 흔한 DC blocker는 1차 IIR이다.

```
y[n] = x[n] − x[n−1] + a·y[n−1]          (a = 0.995 정도, 1에 가까울수록 차단 주파수가 낮다)
H(z) = (1 − z⁻¹) / (1 − a·z⁻¹)           영점이 DC(z = 1)에 있다
f_c ≈ (1 − a)·fs / (2π) = 0.005·16000 / 6.283 ≈ 12.7 Hz
```

말로 하면: 차분기(1 − z⁻¹)로 DC를 완전히 죽이고, 1 바로 아래의 극(a)으로 저주파 응답을 거의 다시 살려서 "DC만 빼고 나머지는 통과"를 만든다.

C에서 Q15로 짤 때의 고전적 함정이 있다. 피드백 경로에서 `>> 15`로 잘라 버리면(truncation) 그 오차가 항상 같은 방향이라 **DC를 없애려는 필터가 새 DC를 만든다**. 해결은 E4에서 본 "버린 비트를 다음에 더해 주기"(error feedback, noise shaping의 가장 단순한 형태)다.

```c
/* dcb.c — Q15 DC blocker: truncation vs error feedback */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#define FS 16000
#define NS 32000                                   /* 2 s */
/* DC blocker: y[n] = x[n] - x[n-1] + a·y[n-1],  a = 0.995 (Q15 = 32604) */
#define A_Q15 32604
int main(void) {
    int16_t xp = 0; int32_t yp_t = 0, yp_e = 0, err = 0;
    double mean_in = 0, mean_t = 0, mean_e = 0;
    for (int n = 0; n < NS; n++) {
        double xf = 0.1 + 0.3 * sin(2 * M_PI * 1000.0 * n / FS);   /* DC 0.1 + 1 kHz */
        int16_t x = (int16_t)lrint(xf * 32767);
        /* (1) 단순: 곱한 뒤 >>15로 잘라 버림 (truncation) */
        int32_t acc = ((int32_t)x - xp) * 32768 + A_Q15 * yp_t;      /* Q30 */
        int32_t yt = acc >> 15;
        /* (2) error feedback: 버린 하위 비트를 다음 샘플에 더해 준다 */
        int32_t acc2 = ((int32_t)x - xp) * 32768 + A_Q15 * yp_e + err;
        int32_t ye = acc2 >> 15; err = acc2 - ye * 32768;
        xp = x; yp_t = yt; yp_e = ye;
        if (n >= NS / 2) { mean_in += x; mean_t += yt; mean_e += ye; }  /* 뒤 1초만 평균 */
    }
    printf("mean of input        : %8.2f LSB\n", mean_in / (NS / 2));
    printf("mean out (truncate)  : %8.2f LSB\n", mean_t / (NS / 2));
    printf("mean out (err. fdbk) : %8.2f LSB\n", mean_e / (NS / 2));
    printf("-3 dB corner approx (1-a)·fs/(2π) = %.1f Hz\n", (1 - A_Q15 / 32768.0) * FS / (2 * M_PI));
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 dcb.c -o dcb -lm && ./dcb
```

```text
mean of input        :  3277.00 LSB
mean out (truncate)  :   -88.50 LSB
mean out (err. fdbk) :     0.00 LSB
-3 dB corner approx (1-a)·fs/(2π) = 12.7 Hz
```

출력에서 볼 것: 입력의 DC 3277 LSB(= 0.1 full scale)는 두 구현 모두 없앴지만, truncation 버전은 **−88.5 LSB의 새 DC**를 만든다. 대략 "평균 잘림 오차(≈ −0.5 LSB) ÷ (1 − a)(= 0.005)" ≈ −100 LSB 크기로, 극이 1에 가까울수록 커진다. error feedback 버전은 정확히 0이다. (`>>`를 음수에 쓰는 것은 C 표준상 구현 정의 동작이지만 ARM·x86 컴파일러는 산술 시프트를 한다.) scipy.signal.freqz로 같은 필터를 계산하면 −3 dB 점은 12.74 Hz, 50 Hz에서 −0.25 dB, 100 Hz에서 −0.05 dB라서 음성 대역은 건드리지 않는다.

### 6.3 clipping과 AOP

- **acoustic clipping**: 마이크 AOP(예: 120 dB SPL)를 넘는 소리 — 입 바로 앞의 고함, 바람이 port를 직접 때림, 기기를 두드림, 그리고 **기기 자신의 스피커**. 웨어러블은 스피커와 마이크가 몇 cm 거리라 TTS 재생 중 마이크 입력이 AOP 근처까지 갈 수 있다. 마이크 안에서 이미 찌그러진 신호는 AEC의 선형 모델로 지울 수 없다(8.5절).
- **digital clipping**: decimation 후 gain을 너무 크게 줘서 int16 범위를 넘는 것. 포화 연산으로 wrap은 막아야 하고(wrap은 클릭 소리), clip 비율을 telemetry로 세어 두면 필드 문제 분석에 유용하다.
- 검출: 연속 샘플이 ±full scale에 붙어 있는 개수, 또는 프레임당 clip 샘플 비율. ML 데이터 수집 파이프라인(H 모듈)에서는 clip 비율을 메타데이터로 남겨 학습에서 거르거나 가중치를 준다.

### 6.4 샘플률 선택

| 용도 | 흔한 샘플률 | 이유 |
|---|---|---|
| KWS, VAD, ASR 입력 | 16 kHz | 음성 정보 대부분이 8 kHz 아래, 공개 데이터셋·모델이 16 kHz 기준 (B5 1.1) |
| 통화 (wideband / super-wideband) | 16 kHz / 32 kHz | 코덱 표준에 따라 |
| 음악 재생, 녹음, AEC reference | 48 kHz | 오디오 경로 표준, 스피커 출력과 같은 클럭 도메인 |
| ANC | 수백 kHz급 내부 처리 (typical) | 지연 최소화 |

한 기기에 여러 샘플률이 공존하므로 **같은 PDM 클럭에서 서로 다른 분주비로 16 kHz와 48 kHz를 동시에 뽑는** 구성(예: 3.072 MHz ÷ 64 = 48 kHz, ÷ 192 = 16 kHz)이나 샘플률 변환기(SRC)가 필요하다. 마이크 경로와 스피커 경로의 클럭이 다른 소스에서 나오면 AEC가 고생한다(G7의 drift).

---

## 7. 마이크 여러 개 — TDOA, beamforming, 그리고 작은 기기의 한계

### 7.1 직관 — 소리는 한 마이크에 먼저 도착한다

소리는 초속 약 343 m(20 °C 공기)로 간다. 1 cm를 가는 데 29 µs. 마이크 두 개를 d만큼 떨어뜨려 놓으면, 비스듬히 오는 소리는 한쪽 마이크에 조금 먼저 닿는다. 이 시간 차이가 **TDOA(time difference of arrival)** 다.

```
τ = d · sin θ / c            θ: 정면(broadside, 두 마이크를 잇는 선에 수직)에서 잰 각도
최대 τ = d / c               (소리가 축 방향 = endfire에서 올 때)
d = 2 cm  →  최대 τ = 0.02 / 343 = 58.3 µs  =  16 kHz에서 0.93 샘플
```

말로 하면: 2 cm 간격 마이크 두 개에서 도착 시간 차이는 아무리 커도 **한 샘플이 안 된다.** 웨어러블 어레이의 모든 어려움이 이 한 줄에서 나온다. Don에게는 RF 안테나 어레이의 위상차(AoA)와 같은 문제지만, 음성은 대역이 100 Hz~8 kHz로 **파장 대비 대역이 매우 넓다**(6옥타브)는 점이 다르다. 파장은 100 Hz에서 3.4 m, 8 kHz에서 4.3 cm다.

```svg
<svg viewBox="0 0 640 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="g4c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><line x1="168" y1="230" x2="445" y2="70" stroke="#4a7bd0" stroke-width="1.5"/><text x="450" y="66" font-size="12">파면 (mic1에 막 닿은 순간)</text><line x1="80" y1="30" x2="135" y2="125" stroke="currentColor" marker-end="url(#g4c)"/><line x1="230" y1="10" x2="285" y2="105" stroke="currentColor" marker-end="url(#g4c)"/><text x="40" y="22" font-size="12">소리가 오는 방향</text><line x1="370" y1="113" x2="420" y2="200" stroke="#d0564a" stroke-width="3"/><text x="404" y="150" font-size="12">d · sin θ (더 가야 하는 거리)</text><line x1="220" y1="200" x2="420" y2="200" stroke="currentColor"/><text x="320" y="222" font-size="12" text-anchor="middle">d (예: 1.5~2 cm)</text><line x1="220" y1="200" x2="220" y2="90" stroke="currentColor" stroke-dasharray="4 4"/><line x1="220" y1="200" x2="165" y2="105" stroke="currentColor" stroke-dasharray="4 4"/><text x="196" y="140" font-size="12">θ</text><text x="226" y="98" font-size="12">broadside (정면)</text>
<circle cx="220" cy="200" r="7" fill="#3f9a6b" stroke="currentColor"/><circle cx="420" cy="200" r="7" fill="#e08a3c" stroke="currentColor"/><text x="220" y="244" font-size="12" text-anchor="middle">mic1</text><text x="420" y="244" font-size="12" text-anchor="middle">mic2</text><text x="480" y="210" font-size="12">→ 축 방향 = endfire</text>
</svg>
```

그림 9 — 평면파가 θ 방향에서 올 때 mic2는 mic1보다 d·sin θ만큼 더 가야 한다. 이 거리를 음속으로 나눈 것이 TDOA다. (그림은 θ = 30°로 그렸다.)

### 7.2 GCC-PHAT — 한 샘플보다 짧은 지연을 찾는 법

두 신호의 지연은 **상호상관(cross-correlation)** 의 봉우리 위치로 찾는다. 그러나 말소리처럼 저주파에 에너지가 몰린 신호는 상관 봉우리가 뭉툭하고, 반사음이 섞이면 봉우리가 여러 개 생긴다. **GCC-PHAT**(Generalized Cross-Correlation with PHAse Transform, Knapp & Carter 1976)은 주파수 영역에서 상호 스펙트럼의 크기를 1로 정규화해 **위상만** 남긴다.

```
G(f) = X₂(f) · X₁*(f)                     상호 스펙트럼 (위상 = 2π·f·τ)
G_PHAT(f) = G(f) / |G(f)|                 모든 주파수에 같은 가중치
r(τ) = IFFT{ G_PHAT }(τ)                  τ에서 날카로운 봉우리
```

말로 하면: 각 주파수가 "나는 지연이 τ라고 생각해"라는 투표를 같은 무게로 하게 만든다. 에너지 큰 저주파가 투표를 독점하지 못하니 봉우리가 뾰족해진다. 한 샘플보다 작은 지연은 IFFT 길이를 늘려(주파수 영역 zero-padding = 시간축 보간) 찾는다.

```python
# gcc.py — 분수 지연 생성 + GCC-PHAT (보간 포함)
import numpy as np
def frac_delay(x, d):                          # d 샘플만큼 지연 (분수 가능, FFT 위상 회전)
    X = np.fft.rfft(x); k = np.fft.rfftfreq(len(x))
    return np.fft.irfft(X*np.exp(-2j*np.pi*k*d), len(x))
def gcc_phat(x1, x2, interp=16, max_lag=None):
    """x2가 x1보다 늦으면 양수. 반환: (지연 샘플, lag축, 상관값)"""
    n = len(x1) + len(x2)
    X = np.fft.rfft(x2, n)*np.conj(np.fft.rfft(x1, n))
    X /= np.abs(X) + 1e-12                     # PHAT: 크기를 버리고 위상만 남긴다
    cc = np.fft.irfft(X, n*interp)             # 주파수 영역 zero-pad = 시간축 보간
    m = int(interp*max_lag) if max_lag else n*interp//2
    cc = np.concatenate((cc[-m:], cc[:m+1]))   # lag -m..+m
    lags = np.arange(-m, m+1)/interp
    return lags[np.argmax(cc)], lags, cc
```

2 cm 간격, 정면에서 40° 방향의 소스를 시뮬레이션한다(진짜 지연 0.6 샘플).

```python
# 2-mic TDOA를 GCC-PHAT로 추정하고 각도로 바꾸는 코드
import numpy as np
from gcc import frac_delay, gcc_phat
rng = np.random.default_rng(0)
fs, d, c = 16000, 0.02, 343.0                  # 16 kHz, 마이크 간격 2 cm, 음속
theta = 40.0                                   # 정면(broadside)에서 40도
tau = d*np.sin(np.radians(theta))/c            # TDOA (초)
print(f"true TDOA = {tau*1e6:.1f} us = {tau*fs:.3f} samples (max possible {d/c*1e6:.1f} us)")
s = rng.standard_normal(8000)                  # 0.5 s 광대역 소스 (말소리 대용)
x1 = s + 0.1*rng.standard_normal(8000)         # mic1 (SNR 20 dB)
x2 = frac_delay(s, tau*fs) + 0.1*rng.standard_normal(8000)
for interp in (1, 16):
    lag, _, _ = gcc_phat(x1, x2, interp, max_lag=2)
    th = np.degrees(np.arcsin(np.clip(lag/fs*c/d, -1, 1)))
    print(f"interp x{interp:2d}: TDOA = {lag/fs*1e6:6.1f} us ({lag:+.4f} samples) -> angle {th:5.1f} deg")
plain = np.correlate(x2, x1, "full"); k = np.argmax(plain) - (len(x1)-1)
print("plain xcorr integer lag:", k, "samples")
```

```text
true TDOA = 37.5 us = 0.600 samples (max possible 58.3 us)
interp x 1: TDOA =   62.5 us (+1.0000 samples) -> angle  90.0 deg
interp x16: TDOA =   39.1 us (+0.6250 samples) -> angle  42.1 deg
plain xcorr integer lag: 1 samples
```

출력에서 볼 것: 정수 샘플 해상도로는 0.6 샘플이 1 샘플로 반올림되어 각도가 90°(완전 측면)로 나온다 — 쓸모가 없다. 16배 보간하면 0.625 샘플(1/16 단위)로 42°가 나와 진짜 40°에 가깝다. 작은 어레이에서는 **보간(또는 더 높은 샘플률에서 TDOA 추정)이 필수**다.

PHAT가 봉우리를 얼마나 날카롭게 하는지는 "말소리 같은"(저역에 몰린) 소스로 보면 분명하다.

```python
# 저역에 몰린 소스에서 일반 GCC와 GCC-PHAT의 봉우리 폭을 비교하는 코드
import numpy as np
from scipy import signal
from gcc import frac_delay
rng = np.random.default_rng(1)
s = signal.lfilter([1], [1, -0.97], rng.standard_normal(8000))   # 저역에 몰린 '말소리 같은' 소스
x1 = s + 0.05*rng.standard_normal(8000)
x2 = frac_delay(s, 0.6) + 0.05*rng.standard_normal(8000)
n = 16000; X = np.fft.rfft(x2, n)*np.conj(np.fft.rfft(x1, n))
for name, W in (("plain GCC", X), ("GCC-PHAT ", X/(np.abs(X)+1e-12))):
    cc = np.fft.irfft(W, n*16); m = 16*20
    cc = np.concatenate((cc[-m:], cc[:m+1])); cc /= cc.max()
    lags = np.arange(-m, m+1)/16
    width = np.ptp(lags[cc >= 0.5])                       # 봉우리가 최대의 절반 이상인 구간 폭
    print(f"{name}: peak at {lags[np.argmax(cc)]:+.3f} samples, half-max width = {width:5.2f} samples, "
          f"value at lag ±5: {cc[m+80]:.2f}/{cc[m-80]:.2f}")
```

```text
plain GCC: peak at +0.625 samples, half-max width = 40.00 samples, value at lag ±5: 0.87/0.84
GCC-PHAT : peak at +0.625 samples, half-max width =  1.19 samples, value at lag ±5: 0.07/-0.05
```

출력에서 볼 것: 깨끗한 조건이라 둘 다 봉우리 위치는 맞지만, 일반 GCC는 ±20 샘플 창 **전체**가 최대의 절반 이상(폭 40 = 창 전체)이고 ±5 샘플에서도 0.85다. 반사음이나 잡음이 조금만 섞여도 봉우리가 옮겨 갈 수 있는 모양이다. PHAT는 폭 1.2 샘플로 뾰족하다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="197" x2="620" y2="197" stroke="currentColor" stroke-opacity="0.4"/><line x1="60" y1="20" x2="60" y2="235" stroke="currentColor"/><line x1="60" y1="235" x2="620" y2="235" stroke="currentColor"/><text x="54" y="34" font-size="12" text-anchor="end">1.0</text><text x="54" y="117" font-size="12" text-anchor="end">0.5</text><text x="54" y="201" font-size="12" text-anchor="end">0</text><line x1="60" y1="113" x2="620" y2="113" stroke="currentColor" stroke-opacity="0.12"/><line x1="60" y1="30" x2="620" y2="30" stroke="currentColor" stroke-opacity="0.12"/><text x="60" y="252" font-size="12" text-anchor="middle">−8</text><text x="200" y="252" font-size="12" text-anchor="middle">−4</text><text x="340" y="252" font-size="12" text-anchor="middle">0</text><text x="480" y="252" font-size="12" text-anchor="middle">+4</text><text x="620" y="252" font-size="12" text-anchor="middle">+8</text><text x="340" y="268" font-size="12" text-anchor="middle">lag (샘플 @ 16 kHz, 16배 보간)</text><line x1="361" y1="20" x2="361" y2="235" stroke="#d0564a" stroke-dasharray="4 3"/><text x="366" y="230" font-size="12">진짜 지연 0.6</text>
<polyline points="60,69 69,68 78,67 86,66 95,65 104,64 112,63 121,62 130,60 139,59 148,58 156,58 165,57 174,56 182,54 191,53 200,52 209,51 218,50 226,49 235,48 244,47 252,45 261,44 270,43 279,41 288,40 296,40 305,39 314,37 322,36 331,34 340,32 349,31 358,30 366,30 375,31 384,33 392,34 401,36 410,38 419,39 428,40 436,41 445,42 454,43 462,44 471,46 480,47 489,48 498,49 506,50 515,51 524,52 532,53 541,55 550,56 559,57 568,58 576,59 585,60 594,61 602,62 611,63 620,64" fill="none" stroke="#e08a3c" stroke-width="2"/>
<polyline points="60,191 69,191 78,195 86,200 95,204 104,203 112,199 121,193 130,189 139,189 148,194 156,201 165,206 174,206 182,200 191,192 200,186 209,186 218,193 226,203 235,210 244,210 252,201 261,188 270,178 279,177 288,190 296,210 305,228 314,230 322,210 331,167 340,111 349,61 358,32 366,36 375,70 384,123 392,177 401,216 410,230 419,224 428,205 436,185 445,176 454,179 462,191 471,204 480,211 489,210 498,201 506,191 515,185 524,186 532,193 541,201 550,206 559,205 568,200 576,193 585,190 594,190 602,195 611,200 620,203" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<line x1="440" y1="90" x2="470" y2="90" stroke="#e08a3c" stroke-width="2"/><text x="476" y="94" font-size="12">일반 GCC (뭉툭)</text><line x1="440" y1="110" x2="470" y2="110" stroke="#4a7bd0" stroke-width="2"/><text x="476" y="114" font-size="12">GCC-PHAT (뾰족)</text>
</svg>
```

그림 10 — 같은 두 마이크 신호(진짜 지연 0.6 샘플)에서 계산한 상관 함수(최대 1로 정규화). 일반 GCC(주황)는 ±8 샘플 전체에 걸쳐 0.6 이상인 언덕이고, GCC-PHAT(파랑)는 0.6 근처에 폭 1샘플 남짓의 봉우리가 선다.

### 7.3 Delay-and-sum beamforming

가장 단순한 beamformer: 원하는 방향에서 온 소리가 두 마이크에서 **같은 시각**이 되도록 한쪽을 지연시킨 뒤 더한다(평균). 그 방향의 소리는 보강되고, 다른 방향의 소리는 위상이 어긋나 덜 보강된다.

```
y(t) = ½·[ x₁(t) + x₂(t − τ_steer) ]
조준 방향 이외에서 온 주파수 f 성분의 이득:  |H| = |cos(π·f·Δτ)|,   Δτ = 남은 시간차
```

말로 하면: 남은 시간차가 반주기(Δτ = 1/(2f))가 되면 완전히 상쇄되고, 시간차가 주기에 비해 아주 작으면 거의 그대로 더해진다(= 방향 구별 못 함).

이게 작은 어레이의 핵심 문제다. d = 1.5 cm면 최대 시간차가 44 µs인데, 1 kHz의 반주기는 500 µs다. 1 kHz에서는 어느 방향 소리든 거의 같은 위상으로 더해진다. 숫자로 확인한다.

```python
# 1.5 cm 2-mic 어레이의 delay-and-sum과 차동(differential) 이득을 주파수별로 계산하는 코드
import numpy as np
c, d = 343.0, 0.015                                  # 1.5 cm (이어버드급)
th = np.radians([0, 90, 180])                       # 0 = 축 방향(endfire, 입 쪽), 180 = 뒤
def das_endfire(f, t):                              # 0도로 조준한 delay-and-sum, |H|
    return np.abs(np.cos(np.pi*f*d*(1-np.cos(t))/c))
def das_broadside(f, t):                            # 정면(90도)으로 조준 = 지연 0으로 그냥 평균
    return np.abs(np.cos(np.pi*f*d*np.cos(t)/c))
def diff(f, t):                                     # delay-and-subtract (1차 차동), 단일 마이크 대비
    return np.abs(2*np.sin(np.pi*f*d*(1+np.cos(t))/c))
dB = lambda v: 20*np.log10(np.maximum(v, 1e-6))
print("f(Hz) | DAS-endfire @90 @180 | DAS-broadside @0 | DIFF @0 @90 (|H|@180) | DIFF noise penalty")
for f in (250, 500, 1000, 2000, 4000, 8000):
    e, b, df = das_endfire(f, th), das_broadside(f, th), diff(f, th)
    pen = 10*np.log10(2/df[0]**2)                   # 0도 이득을 1로 EQ했을 때 마이크 자체잡음 증가
    print(f"{f:5d} | {dB(e[1]):6.2f} {dB(e[2]):6.2f}       | {dB(b[0]):6.2f}           | "
          f"{dB(df[0]):5.1f} {dB(df[1]):5.1f} ({df[2]:.0e}) | {pen:+5.1f} dB")
print(f"spatial alias limit c/(2d) = {c/(2*d):.0f} Hz  (d=5 cm would give {c/(2*0.05):.0f} Hz)")
```

```text
f(Hz) | DAS-endfire @90 @180 | DAS-broadside @0 | DIFF @0 @90 (|H|@180) | DIFF noise penalty
  250 |  -0.01  -0.02       |  -0.01           | -17.2 -23.3 (0e+00) | +20.3 dB
  500 |  -0.02  -0.08       |  -0.02           | -11.2 -17.2 (0e+00) | +14.3 dB
 1000 |  -0.08  -0.33       |  -0.08           |  -5.3 -11.2 (0e+00) |  +8.3 dB
 2000 |  -0.33  -1.38       |  -0.33           |   0.4  -5.3 (0e+00) |  +2.6 dB
 4000 |  -1.38  -6.85       |  -1.38           |   5.0   0.4 (0e+00) |  -2.0 dB
 8000 |  -6.85  -4.63       |  -6.85           |   4.2   5.0 (0e+00) |  -1.2 dB
spatial alias limit c/(2d) = 11433 Hz  (d=5 cm would give 3430 Hz)
```

출력에서 볼 것:

1. **delay-and-sum은 1 kHz 이하에서 사실상 아무것도 안 한다.** 뒤(180°)에서 온 소리를 0.33 dB 줄일 뿐이다. 음성 에너지의 대부분이 1 kHz 아래에 있으니, 1.5 cm DAS는 "마이크 두 개의 자체 잡음을 평균해서 3 dB 줄이는 것" 외에는 효과가 거의 없다. (8 kHz에서 −4.63 dB로 4 kHz의 −6.85 dB보다 덜 줄어든 것은 180° 방향의 첫 null이 c/(4d) ≈ 5.7 kHz에 있어서 그 위로는 다시 올라오기 때문이다.)
2. **차동(differential) 방식** — 한쪽을 d/c만큼 지연시켜 **빼면** — 뒤(180°)가 모든 주파수에서 완전히 0이 된다(카디오이드 패턴). 방향성은 주파수와 무관하게 좋다.
3. 대신 차동 출력은 저주파일수록 작다(250 Hz에서 −17 dB, 즉 1차 고역통과처럼 6 dB/옥타브로 기운다). 이걸 평탄하게 EQ 하면 **두 마이크의 자체 잡음(서로 무상관)이 같이 증폭**되어 250 Hz에서 20 dB 손해를 본다. 이것이 white noise gain 문제다 — 작은 어레이에서 저주파 방향성을 얻는 대가는 잡음이다.

### 7.4 beam pattern 그림과 spatial aliasing

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<circle cx="170" cy="170" r="120" fill="none" stroke="currentColor" stroke-opacity="0.3"/><circle cx="170" cy="170" r="60" fill="none" stroke="currentColor" stroke-opacity="0.2"/><line x1="40" y1="170" x2="300" y2="170" stroke="currentColor" stroke-opacity="0.3"/><line x1="170" y1="40" x2="170" y2="300" stroke="currentColor" stroke-opacity="0.3"/><circle cx="510" cy="170" r="120" fill="none" stroke="currentColor" stroke-opacity="0.3"/><circle cx="510" cy="170" r="60" fill="none" stroke="currentColor" stroke-opacity="0.2"/><line x1="380" y1="170" x2="640" y2="170" stroke="currentColor" stroke-opacity="0.3"/><line x1="510" y1="40" x2="510" y2="300" stroke="currentColor" stroke-opacity="0.3"/>
<polyline points="290,170 288,149 283,129 274,110 262,93 247,78 230,66 211,58 191,53 170,51 149,53 130,59 111,68 95,80 81,95 69,112 61,130 56,150 55,170 56,190 61,210 69,228 81,245 95,260 111,272 130,281 149,287 170,289 191,287 211,282 230,274 247,262 262,247 274,230 283,211 288,191 290,170" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<polyline points="290,170 288,149 283,129 274,110 261,94 246,80 228,70 208,65 189,64 170,68 153,76 140,87 129,99 122,113 118,126 116,139 115,150 115,160 115,170 115,180 115,190 116,201 118,214 122,227 129,241 140,253 153,264 170,272 189,276 208,275 228,270 246,260 261,246 274,230 283,211 288,191 290,170" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<polyline points="290,170 288,149 283,129 273,111 259,95 241,85 221,81 201,85 183,97 170,115 164,137 166,159 165,162 152,149 137,142 122,142 110,148 102,158 100,170 102,182 110,192 122,198 137,198 152,191 165,178 166,181 164,203 170,225 183,243 201,255 221,259 241,255 259,245 273,229 283,211 288,191 290,170" fill="none" stroke="#d0564a" stroke-width="2"/>
<polyline points="630,170 627,149 619,130 607,114 591,102 574,94 555,92 538,94 522,100 510,109 501,121 496,132 495,144 496,153 499,161 503,166 507,169 509,170 510,170 509,170 507,171 503,174 499,179 496,187 495,196 496,208 501,219 510,231 522,240 538,246 555,248 574,246 591,238 607,226 619,210 627,191 630,170" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<polyline points="630,170 628,149 621,130 610,112 595,99 578,89 559,84 541,85 524,90 510,100 500,112 494,125 492,138 493,150 497,159 501,165 506,168 509,170 510,170 509,170 506,172 501,175 497,181 493,190 492,202 494,215 500,228 510,240 524,250 541,255 559,256 578,251 595,241 610,228 621,210 627,191 630,170" fill="none" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="5 3"/>
<text x="300" y="166" font-size="12">0°</text><text x="22" y="166" font-size="12">180°</text><text x="640" y="166" font-size="12">0°</text><text x="170" y="24" font-size="13" text-anchor="middle">delay-and-sum (0°로 조준)</text><text x="510" y="24" font-size="13" text-anchor="middle">차동 (delay-and-subtract)</text><line x1="40" y1="316" x2="62" y2="316" stroke="#3f9a6b" stroke-width="2"/><text x="66" y="320" font-size="12">1 kHz</text><line x1="110" y1="316" x2="132" y2="316" stroke="#4a7bd0" stroke-width="2"/><text x="136" y="320" font-size="12">4 kHz</text><line x1="180" y1="316" x2="202" y2="316" stroke="#d0564a" stroke-width="2"/><text x="206" y="320" font-size="12">8 kHz</text><line x1="400" y1="316" x2="422" y2="316" stroke="#4a7bd0" stroke-width="2"/><text x="426" y="320" font-size="12">1 kHz</text><line x1="480" y1="316" x2="502" y2="316" stroke="#e08a3c" stroke-dasharray="5 3"/><text x="506" y="320" font-size="12">4 kHz (모양 거의 같음)</text><text x="340" y="336" font-size="12" text-anchor="middle">d = 1.5 cm, 축(0°) 방향 endfire 배치, 각 패턴은 최댓값 1로 정규화한 선형 크기</text>
</svg>
```

그림 11 — 계산한 beam pattern(10° 간격). 왼쪽 delay-and-sum은 1 kHz(초록)에서 거의 완전한 원 — 방향성이 없다 — 이고 4 kHz, 8 kHz에서야 뒤쪽이 눌린다. 오른쪽 차동 방식은 1 kHz와 4 kHz에서 같은 카디오이드 모양이며 180°에 null이 있다. 단, 차동의 절대 이득은 저주파에서 작다(7.3절 표).

**spatial aliasing**: 마이크 간격이 반파장보다 크면(d > λ/2) 서로 다른 두 방향이 같은 위상차를 만들어 구별이 안 된다 — 시간 영역 aliasing(G5)의 공간 버전이다. 조건은 `d < λ/2 = c/(2f)`, 즉 `f < c/(2d)`. 1.5 cm면 11.4 kHz까지 안전하니 16 kHz 샘플(8 kHz 대역)에서는 문제없다. 반대로 노트북처럼 5 cm 간격이면 3.4 kHz 위에서 grating lobe가 생긴다. 요약하면 **간격이 넓으면 고주파에서 aliasing, 좁으면 저주파에서 방향성 부족**이다. 웨어러블은 후자 쪽에 산다.

### 7.5 adaptive beamforming — MVDR 개념

고정 beamformer는 방향만 보고 계수를 정한다. **적응형** beamformer는 실제로 들어온 잡음의 공간 통계를 보고 계수를 정한다. 대표가 **MVDR(Minimum Variance Distortionless Response)** 이다.

```
min_w  wᴴ·R·w     subject to  wᴴ·a(θ_target) = 1
해:   w = R⁻¹·a / (aᴴ·R⁻¹·a)
```

말로 하면: 목표 방향 소리는 이득 1로 그대로 통과시키면서(distortionless), 나머지 출력 전력(잡음+간섭)을 최소로 만드는 가중치를 고른다. R은 주파수 bin마다의 마이크 간 공분산 행렬(2×2), a는 목표 방향의 steering vector(마이크별 위상)다. 실제로는 STFT bin마다 이 계산을 한다.

```python
# 1 kHz, 1.5 cm 2-mic에서 DAS와 MVDR의 간섭 억제·white noise gain을 비교하는 코드
import numpy as np
c, d, f = 343.0, 0.015, 1000.0
pos = np.array([0.0, d])                                # 축 위 두 마이크 (endfire 배치)
def steer(deg):                                         # 방향 deg에서 온 평면파의 마이크별 위상
    tau = -pos*np.cos(np.radians(deg))/c                # 0도 쪽 마이크(뒤 마이크 기준)가 먼저 듣는다
    return np.exp(-2j*np.pi*f*tau)
a_t, a_i = steer(0), steer(120)                         # target: 입(0도), interferer: 120도
def report(name, w):
    g_t, g_i = abs(np.vdot(w, a_t)), abs(np.vdot(w, a_i))
    wng = g_t**2/np.vdot(w, w).real                     # white noise gain (1마이크=1, 2마이크 DAS=2)
    print(f"{name:14s} target {20*np.log10(g_t):6.2f} dB, interferer {20*np.log10(g_i):7.2f} dB, "
          f"WNG {10*np.log10(wng):6.1f} dB")
report("DAS", a_t/2)
for load in (1e-4, 1e-2):                               # 마이크 자체 잡음 (diagonal loading 역할)
    Rm = 1.0*np.outer(a_i, a_i.conj()) + load*np.eye(2) # 간섭 공분산 + 잡음
    Ri = np.linalg.inv(Rm)
    w = Ri@a_t/(a_t.conj()@Ri@a_t)                      # MVDR: target 이득 1, 나머지 출력 최소
    report(f"MVDR load={load:g}", w)
```

```text
DAS            target   0.00 dB, interferer   -0.19 dB, WNG    3.0 dB
MVDR load=0.0001 target  -0.00 dB, interferer  -58.66 dB, WNG  -10.8 dB
MVDR load=0.01 target  -0.00 dB, interferer  -19.62 dB, WNG   -9.8 dB
```

출력에서 볼 것: 1 kHz에서 DAS는 120° 간섭을 0.19 dB밖에 못 줄이지만, MVDR은 간섭 방향에 null을 놓아 58.7 dB를 줄인다. 대가는 **WNG(white noise gain)** 가 +3 dB(DAS)에서 −10.8 dB로 14 dB 나빠지는 것 — 마이크 자체 잡음이 그만큼 증폭된다. 잡음 항(diagonal loading)을 키우면 null은 얕아지고(−19.6 dB) WNG는 조금 낫다. 실제 MVDR 구현은 이 loading으로 "얼마나 공격적으로 null을 팔지"를 조절한다. 또 마이크 감도·위상이 개체마다 ±1~3 dB 다르면 steering vector가 틀려져서 목표 음성까지 깎는 "self-cancellation"이 생긴다. 그래서 작은 어레이는 **마이크 매칭(공장 캘리브레이션)** 이 성능을 좌우한다.

### 7.6 임베디드 연결과 함정

- 두 마이크 경로의 **샘플 정렬**: 같은 PDM 클럭·같은 decimation 체인·같은 DMA 버퍼로 받아야 한다. 한 마이크만 한 샘플(62.5 µs) 밀려도 최대 TDOA(44~58 µs)보다 크다 — beamformer가 엉뚱한 방향을 본다.
- 마이크 간 감도·위상 편차 → 공장에서 기준 음원으로 측정해 gain/위상 보정값을 저장(Don의 factory test 경험 그대로).
- 웨어러블은 머리·몸이 음장을 바꾼다(그림자, 반사). 자유 음장 공식대로 설계한 steering vector가 착용 상태에서 틀릴 수 있다 → 착용 상태에서 측정한 전달함수를 쓰거나, 적응형/ML 방식으로 보완한다.
- 이어버드의 대표적 배치는 "입 쪽을 향한 endfire 2-mic + 차동/적응 처리"이고, 귓속(feedback) 마이크나 골전도 센서를 더하기도 한다(9.2절, typical — 제품마다 다르다).

---

## 8. 에코 제거(AEC), 잡음 억제, AGC

### 8.1 문제 — 기기가 자기 목소리를 듣는다

기기가 TTS로 말하는 동안 사용자가 "그만"이라고 끼어드는 것(barge-in)을 들으려면, 마이크에 들어온 **자기 스피커 소리(에코)** 를 지워야 한다. 웨어러블은 스피커와 마이크가 수 cm 거리라 에코가 사용자 목소리보다 **더 클** 수 있다. B5 6.2절에서 개념을 봤고, 여기서는 직접 만든다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="g4d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="20" y="40" font-size="12">원단 신호 x(n)</text><text x="20" y="56" font-size="12">(TTS · 통화 상대)</text><line x1="20" y1="70" x2="520" y2="70" stroke="currentColor" marker-end="url(#g4d)"/><rect x="522" y="50" width="90" height="40" rx="6" fill="#888" fill-opacity="0.2" stroke="currentColor"/><text x="567" y="75" font-size="12" text-anchor="middle">스피커</text><path d="M567,90 C600,140 600,170 567,190" fill="none" stroke="#d0564a" stroke-dasharray="5 4" marker-end="url(#g4d)"/><text x="676" y="145" font-size="12" text-anchor="end">에코 경로 h</text><text x="676" y="161" font-size="12" text-anchor="end">(공기·기구 진동)</text><rect x="522" y="190" width="90" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/><text x="567" y="215" font-size="12" text-anchor="middle">마이크 d(n)</text><text x="440" y="246" font-size="12">+ 사용자 목소리 s(n) + 잡음</text><line x1="160" y1="70" x2="160" y2="108" stroke="currentColor" marker-end="url(#g4d)"/>
<rect x="100" y="110" width="140" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="170" y="130" font-size="12" text-anchor="middle">적응 FIR ŵ</text><text x="170" y="146" font-size="12" text-anchor="middle">(NLMS)</text><line x1="240" y1="132" x2="318" y2="196" stroke="currentColor" marker-end="url(#g4d)"/><text x="262" y="150" font-size="12">ŷ(n) 예측 에코</text><line x1="520" y1="210" x2="342" y2="210" stroke="currentColor" marker-end="url(#g4d)"/><circle cx="330" cy="210" r="12" fill="none" stroke="currentColor"/><text x="330" y="215" font-size="13" text-anchor="middle">−</text><line x1="318" y1="210" x2="40" y2="210" stroke="currentColor" marker-end="url(#g4d)"/><text x="60" y="230" font-size="12">e(n) = d − ŷ → NS · ASR</text><path d="M240,210 L240,178 L200,162" fill="none" stroke="#e08a3c" stroke-dasharray="4 3" marker-end="url(#g4d)"/><text x="248" y="186" font-size="12">e로 계수 갱신</text>
</svg>
```

그림 12 — AEC 구조. 스피커로 보내는 신호 x(n)를 우리가 알고 있으므로, 스피커 → 마이크 경로 h를 적응 FIR ŵ로 흉내 내 예측 에코 ŷ를 만들고 마이크 신호에서 뺀다. 남은 e(n)이 출력이자 필터를 고치는 오차 신호다. Don의 RF 경험에서 TX leakage를 알려진 TX 신호로 추정해 빼는 구조와 같다.

### 8.2 NLMS — 가장 많이 쓰이는 적응 필터

```
ŷ(n) = ŵᵀ·x(n)                     x(n) = [x(n), x(n−1), …, x(n−L+1)]  (최근 L개 reference)
e(n) = d(n) − ŷ(n)
ŵ ← ŵ + μ · e(n) · x(n) / (‖x(n)‖² + δ)
```

말로 하면: 오차가 나면, 그 오차를 만든 입력 방향으로 계수를 조금 고친다. 입력 전력 ‖x‖²로 나누는 것(Normalized LMS)이 핵심이다 — 스피커 소리가 크든 작든 수렴 속도가 같아진다. A3의 SGD와 같은 식이다: 손실 e²/2의 기울기가 −e·x이고, μ가 학습률이다. 0 < μ < 2면 (이상적 조건에서) 수렴하고, 크면 빠르지만 잡음에 흔들린다.

필터 길이 L은 에코 경로 길이로 정한다. 16 kHz에서 웨어러블의 짧은 기구 경로는 수 ms(수십~수백 탭)일 수 있지만, 방 반사까지 잡으려면 수십~수백 ms(수천 탭)가 필요하다. 그래서 실제 AEC는 **주파수 영역(블록) 적응 필터**(FFT로 블록 단위 계산, sub-band별 μ)를 많이 쓴다(typical).

### 8.3 코드로 확인 — 에코 제거와 ERLE

**ERLE(Echo Return Loss Enhancement)** = 마이크의 에코 전력 / AEC 후 남은 에코 전력(dB). AEC가 에코를 몇 dB 줄였는지다. 시뮬레이션: 256탭(16 ms) 에코 경로, 4초, 2.5~3.0초에 사용자가 동시에 말한다(**double-talk**).

```python
# aec.py — 에코 시나리오 + NLMS (dtd: double-talk 검출 방식)
import numpy as np
from scipy import signal
rng = np.random.default_rng(0)
fs, L, T = 16000, 256, 4*16000                       # 256탭 = 16 ms 에코 경로, 4초
h = rng.standard_normal(L)*np.exp(-np.arange(L)/40); h[:8] = 0   # 8샘플 지연 + 지수 감쇠
h *= 0.3/np.abs(h).max()
far = signal.lfilter([1], [1, -0.9], rng.standard_normal(T)); far /= far.std()  # 스피커로 나가는 신호
echo = signal.lfilter(h, 1, far)                     # 스피커 -> 마이크 경로를 거친 에코
near = np.zeros(T); dt = slice(int(2.5*fs), int(3.0*fs))     # 2.5~3.0 s: 사용자도 말함 (double-talk)
near[dt] = 0.5*signal.lfilter([1], [1, -0.9], rng.standard_normal(dt.stop-dt.start))/2.3
mic = echo + near + 0.003*rng.standard_normal(T)     # 마이크 = 에코 + 근단 음성 + 잡음
def nlms(mic, far, mu=0.5, dtd=None):          # dtd: None / 'oracle' / 'geigel'
    w = np.zeros(L); x = np.zeros(L); res_echo = np.zeros(T); e_out = np.zeros(T)
    for n in range(T):
        x = np.roll(x, 1); x[0] = far[n]             # 최근 L개 reference (x[0]=최신)
        e = mic[n] - w@x                             # 에러 = 마이크 - 예측 에코
        e_out[n] = e; res_echo[n] = echo[n] - w@x    # (시뮬레이션이라 '남은 에코'만 따로 볼 수 있다)
        if dtd == "oracle": talk = near[n] != 0      # 이상적 DTD (시뮬레이션에서만 가능)
        elif dtd == "geigel": talk = abs(mic[n]) > 0.5*np.abs(x).max()  # 고전 Geigel 규칙
        else: talk = False
        if not talk:
            w += mu*e*x/(x@x + 1e-6)                 # NLMS 갱신: 입력 전력으로 정규화
    return w, res_echo
```

```python
# 시간에 따른 ERLE와 double-talk 때의 발산을 보는 코드
import numpy as np
from aec import nlms, mic, far, echo, h
def erle(res, blk=800):                               # 50 ms 블록마다 10·log10(에코 전력 / 남은 에코 전력)
    E, Rr = echo.reshape(-1, blk), res.reshape(-1, blk)
    return 10*np.log10((E**2).mean(1)/((Rr**2).mean(1) + 1e-20))
for dtd in (None, "geigel", "oracle"):
    w, res = nlms(mic, far, mu=0.5, dtd=dtd)
    e = erle(res); np.save(f"erle_{dtd}.npy", e)
    mis = 10*np.log10(np.sum((w-h)**2)/np.sum(h**2))  # 추정 필터와 진짜 경로의 차이
    print(f"DTD={str(dtd):6s}: ERLE @0.25s {e[5]:4.1f} | @1s {e[20]:4.1f} | @2.45s {e[49]:4.1f} | "
          f"@2.95s {e[59]:4.1f} | @3.5s {e[70]:4.1f} dB | misalign {mis:5.1f} dB")
```

```text
DTD=None  : ERLE @0.25s 22.7 | @1s 42.4 | @2.45s 54.7 | @2.95s  5.7 | @3.5s 46.3 dB | misalign -43.1 dB
DTD=geigel: ERLE @0.25s 21.2 | @1s 38.8 | @2.45s 54.2 | @2.95s  6.3 | @3.5s 42.6 dB | misalign -37.4 dB
DTD=oracle: ERLE @0.25s 22.7 | @1s 42.4 | @2.45s 54.7 | @2.95s 51.6 | @3.5s 55.5 dB | misalign -50.4 dB
```

출력에서 볼 것:

1. **수렴**: 0.25초에 23 dB, 1초에 42 dB, 2.45초에 55 dB. 이상적 시뮬레이션(선형 경로, 작은 잡음)이라 숫자가 크다. 실제 기기의 선형 AEC는 비선형성·잡음·경로 변화 때문에 훨씬 낮다(수십 dB 미만이 흔하다 — typical, 조건에 따라 크게 다르다).
2. **double-talk**: 사용자가 말하는 2.5~3.0초에 DTD가 없으면 ERLE가 55 dB에서 **5.7 dB로 붕괴**한다. 사용자 목소리 s(n)가 e(n)에 섞여 들어가 "에코 오차"로 오인되고, 필터가 사용자 목소리를 지우는 방향으로 계수를 망가뜨린다. 이상적 DTD(oracle)로 그 구간 갱신을 멈추면 51.6 dB를 유지한다.
3. **Geigel DTD**(마이크 크기가 최근 reference 최댓값의 절반보다 크면 double-talk로 판정)는 이 시나리오에서 거의 효과가 없다. Geigel은 "에코 경로 손실이 6 dB 이상"을 가정하는데, 여기서는 에코가 reference와 비슷하게 크다 — 스피커와 마이크가 가까운 **웨어러블에서는 이 가정이 깨진다.** 그래서 실제 제품은 상관 기반 DTD, 주파수 영역의 다른 판단, 또는 갱신 속도를 연속적으로 조절하는 방식을 쓴다.

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<rect x="410" y="20" width="70" height="220" fill="#e08a3c" fill-opacity="0.15" stroke="none"/><text x="445" y="36" font-size="12" text-anchor="middle">double-talk</text><line x1="60" y1="240" x2="620" y2="240" stroke="currentColor"/><line x1="60" y1="20" x2="60" y2="240" stroke="currentColor"/><line x1="60" y1="167" x2="620" y2="167" stroke="currentColor" stroke-opacity="0.15"/><line x1="60" y1="93" x2="620" y2="93" stroke="currentColor" stroke-opacity="0.15"/><text x="54" y="244" font-size="12" text-anchor="end">0</text><text x="54" y="171" font-size="12" text-anchor="end">20</text><text x="54" y="97" font-size="12" text-anchor="end">40</text><text x="54" y="24" font-size="12" text-anchor="end">60</text><text x="60" y="258" font-size="12" text-anchor="middle">0</text><text x="200" y="258" font-size="12" text-anchor="middle">1</text><text x="340" y="258" font-size="12" text-anchor="middle">2</text><text x="480" y="258" font-size="12" text-anchor="middle">3</text><text x="620" y="258" font-size="12" text-anchor="middle">4</text><text x="340" y="278" font-size="12" text-anchor="middle">시간 (s) · 세로: ERLE (dB, 50 ms 블록)</text>
<polyline points="64,196 70,181 78,171 84,165 92,157 98,157 106,148 112,140 120,132 126,132 134,127 140,130 148,124 154,121 162,117 168,108 176,103 182,106 190,103 196,95 204,84 210,89 218,82 224,76 232,75 239,74 246,69 252,63 260,66 266,54 274,55 280,52 288,47 294,53 302,46 308,48 316,43 322,38 330,44 336,42 344,45 350,41 358,35 365,42 372,41 378,41 386,44 392,43 400,38 406,40 414,51 420,48 428,49 435,48 442,53 449,46 456,44 462,51 470,53 476,51 484,40 490,41 498,43 505,40 512,43 518,43 526,40 532,41 540,38 546,42 554,36 560,43 568,44 574,42 582,41 588,43 596,43 602,40 610,39 616,38" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<polyline points="64,196 70,181 78,171 84,165 92,157 98,157 106,148 112,140 120,132 126,132 134,127 140,130 148,124 154,121 162,117 168,108 176,103 182,106 190,103 196,95 204,84 210,89 218,82 224,76 232,75 239,74 246,69 252,63 260,66 266,54 274,55 280,52 288,47 294,53 302,46 308,48 316,43 322,38 330,44 336,42 344,45 350,41 358,35 365,42 372,41 378,41 386,44 392,43 400,38 406,40 414,213 420,215 428,218 435,217 442,214 449,214 456,218 462,221 470,212 476,219 484,190 490,163 498,145 505,122 512,115 518,102 526,96 532,86 540,80 546,80 554,70 560,74 568,66 574,63 582,59 588,60 596,57 602,49 610,47 616,45" fill="none" stroke="#d0564a" stroke-width="2"/>
<line x1="120" y1="215" x2="150" y2="215" stroke="#d0564a" stroke-width="2"/><text x="156" y="219" font-size="12">DTD 없음 — double-talk에서 붕괴</text><line x1="120" y1="197" x2="150" y2="197" stroke="#3f9a6b" stroke-width="2"/><text x="156" y="201" font-size="12">이상적 DTD — 갱신 멈춤</text>
</svg>
```

그림 13 — 실제 시뮬레이션의 ERLE(50 ms 블록). 처음 2.5초는 두 곡선이 같다(같은 계산). double-talk 구간(주황 띠)에서 DTD가 없는 필터(빨강)는 ERLE가 5 dB 근처로 떨어지고, 구간이 끝난 뒤 다시 수렴하는 데 0.5초 이상 걸린다. 그동안 사용자에게는 에코가 새어 나간다.

### 8.4 펌웨어가 AEC를 망치는 법 — reference 정렬

AEC 알고리즘보다 펌웨어가 더 자주 문제다.

- **지연 정렬**: reference x(n)(스피커로 보낸 PCM)와 마이크 d(n) 사이의 지연(출력 DMA 버퍼 + DAC/앰프 + 음향 경로 + 마이크 decimation 지연(5.7절 약 1 ms) + 입력 DMA 버퍼)이 필터 길이 L 안에 들어와야 하고, **흔들리면 안 된다.** 버퍼 underrun 한 번에 지연이 10 ms 바뀌면 필터는 처음부터 다시 수렴해야 한다.
- **클럭**: 스피커 경로와 마이크 경로가 다른 클럭(예: 48 kHz codec 클럭 vs PDM 클럭)에서 나오면 수십 ppm의 drift가 생기고, 지연이 천천히 미끄러진다(G7). 같은 클럭 소스에서 파생시키거나, drift를 추정해 SRC로 보정한다.
- **reference는 앰프 앞의 신호**: 앰프·스피커의 비선형(작은 스피커를 크게 울릴 때의 찌그러짐, 기구 진동)은 선형 필터로 못 지운다. 그래서 **잔여 에코 억제(RES)** 를 뒤에 둔다 — 고전적으로는 band gain 억제, 요즘은 작은 신경망 mask(B5 6.2절).

### 8.5 noise suppression — spectral subtraction과 Wiener

STFT(G5)의 bin마다 잡음 전력 N(k)를 추정해 두고(말소리가 없을 때 갱신 — VAD가 필요), 각 bin에 gain을 곱한다.

```
spectral subtraction:  |Ŝ(k)|² = max(|X(k)|² − α·N(k), β·|X(k)|²)
Wiener gain:           G(k) = SNR(k) / (1 + SNR(k)),   SNR(k) = 추정 음성 전력 / N(k)
```

말로 하면: 잡음보다 훨씬 큰 bin은 그대로(G ≈ 1), 잡음과 비슷한 bin은 크게 깎는다(SNR = 1이면 G = 0.5, 즉 −6 dB). 손계산: SNR(k) = 9(9.5 dB)면 G = 0.9, SNR = 0.25면 G = 0.2(−14 dB).

고전 방식은 정상(stationary) 잡음(팬, 차 소음)에는 좋지만 키보드·사람 말소리 같은 비정상 잡음에 약하고, bin별 gain이 들쭉날쭉하면 "musical noise"(뾰르륵거리는 잔여음)가 남는다. ML 방식(RNNoise, DTLN 등)은 이 gain을 신경망이 예측한다(B5 6.1절). 그리고 B5에서 말했듯 **사람 귀에 좋은 NS와 KWS/ASR에 좋은 NS는 다르다** — 과한 NS는 인식률을 떨어뜨릴 수 있으니 ML 입력용 경로와 통화 출력용 경로를 분리하는 설계가 흔하다.

### 8.6 AGC

**AGC(Automatic Gain Control)** 는 출력 레벨을 목표(예: −20 dBFS 근처)로 맞추도록 gain을 천천히 조절한다. attack(커질 때 빠르게 줄임)과 release(작아질 때 천천히 키움) 시간 상수, 잡음만 있을 때 gain을 키우지 않는 noise gate가 핵심이다. Don에게는 RF 수신기의 AGC 루프와 같은 구조다. ML 입력 경로에서 AGC는 양날의 검이다: 레벨은 일정해지지만, 학습 데이터에 없던 "gain이 움직이는 신호"를 만든다. 쓴다면 학습 데이터 수집 때도 같은 AGC를 켜고 수집한다.

---

## 9. 웨어러블의 현실 — 바람, 진동, 기구

### 9.1 바람 잡음

바람은 마이크 port 근처에서 난류를 만들어 **아주 큰 저주파 잡음**(수백 Hz 이하에 집중, 순간적으로 AOP 근처까지)을 낸다. 특징은 두 마이크 사이 **상관이 낮다**는 것이다 — 소리(평면파)는 두 마이크에 거의 같은 파형으로 오지만, 난류는 각 port에서 따로 생긴다. 그래서 저주파 대역의 마이크 간 coherence가 갑자기 떨어지면 바람으로 판정할 수 있다. 대응: 기구(메시, 폼, port를 바람 그림자에 두기), 바람 검출 시 차동 beamforming 끄기(7.3절에서 봤듯 차동 처리는 무상관 잡음을 20 dB씩 증폭한다!), 덜 맞는 마이크로 전환, 저역 HPF 강화.

### 9.2 진동과 골전도 음성 검출

이어버드는 사용자가 말할 때 턱·두개골을 통해 전달되는 진동을 **가속도계**나 **VPU(voice pick-up) 센서**로 잡아 "지금 착용자가 말하고 있다"를 판단하는 데 쓰기도 한다(typical — 제품마다 다르다). 이 신호는 주변 소음에 거의 영향받지 않지만 대역이 좁다(수 kHz 아래 위주). 쓰임새는 (1) 착용자 음성 VAD(남의 말소리에 깨지 않기), (2) 시끄러운 환경에서 저역 음성 보강, (3) 바람 속 통화. G1(가속도계)의 ODR·대역·noise density가 그대로 사양이 된다 — 음성 검출용이면 수 kHz 대역이 필요해 일반 IMU 설정(100 Hz~1 kHz ODR)과 다르다. 반대로 **기기 자체의 진동**(스피커 진동, 햅틱 모터)은 기구를 타고 마이크에 들어가는 구조 전달 에코가 되어 AEC를 어렵게 만든다.

### 9.3 sealing과 음향 설계

- **gasket 누설**: 기기 외곽 구멍과 마이크 port 사이가 밀봉되지 않으면 기기 내부 소리(스피커 뒤쪽 소리, 팬, 진동)가 섞이고 방향성이 망가진다.
- **음향 통로 공진**: 외곽 구멍 → 통로 → port → 마이크 앞 공간은 Helmholtz 공진기처럼 동작해 고역에 공진 봉우리를 만든다. 통로가 길고 좁을수록 공진 주파수가 내려와 음성 대역에 가까워진다.
- **방수·방진 membrane**: IP 등급을 위한 막은 감도와 SNR을 몇 dB 깎는 것이 흔하다(typical).
- **결론**: 마이크 성능 검증은 단품이 아니라 **완제품 상태**에서, 주파수 응답·SNR·마이크 간 매칭·스피커 → 마이크 에코 경로를 측정해야 한다. 이 측정값이 ML 데이터 수집과 augmentation(기기 전달함수를 학습 데이터에 컨볼루션) 설계의 입력이 된다.

---

## 10. 임베디드 관점에서 다시 보기 — 펌웨어 경로와 예산

### 10.1 경로 한 장

```
 MEMS mic ──PDM CLK/DATA──▶ PDM 수신 블록 ──(HW decimation 있으면 여기서 PCM)──▶ DMA
                              │ (없으면 SPI/I2S로 원시 비트 수신 → SW CIC + FIR)
                              ▼
            ┌─────────── DMA ping-pong (B5 9절, E4 3.5절) ───────────┐
            │  buf A: DMA가 채우는 중      buf B: CPU/DSP가 처리 중   │
            └──── half/full-transfer IRQ마다 10 ms(160 샘플) 블록 ────┘
                              ▼
     [SW decimation (필요시)] → DC 제거 → gain → (멀티마이크: beamforming, AEC)
                              ▼
            pre-roll 링버퍼 (AON SRAM, 1~2 s)  ──▶  VAD (매 10 ms)
                                                    │ 말소리
                                                    ▼
                                             KWS (DSP, 매 10~20 ms)
                                                    │ 키워드
                                                    ▼
                                    AP 깨움 → pre-roll부터 ASR (E8 1.3절)
```

설계 규칙 몇 가지:

- **블록 크기 = 특징 hop**: DMA 반 버퍼를 10 ms(16 kHz에서 160 샘플)로 맞추면 인터럽트 하나가 log-mel 한 프레임과 대응해 코드가 단순해진다.
- **마이크 여러 개는 interleave**: 2채널 PDM이면 DMA가 L/R을 번갈아 쓰도록 하고, 처리 코드는 stride 2로 읽는다. 두 채널이 같은 IRQ에서 같은 인덱스로 처리되니 샘플 정렬(7.6절)이 구조적으로 보장된다.
- **HW decimation이 있으면 쓴다**: 코어가 PDM 비트를 만지지 않아야 깊은 sleep에 들어갈 수 있다.
- **pre-roll은 decimation 후 PCM으로**: 원시 PDM(1.024 Mbit/s = 128 KB/s)은 PCM(32 KB/s)보다 4배 크다.

### 10.2 SW decimation 비용 견적

HW 블록이 없거나 특별한 필터가 필요해 소프트웨어로 할 때의 연산량이다.

```python
# SW decimation의 초당 연산 수와 코어 점유율을 견적하는 코드
fs_pdm, R, N, taps, fs_out = 1_024_000, 32, 5, 63, 16000
integ = N*fs_pdm                         # 적분기: 입력 비트마다 N번 덧셈
comb  = N*fs_pdm//R                      # comb: 32 kHz에서 N번 뺄셈
fir   = taps*fs_out                      # polyphase: 버릴 출력은 계산 안 함 -> 16 kHz × 63 MAC
fir_sym = (taps+1)//2*fs_out             # 대칭 계수 이용: 곱셈 32개 (덧셈은 그대로)
tot = integ + comb + fir
print(f"integrators {integ/1e6:5.2f} M add/s | combs {comb/1e6:4.2f} M sub/s | FIR {fir/1e6:4.2f} M MAC/s "
      f"(sym {fir_sym/1e6:4.2f} M mul/s)")
print(f"total ≈ {tot/1e6:.2f} M ops/s per mic, {2*tot/1e6:.2f} M for 2 mics")
for mhz in (48, 100, 200):               # '1 op ≈ 1 cycle'이라는 낙관적 가정
    print(f"  on a {mhz:3d} MHz core: {100*2*tot/(mhz*1e6):5.1f} % (2 mics)")
print(f"PDM input words: {fs_pdm/32/1e3:.0f} k 32-bit words/s per mic -> DMA, not per-bit IRQs")
print(f"pre-roll 2 s @16 kHz int16, 2 mics: {2*16000*2*2/1024:.0f} KiB")
```

```text
integrators  5.12 M add/s | combs 0.16 M sub/s | FIR 1.01 M MAC/s (sym 0.51 M mul/s)
total ≈ 6.29 M ops/s per mic, 12.58 M for 2 mics
  on a  48 MHz core:  26.2 % (2 mics)
  on a 100 MHz core:  12.6 % (2 mics)
  on a 200 MHz core:   6.3 % (2 mics)
PDM input words: 32 k 32-bit words/s per mic -> DMA, not per-bit IRQs
pre-roll 2 s @16 kHz int16, 2 mics: 125 KiB
```

출력에서 볼 것: 비용의 80%가 1.024 MHz로 도는 적분기다. 그래서 SW 구현은 첫 단을 비트 단위로 돌리지 않는다. 대표적인 트릭은 (1) 비트 8개(1바이트)를 한 번에 처리하는 **lookup table** — 첫 단 FIR(또는 CIC 첫 부분)의 8비트 부분합을 256-엔트리 표로 미리 계산 — 과 (2) `popcount`(1의 개수 세기 명령)로 boxcar 첫 단을 계산하는 것이다. 이렇게 하면 적분기 비용이 몇 분의 1로 준다. 48 MHz MCU에서 2채널 26%는 always-on 예산으로 부담스럽다 — **HW PDM 블록이 있는 칩을 고르는 이유**다. 그리고 "1 op = 1 cycle"은 낙관적이다. 메모리 접근·루프 오버헤드(E4의 zero-overhead loop가 없는 코어)를 넣으면 더 든다. 실제 숫자는 사이클 카운터(DWT CYCCNT 등)로 측정한다.

### 10.3 always-on 전력 예산 (설명용 가정)

| 항목 | 가정 | 전력 (1.8 V 기준) |
|---|---|---|
| 마이크 2개, 저전력 모드 | 개당 수십~수백 µA (가정: 150 µA) | 2 × 0.27 mW = 0.54 mW |
| 마이크 2개, 고성능 모드 | 개당 수백 µA~1 mA (가정: 600 µA) | 2 × 1.08 mW = 2.16 mW |
| HW PDM decimation + DMA | 칩마다 다름 (가정: 수십~수백 µW) | 0.1 ~ 0.3 mW |
| VAD (LP core, 매 10 ms) | E8 0.3절 방식으로 duty-cycle | 수십~수백 µW |

말로 하면: always-on 단계에서는 **마이크 자체 전류가 처리 전류만큼 혹은 더 크다.** 그래서 (1) VAD 단계는 마이크 1개·저전력 모드, (2) 말소리가 감지되면 2개·고성능 모드로 전환, (3) 하드웨어 wake-on-sound 마이크로 호스트 PDM 블록까지 끄기 같은 단계적 설계가 의미가 있다. 표의 숫자는 감을 잡기 위한 가정이며, 실제 부품 데이터시트와 보드 측정으로 바꿔 넣어야 한다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| CIC 레지스터 폭 < B_in + N·log2(RM) | 큰 소리·바람에서만 "딱" 소리, 평소엔 정상 | 출력 범위 wrap (5.5절 W = 26) | 공식대로 폭 결정, full-scale 테스트 벡터 |
| C에서 CIC를 `int32_t`로 계산 | 최적화 레벨에 따라 결과가 바뀜 | signed 오버플로는 UB | `uint32_t`로 wrap 산술, 마지막에 부호 확장 |
| 보상 FIR 생략 | 고역 3~4 dB 처짐, 공개 데이터 학습 모델의 정확도 하락 | CIC droop (5.6절) | 보상 FIR 또는 HW 블록의 보상 옵션 |
| PDM L/R 에지 설정 반대 | 채널 뒤바뀜, beamformer가 뒤를 봄 | 마이크 SELECT 핀 vs 수신 블록 에지 | 한쪽 마이크를 막고 채널 확인 |
| I2S 형식(1비트 지연) 불일치 | 값 2배/절반, 부호 깨짐 | Philips vs left-justified | 형식 레지스터 확인, 알려진 톤으로 검증 |
| int16으로 자른 뒤 gain | 작은 소리 왜곡, 양자화 잡음 증가 | 하위 비트 이미 손실 | 고정밀에서 gain → 마지막에 포화 |
| Q15 DC blocker truncation | 출력에 수십 LSB DC | 피드백 경로 잘림 오차 누적 (6.2절) | error feedback 또는 반올림 |
| 두 마이크 샘플 1개 어긋남 | 방향 추정 엉뚱, beamforming 효과 없음 | 다른 DMA/IRQ 경로 | 같은 클럭·같은 DMA·interleave |
| AEC reference 지연 흔들림 | 에코가 주기적으로 새어 나옴 | 버퍼 underrun, 클럭 drift | 지연 고정, 같은 클럭, drift 보정 |
| 웨어러블에 Geigel DTD 그대로 | double-talk 때 에코 증가·사용자 음성 깎임 | 에코가 근단 음성보다 큼 (8.3절) | 상관 기반 DTD, 적응 속도 제어 |

---

## 12. 면접에서 이렇게 말한다

**Q.** "Explain PDM and how you get PCM from it."

**A.** PDM은 마이크 안의 ΣΔ 변조기가 내는 1비트 고속 비트열로, 1의 밀도가 신호 값이다. 양자화 잡음은 noise shaping으로 고주파에 밀려 있으므로, 저역통과 필터로 깎고 솎아 내면 PCM이 된다. 보통 CIC로 크게 줄이고 보상 FIR(또는 halfband들)로 마무리한다. 1.024 MHz ÷ 64 = 16 kHz가 대표적 구성이다.

> PDM is the one-bit, heavily oversampled output of the sigma-delta modulator inside the microphone — the density of ones encodes the signal, and the quantization noise has been pushed to high frequencies by noise shaping. To get PCM you low-pass filter and decimate: typically a CIC stage does most of the rate reduction cheaply, then a compensation FIR or a few halfband stages finish the job, for example 1.024 MHz divided by 64 to 16 kHz. In a simulation I did, a second-order modulator plus a fifth-order CIC and a 63-tap FIR gave about 69 dB SNR at 16 kHz with roughly 1 ms of group delay. On a real chip I use the hardware PDM block so the core can sleep.

**Q.** "What's a CIC filter and why compensate it?"

**A.** 길이 R 이동합을 N번 겹친 필터를 적분기 N개(고속) + comb N개(저속)로 곱셈 없이 구현한 것이다. 레지스터 폭은 B_in + N·log2(RM)이면 적분기 wrap이 있어도 출력이 정확하다. 응답이 sinc^N이라 통과대역이 처지므로(예: N = 5, R = 32에서 4 kHz −1.1 dB, 6 kHz −2.5 dB) 역 sinc FIR로 보상한다.

> A CIC filter is a cascade of N moving-sum stages implemented as N integrators at the high rate and N combs at the low rate, so it needs no multipliers — ideal for the first decimation stage. Two things matter in implementation: the register width must be the input width plus N times log2 of R·M, and then integrator overflow is harmless because two's-complement wrap is undone by the combs. I verified that bit-exactly in C — 27-bit registers matched a 64-bit reference, while 26 bits failed only on full-scale input. Its response is sinc to the N, so it droops in the passband — about 1.1 dB at 4 kHz for N=5, R=32 — and a short inverse-sinc FIR at the intermediate rate flattens it.

**Q.** "Mic sensitivity is −26 dBFS and SNR is 64 dB. What's the noise floor in dB SPL?"

**A.** SNR은 94 dB SPL 기준이므로 94 − 64 = 30 dB SPL(A). dBFS로는 0 dBFS = 94 + 26 = 120 dB SPL이니 −90 dBFS. AOP가 120이면 dynamic range 90 dB.

> SNR is referenced to 94 dB SPL, so the equivalent input noise is 94 minus 64, which is 30 dB SPL A-weighted. With −26 dBFS sensitivity, full scale corresponds to 120 dB SPL, so the noise floor sits at −90 dBFS. One practical consequence: conversational speech at a meter is about 60 dB SPL, or −60 dBFS, which is only about 33 LSBs peak in int16 — so I keep the decimator output at higher precision before applying gain.

**Q.** "How does a 2-mic beamformer work and what limits it on a small device?"

**A.** 원하는 방향의 소리가 두 마이크에서 같은 시각이 되도록 지연시켜 더하거나(delay-and-sum) 빼서(differential) 공간 필터를 만든다. 1.5 cm면 시간차가 최대 44 µs라 1 kHz 이하에서 DAS는 거의 효과가 없고, 차동 방식은 방향성은 좋지만 저주파 이득이 작아 EQ하면 자체 잡음이 250 Hz에서 약 20 dB 커진다. 마이크 매칭, 샘플 정렬, 바람(무상관 잡음)이 실전 한계다.

> A two-mic beamformer exploits the arrival-time difference between the mics: delay-and-sum aligns the target direction and adds, differential processing delays and subtracts to put a null behind. The limit on a wearable is spacing — at 1.5 cm the maximum delay is about 44 microseconds, far below the period of speech frequencies, so delay-and-sum gives almost no directivity below 1 kHz. Differential arrays keep a frequency-independent cardioid, but their low-frequency output is small, and equalizing it amplifies the uncorrelated self-noise — about 20 dB at 250 Hz in my calculation — which also makes them terrible in wind. In practice the hard parts are sample-accurate alignment, per-unit mic gain and phase calibration, and switching modes when wind is detected.

**Q.** "How does AEC work?"

**A.** 스피커로 보낸 신호를 reference로 알고 있으므로, 스피커 → 마이크 경로를 NLMS 같은 적응 FIR로 추정해 예측 에코를 마이크 신호에서 뺀다. 성능 지표는 ERLE. double-talk 때 갱신을 멈추지 않으면 필터가 발산한다. 남은 비선형 에코는 residual echo suppression(요즘은 작은 신경망)으로 누른다. 펌웨어 쪽 핵심은 reference와 마이크의 지연 정렬과 클럭.

> Because we know exactly what we sent to the speaker, we model the speaker-to-mic path with an adaptive FIR — NLMS or a frequency-domain variant — predict the echo and subtract it, and the residual drives the coefficient update. ERLE measures how much echo is removed. The classic failure is double-talk: when the user speaks, the error contains their voice and the filter diverges — in my simulation ERLE collapsed from 55 dB to under 6 dB without a double-talk detector. On wearables the speaker is so close that the echo can be louder than the user, which breaks simple detectors like Geigel. Nonlinear residual echo goes to a suppression stage, often a small neural mask. And from the firmware side, a stable, sample-accurate alignment between the reference and mic streams, on a common clock, matters more than the algorithm.

**Q.** "Your KWS works on the dev kit but has a higher false-reject rate on the product. Where in the mic path would you look?"

**A.** 같은 소리를 두 기기에 틀고 PCM을 캡처해 비교한다: 이득(dB 차이), 주파수 응답(기구 공진, CIC 보상 유무), DC·clipping, 샘플률(분주비), 마이크 간 정렬. 그다음 그 전달함수를 학습 데이터에 augmentation으로 넣는다.

> I'd play the same calibrated signals into both devices and capture PCM right after decimation. Then compare overall gain, frequency response — acoustic port resonance or a missing CIC compensation easily tilts the top octave by several dB — DC offset, clipping rate, the actual sample rate from the clock divider, and for multi-mic builds the inter-mic alignment. Each of those changes the log-mel features the model sees. Once the device transfer function is measured, I either fix it in the front end or convolve it into the training data as augmentation, and add a regression test with recorded product audio.

---

## 13. 직접 해보기

1. **손계산**: analog 마이크 sensitivity −42 dBV/Pa, SNR 66 dB(A). 94 dB SPL에서 출력 전압(mV rms)과 자체 잡음(µV rms)은?
   정답: 10^(−42/20) = 7.94 mV rms, 잡음 = 7.94 mV × 10^(−66/20) = 3.98 µV rms (EIN 28 dB SPL).
2. **손계산**: CIC N = 4, R = 64, M = 1, 입력 1비트(±1 → 2비트 signed). B_out은? 3.072 MHz PDM이면 CIC 출력 샘플률은?
   정답: 2 + 4·log2(64) = 26비트, 3.072 MHz / 64 = 48 kHz.
3. **손계산**: 1차 ΣΔ에 x = −0.25를 일정하게 넣고 v[0] = 0에서 시작해 8개 출력을 구하라. 1의 밀도는?
   정답: 출력 +1, −1, −1, +1, −1, −1, +1, −1 → 8개 중 3개가 1, 밀도 0.375 = (1 + (−0.25))/2.
4. **손계산**: 마이크 간격 1 cm, 음속 343 m/s. 최대 TDOA는 몇 µs, 48 kHz에서 몇 샘플인가? spatial aliasing이 시작되는 주파수는?
   정답: 29.2 µs, 1.4 샘플, c/(2d) = 17.15 kHz.
5. **코드**: `chain.py`의 보상 FIR을 halfband 두 단으로 바꿔 보라 — CIC를 R = 16(64 kHz 출력)으로 하고, `signal.remez`로 halfband(차단 fs/4)를 설계해 64 → 32 → 16 kHz로 내리고, 마지막에 짧은 보상 FIR을 둔다. 0~6 kHz 평탄도와 SNR, 총 곱셈 수를 63탭 단일 FIR과 비교하라.
   힌트: halfband는 탭 수가 4k+3 꼴이면 가운데를 뺀 짝수 번째 계수가 0이 된다. 0이 아닌 계수만 세라.
6. **코드**: `aec.py`의 μ를 0.1, 0.5, 1.0으로 바꿔 1초 시점 ERLE와 double-talk 구간의 최저 ERLE를 표로 만들어라. 그리고 near-end 대역 상관 기반 DTD(마이크와 예측 에코 ŷ의 정규화 상관이 떨어지면 double-talk)를 구현해 Geigel과 비교하라.
   힌트: μ가 작을수록 수렴은 느리지만 double-talk에 덜 흔들린다 — 학습률과 같은 트레이드오프다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| MEMS microphone | 실리콘 미세 가공 마이크 | 진동판 + backplate 콘덴서 + ASIC을 한 패키지에 |
| diaphragm / backplate | 진동판 / 고정판 | 간격 변화 → 용량 변화 → 전압 |
| acoustic port | 소리 구멍 | top port(뚜껑) / bottom port(PCB 쪽) |
| dB SPL | 음압 레벨 | 20 µPa 기준, 94 dB SPL = 1 Pa |
| dBFS | 디지털 full scale 대비 레벨 | 0 dBFS가 최대 |
| sensitivity | 감도 | 94 dB SPL·1 kHz에서의 출력 (dBFS 또는 dBV/Pa) |
| SNR (A-weighted) | 신호 대 잡음비 | 94 dB SPL 신호 대 자체 잡음, 귀 가중치 적용 |
| EIN | 등가 입력 잡음 | 94 − SNR dB SPL |
| AOP | acoustic overload point | THD 10%가 되는 음압, 디지털 마이크는 보통 0 dBFS 근처 |
| PSRR | 전원 잡음 제거비 | VDD 리플이 출력에 새는 정도 |
| PDM | pulse density modulation | 1비트 고속 비트열, 1의 밀도 = 신호 |
| ΣΔ modulator | 시그마-델타 변조기 | 오차를 적분해 피드백, 1비트로 고정밀 |
| noise shaping | 잡음 성형 | 양자화 잡음을 관심 대역 밖(고역)으로 밀어냄 |
| OSR | oversampling ratio | 변조 클럭 / (2 × 신호 대역), 예: 64 |
| I2S | Inter-IC Sound | BCLK·WS·SD 3선 PCM 버스 |
| TDM | time-division multiplexing | 한 프레임에 슬롯 여러 개 |
| SoundWire | MIPI 오디오 버스 | 2선 멀티드롭, 제어+오디오 |
| decimation | 데시메이션 | 저역통과 + 샘플 솎기 |
| CIC | cascaded integrator-comb | 곱셈 없는 sinc^N decimation 필터 |
| bit growth | 비트 증가 | CIC 출력 폭 = B_in + N·log2(RM) |
| droop | 통과대역 처짐 | sinc^N 응답 때문에 고역 이득 감소 |
| compensation FIR | 보상 FIR | 역 sinc로 droop을 평탄하게 |
| halfband filter | 하프밴드 필터 | 차단 fs/4, 계수 절반이 0인 ↓2용 FIR |
| group delay | 군지연 | 필터가 신호를 늦추는 시간, 선형위상 FIR = (탭−1)/2 |
| DC blocker | DC 차단기 | y = x − x₋₁ + a·y₋₁ 1차 HPF |
| TDOA | time difference of arrival | 두 마이크 도착 시간 차 = d·sinθ/c |
| GCC-PHAT | 위상 변환 일반화 상호상관 | 크기를 정규화해 지연 봉우리를 뾰족하게 |
| broadside / endfire | 정면 / 축 방향 | 어레이 축에 수직 / 나란한 방향 |
| delay-and-sum | 지연 합 beamformer | 목표 방향 정렬 후 평균 |
| differential array | 차동 어레이 | 지연 후 빼기, 카디오이드, 저역 잡음 증폭 |
| spatial aliasing | 공간 aliasing | d > λ/2이면 방향 모호 |
| MVDR | minimum variance distortionless response | 목표 이득 1, 나머지 출력 최소화 |
| WNG | white noise gain | 무상관 자체 잡음에 대한 어레이 이득 |
| AEC | acoustic echo cancellation | 스피커 → 마이크 에코를 적응 필터로 제거 |
| NLMS | normalized least mean squares | 입력 전력으로 정규화한 적응 필터 갱신 |
| ERLE | echo return loss enhancement | AEC가 줄인 에코 dB |
| double-talk / DTD | 양쪽 동시 발화 / 그 검출기 | 이때 적응을 멈춰야 발산 안 함 |
| RES | residual echo suppression | 선형 AEC 뒤 남은 에코 억제 |
| spectral subtraction / Wiener | 스펙트럼 차감 / 위너 필터 | bin별 잡음 추정 후 gain |
| AGC | automatic gain control | 출력 레벨을 목표로 자동 조절 |
| VPU | voice pick-up sensor | 골전도 진동으로 착용자 음성 검출 |

---

## 15. 요약 & 체크리스트

MEMS 마이크는 진동판-backplate 콘덴서와 ASIC이 한 패키지에 든 센서이고, 데이터시트의 sensitivity(94 dB SPL 기준)·SNR·AOP만으로 noise floor(94 − SNR dB SPL), full scale(94 − sensitivity dB SPL), dynamic range를 계산할 수 있다. 웨어러블에서 흔한 디지털 PDM 마이크는 ΣΔ 변조로 잡음을 고역에 밀어낸 1비트 비트열을 내고, 호스트는 CIC(곱셈 없음, 폭 = B_in + N·log2(RM), wrap 허용) → 보상 FIR(droop 평탄화, 안티에일리어싱) 체인으로 16 kHz PCM을 만든다. 그 뒤 DC 제거와 gain을 고정밀에서 하고, 링버퍼를 거쳐 VAD/KWS로 간다. 마이크가 둘 이상이면 TDOA(GCC-PHAT)와 beamforming을 쓰지만, 1~2 cm 어레이는 저주파 방향성이 약하고(DAS) 방향성을 얻으면 잡음이 커진다(차동, MVDR의 WNG). 기기가 말을 하면 NLMS AEC로 에코를 지우되 double-talk와 reference 정렬이 핵심이다. 그리고 이 모든 것이 바람·진동·기구 설계에 크게 좌우되므로 완제품 상태에서 측정한다.

- [ ] sensitivity −26 dBFS, SNR 64 dB, AOP 120 dB SPL에서 noise floor, 0 dBFS의 SPL, 1 m 대화의 int16 코드를 손으로 계산할 수 있다
- [ ] PDM의 L/R 공유 방식과 I2S(BCLK·WS·1비트 지연)·TDM 프레임을 타이밍도로 그릴 수 있다
- [ ] 1차 ΣΔ를 일정 입력에 대해 손으로 몇 스텝 돌려 1의 밀도를 확인하고, noise shaping을 (1 − z⁻¹)로 설명할 수 있다
- [ ] CIC의 구조(적분기 → ↓R → comb), bit growth 공식, wrap이 괜찮은 이유를 말하고 C로 짤 수 있다
- [ ] sinc^N droop을 특정 주파수에서 계산하고 보상 FIR이 왜 필요한지 설명할 수 있다
- [ ] decimation 체인의 group delay와 SW 구현 연산량(ops/s)을 견적할 수 있다
- [ ] Q15 DC blocker의 truncation DC 문제와 error feedback 해법을 설명할 수 있다
- [ ] TDOA 공식과 GCC-PHAT의 원리, 서브샘플 보간이 필요한 이유를 설명할 수 있다
- [ ] 1.5 cm 어레이에서 DAS·차동·MVDR의 장단점(저역 방향성, WNG, 매칭)을 숫자로 말할 수 있다
- [ ] NLMS AEC의 갱신식, ERLE, double-talk 발산, reference 정렬 문제를 설명할 수 있다

---

## 참고 자료

- E. B. Hogenauer, "An Economical Class of Digital Filters for Decimation and Interpolation," IEEE Trans. ASSP, 1981 — CIC 원 논문 (bit growth, register pruning)
- R. Schreier, G. C. Temes, "Understanding Delta-Sigma Data Converters," Wiley — ΣΔ 변조와 noise shaping 표준 교재
- R. G. Lyons, "Understanding Digital Signal Processing," Prentice Hall — CIC·halfband·decimation을 쉽게 설명
- C. H. Knapp, G. C. Carter, "The Generalized Correlation Method for Estimation of Time Delay," IEEE Trans. ASSP, 1976 — GCC-PHAT
- J. Benesty, J. Chen, Y. Huang, "Microphone Array Signal Processing," Springer, 2008 — DAS, 차동 어레이, MVDR
- G. W. Elko, 차동 마이크 어레이 관련 논문들 — 작은 어레이의 방향성
- S. Haykin, "Adaptive Filter Theory," Prentice Hall — LMS/NLMS
- Analog Devices(구 InvenSense) Application Note AN-1112 "Microphone Specifications Explained", AN-1140 "Microphone Array Beamforming" — 마이크 사양과 어레이 입문
- Philips Semiconductors, "I2S bus specification" (1986) — I2S 원 사양
- MIPI Alliance, SoundWire 사양 소개 — https://www.mipi.org/
- scipy.signal 문서 (firwin2, freqz, lfilter, remez) — https://docs.scipy.org/doc/scipy/reference/signal.html
- J.-M. Valin, "A Hybrid DSP/Deep Learning Approach to Real-Time Full-Band Speech Enhancement" (RNNoise), 2018 — ML 잡음 억제 (B5 6.1절)
