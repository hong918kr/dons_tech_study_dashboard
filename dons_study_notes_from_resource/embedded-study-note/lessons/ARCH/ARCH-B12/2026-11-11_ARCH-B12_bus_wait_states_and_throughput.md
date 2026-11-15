---
id: ARCH-B12
time: 70분
updated: 2026-11-11
---
# ARCH-B12 버스 트랜잭션 기초: 대기 상태와 처리량

## 학습 목표

- 동기 버스 전송을 **주소 단계(address phase) + 데이터 단계(data phase) + 대기 상태(wait state, WS)** 로 나눠 사이클 단위로 시뮬레이션하고, 파이프라인(AHB 스타일) `1 + Σ(1+WS)` 와 비파이프라인(APB 스타일) `Σ(2+WS)` 공식을 파형으로 확인한다.
- 대역폭을 `바이트 / 사이클 × f` 로 계산해 SRAM(0 WS)과 Flash(3 WS)의 차이를 숫자로 말한다 (120 MHz: 478 MB/s vs 120 MB/s).
- 플래시 접근 시간(ns 고정)과 클럭으로 필요한 WS 를 **정수 연산** 으로 구하고, "클럭을 올렸더니 오히려 느려지는" 톱니 현상과 클럭 변경 순서(LATENCY 먼저 / 나중)를 설명한다.
- 128비트 라인 버퍼와 순차 프리페치가 순차·루프·분기 코드에서 각각 얼마나 도움이 되는지(또는 해가 되는지) 트레이스로 비교하고, 그래서 STM32F4 에 ART(명령 캐시)가 왜 붙었는지 연결한다.

## 1. 문제

"168 MHz 로 올렸는데 왜 코드가 기대만큼 안 빨라지죠?", "플래시 wait state 가 뭐고 FLASH_ACR 의 LATENCY 는 왜 클럭 바꾸기 *전에* 쓰나요?" — MCU 펌웨어 면접과 실리콘 검증(버스 성능 측정, 메모리 컨트롤러 bring-up)에서 계속 나오는 질문이다. 답은 전부 **버스 전송 한 번이 몇 사이클인가** 라는 산수에서 나온다.

CPU 가 메모리를 읽는 것은 버스 위의 **전송(transfer, transaction)** 하나다. 동기 버스에서는 모든 일이 클럭 엣지에 맞춰 일어난다.

1. **주소 단계** — 마스터(CPU)가 주소·방향(read/write)·크기를 버스에 낸다 (1 사이클).
2. **데이터 단계** — 슬레이브(메모리)가 데이터를 주거나 받는다. 슬레이브가 아직 준비가 안 됐으면 `HREADY`(AHB) / `PREADY`(APB) 를 0 으로 내려 **한 사이클씩 늘린다** = wait state.

메모리 셀의 접근 시간은 **나노초로 고정** 이다. 클럭을 올리면 한 사이클이 짧아지니 같은 접근 시간을 덮으려면 더 많은 사이클이 필요하다 → WS 가 늘어난다. SRAM 은 보통 코어 클럭에서 0 WS, 내장 플래시는 고속 클럭에서 수 WS 가 필요하다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">파이프라인 버스(AHB 스타일): Flash 읽기(3 WS) 다음 SRAM 읽기 2개 — demo_bus wave 와 같은 파형</text>
  <g fill="currentColor"><text x="10" y="58">HCLK</text><text x="10" y="108">HADDR</text><text x="10" y="158">HREADY</text><text x="10" y="208">HRDATA</text></g>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M90 64 h20 v-20 h40 v20 h40 v-20 h40 v20 h40 v-20 h40 v20 h40 v-20 h40 v20 h40 v-20 h40 v20 h40 v-20 h40 v20 h40 v-20 h40 v20 h40 v-20 h40 v20 h20"/>
  </g>
  <g stroke="currentColor" fill="none" stroke-dasharray="3 4" opacity=".45">
    <path d="M110 40 V250"/><path d="M190 40 V250"/><path d="M270 40 V250"/><path d="M350 40 V250"/>
    <path d="M430 40 V250"/><path d="M510 40 V250"/><path d="M590 40 V250"/><path d="M670 40 V250"/>
  </g>
  <rect x="110" y="92" width="80" height="26" class="f-acc" stroke="currentColor"/>
  <rect x="190" y="92" width="320" height="26" class="f-mut" stroke="currentColor"/>
  <rect x="510" y="92" width="80" height="26" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="150" y="110">A0 Flash</text><text x="350" y="110">A1 SRAM (wait 동안 붙잡혀 있음)</text><text x="550" y="110">A2</text>
  </g>
  <path d="M90 144 H190 V168 H430 V144 H670" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <rect x="190" y="146" width="240" height="22" class="f-warn" opacity=".5"/>
  <text x="310" y="162" fill="currentColor" text-anchor="middle">WS 1 · 2 · 3 (HREADY=0)</text>
  <rect x="430" y="194" width="80" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="510" y="194" width="80" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="590" y="194" width="80" height="24" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle"><text x="470" y="211">D0</text><text x="550" y="211">D1</text><text x="630" y="211">D2</text></g>
  <g fill="currentColor" text-anchor="middle" font-size="12" opacity=".8">
    <text x="150" y="240">c0</text><text x="230" y="240">c1</text><text x="310" y="240">c2</text><text x="390" y="240">c3</text>
    <text x="470" y="240">c4</text><text x="550" y="240">c5</text><text x="630" y="240">c6</text>
  </g>
  <text x="10" y="276" fill="currentColor">Flash 읽기 = 주소 1 + 데이터 (1 + 3) → D0 은 c4 끝에 도착. 그 사이 다음 주소 A1 은 버스에 계속 떠 있다.</text>
  <text x="10" y="298" fill="currentColor">SRAM 은 0 WS 라 c5, c6 에 한 개씩. 총 1 + 4 + 1 + 1 = 7 사이클 (비파이프라인이면 5 + 2 + 2 = 9).</text>
  <text x="10" y="320" fill="currentColor" font-size="12">데이터는 HREADY=1 인 사이클 끝(다음 상승 엣지)에 잡힌다. 그림은 그 사이클 칸에 D 를 표시.</text>
</svg>
```

체크 테스트는 전부 결정적이다(사이클 수·파형 값·공식). 성능 "측정" 은 없고, 모델을 돌린 표는 `demo_bus.c` 가 찍는다. 주소 → 영역 결정과 bus fault 는 ARCH-B11, HTRANS/HBURST·APB 브리지의 세부는 ARCH-M13, AXI 의 VALID/READY 와 outstanding 전송은 ARCH-M14, 여러 마스터 중재는 ARCH-A10 이 맡는다. 여기서는 **WS 가 처리량을 얼마나 깎는가** 만 판다.

### Part A — 사이클 단위 버스 모델과 대역폭

메모리 맵 표(주어짐): `FLASH 0x0800_0000` 3 WS, `SRAM 0x2000_0000` 0 WS, `PERIPH 0x4000_0000` 1 WS (STM32 스타일 주소, WS 는 모델 값).

- `bus_run(mode, xfers, n, wave, cap)` — 전송 배열을 **한 사이클씩** 진행해 총 사이클을 돌려주고, 사이클마다 `{addr_owner, data_owner, hready}` 를 기록한다.
  - `BUS_NONPIPE`: 주소 단계 → 데이터 단계(1+WS) → 끝나야 다음 주소.
  - `BUS_PIPE`: 다음 전송의 주소 단계가 현재 데이터 단계와 겹친다. wait 동안 다음 주소는 붙잡혀(held) 있다.
  - 미매핑 주소가 있으면 -1.
- `total_bytes`, `bw_bytes_per_cycle`, `bw_mb_per_s(bytes, cycles, f_hz)` (MB = 10⁶ B).
- 테스트: 단일 전송 파형(HREADY = 1 0 0 0 1), Flash→SRAM 혼합 파형, 256 워드 읽기의 사이클이 공식과 같은지, 120 MHz 에서 MB/s.

### Part B — 클럭과 WS: 면접관이 찌르는 곳

- `ws_for_clock(t_acc_ps, f_hz)` — 접근 시간을 덮는 사이클 = ⌈t_acc × f⌉, WS = 사이클 − 1 (최소 0). **부동소수점 금지**: 30 ns × 100 MHz 는 *정확히* 3 사이클 → WS 2 여야 한다.
- `max_clock_for_ws(t_acc_ps, ws)` — 그 WS 로 버티는 최대 클럭(Hz). +1 Hz 면 WS+1 이 되는지 역함수 테스트.
- `flash_word_mbps(t_acc_ps, f_hz)` — 캐시·프리페치 없이 32비트 워드를 연속으로 읽을 때 MB/s. 30 → 31 MHz 에서 120 → 62 MB/s 로 **반토막** 나는 톱니를 테스트로 보인다.
- t_acc ≈ 33.3 ns 를 넣으면 STM32F4(2.7–3.6 V) 의 "30 MHz 마다 1 WS, 168 MHz 에서 5 WS" 표가 재현되는지 확인.
- 바이트 전송(64 × 1 B)과 워드 전송(16 × 4 B)의 B/cycle, 파이프라인 이득이 WS 에 따라 줄어드는 것(0 WS ≈ 2배, 3 WS ≈ 1.25배).

### Part C — 플래시 명령 페치: 라인 버퍼와 프리페치

CPU 는 4 B 페치를 하나씩 낸다. 플래시는 한 번 읽을 때 **128비트(16 B) 라인** 을 L = WS+1 사이클에 가져온다.

- `fetch_run(mode, ws, addrs, n)` — 세 모드 비교:
  - `FETCH_NOBUF` — 모든 페치가 플래시 읽기 (L 사이클).
  - `FETCH_LINEBUF` — 마지막 라인을 보관. 같은 라인이면 1 사이클.
  - `FETCH_PREFETCH` — 라인 버퍼 + 다음 순차 라인 1개를 미리 읽음. 분기로 다른 라인에 가면 프리페치를 버리고(flush), **진행 중인 플래시 읽기는 끊지 못한다** 고 가정.
- 트레이스(주어짐): 순차, 8 워드 루프, 매번 먼 라인으로 점프, 분기 많은 코드(xorshift 고정 시드).
- 테스트: 순차 코드의 닫힌 식 `L + (n/4 − 1)·max(4, L) + 3`, 168 MHz/5 WS 에서 1.5 cycle/fetch, 루프와 먼 점프의 정확한 사이클, 분기 트레이스의 불변식.

## 2. Problem (English)

> **Bus wait states and throughput.**
>
> 1. Write a cycle-accurate model of a single-master synchronous bus. Each transfer has a 1-cycle address phase and a data phase of `1 + WS` cycles, where `WS` depends on the target region (Flash 3, SRAM 0, peripheral 1). Support a non-pipelined mode (next address only after the data phase completes: `Σ(2+WS)` cycles) and a pipelined AHB-like mode (next address phase overlaps the current data phase and is held during wait states: `1 + Σ(1+WS)` cycles). Record per-cycle `addr_owner`, `data_owner`, `hready`. Compute bandwidth in bytes/cycle and MB/s.
> 2. Given a memory access time in picoseconds and a clock in Hz, compute the required wait states using integer arithmetic only, and the maximum clock for a given WS. Show that raising the clock from 30 MHz to 31 MHz halves uncached flash bandwidth when `t_acc = 33.333 ns`.
> 3. Model instruction fetch from a flash with 16-byte lines: no buffer, a one-line buffer, and a line buffer with a one-line sequential prefetcher (prefetch is flushed on a non-sequential fetch; an in-flight flash read cannot be aborted). Compare cycles per fetch for sequential, loop, far-jump and branchy traces.
>
> Example: 256 word reads at 120 MHz — SRAM pipelined: 257 cycles, 478.1 MB/s; Flash (3 WS) pipelined: 1025 cycles, 119.9 MB/s. Sequential fetch of 1024 words at 3 WS: 4096 / 1792 / 1027 cycles (no buffer / line buffer / prefetch).

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
```

실제로 돌린 결과 (처음엔 대부분 FAIL, exit 1):

```text
FAIL starter.c:150  bus_run(BUS_PIPE, g_x, 1, g_w, 64) == 2
FAIL starter.c:151  g_w[0].addr_owner == 0 && g_w[0].data_owner == -1 && g_w[0].hready == 1
...
FAIL starter.c:264  b.cycles == 200 * 4 && c.cycles == 2 * 4 * 200 - 4
FAIL starter.c:280  ok
2/46 passed
```

TODO 는 8곳: `bus_run`, `total_bytes`, `bw_bytes_per_cycle`, `bw_mb_per_s` (Part A), `ws_for_clock`, `max_clock_for_ws`, `flash_word_mbps` (Part B), `fetch_run` (Part C). 메모리 맵·트레이스 생성기·하네스는 주어진다.

## 4. 힌트

<details>
<summary>힌트 1 — 사이클 루프의 상태는 세 개면 된다</summary>

`next`(아직 주소 단계를 못 끝낸 첫 전송), `d`(데이터 단계에 있는 전송, 없으면 -1), `rem`(그 전송에 남은 wait). 매 사이클: ① 주소 버스에 누가 있나 결정 (PIPE 면 `next`, NONPIPE 면 `d < 0` 일 때만) ② 데이터 단계가 있으면 `rem > 0` 이면 HREADY=0·rem--, 아니면 완료 ③ **HREADY=1 인 사이클 끝에** 주소 단계가 받아들여져 `d = next`, `rem = WS`.

</details>

<details>
<summary>힌트 2 — 올림 나눗셈</summary>

`cycles = ceil(t_acc_ps × f_hz / 10¹²)` 는 `(t_acc_ps * f_hz + 10¹² − 1) / 10¹²`. 곱은 33333 × 2×10⁸ ≈ 7×10¹² 정도라 `uint64_t` 에 넉넉하다. `double` 로 `ceil(30e-9 * 100e6)` 을 하면 30e-9 가 2진수로 정확히 표현되지 않아 경계에서 틀릴 수 있다.

</details>

<details>
<summary>힌트 3 — max_clock_for_ws 는 부등식 뒤집기</summary>

`(WS+1) ≥ t_acc × f` ⇔ `f ≤ (WS+1) / t_acc`. 정수로 `(WS+1) × 10¹² / t_acc_ps` 내림. 그 값에서는 WS, +1 Hz 에서는 WS+1 이 나와야 한다.

</details>

<details>
<summary>힌트 4 — 프리페치 모델은 "시간 세 개"</summary>

`t`(CPU 가 지금까지 쓴 사이클), `flash_free`(플래시 배열이 비는 시각), `pf_ready`(프리페치 라인이 준비되는 시각). 같은 라인 → `t += 1`. 프리페치 라인 → `t = max(t+1, pf_ready)`, 그리고 다음 라인 프리페치를 `max(flash_free, 요청 시각)` 에 시작. 그 밖 → `t = max(t, flash_free) + L`(진행 중인 읽기를 못 끊으니 기다림), 끝나는 시각에 다음 라인 프리페치 시작.

</details>

## 5. 풀이

### Part A — 한 사이클씩 진행하기

| 모드 | 전송 하나 | n 개 연속 | 0 WS, 256 워드 | 3 WS, 256 워드 |
|---|---|---|---|---|
| 비파이프라인 (APB 스타일) | 주소 1 + 데이터 1 + WS | `Σ(2+WS)` | 512 | 1280 |
| 파이프라인 (AHB 스타일) | 같음 (지연은 2+WS) | `1 + Σ(1+WS)` | 257 | 1025 |

핵심은 **지연(latency)과 처리량(throughput)이 다르다** 는 것. 파이프라인이어도 전송 하나의 지연은 여전히 `2 + WS` 사이클(주소 1 + 데이터 1+WS)이다. 다만 다음 주소가 미리 나가 있으니 연속 전송에서는 사이클당 하나씩(0 WS) 끝난다.

사이클 루프(정답 코드 그대로):

```c
while (next < n || d >= 0) {
    cycle_t cy = { -1, d, 1 };
    if (mode == BUS_PIPE || d < 0) if (next < n) cy.addr_owner = (int)next;
    if (d >= 0) { if (rem > 0) { cy.hready = 0; rem--; } else d = -1; }
    if (cy.addr_owner >= 0 && cy.hready) { d = (int)next; rem = ws_of(x[next].addr); next++; }
    ...
}
```

`demo_bus.c wave` 로 찍은 실제 파형 (그림 1 과 같은 상황):

```text
[pipelined / AHB 스타일]  (총 7 사이클)
  cycle  |   0  |   1  |   2  |   3  |   4  |   5  |   6  |
  HADDR  |  A0  |  A1  |  A1  |  A1  |  A1  |  A2  |  .   |
  DATA   |  .   |  D0  |  D0  |  D0  |  D0  |  D1  |  D2  |
  HREADY |  1   |  0   |  0   |  0   |  1   |  1   |  1   |

[non-pipelined / APB 스타일]  (총 9 사이클)
  cycle  |   0  |   1  |   2  |   3  |   4  |   5  |   6  |   7  |   8  |
  HADDR  |  A0  |  .   |  .   |  .   |  .   |  A1  |  .   |  A2  |  .   |
  DATA   |  .   |  D0  |  D0  |  D0  |  D0  |  .   |  D1  |  .   |  D2  |
  HREADY |  1   |  0   |  0   |  0   |  1   |  1   |  1   |  1   |  1   |
```

- 파이프라인 c1–c4: 주소 버스에 A1 이 4 사이클 동안 떠 있다. AHB 규칙 그대로 — HREADY 가 0 인 동안 마스터는 다음 주소를 바꾸면 안 되고, HREADY=1 인 사이클 끝에 비로소 "받아들여진다".
- 비파이프라인은 실제 APB 처럼 "setup(주소) → access(데이터)" 2 사이클이 최소다. 그래서 APB 주변장치 레지스터를 연속으로 읽는 루프는 AHB SRAM 보다 2배 이상 느리다 (브리지 지연은 ARCH-M13).

대역폭 (`demo_bus.c bw`, 120 MHz, 1024 B):

```text
region  bus       cycles   B/cycle      MB/s
SRAM    pipe         257     3.984     478.1
SRAM    nonpipe      512     2.000     240.0
FLASH   pipe        1025     0.999     119.9
FLASH   nonpipe     1280     0.800      96.0
```

`MB/s = 바이트 / 사이클 × f / 10⁶`. 버스 폭 32비트 × 120 MHz = 이론 최대 480 MB/s, 파이프라인 SRAM 은 첫 주소 단계 1 사이클 빼고 거의 다 쓴다. 3 WS 플래시는 사이클당 1 바이트 = 최대치의 1/4.

### Part B — 고정된 ns, 바뀌는 사이클

**WS 공식**: 접근 시간 `t_acc` 를 덮으려면 `(1 + WS) × T_clk ≥ t_acc` → `WS = ⌈t_acc × f⌉ − 1`. 정수로:

```c
uint64_t cyc = (t_acc_ps * f_hz + PS_PER_S - 1) / PS_PER_S;   /* PS_PER_S = 10^12 */
return cyc > 1 ? (unsigned)(cyc - 1) : 0u;
```

| t_acc | f | t_acc × f | 사이클 | WS | 함정 |
|---|---|---|---|---|---|
| 30.000 ns | 100 MHz | 정확히 3 | 3 | 2 | `double` 로 계산하면 식 모양에 따라 3.0000000000000004 → WS 3 (틀림) |
| 30.001 ns | 100 MHz | 3.0001 | 4 | 3 | 1 ps 차이로 WS 하나 |
| 30 ns | 1 MHz | 0.03 | 1 | 0 | `cyc − 1` 이 음수가 되지 않게 |

`demo_fp_ceil.c` 로 실제 확인한 것 (같은 30 ns × 100 MHz 를 두 가지로 썼을 뿐):

```text
t_ns*1e-9*f = 3.0000000000000004  ceil-1 = 3
30e-9*f     = 2.9999999999999996  ceil-1 = 2
```

두 번째가 맞은 것도 운이다 (2.9999… 를 내림하는 코드였다면 반대로 틀린다). 정수 ps × Hz 는 이런 일이 없다.

**STM32F4 표 재현**: RM0090 의 "HCLK 에 따른 WS" 표(전원 2.7–3.6 V)는 0 WS ≤ 30 MHz, 1 WS ≤ 60, 2 WS ≤ 90, 3 WS ≤ 120, 4 WS ≤ 150, 5 WS ≤ 168 MHz 처럼 30 MHz 마다 한 칸이다. 접근 시간을 33.333 ns 로 놓으면 테스트의 12개 점(24, 30, 31, …, 151, 168 MHz)이 모두 맞는다. 이 숫자는 **표를 재현하는 모델 값** 이지 ST 가 공개한 셀 접근 시간이 아니다. 전압 범위가 낮으면 같은 클럭에 WS 가 더 필요하다 (표가 전압별로 따로 있다).

**톱니(sawtooth)**: 클럭을 올리면 사이클은 짧아지지만 WS 가 계단처럼 늘어서, 캐시 없는 플래시 읽기의 실제 시간은 톱니 모양이 된다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">캐시·프리페치 없는 플래시 워드 읽기 MB/s vs 클럭 (t_acc = 33.333 ns)</text>
  <g stroke="currentColor" fill="none"><path d="M70 250 H710"/><path d="M70 250 V40"/></g>
  <g fill="currentColor" font-size="12" text-anchor="end">
    <text x="62" y="254">0</text><text x="62" y="158">60</text><text x="62" y="110">90</text><text x="62" y="62">120</text>
  </g>
  <g stroke="currentColor" opacity=".25"><path d="M70 154 H710"/><path d="M70 106 H710"/><path d="M70 58 H710"/></g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="170" y="270">30</text><text x="270" y="270">60</text><text x="370" y="270">90</text><text x="470" y="270">120</text>
    <text x="570" y="270">150</text><text x="630" y="270">168</text><text x="670" y="270">180</text><text x="700" y="290">MHz</text>
  </g>
  <path d="M70 250 L170 58 L170 154 L270 58 L270 122 L370 58 L370 106 L470 58 L470 96 L570 58 L570 90 L670 58" stroke="currentColor" stroke-width="2" fill="none"/>
  <circle cx="173" cy="151" r="5" class="f-warn" stroke="currentColor"/>
  <text x="182" y="176" fill="currentColor" font-size="12">31 MHz: 1 WS → 62 MB/s</text>
  <circle cx="630" cy="71" r="5" class="f-acc" stroke="currentColor"/>
  <text x="560" y="112" fill="currentColor" font-size="12">168 MHz: 5 WS → 112</text>
  <text x="80" y="48" fill="currentColor" font-size="12">꼭짓점 = 4 B × f / (WS+1) 가 120 MB/s 에 닿는 곳 (30, 60, 90 … MHz)</text>
</svg>
```

`demo_bus.c clock` 실제 출력 (일부):

```text
  MHz  WS   ns/word  MB/s(no buf) Mfetch/s(prefetch)
   30   0     33.33         120.0             30.0
   31   1     64.52          62.0             31.0
  120   3     33.33         120.0            119.6
  121   4     41.32          96.8             96.6
  150   4     33.33         120.0            119.7
  168   5     35.71         112.0            111.8
  180   5     33.33         120.0            119.8
```

두 가지가 보인다. ① 캐시 없는 플래시 대역폭은 클럭과 거의 상관없이 **4 B / 33.3 ns = 120 MB/s 가 천장** 이다 — 셀 속도가 병목. ② 오른쪽 열(순차 코드 + 128비트 라인 + 프리페치)은 L ≤ 4 (WS ≤ 3) 이면 클럭과 같은 속도로 페치하지만 121 MHz 에서 L=5 가 되는 순간 96.6 으로 떨어진다. 라인 4 워드를 다 먹는 동안(4 사이클) 다음 라인이 준비돼야 하기 때문이다 (Part C).

**바이트 전송과 파이프라인 이득**:

| 측정 | 사이클 | B/cycle |
|---|---|---|
| SRAM 64 × 1 B (파이프) | 65 | 0.98 |
| SRAM 16 × 4 B (파이프) | 17 | 3.76 |
| 이득 비파이프/파이프, 0 WS, 256 워드 | 512 / 257 | ≈ 1.99 |
| 이득 비파이프/파이프, 3 WS, 256 워드 | 1280 / 1025 | ≈ 1.249 |

버스 폭을 못 채우는 바이트 루프(`memcpy` 를 `uint8_t` 로 짠 것)는 같은 사이클에 1/4 만 옮긴다. 파이프라인은 전송마다 정확히 1 사이클(주소 단계)을 숨길 뿐이라, WS 가 크면 그 1 사이클의 비중이 작아진다 — **느린 슬레이브는 파이프라인으로 못 살린다.**

### Part C — 라인 버퍼와 프리페치

내장 플래시는 한 번 읽을 때 넓은 라인을 가져온다 (STM32F4 는 128비트 폭으로 읽는다고 RM0090 에 나온다). 4 B 페치 4개가 한 라인이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">순차 코드 + 프리페치: CPU 가 라인 k 를 먹는 동안 플래시가 k+1 을 읽는다</text>
  <text x="10" y="44" fill="currentColor" font-size="12">한 칸 = 1 사이클. 위: L = 4 (3 WS) 완전히 가려짐 / 아래: L = 6 (5 WS) 라인마다 2 사이클 스톨</text>
  <g fill="currentColor" font-size="12"><text x="10" y="82">CPU</text><text x="10" y="112">Flash</text><text x="10" y="192">CPU</text><text x="10" y="222">Flash</text></g>
  <g stroke="currentColor">
    <rect x="70" y="66" width="30" height="24" class="f-ok"/><rect x="100" y="66" width="30" height="24" class="f-ok"/><rect x="130" y="66" width="30" height="24" class="f-ok"/><rect x="160" y="66" width="30" height="24" class="f-ok"/>
    <rect x="190" y="66" width="30" height="24" class="f-acc"/><rect x="220" y="66" width="30" height="24" class="f-acc"/><rect x="250" y="66" width="30" height="24" class="f-acc"/><rect x="280" y="66" width="30" height="24" class="f-acc"/>
    <rect x="310" y="66" width="30" height="24" class="f-ok"/><rect x="340" y="66" width="30" height="24" class="f-ok"/><rect x="370" y="66" width="30" height="24" class="f-ok"/><rect x="400" y="66" width="30" height="24" class="f-ok"/>
    <rect x="70" y="96" width="120" height="24" class="f-mut"/><rect x="190" y="96" width="120" height="24" class="f-mut"/><rect x="310" y="96" width="120" height="24" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="130" y="83">라인 k</text><text x="250" y="83">라인 k+1</text><text x="370" y="83">라인 k+2</text>
    <text x="130" y="113">read k+1</text><text x="250" y="113">read k+2</text><text x="370" y="113">read k+3</text>
  </g>
  <text x="480" y="83" fill="currentColor" font-size="12">1 fetch / cycle</text>
  <text x="480" y="113" fill="currentColor" font-size="12">L = 4 ≤ 4 워드 → 스톨 0</text>
  <g stroke="currentColor">
    <rect x="70" y="176" width="30" height="24" class="f-ok"/><rect x="100" y="176" width="30" height="24" class="f-ok"/><rect x="130" y="176" width="30" height="24" class="f-ok"/><rect x="160" y="176" width="30" height="24" class="f-ok"/>
    <rect x="190" y="176" width="60" height="24" class="f-warn"/>
    <rect x="250" y="176" width="30" height="24" class="f-acc"/><rect x="280" y="176" width="30" height="24" class="f-acc"/><rect x="310" y="176" width="30" height="24" class="f-acc"/><rect x="340" y="176" width="30" height="24" class="f-acc"/>
    <rect x="370" y="176" width="60" height="24" class="f-warn"/>
    <rect x="100" y="206" width="180" height="24" class="f-mut"/><rect x="280" y="206" width="180" height="24" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="130" y="193">라인 k</text><text x="220" y="193">stall</text><text x="310" y="193">라인 k+1</text><text x="400" y="193">stall</text>
    <text x="190" y="223">read k+1 (6)</text><text x="370" y="223">read k+2 (6)</text>
  </g>
  <text x="480" y="193" fill="currentColor" font-size="12">4 fetch / 6 cycle = 1.5 cycle/fetch</text>
  <text x="480" y="223" fill="currentColor" font-size="12">168 MHz F4 의 상황 → ART 필요</text>
  <text x="70" y="246" fill="currentColor" font-size="12" opacity=".8">(정상 상태. 아래 줄처럼 CPU 가 기다리는 중이면 첫 워드는 읽기의 마지막 사이클에 바로 전달된다)</text>
  <text x="10" y="262" fill="currentColor">정상 상태: 라인 하나에 max(워드 수, L) 사이클. 프리페치가 "가리는" 조건은 L ≤ 라인당 워드 수.</text>
  <text x="10" y="286" fill="currentColor">분기로 다른 라인에 가면 미리 읽은 라인은 버려지고(flush) 새로 L 사이클 — 루프에는 캐시가 필요하다.</text>
</svg>
```

모델 규칙을 표로 정리하면:

| 요청한 라인 | NOBUF | LINEBUF | PREFETCH |
|---|---|---|---|
| 지금 라인 | L | 1 | 1 |
| 프리페치된 라인 (k+1) | L | L | `max(t+1, pf_ready) − t` (준비됐으면 1) → 그 순간 k+2 읽기 시작 |
| 그 밖 (분기) | L | L | 프리페치 버림, 플래시가 빌 때까지 기다린 뒤 L → 끝나는 순간 다음 라인 프리페치 |

닫힌 식(테스트가 ws 0–7 전부 확인, n = 1024 순차):

- NOBUF `n·L`, LINEBUF `(n/4)(L+3)`, PREFETCH `L + (n/4 − 1)·max(4, L) + 3`.
- 3 WS: 4096 / 1792 / 1027 사이클 → 4.0 / 1.75 / 1.003 cycle/fetch.

`demo_bus.c fetch` 실제 출력 (cycle/fetch, n = 4096):

```text
trace     WS     NOBUF   LINEBUF  PREFETCH   PREFETCH 상세
seq        3      4.00      1.75      1.00   hit 3072 pf 1023 miss 1 stall 3
seq        5      6.00      2.25      1.50   hit 3072 pf 1023 miss 1 stall 2051
loop8      3      4.00      1.75      1.50   hit 3072 pf 512 miss 512 stall 2047
loop8      5      6.00      2.25      2.25   hit 3072 pf 512 miss 512 stall 5117
far        3      4.00      4.00      8.00   hit 0 pf 0 miss 4096 stall 28668
branchy    0      1.00      1.00      1.03   hit 2380 pf 793 miss 923 stall 142
branchy    3      4.00      2.26      2.58   hit 2380 pf 793 miss 923 stall 6456
branchy    5      6.00      3.09      3.86   hit 2380 pf 793 miss 923 stall 11732
```

읽는 법:

1. **순차(seq)**: 프리페치가 WS 를 완전히 가린다 (WS ≤ 3). 5 WS 에서는 1.5 — 그림의 아래 줄.
2. **루프(loop8, 2 라인)**: 뒤로 가는 분기마다 라인 k 가 미스. 게다가 라인 k+1 을 먹는 동안 시작한 **쓸모없는 k+2 프리페치** 가 플래시를 붙잡는다. 3 WS 에서 한 바퀴 12 사이클(테스트: 100 바퀴 = 1199), 라인 버퍼만이면 14. 5 WS 에서는 라인 버퍼와 똑같다(2.25).
3. **먼 점프(far)**: 매번 다른 라인 → 프리페치는 100% 쓸모없고, 끊을 수 없다는 가정 때문에 **2배 손해** (`2Ln − L`).
4. **분기 많은 코드(branchy)**: 같은 이유로 3 WS 에서 2.58 > 2.26. 이 결과는 "진행 중 읽기를 못 끊는다" 는 **모델 가정** 에서 나온다. 끊을 수 있게 하면 항상 라인 버퍼 이하가 된다 — 추가 연습 1 에서 확인.

결론: 순차 프리페치는 **직선 코드** 용이다. 루프와 분기에는 **캐시** 가 필요하다. 그래서 STM32F4 는 "ART accelerator" 로 프리페치 + 128비트 명령 캐시 64 라인 + 데이터 캐시 8 라인을 붙였다(RM0090). 작은 캐시가 루프를 어떻게 살리는지는 추가 연습 2, 캐시 자체의 설계는 ARCH-M04/M05, 히트율로 평균 지연을 구하는 식은 ARCH-B10.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
# 46/46 passed   (exit 0)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san
# 46/46 passed   (ASan/UBSan 경고 없음)
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bus.c -o demo && ./demo wave   # bw | clock | fetch | (없으면 전부)
cc -std=c11 -Wall -Wextra -Werror -O2 demo_fp_ceil.c -o fp && ./fp       # double 로 WS 계산하면 틀리는 경계
```

`demo_bus.c` 는 `#define main solution_main` 후 `solution.c` 를 include 해서 **정답 모델 그대로** 표를 찍는다 (체크 없음).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `ws = ceil(t_acc_ns * f_mhz / 1000.0) - 1` | 30 × 100 / 1000 같은 "딱 맞는" 경계에서 부동소수 오차로 WS 하나 더 (또는 덜) | ps × Hz 정수 곱, `(a + D − 1) / D` 올림 |
| `ws = t_acc * f` (내림) | 2.4 사이클이 필요한데 2 사이클 → WS 1 → **셀이 준비되기 전에 데이터를 잡음** = 엉뚱한 명령 실행 | 반드시 올림 |
| 지연 = 처리량으로 착각: "파이프라인이니 SRAM 읽기 1 사이클" | 전송 하나의 지연은 여전히 주소 1 + 데이터 1. 1 사이클은 연속 전송의 **간격** | latency `2+WS`, throughput `1/(1+WS)` 를 따로 말한다 |
| wait 동안 다음 주소를 바꾸는 마스터 모델 | AHB 에서 HREADY=0 인 동안 주소 단계는 아직 안 끝났다. 바꾸면 슬레이브가 엉뚱한 주소를 본다 | 주소는 HREADY=1 인 사이클 끝에만 "받아들여짐" |
| 클럭 먼저 올리고 LATENCY 나중에 | 그 사이 몇 사이클 동안 WS 부족 → 잘못된 명령 페치 → HardFault 또는 랜덤 동작 | 올릴 땐 LATENCY 먼저(되읽어 확인) → 클럭, 내릴 땐 반대 (추가 연습 5) |
| "프리페치 켜면 항상 빨라진다" | 분기 많은 코드에서 버려지는 프리페치가 플래시를 붙잡을 수 있다 (모델: far 2배). 전력도 더 쓴다 | 트레이스로 측정, 루프는 캐시(ART) 로 |
| B/cycle 계산에서 바이트 전송을 4 B 로 셈 | `uint8_t` 루프는 같은 사이클에 1 B 만 옮긴다 | `size` 필드 합으로 바이트를 센다 |

## 8. 면접 Q&A

<details>
<summary>Q1. Wait state 가 무엇이고 누가 넣나요? 하드웨어에서는 어떻게 보이나요?</summary>

슬레이브가 데이터 단계를 끝낼 준비가 안 됐을 때 버스 사이클을 늘리는 것. AHB 에서는 슬레이브가 `HREADYOUT` 을 0 으로 내리고, 인터커넥트가 그것을 `HREADY` 로 모든 마스터·슬레이브에 돌린다. APB 는 `PREADY`(APB3 부터). 내장 플래시처럼 WS 가 레지스터(FLASH_ACR LATENCY)로 정해지는 경우는 플래시 인터페이스가 그 수만큼 HREADY 를 내리는 것.

꼬리질문: "로직 분석기로 볼 수 있나요?" → 내부 버스는 칩 밖으로 안 나온다. 외부 메모리 컨트롤러(FMC/FSMC)의 NWAIT 핀이나 주소/데이터 셋업·홀드 타이밍 레지스터가 같은 개념의 외부 버전이다. 실리콘 검증에서는 시뮬레이션 파형이나 DWT CYCCNT 로 루프를 Flash/SRAM 에서 각각 돌려 사이클 차이로 간접 측정한다.

</details>

<details>
<summary>Q2. STM32F4 를 168 MHz 로 돌리면 플래시가 5 WS 인데, 왜 성능이 6배 나빠지지 않나요?</summary>

플래시가 128비트(명령 4개 분량)를 한 번에 읽고, 프리페치로 다음 라인을 미리 읽으며, ART 의 명령 캐시가 루프와 분기 대상을 잡아준다. 순차 코드만 보면 L = 6 > 4 워드라 프리페치만으로는 1.5 cycle/fetch(본문 테스트)인데, 루프 본문은 캐시에 남아 1 cycle/fetch 에 가깝게 된다. ST 는 이를 "0 WS 와 동등한 성능" 이라고 홍보하지만, 캐시 미스(큰 코드, 첫 실행, 분기 많은 코드)에서는 WS 가 그대로 드러난다.

꼬리질문: "데이터 읽기(const 테이블)는?" → 데이터 캐시(F4 는 8 라인)가 따로 있고, 큰 룩업 테이블을 랜덤하게 읽으면 미스마다 WS 를 낸다. 그래서 자주 쓰는 테이블은 SRAM 으로 복사하고(F4 의 CCM RAM 은 D-bus 전용이라 데이터용, M7 이면 DTCM), 시간 결정적인 ISR 코드는 SRAM 이나 M7 의 ITCM 에서 실행한다.

</details>

<details>
<summary>Q3. 클럭을 올렸더니 오히려 느려졌다. 어떤 원인을 의심하나요?</summary>

① WS 경계를 넘었다 (30 → 31 MHz 처럼) — 캐시 없는 플래시 접근은 톱니로 손해. ② 순차 프리페치가 가리던 L 이 라인 폭을 넘었다 (120 → 121 MHz 에서 119.6 → 96.6 M fetch/s, demo 표). ③ APB 프리스케일러가 바뀌어 주변장치 버스가 상대적으로 느려졌다 (BM-B10). ④ 전압 스케일링·온도로 더 높은 WS 표가 적용. 측정은 같은 루프를 DWT CYCCNT 로 재고 "사이클 수" 와 "벽시계 시간" 을 따로 본다.

꼬리질문: "그럼 WS 를 일부러 높게 두면?" → 안전하지만 느리다. 낮게 두면(필요보다 작게) 데이터가 준비되기 전에 잡혀서 잘못된 명령을 실행한다 — 증상은 랜덤 HardFault 라 디버깅이 아주 어렵다.

</details>

<details>
<summary>Q4. 클럭 주파수를 바꿀 때 FLASH_ACR LATENCY 는 언제 바꾸나요?</summary>

올릴 때는 **먼저** LATENCY 를 새 값으로 쓰고, 레지스터를 되읽어 반영됐는지 확인한 뒤 클럭(PLL 전환·프리스케일러)을 올린다. 내릴 때는 클럭을 먼저 내리고 LATENCY 를 낮춘다. RM0090 이 이 순서를 명시한다. 어느 순간에도 "현재 클럭에 필요한 WS ≤ 설정된 WS" 를 지키는 것이 원칙이고, 추가 연습 5 가 잘못된 순서를 잡아낸다.

꼬리질문: "왜 되읽나요?" → 쓰기가 버스 쓰기 버퍼에 머물러 있을 수 있어서, 되읽기로 실제 반영을 보장한다 (쓰기 버퍼와 순서는 ARCH-A05, 배리어는 C-M01 참고).

</details>

<details>
<summary>Q5. AHB 의 "파이프라인" 은 무엇을 겹치나요? APB 는 왜 최소 2 사이클인가요?</summary>

AHB 는 전송 N+1 의 주소 단계를 전송 N 의 데이터 단계와 겹친다. 그래서 0 WS 연속 전송은 사이클당 하나지만, 전송 하나의 지연은 2 사이클이다. HREADY=0 이면 다음 주소는 그대로 붙잡혀 있다. APB 는 단순함·저전력을 위해 겹치지 않는다: setup 단계(PSEL=1, PENABLE=0) 1 사이클 + access 단계(PENABLE=1) 최소 1 사이클. 브리지가 AHB 전송을 APB 로 바꾸며 추가 지연이 생긴다 (ARCH-M13).

꼬리질문: "그럼 AXI 는?" → 채널이 분리돼 있고 주소를 여러 개 먼저 보내 둘 수 있다(outstanding). 느린 슬레이브의 지연을 "동시에 여러 개 진행" 으로 가린다 — 대역폭 = outstanding 수 × 바이트 / 지연 (Little 의 법칙). ARCH-M14.

</details>

<details>
<summary>Q6. 플래시에 쓰는(프로그램/지우기) 동안 코드가 멈추는 이유는요?</summary>

단일 뱅크 플래시는 프로그램·지우기 중에 같은 배열을 읽을 수 없다. STM32F4 RM0090 은 그 동안의 플래시 읽기가 **버스를 멈춘다(stall)** 고 적고 있다 — 거대한 wait state 다. 섹터 지우기는 수백 ms~수 초 단위라, 그 동안 플래시에서 도는 ISR 도 못 돈다. 해결: 프로그래밍 루틴과 그 사이 필요한 ISR 을 RAM 에서 실행, 듀얼 뱅크 칩이면 다른 뱅크에서 실행(read-while-write), 또는 외부 플래시 사용.

꼬리질문: "부트로더 OTA 에서 이게 왜 중요하죠?" → 워치독을 못 먹여 리셋될 수 있고(ARCH-B14 리셋 원인), 통신 ISR 이 멈춰 패킷을 잃는다.

</details>

<details>
<summary>Q7. 메모리 대역폭을 계산해 보세요: 32비트 AHB, 100 MHz, 2 WS 슬레이브, 연속 읽기.</summary>

파이프라인 정상 상태에서 전송 하나 = 1 + 2 = 3 사이클 → 4 B / 3 사이클 × 100 MHz ≈ 133 MB/s. 비파이프라인이면 4 사이클 → 100 MB/s. 0 WS 라면 400 MB/s 가 이론 최대. 꼬리질문: "버스트(INCR4)면 나아지나요?" → AHB 에서 버스트는 주소가 예측 가능하다는 정보를 줄 뿐 WS 를 없애지 않는다. 다만 메모리 쪽이 그 정보를 써서 다음 데이터를 미리 읽으면(SDRAM 페이지, 플래시 라인) 첫 비트만 느리고 이후는 빨라진다 — 프리페치와 같은 원리.

</details>

## 9. 추가 연습

### 연습 1 — 중단 가능한 프리페치

`fetch_run2` 에 `abortable` 을 추가해, 분기 미스가 진행 중인 프리페치를 버리고 바로 읽기를 시작하게 하라. 먼 점프 트레이스와 분기 트레이스(ws 0–7)에서 결과를 비교하라.

<details>
<summary>답 보기</summary>

미스 시작 시각을 `abortable ? t : max(t, flash_free)` 로 바꾸면 끝. 먼 점프 200 회 3 WS: 1596 → 800 사이클(라인 버퍼와 같음). 미스가 항상 L 이고 프리페치 히트는 L 이하이므로, 끊을 수 있으면 프리페치가 라인 버퍼보다 나빠질 수 없다 — 테스트가 분기 트레이스 ws 0–7 에서 `abortable ≤ LINEBUF` 와 `abortable ≤ non-abortable` 을 확인한다. 순차 트레이스는 프리페치가 버려지지 않으니 두 모델이 같다. 실제 플래시 컨트롤러가 진행 중 읽기를 끊는지는 칩마다 다르고 매뉴얼에 안 나오는 경우가 많다 — 측정으로 확인할 일.

</details>

### 연습 2 — ART 같은 작은 명령 캐시

16 B 라인, 완전 연관(fully associative), LRU 교체, 히트 1 사이클 / 미스 L 사이클인 `icache_run(ws, nlines, …)` 를 작성하라. 8 워드 루프(2 라인)와 40 워드 루프(10 라인)에서 줄 수 1, 4, 8, 16 을 비교하라.

<details>
<summary>답 보기</summary>

8 워드 루프 100 회, 3 WS: 4 줄이면 첫 바퀴만 미스 2번 → `2·(4+3) + 8·99 = 806` 사이클 (거의 1 cycle/fetch), 1 줄이면 라인 버퍼와 같은 1400, 5 WS 에서도 64 줄이면 `2·(6+3) + 8·99`. 40 워드(10 라인) 루프는 **8 줄 LRU 에서 히트 0** — 매 라인이 들어올 때 쫓겨난 줄이 바로 다음에 필요한 줄이다(LRU 의 순환 접근 병적 사례, 7000 사이클). 16 줄이면 첫 바퀴만 미스. "루프가 캐시보다 조금 크면 LRU 는 전멸" 은 ARCH-M05 의 교체 정책 비교로 이어진다.

</details>

### 연습 3 — 라인 폭 일반화와 최소 라인 폭

라인 폭 4/8/16/32/64 B, ws 0–9 에서 순차 프리페치 사이클이 `L + (n/wpl − 1)·max(wpl, L) + (wpl − 1)` 인지 시뮬레이션으로 확인하고, WS 를 완전히 가리는 최소 라인 폭(2의 거듭제곱)을 구하라.

<details>
<summary>답 보기</summary>

조건은 `wpl ≥ L`. 3 WS → 4 워드 = 16 B, 4–7 WS → 8 워드 = 32 B. 168 MHz/5 WS 에서 32 B 라인이면 4096 페치가 `6 + 4095` 사이클 = 첫 미스만 보인다. 즉 F4 가 128비트가 아니라 256비트로 읽었다면 순차 코드는 프리페치만으로 가려졌을 것이다 — 넓게 읽기는 면적·전력 비용이라 대신 캐시를 붙인 것.

</details>

### 연습 4 — 부트 .data 복사 시간

리셋 핸들러가 64 KB `.data` 를 Flash(3 WS) → SRAM(0 WS) 으로 워드 단위 복사한다. 파이프라인 버스에서 읽기·쓰기가 번갈아 나갈 때 사이클과 120 MHz 에서의 시간을 구하라 (프리페치·캐시는 데이터 경로에서 안 돕는다고 가정).

<details>
<summary>답 보기</summary>

`1 + N·((1+3) + (1+0))` = 1 + 16384 × 5 = 81921 사이클 → 120 MHz 에서 약 682.7 µs. 사이클 루프 시뮬레이터(`pipe_sim`)와 공식이 같은지 테스트한다. 원본이 SRAM 이면 `1 + 2N` = 32769 사이클(2.5배 빠름). 실제 리셋 핸들러는 루프 오버헤드(분기, 주소 증가)가 더해지고, DMA 나 `LDM/STM` 다중 전송으로 줄일 수 있다. 부팅 시간 예산(예: 리셋 후 수 ms 안에 CAN 응답)에서 이 숫자가 의미를 가진다.

</details>

### 연습 5 — 클럭 변경 순서 검사기

`SET_LATENCY` / `SET_CLOCK` 단계 목록을 실행하면서 각 단계 직후 "필요 WS ≤ 현재 LATENCY" 를 검사하는 `run_steps` 와, 올바른 순서를 만드는 `plan_clock_change` 를 작성하라. 16 MHz/0 WS ↔ 168 MHz/5 WS.

<details>
<summary>답 보기</summary>

올릴 때 `{LATENCY=5, CLOCK=168M}`, 내릴 때 `{CLOCK=16M, LATENCY=0}`. 반대 순서는 중간 상태가 위반이라 (올릴 때 클럭 먼저: 168 MHz 인데 LATENCY 0 / 내릴 때 LATENCY 먼저: 아직 168 MHz 인데 LATENCY 0) `run_steps` 가 false. 실제 코드는 LATENCY 를 쓴 뒤 `while ((FLASH->ACR & LATENCY_Msk) != want) {}` 로 되읽어 확인한다. HAL 의 `HAL_RCC_ClockConfig()` 도 같은 순서로 짜여 있다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
# 17/17 passed   (ASan/UBSan 빌드도 17/17, 경고 없음)
```

## 10. 핵심 정리

- 전송 = 주소 단계 1 + 데이터 단계 (1 + WS). 비파이프라인 `Σ(2+WS)`, 파이프라인 `1 + Σ(1+WS)`. 지연은 둘 다 `2+WS`.
- 대역폭 = 바이트 / 사이클 × f. 32비트 120 MHz: SRAM 0 WS ≈ 478 MB/s, Flash 3 WS ≈ 120 MB/s.
- WS = ⌈t_acc × f⌉ − 1 — **정수 올림**. 내림하면 잘못된 데이터를 잡고, 부동소수는 경계에서 틀린다.
- 플래시 셀은 ns 고정 → 캐시 없는 플래시 대역폭은 클럭과 무관하게 천장이 있고, WS 경계에서 톱니.
- 클럭 올릴 땐 LATENCY 먼저(되읽기) → 클럭, 내릴 땐 클럭 → LATENCY.
- 넓은 라인 + 순차 프리페치는 `L ≤ 라인당 워드 수` 일 때 직선 코드를 0 WS 처럼 만든다. 루프·분기는 못 살리고 해가 될 수도 → 명령 캐시(ART), RAM/TCM 실행.
- 느린 슬레이브는 파이프라인으로 못 살린다 — 넓게 읽기, 미리 읽기, 캐시, 동시에 여러 개(AXI outstanding).

## 참고자료

- [AMBA AHB Protocol Specification (Arm IHI 0033)](https://developer.arm.com/documentation/ihi0033/latest) — 주소/데이터 단계, HREADY, wait state
- [AMBA APB Protocol Specification (Arm IHI 0024)](https://developer.arm.com/documentation/ihi0024/latest) — setup/access 단계, PREADY
- [STM32F4 Reference Manual RM0090](https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) — Embedded Flash interface: WS 표, FLASH_ACR (LATENCY, PRFTEN, ICEN, DCEN), ART accelerator, 클럭 변경 순서
- [STM32F1 Reference Manual RM0008](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) — 프리페치 버퍼, 0/1/2 WS (24/48/72 MHz)
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* — I-Code/D-Code/System 버스, 플래시 WS 와 프리페치
- Harris & Harris, *Digital Design and Computer Architecture, ARM Edition* — 메모리 시스템과 버스
- 관련 레슨: ARCH-B10 (AMAT), ARCH-B11 (메모리 맵·bus fault), ARCH-B13 (setup/hold·Fmax), ARCH-M05 (교체 정책), ARCH-M13 (AHB/APB), ARCH-M14 (AXI), ARCH-A10 (버스 중재), BM-B10 (클럭 트리)
