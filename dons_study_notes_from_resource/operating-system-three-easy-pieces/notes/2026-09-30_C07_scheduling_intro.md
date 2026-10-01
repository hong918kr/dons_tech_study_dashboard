# Ch.07 스케줄링 입문 — FIFO, SJF, STCF, RR 그리고 두 가지 지표

> 📖 원문: [07. Scheduling: Introduction](../book-md/C07_scheduling_introduction.md) · [PDF p.83](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=83) · ⏱️ 읽기 약 40분 · 🔗 선행: [Ch.06](2026-09-30_C06_limited_direct_execution.md)

## 0. 한눈에 보기

Ch.06 에서 **메커니즘**(타이머 인터럽트, 문맥 교환)을 배웠으니, 이제 **정책(policy)** — "다음에 누구를 돌릴까" — 을 세운다. 이 챕터는 비현실적인 가정 5개에서 출발해 하나씩 풀면서 스케줄러 4개(FIFO → SJF → STCF → RR)를 만들고, 두 지표(반환시간 vs 응답시간)가 **서로 싸운다**는 사실을 보여준다.

> **THE CRUX: HOW TO DEVELOP SCHEDULING POLICY** — "How should we develop a basic framework for thinking about scheduling policies? What are the key assumptions? What metrics are important? What basic approaches have been used in the earliest of computer systems?"
>
> (스케줄링 정책을 생각하는 기본 틀을 어떻게 세울까? 핵심 가정은? 중요한 지표는? 초창기 시스템은 어떤 기본 방법을 썼나?)

- 반환시간(turnaround)만 보면 **짧은 것부터(SJF/STCF)** 가 최적.
- 응답시간(response)만 보면 **번갈아 조금씩(RR)** 이 최적.
- 둘 다 잘하는 정책은 없다 → 다음 장 MLFQ 가 "과거로 미래를 예측"해서 절충한다.

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 워크로드(workload) | 시스템에서 돌아가는 job 들의 특성 묶음 | "다들 10초짜리, 동시에 도착" |
| job | 스케줄링 단위가 되는 프로세스(또는 CPU burst 하나) | 원문 A, B, C |
| 반환시간(turnaround time) | 완료 시각 − 도착 시각 | 줄 서서 계산 끝날 때까지 |
| 응답시간(response time) | 처음 실행된 시각 − 도착 시각 | 키를 누르고 화면에 처음 반응이 올 때까지 |
| FIFO / FCFS | 먼저 온 순서대로 끝까지 실행 | 마트 계산대 한 줄 |
| 호위 효과(convoy effect) | 긴 job 하나 뒤에 짧은 job 들이 줄줄이 막힘 | 카트 세 대 꽉 채운 손님 뒤 |
| SJF | 가장 짧은 job 먼저, 비선점 | "10개 이하 전용 계산대" |
| STCF / PSJF | 남은 시간이 가장 짧은 job 먼저, **선점** | 새 손님이 더 빨리 끝나면 끼워줌 |
| 선점(preemption) | 실행 중인 job 을 멈추고 다른 job 실행 | 타이머 인터럽트 + 문맥 교환 |
| RR (Round Robin) | time slice 만큼씩 돌아가며 실행 | 1초씩 돌려 쓰기 |
| time slice / quantum | RR 이 한 번에 주는 실행 시간, 타이머 주기의 배수 | 10 ms, 100 ms |
| 상각(amortization) | 고정 비용을 덜 자주 내서 총비용을 줄임 | slice 를 늘려 문맥 교환 비율 감소 |
| 겹치기(overlap) | 한 job 이 I/O 기다리는 동안 다른 job 이 CPU 사용 | 디스크 읽는 동안 계산 |
| 오라클(oracle) | job 길이를 미리 아는 전지전능한 스케줄러 | 현실엔 없음 |

## 2. 워크로드 가정 다섯 개 (7.1)

정책을 비교하려면 먼저 "어떤 job 들이 오는가"를 정해야 한다. 원문은 일부러 말도 안 되게 단순한 가정에서 출발한다.

1. 모든 job 은 같은 시간 동안 돈다.
2. 모든 job 은 동시에 도착한다.
3. 한 번 시작하면 끝날 때까지 돈다 (선점 없음).
4. 모든 job 은 CPU 만 쓴다 (I/O 없음).
5. 각 job 의 실행 시간을 미리 안다.

이 챕터의 구조는 단순하다: **가정 하나를 풀 때마다 기존 정책이 망가지는 예를 만들고, 그걸 고치는 새 정책을 낸다.** 특히 5번(길이를 안다)은 "스케줄러가 전지전능하다"는 뜻이라 가장 비현실적이다 — 이건 끝까지 남겨 뒀다가 다음 장 MLFQ 에서 푼다.

## 3. 스케줄링 지표: 반환시간 (7.2)

$$T_{turnaround} = T_{completion} - T_{arrival}$$

가정 2(동시 도착)에서는 $T_{arrival}=0$ 이라 반환시간 = 완료 시각이다. 반환시간은 **성능(performance)** 지표다. 또 하나의 축인 **공정성(fairness)** (예: Jain's Fairness Index) 은 성능과 자주 충돌한다 — 성능을 올리려고 어떤 job 을 계속 미루면 공정성이 떨어진다.

## 4. FIFO — 가장 단순한 것부터 (7.3)

먼저 온 순서대로 끝까지 실행한다. 구현이 쉽고, 가정 1~5 아래에선 꽤 괜찮다.

**원문 예제 1 (같은 길이).** A, B, C 가 t=0 에 (A가 아주 살짝 먼저) 도착, 각 10초.

```text
완료: A=10, B=20, C=30
평균 반환시간 = (10 + 20 + 30) / 3 = 60 / 3 = 20
```

**가정 1을 풀자 (길이가 다름).** A=100초, B=C=10초. FIFO 는 A 를 먼저 끝까지 돌린다.

```text
완료: A=100, B=110, C=120
평균 반환시간 = (100 + 110 + 120) / 3 = 330 / 3 = 110
```

짧은 B, C 가 무거운 A 뒤에서 100초를 기다렸다. 이게 **호위 효과(convoy effect)** — 자원을 오래 쓰는 소비자 하나 뒤에 가벼운 소비자들이 줄줄이 묶이는 현상이다. 데이터베이스 락에서 처음 이름 붙여졌다(원문 [B+79]).

## 5. SJF — 짧은 것부터 (7.4)

**Shortest Job First**: 가장 짧은 job 부터, 그 다음 짧은 것… 이름이 곧 정의다. 같은 A=100, B=C=10 이면 B → C → A:

```text
완료: B=10, C=20, A=120
평균 반환시간 = (10 + 20 + 120) / 3 = 150 / 3 = 50     (110 → 50, 2배 이상 개선)
```

> **TIP — SJF 원리**: 고객이 체감하는 반환시간이 중요한 곳이면 어디든 SJF 가 통한다. 마트의 "10개 이하 계산대"가 바로 SJF.

모든 job 이 동시에 도착한다면 SJF 는 평균 반환시간에 대해 **최적**이다. (직관: 짧은 job 을 앞에 두면 그 짧은 시간만큼만 뒤 job 들이 기다린다. 앞뒤를 바꿔 보면 손해만 난다 — 면접 Q2 참고.)

> **ASIDE — 선점형 스케줄러**: 옛 배치 시스템의 스케줄러는 비선점(non-preemptive) 이었다. 현대 스케줄러는 거의 다 **선점형** — Ch.06 의 타이머 인터럽트와 문맥 교환으로 실행 중인 job 을 언제든 멈출 수 있다.

### 5.1 가정 2를 풀면 SJF 도 무너진다

A 가 t=0 에 도착(100초), B, C 는 **t=10 에 도착**(각 10초). SJF 는 비선점이라, t=0 에 혼자 있던 A 를 고르고 나면 끝까지 돌린다.

```text
A: 도착 0,  완료 100 → 반환 100
B: 도착 10, 완료 110 → 반환 100
C: 도착 10, 완료 120 → 반환 110
평균 = (100 + 100 + 110) / 3 = 310 / 3 = 103.33
```

다시 호위 효과. 해결하려면 가정 3(끝까지 실행)을 풀어야 한다.

## 6. STCF — SJF 에 선점을 더하다 (7.5)

**Shortest Time-to-Completion First** (= Preemptive SJF, PSJF): 새 job 이 들어올 때마다 "남은 시간이 가장 짧은" job 을 고른다. t=10 에 B, C 가 오면 A(남은 90초)를 선점하고 B, C 를 먼저 끝낸다.

```text
A: 0~10 실행, (B,C 도착) 선점됨 … 30~120 실행 → 완료 120, 반환 120 − 0  = 120
B: 10~20 실행                                 → 완료 20,  반환 20 − 10  = 10
C: 20~30 실행                                 → 완료 30,  반환 30 − 10  = 20
평균 = (120 + 10 + 20) / 3 = 150 / 3 = 50
```

같은 가정(도착 시각은 제각각, 길이는 앎) 아래에서 STCF 는 평균 반환시간 **최적**이다. 아래 그림이 SJF 와 STCF 를 나란히 보여준다.

```svg
<svg viewBox="0 0 660 250" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">A=100s(t=0 도착), B·C=10s(t=10 도착) — 원문 Fig 7.4 vs 7.5</text>
  <text x="10" y="58" fill="currentColor">SJF (비선점)</text>
  <rect x="120" y="40" width="400" height="28" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="320" y="59" fill="currentColor" text-anchor="middle">A (100)</text>
  <rect x="520" y="40" width="40" height="28" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="540" y="59" fill="currentColor" text-anchor="middle">B</text>
  <rect x="560" y="40" width="40" height="28" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="580" y="59" fill="currentColor" text-anchor="middle">C</text>
  <text x="10" y="118" fill="currentColor">STCF (선점)</text>
  <rect x="120" y="100" width="40" height="28" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="140" y="119" fill="currentColor" text-anchor="middle">A</text>
  <rect x="160" y="100" width="40" height="28" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="180" y="119" fill="currentColor" text-anchor="middle">B</text>
  <rect x="200" y="100" width="40" height="28" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="220" y="119" fill="currentColor" text-anchor="middle">C</text>
  <rect x="240" y="100" width="360" height="28" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="420" y="119" fill="currentColor" text-anchor="middle">A (남은 90)</text>
  <line x1="160" y1="30" x2="160" y2="150" stroke="#d9534f" stroke-dasharray="4 3" stroke-width="1.5"/>
  <text x="166" y="148" fill="#d9534f" font-size="12">B, C 도착 (t=10)</text>
  <line x1="120" y1="170" x2="600" y2="170" stroke="currentColor"/>
  <g fill="currentColor" font-size="11" text-anchor="middle">
    <line x1="120" y1="166" x2="120" y2="174" stroke="currentColor"/><text x="120" y="187">0</text>
    <line x1="200" y1="166" x2="200" y2="174" stroke="currentColor"/><text x="200" y="187">20</text>
    <line x1="280" y1="166" x2="280" y2="174" stroke="currentColor"/><text x="280" y="187">40</text>
    <line x1="360" y1="166" x2="360" y2="174" stroke="currentColor"/><text x="360" y="187">60</text>
    <line x1="440" y1="166" x2="440" y2="174" stroke="currentColor"/><text x="440" y="187">80</text>
    <line x1="520" y1="166" x2="520" y2="174" stroke="currentColor"/><text x="520" y="187">100</text>
    <line x1="600" y1="166" x2="600" y2="174" stroke="currentColor"/><text x="600" y="187">120 (초)</text>
  </g>
  <text x="120" y="218" fill="currentColor">SJF 평균 반환 = (100 + 100 + 110)/3 = 103.33</text>
  <text x="120" y="238" style="fill:var(--accent)" font-weight="bold">STCF 평균 반환 = (120 + 10 + 20)/3 = 50</text>
</svg>
```

## 7. 새 지표: 응답시간 (7.6)

배치 시대라면 STCF 로 충분했다. 그런데 **시분할(time-sharing)** 이 등장하면서 사용자가 터미널 앞에 앉아 "반응"을 기다리게 됐다. 그래서 새 지표가 생겼다.

$$T_{response} = T_{firstrun} - T_{arrival}$$

위 STCF 스케줄의 응답시간: A=0, B=10−10=0, C=20−10=10 → 평균 **3.33**.

STCF 는 응답시간에 나쁘다. 세 job 이 동시에 오면 세 번째 job 은 앞의 두 개가 **통째로** 끝날 때까지 한 번도 못 돈다. 키보드를 쳤는데 10초 뒤에 글자가 뜨는 셈.

## 8. Round Robin — 조금씩 돌아가며 (7.7)

RR 은 job 을 끝까지 돌리지 않고 **time slice(= scheduling quantum)** 만큼만 돌린 뒤 run queue 의 다음 job 으로 넘어간다. 그래서 time-slicing 이라고도 한다. 주의: time slice 는 **타이머 인터럽트 주기의 배수**여야 한다 (타이머가 10 ms 마다 울리면 slice 는 10, 20, … ms).

**원문 예제.** A, B, C 동시 도착, 각 5초, RR slice = 1초.

```text
SJF : AAAAABBBBBCCCCC          응답 = (0 + 5 + 10)/3 = 5
RR  : ABCABCABCABCABC          응답 = (0 + 1 + 2)/3  = 1

반환시간:  SJF  = (5 + 10 + 15)/3 = 10
           RR   = A13, B14, C15 → (13 + 14 + 15)/3 = 14   ← 훨씬 나쁨
```

```svg
<svg viewBox="0 0 640 230" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C07-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">A·B·C 각 5s, 동시 도착 — Fig 7.6 (SJF) vs Fig 7.7 (RR, slice=1s)</text>
  <text x="10" y="56" fill="currentColor">SJF</text>
  <rect x="80" y="38" width="150" height="28" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="155" y="57" fill="currentColor" text-anchor="middle">A</text>
  <rect x="230" y="38" width="150" height="28" fill="none" stroke="currentColor"/>
  <text x="305" y="57" fill="currentColor" text-anchor="middle">B</text>
  <rect x="380" y="38" width="150" height="28" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="455" y="57" fill="currentColor" text-anchor="middle">C</text>
  <text x="10" y="116" fill="currentColor">RR</text>
  <g stroke="currentColor">
    <rect x="80"  y="98" width="30" height="28" style="fill:var(--accent-soft)"/>
    <rect x="110" y="98" width="30" height="28" fill="none"/>
    <rect x="140" y="98" width="30" height="28" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
    <rect x="170" y="98" width="30" height="28" style="fill:var(--accent-soft)"/>
    <rect x="200" y="98" width="30" height="28" fill="none"/>
    <rect x="230" y="98" width="30" height="28" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
    <rect x="260" y="98" width="30" height="28" style="fill:var(--accent-soft)"/>
    <rect x="290" y="98" width="30" height="28" fill="none"/>
    <rect x="320" y="98" width="30" height="28" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
    <rect x="350" y="98" width="30" height="28" style="fill:var(--accent-soft)"/>
    <rect x="380" y="98" width="30" height="28" fill="none"/>
    <rect x="410" y="98" width="30" height="28" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
    <rect x="440" y="98" width="30" height="28" style="fill:var(--accent-soft)"/>
    <rect x="470" y="98" width="30" height="28" fill="none"/>
    <rect x="500" y="98" width="30" height="28" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="95" y="117">A</text><text x="125" y="117">B</text><text x="155" y="117">C</text>
    <text x="185" y="117">A</text><text x="215" y="117">B</text><text x="245" y="117">C</text>
    <text x="275" y="117">A</text><text x="305" y="117">B</text><text x="335" y="117">C</text>
    <text x="365" y="117">A</text><text x="395" y="117">B</text><text x="425" y="117">C</text>
    <text x="455" y="117">A</text><text x="485" y="117">B</text><text x="515" y="117">C</text>
  </g>
  <line x1="80" y1="145" x2="530" y2="145" stroke="currentColor"/>
  <g fill="currentColor" font-size="11" text-anchor="middle">
    <text x="80" y="160">0</text><text x="230" y="160">5</text><text x="380" y="160">10</text><text x="530" y="160">15</text>
  </g>
  <text x="545" y="57" fill="currentColor" font-size="12">응답 5 / 반환 10</text>
  <text x="545" y="117" style="fill:var(--accent)" font-size="12" font-weight="bold">응답 1 / 반환 14</text>
  <line x1="465" y1="190" x2="505" y2="135" stroke="currentColor" marker-end="url(#C07-arrow)"/>
  <text x="250" y="205" fill="currentColor" font-size="12">RR: 모두 금방 한 번씩 돌지만(응답↓), 다들 늦게 끝난다(반환↑) — 끝 시각만 보는 반환시간엔 최악에 가깝다</text>
</svg>
```

### 8.1 time slice 길이 = 트레이드오프

- slice 가 **짧을수록** 응답시간이 좋아진다.
- 그러나 너무 짧으면 **문맥 교환 비용**이 전체를 잡아먹는다.

> **TIP — 상각(amortization)으로 비용 줄이기**: slice 10 ms, 문맥 교환 1 ms 면 약 10% 가 낭비. slice 를 100 ms 로 늘리면 1% 미만. 고정 비용을 덜 자주 내는 기법이 상각이다.

정확히 계산해 보면: 10 ms 일하고 1 ms 교환 → 낭비 비율 = 1/(10+1) = **9.1%**, 100 ms 면 1/101 = **0.99%**.

그리고 문맥 교환 비용은 레지스터 저장/복원만이 아니다. 실행 중에 쌓아 둔 **CPU 캐시, TLB, 분기 예측기** 상태가 다른 job 으로 넘어가면서 날아가고, 돌아왔을 때 다시 데워야 한다(원문 [MB91]). 실측에선 이 "간접 비용"이 직접 비용보다 큰 경우가 흔하다.

### 8.2 공정함과 반환시간은 상충한다

RR 은 매 순간 CPU 를 고르게 나누는 **공정한** 정책이고, 그래서 반환시간(끝나는 시각만 봄)에는 거의 최악이다. 일반화하면:

- 불공정해도 되면 짧은 job 을 끝까지 몰아줘 반환시간을 줄일 수 있다 (대신 응답시간 손해).
- 공정함을 택하면 응답시간이 줄지만 반환시간을 손해 본다.

원문 말대로 "케이크를 먹으면서 동시에 가질 수는 없다". 지금까지 정리:

| 정책 | 선점 | 반환시간 | 응답시간 | 필요한 지식 |
|---|---|---|---|---|
| FIFO | X | 길이 섞이면 나쁨 (convoy) | 나쁨 | 없음 |
| SJF | X | 동시 도착이면 최적 | 나쁨 | job 길이 |
| STCF | O | 최적 | 나쁨 | job 길이 |
| RR | O | 거의 최악 | 좋음 (slice 짧을수록) | 없음 |

## 9. I/O 를 끼워 넣기 (7.8)

가정 4(I/O 없음)를 푼다. job 이 I/O 를 요청하면 그동안 CPU 를 안 쓰고 **blocked** 상태가 된다(Ch.04 상태도). 스케줄러가 결정할 시점은 둘이다.

- I/O 를 **시작할 때**: 다른 job 을 돌려야 CPU 가 놀지 않는다.
- I/O 가 **끝날 때**: 인터럽트가 오고 OS 가 그 job 을 blocked → ready 로 옮긴다. 바로 돌릴지 정해야 한다.

**원문 예제.** A, B 모두 CPU 50 ms 필요. A 는 10 ms 돌고 I/O(10 ms) 를 5번 반복, B 는 50 ms 연속 CPU.

```text
(한 칸 = 10 ms)
Fig 7.8 (나쁨: A 를 다 끝낸 뒤 B)
CPU : A . A . A . A . A B B B B B      (· = CPU 가 놀고 있음)
Disk: . A . A . A . A . . . . . .
      0   20  40  60  80  100 120 140 ms   → 140 ms 걸림

Fig 7.9 (겹치기: A 의 10 ms 조각 하나하나를 독립 job 으로 보고 STCF)
CPU : A B A B A B A B A B
Disk: . A . A . A . A . .
      0   20  40  60  80  100 ms          → 100 ms 에 끝
```

핵심 아이디어: **A 의 10 ms CPU burst 하나하나를 독립된 job 으로 취급**한다. STCF 입장에서 "10 ms 짜리 A 조각 vs 50 ms 짜리 B" 면 당연히 A 조각 먼저. A 가 I/O 하러 가면 B 가 돌고, A 의 I/O 가 끝나 새 조각이 생기면 B 를 선점한다. 그 결과 CPU 와 디스크가 동시에 일한다 — **겹치기(overlap)**.

> **TIP — 겹치기가 이용률을 높인다**: 디스크 I/O 든 원격 메시지든, 작업을 시작해 두고 다른 일로 넘어가면 시스템 전체 이용률과 효율이 올라간다.

이렇게 하면 대화형(interactive) job 은 burst 가 짧으니 자주 돌고, 그 job 이 I/O 를 기다리는 동안 CPU-bound job 이 CPU 를 채운다.

## 10. 오라클은 없다 (7.9) & 요약 (7.10)

마지막 가정 5(길이를 안다)가 가장 나쁜 가정이다. 범용 OS 는 job 이 얼마나 돌지 거의 모른다. 그러면:

- 길이를 모르고 SJF/STCF 처럼 동작하려면?
- 거기에 RR 의 좋은 응답시간까지 얻으려면?

답: **최근 과거로 미래를 예측**하는 스케줄러 — **다단계 피드백 큐(MLFQ)**, 다음 장의 주제다.

## 11. 직접 해보기

### 11.1 OSTEP 시뮬레이터 `scheduler.py`

```text
cd .tools/ostep-homework/cpu-sched
python3 ./scheduler.py -p FIFO -l 100,10,10 -c
```

```text
ARG policy FIFO
ARG jlist 100,10,10

Here is the job list, with the run time of each job: 
  Job 0 ( length = 100.0 )
  Job 1 ( length = 10.0 )
  Job 2 ( length = 10.0 )


** Solutions **

Execution trace:
  [ time   0 ] Run job 0 for 100.00 secs ( DONE at 100.00 )
  [ time 100 ] Run job 1 for 10.00 secs ( DONE at 110.00 )
  [ time 110 ] Run job 2 for 10.00 secs ( DONE at 120.00 )

Final statistics:
  Job   0 -- Response: 0.00  Turnaround 100.00  Wait 0.00
  Job   1 -- Response: 100.00  Turnaround 110.00  Wait 100.00
  Job   2 -- Response: 110.00  Turnaround 120.00  Wait 110.00

  Average -- Response: 70.00  Turnaround 110.00  Wait 70.00
```

원문 Fig 7.2 의 110 그대로. 같은 워크로드에 `-p SJF`:

```text
python3 ./scheduler.py -p SJF -l 100,10,10 -c
```

```text
Execution trace:
  [ time   0 ] Run job 1 for 10.00 secs ( DONE at 10.00 )
  [ time  10 ] Run job 2 for 10.00 secs ( DONE at 20.00 )
  [ time  20 ] Run job 0 for 100.00 secs ( DONE at 120.00 )

Final statistics:
  Job   1 -- Response: 0.00  Turnaround 10.00  Wait 0.00
  Job   2 -- Response: 10.00  Turnaround 20.00  Wait 10.00
  Job   0 -- Response: 20.00  Turnaround 120.00  Wait 20.00

  Average -- Response: 10.00  Turnaround 50.00  Wait 10.00
```

Fig 7.3 의 50. 이번엔 RR (`-q 1`) 로 5초짜리 3개 (Fig 7.7):

```text
python3 ./scheduler.py -p RR -q 1 -l 5,5,5 -c
```

```text
Final statistics:
  Job   0 -- Response: 0.00  Turnaround 13.00  Wait 8.00
  Job   1 -- Response: 1.00  Turnaround 14.00  Wait 9.00
  Job   2 -- Response: 2.00  Turnaround 15.00  Wait 10.00

  Average -- Response: 1.00  Turnaround 14.00  Wait 9.00
```

응답 1, 반환 14 — 본문 계산과 일치. 숙제 2·3번(길이 100, 200, 300)도 돌려 보자:

```text
python3 ./scheduler.py -p SJF -l 300,200,100 -c      # Final 부분만
  Job   2 -- Response: 0.00  Turnaround 100.00  Wait 0.00
  Job   1 -- Response: 100.00  Turnaround 300.00  Wait 100.00
  Job   0 -- Response: 300.00  Turnaround 600.00  Wait 300.00
  Average -- Response: 133.33  Turnaround 333.33  Wait 133.33

python3 ./scheduler.py -p RR -q 1 -l 100,200,300 -c
  Job   0 -- Response: 0.00  Turnaround 298.00  Wait 198.00
  Job   1 -- Response: 1.00  Turnaround 499.00  Wait 299.00
  Job   2 -- Response: 2.00  Turnaround 600.00  Wait 300.00
  Average -- Response: 1.00  Turnaround 465.67  Wait 265.67

python3 ./scheduler.py -p FIFO -l 200,200,200 -c
  Average -- Response: 200.00  Turnaround 400.00  Wait 200.00
```

해석:
- RR 반환시간이 SJF 의 1.4배(465.67 vs 333.33). 대신 응답시간은 133 → 1.
- RR 에서 job0(100초)이 298 에 끝나는 이유: 처음 300초 동안 셋이 1초씩 돌아가며 job0 이 100번째 slice 를 받는 시각이 3×99+1 = 298.
- 길이가 모두 같으면(200,200,200) SJF = FIFO (숙제 4번의 답).
- 주의: 이 시뮬레이터는 **모든 job 이 t=0 에 도착**한다고 가정한다. 늦게 도착하는 job(Fig 7.4/7.5)은 아래 C 프로그램으로 확인한다.

### 11.2 C 로 짠 1틱 단위 스케줄러: `code/C07_sched_sim.c`

도착 시각을 지원하는 FIFO/SJF/STCF/RR 시뮬레이터. 매 틱마다 "누구를 돌릴지" 정책 함수 하나만 다르다는 점에 주목 — 메커니즘(1틱 실행, 상태 갱신)과 정책(pick)이 분리돼 있다.

```c
/* C07_sched_sim.c — FIFO / SJF / STCF / RR 를 1틱 단위로 시뮬레이션하고
 * Gantt 차트 + 반환시간(turnaround) + 응답시간(response)을 출력한다.
 * 도착 시간(arrival)을 지원하므로 원문 Figure 7.4/7.5 (늦게 온 B, C)도 재현된다. */
#include <stdio.h>
#include <string.h>

#define MAXJ 8
#define MAXT 512

typedef struct {
    char name;
    int arrival, length;   /* 입력 */
    int left, first_run, done;  /* 상태 */
} job_t;

enum policy { FIFO, SJF, STCF, RR };
static const char *pname[] = { "FIFO", "SJF", "STCF", "RR" };

static void simulate(enum policy p, int quantum, const job_t *in, int n) {
    job_t j[MAXJ];
    char gantt[MAXT + 1];
    int rrq[MAXJ * MAXT], qh = 0, qt = 0;     /* RR 용 FIFO 큐 (넉넉하게) */
    int cur = -1, slice = 0, finished = 0, t = 0;

    memcpy(j, in, sizeof(job_t) * (size_t)n);
    for (int i = 0; i < n; i++) { j[i].left = j[i].length; j[i].first_run = -1; j[i].done = -1; }

    while (finished < n && t < MAXT) {
        /* 1) 이번 틱에 도착한 job 을 RR 큐 뒤에 넣는다 */
        for (int i = 0; i < n; i++)
            if (j[i].arrival == t) rrq[qt++] = i;

        /* 2) 다음에 돌릴 job 고르기 */
        int pick = -1;
        if (p == FIFO || p == SJF) {            /* 비선점: 돌던 게 있으면 계속 */
            if (cur >= 0 && j[cur].left > 0) pick = cur;
            else for (int i = 0; i < n; i++) {
                if (j[i].arrival > t || j[i].left == 0) continue;
                if (pick < 0) { pick = i; continue; }
                if (p == FIFO && j[i].arrival < j[pick].arrival) pick = i;
                if (p == SJF  && j[i].length  < j[pick].length)  pick = i;
            }
        } else if (p == STCF) {                 /* 선점: 매 틱 남은 시간 최소 */
            for (int i = 0; i < n; i++) {
                if (j[i].arrival > t || j[i].left == 0) continue;
                if (pick < 0 || j[i].left < j[pick].left) pick = i;
            }
        } else {                                /* RR: 퀀텀 다 쓰면 큐 뒤로 */
            if (cur >= 0 && j[cur].left > 0 && slice < quantum) pick = cur;
            else {
                if (cur >= 0 && j[cur].left > 0) rrq[qt++] = cur;
                pick = (qh < qt) ? rrq[qh++] : -1;
                slice = 0;
            }
        }

        /* 3) 한 틱 실행 */
        if (pick < 0) { gantt[t++] = '.'; cur = -1; continue; }
        if (j[pick].first_run < 0) j[pick].first_run = t;
        gantt[t] = j[pick].name;
        j[pick].left--; slice++; t++;
        if (j[pick].left == 0) { j[pick].done = t; finished++; }
        cur = pick;
    }
    gantt[t] = '\0';

    double sumT = 0, sumR = 0;
    printf("[%s%s] ", pname[p], p == RR ? (quantum == 1 ? " q=1" : " q=2") : "");
    printf("Gantt: %s\n", gantt);
    for (int i = 0; i < n; i++) {
        int tat = j[i].done - j[i].arrival, rsp = j[i].first_run - j[i].arrival;
        sumT += tat; sumR += rsp;
        printf("   %c arr=%2d len=%2d  first=%2d done=%2d  T_turnaround=%3d T_response=%3d\n",
               j[i].name, j[i].arrival, j[i].length, j[i].first_run, j[i].done, tat, rsp);
    }
    printf("   avg turnaround=%.2f  avg response=%.2f\n", sumT / n, sumR / n);
}

int main(void) {
    /* 원문 Fig 7.4/7.5 를 1/10 축소: A=10틱(t=0 도착), B,C=1틱(t=1 도착) */
    job_t late[] = { {'A', 0, 10, 0,0,0}, {'B', 1, 1, 0,0,0}, {'C', 1, 1, 0,0,0} };
    puts("== Workload 1: A(0,10) B(1,1) C(1,1)  — 원문 Fig 7.4/7.5 의 1/10 축소판 ==");
    simulate(FIFO, 0, late, 3);
    simulate(SJF,  0, late, 3);
    simulate(STCF, 0, late, 3);
    simulate(RR,   1, late, 3);

    /* 새 예제: 도착이 제각각인 4개 job */
    job_t mix[] = { {'A', 0, 8, 0,0,0}, {'B', 1, 4, 0,0,0}, {'C', 2, 9, 0,0,0}, {'D', 3, 5, 0,0,0} };
    puts("\n== Workload 2: A(0,8) B(1,4) C(2,9) D(3,5) — 새 예제 ==");
    simulate(FIFO, 0, mix, 4);
    simulate(SJF,  0, mix, 4);
    simulate(STCF, 0, mix, 4);
    simulate(RR,   2, mix, 4);
    return 0;
}
```

```text
cc -Wall -Wextra -O0 -pthread code/C07_sched_sim.c -o .work/bin/C07_sched_sim && .work/bin/C07_sched_sim
```

실제 출력 (경고 0개):

```text
== Workload 1: A(0,10) B(1,1) C(1,1)  — 원문 Fig 7.4/7.5 의 1/10 축소판 ==
[FIFO] Gantt: AAAAAAAAAABC
   A arr= 0 len=10  first= 0 done=10  T_turnaround= 10 T_response=  0
   B arr= 1 len= 1  first=10 done=11  T_turnaround= 10 T_response=  9
   C arr= 1 len= 1  first=11 done=12  T_turnaround= 11 T_response= 10
   avg turnaround=10.33  avg response=6.33
[SJF] Gantt: AAAAAAAAAABC
   A arr= 0 len=10  first= 0 done=10  T_turnaround= 10 T_response=  0
   B arr= 1 len= 1  first=10 done=11  T_turnaround= 10 T_response=  9
   C arr= 1 len= 1  first=11 done=12  T_turnaround= 11 T_response= 10
   avg turnaround=10.33  avg response=6.33
[STCF] Gantt: ABCAAAAAAAAA
   A arr= 0 len=10  first= 0 done=12  T_turnaround= 12 T_response=  0
   B arr= 1 len= 1  first= 1 done= 2  T_turnaround=  1 T_response=  0
   C arr= 1 len= 1  first= 2 done= 3  T_turnaround=  2 T_response=  1
   avg turnaround=5.00  avg response=0.33
[RR q=1] Gantt: ABCAAAAAAAAA
   A arr= 0 len=10  first= 0 done=12  T_turnaround= 12 T_response=  0
   B arr= 1 len= 1  first= 1 done= 2  T_turnaround=  1 T_response=  0
   C arr= 1 len= 1  first= 2 done= 3  T_turnaround=  2 T_response=  1
   avg turnaround=5.00  avg response=0.33

== Workload 2: A(0,8) B(1,4) C(2,9) D(3,5) — 새 예제 ==
[FIFO] Gantt: AAAAAAAABBBBCCCCCCCCCDDDDD
   A arr= 0 len= 8  first= 0 done= 8  T_turnaround=  8 T_response=  0
   B arr= 1 len= 4  first= 8 done=12  T_turnaround= 11 T_response=  7
   C arr= 2 len= 9  first=12 done=21  T_turnaround= 19 T_response= 10
   D arr= 3 len= 5  first=21 done=26  T_turnaround= 23 T_response= 18
   avg turnaround=15.25  avg response=8.75
[SJF] Gantt: AAAAAAAABBBBDDDDDCCCCCCCCC
   A arr= 0 len= 8  first= 0 done= 8  T_turnaround=  8 T_response=  0
   B arr= 1 len= 4  first= 8 done=12  T_turnaround= 11 T_response=  7
   C arr= 2 len= 9  first=17 done=26  T_turnaround= 24 T_response= 15
   D arr= 3 len= 5  first=12 done=17  T_turnaround= 14 T_response=  9
   avg turnaround=14.25  avg response=7.75
[STCF] Gantt: ABBBBDDDDDAAAAAAACCCCCCCCC
   A arr= 0 len= 8  first= 0 done=17  T_turnaround= 17 T_response=  0
   B arr= 1 len= 4  first= 1 done= 5  T_turnaround=  4 T_response=  0
   C arr= 2 len= 9  first=17 done=26  T_turnaround= 24 T_response= 15
   D arr= 3 len= 5  first= 5 done=10  T_turnaround=  7 T_response=  2
   avg turnaround=13.00  avg response=4.25
[RR q=2] Gantt: AABBCCAADDBBCCAADDCCAADCCC
   A arr= 0 len= 8  first= 0 done=22  T_turnaround= 22 T_response=  0
   B arr= 1 len= 4  first= 2 done=12  T_turnaround= 11 T_response=  1
   C arr= 2 len= 9  first= 4 done=26  T_turnaround= 24 T_response=  2
   D arr= 3 len= 5  first= 8 done=23  T_turnaround= 20 T_response=  5
   avg turnaround=19.25  avg response=2.00
```

읽는 법:
- **Workload 1** 은 원문 숫자의 정확히 1/10: SJF 10.33 (원문 103.33), STCF 5.00 (원문 50), STCF 응답 0.33 (원문 3.33). 비선점 SJF 는 늦게 온 짧은 job 을 못 구한다 — FIFO 와 Gantt 가 똑같다.
- **Workload 2 (새 예제) — 손으로 STCF 를 따라가 보자.** t=0 A 혼자 → A. t=1 B(4) 도착, A 남은 7 > 4 → B 선점. t=2 C(9), t=3 D(5) 와도 B(남은 3, 2) 가 최소 → B 가 t=5 에 끝. t=5 남은 시간 A7, C9, D5 → D (5~10). 그 다음 A (10~17), 마지막 C (17~26). 반환 = A17, B4, C24, D7 → 52/4 = **13.00**. 프로그램과 일치.
- 반환시간 순위: STCF 13.00 < SJF 14.25 < FIFO 15.25 < RR 19.25. 응답시간 순위는 정반대에 가깝다: RR 2.00 이 최고.
- RR q=2 구현 규칙: 같은 틱에 "새로 도착한 job"이 "slice 를 다 쓴 job"보다 큐에 먼저 들어간다. 이런 tie-break 하나로 Gantt 가 달라지니, 면접에서 손으로 풀 때는 규칙을 먼저 말하고 시작하자.

## 12. 펌웨어 엔지니어의 눈으로

- **NVMe 큐 중재(arbitration) = 이 장의 정책들 그대로.** NVMe 스펙의 기본 중재는 SQ 간 **Round Robin**, 선택 기능으로 **Weighted Round Robin + Urgent 우선순위 클래스**가 있다. 컨트롤러 FW 가 SQ doorbell 들을 훑어 "다음 커맨드를 어느 SQ 에서 가져올지" 정하는 게 정확히 스케줄러 pick 함수다. Arbitration Burst(한 번에 몇 개 가져오나)는 RR 의 time slice 에 해당한다.
- **호위 효과 = NAND 의 head-of-line blocking.** 한 die 에서 tPROG(수백 µs)·tBERS(수 ms) 가 도는 동안 그 die 를 향한 4 KB read(tR 수십 µs)가 막히는 게 convoy 다. 그래서 SSD 는 **program/erase suspend-resume** 으로 긴 작업을 선점해 read 를 끼워 넣는다 — 이게 바로 SJF → STCF 로 가는 한 걸음(선점 추가)이다. 지표도 똑같이 갈린다: 처리량(반환시간 쪽) vs read tail latency(응답시간 쪽).
- **GC vs 호스트 I/O 는 "공정 vs 반환시간" 문제.** 백그라운드 GC 를 길게 몰아서 돌리면 효율(상각)은 좋지만 그동안 호스트 latency 가 튄다. 잘게 쪼개 호스트 I/O 와 번갈아 돌리면(RR 에 가까움) 응답은 고르지만 GC 총 시간과 쓰기 증폭 처리 효율이 떨어진다. time slice 고르기와 같은 트레이드오프.
- **RTOS 는 다른 축을 쓴다.** 베어메탈/RTOS 는 반환시간 평균이 아니라 **마감(deadline)** 이 지표라 고정 우선순위 선점(Rate Monotonic) 이나 EDF 를 쓰고, 같은 우선순위끼리만 RR time-slicing 을 한다. 이 장의 지표에 "worst-case 응답시간"이 빠져 있다는 걸 의식하자.
- **AI 가속기 커맨드 큐에서도 convoy 가 난다.** 큰 학습 커널이 엔진을 잡고 있으면 짧은 추론 요청이 뒤에서 기다린다. GPU 의 compute preemption(스레드블록/명령 경계에서 문맥 저장)은 STCF 처럼 선점을 하드웨어에 넣는 시도이고, 선점 단위가 거칠수록 문맥 저장 비용(=문맥 교환의 간접 비용)이 커진다.

## 13. 면접 질문

### Q1. 반환시간(turnaround)과 응답시간(response)의 차이, 그리고 각각에 최적인 정책은?
<details>
<summary>답 보기</summary>

- **반환시간** = 완료 − 도착. 배치 처리량 관점. job 길이를 알면 **SJF**(동시 도착) / **STCF**(도착 제각각, 선점) 가 평균 반환시간 최적.
- **응답시간** = 첫 실행 − 도착. 대화형 관점. **RR** 이 좋고 slice 가 짧을수록 더 좋다.
- 둘은 **상충**한다: RR 은 모든 job 을 늘여 놓아 반환시간이 거의 최악, STCF 는 늦게 줄 선 job 이 한참 못 돌아 응답시간이 나쁘다.
- 실제 OS 는 둘 다 필요해서 MLFQ/CFS 같은 절충안을 쓴다.

</details>

### Q2. 모든 job 이 동시에 도착할 때 SJF 가 평균 반환시간에 최적인 이유를 설명해 보라.
<details>
<summary>답 보기</summary>

**교환 논증(exchange argument)**. 어떤 스케줄에서 긴 job L 이 짧은 job S 바로 앞에 있다고 하자. 둘의 순서를 바꾸면:
- 둘 뒤에 있는 job 들의 완료 시각은 그대로(앞 두 개 합이 같으니까).
- L→S 순서: 완료 합 = (t+L) + (t+L+S). S→L 순서: (t+S) + (t+S+L). 차이 = **L − S > 0** 만큼 줄어든다.
- 따라서 "긴 게 짧은 것 앞"인 쌍이 하나라도 있으면 개선 가능 → 최적 스케줄은 길이 오름차순 = SJF.
- 평균 반환시간 = Σ(남은 job 수 × 길이) 꼴이라, 짧은 job 이 큰 가중치(뒤에 많이 남은 위치)를 받아야 최소가 된다고 설명해도 된다.

</details>

### Q3. RR 의 time slice 를 어떻게 정하나? 너무 짧으면/길면 무슨 일이 생기나?
<details>
<summary>답 보기</summary>

- **짧으면**: 응답시간은 좋아지지만 문맥 교환 비율이 커진다. 직접 비용(레지스터 저장, 커널 진입) + **간접 비용**(캐시·TLB·분기 예측기 상태 손실, 다시 데우기).
- **길면**: 상각이 잘 돼 효율은 좋지만 응답시간이 나빠지고, 극단적으로 길면 FIFO 와 같아진다.
- slice 는 **타이머 틱의 배수**. 예: slice 10 ms, 교환 1 ms → 1/11 ≈ 9% 낭비, 100 ms → 1% 미만.
- 실제로는 "대화형은 짧게, 배치는 길게" — 다음 장 MLFQ 처럼 **우선순위별로 slice 를 다르게** 주는 게 정석.

</details>

### Q4. STCF 의 단점은? 실제로 그대로 못 쓰는 이유는?
<details>
<summary>답 보기</summary>

- **오라클 필요**: 남은 실행 시간을 알아야 하는데 범용 OS 는 모른다. (추정은 가능: 지수 이동평균으로 다음 CPU burst 예측 — 고전 교과서의 `τ(n+1) = α·t(n) + (1−α)·τ(n)`.)
- **기아(starvation)**: 짧은 job 이 계속 들어오면 긴 job 은 영원히 못 돈다.
- **응답시간 나쁨**: 동시에 많이 오면 마지막 job 은 앞의 job 들이 다 끝날 때까지 첫 실행을 못 한다.
- 선점이 잦으면 문맥 교환 비용도 무시 못 한다.

</details>

### Q5. "호위 효과(convoy effect)" 를 시스템에서 본 적이 있나? 어떻게 완화하나?
<details>
<summary>답 보기</summary>

- 정의: 긴 자원 소비자 하나 뒤에 짧은 소비자들이 줄 서서 평균 대기시간이 폭증하는 현상.
- 예: 스토리지의 **head-of-line blocking**(큰 erase/program 이 read 를 막음), DB 락 대기열, 네트워크 큐의 큰 패킷 뒤 작은 패킷, GPU 의 긴 커널 뒤 짧은 추론.
- 완화: **선점**(suspend/resume, 커널 선점), **큐 분리**(짧은 요청 전용 큐 = "10개 이하 계산대"), **작업 쪼개기**(큰 작업을 작은 조각으로 → 각 조각이 독립 job 이 되어 겹치기 가능), 우선순위/가중치 중재.

</details>

### Q6. N 개 job 이 동시에 도착하고 RR slice 가 q 일 때 최악 응답시간은?
<details>
<summary>답 보기</summary>

- 마지막 순번 job 은 앞의 N−1 개가 slice 하나씩 쓴 뒤에 처음 돈다 → **(N − 1) · q**. (문맥 교환 비용 c 를 넣으면 (N − 1)(q + c).)
- 평균 응답시간은 (0 + q + … + (N−1)q)/N = **(N − 1)q/2**.
- 단, 각 job 길이가 q 보다 짧으면 앞 job 이 slice 를 다 안 쓰고 끝나므로 더 짧아진다 (숙제 7번).

</details>

## 14. 자가 점검 & 숙제

### 퀴즈 1. A=30, B=10, C=20 이 t=0 에 동시 도착. FIFO(A,B,C 순)와 SJF 의 평균 반환시간은?
<details>
<summary>답 보기</summary>

- FIFO: 완료 30, 40, 60 → 130/3 = **43.33**
- SJF (B, C, A): 완료 10, 30, 60 → 100/3 = **33.33**

</details>

### 퀴즈 2. 같은 job 들을 RR(q=10)로 돌리면 평균 반환시간과 평균 응답시간은?
<details>
<summary>답 보기</summary>

- 순서: A(0~10) B(10~20, 끝) C(20~30) A(30~40) C(40~50, 끝) A(50~60, 끝)
- 반환: A60, B20, C50 → 130/3 = **43.33**; 응답: A0, B10, C20 → **10**
- SJF 의 응답 (0+10+30)/3 = 13.33 보다 좋고, 반환은 SJF 33.33 보다 나쁘다.

</details>

### 퀴즈 3. STCF 와 RR 이 Workload 1(본문 C 출력)에서 같은 Gantt 가 나온 이유는?
<details>
<summary>답 보기</summary>

B, C 의 길이가 1틱이라 RR q=1 에서 slice 한 번에 끝난다. 그러면 RR 도 도착하자마자 B, C 를 한 번씩 돌리고 끝내 버려 STCF 와 결과가 같아진다. 일반적으로 **짧은 job 길이 ≤ slice** 면 RR 이 짧은 job 을 STCF 처럼 빨리 처리한다(숙제 5번과 연결).

</details>

### 퀴즈 4. slice 를 20 ms, 문맥 교환 직접 비용 0.5 ms, 캐시 재워밍 간접 비용 1.5 ms 라고 할 때 낭비 비율은?
<details>
<summary>답 보기</summary>

교환 1회 총비용 2 ms → 2 / (20 + 2) ≈ **9.1%**. 직접 비용만 보면 0.5/20.5 ≈ 2.4% 로 과소평가하게 된다 — 간접 비용이 지배적일 수 있다는 게 포인트.

</details>

### 꼭 해볼 원문 숙제

- **숙제 4** (SJF 와 FIFO 반환시간이 같아지는 워크로드): 길이가 모두 같거나 FIFO 도착 순서가 이미 오름차순이면 같다 — `-l 200,200,200` 으로 확인.
- **숙제 5** (SJF 와 RR 응답시간이 같아지는 조건): 모든 job 길이가 같고 q ≥ 그 길이면 RR = SJF. `-p RR -q 100 -l 100,100,100` 과 `-p SJF` 비교.
- **숙제 7** (RR 최악 응답시간 공식): `-q` 를 1, 5, 10 으로 바꿔 가며 마지막 job 의 Response 가 (N−1)q 를 따르는지 확인.

## 15. 다음으로

- 길이를 모르는 상태에서 SJF 처럼 굴고 RR 처럼 반응하려면? → [Ch.08 MLFQ](2026-09-30_C08_mlfq.md)
- "얼마나 빨리"가 아니라 "얼마만큼 나눠 줄지"가 목표라면? → [Ch.09 비례 배분](2026-09-30_C09_proportional_share.md)
- 메커니즘 복습(타이머 인터럽트, 문맥 교환) → [Ch.06 LDE](2026-09-30_C06_limited_direct_execution.md)
