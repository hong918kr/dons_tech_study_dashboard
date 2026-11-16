---
id: MEM-B08
time: 60분
updated: 2026-11-15
---
# MEM-B08 메모리 테스트 패턴: walking 1/0, address-in-address

## 학습 목표

- 새 보드 bring-up 의 첫 메모리 테스트 3종 — **data bus**(walking 1), **address bus**(2의 거듭제곱 오프셋), **device**(address-in-address 증가 패턴 + 반전) — 을 Barr 형태 그대로 C로 짜고, 각각 몇 번 접근하는지(64 / L²+5L+2 / 4n) 말한다.
- 결함 주입 RAM 모델(데이터 선 stuck-at·short, 주소 선 stuck-at·short·저항성 open, dead cell)을 만들고, 모든 인스턴스를 넣어 **탐지 매트릭스(fault × test)** 를 출력한 뒤 "놓친 칸" 하나하나의 이유를 설명한다.
- 테스트를 왜 **data → address → device 순서**로 돌리는지(앞 단계가 뒤 단계의 전제) 설명하고, 실패 리포트에서 **원인(데이터 선 / 주소 선 / 저항성 open / 셀)과 선 번호**를 판정하는 함수를 쓴다.
- 이 테스트들이 PASS 해도 남는 것(coupling, retention, 타이밍 마진, 캐시·ECC 가 가리는 결함)을 말하고 March(MEM-B09)·ECC(MEM-B07)로 넘긴다.

## 1. 문제

새 보드에 전원을 넣고 JTAG 으로 붙었을 때 가장 먼저 듣는 질문이 "DDR 살아 있나?" 이고, 면접에서도 "보드 bring-up 메모리 테스트를 짜 보세요 — 어떤 패턴이 어떤 결함을 잡나요?" 는 펌웨어·실리콘 검증 단골 문제다.

외부 RAM 이 고장 나는 방식은 크게 세 층이다. **데이터 선**(D0..D31: 냉납, 솔더 브리지), **주소 선**(A0..A11: 같은 이유 + 디코더), **셀**(칩 안의 비트 하나). 층마다 잘 잡는 테스트가 다르고, 아래 층을 먼저 확인해야 위 층의 결과를 믿을 수 있다. Michael Barr 의 고전 글(Embedded Systems Programming, 2000)이 이 세 테스트를 짧은 C 함수 셋으로 정리했고, U-Boot 의 `mtest`(대체 테스트 경로) 같은 부트로더 메모리 테스트도 비슷하게 데이터 선 → 주소 선 → 전체 패턴 순서다 (버전별 소스 확인 권장). SSD 펌웨어에서 Don 이 본 컨트롤러 DRAM(매핑 테이블 캐시) bring-up 도 똑같이 시작한다.

이 레슨은 실제 보드 대신 **결함을 넣을 수 있는 RAM 모델**(32-bit × 4096 word, 주소 선 A0..A11 은 word 단위) 위에서 세 테스트를 돌린다. 레지스터 하나의 walking 1/0 은 SIV-B05, 레지스터 맵의 주소 디코드 aliasing 은 SIV-B06 에서 했다 — 여기서는 **RAM 전체 + 주소 결함 + 3단계 순서 + 매트릭스**가 몫이다. March 알고리즘과 coupling fault 커버리지는 MEM-B09 다.

```svg
<svg viewBox="0 0 740 350" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">이 레슨의 결함 모델 — 선(line) 결함 vs 셀(cell) 결함</text>
  <rect x="20" y="40" width="150" height="260" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="34" y="64" fill="currentColor">CPU /</text>
  <text x="34" y="82" fill="currentColor">메모리 컨트롤러</text>
  <text x="34" y="280" fill="currentColor" font-size="12">논리 주소·값을 낸다</text>
  <rect x="540" y="40" width="180" height="260" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="554" y="64" fill="currentColor">RAM 4096 × 32</text>
  <g stroke="currentColor" fill="none">
    <rect x="560" y="200" width="20" height="16"/><rect x="580" y="200" width="20" height="16"/><rect x="600" y="200" width="20" height="16"/><rect x="620" y="200" width="20" height="16"/><rect x="640" y="200" width="20" height="16"/><rect x="660" y="200" width="20" height="16"/><rect x="680" y="200" width="20" height="16"/>
    <rect x="560" y="216" width="20" height="16"/><rect x="580" y="216" width="20" height="16"/><rect x="600" y="216" width="20" height="16"/><rect x="620" y="216" width="20" height="16"/><rect x="660" y="216" width="20" height="16"/><rect x="680" y="216" width="20" height="16"/>
    <rect x="560" y="232" width="20" height="16"/><rect x="580" y="232" width="20" height="16"/><rect x="600" y="232" width="20" height="16"/><rect x="620" y="232" width="20" height="16"/><rect x="640" y="232" width="20" height="16"/><rect x="660" y="232" width="20" height="16"/><rect x="680" y="232" width="20" height="16"/>
  </g>
  <rect x="640" y="216" width="20" height="16" class="f-warn" stroke="currentColor"/>
  <text x="560" y="266" fill="currentColor" font-size="12">dead cell: 한 word 의</text>
  <text x="560" y="282" fill="currentColor" font-size="12">비트 하나가 0/1 고정</text>
  <text x="180" y="52" fill="currentColor" font-size="12">데이터 버스 (양방향)</text>
  <g stroke="currentColor"><line x1="170" y1="70" x2="540" y2="70"/><line x1="170" y1="92" x2="540" y2="92"/><line x1="170" y1="114" x2="540" y2="114"/><line x1="170" y1="136" x2="540" y2="136"/><line x1="170" y1="158" x2="540" y2="158"/></g>
  <g fill="currentColor" font-size="12" font-family="monospace"><text x="508" y="66">D0</text><text x="508" y="88">D3</text><text x="508" y="110">D4</text><text x="508" y="132">D5</text><text x="500" y="154">D31</text></g>
  <line x1="400" y1="92" x2="400" y2="114" stroke="currentColor" stroke-width="4"/>
  <text x="408" y="106" fill="currentColor" font-size="12">short: AND / OR</text>
  <circle cx="260" cy="136" r="6" class="f-warn" stroke="currentColor"/>
  <text x="272" y="131" fill="currentColor" font-size="12">SA0: GND 에 붙음</text>
  <text x="180" y="188" fill="currentColor" font-size="12">주소 버스 (CPU → RAM)</text>
  <g stroke="currentColor"><line x1="170" y1="206" x2="540" y2="206"/><line x1="170" y1="230" x2="540" y2="230"/><line x1="170" y1="256" x2="370" y2="256"/><line x1="410" y1="256" x2="540" y2="256"/><line x1="170" y1="282" x2="540" y2="282"/></g>
  <path d="M370 256 L375 250 L381 262 L387 250 L393 262 L399 250 L405 262 L410 256" fill="none" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" font-family="monospace"><text x="508" y="202">A0</text><text x="508" y="226">A3</text><text x="508" y="252">A6</text><text x="500" y="278">A11</text></g>
  <circle cx="260" cy="230" r="6" class="f-warn" stroke="currentColor"/>
  <text x="272" y="225" fill="currentColor" font-size="12">SA1: VDD 에 붙음 (0↔8 alias)</text>
  <text x="200" y="247" fill="currentColor" font-size="12">저항성 open: 직전 주소의 비트를 봄</text>
  <text x="10" y="322" fill="currentColor">잡는 테스트: 데이터 선 → data bus (walking 1) · 주소 선 → address bus (2^k 오프셋)</text>
  <text x="10" y="342" fill="currentColor">셀 → device (address-in-address + 반전). 앞 단계가 PASS 해야 뒤 단계 결과를 믿는다.</text>
</svg>
```

이 레슨의 정의 (데이터시트·논문 대표 모델 — 실제 보드 결함은 저항성·간헐적일 수 있다):

| 결함 | 모델 (코드의 `fault_kind`) | 물리적 원인 예 |
|---|---|---|
| 데이터 선 SA0 / SA1 | 쓰기·읽기 모두 비트 a 가 0 / 1 | 핀이 GND/VDD plane 에 붙은 솔더, 내부 tie |
| 데이터 선 short AND / OR | 비트 a, b 가 둘의 AND (0 이 이김) / OR (1 이 이김) | 인접 볼·배선 브리지 |
| 주소 선 SA0 / SA1 | 물리 주소의 비트 a 가 0 / 1 → 주소 i 와 i⊕2^a 가 같은 칸 (alias) | 같은 이유 |
| 주소 선 short AND / OR | 물리 주소 비트 a, b 가 둘의 AND / OR | 브리지 |
| 주소 선 저항성 open (hold) | RAM 이 이번 주소가 아니라 **직전 접근 주소**의 비트 a 를 본다 | 냉납·금 간 via: RC 가 커서 새 레벨이 늦게 도착 |
| dead cell SA0 / SA1 | word `cell` 의 비트 a 하나만 고정 | 칩 내부 셀 결함 |

완전히 끊긴 open 은 누설·풀업으로 한 레벨에 머물러 SA0/SA1 과 같아진다. 그래서 open 은 "직전 값을 기억하는" 저항성 모델로 따로 넣었다. 실패 리포트 형식은 SIV 레슨과 같다: `FAIL addr=0x.. exp=0x.. act=0x.. diff=0x.. bits=[..]` (여기서 addr 는 word offset).

### Part A — 결함 주입 RAM 모델

- `ram_phys_addr(r, la)`: 논리 주소(CPU 가 낸 word offset) → RAM 이 실제로 보는 물리 주소. 주소 선 SA0/SA1, AND/OR short, open(직전 주소 `last_addr` 의 비트) 을 반영한다.
- `data_xform(f, v)`: 데이터 버스를 지나는 값에 데이터 선 결함을 반영한다. 쓰기와 읽기 양쪽에 똑같이 걸린다 (양방향 버스).
- 주어진 `ram_write` / `ram_read` 가 두 함수를 부르고, dead cell 은 읽을 때 적용한다. `nops` 로 접근 횟수를 센다.

### Part B — Barr 테스트 3종

- `mt_data_bus(r, addr, rep)`: 한 주소에 walking 1 (`1, 2, 4, …, 0x80000000`) 을 쓰고 바로 읽는다. 처음 틀린 패턴에서 멈춘다.
- `mt_address_bus(r, rep)`: 2^k 오프셋(1, 2, 4, …, 2048)에 `0xAAAAAAAA` 를 깔고, ① 0 번지에 `0x55555555` → 2^k 가 바뀌면 **stuck-high** 쪽, ② 2^t 하나씩 `0x55555555` → 0 번지나 다른 2^k 가 바뀌면 **stuck-low / short** 쪽.
- `mt_device(r, rep)`: 모든 word 에 증가 패턴(offset+1, 즉 **address-in-address**)을 쓰고, 확인하며 그 자리에서 반전(`~pattern`)하고, 반전값을 다시 확인한다.

### Part C — bring-up 순서와 원인 판정, 탐지 매트릭스

- `bringup_run`: data bus(@0) → address bus → device, 처음 실패한 단계에서 멈춘다. 판정(verdict):
  - 1단계 실패: `data_line_suspects`(walking 1 을 **끝까지** 돌며 diff 를 OR)를 0 번지와 마지막 번지 **두 곳**에서 구해 교집합 ≠ 0 이면 `DATA_LINE`(선 결함은 어디서나 보인다), 아니면 `CELL`.
  - 2단계 실패: `addr_line_history`(같은 주소를 "직전 접근"만 바꿔 두 번 읽기) ≠ 0 이면 `ADDR_OPEN`, 아니면 `addr_line_suspects`(0↔2^k, 2^j↔2^k alias 탐침) ≠ 0 이면 `ADDR_LINE`, 아니면 `CELL`.
  - 3단계 실패: `CELL`.
- 하네스가 모든 결함 인스턴스(데이터 SA 32+32, short 496+496, 주소 SA 12+12, short 66+66, open 12, dead cell 4096+4096)를 테스트마다 새 RAM 에 넣어 매트릭스를 출력하고, 판정이 정확한 선 번호·셀 주소를 맞히는지 확인한다.

## 2. Problem (English)

You are bringing up external SRAM/DRAM on a new board. Write the classic three-step memory test and prove what each step can and cannot see, using a fault-injectable model of a 4096 × 32-bit word-addressed RAM (address lines A0..A11).

```c
typedef enum { F_NONE, F_DATA_SA0, F_DATA_SA1, F_DATA_AND, F_DATA_OR,
               F_ADDR_SA0, F_ADDR_SA1, F_ADDR_AND, F_ADDR_OR, F_ADDR_OPEN,
               F_CELL_SA0, F_CELL_SA1 } fault_kind;
typedef struct { fault_kind kind; unsigned a, b; uint32_t cell; } fault_t;

uint32_t ram_phys_addr(const ram_t *r, uint32_t logical);   /* apply address-line fault */
uint32_t data_xform(const fault_t *f, uint32_t v);           /* apply data-line fault    */
int mt_data_bus(ram_t *r, uint32_t addr, mt_report *rep);    /* walking 1s at one address  */
int mt_address_bus(ram_t *r, mt_report *rep);                /* power-of-two offsets       */
int mt_device(ram_t *r, mt_report *rep);                     /* increment pattern + invert */
int bringup_run(ram_t *r, bringup_t *out);  /* stage 1/2/3 + verdict DATA_LINE/ADDR_LINE/ADDR_OPEN/CELL + lines */
```

Constraints: each test returns 0 on pass, 1 on the first failure and fills `{phase, addr, trig, exp, act}`; no heap; deterministic; the whole fault sweep runs in under 2 s.

Examples (real output of `solution.c`):

```text
data SA1 D5      : FAIL addr=0x000 exp=0x00000001 act=0x00000021 diff=0x00000020 bits=[5] phase=data-bus trig=0x000
addr SA1 A3      : FAIL addr=0x008 exp=0xAAAAAAAA act=0x55555555 diff=0xFFFFFFFF bits=[31-0] phase=addr-stuck-high trig=0x008
addr OR A2-A9    : FAIL addr=0x200 exp=0xAAAAAAAA act=0x55555555 diff=0xFFFFFFFF bits=[31-0] phase=addr-stuck-low/short trig=0x004
bring-up D3-D12 OR : stage=1 verdict=DATA_LINE lines=0x00001008
bring-up A1-A10 AND: stage=2 verdict=ADDR_LINE lines=0x402
```

Follow-ups: Why power-of-two offsets? Why must the address test write the antipattern both at offset 0 and at each 2^k? Why invert in place in the device test? What does a passing memtest still miss on a DDR system with caches and ECC?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/MEM-B08_st && /tmp/MEM-B08_st
```

TODO 는 아홉 군데다 (Part A 2개, Part B 3개, Part C 4개). 결함 모델 구조체, `ram_write`/`ram_read`, 리포트 출력 `mt_print`, 결함 인스턴스 생성기, 매트릭스 하네스는 주어진다. 스텁은 "결함이 안 보이고 모든 테스트가 PASS" 하는 안전한 틀린 값을 돌려준다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:239  data_xform(&f, 0xFFu) == 0xEFu
FAIL starter.c:247  ram_phys_addr(&R, 0x000) == 0x008u
…
data short AND        496        0/496        0/496        0/496       0/    0/    0       0/496
…
FAIL starter.c:358  bringup_run(&R, &b) == 1 && b.verdict == V_CELL && b.rep.addr == 0
9/54 passed
```

(exit 1. 통과한 9개는 "결함 없음" 사례와 none 행처럼 스텁이 우연히 맞는 것들이다.)

## 4. 힌트

<details><summary>힌트 1 — 주소 결함은 "물리 주소 함수" 하나로</summary>

CPU 는 논리 주소를 내고, RAM 은 핀에 도착한 비트로 칸을 고른다. SA0 이면 `p &= ~(1<<a)`, SA1 이면 `p |= 1<<a`. AND short 는 "둘 다 1 일 때만 1" → 둘 다 1 이 아니면 두 비트를 모두 지운다. OR short 는 하나라도 1 이면 둘 다 세운다. open(hold) 은 `p = (p & ~m) | (last & m)`.

</details>

<details><summary>힌트 2 — data bus 루프 종료 조건</summary>

`for (uint32_t p = 1; p != 0; p <<= 1)` — unsigned 라서 `0x80000000 << 1` 은 0 이 되어 32번 돌고 끝난다. `int` 로 쓰면 비트 31 시프트가 UB 다 (C-B03).

</details>

<details><summary>힌트 3 — address bus 테스트의 두 방향</summary>

0 번지에 antipattern 을 쓰고 2^k 들을 보면 "0 번지에 쓴 것이 2^k 로 샜나" 를 본다 (SA1 이면 0 → 2^k 로 간다). 반대로 2^t 에 antipattern 을 쓰고 0 번지와 다른 2^k 를 보면 "2^t 에 쓴 것이 0 이나 2^k 로 샜나" 를 본다 (SA0, short). 안쪽 루프에서 `off == t` 인 칸은 방금 antipattern 을 쓴 자리이니 비교에서 뺀다.

</details>

<details><summary>힌트 4 — device 테스트는 "확인하며 반전"</summary>

두 번째 루프에서 `read == pattern` 을 확인한 **바로 그 자리**에 `~pattern` 을 쓴다. 두 주소가 alias 이면, 먼저 확인한 쪽이 반전해 둔 값을 뒤쪽이 읽게 되어 패턴이 같아도 걸린다 (연습 3).

</details>

<details><summary>힌트 5 — 판정: 선인가 셀인가</summary>

선 결함은 **어느 주소에서나** 같은 비트가 틀린다 → 두 주소의 의심 비트 교집합. 주소 alias 는 "다른 주소에 쓴 값이 **그대로**" 읽힌다 → `read == ~P` 로 비교해야 셀 결함(비트 하나만 다름)과 구분된다. open(hold) 은 같은 주소인데 직전 접근에 따라 값이 바뀐다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. **층을 나눈다**: 데이터 선 → 주소 선 → 셀. 아래 층이 고장이면 위 층 테스트 결과가 엉뚱한 원인을 가리킨다.
2. **각 층에 최소 비용 테스트**: 데이터 선 32개는 한 주소에서 walking 1 (64 접근). 주소 선 L 개는 2^k 오프셋 L+1 곳 (L²+5L+2 접근). 셀은 전체 (4n 접근).
3. **패턴 선택 이유**를 결함 모델로 말한다: walking 1 은 모든 선 쌍이 어느 단계에선 다른 값 → short 전부. 2^k 오프셋은 0 과 정확히 한 비트 차이 → 주소 선 하나씩. 증가 패턴은 주소마다 다른 값 → alias, 반전은 모든 셀 비트가 0 과 1 을 둘 다 겪게.
4. 실패하면 **선 번호와 방향**까지 리포트한다.

### Part A — 모델: 결함은 "핀에서 보이는 함수"

| 함수 | 결함 | 예 (코드 테스트와 같은 값) |
|---|---|---|
| `data_xform` | SA0 D4 | `0xFF → 0xEF` |
| | SA1 D31 | `0x0 → 0x80000000` |
| | AND D0–D1 | `0x1 → 0x0`, `0x2 → 0x0`, `0x3 → 0x3` |
| | OR D3–D7 | `0x08 → 0x88`, `0x80 → 0x88`, `0x11 → 0x11` |
| `ram_phys_addr` | SA1 A3 | `0x000 → 0x008` (0 에 쓴 값을 8 에서 읽음) |
| | SA0 A11 | `0x800 → 0x000`, `0xFFF → 0x7FF` |
| | AND A2–A5 | `0x004 → 0x000`, `0x020 → 0x000`, `0x024 → 0x024` |
| | OR A2–A5 | `0x004 → 0x024`, `0x020 → 0x024`, `0x003 → 0x003` |
| | open A4 | 직전이 `0x010` 이면 `0x001 → 0x011` |

데이터 선 결함은 양방향으로 걸리므로 "저장된 값" 도 이미 틀어져 있고, 읽을 때 한 번 더 걸린다. 이 모델의 xform 은 멱등(두 번 걸어도 같음)이라 결과가 일관된다.

### Part B — 테스트 3종, 손으로 추적

**data bus (walking 1).** SA1 D5 이면 첫 패턴 `0x1` 을 쓰면 `0x21` 이 읽힌다 → 첫 단계에서 실패, `diff=0x20` 이 고장 선이다. SA0 D5 는 `0x20` 단계에서야 `act=0` 으로 걸린다. Barr 원형은 반환값이 "처음 틀린 패턴" 이지 선 번호가 아니다 (SA1 D5 의 반환값은 `0x1`) — 그래서 리포트에 diff 를 남긴다.

실제 보드에서는 `ram_write/ram_read` 대신 volatile 포인터를 쓴다 (Barr 원형과 같은 모양):

```c
uint32_t memTestDataBus(volatile uint32_t *address) {
    for (uint32_t pattern = 1; pattern != 0; pattern <<= 1) {
        *address = pattern;
        if (*address != pattern) return pattern;   /* volatile 이라 실제로 다시 읽는다 */
    }
    return 0;
}
```

**address bus (2^k 오프셋).** 왜 2^k 인가: 오프셋 2^k 는 0 과 **주소 비트 k 하나만** 다르다. 그러면 주소 선 k 가 고장일 때만 0 과 2^k 가 엉킨다 — 선 L 개를 L+1 곳으로 시험한다. 두 단계가 필요한 이유는 이 표로 보인다:

| 결함 | 0 번지에 antipattern (①) | 2^t 에 antipattern (②) | 실제 결과 |
|---|---|---|---|
| SA1 A3 | 0 → 물리 8. 오프셋 8 을 읽으면 55 → **걸림** | — | `addr-stuck-high`, addr=0x008 |
| SA0 A3 | ①이전에 이미 8 → 물리 0 에 AA 를 썼고, 0 에 55 → 오프셋 8 이 55 → **걸림** | — | `addr-stuck-high`, addr=0x008 |
| OR A2–A9 | 4 와 0x200 이 같은 칸(0x204)이지만 둘 다 AA → 안 보임 | t=4: 55 가 0x204 로 → 오프셋 0x200 이 55 → **걸림** | `addr-stuck-low/short`, addr=0x200, trig=0x004 |
| AND A1–A10 | 2 → 물리 0, 0 에 55 → 오프셋 2 가 55 → **걸림** | — | `addr-stuck-high`, addr=0x002 |

즉 "stuck-high 검사"라는 이름과 달리 ① 은 **0 번지와 엉키는 모든 결함**(SA1, SA0, AND short)을 잡고, ② 는 **0 과 무관하게 2^j 와 2^k 끼리 엉키는 결함**(OR short)을 잡는다. ② 가 없으면 OR short 66개를 전부 놓친다.

SA0 A3 과 SA1 A3 은 둘 다 "i 와 i⊕8 이 같은 칸" 이라 **CPU 쪽에서는 구분이 안 된다** (메모리 절반만 쓰인다, 어느 절반인지는 보이지 않는다). 리포트도 똑같이 addr=0x008 이다. 선 번호(A3)만 확실하고, 방향은 보드에서 측정해야 한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">address bus 테스트: 2^k 오프셋 13곳 (L = 12), 결함별로 값이 새는 곳</text>
  <g fill="currentColor" font-size="12" font-family="monospace" text-anchor="middle">
    <text x="45" y="46">0</text><text x="100" y="46">1</text><text x="155" y="46">2</text><text x="210" y="46">4</text><text x="265" y="46">8</text><text x="320" y="46">10</text><text x="375" y="46">20</text><text x="430" y="46">40</text><text x="485" y="46">80</text><text x="540" y="46">100</text><text x="595" y="46">200</text><text x="650" y="46">400</text><text x="705" y="46">800</text>
  </g>
  <text x="10" y="70" fill="currentColor" font-size="12">정상 ①</text>
  <g stroke="currentColor">
    <rect x="22" y="78" width="46" height="24" class="f-acc"/><rect x="77" y="78" width="46" height="24" class="f-ok"/><rect x="132" y="78" width="46" height="24" class="f-ok"/><rect x="187" y="78" width="46" height="24" class="f-ok"/><rect x="242" y="78" width="46" height="24" class="f-ok"/><rect x="297" y="78" width="46" height="24" class="f-ok"/><rect x="352" y="78" width="46" height="24" class="f-ok"/><rect x="407" y="78" width="46" height="24" class="f-ok"/><rect x="462" y="78" width="46" height="24" class="f-ok"/><rect x="517" y="78" width="46" height="24" class="f-ok"/><rect x="572" y="78" width="46" height="24" class="f-ok"/><rect x="627" y="78" width="46" height="24" class="f-ok"/><rect x="682" y="78" width="46" height="24" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle"><text x="45" y="95">55</text><text x="100" y="95">AA</text><text x="155" y="95">AA</text><text x="210" y="95">AA</text><text x="265" y="95">AA</text><text x="320" y="95">AA</text><text x="375" y="95">AA</text><text x="430" y="95">AA</text><text x="485" y="95">AA</text><text x="540" y="95">AA</text><text x="595" y="95">AA</text><text x="650" y="95">AA</text><text x="705" y="95">AA</text></g>
  <text x="10" y="132" fill="currentColor" font-size="12">SA1 A3 ①</text>
  <g stroke="currentColor">
    <rect x="22" y="140" width="46" height="24" class="f-mut"/><rect x="77" y="140" width="46" height="24" class="f-ok"/><rect x="132" y="140" width="46" height="24" class="f-ok"/><rect x="187" y="140" width="46" height="24" class="f-ok"/><rect x="242" y="140" width="46" height="24" class="f-warn"/><rect x="297" y="140" width="46" height="24" class="f-ok"/><rect x="352" y="140" width="46" height="24" class="f-ok"/><rect x="407" y="140" width="46" height="24" class="f-ok"/><rect x="462" y="140" width="46" height="24" class="f-ok"/><rect x="517" y="140" width="46" height="24" class="f-ok"/><rect x="572" y="140" width="46" height="24" class="f-ok"/><rect x="627" y="140" width="46" height="24" class="f-ok"/><rect x="682" y="140" width="46" height="24" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle"><text x="45" y="157">→8</text><text x="100" y="157">AA</text><text x="155" y="157">AA</text><text x="210" y="157">AA</text><text x="265" y="157">55</text><text x="320" y="157">AA</text><text x="375" y="157">AA</text><text x="430" y="157">AA</text><text x="485" y="157">AA</text><text x="540" y="157">AA</text><text x="595" y="157">AA</text><text x="650" y="157">AA</text><text x="705" y="157">AA</text></g>
  <path d="M45 166 C45 186 265 186 265 166" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="80" y="200" fill="currentColor" font-size="12">0 에 쓴 55 가 물리 8 로 → 오프셋 8 에서 읽힘: FAIL addr=0x008 phase=addr-stuck-high</text>
  <text x="10" y="232" fill="currentColor" font-size="12">OR A2–A9 ② t=4</text>
  <g stroke="currentColor">
    <rect x="22" y="240" width="46" height="24" class="f-ok"/><rect x="77" y="240" width="46" height="24" class="f-ok"/><rect x="132" y="240" width="46" height="24" class="f-ok"/><rect x="187" y="240" width="46" height="24" class="f-acc"/><rect x="242" y="240" width="46" height="24" class="f-ok"/><rect x="297" y="240" width="46" height="24" class="f-ok"/><rect x="352" y="240" width="46" height="24" class="f-ok"/><rect x="407" y="240" width="46" height="24" class="f-ok"/><rect x="462" y="240" width="46" height="24" class="f-ok"/><rect x="517" y="240" width="46" height="24" class="f-ok"/><rect x="572" y="240" width="46" height="24" class="f-warn"/><rect x="627" y="240" width="46" height="24" class="f-ok"/><rect x="682" y="240" width="46" height="24" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle"><text x="45" y="257">AA</text><text x="100" y="257">AA</text><text x="155" y="257">AA</text><text x="210" y="257">55</text><text x="265" y="257">AA</text><text x="320" y="257">AA</text><text x="375" y="257">AA</text><text x="430" y="257">AA</text><text x="485" y="257">AA</text><text x="540" y="257">AA</text><text x="595" y="257">55</text><text x="650" y="257">AA</text><text x="705" y="257">AA</text></g>
  <path d="M210 266 C210 290 595 290 595 266" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="120" y="304" fill="currentColor" font-size="12">4 와 0x200 이 둘 다 물리 0x204 → ① 에서는 둘 다 AA 라 안 보이고 ② 에서 걸린다</text>
  <text x="10" y="324" fill="currentColor" font-size="12">AA = 0xAAAAAAAA (pattern), 55 = 0x55555555 (antipattern), 칸 위 숫자 = word offset (hex)</text>
</svg>
```

**device (address-in-address + 반전).** "address-in-address" 는 각 word 에 **자기 주소(여기서는 offset+1)** 를 쓰는 패턴이다. 모든 칸 값이 달라서 alias 가 있으면 남의 주소가 읽힌다. 세 패스:

| 패스 | 하는 일 | 잡는 것 |
|---|---|---|
| 1 | 0..n-1 에 `offset+1` 쓰기 | — |
| 2 | 읽어서 `offset+1` 확인 → 그 자리에 `~(offset+1)` | alias (뒤에서 쓴 값이 앞에 보임), 패턴에서 1 인 비트의 SA0 / 0 인 비트의 SA1 |
| 3 | 읽어서 `~(offset+1)` 확인 | 나머지 셀 비트 (반전으로 모든 비트가 0 과 1 을 다 겪음) |

예: dead cell SA0 word 0x6B5 bit 17. 패턴 `0x6B6` 의 비트 17 은 0 이라 패스 2 는 통과하고, 패스 3 에서 `exp=0xFFFFF949 act=0xFFFDF949 diff=bits[17]` 로 걸린다 (실제 출력). 반전 패스가 없으면 이 셀은 영영 안 보인다.

### 접근 횟수 (이 레슨 하네스가 `nops` 로 확인)

| 테스트 | 접근 수 | 4096 word (L=12) | 2 GiB · 64-bit word (L=28) |
|---|---|---|---|
| data bus | 2 × 데이터 폭 | 64 | 128 |
| address bus | L 쓰기 + (1 + L + 1) + L·(1 + 1 + L + 1) = **L² + 5L + 2** | 206 | 926 |
| device | 쓰기 n + (읽기 + 쓰기) n + 읽기 n = **4n** | 16 384 | 1 073 741 824 (8 GiB 이동) |

앞 둘은 사실상 공짜, 시간은 전부 device 다. 그래서 부트 시 POST 는 data/address 만 하고, device·March 는 제조 테스트나 진단 모드에서 돌리는 구성이 흔하다.

### Part C — 탐지 매트릭스 (실제 출력)

```text
$ cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/MEM-B08_so && /tmp/MEM-B08_so
Detection matrix: 32-bit x 4096 words, every instance of each fault (detected/instances)
fault                inst   data-bus@0  address-bus       device   bring-up stage 1/2/3   diagnosis ok
none                    1          0/1          0/1          0/1       0/    0/    0       1/1
data SA0               32        32/32        16/32        32/32      32/    0/    0       32/32
data SA1               32        32/32        16/32        32/32      32/    0/    0       32/32
data short AND        496      496/496      256/496      325/496     496/    0/    0       496/496
data short OR         496      496/496      256/496      325/496     496/    0/    0       496/496
addr SA0 (low)         12         0/12        12/12        12/12       0/   12/    0       12/12
addr SA1 (high)        12         0/12        12/12        12/12       0/   12/    0       12/12
addr short AND         66         0/66        66/66        66/66       0/   66/    0       66/66
addr short OR          66         0/66        66/66        66/66       0/   66/    0       66/66
addr open (hold)       12         0/12        12/12        12/12       0/   12/    0       12/12
cell SA0             4096       1/4096      12/4096    4096/4096       1/   11/ 4084       4096/4096
cell SA1             4096       1/4096       1/4096    4096/4096       1/    1/ 4094       4096/4096
```

(dead cell 인스턴스는 모든 word × 비트 `(word·7+3) mod 32` 하나씩. 하네스는 이 숫자를 `EXPECT` 표로 단정한다. 전체 실행 0.44 s user.)

**놓친 칸마다 이유:**

| 칸 | 놓친 수 | 왜 |
|---|---|---|
| data-bus × 주소 결함 전부 | 12/12/66/66/12 모두 0 | 같은 주소에 쓰고 바로 읽는다. 주소가 어디로 바뀌든 **같은 칸**에 쓰고 읽으니 일관됨 |
| data-bus × cell | 4095/4096 | word 0 하나만 건드린다. word 0 의 셀만 걸린다 |
| address-bus × data SA | 16/32 놓침 | 읽어서 비교하는 값은 늘 `0xAAAAAAAA` 뿐(antipattern 은 "새는지" 보는 탐침이라 되읽지 않음). 짝수 비트 SA0 · 홀수 비트 SA1 은 그 값과 같아서 안 보임 |
| address-bus × data short | 240/496 놓침 | AA/55 에서 홀짝이 같은 두 비트는 항상 같은 값. 홀짝이 다른 쌍 16×16 = 256 만 잡음 (SIV-B05 의 55/AA 분석과 같은 이유) |
| address-bus × cell | SA0 12, SA1 1 만 잡음 | 13곳만 건드리고, 그 중 고장 비트가 AA 와 반대인 경우만. 이 레슨 비트 배치에서 word 0·2·4·8·16·32..2048 은 홀수 비트(SA0 이 보임), word 1 만 짝수 비트 10(SA1 이 보임) |
| device × data short | 171/496 놓침 | 증가 패턴 1..4096 은 비트 13..31 이 늘 0, 반전하면 늘 1 → 그 비트들끼리는 값이 다른 순간이 없다. C(19,2) = 171 쌍. 연습 2 의 곱셈 해시 패턴이면 0 |

주소 결함은 address-bus 와 device 가 모두 100 % 잡는다. 저항성 open 도 잡히지만, alias 탐침만으로는 **엉뚱한 선을 가리킨다** — 직전 접근에 따라 칸이 바뀌기 때문이다. 판정 함수를 처음 짤 때 open A0 이 `lines=0x006`, open A11 이 `CELL` 로 나왔다 (개발 중 실제 출력). 그래서 history 탐침을 먼저 넣었다:

| 단계 | 직전 접근 | 0 번지 읽기가 가는 물리 칸 (open A_k) | 값 |
|---|---|---|---|
| 준비 | 0 → 0 에 P, 2^k → 2^k 에 ~P | 0, 2^k | |
| v1 | 0 | 0 | P |
| v2 | 2^k | 2^k | ~P → v1 ≠ v2 → 선 k |

정상 선·SA·short 는 0 번지 읽기가 직전 접근과 무관하므로 v1 == v2 다. 실제 보드의 저항성 open 은 온도·속도에 따라 간헐적이라 이 탐침이 늘 깔끔하진 않다 — 모델이 보여 주는 건 "**순서 의존 실패**가 open 의 지문" 이라는 점이다.

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">bring-up 순서와 판정 (bringup_run)</text>
  <rect x="20" y="40" width="300" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="34" y="62" fill="currentColor">① data bus @0 — walking 1</text>
  <text x="34" y="82" fill="currentColor" font-size="12">64 접근 · 데이터 선 32개</text>
  <rect x="20" y="136" width="300" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="34" y="158" fill="currentColor">② address bus — 2^k 오프셋</text>
  <text x="34" y="178" fill="currentColor" font-size="12">L²+5L+2 = 206 접근 · 주소 선 12개</text>
  <rect x="20" y="232" width="300" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="34" y="254" fill="currentColor">③ device — address-in-address + 반전</text>
  <text x="34" y="274" fill="currentColor" font-size="12">4n = 16 384 접근 · 모든 셀</text>
  <rect x="20" y="320" width="300" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="34" y="347" fill="currentColor">OK → March (MEM-B09), ECC (MEM-B07)</text>
  <g stroke="currentColor" fill="none"><path d="M170 96 L170 136"/><path d="M170 192 L170 232"/><path d="M170 288 L170 320"/><path d="M320 68 L380 68"/><path d="M320 164 L380 164"/><path d="M320 260 L380 260"/></g>
  <g fill="currentColor" font-size="12"><text x="178" y="120">PASS</text><text x="178" y="216">PASS</text><text x="178" y="308">PASS</text><text x="330" y="62">FAIL</text><text x="330" y="158">FAIL</text><text x="330" y="254">FAIL</text></g>
  <rect x="380" y="40" width="350" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="392" y="62" fill="currentColor" font-size="12">diff OR 를 @0 과 @n−1 두 곳에서 → 교집합</text>
  <text x="392" y="82" fill="currentColor" font-size="12">≠ 0: DATA_LINE (선 번호) · = 0: CELL</text>
  <rect x="380" y="120" width="350" height="88" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="392" y="142" fill="currentColor" font-size="12">history 탐침 ≠ 0: ADDR_OPEN (순서 의존)</text>
  <text x="392" y="162" fill="currentColor" font-size="12">아니면 alias 탐침 (0↔2^k, 2^j↔2^k, == ~P)</text>
  <text x="392" y="182" fill="currentColor" font-size="12">≠ 0: ADDR_LINE (선 마스크)</text>
  <text x="392" y="200" fill="currentColor" font-size="12">= 0: CELL (그 자리 셀이 AA 와 반대로 죽음)</text>
  <rect x="380" y="232" width="350" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="392" y="254" fill="currentColor" font-size="12">CELL: addr + diff 비트 → 배드 페이지 /</text>
  <text x="392" y="274" fill="currentColor" font-size="12">칩 위치로 (MEM-A06 page retire)</text>
</svg>
```

판정 예 (실제 출력, 리포트 부분은 줄임):

```text
bring-up D3-D12 OR : stage=1 verdict=DATA_LINE lines=0x00001008  FAIL addr=0x000 exp=0x00000008 act=0x00001008 diff=0x00001000 bits=[12] phase=data-bus
bring-up A1-A10 AND: stage=2 verdict=ADDR_LINE lines=0x402       FAIL addr=0x002 exp=0xAAAAAAAA act=0x55555555 diff=0xFFFFFFFF bits=[31-0] phase=addr-stuck-high
bring-up A6 open   : stage=2 verdict=ADDR_OPEN lines=0x040       FAIL addr=0x040 exp=0xAAAAAAAA act=0x55555555 diff=0xFFFFFFFF bits=[31-0] phase=addr-stuck-high
bring-up cell@0 b0 : stage=1 verdict=CELL lines=0x00000000  FAIL addr=0x000 exp=0x00000002 act=0x00000003 diff=0x00000001 bits=[0] phase=data-bus
54/54 passed
```

D3–D12 OR 의 첫 실패 diff 는 비트 12 하나뿐이다. **첫 실패에서 멈추면 선 하나만 보인다** — walking 1 을 끝까지 돌며 diff 를 모으면 패턴 0x8 에서 비트 12, 패턴 0x1000 에서 비트 3 이 튀어 `0x1008` 두 선이 다 나온다. 마지막 줄은 data bus 테스트가 걸렸지만 word 0 의 dead cell 이다: 마지막 번지에서는 깨끗하므로 교집합 0 → `CELL`. 이 두 번째 주소 확인이 없으면 "D0 선 고장" 으로 보드를 잘못 수리한다.

### 실제 DDR 보드로 옮길 때

| 모델의 가정 | 실제 DDR 시스템 | 대처 |
|---|---|---|
| 접근이 곧 RAM 접근 | D-cache 가 있으면 **캐시를 테스트**한다 | 캐시 끄기 / uncached 매핑 / 쓰기 후 clean+invalidate |
| 주소 선 = CPU 주소 비트 | 컨트롤러가 PA 를 bank/row/col 로 매핑 (MEM-B01), 핀은 row·col 을 **같은 A 핀으로 다중화** | 2^k 오프셋은 PA 비트 하나씩 → 매핑 표로 핀을 역추적. 핀 A3 고장은 row 비트와 col 비트 둘 다 엉킨다 |
| 32-bit word 단위 | 64-bit DQ + byte lane(DQS/DM), 접근은 BL8 burst | 64-bit 접근으로 walking, byte lane 별 실패를 따로 집계 |
| 결함이 고정 | 타이밍 마진·training(MEM-A04)·온도에 따라 간헐 | 전압/온도/속도 바꿔 반복 (shmoo) |
| ECC 없음 | ECC 가 단일 비트를 **고쳐서 숨긴다** (MEM-B07) | bring-up 은 ECC 끄고, 또는 CE 카운터(EDAC)를 같이 본다 (SIV-M14) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/MEM-B08_so && /tmp/MEM-B08_so      # 매트릭스 + "54/54 passed"
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/MEM-B08_as && /tmp/MEM-B08_as   # 깨끗, 54/54
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 실제 보드에서 `uint32_t *p` 로 쓰고 읽기 | 컴파일러가 "방금 쓴 값" 으로 읽기를 없앤다 → 테스트가 메모리를 안 본다 | `volatile uint32_t *p` (C-M01) |
| `int pattern = 1; pattern <<= 1` | 비트 31 에서 부호 있는 시프트 = UB, 루프가 안 끝날 수 있음 | `uint32_t pattern`, 조건 `pattern != 0` |
| D-cache 켠 채 테스트 | 쓰고 읽는 게 전부 캐시에서 끝난다. 주소 alias 가 있어도 PASS | 캐시 off / uncached 영역 / clean+invalidate 후 읽기 |
| 크기가 2의 거듭제곱이 아닌데 `(off & mask) != 0` | 3072 word 면 mask 0xBFF, 1024 & mask = 0 → A10·A11 미검사 (연습 4) | `off < nwords` |
| device 를 "채우고-확인, 반전 채우고-확인" 분리 패스로 | 데이터 폭보다 주소가 길면 패턴이 반복 → 반복 주기만큼 떨어진 alias 를 놓친다 (연습 3) | 확인한 **그 자리에서** 반전 |
| address 테스트를 data 테스트보다 먼저 | 데이터 선 결함이 주소 실패처럼 보인다 (data SA 16/32 가 address 테스트에 걸림) | 순서 고정: data → address → device |
| 첫 실패에서 diff 만 기록 | short 는 한 선만 보이고, SA1 은 "처음 틀린 패턴" 이 선 번호와 다르다 | walking 을 끝까지 돌며 diff OR, 두 주소에서 교집합 |
| alias 판정을 `read != P` 로 | 셀 결함(비트 하나)도 alias 로 보여 "모든 주소 선 의심" 이 나온다 | `read == ~P` (다른 주소에 쓴 값이 **그대로**) |
| ECC 켠 채 bring-up | 단일 비트 셀·선 결함이 CE 로 고쳐져 PASS. 2비트면 UE 로 시스템이 멈춤 | ECC off 로 먼저, 또는 EDAC CE 카운터 확인 |

## 8. 면접 Q&A

<details><summary>Q1. 메모리 테스트를 왜 data bus → address bus → device 순서로 하나요?</summary>

각 테스트가 앞 층이 정상이라고 가정하기 때문이다. address 테스트는 AA/55 값으로 "새는지" 보는데, 데이터 선이 고장이면 값 자체가 틀려 **주소 고장처럼** 보인다 (매트릭스에서 data SA 의 절반이 address 테스트에 걸린다). device 테스트는 무엇이든 잡지만 실패 원인을 말해 주지 않는다. 비용도 64 → L²+5L+2 → 4n 으로 커진다. 싼 것부터, 원인이 분명한 것부터.

꼬리질문: "device 하나만 돌리면 되지 않나?" → 탐지율은 비슷하지만 진단이 안 된다. 리포트가 "0x6B5 에서 틀림" 이면 셀인지 선인지 모르고, 보드 수리 엔지니어에겐 "A3 선" 이 필요하다.

</details>

<details><summary>Q2. address bus 테스트에서 왜 2의 거듭제곱 오프셋만 쓰나요?</summary>

오프셋 2^k 는 0 번지와 **주소 비트 k 하나만** 다르다. 주소 선 k 가 고장이면 (stuck 이든 short 든) 0 과 2^k, 혹은 2^j 와 2^k 가 같은 칸이 된다. 선 L 개를 L+1 곳으로 시험하므로 메모리 크기와 상관없이 O(L²) 접근이다 (2 GiB 라도 926 접근). 임의 주소를 쓰면 한 번에 여러 선이 바뀌어 어느 선인지 모른다.

꼬리질문: "DDR 에서 2^k 가 정말 핀 하나에 대응하나?" → 아니다. 컨트롤러가 PA 를 bank/row/col 로 매핑하고(MEM-B01), XOR 해싱이 있으면 PA 비트 하나가 bank 비트 여러 개를 바꾼다. 그래도 각 PA 비트를 한 번씩 토글하는 건 같으니, 매핑 표로 실패 PA 비트 → 핀을 역추적한다.

</details>

<details><summary>Q3. antipattern 을 0 번지에 한 번, 각 2^k 에 한 번 — 왜 두 방향이 다 필요한가요?</summary>

①(0 에 55)은 **0 과 엉키는** 결함을 잡는다: SA1(0 → 2^k), SA0(2^k → 0), AND short(2^j, 2^k → 0). ②(2^t 에 55)는 **0 을 거치지 않고 2^j 와 2^k 가 엉키는** 결함을 잡는다: OR short(2^j, 2^k → 2^j+2^k). 이 레슨 모델에서 ② 를 빼면 OR short 66개를 모두 놓친다. Barr 의 주석은 ① 을 "stuck high", ② 를 "stuck low or shorted" 라고 부르지만, 실제로는 SA0 도 ① 에서 먼저 걸린다 (실제 출력: SA0 A3 → `phase=addr-stuck-high`).

꼬리질문: "SA0 A3 과 SA1 A3 을 구분할 수 있나?" → CPU 쪽 읽기·쓰기만으로는 안 된다. 둘 다 i 와 i⊕8 이 같은 칸이고 메모리 절반만 쓰일 뿐이다. 선 번호는 확실하고, 방향은 오실로스코프나 boundary scan 으로 본다.

</details>

<details><summary>Q4. device 테스트에서 왜 증가 패턴을 쓰고, 왜 확인하면서 바로 반전하나요?</summary>

증가 패턴(address-in-address)은 칸마다 값이 달라서 alias 가 있으면 **남의 값**이 읽힌다. 반전은 모든 셀 비트가 0 과 1 을 다 겪게 한다 — 패턴 0x6B6 에서 0 인 비트 17 의 SA0 은 반전 패스에서만 걸렸다. "그 자리에서" 반전하는 이유는 데이터 폭이 주소보다 짧을 때다: 8-bit 메모리에서 패턴은 256 마다 반복되어, 분리 패스면 A8..A11 alias 를 놓치지만 (연습 3: 24개 중 8개 놓침), 확인-반전을 섞으면 먼저 확인한 쪽의 반전값이 alias 짝에 보여 24/24 다 잡는다.

</details>

<details><summary>Q5. 이 세 테스트가 PASS 하면 메모리가 정상인가요? 무엇을 놓치나요?</summary>

놓치는 것: ① **coupling / pattern-sensitive** 결함 — 이웃 셀 전이가 피해 셀을 뒤집는 것. March(MEM-B09)가 몫. ② **retention** — 약한 셀이 refresh 주기 안에 전하를 잃는 것. 쓰고 기다렸다 읽기, 고온에서 (MEM-B05). ③ **타이밍 마진** — training 이 아슬아슬하면 특정 패턴·온도에서만 틀림 (MEM-A04). 랜덤 패턴과 전압/온도 스윕. ④ **Row Hammer** (MEM-A02). ⑤ 측정 방법의 맹점 — 캐시가 켜져 있거나 ECC 가 고쳐 주면 결함이 숨는다. ⑥ 이 레슨 매트릭스가 보인 패턴 맹점 — device 의 증가 패턴은 상위 데이터 비트 short 171쌍을 놓친다 (data bus 테스트가 먼저 잡으므로 순서가 중요).

</details>

<details><summary>Q6. 실패 로그에 무엇을 남기나요? 선 결함과 셀 결함은 어떻게 가르나요?</summary>

`addr, exp, act, diff, bits, phase, trig` + 보드 ID·온도·전압·주파수. 선 결함은 **어느 주소에서나 같은 비트**가 틀린다 → 두 주소 이상에서 diff 교집합. 셀은 한 주소만. 주소 alias 는 "다른 주소에 쓴 값이 **그대로**" 읽힌다 (diff 가 모든 비트, `act == antipattern`). 셀은 비트 한두 개만 틀린다. 순서(직전 접근)에 따라 결과가 바뀌면 저항성 open 이나 타이밍 문제를 의심한다.

꼬리질문: "DDR 에서 비트 번호로 칩을 찾을 수 있나?" → 64-bit DQ 를 x8 칩 8개가 나눠 가지므로 비트 / 8 = byte lane = 칩 위치. 같은 lane 에서만 실패가 모이면 그 칩이나 그 lane 의 DQS 를 본다.

</details>

<details><summary>Q7. 8 GB DDR 보드에서 부팅마다 메모리 테스트를 할 수 있나요?</summary>

device 테스트만 4n 접근 = 8 GB 기준 32 GB 이동. 실효 수 GB/s 라면 수 초~십수 초가 걸린다 (대략치 — 보드의 실측 대역폭에 따라 다름). 그래서 흔한 구성은: 부트마다 data/address 테스트(수백 접근, 무시할 만함) + 작은 영역 device, 제조·진단 모드에서 전체 March, 운영 중에는 ECC patrol scrub(MEM-A06)로 대신한다. Linux 사용자 공간이면 `memtester`, PC 면 MemTest86 계열, 부트로더면 U-Boot `mtest`.

</details>

<details><summary>Q8. data bus 테스트를 한 주소에서만 해도 되는 이유는? walking 0 은 왜 Barr 원형에 없나요?</summary>

데이터 선은 모든 주소가 공유하므로 한 주소면 충분하다 (단, 그 주소의 셀이 죽어 있으면 선 결함처럼 보이므로 두 번째 주소로 교차 확인 — 이 레슨의 판정 함수). walking 1 하나로도 32개 선의 SA0/SA1 과 모든 쌍 short 를 **검출**한다 (매트릭스 32/32, 496/496). walking 0 은 **진단**(AND-short 와 이중 SA0 구분)과 동시 스위칭 스트레스를 더한다 — SIV-B05 에서 다뤘다.

</details>

## 9. 추가 연습

### 연습 1 — 접근 횟수 공식과 실제 크기

data bus 2·w, address bus L²+5L+2, device 4n 을 모델의 `nops` 카운터로 n = 16, 256, 4096 에서 확인하고, 2 GiB · 64-bit word 시스템에서의 숫자를 구하라.

<details><summary>답 보기</summary>

address: 쓰기 L, ① 쓰기 1 + 읽기 L + 쓰기 1, ② L × (쓰기 1 + 읽기 1 + 읽기 L + 쓰기 1) = L(L+3). 합 L² + 5L + 2. practice.c 실제 출력:

```text
2 GiB / 64-bit word: L=28  data=128  address=926  device=1073741824 accesses (8 GiB moved)
```

주소 테스트는 크기에 거의 무관하고, 시간은 device 가 다 쓴다.

</details>

### 연습 2 — device 테스트의 패턴 맹점 고치기

증가 패턴 1..4096 은 비트 13..31 이 늘 같은 값이라 그 비트 쌍의 short 를 놓친다. 데이터 선 short 992개(496쌍 × AND/OR) 중 몇 개를 놓치는지 세고, `(offset+1) × 0x9E3779B1` 패턴으로 바꾸면 어떻게 되는지, alias 탐지는 유지되는지 확인하라.

<details><summary>답 보기</summary>

```text
device test, data-line shorts missed of 992: increment=342  hash=0 ; addr SA caught of 24: 24 / 24
```

342 = 2 × C(19,2). 홀수 곱은 2^32 에서 전단사라 주소마다 값이 여전히 다르고(alias 탐지 유지), 상위 비트까지 섞여 모든 쌍이 어느 주소에선 다른 값이 된다. 다만 실무에서는 data bus 테스트가 short 를 먼저 잡으므로, 이 개선은 "device 만 돌리는" 상황(예: 운영 중 진단)에서 의미가 있다.

</details>

### 연습 3 — 8-bit 메모리와 "그 자리 반전"

8-bit × 4096 메모리에서 증가 패턴은 256 마다 반복된다. 주소 선 SA0/SA1 24개에 대해 Barr 의 확인-반전 방식과 "채우고-확인, 반전 채우고-확인" 분리 방식을 비교하라.

<details><summary>답 보기</summary>

```text
8-bit RAM, addr SA caught of 24: in-place invert=24  split passes=16 (missed A8..A11: 8)
```

A8 이상이 고장이면 alias 짝 i, i+256 의 패턴이 mod 256 으로 같다. 분리 방식은 두 패스 모두 같은 값을 읽어 통과. 그 자리 반전 방식은 앞쪽 주소를 확인하며 `~pattern` 으로 바꿔 두므로, 뒤쪽 짝이 그 반전값을 읽고 걸린다.

</details>

### 연습 4 — 2의 거듭제곱이 아닌 크기

3 MB 처럼 3072 word 영역에서 Barr 원형의 `(offset & addressMask) != 0` 루프는 어떤 주소 선을 건너뛰나? `offset < nWords` 로 고치면?

<details><summary>답 보기</summary>

```text
3072-word RAM, addr SA caught of 24: (off & mask) loop=20  (off < n) loop=24  missed lines mask=0xC00
```

mask = 3071 = 0xBFF, 비트 10 이 0 이라 offset 1024 에서 루프가 멈춘다 → A10·A11 은 한 번도 토글되지 않는다. 원형은 크기가 2의 거듭제곱이라고 가정한다. 칩 두 개를 이어 붙인 비대칭 구성에서 그대로 쓰면 상위 선 고장을 놓친다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/MEM-B08_pr && /tmp/MEM-B08_pr   # 위 4줄 + "17/17 passed"
```

## 10. 핵심 정리

- 층별 테스트: **데이터 선 → walking 1 한 주소 (64 접근)**, **주소 선 → 2^k 오프셋 (L²+5L+2)**, **셀 → address-in-address + 반전 (4n)**. 순서를 지킨다.
- 2^k 오프셋은 0 과 주소 비트 하나만 다르다. 0 에 antipattern(①)은 0 과 엉키는 SA0/SA1/AND short, 2^t 에 antipattern(②)은 2^j–2^k OR short 를 잡는다.
- 주소 SA0 과 SA1 은 CPU 쪽에서 구분이 안 된다 (둘 다 i ↔ i⊕2^k). 선 번호만 확실하다.
- data bus 테스트는 주소 결함을 하나도 못 본다 (같은 칸에 쓰고 읽으니까). address 테스트는 AA 만 되읽어 데이터 SA 절반, 홀짝 같은 short 를 놓친다. device 의 증가 패턴은 상위 비트 short 171쌍을 놓친다.
- device 는 **확인한 그 자리에서 반전**해야 패턴이 반복되는 좁은 폭에서도 alias 를 잡는다.
- 판정: 선 결함 = 두 주소에서 같은 비트, alias = 다른 주소의 값이 **그대로**, 저항성 open = 직전 접근에 따라 값이 바뀜.
- 실제 DDR: 캐시 끄기/uncached, 컨트롤러 주소 매핑, byte lane, ECC 가 결함을 숨김. PASS 후에도 coupling·retention·마진은 March·스트레스로.

## 참고자료

- Michael Barr, "Software-Based Memory Testing", *Embedded Systems Programming* (2000) — data bus / address bus / device 테스트의 원 글. 같은 내용과 `memtest.c` 가 Barr Group 의 "Fast Accurate Memory Test Code in C" 페이지에 있다: <https://barrgroup.com/embedded-systems/how-to/memory-test-suite-c> (게재 호·연도는 원문 확인 권장)
- A. J. van de Goor, *Testing Semiconductor Memories: Theory and Practice* (Wiley, 1991) — stuck-at / bridging / address decoder fault 모델 (MEM-B09 March 와 같이)
- U-Boot `cmd/mem.c` — `mtest` 명령 (data line / address line / 전체 패턴 구조): <https://source.denx.de/u-boot/u-boot>
- memtester (Linux 사용자 공간 메모리 테스트): <https://pyropus.ca/software/memtester/>
- Linux EDAC 문서 — CE/UE 카운터: <https://docs.kernel.org/driver-api/edac.html>
- 관련 레슨: SIV-B05 (레지스터 walking 1/0, bus-hold decoy), SIV-B06 (주소 디코드 aliasing), SIV-B07 (SRAM March C-), SIV-M14 (ECC 오류 주입·CE/UE), C-B03 (시프트), C-M01 (volatile), MEM-B01 (주소 매핑), MEM-B07 (SECDED), MEM-B09 (March), MEM-A04 (training), MEM-A06 (scrub·page retire), MEM-A10 (bring-up 계획)
