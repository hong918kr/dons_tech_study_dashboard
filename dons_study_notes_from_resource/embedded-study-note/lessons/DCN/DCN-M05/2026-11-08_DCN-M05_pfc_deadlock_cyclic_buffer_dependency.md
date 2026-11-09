---
id: DCN-M05
time: 90분
updated: 2026-11-08
---
# DCN-M05 PFC 데드락: 순환 버퍼 의존성 탐지

## 학습 목표

- PFC(Priority-based Flow Control, IEEE 802.1Qbb)의 PAUSE가 홉마다 거꾸로 퍼지는 구조에서 **버퍼 의존 그래프(BDG, buffer dependency graph)** 를 정의하고, 노드를 왜 `(스위치, 입력 포트, 우선순위)` 로 잡아야 하는지 설명할 수 있다.
- leaf-spine에서 **up-down 라우팅이면 BDG가 비순환** 임을 증명하고, 링크 장애 + 재라우팅으로 생긴 bounce 경로(leaf→spine→leaf→spine)가 만드는 **순환 버퍼 의존(CBD, cyclic buffer dependency)** 을 DFS로 찾아 출력할 수 있다.
- 사이클 단위 시뮬레이터로 데드락을 재현하고, **혼잡과 데드락을 구분하는 증상** ("소스가 멈춘 뒤에도 처리량 0, 패킷은 갇혀 있고 drop은 0")을 말할 수 있다.
- 세 가지 대책(up-down 강제 · 홉별 우선순위 상승 · PFC watchdog)을 **실제 시뮬레이션 숫자** 로 비교하고, 각각이 Coffman의 네 조건 중 무엇을 깨는지 짝지을 수 있다.
- 같은 "순환 대기"가 OS 락(OS-M10/M11), DB(STOR-M08), 칩 NoC(SOCX-M05)에도 그대로 나온다는 것을 그래프 하나로 설명할 수 있다.

## 1. 문제

RoCEv2로 GPU 클러스터를 묶는 AI 학습 패브릭은 **무손실(lossless) 이더넷** 을 쓰고, 그 무손실은 PFC가 만든다. 그런데 PFC는 "버리지 않는 대신 기다리게 하는" 장치라서, 기다림이 고리를 이루면 패브릭 일부가 **영원히** 멈춘다. NVIDIA/Meta/클라우드 네트워크 면접에서 "PFC 데드락이 왜 생기고 어떻게 막나요?"는 단골 질문이고, 답의 뼈대는 OS 데드락과 똑같다.

PFC 복습 (자세한 헤드룸 계산은 DCN-M04): 스위치는 **입력 포트 × 우선순위** 마다 버퍼 사용량을 센다. 그 값이 XOFF를 넘으면 그 포트의 상류(upstream) 장비에 "우선순위 3 보내지 마" PAUSE 프레임을 보내고, XON 아래로 내려가면 다시 보내도 된다고 알린다. PAUSE를 받은 상류는 그 우선순위 패킷을 그 링크로 **내보내지 못하고 자기 버퍼에 쥐고 있는다** — 그래서 상류 버퍼도 차고, 그 상류의 상류로 PAUSE가 번진다 (PAUSE 전파, backpressure).

이 레슨의 장면: 4 leaf × 2 spine 패브릭에서 링크 **S0–L3** 과 **S1–L0** 이 동시에 죽었다. 라우팅이 수렴하기 전, leaf는 아직 옛 ECMP 선택(spine)을 쓰고, 목적지 링크를 잃은 spine은 로컬 복구로 이웃 leaf에 패킷을 **되튕긴다(bounce)**.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">4 leaf × 2 spine, 링크 S0–L3 · S1–L0 장애 → bounce 경로 2개</text>
  <rect x="200" y="40" width="80" height="34" class="f-acc" stroke="currentColor"/>
  <text x="240" y="62" fill="currentColor" text-anchor="middle">S0</text>
  <rect x="460" y="40" width="80" height="34" class="f-acc" stroke="currentColor"/>
  <text x="500" y="62" fill="currentColor" text-anchor="middle">S1</text>
  <g stroke="currentColor" stroke-width="1.5">
    <line x1="240" y1="74" x2="100" y2="190"/><line x1="240" y1="74" x2="280" y2="190"/>
    <line x1="240" y1="74" x2="460" y2="190"/>
    <line x1="500" y1="74" x2="280" y2="190"/><line x1="500" y1="74" x2="460" y2="190"/>
    <line x1="500" y1="74" x2="640" y2="190"/>
  </g>
  <g stroke="currentColor" stroke-width="1.5" stroke-dasharray="5 4">
    <line x1="240" y1="74" x2="640" y2="190"/>
    <line x1="500" y1="74" x2="100" y2="190"/>
  </g>
  <circle cx="580" cy="173" r="10" class="f-warn" stroke="currentColor"/>
  <text x="580" y="177" fill="currentColor" text-anchor="middle" font-size="12">✕</text>
  <circle cx="160" cy="173" r="10" class="f-warn" stroke="currentColor"/>
  <text x="160" y="177" fill="currentColor" text-anchor="middle" font-size="12">✕</text>
  <rect x="60" y="190" width="80" height="34" class="f-mut" stroke="currentColor"/>
  <text x="100" y="212" fill="currentColor" text-anchor="middle">L0</text>
  <rect x="240" y="190" width="80" height="34" class="f-warn" stroke="currentColor"/>
  <text x="280" y="212" fill="currentColor" text-anchor="middle">L1 (bounce)</text>
  <rect x="420" y="190" width="80" height="34" class="f-warn" stroke="currentColor"/>
  <text x="460" y="212" fill="currentColor" text-anchor="middle">L2 (bounce)</text>
  <rect x="600" y="190" width="80" height="34" class="f-mut" stroke="currentColor"/>
  <text x="640" y="212" fill="currentColor" text-anchor="middle">L3</text>
  <text x="10" y="258" fill="currentColor">F1 (L2 → L3): L2 → S0 → (S0–L3 끊김) → L1 → S1 → L3      호스트 2대 × 선로 속도</text>
  <text x="10" y="278" fill="currentColor">F2 (L1 → L0): L1 → S1 → (S1–L0 끊김) → L2 → S0 → L0      호스트 2대 × 선로 속도</text>
  <text x="10" y="302" fill="currentColor" font-size="12">두 경로 모두 "올라갔다 → 내려갔다 → 다시 올라감" = up-down 위반. 수렴한 라우팅이면 F1 은 L2 → S1 → L3,</text>
  <text x="10" y="320" fill="currentColor" font-size="12">F2 는 L1 → S0 → L0 로 간다 (대책 1). 점선 = 장애 링크.</text>
</svg>
```

**버퍼 의존 그래프.** 노드는 버퍼 `(스위치, 입력 포트, 우선순위)`, 간선 `a → b` 는 "a에 있는 패킷이 다음 홉에서 b에 들어가야 한다" — 즉 a의 자리를 **쥔 채** b의 자리를 **기다릴 수 있다**. F1이 지나가는 버퍼는 `(L2,h0) → (S0,L2) → (L1,S0) → (S1,L1) → (L3,S1)`, F2는 `(L1,h0) → (S1,L1) → (L2,S1) → (S0,L2) → (L0,S0)`. 두 사슬을 겹치면 고리가 닫힌다.

```svg
<svg viewBox="0 0 740 366" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">BDG 의 4-노드 사이클: 넷 다 상류에 PAUSE 중 → 아무도 못 움직인다</text>
  <rect x="150" y="50" width="150" height="44" class="f-warn" stroke="currentColor"/>
  <text x="225" y="70" fill="currentColor" text-anchor="middle">(S0, L2, p3)</text>
  <text x="225" y="87" fill="currentColor" text-anchor="middle" font-size="12">F1·F2 섞임, 9 개</text>
  <rect x="440" y="50" width="150" height="44" class="f-warn" stroke="currentColor"/>
  <text x="515" y="70" fill="currentColor" text-anchor="middle">(L1, S0, p3)</text>
  <text x="515" y="87" fill="currentColor" text-anchor="middle" font-size="12">F1 만, 5 개</text>
  <rect x="440" y="200" width="150" height="44" class="f-warn" stroke="currentColor"/>
  <text x="515" y="220" fill="currentColor" text-anchor="middle">(S1, L1, p3)</text>
  <text x="515" y="237" fill="currentColor" text-anchor="middle" font-size="12">F1·F2 섞임, 9 개</text>
  <rect x="150" y="200" width="150" height="44" class="f-warn" stroke="currentColor"/>
  <text x="225" y="220" fill="currentColor" text-anchor="middle">(L2, S1, p3)</text>
  <text x="225" y="237" fill="currentColor" text-anchor="middle" font-size="12">F2 만, 5 개</text>
  <g stroke="currentColor" stroke-width="2">
    <line x1="300" y1="66" x2="432" y2="66"/><line x1="530" y1="94" x2="530" y2="192"/>
    <line x1="440" y1="228" x2="308" y2="228"/><line x1="210" y1="200" x2="210" y2="102"/>
  </g>
  <polygon points="440,66 430,61 430,71" fill="currentColor"/>
  <polygon points="530,200 525,190 535,190" fill="currentColor"/>
  <polygon points="300,228 310,223 310,233" fill="currentColor"/>
  <polygon points="210,94 205,104 215,104" fill="currentColor"/>
  <text x="370" y="58" fill="currentColor" text-anchor="middle" font-size="12">F1: S0 → L1</text>
  <text x="540" y="150" fill="currentColor" font-size="12">F1: L1 → S1 (bounce)</text>
  <text x="370" y="248" fill="currentColor" text-anchor="middle" font-size="12">F2: S1 → L2</text>
  <text x="80" y="150" fill="currentColor" font-size="12">F2: L2 → S0 (bounce)</text>
  <g stroke="currentColor" stroke-width="1" stroke-dasharray="4 3">
    <line x1="432" y1="80" x2="308" y2="80"/><line x1="500" y1="192" x2="500" y2="102"/>
    <line x1="308" y1="214" x2="432" y2="214"/><line x1="240" y1="102" x2="240" y2="192"/>
  </g>
  <text x="370" y="96" fill="currentColor" text-anchor="middle" font-size="12">PAUSE</text>
  <text x="370" y="208" fill="currentColor" text-anchor="middle" font-size="12">PAUSE</text>
  <text x="10" y="280" fill="currentColor" font-size="12">실선 = 패킷이 가려는 방향(의존), 점선 = PAUSE 가 거꾸로 가는 방향.</text>
  <text x="10" y="298" fill="currentColor" font-size="12">숫자 = 시뮬레이터가 멈춘 뒤의 실제 점유 (occ=[5, 9, 5, 9]).</text>
  <text x="10" y="316" fill="currentColor" font-size="12">(S0,L2) 맨 앞 F1 은 (L1,S0) 을, (L1,S0) 은 (S1,L1) 을 기다리고,</text>
  <text x="10" y="334" fill="currentColor" font-size="12">(S1,L1) 맨 앞 F2 는 (L2,S1) 을, (L2,S1) 은 (S0,L2) 를 기다린다.</text>
  <text x="10" y="352" fill="currentColor" font-size="12">5 개짜리 버퍼도 PAUSE 중: XON=4 아래로 내려가야 풀리는데, 아무것도 안 나가니 못 내려간다.</text>
</svg>
```

**같은 순환 대기, 다른 층.** 이 레슨의 그래프는 OS의 wait-for 그래프와 같은 물건이다. 자원 이름만 바뀐다.

| 층 | 자원 (노드) | "쥔 채 기다림" (간선) | 사이클을 끊는 대표 방법 | 레슨 |
|---|---|---|---|---|
| OS 락 | 뮤텍스 (포크) | 락 A를 쥐고 B를 요청 | 전역 락 순서, trylock + backoff | OS-M10 |
| 런타임 검사 | 락 **클래스** | held → new 획득 순서 | lockdep: 처음 보는 간선에서 역경로 검사 | OS-M11 |
| 오프라인 분석 | 의존 그래프 노드 | 임의의 의존 | SCC(Tarjan/Kosaraju)로 사이클 전부 | ALGO-A10 |
| DB | 레코드 락 | 트랜잭션 T1이 T2의 락을 기다림 | wait-for 탐지 → victim abort | STOR-M08 |
| B+tree | 페이지 래치 | 부모 래치 쥐고 자식 래치 | 항상 위→아래 순서 (탐지기 없음) | STOR-M10 |
| 칩 NoC | 채널 (VC 버퍼) | 채널 c1을 쥐고 c2 요청 | XY/turn model, dateline VC (Dally–Seitz) | SOCX-M05 |
| **DC 네트워크** | **(스위치, 입력 포트, 우선순위) 버퍼** | **PAUSE 때문에 다음 홉 버퍼를 기다림** | **up-down, 우선순위 상승, watchdog** | **DCN-M05** |

SOCX-M05의 채널 의존 그래프(CDG)와 이 레슨의 BDG는 거의 같다. 차이는 NoC는 wormhole이라 패킷 하나가 여러 채널을 동시에 쥐고, 이더넷은 store-and-forward라 패킷 하나가 버퍼 하나만 쥔다는 점 — 하지만 **버퍼 하나가 많은 패킷을 담고 FIFO** 라서, 맨 앞 패킷이 막히면 뒤의 패킷도 같이 막힌다(HOL blocking). 그래서 결과는 같다.

**Coffman 네 조건 (Coffman, Elphick, Shoshani 1971)** — 전체 표와 OS 해법은 OS-M10이 주인이다. 여기서는 PFC에 대응시킨다.

| 조건 | PFC 패브릭에서 | 깨는 대책 |
|---|---|---|
| 상호 배제 (mutual exclusion) | 버퍼 칸 하나에 패킷 하나 | 못 깬다 (물리적 메모리) |
| 점유 대기 (hold and wait) | 패킷이 자기 칸을 쥔 채 다음 홉 칸을 기다림 | 무손실을 포기(그 클래스는 drop 허용) — 즉 PFC를 끄는 것 |
| 비선점 (no preemption) | 무손실 = 스위치가 패킷을 **빼앗아 버리지 않음** | **(3) PFC watchdog**: 오래 멈춘 큐를 비운다 |
| 순환 대기 (circular wait) | BDG의 사이클 (CBD) | **(1) up-down 라우팅**, **(2) 홉별 우선순위 상승** |

### Part A — BDG 만들기와 사이클 탐지

- `is_up_down(path)`: 2단 Clos에서 "한 번 내려간 뒤 다시 올라가지 않는다". tier(leaf)=0, tier(spine)=1.
- `build_bdg(flows)`: 흐름마다 `f.bufs` (= `buffers_of(path, classes, host)`)의 연속한 쌍을 간선으로. 결과는 `{노드: 정렬된 후속 목록}`.
- `find_cycle(g)`: **DFS 3색** (WHITE / GRAY = 스택 위 / BLACK). GRAY로 가는 간선 = back edge = 사이클. 최소 노드부터 회전해 돌려준다 (결정적 출력).
- 확인: 정상 패브릭의 24개 ECMP 경로 → 노드 20 · 간선 32 · 비순환 (위상 순서 출력). 장애 후 bounce 경로 → 정확히 `(L1,S0) → (S1,L1) → (L2,S1) → (S0,L2)`. bounce 경로 하나(F1)만 있으면 사슬일 뿐 사이클이 아니다. 다이아몬드 DAG에서 거짓 사이클을 내지 않아야 한다.

### Part B — 사이클 단위 PFC 시뮬레이터에서 데드락 재현

- 시뮬레이터(`Sim.step`)는 주어진다: 도착 → 출구 포트별 라운드 로빈 전송(맨 앞 패킷의 하류 버퍼가 PAUSE면 못 보냄) → 호스트 송신 → watchdog → PAUSE 갱신. PAUSE는 전 사이클 끝의 상태를 쓰므로 반응이 1사이클 늦다 (그래서 헤드룸이 필요).
- `Sim._update_pause(node)`: XOFF=8 / XON=4 히스테리시스. LOSSY 클래스는 PAUSE를 걸지 않는다.
- `Sim.wait_cycle(w)`: **런타임** 순환 대기. 맨 앞 패킷이 w(=50) 사이클 이상 PAUSE에 막힌 버퍼들에서 "맨 앞 패킷이 기다리는 버퍼"를 따라가 고리를 찾는다.
- 확인: incast 3→1(그냥 혼잡)은 PAUSE가 205번 걸려도 drop 0, 소스가 멈추면 57사이클 만에 비워진다. bounce 시나리오는 t=82에 데드락 선언, 런타임 고리 = Part A의 정적 사이클, **소스 정지 후 배달 0 · 갇힌 패킷 64 · drop 0**.

### Part C — 세 가지 대책 비교

- `updown_reroute(topo, flows)`: up-down이 아니거나 죽은 경로는 살아 있는 up-down 경로로 다시 고른다 (없으면 None = 도달 불가).
- `bump_classes(path, base=3, n_lossless=2)`: bounce 할 때마다 클래스 +1 (Tagger 아이디어). 클래스를 다 쓰면 LOSSY(0).
- `Sim._watchdog()`: 맨 앞 패킷이 T 사이클 연속 PAUSE에 막힌 큐를 비운다.
- 확인: 세 대책 모두 데드락을 없애거나 풀지만, (1)(2)는 drop 0, (3)은 drop 108 + 데드락이 계속 다시 생긴다 (12번 발동). 결과 표는 시뮬레이터 실제 출력.

## 2. Problem (English)

> **PFC deadlock in a leaf-spine fabric.** A 2-tier Clos has leaves `L0..L3` and spines `S0,S1`. Lossless priority 3 uses per-(switch, ingress port, priority) buffers with XOFF = 8, XON = 4, capacity 12, 1-cycle link delay and 1-cycle PAUSE reaction.
>
> 1. `build_bdg(flows) -> dict` — nodes are buffers `(switch, in_port, prio)`; add edge `a -> b` when some flow's packet in `a` moves next into `b`. `find_cycle(g) -> list | None` — return one cycle (rotated to start at its smallest node) or `None`, in O(V + E). Prove that up-down routing yields an acyclic graph.
> 2. Links `S0-L3` and `S1-L0` fail. Stale ECMP plus spine local repair produces paths `L2-S0-L1-S1-L3` and `L1-S1-L2-S0-L0`. Show the cyclic buffer dependency, reproduce the deadlock in a cycle-stepped simulation with two hosts per path at line rate, and distinguish it from congestion.
> 3. Compare (a) enforcing up-down routes, (b) raising the priority class on every bounce with a finite number of lossless classes, and (c) a PFC watchdog that flushes a queue paused for T cycles. Map each to the Coffman condition it breaks.
>
> Example: `find_cycle(build_bdg(bounce_flows()))` → `[(L1,S0,3), (S1,L1,3), (L2,S1,3), (S0,L2,3)]`; after sources stop, deadlocked fabric delivers 0 packets and keeps 64 trapped with 0 drops.

## 3. 시작 코드

<!-- code: starter.py -->

```bash
cd lessons/DCN/DCN-M05
python3 -I starter.py          # TODO 상태: 3/21 passed, exit 1 (0.15 s)
python3 -I solution.py         # 21/21 passed, exit 0
python3 -I demo_sweep.py       # 부하 · 버퍼 · watchdog 스윕 (0.5 s)
```

실제 출력 (일부):

```text
-- Part A: buffer dependency graph --
FAIL  healthy fabric: 24 ECMP paths (12 pairs x 2 spines) all valid and up-down
PASS  find_cycle: diamond DAG a->b,a->c,b->d,c->d has NO cycle (visited != on-stack)
FAIL  find_cycle: self-loop x->x and 3-ring found
...
-- Part B: PFC simulator --
FAIL  XOFF=8/XON=4 hysteresis: q 0,5,7,8,9,6,5,4,3,8 -> pause 0,0,0,1,1,1,1,0,0,1  got [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
      incast 3->1: injected 1800 delivered 634 pause_events 0 max_occ 12 tput_on 1.00 empty 37 cyc after stop
FAIL  incast (congestion): PAUSE fires, max occupancy <= CAP, zero drops, drains to 0, no deadlock
      bounce: injected 2400 delivered 446 residual 0 (in fabric) tput_on 0.66 delivered_after_stop 50
...
3/21 passed
```

> 눈여겨볼 것: PAUSE를 구현하기 전(starter)의 bounce 시나리오는 **데드락이 없다** — 대신 2400개 중 1954개를 버린다 (손실 이더넷). 데드락은 "버리지 않겠다"는 약속(PFC)의 대가다. 또 stub `find_cycle` 이 `None` 을 돌려주므로 다이아몬드 DAG 테스트는 공짜로 PASS 한다. 그래서 같은 함수에 "사이클을 찾아야 하는" 양성 테스트가 같이 있다.

## 4. 힌트

<details>
<summary>힌트 1 — is_up_down 은 "계곡(valley)" 찾기</summary>

tier 열을 만든다: `[0,1,0,1,0]` (bounce 경로). 앞에서부터 보며 한 번이라도 감소(1→0)가 나온 뒤 증가(0→1)가 나오면 False. `[0]` (같은 leaf 안)과 `[0,1,0]` 은 True.

</details>

<details>
<summary>힌트 2 — find_cycle: visited 하나로는 부족하다</summary>

"이미 방문한 노드를 다시 만나면 사이클" 은 틀렸다. 다이아몬드 a→b→d, a→c→d에서 d를 두 번 만나지만 사이클이 아니다. **지금 DFS 스택 위에 있는(GRAY)** 노드를 다시 만날 때만 사이클이다. 끝난 노드(BLACK)는 다시 볼 필요가 없다 → O(V+E). 스택에 `(node, iter(g[node]))` 를 넣으면 재귀 없이 "다음 자식" 을 이어서 꺼낼 수 있다.

</details>

<details>
<summary>힌트 3 — PAUSE 히스테리시스는 상태 기계 두 줄</summary>

`if not paused and q >= xoff: paused = True` / `elif paused and q <= xon: paused = False`. 그 사이(4 < q < 8)에서는 **이전 상태 유지**. 테스트 수열 0,5,7,8,9,6,5,4,3,8 에서 q=6,5 일 때 여전히 1 이어야 한다.

</details>

<details>
<summary>힌트 4 — wait_cycle 은 "맨 앞 패킷이 기다리는 버퍼" 사슬</summary>

멈춘 버퍼마다 나가는 간선은 정확히 하나다 (FIFO 맨 앞 패킷의 다음 버퍼). 그래서 DFS가 필요 없고, 연결 리스트에서 고리 찾기처럼 따라가면 된다: `seen` 딕셔너리에 위치를 기록하고, 이미 본 노드를 만나면 그 위치부터가 고리. 멈추지 않은 버퍼에 닿으면 그 시작점은 고리가 아니다.

</details>

<details>
<summary>힌트 5 — bump_classes: bounce 지점 "다음" 버퍼부터 올린다</summary>

bounce leaf 의 입력 버퍼 `(L1,S0)` 는 아직 옛 클래스 3이다 (그 패킷은 S0에서 클래스 3으로 왔다). L1이 위로 다시 보낼 때 태그를 바꾸므로 `(S1,L1)` 부터 4. F1 경로 → `[3,3,3,4,4]`. 반복문에서 "현재 버퍼의 클래스를 먼저 기록하고, 그 버퍼가 중간 leaf면 c += 1" 순서.

</details>

## 5. 풀이

### Part A — up-down이면 왜 비순환인가, 그리고 DFS 3색

**증명 (up-down ⇒ BDG 비순환).** 2단 Clos에서 버퍼를 세 층으로 나눈다.

| 층 | 버퍼 | 그 버퍼의 패킷이 다음에 가는 곳 (up-down 경로) |
|---|---|---|
| 0 | `(Lx, host, p)` — 호스트에서 leaf로 | spine 버퍼 `(Sy, Lx, p)` 또는 바로 출구 |
| 1 | `(Sy, Lx, p)` — leaf에서 spine으로 (올라옴) | leaf 버퍼 `(Lz, Sy, p)` |
| 2 | `(Lz, Sy, p)` — spine에서 leaf로 (내려옴) | **출구(호스트)뿐** |

up-down 경로의 모든 간선은 층 번호를 **엄격하게 올린다** (0→1, 1→2). 층 번호가 줄지 않는 그래프에서 사이클은 불가능하다 — 사이클을 한 바퀴 돌면 층 번호가 출발점보다 커져 있어야 하니까. 위상 순서는 "층 0 전부, 층 1 전부, 층 2 전부" 이고, 실제로 `topo_order` 가 `(L0,h0) (L1,h0) (L2,h0) (L3,h0) (S0,L0) (S0,L1) ...` 를 출력한다. 노드 20 = 층 0 4개 + 층 1 8개 + 층 2 8개, 간선 32 = 층0→1 8개 + 층1→2 8×3 = 24개.

bounce가 하는 일은 정확히 **층 2 → 층 1 간선** 을 추가하는 것이다: `(L1,S0)` (내려온 버퍼, 층 2) → `(S1,L1)` (올라가는 버퍼, 층 1). 층을 거꾸로 가는 간선이 생기면 비로소 사이클이 가능해진다. 이것이 SOCX-M05의 Dally–Seitz 정리(채널 의존 그래프가 비순환이면 결정적 라우팅은 데드락 자유)의 네트워크판이다.

**왜 노드에 입력 포트와 우선순위가 들어가나.** PFC는 **입력 포트 × 우선순위** 단위로 PAUSE를 건다. 노드를 스위치 하나로 뭉개면 정상 up-down 라우팅에서도 거짓 사이클이 나온다 — 실제로 스위치만 노드로 쓰면 `find_cycle` 이 `['L0', 'S0']` 를 돌려준다 (L0→S0 는 L0에서 출발하는 흐름, S0→L0 는 L0로 가는 흐름 — 서로 다른 버퍼인데 한 노드로 합쳐졌다). 우선순위를 빼면 대책 (2)가 아무 효과 없는 것처럼 보인다.

**DFS 3색.**

| 색 | 뜻 | 그 노드로 가는 간선을 만나면 |
|---|---|---|
| WHITE | 아직 안 봄 | 내려간다 (GRAY로 칠하고 스택에 push) |
| GRAY | 지금 DFS 경로(스택) 위 | **back edge → 사이클.** 경로에서 그 노드부터 끝까지가 사이클 |
| BLACK | 그 아래를 다 봤고 사이클 없음 | 무시 (다시 볼 필요 없다 → O(V+E)) |

재귀 대신 `(node, iterator)` 스택을 쓴 이유: Python 재귀 한도(기본 1000)와, 실제 패브릭 BDG(스위치 수천 × 포트 수십 × 클래스 2~3)가 크기 때문이다. 결과를 최소 노드부터 회전하는 이유는 테스트가 **정확한 리스트** 를 비교하기 때문 — 같은 사이클이 어디서 DFS를 시작했느냐에 따라 다르게 출력되면 안 된다.

DFS가 "사이클 하나" 를 준다면, 추가 연습 5의 Tarjan SCC는 "사이클에 걸린 버퍼 **전부**" 를 준다. 검사기 출력으로는 SCC가 더 유용하다 (어느 경로를 빼야 하는지 한 번에 보인다 — ALGO-A10).

### Part B — 시뮬레이터에서 데드락이 생기는 순간

한 사이클의 순서 (주어진 `Sim.step`):

| 단계 | 하는 일 | 왜 이 순서인가 |
|---|---|---|
| 1. 도착 | 전 사이클에 보낸 패킷을 하류 입력 버퍼에 넣는다. CAP 초과면 overflow drop | 무손실이면 이 drop은 반드시 0 (테스트로 확인) |
| 2. 전송 | 출구 포트(스위치, 다음 홉)마다 1 패킷, 라운드 로빈. 후보 = 맨 앞 패킷의 하류 버퍼가 PAUSE 아닌 입력 버퍼 | FIFO 맨 앞만 본다 → HOL blocking |
| 3. 호스트 송신 | 흐름별 rate로 생성, 호스트 링크당 1 패킷/사이클, 첫 버퍼가 PAUSE면 대기 | 호스트 NIC도 PAUSE를 존중 |
| 4. watchdog | (켜져 있으면) 오래 막힌 큐 비우기 | |
| 5. PAUSE 갱신 | XOFF/XON 히스테리시스 | 다음 사이클 2단계가 이 값을 본다 = PAUSE 1사이클 지연 |

**헤드룸.** PAUSE가 1사이클 늦게 효과를 내므로 XOFF를 넘은 뒤에도 한 패킷이 더 들어온다 — incast에서 실제 최대 점유는 `max_occ = 9 = XOFF + 1`. CAP=8(헤드룸 0)로 돌리면 incast에서 overflow drop이 295개 나왔다 (CAP 9·10·12는 0). 실제 스위치의 헤드룸은 케이블 길이·MTU·PAUSE 처리 시간으로 계산한다 (DCN-M04). 이 레슨의 CAP=12는 넉넉히 잡은 값이다.

**incast 3→1은 혼잡일 뿐이다.** L0·L1·L2가 L3로 선로 속도로 보내면 L3의 호스트 출구가 병목(1 패킷/사이클)이다. PAUSE가 205번 걸리고 호스트까지 번지지만, 병목 출구는 **항상 흐르고 있다** (tput_on = 1.00). 소스가 멈추면 57사이클 만에 비워진다.

**bounce는 데드락이다.** 그림 3은 같은 시뮬레이터의 50사이클 창별 배달 수다 (`demo_sweep.py` 1번 출력).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">50 사이클 창별 배달 수 (실측) — 소스는 t = 600 에서 정지</text>
  <text x="10" y="44" fill="currentColor">incast 3→1 (혼잡)</text>
  <line x1="58" y1="130" x2="712" y2="130" stroke="currentColor"/>
  <line x1="58" y1="60" x2="712" y2="60" stroke="currentColor" stroke-dasharray="2 4"/>
  <text x="54" y="64" fill="currentColor" text-anchor="end">50</text>
  <text x="54" y="134" fill="currentColor" text-anchor="end">0</text>
  <rect x="62" y="64.2" width="22" height="65.8" class="f-ok" stroke="currentColor"/>
  <rect x="89" y="60.0" width="22" height="70.0" class="f-ok" stroke="currentColor"/>
  <rect x="116" y="60.0" width="22" height="70.0" class="f-ok" stroke="currentColor"/>
  <rect x="143" y="60.0" width="22" height="70.0" class="f-ok" stroke="currentColor"/>
  <rect x="170" y="60.0" width="22" height="70.0" class="f-ok" stroke="currentColor"/>
  <rect x="197" y="60.0" width="22" height="70.0" class="f-ok" stroke="currentColor"/>
  <rect x="224" y="60.0" width="22" height="70.0" class="f-ok" stroke="currentColor"/>
  <rect x="251" y="60.0" width="22" height="70.0" class="f-ok" stroke="currentColor"/>
  <rect x="278" y="60.0" width="22" height="70.0" class="f-ok" stroke="currentColor"/>
  <rect x="305" y="60.0" width="22" height="70.0" class="f-ok" stroke="currentColor"/>
  <rect x="332" y="60.0" width="22" height="70.0" class="f-ok" stroke="currentColor"/>
  <rect x="359" y="60.0" width="22" height="70.0" class="f-ok" stroke="currentColor"/>
  <rect x="386" y="60.0" width="22" height="70.0" class="f-ok" stroke="currentColor"/>
  <rect x="413" y="120.2" width="22" height="9.8" class="f-ok" stroke="currentColor"/>
  <text x="450" y="112" fill="currentColor">정지 후 밀린 것을 비우고 0 (패브릭 빔)</text>
  <text x="10" y="164" fill="currentColor">bounce CBD (데드락)</text>
  <line x1="58" y1="250" x2="712" y2="250" stroke="currentColor"/>
  <line x1="58" y1="180" x2="712" y2="180" stroke="currentColor" stroke-dasharray="2 4"/>
  <text x="54" y="184" fill="currentColor" text-anchor="end">50</text>
  <text x="54" y="254" fill="currentColor" text-anchor="end">0</text>
  <rect x="62" y="230.4" width="22" height="19.6" class="f-warn" stroke="currentColor"/>
  <text x="96" y="226" fill="currentColor">14 개 배달 (마지막 t = 24), 그 뒤 0 — t = 82 에 "50 사이클 정지" 로 선언</text>
  <text x="400" y="242" fill="currentColor">정지 후에도 0 — 64 개가 갇혀 있고 drop 은 0</text>
  <line x1="383" y1="36" x2="383" y2="262" stroke="currentColor" stroke-dasharray="6 3"/>
  <text x="383" y="278" fill="currentColor" text-anchor="middle">t = 600 소스 정지</text>
  <text x="62" y="294" fill="currentColor">창 0 (t 0–49)</text>
  <text x="712" y="294" fill="currentColor" text-anchor="end">창 23 (t 1150–1199)</text>
</svg>
```

| | 혼잡 (incast) | 데드락 (CBD) |
|---|---|---|
| PAUSE | 걸렸다 풀렸다 (205번) | 한 번 걸리면 영원히 |
| 병목 링크 | 선로 속도로 흐름 (1.00) | **아무 링크도 안 흐름** (0.00) |
| 소스 정지 후 | 57사이클에 비워짐 | **처리량 0 지속, 64개 갇힘** |
| drop | 0 | **0** (그래서 "손실 알람" 으로는 안 보인다) |
| 고치는 방법 | 혼잡 제어(DCQCN 등, DCN-M07) | 라우팅/클래스/watchdog — 혼잡 제어로는 안 풀린다 |

**정적 vs 런타임.** `find_cycle(BDG)` 는 "이 라우팅이면 데드락이 **날 수 있다**" (잠재적), `wait_cycle()` 은 "지금 **났다**" (실제)다. OS-M11의 lockdep이 ABBA 순서를 실제 교착 없이도 잡는 것과 같은 관계 — BDG 검사기는 트래픽이 없어도, 라우팅 테이블만으로 CBD를 잡는다. 테스트는 두 결과가 같은 4개 버퍼임을 확인한다.

**CBD는 필요조건이지 충분조건이 아니다.** 같은 bounce 경로, 같은 CBD에서 호스트를 경로당 1대로 줄이면 공유 링크 수요가 2.0(선로의 2배)인데도 **데드락이 안 생긴다** (추가 연습 2). 그 실행에서 PAUSE는 222번 걸리지만, 한 번이라도 PAUSE를 건 버퍼는 `(L1,h0)`·`(L2,h0)` 두 호스트 버퍼뿐이고 사이클 버퍼의 최대 점유는 1이다. 이유는 출구 중재(arbitration) 비율이다: L1→S1 출구에서 라운드 로빈 후보가 `(L1,S0)`(사이클 안, F1) 하나와 `(L1,h0)`(호스트, F2) 하나이면 F1은 1/2을 받고, F1이 사이클에 들어오는 속도도 L2→S0 출구에서 1/2이다 — 들어오는 만큼 나간다. 호스트가 2대면 사이클 안 버퍼는 1/3을 받는데 들어오는 건 2/3 → 사이클 버퍼가 차고, 넷이 동시에 PAUSE 상태가 되는 순간 끝난다. 호스트 2대일 때 문턱은 호스트당 r = 0.26, 즉 공유 링크 수요 4r이 1을 넘는 지점이다 (`demo_sweep.py` 2번). "CBD가 있어도 부하·중재 패턴에 따라 날 수도 안 날 수도 있다" — 그래서 운영에서는 CBD 자체를 없애는 쪽(대책 1, 2)을 택한다. 이 비율 분석은 이 모델(라운드 로빈, 입력 FIFO)에서의 설명이고, 실제 스위치의 공유 버퍼·스케줄러는 더 복잡하다.

### Part C — 대책 세 가지, 실제 숫자

`python3 -I solution.py` 의 마지막 표 (부하: 경로당 호스트 2대 × 선로 속도, 600사이클 송신 + 600사이클 drain):

```text
mitigation               injected delivered dropped residual tput_on wd_fires deadlock@ empty@
none (bounce routes)           78        14       0       64    0.00        0        82 None
(1) up-down reroute          1218      1218       0        0    2.00        0      None 12
(2) per-hop prio bump         632       632       0        0    0.98        0      None 35
(3) PFC watchdog T=100        182        74     108        0    0.04       12        82 134
```

| 대책 | 깨는 조건 | 결과 (위 표) | 대가 / 현실 |
|---|---|---|---|
| (1) up-down 강제 | 순환 대기 | F1 → L2-S1-L3, F2 → L1-S0-L0. 비순환, 처리량 2.00 (두 출구 모두 선로 속도) | 라우팅이 up-down 경로만 쓰도록 보장해야 한다. 수렴 전 과도 상태·플러딩·로컬 복구가 이걸 깨는 주범. up-down 경로가 없는 목적지는 도달 불가로 둔다 |
| (2) 홉별 우선순위 상승 | 순환 대기 | **같은 bounce 경로** 를 쓰면서 비순환. 처리량 0.98 (bounce 경로가 링크를 공유해서) | 무손실 클래스 수가 유한(우선순위 8개 중 무손실은 클래스마다 헤드룸이 들어서 소수 — 흔히 1~2개로 알려져 있음, 장비마다 다름) → k-bounce에 k+1개 필요. 넘으면 LOSSY로 내려 손실 허용 |
| (3) PFC watchdog | 비선점 | 데드락은 풀리지만 다시 생긴다: 12번 발동, 108개 drop, 처리량 0.04 | 원인을 못 고친다 (안전망). T가 작으면 그냥 혼잡에서도 버린다 (추가 연습 4) |

**(2)가 왜 비순환인가.** 클래스는 경로를 따라 **줄지 않고**, bounce(층 2 → 층 1 간선)를 할 때만 1씩 오른다. 같은 클래스 안의 간선은 전부 up-down 간선이라 Part A의 층 증명이 그대로 성립한다. 따라서 (클래스, 층) 쌍이 사전식으로 엄격히 증가 → 사이클 불가. OS-M10의 "포크에 번호를 매기고 작은 번호부터 잡는다" (계층적 락 순서)와 똑같은 논리다. 추가 연습 3에서 시드 고정 무작위 1-bounce 경로 12개가 한 클래스로는 사이클을 만들고, 클래스 상승을 넣으면 비순환임을 확인한다.

**(3) watchdog의 동작과 함정.** 실제 PFC watchdog(스위치·NIC 벤더 기능)은 "어떤 큐가 PAUSE 상태로 T 동안 전혀 빠지지 않음" 을 감지하면 그 큐의 패킷을 버리거나 PAUSE를 무시하는 storm 모드로 들어가고, 복구 시간 뒤 정상으로 돌아온다. 감지/복구 시간 기본값은 벤더·버전마다 다르며 대략 수백 ms 단위로 알고 있다 — **확인 안 된 값**, 장비 문서를 볼 것. 이 시뮬레이터에서는 T 사이클 연속 PAUSE에 막힌 입력 버퍼를 통째로 비운다. 스윕 (`demo_sweep.py` 4번):

```text
max continuous PAUSE-blocked age in incast without watchdog: 27 cycles
     T | bounce: fires drops delivered empty@ | incast: fires drops delivered
    10 |    46   414       172     40        |    66   594       638
    20 |    34   306       136     51        |    32   288       639
    40 |    24   216       110    113        |     0     0       654
    50 |    20   180        98    100        |     0     0       654
   100 |    12   108        74    134        |     0     0       654
   200 |     8    72        62    301        |     0     0       654
   400 |     4    36        30   None        |     0     0       654
```

T가 작을수록 데드락에서 빨리 빠져나오지만, T ≤ 27(incast의 최대 연속 PAUSE 막힘)이면 **데드락이 없는 incast에서도 594개까지 버린다** — 무손실 클래스의 존재 이유를 watchdog이 없앤다. T=400이면 drain 600사이클 안에 다시 갇혀 끝난다 (`empty@ None`). watchdog은 "최후의 선점" — 펌웨어의 하드웨어 watchdog이 멈춘 태스크를 리셋하는 것과 같은 자리다 (OS-M10).

**버퍼를 키우면?** 안 된다. XOFF 8 → 128로 16배 키우면 데드락은 t=82 → 562로 늦어질 뿐이고, 갇히는 패킷은 64 → 908로 늘어난다 (추가 연습 1). 순환 대기는 용량 문제가 아니라 **구조** 문제다.

`demo_sweep.py` 실행 환경 (결과는 결정적이라 부하와 무관, 실행 시간만 영향):

```text
machine: Apple M6 / 2 / 4 / 6 | python 3.9.6 | Apple clang version 21.0.0 (clang-2100.0.123.102)
load:   16:10  up 6 days, 16:31, 10 users, load averages: 12.73 7.57 4.99
...
wall time: 0.47 s
```

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py                 # 21/21 passed, exit 0
python3 -I -W error -X dev solution.py # 경고 0, exit 0
```

## 7. 자주 하는 실수

**1. "방문한 노드를 다시 만나면 사이클"** — 다이아몬드 DAG에서 거짓 사이클. 실제 BDG에서는 여러 흐름이 같은 spine 버퍼로 모이므로(ECMP 합류) 정상 패브릭을 "데드락 위험" 으로 오판한다.

```python
# 틀림: visited 하나
if nxt in visited: return True
# 맞음: 스택 위(GRAY)인 노드만 사이클. BLACK 은 이미 사이클 없음이 확인된 하위 그래프
elif color.get(nxt, WHITE) == GRAY: ...   # back edge
```

**2. 노드를 스위치(또는 스위치×우선순위)로 잡기** — PFC는 입력 포트별로 PAUSE를 건다. 스위치 단위로 합치면 정상 up-down에서도 `['L0', 'S0']` 같은 거짓 사이클이 나온다 (위에서 실제로 돌려 본 결과). 반대로 우선순위를 빼면 대책 (2)의 효과가 사라진다.

**3. 혼잡을 데드락으로 오진** — "처리량이 떨어졌다", "PAUSE가 많다" 는 incast에서도 똑같다. 판정 기준은 **소스를 멈춰도(또는 아무리 기다려도) 사이클 버퍼가 줄지 않는다** + **wait-for 고리가 실재한다**. 현장에서는 PAUSE 카운터가 계속 오르면서 해당 큐의 TX 카운터가 0인 포트들이 고리를 이루는지 본다.

**4. XON = XOFF (히스테리시스 없음)** — incast에서 PAUSE 전환 수가 XON=4일 때 205, XON=6일 때 364, XON=7일 때 601로 늘었다 (실측). 실제 링크에서는 PAUSE/RESUME 프레임 폭주 = 대역폭 낭비와 CPU/파이프라인 부담. 반대로 XON이 너무 낮으면 재시작이 늦어 링크가 놀게 된다.

```python
# 틀림: 한 문턱
self.paused[node] = q >= self.xoff
# 맞음: 두 문턱 사이에서는 상태 유지
if not self.paused.get(node) and q >= self.xoff: self.paused[node] = True
elif self.paused.get(node) and q <= self.xon:    self.paused[node] = False
```

**5. watchdog T를 "작을수록 좋다" 로 잡기** — T ≤ 정상 혼잡의 최대 연속 PAUSE 길이면 데드락이 없어도 무손실 트래픽을 버린다 (incast T=20에서 288개). T는 운영 중 관찰한 정상 PAUSE 지속 시간의 분포보다 충분히 길게, 그리고 watchdog 발동은 **알람** 으로 올려 원인(라우팅)을 고친다.

**6. bounce 지점 버퍼의 클래스를 올리기** — `(L1,S0)` 까지 클래스 4로 만들면, S0가 클래스 3으로 보낸 패킷을 L1이 클래스 4 버퍼로 받는 셈이라 PAUSE 대상 클래스가 어긋난다 (상류 S0는 클래스 4 PAUSE를 받고도 클래스 3 큐를 계속 보낸다). 태그는 bounce 스위치가 **내보낼 때** 바꾼다 → 다음 버퍼부터 새 클래스.

## 8. 면접 Q&A

<details>
<summary>Q1. PFC 데드락이 무엇이고 왜 생기나요?</summary>

PFC는 입력 버퍼(포트 × 우선순위)가 XOFF를 넘으면 상류에 PAUSE를 보내 손실을 막는다. 상류는 패킷을 쥔 채 기다리고, 그 상류도 차면 또 PAUSE를 보낸다. 버퍼 의존이 고리를 이루면(CBD) 고리의 모든 버퍼가 PAUSE 상태가 되고 모두가 하류의 PAUSE를 기다려 **영원히** 멈춘다. 손실이 없으므로 drop 알람도 안 울린다. CBD는 라우팅이 up-down을 어길 때 생긴다: 링크 장애 후 재라우팅/로컬 복구의 bounce, 라우팅 루프, 수렴 전 과도 상태, 목적지 모르는 패킷의 플러딩.
꼬리질문: "같은 증상의 혼잡과 어떻게 구분하나?" → 소스를 멈춰도 처리량 0이 지속되고 큐가 줄지 않으면 데드락. 혼잡은 병목 출구가 선로 속도로 계속 흐른다 (이 레슨: incast 1.00 vs bounce 0.00, 정지 후 57사이클에 비움 vs 64개 갇힘).

</details>

<details>
<summary>Q2. Clos에서 up-down 라우팅이면 왜 데드락이 없나요?</summary>

버퍼를 층으로 나누면 (호스트→leaf, leaf→spine 올라옴, spine→leaf 내려옴) up-down 경로의 모든 의존 간선이 층을 엄격히 올린다. 층이 줄지 않는 그래프에는 사이클이 없다 → BDG 비순환 → Dally–Seitz 논리로 데드락 자유. 3단 이상 Clos(fat-tree)도 같다: tier가 올라갔다가 내려가기만 하면 된다.
꼬리질문: "그럼 실제로는 왜 데드락이 났나?" → 라우팅이 언제나 up-down이라는 가정이 깨지기 때문. Microsoft의 SIGCOMM 2016 논문은 (기억으로는) 목적지 MAC을 모르는 무손실 패킷이 플러딩되면서 up-down이 깨진 사례를 보고하고, 무손실 패킷은 플러딩 대신 버리도록 고쳤다고 한다 — 세부는 원문 확인 필요.

</details>

<details>
<summary>Q3. 버퍼를 키우거나 헤드룸을 늘리면 해결되나요?</summary>

아니다. 헤드룸은 PAUSE 반응 지연 동안의 **손실** 을 막는 것이고, 순환 대기는 구조 문제다. 이 시뮬레이터에서 XOFF를 8 → 128로 키우면 데드락이 t=82 → 562로 늦어질 뿐이고 갇히는 패킷은 64 → 908로 는다. 오히려 "가끔만 나는" 데드락이 되어 재현이 더 어려워진다.
꼬리질문: "헤드룸은 무엇으로 정하나?" → 링크 속도 × (케이블 왕복 전파 + PAUSE 생성·처리 지연) + MTU 몇 개 (DCN-M04). 이 레슨 모델은 1사이클 지연이라 최대 점유가 XOFF+1이었고, CAP=8(헤드룸 0)이면 incast에서 overflow drop 295개가 났다.

</details>

<details>
<summary>Q4. 홉별 우선순위 상승(Tagger 류)은 어떻게 동작하고 한계는?</summary>

패킷에 태그(bounce 횟수)를 달고, bounce 스위치가 내보낼 때 태그를 올리며 태그 → 무손실 큐 매핑을 바꾼다. 클래스가 줄지 않고 bounce할 때만 오르므로, 같은 클래스 안에서는 up-down 간선만 남는다 → 비순환. 경로를 버리지 않고 **같은 bounce 경로** 를 계속 쓸 수 있다는 게 장점 (이 레슨: 처리량 0.98, drop 0). 한계는 무손실 클래스 수: k-bounce에 k+1개가 필요한데 우선순위 8개 중 무손실로 쓸 수 있는 건 클래스마다 헤드룸 버퍼가 필요해서 소수(장비마다 다름)라, 그 이상 bounce한 패킷은 손실 큐로 내린다. Tagger(CoNEXT 2017) 원문의 정확한 태그 규칙은 확인하지 않았다.
꼬리질문: "OS의 무엇과 같나?" → 계층적 락 순서. 클래스 = 락 레벨, "항상 레벨이 오르는 방향으로만 기다린다".

</details>

<details>
<summary>Q5. PFC watchdog은 무엇을 깨는 대책이고, 언제 위험한가요?</summary>

Coffman의 **비선점** 을 깬다: 무손실 약속을 어기고 오래 멈춘 큐의 패킷을 강제로 빼앗아(버려) 고리를 끊는다. 원인(CBD)은 그대로라 같은 트래픽이면 다시 데드락이 생긴다 (이 레슨: T=100에서 12번 발동, 108개 drop, 처리량 0.04). 위험: T가 정상 혼잡의 PAUSE 지속보다 짧으면 데드락이 없는데도 무손실 트래픽을 버린다 (incast T=20 → 288개). RDMA RC는 손실 시 go-back-N 재전송이라 손실 몇 개가 큰 성능 하락이 된다.
꼬리질문: "NIC 쪽 watchdog은 왜 필요한가?" → 고장 난 NIC/호스트가 PAUSE를 계속 보내면(PAUSE storm) 그 PAUSE가 패브릭 전체로 번진다. NIC이 자기 수신 파이프라인이 멈췄을 때 PAUSE 송신을 끊는 장치가 필요하다 — 이것도 SIGCOMM 2016 논문에 나온 사례로 기억한다(확인 필요).

</details>

<details>
<summary>Q6. 라우팅 테이블에서 CBD를 미리 찾는 검사기를 어떻게 만들겠습니까?</summary>

모든 (소스, 목적지, ECMP 선택)과 장애 시나리오별 경로를 뽑아 버퍼 `(스위치, 입력 포트, 클래스)` 간선을 만든다. DFS 3색으로 사이클 하나를 찾거나, Tarjan SCC로 사이클에 걸린 버퍼 전부를 찾는다 (O(V+E)). 크기 > 1인 SCC(또는 자기 루프)가 있으면 그 SCC를 지나는 경로 목록을 출력해 어느 라우팅 규칙이 원인인지 보인다. 이건 정적 분석이라 트래픽 없이 배포 전에 돌릴 수 있다 — lockdep(OS-M11)이 실제 교착 없이 락 순서 위반을 잡는 것과 같은 위치다.
꼬리질문: "장애 조합이 폭발하면?" → 단일/이중 링크 장애로 한정하거나, 라우팅이 만들 수 있는 bounce 패턴(1-bounce, 2-bounce)만 생성. 증분 검사: 새 간선 a→b가 생길 때 b에서 a로 가는 경로가 있는지만 본다 (lockdep 방식).

</details>

<details>
<summary>Q7. NoC 데드락(SOCX-M05)과 PFC 데드락은 무엇이 같고 다른가요?</summary>

같다: 자원 = 버퍼/채널, 간선 = 쥔 채 다음을 기다림, 비순환이면 데드락 자유(Dally–Seitz), 대책 = 라우팅 제한(XY/turn model ↔ up-down)과 자원 클래스 분리(dateline VC ↔ 우선순위 상승). 다르다: NoC는 보통 wormhole이라 패킷 하나가 여러 채널을 동시에 쥐고, 칩 안이라 "버리기"(watchdog) 선택지가 사실상 없다. 이더넷은 store-and-forward이지만 FIFO 공유 버퍼의 HOL 때문에 결과는 같다. 또 이더넷은 손실 클래스로 내려 버리는 탈출구가 있다.
꼬리질문: "Duato 정리는?" → 적응 라우팅에서는 CDG 전체가 비순환일 필요는 없고, 비순환인 탈출(escape) 채널 부분집합이 있으면 충분하다는 조건 (SOCX-M05).

</details>

## 9. 추가 연습

### 연습 1 — 버퍼를 16배로 키우면

XON = XOFF/2, CAP = XOFF + 4로 XOFF 8 · 32 · 128에서 bounce 시나리오를 돌린다. 데드락은 사라지나?

<details>
<summary>답 보기</summary>

`[(8, 82, 64), (32, 178, 236), (128, 562, 908)]` = (XOFF, 데드락 선언 사이클, 갇힌 패킷). 셋 다 데드락이다. 늦어질 뿐이고, 갇히는 양은 XOFF에 거의 비례해 는다 (사이클 버퍼 4개 + 호스트 버퍼 4개가 각각 XON~XOFF 근처에서 멈춤). "용량을 늘리면 일어나는 시점만 바뀐다" 는 OS에서 스레드 수를 바꿔도 ABBA가 없어지지 않는 것과 같다.

</details>

### 연습 2 — 부하 문턱과 "CBD는 필요조건"

경로당 호스트 2대, 호스트당 rate r을 0.10부터 0.01씩 올려 처음 데드락이 나는 r을 찾는다. 경로당 호스트 1대 × 선로 속도는?

<details>
<summary>답 보기</summary>

r = 0.26. 공유 링크(L1→S1, L2→S0)는 F1 2대 + F2 2대 = 4r을 실어야 하므로 4r > 1이 되는 첫 지점이다 (0.24 → 수요 0.96 → 전부 배달). 호스트 1대면 수요 2.0인데도 데드락이 없다: PAUSE 222번, 그러나 PAUSE를 건 버퍼는 `(L1,h0)`·`(L2,h0)` 뿐, 사이클 버퍼 최대 점유 1. 라운드 로빈에서 사이클 안 트래픽이 받는 몫(1/2)이 들어오는 몫(1/2)과 같아 압력이 호스트 쪽에만 쌓인다. 호스트 2대면 받는 몫 1/3 < 들어오는 몫 2/3 → 사이클 버퍼가 찬다. CBD + 과부하가 있어도 데드락은 트래픽 패턴과 중재에 달려 있다 — 그래서 "지금까지 안 났다" 는 안전의 증거가 아니다.

</details>

### 연습 3 — 유한한 무손실 클래스

`classes_needed(path)` = bounce 수 + 1. 2-bounce 경로 `L2-S0-L1-S1-L0-S0-L3` 에 무손실 클래스 2개 / 3개면? 시드 7로 만든 무작위 1-bounce 경로 12개를 한 클래스로 / 클래스 상승으로 BDG를 만들면?

<details>
<summary>답 보기</summary>

클래스 2개: `[3,3,3,4,4,0,0]` — 두 번째 bounce 뒤 두 홉은 LOSSY(손실 허용). 3개: `[3,3,3,4,4,5,5]`. 무작위 경로 12개를 한 클래스로 두면 `(L1,S1,p3) -> (S0,L1,p3) -> (L2,S0,p3) -> (S1,L2,p3)` 사이클이 나오고, 클래스 상승을 넣으면 비순환이다 (증명: 클래스는 안 줄고, 같은 클래스 안은 up-down 간선뿐). 실무 선택: 2-bounce 이상은 드물게 하고, 넘치면 손실 큐로 내려 "그 패킷만" 손실을 감수한다.

</details>

### 연습 4 — watchdog 거짓 양성

watchdog 없이 incast를 돌려 "맨 앞 패킷이 PAUSE에 연속으로 막힌 최대 사이클" 을 재고, T = 10 · 20 · 27 · 28 · 40으로 drop 수를 비교한다.

<details>
<summary>답 보기</summary>

최대 연속 막힘 = 27사이클. drop = `{10: 594, 20: 288, 27: 143, 28: 0, 40: 0}`. 발동 조건이 `age >= T` 이므로 T ≤ 27이면 데드락이 없는데도 버린다. T의 하한은 정상 혼잡의 PAUSE 지속 분포가 정하고, 상한은 "데드락을 얼마나 오래 견딜 수 있나" 가 정한다. 실제 장비에서 이 둘의 간격이 넓은 이유: PAUSE 한 번은 마이크로초 단위인데 watchdog은 (확인 안 된 기억으로) 수백 ms 단위다.

</details>

### 연습 5 — Tarjan SCC로 "사이클에 걸린 버퍼 전부"

반복형 Tarjan(1972)을 구현해 정상 BDG, bounce BDG, 두 경로 집합을 합친 BDG의 SCC를 구한다. 크기 > 1 또는 자기 루프인 SCC만 "위험" 이다.

<details>
<summary>답 보기</summary>

정상 BDG: SCC 20개 전부 크기 1 (위험 없음). bounce BDG: 위험 SCC 하나 = 사이클 버퍼 4개. 정상 + bounce를 합쳐도 그 하나뿐 — 정상 경로는 층을 올리는 간선만 더하므로 새 고리를 못 만들고, 고리를 닫는 건 bounce 간선(층 2 → 층 1)이다. DFS의 `find_cycle` 은 사이클 하나를 주고 끝나지만, SCC는 "이 버퍼 집합 안의 어떤 경로든 문제" 라는 전체 그림을 준다 — 검사기 리포트로는 SCC가 낫다. Kosaraju(역그래프 2-pass)와의 비교는 ALGO-A10.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py      # 13/13 passed, exit 0
```

실제 출력:

```text
-- 연습 1: bigger buffers --
PASS  XOFF 8/32/128 all deadlock, later and with more trapped packets: [(8, 82, 64), (32, 178, 236), (128, 562, 908)]
PASS  trapped packets grow ~linearly with XOFF (64 -> 908 for 8 -> 128)
-- 연습 2: load threshold --
PASS  2 hosts/path: first deadlocking rate = 0.26 (shared-link demand 4r crosses 1.0)
PASS  1 host/path at line rate (demand 2.0!): PAUSE fires 222 times but no deadlock
      buffers that ever asserted PAUSE: (L1,h0,p3), (L2,h0,p3); peak occupancy of cycle buffers: 1
PASS  ... because only HOST-side buffers ever pause; the cycle buffers never reach XOFF
-- 연습 3: finite lossless classes --
PASS  classes_needed: 0-bounce 1, 1-bounce 2, 2-bounce 3
PASS  2-bounce with 3 lossless classes: [3,3,3,4,4,5,5], no LOSSY
      12 random 1-bounce paths, one class: cycle (L1,S1,p3) -> (S0,L1,p3) -> (L2,S0,p3) -> (S1,L2,p3) -> (L1,S1,p3)
PASS  12 random 1-bounce paths: one class -> cycle; bumped classes -> acyclic
-- 연습 4: watchdog false positive --
      incast max continuous PAUSE-blocked age = 27, drops by T = {10: 594, 20: 288, 27: 143, 28: 0, 40: 0}
PASS  T <= max_age drops packets in pure congestion; T > max_age drops none
-- 연습 5: Tarjan SCC --
PASS  healthy BDG: 20 SCCs, all trivial
PASS  bounce BDG: exactly one non-trivial SCC = the 4 cycle buffers
PASS  healthy routes + bounce routes together: still that one SCC (only bounce edges close loops)
PASS  Tarjan on 3-ring + tail + self-loop: [[a,b,c],[t],[x]] with x self-loop flagged

13/13 passed
```

## 10. 핵심 정리

- BDG 노드 = **(스위치, 입력 포트, 우선순위)** 버퍼 (PFC가 PAUSE를 거는 단위). 간선 = 그 버퍼의 패킷이 다음에 들어갈 버퍼.
- up-down 라우팅 → 간선이 층을 엄격히 올림 → **비순환** → 데드락 자유. bounce(내려왔다 다시 올라감)가 층을 거꾸로 가는 간선을 만들어 CBD가 생긴다.
- 데드락 증상: 고리의 모든 버퍼가 PAUSE 중, **소스를 멈춰도 처리량 0**, drop 0. 혼잡은 병목이 계속 흐르고 정지 후 비워진다.
- CBD는 필요조건, 충분조건 아님: 부하·중재 패턴에 따라 난다 (호스트 1대: 무사, 2대: r=0.26부터 데드락). 그래서 CBD 자체를 없앤다.
- 대책 ↔ Coffman: up-down·우선순위 상승 = **순환 대기**, watchdog = **비선점**, PFC를 끔(손실 허용) = **점유 대기**. 상호 배제는 못 깬다.
- 버퍼/헤드룸을 키우면 늦게 날 뿐 더 많이 갇힌다 (64 → 908). watchdog T가 정상 PAUSE 지속보다 짧으면 혼잡에서도 버린다.
- 검사기: DFS 3색(GRAY로의 back edge)로 사이클 하나, Tarjan SCC로 걸린 버퍼 전부. 정적 검사(라우팅) = lockdep, 런타임 탐지(wait-for 사슬) = 실제 교착 탐지.

## 참고자료

- IEEE Std 802.1Qbb — Priority-based Flow Control (현재는 IEEE 802.1Q 본문에 통합). PFC 프레임 = 우선순위별 enable 벡터 + 우선순위별 시간(quanta).
- C. Guo et al., "RDMA over Commodity Ethernet at Scale", ACM SIGCOMM 2016 — Microsoft의 RoCEv2 배포, PFC 데드락·PAUSE storm·watchdog 사례 (제목·내용 요약은 기억에 의존, 원문 확인 권장)
- S. Hu et al., "Deadlocks in Datacenter Networks: Why Do They Form, and How to Avoid Them", ACM HotNets 2016 (제목 기억에 의존)
- S. Hu et al., "Tagger: Practical PFC Deadlock Prevention in Data Center Networks", ACM CoNEXT 2017 (제목 기억에 의존, 태그 규칙 세부 미확인)
- E. G. Coffman, M. Elphick, A. Shoshani, "System Deadlocks", ACM Computing Surveys 3(2), 1971 — 네 가지 필요조건
- W. J. Dally, C. L. Seitz, "Deadlock-Free Message Routing in Multiprocessor Interconnection Networks", IEEE Trans. Computers, 1987 — 채널 의존 그래프 비순환 ⇒ 데드락 자유
- J. Duato, "A New Theory of Deadlock-Free Adaptive Routing in Wormhole Networks", IEEE TPDS, 1993 — 적응 라우팅의 escape 채널 조건
- R. Tarjan, "Depth-First Search and Linear Graph Algorithms", SIAM J. Computing 1(2), 1972 — SCC
- R. Arpaci-Dusseau, A. Arpaci-Dusseau, *Operating Systems: Three Easy Pieces*, ch. 32 "Common Concurrency Problems" (v0.91) — 데드락 조건과 예방
- up*/down* 라우팅의 원형: DEC Autonet (Schroeder et al., 1991) — 서지 정보 기억에 의존
- SONiC / 스위치 벤더의 PFC watchdog 문서 — 감지·복구 시간 기본값과 동작(drop/forward/alert)은 장비 문서로 확인할 것
- 관련 레슨: DCN-M04(PFC 헤드룸) · DCN-M07(DCQCN, 혼잡 제어) · OS-M10(4가지 조건, 철학자) · OS-M11(mini lockdep) · ALGO-A10(SCC) · STOR-M08(2PL 데드락 탐지) · STOR-M10(래치 순서) · SOCX-M05(NoC CDG, Dally–Seitz)
