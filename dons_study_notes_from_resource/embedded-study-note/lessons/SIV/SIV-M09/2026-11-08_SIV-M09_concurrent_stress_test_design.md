---
id: SIV-M09
time: 95분
updated: 2026-11-08
---
# SIV-M09 동시성 스트레스 테스트 설계

## 학습 목표

- CPU memtest · DMA · 인터럽트 소스를 **동시에** 돌리는 스트레스 하네스를 pthread로 만들고, 트래픽마다 **독립 checker**(소유자 시그니처 / 태그 순서 + CRC32 / 시퀀스 ack)를 붙인다.
- 공유 자원 경쟁(완료 큐 티켓, W1C 상태 레지스터의 read-modify-write)이 **단독 테스트에서는 안 보이고 동시 실행에서만** 드러나는 이유를 인터리빙 전수 탐색으로 설명한다.
- 간헐적인 race를 **시드 고정 토큰 스케줄러**로 결정적으로 재현·리플레이하고, 단일 스레드 hook으로 정확한 인터리빙을 강제해 단위 테스트로 고정한다.
- "n번 돌려서 안 나왔다"의 통계적 의미(검출 확률, 95% 신뢰에 필요한 실행 수, 0 실패의 상한)를 계산하고, 이벤트 속도·race window 넓히기가 검출 시간을 얼마나 줄이는지 실측한다.
- TSan이 잡는 것(data race)과 못 잡는 것(원자적 접근으로 만든 atomicity violation)을 구분하고, 실패 한 줄에 "어디를 보라"까지 담는 리포트를 만든다.

## 1. 문제

first silicon 랩에서 가장 비싼 버그는 "블록마다 단독 테스트는 다 통과하는데 시스템을 다 켜면 몇 시간에 한 번 죽는" 버그다 — NVIDIA·Apple·Rivian 실리콘 검증 면접의 "concurrency stress test를 설계해 보라"는 질문이 정확히 이것을 묻는다.

단독 테스트(SIV-B03~B11)는 한 번에 한 마스터만 버스를 쓴다. 실제 칩에서는 CPU 코어, DMA 엔진, 인터럽트가 **같은 메모리·같은 상태 레지스터·같은 큐**를 동시에 쓴다. 그래서 검증 엔지니어는 ① 트래픽을 동시에 최대한 세게 걸고 ② 트래픽마다 **서로 독립인 checker**를 붙여 "누가 누구를 망가뜨렸는지"를 알아내고 ③ 실패를 재현 가능하게 만든다. 이 레슨은 그 하네스를 host에서 pthread로 만든다.

가상 SoC **KES1** (SIV-B02의 mock을 축약해 넣었다; 버스 접근은 워드 단위 원자적이라고 보고 `_Atomic uint32_t` + relaxed로 모델링):

| agent (스레드) | 하는 일 | 공유 자원 | 독립 checker |
|---|---|---|---|
| CPU0, CPU1 | 자기 영역 32워드에 시그니처 쓰기 → 다시 읽어 비교 (memtest) | DDR | **소유자 시그니처**: `[31:24]` = 쓴 agent (C0/C1/D0/D1) |
| DMA0, DMA1 | 소스 블록 준비 → 8워드 복사 + 엔진 CRC32 → 완료 큐(CQ)에 엔트리 → `INT_STS.DONEx` | DDR, CQ, INT_STS | ISR이 **태그 순서**(채널별 seq) + **CRC32**(엔진 값, dst 메모리) |
| TMR | `TMR_SEQ = n` → `INT_STS.TMR` → ISR의 ack를 기다림 | INT_STS | **seq ack**: 안 오면 LOST, 이미 ack한 seq면 SPURIOUS |
| ISR | `INT_STS`를 W1C로 받아 TMR ack, CQ drain | INT_STS, CQ | (위 두 checker를 실행) |

```svg
<svg viewBox="0 0 760 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m09a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">KES1 스트레스 하네스: 트래픽 4종 · 공유 자원 3개 · 독립 checker 3개</text>
  <rect x="10" y="40" width="160" height="60" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="20" y="64" fill="currentColor" font-weight="bold">CPU0 · CPU1</text>
  <text x="20" y="86" fill="currentColor" font-size="12">memtest (자기 영역)</text>
  <rect x="10" y="130" width="160" height="60" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="20" y="154" fill="currentColor" font-weight="bold">DMA0 · DMA1</text>
  <text x="20" y="176" fill="currentColor" font-size="12">복사 + 엔진 CRC32</text>
  <rect x="10" y="220" width="160" height="50" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="20" y="242" fill="currentColor" font-weight="bold">TMR</text>
  <text x="20" y="262" fill="currentColor" font-size="12">seq++ → 상태 비트</text>
  <rect x="240" y="40" width="280" height="70" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="250" y="60" fill="currentColor" font-weight="bold">DDR (g_mem)</text>
  <g stroke="currentColor" font-size="12">
    <rect x="250" y="72" width="44" height="26" class="f-mut"/><rect x="294" y="72" width="44" height="26" class="f-mut"/>
    <rect x="338" y="72" width="44" height="26" class="f-mut"/><rect x="382" y="72" width="44" height="26" class="f-mut"/>
    <rect x="426" y="72" width="44" height="26" class="f-ok"/><rect x="470" y="72" width="44" height="26" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="272" y="90">SRC0</text><text x="316" y="90">SRC1</text><text x="360" y="90">DST0</text>
    <text x="404" y="90">DST1</text><text x="448" y="90">CPU0</text><text x="492" y="90">CPU1</text>
  </g>
  <rect x="240" y="140" width="280" height="50" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="250" y="162" fill="currentColor" font-weight="bold">완료 큐 CQ (4 슬롯)</text>
  <text x="250" y="180" fill="currentColor" font-size="12">티켓 wr 하나를 DMA0·DMA1이 공유</text>
  <rect x="240" y="215" width="280" height="55" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="250" y="237" fill="currentColor" font-weight="bold">INT_STS (W1C)</text>
  <text x="250" y="257" fill="currentColor" font-size="12">DONE0 · DONE1 · TMR — 세 생산자 + ISR</text>
  <g class="f-warn" stroke="currentColor">
    <circle cx="426" cy="66" r="11"/><circle cx="505" cy="160" r="11"/><circle cx="505" cy="236" r="11"/>
  </g>
  <g fill="currentColor" font-weight="bold" text-anchor="middle">
    <text x="426" y="71">3</text><text x="505" y="165">1</text><text x="505" y="241">2</text>
  </g>
  <rect x="580" y="40" width="170" height="60" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="590" y="62" fill="currentColor" font-weight="bold">CPU checker</text>
  <text x="590" y="84" fill="currentColor" font-size="12">소유자 시그니처</text>
  <rect x="580" y="130" width="170" height="60" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="590" y="152" fill="currentColor" font-weight="bold">ISR</text>
  <text x="590" y="174" fill="currentColor" font-size="12">W1C 받기 · CQ drain</text>
  <rect x="580" y="215" width="170" height="55" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="590" y="237" fill="currentColor" font-size="12">DMA: 태그 순서 + CRC</text>
  <text x="590" y="257" fill="currentColor" font-size="12">IRQ: seq ack / spurious</text>
  <g stroke="currentColor" fill="none" marker-end="url(#m09a)">
    <path d="M170 70 L238 70"/><path d="M170 150 L238 100"/><path d="M170 165 L238 165"/>
    <path d="M170 182 L238 232"/><path d="M170 248 L238 248"/>
    <path d="M520 70 L578 70"/><path d="M520 165 L578 160"/><path d="M520 242 L578 180"/><path d="M665 190 L665 213"/>
  </g>
  <text x="10" y="300" fill="currentColor" font-size="12">1 cq_claim: 티켓 wr 를 load → store 두 번으로 증가 (DMA0·DMA1 경쟁)</text>
  <text x="10" y="320" fill="currentColor" font-size="12">2 irq_rmw: INT_STS 를 읽고-OR-쓰기 (DMA0·DMA1·TMR 의 set, ISR 의 W1C 와 경쟁)</text>
  <text x="10" y="340" fill="currentColor" font-size="12">3 dma_overrun: DMA1 쓰기 꼬리 beat 가 한 워드 더 → DST1 끝 바로 뒤 CPU0[0] (0x80000400)</text>
</svg>
```

메모리 맵 (워드 인덱스, 바이트 주소 = `0x8000_0000 + 4*idx`): SRC0 `0x000` · SRC1 `0x040` · DST0 `0x080` · DST1 `0x0C0` (8워드 블록 × 8) · CPU0 `0x100` · CPU1 `0x120` (각 32워드). DST1 끝 바로 뒤가 CPU0라는 게 결함 3의 무대다.

일부러 넣은 결함 3종 (`run_cfg_t.bug`, 결함 코드는 주어진 부분에 있다):

| 결함 | 코드 | 실제 칩/펌웨어에서 같은 모양 |
|---|---|---|
| `cq_claim` | `w = load(wr); …; store(wr, w+1)` | 두 DMA 채널(또는 두 코어)이 공유 링 인덱스를 lock 없이 증가, doorbell 카운터 RMW |
| `irq_rmw` | `v = load(STS); …; store(STS, v \| bit)` | 공유 SRAM의 소프트웨어 mailbox/상태 워드를 여러 마스터가 RMW, Linux `__set_bit`(비원자) 오용, W1C를 RMW로 쓰는 드라이버 |
| `dma_overrun` | 마지막 beat 뒤 `mem_wr(dst+n, v)` | 쓰기 strobe/길이 카운터 off-by-one. 읽기 쪽 CRC는 정상이라 DMA 자체 테스트로는 안 보임 |

실행 모드 두 가지:

- **det**: 진짜 스레드 6개지만 토큰을 가진 스레드 하나만 달린다. 각 agent가 `yield_point()`에서 토큰을 내놓으면 시드 고정 xorshift가 다음 스레드를 고른다 → 시드가 같으면 인터리빙·실패·step 번호까지 같다. 채점 테스트는 이 모드.
- **free**: 진짜 동시 실행. `./solution --stress <bug> --seconds N [--period-us P] [--window W]` — 장시간 실행용 CLI (채점 밖).

### Part A — 트래픽 checker와 공유 자원 프로토콜

- `cpu_agent_pass(me, iter)`: 자기 영역 32워드에 `sig_word(me, iter, i)`를 쓰고 다시 읽어 비교. 틀린 워드의 최상위 바이트가 다른 agent면 `FOREIGN_WRITE`("written by DMA1"), 아니면 `CORRUPT`.
- `cq_push`(claim 이후 부분) / `cq_peek` / `cq_release`: 슬롯별 seq로 소유권을 넘기는 MPSC 큐. 빈 슬롯은 `seq == 티켓`, 찬 슬롯은 `seq == 티켓+1`. publish는 release, 소비는 acquire. 기다림이 예산을 넘으면 `HANG`.
- `irq_take()`: `INT_STS`를 읽고, **읽은 비트만** W1C로 지운다.
- `dma_check_entry(e)`: 태그 → (채널, seq). 순서가 앞서면 `MISSING`, 뒤면 `DUP`, 엔진 CRC가 생성기 기대값과 다르면 `CRC_ENGINE`, dst 메모리 CRC가 다르면 첫 번째 틀린 워드 주소로 `CRC_DST`.
- 끝에 det 모드로 6스레드 전체를 돌려 정상 모델이 깨끗하게 통과하는지 확인.

### Part B — 경쟁을 결정적으로: 전수 탐색 · hook 재현 · 통계

- `explore_rec`: k개 agent가 공유 워드에 자기 비트를 세우는 프로그램(원자적 1단계 / RMW 2단계)의 **모든 인터리빙**을 펼쳐 갱신 유실 스케줄 수를 센다.
- 단일 스레드 hook으로 `cq_claim`·`irq_rmw`의 race window에 다른 agent를 정확히 끼워 넣어, 유실·HANG·spurious를 결정적으로 재현 (정상 모델은 같은 끼어들기에 멀쩡).
- `p_detect(p, n)`, `runs_needed(p, cl)`: 실행 1회 검출 확률 p일 때 n회 안에 검출할 확률, 신뢰도 cl에 필요한 실행 수.
- det 리플레이: 처음 검출된 시드를 다시 돌리면 같은 실패가 같은 step에서 나와야 한다.

### Part C — 야간 회귀: 결함 × 트래픽 조합 캠페인과 리포트

- `isr_final()`: 생산자가 다 끝난 뒤 마지막 판정 — CQ에 엔트리가 남았는데 DONE 비트가 없었다면 `LOST_IRQ`, 채널별 생산 수 ≠ 검사 수면 `MISSING`.
- `fail_format()`: `FAIL @step=… checker=… kind=… addr=… exp=… act=… diff=… | detail | suspect: …` 한 줄.
- 캠페인: 결함 4종(none 포함) × 트래픽 조합 6개(CPU / DMA1 / TMR / DMA0+1 / DMA+TMR / ALL) × 시드 8개. 단독 조합은 0, ALL은 셋 다 검출, 검출되면 기대한 checker·종류여야 한다.
- free 모드로 정상 모델 실제 동시 실행 (DMA 3000+3000, TMR 300) → 통과, TSan 깨끗.

## 2. Problem (English)

> **Design a host-side concurrency stress harness for an SoC validation lab (C11 + pthreads).**
>
> Agents run as threads: two CPU memtest agents (own regions), two DMA channels that copy 8-word blocks, compute an engine-side CRC-32 and post completions into a *shared* 4-entry completion queue, a timer that bumps `TMR_SEQ` and raises a status bit, and an ISR that takes `INT_STS` (write-1-to-clear) and drains the queue.
>
> 1. Give every traffic type an **independent checker**: CPU data carries an owner signature in bits `[31:24]` so a corrupted word names its writer; DMA completions are checked for per-channel order (missing / duplicate), engine CRC and destination-memory CRC (report the first bad word address); timer events must be acknowledged exactly once (lost / spurious).
> 2. Implement the queue (`cq_push` after the ticket claim, `cq_peek`, `cq_release`) with per-slot sequence numbers and release/acquire publication; a producer that waits past its budget reports `HANG`. Implement `irq_take()` with correct W1C semantics.
> 3. Three planted defects — non-atomic ticket claim, read-modify-write of `INT_STS`, and a DMA write-tail overrun — must stay invisible when each traffic runs alone and be detected, with the right checker and failure kind, when traffic runs concurrently.
> 4. Make it reproducible: a seeded token scheduler (`det` mode) must replay the same failure at the same step for the same seed. Enumerate all interleavings of k racing RMW agents and count lost updates. Compute `p_detect(p, n) = 1-(1-p)^n` and the minimum runs for confidence `cl`.
> 5. Report one actionable line per failure, e.g. `FAIL @step=459 checker=CPU0 kind=FOREIGN_WRITE addr=0x80000400 exp=0xC0F5EC4E act=0xD10FD56D … | iter=1 word=0 written by DMA1 | suspect: …`
>
> Checked tests must be deterministic and finish in seconds; long free-running stress is a CLI mode. The good harness must be ThreadSanitizer-clean.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter -lm && ./starter
```

처음 실행하면 (실제 출력, 가운데 생략):

```text
FAIL starter.c:622  cpu_agent_pass(A_CPU0, 0) == 0 && mem_rd(CPU0_BASE + 5) == sig_word(A_CPU0, 0, 5) && atomic_load(&g_nfail) == 0
FAIL starter.c:623  cpu_agent_pass(A_CPU1, 1) == 0 && mem_rd(CPU1_BASE + CPU_W - 1) == sig_word(A_CPU1, 1, CPU_W - 1)
...
FAIL starter.c:633  cq_push(A_DMA0, TAG(0, 0), BLK, 0x11u) && cq_push(A_DMA1, TAG(1, 0), BLK, 0x22u)
...
[A] det good run: steps=1610 cpu iters 375/397, dma checked 0/0, tmr acked 0, irqs 0 (empty 0)
FAIL starter.c:688  x2.schedules == 6 && x2.lost == 4
...
[C] detection matrix (det mode, seeds 1..8, 0.10 s)
    bug \ cfg        CPU    DMA1     TMR  DMA0+1 DMA+TMR     ALL
    none             0/8     0/8     8/8     0/8     8/8     8/8
    cq_claim         0/8     0/8     8/8     0/8     8/8     8/8
    irq_rmw          0/8     0/8     8/8     0/8     8/8     8/8
    dma_overrun      0/8     0/8     8/8     0/8     8/8     8/8
...
[C] free good run: dma 0+0 checked, tmr 0 acked, cpu iters 1761244451+1758962506, irqs 0 (empty 0)
7/50 passed
```

주어진 것: SoC 모델·결함 스위치(`sts_set`, `cq_claim`, `dma_copy`), det 토큰 스케줄러, agent 스레드 함수, `run_system`, stress CLI, 채점 하네스. TODO는 Part A 6개(`cpu_agent_pass`, `cq_push` 뒷부분, `cq_peek`, `cq_release`, `irq_take`, `dma_check_entry`), Part B 3개(`explore_rec`, `p_detect`, `runs_needed`), Part C 2개(`isr_final`, `fail_format`). 스텁 상태에서도 모든 대기 루프에 예산이 있어 멈추지 않는다 (TMR이 ack를 못 받아 free 실행에서 2초 timeout).

## 4. 힌트

<details>
<summary>힌트 1 — checker는 "기대값을 메모리가 아니라 정의에서" 만든다</summary>

CPU checker는 `sig_word(me, iter, i)`, DMA checker는 `crc_expected(ch, seq)`(생성기 정의로 계산)와 비교한다. dst 메모리에서 CRC를 계산해 엔진 CRC와만 비교하면, 소스 준비가 틀렸을 때 둘 다 같은 틀린 값이라 통과해 버린다. 기대값의 출처가 검사 대상과 독립이어야 "독립 checker"다.

</details>

<details>
<summary>힌트 2 — 슬롯 seq 하나로 "누구 차례인지"를 표현한다</summary>

슬롯 i의 seq 초깃값은 i. 티켓 w를 받은 생산자는 `seq == w`가 될 때까지 기다리고(acquire), 엔트리를 쓰고, `seq = w+1`(release). 소비자는 `seq == rd+1`이면 읽고, 검사가 끝나면 `seq = rd + CQ_SIZE`(release) — 다음 바퀴 티켓의 "빈 슬롯" 표시다. 순서가 바뀌면(seq를 먼저 publish) 소비자가 덜 쓴 엔트리를 읽는다.

</details>

<details>
<summary>힌트 3 — W1C는 "읽은 비트만" 지운다</summary>

`s = load(STS); fetch_and(STS, ~s)`. `store(STS, 0)`으로 지우면 load와 store 사이에 선 새 비트가 같이 사라진다. 실제 하드웨어 W1C 레지스터에 `s`를 쓰는 것과 같은 의미다.

</details>

<details>
<summary>힌트 4 — 인터리빙 탐색은 "아직 안 끝난 agent 중 하나를 골라 한 단계" 재귀</summary>

상태 = (공유값 x, 각 agent의 pc와 레지스터). 매 단계 pc < len인 agent마다 한 단계 실행해 재귀하고 되돌린다(backtracking). 아무도 못 움직이면 스케줄 하나 끝 — 이때 x가 모든 비트를 가졌는지 본다.

</details>

<details>
<summary>힌트 5 — 확률은 log1p로</summary>

n회 모두 못 잡을 확률 (1-p)^n = exp(n·ln(1-p)). p가 1e-6이면 `log(1-p)`는 1-p를 double로 반올림하면서 정밀도를 잃는다 → `log1p(-p)`. 필요한 n = ⌈ln(1-cl) / ln(1-p)⌉.

</details>

## 5. 풀이

### Part A — 독립 checker 세 가지와 공유 자원 프로토콜

**왜 "독립"이어야 하나.** 스트레스에서 실패는 대개 *다른* 트래픽이 일으킨다. checker가 자기 트래픽의 결과만 보면 "CPU0 memtest 실패"까지만 알고, 범인은 모른다. 그래서 데이터 자체에 출처를 새긴다.

| checker | 기대값 출처 | 잡는 것 | 보고 |
|---|---|---|---|
| CPU 시그니처 | `sig_word(agent, iter, idx)` = `owner<<24 \| hash24` | 다른 마스터의 쓰기(FOREIGN), 자기 데이터 비트 뒤집힘(CORRUPT) | 주소, 기대/실제, **쓴 agent 이름** |
| DMA 태그 순서 | 채널별 다음 seq (`g_next[ch]`) | 완료 유실(MISSING), 중복·stale(DUP) | 기대 태그 vs 받은 태그 |
| DMA CRC | `crc_expected(ch, seq)` (정의에서 계산) | 엔진 읽기 경로·찢어진 엔트리(CRC_ENGINE), 쓰기 경로·덮어쓰기(CRC_DST) | 첫 번째 틀린 워드 주소 |
| IRQ ack | TMR이 seq n을 올리고 ack를 기다림 | 상태 비트 유실(LOST), 지운 비트 부활(SPURIOUS) | seq, 당시 INT_STS |

실제 랩에서는 memtest 패턴에 코어 ID·반복 번호를 넣고, DMA는 하드웨어 CRC 유닛이나 descriptor의 체크섬 필드, 인터럽트는 per-source 카운터나 타임스탬프 레지스터로 같은 일을 한다. 핵심은 **실패한 워드 하나만 보고도 "누가 언제 썼는지"를 알 수 있게 데이터 형식을 설계**하는 것이다.

**완료 큐 프로토콜.** 슬롯 seq가 소유권 토큰이다.

| 슬롯 seq | 의미 | 누가 다음에 움직이나 |
|---|---|---|
| `w` | 비어 있음, 티켓 w의 생산자 차례 | 생산자: 엔트리 쓰고 `seq = w+1` (release) |
| `w+1` | 찼음 | 소비자(rd = w): 읽고 검사 → `seq = w + CQ_SIZE` (release) |
| `w + CQ_SIZE` | 다음 바퀴 티켓의 빈 슬롯 | 티켓 w+4의 생산자 |

release/acquire가 하는 일: 생산자의 엔트리 쓰기(평범한 `sl->e = …`)와 dst 메모리 쓰기(relaxed)가 `seq` release store보다 먼저 보이도록 보장하고, 소비자가 `seq`를 acquire로 읽으면 그 앞의 쓰기가 전부 보인다. 그래서 엔트리 필드는 `_Atomic`이 아니어도 data race가 아니다(TSan도 그렇게 판단한다 — 아래 TSan 결과).

**슬롯은 검사가 끝난 뒤에 돌려준다.** 채널은 블록 8개를 `seq % 8`로 재사용한다. 소비자가 dst 블록 CRC를 다 읽기 전에 슬롯을 돌려주면, 생산자가 다음 디스크립터를 들고 와 같은 블록을 덮어쓸 수 있다 → 정상 모델에서 가짜 `CRC_DST`. 검사 후 release이면: 채널이 seq s를 복사하기 시작할 때 s-1이 이미 push됐고, 그러려면 티켓 `w(s-1) - 4` 이하가 소비·검사 완료여야 한다. 같은 채널 티켓은 증가하므로 `w(s-8) ≤ w(s-1) - 7 < w(s-1) - 4` → seq s-8(같은 블록)의 검사는 끝났다. 그래서 `CQ_SIZE ≤ NBLK - 1`이 조건이다. 실제 DMA 드라이버의 "완료 처리 전에 버퍼를 재사용하지 마라"와 같은 규칙이다.

**W1C.** `irq_take`는 `s = load; fetch_and(~s)`. ISR이 `store(0)`으로 지우면 그 사이 DMA가 세운 DONE이 사라진다 — 이것도 lost interrupt지만 *정상 모델*에서 생기는 하네스 버그라 checker가 결함 모델과 구분할 수 없게 된다. 하네스 자신이 깨끗해야 결함을 결함이라고 말할 수 있다.

**DMA checker 판정표** (하네스의 표 그대로):

| 엔트리 | 기대 next | 판정 | 이유 |
|---|---|---|---|
| ch0 seq0, CRC 정상 | 0 | NONE | next → 1 |
| ch0 seq3 | 2 | MISSING | seq 2가 안 왔다 |
| ch0 seq1 (두 번째) | 2 | DUP | 이미 본 seq |
| ch0 seq2, 엔진 CRC ^1 | 2 | CRC_ENGINE | 엔진이 보고한 CRC가 정의와 다름 |
| ch0 seq2, dst word 5 ^0x40 | 2 | CRC_DST | `addr=0x80000254` = `0x8000_0000 + 4*(0x80 + 2*8 + 5)` |

det 모드 정상 실행 결과 (실제 출력): 

```text
[A] det good run: steps=867 cpu iters 3/3, dma checked 16/16, tmr acked 12, irqs 39 (empty 1)
```

`empty 1` = DONE 비트를 받았는데 CQ가 비어 있던 경우. 생산자가 "publish → 비트 set" 순서라, ISR이 publish된 엔트리를 이전 인터럽트에서 미리 drain하고 비트만 나중에 받을 수 있다. 정상이므로 실패가 아니라 카운트만 한다 (TMR은 ack를 기다리므로 같은 상황이 생기지 않고, 그래서 TMR의 "비트는 있는데 새 seq가 없음"은 SPURIOUS 실패다).

### Part B — 경쟁을 결정적으로

**인터리빙 전수 탐색.** RMW set = `LOAD r ← x` 와 `STORE x ← r|bit` 두 단계. 두 agent면 스케줄 4!/(2!·2!) = 6개:

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">A, B 가 각자 RMW 로 비트를 세울 때 가능한 6가지 순서 (x 시작값 0)</text>
  <g fill="currentColor" font-weight="bold"><text x="10" y="52">순서</text><text x="330" y="52">최종 x</text><text x="430" y="52">결과</text></g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="80">rA wA rB wB</text><text x="10" y="108">rB wB rA wA</text><text x="10" y="136">rA rB wA wB</text>
    <text x="10" y="164">rA rB wB wA</text><text x="10" y="192">rB rA wA wB</text><text x="10" y="220">rB rA wB wA</text>
    <text x="330" y="80">a|b</text><text x="330" y="108">a|b</text><text x="330" y="136">b</text>
    <text x="330" y="164">a</text><text x="330" y="192">b</text><text x="330" y="220">a</text>
  </g>
  <g stroke="currentColor">
    <rect x="425" y="66" width="90" height="20" class="f-ok"/><rect x="425" y="94" width="90" height="20" class="f-ok"/>
    <rect x="425" y="122" width="90" height="20" class="f-warn"/><rect x="425" y="150" width="90" height="20" class="f-warn"/>
    <rect x="425" y="178" width="90" height="20" class="f-warn"/><rect x="425" y="206" width="90" height="20" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12"><text x="435" y="81">OK</text><text x="435" y="109">OK</text>
    <text x="435" y="137">a 유실</text><text x="435" y="165">b 유실</text><text x="435" y="193">a 유실</text><text x="435" y="221">b 유실</text></g>
  <text x="540" y="80" fill="currentColor" font-size="12">구간 [r, w] 가 겹치면</text>
  <text x="540" y="100" fill="currentColor" font-size="12">늦게 쓴 쪽이 먼저 쓴</text>
  <text x="540" y="120" fill="currentColor" font-size="12">쪽의 비트를 덮는다.</text>
  <text x="540" y="160" fill="currentColor" font-size="12">원자적 set (ldsetal):</text>
  <text x="540" y="180" fill="currentColor" font-size="12">한 단계라 겹칠 구간이</text>
  <text x="540" y="200" fill="currentColor" font-size="12">없다 → 2가지 순서 모두 OK</text>
</svg>
```

일반화하면: 구간 [r, w]가 하나라도 겹치면 한 쪽이 다른 쪽의 w를 품게 되어 유실이 생긴다. 유실이 없으려면 모든 구간이 서로 겹치지 않아야 하고(= agent를 직렬로 세운 순서), 그런 스케줄은 k!개. 전체는 (2k)!/2^k개. 하네스 결과(실제 출력):

```text
[B] interleavings: rmw k=2 4/6 lost, k=3 84/90 lost; atomic k=3 0/6 lost
```

k=3이면 90개 중 84개(93%)가 유실 — "확률적으로 드물다"는 직관은 *window가 짧아서* 생기는 것이지, 겹치기만 하면 거의 항상 틀린다. 이게 CHESS·loom 같은 **systematic concurrency testing**(가능한 스케줄을 체계적으로 탐색)의 출발점이다. 실제 프로그램은 스케줄 수가 폭발하므로 preemption 횟수를 2~3개로 제한해 탐색한다.

**원자적 접근 두 번 ≠ 원자적 RMW.** 같은 C 코드를 이 맥(arm64)과 Cortex-M4로 컴파일한 결과 (`cc -O2 -S`, `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S`, 실제 출력에서 지시어 줄을 지우고 두 열로 나란히 배치, M4 쪽 짧은 줄은 `;` 로 이어 붙임):

```text
_set_atomic:  (arm64)                  set_atomic:  (Cortex-M4)
  ldsetal  w0, w8, [x8]                  dmb sy
  ret                                  .LBB0_1:
_set_rmw:                                ldrex r2, [r1]
  ldar     w9, [x8]                      orrs  r2, r0
  orr      w9, w9, w0                    strex r3, r2, [r1]
  stlr     w9, [x8]                      cmp r3, #0 ; bne .LBB0_1
  ret                                    dmb sy
                                       set_rmw:
                                         ldr r2, [r1] ; dmb sy ; orrs r0, r2 ; dmb sy ; str r0, [r1] ; dmb sy
```

`set_rmw`의 load와 store는 *각각* 원자적(seq_cst — `ldar`/`stlr`, M4는 `dmb`로 감쌈)이지만 그 사이 `orr`에서 다른 마스터가 끼어들 수 있다. ARMv8.1 LSE의 `ldsetal`은 메모리 쪽에서 한 번에 OR한다. M4는 LDREX/STREX 루프로 "중간에 누가 건드렸으면 다시". **하드웨어 레지스터에는 LDREX/STREX가 안 통하는 경우가 많다**(exclusive monitor는 보통 일반 메모리용, 디바이스 영역은 구현 정의) — 그래서 실제 칩은 SET/CLR 별칭 레지스터, W1C/W1S, 비트밴드 같은 *하드웨어 원자 연산*을 제공한다 (C-M02 참고).

**hook으로 race를 단위 테스트에 고정.** free/hook 모드의 `yield_point`는 `g_hook(me, site)`를 부른다. `race_window()`가 결함 코드의 load와 store 사이에 있으니, 테스트가 그 지점에서 다른 agent의 연산을 *동기적으로* 실행해 정확한 인터리빙을 만든다.

| 테스트 | 끼어드는 것 | 결과 (CHECK) |
|---|---|---|
| `cq_claim`: DMA0 claim 창에 DMA1의 `cq_push` 전체 | DMA1이 티켓 0으로 publish | DMA0도 티켓 0 → 슬롯 seq=1이라 영영 못 씀 → `HANG agent=DMA0`, `wr == 1`, CQ에는 DMA1 것만 |
| 정상 claim, 같은 끼어들기 (claim 직후 지점) | DMA1은 티켓 1 | 두 엔트리 다 순서대로 |
| `irq_rmw`: DMA0 RMW 창에 TMR set | TMR이 STS=TMR | DMA0가 `0\|DONE0` 저장 → `irq_take() == DONE0`, **TMR 유실** |
| `irq_rmw`: DMA0 RMW 창에 ISR이 TMR 처리 | ISR이 W1C + ack | DMA0가 `TMR\|DONE0` 저장 → 지운 TMR 부활 → `SPURIOUS` |
| 정상 set | — | hook 자체가 안 불린다 (window가 없다) |

**det 토큰 스케줄러.** 스레드는 진짜지만, `yield_point()`마다 mutex를 잡고 `xorshift(seed)`로 다음 차례를 뽑아 그 스레드의 condvar만 깨운다. 스레드마다 condvar가 따로라 깨울 때 한 번의 신호로 끝나고(thundering herd 없음), `while (turn != me) wait` 로 spurious wakeup도 안전하다. 시간에 의존하는 결정이 없으니(대기 예산도 *poll 횟수*) 같은 시드 = 같은 실행이다.

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">det 모드: 토큰 하나를 시드 고정 난수로 넘긴다 (한 칸 = yield_point 사이 한 구간)</text>
  <g fill="currentColor" font-size="12"><text x="10" y="56">DMA0</text><text x="10" y="90">DMA1</text><text x="10" y="124">TMR</text><text x="10" y="158">ISR</text></g>
  <g stroke="currentColor" opacity=".35"><path d="M60 52 L740 52"/><path d="M60 86 L740 86"/><path d="M60 120 L740 120"/><path d="M60 154 L740 154"/></g>
  <g stroke="currentColor">
    <rect x="70" y="40" width="70" height="24" class="f-acc"/><rect x="150" y="108" width="70" height="24" class="f-acc"/>
    <rect x="230" y="40" width="70" height="24" class="f-warn"/><rect x="310" y="142" width="70" height="24" class="f-acc"/>
    <rect x="390" y="40" width="70" height="24" class="f-warn"/><rect x="470" y="74" width="70" height="24" class="f-acc"/>
    <rect x="550" y="142" width="70" height="24" class="f-ok"/><rect x="630" y="108" width="70" height="24" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="76" y="57">copy</text><text x="156" y="125">seq=5</text><text x="236" y="57">ld STS</text><text x="316" y="159">W1C TMR</text>
    <text x="396" y="57">st STS</text><text x="476" y="91">copy</text><text x="556" y="159">TMR?</text><text x="636" y="125">wait ack</text>
  </g>
  <text x="10" y="196" fill="currentColor" font-size="12">DMA0 의 ld STS(TMR 포함)와 st STS 사이에 ISR 이 TMR 을 지움</text>
  <text x="10" y="216" fill="currentColor" font-size="12">→ DMA0 가 옛 값 | DONE0 을 써서 TMR 부활 → ISR: SPURIOUS</text>
  <text x="10" y="236" fill="currentColor" font-size="12">같은 시드 = 같은 순서. 실패 step 번호가 그대로 재현 좌표가 된다.</text>
</svg>
```

리플레이 결과 (실제 출력):

```text
[B] irq_rmw first detected at seed 1 (SPURIOUS_IRQ by ISR), replay identical (steps=231)
    FAIL @step=115 checker=ISR kind=SPURIOUS_IRQ exp=0x00000006 act=0x00000005 diff=0x00000003 | INT_STS.TMR set but TMR_SEQ still 5 (already acked) | suspect: status bit re-set after W1C: stale RMW write-back
```

실제 랩의 대응물: 시뮬레이션/에뮬레이션은 원래 결정적이고, 실리콘에서는 같은 시드로 트래픽 패턴과 시작 순서를 고정하고 trace(ETM/로직 애널라이저)로 순서를 남긴다. 소프트웨어 쪽에는 `rr`의 chaos mode, CHESS, loom 같은 도구가 있다. **"재현되는 실패"는 "고칠 수 있는 실패"다.**

**검출 통계.** 실행 1회에 버그가 드러날 확률이 p이고 실행이 독립이면, n회 모두 놓칠 확률은 (1-p)^n. 신뢰도 cl로 "한 번은 잡는다"를 원하면

  (1-p)^n ≤ 1-cl  →  n ≥ ln(1-cl) / ln(1-p)  ≈ -ln(1-cl)/p = 3.0/p  (cl = 95%, p 작을 때)

| p (1회 검출 확률) | 95% 필요 실행 수 | 99% |
|---|---|---|
| 0.5 | 5 | 7 |
| 0.01 | 299 | 459 |
| 0.001 | 2,995 | 4,603 |
| 1e-6 | 2,995,731 | 4,605,168 |

(95% 열은 하네스 CHECK 값, 99% 열은 같은 식으로 계산.) 거꾸로 말하면 **깨끗한 밤 n번은 "p < 3/n" 이상을 말해 주지 못한다** (rule of three, 연습 2). 시간으로 바꾸면 검출까지의 평균 시간(MTTD)이 T일 때 95% 확신에는 약 3T의 soak가 필요하다.

**free 모드 실측 — 이벤트 속도와 window 넓히기.** 같은 바이너리를 `--stress`로 120초 제한, 5개 시드씩 돌렸다 (Apple M6, 12코어, `cc -O2`). 생산자가 쉬지 않고 돌면(period 0) 세 결함 모두 첫 실행에서 바로 잡혔다. 현실적인 1 kHz(`--period-us 1000`, DMA·TMR이 1 ms마다 이벤트 하나)로 늦추면:

| 결함 | `--window` | 시드 1~5 검출 시각 (s) | 중앙값 | 평균 | 검출 종류 |
|---|---|---|---|---|---|
| `irq_rmw` | 0 | 22.96, 45.77, 16.14, 2.76, 48.11 | 22.96 | 27.1 | LOST ×2, SPURIOUS ×3 |
| `irq_rmw` | 300 | 0.70, 0.40, 5.46, 0.59, 2.02 | 0.70 | 1.83 | LOST ×3, SPURIOUS ×2 |
| `cq_claim` | 0 | 1.78, 20.86, 14.41, 0.70, 8.10 | 8.10 | 9.17 | HANG ×5 |
| `cq_claim` | 300 | 2.42, 0.99, 1.83, 1.10, 7.82 | 1.83 | 2.83 | HANG ×5 |
| `dma_overrun` | 0 | 0.111, 0.111, 0.335 (시드 1~3) | — | — | FOREIGN_WRITE ×3 |

명령: `./solution --stress <bug> --seconds 120 --period-us 1000 --window W --seed N`, 시각은 리포트의 `DETECTED at`. 주의: LOST/HANG은 대기 예산(`--timeout-ms` 기본 200 ms)이 지난 뒤에 선언되므로 실제 경쟁 시점은 그보다 0.2 s 앞이다. 읽는 법:

- 1 kHz에서 window가 몇 ns인 RMW 경쟁은 **수 초~수십 초**에 한 번 맞는다. 시드별 편차가 17배(2.8 s vs 48 s)로 크다 — 한두 번 재고 "MTTD 3초"라고 쓰면 안 되는 이유.
- race window를 spin 300회(수백 ns)로 넓히면 `irq_rmw` 중앙값이 23 s → 0.7 s로 약 30배 줄었다. `cq_claim`은 8.1 s → 1.8 s. 줄어드는 폭이 다른 건 `cq_claim`이 *같은 순간 두 DMA 채널이 claim*해야 하는 반면 `irq_rmw`는 생산자 셋 + ISR 누구와 겹쳐도 되기 때문이다.
- `dma_overrun`은 경쟁이 아니라 매 패스 일어나는 결함이라, CPU checker가 옆에서 돌기만 하면 0.1~0.3 s(블록 8개 × 1 ms = 한 패스 주기 수준)에 잡힌다.
- 생산자를 쉬지 않고 돌리면(period 0) `irq_rmw`·`cq_claim`도 수 ms 안에 경쟁이 나서 200 ms timeout 직후 검출됐다. **이벤트 속도 = 검출 속도**: 실칩 스트레스가 인터럽트 rate를 최대로 올리고 작은 DMA를 쏟아붓는 이유다.

### Part C — 야간 회귀 캠페인: 결함 × 트래픽 조합

det 모드, 시드 1..8 (실제 출력):

```text
[C] detection matrix (det mode, seeds 1..8, 0.10 s)
    bug \ cfg        CPU    DMA1     TMR  DMA0+1 DMA+TMR     ALL
    none             0/8     0/8     0/8     0/8     0/8     0/8
    cq_claim         0/8     0/8     0/8     7/8     8/8     8/8
    irq_rmw          0/8     0/8     0/8     0/8     8/8     8/8
    dma_overrun      0/8     0/8     0/8     0/8     0/8     7/8
    cq_claim seed 1: FAIL @step=2146 checker=DMA0 kind=HANG exp=0x00000009 act=0x00000015 diff=0x0000001C | DMA0 stuck waiting CQ slot 1 for ticket 9 (wr=20) | suspect: waited past budget: slot/ticket owned twice, lost wakeup
      p_hat=1.000/run -> 1 runs for 95%
    irq_rmw seed 1: FAIL @step=115 checker=ISR kind=SPURIOUS_IRQ exp=0x00000006 act=0x00000005 diff=0x00000003 | INT_STS.TMR set but TMR_SEQ still 5 (already acked) | suspect: status bit re-set after W1C: stale RMW write-back
      p_hat=1.000/run -> 1 runs for 95%
    dma_overrun seed 1: FAIL @step=459 checker=CPU0 kind=FOREIGN_WRITE addr=0x80000400 exp=0xC0F5EC4E act=0xD10FD56D diff=0x11FA3923 | iter=1 word=0 written by DMA1 | suspect: another master wrote here: check its dst bounds / burst tail strobe
      p_hat=0.875/run -> 2 runs for 95%
```

읽는 법:

- **단독 조합(CPU / DMA1 / TMR)은 결함 셋 모두 0/8.** `cq_claim`은 생산자가 둘이어야, `irq_rmw`는 set하는 쪽이 둘 이상이거나 ISR과 겹쳐야 한다(TMR 혼자면 ack를 기다리므로 자기 RMW 동안 STS가 비어 있다). `dma_overrun`은 DMA1 혼자서도 *매번* 일어나지만 아무도 CPU0[0]을 보지 않는다 — 버그는 있는데 checker가 없다.
- **`irq_rmw` × DMA0+1 = 0/8.** 두 DMA가 서로의 DONE 비트를 지워도, 다음 완료가 비트를 다시 세우고 ISR은 그때 CQ를 몽땅 drain한다 — **coalescing이 유실을 가린다**. 마지막 이벤트가 유실될 때만 `isr_final`의 `LOST_IRQ`(CQ에 남은 엔트리)로 보인다. 이벤트마다 ack를 기다리는 TMR checker가 들어가야(DMA+TMR) 8/8이 된다. 카운터만 세는 인터럽트 테스트가 약한 이유다.
- **`dma_overrun` × DMA+TMR = 0/8, ALL = 7/8.** CPU checker가 있어야 보이고, 그것도 덮어쓴 순간이 CPU0가 word 0을 쓴 뒤·읽기 전이어야 한다. 안쪽 블록의 overrun은 다음 블록 word 0에 떨어지는데, 그 블록은 이미 검사가 끝났고 곧 자기 데이터로 덮이므로 영원히 안 보인다 — **경계(영역 끝)에서만 새는 버그**.
- 최소 재현 조합: `cq_claim` → DMA0+1, `irq_rmw` → DMA+TMR, `dma_overrun` → ALL (연습 5). 디버그 엔지니어에게 넘길 때는 이 최소 조합과 시드를 같이 준다.

**리포트 한 줄이 가리키는 곳.**

| 실패 줄의 핵심 | 디버그 엔지니어의 첫 행동 |
|---|---|
| `checker=CPU0 FOREIGN_WRITE addr=0x80000400 act=0xD1… written by DMA1` | DST1 영역 끝 = `0x800003FC`. 바로 다음 워드 → DMA1 쓰기 경로의 길이/strobe, 마지막 디스크립터만 재현 |
| `checker=ISR SPURIOUS_IRQ … TMR_SEQ still 5` | 지운 비트가 되살아남 → INT_STS를 RMW로 쓰는 마스터 찾기 (버스 trace에서 STS에 대한 write 주체) |
| `checker=TMR LOST_IRQ … INT_STS=0x0` | 비트가 사라짐 → 같은 워드를 set하는 다른 마스터와의 RMW 경쟁 |
| `checker=DMA0 HANG … ticket 9 … wr=20` | 같은 티켓을 두 채널이 받음 → 공유 인덱스 증가의 원자성 |

**TSan이 잡는 것과 못 잡는 것.** 정상 하네스는 TSan 경고 0 (§6). 결함 모드를 TSan 빌드로 free 실행했다 (실제 출력, `cc -g -O1 -fsanitize=thread`):

```text
$ ./solution_tsan --stress irq_rmw --seconds 5        # exit=1, "WARNING: ThreadSanitizer" 0줄
DETECTED at 0.200353 s (run ended 0.201 s, 1 failure records)
FAIL @us=200353 checker=TMR kind=LOST_IRQ exp=0x00000009 act=0x00000008 diff=0x00000001 | TMR seq 9 raised, never acked (INT_STS=0x3) | suspect: status bit lost: non-atomic RMW on shared status/doorbell
$ ./solution_tsan --stress cq_claim --seconds 5       # exit=1, 경고 0줄
FAIL @us=200392 checker=DMA1 kind=HANG exp=0x0000000A act=0x00015FA6 diff=0x00015FAC | DMA1 stuck waiting CQ slot 2 for ticket 10 (wr=90020) | suspect: waited past budget: slot/ticket owned twice, lost wakeup
$ for s in 1..6: ./solution_tsan --stress cq_claim --seconds 5 --window 300 --seed $s
seed=1..6: exit=1 warn=0 kind=HANG   (6번 모두)
$ ./solution_tsan --stress none --seconds 3           # exit=0, 경고 0줄
no failure in 3.000 s
```

exit 1은 TSan(맥에서 race 보고 시 134)이 아니라 **우리 checker**가 낸 것이다. `irq_rmw`의 load와 store는 둘 다 `_Atomic` 접근이라 C11 의미의 data race가 아니다 → TSan은 원리상 볼 수 없다. `cq_claim`도 `wr`은 atomic이고, 같은 티켓을 받은 두 생산자가 *평범한* 엔트리 필드를 동시에 쓰면 그때는 data race가 된다 — 하지만 실측 8번 모두 둘째 생산자가 슬롯 대기에서 막혀 `HANG`으로 끝났고(첫째가 이미 seq를 w+1로 바꾼 뒤라), 필드 동시 쓰기 인터리빙은 한 번도 실행되지 않아 TSan 보고도 없었다. 정리: **TSan은 "동기화 없는 동시 접근"을 찾고, "원자적 조각들의 잘못된 조합"(atomicity violation, 순서 위반)은 못 찾는다.** 그리고 칩 위 RTL의 경쟁은 TSan이 볼 수 없다 — 그래서 데이터 checker가 1차 방어선이다.

정상 모델 free 실행 (진짜 동시, 작업 수 고정이라 결과는 결정적, 실제 출력):

```text
[C] free good run: dma 3000+3000 checked, tmr 300 acked, cpu iters 60463+53029, irqs 341 (empty 38)
```

CPU 반복 수는 실행마다 다르다 (생산자가 끝날 때까지 돈다). 다른 숫자는 고정이고 CHECK한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution -lm && ./solution                      # 50/50 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san -lm && ./solution_san
cc -std=c11 -g -O1 -fsanitize=thread solution.c -o solution_tsan -lm && ./solution_tsan            # 경고 0, exit 0
./solution --stress irq_rmw --seconds 60 --period-us 1000 --window 300                              # 장시간 모드
```

실제 출력:

```text
[A] det good run: steps=867 cpu iters 3/3, dma checked 16/16, tmr acked 12, irqs 39 (empty 1)
[B] interleavings: rmw k=2 4/6 lost, k=3 84/90 lost; atomic k=3 0/6 lost
[B] irq_rmw first detected at seed 1 (SPURIOUS_IRQ by ISR), replay identical (steps=231)
    FAIL @step=115 checker=ISR kind=SPURIOUS_IRQ ... (§5 Part B)
[C] detection matrix (det mode, seeds 1..8, 0.10 s)
    ... (§5 Part C)
[C] free good run: dma 3000+3000 checked, tmr 300 acked, cpu iters 60463+53029, irqs 341 (empty 38)
50/50 passed
$ ./solution_san        → 50/50 passed, exit 0, ASan/UBSan 메시지 없음
$ ./solution_tsan       → 50/50 passed, exit 0, "WARNING: ThreadSanitizer" 0줄 (3회 반복 동일)
```

(`-lm`: 맥은 libm이 libSystem에 있어 없어도 링크되지만, Linux glibc에서는 `log1p/exp/ceil`에 필요하다.)

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 공유 상태 비트를 `v = load; store(v \| bit)`로 세움 | 각 접근은 원자적이어도 사이에 끼어든 set/W1C를 덮는다 (4/6 스케줄 유실) | `atomic_fetch_or`, HW면 SET 별칭·W1S 레지스터 |
| W1C를 `store(STS, 0)` 또는 `store(STS, v & ~handled)`로 | 읽은 뒤 새로 선 비트까지 지움 / 옛 값 write-back으로 비트 부활 | `s = load; fetch_and(~s)` (HW: 처리한 비트만 1을 써서 W1C) |
| 엔트리를 쓰기 전에 `seq` publish, 또는 relaxed store | 소비자가 덜 쓴 엔트리를 읽음. x86에서 우연히 되고 arm64에서 깨짐 | 필드 쓰기 → `store_explicit(seq, w+1, release)`, 읽기는 acquire |
| 슬롯을 검사 전에 반환 | 생산자가 같은 블록을 재사용해 정상 모델에서 가짜 CRC_DST | `dma_check_entry` 후 `cq_release` |
| checker 기대값을 메모리에서 계산 (dst CRC == 엔진 CRC만) | 소스 준비 오류처럼 양쪽이 같이 틀리면 통과 | 생성기 정의에서 `crc_expected(ch, seq)` |
| 인터럽트를 "받은 횟수"로만 검사 | coalescing 때문에 유실이 가려짐 (DMA0+1에서 0/8) | 이벤트별 seq + ack, 끝에 stranded 엔트리 검사 |
| 대기 루프에 예산 없음 | 유실 인터럽트 = 영원히 멈춘 테스트 = 야간 회귀 전체가 timeout으로 끝나 원인 정보가 없음 | poll 예산/벽시계 timeout → `HANG`/`LOST_IRQ` + 당시 상태 기록 |
| 실패 후에도 계속 돌림 | 첫 실패 뒤의 상태는 2차 피해라 원인 분석을 흐림, 로그 폭주 | stop-on-first-fail (`g_abort`), 첫 실패만 자세히 + 개수 |
| race를 `sleep()`으로 "재현" | 머신·부하마다 다름, CI에서 flaky | hook으로 인터리빙 강제, 또는 시드 고정 스케줄러 |
| "TSan 깨끗 = 경쟁 없음" | atomic 조각의 atomicity violation은 TSan이 못 봄 | 데이터 checker + 인터리빙 탐색 병행 |
| 스트레스에서 0 실패 = 통과 | 실행 n번은 p < 3/n까지만 보장 | 필요한 실행 수/soak 시간을 계산해 계획에 적기 |

## 8. 면접 Q&A

<details>
<summary>Q1. 동시성 스트레스 테스트를 어떻게 설계하나요?</summary>

① 공유 자원 목록부터: 메모리 영역·버스·캐시 라인, 큐/링 인덱스, 상태·doorbell 레지스터, 클럭/파워 도메인. ② 그 자원을 쓰는 트래픽 생성기(CPU memtest, DMA, 인터럽트, 가능하면 GPU/NPU·PCIe 트래픽)를 동시에 최대 부하로. ③ 트래픽마다 **독립 checker**: 데이터에 출처(소유자·반복 번호)를 새기고, 기대값은 정의에서 계산. ④ 결함 주입으로 "이 하네스가 실제로 잡는가"를 검증. ⑤ 재현성: 시드, 시작 순서, 실패 시 즉시 정지 + 상태 스냅숏. ⑥ 통계: 몇 시간/몇 번 돌려야 하는지.
꼬리질문 "부하를 어떻게 키우나?" → 이벤트 속도 올리기(인터럽트 rate, 작은 DMA), race window 넓히기(지연 주입, 클럭 비율 변경), 같은 캐시 라인/뱅크로 주소를 몰기, 전압·온도 코너(SIV-M07/M08).

</details>

<details>
<summary>Q2. 단독 테스트가 다 통과했는데 왜 동시 테스트가 필요하죠?</summary>

결함이 *상호작용*에 있기 때문이다. 이 레슨의 매트릭스: 공유 큐 인덱스 경쟁은 생산자 둘, 상태 RMW는 set 주체 둘 이상, DMA 꼬리 overrun은 옆 영역을 쓰는 다른 마스터가 있어야 보인다. 단독 조합은 셋 다 0/8이었다. 실제 칩에서는 버스 arbitration, 캐시 coherency, 공유 FIFO, 전력 노이즈(동시 스위칭으로 인한 IR drop)도 같은 부류다.
꼬리질문 "그럼 처음부터 전부 켜고 돌리면?" → 실패했을 때 원인 공간이 너무 넓다. 단독 → 둘씩 → 전부 순으로 올리고, 실패하면 거꾸로 줄여 최소 재현 조합을 만든다.

</details>

<details>
<summary>Q3. 간헐적인 race를 어떻게 재현하나요?</summary>

(1) 시드 고정: 트래픽 패턴·순서를 시드에서 만들고 로그에 시드를 남긴다. (2) 스케줄 제어: 이 레슨의 det 토큰 스케줄러처럼 인터리빙 자체를 시드로 고정(소프트웨어), 시뮬레이션/에뮬레이터에서 재실행(RTL). (3) window 넓히기: 의심 구간에 지연을 넣어 확률을 올린다 — 실측에서 1 kHz 이벤트 기준 검출 시간이 수 초~수십 초에서 sub-second로 줄었다. (4) 원인이 좁혀지면 hook으로 정확한 인터리빙을 강제하는 단위 테스트로 고정해 회귀 방지.
꼬리질문 "로그를 더 찍었더니 재현이 안 된다?" → 관측이 타이밍을 바꾼 것(heisenbug). 링버퍼에 타임스탬프만 기록하고 실패 후 덤프한다.

</details>

<details>
<summary>Q4. 72시간 스트레스에서 실패가 없었습니다. 버그가 없다고 할 수 있나요?</summary>

아니다. 실패가 지수분포를 따른다고 보면 0 실패에서 95% 신뢰로 말할 수 있는 건 "평균 실패 간격 > 72/3 = 24시간" 정도다. 실행 횟수로는 n번 깨끗 → p < 1-(0.05)^(1/n) ≈ 3/n. 그래서 출하 기준은 "어떤 조건에서 몇 시간"을 명시하고, 부하·코너·결함 주입으로 하네스가 잡을 수 있음을 따로 보인다.
꼬리질문 "BER 측정과 같은 식인가?" → 같다. 0 오류에서 BER < 1e-12 @95%를 주장하려면 약 3e12 비트 (SIV-M06).

</details>

<details>
<summary>Q5. TSan이 깨끗하면 race가 없는 건가요?</summary>

TSan은 happens-before 관계가 없는 **동시 비원자 접근(data race)**을 찾는다. 이 레슨에서 `irq_rmw`는 atomic load + atomic store라 TSan이 조용했지만 checker는 잡았다(atomicity violation). `cq_claim`도 실측 8회 모두 TSan 경고 0 — 경쟁의 결과가 data race가 아니라 HANG으로 나타났기 때문이다. 또 TSan은 실제로 실행된 경로만 보고, 칩의 RTL이나 DMA 같은 HW 마스터는 못 본다. 그래서 TSan은 하네스 자신의 위생 검사로 쓰고, 칩의 경쟁은 데이터 checker로 잡는다.
꼬리질문 "그럼 load와 store를 `_Atomic`으로 바꾸면 '고친' 건가?" → 경고만 사라진 것. 연산 단위(RMW 전체)가 원자적이어야 한다: `fetch_or`, CAS 루프, lock, HW SET/CLR 레지스터.

</details>

<details>
<summary>Q6. W1C 상태 레지스터를 드라이버에서 다룰 때 흔한 버그는?</summary>

① `STS = STS & ~handled` (RMW로 쓰기): W1C 레지스터에 읽은 값을 그대로 다시 쓰면 1인 비트를 *전부* 지운다 → 아직 처리 안 한 이벤트 유실. ② 읽은 값과 상관없이 `0xFFFFFFFF` 기록: 같은 문제. ③ 처리 전에 지우지 않고 처리 후 지움: 처리 중에 다시 온 이벤트가 같이 지워질 수 있다 (edge 이벤트 유실). 올바른 순서: 읽기 → 읽은 비트만 W1C → 처리 → 다시 읽어 남은 비트가 있으면 반복. (SIV-B11, C-M02 참고)
꼬리질문 "level 인터럽트면?" → 원인(소스 쪽 조건)을 먼저 없애고 상태를 지워야 한다. 순서가 바뀌면 즉시 다시 선다.

</details>

<details>
<summary>Q7. 실패 리포트에 무엇을 남기나요? 첫 실패에서 멈추나요?</summary>

검출한 checker, 종류, 시각(step/µs), 주소, 기대/실제/diff, 시드·설정, 그리고 "suspect" 힌트. 시그니처 덕분에 실제값에서 *누가 썼는지*를 바로 말할 수 있다(`act=0xD1…` → DMA1). 멈추는 게 기본이다: 첫 실패 이후는 2차 피해라 원인을 흐리고, 멈춰야 메모리·레지스터 상태를 JTAG로 덤프할 수 있다. 대신 "계속 모드"는 실패율 통계를 낼 때만 쓴다.
꼬리질문 "실패 시 무엇을 덤프?" → 공유 자원 전부(STS, 큐 인덱스, 슬롯 seq, 각 agent의 진행 카운터) — 연습 4의 heartbeat가 HANG의 주인을 가린다.

</details>

<details>
<summary>Q8. 메모리 순서(memory ordering)는 이 하네스 어디에서 중요했나요?</summary>

완료 큐의 publish. 엔트리 필드를 쓴 다음 `seq`를 release로 쓰고, 소비자는 acquire로 읽는다. 이 맥(arm64)은 약한 메모리 모델이라 순서를 안 지키면 실제로 덜 쓴 엔트리가 보일 수 있다. 하드웨어에서도 같은 문제가 있다: DMA 디스크립터를 메모리에 쓰고 doorbell MMIO를 치기 전에 `dmb`/`wmb`가 필요하고, 완료 상태를 읽은 뒤 데이터를 읽기 전에 `rmb`가 필요하다(Linux `dma_wmb`/`dma_rmb`).

</details>

## 9. 추가 연습

### 연습 1 — guard 워드: CPU checker 없이 overrun 잡기

DMA 영역 바로 뒤에 guard 워드 4개(`0xA5A5A5A5 ^ i`)를 두고 `guard_scan()`으로 첫 번째 깨진 guard를 찾아라. DMA 단독 실행에서 `dma_overrun`이 잡히는지 확인하라.

<details>
<summary>답 보기</summary>

정상 복사 후 `guard_scan == -1`, overrun 모드로 8블록 복사 후 `0`번 guard가 깨지고 그 값이 `0xD1000007`(DMA1의 마지막 데이터 — 범인 서명). 안쪽 블록 하나만 overrun하면 guard는 무사하다(다음 블록 word 0에 떨어지므로). 본문 매트릭스에서 7/8이던 검출이 **단독 실행에서 결정적으로 1/1**이 된다 — 검출을 왼쪽(shift-left)으로 옮긴 것. 실제로는 메모리 할당기 redzone(ASan과 같은 발상), MPU/IOMMU 보호 영역으로 같은 일을 하고, IOMMU는 위반 주소와 마스터 ID까지 보고한다. guard 값을 워드마다 다르게 하는 이유: "마지막 값 반복" 같은 결함이 우연히 guard와 같은 값을 쓸 확률을 낮춘다.

</details>

### 연습 2 — 0 실패가 말해 주는 것

`p_upper_zero(n, cl) = 1 - (1-cl)^(1/n)` 과 `soak_seconds(mttd, cl) = -ln(1-cl)·mttd` 를 구현하라. n = 300, 3000에서 rule of three(3/n)와 비교하고, 본문 실측 MTTD로 필요한 soak 시간을 구하라.

<details>
<summary>답 보기</summary>

n=300 → 0.009936 (3/n = 0.01), n=3000 → 3/n과 소수 둘째 자리까지 같다. MTTD 15.6 s(1 kHz·window 0 탐색 실행 한 번에서 잰 값)면 95%에 46.7 s, 99%에 71.8 s. 다만 표의 실측(시드 5개)은 2.8~48 s로 넓게 퍼지고 평균은 27 s다 — 그래서 MTTD는 여러 번 재서 추정하고, 계획에는 그 몇 배를 잡는다.

</details>

### 연습 3 — W1C와 RMW의 경쟁: spurious는 몇 가지?

시작 상태 STS = TMR(대기 중). DMA는 RMW로 DONE을 세우고(2단계), ISR은 `s = load; STS &= ~s`(2단계). 모든 인터리빙 중 ISR이 TMR을 가져갔는데 끝에 TMR이 다시 서 있는 스케줄을 세라. DMA가 원자적 set이면?

<details>
<summary>답 보기</summary>

RMW: 6개 중 2개 (`rD rI cI wD`, `rI rD cI wD` — DMA가 TMR이 든 옛 값을 읽은 뒤, ISR이 지운 다음에 쓰는 경우). 원자적 set: 3개 중 0개. 본문 hook 테스트 3번(SPURIOUS)이 이 2개 중 하나를 강제한 것이다. 실행 출력: `[ex3] W1C vs RMW setter: spurious 2/6, atomic setter 0/3`.

</details>

### 연습 4 — HANG의 주인 찾기 (heartbeat)

agent별 진행 카운터 스냅숏 두 개와 끝난 agent 마스크를 받아, 진행이 없는 agent 마스크를 돌려주는 `stalled_agents()`를 작성하라.

<details>
<summary>답 보기</summary>

`!(done & bit) && cur == prev`. 예: DMA0(40→40)와 TMR(9→9)이 멈췄지만 TMR은 끝났으면 DMA0만. free 모드 HANG에서 "누가" 멈췄는지(그리고 무엇을 기다렸는지)를 리포트에 넣는 장치다. 칩에서는 각 코어/엔진이 공유 SRAM의 heartbeat 워드를 증가시키고, 감시 코어나 JTAG 스크립트가 주기적으로 읽는다. 하드웨어 watchdog은 "죽었다"까지만, heartbeat 표는 "누가"까지 말한다.

</details>

### 연습 5 — 최소 재현 조합

검출 매트릭스(조합별 agent 마스크와 검출 시드 수)에서, 검출된 조합 중 agent 수가 가장 적은 것(같으면 검출률이 높은 것)을 고르는 `minimal_repro()`를 작성하라.

<details>
<summary>답 보기</summary>

본문 매트릭스로 `cq_claim` → DMA0+1(2개), `irq_rmw` → DMA+TMR(3개), `dma_overrun` → ALL(5개), `none` → -1. 디버그 팀에게는 "ALL에서 실패"가 아니라 "DMA0+DMA1만 켜고 시드 1이면 step 2146에서 HANG"을 넘겨야 한다 — 원인 공간이 작을수록 빨리 고친다. 실제로는 delta debugging(ddmin)처럼 조합을 반씩 줄여 가며 자동화한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice -lm && ./practice   # 15/15 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san -lm && ./practice_san
```

## 10. 핵심 정리

- 스트레스 하네스 = 공유 자원 목록 + 동시 트래픽 + **트래픽마다 독립 checker**. 데이터에 출처(소유자·반복·seq)를 새기면 깨진 워드 하나가 범인을 말한다.
- 기대값은 메모리가 아니라 **정의**에서 계산한다 (`sig_word`, `crc_expected`).
- 원자적 load + 원자적 store ≠ 원자적 RMW. 구간이 겹치면 거의 항상 유실(k=2 4/6, k=3 84/90). 고치려면 `fetch_or`/CAS/lock, HW는 SET/CLR·W1C·W1S.
- W1C는 읽은 비트만 지운다. 인터럽트 검사는 횟수가 아니라 이벤트별 seq + ack (coalescing이 유실을 가린다).
- 큐 publish는 "필드 → seq(release)", 소비는 "seq(acquire) → 필드", 슬롯 반환은 검사 뒤.
- race 재현: 시드 고정 스케줄러로 리플레이, hook으로 인터리빙 강제, window·이벤트 속도를 올려 MTTD 단축.
- 0 실패 n번 = p < 3/n (95%). 필요한 실행 수 n ≥ ln(1-cl)/ln(1-p).
- TSan은 data race만, atomicity violation과 HW 경쟁은 못 본다 → 데이터 checker가 1차 방어선. 실패하면 즉시 멈추고, 최소 재현 조합 + 시드로 넘긴다.

## 참고자료

- [C11 (N1570) §5.1.2.4 Multi-threaded executions and data races, §7.17 Atomics `<stdatomic.h>`](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf)
- [Clang ThreadSanitizer documentation](https://clang.llvm.org/docs/ThreadSanitizer.html)
- [Arm Architecture Reference Manual (A-profile) — LSE atomics `LDSET`, `LDADD`; acquire/release `LDAR`/`STLR`](https://developer.arm.com/documentation/ddi0487/latest)
- [Armv7-M Architecture Reference Manual — `LDREX`/`STREX`, exclusive monitors](https://developer.arm.com/documentation/ddi0403/latest)
- [Linux kernel — Documentation/memory-barriers.txt (`dma_wmb`/`dma_rmb`, MMIO ordering)](https://www.kernel.org/doc/Documentation/memory-barriers.txt)
- [Musuvathi et al., "Finding and Reproducing Heisenbugs in Concurrent Programs" (CHESS), OSDI 2008](https://www.usenix.org/conference/osdi-08/finding-and-reproducing-heisenbugs-concurrent-programs)
- [Dmitry Vyukov — Bounded MPMC queue (per-slot sequence numbers)](https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue)
- [rr — record and replay debugger (chaos mode)](https://rr-project.org/)
- [stress-ng — Linux stress test tool (CPU/memory/IO stressors)](https://github.com/ColinIanKing/stress-ng)
- 관련 레슨: SIV-B02 (mock MMIO), SIV-B08 (프레임워크·JSON 로그), SIV-B09 (timeout), SIV-B11 (trigger-check-clear, W1C), SIV-M02 (DMA 디스크립터 + CRC32), SIV-M03 (인터럽트 latency·유실), SIV-M06 (BER 신뢰구간과 같은 통계), SIV-M11 (flaky bisect), C-M02 (RMW·W1C), C-M14 (ISR 공유 데이터 원자성)
