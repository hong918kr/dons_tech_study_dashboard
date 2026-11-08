---
id: SIV-B11
time: 75분
updated: 2026-11-08
---
# SIV-B11 인터럽트 검증 기초: Trigger-Check-Clear

## 학습 목표

- IP 인터럽트 블록의 네 레지스터 — **RAW**(W1C), **EN**(RW), **STAT**(RO = RAW & EN), **FORCE**(WO test) — 과 IRQ 라인·인터럽트 컨트롤러(INTC)까지의 경로를 그리고, 각 지점을 무엇으로 관측하는지 말할 수 있다.
- 소스 하나에 대해 **trigger → check(RAW / STAT / LINE) → clear → check(모두 0)** 시퀀스를 쓰고, 레지스터 기술서의 valid 비트로부터 소스 × enable 모드(NONE / SELF / OTHERS) **테스트 매트릭스** 를 생성한다.
- 매트릭스로는 안 잡히는 결함(pending 중 mask/unmask, 한 소스 clear 가 다른 소스를 지움)을 **mask 토글 테스트** 와 **crosstalk 테스트** 로 잡는다.
- mask 무시, W1C 불량, W1C-as-RW, IRQ 라우팅 오류, FORCE 미연결, disable 시 pending 소실 — 7가지 결함을 mock 에 주입해 **어느 테스트·어느 단계가 잡는지** 표로 설명하고, 실패 기록을 원인 후보로 분류하는 리포트를 만든다.

## 1. 문제

first silicon 에서 "인터럽트가 안 와요" 는 가장 흔한 bring-up 이슈이고, Rivian·NVIDIA·Apple 검증 면접에서는 "IP 인터럽트 하나를 어떻게 검증하겠느냐" 가 거의 반드시 나온다 — 정답의 뼈대가 **Trigger-Check-Clear** 다.

IP 의 인터럽트 로직은 거의 모든 칩에서 같은 모양이다. 이벤트가 나면 **raw status** 비트가 서고(enable 과 무관), **enable(mask)** 과 AND 되어 **masked status** 가 되고, 모든 소스의 masked status 를 OR 한 것이 IP 의 **IRQ 출력 라인** 이다. 그 라인은 인터럽트 컨트롤러(GIC, NVIC, PLIC 등)의 입력 n 번에 배선된다. 소프트웨어는 raw status 에 1 을 써서(W1C) 지운다. 실제 예:

| IP | raw | masked | enable | clear | 강제 트리거 |
|---|---|---|---|---|---|
| Arm PL011 UART | `UARTRIS` | `UARTMIS` | `UARTIMSC` (1 = 인터럽트 허용 — 이름은 "mask" 지만 극성 주의) | `UARTICR` 에 1 쓰기 | integration test 레지스터 |
| Synopsys DW APB I2C | `IC_RAW_INTR_STAT` | `IC_INTR_STAT` | `IC_INTR_MASK` | `IC_CLR_*` 레지스터를 **읽기** (RC) | — |
| 이 레슨의 CSI2RX | `IRQ_RAW` (W1C) | `IRQ_STAT` | `IRQ_EN` | `IRQ_RAW` 에 1 쓰기 | `IRQ_FORCE` (WO) |

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b11a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">소스 s 하나의 인터럽트 경로와 관측 지점 ①②③</text>
  <rect x="10" y="40" width="150" height="34" class="f-mut"/>
  <text x="85" y="62" text-anchor="middle" fill="currentColor">HW 이벤트 (CRC 검출)</text>
  <rect x="10" y="110" width="150" height="34" class="f-warn"/>
  <text x="85" y="132" text-anchor="middle" fill="currentColor">IRQ_FORCE (WO, test)</text>
  <line x1="160" y1="57" x2="198" y2="80" stroke="currentColor" marker-end="url(#b11a)"/>
  <line x1="160" y1="127" x2="198" y2="104" stroke="currentColor" marker-end="url(#b11a)"/>
  <rect x="200" y="70" width="110" height="44" class="f-acc"/>
  <text x="255" y="89" text-anchor="middle" fill="currentColor">IRQ_RAW[s]</text>
  <text x="255" y="106" text-anchor="middle" fill="currentColor" font-size="12">W1C, sticky</text>
  <line x1="310" y1="92" x2="338" y2="92" stroke="currentColor" marker-end="url(#b11a)"/>
  <rect x="340" y="72" width="56" height="40" fill="none" stroke="currentColor"/>
  <text x="368" y="97" text-anchor="middle" fill="currentColor">AND</text>
  <rect x="318" y="162" width="100" height="34" class="f-mut"/>
  <text x="368" y="184" text-anchor="middle" fill="currentColor">IRQ_EN[s] (RW)</text>
  <line x1="368" y1="162" x2="368" y2="114" stroke="currentColor" marker-end="url(#b11a)"/>
  <line x1="396" y1="92" x2="428" y2="92" stroke="currentColor" marker-end="url(#b11a)"/>
  <rect x="430" y="70" width="100" height="44" class="f-acc"/>
  <text x="480" y="89" text-anchor="middle" fill="currentColor">IRQ_STAT[s]</text>
  <text x="480" y="106" text-anchor="middle" fill="currentColor" font-size="12">RO = RAW&amp;EN</text>
  <line x1="530" y1="92" x2="558" y2="92" stroke="currentColor" marker-end="url(#b11a)"/>
  <rect x="560" y="72" width="50" height="40" fill="none" stroke="currentColor"/>
  <text x="585" y="90" text-anchor="middle" fill="currentColor">OR</text>
  <text x="585" y="105" text-anchor="middle" fill="currentColor" font-size="12">전 소스</text>
  <line x1="610" y1="92" x2="638" y2="92" stroke="currentColor" marker-end="url(#b11a)"/>
  <rect x="640" y="70" width="90" height="44" class="f-ok"/>
  <text x="685" y="89" text-anchor="middle" fill="currentColor">INTC 입력 12</text>
  <text x="685" y="106" text-anchor="middle" fill="currentColor" font-size="12">INTC_RAW</text>
  <g fill="currentColor" font-weight="bold" text-anchor="middle">
    <text x="255" y="62">①</text><text x="480" y="62">②</text><text x="685" y="62">③</text>
  </g>
  <line x1="255" y1="214" x2="255" y2="116" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#b11a)"/>
  <text x="180" y="230" fill="currentColor">clear: IRQ_RAW 에 1 쓰기 (W1C)</text>
  <text x="10" y="258" fill="currentColor">trigger = FORCE 쓰기 · check = ①②③ 읽기 · clear = W1C 후 ①②③ 이 모두 0 인지 다시 읽기</text>
</svg>
```

호스트(맥)에는 실리콘이 없으므로 `reg_read32(off)` / `reg_write32(off, v)` 뒤에 mock 을 둔다 (SIV-B02 의 mock 을 이 IP 에 필요한 만큼 축약해 넣었다). 레지스터 맵:

| off | 이름 | access | 의미 |
|---|---|---|---|
| `0x000` | `IRQ_RAW` | W1C | bit0 FRAME_START, bit1 FRAME_END, bit2 CRC_ERR, bit3 reserved, bit4 ECC_ERR, bit5 FIFO_OVF (valid = `0x37`) |
| `0x004` | `IRQ_EN` | RW | 1 = enable. reserved 비트는 저장 안 됨 |
| `0x008` | `IRQ_STAT` | RO | `RAW & EN` |
| `0x00C` | `IRQ_FORCE` | WO | 1 쓴 소스의 RAW 를 세움, 읽으면 0 |
| `0x010` | `IP_CTRL` | W1S, self-clear | bit0 `SRST` — RAW/EN 을 리셋값 0 으로 |
| `0x800` | `INTC_RAW` | RO | INTC 입력 라인 레벨. CSI2RX 는 입력 **12** 에 연결돼야 한다 |

mock 의 `fault_t` 로 결함을 주입한다: `mask_ignored`(소스별 enable 게이트 고장), `line_from_raw`(IRQ 출력이 STAT 대신 RAW 의 OR), `clear_stuck`(W1C 가 안 먹음), `w1c_as_rw`(RAW 가 RW 로 구현됨), `force_dead`(FORCE 미연결), `route_delta`(INTC 입력 번호 어긋남), `disable_clears_raw`(EN 을 끄면 pending 소실).

### Part A — 매트릭스 생성과 Trigger-Check-Clear

- `build_matrix(valid, out, max)` — valid 의 비트마다(오름차순, reserved 건너뜀) enable 모드 3개: **NONE**(`EN=0`), **SELF**(`EN=1<<s`), **OTHERS**(`EN=valid & ~(1<<s)`). 넘치면 -1.
- `tcc_run_case(c, f)` — PRE(`RAW==0`) → EN 설정 → FORCE(trigger) → RAW == bit, STAT == `en & bit`, INTC_RAW == (enable 됐으면 `1<<12`, 아니면 0) → W1C → RAW / STAT / INTC_RAW 모두 0. 첫 실패에서 멈추고 `fail_t` 에 단계·주소·기대·실제를 남긴다. 끝나면 **항상** soft reset.
- 주입: `mask_ignored = CRC_ERR` 에서 (CRC_ERR, NONE) 과 (CRC_ERR, OTHERS) 가 `STAT` 단계에서 실패해야 한다.

### Part B — 매트릭스의 사각지대: mask 토글과 crosstalk

- `mask_toggle_test(src, f)` — enable 상태에서 trigger → **pending 인 채로 mask**(EN=0): RAW 는 남고 STAT·LINE 은 내려가야 → **unmask**: 즉시 다시 올라가야 → clear.
- `crosstalk_test(valid, f)` — 전부 enable, `FORCE = 0xFFFFFFFF`(reserved 포함) → RAW == valid → 소스를 하나씩 W1C 하며 **지운 비트만** 사라지는지, LINE 은 마지막 소스를 지울 때만 내려가는지.
- 주입: `disable_clears_raw` 는 매트릭스 15 케이스를 **모두 통과** 하지만 mask 토글이 잡는다. `w1c_as_rw` 는 crosstalk 첫 clear 에서 `exp=0x36 act=0x01`.

### Part C — CSI2RX 결함 주입 캠페인과 리포트

- `classify(f)` — 실패 기록 하나(단계 + 빠진 비트 `exp & ~act` + 남는 비트 `act & ~exp`)로 원인 후보를 고른다: `MASK_IGNORED`, `TRIGGER_PATH`, `LINE_UNGATED`, `IRQ_ROUTING`, `CLEAR_STUCK`, `CROSS_CLEAR`, `PENDING_LOST` 등 12가지.
- 주어진 `run_suite()` 가 매트릭스 15 + mask 토글 5 + crosstalk 1 = 21 개 테스트를 돌린다. 7가지 결함마다 실패 수와 **첫 실패의 테스트·소스·단계·원인** 이 맞는지 확인하고, task 의 mask 무시 결함은 전체 리포트와 소스 × 모드 결과 격자를 출력한다.

## 2. Problem (English)

> **Interrupt verification: trigger–check–clear.**
>
> An IP block (`CSI2RX`) has a W1C raw status register `IRQ_RAW`, an enable register `IRQ_EN`, a read-only masked status `IRQ_STAT = IRQ_RAW & IRQ_EN`, and a write-only test register `IRQ_FORCE` that sets raw bits. Valid sources are bits `0x37` (bit 3 reserved). The IP's interrupt output is the OR of `IRQ_STAT` and must be wired to input 12 of an interrupt controller whose line levels are readable at `INTC_RAW`. A soft reset (`IP_CTRL.SRST`) returns the block to reset state. All access goes through `uint32_t reg_read32(uint32_t off)` / `void reg_write32(uint32_t off, uint32_t v)`.
>
> 1. `int build_matrix(uint32_t valid, tcc_case_t *out, int max)` — for every valid source create three cases with enable = none / self only / all others. Return the count, or -1 on overflow.
> 2. `bool tcc_run_case(const tcc_case_t *c, fail_t *f)` — check that raw is clear, program the enable, force the source, verify raw, masked status and the INTC line, clear with W1C, verify all three are clear. Stop at the first mismatch and record step, register offset, expected and actual value. Always soft-reset at the end.
> 3. `bool mask_toggle_test(unsigned src, fail_t *f)` — masking a pending source must keep it pending but drop status and line; unmasking must re-assert immediately.
> 4. `bool crosstalk_test(uint32_t valid, fail_t *f)` — force everything (including reserved bits), then clear one source at a time; only that bit may clear, and the line must stay high until the last one.
> 5. `int classify(const fail_t *f)` — map a failure record to a probable root cause.
>
> Example: with the enable gate of `CRC_ERR` broken, case (src=2, en=NONE) must fail with `step=STAT off=0x008 exp=0x00000000 act=0x00000004`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 깨짐 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

실제로 돌린 결과 (앞부분과 끝만):

```text
FAIL starter.c:265  n == 15
FAIL starter.c:271  build_matrix(VALID_MASK, cs, 10) == -1
FAIL starter.c:279  n == 15 && pass == 15
...
[good] runs=6 failures=6 bus_accesses=0
...
19/55 passed
```

mock(`reg_read32`/`reg_write32`/`fault_t`), `expect()` 헬퍼, `run_suite()`, 리포트 출력(`print_fail`, `print_grid`)은 주어진 코드다. 채울 곳은 `build_matrix`, `tcc_run_case`, `mask_toggle_test`, `crosstalk_test`, `classify` 다섯 개. 통과하는 19개는 mock 자체의 sanity 체크와 "결함이 있으면 실패해야 한다" 쪽 CHECK 일부다 — 스텁이 무조건 false 를 돌려주니 우연히 맞는 것이고, **좋은 mock 이 통과하는 쪽** 은 전부 FAIL 이다. 검증 테스트는 두 방향을 다 봐야 하는 이유가 여기서 보인다: "항상 실패하는 테스트" 는 결함 주입 테스트를 모두 통과한다.

## 4. 힌트

<details>
<summary>힌트 1 — expect() 를 &amp;&amp; 로 이어 붙이면 "첫 실패에서 멈춤" 이 공짜</summary>

`ok = expect(f, ST_RAW, IRQ_RAW, bit) && expect(f, ST_STAT, IRQ_STAT, exp_stat) && ...` — `&&` 의 단락 평가 때문에 첫 false 뒤의 읽기는 실행되지 않고, `f` 에는 첫 실패만 남는다. 쓰기 단계(EN, FORCE, W1C)는 `if (ok) { ... }` 블록으로 나눈다.

</details>

<details>
<summary>힌트 2 — 기대값은 케이스에서 계산한다</summary>

`exp_stat = c->en & bit`, `exp_line = exp_stat ? LINE_BIT : 0`. RAW 의 기대값은 enable 과 무관하게 항상 `bit` 이다 (raw 는 mask 전). INTC_RAW 는 **전체 워드** 를 비교한다 — 라인 12 만 보면 13 번에 잘못 배선된 걸 못 본다.

</details>

<details>
<summary>힌트 3 — crosstalk 은 "남은 비트" 를 추적</summary>

`left = valid` 로 시작해 소스마다 `left &= ~bit` 후 `RAW == left`, `INTC_RAW == (left ? LINE_BIT : 0)`. 실패 기록에 어느 소스를 지우던 중이었는지 남기려면 루프 안에서 `f->src = s`.

</details>

<details>
<summary>힌트 4 — classify 는 step 별 switch + 두 마스크</summary>

`missing = exp & ~act` (있어야 할 비트가 없음), `extra = act & ~exp` (없어야 할 비트가 있음). STAT 단계에서 extra 면 mask 무시, missing 이면 enable 불량. LINE 계열은 `act & ~LINE_BIT` 가 있으면 라우팅부터 의심. clear 단계는 missing(남아야 할 다른 소스가 사라짐)을 먼저 본다 — 그게 W1C-as-RW 의 지문이다.

</details>

## 5. 풀이

### Part A — 한 케이스의 시퀀스와 각 체크의 이유

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b11b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">tcc_run_case: 한 케이스 = 7 읽기 + 4 쓰기 (실패 시 그 자리에서 중단 → soft reset)</text>
  <g font-family="monospace" font-size="12" fill="currentColor" text-anchor="middle">
    <rect x="10" y="40" width="70" height="40" class="f-mut"/><text x="45" y="58">PRE</text><text x="45" y="73">RAW==0</text>
    <rect x="92" y="40" width="70" height="40" class="f-mut"/><text x="127" y="58">EN=</text><text x="127" y="73">c-&gt;en</text>
    <rect x="174" y="40" width="70" height="40" class="f-warn"/><text x="209" y="58">FORCE</text><text x="209" y="73">=bit</text>
    <rect x="256" y="40" width="70" height="40" class="f-acc"/><text x="291" y="58">RAW</text><text x="291" y="73">==bit</text>
    <rect x="338" y="40" width="70" height="40" class="f-acc"/><text x="373" y="58">STAT</text><text x="373" y="73">==en&amp;bit</text>
    <rect x="420" y="40" width="70" height="40" class="f-acc"/><text x="455" y="58">LINE</text><text x="455" y="73">INTC_RAW</text>
    <rect x="502" y="40" width="70" height="40" class="f-warn"/><text x="537" y="58">W1C</text><text x="537" y="73">RAW=bit</text>
    <rect x="584" y="40" width="70" height="40" class="f-ok"/><text x="619" y="58">CLR_*</text><text x="619" y="73">3개 == 0</text>
    <rect x="666" y="40" width="64" height="40" class="f-mut"/><text x="698" y="58">SRST</text><text x="698" y="73">격리</text>
  </g>
  <g stroke="currentColor">
    <line x1="80" y1="60" x2="90" y2="60" marker-end="url(#b11b)"/><line x1="162" y1="60" x2="172" y2="60" marker-end="url(#b11b)"/>
    <line x1="244" y1="60" x2="254" y2="60" marker-end="url(#b11b)"/><line x1="326" y1="60" x2="336" y2="60" marker-end="url(#b11b)"/>
    <line x1="408" y1="60" x2="418" y2="60" marker-end="url(#b11b)"/><line x1="490" y1="60" x2="500" y2="60" marker-end="url(#b11b)"/>
    <line x1="572" y1="60" x2="582" y2="60" marker-end="url(#b11b)"/><line x1="654" y1="60" x2="664" y2="60" marker-end="url(#b11b)"/>
  </g>
  <text x="10" y="112" fill="currentColor" font-weight="bold">소스마다 enable 모드 3개 — 기대값</text>
  <g fill="currentColor" font-weight="bold"><text x="20" y="140">모드</text><text x="130" y="140">IRQ_EN</text><text x="300" y="140">STAT</text><text x="400" y="140">LINE</text><text x="480" y="140">주로 잡는 것</text></g>
  <line x1="10" y1="148" x2="730" y2="148" stroke="currentColor" opacity=".35"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="172">NONE</text><text x="130" y="172">0</text><text x="300" y="172">0</text><text x="400" y="172">0</text>
    <text x="20" y="202">SELF</text><text x="130" y="202">1&lt;&lt;s</text><text x="300" y="202">1&lt;&lt;s</text><text x="400" y="202">1</text>
    <text x="20" y="232">OTHERS</text><text x="130" y="232">valid &amp; ~(1&lt;&lt;s)</text><text x="300" y="232">0</text><text x="400" y="232">0</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="480" y="172">mask 무시, LINE 이 mask 안 거침</text>
    <text x="480" y="202">라우팅, enable 불량, LINE 불통</text>
    <text x="480" y="232">이웃 enable 비트가 새는 배선</text>
  </g>
  <rect x="10" y="252" width="720" height="34" class="f-mut"/>
  <text x="20" y="274" fill="currentColor">NONE 만 하면 "라인이 안 선다" 를, SELF 만 하면 "mask 가 안 먹는다" 를 영원히 못 본다.</text>
</svg>
```

```c
static bool tcc_run_case(const tcc_case_t *c, fail_t *f) {
    uint32_t bit = 1u << c->src, exp_stat = c->en & bit;
    f->test = "tcc"; f->src = c->src; f->mode = c->mode;
    bool ok = expect(f, ST_PRE, IRQ_RAW, 0);
    if (ok) {
        reg_write32(IRQ_EN, c->en);
        reg_write32(IRQ_FORCE, bit);                                 /* trigger */
        ok = expect(f, ST_RAW, IRQ_RAW, bit)                         /* check   */
          && expect(f, ST_STAT, IRQ_STAT, exp_stat)
          && expect(f, ST_LINE, INTC_RAW, exp_stat ? LINE_BIT : 0);
    }
    if (ok) {
        reg_write32(IRQ_RAW, bit);                                   /* clear   */
        ok = expect(f, ST_CLR_RAW, IRQ_RAW, 0) && expect(f, ST_CLR_STAT, IRQ_STAT, 0)
          && expect(f, ST_CLR_LINE, INTC_RAW, 0);
    }
    ip_soft_reset();
    return ok;
}
```

각 체크가 왜 있는가 — 실리콘에서 실제로 잡히는 버그 유형으로:

| 단계 | 기대 | 이게 실패하면 의심할 것 |
|---|---|---|
| `PRE` RAW==0 | 깨끗한 시작 | 이전 테스트의 잔여 pending, soft reset 불량, 리셋값 오류(SIV-B03) — 이걸 안 보면 이후 RAW 체크가 "원래 서 있던 비트" 로 거짓 통과한다 |
| `RAW` == bit (정확히) | trigger 경로 | FORCE 비트 매핑 오류(다른 소스가 섬), test 로직 미연결. **정확히 bit** 라야 "FORCE 하나가 두 소스를 세움" 도 잡힌다 |
| `STAT` == en&bit | mask 게이트 | enable 게이트 고장(mask 무시), enable 극성 반대(PL011 처럼 "mask" 레지스터가 1=허용인데 RTL 이 반대로 구현) |
| `LINE` INTC_RAW 전체 워드 | IP→INTC 배선 | 출력이 STAT 가 아닌 RAW 에서 나옴, **INTC 입력 번호 오배선**(SoC 통합 단계의 단골), 출력 라인 tie-off |
| `CLR_RAW/STAT/LINE` == 0 | W1C 경로 | W1C 미구현, clear 가 다른 클럭 도메인에서 늦게 반영, 라인이 내부에서 래치되어 안 내려감 |
| `SRST` | 격리 | 실패한 케이스가 다음 케이스를 오염시키지 않게 — 테스트가 실패해도 리포트의 **첫 실패가 근본 원인** 이 되도록 |

> **왜 FORCE 인가.** 진짜 이벤트(CRC 에러, FIFO overflow)를 만들려면 패턴 생성기·루프백·특정 타이밍이 필요하다. 인터럽트 **로직**(status/mask/clear/라우팅)은 이벤트 **검출기** 와 독립이므로, 먼저 FORCE 로 로직을 매트릭스 전체에 대해 빠르게 검증하고, 검출기는 기능 테스트(SIV 상위 레벨)에서 진짜 이벤트로 따로 검증한다. 반대로 FORCE 로 통과했다고 "CRC 인터럽트가 동작한다" 고 결론 내리면 안 된다 — 리포트에 "forced" 라고 명시한다.

> **INTC 에서 강제 pending 하면 되지 않나?** GIC 의 `GICD_ISPENDR`, NVIC 의 `ISPR` 로 pending 을 세우면 INTC→CPU 경로(우선순위, 벡터, ISR 진입)는 검증되지만 **IP→INTC 배선은 우회** 한다. 라우팅 오류(`route_delta`)는 IP 쪽에서 trigger 해야만 보인다. 두 테스트는 서로 다른 구간을 덮는다.

이 IP 의 매트릭스는 5 소스 × 3 모드 = **15 케이스**, 케이스당 버스 접근 11회(읽기 7 + 쓰기 4; 스위트 21개 전체는 실제 측정 266회). 실제 SoC 의 IP 하나는 소스가 30개를 넘기도 하지만 매트릭스는 여전히 선형(3N)이라 JTAG 로 돌려도 몇 초 안에 끝난다.

### Part B — 매트릭스가 못 보는 것

**① pending 중 mask/unmask.** 매트릭스는 EN 을 trigger **전에** 쓴다. 그래서 "EN 비트를 끌 때 그 소스의 pending 을 지워 버리는" RTL(예: 클리어 로직을 enable 의 falling edge 에 잘못 엮음)은 15 케이스를 모두 통과한다 — 테스트가 실제로 확인한 사실(`pass == 15`). 이 결함은 펌웨어에서 치명적이다: 드라이버가 임계 구역에서 잠시 mask 했다가 풀면 그 사이 도착한 이벤트가 사라진다. 그래서 별도의 시퀀스가 필요하다.

```text
EN=bit, FORCE=bit → RAW=bit, LINE=1
EN=0              → MASK_RAW == bit   (pending 유지 — mask 는 "알리지 않음" 이지 "잊음" 이 아니다)
                    MASK_STAT == 0, MASK_LINE == 0
EN=bit            → UNMASK_STAT == bit, UNMASK_LINE == 1  (unmask 즉시 다시 assert)
W1C               → CLR_RAW == 0, CLR_LINE == 0
```

`disable_clears_raw` 주입 결과: `step=MASK_RAW exp=0x00000004 act=0x00000000`. `line_from_raw` 도 여기서 `MASK_LINE` 으로 한 번 더 잡힌다.

**② crosstalk — 한 소스의 clear 가 다른 소스를 건드리나.** 매트릭스는 한 번에 소스 **하나** 만 세우므로 "W1C 가 아니라 RW 로 구현된 RAW" 를 보면 CLR_RAW 실패(1을 썼더니 1이 남음)로만 보인다 — `clear_stuck` 과 구별이 안 된다. 전 소스를 세운 뒤 하나씩 지우면 지문이 달라진다:

| 결함 | 첫 clear (FRAME_START) 후 RAW | 판정 |
|---|---|---|
| 정상 W1C | `0x36` | 통과 |
| `w1c_as_rw` | `0x01` — 쓴 값이 그대로 저장, 나머지 소멸 | `XCLR exp=0x36 act=0x01` → **CROSS_CLEAR** |
| `clear_stuck(FIFO_OVF)` | 마지막 소스에서 `0x20` 남음 | `XCLR exp=0 act=0x20` → CLEAR_STUCK |

**③ reserved 비트.** crosstalk 은 `FORCE = 0xFFFFFFFF` 를 쓰고 `RAW == valid(0x37)` 를 기대한다. reserved bit 3 이 래치되면(스펙에 없는 flop 이 남아 있거나 FORCE 디코드가 넓음) 여기서 SPURIOUS_RAW 로 잡힌다. 리셋값 테스트(SIV-B03)와 같은 원칙: reserved 는 "0 으로 읽힘" 까지가 스펙이다.

**④ LINE 은 STAT 와 따로 본다.** `line_from_raw` 결함은 STAT 레지스터는 완벽하고 출력 라인만 mask 를 안 거친다. STAT 만 확인하는 테스트는 이걸 100% 놓치고, 실제 시스템에서는 "mask 했는데 인터럽트 폭주" 로 나타난다. 관측 지점을 ①②③ 세 군데로 둔 이유다.

### Part C — 결함 주입 캠페인과 원인 분류

`classify()` 의 결정표:

| step | 조건 | 원인 | 디버그 엔지니어의 다음 행동 |
|---|---|---|---|
| `PRE` | — | `STALE_STATE` | 직전 테스트 로그 확인, SRST/리셋값 확인 |
| `RAW`, `ALL_RAW` | 빠진 비트 | `TRIGGER_PATH` | FORCE 레지스터 디코드·비트 매핑, test 모드 enable 필요 여부 |
| | 남는 비트 | `SPURIOUS_RAW` | 다른 소스가 같이 섬: FORCE 매핑, reserved flop |
| `STAT` 계열 | 남는 비트 | `MASK_IGNORED` | enable AND 게이트, EN 극성 |
| | 빠진 비트 | `ENABLE_DEAD` | EN 비트 매핑, EN 레지스터 R/W (SIV-B04) |
| `LINE` 계열 | 다른 라인 비트 | `IRQ_ROUTING` | SoC 통합 netlist / INTC 입력 번호 표 |
| | exp=0, act=1 | `LINE_UNGATED` (`CLR_LINE` 이면 `LINE_STUCK`) | 출력이 RAW 에서 나오는지, 래치 여부 |
| | exp=1, act=0 | `LINE_DEAD` | 출력 tie-off, 클럭 게이팅된 동기화기 |
| `CLR_*`, `XCLR` | 남아야 할 비트가 빠짐 | `CROSS_CLEAR` | W1C 가 RW 로 구현됐는지 |
| | 지운 비트가 남음 | `CLEAR_STUCK` | W1C 디코드, clear 가 다른 도메인에서 지연 |
| `MASK_RAW` | 빠진 비트 | `PENDING_LOST` | disable 이 clear 로 엮였는지 |

solution 을 실제로 돌린 출력:

```text
[good] runs=21 failures=0 bus_accesses=266
[mask_ignored(CRC_ERR)] failures=3/21 first: FAIL CSI2RX tcc src=CRC_ERR(2) en=NONE step=STAT reg=IRQ_STAT off=0x008 exp=0x00000000 act=0x00000004 diff=0x00000004 bits=[2] cause=MASK_IGNORED
[line_from_raw] failures=15/21 first: FAIL CSI2RX tcc src=FRAME_START(0) en=NONE step=LINE reg=INTC_RAW off=0x800 exp=0x00000000 act=0x00001000 diff=0x00001000 bits=[12] cause=LINE_UNGATED
[clear_stuck(FIFO_OVF)] failures=5/21 first: FAIL CSI2RX tcc src=FIFO_OVF(5) en=NONE step=CLR_RAW reg=IRQ_RAW off=0x000 exp=0x00000000 act=0x00000020 diff=0x00000020 bits=[5] cause=CLEAR_STUCK
[w1c_as_rw] failures=21/21 first: FAIL CSI2RX tcc src=FRAME_START(0) en=NONE step=CLR_RAW reg=IRQ_RAW off=0x000 exp=0x00000000 act=0x00000001 diff=0x00000001 bits=[0] cause=CLEAR_STUCK
[route_delta=+1] failures=11/21 first: FAIL CSI2RX tcc src=FRAME_START(0) en=SELF step=LINE reg=INTC_RAW off=0x800 exp=0x00001000 act=0x00002000 diff=0x00003000 bits=[12,13] cause=IRQ_ROUTING
[force_dead(FRAME_END)] failures=5/21 first: FAIL CSI2RX tcc src=FRAME_END(1) en=NONE step=RAW reg=IRQ_RAW off=0x000 exp=0x00000002 act=0x00000000 diff=0x00000002 bits=[1] cause=TRIGGER_PATH
[disable_clears_raw] failures=5/21 first: FAIL CSI2RX mask src=FRAME_START(0) en=SELF step=MASK_RAW reg=IRQ_RAW off=0x000 exp=0x00000001 act=0x00000000 diff=0x00000001 bits=[0] cause=PENDING_LOST
---- report: CSI2RX fault=mask_ignored(CRC_ERR) ----
FAIL CSI2RX tcc src=CRC_ERR(2) en=NONE step=STAT reg=IRQ_STAT off=0x008 exp=0x00000000 act=0x00000004 diff=0x00000004 bits=[2] cause=MASK_IGNORED
FAIL CSI2RX tcc src=CRC_ERR(2) en=OTHERS step=STAT reg=IRQ_STAT off=0x008 exp=0x00000000 act=0x00000004 diff=0x00000004 bits=[2] cause=MASK_IGNORED
FAIL CSI2RX mask src=CRC_ERR(2) en=SELF step=MASK_STAT reg=IRQ_STAT off=0x008 exp=0x00000000 act=0x00000004 diff=0x00000004 bits=[2] cause=MASK_IGNORED
  src          NONE SELF OTHERS MASK
  FRAME_START  P    P    P      P
  FRAME_END    P    P    P      P
  CRC_ERR      F    P    F      F
  ECC_ERR      P    P    P      P
  FIFO_OVF     P    P    P      P
  crosstalk    P
55/55 passed
```

리포트 한 줄에 디버그 엔지니어가 다시 물어볼 필요가 없는 정보가 다 있다: **어느 IP, 어느 테스트, 어느 소스, 어떤 enable 상태, 어느 단계, 어느 레지스터·오프셋, 기대·실제·차이 비트, 원인 후보**. 격자는 패턴을 보여 준다 — CRC_ERR 행에서 "EN 이 이 소스를 끈 모든 경우(NONE, OTHERS, MASK)" 만 실패하고 SELF 는 통과 → enable 게이트 하나가 고장, 다른 소스·배선·W1C 는 멀쩡하다는 것이 한눈에 보인다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">결함 × 테스트 매트릭스 (강조 = 그 테스트가 잡음, 칸 안 = 실패 단계)</text>
  <g fill="currentColor" font-weight="bold" font-size="12">
    <text x="10" y="48">주입 결함</text><text x="190" y="48">tcc NONE</text><text x="265" y="48">tcc SELF</text>
    <text x="340" y="48">tcc OTHERS</text><text x="420" y="48">mask</text><text x="490" y="48">xtalk</text>
    <text x="560" y="48">실패</text><text x="610" y="48">첫 원인</text>
  </g>
  <line x1="10" y1="56" x2="730" y2="56" stroke="currentColor" opacity=".35"/>
  <g class="f-warn">
    <rect x="186" y="62" width="70" height="26"/><rect x="336" y="62" width="70" height="26"/><rect x="411" y="62" width="70" height="26"/>
    <rect x="186" y="94" width="70" height="26"/><rect x="336" y="94" width="70" height="26"/><rect x="411" y="94" width="70" height="26"/>
    <rect x="186" y="126" width="70" height="26"/><rect x="261" y="126" width="70" height="26"/><rect x="336" y="126" width="70" height="26"/><rect x="411" y="126" width="70" height="26"/><rect x="486" y="126" width="70" height="26"/>
    <rect x="186" y="158" width="70" height="26"/><rect x="261" y="158" width="70" height="26"/><rect x="336" y="158" width="70" height="26"/><rect x="411" y="158" width="70" height="26"/><rect x="486" y="158" width="70" height="26"/>
    <rect x="261" y="190" width="70" height="26"/><rect x="411" y="190" width="70" height="26"/><rect x="486" y="190" width="70" height="26"/>
    <rect x="186" y="222" width="70" height="26"/><rect x="261" y="222" width="70" height="26"/><rect x="336" y="222" width="70" height="26"/><rect x="411" y="222" width="70" height="26"/><rect x="486" y="222" width="70" height="26"/>
    <rect x="411" y="254" width="70" height="26"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="80">mask_ignored(CRC)</text><text x="10" y="112">line_from_raw</text><text x="10" y="144">clear_stuck(OVF)</text>
    <text x="10" y="176">w1c_as_rw</text><text x="10" y="208">route_delta=+1</text><text x="10" y="240">force_dead(FE)</text>
    <text x="10" y="272">disable_clears_raw</text>
  </g>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="192" y="80">STAT</text><text x="342" y="80">STAT</text><text x="417" y="80">M_STAT</text>
    <text x="192" y="112">LINE</text><text x="342" y="112">LINE</text><text x="417" y="112">M_LINE</text>
    <text x="192" y="144">CLR_RAW</text><text x="267" y="144">CLR_RAW</text><text x="342" y="144">CLR_RAW</text><text x="417" y="144">CLR_RAW</text><text x="492" y="144">XCLR</text>
    <text x="192" y="176">CLR_RAW</text><text x="267" y="176">CLR_RAW</text><text x="342" y="176">CLR_RAW</text><text x="417" y="176">CLR_RAW</text><text x="492" y="176">XCLR</text>
    <text x="267" y="208">LINE</text><text x="417" y="208">LINE</text><text x="492" y="208">LINE</text>
    <text x="192" y="240">RAW</text><text x="267" y="240">RAW</text><text x="342" y="240">RAW</text><text x="417" y="240">RAW</text><text x="492" y="240">ALL_RAW</text>
    <text x="417" y="272">M_RAW</text>
    <text x="566" y="80">3</text><text x="566" y="112">15</text><text x="566" y="144">5</text><text x="566" y="176">21</text>
    <text x="566" y="208">11</text><text x="566" y="240">5</text><text x="566" y="272">5</text>
    <text x="610" y="80">MASK_IGNORED</text><text x="610" y="112">LINE_UNGATED</text><text x="610" y="144">CLEAR_STUCK</text>
    <text x="610" y="176">CLEAR+CROSS</text><text x="610" y="208">IRQ_ROUTING</text><text x="610" y="240">TRIGGER_PATH</text>
    <text x="610" y="272">PENDING_LOST</text>
  </g>
</svg>
```

표에서 읽을 것 세 가지:

1. **어떤 열도 혼자서 모든 결함을 잡지 못한다.** NONE 열은 라우팅을, SELF 열은 mask 무시를, 매트릭스 전체는 `disable_clears_raw` 를 놓친다. mask 열이 가장 넓지만 그것만으로는 `line_from_raw` 와 `mask_ignored` 의 범위(어느 enable 조합에서 터지나)를 구분하지 못한다.
2. **`w1c_as_rw` 는 단일 실패로 진단하면 오진한다.** 20개의 실패가 `CLEAR_STUCK` 으로 분류되고, crosstalk 의 1개만 `CROSS_CLEAR` 다. 테스트가 확인하는 사실: `count_cause(CLEAR_STUCK) == 20 && count_cause(CROSS_CLEAR) == 1`. "clear 가 안 되고 + 하나 지우면 나머지가 사라진다" 를 **함께** 보면 RW 구현이라는 결론이 나온다. 리포트는 첫 실패만이 아니라 원인별 집계를 보여 줘야 한다 (SIV-B14).
3. **실패 개수 자체가 정보다.** 15/21(LINE_UNGATED)은 "모든 소스, mask 된 경우 전부" — 소스 하나가 아니라 공통 출력 로직이 문제라는 뜻. 3/21 은 소스 하나의 국소 결함.

### 실제 검증 현장과의 연결

- **pre-silicon**: RTL 시뮬레이션에서는 UVM RAL 의 `uvm_reg_hw_reset_seq`, `uvm_reg_bit_bash_seq`, `uvm_reg_access_seq` 가 레지스터 속성을 보지만, 이들은 레지스터 **하나하나** 의 R/W 시맨틱을 볼 뿐 RAW→STAT→라인의 **관계** 는 보지 않는다. W1C/RO 필드가 있는 인터럽트 레지스터는 보통 bit-bash 에서 제외(`NO_REG_BIT_BASH_TEST` 리소스)하고, 인터럽트는 별도 시퀀스 + 스코어보드/어서션(예: "IRQ 출력 == |(RAW & EN)")으로 검증한다. 이 레슨의 매트릭스가 그 별도 시퀀스의 C 버전이다.
- **post-silicon bring-up**: 같은 C 테스트를 JTAG 로 붙은 디버거나 온칩 테스트 펌웨어에서 실행한다. 손으로 확인할 땐 OpenOCD 로 `mww <base+0xC> 0x4` (FORCE CRC_ERR) → `mdw <base+0x8>` (STAT) → INTC pending 레지스터 읽기 순으로 같은 체크를 한다. CPU 인터럽트는 **꺼 둔 채** 폴링으로 ①②③ 을 확인하는 게 첫 단계 — ISR 이 먼저 지워 버리면 관측할 수 없기 때문이다. 그다음에 INTC·CPU 를 enable 해 실제 ISR 진입·횟수를 확인한다(추가 연습 4). Linux 가 올라간 뒤에는 `/proc/interrupts` 의 해당 IRQ 카운터 증가가 end-to-end 확인이다.
- **ATE vs SLT vs 시스템 검증**: ATE(양산 테스터)는 스캔·BIST 로 구조 결함을 보고, 인터럽트 같은 기능 경로는 SLT(시스템 레벨 테스트)나 validation 팀의 펌웨어 테스트가 맡는 게 일반적이다. 그래서 이 테스트는 "보드 위에서 C 로" 쓴다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 55/55 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san
# 55/55 passed, ASan/UBSan 보고 없음 (출력에서 "runtime error"/"AddressSanitizer" 0건 확인)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 인터럽트 enable 상태로만 테스트 (SELF 만) | mask 무시 결함이 영원히 통과 — mask 는 "꺼져야 할 때 꺼지는가" 로만 검증된다 | NONE / OTHERS 모드 추가 |
| STAT 만 보고 라인은 안 봄 | `line_from_raw`, 라우팅 오류를 놓친다 | INTC 쪽 pending/라인 레벨까지 읽기 |
| INTC 라인을 `& LINE_BIT` 로만 비교 | 13번에 오배선돼도 "12번은 0" 만 보고 NONE 케이스를 통과, SELF 에서야 "라인 불통" 으로 오진 | `INTC_RAW` 전체 워드 비교 → 다른 비트가 서 있으면 ROUTING |
| `RAW & bit` 로만 확인 | FORCE 가 두 소스를 세우는 결함, reserved 래치를 못 봄 | `RAW == bit` (정확히) |
| clear 후 RAW 만 다시 봄 | 라인이 래치되어 안 내려가는 결함(인터럽트 폭주)을 놓침 | CLR_RAW, CLR_STAT, CLR_LINE 셋 다 |
| 케이스 사이 정리 없음 | 한 번 실패하면 이후 전부 STALE 로 오염 → 리포트 노이즈 | 매 케이스 끝에 soft reset, PRE 체크 |
| CPU 인터럽트를 켠 채 FORCE | ISR 이 먼저 W1C 해 버려 RAW 를 관측 못 함 → "trigger 불량" 으로 오진 | 폴링 단계에서는 CPU/INTC enable 끔 |
| W1C 레지스터에 `IRQ_RAW \|= bit` 로 clear | 다른 pending 까지 다 지운다 (C-M02) — 테스트 코드 자신이 crosstalk 버그를 만든다 | `reg_write32(IRQ_RAW, bit)` |
| FORCE 로 통과 = 기능 통과 | FORCE 는 status 로직만 검증, 이벤트 검출기는 안 거친다 | 리포트에 forced 표시, 실제 이벤트 테스트는 별도 |

## 8. 면접 Q&A

<details>
<summary>Q1. IP 의 인터럽트 하나를 검증하는 테스트를 설명해 보세요.</summary>

Trigger-Check-Clear. ① 사전 조건: raw status 가 0 인지 확인(잔여 상태 배제). ② enable 을 원하는 상태로 두고 force/test 레지스터나 실제 이벤트로 trigger. ③ raw status 가 **정확히 그 비트**, masked status 가 `raw & en`, IRQ 라인(INTC pending 또는 라인 레벨)이 기대대로인지 확인 — CPU 인터럽트는 꺼 둔 채 폴링으로. ④ W1C 로 clear 하고 raw/masked/라인이 모두 내려갔는지 확인. 이걸 소스 × {enable 없음, 자기만, 나머지 전부} 매트릭스로 돌리고, pending 중 mask/unmask, 전 소스 동시 trigger 후 하나씩 clear(crosstalk) 를 추가한다. 마지막으로 INTC/CPU 를 켜서 실제 ISR 진입 횟수까지 end-to-end 로 본다.
꼬리질문 "force 레지스터가 없으면?" → 루프백·패턴 생성기·에러 주입 기능으로 실제 이벤트를 만든다. 그것도 어려운 소스(예: 특정 ECC 에러)는 RTL 시뮬레이션/에뮬레이션에서 커버하고 post-silicon 에서는 waiver 로 문서화한다.

</details>

<details>
<summary>Q2. 왜 enable 을 끈 상태에서도 trigger 해 보나요? 인터럽트가 안 나는 걸 확인하는 게 의미가 있나요?</summary>

mask 는 "꺼야 할 때 꺼지는가" 로만 검증된다. enable 상태에서만 테스트하면 enable 게이트가 고장(항상 통과)이어도 모든 테스트가 통과한다. 실제 시스템에서는 드라이버가 mask 한 인터럽트가 계속 들어와 인터럽트 폭주나 레벨 인터럽트 hang 으로 나타난다. 그리고 "enable 은 끄고 raw 는 서야" 하므로, 같은 케이스가 "raw 는 mask 와 독립" 이라는 스펙도 동시에 확인한다 — 폴링 드라이버가 의존하는 성질이다.
꼬리질문 "OTHERS 모드(자기만 끄고 나머지 켬)는 왜?" → enable 비트 배선이 이웃 소스로 새거나 바뀐(swap) 결함은 NONE 으로는 안 보인다. 추가 연습 1에서 이웃 leak 결함은 OTHERS 만 잡는 걸 확인한다.

</details>

<details>
<summary>Q3. 인터럽트 상태가 RC(read-to-clear)인 IP 는 어떻게 테스트하나요?</summary>

관측 자체가 상태를 바꾸므로 순서를 바꾼다. masked/raw status 레지스터가 RC 가 아닌지 먼저 확인하고(두 번 읽어 같은 값), clear 레지스터를 **읽는** 것으로 clear 한 뒤 다시 raw 를 읽어 0 인지 본다. 다른 소스의 clear 레지스터를 읽었을 때 내 비트가 남는지도 확인한다(DW APB I2C 처럼 소스별 `IC_CLR_*` 가 있는 경우). 리셋값 테스트(SIV-B03)나 bit-bash 에서는 RC 레지스터를 읽기만 해도 상태가 바뀌므로 "읽은 뒤 값" 을 기대값에 반영하거나 제외 목록에 넣는다. 디버거 메모리 창에 RC 레지스터를 띄워 두면 디버거의 읽기가 이벤트를 지운다는 것도 bring-up 때 자주 겪는 함정이다.

</details>

<details>
<summary>Q4. bring-up 중 "인터럽트가 안 옵니다". 어디부터 어떻게 좁혀 가나요?</summary>

경로를 앞에서부터 관측한다. ① IP raw status 가 서는가 (이벤트 자체가 났나 — 안 서면 클럭/리셋/기능 설정, SIV-B10) ② masked status 가 서는가 (enable) ③ IP 출력이 INTC 에 도달했나 — INTC 의 raw/pending 레지스터에서 해당 번호(다른 번호에 서 있지 않은지도) ④ INTC enable·우선순위·타깃 CPU 설정 ⑤ CPU 쪽 마스크(PRIMASK/BASEPRI, AArch64 DAIF), 벡터 테이블, 핸들러 등록. 어느 단계에서 끊기는지가 RTL/통합/소프트웨어 중 누구의 버그인지 알려 준다. INTC 의 software-pending 기능(`GICD_ISPENDR`, NVIC `ISPR`)으로 ④⑤ 를 IP 와 분리해 확인할 수 있다.
꼬리질문 "INTC pending 은 서는데 ISR 이 안 불리면?" → INTC distributor/CPU interface enable, 우선순위 마스크(PMR), 보안 그룹(Group0/1) 설정, 벡터 오프셋.

</details>

<details>
<summary>Q5. ISR 에서 W1C 로 지웠는데 같은 인터럽트가 한 번 더 들어옵니다(spurious). 원인과 대책은?</summary>

가장 흔한 원인은 **posted write**: clear 쓰기가 버스 브리지/쓰기 버퍼에 머문 채 ISR 이 복귀하면, INTC 는 아직 high 인 라인을 다시 보고 재진입한다. 두 번째 진입에서 status 를 읽으면 그때는 0 이라 spurious 가 된다. 대책은 clear 후 **같은 주변장치 레지스터를 되읽기**(read-back 은 앞선 쓰기가 도착해야 완료되므로 쓰기를 밀어낸다). 배리어 명령만으로는 버스 브리지 너머의 도착까지 보장되지 않는 구조가 있어 read-back 이 가장 이식성 있는 방법이다. 추가 연습 4 에서 read-back 없이 2번 진입(1번 spurious), read-back 하면 1번 진입을 확인한다. 다른 원인: 레벨 소스의 원인(FIFO 데이터)을 안 없애고 status 만 지움 → 즉시 다시 섬(추가 연습 3).

</details>

<details>
<summary>Q6. 레벨 트리거와 에지 트리거 인터럽트는 검증에서 무엇이 다른가요?</summary>

레벨: 라인이 high 인 동안 계속 pending. clear 를 안 하면 ISR 이 끝나자마자 재진입(폭주) — 그래서 clear 후 라인이 내려가는지(CLR_LINE)가 핵심 체크다. 에지: 상승 에지에서 한 번 래치. 두 소스가 pending 인 상태에서 ISR 이 하나만 지우면 라인이 high 로 남아 **새 에지가 안 생겨** 남은 소스를 놓친다 — ISR 은 status 를 한 번 읽어 본 것을 모두 처리하거나, 끝에서 다시 확인해야 한다. 검증에서는 INTC 의 트리거 설정(GIC `GICD_ICFGR` 등)이 IP 출력의 성격과 맞는지를 통합 체크 항목으로 넣고, crosstalk 처럼 다중 pending 시나리오를 돌린다.

</details>

<details>
<summary>Q7. 테스트가 실패했을 때 로그에는 무엇을 남기나요?</summary>

다시 실행하지 않고도 원인 범위를 좁힐 수 있게: IP 이름·인스턴스(베이스 주소), 테스트 이름, 소스 이름과 비트, enable 상태(모드), 실패 단계, 레지스터 이름·오프셋, 기대·실제·XOR 차이와 차이 비트 목록, 원인 후보. 그리고 run 단위 헤더(보드, 실리콘 리비전, FW 버전, 전압/온도 코너)와 실패 집계 — 이 레슨의 출력이 그 형식이다. 첫 실패만 보지 말고 원인별 집계를 봐야 하는 예가 `w1c_as_rw` (CLEAR_STUCK 20 + CROSS_CLEAR 1). 타임아웃이 끼는 테스트라면 마지막으로 읽은 값과 경과 시간도 (SIV-B09), 리포트 형식은 SIV-B14.

</details>

## 9. 추가 연습

### 연습 1 — enable 배선 결함은 어떤 모드가 잡나

EN 비트 0·1 이 서로 바뀐 결함(swap)과, EN 비트 k 가 소스 k+1 도 enable 하는 결함(leak)을 mock 에 넣고, NONE/SELF/OTHERS 중 어떤 모드가 STAT 체크로 잡는지 비트마스크로 돌려주는 `detect_modes()` 를 작성하라.

<details>
<summary>답 보기</summary>

모드마다 `EN` 설정 → `FORCE=bit` → `STAT == en & bit` 인지 비교해 다르면 그 모드 비트를 켠다. 결과: swap 은 소스 0 에서 `0x6`(SELF, OTHERS) — NONE 은 EN 이 0 이라 swap 해도 0 이므로 못 잡는다. leak 은 소스 1 에서 `0x4`(**OTHERS 만**) — SELF 는 자기 비트가 이미 켜져 있어 leak 가 안 보이고, NONE 은 켜진 게 없다. 소스 0 은 leak 를 받을 이웃이 없어 `0x0`. OTHERS 모드가 매트릭스에 있는 이유가 바로 이것이다.

</details>

### 연습 2 — RC 클리어 레지스터 IP

clear 가 "소스별 CLR 레지스터를 읽기" 인 IP(DW APB I2C 의 `IC_CLR_*` 식)에 대해 trigger-check-clear 를 작성하라. STAT 읽기에 부작용이 있는 결함(`stat_rc`)을 잡아야 한다.

<details>
<summary>답 보기</summary>

trigger 후 STAT 를 **두 번** 읽어 둘 다 bit 인지 확인(첫 읽기가 지우면 둘째가 0 → 검출), 다른 소스의 CLR 를 읽은 뒤에도 내 RAW 가 남는지 확인, 내 CLR 를 읽은 뒤 RAW·STAT 가 0 인지 확인. 상태 레지스터의 "읽기는 부작용이 없어야 한다" 는 스펙 자체가 테스트 항목이다 — 디버거·로깅 코드가 상태를 읽을 수 있어야 하므로.

</details>

### 연습 3 — level 소스: 원인이 남아 있으면 다시 서야 한다

소스 0 = RXNE(수신 FIFO 비어 있지 않음, level). 데이터가 남은 상태에서 W1C 하면 RAW 가 **즉시 다시 서야** 하고, FIFO 를 비운 뒤 W1C 하면 내려가야 한다. push 순간에만 서는 결함(`level_as_edge`)을 잡는 `level_tcc()` 를 작성하라.

<details>
<summary>답 보기</summary>

push → RAW==1 → W1C → **RAW==1 이어야 통과**(일반 소스와 기대값이 반대) → FIFO 비움 → W1C → RAW==0, 라인 0. `level_as_edge` 결함에서는 첫 W1C 뒤 RAW 가 0 이 되어 검출된다. 실제 시스템에선 "FIFO 에 데이터가 남았는데 인터럽트가 다시 안 와서 수신이 멈춤" 으로 나타난다. 그래서 레지스터 기술서(SIV-B01)에 소스마다 pulse/level 속성을 적고, 테스트 생성기가 기대값을 바꾸게 한다.

</details>

### 연습 4 — ISR end-to-end 와 posted write

INTC 라인이 high 인 동안 ISR 을 부르는 CPU 모델을 만들고, ISR 은 STAT 를 한 번 읽어 본 것을 W1C 한다. clear 쓰기가 다음 버스 접근 때 반영되는(posted) mock 에서 FRAME_START·CRC_ERR 를 동시에 trigger 했을 때 ISR 진입 횟수와 spurious 횟수를 세고, read-back 으로 고쳐라.

<details>
<summary>답 보기</summary>

read-back 없음: 진입 2번, 두 번째는 STAT==0 인 spurious(1번). ISR 이 W1C 를 쓰고 복귀했을 때 쓰기가 아직 IP 에 도달하지 않아 라인이 high 이기 때문. W1C 직후 `(void)rd(RAW)` 로 되읽으면 진입 1번, spurious 0. posted 가 아닌 mock 에서는 read-back 없이도 1번 — 그래서 이 버그는 버스 구조가 바뀐 새 SoC 에서 처음 나타난다. 소스별 처리 횟수(각 1회)는 두 경우 모두 같으므로 "카운트만 보는 테스트" 는 이 결함을 못 잡는다 — spurious 횟수를 따로 세야 한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 11/11 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 11/11 passed, 보고 없음
```

## 10. 핵심 정리

- 인터럽트 경로 = RAW(W1C, enable 무관) → AND EN → STAT → OR → IRQ 라인 → INTC 입력 n. 관측 지점 ①RAW ②STAT ③INTC 를 **모두** 본다.
- Trigger-Check-Clear: PRE(RAW==0) → EN → FORCE → RAW==bit(정확히), STAT==en&bit, INTC 전체 워드 → W1C → 셋 다 0 → soft reset 으로 격리.
- 매트릭스 = 소스 × {NONE, SELF, OTHERS}. NONE·OTHERS 가 mask 를, SELF 가 라우팅·라인을 잡는다. 3N 케이스로 선형.
- 매트릭스 밖: pending 중 mask/unmask(pending 유지·즉시 재assert), crosstalk(하나 지우면 그것만), reserved 비트 FORCE.
- FORCE 는 상태 로직만 검증한다. INTC software-pending 은 IP→INTC 배선을 우회한다. 둘 다 "구간" 을 의식해서 쓴다.
- 실패 리포트: IP·테스트·소스·모드·단계·레지스터·오프셋·exp/act/diff·비트·원인 후보. 첫 실패 + 원인별 집계를 함께 본다(`w1c_as_rw` 는 CLEAR_STUCK 20 + CROSS_CLEAR 1).
- ISR 단계: clear 후 read-back(posted write → spurious), level 소스는 원인 제거 후 clear.

## 참고자료

- [Arm PrimeCell UART (PL011) Technical Reference Manual](https://developer.arm.com/documentation/ddi0183/latest/) — UARTRIS / UARTMIS / UARTIMSC / UARTICR, 인터럽트 출력 구조
- Synopsys DesignWare DW_apb_i2c Databook — IC_RAW_INTR_STAT / IC_INTR_STAT / IC_INTR_MASK / IC_CLR_* (공개 요약은 Linux `drivers/i2c/busses/i2c-designware-core.h` 의 레지스터 정의로 확인 가능)
- [Arm Generic Interrupt Controller Architecture Specification (GICv3/v4)](https://developer.arm.com/documentation/ihi0069/latest/) — GICD_ISPENDR/ICPENDR, GICD_ICFGR(edge/level), 우선순위·그룹
- Arm Cortex-M4 Devices Generic User Guide — NVIC ISER/ICER/ISPR/ICPR, PRIMASK/BASEPRI
- IEEE 1800.2 (UVM) — `uvm_reg_hw_reset_seq`, `uvm_reg_bit_bash_seq`, `uvm_reg_access_seq`, `NO_REG_BIT_BASH_TEST`
- [OpenOCD User's Guide — General commands (`mdw`, `mww`)](https://openocd.org/doc/html/General-Commands.html)
- Linux kernel `Documentation/core-api/genericirq.rst` — 레벨/에지 흐름 핸들러, spurious 인터럽트 처리
- 관련 레슨: SIV-B02 (mock MMIO·access type), SIV-B03 (리셋값), SIV-B04 (W1C R/W 기대값), SIV-B09 (poll-with-timeout), SIV-B10 (bring-up: 클럭·리셋), SIV-B14 (리포트), C-M02 (W1C 와 RMW), C-M14 (ISR 공유 데이터)
