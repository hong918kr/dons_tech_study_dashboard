---
id: SIV-B02
time: 75분
updated: 2026-11-08
---
# SIV-B02 Mock MMIO 레지스터 파일과 접근 계층

## 학습 목표

- 레지스터 기술(필드 · lsb · width · access type)에서 비트별 **access class 마스크**(RW/RO/WO/W1C/W1S/RC)를 만들고, 그 마스크를 mock 레지스터 파일의 **읽기 경로 · 쓰기 경로**에 적용한다.
- 테스트 코드가 닿는 길을 `reg_read32 / reg_write32` 하나로 모으고, 그 아래를 **함수 포인터 bus ops** 로 갈아 끼워 mock · trace 데코레이터 · raw MMIO(volatile 포인터) 위에서 *같은 테스트* 를 돌린다.
- 버스 에러(DECERR/SLVERR)를 읽기 데이터(`0xDEADBEEF`)가 아니라 **별도 채널(sticky error)** 로 전달해야 하는 이유를 설명한다.
- access type 마다 "그 정의를 증명하는 최소 시퀀스" 를 쓰고, 각 시퀀스가 어떤 RTL 결함(W1C 를 RW 로 구현, RO 가 쓰기 가능, RC 가 안 지워짐 …)을 잡는지 결함 주입으로 보인다.
- 실패를 `FAIL reg=INT_STAT.OVR off=0x00C acc=W1C step=w0-keeps exp=… act=… diff=… bits=[1]` 처럼 **디버그 엔지니어가 바로 행동할 수 있는 한 줄** 로 남긴다.

## 1. 문제

첫 실리콘이 랩에 도착한 날 검증 엔지니어가 돌리는 건 *그날 짠 코드* 가 아니라 **몇 달 전에 mock 위에서 이미 다 디버그해 둔 코드** 다 — Rivian · NVIDIA · Apple 의 post-silicon validation 면접에서 "칩이 오기 전에 테스트를 어떻게 개발하고, 그게 실칩에서 그대로 돌게 하려면 어떻게 설계하나요?" 가 단골인 이유다.

핵심은 두 가지다. ① 테스트는 레지스터에 **딱 두 함수** `reg_read32(off)` / `reg_write32(off, val)` 로만 닿는다. ② 그 두 함수 아래는 `bus_ops_t` 라는 함수 포인터 묶음이라, 호스트에서는 **mock 레지스터 파일**, 실보드에서는 **volatile 포인터(raw MMIO)**, 랩 PC 에서는 JTAG 어댑터 등으로 바꿔 끼울 수 있다. mock 은 그냥 배열이 아니다 — 비트마다 **access type 시맨틱**(W1C 는 1 을 써야 지워지고, RC 는 읽으면 지워지고, WO 는 0 으로 읽히고 …)을 하드웨어처럼 흉내 내야, 그 위에서 통과한 테스트가 실칩에서도 의미가 있다. 그리고 mock 에 **결함을 주입** 할 수 있어야 "이 테스트가 정말 그 버그를 잡는다" 를 칩 없이 증명할 수 있다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">테스트는 한 번 쓰고, 백엔드만 바꾼다 — 접근 계층과 bus ops</text>
  <rect x="100" y="36" width="540" height="40" rx="6" stroke="currentColor" class="f-acc"/>
  <text x="370" y="61" fill="currentColor" text-anchor="middle">테스트 코드: test_field() · run_suite()  (SIV-B03~B07 이 여기에 얹힌다)</text>
  <rect x="100" y="96" width="540" height="40" rx="6" stroke="currentColor" class="f-ok"/>
  <text x="370" y="114" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">reg_read32(off) / reg_write32(off, v)</text>
  <text x="370" y="130" fill="currentColor" text-anchor="middle" font-size="12">접근 횟수 · 버스 에러(sticky, 첫 에러 + 오프셋) 집계</text>
  <rect x="100" y="156" width="540" height="40" rx="6" stroke="currentColor" fill="none"/>
  <text x="370" y="181" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">bus_ops_t { ctx, read32, write32, hw_set, reset }</text>
  <g stroke="currentColor" fill="none">
    <line x1="370" y1="76" x2="370" y2="96"/><line x1="370" y1="136" x2="370" y2="156"/>
    <line x1="370" y1="196" x2="370" y2="212"/><line x1="95" y1="212" x2="645" y2="212"/>
    <line x1="95" y1="212" x2="95" y2="226"/><line x1="280" y1="212" x2="280" y2="226"/>
    <line x1="460" y1="212" x2="460" y2="226"/><line x1="645" y1="212" x2="645" y2="226"/>
  </g>
  <rect x="10" y="226" width="170" height="62" rx="6" stroke="currentColor" class="f-warn"/>
  <text x="95" y="246" fill="currentColor" text-anchor="middle" font-weight="bold">mock 레지스터 파일</text>
  <text x="95" y="264" fill="currentColor" text-anchor="middle" font-size="12">access type 시맨틱</text>
  <text x="95" y="280" fill="currentColor" text-anchor="middle" font-size="12">+ 결함 주입 (fault_t)</text>
  <rect x="195" y="226" width="170" height="62" rx="6" stroke="currentColor" fill="none"/>
  <text x="280" y="246" fill="currentColor" text-anchor="middle" font-weight="bold">trace 데코레이터</text>
  <text x="280" y="264" fill="currentColor" text-anchor="middle" font-size="12">다른 bus 를 감싸</text>
  <text x="280" y="280" fill="currentColor" text-anchor="middle" font-size="12">모든 접근을 기록</text>
  <rect x="375" y="226" width="170" height="62" rx="6" stroke="currentColor" class="f-ok"/>
  <text x="460" y="246" fill="currentColor" text-anchor="middle" font-weight="bold">raw MMIO</text>
  <text x="460" y="264" fill="currentColor" text-anchor="middle" font-size="12">volatile uint32_t *</text>
  <text x="460" y="280" fill="currentColor" text-anchor="middle" font-size="12">온칩 FW · /dev/mem mmap</text>
  <rect x="560" y="226" width="170" height="62" rx="6" stroke="currentColor" class="f-mut"/>
  <text x="645" y="246" fill="currentColor" text-anchor="middle" font-weight="bold">(이 레슨 밖)</text>
  <text x="645" y="264" fill="currentColor" text-anchor="middle" font-size="12">JTAG/OpenOCD mdw/mww</text>
  <text x="645" y="280" fill="currentColor" text-anchor="middle" font-size="12">시뮬레이터 DPI 브리지</text>
  <text x="10" y="310" fill="currentColor" font-size="12">hw_set / reset 은 선택 항목: 트리거를 걸 수 없는 백엔드는 NULL → 그 테스트는 PASS 가 아니라 SKIP.</text>
</svg>
```

검증 대상은 가상의 SPI 컨트롤러 **SPI0** 다. 레지스터 표는 SIV-B01 이 JSON 에서 생성하는 표를 C 배열로 축약해 넣었다 (`reg_desc_t SPI0[]`).

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">SPI0 레지스터 맵 (비트 31 → 0, 한 칸 = 1비트, 빈 칸 = reserved: 0 으로 읽힘)</text>
  <g fill="currentColor" font-family="monospace"><text x="150" y="40">31</text><text x="306" y="40">15</text><text x="390" y="40">7</text><text x="460" y="40">0</text></g>
  <g fill="currentColor" font-family="monospace">
    <text x="10" y="64">0x000</text><text x="10" y="96">0x004</text><text x="10" y="128">0x008</text><text x="10" y="160">0x00C</text>
    <text x="10" y="192">0x010</text><text x="10" y="224">0x014</text><text x="10" y="256">0x018</text>
  </g>
  <g fill="currentColor" font-weight="bold">
    <text x="60" y="64">ID</text><text x="60" y="96">CTRL</text><text x="60" y="128">STATUS</text><text x="60" y="160">INT_STAT</text>
    <text x="60" y="192">INT_EN</text><text x="60" y="224">TXDATA</text><text x="60" y="256">ERR_CNT</text>
  </g>
  <g stroke="currentColor" fill="none" opacity=".35">
    <rect x="150" y="48" width="320" height="22"/><rect x="150" y="80" width="320" height="22"/>
    <rect x="150" y="112" width="320" height="22"/><rect x="150" y="144" width="320" height="22"/>
    <rect x="150" y="176" width="320" height="22"/><rect x="150" y="208" width="320" height="22"/>
    <rect x="150" y="240" width="320" height="22"/>
  </g>
  <g stroke="currentColor">
    <rect x="150" y="48" width="160" height="22" class="f-mut"/><rect x="390" y="48" width="80" height="22" class="f-mut"/>
    <rect x="310" y="80" width="80" height="22" class="f-ok"/><rect x="440" y="80" width="20" height="22" class="f-ok"/><rect x="460" y="80" width="10" height="22" class="f-ok"/>
    <rect x="390" y="112" width="40" height="22" class="f-mut"/><rect x="450" y="112" width="10" height="22" class="f-mut"/><rect x="460" y="112" width="10" height="22" class="f-mut"/>
    <rect x="440" y="144" width="10" height="22" class="f-warn"/><rect x="450" y="144" width="10" height="22" class="f-warn"/><rect x="460" y="144" width="10" height="22" class="f-warn"/>
    <rect x="450" y="176" width="10" height="22" class="f-acc"/><rect x="460" y="176" width="10" height="22" class="f-acc"/>
    <rect x="390" y="208" width="80" height="22" fill="none" stroke-dasharray="4 3"/>
    <rect x="390" y="240" width="80" height="22" class="f-warn" stroke-dasharray="4 3"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="230" y="63">PART = 0x5350 (RO)</text><text x="430" y="63">REV=02</text>
    <text x="350" y="95">CLKDIV=4</text><text x="410" y="127">RXCNT</text>
    <text x="430" y="223">DATA (WO)</text><text x="430" y="255">CNT (RC)</text>
  </g>
  <g fill="currentColor">
    <text x="480" y="64">reset 0x53500002 · 칩 ID/리비전</text>
    <text x="480" y="96">EN[0] MODE[2:1] CLKDIV[15:8] — RW</text>
    <text x="480" y="128">BUSY[0] TXFULL[1] RXCNT[7:4] — RO</text>
    <text x="480" y="160">DONE[0] OVR[1] UDR[2] — W1C</text>
    <text x="480" y="192">DONE_EN[0] OVR_EN[1] — W1S</text>
    <text x="480" y="224">쓰면 TX FIFO 로, 읽으면 0</text>
    <text x="480" y="256">에러 카운터, 읽으면 0 이 됨</text>
  </g>
  <g stroke="currentColor"><rect x="10" y="280" width="14" height="12" class="f-ok"/><rect x="80" y="280" width="14" height="12" class="f-mut"/>
    <rect x="150" y="280" width="14" height="12" class="f-warn"/><rect x="240" y="280" width="14" height="12" class="f-acc"/></g>
  <g fill="currentColor"><text x="28" y="291">RW</text><text x="98" y="291">RO</text><text x="168" y="291">W1C / RC</text><text x="258" y="291">W1S</text>
    <text x="320" y="291">점선 = 읽기 값이 저장값과 다름 (WO, RC)</text>
    <text x="10" y="312">0x01C 이후는 구멍(hole) — 읽기/쓰기 모두 DECERR, 읽기 데이터 0xDEADBEEF</text></g>
</svg>
```

### Part A — 레지스터 파일과 접근 계층

- `masks_from_fields(r, &m)` — 필드 목록을 비트별 access class 마스크 6개로. 폭 0, 32비트 초과, **필드 겹침** 이면 false (SIV-B01 lint 의 최소판).
- `mock_read32(ctx, off, &val)` — 없는 주소(구멍·비정렬)는 `BUS_DECERR`, 결함으로 지정된 레지스터는 `BUS_SLVERR` (둘 다 `*val = 0xDEADBEEF`). 정상이면 WO 비트는 0 으로 읽히고, **읽은 뒤** RC 비트가 지워진다.
- `mock_write32(ctx, off, v)` — RW·WO 는 교체, W1C 는 1 쓴 비트만 지움, W1S 는 1 쓴 비트만 켬, RO·RC·reserved 는 무시.
- `reg_read32 / reg_write32` — `g_bus` 로 디스패치, 읽기·쓰기·에러 횟수를 세고, **처음 난** 버스 에러와 그 오프셋을 sticky 로 남긴다 (`bus_err_take()` 가 꺼내며 지움).

결함 주입(`fault_t`, `eff_masks`)과 HW 이벤트 `mock_hw_set`, 블록 리셋 `mock_reset` 은 주어진다. 결함 하나 = "이 레지스터의 이 비트들을 다른 access class 로 *잘못 구현* 했다" 이다.

### Part B — access type 별 단위 테스트

`test_field(r, f, &fail)` 는 필드 하나를 그 access type 의 **정의대로** 검사해 `T_PASS / T_FAIL / T_SKIP` 을 돌려준다. 시퀀스(단계 이름은 리포트에 그대로 찍힌다):

| access | 시퀀스 (값은 필드 비트만 비교) | 트리거 필요 |
|---|---|---|
| RW | `w1`: 필드 전부 1 쓰기 → `w1-read` = 1 / `w0` → `w0-read` = 0 | 아니오 |
| RO | `read` v0 → `write` ~v0 → `write-ignored` = v0 | 아니오 |
| WO | `write` 1 → `raz` = 0 | 아니오 |
| W1C | hw_set → `set` = 1 / `w0` → `w0-keeps` = 1 / `w1` → `w1-clears` = 0 | 예 |
| W1S | `read` v0 / `w0` → `w0-noop` = v0 / `w1` → `w1-sets` = 1 / `w0` → `w0-keeps` = 1 | 아니오 |
| RC | hw_set → `read1` = 1 → `read2-clears` = 0 | 예 |

버스 에러가 나면 그 단계에서 즉시 실패(`bus=SLVERR rdata=0xDEADBEEF`). `hw_set` 이 NULL 인 백엔드에서 W1C·RC 는 SKIP.

### Part C — SPI0 스위트, 결함 주입 캠페인, 백엔드 교체

- `fmt_bits(diff)` — 1 인 비트를 높은 쪽부터 `"31,9:8,3:0"` (연속 구간은 `hi:lo`).
- `fmt_fail(&f)` — 리포트 한 줄. 버스 에러면 `bus=… rdata=…`, 아니면 `exp/act/diff/bits`.
- `run_suite(regs, n, report)` — 필드마다 블록 리셋 → `test_field` → 집계, 실패 줄을 report 에.
- `mmio_read32 / mmio_write32` — 실보드 백엔드: `volatile uint32_t *base` 창을 그대로 읽고 쓴다.

하네스는 ① 결함 없는 mock = 15/15 PASS ② 결함 6종을 하나씩 넣어 **정확한 레지스터 · 필드 · 단계 · 비트** 가 보고되는지 ③ 같은 스위트를 raw MMIO 백엔드(창 뒤가 그냥 RAM)에 붙였을 때 RAM 이 레지스터 파일이 아님을 테스트가 알아채는지 ④ trace 데코레이터로 W1C 테스트의 버스 접근 순서를 확인한다.

## 2. Problem (English)

> **Mock MMIO register file and access layer for silicon validation.**
>
> A register block is described by a table of registers (`name, offset, reset`) and fields (`name, lsb, width, access`), where access ∈ {RW, RO, WO, W1C, W1S, RC}. Validation tests must reach registers only through `uint32_t reg_read32(uint32_t off)` and `void reg_write32(uint32_t off, uint32_t v)`, which dispatch through a `bus_ops_t` of function pointers `{ctx, read32, write32, hw_set, reset}` so the same tests run on a host mock, a raw MMIO window, or a debug probe.
>
> 1. Build per-access-class bit masks from the field table (reject zero-width, out-of-range and overlapping fields). Implement the mock's read path (WO reads as zero, RC clears after read, holes return DECERR with read data `0xDEADBEEF`) and write path (RW/WO replace, W1C clears written ones, W1S sets written ones, RO/RC ignore). Implement `reg_read32/reg_write32` with access counters and a sticky first-error + offset.
> 2. Write `int test_field(const reg_desc_t*, const field_desc_t*, fail_t*)` that proves one field's access type with a minimal sequence and returns PASS / FAIL / SKIP (SKIP when the backend cannot trigger hardware events).
> 3. Run all fields of an SPI controller, inject one fault at a time (W1C implemented as RW, RO writable, RC not clearing, WO reading back, W1S as RW, SLVERR on a register) and show each is reported as exactly one line such as `FAIL reg=INT_STAT.OVR off=0x00C acc=W1C step=w0-keeps exp=0x00000002 act=0x00000000 diff=0x00000002 bits=[1]`. Then swap in a raw-MMIO backend.
>
> Example: after `hw_set(INT_STAT, 0x7)`, `reg_write32(0x00C, 0x2)` → `reg_read32(0x00C) == 0x5`. `reg_read32(0x01C)` returns `0xDEADBEEF` and the sticky error is `DECERR` at `0x01C`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 실제 출력 마지막 줄: 24/90 passed (exit 1) — TODO 11곳을 채워 90/90 을 만든다
```

TODO 는 Part A 5곳(`masks_from_fields`, `mock_read32`, `mock_write32`, `reg_read32`, `reg_write32`), Part B 1곳(`test_field`), Part C 5곳(`fmt_bits`, `fmt_fail`, `run_suite`, `mmio_read32`, `mmio_write32`)이다. `step_expect` / `step_write` (한 단계 읽기-비교 / 쓰기 + 실패 기록)는 주어진다 — Part B 에서 고민할 건 "어떤 순서로 무엇을 쓰고 읽어야 그 access type 이 증명되나" 뿐이다.

관찰용 파일 (하네스와 별개, `main` 없음):

```bash
clang -std=c11 --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -   # 직접 MMIO vs bus ops 의 asm
```

## 4. 힌트

<details>
<summary>힌트 1 — 쓰기 경로는 class 별 한 줄</summary>

`wr = rw | wo` 라 하면 `s = (s & ~wr) | (v & wr)` (교체), `s &= ~(v & w1c)` (1 쓴 W1C 지움), `s |= v & w1s` (1 쓴 W1S 켬). RO·RC·reserved 는 어느 식에도 안 나오므로 자동으로 무시된다. 마스크가 서로 겹치지 않는다는 보장(`masks_from_fields` 의 겹침 검사) 덕분에 순서는 상관없다. 비트 의미 자체는 C-M02 의 `apply_write` 와 같다.

</details>

<details>
<summary>힌트 2 — 읽기 경로: 돌려줄 값과 남길 값은 다르다</summary>

돌려줄 값 = `store & ~wo` (WO 는 RAZ). 남길 값 = `store & ~rc` (RC 는 읽기가 부작용). 값을 먼저 계산하고 그다음 지운다. 구멍/SLVERR 이면 `*val = BUS_DEAD` 를 채우고 음수를 돌려준다 — 호출자가 에러를 무시해도 쓰레기 대신 눈에 띄는 패턴이 남게.

</details>

<details>
<summary>힌트 3 — W1C 는 "먼저 1 로 만들어야" 시험할 수 있다</summary>

리셋 직후 W1C 비트는 0 이다. 여기서 "1 쓰기 → 0 읽힘" 을 확인하면 **RO-0, stuck-at-0, 아예 없는 비트** 도 통과한다. 그래서 `hw_set` 으로 1 을 만든 뒤 ① 0 쓰기로 안 지워짐 ② 1 쓰기로 지워짐 두 방향을 본다. `hw_set` 이 없으면 PASS 가 아니라 SKIP 이다. RC 도 같다.

</details>

<details>
<summary>힌트 4 — RO 는 "읽은 값의 반대" 를 쓴다</summary>

RO 필드에 `0xFFFFFFFF` 를 쓰면, 리셋값 비트가 1 인 자리는 쓰기 가능 결함이 있어도 값이 그대로라 못 잡는다. `~v0` 를 써야 모든 비트가 "바뀌려는 시도" 를 받는다. W1S 는 0 쓰기가 무시되는지(`w0-noop`, `w0-keeps`)가 핵심이다 — RW 로 잘못 만들면 0 쓰기에서 지워진다.

</details>

## 5. 풀이

### Part A — 마스크 6개가 곧 "하드웨어"

필드 표를 비트별 마스크로 바꾸면 mock 의 read/write 는 마스크 연산 몇 줄이 된다. SPI0 의 마스크 (Part A 첫 CHECK 들로 확인):

| 레지스터 | rw | ro | wo | w1c | w1s | rc |
|---|---|---|---|---|---|---|
| ID | 0 | `FFFF00FF` | 0 | 0 | 0 | 0 |
| CTRL | `0000FF07` | 0 | 0 | 0 | 0 | 0 |
| STATUS | 0 | `000000F3` | 0 | 0 | 0 | 0 |
| INT_STAT | 0 | 0 | 0 | `7` | 0 | 0 |
| TXDATA | 0 | 0 | `FF` | 0 | 0 | 0 |

`masks_from_fields` 의 거부 조건 세 개(폭 0, `lsb + width > 32`, 겹침)는 단순한 방어가 아니다. 겹침을 허용하면 한 비트가 두 class 에 속해 쓰기 경로의 결과가 **식의 순서** 에 따라 달라진다 — mock 이 기술서가 아니라 구현 세부를 따라가게 된다. 기술서 lint 를 제대로 하는 건 SIV-B01 의 일이고, 여기서는 mock 이 잘못된 표로 *조용히* 돌지 않도록 막는 최소한만 한다. `lsb + width` 는 `uint8_t` 끼리 더해도 `int` 로 승격되므로(C-B02) `28 + 8 = 36 > 32` 가 정확히 걸린다.

**읽기 경로** (`mock_read32`) — Part A 의 CHECK 들이 확인하는 동작 (요약):

```text
reset 후 ID      → 0x53500002          CTRL ← 0xFFFFFFFF → 0x0000FF07 (RW 비트만, reserved 는 0)
ID   ← 0xFFFFFFFF → 0x53500002 (RO)    TXDATA ← 0xA5 → 읽기 0, 그러나 store = 0xA5 (WO 는 래치됨)
INT_STAT: hw_set 0x7, ← 0x2 → 0x5      INT_EN ← 1, ← 2 → 3, ← 0 → 3 (W1S 누적, 0 무시)
ERR_CNT: hw_set 0x2A → 0x2A, 다시 → 0  ERR_CNT ← 0xFF → 0 (RC 는 쓰기 무시)
0x01C (구멍) → 0xDEADBEEF, DECERR      0x006 (비정렬) → DECERR (이 mock 은 정확한 오프셋만 디코드)
```

WO 의 "읽기 0, 그러나 저장됨" 이 중요하다. 테스트가 frontdoor(버스)로는 WO 쓰기가 *먹었는지* 를 알 수 없다는 뜻이고, 그래서 추가 연습 1 의 backdoor peek 이 나온다. UVM RAL 의 frontdoor/backdoor 구분(버스 트랜잭션 vs HDL 경로로 플롭 직접 읽기)과 같은 발상이다.

**구멍에서 무슨 일이 나나 — 실칩은 제각각이다.** 이 mock 은 AXI 응답 이름을 빌려 "디코드 안 되는 주소 = DECERR, 슬레이브가 거부 = SLVERR" 로 나눴다. 실제로는 ① 인터커넥트의 default slave 가 DECERR 를 돌려 CPU 가 예외를 받거나(Cortex-M BusFault, A-class 의 external abort/SError) ② APB 처럼 에러 신호(PSLVERR)가 선택 사항이라 그냥 0 이 읽히거나 ③ 클럭이 꺼진 블록이 응답을 못 해 버스가 멈추거나(hang) ④ 고정 패턴을 돌려주는 설계도 있다. 검증 코드는 "어떤 응답이 스펙인가" 를 기술서/SoC 스펙에서 받아 기대값으로 삼아야 한다. 이 레슨의 `0xDEADBEEF` 는 mock 의 약속일 뿐이다.

**접근 계층** — 왜 에러를 반환값이 아니라 sticky 로?

```c
static uint32_t reg_read32(uint32_t off) {
    uint32_t v = BUS_DEAD;
    int e = g_bus->read32(g_bus->ctx, off, &v);
    g_acc.reads++;
    if (e) {
        g_acc.errors++;
        if (!g_acc.sticky_err) { g_acc.sticky_err = e; g_acc.err_off = off; }
    }
    return v;
}
```

1. **`0xDEADBEEF` 는 정상 데이터일 수 있다.** 32비트 RW 스크래치 레지스터에 그 값을 쓰면 정상적으로 그 값이 읽힌다. 에러를 데이터 패턴으로 판정하면 오탐/미탐이 생긴다 → 에러는 **별도 채널** 로.
2. **시그니처는 `uint32_t reg_read32(off)` 그대로 둔다.** 테스트 수백 개가 `if (reg_read32(X) != Y)` 꼴이다. 반환을 `int` + out 파라미터로 바꾸면 모든 테스트가 지저분해진다. errno 처럼 옆 채널에 두고, 확인하고 싶은 지점에서 `bus_err_take()` 로 꺼낸다.
3. **첫 에러가 이긴다(first error wins).** 클럭이 꺼진 블록에 연속 접근하면 에러가 수십 개 쌓인다. 진단에 필요한 건 *처음* 실패한 주소다 — 하네스 확인: `0x400`, `0x404` 에 연달아 써도 `err_off == 0x400`. Cortex-M 의 BFAR(BusFault Address Register)가 첫 정밀 에러 주소를 잡아 두는 것과 같은 이유다.
4. 횟수 집계(`reads=13 writes=10 errors=4`)는 공짜로 얻는 로그 정보다 — "테스트가 버스에 몇 번 닿았나" 는 JTAG 백엔드에서 실행 시간을 결정한다.

### Part B — 시퀀스의 단계 하나하나가 결함 하나를 잡는다

```svg
<svg viewBox="0 0 740 262" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">W1C 시퀀스 (INT_STAT.OVR, bit1) — 정상 vs FK_W1C_AS_RW (OVR 이 평범한 RW 플롭)</text>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="105" y="52">hw_set 1</text><text x="205" y="52">read</text><text x="305" y="52">write 0</text>
    <text x="405" y="52">read</text><text x="505" y="52">write 1</text><text x="605" y="52">read</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="205" y="68">set</text><text x="405" y="68">w0-keeps</text><text x="605" y="68">w1-clears</text>
  </g>
  <text x="10" y="102" fill="currentColor" font-weight="bold">정상</text>
  <text x="10" y="162" fill="currentColor" font-weight="bold">결함</text>
  <g stroke="currentColor">
    <rect x="60" y="82" width="90" height="32" class="f-mut"/><rect x="160" y="82" width="90" height="32" class="f-ok"/>
    <rect x="260" y="82" width="90" height="32" class="f-mut"/><rect x="360" y="82" width="90" height="32" class="f-ok"/>
    <rect x="460" y="82" width="90" height="32" class="f-mut"/><rect x="560" y="82" width="90" height="32" class="f-ok"/>
    <rect x="60" y="142" width="90" height="32" class="f-mut"/><rect x="160" y="142" width="90" height="32" class="f-ok"/>
    <rect x="260" y="142" width="90" height="32" class="f-mut"/><rect x="360" y="142" width="90" height="32" class="f-warn"/>
    <rect x="460" y="142" width="90" height="32" fill="none" stroke-dasharray="4 3"/><rect x="560" y="142" width="90" height="32" fill="none" stroke-dasharray="4 3"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="105" y="103">OVR=1</text><text x="205" y="103">1 ✓</text><text x="305" y="103">무시</text>
    <text x="405" y="103">1 ✓</text><text x="505" y="103">지움</text><text x="605" y="103">0 ✓</text>
    <text x="105" y="163">OVR=1</text><text x="205" y="163">1 ✓</text><text x="305" y="163">0 저장</text>
    <text x="405" y="163">0 ✗</text><text x="505" y="163">(중단)</text><text x="605" y="163">—</text>
  </g>
  <text x="10" y="200" fill="currentColor" font-family="monospace" font-size="12">FAIL reg=INT_STAT.OVR off=0x00C acc=W1C step=w0-keeps</text>
  <text x="10" y="218" fill="currentColor" font-family="monospace" font-size="12">     exp=0x00000002 act=0x00000000 diff=0x00000002 bits=[1]</text>
  <text x="10" y="248" fill="currentColor" font-size="12">hw_set 단계가 없으면: 리셋값 0 에서 "1 쓰기 → 0 읽기" 는 RO-0 · stuck-at-0 · 없는 비트도 통과한다.</text>
</svg>
```

각 단계가 존재하는 이유 — "어떤 실리콘 버그를 겨냥한 단계인가":

| 단계 | 겨냥하는 결함 (실리콘에서 실제로 나오는 유형) | 빠뜨리면 |
|---|---|---|
| RW `w1-read` + `w0-read` | 플롭 누락(tie-off 0/1), 쓰기 enable 미연결, 필드 폭 축소 | 한 방향만 보면 stuck-at 한쪽을 놓침 (본격 비트별 검사는 SIV-B05) |
| RO `write-ignored` (`~v0` 쓰기) | ID/버전 레지스터가 상수 tie-off 대신 플롭으로 합성됨, 상태 비트에 SW 쓰기 경로가 남음 | `0xFFFFFFFF` 만 쓰면 리셋값 1 인 비트의 결함이 숨음 |
| WO `raz` | 쓰기 전용 데이터가 읽기 mux 로 새어 나옴 (스펙 위반, 보안 키 레지스터라면 정보 유출) | — (래치 여부는 frontdoor 로 불가 → 연습 1) |
| W1C `w0-keeps` | **W1C 를 일반 RW 로 구현** — RMW 하는 드라이버가 다른 이벤트를 지우게 된다 (C-M02) | 가장 흔한 access type 버그 유형을 놓침 |
| W1C `w1-clears` | clear 로직 미연결 → 인터럽트가 영원히 펜딩 (인터럽트 폭주) | |
| W1S `w0-keeps` | W1S 를 RW 로 구현 — "enable 하나 켜기" 가 다른 enable 을 끔 | |
| RC `read2-clears` | read-clear 펄스 미연결 → 카운터가 누적만 됨 | |
| 모든 단계의 버스 상태 | 클럭 게이트·리셋 미해제·디코더 누락으로 SLVERR/DECERR | 에러 응답의 `0xDEADBEEF` 를 데이터로 비교해 엉뚱한 FAIL 사유를 남김 |

`step_expect` 가 비교 전에 **필드 마스크** 를 씌우는 이유: 같은 레지스터의 다른 비트(예: STATUS.BUSY 옆의 RXCNT)는 하드웨어가 아무 때나 바꿀 수 있다. 레지스터 전체를 비교하면 이웃 필드의 정상 동작이 이 필드의 FAIL 로 보고된다 (flaky test). 반대로 reserved 비트 검사는 리셋값 테스트(SIV-B03)와 패턴 R/W 테스트(SIV-B04)가 별도로 한다.

> **이 레슨과 SIV-B04 의 경계.** 여기서 `test_field` 는 "access type 정의를 증명하는 최소 시퀀스" 로 mock(나중엔 실칩)의 *의미* 를 검사한다. SIV-B04 는 `0x0 / 0xFFFFFFFF / 0x5555… / 0xAAAA…` 패턴과 "혼합 레지스터의 write 후 기대 read 값 계산기" 로 데이터 경로를 훑는다. bit 단위 stuck-at/short 는 SIV-B05, 주소 aliasing 은 SIV-B06.

**SKIP 은 PASS 가 아니다.** 트리거가 없는 백엔드(`hw_set == NULL`)에서 W1C/RC 를 "할 수 있는 만큼만" 해서 PASS 로 집계하면, 리포트는 초록색인데 W1C 커버리지는 0 이다. 실리콘에서 트리거는 ① IP 의 force/test 레지스터(인터럽트 강제 세트, SIV-B11) ② 실제 이벤트 유도(루프백으로 RX overrun 만들기) ③ 디버그 전용 백도어(일부 SoC 의 스캔/DFT 경로) 중 하나로 만든다. 셋 다 없으면 SKIP 과 *이유* 를 리포트에 남기는 게 정직하다.

UVM 레지스터 레이어의 내장 시퀀스와 대응시키면: `uvm_reg_hw_reset_seq` ≈ SIV-B03, `uvm_reg_bit_bash_seq` ≈ SIV-B05 (필드 access policy 를 보고 기대값을 계산한다), `uvm_reg_access_seq` ≈ 이 레슨의 frontdoor 쓰기 + backdoor 확인 (연습 1), `uvm_mem_walk_seq` ≈ SIV-B07. 부작용 있는 레지스터는 UVM 에서도 `NO_REG_BIT_BASH_TEST` 같은 리소스 속성으로 제외한다. post-silicon 에는 backdoor 가 없으므로 같은 의도를 frontdoor + 트리거로 다시 짜는 것이 이 레슨의 일이다.

### Part C — 결함 주입 캠페인: 실제 출력

`./solution` 의 실제 출력 (Part C 부분):

```text
== Part C: SPI0 스위트, 결함 주입 캠페인, 백엔드 교체 ==
  [mock good] pass=15 fail=0 skip=0
  [fault 0] pass=14 fail=1 skip=0
    | FAIL reg=INT_STAT.OVR off=0x00C acc=W1C step=w0-keeps exp=0x00000002 act=0x00000000 diff=0x00000002 bits=[1]
  [fault 1] pass=14 fail=1 skip=0
    | FAIL reg=ID.REV off=0x000 acc=RO step=write-ignored exp=0x00000002 act=0x0000000D diff=0x0000000F bits=[3:0]
  [fault 2] pass=14 fail=1 skip=0
    | FAIL reg=ERR_CNT.CNT off=0x018 acc=RC step=read2-clears exp=0x00000000 act=0x000000FF diff=0x000000FF bits=[7:0]
  [fault 3] pass=14 fail=1 skip=0
    | FAIL reg=TXDATA.DATA off=0x014 acc=WO step=raz exp=0x00000000 act=0x000000FF diff=0x000000FF bits=[7:0]
  [fault 4] pass=14 fail=1 skip=0
    | FAIL reg=INT_EN.DONE_EN off=0x010 acc=W1S step=w0-keeps exp=0x00000001 act=0x00000000 diff=0x00000001 bits=[0]
  [fault 5] pass=12 fail=3 skip=0
    | FAIL reg=STATUS.BUSY off=0x008 acc=RO step=read bus=SLVERR rdata=0xDEADBEEF
    | FAIL reg=STATUS.TXFULL off=0x008 acc=RO step=read bus=SLVERR rdata=0xDEADBEEF
    | FAIL reg=STATUS.RXCNT off=0x008 acc=RO step=read bus=SLVERR rdata=0xDEADBEEF
```

| # | 주입한 결함 | 위치 | 검출 단계 | 리포트가 알려 주는 것 |
|---|---|---|---|---|
| 0 | W1C → RW | INT_STAT bit1 | `w0-keeps` | OVR 만. 같은 레지스터의 DONE/UDR 은 PASS → "레지스터 전체가 아니라 bit1 의 clear 로직" |
| 1 | RO → 쓰기 가능 | ID bits[3:0] | `write-ignored` | `bits=[3:0]` — REV 의 하위 니블만 플롭. 상위 니블은 정상 tie-off |
| 2 | RC 가 안 지워짐 | ERR_CNT | `read2-clears` | 첫 읽기는 맞고 두 번째가 틀림 → read-clear 펄스 문제 |
| 3 | WO 가 읽힘 | TXDATA | `raz` | 쓰기 데이터가 읽기 mux 로 샘 |
| 4 | W1S → RW | INT_EN bit0 | `w0-keeps` | DONE_EN 만, OVR_EN 은 정상 |
| 5 | SLVERR | STATUS 전체 | `read` | 세 필드 모두 같은 오프셋 · 같은 버스 에러 → 필드가 아니라 **레지스터/블록 접근 자체** 의 문제 (클럭, 리셋, 디코더) |

5번처럼 "같은 오프셋에서 모든 필드가 버스 에러" 는 비트 버그가 아니라 접근 경로 문제다. 디버그 엔지니어는 이 패턴만 보고 클럭 게이트·리셋 상태부터 확인한다 (SIV-B10 bring-up). 연습 4 는 블록 전체가 에러일 때 리포트가 같은 줄 수십 개로 도배되지 않게 중단하는 정책을 다룬다.

리포트 한 줄에 들어간 필드와 이유:

| 항목 | 왜 |
|---|---|
| `reg=INT_STAT.OVR` | 사람이 기술서·RTL 에서 바로 찾는 이름 |
| `off=0x00C` | JTAG 로 같은 주소를 직접 `mdw` 해 재현할 때 |
| `acc=W1C` | 기대 동작이 무엇이었는지 (리뷰어가 기술서를 다시 안 열어도 됨) |
| `step=w0-keeps` | 시퀀스의 *어느 단계* — 결함 종류를 거의 결정한다 |
| `exp/act` (필드 비트만) | 이웃 필드 잡음 제거 |
| `diff` + `bits=[1]` | 결함 비트 위치. 여러 칩·여러 보드에서 같은 비트면 설계 버그, 제각각이면 제조/마진 문제를 의심 |
| `bus=SLVERR rdata=…` | 데이터 비교가 무의미한 경우를 명시 |

### Part C — 백엔드 교체: RAM 위에서 돌리면

`mmio_read32` 는 실칩 백엔드 그대로다 — `volatile` 포인터로 창을 읽을 뿐 시맨틱이 없다. 호스트에서는 창 뒤가 정적 배열(RAM)이다. 실칩에서 **베이스 주소를 잘못 줘서 SRAM 이나 다른 블록을 때리는** 상황과 똑같다. 실제 출력:

```text
  [raw RAM] pass=3 fail=8 skip=4
    | FAIL reg=ID.REV off=0x000 acc=RO step=write-ignored exp=0x00000002 act=0x000000FD diff=0x000000FF bits=[7:0]
    | FAIL reg=ID.PART off=0x000 acc=RO step=write-ignored exp=0x53500000 act=0xACAF0000 diff=0xFFFF0000 bits=[31:16]
    | FAIL reg=STATUS.BUSY off=0x008 acc=RO step=write-ignored exp=0x00000000 act=0x00000001 diff=0x00000001 bits=[0]
    | FAIL reg=STATUS.TXFULL off=0x008 acc=RO step=write-ignored exp=0x00000000 act=0x00000002 diff=0x00000002 bits=[1]
    | FAIL reg=STATUS.RXCNT off=0x008 acc=RO step=write-ignored exp=0x00000000 act=0x000000F0 diff=0x000000F0 bits=[7:4]
    | FAIL reg=INT_EN.DONE_EN off=0x010 acc=W1S step=w0-keeps exp=0x00000001 act=0x00000000 diff=0x00000001 bits=[0]
    | FAIL reg=INT_EN.OVR_EN off=0x010 acc=W1S step=w0-keeps exp=0x00000002 act=0x00000000 diff=0x00000002 bits=[1]
    | FAIL reg=TXDATA.DATA off=0x014 acc=WO step=raz exp=0x00000000 act=0x000000FF diff=0x000000FF bits=[7:0]
```

이건 **음성 대조군(negative control)** 이다. RAM 은 RW 레지스터와 구별이 안 되므로 RW 3개는 PASS 하지만, RO·W1S·WO 는 전부 FAIL 하고 W1C·RC 는 트리거가 없어 SKIP 이다. 반대로 말하면, 테스트를 *그냥 배열로 만든 mock* 위에서만 돌려 봤다면 "access type 테스트가 실제로 뭔가를 구별하는지" 를 증명한 적이 없는 것이다. 칩에서 "RO 레지스터 5개가 동시에 쓰기 가능" 이 나오면 RTL 버그 5개보다 **주소 맵/베이스 주소 오류** 를 먼저 의심하라는 것도 이 출력이 주는 교훈이다 (aliasing 은 SIV-B06).

**trace 데코레이터** 는 bus ops 를 감싸는 bus ops 다. 테스트 코드는 바뀌지 않는다:

```text
  trace: H 00C|=00000001 R 00C=00000001 W 00C=00000000 R 00C=00000001 W 00C=00000001 R 00C=00000000
```

`H`=트리거, `R`=읽기, `W`=쓰기. 실패한 테스트의 마지막 N개 접근을 링 버퍼로 남겨 두면 "테스트가 무엇을 했는지" 를 로직 분석기 없이 재구성할 수 있다. Linux 커널의 `regmap` 이 같은 구조다 — 드라이버는 `regmap_read/regmap_write` 만 쓰고 아래는 MMIO/I2C/SPI 백엔드, 위에는 tracepoint 와 캐시가 붙는다. regmap 은 레지스터마다 readable / writeable / volatile / **precious**(읽기에 부작용이 있어 함부로 읽으면 안 됨 = RC) 콜백을 둔다 — 이 레슨의 access class 와 같은 정보를 드라이버 쪽에서 들고 있는 것이다.

### 접근 계층의 비용 — `demo_target.c` 실제 asm

`clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c` 의 실제 출력 (지시어 생략, Apple clang 21):

```text
direct_w1c_clear:                    @ INT_STAT ← 0x2; return INT_STAT
	movw	r0, #12300               @ 0x300C
	movt	r0, #16385               @ 0x4001 → r0 = 0x4001300C
	movs	r1, #2
	str	r1, [r0]
	ldr	r0, [r0]
	bx	lr
via_ops_w1c_clear:                   @ g_bus->write32(...); g_bus->read32(...)
	push	{r4, lr}
	sub	sp, #8
	movw	r4, :lower16:g_bus
	movt	r4, :upper16:g_bus
	ldr	r1, [r4]                 @ g_bus
	movs	r2, #2
	ldr	r0, [r1]                 @ ctx
	ldr	r3, [r1, #8]             @ write32
	movs	r1, #12
	blx	r3                       @ 간접 호출 1
	ldr	r1, [r4]                 @ g_bus 를 다시 읽는다 (호출이 바꿨을 수 있으니)
	add	r2, sp, #4
	ldr	r0, [r1]
	ldr	r3, [r1, #4]             @ read32
	movs	r1, #12
	blx	r3                       @ 간접 호출 2
	ldr	r0, [sp, #4]
	add	sp, #8
	pop	{r4, pc}
```

직접 접근은 `str` + `ldr` 두 개, bus ops 는 간접 호출 두 번 + 스택 out 파라미터다. 검증 펌웨어에서는 문제가 안 된다 (테스트 한 단계에 수 µs, JTAG 백엔드라면 접근 하나에 수십 µs~ms). 하지만 **타이밍이 결과를 바꾸는 테스트**(인터럽트 지연 측정, FIFO 를 버스 속도로 채워 overrun 만들기)는 이 오버헤드가 측정값에 섞이므로 직접 접근 경로를 따로 둔다. 제품 드라이버에 런타임 함수 포인터를 넣지 않고 매크로/인라인 + 링크 타임 치환(호스트 테스트 빌드에서만 mock 링크, BM-B02)으로 같은 효과를 내는 이유이기도 하다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 90/90 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san
# 90/90 passed, ASan/UBSan 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 / 방법 |
|---|---|---|
| mock 을 `uint32_t regs[N]` 배열 그대로 둠 | 모든 레지스터가 RW 처럼 동작 → access type 테스트가 mock 에서 *항상* 통과해도 의미가 없다 | 쓰기/읽기 경로에 class 마스크 적용 + raw RAM 백엔드로 음성 대조 |
| 리셋 직후 W1C 에 1 을 쓰고 0 이 읽히면 PASS | RO-0, stuck-at-0, 없는 비트도 통과 | `hw_set` 으로 먼저 1 을 만든다. 못 만들면 SKIP |
| RO 검사에 `0xFFFFFFFF` 만 씀 | 리셋값이 1 인 비트는 쓰기 가능해도 값이 안 바뀜 | `~v0 & fm` 를 쓴다 |
| `if (v == 0xDEADBEEF) bus_error()` | 정상 데이터일 수 있고, 백엔드마다 에러 패턴이 다르다 | 에러는 bus ops 반환값 → sticky 채널 |
| 버스 에러마다 마지막 값으로 덮어씀 | 원인(첫 실패 주소)이 사라지고 연쇄 결과만 남는다 | first error wins (`if (!sticky) …`) |
| 레지스터 전체로 비교 | 이웃 필드(BUSY, RXCNT)의 정상 변화가 FAIL 로 보고됨 | 필드 마스크로 비교, reserved 는 별도 테스트 |
| 필드 테스트 사이에 리셋 안 함 | 앞 테스트가 남긴 상태(W1S 비트 켜짐)에 따라 결과가 바뀌는 순서 의존 | 필드마다 블록 리셋, 또는 테스트가 상태를 복원 |
| SKIP 을 PASS 로 집계 | 리포트는 초록인데 커버리지 0 | pass/fail/skip 을 따로 세고 skip 사유를 남긴다 |
| 실패 덤프 루틴이 모든 레지스터를 읽음 | RC 레지스터(에러 카운터)를 읽는 순간 증거가 지워짐 | RC/precious 는 덤프에서 제외 (연습 2) |
| 테스트가 `*(volatile uint32_t *)0x4001300C` 를 직접 씀 | 호스트·JTAG·시뮬레이터로 옮길 수 없음 | 모든 접근은 `reg_read32/reg_write32` 로 |

## 8. 면접 Q&A

<details>
<summary>Q1. 실리콘이 오기 전에 post-silicon 테스트를 어떻게 개발하나요? 그 테스트가 실칩에서 그대로 돈다는 보장은?</summary>

모든 레지스터 접근을 `reg_read32/reg_write32` 한 쌍으로 모으고 그 아래를 bus ops 로 추상화한다. 호스트에서는 access type 시맨틱과 결함 주입이 있는 mock 위에서 테스트 로직(시퀀스, 기대값, 리포트)을 디버그하고, 결함 주입으로 "이 테스트가 그 버그를 잡는다" 를 증명해 둔다. 칩이 오면 백엔드만 raw MMIO(온칩 펌웨어) 또는 JTAG 로 바꾼다.

꼬리질문 "mock 이 스펙을 잘못 이해했다면?" → 테스트와 mock 이 같은 오해를 공유해 mock 에서는 통과하고 칩에서 깨진다. 그래서 ① mock 의 레지스터 표를 RTL 과 **같은 소스**(SystemRDL/IP-XACT → SIV-B01 코드젠)에서 만들고 ② 가능하면 같은 C 테스트를 pre-silicon(RTL 시뮬레이션·에뮬레이터, SIV-B13)에서도 돌려 mock 과 RTL 의 차이를 먼저 잡는다. 칩에서 깨지면 "테스트 버그 / mock 버그 / 실리콘 버그" 중 무엇인지 가르는 게 첫 일이다.

</details>

<details>
<summary>Q2. RC(read-to-clear) 레지스터는 테스트와 덤프에서 어떻게 다루나요?</summary>

읽기 자체가 상태를 바꾸므로 ① 리셋값 테스트에서는 첫 읽기만 비교 대상이고, 그 뒤 값은 0 이 기대값이다 ② access 테스트는 트리거로 값을 만든 뒤 "첫 읽기 = 값, 둘째 = 0" 을 본다 ③ **실패 시 레지스터 덤프, 디버거 메모리 창, 로그 루틴은 RC 를 읽으면 안 된다** — 증거(에러 카운터)를 지운다 (연습 2). Linux regmap 의 `precious_reg` 가 바로 이 표시다.

꼬리질문 "디자이너에게 무엇을 권하겠나?" → 디버그·검증 친화적으로 W1C 를 권한다. 비트 단위로 무엇을 지울지 고를 수 있고, 덤프가 상태를 바꾸지 않는다.

</details>

<details>
<summary>Q3. 테스트가 레지스터를 읽었는데 0xDEADBEEF 가 나왔습니다. 무엇을 의심하고, 로그에는 무엇을 남기나요?</summary>

그 값이 인터커넥트의 에러 응답 패턴인지(디코드 실패, 슬레이브 에러), 클럭 게이트가 꺼진 블록인지, 리셋이 안 풀렸는지, 베이스 주소가 틀렸는지를 의심한다 (bring-up 순서는 SIV-B10). 판단은 데이터 값이 아니라 **버스 응답 상태** 로 해야 한다 — 그래서 접근 계층이 에러를 별도 채널로 전달한다. 로그에는 오프셋, 레지스터 이름, 버스 응답 종류, 읽은 데이터, 그 블록의 클럭/리셋 상태 레지스터 값, 그리고 직전 몇 개 접근의 trace 를 남긴다.

꼬리질문 "Cortex-M 에서 직접 읽으면 어떻게 되나?" → 에러 응답이면 BusFault(설정에 따라 HardFault 로 승격)가 나고, 정밀 에러면 BFAR 에 주소가 남는다. 프로빙 코드가 존재하지 않는 주소를 일부러 건드려야 할 때를 위해 ARMv7-M 에는 높은 우선순위 핸들러에서 BusFault 를 무시하게 하는 CCR.BFHFNMIGN 같은 장치가 있지만, 검증 펌웨어에서는 보통 fault 핸들러가 주소와 상태를 기록하고 테스트를 FAIL 로 끝낸다.

</details>

<details>
<summary>Q4. 함수 포인터 bus ops 의 오버헤드는? 제품 드라이버에도 이렇게 하나요?</summary>

Cortex-M4 asm 에서 직접 접근은 `str`/`ldr` 두 개, bus ops 는 간접 호출 두 번에 스택 out 파라미터가 붙는다. 검증 펌웨어에서는 무시할 수준이지만, 타이밍에 민감한 테스트(인터럽트 지연, 버스 속도 FIFO 채우기)는 직접 경로를 따로 둔다. 제품 드라이버는 보통 런타임 포인터 대신 컴파일/링크 타임에 백엔드를 고른다 — 인라인 함수 + 테스트 빌드에서만 mock 구현을 링크(BM-B02). Linux 처럼 버스 종류가 런타임에 정해지는 환경은 regmap 이 런타임 간접 호출을 쓴다.

</details>

<details>
<summary>Q5. W1C 비트를 테스트하려면 1 로 만들어야 하는데, 실리콘에서는 어떻게 만드나요? 못 만들면?</summary>

① IP 의 interrupt force/test 레지스터 ② 실제 조건 유도(루프백으로 RX overrun, 타이머 만료) ③ DFT/디버그 백도어. 셋 다 안 되면 테스트는 **SKIP + 사유** 다. "1 쓰기 → 0 읽기" 만 보고 PASS 하면 RO-0·stuck-at-0·연결 안 된 비트가 모두 통과한다.

꼬리질문 "force 레지스터로 세운 것과 실제 이벤트로 세운 것이 다를 수 있나?" → 그렇다. force 는 status 플롭의 set 입력만 검증하고, 이벤트 검출 로직(엣지 검출, 동기화기)은 건너뛴다. 그래서 인터럽트 검증(SIV-B11)은 force 경로와 실제 이벤트 경로를 둘 다 본다.

</details>

<details>
<summary>Q6. bit-bash 같은 자동 R/W 테스트가 W1C·W1S·RC 레지스터를 망가뜨리지 않게 하려면?</summary>

테스트가 레지스터 기술의 access type 을 읽고 **기대값을 access type 별로 계산** 해야 한다 (SIV-B04 의 기대값 함수). 그리고 ① W1S 로 인터럽트 enable 을 켜면 실제 인터럽트가 날 수 있으니 인터럽트 컨트롤러에서 먼저 마스크 ② 쓰기가 동작을 일으키는 레지스터(START, SOFT_RESET, TX FIFO 쓰기)는 제외 목록에 넣는다 — UVM 의 `NO_REG_BIT_BASH_TEST` 같은 속성과 같은 역할 ③ 테스트 사이에 블록 리셋으로 상태를 원복한다.

</details>

<details>
<summary>Q7. 테스트 실패 한 줄에 무엇을 담나요? exp/act 를 왜 필드 비트로만 찍나요?</summary>

레지스터.필드 이름, 오프셋, access type, 실패한 **시퀀스 단계**, exp/act/diff, 비트 목록, 버스 응답 상태. 단계 이름이 결함 종류를 거의 결정한다(`w0-keeps` 실패 = "0 쓰기에 지워짐" = W1C/W1S 를 RW 로 구현). exp/act 를 필드 비트로 줄이는 건 이웃 필드의 정상적인 HW 변화를 잡음에서 빼기 위해서다. 실행 단위로는 보드 ID, 칩 ID/리비전 레지스터 값, FW 버전, 시드를 헤더에 남긴다 (SIV-B08 JSON 로그 → SIV-B14 리포트).

꼬리질문 "같은 FAIL 이 보드 10장 중 1장에서만 나면?" → 설계 버그보다 제조 결함·마진(전압/온도) 문제를 의심하고, 그 보드에서 전압·온도 스윕으로 재현을 시도한다. 10장 모두 같은 비트면 설계(RTL) 버그 쪽이다.

</details>

## 9. 추가 연습

### 연습 1 — backdoor peek 으로 WO 래치 확인

WO 필드는 frontdoor 로 읽으면 항상 0 이라, "쓰기가 플롭에 들어가지 않는" 결함(`FK_WO_DROP`)을 본문 테스트가 못 잡는다. mock 에 backdoor `peek(off)` 을 두고 (실칩에서는 NULL), 두 패턴을 써서 RAZ 와 래치를 함께 확인하는 `test_wo_latch()` 를 작성하라. peek 이 없으면 SKIP.

<details>
<summary>답 보기</summary>

패턴 `0xA5A5A5A5`, `0x5A5A5A5A` 를 차례로 쓰고, 매번 frontdoor 읽기는 0 (`raz`), `peek(off) & fm == pat & fm` (`latched`) 인지 본다. 두 패턴이 서로 보수라 각 비트가 0 과 1 을 한 번씩 래치해야 한다. 상위 니블이 래치 안 되는 결함(`mask=0xF0`)을 넣은 실제 출력:

```text
  ex1: FAIL reg=TXDATA.DATA off=0x014 acc=WO step=latched exp=0x000000A5 act=0x00000005 diff=0x000000A0 bits=[7,5]
```

`bits=[7,5]` 는 첫 패턴 `A5` 에서 1 이어야 했던 상위 니블 비트들이다. 같은 결함에서 peek 없이 돌리면 `test_wo_latch` 는 SKIP, 본문 `test_field` 는 PASS — "RAZ 만 확인했다 = 절반만 본 것" 이다. 실칩에서는 WO 쓰기의 *효과* (TX FIFO 레벨 증가, 핀 파형)로 간접 확인한다.

</details>

### 연습 2 — 증거를 지우지 않는 레지스터 덤프

테스트가 실패하면 블록 전체를 덤프해 로그에 남긴다. RC 필드가 있는 레지스터는 읽지 않는 `safe_dump()` 를 작성하고, 순진한 덤프가 에러 카운터를 지우는 것을 재현하라.

<details>
<summary>답 보기</summary>

레지스터마다 `masks_from_fields` 로 `rc` 마스크를 보고, 0 이 아니면 건너뛴다 (`out[i] = 0`, 읽은 개수에서 제외). 테스트: ERR_CNT 에 0x2A 를 세운 뒤 `safe_dump` → 6개 읽음, 그 뒤에도 `reg_read32(ERR_CNT) == 0x2A`. 순진한 덤프는 0x2A 를 *덤프에는* 찍지만 그 순간 지워서, 다음 진단 단계가 0 을 본다. W1C 레지스터는 읽기만으로는 안 바뀌므로 덤프해도 안전하다. 실무에서는 "precious" 목록을 기술서에서 생성한다.

</details>

### 연습 3 — 결함 모델 × 테스트 검출 행렬

결함 6종을 각 class 의 모든 비트에 걸어 스위트를 돌리고, 실패한 필드를 access type 별로 세어 행렬을 만들어라. 어느 결함이 어느 테스트로만 잡히는지, 그리고 아무것도 못 잡는 결함은 무엇인지 확인하라.

<details>
<summary>답 보기</summary>

`detect_row()` 가 결함마다 스위트를 돌려 `mat[acc]` 를 센다. 실제 출력:

```text
  ex3: fault         RW RO WO W1C W1S RC
       W1C_AS_RW     0  0  0   3   0  0
       RO_WRITABLE   0  3  0   0   0  0
       RC_NO_CLEAR   0  0  0   0   0  1
       WO_READBACK   0  0  1   0   0  0
       W1S_AS_RW     0  0  0   0   2  0
       WO_DROP       0  0  0   0   0  0
```

각 access-class 결함은 그 class 의 테스트로만, 해당 필드 수만큼 검출된다(RO_WRITABLE 은 STATUS 의 RO 필드 3개). **마지막 줄이 핵심** — `WO_DROP` 은 frontdoor 스위트로 검출 0 이다. 커버리지 행렬에 빈 줄이 있으면 테스트 플랜에 구멍이 있는 것이고, 그 구멍은 연습 1 의 backdoor(pre-silicon) 또는 기능 테스트(post-silicon)로 메워야 한다. 이런 행렬이 SIV-B12 테스트 플랜의 "fault model → test" 표가 된다.

</details>

### 연습 4 — 버스 에러 폭주 시 스위트 중단

블록 클럭이 꺼져 있으면(`gated`) 모든 접근이 SLVERR 이다. 연속 `max_bus` 개 필드가 버스 에러로 실패하면 스위트를 멈추고 첫 실패 줄만 남기는 `run_suite_abort()` 를 작성하라. 레지스터 하나만 에러인 경우에는 중단하지 않아야 한다.

<details>
<summary>답 보기</summary>

필드마다 결과를 보고, 버스 에러 실패면 `consec++`, 정상 실패나 PASS 면 `consec = 0`, `consec >= max_bus` 면 `aborted = 1` 로 반환한다. 실제 출력 (`max_bus = 3`):

```text
  ex4: pass=0 fail=3 skip=0 aborted=1 errors=5
```

첫 줄은 `FAIL reg=ID.REV off=0x000 acc=RO step=read bus=SLVERR rdata=0xDEADBEEF`. STATUS 하나만 SLVERR 일 때는 (`max_bus = 4`) 3필드 연속 에러 뒤 INT_STAT 가 PASS 해 카운터가 리셋되므로 끝까지 돈다 (12 pass / 3 fail). 실칩에서 클럭 꺼진 블록 접근은 에러 응답 대신 **버스 hang** 일 수도 있어서, 진짜 방어는 테스트 전에 클럭/리셋 상태 레지스터를 먼저 읽는 것이다 (SIV-B10).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 30/30 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o prac_san && ./prac_san   # 30/30, 보고 없음
```

## 10. 핵심 정리

- 테스트는 레지스터에 `reg_read32/reg_write32` 로만 닿는다. 그 아래는 `bus_ops_t` 함수 포인터 — mock · trace · raw MMIO · JTAG 를 바꿔 끼워도 테스트는 그대로.
- mock 은 배열이 아니라 **access class 마스크 6개** 다: 쓰기 `s = (s & ~(rw|wo)) | (v & (rw|wo)); s &= ~(v & w1c); s |= v & w1s;` 읽기 `val = s & ~wo; s &= ~rc;`.
- 버스 에러는 데이터 패턴(0xDEADBEEF)이 아니라 **별도 sticky 채널**, 그리고 **첫 에러가 이긴다**.
- access type 시퀀스: RW 1·0 / RO `~v0` 쓰기 / WO RAZ / W1C 트리거 → 0 쓰기 유지 → 1 쓰기 지움 / W1S 0 쓰기 무시 / RC 두 번째 읽기 0.
- 트리거 못 거는 테스트는 **SKIP** 이다. 리셋값 0 에서 "1 쓰고 0 읽힘" 은 아무것도 증명하지 않는다.
- 결함 주입으로 테스트가 결함을 잡는다는 걸 증명하고, raw RAM 백엔드로 테스트가 "레지스터가 아닌 것" 을 구별한다는 걸 증명한다 (음성 대조).
- 실패 한 줄 = `reg.field off acc step exp act diff bits` (+ `bus=`). 같은 오프셋 전 필드 버스 에러 = 비트가 아니라 클럭/리셋/디코드 문제.

## 참고자료

- Accellera SystemRDL 2.0 — 필드 속성 `sw`/`hw`, `onread`(rclr), `onwrite`(woclr, woset): [accellera.org/downloads/standards/systemrdl](https://www.accellera.org/downloads/standards/systemrdl)
- IEEE 1685 (IP-XACT) — 필드 `access`, `modifiedWriteValue`, `readAction`
- IEEE 1800.2 (UVM) 레지스터 레이어 — `uvm_reg_field` access policy, `uvm_reg_hw_reset_seq`, `uvm_reg_bit_bash_seq`, `uvm_reg_access_seq`, `uvm_mem_walk_seq`, frontdoor/backdoor
- Arm AMBA AXI 프로토콜 스펙 — RRESP/BRESP `OKAY / EXOKAY / SLVERR / DECERR`; AMBA APB — `PSLVERR`
- Armv7-M Architecture Reference Manual — BusFault, BFAR, CCR.BFHFNMIGN
- Linux `include/linux/regmap.h` — `regmap_config` 의 `readable_reg / writeable_reg / volatile_reg / precious_reg`
- OpenOCD User's Guide — `mdw` / `mww` 메모리 접근 명령 (JTAG 백엔드)
- 관련 레슨: SIV-B01 (기술서 → 헤더), SIV-B03 (리셋값), SIV-B04 (패턴 R/W 와 기대값), SIV-B05 (walking 1s/0s), SIV-B06 (aliasing), SIV-B08 (프레임워크·JSON 로그), SIV-B10 (bring-up), SIV-B11 (인터럽트), SIV-B13 (pre/post-silicon), BM-B02 (mock read/write 훅), C-M01 (volatile 레지스터 구조체), C-M02 (W1C 와 RMW), C-M06 (함수 포인터 테이블)
