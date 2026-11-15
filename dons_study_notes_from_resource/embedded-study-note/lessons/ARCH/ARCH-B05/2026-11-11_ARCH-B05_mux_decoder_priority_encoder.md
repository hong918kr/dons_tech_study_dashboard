---
id: ARCH-B05
time: 70분
updated: 2026-11-11
---
# ARCH-B05 조합 논리: MUX·디코더·우선순위 인코더

## 학습 목표

- 4:1 MUX, 3-to-8 디코더, 8-input 우선순위 인코더(priority encoder)를 **진리표(truth table) = 스펙, 게이트 식 = 구현** 으로 나눠 만들고, 입력이 작으니 **전수(exhaustive) 비교** 로 증명할 수 있다.
- 우선순위 인코더 출력과 **CLZ(count leading zeros)** 의 관계 `idx = 31 − clz(x)` 를 말하고, 2^32 개 입력 전부에서 `__builtin_clz` 와 같음을 확인한다. `clz(0)` 이 어디서는 32, 어디서는 UB 라는 차이를 안다.
- 작은 블록(penc8 · mux4)을 **계층으로 조립** 해 32-input 인코더를 만들고, "가장 낮은 1(CTZ)" 은 선을 거꾸로 꽂아(RBIT) 같은 회로로 얻는다.
- 펌웨어에서 이 회로들을 알아본다: 주소 디코더의 칩 셀렉트(74HC138), 인터럽트 컨트롤러의 승자 선택, RTOS ready 비트맵의 `31 − CLZ`.

## 1. 문제

"주소 디코더를 그려보세요", "인터럽트 8개 중 누가 이기나요?", "RTOS 스케줄러는 가장 높은 우선순위를 어떻게 O(1) 에 찾죠?" — 셋 다 답은 **조합 회로(combinational logic) 하나** 다. 실리콘 검증·FPGA·펌웨어 면접에서 이 블록들이 CPU 명령(CLZ), 칩 셀렉트 핀, NVIC 안에 어떻게 숨어 있는지 연결하는 질문이 자주 나온다.

**조합 회로** 는 출력이 **지금 입력만으로** 정해지는 회로다 — 상태(기억)가 없다. 기억이 있는 순차 회로(flip-flop, 카운터)는 ARCH-B06, 게이트를 지나는 지연(critical path)은 ARCH-B04·B13 이 맡는다. 여기서는 지연 0 인 이상적인 게이트로 **논리** 만 본다.

입력이 n 개면 가능한 입력 조합은 2^n 개뿐이다. 4:1 MUX 는 입력 6개(데이터 4 + 선택 2) → 64 행, 8-input 인코더는 256 행. 그러니 "대표 몇 개 테스트" 가 아니라 **모든 행을 비교** 해서 증명할 수 있다. 이게 하드웨어 검증의 출발점이다(큰 회로에서는 형식 검증·등가성 검사가 같은 일을 한다).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">세 가지 조합 블록 — 입력 수가 작아서 진리표 전체를 확인할 수 있다</text>
  <polygon points="70,50 140,75 140,175 70,200" class="f-acc" stroke="currentColor"/>
  <text x="105" y="130" fill="currentColor" text-anchor="middle" font-size="12">4:1 MUX</text>
  <g stroke="currentColor">
    <line x1="35" y1="75" x2="70" y2="75"/><line x1="35" y1="105" x2="70" y2="105"/><line x1="35" y1="145" x2="70" y2="145"/><line x1="35" y1="175" x2="70" y2="175"/>
    <line x1="95" y1="230" x2="95" y2="191"/><line x1="120" y1="230" x2="120" y2="182"/><line x1="140" y1="125" x2="175" y2="125"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="12" y="79">d0</text><text x="12" y="109">d1</text><text x="12" y="149">d2</text><text x="12" y="179">d3</text><text x="86" y="246">s1</text><text x="113" y="246">s0</text><text x="180" y="129">y</text>
  </g>
  <text x="20" y="272" fill="currentColor">y = d[s1 s0]</text>
  <text x="20" y="292" fill="currentColor" font-size="12">입력 6 → 64 행</text>
  <rect x="290" y="50" width="90" height="160" class="f-ok" stroke="currentColor"/>
  <text x="335" y="135" fill="currentColor" text-anchor="middle" font-size="12">3→8 DEC</text>
  <g stroke="currentColor">
    <line x1="255" y1="90" x2="290" y2="90"/><line x1="255" y1="120" x2="290" y2="120"/><line x1="255" y1="150" x2="290" y2="150"/><line x1="335" y1="240" x2="335" y2="210"/>
    <line x1="380" y1="60" x2="405" y2="60"/><line x1="380" y1="80" x2="405" y2="80"/><line x1="380" y1="100" x2="405" y2="100"/><line x1="380" y1="120" x2="405" y2="120"/><line x1="380" y1="140" x2="405" y2="140"/><line x1="380" y1="160" x2="405" y2="160"/>
    <line x1="380" y1="180" x2="405" y2="180"/><line x1="380" y1="200" x2="405" y2="200"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="232" y="94">a2</text><text x="232" y="124">a1</text><text x="232" y="154">a0</text><text x="326" y="256">en</text><text x="410" y="64">y0</text><text x="410" y="84">y1</text><text x="410" y="104">y2</text><text x="410" y="124">y3</text>
    <text x="410" y="144">y4</text><text x="410" y="164">y5</text><text x="410" y="184">y6</text><text x="410" y="204">y7</text>
  </g>
  <text x="235" y="272" fill="currentColor">en=1 이면 y[a] 하나만 1 (one-hot)</text>
  <text x="235" y="292" fill="currentColor" font-size="12">입력 4 → 16 행</text>
  <rect x="540" y="50" width="90" height="160" class="f-warn" stroke="currentColor"/>
  <text x="585" y="135" fill="currentColor" text-anchor="middle" font-size="12">8→3 PENC</text>
  <g stroke="currentColor">
    <line x1="515" y1="60" x2="540" y2="60"/><line x1="515" y1="80" x2="540" y2="80"/><line x1="515" y1="100" x2="540" y2="100"/><line x1="515" y1="120" x2="540" y2="120"/><line x1="515" y1="140" x2="540" y2="140"/><line x1="515" y1="160" x2="540" y2="160"/>
    <line x1="515" y1="180" x2="540" y2="180"/><line x1="515" y1="200" x2="540" y2="200"/><line x1="630" y1="90" x2="660" y2="90"/><line x1="630" y1="120" x2="660" y2="120"/><line x1="630" y1="150" x2="660" y2="150"/>
    <line x1="630" y1="190" x2="660" y2="190"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="497" y="64">i7</text><text x="497" y="84">i6</text><text x="497" y="104">i5</text><text x="497" y="124">i4</text><text x="497" y="144">i3</text><text x="497" y="164">i2</text><text x="497" y="184">i1</text><text x="497" y="204">i0</text>
    <text x="665" y="94">y2</text><text x="665" y="124">y1</text><text x="665" y="154">y0</text><text x="665" y="194">valid</text>
  </g>
  <text x="490" y="272" fill="currentColor">가장 높은 1 의 위치 (i7 최우선)</text>
  <text x="490" y="292" fill="currentColor" font-size="12">입력 8 → 256 행</text>
</svg>
```

**규칙** (starter.c 머리 주석): Part A 회로 함수는 게이트 식만 — 0/1 값에 `NOT / AND2 / OR2`. `d >> s`, `1u << a`, `for` 루프 같은 **동작 모델(behavioral model)** 은 정답표(`s_mux4`, `s_dec`, `s_penc`) 쪽만 쓴다. `BIT(v, n)` 으로 선 하나 꺼내기와 상수 시프트로 묶기는 "배선(wiring)" 이라 허용. 테스트에서 `__builtin_clz/ctz` 를 기준값으로 쓰지만, TODO 답에는 쓰지 않는다.

### Part A — 진리표와 게이트 모델

- `mux4(d0,d1,d2,d3,s1,s0)` — 곱의 합(SOP, sum of products) 4항.
- `dec3to8(en,a2,a1,a0)` — 출력 8개 각각 = en AND (주소 minterm 하나). 8비트로 묶어 반환.
- `penc8(in, &valid)` — i7 최우선. 출력 y2 y1 y0 와 valid. 입력 0 이면 valid=0, 반환 0.
- `tt_build(f, nin, tt)` — 입력 벡터 0 … 2^nin − 1 마다 `tt[in] = f(in)`. 하네스가 게이트 모델의 표와 정답표를 만들어 **모든 행** 을 비교한다 (64 / 16 / 256 행). 디코더는 "원-핫이거나 0" 성질도 따로 본다.

### Part B — CLZ 와 같다는 것, 계층 조립, 반대 방향

- `clz8(x)` — penc8 로. `x == 0 → 8`. 256 개 전부 `__builtin_clz(x) − 24` 와 비교 (0 은 builtin 이 UB 라 따로).
- `penc32(x, &valid)` — **penc8 4개 + 그룹 인코더(penc8 하나) + mux4 3개** 로 조립. `clz32(x)` = valid ? 31 − idx : 32.
  테스트: 0, 2^k, `0xFFFFFFFF >> k`, 16비트 전수를 세 자리(`x`, `x<<8`, `x<<16`)에 놓은 196 608 개, 무작위 100만 개 (앞자리 0 개수가 고르게 나오도록 무작위 시프트 — 그리고 clz 0…31 이 **모두 시험됐는지** 도 CHECK).
- `lowest8(x, &valid)` — 가장 낮은 1 (CTZ). 선을 거꾸로 꽂고 penc8 → 7 − idx. 256 개 전부 `__builtin_ctz` 와 비교.
- 관찰: `demo_exhaustive32.c` (2^32 전수), `demo_clz.c` (arm64 / Cortex-M4 / Cortex-M0 asm).

### Part C — 칩 셀렉트 · 인터럽트 선택 · RTOS 비트맵

- `hc138(g1, g2a_n, g2b_n, c, b, a)` — 74HC138 식: enable 3개(G1 high, G2A_n·G2B_n low) + active-low 출력. 64 행 전수.
- `chip_selects(addr, mreq_n)` — 64 KB 주소 공간을 A15..A13 으로 8 KB 뱅크 8개로. 전 주소(65 536) × MREQ_n 2 상태: 선택 중이면 **정확히 하나** 의 CS_n 만 0, 유휴면 전부 1.
- `irq_pick(pending, enable, &vec)` — 고정 우선순위, **IRQ0 최우선**. pending AND enable → lowest8. 65 536 조합 전수.
- `rdy_top(&r)` — 32비트 ready 비트맵에서 가장 높은 우선순위 (큰 숫자 = 높음, FreeRTOS 식) = penc32. 시나리오 시퀀스 + 무작위 set/clear 10만 번을 선형 탐색과 비교.

## 2. Problem (English)

> **Combinational building blocks and CLZ.**
>
> Model each circuit with gate-level Boolean equations (AND/OR/NOT on 0/1 values only — no table lookups, no variable shifts, no loops) and prove it against a behavioral truth table by exhaustive comparison.
>
> 1. `unsigned mux4(d0, d1, d2, d3, s1, s0)` → `d[2*s1 + s0]`.
> 2. `uint8_t dec3to8(en, a2, a1, a0)` → one-hot `1 << a` when `en`, else `0`.
> 3. `unsigned penc8(uint8_t in, unsigned *valid)` → index of the most significant set bit; `*valid = (in != 0)`; return 0 when `in == 0`.
> 4. `void tt_build(comb_fn f, unsigned nin, uint16_t *tt)` fills all `2^nin` rows.
> 5. Using only `penc8` and `mux4` blocks, build `penc32` and `clz32` (`clz32(0) == 32`), and `lowest8` (index of the least significant set bit). Show that `clz32(x) == __builtin_clz(x)` for every non-zero 32-bit `x`.
> 6. Firmware use: a 74HC138-style decoder generating active-low chip selects from A15..A13 gated by `MREQ_n`; a fixed-priority interrupt picker (IRQ0 highest) over `pending & enable`; and `rdy_top()` for a 32-level RTOS ready bitmap.
>
> Examples: `penc8(0x5A) == 6`, `clz32(0x00012345) == 15`, `chip_selects(0x2000, 0) == 0xFD`, `irq_pick(0x0C, 0xFB) → 3`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

실제 출력 (일부):

```text
  mux4: 64/64 rows differ, first in=0x00
FAIL starter.c:154  bad == 0
FAIL starter.c:155  mux4(0, 0, 1, 0, 1, 0) == 1
  dec3to8: 16/16 rows differ, first in=0x0
...
FAIL starter.c:261  t1 == 3 && t2 == 17 && t3 == 31 && t4 == 3 && t5 == 0
FAIL starter.c:273  bad == 0
4/29 passed
```

통과하는 4개는 "아무것도 없다" 를 기대하는 줄들(`penc8(0)`, `MREQ_n=1`, `irq_pick` 없음, `rdy_top` 빈 맵) — 스텁이 우연히 맞는다. 29/29 를 만든다.

## 4. 힌트

<details>
<summary>힌트 1 — MUX 는 "행마다 AND, 전체를 OR"</summary>

선택선 값 4가지 각각에 대해 "이 선택일 때만 1 인 항" 을 만든다: `~s1·~s0·d0`, `~s1·s0·d1`, `s1·~s0·d2`, `s1·s0·d3`. 넷 중 정확히 하나만 살아 있으니 OR 하면 끝.

</details>

<details>
<summary>힌트 2 — 디코더 = minterm 발생기</summary>

출력 k 는 주소가 k 일 때만 1 → a2 a1 a0 의 비트 패턴대로 원래 선 또는 NOT 선을 AND. 여기에 en 을 하나 더 AND. 8개를 `y7<<7 | … | y0` 로 묶는다(배선).

</details>

<details>
<summary>힌트 3 — 우선순위 인코더는 "위가 0 이어야" 를 덧붙인다</summary>

y2 = 1 인 경우는 답이 4…7 → `i7|i6|i5|i4`. y1 은 답이 2,3,6,7 → `i7|i6` 이거나, "i5·i4 가 0 이고 i3|i2". y0 은 답이 홀수(1,3,5,7) — 각 홀수 자리 i_k 에 "그보다 높은 짝수 자리들이 0" 을 붙인다. 더 높은 홀수 자리는 어차피 y0=1 이라 조건에서 빼도 된다(흡수).

</details>

<details>
<summary>힌트 4 — 32비트는 바이트 4개로 나눠서</summary>

바이트마다 penc8 → (idx_k, v_k). v3 v2 v1 v0 을 다시 penc8 에 넣으면 "1 이 있는 가장 높은 바이트 g". 최종 idx = g·8 + idx_g. idx_g 는 g 로 골라야 하니 **mux4** (비트마다 하나, 3개).

</details>

<details>
<summary>힌트 5 — 가장 낮은 1 은 선을 거꾸로</summary>

x 의 비트 순서를 뒤집은 r 에서 가장 높은 1 의 위치가 i 면, 원래 x 에서는 7 − i. 하드웨어로는 선을 반대로 연결하는 것뿐이라 공짜다 (ARM `RBIT` + `CLZ`).

</details>

## 5. 풀이

### Part A — 진리표 = 스펙, 게이트 식 = 구현

**4:1 MUX** — 진리표를 선택선 기준으로 접으면 4줄이다.

| s1 s0 | y | 곱 항 |
|---|---|---|
| 0 0 | d0 | `~s1 · ~s0 · d0` |
| 0 1 | d1 | `~s1 · s0 · d1` |
| 1 0 | d2 | `s1 · ~s0 · d2` |
| 1 1 | d3 | `s1 · s0 · d3` |

y = 네 항의 OR. 게이트 수: NOT 2 + AND3 4 + OR4 1. 74HC151(8:1 MUX) 같은 칩, FPGA 의 LUT(look-up table)가 바로 이 구조다 — **선택선 = 주소, 데이터 입력 = 저장된 진리표** 로 보면 MUX 하나가 작은 ROM 이다 (추가 연습 2 에서 mux4 하나로 3입력 함수 256 개를 전부 만든다).

**3-to-8 디코더** — 출력 k = en · (a2 a1 a0 == k 인 minterm). 그래서 디코더는 "모든 minterm 을 한꺼번에 만드는 회로" 이고, 디코더 + OR 게이트 = 임의의 3입력 함수(minterm 합)다. 성질: en=1 이면 1 이 **정확히 하나**(one-hot), en=0 이면 전부 0 — 테스트가 popcount 로 따로 본다. 칩 셀렉트에서 이 성질이 깨지면 두 칩이 버스를 동시에 구동한다(bus contention).

**8-input 우선순위 인코더** — 256 행을 X(don't care)로 접으면 9 행이다.

| i7 i6 i5 i4 i3 i2 i1 i0 | y2 y1 y0 | valid |
|---|---|---|
| 0 0 0 0 0 0 0 0 | 0 0 0 (정함) | 0 |
| 0 0 0 0 0 0 0 1 | 0 0 0 | 1 |
| 0 0 0 0 0 0 1 X | 0 0 1 | 1 |
| 0 0 0 0 0 1 X X | 0 1 0 | 1 |
| 0 0 0 0 1 X X X | 0 1 1 | 1 |
| 0 0 0 1 X X X X | 1 0 0 | 1 |
| 0 0 1 X X X X X | 1 0 1 | 1 |
| 0 1 X X X X X X | 1 1 0 | 1 |
| 1 X X X X X X X | 1 1 1 | 1 |

여기서 식을 읽는다 (74HC148 의 식과 같은 꼴, 단 148 은 active-low 입출력):

- `y2 = i7 + i6 + i5 + i4`
- `y1 = i7 + i6 + ~i5·~i4·(i3 + i2)`
- `y0 = i7 + ~i6·i5 + ~i6·~i4·i3 + ~i6·~i4·~i2·i1`
- `valid = i7 + … + i0`

y0 의 셋째 항을 정직하게 쓰면 `~i7·~i6·~i5·~i4·i3` 인데, i7=1 이면 첫 항이 이미 1, i5=1(그리고 i6=0)이면 둘째 항이 이미 1 → 둘 다 빼도 결과가 같다. 이런 "흡수" 로 줄인 식이 맞는지 손으로 확신하기는 어렵다. 그래서 **256 행 전수 비교** 가 의미 있다.

입력 0 일 때 출력은 don't care 지만, 모델에서는 0 으로 정했다. 그러면 `penc8(0x00)` 과 `penc8(0x01)` 의 인덱스가 둘 다 0 — **구별해 주는 건 valid 뿐** 이다. 일반 인코더(4:2 encoder)에 valid 가 없으면 "IRQ0 이 왔다" 와 "아무것도 안 왔다" 를 구별할 수 없다.

`tt_build` 는 루프 하나: `for (in = 0; in < (1u << nin); in++) tt[in] = f(in);`. 하네스는 표를 0xFFFF 로 미리 채워 두고, 채워지지 않은 행이 남으면 실패로 센다(행 수 실수 검출).

### Part B — 우선순위 인코더 = CLZ

32비트 x 에서 가장 높은 1 의 위치를 idx 라 하면, 그 위의 0 개수가 정확히 CLZ 다.

| x | 가장 높은 1 (idx) | CLZ | 관계 |
|---|---|---|---|
| `0x8000_0000` | 31 | 0 | idx + clz = 31 |
| `0x0001_2345` | 16 | 15 | |
| `0x0000_0001` | 0 | 31 | |
| `0` | 없음 (valid=0) | **32** (ARM `CLZ`) / **UB** (`__builtin_clz`) | 따로 처리 |

`0 ≤ idx ≤ 31` 이면 `31 − idx == idx ^ 31` 이다 (31 = 0b11111 에서 빼기는 받아내림이 없다). 그래서 컴파일러가 `eor #31` 을 내기도 한다 — 아래 asm 에서 보인다.

**계층 조립 (penc32)** — 회로를 키울 때 256 행짜리를 2^32 행으로 다시 설계하지 않고 블록을 쌓는다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b05-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">penc32 = penc8 ×4 (바이트별) + 그룹 인코더 + mux4 ×3</text>
  <g stroke="currentColor">
    <rect x="40" y="40" width="150" height="50" class="f-warn"/><rect x="210" y="40" width="150" height="50" class="f-warn"/>
    <rect x="380" y="40" width="150" height="50" class="f-warn"/><rect x="550" y="40" width="150" height="50" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="115" y="62">penc8 (x[31:24])</text><text x="115" y="80">→ idx3, v3</text><text x="285" y="62">penc8 (x[23:16])</text><text x="285" y="80">→ idx2, v2</text>
    <text x="455" y="62">penc8 (x[15:8])</text><text x="455" y="80">→ idx1, v1</text><text x="625" y="62">penc8 (x[7:0])</text><text x="625" y="80">→ idx0, v0</text>
  </g>
  <rect x="40" y="150" width="220" height="50" class="f-acc" stroke="currentColor"/>
  <text x="150" y="172" fill="currentColor" text-anchor="middle" font-size="12">그룹 인코더 = penc8</text>
  <text x="150" y="190" fill="currentColor" text-anchor="middle" font-size="12">입력 v3 v2 v1 v0 → g, valid</text>
  <rect x="380" y="150" width="320" height="50" class="f-ok" stroke="currentColor"/>
  <text x="540" y="172" fill="currentColor" text-anchor="middle" font-size="12">mux4 ×3 (idx 비트 2, 1, 0 각각)</text>
  <text x="540" y="190" fill="currentColor" text-anchor="middle" font-size="12">데이터 = idx0..idx3 의 같은 비트</text>
  <g stroke="currentColor" fill="none" stroke-dasharray="4 3">
    <line x1="70" y1="90" x2="80" y2="150"/><line x1="240" y1="90" x2="120" y2="150"/><line x1="410" y1="90" x2="170" y2="150"/><line x1="580" y1="90" x2="220" y2="150"/>
  </g>
  <g stroke="currentColor" fill="none">
    <line x1="160" y1="90" x2="420" y2="150"/><line x1="330" y1="90" x2="500" y2="150"/><line x1="500" y1="90" x2="580" y2="150"/><line x1="670" y1="90" x2="660" y2="150"/>
    <line x1="260" y1="175" x2="378" y2="175" marker-end="url(#b05-arr)"/><line x1="150" y1="200" x2="150" y2="248" marker-end="url(#b05-arr)"/>
    <line x1="540" y1="200" x2="540" y2="248" marker-end="url(#b05-arr)"/>
  </g>
  <text x="270" y="168" fill="currentColor" font-size="12">g1 g0 = 선택선</text>
  <text x="20" y="128" fill="currentColor" font-size="12">점선 = v (1 있나)</text>
  <text x="600" y="128" fill="currentColor" font-size="12">실선 = idx (3비트)</text>
  <rect x="40" y="250" width="660" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="370" y="275" fill="currentColor" text-anchor="middle" font-size="12">idx = g·8 + mux4(idx0..idx3; g)   ·   valid = 그룹 valid   ·   clz32 = valid ? 31 − idx : 32</text>
  <text x="10" y="318" fill="currentColor" font-size="12">예: x = 0x0001_2345 → v3=0, v2=1 → g = 2 → idx2 = penc8(0x01) = 0 → idx = 2·8 + 0 = 16, clz = 15</text>
</svg>
```

입력이 0 이면 v 가 전부 0 → g = 0, mux 는 idx0 = 0 을 고른다 → 반환 0, valid 0. 즉 "0 이면 0" 규칙이 조립 후에도 자동으로 유지된다.

**테스트 분포 함정** — 무작위 uint32 로 CLZ 를 시험하면 절반은 clz=0, 1/4 은 clz=1… clz ≥ 8 은 1/256 뿐이다. `demo_exhaustive32.c` 의 히스토그램이 그대로 보여준다(아래). 그래서 체크 테스트는 `r >> (무작위 0..31)` 로 앞자리 0 개수를 섞고, **clz 0…31 이 전부 한 번 이상 나왔는지** 를 CHECK 한다. 실리콘 검증의 "coverage" 개념과 같다 — 많이 돌렸다가 아니라 무엇을 덮었나.

**2^32 전수 확인** (Apple M6, Apple clang 21.0.0, `-O2`):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_exhaustive32.c -o demo_exhaustive32 && ./demo_exhaustive32
```

```text
inputs checked : 4294967296 (= 2^32)
mismatches     : 0
time           : 33.98 s (7.91 ns/input)
clz histogram  : clz=0 2147483648, clz=1 1073741824, clz=8 8388608, clz=24 128, clz=31 1, clz=32 1
```

게이트 모델 `clz32` 가 **모든** 32비트 입력에서 하드웨어 CLZ(`__builtin_clz`, 0 은 32 로 따로)와 같다. 체크 테스트에 넣기엔 34초라 demo 로 뺐다.

**CPU 에서는 명령 하나** — `demo_clz.c` (freestanding, `stdint.h` 만):

```bash
cc -O2 -S -o - demo_clz.c                                                          # arm64 (M6)
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_clz.c -o -  # Cortex-M4
clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_clz.c -o -  # Cortex-M0
```

arm64 (M6) — 지시어 줄만 걸러 냈다:

```text
_top_ready:                             ; @top_ready
	clz	w8, w0
	mov	w9, #31                         ; =0x1f
	sub	w0, w9, w8
	ret
_msb_index_safe:                        ; @msb_index_safe
	clz	w8, w0
	mov	w9, #31                         ; =0x1f
	sub	w0, w9, w8
	ret
_msb_index_loop:                        ; @msb_index_loop
	clz	w8, w0
	eor	w8, w8, #0x1f
	cmp	w0, #0
	csinv	w0, w8, wzr, ne
	ret
_lowest_set:                            ; @lowest_set
	rbit	w8, w0
	clz	w0, w8
	ret
```

Cortex-M4 (thumbv7em) — `top_ready` 도 아래 `msb_index_safe` 와 같은 3줄, `msb_index_loop` 는 `clz` + `eor #31` + `it eq / moveq #-1`:

```text
msb_index_safe:
	clz	r0, r0
	rsb.w	r0, r0, #31
	bx	lr
lowest_set:
	rbit	r0, r0
	clz	r0, r0
	bx	lr
```

읽을 거리 세 가지:

1. `msb_index_safe` 의 `x ? … : -1` 분기가 **사라졌다**. A64 `CLZ` 와 Armv7-M `CLZ` 는 입력 0 에 32 를 돌려주니 `31 − 32 = −1` 이 저절로 나온다 — 컴파일러가 그걸 알고 분기를 지웠다. C 수준의 `__builtin_clz(0)` 은 여전히 UB 라서 소스에서는 분기를 써야 한다.
2. 루프로 쓴 우선순위 인코더도 컴파일러가 알아보고 `clz` + `eor #31` 로 바꿨다 (`31 − c == c ^ 31`).
3. CTZ 전용 명령은 여기 없다 — **`rbit` + `clz`**. 우리 `lowest8` 의 "선 거꾸로 꽂기" 가 그대로 `rbit` 이다.

Cortex-M0 (Armv6-M) 에는 CLZ 명령이 **없다**:

```text
top_ready:
	push	{r7, lr}
	cmp	r0, #0
	beq	.LBB0_2
	bl	__clzsi2
	b	.LBB0_3
.LBB0_2:
	movs	r0, #32
.LBB0_3:
	movs	r1, #31
	subs	r0, r1, r0
	pop	{r7, pc}
...
lowest_set:
	cmp	r0, #0
	beq	.LBB3_3
	cmp	r0, #0
	beq	.LBB3_3
	rsbs	r1, r0, #0
	ands	r1, r0
	ldr	r0, .LCPI3_0
	muls	r0, r1, r0
	lsrs	r0, r0, #27
	adr	r1, .LCPI3_1
	ldrb	r0, [r1, r0]
	bx	lr
...
.LCPI3_0:
	.long	125613361                       @ 0x77cb531
```

`rsbs/ands` = `x & -x` (가장 낮은 1 만 남기기, C-B01 연습 3), 곱하기 `0x077CB531` (de Bruijn 수) 후 `>> 27` → 32바이트 표에서 위치를 읽는다.

CLZ 는 런타임 라이브러리 함수 `__clzsi2` 호출, CTZ 는 de Bruijn 곱셈 + 표 조회. 그래서 FreeRTOS 의 "포트 최적화 태스크 선택"(`configUSE_PORT_OPTIMISED_TASK_SELECTION`, `31 − CLZ`) 은 Cortex-M3/M4/M7 포트에 있고 Cortex-M0 포트에서는 쓰지 않는 것이 보통이다 (일반 C 방식으로 우선순위 리스트를 훑는다).

### Part C — 펌웨어 속 조합 회로

**칩 셀렉트 (74HC138)** — 주소 상위 비트를 디코더에 넣으면 메모리 맵이 된다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">A15..A13 → 74HC138 → CS0_n..CS7_n : 64 KB 를 8 KB 뱅크 8개로</text>
  <rect x="200" y="40" width="100" height="210" class="f-acc" stroke="currentColor"/>
  <text x="250" y="148" fill="currentColor" text-anchor="middle" font-size="12">74HC138</text>
  <g stroke="currentColor">
    <line x1="120" y1="80" x2="200" y2="80"/><line x1="120" y1="110" x2="200" y2="110"/><line x1="120" y1="140" x2="200" y2="140"/><line x1="150" y1="190" x2="200" y2="190"/><line x1="150" y1="215" x2="192" y2="215"/><line x1="150" y1="240" x2="192" y2="240"/>
  </g>
  <g stroke="currentColor" fill="none">
    <circle cx="196" cy="215" r="4"/><circle cx="196" cy="240" r="4"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="84">A15 → C</text><text x="10" y="114">A14 → B</text><text x="10" y="144">A13 → A</text><text x="10" y="194">G1 = 1</text><text x="10" y="219">G2A_n = MREQ_n</text><text x="10" y="244">G2B_n = 0</text>
  </g>
  <g stroke="currentColor">
    <line x1="308" y1="61" x2="420" y2="61"/><line x1="308" y1="85" x2="420" y2="85"/><line x1="308" y1="109" x2="420" y2="109"/><line x1="308" y1="133" x2="420" y2="133"/><line x1="308" y1="157" x2="420" y2="157"/><line x1="308" y1="181" x2="420" y2="181"/>
    <line x1="308" y1="205" x2="420" y2="205"/><line x1="308" y1="229" x2="420" y2="229"/>
  </g>
  <g stroke="currentColor" fill="none">
    <circle cx="304" cy="61" r="4"/><circle cx="304" cy="85" r="4"/><circle cx="304" cy="109" r="4"/><circle cx="304" cy="133" r="4"/><circle cx="304" cy="157" r="4"/><circle cx="304" cy="181" r="4"/><circle cx="304" cy="205" r="4"/><circle cx="304" cy="229" r="4"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="320" y="57">CS0_n</text><text x="320" y="81">CS1_n</text><text x="320" y="105">CS2_n</text><text x="320" y="129">CS3_n</text><text x="320" y="153">CS4_n</text><text x="320" y="177">CS5_n</text><text x="320" y="201">CS6_n</text><text x="320" y="225">CS7_n</text>
  </g>
  <g stroke="currentColor">
    <rect x="420" y="50" width="140" height="22" class="f-warn"/><rect x="420" y="74" width="140" height="22" class="f-warn"/><rect x="420" y="98" width="140" height="22" class="f-ok"/><rect x="420" y="122" width="140" height="22" class="f-ok"/>
    <rect x="420" y="146" width="140" height="22" class="f-mut"/><rect x="420" y="170" width="140" height="22" class="f-mut"/><rect x="420" y="194" width="140" height="22" class="f-acc"/><rect x="420" y="218" width="140" height="22" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="430" y="66">ROM (부트)</text><text x="430" y="90">ROM</text><text x="430" y="114">SRAM</text><text x="430" y="138">SRAM</text><text x="430" y="162">(비어 있음)</text><text x="430" y="186">(비어 있음)</text><text x="430" y="210">UART</text><text x="430" y="234">GPIO</text>
  </g>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="570" y="66">0x0000–0x1FFF</text><text x="570" y="90">0x2000–0x3FFF</text><text x="570" y="114">0x4000–0x5FFF</text><text x="570" y="138">0x6000–0x7FFF</text><text x="570" y="162">0x8000–0x9FFF</text><text x="570" y="186">0xA000–0xBFFF</text>
    <text x="570" y="210">0xC000–0xDFFF</text><text x="570" y="234">0xE000–0xFFFF</text>
  </g>
  <text x="10" y="276" fill="currentColor" font-size="12">동그라미 = active-low. MREQ_n = 1 (버스 유휴) 이면 CS 모두 1 — 주소가 바뀌는 중에 칩이 잘못 선택되지 않게 스트로브로 막는다.</text>
  <text x="10" y="294" fill="currentColor" font-size="12">A12..A0 는 각 칩 안으로 직접 (8 KB = 2^13). 비어 있는 뱅크 접근 → 아무도 응답 안 함 (bus fault 는 ARCH-B11).</text>
</svg>
```

`hc138` = `~dec3to8(G1 · ~G2A_n · ~G2B_n, C, B, A)`. 테스트는 65 536 주소 × 2 상태 전부에서 "선택 중이면 CS_n 중 정확히 하나만 0, 그게 `addr >> 13` 번" 을 확인한다. Cortex-M MCU 안의 버스 매트릭스·주변장치 브리지도 크게 보면 같은 일을 한다 (메모리 맵 전체 모델과 unmapped/misaligned 처리는 ARCH-B11, 부분 디코딩과 앨리어싱 검증은 SIV-B06).

**인터럽트 선택** — `pending & enable` 은 AND 게이트 8개, 그 결과에서 승자 고르기가 우선순위 인코더다. 이 모델은 **IRQ0 최우선** (8259 PIC 의 기본 고정 우선순위 방향, Cortex-M NVIC 에서 우선순위 값이 같을 때 번호 작은 쪽이 이기는 방향)이라 `lowest8` 을 쓴다. NVIC 처럼 우선순위를 **프로그래밍** 할 수 있으면 "가장 급한 레벨 찾기 → 그 레벨 안에서 번호 작은 IRQ" 두 단계가 된다 (추가 연습 5).

**RTOS ready 비트맵** — 우선순위마다 ready 태스크가 있으면 비트 1. 스케줄러는 "가장 높은 1" 하나만 알면 된다 → `rdy_top = penc32` = `31 − CLZ`. FreeRTOS 는 큰 숫자가 높은 우선순위라 CLZ, 낮은 숫자가 높은 우선순위인 체계(Linux RT 스케줄러의 비트맵, NVIC 우선순위 값)는 가장 낮은 1 = find-first-set(CTZ) 방향이다. 32 단계를 넘으면 비트맵을 두 단계로 쌓는다 — penc32 를 penc8 로 쌓은 것과 같은 계층 아이디어.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san
```

```text
29/29 passed
29/29 passed          (ASan + UBSan, 경고 없음)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 우선순위 인코더를 `y0 = i1 \| i3 \| i5 \| i7` 로 씀 | 그건 **일반 인코더** — 입력이 둘 이상 1 이면 섞인다 (0x06 → 3) | 각 항에 "더 높은 짝수 자리 0" 조건: `~i6·~i4·i3` … |
| valid 출력 없이 인덱스만 | `in=0` 과 `in=1` 이 둘 다 0 → "IRQ0" 과 "없음" 구별 불가 | valid 를 같이 내고, 쓰는 쪽은 valid 먼저 확인 |
| `31 - __builtin_clz(x)` 를 x=0 에도 호출 | `__builtin_clz(0)` 은 UB (ARM 명령은 32 지만 C 는 보장 안 함) | `x ? 31 - __builtin_clz(x) : -1` — 분기는 컴파일러가 지워 준다 (A64/M4) |
| 무작위 uint32 만으로 CLZ 테스트 | 절반이 clz=0, clz ≥ 8 은 1/256 → 큰 clz 가 거의 안 시험됨 | `r >> (rand & 31)` 로 섞고 clz 별 coverage 확인 |
| 디코더 출력 active-low 를 잊음 | 74HC138 은 선택된 출력이 **0**. `== 1` 로 검사하면 전부 반대 | `cs = ~dec3to8(...)`, 선택 판정은 `!(cs >> k & 1)` |
| 칩 셀렉트를 스트로브(MREQ_n / 버스 valid) 없이 주소만으로 | 주소 비트가 바뀌는 중간 값에서 엉뚱한 칩이 잠깐 선택됨 (글리치) | enable 에 스트로브를 넣는다 (G2A_n = MREQ_n) |
| `penc32` 하위 3비트를 `idx3 \| idx2 \| idx1 \| idx0` 로 합침 | 1 이 있는 바이트가 여럿이면 아래 바이트의 idx 가 OR 로 섞임 (0x0102_0000 → 틀린 값) | 그룹 인코더로 g 를 고르고 **mux** 로 idx_g 만 선택 |

## 8. 면접 Q&A

<details>
<summary>Q1. MUX 만으로 임의의 논리 함수를 만들 수 있나요? FPGA LUT 와 무슨 관계죠?</summary>

있다. n 입력 함수는 선택선 n 개짜리 2^n:1 MUX 의 데이터 입력에 진리표 상수 2^n 개를 꽂으면 된다 — 그게 LUT 다. 한 입력을 데이터 쪽으로 넘기면(Shannon 전개 f = ~c·f(c=0) + c·f(c=1)) 2^(n−1):1 MUX 로도 된다: 4:1 MUX 하나 + 데이터 입력 {0, 1, c, ~c} 로 3입력 함수 256 개 전부 (추가 연습 2).
꼬리질문 "Xilinx 7-series LUT6 하나에 들어가는 MUX 는?" → 입력 6개 = 데이터 4 + 선택 2 → **4:1 MUX 하나**. 더 큰 MUX 는 슬라이스 안의 전용 MUX(F7/F8)로 LUT 출력을 묶는다.

</details>

<details>
<summary>Q2. 인코더와 우선순위 인코더의 차이는? 하드웨어 어디에 쓰이나요?</summary>

일반 인코더는 입력이 정확히 하나만 1 이라고 가정한다 (one-hot → 이진). 둘 이상이면 출력이 OR 로 섞여 엉뚱한 번호가 나온다. 우선순위 인코더는 가장 높은(또는 낮은) 1 하나를 고르고 valid 를 낸다. 쓰임: 인터럽트 컨트롤러의 승자 선택, CLZ/CTZ 명령, 부동소수점 정규화(가수의 선행 1 위치 = 시프트 양, ARCH-B03), flash ADC 의 온도계 코드 → 이진 (추가 연습 4), 아비터(arbiter)의 고정 우선순위 grant.
꼬리질문 "고정 우선순위 아비터의 문제는?" → 낮은 우선순위가 굶는다(starvation) → round-robin 은 우선순위 시작점을 돌린다 (마스크 + 우선순위 인코더 두 개로 구현하는 것이 흔한 설계).

</details>

<details>
<summary>Q3. CLZ(0) 은 몇인가요? __builtin_clz(0) 은?</summary>

ARM `CLZ` (A64, Armv7-M) 는 0 에 대해 레지스터 폭(32 / 64)을 돌려준다. x86 `LZCNT` 도 폭을 돌려주지만 옛 `BSR` 은 입력 0 이면 ZF 를 세우고 목적지가 정의되지 않는다. GCC/Clang `__builtin_clz(0)` 은 **정의되지 않은 동작** — 그래서 `x ? … : 기본값` 을 쓴다. 이 맥 clang 은 그 분기를 `clz` + `sub` 로 지워 버렸다 (A64 CLZ(0) = 32 → 31−32 = −1 이 원하는 기본값과 같아서).
꼬리질문 "C23 에서는?" → `<stdbit.h>` 의 `stdc_leading_zeros` 류는 0 에 대해 폭을 돌려주도록 정의돼 있다 (이 레슨은 C11 이라 쓰지 않음).

</details>

<details>
<summary>Q4. RTOS 스케줄러가 최고 우선순위 ready 태스크를 O(1) 에 찾는 방법은? 우선순위가 256 개면?</summary>

우선순위별 ready 리스트 + "리스트가 비어 있지 않음" 비트맵. 최고 우선순위 = 비트맵의 가장 높은 1 = `31 − CLZ(bitmap)` (FreeRTOS Cortex-M3/M4 포트의 `portGET_HIGHEST_PRIORITY`). 태스크가 ready/block 될 때 해당 비트만 set/clear. 256 개면 2단계: 상위 8비트 맵(어느 32-그룹에 ready 가 있나) → CLZ → 그 그룹의 32비트 맵 → CLZ. 단계 수는 log₃₂(N) 이라 사실상 상수.
꼬리질문 "Cortex-M0 이면?" → CLZ 명령이 없어 `__clzsi2` 호출이나 표 조회가 된다 (위 asm). 그래서 M0 포트는 보통 일반 C 선택 방식을 쓴다.

</details>

<details>
<summary>Q5. 주소 디코딩에서 full decoding 과 partial decoding 의 차이는? 무엇을 검증해야 하나요?</summary>

full decoding 은 주소의 모든 상위 비트를 본다 → 장치 하나가 주소 하나에만 보인다. partial decoding 은 일부 비트만 봐서 게이트를 아끼지만, 안 본 비트만큼 같은 장치가 여러 주소에 반복해서 보인다(**앨리어싱**, aliasing). 검증: (1) 모든 주소에서 칩 셀렉트가 최대 하나 (버스 충돌 없음 — 이 레슨의 65 536 주소 전수 검사), (2) 앨리어스 주소에 쓰면 원래 주소 값이 바뀌는지 (SIV-B06), (3) 비어 있는 영역 접근이 bus fault 가 되는지 (ARCH-B11).
꼬리질문 "스트로브 없이 주소만 디코딩하면?" → 주소 비트가 동시에 바뀌지 않아 중간 값이 잠깐 디코딩된다 (글리치) → 쓰기 스트로브가 그 순간 겹치면 엉뚱한 칩에 써진다. 그래서 enable 에 MREQ/버스 valid 를 넣는다.

</details>

<details>
<summary>Q6. 2:1 MUX 에서 s 가 바뀔 때 d0 = d1 = 1 인데 출력이 잠깐 0 이 되는 이유는?</summary>

정적 해저드(static-1 hazard). y = d0·~s + d1·s 에서 s 가 1→0 이 될 때 `~s` 가 인버터 지연만큼 늦게 1 이 된다 → 잠깐 두 항이 모두 0 → 글리치. 고치는 법: 합의항(consensus term) `d0·d1` 을 추가 — 논리적으로는 중복이지만 전환 중에도 출력을 1 로 잡아 준다. 동기 회로에서는 글리치가 다음 클럭 에지 전에 가라앉으면(셋업 타임 안에서, ARCH-B13) 문제없다. 문제가 되는 곳은 **글리치가 그대로 클럭·리셋·칩 셀렉트·비동기 스트로브로 쓰이는 경우**.
꼬리질문 "그래서 클럭 게이팅을 AND 게이트로 하면?" → 글리치 클럭이 생긴다 → 래치 기반 ICG(integrated clock gating) 셀을 쓴다.

</details>

<details>
<summary>Q7. NVIC 는 우선순위가 같은 두 인터럽트 중 누구를 먼저 처리하나요? 하드웨어로는 어떻게 구현하죠?</summary>

Cortex-M: 우선순위 **값이 작을수록** 급하다. 값이 같으면 **예외 번호가 작은 쪽** 이 먼저. 하드웨어로는 대기 중인 IRQ 들의 (우선순위 값, 번호) 를 비교기 트리로 줄여 최솟값을 고른다 — 이 레슨 추가 연습 5 는 같은 결과를 "레벨별 비트맵 → 가장 급한 비어 있지 않은 레벨 → 그 안에서 lowest8" 로 만든다. 선점(preemption)은 그룹 우선순위만 보고, 서브우선순위는 대기 중일 때 순서만 정한다.
꼬리질문 "구현된 우선순위 비트 수가 칩마다 다른데 (예: STM32F4 는 4비트) 코드에서 주의할 점은?" → 값은 MSB 쪽에 정렬되어 하위 비트가 무시된다 → CMSIS `NVIC_SetPriority` 가 `__NVIC_PRIO_BITS` 만큼 시프트해 준다. 직접 레지스터에 쓸 때 시프트를 빼먹는 실수가 흔하다.

</details>

## 9. 추가 연습

### 연습 1 — NAND 만으로 2:1 MUX

NAND 게이트만 써서 `mux2(d0, d1, s)` 를 만들어라. 몇 개가 필요한가?

<details>
<summary>답 보기</summary>

4개. `ns = NAND(s, s)` (= NOT s), `y = NAND(NAND(d0, ns), NAND(d1, s))`. 드모르간: NAND(NAND(a), NAND(b)) = a + b 이므로 y = d0·~s + d1·s. NAND 는 **범용 게이트(universal gate)** — 표준 셀 라이브러리·CMOS 에서 NAND 가 기본 블록인 이유.

</details>

### 연습 2 — mux4 하나로 임의의 3입력 함수

`lut3(tt, a, b, c)`: tt 의 비트 `(a<<2 | b<<1 | c)` 가 f(a,b,c). 선택선은 a, b, 데이터 입력은 c 의 함수만.

<details>
<summary>답 보기</summary>

(a,b) = k 일 때 f 는 c 의 1변수 함수다: f0 = f(c=0), f1 = f(c=1) → 데이터 입력 k = `f0·~c + f1·c` ∈ {0, 1, c, ~c}. 256 개 함수 × 8 행 = 2048 행 전부 맞는다. 0xE8 = 다수결(majority, 전가산기 carry), 0x96 = 3입력 XOR(전가산기 sum, 패리티).

</details>

### 연습 3 — 3-to-8 두 개로 4-to-16 디코더

<details>
<summary>답 보기</summary>

a3 로 enable 을 나눈다: 아래 칩 enable = `en·~a3`, 위 칩 enable = `en·a3`, 나머지 주소 a2..a0 는 둘 다 공통. 출력 = hi<<8 | lo. 74HC138 두 개를 G1/G2A_n 에 A3 를 반대로 꽂는 전형적인 확장과 같다. 32 행 전수: 원-핫 또는 0.

</details>

### 연습 4 — 온도계 코드 → 이진 (flash ADC)

flash ADC 의 비교기 7개는 온도계 코드(thermometer code, `0000111` = 3)를 낸다. 3비트 이진으로 바꿔라. 버블(bubble) `0001011` 이면?

<details>
<summary>답 보기</summary>

비트 0 아래에 "항상 1" 선을 하나 더 꽂으면 penc8 결과가 바로 답: `penc8((t << 1) | 1)`. 정상 코드 8개 → 0..7. 버블 `0001011` 은 우선순위 인코더가 **4** (가장 높은 1 기준), 1 개수 세기(ones counter)는 **3**. 실제 flash ADC 는 인코더 앞에 3입력 버블 보정 게이트를 두거나 ones counter / Gray 인코딩으로 큰 오차를 막는다.

</details>

### 연습 5 — NVIC 식 프로그래머블 우선순위

`prio[8]` (0..3, 작을수록 급함), 같으면 IRQ 번호 작은 쪽. `nvic_pick(pending, enable, prio, &vec)`.

<details>
<summary>답 보기</summary>

레벨 0 부터: `bm_L` = prio 가 L 인 IRQ 비트맵 (설정 레지스터가 바뀔 때만 다시 계산되는 값), `act & bm_L` 이 0 이 아니면 그 안에서 `lowest8`. 시험: IRQ1(prio 2) vs IRQ5(prio 0) → 5, IRQ2·IRQ6 둘 다 prio 1 → 2, 무작위 20만 개를 직접 비교 정답표와 대조.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 13/13 passed
```

## 10. 핵심 정리

- 조합 회로 = 출력이 지금 입력만의 함수. n 입력이면 2^n 행 → **진리표 전수 비교** 로 증명 (MUX 64, 디코더 16, 인코더 256 행).
- MUX = 선택선이 주소인 작은 ROM: SOP `Σ (선택 minterm · d_k)`. LUT 와 같은 구조, 4:1 MUX = 6입력 LUT 하나.
- 디코더 = 모든 minterm 발생기 + enable. 칩 셀렉트는 **active-low + 스트로브 enable**, 성질은 "원-핫 또는 0".
- 우선순위 인코더 = "가장 높은 1" + **valid**. 입력 0 과 입력 1 을 가르는 건 valid 뿐.
- `idx = 31 − clz(x) = clz(x) ^ 31`. ARM `CLZ(0) = 32`, `__builtin_clz(0)` 은 UB. CTZ = `RBIT + CLZ`. Cortex-M0 엔 CLZ 가 없다.
- 큰 인코더는 작은 블록 + 그룹 인코더 + MUX 로 계층 조립 (RTOS 2단계 비트맵도 같은 생각).
- 무작위 테스트는 분포를 확인하라: uint32 무작위면 clz=0 이 절반.

## 참고자료

- Harris & Harris, *Digital Design and Computer Architecture: ARM Edition* — 2장 조합 논리 (MUX, 디코더, 타이밍·글리치)
- [TI SN74HC138 3-line to 8-line decoder/demultiplexer datasheet](https://www.ti.com/product/SN74HC138)
- [TI SN74HC148 8-line to 3-line priority encoder datasheet](https://www.ti.com/product/SN74HC148)
- [Arm Architecture Reference Manual for A-profile — CLZ, RBIT](https://developer.arm.com/documentation/ddi0487/latest)
- [Armv7-M Architecture Reference Manual — CLZ, NVIC 우선순위 규칙](https://developer.arm.com/documentation/ddi0403/latest)
- [GCC Other Builtins — `__builtin_clz` (x = 0 이면 정의되지 않음)](https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html)
- [FreeRTOS — `configUSE_PORT_OPTIMISED_TASK_SELECTION`](https://www.freertos.org/a00110.html)
- [AMD/Xilinx UG474 7 Series FPGAs CLB User Guide — LUT6, F7/F8 MUX](https://docs.amd.com/v/u/en-US/ug474_7Series_CLB)
- 소스: `starter.c` · `solution.c` · `practice.c` · `demo_exhaustive32.c` · `demo_clz.c`
