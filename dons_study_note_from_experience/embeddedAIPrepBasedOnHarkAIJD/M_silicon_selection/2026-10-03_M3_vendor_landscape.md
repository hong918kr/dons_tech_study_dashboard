# M3. 엣지 AI 실리콘 벤더 지형 — 웨어러블 AI 기기에서 고를 수 있는 칩들

> **이 노트를 다 읽으면**: 웨어러블 AI 기기에 들어갈 수 있는 칩을 네 세그먼트(always-on MCU · 웨어러블/AR SoC · edge 가속기 · 라이선스 IP)로 나누고 세그먼트마다 대표 부품의 CPU, AI 블록, 공개 TOPS와 정밀도, 메모리, 전력 등급, 소프트웨어 스택을 출처와 함께 말할 수 있다 · 벤더 TOPS 숫자를 "MAC/cycle × 2 × 주파수"로 역검산하고, 대역폭과 TOPS가 둘 다 공개된 경우에만 파생 지표(TOPS/W, byte/op, decode 상한)를 만들 수 있다 · "Qualcomm 웨어러블/AR SoC + 저전력 동반 MCU"라는 추정 구조에서 어떤 부품 조합이 왜 후보가 되는지, 언제 커스텀 NPU를 고려하는지, 스타트업 NPU 벤더의 리스크가 무엇인지 면접에서 근거를 대며 설명할 수 있다
> **JD 연결**: "Evaluate and select silicon platforms (GPUs, NPUs etc.)" · "Work with platform vendors to bring up toolchains, SDKs and new accelerator" — study_prep_list **M3** 행: Qualcomm (Snapdragon, AR1/AR2, W5), MediaTek, NXP i.MX 9x, Ambiq Apollo4/5, Nordic nRF54, STM32N6, Alif Ensemble(Ethos-U), Syntiant, Hailo, Google Tensor, Apple(참고). 함께 닿는 행: **M1**(평가 기준표), **M2**(벤치마크 방법론), **M4**(데이터시트 해석)
> **Don 기준 난이도**: 부품 선정 · 벤더 데이터시트 읽기 · 새 실리콘 bring-up · 2nd source와 공급 리스크는 SSD/RF 경험으로 이미 안다 / 새로 배울 것은 "edge AI 칩 이름과 계보", NPU 블록이 어디에 붙어 있는지, 각 벤더의 ML 소프트웨어 스택 이름, TOPS 숫자가 어떤 조건에서 나온 것인지
> **선행 노트**: E2(Cortex-M/A, Helium), E3(RISC-V), E4(DSP — HiFi, Hexagon), E5(NPU 구조 — Ethos-U, Edge TPU, ANE), E6(GPU — Adreno, Mali, Jetson), E8(이기종 SoC), F4(Qualcomm QNN · AI Hub), F6(그 밖의 런타임 · MCU 도구), D3(roofline), D5(LLM decode 성능), I3(cascade · 분할). 이 노트는 그 내용을 다시 설명하지 않고 ID로 가리킨다

---

## 0. 큰 그림 — 이게 왜 필요한가

Edge ML 엔지니어 면접에서 "칩을 고른다"는 말은 두 가지를 뜻한다. 하나는 **이미 정해진 플랫폼 안에서 어느 블록(CPU, DSP, NPU, 동반 MCU)에 무엇을 둘지** 고르는 일이다(I3, F7). 다른 하나는 **다음 제품이나 다음 세대에 어떤 칩을 넣을지** 평가하는 일이다(M1, M2, M4). 두 번째 질문에 답하려면 먼저 **선택지의 지도**가 머리에 있어야 한다. 이 노트가 그 지도다.

Don에게 익숙한 말로 바꾸면 이렇다. SSD 컨트롤러를 새로 고를 때 "Marvell, Phison, Silicon Motion, 자체 ASIC 중 무엇이 있고, 각각 어느 세그먼트(클라이언트/엔터프라이즈)에 강하고, 펌웨어 SDK가 얼마나 열려 있는가"를 먼저 안다. 그다음에 평가 기준(M1)과 qualification(M2)으로 들어간다. edge AI 칩도 똑같다.

이 노트에서 쓰는 네 세그먼트는 이렇다.

| 세그먼트 | 한 줄 정의 | 웨어러블에서의 역할 (추론) | 절 |
|---|---|---|---|
| Always-on / 초저전력 MCU | Cortex-M(또는 RISC-V) + SIMD(Helium) 또는 작은 NPU. mW 단위 | 마이크·IMU를 항상 보고 있다가 큰 칩을 깨운다 | 2 |
| 웨어러블/AR 애플리케이션 SoC | Cortex-A + LPDDR + DSP/NPU. 수백 mW~W 단위 | 음성 인식, SLM, 카메라, OS(Android/Linux) | 3 |
| Edge 가속기 | PCIe/M.2로 붙는 NPU 또는 Jetson 같은 모듈. W~수십 W | 웨어러블보다 로봇·홈 허브용 | 4 |
| 라이선스 IP | NPU/DSP 설계도를 사서 자기 칩에 넣는다 | 회사가 나중에 커스텀 실리콘을 만들 때 | 5 |

전력 등급(가로)과 공개 연산량(세로)으로 이 세그먼트들을 한 장에 놓으면 이렇다.

```svg
<svg viewBox="0 0 680 390" xmlns="http://www.w3.org/2000/svg">
<text x="370" y="16" font-size="12" text-anchor="middle">라이선스 IP(Ethos-U/N · Neo · NPX6 · NeuPro-Nano · Coral NPU)는 이 지도의 어느 칸에도 들어간다</text> <line x1="90" y1="330" x2="660" y2="330" stroke="currentColor"/> <line x1="90" y1="30" x2="90" y2="330" stroke="currentColor"/> <line x1="204" y1="30" x2="204" y2="330" stroke="#888" stroke-opacity="0.3"/>
<line x1="318" y1="30" x2="318" y2="330" stroke="#888" stroke-opacity="0.3"/> <line x1="432" y1="30" x2="432" y2="330" stroke="#888" stroke-opacity="0.3"/> <line x1="546" y1="30" x2="546" y2="330" stroke="#888" stroke-opacity="0.3"/> <line x1="90" y1="270" x2="660" y2="270" stroke="#888" stroke-opacity="0.3"/>
<line x1="90" y1="210" x2="660" y2="210" stroke="#888" stroke-opacity="0.3"/> <line x1="90" y1="150" x2="660" y2="150" stroke="#888" stroke-opacity="0.3"/> <line x1="90" y1="90" x2="660" y2="90" stroke="#888" stroke-opacity="0.3"/> <text x="84" y="304" font-size="12" text-anchor="end">≤50 GOPS</text>
<text x="84" y="244" font-size="12" text-anchor="end">0.1–1 TOPS</text> <text x="84" y="184" font-size="12" text-anchor="end">1–10 TOPS</text> <text x="84" y="124" font-size="12" text-anchor="end">10–100 TOPS</text> <text x="84" y="64" font-size="12" text-anchor="end">100+ TOPS</text>
<text x="147" y="348" font-size="12" text-anchor="middle">≤1 mW</text> <text x="261" y="348" font-size="12" text-anchor="middle">1–100 mW</text> <text x="375" y="348" font-size="12" text-anchor="middle">0.1–2 W</text> <text x="489" y="348" font-size="12" text-anchor="middle">2–5 W</text>
<text x="603" y="348" font-size="12" text-anchor="middle">5–25 W</text> <text x="375" y="378" font-size="13" text-anchor="middle">전력 등급 (대략, 이 노트의 분류)</text> <rect x="88" y="146" width="346" height="186" rx="10" fill="none" stroke="#d0564a" stroke-width="2" stroke-dasharray="7 4"/> <text x="100" y="164" font-size="12">Hark 같은 웨어러블의 후보 영역</text>
<rect x="96" y="274" width="102" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/> <text x="147" y="294" font-size="12" text-anchor="middle">Always-on 전용</text> <text x="147" y="312" font-size="11" text-anchor="middle">NDP · SoC 내 eNPU</text> <rect x="210" y="214" width="102" height="112" rx="6" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/>
<text x="261" y="234" font-size="12" text-anchor="middle">MCU + Helium</text> <text x="261" y="250" font-size="12" text-anchor="middle">/ micro-NPU</text> <text x="261" y="270" font-size="11" text-anchor="middle">Apollo5 · STM32N6</text> <text x="261" y="286" font-size="11" text-anchor="middle">Alif · RA8P1 · PSOC</text>
<text x="261" y="302" font-size="11" text-anchor="middle">MCX N · RT700</text> <text x="261" y="318" font-size="11" text-anchor="middle">GAP9 · ESP32-P4</text> <rect x="324" y="154" width="102" height="112" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/> <text x="375" y="174" font-size="12" text-anchor="middle">웨어러블·AR SoC</text>
<text x="375" y="194" font-size="11" text-anchor="middle">W5+ · Wear Elite</text> <text x="375" y="210" font-size="11" text-anchor="middle">AR1 · AR1+ · AR2</text> <text x="375" y="226" font-size="11" text-anchor="middle">i.MX 93/95</text> <text x="375" y="242" font-size="11" text-anchor="middle">SL2610 · Exynos W</text>
<text x="375" y="258" font-size="11" text-anchor="middle">(Apple S: 참고)</text> <rect x="438" y="154" width="102" height="80" rx="6" fill="#3f9a6b" fill-opacity="0.1" stroke="#3f9a6b" stroke-dasharray="4 3"/> <text x="489" y="176" font-size="12" text-anchor="middle">IoT·폰 SoC</text> <text x="489" y="196" font-size="11" text-anchor="middle">Genio</text>
<text x="489" y="212" font-size="11" text-anchor="middle">Dimensity · Tensor</text> <rect x="438" y="94" width="102" height="54" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/> <text x="489" y="116" font-size="12" text-anchor="middle">M.2 가속기</text> <text x="489" y="134" font-size="11" text-anchor="middle">Coral · Hailo</text>
<rect x="552" y="34" width="102" height="114" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/> <text x="603" y="58" font-size="12" text-anchor="middle">Edge 박스</text> <text x="603" y="78" font-size="11" text-anchor="middle">Jetson Orin</text> <text x="603" y="94" font-size="11" text-anchor="middle">Axelera Metis</text>
<text x="603" y="110" font-size="11" text-anchor="middle">(로봇·홈 허브)</text>
</svg>
```

그림 1 — 엣지 AI 실리콘 지형도. 상자 위치는 대략이고(전력 등급은 이 노트가 정한 분류, 측정값이 아니다) 상자 안 이름은 2~5절에서 출처와 함께 다시 나온다. 빨간 점선은 Hark 같은 웨어러블이 실제로 고를 만한 영역이다. 오른쪽 위(가속기·Edge 박스)는 연산은 크지만 배터리 기기에는 너무 뜨겁다.

### 0.1 이 노트의 규칙 — 숫자는 출처와 함께

칩 스펙은 마케팅 숫자가 섞여 있고, 제품은 해마다 바뀐다. 그래서 이 노트는 다음 규칙을 지킨다.

- 모든 구체적 숫자(TOPS, 코어, 공정, 메모리, 발표 연도)에는 `[n]`을 붙인다. `[n]`은 맨 아래 **참고 자료**의 번호다. **확인일은 모두 2026-10-03**이다.
- 공식 자료(벤더 보도자료, 제품 페이지, product brief, 데이터시트)를 우선하고, 2차 자료(기술 매체)만 있으면 "2차"라고 적는다.
- 숫자를 확인하지 못하면 쓰지 않거나 "확인 필요"라고 적는다. 예를 들어 Nordic Axon NPU는 TOPS가 공개되지 않았다.
- "Hark에는 이게 맞다" 같은 문장은 **이 노트의 추론**이다. Hark의 실제 부품은 공개되지 않았다. 단서는 이 작업공간의 이전 리서치(`hark_ai_embedded_swe_context.md` §2.2)에 있는 채용 공고 문구뿐이다 — 7절에서 다룬다.
- 파생 지표(TOPS/W, byte/op, tok/s 상한)는 **두 숫자가 모두 공개된 경우에만** 계산하고 "derived"라고 표시한다.

### 0.2 이 노트에서 하는 것과 하지 않는 것

| 한다 | 하지 않는다 |
|---|---|
| 세그먼트별 대표 부품과 출처 있는 스펙 표 | 모든 SKU 나열 (변형이 수십 개다) |
| 벤더 숫자 역검산, 파생 지표, 차트 생성 (Python) | 실제 보드 벤치마크 (M2, PJ8에서) |
| Hark 같은 웨어러블 관점의 강점·약점 (추론 표시) | "이 칩이 최고다" 같은 결론 |
| 면접에서 쓸 비교 문장 | 가격 (공개 가격이 드물고 물량마다 다르다) |

---

## 1. 지도를 읽는 법 — 칩 스펙 한 줄을 해부하기

벤더 페이지의 스펙 한 줄은 대개 이렇게 생겼다.

```text
Cortex-M55 @ 400 MHz + Ethos-U85 NPU (up to 204 GOPS), 9.75 MB SRAM, BLE ...
```

이 한 줄에서 읽어야 할 것은 여섯 가지다. 이 노트의 모든 표가 이 여섯 칸을 쓴다.

| 칸 | 무엇을 보나 | 함정 | 관련 노트 |
|---|---|---|---|
| CPU | 코어 이름과 클럭. M33/M55/M85, A53/A55/A78 | 클럭만 보고 성능을 추정하면 안 된다. M55는 Helium이 있다 | E1, E2 |
| AI 블록 | NPU 이름, MAC 수, DSP(HiFi, Hexagon), SIMD | "AI 가속"이 SIMD 명령인지 별도 NPU인지 구분 | E4, E5 |
| TOPS와 정밀도 | 공개 peak 연산량, int8/int4/sparse | int4나 sparse 숫자를 int8과 비교하면 2배 부풀려진다 | M4 |
| 메모리 | 온칩 SRAM, 외부 LPDDR 폭과 속도 | SRAM이 모델을 다 못 담으면 NPU가 굶는다 | D2, E7 |
| 전력 등급 | µW/mW/W 어느 단위로 사는 칩인가 | 활성 전력과 sleep 전력은 다르다 | D7, E9 |
| 소프트웨어 스택 | 컴파일러/SDK 이름, 입력 포맷, OS | 칩은 좋은데 툴이 미성숙하면 일정이 무너진다 | F1–F8 |

### 1.1 TOPS 숫자를 분해하기 — 손계산

E5에서 배운 식을 다시 쓴다.

```
peak OPS = MAC/cycle × 2 × f
           (MAC 하나 = 곱셈 1 + 덧셈 1 = 연산 2개)
```

말로 하면, NPU가 한 사이클에 MAC을 몇 개 하는지에 2를 곱하고 클럭을 곱하면 벤더가 말하는 OPS가 나온다.

손으로 해 보자. Renesas는 RA8P1의 Ethos-U55가 "256 MACs per cycle"이고 "256 GOPS at 500 MHz"라고 발표했다 [10].

```
256 MAC × 2 × 500×10⁶ /s = 256×10⁹ OPS = 256 GOPS   ✓ 일치
```

Arm은 Ethos-U85가 128~2048 MAC이고 "4 TOPS @ 1 GHz"라고 했다 [40].

```
2048 × 2 × 1×10⁹ = 4.096×10¹² ≈ 4 TOPS   ✓ (반올림)
```

ST는 STM32N6의 Neural-ART가 600 GOPS이고 NPU 클럭이 최대 1 GHz라고 했다 [5]. MAC 수를 거꾸로 구하면:

```
MAC/cycle = 600×10⁹ / (2 × 1×10⁹) = 300
```

ST 블로그는 "nearly 300 configurable MAC units"라고 적었다 [5]. 역산과 맞는다. 이렇게 **역산이 맞아떨어지면 그 숫자는 최소한 산수상 일관된 peak**라는 뜻이다. 맞지 않으면 sparse, int4, 다른 클럭 조건이 숨어 있는지 의심한다(M4).

### 1.2 코드로 확인 — 벤더 숫자 역검산 (예제 1)

다섯 개 NPU 숫자를 한꺼번에 검산하는 코드다. Alif E4의 Ethos-U85는 "up to 204 GOPS"만 공개되어 있어서 [8], 256 MAC × 400 MHz라고 **가정**하면 맞는지 본다(Alif가 이 출처에서 MAC 수와 NPU 클럭을 밝히지 않았기 때문에 `(?)`로 표시했다).

```python
# 예제 1: TOPS = MAC/cycle × 2 × f 로 벤더 숫자를 역검산한다
rows = [  # (part, NPU, MACs/cycle, clock_Hz, vendor_GOPS)
    ("Renesas RA8P1", "Ethos-U55", 256, 500e6, 256),
    ("Alif E4 (?)",   "Ethos-U85", 256, 400e6, 204),
    ("Arm max cfg",   "Ethos-U85", 2048, 1e9, 4000),
    ("Arm max cfg",   "Ethos-U65", 512, 1e9, 1000),
    ("STM32N6",       "Neural-ART", None, 1e9, 600),
]
print(f"{'part':14s} {'NPU':10s} {'MAC':>5s} {'f(MHz)':>7s} {'calc GOPS':>9s} {'vendor':>7s}")
for part, npu, mac, f, vend in rows:
    if mac is None:   # MAC 수 대신 GOPS만 공개 → 역산
        mac_implied = vend * 1e9 / (2 * f)
        print(f"{part:14s} {npu:10s} {'~'+str(round(mac_implied)):>5s} {f/1e6:7.0f} {'(역산)':>9s} {vend:7d}")
        continue
    gops = mac * 2 * f / 1e9
    print(f"{part:14s} {npu:10s} {mac:5d} {f/1e6:7.0f} {gops:9.1f} {vend:7d}")
```

```text
part           NPU          MAC  f(MHz) calc GOPS  vendor
Renesas RA8P1  Ethos-U55    256     500     256.0     256
Alif E4 (?)    Ethos-U85    256     400     204.8     204
Arm max cfg    Ethos-U85   2048    1000    4096.0    4000
Arm max cfg    Ethos-U65    512    1000    1024.0    1000
STM32N6        Neural-ART  ~300    1000      (역산)     600
```

출력에서 볼 것: RA8P1과 Arm 최대 구성은 벤더 숫자와 정확히 맞는다. Alif E4는 "256 MAC × 400 MHz"라는 가정이 204.8 GOPS를 만들어서 공개값 204와 맞는다 — 하지만 이건 **가정이 일관된다**는 뜻이지 확인된 사실이 아니다. STM32N6은 역산하면 MAC 약 300개다.

### 1.3 Don 경험과 연결

SSD 컨트롤러 데이터시트의 "순차 읽기 7 GB/s"가 어떤 큐 깊이, 어떤 블록 크기, 어떤 온도에서 나온 숫자인지 확인했던 것과 똑같다. TOPS도 **조건이 붙은 peak**다. 이 노트는 지도를 그리는 노트라서 peak를 그대로 옮기지만, 실제 선정에서는 M2(벤치마크)와 M4(데이터시트 해석)로 반드시 다시 잰다.

### 1.4 함정

- **TOPS가 없는 칩을 0으로 취급하지 말 것.** Ambiq Apollo510은 NPU 없이 Helium만으로 "vast majority of AI"를 한다고 주장한다 [2]. Nordic Axon은 TOPS를 공개하지 않았다 [6]. 이런 칩은 표에서 "미공개"로 두고, 실제 모델로 잰다.
- **SoC 안의 여러 AI 블록을 합산한 숫자를 조심할 것.** 폰·PC SoC의 "AI TOPS"는 NPU만인지 CPU+GPU+NPU 합인지 문서마다 다르다.
- **같은 이름, 다른 세대.** "Hexagon"은 DSP 이름이기도 하고 NPU 이름이기도 하다. W5+ Gen 1은 "Dual Hexagon QDSP V66K"(DSP)이고 [21], AR1 Gen 1은 "3rd Generation Hexagon NPU"다 [25]. E4 6절과 F4 1.1절 참고.

---

## 2. Always-on / 초저전력 MCU — 항상 켜져 있는 작은 두뇌

### 2.1 직관 — 경비원과 사장

웨어러블에서 큰 SoC는 사장이다. 일을 잘하지만 깨어 있으면 전기를 많이 먹는다. 그래서 대부분의 시간은 자고 있고, **경비원(always-on MCU)**이 마이크와 IMU를 지켜보다가 "사장님, 손님 왔습니다(wake word)"라고 깨운다. 경비원이 똑똑할수록 사장을 덜 깨우고, 배터리가 오래 간다. I3(cascade)의 1단이 바로 이 칩이다.

이 세그먼트의 칩은 AI를 하는 방식에 따라 네 갈래로 나뉜다.

| 갈래 | AI를 하는 곳 | 대표 | 장점 | 약점 |
|---|---|---|---|---|
| ① Helium(SIMD)만 | CPU 벡터 명령 (CMSIS-NN) | Ambiq Apollo5, Apollo330 | 툴체인 단순, 모든 op가 CPU에서 돈다 | NPU 대비 처리량 낮음 |
| ② Arm micro-NPU | Ethos-U55/U85 + Vela 컴파일러 | Alif, Renesas RA8P1, Infineon PSOC Edge E83/E84 | 표준 IP라 툴·모델 이식성 좋음 | 지원 op 밖은 CPU fallback |
| ③ 벤더 자체 NPU | 자체 설계 NPU + 벤더 툴 | ST Neural-ART, NXP eIQ Neutron, Nordic Axon, Infineon NNLite | 칩에 최적화 | 벤더 툴에 종속 |
| ④ 전용 always-on 칩 | 신경망 전용 저전력 칩 | Syntiant NDP, GreenWaves GAP9 | 수 mW 이하 상시 동작 | 범용 펌웨어는 별도 MCU 필요 |

### 2.2 비교 표

| 부품 | CPU | AI 블록 | 공개 TOPS · 정밀도 | 메모리 | 전력 등급 | SW 스택 | 대표 용도 | 출처 |
|---|---|---|---|---|---|---|---|---|
| Ambiq Apollo510 (2024) | Cortex-M55 + Helium, 최대 250 MHz | NPU 없음, Helium "8 MACs per cycle" | 미공개 (NPU 없음) | NVM 4 MB, SRAM+TCM 3.75 MB | mW 이하~수 mW (추론) | neuralSPOT SDK, heliaRT | 웨어러블, 음성, 센서 | [1][2] |
| Ambiq Apollo4 Plus | Cortex-M4F, 최대 192 MHz | NPU 없음 (DSP 확장) | 미공개 | MRAM 2 MB, SRAM 2.75 MB | "4 µA/MHz" (MRAM 실행) | neuralSPOT (추론) | 스마트워치, 피트니스 밴드 | [3] |
| Nordic nRF54LM20B (2026) | Cortex-M33 128 MHz + RISC-V 코프로세서 | Axon NPU (Atlazo 인수, 2023) | 미공개. "최대 7배 빠름, 8배 효율" (경쟁 대비, 벤더 주장) | NVM 2 MB, RAM 512 KB | 초저전력 BLE SoC | Nordic Edge AI Lab, Neuton 모델 | BLE 센서, KWS, 소리 분류 | [6] |
| ST STM32N6 (2024) | Cortex-M55, 800 MHz | Neural-ART NPU (자체), 최대 1 GHz | 600 GOPS, "3 TOPS/W" (벤더) | SRAM 4.2 MB | 수십~수백 mW (추론) | ST Edge AI Suite, STM32Cube.AI | 비전, 오디오, 산업 | [5] |
| Alif Ensemble E4 (2025) | Cortex-M55 ×2 (400 MHz + 160 MHz) | Ethos-U85 + Ethos-U55 ×2 | U85 "up to 204 GOPS" (2차) | SRAM 9.75 MB (2차) | SLM 추론 시 "36 mW" (2차) | Vela, ExecuTorch 지원 발표(2차) | 음성·비전·소형 SLM | [7][8][53] |
| Alif Balletto B1 | Cortex-M55 + Helium, 160 MHz | Ethos-U55 128 MAC | 미공개 (MAC 수만) | SRAM 2 MB, NVM 1.8 MB | BLE 5.3 + 802.15.4 SoC | Alif SDK + Vela | 연결형 센서, 스마트 안경 | [9] |
| Renesas RA8P1 (2025) | Cortex-M85 1 GHz + Cortex-M33 250 MHz | Ethos-U55, 256 MAC | 256 GOPS @ 500 MHz | MRAM 내장, TSMC 22ULL | 고성능 MCU | Renesas FSP + Vela (추론) | 비전·음성 AIoT | [10] |
| NXP MCX N94x | Cortex-M33 ×2, 150 MHz | eIQ Neutron NPU (자체) | 미공개. "CPU 대비 최대 42배" | Flash 2 MB | 저전력 MCU | MCUXpresso + eIQ | 산업·IoT 센싱 | [11] |
| NXP i.MX RT700 (2024) | M33 325 MHz + HiFi 4, M33 250 MHz + HiFi 1 | eIQ Neutron NPU | 미공개. "최대 172배 가속" | SRAM 최대 7.5 MB | 저전력 crossover MCU | MCUXpresso, eIQ, Zephyr 보드 지원 | 웨어러블, AR 안경, 히어러블 | [12][52] |
| Infineon PSOC Edge E84 (2024) | Cortex-M55 + Helium, Cortex-M33 | Ethos-U55 + NNLite(자체) | 미공개. "기존 Cortex-M 대비 480배" | 온칩 메모리 (수치는 SKU별) | 저전력 MCU | ModusToolbox (추론) | HMI, 스마트홈, 웨어러블 | [13] |
| Espressif ESP32-P4 | RISC-V ×2, 400 MHz + LP 코어 40 MHz | "AI instruction extensions" | 미공개 | SRAM 768 KB | 무선 없음 (동반 칩 필요) | ESP-IDF (ESP-DL은 확인 필요) | HMI, 카메라 | [14] |
| Espressif ESP32-S3 | Xtensa LX7 ×2, 240 MHz | 128-bit 벡터 명령 | 미공개 | SRAM 512 KB | Wi-Fi + BLE SoC | ESP-IDF | 음성 인식 장난감, IoT | [15] |
| Syntiant NDP120 (2021) | Cortex-M0 (관리용) + HiFi 3 DSP | Syntiant Core 2 | 미공개. 최대 7M 파라미터 | M0용 SRAM 48 KB | "1 mW" 예산 (벤더) | Syntiant SDK (확인 필요) | 다중 wake word, 이어버드 | [16] |
| Syntiant NDP250 (2024) | (관리용 코어 — 확인 필요) | Syntiant Core 3 | 30 GOPS | 확인 필요 | µW~mW (벤더 주장) | 확인 필요 | 비전·음성·센서 상시 감지 | [17] |
| GreenWaves GAP9 (2020) | RISC-V 클러스터 9코어 + FC | NE16 (int8/4/2) | "up to 150 GOPS" | 확인 필요 | 수십 mW (벤더 사례) | GreenWaves SDK (확인 필요) | 히어러블 신경망 노이즈 제거 | [18] |
| Synaptics SL2610 (2025) | Cortex-A55 ×1~2 + Cortex-M52 Helium | Torq T1 NPU + Google Coral NPU | T1 "1 TOPS" | 확인 필요 | 수백 mW~W (추론) | Torq, IREE/MLIR (오픈) | 멀티모달 IoT | [19][20] |

표 읽는 법: "미공개"는 나쁘다는 뜻이 아니다. 단지 이 칸에 벤더 숫자가 없다는 뜻이다. 전력 등급 칸의 "(추론)"은 이 노트가 칩 종류로 짐작한 것이다.

### 2.3 부품별로 짧게

**Ambiq (Apollo4 → Apollo5 → Apollo330).** Ambiq은 sub-threshold 설계로 µA/MHz 단위 전력을 내세우는 회사다. Apollo4 Plus는 Cortex-M4F 192 MHz에 "4 µA/MHz"를 주장한다 [3]. Apollo510(2024-03 발표)은 Cortex-M55 + Helium 250 MHz, NVM 4 MB, SRAM/TCM 3.75 MB이고, Apollo4 대비 "10x better latency, 약 2x 에너지 절감"을 주장한다 [1]. 눈여겨볼 것은 **NPU가 없다**는 점이다. Ambiq은 "NPU 없이 대부분의 AI를 한다"고 말한다 [2]. 2025-03에는 Apollo330 Plus(M55 + Helium, BLE 옵션)를 냈다 [4]. 2025-07에는 NYSE에 상장했다(티커 AMBQ) [51] — 공급 리스크를 볼 때 의미가 있다(5.4절). Hark 공고에 "Ambiq-class MCUs"라는 표현이 있었다는 점은 7절에서 다룬다.

**Nordic nRF54LM20B.** BLE 칩의 강자 Nordic이 2023년에 Atlazo를 인수해 얻은 Axon NPU를 처음 넣은 SoC다. 2026-01 발표, M33 128 MHz + RISC-V 코프로세서, NVM 2 MB, RAM 512 KB [6]. 성능은 "경쟁 대비 최대 7배 빠르고 8배 효율적"이라는 상대 주장만 있고 TOPS는 없다 [6]. 발표 당시 "2026년 2분기 초 광범위 출시 예정"이었으므로 지금 양산 상태는 **확인 필요**다.

**ST STM32N6.** ST의 첫 Cortex-M55이고 첫 자체 NPU(Neural-ART)다. M55 800 MHz, NPU 최대 1 GHz에서 600 GOPS, SRAM 4.2 MB, NPU 효율 "3 TOPS/W"(벤더) [5]. MIPI CSI·ISP·H.264 인코더가 있어서 **MCU급 비전**을 노린 칩이다. 웨어러블 음성 기기에는 다소 크다(추론).

**Alif Semiconductor (Ensemble, Balletto).** Ethos-U를 가장 적극적으로 쓴 MCU 회사다. 1세대 Ensemble(E1/E3/E5/E7)은 M55 + Ethos-U55, 2세대 E4/E6/E8(2025-01 발표)은 **Ethos-U85**를 처음 MCU에 넣었다 [7]. CNX 정리(2차)에 따르면 E4는 M55 두 개(400 MHz + 160 MHz), U85 "up to 204 GOPS", U55 두 개, SRAM 9.75 MB, SLM 추론 시 36 mW이고, E6/E8은 Cortex-A32를 더해 Linux를 돌린다 [8]. Balletto B1은 BLE/802.15.4 무선 + M55 + U55(128 MAC)이고 [9], Alif는 스마트 안경(Brilliant Labs Halo)에 쓰였다고 홍보한다. 그리고 2026-09-09, **Analog Devices가 Alif를 13.5억 달러(최대 2억 달러 추가 조건부)에 인수한다고 발표**했다(2026년 말 종결 예상) [50]. 이건 5.4절 "스타트업 벤더 리스크"의 좋은 실례다.

**Renesas RA8P1.** Cortex-M85 1 GHz + M33 250 MHz + Ethos-U55(256 MAC, 500 MHz에서 256 GOPS), TSMC 22ULL 공정, 내장 MRAM [10]. 2025-07 발표. Renesas RA8 계열은 M85(Helium)로 시작했고 RA8P1이 NPU를 더했다.

**NXP (MCX N, i.MX RT700).** NXP는 Arm NPU 대신 **자체 eIQ Neutron NPU**를 MCU부터 애플리케이션 프로세서까지 쓴다. MCX N94x는 M33 두 개(150 MHz) + Neutron, "CPU 대비 최대 42배" [11]. i.MX RT700(2024)은 웨어러블을 정면으로 겨냥한다 — M33 + HiFi 4(325 MHz), M33 + HiFi 1(250 MHz, 상시 감지용), Neutron NPU("최대 172배"), SRAM 최대 7.5 MB, 대상 "AR 안경, 히어러블, 스마트워치" [12]. HiFi 4/HiFi 1은 E4 5절의 Cadence Tensilica DSP다. Zephyr에 EVK 보드 지원이 있다 [52].

**Infineon PSOC Edge E81/E83/E84.** 2024-04 발표. 고성능 도메인은 M55 + Helium(+ E83/E84는 Ethos-U55), 저전력 도메인은 M33 + 자체 NNLite 가속기다 [13]. "두 도메인 구조"가 7절의 "SoC + 동반 MCU"를 한 칩 안에서 하는 셈이다.

**Espressif (ESP32-P4, ESP32-S3).** ESP32-P4는 RISC-V 두 개(400 MHz, AI 확장 명령) + LP 코어, SRAM 768 KB, 무선 없음 [14]. ESP32-S3은 Xtensa LX7 두 개(240 MHz)에 128-bit 벡터 명령, Wi-Fi + BLE [15]. Don은 SSD에서 Xtensa를 써 봤으니 S3의 툴체인은 친숙할 것이다. 다만 웨어러블 배터리 기준으로는 Wi-Fi SoC라 상시 전력이 크다(추론).

**Syntiant NDP.** "Neural Decision Processor"라는 이름 그대로, 상시 음성·센서 신경망만 돌리는 칩이다. NDP120(2021-01, Core 2)은 HiFi 3 DSP + 관리용 M0, 최대 7M 파라미터, 여러 네트워크를 "1 mW 예산"으로 돌린다고 주장한다 [16]. NDP250(2024-04, Core 3)은 30 GOPS [17]. 장점은 확실한 상시 저전력, 단점은 **칩이 하나 더 늘고**, 범용 펌웨어는 다른 MCU가 해야 한다는 점이다.

**GreenWaves GAP9.** RISC-V 9코어 클러스터 + NE16 신경망 엔진(int8/4/2), GlobalFoundries 22FDX, 최대 150 GOPS [18]. 히어러블(신경망 노이즈 제거, ANC)에 양산 적용됐다고 홍보한다 [18]. 오디오 품질이 핵심인 이어버드형 기기에서 의미가 있다.

**Synaptics Astra SL2610.** 2025-10 발표. Cortex-A55(1~2개) + Cortex-M52(Helium) + Torq T1 NPU(1 TOPS) + **Google Coral NPU**(RISC-V 기반 오픈 NPU의 첫 양산 구현) [19][20]. 컴파일러가 IREE/MLIR 오픈소스라는 점이 특이하다(F6 7절). A55가 있으므로 엄밀히는 MCU가 아니라 저전력 애플리케이션 프로세서다.

### 2.4 Hark 같은 웨어러블 관점 — 강점과 약점 (이 노트의 추론)

예를 들어 Hark 같은 웨어러블(음성 비서 + IMU 제스처 + SoC에서 SLM)이라면 동반 MCU에 요구되는 것은 이렇다고 가정한다: 마이크 2~4개 상시 VAD/KWS, IMU 제스처, 착용 감지를 수 mW 안에서, RTOS로, SoC를 최대한 재우면서.

| 갈래 | 이 요구에 대한 강점 | 약점·확인할 것 |
|---|---|---|
| ① Helium만 (Apollo5) | KWS·제스처 정도는 CPU로 충분(D4 숫자 참고), 툴 단순, 전력 경험 많음 | 모델이 커지면(예: 상시 ASR 일부) 여유가 작다 |
| ② Ethos-U MCU (Alif, RA8P1, PSOC) | 같은 Vela 흐름으로 여러 벤더 이식, NPU 덕에 burst가 짧다 | NPU idle·wake 전력, op 커버리지(C6), 메모리 배치 |
| ③ 자체 NPU (N6, Neutron, Axon) | 칩마다 효율 최적화 | 벤더 툴 종속, 2nd source 어려움 |
| ④ 전용 칩 (NDP, GAP9) | 상시 음성은 최저 전력 | BOM 한 칸 더, 펌웨어·툴 체인 하나 더 |

### 2.5 함정

- 이 세그먼트의 "AI 성능 X배"는 거의 모두 **"우리 CPU 대비"**다(42배, 172배, 480배). 기준 CPU가 서로 달라서 벤더끼리 비교할 수 없다.
- Ethos-U MCU라도 **NPU가 지원하지 않는 op는 CPU에서 돈다**(C6 CPU fallback). Vela 리포트로 확인해야 한다.
- 무선이 있는 칩(nRF54, Balletto, ESP32-S3)은 라디오 스택이 RAM과 CPU 시간을 먹는다. 모델용 SRAM을 계산할 때 빼야 한다(K1).

---

## 3. 웨어러블·AR 애플리케이션 SoC — 사장님 칩

### 3.1 직관

이 세그먼트는 Cortex-A 코어, LPDDR, GPU, ISP, 모뎀/Wi-Fi, 그리고 DSP/NPU를 한 칩(또는 칩 세트)에 넣는다. OS는 Wear OS, Android, Linux다. **SLM처럼 수백 MB~GB 가중치가 필요한 모델은 이 세그먼트에서만 돈다.** MCU의 SRAM은 MB 단위라 담을 수 없다(D2, L2).

### 3.2 비교 표

| 부품 | CPU | AI 블록 | 공개 TOPS · 정밀도 | 메모리 | 전력 등급 | SW 스택 | 대표 기기 | 출처 |
|---|---|---|---|---|---|---|---|---|
| Snapdragon W5+ Gen 1 (2022) | SoC 4 nm 쿼드코어 1.7 GHz + 동반 칩 QCC5100(22 nm, 250 MHz) | SoC: Dual Hexagon QDSP V66K · 동반 칩: "U55 ML core" + HiFi5 DSP | 미공개 | SoC: 1×16 LPDDR4 2133 MHz · 동반 칩: 8+ MB | 스마트워치 (동반 칩은 FreeRTOS) | Wear OS, AOSP, RTOS · QNN(F4) | Wear OS 스마트워치 | [21] |
| Snapdragon W5 Gen 2 (2025) | W5 Gen 1과 같은 4 nm, 같은 코어 (2차) | 성능 변화 없음 (2차) | 미공개 | — | 크기·전력 약 20% 감소 주장 (2차) | Wear OS | Pixel Watch 4 (2차) | [22] |
| Snapdragon Wear Elite (2026) | 3 nm, 최대 2.1 GHz (1×A78 + 4×A55는 2차) | Hexagon NPU + eNPU(상시 저전력) + Sensing Hub | "up to 12 TOPS" (2차) · 2B 파라미터 모델 온디바이스 (공식) | LPDDR5 "4.2 GHz", 16-bit | 스마트워치·안경 | Android, Zephyr, Wear OS, Linux | 차세대 Galaxy Watch (2차) | [23][24] |
| Snapdragon AR1 Gen 1 (2023) | Kryo 4코어 1.9 GHz | 3세대 Hexagon NPU (INT4, INT8) + Sensing Hub의 micro NPU (INT8) | 미공개 | LPDDR4x 최대 2100 MHz | 스마트 안경 | QNN(F4) | Ray-Ban Meta | [25] |
| Snapdragon AR1+ Gen 1 (2025) | (AR1 계열 — 세부 미공개) | Hexagon NPU | 미공개. Llama-3.2-1B 안경 위 실행 데모 | 미공개 | 패키지 26% 작고 전력 7% 감소 주장 | QNN(F4) | RayNeo X3 Pro (데모) | [26] |
| Snapdragon AR2 Gen 1 (2022) | 멀티칩 (AR 프로세서 + 코프로세서 + 연결 칩), 4 nm | AI "XR2 Gen 1 대비 2.5배" | 미공개 | 미공개 | "50% 적은 전력" 주장 | QNN(F4) | AR 안경 | [27] |
| NXP i.MX 93 (2021) | A55 ×1~2, 1.7 GHz + M33 250 MHz | Arm Ethos-U65 | 0.5 TOPS | LPDDR4/4X x16, 3.7 GT/s | 산업·IoT 저전력 AP | Linux BSP, eIQ, Vela | HMI, 산업 | [29][41] |
| NXP i.MX 95 (2023) | A55 ×6, 2.0 GHz + M7 + M33 | eIQ Neutron NPU | 2.0 TOPS | LPDDR5/4X x32, 6.4 GT/s · SRAM 1376 kB · 16 nm FinFET | 산업·차량 AP | Linux BSP, eIQ | 산업, 차량 | [30] |
| MediaTek Genio 510 / 700 / 1200 | A78 ×2 + A55 ×4 / A78 ×2 + A55 ×6 / A78 ×4 + A55 ×4 | MediaTek NPU (5세대: 510·700) | 3.2 / 4.0 / 4.8 TOPS · 6 nm | SKU별 (확인 필요) | IoT AP (W급, 추론) | Linux(Yocto)·Android, NeuroPilot(확인 필요) | 스마트 디스플레이, 산업 | [31] |
| Samsung Exynos W1000 (2024) | A78 1.6 GHz + A55 ×4 1.5 GHz, 3 nm | NPU 정보 확인 필요 | 미공개 | ePoP, LPDDR5 + 32 GB 저장장치 (2차) | 스마트워치 | Wear OS | Galaxy Watch7 | [32] |
| Apple S10 (참고) | 64-bit 듀얼코어 | 4코어 Neural Engine | 미공개 | 미공개 | 스마트워치 | Core ML (F6) — 외부 구매 불가 | Apple Watch Series 10 | [33] |
| Google Tensor G5 (참고) | 8코어 (1 + 5 + 2), TSMC 3 nm | TPU "최대 60% 강화" | 미공개 | LPDDR5X (2차) | 폰 | Gemini Nano, LiteRT — 외부 구매 불가 | Pixel 10 | [34] |

### 3.3 Qualcomm 웨어러블·AR 라인 — 계보로 이해하기

Qualcomm은 이 세그먼트에서 가장 넓은 라인을 가진다. 계보로 보면 **"SoC + 저전력 섬"** 이라는 같은 생각이 반복된다.

```
2022  W5+ Gen 1   4nm SoC (Hexagon DSP) + 22nm QCC5100 동반 칩 (U55 ML core + HiFi5, FreeRTOS)  [21]
2023  AR1 Gen 1   Hexagon NPU(3세대) + Sensing Hub 안의 micro NPU (INT8)                      [25]
2025  AR1+ Gen 1  패키지 26%↓, 전력 7%↓, 1B SLM 안경 위 데모                                  [26]
2025  W5 Gen 2    같은 칩, 위성 통신·위치 정확도 개선                                         [22]
2026  Wear Elite  3nm, Hexagon NPU + eNPU + 동반 프로세서(최대 500 MHz), 2B 모델 온디바이스  [23][24]
```

W5+ Gen 1의 product brief는 놀랍도록 교과서적이다. 4 nm SoC 옆에 **22 nm, 250 MHz 동반 칩(QCC5100)**이 있고, 그 칩에 "U55 Machine Learning core"(Arm Ethos-U55로 보인다 — brief는 "U55"라고만 적는다)와 HiFi5 DSP가 들어 있으며, **FreeRTOS**를 돌린다 [21]. 즉 Qualcomm 자신이 "큰 SoC + Ethos-U급 MCU" 구조를 웨어러블 표준 구성으로 판다. 그리고 Wear Elite(2026-03)에서는 "eNPU"라는 상시 저전력 AI 가속기와 동반 프로세서를 플랫폼 안에 두고, "keyword detection, activity recognition"을 SoC를 깨우지 않고 한다고 설명한다(2차) [24]. 이것은 **외부 동반 MCU가 하던 일을 SoC 플랫폼이 흡수하는 흐름**이다. 7절의 결정에 직접 영향을 준다.

Wear Elite brief는 OS로 "Android, Zephyr, Wear OS, Linux"를 나열한다 [23]. Zephyr가 공식 목록에 들어간 점은 펌웨어 엔지니어에게 의미가 있다 — 동반 프로세서 쪽 RTOS일 가능성이 높다(추론, 확인 필요).

### 3.4 Hexagon NPU 세대 — 높은 수준에서만

Hexagon은 원래 VLIW DSP였고(E4 6절), 여기에 벡터(HVX)와 행렬(HMX) 확장이 붙으며 "Hexagon NPU"라는 이름이 됐다. 이 노트에서 확인한 범위는 이렇다.

| 칩 | 문서의 표현 | 의미 |
|---|---|---|
| W5+ Gen 1 | "Dual Hexagon QDSP V66K" [21] | DSP로 표기. ML 전용 행렬 유닛 언급 없음 |
| AR1 Gen 1 | "3rd Generation Hexagon NPU", "scalar, vector, and tensor accelerators", INT4/INT8 [25] | scalar·vector·tensor 세 단 구성 (E4의 scalar/HVX/HMX와 같은 그림) |
| Snapdragon X Elite (PC, 참고) | Hexagon NPU 45 TOPS [28] | 같은 계열이 PC에서는 수십 TOPS급까지 커진다 |
| Wear Elite | Hexagon NPU + eNPU, "up to 12 TOPS"(2차) [23][24] | 워치급에서 2B 모델을 노린다 |

세대별 TOPS 표를 만들지 않는 이유: 폰 칩의 NPU TOPS는 Qualcomm이 최근 공식 숫자로 거의 내지 않고 "X% 빠르다"로만 말한다. 확인되지 않은 숫자를 표에 넣지 않는다.

### 3.5 코드로 확인 — "2B 모델, 최대 10 tok/s"를 대역폭으로 검산 (예제 3)

9to5Google(2차)은 Wear Elite가 2B 파라미터 모델을 "up to 10 tokens per second"로 돌린다고 전했다 [24]. D5에서 배운 대로 decode는 **메모리 대역폭에 묶인다**: 토큰 하나마다 가중치 전체를 DRAM에서 한 번 읽는다.

```
tok/s 상한 ≈ DRAM 대역폭 / 가중치 바이트
```

말로 하면, 1초에 읽을 수 있는 바이트를 토큰 하나에 필요한 바이트로 나누면 1초에 만들 수 있는 토큰 수의 천장이 나온다.

brief는 메모리를 "LPDDR5, 4.2 GHz, 16-bit"라고만 적는다 [23]. "4.2 GHz"를 4200 MT/s로 **가정**하면 대역폭은 4200×10⁶ × 16 bit / 8 = 8.4 GB/s다.

```python
# 예제 3: "2B 모델 up to 10 tok/s" 같은 주장을 대역폭으로 검산한다 (D5의 decode 상한)
# decode 1 token ≈ weight 전체를 DRAM에서 한 번 읽는다 → tok/s 상한 = BW / weight_bytes
def bw_GBps(mtps, bus_bits):            # MT/s × bus width
    return mtps * 1e6 * bus_bits / 8 / 1e9
bw = bw_GBps(4200, 16)                  # 가정: brief의 "LPDDR5 4.2 GHz, 16-bit"를 4200 MT/s로 해석
print(f"가정 대역폭 = {bw:.1f} GB/s")
for params in (1.0e9, 2.0e9):
    for bits in (8, 4, 3):
        wbytes = params * bits / 8
        print(f"{params/1e9:.0f}B params, {bits}-bit weights: {wbytes/1e9:.2f} GB -> "
              f"상한 {bw*1e9/wbytes:5.1f} tok/s")
```

```text
가정 대역폭 = 8.4 GB/s
1B params, 8-bit weights: 1.00 GB -> 상한   8.4 tok/s
1B params, 4-bit weights: 0.50 GB -> 상한  16.8 tok/s
1B params, 3-bit weights: 0.38 GB -> 상한  22.4 tok/s
2B params, 8-bit weights: 2.00 GB -> 상한   4.2 tok/s
2B params, 4-bit weights: 1.00 GB -> 상한   8.4 tok/s
2B params, 3-bit weights: 0.75 GB -> 상한  11.2 tok/s
```

출력에서 볼 것: 이 가정 아래에서 2B 모델을 4-bit로 두면 상한은 8.4 tok/s다. "최대 10 tok/s"가 나오려면 가중치가 4-bit보다 작거나(3-bit 혼합 등), 메모리 해석이 다르거나(예: 실제 대역폭이 더 크다), 측정 조건이 다를 것이다. 어느 쪽인지는 **확인 필요**다. 면접에서는 "벤더 tok/s 숫자를 들으면 먼저 대역폭으로 상한을 계산해 본다"는 습관을 보여 주면 된다(M4).

### 3.6 그 밖의 SoC 벤더

**NXP i.MX 93/95.** i.MX 93은 **Ethos-U65**를 처음 넣은 애플리케이션 프로세서다(0.5 TOPS, A55 ×1~2, M33) [29]. NXP 보도자료(2021-11-09)는 이 NPU가 "256 MACs/cycle"이라고 적었다 [29]. NPU 클럭을 1 GHz로 가정하면 256 × 2 × 1 GHz ≈ 0.51 TOPS로 0.5 TOPS와 맞는다(Ethos-U65 구성표는 [41]). i.MX 95는 자체 eIQ Neutron(2 TOPS)으로 바꿨다 [30]. 두 칩 모두 산업·차량 지향이라 웨어러블에는 크고(패키지, 전력), 대신 **Linux BSP가 성숙**하다. 시제품용 개발 보드로는 좋은 선택이다(추론).

**MediaTek.** 웨어러블 전용 라인보다는 IoT용 **Genio**(510/700/1200: 3.2/4.0/4.8 TOPS, 6 nm)가 공개 자료가 풍부하다 [31]. 폰용 Dimensity는 NPU가 크지만 폰 전력 등급이라 웨어러블 후보로는 짧게만 언급한다. MediaTek NPU 툴체인 이름(NeuroPilot)은 이번에 공식 자료로 확인하지 못했다.

**Samsung Exynos W.** W1000(2024-07)은 Samsung의 첫 3 nm 칩, A78 1개 + A55 4개, Galaxy Watch7용 [32]. 외부 판매와 NPU 세부는 확인 필요. Wear Elite가 "차세대 Galaxy Watch"에 들어간다는 보도(2차)는 [24] — 같은 회사 안에서도 칩이 바뀔 수 있다는 예다.

**Apple, Google (참고만).** 구매할 수 없지만 "목표 수준"을 보여 준다. Apple S10은 4코어 Neural Engine으로 double tap 제스처, 온디바이스 Siri, 받아쓰기, 낙상 감지를 한다 [33]. 즉 **워치급 칩에서 제스처 + 음성이 이미 온디바이스**다. Google Tensor G5(TSMC 3 nm)는 TPU가 "최대 60% 강화", Gemini Nano가 "2.6배 빠르다"고 발표했다 [34]. 둘 다 수직 통합의 장점(모델·칩·OS를 같이 설계)을 보여 주는 사례다.

### 3.7 Hark 같은 웨어러블 관점 (이 노트의 추론)

| 후보 | 강점 | 약점 |
|---|---|---|
| Wear Elite급 워치 SoC | 2B 모델 온디바이스를 공식 목표로 함, eNPU로 상시 감지 흡수, 셀룰러·UWB 등 연결 통합 | 2026년 신제품 — 툴·BSP 성숙도, 물량 배정, 장기 공급 확인 필요 |
| AR1/AR1+급 안경 SoC | 카메라·ISP, 안경 폼팩터에서 1B SLM 데모 | 화면 없는 음성 기기에는 ISP·디스플레이 블록이 남는다 |
| W5 Gen 1/2 | 검증된 양산 플랫폼, 동반 칩 구조 | NPU 세대가 오래됐다 — SLM에는 부족할 가능성 |
| i.MX 93/95 | 오픈 Linux BSP, 문서 공개 | 웨어러블 전력·크기에 맞지 않을 수 있다 |
| SL2610 | 오픈 컴파일러(IREE), Helium M52 내장 | 신생 라인, 셀룰러 없음 |

### 3.8 함정

- **"Hexagon"이라는 단어만 보고 같은 능력이라고 가정하지 말 것.** W5 Gen 1의 Hexagon DSP와 AR1의 Hexagon NPU는 세대가 다르다.
- **brief의 메모리 표기("2133 MHz", "4.2 GHz")는 클럭인지 전송률인지 모호하다.** 대역폭을 계산할 때는 가정을 적고, 데이터시트로 확인한다.
- **Qualcomm 플랫폼은 SDK 접근에 계약이 필요하다**(F4 0.4절). 평가 일정에 계정·NDA 시간을 넣는다.

---

## 4. Edge 가속기 — 웨어러블보다는 로봇과 홈 허브

### 4.1 직관

이 세그먼트는 "호스트 CPU 옆에 꽂는 NPU" 또는 "GPU가 큰 모듈"이다. 연산은 크지만 전력이 W 단위라서 **배터리 300 mAh급 웨어러블에는 들어가지 않는다**(E9, I1 예산). 그래도 알아야 하는 이유는 두 가지다. 첫째, Hark 같은 회사가 웨어러블 다음에 홈 기기나 로봇 쪽으로 갈 수 있다. 둘째, 웨어러블의 **데이터 수집·평가 리그**나 데모 기기에 쓰인다.

### 4.2 비교 표

| 부품 | 호스트 연결 | AI 블록 | 공개 TOPS · 정밀도 | 메모리 | 전력 | SW 스택 | 대표 용도 | 출처 |
|---|---|---|---|---|---|---|---|---|
| Google Coral Edge TPU | PCIe Gen2 x1 / USB 2.0 모듈 | Edge TPU | 4 TOPS (int8), "2 TOPS per watt" | 온칩만 (외부 DRAM 없음) | 약 2 W | LiteRT(TFLite) + Edge TPU 컴파일러 | 카메라 분류·검출 | [37] |
| Hailo-8L | M.2 등 | Hailo 1세대 dataflow | 13 TOPS (int8) | 온칩 | 확인 필요 | Hailo SDK (Dataflow Compiler — 확인 필요) | Raspberry Pi AI Kit류 비전 | [35] |
| Hailo-8 | M.2 등 | Hailo 1세대 | 26 TOPS (int8) | 온칩 | 확인 필요 | Hailo SDK | 다채널 비전 | [35] |
| Hailo-10H (2024) | M.2, x86·ARM 호스트 | Hailo 2세대 | 40 TOPS (int4) / 20 TOPS (int8) | 직접 DDR 인터페이스, LPDDR4/4X | "2.5 W (typical)" | Hailo SDK, Linux·Windows·Android | 생성형 AI(LLM·VLM) | [36] |
| NVIDIA Jetson Orin Nano Super (2024) | 독립 모듈 (Cortex-A78AE ×6) | Ampere GPU + tensor core | 67 TOPS (int8 sparse) | 8 GB, 102 GB/s | 최대 25 W 모드 | JetPack 6.2, CUDA, TensorRT | 로봇, 비전, 소형 LLM | [38] |
| Axelera Metis | M.2 / PCIe | 디지털 in-memory compute 4코어 | 214 TOPS | 칩당 최대 16 GB | "4–8 W average" | Voyager SDK (PyTorch, ONNX, TF) | 다채널 비전 | [39] |
| Axelera Europa | PCIe 등 | 2세대 8코어 | 629 TOPS | 칩당 최대 64 GB | "45 W average" | Voyager SDK | 엣지 서버 | [39] |

(Coral 숫자는 Coral Accelerator Module 데이터시트 v1.4 사본에서 확인했다. Coral 사이트는 현재 Google Developers의 Coral 페이지로 옮겨졌고, Google은 2025-10에 별도의 **Coral NPU**(RISC-V 기반 오픈 IP, 5절)를 발표했다 [20]. 둘은 다른 것이다.)

### 4.3 부품별로 짧게

**Coral Edge TPU.** 2 W에 4 TOPS(int8)라는 숫자로 "작은 USB 막대 하나로 MobileNet을 실시간으로"라는 시대를 연 칩이다 [37]. 외부 DRAM이 없어서 모델이 온칩 SRAM에 다 들어가야 빠르고, 넘치면 호스트 메모리에서 가중치를 스트리밍하며 느려진다(E5 5절의 SRAM·tiling 문제). 연산자 지원도 int8 TFLite의 일부만이다. 지금 새 설계에 넣기보다는 **"SRAM에 안 들어가면 어떻게 되는가"를 보여 주는 교재**로 의미가 크다.

**Hailo.** Hailo-8(26 TOPS)과 8L(13 TOPS)은 외부 DRAM 없는 dataflow 구조로 비전 CNN에 강하다 [35]. Hailo-10H는 **직접 DDR 인터페이스**를 추가해 LLM·VLM을 노린다 — 40 TOPS는 int4이고 int8로는 20 TOPS, typical 2.5 W [36]. 이것이 M4의 대표적인 함정 사례다: 헤드라인 숫자가 int4다.

**NVIDIA Jetson Orin Nano Super.** 2024-12에 JetPack 6.2 업데이트로 기존 보드를 "Super" 모드로 올렸다 — 67 TOPS(sparse), 102 GB/s, 25 W 모드 [38]. 67 TOPS는 **sparse**(구조적 희소성 2:4) 기준이라 dense int8로는 그 절반 수준으로 봐야 한다(M4, 정확한 dense 값은 확인 필요). GPU 기반이라 batch=1 상시 추론에 비효율적인 이유는 E6 5절. 대신 CUDA·TensorRT 생태계가 압도적이라 **로봇 프로토타입의 기준점**이다.

**Axelera Metis/Europa.** 유럽 스타트업. Metis는 214 TOPS를 4~8 W 평균으로 주장하고, LLM은 칩당 8B 파라미터까지라고 한다 [39]. 숫자는 크지만 웨어러블과는 무관하고, 스타트업 툴체인 리스크(5.4절)를 이야기할 때의 사례다.

### 4.4 코드로 확인 — 공개된 두 숫자로만 파생 지표 만들기 (예제 2)

TOPS/W는 TOPS와 전력이 **둘 다** 공개된 경우에만, byte/op(대역폭 ÷ 연산량)는 TOPS와 메모리 대역폭이 **둘 다** 공개된 경우에만 계산한다. byte/op의 역수가 D3에서 배운 roofline의 **ridge point**(op/byte)다 — 모델의 arithmetic intensity가 이 값보다 작으면 메모리에 묶인다.

i.MX 93/95의 대역폭은 NXP 페이지의 "3.7 GT/s x16", "6.4 GT/s x32"에서 직접 계산했다: 전송률 × 버스 폭(바이트).

```
i.MX 93:  3.7×10⁹ × 16/8 = 7.4 GB/s
i.MX 95:  6.4×10⁹ × 32/8 = 25.6 GB/s
```

```python
# 예제 2: 공개된 숫자 두 개가 다 있을 때만 파생 지표를 만든다 (TOPS/W, byte/op)
import pandas as pd
d = pd.DataFrame([
  # part, peak TOPS, 조건, 공개 전력 W, 공개 DRAM 대역폭 GB/s (None=미공개)
  ("Coral Edge TPU",         4.0, "int8",        2.0,  None),
  ("Hailo-10H",             20.0, "int8",        2.5,  None),
  ("Hailo-10H",             40.0, "int4",        2.5,  None),
  ("Jetson Orin Nano Super",67.0, "int8 sparse", 25.0, 102.0),
  ("NXP i.MX 93",            0.5, "int8",        None, 3.7e9*2/1e9),   # LPDDR4X 3.7 GT/s x16
  ("NXP i.MX 95",            2.0, "int8",        None, 6.4e9*4/1e9),   # LPDDR5 6.4 GT/s x32
], columns=["part","TOPS","cond","W","GBps"])
d["TOPS/W (derived)"] = d.TOPS / d.W
d["byte/op (derived)"] = d.GBps * 1e9 / (d.TOPS * 1e12)
d["ridge op/byte"]     = 1 / d["byte/op (derived)"]
pd.set_option("display.width", 120)
print(d.round(4).to_string(index=False))
```

```text
                  part  TOPS        cond    W  GBps  TOPS/W (derived)  byte/op (derived)  ridge op/byte
        Coral Edge TPU   4.0        int8  2.0   NaN              2.00                NaN            NaN
             Hailo-10H  20.0        int8  2.5   NaN              8.00                NaN            NaN
             Hailo-10H  40.0        int4  2.5   NaN             16.00                NaN            NaN
Jetson Orin Nano Super  67.0 int8 sparse 25.0 102.0              2.68             0.0015       656.8627
           NXP i.MX 93   0.5        int8  NaN   7.4               NaN             0.0148        67.5676
           NXP i.MX 95   2.0        int8  NaN  25.6               NaN             0.0128        78.1250
```

출력에서 볼 것: (1) Hailo-10H는 같은 칩인데 int8이냐 int4냐에 따라 TOPS/W가 8 ↔ 16으로 두 배 차이 난다 — 정밀도를 맞추지 않은 비교는 무의미하다. (2) Orin Nano Super의 ridge는 약 657 op/byte로, int8 행렬곱이 아닌 대부분의 batch=1 연산(특히 LLM decode, intensity ≈ 1~2)은 대역폭에 묶인다. (3) i.MX 93/95의 ridge는 68~78 op/byte로 Orin보다 훨씬 낮다 — 연산 대비 메모리가 상대적으로 넉넉하다는 뜻이다. 모든 값은 peak끼리 나눈 **derived** 값이고, 실제 유효 대역폭은 더 낮다(E7).

### 4.5 Hark 관점 (이 노트의 추론)

웨어러블 본체에는 들어가지 않는다. 다만 (a) 데이터 수집 리그나 평가 서버 대신 "기기 근처 엣지 허브"로 SLM을 돌리는 시나리오, (b) 미래의 홈 기기나 로봇 제품, (c) 데모 기기에서 후보가 된다. 웨어러블과 같은 모델을 쓰려면 **양쪽 툴체인에서 같은 양자화 결과를 재현**해야 한다는 점을 기억한다(C8).

---

## 5. 라이선스 IP — 커스텀 실리콘을 만든다면

### 5.1 직관 — 설계도를 사는 것

지금까지의 칩은 완성품을 사는 것이었다. 라이선스 IP는 **NPU/DSP의 설계도(RTL)를 사서 자기 SoC에 넣는 것**이다. Apple, Google처럼 자기 칩을 만드는 회사, 또는 칩 회사(Alif가 Ethos-U를, Qualcomm이 W5+ 동반 칩에 "U55"를 넣은 것처럼)가 고객이다. Don이 SSD 컨트롤러에서 Cortex-R 코어를 Arm에서 라이선스한 것과 같은 구조다.

### 5.2 비교 표

| IP | 회사 | 종류 | 공개 성능 범위 | 정밀도·특징 | SW 스택 | 대표 사용처 | 출처 |
|---|---|---|---|---|---|---|---|
| Ethos-U55 | Arm | micro-NPU | 32/64/128/256 MAC/cycle (500 MHz에서 32~256 GOPS) | int8/int16, Cortex-M 짝 | Vela 컴파일러 + TFLM | Alif, RA8P1, PSOC Edge, QCC5100("U55") | [41][42] |
| Ethos-U65 | Arm | micro-NPU | 256/512 MAC, 1 GHz에서 최대 1 TOPS | Cortex-A/M 시스템용 | Vela | NXP i.MX 93 | [41] |
| Ethos-U85 (2024) | Arm | micro-NPU | 128~2048 MAC, 1 GHz에서 4 TOPS | transformer 지원 | Vela, TFLite, PyTorch(ExecuTorch) | Alif E4/E6/E8 | [40] |
| Ethos-N78 (2020) | Arm | 모바일 NPU | 1~10 TOPS (2차) | 드라이버 스택 공개 (Linux) | Arm NN | 상태 확인 필요 | [43] |
| Neo NPU (2023) | Cadence | NPU | 단일 코어 8 GOPS ~ 80 TOPS | 다중 코어로 수백 TOPS | NeuroWeave SDK | 히어러블~ADAS (벤더 대상) | [44] |
| HiFi · Vision DSP | Cadence (Tensilica) | 오디오·비전 DSP | — | Xtensa 기반, TIE 확장 (E4 5절) | Xtensa 툴체인 | QCC5100(HiFi5), RT700(HiFi4/1), NDP120(HiFi3) | [21][12][16] |
| ARC NPX6 (2022) | Synopsys | NPU | 1K~96K MAC, 단일 250+ TOPS, 8클러스터 3,500 TOPS(sparse) | "30 TOPS/W" (벤더) | MetaWare MX | 차량·소비자·데이터센터 | [45] |
| NeuPro-Nano | Ceva | tinyML NPU | 코어당 10~200 GOPS | 상시 오디오·음성·센싱 | Ceva SDK (확인 필요) | 히어러블, 웨어러블 (벤더 대상) | [46] |
| E-Series GPU (2025) | Imagination | GPU + Neural Core | 2~200 TOPS (int8/fp8) | 그래픽과 AI를 한 IP에 | 확인 필요 | 엣지 기기 (2025 가을 첫 제품 예정이었음) | [47] |
| Origin Evolution (2025) | Expedera | NPU | 확인 필요 | LLM·CNN·RNN 겨냥 | Expedera SDK | 확인 필요 | [48] |
| Chimera GPNPU | Quadric | 프로그래머블 NPU | 벤더 주장 범위는 확인 필요 | C++로 전부 프로그래밍, 새 op를 재설계 없이 | Quadric SDK | Kyocera 라이선스 (2025) | [49] |
| Coral NPU (2025) | Google (오픈소스) | RISC-V 기반 NPU | 기본 설계 약 512 GOPS, "수 mW" | scalar + RVV vector + matrix | IREE, TFLM | Synaptics SL2610 | [20][19] |

### 5.3 몇 가지 짚을 점

**Ethos-U는 사실상 MCU NPU의 표준이 되었다.** 같은 Vela 컴파일러가 Alif, Renesas, Infineon, NXP i.MX 93, 그리고 Qualcomm 동반 칩에까지 쓰인다 [21][41][42]. 이식성 면에서 가장 큰 장점이다. Ethos-U85는 transformer 연산을 NPU에서 돌리는 것을 목표로 나왔다 [40].

**Ethos-N(모바일 NPU)의 상태.** Ethos-N78은 2020년 발표, 1~10 TOPS 범위였다(2차) [43]. GitHub의 Ethos-N 드라이버 스택은 2024년 말 태그(24.10)까지 보인다 [43]. 그 뒤 후속 Ethos-N 제품이나 공식 단종 여부는 이번에 **확인하지 못했다**. 면접에서는 "Arm의 모바일 NPU 라인은 상태를 확인해야 한다"고 말하는 정도가 안전하다.

**Cadence HiFi는 이미 여러 칩 안에 있다.** QCC5100의 HiFi5 [21], RT700의 HiFi4/HiFi1 [12], NDP120의 HiFi3 [16]. Don이 Xtensa를 써 봤다는 경험은 "웨어러블 오디오 DSP 툴체인을 안다"로 이어진다(E4 5.4절).

**오픈 하드웨어의 등장.** Google Coral NPU는 RISC-V 스칼라 + RVV 벡터 + 행렬 유닛 구조이고 컴파일러는 IREE/TFLM이다 [20]. Synaptics가 첫 양산 구현을 했다 [19]. 라이선스비 없는 IP라는 점에서 커스텀 실리콘 비용 구조를 바꿀 수 있지만, 성숙도는 아직 지켜볼 단계다(추론).

### 5.4 스타트업 NPU 벤더의 리스크 — 실례로

Don은 SSD 쪽에서 "공급사가 망하거나 인수되면 2nd source가 없는 부품은 어떻게 되나"를 겪어 봤을 것이다. edge AI는 이 리스크가 특히 크다. 이 노트를 쓰는 동안 확인한 실례만 해도:

- **인수:** Analog Devices가 Alif를 인수한다고 2026-09-09 발표했다 [50]. 인수는 보통 제품 지원을 이어 가지만(Alif는 "고객과 파트너 지원에 전념"한다고 밝혔다), 로드맵·가격·툴체인 방향이 바뀔 수 있다. Nordic도 2023년에 Atlazo(Axon)를 인수했다 [6].
- **상장:** Ambiq은 2025-07 NYSE에 상장했다(공모 9,600만 달러) [51]. 상장은 재무 투명성을 높이지만, 작은 회사라는 점은 같다.
- **전략 전환:** Imagination은 별도 NNA 대신 GPU 안에 AI를 넣는 E-Series로 방향을 틀었다(2차) [47].

체크리스트로 정리하면:

| 리스크 | 증상 | 완화 |
|---|---|---|
| 회사 존속·인수 | 로드맵 변경, 지원 인력 이동 | 표준 IP(Ethos-U) 기반 칩 선호, 모델을 표준 포맷(TFLite/ONNX)으로 유지 |
| 툴체인 미성숙 | op 미지원, 컴파일러 버그, 수치 불일치 | 초기 평가에서 우리 모델로 커버리지 측정(C6, C8), 버그 대응 SLA 확인 |
| 단일 공급 | 물량·가격 협상력 약함 | 2nd source 가능한 아키텍처(같은 IP를 쓰는 다른 칩) |
| 문서 비공개 | NDA 없이는 레지스터·전력 상태를 못 봄 | 평가 일정에 NDA·계정 시간 포함 |
| 장기 공급 | 웨어러블 수명 주기 동안 단종 | longevity 프로그램 확인 |

### 5.5 언제 커스텀 NPU를 고려하나 (이 노트의 추론)

커스텀 실리콘은 수년과 큰 비용이 든다. 고려할 만한 조건은 대략 이렇다.

1. 물량이 매우 크다(수백만 대 단위). NRE를 나눠 낼 수 있다.
2. 상용 칩이 **에너지 예산을 구조적으로 못 맞춘다** — 예: 상시 ASR을 1 mW 아래에서 해야 하는데 시장 칩은 다 수 mW.
3. 자체 모델 구조가 안정되어 있고, 그 구조에 맞춘 가속이 큰 이득을 준다(I2, I6 co-design).
4. 하드웨어 팀과 컴파일러 팀을 꾸릴 수 있다. NPU IP를 사도 **컴파일러·런타임·검증**은 자기 몫이다.

Apple(S10 Neural Engine) [33]과 Google(Tensor TPU) [34]은 이 조건을 다 갖춘 경우다. 스타트업 1세대 제품은 보통 상용 SoC로 시작하고, 2~3세대에서 커스텀을 검토한다(추론).

---

## 6. 전체를 한 장에 — 차트로 비교하기

### 6.1 공개 TOPS vs 전력 등급 산점도 (예제 4)

검증된 점만 모아서 그린다. 먼저 데이터 모듈이다. `ref`는 참고 자료 번호, `pclass`는 이 노트가 정한 전력 등급이다(측정값이 아니다).

```python
# M3 vendor landscape — verified specs (확인일 2026-10-03). ref = 노트의 참고 자료 번호
# tops: vendor-published peak, int8 unless noted. None = 공개 수치 없음
SPECS = [
 # name, segment, tops, precision, power_class, ref, year
 dict(name="Syntiant NDP250", seg="MCU/AON", tops=0.030, prec="int8?", pclass=1, ref=17, year=2024),
 dict(name="GreenWaves GAP9", seg="MCU/AON", tops=0.150, prec="int8/4/2", pclass=2, ref=18, year=2020),
 dict(name="Alif E4 (U85)", seg="MCU/AON", tops=0.204, prec="int8", pclass=2, ref=8, year=2025),
 dict(name="Renesas RA8P1", seg="MCU/AON", tops=0.256, prec="int8", pclass=2, ref=10, year=2025),
 dict(name="STM32N6", seg="MCU/AON", tops=0.600, prec="int8", pclass=2, ref=5, year=2024),
 dict(name="NXP i.MX 93", seg="App SoC", tops=0.5, prec="int8", pclass=3, ref=29, year=2021),
 dict(name="Synaptics SL2610", seg="App SoC", tops=1.0, prec="int8/16,bf16", pclass=3, ref=19, year=2025),
 dict(name="NXP i.MX 95", seg="App SoC", tops=2.0, prec="int8", pclass=3, ref=30, year=2023),
 dict(name="MediaTek Genio 700", seg="App SoC", tops=4.0, prec="?", pclass=4, ref=31, year=None),
 dict(name="SD Wear Elite", seg="App SoC", tops=12.0, prec="?", pclass=3, ref=24, year=2026),
 dict(name="Coral Edge TPU", seg="Accel", tops=4.0, prec="int8", pclass=4, ref=37, year=None),
 dict(name="Hailo-8L", seg="Accel", tops=13.0, prec="int8", pclass=4, ref=35, year=None),
 dict(name="Hailo-10H", seg="Accel", tops=20.0, prec="int8 (40@int4)", pclass=4, ref=36, year=2024),
 dict(name="Hailo-8", seg="Accel", tops=26.0, prec="int8", pclass=4, ref=35, year=None),
 dict(name="Orin Nano Super", seg="Accel", tops=67.0, prec="int8 sparse", pclass=5, ref=38, year=2024),
 dict(name="Axelera Metis", seg="Accel", tops=214.0, prec="int8", pclass=5, ref=39, year=None),
]
# power class (이 노트의 분류, 측정값 아님): 1=≤1 mW AON, 2=1~100 mW MCU active, 3=0.1~2 W wearable/AR SoC,
# 4=2~5 W module/accel, 5=5~25 W edge box
PCLASS = {1:"≤1 mW", 2:"1–100 mW", 3:"0.1–2 W", 4:"2–5 W", 5:"5–25 W"}
```

이 데이터로 SVG를 만드는 코드다. 점에는 번호를 붙이고 오른쪽에 범례를 단다(이름이 겹치지 않게).

```python
# 예제 4: 검증된 점만으로 "전력 등급 vs 공개 TOPS" 산점도 SVG를 만든다 (log y, 번호 + 범례)
import math
from specs import SPECS, PCLASS
W, H, L, PR, T, B = 680, 420, 60, 470, 30, 60       # PR = 그림 영역 오른쪽 끝, 그 오른쪽은 범례
ys = lambda t: T + (H-T-B) * (1 - (math.log10(t) + 2) / 5)        # 0.01..1000 TOPS
def xs(c, k, n):
    cw = (PR-L) / 5
    return L + cw*(c-1) + cw*(k+1)/(n+1)
col = {"MCU/AON": "#4a7bd0", "App SoC": "#3f9a6b", "Accel": "#e08a3c"}
o = [f'<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg">',
     f'<line x1="{L}" y1="{H-B}" x2="{PR}" y2="{H-B}" stroke="currentColor"/>',
     f'<line x1="{L}" y1="{T}" x2="{L}" y2="{H-B}" stroke="currentColor"/>']
for e in range(-2, 4):
    y = ys(10**e)
    o += [f'<line x1="{L}" y1="{y:.1f}" x2="{PR}" y2="{y:.1f}" stroke="#888" stroke-opacity="0.3"/>',
          f'<text x="{L-6}" y="{y+4:.1f}" font-size="12" text-anchor="end">{10**e:g}</text>']
for c, lab in PCLASS.items():
    o.append(f'<text x="{xs(c,0,1):.1f}" y="{H-B+18}" font-size="12" text-anchor="middle">{lab}</text>')
num, legend = 0, []
for c in PCLASS:
    pts = sorted([s for s in SPECS if s["pclass"] == c], key=lambda s: s["tops"])
    for k, s in enumerate(pts):
        num += 1
        x, y = xs(c, k, len(pts)), ys(s["tops"])
        o += [f'<circle cx="{x:.1f}" cy="{y:.1f}" r="7" fill="{col[s["seg"]]}" fill-opacity="0.8"/>',
              f'<text x="{x:.1f}" y="{y-10:.1f}" font-size="11" text-anchor="middle">{num}</text>']
        legend.append(f'<text x="{PR+14}" y="{T+6+num*15}" font-size="11">{num}. {s["name"]} {s["tops"]:g} [{s["ref"]}]</text>')
o += legend
o.append(f'<text x="{(L+PR)/2}" y="{H-14}" font-size="13" text-anchor="middle">전력 등급 (이 노트의 분류, 측정값 아님)</text>')
o.append(f'<text x="16" y="{(T+H-B)/2}" font-size="13" text-anchor="middle" transform="rotate(-90 16 {(T+H-B)/2})">공개 peak TOPS (log)</text>')
o.append('</svg>')
open("scatter.svg", "w").write("\n".join(o))
print("points:", num, "| legend lines:", len(legend))
```

```text
points: 16 | legend lines: 16
```

생성된 SVG를 그대로 붙인 것이 그림 2다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="360" x2="470" y2="360" stroke="currentColor"/> <line x1="60" y1="30" x2="60" y2="360" stroke="currentColor"/> <line x1="60" y1="360.0" x2="470" y2="360.0" stroke="#888" stroke-opacity="0.3"/> <text x="54" y="364.0" font-size="12" text-anchor="end">0.01</text>
<line x1="60" y1="294.0" x2="470" y2="294.0" stroke="#888" stroke-opacity="0.3"/> <text x="54" y="298.0" font-size="12" text-anchor="end">0.1</text> <line x1="60" y1="228.0" x2="470" y2="228.0" stroke="#888" stroke-opacity="0.3"/> <text x="54" y="232.0" font-size="12" text-anchor="end">1</text>
<line x1="60" y1="162.0" x2="470" y2="162.0" stroke="#888" stroke-opacity="0.3"/> <text x="54" y="166.0" font-size="12" text-anchor="end">10</text> <line x1="60" y1="96.0" x2="470" y2="96.0" stroke="#888" stroke-opacity="0.3"/> <text x="54" y="100.0" font-size="12" text-anchor="end">100</text>
<line x1="60" y1="30.0" x2="470" y2="30.0" stroke="#888" stroke-opacity="0.3"/> <text x="54" y="34.0" font-size="12" text-anchor="end">1000</text> <text x="101.0" y="378" font-size="12" text-anchor="middle">≤1 mW</text> <text x="183.0" y="378" font-size="12" text-anchor="middle">1–100 mW</text>
<text x="265.0" y="378" font-size="12" text-anchor="middle">0.1–2 W</text> <text x="347.0" y="378" font-size="12" text-anchor="middle">2–5 W</text> <text x="429.0" y="378" font-size="12" text-anchor="middle">5–25 W</text> <circle cx="101.0" cy="328.5" r="7" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="101.0" y="318.5" font-size="11" text-anchor="middle">1</text> <circle cx="158.4" cy="282.4" r="7" fill="#4a7bd0" fill-opacity="0.8"/> <text x="158.4" y="272.4" font-size="11" text-anchor="middle">2</text> <circle cx="174.8" cy="273.6" r="7" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="174.8" y="263.6" font-size="11" text-anchor="middle">3</text> <circle cx="191.2" cy="267.1" r="7" fill="#4a7bd0" fill-opacity="0.8"/> <text x="191.2" y="257.1" font-size="11" text-anchor="middle">4</text> <circle cx="207.6" cy="242.6" r="7" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="207.6" y="232.6" font-size="11" text-anchor="middle">5</text> <circle cx="240.4" cy="247.9" r="7" fill="#3f9a6b" fill-opacity="0.8"/> <text x="240.4" y="237.9" font-size="11" text-anchor="middle">6</text> <circle cx="256.8" cy="228.0" r="7" fill="#3f9a6b" fill-opacity="0.8"/>
<text x="256.8" y="218.0" font-size="11" text-anchor="middle">7</text> <circle cx="273.2" cy="208.1" r="7" fill="#3f9a6b" fill-opacity="0.8"/> <text x="273.2" y="198.1" font-size="11" text-anchor="middle">8</text> <circle cx="289.6" cy="156.8" r="7" fill="#3f9a6b" fill-opacity="0.8"/>
<text x="289.6" y="146.8" font-size="11" text-anchor="middle">9</text> <circle cx="319.7" cy="188.3" r="7" fill="#3f9a6b" fill-opacity="0.8"/> <text x="319.7" y="178.3" font-size="11" text-anchor="middle">10</text> <circle cx="333.3" cy="188.3" r="7" fill="#e08a3c" fill-opacity="0.8"/>
<text x="333.3" y="178.3" font-size="11" text-anchor="middle">11</text> <circle cx="347.0" cy="154.5" r="7" fill="#e08a3c" fill-opacity="0.8"/> <text x="347.0" y="144.5" font-size="11" text-anchor="middle">12</text> <circle cx="360.7" cy="142.1" r="7" fill="#e08a3c" fill-opacity="0.8"/>
<text x="360.7" y="132.1" font-size="11" text-anchor="middle">13</text> <circle cx="374.3" cy="134.6" r="7" fill="#e08a3c" fill-opacity="0.8"/> <text x="374.3" y="124.6" font-size="11" text-anchor="middle">14</text> <circle cx="415.3" cy="107.5" r="7" fill="#e08a3c" fill-opacity="0.8"/>
<text x="415.3" y="97.5" font-size="11" text-anchor="middle">15</text> <circle cx="442.7" cy="74.2" r="7" fill="#e08a3c" fill-opacity="0.8"/> <text x="442.7" y="64.2" font-size="11" text-anchor="middle">16</text> <text x="484" y="51" font-size="11">1. Syntiant NDP250 0.03 [17]</text>
<text x="484" y="66" font-size="11">2. GreenWaves GAP9 0.15 [18]</text> <text x="484" y="81" font-size="11">3. Alif E4 (U85) 0.204 [8]</text> <text x="484" y="96" font-size="11">4. Renesas RA8P1 0.256 [10]</text> <text x="484" y="111" font-size="11">5. STM32N6 0.6 [5]</text>
<text x="484" y="126" font-size="11">6. NXP i.MX 93 0.5 [29]</text> <text x="484" y="141" font-size="11">7. Synaptics SL2610 1 [19]</text> <text x="484" y="156" font-size="11">8. NXP i.MX 95 2 [30]</text> <text x="484" y="171" font-size="11">9. SD Wear Elite 12 [24]</text>
<text x="484" y="186" font-size="11">10. MediaTek Genio 700 4 [31]</text> <text x="484" y="201" font-size="11">11. Coral Edge TPU 4 [37]</text> <text x="484" y="216" font-size="11">12. Hailo-8L 13 [35]</text> <text x="484" y="231" font-size="11">13. Hailo-10H 20 [36]</text>
<text x="484" y="246" font-size="11">14. Hailo-8 26 [35]</text> <text x="484" y="261" font-size="11">15. Orin Nano Super 67 [38]</text> <text x="484" y="276" font-size="11">16. Axelera Metis 214 [39]</text> <text x="265.0" y="406" font-size="13" text-anchor="middle">전력 등급 (이 노트의 분류, 측정값 아님)</text>
<text x="16" y="195.0" font-size="13" text-anchor="middle" transform="rotate(-90 16 195.0)">공개 peak TOPS (log)</text>
</svg>
```

그림 2 — 공개 peak TOPS(log) vs 전력 등급. 파랑 = always-on MCU 세그먼트, 초록 = 애플리케이션 SoC, 주황 = edge 가속기. 정밀도 조건이 섞여 있다(Orin은 sparse, Hailo-10H는 int8 값 20을 썼다). 오른쪽으로 갈수록 대략 10배씩 올라가는 계단이 보인다 — 전력 등급이 한 칸 오를 때 연산량도 한 자릿수 오른다. 9번(Wear Elite 12 TOPS)은 2차 자료 숫자라서 확정이 아니다.

출력에서 볼 것: 16개 점이 모두 범례와 연결되어 있다. NPU가 없는 Apollo5, TOPS 미공개인 Nordic Axon·NXP MCX N은 **의도적으로 빠졌다** — 숫자가 없는 칩을 0으로 그리면 거짓말이 된다.

### 6.2 소프트웨어 스택 근거 히트맵

칩 선정에서 툴체인은 실리콘만큼 중요하다(M1). 아래 히트맵은 "이 노트에서 무엇을 어느 수준으로 확인했나"를 보여 준다. **점수는 품질 평가가 아니라 근거 수준**이다. 0은 "지원하지 않는다"가 아니라 "이번에 확인하지 못했다"이다.

```svg
<svg viewBox="0 0 676 360" xmlns="http://www.w3.org/2000/svg">
<text x="248" y="38" font-size="12" text-anchor="middle">벤더 SDK</text> <text x="324" y="38" font-size="12" text-anchor="middle">TFLite 입력</text> <text x="400" y="38" font-size="12" text-anchor="middle">ONNX 입력</text> <text x="476" y="38" font-size="12" text-anchor="middle">PyTorch 경로</text>
<text x="552" y="38" font-size="12" text-anchor="middle">Linux·Android</text> <text x="628" y="38" font-size="12" text-anchor="middle">RTOS</text> <text x="202" y="67" font-size="12" text-anchor="end">Ethos-U MCU (Alif·Renesas·Infineon)</text> <rect x="212" y="52" width="72" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="248" y="67" font-size="12" text-anchor="middle">2</text> <rect x="288" y="52" width="72" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.75"/> <text x="324" y="67" font-size="12" text-anchor="middle">2</text> <rect x="364" y="52" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/>
<text x="400" y="67" font-size="12" text-anchor="middle">0</text> <rect x="440" y="52" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="476" y="67" font-size="12" text-anchor="middle">1</text> <rect x="516" y="52" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/>
<text x="552" y="67" font-size="12" text-anchor="middle">1</text> <rect x="592" y="52" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="628" y="67" font-size="12" text-anchor="middle">1</text> <text x="202" y="93" font-size="12" text-anchor="end">ST STM32N6 (Neural-ART)</text>
<rect x="212" y="78" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="248" y="93" font-size="12" text-anchor="middle">1</text> <rect x="288" y="78" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="324" y="93" font-size="12" text-anchor="middle">1</text>
<rect x="364" y="78" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="400" y="93" font-size="12" text-anchor="middle">1</text> <rect x="440" y="78" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="476" y="93" font-size="12" text-anchor="middle">0</text>
<rect x="516" y="78" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="552" y="93" font-size="12" text-anchor="middle">0</text> <rect x="592" y="78" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="628" y="93" font-size="12" text-anchor="middle">1</text>
<text x="202" y="119" font-size="12" text-anchor="end">Ambiq Apollo5 (Helium)</text> <rect x="212" y="104" width="72" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.75"/> <text x="248" y="119" font-size="12" text-anchor="middle">2</text> <rect x="288" y="104" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/>
<text x="324" y="119" font-size="12" text-anchor="middle">1</text> <rect x="364" y="104" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="400" y="119" font-size="12" text-anchor="middle">0</text> <rect x="440" y="104" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/>
<text x="476" y="119" font-size="12" text-anchor="middle">0</text> <rect x="516" y="104" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="552" y="119" font-size="12" text-anchor="middle">0</text> <rect x="592" y="104" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/>
<text x="628" y="119" font-size="12" text-anchor="middle">1</text> <text x="202" y="145" font-size="12" text-anchor="end">Nordic nRF54LM20B (Axon)</text> <rect x="212" y="130" width="72" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.75"/> <text x="248" y="145" font-size="12" text-anchor="middle">2</text>
<rect x="288" y="130" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="324" y="145" font-size="12" text-anchor="middle">0</text> <rect x="364" y="130" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="400" y="145" font-size="12" text-anchor="middle">0</text>
<rect x="440" y="130" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="476" y="145" font-size="12" text-anchor="middle">0</text> <rect x="516" y="130" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="552" y="145" font-size="12" text-anchor="middle">0</text>
<rect x="592" y="130" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="628" y="145" font-size="12" text-anchor="middle">1</text> <text x="202" y="171" font-size="12" text-anchor="end">NXP Neutron (MCX N·RT700·i.MX 95)</text> <rect x="212" y="156" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/>
<text x="248" y="171" font-size="12" text-anchor="middle">1</text> <rect x="288" y="156" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="324" y="171" font-size="12" text-anchor="middle">1</text> <rect x="364" y="156" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/>
<text x="400" y="171" font-size="12" text-anchor="middle">0</text> <rect x="440" y="156" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="476" y="171" font-size="12" text-anchor="middle">0</text> <rect x="516" y="156" width="72" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="552" y="171" font-size="12" text-anchor="middle">2</text> <rect x="592" y="156" width="72" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.75"/> <text x="628" y="171" font-size="12" text-anchor="middle">2</text> <text x="202" y="197" font-size="12" text-anchor="end">Synaptics SL2610 (Torq)</text>
<rect x="212" y="182" width="72" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.75"/> <text x="248" y="197" font-size="12" text-anchor="middle">2</text> <rect x="288" y="182" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="324" y="197" font-size="12" text-anchor="middle">0</text>
<rect x="364" y="182" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="400" y="197" font-size="12" text-anchor="middle">0</text> <rect x="440" y="182" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="476" y="197" font-size="12" text-anchor="middle">0</text>
<rect x="516" y="182" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="552" y="197" font-size="12" text-anchor="middle">1</text> <rect x="592" y="182" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="628" y="197" font-size="12" text-anchor="middle">0</text>
<text x="202" y="223" font-size="12" text-anchor="end">Qualcomm W·AR (Hexagon)</text> <rect x="212" y="208" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="248" y="223" font-size="12" text-anchor="middle">1</text> <rect x="288" y="208" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/>
<text x="324" y="223" font-size="12" text-anchor="middle">1</text> <rect x="364" y="208" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="400" y="223" font-size="12" text-anchor="middle">1</text> <rect x="440" y="208" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/>
<text x="476" y="223" font-size="12" text-anchor="middle">1</text> <rect x="516" y="208" width="72" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.75"/> <text x="552" y="223" font-size="12" text-anchor="middle">2</text> <rect x="592" y="208" width="72" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="628" y="223" font-size="12" text-anchor="middle">2</text> <text x="202" y="249" font-size="12" text-anchor="end">Hailo-8/10H</text> <rect x="212" y="234" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="248" y="249" font-size="12" text-anchor="middle">1</text>
<rect x="288" y="234" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="324" y="249" font-size="12" text-anchor="middle">1</text> <rect x="364" y="234" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="400" y="249" font-size="12" text-anchor="middle">1</text>
<rect x="440" y="234" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="476" y="249" font-size="12" text-anchor="middle">0</text> <rect x="516" y="234" width="72" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.75"/> <text x="552" y="249" font-size="12" text-anchor="middle">2</text>
<rect x="592" y="234" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="628" y="249" font-size="12" text-anchor="middle">0</text> <text x="202" y="275" font-size="12" text-anchor="end">NVIDIA Jetson Orin</text> <rect x="212" y="260" width="72" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="248" y="275" font-size="12" text-anchor="middle">2</text> <rect x="288" y="260" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="324" y="275" font-size="12" text-anchor="middle">0</text> <rect x="364" y="260" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/>
<text x="400" y="275" font-size="12" text-anchor="middle">1</text> <rect x="440" y="260" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="476" y="275" font-size="12" text-anchor="middle">1</text> <rect x="516" y="260" width="72" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="552" y="275" font-size="12" text-anchor="middle">2</text> <rect x="592" y="260" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="628" y="275" font-size="12" text-anchor="middle">0</text> <text x="202" y="301" font-size="12" text-anchor="end">Axelera Metis</text>
<rect x="212" y="286" width="72" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.75"/> <text x="248" y="301" font-size="12" text-anchor="middle">2</text> <rect x="288" y="286" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="324" y="301" font-size="12" text-anchor="middle">1</text>
<rect x="364" y="286" width="72" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.75"/> <text x="400" y="301" font-size="12" text-anchor="middle">2</text> <rect x="440" y="286" width="72" height="22" rx="3" fill="#3f9a6b" fill-opacity="0.75"/> <text x="476" y="301" font-size="12" text-anchor="middle">2</text>
<rect x="516" y="286" width="72" height="22" rx="3" fill="#e08a3c" fill-opacity="0.55"/> <text x="552" y="301" font-size="12" text-anchor="middle">1</text> <rect x="592" y="286" width="72" height="22" rx="3" fill="#888" fill-opacity="0.2"/> <text x="628" y="301" font-size="12" text-anchor="middle">0</text>
<rect x="10" y="321" width="14" height="14" rx="2" fill="#3f9a6b" fill-opacity="0.75"/> <text x="30" y="332" font-size="12">2 = 이번에 공식 자료로 확인</text> <rect x="230" y="321" width="14" height="14" rx="2" fill="#e08a3c" fill-opacity="0.55"/> <text x="250" y="332" font-size="12">1 = 2차 자료·다른 노트(F4 등)</text>
<rect x="450" y="321" width="14" height="14" rx="2" fill="#888" fill-opacity="0.2"/> <text x="470" y="332" font-size="12">0 = 확인 못함(없다는 뜻 아님)</text>
</svg>
```

그림 3 — 소프트웨어 스택 근거 히트맵. 2 = 이번에 공식 자료로 확인(예: Ethos-U의 Vela가 TFLite를 입력으로 받는다 [42], Qualcomm Wear Elite brief의 OS 목록 [23], W5+ 동반 칩의 FreeRTOS [21], Hailo-10H의 Linux·Windows·Android 호스트 [36], Jetson JetPack [38], Axelera Voyager의 PyTorch·ONNX [39], NXP RT700 EVK의 Zephyr 지원 [52]). 1 = 2차 자료 또는 이 노트 세트의 다른 노트(F4 등)에서 다룬 것. 회색이 많은 행일수록 평가 초기에 직접 확인할 것이 많다.

이 그림에서 읽을 것은 이렇다. **MCU 세그먼트는 대부분 "벤더 SDK + TFLite 계열 입력 + RTOS"** 라는 한 가지 모양이고, **SoC·가속기는 Linux/Android + ONNX/PyTorch 경로**가 열려 있다. 그래서 웨어러블 한 대에 MCU와 SoC가 같이 있으면, 같은 모델을 두 가지 경로로 내보내야 할 수 있다(F7, C8).

### 6.3 발표 타임라인 (예제 6)

연·월을 확인한 발표만 모았다(각 항목의 출처는 2~5절 표와 참고 자료).

```python
# 예제 6: 확인된 발표 연·월만으로 타임라인 SVG를 만든다 (색 = 세그먼트)
EV = [  # (year, month, label, seg)  seg: m=MCU/AON, s=SoC, a=가속기, i=IP, b=업계 이벤트
 (2020,10,"GAP9","m"), (2021,1,"NDP120","m"), (2021,11,"i.MX 93","s"),
 (2022,4,"ARC NPX6","i"), (2022,7,"W5+ Gen 1","s"), (2022,11,"AR2 Gen 1","s"),
 (2023,1,"i.MX 95","s"), (2023,1,"ESP32-P4","m"), (2023,9,"AR1 Gen 1","s"), (2023,9,"Cadence Neo","i"),
 (2024,3,"Apollo510","m"), (2024,4,"Ethos-U85","i"), (2024,4,"PSOC Edge","m"), (2024,4,"NDP250","m"),
 (2024,4,"Hailo-10","a"), (2024,7,"Exynos W1000","s"), (2024,9,"i.MX RT700","m"),
 (2024,12,"STM32N6","m"), (2024,12,"Orin Nano S.","a"),
 (2025,1,"Alif E4/6/8","m"), (2025,5,"Imagination E","i"), (2025,5,"Origin Evo","i"),
 (2025,6,"AR1+ Gen 1","s"), (2025,7,"RA8P1","m"), (2025,7,"Ambiq IPO","b"), (2025,8,"W5 Gen 2","s"),
 (2025,8,"Tensor G5","s"), (2025,10,"Coral NPU","i"), (2025,10,"SL2610","s"),
 (2026,1,"nRF54LM20B","m"), (2026,3,"Wear Elite","s"), (2026,9,"ADI→Alif","b"),
]
col = {"m":"#4a7bd0","s":"#3f9a6b","a":"#e08a3c","i":"#888","b":"#d0564a"}
W, L, top, row = 680, 20, 60, 17
cw = (W - 2*L) / 7
o = [f'<svg viewBox="0 0 {W} 240" xmlns="http://www.w3.org/2000/svg">',
     f'<line x1="{L}" y1="40" x2="{W-L}" y2="40" stroke="currentColor"/>']
for i, y in enumerate(range(2020, 2027)):
    x = L + cw*i
    o += [f'<line x1="{x:.0f}" y1="34" x2="{x:.0f}" y2="235" stroke="#888" stroke-opacity="0.3"/>',
          f'<text x="{x+cw/2:.0f}" y="28" font-size="13" text-anchor="middle">{y}</text>']
    evs = sorted([e for e in EV if e[0] == y], key=lambda e: e[1])
    for k, (yy, m, lab, s) in enumerate(evs):
        cy = top + k*row
        o += [f'<circle cx="{x+8:.0f}" cy="{cy-4}" r="4" fill="{col[s]}"/>',
              f'<text x="{x+15:.0f}" y="{cy}" font-size="10">{m:02d} {lab}</text>']
o.append('</svg>')
open("timeline.svg","w").write("\n".join(o))
from collections import Counter
print("events:", len(EV), "| per year:", dict(sorted(Counter(e[0] for e in EV).items())))
```

```text
events: 32 | per year: {2020: 1, 2021: 2, 2022: 3, 2023: 4, 2024: 9, 2025: 10, 2026: 3}
```

```svg
<svg viewBox="0 0 680 240" xmlns="http://www.w3.org/2000/svg">
<line x1="20" y1="40" x2="660" y2="40" stroke="currentColor"/> <line x1="20" y1="34" x2="20" y2="235" stroke="#888" stroke-opacity="0.3"/> <text x="66" y="28" font-size="13" text-anchor="middle">2020</text> <circle cx="28" cy="56" r="4" fill="#4a7bd0"/>
<text x="35" y="60" font-size="10">10 GAP9</text> <line x1="111" y1="34" x2="111" y2="235" stroke="#888" stroke-opacity="0.3"/> <text x="157" y="28" font-size="13" text-anchor="middle">2021</text> <circle cx="119" cy="56" r="4" fill="#4a7bd0"/>
<text x="126" y="60" font-size="10">01 NDP120</text> <circle cx="119" cy="73" r="4" fill="#3f9a6b"/> <text x="126" y="77" font-size="10">11 i.MX 93</text> <line x1="203" y1="34" x2="203" y2="235" stroke="#888" stroke-opacity="0.3"/>
<text x="249" y="28" font-size="13" text-anchor="middle">2022</text> <circle cx="211" cy="56" r="4" fill="#888"/> <text x="218" y="60" font-size="10">04 ARC NPX6</text> <circle cx="211" cy="73" r="4" fill="#3f9a6b"/>
<text x="218" y="77" font-size="10">07 W5+ Gen 1</text> <circle cx="211" cy="90" r="4" fill="#3f9a6b"/> <text x="218" y="94" font-size="10">11 AR2 Gen 1</text> <line x1="294" y1="34" x2="294" y2="235" stroke="#888" stroke-opacity="0.3"/>
<text x="340" y="28" font-size="13" text-anchor="middle">2023</text> <circle cx="302" cy="56" r="4" fill="#3f9a6b"/> <text x="309" y="60" font-size="10">01 i.MX 95</text> <circle cx="302" cy="73" r="4" fill="#4a7bd0"/>
<text x="309" y="77" font-size="10">01 ESP32-P4</text> <circle cx="302" cy="90" r="4" fill="#3f9a6b"/> <text x="309" y="94" font-size="10">09 AR1 Gen 1</text> <circle cx="302" cy="107" r="4" fill="#888"/>
<text x="309" y="111" font-size="10">09 Cadence Neo</text> <line x1="386" y1="34" x2="386" y2="235" stroke="#888" stroke-opacity="0.3"/> <text x="431" y="28" font-size="13" text-anchor="middle">2024</text> <circle cx="394" cy="56" r="4" fill="#4a7bd0"/>
<text x="401" y="60" font-size="10">03 Apollo510</text> <circle cx="394" cy="73" r="4" fill="#888"/> <text x="401" y="77" font-size="10">04 Ethos-U85</text> <circle cx="394" cy="90" r="4" fill="#4a7bd0"/>
<text x="401" y="94" font-size="10">04 PSOC Edge</text> <circle cx="394" cy="107" r="4" fill="#4a7bd0"/> <text x="401" y="111" font-size="10">04 NDP250</text> <circle cx="394" cy="124" r="4" fill="#e08a3c"/>
<text x="401" y="128" font-size="10">04 Hailo-10</text> <circle cx="394" cy="141" r="4" fill="#3f9a6b"/> <text x="401" y="145" font-size="10">07 Exynos W1000</text> <circle cx="394" cy="158" r="4" fill="#4a7bd0"/>
<text x="401" y="162" font-size="10">09 i.MX RT700</text> <circle cx="394" cy="175" r="4" fill="#4a7bd0"/> <text x="401" y="179" font-size="10">12 STM32N6</text> <circle cx="394" cy="192" r="4" fill="#e08a3c"/>
<text x="401" y="196" font-size="10">12 Orin Nano S.</text> <line x1="477" y1="34" x2="477" y2="235" stroke="#888" stroke-opacity="0.3"/> <text x="523" y="28" font-size="13" text-anchor="middle">2025</text> <circle cx="485" cy="56" r="4" fill="#4a7bd0"/>
<text x="492" y="60" font-size="10">01 Alif E4/6/8</text> <circle cx="485" cy="73" r="4" fill="#888"/> <text x="492" y="77" font-size="10">05 Imagination E</text> <circle cx="485" cy="90" r="4" fill="#888"/>
<text x="492" y="94" font-size="10">05 Origin Evo</text> <circle cx="485" cy="107" r="4" fill="#3f9a6b"/> <text x="492" y="111" font-size="10">06 AR1+ Gen 1</text> <circle cx="485" cy="124" r="4" fill="#4a7bd0"/>
<text x="492" y="128" font-size="10">07 RA8P1</text> <circle cx="485" cy="141" r="4" fill="#d0564a"/> <text x="492" y="145" font-size="10">07 Ambiq IPO</text> <circle cx="485" cy="158" r="4" fill="#3f9a6b"/>
<text x="492" y="162" font-size="10">08 W5 Gen 2</text> <circle cx="485" cy="175" r="4" fill="#3f9a6b"/> <text x="492" y="179" font-size="10">08 Tensor G5</text> <circle cx="485" cy="192" r="4" fill="#888"/>
<text x="492" y="196" font-size="10">10 Coral NPU</text> <circle cx="485" cy="209" r="4" fill="#3f9a6b"/> <text x="492" y="213" font-size="10">10 SL2610</text> <line x1="569" y1="34" x2="569" y2="235" stroke="#888" stroke-opacity="0.3"/>
<text x="614" y="28" font-size="13" text-anchor="middle">2026</text> <circle cx="577" cy="56" r="4" fill="#4a7bd0"/> <text x="584" y="60" font-size="10">01 nRF54LM20B</text> <circle cx="577" cy="73" r="4" fill="#3f9a6b"/>
<text x="584" y="77" font-size="10">03 Wear Elite</text> <circle cx="577" cy="90" r="4" fill="#d0564a"/> <text x="584" y="94" font-size="10">09 ADI→Alif</text>
</svg>
```

그림 4 — 확인된 발표 타임라인(색: 파랑 MCU/AON, 초록 SoC, 주황 가속기, 회색 IP, 빨강 업계 이벤트). 2024~2025에 발표가 몰린다. 세 가지 흐름이 보인다: (1) 2024 Ethos-U85 → 2025 Alif E4/E6/E8처럼 **IP 발표 후 약 9개월 뒤 칩**, (2) Qualcomm 웨어러블·AR 라인의 거의 매년 갱신, (3) 2025~2026의 인수·상장·오픈 IP(Coral NPU) 같은 **업계 구조 변화**.

출력에서 볼 것: 2024년 9건, 2025년 10건 — 이 분야는 1년이면 지도가 바뀐다. 그래서 이 노트의 모든 숫자에 "확인일 2026-10-03"을 붙였다.

---

## 7. Hark 관점에서 이 지도를 읽는 법

### 7.1 단서 — 이전 리서치에서 나온 것 (사실 vs 추정 구분)

이 작업공간의 이전 리서치(`job_interview_prep_research/hark_ai_embedded_swe/hark_ai_embedded_swe_context.md` §2.2)에는 Hark 채용 공고에서 읽은 단서가 정리되어 있다. 그대로 옮기면:

| 단서 | 출처 표기 (그 문서 기준) |
|---|---|
| System Test 공고: "Working knowledge with Qualcomm chipsets", RTOS/Linux/Android | 확인됨 (공고 문구) |
| On-Device AI 공고: "Hexagon DSP, Ambiq-class MCUs", INT8/INT4, speech 추론 | 확인됨 (공고 문구) |
| Power 공고: Qualcomm/TI/Nordic/Maxim PMIC | 확인됨 (공고 문구) |
| Series A 투자자에 Qualcomm Ventures | 확인됨 |
| 구조: Qualcomm SoC(Cortex-A, Android) + 상시 저전력 MCU(Ambiq Apollo 계열, RTOS) | **추정** |

공고 문구는 사실이지만, "실제 기기에 그 부품이 들어간다"는 것은 추정이다. 면접에서도 "공고에서 Hexagon과 Ambiq-class라는 표현을 봤다"까지만 말하고, 구조는 질문으로 확인한다.

### 7.2 가장 그럴듯한 구조 — 그림으로

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="m3a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <rect x="10" y="40" width="110" height="44" rx="6" fill="#888" fill-opacity="0.15" stroke="#888"/> <text x="65" y="60" font-size="12" text-anchor="middle">PDM 마이크 ×2~4</text> <text x="65" y="76" font-size="11" text-anchor="middle">I2S/PDM</text>
<rect x="10" y="110" width="110" height="44" rx="6" fill="#888" fill-opacity="0.15" stroke="#888"/> <text x="65" y="130" font-size="12" text-anchor="middle">IMU · 착용 센서</text> <text x="65" y="146" font-size="11" text-anchor="middle">SPI/I2C + FIFO</text> <rect x="160" y="30" width="170" height="140" rx="8" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="245" y="52" font-size="13" text-anchor="middle">동반 MCU (항상 켜짐)</text> <text x="245" y="74" font-size="11" text-anchor="middle">Cortex-M55 Helium</text> <text x="245" y="90" font-size="11" text-anchor="middle">(± Ethos-U micro-NPU)</text> <text x="245" y="110" font-size="11" text-anchor="middle">VAD · KWS · 제스처</text>
<text x="245" y="126" font-size="11" text-anchor="middle">RTOS · 수 mW</text> <text x="245" y="146" font-size="11" text-anchor="middle">예: Apollo5급 [1]</text> <rect x="380" y="30" width="180" height="140" rx="8" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/> <text x="470" y="52" font-size="13" text-anchor="middle">웨어러블/AR SoC</text>
<text x="470" y="74" font-size="11" text-anchor="middle">Cortex-A + LPDDR</text> <text x="470" y="90" font-size="11" text-anchor="middle">Hexagon NPU: ASR · SLM</text> <text x="470" y="110" font-size="11" text-anchor="middle">Linux/Android · 수백 mW+</text> <text x="470" y="126" font-size="11" text-anchor="middle">대부분 시간 sleep</text>
<text x="470" y="146" font-size="11" text-anchor="middle">예: Wear Elite · AR1+ [23][26]</text> <rect x="590" y="60" width="82" height="80" rx="6" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/> <text x="631" y="88" font-size="12" text-anchor="middle">폰 ·</text> <text x="631" y="106" font-size="12" text-anchor="middle">클라우드</text>
<line x1="120" y1="62" x2="158" y2="72" stroke="currentColor" marker-end="url(#m3a)"/> <line x1="120" y1="132" x2="158" y2="122" stroke="currentColor" marker-end="url(#m3a)"/> <line x1="330" y1="80" x2="378" y2="80" stroke="currentColor" marker-end="url(#m3a)"/> <text x="354" y="72" font-size="11" text-anchor="middle">wake</text>
<line x1="378" y1="120" x2="332" y2="120" stroke="currentColor" marker-end="url(#m3a)"/> <text x="355" y="136" font-size="11" text-anchor="middle">IPC</text> <line x1="560" y1="100" x2="588" y2="100" stroke="currentColor" marker-end="url(#m3a)"/> <rect x="380" y="200" width="180" height="40" rx="6" fill="none" stroke="#3f9a6b" stroke-dasharray="5 3"/>
<text x="470" y="217" font-size="11" text-anchor="middle">대안: SoC 내 eNPU · 센싱허브가</text> <text x="470" y="232" font-size="11" text-anchor="middle">MCU 역할을 흡수 [23][25]</text> <line x1="330" y1="150" x2="378" y2="212" stroke="#888" stroke-dasharray="4 3"/> <rect x="160" y="200" width="170" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="245" y="225" font-size="12" text-anchor="middle">PMIC · 배터리 (수백 mAh)</text> <text x="340" y="275" font-size="12" text-anchor="middle">추정 구조 — Hark 공고 단서(Qualcomm · Hexagon · Ambiq-class MCU)에서 거꾸로 그린 것, 사실 아님</text>
</svg>
```

그림 5 — 공고 단서에서 거꾸로 그린 추정 구조. 마이크·IMU는 항상 켜진 동반 MCU로 들어가고, MCU가 wake word·제스처를 잡으면 SoC를 깨운다. SoC는 ASR과 SLM을 Hexagon NPU에서 돌리고 필요하면 폰·클라우드로 넘긴다(I3, L3). 점선 상자는 대안이다 — Wear Elite처럼 SoC 플랫폼에 eNPU·Sensing Hub가 있으면 동반 MCU의 역할 일부를 흡수할 수 있다 [23][24][25].

이 구조에서 지도의 각 세그먼트가 하는 일은 이렇다.

| 지도의 세그먼트 | 이 구조에서의 자리 | 고를 때 보는 것 |
|---|---|---|
| 2절 MCU | 동반 MCU | 상시 전력(µA/MHz, sleep), Helium 유무, NPU 필요 여부, SRAM, 오디오 인터페이스 수 |
| 3절 SoC | 메인 SoC | NPU의 SLM 성능(대역폭!), eNPU/Sensing Hub 유무, BSP·SDK 접근, 연결성 |
| 4절 가속기 | 없음 (향후 제품·리그) | — |
| 5절 IP | 2~3세대 커스텀 실리콘 검토 시 | 물량, 에너지 격차, 팀 |

### 7.3 언제 무엇을 — 결정 트리

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="m3t" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <rect x="240" y="10" width="200" height="40" rx="8" fill="#888" fill-opacity="0.15" stroke="#888"/> <text x="340" y="35" font-size="13" text-anchor="middle">이 워크로드는 무엇인가?</text> <line x1="340" y1="50" x2="90" y2="98" stroke="currentColor" marker-end="url(#m3t)"/>
<line x1="340" y1="50" x2="270" y2="98" stroke="currentColor" marker-end="url(#m3t)"/> <line x1="340" y1="50" x2="440" y2="98" stroke="currentColor" marker-end="url(#m3t)"/> <line x1="340" y1="50" x2="600" y2="98" stroke="currentColor" marker-end="url(#m3t)"/> <rect x="10" y="100" width="160" height="50" rx="8" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="90" y="121" font-size="12" text-anchor="middle">상시 감지 (VAD · KWS</text> <text x="90" y="139" font-size="12" text-anchor="middle">IMU 제스처 · 착용 감지)</text> <rect x="190" y="100" width="160" height="50" rx="8" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/> <text x="270" y="121" font-size="12" text-anchor="middle">대화형 음성 · SLM</text>
<text x="270" y="139" font-size="12" text-anchor="middle">(ASR · 0.5B+ 모델)</text> <rect x="370" y="100" width="140" height="50" rx="8" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/> <text x="440" y="121" font-size="12" text-anchor="middle">멀티카메라 비전</text> <text x="440" y="139" font-size="12" text-anchor="middle">로봇 · 홈 허브</text>
<rect x="530" y="100" width="140" height="50" rx="8" fill="#888" fill-opacity="0.15" stroke="#888"/> <text x="600" y="121" font-size="12" text-anchor="middle">수백만 대 물량</text> <text x="600" y="139" font-size="12" text-anchor="middle">+ 차별화 연산</text> <line x1="90" y1="150" x2="90" y2="178" stroke="currentColor" marker-end="url(#m3t)"/>
<rect x="10" y="180" width="160" height="50" rx="8" fill="none" stroke="currentColor" stroke-dasharray="4 3"/> <text x="90" y="201" font-size="12" text-anchor="middle">SoC 안 저전력 섬(eNPU ·</text> <text x="90" y="219" font-size="12" text-anchor="middle">센싱허브)으로 충분한가?</text> <line x1="60" y1="230" x2="50" y2="268" stroke="currentColor" marker-end="url(#m3t)"/>
<line x1="120" y1="230" x2="150" y2="268" stroke="currentColor" marker-end="url(#m3t)"/> <text x="40" y="252" font-size="11" text-anchor="end">예</text> <text x="146" y="252" font-size="11">아니오</text> <rect x="0" y="270" width="96" height="50" rx="8" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/>
<text x="48" y="291" font-size="11" text-anchor="middle">SoC 섬 사용</text> <text x="48" y="308" font-size="11" text-anchor="middle">부품 수 최소</text> <rect x="104" y="270" width="150" height="50" rx="8" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/> <text x="179" y="291" font-size="11" text-anchor="middle">동반 MCU: Helium 또는</text>
<text x="179" y="308" font-size="11" text-anchor="middle">micro-NPU (Apollo5 · Alif)</text> <line x1="270" y1="150" x2="330" y2="268" stroke="currentColor" marker-end="url(#m3t)"/> <rect x="258" y="270" width="152" height="50" rx="8" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/> <text x="334" y="291" font-size="11" text-anchor="middle">DRAM + NPU 있는</text>
<text x="334" y="308" font-size="11" text-anchor="middle">웨어러블·AR SoC + 클라우드</text> <line x1="440" y1="150" x2="476" y2="268" stroke="currentColor" marker-end="url(#m3t)"/> <rect x="414" y="270" width="120" height="50" rx="8" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/> <text x="474" y="291" font-size="11" text-anchor="middle">Hailo · Jetson</text>
<text x="474" y="308" font-size="11" text-anchor="middle">Axelera</text> <line x1="600" y1="150" x2="604" y2="268" stroke="currentColor" marker-end="url(#m3t)"/> <rect x="544" y="270" width="130" height="50" rx="8" fill="#888" fill-opacity="0.25" stroke="#888"/> <text x="609" y="291" font-size="11" text-anchor="middle">라이선스 NPU IP로</text>
<text x="609" y="308" font-size="11" text-anchor="middle">커스텀 SoC (수년)</text>
</svg>
```

그림 6 — 세그먼트 결정 트리. 가장 중요한 분기는 왼쪽 아래 점선 상자다: **SoC 안의 저전력 섬으로 상시 감지가 충분한가?** 충분하면 부품 하나를 줄인다. 부족하면(상시 전력이 높거나, 모델이 섬에 안 들어가거나, 섬의 툴이 닫혀 있으면) 동반 MCU를 둔다. 이 질문의 답은 데이터시트가 아니라 **우리 모델로 잰 전력**(K3)으로 정한다.

### 7.4 코드로 확인 — 가중 점수와 민감도 (예제 5)

M1에서 다룰 평가 기준표를 미리 맛본다. 다섯 가지 아키텍처 후보에 1~5점을 주고 가중합을 낸 다음, 가중치를 ±50% 흔들어 1위가 얼마나 자주 바뀌는지 본다. **점수와 가중치는 모두 이 노트의 가정**이다. 핵심은 숫자가 아니라 "결론이 가중치에 얼마나 민감한가"를 보는 절차다(L1 결정 매트릭스와 같은 방법).

```python
# 예제 5: Hark 같은 웨어러블의 아키텍처 후보를 가중 점수로 비교하고, 가중치를 흔들어 본다
# 점수(1~5)와 가중치는 모두 이 노트의 "가정"이다. 숫자보다 절차가 핵심.
import numpy as np
crit = ["AON 전력", "SLM 가능", "SW 성숙도", "면적/BOM", "공급 리스크", "일정"]
w0 = np.array([0.25, 0.20, 0.20, 0.10, 0.10, 0.15])
opts = {
  "A: 웨어러블 SoC 단독(eNPU/센싱허브)": [3, 5, 4, 5, 4, 4],
  "B: SoC + Helium MCU(NPU 없음)":        [4, 5, 4, 3, 4, 4],
  "C: SoC + micro-NPU MCU(Ethos-U)":      [5, 5, 3, 3, 3, 3],
  "D: micro-NPU MCU 단독 + 클라우드":     [5, 2, 3, 5, 3, 4],
  "E: 커스텀 NPU 실리콘":                 [5, 4, 1, 4, 2, 1],
}
S = np.array(list(opts.values()), dtype=float)
base = S @ w0
for name, sc in sorted(zip(opts, base), key=lambda t: -t[1]):
    print(f"{sc:4.2f}  {name}")
rng = np.random.default_rng(0)
wins = {k: 0 for k in opts}
for _ in range(2000):                         # 가중치를 ±50% 흔들고 다시 정규화
    w = w0 * rng.uniform(0.5, 1.5, size=w0.size); w /= w.sum()
    wins[list(opts)[int(np.argmax(S @ w))]] += 1
print("1위 횟수 /2000:", {k.split(':')[0]: v for k, v in wins.items()})
```

```text
4.10  B: SoC + Helium MCU(NPU 없음)
4.05  A: 웨어러블 SoC 단독(eNPU/센싱허브)
3.90  C: SoC + micro-NPU MCU(Ethos-U)
3.65  D: micro-NPU MCU 단독 + 클라우드
3.00  E: 커스텀 NPU 실리콘
1위 횟수 /2000: {'A': 601, 'B': 1321, 'C': 66, 'D': 12, 'E': 0}
```

출력에서 볼 것: 기본 가중치에서는 B(SoC + Helium MCU, 4.10)와 A(SoC 단독, 4.05)가 거의 같다. 가중치를 흔들면 B가 약 66%, A가 약 30% 1위다. 즉 "동반 MCU를 둘지"는 **가중치(특히 AON 전력 vs 면적·BOM)에 민감한 결정**이고, 그래서 실측이 필요하다. 커스텀 NPU(E)는 SW 성숙도·일정 점수 때문에 2,000번 중 한 번도 1위가 아니다 — 1세대 제품에서 커스텀을 하지 않는 이유가 숫자로 보인다. C(micro-NPU MCU)는 AON 전력은 최고지만 툴·공급 점수에서 밀린다. 모델이 커져 Helium으로 부족해지면 C의 "AON 전력"과 "SLM 가능" 이외의 장점(상시 ASR 일부 처리)을 반영해 점수를 다시 매겨야 한다.

### 7.5 면접에서 쓸 한 문단 (이 노트의 추론)

"For a voice-and-gesture wearable, I'd expect a Qualcomm wearable or AR-class SoC for ASR and the SLM, plus an always-on Cortex-M55-class companion MCU — Ambiq-class, as your job posts hint — for VAD, keyword spotting and IMU gestures under a few milliwatts. The open question I'd want to measure early is whether the SoC's own low-power island — Qualcomm calls it Sensing Hub or eNPU on newer parts — can absorb that always-on work. If it can, we save a part and an RTOS; if not, the companion MCU stays."

---

## 8. 임베디드 관점에서 다시 보기

지도를 펌웨어 엔지니어의 눈으로 다시 보면, 칩마다 다음 다섯 가지가 일의 양을 결정한다.

**① 메모리 배치.** MCU 세그먼트는 SRAM이 0.5~10 MB 범위다(nRF54LM20B 512 KB [6], Apollo510 3.75 MB [1], STM32N6 4.2 MB [5], RT700 7.5 MB [12], Alif E4 9.75 MB(2차) [8]). 모델 가중치·tensor arena·라디오 스택·오디오 버퍼가 이 안에서 겹친다. 링커 스크립트와 메모리 맵이 Don의 무기다(K1, D2).

```
예: 512 KB RAM BLE SoC에서 KWS를 돌린다면 (가정 숫자)
  BLE 스택 + RTOS        ~ 100 KB
  오디오 링버퍼 (2 mic)   ~  32 KB
  tensor arena           ~  60 KB
  나머지 앱              ~ 나머지
  → 모델이 커지면 가장 먼저 arena가 부족하다
```

**② 전원 상태와 wake 경로.** "AI 성능"보다 **NPU를 켜고 끄는 비용**과 **SoC를 깨우는 지연**이 배터리를 좌우한다(D7, E9). Wear Elite brief의 "low-power islands" [23], W5+의 "Deep Sleep and Hibernate" [21] 같은 표현이 실제로 어떤 전류·지연인지 데이터시트(NDA)로 확인해야 한다.

**③ 칩 간 인터페이스.** 동반 MCU와 SoC 사이 IPC(SPI/UART/공유 메모리), 오디오를 누가 받는지(PDM을 MCU가 받고 SoC에 넘기나, 둘 다 받나), wake 신호 GPIO, 시간 동기(G7). 2-칩 구조는 이 경계에서 버그가 난다 — Don이 Apple에서 버스 장애를 root cause 했던 바로 그 자리다.

**④ 툴체인 이원화.** MCU 쪽은 TFLite Micro/Vela/벤더 SDK(F2), SoC 쪽은 QNN/AI Hub(F4)다. 같은 모델의 두 양자화 결과가 bit-exact가 아니어도, **골든 벡터로 허용 오차를 정의**하고 CI에서 돌려야 한다(C8, J6).

**⑤ bring-up 순서.** 새 칩 평가는 보통 EVK → 벤더 예제 모델 → 우리 모델 변환 → 커버리지·수치 확인 → 전력 측정 → 우리 보드 순서다(F8, M2). 각 단계에 NDA·계정·라이선스 시간이 끼어든다.

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| int4 TOPS를 int8 TOPS와 비교 | 후보 A가 2배 좋아 보였는데 실측은 비슷 | 헤드라인이 int4 (예: Hailo-10H 40 vs 20) | 표에 정밀도 칸을 반드시 둔다 |
| sparse TOPS를 dense로 읽음 | 행렬곱 실측이 기대의 절반 | 2:4 sparsity 조건의 peak (예: Orin 67) | "sparse" 표기 확인, dense로 환산 |
| "CPU 대비 X배"를 벤더끼리 비교 | 42배 칩이 172배 칩보다 빨랐다 | 기준 CPU·모델이 다르다 | 같은 모델로 직접 잰다 (M2) |
| TOPS 미공개 칩을 후보에서 제외 | 가장 저전력인 Helium-only 칩을 놓침 | 숫자가 없는 것 ≠ 성능이 없는 것 | 대표 모델 latency·energy로 비교 |
| SRAM 크기만 보고 모델 적합 판단 | 링크 단계에서 RAM overflow | 라디오 스택·버퍼·arena가 같은 SRAM을 쓴다 | 메모리 예산표를 먼저 만든다 (K1) |
| SoC TOPS로 SLM 속도 추정 | tok/s가 기대의 1/10 | decode는 대역폭에 묶인다 | 대역폭 ÷ 가중치 바이트로 상한 계산 (예제 3) |
| 공식 표기 모호성 무시 ("4.2 GHz") | 대역폭 계산이 2배 틀림 | 클럭 vs 전송률 | 가정을 적고 데이터시트로 확인 |
| 툴체인을 평가 마지막에 봄 | 칩은 골랐는데 모델이 안 올라간다 | op 미지원, 컴파일러 버그 | 첫 주에 우리 모델로 컴파일 시도 (C6) |
| 벤더 존속 리스크 무시 | 인수 후 로드맵 변경 | 스타트업·소형 벤더 | 표준 IP·표준 포맷 유지, 2nd source 설계 |
| 2차 자료 숫자를 사실처럼 인용 | 면접에서 근거 질문에 답 못 함 | 기사 숫자는 출처가 섞인다 | "벤더 brief 기준 / 기사 기준"을 구분해 말한다 |

---

## 10. 면접에서 이렇게 말한다

**Q.** "What chips would you consider for an always-on voice wearable?"

**A.** 세그먼트 두 개로 나눠 답한다. 상시 감지(VAD, KWS, 제스처)는 Cortex-M55 + Helium급 MCU — Ambiq Apollo5처럼 NPU 없는 초저전력형이나 Alif·Renesas처럼 Ethos-U가 붙은 형 — 또는 Syntiant 같은 전용 칩. 대화형 음성과 SLM은 DRAM과 NPU가 있는 웨어러블/AR SoC, 예를 들어 Qualcomm W5/Wear Elite나 AR1 계열. 그리고 SoC의 저전력 섬이 상시 감지를 흡수할 수 있는지를 첫 측정 항목으로 둔다.

> "I'd split it into two tiers. For always-on VAD, keyword spotting and IMU gestures, a Cortex-M55-class MCU with Helium — an Ambiq Apollo5-type part, or an Ethos-U part like Alif's or Renesas's — or a dedicated always-on chip like Syntiant's. For conversational ASR and an on-device SLM, a wearable or AR-class SoC with LPDDR and a Hexagon-class NPU. The first thing I'd measure is whether the SoC's own low-power island can take over the always-on tier, because that decides whether we need a second part at all."

**Q.** "Compare an Ethos-U55 MCU with a Hexagon-based SoC for our use."

**A.** 다른 체급이라 경쟁이 아니라 분업이다. Ethos-U55는 32~256 MAC, 500 MHz에서 최대 256 GOPS급이고 SRAM 안에서 mW로 돈다. CNN·작은 RNN 상시 모델에 맞고, Vela로 컴파일하며, 지원 밖 op는 CPU fallback. Hexagon SoC는 LPDDR가 있어 수백 MB 모델(ASR, SLM)을 돌리지만 깨어 있으면 수백 mW 이상이다. 그래서 U55는 1단, Hexagon은 2단. 실제로 Qualcomm W5+ 동반 칩에도 "U55 ML core"가 들어 있다.

> "They're different weight classes, so I see them as tiers rather than competitors. An Ethos-U55 tops out around 256 MACs per cycle — about 256 GOPS at 500 MHz — runs out of on-chip SRAM at milliwatts, and suits small always-on CNNs; anything Vela can't map falls back to the Cortex-M. A Hexagon SoC has LPDDR, so it can hold ASR and SLM weights, but it burns hundreds of milliwatts when awake. Qualcomm's own W5+ platform pairs the two: the always-on co-processor has a U55 ML core next to the 4 nm SoC."

**Q.** "When would you consider a custom NPU?"

**A.** 네 조건이 다 맞을 때: 물량이 수백만 대 단위, 상용 칩이 에너지 예산을 구조적으로 못 맞춤, 모델 구조가 안정되어 전용 가속의 이득이 큼, 컴파일러·검증 팀을 꾸릴 수 있음. IP를 사도 컴파일러와 검증은 우리 몫이다. 1세대는 상용 SoC로 출시하고 실측 데이터로 2~3세대 커스텀을 판단한다.

> "Only when four things line up: volume in the millions, a structural energy gap that no merchant part closes, a model architecture stable enough that a dedicated datapath pays off, and the ability to staff compiler and verification work — licensing the NPU IP doesn't give you the toolchain. For a first-generation product I'd ship on a merchant SoC, instrument it heavily, and use that field data to decide on custom silicon for gen two or three."

**Q.** "What's the risk of a startup NPU vendor?"

**A.** 다섯 가지: 존속·인수(실제로 2026-09 ADI가 Alif 인수 발표), 툴체인 미성숙(op 미지원·수치 불일치), 단일 공급, 문서 비공개, 장기 공급. 완화책은 표준 IP(Ethos-U)를 쓰는 칩 선호, 모델을 표준 포맷으로 유지, 첫 주에 우리 모델로 커버리지 측정, 버그 대응 SLA 확인.

> "Five risks: viability or acquisition — Analog Devices just announced it's acquiring Alif, for example — toolchain maturity, single sourcing, closed documentation, and longevity. I mitigate by preferring parts built on standard IP like Ethos-U, keeping models in standard formats, compiling our own models in week one to measure operator coverage and numerical match, and getting a support commitment in writing."

**Q.** "A vendor says their wearable chip runs a 2B model at 10 tokens per second. How do you sanity-check that?"

**A.** decode는 대역폭 상한부터 계산한다: 토큰당 가중치 전체를 읽으니 tok/s ≤ 대역폭 ÷ 가중치 바이트. 16-bit LPDDR5 4200 MT/s라고 가정하면 8.4 GB/s, 2B × 4-bit = 1 GB → 8.4 tok/s가 천장. 10 tok/s라면 4-bit보다 작은 가중치, 더 넓은 대역폭, 또는 다른 측정 조건이 있다. 그 조건을 물어본다.

> "I'd start from the bandwidth ceiling: decode reads every weight once per token, so tokens per second can't exceed bandwidth divided by weight bytes. If it's a 16-bit LPDDR5 bus at 4200 MT/s, that's 8.4 GB/s; a 2B model at 4 bits is about 1 GB, so the ceiling is roughly 8 tokens per second. Hitting 10 means sub-4-bit weights, more bandwidth than I assumed, or a different measurement setup — and that's exactly what I'd ask them."

**Q.** "Why not just put a Hailo or a Jetson in the device?"

**A.** 전력 등급이 다르다. Hailo-10H typical 2.5 W, Orin Nano Super 최대 25 W 모드인데, 수백 mAh 배터리 웨어러블의 평균 전력 예산은 수십 mW 수준이다(I1). 이 칩들은 홈 허브나 로봇, 데이터 수집 리그에 맞다.

> "It's a power-class mismatch. A Hailo-10H is about 2.5 watts typical and an Orin Nano Super runs up to a 25-watt mode, while a wearable with a few hundred milliamp-hours has an average budget in the tens of milliwatts. Those parts make sense for a home hub, a robot, or our data-collection rigs, not on the wrist."

**Q.** "How do you compare TOPS numbers across vendors?"

**A.** 같은 조건으로 맞춘다: 정밀도(int8 vs int4), sparse 여부, 클럭, NPU만인지 합산인지. 그다음 MAC × 2 × f로 역검산하고, 마지막엔 우리 모델의 latency와 inference당 에너지로 비교한다. TOPS는 후보를 거르는 데만 쓴다.

> "I normalize first — precision, sparsity, clock, and whether it's the NPU alone or a sum across blocks — then back-check it as MACs times two times frequency. After that I stop trusting TOPS and compare latency and energy per inference on our own models; peak TOPS is only good for the first cut."

---

## 11. 직접 해보기

1. **손계산.** Ethos-U55 128 MAC 구성이 400 MHz로 돈다면 peak GOPS는? 같은 칩이 int8 KWS 모델(MAC 2.5M개/추론)을 돌릴 때 NPU 활용률 30%라면 추론 하나에 몇 µs인가? 정답: 128 × 2 × 400 MHz = 102.4 GOPS. 연산 = 2.5M × 2 = 5 M op, 유효 = 102.4 × 0.3 ≈ 30.7 GOPS → 5×10⁶ / 30.7×10⁹ ≈ 163 µs.
2. **손계산.** i.MX 95의 LPDDR5 "6.4 GT/s x32"에서 대역폭을 구하고, 0.5B 모델을 int8로 둘 때 decode 상한 tok/s를 구하라. 정답: 6.4×10⁹ × 4 B = 25.6 GB/s. 가중치 0.5 GB → 약 51 tok/s(상한, 실제는 더 낮다).
3. **코드 과제.** 예제 2에 Coral(4 TOPS, 2 W)처럼 전력과 TOPS가 둘 다 공개된 부품을 하나 더 찾아 넣고, 출처 URL과 확인일을 주석으로 단다. 찾지 못하면 "미공개"로 두는 것이 정답이다. 힌트: STM32N6은 "3 TOPS/W"를 직접 공개했지만 NPU 전력(W)은 따로 공개하지 않았다 — TOPS/W를 역산해 W를 만드는 것은 derived임을 표시해야 한다.
4. **코드 과제.** 예제 5의 점수표에서 "AON 전력" 가중치만 0.10 → 0.50으로 바꾸며(다른 가중치는 그대로 두고 다시 정규화) 1위가 바뀌는 지점을 찾아라. 힌트: `w0[0]`을 바꾼 뒤 `w0 /= w0.sum()`. 정답: 0.10이면 A(SoC 단독) 4.24, 0.25~0.40이면 B, 0.50이면 C(micro-NPU MCU) 4.12가 B(4.08)를 넘는다 — 상시 전력을 중시할수록 동반 MCU, 그것도 NPU 달린 쪽으로 기운다.
5. **조사 과제.** 이 노트에서 "확인 필요"로 남긴 항목 하나(예: Wear Elite NPU TOPS의 공식 출처, Exynos W1000의 NPU, Nordic Axon의 성능 수치)를 공식 자료로 확인해 표를 갱신하라. 공식 자료가 없으면 "벤더가 공개하지 않음"이라고 적는다. 힌트: product brief PDF, 데이터시트, 공식 보도자료 순으로 찾는다. 기사 숫자는 2차로 표시.
6. **설계 과제.** 그림 5의 구조에서 동반 MCU를 없애고 SoC의 eNPU만 쓴다고 하자. 펌웨어 쪽에서 사라지는 것과 새로 생기는 위험을 각각 세 개씩 적어라. 정답 예: 사라지는 것 — MCU RTOS, 칩 간 IPC, MCU OTA. 새 위험 — eNPU 툴 접근성, SoC 플랫폼 펌웨어 의존, 상시 전력이 SoC 전원 설계에 묶임.

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| Always-on (AON) | 상시 동작 | 기기가 자는 동안에도 센서를 보고 있는 저전력 영역 |
| 동반 MCU (companion MCU) | 보조 마이크로컨트롤러 | 큰 SoC 옆에서 상시 감지와 wake를 맡는 작은 칩 |
| Helium (MVE) | Cortex-M 벡터 확장 | M55/M85/M52의 128-bit SIMD (E2) |
| micro-NPU | MCU용 작은 NPU | Ethos-U처럼 SRAM 안에서 mW로 도는 NPU |
| Ethos-U55/U65/U85 | Arm micro-NPU IP | 각각 32~256, 256~512, 128~2048 MAC/cycle |
| Vela | Arm NPU 컴파일러 | TFLite int8 모델을 Ethos-U command stream으로 바꾼다 |
| eIQ Neutron | NXP 자체 NPU | MCX N, RT700, i.MX 95에 쓰인다 |
| Neural-ART | ST 자체 NPU | STM32N6의 600 GOPS NPU |
| Axon | Nordic NPU | Atlazo 인수로 얻은 NPU, nRF54LM20B에 첫 탑재 |
| NDP | Neural Decision Processor | Syntiant의 상시 신경망 전용 칩 |
| Hexagon | Qualcomm DSP/NPU 계열 | scalar + 벡터(HVX) + 행렬(HMX), 문맥에 따라 DSP 또는 NPU |
| eNPU / Sensing Hub | Qualcomm 저전력 AI 섬 | SoC를 깨우지 않고 상시 감지를 하는 블록 |
| HiFi | Cadence 오디오 DSP | Xtensa 기반. HiFi1/3/4/5가 여러 웨어러블 칩에 있다 |
| peak TOPS | 이론 최대 연산량 | MAC/cycle × 2 × f. 정밀도·sparse 조건을 반드시 함께 본다 |
| sparse TOPS | 희소성 가정 연산량 | 0인 가중치를 건너뛴다고 가정한 값. dense의 약 2배로 표기된다 |
| byte/op, ridge point | 대역폭 ÷ 연산량, 그 역수 | roofline에서 메모리 한계와 연산 한계가 만나는 곳 (D3) |
| 라이선스 IP | 설계도 판매 | 칩 대신 RTL을 사서 자기 SoC에 넣는다 |
| 2nd source | 대체 공급처 | 같은 기능을 다른 공급사 부품으로 바꿀 수 있는 설계 |
| product brief | 제품 요약서 | 벤더의 공식 1~4쪽 스펙 문서. 데이터시트보다 덜 자세하다 |

---

## 13. 요약 & 체크리스트

웨어러블 AI 기기가 고를 수 있는 칩은 네 세그먼트로 정리된다. Always-on MCU(Ambiq, Nordic, ST, Alif, NXP, Renesas, Infineon, Espressif, Syntiant, GreenWaves)는 mW 안에서 상시 감지를 하고, AI는 Helium SIMD, Arm Ethos-U, 벤더 자체 NPU, 전용 칩 네 방식으로 한다. 웨어러블/AR SoC(Qualcomm W5/Wear Elite/AR1/AR1+/AR2, NXP i.MX 9x, MediaTek Genio, Samsung Exynos W, 참고로 Apple·Google)는 LPDDR와 NPU로 ASR과 SLM을 맡고, 최근에는 eNPU·Sensing Hub로 상시 감지까지 흡수하려 한다. Edge 가속기(Coral, Hailo, Jetson, Axelera)는 W 단위라 웨어러블 본체가 아니라 허브·로봇용이다. 라이선스 IP(Ethos-U/N, Cadence Neo·HiFi, Synopsys NPX6, Ceva NeuPro-Nano, Imagination, Expedera, Quadric, Google Coral NPU)는 커스텀 실리콘의 재료다. Hark 같은 기기의 가장 그럴듯한 구조는 "Qualcomm SoC + Ambiq-class 동반 MCU"이지만(공고 단서 기반 추정), 동반 MCU를 둘지는 SoC 저전력 섬의 실측 전력으로 정해야 한다. 벤더 숫자는 MAC × 2 × f로 역검산하고, 정밀도·sparse 조건을 맞추고, decode는 대역폭으로 상한을 잡는다. 이 지도는 1년이면 바뀐다 — 모든 숫자에 확인일을 붙인다.

- [ ] 네 세그먼트의 이름과 각 세그먼트의 대표 부품 2~3개를 출처와 함께 말할 수 있다
- [ ] MCU가 AI를 하는 네 방식(Helium, Ethos-U, 자체 NPU, 전용 칩)과 장단점을 설명할 수 있다
- [ ] 벤더 TOPS를 MAC/cycle × 2 × f로 손으로 역검산할 수 있다 (RA8P1 256 GOPS, U85 4 TOPS)
- [ ] int4·sparse TOPS를 int8 dense와 비교하면 안 되는 이유를 예(Hailo-10H, Orin)로 말할 수 있다
- [ ] 대역폭과 가중치 크기로 SLM decode tok/s 상한을 계산할 수 있다
- [ ] Qualcomm 웨어러블·AR 라인의 계보(W5+ 동반 칩 → AR1 Sensing Hub → Wear Elite eNPU)를 설명할 수 있다
- [ ] "SoC + 동반 MCU" 구조에서 동반 MCU를 둘지 정하는 측정 항목을 말할 수 있다
- [ ] 커스텀 NPU를 고려할 조건 네 가지와 스타트업 벤더 리스크 다섯 가지를 말할 수 있다
- [ ] 숫자마다 출처 등급(공식/2차/추론/확인 필요)을 구분해서 말할 수 있다

---

## 참고 자료

모든 자료의 확인일은 2026-10-03이다. 제품과 스펙은 바뀐다 — 인용 전에 다시 확인할 것. "2차"는 벤더가 아닌 매체의 정리다.

1. Ambiq, "Ambiq Apollo510 Delivers 30x Power Efficiency Improvement to Unleash Edge AI" (2024-03-26): https://ambiq.com/news/ambiq-apollo510-delivers-30x-power-efficiency-improvement-to-unleash-edge-ai/
2. Ambiq, Apollo510 제품 페이지 (neuralSPOT, heliaRT, "WITHOUT the need for an NPU"): https://ambiq.com/product/apollo510/
3. Ambiq, Apollo4 Plus 제품 페이지: https://ambiq.com/product/apollo4-plus/
4. Ambiq, "Ambiq Democratizes Edge AI with the Apollo330 Plus Series SoCs" (2025-03-04): https://ambiq.com/news/ambiq-democratizes-edge-ai-with-the-apollo330-plus-series-socs/
5. ST Blog, "STM32N6: Our very own NPU in the most powerful STM32" (2024-12): https://blog.st.com/stm32n6/ · ST 보도자료: https://newsroom.st.com/media-center/press-item.html/p4665.html
6. Nordic Semiconductor, "Nordic Semiconductor simplifies edge AI for billions of IoT devices" (2026-01-06): https://www.prnewswire.com/news-releases/nordic-semiconductor-simplifies-edge-ai-for-billions-of-iot-devices-302652914.html
7. Alif Semiconductor, 2세대 Ensemble 보도자료 (2025-01-07): https://alifsemi.com/press-release/alif-semiconductor-pioneers-generative-ai-in-edge-and-endpoint-devices-with-2nd-generation-low-power-ensemble-mcus/
8. CNX Software (2차), "Alif Ensemble E4, E6, and E8 … feature Ethos-U85 NPU" (2025-08-13): https://www.cnx-software.com/2025/08/13/alif-ensemble-e4-e6-and-e8-cortex-m55-a32-mcus-and-mpus-feature-ethos-u85-npu-for-small-language-models-slm/
9. Alif Semiconductor, Balletto 제품 페이지: https://alifsemi.com/products/balletto/
10. Renesas, "Renesas Sets New MCU Performance Bar with 1-GHz RA8P1 Devices with AI Acceleration" (2025-07-01): https://www.renesas.com/en/about/newsroom/renesas-sets-new-mcu-performance-bar-1-ghz-ra8p1-devices-ai-acceleration
11. NXP, MCX N94/N54 제품 페이지: https://www.nxp.com/products/MCX-N94-N54-N53-N52-N24
12. NXP, "NXP's New i.MX RT700 Crossover MCU with eIQ Neutron NPU …" (2024-09): https://www.nxp.com/company/about-nxp/newsroom/NW-NXP-NEW-IMXRT700-CROSSOVER-MCU-EIQ · CNX (2차, 2024-09-27): https://www.cnx-software.com/2024/09/27/nxp-i-mx-rt700-dual-core-cortex-m33-ai-crossover-mcu-includes-eiq-neutron-npu-and-dsps/
13. Infineon, "Infineon announces next-generation PSOC Edge portfolio" (2024-04): https://www.infineon.com/market-news/2024/infcss202404-086
14. Espressif, ESP32-P4 제품 페이지: https://www.espressif.com/en/products/socs/esp32-p4 · 발표 시점 (CNX, 2차, 2023-01-06): https://www.cnx-software.com/2023/01/06/espressif-esp32-p4-a-400-mhz-general-purpose-dual-core-risc-v-microcontroller/
15. Espressif, ESP32-S3 Series Datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
16. Syntiant, "Syntiant Introduces Second Generation NDP120 …" (2021-01-06): https://www.syntiant.com/press-release/syntiant-introduces-second-generation-ndp120-deep-learning-processor-for-audio-and-sensor-apps/
17. Syntiant, "Syntiant Unveils NDP250 Neural Decision Processor with Next-Gen Core 3 Architecture" (2024-04-09): https://www.syntiant.com/press-release/syntiant-unveils-ndp250-neural-decision-processor-with-next-gen-core-3-architecture/
18. GreenWaves Technologies, GAP9: https://greenwaves-technologies.com/gap9_iot_application_processor/ · GlobalFoundries 보도자료 (2020-10-15): https://investors.gf.com/news-releases/news-release-details/greenwaves-technologies-announces-next-generation-gap9-hearables
19. Synaptics, Astra SL2610 보도자료 (2025-10-15): https://www.synaptics.com/company/news/synaptics-launches-next-generation-astra-multimodal-genai-processors-to-power-future-intelligent-iot-edge · SL2610 Product Brief: https://cp.synaptics.com/cognidox/download/NR-160468-PB-APPROVED.pdf
20. Google Developers Blog, "Introducing Coral NPU: A full-stack platform for Edge AI" (2025-10): https://developers.googleblog.com/introducing-coral-npu-a-full-stack-platform-for-edge-ai/ · Google Research 블로그: https://research.google/blog/coral-npu-a-full-stack-platform-for-edge-ai/
21. Qualcomm, Snapdragon W5+ and W5 Gen 1 Wearable Platforms Product Brief (87-43671-1 Rev C): https://docs.qualcomm.com/doc/87-43671-1/87-43671-1_REV_C_Snapdragon_W5__and_Snapdragon_W5_Gen_1_Wearable_Platforms_Product_Brief.pdf · 보도자료 (2022-07): https://www.qualcomm.com/news/releases/2022/07/qualcomm-launches-snapdragon-w5--and-w5-platforms-for-next-gener
22. 9to5Google (2차), "Qualcomm announces Snapdragon W5 Gen 2 …" (2025-08-20): https://9to5google.com/2025/08/20/snapdragon-w5-gen-2/
23. Qualcomm, Snapdragon Wear Elite Platform Product Brief (87-97359-1 Rev C): https://docs.qualcomm.com/doc/87-97359-1/87-97359-1_REV_C_Snapdragon_Wear_Elite_Platform_Product_Brief.pdf · 보도자료 (2026-03): https://www.qualcomm.com/news/releases/2026/03/qualcomm-powers-the-rise-of-personal-ai-with-new-snapdragon-wear
24. Wear Elite 2차 자료 — audioXpress (12 TOPS, eNPU 인용): https://audioxpress.com/news/qualcomm-launches-snapdragon-wear-elite-platform-for-personal-ai-wearable-designs · 9to5Google (10 tok/s, 2026-03-01): https://9to5google.com/2026/03/01/qualcomm-snapdragon-wear-elite/
25. Qualcomm, Snapdragon AR1 Gen 1 Platform Product Brief (87-86507-1 Rev B): https://docs.qualcomm.com/doc/87-86507-1/87-86507-1_REV_B_Snapdragon_AR1_Gen_1_Platform_Product_Brief.pdf · 제품 페이지: https://www.qualcomm.com/xr-vr-ar/products/ar-series/snapdragon-ar1-gen-1-platform · 출시 시점 (EEWorld, 2차, 2023-09): https://en.eeworld.com.cn/news/qrs/eic699315.html
26. Qualcomm OnQ, "A world's first on-glass GenAI demo" (2025-06): https://www.qualcomm.com/news/onq/2025/06/a-worlds-first-on-glass-gen-ai-demo-qualcomms-vision-for-smart-glasses · UploadVR (2차, 2025-06-10): https://www.uploadvr.com/qualcomm-snapdragon-ar1-plus-smart-glasses-chip/
27. Qualcomm, Snapdragon AR2 Gen 1 제품 페이지: https://www.qualcomm.com/xr-vr-ar/products/ar-series/snapdragon-ar2-gen-1-platform · XDA (2차, 2022-11): https://www.xda-developers.com/qualcomm-snapdragon-ar2-gen-1/ · Malay Mail (2차, 2022-11-17): https://www.malaymail.com/news/tech-gadgets/2022/11/17/snapdragon-ar2-gen-1-is-a-purpose-built-headworn-augmented-reality-platform/40318
28. Qualcomm, "Snapdragon X Series is the Exclusive Platform to Power the Next Generation of Windows PCs with Copilot+" (2024-05-20): https://www.qualcomm.com/news/releases/2024/05/snapdragon-x-series-is-the-exclusive-platform-to-power-the-next-
29. NXP, i.MX 93 제품 페이지: https://www.nxp.com/products/i.MX93 · 보도자료: https://www.nxp.com/company/about-nxp/newsroom/NW-NXP-I-MX-93-APPLICATIONS-PROCESSOR-FAM · Hackster (2차, Ethos-U65 첫 AP): https://www.hackster.io/news/nxp-s-i-mx-93-edge-machine-learning-apu-is-the-first-to-include-an-arm-ethos-u65-micronpu-c320bddfc942
30. NXP, i.MX 95 제품 페이지: https://www.nxp.com/products/i.MX95 · 보도자료 (2023-01-04): https://www.nxp.com/company/about-nxp/newsroom/NW-NXP-NEW-IMX95-FAMILY-OF-APPLICATIONS-PROCESSORS
31. MediaTek, Genio 700: https://genio.mediatek.com/genio-700 · Genio 510: https://genio.mediatek.com/genio-510 · Genio 1200 (CNX, 2차): https://www.cnx-software.com/2022/05/13/mediatek-genio-1200-premium-aiot-processor-with-4-8-tops-npu/
32. Samsung Semiconductor, Exynos W1000: https://semiconductor.samsung.com/processor/wearable-processor/exynos-w1000/ · SamMobile (2차): https://www.sammobile.com/news/samsung-exynos-w1000-first-3nm-chip-launched-specs/ · Sammy Fans (2차, 2024-07-03): https://www.sammyfans.com/2024/07/03/samsung-launches-3nm-exynos-w1000-wearable-processor/
33. Apple Newsroom, "Introducing Apple Watch Series 10" (2024-09): https://www.apple.com/newsroom/2024/09/introducing-apple-watch-series-10/ · "Apple unveils iPhone 15 Pro and iPhone 15 Pro Max" (A17 Pro, 2023-09): https://www.apple.com/newsroom/2023/09/apple-unveils-iphone-15-pro-and-iphone-15-pro-max/
34. Google Blog, "5 reasons why Google Tensor G5 is a game-changer for Pixel" (2025-08): https://blog.google/products-and-platforms/devices/pixel/tensor-g5-pixel-10/ · 9to5Google (2차, 2025-08-20): https://9to5google.com/2025/08/20/pixel-10-tensor-g5/
35. Hailo, AI Accelerators (Hailo-8, Hailo-8L): https://hailo.ai/products/ai-accelerators/
36. Hailo, Hailo-10H: https://hailo.ai/products/ai-accelerators/hailo-10h-ai-accelerator/ · Hailo-10 발표 (2024-04): https://hailo.ai/company-overview/newsroom/news/hailo-closes-new-120-million-funding-round-and-debuts-hailo-10-a-new-powerful-ai-accelerator-bringing-generative-ai-to-edge-devices/
37. Coral Accelerator Module Datasheet v1.4 (Mouser 사본): https://www.mouser.com/datasheet/2/963/Coral_Accelerator_Module_datasheet-1888467.pdf · Coral (현재 Google Developers): https://developers.google.com/coral
38. NVIDIA Technical Blog, "NVIDIA Jetson Orin Nano Developer Kit Gets a 'Super' Boost" (2024-12): https://developer.nvidia.com/blog/nvidia-jetson-orin-nano-developer-kit-gets-a-super-boost/ · CNX (2차, 2024-12-18): https://www.cnx-software.com/2024/12/18/249-nvidia-jetson-orin-nano-super-developer-kit-targets-generative-ai-applications-at-the-edge/
39. Axelera AI, AI accelerators (Metis, Europa): https://axelera.ai/ai-accelerators
40. Arm Newsroom, "Arm Accelerates Edge AI with Latest Generation Ethos-U NPU and New IoT Reference Design Platform" (2024-04-09): https://newsroom.arm.com/news/iot-reference-design-platform-2024
41. Arm, ML Developers Guide for Cortex-M Processors and Ethos-U NPU — Ethos-U hardware architecture: https://developer.arm.com/documentation/109267/0103/Arm-Ethos-U-NPU/Ethos-U-hardware-architecture
42. Arm, Ethos-U Integration for Cortex-M: Vela: https://arm-software.github.io/CMSIS-Ethos-U/main/vela/index.html
43. Arm, Ethos-N driver stack (GitHub): https://github.com/Arm-software/ethos-n-driver-stack · Ethos-N78: https://developer.arm.com/ip-products/processors/machine-learning/arm-ethos-n/ethos-n78 · Jon Peddie Research (2차): https://www.jonpeddie.com/news/arm-ethos-n78-scales-npu-ip-to-10-tops/
44. Cadence, "Cadence Accelerates On-Device and Edge AI Performance and Efficiency with New Neo NPU IP and NeuroWeave SDK" (2023-09-14): https://www.cadence.com/en_US/home/company/newsroom/press-releases/pr/2023/cadence-accelerates-on-device-and-edge-ai-performance-and.html
45. Synopsys, "Synopsys Introduces Industry's Highest Performance Neural Processor IP" (2022-04-19): https://news.synopsys.com/2022-04-19-Synopsys-Introduces-Industrys-Highest-Performance-Neural-Processor-IP
46. Ceva, NeuPro-Nano: https://www.ceva-ip.com/product/ceva-neupro-nano/
47. Edge AI and Vision Alliance (Imagination 보도자료 게재), "Imagination Announces E-Series" (2025-05): https://www.edge-ai-vision.com/2025/05/imagination-announces-e-series-a-new-era-of-on-device-ai-and-graphics/
48. Expedera, "Expedera's Origin Evolution NPU IP Brings Generative AI to Edge Devices" (2025-05-20): https://www.expedera.com/blog/2025/05/20/expederas-origin-evolution-npu-ip-brings-generative-ai-to-edge-devices/
49. Quadric, NPU IP (Chimera GPNPU): https://quadric.ai/npu-ip · Kyocera 라이선스 (2025-05): https://www.businesswire.com/news/home/20250507874323/en/Kyocera-Licenses-Quadrics-Chimera-GPNPU-AI-Processor-IP
50. Analog Devices, Form 8-K Exhibit 99.1 — Alif Semiconductor 인수 발표 (2026-09-09): https://www.sec.gov/Archives/edgar/data/0000006281/000119312526385938/d924057dex991.htm · Alif 공지: https://alifsemi.com/alif-semiconductor-to-join-analog-devices/
51. Ambiq, "Ambiq Announces Pricing of Upsized Initial Public Offering" (2025-07-29): https://ambiq.com/news/ambiq-announces-pricing-of-initial-public-offering/
52. Zephyr Project, MIMXRT700-EVK 보드 문서: https://docs.zephyrproject.org/latest/boards/nxp/mimxrt700_evk/doc/index.html
53. Electronics Industry Magazine (2차), "Alif Semiconductor elevates generative AI at the edge with new support for ExecuTorch Runtime": https://www.allelectronicsindustry.com/features/alif-semiconductor-elevates-generative-ai-at-the-edge-with-new-support-for-executorch-runtime-in-its-ensemble-mcus/

내부 자료 (이 작업공간, 읽기만 함): `job_interview_prep_research/hark_ai_embedded_swe/hark_ai_embedded_swe_context.md` §2.2 — Hark 채용 공고 단서. 이 노트 세트: E2, E3, E4, E5, E6, E8, F4, F6, D3, D5, I1, I3, L1, M1, M2, M4.
