---
id: SIV-A01
time: 120분
updated: 2026-11-08
---
# SIV-A01 Constrained-Random 테스트와 Scoreboard

## 학습 목표

- 레지스터 쓰기 · DMA 시작 · 인터럽트 이벤트를 **가중치 + 합법성 제약** 아래 랜덤으로 뽑는 generator를 만들고, 그 generator가 왜 **DUT가 아니라 모델 상태**를 봐야 재현성이 생기는지 설명한다.
- DUT와 코드를 공유하지 않는 **골든 참조 모델**과, 첫 불일치에서 시드·step·기대값·실제값·직전 trace를 남기는 **scoreboard**를 구현한다.
- directed 테스트가 놓치는 "좁은 순서" 결함을 constrained-random으로 찾고, **sequences-to-detect를 고정 시드 표로 측정**해 가중치·제약·관찰 방식이 검출 속도를 어떻게 바꾸는지 숫자로 말한다.
- 실패 시퀀스를 시드로 재현하고 **ddmin + 타이밍 보존 치환**으로 5 op 근처까지 자동 축소한 뒤, 스펙 단정(assertion)으로 **DUT 버그와 모델 버그를 가른다**.
- 이 방법이 **못 찾는 것**(공통 원인 오독, 값 하나짜리 코너, 과잉 제약, 관찰 부족에 의한 masking)을 실측값으로 제시한다.

## 1. 문제

사람이 쓴 directed 테스트는 사람이 생각한 순서만 검사한다 — 실리콘 검증 면접에서 "랜덤 테스트를 어떻게 설계하고, 실패하면 어떻게 줄여서 디버그 팀에 넘기나"를 묻는 이유가 이것이고, Rivian/NVIDIA 같은 팀의 first-silicon 회귀가 실제로 이렇게 돈다.

SIV-B11(trigger-check-clear)과 SIV-M02/M03은 "DMA 한 번 → 완료 확인 → W1C" 순서를 검사한다. 맞는 테스트지만, 칩이 죽는 건 대개 **순서가 겹칠 때**다. 예: 드라이버가 `INT_STS`의 다른 비트를 W1C로 지우는 **바로 그 cycle**에 DMA 완료가 들어오면, 상태 레지스터의 next-state 로직에서 HW set 경로가 write 경로에 눌려 DONE이 사라진다. 소프트웨어 입장에서는 인터럽트가 영원히 안 오는 hang이다. 이런 순서는 테스트 작성자의 머리에 없으므로 directed로는 잘 안 나온다.

해법은 세 조각이다 (UVM의 sequence/predictor/scoreboard와 같은 역할 분담인데, 여기서는 C로 직접 만든다):

| 조각 | 역할 | UVM 쪽 대응 (개념만) |
|---|---|---|
| Generator | 합법 제약 안에서 op를 가중치로 뽑음 | constrained sequence |
| Golden model | 스펙을 독립적으로 구현, 기대값 생산 | reference model / predictor |
| Scoreboard | 같은 입력 → DUT vs 모델 비교, 첫 불일치 보고 | `uvm_scoreboard` |

대상 블록 (SoC **KES1**의 DMA-lite + 인터럽트 상태, SIV-B02의 mock을 축약해 넣었다):

| 오프셋 | 이름 | 접근 | 의미 |
|---|---|---|---|
| 0x00 | CTRL | RW | [0] EN — 0이면 START는 ERR |
| 0x04 | STATUS | RO | [1:0] BUSY1:BUSY0 |
| 0x08 | INT_STS | W1C | [0] DONE0 [1] DONE1 [2] EVT [3] ERR. **HW set과 W1C가 같은 cycle이면 set 승** |
| 0x0C | INT_EN | RW | [3:0] 마스크 |
| 0x10/0x14 | LEN0/LEN1 | RW | [7:0] 리셋값 4. BUSY 채널의 LEN 쓰기는 무시 |
| 0x18 | CMD | WO | [1:0] START1:START0. BUSY/EN=0/LEN=0이면 시작 안 하고 ERR |
| 0x1C | IRQ | RO | [0] = (INT_STS & INT_EN) != 0 |

시간 모델: **op 하나 = 버스 1 cycle**(OP_IDLE(n)만 n cycle). START 뒤 LEN cycle이 지나면 완료된다. 완료는 버스와 무관하게 일어나므로 "완료 cycle에 어떤 버스 op가 겹쳤나"가 상태 공간이 된다.

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a01a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">Constrained-random 루프: 자극은 모델을 보고 만들고, 비교는 scoreboard가 한다</text>
  <rect x="10" y="50" width="170" height="80" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="22" y="74" fill="currentColor" font-weight="bold">Generator</text>
  <text x="22" y="96" fill="currentColor" font-size="12">시드 + 가중치 + 제약</text>
  <text x="22" y="114" fill="currentColor" font-size="12">합법 op만 뽑음</text>
  <rect x="10" y="190" width="170" height="70" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="22" y="214" fill="currentColor" font-weight="bold">모델 그림자</text>
  <text x="22" y="236" fill="currentColor" font-size="12">다음 op의 합법성 판단</text>
  <rect x="280" y="50" width="170" height="80" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="292" y="74" fill="currentColor" font-weight="bold">시퀀스 (op 배열)</text>
  <text x="292" y="96" fill="currentColor" font-size="12">시드 하나로 재생성 가능</text>
  <text x="292" y="114" fill="currentColor" font-size="12">DUT 동작과 무관</text>
  <rect x="540" y="30" width="200" height="60" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="552" y="54" fill="currentColor" font-weight="bold">DUT (cnt + next-state)</text>
  <text x="552" y="74" fill="currentColor" font-size="12">결함 스위치 bugs</text>
  <rect x="540" y="110" width="200" height="60" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="552" y="134" fill="currentColor" font-weight="bold">Golden model (done_at)</text>
  <text x="552" y="154" fill="currentColor" font-size="12">DUT와 코드 공유 없음</text>
  <rect x="540" y="210" width="200" height="90" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="552" y="234" fill="currentColor" font-weight="bold">Scoreboard</text>
  <text x="552" y="256" fill="currentColor" font-size="12">첫 불일치: seed·step·reg</text>
  <text x="552" y="274" fill="currentColor" font-size="12">exp·act·diff·마지막 8 op</text>
  <text x="552" y="292" fill="currentColor" font-size="12">→ 축소·판정(Part C)</text>
  <g stroke="currentColor" fill="none" marker-end="url(#a01a)">
    <path d="M180 90 L278 90"/>
    <path d="M95 188 L95 132"/>
    <path d="M365 132 L365 200 L130 225"/>
    <path d="M450 80 L538 60"/>
    <path d="M450 100 L538 140"/>
    <path d="M640 92 L640 108"/>
    <path d="M640 172 L640 208"/>
  </g>
  <text x="196" y="82" fill="currentColor" font-size="12">op 1개씩</text>
  <text x="220" y="212" fill="currentColor" font-size="12">적용 결과로 상태 갱신</text>
</svg>
```

일부러 넣는 DUT 결함 (`bugs` 비트 마스크로 조합 가능):

| 결함 | 조건 | 실제로 비슷한 것 |
|---|---|---|
| `BUG_RACE` | **EVT만 W1C**하는 cycle에 **DONE1 완료**가 겹치면 DONE1 set 누락 | status 레지스터의 set/clear 우선순위, 잘못 gate된 hold 경로 |
| `BUG_MASKED` | 마스크된 채널의 완료 cycle에 다른 채널이 BUSY면 latch 안 됨 | 인터럽트 enable 조건이 잘못 섞인 RTL |
| `BUG_W1C_ALL` | W1C가 쓴 비트와 무관하게 전부 지움 | 드라이버/RTL 흔한 실수 (쉬운 결함) |
| `BUG_LATE` | LEN=8 전송이 1 cycle 늦음 | 값 코너 + 타이밍만 어긋남 |
| `BUG_RESTART` | BUSY 중 START가 ERR 없이 재시작 | 에러 경로 미구현 (illegal 영역) |
| `BUG_MAGIC` | CTRL에 32비트 매직값을 쓰면 EVT 켜짐 | 디버그 언락 등 값 하나짜리 코너 |
| `BUG_CLEARW` | set vs clear 동시 → clear 승 (스펙 오독) | 모델에도 같은 오독이 있으면 공통 원인 |

그리고 모델 쪽 결함 `MB_CLEAR_WINS` — "scoreboard는 불일치라고 하는데 틀린 건 모델"인 경우를 만든다.

### Part A — Generator · 골든 모델 · scoreboard

- `model_cycle`: 스펙을 **절대 시각**(`done_at`)으로 구현. BUSY 판정은 cycle 시작 시점 값, 읽기도 cycle 시작 시점 값, set 승 (TODO A1). DUT는 `cnt` 다운카운터로 다르게 구현돼 있다 — 코드를 공유하면 공통 원인 버그가 숨는다.
- `gen_next`: 합법 제약 (BUSY 채널 START 금지, W1C는 pending 비트의 부분집합만, BUSY 채널의 LEN 금지, reserved 비트 0)을 지키며 가중치로 뽑는다. 제약 위반 비율(`illegal_pct`), 다음 cycle에 끝나는 DMA를 W1C로 겨누는 `aware_pct`, 과잉 제약(`no_w1c_busy`)을 프로파일로 둔다 (TODO A2).
- `sb_run`: 같은 op를 DUT/모델에 넣고 `OBS_EVERY`(매 op 전체 비교) · `OBS_READ`(시퀀스의 RD 값만) · `OBS_END`(끝에서만)로 비교, **첫 불일치**에서 멈춰 리포트와 직전 8 op trace를 채운다 (TODO A3).

### Part B — 좁은 순서 결함과 측정

- directed 7개(정상 흐름 + 에러 경로)는 최강 관찰(`OBS_EVERY`)에서도 RACE·MASKED·LATE를 **놓쳐야** 한다 (CHECK). 쉬운 W1C_ALL과 BUSY 중 START는 directed가 잡는다.
- `seqs_to_detect`로 시드 16~32개에 걸친 **sequences-to-detect** 평균/중앙값/최악을 측정. 프로파일(uniform / tuned / aware / over-constrained / illegal)별로 표를 만든다 (TODO B1).
- 한계를 CHECK로 못박는다: 값 코너(`BUG_MAGIC`) 0건, 공통 원인(DUT·모델 둘 다 clear-wins) 불일치 0건, 관찰을 줄이면 masking.

### Part C — 시드 재현, 자동 축소, 판정, 리포트

- 같은 시드는 같은 step에서 같은 리포트를 낸다 (`strcmp` CHECK).
- `ddmin`(complement-only) → `shrink_full`(ddmin + IDLE 치환 + IDLE 병합 + 창 제거): 시퀀스를 합법·같은 시그니처를 유지하며 줄인다 (TODO C1, C1-b).
- `spec_check`(스펙 단정 5개) → `classify`: 축소된 시퀀스를 DUT 쪽/모델 쪽으로 각각 돌려 누가 스펙을 어겼는지로 **DUT_BUG / MODEL_BUG / SPEC_MISREAD? / INCONCLUSIVE** (TODO C2, C3).
- 야간 회귀 시나리오: 결함별 첫 실패 → 축소 → 판정 → 디버그 팀에 바로 넘길 한 줄 (`format_report`, TODO C4).

## 2. Problem (English)

> **Design a constrained-random verification harness for a small DMA / interrupt-status block (C11, host model).**
>
> The block has 2 DMA channels and a W1C status register `INT_STS` (`DONE0`, `DONE1`, `EVT`, `ERR`), with a mask register and an `IRQ` line. A bus operation takes one cycle; a DMA started with `LEN` finishes `LEN` cycles later, independently of the bus. If a hardware set and a W1C clear hit the same bit in the same cycle, set wins.
>
> Implement:
> 1. A golden reference model `model_cycle` (independent of the DUT implementation) and a constrained generator `gen_next(model, rng, profile, op)` that only emits legal ops (no START on a busy channel, W1C only of currently pending bits, LEN only while the channel is idle), weighted per profile. The generator must read the *model* state only.
> 2. A scoreboard `sb_run(seq, n, bugs, model_bugs, mode, report)` comparing DUT and model, stopping at the first mismatch and reporting seed, step, cycle, register, expected, actual and the last 8 operations.
> 3. A campaign function `seqs_to_detect` and a report of sequences-to-detect across fixed seeds. Show that directed tests miss a bug which only fires when a W1C of one bit coincides with a DMA completion of another, and quantify how weights and constraints change the detection rate.
> 4. Failure handling: deterministic seed replay, a `ddmin`-based sequence shrinker that preserves legality and the failure signature, and a classifier that uses independent spec assertions to distinguish a DUT bug from a model bug.
>
> Report format: `FAIL sb seed=0x… step=… cycle=… reg=INT_STS off=0x008 exp=0x… act=0x… diff=0x… bits=[…] class=… sa=… | trace(last N): … | shrunk=<len> ops`
>
> Checked tests must be deterministic (fixed seeds, no wall-clock); planted defects must be caught with the right location, the correct model must pass, and at least one test must demonstrate what the method cannot find.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행하면 (실제 출력, 가운데 생략):

```text
FAIL starter.c:724  m_expect(&m, 1, 0, 0)
FAIL starter.c:728  m_expect(&m, 0, STS_DONE(0), 0)
FAIL starter.c:733  m_expect(&m, 0, STS_DONE(0), 0)
...
17/69 passed
```

주어진 것: 레지스터 맵, DUT와 결함 스위치 7종, op 정의, `op_legal`, 시퀀스 생성 틀, directed 7개, 캠페인 통계, 하네스, `--table`/`--nightly`/`--replay` CLI. TODO 9개: A1 `model_cycle`, A2 `gen_next`, A3 `sb_run`, B1 `seqs_to_detect`, C1 `ddmin`, C1-b `shrink_full`, C2 `spec_check`, C3 `classify`, C4 `format_report`.

스텁 상태에서 17개가 PASS인 것을 눈여겨보자 — "결함 0건"류 CHECK(정상 DUT 불일치 0, 과잉 제약 0건 등)는 **아무 일도 안 하는 scoreboard로도 통과**한다. 그래서 결함 주입 테스트를 반드시 같이 둔다 (§5 Part B의 "양방향" 원칙).

## 4. 힌트

<details>
<summary>힌트 1 — 모델을 DUT와 다르게 쓰기</summary>

DUT는 카운터를 줄여 0이 되면 끝. 모델은 `done_at = now + len`을 저장하고 `done_at == now`일 때 끝. 쓰기 효과는 cycle 시작 시점의 BUSY로 판단해야 "같은 cycle에 끝나는 채널에 START"가 스펙대로 ERR가 된다.

</details>

<details>
<summary>힌트 2 — 합법 제약은 가중치를 0으로 만드는 것</summary>

intent별 가중치 배열을 복사한 뒤, 지금 불가능한 것을 0으로 만들고 추첨한다: EN=0이거나 idle 채널이 없으면 START=0, pending이 없으면 W1C=0. 가중치 합이 0이 되지 않도록 항상 가능한 intent(IDLE/EVT/RD)를 남겨 둔다.

</details>

<details>
<summary>힌트 3 — scoreboard는 첫 불일치에서 멈춘다</summary>

첫 불일치 뒤의 비교는 의미가 없다 — DUT와 모델의 상태가 이미 갈라졌기 때문이다. trace는 불일치 op를 포함해 마지막 8개를 복사한다 (시퀀스가 배열이니 `step-7`..`step`).

</details>

<details>
<summary>힌트 4 — ddmin의 함정은 시간이다</summary>

op를 지우면 뒤 op들이 앞당겨지므로 "완료 cycle과 겹침"이 사라진다. 그래서 ddmin은 시간을 메우던 op를 못 치운다. `IDLE(1)`로 **치환**해 시간을 유지하면서 op의 의미를 없앤 뒤, 인접 IDLE을 합치고, 그다음에 다시 지운다. 또 후보 검사에서 "합법인가"를 먼저 본다.

</details>

<details>
<summary>힌트 5 — 스펙 단정은 모델이 아니라 관찰값으로</summary>

"BUSY가 내려간 채널은 DONE이 1이어야 한다"처럼 op와 전후 관찰값만으로 쓴다. 그래야 모델과 독립이고, 모델 쪽 trace에도 똑같이 적용해서 누가 어겼는지 볼 수 있다.

</details>

## 5. 풀이

### Part A — 왜 이렇게 나눴나

**Generator가 모델을 봐야 하는 이유.** `gen_next`가 다음 op를 고르려면 "지금 pending이 뭔지", "채널이 BUSY인지"를 알아야 한다. 이걸 DUT에서 읽으면 결함이 자극을 바꾼다 — DUT가 틀렸을 때 시퀀스 자체가 달라지므로 시드 재현이 깨지고, 불일치의 원인과 결과가 섞인다. 모델 그림자(shadow) 상태로 만들면 시퀀스는 **시드와 프로파일만의 함수**가 된다. 그래서 시퀀스를 먼저 다 만들고(`seq_generate`) 그다음에 DUT와 모델에 넣는다. 첫 번째 시퀀스 재현(`run_seed` 2회)이 그대로 CHECK다.

**합법 제약 vs 스펙.** 두 층을 구분한다:

| 층 | 예 | 어기면 |
|---|---|---|
| 스펙(하드웨어 동작) | BUSY 중 START → ERR, W1C는 set 승 | DUT 버그 |
| 합법 제약(자극 규칙) | "BUSY 중에는 START 안 함" (constrained) | 버그 아님, 단지 그 영역을 안 쳐봄 |

constrained 모드는 드라이버가 스펙대로 쓰는 범위(conformance)를 집중 공략하고, `illegal_pct`는 에러 경로 강건성을 친다. 나중 표에서 보듯 둘은 찾는 결함이 다르다.

**골든 모델은 왜 DUT와 코드를 공유하지 않나.** DUT와 같은 함수를 쓰면 같은 오해도 복사된다 (Part B의 공통 원인 실험). 모델은 일부러 다른 표현(절대 cycle 타임스탬프)으로 작성했다. `model_cycle`의 핵심 네 줄:

```c
for (c...) if (m->done_at[c] == m->now) { m->done_at[c] = 0; set |= STS_DONE(c); }
/* 버스 쓰기 처리: busy 는 cycle 시작 시점 값 */
if (mb & MB_CLEAR_WINS) m->sts = (m->sts | set) & ~clr;
else                    m->sts = (m->sts & ~clr) | set;   /* 스펙: set 승 */
```

set-승 수식이 `(sts & ~clr) | set`인 것이 이 레슨 전체의 기준점이다. 결함 `BUG_CLEARW`의 `(sts | set) & ~clr`과의 차이는 **같은 cycle에 같은 비트**일 때만 드러난다.

**Scoreboard — 관찰 방식 세 가지.** pre-silicon에서는 내부 신호를 다 볼 수 있지만, post-silicon에서 볼 수 있는 건 버스 읽기로 얻는 값뿐이고 그 읽기는 타이밍을 바꾼다. 그래서 `OBS_EVERY`(이상적 — 시뮬/에뮬), `OBS_READ`(시퀀스에 RD가 있을 때만 — 실리콘에 가깝다), `OBS_END`(끝에서 한 번 — 스트레스 테스트의 흔한 형태)를 분리해 측정한다.

리포트는 **왜 그 필드들인지** 있다: seed로 재현하고, step·cycle로 어디서 터졌는지, reg·exp·act·diff·bits로 무엇이 틀렸는지, 직전 8 op로 어떤 순서 끝에 왔는지.

첫 번째 수동 결함 — `BUG_W1C_ALL`의 첫 불일치 (`test_part_a`의 5 op 시퀀스, 실제 값):

```text
ops: WR CTRL=1 / EVT / WR CMD=1 / IDLE 4 / WR INT_STS=0x4
→ step=4 cycle=8 reg=INT_STS exp=0x00000001 act=0x00000000
```

`OBS_READ`로는 이 시퀀스에 RD가 없어서 **아무것도 못 본다**. 관찰 방식이 자극의 일부라는 뜻이다 — 다음 표의 근거.

### Part B — 좁은 순서 결함, 측정, 한계

**BUG_RACE가 왜 directed에 안 잡히나.** directed 3번(DONE0 pending 상태에서 EVT, 하나씩 clear)을 보자. 순서는 맞지만 W1C는 항상 "DMA가 끝난 뒤"에 온다 — 작성자가 "완료를 기다리고 지운다"고 생각하고 썼기 때문이다. RACE는 **EVT-only W1C와 DONE1 완료가 같은 cycle**이어야 발화한다.

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">BUG_RACE: 최소 재현 5 op의 cycle별 그림</text>
  <g stroke="currentColor" fill="none">
    <path d="M90 60 L90 250"/><path d="M180 60 L180 250"/><path d="M270 60 L270 250"/>
    <path d="M360 60 L360 250"/><path d="M450 60 L450 250"/><path d="M540 60 L540 250"/><path d="M630 60 L630 250"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="135" y="52">c1</text><text x="225" y="52">c2</text><text x="315" y="52">c3</text>
    <text x="405" y="52">c4</text><text x="495" y="52">c5</text><text x="585" y="52">c6</text>
  </g>
  <text x="10" y="84" fill="currentColor" font-size="12">bus op</text>
  <g font-size="12">
    <rect x="95" y="68" width="80" height="24" class="f-mut" stroke="currentColor"/><text x="135" y="85" fill="currentColor" text-anchor="middle">CTRL=1</text>
    <rect x="185" y="68" width="80" height="24" class="f-acc" stroke="currentColor"/><text x="225" y="85" fill="currentColor" text-anchor="middle">CMD=2</text>
    <rect x="275" y="68" width="170" height="24" class="f-mut" stroke="currentColor"/><text x="360" y="85" fill="currentColor" text-anchor="middle">IDLE 2</text>
    <rect x="455" y="68" width="80" height="24" class="f-ok" stroke="currentColor"/><text x="495" y="85" fill="currentColor" text-anchor="middle">EVT</text>
    <rect x="545" y="68" width="80" height="24" class="f-warn" stroke="currentColor"/><text x="585" y="85" fill="currentColor" text-anchor="middle">W1C 0x4</text>
  </g>
  <text x="10" y="124" fill="currentColor" font-size="12">BUSY1</text>
  <rect x="270" y="110" width="360" height="22" class="f-acc" stroke="currentColor"/>
  <text x="450" y="126" fill="currentColor" text-anchor="middle" font-size="12">LEN1=4 → c6에서 완료</text>
  <text x="10" y="164" fill="currentColor" font-size="12">DONE1 set</text>
  <circle cx="585" cy="160" r="9" class="f-warn" stroke="currentColor"/>
  <text x="585" y="164" fill="currentColor" text-anchor="middle" font-size="12">!</text>
  <text x="600" y="164" fill="currentColor" font-size="12">완료 + W1C 같은 cycle</text>
  <text x="10" y="196" fill="currentColor" font-size="12">EVT</text>
  <rect x="540" y="182" width="85" height="22" class="f-ok" stroke="currentColor"/>
  <text x="582" y="198" fill="currentColor" text-anchor="middle" font-size="12">pending → clear</text>
  <text x="10" y="232" fill="currentColor" font-size="12">사후 INT_STS</text>
  <text x="640" y="228" fill="currentColor" font-size="12">모델 0x2 (DONE1)</text>
  <text x="640" y="246" fill="currentColor" font-size="12">DUT  0x0 (누락)</text>
  <text x="10" y="284" fill="currentColor" font-size="13">directed는 "완료를 기다렸다 지운다"라서 이 겹침을 만들지 않는다. 랜덤은 W1C를 제멋대로 놓으니 언젠가 겹친다.</text>
</svg>
```

**측정: sequences-to-detect** (`./solution --table`, 시드 32개 · SEQ_LEN=40 · budget 2000; 평균/중앙값/최악 = 처음 검출까지 시퀀스 수):

| 결함 | directed | uniform | tuned | aware | over-constr | illegal15 |
|---|---|---|---|---|---|---|
| w1call | 찾음 | 1.4 / 1 / 3 | 1.1 / 1 / 2 | 1.1 / 1 / 2 | 1.0 / 1 / 2 | 1.1 / 1 / 2 |
| **race** | **놓침** | 21.2 / 13 / 112 | 3.1 / 3 / 20 | 1.6 / 1 / 7 | **0건** | 18.6 / 8 / 113 |
| masked | 놓침 | 2.4 / 2 / 7 | 1.2 / 1 / 3 | 1.1 / 1 / 3 | 1.2 / 1 / 3 | 2.0 / 1 / 8 |
| late (LEN=8 코너) | 놓침 | 7.7 / 6 / 32 | **0건** | **0건** | **0건** | 8.8 / 7 / 32 |
| restart (illegal) | 찾음 | 0건 | 0건 | 0건 | 0건 | 3.6 / 3 / 16 |
| magic (값 하나) | 놓침 | 0건 | 0건 | 0건 | 0건 | 0건 |

읽는 법:

- **가중치**: uniform → tuned만으로 race가 21.2 → 3.1 시퀀스(약 7배). W1C와 EVT를 늘리고 IDLE을 줄였을 뿐이다. 
- **모델 인식 조준(aware)**: "다음 cycle에 DONE1이 끝나고 EVT가 pending이면 W1C를 겨눈다" — 평균 1.6. 커버리지 기반 생성의 원리와 같다. 단, 이미 결함을 아니까 겨눈 것이고, 야간 회귀에서는 tuned 정도가 정직한 추정이다.
- **가중치의 함정**: tuned는 race가 빨라지는 대신 `len_max=4`라 **LEN=8 코너를 영영 못 친다** (0건). 제약은 무엇을 빠르게 만들면서 무엇을 영구히 잠근다.
- **과잉 제약**: `no_w1c_busy`("BUSY 중 W1C 금지")는 작성자 눈에 안전한 규칙이지만, race를 **0건**으로 만든다. 제약을 볼 때 "이 제약이 어떤 버그 영역을 지우는가"를 물어야 한다.
- **illegal**: restart는 constrained 프로파일 전부 0건, illegal15만 찾는다. 합법 시퀀스는 에러 경로를 안 탄다.

통계: 시퀀스당 검출 확률이 p면 처음 검출까지의 시퀀스 수 평균 1/p (기하 분포). 표의 uniform 21.2는 시퀀스당 p≈4.7% 근처. 0건일 때 p의 상한은 "2000개 0건 → 95% 신뢰로 p < 3/2000"(SIV-M06/M09의 rule of three와 같다).

**관찰 방식에 따른 차이** (bug=race, tuned, 시드 32개 — 실측):

| 관찰 | 검출 | 평균 / 중앙값 / 최악 |
|---|---|---|
| OBS_EVERY | 32/32 | 3.1 / 3 / 20 |
| OBS_READ | 32/32 | 160.1 / 105 / 472 |
| OBS_END | 32/32 | 46.0 / 34 / 225 |

`OBS_READ`는 51배 느리다 — tuned는 RD 가중치가 작아 시퀀스에 RD가 드물고, 그 RD가 하필 오염된 레지스터(INT_STS)를 읽어야 보이기 때문이다. `OBS_END`가 나쁜 이유는 **masking**이다: 모델이 DONE1 pending이라고 믿으니 generator가 W1C(DONE1)을 합법으로 뽑고, DUT는 이미 그 비트가 0이므로 두 상태가 다시 일치해 오류가 지워진다. post-silicon에서는 OBS_EVERY를 못 쓰니, RD 주입 비율을 올리거나 오염 후보 레지스터를 시퀀스 끝에 몰아서 읽는 식으로 관찰을 설계해야 한다.

**시퀀스 길이** (race, tuned, 실측):

| 길이 | 평균 시퀀스 | 평균 op 수 |
|---|---|---|
| 10 | 10.1 | 101 |
| 20 | 4.8 | 95 |
| 40 | 3.1 | 125 |
| 80 | 1.7 | 138 |

검출에 필요한 op 총량은 길이에 크게 민감하지 않고(이 결함은 약 100 op 정도), 짧은 시퀀스는 리셋 비용이 든다. 대표적 판단: 실리콘에서 리셋+초기화가 수십 ms 이상 걸리면 시퀀스를 길게, 첫 불일치 뒤 축소가 쉬우려면 짧게 — 이 두 힘 사이에서 고른다.

**이 방법이 못 찾는 것 (실측):**

| 한계 | 실험 | 결과 |
|---|---|---|
| 값 하나짜리 코너 | `BUG_MAGIC` (32비트 매직) + illegal15, 시퀀스 2000 × 시드 32 | 0건. 랜덤 32비트가 맞을 확률 2⁻³²; 직접 쓰는 directed 또는 값 편향 사전(CSR dictionary)이 필요 |
| 공통 원인 오독 | DUT·모델 둘 다 clear-wins | 불일치 0건 (scoreboard는 일치를 검증할 뿐 "스펙에 맞음"을 검증하지 않는다) |
| 과잉 제약 | over-constrained | race 0건 |
| 관찰 부족/masking | OBS_END | 46.0 (OBS_EVERY 3.1 대비 15배) |
| 타이밍만 어긋난 결함을 단정으로 분류 | `BUG_LATE` | scoreboard는 잡지만 단정은 통과 → `INCONCLUSIVE` (Part C) |

### Part C — 재현, 축소, 판정

**재현**: `run_seed(seed, profile, ...)`는 시드·프로파일·길이의 순수 함수. 같은 시드 → 같은 첫 불일치 step·cycle (CHECK). 캠페인 전체는 `seq_seed_of(camp, i)`로 시드를 파생해, 야간 회귀가 어디서 터져도 캠페인 시드 + 번호로 다시 뽑을 수 있다.

**축소 흐름 (`shrink_full`)**:

```svg
<svg viewBox="0 0 760 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a01b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">자동 축소: 합법 + 같은 시그니처를 지키며 줄인다 (후보는 still_fails 로 판정)</text>
  <rect x="10" y="50" width="130" height="60" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="22" y="74" fill="currentColor">prefix까지 자르기</text>
  <text x="22" y="94" fill="currentColor" font-size="12">step+1 ops</text>
  <rect x="165" y="50" width="130" height="60" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="177" y="74" fill="currentColor">ddmin</text>
  <text x="177" y="94" fill="currentColor" font-size="12">조각 제거 (1-minimal)</text>
  <rect x="320" y="50" width="130" height="60" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="332" y="74" fill="currentColor">IDLE(1) 치환</text>
  <text x="332" y="94" fill="currentColor" font-size="12">시간 보존</text>
  <rect x="475" y="50" width="130" height="60" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="487" y="74" fill="currentColor">IDLE 병합</text>
  <text x="487" y="94" fill="currentColor" font-size="12">길이 ≤ 16</text>
  <rect x="630" y="50" width="120" height="60" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="642" y="74" fill="currentColor">창 제거</text>
  <text x="642" y="94" fill="currentColor" font-size="12">2~4개 묶어서</text>
  <g stroke="currentColor" fill="none" marker-end="url(#a01b)">
    <path d="M140 80 L163 80"/><path d="M295 80 L318 80"/><path d="M450 80 L473 80"/><path d="M605 80 L628 80"/>
    <path d="M690 112 L690 150 L230 150 L230 112"/>
  </g>
  <text x="400" y="145" fill="currentColor" font-size="12" text-anchor="middle">바뀐 게 있으면 다시 ddmin</text>
  <rect x="165" y="170" width="440" height="42" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="385" y="196" fill="currentColor" text-anchor="middle" font-size="12">합법 ∧ 불일치 ∧ 시그니처(reg, diff 비트) 동일 — 이 세 조건이 still_fails</text>
</svg>
```

왜 네 단계가 필요한가 — 실측 (`--table`, race, 시드 16개 평균):

| 단계 | 평균 길이 | 최악 | 후보 실행 수 |
|---|---|---|---|
| 첫 불일치까지 prefix | 18.6 ops | — | — |
| ddmin | 9.8 | 19 | 45 |
| shrink_full | **5.5** | 10 | 97 |

ddmin의 끝은 1-minimal(하나씩 빼면 실패가 사라짐)인데, 이 시퀀스에서는 다음 두 이유로 긴 prefix가 남는다: ① op가 시간을 메우고 있어 지우면 완료 cycle이 어긋난다 → **IDLE(1)로 치환**하면 시간을 유지하면서 의미를 뺄 수 있다. ② START와 그 DONE의 W1C는 서로 의존한다 — 하나씩 빼면 제약 위반(pending 아닌 비트 W1C)이 돼서 still_fails가 거부한다 → **길이 2~4 슬라이딩 창**으로 함께 제거한다.

**슬리피지** (`--table`): `BUG_RACE|BUG_MASKED` 동시 DUT, 시드 20개 — 시그니처 안 보고 ddmin 하면 **2개가 다른 실패로 미끄러졌고**, 시그니처 유지 ddmin은 20/20 원래 결함을 유지했다. 후보 pred가 "아무 실패"면 축소기는 가장 찾기 쉬운 다른 버그로 갈아탈 수 있다. 시그니처는 (레지스터, diff 비트)로 두었다.

**판정 — scoreboard 불일치 ≠ DUT 버그.** 첫 질문은 "DUT가 틀렸나, 모델이 틀렸나, 아니면 스펙이 애매한가". 스펙 단정 5개를 모델과 **독립적으로**, op와 전후 관찰값만으로 만들고(SA1~SA5, `spec_check`), DUT 관찰값과 모델 관찰값 각각에 대해 축소된 시퀀스를 돌린다:

| DUT 측 위반 | 모델 측 위반 | 판정 | 조치 |
|---|---|---|---|
| O | X | `DUT_BUG` | 디버그 팀으로 |
| X | O | `MODEL_BUG` | 모델을 고치고, 그 시드들을 회귀에 넣기 |
| O | O | `SPEC_MISREAD?` | 스펙 해석 회의 (공통 원인 가능성) |
| X | X | `INCONCLUSIVE` | 불일치인데 단정으로 설명 못 함 — 사람이 봐야 함 |

단정을 어느 시점의 값으로 평가하는지 (`spec_scan_dut/model`): 한 op 전 관찰 → 한 op 뒤 관찰, op 자체. IDLE은 여러 cycle이지만 start-of-op/end-of-op만 보는 근사를 쓴다 (BUSY 중 재시작이 불가능한 IDLE에서는 안전).

야간 회귀 — `./solution --nightly` 실제 출력(축소·판정 후, 일부 행과 trace 뒷부분 생략 — 전문은 §6):

```text
race (W1C of EVT vs DONE1)         seq#2    candidates=112  min=5 ops class=DUT_BUG
  repro (5 ops): WR CTRL=0x1; WR CMD=0x2; IDLE 2; EVT; WR INT_STS=0x4
  FAIL sb seed=0xD6BCF23B step=4 cycle=6 reg=INT_STS off=0x008 exp=0x00000002 act=0x00000000 diff=0x00000002 bits=[1] class=DUT_BUG sa=SA1 done-on-busy-fall | trace(last 5): [0] WR CTRL=0x1; [1] WR CMD=0x2; [2] IDLE 2; [3] EVT; [4] WR INT_STS=0x4 | shrunk=5 ops
masked (done masked, other busy)   seq#0    candidates=94   min=4 ops class=DUT_BUG
  repro (4 ops): WR CTRL=0x1; WR CMD=0x1; IDLE 3; WR CMD=0x2
late (LEN=8 finishes late)         seq#1    candidates=99   min=4 ops class=INCONCLUSIVE
  repro (4 ops): WR CTRL=0x1; WR LEN0=0x8; WR CMD=0x1; IDLE 8
  FAIL sb seed=0x4CA90D85 step=3 cycle=11 reg=STATUS off=0x004 exp=0x00000000 act=0x00000001 diff=0x00000001 bits=[0] class=INCONCLUSIVE sa=none | trace(last 4): ...
restart (illegal START)            seq#5    candidates=45   min=3 ops class=DUT_BUG
model bug (clear wins in model)    seq#11   candidates=136  min=9 ops class=MODEL_BUG
  repro (9 ops): WR CTRL=0x1; WR CMD=0x1; IDLE 3; WR CMD=0x2; WR CMD=0x1; IDLE 1; EVT; IDLE 1; WR INT_STS=0x7
```

(`masked`/`restart` 등의 FAIL 전문은 아래 §6의 실제 출력에 있다.) 여기까지 오면 디버그 엔지니어는 시드 없이 **다섯 줄짜리 레지스터 시퀀스**만 보고 시작할 수 있다. masked의 최소 재현이 예상과 조금 다른 점(같은 cycle에 다른 채널이 *시작*되면서도 걸린다)도 축소기가 알려 준 사실이고, 결함 정의(`다른 채널이 BUSY(이번 cycle 시작 포함)`)와 일치한다 — 사람이 조건을 추측한 것보다 축소 결과를 먼저 믿어라.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution                    # 69/69 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 69/69, 깨끗
./solution --table      # 측정표 전체 (위 §5의 숫자)
./solution --nightly    # 야간 회귀 시나리오
./solution --replay 0xD6BCF23B race    # 시드 하나 재현 + 축소 + 리포트
```

`--replay` 실제 출력:

```text
minimal repro (5 ops): WR CTRL=0x1; WR CMD=0x2; IDLE 2; EVT; WR INT_STS=0x4
FAIL sb seed=0xD6BCF23B step=4 cycle=6 reg=INT_STS off=0x008 exp=0x00000002 act=0x00000000 diff=0x00000002 bits=[1] class=DUT_BUG sa=SA1 done-on-busy-fall | trace(last 5): [0] WR CTRL=0x1; [1] WR CMD=0x2; [2] IDLE 2; [3] EVT; [4] WR INT_STS=0x4 | shrunk=5 ops
(ddmin candidates evaluated: 112)
```

손으로 맞춰 보기: c1 CTRL, c2 CMD=2 (LEN1 리셋값 4 → done_at=6), c3–c4 IDLE 2, c5 EVT, c6 W1C(EVT) + DONE1 완료 → DUT의 set 경로만 빠진다.

`--nightly` 나머지 FAIL 전문 (실제 출력):

```text
masked (done masked, other busy)   seq#0    candidates=94   min=4 ops class=DUT_BUG
  FAIL sb seed=0xC2944F40 step=3 cycle=6 reg=INT_STS off=0x008 exp=0x00000001 act=0x00000000 diff=0x00000001 bits=[0] class=DUT_BUG sa=SA1 done-on-busy-fall | trace(last 4): [0] WR CTRL=0x1; [1] WR CMD=0x1; [2] IDLE 3; [3] WR CMD=0x2 | shrunk=4 ops
w1call (W1C clears everything)     seq#0    candidates=136  min=5 ops class=DUT_BUG
  repro (5 ops): WR CTRL=0x1; WR CMD=0x2; IDLE 3; EVT; WR INT_STS=0x2
  FAIL sb seed=0xC2944F40 step=4 cycle=7 reg=INT_STS off=0x008 exp=0x00000004 act=0x00000000 diff=0x00000004 bits=[2] class=DUT_BUG sa=SA3 sticky | trace(last 5): [0] WR CTRL=0x1; [1] WR CMD=0x2; [2] IDLE 3; [3] EVT; [4] WR INT_STS=0x2 | shrunk=5 ops
restart (illegal START)            seq#5    candidates=45   min=3 ops class=DUT_BUG
  repro (3 ops): WR CTRL=0x1; WR CMD=0x2; WR CMD=0x2
  FAIL sb seed=0x64F87773 step=2 cycle=3 reg=INT_STS off=0x008 exp=0x00000008 act=0x00000000 diff=0x00000008 bits=[3] class=DUT_BUG sa=SA5 err-on-bad-start | trace(last 3): [0] WR CTRL=0x1; [1] WR CMD=0x2; [2] WR CMD=0x2 | shrunk=3 ops
model bug (clear wins in model)    seq#11   candidates=136  min=9 ops class=MODEL_BUG
  FAIL sb seed=0x00737DE2 step=8 cycle=11 reg=INT_STS off=0x008 exp=0x00000000 act=0x00000001 diff=0x00000001 bits=[0] class=MODEL_BUG sa=SA1 done-on-busy-fall | trace(last 8): [1] WR CMD=0x1; [2] IDLE 3; [3] WR CMD=0x2; [4] WR CMD=0x1; [5] IDLE 1; [6] EVT; [7] IDLE 1; [8] WR INT_STS=0x7 | shrunk=9 ops
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고침 |
|---|---|---|
| generator가 `dut_read`로 상태를 읽음 | DUT 결함이 자극을 바꿔 시드 재현이 깨지고 원인·결과가 섞인다 | 시퀀스는 **모델 그림자**로 생성, DUT는 비교할 때만 |
| 모델을 DUT 코드에서 복사 | 공통 원인 버그는 절대 불일치가 안 난다 (`shared misread` 0건) | 다른 표현으로 작성, 스펙 단정을 별도로 둠 |
| 첫 불일치 뒤에도 계속 비교 | 상태가 이미 갈라져서 그 뒤의 "불일치"는 원인이 아니다 | 첫 불일치에서 멈추고 축소 |
| 합법 제약 없이 시퀀스를 줄임 | 축소 결과가 스펙 위반 자극이 되어 "DUT 버그"로 오판 | `still_fails`에서 합법성 먼저 |
| ddmin 후보를 "아무 실패"로 판정 | 다른 결함으로 슬리피지 (측정 2/20) | 시그니처(reg, diff)까지 일치 요구 |
| "랜덤 N개 통과 = 안전" | 가중치·제약이 영역을 지우면 0건 (`over-constr`) | 어떤 영역을 못 치는지 프로파일별로 재고, 커버리지(SIV-M13)로 확인 |
| 관찰 없이 자극만 늘림 | masking으로 오류가 지워진다 (OBS_END 46.0 vs OBS_EVERY 3.1) | 관찰 지점 설계: RD 비율, 끝단 전체 덤프 |

## 8. 면접 Q&A

<details>
<summary>Q1. Constrained-random 환경의 세 구성요소와 왜 나누는지 설명해 보세요.</summary>

Generator(제약 + 가중치로 자극), Reference model(스펙을 독립 구현해 기대값 생성), Scoreboard(DUT와 비교, 첫 불일치 보고). 나누는 이유: 자극과 판정이 분리되어야 시드만으로 재현할 수 있고, 모델을 바꾸거나 DUT를 바꿔도 비교부가 그대로다. Generator가 DUT가 아니라 모델 상태를 봐야 하는 게 핵심이다. 
꼬리질문 "모델이 RTL과 다른 사람이 썼으면 충분한가?" → 코드는 분리돼도 같은 스펙 해석이면 공통 원인 버그가 남는다. 그래서 스펙 assertion, 교차 리뷰, 이전 칩 회귀가 별도로 필요하다.

</details>

<details>
<summary>Q2. 랜덤 테스트가 실패했습니다. 다음에 뭘 하나요?</summary>

① 시드로 재현 ② 첫 불일치 step까지 자르기 ③ 축소(ddmin 류) — 합법성과 실패 시그니처를 지키면서 ④ 모델 버그인지 DUT 버그인지 판정(스펙 단정, 이전 revision, 독립 모델) ⑤ 최소 재현을 directed 테스트로 고정해 회귀에 넣기. 대부분의 시간을 쓰는 건 ③④다. 
꼬리질문 "ddmin이 왜 안 줄어드나요?" → 타이밍 의존성. op를 지우면 뒤가 앞당겨져 조건이 깨진다. NOP로 치환해 시간을 유지하고, 의존 쌍(START와 그 clear)은 창으로 같이 뺀다.

</details>

<details>
<summary>Q3. Scoreboard 불일치가 났는데 DUT가 맞을 수도 있나요? 어떻게 구분하죠?</summary>

그렇다 — 이 레슨의 `MB_CLEAR_WINS`가 그 경우다. 구분 방법: 스펙 단정을 모델과 독립적으로 써서 DUT/모델 각각의 관찰값에 적용한다(`classify`). DUT만 위반 → DUT 버그, 모델만 위반 → 모델 버그, 둘 다 → 스펙 해석 회의, 둘 다 통과 → 단정이 커버 못 한 영역(이 레슨의 `late`)이라 사람이 본다. 
꼬리질문 "단정도 사람이 쓰는데?" → 맞다, 단정 층도 틀릴 수 있다. 다만 **모델과 다른 형태**(cycle-accurate 구현 vs 불변식)로 쓰므로 같은 실수를 할 확률이 낮아진다. 그래서 2중화 이상의 판단이 필요한 건 스펙 담당자에게 간다.

</details>

<details>
<summary>Q4. 시퀀스 수를 얼마나 돌려야 충분한가요? "언제 끝났다"고 하죠?</summary>

버그당 p(시퀀스당 검출 확률)를 알면 95% 신뢰에 ln(0.05)/ln(1-p)≈3/p 시퀀스가 필요하다. 하지만 p를 모르는 게 정상이므로, 시드 수 대신 **기능 커버리지 수렴**(SIV-M13)을 본다: 특히 cross 항목(이 레슨이라면 "완료 cycle × 버스 op 종류" — 연습 5)이 채워졌는지. 0건 2000 시퀀스면 "p < 약 3/2000"(95%)까지만 말할 수 있고, 안 친 영역은 아무 말도 못 한다.
꼬리질문 "커버리지 100%면 끝인가요?" → 커버리지 모델이 표현한 공간만 끝난 것이다. over-constr 표에서 race가 0건이어도 그 프로파일의 W1C 커버리지 bin은 뭔가 찰 수 있다 (cross로 "완료 cycle에 W1C" bin을 직접 정의해야 잡힌다).

</details>

<details>
<summary>Q5. 포스트실리콘에서 이 방법의 한계는 무엇입니까? 프리실리콘과 어떻게 다르죠?</summary>

① 관찰: 내부 신호를 못 보고 레지스터 읽기로만 본다 — 읽기 자체가 타이밍을 바꾸고, 표에서 보듯 `OBS_END`/`OBS_READ`로 가면 검출이 15~50배 느려진다. ② 타이밍 제어: 소프트웨어로는 **정확한 cycle을 겨눌 수 없다**. 여기처럼 "완료 cycle에 W1C"를 cycle 단위로 맞추는 건 시뮬레이터 세계의 얘기고, 실리콘에서는 지연 값을 sweep하고 반복 횟수를 늘리며 (shmoo처럼 시간 축도 랜덤화 대상), 짧은 DMA·타이트한 폴링 루프로 충돌 확률을 올린다. ③ 재현성: 인터럽트 타이밍·DVFS·메모리 레이턴시 때문에 같은 시드가 같은 인터리빙을 보장 못 한다. ④ 대신 장점: 실행 속도가 RTL 시뮬레이션과 비교가 안 되게 빨라 사이클 수로 압도할 수 있다. 
꼬리질문 "실패 시 내부 상태는 어떻게 얻나요?" → on-chip 트레이스(ETM/ELA 류), 레지스터 스냅숏, 실패 즉시 멈춰 JTAG로 덤프, 그리고 재현 후 에뮬레이터로 옮겨 내부 신호 보기.

</details>

<details>
<summary>Q6. 가중치를 어떻게 정하나요? 더 세게 편향하면 항상 좋은가요?</summary>

아니다. 표에서 tuned는 race를 21.2 → 3.1로 줄였지만 `len_max=4` 때문에 late(LEN=8) 코너를 **0건**으로 만들었다. 가중치는 탐색의 초점이고, 제약은 영역 절단이다. 그래서 프로파일을 여러 개 두고 회귀를 돌린다(uniform 등 넓은 것 + tuned + illegal), 그리고 커버리지 hole을 보며 가중치를 조정한다(coverage-directed). 모델 인식(aware)은 더 강력하지만 "무엇을 겨눌지 알아야" 한다 — 새 버그 탐색에는 한계가 있고, 알려진 위험 패턴(SET/CLEAR 충돌, FIFO full, 경계 LEN)을 표적으로 쓴다. 
꼬리질문 "시퀀스 길이는요?" → 측정표처럼 총 op 수는 비슷했다. 길이는 리셋 비용(리셋이 비싸면 길게), 축소 쉬움(짧게), 상태 깊이(긴 시퀀스만 도달하는 상태)로 정한다.

</details>

<details>
<summary>Q7. W1C 레지스터에서 HW set과 소프트웨어 clear가 동시에 오면 어떻게 동작해야 하고, 어떻게 검증하나요?</summary>

스펙이 정할 문제고 흔한 답은 **set 승** — 이벤트를 절대 잃지 않기 위해서다 (clear가 이기면 그 사이에 들어온 이벤트가 영구 소실). 검증은 "완료 cycle에 W1C를 겹치는" 시퀀스를 의도적으로 만들어야 한다: directed로 정확한 cycle을 겨누거나(가능한 환경이면), 랜덤에서 그 교차 커버리지(cross) bin을 재서 hole이 있는지 본다. 이 레슨의 두 결함 — set 누락(RACE), clear 승(CLEARW) — 이 정확히 이 지점이다. 
꼬리질문 "소프트웨어가 안전하게 W1C하려면?" → 읽은 값의 비트만 다시 써서 지운다(SIV-M09/C-M02의 RMW 회피). 그 이상의 동시성 보호는 하드웨어 set-승 구조가 책임진다.

</details>

## 9. 추가 연습

`practice.c`는 채널 하나짜리 mini 모델로 코드를 다시 짠다 (본문 DUT를 다시 include 하지 않는다).

<details>
<summary>연습 1 — 온라인 trace ring</summary>

시퀀스가 미리 저장되지 않는 실리콘 스트레스 루프에서는 실패 시점까지의 op를 고리 버퍼에 남겨야 한다. `ring_push`/`ring_dump`를 구현해 wrap 후에도 오래된 것부터 N개, 전체 시작 인덱스를 돌려준다.

답: `b[total % N] = op; total++;` / dump는 `n = min(total, N)`, `first_idx = total - n`, `out[i] = b[(first_idx + i) % N]`. 

</details>

<details>
<summary>연습 2 — 탐지 시퀀스 수 통계</summary>

시퀀스당 검출 확률 p일 때 평균 시퀀스 수(1/p)와 신뢰도 cl에 필요한 시퀀스 수(`ln(1-cl)/ln(1-p)`의 올림)를 구현. p=0.05, cl=0.95면 59 — 실무 감각으로 "3/p"와 맞는지 확인.

답: p=0.05 → ln(0.05)/ln(0.95) = 58.4 → 59개, 3/p=60과 비슷. 작은 p로 갈수록 3/p에 수렴.

</details>

<details>
<summary>연습 3 — 가중치 ↔ 실측 hit rate</summary>

mini generator의 가중치를 바꿔 시퀀스당 p를 측정하고, 시드를 바꿔 측정한 평균 시퀀스 수가 1/p와 ±25% 안에 드는지 확인 (표본 400 trial; 공차는 기하분포 표준편차≈평균을 표본 수의 제곱근으로 나눈 값의 약 5배로 잡은 것 — 시드가 고정이므로 flaky 없이 결정적으로 통과).

답: `practice.c`의 `measure_p`/`measure_mean_seqs`. 측정값: pu=0.1377, pt=0.2663, mean_t=3.62 vs 1/pt=3.75.

</details>

<details>
<summary>연습 4 — 필드 축소 (LEN과 필러 같이 줄이기)</summary>

ddmin은 값을 안 건드린다. `EVT, START(5), NOP×4, W1C(EVT)`처럼 LEN과 필러가 결합돼 있으면 "LEN을 1 줄이고 NOP 하나 지우기"를 같이 해야 시간이 맞는다. `shrink_len_pair`로 `EVT, START(1), W1C(EVT)`까지 줄인다.

답: START를 찾아 `v>1`이면 그 뒤 첫 NOP를 지우고 `v-1`로 만든 후보가 여전히 실패하면 수용, 아니면 종료. 본문의 `shrink_full`에 같은 아이디어를 값 변형 단계로 추가할 수 있다.

</details>

<details>
<summary>연습 5 — done-cycle cross coverage (완료 cycle에 어떤 버스 op가 있었나)</summary>

SIV-M13의 cross 개념을 축소판으로: 완료 cycle에 버스 op가 nop / W1C(EVT만) / W1C(그 외) / 기타 중 무엇이었는지 bin을 센다. W1C 가중치가 0이면 W1C bin은 영원히 hole, 가중치를 주면 채워진다 — 측정은 no_w1c: `w1c_evt=0`, tuned: `w1c_evt=789`.

답: `cov_seq`. 이 cross를 처음부터 재고 있었다면 over-constrained 프로파일의 hole을 바로 봤을 것이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice -lm && ./practice   # 11/11 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san -lm && ./practice_san
```

실제 출력:

```text
[practice] pu=0.1377 pt=0.2663, mean_t=3.62 (1/pt=3.75); done-cycle cov no_w1c: nop=3419 w1c_evt=0 w1c_other=0 other=3427
[practice] done-cycle cov tuned:  nop=2206 w1c_evt=789 w1c_other=464 other=3348
11/11 passed
```

## 10. 핵심 정리

- Constrained-random = Generator(시드·가중치·제약) + 독립 골든 모델 + Scoreboard(첫 불일치 + 재현 정보). Generator는 **모델 상태**를 본다 — DUT를 보면 재현이 깨진다.
- directed는 작성자의 순서만 친다. "완료 cycle에 W1C" 같은 겹침 결함은 측정에서 directed 0건, uniform 평균 21.2 시퀀스, 가중치 조정 3.1, 모델 인식 1.6.
- 가중치는 초점, 제약은 절단 — 빠르게 하면서 영역을 지운다 (tuned가 LEN=8 코너를 0건으로, 과잉 제약이 race를 0건으로). 프로파일 여러 개 + 커버리지 cross로 보완.
- Scoreboard mismatch는 "둘이 다르다"일 뿐이다. 스펙 단정으로 DUT 버그 / 모델 버그 / 스펙 오독을 가른다. 공통 원인은 불일치가 없어 단정·리뷰가 아니면 안 잡힌다.
- 실패 처리 순서: 시드 재현 → 첫 불일치까지 자르기 → 합법·시그니처 유지 축소 → 판정 → 최소 재현을 directed 회귀로 고정.
- ddmin만으론 부족하다: 시간 의존 시퀀스는 op를 IDLE로 치환해 시간을 유지해야 줄어든다 (측정 9.8 → 5.5 ops). 축소 후보는 "아무 실패"가 아니라 "같은 실패"여야 한다.
- 못 찾는 것을 말할 수 있어야 한다: 값 하나짜리 코너, 공통 원인, 과잉 제약, 관찰 부족에 의한 masking(OBS_END 15배 느림).
- 포스트실리콘에서는 정확한 cycle을 못 겨누고 내부 신호를 못 보므로, 지연 sweep·반복·관찰 지점 설계로 메운다. 0건은 "p < 약 3/N" 이상을 말하지 않는다.

## 참고자료

- IEEE 1800 SystemVerilog — Clause 18 Constrained random value generation (constraint, solve…before, `$urandom` 재현성의 개념 대응)
- IEEE 1800.2 Universal Verification Methodology (UVM) — sequence / predictor / scoreboard 역할 분담 (이 레슨은 개념 비유만, UVM 코드 아님)
- Zeller, Hildebrandt, "Simplifying and Isolating Failure-Inducing Input", IEEE TSE 28(2), 2002 (ddmin)
- Claessen, Hughes, "QuickCheck: A Lightweight Tool for Random Testing of Haskell Programs", ICFP 2000 (속성 기반 테스트와 축소의 원조)
- Adir et al., "Genesys-Pro: Innovations in Test Program Generation for Functional Processor Verification", IEEE Design & Test, 2004 (프로세서 대상 test generator — 모델 기반 생성의 산업 사례)
- Mishra, Farahmandi (eds.), *Post-Silicon Validation and Debug*, Springer 2019 (relevant 장: 관찰성·재현성)
- 관련 레슨: SIV-B11 (trigger-check-clear), SIV-M02 (DMA + CRC32), SIV-M03 (IRQ latency/유실), SIV-M06 (BER rule of three), SIV-M09 (동시성 스트레스 · TSan), SIV-M10 (triage 클러스터링), SIV-M11 (bisect), SIV-M13 (기능 커버리지 · cross), C-M02 (RMW/W1C), TEST (단위 테스트), MEM (결함 모델)
