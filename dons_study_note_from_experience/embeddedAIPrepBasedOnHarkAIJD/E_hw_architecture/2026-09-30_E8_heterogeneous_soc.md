# E8. 이기종 SoC 구조 — always-on island, 센서 허브, IPC, 누가 무엇을 돌리나

> **이 노트를 다 읽으면**: 웨어러블급 SoC + companion MCU의 블록도를 그리고 각 엔진(MCU·DSP·NPU·GPU·CPU·cloud)이 왜 거기 있는지 전력·지연·메모리 숫자로 설명할 수 있다 · 코어 사이 IPC(mailbox doorbell + 공유 메모리 링 + RPMsg/FastRPC류 프로토콜)를 C로 짜고 지연을 재고, 캐시·순서 함정을 짚을 수 있다 · VAD → KWS → ASR wake cascade를 전원 상태가 있는 이산 사건 시뮬레이션으로 모델링해 평균 전력과 wake 지연을 계산하고, false wake와 전이 비용이 어떻게 예산을 먹는지 말할 수 있다 · 부팅·SSR·OTA·크로스 코어 디버깅·무선 칩 공존까지 "시스템 전체"를 한 장으로 설명할 수 있다
> **JD 연결**: "Familiarity with embedded systems, **CPU/DSP/NPU HW architectures**", "Profile and optimize memory usage, **power consumption**, real-time performance", "Co-design model architectures that meet latency, memory, power, bandwidth" — study_prep_list **E8**: always-on island, sensor hub, low-power domain / IPC · mailbox · shared memory / MCU ↔ DSP ↔ NPU ↔ AP 역할 분담 (Hark: Qualcomm + Ambiq급 MCU 추정). 함께 닿는 행: **I3**(wake cascade), **I4**(전처리 배치), **L3·L4**(hybrid 라우팅, 음성 파이프라인), **N1·N4**(웨어러블 제약, always-listening UX), **J5**(OTA), **G2·G4·G7**(센서 인터페이스, 마이크, 시간 동기화)
> **Don 기준 난이도**: 여러 코어가 공유 메모리·doorbell로 대화하는 구조, 버스(PCIe·I2C·SPI·SPMI·RFFE) 장애 추적, 전원 시퀀스와 margin sign-off는 이미 몸에 있다 (SSD 컨트롤러 자체가 이기종 SoC다) / 새로 배울 것은 "ML 작업을 어느 엔진에 둘지"를 duty cycle·wake 비용으로 결정하는 사고법, 모바일 SoC의 always-on island·sensor hub 관례, Android/Qualcomm 쪽 IPC·서브시스템 이름들
> **선행 노트**: B5(always-listening 파이프라인, VAD·KWS), B7(always-on IMU 사다리, 센서 내장 기능), B9(cascade 기대 비용), D6(latency·꼬리·실시간), D7(에너지·배터리 수학). 병행 노트: E4(DSP), E5(NPU), E7(메모리·캐시 일관성), E9(전력·열)

---

## 0. 큰 그림 — 왜 칩 하나에 엔진이 여러 개인가

### 0.1 Don은 이미 이기종 SoC에서 일했다

엔터프라이즈 SSD 컨트롤러를 떠올려 보자. host 명령을 받는 Cortex-R 코어, FTL을 도는 또 다른 Cortex-R 코어들, 전원·온도를 보는 작은 M0+, NAND 데이터 경로를 만지는 Xtensa, 그리고 LDPC·암호화 같은 고정 기능 엔진. 이 코어들은 공유 SRAM의 큐와 doorbell 레지스터로 대화하고, 각자 다른 펌웨어 이미지를 가지며, 하나가 죽으면 전체가 멈추지 않게 설계한다.

웨어러블 AI 기기의 SoC도 **정확히 같은 구조**다. 다른 점은 딱 두 가지다.

1. 엔진 사이의 **전력 차이가 1000배 이상**이다. µW로 도는 always-on 코어와 W로 도는 AP·NPU가 한 기기에 같이 있다.
2. 일의 대부분이 **"아무 일도 없는 시간"**이다. 기기는 하루의 99% 이상을 "듣고 있지만 아무도 말하지 않는" 상태로 보낸다. 그래서 설계의 핵심은 "빠르게 계산하기"보다 **"비싼 엔진을 최대한 늦게, 최대한 짧게 깨우기"**다.

이기종(heterogeneous) SoC란 **성격이 다른 여러 종류의 처리 엔진을 한 칩(또는 한 보드)에 모은 것**이다. 같은 코어를 여러 개 넣는 homogeneous multi-core와 달리, 엔진마다 ISA·메모리·전원 도메인·펌웨어가 다르다.

### 0.2 가상의 웨어러블 SoC 한 장

아래 그림은 Hark 같은 기기라면 이런 모양일 것이라는 **추정**이다. 실제 Hark 하드웨어는 공개되지 않았다. Qualcomm 모바일·웨어러블 SoC의 일반적 구성(Cortex-A 클러스터 + Adreno GPU + Hexagon DSP/NPU + 저전력 sensing island)과 Ambiq급 always-on MCU를 합쳐 그린 교과서용 그림이다.

```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg">
<rect x="10" y="26" width="492" height="400" rx="10" fill="none" stroke="#4a7bd0" stroke-width="1.5" stroke-dasharray="7 4"/><text x="20" y="20" font-size="13">SoC (가상의 웨어러블급 SoC — Qualcomm급 구성을 흉내 낸 추정)</text><rect x="25" y="45" width="140" height="62" rx="6" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/><text x="95" y="70" font-size="13" text-anchor="middle">AP cluster</text><text x="95" y="88" font-size="12" text-anchor="middle">Cortex-A · Android</text><rect x="178" y="45" width="92" height="62" rx="6" fill="#888" fill-opacity="0.2" stroke="currentColor"/>
<text x="224" y="70" font-size="13" text-anchor="middle">GPU</text><text x="224" y="88" font-size="12" text-anchor="middle">그래픽 · 일부 ML</text><rect x="283" y="45" width="100" height="62" rx="6" fill="#d0564a" fill-opacity="0.2" stroke="currentColor"/><text x="333" y="70" font-size="13" text-anchor="middle">NPU</text><text x="333" y="88" font-size="12" text-anchor="middle">(예: Hexagon HTP)</text><rect x="396" y="45" width="94" height="62" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/><text x="443" y="70" font-size="13" text-anchor="middle">DSP</text><text x="443" y="88" font-size="12" text-anchor="middle">audio · compute</text>
<rect x="25" y="122" width="465" height="26" rx="5" fill="#888" fill-opacity="0.25" stroke="currentColor"/><text x="257" y="140" font-size="12" text-anchor="middle">interconnect / NoC — 코어·가속기·메모리를 잇는 on-chip 버스 망</text><rect x="25" y="162" width="150" height="46" rx="6" fill="none" stroke="currentColor"/><text x="100" y="182" font-size="12" text-anchor="middle">secure boot · crypto</text><text x="100" y="198" font-size="12" text-anchor="middle">(boot ROM, 키)</text><rect x="190" y="162" width="145" height="46" rx="6" fill="none" stroke="currentColor"/><text x="262" y="190" font-size="12" text-anchor="middle">system cache (공유)</text>
<rect x="350" y="162" width="140" height="46" rx="6" fill="none" stroke="currentColor"/><text x="420" y="190" font-size="12" text-anchor="middle">LPDDR 컨트롤러</text><rect x="25" y="228" width="465" height="186" rx="8" fill="#3f9a6b" fill-opacity="0.08" stroke="#3f9a6b" stroke-width="1.5" stroke-dasharray="6 3"/><text x="35" y="246" font-size="12">always-on / low-power island — 자체 전원·클럭 도메인, SoC 나머지가 꺼져도 산다</text><rect x="40" y="258" width="135" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="107" y="282" font-size="12" text-anchor="middle">sensor/audio</text><text x="107" y="299" font-size="12" text-anchor="middle">저전력 DSP · MCU</text>
<rect x="188" y="258" width="100" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.15" stroke="currentColor"/><text x="238" y="282" font-size="12" text-anchor="middle">AON SRAM</text><text x="238" y="299" font-size="12" text-anchor="middle">(retention)</text><rect x="301" y="258" width="175" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.15" stroke="currentColor"/><text x="388" y="282" font-size="12" text-anchor="middle">mailbox · AON timer</text><text x="388" y="299" font-size="12" text-anchor="middle">power controller</text><rect x="40" y="330" width="135" height="66" rx="6" fill="none" stroke="currentColor"/>
<text x="107" y="355" font-size="12" text-anchor="middle">I2C · I3C · SPI</text><text x="107" y="372" font-size="12" text-anchor="middle">센서 버스 마스터</text><rect x="188" y="330" width="100" height="66" rx="6" fill="none" stroke="currentColor"/><text x="238" y="355" font-size="12" text-anchor="middle">PDM 입력</text><text x="238" y="372" font-size="12" text-anchor="middle">decimator</text><rect x="301" y="330" width="175" height="66" rx="6" fill="none" stroke="currentColor"/><text x="388" y="355" font-size="12" text-anchor="middle">GPIO wake · IRQ</text><text x="388" y="372" font-size="12" text-anchor="middle">UART/SPI (외부 MCU)</text>
<rect x="520" y="40" width="150" height="80" rx="6" fill="#e08a3c" fill-opacity="0.15" stroke="currentColor"/><text x="595" y="62" font-size="12" text-anchor="middle">무선 칩들</text><text x="595" y="80" font-size="12" text-anchor="middle">modem · Wi-Fi/BT</text><text x="595" y="98" font-size="12" text-anchor="middle">GNSS · UWB · NFC</text><rect x="520" y="160" width="150" height="46" rx="6" fill="#d0564a" fill-opacity="0.15" stroke="currentColor"/><text x="595" y="188" font-size="12" text-anchor="middle">LPDDR DRAM</text><rect x="520" y="228" width="150" height="46" rx="6" fill="#888" fill-opacity="0.15" stroke="currentColor"/>
<text x="595" y="256" font-size="12" text-anchor="middle">PMIC (여러 rail)</text><rect x="520" y="292" width="150" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="595" y="316" font-size="12" text-anchor="middle">companion MCU</text><text x="595" y="333" font-size="12" text-anchor="middle">(Ambiq급 추정)</text><rect x="520" y="372" width="150" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.15" stroke="currentColor"/><text x="595" y="400" font-size="12" text-anchor="middle">mic · IMU · 기타 센서</text><line x1="520" y1="110" x2="490" y2="135" stroke="currentColor"/><text x="508" y="150" font-size="12" text-anchor="middle">PCIe</text>
<line x1="490" y1="185" x2="520" y2="183" stroke="currentColor" stroke-width="2"/><line x1="490" y1="258" x2="520" y2="251" stroke="currentColor"/><text x="505" y="246" font-size="12" text-anchor="middle">SPMI</text><line x1="476" y1="360" x2="520" y2="325" stroke="currentColor"/><polyline points="107,396 107,406 520,406" fill="none" stroke="currentColor" stroke-dasharray="3 3"/><line x1="595" y1="372" x2="595" y2="348" stroke="currentColor"/>
</svg>
```

그림 1 — 가상의 웨어러블 SoC와 주변 칩. 파란 점선 안이 SoC 한 개, 초록 점선이 SoC 안의 always-on island(자체 전원 도메인), 오른쪽은 칩 밖의 이웃들(무선 칩, DRAM, PMIC, companion MCU, 센서). 굵은 선은 대역폭이 큰 경로(DRAM), 가는 선은 제어·저속 경로다.

블록을 하나씩 읽어 보자.

| 블록 | 하는 일 | 대략의 전력 규모 | Don이 아는 것에 비유하면 |
|---|---|---|---|
| AP cluster (Cortex-A) | Android, 앱, 네트워크 스택, 무거운 제어 | 수십~수천 mW | SSD의 host interface 코어 + OS |
| GPU | 그래픽, 일부 ML (batch가 큰 연산) | 수백 mW~W | 없음 (E6) |
| NPU (예: Hexagon HTP) | 큰 신경망 추론 (ASR, SLM, 비전) | 수십 mW~W | LDPC 같은 고정 기능 가속기의 "프로그래머블 판" |
| DSP (audio · compute) | 오디오 전처리, 중간 크기 모델 | 수 mW~수십 mW | Xtensa 데이터 경로 코어 |
| always-on island | 센서 허브, VAD, wake 판단 | µW~1 mW대 | 전원·온도 관리 M0+ |
| interconnect / NoC | 모든 블록을 잇는 on-chip 버스 망 | (각 블록에 포함) | AXI 버스 매트릭스 |
| companion MCU (칩 밖) | always-on 작업을 SoC 밖에서 | µW~mW | 별도 보드 관리 MCU (BMC) |

말로 하면: **싼 엔진은 항상 켜 두고, 비싼 엔진은 싼 엔진이 "지금 필요해"라고 할 때만 깨운다.** 이 한 줄이 이 노트 전체다.

### 0.3 왜 한 엔진으로 다 하지 않나 — 숫자로

직관: 트럭 한 대로 편의점 심부름을 가면 기름을 낭비하고, 자전거로 이사를 하면 영원히 끝나지 않는다. 엔진마다 **"켜 두는 바닥 전력"**, **"일할 때 전력"**, **"깨우는 비용"**이 다르기 때문에, 일이 얼마나 자주 오느냐(duty cycle)에 따라 최적의 엔진이 달라진다.

손계산부터. 작은 KWS 추론 1회를 세 엔진에서 돌린다고 하자 (숫자는 모두 설명용 가정).

```
MCU: 5 mW × 12 ms = 60 µJ/회,  바닥 0.1 mW,  깨우기 비용 0 (원래 켜져 있음)
DSP: 20 mW × 1 ms = 20 µJ/회,  켜 두면 1.5 mW, 재우면 0.2 mW + 깨울 때마다 0.3 mJ
AP : 600 mW × 0.4 ms = 240 µJ/회, 켜 두면 120 mW, 재우면 3 mW + 깨울 때마다 40 mJ

초당 1회일 때
  MCU: 0.1 + 1 × 0.060 mJ/s ≈ 0.16 mW
  DSP: min(켜 둠 1.5 + 0.02, 재움 0.2 + 0.02 + 0.3) ≈ 0.52 mW
  AP : 재움 3 + 0.24 + 40 ≈ 43 mW
```

말로 하면: 1회당 에너지는 DSP가 MCU보다 3배 싸지만, 초당 1회 정도로 드물면 DSP의 **바닥 전력과 깨우기 비용**이 그 이득을 삼켜 버린다. AP는 1회 에너지도 비싸고, 깨우기 40 mJ가 모든 것을 지배한다.

**예제 1** — 무엇을 확인하나: 위 계산을 호출 빈도 0.01~100 Hz에 걸쳐 반복해, 빈도마다 어느 엔진이 가장 싼지 본다. 엔진마다 "계속 켜 둔다"와 "매번 잤다 깬다" 두 정책 중 더 싼 쪽을 고른다.

```python
# 같은 작은 작업(작은 KWS 추론 1회)을 세 엔진에서 돌릴 때, 호출 빈도에 따른 평균 전력 (모든 숫자는 설명용 가정)
engines = {  #        켜둔 바닥(mW) 실행 전력(mW) 1회 실행(ms) 깨우기 에너지(mJ) 잘 때(mW)
    "MCU": dict(p_on=0.1,  p_run=5.0,   t_run=12.0, e_wake=0.0,  p_sleep=0.1),
    "DSP": dict(p_on=1.5,  p_run=20.0,  t_run=1.0,  e_wake=0.3,  p_sleep=0.2),
    "AP":  dict(p_on=120., p_run=600.0, t_run=0.4,  e_wake=40.0, p_sleep=3.0),
}
def avg_mw(e, rate_hz):
    busy = rate_hz * e["t_run"] / 1000                      # 실행 중인 시간 비율
    if busy >= 1: return float("inf")                       # 못 따라간다
    run = busy * e["p_run"]
    stay_on = run + (1 - busy) * e["p_on"]                  # 정책 A: 계속 켜 둔다
    sleep = run + (1 - busy) * e["p_sleep"] + rate_hz * e["e_wake"]  # 정책 B: 매번 잤다 깬다 (mJ/s = mW)
    return min(stay_on, sleep)
rates = [0.01, 0.1, 1, 10, 30, 50, 100]
print("rate(Hz) " + "".join(f"{n:>10}" for n in engines) + "   best")
for r in rates:
    vals = {n: avg_mw(e, r) for n, e in engines.items()}
    print(f"{r:>8} " + "".join(f"{v:>10.3f}" for v in vals.values()) + f"   {min(vals, key=vals.get)}")
r = next(x / 10 for x in range(1, 1000) if avg_mw(engines["DSP"], x / 10) < avg_mw(engines["MCU"], x / 10))
print(f"DSP가 MCU를 이기기 시작하는 호출 빈도 ≈ {r} Hz")
```

```text
rate(Hz)        MCU       DSP        AP   best
    0.01      0.101     0.203     3.402   MCU
     0.1      0.106     0.232     7.024   MCU
       1      0.159     0.520    43.239   MCU
      10      0.688     1.685   121.920   MCU
      30      1.864     2.055   125.760   MCU
      50      3.040     2.425   129.600   DSP
     100        inf     3.350   139.200   DSP
DSP가 MCU를 이기기 시작하는 호출 빈도 ≈ 34.8 Hz
```

출력에서 볼 것: 드문 작업(≤ 30 Hz)은 **MCU**가, MCU가 버거워지는 빈번한 작업(≈ 35 Hz 이상)은 **DSP**가 이긴다. **AP는 어떤 빈도에서도 이기지 못한다** — 작은 작업을 AP로 하면 깨우기 비용과 바닥 전력만 수십 mW다. 100 Hz에서 MCU는 `inf`, 즉 12 ms짜리 작업을 10 ms마다 해야 해서 따라가지 못한다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<line x1="80" y1="270" x2="620" y2="270" stroke="currentColor"/><line x1="80" y1="30" x2="80" y2="270" stroke="currentColor"/><line x1="80.0" y1="270" x2="80.0" y2="275" stroke="currentColor"/><text x="80.0" y="290" font-size="12" text-anchor="middle">0.01</text><line x1="215.0" y1="270" x2="215.0" y2="275" stroke="currentColor"/><text x="215.0" y="290" font-size="12" text-anchor="middle">0.1</text><line x1="350.0" y1="270" x2="350.0" y2="275" stroke="currentColor"/><text x="350.0" y="290" font-size="12" text-anchor="middle">1</text><line x1="485.0" y1="270" x2="485.0" y2="275" stroke="currentColor"/><text x="485.0" y="290" font-size="12" text-anchor="middle">10</text>
<line x1="620.0" y1="270" x2="620.0" y2="275" stroke="currentColor"/><text x="620.0" y="290" font-size="12" text-anchor="middle">100</text><line x1="75" y1="270.0" x2="80" y2="270.0" stroke="currentColor"/><text x="70" y="274.0" font-size="12" text-anchor="end">0.1</text><line x1="80" y1="270.0" x2="620" y2="270.0" stroke="currentColor" stroke-opacity="0.12"/><line x1="75" y1="210.0" x2="80" y2="210.0" stroke="currentColor"/><text x="70" y="214.0" font-size="12" text-anchor="end">1</text><line x1="80" y1="210.0" x2="620" y2="210.0" stroke="currentColor" stroke-opacity="0.12"/>
<line x1="75" y1="150.0" x2="80" y2="150.0" stroke="currentColor"/><text x="70" y="154.0" font-size="12" text-anchor="end">10</text><line x1="80" y1="150.0" x2="620" y2="150.0" stroke="currentColor" stroke-opacity="0.12"/><line x1="75" y1="90.0" x2="80" y2="90.0" stroke="currentColor"/><text x="70" y="94.0" font-size="12" text-anchor="end">100</text><line x1="80" y1="90.0" x2="620" y2="90.0" stroke="currentColor" stroke-opacity="0.12"/><line x1="75" y1="30.0" x2="80" y2="30.0" stroke="currentColor"/><text x="70" y="34.0" font-size="12" text-anchor="end">1000</text><line x1="80" y1="30.0" x2="620" y2="30.0" stroke="currentColor" stroke-opacity="0.12"/>
<polyline points="80.0,269.8 93.5,269.8 107.0,269.8 120.5,269.7 134.0,269.6 147.5,269.5 161.0,269.4 174.5,269.2 188.0,269.1 201.5,268.8 215.0,268.5 228.5,268.1 242.0,267.7 255.5,267.1 269.0,266.4 282.5,265.6 296.0,264.5 309.5,263.3 323.0,261.8 336.5,260.0 350.0,257.9 363.5,255.6 377.0,252.8 390.5,249.8 404.0,246.4 417.5,242.6 431.0,238.6 444.5,234.2 458.0,229.6 471.5,224.8 485.0,219.7 498.5,214.5 512.0,209.2 525.5,203.7 539.0,198.1 552.5,192.5 566.0,186.7 579.5,181.0 593.0,175.1 606.5,169.3" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<polyline points="80.0,251.5 93.5,251.4 107.0,251.3 120.5,251.1 134.0,250.9 147.5,250.7 161.0,250.3 174.5,249.9 188.0,249.4 201.5,248.8 215.0,248.1 228.5,247.2 242.0,246.1 255.5,244.7 269.0,243.1 282.5,241.3 296.0,239.1 309.5,236.6 323.0,233.8 336.5,230.6 350.0,227.0 363.5,223.2 377.0,219.0 390.5,214.6 404.0,209.9 417.5,205.0 431.0,199.9 444.5,197.9 458.0,197.5 471.5,197.0 485.0,196.4 498.5,195.7 512.0,194.8 525.5,193.7 539.0,192.4 552.5,190.9 566.0,189.0 579.5,186.9 593.0,184.4 606.5,181.6 620.0,178.5" fill="none" stroke="#e08a3c" stroke-width="2"/>
<polyline points="80.0,178.1 93.5,177.3 107.0,176.4 120.5,175.2 134.0,173.8 147.5,172.2 161.0,170.2 174.5,168.0 188.0,165.4 201.5,162.5 215.0,159.2 228.5,155.6 242.0,151.7 255.5,147.4 269.0,142.9 282.5,138.2 296.0,133.2 309.5,128.1 323.0,122.8 336.5,117.4 350.0,111.8 363.5,106.2 377.0,100.5 390.5,94.8 404.0,89.0 417.5,85.1 431.0,85.1 444.5,85.0 458.0,85.0 471.5,84.9 485.0,84.8 498.5,84.7 512.0,84.6 525.5,84.4 539.0,84.2 552.5,84.0 566.0,83.6 579.5,83.2 593.0,82.7 606.5,82.1 620.0,81.4" fill="none" stroke="#d0564a" stroke-width="2"/><text x="120.6" y="262.0" font-size="13">MCU</text><text x="120.6" y="243.9" font-size="13">DSP</text><text x="120.6" y="169.4" font-size="13">AP</text>
<line x1="558.1" y1="40" x2="558.1" y2="270" stroke="currentColor" stroke-dasharray="4 4"/><text x="554.1" y="52" font-size="12" text-anchor="end">≈35 Hz: DSP가 역전</text><text x="613.3" y="198.1" font-size="12">MCU 포화</text><text x="350" y="318" font-size="12" text-anchor="middle">호출 빈도 [Hz] (log)</text><text x="20" y="22" font-size="12">평균 전력 [mW] (log)</text>
</svg>
```

그림 2 — 예제 1의 결과를 log-log로 그린 것. 호출 빈도가 올라가면 MCU 곡선이 가파르게 오르다 약 83 Hz에서 끊긴다(포화). DSP는 바닥이 높지만 기울기가 완만해 약 35 Hz에서 역전한다. AP 곡선은 전 구간에서 두 자릿수 위에 있다.

이 그림이 "왜 이기종인가"에 대한 답이다. **하나의 엔진이 모든 duty cycle에서 최적일 수 없다.** 그래서 칩 설계자는 duty cycle 구간마다 다른 엔진을 둔다.

### 0.4 전원 도메인과 island — 용어 정리

- **power domain(전원 도메인)**: 같은 스위치로 함께 켜고 끄는 회로 묶음. 도메인마다 독립적으로 전원을 끊을 수 있다(power gating).
- **clock domain(클럭 도메인)**: 같은 클럭을 쓰는 회로 묶음. 클럭만 멈추면(clock gating) 동적 전력은 0에 가까워지지만 누설 전력은 남는다 (E9, D7 1.3절).
- **retention**: 전원을 거의 끊되 SRAM·레지스터 내용만 유지하는 저전압 상태. 깨어날 때 다시 로드할 필요가 없어서 wake가 빠르다.
- **always-on (AON) island**: SoC의 나머지가 전부 꺼져도 살아 있는 작은 전원 도메인. 자체 저전력 코어, 작은 SRAM, 타이머, wake 로직, 센서 버스 컨트롤러를 가진다. Qualcomm 문서에서는 SLPI(Sensor Low Power Island), sensing hub 같은 이름이 보이는데, 세대마다 이름과 구성이 다르므로 구체 사양은 데이터시트로 확인해야 한다.

```
전원 상태 사다리 (위로 갈수록 전력↑, 깨우는 시간↓)

  ACTIVE      클럭 on, 전원 on           전력 100%    깨우기 0
  IDLE (WFI)  클럭 gating                 수 %~수십 %   ~µs
  RETENTION   전압 ↓, SRAM 유지           ~0.1~1 %     수십 µs~수 ms
  OFF         전원 차단, 상태 소실          ~0           수 ms~수백 ms (+ 펌웨어 재로드)
```

말로 하면: 깊이 잘수록 전기는 덜 쓰지만 깨어나는 데 오래 걸리고, 깨어나는 동안에도 에너지를 쓴다. 이 **전이 비용(transition cost)** 이 5절 시뮬레이션의 주인공이다.

흔한 함정: "이 코어는 sleep이니까 0 W"라고 계산하는 것. 실제로는 도메인이 retention인지 off인지, 그 도메인의 SRAM이 몇 KB인지, PLL·LDO가 살아 있는지에 따라 바닥 전력이 10배씩 달라진다. Don이 sign-off 때 rail별로 전류를 찍어 본 것처럼, **엔진별·상태별 바닥 전력표**가 모든 분석의 출발점이다.

---

## 1. Always-on island와 센서 허브

### 1.1 직관 — 건물의 경비실

큰 건물(SoC)은 밤이 되면 불을 다 끈다. 하지만 경비실(always-on island)은 24시간 불이 켜져 있다. 경비원은 CCTV(센서)를 계속 보다가, 진짜 일이 생겼을 때만 담당자(AP)에게 전화를 건다. 경비원이 전화를 너무 자주 걸면(false wake) 담당자는 잠을 못 자고, 너무 안 걸면(miss) 사고를 놓친다.

### 1.2 센서 허브(sensor hub)

**sensor hub**란 센서들이 붙은 버스를 저전력 코어가 **소유**하고, 샘플 수집·버퍼링·간단한 처리를 대신 해 주는 구조다. AP는 센서를 직접 만지지 않고, 허브가 넘겨 주는 이벤트나 묶음(batch)만 받는다.

- 버스 소유: IMU·기압계·PPG 같은 센서는 I2C, I3C, SPI 중 하나로 허브 코어에 붙는다. 버스 master가 허브이므로 AP가 자고 있어도 센서를 읽을 수 있다.
- **I3C**: MIPI가 만든 I2C의 후속 규격. 2선이고 기존 I2C 장치와 한 버스에 공존할 수 있으며, 더 빠르고(SDR 12.5 MHz), 별도 INT 핀 없이 장치가 버스 위로 인터럽트를 올리는 **in-band interrupt(IBI)** 를 지원한다. 웨어러블처럼 핀이 귀한 곳에서 유리하다. (구체적으로 어떤 센서·SoC가 I3C를 쓰는지는 제품마다 다르다.)
- batching: 센서 FIFO + 허브 SRAM에 샘플을 모아 두었다가 한 번에 넘긴다. AP를 깨우는 횟수가 batch 크기에 반비례한다.
- 허브 안 처리: 걸음 수, 제스처 후보, 착용 감지처럼 작은 모델은 허브에서 끝낸다(B7 11.3절 사다리의 stage 1).
- Android 쪽에는 이런 허브에서 작은 앱("nanoapp")을 돌리는 **CHRE(Context Hub Runtime Environment)** 라는 공개 프레임워크가 있다. Hark가 이것을 쓰는지는 알 수 없지만, "Android 기기의 sensor hub" 하면 떠올릴 이름이다.

**batching이 얼마나 중요한가** — 손계산: IMU 100 Hz를 샘플마다 AP에 넘기면 AP는 시간당 36만 번 깨어야 한다. AP 한 번 깨우고 재우는 비용이 45 mJ라면 이것은 불가능하다(깨우는 시간이 샘플 간격보다 길다 → 사실상 계속 깨어 있음). 1분치를 모아 넘기면 시간당 60번, 60 × 45 mJ / 3600 s = 0.75 mW.

**예제 2** — 무엇을 확인하나: batch 크기를 1에서 6000까지 바꿀 때 AP wake 횟수·평균 전력·최악 지연·버퍼 크기가 어떻게 교환되는지.

```python
# 센서 허브 batching: IMU 100 Hz 샘플을 AP에 몇 개씩 모아 넘길까 (숫자는 설명용 가정)
ODR = 100            # IMU 샘플/s
HUB_MW = 0.15        # 허브 MCU가 샘플을 받아 FIFO에 쌓는 평균 전력
E_WAKE_AP = 45.0     # AP 한 번 깨우고 재우는 에너지 [mJ] (resume + tail + suspend)
E_PER_SAMPLE_AP = 0.02  # 깨어 있는 AP가 샘플 1개 처리 [mJ]
AP_SUSPEND_MW = 3.0
print(" batch   AP wakes/h   avg mW   AP 쪽 비율   최악 지연(s)")
for batch in [1, 10, 100, 1000, 6000]:
    wakes_per_s = ODR / batch
    if wakes_per_s * 0.3 > 1:                       # 깨우기 1번에 ~0.3 s: 이보다 자주면 AP는 사실상 계속 깨어 있다
        ap_mw = 150.0 + ODR * E_PER_SAMPLE_AP       # 깨어 있는 AP idle 전력 + 처리
    else:
        ap_mw = AP_SUSPEND_MW + wakes_per_s * E_WAKE_AP + ODR * E_PER_SAMPLE_AP
    total = HUB_MW + ap_mw
    print(f"{batch:>6} {wakes_per_s*3600:>12.0f} {total:>8.2f} {ap_mw/total:>11.1%} {batch/ODR:>12.2f}")
# FIFO 크기: 6축 × int16 = 12 B/샘플
print("batch 6000 에 필요한 버퍼 =", 6000 * 12 // 1024, "KiB")
# 대안: 허브에서 직접 처리(걸음 수·제스처 판정 +0.2 mW 가정)하고 AP는 다른 일로 깼을 때 결과만 가져간다
hub_only = HUB_MW + 0.2 + AP_SUSPEND_MW
print(f"허브에서 처리, AP는 안 깨움: {hub_only:.2f} mW  (AP suspend 바닥 {AP_SUSPEND_MW} mW 포함)")
```

```text
 batch   AP wakes/h   avg mW   AP 쪽 비율   최악 지연(s)
     1       360000   152.15       99.9%         0.01
    10        36000   152.15       99.9%         0.10
   100         3600    50.15       99.7%         1.00
  1000          360     9.65       98.4%        10.00
  6000           60     5.90       97.5%        60.00
batch 6000 에 필요한 버퍼 = 70 KiB
허브에서 처리, AP는 안 깨움: 3.35 mW  (AP suspend 바닥 3.0 mW 포함)
```

출력에서 볼 것: batch 1·10에서는 AP가 사실상 계속 깨어 있어 152 mW. batch를 6000(1분)으로 키우면 5.9 mW까지 떨어지지만 대가로 **지연이 60초**가 되고 버퍼가 70 KiB 필요하다(허브 SRAM 예산, E7). 끝 줄: 아예 허브에서 처리하고 AP를 깨우지 않으면 3.35 mW, 그중 3 mW는 AP가 suspend 상태로 누워 있는 바닥이다. 즉 이 시점부터 줄일 것은 **AP의 suspend 바닥**이고, 그것은 SoC 선택(또는 companion MCU 구조, 2절)의 문제가 된다.

함정: batch가 크면 "지금 넘어졌다" 같은 긴급 이벤트가 60초 늦게 도착한다. 그래서 실제 허브는 **두 경로**를 둔다 — 대량 데이터는 batch로, 급한 이벤트(제스처 확정, 낙상, wake word)는 즉시 인터럽트로.

### 1.3 오디오 front-end — mic에서 wake까지

B5에서 always-listening 파이프라인의 모델 쪽을 봤다. 여기서는 **하드웨어 경로**를 본다.

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="e8a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10" y="24" font-size="13">오디오 경로</text><rect x="10" y="36" width="95" height="54" rx="6" fill="#4a7bd0" fill-opacity="0.15" stroke="currentColor"/><text x="57" y="58" font-size="12" text-anchor="middle">MEMS mic</text><text x="57" y="76" font-size="12" text-anchor="middle">PDM 1-bit</text><rect x="120" y="36" width="95" height="54" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/>
<text x="167" y="58" font-size="12" text-anchor="middle">decimation</text><text x="167" y="76" font-size="12" text-anchor="middle">CIC+FIR→16 kHz</text><rect x="230" y="36" width="95" height="54" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="277" y="58" font-size="12" text-anchor="middle">pre-roll 링</text><text x="277" y="76" font-size="12" text-anchor="middle">AON SRAM 1–2 s</text><rect x="340" y="36" width="95" height="54" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="387" y="58" font-size="12" text-anchor="middle">VAD</text><text x="387" y="76" font-size="12" text-anchor="middle">LP core · MCU</text>
<rect x="450" y="36" width="95" height="54" rx="6" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="497" y="58" font-size="12" text-anchor="middle">KWS</text><text x="497" y="76" font-size="12" text-anchor="middle">audio DSP</text><rect x="560" y="36" width="110" height="54" rx="6" fill="#d0564a" fill-opacity="0.25" stroke="currentColor"/><text x="615" y="58" font-size="12" text-anchor="middle">AP · NPU</text><text x="615" y="76" font-size="12" text-anchor="middle">ASR · LLM</text><line x1="105" y1="63" x2="118" y2="63" stroke="currentColor" marker-end="url(#e8a)"/><line x1="215" y1="63" x2="228" y2="63" stroke="currentColor" marker-end="url(#e8a)"/>
<line x1="325" y1="63" x2="338" y2="63" stroke="currentColor" marker-end="url(#e8a)"/><line x1="435" y1="63" x2="448" y2="63" stroke="currentColor" marker-end="url(#e8a)"/><line x1="545" y1="63" x2="558" y2="63" stroke="currentColor" marker-end="url(#e8a)"/><text x="442" y="108" font-size="12" text-anchor="middle">"말소리!" → DSP 깨움</text><text x="552" y="124" font-size="12" text-anchor="middle">"키워드!" → mailbox IRQ</text><path d="M277,90 L277,142 L615,142 L615,92" fill="none" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#e8a)"/><text x="400" y="158" font-size="12" text-anchor="middle">깨어난 AP가 pre-roll을 읽어 키워드 앞부분부터 ASR (소리를 잃지 않는다)</text>
<text x="10" y="186" font-size="13">센서 허브 경로</text><rect x="10" y="198" width="140" height="54" rx="6" fill="#4a7bd0" fill-opacity="0.15" stroke="currentColor"/><text x="80" y="220" font-size="12" text-anchor="middle">IMU · 기압 · PPG</text><text x="80" y="238" font-size="12" text-anchor="middle">FIFO · 내장 기능</text><rect x="230" y="198" width="205" height="54" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="332" y="220" font-size="12" text-anchor="middle">sensor hub (LP core · MCU)</text><text x="332" y="238" font-size="12" text-anchor="middle">batching · 특징 · 작은 tree/CNN</text>
<rect x="560" y="198" width="110" height="54" rx="6" fill="#d0564a" fill-opacity="0.25" stroke="currentColor"/><text x="615" y="220" font-size="12" text-anchor="middle">AP</text><text x="615" y="238" font-size="12" text-anchor="middle">앱 · 로깅</text><line x1="150" y1="225" x2="228" y2="225" stroke="currentColor" marker-end="url(#e8a)"/><text x="190" y="216" font-size="12" text-anchor="middle">I2C/I3C/SPI</text><text x="190" y="270" font-size="12" text-anchor="middle">+ INT (watermark)</text><line x1="435" y1="225" x2="558" y2="225" stroke="currentColor" marker-end="url(#e8a)"/>
<text x="497" y="216" font-size="12" text-anchor="middle">이벤트 · batch</text><text x="497" y="270" font-size="12" text-anchor="middle">(가끔만)</text><line x1="10" y1="290" x2="670" y2="290" stroke="currentColor" stroke-opacity="0.4"/><text x="120" y="305" font-size="12" text-anchor="middle">항상 켜짐: µW ~ 1 mW</text><text x="440" y="305" font-size="12" text-anchor="middle">가끔: 수 mW</text><text x="615" y="305" font-size="12" text-anchor="middle">드물게: 수백 mW</text>
</svg>
```

그림 3 — always-on 경로 두 개. 위: MEMS 마이크의 PDM 비트열이 decimation을 거쳐 16 kHz PCM이 되고, pre-roll 링버퍼에 쌓이면서 VAD가 보고, 말소리면 DSP의 KWS를 깨우고, 키워드면 AP를 깨운다. 아래: IMU FIFO가 watermark 인터럽트로 허브를 깨우고, 허브는 가끔만 AP에 이벤트나 batch를 넘긴다. 맨 아래 줄은 오른쪽으로 갈수록 드물게, 비싸게 켜진다는 뜻이다.

단계별로:

1. **PDM (Pulse Density Modulation)**: 디지털 MEMS 마이크는 보통 1 MHz~3 MHz 클럭에 맞춰 1비트 스트림을 내보낸다. "1의 밀도"가 소리 크기다. (G4)
2. **decimation**: CIC 필터 + FIR로 걸러 내며 샘플 수를 줄인다. 예: 1.024 MHz PDM ÷ 64 = 16 kHz PCM. 이 블록은 보통 **전용 하드웨어**(island의 PDM 인터페이스, 또는 codec)라서 코어를 깨우지 않는다.
3. **pre-roll 링버퍼**: 16 kHz × 16 bit = 32 KB/s. 2초면 64 KB. AP가 깨어나는 동안(수백 ms) 키워드 앞부분을 잃지 않으려면 **이미 지나간 소리**를 들고 있어야 한다. Don이 SSD에서 쓰던 DMA 링버퍼 그대로다.
4. **VAD**: 에너지·스펙트럼 기반 작은 판별기(B5 2절). µW~수백 µW. island 코어나 companion MCU, 일부 codec·마이크는 하드웨어 VAD를 내장하기도 한다.
5. **KWS**: DSP(또는 강한 MCU)에서 작은 CNN(B5 3절). VAD가 문을 열 때만 돈다.
6. **wake**: KWS가 확신하면 mailbox 인터럽트로 AP를 깨운다. AP는 pre-roll을 읽어서 2차 검증 + streaming ASR을 시작한다.

**SoundWire**: MIPI가 만든 2선(clock + data) 멀티드롭 오디오 버스로, codec·스피커 앰프·마이크를 한 버스에 여러 개 달 수 있고 제어와 오디오를 같은 선으로 보낸다. 최근 모바일 플랫폼 일부가 쓴다. 웨어러블에서 PDM 직결을 쓸지, codec + I2S/TDM을 쓸지, SoundWire를 쓸지는 마이크 수·핀 수·codec 유무에 따라 다르다 — Hark 구성은 알 수 없다.

### 1.4 센서 스스로도 "작은 island"다

B7 11.3절에서 봤듯이 요즘 IMU는 **wake-on-motion, 걸음 수, tap 검출, 심지어 작은 decision tree(예: ST의 MLC)** 를 칩 안에서 돌린다. 마이크 쪽에도 음성 활동 검출을 내장한 제품이 있다. 즉 전력 사다리의 맨 아래 칸은 **센서 칩 자신**이다.

```
stage 0: 센서 내장 로직       ~µW      "움직였다" / "소리가 크다"
stage 1: island 코어 · MCU    ~0.1–1 mW "제스처 후보" / "말소리다"
stage 2: DSP                  ~수~수십 mW "키워드다" / "이 제스처다"
stage 3: AP + NPU             ~수백 mW  "무슨 말인지 이해하고 답하기"
stage 4: cloud                (기기 밖)  "큰 LLM"
```

### 1.5 AP를 깨운다는 것의 실체

"AP를 깨운다"는 SoC 내부에서는 대략 이런 일이다 (Android/Linux 기준, 세부는 벤더마다 다름):

1. island가 AP의 wake source로 등록된 인터럽트(mailbox IRQ 또는 GPIO)를 올린다.
2. 전원 컨트롤러가 AP 클러스터·DRAM을 self-refresh에서 깨우고 클럭을 올린다.
3. kernel이 suspend 경로를 거꾸로 돌며 드라이버들을 resume 한다.
4. 인터럽트 핸들러 → 드라이버 → 사용자 공간 서비스까지 이벤트가 올라간다. 이 서비스가 다시 잠들지 않도록 **wakelock**을 잡는다.
5. 일이 끝나면 wakelock을 놓고, 일정 시간 뒤 다시 suspend.

이 과정은 수십~수백 ms가 걸리고 그동안 수백 mW를 쓴다. 5절 시뮬레이션의 `resume` 상태(300 mW × 150 ms ≈ 45 mJ)와 `tail` 상태가 이것이다.

### 1.6 흔한 함정

- **pre-roll이 없다**: AP가 깨어난 뒤부터 녹음하면 "Hey Hark, 오늘 날씨" 중 "오늘"이 잘린다. 링버퍼 길이 ≥ (KWS 판정 지연 + AP resume 지연 + 여유).
- **허브 SRAM 예산 무시**: pre-roll 64 KB + IMU batch 70 KB + KWS 모델·arena 100 KB면 이미 수백 KB다. island SRAM은 보통 수백 KB~수 MB 규모라 금방 찬다.
- **센서 설정을 AP가 바꾼다**: AP 드라이버와 허브 펌웨어가 같은 센서 레지스터를 만지면 경쟁이 생긴다. 버스 소유자를 하나로 정하고, AP는 허브에 "설정 요청 메시지"만 보낸다.

---

## 2. Companion MCU vs 통합 island

### 2.1 두 구조

```
(A) 통합 island                          (B) companion MCU
┌──────────── SoC ────────────┐          ┌─────── SoC ───────┐      ┌─ MCU (Ambiq급) ─┐
│ AP  NPU  DSP                │          │ AP  NPU  DSP      │ SPI  │ Cortex-M        │
│   ── NoC ──                 │          │   ── NoC ──       │◄────►│ SRAM · flash    │
│ ┌ AON island ┐ 공유 메모리  │          │                   │ IRQ  │ 센서 버스 소유  │
│ │ LP core    │ + mailbox     │          │ (SoC 전체를 완전히 │◄──── │ VAD · 제스처    │
│ └────────────┘              │          │  끌 수도 있다)     │ wake │                 │
└─────────────────────────────┘          └───────────────────┘      └─────────────────┘
```

Hark의 경우 "Qualcomm SoC + Ambiq급 always-on MCU" 조합으로 **추정**된다. 이것이 사실이라면 (B) 구조이거나, (A)와 (B)를 섞은 구조(SoC island는 오디오, 외부 MCU는 센서·전원 관리 등)일 수 있다. 어느 쪽인지는 공개 정보로 알 수 없다.

### 2.2 트레이드오프

| 기준 | 통합 island (A) | companion MCU (B) |
|---|---|---|
| always-on 전력 | SoC 공정(고성능용)이라 누설이 크고, island만 켜도 SoC의 PMIC rail 일부가 살아야 할 수 있다 | Ambiq류 subthreshold 저전력 MCU는 µA/MHz급. **SoC 전체를 완전히 꺼 둘 수 있다** |
| wake 지연 | 칩 안 mailbox → µs급 | 칩 밖 GPIO/SPI → 여전히 빠르지만, SoC가 완전히 꺼져 있었다면 **부팅 시간**(초 단위)이 붙는다 |
| 데이터 이동 | 공유 메모리로 zero-copy 가능 | **공유 메모리가 없다**. 모든 데이터를 SPI/UART로 복사. 오디오 2초 = 64 KB 전송 |
| BOM · 면적 | 추가 칩 없음 | MCU + 크리스털 + flash(내장일 수 있음) + 배선. 웨어러블 PCB 면적은 귀하다 |
| 소프트웨어 복잡도 | 벤더 SDK·툴체인에 묶임 (island 펌웨어는 벤더가 대부분 제공) | 펌웨어를 완전히 직접 소유. 대신 **두 번째 펌웨어·프로토콜·OTA 경로**를 만들어야 한다 |
| 유연성 · 벤더 독립 | SoC를 바꾸면 always-on 코드도 새로 | SoC를 바꿔도 MCU 펌웨어는 유지 가능 |
| 디버그 | 벤더 툴 (접근 제한 있을 수 있음) | 표준 SWD/JTAG, 자기 로그 |

말로 하면: **(A)는 칩 안이라 빠르고 싸게 연결되지만 always-on 바닥이 SoC 공정에 묶이고, (B)는 always-on 바닥을 극한까지 낮추고 소유권을 얻는 대신 링크 프로토콜과 두 번째 펌웨어라는 비용을 치른다.**

판단 질문 세 개:

1. AP를 **완전히 전원 차단**할 수 있는가? (B)의 가장 큰 이득은 AP suspend 바닥(예제 2의 3 mW)을 없애는 것이다. 하지만 완전히 끄면 다시 켜는 데 초 단위가 걸리므로 "Hey Hark" 응답 지연 예산과 충돌한다.
2. always-on 작업이 **오디오처럼 대역폭이 큰가**, IMU처럼 작은가? 오디오를 MCU에서 SoC로 매번 SPI로 옮기면 링크 전력·지연이 붙는다.
3. 팀이 **벤더 island 펌웨어에 얼마나 접근**할 수 있는가? 스타트업은 벤더 SDK가 허용하는 범위 안에서만 island를 쓸 수 있을 수 있다 — 이것이 companion MCU를 택하는 현실적 이유가 되기도 한다.

### 2.3 칩 밖 링크 — 무엇으로, 어떻게 대화하나

칩이 다르면 공유 메모리가 없다. 남는 것은 **직렬 버스 + GPIO**다.

| 수단 | 장점 | 주의 |
|---|---|---|
| SPI | 빠름(수~수십 MHz), 전이중, DMA 쉬움 | master가 clock을 쥔다. slave 쪽이 보낼 게 있으면 **별도 IRQ 선**으로 master에게 알려야 한다 |
| UART | 핀 2개, 비동기, 간단 | 자는 쪽은 첫 바이트를 놓치기 쉽다 (wake 후 UART 클럭이 안정될 때까지). 보통 1~3 Mbps |
| I2C | 핀 2개, 여러 장치 공유 | 느림(400 kHz~1 MHz), 대량 데이터에 부적합. 제어용 |
| GPIO | 인터럽트·wake·"준비됨" 신호 | 방향·극성·edge/level, 부팅 중 기본 상태(pull)를 반드시 정의 |

Don의 버스 경험이 그대로 쓰이는 곳이 **프로토콜 설계**다. 최소 요소:

- **프레이밍**: 바이트 스트림 어디가 메시지의 시작인지. 길이 필드 + 동기 바이트, SLIP, COBS 등.
- **무결성**: CRC-16 이상. 전원 노이즈·ESD·레벨 시프터 문제로 비트는 반드시 뒤집힌다.
- **순서 · 재전송**: sequence 번호 + ACK/NAK + timeout. 중복 수신을 seq로 걸러낸다.
- **흐름 제어**: 받는 쪽 버퍼가 차면 멈추게 하는 방법 (credit 수, RTS/CTS, "ready" GPIO).
- **wake 핸드셰이크**: 양쪽 모두 잘 수 있으므로 "깨워라" 선과 "깨어났다" 선을 둔다.
- **버전 협상**: 첫 메시지로 프로토콜 버전·기능 비트를 교환. 두 펌웨어는 **따로 업데이트**되기 때문이다 (6.4절).

wake 핸드셰이크를 시간 순서로 그리면:

```
MCU                                   SoC(AP)
 │ 키워드 검출                          │ (suspend)
 │── HOST_WAKE ↑ ─────────────────────►│ wake IRQ → resume 시작
 │                                     │ ... resume 수백 ms ...
 │◄──────────────────── DEV_READY ↑ ───│ SPI 드라이버 준비 완료
 │══ SPI 프레임: [KWS_EVENT seq=7] ═══►│
 │◄═══════════════ [ACK seq=7] ════════│
 │══ SPI 프레임: [AUDIO chunk 0..N] ══►│ pre-roll 전송
 │── HOST_WAKE ↓ ─────────────────────►│
 │                                     │ (일 끝나면 DEV_READY ↓, suspend)
```

말로 하면: MCU는 선을 올려 AP를 깨우고, AP가 "준비됐다"고 답할 때까지 **보내지 않는다**. 이 순서를 어기면 AP가 resume 중일 때 보낸 첫 프레임이 사라진다 — Don이 버스 장애 root cause에서 흔히 봤을 "첫 트랜잭션만 가끔 실패" 패턴이다.

### 2.4 코드로 확인 — COBS 프레이밍 + CRC-16

**예제 3** — 무엇을 확인하나: 0x00이 들어 있는 payload를 COBS로 인코딩하면 선 위의 0x00은 **프레임 구분자뿐**이 되어, 한 프레임이 깨져도 수신기가 다음 0x00에서 즉시 재동기화된다는 것.

COBS(Consistent Overhead Byte Stuffing)는 payload의 모든 0x00을 "다음 0x00까지의 거리" 바이트로 바꿔서 인코딩 결과에 0x00이 없게 만드는 방식이다. 오버헤드는 254바이트당 최대 1바이트로 작고 예측 가능하다.

```c
/* link.c — MCU↔SoC UART/SPI 링크용 프레이밍: [seq type len payload crc16] → COBS → 0x00 구분자 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint16_t crc16_ccitt(const uint8_t *p, size_t n) {       /* poly 0x1021, init 0xFFFF */
    uint16_t c = 0xFFFF;
    while (n--) { c ^= (uint16_t)(*p++ << 8);
        for (int i = 0; i < 8; i++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1); }
    return c;
}
static size_t cobs_enc(const uint8_t *in, size_t n, uint8_t *out) { /* 결과에 0x00이 없다 */
    size_t o = 1, code_at = 0; uint8_t code = 1;
    for (size_t i = 0; i < n; i++) {
        if (in[i]) { out[o++] = in[i]; code++; }
        if (!in[i] || code == 0xFF) { out[code_at] = code; code_at = o++; code = 1; }
    }
    out[code_at] = code; return o;
}
static size_t cobs_dec(const uint8_t *in, size_t n, uint8_t *out) {
    size_t i = 0, o = 0;
    while (i < n) { uint8_t code = in[i++];
        if (code == 0 || i + code - 1 > n) return 0;              /* 깨진 프레임 */
        for (uint8_t k = 1; k < code; k++) out[o++] = in[i++];
        if (code != 0xFF && i < n) out[o++] = 0; }
    return o;
}
static size_t frame_build(uint8_t seq, uint8_t type, const uint8_t *pl, uint8_t len, uint8_t *wire) {
    uint8_t raw[260]; raw[0] = seq; raw[1] = type; raw[2] = len; memcpy(raw + 3, pl, len);
    uint16_t c = crc16_ccitt(raw, 3u + len); raw[3 + len] = (uint8_t)(c >> 8); raw[4 + len] = (uint8_t)c;
    size_t n = cobs_enc(raw, 5u + len, wire); wire[n++] = 0x00; return n;   /* 구분자 */
}
```

```c
static void rx_frame(const uint8_t *enc, size_t n) {           /* 0x00을 만날 때마다 호출 */
    uint8_t raw[260]; size_t m = cobs_dec(enc, n, raw);
    if (m < 5 || m != 5u + raw[2] || crc16_ccitt(raw, m - 2) != (uint16_t)(raw[m - 2] << 8 | raw[m - 1])) {
        printf("  rx: DROP (len %zu, CRC/COBS 불일치) -> NAK 요청\n", n); return; }
    printf("  rx: seq=%u type=0x%02X len=%u payload[0..1]=%02X %02X  OK -> ACK\n",
           raw[0], raw[1], raw[2], raw[3], raw[4]);
}
int main(void) {
    printf("crc16(\"123456789\") = 0x%04X (기대 0x29B1)\n", crc16_ccitt((const uint8_t *)"123456789", 9));
    uint8_t wire[1024]; size_t w = 0;
    uint8_t kws[4] = {0x01, 0x00, 0x00, 0x5A};                 /* 0x00 포함 payload: 키워드 ID, score */
    uint8_t imu[6] = {0x00, 0x10, 0xFF, 0x00, 0x00, 0x02};
    w += frame_build(7, 0x21, kws, 4, wire + w);
    size_t f2 = w; w += frame_build(8, 0x30, imu, 6, wire + w);
    w += frame_build(9, 0x21, kws, 4, wire + w);
    size_t zeros = 0; for (size_t i = 0; i < w; i++) zeros += (wire[i] == 0);
    printf("wire %zu bytes, 그 안의 0x00 개수 = %zu (= 프레임 구분자 수)\n", w, zeros);
    wire[f2 + 4] ^= 0x04;                                       /* 프레임 2의 비트 하나 뒤집기 */
    size_t start = 0;
    for (size_t i = 0; i < w; i++) if (wire[i] == 0x00) { rx_frame(wire + start, i - start); start = i + 1; }
    return 0;
}
```

```sh
cat link.c link_main.c > l.c && cc -std=c11 -Wall -Wextra -O2 l.c -o l && ./l
```

```text
crc16("123456789") = 0x29B1 (기대 0x29B1)
wire 35 bytes, 그 안의 0x00 개수 = 3 (= 프레임 구분자 수)
  rx: seq=7 type=0x21 len=4 payload[0..1]=01 00  OK -> ACK
  rx: DROP (len 12, CRC/COBS 불일치) -> NAK 요청
  rx: seq=9 type=0x21 len=4 payload[0..1]=01 00  OK -> ACK
```

출력에서 볼 것: CRC 구현이 표준 check 값(CRC-16/CCITT-FALSE의 "123456789" → 0x29B1)과 맞는다. 세 프레임 35바이트 중 0x00은 정확히 3개(구분자)뿐이다. 두 번째 프레임의 비트 하나를 뒤집자 그 프레임만 버려지고, **세 번째 프레임은 바로 정상 수신**된다. 길이 필드 기반 프레이밍이었다면 길이 바이트가 깨졌을 때 뒤의 프레임들까지 줄줄이 잘못 읽힐 수 있다.

임베디드 연결: MCU 쪽에서는 UART DMA의 idle-line 인터럽트나 0x00 문자 매치 인터럽트로 프레임 끝을 잡으면 바이트마다 인터럽트를 받지 않아도 된다. 흐름 제어·재전송은 이 위에 seq/ACK로 얹는다.

---

## 3. SoC 안의 IPC — mailbox, 공유 메모리 링, 메시지 프로토콜

### 3.1 직관 — 우편함과 초인종

같은 칩 안의 두 코어가 대화하는 방법은 아파트 이웃끼리 물건을 주고받는 것과 같다.

- **우편함 = 공유 메모리**: 물건(데이터)은 둘 다 열 수 있는 공용 우편함에 넣는다. 크고 많은 것도 넣을 수 있다.
- **초인종 = mailbox / doorbell 인터럽트**: "우편함에 뭐 넣었어"라는 신호만 초인종으로 보낸다. 초인종 자체는 정보가 거의 없다.

상대가 자고 있어도 초인종은 깨울 수 있다. 상대가 깨어 있고 바쁘다면 초인종 없이 우편함을 주기적으로 확인(polling)하게 할 수도 있다.

### 3.2 mailbox 하드웨어

**mailbox**는 코어 사이 신호용 작은 하드웨어 블록이다. 전형적으로:

- 코어 A가 "set" 레지스터에 비트를 쓰면 → 코어 B의 인터럽트 선이 올라간다.
- 코어 B는 "status"를 읽고 "clear"에 써서 인터럽트를 내린다.
- 일부 mailbox는 32비트 몇 개짜리 작은 데이터 레지스터나 FIFO를 가진다 (짧은 명령은 여기에 바로).

Linux에는 이런 하드웨어를 추상화한 **mailbox framework**(`mbox_send_message` 등)가 있고, SoC마다 드라이버가 있다. Qualcomm 계열의 upstream 드라이버 중에는 IPCC(Inter-Processor Communication Controller) 같은 이름이 보인다. Don의 SSD 펌웨어에서 "코어 간 doorbell 레지스터 + 공유 SRAM 큐"를 썼다면 개념은 완전히 같다.

### 3.3 공유 메모리 링 — SPSC 규칙

**SPSC(single-producer single-consumer) 링**은 한쪽만 쓰고 한쪽만 읽는 원형 큐다. 규칙 두 개만 지키면 락이 필요 없다.

1. **head는 producer만, tail은 consumer만 쓴다.** 서로 상대의 인덱스는 읽기만 한다.
2. **순서**: producer는 "payload 쓰기 → head 갱신" 순서를, consumer는 "head 읽기 → payload 읽기" 순서를 **다른 코어가 보는 순서로도** 보장해야 한다. C11에서는 head 갱신을 `release`, head 읽기를 `acquire`로 한다. ARM 펌웨어에서는 `DMB`(data memory barrier)가 이 역할이다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="e8b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="10" y="40" width="150" height="230" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="currentColor"/><text x="85" y="62" font-size="13" text-anchor="middle">AP (Linux 드라이버)</text><text x="85" y="84" font-size="12" text-anchor="middle">producer (req)</text><text x="85" y="102" font-size="12" text-anchor="middle">consumer (rsp)</text><rect x="520" y="40" width="150" height="230" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="currentColor"/>
<text x="595" y="62" font-size="13" text-anchor="middle">DSP (RTOS)</text><text x="595" y="84" font-size="12" text-anchor="middle">consumer (req)</text><text x="595" y="102" font-size="12" text-anchor="middle">producer (rsp)</text><rect x="185" y="30" width="310" height="165" rx="8" fill="none" stroke="#3f9a6b" stroke-width="1.5" stroke-dasharray="6 3"/><text x="340" y="48" font-size="12" text-anchor="middle">공유 메모리 (DRAM carve-out 또는 공유 SRAM)</text><text x="200" y="70" font-size="12">req 링 (AP → DSP)</text><rect x="213" y="78" width="30" height="24" fill="none" stroke="currentColor"/><rect x="245" y="78" width="30" height="24" fill="none" stroke="currentColor"/>
<rect x="277" y="78" width="30" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="currentColor"/><rect x="309" y="78" width="30" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="currentColor"/><rect x="341" y="78" width="30" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="currentColor"/><rect x="373" y="78" width="30" height="24" fill="none" stroke="currentColor"/><rect x="405" y="78" width="30" height="24" fill="none" stroke="currentColor"/><rect x="437" y="78" width="30" height="24" fill="none" stroke="currentColor"/><text x="292" y="118" font-size="12" text-anchor="middle">tail</text><text x="388" y="118" font-size="12" text-anchor="middle">head</text>
<text x="200" y="134" font-size="12">rsp 링 (DSP → AP)</text><rect x="213" y="142" width="30" height="24" fill="none" stroke="currentColor"/><rect x="245" y="142" width="30" height="24" fill="none" stroke="currentColor"/><rect x="277" y="142" width="30" height="24" fill="none" stroke="currentColor"/><rect x="309" y="142" width="30" height="24" fill="none" stroke="currentColor"/><rect x="341" y="142" width="30" height="24" fill="none" stroke="currentColor"/><rect x="373" y="142" width="30" height="24" fill="none" stroke="currentColor"/><rect x="405" y="142" width="30" height="24" fill="#3f9a6b" fill-opacity="0.45" stroke="currentColor"/>
<rect x="437" y="142" width="30" height="24" fill="none" stroke="currentColor"/><text x="420" y="182" font-size="12" text-anchor="middle">tail</text><text x="452" y="182" font-size="12" text-anchor="middle">head</text><line x1="160" y1="90" x2="211" y2="90" stroke="currentColor" marker-end="url(#e8b)"/><line x1="471" y1="90" x2="518" y2="90" stroke="currentColor" marker-end="url(#e8b)"/><line x1="518" y1="154" x2="471" y2="154" stroke="currentColor" marker-end="url(#e8b)"/><line x1="211" y1="154" x2="160" y2="154" stroke="currentColor" marker-end="url(#e8b)"/><rect x="185" y="212" width="310" height="54" rx="6" fill="#d0564a" fill-opacity="0.15" stroke="currentColor"/>
<text x="340" y="234" font-size="12" text-anchor="middle">mailbox / IPC 블록: doorbell 레지스터</text><text x="340" y="252" font-size="12" text-anchor="middle">한 쪽이 비트를 쓰면 → 다른 쪽 코어에 IRQ</text><line x1="160" y1="230" x2="183" y2="230" stroke="currentColor" marker-end="url(#e8b)"/><line x1="495" y1="248" x2="518" y2="248" stroke="currentColor" marker-end="url(#e8b)"/><text x="85" y="140" font-size="12" text-anchor="middle">① slot에 payload</text><text x="85" y="158" font-size="12" text-anchor="middle">② head += 1 (release)</text><text x="85" y="210" font-size="12" text-anchor="middle">③ doorbell 쓰기</text><text x="595" y="210" font-size="12" text-anchor="middle">④ IRQ → 깨어남</text>
<text x="595" y="140" font-size="12" text-anchor="middle">⑤ head 읽기 (acquire)</text><text x="595" y="158" font-size="12" text-anchor="middle">⑥ 꺼내고 tail += 1</text><text x="340" y="298" font-size="12" text-anchor="middle">head는 producer만, tail은 consumer만 쓴다 → 락 없이 안전 (SPSC)</text><text x="340" y="315" font-size="12" text-anchor="middle">데이터는 공유 메모리로, "왔다"는 신호만 mailbox로</text>
</svg>
```

그림 4 — AP와 DSP 사이 IPC의 표준 모양. 요청과 응답 링을 따로 두어 각 링이 SPSC가 되게 한다. 데이터는 공유 메모리로, "왔다"는 신호만 mailbox로 보낸다. 번호는 한 요청이 지나가는 순서다.

손계산: 링 크기 N = 16(2의 거듭제곱)이면 인덱스를 `& (N-1)`로 감는다. head = 21, tail = 18이면 들어 있는 메시지 = 21 − 18 = 3개, 빈 칸 = 13개. head와 tail을 32비트 부호 없는 정수로 계속 증가시키면 2³² 에서 넘쳐도 `head - tail` 뺄셈은 모듈러 산술로 여전히 맞다 (N이 2³²의 약수이므로).

### 3.4 코드로 확인 — 두 스레드 SPSC 링 + doorbell, 왕복 지연 측정

**예제 4** — 무엇을 확인하나: 호스트 PC에서 두 스레드를 "AP"와 "DSP"로 보고, 공유 메모리 링 2개 + doorbell(condition variable)로 요청-응답 왕복 시간(RTT)을 잰다. doorbell로 **재웠다 깨우는** 경우와, 상대가 **계속 polling** 하는 경우를 비교한다.

먼저 링. `_Alignas(64)`로 head, tail, slot 배열을 서로 다른 캐시 라인에 둔다(이유는 3.5절).

```c
/* ipc_ring.c — AP 스레드와 DSP 스레드 사이의 SPSC 링 2개(요청/응답) + doorbell */
#define _DARWIN_C_SOURCE     /* macOS에서 CLOCK_MONOTONIC_RAW; Linux는 _GNU_SOURCE */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RING_N 16u                      /* 2의 거듭제곱: index & (N-1) */
typedef struct { uint32_t seq; uint32_t cmd; uint64_t t_send; } msg_t;

typedef struct {
    _Alignas(64) _Atomic uint32_t head; /* producer만 쓴다 */
    _Alignas(64) _Atomic uint32_t tail; /* consumer만 쓴다 */
    _Alignas(64) msg_t slot[RING_N];
} ring_t;

static int ring_push(ring_t *r, const msg_t *m) {
    uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (h - t == RING_N) return 0;                        /* full */
    r->slot[h & (RING_N - 1)] = *m;                       /* 1) payload 쓰기 */
    atomic_store_explicit(&r->head, h + 1, memory_order_release); /* 2) 공개 */
    return 1;
}
static int ring_pop(ring_t *r, msg_t *m) {
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    if (h == t) return 0;                                 /* empty */
    *m = r->slot[t & (RING_N - 1)];
    atomic_store_explicit(&r->tail, t + 1, memory_order_release);
    return 1;
}
```

다음은 doorbell과 두 스레드. doorbell은 "pending 플래그 + condition variable"로 인터럽트 선을 흉내 낸다. 기다리는 쪽은 **링이 비었을 때만** 잠들고, 깨어나면 다시 링부터 본다 — 인터럽트가 몇 번 합쳐져 와도(coalescing) 메시지를 잃지 않는 표준 패턴이다.

```c
/* doorbell = "인터럽트 선" 흉내: pending 플래그 + condition variable */
typedef struct { pthread_mutex_t mu; pthread_cond_t cv; int pending; } bell_t;
static void bell_ring(bell_t *b) {
    pthread_mutex_lock(&b->mu); b->pending = 1;
    pthread_cond_signal(&b->cv); pthread_mutex_unlock(&b->mu);
}
static void bell_wait(bell_t *b) {                 /* 잠들었다가 IRQ로 깨는 코어 */
    pthread_mutex_lock(&b->mu);
    while (!b->pending) pthread_cond_wait(&b->cv, &b->mu);
    b->pending = 0; pthread_mutex_unlock(&b->mu);
}

static ring_t req, rsp;                            /* "공유 SRAM"에 놓인 두 링 */
static bell_t bell_dsp = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0};
static bell_t bell_ap  = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0};
static int use_bell;                               /* 1: doorbell로 깨움, 0: spin polling */
enum { N_WARM = 1000, N_MEAS = 20000, CMD_STOP = 0xDEAD };

static uint64_t now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC_RAW, &ts); /* macOS: ns 해상도 */
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
static void recv_blocking(ring_t *r, bell_t *b, msg_t *m) {
    while (!ring_pop(r, m)) { if (use_bell) bell_wait(b); }
}
static void *dsp_thread(void *arg) {               /* 요청을 받아 그대로 돌려준다 */
    (void)arg; msg_t m;
    for (;;) {
        recv_blocking(&req, &bell_dsp, &m);
        if (m.cmd == CMD_STOP) return NULL;
        m.cmd |= 0x8000u;                          /* "응답" 표시 */
        while (!ring_push(&rsp, &m)) { }
        if (use_bell) bell_ring(&bell_ap);
    }
}
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}
int main(int argc, char **argv) {
    use_bell = (argc > 1 && strcmp(argv[1], "bell") == 0);
    static uint64_t rtt[N_MEAS];
    pthread_t th; pthread_create(&th, NULL, dsp_thread, NULL);
    for (uint32_t i = 0; i < N_WARM + N_MEAS; i++) {
        msg_t m = { i, 0x0001u, now_ns() }, r;
        while (!ring_push(&req, &m)) { }
        if (use_bell) bell_ring(&bell_dsp);
        recv_blocking(&rsp, &bell_ap, &r);
        if (r.seq != i || r.cmd != 0x8001u) { printf("BAD seq %u\n", r.seq); return 1; }
        if (i >= N_WARM) rtt[i - N_WARM] = now_ns() - r.t_send;
    }
    msg_t stop = { 0, CMD_STOP, 0 };
    while (!ring_push(&req, &stop)) { }
    if (use_bell) bell_ring(&bell_dsp);
    pthread_join(th, NULL);
    qsort(rtt, N_MEAS, sizeof rtt[0], cmp_u64);
    printf("%-5s n=%d  p50=%6.2f us  p99=%7.2f us  max=%8.2f us\n",
           use_bell ? "bell" : "spin", N_MEAS, rtt[N_MEAS / 2] / 1e3,
           rtt[N_MEAS * 99 / 100] / 1e3, rtt[N_MEAS - 1] / 1e3);
    return 0;
}
```

```sh
cat ipc_ring.c ipc_main.c > ipc.c
cc -std=c11 -Wall -Wextra -O2 -pthread ipc.c -o ipc
./ipc spin; ./ipc bell; ./ipc spin; ./ipc bell
```

```text
spin  n=20000  p50=  0.25 us  p99=   0.25 us  max=   20.08 us
bell  n=20000  p50=  3.12 us  p99=   8.75 us  max=   32.96 us
spin  n=20000  p50=  0.12 us  p99=   0.17 us  max=   11.79 us
bell  n=20000  p50=  2.46 us  p99=   6.46 us  max=   23.42 us
```

(Apple M2 macOS에서 실행한 실제 값이다. 경고 0개.)

출력에서 볼 것:

- **spin(polling)**: 왕복 p50 ≈ 0.1~0.25 µs. 두 코어가 같은 캐시 라인을 주고받는 비용뿐이다. 대신 두 코어가 **100% 돌고 있다** — 전력 관점에서는 최악이다.
- **bell(doorbell로 재우고 깨움)**: p50 ≈ 2.5~3 µs, p99 ≈ 6~9 µs, max 수십 µs. 잠든 스레드를 OS가 깨워 스케줄하는 비용이다. 같은 바이너리를 다른 때 돌렸을 때는 p99 ≈ 20 µs, max ≈ 580 µs도 나왔다 — 꼬리는 실행마다 크게 다르다 — D6에서 본 "평균이 아니라 꼬리를 본다"가 여기서도 그대로다.
- 실제 SoC에서는 OS 스케줄러 대신 **인터럽트 지연 + 코어의 저전력 상태 탈출 시간 + 캐시 유지보수**가 이 자리를 채운다. 코어가 WFI에서 깨는 정도면 µs급, retention·power collapse에서 깨면 수십 µs~ms급이 될 수 있다(구체 값은 SoC마다 다르므로 실측해야 한다).

설계 교훈: **지연과 전력은 교환 관계**다. 초당 수천 번 주고받는 스트리밍(예: 10 ms 오디오 프레임)은 매 프레임 doorbell 대신 "N 프레임 모이면 한 번" 또는 "상대가 깨어 있는 동안은 polling, 비면 doorbell 재무장" 같은 하이브리드를 쓴다. Linux NAPI나 NVMe의 interrupt coalescing과 같은 아이디어다.

### 3.5 코드로 확인 — false sharing이 링을 반으로 느리게 한다

**예제 5** — 무엇을 확인하나: producer가 쓰는 `head`와 consumer가 쓰는 `tail`이 **같은 캐시 라인**에 있으면, 두 코어가 서로의 라인을 계속 빼앗아(invalidate) 처리량이 떨어진다는 것.

```c
/* fs.c — SPSC 링 처리량: head/tail이 같은 캐시 라인(PACKED) vs 떨어진 라인 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#ifdef PACKED
#define PAD                              /* head, tail이 한 라인에 붙는다 */
#else
#define PAD _Alignas(128)                /* Apple M2: hw.cachelinesize = 128 */
#endif
#define N 256u
#define COUNT 20000000u
static struct { PAD _Atomic uint32_t head; PAD _Atomic uint32_t tail; PAD uint32_t slot[N]; } r;

static void *consumer(void *arg) {
    uint64_t sum = 0; uint32_t t = 0;
    while (t < COUNT) {
        uint32_t h = atomic_load_explicit(&r.head, memory_order_acquire);
        while (t != h) { sum += r.slot[t & (N - 1)]; t++; }      /* 있는 만큼 꺼낸다 */
        atomic_store_explicit(&r.tail, t, memory_order_release);
    }
    *(uint64_t *)arg = sum; return NULL;
}
int main(void) {
    uint64_t sum = 0; pthread_t th; struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    pthread_create(&th, NULL, consumer, &sum);
    for (uint32_t h = 0; h < COUNT; h++) {
        while (h - atomic_load_explicit(&r.tail, memory_order_acquire) == N) { }  /* full */
        r.slot[h & (N - 1)] = h;
        atomic_store_explicit(&r.head, h + 1, memory_order_release);
    }
    pthread_join(th, NULL); clock_gettime(CLOCK_MONOTONIC, &b);
    double s = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
    printf("%s: %u msgs in %.3f s = %.1f M msg/s (sum ok=%d)\n",
#ifdef PACKED
           "packed ",
#else
           "aligned",
#endif
           COUNT, s, COUNT / s / 1e6, sum == (uint64_t)COUNT * (COUNT - 1) / 2);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 -pthread fs.c -o fs_al
cc -std=c11 -Wall -Wextra -O2 -pthread -DPACKED fs.c -o fs_pk
./fs_pk; ./fs_al; ./fs_pk; ./fs_al
```

```text
packed : 20000000 msgs in 0.422 s = 47.4 M msg/s (sum ok=1)
aligned: 20000000 msgs in 0.210 s = 95.3 M msg/s (sum ok=1)
packed : 20000000 msgs in 0.376 s = 53.2 M msg/s (sum ok=1)
aligned: 20000000 msgs in 0.226 s = 88.7 M msg/s (sum ok=1)
```

출력에서 볼 것: 코드는 한 글자도 다르지 않은데 정렬만 바꿔서 처리량이 약 **2배** 차이 난다. 이 기계의 캐시 라인은 128바이트(`sysctl hw.cachelinesize`)라서 `_Alignas(128)`을 썼다. Cortex-A 계열은 보통 64바이트다 — 정렬 크기는 **대상 SoC의 라인 크기**로 맞춘다.

### 3.6 캐시와 일관성 — 이기종 IPC의 진짜 함정 (E7로 연결)

호스트 PC의 두 스레드는 **하드웨어가 캐시 일관성(coherency)을 보장**한다. 이기종 SoC에서는 그렇지 않은 경우가 많다.

- AP 클러스터 안의 코어끼리는 보통 coherent하다.
- AP ↔ DSP, AP ↔ NPU, AP ↔ island 코어 사이는 **non-coherent**이거나, 일부 경로만 I/O coherent일 수 있다. 어느 쪽인지는 SoC 문서와 메모리 매핑 속성(cacheable 여부)에 달려 있다.

non-coherent인데 캐시를 켜 두면 생기는 전형적인 버그:

```
시각   AP (cacheable로 매핑)                 DSP (자기 캐시)
 t0    slot[5] = msg  → AP D-cache에만 있음
 t1    head = 6       → 이것도 캐시에만?  
 t2    doorbell 쓰기 (device 메모리라 즉시 나감)
 t3                                          IRQ → head 읽기 = 5 (옛 값)  → "빈 링"으로 판단
 t4                                          ... 혹은 head=6은 보이는데 slot[5]는 DRAM의 옛 데이터
```

해결 방법 (E7에서 자세히):

1. 공유 영역을 **non-cacheable(또는 write-combine)** 로 매핑 — 단순하지만 대량 데이터는 느리다. 링 인덱스·작은 제어 구조에 흔히 쓴다.
2. cacheable로 두고 **명시적 캐시 유지보수**: producer는 payload를 쓰고 `clean`(write-back), 그다음 인덱스 갱신·clean, 그다음 doorbell. consumer는 읽기 전에 `invalidate`. 캐시 라인 크기 정렬 필수(옆 데이터까지 invalidate 해서 날리는 버그 방지).
3. 하드웨어 I/O coherency가 있는 경로를 쓰기.

그리고 **순서**: doorbell 레지스터 쓰기가 앞선 메모리 쓰기보다 **먼저 도착하면** 안 된다. ARM에서는 device 메모리 쓰기 전에 `DSB`가 필요할 수 있다. Linux의 `writel()`이 내부에 barrier를 넣는 이유다(`writel_relaxed()`는 넣지 않는다).

Don 연결: SSD에서 DMA 디스크립터를 쓰고 doorbell을 울리기 전에 barrier·cache flush를 넣던 것과 **완전히 같은 문제**다. 이기종 SoC IPC 버그의 대부분은 "모델"이 아니라 이 두 줄(캐시 유지보수, barrier)에서 나온다.

### 3.7 메시지 프로토콜 — 링 위에 무엇을 얹나

링과 doorbell은 "바이트를 옮기는" 층이다. 그 위에 "누구에게 어떤 요청을" 보내는 층이 필요하다.

| 이름 | 무엇 | 어디서 보나 |
|---|---|---|
| **remoteproc** | Linux가 원격 프로세서의 펌웨어를 로드·시작·정지·크래시 복구하는 프레임워크 | Linux kernel 표준 (6절) |
| **RPMsg** | virtio vring(공유 메모리 링) 위의 메시지 버스. endpoint 주소, name service로 채널 자동 생성 | Linux kernel, **OpenAMP**(RTOS·bare-metal 쪽 구현). 널리 쓰이는 공개 표준 |
| **FastRPC** | Qualcomm의 CPU → Hexagon DSP 원격 호출. IDL로 stub/skel을 생성하고, 인자·버퍼를 마샬링해 DSP 쪽 함수를 부른다 | Hexagon SDK, Linux `fastrpc` 드라이버. 세부 동작·성능은 SDK 문서로 확인 (hedge) |
| 벤더 IPC (예: Qualcomm GLINK·QMI 류) | 서브시스템 간 채널·서비스 메시지 | 벤더 kernel 트리 (이름 수준으로만 알아 두기) |

**RPC 비용 감각**: FastRPC류 원격 호출은 "함수 호출"처럼 보이지만 실제로는 IPC 왕복 + 버퍼 캐시 유지보수 + (필요하면) DSP 깨우기다. 3.4절의 숫자처럼 한 번에 수~수백 µs가 들 수 있다고 생각하고, **작은 호출을 많이** 하지 말고 **큰 일을 한 번에** 넘기는 API로 설계한다. NPU 런타임(QNN 등, F4)이 그래프 전체를 한 번에 올려 두고 "실행" 호출만 반복하게 만드는 이유다.

메시지 설계 체크포인트:

- 고정 헤더: `{version, type, seq, len, flags}` — 2.4절 프레이밍과 같은 생각.
- **큰 데이터는 링에 복사하지 말고 포인터(물리 주소·버퍼 핸들)만** 보낸다. 오디오 frame, 텐서는 공유 버퍼 풀에 두고 소유권을 메시지로 넘긴다 (zero-copy).
- 버퍼 소유권 규칙: "보낸 뒤 ACK 받기 전에는 쓰지 않는다"를 문서화.
- 상대가 재시작하면(6.3절) 링 상태·seq·버퍼 소유권을 어떻게 리셋할지.

---

## 4. 워크로드 배치 — 누가 무엇을 돌리나

### 4.1 기준

작업 하나를 엔진에 배치할 때 보는 축:

1. **duty cycle**: 얼마나 자주·오래 도나. 항상 → 바닥 전력이 싼 곳. 드물게 → 1회 에너지가 싼 곳(깨우기 비용 포함).
2. **지연 예산**: wake word 응답 < 수백 ms, 제스처 피드백 < 100 ms, 대화 첫 토큰 < 1 s (L4).
3. **메모리**: 모델 + activation + 버퍼가 그 엔진의 로컬 메모리(TCM·SRAM)에 들어가나, DRAM이 필요한가. DRAM을 쓰는 순간 DRAM이 self-refresh에서 깨어나야 하고 그 자체가 전력이다.
4. **연산 적합성**: op 지원, 정밀도(INT8/INT4/FP16), NPU fallback 여부 (E5, F8).
5. **데이터가 어디서 오나**: 오디오가 island SRAM에 있으면 island 근처에서 처리하는 게 이동 비용이 적다 (I4, D7 "data movement dominates").
6. **격리·신뢰성**: always-on 경로는 AP의 앱 크래시와 무관하게 살아 있어야 한다.

### 4.2 코드로 확인 — 제약을 만족하는 가장 싼 엔진

**예제 6** — 무엇을 확인하나: 작업마다 연속 부하(MMAC/s)와 메모리를 주고, 엔진마다 처리량·유효 pJ/MAC·바닥 전력·메모리를 주면, "탈락 조건"과 "평균 전력"만으로 표의 대부분이 자동으로 채워진다는 것. 숫자는 모두 설명용 가정이다(실제 엔진 스펙이 아니다).

```python
# 작업 → 엔진 배치: 연산량·메모리 제약을 만족하는 엔진 중 평균 전력이 가장 낮은 곳 (모든 숫자는 설명용 가정)
ENG = {  # 이름: (처리량 GMAC/s, 유효 pJ/MAC, 켜 두는 바닥 mW, 쓸 수 있는 메모리 MB)
    "MCU": (0.2, 20.0, 0.1, 2), "DSP": (5, 3.0, 3.0, 4), "NPU": (2000, 0.5, 60.0, 2000),
    "GPU": (500, 3.0, 150.0, 2000), "CPU": (50, 30.0, 100.0, 2000)}
TASK = {  # 이름: (연속 부하 MMAC/s, 모델+버퍼 MB)
    "VAD": (0.5, 0.05), "KWS": (27, 0.1), "IMU 제스처": (5, 0.05), "착용 감지": (0.01, 0.01),
    "카메라 presence": (600, 3.5), "streaming ASR": (2000, 40), "TTS": (5000, 50), "SLM decode": (10000, 600)}
print(f"{'작업':<14}" + "".join(f"{e:>8}" for e in ENG) + "   선택")
for t, (mmacs, mb) in TASK.items():
    row, best = [], None
    for e, (gmacs, pj, floor, mem) in ENG.items():
        if mmacs > 0.5 * gmacs * 1000 or mb > mem:        # 50% 이상 점유 또는 메모리 초과면 탈락
            row.append("-"); continue
        mw = floor + mmacs * pj * 1e-3                     # MMAC/s × pJ = µW → mW
        row.append(f"{mw:.2f}")
        if best is None or mw < best[1]: best = (e, mw)
    print(f"{t:<14}" + "".join(f"{v:>8}" for v in row) + f"   {best[0]}")
```

```text
작업                 MCU     DSP     NPU     GPU     CPU   선택
VAD               0.11    3.00   60.00  150.00  100.02   MCU
KWS               0.64    3.08   60.01  150.08  100.81   MCU
IMU 제스처           0.20    3.02   60.00  150.01  100.15   MCU
착용 감지             0.10    3.00   60.00  150.00  100.00   MCU
카메라 presence         -    4.80   60.30  151.80  118.00   DSP
streaming ASR        -       -   61.00  156.00  160.00   NPU
TTS                  -       -   62.50  165.00  250.00   NPU
SLM decode           -       -   65.00  180.00  400.00   NPU
```

출력에서 볼 것: 작은 always-on 작업 4개는 바닥 전력이 0.1 mW인 MCU가 압도적이다. 카메라 presence(작은 비전 모델, 초당 몇 장)는 MCU 처리량을 넘고 DSP에 들어간다. ASR·TTS·SLM은 **메모리 한 줄로** DSP·MCU에서 탈락하고 NPU로 간다. 이 단순 모델은 SLM decode가 사실 **DRAM 대역폭 한계**라는 점(D5)을 무시하고 있고, NPU 바닥 60 mW 안에 DRAM 전력을 뭉뚱그려 넣었다는 점을 기억하자.

### 4.3 Hark 같은 기기의 배치표 (추정)

아래는 "예를 들어 Hark 같은 웨어러블이라면"의 **추정 배치**다. 실제 제품 구성은 알 수 없고, 같은 작업도 SoC와 팀 선택에 따라 다른 엔진에 갈 수 있다.

| 작업 | 1순위 엔진 | 이유 | 대안 · 주의 |
|---|---|---|---|
| VAD | island 코어 · companion MCU (또는 마이크·codec 내장 HW VAD) | 24시간 연속, µW 예산, 모델 수 KB | DSP에 두면 바닥 전력이 10배 이상 |
| KWS (wake word) | audio DSP 또는 강한 MCU (M55+Helium 급) | VAD가 열 때만 돈다, 수십 KB 모델, < 100 ms | 2단 검증은 AP에서 (B5 3.6절) |
| IMU 제스처 | 센서 내장 로직 → MCU (tree·작은 CNN) | 50~100 Hz 연속, 모델 수 KB, 피드백 < 100 ms | 애매한 후보만 DSP로 (B7 11.3절) |
| 착용 감지 | 센서 내장 기능 · MCU | 거의 변화 없는 신호, 초당 1회 이하 판정 | 근접·PPG·정전용량 센서 조합 |
| 센서 로깅 (데이터 수집) | MCU·허브가 batch → AP가 flash·radio로 | 작은 데이터를 큰 묶음으로 (1.2절) | 로깅 자체가 AP를 깨우지 않게 (H1) |
| 카메라 presence | 저전력 비전 경로 (DSP·전용 always-on 카메라 블록이 있는 SoC도 있음) | 초당 몇 장, 작은 모델 | 프라이버시 표시 필수 (N4) |
| streaming ASR | NPU (+ 전처리는 DSP) | 수십 MB 모델, 초당 GMAC급, 세션 동안만 | 오프라인이 아니면 cloud도 후보 (L3) |
| SLM (온디바이스 LLM) | NPU (+ CPU 일부 op) | 수백 MB, DRAM 대역폭 한계 | 긴 답은 cloud로 (L3), 열 한계 (E9) |
| TTS | NPU 또는 CPU | 수십 MB, 세션 동안만 | 짧은 응답음은 미리 합성해 캐시 |
| 큰 LLM · 검색 | cloud | 기기 메모리·열 한계를 넘는다 | radio 에너지·지연 비용 (8절, D7 6.3절) |

말로 하면: **"항상 · 작게"는 MCU·island, "가끔 · 중간"은 DSP, "세션 동안 · 크게"는 NPU, "기기에 안 들어가면" cloud**. 그리고 이 표의 모든 칸은 "추정"이며 실측(K3)으로 확인해야 한다.

### 4.4 흔한 함정

- **엔진 사이 왕복**: 전처리는 DSP, 모델은 NPU, 후처리는 CPU로 나누면 각 경계마다 IPC + 캐시 유지보수 + 메모리 복사가 붙는다. 경계가 싼지 먼저 재자.
- **NPU fallback**: NPU가 지원하지 않는 op 하나 때문에 그래프 중간이 CPU로 떨어지면 위 비용이 **레이어마다** 붙는다 (F8).
- **"평균 부하가 작으니 MCU"의 함정**: 평균은 작아도 **순간 부하**가 크면(예: 1초마다 100 ms짜리 버스트) 지연 예산을 못 지킨다. D6의 꼬리 분석을 같이 하자.

---

## 5. Wake cascade 시뮬레이션 — false wake와 전이 비용이 예산을 먹는다

B9 5.2절에서 3단 cascade의 평균 전력을 **공식**으로 계산했다. 공식은 사건이 겹치지 않는다고 가정한다. 실제로는 사건이 겹치고(DSP가 이미 깨어 있을 때 새 말소리가 오면 추가 wake가 없다), AP가 tail 중에 다시 불리면 resume 비용이 없고, resume 시간이 매번 다르다. 이런 것은 **이산 사건 시뮬레이션(discrete-event simulation, DES)** 으로 본다.

DES란 시간을 일정 간격으로 쪼개지 않고, **사건이 일어나는 시각들만** 우선순위 큐(heap)에 넣어 차례로 처리하는 시뮬레이션이다. 사건 사이에는 상태가 변하지 않으므로, 상태가 바뀔 때마다 "지난 구간 길이 × 그 상태의 전력"을 더하면 에너지가 정확히 나온다.

### 5.1 모델

| 코어 | 상태: 전력 | 전이 | 규칙 |
|---|---|---|---|
| MCU | 항상 0.8 mW | 없음 | mic + PDM decimation + VAD. 음향 사건이 오면 확률 p_vad로 DSP를 깨운다 (판정 30 ms) |
| DSP | off 0.05 · waking 15 · on 20 mW | off→on 3 ms | 사건 끝 + 0.3 s(hangover)까지 켜 있다. 사건 끝 + 50 ms에 KWS 판정, 확률 p_kws로 "키워드!" |
| AP | suspend 3 · resume 300 · active 500 · tail 150 · suspending 200 mW | resume 평균 150 ms(lognormal 흔들림), suspending 50 ms | 진짜 키워드면 5 s 세션(ASR·LLM·TTS), 아니면 1 s 2차 검증 후 거절. 일이 끝나면 2 s tail 후 suspend |

음향 사건(Poisson): 말소리 60/h(2 s), 소음 200/h(0.5 s), 키워드 4/h(0.8 s). p_vad = 말소리 0.98 · 소음 0.3 · 키워드 1.0. p_kws = 말소리 0.01 · 소음 0.002 · 키워드 0.95. **모든 숫자는 설명용 가정**이다.

손계산으로 먼저 감을 잡자 (겹침 무시).

```
DSP wake 횟수/h ≈ 60×0.98 + 200×0.3 + 4 ≈ 122.8
DSP on 시간/h   ≈ 58.8×(2+0.3) + 60×(0.5+0.3) + 4×(0.8+0.3) ≈ 135 + 48 + 4.4 ≈ 188 s
DSP 평균        ≈ 20 mW × 188/3600 + 0.05 ≈ 1.09 mW

AP false wake/h ≈ 58.8×0.01 + 60×0.002 ≈ 0.71      (하루 ≈ 17회)
AP true wake/h  ≈ 4×0.95 = 3.8                       (하루 ≈ 91회)
AP 1회 에너지   false ≈ 300×0.15 + 500×1 + 150×2 + 200×0.05 = 855 mJ
                true  ≈ 45 + 500×5 + 300 + 10 = 2855 mJ
AP 평균         ≈ 3 + (0.71×855 + 3.8×2855)/3600 ≈ 3 + 0.17 + 3.01 ≈ 6.2 mW
```

### 5.2 코드로 확인 — 시뮬레이터

**예제 7(모듈)** — 무엇을 확인하나: 코어마다 전원 상태 기계(`Proc`)를 두고, 사건 큐를 따라 상태를 바꾸며 상태별 에너지를 적립하는 DES. 이 모듈을 다음 두 예제가 import 한다.

```python
# wake_sim.py — MCU(VAD) → DSP(KWS) → AP(ASR/LLM) wake cascade 이산 사건 시뮬레이션 (숫자는 설명용 가정)
import heapq, random
from collections import defaultdict

class Proc:                                    # 전원 상태 기계 하나 = 코어 하나
    def __init__(s, P, st):
        s.P, s.st, s.t0, s.E, s.wakes, s.trace = P, st, 0.0, defaultdict(float), 0, [(0.0, st)]
    def go(s, t, st):                          # 상태 바꾸기 전에 지난 구간 에너지(mW×s = mJ) 적립
        s.E[s.st] += s.P[s.st] * (t - s.t0); s.st, s.t0 = st, t; s.trace.append((t, st))

CFG = dict(
    mcu_mw=0.8,                                                     # mic + PDM 디시메이션 + VAD, 항상 켜짐
    dsp=dict(off=0.05, waking=15.0, on=20.0), dsp_wake=0.003, dsp_hang=0.3, kws_ms=0.05,
    ap=dict(suspend=3.0, resume=300.0, active=500.0, tail=150.0, suspending=200.0),
    ap_resume=0.15, ap_tail=2.0, ap_susp=0.05, job_true=5.0, job_false=1.0,
    rate_h=dict(speech=60, noise=200, keyword=4), dur=dict(speech=2.0, noise=0.5, keyword=0.8),
    p_vad=dict(speech=0.98, noise=0.3, keyword=1.0),               # VAD가 문을 여는 확률
    p_kws=dict(speech=0.01, noise=0.002, keyword=0.95),             # KWS가 "키워드!"라고 할 확률
    use_dsp=True,                                                   # False면 VAD가 바로 AP를 깨운다
)

def simulate(cfg, hours=24.0, seed=0):
    rng, T, q = random.Random(seed), hours * 3600, []
    for kind, r in cfg["rate_h"].items():                           # 음향 사건을 Poisson으로 뿌린다
        t = rng.expovariate(r / 3600)
        while t < T:
            heapq.heappush(q, (t, "seg", kind)); t += rng.expovariate(r / 3600)
    dsp, ap = Proc(cfg["dsp"], "off"), Proc(cfg["ap"], "suspend")
    st = dict(dsp_hold=0.0, ap_hold=0.0, ap_ready=0.0, lat=[], false_ap=0, true_ap=0)
    def ap_request(t, true_kw, t_kw_end):
        if ap.st in ("suspend", "suspending"):
            ap.go(t, "resume"); ap.wakes += 1                       # resume 시간은 매번 조금씩 다르다
            st["ap_ready"] = t + cfg["ap_resume"] * rng.lognormvariate(0, 0.4)
            heapq.heappush(q, (st["ap_ready"], "ap_ready", None))
        elif ap.st == "tail": ap.go(t, "active")                    # tail 중 새 요청: 다시 active
        ready = max(t, st["ap_ready"])
        job_end = ready + (cfg["job_true"] if true_kw else cfg["job_false"])
        st["ap_hold"] = max(st["ap_hold"], job_end)
        heapq.heappush(q, (job_end, "ap_check", None))
        if true_kw: st["lat"].append(ready - t_kw_end); st["true_ap"] += 1
        else: st["false_ap"] += 1
    while q:
        t, ev, kind = heapq.heappop(q)
        if t > T: break
        if ev == "seg":
            end = t + cfg["dur"][kind]
            if rng.random() >= cfg["p_vad"][kind]: continue          # VAD가 무시
            t_vad = t + 0.03                                         # VAD 판정 30 ms
            hit = rng.random() < cfg["p_kws"][kind]
            if not cfg["use_dsp"]:                                   # MCU → AP 직행: AP가 KWS까지 한다
                ap_request(t_vad, kind == "keyword" and hit, end); continue
            st["dsp_hold"] = max(st["dsp_hold"], end + cfg["dsp_hang"])
            if dsp.st == "off":
                dsp.go(t_vad, "waking"); dsp.wakes += 1
                heapq.heappush(q, (t_vad + cfg["dsp_wake"], "dsp_on", None))
            if hit: heapq.heappush(q, (end + cfg["kws_ms"], "kws_hit", kind))
        elif ev == "dsp_on":
            dsp.go(t, "on"); heapq.heappush(q, (st["dsp_hold"], "dsp_check", None))
        elif ev == "dsp_check" and dsp.st == "on":
            if t >= st["dsp_hold"]: dsp.go(t, "off")
            else: heapq.heappush(q, (st["dsp_hold"], "dsp_check", None))
        elif ev == "kws_hit":
            ap_request(t, kind == "keyword", t - cfg["kws_ms"])
        elif ev == "ap_ready" and ap.st == "resume":
            ap.go(t, "active")
        elif ev == "ap_check" and ap.st == "active" and t >= st["ap_hold"]:
            ap.go(t, "tail"); heapq.heappush(q, (t + cfg["ap_tail"], "ap_tail_end", None))
        elif ev == "ap_tail_end" and ap.st == "tail" and t >= st["ap_hold"] + cfg["ap_tail"] - 1e-9:
            ap.go(t, "suspending"); heapq.heappush(q, (t + cfg["ap_susp"], "ap_off", None))
        elif ev == "ap_off" and ap.st == "suspending":
            ap.go(t, "suspend")
    dsp.go(T, dsp.st); ap.go(T, ap.st)
    return dict(T=T, mcu=cfg["mcu_mw"], dsp=dsp, ap=ap, **st)
```

읽는 법: `Proc.go()`가 상태를 바꿀 때마다 **이전 상태의 전력 × 머문 시간**을 그 상태 이름 아래에 더한다. `dsp_hold`·`ap_hold`는 "적어도 이때까지는 켜 있어야 한다"는 시각이고, 새 사건이 오면 뒤로 밀린다. 이미 깨어 있으면 wake 횟수가 늘지 않는다 — 공식이 놓치는 **겹침 효과**다. `use_dsp=False`는 DSP 단계를 빼고 VAD가 곧장 AP를 깨우는 설계(AP가 KWS까지 함)이며, 이때 "false"는 "AP가 깼지만 확정된 키워드가 없었던 경우"다.

### 5.3 기준 시나리오 24시간

**예제 7(실행)** — 무엇을 확인하나: 5.1절 기준 설정으로 24시간을 돌려 코어별·상태별 평균 전력, wake 횟수, 키워드 → AP 준비 지연 분포를 얻고 손계산과 비교한다.

```python
from wake_sim import CFG, simulate
r = simulate(CFG)
T = r["T"]
print(f"MCU 평균 {r['mcu']:.3f} mW (항상 켜짐)")
for name in ("dsp", "ap"):
    p = r[name]; tot = sum(p.E.values()) / T
    parts = ", ".join(f"{k} {v / T:.3f}" for k, v in sorted(p.E.items(), key=lambda kv: -kv[1]))
    print(f"{name.upper()} 평균 {tot:.3f} mW  wakes {p.wakes}/day  [{parts}]")
total = r["mcu"] + sum(sum(r[n].E.values()) for n in ("dsp", "ap")) / T
print(f"합계 {total:.2f} mW   AP 깨움: 진짜 {r['true_ap']}, false {r['false_ap']}")
lat = sorted(r["lat"]); n = len(lat)
print(f"키워드 끝 → AP ready 지연: p50 {lat[n // 2] * 1000:.0f} ms, p95 {lat[int(n * 0.95)] * 1000:.0f} ms (n={n})")
```

```text
MCU 평균 0.800 mW (항상 켜짐)
DSP 평균 1.033 mW  wakes 2775/day  [on 0.984, off 0.048, waking 0.001]
AP 평균 6.296 mW  wakes 111/day  [suspend 2.974, active 2.857, tail 0.385, resume 0.066, suspending 0.013]
합계 8.13 mW   AP 깨움: 진짜 96, false 16
키워드 끝 → AP ready 지연: p50 206 ms, p95 344 ms (n=96)
```

출력에서 볼 것:

- 손계산(DSP ≈ 1.09, AP ≈ 6.2 mW)과 시뮬레이션(1.03, 6.30)이 가깝다. 차이는 겹침(겹치면 DSP on 시간이 줄어든다)과 난수 때문이다. **손계산으로 먼저 예상하고 시뮬레이션으로 확인**하는 습관이 D7에서와 같다.
- AP 6.3 mW 중 **suspend 바닥이 2.97 mW로 가장 크다**. 진짜 일(active 2.86)보다 "누워 있는 비용"이 크다 — 예제 2의 결론과 같고, companion MCU로 SoC를 완전히 끄는 설계(2.2절)가 노리는 것이 바로 이 항이다.
- 키워드 끝 → AP 준비 지연: p50 ≈ 206 ms, p95 ≈ 344 ms. KWS 50 ms + resume(평균 150 ms, 꼬리 있음). 사용자 체감 지연 예산(L4)에서 이 수백 ms를 빼고 나머지로 ASR 첫 결과까지 가야 한다. pre-roll 링버퍼가 이 구간의 소리를 들고 있어야 한다(1.3절).

아래는 같은 시뮬레이션의 45초 구간을 그린 것이다.

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="52" font-size="12">소리</text><rect x="162.9" y="40" width="25.2" height="16" fill="#888" fill-opacity="0.6"/><rect x="243.2" y="40" width="6.3" height="16" fill="#e08a3c" fill-opacity="0.6"/><rect x="368.6" y="40" width="25.2" height="16" fill="#888" fill-opacity="0.6"/><rect x="406.9" y="40" width="6.3" height="16" fill="#e08a3c" fill-opacity="0.6"/><rect x="430.0" y="40" width="10.1" height="16" fill="#3f9a6b" fill-opacity="0.6"/><rect x="562.3" y="40" width="25.2" height="16" fill="#888" fill-opacity="0.6"/><text x="162.9" y="34" font-size="12">말소리</text><text x="235.0" y="34" font-size="12">소음</text><text x="422.7" y="34" font-size="12">키워드</text>
<text x="10" y="100" font-size="12">MCU</text><line x1="80.0" y1="104" x2="647.0" y2="104" stroke="currentColor" stroke-opacity="0.4"/><polyline points="80.0,98 647.0,98" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="537.0" y="92" font-size="12">0.8 mW 항상 켜짐</text><text x="10" y="170" font-size="12">DSP</text><text x="10" y="186" font-size="12">0–20 mW</text><line x1="80.0" y1="190" x2="647.0" y2="190" stroke="currentColor" stroke-opacity="0.4"/>
<polygon points="80.0,190 80.0,189.8 163.3,189.8 163.3,145.0 163.3,145.0 163.3,130.0 191.9,130.0 191.9,189.8 243.6,189.8 243.6,145.0 243.7,145.0 243.7,130.0 253.3,130.0 253.3,189.8 369.0,189.8 369.0,145.0 369.0,145.0 369.0,130.0 397.6,130.0 397.6,189.8 407.3,189.8 407.3,145.0 407.3,145.0 407.3,130.0 417.0,130.0 417.0,189.8 430.4,189.8 430.4,145.0 430.4,145.0 430.4,130.0 443.9,130.0 443.9,189.8 562.7,189.8 562.7,145.0 562.7,145.0 562.7,130.0 591.3,130.0 591.3,189.8 647.0,189.8 647.0,190" fill="#e08a3c" fill-opacity="0.3" stroke="none"/><polyline points="80.0,189.8 163.3,189.8 163.3,145.0 163.3,145.0 163.3,130.0 191.9,130.0 191.9,189.8 243.6,189.8 243.6,145.0 243.7,145.0 243.7,130.0 253.3,130.0 253.3,189.8 369.0,189.8 369.0,145.0 369.0,145.0 369.0,130.0 397.6,130.0 397.6,189.8 407.3,189.8 407.3,145.0 407.3,145.0 407.3,130.0 417.0,130.0 417.0,189.8 430.4,189.8 430.4,145.0 430.4,145.0 430.4,130.0 443.9,130.0 443.9,189.8 562.7,189.8 562.7,145.0 562.7,145.0 562.7,130.0 591.3,130.0 591.3,189.8 647.0,189.8" fill="none" stroke="#e08a3c" stroke-width="1.5"/>
<text x="10" y="280" font-size="12">AP</text><text x="10" y="296" font-size="12">0–500 mW</text><line x1="80.0" y1="300" x2="647.0" y2="300" stroke="currentColor" stroke-opacity="0.4"/>
<polygon points="80.0,300 80.0,299.5 394.4,299.5 394.4,252.0 395.1,252.0 395.1,220.0 407.7,220.0 407.7,276.0 432.9,276.0 432.9,268.0 433.5,268.0 433.5,299.5 440.7,299.5 440.7,252.0 443.4,252.0 443.4,220.0 506.4,220.0 506.4,276.0 531.6,276.0 531.6,268.0 532.2,268.0 532.2,299.5 647.0,299.5 647.0,300" fill="#d0564a" fill-opacity="0.3" stroke="none"/><polyline points="80.0,299.5 394.4,299.5 394.4,252.0 395.1,252.0 395.1,220.0 407.7,220.0 407.7,276.0 432.9,276.0 432.9,268.0 433.5,268.0 433.5,299.5 440.7,299.5 440.7,252.0 443.4,252.0 443.4,220.0 506.4,220.0 506.4,276.0 531.6,276.0 531.6,268.0 532.2,268.0 532.2,299.5 647.0,299.5" fill="none" stroke="#d0564a" stroke-width="1.5"/>
<line x1="80.0" y1="300" x2="80.0" y2="305" stroke="currentColor"/><text x="80.0" y="320" font-size="12" text-anchor="middle">0</text><line x1="143.0" y1="300" x2="143.0" y2="305" stroke="currentColor"/><text x="143.0" y="320" font-size="12" text-anchor="middle">5</text><line x1="206.0" y1="300" x2="206.0" y2="305" stroke="currentColor"/><text x="206.0" y="320" font-size="12" text-anchor="middle">10</text><line x1="269.0" y1="300" x2="269.0" y2="305" stroke="currentColor"/><text x="269.0" y="320" font-size="12" text-anchor="middle">15</text><line x1="332.0" y1="300" x2="332.0" y2="305" stroke="currentColor"/><text x="332.0" y="320" font-size="12" text-anchor="middle">20</text>
<line x1="395.0" y1="300" x2="395.0" y2="305" stroke="currentColor"/><text x="395.0" y="320" font-size="12" text-anchor="middle">25</text><line x1="458.0" y1="300" x2="458.0" y2="305" stroke="currentColor"/><text x="458.0" y="320" font-size="12" text-anchor="middle">30</text><line x1="521.0" y1="300" x2="521.0" y2="305" stroke="currentColor"/><text x="521.0" y="320" font-size="12" text-anchor="middle">35</text><line x1="584.0" y1="300" x2="584.0" y2="305" stroke="currentColor"/><text x="584.0" y="320" font-size="12" text-anchor="middle">40</text><line x1="647.0" y1="300" x2="647.0" y2="305" stroke="currentColor"/><text x="647.0" y="320" font-size="12" text-anchor="middle">45</text>
<text x="363.5" y="340" font-size="12" text-anchor="middle">시간 [s]</text><text x="387.7" y="212" font-size="12" text-anchor="end">false wake (말소리 FA)</text><text x="445.4" y="212" font-size="12">진짜 키워드 → ASR 5 s</text><text x="537.4" y="286" font-size="12">tail 2 s</text>
</svg>
```

그림 5 — 시뮬레이션 trace의 45초 구간(24시간 중 한 조각). 맨 위는 음향 사건(회색 말소리, 주황 소음, 초록 키워드), 그 아래는 코어별 전력. DSP는 VAD가 문을 열 때마다 짧게 켜진다(13 s와 26 s의 짧은 소음도 VAD를 통과해 DSP를 잠깐 깨웠다 — p_vad = 0.3의 비용). 23 s 말소리가 끝날 때 KWS가 잘못 반응해 AP가 1 s 검증 + 2 s tail을 했고(false wake), 28 s의 진짜 키워드에서는 키워드 끝 약 0.27 s 뒤 AP가 준비되어 5 s 세션을 돌렸다.

### 5.4 시나리오 비교 — 무엇이 예산을 먹나

**예제 8** — 무엇을 확인하나: 환경(소음), 모델 품질(KWS false accept), 구조(DSP 단계 유무), 소프트웨어 버그(AP tail이 긴 경우)를 하나씩 바꿔 평균 전력이 어떻게 변하는지.

```python
from wake_sim import CFG, simulate
import copy
def scen(**kw):
    c = copy.deepcopy(CFG)
    for k, v in kw.items():
        if isinstance(v, dict): c[k].update(v)
        else: c[k] = v
    return c
def false_mw(c, r):                        # false wake 1회 비용 × 횟수 (겹침 무시한 근사)
    p = c["ap"]
    e = (p["resume"] * c["ap_resume"] + p["active"] * c["job_false"] + p["tail"] * c["ap_tail"]
         + p["suspending"] * c["ap_susp"])
    return r["false_ap"] * e / r["T"]
S = {"A 기준": scen(),
     "B 시끄러운 카페": scen(rate_h=dict(noise=2000)),
     "C KWS FA x10": scen(p_kws=dict(speech=0.1, noise=0.02)),
     "D DSP 단계 없음": scen(use_dsp=False),
     "E AP tail 10s": scen(ap_tail=10.0),
     "F B+D": scen(rate_h=dict(noise=2000), use_dsp=False)}
print(f"{'시나리오':<14}{'MCU':>6}{'DSP':>7}{'AP':>8}{'합계':>8}  AP깨움(진짜/false)  전이+tail  false몫(근사)")
for name, c in S.items():
    r = simulate(c); T = r["T"]
    d = sum(r["dsp"].E.values()) / T; a = sum(r["ap"].E.values()) / T
    tr = sum(r["ap"].E[k] for k in ("resume", "tail", "suspending")) / T
    print(f"{name:<14}{r['mcu']:>6.2f}{d:>7.2f}{a:>8.2f}{r['mcu'] + d + a:>8.2f}"
          f"  {r['true_ap']:>5}/{r['false_ap']:<6}      {tr / a:>6.1%}   {false_mw(c, r) / a:>6.1%}")
```

```text
시나리오             MCU    DSP      AP      합계  AP깨움(진짜/false)  전이+tail  false몫(근사)
A 기준            0.80   1.03    6.30    8.13     96/16            7.4%     2.5%
B 시끄러운 카페       0.80   3.13    6.54   10.47     96/41            8.6%     6.2%
C KWS FA x10    0.80   1.04    7.84    9.68     95/178          14.3%    22.5%
D DSP 단계 없음     0.80   0.05   32.58   33.43     94/2848         34.3%    86.5%
E AP tail 10s   0.80   1.03    7.79    9.63     96/16           25.6%     4.9%
F B+D           0.80   0.05  130.47  131.32     96/15893        33.6%   120.5%
```

출력에서 볼 것 (표 읽기: "전이+tail"은 AP 에너지 중 resume·tail·suspending 상태 몫, "false몫"은 false wake 1회 비용 × 횟수를 AP 에너지로 나눈 근사):

- **A 기준 8.1 mW**. false wake는 AP 에너지의 2.5%뿐이다 — 이 설계는 건강하다.
- **B 시끄러운 카페**: 소음이 10배가 되어도 합계는 10.5 mW. VAD를 통과한 소음을 DSP의 KWS가 걸러 주므로 AP는 거의 영향을 받지 않고 **DSP가 1.0 → 3.1 mW**로 대신 부담한다. 중간 단계가 "충격 흡수기" 역할을 한다.
- **C KWS false accept ×10**: false wake 16 → 178회/일, AP 에너지의 22.5%가 false wake. 모델의 FA/hour가 곧 전력 예산 항목이라는 B5 8.1절·B9 5.2절의 말이 숫자로 보인다.
- **D DSP 단계 없음**: VAD가 AP를 직접 깨우면 하루 2800번 이상 AP가 깨고, 합계 33 mW(**4배**). AP 에너지의 약 87%가 false wake다. 싼 중간 단계 하나가 비싼 단계의 호출을 막는다.
- **E AP tail 10 s**: 모델도 환경도 같은데 AP가 일이 끝난 뒤 10초 동안 잠들지 않으면(예: 누군가 wakelock을 늦게 놓음) AP가 6.3 → 7.8 mW, 전이+tail 몫이 7% → 26%. **ML 모델이 아니라 드라이버 한 줄이 전력을 먹는** 전형적 사례다.
- **F 시끄러운 환경 + DSP 없음**: 131 mW. AP가 사실상 잠들 틈이 없다. "false몫"이 100%를 넘는 것은 근사가 깨졌다는 신호다 — false wake들이 서로 겹쳐서 resume·tail을 공유하고 있으므로 1회 비용 × 횟수가 실제보다 크게 나온다. AP가 **거의 항상 깨어 있다**는 뜻으로 읽는다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="142" y="45" font-size="12" text-anchor="end">A 기준</text><rect x="150.0" y="30" width="2.5" height="22" fill="#4a7bd0" fill-opacity="0.75"/><rect x="152.5" y="30" width="3.2" height="22" fill="#e08a3c" fill-opacity="0.75"/><rect x="155.8" y="30" width="19.8" height="22" fill="#d0564a" fill-opacity="0.75"/><text x="181.6" y="45" font-size="12">8.1 mW</text><text x="142" y="85" font-size="12" text-anchor="end">B 시끄러운 카페</text><rect x="150.0" y="70" width="2.5" height="22" fill="#4a7bd0" fill-opacity="0.75"/><rect x="152.5" y="70" width="9.8" height="22" fill="#e08a3c" fill-opacity="0.75"/><rect x="162.4" y="70" width="20.6" height="22" fill="#d0564a" fill-opacity="0.75"/>
<text x="188.9" y="85" font-size="12">10.5 mW</text><text x="142" y="125" font-size="12" text-anchor="end">C KWS FA ×10</text><rect x="150.0" y="110" width="2.5" height="22" fill="#4a7bd0" fill-opacity="0.75"/><rect x="152.5" y="110" width="3.3" height="22" fill="#e08a3c" fill-opacity="0.75"/><rect x="155.8" y="110" width="24.6" height="22" fill="#d0564a" fill-opacity="0.75"/><text x="186.4" y="125" font-size="12">9.7 mW</text><text x="142" y="165" font-size="12" text-anchor="end">D DSP 단계 없음</text><rect x="150.0" y="150" width="2.5" height="22" fill="#4a7bd0" fill-opacity="0.75"/><rect x="152.5" y="150" width="0.2" height="22" fill="#e08a3c" fill-opacity="0.75"/>
<rect x="152.7" y="150" width="102.4" height="22" fill="#d0564a" fill-opacity="0.75"/><text x="261.1" y="165" font-size="12">33.4 mW</text><text x="142" y="205" font-size="12" text-anchor="end">E AP tail 10 s</text><rect x="150.0" y="190" width="2.5" height="22" fill="#4a7bd0" fill-opacity="0.75"/><rect x="152.5" y="190" width="3.2" height="22" fill="#e08a3c" fill-opacity="0.75"/><rect x="155.8" y="190" width="24.5" height="22" fill="#d0564a" fill-opacity="0.75"/><text x="186.2" y="205" font-size="12">9.6 mW</text><text x="142" y="245" font-size="12" text-anchor="end">F B + D</text><rect x="150.0" y="230" width="2.5" height="22" fill="#4a7bd0" fill-opacity="0.75"/>
<rect x="152.5" y="230" width="0.2" height="22" fill="#e08a3c" fill-opacity="0.75"/><rect x="152.7" y="230" width="410.0" height="22" fill="#d0564a" fill-opacity="0.75"/><text x="568.7" y="245" font-size="12">131.3 mW</text><line x1="150" y1="24" x2="150" y2="270" stroke="currentColor"/><line x1="150.0" y1="270" x2="150.0" y2="275" stroke="currentColor"/><text x="150.0" y="289" font-size="12" text-anchor="middle">0</text><line x1="212.9" y1="270" x2="212.9" y2="275" stroke="currentColor"/><text x="212.9" y="289" font-size="12" text-anchor="middle">20</text>
<line x1="275.7" y1="270" x2="275.7" y2="275" stroke="currentColor"/><text x="275.7" y="289" font-size="12" text-anchor="middle">40</text><line x1="338.6" y1="270" x2="338.6" y2="275" stroke="currentColor"/><text x="338.6" y="289" font-size="12" text-anchor="middle">60</text><line x1="401.4" y1="270" x2="401.4" y2="275" stroke="currentColor"/><text x="401.4" y="289" font-size="12" text-anchor="middle">80</text><line x1="464.3" y1="270" x2="464.3" y2="275" stroke="currentColor"/><text x="464.3" y="289" font-size="12" text-anchor="middle">100</text><line x1="527.1" y1="270" x2="527.1" y2="275" stroke="currentColor"/><text x="527.1" y="289" font-size="12" text-anchor="middle">120</text>
<line x1="590.0" y1="270" x2="590.0" y2="275" stroke="currentColor"/><text x="590.0" y="289" font-size="12" text-anchor="middle">140</text><line x1="150" y1="270" x2="590.0" y2="270" stroke="currentColor"/><rect x="420" y="150" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75"/><text x="438" y="161" font-size="12">MCU</text><rect x="490" y="150" width="12" height="12" fill="#e08a3c" fill-opacity="0.75"/><text x="508" y="161" font-size="12">DSP</text><rect x="560" y="150" width="12" height="12" fill="#d0564a" fill-opacity="0.75"/><text x="578" y="161" font-size="12">AP</text><text x="420" y="185" font-size="12">평균 전력 [mW], 24 h 시뮬레이션</text>
</svg>
```

그림 6 — 시나리오별 평균 전력(24시간 시뮬레이션). 파랑 MCU, 주황 DSP, 빨강 AP. MCU는 모든 시나리오에서 같고, 차이는 전부 "비싼 코어를 몇 번, 얼마나 오래 깨웠나"에서 나온다.

배터리로 바꿔 보자. 가상의 배터리 1.0 Wh(= 3.85 V에서 약 260 mAh, 웨어러블급)라면, **듣는 경로만** 따졌을 때:

```
A 기준          8.13 mW → 1000 mWh / 8.13  ≈ 123 h
D DSP 단계 없음 33.4 mW → 1000 / 33.4      ≈  30 h
F 카페 + DSP 없음 131 mW → 1000 / 131      ≈ 7.6 h
```

말로 하면: 같은 모델, 같은 칩이라도 **cascade 구조와 false wake 관리**만으로 배터리 수명이 16배 달라진다. 실제로는 여기에 디스플레이·radio·센서 로깅이 더해진다(D7 6절).

### 5.5 설계 교훈

1. **바닥 전력을 먼저 본다.** 기준 시나리오에서 AP suspend 바닥이 AP 에너지의 절반이었다. 모델 최적화보다 SoC의 suspend 상태·companion MCU 구조가 더 큰 레버일 수 있다.
2. **false wake는 단가 × 횟수다.** 단가(resume + 검증 + tail)를 줄이거나(가벼운 2차 검증을 DSP에서), 횟수를 줄인다(FA가 낮은 KWS, 중간 단계 추가).
3. **전이 비용은 모델 밖에 있다.** resume 시간, tail 길이, wakelock 해제 시점은 드라이버·OS 설정이다. edge ML 엔지니어가 커널 로그와 전류 파형을 같이 봐야 하는 이유다.
4. **지연과 전력은 교환한다.** AP를 retention으로 더 얕게 재우면 resume이 빨라지지만 바닥이 오른다. tail을 길게 하면 연속 질문 응답은 빠르지만 전력이 샌다. 숫자로 정한다 (I1).
5. **시뮬레이터를 측정으로 보정한다.** 이 모델의 상수 하나하나(상태별 mW, resume ms)는 실기기에서 Power Analyzer + GPIO 트리거로 재서 채운다(K3). Don이 sign-off에서 하던 일의 연장이다.

---

## 6. 부팅과 생명주기 — 누가 누구를 깨우고, 죽으면 어떻게 되나

### 6.1 누가 누구를 부팅하나

이기종 SoC에서 "부팅"은 한 번이 아니다. 코어마다 펌웨어 이미지가 따로 있고, 대개 **AP 쪽 보안 부팅 체인이 먼저 올라온 뒤 나머지 서브시스템의 이미지를 로드**한다. (always-on island는 SoC에 따라 AP보다 먼저 ROM에서 깨어나는 구조도 있다 — 세부는 벤더마다 다르다.)

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="e8c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10" y="24" font-size="13">AP 쪽 chain of trust (단계마다 다음 이미지의 서명을 검증)</text><rect x="10" y="38" width="118" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="69" y="59" font-size="12" text-anchor="middle">Boot ROM</text><text x="69" y="77" font-size="12" text-anchor="middle">변경 불가</text><line x1="128" y1="63" x2="140" y2="63" stroke="currentColor" marker-end="url(#e8c)"/>
<rect x="142" y="38" width="118" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="201" y="59" font-size="12" text-anchor="middle">1단 bootloader</text><text x="201" y="77" font-size="12" text-anchor="middle">서명 검증</text><line x1="260" y1="63" x2="272" y2="63" stroke="currentColor" marker-end="url(#e8c)"/><rect x="274" y="38" width="118" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="333" y="59" font-size="12" text-anchor="middle">TEE 펌웨어</text><text x="333" y="77" font-size="12" text-anchor="middle">키 · 인증</text><line x1="392" y1="63" x2="404" y2="63" stroke="currentColor" marker-end="url(#e8c)"/>
<rect x="406" y="38" width="118" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="465" y="59" font-size="12" text-anchor="middle">OS bootloader</text><text x="465" y="77" font-size="12" text-anchor="middle">kernel 검증</text><line x1="524" y1="63" x2="536" y2="63" stroke="currentColor" marker-end="url(#e8c)"/><rect x="538" y="38" width="118" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="597" y="59" font-size="12" text-anchor="middle">Linux kernel</text><text x="597" y="77" font-size="12" text-anchor="middle">드라이버 probe</text><polyline points="587,88 587,112 77,112" fill="none" stroke="currentColor"/>
<text x="330" y="106" font-size="12" text-anchor="middle">remoteproc / PIL 류: 서브시스템 이미지를 메모리에 올리고, 검증 후 reset 해제</text><line x1="77" y1="112" x2="77" y2="130" stroke="currentColor" marker-end="url(#e8c)"/><rect x="10" y="132" width="134" height="50" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/><text x="77" y="153" font-size="12" text-anchor="middle">audio DSP</text><text x="77" y="171" font-size="12" text-anchor="middle">KWS · 전처리</text><line x1="245" y1="112" x2="245" y2="130" stroke="currentColor" marker-end="url(#e8c)"/><rect x="178" y="132" width="134" height="50" rx="6" fill="#d0564a" fill-opacity="0.2" stroke="currentColor"/>
<text x="245" y="153" font-size="12" text-anchor="middle">NPU · compute DSP</text><text x="245" y="171" font-size="12" text-anchor="middle">모델 실행</text><line x1="413" y1="112" x2="413" y2="130" stroke="currentColor" marker-end="url(#e8c)"/><rect x="346" y="132" width="134" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="413" y="153" font-size="12" text-anchor="middle">sensor/AON 코어</text><text x="413" y="171" font-size="12" text-anchor="middle">센서 허브</text><line x1="581" y1="112" x2="581" y2="130" stroke="currentColor" marker-end="url(#e8c)"/><rect x="514" y="132" width="134" height="50" rx="6" fill="#888" fill-opacity="0.2" stroke="currentColor"/>
<text x="581" y="153" font-size="12" text-anchor="middle">modem · Wi-Fi</text><text x="581" y="171" font-size="12" text-anchor="middle">무선 FW</text><rect x="10" y="214" width="250" height="62" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="135" y="236" font-size="12" text-anchor="middle">companion MCU (칩 밖)</text><text x="135" y="254" font-size="12" text-anchor="middle">자기 flash에서 독립 부팅, 먼저 깨어 있음</text><text x="135" y="270" font-size="12" text-anchor="middle">업데이트만 AP가 DFU로 밀어 넣음</text><rect x="290" y="214" width="380" height="62" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="currentColor" stroke-dasharray="5 3"/>
<text x="480" y="236" font-size="12" text-anchor="middle">서브시스템이 죽으면 (watchdog · fatal error)</text><text x="480" y="254" font-size="12" text-anchor="middle">→ 덤프 수집 → 그 이미지만 다시 로드 · 재시작</text><text x="480" y="270" font-size="12" text-anchor="middle">AP와 다른 서브시스템은 계속 돈다 (SSR 개념)</text><text x="340" y="304" font-size="12" text-anchor="middle">OTA는 이 모든 이미지를 "한 세트"로 버전 관리해야 한다 (J5)</text>
</svg>
```

그림 7 — 부팅 체인의 일반적 모양(특정 벤더 사양이 아니라 개념도). 위: 변경 불가능한 Boot ROM부터 단계마다 다음 이미지의 서명을 검증하며 올라간다(chain of trust). 가운데: kernel이 DSP·NPU·센서·무선 서브시스템의 이미지를 메모리에 올리고, 검증이 끝나면 reset을 풀어 준다. 아래 왼쪽: 칩 밖 companion MCU는 자기 flash에서 독립적으로 부팅한다. 아래 오른쪽: 서브시스템 하나가 죽으면 그것만 다시 올린다.

- **chain of trust**: Boot ROM은 칩에 구워진 공개키 해시로 1단 bootloader의 서명을 검증하고, 그것이 다음 단계를 검증하는 식이다. 서브시스템 펌웨어도 보통 서명 검증을 거친다(누가 검증하는지는 SoC마다 다름 — TEE나 전용 보안 블록이 맡는 경우가 많다고 알려져 있다).
- **firmware loading**: Linux의 **remoteproc** 프레임워크가 표준 모델이다. ELF 이미지를 carve-out 메모리(부팅 때 예약해 둔 전용 DRAM 영역)에 올리고, 이미지 안의 resource table로 공유 메모리·vring 위치를 합의한 뒤 원격 코어를 시작한다. Qualcomm Android kernel에서는 PIL(peripheral image loader)이라는 이름이 오래 쓰였다 (hedge).
- **companion MCU**: 자기 flash에서 먼저 부팅해 항상 깨어 있다. AP는 "MCU가 살아 있다"를 부팅 때 확인하고(버전 교환), 필요하면 DFU(펌웨어 업데이트 모드)로 새 이미지를 넣는다.

Don 연결: SSD에서 ROM → bootloader → 메인 FW, 그리고 메인 코어가 보조 코어(데이터 경로 Xtensa 등)의 이미지를 TCM에 적재하고 reset을 풀던 순서와 같다. 새 실리콘 bring-up에서 "보조 코어가 reset을 풀었는데 첫 명령을 안 받는다"를 디버그해 봤다면, 그 경험이 그대로 쓰인다.

### 6.2 서브시스템 재시작 (SSR) — 하나가 죽어도 기기는 산다

DSP 펌웨어가 예외로 죽거나 watchdog이 걸리면, 옛날 방식은 기기 전체 리셋이다. 모바일 SoC는 보통 **그 서브시스템만** 다시 올린다. Qualcomm 계열에서는 이것을 SSR(subsystem restart)이라고 부른다고 알려져 있다 (구체 동작은 kernel·펌웨어 버전마다 다름). Linux remoteproc에도 crash 감지 → coredump → recovery(재부팅) 흐름이 있다.

재시작이 **안전하려면** IPC 계층이 준비되어 있어야 한다:

1. **감지**: watchdog IRQ, 상대가 쓴 "fatal error" 플래그, heartbeat 누락.
2. **알림**: 그 서브시스템을 쓰던 모든 클라이언트(오디오 HAL, ML 런타임, 센서 서비스)에게 "DSP down" 이벤트.
3. **정리**: 진행 중이던 요청에 에러를 돌려주고, 공유 버퍼의 소유권을 회수하고, 링 head/tail·seq를 초기화.
4. **덤프**: 원격 코어 메모리를 저장(ramdump) — 나중에 root cause 분석용.
5. **재로드 · 재협상**: 이미지 다시 로드 → 버전·기능 교환 → 모델·그래프 다시 올리기 → "DSP up" 이벤트.

Hark 같은 기기에서 이 흐름이 중요한 이유: KWS가 도는 DSP가 죽었는데 아무도 모르면, 기기는 **조용히 귀머거리**가 된다. always-on 경로에는 heartbeat("최근 N초 동안 VAD 프레임을 몇 개 처리했나")를 두고 AP나 MCU가 감시해야 한다.

### 6.3 여러 이미지의 OTA (J5 연결)

이기종 기기의 펌웨어는 "한 개"가 아니라 **세트**다: AP OS, DSP 이미지, NPU 모델·컨텍스트 바이너리, island 이미지, companion MCU 이미지, 무선 칩 펌웨어. OTA 설계 체크포인트:

- **호환성 매트릭스**: IPC 프로토콜 버전, 모델 포맷 버전, 런타임 버전. "AP는 새 버전, MCU는 옛 버전"인 중간 상태가 반드시 생긴다(업데이트 도중 전원이 꺼지면).
- **순서**: 보통 "새 AP가 옛 MCU와도 대화할 수 있게" 만들고, AP 먼저 → MCU 나중. 반대로 하면 옛 AP가 새 MCU를 못 알아듣는 창이 생긴다.
- **A/B 슬롯**: 각 이미지가 A/B를 가지면 실패 시 롤백 가능. MCU flash가 작으면 A/B를 못 둘 수 있다 — 그러면 bootloader가 DFU 복구 경로를 반드시 가져야 한다.
- **모델만 바꾸기**: 모델 파일이 펌웨어와 분리되어 있으면 ML 팀이 더 자주 배포할 수 있다. 대신 "모델 v7은 런타임 ≥ v3 필요" 같은 제약을 메타데이터로 검사한다.

---

## 7. 이기종 시스템 디버깅 — 시간을 맞추는 것이 절반이다

### 7.1 왜 어려운가

- 코어마다 **자기 시계**(카운터 주파수, 시작 시점, drift)가 있다. DSP 로그의 "t=1234567"과 AP 로그의 "t=89.123 s"는 그냥 비교할 수 없다.
- 코어마다 **다른 도구**를 쓴다: AP는 `logcat`·`dmesg`·ftrace, DSP는 벤더 로그 채널, MCU는 SWO·UART. 
- 어떤 코어는 **자고 있어서 로그를 못 쓴다**. 로그를 쓰려고 깨우면 버그가 사라진다(타이밍이 바뀜).
- 버그의 대부분이 **경계**(IPC, 캐시, 전원 전이)에서 생긴다. 각 코어 로그만 보면 둘 다 "정상"이다.

### 7.2 시간 동기화 — Don의 도메인

가장 좋은 방법은 하드웨어다: 많은 SoC에는 **모든 코어가 읽을 수 있는 전역 카운터**(always-on 영역에서 도는 system counter 류)가 있다. 모든 로그에 이 값을 찍으면 동기화는 끝이다. 그런 것이 없거나 칩 밖 MCU라면 **메시지로 시계를 맞춘다**.

NTP와 같은 4-타임스탬프 방식:

```
AP  t1 ──── 요청 ────►  DSP t2 (DSP 시계로 받은 시각)
AP  t4 ◄─── 응답 ─────  DSP t3 (DSP 시계로 보낸 시각)

offset = ((t2 − t1) + (t3 − t4)) / 2      DSP 시계 − AP 시계
delay  = (t4 − t1) − (t3 − t2)            순수 왕복 전송 시간
```

말로 하면: 가는 길과 오는 길이 같은 시간이 걸린다고 가정하면, 두 시계 차이는 "받은 시각 − 보낸 시각"의 평균이다. 가는 길과 오는 길이 다르면(비대칭) 그 차이의 절반만큼 오차가 생긴다.

손계산: t1 = 1000, t2 = 2240, t3 = 2241, t4 = 1010 (µs). offset = ((2240 − 1000) + (2241 − 1010)) / 2 = (1240 + 1231) / 2 = 1235.5 µs. delay = 10 − 1 = 9 µs.

그리고 **drift**: 두 크리스털이 40 ppm 다르면 60초에 2.4 ms가 벌어진다. 한 번 잰 offset만 믿으면 1분 뒤 로그는 2.4 ms 어긋난다 — 10 ms 오디오 프레임 기준으로 1/4 프레임이다.

**예제 9** — 무엇을 확인하나: doorbell 지연(3.4절에서 잰 것과 비슷한 µs급 분포)을 가진 ping-pong 60번으로 offset과 drift를 추정하고, DSP 로그 시각을 AP 시간축으로 되돌릴 때 오차가 얼마나 줄어드는지.

```python
# AP 시계와 DSP 시계 맞추기: doorbell ping-pong 타임스탬프 4개로 offset·drift 추정 (NTP 방식 + 직선 fit)
import numpy as np
rng = np.random.default_rng(7)
OFF_US, PPM = 1234.5, 40.0                      # 참값: DSP 시계 = AP 시계 × (1+40e-6) + 1234.5 µs
dsp_clock = lambda t_ap: t_ap * (1 + PPM * 1e-6) + OFF_US
est_t, est_off, rtts = [], [], []
for k in range(60):                             # 1초마다 한 번씩 60번
    t1 = k * 1e6                                              # AP: 요청 보낸 시각 (AP 시계, µs)
    d_up = 2.5 + rng.exponential(3.0)                         # doorbell 지연: 바닥 2.5 µs + 꼬리
    d_dn = 2.5 + rng.exponential(3.0)
    t2 = dsp_clock(t1 + d_up); t3 = t2 + 1.0                  # DSP: 받은 시각, 보낸 시각 (DSP 시계)
    t4 = t1 + d_up + 1.0 / (1 + PPM * 1e-6) + d_dn            # AP: 응답 받은 시각
    est_off.append(((t2 - t1) + (t3 - t4)) / 2)               # NTP offset 식: 지연이 대칭이라고 가정
    rtts.append((t4 - t1) - (t3 - t2)); est_t.append(t1)
est_t, est_off, rtts = map(np.array, (est_t, est_off, rtts))
true_off = dsp_clock(est_t) - est_t
print(f"한 번 측정 오차: 평균 {np.mean(np.abs(est_off - true_off)):.2f} µs, 최대 {np.max(np.abs(est_off - true_off)):.2f} µs")
keep = rtts <= np.percentile(rtts, 30)                        # RTT가 짧은 교환만 믿는다 (지연 비대칭이 작다)
slope, icpt = np.polyfit(est_t[keep], est_off[keep], 1)
print(f"직선 fit: drift {slope * 1e6:.2f} ppm (참 {PPM}), offset@0 {icpt:.2f} µs (참 {OFF_US})")
t_log = 59.5e6                                                # DSP 로그의 시각을 AP 시간축으로 되돌리기
dsp_ts = dsp_clock(t_log)
naive = dsp_ts - est_off[0]                                   # 처음 한 번 잰 offset만 빼는 경우
fitted = (dsp_ts - icpt) / (1 + slope)
print(f"59.5 s 시점 DSP 로그 → AP 시각 오차: offset 1회만 {naive - t_log:.1f} µs, fit {fitted - t_log:.2f} µs")
```

```text
한 번 측정 오차: 평균 1.63 µs, 최대 7.93 µs
직선 fit: drift 40.01 ppm (참 40.0), offset@0 1234.21 µs (참 1234.5)
59.5 s 시점 DSP 로그 → AP 시각 오차: offset 1회만 2380.5 µs, fit -0.42 µs
```

출력에서 볼 것: 한 번의 측정은 평균 1.6 µs, 최대 7.9 µs 틀린다(지연 비대칭 때문). RTT가 짧은 교환만 골라(비대칭도 작을 가능성이 높다) 직선을 맞추면 drift 40.01 ppm, offset 1234.21 µs로 참값에 붙는다. 59.5초 시점 로그를 되돌릴 때 **처음 offset만 쓰면 2380 µs(= 40 ppm × 59.5 s) 틀리고, fit을 쓰면 0.4 µs** 틀린다. 실무에서는 이 fit을 주기적으로 갱신하고(온도가 바뀌면 drift도 바뀐다), 로그 후처리 도구가 모든 코어 로그를 AP 시간축으로 변환해 한 타임라인에 합친다 (G7).

### 7.3 공유 로그 버퍼

코어마다 printf를 UART로 내보내면 느리고, 타이밍을 바꾸고, 자는 코어는 못 쓴다. 대신:

- 공유 메모리(또는 AON SRAM)에 코어별 **바이너리 로그 링**을 둔다. 레코드 = `{전역 timestamp, core id, event id, arg0, arg1}` 16~32 바이트. 문자열은 빌드 때 ID로 바꾼다(포맷 문자열은 호스트 쪽 사전에).
- 쓰기는 3절의 SPSC 규칙 그대로(각 코어가 producer, 로그 수집기가 consumer). 넘치면 덮어쓰기 + "dropped N" 카운터.
- 크래시 때 이 링이 ramdump에 포함되게 한다 — "죽기 직전 1000개 이벤트"가 최고의 단서다.
- ARM CoreSight의 **STM(System Trace Macrocell)** 같은 하드웨어 trace가 SoC에 있으면 여러 코어의 계측 메시지를 하드웨어 timestamp와 함께 한 스트림으로 모을 수 있다. 사용 가능 여부는 SoC·보안 설정에 달려 있다.
- 가장 싸고 확실한 도구: **GPIO 토글 + 로직 애널라이저 + 전류 프로브**. "DSP IRQ 진입", "AP resume 완료", "KWS 판정"에 GPIO를 하나씩 배정하면 전류 파형과 같은 화면에서 상태 전이가 보인다. Don이 전력 sign-off에서 GPIO 트리거로 구간을 자르던 방법 그대로다.

### 7.4 사례 — DSP와 AP 사이의 race를 잡는 순서

증상: 하루에 몇 번, KWS 이벤트 뒤 AP가 받은 pre-roll 오디오의 앞 20 ms가 **이전 발화의 소리**다.

1. **재현율 높이기**: 스트레스 스크립트로 키워드를 연속 재생, AP 쪽에 인위적 지연을 넣어 창을 넓힌다.
2. **메시지에 증거 심기**: 각 오디오 버퍼 헤더에 `{seq, 생성 timestamp, CRC}`를 넣고, AP가 받을 때 검증·기록한다. "seq는 맞는데 CRC가 틀림"이면 **데이터가 늦게 보인 것**(캐시·순서 문제), "seq가 옛것"이면 **인덱스·소유권 로직** 문제다.
3. **공유 로그 링으로 순서 복원**: DSP "buf 12 채움 → clean → head=13 → doorbell", AP "IRQ → head 읽음 → invalidate → buf 12 읽음"을 전역 시각으로 한 줄에 놓는다.
4. **흔한 원인 후보**: (a) DSP가 cache clean 전에 head를 갱신, (b) AP가 invalidate를 빼먹거나 라인 정렬이 안 된 버퍼를 invalidate, (c) doorbell 쓰기 전 barrier 누락, (d) AP가 ACK 전에 버퍼를 DSP에 돌려줌(소유권 위반), (e) SSR 뒤 링 초기화가 반쯤만 됨.
5. **고치고 증명하기**: 수정 후 같은 스트레스를 N시간, CRC 불일치 0건. 회귀 테스트로 남긴다(J6).

### 7.5 실무 체크리스트

- [ ] 모든 코어 로그가 같은 시간축(전역 카운터 또는 fit된 변환)으로 합쳐지는가
- [ ] 모든 IPC 메시지에 seq·타입·길이가 있고, 받는 쪽이 누락·중복을 센다
- [ ] 공유 버퍼마다 소유권 규칙과 캐시 유지보수 지점이 문서화되어 있다
- [ ] 서브시스템마다 heartbeat와 "마지막으로 처리한 프레임 번호" 카운터가 있다
- [ ] 크래시 때 로그 링과 IPC 상태가 dump에 포함된다
- [ ] 전원 전이(resume, suspend, retention 진입)가 로그 이벤트로 남는다
- [ ] 핵심 이벤트에 GPIO 마커가 있어 전류 파형과 겹쳐 볼 수 있다
- [ ] IPC 프로토콜 버전이 부팅 때 교환·기록된다

---

## 8. 이웃: 무선 칩과 coexistence — Don의 현재 일

### 8.1 무선 칩은 어떻게 붙나

Hark 같은 기기는 Cellular·Wi-Fi·BT·GNSS·NFC·UWB를 가질 수 있다(추정). 이들은 SoC 안에 통합되기도 하고(모바일 SoC의 modem은 통합인 경우가 많다고 알려져 있다), 별도 칩으로 붙기도 한다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<rect x="10" y="30" width="200" height="285" rx="8" fill="#4a7bd0" fill-opacity="0.1" stroke="currentColor"/><text x="110" y="52" font-size="13" text-anchor="middle">SoC</text><text x="20" y="76" font-size="12">PCIe root complex</text><text x="20" y="96" font-size="12">UART · SPI · I2C 컨트롤러</text><text x="20" y="116" font-size="12">GPIO · wake IRQ</text><text x="20" y="176" font-size="12">AP · DSP · NPU (AI 작업)</text><text x="20" y="196" font-size="12">열 · 전력 관리자</text><rect x="20" y="226" width="180" height="80" rx="6" fill="#888" fill-opacity="0.15" stroke="currentColor"/>
<text x="110" y="248" font-size="12" text-anchor="middle">modem (SoC에 통합된</text><text x="110" y="264" font-size="12" text-anchor="middle">경우가 많다 — 추정)</text><text x="110" y="282" font-size="12" text-anchor="middle">RFFE · SPMI master</text><rect x="330" y="30" width="150" height="70" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/><text x="405" y="60" font-size="12" text-anchor="middle">Wi-Fi / BT combo</text><text x="405" y="78" font-size="12" text-anchor="middle">(별도 칩)</text><line x1="210" y1="45" x2="330" y2="45" stroke="currentColor" stroke-width="2"/><text x="270" y="40" font-size="12" text-anchor="middle">PCIe (Wi-Fi)</text>
<line x1="210" y1="65" x2="330" y2="65" stroke="currentColor"/><text x="270" y="61" font-size="12" text-anchor="middle">UART (BT HCI)</text><line x1="210" y1="88" x2="330" y2="88" stroke="currentColor" stroke-dasharray="2 2"/><text x="270" y="84" font-size="12" text-anchor="middle">EN · host-wake</text><rect x="330" y="120" width="150" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="405" y="145" font-size="12" text-anchor="middle">UWB</text><line x1="210" y1="140" x2="330" y2="140" stroke="currentColor"/><text x="270" y="136" font-size="12" text-anchor="middle">SPI + IRQ</text>
<rect x="330" y="175" width="150" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="405" y="200" font-size="12" text-anchor="middle">NFC</text><line x1="210" y1="195" x2="330" y2="195" stroke="currentColor"/><text x="270" y="191" font-size="12" text-anchor="middle">I2C + IRQ</text><rect x="330" y="228" width="150" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="405" y="253" font-size="12" text-anchor="middle">GNSS</text><line x1="210" y1="248" x2="330" y2="248" stroke="currentColor"/><text x="270" y="244" font-size="12" text-anchor="middle">UART/SPI</text>
<polyline points="330,95 318,95 318,236 200,236" fill="none" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="5 3"/><text x="262" y="224" font-size="12" text-anchor="middle">coex 신호</text><rect x="520" y="110" width="150" height="100" rx="6" fill="#d0564a" fill-opacity="0.15" stroke="currentColor"/><text x="595" y="140" font-size="12" text-anchor="middle">RF front-end</text><text x="595" y="158" font-size="12" text-anchor="middle">PA · LNA · switch</text><text x="595" y="176" font-size="12" text-anchor="middle">antenna tuner</text><rect x="520" y="240" width="150" height="50" rx="6" fill="#888" fill-opacity="0.2" stroke="currentColor"/>
<text x="595" y="270" font-size="12" text-anchor="middle">PMIC</text><polyline points="200,300 505,300 505,190 520,190" fill="none" stroke="currentColor"/><text x="360" y="296" font-size="12" text-anchor="middle">MIPI RFFE (FEM 제어)</text><line x1="200" y1="280" x2="520" y2="272" stroke="currentColor"/><text x="430" y="286" font-size="12" text-anchor="middle">SPMI</text><line x1="480" y1="65" x2="560" y2="110" stroke="currentColor" stroke-dasharray="2 2"/><text x="590" y="70" font-size="12" text-anchor="middle">RF 경로</text>
</svg>
```

그림 8 — 무선 칩이 SoC에 붙는 일반적인 방식(개념도, 특정 제품 구성 아님). 데이터 경로(PCIe, UART, SPI, I2C)와 제어·전원 경로(GPIO, MIPI RFFE, SPMI), 그리고 칩 사이 coexistence 신호(빨간 점선)가 따로 있다.

| 연결 | 무엇이 흐르나 | Don 경험과의 연결 |
|---|---|---|
| PCIe (또는 SDIO) | Wi-Fi 데이터, 때로 modem 데이터 | 링크 학습·ASPM 저전력 상태·L1 substates가 전력과 지연에 직결 |
| UART (HCI) | BT 명령·데이터 | 흐름 제어(RTS/CTS), 자는 쪽 첫 바이트 유실 — 2.3절과 같은 문제 |
| SPI / I2C + IRQ | UWB, NFC, GNSS 등 저속 칩 | 버스 공유, IRQ 폭주 |
| GPIO | enable, reset, host-wake, device-wake | 부팅 중 기본 상태, 극성, glitch |
| MIPI RFFE | PA·LNA·스위치·튜너 같은 RF front-end 제어 (2선) | 타이밍 트리거, 버스 충돌 |
| SPMI | PMIC 제어 (2선) | rail 시퀀스, 전압 변경 타이밍 |

### 8.2 coexistence 신호

같은 기기 안의 무선들은 서로 방해한다: Wi-Fi 2.4 GHz와 BT는 같은 대역, 일부 LTE/5G 대역은 2.4 GHz ISM과 가깝고, 한 안테나를 나눠 쓰기도 한다. 그래서 칩 사이에 **"나 지금 송신한다 / 너 잠깐 멈춰"** 를 주고받는 coex 인터페이스가 있다 — 전용 GPIO 몇 개, 또는 Bluetooth 규격에 정의된 WCI-2 같은 UART 기반 인터페이스가 예다. 칩 벤더마다 구현이 다르므로 이름보다 개념을 기억하자: **시분할 중재(arbitration)**, 우선순위(예: 음성 통화 > BT 오디오 > Wi-Fi 백그라운드), 그리고 그 결과로 **throughput이 줄고 지연이 늘어난다**.

ML과의 연결: 음성 비서가 BT 이어폰으로 TTS를 내보내면서 Wi-Fi로 cloud LLM 결과를 받는 순간, coex 중재 때문에 cloud 응답이 늦어질 수 있다. L3의 hybrid 라우팅 지연 예산에 이 변수가 들어간다.

### 8.3 왜 radio가 AI 전력 · 열 예산을 먹나

손계산 (모든 숫자는 설명용 가정):

```
기기가 오래 버틸 수 있는 지속 전력(피부 온도 한계) ≈ 1.5 W
기본 시스템(디스플레이 없음, 센서·AP 일부)          ≈ 0.3 W
cellular로 오디오·결과를 올리고 받는 중 (TX 높을 때) ≈ 0.8 W
────────────────────────────────────────────────
AI(NPU)에 남는 지속 전력                             ≈ 0.4 W

온디바이스 SLM decode가 1.0 W에서 10 tok/s라면 (대역폭 한계라 대략 비례한다고 가정)
0.4 W 예산에서는 ≈ 4 tok/s, 혹은 throttling 곡선을 따라 떨어진다 (E9, K4)
```

말로 하면: radio와 NPU는 **같은 열 예산과 같은 배터리를 나눠 쓴다**. 그래서 "cloud로 보낼까, 기기에서 할까"(L3)는 에너지만의 문제가 아니라 **동시에 켜지면 둘 다 느려지는** 문제다. 추가로:

- **순간 전류**: PA 송신 버스트와 NPU 버스트가 겹치면 배터리 전압이 순간적으로 떨어진다(내부 저항). PMIC가 brownout을 막으려고 한쪽을 제한할 수 있다 — Don이 margin sign-off에서 보던 바로 그 전류 피크 문제다.
- **DRAM·NoC 공유**: modem과 NPU가 같은 DRAM 대역폭을 쓰는 SoC라면, LLM decode(대역폭 한계, D5)가 느려질 수 있다.
- **스케줄링으로 푸는 법**: 업로드와 온디바이스 추론을 겹치지 않게 순서화, 큰 업로드는 Wi-Fi·충전 중으로 미루기, 첫 응답은 기기에서 짧게 하고 긴 답은 cloud로 (L3, L6).

Don의 차별점: 대부분의 ML 엔지니어는 radio를 "네트워크가 된다/안 된다"로만 본다. Don은 **링크가 어떤 버스로 붙어 있고, 저전력 상태에서 깨는 데 얼마나 걸리고, coex·PA 전류가 어떻게 시스템을 흔드는지**를 안다. 인터뷰에서 hybrid edge-cloud 질문이 나오면 이 층까지 내려가서 답할 수 있다.

---

## 9. 임베디드 관점에서 다시 보기

| 이 노트의 개념 | 펌웨어에서 이미 아는 모양 | 이기종 ML 기기에서 새로 붙는 것 |
|---|---|---|
| 엔진별 duty cycle | 코어별 역할 분담(host IF, FTL, 전원 관리) | 엔진 사이 1000배 전력 차, wake 비용이 결정 변수 |
| always-on island | 전원 관리 M0+, AON 도메인 | 센서 허브, VAD, pre-roll 오디오 버퍼 |
| companion MCU 링크 | 칩 간 SPI/UART 프로토콜, 사이드밴드 GPIO | 오디오급 대역폭, 두 펌웨어의 독립 OTA |
| mailbox + 공유 링 | doorbell + SRAM 큐, DMA 디스크립터 링 | RPMsg·FastRPC 같은 표준·벤더 계층 |
| 캐시 · barrier | DMA 전 flush/invalidate, DMB/DSB | 텐서·오디오 버퍼의 소유권 규칙 |
| wake cascade | 인터럽트 coalescing, 저전력 상태 진입 정책 | false wake = 모델의 FA/hour가 전력 항목 |
| SSR · 이미지 로드 | 보조 코어 FW 적재, watchdog 복구 | 모델·컨텍스트 바이너리 재적재, 클라이언트 알림 |
| 시간 동기화 · 로그 | telemetry, 이벤트 로그 | 센서·오디오·모델 이벤트를 한 타임라인에 (G7) |
| radio 이웃 | 현재 업무 그대로 | AI 작업과 열·배터리·DRAM 경쟁 |

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 작은 always-on 작업을 AP에서 돌림 | 대기 전력이 수십 mW, 배터리 하루 못 감 | 깨우기 비용과 바닥 전력이 1회 에너지를 압도 (예제 1) | MCU·island·센서 내장 기능으로 내린다 |
| 센서 샘플마다 AP에 전달 | AP가 suspend에 못 들어감 | batching 없음 (예제 2) | 허브 batch + 긴급 이벤트만 즉시 경로 |
| pre-roll 버퍼 없음 | wake word 직후 첫 단어가 잘린 ASR | AP resume 동안 소리 유실 | 링 길이 ≥ KWS 지연 + resume p99 + 여유 |
| head/tail이 같은 캐시 라인 | IPC 처리량이 이유 없이 절반 | false sharing (예제 5) | 캐시 라인 크기로 정렬·패딩 |
| non-coherent 공유 메모리에 cache 유지보수 누락 | 가끔 옛 데이터, 재현 어려움 | clean/invalidate·barrier 누락 | 소유권 전환 지점마다 clean/invalidate, doorbell 전 barrier |
| 작은 RPC를 자주 호출 | DSP 오프로드가 CPU보다 느림 | 호출당 µs~수백 µs 고정비 | 큰 단위 API, 그래프 단위 실행, batching |
| AP wakelock을 늦게 놓음 | 모델은 그대로인데 전력 증가 | tail이 길어짐 (시나리오 E) | 전류 파형 + wakelock 통계로 추적, tail 정책 명시 |
| KWS FA 목표를 정확도만으로 정함 | 필드에서 배터리 불만 | FA/hour가 전력 항목임을 무시 (시나리오 C) | FA/hour × false wake 단가로 예산화 |
| 서브시스템 재시작 경로 미검증 | DSP 크래시 뒤 기기가 조용히 안 들림 | 링·seq·버퍼 소유권 리셋 누락, heartbeat 없음 | 크래시 주입 테스트, heartbeat 감시 |
| 코어별 로그를 따로 봄 | "둘 다 정상인데 결과가 틀림" | 시간축 불일치, drift | 전역 카운터 또는 offset+drift fit (예제 9) |
| MCU·AP 펌웨어 버전 조합 미관리 | OTA 뒤 일부 기기만 wake 불가 | 프로토콜 버전 불일치 | 부팅 시 버전 협상, 호환성 매트릭스, 업데이트 순서 |

---

## 11. 면접에서 이렇게 말한다

**Q.** "Design the always-listening path across MCU, DSP, and AP."

**A.** 단계별로 전력이 10배씩 오르는 사다리로 설계하고, 각 단계의 false positive rate가 다음 단계의 깨우는 빈도라는 점을 숫자로 말한다. mic PDM → HW decimation → pre-roll 링 → VAD(항상, sub-mW) → KWS(DSP, VAD가 열 때만) → AP(키워드일 때만, pre-roll부터 ASR). 예산은 "바닥 + 단가 × 횟수"로 계산하고, 내 시뮬레이션에서 DSP 단계를 빼면 평균 전력이 4배가 됐다는 식으로 근거를 댄다.

> "I'd build it as a power ladder: the PDM decimation and a VAD run continuously on the always-on core at sub-milliwatt, the VAD gates a keyword spotter on the DSP, and only a confident keyword wakes the application processor, which reads a one-to-two-second pre-roll buffer so the first word isn't lost. Each stage's false-accept rate sets how often the next, ten-times-more-expensive stage wakes, so I budget it as floor power plus cost-per-wake times wakes-per-hour. In a simple simulation I built, removing the DSP stage and letting the VAD wake the AP directly made the average power about four times higher, almost all of it false wakes."

**Q.** "How do cores communicate inside a SoC?"

**A.** 데이터는 공유 메모리, 신호는 mailbox doorbell 인터럽트. SPSC 링 두 개(요청·응답)로 락 없이, release/acquire(또는 DMB) 순서 규칙과 non-coherent 경로의 cache clean/invalidate가 핵심. 그 위에 RPMsg(OpenAMP) 같은 메시지 계층이나 Qualcomm FastRPC 같은 RPC 계층이 있다. 호출당 고정비가 µs급 이상이므로 큰 단위로 넘긴다.

> "Data goes through shared memory, notification goes through a mailbox doorbell interrupt. The usual structure is a pair of single-producer single-consumer rings, one per direction, with release/acquire ordering on the indices and explicit cache clean and invalidate if the path isn't coherent, plus a barrier before ringing the doorbell. On top of that you have a message layer like RPMsg on virtio rings, or an RPC layer like Qualcomm's FastRPC for Hexagon. Every crossing has a fixed cost, so I design APIs that hand over big chunks of work, like a whole graph execution, rather than many small calls."

**Q.** "Would you use a companion MCU or the SoC's low-power island?"

**A.** 기준 세 개로 답한다: AP를 완전히 끌 수 있는가(suspend 바닥 제거 vs 콜드 부팅 지연), always-on 데이터가 오디오처럼 큰가(칩 밖이면 공유 메모리 없음), 벤더 island 펌웨어에 얼마나 접근 가능한가. island는 연결이 싸고 빠르며, MCU는 바닥 전력과 소유권을 얻는 대신 링크 프로토콜·두 번째 OTA를 치른다. 섞는 것도 답이다.

> "It depends on three things. First, whether we can fully power down the SoC: a companion MCU removes the application processor's suspend floor, which in my model was the largest single term, but cold boot latency then has to fit the wake-word response budget. Second, how much data the always-on path moves: audio across SPI means copies and link power, while an on-chip island shares memory. Third, how much control we have over the vendor's island firmware. A hybrid is common: audio front-end on the SoC island, sensors and power management on an external MCU that we fully own."

**Q.** "How would you debug a race between the DSP and the AP?"

**A.** 재현율을 올리고(스트레스·지연 주입), 메시지마다 seq·timestamp·CRC를 심어 "늦게 보인 데이터"인지 "로직 오류"인지 가르고, 공유 로그 링을 전역 시간축으로 합쳐 순서를 복원한다. 후보는 cache clean/invalidate 누락, doorbell 전 barrier 누락, 버퍼 소유권 위반, 재시작 후 링 초기화. GPIO 마커와 로직 애널라이저로 확인한다.

> "First I make it reproducible, with a stress loop and injected delays to widen the window. Then I put evidence in the protocol: a sequence number, a timestamp and a CRC in every buffer header, so I can tell stale data, which points to cache maintenance or ordering, from a wrong index, which points to ownership logic. I merge both cores' binary trace rings onto one time base, either a global counter or an offset-and-drift fit, and add GPIO markers on the doorbell and the interrupt so I can see them on a logic analyzer. The usual suspects are a missing cache clean before publishing the index, a missing barrier before the doorbell write, or a buffer handed back before it was acknowledged."

**Q.** "Where would you run gesture recognition and why?"

**A.** cascade로: IMU 내장 wake-on-motion(µW) → MCU에서 hand-crafted 특징 + 작은 tree/CNN(연속, 수 KB, 수 µW~수백 µW) → 애매할 때만 DSP. AP·NPU는 항상 도는 50~100 Hz 작업에 바닥 전력이 너무 크고, 피드백 지연(<100 ms)도 wake 비용 때문에 불리하다. 모델 크기·연산량이 MCU 예산을 넘으면 그때 DSP로 올린다.

> "On the always-on side, as a cascade. The IMU's built-in wake-on-motion gates everything; the MCU runs a small feature extractor and a tree or tiny CNN continuously at fifty to a hundred hertz, which is a few kilobytes of model and well under a milliwatt; only ambiguous candidates escalate to the DSP. The application processor or NPU would be faster per inference, but for a continuous task their floor power and wake cost dominate, and waking them would also blow the sub-hundred-millisecond feedback budget."

**Q.** "What happens when the DSP running your wake word crashes?"

**A.** 감지(watchdog, heartbeat) → 클라이언트 알림 → 진행 중 요청 에러 처리·버퍼 회수·링 리셋 → 덤프 → 이미지 재로드·버전 협상·모델 재적재 → 복귀 알림. always-on 경로는 heartbeat로 감시해야 "조용히 귀머거리"가 되지 않는다.

> "The platform should restart just that subsystem: detect it through a watchdog or a missed heartbeat, notify every client, fail in-flight requests and reclaim shared buffers, reset the rings and sequence numbers, save a dump, then reload the image, renegotiate the protocol version and reload the model. For an always-listening product the dangerous case is a silent failure, so I'd have the AP or MCU monitor a heartbeat such as frames processed per second."

**Q.** "How does radio activity affect your on-device AI budget?"

**A.** 같은 열·배터리·DRAM을 나눠 쓴다. cellular TX가 수백 mW를 쓰는 동안 NPU에 남는 지속 전력이 줄고, 순간 전류 피크가 겹치면 PMIC가 제한할 수 있다. 그래서 업로드와 온디바이스 추론을 순서화하고, coex 중재로 cloud 지연이 늘어나는 것도 hybrid 라우팅 예산에 넣는다. 현재 무선 칩셋 통합 업무에서 이 상호작용을 직접 본다.

> "Radios share the same thermal envelope, battery and often DRAM bandwidth with the NPU. While the modem is transmitting at high power, the sustained budget left for on-device inference shrinks, and overlapping current peaks can force the PMIC or thermal manager to throttle something. So I schedule uploads and heavy inference so they don't overlap, and I include coexistence arbitration delays in the hybrid edge-cloud latency budget. That's exactly the kind of interaction I debug today when integrating wireless chipsets into SoC platforms."

---

## 12. 직접 해보기

1. 예제 1에서 DSP의 깨우기 에너지 `e_wake`를 0.3 mJ에서 0.03 mJ로 줄이면 DSP가 MCU를 이기기 시작하는 빈도는 어떻게 되나? 먼저 손으로 예측하고 코드로 확인하라. 정답: 1 Hz에서 DSP 재움 정책은 0.2 + 0.02 + 0.03 ≈ 0.25 mW로 MCU(0.16)보다 여전히 비싸지만, 역전 빈도는 약 35 Hz → 약 11 Hz로 내려간다 (코드로 확인한 값). 이제는 DSP의 재움 바닥 0.2 mW가 새 병목이다.
2. pre-roll 링 크기 계산: 16 kHz, 16 bit mono, KWS 판정 지연 300 ms, AP resume p99 400 ms, 키워드 길이 0.8 s, 여유 30%. 필요한 바이트는? 정답: (0.3 + 0.4 + 0.8) × 1.3 ≈ 1.95 s → 1.95 × 32000 ≈ 62.4 KB (64 KB 링).
3. 예제 4(`ipc`)를 바꿔 "doorbell을 매 메시지 대신 링에 4개가 쌓일 때만 울리는" coalescing 모드를 만들고, 스트리밍 처리량과 첫 메시지 지연을 비교하라. 힌트: 마지막 메시지가 4개를 못 채우면 영원히 안 깨는 버그를 막으려면 timeout(또는 "flush" 메시지)이 필요하다.
4. `wake_sim.py`에 "DSP 2차 검증" 단계를 넣어라: KWS hit 뒤 DSP가 200 ms 더 큰 모델로 재확인(20 mW), false accept를 1/5로 줄이고 true는 2%만 잃는다. 시나리오 C에서 평균 전력이 얼마나 줄어드나? 힌트: false wake 1회 AP 단가 ≈ 855 mJ vs DSP 재확인 단가 4 mJ.
5. 시간 동기화: 두 시계가 20 ppm 다르고 로그 정렬 허용 오차가 100 µs다. offset을 한 번만 재면 몇 초마다 다시 재야 하나? 정답: 100 µs / 20 ppm = 5 s.
6. 가상의 기기에서 cellular 업로드 중 NPU에 남는 전력이 0.4 W일 때, 10초짜리 답변(토큰 100개)을 기기에서 만들 수 있는가? 8.3절의 비례 가정을 쓰라. 정답: 약 4 tok/s → 25초 필요, 예산 초과 → 업로드가 끝난 뒤 생성하거나 cloud로 보내는 순서화가 필요.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| heterogeneous SoC | 이기종 SoC | 성격이 다른 여러 엔진(CPU·DSP·NPU·GPU·MCU)을 한 칩에 모은 것 |
| AP | application processor | Android·앱이 도는 Cortex-A 클러스터 |
| power domain | 전원 도메인 | 함께 켜고 끄는 회로 묶음 |
| retention | 유지 상태 | 전압을 낮추되 SRAM·레지스터 내용은 보존하는 저전력 상태 |
| always-on island | 상시 동작 섬 | SoC 나머지가 꺼져도 살아 있는 작은 전원 도메인 |
| sensor hub | 센서 허브 | 센서 버스를 소유하고 수집·batching·작은 처리를 대신하는 저전력 코어 |
| companion MCU | 동반 MCU | SoC 밖에 따로 둔 저전력 마이크로컨트롤러 |
| PDM | pulse density modulation | 디지털 MEMS 마이크의 1비트 고속 출력 형식 |
| decimation | 데시메이션 | 필터링하며 샘플레이트를 낮추는 것 (PDM → PCM) |
| pre-roll | 선행 버퍼 | wake 이전의 소리를 들고 있는 링버퍼 |
| I3C | MIPI I3C | I2C 후속 2선 버스, in-band interrupt 지원 |
| SoundWire | MIPI SoundWire | 오디오 장치용 2선 멀티드롭 버스 |
| mailbox · doorbell | 메일박스 · 초인종 | 한 코어가 레지스터를 쓰면 다른 코어에 IRQ를 올리는 블록 |
| SPSC ring | 단일 생산자·소비자 링 | head는 producer만, tail은 consumer만 쓰는 락 없는 큐 |
| false sharing | 거짓 공유 | 서로 다른 변수가 한 캐시 라인에 있어 코어끼리 라인을 빼앗는 현상 |
| coherency | 캐시 일관성 | 모든 관찰자가 같은 메모리 값을 보도록 하드웨어가 보장하는 성질 |
| remoteproc | 원격 프로세서 프레임워크 | Linux가 원격 코어 펌웨어를 로드·시작·복구하는 틀 |
| RPMsg · OpenAMP | 원격 메시징 | virtio vring 위의 코어 간 메시지 버스와 그 오픈 구현 |
| FastRPC | Qualcomm RPC | CPU에서 Hexagon DSP 함수를 원격 호출하는 메커니즘 |
| SSR | subsystem restart | 서브시스템 하나만 재시작하는 복구 (Qualcomm 용어로 알려짐) |
| wakelock | 깨어 있기 잠금 | Android에서 시스템이 suspend 하지 못하게 잡는 표시 |
| false wake | 헛깨움 | 필요 없는데 비싼 단계를 깨운 것 |
| DES | discrete-event simulation | 사건 시각만 큐로 처리하는 시뮬레이션 |
| offset · drift | 시계 차이 · 시계 속도 차이 | 두 시계의 현재 차이와 그 차이가 벌어지는 속도(ppm) |
| RFFE | MIPI RF front-end control | PA·LNA·스위치를 제어하는 2선 버스 |
| SPMI | system power management interface | PMIC 제어용 2선 버스 |
| coexistence (coex) | 공존 | 같은 기기 안 무선들이 서로 방해하지 않도록 시분할·우선순위로 중재하는 것 |

---

## 14. 요약 & 체크리스트

이기종 SoC는 "빠른 엔진 하나"가 아니라 **duty cycle 구간마다 다른 엔진**을 두는 설계다. 항상 도는 작은 일은 µW~sub-mW의 island·MCU·센서 내장 로직에, 가끔 도는 중간 일은 DSP에, 세션 동안의 큰 모델은 NPU에, 기기에 안 들어가는 것은 cloud에 둔다. 엔진 사이는 공유 메모리 링 + mailbox doorbell로 대화하며, 성능·정확성 버그의 대부분은 캐시 유지보수·barrier·소유권 규칙에서 나온다. 칩 밖 companion MCU는 always-on 바닥을 극한으로 낮추고 소유권을 주는 대신 프레이밍·흐름 제어·wake 핸드셰이크·두 번째 OTA를 요구한다. wake cascade의 평균 전력은 "바닥 + 단가 × 횟수"이고, 시뮬레이션에서 보듯 **false wake, 전이 비용(resume·tail), suspend 바닥**이 모델 연산보다 예산을 더 많이 먹을 수 있다. 부팅·SSR·OTA·크로스 코어 디버깅·radio 공존까지 "시스템 전체"를 숫자로 설명하는 것이 이 역할의 차별점이다.

- [ ] 가상의 웨어러블 SoC 블록도를 그리고 블록마다 전력 규모와 역할을 말할 수 있다
- [ ] 같은 작업의 엔진별 평균 전력을 "바닥 + 1회 에너지 × 빈도 + 깨우기 비용"으로 손계산할 수 있다
- [ ] 센서 허브 batching이 AP wake 횟수와 지연을 어떻게 바꾸는지 계산할 수 있다
- [ ] PDM → decimation → pre-roll → VAD → KWS → AP 경로와 pre-roll 크기를 설명할 수 있다
- [ ] companion MCU와 통합 island의 트레이드오프를 세 가지 질문으로 판단할 수 있다
- [ ] 칩 간 링크 프로토콜(프레이밍, CRC, seq/ACK, 흐름 제어, wake 핸드셰이크)을 설계할 수 있다
- [ ] SPSC 링 + doorbell을 C로 짜고 release/acquire, false sharing, cache clean/invalidate 지점을 짚을 수 있다
- [ ] RPMsg·FastRPC류 계층의 역할과 호출당 고정비를 설명할 수 있다
- [ ] wake cascade DES를 만들고 false wake·tail·suspend 바닥의 몫을 읽을 수 있다
- [ ] offset + drift fit으로 코어 간 로그 시간을 맞추고, DSP↔AP race 디버깅 순서를 말할 수 있다

## 참고 자료

- Linux kernel 문서 — Remote Processor Framework: https://docs.kernel.org/staging/remoteproc.html
- Linux kernel 문서 — Remote Processor Messaging (rpmsg): https://docs.kernel.org/staging/rpmsg.html
- Linux kernel 문서 — The Common Mailbox Framework: https://docs.kernel.org/driver-api/mailbox.html
- OpenAMP 프로젝트 (RPMsg·remoteproc의 RTOS/bare-metal 구현): https://www.openampproject.org/
- Android Context Hub Runtime Environment (CHRE): https://source.android.com/docs/core/interaction/contexthub
- Qualcomm Hexagon SDK 문서 (FastRPC, DSP 오프로드) — Qualcomm developer 사이트에서 "Hexagon SDK"로 검색
- MIPI Alliance 규격 개요: I3C, SoundWire, RFFE, SPMI — https://www.mipi.org/specifications
- S. Cheshire, M. Baker, "Consistent Overhead Byte Stuffing", IEEE/ACM Transactions on Networking, 1999
- D. L. Mills, Network Time Protocol (RFC 5905) — 4-타임스탬프 offset/delay 식의 원전: https://www.rfc-editor.org/rfc/rfc5905
- Arm CoreSight 아키텍처 문서 (STM, ETM) — developer.arm.com
- Bluetooth Core Specification — MWS coexistence 및 WCI-2 전송 부분 (bluetooth.com)
- 이 노트 세트: B5(오디오 모델·cascade 예산), B7(IMU 사다리), B9(cascade 기대 비용), D6(꼬리 지연), D7(에너지·배터리), E4·E5·E7·E9(병행 노트)
