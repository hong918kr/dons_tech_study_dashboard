# E9. 전력·열 하드웨어 — power domain, clock/power gating, DVFS, 저전력 모드, PMIC, 열 관리

> **이 노트를 다 읽으면**: SoC·MCU가 전력을 줄이는 하드웨어 장치(clock gating, power gating, retention, isolation, DVFS, AVS)를 그림으로 설명할 수 있다 · "추론 사이에 deep sleep에 들어갈 가치가 있나?"를 break-even 식 E_transition / (P_idle − P_sleep)으로 숫자로 답할 수 있다 · OPP 표와 governor가 에너지와 deadline을 어떻게 바꾸는지 시뮬레이션으로 보여 줄 수 있다 · Cortex-M 저전력 모드, PMIC(LDO·buck·PFM), 온도 센서·trip point·throttling 제어 루프를 펌웨어 관점에서 다루고, always-on 센싱 전력을 µA 단위로 예산화할 수 있다
> **JD 연결**: "Profile and optimize memory usage, **power consumption**, real-time performance", "Co-design model architectures that meet latency, memory, **power** … constraints", "Evaluate and select silicon platforms" — study_prep_list **E9**: dynamic power ∝ C·V²·f, leakage / DVFS, clock/power gating, retention / race-to-idle, thermal throttling, 피부 온도 한계. 함께 다루는 행: **K3**(전력 측정, duty-cycle 평균 전류), **K4**(열·지속 성능, throttling 곡선), **N1**(always-on 예산, 피부 온도)
> **Don 기준 난이도**: 전원 시퀀싱, PMIC 레일 bring-up, 전류 프로파일 측정, 전력 margin sign-off, 버스 hang root cause는 이미 손에 익었다 / 그 뒤에 있는 **실리콘 쪽 메커니즘**(retention flop, isolation cell, AVS, 최소 에너지점), governor 정책의 에너지 효과, 열 제어 루프 설계, ML 워크로드에 맞춘 전원 상태 선택을 새로 정리한다
> **선행 노트**: D7(에너지 모델 — C·V²·f, race-to-idle, Horowitz 표, 배터리 수학, 1차 열 RC, 측정 방법론). 함께 보면 좋은 노트: E7(메모리 시스템 — DRAM 에너지), E8(이기종 SoC — always-on island, sensor hub), B7(IMU 모델 — FIFO와 always-on)

---

## 0. 큰 그림 — 이게 왜 필요한가

D7은 **"얼마나"** 를 계산하는 노트였다. 추론 1회에 몇 µJ인지, 배터리가 며칠 가는지, 열이 지속 전력을 몇 mW로 묶는지. 이 노트는 **"어떻게"** 를 다룬다. 칩과 보드가 실제로 전력을 줄이는 하드웨어 장치는 무엇이고, 펌웨어는 그 장치를 어떤 순서로, 어떤 조건에서 켜고 끄는가.

D7에서 가져올 것은 다섯 줄이면 충분하다.

```
P_dyn = α·C·V²·f        사이클당 동적 에너지 = α·C·V²  (f와 무관, V²에 비례)
P_static = V·I_leak(V,T) 켜져 있는 동안 계속 나감 → 에너지는 시간에 비례
race-to-idle            platform 전력이 크고 끝난 뒤 진짜로 꺼질 수 있으면 빨리 끝내고 잔다
data movement           MAC ≪ SRAM ≪ DRAM ≪ radio (바이트당 자릿수로 비싸진다)
thermal RC              지속 가능 전력 ≈ ΔT_허용 / R_th, burst 길이는 τ = R_th·C_th가 정한다
```

말로 하면: 전력을 줄이는 방법은 결국 네 가지다. **토글을 멈추거나**(clock gating), **전원을 끊거나**(power gating), **전압을 낮추거나**(DVFS·AVS), **전력을 변환·배달할 때 새는 것을 줄이거나**(PMIC). 그리고 그 결과로 생기는 열을 **감시하고 제한**한다(thermal management). 이 노트의 순서가 그대로 이것이다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="e9a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="10" y="160" width="80" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="50" y="188" font-size="13" text-anchor="middle">배터리</text><text x="50" y="206" font-size="12" text-anchor="middle">3.0–4.4 V</text><rect x="115" y="40" width="120" height="320" rx="6" fill="#888" fill-opacity="0.12" stroke="currentColor"/><text x="175" y="60" font-size="13" text-anchor="middle">PMIC</text>
<rect x="125" y="75" width="100" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/><text x="175" y="98" font-size="12" text-anchor="middle">buck1 · CPU/NPU</text><rect x="125" y="125" width="100" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/><text x="175" y="148" font-size="12" text-anchor="middle">buck2 · LPDDR</text><rect x="125" y="175" width="100" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/><text x="175" y="198" font-size="12" text-anchor="middle">buck3 · I/O 1.8 V</text>
<rect x="125" y="225" width="100" height="36" rx="4" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="175" y="248" font-size="12" text-anchor="middle">LDO · AON</text><rect x="125" y="275" width="100" height="36" rx="4" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="175" y="298" font-size="12" text-anchor="middle">LDO · RF/센서</text><text x="175" y="345" font-size="12" text-anchor="middle">+ charger · fuel gauge</text><line x1="90" y1="195" x2="113" y2="195" stroke="currentColor" marker-end="url(#e9a)"/><rect x="270" y="20" width="400" height="360" rx="8" fill="none" stroke="currentColor" stroke-dasharray="6 4"/>
<text x="280" y="38" font-size="12">SoC (예시 구조 — 실제 분할은 칩마다 다르다)</text><rect x="290" y="60" width="110" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="345" y="84" font-size="12" text-anchor="middle">CPU cluster</text><text x="345" y="102" font-size="12" text-anchor="middle">DVFS · PG</text><rect x="420" y="60" width="110" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="475" y="84" font-size="12" text-anchor="middle">NPU / DSP</text><text x="475" y="102" font-size="12" text-anchor="middle">DVFS · PG</text>
<rect x="550" y="60" width="105" height="60" rx="6" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="602" y="84" font-size="12" text-anchor="middle">Radio</text><text x="602" y="102" font-size="12" text-anchor="middle">별도 LDO rail</text><rect x="290" y="140" width="240" height="44" rx="6" fill="#888" fill-opacity="0.18" stroke="currentColor"/><text x="410" y="167" font-size="12" text-anchor="middle">interconnect · DDR ctrl (switchable)</text><line x1="290" y1="128" x2="655" y2="128" stroke="#d0564a" stroke-width="3" stroke-dasharray="2 3"/><text x="560" y="150" font-size="12">isolation / level</text><text x="560" y="166" font-size="12">shifter 경계</text>
<rect x="290" y="210" width="365" height="150" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.5"/><text x="300" y="228" font-size="12">always-on (AON) domain — 절대 꺼지지 않음</text><rect x="300" y="240" width="100" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="350" y="266" font-size="12" text-anchor="middle">PMU / 전원 FSM</text><rect x="410" y="240" width="110" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="465" y="266" font-size="12" text-anchor="middle">wake controller</text>
<rect x="530" y="240" width="115" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="587" y="266" font-size="12" text-anchor="middle">RTC · 32 kHz</text><rect x="300" y="296" width="150" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="375" y="317" font-size="12" text-anchor="middle">sensor hub MCU</text><text x="375" y="334" font-size="12" text-anchor="middle">+ retention SRAM</text>
<rect x="460" y="296" width="185" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="552" y="317" font-size="12" text-anchor="middle">TSENS · 온도 센서</text><text x="552" y="334" font-size="12" text-anchor="middle">GPIO/I2C wake 입력</text><line x1="225" y1="93" x2="288" y2="90" stroke="currentColor" marker-end="url(#e9a)"/><line x1="225" y1="143" x2="288" y2="160" stroke="currentColor" marker-end="url(#e9a)"/><line x1="225" y1="243" x2="298" y2="258" stroke="#3f9a6b" stroke-width="1.5" marker-end="url(#e9a)"/>
</svg>
```

그림 1 — 웨어러블급 기기(가정)의 전원 트리와 power domain. 배터리에서 PMIC가 여러 레일을 만들고, SoC 안은 따로 끄고 켤 수 있는 domain으로 나뉜다. 초록 영역(always-on)은 기기가 "꺼져 있는" 동안에도 살아서 깨울 조건을 감시한다. 빨간 점선은 domain 경계 — 여기에 isolation cell과 level shifter가 들어간다. 실제 분할은 칩마다 다르고, 이 그림은 개념도다.

Don에게 익숙한 말로 옮기면: SSD 컨트롤러에서 PS3/PS4(NVMe power state)로 들어갈 때 펌웨어가 한 일 — 클럭 끄기, NAND·DRAM을 저전력으로, 컨텍스트를 AON SRAM에 저장, PCIe L1.2 — 이 이 노트의 3~6절 전체다. 다른 점은 edge ML 기기에서는 이 결정이 **추론 주기(수 ms~수 s)** 단위로 일어나고, 잘못 고르면 모델 최적화로 아낀 에너지를 전원 전환 비용이 다 먹어 버린다는 것이다.

---

## 1. CMOS 전력이 가는 곳 — 복습과 온도

### 1.1 세 가지 항

| 항 | 언제 생기나 | 무엇에 비례하나 | 줄이는 하드웨어 |
|---|---|---|---|
| 동적(switching) | 노드가 0↔1로 바뀔 때 커패시턴스 충·방전 | α·C·V²·f | clock gating, DVFS, 데이터 이동 감소 |
| 단락(short-circuit) | 입력이 천천히 바뀌는 동안 PMOS·NMOS가 잠깐 동시에 켜짐 | 전환 시간(slew) × V × f | 빠른 slew 설계. 보통 동적의 작은 비율 |
| 누설(leakage) | 트랜지스터가 꺼져 있어도 흐르는 전류 | V × I_leak(V, T, Vth) | power gating, retention 전압, high-Vth 셀, body bias |

누설은 다시 몇 갈래다. **subthreshold leakage**(게이트가 꺼져 있어도 채널로 흐르는 전류 — 문턱 전압 Vth 아래에서 지수적으로 줄지만 0은 아님), **gate leakage**(얇은 게이트 산화막을 뚫는 터널링 — high-k 공정으로 크게 줄었다), **junction leakage**(드레인-기판 접합의 역방향 전류, GIDL 등). 이 중 온도에 가장 민감한 것이 subthreshold다.

```
I_sub ∝ exp((V_GS − V_th) / (n · kT/q))
온도 T가 오르면:  kT/q(열 전압)가 커지고, V_th는 내려간다 → 둘 다 I_sub를 키운다
```

말로 하면: 뜨거워지면 트랜지스터가 "덜 꺼진다". 공정·설계마다 다르지만 누설이 **수십 °C마다 두 배** 수준으로 오른다는 것이 흔한 자릿수다. 이 노트에서는 "20 °C마다 2배"를 설명용 가정으로 쓴다.

손계산: 25 °C에서 누설이 50 mW면, 85 °C에서는 2^((85−25)/20) = 2³ = 8배, 400 mW다. 같은 칩이 상온 sleep에서는 누설이 사소해 보여도, 뜨거운 차 안이나 긴 추론 직후에는 누설이 동적 전력만큼 커질 수 있다.

### 1.2 되먹임 — 누설 → 온도 → 누설

누설이 온도를 올리고, 온도가 누설을 올린다. 양의 되먹임이다. 결과는 둘 중 하나다.

```
정상 상태 조건:   T = T_amb + R_th · (P_dyn + P_leak(T))
안정:  방열 직선 (T − T_amb)/R_th 가 발열 곡선 P_dyn + P_leak(T)와 만난다 → 그 교점에서 멈춘다
폭주:  방열 직선이 너무 누워서(R_th가 커서) 발열 곡선 아래에만 있다 → 교점이 없다 → thermal runaway
```

말로 하면: 방열 능력(1/R_th)이 누설의 온도 기울기보다 작아지는 순간, 온도는 어디에서도 멈추지 않는다. 실제 칩은 그 전에 throttling이나 thermal shutdown이 걸리므로 "폭주"를 끝까지 보는 일은 드물지만, **같은 부하인데 기기가 뜨거울수록 전력이 더 드는 현상**은 매일 본다.

동적 400 mW인 칩을 R_th만 바꿔 가며 시뮬레이션한다.

```python
# 누설 전력의 온도 의존성 + 열 되먹임: T = T_amb + R_th·(P_dyn + P_leak(T))
import numpy as np
P_leak25, T_dbl = 0.050, 20.0           # 25 °C에서 누설 50 mW, 20 °C마다 2배 (가정)
def P_leak(T):
    return P_leak25 * 2 ** ((T - 25.0) / T_dbl)
for T in (25, 45, 65, 85, 105):
    print(f"T={T:3d} °C  P_leak={P_leak(T)*1e3:6.1f} mW")

def settle(P_dyn, R_th, T_amb=35.0, C_th=5.0, dt=0.5, t_end=3000):
    T = T_amb
    for _ in range(int(t_end / dt)):               # C·dT/dt = P_total − (T − T_amb)/R_th
        T += dt / C_th * (P_dyn + P_leak(T) - (T - T_amb) / R_th)
        if T > 150: return None                    # 폭주 판정
    return T
print()
for R_th in (20, 40, 60, 70):
    T = settle(P_dyn=0.40, R_th=R_th)
    msg = "RUNAWAY (>150 °C)" if T is None else f"settles at {T:6.1f} °C, P_leak={P_leak(T)*1e3:5.0f} mW"
    print(f"P_dyn=400 mW, R_th={R_th:2d} K/W -> {msg}")
```

```text
T= 25 °C  P_leak=  50.0 mW
T= 45 °C  P_leak= 100.0 mW
T= 65 °C  P_leak= 200.0 mW
T= 85 °C  P_leak= 400.0 mW
T=105 °C  P_leak= 800.0 mW

P_dyn=400 mW, R_th=20 K/W -> settles at   45.0 °C, P_leak=  100 mW
P_dyn=400 mW, R_th=40 K/W -> settles at   57.1 °C, P_leak=  152 mW
P_dyn=400 mW, R_th=60 K/W -> settles at   76.9 °C, P_leak=  303 mW
P_dyn=400 mW, R_th=70 K/W -> RUNAWAY (>150 °C)
```

출력에서 볼 것: 온도가 60 °C 오를 때 누설은 8배다. R_th = 60 K/W에서는 누설이 없다면 35 + 0.4 × 60 = 59 °C에 멈췄을 것이 누설 때문에 77 °C까지 간다(누설 303 mW). R_th = 70 K/W에서는 교점이 없어 폭주한다. R_th = 20 K/W 줄은 손계산으로 확인된다: 45 °C에서 누설 100 mW, 35 + 20 × 0.5 = 45 °C.

```svg
<svg viewBox="0 0 680 345" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="300" x2="620" y2="300" stroke="currentColor"/><line x1="70" y1="300" x2="70" y2="40" stroke="currentColor"/><line x1="70.0" y1="300" x2="70.0" y2="304" stroke="currentColor"/><text x="70.0" y="318.0" font-size="12" text-anchor="middle">35</text><line x1="161.7" y1="300" x2="161.7" y2="304" stroke="currentColor"/><text x="161.7" y="318.0" font-size="12" text-anchor="middle">50</text><line x1="253.3" y1="300" x2="253.3" y2="304" stroke="currentColor"/><text x="253.3" y="318.0" font-size="12" text-anchor="middle">65</text><line x1="345.0" y1="300" x2="345.0" y2="304" stroke="currentColor"/><text x="345.0" y="318.0" font-size="12" text-anchor="middle">80</text>
<line x1="436.7" y1="300" x2="436.7" y2="304" stroke="currentColor"/><text x="436.7" y="318.0" font-size="12" text-anchor="middle">95</text><line x1="528.3" y1="300" x2="528.3" y2="304" stroke="currentColor"/><text x="528.3" y="318.0" font-size="12" text-anchor="middle">110</text><line x1="620.0" y1="300" x2="620.0" y2="304" stroke="currentColor"/><text x="620.0" y="318.0" font-size="12" text-anchor="middle">125</text><line x1="66" y1="300.0" x2="70" y2="300.0" stroke="currentColor"/><text x="62.0" y="304.0" font-size="12" text-anchor="end">0.0</text><line x1="66" y1="262.9" x2="70" y2="262.9" stroke="currentColor"/><text x="62.0" y="266.9" font-size="12" text-anchor="end">0.2</text>
<line x1="66" y1="225.7" x2="70" y2="225.7" stroke="currentColor"/><text x="62.0" y="229.7" font-size="12" text-anchor="end">0.4</text><line x1="66" y1="188.6" x2="70" y2="188.6" stroke="currentColor"/><text x="62.0" y="192.6" font-size="12" text-anchor="end">0.6</text><line x1="66" y1="151.4" x2="70" y2="151.4" stroke="currentColor"/><text x="62.0" y="155.4" font-size="12" text-anchor="end">0.8</text><line x1="66" y1="114.3" x2="70" y2="114.3" stroke="currentColor"/><text x="62.0" y="118.3" font-size="12" text-anchor="end">1.0</text><line x1="66" y1="77.1" x2="70" y2="77.1" stroke="currentColor"/><text x="62.0" y="81.1" font-size="12" text-anchor="end">1.2</text>
<line x1="66" y1="40.0" x2="70" y2="40.0" stroke="currentColor"/><text x="62.0" y="44.0" font-size="12" text-anchor="end">1.4</text><polyline points="70.0,212.6 84.1,211.5 98.2,210.3 112.3,209.0 126.4,207.6 140.5,206.1 154.6,204.5 168.7,202.7 182.8,200.8 196.9,198.7 211.0,196.5 225.1,194.1 239.2,191.4 253.3,188.6 267.4,185.5 281.5,182.1 295.6,178.5 309.7,174.6 323.8,170.3 337.9,165.7 352.1,160.7 366.2,155.3 380.3,149.4 394.4,143.1 408.5,136.2 422.6,128.7 436.7,120.7 450.8,111.9 464.9,102.4 479.0,92.2 493.1,81.1 507.2,69.0 521.3,56.0 535.4,41.8" fill="none" stroke="#d0564a" stroke-width="2.2"/>
<polyline points="70.0,300.0 412.2,40.0" fill="none" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="6 4"/><polyline points="70.0,300.0 620.0,61.2" fill="none" stroke="#e08a3c" stroke-width="2" stroke-dasharray="6 4"/><circle cx="205.1" cy="197.5" r="5" fill="#4a7bd0"/><text x="213.1" y="217.5" font-size="12" text-anchor="start">안정점 57 °C</text><text x="345.0" y="332.0" font-size="13" text-anchor="middle">칩 온도 T (°C)</text><text x="20.0" y="170.0" font-size="13" text-anchor="middle" transform="rotate(-90 20 170)">전력 (W)</text><line x1="330" y1="232" x2="355" y2="232" stroke="#d0564a" stroke-width="3"/>
<text x="362.0" y="236.0" font-size="12" text-anchor="start">발열 P_dyn + P_leak(T)  (400 mW + 누설)</text><line x1="330" y1="252" x2="355" y2="252" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="6 4"/><text x="362.0" y="256.0" font-size="12" text-anchor="start">방열 (T − 35)/R_th, R_th = 40 K/W</text><line x1="330" y1="272" x2="355" y2="272" stroke="#e08a3c" stroke-width="2" stroke-dasharray="6 4"/><text x="362.0" y="276.0" font-size="12" text-anchor="start">R_th = 70 K/W → 교점 없음 → 폭주</text>
</svg>
```

그림 2 — 발열 곡선(빨강)과 방열 직선(점선)의 교점이 정상 상태 온도다. R_th = 40 K/W이면 57 °C에서 안정되고, R_th = 70 K/W이면 두 선이 만나지 않아 온도가 계속 오른다. 파라미터는 설명용 가정이다.

### 1.3 펌웨어·측정과 연결

- factory test나 sign-off에서 sleep 전류를 **상온에서만** 재면 고온 사용 조건의 배터리 수명을 과대평가한다. 사양은 보통 25 °C typ와 고온 max를 따로 준다 — 고온 max가 몇 배인 경우가 흔하다.
- 긴 추론(온디바이스 LLM 수십 초) 직후의 idle 전력은 식기 전까지 평소보다 높다. 에너지 측정의 baseline은 **같은 온도에서** 잡아야 한다(D7 8절).
- 온도를 로그에 같이 남기지 않은 전력 데이터는 반쪽이다.

---

## 2. Clock gating — 토글을 멈춘다

### 2.1 직관

동적 전력은 **토글**에서 나온다. 계산할 데이터가 없는데도 클럭이 돌면 flip-flop의 클럭 핀, 클럭 트리의 버퍼가 매 사이클 충·방전된다. 클럭 트리는 칩에서 가장 자주 토글하는 넷이라 동적 전력의 상당한 비율(설계에 따라 수십 %로 자주 인용)을 차지한다. clock gating은 "쓰지 않는 블록의 클럭을 막는다"는 단순한 아이디어다.

### 2.2 fine-grained vs coarse-grained

| 종류 | 누가 넣나 | 단위 | 예 | 펌웨어가 할 일 |
|---|---|---|---|---|
| fine-grained | 합성 툴이 자동 삽입 (ICG 셀) | 레지스터 그룹 (enable이 있는 flop 묶음) | "load enable이 0이면 이 32비트 레지스터 클럭 차단" | 없음 — 하드웨어가 알아서 |
| coarse-grained (블록) | 설계자가 clock controller에 둔다 | IP 블록 전체 (UART, SPI, NPU…) | peripheral clock enable 레지스터 | 쓰지 않는 IP의 클럭을 끈다 |
| 코어 클럭 정지 | CPU 명령 | 코어 전체 | WFI / WFE | idle 루프에서 실행 |
| 소스 정지 | clock controller | PLL·오실레이터 | deep sleep에서 PLL off, 32 kHz만 남김 | 깨어날 때 PLL lock 대기 |

ICG(integrated clock gating) 셀은 latch 하나와 AND 게이트로 만든다. latch가 있어야 enable이 클럭 high 구간에 바뀌어도 잘린 펄스(glitch)가 생기지 않는다.

```
         ┌────────┐
 EN ────►│ latch  │── en_l ──┐
         │(CLK low│          │   ┌─────┐
CLK ──┬─►│에 투명) │          └──►│ AND ├──► GCLK (게이트된 클럭)
      │  └────────┘              │     │
      └─────────────────────────►│     │
                                 └─────┘
 CLK가 low일 때 EN을 잡아 두었다가, high 구간 동안은 고정 → GCLK에 잘린 펄스가 없다
```

### 2.3 무엇을 아끼고 무엇을 못 아끼나

clock gating은 **동적 전력만** 없앤다. 전압은 그대로이므로 누설은 그대로다. D7 1.4절의 표에서 "clock-gated idle (WFI)"가 누설을 못 줄였던 이유다. 대신 전환 비용이 거의 없다 — 다음 사이클에 바로 다시 켤 수 있다.

손계산: α·C = 0.3 nF, 0.8 V, 400 MHz인 NPU가 idle인데 클럭만 돌고 있다면(α는 idle에서도 클럭 트리 몫이 남는다고 치고 20 %만 남는다고 가정) 0.2 × 0.3 nF × 0.64 × 400 MHz ≈ 15 mW가 그냥 샌다. 블록 clock enable 하나로 이것이 0이 된다. 반면 누설 10 mA × 0.8 V = 8 mW는 남는다 — 그것을 없애려면 3절의 power gating이 필요하다.

펌웨어 함정: peripheral clock을 끈 상태에서 그 블록 레지스터를 읽으면, 칩에 따라 0이 읽히거나, bus error가 나거나, **버스가 멈춘다**. Don이 RF 칩셋에서 본 "버스 hang root cause" 중 상당수가 이 부류다 — "누가 언제 이 블록의 클럭/전원을 껐나"를 먼저 의심한다.

---

## 3. Power gating · power domain · isolation · retention

### 3.1 power switch — header와 footer

power gating은 블록의 전원 자체를 끊어 **누설까지** 없앤다. 방법은 블록과 레일 사이에 큰 트랜지스터 스위치를 넣는 것이다.

- **header switch**: VDD와 블록 사이의 PMOS. 끄면 블록의 "virtual VDD"가 떠서 0 V 쪽으로 내려간다.
- **footer switch**: 블록과 VSS 사이의 NMOS. 같은 원리로 접지 쪽을 끊는다.
- 스위치 자체의 저항 때문에 켜져 있을 때 약간의 전압 강하(IR drop)가 생기고, 꺼져 있을 때도 스위치를 통한 작은 누설이 남는다 — 0이 아니라 "수십~수백 배 작다" 정도다.

켤 때가 까다롭다. 꺼진 블록의 virtual VDD에 달린 커패시턴스를 순식간에 충전하면 **rush current(돌입 전류)** 가 레일을 흔들어 옆 블록이 오동작한다. 그래서 스위치를 여러 개로 나눠 **daisy chain으로 천천히** 켠다(약한 스위치 먼저, 강한 스위치 나중). 이 때문에 power-up에 µs급 시간이 든다. Don이 PMIC bring-up에서 본 soft-start와 같은 문제를 칩 안에서 푸는 것이다.

### 3.2 power domain, isolation, level shifter, always-on

**power domain**은 "같이 켜지고 같이 꺼지는 로직 묶음"이다. 칩은 CPU cluster, NPU, DSP, 멀티미디어, interconnect, AON 등으로 나뉘고, 각 domain을 PMU(power management unit — 칩 안의 전원 FSM)가 순서대로 켜고 끈다.

domain 경계에는 두 가지 셀이 필요하다.

- **isolation cell**: 꺼진 domain의 출력은 0도 1도 아닌 떠 있는 값(X)이다. 이 값이 켜진 domain의 입력으로 들어가면 입력단 트랜지스터가 반쯤 켜져 누설이 흐르거나 로직이 오동작한다. isolation cell은 끄기 전에 출력을 0 또는 1로 **고정(clamp)** 한다.
- **level shifter**: 두 domain의 전압이 다르면(예: 0.6 V NPU ↔ 0.8 V interconnect) 신호 레벨을 바꿔 준다. DVFS로 domain 전압이 수시로 바뀌는 칩에는 필수다.

**always-on(AON) domain**은 이름 그대로 꺼지지 않는 domain이다. PMU 자신, wake controller, RTC, 일부 retention SRAM, 온도 센서, wake 입력 GPIO가 여기 산다. AON의 누설은 **기기의 바닥 전류**가 되므로 가능한 작게, 낮은 전압·느린 클럭(32 kHz)으로 설계한다. E8의 "always-on island / sensor hub"는 이 domain을 크게 키워 작은 MCU까지 넣은 것이다.

설계 쪽에서는 이 모든 것(어느 셀이 어느 domain인지, isolation 값, retention 대상, 전원 상태 표)을 **UPF(Unified Power Format, IEEE 1801)** 로 기술한다. 펌웨어 엔지니어가 UPF를 쓸 일은 없지만, 칩 문서의 "power state table"이 여기서 나온다는 것은 알아 두면 좋다.

### 3.3 retention — 상태를 잃지 않고 끄기

전원을 끊으면 레지스터와 SRAM 내용이 날아간다. 깨어날 때마다 전부 다시 초기화하면 느리고 에너지가 든다. 해결책은 **일부만 살려 두는 것**이다.

- **retention flop**: 일반 flop 옆에 AON 전원에 붙은 작은 latch("balloon latch")를 둔다. 끄기 전 SAVE 신호로 값을 latch에 옮기고, 켠 뒤 RESTORE로 되돌린다. 면적과 누설이 조금 늘지만 복구가 몇 사이클이다.
- **SRAM retention**: SRAM bank의 전원을 완전히 끊지 않고 **데이터가 유지되는 최소 전압**으로 내린다(주변 회로는 끄고 셀 배열만 유지). bank 단위로 켜고 끌 수 있는 칩이 많다 — "64 KB만 retention" 같은 설정이 여기서 나온다.
- **소프트웨어 save/restore**: 하드웨어 retention이 없는 레지스터는 펌웨어가 AON SRAM에 저장했다가 되돌린다. SSD 펌웨어의 context save와 같다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="e9b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><line x1="30" y1="40" x2="650" y2="40" stroke="currentColor" stroke-width="2"/><text x="34" y="32" font-size="12">VDD (항상 켜진 레일)</text><line x1="30" y1="300" x2="650" y2="300" stroke="currentColor" stroke-width="2"/><text x="34" y="318" font-size="12">VSS</text><rect x="80" y="60" width="70" height="40" rx="4" fill="#e08a3c" fill-opacity="0.3" stroke="currentColor"/><text x="115" y="85" font-size="12" text-anchor="middle">header</text>
<line x1="115" y1="40" x2="115" y2="60" stroke="currentColor"/><line x1="115" y1="100" x2="115" y2="125" stroke="currentColor"/><line x1="30" y1="80" x2="78" y2="80" stroke="currentColor" marker-end="url(#e9b)"/><text x="22" y="72" font-size="12">nSLEEP</text><line x1="60" y1="125" x2="420" y2="125" stroke="#e08a3c" stroke-width="2" stroke-dasharray="6 3"/><text x="305" y="143" font-size="12">virtual VDD (꺼지는 레일)</text><rect x="80" y="145" width="220" height="120" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="90" y="166" font-size="13">꺼지는 도메인 (예: NPU)</text>
<rect x="95" y="180" width="90" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="currentColor"/><text x="140" y="203" font-size="12" text-anchor="middle">일반 flop</text><rect x="195" y="180" width="95" height="36" rx="4" fill="#3f9a6b" fill-opacity="0.3" stroke="currentColor"/><text x="242" y="198" font-size="12" text-anchor="middle">retention</text><text x="242" y="211" font-size="12" text-anchor="middle">flop</text><text x="190" y="248" font-size="12" text-anchor="middle">SRAM: 일부 bank만 retention</text><line x1="190" y1="125" x2="190" y2="145" stroke="currentColor"/>
<line x1="280" y1="40" x2="280" y2="178" stroke="#3f9a6b" stroke-width="1.5"/><text x="286" y="100" font-size="12">AON 전원 → retention latch</text><rect x="340" y="190" width="80" height="40" rx="4" fill="#d0564a" fill-opacity="0.25" stroke="currentColor"/><text x="380" y="208" font-size="12" text-anchor="middle">isolation</text><text x="380" y="222" font-size="12" text-anchor="middle">clamp 0/1</text><line x1="300" y1="210" x2="338" y2="210" stroke="currentColor" marker-end="url(#e9b)"/><line x1="420" y1="210" x2="468" y2="210" stroke="currentColor" marker-end="url(#e9b)"/>
<line x1="380" y1="260" x2="380" y2="232" stroke="currentColor" marker-end="url(#e9b)"/><text x="380" y="276" font-size="12" text-anchor="middle">ISO_EN</text><rect x="470" y="160" width="170" height="100" rx="6" fill="#3f9a6b" fill-opacity="0.15" stroke="currentColor"/><text x="555" y="185" font-size="13" text-anchor="middle">켜져 있는 도메인</text><text x="555" y="205" font-size="12" text-anchor="middle">(AON · interconnect)</text><text x="555" y="230" font-size="12" text-anchor="middle">떠 있는 입력(X)이</text><text x="555" y="246" font-size="12" text-anchor="middle">들어오면 누설·오동작</text><rect x="470" y="55" width="170" height="80" rx="6" fill="#888" fill-opacity="0.15" stroke="currentColor"/>
<text x="555" y="75" font-size="12" text-anchor="middle">PMU 끄는 순서</text><text x="555" y="93" font-size="12" text-anchor="middle">clk stop → ISO on</text><text x="555" y="109" font-size="12" text-anchor="middle">→ save(RET) → switch off</text><text x="555" y="126" font-size="12" text-anchor="middle">켜기는 정확히 역순</text><text x="190" y="290" font-size="12" text-anchor="middle">footer 스위치(VSS 쪽 NMOS)로 끊는 방식도 있다</text>
</svg>
```

그림 3 — power gating의 부품. header 스위치가 꺼지면 virtual VDD가 내려가고, retention flop은 AON 전원으로 값을 붙잡으며, isolation cell이 꺼진 domain의 출력을 고정해 켜진 domain을 보호한다. PMU는 이 신호들을 정해진 순서로 움직인다.

### 3.4 순서가 전부다

전형적인 끄기/켜기 시퀀스는 이렇다(칩마다 세부는 다르다).

```
끄기:  ① 트래픽 drain (진행 중인 bus transaction 완료, DMA 정지)
       ② clock gate
       ③ isolation ON  (출력 clamp)
       ④ retention SAVE / SW context save
       ⑤ power switch OFF   → 이후 누설 ≈ 스위치 누설 + retention 셀
켜기:  ⑤' power switch ON (daisy chain, rush current 제한) → 전압 안정 대기
       ④' reset 해제 → retention RESTORE / SW context restore
       ③' isolation OFF
       ②' clock ungate (PLL이 꺼졌었다면 lock 대기)
       ①' 트래픽 재개
```

말로 하면: "출력을 고정하고, 기억할 것을 챙기고, 끈다. 켤 때는 정확히 거꾸로." 순서가 어긋나면 증상이 교과서적이다. ③ 전에 ⑤를 하면 켜진 쪽에 X가 들어가 간헐적 오동작, ① 없이 ②를 하면 **버스 hang**, ③'을 ④'보다 먼저 풀면 restore 전 쓰레기 값이 밖으로 나간다. 이 시퀀스는 보통 PMU 하드웨어가 하지만, 펌웨어가 ①과 SW save/restore를 책임지는 경우가 많다.

### 3.5 전환 에너지는 어디서 오나 — 코드로 분해

power gating의 "비용"을 항목별로 나눠 보면 의외의 결론이 나온다. NPU 서브시스템을 끄고 켜는 한 번의 비용을 가정 값으로 분해한다.

```python
# power-gate 한 번의 전환 에너지를 항목별로 분해 (NPU 서브시스템, 모든 숫자는 설명용 가정)
V_dom, C_dom = 0.8, 100e-9                 # 도메인 전압, 가상 레일 decap
items = {
    "rail recharge (C·V²)":        C_dom * V_dom**2,
    "PLL/clock relock 50 µs@3 mW":  50e-6 * 3e-3,
    "save 4 KB ctx → AON SRAM":     4096 * 5e-12 + 20e-6 * 5e-3,   # 5 pJ/B + 20 µs 제어 오버헤드
    "restore ctx + re-init 200 µs": 200e-6 * 8e-3,                # CPU가 레지스터 재설정
    "reload 300 KB weights (QSPI)": (300e3 / 25e6) * (15e-3 + 10e-3), # 12 ms × (flash+MCU)
}
tot = sum(items.values())
for k, v in items.items():
    print(f"{k:32s} {v*1e6:9.3f} µJ  ({v/tot*100:5.1f} %)")
print(f"{'TOTAL':32s} {tot*1e6:9.3f} µJ")
no_reload = tot - items["reload 300 KB weights (QSPI)"]
print(f"without weight reload:           {no_reload*1e6:9.3f} µJ")

# weight를 retention SRAM에 붙잡아 두는 비용 vs 매번 다시 읽는 비용
ret_w_per_kb = 1.8 * 1e-6 / 32            # 32 KB bank당 1 µA @1.8 V (가정) → W/KB
P_ret = 300 * ret_w_per_kb
print(f"\nretain 300 KB: {P_ret*1e6:.1f} µW   break-even interval = {items['reload 300 KB weights (QSPI)']/P_ret:.1f} s")
```

```text
rail recharge (C·V²)                 0.064 µJ  (  0.0 %)
PLL/clock relock 50 µs@3 mW          0.150 µJ  (  0.0 %)
save 4 KB ctx → AON SRAM             0.120 µJ  (  0.0 %)
restore ctx + re-init 200 µs         1.600 µJ  (  0.5 %)
reload 300 KB weights (QSPI)       300.000 µJ  ( 99.4 %)
TOTAL                              301.934 µJ
without weight reload:               1.934 µJ

retain 300 KB: 16.9 µW   break-even interval = 17.8 s
```

출력에서 볼 것: 전원 스위치·레일 재충전·PLL relock 같은 "하드웨어 전환 비용"은 µJ 이하다. 전체의 99 %는 **꺼진 SRAM에 있던 weight를 flash에서 다시 읽어 오는 비용**이다. 그래서 질문은 "power gate를 할까?"가 아니라 "**weight를 담은 SRAM을 retention으로 남길까?**"가 된다. 300 KB를 retention으로 붙잡는 데 17 µW(가정)라면, 추론 간격이 약 18초보다 짧을 때는 retention이 이긴다.

손계산으로 확인: 300 KB ÷ 25 MB/s = 12 ms, 12 ms × 25 mW = 300 µJ. 300 µJ ÷ 16.9 µW = 17.8 s.

---

## 4. deep sleep의 손익분기 — 언제 더 깊이 잘 가치가 있나

### 4.1 식

상태 A(얕은 잠, 전력 P_A, 전환 비용 거의 0)와 상태 B(깊은 잠, 전력 P_B < P_A, 들어갔다 나오는 데 에너지 E_trans)를 비교한다. idle 시간이 t일 때:

```
E_A(t) = P_A · t
E_B(t) = E_trans + P_B · t
손익분기:  t_be = E_trans / (P_A − P_B)
```

말로 하면: 깊은 잠이 매초 아껴 주는 전력(P_A − P_B)으로 입장료(E_trans)를 갚는 데 걸리는 시간이 t_be다. idle이 t_be보다 길면 깊이 자고, 짧으면 얕게 잔다. 여기에 **wake latency 제약**이 하나 더 붙는다 — 깨어나는 데 3 ms가 걸리는 상태는 1 ms 안에 반응해야 하는 인터럽트가 있으면 쓸 수 없다.

주의: E_trans는 "전환 중에 쓴 에너지 전부"가 아니라 "그 시간 동안 **A에 머물렀다면 썼을 에너지를 뺀 초과분**"이다. 측정할 때는 D7 8절처럼 ∫(P − P_A)dt로 잡는다.

손계산 (가정 값):

```
WFI:         P = 1.2 mW,  E_trans = 0,       wake 2 µs
retention:   P = 0.08 mW, E_trans = 15 µJ,   wake 60 µs
power-off:   P = 0.005 mW, E_trans = 400 µJ (weight reload 포함), wake 3 ms

retention vs WFI:        15 / (1.2 − 0.08)    = 13.4 ms
power-off vs retention:  (400 − 15) / (0.08 − 0.005) = 5133 ms ≈ 5.1 s
```

말로 하면: 이 가정에서는 idle이 13 ms만 넘어도 retention이 이득이고, 전원을 완전히 끄는 것은 idle이 5초를 넘을 때만 이득이다. 추론이 초당 몇 번 도는 KWS·IMU 모델이라면 답은 retention이다.

### 4.2 코드 — 상태 선택기 (C)

펌웨어의 idle 루프가 실제로 하는 판단을 C로 쓴다. "다음 이벤트까지 남은 시간"과 "허용 wake latency"를 받아 에너지가 최소인 상태를 고른다.

```c
/* 저전력 상태 선택기: idle 시간 t에 대해 E(t) = E_trans + P_state × t 가 최소인 상태 */
#include <stdio.h>

typedef struct { const char *name; double p_mw; double e_trans_uj; double wake_us; } state_t;

static const state_t S[] = {              /* 모든 숫자는 설명용 가정 */
    { "WFI (clock-gated)",  1.200,   0.0,     2.0 },
    { "retention sleep",    0.080,  15.0,    60.0 },
    { "power-off + reload", 0.005, 400.0,  3000.0 },
};
#define NS (sizeof S / sizeof S[0])

static double energy_uj(const state_t *s, double t_ms) { return s->e_trans_uj + s->p_mw * t_ms; } /* mW×ms = µJ */

int main(void) {
    for (unsigned i = 1; i < NS; i++)
        for (unsigned j = 0; j < i; j++) {
            double tbe = (S[i].e_trans_uj - S[j].e_trans_uj) / (S[j].p_mw - S[i].p_mw);
            printf("break-even %-18s vs %-18s: %8.1f ms\n", S[i].name, S[j].name, tbe);
        }
    const double idle_ms[] = { 1, 10, 20, 100, 500, 2000, 10000 };
    const double max_wake_us = 1000.0;     /* 응답 요구: 1 ms 안에 깨어나야 함 (가정) */
    printf("\n idle_ms | best state (no latency limit)  E_uJ | best with wake<=1 ms       E_uJ\n");
    for (unsigned k = 0; k < sizeof idle_ms / sizeof idle_ms[0]; k++) {
        int b = 0, bl = 0;
        for (unsigned i = 1; i < NS; i++) {
            if (energy_uj(&S[i], idle_ms[k]) < energy_uj(&S[b], idle_ms[k])) b = (int)i;
            if (S[i].wake_us <= max_wake_us &&
                energy_uj(&S[i], idle_ms[k]) < energy_uj(&S[bl], idle_ms[k])) bl = (int)i;
        }
        printf("%8.0f | %-18s %10.1f | %-18s %10.1f\n", idle_ms[k],
               S[b].name, energy_uj(&S[b], idle_ms[k]), S[bl].name, energy_uj(&S[bl], idle_ms[k]));
    }
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 ex2.c -o ex2 -lm && ./ex2
```

```text
break-even retention sleep    vs WFI (clock-gated) :     13.4 ms
break-even power-off + reload vs WFI (clock-gated) :    334.7 ms
break-even power-off + reload vs retention sleep   :   5133.3 ms

 idle_ms | best state (no latency limit)  E_uJ | best with wake<=1 ms       E_uJ
       1 | WFI (clock-gated)         1.2 | WFI (clock-gated)         1.2
      10 | WFI (clock-gated)        12.0 | WFI (clock-gated)        12.0
      20 | retention sleep          16.6 | retention sleep          16.6
     100 | retention sleep          23.0 | retention sleep          23.0
     500 | retention sleep          55.0 | retention sleep          55.0
    2000 | retention sleep         175.0 | retention sleep         175.0
   10000 | power-off + reload      450.0 | retention sleep         815.0
```

출력에서 볼 것: 손계산과 같은 13.4 ms, 5133 ms가 나온다. 10 s idle에서는 power-off가 가장 싸지만, wake ≤ 1 ms 제약을 걸면 retention으로 물러나고 에너지는 815 µJ로 늘어난다 — **지연 요구가 곧 전력 비용**이다.

```svg
<svg viewBox="0 0 680 345" xmlns="http://www.w3.org/2000/svg">
<line x1="80" y1="300" x2="620" y2="300" stroke="currentColor"/><line x1="80" y1="300" x2="80" y2="40" stroke="currentColor"/><line x1="80.0" y1="300" x2="80.0" y2="304" stroke="currentColor"/><text x="80.0" y="318.0" font-size="12" text-anchor="middle">1 ms</text><line x1="205.6" y1="300" x2="205.6" y2="304" stroke="currentColor"/><text x="205.6" y="318.0" font-size="12" text-anchor="middle">10 ms</text><line x1="331.2" y1="300" x2="331.2" y2="304" stroke="currentColor"/><text x="331.2" y="318.0" font-size="12" text-anchor="middle">100 ms</text><line x1="456.7" y1="300" x2="456.7" y2="304" stroke="currentColor"/><text x="456.7" y="318.0" font-size="12" text-anchor="middle">1 s</text>
<line x1="582.3" y1="300" x2="582.3" y2="304" stroke="currentColor"/><text x="582.3" y="318.0" font-size="12" text-anchor="middle">10 s</text><line x1="76" y1="300.0" x2="80" y2="300.0" stroke="currentColor"/><text x="72.0" y="304.0" font-size="12" text-anchor="end">1 µJ</text><line x1="76" y1="242.2" x2="80" y2="242.2" stroke="currentColor"/><text x="72.0" y="246.2" font-size="12" text-anchor="end">10</text><line x1="76" y1="184.4" x2="80" y2="184.4" stroke="currentColor"/><text x="72.0" y="188.4" font-size="12" text-anchor="end">100</text><line x1="76" y1="126.7" x2="80" y2="126.7" stroke="currentColor"/><text x="72.0" y="130.7" font-size="12" text-anchor="end">1 mJ</text>
<line x1="76" y1="68.9" x2="80" y2="68.9" stroke="currentColor"/><text x="72.0" y="72.9" font-size="12" text-anchor="end">10 mJ</text><polyline points="80.0,295.4 93.8,289.1 107.7,282.7 121.5,276.3 135.4,269.9 149.2,263.6 163.1,257.2 176.9,250.8 190.8,244.5 204.6,238.1 218.5,231.7 232.3,225.4 246.2,219.0 260.0,212.6 273.8,206.2 287.7,199.9 301.5,193.5 315.4,187.1 329.2,180.8 343.1,174.4 356.9,168.0 370.8,161.6 384.6,155.3 398.5,148.9 412.3,142.5 426.2,136.2 440.0,129.8 453.8,123.4 467.7,117.1 481.5,110.7 495.4,104.3 509.2,97.9 523.1,91.6 536.9,85.2 550.8,78.8 564.6,72.5 578.5,66.1 592.3,59.7 606.2,53.4 620.0,47.0" fill="none" stroke="#4a7bd0" stroke-width="2.2"/>
<polyline points="80.0,231.9 93.8,231.9 107.7,231.8 121.5,231.8 135.4,231.7 149.2,231.6 163.1,231.4 176.9,231.3 190.8,231.0 204.6,230.8 218.5,230.4 232.3,230.0 246.2,229.4 260.0,228.7 273.8,227.8 287.7,226.6 301.5,225.3 315.4,223.6 329.2,221.6 343.1,219.3 356.9,216.5 370.8,213.4 384.6,209.9 398.5,205.9 412.3,201.6 426.2,197.0 440.0,192.1 453.8,186.8 467.7,181.4 481.5,175.8 495.4,170.1 509.2,164.2 523.1,158.2 536.9,152.1 550.8,146.0 564.6,139.8 578.5,133.5 592.3,127.3 606.2,121.0 620.0,114.7" fill="none" stroke="#3f9a6b" stroke-width="2.2"/>
<polyline points="80.0,149.7 93.8,149.7 107.7,149.7 121.5,149.7 135.4,149.7 149.2,149.7 163.1,149.7 176.9,149.7 190.8,149.7 204.6,149.7 218.5,149.7 232.3,149.7 246.2,149.7 260.0,149.7 273.8,149.6 287.7,149.6 301.5,149.6 315.4,149.6 329.2,149.6 343.1,149.6 356.9,149.6 370.8,149.6 384.6,149.6 398.5,149.6 412.3,149.5 426.2,149.5 440.0,149.4 453.8,149.4 467.7,149.3 481.5,149.2 495.4,149.0 509.2,148.9 523.1,148.6 536.9,148.3 550.8,148.0 564.6,147.5 578.5,146.9 592.3,146.1 606.2,145.2 620.0,144.1" fill="none" stroke="#e08a3c" stroke-width="2.2"/><line x1="221.5" y1="300" x2="221.5" y2="70" stroke="#888" stroke-dasharray="3 3"/>
<text x="224.5" y="66.0" font-size="12" text-anchor="start">13 ms</text><line x1="397.0" y1="300" x2="397.0" y2="70" stroke="#888" stroke-dasharray="3 3"/><text x="400.0" y="66.0" font-size="12" text-anchor="start">335 ms</text><line x1="546.0" y1="300" x2="546.0" y2="70" stroke="#888" stroke-dasharray="3 3"/><text x="549.0" y="66.0" font-size="12" text-anchor="start">5.1 s</text><text x="350.0" y="332.0" font-size="13" text-anchor="middle">idle 시간 (log)</text><text x="22.0" y="170.0" font-size="13" text-anchor="middle" transform="rotate(-90 22 170)">idle 구간 에너지 (log)</text><line x1="100" y1="92" x2="125" y2="92" stroke="#4a7bd0" stroke-width="3"/>
<text x="132.0" y="96.0" font-size="12" text-anchor="start">WFI: 0 µJ + 1.2 mW × t</text><line x1="100" y1="110" x2="125" y2="110" stroke="#3f9a6b" stroke-width="3"/><text x="132.0" y="114.0" font-size="12" text-anchor="start">retention: 15 µJ + 0.08 mW × t</text><line x1="100" y1="128" x2="125" y2="128" stroke="#e08a3c" stroke-width="3"/><text x="132.0" y="132.0" font-size="12" text-anchor="start">power-off + reload: 400 µJ + 0.005 mW × t</text>
</svg>
```

그림 4 — 세 상태의 idle 구간 에너지(양축 log). 선이 교차하는 점이 손익분기 시간이다. 짧은 idle은 WFI, 중간은 retention, 아주 긴 idle만 전원 차단이 이긴다.

### 4.3 실제 시스템에서의 모양

- Linux의 **cpuidle**은 정확히 이 계산을 한다. 각 idle state는 `exit_latency`와 `target_residency`(= 사실상 break-even 시간)를 가지고, governor(menu, TEO 등)가 "다음 wake까지 예상 시간"을 추정해 state를 고른다. 예측이 틀리면(곧 깨어날 줄 모르고 깊이 자면) 에너지와 지연을 둘 다 잃는다.
- MCU에서는 RTOS의 **tickless idle**이 같은 역할이다. 다음 타이머까지 시간을 보고 SLEEP/DEEPSLEEP을 고른다(6절).
- "예상 idle 시간"은 ML 기기에서 오히려 **예측하기 쉽다**. 센서 FIFO watermark, 프레임 주기, 추론 주기가 고정이기 때문이다. 이 결정론을 활용하면 governor의 추정보다 정확하게 상태를 고를 수 있다.

---

## 5. DVFS — 전압과 주파수를 같이 움직인다

### 5.1 왜 f를 올리려면 V를 올려야 하나

게이트 하나의 지연은 "부하 커패시턴스를 전류로 충전하는 시간"이다: t_d ∝ C·V / I_on(V). I_on은 전압이 문턱(Vth)보다 얼마나 높은지에 따라 빠르게 커진다. 그래서

```
f_max(V) ∝ I_on(V) / (C · V)
강반전(V ≫ Vth):      I_on ∝ (V − Vth)^α,  α ≈ 1.2~1.5  (alpha-power law)
문턱 근처·아래:       I_on ∝ exp((V − Vth)/(n·kT/q))  → 지수적으로 느려진다
```

말로 하면: 전압을 낮추면 게이트가 느려져 최대 주파수가 떨어진다. 그러니 **주파수를 내릴 때 전압도 같이 내려야** 이득이 있다. 주파수만 내리면 D7 1.2절대로 일 하나당 동적 에너지는 그대로이고 시간만 길어져 누설이 는다.

### 5.2 OPP 표

실리콘 팀은 각 주파수에서 **모든 칩·온도·노화 조건에서 동작하는 최소 전압**에 guardband를 더해 OPP(operating performance point) 표를 만든다. Linux에서는 device tree의 `operating-points-v2` 바인딩으로 기술한다.

```text
cpu_opp_table: opp-table {
    compatible = "operating-points-v2";
    opp-shared;                                  /* 이 표를 쓰는 CPU들이 한 클럭·전압을 공유 (= per-cluster DVFS) */
    opp-200000000 { opp-hz = /bits/ 64 <200000000>;  opp-microvolt = <590000>; };
    opp-600000000 { opp-hz = /bits/ 64 <600000000>;  opp-microvolt = <830000>; };
    opp-1000000000 { opp-hz = /bits/ 64 <1000000000>; opp-microvolt = <1030000>; };
};
```

(값은 아래 예제의 가정 OPP다.) `opp-shared`가 뜻하는 것이 **per-cluster DVFS**다. 한 cluster의 코어들은 같은 PLL과 같은 레일을 쓰므로 가장 바쁜 코어가 cluster 전체의 OPP를 정한다. big.LITTLE/DynamIQ 칩은 cluster마다 레일과 PLL이 따로라 작은 코어는 낮은 OPP에, 큰 코어는 높은 OPP에 둘 수 있다. NPU·DSP·GPU도 보통 자기 domain의 OPP를 따로 가진다.

### 5.3 코드 — OPP 표 만들기와 최소 에너지점

위 속도 모델(강반전과 문턱 근처를 부드럽게 잇는 식)로 OPP 표를 만들고, 누설까지 넣은 **사이클당 에너지**가 어디서 최소인지 본다.

```python
# OPP 표 만들기 + 최소 에너지점. 속도 모델: f(V) ∝ I_on(V)/V, I_on은 EKV식 보간(가정 파라미터)
import numpy as np
Vth, n, phi_t = 0.35, 1.5, 0.026
Ion = lambda V: np.log1p(np.exp((V - Vth) / (2 * n * phi_t))) ** 2
K = 1000e6 / (Ion(1.0) / 1.0)                      # 1.0 V에서 1 GHz로 정규화
f_of = lambda V: K * Ion(V) / V
Vg = np.arange(0.20, 1.20001, 0.0005)
rows = []
for f in (200e6, 400e6, 600e6, 800e6, 1000e6):
    vmin = Vg[np.argmax(f_of(Vg) >= f)]
    rows.append((f, vmin, np.ceil((vmin + 0.025) / 0.005) * 0.005))  # guardband 25 mV, 5 mV step
Vtop = rows[-1][2]
print(" f_MHz  Vmin_V  V_opp  E/cycle(rel)  P_dyn(rel)")
for f, vmin, vopp in rows:
    print(f"{f/1e6:6.0f}  {vmin:6.3f}  {vopp:5.3f}  {(vopp/Vtop)**2:12.3f}  {(vopp/Vtop)**2*f/1e9:10.3f}")

C, I_leak = 1e-9, 30e-3                             # 1 nF 스위칭 C, 누설 30 mA (가정)
V = np.round(np.arange(0.20, 1.0001, 0.01), 2)
E_dyn, E_leak = C * V**2, I_leak * V / f_of(V)      # 누설 에너지/cycle = P_leak × (1/f)
E = E_dyn + E_leak
i = np.argmin(E)
print(f"\nmin-energy point: V={V[i]:.2f} V  f={f_of(V[i])/1e6:.1f} MHz  E={E[i]*1e12:.0f} pJ/cycle")
for v in (0.20, 0.25, 0.30, V[i], 0.50, 0.70, 1.00):
    j = int(np.argmin(abs(V - v)))
    print(f"  V={V[j]:.2f}  f={f_of(V[j])/1e6:7.1f} MHz  dyn={E_dyn[j]*1e12:5.0f}  leak={E_leak[j]*1e12:6.0f}  total={E[j]*1e12:6.0f} pJ")
```

```text
 f_MHz  Vmin_V  V_opp  E/cycle(rel)  P_dyn(rel)
   200   0.564  0.590         0.328       0.066
   400   0.691  0.720         0.489       0.195
   600   0.801  0.830         0.649       0.390
   800   0.903  0.930         0.815       0.652
  1000   1.000  1.030         1.000       1.000

min-energy point: V=0.49 V  f=111.6 MHz  E=372 pJ/cycle
  V=0.20  f=    1.3 MHz  dyn=   40  leak=  4478  total=  4518 pJ
  V=0.25  f=    3.5 MHz  dyn=   63  leak=  2171  total=  2234 pJ
  V=0.30  f=    8.6 MHz  dyn=   90  leak=  1047  total=  1137 pJ
  V=0.49  f=  111.6 MHz  dyn=  240  leak=   132  total=   372 pJ
  V=0.50  f=  122.1 MHz  dyn=  250  leak=   123  total=   373 pJ
  V=0.70  f=  416.2 MHz  dyn=  490  leak=    50  total=   540 pJ
  V=1.00  f= 1000.0 MHz  dyn= 1000  leak=    30  total=  1030 pJ
```

출력에서 볼 것:

- OPP 표: 1 GHz → 200 MHz로 5배 느리게 하면 전압은 1.03 → 0.59 V, **사이클당 동적 에너지는 0.328배**(= (0.59/1.03)²)다. 같은 일의 동적 에너지가 1/3로 준다 — DVFS가 에너지를 아끼는 원리다. 전력은 0.066배지만 시간이 5배이므로 에너지는 0.33배다.
- 최소 에너지점(minimum energy point): 전압을 계속 내리면 동적 에너지는 V²로 줄지만, 주파수가 문턱 근처에서 지수적으로 떨어져 **한 사이클이 너무 길어지고, 그동안 새는 누설 에너지**가 폭증한다. 이 가정에서는 0.49 V(112 MHz)가 최소이고, 0.2 V까지 내리면 오히려 12배 비싸다.

```svg
<svg viewBox="0 0 680 345" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="300" x2="620" y2="300" stroke="currentColor"/><line x1="70" y1="300" x2="70" y2="40" stroke="currentColor"/><line x1="70.0" y1="300" x2="70.0" y2="304" stroke="currentColor"/><text x="70.0" y="318.0" font-size="12" text-anchor="middle">0.2</text><line x1="138.8" y1="300" x2="138.8" y2="304" stroke="currentColor"/><text x="138.8" y="318.0" font-size="12" text-anchor="middle">0.3</text><line x1="207.5" y1="300" x2="207.5" y2="304" stroke="currentColor"/><text x="207.5" y="318.0" font-size="12" text-anchor="middle">0.4</text><line x1="276.2" y1="300" x2="276.2" y2="304" stroke="currentColor"/><text x="276.2" y="318.0" font-size="12" text-anchor="middle">0.5</text>
<line x1="345.0" y1="300" x2="345.0" y2="304" stroke="currentColor"/><text x="345.0" y="318.0" font-size="12" text-anchor="middle">0.6</text><line x1="413.7" y1="300" x2="413.7" y2="304" stroke="currentColor"/><text x="413.7" y="318.0" font-size="12" text-anchor="middle">0.7</text><line x1="482.5" y1="300" x2="482.5" y2="304" stroke="currentColor"/><text x="482.5" y="318.0" font-size="12" text-anchor="middle">0.8</text><line x1="551.2" y1="300" x2="551.2" y2="304" stroke="currentColor"/><text x="551.2" y="318.0" font-size="12" text-anchor="middle">0.9</text>
<line x1="620.0" y1="300" x2="620.0" y2="304" stroke="currentColor"/><text x="620.0" y="318.0" font-size="12" text-anchor="middle">1.0</text><line x1="66" y1="300.0" x2="70" y2="300.0" stroke="currentColor"/><text x="62.0" y="304.0" font-size="12" text-anchor="end">0</text><line x1="66" y1="235.0" x2="70" y2="235.0" stroke="currentColor"/><text x="62.0" y="239.0" font-size="12" text-anchor="end">500</text><line x1="66" y1="170.0" x2="70" y2="170.0" stroke="currentColor"/><text x="62.0" y="174.0" font-size="12" text-anchor="end">1000</text><line x1="66" y1="105.0" x2="70" y2="105.0" stroke="currentColor"/><text x="62.0" y="109.0" font-size="12" text-anchor="end">1500</text>
<line x1="66" y1="40.0" x2="70" y2="40.0" stroke="currentColor"/><text x="62.0" y="44.0" font-size="12" text-anchor="end">2000</text><polyline points="83.8,293.7 97.5,292.5 111.2,291.2 125.0,289.8 138.8,288.3 152.5,286.7 166.2,285.0 180.0,283.2 193.8,281.2 207.5,279.2 221.2,277.1 235.0,274.8 248.8,272.5 262.5,270.0 276.2,267.5 290.0,264.8 303.8,262.1 317.5,259.2 331.2,256.3 345.0,253.2 358.7,250.0 372.5,246.8 386.2,243.4 400.0,239.9 413.7,236.3 427.5,232.6 441.2,228.8 455.0,224.9 468.8,220.9 482.5,216.8 496.2,212.6 510.0,208.3 523.7,203.9 537.5,199.3 551.2,194.7 565.0,190.0 578.8,185.1 592.5,180.2 606.2,175.1 620.0,170.0" fill="none" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="6 4"/>
<polyline points="111.2,56.4 125.0,118.3 138.8,163.9 152.5,197.1 166.2,221.4 180.0,239.1 193.8,252.1 207.5,261.7 221.2,268.9 235.0,274.3 248.8,278.4 262.5,281.6 276.2,284.0 290.0,286.0 303.8,287.6 317.5,288.8 331.2,289.9 345.0,290.7 358.7,291.5 372.5,292.1 386.2,292.6 400.0,293.1 413.7,293.4 427.5,293.8 441.2,294.1 455.0,294.3 468.8,294.6 482.5,294.8 496.2,295.0 510.0,295.2 523.7,295.3 537.5,295.5 551.2,295.6 565.0,295.7 578.8,295.8 592.5,295.9 606.2,296.0 620.0,296.1" fill="none" stroke="#e08a3c" stroke-width="2" stroke-dasharray="6 4"/>
<polyline points="111.2,47.6 125.0,108.1 138.8,152.2 152.5,183.8 166.2,206.4 180.0,222.3 193.8,233.4 207.5,240.9 221.2,246.0 235.0,249.1 248.8,250.9 262.5,251.6 276.2,251.5 290.0,250.8 303.8,249.7 317.5,248.1 331.2,246.1 345.0,243.9 358.7,241.5 372.5,238.8 386.2,236.0 400.0,232.9 413.7,229.7 427.5,226.4 441.2,222.9 455.0,219.3 468.8,215.5 482.5,211.6 496.2,207.6 510.0,203.4 523.7,199.2 537.5,194.8 551.2,190.3 565.0,185.7 578.8,180.9 592.5,176.1 606.2,171.2 620.0,166.1" fill="none" stroke="#d0564a" stroke-width="2.4"/><circle cx="269.4" cy="251.6" r="5" fill="#d0564a"/><text x="269.4" y="211.6" font-size="12" text-anchor="middle">최소 에너지점 0.49 V · 112 MHz</text>
<text x="345.0" y="332.0" font-size="13" text-anchor="middle">공급 전압 V (V)</text><text x="20.0" y="170.0" font-size="13" text-anchor="middle" transform="rotate(-90 20 170)">사이클당 에너지 (pJ)</text><line x1="400" y1="56" x2="425" y2="56" stroke="#4a7bd0" stroke-width="3" stroke-dasharray="6 4"/><text x="432.0" y="60.0" font-size="12" text-anchor="start">동적 C·V²</text><line x1="400" y1="74" x2="425" y2="74" stroke="#e08a3c" stroke-width="3" stroke-dasharray="6 4"/><text x="432.0" y="78.0" font-size="12" text-anchor="start">누설 P_leak / f(V)</text><line x1="400" y1="92" x2="425" y2="92" stroke="#d0564a" stroke-width="3"/><text x="432.0" y="96.0" font-size="12" text-anchor="start">합계</text>
<text x="430.0" y="125.0" font-size="12" text-anchor="start">← 문턱 근처: f가 지수적으로 줄어</text><text x="430.0" y="141.0" font-size="12" text-anchor="start">   누설 에너지/cycle 폭증</text>
</svg>
```

그림 5 — 사이클당 에너지 = 동적(C·V², 파랑 점선) + 누설(P_leak/f, 주황 점선). 합(빨강)은 문턱 근처에서 최소가 된다. 이 최소 에너지점보다 낮은 전압은 "느리기만 하고 에너지도 더 드는" 영역이다.

Ambiq류의 초저전력 MCU가 **subthreshold / near-threshold 동작**을 내세우는 이유가 이 그림이다. 느린 클럭(수십~수백 MHz)으로 충분한 always-on 작업이라면 최소 에너지점 근처에서 도는 것이 µA/MHz를 최소로 만든다. 반대로 고성능 코어는 최소 에너지점보다 훨씬 위에서 돈다 — 필요한 속도가 그것을 요구하기 때문이다.

### 5.4 governor — 누가 OPP를 고르나

**governor**는 부하를 보고 OPP를 고르는 정책이다. Linux cpufreq의 대표적인 것들:

| governor | 규칙 (개념) | 장점 | 약점 |
|---|---|---|---|
| performance | 항상 최대 OPP | 지연 최소, race-to-idle | idle 동안 전압이 높으면 누설, 에너지 큼 |
| powersave | 항상 최소 OPP | 순간 전력 최소 | 무거운 일을 제때 못 끝냄 |
| ondemand | 샘플링 주기마다 CPU 사용률을 보고, 임계값을 넘으면 최대로, 아니면 사용률에 비례해 낮춘다 | 단순 | 반응이 샘플링 주기만큼 늦고 뾰족한 부하에 과민 |
| conservative | ondemand와 비슷하지만 한 단계씩 올리고 내린다 | 부드러움 | 버스트에 느림 |
| schedutil | 스케줄러의 부하 추적값(util)을 직접 쓴다: next_f = 1.25 × f_max × util / max | 스케줄러와 통합, 빠름 | 과거 평균 기반이라 deadline을 모른다 |

(정확한 동작과 기본값은 커널 버전 문서를 확인할 것. schedutil의 1.25 계수는 "util 80 %에서 한 단계 위로"라는 의미로 커널 문서에 설명되어 있다.)

핵심은 마지막 칸이다: **범용 governor는 deadline을 모른다.** "이 추론은 50 ms 안에 끝나야 한다"는 정보는 애플리케이션에만 있다. 그래서 실제 ML 시스템은 hint를 준다 — Linux의 `uclamp`(작업별 최소/최대 util), Android의 performance hint API, Qualcomm QNN/SNPE 같은 런타임의 **performance profile**(burst, balanced, power saver 등 — 정확한 목록은 SDK 버전별 문서 확인)이 그것이다. NPU·DSP 쪽은 보통 자기 DCVS(dynamic clock and voltage scaling) 정책이 런타임 설정을 받아 동작한다.

### 5.5 AVS — 칩마다 전압을 깎는다

OPP 표의 전압은 **가장 느린 칩, 가장 나쁜 온도, 수명 말기**를 기준으로 잡은 값이다. 대부분의 칩은 그보다 빠르므로 전압이 남는다. **AVS(adaptive voltage scaling)** 는 이 여유를 칩별로 회수한다.

- **open-loop / 정적**: 생산 테스트에서 칩 속도를 재어(speed binning) fuse에 기록하고, 부팅 시 그 bin에 맞는 전압 표를 쓴다.
- **closed-loop / 동적**: 칩 안의 ring oscillator나 critical-path monitor가 "지금 이 전압에서 얼마나 여유가 있나"를 재고, PMIC 전압을 실시간으로 조금씩 내리거나 올린다. 온도·노화 변화도 따라간다. Qualcomm 칩의 CPR(Core Power Reduction)이 이런 종류로 알려져 있다(Linux 커널에 CPR 드라이버가 있다). 구체 동작은 칩 문서 확인.

손계산: 0.83 V OPP에서 AVS가 40 mV를 회수하면 동적 에너지는 (0.79/0.83)² = 0.906, 약 9 % 절약이다. 모델 최적화 없이 얻는 공짜 9 %다. 대신 Don의 sign-off 경험과 연결되는 함정이 있다: AVS가 켜진 칩은 **칩마다 전력이 다르다**. 전력 측정을 칩 몇 개로 할지, fast/slow corner 샘플을 어떻게 구할지를 계획해야 한다.

### 5.6 코드 — bursty ML 워크로드에서 governor 비교

50 ms마다 추론 요청이 오고, 70 %는 가벼운 모델(3 M cycle), 30 %는 무거운 모델(30 M cycle)이다. deadline은 다음 요청까지(50 ms). 여섯 정책을 비교한다: performance(= race-to-idle), powersave, ondemand, schedutil 흉내, 그리고 작업량과 deadline을 아는 두 oracle — **oracle-lowest**(deadline을 맞추는 가장 낮은 OPP)와 **oracle-Emin**(deadline을 맞추는 OPP 중 cycle당 에너지가 최소인 것). idle은 1 mW retention이라 가정한다.

```python
# bursty ML 워크로드에서 DVFS governor 정책 6종 비교: 에너지와 deadline miss (모든 파라미터는 가정)
import numpy as np
OPP = [(200e6, .59), (400e6, .72), (600e6, .83), (800e6, .93), (1000e6, 1.03)]  # ex4의 표
C, I_LK, P_IDLE = 0.3e-9, 10e-3, 1e-3                     # 스위칭 C, 누설, idle(retention) 1 mW
rng = np.random.default_rng(0)
T_END, PERIOD, DL = 20_000, 50, 50                         # ms 단위: 20 s, 50 ms마다 job, deadline 50 ms
jobs = [(t, 3e6 if rng.random() < 0.7 else 30e6) for t in range(0, T_END, PERIOD)]  # 경량 3M / 무거운 30M cycle

def run(policy, P_PLAT):                                   # P_PLAT: 활성 중 platform 전력
    q, E, miss, k, util_ewma, win_busy = [], 0.0, 0, 0, 0.0, 0
    oi = len(OPP) - 1; y = 0.5 ** (1 / 32)                 # PELT식 감쇠: 32 ms 반감기
    for t in range(T_END):
        while k < len(jobs) and jobs[k][0] == t: q.append([jobs[k][0] + DL, jobs[k][1]]); k += 1
        if policy.startswith("oracle") and q:              # 남은 cycle과 deadline을 안다
            need = max(sum(c for _, c in q[:i + 1]) / max(d - t, 1) * 1e3 for i, (d, _) in enumerate(q))
            ok = [i for i, (f, _) in enumerate(OPP) if f >= need] or [len(OPP) - 1]
            ecyc = lambda i: (C * OPP[i][1]**2 * OPP[i][0] + I_LK * OPP[i][1] + P_PLAT) / OPP[i][0]
            oi = ok[0] if policy == "oracle-lowest" else min(ok, key=ecyc)  # 최저 OPP vs 최소 J/cycle
        f, V = OPP[oi]
        busy = bool(q)
        if busy:
            q[0][1] -= f * 1e-3                             # 1 ms 동안 처리한 cycle
            E += (C * V**2 * f + I_LK * V + P_PLAT) * 1e-3
            if q[0][1] <= 0:
                d, _ = q.pop(0); miss += (t + 1 > d)
        else:
            E += P_IDLE * 1e-3
        win_busy += busy
        util_ewma = util_ewma * y + (1 - y) * busy * f / OPP[-1][0]
        if policy == "ondemand" and t % 10 == 9:           # 10 ms마다 샘플링
            u = win_busy / 10; win_busy = 0
            tgt = OPP[-1][0] if u > 0.8 else f * u / 0.8
            oi = next(i for i, (ff, _) in enumerate(OPP) if ff >= tgt or i == len(OPP) - 1)
        if policy == "schedutil":                          # next_f = 1.25 · f_max · util
            tgt = 1.25 * OPP[-1][0] * util_ewma
            oi = next(i for i, (ff, _) in enumerate(OPP) if ff >= tgt or i == len(OPP) - 1)
    return E, miss + len(q)                                # 끝까지 못 끝낸 job도 miss

print(f"jobs={len(jobs)}  heavy={sum(c > 1e7 for _, c in jobs)}")
for P_PLAT in (30e-3, 150e-3):
    print(f"-- platform power while active = {P_PLAT*1e3:.0f} mW")
    for pol in ("performance", "powersave", "ondemand", "schedutil", "oracle-lowest", "oracle-Emin"):
        saved = OPP[:]
        if pol == "powersave": OPP[:] = OPP[:1]
        E, m = run(pol, P_PLAT); OPP[:] = saved
        print(f"   {pol:13s} E={E*1e3:7.1f} mJ  avg P={E/20*1e3:6.2f} mW  deadline miss={m:3d}")
```

```text
jobs=400  heavy=143
-- platform power while active = 30 mW
   performance   E= 1829.7 mJ  avg P= 91.48 mW  deadline miss=  0
   powersave     E= 1127.9 mJ  avg P= 56.40 mW  deadline miss=396
   ondemand      E= 1766.9 mJ  avg P= 88.35 mW  deadline miss=  0
   schedutil     E= 1436.5 mJ  avg P= 71.83 mW  deadline miss=151
   oracle-lowest E= 1388.4 mJ  avg P= 69.42 mW  deadline miss=  0
   oracle-Emin   E= 1375.6 mJ  avg P= 68.78 mW  deadline miss=  0
-- platform power while active = 150 mW
   performance   E= 2437.0 mJ  avg P=121.85 mW  deadline miss=  0
   powersave     E= 3511.1 mJ  avg P=175.56 mW  deadline miss=396
   ondemand      E= 2757.3 mJ  avg P=137.86 mW  deadline miss=  0
   schedutil     E= 2850.8 mJ  avg P=142.54 mW  deadline miss=151
   oracle-lowest E= 2709.0 mJ  avg P=135.45 mW  deadline miss=  0
   oracle-Emin   E= 2384.3 mJ  avg P=119.21 mW  deadline miss=  0
```

출력에서 볼 것:

- **platform 30 mW**: 가장 낮은 feasible OPP(oracle-lowest)가 performance보다 24 % 싸다 — 동적 에너지 절감(V²)이 켜져 있는 시간 증가를 이긴다. oracle-Emin은 거의 같다.
- **platform 150 mW**: oracle-lowest가 performance보다 **비싸다**. 켜져 있는 동안 나가는 platform 전력(DRAM, interconnect, PLL — D7의 경우 C)이 크면 빨리 끝내고 자는 게 이긴다. oracle-Emin은 cycle당 에너지를 직접 비교하므로 두 경우 모두 최저다. **"가장 낮은 OPP" ≠ "가장 적은 에너지"** — 이것이 D7 race-to-idle 결론의 governor 버전이다.
- schedutil 흉내는 과거 평균으로 반응하기 때문에 무거운 요청이 낮은 OPP에서 시작해 151번 deadline을 놓친다(실제 schedutil에는 util 추정 보정, uclamp, boost 등이 있어 덜하다 — 이 시뮬레이션은 "반응형 정책은 deadline을 모른다"는 점만 보여 준다). powersave는 일을 다 못 끝낸다. 에너지가 작아 보이는 것은 일을 안 했기 때문이다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="290" x2="640" y2="290" stroke="currentColor"/><line x1="70" y1="290" x2="70" y2="40" stroke="currentColor"/><line x1="66" y1="290.0" x2="70" y2="290.0" stroke="currentColor"/><text x="62.0" y="294.0" font-size="12" text-anchor="end">0</text><line x1="66" y1="227.5" x2="70" y2="227.5" stroke="currentColor"/><text x="62.0" y="231.5" font-size="12" text-anchor="end">1000</text><line x1="66" y1="165.0" x2="70" y2="165.0" stroke="currentColor"/><text x="62.0" y="169.0" font-size="12" text-anchor="end">2000</text><line x1="66" y1="102.5" x2="70" y2="102.5" stroke="currentColor"/><text x="62.0" y="106.5" font-size="12" text-anchor="end">3000</text>
<line x1="66" y1="40.0" x2="70" y2="40.0" stroke="currentColor"/><text x="62.0" y="44.0" font-size="12" text-anchor="end">4000</text><rect x="87.5" y="175.6" width="26" height="114.4" fill="#4a7bd0" fill-opacity="0.8"/><rect x="117.5" y="137.7" width="26" height="152.3" fill="#e08a3c" fill-opacity="0.8"/><text x="117.5" y="308.0" font-size="12" text-anchor="middle">performance</text><rect x="182.5" y="219.5" width="26" height="70.5" fill="#4a7bd0" fill-opacity="0.8"/><rect x="212.5" y="70.6" width="26" height="219.4" fill="#e08a3c" fill-opacity="0.8"/><text x="212.5" y="308.0" font-size="12" text-anchor="middle">powersave</text>
<text x="212.5" y="62.6" font-size="12" text-anchor="middle">miss 396</text><rect x="277.5" y="179.6" width="26" height="110.4" fill="#4a7bd0" fill-opacity="0.8"/><rect x="307.5" y="117.7" width="26" height="172.3" fill="#e08a3c" fill-opacity="0.8"/><text x="307.5" y="308.0" font-size="12" text-anchor="middle">ondemand</text><rect x="372.5" y="200.2" width="26" height="89.8" fill="#4a7bd0" fill-opacity="0.8"/><rect x="402.5" y="111.8" width="26" height="178.2" fill="#e08a3c" fill-opacity="0.8"/><text x="402.5" y="308.0" font-size="12" text-anchor="middle">schedutil</text><text x="402.5" y="103.8" font-size="12" text-anchor="middle">miss 151</text>
<rect x="467.5" y="203.2" width="26" height="86.8" fill="#4a7bd0" fill-opacity="0.8"/><rect x="497.5" y="120.7" width="26" height="169.3" fill="#e08a3c" fill-opacity="0.8"/><text x="497.5" y="308.0" font-size="12" text-anchor="middle">oracle-lowest</text><rect x="562.5" y="204.0" width="26" height="86.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="592.5" y="141.0" width="26" height="149.0" fill="#e08a3c" fill-opacity="0.8"/><text x="592.5" y="308.0" font-size="12" text-anchor="middle">oracle-Emin</text><text x="20.0" y="165.0" font-size="13" text-anchor="middle" transform="rotate(-90 20 165)">20 s 동안 에너지 (mJ)</text>
<text x="355.0" y="326.0" font-size="13" text-anchor="middle">governor 정책 — miss 표시가 없는 정책은 deadline miss 0</text><rect x="440" y="48" width="14" height="14" fill="#4a7bd0" fill-opacity="0.8"/><text x="460.0" y="60.0" font-size="12" text-anchor="start">활성 중 platform 30 mW</text><rect x="440" y="68" width="14" height="14" fill="#e08a3c" fill-opacity="0.8"/><text x="460.0" y="80.0" font-size="12" text-anchor="start">활성 중 platform 150 mW</text>
</svg>
```

그림 6 — 20초 동안의 에너지(막대)와 deadline miss. platform 전력이 커지면 최적 정책이 "낮은 OPP"에서 "빨리 끝내기"로 바뀐다. 작업량과 deadline을 아는 정책(oracle-Emin)이 두 경우 모두 최저이면서 miss가 0이다 — 이것이 런타임이 governor에 hint를 줘야 하는 이유다.

### 5.7 DVFS가 에너지를 못 아끼는 경우

1. **memory-bound 구간**: DRAM 대기가 지배하면 코어 클럭을 올려도 빨리 안 끝나고, 내려도 느려지지 않는다. 이 구간은 코어 OPP를 **낮추는 게 공짜**다(반대로 올리는 건 순수 낭비). LLM decode(D5)가 전형이다. 여기서는 오히려 **메모리 쪽 DVFS**(DDR 주파수)가 중요하다.
2. **platform·누설 지배**: 위 예제의 150 mW 경우. 낮은 OPP는 켜져 있는 시간만 늘린다.
3. **전압 바닥(V_min)**: SRAM이 안정적으로 동작하는 최소 전압 아래로는 못 내린다. 그 아래 OPP는 주파수만 내려가서 에너지 이득이 없다.
4. **최소 에너지점 아래**: 5.3절 — 느리고 비싸기만 하다.
5. **전환 비용**: PLL 재lock, PMIC 전압 slew(수십 µs급이 흔하다), 캐시·TLB 영향. ms 단위로 OPP를 흔들면 전환 비용이 이득을 먹는다.
6. **열 제약**: 이미 throttling 중이면 governor의 선택이 thermal cap에 의해 잘린다(8절).

---

## 6. MCU 저전력 모드 — always-on의 세계

### 6.1 Cortex-M의 공통 부품

Arm Cortex-M 코어는 저전력 진입을 위한 몇 가지 공통 장치를 준다(아키텍처 매뉴얼에 정의된, 잘 알려진 기능이다).

| 장치 | 뜻 | 펌웨어 관점 |
|---|---|---|
| `WFI` | Wait For Interrupt — 인터럽트가 pending될 때까지 코어를 재운다 | idle 루프의 기본. PRIMASK로 마스크돼 있어도 pending 인터럽트가 있으면 깨어난다 |
| `WFE` | Wait For Event — event register가 set될 때까지 잔다 | `SEV`, 다른 코어의 이벤트, (SEVONPEND 시) pending 인터럽트로 깬다. spinlock 대기·멀티코어에 유용 |
| `SCR.SLEEPDEEP` (bit 2) | 이번 잠이 "deep sleep"임을 시스템에 알린다 | 실제로 무엇이 꺼지는지는 **벤더 PMU가 정한다** |
| `SCR.SLEEPONEXIT` (bit 1) | 인터럽트 핸들러에서 돌아올 때 thread 모드로 가지 않고 바로 다시 잔다 | 모든 일이 ISR에서 끝나는 설계에서 복귀·재진입 오버헤드 제거 |
| `SCR.SEVONPEND` (bit 4) | 비활성화된 인터럽트도 pending 시 event를 만든다 | WFE와 조합 |

SCR은 System Control Block의 System Control Register(주소 0xE000ED10)다. 코어는 "sleep"과 "deep sleep" 두 단계만 알고, 그보다 깊은 모드(standby, shutdown, system off)는 **벤더가 PMU 레지스터로 따로 정의**한다.

### 6.2 벤더마다 이름이 다르다

| 개념 단계 | STM32 계열 이름 | nRF52 계열 이름 | Ambiq Apollo 계열 이름 | 대략 유지되는 것 |
|---|---|---|---|---|
| 코어만 정지 | Sleep | System ON (CPU idle) | Sleep | 모든 주변장치·SRAM·클럭 |
| 고속 클럭 정지, 상태 유지 | Stop | System ON low-power | Deep sleep | SRAM(설정한 bank), 레지스터, RTC, 일부 주변장치 |
| 대부분 전원 차단 | Standby | — | (칩·설정에 따라) | RTC, backup 레지스터, 일부 SRAM 선택 |
| 거의 전부 차단 | Shutdown | System OFF | — | wake 핀만 (리셋처럼 깨어남) |

이 표는 **개념 대응일 뿐**이다. 같은 이름이어도 칩 세대마다 유지되는 것과 전류가 다르고, 세부 모드(Stop 0/1/2 등)가 더 있다. 반드시 해당 칩의 reference manual의 "low-power modes" 표를 본다. 설계할 때 확인할 질문은 네 가지다: 무엇이 유지되나(SRAM bank, 레지스터, 주변장치 설정), 무엇이 깨울 수 있나, 깨어나는 데 얼마나 걸리나, 그 상태의 전류는 온도별로 얼마인가.

### 6.3 무엇이 켜져 남고, 무엇이 깨우나

deep sleep에서 남는 전형적인 것:

- **RTC / 저속 오실레이터**(32.768 kHz 크리스털 또는 내부 RC): 시간 유지, 주기적 wake.
- **retention SRAM bank**: 모델 weight, 상태 변수, 링 버퍼. bank마다 µA 이하~수 µA 단위의 비용이 붙으므로 **필요한 bank만** 남긴다.
- **wake 소스**: GPIO 인터럽트(IMU INT 핀, 버튼), RTC alarm, 저전력 comparator(아날로그 임계값 — 예: 마이크 에너지 검출), 저전력 UART/I2C 주소 매칭, USB/충전기 연결, 센서 hub의 mailbox.
- **brown-out detector**: 전원이 너무 낮아지면 안전하게 리셋(7절). 끄면 전류는 줄지만 위험하다.

ML 기기에서 가장 중요한 wake 소스는 **센서 자체의 인터럽트**다. IMU의 wake-on-motion, FIFO watermark, 센서 내장 ML core의 결과 인터럽트, 마이크 쪽 음성 활동 감지(VAD) 인터럽트가 MCU를 깨운다(B7·G2). 즉 "always-on"의 첫 단은 MCU가 아니라 **센서 안**에 있는 경우가 많다.

### 6.4 코드 — sleep 진입 시퀀스 (호스트 mock)

실제 레지스터 대신 mock을 써서 호스트에서 흐름만 검증한다. 핵심 패턴은 "인터럽트를 막고 → 할 일이 없는지 확인하고 → 잔다"이다. 막지 않고 확인하면, 확인과 WFI 사이에 인터럽트가 와서 플래그를 세운 뒤 **다음 인터럽트까지 잠들어 버리는** race가 생긴다. Cortex-M은 PRIMASK로 마스크된 상태에서도 pending 인터럽트가 WFI를 깨우므로 이 패턴이 안전하다.

```c
/* Cortex-M sleep 진입 시퀀스 — SCB·PRIMASK·WFI를 호스트용 mock으로 바꿔 흐름만 검증 */
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

static struct { volatile uint32_t SCR; } SCB_mock;          /* 실제: SCB->SCR @ 0xE000ED10 */
#define SCB_SCR_SLEEPONEXIT (1u << 1)
#define SCB_SCR_SLEEPDEEP   (1u << 2)
static volatile bool imu_fifo_ready;                       /* ISR가 세우는 플래그 */
static int primask;
static void disable_irq(void) { primask = 1; }
static void enable_irq(void)  { primask = 0; }
static void dsb(void) {}
static void wfi(void) {                                    /* mock: 마스크돼 있어도 pending IRQ면 깨어남 */
    printf("  WFI  (%s)\n", (SCB_mock.SCR & SCB_SCR_SLEEPDEEP) ? "deep sleep" : "sleep");
    imu_fifo_ready = true;                                 /* 자는 동안 IMU watermark IRQ 도착 */
}

static void idle_once(bool allow_deep) {
    disable_irq();                                         /* ① 검사와 잠들기 사이의 race 차단 */
    if (imu_fifo_ready) { enable_irq(); return; }          /* ② 할 일이 있으면 자지 않는다 */
    if (allow_deep) SCB_mock.SCR |= SCB_SCR_SLEEPDEEP;     /* ③ 이번 잠의 깊이 선택 */
    else            SCB_mock.SCR &= ~SCB_SCR_SLEEPDEEP;
    dsb();                                                 /* ④ 이전 메모리 쓰기 완료 */
    wfi();                                                 /* ⑤ 잠 — pending IRQ로 깨어남 */
    enable_irq();                                          /* ⑥ 여기서 ISR 실행 */
}

int main(void) {
    for (int i = 0; i < 3; i++) {
        bool deep = (i != 1);                              /* 예: 오디오 DMA 중엔 deep 금지 */
        printf("iter %d: fifo_ready=%d\n", i, imu_fifo_ready);
        idle_once(deep);
        if (imu_fifo_ready) { printf("  -> drain FIFO, run model\n"); imu_fifo_ready = false; }
    }
    printf("SCR=0x%02x (SLEEPONEXIT=%u)\n", (unsigned)SCB_mock.SCR, (unsigned)((SCB_mock.SCR & SCB_SCR_SLEEPONEXIT) != 0));
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 ex9.c -o ex9 -lm && ./ex9
```

```text
iter 0: fifo_ready=0
  WFI  (deep sleep)
  -> drain FIFO, run model
iter 1: fifo_ready=0
  WFI  (sleep)
  -> drain FIFO, run model
iter 2: fifo_ready=0
  WFI  (deep sleep)
  -> drain FIFO, run model
SCR=0x04 (SLEEPONEXIT=0)
```

출력에서 볼 것: 매 반복에서 먼저 플래그를 보고, 없으면 그 반복의 조건(예: 오디오 DMA가 돌고 있으면 deep 금지)에 따라 SLEEPDEEP을 세우거나 지운 뒤 잔다. 실제 타깃에서는 `__disable_irq()`, `__DSB()`, `__WFI()`, `__enable_irq()`(CMSIS 이름)와 `SCB->SCR`를 쓰고, 벤더 PMU 레지스터로 deep sleep의 깊이(어느 SRAM bank를 유지할지 등)를 추가로 설정한다.

### 6.5 µA 예산 — IMU 제스처 인식

Ambiq급 초저전력 MCU를 가정한다. 아래 숫자는 **자릿수를 보기 위한 가정**이다(이런 칩들은 공개 자료에서 한 자릿수 µA/MHz급 active 효율과 µA급 deep sleep을 내세운다 — 정확한 값은 데이터시트의 전압·온도·코드 조건과 함께 확인).

| 항목 | 가정 값 | 비고 |
|---|---|---|
| MCU active | 6 µA/MHz × 48 MHz = 288 µA | 코드·메모리 위치에 따라 크게 달라짐 |
| MCU deep sleep | 2 µA | RTC + 64 KB SRAM retention |
| deep sleep 탈출 | 150 µs (run 전류) | 고속 클럭 안정화 포함 |
| IMU 가속도계 저전력 50 Hz | 25 µA | 센서마다 수 µA~수십 µA |
| IMU wake-on-motion만 | 12 µA | 움직임 감지 회로만 동작 |
| 모델 | 1 M cycle / 1초 창 = 20.8 ms | B7급 작은 1D-CNN |

세 가지 설계를 비교한다: (A) 샘플마다 MCU를 깨워 읽기, (B) IMU FIFO에 25샘플 모았다가 watermark 인터럽트로 한 번에 읽기, (C) B + 움직임이 없으면 IMU를 wake-on-motion 모드로 두고 MCU도 잔다(하루의 5 %만 움직인다고 가정).

```c
/* IMU 제스처 인식의 평균 전류 분해: polling vs FIFO batching vs wake-on-motion (전부 가정 값) */
#include <stdio.h>

#define I_SLEEP  2.0      /* µA, MCU deep sleep: RTC + 64 KB SRAM retention */
#define I_RUN    288.0    /* µA, 48 MHz × 6 µA/MHz */
#define T_WAKE   150e-6   /* s, deep sleep 탈출 + 고속 클럭 안정화 (run 전류로 가정) */
#define T_PRE    30e-6    /* s, 샘플 1개 전처리 */
#define T_INF    20.8e-3  /* s, 1 M cycle 모델 @ 48 MHz, 1초 창마다 1회 */
#define ODR      50.0     /* Hz */

typedef struct { const char *name; double wakes, t_spi, i_imu_on, i_imu_idle, moving; } imu_mode_t;

int main(void) {
    const imu_mode_t M[] = {
        { "A poll each sample", 50.0,  20e-6, 25.0, 25.0, 1.00 },  /* 항상 분석 */
        { "B FIFO wm=25",        2.0, 200e-6, 25.0, 25.0, 1.00 },
        { "C B + wake-on-motion",2.0, 200e-6, 25.0, 12.0, 0.05 },  /* 움직일 때만 5 % */
    };
    printf("%-22s %6s %6s %6s %6s %6s | %7s %7s\n", "mode", "sleep", "imu", "wake", "pre", "infer", "avg_uA", "days");
    for (unsigned i = 0; i < 3; i++) {
        const imu_mode_t *m = &M[i];
        double dI = I_RUN - I_SLEEP, a = m->moving;          /* 활성 시 추가 전류 */
        double wake  = a * dI * m->wakes * (T_WAKE + m->t_spi);
        double pre   = a * dI * ODR * T_PRE;
        double infer = a * dI * T_INF;
        double imu   = a * m->i_imu_on + (1 - a) * m->i_imu_idle;
        double tot   = I_SLEEP + imu + wake + pre + infer;
        printf("%-22s %6.1f %6.1f %6.1f %6.1f %6.1f | %7.1f %7.0f\n", m->name,
               I_SLEEP, imu, wake, pre, infer, tot, 150.0 / (tot * 1e-3) / 24.0);  /* 150 mAh */
    }
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 ex6.c -o ex6 -lm && ./ex6
```

```text
mode                    sleep    imu   wake    pre  infer |  avg_uA    days
A poll each sample        2.0   25.0    2.4    0.4    5.9 |    35.8     175
B FIFO wm=25              2.0   25.0    0.2    0.4    5.9 |    33.6     186
C B + wake-on-motion      2.0   12.6    0.0    0.0    0.3 |    15.0     417
```

출력에서 볼 것:

- A → B(FIFO batching): wake 오버헤드가 2.4 → 0.2 µA로 준다. 이 MCU는 깨어나는 비용이 작아서 전체로는 6 %만 준다. **깨어나는 대상이 MCU가 아니라 SoC였다면**(wake 한 번에 mW × ms) 이 차이가 수십 배가 된다 — E8의 sensor hub가 존재하는 이유다.
- 가장 큰 항목은 **IMU 자체(25 µA)** 다. 모델을 아무리 줄여도(추론 5.9 µA) 센서 설정이 예산을 지배한다.
- C(wake-on-motion): 움직이지 않는 95 % 동안 센서도 MCU도 바닥으로 내려가 평균 15 µA, 150 mAh로 1년 이상. **"언제 모델을 돌리지 않을지"를 정하는 게 모델 최적화보다 크다.**

---

## 7. PMIC와 전력 배달

### 7.1 레귤레이터 세 종류

**PMIC(power management IC)** 는 배터리 전압을 칩이 원하는 여러 레일로 바꾸고, 충전·보호·시퀀싱·fuel gauge까지 맡는 칩이다. 그 안의 레귤레이터는 세 종류다.

| 종류 | 원리 | 효율 | 장점 | 단점 |
|---|---|---|---|---|
| LDO (linear) | 직렬 트랜지스터가 남는 전압을 열로 태운다 | 최대 Vout/Vin | 잡음 적음, 작음, 빠른 응답, Iq 작게 가능 | 전압 차가 크면 효율 나쁨. dropout 필요 |
| buck (step-down 스위처) | 스위치 + 인덕터로 에너지를 잘라 옮긴다 | 보통 80~95 % | 전압 차가 커도 효율 좋음 | 리플·EMI, 인덕터, 저부하에서 효율 저하 |
| boost / buck-boost | 인덕터로 전압을 올리거나 올리고 내린다 | 보통 80~90 % | 배터리보다 높은 전압(또는 배터리 전압 전 범위에서 일정 출력) | 부품·효율 비용 |

LDO 효율은 정의상 간단하다.

```
η_LDO = (Vout · Iout) / (Vin · (Iout + Iq))  ≤  Vout / Vin
```

말로 하면: LDO는 입력 전류를 그대로 흘려보내고 전압 차만큼을 열로 버린다. 3.7 V 배터리에서 1.8 V를 LDO로 만들면 최대 48.6 %, 절반 이상이 열이다. 4.2 V(만충)에서는 43 %, 3.0 V(방전 말기)에서는 60 %. 반대로 **buck 출력(예: 2.0 V)에서 LDO로 1.8 V를 만들면** 1.8/2.0 = 90 %라서, 잡음에 민감한 아날로그·RF 레일은 "buck → LDO" 2단으로 만드는 일이 흔하다.

**dropout**: LDO는 Vin이 Vout보다 최소 dropout 전압(수십~수백 mV)만큼 높아야 레귤레이션한다. 3.3 V 레일을 배터리에서 LDO로 바로 만들면, 배터리가 3.5 V 밑으로 내려가는 방전 말기에 레일이 같이 처진다. 이것이 buck-boost를 쓰는 이유 중 하나다.

### 7.2 저부하 효율 — PWM vs PFM

buck은 두 가지 방식으로 스위칭한다.

- **PWM(고정 주파수)**: 매 주기 스위칭한다. 중·고부하에서 효율 좋고 리플이 작고 주파수가 예측 가능(RF 간섭 관리가 쉽다). 하지만 스위칭 손실과 구동 회로의 전류가 **부하와 무관하게** 나가서 저부하 효율이 무너진다.
- **PFM / burst / skip 모드**: 출력이 떨어졌을 때만 펄스를 몇 개 보내고 나머지 시간은 쉰다. 저부하에서 효율이 높지만, 리플이 크고 스위칭 주파수가 부하에 따라 움직인다(오디오·RF에 잡음 걱정).

대부분의 현대 PMIC buck은 부하에 따라 자동으로 모드를 바꾸고(auto PFM/PWM), 펌웨어가 강제로 PWM을 걸 수도 있다(예: RF 수신 중에는 잡음 때문에 forced PWM). 저부하에서 중요한 숫자는 **Iq(quiescent current)** — 레귤레이터 자신이 먹는 전류다. always-on용 nano-power buck은 Iq가 수십~수백 nA급인 제품이 있다.

손실 모델로 부하별 효율을 계산한다(모든 파라미터는 가정).

```python
# 레귤레이터 효율: LDO = Vout/Vin (+Iq), buck PWM vs PFM의 부하별 효율 (손실 모델 파라미터는 가정)
import numpy as np
Vin, Vout, R = 3.7, 1.8, 0.3                        # 배터리 3.7 V → 1.8 V 레일, 도통 저항 0.3 Ω
def eta_ldo(I, Iq=1e-6):   return Vout * I / (Vin * (I + Iq))
def eta_pwm(I, Iq=50e-6, P_sw=3e-3):                # 고정 주파수 스위칭 손실이 부하와 무관
    Po = Vout * I; return Po / (Po + Vin * Iq + I**2 * R + P_sw)
def eta_pfm(I, Iq=0.5e-6, k=0.06):                  # 필요할 때만 펄스: 스위칭 손실이 부하에 비례
    Po = Vout * I; return Po / (Po + Vin * Iq + I**2 * R + k * Po)
print("  load     LDO    buck-PWM  buck-PFM")
for I in (1e-6, 10e-6, 100e-6, 1e-3, 10e-3, 100e-3, 500e-3):
    print(f"{I*1e3:9.3f} mA  {eta_ldo(I)*100:5.1f} %  {eta_pwm(I)*100:6.1f} %  {eta_pfm(I)*100:6.1f} %")
print("\nLDO 1.8 V out vs battery voltage (ideal, Iq=0):",
      ", ".join(f"{v:.1f} V→{Vout/v*100:.0f} %" for v in (4.2, 3.7, 3.3, 3.0)))
# always-on 20 µA 부하가 하루 동안 배터리에서 가져가는 에너지 (J)
for name, fn in (("LDO", eta_ldo), ("buck-PWM", eta_pwm), ("buck-PFM", eta_pfm)):
    print(f"20 µA always-on via {name:8s}: battery draw {Vout*20e-6/fn(20e-6)*1e6:7.1f} µW "
          f"= {Vout*20e-6/fn(20e-6)*86400:6.2f} J/day")
```

```text
  load     LDO    buck-PWM  buck-PFM
    0.001 mA   24.3 %     0.1 %    47.9 %
    0.010 mA   44.2 %     0.6 %    86.0 %
    0.100 mA   48.2 %     5.3 %    93.4 %
    1.000 mA   48.6 %    36.1 %    94.2 %
   10.000 mA   48.6 %    84.8 %    94.2 %
  100.000 mA   48.6 %    96.7 %    92.9 %
  500.000 mA   48.6 %    92.0 %    87.5 %

LDO 1.8 V out vs battery voltage (ideal, Iq=0): 4.2 V→43 %, 3.7 V→49 %, 3.3 V→55 %, 3.0 V→60 %
20 µA always-on via LDO     : battery draw    77.7 µW =   6.71 J/day
20 µA always-on via buck-PWM: battery draw  3221.0 µW = 278.29 J/day
20 µA always-on via buck-PFM: battery draw    40.0 µW =   3.46 J/day
```

출력에서 볼 것:

- **20 µA always-on 부하**에서 PWM 고정 buck은 효율 1 % 미만 — 배터리에서 3.2 mW를 끌어 하루 278 J(1.11 Wh 배터리 = 3996 J의 7 %)를 낭비한다. PFM은 40 µW, LDO는 78 µW다. **µW 부하의 효율은 레귤레이터의 Iq가 정한다.**
- 1 mA 이하에서는 LDO조차 PWM buck보다 낫다. 10 mA 이상에서는 buck이 LDO를 크게 이긴다. 100 mA 이상에서는 PWM이 PFM보다 낫다.
- LDO의 효율은 배터리 전압에 따라 43~60 %로 움직인다 — 배터리 수명 계산에서 "레일 전류 × 배터리 전압"을 쓰면 안 되는 이유다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="290" x2="620" y2="290" stroke="currentColor"/><line x1="70" y1="290" x2="70" y2="40" stroke="currentColor"/><line x1="70.0" y1="290" x2="70.0" y2="294" stroke="currentColor"/><text x="70.0" y="308.0" font-size="12" text-anchor="middle">1 µA</text><line x1="166.5" y1="290" x2="166.5" y2="294" stroke="currentColor"/><text x="166.5" y="308.0" font-size="12" text-anchor="middle">10 µA</text><line x1="263.0" y1="290" x2="263.0" y2="294" stroke="currentColor"/><text x="263.0" y="308.0" font-size="12" text-anchor="middle">100 µA</text><line x1="359.5" y1="290" x2="359.5" y2="294" stroke="currentColor"/><text x="359.5" y="308.0" font-size="12" text-anchor="middle">1 mA</text>
<line x1="456.0" y1="290" x2="456.0" y2="294" stroke="currentColor"/><text x="456.0" y="308.0" font-size="12" text-anchor="middle">10 mA</text><line x1="552.5" y1="290" x2="552.5" y2="294" stroke="currentColor"/><text x="552.5" y="308.0" font-size="12" text-anchor="middle">100 mA</text><line x1="66" y1="290.0" x2="70" y2="290.0" stroke="currentColor"/><text x="62.0" y="294.0" font-size="12" text-anchor="end">0 %</text><line x1="66" y1="240.0" x2="70" y2="240.0" stroke="currentColor"/><text x="62.0" y="244.0" font-size="12" text-anchor="end">20 %</text><line x1="66" y1="190.0" x2="70" y2="190.0" stroke="currentColor"/><text x="62.0" y="194.0" font-size="12" text-anchor="end">40 %</text>
<line x1="66" y1="140.0" x2="70" y2="140.0" stroke="currentColor"/><text x="62.0" y="144.0" font-size="12" text-anchor="end">60 %</text><line x1="66" y1="90.0" x2="70" y2="90.0" stroke="currentColor"/><text x="62.0" y="94.0" font-size="12" text-anchor="end">80 %</text><line x1="66" y1="40.0" x2="70" y2="40.0" stroke="currentColor"/><text x="62.0" y="44.0" font-size="12" text-anchor="end">100 %</text>
<polyline points="70.0,229.2 81.2,221.1 92.4,213.3 103.7,206.0 114.9,199.4 126.1,193.6 137.3,188.7 148.6,184.6 159.8,181.2 171.0,178.4 182.2,176.2 193.5,174.5 204.7,173.1 215.9,172.0 227.1,171.2 238.4,170.5 249.6,170.0 260.8,169.6 272.0,169.4 283.3,169.1 294.5,168.9 305.7,168.8 316.9,168.7 328.2,168.6 339.4,168.6 350.6,168.5 361.8,168.5 373.1,168.5 384.3,168.4 395.5,168.4 406.7,168.4 418.0,168.4 429.2,168.4 440.4,168.4 451.6,168.4 462.9,168.4 474.1,168.4 485.3,168.4 496.5,168.4 507.8,168.4 519.0,168.4 530.2,168.4 541.4,168.4 552.7,168.4 563.9,168.4 575.1,168.4 586.3,168.4 597.6,168.4 608.8,168.4 620.0,168.4" fill="none" stroke="#3f9a6b" stroke-width="2.2"/>
<polyline points="70.0,289.9 81.2,289.8 92.4,289.8 103.7,289.7 114.9,289.6 126.1,289.5 137.3,289.3 148.6,289.1 159.8,288.8 171.0,288.4 182.2,288.0 193.5,287.3 204.7,286.5 215.9,285.5 227.1,284.1 238.4,282.4 249.6,280.1 260.8,277.3 272.0,273.6 283.3,269.0 294.5,263.3 305.7,256.2 316.9,247.5 328.2,237.3 339.4,225.3 350.6,211.6 361.8,196.5 373.1,180.4 384.3,163.8 395.5,147.2 406.7,131.2 418.0,116.3 429.2,103.0 440.4,91.3 451.6,81.3 462.9,73.0 474.1,66.3 485.3,60.9 496.5,56.7 507.8,53.5 519.0,51.2 530.2,49.6 541.4,48.7 552.7,48.3 563.9,48.5 575.1,49.4 586.3,50.8 597.6,53.0 608.8,56.0 620.0,60.0" fill="none" stroke="#d0564a" stroke-width="2.2"/>
<polyline points="70.0,170.3 81.2,154.6 92.4,139.5 103.7,125.6 114.9,113.0 126.1,101.9 137.3,92.5 148.6,84.7 159.8,78.2 171.0,73.0 182.2,68.9 193.5,65.6 204.7,63.0 215.9,61.0 227.1,59.4 238.4,58.2 249.6,57.3 260.8,56.5 272.0,56.0 283.3,55.6 294.5,55.2 305.7,55.0 316.9,54.8 328.2,54.7 339.4,54.5 350.6,54.5 361.8,54.4 373.1,54.4 384.3,54.3 395.5,54.3 406.7,54.3 418.0,54.4 429.2,54.4 440.4,54.4 451.6,54.5 462.9,54.6 474.1,54.7 485.3,54.9 496.5,55.1 507.8,55.4 519.0,55.8 530.2,56.3 541.4,57.0 552.7,57.8 563.9,58.9 575.1,60.3 586.3,62.2 597.6,64.5 608.8,67.5 620.0,71.3" fill="none" stroke="#4a7bd0" stroke-width="2.2"/>
<rect x="70.0" y="40" width="193.0" height="250" fill="#888" fill-opacity="0.12"/><text x="166.5" y="56.0" font-size="12" text-anchor="middle">always-on 영역</text><text x="345.0" y="326.0" font-size="13" text-anchor="middle">부하 전류 (log) — 3.7 V 배터리 → 1.8 V 레일</text><text x="20.0" y="165.0" font-size="13" text-anchor="middle" transform="rotate(-90 20 165)">효율</text><line x1="330" y1="210" x2="355" y2="210" stroke="#4a7bd0" stroke-width="3"/><text x="362.0" y="214.0" font-size="12" text-anchor="start">buck PFM (저부하 모드)</text><line x1="330" y1="228" x2="355" y2="228" stroke="#d0564a" stroke-width="3"/>
<text x="362.0" y="232.0" font-size="12" text-anchor="start">buck PWM 고정 (Iq 50 µA + 3 mW 스위칭)</text><line x1="330" y1="246" x2="355" y2="246" stroke="#3f9a6b" stroke-width="3"/><text x="362.0" y="250.0" font-size="12" text-anchor="start">LDO (최대 Vout/Vin = 49 %)</text>
</svg>
```

그림 7 — 3.7 V → 1.8 V 변환 효율(가정 손실 모델). always-on 영역(회색)에서는 PFM buck이 압도적이고, PWM 고정 buck은 거의 모든 에너지를 스스로 태운다. 무거운 부하에서는 PWM이 가장 좋다. 실제 PMIC는 이 두 모드를 자동 전환한다.

### 7.3 레일, 배터리, brown-out

웨어러블급 기기의 전형적인 레일(가정):

| 레일 | 전압 자릿수 | 부하 | 특징 |
|---|---|---|---|
| SoC CPU/NPU core | 0.5~1.1 V (DVFS) | 수 mA ~ 1 A 이상 | buck, 빠른 전압 변경, 큰 load step |
| LPDDR (VDD1/VDD2/VDDQ) | 1.8 / 1.05~1.1 / 0.5~0.6 V 급 (세대별로 다름) | 수십~수백 mA | 여러 레일, self-refresh 중에도 일부 유지 |
| I/O | 1.8 V | 작음 | 센서·플래시 인터페이스 |
| AON / MCU | 0.7~1.8 V | µA ~ mA | 저 Iq buck 또는 LDO, 절대 안 꺼짐 |
| RF / 오디오 아날로그 | 1.x V | 순간 수십 mA | LDO(잡음), 또는 forced PWM |
| 햅틱·디스플레이 백라이트 | 배터리 또는 boost | 순간 큼 | 큰 피크 전류 |

**Li-ion 배터리** 전압은 만충 4.2 V(고전압 셀은 4.35~4.4 V)에서 방전 말기 3.0 V 근처까지 내려간다. 모든 레귤레이터는 이 전 범위에서 동작해야 한다.

**brown-out**: 배터리 내부 저항(R_int) 때문에 순간 부하가 크면 단자 전압이 떨어진다. 손계산(가정): 저온에서 R_int = 0.3 Ω, 개방 전압 3.5 V인 배터리에서 NPU burst + 무선 TX + 햅틱이 동시에 1.5 A를 뽑으면 0.3 × 1.5 = 0.45 V가 떨어져 3.05 V — BOR(brown-out reset) 임계가 3.0 V라면 경계선이다. 그래서

- PMIC와 SoC에는 **UVLO(under-voltage lockout), POR(power-on reset), BOR** 이 있다. BOR가 걸리면 기기가 리셋되고, 그 순간 쓰던 flash가 깨질 수 있다.
- 시스템 소프트웨어는 **피크 전류 관리**를 한다: 배터리 잔량이 낮거나 추울 때 NPU 최대 OPP 금지, 무선 TX와 무거운 추론을 동시에 하지 않도록 스케줄링. Don이 margin sign-off에서 본 "저온·저전압 corner의 동시 부하" 시나리오 그대로다.
- 레귤레이터 쪽에서는 NPU가 idle → full로 바뀌는 **load step**에 레일이 순간적으로 처진다(droop). DVFS 드라이버가 전압을 먼저 올리고 주파수를 나중에 올리는 순서(내릴 때는 반대)를 지키는 이유이고, 일부 칩은 droop 검출 시 클럭을 순간적으로 늦추는 회로를 둔다.

### 7.4 왜 µW 효율이 중요한가

always-on 예산이 수십 µW라면, 레귤레이터 Iq 1 µA × 3.7 V = 3.7 µW가 곧 예산의 10 %다. 7.2절의 계산처럼 레귤레이터 모드 하나를 잘못 두면 always-on 전력이 수십 배가 된다. 그래서 always-on 레일은 (1) 저 Iq 레귤레이터를 쓰고, (2) 가능하면 전압을 낮게 두고, (3) sleep 중 PMIC 자신도 저전력 모드로 보내며(많은 PMIC가 sleep 모드 레지스터를 가진다), (4) 측정할 때 **PMIC 입력(배터리)** 에서 잰다 — 레일에서 재면 레귤레이터 손실이 빠진다.

---

## 8. 열 하드웨어와 관리

### 8.1 센서

- **온칩 온도 센서**: 다이오드/BJT의 온도 의존 전압이나 ring oscillator 주파수를 ADC로 읽는다. 큰 SoC는 CPU cluster, GPU, NPU, 모뎀 근처 등 **핫스팟마다 여러 개**를 둔다. Qualcomm 칩의 온도 센서 블록은 TSENS라고 불린다(Linux thermal 쪽에 tsens 드라이버가 있다). 센서는 임계값 인터럽트를 가져서, 펌웨어가 폴링하지 않아도 "상한을 넘었다"를 알려 줄 수 있다.
- **보드 센서**: NTC 서미스터를 배터리 근처(충전 안전), 케이스 안쪽, 피부 접촉면 근처에 둔다. PMIC나 fuel gauge의 ADC로 읽는 경우가 많다.
- **배터리 온도**: 충전은 온도에 따라 전류·전압을 줄이도록 되어 있다(JEITA 가이드라인이 대표적). 추운 곳에서 충전 전류가 줄고, 뜨거울 때 충전이 멈추는 이유다.

### 8.2 thermal zone, trip point, cooling device

Linux thermal framework는 열 관리를 세 부품으로 나눈다.

| 부품 | 뜻 | 예 |
|---|---|---|
| thermal zone | 온도 하나를 대표하는 영역 (센서 + 정책) | cpu-cluster0, npu, skin, battery |
| trip point | 행동을 시작하는 온도 | passive(throttle 시작), active(팬 등), hot, critical(강제 종료) |
| cooling device | 온도를 낮추는 수단 | CPU/NPU 주파수 상한, GPU 상한, 충전 전류 제한, 디스플레이 밝기 |
| governor | trip과 온도를 보고 cooling 단계를 고르는 정책 | step_wise(한 단계씩), bang_bang, power_allocator |

**power_allocator**(Arm의 IPA — Intelligent Power Allocation에서 온 governor)는 이 노트의 8.5절과 같은 발상이다: **PID 제어기**가 온도 오차로부터 전체 전력 예산을 정하고, 그 예산을 CPU·GPU 등 "actor"들에게 요구량에 비례해 나눈다. 설정 항목에 `sustainable-power`(지속 가능 전력)가 있다 — D7 7.2절의 ΔT/R_th 그 값이다.

Android는 그 위에 **Thermal HAL**을 두어 온도 타입(CPU, GPU, BATTERY, SKIN 등)과 throttling 심각도(NONE, LIGHT, MODERATE, SEVERE, CRITICAL, EMERGENCY, SHUTDOWN)를 앱에 알려 준다. 앱은 `PowerManager`의 thermal status 리스너와 thermal headroom 조회로 "곧 throttle될 것"을 미리 알 수 있다. ML 앱이 이 신호를 받아 **모델을 작은 것으로 바꾸거나 프레임 레이트를 낮추는 것**이 가장 우아한 cooling device다.

### 8.3 피부 온도 — 재지 못하는 것을 추정하기

사용자가 느끼는 것은 **피부에 닿는 케이스 온도**인데, 그 자리에 센서를 두기는 어렵다. 그래서 여러 센서(칩 온도, 배터리, 보드 서미스터)와 전력 정보를 조합한 모델로 피부 온도를 **추정**한다. 이것을 흔히 **virtual sensor**(가상 센서)라고 부른다. 구현은 제조사마다 다르고 공개된 세부는 적다 — 선형 가중합부터 열 RC 모델 기반 observer까지 다양하다고 알려져 있다. 핵심 성질은:

- 피부 온도는 칩 온도보다 **훨씬 느리게**(시정수 수십 초~수 분) 변한다. 칩 온도는 추론 한 번에도 튀지만 피부는 평균 전력을 따라간다.
- 그래서 제어는 두 층이다: **빠른 층**(칩 접합 온도 보호, 수 ms~수백 ms, 하드웨어 가까이)과 **느린 층**(피부 온도, 수 초 주기, 소프트웨어 정책).
- 목표 온도는 규격(D7 7.1절 — IEC 62368-1 등)과 사내 기준에서 오고, 여유를 둔다.

### 8.4 웨어러블의 열 경로와 열 퍼뜨리기

작은 기기의 R_th는 대부분 **표면에서 공기로** 가는 경로가 정한다.

```
R_th(표면 → 주변) ≈ 1 / (h · A)
  h: 대류 + 복사 열전달 계수. 자연 대류 + 복사 합쳐 대략 10 W/(m²·K) 자릿수 (가정)
  A: 열을 버리는 표면적
```

손계산: 손목 기기가 앞뒷면 합쳐 4 cm × 4 cm × 2 = 32 cm² = 3.2 × 10⁻³ m²를 쓴다면 R_th ≈ 1 / (10 × 0.0032) ≈ 31 K/W — D7에서 가정한 30 K/W와 같은 자릿수다. 이어버드처럼 표면적이 수 cm²면 R_th는 수백 K/W가 되어 지속 전력이 수십 mW로 떨어진다.

말로 하면: **총 열을 버리는 능력은 표면적이 정한다.** 그라파이트 시트, 금속 프레임, 열전도 패드 같은 heat spreading은 총 방열 능력을 크게 바꾸지 못하지만, 칩 위 한 점에 몰린 열을 **표면 전체로 퍼뜨려 핫스팟 온도를 낮춘다** — 피부 한계는 평균이 아니라 가장 뜨거운 점에 걸리므로 이것이 사실상 지속 전력을 올린다. 또 배터리는 열에 약하므로(수명 저하) 열원과 배터리를 떨어뜨려 배치한다.

### 8.5 코드 — PI 제어기로 power cap 정하기

D7 7.3절은 1차 모델 + 단순 on/off throttling이었다. 여기서는 **2-node 모델**(작은 열용량의 die + 큰 열용량의 케이스/피부)을 쓰고, **제어기 설계**에 집중한다. 앱은 1.5 W를 원한다(예: 긴 온디바이스 생성). 피부 한계 41 °C, 제어 목표 40 °C. 세 제어를 비교한다: 없음, hysteresis(40.5 °C에서 0.2 W로, 39.5 °C에서 1.5 W로 복귀), **PI + anti-windup**(1초마다 cap 갱신).

PI 제어기는 Don에게 익숙한 형태다:

```
e[k] = T_set − T_meas[k]
P_cap[k] = clip( P_ff + Kp·e[k] + Ki·Σe , P_min, P_max )
anti-windup: P_cap가 포화(clip)된 동안은 적분을 멈춘다 (아니면 포화 동안 쌓인 적분이 풀리면서 overshoot)
```

말로 하면: 목표보다 차가우면 전력을 더 주고, 뜨거우면 줄인다. 비례항은 지금의 오차에, 적분항은 "지속 가능 전력"이 정확히 얼마인지 모르는 것(모델 오차)을 천천히 보정한다. P_ff(feed-forward)는 지속 가능 전력의 추정치다.

```python
# 2-node 열 모델(die + 케이스/피부) 위의 power-cap 제어: 없음 vs hysteresis vs PI (파라미터는 가정)
import numpy as np
Cd, Rds, Cs, Rsa, Ta = 0.5, 5.0, 8.0, 25.0, 32.0     # J/K, K/W: die→skin 5 K/W, skin→주변 25 K/W (τ≈200 s)
P_want, T_lim, T_set = 1.5, 41.0, 40.0              # 앱이 원하는 전력, 피부 한계, 제어 목표
dt, n = 0.1, 9000                                   # 900 s
rng = np.random.default_rng(1)

def sim(ctrl, Kp=0.4, Ki=0.004):
    Td = Ts = Ta; P = P_want; integ = 0.0; hot = False
    log = np.empty((n, 3))
    for k in range(n):
        if k % 10 == 0:                              # 1 s마다 센서 읽고 cap 갱신
            meas = Ts + rng.normal(0, 0.05)          # 피부 쪽 thermistor (또는 virtual sensor)
            e = T_set - meas
            if ctrl == "hyst":
                hot = meas > T_set + 0.5 or (hot and meas > T_set - 0.5)
                P = 0.2 if hot else P_want
            elif ctrl == "pi":
                u = Kp * e + Ki * integ
                P = float(np.clip(u + 0.3, 0.1, P_want))    # 0.3 W feed-forward ≈ 지속 가능 전력 추정
                if 0.1 < u + 0.3 < P_want or e * u < 0:    # anti-windup: 포화 중엔 적분 멈춤
                    integ += e * 1.0
        Td += dt / Cd * (P - (Td - Ts) / Rds)
        Ts += dt / Cs * ((Td - Ts) / Rds - (Ts - Ta) / Rsa)
        log[k] = (Td, Ts, P)
    return log

for c in ("none", "hyst", "pi"):
    L = sim(c); tail = L[-3000:]                     # 마지막 300 s = 정상 상태
    print(f"{c:5s} max_skin={L[:,1].max():5.2f} °C  t>{T_lim:.0f}°C={np.sum(L[:,1]>T_lim)*dt:5.1f} s  "
          f"steady P={tail[:,2].mean():.3f} W  P std={tail[:,2].std():.3f} W  max_die={L[:,0].max():5.1f} °C")
print(f"sustainable power ≈ (T_set−Ta)/Rsa = {(T_set-Ta)/Rsa:.3f} W")
```

```text
none  max_skin=68.96 °C  t>41°C=839.6 s  steady P=1.500 W  P std=0.000 W  max_die= 76.4 °C
hyst  max_skin=40.92 °C  t>41°C=  0.0 s  steady P=0.330 W  P std=0.390 W  max_die= 47.8 °C
pi    max_skin=40.27 °C  t>41°C=  0.0 s  steady P=0.320 W  P std=0.019 W  max_die= 44.3 °C
sustainable power ≈ (T_set−Ta)/Rsa = 0.320 W
```

출력에서 볼 것:

- 제어 없음: 피부 69 °C — 당연히 불가.
- hysteresis: 한계를 겨우 지키지만(최대 40.92 °C) 전력이 0.2 W와 1.5 W 사이를 오가서 **전력 표준편차 0.39 W** — 사용자 입장에서는 몇 초 빠르다가 몇십 초 느린 "울컥거림"이다.
- PI: 최대 40.27 °C, 정상 상태 전력 0.320 W로 **이론적 지속 가능 전력 (40 − 32)/25 = 0.320 W에 정확히 수렴**, 흔들림 0.019 W. 같은 평균 성능을 부드럽게 준다.
- 두 제어 모두 결국 0.32 W 근처로 간다 — **어떤 제어기도 열 물리(R_th)가 정한 지속 전력을 바꾸지 못한다.** 제어기가 바꾸는 것은 "얼마나 부드럽게, 얼마나 한계에 가까이" 뿐이다.

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="170" x2="640" y2="170" stroke="currentColor"/><line x1="70" y1="170" x2="70" y2="30" stroke="currentColor"/><line x1="70" y1="330" x2="640" y2="330" stroke="currentColor"/><line x1="70" y1="330" x2="70" y2="215" stroke="currentColor"/><line x1="66" y1="170.0" x2="70" y2="170.0" stroke="currentColor"/><text x="62.0" y="174.0" font-size="12" text-anchor="end">32</text><line x1="66" y1="123.3" x2="70" y2="123.3" stroke="currentColor"/><text x="62.0" y="127.3" font-size="12" text-anchor="end">36</text><line x1="66" y1="76.7" x2="70" y2="76.7" stroke="currentColor"/><text x="62.0" y="80.7" font-size="12" text-anchor="end">40</text>
<line x1="66" y1="30.0" x2="70" y2="30.0" stroke="currentColor"/><text x="62.0" y="34.0" font-size="12" text-anchor="end">44</text><line x1="66" y1="330.0" x2="70" y2="330.0" stroke="currentColor"/><text x="62.0" y="334.0" font-size="12" text-anchor="end">0.0</text><line x1="66" y1="294.1" x2="70" y2="294.1" stroke="currentColor"/><text x="62.0" y="298.1" font-size="12" text-anchor="end">0.5</text><line x1="66" y1="258.1" x2="70" y2="258.1" stroke="currentColor"/><text x="62.0" y="262.1" font-size="12" text-anchor="end">1.0</text><line x1="66" y1="222.2" x2="70" y2="222.2" stroke="currentColor"/><text x="62.0" y="226.2" font-size="12" text-anchor="end">1.5</text>
<line x1="70.0" y1="330" x2="70.0" y2="334" stroke="currentColor"/><text x="70.0" y="348.0" font-size="12" text-anchor="middle">0 s</text><line x1="260.0" y1="330" x2="260.0" y2="334" stroke="currentColor"/><text x="260.0" y="348.0" font-size="12" text-anchor="middle">300 s</text><line x1="450.0" y1="330" x2="450.0" y2="334" stroke="currentColor"/><text x="450.0" y="348.0" font-size="12" text-anchor="middle">600 s</text><line x1="640.0" y1="330" x2="640.0" y2="334" stroke="currentColor"/><text x="640.0" y="348.0" font-size="12" text-anchor="middle">900 s</text><line x1="70" y1="65.0" x2="640" y2="65.0" stroke="#d0564a" stroke-dasharray="5 4"/>
<text x="636.0" y="60.0" font-size="12" text-anchor="end">한계 41 °C</text>
<polyline points="70.0,170.0 72.5,165.4 75.1,158.0 77.6,150.2 80.1,142.4 82.7,134.7 85.2,127.2 87.7,119.8 90.3,112.6 92.8,105.5 95.3,98.5 97.9,91.7 100.4,85.0 102.9,78.4 105.5,71.9 108.0,68.1 110.5,68.1 113.1,68.8 115.6,69.5 118.1,70.3 120.7,71.1 123.2,71.8 125.7,72.6 128.3,73.3 130.8,74.0 133.3,74.7 135.9,75.4 138.4,76.1 140.9,76.8 143.5,77.4 146.0,78.1 148.5,78.7 151.1,79.3 153.6,79.9 156.1,80.5 158.7,81.1 161.2,81.7 163.7,82.2 166.3,82.3 168.8,77.8 171.3,71.7 173.9,66.8 176.4,66.3 178.9,66.9 181.5,67.7 184.0,68.6 186.5,69.4 189.1,70.2 191.6,70.9 194.1,71.7 196.7,72.4 199.2,73.2 201.7,73.9 204.3,74.6 206.8,75.3 209.3,76.0 211.9,76.6 214.4,77.3 216.9,77.9 219.5,78.6 222.0,79.2 224.5,79.8 227.1,80.4 229.6,81.0 232.1,81.5 234.7,80.8 237.2,75.6 239.7,69.8 242.3,68.6 244.8,69.1 247.3,69.8 249.9,70.6 252.4,71.3 254.9,72.1 257.5,72.8 260.0,73.6 262.5,74.3 265.1,75.0 267.6,75.7 270.1,76.3 272.7,77.0 275.2,77.6 277.7,78.3 280.3,78.9 282.8,79.5 285.3,80.1 287.9,80.7 290.4,81.3 292.9,81.8 295.5,81.1 298.0,75.9 300.5,69.7 303.1,67.4 305.6,67.7 308.1,68.4 310.7,69.2 313.2,70.0 315.7,70.7 318.3,71.5 320.8,72.3 323.3,73.0 325.9,73.7 328.4,74.4 330.9,75.1 333.5,75.8 336.0,76.5 338.5,77.1 341.1,77.8 343.6,78.4 346.1,79.0 348.7,79.6 351.2,80.2 353.7,80.8 356.3,81.4 358.8,82.0 361.3,80.0 363.9,74.3 366.4,69.4 368.9,68.9 371.5,69.5 374.0,70.3 376.5,71.0 379.1,71.8 381.6,72.5 384.1,73.3 386.7,74.0 389.2,74.7 391.7,75.4 394.3,76.1 396.8,76.7 399.3,77.4 401.9,78.0 404.4,78.6 406.9,79.3 409.5,79.9 412.0,80.5 414.5,81.0 417.1,81.6 419.6,82.2 422.1,82.7 424.7,80.7 427.2,75.1 429.7,69.3 432.3,68.0 434.8,68.5 437.3,69.2 439.9,70.0 442.4,70.8 444.9,71.6 447.5,72.3 450.0,73.0 452.5,73.8 455.1,74.5 457.6,75.2 460.1,75.9 462.7,76.5 465.2,77.2 467.7,77.8 470.3,78.5 472.8,79.1 475.3,79.7 477.9,80.3 480.4,80.9 482.9,81.4 485.5,82.0 488.0,82.6 490.5,83.1 493.1,79.7 495.6,73.8 498.1,68.8 500.7,68.3 503.2,68.9 505.7,69.7 508.3,70.4 510.8,71.2 513.3,72.0 515.9,72.7 518.4,73.4 520.9,74.1 523.5,74.8 526.0,75.5 528.5,76.2 531.1,76.9 533.6,77.5 536.1,78.2 538.7,78.8 541.2,79.4 543.7,80.0 546.3,80.6 548.8,81.2 551.3,81.7 553.9,79.8 556.4,74.1 558.9,68.3 561.5,67.1 564.0,67.6 566.5,68.4 569.1,69.2 571.6,70.0 574.1,70.7 576.7,71.5 579.2,72.2 581.7,73.0 584.3,73.7 586.8,74.4 589.3,75.1 591.9,75.8 594.4,76.5 596.9,77.1 599.5,77.8 602.0,78.4 604.5,79.0 607.1,79.6 609.6,80.2 612.1,80.8 614.7,81.4 617.2,79.4 619.7,73.8 622.3,68.0 624.8,66.8 627.3,67.3 629.9,68.1 632.4,68.9 634.9,69.7 637.5,70.4" fill="none" stroke="#e08a3c" stroke-width="1.8"/>
<polyline points="70.0,170.0 72.5,165.4 75.1,158.0 77.6,150.2 80.1,142.4 82.7,134.7 85.2,127.2 87.7,119.8 90.3,112.6 92.8,105.6 95.3,99.4 97.9,94.3 100.4,90.0 102.9,86.6 105.5,83.9 108.0,81.7 110.5,79.8 113.1,78.3 115.6,77.1 118.1,76.2 120.7,75.4 123.2,74.8 125.7,74.4 128.3,74.0 130.8,73.8 133.3,73.6 135.9,73.5 138.4,73.5 140.9,73.4 143.5,73.4 146.0,73.5 148.5,73.5 151.1,73.5 153.6,73.6 156.1,73.7 158.7,73.8 161.2,73.9 163.7,73.9 166.3,74.0 168.8,74.2 171.3,74.3 173.9,74.4 176.4,74.5 178.9,74.5 181.5,74.7 184.0,74.8 186.5,74.8 189.1,74.9 191.6,74.9 194.1,74.9 196.7,74.9 199.2,75.0 201.7,75.1 204.3,75.2 206.8,75.2 209.3,75.3 211.9,75.3 214.4,75.4 216.9,75.5 219.5,75.7 222.0,75.8 224.5,75.9 227.1,76.0 229.6,76.0 232.1,76.0 234.7,76.0 237.2,76.0 239.7,76.0 242.3,75.9 244.8,75.9 247.3,76.0 249.9,76.1 252.4,76.1 254.9,76.2 257.5,76.2 260.0,76.2 262.5,76.3 265.1,76.3 267.6,76.3 270.1,76.3 272.7,76.3 275.2,76.3 277.7,76.3 280.3,76.3 282.8,76.2 285.3,76.3 287.9,76.3 290.4,76.3 292.9,76.3 295.5,76.3 298.0,76.4 300.5,76.4 303.1,76.4 305.6,76.4 308.1,76.3 310.7,76.3 313.2,76.4 315.7,76.5 318.3,76.5 320.8,76.5 323.3,76.5 325.9,76.6 328.4,76.6 330.9,76.6 333.5,76.6 336.0,76.7 338.5,76.7 341.1,76.7 343.6,76.8 346.1,76.7 348.7,76.6 351.2,76.6 353.7,76.5 356.3,76.6 358.8,76.6 361.3,76.6 363.9,76.6 366.4,76.6 368.9,76.6 371.5,76.7 374.0,76.7 376.5,76.7 379.1,76.7 381.6,76.7 384.1,76.7 386.7,76.7 389.2,76.7 391.7,76.7 394.3,76.7 396.8,76.7 399.3,76.7 401.9,76.7 404.4,76.6 406.9,76.6 409.5,76.6 412.0,76.5 414.5,76.5 417.1,76.5 419.6,76.6 422.1,76.6 424.7,76.6 427.2,76.6 429.7,76.7 432.3,76.7 434.8,76.6 437.3,76.6 439.9,76.6 442.4,76.7 444.9,76.7 447.5,76.7 450.0,76.8 452.5,76.8 455.1,76.7 457.6,76.7 460.1,76.8 462.7,76.7 465.2,76.7 467.7,76.8 470.3,76.8 472.8,76.9 475.3,76.8 477.9,76.8 480.4,76.7 482.9,76.7 485.5,76.7 488.0,76.7 490.5,76.7 493.1,76.7 495.6,76.6 498.1,76.6 500.7,76.7 503.2,76.6 505.7,76.6 508.3,76.6 510.8,76.5 513.3,76.5 515.9,76.5 518.4,76.5 520.9,76.5 523.5,76.5 526.0,76.5 528.5,76.6 531.1,76.6 533.6,76.6 536.1,76.7 538.7,76.7 541.2,76.7 543.7,76.7 546.3,76.7 548.8,76.7 551.3,76.6 553.9,76.6 556.4,76.5 558.9,76.5 561.5,76.5 564.0,76.5 566.5,76.6 569.1,76.7 571.6,76.7 574.1,76.7 576.7,76.8 579.2,76.7 581.7,76.7 584.3,76.7 586.8,76.7 589.3,76.7 591.9,76.7 594.4,76.6 596.9,76.6 599.5,76.6 602.0,76.6 604.5,76.6 607.1,76.6 609.6,76.7 612.1,76.7 614.7,76.7 617.2,76.6 619.7,76.6 622.3,76.7 624.8,76.7 627.3,76.7 629.9,76.7 632.4,76.7 634.9,76.7 637.5,76.7" fill="none" stroke="#4a7bd0" stroke-width="2.2"/>
<polyline points="70.0,222.2 71.3,222.2 72.5,222.2 73.8,222.2 75.1,222.2 76.3,222.2 77.6,222.2 78.9,222.2 80.1,222.2 81.4,222.2 82.7,222.2 83.9,222.2 85.2,222.2 86.5,222.2 87.7,222.2 89.0,222.2 90.3,222.2 91.5,222.2 92.8,222.2 94.1,222.2 95.3,222.2 96.6,222.2 97.9,222.2 99.1,222.2 100.4,222.2 101.7,222.2 102.9,222.2 104.2,222.2 105.5,222.2 106.7,315.6 108.0,315.6 109.3,315.6 110.5,315.6 111.8,315.6 113.1,315.6 114.3,315.6 115.6,315.6 116.9,315.6 118.1,315.6 119.4,315.6 120.7,315.6 121.9,315.6 123.2,315.6 124.5,315.6 125.7,315.6 127.0,315.6 128.3,315.6 129.5,315.6 130.8,315.6 132.1,315.6 133.3,315.6 134.6,315.6 135.9,315.6 137.1,315.6 138.4,315.6 139.7,315.6 140.9,315.6 142.2,315.6 143.5,315.6 144.7,315.6 146.0,315.6 147.3,315.6 148.5,315.6 149.8,315.6 151.1,315.6 152.3,315.6 153.6,315.6 154.9,315.6 156.1,315.6 157.4,315.6 158.7,315.6 159.9,315.6 161.2,315.6 162.5,315.6 163.7,315.6 165.0,315.6 166.3,222.2 167.5,222.2 168.8,222.2 170.1,222.2 171.3,222.2 172.6,315.6 173.9,315.6 175.1,315.6 176.4,315.6 177.7,315.6 178.9,315.6 180.2,315.6 181.5,315.6 182.7,315.6 184.0,315.6 185.3,315.6 186.5,315.6 187.8,315.6 189.1,315.6 190.3,315.6 191.6,315.6 192.9,315.6 194.1,315.6 195.4,315.6 196.7,315.6 197.9,315.6 199.2,315.6 200.5,315.6 201.7,315.6 203.0,315.6 204.3,315.6 205.5,315.6 206.8,315.6 208.1,315.6 209.3,315.6 210.6,315.6 211.9,315.6 213.1,315.6 214.4,315.6 215.7,315.6 216.9,315.6 218.2,315.6 219.5,315.6 220.7,315.6 222.0,315.6 223.3,315.6 224.5,315.6 225.8,315.6 227.1,315.6 228.3,315.6 229.6,315.6 230.9,315.6 232.1,315.6 233.4,222.2 234.7,222.2 235.9,222.2 237.2,222.2 238.5,222.2 239.7,315.6 241.0,315.6 242.3,315.6 243.5,315.6 244.8,315.6 246.1,315.6 247.3,315.6 248.6,315.6 249.9,315.6 251.1,315.6 252.4,315.6 253.7,315.6 254.9,315.6 256.2,315.6 257.5,315.6 258.7,315.6 260.0,315.6 261.3,315.6 262.5,315.6 263.8,315.6 265.1,315.6 266.3,315.6 267.6,315.6 268.9,315.6 270.1,315.6 271.4,315.6 272.7,315.6 273.9,315.6 275.2,315.6 276.5,315.6 277.7,315.6 279.0,315.6 280.3,315.6 281.5,315.6 282.8,315.6 284.1,315.6 285.3,315.6 286.6,315.6 287.9,315.6 289.1,315.6 290.4,315.6 291.7,315.6 292.9,315.6 294.2,222.2 295.5,222.2 296.7,222.2 298.0,222.2 299.3,222.2 300.5,315.6 301.8,315.6 303.1,315.6 304.3,315.6 305.6,315.6 306.9,315.6 308.1,315.6 309.4,315.6 310.7,315.6 311.9,315.6 313.2,315.6 314.5,315.6 315.7,315.6 317.0,315.6 318.3,315.6 319.5,315.6 320.8,315.6 322.1,315.6 323.3,315.6 324.6,315.6 325.9,315.6 327.1,315.6 328.4,315.6 329.7,315.6 330.9,315.6 332.2,315.6 333.5,315.6 334.7,315.6 336.0,315.6 337.3,315.6 338.5,315.6 339.8,315.6 341.1,315.6 342.3,315.6 343.6,315.6 344.9,315.6 346.1,315.6 347.4,315.6 348.7,315.6 349.9,315.6 351.2,315.6 352.5,315.6 353.7,315.6 355.0,315.6 356.3,315.6 357.5,315.6 358.8,315.6 360.1,222.2 361.3,222.2 362.6,222.2 363.9,222.2 365.1,315.6 366.4,315.6 367.7,315.6 368.9,315.6 370.2,315.6 371.5,315.6 372.7,315.6 374.0,315.6 375.3,315.6 376.5,315.6 377.8,315.6 379.1,315.6 380.3,315.6 381.6,315.6 382.9,315.6 384.1,315.6 385.4,315.6 386.7,315.6 387.9,315.6 389.2,315.6 390.5,315.6 391.7,315.6 393.0,315.6 394.3,315.6 395.5,315.6 396.8,315.6 398.1,315.6 399.3,315.6 400.6,315.6 401.9,315.6 403.1,315.6 404.4,315.6 405.7,315.6 406.9,315.6 408.2,315.6 409.5,315.6 410.7,315.6 412.0,315.6 413.3,315.6 414.5,315.6 415.8,315.6 417.1,315.6 418.3,315.6 419.6,315.6 420.9,315.6 422.1,315.6 423.4,222.2 424.7,222.2 425.9,222.2 427.2,222.2 428.5,222.2 429.7,315.6 431.0,315.6 432.3,315.6 433.5,315.6 434.8,315.6 436.1,315.6 437.3,315.6 438.6,315.6 439.9,315.6 441.1,315.6 442.4,315.6 443.7,315.6 444.9,315.6 446.2,315.6 447.5,315.6 448.7,315.6 450.0,315.6 451.3,315.6 452.5,315.6 453.8,315.6 455.1,315.6 456.3,315.6 457.6,315.6 458.9,315.6 460.1,315.6 461.4,315.6 462.7,315.6 463.9,315.6 465.2,315.6 466.5,315.6 467.7,315.6 469.0,315.6 470.3,315.6 471.5,315.6 472.8,315.6 474.1,315.6 475.3,315.6 476.6,315.6 477.9,315.6 479.1,315.6 480.4,315.6 481.7,315.6 482.9,315.6 484.2,315.6 485.5,315.6 486.7,315.6 488.0,315.6 489.3,315.6 490.5,222.2 491.8,222.2 493.1,222.2 494.3,222.2 495.6,222.2 496.9,315.6 498.1,315.6 499.4,315.6 500.7,315.6 501.9,315.6 503.2,315.6 504.5,315.6 505.7,315.6 507.0,315.6 508.3,315.6 509.5,315.6 510.8,315.6 512.1,315.6 513.3,315.6 514.6,315.6 515.9,315.6 517.1,315.6 518.4,315.6 519.7,315.6 520.9,315.6 522.2,315.6 523.5,315.6 524.7,315.6 526.0,315.6 527.3,315.6 528.5,315.6 529.8,315.6 531.1,315.6 532.3,315.6 533.6,315.6 534.9,315.6 536.1,315.6 537.4,315.6 538.7,315.6 539.9,315.6 541.2,315.6 542.5,315.6 543.7,315.6 545.0,315.6 546.3,315.6 547.5,315.6 548.8,315.6 550.1,315.6 551.3,315.6 552.6,222.2 553.9,222.2 555.1,222.2 556.4,222.2 557.7,222.2 558.9,315.6 560.2,315.6 561.5,315.6 562.7,315.6 564.0,315.6 565.3,315.6 566.5,315.6 567.8,315.6 569.1,315.6 570.3,315.6 571.6,315.6 572.9,315.6 574.1,315.6 575.4,315.6 576.7,315.6 577.9,315.6 579.2,315.6 580.5,315.6 581.7,315.6 583.0,315.6 584.3,315.6 585.5,315.6 586.8,315.6 588.1,315.6 589.3,315.6 590.6,315.6 591.9,315.6 593.1,315.6 594.4,315.6 595.7,315.6 596.9,315.6 598.2,315.6 599.5,315.6 600.7,315.6 602.0,315.6 603.3,315.6 604.5,315.6 605.8,315.6 607.1,315.6 608.3,315.6 609.6,315.6 610.9,315.6 612.1,315.6 613.4,315.6 614.7,315.6 615.9,222.2 617.2,222.2 618.5,222.2 619.7,222.2 621.0,222.2 622.3,315.6 623.5,315.6 624.8,315.6 626.1,315.6 627.3,315.6 628.6,315.6 629.9,315.6 631.1,315.6 632.4,315.6 633.7,315.6 634.9,315.6 636.2,315.6 637.5,315.6 638.7,315.6" fill="none" stroke="#e08a3c" stroke-width="1.2"/>
<polyline points="70.0,222.2 71.3,222.2 72.5,222.2 73.8,222.2 75.1,222.2 76.3,222.2 77.6,222.2 78.9,222.2 80.1,222.2 81.4,222.2 82.7,222.2 83.9,222.2 85.2,222.2 86.5,222.2 87.7,222.2 89.0,222.2 90.3,222.2 91.5,224.6 92.8,234.6 94.1,240.8 95.3,246.5 96.6,251.9 97.9,257.1 99.1,260.8 100.4,268.2 101.7,270.1 102.9,274.5 104.2,276.2 105.5,281.1 106.7,282.5 108.0,286.1 109.3,285.7 110.5,286.7 111.8,289.8 113.1,292.8 114.3,293.8 115.6,293.8 116.9,295.6 118.1,299.3 119.4,297.1 120.7,302.8 121.9,299.6 123.2,300.0 124.5,301.0 125.7,300.8 127.0,300.8 128.3,304.3 129.5,304.0 130.8,303.8 132.1,305.2 133.3,304.1 134.6,304.6 135.9,305.8 137.1,305.6 138.4,305.1 139.7,305.5 140.9,306.6 142.2,305.3 143.5,306.6 144.7,308.7 146.0,306.7 147.3,305.7 148.5,305.5 149.8,308.8 151.1,306.3 152.3,307.1 153.6,303.3 154.9,306.0 156.1,308.7 157.4,307.4 158.7,306.0 159.9,307.4 161.2,309.0 162.5,308.3 163.7,308.0 165.0,307.8 166.3,308.6 167.5,307.1 168.8,307.6 170.1,309.0 171.3,307.0 172.6,306.3 173.9,309.3 175.1,308.2 176.4,306.8 177.7,308.4 178.9,308.3 180.2,309.2 181.5,308.5 182.7,306.1 184.0,307.1 185.3,305.3 186.5,306.1 187.8,306.6 189.1,304.7 190.3,305.4 191.6,309.3 192.9,307.3 194.1,305.6 195.4,306.9 196.7,308.2 197.9,308.2 199.2,307.5 200.5,309.3 201.7,307.2 203.0,305.5 204.3,308.7 205.5,308.2 206.8,307.8 208.1,308.8 209.3,307.3 210.6,307.7 211.9,306.9 213.1,308.3 214.4,308.6 215.7,307.3 216.9,307.4 218.2,306.9 219.5,309.4 220.7,306.6 222.0,307.9 223.3,308.0 224.5,306.0 225.8,306.0 227.1,308.5 228.3,307.1 229.6,307.2 230.9,304.8 232.1,310.0 233.4,306.1 234.7,304.4 235.9,306.0 237.2,306.0 238.5,306.3 239.7,305.6 241.0,306.6 242.3,307.1 243.5,308.0 244.8,309.5 246.1,306.3 247.3,307.3 248.6,308.8 249.9,308.6 251.1,309.1 252.4,308.3 253.7,309.4 254.9,306.5 256.2,304.5 257.5,307.3 258.7,306.5 260.0,307.4 261.3,306.7 262.5,309.3 263.8,308.6 265.1,307.8 266.3,306.5 267.6,308.0 268.9,305.6 270.1,306.0 271.4,305.1 272.7,308.7 273.9,307.4 275.2,307.2 276.5,304.7 277.7,304.6 279.0,308.5 280.3,304.1 281.5,307.7 282.8,309.3 284.1,309.6 285.3,306.0 286.6,306.0 287.9,306.5 289.1,309.2 290.4,305.5 291.7,306.6 292.9,307.1 294.2,308.9 295.5,306.2 296.7,309.3 298.0,306.0 299.3,307.1 300.5,305.6 301.8,306.9 303.1,306.2 304.3,303.8 305.6,303.6 306.9,305.2 308.1,308.9 309.4,307.1 310.7,308.1 311.9,308.3 313.2,309.2 314.5,309.9 315.7,306.0 317.0,309.6 318.3,309.7 319.5,306.2 320.8,306.2 322.1,305.9 323.3,307.5 324.6,310.6 325.9,308.5 327.1,306.4 328.4,307.1 329.7,308.1 330.9,307.1 332.2,307.2 333.5,308.1 334.7,309.0 336.0,306.6 337.3,305.9 338.5,309.9 339.8,307.1 341.1,308.9 342.3,304.5 343.6,305.7 344.9,306.6 346.1,308.4 347.4,304.9 348.7,306.7 349.9,305.4 351.2,304.7 352.5,305.6 353.7,309.9 355.0,307.3 356.3,306.3 357.5,308.0 358.8,306.6 360.1,308.8 361.3,307.6 362.6,308.0 363.9,306.3 365.1,307.3 366.4,306.6 367.7,310.4 368.9,304.3 370.2,307.1 371.5,308.8 372.7,308.3 374.0,308.4 375.3,308.2 376.5,305.2 377.8,307.5 379.1,304.2 380.3,306.4 381.6,305.3 382.9,307.1 384.1,305.4 385.4,306.1 386.7,308.3 387.9,308.3 389.2,305.1 390.5,305.6 391.7,309.3 393.0,306.9 394.3,306.4 395.5,306.5 396.8,306.4 398.1,306.4 399.3,306.8 400.6,309.9 401.9,304.9 403.1,307.0 404.4,309.7 405.7,306.8 406.9,306.6 408.2,305.0 409.5,305.9 410.7,304.8 412.0,306.1 413.3,305.0 414.5,308.5 415.8,308.4 417.1,307.9 418.3,310.4 419.6,306.8 420.9,306.4 422.1,307.6 423.4,306.3 424.7,306.8 425.9,308.3 427.2,307.4 428.5,305.7 429.7,307.7 431.0,307.0 432.3,306.7 433.5,308.0 434.8,309.4 436.1,307.1 437.3,306.8 438.6,307.8 439.9,309.5 441.1,306.5 442.4,306.5 443.7,307.9 444.9,307.7 446.2,307.4 447.5,307.4 448.7,305.6 450.0,306.3 451.3,307.3 452.5,307.4 453.8,304.6 455.1,308.6 456.3,308.5 457.6,307.2 458.9,305.8 460.1,304.9 461.4,307.7 462.7,309.9 463.9,308.3 465.2,306.4 466.5,307.6 467.7,309.2 469.0,307.2 470.3,306.9 471.5,307.0 472.8,305.4 474.1,306.8 475.3,303.7 476.6,307.7 477.9,305.3 479.1,309.3 480.4,306.5 481.7,304.1 482.9,304.8 484.2,310.9 485.5,306.9 486.7,306.2 488.0,306.0 489.3,305.8 490.5,307.7 491.8,306.9 493.1,306.4 494.3,306.5 495.6,306.9 496.9,307.2 498.1,306.4 499.4,307.0 500.7,305.5 501.9,306.2 503.2,304.6 504.5,307.3 505.7,305.9 507.0,307.5 508.3,306.2 509.5,307.1 510.8,308.0 512.1,306.9 513.3,307.0 514.6,307.3 515.9,305.0 517.1,306.5 518.4,308.7 519.7,307.7 520.9,307.0 522.2,308.5 523.5,307.7 524.7,305.7 526.0,306.8 527.3,309.0 528.5,306.7 529.8,306.1 531.1,305.1 532.3,308.2 533.6,308.2 534.9,307.5 536.1,306.6 537.4,308.3 538.7,308.3 539.9,306.2 541.2,307.1 542.5,308.1 543.7,306.6 545.0,307.1 546.3,308.3 547.5,304.6 548.8,305.7 550.1,304.5 551.3,307.3 552.6,307.6 553.9,308.0 555.1,306.4 556.4,306.8 557.7,307.1 558.9,309.3 560.2,307.0 561.5,310.0 562.7,308.0 564.0,308.5 565.3,308.2 566.5,308.3 567.8,306.5 569.1,307.4 570.3,304.9 571.6,307.2 572.9,308.7 574.1,307.8 575.4,305.8 576.7,308.8 577.9,305.3 579.2,307.9 580.5,307.3 581.7,305.3 583.0,307.0 584.3,309.1 585.5,305.0 586.8,305.7 588.1,306.2 589.3,308.3 590.6,307.3 591.9,307.4 593.1,305.6 594.4,309.5 595.7,305.5 596.9,307.6 598.2,307.3 599.5,309.6 600.7,307.4 602.0,306.4 603.3,305.7 604.5,306.9 605.8,307.0 607.1,308.6 608.3,307.6 609.6,305.9 610.9,307.6 612.1,307.3 613.4,307.9 614.7,306.5 615.9,304.3 617.2,306.8 618.5,308.0 619.7,309.3 621.0,308.5 622.3,306.4 623.5,307.2 624.8,308.6 626.1,308.9 627.3,307.6 628.6,305.6 629.9,304.4 631.1,306.2 632.4,307.2 633.7,308.9 634.9,307.9 636.2,307.1 637.5,308.2 638.7,310.0" fill="none" stroke="#4a7bd0" stroke-width="2.2"/>
<text x="20.0" y="100.0" font-size="13" text-anchor="middle" transform="rotate(-90 20 100)">피부 °C</text><text x="20.0" y="272.0" font-size="13" text-anchor="middle" transform="rotate(-90 20 272)">전력 cap W</text><line x1="330" y1="120" x2="355" y2="120" stroke="#e08a3c" stroke-width="3"/><text x="362.0" y="124.0" font-size="12" text-anchor="start">hysteresis (1.5 W ↔ 0.2 W)</text><line x1="330" y1="140" x2="355" y2="140" stroke="#4a7bd0" stroke-width="3"/><text x="362.0" y="144.0" font-size="12" text-anchor="start">PI + anti-windup</text>
</svg>
```

그림 8 — 위: 피부 온도, 아래: 전력 cap. hysteresis(주황)는 한계 근처에서 톱니를 그리며 전력이 on/off로 튄다. PI(파랑)는 처음 약 33초 동안 1.5 W burst를 허용한 뒤(hysteresis는 57초) 목표 온도 근처에서 0.32 W로 매끄럽게 수렴한다.

실무 연결:

- 실제 cap은 연속값이 아니라 **OPP 단계**다. PI 출력(W)을 "이 전력 이하인 가장 높은 OPP"로 바꾸는 표가 필요하다 — power_allocator가 cooling device마다 전력 모델(OPP별 전력)을 요구하는 이유다.
- 센서 잡음과 지연(서미스터의 열 지연, virtual sensor의 추정 오차)은 PI gain을 낮게 잡게 만든다. 칩 쪽 빠른 보호는 별도 루프(하드웨어 임계 인터럽트)로 둔다.
- ML 관점의 cooling device: OPP를 낮추는 것보다 **모델을 바꾸는 것**(큰 모델 → 작은 모델, 프레임 레이트 절반, 음성 인식을 클라우드로)이 같은 전력 절감에서 사용자 경험을 덜 해친다.

---

## 9. 전력을 의식한 ML 배포

지금까지의 하드웨어 장치를 ML 워크로드 결정으로 옮기면 다음 표가 된다.

| 결정 | 이유 (어느 절) | 예 |
|---|---|---|
| 무거운 작업은 충전 중·식어 있을 때 | 충전 중엔 배터리 에너지가 공짜에 가깝고, 열 여유(8절)가 있다. 단 충전 자체도 열을 낸다 | 개인화 fine-tuning, 로그 요약, 인덱스 재구축, 모델 업데이트 |
| 같은 모델이면 CPU보다 NPU/DSP | 추론당 에너지가 자릿수로 작다 (D7 3절). 켜 있는 시간도 짧다 | 단, NPU domain을 켜는 전환 비용(3.5절)과 weight 적재 비용을 같이 본다 |
| 센서는 FIFO에 모았다가 한 번에 | 깨어나는 횟수 × 전환 비용 감소 (4절, 6.5절) | IMU watermark, 오디오 DMA ping-pong (B7, G2) |
| always-on 모델은 SRAM에 상주 | DRAM을 깨우는 비용과 self-refresh 탈출 지연을 피한다 (E7) | KWS·IMU 모델 weight를 retention SRAM에 |
| "언제 안 돌릴지"를 먼저 | 가장 싼 추론은 돌리지 않은 추론이다 (6.5절 C) | wake-on-motion, VAD gate, 착용 감지 |
| 무선과 묶기 | 무선은 켤 때 고정비와 tail이 크다 (D7 6.3). 피크 전류 동시 발생을 피한다 (7.3절) | 업로드는 이미 켜진 연결에 편승, 결과를 모아 전송 (E8) |
| deadline을 런타임에 알린다 | 범용 governor는 deadline을 모른다 (5.6절) | uclamp, performance hint, NPU perf profile |
| 열 상태에 따라 모델을 바꾼다 | 열이 지속 전력을 정한다 (8.5절) | thermal status가 MODERATE면 작은 ASR 모델로 |
| 배터리 잔량·온도에 따라 피크 제한 | brown-out 방지 (7.3절) | 저온·저잔량에서 NPU 최대 OPP 금지 |

---

## 10. 펌웨어 측정 훅 — Don의 영역

측정 방법론(샘플링, baseline 차감, 적분)은 D7 8절에 있다. 여기서는 **펌웨어가 측정을 가능하게 만들기 위해 심어 둘 것**만 체크리스트로 정리한다.

- [ ] **GPIO 마커**: 추론 시작/끝, 전원 상태 진입/탈출, DVFS 변경, 무선 TX에 GPIO 토글. 여러 개면 비트 조합으로 상태 번호를 낸다. 로직 분석기와 전력 분석기를 같은 트리거로 묶는다.
- [ ] **전원 상태 로그**: 상태 전이마다 (timestamp, 이전 상태, 다음 상태, 이유 — 어떤 wake source인지) 를 링 버퍼에 남긴다. "왜 deep sleep에 못 들어갔나"(어떤 드라이버가 막았나 — wakelock / sleep veto)가 가장 흔한 배터리 버그다.
- [ ] **상태별 체류 시간 카운터**: 상태마다 누적 시간을 저전력 타이머로 센다. 평균 전력 ≈ Σ(상태 전력 × 체류 비율) 로 D7 6.1절의 식을 필드 데이터로 검증할 수 있다.
- [ ] **wake source 카운터**: 어떤 인터럽트가 몇 번 깨웠나. false wake(KWS 오검출, 센서 잡음)의 전력 비용이 여기서 드러난다.
- [ ] **레일별 측정 (EVT 보드)**: EVT/DVT 보드에는 레일마다 shunt 저항이나 측정 점퍼를 둔다(양산 보드에는 없는 경우가 많다). "배터리 전류가 늘었다"를 "NPU 레일인지 DDR 레일인지"로 분해하는 유일한 방법이다. 레일 측정값 합과 배터리 측정값의 차이가 레귤레이터 손실이다(7절).
- [ ] **fuel gauge 읽기**: coulomb counter(전류 감지 저항의 전압을 적분해 전하를 셈)의 누적 전하, 평균 전류, 전압, 온도를 주기적으로 로그. 대역폭이 낮아 추론 1회는 못 보지만, **몇 시간짜리 필드 테스트의 평균 전력**을 실험실 장비 없이 얻는다.
- [ ] **온도 로그**: 전력 로그 옆에 칩·배터리·(가상) 피부 온도. 누설(1절)과 throttling(8절) 때문에 온도 없는 전력은 해석이 안 된다.
- [ ] **OPP·thermal cap 로그**: 측정 당시 실제 OPP와 cap이 무엇이었는지. "벤치마크가 느려졌다"의 절반은 throttling이다.
- [ ] **재현 가능한 전원 테스트 모드**: 특정 상태에 고정하는 디버그 명령(예: "deep sleep에 60초 고정", "NPU를 OPP 3에 고정")을 두면 상태별 전력을 깨끗하게 잰다.

---

## 11. 임베디드 관점에서 다시 보기

펌웨어가 직접 만지는 전력 관련 코드를 한곳에 모으면 대략 이런 모양이다.

```
power_manager (idle hook)
 ├─ next_event = min(timer, sensor FIFO 예상, 무선 스케줄)       ← 4절: 예상 idle 시간
 ├─ allowed = 모든 드라이버의 veto 확인 (DMA 진행, UART 수신 중…)  ← 3.4절 ①: drain
 ├─ state = argmin_E(state, next_event) subject to wake ≤ latency  ← 4.2절 ex2
 ├─ save context / 설정할 retention bank                           ← 3.3절
 ├─ SCR.SLEEPDEEP + 벤더 PMU 설정 → disable_irq, DSB, WFI           ← 6.4절 ex9
 └─ wake: restore, wake source 기록, 상태 체류 시간 누적           ← 10절

dvfs_manager
 ├─ 런타임 hint (모델 종류, deadline) → 필요한 cycle/s              ← 5.6절 oracle-Emin
 ├─ thermal cap과 배터리 피크 제한으로 자르기                      ← 8.5절, 7.3절
 └─ 전압 먼저 올리고 주파수 올리기 / 주파수 먼저 내리고 전압 내리기 ← 7.3절 droop

thermal_manager (1 s 주기, 느린 층)
 ├─ skin 추정 (virtual sensor)
 ├─ PI → power budget → OPP cap, 모델 크기 선택                    ← 8.5절 ex8
 └─ 칩 접합 보호는 하드웨어 임계 인터럽트 (빠른 층)
```

Don이 SSD 펌웨어에서 만든 power state machine(NVMe PS 전환, APST — autonomous power state transition의 idle timer 기반 진입)과 구조가 같다. APST의 "idle 시간 X ms 후 PS3로"라는 설정이 바로 4절의 break-even 시간이다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| clock을 끈 블록 레지스터 접근 | 간헐적 bus hang, HardFault, 0 읽힘 | 드라이버 간 clock/power 참조 카운트 불일치 | 접근 전 clock/power 상태 assert, 참조 카운트 관리 |
| isolation 전에 전원 차단 (또는 복구 순서 역전) | 드물게 켜진 domain 오동작, sleep 전류가 기대보다 큼 | 떠 있는 입력(X)이 켜진 쪽으로 | PMU 시퀀스 문서대로, 시퀀스 검증 테스트 |
| check-then-WFI race | 가끔 인터럽트 하나를 놓치고 다음 이벤트까지 잠 (지연 튐) | 플래그 확인과 WFI 사이에 인터럽트 도착 | 인터럽트 마스크 후 확인 → WFI (6.4절) |
| 짧은 idle에 깊은 잠 | 평균 전력이 오히려 증가, 반응 지연 | E_trans를 무시 | break-even 계산, idle 예측 개선 (4절) |
| weight를 매번 flash에서 재적재 | 추론당 에너지가 모델 에너지의 수십 배 | power-off에서 SRAM 내용 소실 | weight bank를 retention (3.5절) |
| 상온에서만 sleep 전류 측정 | 여름·고온 필드에서 배터리 수명 미달 | 누설의 온도 의존 (1절) | 고온 측정, 온도별 예산 |
| PWM 고정 buck을 always-on 레일에 | sleep 전류가 mA급 | 저부하에서 스위칭 손실 (7.2절) | auto PFM, 저 Iq 레귤레이터, PMIC sleep 모드 |
| 레일에서만 측정 | 배터리 수명 예측이 낙관적 | 레귤레이터 손실 누락 | 배터리 단자에서 측정 (7.4절) |
| 최저 OPP가 항상 최저 에너지라고 가정 | 오히려 에너지 증가 | platform·누설 지배 (5.6절) | OPP별 cycle당 에너지 비교 |
| on/off throttling만 사용 | 성능이 울컥거림, 사용자 불만 | hysteresis 제어의 본질 (8.5절) | PI/power budget, 모델 크기 단계 전환 |

---

## 13. 면접에서 이렇게 말한다

**Q.** "What's the difference between clock gating and power gating?"

**A.** clock gating은 클럭을 막아 토글을 멈추는 것이라 동적 전력만 없앤다. 전압이 그대로라 누설은 남지만 상태가 유지되고 다음 사이클에 바로 재개된다. power gating은 header/footer 스위치로 전원 자체를 끊어 누설까지 없앤다. 대신 상태를 잃으므로 isolation cell로 출력을 고정하고, 필요한 상태는 retention flop·retention SRAM·소프트웨어 save로 보존해야 하며, rush current를 제한하며 켜느라 µs~ms의 wake latency와 전환 에너지가 든다. 그래서 짧은 idle은 clock gating, 긴 idle은 power gating이고, 그 경계가 break-even 시간이다.

> "Clock gating stops the clock so nothing toggles — it removes dynamic power, but the supply stays on, so leakage remains; state is kept and you can resume on the next cycle. Power gating cuts the supply through header or footer switches, so leakage goes away too, but you lose state. You need isolation cells to clamp outputs, retention flops or retention SRAM or a software save for whatever must survive, and a controlled ramp to limit rush current — so there's wake latency and transition energy. Short idle periods get clock gating; long ones get power gating, and the boundary is the break-even time."

**Q.** "When is it worth entering deep sleep between inferences?"

**A.** break-even 시간으로 판단한다: t_be = E_transition / (P_얕은잠 − P_깊은잠). 예를 들어 WFI가 1.2 mW, retention이 0.08 mW에 전환 15 µJ면 13 ms만 넘어도 retention이 이긴다. 전원 차단은 weight를 flash에서 다시 읽는 비용 때문에 수백 µJ가 들어서 수 초 이상 idle일 때만 이긴다. 여기에 wake latency 제약을 더한다. 그리고 전환 비용의 대부분은 스위치가 아니라 상태 복구, 특히 weight 재적재이므로 weight가 든 SRAM bank를 retention으로 남기는 게 보통 정답이다. ML 기기는 추론 주기가 결정적이라 idle 시간을 잘 예측할 수 있어서 이 계산이 잘 맞는다.

> "I compute the break-even time: transition energy divided by the power difference between the shallow and deep state. With illustrative numbers — 1.2 milliwatts in WFI, 80 microwatts in retention, 15 microjoules to go in and out — retention pays off after about 13 milliseconds. Full power-off costs hundreds of microjoules mainly because weights have to be reloaded from flash, so it only wins for idle periods of several seconds. Then I apply the wake-latency constraint. Since most of the transition cost is restoring state, keeping the weight SRAM banks in retention is usually the right call — and with periodic inference the idle time is very predictable, so the decision is reliable."

**Q.** "How does DVFS save energy, and when doesn't it?"

**A.** 사이클당 동적 에너지는 C·V²이고, 주파수를 내리면 전압도 내릴 수 있으니 일 하나당 에너지가 V²로 준다. 예를 들어 1.03 V → 0.59 V면 동적 에너지가 약 1/3이다. 하지만 켜져 있는 시간이 늘어 누설과 platform 전력(DRAM, PLL, interconnect)이 쌓인다. 그래서 platform 전력이 크면 오히려 최고 OPP로 빨리 끝내고 자는 게 이긴다. memory-bound 구간은 클럭과 무관하게 걸리므로 코어 DVFS 이득이 없고, V_min 아래나 최소 에너지점 아래에서는 주파수만 떨어진다. 시뮬레이션해 보면 "가장 낮은 feasible OPP"가 platform 전력이 클 때 최고 OPP보다 비쌌다. 그래서 OPP별 cycle당 에너지를 비교하고, deadline은 런타임 hint로 governor에 알려 준다.

> "Dynamic energy per cycle is C V squared, and lowering frequency lets you lower voltage, so the energy per operation drops quadratically — going from about 1.03 to 0.59 volts cuts it to roughly a third. But the job takes longer, so leakage and platform power — DRAM, PLLs, interconnect — accumulate. When platform power is large, finishing at the top OPP and sleeping wins. It also doesn't help in memory-bound phases, below the SRAM minimum voltage, or below the minimum-energy point near threshold. In my simulation, the lowest feasible OPP was actually worse than max frequency once active platform power was 150 milliwatts. So I compare energy per cycle across OPPs and give the governor the deadline through a runtime hint, because a utilization-based governor can't see it."

**Q.** "What happens as a wearable heats up?"

**A.** 세 가지가 동시에 일어난다. 첫째, 누설이 온도에 대해 지수적으로 늘어 같은 일에도 전력이 더 든다 — 되먹임이다. 둘째, thermal zone의 trip point를 넘으면 governor가 cooling device, 즉 CPU·NPU 주파수 상한을 걸어 성능이 떨어진다. 웨어러블은 칩 한계가 아니라 피부 온도 한계에 먼저 걸리고, 피부는 직접 재기 어려워 virtual sensor로 추정한다. 셋째, 배터리가 뜨거우면 충전 전류가 제한된다. 지속 가능 전력은 결국 ΔT/R_th로 정해지고, 제어기는 그걸 바꾸지 못하고 얼마나 부드럽게 도달하느냐만 바꾼다. 그래서 PI나 power-budget 방식으로 부드럽게 제한하고, ML 쪽에서는 모델을 작은 것으로 바꾸는 식으로 대응한다.

> "Three things happen together. Leakage rises roughly exponentially with temperature, so the same work costs more power — a positive feedback. When a thermal zone crosses its trip point, the governor applies cooling devices, typically frequency caps on the CPU and NPU, so performance drops. In a wearable the binding limit is usually skin temperature, not the junction, and skin isn't directly measurable, so it's estimated with a virtual sensor. And a hot battery gets its charge current reduced. Sustainable power is set by the allowed temperature rise over the thermal resistance — the controller can't change that, only how smoothly you get there. So I prefer a PI or power-budget controller over on-off throttling, and on the ML side I'd rather switch to a smaller model than just drop the clock."

**Q.** "How would you minimize always-on sensing power?"

**A.** 계층으로 설계한다. 가장 싼 추론은 안 돌린 추론이라서, 첫 단은 센서 안에서 한다 — IMU wake-on-motion, 센서 내장 ML core, 마이크 VAD. 둘째, 센서 FIFO watermark로 샘플을 모아 MCU wake 횟수를 줄이고, 절대 SoC를 샘플마다 깨우지 않는다. 셋째, always-on 모델은 retention SRAM에 상주시켜 DRAM과 flash를 건드리지 않는다. 넷째, MCU는 최소 에너지점 근처의 낮은 전압·클럭에서 돌리고, 필요한 SRAM bank만 retention한다. 다섯째, 레일은 저 Iq 레귤레이터와 PFM 모드를 쓴다 — µW 부하에서는 레귤레이터 Iq가 예산의 큰 몫이다. 계산해 보면 보통 센서 전류가 모델보다 크고, wake-on-motion 같은 gating이 평균 전류를 절반 이하로 줄인다. 마지막으로 배터리 단자에서 온도별로 측정해 검증한다.

> "I design it as a cascade. The cheapest inference is the one you don't run, so the first stage lives in the sensor — wake-on-motion, the sensor's embedded ML core, or a voice activity detector. Samples are batched in the sensor FIFO so the MCU wakes a couple of times a second instead of every sample, and the big SoC never wakes per sample. The always-on model stays resident in retention SRAM so DRAM and flash aren't touched. The MCU runs near its minimum-energy voltage with only the SRAM banks it needs retained. On the power side I use low-quiescent-current regulators in PFM mode, because at microwatt loads the regulator's own current is a big share. When I budget it, the sensor current usually dominates the model, and motion gating alone can cut the average by more than half. Then I verify at the battery terminals across temperature."

**Q.** "A peripheral register read occasionally hangs the bus after you added a new low-power mode. How do you debug it?"

**A.** 먼저 그 레지스터가 속한 clock/power domain이 접근 시점에 켜져 있었는지 의심한다. 새 저전력 모드는 보통 어떤 domain을 끄거나 clock을 막는데, 다른 드라이버가 참조 카운트 없이 접근하면 hang이나 fault가 난다. 전원 상태 로그와 GPIO 마커로 hang 직전의 상태 전이를 잡고, PMU 상태 레지스터를 JTAG로 읽어 domain이 꺼져 있는지 확인한다. 끄기 전 트래픽 drain(진행 중인 DMA·버스 트랜잭션)을 했는지, isolation과 restore 순서가 문서대로인지도 본다. 고치는 방법은 clock/power 참조 카운트와 접근 전 assert, 그리고 sleep veto 경로를 명확히 하는 것이다.

> "My first suspect is that the register's clock or power domain was off at the moment of access. A new low-power mode usually gates a clock or powers down a domain, and any driver touching that block without holding a reference will hang or fault. I'd capture the power-state transitions right before the hang with state logging and GPIO markers, then read the PMU status over JTAG to confirm the domain state. I'd also check that outstanding DMA and bus transactions are drained before gating, and that isolation release and state restore happen in the documented order. The fix is proper clock and power reference counting, an assert before register access, and a clear sleep-veto path for drivers with work in flight."

---

## 14. 직접 해보기

1. 손계산: 누설이 20 °C마다 2배라면 25 °C에서 30 mW인 누설은 65 °C에서 몇 mW인가? 정답: 2^(40/20) = 4배 → 120 mW.
2. 손계산: 얕은 잠 0.5 mW, 깊은 잠 0.02 mW, 전환 에너지 0.24 mJ, wake latency 2 ms. 추론 주기가 200 ms(추론 10 ms)라면 깊은 잠에 들어가야 하나? 정답: t_be = 0.24 mJ / 0.48 mW = 500 ms. idle 190 ms < 500 ms이므로 얕은 잠이 낫다.
3. 손계산: 3.8 V 배터리에서 1.2 V 레일 5 mA를 LDO로 만들 때와 효율 90 % buck으로 만들 때 배터리 전력은? 정답: LDO는 3.8 V × 5 mA = 19 mW(효율 32 %), buck은 1.2 × 5 / 0.9 = 6.67 mW.
4. 손계산: R_th = 1/(h·A), h = 10 W/(m²·K). 표면적 12 cm²인 이어버드의 R_th와 ΔT = 6 K에서의 지속 전력은? 정답: 1/(10 × 0.0012) ≈ 83 K/W, 6/83 ≈ 72 mW.
5. 코드: ex5에서 idle 전력 P_IDLE을 1 mW → 20 mW(깊은 잠 불가)로 바꾸면 결과가 어떻게 되나? 힌트: idle이 비싸면 빨리 끝내고 쉬는 이득이 줄어든다. 그리고 oracle-Emin의 비용 함수에 idle 전력이 들어 있는지 보라. 정답: platform 30 mW에서 performance 2113.5 mJ, oracle-lowest 1559.3 mJ(26 % 절약, 1 mW일 때의 24 %보다 우위 확대), oracle-Emin 1580.7 mJ로 oracle-lowest보다 약간 비싸진다 — Emin은 활성 중 cycle당 에너지만 보고 idle 에너지를 무시하기 때문이다. 비용 함수에서 빠진 항이 있으면 "최적" 정책도 틀린다.
6. 코드: ex8의 PI에서 anti-windup 조건을 지우고(항상 `integ += e`) 실행하면 max_skin이 어떻게 되나? 힌트: 처음 약 33초 동안 cap이 1.5 W에 포화된 채로 큰 양의 오차가 적분에 쌓인다. 정답: max_skin 41.56 °C, 한계(41 °C) 초과 86.7초 — 쌓인 적분이 풀리는 동안 cap이 늦게 내려가 overshoot가 생긴다. anti-windup이 있으면 40.27 °C, 초과 0초.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| subthreshold leakage | 문턱 전압 아래 누설 | 꺼진 트랜지스터로 흐르는 전류. 온도에 지수적으로 민감 |
| thermal runaway | 열 폭주 | 누설 ↔ 온도의 양의 되먹임이 방열을 이기는 상태 |
| clock gating | 클럭 차단 | 토글을 멈춰 동적 전력 제거. 누설은 남음 |
| ICG | integrated clock gating 셀 | latch + AND로 glitch 없이 클럭을 막는 셀 |
| power gating | 전원 차단 | header/footer 스위치로 domain 전원을 끊어 누설까지 제거 |
| header / footer switch | 전원 스위치 | VDD 쪽 PMOS / VSS 쪽 NMOS |
| rush current | 돌입 전류 | 꺼진 domain을 켤 때 커패시턴스 충전 전류. daisy chain으로 제한 |
| power domain | 전원 영역 | 같이 켜지고 꺼지는 로직 묶음 |
| isolation cell | 격리 셀 | 꺼진 domain의 출력을 0/1로 고정 |
| level shifter | 레벨 변환기 | 전압이 다른 domain 사이 신호 변환 |
| AON | always-on domain | 절대 꺼지지 않는 영역 (PMU, RTC, wake) |
| retention | 상태 유지 | 낮은 전압으로 flop·SRAM 값만 유지 |
| UPF | Unified Power Format (IEEE 1801) | 칩의 전원 의도를 기술하는 형식 |
| PMU | power management unit | 칩 안의 전원 시퀀스 FSM |
| break-even time | 손익분기 시간 | E_trans / (P_A − P_B). 이보다 긴 idle에서 깊은 잠이 이득 |
| OPP | operating performance point | 검증된 (주파수, 전압) 쌍 |
| governor | 정책 | OPP나 idle state를 고르는 알고리즘 |
| schedutil | Linux cpufreq governor | 스케줄러 util로 주파수 결정 (1.25 × f_max × util) |
| uclamp | utilization clamp | 작업별 util 하한·상한 hint |
| AVS | adaptive voltage scaling | 칩별·실시간으로 전압 여유를 회수 |
| minimum energy point | 최소 에너지점 | 동적 + 누설의 사이클당 에너지가 최소인 전압 |
| WFI / WFE | wait for interrupt / event | Cortex-M 코어를 재우는 명령 |
| SLEEPDEEP | SCR bit 2 | 이번 잠이 deep sleep임을 시스템에 알림 |
| wake source | 깨우기 원인 | GPIO, RTC, comparator, 센서 인터럽트 등 |
| PMIC | 전원 관리 IC | 레귤레이터, 충전, 시퀀싱, fuel gauge |
| LDO | 선형 레귤레이터 | 효율 ≤ Vout/Vin |
| buck | 강압 스위칭 레귤레이터 | 인덕터로 전압을 낮춤, 고효율 |
| PFM / PWM | 스위칭 모드 | 저부하용 펄스 / 고정 주파수 |
| Iq | quiescent current | 레귤레이터 자신이 먹는 전류 |
| dropout | 최소 전압 차 | LDO가 레귤레이션하려면 필요한 Vin − Vout |
| brown-out (BOR) | 저전압 리셋 | 전압이 임계 아래로 떨어지면 리셋 |
| fuel gauge | 잔량 계 | coulomb counter·전압 모델로 SoC 추정 |
| TSENS | 온칩 온도 센서 | Qualcomm 칩에서 쓰는 이름 |
| thermal zone | 열 영역 | 온도 하나와 그 정책 |
| trip point | 동작 임계 온도 | passive, active, hot, critical |
| cooling device | 냉각 수단 | 주파수 상한, 충전 제한 등 |
| power_allocator | Linux thermal governor | PID로 전력 예산을 정해 actor에 분배 |
| virtual sensor | 가상 센서 | 여러 센서와 모델로 피부 온도 등을 추정 |
| anti-windup | 적분 포화 방지 | 출력이 포화된 동안 적분을 멈춤 |

---

## 16. 요약 & 체크리스트

CMOS 전력은 동적(α·C·V²·f), 단락, 누설로 나뉘고, 누설은 온도에 지수적으로 민감해서 방열이 약하면 열 폭주까지 갈 수 있다. 하드웨어는 네 가지로 전력을 줄인다: clock gating은 토글을 멈춰 동적 전력만 없애고 전환 비용이 거의 없다. power gating은 header/footer 스위치로 누설까지 없애지만 isolation·retention·순서 있는 시퀀스가 필요하고, 전환 비용의 대부분은 스위치가 아니라 상태 복구(특히 weight 재적재)다. 깊은 잠의 가치는 break-even 시간 E_trans / (P_A − P_B)와 wake latency로 판단한다. DVFS는 V²로 일당 에너지를 줄이지만 platform·누설·memory-bound·V_min·최소 에너지점 때문에 "가장 낮은 OPP"가 최적이 아닐 수 있고, 범용 governor는 deadline을 모르므로 런타임 hint가 필요하다. AVS는 칩별 여유를 회수한다. MCU는 WFI/WFE와 SLEEPDEEP 위에 벤더 PMU 모드를 얹고, always-on 예산은 센서 전류와 "언제 안 돌릴지"가 지배한다. PMIC 쪽에서 LDO 효율은 Vout/Vin을 못 넘고, µW 부하에서는 레귤레이터 Iq와 PFM 모드가 결정적이며, 피크 전류는 저온·저잔량에서 brown-out을 부른다. 열은 센서 → thermal zone → trip point → cooling device로 관리하고, 피부 온도는 virtual sensor로 추정하며, PI 같은 제어기는 지속 전력(ΔT/R_th)을 바꾸지 못하고 도달 방식만 부드럽게 한다.

- [ ] 누설의 온도 의존을 지수 모델로 계산하고 열 폭주 조건(교점 없음)을 그림으로 설명할 수 있다
- [ ] clock gating과 power gating이 각각 무엇을 없애고 무엇을 남기는지, 비용이 무엇인지 말할 수 있다
- [ ] isolation cell, level shifter, retention flop, SRAM retention, AON domain의 역할과 끄기/켜기 순서를 그릴 수 있다
- [ ] break-even 시간 E_trans / (P_A − P_B)를 손으로 계산하고 wake latency 제약을 더해 상태를 고를 수 있다
- [ ] 전환 에너지를 항목별로 분해해 weight 재적재가 지배함을 보이고 retention의 손익분기를 계산할 수 있다
- [ ] 왜 f를 올리려면 V를 올려야 하는지, OPP 표와 최소 에너지점이 무엇인지 설명할 수 있다
- [ ] governor 정책별 에너지·deadline 결과를 해석하고 "최저 OPP ≠ 최소 에너지"를 설명할 수 있다
- [ ] WFI/WFE/SLEEPDEEP/SLEEPONEXIT와 race 없는 sleep 진입 패턴을 코드로 쓸 수 있다
- [ ] IMU always-on 시나리오의 µA 예산을 항목별로 계산할 수 있다
- [ ] LDO 효율과 PWM/PFM buck의 저부하 효율 차이를 계산하고, 열 제어 루프(PI + anti-windup)를 설계·설명할 수 있다

## 참고 자료

- J. Rabaey, A. Chandrakasan, B. Nikolić, "Digital Integrated Circuits: A Design Perspective" (2nd ed.) — 동적·단락·누설 전력, 전원 스위치
- M. Keating, D. Flynn, R. Aitken, A. Gibbons, K. Shi, "Low Power Methodology Manual: For System-on-Chip Design", Springer, 2007 — clock gating, power gating, isolation, retention, 시퀀스
- IEEE 1801 (Unified Power Format) — power intent 기술 표준
- T. Sakurai, A. R. Newton, "Alpha-power law MOSFET model and its applications to CMOS inverter delay and other formulas", IEEE JSSC, 1990 — 5.1절 속도 모델
- B. H. Calhoun, A. Wang, A. Chandrakasan, "Modeling and sizing for minimum energy operation in subthreshold circuits", IEEE JSSC, 2005 — 최소 에너지점
- Arm, "Armv7-M / Armv8-M Architecture Reference Manual" — WFI, WFE, SCR(SLEEPDEEP, SLEEPONEXIT, SEVONPEND)
- J. Yiu, "The Definitive Guide to Arm Cortex-M3 and Cortex-M4 Processors" (3rd ed.) — Cortex-M 저전력 기능 실무
- Linux kernel documentation — [CPU performance scaling (cpufreq)](https://docs.kernel.org/admin-guide/pm/cpufreq.html), [CPU idle time management](https://docs.kernel.org/admin-guide/pm/cpuidle.html), thermal framework의 power allocator governor 문서
- Linux device tree bindings — `operating-points-v2` (OPP) 바인딩
- Android 문서 — Thermal HAL, `PowerManager` thermal status / thermal headroom API ([source.android.com](https://source.android.com))
- TI·Analog Devices 등의 레귤레이터 앱노트 — LDO 효율, PFM/PWM 동작, quiescent current (제품별 데이터시트로 수치 확인)
- IEC 62368-1 — 접촉 가능한 표면의 온도 한계 (D7 7.1절과 같은 주의: 원문 확인 필요)
- D7 노트 — C·V²·f, race-to-idle, 배터리 수학, 1차 열 RC, 측정 방법론
