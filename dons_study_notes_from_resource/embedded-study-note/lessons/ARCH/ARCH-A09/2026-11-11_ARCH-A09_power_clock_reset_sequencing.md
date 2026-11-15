---
id: ARCH-A09
time: 120분
updated: 2026-11-11
---
# ARCH-A09 파워·클럭·리셋 도메인 시퀀싱 설계

## 학습 목표

- 레일 → PLL lock → 클럭 enable → 리셋 해제 → 주변장치 init 의 순서를 **코드가 아니라 데이터**(노드 + 간선 + 최소 지연 + 타임아웃)로 적고, 위상 정렬로 순서를 뽑으며, 사이클을 "설정 오류" 로 거부할 수 있다.
- 그 그래프를 그대로 따르는 **이벤트 구동 시퀀서 FSM** 을 짜서, 병렬 power-up 이 그래프의 **임계 경로**(890.010 µs) 에 정확히 끝나고, power-down 은 역순 + isolation 먼저 + retention save 뒤 rail off 로 도는 것을 보인다.
- 아무 이벤트 로그나 받아 11 종류의 위반(순서·최소 지연·rail-good 전 클럭·lock 전 PLL 사용·클럭 안정 전 리셋 해제·끄는 순서·isolation 전 rail off·save 전 rail off·이른 ISO 해제·손실 미대응·상태 불일치)을 **간선과 타임스탬프와 함께 정확히 한 번씩** 보고하는 체커를 만든다.
- 결함 주입(PG 영원히 안 옴, PLL lock 실패, 동작 중 unlock, brown-out, PG 글리치, 늦은 PG)마다 시퀀서가 어떤 **안전 상태** 로 가고 AON 원인 레지스터에 무엇을 남기는지 설계·검증하고, 일부러 틀린 시퀀서 4 종을 체커가 잡는지 확인한다.
- 면접 질문 "리셋을 PLL lock 전에 풀면?", "isolation 은 언제 켜고 언제 푸나?", "brown-out 이면 시퀀서는 무엇을 하나?" 에 그림 하나로 답한다.

## 1. 문제

"리셋을 PLL lock 전에 풀면 무슨 일이 나나요? 그 순서는 누가, 어디에 정의하나요?" — SoC 아키텍처·실리콘 검증·펌웨어 bring-up 면접에서 반드시 나오는 질문이고, first silicon 이 **벽돌이 되는 가장 흔한 이유** 가 이 순서다.

이 레슨은 **칩 안쪽, 설계 쪽** 이다. 같은 주제의 다른 두 레슨과 자리가 다르다:

| 레슨 | 자리 | 하는 일 |
|---|---|---|
| SIV-A06 | 보드 · 측정 | DAQ 로 잰 레일 **파형** 에서 순서·ramp·glitch 판정 (아날로그 쪽) |
| SIV-A11 | 칩이 나온 뒤 · 시험 | mock SoC 레지스터로 게이트 접근 에러, retention 값, isolation clamp 를 **시험** |
| **ARCH-A09** | 칩 설계 · PMU 시퀀서 | 순서를 **명세(그래프)** 로 적고, 그걸 실행하는 **시퀀서** 와 로그를 판정하는 **체커** 를 설계 |

A06 의 "레일 하나의 파형" 이나 A11 의 "도메인 레지스터 시험" 은 다시 하지 않는다. 여기 대상은 PMU(power management unit) 안의 시퀀서 FSM — 하드웨어 상태기계이든 AON 마이크로컨트롤러 펌웨어이든 — 가 따르는 의존성 그래프다.

가상의 edge-AI SoC **EDGE-1** (모든 시간은 대표값, 실제 값은 SoC 데이터시트의 power sequencing 표 · PMIC OTP · 전원 설계서가 정한다):

```svg
<svg viewBox="0 0 760 400" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="a09ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">EDGE-1 의존성 그래프 (굵은 선 = 임계 경로 890.010 µs)</text>
  <g fill="currentColor" font-size="12">
    <text x="40" y="38">RAIL (ramp→PG)</text><text x="190" y="38">PLL (lock)</text><text x="345" y="38">CLK (gate on)</text>
    <text x="500" y="38">RST (해제)</text><text x="650" y="38">INIT</text>
  </g>
  <g stroke="currentColor">
    <rect x="30" y="50" width="110" height="30" rx="4" class="f-warn"/>
    <rect x="30" y="110" width="110" height="30" rx="4" class="f-mut"/>
    <rect x="30" y="170" width="110" height="30" rx="4" class="f-acc"/>
    <rect x="30" y="230" width="110" height="30" rx="4" class="f-acc"/>
    <rect x="30" y="290" width="110" height="30" rx="4" class="f-warn"/>
    <rect x="180" y="110" width="110" height="30" rx="4" class="f-mut"/>
    <rect x="180" y="290" width="110" height="30" rx="4" class="f-warn"/>
    <rect x="335" y="50" width="110" height="30" rx="4" class="f-ok"/>
    <rect x="335" y="170" width="110" height="30" rx="4" class="f-ok"/>
    <rect x="335" y="230" width="110" height="30" rx="4" class="f-ok"/>
    <rect x="335" y="290" width="110" height="30" rx="4" class="f-warn"/>
    <rect x="490" y="50" width="110" height="30" rx="4" class="f-ok"/>
    <rect x="490" y="170" width="110" height="30" rx="4" class="f-ok"/>
    <rect x="490" y="230" width="110" height="30" rx="4" class="f-ok"/>
    <rect x="490" y="290" width="110" height="30" rx="4" class="f-warn"/>
    <rect x="640" y="230" width="110" height="30" rx="4" class="f-mut"/>
    <rect x="640" y="290" width="110" height="30" rx="4" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="85" y="69">VDD_AON 300</text><text x="85" y="129">VDD_PLL 100</text><text x="85" y="189">VDD_CORE 250</text>
    <text x="85" y="249">VDD_IO 400</text><text x="85" y="309">VDD_DDR 200</text>
    <text x="235" y="129">PLL_SYS 50</text><text x="235" y="309">PLL_DDR 80</text>
    <text x="390" y="69">CLK_AON 32k</text><text x="390" y="189">CLK_CORE 800M</text><text x="390" y="249">CLK_IO 100M</text><text x="390" y="309">CLK_DDR 400M</text>
    <text x="545" y="69">RST_AON</text><text x="545" y="189">RST_CORE</text><text x="545" y="249">RST_IO</text><text x="545" y="309">RST_DDR</text>
    <text x="695" y="249">INIT_UART 20</text><text x="695" y="309">INIT_DDRC 150</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#a09ar)">
    <path d="M30 65 L18 65 L18 125 L28 125"/><path d="M18 125 L18 185 L28 185"/><path d="M18 185 L18 245 L28 245"/>
    <path d="M140 65 L333 65"/>
    <path d="M140 125 L178 125"/><path d="M140 130 L178 300"/>
    <path d="M290 125 L333 182"/><path d="M290 130 L333 240"/>
    <path d="M140 185 L333 185"/><path d="M140 245 L333 245"/>
    <path d="M85 320 L85 345 L390 345 L390 322"/>
    <path d="M445 65 L488 65"/><path d="M445 185 L488 185"/><path d="M445 245 L488 245"/>
    <path d="M600 65 L615 65 L615 178 L602 178"/><path d="M615 178 L615 238 L602 238"/>
    <path d="M600 245 L638 245"/><path d="M600 190 L638 296"/>
  </g>
  <g stroke="currentColor" stroke-width="3" fill="none" marker-end="url(#a09ar)">
    <path d="M18 245 L18 305 L28 305"/><path d="M140 305 L178 305"/><path d="M290 305 L333 305"/>
    <path d="M445 305 L488 305"/><path d="M600 305 L638 305"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="150" y="60">+200 µs (발진기 기동)</text><text x="460" y="60">K=4</text>
    <text x="460" y="180">K=4</text><text x="460" y="240">K=4</text><text x="460" y="300">K=4</text>
    <text x="0" y="350">+150</text><text x="300" y="360">VDD_DDR → CLK_DDR</text><text x="605" y="300">+10</text>
    <text x="10" y="377">숫자 = ramp/lock/init 시간(µs) · 클럭 주파수. K=4 = 리셋 동기화기용 클럭 4 사이클.</text>
    <text x="10" y="394">RST_AON → RST_CORE/RST_IO, RST_CORE → RST_DDR/INIT_DDRC 는 아래 표 참고</text>
  </g>
</svg>
```

전체 간선 (24 개, `build_soc()` 가 그대로 이 표):

| 간선 | 최소 지연 | 의미 |
|---|---|---|
| VDD_AON → VDD_PLL / VDD_CORE / VDD_IO / VDD_DDR | 0 / 50 / 100 / 150 µs | AON 이 먼저, 나머지는 시차를 두고 (돌입 전류 분산 — 가정) |
| VDD_AON → CLK_AON | 200 µs | 32.768 kHz 발진기 기동 시간 |
| VDD_PLL → PLL_SYS, PLL_DDR | 10 µs | 아날로그 레일 정착 후 PLL enable |
| VDD_DDR → PLL_DDR | 0 | DDR PLL 은 DDR 레일도 필요 |
| PLL_x → CLK_y, VDD_y → CLK_y | 0 | 클럭 게이트는 **소스 lock + 목적지 레일 good** 뒤 |
| CLK_x → RST_x | `SYNC` = ⌈4 사이클⌉ | AON 122.071 µs · CORE 5 ns · IO 40 ns · DDR 10 ns |
| RST_AON → RST_CORE, RST_IO · RST_CORE → RST_DDR | 0 | 리셋 트리 |
| RST_IO → INIT_UART · RST_DDR → INIT_DDRC · RST_CORE → INIT_DDRC | 1 µs · 10 µs · 0 | init 은 리셋 해제 후 (DDR init 은 CPU 가 돌린다) |

도메인: `AON`(항상 켜짐, 원인 레지스터가 산다) · `ANA`(PLL 레일) · `CORE`(isolation + **retention**) · `IO`(isolation) · `DDR`(isolation). 전원이 들어올 때 isolation 셀은 **clamp 상태** 로 시작한다(AON 쪽 셀이므로).

로그 형식 (시퀀서 출력과 손으로 쓴 로그가 같다): `<t_µs> <EVENT> <노드|도메인>` — `RAIL_EN RAIL_PG RAIL_PG_LOST RAIL_OFF PLL_EN PLL_LOCK PLL_UNLOCK PLL_OFF CLK_ON CLK_OFF RST_DEASSERT RST_ASSERT INIT_START INIT_DONE ISO_ON ISO_OFF RET_SAVE RET_RESTORE FAULT`.

### Part A — 그래프 · 위상 정렬 · power-up/down FSM

- `topo_order(g, out)` — Kahn, 동률은 작은 번호 먼저(로그가 매번 같게), 사이클이면 -1 → `seq_init` 이 설정 오류로 거부.
- `ready_time(sq, v)` — 선행 노드가 모두 완료(lost 아님)면 `max(done_at[u] + min_ns)`, 아니면 -1.
- 하네스의 이벤트 구동 FSM(`OFF → UP → ON / PART / FAULT`)이 이 둘로 노드를 병렬로(또는 `serial` 로 하나씩) 켠다. RAIL/PLL/INIT 은 plant 가 완료 이벤트를 늦게 주고, 타임아웃 타이머가 걸린다.
- `seq_shutdown(sq, mask)` — 위상 **역순** 으로 끄되, 도메인의 첫 노드를 끄기 직전에 `ISO_ON`, retention 도메인이면 `CLK_OFF → RET_SAVE`.
- 확인: 병렬 power-up = 890.010 µs (손 계산 임계 경로), 직렬 = 1551.040 µs, 전체 power-down 로그 · sleep(CORE)/wake 로그가 기대 문자열과 한 글자까지 같고 체커 위반 0.

### Part B — 체커: 어떤 로그든

- `chk_action` (켜는 쪽 5 종) · `chk_off` (끄는 쪽 3 종) · `chk_loss_due` (외부 손실 후 `T_REACT` = 5 µs 안에 하위 자원을 내렸는가). 상태 추적 · BAD_EVENT · ISO_EARLY 는 하네스.
- 작은 그래프(`build_mini`)에서 손으로 쓴 정상 로그 1 개 + 위반 로그 13 개(각각 **정확히 1 개**, 종류·노드·간선까지 일치) + 위반이 아니어야 하는 2 개 + 위반 3 개짜리 1 개.
- 시퀀서 로그의 인접 이벤트 교환 47 개 → 몇 개를 잡고, 놓친 것은 왜 놓쳤나. "간선을 빠뜨린 스펙" 으로 만든 시퀀서를 **같은 스펙** 의 체커는 통과시킨다.

### Part C — 결함 주입

- `seq_fault(sq, reason, v)` — FAULT 기록, **sticky** 원인 레지스터, 안전 상태 선택(PLL unlock = 그 PLL 하위만 / 나머지 = AON 빼고 전부), shutdown.
- 9 개 시나리오(PG 안 옴 · lock 실패 · init 실패 · 동작 중 unlock · brown-out · 1 µs 글리치 · 늦은 PG · power-up 도중 brown-out · PLL 레일 글리치) 각각 기대 상태 + 원인 + 시각 + **체커 위반 0**.
- 일부러 틀린 시퀀서 4 개(ISO 누락 · SAVE 누락 · K=0 · unlock 무시)를 체커가 기대한 종류·개수로 잡는다.

## 2. Problem (English)

> **Power / clock / reset domain sequencing.** An SoC's bring-up dependencies are given as data: nodes are rails (enable → ramp → power-good), PLLs (enable → lock), clock gates, reset deassertions and peripheral inits; an edge `u → v, d` means *v may start only after u has completed and at least d ns have elapsed*. A clock→reset edge requires the clock to have run for K cycles (reset synchronizer). Domains group a rail, clock and reset and may have isolation clamps and retention.
>
> A. Implement `int topo_order(const graph_t*, int *out)` (deterministic, -1 on a cycle = configuration error) and `int64_t ready_time(const seq_t*, int v)`. Using them, an event-driven sequencer powers up every node as early as allowed (rails/PLLs/inits complete asynchronously, each with a timeout). Implement `void seq_shutdown(seq_t*, const bool *mask)`: power down in reverse topological order; before a domain loses anything, clamp its isolation; for a retention domain whose rail is going off, stop its clock and save state first.
>
> B. Implement a checker that, for **any** event log, reports every violation exactly once with the edge and timestamps: order, minimum delay, clock/PLL enabled before its rail is good, clock used before PLL lock, reset deasserted before its clock ran K cycles, resource turned off while a dependent is on, rail off before isolation, rail off before retention save, isolation released early, external loss (PG drop / PLL unlock) not handled within T_react, and state-inconsistent events.
>
> C. Implement `void seq_fault(seq_t*, int reason, int v)` and inject faults: PG never asserts, PLL never locks, PLL loses lock while running, brown-out, a PG glitch shorter / longer than the deglitch filter, PG arriving after its timeout. Each must end in a defined safe state with a sticky reset cause, and the checker must pass on the sequencer's own log.
>
> Example: nominal power-up of the 17-node SoC finishes at 890.010 µs (critical path VDD_AON → VDD_DDR → PLL_DDR → CLK_DDR → RST_DDR → INIT_DDRC); a hand-written log that deasserts `RST_A` 2 µs after `CLK_A` (needs 4 µs) yields exactly `RST_BEFORE_CLK edge CLK_A->RST_A`.
>
> Follow-ups: what happens if reset is released before PLL lock? Why clamp isolation before the rail drops and release it only after reset? What does your checker miss? Why does a checker generated from the same spec as the sequencer not catch spec bugs?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
```

TODO 는 7 개: `topo_order`, `ready_time`, `seq_shutdown` (A) · `chk_action`, `chk_off`, `chk_loss_due` (B) · `seq_fault` (C). 그래프 빌더, 로그 파서, plant(완료 이벤트 · 타임아웃 · 외부 사건 큐), FSM 본체(`issue`, `issue_ready`, `handle`, `seq_run`), 체커의 상태 추적은 starter 와 solution 이 같다. 처음 실행 결과 (실제 출력 일부, exit 1):

```text
== Part A: 의존성 그래프 → 위상 정렬 → power-up/down FSM ==
FAIL starter.c:669  topo_order(&G_SOC, ord) == 17 && order_valid(&G_SOC, ord, 17)
...
FAIL starter.c:699  seq_up(&SQ) == 0 && SQ.state == SQ_ON
  power-up: parallel 0.000us (= critical path), serial 0.000us, events 0
...
== Part B: 체커 — 손으로 쓴 로그 ==
FAIL starter.c:825  ok
  ORDER            -> 0 violation(s)   <-- 기대와 다름
...
15/69 passed
```

스텁 `topo_order` 가 0 을 돌려주니 `seq_init` 이 "그래프 설정 오류" 로 보고 시퀀서가 아예 시작하지 않는다(로그 0 개). 통과하는 15 개는 TODO 와 상관없는 것(파서 오류 처리, 정상 로그 = 위반 0 등)이다.

보기 모드:

```bash
./so show up          # 병렬 power-up 로그 (serial / down / sleep 도 있음)
./so show brownout    # 결함 시나리오: pg_never lock_never init_never unlock brownout glitch late_pg bo_up pllrail
./so gantt            # 노드별 시작/완료 시각
./so check my.log mini    # 손으로 쓴 로그 파일 검사 (mini 또는 soc 그래프)
```

## 4. 힌트

<details><summary>힌트 1 — 위상 정렬이 결정적이어야 하는 이유</summary>

같은 시각에 준비되는 노드가 여럿이면, 정렬 순서가 곧 **로그 순서** 다. 순서가 실행마다 바뀌면 기대 로그 비교가 불가능하고, 실리콘에서는 "같은 시각" 이 사실 "같은 PMU 사이클 안의 순서" 다. Kahn 에서 진입차수 0 인 노드 중 **가장 작은 번호** 를 고르면 된다. 고를 게 없는데 남은 노드가 있으면 그게 사이클이다.
</details>

<details><summary>힌트 2 — ready_time 은 "언제" 와 "아직 모름" 을 구분한다</summary>

모든 선행 노드가 `NS_ON` 이고 `lost` 가 아니어야 시각을 계산할 수 있다. 하나라도 아니면 -1 (아직 모름 — 그 노드가 끝날 때 다시 묻는다). 선행이 다 끝났는데 min 지연이 남았으면 미래 시각이 나오고, 하네스가 그 시각에 `Q_WAKE` 를 건다. 선행이 없는 노드(VDD_AON)는 0.
</details>

<details><summary>힌트 3 — power-down 에서 무엇을 먼저 하나</summary>

역순이 기본이다: 의존하는 쪽을 먼저 끄고 의존되는 쪽을 나중에. 그런데 역순만으로는 두 가지가 빠진다. ① 꺼지는 쪽 출력이 살아 있는 쪽으로 쓰레기를 내보내지 않게 **그 도메인의 첫 동작 전에** clamp(ISO_ON). ② retention 은 클럭이 멈춘 뒤, 리셋이 상태를 지우기 **전에** save — 그래서 retention 도메인은 역순(RST_ASSERT 가 CLK_OFF 보다 먼저)을 깨고 CLK_OFF → RET_SAVE → RST_ASSERT 가 된다. 이미 brown-out 으로 rail 이 빠졌다면 save 는 불가능하다(조건에 넣을 것).
</details>

<details><summary>힌트 4 — 체커가 위반을 "정확히 한 번" 내려면</summary>

위반은 **이벤트 × 간선** 단위로 낸다. 켜는 이벤트 하나에 대해 들어오는 간선을 한 번씩만 보고, 간선마다 "완료 안 됨" 과 "너무 이름" 중 하나만 낸다. 종류는 (u 의 kind, v 의 kind) 로 정한다 — CLK→RST 간선이면 둘 다 `RST_BEFORE_CLK`. 손실(PG_LOST/UNLOCK)은 시한이 지날 때 **한 번만** 평가하고 목록에서 지운다. 복귀(PG/LOCK)하면 하네스가 먼저 지운다.
</details>

<details><summary>힌트 5 — fault 의 안전 상태는 둘이다</summary>

PLL unlock 은 전원 문제가 아니다 — 그 PLL 에서 나온 클럭을 쓰는 것들만 멈추고 리셋하면 된다(`closure(g, v, mask)`). 레일 문제(타임아웃, brown-out)나 init 실패는 보수적으로 AON 을 뺀 전부를 내린다. AON 을 남기는 이유: 원인 레지스터와 wake 로직이 거기 산다(ARCH-B14). 원인 노드 자신이 AON 이면 전부(= 사실상 POR). 원인은 **처음 것만** 남긴다(sticky).
</details>

## 5. 풀이

### Part A — 그래프에서 순서와 시간을 뽑는다

**위상 정렬.** `build_soc()` 는 노드를 일부러 위상 순서로 선언했으므로 결과는 `0..16` 그대로다(CHECK). 역순으로 선언한 mini 그래프도 유효한 순서가 나오고(첫 노드 VDD_A), `INIT_A → VDD_A` 간선을 넣으면 -1 → `seq_init` = -1, `seq_up` = -1, 로그 0 개. 사이클은 "만족 불가능한 스펙" 이다 — 칩에서 이게 PMU 펌웨어 테이블에 들어가면 **영원히 기다리는 시퀀서**(벽돌)가 된다. 그래서 런타임이 아니라 로드 시점에 거부한다.

**병렬 power-up 의 시간 = 임계 경로.** 각 노드의 시작 = max(선행 완료 + 지연), 완료 = 시작 + 지속. 손으로:

| 노드 | 시작 (µs) | 완료 (µs) | 지배하는 선행 |
|---|---|---|---|
| VDD_AON | 0 | 300 | — |
| VDD_PLL / VDD_CORE / VDD_IO / VDD_DDR | 300 / 350 / 400 / 450 | 400 / 600 / 800 / 650 | AON + 0/50/100/150 |
| CLK_AON | 500 | 500 | AON + 200 (발진기) |
| PLL_SYS · PLL_DDR | 410 · **650** | 460 · **730** | VDD_PLL+10 · **VDD_DDR** |
| CLK_CORE · CLK_IO · CLK_DDR | 600 · 800 · 730 | = | 레일이 PLL 보다 늦다 |
| RST_AON | 622.071 | = | 500 + ⌈4 / 32768 Hz⌉ = 122.071 |
| RST_CORE | **622.071** | = | 600.005 이 아니라 **RST_AON** 이 지배 |
| RST_IO · RST_DDR | 800.040 · 730.010 | = | 클럭 + 4 사이클 |
| INIT_UART | 801.040 | 821.040 | RST_IO + 1 |
| INIT_DDRC | 740.010 | **890.010** | RST_DDR + 10 |

시뮬레이터 실제 출력(`./so show up`) 이 이 표와 같다:

```text
    0      0.000us  RAIL_EN      VDD_AON
    1    300.000us  RAIL_PG      VDD_AON
    2    300.000us  RAIL_EN      VDD_PLL
    3    350.000us  RAIL_EN      VDD_CORE
    4    400.000us  RAIL_PG      VDD_PLL
    5    400.000us  RAIL_EN      VDD_IO
    6    410.000us  PLL_EN       PLL_SYS
    7    450.000us  RAIL_EN      VDD_DDR
    8    460.000us  PLL_LOCK     PLL_SYS
    9    500.000us  CLK_ON       CLK_AON
   10    600.000us  RAIL_PG      VDD_CORE
   11    600.000us  CLK_ON       CLK_CORE
   12    622.071us  RST_DEASSERT RST_AON
   13    622.071us  RST_DEASSERT RST_CORE
   14    622.071us  ISO_OFF      CORE
   15    650.000us  RAIL_PG      VDD_DDR
   16    650.000us  PLL_EN       PLL_DDR
   17    730.000us  PLL_LOCK     PLL_DDR
   18    730.000us  CLK_ON       CLK_DDR
   19    730.010us  RST_DEASSERT RST_DDR
   20    730.010us  ISO_OFF      DDR
   21    740.010us  INIT_START   INIT_DDRC
   22    800.000us  RAIL_PG      VDD_IO
   23    800.000us  CLK_ON       CLK_IO
   24    800.040us  RST_DEASSERT RST_IO
   25    800.040us  ISO_OFF      IO
   26    801.040us  INIT_START   INIT_UART
   27    821.040us  INIT_DONE    INIT_UART
   28    890.010us  INIT_DONE    INIT_DDRC
state=ON cause=NONE t_up=890.010us checker violations=0
```

```svg
<svg viewBox="0 0 760 340" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">병렬 power-up 타임라인 (./so gantt 실측, 굵은 테두리 = 임계 경로)</text>
  <g stroke="currentColor" stroke-dasharray="2 4">
    <line x1="130" y1="30" x2="130" y2="310"/><line x1="263" y1="30" x2="263" y2="310"/><line x1="397" y1="30" x2="397" y2="310"/>
    <line x1="530" y1="30" x2="530" y2="310"/><line x1="663" y1="30" x2="663" y2="310"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="130" y="326">0</text><text x="263" y="326">200</text><text x="397" y="326">400</text><text x="530" y="326">600</text><text x="663" y="326">800 µs</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="45">VDD_AON</text><text x="10" y="61">VDD_PLL</text><text x="10" y="77">VDD_CORE</text><text x="10" y="93">VDD_IO</text>
    <text x="10" y="109">VDD_DDR</text><text x="10" y="125">CLK_AON</text><text x="10" y="141">PLL_SYS</text><text x="10" y="157">PLL_DDR</text>
    <text x="10" y="173">CLK_CORE</text><text x="10" y="189">CLK_IO</text><text x="10" y="205">CLK_DDR</text><text x="10" y="221">RST_AON</text>
    <text x="10" y="237">RST_CORE</text><text x="10" y="253">RST_IO</text><text x="10" y="269">RST_DDR</text><text x="10" y="285">INIT_UART</text>
    <text x="10" y="301">INIT_DDRC</text>
  </g>
  <g stroke="currentColor">
    <rect x="130" y="35" width="200" height="12" class="f-warn" stroke-width="2.5"/>
    <rect x="330" y="51" width="67" height="12" class="f-acc"/>
    <rect x="363" y="67" width="167" height="12" class="f-acc"/>
    <rect x="397" y="83" width="266" height="12" class="f-acc"/>
    <rect x="430" y="99" width="133" height="12" class="f-warn" stroke-width="2.5"/>
    <rect x="403" y="131" width="34" height="12" class="f-mut"/>
    <rect x="563" y="147" width="54" height="12" class="f-warn" stroke-width="2.5"/>
    <rect x="664" y="275" width="13" height="12" class="f-acc"/>
    <rect x="623" y="291" width="100" height="12" class="f-warn" stroke-width="2.5"/>
  </g>
  <g stroke="currentColor" stroke-width="2">
    <line x1="463" y1="113" x2="463" y2="127"/><line x1="530" y1="161" x2="530" y2="175"/><line x1="663" y1="177" x2="663" y2="191"/>
    <line x1="617" y1="193" x2="617" y2="207" stroke-width="3.5"/><line x1="545" y1="209" x2="545" y2="223"/>
    <line x1="545" y1="225" x2="545" y2="239"/><line x1="663" y1="241" x2="663" y2="255"/><line x1="617" y1="257" x2="617" y2="271" stroke-width="3.5"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="470" y="125">500 (osc +200)</text><text x="552" y="221">622.071 (32 kHz × 4)</text><text x="552" y="237">622.071 ← RST_AON 지배</text>
    <text x="440" y="141">lock 410→460</text><text x="625" y="157">650→730</text><text x="676" y="269">730.010</text>
    <text x="682" y="285">→821.040</text><text x="600" y="314">740.010 → 890.010</text>
  </g>
</svg>
```

읽을 것 세 가지:

1. **임계 경로는 DDR 쪽** 이다: AON(300) → +150 → DDR 레일(200) → PLL_DDR lock(80) → CLK_DDR → +10 ns → +10 µs → DDR init(150) = 890.010 µs. 연습 1 의 CPM 이 여유(slack)를 계산한다: VDD_IO 는 68.970 µs, RST_CORE 107.939 µs 여유 — IO 레일을 빠르게 해도 부팅 시간은 그대로고, DDR 레일을 50 µs 빠르게 하면 840.010 µs 가 된다.
2. **저속 클럭의 리셋 동기화 지연이 실제로 보인다.** CORE 의 4 사이클은 5 ns 지만 AON 32.768 kHz 의 4 사이클은 122.071 µs 이고, RST_CORE 가 RST_AON 뒤에 묶여 있어 622.071 µs 에 풀린다. "리셋 동기화기는 몇 사이클이면 되나" 보다 **"어느 클럭의 사이클인가"** 가 중요한 이유다 (동기화기 RTL 은 RTLDV-B06).
3. **ISO_OFF 는 리셋 해제 직후** 에 같은 시각으로 나온다. clamp 를 푸는 조건 = 그 도메인이 리셋에서 나와 출력이 정의됨. 그 전에 풀면 AON 쪽이 리셋 중의 값(또는 X)을 본다.

**직렬(한 번에 하나) 시퀀서** — 위상 순서대로 앞 노드가 끝나야 다음을 시작하는 단순한 펌웨어 루프 — 는 같은 그래프에서 **1551.040 µs** (CHECK). VDD_CORE 가 350 µs 에 시작할 수 있는데 VDD_PLL 이 끝나는 400 µs 를 기다리고, VDD_IO 는 650, VDD_DDR 는 1050 µs 에야 시작한다. 레일 ramp 가 직렬로 더해지는 것이 1.74 배 차이의 대부분이다. 실제 PMIC 는 시퀀스 슬롯(slot) + 슬롯 간 지연으로 이 둘 사이 어딘가를 구현한다(같은 슬롯의 레일은 동시에).

**Power-down.** 2000 µs 에 전체 power-down (실제 출력, CHECK 문자열과 같다):

```text
2000.000us ISO_ON DDR            ← DDR 도메인 첫 노드(INIT_DDRC) 직전
2000.000us ISO_ON IO
2000.000us RST_ASSERT RST_DDR
2000.000us RST_ASSERT RST_IO
2000.000us ISO_ON CORE
2000.000us CLK_OFF CLK_CORE      ← retention: 클럭 정지가 먼저
2000.000us RET_SAVE CORE         ← 리셋이 상태를 지우기 전에
2000.000us RST_ASSERT RST_CORE
2000.000us RST_ASSERT RST_AON
2000.000us CLK_OFF CLK_DDR
2000.000us CLK_OFF CLK_IO
2000.000us PLL_OFF PLL_DDR
2000.000us PLL_OFF PLL_SYS
2000.000us CLK_OFF CLK_AON
2000.000us RAIL_OFF VDD_DDR      ← 레일은 맨 끝, 역순
2000.000us RAIL_OFF VDD_IO
2000.000us RAIL_OFF VDD_CORE
2000.000us RAIL_OFF VDD_PLL
2000.000us RAIL_OFF VDD_AON
```

모두 같은 타임스탬프인 것은 모델의 단순화다(클럭 게이팅 · 리셋 assert 는 수 사이클, 레일 방전은 수십~수백 µs — 이 모델은 off 쪽 지연을 0 으로 두었다). **순서** 가 핵심이고, 실제 설계는 off 쪽에도 최소 지연(예: 레일 방전 후 다시 켜기 전 최소 off 시간)을 간선으로 둔다.

| 단계 | 왜 이 순서 |
|---|---|
| ISO_ON 을 도메인의 **첫** 동작 전에 | 리셋 assert · 클럭 정지 · 전원 차단 중 어느 것이든 출력이 바뀐다. 살아 있는 쪽이 보기 전에 고정 |
| retention: CLK_OFF → RET_SAVE → RST_ASSERT | 클럭이 돌면 save 중 값이 바뀐다. 리셋이 먼저면 지워진 값을 저장한다. 그래서 일반 역순(RST 먼저)을 이 도메인에서만 깬다 |
| 리셋 → 클럭 → PLL → 레일 | 의존 역순. 레일을 끄기 전에 그 레일 위의 클럭이 서 있어야 하고(체커 OFF_ORDER), 그 도메인 리셋이 걸려 있어야 다음 power-up 이 리셋 상태에서 시작한다 |
| INIT 은 이벤트 없음 | 리셋 assert 가 init 결과를 무효화한다(체커도 RST_ASSERT 에서 그 위 INIT 을 내림) |

**Sleep(CORE) + wake.** `seq_down(sq, VDD_CORE)` 는 VDD_CORE 의 closure(VDD_CORE, CLK_CORE, RST_CORE, RST_DDR, INIT_DDRC)만 내린다 — DDR 리셋이 CORE 리셋 아래에 있으므로 DDR 컨트롤러도 리셋된다. 실제 출력:

```text
2000.000us ISO_ON DDR
2000.000us RST_ASSERT RST_DDR
2000.000us ISO_ON CORE
2000.000us CLK_OFF CLK_CORE
2000.000us RET_SAVE CORE
2000.000us RST_ASSERT RST_CORE
2000.000us RAIL_OFF VDD_CORE
-- 3000 µs 에 seq_up --
3000.000us RAIL_EN VDD_CORE
3250.000us RAIL_PG VDD_CORE
3250.000us RET_RESTORE CORE      ← 클럭을 켜기 전에 복원
3250.000us CLK_ON CLK_CORE
3250.005us RST_DEASSERT RST_CORE ← 이번엔 5 ns (RST_AON 은 계속 켜져 있다)
3250.005us ISO_OFF CORE
3250.005us RST_DEASSERT RST_DDR  ← CLK_DDR 은 sleep 동안 계속 돌았다
3250.005us ISO_OFF DDR
3260.005us INIT_START INIT_DDRC
3410.005us INIT_DONE INIT_DDRC
```

wake 지연 410.005 µs (= 레일 250 + 5 ns + 10 µs + DDR init 150; 연습 4 가 그래프만으로 같은 값을 낸다). 전체 로그 46 이벤트에 체커 위반 0. 범위 밖인 CLK_DDR, INIT_UART 는 그대로 ON (CHECK). 실제 칩에서 DDR 은 sleep 동안 self-refresh 로 내용을 지키고, wake 때 training 을 다시 하지 않도록 저장된 결과를 쓴다 — 이 모델의 "INIT_DDRC 150 µs 재실행" 은 그 비용을 과장한 단순화다.

### Part B — 체커: 시퀀서를 믿지 않는다

체커는 시퀀서와 **독립** 이어야 한다. 시퀀서 코드를 재사용하면 시퀀서의 버그를 그대로 물려받는다. 체커가 공유하는 것은 그래프(스펙)뿐이고, 로그만 보고 상태를 다시 세운다.

| 위반 | 언제 | 예 (손으로 쓴 mini 로그의 실제 출력) |
|---|---|---|
| ORDER | 선행 노드(RST/INIT 등) 미완료 | `#7 t=178.000us ORDER edge RST_A->INIT_A: RST_A not done` |
| MIN_DELAY | 선행 완료, 지연 부족 | `#2 t=105.000us MIN_DELAY edge VDD_A->VDD_P: need 10.000us after VDD_A@100.000us, got 5.000us` |
| CLK_BEFORE_RAIL | RAIL → PLL/CLK 간선, 레일 PG 전 | `#3 t=145.000us CLK_BEFORE_RAIL edge VDD_P->PLL0: VDD_P not done` |
| PLL_NOT_LOCKED | PLL → CLK 간선, lock 전 | `#5 t=170.000us PLL_NOT_LOCKED edge PLL0->CLK_A: PLL0 not done` |
| RST_BEFORE_CLK | CLK → RST 간선, 클럭 없음 또는 K 사이클 전 | `#7 t=177.000us RST_BEFORE_CLK edge CLK_A->RST_A: need 4.000us after CLK_A@175.000us, got 2.000us` |
| OFF_ORDER | 하위 자원(또는 도메인 리셋 해제 상태)이 켜진 채로 끔 | `#16 t=300.000us OFF_ORDER VDD_A off while VDD_P still on` |
| OFF_BEFORE_ISO | isolation 도메인 레일을 clamp 없이 끔 | `#16 t=300.000us OFF_BEFORE_ISO VDD_A off, domain A not isolated` |
| OFF_BEFORE_SAVE | retention 도메인에 저장 안 된 상태(CLK_ON 이후 save 없음)가 있는데 레일 끔 | `#16 t=300.000us OFF_BEFORE_SAVE VDD_A off, domain A state not saved` |
| ISO_EARLY | 도메인 리셋 해제(·레일 good) 전에 ISO_OFF | `#7 t=176.000us ISO_EARLY ISO_OFF A while RST_A not up` |
| UNHANDLED_LOSS | PG_LOST/UNLOCK 후 `T_REACT` 안에 복귀도 하위 정지도 없음 | `#11 t=255.000us UNHANDLED_LOSS PLL0 lost @250.000us, CLK_A still on at deadline` |
| BAD_EVENT | 상태와 맞지 않음(EN 없이 PG, PG 두 번, 시간 역행, 종류 불일치) | `#2 t=100.000us BAD_EVENT ...` |

mini 그래프 정상 로그(18 줄, `MINI_GOOD`)는 위반 0. 13 개의 위반 로그는 각각 **정확히 1 개**, 종류 · 노드 · 간선이 기대와 같다(CHECK). 위반이 아니어야 하는 두 로그도 확인한다: `250 PLL_UNLOCK` 후 1 µs 안에 ISO_ON · CLK_OFF · SAVE · RST · PLL_OFF 를 한 로그(처리됨), 1 µs 짜리 PG 글리치(시한 안에 복귀). 반대로 30 µs 동안 PG 가 빠진 로그는 `UNHANDLED_LOSS VDD_A lost @250.000us, VDD_P still on at deadline` 로 잡힌다. 위반 3 개를 섞은 로그:

```text
  multi            -> 3 violation(s)
    #2   t=105.000us    MIN_DELAY        edge VDD_A->VDD_P: need 10.000us after VDD_A@100.000us, got 5.000us
    #7   t=177.000us    RST_BEFORE_CLK   edge CLK_A->RST_A: need 4.000us after CLK_A@175.000us, got 2.000us
    #16  t=300.000us    OFF_BEFORE_SAVE  VDD_A off, domain A state not saved
```

**"정확히 한 번" 의 설계.** ① 위반 단위 = (이벤트, 간선). 같은 원인이 다음 이벤트에서 또 보고되지 않게, 위반 이벤트 뒤에도 상태는 **로그대로** 갱신한다(예: 이른 RST_DEASSERT 뒤에도 RST_A 는 해제된 것으로 본다 → INIT_START 는 정상 판정). ② 연쇄 위반을 막는 대가로 "첫 위반이 만든 2 차 피해" 는 체커가 보지 않는다 — 그건 근본 원인 하나로 충분하다는 선택이다. ③ 손실은 시한에 한 번 평가하고 지운다.

**인접 교환 변이 (시퀀서 로그 47 쌍, 실제 출력).**

```text
  undetected swap #36: RST_ASSERT RST_CORE <-> RST_ASSERT RST_AON
  47 adjacent swaps: detected 23, undetected 24 (same timestamp 18), undetected with a direct edge 0 (+1 RST_ASSERT pair)
```

놓친 24 개는 세 부류다:

- **진짜로 다른 합법 순서** (대부분): 같은 시각의 독립 이벤트(`ISO_ON DDR` ↔ `ISO_ON IO`), 또는 한쪽을 뒤로 미루는 교환(`RAIL_EN VDD_CORE` 를 350 → 400 µs 로). 스펙은 **부분순서** 이므로 이것들은 위반이 아니다.
- **리셋 assert 순서** (`RST_ASSERT RST_CORE ↔ RST_AON`): 간선 RST_AON → RST_CORE 가 있지만 이 모델은 assert 순서를 규칙으로 두지 않았다(비동기 assert, 같은 시각). 설계 선택이고, 필요하면 off 쪽 간선을 따로 정의해야 한다.
- **plant 시간 위반**: `INIT_DONE INIT_UART@821 ↔ INIT_DONE INIT_DDRC@890` 은 DDR init 이 150 µs 가 아니라 81 µs 만에 끝났다는 로그인데 통과한다. 체커는 **순서 규칙** 만 보고, 완료가 물리적으로 가능한 시간인지(ramp 가 너무 빠른 PG = 측정/배선 오류 가능성)는 보지 않는다. 그건 SIV-A06 의 ramp 시간 판정의 일이다.

**스펙 버그는 같은 스펙의 체커가 못 본다** (실제 출력):

```text
  buggy spec: power-up 821.040us, checker(buggy spec) 0 violations, checker(true spec) 1:
    #7   t=410.000us    CLK_BEFORE_RAIL  edge VDD_DDR->PLL_DDR: VDD_DDR not done
```

`VDD_DDR → PLL_DDR` 간선을 빠뜨린 스펙으로 시퀀서를 만들면 PLL_DDR 이 DDR 레일보다 240 µs 먼저 켜지고, 부팅이 68.97 µs **빨라진다** — 최적화처럼 보인다. 같은 스펙에서 생성한 체커는 0 위반. 올바른 스펙의 체커만 잡는다. 검증의 기본 원칙 — **참조(reference)는 독립적으로 만들어야 한다** — 이 시퀀싱에서도 그대로다. 실제 흐름에서 이 독립성은 데이터시트의 시퀀싱 표, IP 별 통합 가이드, UPF power intent 를 서로 대조하는 리뷰와, 연습 3 같은 구조 lint 에서 온다(이 버그는 lint 도 못 잡는다 — 연습 3).

### Part C — 결함 주입: 안전 상태와 원인

실제 출력:

```text
  pg_never   state=FAULT cause=PG_TIMEOUT   node=VDD_DDR   t=1450.000us   events=34  ignored=0 filtered=0 violations=0
  lock_never state=FAULT cause=LOCK_TIMEOUT node=PLL_DDR   t=850.000us    events=37  ignored=0 filtered=0 violations=0
  init_never state=FAULT cause=INIT_TIMEOUT node=INIT_DDRC t=1240.010us   events=45  ignored=0 filtered=0 violations=0
  unlock     state=FAULT cause=PLL_UNLOCK   node=PLL_SYS   t=2000.000us   events=40  ignored=0 filtered=0 violations=0
  brownout   state=FAULT cause=BROWNOUT     node=VDD_CORE  t=2002.000us   events=46  ignored=1 filtered=0 violations=0
  glitch     state=ON    cause=NONE         node=-         t=-            events=31  ignored=0 filtered=1 violations=0
  late_pg    state=FAULT cause=PG_TIMEOUT   node=VDD_IO    t=1400.000us   events=37  ignored=1 filtered=0 violations=0
  bo_up      state=FAULT cause=BROWNOUT     node=VDD_CORE  t=612.000us    events=20  ignored=3 filtered=0 violations=0
  pllrail    state=ON    cause=NONE         node=-         t=-            events=31  ignored=0 filtered=1 violations=0
```

| 시나리오 | 주입 | 시퀀서 반응 | 안전 상태 |
|---|---|---|---|
| pg_never | VDD_DDR PG 안 옴 | 450 + 1000 µs 타임아웃 | AON 레일만 ON, 나머지 OFF |
| lock_never | PLL_DDR lock 안 옴 | 650 + 200 µs | 같음 |
| init_never | INIT_DDRC 완료 안 옴 | 740.010 + 500 µs | 같음 (실제 설계는 그 주변장치만 끄고 재시도할 수 있다) |
| unlock | 2000 µs PLL_SYS unlock | **즉시**, PLL_SYS 하위만 | 레일 전부 ON, CORE/IO/DDR 리셋+isolation, CLK_DDR·PLL_DDR 은 계속 |
| brownout | 2000 µs VDD_CORE PG 50 µs 빠짐 | 2 µs deglitch 후 2002 µs | AON 만, **RET_SAVE 없음** (레일이 이미 빠짐) |
| glitch | VDD_IO PG 1 µs 빠짐 | deglitch 안에 복귀 → 무시 | ON 유지, 로그에 LOST/PG 가 남음 |
| late_pg | VDD_IO PG 1500 µs (타임아웃 1000) | 1400 µs fault, 1900 µs 의 PG 무시 | FAULT 유지, `seq_up` 거부 |
| bo_up | power-up 도중 610 µs VDD_CORE | 612 µs | ramp 중이던 VDD_IO 까지 OFF |
| pllrail | VDD_PLL 405 µs 1 µs 글리치 | 무시, 단 **복귀 시각부터** 10 µs 재측정 | PLL_SYS 시작 416 µs, 전체 시간 890.010 그대로 |

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">PG 글리치 vs brown-out (deglitch 2 µs, 체커 시한 5 µs)</text>
  <g fill="currentColor"><text x="20" y="44">(a) VDD_IO 1 µs 글리치</text><text x="400" y="44">(b) VDD_CORE 50 µs brown-out</text></g>
  <g stroke="currentColor" stroke-dasharray="2 4">
    <line x1="80" y1="52" x2="80" y2="270"/><line x1="200" y1="52" x2="200" y2="270"/><line x1="380" y1="52" x2="380" y2="270"/>
    <line x1="460" y1="52" x2="460" y2="270"/><line x1="560" y1="52" x2="560" y2="270"/><line x1="710" y1="52" x2="710" y2="270"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="80" y="288">2000</text><text x="140" y="288">2001</text><text x="200" y="288">2002</text><text x="380" y="288">2005</text>
    <text x="460" y="288">2000</text><text x="560" y="288">2002</text><text x="710" y="288">2005 µs</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="80">PG</text><text x="20" y="130">deglitch</text><text x="20" y="180">CLK</text><text x="20" y="230">ISO</text>
    <text x="400" y="80">PG</text><text x="400" y="130">deglitch</text><text x="400" y="180">CLK</text><text x="400" y="230">ISO / RAIL</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="2">
    <path d="M60 70 L80 70 L80 90 L140 90 L140 70 L380 70"/>
    <path d="M460 70 L460 90 L740 90"/>
    <path d="M60 195 L380 195" /><path d="M60 245 L380 245"/>
    <path d="M440 170 L560 170 L560 195 L740 195"/>
    <path d="M440 245 L560 245 L560 225 L740 225"/>
  </g>
  <g stroke="currentColor">
    <rect x="80" y="118" width="120" height="16" class="f-ok"/>
    <rect x="460" y="118" width="100" height="16" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="210" y="130">2002: 복귀 → 무시</text><text x="150" y="65">2001 PG 복귀</text>
    <text x="90" y="190">CLK 그대로 (클럭 · 리셋 · ISO 변화 없음)</text>
    <text x="566" y="130">2002: low → FAULT</text>
    <text x="566" y="165">ISO_ON → RST → CLK_OFF</text><text x="566" y="215">→ PLL_OFF → RAIL_OFF</text>
    <text x="566" y="262">체커 시한 2005 전 정지</text>
  </g>
</svg>
```

설계 포인트:

1. **Deglitch 와 반응 시한은 짝이다.** 2 µs 미만의 PG 하락은 무시(레귤레이터 부하 과도, PG 비교기 잡음), 그 이상은 brown-out. 체커의 `T_REACT` (5 µs) > deglitch(2 µs) + shutdown 지연이어야 정상 시퀀서가 통과한다. 반대로 deglitch 를 길게 잡을수록 **진짜 brown-out 동안 회로가 낮은 전압에서 돈 시간** 이 길어진다 — 그동안의 레지스터/SRAM 값은 믿을 수 없다. 이 트레이드오프는 PMIC/PG 비교기 사양과 함께 정한다(실제 BOR 회로는 아날로그 히스테리시스 + 필터 — ARCH-B14 의 BOR 플래그가 그 결과).
2. **Brown-out 이면 save 하지 않는다.** 레일이 이미 빠진 도메인의 플롭 값은 쓰레기일 수 있다. 시퀀서 조건 `!lost[rail]` 과 체커 조건 `good[u]` 가 같은 규칙을 양쪽에서 말한다. 그래서 다음 부팅은 retention 을 믿지 말고 콜드 부트를 해야 하고, 그 판단 근거가 **원인 레지스터** 다.
3. **원인 레지스터는 sticky, AON 에 산다.** `fault` 는 처음 것만 남는다 — brown-out 이 PLL unlock 을 일으키는 경우처럼 연쇄 고장에서 근본 원인을 지키기 위해서다. 부팅 코드가 이것을 읽고, 로그를 남기고, 지우고, 결정한다(재시도 · 안전 모드 · 콜드 부트) — ARCH-B14 의 "read, log, clear, decide" 그대로다. 원인 노드가 AON 자신이면(VDD_AON brown-out) 원인 레지스터도 사라지므로 다음 부팅은 POR 로 보인다.
4. **Unlock 은 부분 정지.** 레일을 내릴 이유가 없다. 그 PLL 의 하위만 리셋하면 DDR 쪽(PLL_DDR)은 계속 돈다. 실제 칩은 클럭 모니터가 unlock 을 감지하면 bypass(기준 클럭)로 전환해 CPU 가 원인을 기록하게 하기도 한다 — 이 모델은 단순히 멈춘다.
5. **늦은 사건은 세대 번호(gen)로 버린다.** 노드를 켜고 끌 때마다 `gen` 이 증가하고, 타이머·완료 이벤트는 발행 당시의 gen 을 들고 있다. 1900 µs 에 도착한 PG 는 이미 꺼진(gen 이 바뀐) 레일의 것이므로 `ignored++` 하고 로그에 쓰지 않는다. 이걸 빠뜨리면 꺼진 레일이 "PG" 로 되살아나 FAULT 상태에서 시퀀스가 재개된다 — 실제 PMU 펌웨어 버그 패턴이다. `bo_up` 의 ignored=3 은 VDD_DDR(650) · VDD_IO(800) PG 와 VDD_CORE 복귀(710)를 버린 것이다.
6. `bo_up` 에서 RST_AON 은 아직 해제 전(622.071 µs 예정)이었고 fault 이후 시퀀서가 멈추므로 해제되지 않는다. AON 은 **레일만** 유지된다 — 원인 레지스터가 POR 로만 지워지는 별도 리셋 도메인에 있어야 하는 이유다(이 모델은 "AON 레일 ON" 까지만 확인).

**틀린 시퀀서를 체커가 잡는가** (실제 출력):

```text
  no_iso       -> 3
    #40  t=2000.000us   OFF_BEFORE_ISO   VDD_DDR off, domain DDR not isolated
    #41  t=2000.000us   OFF_BEFORE_ISO   VDD_IO off, domain IO not isolated
    #42  t=2000.000us   OFF_BEFORE_ISO   VDD_CORE off, domain CORE not isolated
  no_save      -> 1
    #34  t=2000.000us   OFF_BEFORE_SAVE  VDD_CORE off, domain CORE state not saved
  k_sync=0     -> 4 (power-up 890.000us)
    #10  t=500.000us    RST_BEFORE_CLK   edge CLK_AON->RST_AON: need 122.071us after CLK_AON@500.000us, got 0.000us
    #13  t=600.000us    RST_BEFORE_CLK   edge CLK_CORE->RST_CORE: need 0.005us after CLK_CORE@600.000us, got 0.000us
    #19  t=730.000us    RST_BEFORE_CLK   edge CLK_DDR->RST_DDR: need 0.010us after CLK_DDR@730.000us, got 0.000us
    #24  t=800.000us    RST_BEFORE_CLK   edge CLK_IO->RST_IO: need 0.040us after CLK_IO@800.000us, got 0.000us
  ignore_unlock-> 1
    #29  t=2005.000us   UNHANDLED_LOSS   PLL_SYS lost @2000.000us, CLK_CORE still on at deadline
```

`k_sync=0` 이 흥미롭다: 부팅 시간은 890.010 → 890.000 µs 로 10 ns 밖에 안 줄었는데(임계 경로의 동기화 지연은 10 ns 뿐) 위반은 4 개다. "리셋 동기화 사이클을 빼서 부팅을 빠르게" 는 거의 이득 없이 4 개 도메인 모두를 메타안정 해제 위험에 노출한다(ARCH-A07 의 동기화기, RTLDV-B06).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so          # 69/69 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 69/69, 경고 없음
```

(Apple clang 21.0.0, Apple M6. 타이밍 측정은 없다 — 모든 시간은 시뮬레이션 시간이다.)

7 개 TODO 의 핵심:

- `topo_order`: 진입차수 배열 + "아직 안 쓴 것 중 진입차수 0 인 가장 작은 번호" 를 n 번. O(n·(n+e)) 이지만 노드 수십 개라 충분.
- `ready_time`: 선행 하나라도 미완료/lost → -1, 아니면 `max(done_at + min_ns)`.
- `seq_shutdown`: 위상 역순 + `q[d]` 로 도메인당 한 번 quiesce (ISO_ON, 조건부 CLK_OFF+RET_SAVE) + quiesce 에서 이미 끈 클럭은 건너뜀.
- `chk_action`: (u.kind, v.kind) → 위반 종류 표, 미완료와 지연 부족 중 하나만.
- `chk_off`: 하위 자원 → OFF_ORDER, 도메인 rail 이면 리셋 · iso · save 세 조건.
- `chk_loss_due`: 시한 `< t` 인 손실만, 하위 자원(없으면 도메인 리셋) 중 첫 번째 활성 → 한 번 보고, 순서 유지 삭제.
- `seq_fault`: 기록 → sticky → mask(unlock = closure, 그 외 = AON 제외 전부, AON 원인 = 전부) → shutdown → FAULT.

## 7. 자주 하는 실수

**① 순서를 코드에 박는다.** `rail_on(CORE); wait(250); pll_on(SYS); ...` 같은 직선 코드는 새 레일 하나만 추가해도 모든 곳을 고쳐야 하고, 체커가 같은 스펙을 공유할 수 없다. 그래프(테이블)로 두면 시퀀서 · 체커 · 문서 · 부팅 시간 분석(CPM)이 한 데이터에서 나온다. 실제 PMIC 의 OTP 시퀀스 슬롯, PMU 펌웨어의 시퀀스 테이블이 이 형태다.

**② 리셋 동기화 지연을 "빠른 클럭" 기준으로 잡는다.**

```c
/* 틀림: 모든 도메인에 고정 100 ns */
g_edge(g, "CLK_AON", "RST_AON", 100);
/* 고침: 그 리셋을 동기화하는 클럭의 K 사이클 — 32.768 kHz 면 122.071 µs */
g_edge(g, "CLK_AON", "RST_AON", SYNC);
```

**③ power-down 을 단순 역순으로 한다.** 역순은 리셋을 클럭보다 먼저 건다 → retention 도메인은 지워진 값을 저장한다. retention 도메인은 CLK_OFF → SAVE → RST_ASSERT 로 예외를 둔다(이 레슨의 quiesce).

**④ isolation 을 레일을 끄기 직전에 켠다.** 그 전에 이미 리셋 assert · 클럭 정지로 출력이 바뀌었고, 살아 있는 쪽은 그 순간을 봤다. 도메인의 **첫** 동작 전에 clamp 한다. 반대로 ISO_OFF 는 리셋 해제(와 restore) **뒤** — SIV-A11 이 포트 샘플로 이걸 시험한다.

**⑤ 타임아웃 뒤 늦게 온 완료를 그대로 받는다.**

```c
/* 틀림: 완료 이벤트면 무조건 ON */
case Q_DONE: sq->st[v] = NS_ON; break;
/* 고침: 발행 당시 세대(gen)이고 아직 기다리는 중일 때만 */
case Q_DONE: if (q->gen != sq->gen[v] || sq->st[v] != NS_BUSY) { sq->ignored++; break; } ...
```

**⑥ 글리치 복귀 후 min 지연을 처음 PG 기준으로 잰다.** VDD_PLL 이 405 µs 에 1 µs 빠졌다 돌아오면 "PG 후 10 µs" 는 406 µs 부터다(`pllrail` 시나리오: PLL_SYS 시작 410 → 416 µs). 처음 PG(400) 기준이면 PLL 을 정착 안 된 레일에서 켠다.

**⑦ 체커를 시퀀서 코드로 만든다.** 시퀀서의 `ready_time` 을 불러 "준비됐나" 를 판단하면 시퀀서 버그가 체커에도 있다. 체커는 로그만으로 상태를 다시 세운다. 그래도 스펙(그래프) 버그는 공유한다(Part B) — 스펙은 별도 리뷰와 lint 로.

## 8. 면접 Q&A

<details>
<summary>Q1. 리셋을 PLL lock 전에 풀면 무슨 일이 나나? (꼬리: 리셋을 클럭 없이 풀면? 동기화기는 몇 단?)</summary>

lock 전 PLL 출력은 주파수가 목표와 다르거나(overshoot 로 목표보다 빠를 수도 있다) 듀티가 깨지고 글리치가 있을 수 있다. 그 클럭으로 리셋에서 나온 로직은 setup 위반(ARCH-B13 의 slack 이 음수)으로 상태가 틀어지고, 리셋 해제 자체가 그 클럭 도메인에 동기화되지 않는다 — 플롭마다 다른 사이클에 리셋에서 나와 FSM 이 불법 상태에 들어간다. 그래서 순서는 `PLL lock → 클럭 게이트 enable → K 사이클 → 리셋 해제` 이고, 체커의 `PLL_NOT_LOCKED` 와 `RST_BEFORE_CLK` 가 그 두 간선이다.

꼬리: 클럭이 없으면 비동기 assert 는 되지만 **동기 해제(sync deassert)** 는 일어나지 않는다 — 동기화기 플롭이 클럭 에지를 받아야 리셋 출력이 풀린다. 그러니 "클럭 없이 풀면" 실제로는 안 풀리거나(동기화기 있음) 플롭마다 따로 풀린다(없음). 단 수는 보통 2–3 FF 이고 MTBF 계산(ARCH-A07)으로 정하며, 이 레슨의 K=4 는 "동기화기 2 + 여유" 의 가정값이다. 핵심은 사이클 **수** 보다 **어느 클럭의** 사이클인가 — 32 kHz 의 4 사이클은 122 µs 다.
</details>

<details>
<summary>Q2. isolation 은 언제 켜고 언제 푸나? clamp 값은? (꼬리: isolation 셀은 어느 전원에 있어야 하나?)</summary>

켜기: 그 도메인에서 무엇이든 바뀌기 **전** (리셋 assert · 클럭 정지 · 전원 차단 중 첫 동작 전). 풀기: 도메인이 전원 good → (restore) → 클럭 → 리셋 해제까지 끝나 출력이 정의된 **뒤**. 체커의 `OFF_BEFORE_ISO` 와 `ISO_EARLY` 가 이 두 끝이다. clamp 값은 신호의 "비활성" 값 — active-high 요청/인터럽트는 0, active-low 리셋/enable 은 1 (UPF 의 `-clamp_value`; SIV-A11 Q3).

꼬리: isolation 셀은 **켜져 있는 쪽**(보통 AON 또는 수신 도메인) 전원으로 동작해야 한다. 꺼지는 도메인 전원에 있으면 정작 필요할 때 셀이 죽는다. 그래서 전원 인가 직후의 기본값도 clamp 이고(이 모델의 초기 `iso[d] = true`), ISO 제어 신호는 AON 의 PMU 가 낸다.
</details>

<details>
<summary>Q3. 그 순서는 어디에 정의되고, 누가 지키나? (꼬리: 순서가 틀렸다는 걸 어떻게 아나?)</summary>

정의: SoC 데이터시트의 power sequencing 표(레일 순서 · 최소 지연 · ramp 요구), IP 별 통합 가이드(PLL lock 시간, 리셋 사이클 요구), UPF power intent(isolation/retention/switch 와 그 제어 신호). 실행: 레일은 PMIC(OTP 시퀀스 슬롯 또는 PG 체인), 칩 내부 도메인은 PMU — 하드 FSM 이거나 AON 마이크로컨트롤러 펌웨어(시퀀스 테이블). 이 레슨의 그래프가 그 테이블이다.

꼬리: 설계 단계에서 ① 스펙 lint(사이클, 빠진 간선 — 연습 2·3) ② 시퀀서 시뮬레이션 + 독립 체커(이 레슨) ③ UPF-aware RTL 시뮬레이션(RTLDV-A08). 실리콘에서 ④ 보드 파형(SIV-A06) ⑤ 도메인 레지스터 시험(SIV-A11). 각 단계가 다른 버그를 잡는다 — Part B 의 "같은 스펙 체커는 스펙 버그를 못 본다" 가 왜 여러 단계가 필요한지의 답이다.
</details>

<details>
<summary>Q4. Brown-out 이 나면 시퀀서는 무엇을 하나? (꼬리: deglitch 시간은 어떻게 정하나? retention 은 믿어도 되나?)</summary>

PG 하락을 deglitch(여기 2 µs) 동안 지켜보고, 복귀하지 않으면 FAULT: 원인(BROWNOUT, 레일)을 AON 의 sticky 레지스터에 남기고, AON 을 뺀 전부를 순서대로 내린다 — isolation → 리셋 → 클럭 → PLL → 레일. 레일이 이미 빠진 도메인은 save 하지 않는다. 다음 부팅 코드는 원인을 읽고 콜드 부트·로그·재시도 정책을 정한다(ARCH-B14).

꼬리: deglitch 는 PG 비교기 잡음/부하 과도보다 길고, "낮은 전압에서 로직이 오동작하기 시작하는 시간" 보다 짧아야 한다. 이 레슨의 체커는 `T_REACT > deglitch + shutdown` 로 그 상한을 강제한다. retention: 레일 하락이 retention 전원까지 건드렸는지 모르면 **믿지 않는다** — 그래서 원인 레지스터에 brown-out 이 있으면 retention 무효로 처리하는 것이 안전한 기본값이다.
</details>

<details>
<summary>Q5. 병렬 시퀀서와 직렬 시퀀서의 차이는? 부팅 시간을 줄이려면? (꼬리: 레일을 다 동시에 켜면 안 되나?)</summary>

같은 그래프에서 병렬 = 임계 경로 890.010 µs, 직렬 = 1551.040 µs (1.74 배). 병렬은 준비된 노드를 다 켜고, 직렬은 앞 노드가 끝나야 다음을 켠다. 부팅 시간을 줄이려면 **임계 경로 위의 노드만** 의미가 있다: CPM 의 slack 0 노드(AON → DDR 레일 → PLL_DDR → CLK_DDR → RST_DDR → INIT_DDRC). VDD_IO 를 50 µs 빠르게 해도 0 이고(여유 68.970 µs), VDD_DDR 를 50 µs 빠르게 하면 50 µs 준다 (연습 1).

꼬리: 동시에 켜면 ① 돌입 전류(inrush)가 합쳐져 입력 레일이 처지고(그 자체가 brown-out) ② 레일 간 순서 요구(예: 코어 전압이 IO 보다 먼저 / 반대 — 칩마다 다름, ESD 다이오드 · 레벨 시프터 바이어스 때문)를 깬다. 이 레슨의 AON → 각 레일 50/100/150 µs 시차가 그런 분산의 대표값이다. 이득과 위험은 PMIC 전류 한도로 정한다.
</details>

<details>
<summary>Q6. 체커가 놓치는 것은? (꼬리: 그럼 무엇으로 보완하나?)</summary>

측정한 것: ① 스펙(그래프) 자체의 누락 — 같은 스펙의 체커는 0 위반(Part B) ② plant 시간 — DDR init 이 81 µs 만에 끝난 로그가 통과(인접 교환 #27) ③ 규칙으로 정하지 않은 것 — 리셋 assert 순서 ④ 로그에 없는 것 — 클럭 글리치, 레일 처짐, 같은 타임스탬프 안의 실제 지연. 체커는 "로그가 그래프의 부분순서와 off 규칙을 만족하는가" 만 답한다.

꼬리: ① 스펙은 다른 출처(데이터시트 표 · IP 가이드 · UPF)와의 대조 + 구조 lint(연습 3 — 그것도 VDD_DDR → PLL_DDR 누락은 못 본다) ② plant 시간은 노드별 최소/최대 지속(ramp 시간 범위) 규칙을 추가하거나 SIV-A06 의 파형 판정 ③ 로그 밖은 파형 측정과 칩 내 모니터(클럭 모니터, PG 상태 sticky). 그리고 **체커를 검사**: 일부러 틀린 시퀀서 4 개가 기대 위반을 내는지(Part C) — ARCH-A04 의 돌연변이와 같은 방법이다.
</details>

<details>
<summary>Q7. PLL 이 동작 중 lock 을 잃으면? (꼬리: 왜 레일까지 내리지 않나? 클럭을 먼저 끄나 리셋을 먼저 거나?)</summary>

lock detect 가 내려가면 그 PLL 의 출력 주파수/위상을 믿을 수 없다. 이 시퀀서는 즉시(deglitch 없이) 그 PLL 의 closure — CLK_CORE · CLK_IO 와 그 위의 리셋 · init — 를 isolation → 리셋 assert → 클럭 게이트 off 로 정지하고 PLL 을 끈다. 레일과 다른 PLL(PLL_DDR) 쪽은 계속 동작. 원인 PLL_UNLOCK 기록.

꼬리: 전원은 멀쩡하므로 내릴 이유가 없고, 내리면 retention/DDR 내용까지 잃는다 — 고장 범위(blast radius)를 최소로. 순서는 같은 타임스탬프 안이라 이 모델에선 구분이 안 되지만, 실제로는 **리셋을 먼저(비동기 assert, 클럭 불필요)** 걸어 상태 진행을 멈추고 클럭을 게이트하는 쪽이 글리치 클럭이 상태를 망가뜨릴 창을 줄인다. lock 이 흔들리는 동안 클럭 게이트(ICG) 자체가 글리치를 낼 수 있어서, 많은 설계가 클럭 모니터 + 글리치 없는 MUX 로 기준 클럭(bypass)에 붙인다(hedge: 칩마다 다름).
</details>

<details>
<summary>Q8. 그래프에 사이클이 생기는 실제 예는? (꼬리: 런타임에 감지하면 안 되나?)</summary>

예: "DDR training 이 끝나야 DDR 레일을 확정한다"(연습 2 의 `INIT_DDRC → VDD_DDR`), "동기 리셋 플롭은 리셋 중 클럭이 필요한데 클럭 enable 은 리셋 해제 뒤로 정의" (SIV-A11 연습 2 와 같은 구조), "PMU 펌웨어가 SRAM 에서 도는데 SRAM 레일을 PMU 가 켠다". 해결은 노드를 쪼개는 것 — 레일을 기본 전압으로 먼저 켜고 training 후 전압 조정 노드를 따로 두기, 리셋 중 유한 클럭 펄스 노드 추가, PMU 를 ROM/AON SRAM 으로.

꼬리: 런타임에 감지하면 이미 늦다 — 사이클이 있는 시퀀서는 무엇도 시작하지 않거나(모든 노드가 서로를 기다림) 타임아웃으로 FAULT 만 반복하고, 칩은 벽돌처럼 보인다. 그래서 테이블 로드(또는 빌드) 시점에 위상 정렬로 거부하고(`seq_init` = -1), 오류 메시지에 사이클 경로를 찍는다(연습 2).
</details>

## 9. 추가 연습

**연습 1 — 부팅 시간 예산 (CPM).** 그래프에서 각 노드의 가장 이른 시작(ES), 가장 늦은 완료(LF), 여유(slack = LS − ES)를 구하라. 임계 경로와 총 시간이 시뮬레이터와 같은지, VDD_IO 와 VDD_DDR 의 ramp 를 각각 50 µs 줄이면 총 시간이 얼마가 되는지 보여라.

<details>
<summary>답</summary>

전방: `ES[v] = max(EF[u] + lag)`, `EF = ES + dur`. 후방(위상 역순): `LF[v] = min(LS[w] − lag)`, `LS = LF − dur`. 간선에 지연(lag)이 있는 CPM 이다. 결과: T = 890.010 µs (시뮬레이터와 같음), slack 0 = VDD_AON VDD_DDR PLL_DDR CLK_DDR RST_DDR INIT_DDRC. VDD_IO 여유 68.970 µs, VDD_CORE 130.005, RST_CORE 107.939, PLL_SYS 270.005 µs. DDR ramp −50 µs → 840.010 µs, IO ramp −50 µs → 890.010 µs (그대로). 부팅 최적화는 slack 0 노드에서만.

</details>

**연습 2 — 사이클을 경로로 보고.** 위상 정렬이 실패하면 "-1" 대신 사이클을 `A -> B -> ... -> A` 로 찍어라.

<details>
<summary>답</summary>

Kahn 을 끝까지 돌린 뒤 남은 노드는 모두 남은 선행자를 하나 이상 가진다(그래서 남았다). 아무 남은 노드에서 "남은 선행자" 를 따라 거꾸로 걷다가 이미 본 노드를 다시 만나면 그 구간이 사이클이다 — 뒤집어서 정방향으로 출력. `INIT_DDRC → VDD_DDR` 을 넣은 실제 출력: `PLL_DDR -> CLK_DDR -> RST_DDR -> INIT_DDRC -> VDD_DDR -> PLL_DDR` (연속 쌍이 모두 간선인지 CHECK).

</details>

**연습 3 — 스펙 lint.** 체커가 못 보는 스펙 누락의 일부를 구조 규칙으로 잡아라: L1 클럭에 자기 도메인 레일 간선이 없다 · L2 PLL 에 공급 레일 간선이 없다 · L3 리셋에 자기 도메인 클럭 간선이 없다 · L4 클럭→리셋 지연이 K 사이클 미만 · L5 isolation 도메인인데 리셋 노드가 없다(ISO_OFF 조건을 못 정함).

<details>
<summary>답</summary>

노드마다 들어오는 간선을 훑어 조건을 본다. EDGE-1 은 0 건. `VDD_CORE → CLK_CORE` 를 지우면 `L1 CLK_CORE` 1 건, K=0 그래프는 L4 4 건. 그러나 Part B 의 버그(`VDD_DDR → PLL_DDR` 누락)는 **0 건** — PLL_DDR 은 VDD_PLL 간선이 있어서 L2 를 만족한다. "이 PLL 은 어느 레일들에 걸쳐 있나" 는 구조에서 나오지 않는 **사실** 이라 lint 로는 못 잡는다. 그 정보의 출처(IP 통합 가이드의 공급 핀 목록)를 그래프 생성기의 입력으로 삼아야 한다.

</details>

**연습 4 — wake 지연.** sleep 했던 도메인 레일 하나를 다시 켤 때, 나머지는 이미 켜져 있다고 보고 closure 만의 임계 경로를 구하라. CORE · DDR · IO 레일 각각.

<details>
<summary>답</summary>

closure 밖의 선행은 "이미 오래전에 완료" 이므로 간선을 무시하고, closure 안에서만 전방 패스를 돈다(wake 명령 = 0). CORE = 250 + 0.005 + 10 + 150 = **410.005 µs** (Part A 시뮬레이터와 같음), DDR = 200 + 80 + 0.010 + 10 + 150 = 440.010 µs, IO = 400 + 0.040 + 1 + 20 = 421.040 µs. AON 리셋의 122 µs 는 sleep 동안 AON 이 켜져 있으므로 wake 에는 안 들어간다 — 깊은 sleep(AON 저속 클럭까지 끔)과 얕은 sleep 의 wake 지연이 다른 이유다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr      # 29/29 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o san_pr && ./san_pr   # 29/29, 경고 없음
```

실제 출력:

```text
ex1: T=890010 ns, critical: VDD_AON VDD_DDR PLL_DDR CLK_DDR RST_DDR INIT_DDRC
     slack: VDD_IO=68970 VDD_CORE=130005 RST_CORE=107939 PLL_SYS=270005 ns
ex2: cycle (5): PLL_DDR -> CLK_DDR -> RST_DDR -> INIT_DDRC -> VDD_DDR -> PLL_DDR
ex3: lint(soc)=0, lint(no VDD_CORE->CLK_CORE)=1, lint(k_sync=0)=4, lint(no VDD_DDR->PLL_DDR)=0
ex4: wake latency CORE=410005 DDR=440010 IO=421040 ns
29/29 passed
```

## 10. 핵심 정리

- 순서는 **데이터**: 노드(RAIL/PLL/CLK/RST/INIT) + 간선(최소 지연) + 타임아웃 + 도메인(iso/ret). 위상 정렬로 순서, 사이클 = 로드 시점 설정 오류.
- 올림: 레일 PG → PLL lock → 클럭 enable(소스 lock + 목적지 레일 good) → **그 클럭의** K 사이클 → 리셋 해제 → ISO_OFF → init. 32 kHz 의 4 사이클은 122 µs.
- 병렬 시퀀서의 부팅 시간 = 임계 경로(890.010 µs), 직렬은 1551.040 µs. 최적화는 slack 0 노드에서만.
- 내림: 역순 + 도메인 첫 동작 전 ISO_ON + retention 은 CLK_OFF → SAVE → RST (역순의 예외). 레일은 맨 끝.
- 체커는 시퀀서와 독립, 로그만으로 상태를 세우고 (이벤트 × 간선) 단위로 한 번씩 보고. 같은 스펙에서 만든 체커는 스펙 버그를 못 본다.
- Fault: 타임아웃/brown-out = AON 만 남기고 전부, PLL unlock = 그 PLL 하위만. 원인은 AON 의 sticky 레지스터, brown-out 이면 save 안 함 → 다음 부팅은 retention 불신.
- Deglitch(2 µs) < 반응 시한(5 µs). 늦은 이벤트는 세대 번호로 버린다. 글리치 복귀 후 min 지연은 복귀 시각부터.

## 참고자료

- IEEE 1801 (Unified Power Format, UPF) — `create_power_domain`, `create_power_switch`, `set_isolation`, `set_retention`, `-clamp_value`, power state table.
- Keating, Flynn, Aitken, Gibbons, Shi, *Low Power Methodology Manual* (Springer, 2007) — power gating, isolation, retention 시퀀스.
- Arm, *AMBA Low Power Interface Specification* (Q-Channel / P-Channel) — 도메인 quiesce 핸드셰이크 (다음 단계 SOCX-B11).
- Clifford E. Cummings, Don Mills, Steve Golson, "Asynchronous & Synchronous Reset Design Techniques — Part Deux" (SNUG 2003) — 비동기 assert / 동기 deassert 리셋 동기화기 (다음 단계 RTLDV-B06).
- 임계 경로법(CPM) — 간선 지연(lag)이 있는 프로젝트 스케줄링의 표준 기법 (연습 1).
- 이 트랙/다른 트랙: ARCH-B06(플립플롭), ARCH-B13(setup/hold), **ARCH-B14(리셋 원인 · sticky)**, ARCH-A04(돌연변이로 checker 검증), ARCH-A07(동기화기 · MTBF), ARCH-A08(전력/DVFS), SIV-A06(보드 파형), SIV-A11(도메인 시험), TEST-A05(결정론적 이벤트 시뮬레이션). 다음 단계: RTLDV-B06(리셋 동기화기 RTL), SOCX-B11(Q/P-channel), BM-B03(RCC 클럭 게이팅·리셋), NPU-A07(NPU 파워 도메인), BM-M11(저전력 모드), RTLDV-A08(저전력 RTL · UPF-aware 검증).
