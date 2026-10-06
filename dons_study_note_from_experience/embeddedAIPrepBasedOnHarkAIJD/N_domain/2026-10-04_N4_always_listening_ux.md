# N4. Always-listening UX — 오작동·표시등·지연을 "사용자 비용"으로 설계하기

> **이 노트를 다 읽으면**: false accept와 false reject에 사용자 비용 가중치를 붙여 엔지니어링 최적점과 다른 wake 동작점을 고를 수 있다 · 화자 검증 gate가 TV·타인 오작동을 얼마나 줄이고 주인을 얼마나 거부하는지 숫자로 말할 수 있다 · "LED가 켜지기 전에는 오디오가 기기를 떠나지 않는다"를 하드웨어 배선과 펌웨어 불변식 checker로 보장하는 방법을 설명할 수 있다 · 사람 대화의 turn 간격과 비교해 음성 응답 지연 목표를 정하고 ack 신호로 체감 지연을 줄일 수 있다
> **JD 연결**: 컨슈머 AI 기기 — study_prep_list N4: "always-listening UX — 프라이버시 표시, false wake 비용, 사용자 신뢰" (JD 우대 "Wearables … IoT"). 데이터 쪽 짝은 H6.
> **Don 기준 난이도**: 상태기계·GPIO·전류 감지 ADC·이벤트 trace·양산 테스트·불변식 검사는 이미 강하다 / "사용자가 무엇을 비용으로 느끼는가"를 숫자로 모델링하는 법, 프라이버시 표시등의 설계 원칙, 대화 지연 지각, 화자 검증의 UX 쪽 trade-off는 새로 배운다
> **선행 노트**: A2 §9(false wake의 Bayes 산수), A4 §9(FAR·FRR·DET), B5 §3·§5(KWS, 화자 검증), I3(cascade 임계값 공동 최적화), L4(endpointing·TTFA·barge-in), H6(동의·pre-roll·보관·규제), H7 §6(trigger rate 모니터링), G6 §2.7(카메라 표시등)

> **범위 메모 — 이미 다룬 것은 반복하지 않는다.** FAR/FRR의 정의와 DET 곡선(A4), 하루 판정 횟수에서 판정당 오판 확률을 거꾸로 구하는 산수(A2, A4), 여러 단 cascade의 임계값 공동 최적화와 상관(I3), 동의 ledger·keyword-gated pre-roll·차등 프라이버시·보관 기간·규제(H6), endpointing hangover와 TTFA 측정(L4), fleet의 trigger rate 감시(H7)는 각 노트를 본다. N4는 그 위에서 **"사람이 이것을 어떻게 느끼고, 그 느낌을 펌웨어가 어떻게 보장하는가"** 만 다룬다. 이 노트의 모든 비용 가중치·신뢰 모델 파라미터는 **설명용 가정**이며 실측 사용자 연구가 아니다.

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 정확도 지표는 사용자의 언어가 아니다

엔지니어는 "FRR 5 %, FA 0.5회/day"라고 말한다. 사용자는 이렇게 말한다.

- "회의 중에 갑자기 귀에서 '네, 듣고 있어요'라고 해서 민망했다." — false accept 한 번.
- "세 번 불렀는데 대답을 안 해서 그냥 폰을 꺼냈다." — false reject 연속 두세 번.
- "불이 언제 켜지는지 모르겠어서 중요한 얘기할 땐 그냥 뺀다." — 표시(indicator)를 믿지 못함.
- "대답이 너무 늦어서 내가 다시 말하려는 순간 말을 시작했다." — turn 지연과 겹침.

예를 들어 Hark 같은 웨어러블 AI 기기라면(추정), 이 문장들이 바로 **착용 시간·사용 빈도·반품률**로 이어진다. 착용하지 않으면 모델도, 데이터도, 매출도 없다. 그래서 always-listening UX는 디자이너만의 일이 아니라 **동작점(operating point), 상태기계, 타이밍 보장**을 정하는 엔지니어링 문제다.

### 0.2 다섯 개의 접점

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg"><text x="340" y="20" font-size="14" text-anchor="middle">always-listening 기기의 사용자 접점 다섯 개 — 각 접점마다 펌웨어가 보장할 것이 있다</text><rect x="10" y="40" width="120" height="56" rx="6" fill="none" stroke="#888" stroke-width="2"/><text x="70" y="63" font-size="13" text-anchor="middle">대기</text><text x="70" y="83" font-size="12" text-anchor="middle">항상 듣기 (로컬)</text><line x1="130" y1="68" x2="144" y2="68" stroke="currentColor"/><polygon points="144,68 138,64 138,72" fill="currentColor"/><rect x="10" y="125" width="120" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="none"/><text x="70" y="152" font-size="12" text-anchor="middle">엿듣는 거 아냐?</text><rect x="10" y="198" width="120" height="56" rx="6" fill="none" stroke="currentColor" stroke-dasharray="4 3"/><text x="70" y="221" font-size="12" text-anchor="middle">mute 하드웨어 차단</text><text x="70" y="240" font-size="12" text-anchor="middle">상태 보고 (§4·§9)</text><rect x="144" y="40" width="120" height="56" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="204" y="63" font-size="13" text-anchor="middle">wake 판정</text><text x="204" y="83" font-size="12" text-anchor="middle">FA · FR</text><line x1="264" y1="68" x2="278" y2="68" stroke="currentColor"/><polygon points="278,68 272,64 272,72" fill="currentColor"/><rect x="144" y="125" width="120" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="none"/><text x="204" y="152" font-size="12" text-anchor="middle">갑자기 켜짐 / 무시</text><rect x="144" y="198" width="120" height="56" rx="6" fill="none" stroke="currentColor" stroke-dasharray="4 3"/><text x="204" y="221" font-size="12" text-anchor="middle">UX 비용 임계값</text><text x="204" y="240" font-size="12" text-anchor="middle">화자 검증 (§1–3)</text><rect x="278" y="40" width="120" height="56" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/><text x="338" y="63" font-size="13" text-anchor="middle">표시</text><text x="338" y="83" font-size="12" text-anchor="middle">빛 · 소리 · 진동</text><line x1="398" y1="68" x2="412" y2="68" stroke="currentColor"/><polygon points="412,68 406,64 406,72" fill="currentColor"/><rect x="278" y="125" width="120" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="none"/><text x="338" y="152" font-size="12" text-anchor="middle">지금 듣는 중?</text><rect x="278" y="198" width="120" height="56" rx="6" fill="none" stroke="currentColor" stroke-dasharray="4 3"/><text x="338" y="221" font-size="12" text-anchor="middle">LED 확인 후 전송</text><text x="338" y="240" font-size="12" text-anchor="middle">tamper 감지 (§4)</text><rect x="412" y="40" width="120" height="56" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="472" y="63" font-size="13" text-anchor="middle">응답</text><text x="472" y="83" font-size="12" text-anchor="middle">턴 지연</text><line x1="532" y1="68" x2="546" y2="68" stroke="currentColor"/><polygon points="546,68 540,64 540,72" fill="currentColor"/><rect x="412" y="125" width="120" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="none"/><text x="472" y="152" font-size="12" text-anchor="middle">대화 같나?</text><rect x="412" y="198" width="120" height="56" rx="6" fill="none" stroke="currentColor" stroke-dasharray="4 3"/><text x="472" y="221" font-size="12" text-anchor="middle">ack earcon</text><text x="472" y="240" font-size="12" text-anchor="middle">지연 예산 (§5–6)</text><rect x="546" y="40" width="120" height="56" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="606" y="63" font-size="13" text-anchor="middle">사후</text><text x="606" y="83" font-size="12" text-anchor="middle">복구 · 기록</text><rect x="546" y="125" width="120" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="none"/><text x="606" y="152" font-size="12" text-anchor="middle">되돌릴 수 있나?</text><rect x="546" y="198" width="120" height="56" rx="6" fill="none" stroke="currentColor" stroke-dasharray="4 3"/><text x="606" y="221" font-size="12" text-anchor="middle">취소 · 재시도</text><text x="606" y="240" font-size="12" text-anchor="middle">기록 · 삭제 (§7–8)</text><text x="10" y="118" font-size="12">사용자가 느끼는 질문</text><text x="10" y="191" font-size="12">펌웨어·시스템이 보장할 것</text><text x="340" y="285" font-size="12" text-anchor="middle">데이터 쪽(동의·보관·삭제 파이프라인)은 H6, 지연 측정은 L4, cascade 최적화는 I3에 있다</text></svg>
```

그림 1 — 이 노트의 지도. 위 줄은 사용자가 기기와 만나는 순서, 가운데는 그때 사용자가 품는 질문, 아래는 펌웨어·시스템이 보장해야 할 것과 이 노트의 절 번호다.

### 0.3 Don의 경험과 연결

SSD 펌웨어에서 Don은 이미 이 구조의 일을 해 봤다.

| SSD 펌웨어 | always-listening 기기 |
|---|---|
| "읽기 실패 0"보다 "고객이 느끼는 latency tail"이 더 중요한 경우 | "FRR 평균"보다 "연속 두 번 거부"가 더 중요하다 (§1) |
| power-loss 시 "ack한 쓰기는 반드시 남는다"는 불변식 | "LED 확인 전엔 오디오가 나가지 않는다"는 불변식 (§4) |
| 펌웨어 버그가 있어도 지켜지는 하드웨어 write-protect 핀 | 펌웨어가 무엇을 하든 마이크를 끊는 하드웨어 mute (§4) |
| SMART 로그로 고객이 상태를 검증 | 활성화 기록(history)으로 사용자가 검증 (§8) |

핵심 감각은 같다. **사용자(고객)가 믿을 수 있는 것은 "펌웨어가 잘 짜여 있을 것"이 아니라 "펌웨어가 틀려도 깨지지 않는 구조"다.**

---

## 1. False accept vs false reject — 사용자 비용으로 동작점 고르기

### 1.1 직관 — 두 실수는 같은 무게가 아니다

A4에서 임계값을 올리면 FA는 줄고 FRR은 늘어나는 trade-off를 봤다. 엔지니어링 기본값은 "FA ≤ 1회/day 같은 spec을 맞추고 그 안에서 FRR 최소" 또는 "오류 개수의 합 최소"다. 그런데 사용자는 실수마다 다른 값을 매긴다.

- **False accept(FA, 오작동)**: 부르지 않았는데 깨어난다. 비용은 방해(소리·진동)에 더해 **"내 말을 몰래 들었다"는 프라이버시 비용**이 붙는다. 사적인 대화 중이었다면 훨씬 크다. 회의·수업 중이면 사회적 민망함이 크다.
- **False reject(FR, 무시)**: 불렀는데 대답이 없다. 한 번은 "다시 말하면 되지" 수준이다. 그런데 **두 번 연속**이면 사용자는 포기(abandon)하고 폰을 꺼낸다. 포기 비용은 한 번 무시의 몇 배다.

그래서 UX 비용은 대략 이렇게 생겼다.

```
하루 UX 비용 = c_FA · FA/day  +  c_miss · (호출 × FRR)  +  c_ab · (호출 × FRR²)

c_FA  = c_방해 + P(사적 대화 중) × c_프라이버시
FRR²  = 독립 가정에서 두 번 연속 거부될 확률 (포기 근사)
```

말로 하면: 오작동은 "상황에 따라 커지는 고정 비용", 무시는 "한 번은 싸지만 연속되면 비싸지는 비용"이다. 두 번째 항은 FRR에 선형, 세 번째 항은 제곱이라 FRR이 작을 때는 거의 사라지고 커지면 급격히 커진다.

### 1.2 가정 — 이 절의 숫자

| 항목 | 값 | 근거 |
|---|---|---|
| 하루 negative 창 | 25,920 | I3 공통 사례와 같은 가정 (말소리 30 % × 1창/s) |
| 하루 진짜 호출 | 20 | I3와 같은 가정 |
| 점수 모델 | negative ~ N(0, 1), positive ~ N(6, 1) | d' = 6, 시스템 전체를 한 단으로 근사 |
| c_miss | 1 | 기준 단위 ("짜증 1점") |
| c_ab (연속 거부로 포기) | 4 | 가정 |
| c_FA (집) | 2 + 0.3 × 10 = 5 | 방해 2, 사적 대화 확률 30 %, 프라이버시 10 (가정) |
| c_FA (회의) | 8 + 0.6 × 10 = 14 | 민망함 8, 사적 60 % (가정) |
| c_FA (로컬 전용) | 1 | 오작동해도 차임만 울리고 아무것도 저장·전송하지 않는 설계 |

이 가중치는 **설명용**이다. 실제 제품에서는 사용자 연구(설문, 일기 연구, A/B의 이탈률)로 맞춰야 한다. 중요한 것은 값이 아니라 **구조**다: 비용 함수를 명시하면 "왜 이 임계값인가"를 숫자로 토론할 수 있다.

### 1.3 손으로 계산 — θ = 4.40에서 '집' 비용

```
FA/day   = 25,920 × Q(4.40)  = 25,920 × 5.41×10⁻⁶ ≈ 0.140
FRR      = Φ(4.40 − 6)       = Φ(−1.6)            ≈ 0.0548
miss/day = 20 × 0.0548                              ≈ 1.10
포기/day = 20 × 0.0548²                             ≈ 0.060

UX(집) = 5 × 0.140 + 1 × 1.10 + 4 × 0.060 = 0.70 + 1.10 + 0.24 = 2.04
```

말로 하면: 이 동작점에서 하루 비용의 절반 이상은 "한 번 무시"에서 오고, 오작동은 하루 0.14회지만 회당 5점이라 0.70을 차지한다. 비교로 spec "FA ≤ 1/day"에 딱 맞춘 θ = 3.95를 같은 식에 넣으면 FA ≈ 1.0/day × 5점 = 5.0이 혼자서 비용을 지배해 합이 약 5.5가 된다. **spec을 만족하는 것과 사용자 비용이 작은 것은 다른 이야기다.**

### 1.4 코드로 확인 — 네 가지 기준의 최적 θ

무엇을 확인하는 코드인지: 같은 검출기(d' = 6)에서 "오류 개수 최소", "UX 집/회의/로컬 전용 비용 최소", "spec FA ≤ 1/day"가 각각 어떤 임계값을 고르는지.

```python
import numpy as np
from scipy.stats import norm
NEG, CALLS, DP = 25_920, 20, 6.0        # 하루 negative 창, 하루 호출, d' (모두 가정)
th = np.linspace(3.0, 6.0, 301)
fa = NEG * norm.sf(th)                  # 하루 false accept 수
frr = norm.cdf(th - DP)                 # 호출 1회가 거부될 확률
miss = CALLS * frr
miss2 = CALLS * frr**2                  # 두 번 연속 거부 → 포기(abandon) 횟수 근사
count = fa + miss                       # 엔지니어링 기준: 오류 개수 합
def ux(c_fa, c_miss=1.0, c_ab=4.0):     # UX 비용 (단위: "짜증 점수", 가정)
    return c_fa * fa + c_miss * miss + c_ab * miss2
home = ux(c_fa=2 + 0.3 * 10)            # 방해 2 + 사적 대화 30% × 프라이버시 10
meet = ux(c_fa=8 + 0.6 * 10)            # 회의 중: 민망함 8 + 사적 60% × 10
local = ux(c_fa=1)                      # FA가 차임만 울리고 아무것도 안 보내는 설계
for name, cost in [("count", count), ("UX home", home), ("UX meeting", meet), ("UX local", local)]:
    i = np.argmin(cost)
    print(f"{name:10s} θ={th[i]:.2f}  FA/day={fa[i]:6.2f}  FRR={frr[i]*100:5.2f}%  "
          f"miss/day={miss[i]:.2f}  cost={cost[i]:.2f}")
i1 = np.argmin(np.abs(fa - 1.0))
print(f"spec FA≤1/day  θ={th[i1]:.2f}  FRR={frr[i1]*100:.2f}%  UX home cost={home[i1]:.2f}")
```

```text
count      θ=4.19  FA/day=  0.36  FRR= 3.51%  miss/day=0.70  cost=1.06
UX home    θ=4.40  FA/day=  0.14  FRR= 5.48%  miss/day=1.10  cost=2.04
UX meeting θ=4.56  FA/day=  0.07  FRR= 7.49%  miss/day=1.50  cost=2.88
UX local   θ=4.16  FA/day=  0.41  FRR= 3.29%  miss/day=0.66  cost=1.16
spec FA≤1/day  θ=3.95  FRR=2.02%  UX home cost=5.50
```

출력에서 볼 것: 오류 개수만 세면 θ = 4.19(FA 0.36/day)가 최적이다. 오작동에 프라이버시 비용을 붙이면 θ가 4.40, 회의 맥락이면 4.56으로 올라가 **FRR을 일부러 5.5~7.5 %로 키운다**. 반대로 오작동해도 아무것도 나가지 않는 "로컬 전용" 설계면 θ가 4.16으로 내려간다. spec 동작점(θ = 3.95)은 FRR이 가장 낮지만 UX 비용은 2.7배다.

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg"><line x1="70" y1="290" x2="640" y2="290" stroke="currentColor"/><line x1="70" y1="40" x2="70" y2="290" stroke="currentColor"/><line x1="70.0" y1="290" x2="70.0" y2="294" stroke="currentColor"/><text x="70.0" y="308" font-size="12" text-anchor="middle">3.6</text><line x1="151.4" y1="290" x2="151.4" y2="294" stroke="currentColor"/><text x="151.4" y="308" font-size="12" text-anchor="middle">3.8</text><line x1="232.9" y1="290" x2="232.9" y2="294" stroke="currentColor"/><text x="232.9" y="308" font-size="12" text-anchor="middle">4.0</text><line x1="314.3" y1="290" x2="314.3" y2="294" stroke="currentColor"/><text x="314.3" y="308" font-size="12" text-anchor="middle">4.2</text><line x1="395.7" y1="290" x2="395.7" y2="294" stroke="currentColor"/><text x="395.7" y="308" font-size="12" text-anchor="middle">4.4</text><line x1="477.1" y1="290" x2="477.1" y2="294" stroke="currentColor"/><text x="477.1" y="308" font-size="12" text-anchor="middle">4.6</text><line x1="558.6" y1="290" x2="558.6" y2="294" stroke="currentColor"/><text x="558.6" y="308" font-size="12" text-anchor="middle">4.8</text><line x1="640.0" y1="290" x2="640.0" y2="294" stroke="currentColor"/><text x="640.0" y="308" font-size="12" text-anchor="middle">5.0</text><line x1="66" y1="290.0" x2="70" y2="290.0" stroke="currentColor"/><text x="62" y="294.0" font-size="12" text-anchor="end">0</text><line x1="66" y1="206.7" x2="70" y2="206.7" stroke="currentColor"/><text x="62" y="210.7" font-size="12" text-anchor="end">2</text><line x1="66" y1="123.3" x2="70" y2="123.3" stroke="currentColor"/><text x="62" y="127.3" font-size="12" text-anchor="end">4</text><line x1="66" y1="40.0" x2="70" y2="40.0" stroke="currentColor"/><text x="62" y="44.0" font-size="12" text-anchor="end">6</text><text x="355.0" y="328" font-size="13" text-anchor="middle">wake 임계값 θ (negative 점수의 σ 단위)</text><text x="18" y="165.0" font-size="13" text-anchor="middle" transform="rotate(-90 18 165.0)">하루 비용 (가정 단위)</text><polyline points="70.0,111.3 74.1,117.6 78.1,123.7 82.2,129.5 86.3,135.2 90.4,140.6 94.4,145.8 98.5,150.8 102.6,155.6 106.6,160.2 110.7,164.6 114.8,168.9 118.9,173.0 122.9,176.9 127.0,180.7 131.1,184.3 135.1,187.8 139.2,191.1 143.3,194.3 147.4,197.4 151.4,200.3 155.5,203.1 159.6,205.7 163.6,208.3 167.7,210.7 171.8,213.1 175.9,215.3 179.9,217.4 184.0,219.4 188.1,221.3 192.1,223.2 196.2,224.9 200.3,226.5 204.4,228.1 208.4,229.6 212.5,231.0 216.6,232.3 220.6,233.5 224.7,234.7 228.8,235.8 232.9,236.8 236.9,237.8 241.0,238.7 245.1,239.5 249.1,240.3 253.2,241.0 257.3,241.7 261.4,242.3 265.4,242.8 269.5,243.3 273.6,243.8 277.6,244.2 281.7,244.5 285.8,244.8 289.9,245.0 293.9,245.2 298.0,245.4 302.1,245.5 306.1,245.6 310.2,245.6 314.3,245.6 318.4,245.6 322.4,245.5 326.5,245.4 330.6,245.3 334.6,245.1 338.7,244.9 342.8,244.6 346.9,244.3 350.9,244.0 355.0,243.6 359.1,243.3 363.1,242.8 367.2,242.4 371.3,241.9 375.4,241.4 379.4,240.9 383.5,240.3 387.6,239.7 391.6,239.1 395.7,238.5 399.8,237.8 403.9,237.1 407.9,236.4 412.0,235.7 416.1,234.9 420.1,234.1 424.2,233.3 428.3,232.4 432.4,231.6 436.4,230.7 440.5,229.7 444.6,228.8 448.6,227.8 452.7,226.8 456.8,225.8 460.9,224.8 464.9,223.7 469.0,222.7 473.1,221.5 477.1,220.4 481.2,219.3 485.3,218.1 489.4,216.9 493.4,215.7 497.5,214.5 501.6,213.2 505.6,211.9 509.7,210.6 513.8,209.3 517.9,207.9 521.9,206.6 526.0,205.2 530.1,203.8 534.1,202.3 538.2,200.9 542.3,199.4 546.4,197.9 550.4,196.4 554.5,194.8 558.6,193.3 562.6,191.7 566.7,190.1 570.8,188.4 574.9,186.8 578.9,185.1 583.0,183.4 587.1,181.7 591.1,180.0 595.2,178.2 599.3,176.4 603.4,174.6 607.4,172.8 611.5,171.0 615.6,169.1 619.6,167.2 623.7,165.3 627.8,163.4 631.9,161.4 635.9,159.5 640.0,157.5" fill="none" stroke="#888" stroke-width="2"/><circle cx="310.2" cy="245.6" r="5" fill="#888"/><text x="310.2" y="235.6" font-size="12" text-anchor="middle">θ=4.19</text><line x1="430" y1="60" x2="455" y2="60" stroke="#888" stroke-width="2"/><text x="462" y="64" font-size="12">오류 개수 (FA+miss)</text><polyline points="70.0,40.0 74.1,40.0 78.1,40.0 82.2,40.0 86.3,40.0 90.4,40.0 94.4,40.0 98.5,40.0 102.6,40.0 106.6,40.0 110.7,40.0 114.8,40.0 118.9,40.0 122.9,40.0 127.0,40.0 131.1,40.0 135.1,40.0 139.2,40.0 143.3,40.0 147.4,40.0 151.4,40.0 155.5,40.0 159.6,40.0 163.6,40.0 167.7,40.0 171.8,40.0 175.9,40.0 179.9,40.0 184.0,40.0 188.1,40.0 192.1,40.0 196.2,40.0 200.3,40.0 204.4,43.4 208.4,52.3 212.5,60.8 216.6,69.0 220.6,76.8 224.7,84.3 228.8,91.4 232.9,98.3 236.9,104.8 241.0,111.1 245.1,117.1 249.1,122.8 253.2,128.2 257.3,133.4 261.4,138.3 265.4,143.0 269.5,147.5 273.6,151.8 277.6,155.8 281.7,159.6 285.8,163.3 289.9,166.7 293.9,170.0 298.0,173.1 302.1,176.0 306.1,178.7 310.2,181.3 314.3,183.7 318.4,185.9 322.4,188.1 326.5,190.0 330.6,191.9 334.6,193.6 338.7,195.1 342.8,196.5 346.9,197.9 350.9,199.1 355.0,200.1 359.1,201.1 363.1,201.9 367.2,202.7 371.3,203.3 375.4,203.9 379.4,204.3 383.5,204.6 387.6,204.9 391.6,205.0 395.7,205.1 399.8,205.1 403.9,205.0 407.9,204.8 412.0,204.5 416.1,204.1 420.1,203.7 424.2,203.1 428.3,202.5 432.4,201.9 436.4,201.1 440.5,200.3 444.6,199.4 448.6,198.4 452.7,197.3 456.8,196.2 460.9,195.0 464.9,193.8 469.0,192.4 473.1,191.0 477.1,189.6 481.2,188.0 485.3,186.4 489.4,184.7 493.4,183.0 497.5,181.2 501.6,179.3 505.6,177.3 509.7,175.3 513.8,173.2 517.9,171.1 521.9,168.8 526.0,166.6 530.1,164.2 534.1,161.8 538.2,159.3 542.3,156.7 546.4,154.0 550.4,151.3 554.5,148.5 558.6,145.7 562.6,142.8 566.7,139.8 570.8,136.7 574.9,133.5 578.9,130.3 583.0,127.0 587.1,123.6 591.1,120.2 595.2,116.6 599.3,113.0 603.4,109.3 607.4,105.5 611.5,101.7 615.6,97.7 619.6,93.7 623.7,89.6 627.8,85.4 631.9,81.1 635.9,76.8 640.0,72.3" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="395.7" cy="205.1" r="5" fill="#4a7bd0"/><text x="395.7" y="195.1" font-size="12" text-anchor="middle">θ=4.40</text><line x1="430" y1="80" x2="455" y2="80" stroke="#4a7bd0" stroke-width="2"/><text x="462" y="84" font-size="12">UX 집</text><polyline points="70.0,40.0 74.1,40.0 78.1,40.0 82.2,40.0 86.3,40.0 90.4,40.0 94.4,40.0 98.5,40.0 102.6,40.0 106.6,40.0 110.7,40.0 114.8,40.0 118.9,40.0 122.9,40.0 127.0,40.0 131.1,40.0 135.1,40.0 139.2,40.0 143.3,40.0 147.4,40.0 151.4,40.0 155.5,40.0 159.6,40.0 163.6,40.0 167.7,40.0 171.8,40.0 175.9,40.0 179.9,40.0 184.0,40.0 188.1,40.0 192.1,40.0 196.2,40.0 200.3,40.0 204.4,40.0 208.4,40.0 212.5,40.0 216.6,40.0 220.6,40.0 224.7,40.0 228.8,40.0 232.9,40.0 236.9,40.0 241.0,40.0 245.1,40.0 249.1,40.0 253.2,40.0 257.3,40.0 261.4,40.0 265.4,40.0 269.5,40.0 273.6,40.0 277.6,40.0 281.7,40.0 285.8,40.0 289.9,40.0 293.9,40.0 298.0,40.0 302.1,40.0 306.1,40.0 310.2,45.7 314.3,54.0 318.4,61.8 322.4,69.3 326.5,76.5 330.6,83.2 334.6,89.7 338.7,95.8 342.8,101.5 346.9,107.0 350.9,112.2 355.0,117.1 359.1,121.7 363.1,126.1 367.2,130.2 371.3,134.1 375.4,137.7 379.4,141.1 383.5,144.3 387.6,147.2 391.6,149.9 395.7,152.5 399.8,154.8 403.9,157.0 407.9,159.0 412.0,160.8 416.1,162.4 420.1,163.8 424.2,165.1 428.3,166.3 432.4,167.2 436.4,168.1 440.5,168.8 444.6,169.3 448.6,169.7 452.7,170.0 456.8,170.1 460.9,170.2 464.9,170.1 469.0,169.8 473.1,169.5 477.1,169.0 481.2,168.4 485.3,167.8 489.4,167.0 493.4,166.1 497.5,165.0 501.6,163.9 505.6,162.7 509.7,161.4 513.8,160.0 517.9,158.4 521.9,156.8 526.0,155.1 530.1,153.3 534.1,151.4 538.2,149.4 542.3,147.3 546.4,145.1 550.4,142.8 554.5,140.4 558.6,138.0 562.6,135.4 566.7,132.8 570.8,130.0 574.9,127.2 578.9,124.3 583.0,121.3 587.1,118.2 591.1,115.0 595.2,111.7 599.3,108.3 603.4,104.9 607.4,101.3 611.5,97.7 615.6,93.9 619.6,90.1 623.7,86.2 627.8,82.2 631.9,78.1 635.9,73.9 640.0,69.5" fill="none" stroke="#d0564a" stroke-width="2"/><circle cx="460.9" cy="170.2" r="5" fill="#d0564a"/><text x="460.9" y="160.2" font-size="12" text-anchor="middle">θ=4.56</text><line x1="430" y1="100" x2="455" y2="100" stroke="#d0564a" stroke-width="2"/><text x="462" y="104" font-size="12">UX 회의</text><line x1="212.5" y1="40" x2="212.5" y2="290" stroke="#e08a3c" stroke-dasharray="4 3"/><text x="216.5" y="50" font-size="12">spec FA≤1/day</text></svg>
```

그림 2 — 임계값에 따른 하루 비용 (가정 가중치). 회색은 오류 개수, 파랑은 '집', 빨강은 '회의' 비용이다. 점은 각 곡선의 최솟값, 주황 점선은 FA ≤ 1/day spec 동작점이다. 빨강 곡선은 왼쪽에서 가파르게 올라간다 — 오작동 한 번의 값이 크기 때문이다.

### 1.5 이 결과에서 나오는 설계 원칙 세 가지

1. **오작동의 비용을 줄이는 설계가 임계값을 내릴 여유를 만든다.** "로컬 전용"처럼 FA가 일어나도 오디오가 기기를 떠나지 않으면 c_FA가 작아지고, 같은 모델로 더 낮은 FRR을 살 수 있다. 즉 프라이버시 구조(H6의 keyword-gated pre-roll, §4의 LED 불변식)는 정확도 지표에도 돈을 벌어 준다.
2. **맥락별 동작점**: 기기가 "회의 중"(캘린더, 주변 다화자 감지)이나 "혼자 걷는 중"(IMU)을 안다면 θ를 바꿀 수 있다. 단, 사용자가 예측할 수 없게 바뀌면 그것 자체가 혼란 비용이다. 바뀌는 범위를 작게, 방향은 보수적으로.
3. **연속 거부를 따로 줄인다**: 비용의 제곱 항은 "두 번째 시도"를 살리면 크게 준다. §7의 second-chance 재시도가 이것이다.

### 1.6 함정

- 비용 가중치를 엔지니어가 혼자 정하면 "엔지니어의 취향"이 된다. 가중치는 PM·UX 리서치와 같이 정하고, 그 값을 문서에 박아 버전 관리한다.
- FRR²은 **독립 가정**이다. 실제로는 같은 사람이 같은 환경(시끄러운 지하철)에서 다시 부르므로 두 번째 실패 확률이 첫 번째보다 높다. 실측 FRR(2연속 | 첫 실패)을 따로 재야 한다 (I3 §2의 상관 문제와 같은 구조).
- "하루 평균 FA"는 사용자 분포를 숨긴다. TV를 하루 종일 켜 두는 집은 평균의 10배를 겪는다. 비용은 **사용자별 분포의 꼬리**로 봐야 한다 (H7 §6).

---

## 2. 신뢰는 누적된다 — 몇 주에 걸친 사용자 모델

### 2.1 직관 — 신뢰는 적분기, 오작동은 충격

1절은 하루 비용이었다. 하지만 사용자는 매일 리셋되지 않는다. 신뢰(trust)는 성공 경험으로 천천히 쌓이고, 오작동 한 번에 뚝 떨어진다. 제어공학으로 말하면 **누설이 있는 적분기(leaky integrator) 위에 비대칭 충격**이 들어오는 시스템이다. 그리고 신뢰가 일정 수준 아래로 내려간 사용자는 기기를 서랍에 넣는다(이탈, churn).

### 2.2 모델 (toy — 모든 파라미터는 가정)

```
성공 1회:   T ← T + α · (1 − T)        α = 0.01   (천천히 쌓인다)
FA 1회:     T ← T · (1 − β_FA)         β_FA = 0.06 (한 번에 6 % 깎인다)
거부 1회:   T ← T · (1 − β_FR)         β_FR = 0.02
하루 이탈 확률 = H · ((1 − T) / 0.5)²    H = 0.03
```

말로 하면: 신뢰 T는 0~1. 성공은 "남은 여유"의 1 %씩 채우고, 오작동은 "가진 신뢰"의 6 %를 깎는다. 신뢰가 0.5까지 떨어지면 하루 3 % 확률로 떠난다.

### 2.3 손으로 계산 — 평형 신뢰

하루 8번 호출, FRR 5 %라면 하루 성공 7.6회, 거부 0.4회다. 하루 단위로 근사하면 평형점 `T*`에서 증가 = 감소다.

```
증가 ≈ 7.6 × 0.01 × (1 − T) = 0.076 (1 − T)
감소 ≈ T × (FA/day × 0.06 + 0.4 × 0.02) = T × (0.06·FA + 0.008)

FA = 0.1/day:  T* = 0.076 / (0.076 + 0.014) ≈ 0.84
FA = 1.0/day:  T* = 0.076 / (0.076 + 0.068) ≈ 0.53
```

말로 하면: 오작동이 하루 0.1회에서 1회로 늘면 평형 신뢰가 0.84에서 0.53으로 내려가고, 이탈 확률은 `((1−T)/0.5)²`에 비례하므로 약 0.10배에서 0.88배로 9배 가까이 커진다. 오작동 빈도와 이탈은 선형이 아니다.

### 2.4 코드로 확인 — 2,000명 × 12주

무엇을 확인하는 코드인지: Poisson으로 FA를 뽑아 사용자 2,000명의 신뢰와 이탈을 12주 동안 시뮬레이션하고, 주별 잔존율을 본다.

```python
import numpy as np
rng = np.random.default_rng(7)
U, DAYS, USES, FRR = 2000, 84, 8, 0.05          # 사용자, 12주, 하루 호출, FRR (가정)
A_OK, B_FA, B_FR = 0.01, 0.06, 0.02             # 성공 +α(1−T), FA −β·T, 거부 −β·T (가정)
H = 0.03                                         # 하루 이탈 확률 = H·((1−T)/0.5)² (가정)
def simulate(fa_per_day):
    T = np.full(U, 0.7); alive = np.ones(U, bool); weekly = []
    for d in range(DAYS):
        ok = rng.binomial(USES, 1 - FRR, U); fr = USES - ok
        fa = rng.poisson(fa_per_day, U)
        T = T + A_OK * ok * (1 - T)              # 성공은 천천히 쌓인다(근사)
        T = T * (1 - B_FA) ** fa * (1 - B_FR) ** fr
        T = np.clip(T, 0, 1)
        quit_ = rng.random(U) < H * ((1 - T) / 0.5) ** 2
        alive &= ~quit_
        if d % 7 == 6: weekly.append(alive.mean())
    return np.array(weekly), T[alive].mean()
for r in (0.1, 0.5, 1.0, 3.0):
    w, t = simulate(r)
    print(f"FA {r:3.1f}/day  wk1 {w[0]*100:5.1f}%  wk4 {w[3]*100:5.1f}%  "
          f"wk12 {w[11]*100:5.1f}%  남은 사용자 평균 T={t:.2f}")
```

```text
FA 0.1/day  wk1  94.5%  wk4  85.7%  wk12  71.9%  남은 사용자 평균 T=0.84
FA 0.5/day  wk1  92.2%  wk4  69.7%  wk12  30.9%  남은 사용자 평균 T=0.67
FA 1.0/day  wk1  88.1%  wk4  50.3%  wk12  10.3%  남은 사용자 평균 T=0.53
FA 3.0/day  wk1  75.9%  wk4  19.5%  wk12   0.4%  남은 사용자 평균 T=0.26
```

출력에서 볼 것: 12주 뒤 남은 사용자의 평균 T가 2.3절 손계산 평형값(0.84, 0.53)과 맞는다. FA 0.1/day와 1/day는 1주차에는 6 %p 차이밖에 안 나지만 12주차에는 72 % 대 10 %가 된다. **짧은 베타 테스트로는 오작동의 진짜 비용이 안 보인다**는 뜻이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><line x1="70" y1="270" x2="600" y2="270" stroke="currentColor"/><line x1="70" y1="30" x2="70" y2="270" stroke="currentColor"/><line x1="70.0" y1="270" x2="70.0" y2="274" stroke="currentColor"/><text x="70.0" y="288" font-size="12" text-anchor="middle">0</text><line x1="158.3" y1="270" x2="158.3" y2="274" stroke="currentColor"/><text x="158.3" y="288" font-size="12" text-anchor="middle">2</text><line x1="246.7" y1="270" x2="246.7" y2="274" stroke="currentColor"/><text x="246.7" y="288" font-size="12" text-anchor="middle">4</text><line x1="335.0" y1="270" x2="335.0" y2="274" stroke="currentColor"/><text x="335.0" y="288" font-size="12" text-anchor="middle">6</text><line x1="423.3" y1="270" x2="423.3" y2="274" stroke="currentColor"/><text x="423.3" y="288" font-size="12" text-anchor="middle">8</text><line x1="511.7" y1="270" x2="511.7" y2="274" stroke="currentColor"/><text x="511.7" y="288" font-size="12" text-anchor="middle">10</text><line x1="600.0" y1="270" x2="600.0" y2="274" stroke="currentColor"/><text x="600.0" y="288" font-size="12" text-anchor="middle">12</text><line x1="66" y1="270.0" x2="70" y2="270.0" stroke="currentColor"/><text x="62" y="274.0" font-size="12" text-anchor="end">0%</text><line x1="66" y1="210.0" x2="70" y2="210.0" stroke="currentColor"/><text x="62" y="214.0" font-size="12" text-anchor="end">25%</text><line x1="66" y1="150.0" x2="70" y2="150.0" stroke="currentColor"/><text x="62" y="154.0" font-size="12" text-anchor="end">50%</text><line x1="66" y1="90.0" x2="70" y2="90.0" stroke="currentColor"/><text x="62" y="94.0" font-size="12" text-anchor="end">75%</text><line x1="66" y1="30.0" x2="70" y2="30.0" stroke="currentColor"/><text x="62" y="34.0" font-size="12" text-anchor="end">100%</text><text x="335.0" y="310" font-size="13" text-anchor="middle">주 (week)</text><text x="18" y="150" font-size="13" text-anchor="middle" transform="rotate(-90 18 150)">계속 쓰는 사용자 비율</text><polyline points="70.0,30.0 114.2,43.2 158.3,52.8 202.5,58.6 246.7,64.4 290.8,68.8 335.0,74.4 379.2,77.2 423.3,81.6 467.5,85.3 511.7,90.0 555.8,94.1 600.0,97.4" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="606.0" y="101.4" font-size="12">FA 0.1/day</text><polyline points="70.0,30.0 114.2,48.8 158.3,68.3 202.5,88.1 246.7,102.8 290.8,118.1 335.0,130.9 379.2,143.8 423.3,155.9 467.5,166.0 511.7,177.2 555.8,186.1 600.0,195.7" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="606.0" y="199.7" font-size="12">FA 0.5/day</text><polyline points="70.0,30.0 114.2,58.4 158.3,91.9 202.5,124.8 246.7,149.2 290.8,171.7 335.0,189.4 379.2,204.4 423.3,215.3 467.5,223.9 511.7,232.3 555.8,239.4 600.0,245.3" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="606.0" y="249.3" font-size="12">FA 1.0/day</text><polyline points="70.0,30.0 114.2,87.8 158.3,154.9 202.5,196.2 246.7,223.2 290.8,240.2 335.0,253.1 379.2,259.8 423.3,264.0 467.5,266.4 511.7,268.0 555.8,268.8 600.0,268.9" fill="none" stroke="#d0564a" stroke-width="2"/><text x="606.0" y="272.9" font-size="12">FA 3.0/day</text></svg>
```

그림 3 — 하루 FA 빈도별 잔존 곡선 (toy model, 가정 파라미터). 곡선의 절대값은 의미가 없다. 의미 있는 것은 FA 빈도에 따라 곡선이 벌어지는 **모양**과, 첫 1~2주에는 차이가 작다는 점이다.

### 2.5 이 모델로 할 수 있는 일과 없는 일

| 할 수 있는 일 | 할 수 없는 일 |
|---|---|
| "FA를 0.5 → 0.1로 줄이는 것"과 "FRR을 5 → 3 %로 줄이는 것" 중 어느 쪽이 잔존에 더 효과적인지 **방향** 비교 | 실제 잔존율 예측 (파라미터가 가정이다) |
| 베타 기간이 짧으면 차이가 안 보인다는 **통계적 경고** | 사용자 유형(헤비 유저, TV 가정)별 차이 — 모델을 늘려야 한다 |
| 필드 telemetry(H7)로 β·H를 맞출 **틀** 제공 | 인과 추론 — 이탈 원인은 오작동 말고도 많다 |

함정: 이 모델을 실측처럼 발표하지 않는다. 면접에서는 "toy model로 방향을 보고, 필드에서 trigger rate와 사용 빈도·mute 시간의 상관으로 검증한다"고 말한다. 또 신뢰가 떨어진 사용자는 떠나기 전에 먼저 **mute를 켜 두는 시간이 늘어난다** — mute 시간 비율은 이탈의 선행 지표로 쓸 만하다 (§9.1에서 mute 상태를 telemetry에 넣는 이유).

---

## 3. 화자 검증 gate — "주인 목소리에만 깨어나기"

### 3.1 직관 — 오작동의 출처를 나눠 본다

하루 오작동 2회가 어디서 오는지 쪼개 보면(가정):

| 출처 | FA/day | SV(speaker verification)로 걸러지나 |
|---|---|---|
| 다른 사람의 말 (동료, 가족) | 0.8 | 걸러진다 — 목소리가 다르다 |
| TV·라디오·영상 | 1.0 | 대부분 걸러진다 — 다른 화자 + 채널 왜곡 |
| 주인 자신의 비-wake 발화 ("헤이, 하크닉…") | 0.2 | **못 거른다** — 주인 목소리가 맞다 |

B5 §5에서 본 SV(embedding + cosine + threshold)를 KWS **뒤에** gate로 붙이면, 위 두 줄을 크게 줄일 수 있다. 웨어러블은 여기에 유리한 점이 하나 더 있다: 착용자의 목소리는 몸을 타고 오는 진동(골전도)으로도 잡힌다. 일부 무선 이어폰은 사양에 speech-detecting accelerometer(말소리 감지 가속도계)를 명시한다. 이런 센서로 "지금 말한 사람이 착용자인가"를 싸게 판정하는 것도 같은 역할의 gate다 (구현은 제품마다 다르다).

### 3.2 UX 쪽 trade-off — B5와 무엇이 다른가

B5 §5는 EER과 threshold를 봤다. UX에서 새로 생기는 질문은 이것이다.

- **주인이 거부되는 순간이 가장 나쁘다.** 주인 입장에서 SV 거부는 그냥 FR이다. 그런데 이유를 모르니 더 답답하다. 특히 감기·속삭임·시끄러운 곳처럼 등록할 때와 조건이 다르면 주인 점수가 내려간다.
- **바닥(floor)이 있다.** 주인 자신의 오발화는 SV로 못 거른다. SV에 기대하는 최대 효과를 미리 계산해 둔다.
- **가족 공유 기기**라면 등록자가 여럿이 되고, 비등록자(손님)가 쓰려면 "게스트 모드"가 필요하다. 웨어러블은 개인 기기라 이 문제가 작다(가정).

### 3.3 코드로 확인 — 주인·타인·TV의 synthetic embedding

무엇을 확인하는 코드인지: 32차원 synthetic embedding으로 주인(조용/시끄러움), 타인 200명, TV 화자 50명(채널 왜곡으로 흔들림 큼)을 만들고, SV threshold별로 남는 FA/day와 주인 FRR을 계산한다. 실제 embedding 모델이 아니라 **Gaussian cluster 가정**이다.

```python
import numpy as np
rng = np.random.default_rng(3)
D = 32
def unit(x): return x / np.linalg.norm(x, axis=-1, keepdims=True)
owner = rng.normal(0, 1, D)
def utts(center, n, sigma): return unit(center + rng.normal(0, sigma, (n, D)))
enroll = unit(utts(owner, 5, 1.0).mean(0))               # 조용한 곳에서 5번 등록
s_own_q = utts(owner, 4000, 1.0) @ enroll                 # 주인, 조용
s_own_n = utts(owner, 4000, 1.6) @ enroll                 # 주인, 시끄러움(가정: 흔들림 ↑)
others = rng.normal(0, 1, (200, D))                       # 다른 사람 200명
s_oth = np.concatenate([utts(c, 20, 1.0) @ enroll for c in others])
tv = rng.normal(0, 1, (50, D))                            # TV 화자 50명, 채널 왜곡으로 흔들림 ↑
s_tv = np.concatenate([utts(c, 80, 1.3) @ enroll for c in tv])
FA_SRC = {"others": 0.8, "tv": 1.0, "owner_nonwake": 0.2}  # SV 전 하루 FA 구성 (가정)
print("thr  ownerFRR(quiet) ownerFRR(noisy)  pass(oth)  pass(tv)  FA/day  감소")
for thr in (0.0, 0.2, 0.3, 0.4, 0.5):
    po, pt = (s_oth >= thr).mean(), (s_tv >= thr).mean()
    fa = FA_SRC["others"] * po + FA_SRC["tv"] * pt + FA_SRC["owner_nonwake"] * (s_own_q >= thr).mean()
    print(f"{thr:.1f}  {(s_own_q < thr).mean()*100:6.2f}%        {(s_own_n < thr).mean()*100:6.2f}%"
          f"       {po*100:5.1f}%    {pt*100:5.1f}%   {fa:.3f}  {(1 - fa/2.0)*100:4.1f}%")
```

```text
thr  ownerFRR(quiet) ownerFRR(noisy)  pass(oth)  pass(tv)  FA/day  감소
0.0    0.00%          0.03%        48.5%     47.3%   1.061  46.9%
0.2    0.00%          0.78%        12.4%     10.7%   0.406  79.7%
0.3    0.00%          3.98%         4.1%      3.0%   0.263  86.9%
0.4    0.18%         15.00%         1.1%      0.8%   0.216  89.2%
0.5    1.95%         40.38%         0.1%      0.1%   0.198  90.1%
```

출력에서 볼 것: threshold 0.3에서 FA는 2.0 → 0.263/day로 87 % 줄고, 조용한 곳의 주인은 한 번도 거부되지 않는다. 그런데 **시끄러운 곳의 주인 FRR은 4 %**, threshold 0.4면 15 %다. threshold를 올려도 FA는 0.2 근처 바닥에서 더 안 내려간다(주인 본인의 오발화). 즉 0.3 이상으로 올리는 것은 FA는 거의 못 줄이면서 시끄러운 곳의 주인만 괴롭힌다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><line x1="70" y1="270" x2="620" y2="270" stroke="currentColor"/><line x1="70" y1="30" x2="70" y2="270" stroke="currentColor"/><line x1="70.0" y1="270" x2="70.0" y2="274" stroke="currentColor"/><text x="70.0" y="288" font-size="12" text-anchor="middle">0%</text><line x1="180.0" y1="270" x2="180.0" y2="274" stroke="currentColor"/><text x="180.0" y="288" font-size="12" text-anchor="middle">10%</text><line x1="290.0" y1="270" x2="290.0" y2="274" stroke="currentColor"/><text x="290.0" y="288" font-size="12" text-anchor="middle">20%</text><line x1="400.0" y1="270" x2="400.0" y2="274" stroke="currentColor"/><text x="400.0" y="288" font-size="12" text-anchor="middle">30%</text><line x1="510.0" y1="270" x2="510.0" y2="274" stroke="currentColor"/><text x="510.0" y="288" font-size="12" text-anchor="middle">40%</text><line x1="620.0" y1="270" x2="620.0" y2="274" stroke="currentColor"/><text x="620.0" y="288" font-size="12" text-anchor="middle">50%</text><line x1="66" y1="270.0" x2="70" y2="270.0" stroke="currentColor"/><text x="62" y="274.0" font-size="12" text-anchor="end">0</text><line x1="66" y1="210.0" x2="70" y2="210.0" stroke="currentColor"/><text x="62" y="214.0" font-size="12" text-anchor="end">0.5</text><line x1="66" y1="150.0" x2="70" y2="150.0" stroke="currentColor"/><text x="62" y="154.0" font-size="12" text-anchor="end">1.0</text><line x1="66" y1="90.0" x2="70" y2="90.0" stroke="currentColor"/><text x="62" y="94.0" font-size="12" text-anchor="end">1.5</text><line x1="66" y1="30.0" x2="70" y2="30.0" stroke="currentColor"/><text x="62" y="34.0" font-size="12" text-anchor="end">2.0</text><text x="345.0" y="310" font-size="13" text-anchor="middle">주인 FRR (SV가 주인을 거부하는 비율)</text><text x="18" y="150" font-size="13" text-anchor="middle" transform="rotate(-90 18 150)">남는 false wake / day</text><polyline points="70.0,60.2 70.0,66.5 70.0,72.5 70.0,79.5 70.0,86.8 70.0,95.2 70.0,104.2 70.0,113.3 70.0,123.1 70.0,132.6 70.0,142.6 70.0,153.3 70.0,162.4 70.0,171.9 70.0,180.5 70.0,189.0 70.0,196.6 70.0,204.5 70.0,210.9 70.0,216.4 70.0,221.3 70.0,225.5 70.0,229.4 70.0,233.1 70.0,236.1 70.0,238.5 70.3,240.0 70.5,241.3 70.8,242.4 71.4,243.2 71.9,244.1 71.9,244.5 75.0,245.2 77.4,245.5 82.4,245.8 91.5,246.2 102.2,246.6 118.7,247.0 144.2,247.6 179.2,248.4 226.2,249.4" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="70.0" cy="221.3" r="4" fill="#4a7bd0"/><circle cx="70.0" cy="238.5" r="4" fill="#4a7bd0"/><circle cx="71.9" cy="244.1" r="4" fill="#4a7bd0"/><polyline points="70.0,60.2 70.0,66.5 70.0,72.5 70.0,79.5 70.0,86.8 70.0,95.2 70.0,104.2 70.0,113.3 70.3,123.1 70.3,132.6 70.3,142.6 70.3,153.3 70.3,162.4 70.5,171.9 70.5,180.5 70.8,189.0 71.1,196.6 71.9,204.5 73.8,210.9 76.0,216.4 78.5,221.3 81.3,225.5 86.2,229.4 93.7,233.1 102.2,236.1 113.7,238.5 129.1,240.0 149.2,241.3 173.7,242.4 202.0,243.2 235.0,244.1 279.8,244.5 327.9,245.2 383.2,245.5 443.2,245.8 514.1,246.2 581.8,246.6 620.0,247.0 620.0,247.6 620.0,248.4 620.0,249.4" fill="none" stroke="#d0564a" stroke-width="2"/><circle cx="78.5" cy="221.3" r="4" fill="#d0564a"/><circle cx="113.7" cy="238.5" r="4" fill="#d0564a"/><circle cx="235.0" cy="244.1" r="4" fill="#d0564a"/><line x1="70" y1="246.0" x2="620" y2="246.0" stroke="#888" stroke-dasharray="4 3"/><text x="620" y="240.0" font-size="12" text-anchor="end">바닥 0.2 = 주인 본인의 비-wake 발화 (SV로 못 거름)</text><line x1="70" y1="30.0" x2="620" y2="30.0" stroke="#888" stroke-dasharray="2 4"/><text x="620" y="46.0" font-size="12" text-anchor="end">SV 없음 2.0 / day</text><line x1="420" y1="90" x2="445" y2="90" stroke="#4a7bd0" stroke-width="2"/><text x="452" y="94" font-size="12">주인 조용 (σ=1.0)</text><line x1="420" y1="110" x2="445" y2="110" stroke="#d0564a" stroke-width="2"/><text x="452" y="114" font-size="12">주인 시끄러움 (σ=1.6)</text><text x="452" y="134" font-size="12">점: thr 0.2 / 0.3 / 0.4</text></svg>
```

그림 4 — SV threshold를 움직일 때의 trade-off. x축은 주인 FRR, y축은 남는 FA/day. 파랑(조용)은 거의 공짜로 FA를 줄이지만, 빨강(시끄러움)은 같은 threshold에서 FRR이 크게 늘어난다. 회색 점선 0.2는 SV로 줄일 수 없는 바닥이다.

### 3.4 설계 패턴

1. **조건부 threshold**: 잡음 추정치(SNR)가 낮으면 SV threshold를 낮추거나 SV를 건너뛴다. 시끄러운 곳의 주인을 살리는 대신, 그 시간대의 FA는 KWS 임계값이 막는다.
2. **hard reject 대신 soft gate**: SV가 애매하면 "깨어나되 개인 데이터(메시지, 일정)는 열지 않는" 제한 모드로 응답한다. 보안이 필요한 기능(결제, 메시지 읽기)만 높은 threshold를 쓴다.
3. **등록 품질**: 등록을 조용한 방 하나에서만 하면 위 실험처럼 시끄러운 곳에서 무너진다. 등록 발화를 여러 환경에서 받거나, 사용 중 높은 확신으로 통과한 발화로 등록 벡터를 천천히 갱신한다(대신 오염 위험 — 다른 사람 발화가 섞이면 gate가 열린다). 단 이 효과는 조건 차이가 embedding을 **체계적으로** 옮길 때 생긴다. 무작위 잡음만 있는 §3.3 같은 toy 모델에서는 다환경 등록이 오히려 손해다(§12 문제 4).
4. **embedding은 생체 정보**다. 등록 벡터는 기기 secure storage에 두고 기기 밖으로 보내지 않는 것을 기본으로 한다 (데이터 분류·동의는 H6).

---

## 4. 프라이버시 표시와 mute — 하드웨어에 묶는다

### 4.1 사용자가 알고 싶은 상태는 네 개다

| 상태 | 사용자의 질문 | 흔한 표시 |
|---|---|---|
| 로컬 대기 (wake 감지만) | "평소에 엿듣나?" | 표시 없음 또는 아주 약한 상태 표시 |
| 활성 (wake 후 듣는 중) | "지금 내 말이 처리되나?" | LED 켜짐, 시작 earcon |
| 전송·저장 중 | "지금 이게 기기 밖으로 나가나?" | LED 켜짐 유지 (가장 중요한 불변식) |
| mute (마이크 차단) | "정말 꺼졌나?" | 다른 색 LED (흔히 빨강), 물리 스위치 위치 |

여기서 핵심은 사용자가 **표시를 믿을 수 있어야** 한다는 것이다. 표시가 펌웨어의 `led_on()` 호출 하나에 달려 있으면, 버그 하나·악성 업데이트 하나로 "불 꺼진 채 녹음"이 가능하다. 그러면 표시는 장식이 된다.

### 4.2 실제 제품들은 이렇게 한다 (확인일 2026-10-04)

- **Apple 노트북·iPad**: Apple Platform Security 문서는 Apple silicon Mac 노트북과 T2 칩 Intel Mac 노트북에서 덮개를 닫으면 마이크가 **하드웨어로** 끊긴다고 설명한다. 덮개 센서(Hall 센서·힌지 각도) 신호가 **재프로그래밍할 수 없는 단순한 하드웨어 로직**(debounce, hysteresis, 최대 500 ms 지연)을 거쳐 마이크를 끊고, root·커널 권한의 소프트웨어나 기기 펌웨어도 마이크 데이터를 받을 수 없게 하는 것이 목적이라고 한다. 2020년 이후 iPad도 MFi 케이스를 닫으면 같은 방식이다.
- **iPhone/iPad의 화면 표시**: iOS 14부터 마이크 사용 중이면 주황 점, 카메라면 초록 점이 상태 막대에 뜬다. 이것은 OS 수준 표시다.
- **스마트 스피커**: Amazon 도움말은 Echo의 microphone off 버튼을 누르면 마이크가 차단되고 빨간 불이 켜진다고 안내한다. 어떤 회로로 차단하는지는 세대·모델마다 다를 수 있어 이 노트에서는 단정하지 않는다.
- **카메라 안경**: Ray-Ban Meta 안경은 촬영 중 전면 LED가 켜지고, LED가 가려지면 촬영을 막는다고 Meta가 안내해 왔다. 2026년 7월에는 LED가 물리적으로 훼손되면 카메라를 비활성화하는 업데이트가 배포된다고 보도됐다(9to5Google 등). 즉 "표시를 끌 수 없게"를 넘어 "표시가 고장 나면 기능도 멈춘다"로 간 사례다. 카메라 쪽 원칙은 G6 §2.7.

공통 원칙: **표시는 기능의 부작용(side effect)이어야 하고, 기능과 독립된 명령이면 안 된다.**

### 4.3 하드웨어 배선 패턴

```
              mute 스위치 (물리, 2극)
                 ┌──────────┐
 VDD_MIC ───────┤ 극 A     ├──────── 마이크 전원 (끊으면 마이크 무전원)
                 │          │
 PDM_CLK (SoC) ─┤ 극 B     ├──────── 마이크 클럭 (끊으면 데이터 없음)
                 └──────────┘
                      │ 상태 읽기 전용 (풀업 + GPIO 입력, 펌웨어는 읽기만)
                      ▼
                  MCU GPIO  ──► telemetry / 앱 / mute LED 표시

 활성 LED:  mic 경로 enable 신호 ──AND── 전송 경로 enable ──► LED 드라이버
            LED 전류 감지 저항 ──► ADC ──► "LED 실제 켜짐" 확인 (§4.4 LED_CONFIRMED)
```

- **mute는 스위치가 직접 전원·클럭을 끊는다.** 펌웨어는 스위치 위치를 **읽기만** 한다. 펌웨어가 mute를 "해제"할 경로가 회로에 없다. 이것이 tamper-resistance의 첫 단계다.
- **두 극(전원과 클럭)을 같이 끊는** 이유: 한쪽만 끊으면 누설 전류나 다른 클럭 소스로 신호가 살아날 수 있다는 의심을 받는다. 여러 마이크(빔포밍 어레이)라면 모든 마이크가 같은 스위치 뒤에 있어야 한다. 하나라도 빠지면 mute는 거짓말이다.
- **활성 LED는 하드웨어 enable과 묶는 것이 가장 강하다.** 다만 웨어러블에서는 "wake 감지용으로는 항상 켜진 마이크"와 "활성 세션"이 같은 마이크를 쓰므로, 마이크 전원에 LED를 묶을 수 없다(그러면 LED가 항상 켜진다). 그래서 **전송·저장 경로(무선 TX, flash write)의 enable**에 묶거나, 펌웨어 불변식 + 전류 감지 확인으로 보장한다. 아래 §4.4가 그 펌웨어 쪽이다.

### 4.4 불변식 — "LED 확인 전에는 아무것도 나가지 않는다"

정확히 쓰면:

```
EGRESS(무선 전송) 또는 STORE(flash 저장) 이벤트가 시각 t에 일어나면
  → t 이전에 LED_CONFIRMED가 있었고
  → 그 사이에 LED_OFF가 없었다
```

말로 하면: 오디오가 기기 밖으로 나가거나 남는 모든 순간은 "LED가 실제로 켜져 있음을 하드웨어로 확인한 구간" 안에 있어야 한다. **RAM 링버퍼에 잠깐 들어 있는 pre-roll은 예외**다 — 그것은 기기 안에 머물고 덮어써진다(H6 §5.3). 불변식은 "캡처"가 아니라 **"egress와 저장"** 에 걸린다.

왜 `LED_ON_CMD`가 아니라 `LED_CONFIRMED`인가: 명령을 보냈다고 LED가 켜진 것이 아니다. LED가 단선됐거나, 누가 뜯어냈거나, 드라이버가 고장 났으면 명령은 성공해도 빛은 없다. 전류 감지 ADC로 "LED에 전류가 흐른다"를 읽은 뒤에만 확인으로 친다. Don에게 익숙한 말로 하면 **write 후 read-back verify**다.

### 4.5 코드로 확인 — 세션 시퀀서와 독립 trace checker (C)

무엇을 확인하는 코드인지: 펌웨어 세션 코드가 이벤트 trace를 남기고, **별도의 checker**가 trace만 보고 불변식을 검사한다. 정상 빌드, LED 단선 시나리오, 그리고 `-DBUG_EARLY_STREAM`으로 "지연을 줄이려고 LED 확인 전에 pre-roll을 먼저 보내는" 버그를 주입한 빌드를 비교한다.

```c
/* 프라이버시 LED 불변식: 오디오가 기기를 떠나거나(EGRESS) 저장(STORE)되기 전에 LED가 켜져 있고 확인돼야 한다 */
#include <stdio.h>
#include <stdbool.h>
enum ev { WAKE, LED_ON_CMD, LED_CONFIRMED, EGRESS, STORE, END_SESSION, LED_OFF };
static const char *nm[] = {"WAKE","LED_ON_CMD","LED_CONFIRMED","EGRESS","STORE","END_SESSION","LED_OFF"};
typedef struct { int t_ms; enum ev e; } rec_t;
static rec_t trace[32]; static int n;
static void log_ev(int t, enum ev e) { trace[n++] = (rec_t){t, e}; }
static bool led_hw_ok = true;                 /* 전류 감지로 LED가 실제로 켜졌는지 (시뮬) */
static void session(int t0, int chunks) {     /* 펌웨어: wake 하나에 대한 세션 */
    log_ev(t0, WAKE);
#ifdef BUG_EARLY_STREAM
    log_ev(t0 + 1, EGRESS);                   /* 버그: 지연 줄이려고 LED 확인 전에 pre-roll 전송 */
#endif
    log_ev(t0 + 1, LED_ON_CMD);
    if (!led_hw_ok) { log_ev(t0 + 6, END_SESSION); return; }   /* LED 확인 실패 → 세션 거부 */
    log_ev(t0 + 5, LED_CONFIRMED);            /* 전류 감지 ADC 결과까지 ~4 ms (가정) */
    for (int i = 0; i < chunks; i++) log_ev(t0 + 10 + 20 * i, i == 0 ? STORE : EGRESS);
    log_ev(t0 + 10 + 20 * chunks, END_SESSION);
    log_ev(t0 + 12 + 20 * chunks, LED_OFF);   /* 마지막 전송이 끝난 뒤에만 끈다 */
}
static int check(void) {                      /* 독립 checker: trace만 보고 불변식 검사 */
    bool lit = false; int bad = 0;
    for (int i = 0; i < n; i++) {
        enum ev e = trace[i].e;
        if (e == LED_CONFIRMED) lit = true;
        if (e == LED_OFF) lit = false;
        if ((e == EGRESS || e == STORE) && !lit) {
            printf("  VIOLATION t=%d ms: %s while LED not confirmed\n", trace[i].t_ms, nm[e]); bad++;
        }
    }
    return bad;
}
int main(void) {
    session(0, 3);
    led_hw_ok = false; session(1000, 3);       /* LED 단선(tamper) 시나리오 */
    for (int i = 0; i < n; i++) printf("%5d %s\n", trace[i].t_ms, nm[trace[i].e]);
    int bad = check();
    printf("violations = %d -> %s\n", bad, bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 led.c -o led && ./led; echo "exit=$?"
```

```text
    0 WAKE
    1 LED_ON_CMD
    5 LED_CONFIRMED
   10 STORE
   30 EGRESS
   50 EGRESS
   70 END_SESSION
   72 LED_OFF
 1000 WAKE
 1001 LED_ON_CMD
 1006 END_SESSION
violations = 0 -> PASS
exit=0
```

출력에서 볼 것: 정상 빌드는 STORE·EGRESS가 모두 t=5(LED 확인) 이후 t=72(LED 끔) 이전에 있다. 두 번째 세션은 LED가 단선돼 확인이 안 오자 **아무것도 보내지 않고** 세션을 끝냈다. 경고 0개로 컴파일된다.

이제 버그를 주입한다.

```sh
cc -std=c11 -Wall -Wextra -O2 -DBUG_EARLY_STREAM led.c -o led_bug && ./led_bug; echo "exit=$?"
```

```text
    0 WAKE
    1 EGRESS
    1 LED_ON_CMD
    5 LED_CONFIRMED
   10 STORE
   30 EGRESS
   50 EGRESS
   70 END_SESSION
   72 LED_OFF
 1000 WAKE
 1001 EGRESS
 1001 LED_ON_CMD
 1006 END_SESSION
  VIOLATION t=1 ms: EGRESS while LED not confirmed
  VIOLATION t=1001 ms: EGRESS while LED not confirmed
violations = 2 -> FAIL
exit=1
```

출력에서 볼 것: checker가 두 위반을 잡는다. 두 번째 위반이 더 무섭다 — **LED가 고장 난 기기에서도 오디오가 새어 나갔다.** "4 ms 아끼자"는 최적화가 프라이버시 보장을 통째로 깨뜨린다. exit code 1이므로 CI에서 이 checker를 돌리면 빌드가 실패한다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg"><text x="142" y="44" font-size="12" text-anchor="end">마이크 → RAM 링버퍼</text><line x1="150" y1="40" x2="660" y2="40" stroke="#888" stroke-width="0.5"/><text x="142" y="80" font-size="12" text-anchor="end">wake 판정</text><line x1="150" y1="76" x2="660" y2="76" stroke="#888" stroke-width="0.5"/><text x="142" y="116" font-size="12" text-anchor="end">LED 명령</text><line x1="150" y1="112" x2="660" y2="112" stroke="#888" stroke-width="0.5"/><text x="142" y="152" font-size="12" text-anchor="end">LED 확인 (전류 감지)</text><line x1="150" y1="148" x2="660" y2="148" stroke="#888" stroke-width="0.5"/><text x="142" y="188" font-size="12" text-anchor="end">STORE / EGRESS</text><line x1="150" y1="184" x2="660" y2="184" stroke="#888" stroke-width="0.5"/><text x="142" y="224" font-size="12" text-anchor="end">LED 상태</text><line x1="150" y1="220" x2="660" y2="220" stroke="#888" stroke-width="0.5"/><rect x="150.0" y="32" width="510.0" height="16" fill="#888" fill-opacity="0.3"/><text x="235.0" y="44" font-size="11" text-anchor="middle">pre-roll (기기 안에만)</text><circle cx="320.0" cy="76" r="5" fill="#4a7bd0"/><text x="328.0" y="80" font-size="12">WAKE t=0</text><circle cx="324.2" cy="112" r="5" fill="#e08a3c"/><text x="332.2" y="116" font-size="12">t=1</text><circle cx="341.2" cy="148" r="5" fill="#3f9a6b"/><text x="349.2" y="152" font-size="12">t=5 확인</text><rect x="356.5" y="176" width="12" height="16" fill="#4a7bd0" fill-opacity="0.6"/><text x="362.5" y="210" font-size="11" text-anchor="middle">S</text><rect x="441.5" y="176" width="12" height="16" fill="#4a7bd0" fill-opacity="0.6"/><text x="447.5" y="210" font-size="11" text-anchor="middle">E</text><rect x="526.5" y="176" width="12" height="16" fill="#4a7bd0" fill-opacity="0.6"/><text x="532.5" y="210" font-size="11" text-anchor="middle">E</text><line x1="318.2" y1="178" x2="330.2" y2="190" stroke="#d0564a" stroke-width="2.5"/><line x1="318.2" y1="190" x2="330.2" y2="178" stroke="#d0564a" stroke-width="2.5"/><text x="314.2" y="188" font-size="11" text-anchor="end">버그 주입: t=1 EGRESS</text><rect x="341.2" y="212" width="284.8" height="16" fill="#3f9a6b" fill-opacity="0.45"/><text x="483.6" y="224" font-size="12" text-anchor="middle">전송·저장이 허용되는 유일한 창</text><line x1="617.5" y1="168" x2="617.5" y2="236" stroke="currentColor" stroke-dasharray="3 3"/><text x="617.5" y="250" font-size="11" text-anchor="middle">END 70</text><text x="630.0" y="210" font-size="11">OFF 72</text><line x1="150" y1="262" x2="660" y2="262" stroke="currentColor"/><line x1="150.0" y1="262" x2="150.0" y2="266" stroke="currentColor"/><text x="150.0" y="280" font-size="12" text-anchor="middle">-40</text><line x1="235.0" y1="262" x2="235.0" y2="266" stroke="currentColor"/><text x="235.0" y="280" font-size="12" text-anchor="middle">-20</text><line x1="320.0" y1="262" x2="320.0" y2="266" stroke="currentColor"/><text x="320.0" y="280" font-size="12" text-anchor="middle">0</text><line x1="405.0" y1="262" x2="405.0" y2="266" stroke="currentColor"/><text x="405.0" y="280" font-size="12" text-anchor="middle">20</text><line x1="490.0" y1="262" x2="490.0" y2="266" stroke="currentColor"/><text x="490.0" y="280" font-size="12" text-anchor="middle">40</text><line x1="575.0" y1="262" x2="575.0" y2="266" stroke="currentColor"/><text x="575.0" y="280" font-size="12" text-anchor="middle">60</text><line x1="660.0" y1="262" x2="660.0" y2="266" stroke="currentColor"/><text x="660.0" y="280" font-size="12" text-anchor="middle">80</text><text x="660" y="296" font-size="12" text-anchor="end">시간 (ms) · S = STORE, E = EGRESS</text></svg>
```

그림 5 — 정상 세션의 타이밍. 회색 띠는 항상 도는 RAM 링버퍼(pre-roll, 기기 밖으로 안 나감), 초록 구간이 전송·저장이 허용되는 유일한 창이다. 빨간 X는 버그 주입 빌드의 t=1 EGRESS — 창 밖이다.

### 4.6 이 checker를 어디에 붙이나

| 단계 | 방법 |
|---|---|
| 단위 테스트 / CI | 위처럼 시뮬레이션 trace를 checker에 통과시킨다. 버그 주입 빌드가 반드시 실패하는지도 테스트한다 (checker 자체의 테스트) |
| HIL(hardware-in-the-loop) | 실제 보드에서 LED 전류와 무선 TX enable 핀을 logic analyzer로 같이 찍고, 같은 checker를 캡처 파일에 돌린다 |
| 양산 테스트 | LED 전류 감지 경로가 살아 있는지(개방·단락) factory test 항목에 넣는다 |
| 필드 | 세션마다 `wake → LED 확인` 지연을 활성화 기록(§8)에 남기고, 0이거나 비정상이면 플래그 |

함정: checker를 펌웨어와 같은 코드로 짜면 같은 버그를 공유한다. **판정 로직은 독립적으로**, 가능하면 다른 사람이 짠다. SSD의 "펌웨어 로그를 펌웨어가 검증하지 않는다"와 같은 원칙이다.

---

## 5. 피드백 신호 — 빛, 소리, 진동

### 5.1 왜 따로 설계하나

표시(§4)는 "상태가 무엇인가", 피드백은 "내 행동이 받아들여졌나"를 알려 준다. 웨어러블은 화면이 없거나 작아서 피드백이 거의 전부 소리(earcon), 진동(haptic), 작은 LED다.

- **earcon**: 의미를 가진 짧은 소리. "들었어요"(상승음), "취소"(하강음), "실패"(낮은 두 음) 같은 식. 말(TTS)보다 짧고 언어에 독립적이다.
- **haptic**: 진동 패턴. 귀나 손목 기기에서 남에게 안 들리게 알릴 수 있다. 회의 중에 좋다.

### 5.2 상태 → 신호 표 (예시 설계)

| 사건 | 소리 | 진동 | 빛 | 목표 지연 (가정) |
|---|---|---|---|---|
| wake 수락 | 짧은 상승음 | 짧게 1회 | 활성 LED 켜짐 | wake 판정 후 100 ms 이내 |
| 말 끝 감지 (처리 중) | 없음 또는 아주 짧은 틱 | 없음 | LED 맥동 | endpoint 직후 |
| 응답 시작 | (응답 자체) | 없음 | 유지 | §6 |
| 사용자 취소 | 하강음 | 짧게 2회 | 즉시 꺼짐 | 100 ms 이내 |
| 실패 (네트워크 등) | 낮은 두 음 | 길게 1회 | 짧게 깜빡 | 실패 판정 즉시 |
| mute 켜짐 / 꺼짐 | 짧은 확인음 | 없음 | mute LED | 스위치 후 즉시 |

"100 ms 이내" 숫자의 근거: Nielsen이 정리한 응답 시간 기준에서 약 0.1 s는 "즉시 반응한다"고 느끼는 한계, 약 1 s는 생각의 흐름이 끊기지 않는 한계, 약 10 s는 주의를 유지하는 한계다(Nielsen, Usability Engineering 1993; NN/g 글). 화면 UI용 경험칙이지만, "버튼을 눌렀는데 반응이 있나"라는 점에서 wake 피드백에도 출발점으로 쓸 수 있다.

### 5.3 맥락에 맞추기

- **조용 모드**: 회의·수업 맥락에서는 소리를 끄고 진동만. 단, **프라이버시 표시(LED)는 조용 모드에서도 끄지 않는다** — 표시는 착용자만이 아니라 주변 사람(bystander)을 위한 것이기도 하다(§8.2).
- **오작동 후의 피드백은 짧아야 한다.** FA일 때 긴 TTS("무엇을 도와드릴까요?")는 비용을 키운다. 짧은 earcon 하나로 시작하고, 말이 이어지지 않으면 조용히 끝낸다. 1절의 c_방해를 직접 줄이는 방법이다.
- **누가 들으라는 신호인가**: 귀 안의 earcon은 착용자만 듣는다. 주변 사람에게 "지금 녹음 중"을 알리려면 바깥을 향한 LED가 필요하다.

---

## 6. 지연 지각 — 대화처럼 느껴지는 시간

### 6.1 사람 대화의 기준선

Stivers 외(2009, PNAS)는 10개 언어의 질문-대답 쌍을 분석해, 모든 언어에서 대답 시작 시각의 분포가 **질문 끝 후 약 200 ms 안쪽에 봉우리**를 가지며, 전체 평균은 **약 +208 ms**라고 보고했다. 언어별 평균은 일본어 약 +7 ms에서 덴마크어 약 +469 ms까지 퍼져 있다. 한편 단어 하나를 말로 계획해 내는 데만 수백 ms(대략 600 ms 이상으로 추정되는 연구들이 있다)가 걸리므로, 사람은 상대가 말하는 **도중에** 대답을 준비하고 끝을 예측한다는 해석이 일반적이다(Levinson & Torreira 2015).

엔지니어가 가져갈 결론:

- 사람 대화의 **간격은 약 200 ms 규모**다. 침묵을 300 ms 이상 기다린 뒤에야 "끝났다"고 판단하는 endpointer(L4 §2)는 구조적으로 사람보다 느리다.
- 사람은 **예측**으로 이 속도를 낸다. 기계도 같은 속도를 원하면 침묵 대기 대신 "말이 끝날 것 같다"를 예측하는 endpoint 모델이 필요하다.
- 그 대신 **빠른 ack 신호**(§5)로 "들었다"를 먼저 알리면, 실제 답이 늦어도 대화가 끊긴 느낌이 줄어든다.

이 노트에서는 세 기준선을 **라벨로** 쓴다: 300 ms(사람 대화 간격 수준), 700 ms(눈에 띄지만 흐름은 유지된다고 볼 만한 중간값 — 이 노트의 가정), 1000 ms(Nielsen의 흐름 유지 한계, L4/I1의 TTFA p90 목표). 700 ms는 문헌 수치가 아니라 설명용 구분선이다.

### 6.2 코드로 확인 — 단계 지연을 합쳐 기준선 아래 비율 보기

무엇을 확인하는 코드인지: L4/I1의 단계 예산(가정)을 lognormal로 흔들어 20,000턴을 만들고, 구성별로 "말 끝 → 사용자가 처음 듣는 응답 소리"가 300/700/1000 ms 아래인 비율을 센다. 마지막 줄은 응답이 아니라 endpoint 직후의 ack earcon이다.

```python
import numpy as np
rng = np.random.default_rng(11)
N = 20_000
def ln(median, spread):                       # lognormal: 중앙값과 퍼짐(σ) — 단계 시간 가정
    return median * np.exp(rng.normal(0, spread, N))
asr, ttft, phrase, tts, buf = ln(120, .3), ln(120, .4), ln(200, .3), ln(120, .3), 40
net = ln(150, .6)                             # cloud 왕복 (가정, 꼬리가 길다)
cfg = {
  "A hangover 600, cloud": 600 + ln(20, .5) + asr + net + ttft + phrase + tts + buf,
  "B hangover 300, cloud": 300 + ln(20, .5) + asr + net + ttft + phrase + tts + buf,
  "C hangover 300, local": 300 + ln(20, .5) + asr + ttft + phrase + tts + buf,
  "D 예측 endpoint, local": ln(150, .3) + asr + ttft + phrase + tts + buf,
}
ack = 300 + ln(20, .5) + 30                   # E: endpoint 직후 earcon (첫 '반응' 신호)
print(f"{'config':24s} p50   p90   <300  <700  <1000 ms")
for k, v in list(cfg.items()) + [("E earcon ack (첫 신호)", ack)]:
    p50, p90 = np.percentile(v, [50, 90])
    print(f"{k:24s} {p50:4.0f}  {p90:4.0f}  " + "  ".join(f"{(v < t).mean()*100:3.0f}%" for t in (300, 700, 1000)))
```

```text
config                   p50   p90   <300  <700  <1000 ms
A hangover 600, cloud    1412  1631    0%    0%    0%
B hangover 300, cloud    1111  1331    0%    0%   18%
C hangover 300, local     944  1083    0%    0%   71%
D 예측 endpoint, local      777   932    0%   22%   96%
E earcon ack (첫 신호)       350   368    0%  100%  100%
```

손으로 맞춰 보기: 구성 C의 중앙값 합은 `300 + 20 + 120 + 120 + 200 + 120 + 40 = 920 ms`인데 시뮬레이션 p50은 944 ms다. lognormal의 합은 중앙값의 합보다 약간 크다(오른쪽 꼬리). 구성 A는 `600 + 20 + 120 + 150 + 120 + 200 + 120 + 40 = 1370` 대 1412 ms.

출력에서 볼 것: 침묵 기반 endpoint(A~C)로는 **어떤 턴도 700 ms 아래로 못 들어간다.** hangover 300 ms만으로 사람 대화 간격을 이미 넘기 때문이다. 600 → 300으로 줄이면 p50이 300 ms 줄고, cloud를 빼면(C) 1000 ms 안쪽이 71 %가 된다. 예측 endpoint(D, 중앙값 150 ms 가정)를 써야 비로소 700 ms 안쪽이 생긴다. 반면 ack earcon(E)은 **모든 턴에서 700 ms 안쪽**(p90 368 ms)에 첫 신호를 준다 — 응답 자체를 빠르게 만드는 것보다 훨씬 싸다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg"><line x1="70" y1="260" x2="640" y2="260" stroke="currentColor"/><line x1="70" y1="30" x2="70" y2="260" stroke="currentColor"/><line x1="70.0" y1="260" x2="70.0" y2="264" stroke="currentColor"/><text x="70.0" y="278" font-size="12" text-anchor="middle">0</text><line x1="141.2" y1="260" x2="141.2" y2="264" stroke="currentColor"/><text x="141.2" y="278" font-size="12" text-anchor="middle">250</text><line x1="212.5" y1="260" x2="212.5" y2="264" stroke="currentColor"/><text x="212.5" y="278" font-size="12" text-anchor="middle">500</text><line x1="283.8" y1="260" x2="283.8" y2="264" stroke="currentColor"/><text x="283.8" y="278" font-size="12" text-anchor="middle">750</text><line x1="355.0" y1="260" x2="355.0" y2="264" stroke="currentColor"/><text x="355.0" y="278" font-size="12" text-anchor="middle">1000</text><line x1="426.2" y1="260" x2="426.2" y2="264" stroke="currentColor"/><text x="426.2" y="278" font-size="12" text-anchor="middle">1250</text><line x1="497.5" y1="260" x2="497.5" y2="264" stroke="currentColor"/><text x="497.5" y="278" font-size="12" text-anchor="middle">1500</text><line x1="568.8" y1="260" x2="568.8" y2="264" stroke="currentColor"/><text x="568.8" y="278" font-size="12" text-anchor="middle">1750</text><line x1="640.0" y1="260" x2="640.0" y2="264" stroke="currentColor"/><text x="640.0" y="278" font-size="12" text-anchor="middle">2000</text><text x="62" y="264.0" font-size="12" text-anchor="end">0%</text><text x="62" y="149.0" font-size="12" text-anchor="end">50%</text><text x="62" y="34.0" font-size="12" text-anchor="end">100%</text><line x1="155.5" y1="30" x2="155.5" y2="260" stroke="#888" stroke-dasharray="3 3"/><line x1="269.5" y1="30" x2="269.5" y2="260" stroke="#888" stroke-dasharray="3 3"/><line x1="355.0" y1="30" x2="355.0" y2="260" stroke="#888" stroke-dasharray="3 3"/><text x="355.0" y="300" font-size="13" text-anchor="middle">말 끝 → 사용자가 듣는 신호까지 (ms) · 점선 300 / 700 / 1000</text><text x="18" y="145" font-size="13" text-anchor="middle" transform="rotate(-90 18 145)">누적 비율 (CDF)</text><polyline points="395.1,258.9 408.8,255.0 415.6,251.1 420.3,247.3 423.8,243.4 427.1,239.6 430.0,235.7 432.6,231.8 434.9,228.0 437.1,224.1 439.2,220.3 441.3,216.4 443.3,212.5 445.1,208.7 447.0,204.8 448.7,201.0 450.5,197.1 452.1,193.2 453.8,189.4 455.5,185.5 457.2,181.7 458.8,177.8 460.3,173.9 461.9,170.1 463.6,166.2 465.0,162.4 466.7,158.5 468.3,154.6 470.0,150.8 471.7,146.9 473.3,143.1 474.8,139.2 476.4,135.4 478.0,131.5 479.8,127.6 481.6,123.8 483.5,119.9 485.3,116.1 487.5,112.2 489.4,108.3 491.4,104.5 493.7,100.6 495.8,96.8 498.1,92.9 500.7,89.0 503.4,85.2 506.2,81.3 509.5,77.5 512.7,73.6 515.9,69.7 519.4,65.9 523.2,62.0 527.5,58.2 532.7,54.3 539.0,50.4 546.2,46.6 554.7,42.7 566.9,38.9 585.6,35.0 640.0,31.2" fill="none" stroke="#d0564a" stroke-width="2"/><text x="478.5" y="139.0" font-size="12">A</text><polyline points="309.5,258.9 323.8,255.0 329.9,251.1 334.7,247.3 338.4,243.4 341.5,239.6 344.5,235.7 347.0,231.8 349.5,228.0 351.9,224.1 353.9,220.3 355.9,216.4 357.7,212.5 359.6,208.7 361.5,204.8 363.3,201.0 364.9,197.1 366.7,193.2 368.4,189.4 370.0,185.5 371.6,181.7 373.1,177.8 374.7,173.9 376.1,170.1 377.8,166.2 379.3,162.4 381.0,158.5 382.6,154.6 384.2,150.8 385.9,146.9 387.6,143.1 389.4,139.2 391.1,135.4 392.8,131.5 394.4,127.6 396.1,123.8 398.0,119.9 399.9,116.1 401.9,112.2 403.9,108.3 406.1,104.5 408.2,100.6 410.4,96.8 412.9,92.9 415.3,89.0 417.7,85.2 420.7,81.3 423.7,77.5 427.2,73.6 430.2,69.7 434.1,65.9 437.7,62.0 442.2,58.2 447.1,54.3 453.4,50.4 460.9,46.6 469.2,42.7 481.9,38.9 501.2,35.0 556.1,31.2" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="392.7" y="139.0" font-size="12">B</text><polyline points="281.2,258.9 291.6,255.0 296.2,251.1 300.1,247.3 302.8,243.4 305.4,239.6 307.6,235.7 309.6,231.8 311.5,228.0 313.1,224.1 314.7,220.3 316.3,216.4 317.7,212.5 319.1,208.7 320.4,204.8 321.8,201.0 323.2,197.1 324.4,193.2 325.6,189.4 326.7,185.5 327.9,181.7 329.0,177.8 330.2,173.9 331.4,170.1 332.6,166.2 333.7,162.4 334.8,158.5 336.0,154.6 337.2,150.8 338.4,146.9 339.5,143.1 340.8,139.2 342.0,135.4 343.2,131.5 344.3,127.6 345.6,123.8 346.8,119.9 348.0,116.1 349.3,112.2 350.5,108.3 352.1,104.5 353.5,100.6 355.1,96.8 356.6,92.9 358.2,89.0 359.9,85.2 361.5,81.3 363.3,77.5 365.3,73.6 367.4,69.7 369.7,65.9 372.1,62.0 374.7,58.2 377.5,54.3 381.2,50.4 385.1,46.6 390.1,42.7 396.4,38.9 406.4,35.0 430.3,31.2" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="345.0" y="139.0" font-size="12">C</text><polyline points="225.8,258.9 238.2,255.0 243.8,251.1 248.3,247.3 251.7,243.4 254.4,239.6 256.6,235.7 258.9,231.8 261.0,228.0 262.9,224.1 264.5,220.3 266.4,216.4 268.0,212.5 269.5,208.7 270.9,204.8 272.4,201.0 273.8,197.1 275.3,193.2 276.6,189.4 278.0,185.5 279.4,181.7 280.7,177.8 282.1,173.9 283.4,170.1 284.6,166.2 285.8,162.4 287.0,158.5 288.3,154.6 289.7,150.8 290.8,146.9 292.3,143.1 293.7,139.2 295.0,135.4 296.2,131.5 297.5,127.6 298.8,123.8 300.2,119.9 301.7,116.1 303.1,112.2 304.6,108.3 306.2,104.5 307.9,100.6 309.5,96.8 311.2,92.9 313.0,89.0 314.8,85.2 316.7,81.3 318.7,77.5 320.8,73.6 323.0,69.7 325.5,65.9 328.3,62.0 331.4,58.2 334.6,54.3 338.1,50.4 342.4,46.6 347.6,42.7 354.8,38.9 366.0,35.0 390.4,31.2" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="297.5" y="139.0" font-size="12">D</text><polyline points="165.6,258.9 166.1,255.0 166.4,251.1 166.6,247.3 166.8,243.4 166.9,239.6 167.1,235.7 167.2,231.8 167.3,228.0 167.5,224.1 167.6,220.3 167.7,216.4 167.8,212.5 167.9,208.7 168.0,204.8 168.1,201.0 168.2,197.1 168.3,193.2 168.4,189.4 168.5,185.5 168.7,181.7 168.8,177.8 168.9,173.9 169.0,170.1 169.1,166.2 169.2,162.4 169.3,158.5 169.4,154.6 169.6,150.8 169.7,146.9 169.8,143.1 169.9,139.2 170.0,135.4 170.2,131.5 170.3,127.6 170.4,123.8 170.5,119.9 170.7,116.1 170.9,112.2 171.0,108.3 171.2,104.5 171.3,100.6 171.5,96.8 171.7,92.9 171.9,89.0 172.1,85.2 172.4,81.3 172.6,77.5 172.9,73.6 173.2,69.7 173.5,65.9 173.8,62.0 174.2,58.2 174.7,54.3 175.2,50.4 175.8,46.6 176.7,42.7 177.8,38.9 179.5,35.0 184.0,31.2" fill="none" stroke="#888" stroke-width="2"/><text x="175.7" y="139.0" font-size="12">E earcon</text></svg>
```

그림 6 — 구성별 "말 끝 → 첫 소리" 누적 분포. 점선은 300/700/1000 ms 라벨이다. A(빨강)는 1000 ms 선을 거의 넘지 못하고, D(초록)만 700 ms 선 왼쪽에 꼬리가 있다. 회색 E는 응답이 아니라 ack다.

### 6.3 ack가 만능이 아닌 이유

- ack는 "들었다"만 알린다. 그 뒤 2초 넘게 조용하면 사용자는 다시 말하기 시작하고, 그 순간 응답이 시작되면 **겹침(overlap)** 이 생긴다. 긴 처리 중에는 짧은 진행 신호(LED 맥동)를 이어 준다.
- ack를 endpoint보다 먼저 내면(예측이 틀리면) 사용자의 말을 자르는 셈이다. 예측 endpoint를 쓴다면 **"ack는 확신이 높을 때만"** 같은 규칙이 필요하다.
- ack 소리가 마이크로 다시 들어오면 barge-in 검출을 방해한다(L4 §7.3 에코). earcon도 AEC 기준 신호에 넣는다.

### 6.4 함정

- 평균 지연만 보지 않는다. 사용자는 **가장 느린 턴**을 기억한다. p90·p99와 "1초 넘은 턴 비율"을 같이 본다.
- 단계 지연을 독립으로 더했다. 실제로는 네트워크가 나쁠 때 ASR 서버도 느린 식으로 상관이 있어 꼬리가 더 길다.
- 사람 간격 200 ms는 **사람끼리**의 기준이다. 사용자가 기계에게 같은 속도를 기대하는지는 별개의 질문이며, 이 노트는 그것을 주장하지 않는다. 기준선은 목표가 아니라 **눈금**으로 쓴다.

---

## 7. 오류 복구 — 실수 뒤에 무엇을 하나

### 7.1 FR 복구: second-chance 재시도

1절에서 연속 거부(FRR²)가 비쌌다. 아이디어: 첫 시도의 점수가 임계값 바로 아래(grey zone `[θ − δ, θ)`)였다면, **몇 초 안의 재시도에는 낮춘 임계값 θ − δ를 적용**한다. 사람이 다시 부르는 상황은 "방금 애매했던 소리 + 곧바로 비슷한 소리"라는 강한 증거다.

```
FRR_eff = 1 − P(첫 시도 통과) − P(첫 시도 grey) × P(재시도함) × P(재시도 ≥ θ − δ)
추가 FA ≈ (하루 negative가 grey에 떨어지는 횟수) × P(5 s 안에 negative가 θ − δ 넘음)
```

말로 하면: 애매하게 놓친 호출만 구제하고, 오작동은 "negative 두 개가 연달아 애매하게 높을" 때만 늘어난다. 독립이면 두 번째 조건이 아주 드물다.

### 7.2 코드로 확인 — grey zone 폭에 따른 효과

무엇을 확인하는 코드인지: 1절 'UX 집' 동작점(θ = 4.40)에서 grey 폭 δ를 0, 0.5, 1.0으로 바꿀 때 유효 FRR과 추가 FA.

```python
import numpy as np
from scipy.stats import norm
DP, TH, CALLS, NEG = 6.0, 4.40, 20, 25_920    # 1절 'UX 집' 동작점 (가정 재사용)
P_RETRY = 0.8                                  # 거부당한 사용자가 5 s 안에 다시 말할 확률 (가정)
for grey in (0.0, 0.5, 1.0):                   # grey zone 폭: [TH−grey, TH)
    lo = TH - grey
    p_hit = norm.sf(TH - DP)                   # 첫 시도 통과
    p_grey = norm.cdf(TH - DP) - norm.cdf(lo - DP)   # 첫 시도가 grey에 떨어짐
    p_second = P_RETRY * norm.sf(lo - DP)      # 재시도는 낮춘 임계값 lo로 판정
    frr_eff = 1 - p_hit - p_grey * p_second    # grey 밖으로 떨어진 거부는 구제 못 함
    fa_grey = NEG * (norm.cdf(TH) - norm.cdf(lo))     # negative가 grey에 떨어지는 하루 횟수
    k = 5                                      # 5 s 창 = negative 창 5개 (1창/s, 가정)
    fa_extra = fa_grey * (1 - (1 - norm.sf(lo)) ** k)  # 독립 가정: 이어서 lo를 넘을 확률
    print(f"grey {grey:.1f}  FRR {norm.cdf(TH-DP)*100:4.2f}% → {frr_eff*100:4.2f}%  "
          f"extra FA/day {fa_extra:.4f}  (base FA/day {NEG*norm.sf(TH):.3f})")
```

```text
grey 0.0  FRR 5.48% → 5.48%  extra FA/day 0.0000  (base FA/day 0.140)
grey 0.5  FRR 5.48% → 2.58%  extra FA/day 0.0003  (base FA/day 0.140)
grey 1.0  FRR 5.48% → 1.49%  extra FA/day 0.0145  (base FA/day 0.140)
```

손으로 맞춰 보기 (δ = 0.5): `P(grey) = Φ(−1.6) − Φ(−2.1) = 0.0548 − 0.0179 = 0.0369`, `P(재시도 통과) = 0.8 × Q(−2.1) = 0.8 × 0.982 = 0.786`, 구제 = 0.0369 × 0.786 = 0.029 → FRR 5.48 − 2.90 = 2.58 %.

출력에서 볼 것: δ = 0.5만으로 FRR이 절반 아래로 줄고, 추가 FA는 0.0003/day로 무시할 만하다. δ = 1.0이면 추가 FA가 기본 FA의 10 %까지 커진다. **주의**: 추가 FA 계산은 negative 창이 독립이라는 가정이다. TV 광고가 비슷한 문구를 반복하면 두 창이 강하게 상관되어 추가 FA가 훨씬 커진다(I3 §2). 실제로는 재시도 구간을 SV 통과(§3)와 함께 요구하는 식으로 막는다.

### 7.3 FA 복구: 빨리 끝내고, 되돌릴 수 있게

- **wake word 없이 취소**: 깨어난 직후 몇 초 동안은 "아니야", "취소" 같은 짧은 명령을 wake word 없이 받는다. 사용자가 오작동을 알아챈 순간 바로 끝낼 수 있어야 한다.
- **조용한 타임아웃**: 오작동이면 뒤에 말이 없다. 일정 시간 안에 말이 없으면 응답 없이 LED만 끄고 끝낸다. "죄송해요, 잘 못 들었어요"를 말하면 오작동의 방해 비용을 키운다.
- **방금 것 지우기**: 주요 음성 비서들은 "방금 말한 것 지워 줘"류의 음성 삭제 명령을 제공해 왔다(세부는 제품·지역별로 다름). 펌웨어 쪽에서는 마지막 세션의 audio_id를 기억하고 삭제 요청을 서버로 전파하는 경로가 필요하다(§8, 삭제 파이프라인은 H6 §9).
- **오작동 신고를 학습 신호로**: 사용자가 취소한 세션은 "아마 FA"라는 약한 라벨이다. 단, 그 오디오를 학습에 쓰려면 동의 범위가 필요하다(H6 §3). 동의가 없으면 점수·메타데이터만 남긴다.

### 7.4 ASR·의도 단계의 복구

ASR confidence가 낮을 때 되묻는 것은 L4 §8.2에서 다뤘다. UX 쪽에서 덧붙일 것은 하나다: **되묻기는 비용이다.** 되묻기가 잦으면 그 자체가 FR처럼 느껴진다. confidence가 애매하면 "가장 그럴듯한 해석으로 실행하되 되돌릴 수 있게"(예: "알람을 7시로 맞췄어요" — 틀렸으면 사용자가 바로 고친다)가 되묻기보다 나은 경우가 많다. 되돌릴 수 없는 행동(메시지 전송, 결제)만 확인을 받는다.

---

## 8. 투명성과 주변 사람

### 8.1 활성화 기록 — 사용자가 검증할 수 있게

"기기가 언제 깨어났고, 그때 무엇이 나갔는가"를 사용자가 앱에서 볼 수 있으면, 표시(LED)를 놓친 순간도 나중에 확인할 수 있다. 오작동도 숨기지 않고 보여 준다. 이 기록은 **기기에서 만들어져야** 한다 — 서버 기록만 있으면 "기기가 몰래 보냈다가 서버에 안 남긴 것"을 증명할 수 없다.

무엇을 확인하는 코드인지: 세션마다 남기는 16바이트 활성화 레코드. 앱의 기록 화면, 삭제 요청의 키(audio_id), §4.6의 LED 지연 감시를 한 레코드로 해결한다.

```c
/* 활성화 기록(activation history) 레코드: 앱의 "기기가 깨어난 기록" 화면과 삭제의 근거 */
#include <stdio.h>
#include <stdint.h>
typedef struct __attribute__((packed)) {
    uint32_t t_sec;          /* RTC 시각 */
    uint16_t kws_score_q8;   /* KWS 점수 (Q8.8) */
    int8_t   sv_score_q7;    /* 화자 검증 cosine (Q0.7, −1..1) */
    uint8_t  flags;          /* bit0 SV 통과, bit1 오디오 전송됨, bit2 저장됨, bit3 사용자 취소, bit4 mute 중 */
    uint16_t led_lat_ms;     /* wake → LED 확인까지 걸린 시간 (불변식 감시용) */
    uint16_t audio_ms;       /* 전송·저장된 오디오 길이 (0이면 기기 안에서 끝남) */
    uint32_t audio_id;       /* 서버 쪽 객체 id (삭제 요청을 전파할 키) */
} act_rec_t;
int main(void) {
    act_rec_t r = { 1791100800u, (uint16_t)(4.62 * 256), (int8_t)(0.41 * 127), 0x03, 5, 2400, 0x1A2B3C4Du };
    printf("sizeof(act_rec_t) = %zu B, 256 records = %zu B\n", sizeof r, 256 * sizeof r);
    printf("kws=%.2f sv=%.2f sent=%d stored=%d led=%u ms audio=%u ms\n", r.kws_score_q8 / 256.0,
           r.sv_score_q7 / 127.0, (r.flags >> 1) & 1, (r.flags >> 2) & 1, r.led_lat_ms, r.audio_ms);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 hist.c -o hist && ./hist
```

```text
sizeof(act_rec_t) = 16 B, 256 records = 4096 B
kws=4.62 sv=0.41 sent=1 stored=0 led=5 ms audio=2400 ms
```

출력에서 볼 것: 레코드 하나 16 B, 256개 링 = 4 KB로 MCU flash 한 sector 크기다. KWS·SV 점수가 들어 있어 "왜 깨어났는지"를 앱이 보여 줄 수 있고, `audio_ms = 0`이면 "기기 안에서 끝났다(아무것도 안 나갔다)"를 사용자에게 보여 줄 수 있다. 이 레코드도 개인정보이므로 보관 기간·삭제는 H6 §9 규칙을 따른다.

기록 화면에서 보여 줄 것(예시):

| 항목 | 사용자에게 보이는 문장 |
|---|---|
| 시각, 점수 | "오후 3:12 — 기기가 깨어남 (확신 낮음)" |
| 전송 여부 | "오디오 2.4초가 처리를 위해 전송됨" 또는 "기기 안에서 끝남" |
| 저장 여부 | "보관 안 함" / "30일 보관 (설정에서 변경)" |
| 사용자 행동 | "삭제" 버튼, "이건 내가 부른 게 아님" 신고 |

### 8.2 Bystander — 동의하지 않은 사람

착용자는 약관에 동의했지만 옆 사람은 아니다(H6 §0.1). UX와 펌웨어가 할 수 있는 것:

- **바깥에서 보이는 표시**: 활성 LED는 착용자 쪽이 아니라 **주변 사람이 볼 수 있는 방향**에도 있어야 한다. 귀 안쪽 earcon은 주변 사람에게 아무것도 알리지 않는다.
- **조용 모드에서도 표시 유지**: §5.3. 소리·진동은 끌 수 있어도 활성 LED는 끌 수 없게 한다.
- **타인 음성 최소화**: SV(§3)는 오작동 감소뿐 아니라 "착용자가 아닌 사람의 말로 세션이 시작되지 않게" 하는 bystander 보호 장치이기도 하다. 세션 중에도 착용자가 아닌 화자의 구간을 전사·저장에서 빼는 정책을 검토할 수 있다(구현 난도 높음).
- **장소 기반 제한**: 병원·화장실처럼 민감한 장소에서 기능을 끄는 것은 제품 정책의 문제다. 엔지니어는 "끌 수 있는 스위치"와 그 상태가 활성화 기록에 남는 구조를 만든다.
- **사회적 신호**: 카메라 안경 사례(§4.2)처럼, 표시가 가려지거나 훼손됐을 때 기능을 멈추는 것은 착용자의 편의보다 주변 사람의 신뢰를 우선한 설계다. 마이크 기기에도 같은 논리를 적용할 수 있다.

---

## 9. 임베디드 관점에서 다시 보기

### 9.1 hardware mute 상태 보고

- mute 스위치는 GPIO **입력**으로만 연결한다. 풀업, 하드웨어 RC 또는 펌웨어 debounce(예: 20 ms 동안 같은 값이면 확정)를 둔다.
- 펌웨어는 mute 상태를 "명령"이 아니라 **관측값**으로 다룬다: 앱 표시, 활성화 기록의 flags bit4, telemetry의 mute 시간 비율(§2.5의 이탈 선행 지표).
- **교차 검증**: mute가 켜져 있는데 마이크 DMA에 0이 아닌 데이터가 들어오면, 스위치나 배선이 고장 난 것이다. 이 경우를 "mute 무결성 오류"로 기록하고 사용자에게 알린다. 거짓말하는 mute는 mute가 없는 것보다 나쁘다.
- 부팅 직후에도 스위치를 먼저 읽는다. 펌웨어 초기화 순서가 "마이크 드라이버 → mute 읽기"면 부팅 수백 ms 동안 mute가 무시된 것처럼 보인다(실제로는 하드웨어가 끊고 있어도 로그가 혼란스럽다).

### 9.2 표시등 지연 보장

| 요구 | 구현 |
|---|---|
| wake 후 LED 확인까지 상한 (예: 20 ms, 가정) | LED 켜기는 wake 이벤트 처리의 **첫 동작**. 큐에 넣지 않고 ISR 또는 최고 우선순위 태스크에서 GPIO를 직접 친다 |
| LED 확인 전 egress 금지 | 무선 TX와 flash write API가 `led_confirmed` 플래그를 내부에서 검사한다. 호출자가 검사하게 두면 누군가 빠뜨린다 (§4.5의 버그) |
| LED는 마지막 전송 뒤에 끈다 | 무선 스택의 TX 완료 콜백(버퍼가 실제로 비었음) 뒤에 끈다. "전송 요청" 뒤가 아니다 |
| 지연 측정 | 활성화 레코드의 `led_lat_ms`, HIL에서 logic analyzer |

Don에게 익숙한 비유: NVMe 명령 완료(CQE)를 "DMA를 시작했을 때"가 아니라 "데이터가 실제로 쓰였을 때" 올리는 것과 같다. LED 끄기도 "보내기 시작"이 아니라 "다 보냈음"에 묶는다.

### 9.3 tamper-resistance 체크리스트

- [ ] mute가 전원과 클럭을 **둘 다** 끊는다. 모든 마이크가 스위치 뒤에 있다.
- [ ] 펌웨어가 mute를 해제할 회로 경로가 없다 (스위치 → 펌웨어 방향은 읽기 전용).
- [ ] LED 전류 감지로 단선·단락·가림(광센서가 있다면)을 검출하고, 검출되면 egress를 막는다.
- [ ] egress·저장 API 안에서 LED 확인을 검사한다 (호출자 책임 아님).
- [ ] secure boot와 서명된 펌웨어 업데이트 — 펌웨어 불변식은 펌웨어가 교체되면 의미가 없다. 하드웨어 경로(mute)만이 교체된 펌웨어에도 살아남는다.
- [ ] 디버그 포트(JTAG/SWD)는 양산에서 잠근다. 열려 있으면 GPIO를 마음대로 칠 수 있다.
- [ ] factory test에 LED 전류 경로, mute 스위치 양극, mute 중 마이크 무신호 확인을 넣는다.

### 9.4 전력과의 관계

LED와 earcon도 전력을 쓴다. 오작동 한 번의 에너지(B5 §8.1, I3)에 LED 수백 ms와 스피커 구동이 더해진다. 그래도 **LED를 아끼려고 표시를 줄이면 안 된다** — 저전력 LED를 고르고 밝기를 PWM으로 낮추되, 켜짐 자체는 불변식으로 남긴다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| spec "FA ≤ 1/day"에만 맞춘 임계값 | 리뷰에 "갑자기 말을 건다" 불만이 많다 | 오작동의 프라이버시·사회적 비용 무시 | UX 비용 함수로 동작점 선택 (§1), 맥락별 c_FA |
| FRR 평균만 본다 | "몇 번 불러야 대답한다" 불만 | 연속 거부(상관)가 평균에 묻힘 | 2연속 실패율을 따로 재고 second-chance 재시도 (§7) |
| 짧은 베타로 FA 영향 판단 | 출시 후 몇 주 뒤 사용량 급감 | 신뢰 누적 효과가 늦게 나타남 | 장기 지표(주별 잔존, mute 시간 비율) 추적 (§2) |
| SV threshold를 조용한 방 기준으로 | 지하철·거리에서 주인이 거부됨 | 등록 조건과 사용 조건 불일치 | SNR 조건부 threshold, 다환경 등록 (§3) |
| LED를 `led_on()` 명령으로만 켬 | LED 단선 기기에서도 녹음이 나감 | 확인(read-back) 없음 | 전류 감지 확인 후에만 egress (§4.4) |
| 지연 줄이려 LED 전에 pre-roll 전송 | trace checker 위반, 감사에서 발견 | 성능 최적화가 불변식을 우회 | egress API 내부에서 검사, CI checker (§4.5) |
| mute를 펌웨어가 구현 | "mute인데 들은 것 같다" 의심, 보안 연구자 지적 | 소프트웨어 mute는 신뢰할 수 없음 | 스위치가 전원·클럭 직접 차단, 펌웨어는 읽기만 (§4.3) |
| 오작동 뒤 긴 TTS로 응답 | 오작동 한 번이 더 거슬림 | 피드백 길이를 FA 상황에 맞추지 않음 | 짧은 earcon + 조용한 타임아웃 (§7.3) |
| 조용 모드에서 LED까지 끔 | 주변 사람이 녹음 여부를 모름 | 표시를 착용자 편의로만 봄 | 소리·진동만 끄고 LED는 유지 (§5.3, §8.2) |
| 평균 지연으로 목표 관리 | "가끔 너무 느리다" 불만 | 꼬리(p90·p99) 무시 | 기준선 아래 비율, p90 관리, ack 신호 (§6) |

---

## 11. 면접에서 이렇게 말한다

**Q.** How do you decide the wake-word threshold from a user's perspective?

**A.** 엔지니어링 spec(FA/day 상한)은 출발점일 뿐이다. 오작동과 무시에 사용자 비용을 붙인다. 오작동은 방해 비용에 프라이버시·사회적 비용이 더해지고 맥락(집, 회의)에 따라 커진다. 무시는 한 번은 싸지만 연속되면 포기로 이어져 비싸다. 그 비용 함수를 최소화하는 θ를 고르면 오류 개수 최적점과 다르다. 내 toy 계산에서는 프라이버시 비용을 넣자 FRR을 일부러 3.5 %에서 5.5 %로 올리는 쪽이 이겼다. 그리고 오작동해도 아무것도 나가지 않게 설계하면 오작동 비용이 작아져 임계값을 낮출 여유가 생긴다.

> I start from the engineering spec, but I pick the operating point by minimizing a user-cost function: a false accept carries an interruption cost plus a privacy cost that depends on context, and a false reject is cheap once but expensive when it repeats, because users abandon after two misses. With those weights, the optimum moves away from the minimum-error point — in my model, toward a higher threshold. And architecture changes the weights: if a false wake never lets audio leave the device, its cost drops and I can afford a lower threshold.

**Q.** How do you guarantee the privacy LED can't be bypassed?

**A.** 세 층으로 한다. 첫째, mute는 물리 스위치가 마이크 전원과 클럭을 직접 끊고 펌웨어는 그 상태를 읽기만 한다. 둘째, 활성 표시는 egress·저장 경로에 묶는다: 무선 전송과 flash 쓰기 API가 내부에서 "LED가 전류 감지로 실제 켜졌는가"를 검사하고, 아니면 거부한다. 셋째, 이 불변식을 독립 trace checker로 CI와 HIL에서 검사하고, 버그 주입 빌드가 실패하는지도 테스트한다. secure boot로 펌웨어 교체를 막고, LED 단선·가림이 감지되면 기능 자체를 멈춘다.

> Three layers. The mute switch physically cuts mic power and clock, and firmware can only read its state. The activity LED is tied to egress: the radio-transmit and flash-write paths check that the LED is actually lit, verified by current sensing, before any audio leaves or is stored. Then an independent trace checker enforces "no egress outside a confirmed-LED window" in CI and hardware-in-the-loop tests, including a fault-injected build that must fail. Secure boot protects the firmware layer, and if the LED is open or covered, the feature shuts off rather than recording silently.

**Q.** How can speaker verification help an always-listening device?

**A.** 오작동을 출처별로 나누면 타인의 말과 TV가 큰 몫이다. KWS 뒤에 SV gate를 두면 그 둘을 크게 줄인다. 내 시뮬레이션에서는 FA 2.0/day가 0.26/day가 됐다. 하지만 주인 자신의 오발화는 바닥으로 남고, 시끄러운 곳에서 주인 거부가 늘어난다. 그래서 SNR 조건부 threshold, 애매하면 제한 모드로 응답하는 soft gate, 여러 환경에서의 등록을 쓴다. 덤으로 bystander 보호도 된다: 다른 사람의 말로 세션이 시작되지 않는다.

> If you break false wakes down by source, other people and TV are a big share, and a speaker-verification gate after the keyword spotter removes most of them — in my simulation, from 2.0 to about 0.26 per day. But there's a floor from the owner's own near-miss phrases, and the cost shows up as owner rejections in noise. So I'd use an SNR-dependent threshold, a soft gate that opens a limited mode when unsure, and enrollment across conditions. It also protects bystanders, since someone else's voice can't start a session.

**Q.** What latency feels conversational?

**A.** 사람 대화의 turn 간격은 약 200 ms 규모다(Stivers 외 2009, 10개 언어 평균 약 208 ms). 사람은 상대의 끝을 예측해서 그렇게 빠르다. 침묵을 300~600 ms 기다리는 endpointer는 그 자체로 이 간격을 넘는다. 그래서 두 가지를 한다: 예측 endpoint로 대기 시간을 줄이고, endpoint 직후 짧은 ack earcon으로 "들었다"를 즉시 알린다. 목표는 평균이 아니라 분포로 잡는다 — 예를 들어 첫 응답 소리 p90 1초, ack는 거의 모든 턴에서 수백 ms 안쪽.

> Human turn-taking gaps are on the order of 200 milliseconds — a ten-language study found a mean around 200 ms — and people achieve that by predicting the end of the turn. A silence-based endpointer that waits 300 to 600 ms already exceeds that, so I'd attack the endpoint with a predictive model and give an immediate acknowledgment earcon so the user knows they were heard. I set targets on the distribution, not the mean — for example p90 time-to-first-audio under a second, and an acknowledgment within a few hundred milliseconds on essentially every turn.

**Q.** A user complains the device "keeps ignoring me." How do you debug it?

**A.** 먼저 활성화 기록에서 그 사용자의 KWS·SV 점수를 본다. 점수가 grey zone에 몰려 있으면 임계값이나 second-chance 문제, SV에서 막혔으면 등록 조건과 잡음 문제, KWS 점수가 아예 낮으면 마이크 경로(막힘, 게인)나 착용 위치 문제다. 연속 실패율을 따로 보고, 고친 뒤 FA가 얼마나 늘었는지 같이 본다.

> I'd pull that user's activation log and look at where the scores fall. If KWS scores cluster just under threshold, it's an operating-point issue and second-chance retry helps; if SV is rejecting, it's enrollment versus noise conditions; if KWS scores are low across the board, I suspect the mic path or fit. I track consecutive-miss rate specifically, and I check how much any fix raises false accepts.

**Q.** How do you make false wakes less costly instead of just less frequent?

**A.** 오작동의 결과를 작게 만든다. 오디오는 확인될 때까지 기기 안에만 두고, 피드백은 짧은 earcon 하나, 말이 없으면 조용히 끝낸다. wake word 없이 "취소"를 받고, 기록에서 지울 수 있게 한다. 이러면 c_FA가 내려가 같은 모델로 더 낮은 FRR을 쓸 수 있다.

> Shrink the consequence: keep audio on-device until the wake is confirmed, respond with a short earcon rather than a spoken prompt, time out silently if nothing follows, accept "cancel" without the wake word, and let users see and delete the event. Lower per-event cost lets me run a lower threshold with the same model.

---

## 12. 직접 해보기

1. 손계산: §1의 모델에서 θ = 4.19일 때 FA/day, FRR, '집' UX 비용을 계산하라 (Q(4.19) ≈ 1.39×10⁻⁵, Φ(−1.81) ≈ 0.0351). 정답: FA ≈ 0.36, miss ≈ 0.70, 포기 ≈ 0.025 → 5×0.36 + 0.70 + 4×0.025 ≈ 2.6 — θ = 4.40의 2.04보다 크다.
2. 손계산: §2.3 식으로 FA = 0.5/day의 평형 신뢰 `T*`를 구하라. 정답: 0.076 / (0.076 + 0.038) ≈ 0.67 (§2.4 출력의 0.67과 일치).
3. 코드: §1 코드에 "c_FA를 사용자별로 lognormal(중앙값 5)로 뽑고 사용자별 최적 θ의 분포"를 그려라. 정답/힌트: 개인화된 θ의 범위가 생각보다 넓다 — 그런데 사용자에게 슬라이더를 주면 설명 비용이 생긴다. "민감도 3단계" 정도로 양자화하는 이유를 생각해 보라.
4. 코드: §3 코드에서 등록을 "조용 3 + 시끄러움 2"로 바꾸고 threshold 0.3의 시끄러운 주인 FRR을 비교하라. 정답/힌트: 직접 돌려 보면 이 toy 모델에서는 오히려 나빠진다(시끄러운 주인 FRR 약 4.0 % → 5.5 %). 잡음이 무작위라서 등록 중심만 흐려지기 때문이다. 실제 시스템에서 다환경 등록이 도움이 되는 것은 잡음·마이크 위치 같은 **체계적인** 채널 차이가 embedding을 한쪽으로 옮길 때다 — 모델 가정이 결론을 정한다는 연습.
5. C: §4.5 checker에 "LED_OFF 뒤 200 ms 안에 다시 EGRESS가 없다"와 "END_SESSION 전에 LED_OFF가 오면 위반"을 추가하고, 각각을 깨뜨리는 버그 주입 매크로를 만들어라. 정답/힌트: 두 번째 규칙은 "TX 요청 뒤가 아니라 TX 완료 뒤에 LED를 끈다"(§9.2)를 검사한다.
6. 설계: 웨어러블에서 "조용 모드"를 켰을 때 어떤 신호를 끄고 어떤 것을 유지할지 §5.2 표를 다시 써라. 정답: 소리·진동은 줄이거나 끄고, 바깥을 향한 활성 LED와 mute 표시는 유지한다.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| false accept (FA) | 오작동, false wake | 부르지 않았는데 깨어남. 방해 + 프라이버시 비용 |
| false reject (FR) | 무시, miss | 불렀는데 대답 없음. 연속되면 포기 |
| abandonment | 포기 | 연속 실패 뒤 사용자가 기기 대신 다른 수단을 씀 |
| operating point | 동작점 | 임계값 하나가 정하는 (FA, FRR) 쌍 |
| UX cost function | 사용자 비용 함수 | 실수 종류별 가중치를 붙인 하루 비용 |
| trust / churn | 신뢰 / 이탈 | 누적된 사용자 신뢰와, 기기를 그만 쓰는 사건 |
| speaker verification (SV) | 화자 검증 | 지금 말한 사람이 등록된 주인인가 판정 |
| enrollment | 등록 | 주인 발화로 기준 embedding을 만드는 과정 |
| soft gate | 부드러운 차단 | 애매하면 거부 대신 제한 모드로 응답 |
| privacy indicator | 프라이버시 표시 | 듣는 중·전송 중을 알리는 LED·소리·화면 표시 |
| hardware mute | 하드웨어 차단 | 물리 스위치가 마이크 전원·클럭을 직접 끊음 |
| read-back verify | 되읽기 확인 | 명령 후 실제 상태(LED 전류)를 읽어 확인 |
| egress | 기기 밖으로 나감 | 무선 전송 등 데이터가 기기를 떠나는 사건 |
| invariant checker | 불변식 검사기 | trace만 보고 규칙 위반을 찾는 독립 코드 |
| earcon | 의미 있는 짧은 소리 | "들었다", "취소" 같은 비언어 소리 신호 |
| haptic | 진동 피드백 | 남에게 안 들리게 알리는 진동 패턴 |
| turn-taking gap | 대화 turn 간격 | 한 사람 말 끝과 다음 사람 말 시작 사이 시간 |
| ack | 수신 확인 신호 | 처리 결과 전에 "들었다"를 먼저 알림 |
| second-chance retry | 재시도 구제 | 애매하게 놓친 직후의 재시도에 낮춘 임계값 적용 |
| activation history | 활성화 기록 | 기기가 깨어난 사건과 그때 나간 데이터의 목록 |
| bystander | 주변 사람 | 동의하지 않았지만 마이크 범위에 있는 사람 |
| tamper-resistance | 변조 저항 | 표시·차단 장치를 끄거나 우회하기 어렵게 하는 설계 |

---

## 14. 요약 & 체크리스트

always-listening 기기의 UX는 정확도 지표를 사용자의 비용으로 번역하는 일이다. 오작동은 상황에 따라 커지는 프라이버시·사회적 비용을 갖고, 무시는 연속될 때 비싸지므로, 비용 함수로 고른 동작점은 오류 개수 최적점이나 spec 경계와 다르다. 신뢰는 몇 주에 걸쳐 누적되어 짧은 테스트로는 오작동의 비용이 보이지 않는다. 화자 검증은 타인·TV 오작동을 크게 줄이지만 바닥이 있고 시끄러운 곳의 주인을 거부하는 비용이 있다. 프라이버시 표시는 펌웨어 명령이 아니라 하드웨어 차단, egress에 묶인 LED 확인, 독립 checker로 보장해야 믿을 수 있다. 대화 지연은 사람 turn 간격(약 200 ms 규모)을 눈금으로 삼고, 예측 endpoint와 ack 신호로 체감을 줄인다.

- [ ] FA·FR·연속 거부에 가중치를 붙인 하루 UX 비용을 손으로 계산할 수 있다
- [ ] "오작동 비용을 줄이면 임계값을 낮출 수 있다"를 숫자로 설명할 수 있다
- [ ] 신뢰 toy model의 평형값을 손으로 구하고 그 한계를 말할 수 있다
- [ ] SV gate의 FA 감소, 바닥, 잡음에서의 주인 FRR을 함께 말할 수 있다
- [ ] mute 스위치와 활성 LED의 하드웨어 배선 원칙을 그릴 수 있다
- [ ] "LED 확인 전 egress 금지" 불변식을 trace checker로 짜고 버그 주입으로 검증할 수 있다
- [ ] Stivers 외(2009)의 turn 간격 결과와 Nielsen의 0.1/1/10 s 기준을 정확히 인용할 수 있다
- [ ] 단계 지연 분포에서 기준선 아래 비율을 계산하고 ack의 효과를 설명할 수 있다
- [ ] second-chance 재시도의 FRR 감소와 추가 FA를 계산하고 독립 가정의 위험을 말할 수 있다
- [ ] 활성화 기록 레코드에 무엇을 넣을지와 bystander 보호 방법을 설명할 수 있다

---

## 참고 자료

- Stivers, T. et al. (2009). "Universals and cultural variation in turn-taking in conversation." PNAS 106(26), 10587–10592. [pnas.org](https://www.pnas.org/doi/10.1073/pnas.0903616106) (확인일 2026-10-04)
- Levinson, S. C. & Torreira, F. (2015). "Timing in turn-taking and its implications for processing models of language." Frontiers in Psychology 6.
- Nielsen, J. "Response Times: The 3 Important Limits." NN/g. [nngroup.com](https://www.nngroup.com/articles/response-times-3-important-limits/) (확인일 2026-10-04) — 원 출처는 Nielsen, Usability Engineering (1993)
- Apple Platform Security, "Hardware microphone disconnect." [support.apple.com](https://support.apple.com/guide/security/hardware-microphone-disconnect-secbbd20b00b/web) (확인일 2026-10-04)
- Amazon 고객 도움말, "Turn Your Echo Device's Microphones On or Off." [amazon.com](https://www.amazon.com/gp/help/customer/display.html?nodeId=GKA25FVYEV5PY8C9) (확인일 2026-10-04)
- Meta, "Privacy Settings for Meta AI Glasses." [meta.com](https://www.meta.com/ai-glasses/privacy/) 및 9to5Google 보도 (2026-07-07). [9to5google.com](https://9to5google.com/2026/07/07/meta-ray-ban-smart-glasses-privacy-light-camera-update/) (확인일 2026-10-04)
- 이 스터디 세트의 관련 노트: A2 §9, A4 §9, B5 §3·§5, I3, L4, H6, H7 §6, G6 §2.7
