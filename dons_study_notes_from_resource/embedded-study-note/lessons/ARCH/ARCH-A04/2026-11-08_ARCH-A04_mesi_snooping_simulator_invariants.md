---
id: ARCH-A04
time: 110분
updated: 2026-11-08
---
# ARCH-A04 MESI 캐시 일관성 프로토콜 시뮬레이터

## 학습 목표

- 코어 4 개 + private 캐시 + **원자적 스누핑 버스**(한 사이클에 트랜잭션 1 개, 라운드로빈 중재) 위에서 MESI 의 전이 함수 3 개(프로세서 쪽 `cpu_access`, 스누프 쪽 `snoop`, 요청자 쪽 `fill_state`)를 짜고, 트레이스 `c<k> R|W|A addr [val]` 의 매 단계를 `c2:I->M c0:S->I c1:S->I src=mem` 같은 한 줄로 설명할 수 있다.
- 일관성의 정의를 **불변식 3 개**로 말한다: SWMR(한 명이 쓰거나 여럿이 읽거나), DATA(모든 유효 복사본과 모든 읽기 = 전역 쓰기 순서상 최신 값, golden shadow memory 로 검사), MEM(M/O 소유자가 없으면 메모리가 최신). 이걸 **매 사이클** 검사하는 checker 를 붙인다.
- 일부러 넣은 프로토콜 버그 8 개(돌연변이, mutant)가 각각 **어느 불변식에 몇 사이클째** 걸리는지 표로 만들고, 고정 시드 랜덤 트레이스로도 모두 잡히는 것을 확인한다 — "검사기를 검사하는" 실리콘 검증(DV) 방식 그대로.
- MSI / MESI / MOESI 를 같은 트레이스에서 세어 **E 는 BusUpgr 를, O 는 메모리 write-back 을 아낀다**는 것을 숫자로 보이고, 표준 패턴 a(공유 카운터)·b(코어별 라인)·b'(같은 라인 = false sharing)·c(읽기 전용 공유)의 연산당 메시지 수를 MEM-M04 실측과 비교할 형태로 남긴다.
- 면접 체크 "쓰기 하나로 MESI 따라가기 (S → I, RFO)" 를 화이트보드에서 5 분 안에 한다.

## 1. 문제

"코어 0 이 S 상태 라인에 쓰면 무슨 일이 일어나나요?" — 멀티코어 펌웨어, 실리콘 검증, AI 가속기/CPU 설계 면접에서 거의 반드시 나오는 질문이다. 3·4 층(C-A03 · OS-M07 · LLAT-M05)에서 "공유 카운터가 느리다", "128 B 패딩이 필요하다", "TAS 가 캐시 라인을 핑퐁시킨다" 를 **측정**했다. 이 레슨은 그 숫자들 밑에 있는 **프로토콜**을 직접 돌려 본다. 하드웨어 일관성(coherence)이 무엇을 보장하고(한 주소에 대한 모든 코어의 합의) 무엇을 보장하지 않는지(서로 다른 주소 사이의 순서 = memory consistency, ARCH-A05 · MEM-M07) 구분하는 것이 목표다.

펌웨어 쪽 현실: 단일 코어 Cortex-M7 은 코어끼리의 일관성 문제는 없지만 **DMA 는 캐시를 스누프하지 않는다** → 소프트웨어가 clean/invalidate 로 프로토콜을 대신한다 (ARCH-M12). Cortex-A 클러스터는 MOESI 계열 하드웨어 프로토콜과 AMBA ACE/CHI 인터커넥트가, Apple M 계열은 (세부 비공개인) 하드웨어 일관성이 이 일을 한다. 실리콘 검증 엔지니어는 바로 이 레슨의 구조 — **랜덤 트레이스 + golden 메모리 + 불변식 assertion + 돌연변이로 checker 검증** — 로 인터커넥트를 검증한다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">시뮬레이터 구조 — 사이클마다: 히트 처리 → 버스 1 트랜잭션 → 불변식 검사</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="150" height="34" rx="4" class="f-mut"/>
    <rect x="200" y="40" width="150" height="34" rx="4" class="f-mut"/>
    <rect x="380" y="40" width="150" height="34" rx="4" class="f-mut"/>
    <rect x="560" y="40" width="150" height="34" rx="4" class="f-mut"/>
    <rect x="20" y="90" width="150" height="50" rx="4" class="f-acc"/>
    <rect x="200" y="90" width="150" height="50" rx="4" class="f-acc"/>
    <rect x="380" y="90" width="150" height="50" rx="4" class="f-acc"/>
    <rect x="560" y="90" width="150" height="50" rx="4" class="f-acc"/>
    <rect x="20" y="170" width="690" height="30" rx="4" class="f-warn"/>
    <rect x="20" y="230" width="220" height="40" rx="4" class="f-ok"/>
    <rect x="260" y="230" width="220" height="40" rx="4" class="f-ok"/>
    <rect x="500" y="230" width="210" height="40" rx="4" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="30" y="62">c0 트레이스 큐</text><text x="210" y="62">c1 트레이스 큐</text>
    <text x="390" y="62">c2 트레이스 큐</text><text x="570" y="62">c3 트레이스 큐</text>
    <text x="30" y="110">L1: sets × ways</text><text x="30" y="128" font-family="monospace">{st,line,d[4]}</text>
    <text x="210" y="110">L1 (private)</text><text x="390" y="110">L1 (private)</text><text x="570" y="110">L1 (private)</text>
    <text x="30" y="190">원자적 스누핑 버스: BusRd / BusRdX / BusUpgr / BusWB, 응답 = shared 신호 · Flush, 라운드로빈 중재</text>
    <text x="30" y="255">메모리 mem[256] (워드)</text>
    <text x="270" y="255">golden[256] = 최신 쓰기 값</text>
    <text x="510" y="248">checker: SWMR · DATA · MEM</text>
    <text x="510" y="264">매 사이클 · 첫 위반 기록</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5">
    <path d="M95 74 L95 90"/><path d="M275 74 L275 90"/><path d="M455 74 L455 90"/><path d="M635 74 L635 90"/>
    <path d="M95 140 L95 170"/><path d="M275 140 L275 170"/><path d="M455 140 L455 170"/><path d="M635 140 L635 170"/>
    <path d="M130 200 L130 230"/><path d="M370 200 L370 230" stroke-dasharray="4 3"/>
  </g>
</svg>
```

모델 크기: 라인 = 4 워드(32-bit) = 16 B, 메모리 256 워드 = 64 라인, 캐시 = 1~16 set × 1~4 way (LRU), 코어 2~8 개(기본 4). 실제 칩(64 B / Apple M 128 B 라인, 수 MB 캐시)과 숫자는 다르지만 **상태 기계와 불변식은 같다**.

| 상태 | 유효 | 나만 가짐 | dirty(메모리와 다름) | 읽기 | 쓰기 |
|---|---|---|---|---|---|
| M (Modified) | ✔ | ✔ | ✔ | 히트 | 히트 |
| O (Owned, MOESI) | ✔ | ✘ (S 가 더 있을 수 있음) | ✔ — 나만 최신 값의 책임자 | 히트 | BusUpgr |
| E (Exclusive) | ✔ | ✔ | ✘ | 히트 | **히트 (조용히 M)** |
| S (Shared) | ✔ | ✘ | ✘ (MOESI 에선 O 와 같은 값) | 히트 | BusUpgr |
| I (Invalid) | ✘ | — | — | BusRd | BusRdX (RFO) |

### Part A — 전이 함수 3 개와 교과서 트레이스

- `cpu_out_t cpu_access(proto, state, op)` — 상태 `state` 의 라인에 `op` 를 하려면 필요한 버스 메시지(없으면 `BUS_NONE` = 로컬 히트)와 히트 시 새 상태.
- `snoop_out_t snoop(proto, state, msg)` — 다른 코어의 `msg` 를 본 캐시의 새 상태, `flush`(라인을 버스에 올려 공급), `shared`(나도 가졌다) 신호.
- `mstate_t fill_state(proto, msg, shared)` — 트랜잭션 후 요청자의 상태 (BusRd 면 shared 신호로 E/S 가 갈린다).
- 12 단계 교과서 트레이스(`trace_textbook.txt`)의 이벤트 로그가 MSI / MESI / MOESI 각각 기대 문자열과 **한 글자까지** 같아야 하고, 통계(BusRd 7, BusRdX 1, BusUpgr 2/3 …)도 손으로 센 값과 같아야 한다. 같은 set 충돌로 M 라인이 축출되면 **BusWB 가 먼저 버스를 한 사이클 쓴다**.

### Part B — 불변식과 돌연변이 (하네스가 제공, 너는 결과를 해석)

- 불변식 3 개를 매 사이클 끝에 모든 라인에 대해 검사. 위반하면 첫 위반의 사이클·이유를 기록하고 멈춘다.
- 돌연변이 8 개: `no_inv_upgr`, `no_inv_rdx`, `e_when_shared`, `silent_s_write`, `no_flush_rd`, `flush_no_memwb`, `evict_no_wb`, `upgr_race`. 각각 directed 트레이스로 **기대한 불변식 · 사이클**에 잡혀야 하고, 올바른 프로토콜은 같은 트레이스를 통과해야 한다.
- 랜덤 스트레스: 3 프로토콜 × 캐시 구성 4 종(2×1, 4×2, 1×4, 8 코어 4×2) × 시드 20 = 240 회 × 2000 연산 → 위반 0, 끝난 뒤 메모리 == golden. 같은 생성기로 돌연변이 8 개 모두 검출.

### Part C — MSI vs MESI vs MOESI, 그리고 패턴 a/b/b'/c

- 사적 데이터(코어마다 자기 라인 R 후 W): MSI 는 BusUpgr 4 번, MESI/MOESI 는 0 번 (E 의 존재 이유).
- 생산자-소비자(c0 쓰기 → c1 읽기 × 8): MSI/MESI 는 메모리 write-back 8 번, MOESI 는 0 번 (O 의 존재 이유).
- 표준 패턴 k = 1..4 코어, 100 라운드 엄격 순환: 연산당 버스 트랜잭션 · c2c · 무효화 · write-back. MEM-M04 의 측정 곡선과 같은 이름(a/b/c)으로 남긴다.

## 2. Problem (English)

> **Snooping MESI simulator with per-cycle invariant checking.** Four cores (up to 8), each with a private cache (configurable sets × ways, LRU), connected by one *atomic* snooping bus: at most one bus transaction per cycle, round-robin arbitration. Bus requests: `BusRd`, `BusRdX` (read-for-ownership), `BusUpgr` (write to a line held in S/O), `BusWB` (write-back of an evicted dirty line). Snoop responses: a wired-OR *shared* signal and *Flush* (the M/O owner supplies the line, cache-to-cache).
>
> Input trace, one op per line: `c<k> R <addr>`, `c<k> W <addr> <val>`, `c<k> A <addr> <inc>` (atomic fetch-add), `sync` (barrier), `#` comments. Each core executes its own ops in program order.
>
> A. Implement the three transition functions `cpu_access(proto, state, op)`, `snoop(proto, state, msg)`, `fill_state(proto, msg, shared)` for MSI, MESI and MOESI. Print, per step, the transition (`c1 S->M`), the bus message, which caches were invalidated and where the data came from (memory or cache-to-cache).
>
> B. After every cycle check: (1) SWMR — at most one owner (M/E/O), and M/E implies all other copies are I; (2) data-value — every valid copy and every read equals the latest write in the global write order (keep a golden shadow memory); (3) memory is up to date unless some cache holds the line in M (or O). Inject at least five protocol bugs and report, for each, which invariant catches it and at which cycle. Stress with deterministic xorshift random traces.
>
> C. Compare MSI vs MESI vs MOESI on traces where E and O matter; report bus transactions by type, cache-to-cache transfers and write-backs for three canonical patterns: shared counter, private counters, read-only shared data.
>
> Follow-ups: walk one write through S → M (who gets invalidated, is data transferred?). Why is the upgrade race only a bug if the request type is decided before arbitration? Why does a real 64-core chip not use a snooping bus? What does an `A` (atomic RMW) cost compared with a plain read in this model, and how does that relate to the measured 24 vs 6786 Mops/s in LLAT-B06?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
```

TODO 는 전이 함수 3 개뿐이다. 시뮬레이터 본체(캐시 조회·축출, 버스 데이터 경로, 중재, 불변식 checker, 돌연변이 주입, 트레이스 파서·생성기)는 starter 와 solution 이 같다. 처음 실행 결과 (실제 출력 일부, 0.03 초, exit 1 — 멈추지 않는다):

```text
== Part A: 교과서 트레이스 (4 코어, 4 set direct-mapped) ==
FAIL starter.c:517  sim_run(S) == INV_OK
  log[0] got  "c0 R 0x00 hit"
         want "c0 R 0x00 BusRd c0:I->S src=mem"
FAIL starter.c:519  log_match(S, exp[p], 12) == 0
  MSI   4c 4x1: cyc=1 ops=1 hit=1 | Rd=0 RdX=0 Upgr=0 WB=0 | flush=0 c2c=0 memrd=0 memwb=0 inval=0 E>M=0
...
  !! VIOLATION DATA at cycle 1: c0 read 0x00 = 0, latest write = 1000
   1  c0 R 0x00 hit
...
  random: 240 runs, 834 ops, 0 write-backs, 0 c2c → violations 240, final mismatch 216
...
12/79 passed
```

스텁 `cpu_access` 가 모든 접근을 "히트" 라고 하니 I 라인(데이터 0)을 그대로 읽고, checker 가 **첫 사이클에** "읽은 값 0, 최신 쓰기 1000" 으로 DATA 위반을 잡는다 (메모리 초기값은 `1000 + 주소` 라서 0 은 절대 맞는 값이 아니다). 시뮬레이터는 첫 위반에서 멈추므로 starter 의 각 실행은 몇 연산 만에 끝난다.

트레이스 파일 모드 (사이클마다 한 줄씩 출력):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so
./so trace_textbook.txt mesi           # msi / moesi 로 바꿔 비교
./so trace_upgr_race.txt mesi          # 올바름
./so trace_upgr_race.txt mesi upgr_race   # 3번째 인자 = 돌연변이 이름 (MUTANTS[] 표)
```

## 4. 힌트

<details><summary>힌트 1 — 프로세서 쪽은 "권한" 만 본다</summary>

읽기에는 유효한 복사본(S/E/O/M)만 있으면 된다. 쓰기에는 **유일한 복사본**이어야 한다. S 와 O 는 남이 가졌을 수 있으니 버스로 알려야 하고(BusUpgr — 데이터는 이미 있으니 무효화만), I 는 데이터도 권한도 없으니 BusRdX(RFO). E 는? 아무도 안 가졌다는 걸 **이미 안다**.
</details>

<details><summary>힌트 2 — 스누프 쪽은 "누가 최신 데이터를 가졌나" 와 "쓰기꾼이 생기나"</summary>

BusRd: 읽기꾼이 하나 늘 뿐 — 나는 공유 상태로 내려가고, 내가 dirty(M/O)면 데이터를 내놓는다(Flush). BusRdX/BusUpgr: 쓰기꾼이 생긴다 — 나는 무조건 I. BusRdX 인데 내가 dirty 면 데이터도 내놓아야 한다(요청자는 복사본이 없다). BusUpgr 를 받는 쪽이 M 일 수 있을까? (SWMR 이 지켜지면 없다 — Part B 의 `upgr_race` 가 이 가정을 깬다.)
</details>

<details><summary>힌트 3 — shared 신호와 E</summary>

BusRd 요청자는 응답을 보고 결정한다: 다른 누구도 "나도 가졌다" 를 안 외쳤으면 E, 외쳤으면 S. 그러려면 스누프 쪽에서 S/E/O/M 모두 BusRd 에 `shared = true` 를 내야 한다. MSI 는 E 가 없으니 항상 S.
</details>

<details><summary>힌트 4 — MOESI 는 한 칸만 다르다</summary>

MESI 에서 BusRd 를 본 M 은 Flush 하고 S 가 된다 → 이때 메모리도 갱신된다(하네스의 데이터 경로가 `proto != PR_MOESI` 일 때 메모리에 쓴다). MOESI 에서는 **메모리를 안 쓰고** O 로 남아 계속 공급 책임을 진다. O 가 BusRd 를 또 보면? O 가 BusRdX 를 보면?
</details>

<details><summary>힌트 5 — 로그가 한 글자 다를 때</summary>

`log[i] got/want` 두 줄을 비교하라. 스누퍼 표기는 상태가 바뀌거나 Flush 했을 때만 찍힌다(`c3:O(Flush)` = O 그대로 공급). `src=c2` 가 나와야 하는데 `src=mem` 이면 M/O 가 flush 를 안 하고 있고, 반대로 `src=mem` 이어야 하는데 `src=c2` 면 E/S 까지 flush 하고 있다. `mem<-c2` 는 하네스가 MSI/MESI 의 flush 에서만 찍는다 (MOESI 는 메모리를 안 쓰니 안 나와야 정상).
</details>

## 5. 풀이

### 0. 사이클 모델 — 왜 "원자적 버스" 인가

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">sim_step() 한 사이클</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="210" height="120" rx="6" class="f-ok"/>
    <rect x="265" y="40" width="210" height="120" rx="6" class="f-acc"/>
    <rect x="505" y="40" width="230" height="120" rx="6" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="32" y="62" font-weight="bold">① 로컬 히트</text>
    <text x="32" y="84">준비된 코어마다 cpu_access</text>
    <text x="32" y="102">BUS_NONE → 그 자리에서 완료</text>
    <text x="32" y="120">(E 쓰기 = 조용히 M)</text>
    <text x="32" y="138">아니면 버스 대기(pend)</text>
    <text x="277" y="62" font-weight="bold">② 버스 1 트랜잭션</text>
    <text x="277" y="84">라운드로빈으로 1 코어 승인</text>
    <text x="277" y="102">메시지 다시 계산 → 축출 WB?</text>
    <text x="277" y="120">모든 스누퍼 → 데이터 → 완료</text>
    <text x="277" y="138">= 다른 트랜잭션과 안 섞임</text>
    <text x="515" y="62" font-weight="bold">③ 불변식</text>
    <text x="515" y="84">SWMR: 소유자 ≤ 1, M/E ⇒ 남 I</text>
    <text x="515" y="102">DATA: 유효 복사본 == golden</text>
    <text x="515" y="120">MEM: M/O 없으면 mem == golden</text>
    <text x="515" y="138">첫 위반 + 사이클 기록 → 정지</text>
  </g>
  <g stroke="currentColor" stroke-width="2" fill="none">
    <path d="M230 100 L262 100"/><path d="M254 94 L264 100 L254 106"/>
    <path d="M475 100 L502 100"/><path d="M494 94 L504 100 L494 106"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="190">sync 가 있으면 "가장 작은 phase 의 연산" 만 준비됨 → 교과서 트레이스는 사이클당 연산 1 개.</text>
    <text x="20" y="210">원자적 버스 = 요청 → 스누프 → 데이터가 한 덩어리. 실제 칩(분할 트랜잭션·디렉터리)은 그 사이에</text>
    <text x="20" y="230">과도 상태(transient state)가 생긴다 — Part B 의 upgr_race 가 그 맛보기다.</text>
  </g>
</svg>
```

원자적 버스는 교과서(Culler & Singh 5 장, Primer 7 장)의 출발점이다. 트랜잭션이 끝날 때까지 다른 트랜잭션이 끼어들지 않으므로 **버스 순서 = 전역 쓰기 순서** 가 되고, 상태는 안정 상태(M/O/E/S/I) 다섯 개면 충분하다. golden 메모리가 "전역 쓰기 순서상 최신 값" 을 들 수 있는 것도 이 덕분이다 — 쓰기는 ①(로컬 히트, 코어 번호 순) 또는 ②(버스 승인) 에서 정확히 한 번 완료되고, 그 순간 golden 을 갱신한다.

### Part A — 전이 함수

**프로세서 쪽** (`cpu_access`):

| 상태 \ 연산 | 읽기 (R) | 쓰기 (W, A) |
|---|---|---|
| I | BusRd | BusRdX (RFO = Read For Ownership) |
| S | 히트 | BusUpgr |
| E | 히트 | **히트 → M (silent upgrade)** |
| O | 히트 | BusUpgr |
| M | 히트 | 히트 |

**스누프 쪽** (`snoop`, 자신이 I 가 아닐 때):

| 상태 \ 본 메시지 | BusRd | BusRdX | BusUpgr |
|---|---|---|---|
| S | S, shared | I | I |
| E | **S**, shared | I | I |
| O | O, shared, **Flush** | I, Flush | I |
| M | MESI: **S**, Flush (메모리도 갱신) / MOESI: **O**, Flush | I, Flush | (SWMR 이면 일어날 수 없음) → I |

**요청자** (`fill_state`): BusRd → `shared ? S : E` (MSI 는 항상 S), BusRdX/BusUpgr → M. 데이터 출처는 하네스가 정한다: Flush 한 스누퍼가 있으면 그 캐시(c2c), 없으면 메모리. BusUpgr 는 데이터가 없다(이미 가졌으니까).

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs>
    <marker id="a04arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0 0 L10 5 L0 10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">MESI — 왼쪽: 내 코어의 접근 (실선) / 오른쪽: 남의 버스 메시지를 스누프 (점선)</text>
  <g stroke="currentColor">
    <circle cx="70" cy="70" r="24" class="f-ok"/><circle cx="290" cy="70" r="24" class="f-warn"/>
    <circle cx="70" cy="240" r="24" class="f-mut"/><circle cx="290" cy="240" r="24" class="f-acc"/>
    <circle cx="450" cy="70" r="24" class="f-ok"/><circle cx="670" cy="70" r="24" class="f-warn"/>
    <circle cx="450" cy="240" r="24" class="f-mut"/><circle cx="670" cy="240" r="24" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="14" font-weight="bold">
    <text x="65" y="75">S</text><text x="284" y="75">M</text><text x="66" y="245">I</text><text x="285" y="245">E</text>
    <text x="445" y="75">S</text><text x="664" y="75">M</text><text x="446" y="245">I</text><text x="665" y="245">E</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5" fill="none" marker-end="url(#a04arr)">
    <path d="M70 216 L70 96"/>
    <path d="M96 240 L264 240"/>
    <path d="M88 223 L270 88"/>
    <path d="M96 70 L264 70"/>
    <path d="M290 216 L290 96"/>
  </g>
  <g fill="currentColor">
    <text x="76" y="160">PrRd / BusRd</text><text x="76" y="174">(shared)</text>
    <text x="110" y="262">PrRd / BusRd (no shared)</text>
    <text x="180" y="196">PrWr / BusRdX</text>
    <text x="118" y="62">PrWr / BusUpgr</text>
    <text x="296" y="150">PrWr / —</text><text x="296" y="164">(silent)</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5" fill="none" stroke-dasharray="5 3" marker-end="url(#a04arr)">
    <path d="M644 70 L476 70"/>
    <path d="M652 87 L470 223"/>
    <path d="M652 223 L470 87"/>
    <path d="M644 240 L476 240"/>
    <path d="M450 96 L450 216"/>
  </g>
  <g fill="currentColor">
    <text x="510" y="62">BusRd / Flush</text>
    <text x="640" y="112">BusRdX/Flush</text>
    <text x="600" y="202">BusRd</text>
    <text x="510" y="262">BusRdX, BusUpgr</text>
    <text x="456" y="160">BusRdX,</text><text x="456" y="174">BusUpgr</text>
    <text x="10" y="292">히트(자기 루프)는 생략. MOESI 는 오른쪽 M→S 대신 M→O (메모리 안 씀),</text>
    <text x="10" y="310">O 는 BusRd 에 Flush 하고 O 유지, BusRdX · BusUpgr 에는 I.</text>
  </g>
</svg>
```

**교과서 트레이스 (MESI, 실제 출력)** — `./so trace_textbook.txt mesi`:

```text
   1  c0 R 0x00 BusRd c0:I->E src=mem
   2  c1 R 0x00 BusRd c1:I->S c0:E->S src=mem
   3  c2 W 0x00=7 BusRdX c2:I->M c0:S->I c1:S->I src=mem
   4  c0 R 0x00 BusRd c0:I->S c2:M->S(Flush) src=c2 mem<-c2
   5  c0 W 0x00=8 BusUpgr c0:S->M c2:S->I
   6  c3 R 0x01 BusRd c3:I->S c0:M->S(Flush) src=c0 mem<-c0
   7  c3 W 0x01=5 BusUpgr c3:S->M c0:S->I
   8  c3 R 0x02 hit
   9  c1 R 0x08 BusRd c1:I->E src=mem
  10  c1 W 0x08=9 hit c1:E->M
  11  c2 R 0x01 BusRd c2:I->S c3:M->S(Flush) src=c3 mem<-c3
  12  c1 R 0x02 BusRd c1:I->S src=mem
  MESI  4c 4x1: cyc=12 ops=12 hit=2 | Rd=7 RdX=1 Upgr=2 WB=0 | flush=3 c2c=3 memrd=5 memwb=3 inval=4 E>M=1
```

단계별로 읽기:

| 사이클 | 핵심 | 면접에서 말할 한 줄 |
|---|---|---|
| 1 | 아무도 없음 → shared 신호 없음 → **E** | "첫 읽기는 E 로 받아 둔다 — 나중에 쓰면 버스가 필요 없다" |
| 2 | E 가 BusRd 를 봄 → S, 데이터는 메모리 (E 는 깨끗하다) | Illinois 계열은 E/S 도 공급할 수 있다 — 이 모델은 메모리 공급 |
| 3 | 쓰기 미스 = **RFO**: 데이터와 소유권을 한 번에, S 둘 다 I | "쓰기 하나가 무효화 N−1 개" |
| 4 | M 이 BusRd 를 봄 → Flush: 요청자에게 직접 + 메모리 갱신, M→S | cache-to-cache(intervention). MOESI 면 메모리 안 씀 |
| 5 | **S 에서 쓰기 → BusUpgr**: 데이터 이동 없음, 남은 S(c2) 만 I | 면접 체크 "S → I, RFO" 의 정답 그림 |
| 6–7 | 워드 1 은 **같은 라인** → 워드 0 쓰기꾼 c0 의 M 을 빼앗아 옴 | 이게 false sharing 의 정체 (ARCH-M08 실측) |
| 8 | 같은 라인의 워드 2 → 히트 | 라인 단위로 상태를 가진다 |
| 9–10 | 혼자 쓰는 라인: BusRd(E) 한 번 뒤 **쓰기는 버스 없음** | MSI 는 여기서 BusUpgr 가 하나 더 (아래) |
| 12 | S 둘(c2, c3)이 있어도 데이터는 메모리에서 | MOESI 는 O(c3) 가 공급 |

**MSI / MOESI 와 다른 줄만** (각각 `./so trace_textbook.txt msi|moesi`, 실제 출력):

| 사이클 | MSI | MOESI |
|---|---|---|
| 1 | `c0 R 0x00 BusRd c0:I->S src=mem` | (MESI 와 같음) |
| 2 | `c1 R 0x00 BusRd c1:I->S src=mem` | (같음) |
| 4 | (MESI 와 같음) | `c0 R 0x00 BusRd c0:I->S c2:M->O(Flush) src=c2` |
| 5 | (같음) | `c0 W 0x00=8 BusUpgr c0:S->M c2:O->I` |
| 6 | (같음) | `c3 R 0x01 BusRd c3:I->S c0:M->O(Flush) src=c0` |
| 7 | (같음) | `c3 W 0x01=5 BusUpgr c3:S->M c0:O->I` |
| 9 | `c1 R 0x08 BusRd c1:I->S src=mem` | (같음) |
| 10 | `c1 W 0x08=9 BusUpgr c1:S->M` | (같음) |
| 11 | (같음) | `c2 R 0x01 BusRd c2:I->S c3:M->O(Flush) src=c3` |
| 12 | (같음) | `c1 R 0x02 BusRd c1:I->S c3:O(Flush) src=c3` |

```text
  MSI   4c 4x1: cyc=12 ops=12 hit=1 | Rd=7 RdX=1 Upgr=3 WB=0 | flush=3 c2c=3 memrd=5 memwb=3 inval=4 E>M=0
  MESI  4c 4x1: cyc=12 ops=12 hit=2 | Rd=7 RdX=1 Upgr=2 WB=0 | flush=3 c2c=3 memrd=5 memwb=3 inval=4 E>M=1
  MOESI 4c 4x1: cyc=12 ops=12 hit=2 | Rd=7 RdX=1 Upgr=2 WB=0 | flush=4 c2c=4 memrd=4 memwb=0 inval=4 E>M=1
```

MOESI 는 끝났을 때 `mem[0] == 1000` (옛 값), `golden[0] == 8` 이다 — O(c3) 가 dirty 를 쥐고 있고 MEM 불변식은 "M/O 소유자가 있으니 괜찮다" 고 본다. 테스트는 이것도 CHECK 한 뒤 `sim_flush_all` 로 내려 메모리 == golden 을 확인한다.

**축출** (direct-mapped 4 set: 0x00 과 0x40 은 같은 set 0, 실제 출력):

```text
c0 W 0x00=5 BusRdX c0:I->M src=mem
c0 BusWB 0x00 c0:M->I mem<-c0             ← dirty 축출이 한 사이클 버스를 쓴다 (요청은 다음 승인)
c0 R 0x40 BusRd c0:I->E src=mem
c0 R 0x01 BusRd c0:I->E drop(0x40:E) src=mem   ← 깨끗한 라인(E/S)은 조용히 버린다
c1 R 0x00 BusRd c1:I->S c0:E->S src=mem
```

2-way 로 바꾸면 같은 트레이스에서 BusWB 0 (CHECK). 스누핑 프로토콜은 S/E 축출을 알릴 필요가 없다 — 누가 S 를 가졌는지 아무도 기록하지 않으니까. 디렉터리 프로토콜은 다르다(공유자 목록이 틀려짐 → PutS 같은 메시지 또는 "부정확한 목록" 허용, Primer 8 장).

### Part B — 불변식, 돌연변이, 랜덤 스트레스

**불변식의 근거.** Nagarajan·Sorin·Hill·Wood, *A Primer on Memory Consistency and Cache Coherence* (2nd ed., 2020) 2 장은 일관성을 두 불변식으로 정의한다: **SWMR**(Single-Writer, Multiple-Reader — 어느 시점이든 한 주소에 대해 쓸 수 있는 코어 1 개, 또는 읽기만 하는 코어 여럿) 과 **Data-Value**(한 epoch 시작 시 값 = 직전 read-write epoch 끝의 값). 이 시뮬레이터의 DATA 불변식은 원자적 버스 덕에 더 강한 형태로 검사할 수 있다: "유효한 복사본은 **항상** golden 과 같다" (쓰기가 다른 복사본을 같은 트랜잭션 안에서 무효화하니까). MEM 은 프로토콜 고유의 보조 불변식 — write-back 이 빠지면 SWMR·DATA 는 그 순간 멀쩡하고 **나중에** 누가 메모리에서 읽을 때야 터지는데, MEM 은 그걸 **원인 사이클에** 잡는다.

```c
for (uint32_t L = 0; L < NLINES; L++)                 /* MEM: dirty 소유자(M/O)가 없으면 메모리가 최신 */
    if (dirty[L] == 0 && memcmp(&s->mem[L * LINE_WORDS], &s->gold[L * LINE_WORDS],
                                LINE_WORDS * sizeof(uint32_t)) != 0) {
        violate(s, INV_MEM, "line 0x%02x: no M/O owner but memory is stale", L * LINE_WORDS);
        return;
    }
```

**돌연변이 표** (solution 실행의 실제 출력에서):

| 돌연변이 | 주입한 버그 | directed 트레이스 | 잡은 불변식 · 사이클 | 위반 메시지 | 랜덤(시드 1) |
|---|---|---|---|---|---|
| `no_inv_upgr` | BusUpgr 를 본 S 가 안 무효화 | c0,c1 R 0 / c0 W 0 | **SWMR** 3 | `c0=M c1=S` | SWMR 16 |
| `no_inv_rdx` | BusRdX 를 본 S 가 안 무효화 | c0,c1 R 0 / c2 W 0 | **SWMR** 3 | `c0=S c1=S c2=M` | SWMR 28 |
| `e_when_shared` | shared 신호 무시하고 E | c0 R 0 / c1 R 0 | **SWMR** 2 | `c0=S c1=E` | SWMR 4 |
| `silent_s_write` | S 에서 버스 없이 M | c0,c1 R 0 / c0 W 0 | **SWMR** 3 | `c0=M c1=S` | SWMR 39 |
| `no_flush_rd` | BusRd 를 본 M 이 데이터 안 냄 | c0 W 0 1 / c1 R 0 | **DATA** 2 | `c1 read 0x00 = 1000, latest write = 1` | DATA 10 |
| `flush_no_memwb` | MESI 인데 Flush 때 메모리 안 씀 | c0 W 0 1 / c1 R 0 | **MEM** 2 | `no M/O owner but memory is stale` | MEM 10 |
| `evict_no_wb` | M 축출 때 write-back 없이 버림 | c0 W 0 1 / c0 R 64 | **MEM** 2 | 〃 | MEM 7 |
| `upgr_race` | 대기 중 무효화돼도 옛 BusUpgr 송신 | c0,c1 R 0 / c0 W 0, c1 W 1 | **DATA** 4 | `c1 holds a stale copy of line 0x00` | DATA 18 |

읽는 법:
- **SWMR 류** (무효화 누락, 잘못된 E, 조용한 S 쓰기)는 **잘못된 전이가 일어난 그 사이클**에 잡힌다. 값은 아직 멀쩡하다 — 값만 보는 테스트(예: 최종 메모리 비교)는 한참 뒤에야, 혹은 영영 못 잡는다.
- `no_flush_rd` 는 요청자가 **메모리의 옛 값**을 받는다 → 그 읽기가 바로 DATA 위반. `flush_no_memwb` 는 요청자는 c2c 로 맞는 값을 받지만 M 이 S 로 내려가면서 "dirty 책임자" 가 사라진다 → MEM. **이 돌연변이는 "O 상태 없이 MOESI 를 흉내 낸 것"** 이다. 같은 동작이 MOESI 에서 정답인 이유는 M 이 S 가 아니라 O 로 가서 책임을 유지하기 때문.
- `evict_no_wb` 의 MEM 위반은 축출 사이클에 뜬다. MEM 불변식이 없었다면 그 라인을 누군가 메모리에서 다시 읽는 사이클까지(읽는 코어가 없으면 영원히) 드러나지 않는다 — **원인과 증상의 거리**를 줄이는 게 좋은 checker 다.

**`upgr_race` 자세히** — 두 S 소유자가 같은 사이클에 쓰기:

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">사이클 3: c0·c1 둘 다 S, 둘 다 쓰기 → 둘 다 "BusUpgr 필요" 로 대기. 중재기는 c0 승인</text>
  <g stroke="currentColor">
    <rect x="20" y="34" width="330" height="70" rx="5" class="f-mut"/>
    <rect x="20" y="124" width="330" height="100" rx="5" class="f-ok"/>
    <rect x="390" y="124" width="330" height="100" rx="5" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="30" y="54" font-family="monospace">3  c0 W 0x00=1 BusUpgr c0:S->M c1:S->I</text>
    <text x="30" y="74">c1 은 아직 대기 중인데 자기 라인이 I 가 됐다.</text>
    <text x="30" y="92">c1 이 사이클 3 에 정한 메시지 = BusUpgr</text>
    <text x="30" y="144" font-weight="bold">올바름: 승인 시점에 다시 계산</text>
    <text x="30" y="164" font-family="monospace">4  c1 W 0x01=2 BusRdX c1:I->M</text>
    <text x="30" y="180" font-family="monospace">     c0:M->I(Flush) src=c0 mem&lt;-c0</text>
    <text x="30" y="200">c0 의 쓰기(워드0=1)를 받아 와서 워드 1 을 쓴다.</text>
    <text x="30" y="216">5–6 사이클: c2 가 1 과 2 를 모두 읽는다 → OK</text>
    <text x="400" y="144" font-weight="bold">돌연변이: 옛 BusUpgr 그대로</text>
    <text x="400" y="164" font-family="monospace">4  c1 W 0x01=2 BusUpgr c1:I->M c0:M->I</text>
    <text x="400" y="182">BusUpgr = 데이터 없음 → I 라인의 옛 데이터</text>
    <text x="400" y="198">(워드 0 = 1000) 로 M. c0 의 쓰기 1 은 사라졌다.</text>
    <text x="400" y="216">!! DATA at cycle 4: c1 holds a stale copy</text>
  </g>
</svg>
```

이게 왜 중요한가: 원자적 버스에서는 "승인 때 다시 계산" 으로 쉽게 고쳐진다. 그러나 **분할 트랜잭션 버스 / 디렉터리 / 메시 NoC** 에서는 요청이 이미 떠난 뒤에 무효화가 도착한다. 그래서 실제 프로토콜은 과도 상태(Primer 표기 `SM^AD` — "S 에서 M 으로 가는 중, 아직 내 요청이 순서를 못 받음", 표기 세부는 장마다 다를 수 있음)를 두고, 그 상태에서 다른 코어의 쓰기 요청을 보면 `IM^AD`(데이터까지 필요해짐)로 바꾼다. 업그레이드 경쟁은 실리콘 버그 단골이고, 이 레슨의 랜덤 트레이스도 시드 1 의 18 번째 사이클에 그것을 스스로 찾아냈다.

**랜덤 스트레스 (실제 출력):**

```text
  random: 240 runs, 480000 ops, 29739 write-backs, 202281 c2c → violations 0, final mismatch 0
```

생성기: xorshift32, 시드 고정, 6 개 라인 × 4 워드 범위에서 R 50% / W 30% / A 20%, 1/16 확률로 sync. 라인 6 개를 2 set direct-mapped 에 넣으면 축출이 잦아 BusWB 경로까지 덮는다 (`write-backs 29739`). 이 숫자들이 0 이 아닌 것도 CHECK 한다 — **스트레스가 실제로 그 경로를 지나갔다는 증거**(coverage) 없이 "위반 0" 은 의미가 없다.

### Part C — MSI vs MESI vs MOESI, 표준 패턴

**E 와 O 가 각각 무엇을 아끼나** (실제 출력):

```text
  private R+W x4     MSI   4c 4x1: cyc=8 ops=8 hit=0 | Rd=4 RdX=0 Upgr=4 WB=0 | flush=0 c2c=0 memrd=4 memwb=0 inval=0 E>M=0
  private R+W x4     MESI  4c 4x1: cyc=5 ops=8 hit=4 | Rd=4 RdX=0 Upgr=0 WB=0 | flush=0 c2c=0 memrd=4 memwb=0 inval=0 E>M=4
  prod-cons x8       MESI  4c 4x1: cyc=16 ops=16 hit=0 | Rd=8 RdX=1 Upgr=7 WB=0 | flush=8 c2c=8 memrd=1 memwb=8 inval=7 E>M=0
  prod-cons x8       MOESI 4c 4x1: cyc=16 ops=16 hit=0 | Rd=8 RdX=1 Upgr=7 WB=0 | flush=8 c2c=8 memrd=1 memwb=0 inval=7 E>M=0
```

| 비교 | 트레이스 | 차이 | 이유 |
|---|---|---|---|
| MSI → MESI | 코어마다 자기 라인 R 후 W | BusUpgr 4 → **0**, 사이클 8 → 5 | 읽을 때 "나만 가졌다" 를 알아 두면(E) 쓰기에 허락이 필요 없다. 단일 스레드 코드의 거의 모든 데이터가 이 경우 |
| MESI → MOESI | c0 쓰기 → c1 읽기 × 8 | 메모리 write-back 8 → **0** | 공유가 생길 때마다 DRAM 까지 내려가지 않고 O 가 계속 책임. 생산자-소비자·락 핸드오프에서 메모리 대역폭 절약 |
| MESIF (미구현) | 패턴 c (읽기 공유) | 개념: 공유본 중 하나(F)만 응답 → 메모리 대신 c2c | Intel 의 MESIF 설명 기준 (Goodman & Hum). 이 모델의 MESI 는 S 가 공급하지 않아 패턴 c 의 메모리 읽기가 k 번 — F 가 있으면 1 번 + c2c (k−1) 번이 될 것이다 (미검증, 개념) |

**표준 패턴 a/b/b'/c** — k 코어가 c0, c1, …, c(k−1), c0, … 엄격 순환으로 100 라운드 (실제 출력, MESI):

```text
  pattern (MESI, 100 rounds)   k  ops   bus/op  c2c/op  inval/op  memwb/op
  a shared counter             1  100    0.010   0.000     0.000     0.000
  a shared counter             2  200    1.000   0.995     0.995     0.995
  a shared counter             4  400    1.000   0.998     0.998     0.998
  b per-core lines             4  400    0.010   0.000     0.000     0.000
  b' same line                 4  400    1.000   0.998     0.998     0.998
  c read-only shared           4  400    0.010   0.000     0.000     0.000
  a shared counter, MOESI    4  400   RdX=400 c2c=399 memwb=0
  a, sync per round only     4  400   RdX=301 c2c=300 hits=99  (0.750 transfers/op)
```

(k = 3 줄 등은 생략, 전체는 `./so` 출력.) 손으로 유도한 공식 (n = k × 라운드, k ≥ 2):

| 패턴 | BusRd | BusRdX | c2c | 무효화 | write-back (MSI/MESI · MOESI) | 연산당 전송 |
|---|---|---|---|---|---|---|
| a 공유 카운터 `A 0 1` | 0 | n | n − 1 | n − 1 | n − 1 · 0 | **≈ 1** |
| b 코어별 라인 `A 4c 1` | 0 | k | 0 | 0 | 0 | **→ 0** |
| b' 같은 라인 다른 워드 `A c 1` | 0 | n | n − 1 | n − 1 | n − 1 · 0 | **≈ 1 (= a)** |
| c 읽기 전용 `R 0` | k | 0 | 0 | 0 | 0 | **→ 0** |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">모델: 연산당 라인 전송 (k = 4, MESI)  vs  실측 (LLAT-B06, 8 스레드, Mops/s)</text>
  <g fill="currentColor">
    <text x="10" y="52">a 공유 카운터</text><text x="10" y="82">b' 같은 라인</text>
    <text x="10" y="112">b 코어별 라인</text><text x="10" y="142">c 읽기 공유</text>
  </g>
  <g stroke="currentColor">
    <rect x="120" y="38" width="250" height="20" class="f-warn"/>
    <rect x="120" y="68" width="250" height="20" class="f-warn"/>
    <rect x="120" y="98" width="3" height="20" class="f-ok"/>
    <rect x="120" y="128" width="3" height="20" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="378" y="52">0.998</text><text x="378" y="82">0.998</text>
    <text x="130" y="112">0.000 (첫 접근만)</text><text x="130" y="142">0.000 (첫 읽기만)</text>
    <text x="470" y="52">경합 CAS 루프  24 Mops/s</text>
    <text x="470" y="112">스레드별 슬롯  6786 Mops/s</text>
    <text x="470" y="82">(ARCH-M08 · MEM-M04 실측 참조)</text>
    <text x="470" y="142">(MEM-M04 실측 참조)</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="180">a 는 전송이 직렬: 처리량 ≈ 1 / (라인 전송 지연). LLAT-M05 의 P–P 왕복 74–90 ns ⇒ 편도 ≈ 40 ns ⇒ ≈ 25 Mops/s.</text>
    <text x="10" y="200">b 는 히트만: 코어마다 ≈ 1.2–1.4 ns (LLAT-B06 비경합 ldadd) × 8 코어 ⇒ 수천 Mops/s.</text>
    <text x="10" y="220">모델 하나 + 두 실측(전송 지연, 히트 비용)으로 ≈ 280× 차이를 대략 설명할 수 있다.</text>
    <text x="10" y="240">(대략적 추정: 코어 종류·클럭·load 에 따라 흔들린다. 배수만 본다.)</text>
  </g>
</svg>
```

모델과 실측의 연결 (이 맥 = Apple M6, Super 2 + Performance 4 + Efficiency 6, `hw.cachelinesize = 128`, Apple clang 21. 이 레슨 자체는 단일 스레드 시뮬레이터라 타이밍을 재지 않았다 — 아래 숫자는 다른 레슨의 실측 인용):

- **패턴 a (연산당 전송 1)**: LLAT-B06 의 8 스레드 경합 CAS 루프 24 Mops/s. 전송이 하나씩 직렬로 일어나니 처리량 ≈ 1 / 편도 전송 지연. LLAT-M05 의 P–P 핑퐁 왕복 p50 74–90 ns 의 절반 ≈ 40 ns → ≈ 25 Mops/s. 대략 맞는다 (우연에 가까운 일치일 수 있다 — 코어 배치·경합 재시도는 모델에 없다).
- **패턴 b (전송 0)**: LLAT-B06 스레드별 슬롯 6786 Mops/s ≈ 8 코어 × 비경합 `ldadd` ≈ 1.2–1.4 ns. 모델은 "전송 없음 → 코어 수에 선형" 을 예측한다.
- **패턴 b' = a**: 서로 다른 변수인데 같은 라인이면 프로토콜에겐 같은 라인이다 — false sharing. ARCH-M08 이 stride 를 쓸어 이 맥의 일관성 단위(128 B, LLAT-M05: E 코어가 끼면 64 B 로는 부족)를 실측한다.
- **"sync per round only" 0.750**: 라운드 경계마다 직전 라운드의 마지막 소유자가 다음 라운드 첫 사이클에 **로컬 히트**한다 → 전송 (k−1)/k. 실제 스레드도 한 코어가 연달아 여러 번 연산하면 그만큼 전송이 준다 — 경합 카운터 처리량이 "스레드 수만큼 나빠지지는 않는" 이유 중 하나, 그리고 **배치(batching)** 의 원리.
- **모델이 안 맞는 곳** (MEM-M04 와 비교할 때 기억할 것): Apple M 계열은 클러스터 내 공유 L2 + SLC 구조이고 프로토콜·디렉터리 구조는 공개 문서가 없다(확인 불가). P↔E 전송은 P–P 보다 3–4 배 느리다(LLAT-M05). 실제 칩은 버스가 아니라 디렉터리/스누프 필터 + 분할 트랜잭션이라 여러 전송이 겹친다. 스토어 버퍼·하드웨어 프리페치·LSE far atomic(ARM-M04 의 가설) 도 모델에 없다.

**왜 실제 큰 칩은 스누핑 버스가 아닌가** — 연습 4 의 실제 출력: 브로드캐스트는 트랜잭션마다 나머지 n−1 개 캐시가 태그를 조회하지만, 실제로 반응해야 했던 캐시는 패턴 a 에서 트랜잭션당 1 개(직전 소유자), 패턴 c 에서 전체 1 개(첫 E→S)뿐이다. n = 8 에서 필요/조회 비율이 0.141, 0.018 로 떨어진다. 디렉터리는 "누가 가졌나" 를 홈 노드에 기록해 필요한 캐시에만 보낸다(Primer 8 장). 대신 간접 경로(요청 → 홈 → 소유자 → 요청자 = 3-hop)와 디렉터리 저장 공간을 낸다 — MESIF 의 F, AMBA CHI 의 DCT(Direct Cache Transfer) 같은 장치는 이 hop 을 줄이려는 것이다 (CHI 세부는 문서 기준, 미검증).

**실제 프로토콜 이름 대응** (문서 기준, 세부 확인 필요): AMBA ACE / CHI 의 캐시 상태 UniqueDirty(UD) = M, UniqueClean(UC) = E, SharedDirty(SD) = O, SharedClean(SC) = S, Invalid = I. AMD64 APM Vol. 2 는 MOESI 를 문서화한다. Intel 은 MESIF 를 QPI 시절부터 설명했다. Cortex-A53 TRM 은 L1 데이터 캐시 일관성에 MOESI 를 쓴다고 적는다(확인 필요).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so          # 79/79 passed, 0.3 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_san && ./so_san   # 깨끗
```

(Apple M6, `sysctl -n hw.perflevel0/1/2.physicalcpu` = 2/4/6, Apple clang 21.0.0, `uptime` load 5–7. 단일 스레드 결정적 모델이라 타이밍 없음 · TSan 해당 없음 · load 영향 없음.)

바뀐 곳은 전이 함수 3 개의 본문뿐이다 — 프로토콜 전체가 **표 3 개(프로세서 10 칸, 스누프 15 칸, 요청자 3 칸)** 라는 뜻이다. 나머지 600 줄은 그 표를 "검증 가능한 기계" 로 만드는 하네스: 캐시·축출, 데이터 경로(Flush → 요청자·메모리), 중재, checker, 돌연변이 주입점, 파서, 트레이스 생성기. 실리콘 검증에서도 비율이 비슷하다 — 설계보다 테스트벤치가 크다.

설계 메모:
- **돌연변이를 전이 함수 밖에서 씌운다** (`cpu_m` / `snoop_m` / `fill_m` 래퍼, 데이터 경로의 `MUT_FLUSH_NO_MEMWB`, 중재의 `MUT_UPGR_RACE`, 축출의 `MUT_EVICT_NO_WB`). 학생 코드(전이 표)를 고치지 않고 버그를 주입할 수 있고, 같은 checker 로 정답·돌연변이를 다 돌린다.
- **I 라인의 데이터를 지우지 않는다**: 무효화는 상태 비트만 바꾼다(실제 하드웨어처럼). 그래서 `upgr_race` 처럼 "I 인데 태그가 같은 라인을 데이터 없이 M 으로" 만드는 버그가 **옛 데이터를 진짜로 되살려** DATA 위반이 된다.
- **위반은 첫 번째만 기록하고 멈춘다**: 첫 위반 이후의 상태는 이미 오염돼 있어 뒤의 위반은 원인 분석에 방해만 된다 (DV 에서도 첫 assertion 실패에서 멈추고 파형을 본다).
- **결정적**: 난수는 xorshift32 고정 시드, 중재는 라운드로빈, 같은 사이클 히트는 코어 번호 순. 6 번 실행한 출력이 바이트 단위로 같다 (`cmp` 로 확인).

## 7. 자주 하는 실수

1. **E 에 쓰기를 BusUpgr 로 보낸다** — 동작은 맞지만 E 를 만든 의미가 없다. Part C 의 사적 데이터 트레이스에서 MESI 의 BusUpgr 가 4 가 나온다. 고친 코드: `case ST_E: if (wr) o.next = ST_M; break;`
2. **BusRd 를 본 E/S 가 shared 신호를 안 낸다** — 요청자가 E 를 받아 SWMR 위반 (`e_when_shared` 돌연변이와 같은 증상). 스누프 쪽 BusRd 첫 줄이 `o.shared = true;` 인 이유: 유효한 복사본은 상태와 무관하게 "나도 있다" 를 말해야 한다.
3. **MOESI 에서 O 가 BusRd 에 Flush 를 안 한다** — O 만 최신 값을 가졌으니(메모리는 옛 값) 요청자가 메모리에서 옛 값을 받는다 → DATA (연습 3 의 `o_no_supply`). 고친 코드: `else if (s == ST_O) o.flush = true;`
4. **BusRdX 를 본 M 이 무효화만 하고 Flush 를 안 한다** — BusUpgr 와 혼동. BusRdX 요청자는 복사본이 **없다**. 고친 코드: `if (s == ST_M || s == ST_O) o.flush = true; o.next = ST_I;`
5. **"MESI 에서 M→S 때 메모리 갱신은 선택" 이라고 생각한다** — 선택이 아니라 프로토콜이 가진 상태로 결정된다. S 는 "깨끗하다" 는 약속이라 M→S 면 누군가는 메모리를 써야 한다(`flush_no_memwb` → MEM 위반). 메모리를 안 쓰고 싶으면 그걸 표현할 상태(O)가 필요하다 = MOESI.
6. **불변식을 끝에서 한 번만 검사한다** — 최종 메모리 비교만 하면 `no_inv_upgr` 같은 SWMR 버그는 그 뒤 값이 우연히 덮어쓰여 안 보일 수 있고, 보여도 원인 사이클을 모른다. 매 사이클 검사 + 첫 위반의 사이클을 남겨야 디버깅이 된다.
7. **커버리지 없이 "랜덤 240 회 통과" 를 믿는다** — 생성기가 축출을 안 만들면 BusWB 경로는 한 번도 안 돌았는데도 통과한다. 그래서 `wb > 0 && c2c > 0` 도 CHECK 하고, 돌연변이가 **랜덤으로도** 모두 잡히는지 본다(= 생성기의 힘 측정).

## 8. 면접 Q&A

<details><summary>Q1. 코어 0 이 S 상태인 라인에 쓴다. MESI 에서 무슨 일이 일어나나? (면접 체크: S → I, RFO)</summary>

코어 0 은 BusUpgr(일부 구현은 BusRdX 로 통일)를 낸다 — 데이터는 이미 있으니 "무효화해 달라" 만. 버스에 연결된 다른 캐시가 스누프해서 S 복사본을 모두 I 로 바꾼다. 트랜잭션이 끝나면 코어 0 은 M, 쓰기는 캐시에만 반영되고 메모리는 옛 값. 이 레슨 로그: `c0 W 0x00=8 BusUpgr c0:S->M c2:S->I`. I 에서 쓰면 BusRdX(RFO) — 데이터와 소유권을 한 번에 받는다: `c2 W 0x00=7 BusRdX c2:I->M c0:S->I c1:S->I src=mem`.

꼬리질문: "그 사이에 다른 코어가 같은 라인에 쓰려 하면?" → 버스가 둘을 직렬화한다. 진 쪽은 자기 S 가 I 로 바뀌므로 BusUpgr 가 아니라 BusRdX 를 내야 한다 — `upgr_race` 돌연변이가 이 재계산을 빼먹으면 사이클 4 에 DATA 위반. 분할 트랜잭션 시스템에서는 과도 상태(Primer 표기 `SM^AD` → 다른 코어의 GetM 을 보면 `IM^AD`)로 처리한다.
</details>

<details><summary>Q2. E 상태는 왜 있나? MSI 로 충분하지 않나?</summary>

정확성은 MSI 로 충분하다. E 는 성능: 읽기 미스 때 "아무도 안 가졌다" 를 shared 신호로 알아 두면, 이후 쓰기가 버스 없이 E→M 이 된다. 단일 스레드 코드의 사적 데이터(스택, 힙의 대부분)는 거의 항상 "읽고 나서 쓴다" — Part C 에서 BusUpgr 4 → 0, 사이클 8 → 5. 대가: shared 신호(와이어드 OR 또는 응답 집계)가 필요하고, E 라인은 조용히 M 이 될 수 있어서 디렉터리 시스템은 E 소유자를 "M 일 수도 있는 소유자" 로 취급해야 한다.

꼬리질문: "E 라인을 축출할 때 write-back 이 필요한가?" → 아니다, 깨끗하다. 다만 디렉터리에서는 알리지 않으면 디렉터리가 계속 소유자로 기억한다(다음 요청 때 소유자에게 물어봤다가 "없다" 응답을 받는 식으로 처리 — 구현마다 다름).
</details>

<details><summary>Q3. MOESI 의 O 는 무엇을 해결하나? 대가는?</summary>

MESI 에서 M 라인을 누가 읽으면 M→S 로 내려가며 **메모리에도 써야** 한다 (S 는 깨끗하다는 약속). 생산자-소비자처럼 쓰기·읽기가 번갈아 오면 매번 DRAM write-back — Part C 에서 8 번. O 는 "dirty 하지만 공유 중, 내가 공급 책임자" 를 표현해서 write-back 을 축출 때까지 미룬다 → 0 번. 대가: 상태 하나 더, O 가 BusRd 마다 공급해야 하고(연습 3 `o_no_supply` 버그 → DATA), O 축출 때 write-back 을 잊으면 데이터 유실(`o_evict_silent` → MEM). AMD 가 MOESI 를 문서화했고, AMBA 의 SharedDirty 가 O 에 해당한다.

꼬리질문: "그럼 MESIF 의 F 는?" → F 는 깨끗한 공유본 중 **응답 담당 하나**를 정해 메모리 대신 캐시가 공급하게 한다(점대점 인터커넥트에서 여러 S 가 동시에 응답하는 걸 막고 지연을 줄임). O 는 dirty 공유, F 는 clean 공유의 지정 공급자 — 목적이 다르다.
</details>

<details><summary>Q4. 일관성(coherence)을 형식적으로 어떻게 정의하고, 시뮬레이터에서 어떻게 검사했나?</summary>

Primer 2 장의 두 불변식: SWMR(한 주소에 대해 어느 시점이든 쓰기 가능 1 명 또는 읽기 전용 여럿) + Data-Value(각 읽기 구간의 값 = 직전 쓰기 구간의 마지막 값). 구현: 매 사이클 끝에 모든 라인의 상태를 모아 소유자(M/E/O) ≤ 1, M/E 면 나머지 I 를 본다(SWMR). golden 메모리 = 버스/히트에서 완료된 쓰기를 순서대로 반영한 그림자 메모리 → 모든 읽기 값과 모든 유효 복사본이 golden 과 같아야 한다(DATA). 보조로 M/O 가 없으면 메모리 == golden (MEM) — write-back 누락을 원인 사이클에 잡는다.

꼬리질문: "일관성이 지켜지면 SB 리트머스 결과 (0,0) 은 안 나오나?" → 나온다. 일관성은 **한 주소** 에 대한 합의다. 서로 다른 두 주소 사이의 순서는 consistency(메모리 모델)이고, 스토어 버퍼가 일관성 프로토콜 **앞에** 있어서 store→load 재배치가 생긴다 (ARCH-A05 실측, MEM-M07 모델, OS-A03 demo_sb).
</details>

<details><summary>Q5. 프로토콜 checker 가 버그를 잡을 수 있다는 건 어떻게 믿나? (실리콘 검증 질문)</summary>

돌연변이 테스트(mutation testing): 알려진 버그를 하나씩 주입하고 checker 가 **잡는지** 본다. 이 레슨은 8 개를 넣었고, directed 트레이스로는 기대한 불변식·사이클에서, 랜덤 트레이스로는 시드 1 안에서 모두 잡혔다. 잡히지 않는 돌연변이가 있으면 (a) checker 가 약하거나 (b) 생성기가 그 상황을 못 만들거나 (c) **등가 돌연변이**(그 코드가 절대 실행 안 됨 — 연습 3: MESI 에는 O 가 없어 O 관련 버그가 무해)다. 커버리지도 같이 본다: 랜덤 240 회에서 write-back 29739 번, c2c 202281 번이 실제로 일어났다.

꼬리질문: "RTL 에서는 이걸 어떻게 하나?" → 불변식을 SVA assertion 으로 캐시 상태 신호에 걸고(혹은 형식 검증 툴로 증명), 랜덤 스티뮬러스 + 참조 모델(scoreboard = golden memory) 로 데이터를 비교한다. 돌연변이는 일부러 RTL 버그를 넣거나 "checker 끄기 → 기존 회귀가 실패하는가" 로 검증한다 (방법론 일반론, 특정 툴 미검증).
</details>

<details><summary>Q6. 같은 라인의 서로 다른 변수(false sharing)를 프로토콜 관점에서 설명하라. 고치는 법은?</summary>

프로토콜은 라인 단위로 상태를 가진다. 코어 0 이 워드 0, 코어 1 이 워드 1 을 쓰면 서로 무관한 변수인데도 쓰기마다 상대의 M 을 빼앗아야 한다 → 패턴 b' 의 연산당 전송 ≈ 1, 패턴 a(진짜 공유)와 **같은 숫자**. 고치는 법: 변수를 라인 크기 이상 간격으로 — 연습 2 에서 stride 1·2·3 워드는 전송 79/78/39, stride 4(= 라인) 부터 0. 실제 코드: `alignas(128)` (Apple M), Linux `____cacheline_aligned_in_smp`, C++ `std::hardware_destructive_interference_size` (ARCH-M08, LLAT-M01).

꼬리질문: "Cortex-M7 단일 코어에서 false sharing 이 문제인가?" → 코어끼리는 없다. 대신 DMA 버퍼가 다른 변수와 32 B 라인을 공유하면 invalidate 가 이웃 변수의 dirty 데이터를 날린다 — 같은 "라인 단위" 문제의 소프트웨어 버전 (ARCH-M12).
</details>

<details><summary>Q7. 64 코어 칩이 스누핑 버스를 안 쓰는 이유와 대안은?</summary>

모든 트랜잭션을 모든 캐시가 봐야 한다 → 버스 대역폭과 태그 조회가 코어 수에 비례(트랜잭션당 n−1 조회). 연습 4 에서 n = 8 일 때 실제로 반응해야 했던 캐시는 조회의 14%(패턴 a), 1.8%(패턴 c). 대안: 디렉터리(라인별 공유자 목록, 필요한 캐시에만 점대점 메시지), 스누프 필터(포함적 태그 사본으로 "아무도 없음" 을 걸러냄), 계층화(클러스터 안은 스누프, 밖은 디렉터리). 대가: 3-hop 지연, 디렉터리 저장 공간, 과도 상태 폭증(검증 난이도).

꼬리질문: "Apple M 은?" → 클러스터 공유 L2 + SLC 구조까지는 알려져 있지만 프로토콜은 공개 문서가 없다 — 측정(LLAT-M05: P–P vs P↔E 지연 3–4 배)으로 토폴로지를 추정할 뿐이다.
</details>

## 9. 추가 연습

1. **패턴 공식 일반화.** 패턴 a/b/b'/c 의 BusRd·BusRdX·BusUpgr·c2c·메모리 읽기·write-back·무효화·히트를 k(1..8), 라운드 R(1..6), 프로토콜 3 종에 대한 식으로 쓰고 시뮬레이터와 비교하라.
   <details><summary>답</summary>

   n = kR. a·b' (k ≥ 2): BusRdX = n, memrd = 1, c2c = 무효화 = n−1, write-back = MSI/MESI n−1 · MOESI 0, 히트 0. k = 1: BusRdX 1, 히트 n−1. b: BusRdX = memrd = k, 히트 n−k. c: BusRd = memrd = k, 히트 n−k (첫 읽기꾼만 E, 둘째가 E→S — 무효화 없음). 3 × 6 × (8+8+4+8) = 504 경우 전부 일치.
   </details>
2. **최소 패딩 찾기.** 코어 4 개가 `addr = c × stride` 의 자기 카운터를 엄격 순환으로 20 라운드 증가시킬 때 c2c 가 0 이 되는 최소 stride 를 시뮬레이터로 찾고, stride 2 의 c2c 를 식으로 예측하라.
   <details><summary>답</summary>

   최소 stride = LINE_WORDS = 4 (실측 c2c: 1 → 79, 2 → 78, 3 → 39, 4 이상 → 0). stride 2 는 라인 0 에 c0·c1, 라인 1 에 c2·c3 → 라인마다 둘이 핑퐁 → 라인당 2R − 1, 합 2(2R − 1) = 78. stride 3 은 주소 0,3,6,9 → 라인 0,0,1,2 → c0·c1 만 공유 → 39.
   </details>
3. **MOESI 돌연변이 두 개를 잡는 directed 트레이스.** `o_no_supply`(O 가 BusRd 에 공급 안 함)와 `o_evict_silent`(O 축출 때 write-back 없음)를 각각 3 사이클 안에 잡는 트레이스를 쓰고, 같은 돌연변이를 MESI 에 넣으면 왜 영원히 안 잡히는지 설명하라.
   <details><summary>답</summary>

   공통 준비: `c0 W 0 1 / sync / c1 R 0` → c0 이 O. `o_no_supply`: `sync / c2 R 0` → c2 가 메모리의 1000 을 읽음 → **DATA, 사이클 3**. `o_evict_silent`: `sync / c0 R 64` (같은 set) → O 가 조용히 사라짐, c1 의 S 는 맞는 값이지만 dirty 책임자가 없음 → **MEM, 사이클 3**. 올바른 MOESI 는 같은 트레이스에서 BusWB 1 번. MESI 에는 O 상태가 없어 그 코드가 절대 실행되지 않는다 → 랜덤 20 시드 0 위반 = 등가 돌연변이 (돌연변이 점수에서 빼야 한다).
   </details>
4. **브로드캐스트 vs 디렉터리.** 패턴 a 와 c 를 n = 2, 4, 8 코어로 10 라운드 돌려, 버스 트랜잭션 × (n−1) = 스누프 조회 수와 "실제로 상태가 바뀌거나 데이터를 낸 캐시 수" (`snoop_act`)를 비교하라.
   <details><summary>답</summary>

   실제 출력: a — n=2: 조회 20 / 필요 19, n=4: 120 / 39, n=8: 560 / 79 → 필요 = 10n − 1 (트랜잭션당 직전 소유자 1 개). c — 2/1, 12/1, 56/1 (첫 E→S 한 번). 브로드캐스트 비용은 n² 로, 필요한 메시지는 n 으로(a) 또는 상수(c)로 자란다 → 디렉터리의 동기. 디렉터리는 대신 요청 → 홈 → 소유자의 간접 hop 을 낸다.
   </details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr      # solution.c 를 포함하므로 같은 폴더에서
```

실제 출력:

```text
== 연습 1: 패턴 공식 vs 시뮬레이터 ==
  504/504 cases match
== 연습 2: 코어별 카운터 간격(워드) vs cache-to-cache 전송 (k=4, 20 라운드) ==
  stride 1 words: c2c = 79
  stride 2 words: c2c = 78
  stride 3 words: c2c = 39
  stride 4 words: c2c = 0
  ...
== 연습 3: MOESI 전용 돌연변이 ==
  o_no_supply    : DATA cycle 3  c2 read 0x00 = 1000, latest write = 1
  o_evict_silent : MEM cycle 3  line 0x00: no M/O owner but memory is stale
  same mutants on MESI, 20 random seeds: 0 violations (equivalent mutants)
== 연습 4: 스누프 조회 수 vs 반응해야 했던 캐시 수 (10 라운드) ==
  pattern               n  txns  broadcast-lookups  needed  needed/lookup
  a shared counter      2    20                 20      19          0.950
  a shared counter      4    40                120      39          0.325
  a shared counter      8    80                560      79          0.141
  c read-only shared    2     2                  2       1          0.500
  c read-only shared    4     4                 12       1          0.083
  c read-only shared    8     8                 56       1          0.018
20/20 passed
```

## 10. 핵심 정리

- 일관성 = 한 주소에 대한 합의: **SWMR + Data-Value**. 순서(서로 다른 주소)는 별개 문제 = 메모리 모델 (ARCH-A05 / MEM-M07).
- 프로토콜 = 표 3 개: 프로세서 쪽(I 읽기 BusRd, I 쓰기 BusRdX=RFO, S/O 쓰기 BusUpgr, E 쓰기 조용히 M), 스누프 쪽(BusRd → 공유로 내려가며 dirty 면 Flush, BusRdX/BusUpgr → I), 요청자(shared 신호로 E/S).
- **E 는 BusUpgr 를, O 는 메모리 write-back 을 아낀다.** MESI 의 M→S 는 메모리를 써야 한다 — 안 쓰고 싶으면 O 가 필요하다.
- 패턴별 연산당 전송: 공유 카운터 ≈ 1, false sharing ≈ 1 (같은 값!), 코어별 라인 → 0, 읽기 공유 → 0. 이 맥에서 1 전송 ≈ 40 ns 라 경합 카운터 ≈ 25 Mops/s (LLAT-B06 24 실측).
- checker 는 매 사이클 + 첫 위반의 사이클. 돌연변이로 checker 를 검증하고, 커버리지(BusWB·c2c 횟수)로 생성기를 검증한다. 등가 돌연변이는 점수에서 뺀다.
- 업그레이드 경쟁: 대기 중 무효화되면 BusUpgr → BusRdX. 비원자적 인터커넥트에서는 과도 상태로 다룬다 — 실리콘 버그 단골.
- 큰 칩은 디렉터리/스누프 필터: 브로드캐스트 조회 n−1 대신 필요한 캐시에만. 대가는 간접 hop.

## 참고자료

- V. Nagarajan, D. J. Sorin, M. D. Hill, D. A. Wood, *A Primer on Memory Consistency and Cache Coherence*, 2nd ed., Morgan & Claypool, 2020 — 2 장 일관성 정의(SWMR, Data-Value Invariant), 7 장 스누핑 프로토콜(과도 상태 표기), 8 장 디렉터리 (장 번호는 2판 목차 기준).
- D. E. Culler, J. P. Singh, A. Gupta, *Parallel Computer Architecture: A Hardware/Software Approach*, 1999 — 5 장 스누핑 일관성: MSI / MESI(Illinois), BusRd · BusRdX · BusUpgr · Flush 명명.
- J. L. Hennessy, D. A. Patterson, *Computer Architecture: A Quantitative Approach*, 6th ed., 5 장 Thread-Level Parallelism (스누핑·디렉터리 프로토콜).
- M. S. Papamarcos, J. H. Patel, "A Low-Overhead Coherence Solution for Multiprocessors with Private Cache Memories", ISCA 1984 — Illinois(= MESI) 프로토콜.
- P. Sweazey, A. J. Smith, "A Class of Compatible Cache Consistency Protocols and their Support by the IEEE Futurebus", ISCA 1986 — MOESI 클래스.
- J. R. Goodman, H. H. J. Hum, "MESIF: A Two-Hop Cache Coherency Protocol for Point-to-Point Interconnects", 2004/2009 기술 보고서 — F 상태 (세부 미검증).
- AMD64 Architecture Programmer's Manual Vol. 2: System Programming — 캐시 일관성의 MOESI 설명 (절 번호 확인 필요).
- Arm, *AMBA AXI and ACE Protocol Specification* (IHI 0022) / *AMBA CHI Architecture Specification* (IHI 0050) — UniqueDirty / UniqueClean / SharedDirty / SharedClean / Invalid 상태, DCT (세부 확인 필요).
- 이 노트의 관련 레슨: ARCH-M08 (false sharing stride 실측), MEM-M04 (일관성 트래픽 vs 코어 수 실측 — 패턴 a/b/c 비교 대상), ARCH-M12 (non-coherent DMA), ARCH-A05 · MEM-M07 (메모리 모델), LLAT-M05 (핑퐁 지연, 128 B), LLAT-B06 (원자 연산 비용), OS-M07 (스핀락과 일관성 트래픽), ARM-M04 (LL/SC vs LSE), SOCX-B03 (AXI exclusive monitor).
