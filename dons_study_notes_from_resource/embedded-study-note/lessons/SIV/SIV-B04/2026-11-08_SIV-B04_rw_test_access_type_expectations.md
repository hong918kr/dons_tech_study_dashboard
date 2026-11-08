---
id: SIV-B04
time: 70분
updated: 2026-11-08
---
# SIV-B04 Read/Write 테스트와 Access Type별 기대값

## 학습 목표

- 필드마다 access type(RW/RO/WO/W1C/W1S/RC)이 다른 레지스터에서 **write 직후 read 의 기대값(expected)** 과 **비교할 비트(care mask)** 를 기술서로부터 계산하는 함수를 쓴다.
- 0x00000000 / 0xFFFFFFFF / 0x55555555 / 0xAAAAAAAA 네 패턴 R/W 테스트를 짜면서, reserved 비트·명령(side-effect) 비트·volatile 비트를 어떻게 빼는지, W1C/RC 를 왜 먼저 1 로 세워야(prime) 하는지 설명한다.
- mock 에 결함(RO 가 써짐, W1C 가 안 지워짐, W1C 를 RW 로 구현, WO 가 읽힘, RC 가 안 지워짐, W1S 를 RW 로 구현)을 주입하고 **어떤 패턴이 어떤 결함을 잡는지** 표로 말할 수 있다.
- 디버그 엔지니어가 바로 움직일 수 있는 실패 한 줄(`reg/off/pat/wr/exp/act/diff/bits/field`)과 원인 추정 문장을 만든다.
- 이 테스트가 UVM RAL 의 `uvm_reg_bit_bash_seq` / `uvm_reg_access_seq`, mirror·predict 모델과 어떻게 대응되는지 안다.

## 1. 문제

첫 실리콘이 보드에 올라오면 JTAG 로 붙어서 가장 먼저 돌리는 것이 리셋값 테스트(SIV-B03)와 이 **R/W 테스트** 다. NVIDIA·Apple·Rivian 의 post-silicon validation 면접에서 "레지스터 R/W 테스트를 짜 보세요" 는 단골인데, 핵심은 코드 길이가 아니라 **"이 비트를 쓰고 읽으면 무엇이 나와야 하나"** 를 access type 별로 정확히 말하는 것이다.

"0xFFFFFFFF 를 쓰고 0xFFFFFFFF 가 읽히는지 본다" 는 순진한 테스트는 RW 레지스터에서만 맞다. 실제 IP 블록에는 RO(ID, 상태), WO(명령), W1C(인터럽트 상태), W1S, RC(읽으면 지워지는 카운터), reserved 비트가 한 레지스터 안에 섞여 있다. 이 레슨의 시작 코드에 들어 있는 순진한 테스트를 **정상 칩** 에 돌리면 44번 쓰고 읽어서 **31개의 가짜 FAIL** 이 나오고, 덤으로 DMA `START` 비트를 6번 쳐서 DMA 를 엉뚱하게 출발시킨다 (아래 §3 에서 실제로 돌려 본다). 반대로, 기대값을 대충 "care=0" 으로 덮으면 진짜 결함을 놓친다.

호스트에는 진짜 IP 가 없으므로 SIV-B02 의 mock MMIO 를 축약해 넣었다: `reg_read32(off)` / `reg_write32(off, v)` 가 기술서의 access type 대로 동작하는 "모범 RTL" 이고, `mock_inject()` 로 RTL 버그를 흉내 낸다. 기술서는 SIV-B01 이 JSON 에서 생성하는 표를 C 배열로 축약한 것이다.

가상의 DMA 채널 IP **DMA0** (11개 레지스터):

| off | 레지스터 | 필드 (비트, access, 플래그) | 리셋 |
|---|---|---|---|
| 0x000 | ID | ID[31:0] RO | 0x0D3A0102 |
| 0x004 | CTRL | EN[0] RW **side**, MODE[2:1] RW, PRIO[5:4] RW, BURST[11:8] RW | 0 |
| 0x008 | STATUS | BUSY[0] RO **vol**, FIFO_LVL[7:4] RO **vol**, ERR_CODE[11:8] RO | 0 |
| 0x00C | INT_STATUS | DONE[0], ERR[1], HALF[2] — 모두 W1C | 0 |
| 0x010 | INT_EN | EN[2:0] RW | 0 |
| 0x014 | SRC_ADDR | ALIGN[1:0] RO (정렬, 0 고정), ADDR[31:2] RW | 0 |
| 0x018 | LEN | LEN[15:0] RW | 0 |
| 0x01C | CMD | START[0] WO **side**, ABORT[1] WO **side**, TAG[15:8] WO | 0 |
| 0x020 | ERR_CNT | CNT[7:0] RC | 0 |
| 0x024 | SW_EVT | EVT[7:0] W1S | 0 |
| 0x028 | SCRATCH | DATA[31:0] RW | 0 |

필드로 정의되지 않은 비트는 reserved 다. `F_VOL`(vol)은 하드웨어가 언제든 바꾸는 필드(mock 은 STATUS 를 읽을 때마다 BUSY/FIFO_LVL 을 움직인다), `F_SIDE`(side)는 1 을 쓰면 동작이 일어나는 필드다.

```svg
<svg viewBox="0 0 740 276" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">패턴 하나에 대한 R/W 테스트 한 사이클 (rw_test_reg)</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="40" width="130" height="58" rx="6"/>
    <rect x="155" y="40" width="130" height="58" rx="6"/>
    <rect x="300" y="40" width="130" height="58" rx="6"/>
    <rect x="445" y="40" width="130" height="58" rx="6"/>
    <rect x="590" y="40" width="140" height="58" rx="6"/>
    <line x1="140" y1="69" x2="155" y2="69"/><line x1="285" y1="69" x2="300" y2="69"/>
    <line x1="430" y1="69" x2="445" y2="69"/><line x1="575" y1="69" x2="590" y2="69"/>
    <line x1="660" y1="98" x2="660" y2="128"/>
    <line x1="220" y1="98" x2="220" y2="140"/><line x1="365" y1="98" x2="365" y2="140"/>
  </g>
  <rect x="10" y="40" width="130" height="58" rx="6" class="f-mut"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="75" y="62">① prime</text><text x="75" y="82" font-size="12">W1C·RC 를 1 로 세움</text>
    <text x="220" y="62">② before = read</text><text x="220" y="82" font-size="12">RO·W1x 기준값</text>
    <text x="365" y="62">③ wr = safe(pat)</text><text x="365" y="82" font-size="12">reserved·side → 0</text>
    <text x="510" y="62">④ write(wr)</text><text x="510" y="82" font-size="12">버스 쓰기 1회</text>
    <text x="660" y="62">⑤ act = read</text><text x="660" y="82" font-size="12">버스 읽기 1회</text>
  </g>
  <rect x="155" y="140" width="290" height="44" rx="6" class="f-acc"/>
  <text x="300" y="160" fill="currentColor" text-anchor="middle">⑥ expect_after_write(before, wr)</text>
  <text x="300" y="177" fill="currentColor" text-anchor="middle" font-size="12">→ exp, care (필드별 access type)</text>
  <rect x="520" y="128" width="210" height="56" rx="6" class="f-warn"/>
  <text x="625" y="150" fill="currentColor" text-anchor="middle">⑦ (act ^ exp) &amp; care ≠ 0 ?</text>
  <text x="625" y="170" fill="currentColor" text-anchor="middle" font-size="12">→ fail_t 기록 → 리포트</text>
  <line x1="445" y1="162" x2="520" y2="162" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="10" y="210">prime = 실보드에선 force/test 레지스터 쓰기나 실제 이벤트 (SIV-B11).</text>
    <text x="10" y="228">없으면 W1C·RC 가 항상 0 이라 "지워지는가" 를 시험할 수 없다.</text>
    <text x="10" y="246">before 는 매 패턴 write 직전에 읽는다 — RO·W1C·W1S 기대값의 기준.</text>
    <text x="10" y="264">RC 는 이 읽기로 이미 지워지므로 ⑤ 의 기대값은 0.</text>
  </g>
</svg>
```

### Part A — 기대값 모델

- `field_mask(f)` — 필드의 비트 마스크. width 는 1..32 (`1u << 32` 는 UB 다).
- `expect_after_write(r, before, wr, &exp, &care)` — write 직전에 읽은 값 `before`, 실제로 쓴 값 `wr` 에 대해 write 직후 read 의 기대값과 비교 마스크. 규칙:

| access | 기대 read 값 (필드 비트 m) | care |
|---|---|---|
| RW | `wr & m` | m |
| RO | `before & m` | m |
| WO | 0 (read-as-zero, RAZ) | m |
| W1C | `before & ~wr & m` | m |
| W1S | `(before \| wr) & m` | m |
| RC | 0 (`before` 읽기가 이미 지움) | m |
| F_VOL 필드, reserved | 0 | 0 |

### Part B — 안전한 패턴과 R/W 테스트

- `safe_write_value(r, pat)` — 패턴을 그대로 쓰지 않는다. reserved 비트와 `F_SIDE` 필드는 0. RO 비트에는 패턴을 **그대로** 쓴다 (RO 가 정말 RO 인지 보려면 써 봐야 한다).
- `rw_test_reg(r, pats, npat, prime, out, maxf)` — 그림의 ①~⑦ 을 패턴마다. 실패를 `out[0..maxf-1]` 에 기록하고 **총 실패 수** 를 돌려준다.
- 정상 mock 은 11개 레지스터 × 4패턴 모두 통과하고, side 비트에 1 이 한 번도 쓰이지 않아야 한다 (`g_side_hits == 0`).
- `prime=false` 이면 W1C 결함·RC 결함을 **못 잡는다** 는 것을 테스트로 확인한다.

### Part C — DMA0 결함 주입 런과 리포트

- `format_fail(f, buf, n)` — `FAIL reg=INT_STATUS off=0x00C pat=0xFFFFFFFF wr=0x00000007 exp=0x00000000 act=0x00000002 diff=0x00000002 bits=[1] field=ERR(W1C)`. bits 는 오름차순, 연속 구간은 `hi:lo`.
- `diagnose(f)` — 가장 낮은 실패 비트의 access type 과 방향(기대 0/실제 1 인지 반대인지)으로 원인 추정 문장.
- 과제의 두 결함 — **ID[3] RO 에 write 가 먹음**, **INT_STATUS.ERR(W1C) 가 안 지워짐** — 을 넣고 블록 전체를 돌려 리포트를 만든다. 결함 7종 × 패턴 4개 검출 매트릭스도 테스트로 고정한다.

## 2. Problem (English)

> **Register read/write test with per-field access-type expectations.**
>
> A register map is given as a table of registers; each register has fields `{name, lsb, width, access, flags}` with access ∈ {RW, RO, WO, W1C, W1S, RC} and flags `F_VOL` (hardware-updated, do not compare) and `F_SIDE` (writing 1 triggers an action, never write 1). Bits not covered by a field are reserved. Accesses go through `uint32_t reg_read32(uint32_t off)` / `void reg_write32(uint32_t off, uint32_t v)`; a test hook `hw_force(off, bits)` can set status bits (models a force/test register).
>
> 1. `uint32_t field_mask(const field_t *f)` (width 1..32) and `void expect_after_write(const reg_desc_t *r, uint32_t before, uint32_t wr, uint32_t *exp, uint32_t *care)` returning the value the next read must return and which bits are comparable. WO reads as zero; an RC field was already cleared by the read that produced `before`.
> 2. `uint32_t safe_write_value(const reg_desc_t *r, uint32_t pat)` and `int rw_test_reg(const reg_desc_t *r, const uint32_t *pats, int npat, bool prime, fail_t *out, int maxf)` using patterns `0x00000000, 0xFFFFFFFF, 0x55555555, 0xAAAAAAAA`. If `prime`, set all non-volatile W1C/RC bits before each pattern. Return the total number of mismatches.
> 3. `int format_fail(const fail_t *f, char *buf, size_t n)` and `const char *diagnose(const fail_t *f)`. Inject "RO bit 3 of ID is writable" and "W1C bit ERR does not clear" into the mock and show the report.
>
> Example: `INT_STATUS` (W1C bits [2:0]) primed to `0x7`, pattern `0x55555555` → write `0x5` → expected read `0x2`. With the ERR bit stuck (no clear) and pattern `0xFFFFFFFF`: `exp=0x00000000 act=0x00000002 diff=0x00000002 bits=[1] field=ERR(W1C)`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 의 tapi 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 마지막 줄: 21/83 passed (exit 1) — TODO 를 채워 83/83 을 만든다
```

시작 코드의 `rw_test_reg` 는 일부러 **"쓴 값이 그대로 읽혀야 한다"** 는 순진한 테스트로 채워 두었다. 이걸 결함 없는 정상 mock 의 11개 레지스터에 그대로 돌려 본 실제 결과 (시작 코드에 짧은 `main` 을 붙여 실행):

```text
ID 4
CTRL 3
STATUS 3
INT_STATUS 3
INT_EN 3
SRC_ADDR 3
LEN 3
CMD 3
ERR_CNT 3
SW_EVT 3
naive total=31 side=6
```

SCRATCH(32비트 전부 RW) 하나만 통과한다. CTRL 이 실패하는 이유는 reserved 비트가 0 으로 읽히기 때문이고(칩은 정상), `side=6` 은 CTRL.EN 과 CMD.START/ABORT 에 1 을 6번 썼다는 뜻이다 — 실보드였다면 DMA 가 주소 0x00000000 에서 길이 0 짜리 전송을 시작했거나, 버스 에러를 냈을 것이다. **가짜 FAIL 이 많은 테스트는 아무도 안 본다.** 그게 이 레슨의 출발점이다.

## 4. 힌트

<details>
<summary>힌트 1 — field_mask 의 width 32</summary>

`(1u << width) - 1u` 는 width 가 32 이면 UB 다 (C11 6.5.7: 시프트 양이 피연산자 폭 이상). `width >= 32 ? 0xFFFFFFFFu : ...` 로 분기하거나 64비트로 계산한 뒤 자른다. ID·SCRATCH 가 32비트 필드라 테스트가 실제로 이 경우를 친다.

</details>

<details>
<summary>힌트 2 — expect 는 "필드별 switch + care 누적"</summary>

필드를 돌며 `m = field_mask(f)`; `F_VOL` 이면 건너뛴다(exp·care 둘 다 0). 아니면 access 별로 `e |= ...` 하고 `c |= m`. reserved 비트는 어떤 필드에도 없으니 자동으로 care 0, exp 0 이 된다 — `before` 의 reserved 비트를 exp 로 흘려보내지 않게 주의.

</details>

<details>
<summary>힌트 3 — safe_write_value 는 "허용 마스크" 하나</summary>

`F_SIDE` 가 아닌 모든 필드의 마스크를 OR 한 것이 허용 마스크. `pat & allowed`. RO 도 허용 마스크에 들어간다 — RO 에 쓰는 것은 안전하고, 그래야 "RO 가 써지는" 결함을 잡는다.

</details>

<details>
<summary>힌트 4 — rw_test_reg 의 순서</summary>

prime 마스크(`W1C|RC`, 비-volatile)를 미리 계산 → 패턴마다 `hw_force` → `before = reg_read32` → `wr = safe_write_value` → `reg_write32` → `act = reg_read32` → `expect_after_write(r, before, wr, ...)` → `(act ^ exp) & care`. 비교에는 패턴이 아니라 **실제로 쓴 값 wr** 을 넣는다.

</details>

<details>
<summary>힌트 5 — bits=[...] 구간 압축</summary>

b 를 0 부터 올리며 1 인 비트를 만나면 `lo=b`, 1 이 끝날 때까지 b 를 올리고 `hi=b-1`. `hi==lo` 면 `"%d"`, 아니면 `"%d:%d"`. 구간 사이에 콤마.

</details>

## 5. 풀이

### Part A — access type 별 기대값

핵심은 **기대값이 "패턴" 이 아니라 "(직전 상태, 실제로 쓴 값, access type)" 의 함수** 라는 것이다. 테스트의 실제 값으로 따라가 보자.

| 레지스터 | before | wr | exp | care | 읽는 법 |
|---|---|---|---|---|---|
| CTRL | `00000000` | `FFFFFFFF` | `00000F37` | `00000F37` | RW 필드 마스크만. reserved 는 비교 안 함 |
| CTRL | `FFFFFFFF` | `00000000` | `00000000` | `00000F37` | before 의 reserved 1 이 exp 로 새면 안 된다 |
| ID | `0D3A0102` | `FFFFFFFF` | `0D3A0102` | `FFFFFFFF` | RO = before 그대로 |
| STATUS | `000005F1` | `FFFFFFFF` | `00000500` | `00000F00` | BUSY·FIFO_LVL(volatile) 제외, ERR_CODE 만 |
| INT_STATUS | `00000007` | `00000005` | `00000002` | `00000007` | W1C: 1 쓴 bit0, bit2 만 지워짐 |
| INT_STATUS | `00000007` | `00000000` | `00000007` | `00000007` | W1C 에 0 쓰기는 무효 |
| CMD | `0000FF03` | `0000FF03` | `00000000` | `0000FF03` | WO = RAZ |
| ERR_CNT | `0000003C` | `000000FF` | `00000000` | `000000FF` | RC: before 읽기가 이미 지움, 쓰기는 무효 |
| SW_EVT | `0000000F` | `00000050` | `0000005F` | `000000FF` | W1S: OR |
| SRC_ADDR | `00000000` | `FFFFFFFF` | `FFFFFFFC` | `FFFFFFFF` | ADDR RW + ALIGN[1:0] RO 0 고정 |

몇 가지 설계 결정은 설명이 필요하다.

**RO 의 기대값을 리셋값이 아니라 `before` 로 둔다.** RO 필드는 하드웨어가 값을 정한다(ERR_CODE, 핀 스트랩, 퓨즈, 버전). R/W 테스트가 묻는 것은 "값이 맞나" 가 아니라 "**write 로 바뀌지 않았나**" 다 — 값이 맞는지는 리셋값 테스트(SIV-B03)의 몫이다. `before` 를 쓰면 RO 값이 리셋값과 달라도(예: 이전 테스트가 ERR_CODE 를 남김) 오탐이 없다.

**WO 는 RAZ 로 본다.** 스펙에 따라 WO 읽기가 "0", "마지막으로 쓴 값", "undefined" 중 하나다. 이 IP 는 RAZ 라고 정했으므로 care=1 로 비교한다. 스펙이 undefined 라면 care=0 이 맞다 — 그 경우 WO 필드는 R/W 테스트로 아무것도 검증할 수 없고, 기능 테스트(쓰면 동작이 일어나는가)로 넘어간다.

**RC 는 `before` 를 읽는 순간 지워진다.** 그래서 ⑤ 의 기대값은 0 이다. 이 한 줄이 RC 의 두 성질 — 쓰기는 무효, 읽으면 지워짐 — 을 동시에 검사한다. 단, 지울 값이 있어야(prime) 의미가 있다.

**volatile 필드는 care=0.** mock 은 STATUS 를 읽을 때마다 FIFO_LVL 을 올린다 (테스트 `reg_read32(0x008) != reg_read32(0x008)`). 실칩의 BUSY, FIFO 레벨, 카운터, 핀 입력 값도 마찬가지다. 이걸 비교하면 "가끔 실패하는" 테스트가 되고, 가끔 실패하는 테스트는 진짜 실패를 숨긴다.

> UVM RAL 에서 이 함수에 해당하는 것이 `uvm_reg_field` 의 예측(predict) 로직이다 — front-door write 를 관찰하면 필드의 access policy("RW", "RO", "W1C", "W1S", "RC", "WO" …)에 따라 mirror 값을 갱신하고, `mirror(…, UVM_CHECK)` 가 읽은 값을 mirror 와 비교한다. volatile 로 configure 한 필드는 그 비교에서 기본적으로 빠진다. post-silicon 에서는 이 mirror 를 C 로 손수 다시 만드는 셈이다.

### Part B — 무엇을 쓰고, 무엇을 먼저 세우나

```svg
<svg viewBox="0 0 740 266" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">CTRL (off 0x004) 에 0xFFFFFFFF 를 "안전하게" 쓰기 — 하위 12비트</text>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="90" y="46">11</text><text x="140" y="46">10</text><text x="190" y="46">9</text><text x="240" y="46">8</text>
    <text x="290" y="46">7</text><text x="340" y="46">6</text><text x="390" y="46">5</text><text x="440" y="46">4</text>
    <text x="490" y="46">3</text><text x="540" y="46">2</text><text x="590" y="46">1</text><text x="640" y="46">0</text>
  </g>
  <rect x="65" y="54" width="200" height="34" class="f-acc"/>
  <rect x="265" y="54" width="100" height="34" class="f-mut"/>
  <rect x="365" y="54" width="100" height="34" class="f-acc"/>
  <rect x="465" y="54" width="50" height="34" class="f-mut"/>
  <rect x="515" y="54" width="100" height="34" class="f-acc"/>
  <rect x="615" y="54" width="50" height="34" class="f-warn"/>
  <g stroke="currentColor" fill="none"><rect x="65" y="54" width="600" height="34"/></g>
  <g fill="currentColor" text-anchor="middle">
    <text x="165" y="76">BURST RW</text><text x="315" y="76">rsvd</text><text x="415" y="76">PRIO RW</text>
    <text x="490" y="76">rsvd</text><text x="565" y="76">MODE RW</text><text x="640" y="76">EN</text>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="20" y="116" text-anchor="start">pat</text>
    <text x="90" y="116">1</text><text x="140" y="116">1</text><text x="190" y="116">1</text><text x="240" y="116">1</text>
    <text x="290" y="116">1</text><text x="340" y="116">1</text><text x="390" y="116">1</text><text x="440" y="116">1</text>
    <text x="490" y="116">1</text><text x="540" y="116">1</text><text x="590" y="116">1</text><text x="640" y="116">1</text>
    <text x="20" y="142" text-anchor="start">wr</text>
    <text x="90" y="142">1</text><text x="140" y="142">1</text><text x="190" y="142">1</text><text x="240" y="142">1</text>
    <text x="290" y="142">0</text><text x="340" y="142">0</text><text x="390" y="142">1</text><text x="440" y="142">1</text>
    <text x="490" y="142">0</text><text x="540" y="142">1</text><text x="590" y="142">1</text><text x="640" y="142">0</text>
    <text x="20" y="168" text-anchor="start">care</text>
    <text x="90" y="168">1</text><text x="140" y="168">1</text><text x="190" y="168">1</text><text x="240" y="168">1</text>
    <text x="290" y="168">0</text><text x="340" y="168">0</text><text x="390" y="168">1</text><text x="440" y="168">1</text>
    <text x="490" y="168">0</text><text x="540" y="168">1</text><text x="590" y="168">1</text><text x="640" y="168">1</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="690" y="116">0xFFF</text><text x="690" y="142">0xF36</text><text x="690" y="168">0xF37</text>
    <text x="10" y="200">reserved(회색): 0 을 쓰고(RES0), 읽은 값은 비교하지 않는다.</text>
    <text x="10" y="218">EN(side): 1 을 쓰면 DMA 가 출발하므로 0 만 쓴다. 그래도 care 에는 남는다 (0 을 썼으니 0).</text>
    <text x="10" y="236">"써 볼 수 없는 비트" 와 "볼 수 없는 비트" 는 다르다.</text>
    <text x="10" y="254">EN 에 1 이 써지는지는 bring-up 기능 테스트(SIV-B10)의 몫.</text>
  </g>
</svg>
```

**쓰는 값은 세 종류로 걸러진다.**

| 비트 종류 | 쓰는 값 | 비교 | 이유 |
|---|---|---|---|
| reserved | 0 | 안 함 | 스펙상 의미 없음. 1 을 쓰면 미래 리비전에서 새 기능을 켤 수도 있다 (Arm 의 RES0 관습: 읽은 값에 의존하지 말고 0 또는 보존 값을 쓴다) |
| `F_SIDE` (EN, START, ABORT) | 0 | 함 | 테스트가 장치를 동작시키면 안 된다. 다음 레지스터 테스트의 전제가 깨지고, 실보드에선 버스 에러·행(hang)·메모리 오염 |
| 나머지 (RW/RO/WO/W1C/W1S/RC) | 패턴 그대로 | access 대로 | RO 에도 써야 "RO 가 써지는" 결함을 잡는다 |

실무 기술서에는 이 정보가 들어 있다. IP-XACT 의 필드에는 `volatile` 표기와 자동 테스트 가능 여부를 나타내는 `testable`(제약 포함) 표기가 있고, UVM 에서는 레지스터 단위로 `NO_REG_BIT_BASH_TEST`, `NO_REG_ACCESS_TEST`, `NO_REG_TESTS` 같은 resource 속성으로 자동 시퀀스에서 뺀다. 이 레슨의 `F_VOL` / `F_SIDE` 는 그 축약판이다.

**prime 없이는 W1C/RC 를 시험할 수 없다.** 리셋 직후 INT_STATUS 는 0 이다. 0 인 W1C 비트에 무엇을 쓰든 0 이 읽힌다 — "clear 경로가 끊긴" 결함이 있어도. 실제 테스트 결과:

| 결함 | prime=false | prime=true |
|---|---|---|
| W1C_NOCLEAR (INT_STATUS.ERR) | 0 fail (놓침) | 2 fail (0xFFFFFFFF, 0xAAAAAAAA) |
| RC_NOCLEAR (ERR_CNT) | 0 fail (놓침) | 4 fail (모든 패턴) |

리셋 상태에서만 돌리는 R/W 테스트는 W1C 에 대해 "1 을 써도 1 이 되지 않는다" — 즉 W1C 가 RW 로 구현된 경우 — 만 확인한다. clear 경로는 비트가 1 일 때만 보인다. 실보드에서 비트를 1 로 만드는 방법은 IP 의 interrupt force/test 레지스터, 루프백, 실제 이벤트 유발(타이머 만료, 잘못된 descriptor 로 ERR 유발)이다 (SIV-B11 참고).

**before 는 매 패턴마다 다시 읽는다.** ID[3] 이 써지는 결함에서 0xFFFFFFFF 패턴이 bit3 을 1 로 만든 뒤, 0x55555555 패턴은 bit3 에 0 을 쓴다. 이때 `before` 는 `0x0D3A010A`(오염된 값)이고 기대값도 그것이라, 실제 `0x0D3A0102` 와 다르다 → 또 실패. 실제 테스트에서 이 결함은 패턴 F, 5, A 에서 **3번** 실패했다. 같은 비트·같은 원인이 반복되는 것이므로, 리포트를 읽는 사람은 "실패 3줄 = 결함 1개" 로 묶어야 한다 (Q7).

**W1S 결함은 패턴 순서가 있어야 드러난다.** SW_EVT 를 RW 로 잘못 구현했을 때: 0x0 → 0 (정상과 같음), 0xFFFFFFFF → 0xFF (같음), 0x55555555 → RW 는 0x55, W1S 는 0xFF 유지 → **여기서 처음 실패**. 리셋 직후 패턴 하나씩만 돌리면 네 패턴 모두 놓친다 (아래 매트릭스 마지막 줄). W1S 는 "이미 1 인 비트에 0 을 써도 1 로 남는가" 를 봐야 RW 와 구별되는데, 그 상태는 앞 패턴이 만들어 준다.

### Part C — 검출 매트릭스와 리포트

결함 하나를 넣고, 리셋 직후 prime 을 켜고 패턴 **하나만** 돌렸을 때의 검출 여부 (테스트 `MX[]` 로 고정된 실제 결과). 마지막 열은 네 패턴을 순서대로 돌린 `rw_test_reg` 의 실패 수다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">결함 × 패턴 검출 매트릭스 (DMA0 mock, 리셋 직후 단일 패턴, prime)</text>
  <g fill="currentColor" font-weight="bold" text-anchor="middle" font-size="12">
    <text x="335" y="46">0x00000000</text><text x="425" y="46">0xFFFFFFFF</text>
    <text x="515" y="46">0x55555555</text><text x="605" y="46">0xAAAAAAAA</text><text x="690" y="46">4개 연속</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="74">RO_WRITABLE  ID[3] (리셋 0)</text>
    <text x="10" y="102">W1C_NOCLEAR  INT_STATUS[1]</text>
    <text x="10" y="130">W1C_AS_RW  INT_STATUS[1]</text>
    <text x="10" y="158">W1C_CLEAR_ALL  INT_STATUS[2:0]</text>
    <text x="10" y="186">WO_READBACK  CMD.TAG[15:8]</text>
    <text x="10" y="214">RC_NOCLEAR  ERR_CNT[7:0]</text>
    <text x="10" y="242">W1S_AS_RW  SW_EVT[7:0]</text>
  </g>
  <g>
    <rect x="292" y="58" width="86" height="24" class="f-mut"/><rect x="382" y="58" width="86" height="24" class="f-ok"/><rect x="472" y="58" width="86" height="24" class="f-mut"/><rect x="562" y="58" width="86" height="24" class="f-ok"/>
    <rect x="292" y="86" width="86" height="24" class="f-mut"/><rect x="382" y="86" width="86" height="24" class="f-ok"/><rect x="472" y="86" width="86" height="24" class="f-mut"/><rect x="562" y="86" width="86" height="24" class="f-ok"/>
    <rect x="292" y="114" width="86" height="24" class="f-ok"/><rect x="382" y="114" width="86" height="24" class="f-ok"/><rect x="472" y="114" width="86" height="24" class="f-ok"/><rect x="562" y="114" width="86" height="24" class="f-ok"/>
    <rect x="292" y="142" width="86" height="24" class="f-ok"/><rect x="382" y="142" width="86" height="24" class="f-mut"/><rect x="472" y="142" width="86" height="24" class="f-ok"/><rect x="562" y="142" width="86" height="24" class="f-ok"/>
    <rect x="292" y="170" width="86" height="24" class="f-mut"/><rect x="382" y="170" width="86" height="24" class="f-ok"/><rect x="472" y="170" width="86" height="24" class="f-ok"/><rect x="562" y="170" width="86" height="24" class="f-ok"/>
    <rect x="292" y="198" width="86" height="24" class="f-ok"/><rect x="382" y="198" width="86" height="24" class="f-ok"/><rect x="472" y="198" width="86" height="24" class="f-ok"/><rect x="562" y="198" width="86" height="24" class="f-ok"/>
    <rect x="292" y="226" width="86" height="24" class="f-warn"/><rect x="382" y="226" width="86" height="24" class="f-warn"/><rect x="472" y="226" width="86" height="24" class="f-warn"/><rect x="562" y="226" width="86" height="24" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="335" y="75">—</text><text x="425" y="75">검출</text><text x="515" y="75">—</text><text x="605" y="75">검출</text><text x="690" y="75">3 fail</text>
    <text x="335" y="103">—</text><text x="425" y="103">검출</text><text x="515" y="103">—</text><text x="605" y="103">검출</text><text x="690" y="103">2 fail</text>
    <text x="335" y="131">검출</text><text x="425" y="131">검출</text><text x="515" y="131">검출</text><text x="605" y="131">검출</text><text x="690" y="131">4 fail</text>
    <text x="335" y="159">검출</text><text x="425" y="159">—</text><text x="515" y="159">검출</text><text x="605" y="159">검출</text><text x="690" y="159">3 fail</text>
    <text x="335" y="187">—</text><text x="425" y="187">검출</text><text x="515" y="187">검출</text><text x="605" y="187">검출</text><text x="690" y="187">3 fail</text>
    <text x="335" y="215">검출</text><text x="425" y="215">검출</text><text x="515" y="215">검출</text><text x="605" y="215">검출</text><text x="690" y="215">4 fail</text>
    <text x="335" y="243">놓침</text><text x="425" y="243">놓침</text><text x="515" y="243">놓침</text><text x="605" y="243">놓침</text><text x="690" y="243">2 fail</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="274">어느 한 패턴도 모든 결함을 잡지 못한다. 0x0 = "1 이 0 이 되는" 결함, 0xF.. = "0 이 1 이 되는" 결함.</text>
    <text x="10" y="292">0x5../0xA.. 쌍 = 비트마다 두 극성 + 이웃 비트끼리 다른 값 (인접 short 는 SIV-B05).</text>
    <text x="10" y="310">상태가 필요한 결함(W1S_AS_RW)은 단일 패턴으로는 못 잡고, 패턴 순서가 잡는다.</text>
  </g>
</svg>
```

표로 다시 정리하면 — **결함 모델 → 무엇이 잡나**:

| 결함 모델 (흔한 RTL/통합 버그) | 잡는 조건 | 놓치는 경우 |
|---|---|---|
| RO 에 write enable 이 붙음 (상수 tie-off 대신 flop 으로 합성, 레지스터 생성기 access 오기) | RO 리셋값과 **반대 값** 을 쓰는 패턴 | 리셋값과 같은 값만 쓰는 패턴 |
| W1C clear 경로 끊김 | prime + 그 비트에 1 쓰기 (0xF.., 해당 극성의 0x5/0xA) | prime 없음 |
| W1C 를 RW 로 구현 | 아무 패턴 (prime 시). prime 없이도 "1 을 써서 1 이 읽힘" 으로 잡힘 | — |
| 아무 write 나 W1C 필드를 지움 (WC 로 구현) | prime + 0 쓰기 | 0xFFFFFFFF 만 쓰는 테스트 |
| WO 가 RAZ 가 아님 (write data 가 read mux 로 샘) | 0 이 아닌 값 쓰기 | 0x0 만 |
| RC 가 안 지워짐 | prime | prime 없음 |
| W1S 를 RW 로 구현 | 1 로 세운 뒤 0 쓰기 (패턴 순서) | 단일 패턴, 리셋 직후 |
| RW stuck-at, 비트 short | 0/F + 5/A — SIV-B05 의 walking 1s/0s 가 더 정확 | — |
| 주소 디코드 aliasing | 이 테스트로는 못 잡는 경우가 많다 → SIV-B06 | — |

**리포트는 행동 가능해야 한다.** 과제의 두 결함을 넣은 블록 런의 실제 출력:

```text
FAIL reg=ID off=0x000 pat=0xFFFFFFFF wr=0xFFFFFFFF exp=0x0D3A0102 act=0x0D3A010A diff=0x00000008 bits=[3] field=ID(RO)
  -> RO field took a write (write-enable on RO / tie-off wired to a flop?)
FAIL reg=ID off=0x000 pat=0x55555555 wr=0x55555555 exp=0x0D3A010A act=0x0D3A0102 diff=0x00000008 bits=[3] field=ID(RO)
  -> RO field took a write (write-enable on RO / tie-off wired to a flop?)
FAIL reg=ID off=0x000 pat=0xAAAAAAAA wr=0xAAAAAAAA exp=0x0D3A0102 act=0x0D3A010A diff=0x00000008 bits=[3] field=ID(RO)
  -> RO field took a write (write-enable on RO / tie-off wired to a flop?)
FAIL reg=INT_STATUS off=0x00C pat=0xFFFFFFFF wr=0x00000007 exp=0x00000000 act=0x00000002 diff=0x00000002 bits=[1] field=ERR(W1C)
  -> W1C bit did not clear on write-1 (clear path broken?)
FAIL reg=INT_STATUS off=0x00C pat=0xAAAAAAAA wr=0x00000002 exp=0x00000005 act=0x00000007 diff=0x00000002 bits=[1] field=ERR(W1C)
  -> W1C bit did not clear on write-1 (clear path broken?)
DMA0 rw_test: 11 regs x 4 patterns, 5 failure(s)
```

한 줄에 들어 있어야 하는 것과 이유:

| 항목 | 왜 |
|---|---|
| `reg=` 와 `off=` | 이름은 사람용, 오프셋은 JTAG/OpenOCD 에서 `mdw BASE+off` 로 바로 재현하는 용도 |
| `pat=` 와 `wr=` 둘 다 | 패턴과 실제 버스에 나간 값은 다르다. 재현은 `wr` 로 한다 |
| `exp=` `act=` `diff=` | `diff` 하나로 어느 비트인지, exp/act 로 방향(0→1 인지 1→0 인지)을 안다 |
| `bits=[3]` | 32비트 hex 를 머리로 풀지 않게. `[15:8]` 처럼 구간으로 묶으면 "필드 전체" 결함이 한눈에 보인다 |
| `field=ERR(W1C)` | RTL 담당자에게 넘길 단위. access type 이 있어야 기대값이 왜 그런지 설명된다 |
| `-> diagnose` | 첫 가설. 단정이 아니라 물음표 — 같은 증상이 다른 원인(예: 클럭 게이팅으로 write 가 안 먹음)일 수 있다 |

`diagnose` 가 "가장 낮은 실패 비트" 하나로 판단하는 것은 단순화다. `field=DONE(W1C),ERR(W1C),HALF(W1C)` 처럼 필드 전체가 같은 방향으로 틀리면 비트 버그가 아니라 **레지스터 단위 버그**(write strobe, 클럭, 블록 리셋 유지)를 먼저 의심한다. 실보드에서 블록 전체가 `0xDEADBEEF` 나 `0xFFFFFFFF` 로 읽히면 R/W 테스트 결과는 무의미하다 — 클럭 게이트·파워 도메인·리셋을 먼저 확인한다 (추가 연습 4, SIV-B10).

### 실제 검증 흐름에서의 위치

| 단계 | 이 테스트의 모습 |
|---|---|
| Pre-silicon (UVM 시뮬레이션) | `uvm_reg_hw_reset_seq`(리셋값) → `uvm_reg_bit_bash_seq`(비트마다 1/0 쓰고 policy 대로 확인) → `uvm_reg_access_seq`(front-door 쓰기 후 back-door 로 확인, 그 반대도). 기대값은 RAL mirror 가 계산 |
| 에뮬레이션/FPGA | 같은 C 테스트를 SoC CPU 펌웨어로 실행. 백도어가 일부 있음 |
| Post-silicon (첫 실리콘, 보드) | 백도어 없음. JTAG/OpenOCD 로 외부에서 읽고 쓰거나, 온칩 펌웨어가 이 C 코드를 돌려 UART/메모리에 로그. prime 은 force 레지스터나 실제 이벤트 |
| ATE (양산 테스트) | 레지스터 R/W 는 주로 스캔/BIST 가 대신하고, 기능 레지스터 접근은 짧은 패턴만 |
| SLT / 시스템 검증 | 드라이버 수준 기능 테스트. 레지스터 테스트는 bring-up 진단용으로 남음 |

같은 테스트가 단계마다 재사용되려면 `reg_read32` / `reg_write32` 를 함수 포인터 백엔드로 감싸고(SIV-B02, SIV-B13), 기술서 하나(SIV-B01)에서 RTL·RAL·C 헤더·이 테스트의 표가 모두 생성되어야 한다. 기술서와 RTL 이 따로 손으로 관리되면, 테스트가 실패했을 때 "칩이 틀렸나, 문서가 틀렸나" 부터 다퉈야 한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
# 블록 리포트(위 5. 풀이의 출력) 후 마지막 줄: 83/83 passed (exit 0)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san
# 83/83 passed, ASan/UBSan 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `if (read() != pat) FAIL` | RW 에서만 맞다. reserved·RO·WO·W1C 전부 가짜 FAIL (정상 칩에서 31개) | `if ((act ^ exp) & care)` — exp 는 access type 으로 계산 |
| 기대값을 `pat` 으로 계산 | 실제로 버스에 나간 값은 `safe_write_value(pat)` 이다 | `expect_after_write(r, before, wr, …)` |
| `before` 를 테스트 시작 때 한 번만 읽음 | RO 기대값·W1C/W1S 계산의 기준이 낡는다. 앞 패턴이 바꾼 상태를 모른다 | 매 패턴 write 직전에 읽기 |
| reserved 비트까지 비교 | 스펙상 값이 정해지지 않은 비트 — 다음 리비전에서 의미가 생기면 테스트가 깨진다 | care 에서 제외, 쓸 때는 0 |
| 명령 비트(START/ABORT/RESET)에 0xFFFFFFFF | 테스트가 장치를 동작시킨다. 이후 결과 전부 오염, 실보드는 hang | `F_SIDE` 필드는 항상 0, 기능 테스트로 분리 |
| W1C 를 리셋 상태에서만 시험 | 0 인 비트는 clear 경로를 보여 주지 않는다 | force/test 레지스터로 prime |
| volatile 필드를 비교 | 간헐적 실패 → 아무도 안 믿는 테스트 | care=0, 또는 "범위 안인가" 같은 별도 검사 |
| `(1u << width) - 1` | width 32 에서 UB. 32비트 필드(ID, 주소)가 흔하다 | `width >= 32 ? ~0u : …` |
| 테스트 후 원래 값을 그대로 되씀 | W1C 에 원래 값(1)을 쓰면 pending 이 지워지고, WO 명령은 재실행된다 | RW 필드만 복원 (추가 연습 3) |
| RC 레지스터를 디버거 watch 창에 띄워 둠 | 디버거의 읽기도 읽기다 — 테스트 중간에 값이 사라진다 | RC 는 watch 에서 제외 (C-M02 참고) |

## 8. 면접 Q&A

<details>
<summary>Q1. W1C 상태 레지스터에 0xFFFFFFFF 를 쓰고 읽으면 기대값은? 리셋 직후라면 이 테스트로 무엇을 증명한 건가요?</summary>

기대값은 `before & ~wr` — 1 을 쓴 모든 W1C 비트가 0. 리셋 직후 비트가 전부 0 이면 기대값도 0, 실제도 0 이라 **"1 을 써도 1 이 되지 않는다"**, 즉 W1C 가 RW 로 구현되지 않았다는 것만 증명한다. clear 경로는 증명하지 못한다. 꼬리질문 "그럼 어떻게?" → 비트를 먼저 1 로 만든다: interrupt force/test 레지스터, 루프백, 실제 이벤트. 그리고 0 을 써서 유지되는지(WC 로 잘못 구현), 1 을 써서 지워지는지, 0x5/0xA 로 비트별로 섞어서 한 비트만 지워지는지 본다.

</details>

<details>
<summary>Q2. RC(read-to-clear) 레지스터는 R/W 테스트나 리셋값 테스트에서 어떻게 다루나요?</summary>

R/W 테스트에서는 "write 직전 read 가 이미 지웠으니 다음 read 는 0, write 는 무효" 로 모델링하면 쓰기 무시와 읽기-클리어를 같이 검사할 수 있다. 단 prime 이 필요하다. 리셋값 테스트(SIV-B03)에서는 **첫 번째 읽기만** 리셋값과 비교할 수 있다 — 두 번째부터는 0 이다. 그래서 리셋값 테스트를 다른 테스트보다 먼저, 디버거 watch 없이 돌린다. 하드웨어가 계속 세우는 RC(이벤트 카운터)는 volatile 로 보고 비교에서 뺀다. 꼬리: "RC 와 W1C 중 설계자는 무엇을 선호하나?" → 디버거·다중 읽기에 안전한 W1C. RC 는 "읽기는 부작용이 없다" 는 가정을 깨서 디버그·투기적 읽기·검증 모두를 어렵게 한다.

</details>

<details>
<summary>Q3. bit-bash 가 W1C 상태나 명령 비트를 망가뜨리지 않으려면 어떻게 하나요?</summary>

① 기술서에 side-effect 필드를 표시하고(`F_SIDE`, IP-XACT 의 testable 제약, UVM 의 `NO_REG_BIT_BASH_TEST`) 그 비트에는 0 만 쓴다. ② reserved 비트에도 0. ③ 테스트 전후 상태 관리 — 테스트 후 복원은 **RW 필드만** 원래 값으로, W1C·W1S·WO 자리는 0 (W1C 에 원래 값을 쓰면 남아 있던 pending 이 지워진다). ④ 블록을 기능 상태가 아닌 곳(클럭 on, 기능 disable)에서 돌리고, 다른 마스터(DMA, 펌웨어 ISR)가 같은 레지스터를 만지지 않게 인터럽트를 막는다. 꼬리: "EN 비트는 영영 테스트 안 하나?" → R/W 테스트에선 0 쓰기만, 1 쓰기는 bring-up 기능 테스트에서 "켜면 BUSY 가 서는가" 로 검증.

</details>

<details>
<summary>Q4. RO 필드의 기대값을 리셋값으로 두나요, 직전 읽은 값으로 두나요?</summary>

R/W 테스트에서는 **직전 읽은 값**. 질문이 "write 가 RO 를 바꾸나" 이기 때문이다. 리셋값과의 비교는 리셋값 테스트의 일이고, RO 값은 스트랩·퓨즈·상태에 따라 리셋값과 다를 수 있다. 부작용: 결함이 값을 바꾼 뒤엔 다음 패턴이 그 오염된 값을 기준으로 삼아 "되돌아간 것" 도 실패로 찍힌다(이 레슨의 ID[3] 이 3번 실패한 이유). 꼬리: "RO 인데 하드웨어가 바꾸는 필드는?" → volatile, care=0. 꼭 보고 싶다면 "write 전후로 값이 패턴 쪽으로 움직였나" 같은 통계적 검사를 별도로.

</details>

<details>
<summary>Q5. 왜 0x00000000 / 0xFFFFFFFF / 0x55555555 / 0xAAAAAAAA 네 개인가요? 하나로는 안 되나요?</summary>

각 비트가 0 과 1 을 둘 다 받아야 "한 방향으로만 틀리는" 결함(stuck-at, RO 쓰기, WC 구현)을 잡는다 → 0/F 로 충분. 하지만 0/F 는 모든 비트가 같은 값이라 인접 비트 short·bridging 을 못 본다 → 이웃끼리 다른 값을 주는 5/A 체커보드. 5/A 쌍만으로도 비트별 양극성은 채워지지만, 0/F 는 "모든 비트 동시 토글" 로 전원·SSO(동시 스위칭) 노이즈나 필드 전체 버그를 보기 좋다. 꼬리 "그래도 못 잡는 건?" → 상태가 필요한 결함(W1S 를 RW 로: 앞 패턴이 1 로 세워야 함), 주소 aliasing(SIV-B06), 비트 간 coupling 의 일반형(walking 1s/0s — SIV-B05, March — SIV-B07).

</details>

<details>
<summary>Q6. WO 레지스터는 읽으면 뭐가 나와야 하나요? 스펙에 안 적혀 있으면?</summary>

스펙마다 다르다: RAZ(read-as-zero), 마지막으로 쓴 값, 다른 레지스터의 값(같은 주소에 read 는 STATUS, write 는 CMD 인 설계), undefined. 정의되어 있으면 그대로 기대값으로 쓴다. 이 레슨의 DMA0 는 RAZ 로 정의해서 "write data 가 read mux 로 샌다" 결함을 잡았다. 안 적혀 있으면 기대값을 지어내지 말고 care=0 으로 두고, **스펙 이슈로 올린다** — 검증 엔지니어의 산출물에는 칩 버그뿐 아니라 문서 구멍도 들어간다. 꼬리: "같은 주소가 read 는 STATUS, write 는 CMD 면?" → 기술서에 두 개의 뷰로 표현하고, R/W 테스트에서 그 주소는 제외(쓰면 명령 실행).

</details>

<details>
<summary>Q7. R/W 테스트가 실패했을 때 로그에 무엇을 남기고, 실패 수십 줄은 어떻게 읽나요?</summary>

한 줄에: 레지스터 이름, 오프셋(재현용), 패턴, 실제로 쓴 값, 기대값, 실제값, diff, 비트 목록, 필드와 access type, 원인 가설. 그리고 런 헤더에 보드/칩 리비전, 펌웨어 버전, 전압·온도·클럭 설정, 시드(SIV-B14). 읽을 때는 **(레지스터, 비트) 로 묶는다** — 같은 비트가 여러 패턴에서 실패하면 결함 하나다(ID[3] 3줄). 블록 전체가 같은 값(0xDEADBEEF, 0xFFFFFFFF, 0)이면 개별 비트가 아니라 클럭·리셋·파워·디코드 문제. 꼬리: "온도를 바꾸니 실패가 사라진다면?" → 로직 버그가 아니라 타이밍 마진 문제일 가능성 — shmoo(전압×주파수)로 경계를 그린다.

</details>

<details>
<summary>Q8. UVM RAL 의 bit-bash/access 시퀀스와 post-silicon C 테스트는 무엇이 다른가요?</summary>

같은 질문("access policy 대로 동작하나")을 다른 수단으로 묻는다. UVM 은 RAL mirror 가 policy 로 기대값을 예측하고, `uvm_reg_access_seq` 는 back-door(HDL 경로로 직접 읽기)로 front-door 쓰기 결과를 확인할 수 있다. 실리콘에는 back-door 가 없으므로 front-door write → front-door read 만 가능하고, 상태 비트를 세울 수단(force 레지스터)이 RTL 에 미리 있어야 한다 — **post-silicon 테스트 가능성은 설계 단계에서 결정된다**. 또 실리콘에서는 같은 테스트를 전압·온도·클럭 코너에서 수천 번 돌리고, 실패가 로직 버그인지 전기적 마진인지 구분해야 한다. 꼬리: "pre-silicon 에서 다 통과했는데 실리콘에서 RO 비트가 써진다면?" → RTL 과 실리콘 넷리스트의 차이(ECO, 합성 최적화로 tie-off 처리 변화), 스캔/테스트 모드 신호가 기능 모드에서 풀리지 않음 등을 의심.

</details>

## 9. 추가 연습

### 연습 1 — W0C(rc_w0)와 W1T(toggle) 기대값, 그리고 W1T 를 W1S 로 만든 결함

`expect_field(acc, before, wr, m)` 에 W0C(0 을 쓴 비트만 지움 — STM32 `TIMx_SR` 의 rc_w0)와 W1T(1 을 쓴 비트 반전)를 추가하라. 그리고 비트 하나짜리 W1T 를 W1S 로 잘못 구현한 장치에서, 리셋(0)부터 패턴을 순서대로 쓸 때 몇 번째 패턴에서 처음 잡히는지 구하라. `{0, F, 5, A}`, `{F, F}`, `{0, A}` 각각.

<details>
<summary>답 보기</summary>

W0C: `before & wr & m` (1 쓰기 무효, 0 쓰기는 지움). W1T: `(before ^ wr) & m`. W1T vs W1S 는 "이미 1 인 비트에 1 을 쓸 때" 만 다르다(W1T 는 0, W1S 는 1). `{0,F,5,A}`: F 가 bit0 을 1 로, 5 의 bit0=1 이 다시 1 을 쓰므로 인덱스 2 에서 검출. `{F,F}`: 인덱스 1. `{0,A}`: 0xA.. 의 bit0 은 0 이라 1 을 한 번도 못 쓴다 → -1(놓침). 체커보드 한쪽만 쓰면 절반의 비트가 한 극성만 받는다는 것과 같은 이야기다.

</details>

### 연습 2 — 패턴 집합의 품질 측정

`polarity_cover(pats, n)` — 0 과 1 을 둘 다 받아 본 비트 마스크, `adjacent_differ(pats, n)` — bit i 와 bit i+1 이 다른 값을 가져 본 i 의 마스크(bit 0..30). `{0,F}`, `{5,A}`, `{5}`, 네 패턴 각각의 값을 구하라.

<details>
<summary>답 보기</summary>

`saw1 |= p; saw0 |= ~p; cover = saw1 & saw0`. `adj |= (p ^ (p >> 1)) & 0x7FFFFFFF`. `{0,F}`: cover 전부, adjacent 0 — 이웃 short 를 못 본다. `{5,A}`: cover 전부, adjacent `0x7FFFFFFF`. `{5}` 하나: cover 0. 네 패턴: 둘 다 최대. 패턴 집합을 고를 때 이렇게 "어떤 결함 조건을 만족시키는가" 를 함수로 재 두면, 패턴을 줄이자는 요청(테스트 시간)에 근거 있게 답할 수 있다.

</details>

### 연습 3 — 테스트 후 복원값

R/W 테스트 전에 읽어 둔 `orig` 로 레지스터를 되돌릴 때 실제로 쓸 값 `restore_value(r, orig)` 를 작성하라. CTRL(EN[0], BURST[11:8] RW) `orig=0xFFFFF5A1`, INT_STATUS(W1C[2:0]) `orig=7`, CMD(WO) `orig=0xAB01`, MIX(RW[3:0], W1C[7:4], W1S[11:8]) `orig=0xFF9`.

<details>
<summary>답 보기</summary>

RW 필드만 원래 값, 나머지는 무해한 0: `orig & rw_mask`. 결과 `0x501`, `0`, `0`, `0x9`. W1C 에 원래 값 7 을 쓰면 남아 있던 pending 세 개를 지워 버리고, CMD 에 원래 값을 쓰면 START 를 다시 친다. W1S 에 0 은 무해하다(원래 1 인 비트는 W1S 라 테스트로도 내려가지 않았다). 단, W1C/RC 의 "원래 상태" 는 R/W 테스트로 이미 바뀌었으므로 복원할 수 없다 — 그래서 레지스터 테스트는 기능 테스트 **전에**, 블록 리셋 후에 돌린다.

</details>

### 연습 4 — R/W 테스트 전에 블록이 살아 있는지

`precheck(rd, id_off, id_exp)` — ID 를 두 번 읽어 `0xDEADBEEF`/`0xFFFFFFFF` 이거나 두 값이 다르면 `PRE_BUS_ERR`, 기대 ID 와 다르면 `PRE_ID_MISMATCH`, 아니면 `PRE_OK`. 살아 있는 블록, 클럭 게이트된 블록(0xDEADBEEF), 전원이 꺼진 블록(0xFFFFFFFF), 구 리비전(ID 끝자리 다름)에 대해 확인하라.

<details>
<summary>답 보기</summary>

클럭이 꺼진 블록의 읽기는 SoC 마다 다르게 보인다: 버스가 에러 응답을 주면 CPU 는 abort/bus fault 를 받고, 일부 인터커넥트는 0xDEADBEEF 같은 고정값이나 0xFFFFFFFF(PCIe 에서 디바이스가 응답하지 않을 때의 전형)를 돌려준다. 이 상태에서 R/W 테스트를 돌리면 모든 레지스터·모든 비트가 실패해서 리포트가 쓸모없어진다. 그래서 테스트 러너는 블록마다 precheck 를 먼저 하고, 실패하면 "SKIP: clock/power/reset" 으로 한 줄만 남긴다. ID 불일치는 칩 리비전과 기술서 버전이 다르다는 뜻 — 기대값 표 자체가 틀렸을 수 있다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 19/19 passed
```

## 10. 핵심 정리

- R/W 테스트의 비교식은 `(act ^ exp) & care` 이고, `exp` 는 **(직전 read, 실제 쓴 값, access type)** 의 함수다: RW=wr, RO=before, WO=0(RAZ), W1C=before&~wr, W1S=before|wr, RC=0.
- `care` 에서 reserved 와 volatile 을 뺀다. 쓰는 값에서 reserved 와 side-effect(명령·enable) 비트를 뺀다. RO 에는 일부러 쓴다.
- W1C·RC 는 먼저 1 로 세워야(prime: force 레지스터, 실제 이벤트) clear 경로를 시험할 수 있다. 리셋 상태만 보면 놓친다.
- 패턴 4개는 역할이 다르다: 0/F = 비트별 양극성·전체 토글, 5/A = 이웃 비트 다른 값. 상태가 필요한 결함(W1S as RW)은 패턴 순서가 잡는다.
- 리포트 한 줄 = reg, off, pat, wr, exp, act, diff, bits, field(access), 가설. 같은 (레지스터, 비트) 실패는 한 결함으로 묶는다.
- 블록 전체가 0xDEADBEEF/0xFFFFFFFF 면 비트 버그가 아니라 클럭·파워·리셋 — precheck 후 SKIP.
- pre-silicon 의 UVM `uvm_reg_bit_bash_seq`/`uvm_reg_access_seq`·RAL mirror 와 같은 질문을, 실리콘에선 back-door 없이 C 로 묻는다. 그래서 force 레지스터 같은 테스트 가능성은 설계 때 넣어야 한다.

## 참고자료

- Accellera SystemRDL 2.0 Specification — 필드 속성 `sw`, `hw`, `onread`(rclr …), `onwrite`(woclr, woset, wot, wzc …)
- IEEE 1685 (IP-XACT) — 필드 `access`, `modifiedWriteValue`, `readAction`, `volatile`, `testable`
- IEEE 1800.2 (UVM) Register Layer — `uvm_reg_field` access policy, predict/mirror, `uvm_reg_hw_reset_seq`, `uvm_reg_bit_bash_seq`, `uvm_reg_access_seq`, `uvm_mem_walk_seq`, `NO_REG_TESTS`/`NO_REG_BIT_BASH_TEST`/`NO_REG_ACCESS_TEST`
- Verification Academy UVM Cookbook — Registers 장 (내장 레지스터 테스트 시퀀스 사용법)
- Arm Architecture Reference Manual — RES0/RES1 정의 (reserved 비트에 쓰고 읽는 규칙)
- [OpenOCD User's Guide — `mdw`/`mww` 메모리 접근 명령](https://openocd.org/doc/html/General-Commands.html) — JTAG 로 레지스터를 읽고 쓰는 post-silicon 재현
- PCI Express Base Specification — 레지스터 속성 RO/RW/RW1C/RW1CS/HwInit 정의 (access type 이 스펙 수준으로 표준화된 예)
- 관련 레슨: SIV-B01 (레지스터 기술서·codegen), SIV-B02 (mock MMIO·access type 시맨틱), SIV-B03 (리셋값 테스트), SIV-B05 (walking 1s/0s, stuck-at/short), SIV-B06 (주소 디코드·aliasing), SIV-B08 (테스트 프레임워크·JSON 로그), SIV-B10 (bring-up), SIV-B11 (인터럽트 trigger-check-clear), SIV-B14 (리포트), C-M01 (MMIO volatile·레지스터 구조체), C-M02 (RMW 와 W1C), C-B04 (GET/SET_FIELD)
