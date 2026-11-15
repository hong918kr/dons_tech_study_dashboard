---
id: ARCH-A10
time: 120분
updated: 2026-11-11
---
# ARCH-A10 버스 중재: 고정 우선순위 vs 라운드로빈

## 학습 목표

- 공유 자원 하나(버스 / 메모리 포트)에 CPU · DMA · NPU 가 요청을 보내는 **사이클 단위 모델**을 짜고, 고정 우선순위(fixed priority) · 라운드로빈(round-robin, RR) · 가중 RR(weighted RR = beat 단위 Deficit Round Robin) 중재기를 각각 5–10 줄로 구현한다.
- 지연을 세 가지로 **정의해서** 잰다: wait(도착 → 그랜트), total(도착 → 완료), HOL(큐 머리에서 남 때문에 기다린 시간). 평균 · p99(nearest-rank) · 최악, 그리고 기아(starvation) 플래그.
- 두 상한을 증명하고 CHECK 로 확인한다: **RR 의 HOL 대기 ≤ 다른 마스터 최대 버스트의 합** (그리고 이 상한이 빡빡하다), **고정 우선순위 최상위의 대기 ≤ 다른 마스터 최대 버스트 − 1**. 요청이 있는데 자원이 노는 사이클이 0 인지(work-conserving)도 매 사이클 센다.
- "RR 은 공정하다" 가 **그랜트 단위**의 말이라는 것을 숫자로 보인다(대역폭은 버스트 길이 비례 → 짧은 버스트의 CPU 가 손해). DRR 은 대역폭이 가중치 비례이고, 그 오차 상한을 유도해 허용오차로 쓴다.
- 카메라/디스플레이 같은 **마감(deadline)이 있는 마스터**가 낀 SoC 에서 RR · DRR · 고정 우선순위 · QoS(긴급 승격 + 토큰 버킷 규제기)를 비교하고, "어떤 정책이 마감을 지키고 누구를 굶기나" 를 표로 답한다.

## 1. 문제

"라운드로빈 최악 대기 시간은?", "UART/디스플레이를 최고 우선순위로 올렸는데 왜 언더런이 나죠?", "NPU 가 대역폭을 다 먹지 않게 하려면?" — SoC · 인터커넥트 · 실리콘 검증 면접의 단골이고, 실제 칩에서는 디스플레이 언더런, 카메라 프레임 드롭, CPU 지연 급증으로 나타나는 문제다. ARCH-M11 에서 DMA 채널 중재(레벨 고정 + 레벨 안 RR, 비선점 버스트, 최고 우선순위 최악 대기 = 버스트 − 1, ⌈N/P⌉ FIFO 깊이)를 한 엔진 **안에서** 다뤘다. 이 레슨은 한 층 위 — **여러 마스터가 공유 포트 하나를 두고 다투는 인터커넥트 중재** — 로 올라가, 정책별 지연 **분포**와 대역폭 **몫**, 기아, 마감을 정량으로 비교한다.

```svg
<svg viewBox="0 0 740 305" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">모델: 마스터마다 요청 큐 → 중재기 → 공유 포트 (그랜트 1 개 = L 사이클 비선점 점유)</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="130" height="44" rx="4" class="f-ok"/>
    <rect x="20" y="96" width="130" height="44" rx="4" class="f-warn"/>
    <rect x="20" y="152" width="130" height="44" rx="4" class="f-acc"/>
    <rect x="20" y="208" width="130" height="44" rx="4" class="f-mut"/>
    <rect x="170" y="50" width="110" height="24" class="f-mut"/>
    <rect x="170" y="106" width="110" height="24" class="f-mut"/>
    <rect x="170" y="162" width="110" height="24" class="f-mut"/>
    <rect x="170" y="218" width="110" height="24" class="f-mut"/>
    <rect x="330" y="90" width="140" height="120" rx="8" class="f-acc"/>
    <rect x="530" y="120" width="190" height="60" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="30" y="58">CPU  L = 2</text><text x="30" y="76" font-size="12">드문드문 · 지연 민감</text>
    <text x="30" y="114">DMA  L = 16 × 4</text><text x="30" y="132" font-size="12">몰려옴 (디스크립터)</text>
    <text x="30" y="170">NPU  L = 8 / 32</text><text x="30" y="188" font-size="12">일정한 흐름 · 대량</text>
    <text x="30" y="226">CAM  L = 8</text><text x="30" y="244" font-size="12">64 사이클마다 · 마감 48</text>
    <text x="178" y="67" font-size="12">큐 (≤ 512, FIFO)</text>
    <text x="178" y="123" font-size="12">큐</text><text x="178" y="179" font-size="12">큐</text><text x="178" y="235" font-size="12">큐 (Part C)</text>
    <text x="345" y="115" font-weight="bold">중재기</text>
    <text x="345" y="137" font-size="12">FP · RR · DRR</text>
    <text x="345" y="155" font-size="12">QOS = 긴급 승격</text>
    <text x="345" y="173" font-size="12">+ 레벨 + 토큰 버킷</text>
    <text x="345" y="195" font-size="12">자원이 빌 때만 결정</text>
    <text x="540" y="143">공유 포트 (1 beat/cycle)</text>
    <text x="540" y="163" font-size="12">버스트 끝까지 아무도 못 끊음</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5" fill="none">
    <path d="M150 62 L170 62"/><path d="M150 118 L170 118"/><path d="M150 174 L170 174"/><path d="M150 230 L170 230"/>
    <path d="M280 62 L330 110"/><path d="M280 118 L330 135"/><path d="M280 174 L330 165"/><path d="M280 230 L330 195"/>
    <path d="M470 150 L528 150"/><path d="M520 144 L530 150 L520 156"/>
  </g>
  <text x="20" y="276" fill="currentColor" font-size="12">사이클 순서: ① 도착 생성 ② 토큰 충전 ③ 비었고 요청 있으면 중재 → 그랜트</text>
  <text x="20" y="294" fill="currentColor" font-size="12">④ 점유/유휴 기록 — 도착한 사이클에 바로 그랜트될 수 있다 (중재 지연 0)</text>
</svg>
```

모델 규칙 (starter 와 solution 이 같고, 네가 고치는 것은 중재기 본문뿐이다):

| 항목 | 정의 |
|---|---|
| 자원 | 한 사이클에 1 beat. 그랜트 = 머리 요청 하나를 L 사이클 연속 점유, **비선점**. 중재 지연 0 (마지막 beat 다음 사이클에 새 그랜트) |
| 요청 큐 | 마스터마다 FIFO 512 개. 넘치면 drop (마스터가 멈춘 것으로 본다 — offered 에는 포함) |
| wait | grant − arrival |
| total | wait + L = 도착 → 완료 (마지막 beat 다음 사이클). 마감 판정은 total > deadline |
| HOL | grant − max(arrival, **자기 직전 버스트의 끝**) — 큐 머리에서 *남* 때문에 기다린 시간 |
| starve | 어떤 요청의 HOL ≥ 4096 사이클 (끝까지 못 받은 요청은 시뮬레이션 끝까지의 나이로) |
| offered / achieved | 도착한 beat 수 / [0, T) 안에서 받은 beat 수, 둘 다 ÷ T (beat/cycle) |
| p99 | nearest-rank: 누적 개수 ≥ ⌈0.99 n⌉ 이 되는 최소 wait (그랜트된 요청만 — 7 장 실수 5) |

트래픽 생성기 (고정 시드 xorshift32 — 같은 입력이면 출력이 바이트 단위로 같다):

| 종류 | 사용처 | 동작 |
|---|---|---|
| `TG_BERN` | CPU, DMA | 매 사이클 확률 p 로 요청 `batch` 개 (CPU 1 × 2 beat, DMA 4 × 16 beat) |
| `TG_STREAM` | NPU(Part A/B), CAM | 누적기 += rate, L 을 넘을 때마다 요청 1 개 (CAM: 0.125 → 정확히 64 사이클마다 8 beat) |
| `TG_BACKLOG` | 포화 시험, NPU(Part C) | 큐가 비면 바로 하나 채움 — 항상 줄 서 있는 마스터 |
| `TG_SCRIPT` | 지시형 테스트 | {t, L} 목록 |

### Part A — 고정 우선순위와 라운드로빈, 그리고 두 상한

- `arb_fixed(s)`: `s->order[0]` 이 최고. 요청 있는 마스터 중 가장 높은 것. `arb_rr(s)`: `rr_last` 다음 번호부터 한 바퀴, 그랜트하면 `rr_last` 갱신.
- 지시형 트레이스(사이클별 점유자 문자열)가 한 글자까지 맞아야 한다: `00111100222.....` (FP 0>1>2), `00111122200.....` (RR) 등.
- 무작위 혼합 트래픽(부하 4 단계 × 시드 3)에서: work-conserving 위반 0, RR 의 모든 마스터 HOL ≤ Σ(다른 마스터 Lmax), FP 최상위 HOL ≤ max(다른 Lmax) − 1. 셋 다 backlog 일 때 RR 상한이 **정확히** 달성됨(빡빡함).

### Part B — 가중 RR (DRR) 과 부하 스윕, 기아

- `arb_drr(s)`: beat 단위 Deficit Round Robin. 정의는 코드 주석과 5 장에 있다.
- 셋 다 backlog: RR 의 대역폭 몫 = L_i / ΣL (그랜트 공정), DRR 의 몫 = w_i / Σw — 가중치 4 세트에서 **유도한 오차 상한** 안에 들어와야 한다. 한 마스터만 가벼우면 그 마스터는 원하는 만큼 받고, 나머지는 가중치 비율로 나눈다.
- 고정 우선순위 기아: CPU > NPU(backlog) > DMA 에서 DMA 그랜트 0. 같은 트래픽을 RR 로 → 기아 없음.
- 부하 스윕 x = 0.2 … 1.6 (CPU 0.15x · DMA 0.35x · NPU 0.5x) × {FP, RR, DRR}: 마스터별 offered/achieved, p99/최악 wait, HOL, 기아 표.

### Part C — 마감이 있는 마스터 + QoS

- `tb_tick / tb_ok / tb_charge`: 토큰 버킷(rate beat/cycle, cap). `arb_qos(s)`: (1) 머리 요청 나이 ≥ `urg_after` 인 마감 마스터는 긴급 → EDF, (2) 그 외에는 레벨(`qos[m]`, AxQOS 처럼 클수록 높음) 최대 + 같은 레벨 RR, 토큰이 없는 규제 대상은 soft 면 레벨 0, hard 면 후보 제외.
- SoC: CAM(64 사이클마다 8 beat, 마감 48) · CPU 0.1 · DMA 0.2 · NPU backlog L = 32. 정책 6 개의 CAM 미스 · 최악 total, CPU p99, DMA 기아, NPU 대역폭. DMA 까지 backlog 인 변형에서 "고정 우선순위는 대량 마스터 둘 중 하나를 반드시 굶긴다" 를 확인.

## 2. Problem (English)

> **Shared-resource arbitration, cycle-level.** One shared port (1 beat/cycle) is used by 3–4 masters, each with a FIFO request queue. A request `{arrival, L}` that is granted occupies the port for L consecutive cycles (non-preemptive). Arrivals in cycle t are visible to the arbiter in cycle t; the arbiter is consulted only when the port is free and some request is pending.
>
> Traffic: CPU — Bernoulli, 2-beat requests (latency-sensitive); DMA — Bernoulli batches of 4 × 16-beat bursts; NPU — constant-rate stream of 8-beat requests (or always backlogged with 32-beat bursts); CAM — one 8-beat request every 64 cycles with a 48-cycle completion deadline.
>
> A. Implement `int arb_fixed(const sim_t*)` (configurable order) and `int arb_rr(sim_t*)`. Prove and check: the arbiter is work-conserving; under RR, a master's head-of-line wait is at most the sum of the other masters' maximum burst lengths, and the bound is tight; under fixed priority, the top master waits at most (max other burst − 1).
>
> B. Implement weighted round robin as beat-based Deficit Round Robin `int arb_drr(sim_t*)`. With all masters saturated, show that RR divides bandwidth in proportion to burst length while DRR divides it in proportion to weights; derive the error bound you use as test tolerance. Sweep offered load from light to saturated and report, per master: offered vs achieved bandwidth, mean / p99 / max wait, max HOL wait, starvation.
>
> C. Add a token-bucket bandwidth regulator and a QoS arbiter (deadline-based urgency escalation, priority levels, RR within a level). Which policy meets CAM's deadline, and at what cost to the others?
>
> Follow-ups: what is the worst-case wait under RR and why? Why can't the highest-priority master get zero latency? Is RR fair? Soft vs hard regulators? How would you verify an arbiter in silicon?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
```

TODO 는 7 개: `arb_fixed`(A-1), `arb_rr`(A-2), `arb_drr`(B-1), `tb_tick`·`tb_ok`·`tb_charge`(C-1), `arb_qos`(C-2). 트래픽 생성기, 큐, 그랜트 기록, 지표, work-conserving 카운터, 표 출력은 주어진다. 처음 실행 결과 (실제 출력 일부, exit 1):

```text
== Part A: 지시형 트레이스 (m0: t0 L2, t3 L2 / m1: t0 L4 / m2: t0 L3) ==
  FP 0>1>2  ................
FAIL starter.c:336  strcmp(tr, "00111100222.....") == 0
...
== Part A: 무작위 혼합 트래픽 — work-conserving · 지연 상한 (부하 4 × 시드 3) ==
  FP   wc_viol=0 on all 12 runs: NO   top(CPU) worst HOL 49995 <= bound 15 (max other L - 1)
FAIL starter.c:373  wc
FAIL starter.c:375  hol_fp
...
== Part B: 셋 다 backlog — RR 은 그랜트 공정, 대역폭은 L 에 비례 ==
  RR  CPU share 0.0000  (L/sum L = 0.0769)
...
6/53 passed
```

스텁 중재기는 늘 −1 → 자원이 한 번도 안 쓰이고, 하네스가 매 사이클 "요청이 있는데 아무도 못 받음" 을 센다. 지나가는 6 개는 "DMA 가 굶는다", "RR 에서 CPU 가 원하는 만큼 못 받는다", "DRR 은 마감을 놓친다" 처럼 **아무것도 안 하면 참이 되는** 검사다 — 정답에서도 참이어야 하는 성질이지만 그것만으로는 아무것도 증명하지 않는다 (그래서 각각 옆에 "다른 정책에선 굶지 않는다" 같은 짝 검사가 있다).

## 4. 힌트

<details><summary>힌트 1 — 고정 우선순위는 한 줄, RR 은 "어디서부터 찾나" 가 전부</summary>

FP: `order[k]` 를 k = 0.. 로 보며 `pending` 인 첫 것. RR: `m = (rr_last + k) % nm`, k = 1..nm. k 가 1 부터라서 방금 받은 마스터는 **맨 마지막**에 다시 본다 — 그래서 혼자 요청 중이면 연속으로 받을 수 있다(work-conserving). 그랜트한 마스터를 `rr_last` 에 저장하는 걸 잊으면 늘 0 번부터 찾는 = 고정 우선순위가 된다.
</details>

<details><summary>힌트 2 — RR 상한: "내가 기다리는 동안 남이 몇 번 받을 수 있나"</summary>

HOL 대기가 시작되는 순간(내 직전 버스트가 끝났거나 내 요청이 도착한 순간) 버스를 쥔 것은 다른 누군가 j 이거나 아무도 아니다. 그 뒤 포인터는 j 에서 출발해 나에게 오기 전까지 **각 마스터를 최대 한 번** 지난다. 그러니 대기 ≤ (진행 중인 j 의 남은 버스트) + (j 와 나 사이 마스터들의 버스트) ≤ Σ_{j≠i} Lmax_j. 내 큐에 줄이 있으면? 내 직전 버스트 시간은 "남 때문" 이 아니니 빼고 잰다 — 그게 HOL 의 정의다.
</details>

<details><summary>힌트 3 — DRR 은 "차례" 와 "잔고"</summary>

차례가 시작될 때만 quantum 을 잔고(dc)에 더한다. 잔고로 머리 요청 L 을 감당할 수 있으면 그랜트하고 L 을 빼고 **차례를 유지**한다(다음 중재에서 또 이 마스터부터 본다). 감당 못 하면 차례 끝 → 다음 마스터. 큐가 비어서 차례가 끝나면 잔고 0 (안 쓴 몫을 저축하면 나중에 몰아 받아 다른 마스터의 지연을 깬다). quantum < L 이면 몇 바퀴에 걸쳐 잔고를 모아야 하니 루프를 넉넉히.
</details>

<details><summary>힌트 4 — 토큰 버킷 3 줄과 soft/hard</summary>

`tokens = min(cap, tokens + rate)`, `ok = tokens ≥ L`, `charge: tokens = tokens ≥ L ? tokens − L : 0`. soft 에서는 토큰이 없어도 "다른 누구도 원하지 않으면" 받을 수 있다 — 그래서 charge 가 0 으로 바닥을 친다(빚을 지게 하면 빈 버스를 쓴 대가로 나중에 굶는다).
</details>

<details><summary>힌트 5 — QoS 는 두 단계</summary>

① 긴급: 마감 마스터 중 `head_age ≥ urg_after` 인 것들 → 절대 마감(arrival + deadline)이 가장 이른 것. ② 아니면 유효 레벨이 가장 높은 것, 같은 레벨은 RR 순서상 먼저 나오는 것 — `rr_last + k` 순서로 돌며 "엄격히 클 때만" 교체하면 RR 타이브레이크가 공짜로 된다. 규제 대상이 `!tb_ok` 면 soft → 레벨 0, hard → 건너뜀.
</details>

## 5. 풀이

### 0. 왜 HOL 을 따로 재나 — "느리다" 의 세 가지 원인

wait 이 큰 이유는 셋 중 하나다: (1) **중재** 때문에 남에게 밀림, (2) **자기 큐** 에 앞선 요청이 많음 (자기 도착률 > 자기 몫 = 대역폭 부족), (3) 비선점 버스트에 **막힘**. HOL 은 (2) 를 빼고 (1)+(3) 만 잰다. 그래서 상한 증명의 대상이 되고, 기아(= 중재가 영원히 안 줌)를 과부하(= 줄이 길어짐)와 구분할 수 있다. Part B 스윕에서 FP 의 DMA 는 x = 1.2 에서 p99 wait 37750 사이클인데 HOL 은 172 — **기아가 아니라 대역폭 부족**이다. x = 1.6 에서야 HOL 199886 → 기아.

### Part A — 고정 우선순위와 RR

```c
static int arb_fixed(const sim_t *s) {
    for (int k = 0; k < s->nm; k++)
        if (pending(s, s->order[k])) return s->order[k];
    return -1;
}
static int arb_rr(sim_t *s) {
    for (int k = 1; k <= s->nm; k++) {
        int m = (s->rr_last + k) % s->nm;
        if (pending(s, m)) { s->rr_last = m; return m; }
    }
    return -1;
}
```

지시형 트레이스 (실제 출력):

```text
== Part A: 지시형 트레이스 (m0: t0 L2, t3 L2 / m1: t0 L4 / m2: t0 L3) ==
  FP 0>1>2  00111100222.....
  FP 2>1>0  22211110000.....
  RR        00111122200.....
  (m0: t0 L2 ×3 / m1: t0 L4 / m2: t0 L3, DRR quantum 4)
  RR        0011112220000...
  DRR q=4   0000111122200...
```

첫 줄: m0 의 두 번째 요청은 t3 에 오지만 m1 의 버스트(t2–t5)를 **끊지 못한다** → t6 에 받음 (wait 3). m2 는 우선순위가 가장 낮아 t8 (wait 8). 셋째 줄 RR 은 같은 입력에서 m2 를 t6 에 준다. 마지막 두 줄: m0 이 2-beat 요청 3 개를 들고 있을 때 RR 은 한 번에 하나씩, DRR(quantum 4)은 한 차례에 **4 beat 어치(요청 2 개)** 를 준다.

**고정 우선순위 최상위의 상한.** 최상위 마스터는 자기 요청이 보이는 순간 자원이 비어 있으면 무조건 받는다. 막힌다면 진행 중인 남의 버스트 때문인데, 그 버스트는 **도착 사이클 이전에** 시작됐어야 한다(같은 사이클이면 도착이 먼저 처리되므로 최상위가 이겼다). 그래서 HOL ≤ max_{j≠top} L_j − 1. 무작위 12 회 실행의 최악이 정확히 15 = 16 − 1 이었다. ARCH-M11 의 "버스트 − 1" 과 같은 식이고, **우선순위를 아무리 올려도 이 아래로는 못 내린다** — 줄이려면 남의 버스트를 짧게 자르거나(버스트 제한) 선점 가능한 프로토콜이어야 한다.

**RR 상한과 빡빡함.** 힌트 2 의 논증으로 HOL ≤ Σ_{j≠i} Lmax_j. 셋 다 backlog 이면 정확히 그 값이 된다:

```text
== Part A: RR 상한은 빡빡하다 — 셋 다 backlog (L = 2 / 16 / 8) ==
  maxHOL CPU 24 (bound 24)  DMA 10 (bound 10)  NPU 18 (bound 18)
  trace 0011111111111111112222222200111111111111111122222222001111111111
```

```svg
<svg viewBox="0 0 740 215" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">셋 다 backlog 인 RR — 실제 트레이스 0–55 사이클 (1 칸 = 1 사이클 × 10 px)</text>
  <g stroke="currentColor">
    <rect x="80" y="40" width="20" height="26" class="f-ok"/>
    <rect x="100" y="40" width="160" height="26" class="f-warn"/>
    <rect x="260" y="40" width="80" height="26" class="f-acc"/>
    <rect x="340" y="40" width="20" height="26" class="f-ok"/>
    <rect x="360" y="40" width="160" height="26" class="f-warn"/>
    <rect x="520" y="40" width="80" height="26" class="f-acc"/>
    <rect x="600" y="40" width="20" height="26" class="f-ok"/>
    <rect x="620" y="40" width="20" height="26" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="58">점유</text>
    <text x="83" y="58">C</text><text x="160" y="58">DMA 16</text><text x="280" y="58">NPU 8</text>
    <text x="343" y="58">C</text><text x="420" y="58">DMA 16</text><text x="540" y="58">NPU 8</text><text x="603" y="58">C</text>
    <text x="76" y="84">0</text><text x="96" y="84">2</text><text x="252" y="84">18</text><text x="332" y="84">26</text>
    <text x="352" y="84">28</text><text x="512" y="84">44</text><text x="592" y="84">52</text>
  </g>
  <g stroke="currentColor" stroke-width="2" fill="none">
    <path d="M100 104 L340 104"/><path d="M100 98 L100 110"/><path d="M340 98 L340 110"/>
    <path d="M260 134 L360 134"/><path d="M260 128 L260 140"/><path d="M360 128 L360 140"/>
    <path d="M340 164 L520 164" stroke-dasharray="5 3"/><path d="M340 158 L340 170"/><path d="M520 158 L520 170"/>
  </g>
  <g fill="currentColor">
    <text x="110" y="98">CPU HOL = 16 + 8 = 24 (= DMA Lmax + NPU Lmax)</text>
    <text x="380" y="134">DMA HOL = 8 + 2 = 10</text>
    <text x="530" y="164">NPU HOL = 2 + 16 = 18</text>
    <text x="10" y="190">RR 의 최악 = "다른 모두가 최대 버스트로 한 번씩" — 그 이상은 포인터가 막는다.</text>
    <text x="10" y="207">고정 우선순위의 낮은 마스터에게는 이런 상한이 없다.</text>
  </g>
</svg>
```

**Work-conserving.** 자원이 비었고 요청이 있는 사이클에 중재기가 −1 을 내거나 요청 없는 마스터를 고르면 `wc_viol` 이 오른다. FP/RR/DRR 모두 12 회 실행에서 0, 그리고 `busy + idle == 총 사이클`, `Σ served == busy` 로 회계도 맞춘다. 이 성질이 중요한 이유: 지연 상한 증명은 모두 "비면 바로 준다" 를 깔고 있다. 버그 난 RR(예: `rr_last + 1` 만 보고 없으면 −1)은 기능 테스트를 통과하면서 처리량만 조용히 잃는다 — 그래서 하드웨어 검증에서도 이것을 assertion 으로 건다 (RTLDV-M03 에서 SVA 로).

### Part B — 공정성의 단위, DRR, 부하 스윕

**RR 은 무엇에 대해 공정한가.** 셋 다 backlog 이면 RR 은 한 바퀴(26 사이클)에 각자 한 번씩 준다 → 대역폭 몫 = L_i / ΣL:

```text
  RR  CPU share 0.0769  (L/sum L = 0.0769)
  RR  DMA share 0.6154  (L/sum L = 0.6154)
  RR  NPU share 0.3077  (L/sum L = 0.3077)
```

**그랜트 공정 ≠ 대역폭 공정.** 버스트가 긴 DMA 가 CPU 의 8 배를 가져간다. 이게 바로 스윕에서 RR 이 CPU 를 망치는 이유다: 모두 바쁠 때 CPU 의 최대 서비스율은 "한 바퀴에 2 beat" = 2/26 ≈ 0.077 beat/cycle 인데, x = 0.6 에서 CPU 도착률이 이미 0.09 → DMA 묶음이 들어와 있는 동안 CPU 큐가 쌓인다(p99 149, x = 1.0 에서 9371, 그리고 achieved 0.135 < offered 0.149).

**DRR 의 정의** (Shreedhar & Varghese 의 Deficit Round Robin 을 비선점 버스 그랜트로 옮김):

```c
static int arb_drr(sim_t *s) {
    for (int guard = 0; guard < 1000000; guard++) {
        int m = s->drr_cur;
        if (!s->drr_turn) { s->dc[m] += s->quantum[m]; s->drr_turn = true; }   /* 차례 시작: 잔고 충전 */
        if (pending(s, m) && head_len(s, m) <= s->dc[m]) { s->dc[m] -= head_len(s, m); return m; }
        if (!pending(s, m)) s->dc[m] = 0;                                        /* 빈 큐는 저축 불가 */
        s->drr_turn = false;
        s->drr_cur = (m + 1) % s->nm;
    }
    return -1;
}
```

가중치 w_i → quantum Q_i = w_i × q (q = quantum 단위, beat). 몫이 **beat** 로 세어지므로 버스트 길이와 무관하게 대역폭이 w 비례가 된다.

**허용오차 유도.** 모두 backlog 이라 하자. 차례를 마친 직후 잔고는 0 ≤ dc_i < L_i (머리 요청을 감당 못 해서 끝났으니까). 그래서 k 번의 차례 동안 받은 양은 kQ_i − dc_i ∈ (kQ_i − L_i, kQ_i]. 임의 시점의 누적 S_i 는 k 차례 + (진행 중일 수 있는) 한 차례 일부 → S_i = kQ_i + e_i, −L_i < e_i < Q_i + L_i. 총합 T = kΣQ + Σe. 정리하면

|S_i − (Q_i/ΣQ)·T| = |e_i − (Q_i/ΣQ)Σe_j| < 2Q_i + L_i + Q_i·ΣL/ΣQ

여기에 T 경계에서 잘린 버스트 하나(< Lmax)를 더한 것을 T 로 나눠 허용오차로 쓴다. 상수/T 꼴이라 T = 200000 이면 10⁻³ 아래:

```text
== Part B: DRR (beat 단위 가중 RR) — 대역폭 몫 = 가중치 비율, 허용오차 = 유도한 상한 / T ==
  weights   qunit | CPU share (want)   DMA share (want)   NPU share (want) | max|err|  tol
  1:2:4       16 | 0.1429 (0.1429)    0.2858 (0.2857)    0.5714 (0.5714)   | 0.00007  0.00083
  4:2:1       16 | 0.5715 (0.5714)    0.2857 (0.2857)    0.1428 (0.1429)   | 0.00009  0.00080
  1:1:1       16 | 0.3334 (0.3333)    0.3334 (0.3333)    0.3333 (0.3333)   | 0.00005  0.00036
  3:1:1        4 | 0.6000 (0.6000)    0.2000 (0.2000)    0.2000 (0.2000)   | 0.00000  0.00029
```

1:1:1 이 핵심 행이다 — L 이 2 / 16 / 8 로 달라도 대역폭이 정확히 1/3 씩. 3:1:1 은 quantum 단위 4 < DMA 버스트 16 이라 DMA 가 **네 차례 잔고를 모아야** 한 번 받는다; 그래도 몫은 맞는다(잔고가 이월되니까). 측정 오차가 상한의 1/10 쯤인 건 상한이 최악 조합(모든 e_j 가 동시에 극단)을 가정해서다.

**가벼운 마스터가 있을 때 (work-conserving 가중 공유).**

```text
== Part B: DRR 3:3:4, CPU 만 가벼움 (0.10) — 남는 몫은 백로그끼리 3:4 로 ==
         offered   achvd grants | w.mean w.p99  w.max | maxHOL starv miss  drop
    CPU    0.101   0.101  10144 |   55.9   111    112 |    111     -    0     0
    DMA    0.385   0.385   4815 |   40.5   103    113 |     98     -    0     0
    NPU    0.513   0.513  12834 |   14.6    75     89 |     82     -    0     0
  DMA/NPU = 0.7504 (want 0.75)
```

CPU 는 몫 0.3 중 0.1 만 원하므로 다 받고, 남은 0.9 가 DMA:NPU = 3:4 로 나뉜다. 대신 **CPU 지연이 비싸다**: 최악 wait 112 = 다른 두 마스터의 quantum 합(48 + 64). DRR 의 지연 상한은 대략 Σ_{j≠i}(Q_j + L_j) 로, quantum 이 클수록 몫은 정밀해지지만(오차 ∝ Q/T 는 상관없음) 차례 하나가 길어져 지연이 늘어난다. **quantum 은 지연과 오버헤드의 손잡이**다 — 하드웨어 WRR 이 가중치를 작은 정수(예: 1–15)로 두는 이유이기도 하다(구체 구현은 제품마다 다름).

**고정 우선순위의 기아.**

```text
== Part B: 고정 우선순위의 기아 — CPU 0.1 · NPU backlog · DMA 0.2 (CPU > NPU > DMA) ==
  FP:
         offered   achvd grants | w.mean w.p99  w.max | maxHOL starv miss  drop
    CPU    0.099   0.099   9903 |    3.5     7      9 |      7     -    0     0
    DMA    0.211   0.000      0 |    0.0     0      0 | 199990   YES    0  2120
    NPU    0.901   0.901  22525 |    7.9    13     19 |     12     -    0     0
  RR:
         offered   achvd grants | w.mean w.p99  w.max | maxHOL starv miss  drop
    CPU    0.099   0.099   9903 |   32.7   208    464 |     24     -    0     0
    DMA    0.211   0.211   2632 |   68.0   265    392 |     10     -    0     0
    NPU    0.690   0.690  17261 |   10.6    25     25 |     18     -    0     0
```

NPU 가 늘 줄 서 있으니 FP 에선 DMA 차례가 오지 않는다(그랜트 0, 큐가 차서 drop 2120). RR 은 DMA 를 살리지만 CPU 의 p99 가 7 → 208 로 뛴다 — 공짜 점심은 없다.

**부하 스윕** (`./so` 실제 출력, T = 200000, 시드 1. 단일 스레드 결정적 모델이라 타이밍·load average 와 무관):

```text
  x    pol | CPU ach/off  p99   max | DMA ach/off  p99   maxHOL st | NPU ach/off  p99
  0.2  FP  | 0.029/0.029   14    15 | 0.073/0.073   113      14 -  | 0.100/0.100   14
  0.2  RR  | 0.029/0.029   22    38 | 0.073/0.073   113      10 -  | 0.100/0.100   14
  0.2  DRR | 0.029/0.029   42    55 | 0.073/0.073   108      16 -  | 0.100/0.100   38
  0.6  FP  | 0.089/0.089   15    16 | 0.220/0.220   312      30 -  | 0.300/0.300   18
  0.6  RR  | 0.089/0.089  149   290 | 0.220/0.220   287      10 -  | 0.300/0.300   17
  0.6  DRR | 0.089/0.089   69    77 | 0.220/0.220   293      46 -  | 0.300/0.300   50
  0.9  FP  | 0.134/0.134   15    17 | 0.328/0.329  1510      68 -  | 0.450/0.450   21
  0.9  RR  | 0.134/0.134 1380  1497 | 0.329/0.329   462      10 -  | 0.449/0.450  771
  0.9  DRR | 0.134/0.134  101   111 | 0.328/0.329  1471      96 -  | 0.450/0.450   67
  1.0  FP  | 0.149/0.149   15    17 | 0.349/0.371  9641      80 -  | 0.500/0.500   22
  1.0  RR  | 0.135/0.149 9371 10045 | 0.371/0.371   484      10 -  | 0.492/0.500 2922
  1.0  DRR | 0.149/0.149  110   113 | 0.357/0.371  6858     112 -  | 0.492/0.500 3164
  1.2  FP  | 0.179/0.179   15    17 | 0.221/0.435 37750     172 -  | 0.600/0.600   23
  1.2  RR  | 0.113/0.179 10943 11178 | 0.434/0.435   782      10 -  | 0.452/0.600 10842
  1.2  DRR | 0.179/0.179  111   138 | 0.351/0.435 23346     112 -  | 0.470/0.600 8809
  1.6  FP  | 0.239/0.239    8    12 | 0.000/0.561     0  199886 S  | 0.761/0.800 5440
  1.6  RR  | 0.088/0.239 13311 13311 | 0.558/0.561  2042      10 -  | 0.354/0.800 13310
  1.6  DRR | 0.239/0.239  123   182 | 0.326/0.561 25349     112 -  | 0.435/0.800 9600
```

(x = 0.4 / 0.8 행은 생략 — 전체는 `./so` 출력.) FP 순서는 CPU > NPU > DMA, DRR 가중치는 3:3:4 (quantum 48 / 48 / 64).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">CPU p99 wait (사이클, 로그 눈금) vs 부하 x — 위 표의 실제 값</text>
  <g stroke="currentColor" stroke-width="0.5" stroke-dasharray="3 3">
    <path d="M90 225 L690 225"/><path d="M90 175 L690 175"/><path d="M90 125 L690 125"/><path d="M90 75 L690 75"/>
  </g>
  <g stroke="currentColor"><path d="M90 30 L90 250 L700 250"/></g>
  <g fill="currentColor">
    <text x="58" y="229">10</text><text x="50" y="179">100</text><text x="42" y="129">1000</text><text x="34" y="79">10000</text>
    <text x="80" y="266">0.2</text><text x="166" y="266">0.4</text><text x="251" y="266">0.6</text><text x="337" y="266">0.8</text>
    <text x="423" y="266">1.0</text><text x="509" y="266">1.2</text><text x="680" y="266">1.6</text>
    <text x="330" y="288">offered 합 x (beat/cycle)</text>
  </g>
  <g fill="none" stroke="currentColor">
    <polyline stroke-width="3.5" points="90,208 176,193 261,166 347,131 390,118 433,76 519,73 690,69"/>
    <polyline stroke-width="2" stroke-dasharray="7 4" points="90,194 176,188 261,183 347,178 390,175 433,173 519,173 690,171"/>
    <polyline stroke-width="1.2" points="90,218 176,216 261,216 347,216 390,216 433,216 519,216 690,230"/>
  </g>
  <g fill="currentColor">
    <circle cx="433" cy="76" r="3"/><circle cx="433" cy="173" r="3"/><circle cx="433" cy="216" r="3"/>
    <text x="530" y="62">RR — 9371 @ x=1.0</text>
    <text x="530" y="164">DRR 3:3:4 — 110</text>
    <text x="530" y="210">FP (CPU 최상위) — 15</text>
    <text x="100" y="45">RR: 한 바퀴에 CPU 2 beat → 0.077 이 상한</text>
    <text x="100" y="60">x = 0.6 에서 CPU 0.09 를 넘으며 급등</text>
  </g>
</svg>
```

(RR = 굵은 실선, DRR = 점선, FP = 가는 실선.) 읽는 법:

- **x ≤ 0.6**: 모든 정책에서 achieved = offered (CHECK). 정책은 **지연 분포**만 바꾼다.
- **FP**: CPU 는 늘 p99 ≈ 15 = DMA 버스트 − 1 (비선점 막힘만). 대가는 맨 아래 DMA 가 치른다 — x = 1.2 에서 대역폭 부족(0.221/0.435), x = 1.6 에서 위 두 마스터의 합이 1.04 > 1 이 되며 **기아**.
- **RR**: 아무도 굶지 않고(HOL ≤ 24 를 모든 부하에서 CHECK), DMA 는 오히려 가장 잘 받는다. 하지만 짧은 버스트의 CPU 와 NPU 가 "그랜트 공정" 에 당한다.
- **DRR**: CPU 의 p99 가 부하와 거의 무관(42 → 123)하고 굶는 마스터도 없다. 과부하에서 손해는 **가중치 대비 많이 원한 마스터**(DMA, NPU)가 진다 — 원하는 동작이다.

### Part C — 마감 마스터, 토큰 버킷, QoS

```c
static void tb_tick(tb_t *b) { b->tokens += b->rate; if (b->tokens > b->cap) b->tokens = b->cap; }
static bool tb_ok(const tb_t *b, int len) { return b->tokens >= (double)len; }
static void tb_charge(tb_t *b, int len) { b->tokens = (b->tokens >= (double)len) ? b->tokens - len : 0.0; }

static int arb_qos(sim_t *s) {
    int best = -1; int64_t best_dl = 0;
    for (int m = 0; m < s->nm; m++) {                     /* ① 긴급 승격 → EDF */
        if (!pending(s, m) || s->cfg[m].deadline <= 0 || s->urg_after[m] < 0) continue;
        if (head_age(s, m) < s->urg_after[m]) continue;
        int64_t dl = head(s, m)->arr + s->cfg[m].deadline;
        if (best < 0 || dl < best_dl) { best = m; best_dl = dl; }
    }
    if (best >= 0) return best;
    int best_lvl = -1;                                    /* ② 레벨 최대 + 같은 레벨 RR */
    for (int k = 1; k <= s->nm; k++) {
        int m = (s->rr_last + k) % s->nm;
        if (!pending(s, m)) continue;
        int lvl = s->qos[m];
        if (s->regulated[m] && !tb_ok(&s->tb[m], head_len(s, m))) { if (s->hard_reg) continue; lvl = 0; }
        if (lvl > best_lvl) { best_lvl = lvl; best = m; }
    }
    if (best >= 0) s->rr_last = best;
    return best;
}
```

SoC 시나리오 (실제 출력, T = 200000 → CAM 요청 3125 개):

```text
== Part C: SoC — CAM(64 사이클마다 8 beat, 마감 48) · CPU 0.1 · DMA 0.2 · NPU backlog L=32, T = 200000 ==
  policy               | CAM miss max.tot | CPU ach/off  w.p99 | DMA ach/off maxHOL st | NPU ach | wc
  RR                   |      427      57 | 0.040/0.100  26910 | 0.193/0.193     42 -  | 0.642   | 0
  DRR 1:1:2:4          |     1860     119 | 0.100/0.100    181 | 0.192/0.193     96 -  | 0.583   | 0
  FP CAM>CPU>DMA>NPU   |        0      39 | 0.100/0.100     39 | 0.193/0.193     55 -  | 0.582   | 0
  FP CAM>CPU>NPU>DMA   |        0      39 | 0.100/0.100     39 | 0.000/0.193 199247 S  | 0.775   | 0
  QOS all lvl 4        |        0      47 | 0.099/0.100   2272 | 0.193/0.193     42 -  | 0.583   | 0
  QOS CPU lvl 8        |        0      47 | 0.100/0.100     39 | 0.193/0.193     64 -  | 0.582   | 0
== Part C: 같은 SoC, DMA 도 backlog (대량 마스터 둘) ==
  policy               | CAM miss max.tot | CPU w.p99 | DMA ach maxHOL st | NPU ach maxHOL st
  FP CAM>CPU>DMA>NPU   |        0      23 |        23 | 0.777     24 -  | 0.000 200000 S 
  FP CAM>CPU>NPU>DMA   |        0      39 |        39 | 0.000 200000 S  | 0.777     28 - 
  QOS CPU lvl 8        |        0      47 |        38 | 0.424     62 -  | 0.353    334 - 
```

```svg
<svg viewBox="0 0 740 268" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">CAM 최악 total (사이클) vs 마감 48 — 위 표의 실제 값 (막대 1 사이클 = 4 px)</text>
  <g stroke="currentColor">
    <rect x="180" y="34" width="228" height="22" class="f-warn"/>
    <rect x="180" y="64" width="476" height="22" class="f-warn"/>
    <rect x="180" y="94" width="156" height="22" class="f-ok"/>
    <rect x="180" y="124" width="156" height="22" class="f-ok"/>
    <rect x="180" y="154" width="188" height="22" class="f-ok"/>
    <rect x="180" y="184" width="188" height="22" class="f-ok"/>
  </g>
  <path d="M372 28 L372 214" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3"/>
  <g fill="currentColor">
    <text x="10" y="50">RR</text><text x="414" y="50">57 · miss 427 (13.7 %)</text>
    <text x="10" y="80">DRR 1:1:2:4</text><text x="600" y="102">119 · miss 1860</text>
    <text x="10" y="110">FP CAM&gt;CPU&gt;DMA&gt;NPU</text><text x="342" y="110">39</text>
    <text x="10" y="140">FP CAM&gt;CPU&gt;NPU&gt;DMA</text><text x="342" y="140">39 (대신 DMA 기아)</text>
    <text x="10" y="170">QOS all lvl 4</text><text x="376" y="170">47 (CPU p99 2272)</text>
    <text x="10" y="200">QOS CPU lvl 8</text><text x="376" y="200">47</text>
    <text x="350" y="232">마감 48</text>
    <text x="10" y="258">상한: RR 2+16+32+8 = 58 · FP 31+8 = 39 · QOS 7+32+8 = 47</text>
  </g>
</svg>
```

정책별로 무엇이 일어났나:

1. **RR — 마감을 놓친다.** CAM 의 최악 대기 = 다른 셋의 최대 버스트 합 2 + 16 + 32 = 50 → total 58 > 48. 실측 최악 57, 13.7 % 미스. 게다가 CPU 는 한 바퀴(≈ 50 사이클)에 2 beat 뿐이라 0.1 중 0.04 만 받는다 — "기아가 아닌데 굶는" 대역폭 부족.
2. **DRR — 몫은 공정, 마감은 모름.** NPU quantum 64 = 32-beat 버스트 두 개를 한 차례에 연달아 준다. 대역폭 공정성은 **시간 척도가 긴** 성질이라 48 사이클 마감 같은 짧은 척도의 약속을 못 한다.
3. **FP, CAM 최상위 — 마감은 지킨다.** 대기 ≤ 32 − 1 → total ≤ 39 (실측 39 = 상한). 그런데 나머지 순서가 문제다. NPU 가 DMA 위면 DMA 기아. DMA 도 backlog 인 변형에서는 **어느 순서든 대량 마스터 하나가 굶는다** (표 아래쪽). 고정 우선순위로는 두 backlog 마스터에게 동시에 최소 대역폭을 보장할 수 없다.
4. **QOS — 긴급 승격 + 레벨 + 규제기.** CAM 은 평소 DMA/NPU 와 같은 레벨 4 에서 RR 로 경쟁하다가, 머리 요청이 8 사이클 기다리면 긴급이 된다. 상한: 긴급이 되기 전 마지막 중재(나이 7)에서 남이 최대 32-beat 버스트를 잡을 수 있으므로 wait ≤ (8 − 1) + 32 = 39, total ≤ 47 ≤ 48 — 실측 47 로 **상한과 같다** (urg_after 를 9 까지 올려도 48 로 맞는다; 8 은 여유 1). 모든 레벨이 같으면(QOS all lvl 4) CPU 가 RR 의 "그랜트 공정" 에 다시 당한다(p99 2272) → CPU 를 레벨 8 로 올리면 39. DMA 까지 backlog 여도 아무도 굶지 않는다(DMA 0.424, NPU 0.353). NPU 의 maxHOL 334 는 토큰이 떨어져 레벨 0 으로 내려가 있는 시간이다 — 규제가 의도한 대기.

**마감 설계 공식** (연습 3 에서 L 스윕으로 확인): 비선점 자원에서 마감 마스터의 최악 total = (승격 지연 − 1) + (다른 마스터 최대 버스트) + (자기 L). 그러니 마감 D 를 지키려면 **남의 버스트 길이를 D − L_self − (승격 지연 − 1) 이하로 제한**해야 하고, 승격 지연 0(= 항상 최상위)이어도 Lmax_other ≤ D − L_self + 1. 이 모델에서 NPU 버스트 48 이상이면 어떤 중재 정책도 마감을 못 지킨다(연습 3: L = 48, 64 에서 FP 도 미스). 우선순위가 아니라 **버스트 분할**이 해법인 영역이다 — ARCH-M11 의 "UART 를 최고 우선순위로 올려도 overrun" 과 같은 구조.

**실제 칩과의 대응 (세부는 제품·스펙 확인 필요).**

- **AXI QoS**: AXI4 는 AR/AW 채널에 4-bit `ARQOS`/`AWQOS` 를 두고, 스펙은 이를 QoS 식별자로 정의하며 "값이 클수록 높은 우선순위" 로 쓰는 것을 권장 해석으로 든다고 알고 있다. 중재 알고리즘 자체는 인터커넥트 구현이 정한다 — 이 레슨의 `qos[m]` 레벨이 그 역할이다.
- **규제기(regulator)**: Arm CoreLink 계열 인터커넥트의 QoS 기능은 대역폭(기간당 트랜잭션), 미결 트랜잭션 수, 지연 목표 기반의 규제/동적 QoS 조정을 제공한다고 문서에 설명돼 있다 (QoS-400/NIC-400 등 — 이름·기능 범위는 TRM 확인 필요, 이번에 원문 미확인). 이 레슨의 토큰 버킷은 그 중 "대역폭 규제" 의 최소 모델이다.
- **긴급 승격**: 디스플레이/카메라 컨트롤러가 라인 버퍼 FIFO 수위가 문턱 아래로 내려가면 더 높은 QoS 값을 내보내는 방식이 흔히 쓰인다고 알려져 있다(구체 신호는 SoC 마다 다름). 이 레슨은 FIFO 수위 대신 "머리 요청 나이" 를 썼다 — 같은 아이디어(남은 여유 = slack 이 줄면 승격).
- **비선점의 의미**: 이 모델의 그랜트는 "버스트 하나 동안 포트 독점" 이다 — 단일 포트 메모리 컨트롤러나 공유 버스(ARCH-B12 대기 상태, ARCH-M13 AHB-Lite 의 버스트)에 가깝다. AXI(ARCH-M14)는 주소 채널 중재와 데이터 비트가 분리돼 있고 ID 가 다른 읽기 데이터는 섞일 수 있어(인터리빙) "막힘" 의 단위가 달라진다; 재정렬은 SOCX-M02 의 주제. 멀티 매니저 AHB 처럼 중재기가 진행 중인 버스트를 일찍 끊을 수 있는 프로토콜도 있다(세부는 해당 스펙 확인).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so          # 53/53 passed, 약 0.2 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_san && ./so_san   # 깨끗, 53/53
```

(Apple M6, Apple clang 21.0.0. 측정 시점 `uptime` load average 7.18 → 다른 세션 때문에 한때 90 까지 — 단일 스레드 결정적 시뮬레이터라 출력은 load 와 무관하고, 두 번 실행한 출력이 바이트 단위로 같다(`cmp`). 타이밍 측정·demo 파일 없음, 스레드 없음 → TSan 해당 없음.)

바뀐 곳은 중재기 4 개와 토큰 버킷 3 줄뿐 — 정책 하나가 **10 줄 안팎**이다. 나머지 500 줄은 "정책을 비교 가능한 숫자로 만드는" 하네스: 트래픽 생성기, 큐, 지표 정의, work-conserving 카운터, censored 요청 처리, 표. 설계 메모:

- **중재기는 요청이 있을 때만 불린다.** 그래서 −1 반환 = 곧 work-conserving 위반으로 셀 수 있다. 중재기가 요청 없는 마스터를 고르면 `bad_grant` 로 따로 센다.
- **HOL 기준점을 하네스가 들고 있다** (`last_end`). 중재기 구현과 독립적으로 상한을 검사하기 위해서다 — 검사 대상이 자기 검사에 필요한 값을 계산하게 두지 않는다.
- **끝까지 못 받은 요청을 버리지 않는다.** 시뮬레이션 끝에 큐에 남은 요청의 나이로 HOL · 마감 미스를 갱신한다. 이걸 빼면 FP 의 DMA 기아가 "그랜트 0, 지연 0" 으로 보인다.
- **결정적**: xorshift32 고정 시드, 마스터 번호 순 도착 생성, 같은 입력 → 같은 출력.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 / 방법 |
|---|---|---|
| RR 포인터를 갱신하지 않음 | 늘 0 번부터 찾으니 = 고정 우선순위. 지시형 트레이스가 `00111100222` 로 FP 와 같아진다 | `if (pending(s, m)) { s->rr_last = m; return m; }` |
| RR 이 `rr_last + 1` 하나만 보고 없으면 −1 | 기능은 돌지만 자원이 놀며 처리량을 잃는다 → `wc_viol` > 0 | k = 1..nm 한 바퀴 전체를 본다 (자기 자신은 맨 끝) |
| "WRR = 그랜트 횟수를 가중치만큼" | 대역폭은 w·L 비례가 된다(연습 1: 3:3:4 인데 CPU 0.070, DRR 은 0.300). 버스트 길이가 다른 마스터끼리는 가중치가 뜻을 잃는다 | 가중치를 beat(바이트)로 세는 DRR, 또는 가중치에 L 을 반영 |
| DRR 에서 빈 큐의 잔고를 남겨 둠 | 쉬던 마스터가 돌아와 저축한 잔고로 연달아 받는다 → 남의 지연 상한이 깨진다 | `if (!pending(s, m)) s->dc[m] = 0;` |
| p99 을 그랜트된 요청만으로 내고 끝 | 기아 상태의 요청은 영원히 그랜트되지 않아 통계에서 **사라진다** (FP 의 DMA: 그랜트 0 → p99 0). 생존 편향(censoring) | 남은 요청의 나이로 HOL/미스를 갱신하고 starve 플래그를 따로 본다 |
| 마감 마스터를 최고 우선순위로 올리면 끝이라고 생각 | 비선점 버스트 때문에 최악 = Lmax_other − 1 + L_self. NPU 버스트 48 이상이면 FP 최상위도 미스 (연습 3) | 남의 버스트 길이 제한 + 승격 시점 설계 (D − L_self − Lmax_other + 1) |
| 규제기 rate 를 "보장 대역폭" 으로 설명 | rate 는 **상한**이다. hard 모드 NPU 는 0.5 가 아니라 0.47 — 남에게 막혀 있는 동안 토큰이 cap 에서 버려진다 (연습 2) | 보장은 RR/DRR 몫이나 레벨로, 상한은 규제기로 — 역할을 나눈다 |

## 8. 면접 Q&A

<details><summary>Q1. 라운드로빈 중재의 최악 대기 시간은? 증명해 보라.</summary>

비선점 그랜트, 중재 지연 0 이면 마스터 i 의 HOL 대기 ≤ Σ_{j≠i} Lmax_j. 대기가 시작될 때 자원을 쥔 마스터 j 에서 포인터가 출발해 i 에 도착하기 전까지 각 마스터를 최대 한 번 지나기 때문이다 (j 자신은 다시 안 옴). 셋 다 backlog 이면 정확히 그 값이 나온다 — 실측 CPU 24 = 16 + 8. 꼬리질문: "그럼 wait(도착 → 그랜트)의 상한은?" → 자기 큐에 앞선 요청이 q 개면 대략 q × (자기 L + Σ others) 가 더해진다. 자기 도착률이 RR 서비스율(L_i / ΣL)을 넘으면 큐가 무한히 길어진다 — 상한은 HOL 에만 있다. "중재 지연이 1 사이클이면?" → 그랜트마다 1 사이클을 더하거나, 중재를 이전 버스트의 마지막 beat 와 겹치는(파이프라인) 설계를 한다.
</details>

<details><summary>Q2. 최고 우선순위 마스터도 기다리나? 0 으로 만들 수 있나?</summary>

기다린다. 남의 버스트가 진행 중이면 끝날 때까지 — 최악 = 다른 마스터 최대 버스트 − 1 (도착 사이클에 시작된 버스트는 최고 우선순위가 이겼을 것이므로 −1). 이 레슨 무작위 실행 최악 15 = 16 − 1, CAM 최상위 FP 최악 total 39 = 31 + 8. 0 으로 만들려면 선점(진행 중 버스트를 끊고 나중에 이어 줌 — 프로토콜·슬레이브가 지원해야 함)이나 남의 버스트 제한밖에 없다. 꼬리질문: "그럼 버스트를 1 beat 로 하면?" → 지연은 최소지만 버스트마다 주소 단계·DRAM 행 전환·중재 오버헤드가 붙어 효율이 떨어진다. 실제 설계는 실시간 마스터의 마감에서 역산해 버스트 상한을 정한다 (ARCH-M11 의 ⌈N/P⌉ 와 같은 계산).
</details>

<details><summary>Q3. 라운드로빈은 공정한가?</summary>

"그랜트 횟수" 에 대해서만. 셋 다 backlog 일 때 대역폭 몫은 L_i / ΣL — DMA(16 beat)가 CPU(2 beat)의 8 배. 그래서 짧은 버스트의 지연 민감 마스터가 과부하에서 가장 먼저 무너진다: 스윕에서 RR 의 CPU p99 는 x = 0.6 에 149, x = 1.0 에 9371 (FP 15, DRR 110). Part C 에서는 CPU 가 0.1 중 0.04 만 받았다. 꼬리질문: "그럼 어떻게?" → beat 단위 가중 RR(DRR)로 대역폭을 가중치 비례로 만들거나, 지연 민감 마스터를 높은 레벨에 두고 레벨 안에서만 RR (QOS CPU lvl 8: p99 39).
</details>

<details><summary>Q4. 가중 RR 을 정확히 정의하라. 가중치 4:2:1 이면 정말 4:2:1 을 받나? 오차는?</summary>

이 레슨의 정의는 beat 단위 DRR: 차례마다 잔고 += w·q, 잔고로 머리 요청을 감당하는 동안 연속 그랜트, 빈 큐면 잔고 0. 모두 backlog 일 때 차례 후 잔고가 [0, L_i) 라는 불변식에서 |S_i − (w_i/Σw)·T| < 2Q_i + L_i + Q_i·ΣL/ΣQ (+ 경계의 잘린 버스트). 상수라서 오차는 1/T 로 줄고, 실측 오차 ≤ 9 × 10⁻⁵ (상한 8 × 10⁻⁴). 한 마스터가 몫보다 덜 원하면 그만큼은 다른 마스터들에게 가중치 비율로 넘어간다(work-conserving) — CPU 0.1, DMA/NPU = 0.7504. 꼬리질문: "quantum 을 키우면?" → 몫 정밀도는 그대로, 한 차례가 길어져 지연이 는다: CPU 최악 wait 112 = 다른 quantum 합. 그래서 가중치는 작게, 단위 q 는 최대 버스트 근처로.
</details>

<details><summary>Q5. 디스플레이(또는 카메라)가 마감을 놓친다. 무엇을 보고, 무엇을 바꾸나?</summary>

먼저 숫자: 마감 D, 자기 버스트 L_self, 같은 포트에서 다른 마스터가 잡을 수 있는 최대 비선점 시간 Lmax_other, 승격까지 걸리는 시간. 최악 total = (승격 − 1) + Lmax_other + L_self ≤ D 여야 한다. 이 모델: RR 이면 2 + 16 + 32 + 8 = 58 > 48 → 13.7 % 미스, 승격 8 이면 47 → 0. 순서대로 바꾸는 것: (1) 실시간 마스터에 높은 QoS 또는 FIFO 수위 기반 긴급 승격, (2) 대량 마스터의 버스트 길이 상한 (NPU 48 이상이면 어떤 정책도 불가능 — 연습 3), (3) 대량 마스터 대역폭 규제, (4) 라인 버퍼 깊이를 늘려 D 자체를 키움. 꼬리질문: "평균 대역폭은 충분한데요?" → 평균은 무관하다. 언더런은 **최악 지연** 문제이고, 평균 대역폭은 필요조건일 뿐이다.
</details>

<details><summary>Q6. 대역폭 규제기 soft vs hard — 언제 무엇을?</summary>

soft(토큰이 없으면 레벨 0 으로 강등)는 work-conserving: 아무도 원하지 않는 대역폭은 써도 된다 → NPU 0.582. hard(토큰 없으면 자원이 놀아도 안 줌)는 work-conserving 이 깨진다 — 연습 2 에서 22431 사이클 동안 요청이 있는데 포트가 놀았고 NPU 는 0.470. 그래도 hard 를 쓰는 이유: **격리와 예측 가능성** (다른 마스터의 최악을 NPU 의 순간 폭주와 무관하게 보장, 전력·온도 상한, 보안상 타이밍 채널 축소). 꼬리질문: "rate 0.5 로 걸었는데 왜 0.47?" → rate 는 상한이지 보장이 아니다. NPU 가 남에게 막혀 있는 동안 토큰이 cap(64)에서 넘쳐 버려진다. 보장은 RR/DRR 몫이나 레벨로 따로 만들어야 한다.
</details>

<details><summary>Q7. 이 중재기를 RTL/실리콘에서 검증한다면? (silicon validation 질문)</summary>

이 레슨의 검사가 그대로 체크리스트다. (1) 기능: 지시형 시나리오의 그랜트 순서 문자열 비교. (2) 안전 성질: work-conserving(요청이 있으면 다음 사이클 안에 그랜트), one-hot 그랜트, 요청 없는 마스터에게 그랜트 금지 — SVA assertion 으로 매 사이클 (RTLDV-M03 에서 RTL). (3) 활성(liveness) 성질: RR 은 "요청 후 Σ others Lmax 안에 그랜트" 를 bounded liveness 로 검사, 기아 카운터(HOL 최대)를 성능 카운터로. (4) 성능: 무작위 트래픽 + 같은 시드의 골든 모델(이 레슨 코드)과 대역폭 몫·지연 분포 비교 — 사이클이 정확히 같을 필요는 없고 상한·허용오차로. (5) 실리콘: 인터커넥트 PMU 의 포트별 바이트·지연 카운터로 실제 부하에서 몫과 최악을 잰다. 꼬리질문: "상한 검사를 통과하면 다 된 건가?" → 상한은 시나리오에 의존한다. 상한이 **달성되는** 시나리오(셋 다 backlog)를 반드시 하나 넣어야 상한 계산 자체가 틀렸을 때 잡힌다.
</details>

<details><summary>Q8. 고정 우선순위가 나쁘기만 한가? 언제 쓰나?</summary>

아니다. 가장 단순하고(우선순위 인코더 하나 — ARCH-B05), 최상위의 지연이 **가장 작고 예측 가능**하다(L − 1). 총 부하가 충분히 작다는 것을 보증할 수 있거나(높은 마스터들의 합 < 1 − 여유) 낮은 마스터가 "남는 것만 써도 되는" 배경 작업이면 좋은 선택이다. 위험은 높은 마스터의 부하가 설계 가정보다 커질 때 아래가 굶는 것 — 스윕에서 위 둘의 합이 1.04 가 되자 DMA 그랜트 0. 대량 마스터 둘(DMA, NPU)을 동시에 살리는 것은 FP 로 불가능하다(Part C 아래 표). 실무 절충은 "레벨은 고정, 레벨 안은 RR" + 오래 기다린 요청의 승격(aging) — ARCH-M11 의 DMA 채널 중재와 이 레슨의 QOS 가 그 형태다.
</details>

## 9. 추가 연습

1. **credit WRR vs DRR.** 가중치를 "한 프레임에 받을 그랜트 수" 로 세는 WRR(크레딧 w_i, 요청 있는 마스터의 크레딧이 모두 0 이면 전원 재충전)을 `ARB_EXT` 로 구현하고, 셋 다 backlog(L = 2 / 16 / 8), 가중치 3:3:4 에서 대역폭 몫을 식으로 예측하라.
   <details><summary>답</summary>

   한 프레임 = 각자 w_i 번 그랜트 → beat 로 w_i·L_i. 몫 = w_i·L_i / Σ w_j·L_j = 6/86, 48/86, 32/86 = 0.0698, 0.5581, 0.3721. 실측 0.0698 / 0.5582 / 0.3721. 같은 가중치의 DRR 은 0.3000 / 0.3000 / 0.4000. 가중치 설계자가 "CPU 30 %" 를 의도했다면 credit WRR 은 CPU 에 7 % 만 준다 — 마스터마다 버스트 길이가 다르면 가중치를 beat(또는 바이트)로 세야 한다.
   </details>
2. **soft vs hard 규제기.** Part C 의 `QOS CPU lvl 8` 에서 NPU 규제를 hard 로 바꿔 NPU 대역폭, 유휴 사이클, work-conserving 위반, CAM 미스를 비교하라. NPU 의 장기 대역폭 상한을 식으로.
   <details><summary>답</summary>

   soft: NPU 0.5821, 유휴 0, wc_viol 0. hard: NPU 0.4700, 유휴 22431 사이클 = wc_viol 22431 (요청이 있는데 포트가 놂), CAM 미스 둘 다 0. 상한 = rate + cap / T = 0.5 + 64/200000 (처음 꽉 찬 버킷만큼 더). 0.47 < 0.5 인 이유: NPU 가 DMA/CAM 버스트에 막혀 있는 동안 토큰이 cap 에서 버려진다 — rate 는 보장이 아니라 상한.
   </details>
3. **NPU 버스트 길이 vs CAM 마감.** NPU L ∈ {16, 24, 32, 40, 48, 64} 에서 (a) FP CAM 최상위, (b) QOS urg_after 8, (c) QOS urg_after 를 L 에 맞춰 다시 고른 경우의 CAM 최악 total 을 식으로 예측하고 측정과 비교하라. 어떤 L 부터 어떤 중재기로도 불가능한가?
   <details><summary>답</summary>

   Lo = max(L_npu, 16) (DMA 16 이 있으니). (a) (Lo − 1) + 8, (b) 7 + Lo + 8, (c) u = max(0, 48 − 8 − Lo + 1) → (u − 1) + Lo + 8 (u = 0 이면 (a) 와 같음). 측정 = 예측 상한 (a)(b) 모든 행에서 **정확히 같음** — 상한이 실제로 달성된다. (b) 는 L = 40 에서 55 > 48 → 미스 176; (c) 는 u = 1 로 47 → 0. L ≥ 48 이면 (a) 도 55 > 48 → 미스 313 (L 48), 786 (L 64). 결론: L_npu ≤ D − L_cam + 1 = 41 이 **어떤 중재로도 넘을 수 없는 선**이고, 그 위는 버스트 분할로만 해결된다.
   </details>
4. **Little 의 법칙으로 통계 회계 검증.** x = 0.9 혼합 트래픽을 T = 100000 동안 돌린 뒤 도착을 끊고 모든 큐가 빌 때까지(drain) 돌려라. 매 사이클 큐 길이의 합(넓이)과 모든 요청의 wait 합이 같아야 하는 이유는? L_q = λ·W_q 를 확인하라.
   <details><summary>답</summary>

   요청 하나는 도착 사이클 a 부터 그랜트 사이클 g − 1 까지 매 사이클 큐 길이에 1 씩 기여한다 → 정확히 wait = g − a 만큼. 시작과 끝이 비어 있으면 넓이 = Σ wait (항등식). 넓이/총 사이클 = L_q, 요청 수/총 사이클 = λ, Σ wait/요청 수 = W_q 이므로 L_q = λ·W_q 가 반올림 없이 성립. 실측: FP 582565 = 582565, RR 1731761 = 1731761, DRR 799401 = 799401 (모두 총 100076 사이클, λ = 0.14282). 같은 λ 에서 정책은 W_q(평균 큐 대기) 만 바꾼다 — RR 이 FP 의 3 배. 하네스의 지연 회계에 버그(예: 그랜트 사이클을 한 번 더 센다)가 있으면 이 등식이 깨진다.
   </details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr      # solution.c 를 포함하므로 같은 폴더에서
```

실제 출력:

```text
== 연습 1: credit WRR vs DRR, 가중치 3:3:4, 셋 다 backlog (L = 2 / 16 / 8) ==
         credit-WRR  (w*L/sum)   DRR    (w/sum)
  CPU    0.0698     (0.0698)     0.3000 (0.3000)
  DMA    0.5582     (0.5581)     0.3000 (0.3000)
  NPU    0.3721     (0.3721)     0.4000 (0.4000)
== 연습 2: QOS(CPU lvl 8) 에서 NPU 규제 soft vs hard (토큰 0.5 beat/cycle, cap 64) ==
  soft  NPU ach 0.5821  idle      0  wc_viol      0  CAM miss 0
  hard  NPU ach 0.4700  idle  22431  wc_viol  22431  CAM miss 0
== 연습 3: NPU 버스트 L 스윕 — CAM 최악 총지연 예측 vs 측정 (마감 48) ==
  L_npu | FP_A bound  max  miss | QOS u=8 bound  max  miss | QOS u=adapt  u bound  max  miss
     16 |         23   23     0 |            31   31     0 |          25     48   47     0
     24 |         31   31     0 |            39   39     0 |          17     48   47     0
     32 |         39   39     0 |            47   47     0 |           9     48   47     0
     40 |         47   47     0 |            55   55   176 |           1     48   47     0
     48 |         55   55   313 |            63   63   453 |           0     55   55   310
     64 |         71   71   786 |            79   79   907 |           0     71   71   795
== 연습 4: Little 의 법칙 (x = 0.9, T = 100000 후 drain) ==
  FP  cycles 100076  area 582565  sum wait 582565  | Lq 5.8212 = lambda 0.14282 x Wq 40.759 = 5.8212
  RR  cycles 100076  area 1731761  sum wait 1731761  | Lq 17.3045 = lambda 0.14282 x Wq 121.161 = 17.3045
  DRR cycles 100076  area 799401  sum wait 799401  | Lq 7.9879 = lambda 0.14282 x Wq 55.930 = 7.9879
10/10 passed
```

(연습 3 의 (c) 열에서 u 를 상한이 정확히 48 이 되도록 골랐는데 측정 최악은 47 — 상한은 안전하지만 이 트래픽에서는 1 사이클 덜 나빴다. (a)(b) 열은 상한이 그대로 달성됐다.) practice.c 도 ASan/UBSan 빌드에서 깨끗했다 (10/10).

## 10. 핵심 정리

- 지연은 세 가지로 나눠 잰다: wait(도착 → 그랜트), total(→ 완료), **HOL(남 때문에만)**. 상한과 기아는 HOL 로, 대역폭 부족은 achieved/offered 와 큐 길이로 본다. 못 받은 요청을 통계에서 빼면 기아가 "지연 0" 으로 보인다.
- 비선점 버스트: 최고 우선순위의 최악 = Lmax_other − 1. 우선순위로는 이 아래로 못 내린다 — 버스트 제한이 답.
- RR: HOL ≤ Σ_{j≠i} Lmax_j (빡빡함, backlog 에서 24 = 16 + 8). 기아 없음. 하지만 **그랜트 공정**이라 대역폭은 L 비례 → 짧은 버스트의 CPU 가 과부하에서 무너진다 (p99 9371).
- 가중 RR 은 beat 단위 DRR 로: 몫 = w/Σw, 오차 < (2Q_i + L_i + Q_i ΣL/ΣQ)/T. quantum 이 클수록 지연(≈ Σ others' Q)이 커진다. 그랜트 횟수로 센 WRR 은 w·L 비례.
- 고정 우선순위: 최상위에 가장 좋고 단순, 대신 위의 합이 1 을 넘으면 아래가 굶는다. backlog 대량 마스터 둘을 동시에 살릴 수 없다.
- 마감 마스터: 최악 total = (승격 − 1) + Lmax_other + L_self ≤ D. RR(58) · DRR(119) 는 실패, FP 최상위(39) · 긴급 승격 8(47)은 성공. L_other > D − L_self + 1 이면 어떤 중재도 불가능.
- QoS = 레벨(AxQOS 류) + 긴급 승격(slack 기반) + 규제기(토큰 버킷, rate 는 상한이지 보장 아님). soft 는 work-conserving, hard 는 격리를 위해 그걸 포기한다.

## 참고자료

- M. Shreedhar, G. Varghese, "Efficient Fair Queuing Using Deficit Round-Robin", *IEEE/ACM Transactions on Networking* 4(3), 1996 (SIGCOMM '95) — DRR 정의와 공정성 상한(잔고 < 최대 패킷 길이).
- J. D. C. Little, "A Proof for the Queuing Formula: L = λW", *Operations Research* 9(3), 1961.
- Arm, *AMBA AXI and ACE Protocol Specification* (IHI 0022) — AxQOS 신호 정의와 권장 해석 (절 번호는 판마다 다름, 이번에 원문 미확인).
- Arm, CoreLink NIC-400 / QoS-400 Technical Reference Manual — 인터커넥트 QoS 규제(대역폭·미결 트랜잭션·지연) 기능 설명 (제품별 기능 범위 확인 필요, 이번에 원문 미확인).
- W. J. Dally, B. Towles, *Principles and Practices of Interconnection Networks*, Morgan Kaufmann, 2004 — 18 장 전후 중재기(round-robin, matrix arbiter)와 공정성 (장 번호 기억 기준).
- J. L. Hennessy, D. A. Patterson, *Computer Architecture: A Quantitative Approach*, 6th ed. — 부록의 인터커넥트/버스 중재 개요.
- 이 노트의 관련 레슨: ARCH-M11 (DMA 엔진 안의 채널 중재, 비선점 버스트, ⌈N/P⌉), ARCH-B05 (우선순위 인코더 = 고정 우선순위 하드웨어), ARCH-B12 (버스 대기 상태), ARCH-M13 (AHB-Lite/APB), ARCH-M14 (AXI valid/ready). 다음 단계: RTLDV-M03 (중재기 RTL + SVA), SIM-M05 (인터커넥트 경합 시뮬레이션), SOCX-M02 (AXI ID 재정렬 버퍼).
