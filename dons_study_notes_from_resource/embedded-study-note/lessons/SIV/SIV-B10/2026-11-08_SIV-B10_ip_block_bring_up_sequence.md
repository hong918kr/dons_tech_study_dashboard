---
id: SIV-B10
time: 70분
updated: 2026-11-08
---
# SIV-B10 IP 블록 Bring-up 시퀀스

## 학습 목표

- IP 블록 하나를 **클럭 enable → 리셋 해제 → 살아 있음 확인 → ID → 버전 → 기본 기능** 순서로 올리는 bring-up 함수를 체크포인트 단위로 작성한다.
- 공유 클럭·리셋 레지스터(CRU)에서 **내 비트만** 바꾸고, 폴링은 항상 횟수(또는 시간) 한도를 둔다.
- 실패 리포트에서 **"어디서 발견됐나(step)"** 와 **"원인이 뭐로 보이나(cause)"** 를 분리한다 — CLK_STAT 이 거짓말을 해도 버스 에러 로거로, RST_STAT 이 거짓말을 해도 scratch 쓰기로 진짜 원인을 짚는다.
- 결함 9종(클럭 죽음, 상태 레지스터 거짓, 리셋 고착, 데이터 비트 고착, ID tie-off, 구버전 IP, RX 경로 결함, 정상 지연…)을 mock 에 주입해 **진단이 정확한지** 매트릭스로 검증한다.
- 첫 실패에서 멈추고(그 뒤 IP 는 건드리지 않음), 로거를 깨끗이 남기고, 같은 리포트가 디버그 엔지니어에게 바로 쓸모 있도록 만든다.

## 1. 문제

first silicon 이 보드에 올라온 날, validation 엔지니어가 블록마다 제일 먼저 돌리는 코드가 bring-up 시퀀스이고, Rivian·NVIDIA 같은 실리콘 검증 면접에서 "새 칩에서 UART 가 응답이 없다. 무엇부터 보나?" 로 그대로 나온다.

첫날 "IP 가 안 된다" 의 원인은 흔히 IP 로직 자체가 아니라 그 **주변** 이다: 클럭 게이트가 다른 비트에 물려 있다, 리셋이 안 풀린다, 전원 도메인이 꺼져 있다, ID tie-off 가 틀렸다, 넷리스트에 구버전 IP 가 들어갔다. 그래서 bring-up 은 "기능 테스트 한 방" 이 아니라 **바닥부터 하나씩 확인하는 체크포인트 사다리** 로 짠다. 아래 칸이 통과해야 위 칸의 결과를 믿을 수 있다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">mock SoC — CPU 가 보는 것은 rd32/wr32 뿐</text>
  <rect x="20" y="50" width="110" height="60" class="f-mut" stroke="currentColor"/>
  <text x="75" y="76" fill="currentColor" text-anchor="middle">CPU / JTAG</text>
  <text x="75" y="94" fill="currentColor" text-anchor="middle" font-size="12">rd32 / wr32</text>
  <rect x="170" y="40" width="150" height="80" class="f-mut" stroke="currentColor"/>
  <text x="245" y="64" fill="currentColor" text-anchor="middle">interconnect</text>
  <text x="245" y="84" fill="currentColor" text-anchor="middle" font-size="12">주소 디코드</text>
  <text x="245" y="102" fill="currentColor" text-anchor="middle" font-size="12">에러 → 0xDEADBEEF</text>
  <line x1="130" y1="80" x2="170" y2="80" stroke="currentColor" stroke-width="2"/>
  <rect x="370" y="30" width="190" height="130" class="f-acc" stroke="currentColor"/>
  <text x="465" y="50" fill="currentColor" text-anchor="middle" font-weight="bold">CRU @0x0000</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="380" y="72">CLK_EN    RW  0x000</text>
    <text x="380" y="90">CLK_STAT  RO  0x004</text>
    <text x="380" y="108">RST_CTRL  RW  0x008</text>
    <text x="380" y="126">RST_STAT  RO  0x00C</text>
    <text x="380" y="144">BUSERR    W1C 0x010 (+ADDR)</text>
  </g>
  <line x1="320" y1="70" x2="370" y2="70" stroke="currentColor" stroke-width="2"/>
  <rect x="370" y="200" width="190" height="110" class="f-ok" stroke="currentColor"/>
  <text x="465" y="220" fill="currentColor" text-anchor="middle" font-weight="bold">UART0 @0x1000</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="380" y="240">ID 0x00  VER 0x04</text>
    <text x="380" y="258">CTRL 0x08 BAUD 0x0C</text>
    <text x="380" y="276">STAT 0x10 TXD 0x14</text>
    <text x="380" y="294">RXD 0x18 SCRATCH 0x1C</text>
  </g>
  <line x1="300" y1="120" x2="300" y2="255" stroke="currentColor" stroke-width="2"/>
  <line x1="300" y1="255" x2="370" y2="255" stroke="currentColor" stroke-width="2"/>
  <rect x="600" y="60" width="120" height="40" class="f-warn" stroke="currentColor"/>
  <text x="660" y="85" fill="currentColor" text-anchor="middle">clock gate</text>
  <rect x="600" y="130" width="120" height="40" class="f-warn" stroke="currentColor"/>
  <text x="660" y="155" fill="currentColor" text-anchor="middle">reset sync</text>
  <line x1="560" y1="72" x2="600" y2="80" stroke="currentColor"/>
  <line x1="560" y1="108" x2="600" y2="150" stroke="currentColor"/>
  <line x1="660" y1="100" x2="660" y2="130" stroke="currentColor" stroke-dasharray="4 3"/>
  <line x1="660" y1="170" x2="660" y2="250" stroke="currentColor"/>
  <line x1="660" y1="250" x2="560" y2="250" stroke="currentColor"/>
  <text x="20" y="160" fill="currentColor" font-size="12">bit3 = UART0 (CLK_EN, RST_CTRL 공유:</text>
  <text x="20" y="176" fill="currentColor" font-size="12">bit0 TIMER0 은 bootrom 이 켜 둠,</text>
  <text x="20" y="192" fill="currentColor" font-size="12">bit1 SPI0 · bit2 I2C0 는 리셋 유지)</text>
  <text x="20" y="232" fill="currentColor" font-size="12">주입 결함 (오른쪽 경고색 박스):</text>
  <text x="20" y="250" fill="currentColor" font-size="12" font-family="monospace">clk_dead · clk_stat_lies</text>
  <text x="20" y="268" fill="currentColor" font-size="12" font-family="monospace">rst_stuck · rst_stat_lies</text>
  <text x="20" y="300" fill="currentColor" font-size="12">+ rdata_stuck0 · id_xor · ver · rx_stuck0 (UART 내부)</text>
</svg>
```

mock 은 SIV-B02 의 레지스터 파일을 이 레슨에 필요한 만큼만 축약해 넣었다 (access type 은 CRU 의 RW/RO/W1C 와 UART 의 RW/RO/WO 만). 주요 동작:

| 상황 | mock 의 반응 | 실제 칩에서 흔한 모습 |
|---|---|---|
| UART0 클럭이 안 돌 때 접근 | `BUSERR.bit0=1`, `BUSERR_ADDR` 기록, 읽기는 `0xDEADBEEF`, 쓰기는 버려짐 | AXI SLVERR/DECERR → Arm 데이터 abort·BusFault, 또는 버스가 응답을 안 해 **행(hang)** |
| UART0 가 리셋 중일 때 접근 | ID·VER(상수 tie-off)은 읽힘, 나머지는 리셋값 0, 쓰기는 무시 | 대부분 비슷. "ID 가 읽힌다 = 리셋 풀렸다" 가 아니다 |
| `CLK_EN` 0→1 후 | `clk_settle` 번의 CLK_STAT 읽기 동안 0 (정상 지연) | 클럭 게이트 동기화기·PLL lock 지연 |

주입 결함 (`fault_t`) — 테스트마다 `reset_soc(&fault)` 로 POR 상태와 함께 넣는다:

| 필드 | 뜻 | 어디서 드러나야 하나 |
|---|---|---|
| `clk_dead` | 게이트가 안 열림, CLK_STAT 도 0 | CLK 폴링 한도 초과 |
| `clk_stat_lies` | CLK_STAT 은 1, IP 엔 클럭 없음 | 첫 IP 접근의 **버스 에러** |
| `clk_settle` | CLK_STAT 이 N 번 늦게 1 (결함 아님) | 한도 안이면 PASS 여야 함 |
| `rst_stuck` | 리셋이 안 풀림, RST_STAT 도 0 | RST 폴링 한도 초과 |
| `rst_stat_lies` | RST_STAT 은 1, IP 는 여전히 리셋 | scratch 쓰기가 **무시됨** |
| `rdata_stuck0` | UART 읽기 데이터 비트 stuck-at-0 | scratch 패턴 비트 리포트 |
| `id_xor` | ID tie-off 오류 | ID 비교 diff 비트 |
| `ver` | 다른 VERSION 값 | major/minor 판정 |
| `rx_stuck0` | RX 데이터 경로 비트 고착 | loopback 데이터 diff |

### Part A — 클럭 → 리셋 → ID

- `bu_clock_enable(r)` — `CLK_EN` 의 UART0 비트만 켜고(다른 IP 비트 보존), `CLK_STAT` 을 최대 `MAX_POLLS`(8) 번 읽어 확인. 성공 시 `r->polls` = 읽은 횟수.
- `bu_reset_release(r)` — `RST_CTRL` 의 UART0 비트만 내리고 `RST_STAT` 확인 (SPI0/I2C0 는 계속 리셋).
- `bu_check_id(r)` — `UART0.ID` 를 `bu_read_checked` 로 읽어 `0x55415254`("UART") 와 비교.
- 실패는 주어진 `bu_fail()` 로: `FAIL step=… cause=… reg=… addr=… exp=… act=… diff=… bits=[…] polls=… | note`.
- 버스 접근 기록으로 **순서** 도 검사한다: CLK_EN 쓰기 → RST_CTRL 쓰기 → UART0.ID 읽기.

### Part B — 증상 ≠ 원인

- `bu_read_checked(r, step, addr, &v)` — stale 버스 에러를 먼저 지우고(W1C) 읽은 뒤 `BUSERR` 를 확인. 잡혔으면 `cause=CLOCK_NOT_APPLIED` 로 기록하고 로거를 다시 비운다.
- `bu_check_alive(r)` — `SCRATCH` 에 `0xA5A5A5A5`, `0x5A5A5A5A` 를 쓰고 되읽기. 버스 에러 → CLK, 리셋값(0) → RST, 그 밖의 불일치 → `BUS_DATA_PATH`.
- 정상 지연 `clk_settle = 7` 은 통과(polls=8), `= 8` 은 실패 — 한도 경계. 같은 시퀀스를 두 번 돌려도 안전해야 한다(idempotent).

### Part C — 전체 시퀀스와 결함 매트릭스

- `bu_check_version(r)` — major == 1 && minor ≥ 2.
- `bu_func_loopback(r)` — BAUD=104 쓰고 되읽기, `CTRL=EN|LOOP`, TXE 확인, `TXD=0xA5`, RXNE 폴링, RXD 비교, 끝나면 `CTRL=EN`.
- `uart_bringup(r)` — 6 체크포인트를 차례로, **첫 실패에서 멈춤**. 성공 메시지는 정확히 `PASS UART0 bring-up: ID=0x55415254 VER=1.2.0 checkpoints=6/6`.
- 하네스가 9개 결함으로 매트릭스를 돌려 step·cause·레지스터·비트를 검사하고, 매번 버스 에러 로거가 깨끗한지, 클럭/리셋 실패 때 UART 를 한 번도 건드리지 않았는지 확인한다.

## 2. Problem (English)

> **IP block bring-up with diagnosable checkpoints.**
>
> A mock SoC exposes a clock & reset unit (CRU) and a UART IP through `uint32_t rd32(uint32_t addr)` / `void wr32(uint32_t addr, uint32_t v)`. `CRU.CLK_EN` / `CRU.RST_CTRL` are shared by several IPs (UART0 is bit 3); `CLK_STAT` / `RST_STAT` report the result; `CRU.BUSERR` (W1C) and `BUSERR_ADDR` capture interconnect errors. Accessing an unclocked IP returns `0xDEADBEEF` and sets `BUSERR`. An IP held in reset ignores writes but still returns its constant ID.
>
> 1. Implement `bool bu_clock_enable(bu_result_t *r)`, `bool bu_reset_release(bu_result_t *r)` and `bool bu_check_id(bu_result_t *r)`. Touch only UART0's bits; poll status at most `MAX_POLLS` (8) reads; on failure call `bu_fail()` with the step, the root cause, the register, expected/actual and mask.
> 2. Implement `bool bu_read_checked(bu_result_t *r, step_t st, uint32_t addr, uint32_t *out)` that reports a bus error as `CAUSE_CLK` even when `CLK_STAT` claims the clock is running (and is not fooled by a stale error), and `bool bu_check_alive(bu_result_t *r)` that writes/reads two scratch patterns and distinguishes "still in reset" (reads the reset value) from a data-path defect.
> 3. Implement `bu_check_version` (major == 1, minor ≥ 2), `bu_func_loopback` and `bool uart_bringup(bu_result_t *r)` that runs CLK → RST → ALIVE → ID → VER → FUNC and stops at the first failure.
>
> Example: with the clock-gate status bit lying (`CLK_STAT=1`, no clock at the IP), a naive sequence reports `ID_MISMATCH act=0xDEADBEEF`; yours must report `FAIL step=ALIVE cause=CLOCK_NOT_APPLIED reg=UART0.SCRATCH addr=0x101C act=0xDEADBEEF | bus error captured at 0x101C ...`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 실제 출력 끝부분: "11/64 passed", exit 1 — TODO 를 채워 64/64 를 만든다
```

시작 코드에서 주어진 것(고치지 않음): mock SoC 전체(`reset_soc`, `rd32`, `wr32`, `fault_t`), 리포트 도우미 `bu_fail()` / `bu_pass()`, 하네스. TODO 는 8개 함수다. `bu_read_checked` 는 일부러 **순진한 읽기** 로, `bu_check_alive` 는 **무조건 통과** 로 채워 두었다 — Part A 를 다 풀고 나면 Part B 테스트가 "그럴듯하지만 틀린 진단" 을 내는 것을 직접 보게 된다 (5. 풀이 Part B 에 실제 출력).

## 4. 힌트

<details>
<summary>힌트 1 — 공유 레지스터는 RMW, 폴링은 for 문</summary>

`wr32(CRU_CLK_EN, rd32(CRU_CLK_EN) | UART0_BIT)`. 리셋 해제는 `& ~UART0_BIT`. 폴링은 `for (i = 1; i <= MAX_POLLS; i++) { s = rd32(STAT); if (s & BIT) {...return true;} }` 로 끝이 보장되게 쓰고, 루프를 빠져나오면 마지막으로 읽은 `s` 를 `act` 로 넘긴다. 실패 시 `r->polls = MAX_POLLS` 를 먼저 넣어야 메시지에 찍힌다.

</details>

<details>
<summary>힌트 2 — 버스 에러 로거는 "지우고 → 읽고 → 확인 → 지우기"</summary>

로거는 sticky(W1C)라서 이전 테스트나 디버거 접근이 남긴 에러가 있을 수 있다. 확인하기 **전에** 1 을 써서 비우지 않으면 정상 읽기도 실패로 보인다 (하네스의 stale 에러 테스트). 잡혔으면 `BUSERR_ADDR` 를 메모에 넣고, 다시 1 을 써서 다음 단계를 위해 비운다.

</details>

<details>
<summary>힌트 3 — "쓰기가 무시됐다" 와 "값이 틀렸다" 는 다른 원인</summary>

scratch 에 `0xA5A5A5A5` 를 썼는데 리셋값 `0` 이 읽히면 쓰기 자체가 안 먹은 것 — 리셋 중. 다른 값이면 쓰기는 먹었는데 경로가 깨진 것 — 데이터 비트. 두 패턴은 서로 보수라서 모든 비트를 0→1, 1→0 양쪽으로 한 번씩 확인한다 (stuck-at 두 방향, SIV-B05 참고).

</details>

<details>
<summary>힌트 4 — uart_bringup 은 && 사슬</summary>

각 단계가 `bool` 을 돌려주므로 `if (!a(r) || !b(r) || ...) return false;` 하나로 "첫 실패에서 멈춤" 이 된다 (`||` 는 단락 평가). 실패 정보는 이미 `bu_fail` 이 `r` 에 채웠다. 시작에 `memset(r, 0, ...)` 을 잊으면 이전 실행의 `done` 비트가 남는다.

</details>

## 5. 풀이

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">체크포인트 사다리 — 실패하면 오른쪽으로 빠져 원인을 기록하고 멈춘다</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="300" height="40" class="f-acc"/>
    <rect x="20" y="96" width="300" height="40" class="f-acc"/>
    <rect x="20" y="152" width="300" height="40" class="f-acc"/>
    <rect x="20" y="208" width="300" height="40" class="f-acc"/>
    <rect x="20" y="264" width="300" height="40" class="f-acc"/>
    <rect x="20" y="320" width="300" height="40" class="f-acc"/>
    <rect x="400" y="40" width="330" height="40" class="f-warn"/>
    <rect x="400" y="96" width="330" height="40" class="f-warn"/>
    <rect x="400" y="152" width="330" height="40" class="f-warn"/>
    <rect x="400" y="208" width="330" height="40" class="f-warn"/>
    <rect x="400" y="264" width="330" height="40" class="f-warn"/>
    <rect x="400" y="320" width="330" height="40" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="30" y="65">① CLK   CLK_EN |= bit3, CLK_STAT 폴링 ≤ 8</text>
    <text x="30" y="121">② RST   RST_CTRL &amp;= ~bit3, RST_STAT 폴링</text>
    <text x="30" y="177">③ ALIVE SCRATCH A5A5A5A5 / 5A5A5A5A</text>
    <text x="30" y="233">④ ID    == 0x55415254 ("UART")</text>
    <text x="30" y="289">⑤ VER   major == 1, minor ≥ 2</text>
    <text x="30" y="345">⑥ FUNC  BAUD, EN|LOOP, TXD → RXD</text>
    <text x="410" y="65">CLOCK_NOT_APPLIED (CLK_STAT bit3=0)</text>
    <text x="410" y="121">RESET_NOT_RELEASED (RST_STAT bit3=0)</text>
    <text x="410" y="170" font-size="12">버스 에러 → CLOCK_NOT_APPLIED</text>
    <text x="410" y="186" font-size="12">0 읽힘 → RESET · 그 외 → BUS_DATA_PATH</text>
    <text x="410" y="233">ID_MISMATCH (diff 비트) / 버스 에러 → CLK</text>
    <text x="410" y="289">VERSION_UNSUPPORTED</text>
    <text x="410" y="345">FUNC_LOOPBACK (RX 데이터 diff)</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5">
    <line x1="320" y1="60" x2="400" y2="60"/><line x1="320" y1="116" x2="400" y2="116"/>
    <line x1="320" y1="172" x2="400" y2="172"/><line x1="320" y1="228" x2="400" y2="228"/>
    <line x1="320" y1="284" x2="400" y2="284"/><line x1="320" y1="340" x2="400" y2="340"/>
    <line x1="170" y1="80" x2="170" y2="96"/><line x1="170" y1="136" x2="170" y2="152"/>
    <line x1="170" y1="192" x2="170" y2="208"/><line x1="170" y1="248" x2="170" y2="264"/>
    <line x1="170" y1="304" x2="170" y2="320"/>
  </g>
  <text x="20" y="388" fill="currentColor">③ 이후의 모든 읽기는 bu_read_checked — 어느 단계에서든 버스 에러는 "클럭 미인가" 로 분류</text>
</svg>
```

### Part A — 왜 이 순서인가

**클럭이 먼저, 리셋 해제는 그다음.** 대부분의 IP 는 "비동기 assert, 동기 deassert" 리셋 동기화기(reset synchronizer)를 거친다. 리셋이 풀리는 순간은 IP 클럭 에지에 맞춰 퍼지므로 **클럭이 돌고 있어야 리셋 해제가 끝난다**. 또 클럭이 없는 IP 의 레지스터에 접근하면 버스가 에러를 돌려주거나(AXI SLVERR), 버스 브리지가 응답을 영원히 기다려 코어가 멈춘다 — JTAG 으로 붙어 있으면 "cannot access memory" 류 에러만 보게 된다. 그래서 순서는 정해져 있다:

1. 클럭 켜기 → 정말 돌고 있는지 확인
2. 리셋 해제 → 해제 완료 확인
3. 그다음에야 IP 레지스터 접근

Linux 드라이버의 `probe()` 가 `clk_prepare_enable()` → `reset_control_deassert()` → 레지스터 접근 순서인 것도, Linux AMBA 버스 코드가 PrimeCell 장치(PL011 UART 등)의 ID 레지스터(블록 끝의 PeriphID/CellID, CellID = `0xB105F00D`)를 **apb_pclk 를 켠 뒤에** 읽어 드라이버를 매칭하는 것도 같은 이유다. 테스트는 이 순서를 버스 접근 기록에서 직접 검사한다:

```c
CHECK(first_acc('w', CRU_CLK_EN) < first_acc('w', CRU_RST_CTRL));
CHECK(first_acc('w', CRU_RST_CTRL) < first_acc('r', UART0_BASE + UART_ID));
```

**공유 레지스터는 내 비트만.** `CLK_EN` 은 POR 후 `0x1`(bootrom 이 TIMER0 클럭을 켜 둠)이다. `wr32(CRU_CLK_EN, UART0_BIT)` 처럼 통째로 쓰면 TIMER0 클럭이 꺼진다 — 그 타이머가 OS tick 이나 워치독이면 bring-up 테스트가 시스템을 죽인다. `RST_CTRL` 도 POR 값 `0xE` 에서 bit3 만 내려 `0x6` 이 되어야 한다 (SPI0/I2C0 는 아직 아무도 bring-up 하지 않았다). 실제 칩은 이 경쟁을 피하려고 CRU 에 SET/CLR alias 레지스터를 두는 경우가 많다 — 그러면 RMW 대신 alias 에 한 번 쓰면 된다 (C-M02 참고).

**폴링은 무조건 한도.** `while (!(rd32(CLK_STAT) & BIT));` 는 클럭이 안 들어오는 칩에서 테스트 전체를 멈춘다. 첫날 실리콘에서 가장 흔한 "테스트가 안 끝나요" 다. 여기서는 결정적인 테스트를 위해 **읽기 횟수** 한도(`MAX_POLLS`)를 쓰고, 실시간 한도와 타임아웃 리포트(마지막 값·경과 시간·호출 위치)는 SIV-B09 에서 다룬다. 실패 리포트의 `polls=8` 과 `act=` 는 "얼마나 기다렸고 마지막에 무엇을 봤나" 다.

**ID 비교는 마스크 32비트, diff 를 비트로.** `id_xor = 0x100` 결함의 실제 리포트:

```text
FAIL step=ID cause=ID_MISMATCH reg=UART0.ID addr=0x1000 exp=0x55415254 act=0x55415354 diff=0x00000100 bits=[8] | wrong IP at this base or ID tie-off error
```

비트 하나 차이면 tie-off 오류(또는 그 비트의 데이터 경로)를, 완전히 다른 값이면 "이 주소에 다른 IP 가 있다"(주소 맵/디코더 문제, SIV-B06 참고)를 의심한다. diff 를 비트 목록으로 찍어 두면 디버그 엔지니어가 바로 판단할 수 있다.

### Part B — 상태 레지스터를 믿지 말고 교차 확인하라

`CLK_STAT`, `RST_STAT` 은 "CRU 가 그렇다고 믿는 것" 이다. 게이트 출력이 다른 IP 로 배선됐거나(통합 실수), 리셋이 다른 비트에 물렸거나, 상태 비트가 enable 비트를 그대로 되돌려 주는 구현이면 상태는 1 인데 IP 는 죽어 있다. 그래서 **IP 자체에 대한 접근으로 교차 확인** 한다.

| 증상 | 순진한 진단 | 진짜 원인 | 구별하는 확인 |
|---|---|---|---|
| ID 가 `0xDEADBEEF` | ID 불일치 | 클럭이 IP 에 안 닿음 | 버스 에러 로거 (`BUSERR`, `BUSERR_ADDR`) |
| ID 정상인데 BAUD 되읽기 0 | 기능 버그 | IP 가 아직 리셋 중 | scratch 쓰기가 리셋값으로 읽힘 |
| scratch 한 비트 틀림 | "IP 불량" | 데이터 경로 stuck-at | 보수 패턴 두 개 + diff 비트 |
| CLK_STAT 이 늦게 1 | 클럭 결함 | 정상 지연(동기화기·PLL) | 한도 안이면 PASS, polls 기록 |

순진한 버전이 실제로 무엇을 내는지 돌려 보았다. `solution.c` 에서 `bu_read_checked` 를 starter 의 순진한 읽기로, `bu_check_alive` 를 "무조건 통과" 로 바꾼 사본의 **실제 출력** (매트릭스 일부, 50/64 passed):

```text
  clk_stat_lies  FAIL step=ID cause=ID_MISMATCH reg=UART0.ID addr=0x1000 exp=0x55415254 act=0xDEADBEEF diff=0x8BECECBB bits=[31,27,25,24,23,22,21,19,...+12] | wrong IP at this base or ID tie-off error
  rst_stat_lies  FAIL step=FUNC cause=FUNC_LOOPBACK reg=UART0.BAUD addr=0x100C exp=0x00000068 act=0x00000000 diff=0x00000068 bits=[6,5,3] | BAUD readback mismatch
  rdata_stuck0   FAIL step=FUNC cause=FUNC_LOOPBACK reg=UART0.RXD addr=0x1018 exp=0x000000A5 act=0x00000025 diff=0x00000080 bits=[7] | loopback data mismatch: TX/RX data path bit(s)
```

세 줄 모두 **틀린 팀에게 버그가 간다**. 첫 줄은 "ID tie-off 틀렸다" 로 IP 팀에, 둘째 줄은 "UART 기능 버그" 로 IP 팀에, 셋째 줄은 "loopback 이 깨졌다" 로 — 실제로는 클럭 배선(SoC 통합), 리셋 배선(SoC 통합), 읽기 데이터 버스(인터커넥트)다. 정답 버전은 같은 결함을 이렇게 낸다 (아래 Part C 매트릭스 출력에서):

```text
  clk_stat_lies  FAIL step=ALIVE cause=CLOCK_NOT_APPLIED reg=UART0.SCRATCH addr=0x101C act=0xDEADBEEF | bus error captured at 0x101C although CLK_STAT.UART0=1: clock not reaching IP
  rst_stat_lies  FAIL step=ALIVE cause=RESET_NOT_RELEASED reg=UART0.SCRATCH addr=0x101C exp=0xA5A5A5A5 act=0x00000000 diff=0xA5A5A5A5 bits=[31,29,26,24,23,21,18,16,...+8] | write ignored, reads reset value although RST_STAT.UART0=1: IP still in reset
```

**`bu_read_checked` 의 네 줄:**

```c
wr32(CRU_BUSERR, 1u);              /* ① stale 에러 제거 (W1C) */
*out = rd32(addr);                 /* ② 읽기 */
if (!(rd32(CRU_BUSERR) & 1u)) return true;   /* ③ 확인 */
... wr32(CRU_BUSERR, 1u); return bu_fail(..., CAUSE_CLK, ...);  /* ④ 정리 후 기록 */
```

① 이 없으면 이전 테스트(또는 디버거가 아무 주소나 읽어 본 것)가 남긴 sticky 에러 때문에 정상 장치가 실패한다 — 하네스의 `S.buserr = 1; S.buserr_addr = 0x3000;` 테스트가 이것을 잡는다. ④ 가 없으면 한 테스트의 에러가 다음 테스트의 거짓 실패가 된다. 테스트 하나가 **끝날 때 환경을 깨끗이 남기는 것** 은 수백 개 테스트를 연달아 돌리는 validation 에서 필수다 (SIV-B08).

`0xDEADBEEF` 라는 값 자체로 판단하지 않는 이유: 그것은 이 mock 인터커넥트의 규칙일 뿐이다. 칩마다 에러 시 읽기 데이터는 0, `0xFFFFFFFF`, 마지막 버스 값, 아무 값이나 될 수 있고, 진짜 ID 가 우연히 그 값일 수도 있다. 에러 로거나 abort/BusFault 처럼 **에러를 에러로 알려 주는 경로** 가 있으면 그것을 쓰고, 없을 때만 값 시그니처로 1차 분류한다 (추가 연습 3).

**"ID 가 읽혔다" ≠ "리셋이 풀렸다".** ID·VERSION 은 보통 상수 tie-off 라서 플롭이 리셋 중이어도 읽힌다. `rst_stat_lies` 테스트에서 `bu_check_id` 가 **통과** 하는 것을 하네스가 확인한다 (`/* ID 는 리셋 중에도 읽힌다! */`). 리셋이 풀렸다는 증거는 "쓴 값이 남는다" 뿐이라서 RW scratch 레지스터를 쓴다. scratch 가 없는 IP 라면 기능에 영향이 없는 RW 필드(예: 분주비)를 쓰고 원래 값으로 되돌린다.

**두 패턴인 이유.** `0xA5A5A5A5` 와 `0x5A5A5A5A` 는 서로 보수다. 하나만 쓰면 그 패턴에서 0 인 비트의 stuck-at-0, 1 인 비트의 stuck-at-1 은 못 본다. 둘을 쓰면 32비트 모두가 0 과 1 을 한 번씩 거친다. 인접 비트가 교대로 0/1 이라 인접 short 도 일부 드러난다 (체계적인 커버리지는 SIV-B05 walking 1s/0s). `rdata_stuck0 = 0x80` 의 실제 리포트는 `act=0xA5A5A525 diff=0x00000080 bits=[7]` — 버스 데이터 비트 7 을 바로 가리킨다.

**한도 경계와 idempotence.** `clk_settle = 7` 이면 8번째 읽기에서 1 → PASS, `polls=8`. `= 8` 이면 FAIL. 경계 양쪽을 테스트해 두면 나중에 `MAX_POLLS` 를 바꾸거나 `<` / `<=` 를 실수했을 때 바로 걸린다. 또 bring-up 을 두 번 불러도(이미 켜진 클럭, 이미 풀린 리셋) 같은 결과여야 한다 — 다른 테스트가 bring-up 을 전제 조건으로 다시 부르는 일이 흔하기 때문이다.

### Part C — 전체 시퀀스, 버전, 기능

```c
memset(r, 0, sizeof *r);
if (!bu_clock_enable(r) || !bu_reset_release(r) || !bu_check_alive(r) ||
    !bu_check_id(r) || !bu_check_version(r) || !bu_func_loopback(r))
    return false;
```

**첫 실패에서 멈추는 이유.** 클럭이 없는데 계속 진행하면 UART 접근마다 버스 에러(실제 칩에선 행)가 난다. 리포트가 쓸모 있는 것은 **첫 번째** 실패뿐이고, 그 뒤의 실패는 대개 같은 원인의 메아리다. 하네스는 `clk_dead`·`rst_stuck` 때 `uart_accesses() == 0` — UART 주소를 한 번도 건드리지 않았는지까지 확인한다.

**ALIVE 를 ID 앞에 둔 이유.** 위 표처럼 ID 는 리셋 중에도 읽히므로, ID 를 먼저 보면 "ID OK" 라는 잘못된 안심을 준다. 쓰기가 먹는지 먼저 확인하고 나서 ID 를 본다. (ID 를 먼저 보는 팀도 있다 — 그 경우 "ID OK" 를 "디코드와 클럭 OK" 로만 해석하면 된다. 중요한 것은 각 체크포인트가 **무엇을 증명하는지** 를 정확히 아는 것이다.)

**버전 판정: `==` 가 아니라 "major 같고 minor 이상".** major 가 다르면 레지스터 맵이 바뀌었을 수 있으니 거부, minor 가 높으면 하위 호환 기능 추가로 보고 통과, minor 가 낮으면 필요한 기능(여기선 v1.2 의 LOOP 비트라고 가정)이 없다. 넷리스트에 구버전 IP 가 들어간 것은 first silicon 에서 실제로 의심하는 항목이다. `ver = 0x00010105`(1.1.5) 의 리포트는 `exp=0x00010200 act=0x00010105 diff=0x00000300 bits=[9,8]` — 마스크 `0x00FFFF00` 으로 patch 차이는 리포트에서 뺐다.

**기본 기능은 가장 작은 end-to-end 경로.** BAUD 를 쓰고 되읽어 RW 레지스터를 확인하고, 내부 loopback 으로 TX → RX 데이터 경로를 한 바이트 통과시킨다. 핀·보드·상대 장치가 필요 없는 범위에서 IP 가 "일을 한다" 를 보이는 것이 bring-up 의 마지막 칸이다. 외부 loopback(TX 핀-RX 핀 점퍼), 보율 정확도, 인터럽트(SIV-B11)는 그다음 테스트들의 몫이다. `0xA5` 를 쓰는 이유는 비트 7 과 5 를 모두 1 로 가진 교대 패턴이라 RX 경로 stuck-at-0 이 드러나기 때문이다: `rx_stuck0 = 0x20` → `act=0x00000085 bits=[5]`. 끝나면 `CTRL=EN` 으로 loopback 을 꺼서 **다음 테스트가 쓸 수 있는 상태** 로 남긴다.

정답 코드의 결함 매트릭스 **실제 출력**:

```text
fault matrix:
  clk_dead       FAIL step=CLK cause=CLOCK_NOT_APPLIED reg=CRU.CLK_STAT addr=0x0004 exp=0x00000008 act=0x00000001 diff=0x00000008 bits=[3] polls=8 | CLK_EN.UART0 written but clock never reported running (gate/PLL/clock tree)
  clk_stat_lies  FAIL step=ALIVE cause=CLOCK_NOT_APPLIED reg=UART0.SCRATCH addr=0x101C act=0xDEADBEEF | bus error captured at 0x101C although CLK_STAT.UART0=1: clock not reaching IP
  rst_stuck      FAIL step=RST cause=RESET_NOT_RELEASED reg=CRU.RST_STAT addr=0x000C exp=0x00000008 act=0x00000001 diff=0x00000008 bits=[3] polls=8 | RST_CTRL.UART0 deasserted but reset-done never set (reset wiring/synchronizer)
  rst_stat_lies  FAIL step=ALIVE cause=RESET_NOT_RELEASED reg=UART0.SCRATCH addr=0x101C exp=0xA5A5A5A5 act=0x00000000 diff=0xA5A5A5A5 bits=[31,29,26,24,23,21,18,16,...+8] | write ignored, reads reset value although RST_STAT.UART0=1: IP still in reset
  rdata_stuck0   FAIL step=ALIVE cause=BUS_DATA_PATH reg=UART0.SCRATCH addr=0x101C exp=0xA5A5A5A5 act=0xA5A5A525 diff=0x00000080 bits=[7] | scratch readback corrupted: data-path bit(s) stuck
  id_xor         FAIL step=ID cause=ID_MISMATCH reg=UART0.ID addr=0x1000 exp=0x55415254 act=0x55415354 diff=0x00000100 bits=[8] | wrong IP at this base or ID tie-off error
  ver_old        FAIL step=VER cause=VERSION_UNSUPPORTED reg=UART0.VER addr=0x1004 exp=0x00010200 act=0x00010105 diff=0x00000300 bits=[9,8] | IP version older than required (need 1.2+): wrong netlist/IP drop?
  rx_stuck0      FAIL step=FUNC cause=FUNC_LOOPBACK reg=UART0.RXD addr=0x1018 exp=0x000000A5 act=0x00000085 diff=0x00000020 bits=[5] | loopback data mismatch: TX/RX data path bit(s)
  clk_settle_5   PASS UART0 bring-up: ID=0x55415254 VER=1.2.0 checkpoints=6/6
64/64 passed
```

`clk_dead` 의 `act=0x00000001` 에서 bit0 은 TIMER0 클럭(정상)이다 — `mask` 로 bit3 만 비교했기 때문에 diff 는 bit3 하나만 나온다. 마스크 없이 비교했다면 이웃 IP 의 상태가 매번 "다른 비트" 로 섞여 리포트를 흐린다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">결함 매트릭스 — 칸 = 체크포인트 결과 (✓ 통과 · ✗ 여기서 발견 · 빈칸 = 실행 안 함)</text>
  <g fill="currentColor" font-weight="bold">
    <text x="12" y="48">결함</text><text x="152" y="48">CLK</text><text x="222" y="48">RST</text><text x="292" y="48">ALIVE</text>
    <text x="362" y="48">ID</text><text x="432" y="48">VER</text><text x="502" y="48">FUNC</text><text x="572" y="48">cause</text>
  </g>
  <g stroke="currentColor" opacity=".35"><line x1="10" y1="56" x2="730" y2="56"/></g>
  <g fill="currentColor" font-family="monospace">
    <text x="12" y="80">clk_dead</text><text x="12" y="108">clk_stat_lies</text><text x="12" y="136">rst_stuck</text>
    <text x="12" y="164">rst_stat_lies</text><text x="12" y="192">rdata_stuck0</text><text x="12" y="220">id_xor</text>
    <text x="12" y="248">ver_old</text><text x="12" y="276">rx_stuck0</text><text x="12" y="304">clk_settle_5</text>
  </g>
  <g stroke="currentColor">
    <rect x="145" y="64" width="60" height="22" class="f-warn"/>
    <rect x="145" y="92" width="60" height="22" class="f-ok"/><rect x="215" y="92" width="60" height="22" class="f-ok"/><rect x="285" y="92" width="60" height="22" class="f-warn"/>
    <rect x="145" y="120" width="60" height="22" class="f-ok"/><rect x="215" y="120" width="60" height="22" class="f-warn"/>
    <rect x="145" y="148" width="60" height="22" class="f-ok"/><rect x="215" y="148" width="60" height="22" class="f-ok"/><rect x="285" y="148" width="60" height="22" class="f-warn"/>
    <rect x="145" y="176" width="60" height="22" class="f-ok"/><rect x="215" y="176" width="60" height="22" class="f-ok"/><rect x="285" y="176" width="60" height="22" class="f-warn"/>
    <rect x="145" y="204" width="60" height="22" class="f-ok"/><rect x="215" y="204" width="60" height="22" class="f-ok"/><rect x="285" y="204" width="60" height="22" class="f-ok"/><rect x="355" y="204" width="60" height="22" class="f-warn"/>
    <rect x="145" y="232" width="60" height="22" class="f-ok"/><rect x="215" y="232" width="60" height="22" class="f-ok"/><rect x="285" y="232" width="60" height="22" class="f-ok"/><rect x="355" y="232" width="60" height="22" class="f-ok"/><rect x="425" y="232" width="60" height="22" class="f-warn"/>
    <rect x="145" y="260" width="60" height="22" class="f-ok"/><rect x="215" y="260" width="60" height="22" class="f-ok"/><rect x="285" y="260" width="60" height="22" class="f-ok"/><rect x="355" y="260" width="60" height="22" class="f-ok"/><rect x="425" y="260" width="60" height="22" class="f-ok"/><rect x="495" y="260" width="60" height="22" class="f-warn"/>
    <rect x="145" y="288" width="60" height="22" class="f-ok"/><rect x="215" y="288" width="60" height="22" class="f-ok"/><rect x="285" y="288" width="60" height="22" class="f-ok"/><rect x="355" y="288" width="60" height="22" class="f-ok"/><rect x="425" y="288" width="60" height="22" class="f-ok"/><rect x="495" y="288" width="60" height="22" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="175" y="80">✗</text>
    <text x="175" y="108">✓</text><text x="245" y="108">✓</text><text x="315" y="108">✗</text>
    <text x="175" y="136">✓</text><text x="245" y="136">✗</text>
    <text x="175" y="164">✓</text><text x="245" y="164">✓</text><text x="315" y="164">✗</text>
    <text x="175" y="192">✓</text><text x="245" y="192">✓</text><text x="315" y="192">✗</text>
    <text x="175" y="220">✓</text><text x="245" y="220">✓</text><text x="315" y="220">✓</text><text x="385" y="220">✗</text>
    <text x="175" y="248">✓</text><text x="245" y="248">✓</text><text x="315" y="248">✓</text><text x="385" y="248">✓</text><text x="455" y="248">✗</text>
    <text x="175" y="276">✓</text><text x="245" y="276">✓</text><text x="315" y="276">✓</text><text x="385" y="276">✓</text><text x="455" y="276">✓</text><text x="525" y="276">✗</text>
    <text x="175" y="304">✓</text><text x="245" y="304">✓</text><text x="315" y="304">✓</text><text x="385" y="304">✓</text><text x="455" y="304">✓</text><text x="525" y="304">✓</text>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="572" y="80">CLOCK</text><text x="572" y="108">CLOCK (상태 거짓)</text><text x="572" y="136">RESET</text>
    <text x="572" y="164">RESET (상태 거짓)</text><text x="572" y="192">BUS_DATA_PATH</text><text x="572" y="220">ID_MISMATCH</text>
    <text x="572" y="248">VERSION</text><text x="572" y="276">FUNC_LOOPBACK</text><text x="572" y="304">OK (polls=6)</text>
  </g>
</svg>
```

핵심 관찰: `clk_stat_lies` 와 `rst_stat_lies` 는 **CLK/RST 칸을 통과한다**. 상태 레지스터만 보는 bring-up 은 그 두 결함을 원인 칸에서 절대 못 잡는다. 발견 단계(ALIVE)와 원인(CLOCK/RESET)이 다른 칸에 있다는 것이 이 매트릭스의 요점이다.

### 실제 검증 현장과의 연결

| 단계 | 같은 bring-up 이 어떻게 쓰이나 |
|---|---|
| Pre-silicon (RTL 시뮬) | UVM 환경에서 클럭·리셋 시퀀스 뒤에 레지스터 모델 시퀀스(`uvm_reg_hw_reset_seq` 로 리셋값, `uvm_reg_bit_bash_seq` 로 access type)를 돌린다. C 테스트를 CPU 모델에서 그대로 돌리는 SoC 레벨 테스트도 흔하다 |
| 에뮬레이션 / FPGA | 같은 C bring-up 을 실제 속도에 가깝게. 클럭 트리는 실물과 다를 수 있음 (SIV-B13) |
| First silicon (bench) | JTAG + OpenOCD 의 `mww`/`mdw` 로 한 줄씩 손으로 시작 → 같은 시퀀스를 on-chip 펌웨어나 스크립트로 자동화 |
| ATE / SLT | ATE 는 주로 스캔·BIST 같은 구조 테스트, SLT 는 실제 부팅·워크로드. bring-up 체크포인트는 SLT 와 시스템 validation 의 전제 조건 테스트로 남는다 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
# 실제 출력: 위의 fault matrix 9줄 + "64/64 passed", exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san
# 실제 출력: 64/64 passed, sanitizer 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `wr32(CRU_CLK_EN, UART0_BIT);` | 공유 레지스터 통째 쓰기 → 다른 IP(TIMER0) 클럭이 꺼짐 | `wr32(CRU_CLK_EN, rd32(CRU_CLK_EN) \| UART0_BIT);` (또는 SET alias) |
| `while (!(rd32(CLK_STAT) & BIT));` | 클럭이 안 오는 칩에서 테스트가 영원히 멈춤, 리포트도 없음 | 한도 있는 폴링 + 마지막 값·횟수 기록 (SIV-B09) |
| 리셋을 먼저 풀고 클럭을 켬 | 동기 해제가 클럭 에지를 필요로 함. 칩에 따라 IP 가 정의되지 않은 상태로 깸 | 클럭 → 확인 → 리셋 해제 → 확인 |
| ID 가 읽히면 리셋도 풀렸다고 판단 | ID 는 tie-off 상수라 리셋 중에도 읽힘 | RW scratch 쓰기/되읽기로 확인 |
| `id == 0xDEADBEEF` 로 버스 에러 판정 | 에러 데이터 값은 칩마다 다름, 진짜 값과 겹칠 수 있음 | 버스 에러 로거·abort 경로로 판정, 값은 보조 |
| 버스 에러 로거를 읽기만 하고 안 지움 | sticky 라서 이전 에러가 다음 테스트를 실패시킴 | 확인 전 W1C 로 비우고, 잡힌 뒤에도 비움 |
| 실패 후에도 다음 단계 진행 | 클럭 없는 IP 접근 → 버스 에러/행, 리포트가 메아리로 오염 | 첫 실패에서 멈춤 (`\|\|` 사슬) |
| 상태 비교에 마스크 없이 `s == UART0_BIT` | 이웃 IP 비트(TIMER0)가 섞여 항상 실패하거나 diff 가 흐려짐 | `s & UART0_BIT`, 리포트 mask 도 해당 비트만 |
| 버전을 `ver == 0x00010200` 로 비교 | 하위 호환 상위 버전(1.3)을 거부, patch 차이에도 실패 | major 같고 minor ≥ 필요값 |
| loopback 을 켠 채로 bring-up 종료 | 다음 테스트(외부 핀 테스트)가 이상한 상태에서 시작 | 끝에 `CTRL=EN` — 알려진 상태로 남김 |

## 8. 면접 Q&A

<details>
<summary>Q1. 첫 실리콘에서 UART 가 아무 응답이 없습니다. 무엇부터 어떤 순서로 보나요?</summary>

바닥부터: ① 전원 도메인이 켜져 있나(PMU 상태) ② IP 클럭 게이트를 켰고 실제로 도나(CLK_STAT, 가능하면 클럭 관측 핀이나 주파수 카운터) ③ 리셋이 풀렸나(RST_STAT) ④ IP 레지스터에 접근 가능한가 — 버스 에러/abort 가 나는지, ID 가 기대값인지 ⑤ RW scratch 쓰기/되읽기 ⑥ 내부 loopback ⑦ 그다음에야 핀(pinmux, 패드 설정), 보드(레벨 시프터, 커넥터), 보율. 각 단계가 무엇을 증명하는지 말하는 것이 핵심이다. 꼬리질문 "JTAG 으로 ID 를 읽었더니 0 이 나왔다면?" → 리셋 중이면서 ID 도 리셋값 0 인 설계, 클럭 게이트된 IP 가 0 을 돌려주는 패브릭, 주소 맵 구멍(RAZ) 가능성. 버스 에러 로거나 디버거의 에러 응답을 같이 본다.

</details>

<details>
<summary>Q2. 왜 클럭을 먼저 켜고 리셋을 푸나요? 리셋을 걸 때도 클럭이 필요한가요?</summary>

흔한 리셋 동기화기는 "비동기 assert, 동기 deassert" 다. 해제는 IP 클럭 에지에 맞춰 퍼지므로 클럭이 없으면 해제가 끝나지 않는다 — 그래서 클럭 먼저. assert 는 비동기라 클럭 없이도 걸리지만, IP 가 **동기 리셋** 만 쓰는 플롭(또는 리셋 중 몇 사이클 동안 내부 상태를 정리하는 로직)을 갖고 있으면 assert 중에도 클럭 사이클이 필요하다. 그래서 많은 SoC 매뉴얼이 "클럭을 켜고 N 사이클 리셋 유지 후 해제" 를 요구한다. 꼬리질문 "끌 때는?" → 반대 순서: IP 기능 정지 → 리셋 assert → 마지막에 클럭 게이트 (추가 연습 1). 클럭부터 끄면 그 뒤의 레지스터 접근이 버스 에러가 된다.

</details>

<details>
<summary>Q3. CLK_STAT 은 1 인데 IP 접근에서 버스 에러가 납니다. 가능한 원인과 증명 방법은?</summary>

가능성: 게이트 출력이 다른 IP 로 배선됨(통합 실수), 상태 비트가 실제 클럭이 아니라 enable 레지스터를 되돌려 줌, 클럭 소스(PLL)가 lock 안 됨, IP 가 다른 전원 도메인이라 꺼져 있음, 버스 브리지 쪽 클럭이 따로 있음(APB 브리지 클럭 미인가). 증명: 클럭 관측 mux 로 그 클럭을 핀에 내보내 스코프로 보기, 클럭 모니터/주파수 카운터 레지스터, 다른 비트를 하나씩 켜 보며 IP 가 살아나는지 확인(배선 오류 탐색), 전원 상태 확인. 리포트에는 "CLK_STAT=1 인데 버스 에러 @주소" 를 둘 다 남겨서 "상태와 현실이 다르다" 는 사실 자체가 버그 리포트가 되게 한다.

</details>

<details>
<summary>Q4. bring-up 에서 폴링 한도는 어떻게 정하고, 타임아웃 났을 때 무엇을 로그에 남기나요?</summary>

스펙의 최대 지연(PLL lock 시간, 리셋 해제 사이클 수)에 여유를 곱해 정하고, 단위는 실제 칩에선 시간(µs)이다 — 읽기 횟수는 버스 속도에 따라 의미가 바뀐다. 로그에는 ① 무엇을 기다렸나(레지스터, 마스크, 기대값) ② 마지막으로 읽은 값 ③ 기다린 시간/횟수 ④ 호출 위치·단계 ⑤ 관련 상태(같은 순간의 CLK_EN, RST_CTRL, 버스 에러 로거). 이 레슨의 리포트가 `reg= exp= act= bits= polls=` 를 다 찍는 이유다. 실시간 타임아웃 헬퍼는 SIV-B09. 꼬리질문 "느린 칩에서만 가끔 타임아웃이 난다면?" → 한도가 스펙 최대값보다 짧은지, 온도/전압 코너 의존인지 기록을 비교한다. 그런 경우 한도를 늘리기 전에 **관측된 지연 분포** 를 로그로 모은다.

</details>

<details>
<summary>Q5. ID 레지스터 확인은 정확히 무엇을 증명하나요? 그것만으로 충분한가요?</summary>

"이 주소에 기대한 종류의 IP 가 있고, 버스로 읽기가 된다" 까지다. ID·VERSION 은 보통 상수 tie-off 라서 리셋이 안 풀렸어도, 쓰기 경로가 깨졌어도 읽힌다. 그래서 RW scratch 쓰기/되읽기, 버전 판정, 최소 기능 테스트가 뒤따라야 한다. 실제 예로 Arm PrimeCell 주변장치(PL011 UART 등)는 블록 끝에 PeriphID/CellID 레지스터를 두고, Linux AMBA 버스 코드는 클럭을 켠 뒤 이를 읽어 드라이버를 고른다. 꼬리질문 "ID 비트 하나만 다르면?" → tie-off 오류이거나 그 데이터 비트가 stuck — scratch 패턴으로 데이터 경로를 먼저 배제한 뒤 IP 팀에 리포트한다.

</details>

<details>
<summary>Q6. bring-up 이 리셋 재시도 끝에 성공했습니다. PASS 인가요?</summary>

기능적으로는 진행할 수 있지만 **결과는 WARN** 으로 남기고 버그를 연다. 한 번에 안 풀리는 리셋은 타이밍 마진(리셋 동기화기, 전원 시퀀스) 문제일 수 있고, 양산에서는 일정 비율의 칩이 부팅 실패로 이어진다. 재시도 횟수와 어떤 단계에서 몇 번 만에 성공했는지 기록해야 칩·온도·전압별 통계를 낼 수 있다 (추가 연습 2). 재시도를 조용히 삼키는 코드는 validation 에서는 버그를 숨기는 코드다.

</details>

<details>
<summary>Q7. 왜 실패 리포트에 "발견 단계" 와 "원인" 을 따로 두나요?</summary>

같은 증상을 여러 원인이 만들기 때문이다. 리셋 미해제는 RST 단계에서도(상태가 정직할 때), ALIVE 단계에서도(상태가 거짓일 때) 발견된다. 원인 필드는 **어느 팀에게 가야 하는가**(SoC 통합, 클럭/전원, IP, 보드)를 정하고, 단계 필드는 **어디까지 믿을 수 있는가**(통과한 체크포인트 비트마스크 `done`)를 알려 준다. 리포트를 자동 분류(triage)할 때도 cause 로 묶으면 같은 근본 원인의 실패 수백 개가 한 버그로 모인다 (SIV-B14).

</details>

## 9. 추가 연습

`practice.c` 는 본문 mock 을 인스턴스 여러 개(UART0 @0x1000 bit3, UART1 @0x2000 bit4)로 일반화한 축약판이다.

### 연습 1 — Teardown(전원 내리기) 시퀀스

bring-up 한 UART0 를 안전하게 내리는 `ip_teardown(desc)` 를 작성하라. 마지막에 클럭이 꺼지고, 리셋이 걸려 있고, 버스 에러가 없어야 한다. 주어진 `ip_teardown_buggy()`(클럭부터 끔)가 무엇을 일으키는지도 확인하라.

<details>
<summary>답 보기</summary>

bring-up 의 정확한 역순: ① `CTRL=0` (클럭이 살아 있을 때 IP 기능 정지) ② `RST_CTRL |= bit` 후 `RST_STAT` 이 0 으로 내려갔는지 폴링 ③ 마지막에 `CLK_EN &= ~bit`. 테스트는 접근 기록에서 마지막 CTRL 쓰기 < 마지막 RST_CTRL 쓰기 < 마지막 CLK_EN 쓰기 순서를 확인하고, 리셋 assert 로 scratch/CTRL 이 0 이 됐는지, 다시 bring-up 이 되는지 본다. 버그 버전은 클럭을 끈 뒤 CTRL 에 쓰므로 `BUSERR=1, BUSERR_ADDR=0x1008` 이 된다 — 실제 칩이라면 BusFault 이거나 버스 행이다. 저전력 진입(suspend) 코드에서 자주 나는 버그다.

</details>

### 연습 2 — 리셋 펄스 재시도와 WARN

리셋 해제가 한 번 "걸리는"(`rst_fail = 1`) 결함에서, 최대 `max_retry` 번 리셋을 다시 assert/deassert 해 복구하라. 복구되면 PASS 가 아니라 `WARN` 으로 남겨라.

<details>
<summary>답 보기</summary>

`while (!poll(RST_STAT)) { if (retries == max_retry) return fail(RST); retries++; RST_CTRL |= bit; RST_CTRL &= ~bit; }`. 결과 실제 출력: `WARN ip=UART0 bring-up recovered after reset retry (flaky reset: file a bug)`. `max_retry=0` 이면 같은 결함이 RESET_NOT_RELEASED 로 실패하고, `rst_stuck` 이면 재시도 1번 후 실패(`retries == 1`). 재시도 횟수를 결과에 남기는 것이 핵심이다 (Q6).

</details>

### 연습 3 — 버스 에러 로거가 없는 칩

에러 로거가 없고 에러 응답도 데이터만 바뀌는 칩이라고 하자. ID 읽은 값으로 1차 분류하는 `classify_read(v, expect_id)` 를 작성하라: 기대값 / `0xDEADBEEF` / `0` / `0xFFFFFFFF` / 그 밖.

<details>
<summary>답 보기</summary>

`OK` / `BUS_ERROR_PATTERN`(클럭·전원 꺼짐 또는 디코드 에러 — 그 칩의 에러 데이터 규칙을 문서에서 확인) / `ALL_ZERO`(리셋 중, RAZ 구멍, 게이트된 IP 가 0 을 돌려주는 패브릭) / `ALL_ONES`(매핑 안 된 주소에서 떠 있는 버스, 꺼진 전원 도메인) / `OTHER`(다른 IP 이거나 tie-off 오류 → diff 비트 리포트). 이것은 **가설** 일 뿐이므로 리포트에 "signature" 라고 명시하고, 가능한 한 본문처럼 에러를 에러로 알려 주는 경로(로거, abort/BusFault 핸들러)로 확인한다.

</details>

### 연습 4 — 기술자(descriptor) 테이블 기반 bring-up

`ip_desc_t { name, base, clk_bit, rst_bit, id }` 테이블로 UART0·UART1 을 같은 함수로 올려라. UART1 의 클럭 게이트가 실제로는 CLK_EN bit5 에 배선된 결함(`clk_wire = {0, 5}`)에서 UART1 만 정확히 진단되고 UART0 는 영향받지 않아야 한다.

<details>
<summary>답 보기</summary>

본문 함수의 상수(`UART0_BIT`, `UART0_BASE`)를 `d->clk_bit`, `d->base` 로 바꾸면 된다. 실제 출력: `FAIL ip=UART1 step=CLK cause=CLOCK_NOT_APPLIED reg=CRU.CLK_STAT exp=0x00000010 act=0x00000009 bits=[4]` — `act` 에 UART0(bit3)와 TIMER0(bit0)는 켜져 있고 UART1(bit4)만 꺼져 있다. 기술서(SIV-B01)에서 테이블을 생성하면 인스턴스가 50개여도 같은 코드가 돈다. 주의: UART1 이 UART0 와 **같은 게이트**(bit3)에 잘못 물린 경우는 UART0 가 이미 켜져 있으면 bring-up 이 통과해 버린다. 그런 배선 오류는 "UART0 클럭을 끄면 UART1 도 죽는가" 같은 **격리(isolation) 테스트** 로만 드러난다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
# 실제 출력:
#   WARN ip=UART0 bring-up recovered after reset retry (flaky reset: file a bug)
#   FAIL ip=UART1 step=CLK cause=CLOCK_NOT_APPLIED reg=CRU.CLK_STAT exp=0x00000010 act=0x00000009 bits=[4]
# 23/23 passed   (ASan/UBSan 빌드도 23/23, 보고 없음)
```

## 10. 핵심 정리

- 순서: **클럭 켜기 → 확인 → 리셋 해제 → 확인 → 살아 있음(scratch) → ID → 버전 → 최소 기능**. 내릴 땐 정확히 역순(기능 정지 → 리셋 → 클럭).
- 공유 CRU 레지스터는 내 비트만 바꾼다 (RMW 또는 SET/CLR alias). 폴링은 항상 한도, 실패하면 마지막 값·횟수를 남긴다.
- 상태 레지스터(CLK_STAT/RST_STAT)는 CRU 의 믿음일 뿐 — IP 접근(버스 에러 로거, scratch 쓰기)으로 교차 확인한다.
- ID 는 리셋 중에도 읽히는 tie-off 다. "쓴 값이 남는다" 만이 리셋 해제의 증거.
- 버스 에러 로거는 sticky: 확인 전에 비우고, 잡은 뒤에도 비운다. 에러 데이터 값(0xDEADBEEF 등)으로 판정하지 않는다.
- 리포트 = 발견 단계 + 원인 + reg/addr/exp/act/diff/bits + 통과한 체크포인트. 원인은 "어느 팀으로 가나" 를 정한다.
- 첫 실패에서 멈추고, 끝나면 IP 와 로거를 알려진 상태로 남긴다. 재시도로 성공하면 WARN 으로 기록한다.

## 참고자료

- Arm AMBA AXI and ACE Protocol Specification (IHI 0022) — 응답 코드 OKAY/SLVERR/DECERR; AMBA APB Protocol Specification (IHI 0024) — PREADY, PSLVERR
- Arm PrimeCell UART (PL011) Technical Reference Manual — UARTPeriphID/UARTPCellID 레지스터, 클럭(PCLK/UARTCLK)과 리셋
- Linux kernel: [Common Clock Framework `clk_prepare_enable`](https://docs.kernel.org/driver-api/clk.html), [Reset controller API `reset_control_deassert`](https://docs.kernel.org/driver-api/reset.html), `drivers/amba/bus.c` (pclk 를 켠 뒤 PrimeCell ID 읽기)
- Clifford E. Cummings, Don Mills, Steve Golson, "Asynchronous & Synchronous Reset Design Techniques — Part Deux" (SNUG 2003) — 비동기 assert / 동기 deassert 리셋 동기화기
- [OpenOCD User's Guide — `mdw` / `mww` 메모리 접근 명령](https://openocd.org/doc/html/General-Commands.html)
- IEEE 1800.2 (UVM) 레지스터 시퀀스 `uvm_reg_hw_reset_seq`, `uvm_reg_bit_bash_seq`, `uvm_reg_access_seq`; Accellera SystemRDL 2.0 / IP-XACT (IEEE 1685) — 레지스터 기술서 단일 소스
- 관련 레슨: SIV-B01 (기술서·코드 생성), SIV-B02 (mock MMIO·access type), SIV-B05 (walking 1s/0s, stuck-at), SIV-B06 (주소 디코드), SIV-B08 (테스트 프레임워크), SIV-B09 (poll-with-timeout), SIV-B11 (인터럽트), SIV-B13 (pre/post-silicon 백엔드 교체), SIV-B14 (리포트), C-M01 (MMIO volatile), C-M02 (RMW·W1C·SET/CLR alias)
