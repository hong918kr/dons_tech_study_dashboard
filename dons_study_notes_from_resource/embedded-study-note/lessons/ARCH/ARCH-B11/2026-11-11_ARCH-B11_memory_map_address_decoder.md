---
id: ARCH-B11
time: 70분
updated: 2026-11-11
---
# ARCH-B11 메모리 맵과 주소 디코더 모델

## 학습 목표

- STM32 스타일 메모리 맵(Flash 0x0800_0000 · SRAM 0x2000_0000 · 주변장치 0x4000_0000 영역)을 **표(table)** 로 표현하고, 주소 하나를 받아 그 영역의 디바이스 read/write 콜백으로 보내는 **주소 디코더(address decoder)** 를 짠다.
- 버스 접근 하나를 "디코드 → 폭 → 정렬 → 영역 끝 걸침 → 읽기 전용 → 클럭 게이트 → 디바이스 응답" 순서로 검사하고, 실패하면 (주소, 폭, R/W, 이유) 를 담은 **버스 폴트 보고** 를 낸다. 왜 이 순서인지 말할 수 있다.
- `base + size` 가 정확히 2^32 인 영역, 4바이트 접근이 영역 끝에 걸치는 경우, 0x0 부트 alias, 겹치는 맵 같은 **경계 함정** 을 64비트 산술로 피한다.
- 모델의 폴트를 실제 Cortex-M 폴트(BusFault PRECISERR/IMPRECISERR + BFAR, UsageFault UNALIGNED, HardFault 승격)와 AHB/AXI 응답(HRESP ERROR, SLVERR/DECERR)에 대응시킨다.
- 쓰기 버퍼 때문에 폴트가 **나중 명령에서** 터지는 imprecise 폴트와, "클럭을 안 켜서 레지스터가 0 으로만 읽히는" 조용한 실패를 재현하고 디버그 방법을 설명한다.

## 1. 문제

"0x4000_4400 에 쓰면 실제로 무슨 일이 일어나나요?", "HardFault 가 났는데 BFAR 가 비어 있다 — 왜?", "UART 레지스터에 써도 값이 안 바뀐다 — 뭘 먼저 보나?" — 펌웨어·실리콘 검증 면접에서 메모리 맵은 **모든 디버그의 출발점** 이고, 디코더와 폴트 규칙을 정확히 아는 사람이 HardFault 를 몇 분 만에 잡는다.

CPU 가 내보낸 32비트 주소는 버스 매트릭스(interconnect)의 **디코더** 가 보고 "어느 슬레이브(Flash 컨트롤러, SRAM, APB 브리지 → UART …)인가" 를 고른다. 아무 슬레이브도 해당하지 않으면 **기본 슬레이브(default slave)** 가 에러 응답을 돌려주고, CPU 는 그것을 BusFault 로 바꾼다. 이 레슨은 그 길을 C 로 모델링한다.

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b11-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">STM32 스타일 메모리 맵 (이 모델) — 주소 → 디코더 → 디바이스 콜백</text>
  <rect x="20" y="40" width="200" height="340" fill="none" stroke="currentColor"/>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="228" y="44">0xFFFF_FFFF</text>
    <text x="228" y="80">0xE000_0000</text>
    <text x="228" y="168">0x4000_0000</text>
    <text x="228" y="258">0x2000_0000</text>
    <text x="228" y="318">0x0800_0000</text>
    <text x="228" y="380">0x0000_0000</text>
  </g>
  <rect x="21" y="66" width="198" height="20" class="f-mut"/>
  <text x="30" y="81" fill="currentColor" font-size="12">PPB (NVIC/SCB, 모델엔 없음)</text>
  <rect x="21" y="120" width="198" height="18" class="f-acc" stroke="currentColor"/>
  <text x="30" y="134" fill="currentColor" font-size="12">GPIOA 0x4002_0000 (1 KB)</text>
  <rect x="21" y="146" width="198" height="18" class="f-acc" stroke="currentColor"/>
  <text x="30" y="160" fill="currentColor" font-size="12">USART2 0x4000_4400 (1 KB)</text>
  <rect x="21" y="236" width="198" height="24" class="f-ok" stroke="currentColor"/>
  <text x="30" y="253" fill="currentColor" font-size="12">SRAM 16 KB (rw)</text>
  <rect x="21" y="298" width="198" height="24" class="f-ok" stroke="currentColor"/>
  <text x="30" y="315" fill="currentColor" font-size="12">FLASH 64 KB (ro)</text>
  <rect x="21" y="358" width="198" height="21" class="f-warn" stroke="currentColor"/>
  <text x="30" y="373" fill="currentColor" font-size="12">BOOT alias → FLASH</text>
  <text x="30" y="205" fill="currentColor" font-size="12" opacity=".7">나머지 = 미매핑 → 폴트</text>
  <rect x="350" y="150" width="150" height="110" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="425" y="175" fill="currentColor" text-anchor="middle" font-weight="bold">디코더</text>
  <text x="425" y="197" fill="currentColor" text-anchor="middle" font-size="12">map_find(addr)</text>
  <text x="425" y="217" fill="currentColor" text-anchor="middle" font-size="12">bus_check(…)</text>
  <text x="425" y="237" fill="currentColor" text-anchor="middle" font-size="12">off = addr−base+ctx_off</text>
  <path d="M330 205 H348" stroke="currentColor" marker-end="url(#b11-arr)"/>
  <text x="318" y="140" fill="currentColor" font-size="12" font-family="monospace">addr, size, R/W</text>
  <g font-size="12" fill="currentColor">
    <rect x="560" y="60" width="170" height="34" rx="6" class="f-ok" stroke="currentColor"/>
    <text x="570" y="82">mem_rd/wr (Flash·SRAM)</text>
    <rect x="560" y="110" width="170" height="34" rx="6" class="f-acc" stroke="currentColor"/>
    <text x="570" y="132">uart_rd / uart_wr</text>
    <rect x="560" y="160" width="170" height="34" rx="6" class="f-acc" stroke="currentColor"/>
    <text x="570" y="182">gpio_rd / gpio_wr</text>
    <rect x="560" y="240" width="170" height="54" rx="6" class="f-warn" stroke="currentColor"/>
    <text x="570" y="262">fault_record()</text>
    <text x="570" y="282">addr · size · R/W · 이유</text>
  </g>
  <path d="M500 175 L558 80" stroke="currentColor" fill="none" marker-end="url(#b11-arr)"/>
  <path d="M500 185 L558 127" stroke="currentColor" fill="none" marker-end="url(#b11-arr)"/>
  <path d="M500 195 L558 177" stroke="currentColor" fill="none" marker-end="url(#b11-arr)"/>
  <path d="M500 240 L558 262" stroke="currentColor" fill="none" stroke-dasharray="4 3" marker-end="url(#b11-arr)"/>
  <text x="350" y="300" fill="currentColor" font-size="12">미매핑 / 폭 / 정렬 / 걸침 / RO / 게이트 / 디바이스 거부</text>
  <text x="350" y="320" fill="currentColor" font-size="12">→ 콜백을 부르지 않고 폴트 기록 (모든 접근이 같은 길)</text>
  <text x="350" y="350" fill="currentColor" font-size="12" opacity=".8">세로축은 비율이 아니다 (영역 위치 순서만 맞춤)</text>
</svg>
```

모델 맵 (주소는 STM32F4 에서 빌렸고 크기·정책은 단순화):

| 영역 | base | size | 폭 | 정렬 필수 | R/W | 클럭 꺼짐 정책 |
|---|---|---|---|---|---|---|
| BOOT (alias) | 0x0000_0000 | 64 KB | 1/2/4 | 아니오 | RO | 없음 |
| FLASH | 0x0800_0000 | 64 KB | 1/2/4 | 아니오 | RO | 없음 |
| SRAM | 0x2000_0000 | 16 KB | 1/2/4 | 아니오 | RW | 없음 |
| USART2 | 0x4000_4400 | 1 KB | 2/4 | 예 | RW | RAZ/WI (또는 FAULT) |
| GPIOA | 0x4002_0000 | 1 KB | 1/2/4 | 예 | RW | RAZ/WI |

이미 아는 것 (다시 가르치지 않는다): `volatile` 레지스터 구조체와 MMIO (C-M01), RMW/W1C (C-M02), 레지스터 리셋값 검사 (SIV-B03), 디코더 **결함** 을 찾는 aliasing 테스트 (SIV-B06), 버스 대기 상태·처리량 (ARCH-B12), 리셋 원인 (ARCH-B14). 여기서는 **정상 디코더가 무엇을 하고 언제 폴트를 내는가** 만 다룬다.

### Part A — 맵 테이블과 디코더, 디바이스 콜백

- `mem_rd / mem_wr` — Flash·SRAM 배열 디바이스. 1/2/4 바이트를 **리틀 엔디언** 으로 모으고 흩는다. `off + size` 가 배열을 넘으면 slave error(1).
- `map_find(bus, addr)` — addr 를 포함하는 영역 인덱스, 없으면 -1.
- `bus_check(bus, addr, size, write, &idx)` — 디코드 + 정책 검사 (순서는 §2). 주어진 `bus_read / bus_write` 가 이것을 부르고, OK 면 `off = addr − base + ctx_off` 로 콜백을 부른다.
- 테스트: 영역 경계 디코드, Flash 벡터 테이블 읽기, SRAM 바이트/하프워드 레인, GPIO `BSRR → ODR`, UART `BRR` 쓰기, 미매핑·RO·폭 위반.

### Part B — 경계와 함정

- `map_validate(map, n, &bad)` — 0 크기 / `base+size > 2^32` / 두 영역 겹침을 찾는다 (정렬 안 된 표도).
- SRAM 비정렬 4바이트 OK vs 영역 끝 걸침 F_CROSS, 주변장치 비정렬 F_UNALIGNED, 크기 3 → F_SIZE, `0xFFFF_F000 + 0x1000` 영역에서 `0xFFFF_FFFE` 4바이트가 0 으로 감기지 않고 F_CROSS.
- 0x0 부트 alias 로 읽은 리셋 벡터 == Flash 에서 읽은 값, alias 에 쓰기는 RO. 설정을 잘못한 alias(뒤 메모리보다 큰 창)는 디코더는 통과하고 **디바이스가 거부(SLVERR)**.
- **독립 참조 모델** (64비트 + 바이트 단위 판정) 과 비교: 모든 영역 경계 ±16 × 폭 0..5 × R/W, 그리고 무작위 20만 접근. 불일치 0.

### Part C — 펌웨어 현실: 폴트 기록, 쓰기 버퍼, UART 드라이버

- `fault_record` — 폴트 수와 마지막 폴트를 저장하고, **precise 폴트일 때만** BFAR 비슷한 `bfar` 를 잡는다 (지우기 전까지 첫 주소 유지).
- `bus_write_posted / bus_drain` — 깊이 1 쓰기 버퍼. posted write 는 바로 OK 를 돌려주고, 실제 버스 쓰기는 다음 접근 직전에 나간다 → 폴트가 **imprecise** 로, 엉뚱한 명령에서 보고된다.
- `uart_putc(bus, base, c, max_polls)` — SR.TXE 를 최대 `max_polls` 번 폴링하고 DR 에 쓴다. 0 / -1(버스 폴트) / -2(타임아웃). USART2 클럭을 안 켰을 때 RAZ/WI 정책이면 **폴트 없이 타임아웃**, FAULT 정책이면 **GATED 폴트** 가 나야 한다.

## 2. Problem (English)

> **Memory map, address decoder and bus faults (MCU model).**
>
> A bus is a table of regions `{name, base, size, allowed widths (1/2/4), must_align, writable, clock-gate policy, read/write callbacks, ctx, ctx_off}`. A region covers `[base, base+size)` and may end exactly at 2^32.
>
> 1. Implement `int map_find(const bus_t *b, uint32_t addr)` and `bus_st_t bus_check(const bus_t *b, uint32_t addr, unsigned size, bool write, int *idx)`, which checks, in this order: unmapped → `F_UNMAPPED`; width not allowed (including widths other than 1/2/4) → `F_SIZE`; misaligned in a must-align region → `F_UNALIGNED`; last byte beyond the region → `F_CROSS`; write to read-only → `F_RO`; clock off with policy FAULT → `F_GATED`. Implement little-endian byte-array device callbacks `mem_rd/mem_wr` that return a slave error if `off+size` exceeds the array.
> 2. Implement `map_validate()` that rejects zero-size regions, regions wrapping past 2^32, and overlapping regions (table not necessarily sorted). All arithmetic must be correct for a region ending at 2^32.
> 3. Implement a fault log (`nfault`, last fault, and a BFAR-like address captured only for *precise* faults, kept until cleared), a depth-1 posted write buffer whose faults surface as *imprecise* on the next access, and `int uart_putc(bus_t *b, uint32_t base, char c, int max_polls)` that polls `SR.TXE` (bit 7) with a bound and writes `DR`. Return 0 / -1 (bus fault) / -2 (timeout).
>
> Example: reading 4 bytes at `0x2000_0011` after writing `0x11223344` at `0x2000_0010` and `0xAABBCCDD` at `0x2000_0014` gives `0xDD112233`; `bus_check(0x4002_0015, 2)` → `F_UNALIGNED`; a 4-byte read at `0x2000_3FFE` → `F_CROSS`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/ARCH/ARCH-B11
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있다
mkdir -p /tmp/esn_ARCH-B11
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/esn_ARCH-B11/st && /tmp/esn_ARCH-B11/st; echo "exit $?"
```

실제 출력 (앞뒤만):

```text
FAIL starter.c:300  map_find(&bus, 0x08000000u) == R_FLASH
FAIL starter.c:301  map_find(&bus, 0x0800FFFFu) == R_FLASH
FAIL starter.c:303  map_find(&bus, 0x20003FFFu) == R_SRAM
...
FAIL starter.c:377  sweep_mismatches(&b3) == 0
FAIL starter.c:383  bus.nfault == 1 && bus.bfar_valid && bus.bfar == 0x30000000u && bus.last.precise
...
FAIL starter.c:420  bus_write(&bus, 0x40020000u + GPIO_BSRR + 2, 2, 1u << 5) == BUS_OK && gpioa.odr == 0
FAIL starter.c:421  bus_write(&bus, 0x40020000u + GPIO_ODR + 1, 1, 0x80u) == BUS_OK && gpioa.odr == 0x8000u
13/67 passed
exit 1
```

TODO 는 9개: `mem_rd`, `mem_wr`, `map_find`, `bus_check` (Part A), `map_validate` (Part B), `fault_record`, `bus_drain`, `bus_write_posted`, `uart_putc` (Part C). 지나가는 13개는 "미매핑이면 -1" 처럼 스텁이 우연히 맞히는 것들이다.

## 4. 힌트

<details>
<summary>힌트 1 — 영역 포함 검사는 뺄셈 한 번</summary>

`base <= addr && addr < base + size` 는 `base + size` 가 2^32 일 때 0 으로 감겨 틀린다. 대신 `addr - base < size` (부호 없는 32비트). addr 가 base 보다 작으면 뺄셈이 감겨 아주 큰 수가 되어 자동으로 거짓이다.

</details>

<details>
<summary>힌트 2 — 검사 순서에는 이유가 있다</summary>

정책은 영역마다 다르니 **디코드가 먼저**. 폭이 1/2/4 가 아니면 "정렬" 이 정의되지 않으니(`addr & (size-1)` 은 2의 거듭제곱일 때만 의미) **폭이 정렬보다 먼저**. 영역 끝 걸침은 `(uint64_t)(addr - base) + size > region.size` — 64비트로 올려서 더한다.

</details>

<details>
<summary>힌트 3 — 리틀 엔디언 모으기</summary>

`v |= (uint32_t)p[off + i] << (8 * i)` — `uint8_t` 는 `int` 로 승격되므로 `<< 24` 전에 `uint32_t` 로 캐스트 (C-B02). 쓰기는 `p[off + i] = (uint8_t)(val >> (8 * i))`.

</details>

<details>
<summary>힌트 4 — 겹침 판정</summary>

반열린 구간 `[a0, a1)` 과 `[b0, b1)` 이 겹치는 조건은 `a0 < b1 && b0 < a1`. 끝이 2^32 일 수 있으니 `uint64_t` 로. 딱 붙은 두 영역(`a1 == b0`)은 겹침이 아니다.

</details>

<details>
<summary>힌트 5 — posted write 와 BFAR</summary>

drain 할 때는 **버퍼를 먼저 비우고**(`wb_full = false`) `do_access(..., precise=false)` 를 부른다. `fault_record` 는 `precise && !bfar_valid` 일 때만 `bfar` 를 잡는다 — imprecise 폴트의 주소는 "그 시점 명령" 과 무관하므로 BFAR 에 넣지 않는다.

</details>

<details>
<summary>힌트 6 — 폴링 루프는 반드시 끝나야 한다</summary>

`while (!(SR & TXE));` 는 클럭이 꺼져 SR 이 0 으로 읽히면 영원히 돈다. 횟수 상한을 두고, SR 읽기 자체가 폴트면 바로 -1.

</details>

## 5. 풀이

### Part A — 표 + 뺄셈 한 번 + 콜백

디코더 = "어느 행인가" 를 찾는 함수. 하드웨어 디코더는 주소 상위 비트를 각 슬레이브의 base 와 **병렬 비교기** 로 비교한다 (그래서 실제 칩의 영역 base 는 보통 크기의 배수다 — 비교할 비트가 깔끔하게 상위 몇 비트로 정해진다). 소프트웨어 모델에선 선형 탐색으로 충분하고, 영역이 많으면 정렬된 표의 이진 탐색(추가 연습 1)이나 상위 비트 인덱스 표를 쓴다.

```c
for (int i = 0; i < b->n; i++)
    if (addr - b->map[i].base < b->map[i].size) return i;
```

| addr | `addr - base` (FLASH 행) | `< 0x10000` ? | 결과 |
|---|---|---|---|
| 0x0800_0000 | 0x0000_0000 | 예 | FLASH |
| 0x0800_FFFF | 0x0000_FFFF | 예 | FLASH |
| 0x0801_0000 | 0x0001_0000 | 아니오 | 다음 행… → -1 |
| 0x07FF_FFFF | 0xFFFF_FFFF (감김) | 아니오 | 다음 행… |

콜백에 넘기는 오프셋은 `addr − base + ctx_off`. 디바이스는 **자기 안의 오프셋만** 안다 — 실제 IP 도 그렇다 (UART IP 는 자기가 0x4000_4400 에 붙었는지 모른다. 그래서 같은 IP 를 USART1/USART2 에 여러 번 붙인다). `ctx_off` 는 alias 창이 디바이스의 중간을 보게 할 때 쓴다.

디바이스 콜백은 **레인(lane)** 을 처리한다. GPIO 모델은 `off & 3` 으로 바이트 레인을 골라 `(word >> 8*lane) & mask` 로 읽고, 쓸 때는 그 레인만 바꾼다. 그래서 `BSRR + 2` 에 하프워드로 `1 << 5` 를 쓰면 상위 16비트(= reset 쪽)에 들어가 핀 5 가 꺼진다 — 옛 STM32F4 CMSIS 헤더의 `BSRRL / BSRRH` (16비트 두 개) 가 바로 이 사용법이었다.

### Part B — 검사 순서와 경계 산수

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b11-arr2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">bus_check — 첫 번째로 걸린 이유 하나만 보고한다</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="96" height="50" rx="6" class="f-mut"/>
    <rect x="128" y="40" width="96" height="50" rx="6" class="f-mut"/>
    <rect x="246" y="40" width="96" height="50" rx="6" class="f-mut"/>
    <rect x="364" y="40" width="96" height="50" rx="6" class="f-mut"/>
    <rect x="482" y="40" width="96" height="50" rx="6" class="f-mut"/>
    <rect x="600" y="40" width="96" height="50" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="58" y="60">① 디코드</text><text x="58" y="78">map_find</text>
    <text x="176" y="60">② 폭</text><text x="176" y="78">sizes 마스크</text>
    <text x="294" y="60">③ 정렬</text><text x="294" y="78">addr&amp;(size−1)</text>
    <text x="412" y="60">④ 걸침</text><text x="412" y="78">off+size&gt;len</text>
    <text x="530" y="60">⑤ 쓰기권한</text><text x="530" y="78">writable</text>
    <text x="648" y="60">⑥ 클럭</text><text x="648" y="78">gate+clk_on</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M106 65 H126" marker-end="url(#b11-arr2)"/><path d="M224 65 H244" marker-end="url(#b11-arr2)"/>
    <path d="M342 65 H362" marker-end="url(#b11-arr2)"/><path d="M460 65 H480" marker-end="url(#b11-arr2)"/>
    <path d="M578 65 H598" marker-end="url(#b11-arr2)"/>
    <path d="M58 90 V118" marker-end="url(#b11-arr2)"/><path d="M176 90 V118" marker-end="url(#b11-arr2)"/>
    <path d="M294 90 V118" marker-end="url(#b11-arr2)"/><path d="M412 90 V118" marker-end="url(#b11-arr2)"/>
    <path d="M530 90 V118" marker-end="url(#b11-arr2)"/><path d="M648 90 V118" marker-end="url(#b11-arr2)"/>
  </g>
  <g stroke="currentColor">
    <rect x="10" y="120" width="96" height="26" class="f-warn"/><rect x="128" y="120" width="96" height="26" class="f-warn"/>
    <rect x="246" y="120" width="96" height="26" class="f-warn"/><rect x="364" y="120" width="96" height="26" class="f-warn"/>
    <rect x="482" y="120" width="96" height="26" class="f-warn"/><rect x="600" y="120" width="96" height="26" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">
    <text x="58" y="138">UNMAPPED</text><text x="176" y="138">SIZE</text><text x="294" y="138">UNALIGNED</text>
    <text x="412" y="138">CROSS</text><text x="530" y="138">RO</text><text x="648" y="138">GATED</text>
  </g>
  <rect x="10" y="166" width="686" height="34" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="20" y="188" fill="currentColor" font-size="12">통과 → (RAZ/WI 게이트면 0 읽기·쓰기 무시) → 디바이스 콜백 → 실패면 SLVERR (슬레이브가 거부)</text>
  <text x="10" y="226" fill="currentColor" font-size="12">실제 Cortex-M 순서는 다르다:</text>
  <text x="10" y="246" fill="currentColor" font-size="12">정렬(코어가 버스 전에, UsageFault) → MPU(MemManage) → 버스 디코드·슬레이브 응답(BusFault).</text>
  <text x="10" y="266" fill="currentColor" font-size="12">이 모델은 정렬 정책이 영역마다 달라서 디코드를 먼저 한다.</text>
  <text x="10" y="286" fill="currentColor" font-size="12">면접에서 이 차이를 말할 수 있으면 좋다.</text>
</svg>
```

| 순서 | 왜 이 자리인가 | 테스트 예 |
|---|---|---|
| ① 디코드 | 정책은 영역의 속성 → 영역을 알아야 나머지를 판정 | `0x3000_0001, size 3` → UNMAPPED (SIZE 아님) |
| ② 폭 | 3바이트·0바이트는 "정렬" 자체가 정의 안 됨, `size-1` 마스크가 무의미 | `0x4002_0001, size 3` → SIZE (UNALIGNED 아님) |
| ③ 정렬 | 정렬된 접근은 자연 정렬 블록 안 → 영역 크기가 폭의 배수면 ④에 절대 안 걸림 | `0x4002_0015, size 2` → UNALIGNED |
| ④ 걸침 | 정렬 안 해도 되는 SRAM/Flash 에서만 실제로 발생 | `0x2000_3FFE, size 4` → CROSS |
| ⑤ RO | 접근 모양이 맞아야 "쓰기 권한" 질문이 의미 있음 | Flash, BOOT alias 쓰기 → RO |
| ⑥ 게이트 | 클럭 상태는 런타임 상태 → 가장 마지막 | USART2 클럭 꺼짐 + FAULT → GATED |

**경계 산수.** 영역 끝이 2^32 인 `TOP = [0xFFFF_F000, 2^32)` 에서 `0xFFFF_FFFE` 4바이트:

- 틀린 코드 `addr + size <= base + size` → 32비트에서 좌변 `0x0000_0002`, 우변 `0x0000_0000`(감김) — 결과는 우연에 맡겨진다.
- 맞는 코드 `(uint64_t)(addr - base) + size > r->size` → `0xFFE + 4 = 0x1002 > 0x1000` → CROSS.

**map_validate.** 영역별 검사(0 크기, `base + size > 2^32`)를 **먼저 전부** 하고, 그다음 모든 쌍 겹침. 정렬을 가정하지 않으니 O(n²) 지만 맵은 부팅 때 한 번 검사하면 된다. 테스트에서 GPIOA 를 0x4002_0200 에 하나 더 두는(=앞 GPIOA 와 반쯤 겹치는) 경우를 순서가 섞인 표로 잡는다.

**alias.** 실제 STM32 는 BOOT 핀(과 SYSCFG 의 remap 레지스터)에 따라 0x0000_0000 에 main Flash / system memory(부트로더) / SRAM 중 하나가 보인다. Cortex-M 은 리셋 때 0x0 에서 초기 SP, 0x4 에서 리셋 벡터를 읽으니(VTOR 기본값 0) 이 alias 가 부팅을 가능하게 한다. 모델에선 같은 `ctx` 를 가리키는 행 하나 — 테스트에서 `0x0000_0004` 와 `0x0800_0004` 가 둘 다 `0x0800_0101` (Thumb 비트 1).

설정 실수 alias (`SRAM_ALIAS` 창 32 KB, 뒤 SRAM 16 KB): 디코더는 `0x1000_4000` 을 통과시키지만 `mem_rd` 가 범위 밖이라 거부 → **SLVERR**. 미매핑(UNMAPPED)과 구별하는 이유: AXI 로 치면 **DECERR**(인터커넥트가 슬레이브를 못 찾음) vs **SLVERR**(슬레이브가 받았지만 거부) — 디버그 방향이 다르다(맵 문제 vs IP 문제).

**참조 모델 비교.** 테스트 하네스의 `ref_check` 는 같은 규칙을 다른 방식(64비트 비교, 뒤에서부터 탐색, 바이트 하나씩 걸침 판정)으로 구현한다. 모든 영역의 시작·끝 ±16 주소 × 폭 0..5 × R/W, 그리고 xorshift32 고정 시드 무작위 20만 개에서 `bus_check` 와 결과가 하나도 다르지 않아야 한다. 경계 off-by-one 은 이런 스윕이 아니면 잘 안 잡힌다.

### Part C — BFAR, 쓰기 버퍼, 조용한 실패

**모델 상태 ↔ 실제 Cortex-M(ARMv7-M) ↔ 버스 응답:**

| 모델 | Cortex-M3/M4 에서 보통 보이는 것 | 버스 |
|---|---|---|
| UNMAPPED (읽기) | BusFault, CFSR.PRECISERR + BFARVALID, BFAR = 주소 | AHB 기본 슬레이브 HRESP=ERROR / AXI DECERR |
| UNMAPPED (버퍼된 쓰기) | BusFault, IMPRECISERR, **BFAR 무효** | 같음, 단 나중에 |
| UNALIGNED | UsageFault, UFSR.UNALIGNED (버스 전에 코어가 검출). `LDR/STR/LDRH/STRH` 는 Normal 메모리에서 비정렬 허용(CCR.UNALIGN_TRP=0 일 때), `LDM/STM/LDRD/STRD/LDREX` 는 항상 폴트. Cortex-M0/M0+(ARMv6-M) 은 비정렬이면 무조건 HardFault | 버스 트랜잭션 없음 |
| SIZE | 주변장치마다 다름: 버스 에러가 나거나, 그냥 무시되거나 (RM 의 "접근 폭" 문구 확인) | SLVERR 또는 무응답 처리 |
| CROSS | 영역 끝을 넘는 비정렬 접근은 여러 정렬 전송으로 쪼개지고(연습 4) 뒷부분이 미매핑을 치면 BusFault | |
| RO | Flash 쓰기는 부품마다 다름 (플래시 컨트롤러 에러 플래그 / 버스 에러). MPU 의 RO 영역이면 MemManage DACCVIOL | |
| GATED | 칩마다 다름: 예) NXP Kinetis 계열은 클럭 게이트된 주변장치 접근이 폴트로 유명. STM32 는 보통 폴트 없이 0 읽기·쓰기 무시 | |
| SLVERR | BusFault (PRECISERR/IMPRECISERR) | AXI SLVERR / APB PSLVERR |

BusFault 를 SHCSR.BUSFAULTENA 로 켜 두지 않으면 HardFault 로 **승격(escalation)** 되고 HFSR.FORCED 가 선다. 그래서 HardFault 핸들러는 CFSR(0xE000_ED28) · HFSR(0xE000_ED2C) · BFAR(0xE000_ED38) · MMFAR(0xE000_ED34) 를 같이 덤프한다.

**BFAR 를 "첫 precise 폴트, 지울 때까지 유지" 로 모델한 이유.** 핸들러가 읽기 전에 두 번째 폴트 주소로 덮이면 원인을 잃는다. 실제 ARMv7-M 에서도 BFAR 은 BFARVALID 와 짝으로만 의미가 있고, 우선순위가 더 높은 폴트가 끼어들면 바뀔 수 있으므로 "BFAR 를 읽고 → BFARVALID 가 아직 1 인지 다시 확인" 하라고 안내한다. 지우는 건 CFSR 의 해당 비트에 1 을 써서(W1C, C-M02) — 모델의 `fault_clear`.

**imprecise 폴트 재현.**

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">쓰기 버퍼가 있으면 폴트는 "다음" 명령에서 보고된다</text>
  <text x="705" y="20" fill="currentColor" text-anchor="end" font-size="12" opacity=".7">시간 →</text>
  <g fill="currentColor" font-weight="bold"><text x="10" y="64">CPU</text><text x="10" y="124">쓰기 버퍼</text><text x="10" y="184">버스</text></g>
  <rect x="90" y="44" width="200" height="32" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="190" y="65" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">STR 7 → 0x3000_0004</text>
  <rect x="300" y="44" width="200" height="32" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="400" y="65" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">LDR [0x2000_0000]</text>
  <rect x="90" y="104" width="300" height="32" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="240" y="125" fill="currentColor" text-anchor="middle" font-size="12">0x3000_0004 대기 (CPU 는 이미 OK 받음)</text>
  <rect x="300" y="164" width="110" height="32" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="355" y="185" fill="currentColor" text-anchor="middle" font-size="12">drain: ERROR</text>
  <rect x="420" y="164" width="140" height="32" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="490" y="185" fill="currentColor" text-anchor="middle" font-size="12">LDR 읽기 OK</text>
  <path d="M355 164 L470 80" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <rect x="520" y="44" width="200" height="32" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="620" y="65" fill="currentColor" text-anchor="middle" font-size="12">IMPRECISERR, BFAR 무효</text>
  <text x="10" y="226" fill="currentColor" font-size="12">스택에 쌓인 PC 는 LDR 이후를 가리킨다 — 범인 STR 은 그보다 앞에 있다.</text>
  <text x="10" y="246" fill="currentColor" font-size="12">찾는 법: 쓰기 버퍼를 꺼서(ACTLR.DISDEFWBUF, Cortex-M3/M4, 디버그용) precise 로 만들거나,</text>
  <text x="10" y="266" fill="currentColor" font-size="12">의심 store 뒤에 DSB 를 넣어 범위를 좁힌다.</text>
</svg>
```

`demo_fault_trace.c` 는 solution.c 의 모델을 그대로 가져와(`#define main lesson_main` 후 `#include "solution.c"`) 접근 로그를 찍는다. 실제 출력:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_fault_trace.c -o /tmp/esn_ARCH-B11/demo && /tmp/esn_ARCH-B11/demo
```

```text
memory map (map_validate = 0)
  name    range                   sizes  align rw  gate
  BOOT    0x00000000-0x0000FFFF  1/2/4  no    ro  -
  FLASH   0x08000000-0x0800FFFF  1/2/4  no    ro  -
  SRAM    0x20000000-0x20003FFF  1/2/4  no    rw  -
  USART2  0x40004400-0x400047FF  -/2/4  yes   rw  RAZ/WI
  GPIOA   0x40020000-0x400203FF  1/2/4  yes   rw  RAZ/WI

reset: initial SP (alias)    R4 @0x00000000  BOOT    → OK        val=0x20004000
reset: Reset_Handler         R4 @0x00000004  BOOT    → OK        val=0x08000101
stack push                   W4 @0x20003FFC  SRAM    → OK       
buf @+0x10 = 0x11223344      W4 @0x20000010  SRAM    → OK       
buf @+0x14 = 0xAABBCCDD      W4 @0x20000014  SRAM    → OK       
packed struct field          R4 @0x20000011  SRAM    → OK        val=0xDD112233
GPIOA->BSRR = 1<<5           W4 @0x40020018  GPIOA   → OK       
GPIOA ODR byte 0             R1 @0x40020014  GPIOA   → OK        val=0x20
uint16_t* into GPIO, odd     R2 @0x40020015  GPIOA   → UNALIGNED  [fault #1 precise]
USART2 DR as byte            W1 @0x40004404  USART2  → SIZE       [fault #2 precise]
NULL deref (+0x30)           W4 @0x00000030  BOOT    → RO         [fault #3 precise]
stray pointer                R4 @0x30000000  -       → UNMAPPED   [fault #4 precise]
memcpy past SRAM end         R4 @0x20003FFE  SRAM    → CROSS      [fault #5 precise]
  BFAR-like: valid=1 addr=0x40020015, faults=5

posted write to unmapped address, then the next access:
*(uint32_t*)0x30000004 = 7   W4 @0x30000004  (buffered) → returns OK, faults=5
next: load from SRAM         R4 @0x20000000  SRAM    → OK        val=0x00000000  [fault #6 IMPRECISE]
  last fault addr=0x30000004 precise=0  BFAR valid=0  ← 주소는 기록에만, BFAR 는 비어 있다

USART2 clock forgotten (RCC enable bit not set):
  gate=RAZ/WI : uart_putc → -2 (timeout after 1000 polls, faults=6, tx="")
  gate=FAULT  : uart_putc → -1 (fault GATED @0x40004400, faults=7)
```

읽을 거리 세 개:

1. **NULL 읽기는 폴트가 아니다.** `0x0000_0000` 은 BOOT alias 라 읽으면 초기 SP 가 나온다. 모델에선 쓰기만 RO 로 걸렸다. 실제 Cortex-M 에서 NULL 역참조를 잡으려면 MPU 로 0 근처를 접근 금지 영역으로 만든다.
2. **imprecise 폴트는 "LDR 이 범인처럼" 보인다.** 폴트 #6 은 SRAM 읽기 도중 보고됐지만 원인 주소는 그 앞의 store.
3. **클럭 안 켠 UART**: RAZ/WI 칩에서는 폴트 0 개, 1000 번 폴링 후 타임아웃, 송신 0 바이트. 폴링에 상한이 없었다면 여기서 멈춘다(워치독 리셋 → ARCH-B14 의 리셋 원인 로그로 이어진다). STM32 에서 "레지스터에 써도 안 바뀐다" 의 첫 번째 용의자는 `RCC_APB1ENR`/`RCC_AHB1ENR` 의 enable 비트다.

**UART 드라이버.** 모델 UART 는 DR 을 쓰면 SR 을 2번 읽는 동안 TXE=0 (전송 중). 그래서 `max_polls = 2` 로는 연속 두 번째 문자가 타임아웃(-2)이고, 폴링 없이 DR 을 바로 또 쓰면 `lost` 가 1 오른다 — "TXE 확인 없이 DR 쓰기" 버그가 글자를 잃는 모습이다.

**Cortex-M 에서 같은 C 코드가 내는 버스 접근 (`demo_unaligned_asm.c`).** 실제 출력(지시어·라벨 줄 제거):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_unaligned_asm.c -o -
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -mno-unaligned-access -S demo_unaligned_asm.c -o -
clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_unaligned_asm.c -o -
```

```text
=== cortex-m4
uart_putc_mmio:
	movw	r1, #17408          ; 0x4400
	movt	r1, #16384          ; 0x4000  → r1 = 0x4000_4400 (USART2)
	ldr	r2, [r1]            ; SR 워드 읽기
	lsls	r2, r2, #24         ; bit7(TXE) → N 플래그
	bmi	.LBB0_4
	...                         ; (폴링 루프 언롤)
	str	r0, [r1, #4]        ; DR 워드 쓰기
	bx	lr
load_u32_unaligned:
	ldr	r0, [r0]            ; 비정렬 가능 → LDR 한 번
	bx	lr
=== cortex-m4 -mno-unaligned-access
load_u32_unaligned:
	ldrb	r1, [r0]
	ldrb	r2, [r0, #2]
	ldrb	r3, [r0, #3]
	ldrb	r0, [r0, #1]
	orr.w	r2, r2, r3, lsl #8
	orr.w	r0, r1, r0, lsl #8
	orr.w	r0, r0, r2, lsl #16
	bx	lr
=== cortex-m0
uart_putc_mmio:
	ldr	r1, .LCPI0_0        ; 상수 풀에서 0x4000_4400
	...
load_u32_unaligned:
	ldrb	r1, [r0]
	ldrb	r2, [r0, #1]
	lsls	r2, r2, #8
	adds	r1, r2, r1
	ldrb	r2, [r0, #2]
	lsls	r2, r2, #16
	ldrb	r0, [r0, #3]
	lsls	r0, r0, #24
	adds	r0, r0, r2
	adds	r0, r0, r1
	bx	lr
```

(`;` 주석은 설명을 위해 붙였다.) M4 는 Normal 메모리 비정렬 `LDR` 을 지원하니 한 명령, M0 은 비정렬이 HardFault 라 컴파일러가 바이트 4개로 쪼갠다. `-mno-unaligned-access` 는 M4 에서도 바이트 접근을 강제한다 — CCR.UNALIGN_TRP 를 켠 프로젝트나 **비정렬 접근을 허용하지 않는 영역**(주변장치, 일부 외부 메모리)을 포인터로 훑는 코드에 필요하다. MMIO 쪽은 `volatile uint32_t` 필드라 항상 워드 `ldr/str` — 모델의 USART2 "폭 2/4 만" 정책과 맞는다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/esn_ARCH-B11/so && /tmp/esn_ARCH-B11/so; echo "exit $?"
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/esn_ARCH-B11/san && /tmp/esn_ARCH-B11/san
```

```text
67/67 passed
exit 0
67/67 passed
```

(Apple clang 21.0.0, Apple M6. ASan/UBSan 경고 없음.)

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `addr >= base && addr < base + size` | `base + size` 가 2^32 이면 0 으로 감겨 TOP 영역 전체가 미매핑 | `addr - base < size` |
| `addr + size > base + size` 로 걸침 판정 | 32비트 덧셈이 감김 → `0xFFFF_FFFE + 4 = 2` | `(uint64_t)(addr - base) + size > size` |
| 정렬을 폭보다 먼저 검사 | size 3 에서 `addr & 2` 같은 엉뚱한 판정, 보고 이유가 틀림 | 폭(1/2/4) → 정렬 |
| `p[off] \| p[off+1] << 8 \| p[off+3] << 24` | `uint8_t` → `int` 승격 후 `<< 24` 가 부호 비트로 → UB (C-B02) | `(uint32_t)p[...] << 24` |
| 겹침을 "정렬된 표의 이웃끼리" 만 비교 | 표가 정렬돼 있다는 보장이 없으면 멀리 떨어진 행끼리 겹침을 놓침 | 모든 쌍 (또는 먼저 정렬) |
| `while (!(USART2->SR & TXE));` 상한 없음 | 클럭 미설정·리셋 유지·잘못된 base 에서 SR=0 → 영원히 대기 | 횟수/시간 상한 + 에러 반환 |
| imprecise 폴트에서 스택된 PC 의 명령을 범인으로 단정 | 쓰기 버퍼 때문에 원인 store 는 그보다 앞 | DISDEFWBUF 로 재현, 의심 store 뒤 DSB |
| 비정렬 `uint16_t *` 로 주변장치 레지스터 접근 | 주변장치는 Device 메모리 — 비정렬 접근은 정의된 동작이 아님 | 레지스터는 `volatile uint32_t` 필드, 정렬 접근만 |

## 8. 면접 Q&A

<details>
<summary>Q1. 주소 디코더는 하드웨어로 어떻게 만들어지나요? 왜 영역 base 가 보통 크기의 배수인가요?</summary>

슬레이브마다 "주소 상위 비트 == 이 슬레이브의 패턴" 비교기가 있고, 그 출력이 select 선(AHB HSELx)이 된다. 크기가 2^k 이고 base 가 2^k 의 배수면 비교는 상위 `32−k` 비트의 상수 비교 하나로 끝난다 — 덧셈기나 범위 비교기가 필요 없다. 아무 select 도 안 켜지면 **기본 슬레이브** 가 선택되어 에러 응답을 낸다.

꼬리질문: "디코더가 상위 비트 일부를 안 보면?" → **부분 디코드(partial decode)**: 같은 레지스터가 여러 주소에 반복(mirror)해서 보인다. 비용 절감으로 일부러 하기도 하지만, 의도치 않으면 aliasing 버그 — 찾는 테스트는 SIV-B06.

</details>

<details>
<summary>Q2. Cortex-M4 에서 매핑 안 된 주소 읽기와 비정렬 주소 읽기는 각각 어떤 폴트가 나나요?</summary>

미매핑 읽기: 버스가 에러 응답 → **BusFault**, CFSR 의 PRECISERR 와 BFARVALID, BFAR 에 주소. 비정렬: 코어가 버스에 내보내기 **전에** 판정 → `LDM/STM/LDRD/STRD/LDREX` 류면 항상, `LDR/LDRH/STR/STRH` 는 CCR.UNALIGN_TRP=1 일 때 **UsageFault** UNALIGNED. 해당 fault 가 SHCSR 에서 꺼져 있으면 둘 다 **HardFault 로 승격**(HFSR.FORCED).

꼬리질문: "Cortex-M0 에서는?" → ARMv6-M 은 비정렬 접근 자체가 없어 바로 HardFault, 그리고 CFSR/BFAR 같은 상세 폴트 레지스터도 없다 — 스택된 PC 와 디스어셈블로 추적한다.

</details>

<details>
<summary>Q3. HardFault 가 났는데 BFARVALID 가 0 이고 IMPRECISERR 가 1 입니다. 어떻게 디버그하나요?</summary>

버퍼된 쓰기(store)가 나중에 에러 응답을 받은 것. 스택된 PC 는 그 store 이후의 명령이다. ① Cortex-M3/M4 면 ACTLR.DISDEFWBUF 로 기본 쓰기 버퍼를 꺼서 precise 로 바꾸고 재현 → BFAR 가 채워진다 ② 의심 구간의 store 뒤에 `__DSB()` 를 넣어 범위를 좁힌다 ③ PC 바로 앞의 store 들의 주소 레지스터 값을 스택 프레임에서 추적.

꼬리질문: "DSB 가 왜 도움이 되나?" → DSB 는 앞선 메모리 접근이 끝날 때까지 다음 명령을 막으므로, 에러가 DSB 시점(= 바로 앞 store 근처)에 보고된다.

</details>

<details>
<summary>Q4. Cortex-M 에서 NULL 포인터 역참조가 왜 바로 크래시하지 않나요?</summary>

주소 0 은 Code 영역이고 대부분의 MCU 가 거기에 Flash(또는 alias)를 매핑한다 — 벡터 테이블(초기 SP, 리셋 벡터)이 있다. 그래서 `*(uint32_t *)0` 은 초기 SP 값을 조용히 읽는다. 쓰기는 Flash 라서 무시되거나 플래시 컨트롤러 에러가 되거나 칩마다 다르다. 잡으려면 MPU 로 0 부근(예: 첫 몇 백 바이트~수 KB)을 no-access 영역으로 설정하고, 벡터 테이블은 VTOR 로 다른 곳을 쓰게 한다.

꼬리질문: "구조체 포인터가 NULL 이고 필드 오프셋이 0x4000_0000 보다 크면?" → `NULL->field` 가 엉뚱한 주변장치를 건드릴 수 있다. NULL 검사 대신 주소 범위를 믿으면 안 되는 이유.

</details>

<details>
<summary>Q5. UART 레지스터에 값을 써도 읽으면 0 입니다. 체크리스트를 말해 보세요.</summary>

① **클럭 enable** (STM32 `RCC_APBxENR`/`RCC_AHBxENR`) — 꺼져 있으면 STM32 류는 폴트 없이 0 읽기·쓰기 무시 ② 주변장치 **리셋 유지** (`RCC_APBxRSTR` 비트가 1 로 남아 있음) ③ **base 주소** 오타 / 다른 인스턴스(USART1 vs USART2) ④ **접근 폭** — 바이트 쓰기를 허용 안 하는 IP ⑤ 쓰기 보호·락 비트, 다른 비트가 먼저 설정돼야 쓸 수 있는 레지스터 ⑥ 쓰기 전용·읽으면 다른 값인 레지스터(DR 은 TDR/RDR 이 같은 주소) ⑦ `volatile` 누락으로 접근 자체가 사라짐(C-M01).

꼬리질문: "클럭을 켠 직후 바로 쓰면?" → 일부 STM32 에라타는 enable 직후 몇 사이클은 주변장치 접근이 반영되지 않을 수 있다고 적고 있어, HAL 은 enable 후 그 레지스터를 한 번 읽어 지연을 넣는다. (정확한 조건은 부품 에라타 확인.)

</details>

<details>
<summary>Q6. 시뮬레이터(QEMU, 가상 플랫폼)에서 주소 디코드를 빠르게 하려면?</summary>

선형 탐색 O(n) → 정렬된 표 이진 탐색 O(log n)(연습 1) → 상위 비트로 인덱싱하는 페이지 표(예: 4 KB 단위 2단계 표, O(1)). QEMU 는 MemoryRegion 트리(우선순위로 겹침 허용)를 평탄화한 뷰를 만들고 그 위에 빠른 조회 구조와 TLB 를 둔다 — "겹침 허용 + 우선순위" 는 alias·오버레이(부트 ROM 을 0 에 덮었다가 치우기)를 표현하려는 것.

꼬리질문: "그럼 이 레슨의 map_validate 는 왜 겹침을 금지하나?" → 우선순위 규칙이 없는 단순 맵에선 겹침 = 설정 실수. 겹침을 허용하려면 우선순위를 명시하고 "누가 이기나" 를 테스트해야 한다.

</details>

<details>
<summary>Q7. AXI 의 SLVERR 와 DECERR 는 어떻게 다르고, 펌웨어 디버그에서 왜 구분이 중요한가요?</summary>

DECERR = 인터커넥트가 그 주소에 해당하는 슬레이브를 못 찾음(보통 기본 슬레이브가 응답) → **맵/주소 문제**. SLVERR = 슬레이브까지 갔지만 슬레이브가 거부(지원 안 하는 폭, 꺼진 상태, 내부 에러) → **IP·설정 문제**. CPU 쪽 BusFault 에선 둘이 같은 비트로 보일 수 있어서, 실리콘 검증에선 버스 모니터/트레이스로 응답 코드를 직접 본다.

꼬리질문: "APB 는?" → APB3 부터 PSLVERR 신호가 있고, APB 브리지가 그것을 AHB/AXI 에러 응답으로 바꿔 올린다.

</details>

## 9. 추가 연습

### 연습 1 — 이진 탐색 디코더

base 오름차순·겹침 없는 표에서 `find_bsearch(m, n, addr)` 를 구현하라. 선형 탐색과 모든 영역 경계 ±4, 무작위 20만 주소에서 결과가 같아야 한다 (끝이 2^32 인 영역 포함).

<details>
<summary>답 보기</summary>

`base <= addr` 인 **마지막** 행을 이진 탐색으로 찾고(`cand = mid; lo = mid + 1`), 그 행 하나만 `addr - base < size` 로 확인. 겹침이 없으니 포함할 수 있는 후보는 그 행뿐이다. `mid = lo + (hi - lo) / 2` 로 오버플로 회피.

</details>

### 연습 2 — ARMv7-M 기본 메모리 맵 분류기

`cm_class(addr)` 가 영역 이름 · 메모리 타입(Normal / Device / Strongly-ordered) · XN(실행 금지)을 돌려주게 하라: Code `0x0000_0000–0x1FFF_FFFF`, SRAM `0x2000_0000–0x3FFF_FFFF`, Peripheral `0x4000_0000–0x5FFF_FFFF`, External RAM `0x6000_0000–0x9FFF_FFFF`, External device `0xA000_0000–0xDFFF_FFFF`, PPB `0xE000_0000–0xE00F_FFFF`, Vendor system `0xE010_0000–0xFFFF_FFFF`.

<details>
<summary>답 보기</summary>

상한 비교 사다리. Code/SRAM/External RAM 은 Normal·실행 가능, Peripheral/External device 는 Device·XN, PPB 는 Strongly-ordered·XN, Vendor 는 Device·XN. 펌웨어에 쓰는 법: "주변장치 영역으로 점프하면 왜 폴트?" → XN 이라 명령 fetch 가 MemManage(IACCVIOL) 류 폴트. MPU 가 꺼져 있으면 이 기본 맵 속성이 그대로 적용된다.

</details>

### 연습 3 — bit-band 별칭 주소

Cortex-M3/M4 의 bit-band: SRAM `0x2000_0000–0x200F_FFFF` → alias `0x2200_0000`, 주변장치 `0x4000_0000–0x400F_FFFF` → alias `0x4200_0000`. `bb_alias(addr, bit)`, 역변환 `bb_target(alias, &addr, &bit)`, alias 에 쓰면 한 비트만 바뀌는 `bb_write` 를 구현하라. ARM 문서 예: `0x23FF_FFE0` ↔ `0x200F_FFFF` bit 0, `0x2200_001C` ↔ `0x2000_0000` bit 7.

<details>
<summary>답 보기</summary>

`alias = alias_base + (addr − region_base) × 32 + bit × 4` — 원본 1바이트의 8비트가 alias 워드 8개(32바이트)로 펼쳐진다. 역변환은 `off = alias − alias_base`, `addr = region_base + off / 32`, `bit = (off % 32) / 4`, `off % 4 != 0` 이면 무효. alias 쓰기는 값의 bit 0 만 보고, 버스가 대상 바이트를 원자적으로 RMW 한다 — ISR 과 경쟁 없는 비트 set/clear (C-B01 의 BSRR 과 같은 목적). 주의: bit-band 는 M3/M4 의 구현 선택 기능이고 M0/M7/ARMv8-M(M33 등)에는 없다.

</details>

### 연습 4 — 비정렬 접근을 정렬 전송으로 쪼개기

`split_aligned(addr, size, out)` — 매 단계에서 현재 주소에 자연 정렬되고 남은 길이를 넘지 않는 가장 큰 폭(4/2/1)을 고른다. 예: `0x…01, 4` → `(01,1) (02,2) (04,1)`. 주소 하위 2비트 4가지 × 폭 1..4 의 전송 수 합을 구하라.

<details>
<summary>답 보기</summary>

`w = 4; while (w > size || (addr & (w-1))) w >>= 1;` 를 반복. 합은 27 (정렬 접근 8개가 각 1번, 나머지가 2~3번). 의미: 비정렬 워드 하나가 버스 전송 2~3번이 되어 느리고(ARCH-B12 의 대기 상태가 곱해진다), 영역 끝에 걸치면 뒷전송만 폴트를 낸다(Part B 의 CROSS). 실제 코어가 정확히 이렇게 쪼개는지는 구현마다 다르다 — 이건 한 가지 자연 정렬 분할이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/esn_ARCH-B11/pr && /tmp/esn_ARCH-B11/pr   # 28/28 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o /tmp/esn_ARCH-B11/psan && /tmp/esn_ARCH-B11/psan   # 28/28 passed
```

## 10. 핵심 정리

- 메모리 맵 = `{base, size, 폭, 정렬, R/W, 클럭 정책, 콜백}` 표. 디코더는 "어느 행인가" 를 찾고, 디바이스는 자기 오프셋만 안다.
- 포함 검사는 `addr - base < size` (2^32 끝 안전), 걸침은 `(uint64_t)(addr - base) + size > size`.
- 검사 순서: 디코드 → 폭 → 정렬 → 걸침 → RO → 클럭 → 슬레이브 응답. 실제 Cortex-M 은 정렬(코어, UsageFault) → MPU(MemManage) → 버스(BusFault).
- 미매핑 = DECERR/기본 슬레이브, 슬레이브 거부 = SLVERR. BusFault 꺼져 있으면 HardFault 로 승격(FORCED).
- precise 폴트는 BFAR 가 유효, 버퍼된 store 의 폴트는 IMPRECISERR + BFAR 무효 + PC 는 나중 명령. 디버그: DISDEFWBUF, DSB.
- 0x0 은 부트 alias(벡터 테이블) — NULL 읽기는 폴트가 안 난다. MPU 로 막는다.
- "레지스터가 안 써진다" 1순위는 클럭 enable. 폴링 루프엔 항상 상한.
- 비정렬: M3/M4 는 `LDR/LDRH/STR/STRH` 만 Normal 메모리에서 허용, M0 은 전부 HardFault. 주변장치 레지스터는 정렬된 `volatile uint32_t` 로만.

## 참고자료

- [ARMv7-M Architecture Reference Manual (DDI 0403)](https://developer.arm.com/documentation/ddi0403/latest) — 기본 메모리 맵, 메모리 타입, 정렬 규칙, 폴트 상태 레지스터
- [Cortex-M4 Devices Generic User Guide (DUI 0553)](https://developer.arm.com/documentation/dui0553/latest) — CFSR/HFSR/BFAR/MMFAR, SHCSR, CCR.UNALIGN_TRP, bit-banding
- [Keil Application Note 209 — Using Cortex-M3/M4/M7 Fault Exceptions](https://developer.arm.com/documentation/kan209/latest) — 폴트 핸들러 덤프, imprecise 폴트 디버그
- [STM32F4 Reference Manual RM0090](https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) — 메모리 맵, 부트 모드/remap, RCC enable, GPIO/USART 레지스터
- [AMBA AHB Protocol Specification (IHI 0033)](https://developer.arm.com/documentation/ihi0033/latest) — HSEL 디코드, 기본 슬레이브, HRESP ERROR
- [AMBA AXI Protocol Specification (IHI 0022)](https://developer.arm.com/documentation/ihi0022/latest) — RRESP/BRESP: OKAY / SLVERR / DECERR
- [QEMU Memory API](https://www.qemu.org/docs/master/devel/memory.html) — MemoryRegion 트리, alias, 우선순위, 평탄화 뷰
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* — 메모리 맵·폴트 처리 장
