# M4. 데이터시트 해석 — TOPS·전력·대역폭 숫자에 숨은 조건 읽어내기

> **이 노트를 다 읽으면**: 보도자료·product brief·datasheet·reference manual·errata를 구분해서 "어느 숫자를 믿을지" 정할 수 있다 · 실제 공개 자료 7종(STM32N6, Ethos-U85·Alif, Apollo510, Hailo-8, Coral, Jetson Orin, Snapdragon AR1)의 문장을 한 줄씩 해부해 "측정값인지, 이론값인지, 무엇이 빠졌는지"를 말할 수 있다 · 광고 TOPS를 dense INT8 한 엔진 기준으로 정규화하고, 대역폭과 엮어 ridge point와 우리 모델의 지연 범위를 낼 수 있다 · µW 헤드라인을 우리 duty cycle의 평균 전력으로 바꾸고, 빠진 정보를 FAE 질문 목록으로 만들 수 있다
> **JD 연결**: "Evaluate and select silicon platforms (GPUs, NPUs etc.)" — study_prep_list **M4**: TOPS에 숨은 조건 (INT4, sparsity, peak), 실측과 스펙의 차이, "벤더 마케팅 걸러내기". 함께 닿는 행: **M1**(평가 기준표), **M2**(벤치마크 방법론), **M3**(벤더 지형), **PJ8**(가상 실리콘 선정 보고서)
> **Don 기준 난이도**: SSD 컨트롤러·NAND datasheet와 errata 읽기, RF 칩셋 스펙을 온도·전압 corner로 검증하는 일은 이미 몸에 배어 있다 / 새로 배울 것은 ML 가속기 특유의 단위(TOPS·TOPs·GOPS, op 대 MAC, sparse, INT4), "AI TOPS" 합산 관행, 그리고 그 숫자를 우리 모델의 ms·µW로 옮기는 정규화 절차
> **선행 노트**: D3(roofline·ridge point), D7 5절(TOPS/W의 다섯 가지 숨은 조건, "10 TOPS/W → 11배"), C4 9절(sparse TOPS), E5(모양에 따른 활용률), E7(LPDDR 대역폭 계산), E9(저전력 모드·retention), I5 3절(벤더 숫자가 숨기는 것, TOPS → ms 손계산). 이 노트는 그 유도를 **반복하지 않고** 결과만 가져다 쓴다.

---

## 0. 큰 그림 — 이게 왜 필요한가

D7·I5·C4에서 "TOPS 숫자에는 조건이 숨어 있다"는 것을 **가상의 스펙**으로 배웠다. 이 노트는 그 다음 단계다. 진짜 벤더가 진짜로 공개한 문장을 앞에 놓고, 한 줄씩 읽으면서 다음 세 가지를 판정한다.

1. 이 숫자는 **측정값**인가, **이론값(MAC 수 × clock)**인가, 아니면 **비교 배수**("30x")인가?
2. 숫자에 붙은 **조건**(정밀도, sparsity, 어느 엔진, 어떤 clock·전압·온도, 어떤 rail, typ인지 max인지)은 어디에 적혀 있고, 무엇이 **빠져 있나**?
3. 빠진 것을 채우려면 **누구에게 무엇을** 물어야 하나?

예를 들어 Hark 같은 웨어러블을 만든다고 하자(추정 예시). 후보 칩 셋을 놓고 "어느 칩이 wake word를 0.5 mW 안에서, 사용자 질문의 첫 토큰을 1초 안에 낼 수 있나"를 결정해야 한다. 그런데 손에 있는 것은 보도자료의 "600 GOPS", product brief의 "typical 2.5W", 제품 페이지의 "67 TOPS" 정도다. 이 숫자들은 **같은 단위처럼 보이지만 같은 것을 재지 않는다.** 이 노트의 목표는 이것들을 **같은 자(ruler)** 위에 올리는 것이다.

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
<rect x="10" y="60" width="86" height="70" rx="6" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="2"/><text x="53.0" y="86" font-size="13" text-anchor="middle">보도자료·</text><text x="53.0" y="102" font-size="13" text-anchor="middle">제품 페이지</text>
<text x="53.0" y="122" font-size="12" text-anchor="middle">'up to' 숫자</text><line x1="96" y1="95" x2="104" y2="95" stroke="currentColor"/><rect x="104" y="60" width="86" height="70" rx="6" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="2"/>
<text x="147.0" y="86" font-size="13" text-anchor="middle">product</text><text x="147.0" y="102" font-size="13" text-anchor="middle">brief</text><text x="147.0" y="122" font-size="12" text-anchor="middle">조건 생략</text>
<line x1="190" y1="95" x2="198" y2="95" stroke="currentColor"/><rect x="198" y="60" width="86" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="2"/><text x="241.0" y="94" font-size="13" text-anchor="middle">datasheet</text>
<text x="241.0" y="122" font-size="12" text-anchor="middle">min·typ·max</text><line x1="284" y1="95" x2="292" y2="95" stroke="currentColor"/><rect x="292" y="60" width="86" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="2"/>
<text x="335.0" y="86" font-size="13" text-anchor="middle">reference</text><text x="335.0" y="102" font-size="13" text-anchor="middle">manual</text><text x="335.0" y="122" font-size="12" text-anchor="middle">동작·레지스터</text>
<line x1="378" y1="95" x2="386" y2="95" stroke="currentColor"/><rect x="386" y="60" width="86" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="2"/><text x="429.0" y="94" font-size="13" text-anchor="middle">app note</text>
<text x="429.0" y="122" font-size="12" text-anchor="middle">측정법·예제</text><line x1="472" y1="95" x2="480" y2="95" stroke="currentColor"/><rect x="480" y="60" width="86" height="70" rx="6" fill="#d0564a" fill-opacity="0.15" stroke="#d0564a" stroke-width="2"/>
<text x="523.0" y="94" font-size="13" text-anchor="middle">errata</text><text x="523.0" y="122" font-size="12" text-anchor="middle">알려진 결함</text><line x1="566" y1="95" x2="574" y2="95" stroke="currentColor"/>
<rect x="574" y="60" width="86" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b" stroke-width="2"/><text x="617.0" y="86" font-size="13" text-anchor="middle">우리 실측</text><text x="617.0" y="102" font-size="13" text-anchor="middle">EVB</text>
<text x="617.0" y="122" font-size="12" text-anchor="middle">우리 모델</text><line x1="20" y1="160" x2="650" y2="160" stroke="currentColor" stroke-width="1.5"/><polygon points="650,160 640,155 640,165" fill="currentColor"/>
<text x="335" y="182" font-size="13" text-anchor="middle">오른쪽으로 갈수록: 조건이 구체적 · 측정 근거가 있음 · 늦게 나옴 · NDA가 필요할 수 있음</text><line x1="650" y1="34" x2="20" y2="34" stroke="currentColor" stroke-width="1.5"/><polygon points="20,34 30,29 30,39" fill="currentColor"/>
<text x="335" y="24" font-size="13" text-anchor="middle">왼쪽으로 갈수록: 먼저 보이고, 숫자가 크고, 조건이 적다</text><text x="335" y="212" font-size="12" text-anchor="middle">선정 결정은 가장 오른쪽 두 칸(errata를 반영한 우리 실측)으로 내린다</text>
</svg>
```

그림 1 — 칩 문서의 사다리. 왼쪽 문서는 먼저 공개되고 숫자가 크며 조건이 적다. 오른쪽으로 갈수록 조건이 구체적이고 측정 근거가 있지만, 늦게 나오거나 NDA가 필요하다. 선정 결정은 errata를 반영한 우리 실측으로 내린다.

Don에게 익숙한 비유로 말하면: SSD 제품 페이지의 "순차 읽기 최대 7 GB/s"는 왼쪽 끝 숫자다. 고객사 qualification에서 실제로 쓰는 것은 datasheet의 QD·전송 크기·SLC 캐시 조건, NAND datasheet의 tPROG·tR **typ/max**, 그리고 컨트롤러 **errata** 시트다. RF 칩셋 통합에서도 "TX power +20 dBm"은 25 °C, 공칭 전압, 특정 채널에서의 typ 값이고, sign-off는 온도·전압 corner 전체의 min/max로 한다. ML 가속기라고 다르지 않다. **단위와 관행만 새롭다.**

이 노트의 흐름:

```
1절  문서 종류와 신뢰도, datasheet 표의 해부 (typ/max, 조건 열, 각주)
2절  단위 함정 사전 (TOPS·TOPs·GOPS, op/MAC, dense/sparse, INT4/INT8, 합산 TOPS)  ← 예제 1
3절  실제 공개 자료 7건 사례 연구 (문장 인용 → 실제 의미 → 빠진 것)
4절  정규화 방법과 결과표                                                    ← 예제 2
5절  메모리 스펙 읽기와 ridge point                                          ← 예제 3
6절  우리 모델(KWS, MobileNetV2, 0.5B LLM)로 지연 범위 추정, 실측 벤치로 검산  ← 예제 4, 5
7절  전력 숫자 읽기 — typ/max, rail, µW/MHz, sleep + retention, duty cycle   ← 예제 6, 7
8절  벤치마크 주장 읽기
9절  빠진 정보 → 벤더 FAE 질문 (템플릿 24개)                                 ← 예제 8
10절 red flag — benchmarketing 패턴
```

---

## 1. 문서의 해부 — 무엇을 얼마나 믿나

### 1.1 문서 종류별 성격

칩 하나에 붙는 문서는 여러 종류다. 이름이 비슷해도 목적과 책임 수준이 다르다.

| 문서 | 누가·왜 쓰나 | 숫자의 성격 | 얼마나 믿나 |
|---|---|---|---|
| 보도자료·제품 페이지 | 마케팅. 발표 시점에 관심 끌기 | "up to", 배수("600x"), 헤드라인 1~2개 | 방향만. 조건이 거의 없다 |
| product brief (1~2쪽 PDF) | 마케팅 + 영업. 후보 목록에 오르기 | 기능 목록, 대표 숫자, 각주 약간 | 기능 유무 확인용. 숫자는 질문의 출발점 |
| datasheet | 제품 엔지니어링. 전기적 특성의 **약속** | min/typ/max, 측정 조건 열, 각주 | 높음. 단 typ는 약속이 아니다 |
| reference manual (RM, TRM) | 설계·SW 팀. 레지스터와 동작 | 동작 모드, 버스 폭, clock tree, 전원 도메인 | 구조적 사실은 높음. 성능 숫자는 거의 없다 |
| application note | FAE·응용팀. 사용 예와 측정법 | 특정 보드·조건의 측정값 | 조건이 적혀 있으면 높음. 우리 조건과 다를 수 있다 |
| errata (silicon errata sheet) | 품질·제품팀. 알려진 결함과 workaround | "이 모드에서 이 기능이 안 된다" | 매우 높음. **선정 전에 반드시 읽는다** |
| SDK·컴파일러 release note | SW 팀 | 지원 op, 알려진 문제, 성능 회귀 | 툴체인 성숙도의 실제 증거 |
| 공개 벤치마크(MLPerf 등) | 제3자 규칙 아래 제출 | 정해진 모델·시나리오·정확도 목표 | 규칙이 있어 비교가 가능. 단 우리 모델이 아니다 (I5) |

말로 하면: 왼쪽 문서일수록 "할 수 있다"를 말하고, 오른쪽 문서일수록 "어떤 조건에서 얼마나, 그리고 언제 안 되는지"를 말한다. 선정 단계에서 보도자료 숫자로 후보를 **추리는** 것은 괜찮다. 하지만 **결정**은 datasheet의 조건 열, errata, 그리고 우리 모델의 실측으로 한다.

### 1.2 datasheet의 전기적 특성 표 — 한 행을 해부하기

ML 가속기 숫자를 읽기 전에, Don이 이미 아는 형태부터 다시 짚는다. MCU datasheet의 전류 소비 표는 보통 이렇게 생겼다(형태 예시, 숫자는 가상).

```
Table 23. Current consumption in Stop mode                       ← 표 제목에 '어떤 모드'
Symbol  Parameter        Conditions                    Typ   Max(1)  Unit
                         ─────────────────────────────  ───── ─────── ────
IDD     Supply current   RTC on, LSE, 64 KB SRAM ret.  2.9   11      µA
        in Stop mode     VDD = 3.0 V, TA = 25 °C       ↑     ↑
                                                       │     └─ (1) "Guaranteed by characterization,
                                                       │            not tested in production" 같은 각주
                                                       └─ 보통 25 °C·공칭 전압·대표 부품의 '전형값'
                         RTC on, LSE, 64 KB SRAM ret.  9.8   —       µA   ← 같은 모드, 온도만 다름
                         VDD = 3.0 V, TA = 60 °C
```

읽는 순서는 항상 같다.

1. **Parameter와 모드**: 무엇을 재나? "Stop mode"가 이 칩에서 무엇을 끄고 무엇을 남기는 모드인지는 RM의 전원 모드 장에서 확인한다(E9 6절).
2. **Conditions 열**: 무엇이 켜져 있나(RTC, 발진기, retention SRAM 크기, wake source), 전압, 온도.
3. **Typ과 Max**: typ는 보통 "25 °C, 공칭 전압에서 대표 부품의 값"이다. 약속이 아니다. max는 "이 조건 범위 전체에서 넘지 않는다"는 약속이며, 생산 테스트로 보증하는지 특성 평가(characterization)로만 보증하는지 각주가 말해 준다.
4. **각주**: "design guaranteed", "not tested in production", "measured on evaluation board", "excluding I/O current" 같은 문장이 숫자의 의미를 바꾼다.

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
<polyline points="67.2,220.0 76.7,220.0 86.1,220.0 95.5,220.0 105.0,219.2 114.4,213.2 123.8,194.5 133.2,162.6 142.7,128.9 152.1,107.1 161.5,103.3 170.9,114.6 180.4,134.1 189.8,155.4 199.2,174.3 208.6,189.2 218.1,200.0 227.5,207.4 236.9,212.3 246.3,215.3 255.8,217.2 265.2,218.4 274.6,219.1 284.0,219.5 293.5,219.7 302.9,219.8 312.3,219.9 321.7,219.9 331.2,220.0 340.6,220.0 350.0,220.0 359.4,220.0 368.8,220.0 378.3,220.0 387.7,220.0 397.1,220.0 406.5,220.0 416.0,220.0 425.4,220.0 434.8,220.0 444.2,220.0 453.7,220.0 463.1,220.0 472.5,220.0 482.0,220.0 491.4,220.0 500.8,220.0 510.2,220.0 519.6,220.0 529.1,220.0 538.5,220.0 547.9,220.0 557.3,220.0 566.8,220.0 576.2,220.0 585.6,220.0 595.0,220.0 604.5,220.0 613.9,220.0 623.3,220.0 632.8,220.0" fill="none" stroke="#4a7bd0" stroke-width="2"/><polyline points="67.2,220.0 76.7,220.0 86.1,220.0 95.5,220.0 105.0,220.0 114.4,220.0 123.8,220.0 133.2,220.0 142.7,220.0 152.1,220.0 161.5,220.0 170.9,220.0 180.4,220.0 189.8,220.0 199.2,220.0 208.6,220.0 218.1,220.0 227.5,220.0 236.9,220.0 246.3,220.0 255.8,220.0 265.2,220.0 274.6,220.0 284.0,219.9 293.5,219.6 302.9,218.9 312.3,217.2 321.7,213.8 331.2,207.8 340.6,198.5 350.0,185.6 359.4,169.5 368.8,151.6 378.3,133.9 387.7,118.5 397.1,107.6 406.5,102.2 416.0,102.9 425.4,109.0 434.8,119.4 444.2,132.4 453.7,146.6 463.1,160.6 472.5,173.5 482.0,184.7 491.4,193.9 500.8,201.2 510.2,206.8 519.6,211.0 529.1,213.9 538.5,216.0 547.9,217.4 557.3,218.4 566.8,219.0 576.2,219.4 585.6,219.6 595.0,219.8 604.5,219.9 613.9,219.9 623.3,220.0 632.8,220.0" fill="none" stroke="#d0564a" stroke-width="2"/><line x1="60" y1="220" x2="640" y2="220" stroke="currentColor"/>
<line x1="60.0" y1="220" x2="60.0" y2="224" stroke="currentColor"/><text x="60.0" y="237.0" font-size="12" text-anchor="middle">0</text><line x1="132.5" y1="220" x2="132.5" y2="224" stroke="currentColor"/><text x="132.5" y="237.0" font-size="12" text-anchor="middle">2</text><line x1="205.0" y1="220" x2="205.0" y2="224" stroke="currentColor"/><text x="205.0" y="237.0" font-size="12" text-anchor="middle">4</text>
<line x1="277.5" y1="220" x2="277.5" y2="224" stroke="currentColor"/><text x="277.5" y="237.0" font-size="12" text-anchor="middle">6</text><line x1="350.0" y1="220" x2="350.0" y2="224" stroke="currentColor"/><text x="350.0" y="237.0" font-size="12" text-anchor="middle">8</text><line x1="422.5" y1="220" x2="422.5" y2="224" stroke="currentColor"/><text x="422.5" y="237.0" font-size="12" text-anchor="middle">10</text>
<line x1="495.0" y1="220" x2="495.0" y2="224" stroke="currentColor"/><text x="495.0" y="237.0" font-size="12" text-anchor="middle">12</text><line x1="567.5" y1="220" x2="567.5" y2="224" stroke="currentColor"/><text x="567.5" y="237.0" font-size="12" text-anchor="middle">14</text><line x1="640.0" y1="220" x2="640.0" y2="224" stroke="currentColor"/><text x="640.0" y="237.0" font-size="12" text-anchor="middle">16</text>
<line x1="165.1" y1="40" x2="165.1" y2="220" stroke="#4a7bd0" stroke-dasharray="4 3"/><text x="171.1" y="36.0" font-size="12" text-anchor="start">typ 2.9 (25 C 중앙값)</text><line x1="567.5" y1="40" x2="567.5" y2="220" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="561.5" y="36.0" font-size="12" text-anchor="end">max 14 (전 온도·전압 corner 보증)</text><text x="190.5" y="110.0" font-size="12" text-anchor="start">25 C 부품 분포</text><text x="444.2" y="110.0" font-size="12" text-anchor="start">60 C 분포</text>
<text x="350.0" y="258.0" font-size="13" text-anchor="middle">sleep 전류 (µA) — 가상의 분포: typ는 '보통', max는 '보증'</text>
</svg>
```

그림 2 — typ와 max의 의미(가상의 분포). typ 2.9 µA는 25 °C 부품 분포의 중앙값이고, 60 °C에서는 분포 전체가 오른쪽으로 이동한다. max는 온도·전압·공정 corner 전체를 덮는 보증선이다. 웨어러블은 피부 온도 근처(30 °C 대 중반)에서 살기 때문에 25 °C typ만으로 배터리를 계산하면 낙관적이다.

### 1.3 ML 가속기 문서가 MCU 문서와 다른 점

MCU의 전류 표는 수십 년 된 관행이라 조건 열이 꼼꼼하다. 반면 **NPU 성능 숫자에는 아직 업계 공통의 표기 규칙이 없다.** 그래서 같은 "TOPS"라도:

- 어떤 회사는 dense INT8, 어떤 회사는 sparse INT8, 어떤 회사는 INT4를 헤드라인에 쓴다.
- 어떤 회사는 NPU 한 개, 어떤 회사는 GPU + 가속기 합, 어떤 회사(PC 업계)는 CPU + GPU + NPU 합을 쓴다.
- 대부분 1 MAC = 2 op로 세지만, 그 사실을 문서에 적는 회사는 드물다.
- 전력은 "typical"만 있고 어떤 모델·어떤 rail·어떤 온도인지 없는 경우가 많다.

MLPerf 같은 제3자 벤치마크(I5 2절)가 존재하는 이유가 바로 이것이다. 이 노트에서는 문서 자체에서 최대한 뽑아내고, 못 뽑는 것은 질문으로 바꾼다.

### 1.4 Don의 경험과 연결 — "숫자의 출생증명서"

SSD 펌웨어 시절의 습관을 그대로 옮기면 된다.

| SSD·RF에서 하던 일 | ML 가속기에서의 대응 |
|---|---|
| NAND datasheet의 tPROG typ/max를 보고 최악 쓰기 지연을 잡음 | NPU의 peak TOPS가 아니라 sustained clock과 우리 모델 활용률로 최악 지연을 잡음 |
| 컨트롤러 errata에서 "특정 QD·전송 크기 조합에서 DMA hang"을 찾아 우회 | NPU errata·SDK release note에서 "특정 op·shape에서 잘못된 결과 / CPU fallback"을 찾음 |
| "7 GB/s"가 QD32·128 KB 순차·SLC 캐시 안에서만 나온다는 것을 앎 | "67 TOPS"가 sparse·GPU 전체·최고 전력 모드 숫자라는 것을 앎 |
| RF TX power를 온도·전압·채널 corner로 sign-off | NPU 지연·전력을 온도(throttling)·배터리 전압·모델 변형 corner로 sign-off |
| 전류는 어느 rail에서 쟀나(VCC, VCCQ, 1.2 V core?) 확인 | NPU 전력이 NPU rail만인지, DRAM·PMIC 손실 포함인지 확인 |

---

## 2. 단위 함정 사전

### 2.1 한 장 요약

| 표기 | 실제 뜻 | 함정 |
|---|---|---|
| TOPS | Tera Operations Per Second, 초당 10¹² op | "op"가 무엇인지 정의가 없다 |
| TOPs | 보통 TOPS와 같은 뜻으로 쓴다 (Arm 페이지 표기) | 복수형 "operations"인지 "per second"인지 헷갈린다. 문맥상 속도인지 확인 |
| GOPS, GOP/s | 초당 10⁹ op. 1 TOPS = 1,000 GOPS | MCU NPU는 GOPS로 쓰는 경우가 많아 TOPS와 바로 비교하면 1000배 착각 |
| MAC/cycle | 한 cycle에 하는 곱셈-누산 수 | 구성(configuration)마다 다르다. IP 이름만으로 성능을 모른다 |
| 1 MAC = 2 op | 곱 1 + 덧셈 1 | 거의 모든 벤더가 이렇게 센다. 그래서 TOPS ÷ 2 = 초당 MAC |
| dense / sparse | sparse는 2:4 구조적 sparsity로 0을 건너뛴 연산까지 센 값 | sparse = dense × 2. 우리 모델이 2:4로 재학습되지 않았으면 dense로 읽는다 (C4 9절) |
| INT4 / INT8 / FP16 | 연산 정밀도 | INT4 TOPS는 보통 INT8의 2배로 적힌다. 모델 정밀도에 맞춰 읽는다 |
| AI TOPS, platform TOPS | 여러 엔진(CPU·GPU·NPU·DLA)의 합 | 한 모델이 모든 엔진을 동시에 100 % 쓰는 일은 없다 |
| up to | 최고 구성·최고 clock·최적 조건 | 기본값이 아니다. 어떤 SKU·모드인지 확인 |
| "Nx better" | 비교 배수 | 비교 대상(이전 세대? 경쟁사? CPU?)과 지표(지연? 에너지? 처리량?)가 숨어 있다 |

### 2.2 검산 공식 — MAC × 2 × clock

광고 숫자가 이론값인지 확인하는 가장 빠른 방법은 공식에 넣어 보는 것이다.

```
peak ops/s = MAC 유닛 수 × 2 (op/MAC) × clock (Hz)
예) 2048 MAC × 2 × 1 GHz = 4.096 × 10¹² = 4.1 TOPS
```

말로 하면: 모든 MAC 유닛이 매 cycle 쉬지 않고 일할 때의 상한이다. 광고 숫자가 이 공식과 딱 맞으면 그 숫자는 **측정값이 아니라 설계 사양의 곱셈**이다. 셋 중 하나가 공개되지 않았으면 나머지 둘로 역산할 수 있고, 역산값이 이상하면 질문거리다.

무엇을 확인하나: 3절에서 볼 실제 공개 숫자들을 공식에 넣어 일치 여부를 보고, 빠진 값을 역산한다.

```python
# 광고 숫자가 "MAC 수 × 2 op × clock"과 맞는지 검산한다 (모르는 값은 역산)
claims = [  # (이름, 광고 ops/s, MAC/cycle, clock Hz)  None = 공개 자료에 없음
    ("STM32N6 Neural-ART",      600e9,  300,  None),   # "nearly 300" MAC, clock은 보도자료에 없음
    ("Ethos-U85 128-MAC cfg",   256e9,  128,  1e9),
    ("Ethos-U85 2048-MAC cfg",  4e12,   2048, 1e9),
    ("Apollo510 Helium (CPU)",  None,   8,    250e6),  # "up to 8 MACs per cycle", "up to 250MHz"
    ("Alif Ensemble 2x U55",    250e9,  None, 400e6),  # "250+ GOPS", "to 400MHz"
    ("Coral Edge TPU",          4e12,   None, None),
]
for name, ops, mac, f in claims:
    if ops and mac and f:
        calc = 2 * mac * f
        print(f"{name:24s} claim {ops/1e9:7.1f} GOPS  calc {calc/1e9:7.1f} GOPS  ratio {ops/calc:.3f}")
    elif mac and f:
        print(f"{name:24s} no claim       calc {2*mac*f/1e9:7.1f} GOPS  (= 8 MAC x 2 x 250 MHz)")
    elif ops and mac:
        print(f"{name:24s} implied clock = {ops/(2*mac)/1e6:7.1f} MHz")
    elif ops and f:
        print(f"{name:24s} implied MAC/cycle = {ops/(2*f):7.1f}")
    else:
        print(f"{name:24s} implied MAC/cycle x clock = {ops/2:.2e} MAC/s (분해 불가)")
# Alif: Ethos-U55 구성은 32/64/128/256 MAC/cycle. 두 개 합이 312.5 근처가 되는 조합은?
cfg = [32, 64, 128, 256]
for a in cfg:
    for b in cfg:
        if a >= b:
            for fb in (400e6, 160e6):
                g = (a * 400e6 + b * fb) * 2 / 1e9
                if 240 <= g <= 320:
                    print(f"  U55-{a}@400MHz + U55-{b}@{fb/1e6:.0f}MHz -> {g:6.1f} GOPS")
```

```text
STM32N6 Neural-ART       implied clock =  1000.0 MHz
Ethos-U85 128-MAC cfg    claim   256.0 GOPS  calc   256.0 GOPS  ratio 1.000
Ethos-U85 2048-MAC cfg   claim  4000.0 GOPS  calc  4096.0 GOPS  ratio 0.977
Apollo510 Helium (CPU)   no claim       calc     4.0 GOPS  (= 8 MAC x 2 x 250 MHz)
Alif Ensemble 2x U55     implied MAC/cycle =   312.5
Coral Edge TPU           implied MAC/cycle x clock = 2.00e+12 MAC/s (분해 불가)
  U55-256@400MHz + U55-64@400MHz ->  256.0 GOPS
  U55-256@400MHz + U55-128@400MHz ->  307.2 GOPS
  U55-256@400MHz + U55-128@160MHz ->  245.8 GOPS
  U55-256@400MHz + U55-256@160MHz ->  286.7 GOPS
```

출력에서 볼 것:

- Ethos-U85는 공식과 거의 정확히 맞는다(128 MAC → 256 GOP/s). **이론값**이다. 2048 MAC 구성은 정확히는 4.096인데 "4 TOPs"로 반올림해 적었다.
- STM32N6의 600 GOPS는 "nearly 300" MAC이면 **NPU clock 약 1 GHz**를 뜻한다. 보도자료에는 clock이 없으므로 이것은 **역산**이다. datasheet에서 확인할 값이다.
- Apollo510은 TOPS를 광고하지 않는다. 대신 "up to 8 MACs per cycle"과 "up to 250MHz"를 곱하면 **4 GOPS**(CPU SIMD peak)가 나온다. 1 TOPS의 1/250이다. 칩의 성격(NPU 없는 초저전력 MCU)을 숫자가 그대로 보여 준다.
- Alif의 "250+ GOPS"는 두 NPU 합이다. Ethos-U55 구성(32/64/128/256 MAC)과 clock 조합 중 250 근처가 여러 개 나온다. **공개 페이지만으로는 구성을 확정할 수 없다** — 그 자체가 질문거리다.

### 2.3 합산 TOPS — 왜 SoC는 CPU + GPU + NPU를 더하나

PC·고성능 SoC 업계는 "platform TOPS" 또는 "AI TOPS"라는 이름으로 여러 엔진의 peak를 더해 쓴다. 이유는 단순하다. **숫자가 커지고, 경쟁사도 그렇게 쓰기 때문이다.** 공학적으로 말이 안 되는 것은 아니다 — 서로 다른 모델을 서로 다른 엔진에 동시에 올리면 시스템 전체 처리량은 합에 가까워질 수 있다. 하지만 **모델 하나의 지연**에는 의미가 없다.

- 한 모델의 한 layer는 한 엔진에서 돈다. 엔진 사이로 tensor를 넘기면 메모리 복사·동기화 비용이 생긴다.
- 엔진들은 같은 DRAM 대역폭과 같은 전력·열 예산을 나눠 쓴다. 동시에 모두 peak를 낼 수 없다.
- 엔진마다 지원 정밀도·op가 다르다(GPU는 FP16, NPU는 INT8, CPU는 무엇이든 느리게).

그래서 읽는 법: **합산 숫자를 보면 엔진별 표를 찾는다.** 3.6절의 Jetson처럼 엔진별 표를 공개하는 벤더도 있고, 2차 보도에서만 분해가 나오는 경우도 있다.

### 2.4 흔한 혼동 세 가지 — 손으로 한 번씩

```
(a) "600 GOPS NPU"와 "4 TOPS NPU" 비교
    600 GOPS = 0.6 TOPS → 4 TOPS가 6.7배.  (GOPS를 TOPS로 착각하면 150배 앞선 칩으로 보인다)

(b) "67 sparse INT8 TOPS" → dense INT8
    67 ÷ 2 = 33.5 → 벤더 표의 dense 값 33 TOPS와 일치

(c) "10 INT4 TOPS"와 "8 INT8 TOPS"
    INT4가 INT8 대비 2배 rate인 구조라면 10 INT4 TOPS ≈ 5 INT8 TOPS → 실제로는 8 INT8 쪽이 1.6배 빠르다
```

---

## 3. 사례 연구 — 실제 공개 자료를 한 줄씩

규칙: 인용은 원문 그대로 짧게(25단어 이하) 옮기고, 확인일은 모두 **2026-10-03**이다. 원문을 직접 확인하지 못하고 요약 도구·2차 보도에 의존한 부분은 그렇다고 표시했다. 벤더 페이지는 수시로 바뀌므로, 실제 선정 때는 최신 datasheet로 다시 확인해야 한다.

### 3.1 STM32N6 (ST) — MCU + 자체 NPU, 보도자료

출처: ST 보도자료 "STMicroelectronics to boost AI at the edge with new NPU-accelerated STM32 microcontrollers" — [newsroom.st.com/media-center/press-item.html/p4665.html](https://newsroom.st.com/media-center/press-item.html/p4665.html) (확인일 2026-10-03)

| 원문 (짧은 인용) | 실제 뜻 | 빠진 것 |
|---|---|---|
| "delivering 600 times more machine-learning performance than a high-end STM32 MCU today" | 비교 배수. 기준은 "high-end STM32" (NPU 없는 Cortex-M7급으로 추정) | 어떤 STM32, 어떤 모델, 지연인지 처리량인지, 같은 clock인지 |
| "nearly 300 configurable multiply-accumulate units achieving up to 600 giga operations per second (GOPS)" | 이론 peak. 예제 1로 역산하면 NPU clock ≈ 1 GHz | 정밀도(INT8 가정), sparsity 여부, clock·전압, 지속 가능한 clock |
| "800 MHz Arm Cortex-M55 core, delivering an unprecedented 3,360 CoreMark score" | CPU 성능. 3,360 ÷ 800 = 4.2 CoreMark/MHz | 이것은 CPU 숫자다. NPU 성능과 섞어 읽지 않는다 |
| "4.2 MB of RAM, the largest amount ever found on an STM32" | on-chip SRAM 총량 | NPU가 직접 쓸 수 있는 영역, retention 가능한 영역, 다른 IP(ISP·display)와 공유 여부 |
| "Two 64-bit AXI interfaces provide a high level of bandwidth" | 버스 폭만 있다 | 버스 clock → 대역폭(GB/s)을 계산할 수 없다. 외부 메모리(xSPI) 대역폭도 없다 |

ST 자료 가운데 일부(2차 보도 포함)에는 "3 TOPS/W" 같은 효율 숫자도 보이지만, 이번에 직접 확인한 보도자료 본문에는 없었다. 이런 숫자는 **datasheet의 측정 조건 표**에서 다시 찾는다(D7 5절의 다섯 조건으로 읽는다).

읽은 결과 한 줄: "0.6 TOPS급 이론 peak, 4.2 MB SRAM, 정밀도·clock·대역폭·전력 조건은 datasheet에서 확인 필요." 이 한 줄이 비교표(4절)로 들어간다.

### 3.2 Arm Ethos-U85 (IP) 와 Alif Ensemble (그 계열 IP를 쓴 칩)

**IP 제품 페이지는 칩 datasheet가 아니다.** Arm은 NPU 설계(IP)를 팔고, 칩 회사가 MAC 수·clock·SRAM을 골라 넣는다. 그래서 IP 페이지의 숫자는 "설계 공간의 범위"다.

출처: Arm Ethos-U85 제품 페이지 — [arm.com/products/silicon-ip-cpu/ethos/ethos-u85](https://www.arm.com/products/silicon-ip-cpu/ethos/ethos-u85) (확인일 2026-10-03)

| 원문 | 실제 뜻 | 빠진 것 |
|---|---|---|
| "Scalable performance – 256 GOP/s up to 4 TOPs at 1 GHz" | 128 MAC × 2 × 1 GHz = 256 GOP/s ~ 2048 MAC × 2 × 1 GHz ≈ 4.1 TOPS. 이론값 | 1 GHz는 공정·구현에 따라 다르다. MCU에 들어가면 수백 MHz가 흔하다 |
| "Scales from 128 to 2048 MACs" | 칩 회사가 고르는 구성 | 어떤 칩이 어떤 구성을 골랐는지는 그 칩 datasheet에만 있다 |
| "20% more energy efficient than Arm Ethos-U55 and Arm Ethos-U65" | 비교 배수 | 어떤 공정, 어떤 워크로드, 같은 MAC 수인지 |

출처: Alif Ensemble 제품 페이지 — [alifsemi.com/products/ensemble/](https://alifsemi.com/products/ensemble/) (확인일 2026-10-03)

| 원문 | 실제 뜻 | 빠진 것 |
|---|---|---|
| "Up to two Ethos-U55 microNPUs to 400MHz provides 250+ GOPS" | 두 NPU의 이론 peak 합. clock은 "최대 400 MHz" | 각 NPU의 MAC 구성과 실제 clock. 예제 1에서 보듯 조합이 여러 개 가능 |
| "scales up to: … 250 GigaOPS + AI Performance" | 패밀리 중 최상위 SKU 기준 | 하위 SKU(E1 등)는 NPU가 하나거나 작다 |
| "For less than 1uA a real-time clock, a 32-bit timer, an analog comparator, and GPIO wake pins are all active" | 아주 깊은 retention 모드의 전류. 무엇이 켜져 있는지 적혀 있다 — 좋은 표기다 | 온도, 전압, typ인지 max인지. 그리고 이 모드에서 NPU·대부분 SRAM은 꺼져 있다 |
| "Additionally, 4KB of SRAM are retained. For more than 1uA you can choose to retain more internal SRAM" | retention 크기에 따라 전류가 올라간다 | KB당 얼마나 올라가는지 (datasheet 표에서 확인) |

이 사례의 교훈: **"패밀리 최대값"과 "SKU 값"을 구분한다.** 제품 페이지 헤드라인은 대개 패밀리의 가장 큰 SKU 숫자다. 그리고 µA 헤드라인은 거의 항상 가장 깊은 모드다 — 그 모드에서 무엇이 꺼지는지까지 읽어야 한다(7절).

### 3.3 Ambiq Apollo510 — NPU 없는 초저전력 MCU의 "배수" 마케팅

출처: Ambiq 보도자료 — [ambiq.com/news/ambiq-apollo510-delivers-30x-power-efficiency-improvement-to-unleash-edge-ai/](https://ambiq.com/news/ambiq-apollo510-delivers-30x-power-efficiency-improvement-to-unleash-edge-ai/), 제품 페이지 — [ambiq.com/product/apollo510/](https://ambiq.com/product/apollo510/) (둘 다 확인일 2026-10-03)

| 원문 | 실제 뜻 | 빠진 것 |
|---|---|---|
| "delivers 30x better power efficiency and 10x faster performance compared to previous generations" (보도자료 요약 bullet) | 이전 세대 대비 배수 | 어느 이전 세대, 어떤 모델, 무엇을 나눈 효율인지 |
| "achieves up to 10x better latency while reducing energy consumption by around 2x, compared to … the Apollo4" (본문) | 같은 보도자료 본문에서는 에너지 개선이 "약 2배" | 요약의 "30x"와 본문의 "2x"는 서로 다른 지표·조건일 것이다. 어느 쪽이 무엇인지 문서만으로는 모른다 |
| "up to 300x more AI inference throughput per joule" (제품 페이지) | 또 다른 배수 | 비교 기준(Cortex-M4 CPU 대비?)이 같은 문장에 없다 |
| "Arm Helium technology on Apollo510 supports up to 8 MACs per cycle" | 8 MAC × 250 MHz × 2 = 4 GOPS 이론 peak (예제 1) | 정밀도(8-bit MAC 기준으로 보임), 실제 커널 효율 |
| "individually capable of peak throughputs up to 500MB/s and sustained throughput over 300MB/s" (off-chip 인터페이스) | 인터페이스 **하나당** peak와 sustained를 둘 다 적었다 — 좋은 표기다 | 어느 인터페이스·메모리 종류·clock 기준인지 |

같은 회사의 같은 칩에 대해 **30x, 10x, 2x, 300x** 네 개의 배수가 나온다. 각각 다른 지표(효율, 지연, 에너지, 처리량/J)와 다른 비교 대상을 쓴 결과일 가능성이 높다. 틀렸다는 뜻이 아니다. **배수는 분자·분모·기준선이 없으면 정보가 아니다**는 뜻이다. 엔지니어가 쓸 수 있는 숫자는 오히려 맨 아래 두 줄(8 MAC/cycle, 300 MB/s sustained)이다.

같은 제품 페이지의 비교표에는 이전 세대의 전력 효율이 "Apollo4 Blue Plus … 29 µW/MHz" 식으로 적혀 있다. µW/MHz는 7절에서 다시 다룬다 — 어느 전압 기준인지, 어떤 코드(CoreMark? while(1)?)를 어디서(MRAM? cache?) 돌렸는지가 숫자를 바꾼다.

### 3.4 Hailo-8 / Hailo-8L — 조건 없는 "typical"

출처: Hailo-8 product brief (04/23판) — [hailo.ai/wp-content/uploads/2023/12/hailo8_product_brief_3.23.pdf](https://hailo.ai/wp-content/uploads/2023/12/hailo8_product_brief_3.23.pdf), Hailo-8L M.2 product brief (08/23판) — [hailo.ai/wp-content/uploads/2023/10/Hailo-8L-M.2-ET-Product-Brief-Rev2.0.pdf](https://hailo.ai/wp-content/uploads/2023/10/Hailo-8L-M.2-ET-Product-Brief-Rev2.0.pdf) (둘 다 확인일 2026-10-03, PDF 텍스트 직접 추출)

| 원문 | 실제 뜻 | 빠진 것 |
|---|---|---|
| "Unprecedented AI compute power of 26 Tera-Operations Per Second (TOPS)" | 칩의 peak | 정밀도, sparsity, op 정의, clock — brief 본문에 없다 |
| "Best-in-class power efficiency with typical power consumption of 2.5W" | "typical"한 전력 | 어떤 모델·어떤 처리량에서의 typical인가, 칩만인가 M.2 모듈 전체인가, 온도 |
| "Does not require external memory" | DRAM 없이 on-chip 메모리로 동작 | on-chip 용량. 큰 모델(LLM)은 어떻게 되나 |
| (8L) "delivering up to 13 tera-operations per second (TOPS)" | 8의 절반 | 같은 질문들 |
| (8L) "PCIe Gen-3.0, 2-lanes" | 호스트 링크 | 입력·출력 tensor 전송 시간이 end-to-end에 들어간다 |

26 TOPS ÷ 2.5 W ≈ 10 TOPS/W라는 계산이 바로 나오지만, **두 숫자가 같은 조건에서 잰 것이라는 근거가 문서에 없다.** peak TOPS는 이론값이고 typical 전력은 어떤 워크로드의 측정값일 것이다. 둘을 나눈 값은 D7 5.2절의 "서로 다른 동작점을 곱하지 말 것"에 정확히 해당한다. Hailo는 모델별 FPS를 별도의 model zoo 문서로 공개하는데, 그런 **모델별 측정표가 1~2쪽 brief보다 훨씬 쓸모 있는 자료**다.

### 3.5 Google Coral Edge TPU — 효율을 정의로 적은 드문 예, 그리고 "model only" 벤치마크

출처: Coral Edge TPU 성능 벤치마크 문서 — [coral.ai/docs/edgetpu/benchmarks/](https://coral.ai/docs/edgetpu/benchmarks/) (확인일 2026-10-03; 페이지 리다이렉트 후 요약 도구로 읽음, 원문 재확인 권장)

| 원문 | 실제 뜻 | 빠진 것 |
|---|---|---|
| "capable of performing 4 trillion operations (tera-operations) per second (TOPS), using 0.5 watts for each TOPS (2 TOPS per watt)" | peak와 효율을 함께 적었다. Edge TPU는 int8 모델만 실행 | 2 W가 peak 부하의 칩 전력인지, 호스트·USB 포함인지 |
| 벤치마크 표는 "the time required to execute the model only" | 전처리(이미지 resize 등)를 **뺀** 시간 | end-to-end 지연은 이보다 길다 |
| MobileNet v1 (224×224): Desktop CPU 53 ms, USB Accelerator 2.4 ms | 측정값 | 호스트 CPU·USB 속도에 따라 다르다는 단서가 붙어 있다 |
| ResNet-50 V1 (299×299): USB Accelerator 49 ms | 측정값 | 왜 MobileNet보다 20배 느린가 → 6.3절에서 역산 |

Coral 문서는 이 노트에서 본 것 중 **가장 정직한 축**이다. 실측 표가 있고, 무엇을 뺐는지("model only")와 무엇에 따라 달라지는지(호스트, USB)를 적었다. 이 실측표로 "4 TOPS"가 실제로 몇 %로 쓰이는지 역산할 수 있다(예제 5).

### 3.6 NVIDIA Jetson Orin — "67 TOPS"의 해부, 그리고 합산 TOPS

출처: Jetson Orin 제품 페이지 비교표 — [nvidia.com/en-us/autonomous-machines/embedded-systems/jetson-orin/](https://www.nvidia.com/en-us/autonomous-machines/embedded-systems/jetson-orin/), Orin Nano Super Developer Kit 페이지 — [nvidia.com/en-us/autonomous-machines/embedded-systems/jetson-orin/nano-super-developer-kit/](https://www.nvidia.com/en-us/autonomous-machines/embedded-systems/jetson-orin/nano-super-developer-kit/) (둘 다 확인일 2026-10-03, HTML 표를 직접 추출)

이 페이지는 **엔진별 dense/sparse를 모두 공개**한다는 점에서 좋은 자료다. 헤드라인만 보면 오해하기 쉽고, 표를 끝까지 읽으면 정규화가 가능하다.

| 원문 (표의 칸) | 실제 뜻 | 빠진 것 |
|---|---|---|
| Overall AI Performance: "67 SPARSE INT8 TOPS" (Orin Nano 8GB) | sparse 기준 헤드라인 | 어느 전력 모드에서의 숫자인지 (최고 모드일 가능성이 높다 — 확인 필요) |
| GPU Tensor Core INT8: "67 SPARSE INT8 TOPs" / "33 DENSE INT8 TOPs" | Orin Nano는 DLA가 없어 GPU가 전부. dense는 33 | 우리 모델(INT8 dense)의 기준은 33 |
| Overall "157 SPARSE INT8TOPS" (Orin NX 16GB) | GPU 77 sparse + DLA 80 sparse의 **합** | 한 모델이 GPU와 DLA를 동시에 다 쓰지는 않는다 |
| DLA INT8: "80 SPARSE INT8 TOPs" / "40 DENSE INT8 TOPs" (NX 16GB) | DLA 2개(NVDLA v2) 합 | DLA 지원 op 범위 |
| Memory: "8GB 128-bit LPDDR5 102 GB/s" | 이론 대역폭. 128 bit × 6400 MT/s ÷ 8 ≈ 102 GB/s | CPU·GPU·DLA·ISP가 **공유**. 실효는 더 낮다 |
| Power: "7W - 15W - 25W" | 전력 **모드**(상한 설정)의 목록 | 각 모드에서의 clock과 TOPS |
| "up to a 1.7X gain in generative AI performance" (Nano Super 페이지) | 이전 소프트웨어·clock 설정 대비 배수 | 어떤 모델, 토큰/s인지 TTFT인지 |

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
<text x="142.0" y="38.0" font-size="12" text-anchor="end">Orin NX 16GB</text><rect x="150.0" y="20" width="114.0" height="26" fill="#4a7bd0" fill-opacity="0.85" stroke="currentColor" stroke-width="0.5"/><text x="207.0" y="38.0" font-size="12" text-anchor="middle">GPU 38</text>
<rect x="264.0" y="20" width="117.0" height="26" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor" stroke-width="0.5"/><text x="322.5" y="38.0" font-size="12" text-anchor="middle">+39 sparse</text><rect x="381.0" y="20" width="120.0" height="26" fill="#3f9a6b" fill-opacity="0.85" stroke="currentColor" stroke-width="0.5"/>
<text x="441.0" y="38.0" font-size="12" text-anchor="middle">DLA 40</text><rect x="501.0" y="20" width="120.0" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor" stroke-width="0.5"/><text x="561.0" y="38.0" font-size="12" text-anchor="middle">+40 sparse</text>
<text x="627.0" y="38.0" font-size="12" text-anchor="start">= 157</text><text x="142.0" y="88.0" font-size="12" text-anchor="end">Orin Nano 8GB</text><rect x="150.0" y="70" width="99.0" height="26" fill="#4a7bd0" fill-opacity="0.85" stroke="currentColor" stroke-width="0.5"/>
<text x="199.5" y="88.0" font-size="12" text-anchor="middle">GPU 33</text><rect x="249.0" y="70" width="102.0" height="26" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor" stroke-width="0.5"/><text x="300.0" y="88.0" font-size="12" text-anchor="middle">+34</text>
<text x="357.0" y="88.0" font-size="12" text-anchor="start">= 67</text><text x="142.0" y="138.0" font-size="12" text-anchor="end">Lunar Lake</text><rect x="150.0" y="120" width="15.0" height="26" fill="#888" fill-opacity="0.8" stroke="currentColor" stroke-width="0.5"/>
<rect x="165.0" y="120" width="201.0" height="26" fill="#e08a3c" fill-opacity="0.8" stroke="currentColor" stroke-width="0.5"/><text x="265.5" y="138.0" font-size="12" text-anchor="middle">GPU 67</text><rect x="366.0" y="120" width="144.0" height="26" fill="#d0564a" fill-opacity="0.8" stroke="currentColor" stroke-width="0.5"/>
<text x="438.0" y="138.0" font-size="12" text-anchor="middle">NPU 48</text><text x="516.0" y="138.0" font-size="12" text-anchor="start">= 120</text><line x1="150" y1="168" x2="660.0" y2="168" stroke="currentColor"/>
<line x1="150.0" y1="168" x2="150.0" y2="172" stroke="currentColor"/><text x="150.0" y="185.0" font-size="12" text-anchor="middle">0</text><line x1="225.0" y1="168" x2="225.0" y2="172" stroke="currentColor"/><text x="225.0" y="185.0" font-size="12" text-anchor="middle">25</text><line x1="300.0" y1="168" x2="300.0" y2="172" stroke="currentColor"/><text x="300.0" y="185.0" font-size="12" text-anchor="middle">50</text>
<line x1="375.0" y1="168" x2="375.0" y2="172" stroke="currentColor"/><text x="375.0" y="185.0" font-size="12" text-anchor="middle">75</text><line x1="450.0" y1="168" x2="450.0" y2="172" stroke="currentColor"/><text x="450.0" y="185.0" font-size="12" text-anchor="middle">100</text><line x1="525.0" y1="168" x2="525.0" y2="172" stroke="currentColor"/><text x="525.0" y="185.0" font-size="12" text-anchor="middle">125</text>
<line x1="600.0" y1="168" x2="600.0" y2="172" stroke="currentColor"/><text x="600.0" y="185.0" font-size="12" text-anchor="middle">150</text><text x="142.0" y="138.0" font-size="12" text-anchor="start"></text><text x="400.0" y="210.0" font-size="13" text-anchor="middle">헤드라인 TOPS의 구성 (진한 색: 실제로 한 엔진이 dense로 내는 몫)</text>
<text x="157.5" y="114.0" font-size="12" text-anchor="start">CPU 5</text>
</svg>
```

그림 3 — 헤드라인 TOPS의 구성. 진한 칸이 한 엔진이 dense로 내는 몫이고, 옅은 칸은 2:4 sparsity 크레딧이다. Orin NX 16GB의 157은 GPU와 DLA의 sparse 합이고, 모델 하나가 GPU에서 dense INT8로 쓸 수 있는 몫은 38이다. Lunar Lake의 120은 CPU·GPU·NPU의 합이다(2차 보도 기준).

PC 쪽의 예: Intel은 Lunar Lake(Core Ultra 200V)에 대해 "120 platform TOPS"를 발표했고, 보도에 따르면 NPU 48 + GPU 67 + CPU 5 TOPS(int8)의 합이다. 출처: [theregister.com/2024/06/04/intel_details_lunar_lake_tops/](https://www.theregister.com/2024/06/04/intel_details_lunar_lake_tops/) (2차 보도, 검색 결과 요약 기준, 확인일 2026-10-03). Microsoft Copilot+ PC 요건이 **NPU** TOPS 기준이라 48이 따로 강조된다 — 합산 숫자와 엔진 숫자가 다른 목적에 쓰인다는 좋은 예다.

### 3.7 Qualcomm Snapdragon AR1 Gen 1 — TOPS가 아예 없는 product brief

출처: Snapdragon AR1 Gen 1 Platform product brief (87-86507-1 Rev B) — [docs.qualcomm.com/doc/87-86507-1/87-86507-1_REV_B_Snapdragon_AR1_Gen_1_Platform_Product_Brief.pdf](https://docs.qualcomm.com/doc/87-86507-1/87-86507-1_REV_B_Snapdragon_AR1_Gen_1_Platform_Product_Brief.pdf) (확인일 2026-10-03, PDF 텍스트 직접 추출)

스마트 글라스용 플랫폼이다. 웨어러블 AI 기기를 만드는 회사라면 가장 먼저 볼 후보군 중 하나다(M3).

| 원문 | 실제 뜻 | 빠진 것 |
|---|---|---|
| "Hexagon NPU … Hexagon scalar, vector, and tensor accelerators" | NPU 구성 요소의 이름 | **TOPS 숫자 자체가 brief에 없다** |
| "Support for precisions (INT4, INT8)" | 지원 정밀도 | INT4와 INT8 각각의 처리량 |
| "Micro NPU for audio and sensors" (Sensing Hub) | always-on 저전력 NPU가 따로 있다 | 그 micro NPU의 성능·전력·메모리 |
| "Support for LPDDR4x memory, up to 2100 MHz" | 메모리 종류와 속도 | **버스 폭**이 없다. 그리고 2100 MHz가 clock인지(→ 4200 MT/s) 데이터 rate인지 문장만으로는 확정할 수 없다 |
| "4x cores 1.9 GHz" (Kryo CPU) | CPU clock | 코어 종류(big/little) |
| "optimized for lightweight, battery-efficient smart glasses" | 정성 문장 | 전력 숫자 없음 |

후속 칩 AR1+ Gen 1에 대해서는 2025년 6월 AWE 발표 보도에서 "26% smaller"와 "uses 7% less power to enable longer battery life", 그리고 Llama 3.2 1B를 글라스에서 직접 돌린 데모가 소개됐다. 출처: [uploadvr.com/qualcomm-snapdragon-ar1-plus-smart-glasses-chip/](https://www.uploadvr.com/qualcomm-snapdragon-ar1-plus-smart-glasses-chip/) (2차 보도, 확인일 2026-10-03). 다른 보도는 TTFT 약 1.2초를 전한다(검색 결과 요약 기준, 원문 미확인). 이 "7%"와 "1.2초"도 조건이 없다 — 어떤 워크로드의 전력인지, 프롬프트가 몇 토큰인지, 정밀도가 무엇인지.

이 사례의 교훈: **스마트폰·XR SoC 벤더는 NPU TOPS를 공개하지 않거나 플랫폼 단위로만 공개하는 경우가 많다.** 이때 공개 자료로 계산할 수 있는 것은 메모리 쪽뿐이다. LLM decode는 memory-bound(D5)라서, 사실 TOPS보다 버스 폭이 더 중요한 질문이다(5절, 예제 3).

### 3.8 한눈에 — 사례별 "숫자의 종류"

| 자료 | 이론 peak | 측정값 | 배수 | 조건 표기 수준 |
|---|---|---|---|---|
| STM32N6 보도자료 | 600 GOPS | 없음 | 600x | 낮음 (clock·정밀도 없음) |
| Ethos-U85 IP 페이지 | 256 GOP/s ~ 4 TOPs @1 GHz | 없음 | 20% | 중간 (clock은 있음, 구현 의존) |
| Alif 제품 페이지 | 250+ GOPS | sleep <1 µA (조건 일부) | 없음 | 중간 |
| Ambiq 보도자료·페이지 | 8 MAC/cycle | 300 MB/s sustained | 30x, 10x, 2x, 300x | 낮음 (배수) / 좋음 (peak·sustained 병기) |
| Hailo-8 brief | 26 TOPS | typical 2.5 W | 없음 | 낮음 |
| Coral 문서 | 4 TOPS, 2 TOPS/W | 모델별 ms | 없음 | 높음 (포함 범위 명시) |
| Jetson Orin 표 | 엔진별 sparse·dense | 없음 | 1.7X | 높음 (엔진·sparse 분해) |
| AR1 Gen 1 brief | 없음 | 없음 | 없음 | 낮음 (TOPS 미공개) |

---

## 4. 정규화 — 같은 자 위에 올리기

### 4.1 기준 정하기

비교하려면 하나의 기준이 필요하다. 이 노트의 기준은 다음과 같다.

```
정규화 단위 = dense INT8 ops/s, 한 엔진(NPU급 가속기 하나), 1 MAC = 2 op, 이론 peak
           → 그 다음 단계(6절)에서 활용률·대역폭 효율을 곱해 '우리 모델' 값으로 내린다
```

말로 하면: 먼저 "광고 방식의 차이"만 걷어 내고(정밀도, sparsity, 합산), 그 다음에 "실제로 얼마나 쓰이나"(활용률)를 따로 곱한다. 두 단계를 섞으면 어느 가정이 결과를 움직였는지 추적할 수 없다. D7·I5에서 계수를 한 줄씩 곱해 나간 것과 같은 원리인데, 여기서는 **실제 칩들에 같은 규칙을 일괄 적용**하고 **불확실성을 flag로 남기는** 것이 목적이다.

정규화 규칙:

| 조건 | 처리 | flag |
|---|---|---|
| sparse로 표기 | ÷ 2 (벤더가 dense 값을 공개했으면 그 값을 쓴다) | "sparse /2" |
| sparse 여부 미표기 | 그대로 두고 flag | "sparse? 미표기" |
| INT4로만 표기 | ÷ 2 (INT4가 INT8의 2배 rate인 구조 가정) | "INT4 /2" |
| 정밀도 미표기 | INT8로 가정 | "정밀도 미표기" |
| 여러 엔진 합 | 주 엔진(모델이 실제로 도는 엔진)의 값만 | "합산 → 주 엔진만" |
| TOPS 미공개 | 계산하지 않음 | "TOPS 비공개" |

### 4.2 코드 — spec normalizer

무엇을 확인하나: 3절에서 읽은 숫자를 위 규칙으로 일괄 정규화하고, 칩별로 남은 불확실성을 flag로 출력한다.

```python
# 광고 TOPS를 "dense INT8, 한 엔진(NPU급), 1 MAC = 2 op" 기준으로 정규화하고 불확실성 flag를 붙인다
SPEC = [  # name, claim TOPS, precision, sparse?, scope, published dense-INT8 of main engine (or None)
    ("Jetson Orin Nano 8GB",  67,   "INT8", True,  "GPU",         33),   # DLA 없음
    ("Jetson Orin NX 16GB",   157,  "INT8", True,  "GPU+DLA",     38),   # GPU 77s|38d + DLA 80s|40d
    ("Intel Lunar Lake",      120,  "INT8", False, "CPU+GPU+NPU", 48),   # 5 + 67 + 48
    ("Hailo-8",               26,   None,   None,  "chip",        None),
    ("Hailo-8L",              13,   None,   None,  "chip",        None),
    ("Coral Edge TPU",        4,    "INT8", None,  "chip",        None),
    ("STM32N6 Neural-ART",    0.6,  None,   None,  "NPU",         None),
    ("Alif Ensemble (2xU55)", 0.25, None,   None,  "2 NPUs",      None),
    ("Apollo510 (Helium)",    0.004,"INT8", False, "CPU SIMD",    None), # 8 MAC x 2 x 250 MHz 역산
    ("Snapdragon AR1 Gen 1",  None, "INT4/INT8", None, "NPU",     None),
]
def normalize(name, tops, prec, sparse, scope, dense_pub):
    flags = []
    if tops is None:
        return None, ["TOPS 비공개"]
    if dense_pub is not None:
        x = dense_pub; flags.append("vendor 엔진별 값 사용")
    else:
        x = tops
        if sparse:            x /= 2;  flags.append("sparse /2")
        if sparse is None:    flags.append("sparse? 미표기")
        if prec is None:      flags.append("정밀도 미표기(INT8 가정)")
        elif "INT4" in prec and "INT8" not in prec: x /= 2; flags.append("INT4 /2")
    if "+" in scope:          flags.append(f"합산({scope}) -> 주 엔진만")
    flags.append("1 MAC=2 op 가정")
    return x, flags
print(f"{'chip':22s} {'claim':>7s} {'dense INT8':>10s} {'ratio':>6s}  flags")
for row in SPEC:
    v, fl = normalize(*row)
    c = row[1]
    cs = f"{c:7.3g}" if c else "    n/a"
    vs = f"{v:10.3g}" if v else "       n/a"
    rs = f"{v/c:6.2f}" if v and c else "   n/a"
    print(f"{row[0]:22s} {cs} {vs} {rs}  {'; '.join(fl)}")
```

```text
chip                     claim dense INT8  ratio  flags
Jetson Orin Nano 8GB        67         33   0.49  vendor 엔진별 값 사용; 1 MAC=2 op 가정
Jetson Orin NX 16GB        157         38   0.24  vendor 엔진별 값 사용; 합산(GPU+DLA) -> 주 엔진만; 1 MAC=2 op 가정
Intel Lunar Lake           120         48   0.40  vendor 엔진별 값 사용; 합산(CPU+GPU+NPU) -> 주 엔진만; 1 MAC=2 op 가정
Hailo-8                     26         26   1.00  sparse? 미표기; 정밀도 미표기(INT8 가정); 1 MAC=2 op 가정
Hailo-8L                    13         13   1.00  sparse? 미표기; 정밀도 미표기(INT8 가정); 1 MAC=2 op 가정
Coral Edge TPU               4          4   1.00  sparse? 미표기; 1 MAC=2 op 가정
STM32N6 Neural-ART         0.6        0.6   1.00  sparse? 미표기; 정밀도 미표기(INT8 가정); 1 MAC=2 op 가정
Alif Ensemble (2xU55)     0.25       0.25   1.00  sparse? 미표기; 정밀도 미표기(INT8 가정); 1 MAC=2 op 가정
Apollo510 (Helium)       0.004      0.004   1.00  1 MAC=2 op 가정
Snapdragon AR1 Gen 1       n/a        n/a    n/a  TOPS 비공개
```

출력에서 볼 것:

- Jetson 계열과 Lunar Lake는 정규화 후 **0.24~0.49배**로 줄어든다. 하지만 이 세 줄은 flag가 "vendor 엔진별 값 사용"뿐이라 **가장 확실한** 줄이다. 벤더가 분해표를 공개했기 때문이다.
- Hailo·Coral·STM32N6·Alif는 ratio가 1.00으로 남았다. "줄일 근거가 없었다"는 뜻이지 "광고가 정확하다"는 뜻이 아니다. flag가 그 차이를 기록한다. **ratio 1.00 + flag 다수 = 가장 불확실한 줄**이다.
- AR1 Gen 1은 계산할 수 없다. 이것도 결과다 — 비교표에 "n/a (질문 필요)"로 들어간다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<text x="192.0" y="44.0" font-size="12" text-anchor="end">Jetson Orin Nano 8GB</text><rect x="200" y="30" width="362.0" height="11" fill="#4a7bd0" fill-opacity="0.75"/><rect x="200" y="42" width="338.9" height="11" fill="#e08a3c" fill-opacity="0.85"/>
<text x="566.0" y="40.0" font-size="12" text-anchor="start">67</text><text x="542.9" y="52.0" font-size="12" text-anchor="start">33</text><text x="192.0" y="84.0" font-size="12" text-anchor="end">Jetson Orin NX 16GB</text>
<rect x="200" y="70" width="389.7" height="11" fill="#4a7bd0" fill-opacity="0.75"/><rect x="200" y="82" width="343.5" height="11" fill="#e08a3c" fill-opacity="0.85"/><text x="593.7" y="80.0" font-size="12" text-anchor="start">157</text>
<text x="547.5" y="92.0" font-size="12" text-anchor="start">38</text><text x="192.0" y="124.0" font-size="12" text-anchor="end">Intel Lunar Lake</text><rect x="200" y="110" width="380.9" height="11" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="200" y="122" width="351.1" height="11" fill="#e08a3c" fill-opacity="0.85"/><text x="584.9" y="120.0" font-size="12" text-anchor="start">120</text><text x="555.1" y="132.0" font-size="12" text-anchor="start">48</text>
<text x="192.0" y="164.0" font-size="12" text-anchor="end">Hailo-8</text><rect x="200" y="150" width="331.1" height="11" fill="#4a7bd0" fill-opacity="0.75"/><rect x="200" y="162" width="331.1" height="11" fill="#e08a3c" fill-opacity="0.85"/>
<text x="535.1" y="160.0" font-size="12" text-anchor="start">26</text><text x="535.1" y="172.0" font-size="12" text-anchor="start">26 (?)</text><text x="192.0" y="204.0" font-size="12" text-anchor="end">Coral Edge TPU</text>
<rect x="200" y="190" width="270.2" height="11" fill="#4a7bd0" fill-opacity="0.75"/><rect x="200" y="202" width="270.2" height="11" fill="#e08a3c" fill-opacity="0.85"/><text x="474.2" y="200.0" font-size="12" text-anchor="start">4</text>
<text x="474.2" y="212.0" font-size="12" text-anchor="start">4 (?)</text><text x="192.0" y="244.0" font-size="12" text-anchor="end">STM32N6 NPU</text><rect x="200" y="230" width="208.4" height="11" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="200" y="242" width="208.4" height="11" fill="#e08a3c" fill-opacity="0.85"/><text x="412.4" y="240.0" font-size="12" text-anchor="start">0.6</text><text x="412.4" y="252.0" font-size="12" text-anchor="start">0.6 (?)</text>
<text x="192.0" y="284.0" font-size="12" text-anchor="end">Alif Ensemble 2xU55</text><rect x="200" y="270" width="179.8" height="11" fill="#4a7bd0" fill-opacity="0.75"/><rect x="200" y="282" width="179.8" height="11" fill="#e08a3c" fill-opacity="0.85"/>
<text x="383.8" y="280.0" font-size="12" text-anchor="start">0.25</text><text x="383.8" y="292.0" font-size="12" text-anchor="start">0.25 (?)</text><text x="192.0" y="324.0" font-size="12" text-anchor="end">Apollo510 Helium</text>
<rect x="200" y="310" width="45.2" height="11" fill="#4a7bd0" fill-opacity="0.75"/><rect x="200" y="322" width="45.2" height="11" fill="#e08a3c" fill-opacity="0.85"/><text x="249.2" y="320.0" font-size="12" text-anchor="start">0.004</text>
<text x="249.2" y="332.0" font-size="12" text-anchor="start">0.004 (?)</text><line x1="200" y1="354" x2="650" y2="354" stroke="currentColor"/><line x1="200.0" y1="354" x2="200.0" y2="358" stroke="currentColor"/>
<text x="200.0" y="371.0" font-size="12" text-anchor="middle">0.001</text><line x1="275.0" y1="354" x2="275.0" y2="358" stroke="currentColor"/><text x="275.0" y="371.0" font-size="12" text-anchor="middle">0.01</text>
<line x1="350.0" y1="354" x2="350.0" y2="358" stroke="currentColor"/><text x="350.0" y="371.0" font-size="12" text-anchor="middle">0.1</text><line x1="425.0" y1="354" x2="425.0" y2="358" stroke="currentColor"/>
<text x="425.0" y="371.0" font-size="12" text-anchor="middle">1</text><line x1="500.0" y1="354" x2="500.0" y2="358" stroke="currentColor"/><text x="500.0" y="371.0" font-size="12" text-anchor="middle">10</text>
<line x1="575.0" y1="354" x2="575.0" y2="358" stroke="currentColor"/><text x="575.0" y="371.0" font-size="12" text-anchor="middle">100</text><line x1="650.0" y1="354" x2="650.0" y2="358" stroke="currentColor"/>
<text x="650.0" y="371.0" font-size="12" text-anchor="middle">1000</text><text x="425.0" y="388.0" font-size="13" text-anchor="middle">TOPS (log 눈금) — 파랑: 광고 숫자, 주황: dense INT8 한 엔진 기준</text>
</svg>
```

그림 4 — 광고 숫자(파랑)와 dense INT8 한 엔진 기준(주황), log 눈금. "(?)"가 붙은 줄은 정규화 근거가 없어 값이 그대로인 칩이다. 5자릿수(0.004 ~ 157)에 걸쳐 있다는 것 자체가 중요하다: "edge AI 칩"이라는 한 단어 안에 서로 다른 세계가 섞여 있다.

### 4.3 정규화 결과표

| 칩 | 광고 | dense INT8 한 엔진 | 확신도 | 남은 질문 |
|---|---|---|---|---|
| Jetson Orin Nano 8GB | 67 sparse INT8 TOPS | 33 TOPS | 높음 | 어느 전력 모드의 숫자인가 |
| Jetson Orin NX 16GB | 157 (GPU+DLA, sparse) | 38 TOPS (GPU) | 높음 | DLA에 올릴 수 있는 op 비율 |
| Intel Lunar Lake | 120 platform TOPS | 48 TOPS (NPU) | 중간 (2차 보도) | NPU 숫자의 sparse 여부 |
| Hailo-8 | 26 TOPS | 26 TOPS (가정) | 낮음 | 정밀도·sparse·on-chip 용량 |
| Coral Edge TPU | 4 TOPS | 4 TOPS | 중간 (int8 전용) | — 실측표로 검산 가능(6.3절) |
| STM32N6 | 600 GOPS | 0.6 TOPS (가정) | 낮음 | 정밀도·clock·대역폭 |
| Alif Ensemble (최상위) | 250+ GOPS | 0.25 TOPS (두 NPU 합) | 낮음 | 각 NPU 구성·clock |
| Apollo510 | (TOPS 미표기) | 0.004 TOPS (CPU SIMD) | 중간 (역산) | 실제 커널 효율 |
| Snapdragon AR1 Gen 1 | 미공개 | n/a | — | NPU TOPS (INT8/INT4), micro NPU 사양 |

---

## 5. 메모리 스펙 읽기와 ridge point

### 5.1 대역폭 숫자의 세 층

메모리 대역폭도 TOPS처럼 층이 있다(E7 3절).

```
이론 대역폭  = 버스 폭(bit) × 전송률(MT/s) ÷ 8        ← datasheet가 주는 값 (주면 다행)
실효 대역폭  = 이론 × 효율(refresh, bank 충돌, read/write 전환, 접근 패턴)  ← 보통 60~80 %
NPU 몫      = 실효 × (CPU·GPU·ISP·display와 나눈 뒤 남는 비율)          ← 문서에 거의 없다
```

말로 하면: datasheet의 "102 GB/s"는 128 bit 버스가 쉬지 않고 데이터만 실어 나를 때의 값이다. 실제로는 refresh와 명령 오버헤드로 깎이고, 같은 DRAM을 쓰는 다른 마스터와 나눠 쓴다. 웨어러블 SoC에서는 카메라 ISP·display가 실시간 우선순위를 가지므로 NPU 몫이 더 줄어든다.

datasheet에서 읽어야 할 것:

| 항목 | 왜 | 사례 |
|---|---|---|
| 메모리 종류와 속도 | 전송률 | AR1: "LPDDR4x, up to 2100 MHz" |
| 버스 폭 | 이론 대역폭 계산 | Orin Nano: "128-bit" 명시 / AR1: 없음 |
| MHz인지 MT/s인지 | DDR은 clock의 2배 전송 | AR1의 "2100 MHz"는 둘 다로 읽힌다 |
| peak와 sustained | 실효 | Apollo510: "peak … 500MB/s and sustained … over 300MB/s" |
| on-chip SRAM 크기와 NPU 전용 여부 | weight·activation이 들어가면 DRAM이 병목에서 빠진다 | STM32N6: 4.2 MB (공유 여부 미표기) |
| 공유 여부 | NPU 몫 | Jetson: CPU·GPU·DLA 공유 (unified memory) |

### 5.2 ridge point — 정규화 숫자로 계산하기

D3에서 배운 ridge point = peak ops/s ÷ 대역폭(B/s)이다. 모델의 arithmetic intensity(AI, op/byte)가 ridge보다 낮으면 memory-bound, 높으면 compute-bound다. 여기서는 공식을 다시 유도하지 않고, **정규화된 실제 칩 숫자**로 계산만 한다. 핵심은 "weight를 **어느 메모리에서** 읽느냐"에 따라 같은 칩도 ridge가 수십 배 달라진다는 점이다.

무엇을 확인하나: 칩마다 weight를 읽는 경로의 대역폭으로 ridge를 내고, 우리 모델 세 개의 AI와 비교한다. 버스 폭이 공개되지 않은 AR1은 가능한 해석을 모두 펼친다.

```python
# 정규화한 dense INT8 ops/s와 "어느 메모리에서 weight를 읽나"의 대역폭으로 ridge point(op/byte)를 낸다
def lpddr_gbs(mts, bits): return mts * 1e6 * bits / 8 / 1e9      # 이론 대역폭 (E7)
rows = [  # chip, dense INT8 TOPS, memory, theoretical GB/s, 출처 메모
    ("Jetson Orin Nano 8GB", 33,    "LPDDR5 128-bit", 102.0, "spec table"),
    ("Apollo510 (Helium)",   0.004, "off-chip sustained", 0.3, "'sustained over 300MB/s'"),
    ("STM32N6 on-chip AXI",  0.6,   "2x64-bit AXI @1GHz?", 16.0, "clock 가정"),
    ("STM32N6 ext. xSPI",    0.6,   "xSPI x8 DDR 200MHz?", 0.4, "가정"),
    ("Coral USB (weights>cache)", 4, "USB 3 5Gb/s 8b10b", 0.5, "이론 상한"),
]
print(f"{'case':28s} {'TOPS':>6s} {'GB/s':>6s} {'ridge':>8s} {'ridge@70%BW':>11s}")
for chip, tops, mem, bw, note in rows:
    r = tops * 1e12 / (bw * 1e9)
    print(f"{chip:28s} {tops:6.3g} {bw:6.3g} {r:8.1f} {r/0.7:11.1f}   [{mem}; {note}]")
print("AR1 Gen 1: LPDDR4x 'up to 2100 MHz' — clock인지 MT/s인지, 버스 폭 모두 미공개")
for mts in (4200, 2100):
    for bits in (16, 32):
        bw = lpddr_gbs(mts, bits)
        print(f"  {mts} MT/s x{bits} -> {bw:5.1f} GB/s, 0.5B int4 decode 상한 {bw/0.28:5.1f} tok/s")
# 우리 모델의 arithmetic intensity (weight만 센 상한 근사 — activation traffic 무시, 1 MAC = 2 op)
for m, macs, wbytes in [("DS-CNN KWS", 2.66e6, 22e3), ("MobileNetV2", 300.8e6, 3.5e6),
                        ("0.5B LLM decode int4", 0.49e9, 0.28e9)]:
    print(f"AI({m:20s}) = {2*macs/wbytes:8.1f} op/B (weight를 한 번씩만 읽을 때)")
```

```text
case                           TOPS   GB/s    ridge ridge@70%BW
Jetson Orin Nano 8GB             33    102    323.5       462.2   [LPDDR5 128-bit; spec table]
Apollo510 (Helium)            0.004    0.3     13.3        19.0   [off-chip sustained; 'sustained over 300MB/s']
STM32N6 on-chip AXI             0.6     16     37.5        53.6   [2x64-bit AXI @1GHz?; clock 가정]
STM32N6 ext. xSPI               0.6    0.4   1500.0      2142.9   [xSPI x8 DDR 200MHz?; 가정]
Coral USB (weights>cache)         4    0.5   8000.0     11428.6   [USB 3 5Gb/s 8b10b; 이론 상한]
AR1 Gen 1: LPDDR4x 'up to 2100 MHz' — clock인지 MT/s인지, 버스 폭 모두 미공개
  4200 MT/s x16 ->   8.4 GB/s, 0.5B int4 decode 상한  30.0 tok/s
  4200 MT/s x32 ->  16.8 GB/s, 0.5B int4 decode 상한  60.0 tok/s
  2100 MT/s x16 ->   4.2 GB/s, 0.5B int4 decode 상한  15.0 tok/s
  2100 MT/s x32 ->   8.4 GB/s, 0.5B int4 decode 상한  30.0 tok/s
AI(DS-CNN KWS          ) =    241.8 op/B (weight를 한 번씩만 읽을 때)
AI(MobileNetV2         ) =    171.9 op/B (weight를 한 번씩만 읽을 때)
AI(0.5B LLM decode int4) =      3.5 op/B (weight를 한 번씩만 읽을 때)
```

출력에서 볼 것:

- 같은 STM32N6도 weight가 on-chip AXI에 있으면 ridge 37.5, 외부 xSPI flash에서 읽으면 1500이다(둘 다 가정 clock 기준). MobileNetV2(AI ≈ 172, weight만 센 상한)는 on-chip이면 compute-bound, 외부면 **memory-bound**다. 그래서 "SRAM이 몇 MB인가"가 "TOPS가 얼마인가"보다 먼저 나오는 질문이다.
- Coral USB는 weight가 8 MB 캐시를 넘으면 ridge가 8000이다. 사실상 모든 모델이 memory-bound가 된다 — 6.3절의 ResNet-50 실측이 이것을 보여 준다.
- 0.5B LLM decode의 AI는 3.5 op/B다. 어떤 칩의 ridge보다도 낮다. **decode 속도는 TOPS가 아니라 대역폭이 정한다**(D5). AR1의 버스 폭 해석에 따라 상한이 15~60 tok/s로 4배 벌어진다. 버스 폭 한 줄이 빠진 것이 이만큼의 불확실성이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<line x1="40" y1="220" x2="650" y2="220" stroke="currentColor"/><line x1="40.0" y1="220" x2="40.0" y2="224" stroke="currentColor"/><text x="40.0" y="237.0" font-size="12" text-anchor="middle">1</text><line x1="192.5" y1="220" x2="192.5" y2="224" stroke="currentColor"/><text x="192.5" y="237.0" font-size="12" text-anchor="middle">10</text>
<line x1="345.0" y1="220" x2="345.0" y2="224" stroke="currentColor"/><text x="345.0" y="237.0" font-size="12" text-anchor="middle">100</text><line x1="497.5" y1="220" x2="497.5" y2="224" stroke="currentColor"/><text x="497.5" y="237.0" font-size="12" text-anchor="middle">1000</text><line x1="650.0" y1="220" x2="650.0" y2="224" stroke="currentColor"/><text x="650.0" y="237.0" font-size="12" text-anchor="middle">10000</text>
<text x="345.0" y="256.0" font-size="13" text-anchor="middle">op/byte (log) — 위: 칩의 ridge point, 아래: 모델의 arithmetic intensity</text><line x1="211.4" y1="44" x2="211.4" y2="220" stroke="#4a7bd0" stroke-dasharray="3 3"/><circle cx="211.4" cy="40" r="5" fill="#4a7bd0"/>
<text x="219.4" y="44.0" font-size="12" text-anchor="start">Apollo510 off-chip 13.3</text><line x1="280.0" y1="74" x2="280.0" y2="220" stroke="#4a7bd0" stroke-dasharray="3 3"/><circle cx="280.0" cy="70" r="5" fill="#4a7bd0"/>
<text x="288.0" y="74.0" font-size="12" text-anchor="start">STM32N6 AXI(가정) 37.5</text><line x1="422.8" y1="104" x2="422.8" y2="220" stroke="#4a7bd0" stroke-dasharray="3 3"/><circle cx="422.8" cy="100" r="5" fill="#4a7bd0"/>
<text x="430.8" y="104.0" font-size="12" text-anchor="start">Orin Nano 323.5</text><line x1="524.4" y1="134" x2="524.4" y2="220" stroke="#4a7bd0" stroke-dasharray="3 3"/><circle cx="524.4" cy="130" r="5" fill="#4a7bd0"/>
<text x="532.4" y="134.0" font-size="12" text-anchor="start">STM32N6 xSPI(가정) 1500</text><line x1="635.2" y1="164" x2="635.2" y2="220" stroke="#4a7bd0" stroke-dasharray="3 3"/><circle cx="635.2" cy="160" r="5" fill="#4a7bd0"/>
<text x="627.2" y="164.0" font-size="12" text-anchor="end">Coral USB 8000</text><rect x="118.0" y="200" width="10" height="10" fill="#e08a3c"/><text x="131.0" y="209.0" font-size="12" text-anchor="start">LLM decode 3.5</text>
<rect x="375.9" y="200" width="10" height="10" fill="#e08a3c"/><text x="388.9" y="209.0" font-size="12" text-anchor="start">MobileNetV2 172</text><rect x="398.5" y="185" width="10" height="10" fill="#e08a3c"/>
<text x="411.5" y="194.0" font-size="12" text-anchor="start">DS-CNN 242</text>
</svg>
```

그림 5 — 칩의 ridge point(위, 파랑 점)와 모델의 arithmetic intensity(아래, 주황 사각형), log 눈금. 모델 점이 칩 ridge보다 왼쪽이면 그 칩에서 memory-bound다. "(가정)"이 붙은 두 점은 공개 자료에 clock이 없어 가정한 값이다.

### 5.3 함정 — "unified memory"와 "no external memory"

- **unified memory**(Jetson, 스마트폰 SoC): 모델 weight·KV cache·카메라 프레임·OS가 한 DRAM에 있다. 용량(8 GB)은 크지만 대역폭은 모두가 나눈다. "8 GB면 3B 모델이 들어간다"는 계산 옆에 반드시 "decode 동안 다른 마스터가 몇 GB/s를 쓰나"를 적는다.
- **no external memory**(Hailo-8): DRAM이 없어 전력·BOM이 작고 weight 읽기가 빠르다. 대신 모델 크기가 on-chip 용량에 묶인다. LLM처럼 큰 모델은 애초에 대상이 아니다.
- **MCU의 "x MB RAM"**: 여러 SRAM 뱅크의 합인 경우가 많다. NPU가 접근 가능한 뱅크, retention 가능한 뱅크, ISP·display가 쓰는 뱅크를 RM의 memory map에서 확인한다(K1).

---

## 6. 우리 모델로 환산 — 지연 범위 추정과 실측 검산

### 6.1 왜 "범위"인가

I5 3.2절은 가상의 칩 하나로 TOPS → ms 손계산을 했다. 여기서는 정규화된 **실제 후보 4개**에 우리 모델 3개를 올린다. 다만 점 추정 하나를 내지 않고 **최선~최악 범위**를 낸다. 활용률(E5)과 대역폭 효율(E7)을 모르는 상태에서 숫자 하나를 내면 그 숫자가 가정을 숨기기 때문이다.

```
지연 ≈ max( 2·MAC ÷ (peak × u),  weight bytes ÷ (BW × e) ) + 고정 오버헤드
       u ∈ [u_lo, u_hi]  활용률 (E5: 모양·batch·op 지원에 따라)
       e ∈ [e_lo, e_hi]  대역폭 효율 (E7)
       오버헤드: NPU job 시작, 드라이버, PCIe/USB 전송
```

말로 하면: compute 시간과 memory 시간 중 긴 쪽이 지배하고(roofline), 거기에 매번 내는 고정 비용을 더한다. 범위의 양 끝은 각 가정의 양 끝을 모아 계산한다.

가정(전부 추정이며, 실측으로 바꿔야 할 값):

| 칩 | 활용률 범위 | weight 경로 | 오버헤드 |
|---|---|---|---|
| Apollo510 (Helium) | 30~70 % (CPU SIMD 커널, E2) | on-chip | 0 |
| STM32N6 NPU | 10~50 % | 1 MB 넘는 weight는 xSPI 0.4 GB/s (가정) | 0.02~0.1 ms |
| Hailo-8 | 5~60 % (작은 모델은 큰 칩을 못 채운다) | on-chip | 0.2~1 ms (PCIe·호스트 드라이버) |
| Orin Nano 8GB | 2~30 % (batch 1, 작은 모델에 GPU는 과하다) | LPDDR5 102 GB/s × 50~80 % | 0.1~1 ms (커널 launch·동기화) |

### 6.2 코드 — 지연 범위

무엇을 확인하나: 위 가정으로 칩 4개 × 모델 3개의 최선·최악 지연과 병목 종류를 계산한다.

```python
# 정규화된 숫자 + 활용률 범위(E5) + 대역폭 효율(E7)로 우리 모델 3개의 지연 "범위"를 낸다 (모두 가정 명시)
MODELS = {"DS-CNN KWS": (2.66e6, 22e3), "MobileNetV2": (300.8e6, 3.5e6), "0.5B LLM decode": (0.49e9, 0.28e9)}
CHIPS = {  # dense INT8 TOPS, util(lo,hi), 큰 weight 경로 GB/s(None=on-chip), BW eff(lo,hi), overhead ms(lo,hi), weight 한도 B
    "Apollo510 Helium": (0.004, (0.3, 0.7),  None, (1, 1),     (0.0, 0.0),  4e6),
    "STM32N6 NPU":      (0.6,   (0.1, 0.5),  0.4,  (0.7, 0.9), (0.02, 0.1), 64e6),  # 1 MB 넘는 weight는 xSPI 가정
    "Hailo-8":          (26,    (0.05, 0.6), None, (1, 1),     (0.2, 1.0),  0),     # DRAM 없음 -> LLM 제외
    "Orin Nano 8GB":    (33,    (0.02, 0.3), 102,  (0.5, 0.8), (0.1, 1.0),  7e9),
}
print(f"{'chip':17s} {'model':16s} {'best ms':>9s} {'worst ms':>9s}  bound")
for chip, (tops, (ulo, uhi), bw, (elo, ehi), (olo, ohi), cap) in CHIPS.items():
    for m, (macs, wb) in MODELS.items():
        if m.startswith("0.5B") and wb > cap:
            print(f"{chip:17s} {m:16s} {'-':>9s} {'-':>9s}  weight 0.28 GB가 안 들어감"); continue
        comp = [2 * macs / (tops * 1e12 * u) * 1e3 for u in (uhi, ulo)]                 # ms
        on_chip = bw is None or wb <= 1e6
        mem = [0.0, 0.0] if on_chip else [wb / (bw * 1e9 * e) * 1e3 for e in (ehi, elo)]
        best, worst = max(comp[0], mem[0]) + olo, max(comp[1], mem[1]) + ohi
        bound = "memory" if mem[1] > comp[1] else "compute"
        print(f"{chip:17s} {m:16s} {best:9.3f} {worst:9.3f}  {bound}")
print(f"naive (claim 그대로, util 100 %) Orin 67 TOPS MobileNetV2 = {2*300.8e6/67e12*1e3:.4f} ms")
```

```text
chip              model              best ms  worst ms  bound
Apollo510 Helium  DS-CNN KWS           1.900     4.433  compute
Apollo510 Helium  MobileNetV2        214.857   501.333  compute
Apollo510 Helium  0.5B LLM decode          -         -  weight 0.28 GB가 안 들어감
STM32N6 NPU       DS-CNN KWS           0.038     0.189  compute
STM32N6 NPU       MobileNetV2          9.742    12.600  memory
STM32N6 NPU       0.5B LLM decode          -         -  weight 0.28 GB가 안 들어감
Hailo-8           DS-CNN KWS           0.200     1.004  compute
Hailo-8           MobileNetV2          0.239     1.463  compute
Hailo-8           0.5B LLM decode          -         -  weight 0.28 GB가 안 들어감
Orin Nano 8GB     DS-CNN KWS           0.101     1.008  compute
Orin Nano 8GB     MobileNetV2          0.161     1.912  compute
Orin Nano 8GB     0.5B LLM decode      3.531     6.490  memory
naive (claim 그대로, util 100 %) Orin 67 TOPS MobileNetV2 = 0.0090 ms
```

출력에서 볼 것:

- **KWS(2.66 M MAC)** 는 어느 칩이든 1 ms 안팎이다. Hailo·Orin 같은 큰 가속기에서도 0.1~1 ms인데, 그 대부분이 **오버헤드**다. 작은 모델에서는 TOPS가 1000배 커도 지연이 별로 줄지 않는다. 웨어러블 always-on KWS 칩을 고를 때 TOPS는 거의 의미가 없고, 7절의 전력 숫자가 결정한다.
- **MobileNetV2** 는 Apollo510(CPU SIMD)에서 0.2~0.5초, STM32N6에서는 xSPI 대역폭 가정 때문에 **memory-bound**로 10 ms 안팎이다. Hailo·Orin은 0.2~2 ms.
- **0.5B LLM decode** 는 Orin Nano에서만 가능하고 토큰당 3.5~6.5 ms(약 150~280 tok/s 상한)다. 여기서도 병목은 memory다. MCU·Hailo-8은 weight가 들어가지 않는다.
- 마지막 줄: 광고 67 TOPS를 그대로 쓰면 MobileNetV2가 0.009 ms다. 범위 추정(0.16~1.9 ms)과 **18~210배** 차이가 난다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<text x="162.0" y="33.0" font-size="12" text-anchor="end">Apollo510 · KWS</text><rect x="388.8" y="24" width="35.3" height="12" fill="#4a7bd0" fill-opacity="0.8"/><text x="429.1" y="34.0" font-size="12" text-anchor="start">1.9–4.43 ms</text>
<text x="162.0" y="67.0" font-size="12" text-anchor="end">Apollo510 · MNv2</text><rect x="585.9" y="58" width="35.3" height="12" fill="#e08a3c" fill-opacity="0.8"/><text x="580.9" y="68.0" font-size="12" text-anchor="end">215–501 ms</text>
<text x="162.0" y="101.0" font-size="12" text-anchor="end">STM32N6 · KWS</text><rect x="225.7" y="92" width="66.9" height="12" fill="#4a7bd0" fill-opacity="0.8"/><text x="297.5" y="102.0" font-size="12" text-anchor="start">0.038–0.189 ms</text>
<text x="162.0" y="135.0" font-size="12" text-anchor="end">STM32N6 · MNv2</text><rect x="456.9" y="126" width="10.7" height="12" fill="#e08a3c" fill-opacity="0.8"/><text x="472.6" y="136.0" font-size="12" text-anchor="start">9.74–12.6 ms</text>
<text x="162.0" y="169.0" font-size="12" text-anchor="end">Hailo-8 · KWS</text><rect x="294.9" y="160" width="67.3" height="12" fill="#4a7bd0" fill-opacity="0.8"/><text x="367.2" y="170.0" font-size="12" text-anchor="start">0.2–1 ms</text>
<text x="162.0" y="203.0" font-size="12" text-anchor="end">Hailo-8 · MNv2</text><rect x="302.3" y="194" width="75.5" height="12" fill="#e08a3c" fill-opacity="0.8"/><text x="382.9" y="204.0" font-size="12" text-anchor="start">0.239–1.46 ms</text>
<text x="162.0" y="237.0" font-size="12" text-anchor="end">Orin Nano · KWS</text><rect x="266.4" y="228" width="95.9" height="12" fill="#4a7bd0" fill-opacity="0.8"/><text x="367.3" y="238.0" font-size="12" text-anchor="start">0.101–1.01 ms</text>
<text x="162.0" y="271.0" font-size="12" text-anchor="end">Orin Nano · MNv2</text><rect x="285.9" y="262" width="103.2" height="12" fill="#e08a3c" fill-opacity="0.8"/><text x="394.0" y="272.0" font-size="12" text-anchor="start">0.161–1.91 ms</text>
<text x="162.0" y="305.0" font-size="12" text-anchor="end">Orin Nano · LLM tok</text><rect x="414.6" y="296" width="25.4" height="12" fill="#d0564a" fill-opacity="0.8"/><text x="445.0" y="306.0" font-size="12" text-anchor="start">3.53–6.49 ms</text>
<line x1="170" y1="332" x2="650" y2="332" stroke="currentColor"/><line x1="170.0" y1="332" x2="170.0" y2="336" stroke="currentColor"/><text x="170.0" y="349.0" font-size="12" text-anchor="middle">0.01</text><line x1="266.0" y1="332" x2="266.0" y2="336" stroke="currentColor"/><text x="266.0" y="349.0" font-size="12" text-anchor="middle">0.1</text>
<line x1="362.0" y1="332" x2="362.0" y2="336" stroke="currentColor"/><text x="362.0" y="349.0" font-size="12" text-anchor="middle">1</text><line x1="458.0" y1="332" x2="458.0" y2="336" stroke="currentColor"/><text x="458.0" y="349.0" font-size="12" text-anchor="middle">10</text><line x1="554.0" y1="332" x2="554.0" y2="336" stroke="currentColor"/><text x="554.0" y="349.0" font-size="12" text-anchor="middle">100</text>
<line x1="650.0" y1="332" x2="650.0" y2="336" stroke="currentColor"/><text x="650.0" y="349.0" font-size="12" text-anchor="middle">1000</text><text x="410.0" y="366.0" font-size="13" text-anchor="middle">추정 지연 범위 (ms, log) — 파랑 KWS, 주황 MobileNetV2, 빨강 LLM 토큰</text>
</svg>
```

그림 6 — 칩 × 모델별 추정 지연 범위(ms, log). 막대 길이가 곧 "모르는 것의 크기"다. Hailo·Orin의 KWS 막대가 긴 이유는 대부분 오버헤드 가정의 폭 때문이다 — 이 폭을 줄이는 유일한 방법은 우리 모델로 실측하는 것이다.

### 6.3 실측 벤치마크로 검산하기 — Coral의 공개 표

정규화와 범위 추정이 맞는 방향인지 확인하려면, **벤더가 공개한 실측값**으로 역산해 보면 된다. Coral 문서는 모델별 실측 지연을 공개하므로 이것이 가능하다.

무엇을 확인하나: Coral 표의 USB Accelerator 실측(MobileNet v1 2.4 ms, ResNet-50 49 ms)을 4 TOPS peak와 비교해 실효 활용률을 내고, 파라미터 캐시를 넘는 weight가 USB로 얼마나 시간을 쓰는지 본다. MAC 수와 weight 크기는 공개 자릿수의 근사다.

```python
# Coral가 공개한 실측 지연(Edge TPU, USB)으로 "4 TOPS" 대비 실제 활용률을 역산한다
PEAK = 4e12                                  # Coral 문서의 Edge TPU peak ops/s
rows = [  # model, MAC/추론(공개 자릿수, 근사), int8 weight bytes(근사), Coral 표의 USB 측정 ms
    ("MobileNet v1 224", 569e6, 4.2e6,  2.4),
    ("ResNet-50 299",    7.3e9, 25.6e6, 49.0),  # 224 기준 ~4.1 GMAC x (299/224)^2 로 근사
]
CACHE = 8e6                                  # on-chip 파라미터 캐시 약 8 MB (E5 7절)
USB_BW = 0.5e9                               # USB 3, 5 Gb/s, 8b/10b 후 이론 상한
for name, macs, wb, ms in rows:
    eff = 2 * macs / (ms * 1e-3)
    naive = 2 * macs / PEAK * 1e3
    spill = max(0.0, wb - CACHE)
    print(f"{name:17s} naive {naive:6.2f} ms  measured {ms:5.1f} ms  "
          f"effective {eff/1e12:5.2f} TOPS = {eff/PEAK:5.1%} of peak")
    print(f"{'':17s} weight {wb/1e6:5.1f} MB, 캐시 초과 {spill/1e6:5.1f} MB -> USB로 매 추론 "
          f">= {spill/USB_BW*1e3:5.1f} ms")
```

```text
MobileNet v1 224  naive   0.28 ms  measured   2.4 ms  effective  0.47 TOPS = 11.9% of peak
                  weight   4.2 MB, 캐시 초과   0.0 MB -> USB로 매 추론 >=   0.0 ms
ResNet-50 299     naive   3.65 ms  measured  49.0 ms  effective  0.30 TOPS =  7.4% of peak
                  weight  25.6 MB, 캐시 초과  17.6 MB -> USB로 매 추론 >=  35.2 ms
```

출력에서 볼 것:

- MobileNet v1은 weight가 캐시 안에 들어가는데도 **peak의 12 %** 만 나온다. depthwise conv 같은 모양 문제와 고정 오버헤드(E5) 때문이다. 6.1절의 활용률 범위(5~60 %)가 터무니없지 않다는 근거다.
- ResNet-50은 weight 25.6 MB 중 17.6 MB가 캐시를 넘는다. 이것을 USB 이론 상한으로 매번 옮기는 데만 35 ms 이상 — 실측 49 ms의 대부분이 **weight 전송**으로 설명된다. 5.2절의 "Coral USB ridge 8000"이 실제로 드러나는 장면이다.
- 교훈: 벤더가 **모델별 실측표**를 주면 그것으로 정규화 가정을 검산한다. 주지 않으면 질문 목록(9절) 맨 위에 올린다.

---

## 7. 전력 숫자 읽기

### 7.1 전력 숫자에 붙어야 할 조건

TOPS에 조건이 있듯 전력 숫자에도 조건이 있다. D7 5절이 TOPS/W(효율)의 조건을 다뤘다면, 여기서는 **절대 전력·전류 숫자**를 읽는 법이다.

| 조건 | 왜 숫자를 바꾸나 | 확인할 곳 |
|---|---|---|
| typ / max | typ는 25 °C 대표값, max는 corner 보증 (1.2절) | 전기적 특성 표의 열 |
| 어느 rail | core rail(0.7 V대)인지, 칩 공급(1.8 V)인지, 배터리 쪽인지 | 표의 Symbol(IDD_CORE, VBAT…)과 각주 |
| 어떤 clock·OPP | 동적 전력은 f·V²에 비례 (D7 1.2절) | 조건 열 |
| 어떤 워크로드 | while(1), CoreMark, 특정 모델 | 각주 |
| 어디서 실행 | flash/MRAM에서 실행 vs cache·TCM에서 실행 | 조건 열 |
| 온도 | 누설은 온도에 지수적으로 민감 (E9 1.2절) | 조건 열, typical characteristics 그래프 |
| 무엇이 켜져 있나 | 발진기, RTC, retention SRAM 크기, wake source, 주변장치 | 조건 열 — 가장 중요 |
| 무엇이 빠졌나 | I/O 전류, 외부 레귤레이터 손실, DRAM, 무선 | 각주 |

### 7.2 "µW 헤드라인" 읽기 — 깊은 sleep의 함정

always-on 칩의 헤드라인은 거의 항상 **가장 깊은 모드**의 전류다. 실제 사례 두 개:

- Alif Ensemble: "For less than 1uA a real-time clock, a 32-bit timer, an analog comparator, and GPIO wake pins are all active and available. Additionally, 4KB of SRAM are retained." — 무엇이 켜져 있는지를 적은 좋은 표기다. 하지만 이 모드에서 retention되는 SRAM은 4 KB뿐이다. KWS 모델 weight(22 KB)와 오디오 링버퍼(32 KB)를 들고 있으려면 "more than 1uA" 쪽으로 가야 한다(3.2절 출처).
- Nordic nRF54L15 제품 페이지는 sleep 전류를 "from 0.7 μA to 2.9 μA (@ 3 V)" 같은 **범위**로 표기한다(출처: [nordicsemi.com/Products/nRF54L15](https://www.nordicsemi.com/Products/nRF54L15), 확인일 2026-10-03, 페이지가 동적 렌더링이라 요약 도구로 읽음 — 원문 표기 재확인 권장). 범위의 양 끝이 각각 어떤 모드(RAM retention 크기, RTC 유무)인지는 datasheet의 전류 표에서 확인한다.

말로 하면: µA 헤드라인은 "칩이 거의 죽어 있을 때" 숫자다. 우리 제품이 실제로 머무는 상태(필요한 SRAM을 들고, 센서·마이크 인터페이스를 켜고, 주기적으로 깨어나는 상태)의 평균 전력은 **우리 duty cycle로 직접 계산**해야 한다(E9 4절의 손익분기, D7 6절의 배터리 수학).

### 7.3 코드 — 우리 duty cycle의 평균 전력

무엇을 확인하나: 가상의 always-on 칩 "X"(공개 자료에서 본 자릿수를 흉내 낸 가정값)의 상태별 전력과 wake 시간으로, 네 가지 사용 시나리오의 평균 전력과 100 mAh 배터리 수명을 계산한다.

```python
# 가상의 always-on 칩 "X" 데이터시트 숫자로 우리 duty cycle의 평균 전력을 계산한다 (값은 공개 자료 자릿수를 흉내 낸 가정)
V = 3.0
P = {  # 상태별 전력 W
    "deep_sleep": 0.7e-6 * V,           # RTC만, RAM retention 없음 (헤드라인 숫자)
    "sleep_ret":  2.9e-6 * V,           # RTC + RAM 전부 retention
    "cpu":        29e-6 * 96,           # 29 uW/MHz x 96 MHz
    "npu":        15e-3,                # NPU burst (데이터시트에 없으면 물어야 할 값)
    "mic_fe":     0.6e-3,               # 마이크 + PDM 필터 상시 (칩 밖 + 칩 안)
}
WAKE_S = 100e-6                         # sleep -> run 복귀 시간, 그 동안 cpu 전력
def avg(events, base="sleep_ret", always=()):
    """events: (상태, 1회 지속 s, 초당 횟수). 나머지 시간은 base 상태."""
    busy = sum((d + WAKE_S) * n for _, d, n in events)
    e = sum((P[s] * d + P["cpu"] * WAKE_S) * n for s, d, n in events)
    return e + P[base] * (1 - busy) + sum(P[a] for a in always), busy
cases = {
    "A headline: deep sleep only": ([], "deep_sleep", ()),
    "B 손목 제스처: 1 Hz wake, 2 ms CPU": ([("cpu", 2e-3, 1)], "sleep_ret", ()),
    "C KWS(NPU): 10 ms feature + 100 ms 추론": ([("cpu", 0.3e-3, 100), ("npu", 0.2e-3, 10)], "sleep_ret", ("mic_fe",)),
    "D KWS(CPU only, Helium)": ([("cpu", 0.3e-3, 100), ("cpu", 4.4e-3, 10)], "sleep_ret", ("mic_fe",)),
}
BATT_WH = 0.100 * 3.7                   # 100 mAh, 3.7 V
for name, (ev, base, alw) in cases.items():
    p, busy = avg(ev, base, alw)
    print(f"{name:40s} avg {p*1e6:8.1f} uW  active {busy:6.2%}  battery {BATT_WH/p/24:8.1f} days")
```

```text
A headline: deep sleep only              avg      2.1 uW  active  0.00%  battery   7341.3 days
B 손목 제스처: 1 Hz wake, 2 ms CPU            avg     14.5 uW  active  0.21%  battery   1061.2 days
C KWS(NPU): 10 ms feature + 100 ms 추론    avg    752.5 uW  active  4.30%  battery     20.5 days
D KWS(CPU only, Helium)                  avg    844.6 uW  active  8.50%  battery     18.3 days
```

출력에서 볼 것:

- 헤드라인(A) 2.1 µW → 손목 제스처(B) 14.5 µW → 상시 KWS(C) 752 µW. **같은 칩의 같은 datasheet에서 350배**가 벌어진다. 차이를 만든 것은 칩이 아니라 duty cycle이다.
- C에서 752 µW 중 600 µW는 **마이크·PDM 프런트엔드 상시 전력**이다. NPU(C)와 CPU-only(D)의 차이는 92 µW뿐이다. 즉 이 시나리오에서는 "NPU가 몇 TOPS/W인가"보다 "오디오 프런트엔드를 얼마나 싸게 상시 켤 수 있나(저전력 audio ADC, 하드웨어 VAD)"가 배터리를 정한다. 데이터시트의 always-on audio 블록 전력이 TOPS보다 중요한 스펙이다.
- B에서 wake 1회 비용(100 µs)이 작아 보이지만 1 Hz × 100 µs × 2.8 mW = 0.28 µW로, 수 µW 예산에서는 무시할 수 없다. 깨어나는 시간과 그 동안의 전류는 datasheet에 꼭 있어야 하는 숫자다.

### 7.4 코드 — 기준 전압과 온도가 바꾸는 것

무엇을 확인하나: µW/MHz와 µA/MHz를 오갈 때 기준 전압이 결과를 얼마나 바꾸는지, CoreMark/mA를 에너지로 바꾸면 무엇인지, 그리고 sleep 전류의 온도 의존이 평균 전력을 얼마나 바꾸는지 본다(온도 계수는 가정).

```python
# 전력 단위의 기준 전압과 온도 조건을 바꿔 보면 같은 칩의 "숫자"가 얼마나 달라지는지 본다
def ua_per_mhz(uw_per_mhz, v): return uw_per_mhz / v
for v in (0.75, 1.8, 3.0):   # 코어 rail? 칩 공급? 배터리? — 데이터시트가 말해 줘야 한다
    print(f"29 uW/MHz 를 {v:4.2f} V 기준 전류로 보면 {ua_per_mhz(29, v):6.1f} uA/MHz")
# 경쟁 칩 Y가 "20 uA/MHz @ 3.0 V"라고 쓰면?  같은 전력 단위로 맞춰 비교
print(f"Y: 20 uA/MHz @3.0V = {20*3.0:.0f} uW/MHz  vs  X: 29 uW/MHz  -> 숫자만 보면 Y가 좋아 보이지만 X가 {60/29:.1f}x 효율")
# CoreMark/mA (Nordic식 표기) -> 같은 일 하나당 에너지
cm_per_ma, v = 193, 3.0
print(f"193 CoreMark/mA @3V -> {v*1e3/cm_per_ma:5.2f} uJ per CoreMark iteration (= {v*1e-3/cm_per_ma*1e6:.1f} uW per CoreMark/s)")
# 온도: sleep 전류 typ는 보통 25 C. 누설이 20 C마다 2배라고 '가정'하면 (실제는 datasheet 그래프로)
base_ua, t0 = 2.9, 25
active_uw = 29 * 96 * (2e-3 + 100e-6)          # ex6 케이스 B의 active 부분 (1 Hz)
for t in (25, 37, 45, 60, 85):
    i = base_ua * 2 ** ((t - t0) / 20)
    print(f"  {t:3d} C: sleep {i:5.1f} uA -> 손목 제스처 케이스 평균 {(i*3.0 + active_uw):5.1f} uW")
```

```text
29 uW/MHz 를 0.75 V 기준 전류로 보면   38.7 uA/MHz
29 uW/MHz 를 1.80 V 기준 전류로 보면   16.1 uA/MHz
29 uW/MHz 를 3.00 V 기준 전류로 보면    9.7 uA/MHz
Y: 20 uA/MHz @3.0V = 60 uW/MHz  vs  X: 29 uW/MHz  -> 숫자만 보면 Y가 좋아 보이지만 X가 2.1x 효율
193 CoreMark/mA @3V -> 15.54 uJ per CoreMark iteration (= 15.5 uW per CoreMark/s)
   25 C: sleep   2.9 uA -> 손목 제스처 케이스 평균  14.5 uW
   37 C: sleep   4.4 uA -> 손목 제스처 케이스 평균  19.0 uW
   45 C: sleep   5.8 uA -> 손목 제스처 케이스 평균  23.2 uW
   60 C: sleep   9.8 uA -> 손목 제스처 케이스 평균  35.1 uW
   85 C: sleep  23.2 uA -> 손목 제스처 케이스 평균  75.4 uW
```

출력에서 볼 것:

- 같은 29 µW/MHz가 기준 전압에 따라 9.7~38.7 µA/MHz로 4배 다르게 읽힌다. 경쟁 칩이 "20 µA/MHz @3.0 V"라고 쓰면 숫자는 작아 보이지만 전력으로는 60 µW/MHz, 즉 **2.1배 나쁘다.** 단위를 전력(µW/MHz) 또는 에너지(µJ/작업)로 통일하고 나서 비교한다.
- "193 CoreMark/mA @3V"는 CoreMark 1회 반복당 15.5 µJ라는 뜻이다. "/mA" 표기는 전압이 빠지면 의미가 없다 — Nordic은 "@ 3 V"를 붙였다.
- sleep 전류가 25 °C typ 2.9 µA에서 37 °C(피부 근처)에 4.4 µA, 60 °C(여름 차 안)에 9.8 µA가 되면, 제스처 시나리오 평균 전력은 14.5 → 19.0 → 35.1 µW다. **누설이 지배하는 저전력 영역에서는 온도 조건이 duty cycle만큼 중요하다.** 실제 계수는 datasheet의 "typical characteristics" 그래프에서 읽는다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
<text x="182.0" y="36.0" font-size="12" text-anchor="end">A 헤드라인 deep sleep</text><rect x="190" y="22" width="43.0" height="20" fill="#3f9a6b" fill-opacity="0.8"/><text x="238.0" y="36.0" font-size="12" text-anchor="start">2.1 µW</text>
<text x="182.0" y="72.0" font-size="12" text-anchor="end">B 제스처 1 Hz @25 C</text><rect x="190" y="58" width="154.8" height="20" fill="#4a7bd0" fill-opacity="0.8"/><text x="349.8" y="72.0" font-size="12" text-anchor="start">14.5 µW</text>
<text x="182.0" y="108.0" font-size="12" text-anchor="end">B 제스처 1 Hz @60 C</text><rect x="190" y="94" width="206.0" height="20" fill="#4a7bd0" fill-opacity="0.8"/><text x="401.0" y="108.0" font-size="12" text-anchor="start">35.1 µW</text>
<text x="182.0" y="144.0" font-size="12" text-anchor="end">C KWS (NPU)</text><rect x="190" y="130" width="383.5" height="20" fill="#e08a3c" fill-opacity="0.8"/><text x="578.5" y="144.0" font-size="12" text-anchor="start">752.5 µW</text>
<text x="182.0" y="180.0" font-size="12" text-anchor="end">D KWS (CPU only)</text><rect x="190" y="166" width="390.2" height="20" fill="#d0564a" fill-opacity="0.8"/><text x="585.2" y="180.0" font-size="12" text-anchor="start">844.6 µW</text>
<line x1="190" y1="206" x2="590" y2="206" stroke="currentColor"/><line x1="190.0" y1="206" x2="190.0" y2="210" stroke="currentColor"/><text x="190.0" y="223.0" font-size="12" text-anchor="middle">1</text><line x1="323.3" y1="206" x2="323.3" y2="210" stroke="currentColor"/><text x="323.3" y="223.0" font-size="12" text-anchor="middle">10</text>
<line x1="456.7" y1="206" x2="456.7" y2="210" stroke="currentColor"/><text x="456.7" y="223.0" font-size="12" text-anchor="middle">100</text><line x1="590.0" y1="206" x2="590.0" y2="210" stroke="currentColor"/><text x="590.0" y="223.0" font-size="12" text-anchor="middle">1000</text><text x="390.0" y="242.0" font-size="13" text-anchor="middle">평균 전력 (µW, log) — 같은 가상 칩 X, 다른 duty cycle·온도</text>
</svg>
```

그림 7 — 같은 가상 칩 X의 평균 전력(µW, log). 초록은 데이터시트 헤드라인, 파랑은 손목 제스처(25 °C·60 °C), 주황·빨강은 상시 KWS. 헤드라인과 실제 사용 사이에 2~3자릿수가 있다.

### 7.5 "typical power"를 읽는 법 (가속기)

Hailo-8의 "typical power consumption of 2.5W" 같은 문장을 만나면 다음 순서로 읽는다.

1. 무엇이 "typical"한가: 어떤 모델·FPS의 측정 평균인가? peak TOPS를 낼 때인가?
2. 경계: 칩 die만? 패키지? M.2 모듈 전체(레귤레이터 포함)? 호스트 PCIe 링크 전력은?
3. idle 전력: 추론하지 않을 때 몇 mW인가? 링크를 끄면(PCIe L1 sub-state 등) 얼마나 내려가나? 웨어러블·배터리 기기에서는 이것이 평균 전력을 정한다.
4. 온도: 지속 부하에서 throttling 없이 유지되는 전력은? 방열 조건은?

---

## 8. 벤치마크 주장 읽기

벤더가 숫자를 "측정값"으로 제시할 때는 아래 필드가 모두 있어야 비교할 수 있다. I5 3.1절의 표가 무엇이 부풀려지는지를 다뤘다면, 여기서는 **문장 하나를 받았을 때 채워 넣는 양식**이다.

| 필드 | 예 (좋은 표기) | 사례에서 빠진 것 |
|---|---|---|
| 모델과 버전 | MobileNet v1, 224×224 | AR1+ "Llama 3.2 1B" — 양자화 형식 없음 |
| 정밀도 | INT8 per-channel | Hailo brief 전반 |
| batch | 1 | 대부분 미표기 |
| 지표 | p50·p90 지연(ms), 처리량(FPS), TTFT, tok/s | Jetson "1.7X generative AI performance" — 어떤 지표인지 없음 |
| 포함 범위 | 모델만 / 전처리 포함 / end-to-end | Coral은 "model only"라고 적었다 (좋은 예) |
| 입력 | 프롬프트 길이, 이미지 크기 | AR1+ TTFT 약 1.2초 — 프롬프트 길이 없음 |
| 하드웨어 조건 | 전력 모드, clock, 온도, 방열 | Jetson 숫자의 전력 모드 |
| 소프트웨어 버전 | SDK·컴파일러·드라이버 버전 | 대부분 미표기 |
| 정확도 | FP32 대비 delta | 대부분 미표기 |

TTFT 같은 LLM 숫자는 특히 조건이 많이 필요하다. TTFT ≈ prefill 시간 + 첫 decode 1회인데(D5), prefill 시간은 **프롬프트 길이에 거의 비례**한다. 프롬프트 길이 없는 "TTFT 1.2초"는 "몇 토큰을 1.2초에 처리했는지" 알 수 없으므로 prefill 속도(tok/s)로 바꿀 수 없다. 벤더에게는 "prompt 128/512/1024 토큰에서의 TTFT, 정밀도, KV cache 위치"를 묻는다.

---

## 9. 빠진 정보 → 벤더 FAE에게 묻기

### 9.1 코드 — 체크리스트 생성기

무엇을 확인하나: 3절에서 각 자료가 실제로 공개한 필드를 적어 두고, 빠진 필드를 FAE 질문으로 자동 변환한다. 질문 문구는 9.2절 템플릿의 일부다.

```python
# 데이터시트별로 "공개된 필드"를 적어 두면, 빠진 필드를 FAE 질문으로 바꿔 준다
Q = {  # 필드 -> 빠졌을 때 물을 질문
    "precision":  "Headline TOPS는 어떤 정밀도(INT4/INT8/FP16) 기준인가?",
    "sparsity":   "Dense인가, structured sparsity 2배 크레딧이 포함됐나?",
    "op_def":     "1 MAC을 2 op로 세나? MAC/cycle 구성과 clock은?",
    "scope":      "어느 엔진의 숫자인가(NPU만? GPU+DLA 합? CPU 포함)?",
    "clock":      "그 TOPS는 어떤 clock·전압(OPP)에서이고, 지속 가능한 clock은?",
    "mem_bw":     "NPU가 weight를 읽는 경로의 이론/실효 대역폭은? 다른 마스터와 공유하나?",
    "sram":       "NPU 전용 SRAM 크기와 retention 가능 여부는?",
    "power_cond": "전력 숫자는 typ/max 중 무엇이고 어떤 rail·온도·워크로드에서 쟀나?",
    "sleep_ret":  "sleep 전류는 RAM 몇 KB retention, 어떤 wake source 켠 상태인가?",
    "wake_time":  "sleep -> 추론 가능 상태까지 시간과 그 동안의 전류는?",
    "bench":      "공개 벤치마크의 모델·입력 크기·batch·정밀도·포함 범위(전처리?)는?",
    "our_model":  "우리 ONNX/TFLite 모델로 end-to-end 지연·mJ를 재 줄 수 있나?",
}
SHEETS = {  # 각 공개 자료에서 실제로 확인한 필드 (이 노트 3절)
    "STM32N6 PR":        {"op_def", "sram"},
    "Ethos-U85 page":    {"op_def", "clock"},
    "Apollo510 PR/page": {"clock", "sram"},
    "Hailo-8 brief":     {"power_cond"},          # 'typical 2.5W'만 — 조건 없음
    "Coral docs":        {"precision", "bench"},
    "Jetson Orin page":  {"precision", "sparsity", "scope", "mem_bw", "clock"},
    "AR1 Gen 1 brief":   {"precision"},
}
for name, have in SHEETS.items():
    miss = [k for k in Q if k not in have]
    print(f"{name:18s} 공개 {len(have):2d}/{len(Q)}  질문 {len(miss):2d}개: {', '.join(miss[:4])}...")
print("\n[Hailo-8 brief -> FAE 메일 초안]")
for i, k in enumerate([k for k in Q if k not in SHEETS["Hailo-8 brief"]][:6], 1):
    print(f"{i}. {Q[k]}")
```

```text
STM32N6 PR         공개  2/12  질문 10개: precision, sparsity, scope, clock...
Ethos-U85 page     공개  2/12  질문 10개: precision, sparsity, scope, mem_bw...
Apollo510 PR/page  공개  2/12  질문 10개: precision, sparsity, op_def, scope...
Hailo-8 brief      공개  1/12  질문 11개: precision, sparsity, op_def, scope...
Coral docs         공개  2/12  질문 10개: sparsity, op_def, scope, clock...
Jetson Orin page   공개  5/12  질문  7개: op_def, sram, power_cond, sleep_ret...
AR1 Gen 1 brief    공개  1/12  질문 11개: sparsity, op_def, scope, clock...

[Hailo-8 brief -> FAE 메일 초안]
1. Headline TOPS는 어떤 정밀도(INT4/INT8/FP16) 기준인가?
2. Dense인가, structured sparsity 2배 크레딧이 포함됐나?
3. 1 MAC을 2 op로 세나? MAC/cycle 구성과 clock은?
4. 어느 엔진의 숫자인가(NPU만? GPU+DLA 합? CPU 포함)?
5. 그 TOPS는 어떤 clock·전압(OPP)에서이고, 지속 가능한 clock은?
6. NPU가 weight를 읽는 경로의 이론/실효 대역폭은? 다른 마스터와 공유하나?
```

출력에서 볼 것: 가장 많이 공개한 Jetson 표도 12개 중 5개다. 이 칩들이 나쁜 것이 아니라, **공개 자료는 원래 비교에 필요한 정보의 절반도 주지 않는다.** 빠진 필드 목록이 곧 FAE 미팅 안건이다. 템플릿이라 칩에 안 맞는 질문(예: DRAM이 없는 Hailo에 대역폭 공유 질문)은 보내기 전에 지우거나 바꾼다.

### 9.2 질문 템플릿 — 24개

FAE(Field Application Engineer)는 벤더 쪽 기술 창구다. 첫 미팅 전에 메일로 보내고, NDA 아래 datasheet·errata·내부 벤치마크를 받는 것이 목표다.

**연산 성능**

1. 헤드라인 TOPS는 어떤 정밀도(INT4 / INT8 / FP16) 기준인가? 정밀도별 처리량을 각각 달라.
2. dense 숫자인가, structured sparsity(2:4) 크레딧이 포함됐나?
3. 1 MAC을 2 op로 세나? NPU의 MAC/cycle 구성과 clock은?
4. 이 숫자는 어느 엔진의 것인가 — NPU만, GPU+DLA 합, CPU 포함 platform 합?
5. 그 TOPS는 어떤 OPP(clock·전압)에서인가? 우리 폼팩터의 열 조건에서 **지속 가능한** clock은?
6. 우리 모델의 op(depthwise, GEMV, attention, LayerNorm, GELU, LSTM) 중 NPU에서 돌지 않고 CPU로 fallback되는 것은?

**메모리**

7. NPU 전용 SRAM은 몇 KB/MB인가? 다른 IP(ISP, display, DSP)와 공유하나?
8. 외부 메모리의 버스 폭과 전송률(MT/s)은? NPU가 쓸 수 있는 실효 대역폭 측정값이 있나?
9. 카메라·display가 동시에 돌 때 NPU 대역폭은 얼마나 줄어드나? QoS 설정이 있나?
10. weight를 flash/xSPI에서 바로 실행(XIP)할 때와 SRAM에 올렸을 때의 지연 차이는?

**전력**

11. "typical" 전력은 어떤 모델·처리량·온도·rail에서 잰 값인가? max는?
12. NPU idle 전력과 power-gating 후 전력은? 다시 켜는 데 걸리는 시간과 에너지는?
13. sleep 전류 표에서 RAM retention 크기별 전류는? (KB당 증가분)
14. sleep → 추론 가능 상태까지의 wake 시간과 그 동안의 전류 프로파일은?
15. always-on audio 경로(마이크 인터페이스, 저전력 ADC, 하드웨어 VAD)의 상시 전력은?
16. 전력 숫자는 칩 die 기준인가, PMIC·레귤레이터 손실을 포함한 배터리 기준인가?
17. 25 °C 외에 37 °C·60 °C에서의 sleep 전류와 지속 성능은?

**벤치마크와 검증**

18. 공개 벤치마크의 모델·입력 크기·batch·정밀도·포함 범위(전처리 포함 여부)·SDK 버전은?
19. 우리 ONNX/TFLite 모델 세 개를 컴파일해 layer별 cycle·SRAM 사용량 리포트를 줄 수 있나?
20. EVB에서 우리 모델의 end-to-end p50/p90 지연과 추론당 mJ를 재 줄 수 있나? 측정 방법은?
21. LLM이라면: 정밀도·KV cache 위치·prompt 길이별 TTFT와 decode tok/s는?

**제품화**

22. 현재 silicon revision의 errata 목록과 NPU·전원 관리 관련 항목은?
23. 컴파일러·런타임 릴리스 주기, 공개 이슈 트래커, 지난 1년간 성능 회귀 이력은?
24. 이 SKU의 양산 일정, 장기 공급 보증 기간, 온도 등급(consumer/industrial)은?

---

## 10. Red flag — benchmarketing 패턴

이런 표기를 보면 숫자를 "질문"으로 분류한다. 틀렸다는 뜻이 아니라, 그대로는 비교에 쓸 수 없다는 뜻이다.

| 패턴 | 왜 위험한가 | 사례에서 |
|---|---|---|
| 정밀도 없는 TOPS | INT4면 INT8 기준 절반 | Hailo-8 brief |
| "sparse"가 작은 글씨 또는 표 안에만 | dense 모델은 절반 | Jetson 헤드라인 (표에는 분해가 있다) |
| 합산 TOPS (AI TOPS, platform TOPS) | 한 모델은 한 엔진에서 돈다 | Orin NX 157, Lunar Lake 120 |
| 기준 없는 배수 ("30x", "600x") | 분자·분모·기준선 미상 | Apollo510, STM32N6 |
| 같은 칩에 서로 다른 배수 여러 개 | 지표를 바꿔 가며 가장 큰 값을 고름 | Apollo510 30x / 10x / 2x / 300x |
| 패밀리 최대값을 SKU처럼 표기 ("scales up to") | 고른 SKU는 더 작을 수 있다 | Alif "250+ GOPS" |
| peak TOPS ÷ typical W로 만든 TOPS/W | 서로 다른 동작점의 나눗셈 (D7 5.2절) | Hailo 26 TOPS / 2.5 W |
| µA 헤드라인에 retention 크기 없음 | 우리 상태와 다른 모드 | (반례: Alif는 "4KB retained"를 적었다) |
| "/mA", "/MHz" 단위에 전압 없음 | 기준 전압에 따라 4배 | 7.4절 |
| 버스 폭 없는 메모리 속도 | 대역폭 계산 불가 | AR1 Gen 1 "up to 2100 MHz" |
| 벤치마크에 포함 범위·batch 없음 | 전처리·batch 효과 숨김 | Jetson "1.7X" |
| 벤더가 고른 모델만 공개 | 그 가속기에 유리한 모양 | 대부분 (I5 3.1절) |
| IP 페이지 숫자를 칩 숫자로 인용 | 칩 회사가 구성·clock을 줄였을 수 있다 | Ethos-U85 "4 TOPs at 1 GHz" |

---

## 11. 임베디드 관점에서 다시 보기

데이터시트 해석은 결국 **펌웨어 엔지니어가 칩 bring-up 전에 하는 일**과 같다. Don이 SSD·RF에서 하던 것을 ML 가속기 언어로 옮기면 이렇다.

- **사양 → 검증 계획**: datasheet에서 뽑은 숫자 하나하나에 "EVB에서 어떻게 재나"를 붙인다. TOPS → 우리 모델 layer별 cycle, typical power → 전류 프로브로 추론당 µJ(D7 8절), sleep 전류 → retention 크기별·온도별 측정(E9).
- **errata 먼저**: SSD 컨트롤러에서 errata 한 줄이 펌웨어 설계를 바꿨듯, NPU errata("특정 stride의 DMA가 잘못된 주소를 읽는다", "power gating 후 첫 job의 결과가 틀린다")는 런타임 설계를 바꾼다. 선정 단계에서 errata를 요청하지 않으면, bring-up 단계에서 같은 정보를 비싸게 얻는다.
- **corner 사고**: RF sign-off처럼 typ 하나로 결정하지 않는다. 배터리 전압 끝(3.0 V vs 4.2 V), 피부 온도·여름 온도, 최악 모델 변형에서의 지연·전력을 표로 만든다.
- **숫자의 출생증명서**: 비교표의 모든 칸에 "출처 문서·쪽·조건·확인일"을 남긴다(I5 7.1절의 환경 지문과 같은 정신). 6개월 뒤 벤더가 숫자를 바꿨을 때 무엇이 바뀌었는지 추적할 수 있다.

```
비교표 한 칸의 형식 (예)
  STM32N6 · NPU peak · 0.6 TOPS (dense INT8 가정)
    출처: ST 보도자료 p4665, "nearly 300 … MAC units achieving up to 600 GOPS", 확인 2026-10-03
    조건: clock 미표기 (역산 ~1 GHz), 정밀도 미표기
    상태: 질문 Q1·Q3 발송, datasheet 대기
```

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| GOPS와 TOPS를 같은 축에 그냥 놓음 | MCU NPU가 큰 가속기보다 빨라 보임 | 단위 1000배 착각 | 모든 숫자를 ops/s로 바꾼 뒤 비교 |
| sparse 헤드라인을 dense 모델에 적용 | 예상 지연의 절반밖에 안 나옴 | 2:4 크레딧 | dense 값(표 안)을 찾거나 ÷ 2 |
| platform TOPS로 모델 하나의 지연 추정 | 실측이 3~4배 느림 | CPU+GPU+NPU 합 | 모델이 도는 엔진의 숫자만 사용 |
| peak TOPS ÷ typical W를 효율로 사용 | 배터리 수명이 한 자릿수 낙관적 | 다른 동작점끼리 나눔 | 같은 조건의 추론당 mJ를 요구 (D7) |
| µA 헤드라인으로 배터리 계산 | 수명이 수십~수백 배 낙관적 | 가장 깊은 모드, 우리 상태가 아님 | 우리 duty cycle로 평균 전력 계산 (예제 6) |
| 25 °C typ로만 전력 예산 | 여름·착용 중 배터리가 빨리 닳음 | 누설의 온도 의존 | 37 °C·60 °C 숫자 또는 그래프 확인 |
| µA/MHz를 전압 없이 비교 | 더 나쁜 칩을 고름 | 기준 전압 차이 | µW/MHz나 µJ/작업으로 통일 |
| 버스 폭 없이 메모리 속도로 LLM 속도 추정 | tok/s 예측이 2~4배 틀림 | MHz·MT/s·폭 미상 | 폭과 MT/s를 질문, 해석별 범위 제시 |
| IP 페이지의 "4 TOPs @1 GHz"를 칩 성능으로 인용 | 칩 실측이 몇 분의 1 | 칩 구성·clock이 더 작음 | 칩 datasheet의 구성·clock 확인 |
| errata를 선정 후에 읽음 | bring-up에서 기능 포기·우회 | 결정 순서 | 후보 목록 단계에서 errata 요청 |

---

## 13. 면접에서 이렇게 말한다

**Q.** "A vendor tells you their chip does 40 TOPS. What questions do you ask?"

**A.** 숫자를 버리지 않고 조건을 채운다. 정밀도(INT4/INT8), dense인지 sparse인지, 어느 엔진의 합인지, 어떤 clock·전력 모드에서인지 묻는다. 그 다음 메모리(NPU가 쓰는 SRAM 크기, DRAM 폭과 실효 대역폭, 공유 여부)를 묻고, 마지막으로 우리 모델 세 개를 그들 툴체인으로 컴파일한 layer별 리포트와 EVB의 end-to-end 지연·추론당 mJ를 요청한다.

> I'd treat 40 TOPS as the start of a conversation. First I normalize it: which precision, dense or sparse, which engine or sum of engines, and at what clock and power mode. Then memory, because most of our models are bandwidth-bound: on-chip SRAM size, DRAM width and effective bandwidth, and what else shares it. Finally I ask them to compile our three representative models and give me per-layer cycles plus end-to-end latency and millijoules per inference on their EVB.

**Q.** "Why do 'AI TOPS' numbers for SoCs often add CPU, GPU and NPU together?"

**A.** 마케팅상 숫자가 커지고, 여러 모델을 여러 엔진에 동시에 올리면 시스템 처리량이 합에 가까워질 수는 있기 때문이다. 하지만 모델 하나는 한 엔진에서 돌고, 엔진들은 같은 DRAM 대역폭과 전력·열 예산을 나눈다. 그래서 엔진별 표를 찾는다 — 예를 들어 Jetson Orin NX의 157은 GPU 77 + DLA 80의 sparse 합이고, GPU dense INT8은 38이다.

> Because the sum is bigger and it's defensible for multi-model throughput. But a single model runs mostly on one engine, and all engines share the same DRAM bandwidth and thermal budget. So I always look for the per-engine table. Jetson Orin NX's 157 TOPS, for example, is GPU plus DLA with sparsity; the GPU's dense INT8 number is 38.

**Q.** "How do you read microwatt claims for always-on chips?"

**A.** 헤드라인 µA는 거의 항상 가장 깊은 모드다. 그 모드에서 무엇이 켜져 있는지(RTC, wake source, retention SRAM 크기), 전압·온도, typ인지 max인지를 본다. 그 다음 우리 duty cycle(필요한 SRAM retention, 마이크 프런트엔드 상시 전력, wake 빈도와 wake 시간)로 평균 전력을 직접 계산한다. 같은 칩에서 2 µW 헤드라인이 상시 KWS 시나리오에서는 수백 µW가 된다.

> The headline is the deepest sleep mode, so I check what's still on — RTC, wake sources, how many kilobytes of SRAM are retained — plus voltage, temperature, and typ versus max. Then I compute average power for our actual duty cycle: retained memory, always-on audio front end, wake rate and wake time. In my own model the same chip went from a 2 microwatt headline to about 750 microwatts for always-on keyword spotting, mostly from the audio front end.

**Q.** "What's the difference between a product brief and a datasheet?"

**A.** product brief는 1~2쪽 마케팅·영업 문서로 기능 목록과 헤드라인 숫자를 준다. datasheet는 전기적 특성을 min/typ/max와 측정 조건 열로 **약속**하는 엔지니어링 문서다. 선정은 brief로 후보를 추리고, datasheet·errata·우리 실측으로 결정한다. 실제로 Snapdragon AR1 Gen 1 brief에는 NPU TOPS도, 메모리 버스 폭도 없다.

> A product brief is a one or two page sales document: features and headline numbers, few conditions. A datasheet is an engineering commitment: min, typ and max with test conditions and footnotes about how each limit is guaranteed. I use briefs to build the shortlist and datasheets, errata and our own measurements to decide. For example, one smart-glasses platform brief I read lists INT4 and INT8 support but no TOPS and no memory bus width.

**Q.** "Given only TOPS and memory bandwidth from a datasheet, how would you estimate latency for your model?"

**A.** 먼저 TOPS를 dense INT8 한 엔진 기준으로 정규화하고, ridge point = TOPS ÷ 대역폭을 낸다. 모델의 arithmetic intensity가 ridge보다 낮으면 memory-bound라 weight bytes ÷ 실효 대역폭이 하한이고, 높으면 MAC ÷ (peak × 활용률)이다. 활용률은 10~60 % 범위로 두고, 고정 오버헤드를 더해 **범위**로 답한다. 그리고 벤더 실측표가 있으면 그것으로 역산해 범위를 좁힌다 — Coral의 MobileNet v1 실측은 peak의 약 12 %였다.

> I normalize the TOPS to dense INT8 on one engine, compute the ridge point, and compare it with the model's arithmetic intensity. Below the ridge, latency is bounded by weight bytes over effective bandwidth; above it, by MACs over peak times a utilization range. I add a fixed invocation overhead and report a best-to-worst range, then calibrate with any published measurements — Coral's MobileNet v1 number, for instance, works out to roughly 12 percent of peak.

**Q.** "Tell me how your hardware background helps with silicon selection."

**A.** SSD 시절 NAND datasheet의 typ/max와 컨트롤러 errata로 펌웨어 설계를 정했고, Apple RF에서는 새 칩셋을 온도·전압 corner로 sign-off했다. NPU 선정도 같은 일이다 — 헤드라인을 조건으로 분해하고, 빠진 것을 FAE 질문으로 만들고, EVB 실측으로 닫는다. ML 쪽에서 새로 배운 것은 TOPS·sparse·합산 표기 같은 단위 관행이다.

> In SSD firmware I designed around NAND datasheet typ and max values and controller errata, and at Apple I signed off new RF chipsets across temperature and voltage corners. Choosing an NPU is the same discipline: break the headline into its conditions, turn the gaps into FAE questions, and close it with measurements on an EVB. What was new for me was the vocabulary — sparse TOPS, platform TOPS, INT4 credits — not the method.

---

## 14. 직접 해보기

1. 손계산: Ethos-U55 256 MAC 구성이 400 MHz에서 돈다. peak GOPS는? 이 NPU가 DS-CNN KWS(2.66 M MAC)를 활용률 25 %로 돌리면 몇 ms인가?
   정답: 256 × 2 × 0.4 GHz = 204.8 GOPS. 2 × 2.66 M ÷ (204.8 G × 0.25) ≈ 0.104 ms.

2. 손계산: 어떤 SoC가 "45 AI TOPS (INT4, sparse)"라고 광고한다. dense INT8로 정규화하면? 이 SoC의 LPDDR5X가 64-bit, 8533 MT/s라면 ridge point는?
   정답: 45 ÷ 2 ÷ 2 = 11.25 TOPS. 대역폭 64 × 8533 ÷ 8 ≈ 68.3 GB/s → ridge ≈ 165 op/B (이론 대역폭 기준).

3. 손계산: sleep 전류 "1.5 µA typ @3.0 V, 25 °C, 32 KB retention"인 칩이 2초마다 깨어나 3 ms 동안 3 mW를 쓴다(wake 시간 무시). 평균 전력은? 150 mAh·3.7 V 배터리로 며칠인가(이 칩만 고려)?
   정답: sleep 4.5 µW × (1 − 0.0015) + 3 mW × 0.0015 = 4.49 + 4.5 ≈ 9.0 µW. 0.555 Wh ÷ 9.0 µW ≈ 61,700 h ≈ 2,570일.

4. 코드 과제: 예제 2의 `SPEC`에 관심 있는 칩 두 개(예: NXP i.MX 93의 Ethos-U65, MediaTek의 웨어러블 칩)를 공개 자료에서 찾아 추가하고, 원문 인용과 확인일을 주석으로 단다. flag가 가장 많은 칩은?
   힌트: 정밀도와 sparse 표기를 먼저 찾는다. 찾지 못한 필드는 None으로 두면 flag가 붙는다.

5. 코드 과제: 예제 6에 "PMIC 정지 전류 1 µA"와 "BLE advertising 1초마다 2 ms × 5 mW"를 추가해 B 시나리오의 평균 전력이 어떻게 바뀌는지 보라. 무엇이 칩 sleep 전류보다 커지나?
   정답 방향: BLE 평균 ≈ 10 µW로 B의 14.5 µW에 맞먹는다. 무선·PMIC가 칩 datasheet 밖에서 예산을 먹는다.

6. 조사 과제: 관심 있는 칩 하나의 **errata** 문서를 찾아 NPU·DMA·저전력 모드 관련 항목을 세 줄로 요약하고, 각 항목이 9.2절의 어느 질문과 연결되는지 적어라.
   힌트: 벤더 사이트에서 "errata sheet" 또는 "silicon errata"와 품번을 함께 검색한다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| product brief | 제품 요약서 | 1~2쪽 영업 문서. 기능과 헤드라인 숫자 |
| datasheet | 데이터시트 | 전기적 특성을 min/typ/max와 조건으로 약속하는 문서 |
| reference manual (RM, TRM) | 레퍼런스 매뉴얼 | 레지스터·동작 모드·memory map. 성능 숫자는 적다 |
| errata | 정오표 | 알려진 silicon 결함과 workaround |
| FAE | Field Application Engineer | 벤더의 고객 기술 지원 엔지니어 |
| typ / max | 전형값 / 최대값 | typ는 대표값(약속 아님), max는 조건 범위 전체의 보증 |
| characterization | 특성 평가 | 생산 테스트 대신 샘플을 corner에서 재서 보증 |
| TOPS / TOPs / GOPS | 초당 10¹² / 10¹² / 10⁹ op | op 정의(보통 1 MAC = 2 op)를 확인 |
| dense / sparse TOPS | 0 건너뛰기 크레딧 없음 / 있음 | sparse = dense × 2 (2:4) |
| platform TOPS, AI TOPS | 여러 엔진 합 | 모델 하나의 지연에는 의미가 약하다 |
| DLA | Deep Learning Accelerator | NVIDIA Jetson의 고정 기능 NPU(NVDLA) |
| IP (silicon IP) | 설계 라이선스 | Ethos-U처럼 칩 회사가 구성을 골라 넣는 설계 |
| unified memory | 공유 DRAM | CPU·GPU·NPU가 한 DRAM을 나눠 씀 |
| MT/s | 초당 전송 수 | DDR은 clock(MHz)의 2배 |
| retention | 유지 | 전원을 낮춘 상태에서 SRAM·레지스터 내용을 보존 |
| µW/MHz, µA/MHz | 주파수당 동적 전력·전류 | 기준 전압·코드·실행 위치가 숫자를 바꾼다 |
| CoreMark/mA | 전류당 CPU 성능 | 전압이 붙어야 에너지로 바꿀 수 있다 |
| duty cycle | 동작 비율 | 평균 전력 = Σ 상태 전력 × 상태 시간 비율 |
| benchmarketing | 벤치마크 마케팅 | 유리한 조건·지표만 골라 보여 주는 관행 |

---

## 16. 요약 & 체크리스트

데이터시트 해석은 "숫자를 믿느냐 마느냐"가 아니라 **숫자에 조건을 다시 붙이는 일**이다. 보도자료·brief의 헤드라인은 이론 peak(MAC × 2 × clock)나 기준 없는 배수인 경우가 많고, 정밀도·sparsity·엔진 합산·clock이 빠져 있다. 이를 dense INT8 한 엔진 기준으로 정규화하고(불확실성은 flag로 남기고), 메모리 경로의 대역폭과 엮어 ridge point를 내면 우리 모델이 어디서 막히는지 보인다. 지연은 점이 아니라 범위로 내고, 벤더 실측표로 검산한다. 전력은 가장 깊은 sleep 헤드라인이 아니라 우리 duty cycle·온도·retention 조건의 평균으로 계산한다 — 웨어러블 상시 음성 시나리오에서는 NPU보다 오디오 프런트엔드가 예산을 지배할 수 있다. 빠진 정보는 FAE 질문으로 바꾸고, 결정은 errata를 반영한 EVB 실측으로 내린다. Don이 SSD·RF에서 하던 corner 사고와 errata 읽기가 그대로 통한다.

- [ ] 보도자료·product brief·datasheet·RM·app note·errata가 각각 무엇을 약속하는지 말할 수 있다
- [ ] datasheet 전류 표의 typ·max·조건 열·각주를 읽고 "이 숫자가 우리 상태에 해당하는지" 판정할 수 있다
- [ ] TOPS·TOPs·GOPS, op/MAC, dense/sparse, INT4/INT8, platform TOPS를 손으로 서로 변환할 수 있다
- [ ] 광고 숫자를 MAC × 2 × clock으로 검산하고, 빠진 값을 역산할 수 있다
- [ ] 실제 칩 숫자를 dense INT8 한 엔진 기준으로 정규화하고 flag로 불확실성을 표시할 수 있다
- [ ] 버스 폭·MT/s로 이론 대역폭을, 정규화 TOPS로 ridge point를 계산하고 모델 AI와 비교할 수 있다
- [ ] 활용률·대역폭 효율·오버헤드 범위로 우리 모델의 지연 범위를 내고, 벤더 실측으로 검산할 수 있다
- [ ] µA 헤드라인을 duty cycle·retention·온도를 반영한 평균 전력과 배터리 수명으로 바꿀 수 있다
- [ ] µA/MHz·µW/MHz·CoreMark/mA를 같은 기준으로 바꿔 비교할 수 있다
- [ ] 빠진 정보를 20개 이상의 FAE 질문으로 만들고, benchmarketing 패턴을 짚을 수 있다

---

## 참고 자료

공개 자료(모두 확인일 2026-10-03; 벤더 페이지는 바뀔 수 있다)

- ST, STM32N6 보도자료: [newsroom.st.com/media-center/press-item.html/p4665.html](https://newsroom.st.com/media-center/press-item.html/p4665.html)
- Arm, Ethos-U85 제품 페이지: [arm.com/products/silicon-ip-cpu/ethos/ethos-u85](https://www.arm.com/products/silicon-ip-cpu/ethos/ethos-u85)
- Alif Semiconductor, Ensemble 제품 페이지: [alifsemi.com/products/ensemble/](https://alifsemi.com/products/ensemble/)
- Ambiq, Apollo510 보도자료: [ambiq.com/news/ambiq-apollo510-delivers-30x-power-efficiency-improvement-to-unleash-edge-ai/](https://ambiq.com/news/ambiq-apollo510-delivers-30x-power-efficiency-improvement-to-unleash-edge-ai/)
- Ambiq, Apollo510 제품 페이지: [ambiq.com/product/apollo510/](https://ambiq.com/product/apollo510/)
- Nordic Semiconductor, nRF54L15 제품 페이지: [nordicsemi.com/Products/nRF54L15](https://www.nordicsemi.com/Products/nRF54L15) (요약 도구 기반, 재확인 권장)
- Hailo, Hailo-8 product brief: [hailo.ai/wp-content/uploads/2023/12/hailo8_product_brief_3.23.pdf](https://hailo.ai/wp-content/uploads/2023/12/hailo8_product_brief_3.23.pdf)
- Hailo, Hailo-8L M.2 product brief: [hailo.ai/wp-content/uploads/2023/10/Hailo-8L-M.2-ET-Product-Brief-Rev2.0.pdf](https://hailo.ai/wp-content/uploads/2023/10/Hailo-8L-M.2-ET-Product-Brief-Rev2.0.pdf)
- Coral, Edge TPU performance benchmarks: [coral.ai/docs/edgetpu/benchmarks/](https://coral.ai/docs/edgetpu/benchmarks/) (요약 도구 기반, 재확인 권장)
- NVIDIA, Jetson Orin 제품 페이지(비교표): [nvidia.com/en-us/autonomous-machines/embedded-systems/jetson-orin/](https://www.nvidia.com/en-us/autonomous-machines/embedded-systems/jetson-orin/)
- NVIDIA, Jetson Orin Nano Super Developer Kit: [nvidia.com/en-us/autonomous-machines/embedded-systems/jetson-orin/nano-super-developer-kit/](https://www.nvidia.com/en-us/autonomous-machines/embedded-systems/jetson-orin/nano-super-developer-kit/)
- Qualcomm, Snapdragon AR1 Gen 1 product brief: [docs.qualcomm.com/doc/87-86507-1/87-86507-1_REV_B_Snapdragon_AR1_Gen_1_Platform_Product_Brief.pdf](https://docs.qualcomm.com/doc/87-86507-1/87-86507-1_REV_B_Snapdragon_AR1_Gen_1_Platform_Product_Brief.pdf)
- UploadVR, Snapdragon AR1+ Gen 1 기사 (2차): [uploadvr.com/qualcomm-snapdragon-ar1-plus-smart-glasses-chip/](https://www.uploadvr.com/qualcomm-snapdragon-ar1-plus-smart-glasses-chip/)
- The Register, Lunar Lake TOPS 분해 기사 (2차): [theregister.com/2024/06/04/intel_details_lunar_lake_tops/](https://www.theregister.com/2024/06/04/intel_details_lunar_lake_tops/)

배경 자료

- MLCommons, MLPerf Inference·Tiny 벤치마크 규칙: [mlcommons.org](https://mlcommons.org)
- Williams, Waterman, Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures", CACM 2009 — D3의 출발점
- Howard et al., "MobileNets: Efficient Convolutional Neural Networks for Mobile Vision Applications", 2017 — MobileNet v1 MAC 수(약 569 M)
- 이 노트 세트: D3(roofline), D5(LLM 추론 성능), D7(에너지 모델, TOPS/W), C4(sparsity), E5(NPU 구조·활용률), E7(메모리 시스템), E9(저전력 모드), I5(벤치마크·지표), M1~M3
