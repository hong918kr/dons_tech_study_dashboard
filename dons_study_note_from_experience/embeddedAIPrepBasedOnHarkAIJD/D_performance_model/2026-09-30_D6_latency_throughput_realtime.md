# D6. Latency · Throughput · Real-time — 평균이 아니라 꼬리를 본다

> **이 노트를 다 읽으면**: latency·throughput·batch·pipelining·Little's law를 숫자로 구분해 말할 수 있다 · warm-up·반복·분위수(p50/p90/p99/max)·스레드 고정을 갖춘 벤치마크 하니스를 Python과 C로 짜서 torch·ONNX Runtime 모델을 제대로 잴 수 있다 · 10 ms hop 스트리밍 루프에서 deadline miss·drop을 링버퍼 깊이와 latency 분포로 계산할 수 있다 · wake word의 알고리즘 지연과 계산 지연을 분리해 검출 latency를 예산으로 나눌 수 있다
> **JD 연결**: "Profile and optimize memory usage, power consumption, and **real-time performance**" · "Co-design model architectures that meet **latency**, memory, power, bandwidth" · study_prep_list **D6** 행: 평균 vs tail latency, jitter, batch=1 스트리밍의 특성, deadline 기반 설계 (오디오 프레임 10–20 ms) · 연결: J2(실시간 설계: deadline, WCET, jitter), K2(DWT CYCCNT, PMU, perf), K4(지속 성능), M2(벤치마크 방법론)
> **Don 기준 난이도**: 인터럽트·DMA·RTOS 우선순위·측정 기반 성능 튜닝은 이미 강하다 / 새로 배울 것은 ML 쪽 특수성 — 프레임워크 오버헤드와 cold start, 창(window)·smoothing이 만드는 **알고리즘 지연**, batch와 처리량의 관계, 분포(꼬리)로 모델 latency를 말하는 습관
> **선행 노트**: B3(스트리밍 RNN, 상태 carry), B5(frame·hop·DMA ping-pong, KWS streaming 검출), B7(후처리·debounce), C7(latency LUT와 벤치 방법론). 병렬 작성 중인 D1(MAC)·D3(roofline)·D7(에너지)와 함께 보면 좋다

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어에서 Don이 늘 보던 숫자를 떠올려 보자. "4K random read QoS: 평균 80 µs, **99.99% 1 ms 이하**". 엔터프라이즈 고객은 평균을 거의 보지 않는다. 가끔 튀는 한 번(GC가 겹친 read, 에러 복구가 걸린 read)이 서비스 전체를 멈추기 때문이다. 이 노트의 주제는 그 감각을 **모델 추론**으로 옮기는 것이다.

on-device ML에서도 똑같다. 귀에 거는 기기가 10 ms마다 오디오 블록을 받는데, 모델이 **평균** 5 ms에 끝난다고 안심하면 안 된다. 100번에 한 번 12 ms가 걸리면 그 순간 DMA가 다음 블록을 덮어쓰고, 샘플이 사라지고, wake word를 놓치거나 음성이 끊긴다. 그래서 edge ML 엔지니어는 latency를 **분포**로 말한다: p50, p99, max, 그리고 "deadline을 넘긴 비율".

그리고 ML에는 펌웨어에 없던 지연이 하나 더 있다. **알고리즘 지연(algorithmic latency)**: 모델이 1초짜리 창을 보고 판단한다면, 계산이 0 ms에 끝나도 말이 끝난 뒤 창이 그 말을 다 품을 때까지는 판단할 수 없다. smoothing과 debounce도 지연을 더한다. 사용자가 느끼는 지연은 이 둘의 합이다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13">마이크에서 동작까지 — 각 단계가 더하는 지연의 종류</text> <rect x="10" y="40" width="84" height="50" rx="5" fill="none" stroke="#888" stroke-width="2"/> <text x="52" y="61" font-size="12" text-anchor="middle">mic · ADC</text><text x="52" y="78" font-size="12" text-anchor="middle">PDM/I2S</text> <rect x="106" y="40" width="84" height="50" rx="5" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="148" y="61" font-size="12" text-anchor="middle">DMA 블록</text><text x="148" y="78" font-size="12" text-anchor="middle">10 ms 모음</text> <rect x="202" y="40" width="84" height="50" rx="5" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="244" y="61" font-size="12" text-anchor="middle">feature</text><text x="244" y="78" font-size="12" text-anchor="middle">FFT·mel</text>
<rect x="298" y="40" width="84" height="50" rx="5" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="340" y="61" font-size="12" text-anchor="middle">창 버퍼</text><text x="340" y="78" font-size="12" text-anchor="middle">1 s, hop</text> <rect x="394" y="40" width="84" height="50" rx="5" fill="none" stroke="#4a7bd0" stroke-width="3"/>
<text x="436" y="61" font-size="12" text-anchor="middle">model</text><text x="436" y="78" font-size="12" text-anchor="middle">추론</text> <rect x="490" y="40" width="84" height="50" rx="5" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="532" y="61" font-size="12" text-anchor="middle">후처리</text><text x="532" y="78" font-size="12" text-anchor="middle">smooth·debounce</text>
<rect x="586" y="40" width="84" height="50" rx="5" fill="none" stroke="#888" stroke-width="2"/> <text x="628" y="61" font-size="12" text-anchor="middle">동작</text><text x="628" y="78" font-size="12" text-anchor="middle">AP wake·chime</text>
<line x1="94" y1="65" x2="104" y2="65" stroke="currentColor"/><line x1="190" y1="65" x2="200" y2="65" stroke="currentColor"/><line x1="286" y1="65" x2="296" y2="65" stroke="currentColor"/> <line x1="382" y1="65" x2="392" y2="65" stroke="currentColor"/><line x1="478" y1="65" x2="488" y2="65" stroke="currentColor"/><line x1="574" y1="65" x2="584" y2="65" stroke="currentColor"/>
<text x="148" y="108" font-size="12" text-anchor="middle">알고리즘</text><text x="244" y="108" font-size="12" text-anchor="middle">계산</text> <text x="340" y="108" font-size="12" text-anchor="middle">알고리즘</text><text x="436" y="108" font-size="12" text-anchor="middle">계산</text> <text x="532" y="108" font-size="12" text-anchor="middle">알고리즘</text><text x="628" y="108" font-size="12" text-anchor="middle">시스템</text>
<line x1="394" y1="130" x2="478" y2="130" stroke="#4a7bd0" stroke-width="2"/><line x1="394" y1="124" x2="394" y2="136" stroke="#4a7bd0"/><line x1="478" y1="124" x2="478" y2="136" stroke="#4a7bd0"/> <text x="436" y="150" font-size="12" text-anchor="middle">model-only latency</text>
<line x1="202" y1="172" x2="574" y2="172" stroke="currentColor" stroke-width="2"/><line x1="202" y1="166" x2="202" y2="178" stroke="currentColor"/><line x1="574" y1="166" x2="574" y2="178" stroke="currentColor"/> <text x="388" y="192" font-size="12" text-anchor="middle">end-to-end 계산 latency (전처리 + 복사 + 모델 + 후처리)</text> <line x1="10" y1="212" x2="670" y2="212" stroke="#e08a3c" stroke-width="2" stroke-dasharray="6 3"/>
<text x="340" y="232" font-size="12" text-anchor="middle">사용자가 느끼는 지연 = 알고리즘 지연(주황) + 계산 지연(파랑) + 시스템 지연(회색)</text>
</svg>
```

그림 1 — 오디오 wake word 파이프라인의 지연 분해. 벤치마크 표에 흔히 나오는 숫자는 굵은 파랑 상자(model-only) 하나뿐이다. 실제 제품에서는 주황(창·hop·smoothing)이 수백 ms로 가장 크고, 파랑의 **꼬리**가 deadline을 깨뜨린다.

이 노트의 순서: 용어와 Little's law(1절) → Python 벤치마크 하니스로 torch·ORT 제대로 재기(2절) → end-to-end 분해(3절) → C 하니스와 MCU cycle counter(4절) → 스트리밍 deadline·링버퍼 시뮬레이션(5절) → 알고리즘 지연 vs 계산 지연(6절) → RTOS 스케줄링(7절) → 사용자 기능 latency 예산(8절) → batching과 처리량(9절).

측정 환경: **Apple M2 (P-core 4 + E-core 4) 노트북**, PyTorch 2.8 eager, ONNX Runtime 1.19 CPU EP, fp32. 이 노트를 쓰는 동안 같은 머신에서 **다른 작업들이 병렬로 돌고 있었다**. 보통은 나쁜 조건이지만 이 노트에는 오히려 좋다. 꼬리(tail)가 어디서 오는지 실제 숫자로 볼 수 있기 때문이다. 절대값은 타깃 칩(Cortex-M, Hexagon)과 전혀 다르니 **방법과 현상**을 가져가라.

---

## 1. 용어 — latency, throughput, batch, pipelining

### 1.1 직관 — 톨게이트 비유

고속도로 톨게이트를 생각하자.

- **latency(지연)**: 차 한 대가 톨게이트에 들어가서 나올 때까지 걸린 시간. 추론 한 번에 걸린 시간이다. 단위는 ms, µs.
- **throughput(처리량)**: 1초에 톨게이트를 통과하는 차의 수. 1초에 끝나는 추론 수다. 단위는 inferences/s (inf/s), 프레임이면 fps.
- **batch**: 차 여러 대를 버스 한 대에 태워 한 번에 통과시키는 것. 입력 N개를 텐서 `[N, …]` 하나로 묶어 모델을 한 번 호출한다. 1회 호출은 느려지지만 1초당 처리량은 늘 수 있다.
- **pipelining**: 톨게이트를 여러 단계(요금 계산 → 영수증 → 차단기)로 나누고 단계마다 사람을 둔다. 차 한 대가 걸리는 시간은 그대로인데 동시에 여러 대가 서로 다른 단계에 있으니 처리량이 오른다.

### 1.2 정의와 관계식

```
latency        L  = 한 입력이 도착한 시각 → 결과가 나온 시각
throughput     X  = 완료된 입력 수 / 시간
batch=1, 직렬   X  = 1 / L                    (한 번에 하나, 앞의 것이 끝나야 다음)
batch=B, 직렬   X  = B / L(B)                 (L(B)는 batch B 한 번 호출의 시간)
pipelined      X  = 1 / max(stage 시간)       (가장 느린 단계가 병목)
               L  = ∑ stage 시간 (+ 큐 대기)
Little's law   N  = X · W                     (N = 시스템 안에 머무는 평균 개수, W = 평균 체류 시간)
```

말로 하면: batch=1 직렬에서는 latency와 throughput이 역수 관계라서 하나만 알면 된다. 그러나 batch나 pipeline이 들어오는 순간 둘은 **따로 움직인다**. pipeline은 throughput을 병목 단계로 올리고 latency는 줄이지 않는다. batch는 throughput을 올리는 대신 latency를 늘린다.

**Little's law**는 큐 이론에서 가장 단순하면서 강력한 식이다. 분포나 스케줄 방식과 상관없이 (정상 상태에서) 항상 성립한다. 예를 들어 100 fps로 들어오는 프레임이 도착에서 결과까지 평균 30 ms 머문다면 시스템 안에는 평균 `100 × 0.030 = 3`개 프레임이 떠 있다. 즉 **버퍼 슬롯이 최소 3개**는 있어야 한다. Don이 NVMe 큐 깊이(queue depth)와 IOPS·latency 관계로 이미 써 본 식이다: `IOPS = QD / latency`.

### 1.3 손계산 — 3단 파이프라인

preprocess 2 ms, model 5 ms, postprocess 1 ms.

- 직렬(코어 1개): latency = 2 + 5 + 1 = 8 ms. 다음 프레임은 8 ms 뒤에 시작하므로 throughput = 1/8 ms = 125 fps.
- 파이프라인(단계마다 코어 1개): 병목은 model 5 ms. throughput = 1/5 ms = 200 fps. latency는 여전히 8 ms.
- 입력을 4 ms마다 넣으면? 병목 단계가 5 ms마다만 처리할 수 있으니 model 앞에 큐가 매 프레임 1 ms씩 늘어난다. latency가 한없이 커진다. **처리량 상한보다 빨리 넣으면 latency는 발산한다.**

### 1.4 코드로 확인

도착 주기 P를 바꿔 가며 직렬/파이프라인을 이벤트 단위로 시뮬레이션한다.

```python
# 예제 1: 3단 파이프라인 — 직렬 vs 파이프라인(단계마다 다른 코어), 입력 도착 주기 P를 바꿔 본다
stages = {"preprocess": 2.0, "model": 5.0, "postprocess": 1.0}   # ms, 각 단계 고정 시간
N = 1000                                                         # 프레임 수
def simulate(pipelined, P):
    free = {s: 0.0 for s in stages}           # 각 단계(코어)가 다음 일을 받을 수 있는 시각
    lat, prev_done, last = [], 0.0, 0.0
    for i in range(N):
        arrive = i * P                        # 프레임 i가 도착한 시각
        t = arrive if pipelined else max(arrive, prev_done)
        for s, d in stages.items():
            t = max(t, free[s]) + d           # 단계가 비어야 시작, d ms 후 끝
            free[s] = t
        lat.append(t - arrive); prev_done = last = t
    thr = N / (last / 1000)                   # 처리량 frames/s
    W = sum(lat[-100:]) / 100                 # 마지막 100 프레임의 평균 latency (도착→완료)
    return W, thr
for name, p, P in (("serial", False, 8), ("pipelined", True, 5), ("pipelined", True, 4)):
    W, thr = simulate(p, P)
    print(f"{name:9s} P={P} ms  latency = {W:7.1f} ms  throughput = {thr:5.1f} fps  "
          f"Little L = λ·W = {thr * W / 1000:6.2f} frames in flight")
```

```text
serial    P=8 ms  latency =     8.0 ms  throughput = 125.0 fps  Little L = λ·W =   1.00 frames in flight
pipelined P=5 ms  latency =     8.0 ms  throughput = 199.9 fps  Little L = λ·W =   1.60 frames in flight
pipelined P=4 ms  latency =   957.5 ms  throughput = 199.9 fps  Little L = λ·W = 191.39 frames in flight
```

출력에서 볼 것: 파이프라인은 latency 8 ms 그대로, throughput만 125 → 200 fps. Little's law로 동시에 떠 있는 프레임이 1.6개라는 것도 나온다(8 ms / 5 ms). P=4 ms로 과부하를 걸면 throughput은 200 fps에서 더 오르지 않고 latency만 1000 프레임 동안 957 ms까지 커진다. 이때는 정상 상태가 아니므로 Little's law 숫자는 "지금 큐에 쌓인 양"일 뿐이다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13">직렬 (코어 1개): 한 프레임이 끝나야 다음 프레임 — latency 8 ms, 8 ms마다 1개 = 125 fps</text> <text x="10" y="50" font-size="12">core 0</text> <rect x="110" y="34" width="32" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <rect x="142" y="34" width="80" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/>
<text x="182" y="50" font-size="12" text-anchor="middle">f0</text> <rect x="222" y="34" width="16" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <rect x="238" y="34" width="32" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/>
<rect x="270" y="34" width="80" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <text x="310" y="50" font-size="12" text-anchor="middle">f1</text> <rect x="350" y="34" width="16" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/>
<rect x="366" y="34" width="32" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <rect x="398" y="34" width="80" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <text x="438" y="50" font-size="12" text-anchor="middle">f2</text>
<rect x="478" y="34" width="16" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <rect x="494" y="34" width="32" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <rect x="526" y="34" width="80" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/>
<text x="566" y="50" font-size="12" text-anchor="middle">f3</text> <rect x="606" y="34" width="16" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <text x="10" y="85" font-size="13">파이프라인 (단계마다 코어 1개, 5 ms마다 입력): latency 그대로 8 ms, 5 ms마다 1개 = 200 fps</text> <text x="10" y="120" font-size="12">pre core</text>
<rect x="110" y="104" width="32" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <rect x="190" y="104" width="32" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <rect x="270" y="104" width="32" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/>
<rect x="350" y="104" width="32" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <rect x="430" y="104" width="32" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <rect x="510" y="104" width="32" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/>
<rect x="590" y="104" width="32" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <text x="10" y="150" font-size="12">model core</text> <rect x="142" y="134" width="80" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <text x="182" y="150" font-size="12" text-anchor="middle">f0</text>
<rect x="222" y="134" width="80" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <text x="262" y="150" font-size="12" text-anchor="middle">f1</text> <rect x="302" y="134" width="80" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <text x="342" y="150" font-size="12" text-anchor="middle">f2</text>
<rect x="382" y="134" width="80" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <text x="422" y="150" font-size="12" text-anchor="middle">f3</text> <rect x="462" y="134" width="80" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <text x="502" y="150" font-size="12" text-anchor="middle">f4</text>
<rect x="542" y="134" width="80" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <text x="582" y="150" font-size="12" text-anchor="middle">f5</text> <text x="10" y="180" font-size="12">post core</text> <rect x="222" y="164" width="16" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/>
<rect x="302" y="164" width="16" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <rect x="382" y="164" width="16" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <rect x="462" y="164" width="16" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/>
<rect x="542" y="164" width="16" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor" stroke-width="0.8"/> <line x1="110" y1="205" x2="622" y2="205" stroke="currentColor"/> <line x1="110" y1="205" x2="110" y2="209" stroke="currentColor"/><text x="110" y="222" font-size="12" text-anchor="middle">0</text>
<line x1="174" y1="205" x2="174" y2="209" stroke="currentColor"/><text x="174" y="222" font-size="12" text-anchor="middle">4</text> <line x1="238" y1="205" x2="238" y2="209" stroke="currentColor"/><text x="238" y="222" font-size="12" text-anchor="middle">8</text> <line x1="302" y1="205" x2="302" y2="209" stroke="currentColor"/><text x="302" y="222" font-size="12" text-anchor="middle">12</text>
<line x1="366" y1="205" x2="366" y2="209" stroke="currentColor"/><text x="366" y="222" font-size="12" text-anchor="middle">16</text> <line x1="430" y1="205" x2="430" y2="209" stroke="currentColor"/><text x="430" y="222" font-size="12" text-anchor="middle">20</text> <line x1="494" y1="205" x2="494" y2="209" stroke="currentColor"/><text x="494" y="222" font-size="12" text-anchor="middle">24</text>
<line x1="558" y1="205" x2="558" y2="209" stroke="currentColor"/><text x="558" y="222" font-size="12" text-anchor="middle">28</text> <line x1="622" y1="205" x2="622" y2="209" stroke="currentColor"/><text x="622" y="222" font-size="12" text-anchor="middle">32</text> <text x="622" y="238" font-size="12" text-anchor="end">시간 (ms)</text>
<line x1="190" y1="100" x2="190" y2="198" stroke="#d0564a" stroke-dasharray="4 3"/><line x1="318" y1="100" x2="318" y2="198" stroke="#d0564a" stroke-dasharray="4 3"/> <text x="254" y="100" font-size="12" text-anchor="middle">f1: 도착 5 → 완료 13</text> <text x="10" y="262" font-size="12">파랑 = preprocess 2 ms · 주황 = model 5 ms · 초록 = postprocess 1 ms</text>
<text x="10" y="282" font-size="12">처리량 상한 = 1 / (가장 느린 단계) = 1/5 ms. 입력을 이보다 빨리 넣으면 model 앞에 큐가 쌓여 latency가 계속 늘어난다</text>
</svg>
```

그림 2 — 위 손계산을 시간표로 그렸다. 직렬에서는 코어가 한 프레임을 끝까지 들고 간다. 파이프라인에서는 f1이 model 코어에 있을 때 f2가 pre 코어에 들어온다. f1의 latency(빨간 점선 사이)는 8 ms로 같다.

### 1.5 model-only vs end-to-end

벤더 자료나 논문 표의 "latency 3 ms"는 보통 **model-only**다: 이미 준비된 입력 텐서로 `invoke()` 한 번. 제품에서 중요한 것은 **end-to-end**다.

| 구간 | 무엇 | 흔히 빠지는 이유 |
|---|---|---|
| 입력 수집 | DMA 블록이 찰 때까지, 창이 찰 때까지 | 알고리즘 지연이라 벤치마크에 안 잡힌다 |
| 전처리 | 정규화, FFT·mel, 리샘플링, 이미지 resize | 모델 밖 코드라서 |
| 복사·변환 | numpy → tensor, float → int8 양자화, layout 변환(NCHW↔NHWC), CPU→NPU 버퍼 | 작아 보여서. NPU면 캐시 flush·IOMMU map까지 |
| 모델 | 커널 실행 + 런타임 오버헤드(op dispatch) | — |
| 후처리 | softmax, argmax, NMS, CTC decode, smoothing | "모델이 아니라서" |
| 전달 | IPC·mailbox로 AP에 알림, AP wake-up | 다른 팀 영역이라서 |

3절에서 실제로 나눠 잰다.

---

## 2. 제대로 재기 — Python 벤치마크 하니스

### 2.1 무엇이 측정을 망치나

| 원인 | 증상 | 대책 |
|---|---|---|
| cold start | 첫 호출이 몇 배 느림 (메모리 할당, 커널 선택, 캐시·TLB 비어 있음, lazy init) | warm-up 반복을 버린다. 단 **cold latency는 따로 기록**한다 (기기 wake 후 첫 추론이 제품 경로일 수 있다) |
| 타이머 | 해상도가 커널보다 거침, 벽시계(wall clock)가 NTP로 점프 | 단조(monotonic) 고해상도 타이머: Python `time.perf_counter_ns()`, C `clock_gettime(CLOCK_MONOTONIC)`, MCU는 cycle counter |
| 반복 수 부족 | p99가 매번 다름 | 꼬리 분위수 q를 보려면 `n·(1−q)`가 최소 수십 개 되도록. p99면 수천 번, p99.9면 수만 번 |
| 스레드 수 | 작은 모델이 멀티스레드에서 더 느림, 꼬리 폭증 | 스레드 수를 명시적으로 고정하고 기록 (`torch.set_num_threads`, ORT `intra_op_num_threads`) |
| CPU 주파수·열 | 처음엔 빠르다가 몇 분 뒤 느려짐 (thermal throttling), DVFS가 부하 따라 클럭 변경 | 주파수 고정(Linux `cpufreq` performance governor, 벤더 성능 모드) 또는 **장시간 반복하며 시간축 그래프**를 같이 본다 (K4) |
| 다른 작업 | 드문 큰 spike, 분포가 두 봉우리 | 격리(코어 고정, 다른 프로세스 정지) 또는 **그 조건 자체를 재서** 제품의 현실적 꼬리를 본다 |
| big.LITTLE | 같은 코드가 어떤 코어에 올라가느냐에 따라 2배 차이 | affinity 고정(`taskset`), 어느 클러스터에서 쟀는지 기록 |

### 2.2 하니스 — 공용 모듈 benchlib.py

아래 `benchlib.py`는 이 노트의 Python 예제들이 import 하는 공용 도구다. 핵심은 세 가지: warm-up을 버린다, 반복마다 **따로** 잰다(총 시간 / 횟수가 아니라), 결과를 평균 하나가 아니라 **분위수**로 요약한다. 모델은 B5의 DS-CNN과 비슷한 작은 KWS 모델이다(입력 49 프레임 × 10 MFCC, 파라미터 23,756개, MAC 약 2.66 M — hook으로 센 값).

```python
# benchlib.py — batch=1 latency 벤치마크 하니스 (이 노트의 다른 예제가 import 한다)
import time, numpy as np, torch, torch.nn as nn

def bench(fn, warmup=20, reps=500):
    """fn()을 warmup번 버리고 reps번 재서 ns 배열을 돌려준다."""
    for _ in range(warmup):
        fn()
    ts = np.empty(reps, dtype=np.int64)
    for i in range(reps):
        t0 = time.perf_counter_ns()
        fn()
        ts[i] = time.perf_counter_ns() - t0
    return ts

def summary(ts_ns, name=""):
    us = ts_ns / 1e3
    p50, p90, p99 = np.percentile(us, [50, 90, 99])
    print(f"{name:<14} n={len(us):4d} mean={us.mean():7.1f} p50={p50:7.1f} "
          f"p90={p90:7.1f} p99={p99:7.1f} max={us.max():8.1f}  (us)")
    return dict(mean=us.mean(), p50=p50, p90=p90, p99=p99, max=us.max())

def kws_model():
    """DS-CNN 비슷한 작은 KWS 모델. 입력 [N,1,49,10] (49 프레임 x 10 MFCC)."""
    def dws(c):
        return [nn.Conv2d(c, c, 3, 1, 1, groups=c), nn.BatchNorm2d(c), nn.ReLU(),
                nn.Conv2d(c, c, 1), nn.BatchNorm2d(c), nn.ReLU()]
    torch.manual_seed(0)
    return nn.Sequential(nn.Conv2d(1, 64, (10, 4), (2, 2), (5, 1)), nn.BatchNorm2d(64), nn.ReLU(),
                         *dws(64), *dws(64), *dws(64), *dws(64),
                         nn.AdaptiveAvgPool2d(1), nn.Flatten(), nn.Linear(64, 12)).eval()
```

"총 시간 / 횟수"로 재면 안 되는 이유: 그러면 평균 하나만 남는다. 반복마다 잰 배열이 있어야 p99와 max를 볼 수 있다. 대신 반복마다 타이머 호출 비용이 들어가는데, `perf_counter_ns()`는 수십 ns 수준이라 수백 µs 모델에서는 무시할 만하다. 커널이 수 µs 이하라면 4.1절처럼 타이머 해상도를 먼저 확인한다.

**분위수(percentile)** 복습 (A2): p90은 "측정값의 90%가 이 값 이하"인 값이다. p50은 중앙값이다. 평균과 달리 한두 개의 극단값에 끌려가지 않는다. max는 분위수가 아니라 관측된 최악값이고, 반복 수를 늘릴수록 커지기만 한다.

### 2.3 torch 모델 batch=1

cold start 한 번과 warm-up 후 2000번을 잰다.

```python
# 예제 2: torch 모델 batch=1 latency — 첫 호출(cold) vs warm-up 후 분포
import time, numpy as np, torch
from benchlib import bench, summary, kws_model
torch.set_num_threads(1)                       # 스레드 수 고정 (측정 조건의 일부)
m, x = kws_model(), torch.randn(1, 1, 49, 10)
with torch.inference_mode():
    t0 = time.perf_counter_ns(); m(x); cold = (time.perf_counter_ns() - t0) / 1e3
    ts = bench(lambda: m(x), warmup=20, reps=2000)
print(f"cold first call = {cold:.1f} us")
s = summary(ts, "torch b=1")
np.save("torch_b1.npy", ts)
us = ts / 1e3
print(f"mean/p50 = {s['mean']/s['p50']:.3f}   max/p50 = {s['max']/s['p50']:.1f}")
print("fraction of runs slower than mean:", round(float((us > s['mean']).mean()), 3))
```

```text
cold first call = 628.7 us
torch b=1      n=2000 mean=  321.6 p50=  319.5 p90=  329.2 p99=  354.6 max=   432.1  (us)
mean/p50 = 1.007   max/p50 = 1.4
fraction of runs slower than mean: 0.377
```

출력에서 볼 것: 첫 호출은 629 µs로 정상 상태 p50(320 µs)의 2배다. 이번 실행은 조용했다. p99/p50이 1.1, max/p50이 1.4라서 평균과 중앙값이 거의 같다. 이런 "얌전한" 분포에서는 평균도 쓸 만하다. 문제는 분포가 늘 이렇지 않다는 것이다(2.5절).

참고로 이 모델은 2.66 M MAC을 320 µs에 끝낸다. 약 8.3 GMAC/s다. 4.1절의 C 행렬-벡터 곱은 같은 코어에서 약 44 GMAC/s가 나온다. 차이의 대부분은 작은 레이어 30개를 하나씩 호출하는 **eager 프레임워크 오버헤드**(op마다 Python→C++ dispatch, 출력 텐서 할당)와 depthwise conv의 낮은 연산 밀도(D3·D4)다. batch=1 작은 모델에서는 "연산 시간"보다 "호출 비용"이 클 수 있다. 이것이 MCU에서 TFLite Micro처럼 해석 오버헤드가 작은 런타임이나 AOT 컴파일(모델을 C 코드로 생성)을 쓰는 이유 중 하나다.

### 2.4 ONNX Runtime 세션 batch=1

같은 모델을 ONNX로 내보내 ORT로 잰다. 결과가 같은지 먼저 확인하고(C8의 원칙), 스레드를 1로 고정한다.

```python
# 예제 3: 같은 모델을 ONNX Runtime 세션으로 — 스레드 수 고정, batch=1
import warnings; warnings.filterwarnings("ignore")      # export 경고 숨김
import numpy as np, torch, onnxruntime as ort
from benchlib import bench, summary, kws_model
m, x = kws_model(), torch.randn(1, 1, 49, 10)
torch.onnx.export(m, x, "kws.onnx", input_names=["x"], output_names=["y"],
                  dynamic_axes={"x": {0: "N"}}, dynamo=False)
so = ort.SessionOptions()
so.intra_op_num_threads = 1                    # 연산자 내부 병렬 스레드
so.inter_op_num_threads = 1
sess = ort.InferenceSession("kws.onnx", so, providers=["CPUExecutionProvider"])
xn = x.numpy()
y_ort = sess.run(None, {"x": xn})[0]
with torch.inference_mode(): y_pt = m(x).numpy()
print("max |torch - ort| =", float(np.abs(y_ort - y_pt).max()))
ts = bench(lambda: sess.run(None, {"x": xn}), warmup=20, reps=2000)
summary(ts, "ort b=1 t=1")
np.save("ort_b1.npy", ts)
```

```text
max |torch - ort| = 2.2351741790771484e-08
ort b=1 t=1    n=2000 mean=  204.9 p50=  202.0 p90=  214.0 p99=  233.0 max=   415.5  (us)
```

출력에서 볼 것: 출력 차이는 fp32 반올림 수준(2e-8)이다. 같은 계산인데 p50이 320 → 202 µs로 줄었다. ORT는 세션을 만들 때 그래프 최적화(Conv+BatchNorm 같은 fusion, C6)를 하고, op 하나당 실행 오버헤드가 eager PyTorch보다 작다. **"모델 latency"는 모델 혼자의 속성이 아니라 모델 × 런타임 × 설정 × HW의 속성**이다. 숫자를 보고할 때 이 네 가지를 같이 적는다.

### 2.5 스레드 수와 배경 부하 — 꼬리가 태어나는 곳

이번에는 일부러 조건을 흔든다. 스레드 수 1·2·4·8, 그리고 8개 코어를 모두 점유하는 spin 프로세스 8개를 켠 경우.

```python
# 예제 4: 스레드 수 × 배경 부하(8코어를 도는 spin 프로세스) → p50과 p99가 어떻게 변하나
import multiprocessing as mp, time, numpy as np, torch
from benchlib import bench, kws_model
def spin(stop):
    while not stop.is_set(): pass
def run(threads):
    torch.set_num_threads(threads)
    with torch.inference_mode():
        ts = bench(lambda: m(x), warmup=20, reps=600) / 1e3
    return np.percentile(ts, [50, 99]).tolist() + [ts.max()]
if __name__ == "__main__":
    m, x = kws_model(), torch.randn(1, 1, 49, 10)
    for load in (0, 8):
        stop = mp.Event(); ps = [mp.Process(target=spin, args=(stop,)) for _ in range(load)]
        [p.start() for p in ps]; time.sleep(0.5)
        for t in (1, 2, 4, 8):
            p50, p99, mx = run(t)
            print(f"bg_spin={load}  threads={t}  p50={p50:7.1f}  p99={p99:8.1f}  max={mx:8.1f} us  p99/p50={p99/p50:5.2f}")
        stop.set(); [p.join() for p in ps]
```

```text
bg_spin=0  threads=1  p50=  320.8  p99=   437.1  max=   795.0 us  p99/p50= 1.36
bg_spin=0  threads=2  p50=  479.5  p99=  1522.5  max=  8168.7 us  p99/p50= 3.17
bg_spin=0  threads=4  p50=  498.2  p99=   877.0  max=   949.5 us  p99/p50= 1.76
bg_spin=0  threads=8  p50=  767.8  p99=  1247.3  max=  1496.8 us  p99/p50= 1.62
bg_spin=8  threads=1  p50=  377.1  p99=  3404.3  max=  9024.5 us  p99/p50= 9.03
bg_spin=8  threads=2  p50=  973.1  p99=  4717.1  max= 25421.0 us  p99/p50= 4.85
bg_spin=8  threads=4  p50=  785.8  p99=  1285.6  max=  1934.5 us  p99/p50= 1.64
bg_spin=8  threads=8  p50=  893.9  p99=  1761.6  max=  3752.2 us  p99/p50= 1.97
```

출력에서 볼 것:

- **작은 모델에 스레드를 늘리면 더 느리다.** 조용할 때 1 thread p50 321 µs, 8 threads 768 µs. 레이어 하나가 수 µs짜리라서 스레드를 깨우고 동기화하는 비용이 나눠 얻는 이득보다 크다. batch=1 edge 추론에서 흔한 현상이다.
- **배경 부하는 p50보다 p99를 먼저 망가뜨린다.** 1 thread에서 p50은 321 → 377 µs(+18%)인데 p99는 437 → 3404 µs(7.8배)다. 평균만 보면 "조금 느려졌네"로 보이지만 실시간 시스템에게는 전혀 다른 세상이다.
- 이 숫자들은 실행할 때마다 꽤 바뀐다(특히 threads=2 행). 병렬로 돌던 다른 작업의 타이밍에 따라 달라진다. **그 변동 자체가 꼬리다.** 한 번 잰 p99를 스펙처럼 쓰면 안 되는 이유다.

### 2.6 평균이 숨기는 것 — 3000번의 분포

1 thread, 배경 spin 6개 아래에서 3000번 잰다. 이 분포(`lat_loaded.npy`)는 5절 시뮬레이션의 입력으로 다시 쓴다.

```python
# 예제 5: 배경 spin 프로세스 6개 아래에서 3000번 — 평균이 무엇을 숨기는가
import multiprocessing as mp, time, numpy as np, torch
from benchlib import bench, summary, kws_model
def spin(stop):
    while not stop.is_set(): pass
if __name__ == "__main__":
    torch.set_num_threads(1)
    m, x = kws_model(), torch.randn(1, 1, 49, 10)
    stop = mp.Event(); ps = [mp.Process(target=spin, args=(stop,)) for _ in range(6)]
    [p.start() for p in ps]; time.sleep(0.5)
    with torch.inference_mode(): ts = bench(lambda: m(x), warmup=50, reps=3000)
    stop.set(); [p.join() for p in ps]
    np.save("lat_loaded.npy", ts)
    s = summary(ts, "loaded t=1"); us = ts / 1e3
    print(f"std = {us.std():.1f} us   p99.9 = {np.percentile(us, 99.9):.1f} us")
    for k in (1.5, 2, 5, 10):
        print(f"  runs > {k:>4}x p50 : {int((us > k * s['p50']).sum()):4d} / {len(us)}")
    keep = us[us <= np.percentile(us, 99)]
    print(f"mean after dropping top 1% = {keep.mean():.1f} us  (A/B 비교용으로는 OK, deadline 분석용으로는 금지)")
    print(f"share of total time spent in slowest 1% runs = {np.sort(us)[-30:].sum() / us.sum():.3f}")
    edges = np.array([0, 300, 350, 400, 500, 700, 1000, 2000, 5000, 1e9])
    print("hist:", np.histogram(us, edges)[0].tolist())
```

```text
loaded t=1     n=3000 mean=  459.3 p50=  375.1 p90=  746.6 p99= 1582.9 max=  7618.7  (us)
std = 353.4 us   p99.9 = 5710.3 us
  runs >  1.5x p50 :  361 / 3000
  runs >    2x p50 :  299 / 3000
  runs >    5x p50 :   22 / 3000
  runs >   10x p50 :    7 / 3000
mean after dropping top 1% = 432.8 us  (A/B 비교용으로는 OK, deadline 분석용으로는 금지)
share of total time spent in slowest 1% runs = 0.067
hist: [0, 586, 1331, 703, 41, 258, 64, 12, 5]
```

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="250" x2="620" y2="250" stroke="currentColor"/><line x1="60" y1="250" x2="60" y2="30" stroke="currentColor"/> <rect x="78.7" y="116.9" width="17.7" height="133.1" fill="#4a7bd0" fill-opacity="0.45"/> <rect x="97.3" y="43.3" width="17.7" height="206.7" fill="#4a7bd0" fill-opacity="0.45"/> <rect x="116.0" y="170.6" width="17.7" height="79.4" fill="#4a7bd0" fill-opacity="0.45"/>
<rect x="134.7" y="205.8" width="17.7" height="44.2" fill="#4a7bd0" fill-opacity="0.45"/> <rect x="97.3" y="73.3" width="17.7" height="176.7" fill="#e08a3c" fill-opacity="0.55"/> <rect x="116.0" y="53.6" width="17.7" height="196.4" fill="#e08a3c" fill-opacity="0.55"/> <rect x="134.7" y="71.7" width="17.7" height="178.3" fill="#e08a3c" fill-opacity="0.55"/>
<rect x="153.3" y="133.3" width="17.7" height="116.7" fill="#e08a3c" fill-opacity="0.55"/> <rect x="172.0" y="169.1" width="17.7" height="80.9" fill="#e08a3c" fill-opacity="0.55"/> <rect x="190.7" y="177.5" width="17.7" height="72.5" fill="#e08a3c" fill-opacity="0.55"/> <rect x="209.3" y="186.7" width="17.7" height="63.3" fill="#e08a3c" fill-opacity="0.55"/>
<rect x="228.0" y="123.5" width="17.7" height="126.5" fill="#e08a3c" fill-opacity="0.55"/> <rect x="246.7" y="126.1" width="17.7" height="123.9" fill="#e08a3c" fill-opacity="0.55"/> <rect x="265.3" y="135.3" width="17.7" height="114.7" fill="#e08a3c" fill-opacity="0.55"/> <rect x="284.0" y="169.1" width="17.7" height="80.9" fill="#e08a3c" fill-opacity="0.55"/>
<rect x="302.7" y="172.1" width="17.7" height="77.9" fill="#e08a3c" fill-opacity="0.55"/> <rect x="321.3" y="192.9" width="17.7" height="57.1" fill="#e08a3c" fill-opacity="0.55"/> <rect x="340.0" y="196.5" width="17.7" height="53.5" fill="#e08a3c" fill-opacity="0.55"/> <rect x="358.7" y="205.8" width="17.7" height="44.2" fill="#e08a3c" fill-opacity="0.55"/>
<rect x="377.3" y="192.9" width="17.7" height="57.1" fill="#e08a3c" fill-opacity="0.55"/> <rect x="396.0" y="244.0" width="17.7" height="6.0" fill="#e08a3c" fill-opacity="0.55"/> <rect x="433.3" y="211.9" width="17.7" height="38.1" fill="#e08a3c" fill-opacity="0.55"/> <rect x="452.0" y="231.0" width="17.7" height="19.0" fill="#e08a3c" fill-opacity="0.55"/>
<rect x="470.7" y="231.0" width="17.7" height="19.0" fill="#e08a3c" fill-opacity="0.55"/> <rect x="489.3" y="244.0" width="17.7" height="6.0" fill="#e08a3c" fill-opacity="0.55"/> <rect x="508.0" y="244.0" width="17.7" height="6.0" fill="#e08a3c" fill-opacity="0.55"/> <rect x="526.7" y="244.0" width="17.7" height="6.0" fill="#e08a3c" fill-opacity="0.55"/>
<rect x="545.3" y="244.0" width="17.7" height="6.0" fill="#e08a3c" fill-opacity="0.55"/> <rect x="564.0" y="231.0" width="17.7" height="19.0" fill="#e08a3c" fill-opacity="0.55"/> <rect x="582.7" y="244.0" width="17.7" height="6.0" fill="#e08a3c" fill-opacity="0.55"/> <rect x="601.3" y="244.0" width="17.7" height="6.0" fill="#e08a3c" fill-opacity="0.55"/>
<text x="54" y="248.0" font-size="12" text-anchor="end">1</text><line x1="57" y1="244.0" x2="60" y2="244.0" stroke="currentColor"/> <text x="54" y="190.7" font-size="12" text-anchor="end">10</text><line x1="57" y1="186.7" x2="60" y2="186.7" stroke="currentColor"/> <text x="54" y="127.5" font-size="12" text-anchor="end">100</text><line x1="57" y1="123.5" x2="60" y2="123.5" stroke="currentColor"/>
<text x="54" y="64.2" font-size="12" text-anchor="end">1000</text><line x1="57" y1="60.2" x2="60" y2="60.2" stroke="currentColor"/> <line x1="60.0" y1="250" x2="60.0" y2="254" stroke="currentColor"/><text x="60.0" y="267" font-size="12" text-anchor="middle">250</text> <line x1="172.0" y1="250" x2="172.0" y2="254" stroke="currentColor"/><text x="172.0" y="267" font-size="12" text-anchor="middle">500</text>
<line x1="284.0" y1="250" x2="284.0" y2="254" stroke="currentColor"/><text x="284.0" y="267" font-size="12" text-anchor="middle">1000</text> <line x1="396.0" y1="250" x2="396.0" y2="254" stroke="currentColor"/><text x="396.0" y="267" font-size="12" text-anchor="middle">2000</text>
<line x1="508.0" y1="250" x2="508.0" y2="254" stroke="currentColor"/><text x="508.0" y="267" font-size="12" text-anchor="middle">4000</text> <line x1="620.0" y1="250" x2="620.0" y2="254" stroke="currentColor"/><text x="620.0" y="267" font-size="12" text-anchor="middle">8000</text> <line x1="125.6" y1="30" x2="125.6" y2="250" stroke="#3f9a6b" stroke-dasharray="4 3"/><text x="128.6" y="40" font-size="12">p50 375</text>
<line x1="158.3" y1="30" x2="158.3" y2="250" stroke="currentColor" stroke-dasharray="4 3"/><text x="161.3" y="54" font-size="12">mean 459</text> <line x1="358.2" y1="30" x2="358.2" y2="250" stroke="#d0564a" stroke-dasharray="4 3"/><text x="361.2" y="40" font-size="12">p99 1583</text>
<line x1="612.1" y1="30" x2="612.1" y2="250" stroke="#d0564a" stroke-dasharray="4 3"/><text x="615.1" y="40" font-size="12">max 7619</text> <text x="340.0" y="284" font-size="12" text-anchor="middle">latency (µs, log 축)</text> <text x="14" y="140.0" font-size="12" transform="rotate(-90 14 140.0)" text-anchor="middle">횟수 (log 축)</text>
<text x="60" y="318" font-size="12">파랑 = 조용할 때 2000회 (예제 2) · 주황 = 배경 spin 6개 아래 3000회 (예제 5) · 점선은 주황 분포의 통계</text>
</svg>
```

그림 3 — 실제 측정한 latency 히스토그램(두 축 모두 log). 파랑은 조용할 때(예제 2), 주황은 배경 부하 아래(예제 5). 주황은 375 µs 근처 주봉우리 말고 700~900 µs 근처에 **두 번째 봉우리**가 있고, 그 오른쪽으로 7.6 ms까지 긴 꼬리가 이어진다.

출력과 그림에서 볼 것:

- mean 459 µs는 p50 375 µs보다 22% 크다. 꼬리가 평균을 끌어올린다. 그런데 mean은 p99(1583 µs)나 max(7619 µs)에 대해서는 **아무것도 알려 주지 않는다**. "평균 0.46 ms"라는 보고서만 받은 사람은 가끔 7.6 ms가 걸린다는 것을 상상할 수 없다.
- 표준편차(353 µs)도 도움이 안 된다. 정규분포라면 mean + 3σ ≈ 1.5 ms 밖이 0.13%여야 하는데, 실제로는 p99가 1.58 ms이고 max는 7.6 ms다. latency 분포는 **정규분포가 아니다**. 오른쪽으로 긴 꼬리(right-skewed, heavy tail)를 가진다. 0 아래로는 못 가고 위로는 끝없이 갈 수 있기 때문이다.
- 두 번째 봉우리(약 2배 느린 무리)는 M2의 P-core가 다른 작업에 뺏겨 스레드가 **E-core(효율 코어)로 옮겨졌을 때**로 추정한다(확인하지는 않았다). 모바일 SoC의 big.LITTLE에서도 똑같은 모양이 나온다. 분포가 두 봉우리면 "평균"은 어느 쪽도 대표하지 않는다.
- **이상치(outlier) 처리 원칙**: 두 커널 A/B를 비교할 때는 꼬리를 잘라도(top 1% 제거, 또는 p50·p10 비교) 된다. 방해 요인이 두 쪽에 공평하게 들어간다고 볼 수 있기 때문이다(C7이 p10을 쓴 이유). 그러나 **deadline 분석에서는 절대 자르지 않는다.** 잘라낸 1%가 바로 deadline을 깨는 프레임이다.

### 2.7 임베디드 연결

Don이 SSD에서 쓰던 QoS 표(99%, 99.9%, 99.99% latency)가 그대로 모델 latency 보고 형식이 된다. 추가로 기록할 것: 런타임·버전, 스레드 수, 코어(클러스터), 주파수 정책, 입력 shape, 정밀도(fp32/int8), cold/warm, 반복 수. 업계 표준 벤치마크인 MLPerf Inference도 시나리오를 나눈다: **SingleStream**(batch=1, 하나 끝나면 다음 — 90th percentile latency를 보고), **Offline**(모든 입력을 한꺼번에 — throughput을 보고) 등. 같은 모델도 시나리오에 따라 전혀 다른 숫자가 나온다는 것을 표준이 인정한 것이다.

---

## 3. End-to-end 분해 — 모델만 재면 절반만 잰 것

1초 오디오에서 MFCC `[49, 10]`을 numpy로 만들고(B5의 방식을 줄인 것), 모델을 돌리고, softmax·argmax까지 한다. 단계별로 따로 재고, 전체를 한 번에도 잰다. 스트리밍에서는 매번 49 프레임을 다시 계산할 필요가 없으므로 **새 프레임 1개만** 계산하는 비용도 잰다.

```python
# 예제 6: end-to-end vs model-only — 1초 오디오 → MFCC [49,10] → 모델 → softmax/argmax
import numpy as np, torch
from benchlib import bench, kws_model
torch.set_num_threads(1)
FS, WIN, HOP, NFFT = 16000, 640, 320, 1024              # 40 ms 창, 20 ms hop → 49 프레임
f = np.linspace(0, FS / 2, NFFT // 2 + 1); mel = lambda h: 2595 * np.log10(1 + h / 700)
pts = np.interp(np.linspace(mel(20), mel(FS / 2), 42), mel(f), f)          # 40개 삼각 필터의 꼭짓점 (Hz)
fb = np.stack([np.clip(np.minimum((f - a) / (b - a), (c - f) / (c - b)), 0, None)
               for a, b, c in zip(pts[:-2], pts[1:-1], pts[2:])]).astype(np.float32)   # [40, 513]
n = np.arange(40); dct = np.cos(np.pi / 40 * (n + 0.5)[None, :] * np.arange(10)[:, None]).astype(np.float32)
hann = np.hanning(WIN).astype(np.float32)
audio = np.random.default_rng(0).normal(0, 0.1, FS).astype(np.float32)
def features(a):                                        # [49, 10]
    idx = np.arange(49)[:, None] * HOP + np.arange(WIN)[None, :]
    spec = np.abs(np.fft.rfft(a[idx] * hann, NFFT)) ** 2
    return (np.log(spec @ fb.T + 1e-6) @ dct.T).astype(np.float32)
def frame1(w):                                          # 새 프레임 하나만 (스트리밍 증분 계산)
    return np.log((np.abs(np.fft.rfft(w * hann, NFFT)) ** 2) @ fb.T + 1e-6) @ dct.T
m = kws_model(); feats = features(audio); xt = torch.from_numpy(feats)[None, None]
with torch.inference_mode():
    logits = m(xt)
    stages = {"features(49 frames)": lambda: features(audio),
              "feature 1 frame": lambda: frame1(audio[-WIN:]),
              "numpy->tensor": lambda: torch.from_numpy(feats).reshape(1, 1, 49, 10),
              "model": lambda: m(xt),
              "softmax+argmax": lambda: int(logits.softmax(-1).argmax()),
              "END-TO-END": lambda: int(m(torch.from_numpy(features(audio))[None, None]).softmax(-1).argmax())}
    for k, fn in stages.items():
        us = bench(fn, warmup=20, reps=1000) / 1e3
        print(f"{k:20s} p50 = {np.percentile(us, 50):7.1f} us   p99 = {np.percentile(us, 99):7.1f} us")
```

```text
features(49 frames)  p50 =   265.4 us   p99 =   369.1 us
feature 1 frame      p50 =    22.8 us   p99 =    45.5 us
numpy->tensor        p50 =     1.0 us   p99 =     4.5 us
model                p50 =   324.2 us   p99 =   442.4 us
softmax+argmax       p50 =     1.8 us   p99 =     2.5 us
END-TO-END           p50 =   602.1 us   p99 =   717.8 us
```

출력에서 볼 것:

- 전처리(49 프레임 feature)가 265 µs로 **모델(324 µs)과 거의 같다.** "모델 latency 0.32 ms"라고 보고하면 실제 비용의 절반을 빼먹는 것이다. 작은 KWS·IMU 모델에서는 전처리가 모델보다 비싼 경우도 흔하다.
- end-to-end p50(602 µs)은 단계별 p50의 합(265 + 1 + 324 + 2 = 592 µs)과 비슷하다. 그러나 **p99는 더하면 안 된다.** 각 단계의 p99가 동시에 일어나는 경우는 드물어서 합(369 + 4.5 + 442 + 2.5 = 818 µs)은 실제 end-to-end p99(718 µs)보다 크다. 보수적 상한으로는 쓸 수 있다. 정확한 꼬리는 end-to-end를 직접 재야 한다.
- 스트리밍으로 바꾸면 feature는 프레임당 23 µs로 줄어든다(49 프레임 재계산의 약 1/12). 100 ms마다 판정하는 구조라면 그 사이 5 프레임만 새로 계산하면 된다. **"매번 창 전체를 다시 계산하지 말고 증분(incremental)으로"** 는 B3의 stateful RNN, B5의 링버퍼와 같은 발상이다.
- 같은 코드를 처음 실행했을 때는 `features p99 = 1095 µs`, `model p99 = 1231 µs`, `END-TO-END p99 = 2542 µs`가 나왔다(p50은 거의 같았다). 병렬 작업이 몰린 순간이었다. p50은 재현되는데 p99는 재현되지 않는다 — 2.5절의 교훈이 반복된다.

---

## 4. C 하니스 — 호스트에서, 그리고 MCU에서

### 4.1 호스트(Linux/macOS) — `clock_gettime(CLOCK_MONOTONIC)`

모델 전체 대신 커널 하나(int8 행렬-벡터 곱, 즉 FC 레이어 하나: 256 × 1024 = 262,144 MAC)를 C로 잰다. Python 하니스와 같은 원칙: warm-up, 반복마다 측정, 정렬 후 분위수. 컴파일러가 결과를 안 쓰는 루프를 지워 버리지 않도록 `volatile` sink에 결과를 쓴다.

```c
/* bench.c — int8 matvec 커널의 latency 분포를 CLOCK_MONOTONIC으로 잰다 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#ifndef ROWS
#define ROWS 256                                 /* -DROWS=4096 으로 일을 16배 늘려 본다 */
#endif
#define COLS 1024
#define REPS 5000
static int8_t W[ROWS][COLS], X[COLS];
static int32_t Y[ROWS];
static volatile int32_t sink;                    /* 결과를 써서 컴파일러가 루프를 지우지 못하게 */

static void matvec_s8(void) {                    /* Y = W·X, int32 누산 (FC 레이어 하나) */
    for (int r = 0; r < ROWS; r++) {
        int32_t acc = 0;
        for (int c = 0; c < COLS; c++) acc += (int32_t)W[r][c] * X[c];
        Y[r] = acc;
    }
    sink = Y[ROWS - 1];
}
static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}
static uint64_t pct(const uint64_t *s, int n, double p) {  /* nearest-rank percentile */
    int k = (int)(p / 100.0 * n + 0.999999); if (k < 1) k = 1; if (k > n) k = n;
    return s[k - 1];
}
int main(void) {
    static uint64_t t[REPS];
    struct timespec res; clock_getres(CLOCK_MONOTONIC, &res);
    srand(1);
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) W[r][c] = (int8_t)(rand() % 255 - 127);
    for (int c = 0; c < COLS; c++) X[c] = (int8_t)(rand() % 255 - 127);
    uint64_t a = now_ns(), b = now_ns();         /* 타이머 자체 비용 */
    for (int i = 0; i < 100; i++) matvec_s8();    /* warm-up: 캐시·분기예측·주파수 */
    for (int i = 0; i < REPS; i++) { uint64_t t0 = now_ns(); matvec_s8(); t[i] = now_ns() - t0; }
    double sum = 0; for (int i = 0; i < REPS; i++) sum += (double)t[i];
    qsort(t, REPS, sizeof t[0], cmp_u64);
    printf("clock res = %ld ns, back-to-back now_ns() = %llu ns, MACs/call = %d\n",
           res.tv_nsec, (unsigned long long)(b - a), ROWS * COLS);
    printf("n=%d mean=%.2f p50=%.2f p90=%.2f p99=%.2f p99.9=%.2f max=%.2f (us)\n", REPS, sum / REPS / 1e3,
           pct(t, REPS, 50) / 1e3, pct(t, REPS, 90) / 1e3, pct(t, REPS, 99) / 1e3,
           pct(t, REPS, 99.9) / 1e3, t[REPS - 1] / 1e3);
    printf("Y[0]=%d  GMAC/s at p50 = %.2f\n", (int)Y[0], ROWS * COLS / (double)pct(t, REPS, 50));
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 bench.c -o bench && ./bench
cc -std=c11 -Wall -Wextra -O2 -DROWS=4096 bench.c -o bench4k && ./bench4k
```

```text
clock res = 1000 ns, back-to-back now_ns() = 0 ns, MACs/call = 262144
n=5000 mean=5.87 p50=6.00 p90=7.00 p99=8.00 p99.9=9.00 max=53.00 (us)
Y[0]=17210  GMAC/s at p50 = 43.69
clock res = 1000 ns, back-to-back now_ns() = 0 ns, MACs/call = 4194304
n=5000 mean=89.89 p50=82.00 p90=104.00 p99=192.00 p99.9=272.00 max=517.00 (us)
Y[0]=-15952  GMAC/s at p50 = 51.15
```

출력에서 볼 것:

- **macOS의 `CLOCK_MONOTONIC` 해상도는 1 µs**(1000 ns)다. 그래서 첫 실행의 분위수가 전부 정수 µs(6, 7, 8)로 나오고, 같은 시각을 두 번 읽으면 0 ns다. 커널이 6 µs면 해상도 오차가 최대 약 17%다. Linux의 vDSO `clock_gettime`은 보통 ns 해상도지만 **반드시 `clock_getres`로 확인**한다. 대책은 커널을 크게 만들거나(두 번째 실행: 4096행, 82 µs) 더 고운 카운터(PMU cycle counter)를 쓰는 것이다.
- `-O2`만으로 약 44~51 GMAC/s가 나왔다. clang이 내부 루프를 SIMD로 자동 벡터화했기 때문이다(E2의 NEON). 같은 코어에서 2.3절의 torch 모델이 8.3 GMAC/s였던 것과 비교하면 batch=1 프레임워크 오버헤드가 얼마나 큰지 보인다.
- 4096행(가중치 4 MB)에서는 p99/p50이 2.3배로 벌어졌다. 데이터가 L1을 크게 넘어 L2·메모리 쪽 변동과 다른 작업의 캐시 간섭을 더 받는다(추정). 입력이 커지면 꼬리 모양도 바뀐다.

### 4.2 MCU — DWT CYCCNT (예시 코드, 이 노트에서는 컴파일하지 않음)

Cortex-M3/M4/M7/M33/M55 등 Armv7-M·Armv8-M Mainline 코어에는 디버그 블록 DWT(Data Watchpoint and Trace) 안에 32비트 **cycle counter `CYCCNT`**가 있다. CPU 클럭마다 1씩 증가하므로 해상도는 1 cycle(96 MHz면 약 10.4 ns)이다. Cortex-M0/M0+(Armv6-M)에는 CYCCNT가 없어서 SysTick이나 하드웨어 타이머를 쓴다.

레지스터는 일반적으로 이렇다(주소는 Arm 아키텍처 표준 위치. CMSIS 헤더를 쓰면 `CoreDebug->DEMCR`(v8-M에서는 `DCB->DEMCR`), `DWT->CTRL`, `DWT->CYCCNT` 이름으로 접근한다).

| 레지스터 | 주소 | 할 일 |
|---|---|---|
| DEMCR | `0xE000EDFC` | bit 24 `TRCENA` = 1 → DWT·ITM 블록 전원/활성화 |
| DWT_CTRL | `0xE0001000` | bit 0 `CYCCNTENA` = 1 → 카운터 시작 |
| DWT_CYCCNT | `0xE0001004` | 현재 cycle 수 (쓰기로 0 초기화 가능) |
| DWT_LAR | `0xE0001FB0` | 일부 구현(예: Cortex-M7)은 `0xC5ACCE55`를 써서 잠금 해제해야 함 |

```c
/* 예시 (illustrative) — 타깃 보드용. 이 노트에서는 컴파일·실행하지 않았다. */
#include <stdint.h>
#define DEMCR      (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)
#define DWT_LAR    (*(volatile uint32_t *)0xE0001FB0u)

static inline void cyc_init(void) {
    DEMCR |= (1u << 24);          /* TRCENA */
    DWT_LAR = 0xC5ACCE55u;        /* 필요한 코어에서만 의미 있음 */
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1u;               /* CYCCNTENA */
}
/* 사용: 32비트 wrap-around도 unsigned 뺄셈이면 한 번까지는 정확하다
   (96 MHz에서 2^32 cycle ≈ 44.7 s) */
uint32_t t0 = DWT_CYCCNT;
run_inference();                   /* 예: TFLM interpreter.Invoke() */
uint32_t cycles = DWT_CYCCNT - t0;
hist_add(&h, cycles / (SystemCoreClock / 1000000u), 10000u);   /* µs로 바꿔 4.3의 히스토그램에 */
```

타깃에서 잴 때의 체크리스트 (Don에게 익숙한 것들이지만 ML 추론에 맞춰 다시):

- 인터럽트를 막고 재면 **순수 계산 시간(WCET 후보)**, 켜고 재면 **실제 응답 시간**이다. 둘 다 필요하다. deadline 분석에는 후자를 쓴다.
- flash wait state, I-cache·D-cache 켜짐 여부, 가중치가 flash(XIP)에 있는지 SRAM/TCM에 있는지가 latency를 몇 배 바꾼다. 측정표에 반드시 적는다.
- GPIO를 추론 시작에 올리고 끝에 내려서 로직 분석기로 보는 방법은 CPU 부담 없이 **분포와 주기**를 동시에 보여 준다. ISR 진입, DMA 완료, 추론 구간을 채널별로 나누면 5·7절의 시간표가 오실로스코프 화면에 그대로 나온다.
- Linux·Android 쪽이면 `perf stat`/`simpleperf`로 cycle·cache miss를, 벤더 프로파일러(QNN profiler, Snapdragon Profiler)로 op별 시간을 본다(K2).

### 4.3 MCU용 히스토그램 — 샘플을 저장하지 않고 분위수 추정

MCU에는 3000개 샘플 배열을 둘 RAM이 아깝다. 고정 폭 bucket에 개수만 센다. 필드에서도 켜 둘 수 있을 만큼 작다(telemetry로 올려 보내기 좋다 — Don의 SSD telemetry 경험 그대로). 아래는 호스트에서 가짜 측정값으로 검증한 코드다: 평소 4.8~5.2 ms, 3% 확률로 +1.5~3 ms(ISR 폭주 가정), 0.2% 확률로 +6~10 ms(flash 쓰기와 겹친 경우 가정).

```c
/* hist.c — MCU용 latency 히스토그램: 샘플을 저장하지 않고 고정 bucket에 세기만 한다 (RAM 몇백 B) */
#include <stdio.h>
#include <stdint.h>

#define BUCKET_US 250u                       /* bucket 폭 0.25 ms */
#define NBUCKET   64u                        /* 0 ~ 16 ms, 마지막 칸은 overflow */
typedef struct { uint32_t cnt[NBUCKET]; uint32_t n, max_us, over_budget; uint64_t sum_us; } lat_hist_t;

static void hist_add(lat_hist_t *h, uint32_t us, uint32_t budget_us) {
    uint32_t b = us / BUCKET_US; if (b >= NBUCKET) b = NBUCKET - 1;
    h->cnt[b]++; h->n++; h->sum_us += us;
    if (us > h->max_us) h->max_us = us;
    if (us > budget_us) h->over_budget++;
}
static uint32_t hist_pm_us(const lat_hist_t *h, uint32_t permille) {  /* 천분위, bucket 상한으로 보수적 추정 */
    uint32_t need = (uint32_t)(((uint64_t)h->n * permille + 999) / 1000), acc = 0;
    for (uint32_t b = 0; b < NBUCKET; b++) { acc += h->cnt[b]; if (acc >= need) return (b + 1) * BUCKET_US; }
    return h->max_us;
}
static uint32_t xs = 12345u;                 /* xorshift32: 재현 가능한 가짜 측정값 */
static uint32_t rnd(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }

int main(void) {
    static lat_hist_t h;                     /* 0으로 초기화 (.bss) */
    const uint32_t budget = 10000;           /* 10 ms frame budget */
    for (int i = 0; i < 100000; i++) {
        uint32_t us = 4800 + rnd() % 400;                 /* 평소: 4.8 ~ 5.2 ms */
        if (rnd() % 100 < 3)  us += 1500 + rnd() % 1500;  /* 3%: ISR 폭주·캐시 miss로 +1.5~3 ms */
        if (rnd() % 1000 < 2) us += 6000 + rnd() % 4000;  /* 0.2%: flash 쓰기 등과 겹쳐 +6~10 ms */
        hist_add(&h, us, budget);
    }
    printf("n=%u  mean=%.0f us  p50<=%u  p99<=%u  p99.9<=%u  max=%u us\n", h.n, (double)h.sum_us / h.n,
           hist_pm_us(&h, 500), hist_pm_us(&h, 990), hist_pm_us(&h, 999), h.max_us);
    printf("over 10 ms budget: %u (%.3f%%)   RAM for histogram = %zu bytes\n",
           h.over_budget, 100.0 * h.over_budget / h.n, sizeof h);
    return 0;
}
```

```text
n=100000  mean=5085 us  p50<=5250  p99<=7750  p99.9<=13250  max=16669 us
over 10 ms budget: 192 (0.192%)   RAM for histogram = 280 bytes
```

출력에서 볼 것: **평균 5.1 ms, 예산 10 ms — 안전해 보이지만** 0.192%가 예산을 넘었다. 100 fps(10 ms hop)면 초당 0.19번, 즉 **약 5초에 한 번** deadline miss다. 오디오라면 5초마다 클릭 잡음이나 끊김이 들린다. bucket 폭(250 µs)만큼 분위수가 위로 반올림되는 대신 RAM은 280 B뿐이다.

---

## 5. 실시간 스트리밍 — deadline, jitter, 링버퍼

### 5.1 정의 — 프레임 주기가 곧 deadline

| 센서 | 샘플레이트 | hop(프레임 주기) | 한 프레임 샘플 수 | 96 MHz MCU의 프레임당 cycle |
|---|---|---|---|---|
| 오디오 (KWS·VAD) | 16 kHz | 10 ms | 160 | 960,000 |
| 오디오 (20 ms hop) | 16 kHz | 20 ms | 320 | 1,920,000 |
| IMU (활동·제스처) | 100 Hz | 10 ms (샘플당) | 1 | 960,000 |
| IMU 창 판정 | 100 Hz | 창 2 s, hop 0.5 s | 50 | 48,000,000 |

- **frame period(hop)** `T`: 새 데이터 블록이 도착하는 주기. 10 ms hop = 16,000 × 0.010 = 160 샘플.
- **deadline** `D`: 그 블록 처리를 끝내야 하는 시각. ping-pong DMA(B5 9.1절)면 `D = T`다. 다음 블록이 같은 버퍼를 덮어쓰기 전에 끝내야 하기 때문이다.
- **compute budget**: `D` 안에서 추론에 쓸 수 있는 시간. 다른 일(오디오 코덱, BLE, 센서 드라이버)을 빼고 남는 몫이다. 예: 10 ms 중 feature 1 ms, 기타 태스크 2 ms, 여유 margin 30% → 모델 몫은 약 `10 × 0.7 − 3 = 4 ms`.
- **utilization** `U = C / T`: 평균 계산 시간 ÷ 주기. U가 1 미만이어야 평균적으로 따라간다. 그러나 **U가 1 미만인 것은 필요조건일 뿐**, deadline을 지킨다는 보장이 아니다.
- **jitter**: 주기적 사건의 시각이 흔들리는 정도. 여기서는 (1) 결과가 나오는 시각의 흔들림(output jitter), (2) 계산 시간의 흔들림을 말한다. 오디오 재생이나 제어 루프에서는 평균 latency보다 jitter가 더 문제일 때가 많다.
- **deadline miss rate**: deadline을 넘긴 프레임 비율. **drop(overrun) rate**: 버퍼가 꽉 차서 아예 버려진 프레임 비율.

### 5.2 링버퍼 깊이는 꼬리를 흡수한다 — 대신 latency를 낸다

ping-pong(슬롯 1개를 처리하는 동안 다른 1개를 DMA가 채움)보다 슬롯을 늘리면 가끔 긴 프레임이 와도 뒤 프레임들이 버퍼에서 기다릴 수 있다. 대신 기다린 만큼 결과가 늦게 나온다. 얼마나 늘려야 하나? **측정한 분포로 시뮬레이션하면 된다.**

설정: 10 ms마다 프레임 도착. 처리 시간은 2.6절에서 실제로 잰 분포(부하 걸린 Mac)를 **모양은 그대로 두고 중앙값만** 2~6 ms로 스케일해서 프레임마다 무작위로 뽑는다. 슬롯 수(depth, DMA가 채우는 슬롯 제외)가 꽉 차 있으면 새 프레임은 버린다. 10만 프레임(1000초).

```python
# 예제 8: 10 ms hop 스트리밍 루프 — 실측 latency 분포(예제 5)를 스케일해서 프레임마다 샘플링
from collections import deque
import numpy as np
HOP = 10.0                                              # ms, 프레임 주기
base = np.load("lat_loaded.npy") / 1e6                  # 예제 5 실측 (ms), 부하 걸린 Mac의 분포 모양
def stream(p50_ms, depth, n=100_000, seed=0):
    rng = np.random.default_rng(seed)
    c = rng.choice(base, n) * (p50_ms / np.median(base))  # 모양 유지, 중앙값만 p50_ms로 스케일
    fin, last, drops, lat = deque(), 0.0, 0, []        # fin = 슬롯을 차지한 프레임들의 완료 시각
    for i in range(n):
        t = i * HOP                                     # DMA가 프레임 i를 넘겨주는 시각
        while fin and fin[0] <= t: fin.popleft()        # 끝난 프레임은 슬롯 반납
        if len(fin) >= depth: drops += 1; continue      # 슬롯 없음 → overrun, 프레임 버림
        last = max(t, last) + c[i]; fin.append(last)    # FIFO로 하나씩 처리
        lat.append(last - t)                            # 도착 → 결과까지
    lat = np.array(lat)
    return drops / n, (lat > HOP).mean(), np.percentile(lat, 99), c.mean()
print("p50 | mean_c | util | depth=1 drop  miss>10ms |  depth=2 drop | depth=4 drop  p99_lat | depth=16 drop p99_lat")
for p50 in (2, 3, 4, 5, 6):
    r = {d: stream(p50, d) for d in (1, 2, 4, 16)}
    m = r[1][3]
    print(f"{p50:3d} | {m:6.2f} | {m / HOP:4.2f} | {r[1][0]:11.4%} {r[1][1]:9.4%} | {r[2][0]:12.4%} |"
          f" {r[4][0]:11.4%} {r[4][2]:6.1f} | {r[16][0]:12.4%} {r[16][2]:6.1f}")
base = np.load("torch_b1.npy") / 1e6                    # 비교: 부하 없는 조용한 분포 (예제 2, max/p50 ≈ 1.4)
for p50 in (5, 6, 7):
    print(f"quiet shape, p50={p50} ms, depth=1: drop = {stream(p50, 1)[0]:.4%}, p99 latency = {stream(p50, 1)[2]:.1f} ms")
```

```text
p50 | mean_c | util | depth=1 drop  miss>10ms |  depth=2 drop | depth=4 drop  p99_lat | depth=16 drop p99_lat
  2 |   2.45 | 0.25 |     1.1710%   0.7700% |      0.4120% |     0.0390%   12.7 |      0.0000%   13.2
  3 |   3.68 | 0.37 |     2.5900%   1.5871% |      1.0640% |     0.2870%   24.1 |      0.0000%   26.1
  4 |   4.90 | 0.49 |     5.4760%   3.9577% |      1.8240% |     0.6810%   34.8 |      0.0000%   44.3
  5 |   6.13 | 0.61 |    11.3970%   9.9319% |      2.8670% |     1.1770%   46.7 |      0.0040%   67.1
  6 |   7.35 | 0.74 |    13.6710%  11.5616% |      4.3050% |     1.9470%   59.9 |      0.0360%   97.0
quiet shape, p50=5 ms, depth=1: drop = 0.0000%, p99 latency = 5.5 ms
quiet shape, p50=6 ms, depth=1: drop = 0.0000%, p99 latency = 6.7 ms
quiet shape, p50=7 ms, depth=1: drop = 0.0000%, p99 latency = 7.8 ms
```

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="250" x2="400" y2="250" stroke="currentColor"/><line x1="70" y1="250" x2="70" y2="30" stroke="currentColor"/> <line x1="67" y1="30.0" x2="400" y2="30.0" stroke="#888" stroke-opacity="0.3"/><text x="64" y="34.0" font-size="12" text-anchor="end">100%</text> <line x1="67" y1="74.0" x2="400" y2="74.0" stroke="#888" stroke-opacity="0.3"/><text x="64" y="78.0" font-size="12" text-anchor="end">10%</text>
<line x1="67" y1="118.0" x2="400" y2="118.0" stroke="#888" stroke-opacity="0.3"/><text x="64" y="122.0" font-size="12" text-anchor="end">1%</text> <line x1="67" y1="162.0" x2="400" y2="162.0" stroke="#888" stroke-opacity="0.3"/><text x="64" y="166.0" font-size="12" text-anchor="end">0.1%</text>
<line x1="67" y1="206.0" x2="400" y2="206.0" stroke="#888" stroke-opacity="0.3"/><text x="64" y="210.0" font-size="12" text-anchor="end">0.01%</text> <line x1="67" y1="250.0" x2="400" y2="250.0" stroke="#888" stroke-opacity="0.3"/><text x="64" y="254.0" font-size="12" text-anchor="end">≤0.001%</text> <text x="70.0" y="267" font-size="12" text-anchor="middle">1</text>
<text x="152.5" y="267" font-size="12" text-anchor="middle">2</text> <text x="235.0" y="267" font-size="12" text-anchor="middle">4</text> <text x="317.5" y="267" font-size="12" text-anchor="middle">8</text> <text x="400.0" y="267" font-size="12" text-anchor="middle">16</text> <text x="235.0" y="284" font-size="12" text-anchor="middle">링버퍼 깊이 (프레임 슬롯, log2 축)</text>
<text x="70" y="20" font-size="13">drop(overrun) 비율</text> <polyline fill="none" stroke="#4a7bd0" stroke-width="2" points="70.0,115.0 152.5,134.9 200.8,151.9 235.0,180.0 283.3,250.0 317.5,250.0 365.8,250.0 400.0,250.0"/> <circle cx="70.0" cy="115.0" r="3" fill="#4a7bd0"/> <circle cx="152.5" cy="134.9" r="3" fill="#4a7bd0"/> <circle cx="200.8" cy="151.9" r="3" fill="#4a7bd0"/>
<circle cx="235.0" cy="180.0" r="3" fill="#4a7bd0"/> <circle cx="283.3" cy="250.0" r="3" fill="#4a7bd0"/> <circle cx="317.5" cy="250.0" r="3" fill="#4a7bd0"/> <circle cx="365.8" cy="250.0" r="3" fill="#4a7bd0"/> <circle cx="400.0" cy="250.0" r="3" fill="#4a7bd0"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="2" points="70.0,85.5 152.5,106.5 200.8,116.6 235.0,125.3 283.3,145.1 317.5,172.1 365.8,223.5 400.0,250.0"/> <circle cx="70.0" cy="85.5" r="3" fill="#e08a3c"/> <circle cx="152.5" cy="106.5" r="3" fill="#e08a3c"/> <circle cx="200.8" cy="116.6" r="3" fill="#e08a3c"/> <circle cx="235.0" cy="125.3" r="3" fill="#e08a3c"/>
<circle cx="283.3" cy="145.1" r="3" fill="#e08a3c"/> <circle cx="317.5" cy="172.1" r="3" fill="#e08a3c"/> <circle cx="365.8" cy="223.5" r="3" fill="#e08a3c"/> <circle cx="400.0" cy="250.0" r="3" fill="#e08a3c"/> <polyline fill="none" stroke="#d0564a" stroke-width="2" points="70.0,68.0 152.5,90.1 200.8,99.0 235.0,105.3 283.3,116.5 317.5,127.3 365.8,156.1 400.0,181.5"/> <circle cx="70.0" cy="68.0" r="3" fill="#d0564a"/>
<circle cx="152.5" cy="90.1" r="3" fill="#d0564a"/> <circle cx="200.8" cy="99.0" r="3" fill="#d0564a"/> <circle cx="235.0" cy="105.3" r="3" fill="#d0564a"/> <circle cx="283.3" cy="116.5" r="3" fill="#d0564a"/> <circle cx="317.5" cy="127.3" r="3" fill="#d0564a"/> <circle cx="365.8" cy="156.1" r="3" fill="#d0564a"/> <circle cx="400.0" cy="181.5" r="3" fill="#d0564a"/>
<line x1="460" y1="250" x2="660" y2="250" stroke="currentColor"/><line x1="460" y1="250" x2="460" y2="30" stroke="currentColor"/> <text x="454" y="254.0" font-size="12" text-anchor="end">0</text> <text x="454" y="199.0" font-size="12" text-anchor="end">25</text> <text x="454" y="144.0" font-size="12" text-anchor="end">50</text> <text x="454" y="89.0" font-size="12" text-anchor="end">75</text>
<text x="454" y="34.0" font-size="12" text-anchor="end">100</text> <text x="460.0" y="267" font-size="12" text-anchor="middle">1</text> <text x="510.0" y="267" font-size="12" text-anchor="middle">2</text> <text x="560.0" y="267" font-size="12" text-anchor="middle">4</text> <text x="610.0" y="267" font-size="12" text-anchor="middle">8</text> <text x="660.0" y="267" font-size="12" text-anchor="middle">16</text>
<text x="460" y="20" font-size="13">p99 latency (ms)</text> <line x1="460" y1="228.0" x2="660" y2="228.0" stroke="currentColor" stroke-dasharray="4 3"/><text x="660" y="224.0" font-size="12" text-anchor="end">hop 10 ms</text> <polyline fill="none" stroke="#4a7bd0" stroke-width="2" points="460.0,230.3 510.0,227.5 539.2,225.4 560.0,222.1 589.2,220.9 610.0,220.9 639.2,220.9 660.0,220.9"/>
<polyline fill="none" stroke="#e08a3c" stroke-width="2" points="460.0,210.6 510.0,198.5 539.2,185.7 560.0,173.5 589.2,160.6 610.0,154.7 639.2,152.8 660.0,152.6"/> <polyline fill="none" stroke="#d0564a" stroke-width="2" points="460.0,190.3 510.0,161.8 539.2,141.6 560.0,118.2 589.2,81.9 610.0,67.0 639.2,46.1 660.0,36.5"/>
<text x="70" y="300" font-size="12">파랑 p50=2 ms · 주황 p50=4 ms · 빨강 p50=6 ms (예제 5 분포 모양, 10만 프레임). 왼쪽: drop, 오른쪽: 도착→결과 p99</text>
</svg>
```

그림 4 — 예제 8과 같은 시뮬레이션을 depth 1~16으로 촘촘히 돌린 결과(같은 seed). 왼쪽: 깊이를 늘리면 drop이 지수적으로 준다. 오른쪽: 대신 도착→결과 p99 latency가 hop(10 ms)의 몇 배로 늘어난다.

출력과 그림에서 볼 것:

- **utilization 25%인데도 1.2%를 버린다.** p50 = 2 ms, 평균 2.45 ms로 10 ms 예산의 1/4밖에 안 쓰는데 depth 1(ping-pong)에서 1.17%의 프레임이 사라진다. 꼬리(p50의 5~20배짜리 드문 실행)가 10 ms를 넘기 때문이다.
- 같은 p50이라도 **분포 모양이 조용하면**(마지막 세 줄, max/p50 ≈ 1.4) p50 = 7 ms, utilization 70%에서도 drop 0이다. 결정하는 것은 평균이 아니라 **꼬리의 모양**이다.
- 깊이 16이면 p50 = 4 ms까지 drop이 0이 되지만 p99 latency가 44 ms가 된다. KWS처럼 수백 ms 알고리즘 지연이 이미 있는 기능에는 괜찮지만, 음성 통화 잡음 제거처럼 **입출력이 이어지는 경로**(end-to-end 10~20 ms 수준 요구)에는 받아들일 수 없다. 버퍼는 공짜가 아니다. 드롭과 latency를 교환하는 손잡이다.
- 방법의 한계: 여기서는 프레임마다 독립적으로 뽑았다(i.i.d.). 실제 꼬리는 **몰려서** 온다(다른 태스크가 바쁜 구간에 연달아 느림). 몰리면 같은 깊이에서도 drop이 더 크다. 그래서 가능하면 타깃에서 **연속 측정한 시계열**을 그대로 재생(replay)해서 시뮬레이션한다.

### 5.3 측정 함정 — coordinated omission

예제 2~5처럼 "끝나면 바로 다음 호출"하는 벤치마크(closed loop)는 느린 호출이 있으면 그 동안 **다음 요청을 보내지 않는다**. 그래서 실제 시스템이라면 그 사이 쌓였을 대기 시간이 측정에서 빠진다. 이 현상을 Gil Tene는 **coordinated omission**이라고 불렀다. 스트리밍 시스템은 입력이 벽시계에 맞춰 주기적으로 들어오므로(open loop), latency를 **예정 도착 시각부터** 재야 한다. 예제 8은 그렇게 쟀다(`lat = 완료 − t`, `t = i × HOP`). 타깃에서도 DMA 완료 인터럽트 시각에 타임스탬프를 찍고 결과 시각과 비교한다.

### 5.4 임베디드 연결

- ping-pong은 depth 1이다. 긴 추론을 오디오 ISR 경로에서 직접 돌리면 안 되는 이유가 이 표에 있다.
- 두 계층으로 나누는 것이 표준이다: **샘플 수집 링버퍼는 넉넉하게**(수백 ms, 오디오 16 kHz × 16 bit면 100 ms = 3.2 KB), **feature는 프레임마다 즉시**, **모델은 N 프레임마다 별도 태스크**. 모델이 늦으면 모델 입력만 늦어지고 샘플은 잃지 않는다.
- 늦었을 때의 정책을 미리 정한다: 오래된 프레임을 버리고 최신만 처리(latest-wins — 제스처·UI처럼 "지금"이 중요한 경우), 전부 처리하며 따라잡기(ASR처럼 빠진 샘플이 치명적인 경우), 모델을 건너뛰고 이전 결과 유지(frame skipping). RNN(B3)은 프레임을 건너뛰면 상태가 틀어지므로 따라잡기가 필요하다.

---

## 6. 알고리즘 지연 vs 계산 지연

### 6.1 직관

- **계산 지연(compute latency)**: 입력이 다 준비된 시점부터 결과까지. 빠른 칩, 양자화, 커널 최적화로 줄인다.
- **알고리즘 지연(algorithmic latency)**: 입력이 다 준비될 때까지 기다려야 하는 시간. 파이프라인 **설계**가 정한다. 칩이 무한히 빨라도 줄지 않는다.

알고리즘 지연을 만드는 것들:

- **창 길이**: 1초 창으로 판단하는 KWS는 판정 순간 1초 과거를 본다. 호출어 시작 기준으로는 적어도 "호출어 길이"만큼 지나야 판정할 수 있고, 학습 때 호출어를 창 가운데 두고 잘랐다면 창 끝이 호출어 끝을 **여유분만큼 더** 지나야 확률이 높아진다(사실상 lookahead).
- **hop**: 100 ms마다 판정하면, 판정 가능한 순간이 오기까지 평균 50 ms, 최대 100 ms를 기다린다(격자 정렬 대기).
- **non-causal 모델**: bidirectional RNN(B3 7.2절), 미래 프레임을 보는 conv padding, 전체 시퀀스 attention은 **lookahead** L 프레임을 요구한다. L × hop이 그대로 지연이다. 스트리밍 모델은 causal(과거만 보는) 구조를 쓰거나 lookahead를 수십~수백 ms로 제한한다.
- **smoothing**: N개 창 평균(B5 3.4절)은 threshold를 넘기까지 창 여러 개를 기다린다.
- **debounce·hysteresis**(B7 7.3절): "K번 연속 확인"은 (K−1) × hop을 더한다.
- **DMA 블록**: 블록이 다 차야 CPU가 본다. 10 ms 블록이면 최대 10 ms.

### 6.2 손계산 — 설정 A

1초 창, hop 100 ms, 3개 이동평균 smoothing, threshold 0.7, debounce 없음, DMA 10 ms, 계산 6 ms. 모델은 이상적이라고 하자: 창이 호출어를 다 품으면 확률 1, 아니면 0.

```
호출어 끝 = 1030 ms
호출어를 다 품은 첫 창의 끝 = ceil(1030 / 100) × 100 = 1100 ms          → 격자 대기 70 ms
3개 평균 ≥ 0.7 이려면 1이 ceil(0.7 × 3) = 3개 필요 → 창 1100, 1200, 1300  → smoothing 200 ms
1300 ms 창의 마지막 샘플이 DMA 블록으로 도착                             → +10 ms
추론                                                                     → +6 ms
trigger = 1316 ms → 호출어 끝 기준 286 ms,  호출어 시작(300 ms) 기준 1016 ms
```

말로 하면: 계산은 6 ms뿐인데 사용자가 말을 끝낸 뒤 286 ms가 지나야 반응한다. 그중 270 ms(격자 70 + smoothing 200)가 알고리즘 지연이다. 모델을 2배 빠르게 만들면 3 ms를 벌고, smoothing을 3개 → 2개로 줄이면 100 ms를 번다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13">설정 A: 1 s 창 · hop 100 ms · 3개 평균 ≥ 0.7 → trigger</text> <text x="10" y="50" font-size="12">음성</text> <rect x="180" y="38" width="292" height="18" fill="#3f9a6b" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/> <text x="326" y="51" font-size="12" text-anchor="middle">호출어 0.30 → 1.03 s</text> <text x="10" y="80" font-size="12">창</text>
<rect x="100" y="68" width="400" height="10" fill="#4a7bd0" fill-opacity="0.35"/> <rect x="140" y="84" width="400" height="10" fill="#4a7bd0" fill-opacity="0.35"/> <rect x="180" y="100" width="400" height="10" fill="#4a7bd0" fill-opacity="0.35"/> <text x="504" y="77" font-size="12">끝 1.1 s, p=1, 평균 0.33</text> <text x="544" y="93" font-size="12">1.2 s, 평균 0.67</text>
<text x="584" y="109" font-size="12">1.3 s, 1.0</text> <line x1="60" y1="140" x2="620" y2="140" stroke="currentColor"/> <line x1="60" y1="135" x2="60" y2="145" stroke="currentColor"/><line x1="100" y1="135" x2="100" y2="145" stroke="currentColor"/><line x1="140" y1="135" x2="140" y2="145" stroke="currentColor"/>
<line x1="180" y1="135" x2="180" y2="145" stroke="currentColor"/><line x1="220" y1="135" x2="220" y2="145" stroke="currentColor"/><line x1="260" y1="135" x2="260" y2="145" stroke="currentColor"/> <line x1="300" y1="135" x2="300" y2="145" stroke="currentColor"/><line x1="340" y1="135" x2="340" y2="145" stroke="currentColor"/><line x1="380" y1="135" x2="380" y2="145" stroke="currentColor"/>
<line x1="420" y1="135" x2="420" y2="145" stroke="currentColor"/><line x1="460" y1="135" x2="460" y2="145" stroke="currentColor"/><line x1="500" y1="135" x2="500" y2="145" stroke="currentColor"/> <line x1="540" y1="135" x2="540" y2="145" stroke="currentColor"/><line x1="580" y1="135" x2="580" y2="145" stroke="currentColor"/><line x1="620" y1="135" x2="620" y2="145" stroke="currentColor"/>
<text x="60" y="160" font-size="12" text-anchor="middle">0</text><text x="260" y="160" font-size="12" text-anchor="middle">0.5</text> <text x="460" y="160" font-size="12" text-anchor="middle">1.0</text><text x="620" y="160" font-size="12" text-anchor="middle">1.4 s</text> <text x="80" y="175" font-size="12">눈금 = 창 끝(판정 시각), 100 ms 간격</text>
<line x1="472" y1="30" x2="472" y2="250" stroke="#3f9a6b" stroke-dasharray="4 3"/> <line x1="586" y1="118" x2="586" y2="250" stroke="#d0564a" stroke-width="2"/> <text x="590" y="130" font-size="12">trigger 1.316 s</text> <line x1="472" y1="200" x2="586" y2="200" stroke="#e08a3c" stroke-width="2"/> <line x1="472" y1="194" x2="472" y2="206" stroke="#e08a3c"/><line x1="586" y1="194" x2="586" y2="206" stroke="#e08a3c"/>
<text x="529" y="194" font-size="12" text-anchor="middle">286 ms</text> <line x1="180" y1="232" x2="586" y2="232" stroke="currentColor" stroke-width="1.5"/> <line x1="180" y1="226" x2="180" y2="238" stroke="currentColor"/> <text x="383" y="226" font-size="12" text-anchor="middle">호출어 시작부터 1016 ms</text> <text x="10" y="270" font-size="12">286 ms = 격자 대기 70 + smoothing 200 (알고리즘) + DMA 10 + 계산 6</text>
<text x="10" y="290" font-size="12">초록 점선 = 호출어 끝 · 빨강 = trigger · 모델 계산 6 ms는 이 그림 축척에서 2.4 px다</text>
</svg>
```

그림 5 — 설정 A의 시간표(축척 1 ms = 0.4 px). 창 셋이 호출어를 차례로 품고, 셋째 창에서 평균이 0.7을 넘는다. 계산 시간은 그림에서 거의 보이지 않는다.

### 6.3 코드로 확인 — 여러 설정의 검출 지연 분포

호출어 끝 시각을 hop 격자에 대해 무작위로 놓고 2만 번 시뮬레이션한다. 계산 시간은 대부분 6 ms, 가끔 15·25 ms로 꼬리를 줬다.

```python
# 예제 9: wake word 검출 latency = 알고리즘 지연 + 계산 지연 — keyword 끝 시각을 무작위로 놓고 시뮬레이션
import numpy as np, math
rng = np.random.default_rng(0)
def detect_latency(hop, n_smooth, thr, debounce, dma_ms, compute_ms, lookahead_ms=0, trials=20000):
    """keyword 끝 → trigger 까지 (ms). posterior는 이상적인 0/1: 창 끝이 keyword 끝(+lookahead)을 지나면 1."""
    need_high = math.ceil(thr * n_smooth)              # N개 평균이 thr을 넘으려면 필요한 '1' 창 수
    out = []
    for _ in range(trials):
        kw_end = 1000 + rng.uniform(0, hop)             # 창 격자(hop 간격)에 대해 무작위 위치
        first = math.ceil((kw_end + lookahead_ms) / hop) * hop   # keyword를 다 품은 첫 창의 끝 시각
        high_k = first + (need_high - 1) * hop          # smoothing 평균이 thr을 넘는 창
        fire = high_k + (debounce - 1) * hop            # debounce: 연속 K창 확인
        t = fire + dma_ms + compute_ms(rng)             # 창 끝 샘플이 DMA 블록으로 도착 + 추론 시간
        out.append(t - kw_end)
    return np.percentile(out, [0, 50, 99, 100])
c = lambda r: r.choice([6.0] * 98 + [15.0, 25.0])      # 계산 시간: 대부분 6 ms, 가끔 15·25 ms (tail)
cfgs = {"A hop100 smooth3 thr0.7 deb1": (100, 3, 0.7, 1, 10, c),
        "B hop100 smooth1 deb1       ": (100, 1, 0.5, 1, 10, c),
        "C hop20  smooth5 thr0.7 deb2": (20, 5, 0.7, 2, 10, c),
        "D = C + model lookahead 200 ": (20, 5, 0.7, 2, 10, c, 200)}
for k, a in cfgs.items():
    mn, p50, p99, mx = detect_latency(*a)
    print(f"{k}  min={mn:6.1f}  p50={p50:6.1f}  p99={p99:6.1f}  max={mx:6.1f} ms")
```

```text
A hop100 smooth3 thr0.7 deb1  min= 216.0  p50= 266.3  p99= 315.3  max= 333.7 ms
B hop100 smooth1 deb1         min=  16.0  p50=  66.5  p99= 115.4  max= 134.9 ms
C hop20  smooth5 thr0.7 deb2  min=  96.0  p50= 106.2  p99= 119.5  max= 134.9 ms
D = C + model lookahead 200   min= 296.0  p50= 306.2  p99= 321.3  max= 334.9 ms
```

출력에서 볼 것:

- A의 min 216 ms = 격자 대기 0 + smoothing 200 + DMA 10 + 계산 6. 손계산과 맞는다. p50 266 ms는 격자 대기 평균 50 ms가 더해진 값이다.
- B(smoothing 없음)는 p50 67 ms로 200 ms 빠르다. 대신 한 창짜리 spike에 바로 반응하므로 false alarm이 는다(B5 3.4절). **지연과 오경보는 같은 손잡이의 양쪽이다.**
- C는 hop을 20 ms로 줄이고 smoothing 5개 + debounce 2를 넣었는데도 p50 106 ms다. 창을 5배 자주 돌리므로 **계산량(전력)은 5배**다. 알고리즘 지연을 줄이는 대가를 계산 쪽 예산에서 치른다. 이 교환을 숫자로 보여 주는 것이 embedded AI 엔지니어의 일이다.
- D는 C에 non-causal 모델(200 ms lookahead)을 넣은 것이다. 계산은 그대로인데 p50이 306 ms가 됐다. **lookahead는 그대로 latency다.**
- 계산 꼬리(25 ms)는 max를 약 20 ms 늘릴 뿐이다. 이 기능에서는 알고리즘 지연이 지배한다. 반대로 5절의 deadline 문제는 계산 꼬리가 지배한다. 둘을 구분해서 말해야 한다.

### 6.4 흔한 함정

- "모델을 int8로 바꿔서 3 ms 빨라졌으니 wake 반응이 빨라진다" — 총 지연 286 ms 중 1%다. 사용자는 느끼지 못한다. 그 3 ms는 **전력**(D7) 이야기로 가져가야 한다.
- 학습 데이터에서 호출어를 창 끝쪽에 두고 자르면(정렬 편향) 스트리밍에서 창 끝이 호출어 끝을 지나자마자 반응하는 모델이 된다. 반대로 가운데 정렬이면 lookahead가 생긴다. **데이터 정렬이 latency를 정한다.** 모델 팀과 이야기할 지점이다.

---

## 7. 기기 위 스케줄링 — ISR·DMA·feature·inference

### 7.1 역할 분담 (Don에게 익숙한 구조)

| 층 | 우선순위 | 할 일 | 시간 제약 |
|---|---|---|---|
| DMA 완료 ISR | 최고 | 버퍼 인덱스 교체, 태스크에 알림(semaphore·event flag) | 수 µs. 절대 계산하지 않는다 |
| audio/feature 태스크 | 높음 | 링버퍼에 넣기, FFT·mel 1 프레임 | 프레임마다, deadline = hop |
| inference 태스크 | 중간~낮음 | N 프레임마다 모델 실행, 결과 큐에 | 판정 주기(예: 100 ms) 안 |
| 후처리·통신 | 낮음 | smoothing, 이벤트 판단, AP에 IPC | 기능 latency 예산 안 |

**double buffering**(B5 9.1절)은 ISR과 feature 태스크 사이의 계약이다: feature 태스크가 hop 안에 한 블록을 끝내기만 하면 샘플은 절대 잃지 않는다. inference는 이 계약에 끼어들면 안 된다.

### 7.2 손계산 — 선점 스케줄링으로 되나?

feature F: 주기 10 ms, 계산 1 ms. inference I: 주기 100 ms, 계산 25 ms. 한 코어.

```
U = 1/10 + 25/100 = 0.35
Rate-monotonic(주기가 짧을수록 높은 우선순위, 선점)의 충분조건 (Liu & Layland, 1973):
  U ≤ n·(2^(1/n) − 1),  n = 2 → 0.828
0.35 ≤ 0.828 → 선점 RTOS면 두 태스크 모두 deadline을 지킨다.
비선점(superloop, run-to-completion)이면: F가 I 뒤에 걸리면 최대 25 ms를 기다린다 → 10 ms deadline miss.
```

말로 하면: 이용률은 35%뿐이라 CPU는 충분하다. 문제는 **긴 비선점 구간(blocking)** 이다. F의 최악 응답 시간 ≈ 자기 계산 + 가장 긴 비선점 구간.

### 7.3 코드로 확인 — 세 가지 방식

0.1 ms tick으로 10초를 시뮬레이션한다. 방식: (1) superloop에서 inference를 한 번에 끝까지, (2) inference를 5 ms·9.5 ms 조각(chunk)으로 쪼개 조각 사이에 F를 끼움, (3) 선점 우선순위(F가 I보다 높음).

```python
# 예제 11: 한 코어에서 feature task(10 ms마다 1 ms) + inference(100 ms마다 25 ms) — 스케줄링 방식 비교
import math
TICK = 0.1                                              # ms, 시뮬레이션 해상도
def run(policy, chunk_ms=25.0, T_end=10_000):
    F = dict(period=10, C=1.0, rel=[], resp=[]); I = dict(period=100, C=25.0, rel=[], resp=[])
    fq, iq = [], []                                     # 대기 중인 job: [release 시각, 남은 일]
    running, slice_left = None, 0.0                    # 비선점 방식에서 지금 잡고 있는 job
    for k in range(int(T_end / TICK)):
        t = k * TICK
        if k % int(F["period"] / TICK) == 0: fq.append([t, F["C"]])
        if k % int(I["period"] / TICK) == 0: iq.append([t, I["C"]])
        if policy == "preemptive":                      # 매 tick 가장 높은 우선순위(F)부터
            job, q = (fq[0], fq) if fq else ((iq[0], iq) if iq else (None, None))
        else:                                           # 비선점: 잡은 조각이 끝날 때까지 놓지 않음
            if running is None or slice_left <= 1e-9:
                running = (fq, fq[0]) if fq else ((iq, iq[0]) if iq else None)
                slice_left = (F["C"] if running and running[0] is fq else chunk_ms) if running else 0
            q, job = running if running else (None, None)
        if job is None: continue
        job[1] -= TICK; slice_left -= TICK
        if job[1] <= 1e-9:
            q.pop(0); (F if q is fq else I)["resp"].append(t + TICK - job[0]); slice_left = 0
    fmiss = sum(r > F["period"] + 1e-9 for r in F["resp"])
    print(f"{policy:24s} F: max resp {max(F['resp']):5.1f} ms, deadline(10 ms) miss {fmiss:4d}/{len(F['resp'])}"
          f"  |  I: max resp {max(I['resp']):5.1f} ms")
run("superloop run-to-complete", 25.0)
run("superloop chunk 5 ms", 5.0)
run("superloop chunk 9.5 ms", 9.5)
run("preemptive")
```

```text
superloop run-to-complete F: max resp  17.0 ms, deadline(10 ms) miss  100/1000  |  I: max resp  26.0 ms
superloop chunk 5 ms     F: max resp   3.0 ms, deadline(10 ms) miss    0/1000  |  I: max resp  28.0 ms
superloop chunk 9.5 ms   F: max resp   2.0 ms, deadline(10 ms) miss    0/1000  |  I: max resp  28.0 ms
preemptive               F: max resp   1.0 ms, deadline(10 ms) miss    0/1000  |  I: max resp  28.0 ms
```

```svg
<svg viewBox="0 0 660 270" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="46" font-size="12">run-to-complete</text> <rect x="150" y="30" width="16" height="22" fill="#4a7bd0" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/> <rect x="166" y="30" width="400" height="22" fill="#e08a3c" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/> <text x="366.0" y="46" font-size="12" text-anchor="middle">inference</text>
<rect x="566" y="30" width="16" height="22" fill="#4a7bd0" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/> <rect x="582" y="30" width="16" height="22" fill="#4a7bd0" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/> <text x="10" y="96" font-size="12">chunk 5 ms</text> <rect x="150" y="80" width="16" height="22" fill="#4a7bd0" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/>
<rect x="166" y="80" width="80" height="22" fill="#e08a3c" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/> <text x="206.0" y="96" font-size="12" text-anchor="middle">inference</text> <rect x="246" y="80" width="80" height="22" fill="#e08a3c" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/> <text x="286.0" y="96" font-size="12" text-anchor="middle">inference</text>
<rect x="326" y="80" width="16" height="22" fill="#4a7bd0" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/> <rect x="342" y="80" width="80" height="22" fill="#e08a3c" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/> <text x="382.0" y="96" font-size="12" text-anchor="middle">inference</text>
<rect x="422" y="80" width="80" height="22" fill="#e08a3c" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/> <text x="462.0" y="96" font-size="12" text-anchor="middle">inference</text> <rect x="502" y="80" width="16" height="22" fill="#4a7bd0" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/>
<rect x="518" y="80" width="80" height="22" fill="#e08a3c" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/> <text x="558.0" y="96" font-size="12" text-anchor="middle">inference</text> <text x="10" y="146" font-size="12">preemptive F>I</text> <rect x="150" y="130" width="16" height="22" fill="#4a7bd0" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/>
<rect x="166" y="130" width="144" height="22" fill="#e08a3c" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/> <text x="238.0" y="146" font-size="12" text-anchor="middle">inference</text> <rect x="310" y="130" width="16" height="22" fill="#4a7bd0" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/>
<rect x="326" y="130" width="144" height="22" fill="#e08a3c" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/> <text x="398.0" y="146" font-size="12" text-anchor="middle">inference</text> <rect x="470" y="130" width="16" height="22" fill="#4a7bd0" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/>
<rect x="486" y="130" width="112" height="22" fill="#e08a3c" fill-opacity="0.4" stroke="currentColor" stroke-width="0.8"/> <text x="542.0" y="146" font-size="12" text-anchor="middle">inference</text> <line x1="310" y1="22" x2="310" y2="190" stroke="#d0564a" stroke-dasharray="4 3"/> <line x1="470" y1="22" x2="470" y2="190" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="655" y="18" font-size="12" text-anchor="end">F(10) 완료 27 → 17 ms, miss</text> <line x1="150" y1="190" x2="630" y2="190" stroke="currentColor"/> <line x1="150" y1="190" x2="150" y2="194" stroke="currentColor"/><text x="150" y="207" font-size="12" text-anchor="middle">0</text> <line x1="230" y1="190" x2="230" y2="194" stroke="currentColor"/><text x="230" y="207" font-size="12" text-anchor="middle">5</text>
<line x1="310" y1="190" x2="310" y2="194" stroke="currentColor"/><text x="310" y="207" font-size="12" text-anchor="middle">10</text> <line x1="390" y1="190" x2="390" y2="194" stroke="currentColor"/><text x="390" y="207" font-size="12" text-anchor="middle">15</text> <line x1="470" y1="190" x2="470" y2="194" stroke="currentColor"/><text x="470" y="207" font-size="12" text-anchor="middle">20</text>
<line x1="550" y1="190" x2="550" y2="194" stroke="currentColor"/><text x="550" y="207" font-size="12" text-anchor="middle">25</text> <line x1="630" y1="190" x2="630" y2="194" stroke="currentColor"/><text x="630" y="207" font-size="12" text-anchor="middle">30</text> <text x="630" y="222" font-size="12" text-anchor="end">시간 (ms)</text>
<text x="10" y="244" font-size="12">파랑 = feature task F (10 ms마다 1 ms, deadline 10 ms) · 주황 = inference I (100 ms마다 25 ms)</text> <text x="10" y="262" font-size="12">빨간 점선 = F 릴리스(DMA 블록 도착). 예제 11의 첫 30 ms를 그대로 그렸다</text>
</svg>
```

그림 6 — 예제 11의 첫 30 ms. run-to-complete에서는 10 ms에 도착한 F가 inference 뒤에서 26 ms까지 기다린다(응답 17 ms, miss). 조각내기나 선점에서는 F가 10 ms 안에 끝나고, inference는 끼어든 F 두 개만큼(2 ms) 늦게 끝난다.

출력에서 볼 것:

- run-to-complete는 inference 한 번마다 F 하나가 deadline을 놓친다: 1000개 중 100개(10%). ping-pong이면 그때마다 오디오 블록 하나가 덮어써진다.
- 조각 크기가 hop보다 충분히 작으면 비선점으로도 해결된다(9.5 ms 조각도 이 예에서는 통과했지만 F의 계산 1 ms와 합쳐 10.5 ms가 될 수 있는 위치가 있으면 깨진다 — 조각 + F ≤ hop이 안전한 규칙이다).
- 세 방식 모두 inference의 응답은 26~28 ms로 비슷하다. **F를 지켜 주는 대가는 inference의 약간의 지연**이다. 판정 주기 100 ms 안이니 문제없다.

### 7.4 inference를 조각내는 방법

- **레이어 단위 실행**: 런타임이 op·레이어 하나씩 실행하는 API를 주면 조각 사이에 yield한다. 표준 TFLite Micro의 `Invoke()`는 모델 전체를 한 번에 돈다. 조각이 필요하면 모델을 여러 개의 작은 모델(subgraph)로 나눠 중간 텐서를 넘기거나, 벤더 런타임의 기능을 확인하거나, 그냥 RTOS 선점에 맡긴다.
- **시간 방향 조각**: 스트리밍 모델(causal conv, RNN — B3)은 원래 프레임마다 조금씩 계산한다. 1초 창을 100 ms마다 통째로 다시 돌리는 대신 프레임마다 1/N만 계산하면 부하가 **평평해져서** 꼬리가 줄고 조각내기 문제가 사라진다. 스트리밍 설계가 스케줄링 설계이기도 한 이유다.
- **공간 방향 조각**: 이미지 모델은 타일(patch) 단위로(C7의 MCUNetV2 patch 기반 추론) 나눌 수 있다.

### 7.5 선점과 priority inversion (짧게)

선점 RTOS에서는 공유 자원이 문제다. inference 태스크(낮음)가 I2C 버스나 공유 텐서 arena의 mutex를 잡은 채로 선점당하고, 중간 우선순위 태스크(BLE 등)가 오래 돌면, 그 mutex를 기다리는 높은 우선순위 feature 태스크가 **중간 태스크 때문에** 막힌다. 1997년 Mars Pathfinder의 리셋 사례로 유명한 현상이다. 대책은 Don이 아는 그대로: priority inheritance mutex, 임계 구역 짧게, 높은 우선순위 태스크와 inference가 같은 자원을 공유하지 않게 설계(버퍼 소유권을 넘기는 큐 사용).

ML 특유의 함정: **tensor arena(TFLM의 작업 메모리)를 여러 태스크가 공유**하면 추론 중 다른 태스크가 arena를 쓸 수 없다. 모델마다 arena를 따로 두거나, 추론을 한 태스크에서만 직렬로 돌린다.

### 7.6 멀티코어·가속기 오프로드 — 비동기 완료

DSP·NPU로 오프로드하면 CPU는 기다리는 동안 다른 일을 할 수 있다. 패턴은 DMA와 같다: 작업 제출 → 완료 인터럽트 → 결과 수거.

```c
/* 예시 (illustrative) — 가상의 NPU 드라이버 API. 컴파일하지 않았다. */
void inference_task(void *arg) {
    for (;;) {
        wait_event(&feat_ready);                 /* feature 태스크가 N 프레임을 채우면 */
        npu_job_t *job = npu_submit(model, in_buf, out_buf);   /* 비동기 제출, 즉시 반환 */
        /* 이 사이 CPU는 feature·BLE 등 다른 태스크를 계속 돈다 */
        if (sem_take(&job->done, TIMEOUT_MS(30)) != OK) {      /* 완료 IRQ가 semaphore를 준다 */
            npu_reset(); stats.npu_timeout++;    /* 타임아웃도 설계한다: 결과 없음 처리 */
            continue;
        }
        postprocess(out_buf);                    /* 캐시 invalidate 후 읽기 (coherence!) */
    }
}
```

오프로드에서 latency가 새는 곳: 제출 비용(드라이버·IPC, 수십~수백 µs), 캐시 flush·invalidate, NPU가 지원하지 않는 op의 **CPU fallback**(F8 — 그래프가 NPU→CPU→NPU로 쪼개지며 왕복 복사), 다른 클라이언트와 NPU 공유 시 큐 대기, 첫 실행의 컨텍스트 로드(cold). 오프로드 latency도 **분포로** 잰다. 특히 AP 쪽 NPU는 OS 스케줄링·DVFS 때문에 꼬리가 길다.

---

## 8. 사용자 기능의 latency 예산

### 8.1 사람이 느끼는 기준

잘 알려진 경험칙: 반응이 약 **0.1초** 안이면 "즉시"로 느끼고, **1초** 안이면 흐름이 끊기지 않으며, 그 이상이면 기다린다고 느낀다(Nielsen의 응답 시간 한계). 대화에서 사람끼리의 말 차례 사이 간격은 언어와 상관없이 **약 200 ms** 근처에 몰려 있다는 연구가 있다(Stivers et al., 2009). 음성 비서가 1~2초씩 조용하면 어색하게 느껴지는 이유다.

### 8.2 기능별 목표 (typical — 업계 감각치, 제품·출처마다 다르다)

| 기능 | 측정 구간 | 목표 (typical) | 지배 요인 |
|---|---|---|---|
| wake word 반응 | 호출어 끝 → chime·LED | 약 200–500 ms | 창·smoothing(알고리즘) |
| 음성 비서 한 턴 | 사용자 말 끝 → 응답 음성 시작 | 약 0.5–1.5 s | endpointing 대기 + ASR + LLM 첫 토큰(TTFT, D5) + TTS 첫 청크 |
| raise-to-wake (손목 들어 화면 켜기) | 동작 완료 → 화면 켜짐 | 약 100–300 ms | IMU 창 + 디스플레이 켜기 |
| 탭·제스처 피드백 | 동작 → 햅틱·소리 | 약 100 ms 이하 | 창 길이, debounce |
| 착용 감지 | 착용/탈착 → 상태 변경 | 약 0.5–2 s | debounce(오감지 방지가 우선) |
| 실시간 잡음 제거·통화 | 마이크 → 스피커/송신 | 약 10–30 ms (알고리즘 지연 포함) | 프레임·lookahead |

### 8.3 예산 나누기 — 음성 비서 한 턴 예시 (가정 숫자)

목표 1000 ms (말 끝 → 응답 음성 시작)라고 하자.

| 단계 | 할당 | 설명 |
|---|---|---|
| endpointing (말 끝 판정) | 300 ms | VAD가 "조용함"을 이만큼 봐야 말이 끝났다고 판단 — 순수 알고리즘 지연. 너무 줄이면 말 중간에 끊는다 |
| ASR 확정 | 100 ms | streaming ASR이면 대부분 이미 끝나 있고 마지막 조각만 |
| LLM 첫 토큰 (TTFT) | 350 ms | on-device SLM의 prefill 또는 네트워크 왕복 + 서버 (D5·L 모듈) |
| TTS 첫 오디오 청크 | 150 ms | 스트리밍 TTS면 첫 청크만 기다린다 |
| 오디오 출력 버퍼·코덱 | 50 ms | 재생 경로의 버퍼 깊이 |
| 여유 (margin) | 50 ms | 꼬리 흡수 |

할당 원칙:

- 알고리즘 지연을 먼저 적는다. 칩이 빨라도 안 줄어드는 부분이 얼마인지 알아야 계산 쪽에 몇 ms가 남는지 안다.
- 각 단계 예산은 **p50이 아니라 p95·p99 기준**으로 잡는다. 단계 p99를 그냥 더하면 보수적(과대)이고, p50을 더하면 낙관적이다. 가능하면 end-to-end를 직접 재서 확인한다(3절).
- **"먼저 반응하고 나중에 완성"** 이 사용자 체감을 크게 바꾼다: 호출어를 인식하면 즉시 chime(수백 ms), LLM 답이 늦으면 "음…" 같은 짧은 filler나 첫 문장 스트리밍. 이는 기능 설계로 알고리즘 지연을 가리는 방법이다.

---

## 9. Throughput도 중요하다 — batching

### 9.1 언제 throughput을 보나

on-device 실시간 경로는 거의 batch=1이다. 그러나 embedded AI 팀도 throughput을 본다:

- **데이터 수집·처리 파이프라인**(H 모듈): 기기에서 올라온 수천 시간의 센서 로그에 모델을 돌려 라벨 후보를 뽑거나 오경보를 찾을 때. 이때 목표는 "하룻밤 안에 전부"다.
- **서버 쪽 추론**: hybrid edge-LLM(L 모듈)에서 서버가 여러 기기의 요청을 모아(dynamic batching) 처리할 때. 이때는 throughput과 요청당 latency p99를 동시에 본다.
- **기기 안의 batch**: 여러 마이크 채널, 여러 후보(beam), 여러 창을 한 번에 돌리는 경우. 작지만 실제로 있다.

### 9.2 코드로 확인 — batch 크기 스윕

```python
# 예제 10: 오프라인 처리량 — batch 크기에 따른 inferences/s와 호출당 latency (+ 커널 선택 절벽)
import numpy as np, torch
from benchlib import bench, kws_model
m = kws_model()
def sweep(label, threads, nnpack):
    torch.set_num_threads(threads); torch.backends.nnpack.set_flags(nnpack)
    print(f"--- {label}: threads={threads}, nnpack={'on (default)' if nnpack else 'off'}")
    with torch.inference_mode():
        for B in (1, 4, 8, 16, 32, 64, 128, 256):
            x = torch.randn(B, 1, 49, 10)
            ms = bench(lambda: m(x), warmup=5, reps=max(20, 2000 // B)) / 1e6
            p50 = np.percentile(ms, 50)
            print(f"B={B:4d}  call p50 = {p50:7.2f} ms   per-sample = {p50 / B * 1e3:6.1f} us"
                  f"   throughput = {B / p50 * 1e3:6.0f} inf/s")
sweep("S1", 1, True)
sweep("S2", 1, False)
sweep("S3", 4, False)
```

```text
--- S1: threads=1, nnpack=on (default)
B=   1  call p50 =    0.28 ms   per-sample =  281.3 us   throughput =   3555 inf/s
B=   4  call p50 =    0.73 ms   per-sample =  183.5 us   throughput =   5448 inf/s
B=   8  call p50 =    1.27 ms   per-sample =  158.8 us   throughput =   6297 inf/s
B=  16  call p50 =    9.99 ms   per-sample =  624.6 us   throughput =   1601 inf/s
B=  32  call p50 =   18.44 ms   per-sample =  576.2 us   throughput =   1735 inf/s
B=  64  call p50 =   37.82 ms   per-sample =  591.0 us   throughput =   1692 inf/s
B= 128  call p50 =   79.12 ms   per-sample =  618.1 us   throughput =   1618 inf/s
B= 256  call p50 =  204.50 ms   per-sample =  798.8 us   throughput =   1252 inf/s
--- S2: threads=1, nnpack=off
B=   1  call p50 =    0.32 ms   per-sample =  322.9 us   throughput =   3097 inf/s
B=   4  call p50 =    0.73 ms   per-sample =  182.5 us   throughput =   5478 inf/s
B=   8  call p50 =    1.26 ms   per-sample =  157.4 us   throughput =   6352 inf/s
B=  16  call p50 =    2.32 ms   per-sample =  144.8 us   throughput =   6905 inf/s
B=  32  call p50 =    4.47 ms   per-sample =  139.8 us   throughput =   7154 inf/s
B=  64  call p50 =    8.79 ms   per-sample =  137.3 us   throughput =   7281 inf/s
B= 128  call p50 =   17.46 ms   per-sample =  136.4 us   throughput =   7332 inf/s
B= 256  call p50 =   35.53 ms   per-sample =  138.8 us   throughput =   7205 inf/s
--- S3: threads=4, nnpack=off
B=   1  call p50 =    0.49 ms   per-sample =  486.0 us   throughput =   2058 inf/s
B=   4  call p50 =    0.81 ms   per-sample =  202.0 us   throughput =   4951 inf/s
B=   8  call p50 =    1.31 ms   per-sample =  164.4 us   throughput =   6085 inf/s
B=  16  call p50 =    1.89 ms   per-sample =  118.2 us   throughput =   8458 inf/s
B=  32  call p50 =    3.06 ms   per-sample =   95.6 us   throughput =  10464 inf/s
B=  64  call p50 =    5.09 ms   per-sample =   79.5 us   throughput =  12585 inf/s
B= 128  call p50 =    9.17 ms   per-sample =   71.6 us   throughput =  13958 inf/s
B= 256  call p50 =   26.84 ms   per-sample =  104.8 us   throughput =   9538 inf/s
```

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">

<line x1="70" y1="250" x2="600" y2="250" stroke="currentColor"/><line x1="70" y1="250" x2="70" y2="30" stroke="currentColor"/> <text x="64" y="254.0" font-size="12" text-anchor="end">0</text><line x1="70" y1="250.0" x2="600" y2="250.0" stroke="#888" stroke-opacity="0.3"/> <text x="64" y="180.7" font-size="12" text-anchor="end">5000</text><line x1="70" y1="176.7" x2="600" y2="176.7" stroke="#888" stroke-opacity="0.3"/>
<text x="64" y="107.3" font-size="12" text-anchor="end">10000</text><line x1="70" y1="103.3" x2="600" y2="103.3" stroke="#888" stroke-opacity="0.3"/> <text x="64" y="34.0" font-size="12" text-anchor="end">15000</text><line x1="70" y1="30.0" x2="600" y2="30.0" stroke="#888" stroke-opacity="0.3"/> <text x="70.0" y="267" font-size="12" text-anchor="middle">1</text>
<text x="202.5" y="267" font-size="12" text-anchor="middle">4</text> <text x="268.8" y="267" font-size="12" text-anchor="middle">8</text> <text x="335.0" y="267" font-size="12" text-anchor="middle">16</text> <text x="401.2" y="267" font-size="12" text-anchor="middle">32</text> <text x="467.5" y="267" font-size="12" text-anchor="middle">64</text> <text x="533.8" y="267" font-size="12" text-anchor="middle">128</text>
<text x="600.0" y="267" font-size="12" text-anchor="middle">256</text> <text x="335.0" y="284" font-size="12" text-anchor="middle">batch 크기 B (log2 축)</text> <text x="70" y="20" font-size="13">throughput (inferences/s)</text> <polyline fill="none" stroke="#d0564a" stroke-width="2" points="70.0,197.9 202.5,170.1 268.8,157.6 335.0,226.5 401.2,224.6 467.5,225.2 533.8,226.3 600.0,231.6"/>
<circle cx="70.0" cy="197.9" r="3" fill="#d0564a"/> <circle cx="202.5" cy="170.1" r="3" fill="#d0564a"/> <circle cx="268.8" cy="157.6" r="3" fill="#d0564a"/> <circle cx="335.0" cy="226.5" r="3" fill="#d0564a"/> <circle cx="401.2" cy="224.6" r="3" fill="#d0564a"/> <circle cx="467.5" cy="225.2" r="3" fill="#d0564a"/> <circle cx="533.8" cy="226.3" r="3" fill="#d0564a"/>
<circle cx="600.0" cy="231.6" r="3" fill="#d0564a"/> <polyline fill="none" stroke="#4a7bd0" stroke-width="2" points="70.0,204.6 202.5,169.7 268.8,156.8 335.0,148.7 401.2,145.1 467.5,143.2 533.8,142.5 600.0,144.3"/> <circle cx="70.0" cy="204.6" r="3" fill="#4a7bd0"/> <circle cx="202.5" cy="169.7" r="3" fill="#4a7bd0"/> <circle cx="268.8" cy="156.8" r="3" fill="#4a7bd0"/>
<circle cx="335.0" cy="148.7" r="3" fill="#4a7bd0"/> <circle cx="401.2" cy="145.1" r="3" fill="#4a7bd0"/> <circle cx="467.5" cy="143.2" r="3" fill="#4a7bd0"/> <circle cx="533.8" cy="142.5" r="3" fill="#4a7bd0"/> <circle cx="600.0" cy="144.3" r="3" fill="#4a7bd0"/>
<polyline fill="none" stroke="#3f9a6b" stroke-width="2" points="70.0,219.8 202.5,177.4 268.8,160.8 335.0,125.9 401.2,96.5 467.5,65.4 533.8,45.3 600.0,110.1"/> <circle cx="70.0" cy="219.8" r="3" fill="#3f9a6b"/> <circle cx="202.5" cy="177.4" r="3" fill="#3f9a6b"/> <circle cx="268.8" cy="160.8" r="3" fill="#3f9a6b"/> <circle cx="335.0" cy="125.9" r="3" fill="#3f9a6b"/> <circle cx="401.2" cy="96.5" r="3" fill="#3f9a6b"/>
<circle cx="467.5" cy="65.4" r="3" fill="#3f9a6b"/> <circle cx="533.8" cy="45.3" r="3" fill="#3f9a6b"/> <circle cx="600.0" cy="110.1" r="3" fill="#3f9a6b"/> <text x="70.0" y="210.8" font-size="12" text-anchor="middle">0.49 ms</text> <text x="335.0" y="116.9" font-size="12" text-anchor="middle">1.89 ms</text> <text x="467.5" y="56.4" font-size="12" text-anchor="middle">5.09 ms</text>
<text x="533.8" y="36.3" font-size="12" text-anchor="middle">9.17 ms</text> <text x="600.0" y="101.1" font-size="12" text-anchor="middle">26.84 ms</text> <text x="341.0" y="244.5" font-size="12">B≥16: NNPACK 선택 → 절벽</text> <text x="70" y="298" font-size="12">빨강 S1 = 1 thread, 기본 설정 · 파랑 S2 = 1 thread, NNPACK off</text><text x="70" y="314" font-size="12">초록 S3 = 4 threads, NNPACK off (라벨 = S3 호출당 p50)</text>
</svg>
```

그림 7 — batch 크기 대 throughput. 빨강(기본 설정)은 B=16에서 1/4로 떨어진다. 파랑(1 thread)은 B=16 이후 약 7300 inf/s에서 포화한다. 초록(4 threads)은 B=128에서 약 14,000 inf/s까지 오르지만 그때 호출 한 번은 9 ms다.

출력과 그림에서 볼 것:

- **batch는 latency를 throughput으로 바꾼다.** S2에서 B=1 → 8은 호출 latency 0.32 → 1.26 ms(4배), throughput 3100 → 6350 inf/s(2배). 이득의 원천: op마다 드는 고정 오버헤드(dispatch, 할당)를 B개가 나눠 내고, 가중치를 한 번 읽어 B개 입력에 재사용한다(arithmetic intensity 증가 — D3 roofline).
- 1 thread에서는 B≈16 이후 **포화**한다. 고정 오버헤드가 이미 충분히 나뉘었고 이제 순수 연산이 병목이다. 4 threads(S3)는 batch가 커야 비로소 스레드들이 나눌 일이 생겨서 B=1에서는 제일 느리고(0.49 ms) B=128에서는 제일 빠르다. **2.5절의 "스레드를 늘리면 느려진다"는 batch=1 이야기였다.**
- **절벽**: S1(기본 설정)은 B=16에서 throughput이 6297 → 1601로 떨어진다. torch profiler로 보니 B=8에서는 `aten::_slow_conv2d_forward`·기본 conv 경로가, B=16에서는 `aten::_nnpack_spatial_convolution`(NNPACK 라이브러리)이 선택됐고 이것이 이 머신에서 훨씬 느렸다. `torch.backends.nnpack.set_flags(False)`로 끄면(S2) 절벽이 사라진다. 교훈: **런타임은 shape에 따라 다른 커널을 고른다.** 성능 곡선은 매끄럽다고 가정하지 말고 실제 운영할 shape에서 잰다. NPU·DSP 컴파일러도 똑같이 행동한다(채널 수·tile 경계에서 절벽 — C7 2.5절).
- S3의 B=256에서 throughput이 다시 떨어진 원인은 확인하지 않았다(중간 텐서가 캐시를 넘었거나 측정 중 다른 작업이 겹쳤을 수 있다). 스윕은 반복해서 재현되는지 확인해야 한다.
- 서버라면 "p99 latency ≤ 50 ms를 지키는 최대 batch"를 고른다. 오프라인 로그 처리라면 latency는 상관없으니 throughput 최대점(여기서는 S3, B=128)을 고른다. **같은 모델, 다른 목적, 다른 최적점.**

---

## 10. 임베디드 관점에서 다시 보기

- **WCET 사고방식을 모델에도**: 전통적인 실시간 펌웨어는 WCET(worst-case execution time)로 설계한다. 신경망 추론은 대부분 **데이터와 무관한 고정 제어 흐름**이다(같은 shape, 같은 루프 횟수). 그래서 MCU 위의 int8 CNN은 분포가 매우 좁고 WCET 분석에 잘 맞는다. 꼬리를 만드는 것은 모델이 아니라 **주변**이다: 인터럽트, 캐시·flash wait state, 버스 경합(DMA와 CPU가 같은 SRAM 뱅크), DVFS, 공유 NPU. 예외적으로 입력에 따라 계산량이 바뀌는 모델(early exit, 동적 NMS 후보 수, LLM 생성 길이, beam search)은 꼬리를 모델이 만든다.
- **계산량을 평평하게**: 1초 창을 100 ms마다 통째로 돌리는 것보다 스트리밍 모델로 프레임마다 조금씩 돌리면 CPU 부하가 평평해져서 꼬리·버퍼·조각내기 문제가 한꺼번에 줄어든다.
- **여유 margin**: 펌웨어 sign-off에서 쓰던 방식 그대로, 측정한 최악값(실험 중 max)에 margin을 곱해 예산과 비교한다. 예: 측정 max 6 ms × 1.3 = 7.8 ms로 10 ms 안이다. 온도·전압 코너(저전압에서 클럭 강하), 다른 기능 동시 실행(BLE 스트리밍 + 음성)을 corner case로 측정한다.
- **telemetry**: 4.3절 히스토그램과 deadline miss 카운터를 필드 펌웨어에 넣어 두면, 출시 후 "가끔 wake word를 놓친다"는 리포트를 재현 없이 분석할 수 있다.
- **전력과의 관계**: 빨리 끝내고 잠드는 race-to-idle(D7·E9)은 latency도 줄인다. 반대로 DVFS가 저클럭에서 시작해 부하를 보고 올리는 정책이면 **첫 추론이 느린** 꼬리가 생긴다. 주기적 추론이면 주파수 정책을 추론 주기에 맞춰 고정하는 것이 흔하다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 평균 latency만 보고 | 필드에서 가끔 오디오 끊김·wake 누락 | 꼬리가 deadline을 넘음 | p99·max·miss rate로 보고, 히스토그램을 본다 |
| warm-up 없이 잼 | 첫 몇 번이 결과를 끌어올림, 반복마다 숫자 다름 | 할당·커널 선택·캐시 cold | warm-up을 버리고, cold latency는 따로 기록 |
| 총 시간 / 횟수로 잼 | 분포를 볼 수 없음 | 반복별 측정 안 함 | 반복마다 타임스탬프 |
| 스레드 수 기본값 | 작은 모델이 이상하게 느리고 꼬리가 큼 | 스레드 동기화 비용, 과구독 | 명시적으로 고정하고 스윕 |
| model-only만 보고 | 기기 전체 지연이 예상의 2배 | 전처리·복사·후처리 누락 | end-to-end를 단계별 + 전체로 잰다 |
| 단계 p99를 그냥 더함 | 예산이 과하게 빡빡해 보임 | 꼬리가 동시에 오지 않음 | 전체를 직접 재거나 분포를 합성(시뮬레이션) |
| closed-loop 벤치로 스트리밍 판단 | 벤치는 괜찮은데 실제로 큐가 쌓임 | coordinated omission | 예정 도착 시각부터 latency를 잰다 |
| 긴 추론을 비선점 루프에서 | 주기적으로 샘플 유실 | 비선점 구간이 hop보다 김 | 별도 태스크 + 선점, 또는 조각 ≤ hop − feature |
| 계산만 줄이면 반응이 빨라진다고 생각 | 모델을 2배 빠르게 했는데 체감 변화 없음 | 알고리즘 지연(창·smoothing)이 지배 | 지연을 분해해 큰 항부터 줄인다 |
| 한 batch 크기에서 잰 곡선을 외삽 | 특정 batch에서 급격히 느려짐 | shape별 커널 선택 | 운영할 shape 전부에서 측정 |

---

## 12. 면접에서 이렇게 말한다

**Q.** "How do you benchmark an on-device model properly?"

**A.** 조건을 고정하고(런타임·버전, 스레드, 코어, 주파수, 정밀도, 입력 shape) cold 첫 호출을 따로 기록한 다음, warm-up을 버리고 수천 번 반복마다 단조 타이머나 cycle counter로 잰다. 결과는 평균이 아니라 p50·p90·p99·max와 히스토그램으로 보고하고, 모델만이 아니라 전처리·복사·후처리를 포함한 end-to-end도 잰다. 스트리밍이면 예정 도착 시각 기준으로 잰다.

> I pin the conditions — runtime version, thread count, core, clock policy, precision, input shape — record the cold first call separately, then discard warm-up runs and time each of a few thousand iterations individually with a monotonic timer or the cycle counter. I report p50, p99, max and a histogram rather than a mean, and I measure end-to-end including pre-processing, copies and post-processing, not just the model. For streaming I timestamp from the scheduled arrival of each frame to avoid coordinated omission.

**Q.** "Average latency is 5 ms and the frame budget is 10 ms. Are we safe?"

**A.** 평균만으로는 알 수 없다. 분포를 봐야 한다. 내가 만든 예에서 평균 5.1 ms인데 0.19%가 10 ms를 넘었고, 100 fps면 5초에 한 번 miss다. 버퍼 깊이·miss 처리 정책·다른 태스크와의 간섭까지 포함해 miss rate를 재고, 기능이 허용하는 miss rate와 비교한다.

> Not necessarily — the mean says nothing about the tail. In one example the mean was 5.1 ms but 0.19 % of frames exceeded 10 ms, which at 100 frames per second is a miss every five seconds. I'd look at p99, p99.9 and max under realistic load, then compute the deadline-miss rate given our buffering, and compare that against what the feature can tolerate.

**Q.** "What's the difference between algorithmic latency and compute latency for a wake word?"

**A.** 계산 지연은 입력이 준비된 뒤 모델이 도는 시간이고, 알고리즘 지연은 입력이 준비될 때까지 기다리는 시간이다 — 창 길이와 학습 정렬, hop 격자, smoothing, debounce, lookahead. 1초 창·100 ms hop·3개 smoothing이면 계산이 6 ms여도 호출어 끝에서 trigger까지 약 270 ms가 알고리즘 지연이다. 그래서 반응 속도는 hop·smoothing 설계로, 계산 최적화는 전력으로 가져간다.

> Compute latency is how long inference takes once the input is ready; algorithmic latency is how long we must wait until it is ready — window length and how training clips were aligned, the hop grid, posterior smoothing, debounce and any lookahead. With a 1-second window, 100 ms hop and 3-window smoothing, about 270 ms of the delay after the keyword ends is algorithmic even if the model runs in 6 ms. So responsiveness is tuned through hop and smoothing, and faster kernels mostly buy power.

**Q.** "How do you schedule inference alongside audio capture on an RTOS?"

**A.** DMA ping-pong과 완료 ISR은 버퍼 교체와 알림만 한다. feature는 높은 우선순위 태스크에서 프레임마다, inference는 낮은 우선순위 태스크에서 N 프레임마다 돌린다. 선점이면 feature의 최악 응답은 자기 계산 + 공유 자원 blocking뿐이라 이용률 계산으로 확인할 수 있고, 비선점이면 inference를 hop보다 짧은 조각으로 쪼갠다. 공유 자원은 priority inheritance mutex나 소유권을 넘기는 큐로, NPU 오프로드는 비동기 제출 + 완료 인터럽트 + 타임아웃으로 처리한다.

> The DMA completion ISR only swaps buffers and signals. Feature extraction runs per frame in a high-priority task, inference every N frames in a lower-priority task. With preemption the feature task's worst-case response is its own compute plus any blocking, which I check with a utilization and response-time analysis; without preemption I split inference into chunks shorter than the hop. Shared resources use priority-inheritance mutexes or ownership-passing queues, and accelerator offload is asynchronous with a completion interrupt and a timeout.

**Q.** "When would you batch on an edge device, and what does it cost?"

**A.** 실시간 경로는 batch=1이다. 여러 채널·후보를 한 번에 돌리거나 로그 오프라인 처리, 서버 쪽에서는 batch로 고정 오버헤드와 가중치 읽기를 나눠 throughput을 올린다. 대가는 호출당 latency와 메모리(activation이 B배)다. 내 측정에서 B=1→8은 latency 4배, throughput 2배였고, 특정 batch에서 커널 선택이 바뀌어 throughput이 1/4로 떨어지는 절벽도 있었다. 그래서 운영할 shape에서 곡선을 직접 잰다.

> On the real-time path it's batch 1. I batch when I have multiple channels or candidates at once, for offline log processing, or on the server side, because it amortizes per-op overhead and weight reads. The cost is per-call latency and B-times activation memory. In my measurements going from batch 1 to 8 quadrupled call latency and doubled throughput, and at batch 16 the framework switched convolution kernels and throughput fell by 4×, so I always sweep the shapes we'll actually run.

---

## 13. 직접 해보기

1. 16 kHz 오디오, hop 20 ms, 창 25 ms. hop 한 번의 샘플 수와 창 하나의 샘플 수는? 정답: hop 320 샘플, 창 400 샘플.
2. 초당 50개 요청이 들어오고 평균 체류 시간이 60 ms다. 시스템 안에 평균 몇 개가 있나? 정답: Little's law `N = 50 × 0.06 = 3`개.
3. 3단 파이프라인 4 ms, 6 ms, 2 ms를 단계마다 다른 코어에서 돌린다. 최대 throughput과 latency는? 정답: 1/6 ms ≈ 167 fps, latency 12 ms (큐 대기 없을 때).
4. 예제 9의 설정에서 hop 100 ms, smoothing 2개, threshold 0.5, debounce 1일 때 min 검출 지연은? 정답: ceil(0.5 × 2) = 1개면 되므로 smoothing 추가 0 → min = 0 + 10 + 6 = 16 ms (B와 같음). threshold 0.6이면 2개 필요 → 116 ms.
5. 코드 과제: 예제 8의 `stream()`을 바꿔 i.i.d. 대신 "느린 실행이 몰려 오는" 경우를 만들어 보라(예: 1% 확률로 다음 20 프레임 동안 처리 시간 3배). 같은 depth에서 drop이 어떻게 변하나? 힌트: 상태 변수 하나(남은 slow 프레임 수)로 구현된다. 몰릴수록 필요한 depth가 커진다.
6. 코드 과제: `benchlib.bench`를 open-loop로 바꿔라 — 요청을 1 ms마다 예정하고 latency를 "예정 시각 → 완료"로 잰다. 모델 p50이 0.3 ms일 때 예정 간격을 0.5 ms, 0.35 ms로 줄이면 p99가 어떻게 되나? 힌트: `time.perf_counter_ns()`로 예정 시각까지 busy-wait. 간격이 p50에 가까워지면 p99가 폭증한다(1절 P=4 ms와 같은 현상).

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| latency | 지연 | 입력 하나가 들어와서 결과가 나올 때까지의 시간 |
| throughput | 처리량 | 단위 시간당 완료되는 추론 수 (inf/s, fps) |
| tail latency | 꼬리 지연 | p99·p99.9·max처럼 분포 오른쪽 끝의 드문 느린 경우 |
| percentile (pXX) | 분위수 | 측정값의 XX%가 이 값 이하 |
| jitter | 지터 | 주기적 사건의 시각·소요 시간이 흔들리는 정도 |
| deadline | 마감 | 이때까지 끝나지 않으면 틀린 결과로 치는 시각 (프레임 hop) |
| deadline miss rate | 마감 초과율 | 마감을 넘긴 프레임의 비율 |
| overrun / drop | 넘침 / 버림 | 버퍼가 꽉 차 새 데이터를 잃는 것 |
| WCET | 최악 실행 시간 | worst-case execution time. 실시간 설계의 기준 |
| utilization | 이용률 | 평균 계산 시간 ÷ 주기. 1 미만이어야 따라간다 (충분조건 아님) |
| batch | 배치 | 입력 여러 개를 한 번의 호출로 묶기 |
| pipelining | 파이프라이닝 | 단계를 겹쳐 실행해 throughput을 올림. latency는 그대로 |
| Little's law | 리틀의 법칙 | 시스템 안 평균 개수 = 도착률 × 평균 체류 시간 |
| warm-up | 예열 | 측정 전 버리는 반복 (할당·캐시·커널 선택 안정화) |
| cold start | 콜드 스타트 | 첫 호출. 초기화 비용이 들어가 느림 |
| coordinated omission | 조정된 누락 | closed-loop 벤치가 느린 구간의 대기 시간을 빼먹는 측정 오류 |
| algorithmic latency | 알고리즘 지연 | 창·hop·smoothing·lookahead 때문에 기다려야 하는 시간 |
| compute latency | 계산 지연 | 입력이 준비된 후 계산에 걸리는 시간 |
| lookahead | 미리보기 | non-causal 모델이 필요로 하는 미래 프레임 |
| causal model | 인과 모델 | 현재와 과거 입력만 쓰는 모델 (스트리밍 가능) |
| endpointing | 발화 끝 판정 | 말이 끝났다고 결정하는 것. 일정 시간 조용함을 기다림 |
| DWT CYCCNT | cycle counter | Cortex-M 디버그 블록의 32비트 클럭 카운터 |
| priority inversion | 우선순위 역전 | 높은 태스크가 낮은 태스크의 자원 때문에 중간 태스크에게 막힘 |

---

## 15. 요약 & 체크리스트

latency는 입력 하나의 시간, throughput은 초당 완료 수다. batch=1 직렬에서만 역수 관계이고, batch는 latency를 throughput으로 바꾸며, pipeline은 throughput만 올린다(Little's law로 필요한 버퍼 수가 나온다). 모델 latency는 분포다: warm-up을 버리고 반복마다 재서 p50·p99·max와 히스토그램으로 보고, 스레드·코어·주파수·런타임을 기록한다. 실내 측정에서 배경 부하는 p50을 18% 늘리는 동안 p99를 8배로 만들었고, 평균 5 ms에 예산 10 ms라도 꼬리 때문에 5초에 한 번 miss할 수 있다. 스트리밍에서는 deadline = hop이고, 링버퍼 깊이는 drop을 latency로 교환하는 손잡이다. wake word 같은 기능은 계산 지연보다 창·hop·smoothing이 만드는 알고리즘 지연이 지배한다. RTOS에서는 ISR은 알림만, feature는 높은 우선순위로 프레임마다, inference는 낮은 우선순위 별도 태스크로 두고, 비선점이면 조각을 hop보다 짧게 쪼갠다.

- [ ] batch=1 직렬, batch B, pipeline 각각에서 latency와 throughput의 관계를 식으로 쓸 수 있다
- [ ] Little's law로 필요한 버퍼 슬롯 수를 손으로 계산할 수 있다
- [ ] warm-up·반복별 측정·분위수·스레드 고정을 갖춘 벤치마크 하니스를 Python과 C로 쓸 수 있다
- [ ] 평균이 꼬리를 숨기는 예를 숫자로 설명하고, 이상치를 잘라도 되는 경우와 안 되는 경우를 구분할 수 있다
- [ ] 타이머 해상도(`clock_getres`)와 MCU DWT CYCCNT 설정 레지스터를 설명할 수 있다
- [ ] 측정한 latency 분포로 스트리밍 루프의 drop·miss rate를 시뮬레이션할 수 있다
- [ ] wake word 검출 지연을 격자 대기·smoothing·DMA·계산으로 분해해 손으로 계산할 수 있다
- [ ] 선점/비선점/조각내기에서 feature 태스크의 최악 응답 시간을 추정할 수 있다
- [ ] 음성 비서 한 턴의 latency 예산을 단계별로 나눌 수 있다
- [ ] batch 스윕 곡선을 재고 포화·절벽을 해석할 수 있다

---

## 참고 자료

- J. D. C. Little, "A Proof for the Queuing Formula: L = λW", Operations Research, 1961.
- J. Dean, L. A. Barroso, "The Tail at Scale", Communications of the ACM, 2013 — 분산 시스템의 tail latency 고전. 원리는 기기 안에서도 같다.
- Gil Tene, "How NOT to Measure Latency" (강연) — coordinated omission과 분위수 보고.
- C. L. Liu, J. W. Layland, "Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment", Journal of the ACM, 1973 — rate-monotonic 이용률 한계.
- V. J. Reddi et al., "MLPerf Inference Benchmark", ISCA 2020 — SingleStream·MultiStream·Server·Offline 시나리오. 그리고 MLPerf Tiny (C. Banbury et al., 2021).
- T. Stivers et al., "Universals and cultural variation in turn-taking in conversation", PNAS, 2009.
- J. Nielsen, "Response Times: The 3 Important Limits" (Nielsen Norman Group).
- Arm, "Armv7-M Architecture Reference Manual" / "Armv8-M Architecture Reference Manual" — DWT, DEMCR 레지스터 정의.
- PyTorch 문서: [torch.set_num_threads](https://pytorch.org/docs/stable/generated/torch.set_num_threads.html), [torch.profiler](https://pytorch.org/docs/stable/profiler.html)
- ONNX Runtime 문서: [Thread management](https://onnxruntime.ai/docs/performance/tune-performance/threading.html), [Graph optimizations](https://onnxruntime.ai/docs/performance/model-optimizations/graph-optimizations.html)
- Python 문서: [time.perf_counter_ns](https://docs.python.org/3/library/time.html#time.perf_counter_ns)
- Y. Zhang et al., "Hello Edge: Keyword Spotting on Microcontrollers", 2017 — DS-CNN KWS (이 노트의 모델 모양).
