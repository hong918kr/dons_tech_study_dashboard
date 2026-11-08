---
id: SIV-B03
time: 70분
updated: 2026-11-08
---
# SIV-B03 리셋값 테스트 (Reset Value Test)

## 학습 목표

- 레지스터 기술서(register description)의 필드 정보만으로 레지스터마다 **기대 리셋값(exp)** 과 **비교 마스크(mask)** 를 계산할 수 있다. 어떤 비트를 빼는지(VOLATILE · UNDEF · WO)와 예약 비트를 어떻게 다루는지 근거를 말할 수 있다.
- 읽기에 부작용이 있는 레지스터(RC, FIFO pop)를 고려해 **"레지스터당 정확히 한 번, 안전한 것만 읽는"** 리셋 테스트를 짠다.
- 불일치를 `FAIL reg=CTRL off=0x004 exp=… act=… diff=… bits=[0] fields=[EN]` 처럼 **레지스터·비트·필드 단위**로 보고해서 디버그 엔지니어가 바로 RTL/통합 담당에게 넘길 수 있게 만든다.
- 블록 전체가 같은 값(0xDEADBEEF 등)으로 읽히는 상황을 레지스터 버그와 구분하는 **트리아지 힌트**를 넣는다.
- 기술서 → C 테스트 벡터 표 생성 → 타깃 러너로 이어지는 흐름을 UVM RAL `uvm_reg_hw_reset_seq`, IP-XACT/SystemRDL, JTAG/OpenOCD 실무와 연결해 설명한다.

## 1. 문제

first silicon이 돌아온 날 **가장 먼저 도는 레지스터 테스트가 리셋값 테스트**다. 쓰기 없이 읽기만 하니 칩을 망가뜨릴 위험이 없고, 클럭·리셋·버스 디코드·tie-off 가 한 번에 확인된다. Rivian Silicon & Sensing, NVIDIA/Apple post-silicon 면접에서 "레지스터 검증을 어디서부터 하겠냐" 고 물으면 첫 대답이 이것이다.

리셋값 테스트는 "읽어서 스펙의 reset 값과 같은지 본다" 로 끝나는 쉬운 문제처럼 보인다. 그런데 실제로 짜보면 함정이 많다.

- STATUS 의 `CTS` 비트는 **외부 핀 레벨**이다. 보드 pull-up 이면 리셋 직후에도 1이다 → 비교하면 오탐(false fail).
- DBG 의 `TAP` 필드는 **리셋되지 않는 플롭**이다. 전원 켤 때마다 값이 다르다 → 비교하면 가끔 fail 하는 flaky 테스트.
- ERRCNT 는 **읽으면 지워지는(RC)** 레지스터다. 두 번 읽으면 두 번째 값은 이미 0 → 리포트가 증거를 잃는다.
- RXDATA 는 **읽으면 FIFO 에서 꺼내는(pop)** 레지스터다. 빈 FIFO 를 읽으면 INT_RAW.RXUF(underflow)가 서고 ERRCNT 가 오른다 → 테스트가 다른 레지스터를 망가뜨린다.

그래서 이 레슨의 핵심은 **기술서에서 "무엇을, 어떤 마스크로, 읽어도 되는지" 를 계산하는 것**이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">기술서 → exp / mask (UART0.STATUS 와 UART0.DBG)</text>
  <text x="10" y="58" fill="currentColor" font-weight="bold">STATUS 0x018</text>
  <g stroke="currentColor">
    <rect x="120" y="40" width="250" height="30" class="f-mut"/>
    <rect x="370" y="40" width="60" height="30" class="f-warn"/>
    <rect x="430" y="40" width="60" height="30" class="f-ok"/>
    <rect x="490" y="40" width="60" height="30" class="f-ok"/>
    <rect x="550" y="40" width="60" height="30" class="f-ok"/>
    <rect x="610" y="40" width="60" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="245" y="60">[31:5] 예약 (0 으로 읽혀야 함)</text>
    <text x="400" y="60">CTS</text><text x="460" y="60">BUSY</text><text x="520" y="60">RXEMP</text>
    <text x="580" y="60">TXFUL</text><text x="640" y="60">TXEMP</text>
    <text x="400" y="88">핀 레벨</text><text x="460" y="88">0</text><text x="520" y="88">1</text>
    <text x="580" y="88">0</text><text x="640" y="88">1</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="120" y="112">exp = 0x00000005   mask = 0xFFFFFFEF   (bit4 CTS 만 뺀다)</text>
  </g>
  <text x="10" y="160" fill="currentColor" font-weight="bold">DBG 0x02C</text>
  <g stroke="currentColor">
    <rect x="120" y="142" width="160" height="30" class="f-warn"/>
    <rect x="280" y="142" width="300" height="30" class="f-mut"/>
    <rect x="580" y="142" width="90" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="200" y="162">TAP [31:24] UNDEF</text>
    <text x="430" y="162">[23:4] 예약</text>
    <text x="625" y="162">SEL [3:0]=0</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="120" y="196">exp = 0x00000000   mask = 0x00FFFFFF</text>
  </g>
  <rect x="120" y="222" width="18" height="14" class="f-ok" stroke="currentColor"/>
  <text x="144" y="234" fill="currentColor" font-size="12">비교: 리셋값이 정의된 필드</text>
  <rect x="310" y="222" width="18" height="14" class="f-mut" stroke="currentColor"/>
  <text x="334" y="234" fill="currentColor" font-size="12">비교: 예약 비트 = 0 (RAZ 정책)</text>
  <rect x="520" y="222" width="18" height="14" class="f-warn" stroke="currentColor"/>
  <text x="544" y="234" fill="currentColor" font-size="12">제외: VOLATILE/UNDEF/WO</text>
  <text x="10" y="270" fill="currentColor">diff = (act ^ exp) &amp; mask — 0 이면 PASS. 마스크 밖의 비트는 얼마나 틀려도 보고하지 않는다.</text>
  <text x="10" y="292" fill="currentColor">비교할 필드가 하나도 없으면 (TXDATA=WO, TIMER=VOLATILE) mask=0 → 읽지도 않고 SKIP.</text>
</svg>
```

이 레슨의 가상 IP는 **UART0** 다 (SIV-B01 의 regmap.json 이 만든 표를 C 배열로 축약해 넣었고, mock 은 SIV-B02 의 mock 을 읽기 경로만 남겨 축약했다).

| off | 레지스터 | 필드 (리셋값) | 특이점 | 리셋 테스트 |
|---|---|---|---|---|
| 0x000 | ID | REV[15:8]=0x01, PART[31:16]=0x5541 | RO | 검사 (exp 0x55410100) |
| 0x004 | CTRL | EN, TXEN, RXEN, PAR[4:3], STOP2 = 0 | RW | 검사 |
| 0x008 | BAUD | DIV[15:0]=0x001A | RW (48MHz/16/115200 ≈ 26) | 검사 |
| 0x00C | TXDATA | DATA[7:0] | **WO** | SKIP (비교할 비트 없음) |
| 0x010 | RXDATA | DATA[7:0] UNDEF | **읽으면 FIFO pop** (RF_NOREAD) | SKIP (읽기 부작용) |
| 0x014 | FIFOCFG | TXTH[3:0]=4, RXTH[11:8]=8 | RW | 검사 |
| 0x018 | STATUS | TXEMPTY=1, TXFULL=0, RXEMPTY=1, BUSY=0, CTS | CTS **VOLATILE** | 검사 (bit4 제외) |
| 0x01C | INT_RAW | TXDONE, RXDONE, RXUF = 0 | W1C | 검사 |
| 0x020 | INT_EN | 같은 3비트 = 0 | RW | 검사 |
| 0x024 | ERRCNT | CNT[7:0]=0 | **RC** | 검사 (한 번만 읽기) |
| 0x028 | TIMER | COUNT[31:0] | **VOLATILE** (free-running) | SKIP (비교할 비트 없음) |
| 0x02C | DBG | SEL[3:0]=0, TAP[31:24] UNDEF | TAP **UNDEF** | 검사 (TAP 제외) |

### Part A — 기대값·마스크 계산과 레지스터 1개 검사

- `reset_expect(r, &exp, &mask)`: 필드를 돌며 `exp` 에 리셋값을 모으고, 비교 대상 필드 비트와 **예약 비트(어느 필드에도 없는 비트)** 를 `mask` 에 넣는다. VOLATILE · UNDEF 플래그가 있거나 access 가 WO 인 필드는 뺀다. 비교 대상 필드가 하나도 없으면 `exp = mask = 0`.
- `reset_check_one(bus, r, &res)`: `RF_NOREAD` 이거나 `mask == 0` 이면 **읽지 않고** `RES_SKIP`. 아니면 bus 로 **정확히 한 번** 읽어 `act`, `diff = (act ^ exp) & mask` 를 채우고 PASS/FAIL.

### Part B — 비트·필드 단위 리포트와 함정 처리

- `bits_to_str(diff)`: 1인 비트를 MSB 부터 연속 구간으로 묶는다. `0xF1 → "[7:4,0]"`, `0x1A → "[4:3,1]"`, `0 → "[]"`.
- `fields_of(r, diff)`: diff 와 겹치는 필드 이름을 표 순서대로, 예약 비트가 섞였으면 끝에 `rsvd`. `CTRL, 0x41 → "[EN,rsvd]"`.
- 테스트로 확인할 함정: 제외 비트에 결함을 넣어도 PASS(오탐 금지), RC 레지스터는 **첫 읽기 값**이 리포트에 남고 읽기 횟수 1, RXDATA·TXDATA 는 읽기 횟수 0, RXDATA 를 읽었다면 INT_RAW 가 오탐 난다는 것.

### Part C — UART0 전체 실행: 결함 주입, 죽은 블록, 벡터 생성

- `reset_test_run(bus, regs, n, rep, cap)`: 표 순서로 전부 검사하고 `FAIL …` / `SKIP … why=…` 줄, `SUMMARY reset_value: pass= fail= skip=` 줄을 쓴다. **검사한 레지스터가 3개 이상 전부 실패하고 모두 같은 값**이면 `HINT …` 줄을 덧붙인다 (클럭 게이트/리셋 유지/디코드 안 됨 의심).
- 결함 주입: 진짜 결함 2개 — `CTRL.EN` 리셋이 1 (tie-off 실수), `BAUD.DIV` 리셋이 0x0000 — 와 **미끼 2개** — STATUS.CTS, DBG.TAP (제외 비트) — 를 넣고, 리포트에 정확히 2줄만 나오는지 확인한다.
- `emit_reset_vectors("UART0", …)`: 기술서에서 `{ "NAME", off, exp, mask }` 표를 C 소스로 생성한다 (SKIP 대상은 이유 주석). 이 표가 타깃에서 도는 리셋 테스트의 입력이 된다.

## 2. Problem (English)

> **Register reset-value test, generated from a register description.**
>
> You are given a register map for an IP block: each register has a name, a byte offset and a list of fields `{name, lsb, width, access ∈ {RW,RO,WO,W1C,W1S,RC}, reset, flags}` where `flags` may contain `VOLATILE` (hardware-driven, not deterministic after reset) or `UNDEF` (not reset). A register may carry `NOREAD` (reading it has side effects, e.g. pops a FIFO). Reads go through `uint32_t bus->read32(ctx, off)`.
>
> 1. `void reset_expect(const reg_desc_t *r, uint32_t *exp, uint32_t *mask)` — compare mask = bits of every field that is not VOLATILE/UNDEF/WO, plus reserved bits (bits covered by no field, expected to read as 0). If no field is checkable, return `exp = mask = 0`.
> 2. `res_t reset_check_one(const bus_t *b, const reg_desc_t *r, reset_res_t *out)` — skip (without reading) NOREAD registers and registers with `mask == 0`; otherwise read **exactly once** and fill `act` and `diff = (act ^ exp) & mask`.
> 3. `bits_to_str(diff)` → `"[7:4,0]"` (MSB-first ranges), `fields_of(r, diff)` → `"[EN,PAR]"` / `"[rsvd]"`.
> 4. `tally_t reset_test_run(...)` — run the whole block, emit one line per failure:
>    `FAIL reg=CTRL off=0x004 exp=0x00000000 act=0x00000001 diff=0x00000001 bits=[0] fields=[EN]`, one `SKIP` line per skipped register with the reason, a `SUMMARY` line, and a `HINT` line if ≥3 registers were checked, all failed, and all returned the same value.
> 5. `emit_reset_vectors(...)` — generate a C table `{ "NAME", 0xOFFu, 0xEXPu, 0xMASKu }` for the checkable registers.
>
> Inject two reset-value defects into the mock and show the report names the register and the bit(s); show that defects in excluded bits are **not** reported.
>
> Follow-ups: How do you handle read-clear registers? A register whose reset depends on a strap pin? A block that returns 0xDEADBEEF everywhere?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 27.2 SDK 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/SIV-B03_st && /tmp/SIV-B03_st
# 경고 0개로 빌드되고, FAIL 줄들 뒤에 "6/41 passed", exit 1 — TODO 6개를 채워 41/41 을 만든다
```

mock(전원 인가 상태 생성 `mock_power_on`, 읽기 경로 `mock_read32`)과 하네스는 주어진 코드다. 고치지 않는다. mock 은 같은 기술서로 리셋 이미지를 만들고 그 위에 **결함(`fault_t`: 비트 XOR)** 만 덧씌운다 — 실리콘이라면 RTL 의 리셋 로직·tie-off 가 이 역할을 한다.

## 4. 힌트

<details>
<summary>힌트 1 — mask 는 "비교할 비트" 이고 예약 비트도 포함한다</summary>

필드마다 `fm = field_mask(width) << lsb`. 모든 필드의 `fm` 을 OR 한 것이 `covered`, 비교 대상 필드만 OR 한 것이 `chk`. 예약 비트는 `~covered`. 그래서 `mask = chk | ~covered`. 단, `chk == 0` 이면 "읽을 이유가 없는 레지스터" 라서 0/0 을 돌려준다 (그렇지 않으면 TXDATA 를 예약 비트 때문에 읽게 된다).

</details>

<details>
<summary>힌트 2 — 읽기는 한 번, 결과는 변수에</summary>

`act = b->read32(b->ctx, r->off);` 를 한 번만 하고 비교·리포트 모두 `act` 를 쓴다. 리포트를 찍을 때 다시 읽으면 RC 레지스터는 이미 0, TIMER 같은 카운터는 이미 다른 값이다.

</details>

<details>
<summary>힌트 3 — bits_to_str 은 두 겹 while</summary>

`b = 31` 부터 내려가며 0 비트는 건너뛴다. 1 비트를 만나면 `hi = b`, 계속 1인 동안 내려간 뒤 `lo = b + 1`. `hi == lo` 면 `"b"`, 아니면 `"hi:lo"`. 첫 항목 앞에는 쉼표를 안 붙인다. `b` 는 `int` 로 두어야 `b >= 0` 비교가 의미가 있다 (`unsigned` 면 0 에서 내려갈 때 무한 루프).

</details>

<details>
<summary>힌트 4 — HINT 조건은 "같은 값으로 전부 실패"</summary>

검사한(SKIP 아닌) 레지스터의 첫 `act` 를 기억하고, 이후 다른 `act` 가 나오면 `same = false`. 루프 뒤 `fail >= 3 && pass == 0 && same` 이면 HINT. 레지스터 몇 개만 틀린 경우와 블록이 통째로 죽은 경우는 원인도 담당자도 다르다.

</details>

<details>
<summary>힌트 5 — 생성기는 reset_expect 를 재사용한다</summary>

`emit_reset_vectors` 는 테스트와 **같은** 규칙(NOREAD → skip, mask==0 → skip)으로 표를 만든다. 규칙이 두 곳에 따로 있으면 언젠가 어긋난다. 오프셋은 `0x%03Xu`, 값은 `0x%08Xu` 로 찍는다 (`uint32_t` 는 `(unsigned)` 로 캐스트해 `%X` 에 넘긴다).

</details>

## 5. 풀이

### 전체 흐름

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="sivb03-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">reset_test_run — 레지스터 하나당 흐름</text>
  <rect x="10" y="40" width="150" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="85" y="65" fill="currentColor" text-anchor="middle">기술서 reg_desc_t</text>
  <rect x="200" y="40" width="150" height="40" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="275" y="65" fill="currentColor" text-anchor="middle">RF_NOREAD ?</text>
  <rect x="385" y="40" width="180" height="40" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="475" y="65" fill="currentColor" text-anchor="middle" font-size="12">reset_expect → mask==0 ?</text>
  <rect x="595" y="40" width="135" height="40" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="660" y="58" fill="currentColor" text-anchor="middle" font-size="12">SKIP 줄 (why=…)</text>
  <text x="660" y="74" fill="currentColor" text-anchor="middle" font-size="12">읽기 0회</text>
  <path d="M160 60 L198 60" stroke="currentColor" marker-end="url(#sivb03-arr)"/>
  <path d="M350 60 L383 60" stroke="currentColor" marker-end="url(#sivb03-arr)"/>
  <text x="367" y="52" fill="currentColor" font-size="12" text-anchor="middle">no</text>
  <path d="M565 60 L593 60" stroke="currentColor" marker-end="url(#sivb03-arr)"/>
  <text x="579" y="52" fill="currentColor" font-size="12" text-anchor="middle">yes</text>
  <path d="M275 80 L275 100 L660 100 L660 82" stroke="currentColor" fill="none" stroke-dasharray="4 3" marker-end="url(#sivb03-arr)"/>
  <text x="440" y="96" fill="currentColor" font-size="12" text-anchor="middle">yes (FIFO pop 등)</text>
  <path d="M470 80 L470 128" stroke="currentColor" marker-end="url(#sivb03-arr)"/>
  <text x="482" y="114" fill="currentColor" font-size="12">no</text>
  <rect x="360" y="130" width="220" height="40" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="470" y="155" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">act = read32(off)  ← 단 1회</text>
  <path d="M470 170 L470 196" stroke="currentColor" marker-end="url(#sivb03-arr)"/>
  <rect x="340" y="198" width="260" height="40" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="470" y="223" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">diff = (act ^ exp) &amp; mask</text>
  <path d="M340 218 L222 218" stroke="currentColor" marker-end="url(#sivb03-arr)"/>
  <text x="282" y="210" fill="currentColor" font-size="12" text-anchor="middle">diff == 0</text>
  <rect x="120" y="198" width="100" height="40" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="170" y="223" fill="currentColor" text-anchor="middle">PASS</text>
  <path d="M600 218 L640 218 L640 254" stroke="currentColor" fill="none" marker-end="url(#sivb03-arr)"/>
  <text x="660" y="236" fill="currentColor" font-size="12">≠ 0</text>
  <rect x="440" y="256" width="290" height="40" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="585" y="274" fill="currentColor" text-anchor="middle" font-size="12">FAIL reg off exp act diff</text>
  <text x="585" y="290" fill="currentColor" text-anchor="middle" font-size="12">bits=bits_to_str  fields=fields_of</text>
  <text x="10" y="276" fill="currentColor" font-size="12">루프가 끝나면: SUMMARY pass/fail/skip</text>
  <text x="10" y="296" fill="currentColor" font-size="12">+ (fail ≥ 3, pass == 0, act 전부 같음) → HINT</text>
  <text x="10" y="320" fill="currentColor" font-size="12">읽기 순서 = 기술서 순서. 쓰기는 한 번도 없다 → 리셋 테스트는 항상 맨 처음, 다른 테스트보다 먼저 돈다.</text>
</svg>
```

### Part A — 무엇을 빼고, 왜 빼나

| 비트 종류 | 예 | mask 에 넣나 | 이유 |
|---|---|---|---|
| 리셋값이 정의된 RW/RO/W1C/W1S/RC 필드 | CTRL.EN, ID.PART, INT_RAW | **넣는다** | 스펙이 값을 약속한 비트 |
| 예약 비트 (어느 필드에도 없음) | CTRL[31:6] | **넣는다 (0 기대)** | "reserved, read as zero" 가 흔한 규칙. 안 넣으면 **필드 누락**(기술서에 없는 플롭이 RTL 에 있음)을 못 잡는다 |
| VOLATILE | STATUS.CTS, TIMER.COUNT | 뺀다 | 핀·카운터·HW 상태가 바꾼다 → 값이 보드/타이밍에 따라 다름 |
| UNDEF (리셋 없음) | DBG.TAP, RXDATA.DATA | 뺀다 | 리셋이 없는 플롭/RAM: 전원마다 다름 → flaky |
| WO | TXDATA.DATA | 뺀다 | 읽기 값이 스펙에 없음 (0일 수도, 버스 에러일 수도) |

예약 비트 정책은 **팀/IP 마다 다르다**. "reserved 는 읽기 값 미정(undefined)" 으로 적힌 스펙이면 `mask = chk` 로 바꿔야 한다. 이 레슨은 RAZ(Read-As-Zero) 정책을 쓰고, 그 정책을 리포트와 md 에 명시한다 — 정책이 암묵적이면 같은 실패를 두 팀이 다르게 해석한다.

`chk == 0` 일 때 0/0 을 돌려주는 것도 정책이다. TXDATA 는 `DATA[7:0]` 만 WO 이고 [31:8] 은 예약이라, 분기 없이 `chk | ~covered` 를 쓰면 mask = 0xFFFFFF00 이 되어 **WO 데이터 레지스터를 읽게 된다**. 어떤 버스 구조에서는 WO 레지스터 읽기가 버스 에러(AXI SLVERR → Arm data abort)다. "비교할 필드가 없으면 읽지 않는다" 가 안전하다.

`(f->reset << f->lsb) & fm` 의 `& fm` 은 기술서의 reset 값이 필드 폭보다 큰 경우를 자른다. 그런 기술서는 원래 SIV-B01 의 lint 가 거부해야 하지만, 테스트 코드는 기술서를 믿지 않는 쪽이 낫다.

### Part B — 리포트가 바로 행동으로 이어지게

`bits_to_str` 를 `0x1A` 로 따라가 보자 (`0b1_1010`).

| b | 비트 | 동작 | 버퍼 |
|---|---|---|---|
| 31..5 | 0 | 건너뜀 | `[` |
| 4 | 1 | hi=4, 3 도 1 → 계속, 2 는 0 → lo=3 | `[4:3` |
| 2 | 0 | 건너뜀 | |
| 1 | 1 | hi=1, 0 은 0 → lo=1 | `[4:3,1` |
| 0 | 0 | 건너뜀, 끝 | `[4:3,1]` |

리포트에 `diff=0x0000001A` 만 있으면 사람은 머릿속으로 2진수를 풀어야 한다. `bits=[4:3,1] fields=[DIV]` 이면 **"BAUD.DIV 리셋값 틀림"** 이 한 눈에 보이고, RTL 담당자에게 그대로 붙여 넣을 수 있다. `rsvd` 가 찍히면 원인 후보가 달라진다 — 기술서에 없는 비트가 1로 읽힌다는 건 **기술서가 RTL 보다 오래됐거나**(필드 추가 후 문서 미반영) 디코드가 다른 레지스터를 보고 있다는 뜻이다.

**RC 레지스터 — 첫 읽기가 유일한 증거.**

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">ERRCNT (RC) 에 리셋 글리치로 0x03 이 남아 있을 때</text>
  <text x="700" y="22" fill="currentColor" text-anchor="end" font-size="12" opacity=".7">시간 →</text>
  <rect x="10" y="40" width="150" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="85" y="65" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">ERRCNT = 0x03</text>
  <rect x="200" y="40" width="200" height="40" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="300" y="58" fill="currentColor" text-anchor="middle" font-size="12">읽기 1: act = 0x03</text>
  <text x="300" y="74" fill="currentColor" text-anchor="middle" font-size="12">HW 가 0 으로 지움</text>
  <rect x="440" y="40" width="290" height="40" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="585" y="65" fill="currentColor" text-anchor="middle" font-size="12">FAIL ERRCNT act=0x03 bits=[1:0] [CNT]</text>
  <path d="M160 60 L198 60" stroke="currentColor"/><path d="M400 60 L438 60" stroke="currentColor"/>
  <rect x="200" y="110" width="200" height="40" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="300" y="128" fill="currentColor" text-anchor="middle" font-size="12">잘못된 코드: 리포트용으로</text>
  <text x="300" y="144" fill="currentColor" text-anchor="middle" font-size="12">다시 읽기 → 0x00</text>
  <rect x="440" y="110" width="290" height="40" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="585" y="128" fill="currentColor" text-anchor="middle" font-size="12">act=0x00000000 diff=0 → 증거 없음</text>
  <text x="585" y="144" fill="currentColor" text-anchor="middle" font-size="12">(또는 재실행 시 PASS → "재현 안 됨")</text>
  <path d="M400 130 L438 130" stroke="currentColor"/>
  <text x="10" y="185" fill="currentColor">RC/FIFO/카운터가 있는 블록에서 "실패 시 한 번 더 읽어 확인" 은 증거 인멸이다. 값은 변수에 한 번만 받는다.</text>
</svg>
```

정답 코드의 테스트가 `m.reads[ERRCNT] == 1 && m.val[ERRCNT] == 0` 를 확인하는 이유가 이것이다 — 읽기는 한 번이었고, 그 한 번으로 레지스터는 이미 지워졌다. 실리콘에서 이 실패를 재실행하면 PASS 한다 ("간헐적 실패" 로 오분류되기 쉽다). 그래서 리셋 테스트는 **전원 인가 직후 첫 번째 테스트**여야 하고, 실패 로그는 첫 읽기 값을 남겨야 한다.

**NOREAD — 테스트가 칩 상태를 바꾸면 안 된다.** 정답 코드는 일부러 RXDATA 를 한 번 읽은 다음 INT_RAW 를 검사해 `diff == 0x4` (RXUF) 가 나는 것을 확인한다. 순진한 "모든 오프셋 읽기" 테스트라면 이 실패는 RTL 버그처럼 보이지만 실제로는 **테스트가 만든 실패**다. OpenOCD 에서 `mdw base 12` 로 블록 전체를 덤프할 때도 똑같이 일어난다.

### Part C — 실제로 돌린 결과

결함 2개 + 미끼 2개 주입 (`/tmp/SIV-B03_so` 출력에서 발췌, 그대로 붙임):

```text
---- UART0 reset test, 4 faults injected (2 real, 2 in excluded bits) ----
FAIL reg=CTRL off=0x004 exp=0x00000000 act=0x00000001 diff=0x00000001 bits=[0] fields=[EN]
FAIL reg=BAUD off=0x008 exp=0x0000001A act=0x00000000 diff=0x0000001A bits=[4:3,1] fields=[DIV]
SKIP reg=TXDATA off=0x00C why=no-checkable-bits
SKIP reg=RXDATA off=0x010 why=read-side-effect
SKIP reg=TIMER off=0x028 why=no-checkable-bits
SUMMARY reset_value: pass=7 fail=2 skip=3
```

- STATUS.CTS 와 DBG.TAP 에 넣은 미끼 결함은 리포트에 없다 — 이게 없으면 매 보드·매 전원마다 다른 오탐이 생기고, 엔지니어는 리셋 테스트 결과를 **무시하는 습관**이 든다. 그게 진짜 버그를 놓치는 가장 흔한 경로다.
- SKIP 줄은 커버리지 감사(audit)용이다. "9/12 레지스터 검사, 3개는 왜 안 했는지" 가 로그에 남아야 리뷰어가 정책을 확인할 수 있다.
- `CTRL.EN` 리셋이 1 이면 실제 칩에서는 부팅 직후 UART 가 TX 라인을 구동해 **보드 레벨 글리치**가 생긴다 — 리셋값 하나가 시스템 버그로 번지는 전형.

블록이 클럭 게이트된 경우 (mock 의 `dead = 0xDEADBEEF`):

```text
---- UART0 reset test, block clock-gated ----
FAIL reg=ID off=0x000 exp=0x55410100 act=0xDEADBEEF diff=0x8BECBFEF bits=[31,27,25:21,19:18,15,13:5,3:0] fields=[REV,PART,rsvd]
FAIL reg=CTRL off=0x004 exp=0x00000000 act=0xDEADBEEF diff=0xDEADBEEF bits=[31:30,28:25,23,21,19:18,16:15,13:9,7:5,3:0] fields=[EN,TXEN,RXEN,PAR,STOP2,rsvd]
FAIL reg=BAUD off=0x008 exp=0x0000001A act=0xDEADBEEF diff=0xDEADBEF5 bits=[31:30,28:25,23,21,19:18,16:15,13:9,7:4,2,0] fields=[DIV,rsvd]
SKIP reg=TXDATA off=0x00C why=no-checkable-bits
SKIP reg=RXDATA off=0x010 why=read-side-effect
FAIL reg=FIFOCFG off=0x014 exp=0x00000804 act=0xDEADBEEF diff=0xDEADB6EB bits=[31:30,28:25,23,21,19:18,16:15,13:12,10:9,7:5,3,1:0] fields=[TXTH,RXTH,rsvd]
FAIL reg=STATUS off=0x018 exp=0x00000005 act=0xDEADBEEF diff=0xDEADBEEA bits=[31:30,28:25,23,21,19:18,16:15,13:9,7:5,3,1] fields=[TXFULL,BUSY,rsvd]
FAIL reg=INT_RAW off=0x01C exp=0x00000000 act=0xDEADBEEF diff=0xDEADBEEF bits=[31:30,28:25,23,21,19:18,16:15,13:9,7:5,3:0] fields=[TXDONE,RXDONE,RXUF,rsvd]
FAIL reg=INT_EN off=0x020 exp=0x00000000 act=0xDEADBEEF diff=0xDEADBEEF bits=[31:30,28:25,23,21,19:18,16:15,13:9,7:5,3:0] fields=[TXDONE,RXDONE,RXUF,rsvd]
FAIL reg=ERRCNT off=0x024 exp=0x00000000 act=0xDEADBEEF diff=0xDEADBEEF bits=[31:30,28:25,23,21,19:18,16:15,13:9,7:5,3:0] fields=[CNT,rsvd]
SKIP reg=TIMER off=0x028 why=no-checkable-bits
FAIL reg=DBG off=0x02C exp=0x00000000 act=0xDEADBEEF diff=0x00ADBEEF bits=[23,21,19:18,16:15,13:9,7:5,3:0] fields=[SEL,rsvd]
SUMMARY reset_value: pass=0 fail=9 skip=3
HINT all 9 checked regs read 0xDEADBEEF: block unclocked/held in reset/not decoded? check clock gate + reset ctrl before filing an RTL bug
```

FAIL 9줄은 버그 9개가 아니라 **원인 1개**다. 꺼진 블록을 읽었을 때 무엇이 돌아오는지는 인터커넥트 구현마다 다르다 — 0, 0xFFFFFFFF, 0xDEADBEEF 같은 고정 패턴을 돌려주는 default slave 도 있고, 버스 에러 응답(AXI SLVERR/DECERR)으로 CPU 가 abort 를 받는 구조도 있다. 어느 쪽이든 "모든 레지스터가 같은 값" 은 레지스터 문제가 아니라 클럭·리셋·전원·주소 디코드 문제이고, 담당도 IP 설계자가 아니라 SoC 통합/클럭 리셋 담당이다 (bring-up 순서는 SIV-B10 참고).

| 결과 패턴 | 가장 그럴듯한 원인 | 다음 행동 |
|---|---|---|
| 레지스터 1~2개, 필드 안의 몇 비트 | 리셋 로직·tie-off 실수, 또는 기술서 오타 | RTL 리셋값과 기술서 둘 다 확인 → 어느 쪽 버그인지 정해서 담당 지정 |
| `fields=[…,rsvd]` | 기술서에 없는 비트 (문서가 RTL 보다 오래됨) / 다른 레지스터 디코드 | 기술서 버전 확인, 주소 디코드는 SIV-B06 |
| 블록 전체 같은 값 | 클럭 게이트, 리셋 유지, 전원 도메인 off, 디코드 안 됨 | 클럭/리셋 컨트롤러 레지스터부터 확인 |
| 전원마다 비트가 흔들림 | 리셋 없는 플롭인데 UNDEF 표시 누락 / 메타안정 | 여러 번 전원 사이클 → flaky mask (추가 연습 3) |
| 같은 IP 4개 중 1개만 실패 | 인스턴스별 통합(tie-off, 파라미터) | 추가 연습 4 |
| FIFO/카운터 읽은 뒤부터 실패 | 테스트가 만든 부작용 | NOREAD 표시, 읽기 순서 확인 |

**생성된 벡터 표와 타깃 러너.** `emit_reset_vectors` 출력은 그대로 C 헤더가 된다:

```bash
/tmp/SIV-B03_so | sed -n '/^\/\* generated/,/^};/p' > gen_uart0_reset_vec.h
cc -std=c11 -Wall -Wextra -Werror -O2 demo_gen_runner.c -o /tmp/SIV-B03_gen && /tmp/SIV-B03_gen; echo "exit=$?"
```

```text
/* generated from UART0 regmap by siv_reset_gen - do not edit */
static const reset_vec_t UART0_reset_vec[] = {
    { "ID", 0x000u, 0x55410100u, 0xFFFFFFFFu },
    { "CTRL", 0x004u, 0x00000000u, 0xFFFFFFFFu },
    { "BAUD", 0x008u, 0x0000001Au, 0xFFFFFFFFu },
    /* skip TXDATA: no checkable bits */
    /* skip RXDATA: read side effect */
    { "FIFOCFG", 0x014u, 0x00000804u, 0xFFFFFFFFu },
    { "STATUS", 0x018u, 0x00000005u, 0xFFFFFFEFu },
    { "INT_RAW", 0x01Cu, 0x00000000u, 0xFFFFFFFFu },
    { "INT_EN", 0x020u, 0x00000000u, 0xFFFFFFFFu },
    { "ERRCNT", 0x024u, 0x00000000u, 0xFFFFFFFFu },
    /* skip TIMER: no checkable bits */
    { "DBG", 0x02Cu, 0x00000000u, 0x00FFFFFFu },
};
```

`demo_gen_runner.c` 는 이 헤더만 include 해서 "실리콘에서 읽은 값" 배열(CTRL.EN=1 결함, CTS=1, TAP 쓰레기 포함)을 검사한다. 실제 출력:

```text
FAIL reg=CTRL off=0x004 exp=0x00000000 act=0x00000001 diff=0x00000001
vectors=9 fail=1  touched RXDATA=0 TIMER=0
exit=1
```

<!-- code: demo_gen_runner.c -->

타깃 쪽 러너는 기술서 파서도 문자열 처리도 없이 `{off, exp, mask}` 표 + 루프 하나다. 이것이 생성 방식의 장점이다: 기술서가 바뀌면 CI 가 표를 다시 만들고, 러너 코드는 바뀌지 않는다. 같은 표를 JTAG 으로 돌리는 호스트 스크립트(OpenOCD `mdw`/`read_memory` 로 오프셋별 1회 읽기)나 on-chip 펌웨어 테스트가 공유할 수 있다.

### 결함 모델 → 리셋값 테스트가 잡는가

| 결함 | 리셋값 테스트 | 더 잘 잡는 테스트 |
|---|---|---|
| 리셋 로직 누락/잘못된 tie-off (필드 리셋값 틀림) | **잡는다** (이 레슨의 주 목표) | — |
| 기술서와 RTL 불일치 (문서 오타, 필드 추가 미반영) | **잡는다** (`rsvd` 포함) | SIV-B01 lint 는 문서 내부 모순만 |
| 클럭 미인가/리셋 미해제/전원 off | 잡는다 (HINT) | SIV-B10 bring-up |
| RO 비트에 write 가 먹음, W1C 가 안 지워짐 | 못 잡는다 (쓰기를 안 함) | SIV-B04 |
| stuck-at (리셋값과 같은 쪽으로 stuck) | **못 잡는다** — 리셋 0, stuck-at-0 이면 통과 | SIV-B05 walking 1s/0s |
| 주소 비트 무시(aliasing) | 리셋값이 다르면 우연히 잡힘, 같으면 놓침 | SIV-B06 |
| 리셋 없는 플롭인데 문서가 리셋 있다고 함 | 가끔 잡힘(flaky) | 전원 사이클 반복 (추가 연습 3) |

"stuck-at 을 못 잡는다" 는 면접 단골 꼬리질문이다. 리셋값 테스트는 **한 점(리셋 상태)만 본다**. 비트가 0 에 묶여 있어도 리셋값이 0 이면 구분할 수 없다. 그래서 레지스터 검증은 reset → R/W(access) → bit-bash/walking → aliasing 순서로 쌓는다.

### 실무와 연결

- **UVM RAL (pre-silicon 대응물)**: `uvm_reg_hw_reset_seq` 가 블록의 모든 레지스터를 읽어 모델의 리셋값과 비교한다. 레지스터에 `NO_REG_HW_RESET_TEST` (또는 `NO_REG_TESTS`) 속성을 resource DB 로 걸면 제외된다 — 이 레슨의 `RF_NOREAD` 와 같은 역할. volatile 로 선언된 필드는 비교에서 빠지는 것이 기본 동작이다. 같은 regmap 에서 생성된 RAL 모델과 이 레슨의 C 벡터 표가 **같은 기대값**을 쓴다는 것이 pre-/post-silicon 재사용의 핵심 (SIV-B13 참고).
- **기술서 포맷**: SystemRDL 은 필드의 `reset` 속성이 없으면 리셋이 없는 필드다 (이 레슨의 UNDEF). 하드웨어가 쓰는 필드(`hw=w`/`rw`, counter 등)는 RAL 생성기가 volatile 로 다루는 경우가 많다. IP-XACT 는 reset 에 값과 함께 mask(리셋이 정의된 비트)를 줄 수 있다. 어느 쪽이든 결론은 같다: **mask 는 기술서에서 계산된다** — 손으로 쓰지 않는다.
- **post-silicon 실행 경로**: ① JTAG/SWD 디버거(OpenOCD, Lauterbach)로 호스트가 레지스터를 읽는다 — CPU 가 아직 안 뜬 날에도 된다. ② 칩 안 CPU 에서 도는 펌웨어 테스트 — 빠르고 대량 반복 가능. ③ ATE(양산 테스터)는 주로 스캔/BIST 로 구조 결함을 보고, SLT(시스템 레벨 테스트)와 시스템 검증 단계에서 이런 기능 레지스터 테스트가 돈다. 리셋값 테스트는 ①에서 가장 먼저 돈다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/SIV-B03_so && /tmp/SIV-B03_so   # 41/41 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/SIV-B03_san && /tmp/SIV-B03_san
# ASan/UBSan 경고 없이 41/41 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `if (read32(off) != reset)` — 마스크 없이 전체 비교 | CTS 핀, TAP 쓰레기 때문에 보드·전원마다 오탐. 팀이 결과를 무시하게 됨 | `diff = (act ^ exp) & mask` |
| mask 에서 예약 비트를 뺌 (`mask = chk`) | RTL 에 문서에 없는 비트가 생겨도 못 잡음 | 정책이 RAZ 면 `chk \| ~covered` |
| `chk == 0` 분기 없이 `chk \| ~covered` | TXDATA 같은 WO 레지스터를 예약 비트 때문에 읽음 → 버스 에러 가능 | `if (chk == 0) { 0/0; SKIP }` |
| 실패하면 리포트 찍으려고 다시 읽기 | RC 는 이미 0, 카운터는 다른 값 → 증거 인멸 | `act` 를 한 번만 받아 재사용 |
| 오프셋 0..N 을 전부 덤프 (`mdw base N`) | RXDATA pop → INT_RAW.RXUF, ERRCNT 오탐 | 기술서의 NOREAD/skip 을 존중하는 표 기반 읽기 |
| 다른 테스트 후에 리셋 테스트 실행 | 앞 테스트가 쓴 값이 남아 "리셋값" 이 아님 | 전원/리셋 직후 첫 테스트, 또는 매번 리셋 후 실행 |
| `for (unsigned b = 31; b >= 0; b--)` | `unsigned >= 0` 은 항상 참 → 무한 루프. 이 맥의 Apple clang 21 은 `-Wall -Wextra` 로도 경고하지 않았다 (GCC 는 `-Wextra` 의 `-Wtype-limits` 로 경고) | `int b` |
| `(1u << width) - 1` 에 width=32 | `1u << 32` 는 UB (C-B01 참고) — TIMER 가 32비트 필드 | `width >= 32 ? 0xFFFFFFFFu : …` |
| `printf("%X", uint32_t)` | `uint32_t` 가 `unsigned long` 인 타깃에서 포맷 불일치 | `(unsigned)` 캐스트 또는 `PRIX32` |

## 8. 면접 Q&A

<details>
<summary>Q1. 새 IP 블록 레지스터 검증을 어디서부터 시작하겠습니까? 왜 리셋값부터인가요?</summary>

리셋값 테스트부터다. 읽기만 하므로 칩 상태를 바꾸지 않아 가장 안전하고, 실패하면 이후 테스트(R/W, bit-bash)가 전부 의미 없어지는 기초(클럭·리셋·디코드)를 먼저 확인한다. 순서는 reset → access type R/W(SIV-B04) → walking/bit-bash(SIV-B05) → aliasing(SIV-B06) → 기능. 꼬리질문 "리셋값 테스트가 통과하면 레지스터가 정상인가?" → 아니다. 리셋 한 점만 본다. 리셋값과 같은 방향의 stuck-at, 쓰기 경로 결함은 못 잡는다.

</details>

<details>
<summary>Q2. RC(read-to-clear) 레지스터는 리셋값 테스트에서 어떻게 다룹니까?</summary>

읽어도 된다 — 리셋 직후 RC 레지스터의 첫 읽기 값이 바로 리셋값이다. 단 **정확히 한 번** 읽고 그 값을 변수에 보관해 비교와 로그에 같이 쓴다. 실패 시 재확인하려고 다시 읽으면 이미 지워진 값을 보게 되어 증거가 사라지고, 재실행하면 PASS 해서 "재현 불가" 로 오분류된다. 꼬리질문 "RC 가 다른 레지스터 상태를 바꾸면?" (예: 인터럽트 상태 읽기가 IRQ 라인을 내림) → 읽기 순서를 기술서에 명시하고, 부작용이 다른 레지스터에 미치면 NOREAD 로 빼거나 그 레지스터를 마지막에 읽는다.

</details>

<details>
<summary>Q3. 마스크(mask)는 어떻게 정합니까? 손으로 씁니까?</summary>

기술서에서 계산한다. 리셋값이 정의된 필드 비트 + 예약 비트(RAZ 정책일 때)를 넣고, volatile(HW·핀이 바꾸는 값), 리셋 없는 필드, WO 필드를 뺀다. 손으로 쓴 마스크는 regmap 이 바뀔 때 반드시 어긋난다. UVM RAL 도 같은 regmap 에서 생성된 모델의 volatile/reset 정보로 비교 대상을 정한다. 꼬리질문 "예약 비트는 왜 넣나?" → 문서에 없는 비트가 RTL 에서 1로 나오면 문서가 낡았거나 디코드가 틀렸다는 신호다. 스펙이 reserved 를 "undefined" 로 정의하면 빼고, 그 정책을 로그에 남긴다.

</details>

<details>
<summary>Q4. 리셋값 테스트에서 모든 레지스터가 0xDEADBEEF(또는 0xFFFFFFFF)로 읽힙니다. 무엇을 의심하나요?</summary>

레지스터 버그가 아니라 블록 접근 자체의 문제다: 클럭 게이트 off, 리셋 유지, 전원 도메인 off, 주소 디코드가 default slave 로 감, 보안 설정으로 접근 차단. 확인 순서: 클럭 컨트롤러 enable 비트 → 리셋 컨트롤러 → 전원 상태 → 주소 맵. 꺼진 블록 접근이 버스 에러(AXI SLVERR/DECERR)를 내는 구조면 CPU 가 abort 를 받으므로, 테스트 러너는 fault handler 로 "어느 주소에서 abort 났나" 를 기록해야 한다. 그래서 리포트에 "전부 같은 값" HINT 를 자동으로 넣는다 — FAIL 9줄을 RTL 버그 9개로 접수하면 시간 낭비다.

</details>

<details>
<summary>Q5. 리셋값이 strap 핀이나 fuse 에 따라 달라지는 필드는요?</summary>

기술서에 그 필드를 "외부 값 의존" 으로 표시하고, 기대값을 테스트 시점에 보드/퓨즈 정보에서 받아 계산한다 (추가 연습 1). 하드코딩하면 보드 A 에서 통과하고 보드 B 에서 실패하는 테스트가 된다. 꼬리질문 "strap 값 자체를 어떻게 아나?" → 보드 설정 파일(테스트 인자), 또는 SoC 의 strap 래치 레지스터를 먼저 읽어 교차 확인. 둘이 다르면 그 자체가 보드/패드 버그 후보다.

</details>

<details>
<summary>Q6. 테스트가 실패했는데 RTL 버그인지 기술서 버그인지 어떻게 가릅니까?</summary>

리셋값 테스트는 "실리콘 vs 기술서" 비교라서 둘 중 어느 쪽도 틀릴 수 있다. ① 같은 regmap 으로 돈 pre-silicon `uvm_reg_hw_reset_seq` 결과를 본다 — 시뮬에서도 실패했는데 waive 됐는지, 시뮬은 통과했는데 실리콘만 실패하는지. 시뮬 통과·실리콘 실패면 RTL 보다 통합/tie-off/ECO/합성 쪽 차이를 의심한다. ② 같은 IP 의 다른 인스턴스 결과를 본다 — 전 인스턴스 동일 실패면 IP RTL 또는 기술서, 한 인스턴스만이면 통합(추가 연습 4). ③ RTL 소스의 리셋 블록을 직접 확인. 결론이 나면 기술서 수정 또는 errata/버그 트래커 등록, 그리고 waiver 는 근거와 함께 기술서 쪽에 남긴다.

</details>

<details>
<summary>Q7. soft reset 후에도 같은 테스트를 돌리면 되나요?</summary>

그대로는 안 된다. 리셋 종류(POR, warm/soft reset, 블록 리셋, PCIe hot reset/FLR)마다 리셋되는 레지스터가 다르다. AON(always-on) 도메인이나 PCIe 의 sticky 속성(RWS/ROS/RW1CS 등) 비트는 hot reset·FLR 에도 값이 유지되는 것이 스펙이다. 그래서 기대값은 "리셋 종류 × 도메인" 으로 정해야 하고, soft reset 테스트는 **먼저 비리셋값을 써 놓고** 리셋 후 CORE 는 리셋값으로, AON/sticky 는 이전 값 그대로인지 본다 (추가 연습 2). 쓰지 않고 돌리면 "원래 리셋값이었는데 그대로" 와 "리셋이 걸렸다" 를 구분할 수 없다.

</details>

<details>
<summary>Q8. 리셋값 테스트 결과를 로그에 어떻게 남겨야 디버그 엔지니어가 바로 쓸 수 있나요?</summary>

레지스터 이름, 절대/상대 주소, exp, act, mask, diff, 비트 번호, 필드 이름을 한 줄에. SKIP 은 이유와 함께. 헤더에는 칩 리비전(ID 레지스터 값), 보드 ID, 테스트 FW/스크립트 버전, 리셋 종류, 전압/온도 조건. "diff=0x1A" 만 있는 로그는 사람이 2진수를 풀어야 하고, 이름 없이 주소만 있으면 regmap 버전을 맞춰야 해석된다. JSON 로그 형식과 리포트 집계는 SIV-B08/B14 참고.

</details>

## 9. 추가 연습

### 연습 1 — strap 핀으로 리셋값이 정해지는 필드

`BOOTCFG`: `MODE[1:0]` 은 부트 strap 핀, `ECC_EN[4]=1`, `VER[11:8]=3`. strap 값을 받아 기대값을 만드는 `expect_with_strap(fields, n, strap)` 을 짜고, 보드 A(MODE 핀=3) 실리콘 값 `0x313` 을 보드 B(=2) 기준으로 검사하면 오탐이 나는 것을 보여라.

<details>
<summary>답 보기</summary>

`FF_STRAP` 필드는 `reset` 대신 `strap` 을 쓰고, 나머지는 동일하게 `(v << lsb) & fm` 으로 모은다. strap=2 → `0x312`, strap=3 → `0x313`, strap=7 → 2비트 필드라 잘려서 `0x313`. 보드 B 기준이면 `diff = 0x1` — 칩은 정상인데 테스트가 틀린 것. 기대값의 입력이 "기술서 + 보드 설정" 두 개라는 것이 요점.

</details>

### 연습 2 — soft reset 과 도메인

레지스터마다 `DOM_CORE` / `DOM_AON` 이 있다. soft reset 전에 쓴 값 `before[]` 와 후에 읽은 값 `after[]` 로, CORE 는 리셋값, AON 은 `before` 와 같아야 한다. 실패한 레지스터 인덱스를 비트마스크로 돌려주는 `soft_reset_check` 를 짜고, "AON 레지스터가 리셋돼 버림" 과 "CORE 레지스터가 soft reset 에 안 걸림" 두 결함을 검출하라.

<details>
<summary>답 보기</summary>

`want = (domain == AON) ? before[i] : reset;` 다르면 `bad |= 1u << i`. 테스트의 `before` 는 리셋값과 **다른** 값이어야 한다 (CTRL=0x7, BAUD=0x68) — 그래야 "리셋이 걸렸는지" 가 관측된다. PCIe 의 sticky 비트(hot reset 에도 유지), RTC/wake 설정 레지스터가 실제 예다.

</details>

### 연습 3 — 전원 사이클마다 흔들리는 비트

DBG 를 전원 사이클 5번에 걸쳐 읽은 값 `{0x7C000000, 0x3C000004, 0x7C000000, 0x5C000000, 0x7D000000}` 에서 흔들린 비트 `flaky_mask` 를 구하고, 기술서에 UNDEF 로 표시된 TAP[31:24] 를 뺀 **표시 누락 비트**를 찾아라.

<details>
<summary>답 보기</summary>

`f |= s[i] ^ s[0]` 를 모든 i 에 대해 → `0x61000004`. `& ~0xFF000000` → `0x00000004` (bit2). bit2 는 SEL 필드 안이라 리셋이 있어야 하는 비트가 전원마다 다르다 — 리셋 연결 누락이나 리셋 해제 타이밍(메타안정) 버그 후보. 한 번의 리셋 테스트로는 운 좋게 통과할 수 있어서, 양산 전 검증에서는 전원 사이클 반복이 필요하다.

</details>

### 연습 4 — 같은 IP 여러 인스턴스로 원인 분류

UART0..3 에서 같은 레지스터의 diff 배열을 받아 `CLS_OK`(전부 0) / `CLS_ALL_SAME`(전부 같은 0 아닌 diff) / `CLS_INSTANCE`(그 외) 로 분류하라.

<details>
<summary>답 보기</summary>

`any` 와 `all_same` 두 플래그로 한 번 돈다. 전부 같은 diff 면 IP RTL 또는 기술서 쪽(모든 인스턴스가 같은 RTL 을 쓴다), 일부만이면 인스턴스별 통합 — tie-off, 파라미터, 다른 클럭/리셋 도메인 연결. `{1,1,2,1}` 처럼 전부 실패했지만 diff 가 다르면 역시 INSTANCE 로 본다. 트리아지에서 "누구에게 버그를 넘기나" 를 정하는 첫 질문이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/SIV-B03_pr && /tmp/SIV-B03_pr   # 15/15 passed
```

## 10. 핵심 정리

- 리셋값 테스트 = **기술서에서 계산한** `exp` 와 `mask` 로 `diff = (act ^ exp) & mask`. first silicon 에서 맨 먼저, 쓰기 없이.
- mask: 리셋값이 정의된 필드 + 예약 비트(RAZ 정책) / 제외: VOLATILE, UNDEF, WO. 비교할 필드가 없으면 읽지 않는다.
- 읽기 부작용(FIFO pop, RC)이 있다: 레지스터당 **정확히 한 번**, NOREAD 는 건너뛰고, 값은 변수에 받아 비교·로그에 재사용.
- 리포트는 `reg off exp act diff bits=[hi:lo,…] fields=[…]` — 사람이 2진수를 풀지 않게. SKIP 도 이유와 함께 남긴다.
- 전부 같은 값으로 실패 = 클럭/리셋/전원/디코드 문제 → HINT. FAIL N줄 ≠ 버그 N개.
- 리셋값 테스트는 리셋 한 점만 본다: stuck-at(같은 방향), 쓰기 경로, aliasing 은 B04~B06 이 잡는다.
- 기대값의 입력은 기술서 + 보드(strap) + 리셋 종류/도메인. 같은 regmap 이 UVM RAL(`uvm_reg_hw_reset_seq`)과 post-silicon C 벡터 표를 함께 만든다.

## 참고자료

- [Accellera SystemRDL 2.0](https://www.accellera.org/downloads/standards/systemrdl) — 필드 `reset` 속성, sw/hw 접근 속성
- IEEE 1685 IP-XACT — register/field reset value 와 mask
- [Accellera UVM 1.2 Class Reference](https://www.accellera.org/downloads/standards/uvm) — `uvm_reg_hw_reset_seq`, `NO_REG_HW_RESET_TEST`, `uvm_reg_field` volatile
- [OpenOCD User's Guide](https://openocd.org/doc/html/index.html) — `mdw`, `read_memory` (JTAG/SWD 로 레지스터 읽기)
- P. Mishra, F. Farahmandi (eds.), *Post-Silicon Validation and Debug*, Springer 2019
- PCI Express Base Specification — sticky 레지스터 속성(RWS/ROS/RW1CS)과 reset 종류
- 같은 트랙: SIV-B01(기술서/codegen), SIV-B02(mock MMIO), SIV-B04(R/W), SIV-B05(walking), SIV-B06(aliasing), SIV-B08/B14(로그·리포트), SIV-B10(bring-up)
