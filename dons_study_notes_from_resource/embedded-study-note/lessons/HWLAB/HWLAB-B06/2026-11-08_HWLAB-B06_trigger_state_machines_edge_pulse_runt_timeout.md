---
id: HWLAB-B06
time: 70분
updated: 2026-11-08
---
# HWLAB-B06 트리거 로직 구현: Edge·Pulse-width·Runt·Timeout

## 학습 목표

- 스코프 트리거를 **비교기(comparator) + 작은 상태기계(state machine)** 로 설명하고, 히스테리시스 edge · pulse-width(`<`, `>`, 범위) · runt · timeout 트리거를 샘플 스트림 위에서 직접 구현할 수 있다.
- 각 트리거의 **트리거 지점(어느 샘플에서 발사되나)** 과 그것이 왜 중요한지(pulse `>` 는 뒷에지, timeout 은 만료 순간 — 멈춰 버린 선은 timeout 만 잡는다)를 말할 수 있다.
- 히스테리시스가 잡음 p-p 보다 작으면 edge 는 여러 번, runt 는 가짜로 발사된다는 것을 수치로 보이고, 청크 경계에서 상태를 이어받는 스트리밍 구현이 한 번에 처리한 결과와 **완전히 같다** 는 것을 테스트로 증명할 수 있다.
- 10M 샘플(10 ms @ 1 GSa/s) 캡처에서 드문 runt/글리치/클록 정지를 찾아 pre/post-trigger 윈도우로 잘라 저장하고, 하드웨어 엔지니어가 바로 움직일 수 있는 리포트로 정리할 수 있다.

## 1. 문제

"가끔 한 번" 깨지는 보드에서 스코프를 auto 로 켜 놓고 화면을 쳐다보는 것은 거의 쓸모가 없다 — 드문 이벤트를 **트리거 조건으로 정의해서 기계가 기다리게** 하는 것이 브링업의 기본기이고, Rivian/NVIDIA/Apple 검증 면접의 단골 질문 "간헐 글리치를 어떻게 잡았나?"의 답이다.

스코프 화면은 대부분의 시간 동안 **아무것도 보고 있지 않다**. 한 번 수집(acquisition)한 뒤 처리·표시하는 동안의 dead time 이 길어서, 1000 wfm/s 에 10 µs 창이면 실제로 보는 시간은 1% 뿐이다 (연습 3). 그래서 "초당 한 번 생기는 4 ns 글리치"는 auto 화면에서 5분을 지켜봐도 95% 확률로 보이는 정도다. 트리거는 이 문제를 뒤집는다: ADC 샘플(또는 아날로그 비교기 출력)을 **빠짐없이** 감시하는 하드웨어 상태기계가 조건이 맞는 순간만 메모리를 멈추고, 그 앞(pre-trigger)과 뒤(post-trigger)를 보여 준다.

이 레슨의 캡처: 브링업 보드 **RB-07** 의 `IMU_SPI_SCLK` (MCU → IMU, 10 MHz, 3.3 V LVCMOS) 를 TP12 에서 1 GSa/s · 8비트로 10 ms(= 10,000,000 샘플) 찍은 것이다. 증상은 "IMU `WHO_AM_I` 읽기가 몇 초에 한 번 틀린 값을 준다". 캡처에는 정상 클록 99,980 주기 사이에 이상 파형 4개가 숨어 있다.

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">IMU_SPI_SCLK 에 심어 둔 4개의 이상 파형과 트리거 임계</text>
  <rect x="10" y="152" width="740" height="10" class="f-mut" opacity=".5"/>
  <g stroke="currentColor" stroke-dasharray="5 4" fill="none" opacity=".8">
    <path d="M10 140 L750 140"/><path d="M10 200 L750 200"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="12" y="135">H = VIH 2.0 V</text>
    <text x="12" y="214">L = VIL 0.8 V</text>
    <text x="640" y="150">edge 밴드 1.55–1.75 V</text>
    <text x="12" y="72">3.3 V</text><text x="12" y="252">0 V</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.6">
    <path d="M40 240 L60 240 L64 75 L100 75 L104 240 L118 240 C126 240 128 175 132 175 C136 175 138 240 146 240 L170 240 L174 75 L185 75"/>
    <path d="M195 75 L205 75 L209 240 L228 240 L230 75 L234 75 L236 240 L262 240 L266 75 L300 75 L304 240 L340 240 L344 75 L370 75"/>
    <path d="M380 75 L396 75 L400 240 L540 240 L544 75 L555 75"/>
    <path d="M565 240 L585 240 L589 75 L625 75 C633 75 635 175 640 175 C645 175 647 75 655 75 L690 75 L694 240 L730 240 L734 75 L750 75"/>
  </g>
  <g class="f-warn" stroke="currentColor">
    <circle cx="143" cy="230" r="5"/><circle cx="236" cy="240" r="5"/>
    <circle cx="470" cy="240" r="5"/><circle cx="650" cy="140" r="5"/>
  </g>
  <g stroke="currentColor" fill="none"><path d="M400 262 L470 262"/><path d="M400 257 L400 267"/><path d="M470 257 L470 267"/></g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="105" y="285">① runt+ (피크 1.3 V)</text>
    <text x="282" y="285">② 글리치 폭 4 ns</text>
    <text x="435" y="258">≥ 200 ns</text>
    <text x="467" y="285">③ 클록 정지 2 µs (timeout)</text>
    <text x="657" y="285">④ runt− (바닥 1.3 V)</text>
  </g>
  <text x="200" y="45" fill="currentColor" font-size="12">● = 트리거 지점: runt 는 '돌아온' 순간, 글리치는 뒷에지, timeout 은 만료 순간</text>
</svg>
```

| # | 이상 파형 | 무엇이 다른가 | 잡는 트리거 | 못 잡는 트리거 |
|---|---|---|---|---|
| ① | runt+ | L 은 넘고 H 는 못 넘고 돌아옴 | runt | edge(1.65 V 레벨 못 넘음), pulse-width |
| ② | 글리치 | 풀스윙인데 폭 4 ns | pulse-width `< 10 ns` | runt (H 를 넘었으니 "정상 높이") |
| ③ | 클록 정지 | 2 µs 동안 에지 없음 | timeout `low ≥ 200 ns` | edge·runt (아무 일도 안 일어남) |
| ④ | runt− | high 에서 1.3 V 까지 꺼졌다 복귀 | runt | pulse-width `< 10 ns` (양 펄스가 ~25 ns 둘로 쪼개질 뿐) |

ADC 모델 (8비트 스코프 흉내, `gen_capture.py`):

| 항목 | 값 |
|---|---|
| 샘플레이트 | 1 GSa/s → 1 샘플 = 1 ns, 10 MHz 클록 = 100 샘플/주기 |
| 코드 | `code = round((v + 0.5 V) / 20 mV)`, 0..255 포화 → 0 V = 25, 3.3 V = 190 |
| 에지 | `tanh` 형, 10–90% ≈ 2.0 ns, 에지 지터 ±0.3 ns |
| 잡음 | 가우시안 30 mV rms (1.5 코드) |
| 결정성 | `random.Random(20261108)` 의 `random()` 만 사용 → sha256 고정 |

### Part A — 트리거 상태기계 4종

`Schmitt(lo, hi).step(x)` — `x >= hi` 면 1, `x < lo` 면 0, 밴드 안이면 유지, 초기 상태는 "모름". 그 위에 `EdgeTrigger`, `PulseWidthTrigger`, `RuntTrigger`, `TimeoutTrigger` 의 `process(chunk, base)` 를 채운다. 모두 상태를 `self` 에 저장해서 여러 청크에 나눠 불러도 같은 답을 내야 한다.

### Part B — 측정이 거짓말하는 지점

① 히스테리시스 0 / 0.1 / 0.2 V 로 느린(400 ns) 잡음 섞인 에지에서 edge 개수와 가짜 runt 개수를 비교한다. ② 청크 크기 1, 7, 100, 333 으로 잘라도 한 번에 처리한 결과와 같아야 한다. ③ 클록 정지를 pulse-width `>` 와 timeout 으로 잡을 때 트리거 시각이 어떻게 다른지, 선이 영영 멈추면 어떻게 되는지. ④ `extract_windows` — 프리트리거 미충전, holdoff, 레코드 끝.

### Part C — 10M 샘플 스캔과 브링업 리포트

`scan_capture(buf)` 로 4개 트리거를 1M 샘플 청크 단위로 한 번에 돌리고, 정확히 4개 이벤트(runt+, glitch, timeout, runt−)만 나오는지, 상승 에지 수가 99,982 인지 확인한다. 각 이벤트를 `fmt_event` 로 한 줄 리포트로 만들고, 200 ns 앞 / 600 ns 뒤 윈도우를 `captures/evt*.csv` 로 저장한다.

## 2. Problem (English)

You are given an 8-bit oscilloscope capture of a 10 MHz 3.3 V clock: 10,000,000 samples at 1 GSa/s (`gen_capture.build_capture()` builds it in memory). Implement streaming trigger state machines and use them to find rare anomalies.

```python
class Schmitt:            # step(x) -> +1 / -1 / 0 ; x >= hi -> 1, x < lo -> 0, else hold
class EdgeTrigger:        # (lo, hi, slope='rise'|'fall'|'either').process(chunk, base) -> [index]
class PulseWidthTrigger:  # (lo, hi, polarity='pos'|'neg', less=None, greater=None)
                          #   .process(...) -> [(trailing_edge_index, width)]
class RuntTrigger:        # (low, high, hys, polarity='pos'|'neg'|'either')
                          #   .process(...) -> [(index, '+'|'-', extreme_code)]
class TimeoutTrigger:     # (lo, hi, state='low'|'high'|'either', timeout)
                          #   .process(...) -> [(expiry_index, 'low'|'high')]
def extract_windows(buf, trigs, pre, post, holdoff=None) -> (accepted, dropped)
def scan_capture(buf, chunk=1<<20) -> (events, n_rising_edges)
def fmt_event(n, event) -> str
```

Constraints: Python 3.9 stdlib only; every `process()` must be resumable — feeding a stream in chunks of any size must produce exactly the same triggers as feeding it at once. Full 10M-sample scan must run in a few seconds.

Example: `RuntTrigger(30, 70).process([10]*5 + [50]*3 + [10]*5 + [90]*5 + [50]*3 + [90]*5 + ...)` → `[(8, '+', 50), (21, '-', 50)]`. `TimeoutTrigger(40, 60, 'low', 40).process([100]*10 + [0]*100 + ...)` → `[(50, 'low')]` (fires 40 samples after the falling edge, not when the line comes back).

## 3. 시작 코드

<!-- code: starter.py -->

캡처 생성기 (파일로 쓰지 않는다 — 10 MB 를 커밋하지 않고 0.03 s 만에 메모리에서 만든다):

<!-- code: gen_capture.py -->

```bash
cd lessons/HWLAB/HWLAB-B06
python3 -I gen_capture.py      # 요약 + sha256
python3 -I starter.py          # TODO 상태: 7/44 passed, exit 1
```

`gen_capture.py` 실제 출력 (build time 줄 제외 — 실행마다 조금씩 다르다):

```text
samples   : 10000000 (10.0 ms @ 1 GSa/s)
min/max   : 19 / 196 codes (-0.12 / 3.42 V)
planted   : runt_pos period=13579  index=1357912
planted   : glitch   period=41234  index=4123412
planted   : stall    period=58000  index=5800000
planted   : runt_neg period=77777  index=7777750
sha256    : dbccc732edc97cb49f68f90dc18dc40bf4eb90876a52ad8f7f16e9b4822b5432
```

`starter.py` 실제 출력 (일부):

```text
-- Part A: trigger state machines --
FAIL  Schmitt: [0,50,61,55,45,39,50,61] -> 0,0,+1,0,0,-1,0,+1
...
-- Part B: hysteresis, chunking, timeout vs width, windows --
      slow noisy edges x5: rising count hys 0 / 0.1 / 0.2 V = 0 / 0 / 0
FAIL  no hysteresis: single-threshold edge multi-triggers on slow noisy edge (> 5)
...
PASS  runt trigger hys 0.2 V -> 0 false runts
      mini capture: 0 edges, glitch=[], runts=[], timeout=[]
FAIL  mini capture: 1 glitch, 2 runts, 1 timeout
PASS  chunk=1 gives identical results to one-shot (state carried)
...
-- Part C: 10M-sample capture scan --
PASS  capture is deterministic (sha256)
...
PASS  chunk 999983 (prime) on 10M -> same events
...
7/44 passed
```

> 눈여겨볼 것: **아무것도 안 하는 트리거(`return []`)도 "가짜 runt 0개"와 "청크로 나눠도 결과가 같다" 테스트는 통과한다** (빈 리스트 == 빈 리스트). 그래서 청크 테스트 바로 앞에 "mini capture 에서 글리치 1, runt 2, timeout 1 이 *나와야* 한다" 는 양성 테스트를 둔다. 트리거 검증은 항상 "잡아야 할 것을 잡는가" + "잡지 말아야 할 것을 안 잡는가" 한 쌍이다.

## 4. 힌트

<details>
<summary>힌트 1 — 모든 트리거의 바닥은 Schmitt 비교기</summary>

상태 `st ∈ {None, 0, 1}`. `st == 0` 이면 `x >= hi` 만 보고, `st == 1` 이면 `x < lo` 만 본다. 밴드(lo..hi) 안에서는 아무 일도 없다 — 그게 히스테리시스다. `None` 이면 밴드 밖 샘플이 처음 왔을 때 상태만 정하고 에지는 내지 않는다 (캡처 시작이 에지 한가운데일 수 있다).

</details>

<details>
<summary>힌트 2 — pulse-width 는 '앞에지 시각' 하나만 기억하면 된다</summary>

양(+) 펄스: 상승 전이에서 `t0 = j`, 하강 전이에서 `w = j - t0` 를 판정하고 그 자리(뒷에지)에서 발사. 음(−) 펄스는 반대. `t0 is None`(앞에지를 못 본 첫 펄스)은 판정하지 않는다 — 폭을 모르는 펄스로 트리거하면 캡처 시작마다 가짜 이벤트가 나온다.

</details>

<details>
<summary>힌트 3 — runt 는 비교기 두 개, 위치 세 곳</summary>

바닥(L 아래) / 가운데(L..H) / 꼭대기(H 위). 바닥에서 가운데로 들어오면 "+후보", 꼭대기에서 가운데로 내려오면 "−후보". 가운데에서 H 를 넘으면: −후보였다면 runt−. L−hys 아래로 떨어지면: +후보였다면 runt+. 한 샘플에 바닥 → 꼭대기로 점프하는 경우(빠른 에지)를 따로 처리하지 않으면 정상 에지가 후보로 남는다.

</details>

<details>
<summary>힌트 4 — timeout 은 '마감 시각' 하나</summary>

전이가 일어날 때마다 감시하는 상태면 `dl = j + T`, 아니면 `dl = -1`. 매 샘플 `j == dl` 이면 발사하고 `dl = -1` (한 구간에 한 번). 샘플마다 경과 시간을 빼는 대신 정수 비교 하나로 끝난다 — 10M 샘플에서 이 차이가 보인다.

</details>

<details>
<summary>힌트 5 — 10M 샘플을 빠르게</summary>

`for x in chunk:` 로 `bytes` 를 돌면 원소가 바로 `int` 다. 루프 안에서 `self.` 속성·메서드 호출을 하지 말고 지역 변수로 꺼내 쓴 뒤 루프가 끝나면 되돌려 놓는다. 인덱스는 `enumerate` 대신 `j += 1`. 이 레슨의 정답은 4개 트리거 합쳐 샘플당 ~140 ns 다.

</details>

## 5. 풀이

### Part A — 비교기 + 상태기계

실제 스코프의 트리거 회로도 같은 구조다. 아날로그 트리거는 입력을 별도 비교기 경로에 넣고, 디지털 트리거는 ADC 샘플을 FPGA/ASIC 안에서 비교한다. 어느 쪽이든 "비교기 출력의 전이"를 작은 상태기계가 보고, 조건이 맞으면 수집 메모리에 "여기서 멈춰라" 를 건다.

| 트리거 | 비교기 | 상태 | 발사 지점 | 놓치기 쉬운 점 |
|---|---|---|---|---|
| Edge | Schmitt 1개 | st | Schmitt 전이 샘플 | 레벨 = 상승은 hi, 하강은 lo 에서 발사 |
| Pulse-width | Schmitt 1개 | st, t0 | 펄스 **뒷에지** | 첫 펄스(앞에지 미관측)는 판정 안 함 |
| Runt | 2개 (L, H, 각 hys) | cL, cH, ap/an, peak | L 복귀(+) / H 복귀(−) | 한 샘플 풀스윙, 비교기 히스테리시스 |
| Timeout | Schmitt 1개 | st, deadline | 마지막 전이 + T | 전이가 다시 올 때까지 재발사 금지 |

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="hwb06-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">RuntTrigger 상태기계 (비교기 2개: L 과 H, 각 히스테리시스 h)</text>
  <g stroke="currentColor">
    <rect x="20" y="125" width="140" height="50" rx="8" class="f-mut"/>
    <rect x="600" y="125" width="140" height="50" rx="8" class="f-mut"/>
    <rect x="300" y="40" width="160" height="50" rx="8" class="f-acc"/>
    <rect x="300" y="210" width="160" height="50" rx="8" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="90" y="147" font-weight="bold">바닥</text><text x="90" y="164">cL=0 (x &lt; L)</text>
    <text x="670" y="147" font-weight="bold">꼭대기</text><text x="670" y="164">cH=1 (x ≥ H)</text>
    <text x="380" y="62" font-weight="bold">가운데 · +후보</text><text x="380" y="79">peak = max(x)</text>
    <text x="380" y="232" font-weight="bold">가운데 · −후보</text><text x="380" y="249">trough = min(x)</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#hwb06-arr)">
    <path d="M120 125 L300 70"/>
    <path d="M460 60 L640 125"/>
    <path d="M300 82 L150 128"/>
    <path d="M640 175 L460 238"/>
    <path d="M460 250 L700 182"/>
    <path d="M300 240 L110 175"/>
    <path d="M160 150 L600 150" stroke-dasharray="5 4"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="150" y="88">x ≥ L</text>
    <text x="560" y="80">x ≥ H (정상 상승)</text>
    <text x="165" y="118" font-weight="bold">x &lt; L−h ⇒ RUNT+</text>
    <text x="545" y="215">x &lt; H−h</text>
    <text x="560" y="250" font-weight="bold">x ≥ H ⇒ RUNT−</text>
    <text x="110" y="222">x &lt; L−h (정상 하강)</text>
    <text x="300" y="143">한 샘플에 L·H 를 다 넘는 빠른 에지는 직행</text>
  </g>
</svg>
```

**Runt 의 정의를 "비교기 두 개의 전이 순서"로 쓰면 깔끔하다.** L 비교기가 켜진 뒤 H 비교기가 켜지기 전에 L 비교기가 다시 꺼지면 runt+. 반대로 H 비교기가 꺼진 뒤 L 비교기가 꺼지기 전에 H 비교기가 다시 켜지면 runt−. 각 비교기는 `x >= thr` 에서 켜지고 `x < thr − h` 에서 꺼진다. 그래서 코드에는 `Llo = L − h`, `Hlo = H − h` 가 나온다. 생성자의 `assert H − h > L` 는 "H 비교기가 켜져 있으면 L 비교기도 반드시 켜져 있다"는 불변식을 보장한다. 이게 깨지면 가운데 밴드가 사라져 상태가 모순된다.

**임계값은 수신기 스펙에서 가져온다.** runt 의 L/H 는 "10%/90%" 가 아니라 **받는 칩의 VIL/VIH** 다 (HWLAB-B02 참고). 3.3 V LVCMOS/LVTTL(JEDEC JESD8C)은 VIL = 0.8 V, VIH = 2.0 V. 그 사이에서 돌아오는 펄스는 수신기가 0 으로도 1 로도 볼 수 있는 "불확정" 펄스라서, IMU 의 SPI 시프트 레지스터가 클록을 하나 더 세거나(double clocking) 덜 셀 수 있다. 바이트 경계가 밀리면 `WHO_AM_I` 가 틀린 값으로 읽힌다. 증상과 정확히 맞는다.

**Pulse-width 폭은 Schmitt 교차 사이 샘플 수다.** 상승은 1.75 V, 하강은 1.55 V 에서 잡히므로 두 에지 모두 50% 지점(1.65 V)보다 같은 시간 δ 만큼 늦게 잡힌다. 에지가 대칭이면 폭에는 편향이 없다. 남는 오차는 ±1 샘플 양자화다. 정밀한 폭·상승시간 측정은 IEEE 181 상태 레벨과 50% 기준 보간으로 따로 한다 (HWLAB-B04 참고). 트리거의 역할은 "어디를 볼지" 를 찾는 것이다.

**Timeout 과 pulse `>` 는 다르다.** 둘 다 "오래 머문 상태" 를 찾지만, pulse-width 는 펄스가 **끝나야**(뒷에지) 폭을 안다. timeout 은 T 가 지나는 **순간** 발사한다. 선이 영영 돌아오지 않으면(리셋 라인 stuck, 클록이 영구 정지) pulse-width 는 아무것도 못 잡는다. 스코프 메뉴에서 timeout 이 별도 항목인 이유다 (Part B 테스트로 확인).

### Part B — 히스테리시스, 청크 경계, 트리거 시각, 윈도우

**히스테리시스는 잡음 p-p 보다 커야 한다.** 느린 에지는 레벨 근처에 오래 머물고, 그동안 잡음이 레벨을 여러 번 넘나든다. 400 ns 램프에 40 mV rms 잡음을 얹어 5번 올렸다 내린 결과 (실제 출력):

```text
      slow noisy edges x5: rising count hys 0 / 0.1 / 0.2 V = 25 / 7 / 5
      slow noisy edges x5: false runts hys 0 / 0.1 / 0.2 V = 41 / 2 / 0
```

| 히스테리시스 | edge 상승 개수 (정답 5) | 가짜 runt | 해석 |
|---|---|---|---|
| 0 | 25 | 41 | 단일 임계 비교기. 잡음이 레벨을 넘나들 때마다 발사 |
| 0.1 V | 7 | 2 | 가우시안 40 mV rms 의 p-p 는 대략 6σ ≈ 0.24 V. 0.1 V 로는 부족 |
| 0.2 V | 5 | 0 | 이 시드에서는 충분. 원칙은 "잡음 p-p 이상" |

가짜 runt 가 생기는 경로: L 을 막 넘은 직후 잡음으로 L 아래로 떨어지면 "L 넘고 H 못 넘고 돌아옴" 이 된다. 스코프의 "noise reject" 가 하는 일이 트리거 히스테리시스를 키우는 것이다. 대가로 작은 진폭의 진짜 이벤트를 놓칠 수 있다. I2C 같은 RC 상승 신호(HWLAB-B03 참고)를 edge 트리거로 볼 때 화면이 흔들리면 가장 먼저 이것을 의심한다.

**청크 경계 = 하드웨어 파이프라인 경계.** FPGA 트리거는 클록당 샘플 여러 개를 병렬로 받는다 (예: 1 GSa/s 를 125 MHz 클록당 8샘플). 펌웨어나 호스트 스트리밍 분석은 버퍼 단위로 받는다. runt 가 청크 경계에 걸쳐 있어도 결과가 같으려면 `cL, cH, ap, an, pk`, `t0`, `deadline` 처럼 **판단에 필요한 모든 것** 을 객체에 남겨야 한다. 테스트는 청크 1, 7, 100, 333 (주기 100 과 서로소인 값 포함)과 10M 전체에 대해 소수 청크 999,983 을 쓴다. 경계가 이벤트 한가운데 오도록 일부러 고른 크기들이다.

**트리거 시각 (실제 출력):**

```text
      stall: pulse-width neg>200 fires at [(600, 500)], timeout low 200 fires at [(300, 'low')]
PASS  pulse '>' fires at trailing edge (600), timeout at fall+200 (300)
PASS  line stuck low forever: pulse-width never fires, timeout does
```

같은 500 ns low 구간인데 timeout 은 300 에서, pulse-width 는 600 에서 발사한다. pre-trigger 윈도우가 200 이면 pulse-width 트리거의 윈도우에는 **원인(마지막 하강 에지)이 아예 안 들어온다.** 무엇을 보고 싶은지(원인 vs 복귀)에 따라 트리거와 트리거 위치를 고르는 이유다.

```svg
<svg viewBox="0 0 760 190" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">extract_windows(len=10000, pre=100, post=300, holdoff=pre+post=400) — 축척 아님</text>
  <rect x="20" y="70" width="720" height="26" class="f-mut" stroke="currentColor"/>
  <g stroke="currentColor">
    <rect x="160" y="70" width="40" height="26" class="f-ok"/><rect x="200" y="70" width="110" height="26" class="f-ok"/>
    <rect x="420" y="70" width="40" height="26" class="f-ok"/><rect x="460" y="70" width="110" height="26" class="f-ok"/>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="2">
    <path d="M30 50 L30 110"/><path d="M200 50 L200 110"/><path d="M260 50 L260 110"/>
    <path d="M460 50 L460 110"/><path d="M730 50 L730 110"/>
  </g>
  <g stroke="currentColor" fill="none" stroke-dasharray="4 3"><path d="M200 125 L360 125"/><path d="M360 118 L360 132"/></g>
  <g fill="currentColor" text-anchor="middle">
    <text x="30" y="44">t=10</text><text x="200" y="44">t=500</text><text x="260" y="44">t=700</text>
    <text x="460" y="44">t=2000</text><text x="730" y="44">t=9990</text>
    <text x="180" y="88" font-size="12">pre</text><text x="255" y="88" font-size="12">post</text>
    <text x="440" y="88" font-size="12">pre</text><text x="515" y="88" font-size="12">post</text>
    <text x="280" y="142">holdoff 400 (재무장 전)</text>
    <text x="30" y="160" font-weight="bold">pretrig</text><text x="260" y="160" font-weight="bold">holdoff</text>
    <text x="730" y="160" font-weight="bold">end</text>
    <text x="200" y="160">채택</text><text x="460" y="160">채택</text>
  </g>
  <text x="20" y="182" fill="currentColor">프리트리거가 덜 찼거나, 재무장 전이거나, 레코드 끝이라 포스트를 못 채우면 그 트리거는 버린다</text>
</svg>
```

`holdoff` 기본값 `pre + post`: 스코프는 트리거 후 포스트트리거를 다 채우고, 다음 트리거의 프리트리거를 다시 채워야 재무장(re-arm)한다. 실기에서 holdoff 를 수동으로 늘리는 대표 사례는 SPI 같은 버스트 신호다. holdoff 를 버스트 길이보다 길게 잡으면 **항상 프레임의 첫 클록** 에서 트리거되어 화면이 안정된다.

### Part C — 10M 샘플 스캔 결과와 리포트

런타임 노트: 10M 샘플을 **줄이지 않고 전부** 처리했다. 1M(2²⁰) 샘플 청크, 4개 트리거를 같은 청크에 차례로 적용한다. 실제 출력:

```text
-- Part C: 10M-sample capture scan --
PASS  capture is deterministic (sha256)
      build 0.03 s, scan 10000000 samples x 4 triggers 1.42 s (142 ns/sample)
      events: ['runt+', 'glitch', 'timeout', 'runt-']
PASS  exactly 4 events: runt+, glitch, timeout, runt-
      runt+ trigger index - planted center: got=4 ref=4 tol=4
      runt+ peak [V]: got=1.26 ref=1.3 tol=0.08
      glitch trigger index - planted center: got=3 ref=2 tol=3
      glitch width [ns]: got=4 ref=4 tol=1
      timeout index - (last fall before stall + 200): got=1 ref=0 tol=1
      runt- trough [V]: got=1.32 ref=1.3 tol=0.08
PASS  rising edges = 100000 periods - 20 stalled + 1 glitch + 1 after runt- dip = 99982
PASS  chunk 999983 (prime) on 10M -> same events
PASS  all 4 windows complete (200+600 samples), none dropped
      EVT1 RUNT+    @1.357916 ms  peak=1.26 V  (>L 0.80 V, <H 2.00 V)
      EVT2 GLITCH   @4.123415 ms  width=4 ns  (< 10 ns)
      EVT3 TIMEOUT  @5.800176 ms  low >= 200 ns  (clock stalled)
      EVT4 RUNT-    @7.777753 ms  trough=1.32 V  (<H 2.00 V, >L 0.80 V)
```

(`solution.py` 전체 실행은 `time -p` 기준 real 2.95 s. 10M 스캔을 청크 크기를 바꿔 두 번 하므로 스캔 시간의 두 배쯤이다.)

**숫자 읽기:**

- runt+ 피크 1.26 V (생성기 1.3 V + 잡음 + 8비트 양자화 20 mV). 트리거는 피크보다 4 샘플 뒤, 0.6 V(= L − h) 아래로 돌아온 순간이다. 저장된 윈도우 `evt1_runt_pos.csv` 의 트리거 주변 (실제 값): `-5:1.22 -4:1.26 -3:1.20 -2:0.96 -1:0.66 0:0.36` V.
- 글리치: Schmitt 폭 4 ns, 트리거는 뒷에지. `evt2_glitch.csv`: `-5:1.64 -4:2.92 -3:3.24 -2:2.98 -1:1.66 0:0.34` V. 1.64 V 는 아직 1.75 V 미만이라 상승 전이는 −4 에서 잡힌다. 0.34 V 에서 하강 전이(1.55 V 미만)가 일어나 폭 = 4 다.
- timeout: 정지 직전 하강 에지는 1.55 V 아래로 내려가는 샘플이 공칭 에지 중심(75)보다 1 샘플 늦다. 그래서 "+1" 이 나오고, `evt3_timeout.csv` 는 t = −200 에 하강 에지(0.38 V)가 있다.
- 상승 에지 99,982 = 100,000 − 정지 20 주기 + 글리치 1 + runt− 의 딥 후 복귀 1. 에지 개수를 기대 주기 수와 맞춰 보는 것만으로도 "어딘가 이상하다" 를 알 수 있다. 다만 **어디서** 인지는 트리거가 알려 준다.

**하드웨어 엔지니어에게 넘기는 정리 (프로그램 출력이 아니라 위 결과를 읽고 쓴 메모):**

| 이벤트 | 가능한 원인 (가설) | 다음 측정 |
|---|---|---|
| RUNT+ 1.26 V @ low | 인접 신호 크로스토크, 리턴 경로/그라운드 바운스, 다른 드라이버와 잠깐 충돌(contention) | 같은 시각 인접 넷(CS, MOSI)을 2채널로 함께 트리거. 수신 핀(IMU 쪽)에서 다시 측정 |
| GLITCH 4 ns 풀스윙 | MCU 클록 출력 핀 재설정(GPIO 모드 전환)·클록 게이팅 순간, 반사 | 펌웨어 이벤트(GPIO 토글 마커)와 상관. 소스 단 직렬 저항 유무 확인 |
| TIMEOUT 2 µs 정지 | SI 문제가 아니라 펌웨어: DMA underrun, 인터럽트 지연, 클록 스트레칭 없는 SPI 의 FIFO 고갈 | 같은 시각 CS 상태 확인 (CS 활성 중 정지면 프레임 내부 정지) |
| RUNT− 1.32 V @ high | 위 runt+ 와 동일 계열 + 전원 순간 강하 | VDD_IO 를 AC 결합으로 같이 트리거 (HWLAB-B08 참고) |

주의: runt·글리치를 보기 전에 **측정 체인부터 의심한다.** 15 cm 그라운드 리드의 링잉(HWLAB-B05 참고)이나 대역 부족으로 진폭이 깎인 짧은 펄스는 가짜 runt 를 만든다. 스프링 그라운드/팁 어댑터로 수신 핀 바로 옆에서 다시 찍어 같은 이벤트가 나오는지가 첫 확인이다.

**장비/도구가 있을 때** (이 레슨의 테스트는 합성 데이터만 쓴다. 아래는 실행하지 않은 예시다): 실제 스코프에서는 runt 트리거(두 레벨 = 수신기 VIL/VIH), Normal 트리거 모드, 프리트리거 25% 정도, single 수집 또는 segmented memory(트리거마다 짧은 세그먼트를 연속 저장)로 위 4개를 모은다. 자동화는 PyVISA + SCPI 로 한다. 명령 이름은 벤더·기종마다 다르니 반드시 해당 프로그래밍 매뉴얼에서 확인한다.

```python
import pyvisa                                   # 장비가 있을 때만. 이 레슨에서는 실행하지 않음
scope = pyvisa.ResourceManager().open_resource("TCPIP0::<scope-ip>::INSTR")
print(scope.query("*IDN?"))
# 트리거 모드/레벨/극성 설정 → single → 트리거 대기 → 파형 바이너리 읽기 (명령은 기종 매뉴얼 참조)
```

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py                    # 44/44 passed, exit 0 (~3 s)
python3 -I solution.py --save captures    # 테스트 + 윈도우 CSV 4개 다시 쓰기 (커밋된 파일과 같아야 함)
python3 -I -W error -X dev solution.py    # 경고 없이 44/44
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| edge 를 `prev < level <= x` 단일 임계로 | 느린 잡음 에지에서 25번 발사 (정답 5) | Schmitt `x >= hi` / `x < lo`, 밴드 ≥ 잡음 p-p |
| runt 를 "피크가 L..H 사이인 펄스" 로 사후 계산 | 피크를 알려면 펄스가 끝나야 함. 스트리밍 불가, 풀스윙 + 링잉도 헷갈림 | 비교기 두 개의 전이 순서로 상태기계 |
| 한 샘플에 L·H 를 다 넘는 에지를 처리 안 함 | 정상 에지가 +후보로 남아 다음 하강에서 가짜 runt | 바닥 → 꼭대기 직행 분기 |
| pulse `>` 로 stuck 라인을 잡으려 함 | 뒷에지가 안 오면 영원히 발사 안 됨 | timeout 트리거 (만료 순간 발사) |
| 첫 펄스 폭을 0 또는 시작 기준으로 계산 | 캡처마다 시작에서 가짜 글리치/롱펄스 | `t0 is None` 이면 판정 생략 |
| 상태를 지역 변수에만 두고 `self` 에 저장 안 함 | 청크 경계 이벤트를 놓치거나 중복 | 루프 전 꺼내고 루프 후 `self.x = x` |
| 빈 결과로 청크 테스트만 통과시키고 끝 | `[] == []` 공짜 PASS | 양성 테스트(잡아야 할 이벤트 수)와 짝 |
| timeout 을 `j - last >= T` 로 매 샘플 발사 | 같은 구간에서 수천 번 발사 | 발사 후 `dl = -1`, 다음 전이에서 재무장 |
| 윈도우를 `buf[max(0,t-pre):t+post]` 로 잘라 저장 | 길이가 다른 윈도우 → 트리거 위치(t=0)가 어긋남 | 프리/포스트 못 채우면 버리고 이유를 기록 |
| 트리거 폭을 IEEE-181 펄스 폭으로 보고 | 트리거는 Schmitt 교차·±1 샘플 | 폭 측정은 HWLAB-B04 방식으로 따로 |

## 8. 면접 Q&A

<details>
<summary>Q1. 몇 초에 한 번 생기는 글리치를 스코프로 어떻게 잡나요?</summary>

auto 화면으로는 거의 못 본다. dead time 때문에 실제로 보는 시간 비율이 1% 안팎일 수 있다 (1000 wfm/s × 10 µs). 대신 이벤트를 **트리거 조건으로 정의** 한다: 좁은 펄스면 pulse-width `<`, 수신기 VIL/VIH 사이에서 돌아오면 runt, 멈춤이면 timeout. 모드는 Normal 로 두고(auto 는 트리거가 없으면 스스로 돈다), single 또는 segmented memory 로 이벤트마다 짧은 구간을 모은다. 무한 persistence 를 켜 두고 밤새 돌리는 것도 방법이다.
꼬리질문: "어떤 조건인지 모르면?" → 우선 정상 파형의 통계를 낸다 (폭 히스토그램, 에지 개수 vs 기대 주기 수). 그다음 "정상 범위 밖" 을 트리거로 만든다 (pulse width outside range, window 트리거). 이 레슨의 에지 개수 99,982 vs 기대값처럼 숫자가 먼저 이상을 알려 준다.

</details>

<details>
<summary>Q2. Runt 펄스란? 왜 위험하고, 임계값은 어떻게 정하나요?</summary>

한 논리 임계를 넘었지만 반대쪽 임계에는 못 닿고 돌아오는 펄스다. 위험한 이유는 수신기의 VIL..VIH 사이가 불확정 영역이기 때문이다. 칩마다, 온도마다, 심지어 같은 칩의 플립플롭마다 0/1 판정이 다를 수 있다. 클록이면 일부 플롭만 클록을 받는 상황이 되고, 비동기 입력이면 메타안정성 위험이 생긴다. 임계는 **받는 칩의 VIL/VIH** 로 정한다 (3.3 V LVCMOS 는 0.8 / 2.0 V). 드라이버 출력의 10/90% 가 아니다.
꼬리질문: "runt 가 나오면 원인은?" → 버스 충돌(두 드라이버), 크로스토크, 그라운드 바운스, 반사, 전원 강하. 그리고 측정 아티팩트(긴 그라운드 리드 링잉, 부족한 대역)가 늘 후보다. 수신 핀 근처에서 스프링 그라운드로 다시 찍어 재현되는지부터 본다.

</details>

<details>
<summary>Q3. Pulse-width ">" 트리거와 timeout 트리거는 뭐가 다른가요?</summary>

pulse-width 는 펄스가 **끝나야** 폭을 알기 때문에 뒷에지에서 발사한다. timeout 은 마지막 에지 후 T 가 지나는 **그 순간** 발사한다. 그래서 ① 선이 영원히 멈추면(stuck) pulse-width 는 아무것도 못 잡고 timeout 만 잡는다. ② 같은 이벤트라도 트리거 시각이 다르므로 프리트리거 윈도우에 무엇(원인 에지 vs 복귀 에지)이 들어오는지가 달라진다. 이 레슨에서 500 ns low 구간은 timeout 300, pulse `>` 600 이었다.
꼬리질문: "워치독 리셋 직전을 보고 싶으면?" → 하트비트 GPIO 에 timeout 트리거를 걸고 프리트리거를 길게 잡는다. 리셋 순간이 아니라 "하트비트가 끊긴 순간" 을 기준으로 앞 상황이 보인다.

</details>

<details>
<summary>Q4. 트리거 히스테리시스(노이즈 리젝트)는 왜 필요하고 얼마로 하나요?</summary>

느린 에지가 레벨 근처에 머무는 동안 잡음이 레벨을 여러 번 넘나들면 트리거가 같은 에지에서 여러 번 걸린다. 화면이 좌우로 흔들리고 runt 트리거는 가짜를 낸다. 밴드를 잡음 p-p 보다 크게 둔다. 가우시안이면 대략 6σ 이다. 이 레슨에서 40 mV rms 잡음에 0.1 V 밴드는 edge 7개(정답 5), 가짜 runt 2개를 냈고 0.2 V 에서 깨끗해졌다.
꼬리질문: "그럼 크게 잡을수록 좋은가?" → 아니다. 밴드보다 작은 진짜 이벤트(작은 runt, 진폭이 깎인 글리치)를 놓친다. HF reject(트리거 경로 저역통과)도 마찬가지로 좁은 글리치를 지운다. 트리거 경로 필터는 화면 파형과 별개라는 점도 기억한다.

</details>

<details>
<summary>Q5. 1 GSa/s 스코프로 0.5 ns 글리치를 잡을 수 있나요?</summary>

샘플 기반 디지털 트리거로는 운에 달려 있다. 샘플 사이에 끝나는 펄스는 ADC 가 아예 못 본다. 게다가 아날로그 대역폭이 펄스를 먼저 깎는다. 단극 근사로 상승시간 ≈ 0.35/BW 이니 200 MHz 스코프의 상승시간은 ~1.75 ns 이고, 0.5 ns 펄스는 진폭이 크게 줄어 임계에 못 닿을 수 있다 (HWLAB-B05 참고). 데이터시트의 트리거 항목(최소 검출 펄스 폭, 트리거 감도)을 확인한다. 아날로그 비교기 트리거 경로가 있는 기종은 샘플레이트와 무관하게 더 좁은 펄스를 검출하기도 한다. 수집 쪽에서는 **peak detect** 모드가 솎아낸 샘플 사이의 min/max 를 남겨 준다 (연습 1).
꼬리질문: "메모리 깊이와는 무슨 관계?" → 긴 시간축에서 메모리가 모자라면 스코프가 샘플레이트를 낮춘다. 10 ms 를 1 Mpts 로 찍으면 100 MSa/s 가 되어 4 ns 글리치가 화면에서 사라진다 (HWLAB-B07 참고).

</details>

<details>
<summary>Q6. 프리트리거와 holdoff 는 각각 무엇을 위한 것인가요?</summary>

프리트리거는 트리거 **이전** 데이터를 보존해 원인을 보게 한다. 수집 메모리가 링 버퍼로 계속 돌다가 트리거 후 포스트트리거만큼 더 채우고 멈춘다. 그래서 프리트리거가 다 차기 전에 생긴 트리거는 무시된다 (`pretrig`). holdoff 는 트리거 후 일정 시간 재무장을 막는다. 버스트 신호에서 항상 버스트의 첫 에지로 트리거하게 만들거나, 같은 이벤트의 링잉에 연속 트리거되는 것을 막는다.
꼬리질문: "segmented memory 의 장점은?" → 메모리를 짧은 세그먼트로 나눠 트리거마다 하나씩 채우고 재무장 시간이 짧다. 드문 이벤트 수천 개를 이벤트 사이의 긴 빈 시간 없이 모으고, 세그먼트마다 타임스탬프가 붙어 발생 간격 통계를 낼 수 있다.

</details>

<details>
<summary>Q7. 이 상태기계를 FPGA 에서 1 GSa/s 로 돌리려면?</summary>

FPGA 클록은 ADC 샘플레이트보다 느리므로 클록당 N 샘플(예: 125 MHz × 8)을 병렬로 받는다. 각 샘플의 비교기 출력(N비트 × 비교기 수)을 한 클록에 계산하고, 상태 전이는 N 샘플을 순서대로 펼친 조합 논리(또는 상태 전이 함수의 병렬 합성)로 처리한다. 첫 발사 위치는 priority encoder 로 찾는다. 결과는 이 레슨의 "청크 + 상태 이어받기" 와 같은 문제다. 청크 경계에서 상태를 잃지 않아야 하고, 경계에 걸친 이벤트로 테스트해야 한다.
꼬리질문: "타임스탬프 정밀도는?" → 정수 샘플이면 ±0.5 샘플 트리거 지터가 생긴다. 실제 스코프는 교차점 보간(연습 2)으로 트리거 위치를 샘플 간격보다 훨씬 정밀하게 맞춘다. 데이터시트의 "trigger jitter" 항목이 이 결과다.

</details>

## 9. 추가 연습

### 연습 1 — 단순 솎아내기 vs peak detect

저장된 글리치 윈도우를 10:1 로 줄인다 (1 GSa/s → 100 MSa/s). `decimate_plain`(10 샘플마다 1개)과 `decimate_peak`(블록마다 min/max)를 비교하라. 글리치가 든 low 구간에서 각각의 최댓값, 그리고 전체 윈도우에서 edge 트리거가 잡는 상승 에지 수는?

<details>
<summary>답 보기</summary>

실제 출력: plain 최대 0.34 V (VIL 0.8 V 아래 — 글리치가 사라짐), peak-detect 최대 3.24 V. 전체 800 ns 윈도우에서 plain 은 클록 에지 8개만, peak-detect 의 max 스트림은 9개다 (블록 19 = 글리치). 클록 자체(10 MHz)는 100 MSa/s 로도 잘 보이므로 "파형은 멀쩡해 보이는데 글리치만 없다" 는 위험한 화면이 된다. peak detect 는 min/max 쌍으로 메모리를 두 배 쓰는 대신 블록 안의 극값을 보존한다. 하지만 **시각 정보는 블록 단위로 뭉개진다** (에지가 한 블록 앞당겨 보인다).

</details>

### 연습 2 — 트리거 시각 보간

50% 레벨 상승 교차를 정수 샘플로 잡으면 트리거 시각 오차가 몇 ns 범위인가? 앞뒤 두 샘플로 선형 보간하면?

<details>
<summary>답 보기</summary>

`t = (j-1) + (level - x[j-1]) / (x[j] - x[j-1])`. 실제 출력: 정수 스탬프 오차 p-p 0.999 ns (1 샘플 전체), 보간 최대 오차 0.0346 ns (잡음 없는 2 ns `tanh` 에지, 200회). 남는 오차는 에지 곡률 때문이다. 에지가 샘플 간격보다 충분히 느려야(대역 제한 + 오버샘플링) 보간이 정당하다. 화면 겹쳐 그리기(persistence)에서 파형이 "번져" 보이지 않는 이유이고, 지터 측정(SIV/COMM 쪽)의 출발점이다.

</details>

### 연습 3 — 화면만 보고 기다리면 얼마나 걸리나

1000 wfm/s, 화면 10 µs, 글리치가 초당 1회(포아송). 10초 지켜볼 때 한 번이라도 볼 확률, 95% 확률로 보려면 몇 초? 글리치 트리거를 걸면?

<details>
<summary>답 보기</summary>

live = 1000 × 10 µs = 0.01. `P = 1 − exp(−r · live · t)` = 1 − e^−0.1 = 0.095. `t95 = ln 20 / (r · live)` = 299.6 s ≈ 5분. 트리거를 걸면 고립된 이벤트에 대해 live ≈ 1 이므로 t95 ≈ 3.0 s. 가정: 이벤트끼리 독립이고, 재무장 시간보다 드물게 일어난다 (몰려서 생기면 dead time 안의 이벤트는 놓친다 — segmented memory 가 줄여 주는 부분).

</details>

### 연습 4 — runt 폭 조건

`RuntWidthTrigger(L, H, hys, max_w)`: runt+ 중 폭(L 상승 교차 ~ L−hys 복귀) `< max_w` 만 발사. 폭 5 와 40 인 runt 가 있을 때 `max_w=20` 이면?

<details>
<summary>답 보기</summary>

`[(15, 5, 50)]` — 좁은 runt 만 잡힌다. `max_w=100` 이면 폭 [5, 40] 둘 다. 폭 조건은 "넓은 중간 레벨 고원"(버스 충돌로 두 드라이버가 싸우는 구간, 오픈드레인 라인의 약한 풀업)과 "좁은 runt"(크로스토크, 반사)를 갈라 원인 후보를 좁히는 데 쓴다. 구현은 `RuntTrigger` 를 상속해 +후보 진입 시각 `t0` 하나만 더 저장한다. 청크(3)로 나눠도 같은 결과임을 테스트한다.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py          # 12/12 passed, exit 0
```

## 10. 핵심 정리

- 트리거 = **비교기(히스테리시스) + 작은 상태기계**. Edge(전이), pulse-width(앞에지 시각 기억, 뒷에지에서 판정), runt(비교기 2개의 전이 순서), timeout(마감 시각).
- 발사 지점이 다르다: runt 는 복귀 순간, pulse-width 는 뒷에지, timeout 은 만료 순간. stuck 라인은 timeout 만 잡는다.
- runt 임계 = **수신기 VIL/VIH** (3.3 V LVCMOS 0.8 / 2.0 V). 그 사이 펄스는 수신기마다 판정이 다르다 → double clocking.
- 히스테리시스 ≥ 잡음 p-p (가우시안 ≈ 6σ). 0 이면 느린 에지에서 다중 트리거·가짜 runt (25/41 vs 5/0).
- 스트리밍 구현은 상태를 전부 넘겨 **어떤 청크 크기든 같은 답**. `[] == []` 공짜 PASS 를 양성 테스트로 막는다.
- 윈도우: pre/post, 프리트리거 미충전·holdoff·레코드 끝은 버리고 이유를 남긴다. holdoff 기본 = pre + post.
- 10M 샘플 순수 Python: 4개 트리거 1.42 s (142 ns/샘플), 1M 청크. 이상 4개를 정확히 찾고 에지 수 99,982 로 교차 확인.
- 측정 체인부터 의심: 그라운드 리드 링잉(B05), 대역/샘플레이트 부족(B05/B07)이 가짜 runt·사라진 글리치를 만든다.

## 참고자료

- JEDEC JESD8C.01 — Interface Standard for Nominal 3 V/3.3 V Supply Digital Integrated Circuits (LVTTL/LVCMOS VIH 2.0 V, VIL 0.8 V)
- Tektronix, *XYZs of Oscilloscopes* primer — 트리거 종류(edge, pulse width, runt, timeout, logic), holdoff, 수집 모드(sample, peak detect)
- Keysight, *Evaluating Oscilloscope Fundamentals* / waveform update rate 관련 application note — dead time 과 드문 이벤트 포착 확률
- Rohde & Schwarz, digital trigger system 백서 (RTO/MXO 계열) — ADC 샘플 기반 트리거, 트리거 지터, 히스테리시스 설정
- IEEE Std 181-2011 — Transitions, Pulses, and Related Waveforms (폭·상승시간 측정은 HWLAB-B04)
- [PyVISA 문서](https://pyvisa.readthedocs.io/) — 장비 자동화 (SCPI 명령 자체는 기종별 programming manual)
- [Python `bytes` / `hashlib` 문서](https://docs.python.org/3/library/stdtypes.html#bytes-objects)
