---
id: BM-B10
time: 60분
updated: 2026-11-11
---
# BM-B10 클럭 트리 계산기: PLL M/N/P와 버스 프리스케일러

## 학습 목표

- STM32F4 메인 PLL 식 `SYSCLK = HSE / M × N / P`, `PLL48CLK = HSE / M × N / Q` 와 각 단계의 범위 제약(VCO 입력 1–2 MHz, VCO 출력 100–432 MHz, P ∈ {2,4,6,8}, Q 2–15)을 **제약 구조체**로 표현하고, 목표 SYSCLK 을 **정확히** 만드는 (M, N, P) 를 정수 연산만으로 전부 열거한다.
- 해가 여러 개일 때 무엇을 고를지(USB 48 MHz 정확 → VCO 입력 2 MHz → 낮은 VCO) 근거를 대고, "해가 없다" 를 약수 조건으로 증명할 수 있다 (25 MHz → 168 MHz 는 해가 1 개, 24.576 MHz → 168 MHz 는 0 개, 180 MHz 에서는 USB 48 MHz 불가).
- AHB/APB 분주를 고르고 **APB 분주 ≠ 1 이면 타이머 클럭 ×2** 규칙, **플래시 wait state 는 HCLK 기준** 규칙을 적용해 전체 클럭 계획을 만든다.
- 계획을 `RCC_PLLCFGR` / `RCC_CFGR` / `FLASH_ACR` 레지스터 값으로 인코딩하고(0x07402A04, 0x9402, 0x705), 반대로 레지스터 덤프를 읽어 주파수를 거꾸로 계산한다.
- float `==` 로 PLL 을 찾으면 해를 놓치고 틀린 해를 받아들인다는 것을 실측으로 보인다.

## 1. 문제

보드 bring-up 에서 처음 막히는 곳은 대개 클럭이다. "UART 글자가 깨져요", "타이머가 두 배 빨라요", "168 MHz 로 올리자마자 HardFault 가 나요" 는 거의 다 클럭 트리 계산 실수에서 온다. 그래서 펌웨어 면접에서는 "8 MHz 크리스털로 168 MHz 를 만들어 보세요. M, N, P, Q 는? APB1 은? 플래시 wait state 는?" 이 화이트보드 문제로 자주 나온다.

STM32CubeMX 의 "Clock Configuration" 탭이 해 주는 일을 직접 짜는 레슨이다. Don 이 SSD 컨트롤러나 RF 모듈 보드에서 크리스털 값을 바꿀 때(예: 8 MHz → 25 MHz, RF 쪽 26 MHz TCXO 를 같이 쓰는 경우) "같은 168 MHz 가 나오나, USB 는 되나" 를 숫자로 바로 답할 수 있어야 한다.

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">STM32F4 클럭 트리 — HSE 8 MHz → 168 MHz 계획 (solution.c 의 clock_plan 출력과 같은 값)</text>
  <rect x="10" y="40" width="95" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="18" y="58" fill="currentColor" font-weight="bold">HSE 8 MHz</text>
  <text x="18" y="76" fill="currentColor">4–26 MHz</text>
  <path d="M105 62 L130 62" stroke="currentColor"/><path d="M130 62 L124 58 L124 66 Z" fill="currentColor"/>
  <rect x="130" y="40" width="70" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="140" y="58" fill="currentColor" font-weight="bold">/M = 4</text>
  <text x="140" y="76" fill="currentColor">M 2..63</text>
  <path d="M200 62 L245 62" stroke="currentColor"/><path d="M245 62 L239 58 L239 66 Z" fill="currentColor"/>
  <text x="204" y="54" fill="currentColor">2 MHz</text>
  <text x="204" y="100" fill="currentColor">1–2 MHz</text>
  <rect x="245" y="40" width="125" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="255" y="58" fill="currentColor" font-weight="bold">VCO ×N = 168</text>
  <text x="255" y="76" fill="currentColor">= 336 MHz</text>
  <text x="275" y="100" fill="currentColor">100–432 MHz</text>
  <path d="M370 62 L395 62" stroke="currentColor"/><path d="M395 62 L389 58 L389 66 Z" fill="currentColor"/>
  <rect x="395" y="40" width="80" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="405" y="58" fill="currentColor" font-weight="bold">/P = 2</text>
  <text x="405" y="76" fill="currentColor">{2,4,6,8}</text>
  <path d="M475 62 L500 62" stroke="currentColor"/><path d="M500 62 L494 58 L494 66 Z" fill="currentColor"/>
  <rect x="500" y="40" width="110" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="510" y="58" fill="currentColor" font-weight="bold">SYSCLK 168</text>
  <text x="510" y="76" fill="currentColor">≤ 168 MHz</text>
  <path d="M610 62 L635 62" stroke="currentColor"/><path d="M635 62 L629 58 L629 66 Z" fill="currentColor"/>
  <rect x="635" y="40" width="115" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="645" y="58" fill="currentColor" font-weight="bold">AHB /1</text>
  <text x="645" y="76" fill="currentColor">HCLK 168 MHz</text>
  <path d="M307 84 L307 130" stroke="currentColor"/><path d="M307 130 L303 124 L311 124 Z" fill="currentColor"/>
  <rect x="245" y="130" width="125" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="255" y="148" fill="currentColor" font-weight="bold">/Q = 7 (2..15)</text>
  <text x="255" y="166" fill="currentColor">PLL48CLK 48 MHz</text>
  <text x="245" y="192" fill="currentColor">USB OTG FS = 정확히 48</text>
  <text x="245" y="208" fill="currentColor">SDIO · RNG ≤ 48</text>
  <path d="M692 84 L692 110 L540 110 L540 130" stroke="currentColor" fill="none"/>
  <path d="M692 110 L692 130" stroke="currentColor"/>
  <path d="M540 130 L536 124 L544 124 Z" fill="currentColor"/><path d="M692 130 L688 124 L696 124 Z" fill="currentColor"/>
  <rect x="470" y="130" width="140" height="44" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="480" y="148" fill="currentColor" font-weight="bold">APB1 /4</text>
  <text x="480" y="166" fill="currentColor">PCLK1 42 (≤ 42)</text>
  <rect x="625" y="130" width="125" height="44" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="635" y="148" fill="currentColor" font-weight="bold">APB2 /2</text>
  <text x="635" y="166" fill="currentColor">PCLK2 84 (≤ 84)</text>
  <text x="470" y="194" fill="currentColor">TIM2–7,12–14:</text>
  <text x="470" y="210" fill="currentColor">/4 ≠ 1 → ×2 = 84 MHz</text>
  <text x="625" y="194" fill="currentColor">TIM1,8–11:</text>
  <text x="625" y="210" fill="currentColor">/2 ≠ 1 → ×2 = 168</text>
  <rect x="10" y="232" width="740" height="88" rx="6" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="20" y="252" fill="currentColor">플래시: HCLK 168 MHz, 3.3 V → 30 MHz 한 칸 → LATENCY = ceil(168/30) − 1 = 5 WS</text>
  <text x="20" y="272" fill="currentColor" font-family="monospace">RCC_PLLCFGR = 0x07402A04   RCC_CFGR = 0x00009402 (SW=PLL)   FLASH_ACR = 0x00000705</text>
  <text x="20" y="292" fill="currentColor">ST 템플릿의 고전값 M=8, N=336 (VCO 입력 1 MHz) 도 해다 → PLLCFGR = 0x07405408.</text>
  <text x="20" y="310" fill="currentColor">이 레슨은 RM0090 의 "2 MHz 권장" 을 따라 M=4, N=168 을 고른다. (범위 값: RM0090 기준 대표값)</text>
</svg>
```

모든 제약은 코드 안에서 **하나의 구조체**(`clk_limits_t`)에 있다. 칩이 바뀌면(F405 → F429, 3.3 V → 1.8 V, 구형 데이터시트) 구조체만 바꾸고 알고리즘은 그대로 둔다. 테스트의 정답도 이 구조체다.

| 제약 (`F40X` 프리셋) | 값 | 비고 |
|---|---|---|
| HSE | 4–26 MHz | |
| VCO 입력 = HSE/M | 1–2 MHz | RM0090: jitter 를 줄이려면 2 MHz 권장 |
| M | 2–63 | 6비트 필드, 0·1 금지 |
| N | 50–432 | 9비트 필드. **구형 문서는 192–432** (`F40X_OLD`) |
| VCO 출력 | 100–432 MHz | 구형 문서 192–432 |
| P | {2, 4, 6, 8} | 2비트 코드 00/01/10/11 |
| Q | 2–15 | PLL48CLK: USB OTG FS 는 정확히 48 MHz, SDIO·RNG 는 48 MHz 이하 |
| SYSCLK(=HCLK 상한) | 168 MHz | F42x/43x over-drive 180 MHz (`F42X_OD`) |
| PCLK1 / PCLK2 | ≤ 42 / ≤ 84 MHz | F42x 45 / 90 |
| 플래시 WS | 30 MHz 한 칸 (2.7–3.6 V) | 2.4–2.7 V 24, 2.1–2.4 V 22, 1.8–2.1 V 20 MHz (`F40X_1V8`) |

> 위 값은 RM0090 (STM32F405/407/415/417, F42x/43x) 기준 대표값이다 — RM·데이터시트 확인 권장. 특히 N/VCO 하한은 문서 개정판마다 다르게 적혀 있다.

### Part A — 정확한 PLL 해 전부 열거하고 하나 고르기

- `uint32_t pll_q_for(L, vco_hz, bool *exact)` — VCO/Q 가 정확히 48 MHz 인 Q 가 범위 안에 있으면 그것(exact = true), 아니면 48 MHz 를 넘지 않는 가장 작은 Q.
- `int pll_enumerate(L, hse_hz, target_hz, pll_cfg_t *out, int cap)` — `SYSCLK == target` 을 **정확히** 만드는 (M, N, P) 를 전부 센다. 반환값은 총 개수(버퍼보다 많을 수 있음), out 에는 cap 개까지만. 오류는 음수(`CLK_ERR_HSE`, `CLK_ERR_SYSCLK`, `CLK_ERR_ARG`).
- `int pll_pick_best(v, n)` — 순위: ① USB 48 MHz 정확 ② VCO 입력이 큰 것(M 작은 것, 2 MHz 에 가까움) ③ VCO 가 낮은 것.
- 테스트 벡터: 8 MHz → 168 MHz 해 5 개, best = M4 N168 P2 Q7, 고전값 M8 N336 P2 Q7 포함. 25 MHz → 168 MHz 해 1 개 (M25 N336). 26 MHz → 168 MHz 해 2 개 (best M13 N168). 8 MHz → 84 MHz 해 10 개 중 P=4(USB 정확)가 이김. 구형 문서 제약에서는 M=4 탈락 → M5 N210.

### Part B — 경계, 오류, 플래시 wait state, property test

- 오류: HSE 3 / 27 MHz, 목표 0, F40x 에서 180 MHz (한계 초과), 12 MHz (VCO 최소 미만), 100 000 001 Hz (정확한 해 없음).
- 경계 포함 여부: VCO = 100 MHz 와 N = 50 (12.5 MHz), VCO = 432 MHz 와 N = 432 (108 MHz).
- 버퍼: cap = 2 로 불러도 총 개수 5 를 돌려주고 out[2] 는 건드리지 않음 (가드 비교).
- `int flash_wait_states(L, hclk_hz)` — WS = ceil(HCLK / step) − 1, LATENCY 최댓값 초과는 -1. 30 MHz → 0, 30 MHz + 1 Hz → 1, 168 MHz → 5, 1.8 V 에서 168 MHz → -1.
- F42x 에서 180 MHz: 해 5 개 전부 VCO 360 MHz → USB 48 MHz 불가 (Q = 8 → 45 MHz).
- **property test**: 고정 시드 xorshift32 로 (HSE, 목표) 400 개 — `pll_enumerate` 개수 == 레지스터로 표현 가능한 모든 (M 6비트, N 9비트, P 2비트) 를 무식하게 도는 `brute_count` 개수, 그리고 나온 해마다 독립 검사 `cfg_valid`.

### Part C — 전체 클럭 계획과 레지스터 값

- `int clock_plan(L, hse_hz, target_hclk_hz, clk_plan_t *pl)` — AHB 분주 {1,2,4,8,16,64,…,512}(**/32 는 없다**)를 작은 것부터 시도해 SYSCLK = HCLK × d 가 HSE 와 같으면 HSE 직결(PLL 끔), 아니면 PLL best. 그다음 `apb_div_for` 로 APB1/APB2 분주, 타이머 클럭(×2 규칙), WS, 그리고 `rcc_pllcfgr_word`, `rcc_cfgr_word`, `FLASH_ACR` 값.
- 테스트: 8 → 168 (0x07402A04 / 0x9402 / 0x705, TIM APB1 84 MHz, APB2 168 MHz), 8 → 84 (APB2 /1 이라 그 타이머는 ×1 → 84), 25 → 168 (0x07405419), 8 → 8 (HSE 직결), 8 → 1 (HSE + AHB /8 → CFGR 0xA1), 8 → 12 (PLL 은 12 MHz 를 못 만듦 → PLL 24 MHz + AHB /2), F42x 180, 1.8 V 에서 168 MHz → `CLK_ERR_FLASH`.

## 2. Problem (English)

> **Clock-tree calculator for an STM32F4-class MCU.**
>
> All limits live in a parameter struct `clk_limits_t` (HSE 4–26 MHz; VCO input HSE/M in [1, 2] MHz; M 2..63; N 50..432; VCO output 100..432 MHz; P ∈ {2,4,6,8}; Q 2..15 with PLL48CLK = 48 MHz for USB; SYSCLK ≤ 168 MHz; APB1 ≤ 42 MHz; APB2 ≤ 84 MHz; one flash wait state per 30 MHz of HCLK, LATENCY ≤ 7). Use integer arithmetic in Hz only — no floating-point equality.
>
> 1. `int pll_enumerate(const clk_limits_t *L, uint32_t hse_hz, uint32_t target_hz, pll_cfg_t *out, int cap)` — return the number of (M, N, P) with `hse/M*N/P == target` **exactly** that satisfy every limit, write at most `cap` of them, attach Q. Return a negative error for an out-of-range HSE, a target above SYSCLK max, or bad arguments. `int pll_pick_best(const pll_cfg_t *v, int n)` — prefer exact 48 MHz, then the highest VCO input (2 MHz recommended to limit jitter), then the lowest VCO.
> 2. `int flash_wait_states(const clk_limits_t *L, uint32_t hclk_hz)` — WS = ceil(HCLK/step) − 1, or -1 if it exceeds the LATENCY field. Validate the enumerator against a brute force over every register encoding.
> 3. `int clock_plan(const clk_limits_t *L, uint32_t hse_hz, uint32_t target_hclk_hz, clk_plan_t *pl)` — choose source (HSE direct or PLL), AHB/APB prescalers, timer clocks (×2 when the APB prescaler ≠ 1), wait states, and the `RCC_PLLCFGR` / `RCC_CFGR` / `FLASH_ACR` words.
>
> Examples: HSE 8 MHz → 168 MHz: 5 solutions, best M=4 N=168 P=2 Q=7, PLLCFGR = 0x07402A04, CFGR = 0x00009402, ACR = 0x705, PCLK1 = 42 MHz, APB1 timers = 84 MHz. HSE 25 MHz → 168 MHz: exactly one solution (M=25, N=336). HSE 8 MHz → 180 MHz: error on F40x; on F42x 5 solutions, none gives 48 MHz.
>
> Follow-ups: why is 2 MHz VCO input recommended? Why do APB timers run at 2×PCLK? In what order do you change FLASH latency and SYSCLK when speeding up — and when slowing down? How do you verify the clock on real hardware?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "7/69 passed" (오류·0 개 기대 테스트 몇 개는 스텁으로도 통과) — TODO 를 채워 69/69
./solution -v     # 정답 코드: property 통계, 클럭 계획 6 개, 해 목록 표 4 개를 출력
```

관찰용 파일 (하네스와 별개):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_float.c -o demo_float && ./demo_float   # float/double == 로 찾으면 무엇을 놓치나
```

## 4. 힌트

<details>
<summary>힌트 1 — "정확히" 를 나눗셈 없이 쓰기</summary>

`hse / m * n / p == target` 대신 양변에 M·P 를 곱한다: **`hse · N == target · M · P`**. 그러면 N 은 `target · M · P / hse` 가 **나누어떨어질 때만** 존재한다. M 과 P 를 고르면 N 이 결정되므로 N 을 루프 돌 필요가 없다 (M ≤ 62 가지 × P 4 가지 = 최대 248 번). 곱은 168e6 × 63 × 8 ≈ 8.5e10 이라 `uint64_t` 로 올려서 계산.

</details>

<details>
<summary>힌트 2 — 범위 검사도 곱셈으로</summary>

VCO 입력 `hse/M ∈ [1, 2] MHz` ⇔ `1 MHz · M ≤ hse ≤ 2 MHz · M`. VCO 출력은 정확한 해에서 `hse·N/M = target·P` 이므로 그냥 `target · P` 를 [100, 432] MHz 와 비교하면 된다. 양 끝은 **포함**이다 (`<` 와 `≤` 를 헷갈리면 12.5 MHz·108 MHz 테스트가 잡는다).

</details>

<details>
<summary>힌트 3 — Q 와 순위</summary>

USB 정확 ⇔ `VCO % 48 MHz == 0` 이고 그 몫이 2..15. 아니면 `ceil(VCO / 48 MHz)` (48 MHz 를 넘지 않는 가장 작은 Q). 순위는 비교 함수 하나로: usb_exact 가 다르면 true 쪽, M 이 다르면 작은 쪽, 아니면 VCO 낮은 쪽. M 이 같고 VCO 가 같으면 N·P 도 같다 — 같은 해다.

</details>

<details>
<summary>힌트 4 — WS 공식과 경계</summary>

표는 "0 < HCLK ≤ 30 → 0 WS, 30 < HCLK ≤ 60 → 1 WS …" 다. 정수로 `ceil(h/30) − 1 = (h − 1) / 30` (h ≥ 1). 30 000 000 Hz 는 0, 30 000 001 Hz 는 1. h = 0 은 따로 거른다 (`0 − 1u` 는 4 294 967 295).

</details>

<details>
<summary>힌트 5 — 레지스터 인코딩</summary>

PLLCFGR: `M | N << 6 | (P/2 − 1) << 16 | PLLSRC << 22 | Q << 24`. P 는 **값이 아니라 코드**다 (00 = /2). CFGR: `SW[1:0]` (01 HSE, 10 PLL), `HPRE[7:4]` (0 = /1, 1000 = /2, 1001 = /4, … 1011 = /16, 1100 = /64 … 1111 = /512), `PPRE1[12:10]`, `PPRE2[15:13]` (0 = /1, 100 = /2, 101 = /4, 110 = /8, 111 = /16). 시작 코드의 `hpre_code` / `ppre_code` 가 이 표다.

</details>

## 5. 풀이

### Part A — 정확한 해의 조건: 약수 문제다

식을 정리하면 `SYSCLK = HSE · N / (M · P)`. 목표 T 를 정확히 만들려면 `N = T · M · P / HSE` 가 **정수**여야 한다. 그래서 M 과 P 만 돌리면 된다.

8 MHz → 168 MHz 를 손으로 따라가 보자 (P 는 2, 4, 6, 8 순서):

| M | VCO 입력 | 입력 검사 | P | N = 168·M·P/8 | N 검사 | VCO = 168·P | VCO 검사 | 결과 |
|---|---|---|---|---|---|---|---|---|
| 2 | 4 MHz | > 2 MHz ✗ | | | | | | 건너뜀 |
| 3 | 2.67 MHz | > 2 MHz ✗ | | | | | | 건너뜀 |
| 4 | 2 MHz | ✓ (경계 포함) | 2 | 168 | ✓ | 336 | ✓ | **해** (Q = 336/48 = 7, 정확) |
| 4 | | | 4 | 336 | ✓ | 672 | > 432 ✗ | |
| 4 | | | 6 | 504 | > 432 ✗ | | | |
| 5 | 1.6 MHz | ✓ | 2 | 210 | ✓ | 336 | ✓ | 해 |
| 6 | 1.333 MHz | ✓ | 2 | 252 | ✓ | 336 | ✓ | 해 |
| 7 | 1.143 MHz | ✓ | 2 | 294 | ✓ | 336 | ✓ | 해 |
| 8 | 1 MHz | ✓ (경계) | 2 | 336 | ✓ | 336 | ✓ | 해 (고전값) |
| 9 | 0.889 MHz | < 1 MHz ✗ | | | | | | 이후 전부 건너뜀 |

`./solution -v` 실제 출력:

```text
[F40x 3.3V] HSE 8000000 Hz -> SYSCLK 168000000 Hz : 5 solution(s)
   M    N  P   Q  VCO_in(Hz)   VCO(MHz)  PLL48(Hz)  USB
   4  168  2   7    2000000        336   48000000  48 exact   <- best
   5  210  2   7    1600000        336   48000000  48 exact
   6  252  2   7    1333333        336   48000000  48 exact
   7  294  2   7    1142857        336   48000000  48 exact
   8  336  2   7    1000000        336   48000000  48 exact
[F40x old-DS] HSE 8000000 Hz -> SYSCLK 168000000 Hz : 4 solution(s)
   M    N  P   Q  VCO_in(Hz)   VCO(MHz)  PLL48(Hz)  USB
   5  210  2   7    1600000        336   48000000  48 exact   <- best
   6  252  2   7    1333333        336   48000000  48 exact
   7  294  2   7    1142857        336   48000000  48 exact
   8  336  2   7    1000000        336   48000000  48 exact
[F40x 3.3V] HSE 25000000 Hz -> SYSCLK 168000000 Hz : 1 solution(s)
   M    N  P   Q  VCO_in(Hz)   VCO(MHz)  PLL48(Hz)  USB
  25  336  2   7    1000000        336   48000000  48 exact   <- best
[F42x OD] HSE 8000000 Hz -> SYSCLK 180000000 Hz : 5 solution(s)
   M    N  P   Q  VCO_in(Hz)   VCO(MHz)  PLL48(Hz)  USB
   4  180  2   8    2000000        360   45000000  -   <- best
   5  225  2   8    1600000        360   45000000  -
   6  270  2   8    1333333        360   45000000  -
   7  315  2   8    1142857        360   45000000  -
   8  360  2   8    1000000        360   45000000  -
```

(VCO_in 열은 표시용 내림 값이다 — 8/6 MHz 는 정수 Hz 가 아니다. 계산은 그 값을 쓰지 않는다.)

구형 문서 제약(N ≥ 192)에서는 M=4 의 N=168 이 탈락한다. ST 의 오래된 F4 템플릿이 M=8, N=336 을 쓴 이유가 이것인지는 확인하지 못했다 — 다만 그 값은 두 제약 모두에서 해이므로 "어느 문서를 봐도 안전한 값" 이긴 하다.

**왜 25 MHz 크리스털은 해가 하나뿐인가 — 약수 논증.** N = 168·M·P/25 가 정수 ⇔ 25 | 168·M·P. gcd(168, 25) = 1 이고 P ∈ {2,4,6,8} 은 5 의 배수가 아니므로 ⇔ **25 | M**. VCO 입력 1–2 MHz 는 M ∈ [13, 25] 를 강제하므로 M = 25 뿐. P = 2 → N = 336, VCO 336 ✓, P = 4 → VCO 672 ✗. 그래서 해는 (25, 336, 2) 하나, VCO 입력은 1 MHz 로 고정이다 — 25 MHz 보드는 "2 MHz 권장" 을 168 MHz 에서 지킬 수 없다. (Ethernet PHY 때문에 25 MHz 를 쓰는 보드에서 실제로 마주치는 상황.)

**같은 방법으로 26 MHz** (RF 모듈에서 흔한 TCXO 값): N = 168·M·P/26 = 84·M·P/13 → 13 | M. VCO 입력 범위가 M ∈ [13, 26] 이므로 M = 13 (VCO 입력 2 MHz!) 또는 26 → 해 2 개 (M13 N168, M26 N336, 둘 다 P = 2, Q = 7). 25 MHz 보다 26 MHz 가 오히려 "권장 2 MHz" 를 지킨다. 손 논증은 항상 코드로 검증한다 — 테스트의 `26 MHz → 156 MHz` 는 brute force 와 개수를 비교한다 (14 개).

**USB 48 MHz 가 정확한지는 HSE 와 무관하다.** 정확한 해에서 VCO = SYSCLK·P 이므로 "Q 가 있다" ⇔ 48 MHz | SYSCLK·P. 180 MHz 이면 180·P/48 = 15P/4 → P 가 4 의 배수여야 하는데 P = 4 면 VCO 720 MHz ✗. 그래서 F42x 를 180 MHz 로 돌리면 메인 PLL 로는 USB 를 못 쓴다 — 실무에서 "180 MHz 로 올렸더니 USB 가 죽었다" 의 정체. (F446·F469·F7 계열은 다른 PLL 출력을 48 MHz 소스로 고를 수 있다 — RM 확인 권장.)

**순위 근거.**

| 순위 | 기준 | 이유 |
|---|---|---|
| ① | USB 48 MHz 정확 | USB full-speed 는 12 Mb/s ±0.25 % (2500 ppm). 45 MHz 같은 값은 기능 고장 — 하드 요구사항 |
| ② | VCO 입력 2 MHz 에 가까울수록 (M 작게) | RM0090 권장 ("to limit PLL jitter"). 위상 비교 주파수가 높을수록 N 이 작고, 기준 쪽 잡음은 대략 N 배(20·log10 N dB)로 출력에 실린다 — N 168 vs 336 이면 약 6 dB (일반 PLL 이론, 칩별 수치는 아님) |
| ③ | VCO 가 낮을수록 | 같은 SYSCLK 이면 VCO 를 덜 돌리는 쪽이 전력이 적다 (이 레슨의 선택 — RM 근거 아님) |

8 MHz → 84 MHz 에서 이 순위가 실제로 작동한다: 해 10 개 중 P = 2 (VCO 168 → 168/48 = 3.5, USB ✗) 와 P = 4 (VCO 336, Q = 7 ✓) 가 같은 M = 4 로 나오는데, ①이 P = 4 를 고른다. ③(낮은 VCO)만 봤다면 USB 가 안 되는 P = 2 를 골랐을 것이다.

### Part B — 경계와 property test

| 테스트 | 왜 넣었나 | 결과 |
|---|---|---|
| 8 → 12.5 MHz | VCO = 100 MHz (하한), N = 50 (하한) 둘 다 경계 | 해 3 개 (M4 N50, M6 N75, M8 N100, 전부 P = 8) |
| 8 → 108 MHz | VCO = 432 MHz (상한, P = 4), N = 432 (상한, M = 8) | M4 N216 P4 Q9, M8 N432 P4 Q9 포함 |
| 4 → 168 MHz | HSE 하한, VCO 입력 2 MHz(M=2)·1 MHz(M=4) 양 끝 | 해 3 개 |
| 8 → 12 MHz | 12 × 8 = 96 < 100: P 를 최대로 해도 VCO 하한 미달 | 0 개 |
| 8 → 100 000 001 Hz | 소수(素數)에 가까운 목표 — N 이 정수가 될 수 없음 | 0 개 |
| HSE 3 / 27 MHz, 목표 0, F40x 180 MHz | 인자 오류 경로 | `CLK_ERR_HSE` / `_ARG` / `_SYSCLK` |
| cap = 2 | 버퍼보다 해가 많을 때 | 반환 5, out[0..1] = M4, M5, out[2] 가드 그대로 |

property test 는 정답 코드와 **완전히 다른 방식**의 기준(oracle)을 쓴다: 레지스터 필드로 표현 가능한 모든 값 — M 0..63, N 0..511, P 코드 0..3 — 를 다 돌며 제약을 하나씩 직접 검사한다 (64 × 512 × 4 = 131 072 조합). 열거기가 약수 조건이나 경계를 하나라도 틀리면 개수가 어긋난다. 목표는 ① 임의의 (M, N, P) 에서 역산한 값(해가 있을 법함) ② 정수 MHz ③ 아무 Hz 를 섞었다. `./solution -v` 실제 출력:

```text
property: 400 cases, 118 with solutions, 719 solutions total
```

"0 == 0" 만 비교한 테스트가 아니라는 증거로 `nonzero > 100` 도 CHECK 한다.

**플래시 wait state.** 플래시 읽기 시간은 고정(수십 ns)인데 CPU 클럭이 빨라지면 한 클럭 안에 못 읽는다. 그래서 HCLK 가 한 칸(3.3 V 에서 30 MHz)을 넘을 때마다 wait state 를 하나 더 넣는다. 전압이 낮으면 플래시가 느려져 칸이 좁아진다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">FLASH_ACR.LATENCY vs HCLK — WS = ceil(HCLK/step) − 1 (RM0090 표의 step 모델)</text>
  <rect x="60" y="40" width="640" height="220" fill="none" stroke="currentColor"/>
  <line x1="60" y1="232.5" x2="700" y2="232.5" stroke="currentColor" stroke-opacity="0.2"/>
  <line x1="60" y1="205" x2="700" y2="205" stroke="currentColor" stroke-opacity="0.2"/>
  <line x1="60" y1="177.5" x2="700" y2="177.5" stroke="currentColor" stroke-opacity="0.2"/>
  <line x1="60" y1="150" x2="700" y2="150" stroke="currentColor" stroke-opacity="0.2"/>
  <line x1="60" y1="122.5" x2="700" y2="122.5" stroke="currentColor" stroke-opacity="0.2"/>
  <line x1="60" y1="95" x2="700" y2="95" stroke="currentColor" stroke-opacity="0.2"/>
  <line x1="60" y1="67.5" x2="700" y2="67.5" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="52" y="264" fill="currentColor" text-anchor="end">0</text>
  <text x="52" y="236" fill="currentColor" text-anchor="end">1</text>
  <text x="52" y="209" fill="currentColor" text-anchor="end">2</text>
  <text x="52" y="181" fill="currentColor" text-anchor="end">3</text>
  <text x="52" y="154" fill="currentColor" text-anchor="end">4</text>
  <text x="52" y="126" fill="currentColor" text-anchor="end">5</text>
  <text x="52" y="99" fill="currentColor" text-anchor="end">6</text>
  <text x="52" y="71" fill="currentColor" text-anchor="end">7</text>
  <text x="52" y="44" fill="currentColor" text-anchor="end">8</text>
  <text x="20" y="150" fill="currentColor">WS</text>
  <text x="60" y="276" fill="currentColor" text-anchor="middle">0</text>
  <text x="166.7" y="276" fill="currentColor" text-anchor="middle">30</text>
  <text x="273.3" y="276" fill="currentColor" text-anchor="middle">60</text>
  <text x="380" y="276" fill="currentColor" text-anchor="middle">90</text>
  <text x="486.7" y="276" fill="currentColor" text-anchor="middle">120</text>
  <text x="593.3" y="276" fill="currentColor" text-anchor="middle">150</text>
  <text x="657.3" y="276" fill="currentColor" text-anchor="middle">168</text>
  <text x="380" y="294" fill="currentColor" text-anchor="middle">HCLK [MHz] (SYSCLK 이 아니라 AHB 분주 뒤의 HCLK)</text>
  <polyline points="60,260 166.7,260 166.7,232.5 273.3,232.5 273.3,205 380,205 380,177.5 486.7,177.5 486.7,150 593.3,150 593.3,122.5 657.3,122.5" fill="none" stroke="currentColor" stroke-width="2.5"/>
  <polyline points="60,260 131.1,260 131.1,232.5 202.2,232.5 202.2,205 273.3,205 273.3,177.5 344.4,177.5 344.4,150 415.6,150 415.6,122.5 486.7,122.5 486.7,95 557.8,95 557.8,67.5 628.9,67.5" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="5 3"/>
  <circle cx="358.7" cy="205" r="5" class="f-ok" stroke="currentColor"/>
  <text x="366" y="222" fill="currentColor">84 MHz → 2</text>
  <circle cx="657.3" cy="122.5" r="5" class="f-ok" stroke="currentColor"/>
  <text x="604" y="114" fill="currentColor">168 → 5</text>
  <circle cx="657.3" cy="40" r="5" class="f-warn" stroke="currentColor"/>
  <text x="560" y="58" fill="currentColor">1.8 V: 168 → 8 WS ✗</text>
  <text x="80" y="62" fill="currentColor">실선: 2.7–3.6 V, 30 MHz/WS</text>
  <text x="80" y="80" fill="currentColor">점선: 1.8–2.1 V, 20 MHz/WS, 최대 7 WS → 160 MHz</text>
</svg>
```

경계 숫자: `flash_wait_states(30 000 000) = 0`, `(30 000 001) = 1`, `(60 000 000) = 1`, `(84 MHz) = 2`, `(168 MHz) = 5`, 1.8 V 에서 `(160 MHz) = 7`, `(168 MHz) = -1`. WS 가 부족하면 CPU 가 덜 읽힌 플래시 값을 명령으로 실행한다 — 증상은 랜덤 HardFault 이고, 디버거로 한 스텝씩 가면 멀쩡해 보여 찾기 어렵다.

### Part C — 계획 하나를 레지스터 값까지

`clock_plan(F40X, 8 MHz, 168 MHz)` 의 단계:

| 단계 | 규칙 | 값 |
|---|---|---|
| 1. 소스 | AHB d = 1 부터: 168 ≠ HSE 8 → PLL 열거 → best | M4 N168 P2 Q7, SYSCLK 168 |
| 2. WS | (168e6 − 1) / 30e6 | 5 |
| 3. APB1 | 168 ≤ 42·d 인 최소 d ∈ {1,2,4,8,16} | /4 → PCLK1 42 MHz |
| 4. APB2 | 168 ≤ 84·d | /2 → PCLK2 84 MHz |
| 5. 타이머 | 분주 ≠ 1 → ×2 | APB1 타이머 84 MHz, APB2 타이머 168 MHz |
| 6. PLLCFGR | 4 \| 168≪6 \| 0≪16 \| 1≪22 \| 7≪24 | 0x0000_0004 + 0x0000_2A00 + 0x0040_0000 + 0x0700_0000 = **0x07402A04** |
| 7. CFGR | SW = 10, HPRE = 0, PPRE1 = 101, PPRE2 = 100 | 0x2 + 0x5≪10 (0x1400) + 0x4≪13 (0x8000) = **0x9402** |
| 8. ACR | LATENCY 5 \| PRFTEN \| ICEN \| DCEN | **0x705** |

```svg
<svg viewBox="0 0 760 210" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">RCC_PLLCFGR = 0x07402A04 (M=4, N=168, P=/2, HSE, Q=7) — 비트 위치는 RM0090 기준 대표값</text>
  <rect x="20" y="40" width="90" height="40" class="f-mut" stroke="currentColor"/>
  <rect x="110" y="40" width="90" height="40" class="f-acc" stroke="currentColor"/>
  <rect x="200" y="40" width="22.5" height="40" class="f-mut" stroke="currentColor"/>
  <rect x="222.5" y="40" width="22.5" height="40" class="f-ok" stroke="currentColor"/>
  <rect x="245" y="40" width="90" height="40" class="f-mut" stroke="currentColor"/>
  <rect x="335" y="40" width="45" height="40" class="f-warn" stroke="currentColor"/>
  <rect x="380" y="40" width="22.5" height="40" class="f-mut" stroke="currentColor"/>
  <rect x="402.5" y="40" width="202.5" height="40" class="f-acc" stroke="currentColor"/>
  <rect x="605" y="40" width="135" height="40" class="f-ok" stroke="currentColor"/>
  <text x="65" y="34" fill="currentColor" text-anchor="middle">31:28 res</text>
  <text x="155" y="34" fill="currentColor" text-anchor="middle">PLLQ 27:24</text>
  <text x="233.75" y="34" fill="currentColor" text-anchor="middle">SRC</text>
  <text x="290" y="34" fill="currentColor" text-anchor="middle">21:18 res</text>
  <text x="357.5" y="34" fill="currentColor" text-anchor="middle">P 17:16</text>
  <text x="503.75" y="34" fill="currentColor" text-anchor="middle">PLLN 14:6</text>
  <text x="672.5" y="34" fill="currentColor" text-anchor="middle">PLLM 5:0</text>
  <text x="65" y="65" fill="currentColor" text-anchor="middle" font-family="monospace">0000</text>
  <text x="155" y="65" fill="currentColor" text-anchor="middle" font-family="monospace">0111</text>
  <text x="211" y="65" fill="currentColor" text-anchor="middle" font-family="monospace">0</text>
  <text x="233.75" y="65" fill="currentColor" text-anchor="middle" font-family="monospace">1</text>
  <text x="290" y="65" fill="currentColor" text-anchor="middle" font-family="monospace">0000</text>
  <text x="357.5" y="65" fill="currentColor" text-anchor="middle" font-family="monospace">00</text>
  <text x="391" y="65" fill="currentColor" text-anchor="middle" font-family="monospace">0</text>
  <text x="503.75" y="65" fill="currentColor" text-anchor="middle" font-family="monospace">0 1010 1000</text>
  <text x="672.5" y="65" fill="currentColor" text-anchor="middle" font-family="monospace">00 0100</text>
  <text x="155" y="100" fill="currentColor" text-anchor="middle">Q = 7</text>
  <text x="233.75" y="100" fill="currentColor" text-anchor="middle">HSE</text>
  <text x="357.5" y="100" fill="currentColor" text-anchor="middle">/2</text>
  <text x="503.75" y="100" fill="currentColor" text-anchor="middle">N = 168</text>
  <text x="672.5" y="100" fill="currentColor" text-anchor="middle">M = 4</text>
  <text x="20" y="130" fill="currentColor">P 코드: 00=/2 01=/4 10=/6 11=/8 → 코드 = P/2 − 1. 값 2 를 그대로 2≪16 으로 쓰면 코드 10 = /6:</text>
  <text x="20" y="150" fill="currentColor" font-family="monospace">0x07422A04 → SYSCLK = 336/6 = 56 MHz (컴파일·실행 모두 조용히 "성공")</text>
  <text x="20" y="175" fill="currentColor">리셋값 0x24003010 = M 16, N 192, /2, HSI, Q 4 (bit29 예약=1) → 96 MHz · 48 MHz (연습 2)</text>
  <text x="20" y="195" fill="currentColor">M = 0·1, N &lt; 50 같은 값은 필드에 들어가지만 금지 — 디코더가 거부해야 한다</text>
</svg>
```

`./solution -v` 의 실제 계획 출력:

```text
8->168     src=PLL M=4 N=168 P=2 Q=7 SYSCLK=168000000 HCLK=168000000 AHB/1 APB1/4=42000000 APB2/2=84000000 TIMx1=84000000 TIMx2=168000000 WS=5 PLLCFGR=0x07402A04 CFGR=0x00009402 ACR=0x00000705
8->84      src=PLL M=4 N=168 P=4 Q=7 SYSCLK=84000000 HCLK=84000000 AHB/1 APB1/2=42000000 APB2/1=84000000 TIMx1=84000000 TIMx2=84000000 WS=2 PLLCFGR=0x07412A04 CFGR=0x00001002 ACR=0x00000702
25->168    src=PLL M=25 N=336 P=2 Q=7 SYSCLK=168000000 HCLK=168000000 AHB/1 APB1/4=42000000 APB2/2=84000000 TIMx1=84000000 TIMx2=168000000 WS=5 PLLCFGR=0x07405419 CFGR=0x00009402 ACR=0x00000705
8->1       src=HSE M=0 N=0 P=0 Q=0 SYSCLK=8000000 HCLK=1000000 AHB/8 APB1/1=1000000 APB2/1=1000000 TIMx1=1000000 TIMx2=1000000 WS=0 PLLCFGR=0x00000000 CFGR=0x000000A1 ACR=0x00000700
8->12      src=PLL M=4 N=72 P=6 Q=3 SYSCLK=24000000 HCLK=12000000 AHB/2 APB1/1=12000000 APB2/1=12000000 TIMx1=12000000 TIMx2=12000000 WS=0 PLLCFGR=0x03421204 CFGR=0x00000082 ACR=0x00000700
F42x 180   src=PLL M=4 N=180 P=2 Q=8 SYSCLK=180000000 HCLK=180000000 AHB/1 APB1/4=45000000 APB2/2=90000000 TIMx1=90000000 TIMx2=180000000 WS=5 PLLCFGR=0x08402D04 CFGR=0x00009402 ACR=0x00000705
```

읽을 거리:

- **8 → 84**: 같은 84 MHz 인데 APB1 타이머(분주 /2 → ×2 = 84)와 APB2 타이머(분주 /1 → ×1 = 84)가 같은 값이 된다. "타이머 클럭 = PCLK × 2" 로 외우면 APB2 쪽에서 틀린다. 규칙은 "분주가 1 이면 ×1, 아니면 ×2" (RM0090 RCC 장, F42x 는 RCC_DCKCFGR.TIMPRE 로 ×4 까지 — RM 확인 권장). 타이머 PSC/ARR 계산(BM-B14)은 이 값에서 시작한다.
- **8 → 12**: PLL 출력 최솟값은 VCO 100 MHz / P 8 = 12.5 MHz 라 12 MHz 는 PLL 로 직접 안 된다. 계획기는 AHB /2 를 써서 PLL 24 MHz (VCO 144, Q 3 → 48 MHz 정확)로 간다. HSE 8 MHz 를 /… 로 나눠서는 12 가 안 나온다.
- **8 → 1**: PLL 을 켜지 않고 HSE + AHB /8. 저전력 쪽에서는 이게 정답이다 (PLL 자체도 전류를 먹고 — 데이터시트 표로 수백 µA 대, 확인 권장 — 락 시간도 든다. BM-M11 참고).
- **CFGR 0x9402 / ACR 0x705** 는 구 SPL `system_stm32f4xx.c` 의 `SetSysClock()` 에서 보던 값 (`HPRE_DIV1 | PPRE2_DIV2 | PPRE1_DIV4`, `PRFTEN | ICEN | DCEN | LATENCY_5WS`) 과 같다. 전환 **순서**(HSE ON → HSERDY 대기 → PLL 설정 → PLLON → PLLRDY → LATENCY 먼저 → SW = PLL → SWS 확인)는 BM-A01 에서 다룬다.

**float 로 하면 왜 안 되나 — `demo_float.c` 실제 출력.** 10 가지 크리스털에 대해 F40X 제약을 만족하는 모든 (M, N, P) 중 결과가 정수 Hz 인 것(정확한 해)을 기준으로, `double`·`float` 로 `hse / M * N / P == target` 을 계산했을 때:

```text
HSE(Hz)     int-solutions  double-missed  float-missed  float-false
  double miss: HSE=8000000 M=6 N=99 P=2 target=66000000 got 65999999.999999993
  float  miss: HSE=8000000 M=6 N=99 P=2 target=66000000 got 66000004.000
  ...
8000000              2541             95            14        20735
12000000             3839             62            22        29524
16000000             3050            205           124        35467
25000000             3353             91           140        49167
26000000             4792            121           236        55118
12288000             3237             40             3        26291
24576000             4166             40            43        48371
19200000             3791            110           159        40242
14745600             3457            155            48        29142
13560000             3021              0            65        26587
total               35247            919           854       360769   (double-false 0)
worst float false accept: HSE=25000000 M=21 N=260 P=2 true=154761904.761905 Hz accepted target=154761928 (off by >= 24 Hz)
```

- `double` 은 정확한 해 35 247 개 중 **919 개(2.6 %)를 놓친다** — 8e6/6 이 2 진수로 정확하지 않아 66 000 000 이 65 999 999.999999993 이 된다. 8 MHz → 66 MHz 처럼 평범한 목표에서다.
- `float` (24비트 가수) 은 1e8 근처에서 표현 간격이 8–16 Hz 라, 진짜 값에서 1 Hz 이상 떨어진 목표를 "정확" 이라고 받아들이는 (조합, 목표) 쌍이 36 만 개다. 최악은 24 Hz 차이.
- 정수식 `hse·N == T·M·P` 는 둘 다 0. 펌웨어 쪽(FPU 없는 M0/M3)에서도 그대로 돈다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 69/69 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 69/69, 경고 없음
```

## 7. 자주 하는 실수

**1. float 로 "정확히" 비교한다.**
`if ((float)hse / m * n / p == target)` — 위 실측대로 해를 놓치고 틀린 해를 받는다. 오차 허용(`fabs(x − t) < 1`)으로 바꿔도 "정확한 해" 와 "0.4 Hz 틀린 해" 를 구분하지 못한다. 고친 코드: `(uint64_t)hse * n == (uint64_t)target * m * p`.

**2. 32비트 곱셈.**
`hse * n` 은 26 MHz × 432 ≈ 1.12e10, `target * m * p` 는 168 MHz × 63 × 8 ≈ 8.5e10 — 둘 다 2^32 ≈ 4.29e9 를 넘는다. uint32 로는 mod 2^32 로 감겨 비교가 엉뚱하게 참/거짓이 된다. 첫 피연산자를 `(uint64_t)` 로 캐스트한다.

**3. P 를 값으로 쓴다.**

```c
RCC->PLLCFGR = M | (N << 6) | (2u << 16) | ...;        /* 틀림: 코드 10 = /6 → 56 MHz */
RCC->PLLCFGR = M | (N << 6) | ((P / 2u - 1u) << 16) | ...;  /* 맞음: /2 → 00 */
```

같은 계열로 HPRE: /2 는 `1000` 이지 `0001` 이 아니고, `/32` 는 존재하지 않는다 (`rcc_cfgr_word(…, 32, …) == 0xFFFFFFFF` 테스트).

**4. WS 를 SYSCLK 으로 계산하거나, 클럭을 먼저 올린다.**
WS 는 CPU 가 플래시를 읽는 클럭 = **HCLK** 기준이다 (8 → 1 계획은 SYSCLK 8 MHz 지만 HCLK 1 MHz). 그리고 클럭을 **올릴 때는 LATENCY 를 먼저** 쓰고 읽어서 확인한 뒤 SW 를 바꾸고, **내릴 때는 SW 를 먼저** 바꾼 뒤 LATENCY 를 줄인다 (RM0090 플래시 장 — 확인 권장). 반대로 하면 그 사이 몇 클럭 동안 WS 가 부족하다.

**5. 타이머 클럭을 PCLK 로 계산한다.**
PCLK1 = 42 MHz 인데 TIM2 는 84 MHz 로 돈다. PSC 를 42 MHz 기준으로 잡으면 주기가 절반이 된다 (BM-B14). 반대로 APB2 분주가 1 이면 ×2 가 없다.

**6. 경계를 `<` 로 쓴다.**
`vco > vco_min` 이라고 쓰면 VCO = 100 MHz 인 12.5 MHz 해 3 개가 사라진다. RM 표는 "100 ≤ VCO ≤ 432" 처럼 양 끝 포함이다. 테스트가 12.5 MHz 와 108 MHz 를 넣는 이유.

**7. cap 을 넘겨 쓰거나, 총 개수 대신 쓴 개수를 돌려준다.**
`out[count++] = …` 를 cap 검사 없이 쓰면 스택 버퍼 오버런이고, 쓴 개수만 돌려주면 호출자가 "버퍼가 작았다" 를 알 수 없다. `snprintf` 처럼 **총 개수를 돌려주고 cap 개만 쓴다**.

## 8. 면접 Q&A

<details>
<summary>Q1. VCO 입력을 1–2 MHz 로 제한하고 2 MHz 를 권장하는 이유는?</summary>

PLL 은 VCO 출력을 N 으로 나눈 것과 입력(HSE/M)을 위상 비교기에서 맞춘다. 비교 주파수가 높을수록 ① 같은 VCO 에 N 이 작아져 기준 쪽 잡음이 덜 증폭되고 (대략 20·log10 N dB — N 168 vs 336 이면 약 6 dB), ② 루프 대역폭을 넓게 잡을 수 있어 VCO 자체 잡음을 더 잘 누른다. 그래서 jitter 가 줄어든다. 하한(1 MHz)·상한(2 MHz)은 그 칩의 위상 비교기·루프 필터가 설계된 범위다. RF 쪽 synthesizer 를 다뤄 봤다면 "phase noise floor + 20 log N" 과 같은 얘기다. 꼬리질문 "그럼 25 MHz 크리스털은?" → 168 MHz 정확해는 M = 25 (1 MHz) 하나뿐이라 권장값을 못 지킨다. 1 MHz 도 사양 범위 안이므로 보통은 그대로 쓰고 jitter 를 감수한다. jitter 가 중요한 설계(고속 ADC 샘플 클럭, 오디오)라면 크리스털을 8/16/26 MHz 처럼 2 MHz 의 정수배로 고르는 것이 근본 해결이다 (26 MHz 는 M = 13 으로 2 MHz).

</details>

<details>
<summary>Q2. 왜 APB 타이머 클럭은 APB 분주가 1 이 아니면 2배인가?</summary>

타이머가 버스 분주 때문에 느려지지 않게 하려는 설계다. APB1 은 42 MHz 상한 때문에 168 MHz 에서 /4 를 써야 하는데, 타이머까지 42 MHz 로 떨어지면 PWM 해상도·입력 캡처 분해능이 반으로 준다. 그래서 RCC 가 타이머에는 분주 전 클럭의 절반(= PCLK × 2)을 따로 준다. 분주가 1 이면 PCLK 자체가 HCLK 라 더 올릴 수 없으니 ×1. 실무 함정: "타이머 = PCLK × 2" 로만 외우면 APB2 분주 1 인 설계(84 MHz 계획)에서 틀린다. 레지스터 쪽 확인 방법: CubeMX 의 "APB1 Timer clocks" 칸, 또는 HAL 의 `HAL_RCC_GetPCLK1Freq()` 후 `CFGR.PPRE1` 을 보고 직접 ×2.

</details>

<details>
<summary>Q3. 168 MHz 로 올리는 코드에서 FLASH LATENCY 와 SW 중 무엇을 먼저 바꾸나? 내릴 때는?</summary>

올릴 때: LATENCY 를 먼저 5 로 쓰고 **다시 읽어서 5 인지 확인**한 다음 SW = PLL. 플래시 인터페이스가 새 지연을 적용하기 전에 클럭이 올라가면 몇 번의 fetch 가 WS 부족으로 깨진다. 내릴 때: SW 를 먼저 낮은 클럭으로 바꾸고 SWS 로 확인한 다음 LATENCY 를 줄인다. 둘 다 "모든 순간에 WS ≥ 필요량" 을 지키는 순서다. 지연을 늘리는 쪽은 언제나 안전하고(느려질 뿐), 줄이는 쪽만 위험하다. 순서 전체는 BM-A01 (SystemInit).

</details>

<details>
<summary>Q4. F429 를 180 MHz 로 돌리면 USB 가 안 된다는데 왜? 해결은?</summary>

PLL48CLK = VCO / Q 이고, 정확한 해에서 VCO = SYSCLK × P. 180 × 2 = 360 MHz 는 48 의 배수가 아니다 (360/48 = 7.5). P = 4 면 720 MHz 로 VCO 상한 초과. 그래서 M, HSE 와 상관없이 메인 PLL 로는 48 MHz 정확이 불가능하다 — 이 레슨의 열거가 5 개 해 전부 Q = 8 (45 MHz) 인 것으로 보여 준다. 45 MHz 는 USB full-speed 허용 오차 ±0.25 % 를 한참 벗어난다. 해결: ① SYSCLK 을 168 MHz 로 (336/7 = 48) ② 48 MHz 를 다른 PLL(PLLSAI/PLLI2S)에서 받을 수 있는 칩(F446·F469·F7 등 — RM 확인 권장) ③ 외부 ULPI PHY 를 쓰는 HS 포트. 면접에서 "왜 많은 보드가 180 이 아니라 168 MHz 로 도나" 의 답이기도 하다.

</details>

<details>
<summary>Q5. 클럭 설정이 맞는지 실보드에서 어떻게 확인하나?</summary>

① `RCC_CFGR.SWS` 를 읽어 실제 시스템 클럭 소스가 PLL 인지, `RCC_CR.PLLRDY`·`HSERDY` 확인. ② **MCO 핀**: MCO1 (PA8) 에 HSE/PLL, MCO2 (PC9) 에 SYSCLK/PLL 을 분주(/1–/5)해 내보내 스코프·주파수 카운터로 잰다 (168/4 = 42 MHz 처럼 측정 가능한 값으로). ③ SysTick 이나 DWT CYCCNT 로 1 초를 세게 하고 LSE 32.768 kHz 기반 RTC 와 비교 — 크리스털 두 개의 비로 SYSCLK 을 ppm 단위로 잴 수 있다. ④ UART 를 115200 으로 띄워 글자가 깨지지 않는지 (깨지면 HSE 값 설정 `HSE_VALUE` 와 실제 크리스털 불일치가 1순위). 실리콘 검증 관점에서는 ②·③을 자동화해 모든 프리셋을 스윕한다.

</details>

<details>
<summary>Q6. HSE 가 안 뜨면(크리스털 불량, 부하 커패시터 오류)? </summary>

`HSEON` 후 `HSERDY` 를 **타임아웃을 두고** 기다린다 (HAL 의 `HSE_STARTUP_TIMEOUT` 기본 100 ms — 확인 권장). 타임아웃이면 HSI 16 MHz 로 남아 동작하면서 오류를 기록·보고한다 — 무한 대기 루프면 보드가 부팅조차 안 하는 것처럼 보인다. 동작 중 HSE 가 죽는 경우는 CSS (Clock Security System, `RCC_CR.CSSON`) 가 감지해 HSI 로 자동 전환하고 NMI 를 띄운다. 이때 PLL 도 꺼지므로 NMI 핸들러에서 재설정해야 한다. 이 레슨의 `practice.c` 연습 1 이 HSI 를 PLL 소스로 하는 계획(16 → 168 MHz, PLLCFGR 0x07002A08)을 계산한다 — 그게 폴백 계획이다.

</details>

<details>
<summary>Q7. 이 계산기를 펌웨어에서 런타임에 돌리나?</summary>

보통은 아니다. 크리스털·목표가 컴파일 시점에 정해지므로 빌드 때(호스트 툴, CubeMX, 혹은 Python 스크립트) 상수를 만들고 펌웨어에는 결과만 넣는다. 펌웨어 쪽에서는 `_Static_assert(HSE_VALUE / PLL_M * PLL_N / PLL_P == 168000000, ...)` 처럼 정수식으로 다시 검증한다 (정수식이 정확할 때만). 런타임 계산이 필요한 경우는 ① 여러 HSE 변형 보드를 한 바이너리로 지원 ② DVFS (전압·주파수 단계 전환) ③ 부트로더가 앱의 요구 클럭을 읽어 맞출 때. 그때도 이 레슨처럼 malloc 없는 정적 버퍼·정수 연산이어야 하고, 열거는 최대 248 번 루프라 수 µs 수준이다.

</details>

<details>
<summary>Q8. APB1 을 42 MHz 넘게(예: /2 → 84 MHz) 쓰면 어떻게 되나?</summary>

레지스터는 그 값을 받아들이고 많은 경우 "돌아가는 것처럼" 보인다. 하지만 APB1 주변장치(USART2–5, I2C, SPI2/3, TIM2–7…)의 타이밍 사양은 42 MHz 에서 보증된 것이라 온도·전압 코너에서 간헐 오류가 난다 — 재현이 어려운 최악의 유형. 그래서 계획기는 상한을 하드 제약으로 두고 `apb_div_for` 가 "상한 이하인 최소 분주" 를 고른다. 연습 4 는 그 결과로 APB1 USART 의 921600 baud 오차가 −0.93 % (APB2 는 +0.16 %) 가 됨을 보인다 — 고속 UART 는 APB2 의 USART1/6 에 둔다 (BRR 세부는 BM-B12, 허용 오차는 COMM-B03).

</details>

## 9. 추가 연습

### 연습 1 — HSI 16 MHz 를 PLL 소스로 (HSE 폴백 계획)

HSI 를 소스로 168 MHz 를 만드는 해는 몇 개이고, best 와 PLLCFGR 값은? (제약 구조체의 HSE 범위를 16 MHz 한 점으로 바꾼 사본을 쓰면 열거기는 그대로.)

<details>
<summary>답</summary>

M = 8..16 (VCO 입력 2..1 MHz), 전부 P = 2, N = 21·M → **9 개**. best M8 N168 P2 Q7. PLLSRC = 0 이므로 `8 | 168<<6 | 7<<24` = **0x07002A08** (HSE 판 0x07402A04 와 bit 22 만 다르다). HSI 는 RC 발진기라 정확도가 ±1 % 대(데이터시트 확인 권장)여서 USB 48 MHz 는 계산상 정확해도 실제로는 허용 오차(±0.25 %)를 못 지킬 수 있다.

</details>

### 연습 2 — 레지스터 덤프 읽기: RCC_PLLCFGR 디코더

`pllcfgr_decode(w, hse, hsi, L, &d)` 로 필드를 뽑고 주파수를 계산하고, 제약 위반이면 -1. 리셋값 0x24003010, 고전값 0x07405408 을 8 MHz 보드와 25 MHz 보드에서 각각 읽어라.

<details>
<summary>답</summary>

리셋값 0x24003010 → M 16, N 192, P /2, **HSI**, Q 4 → VCO 입력 1 MHz, VCO 192, PLLCLK 96 MHz, PLL48 48 MHz (유효). 0x07405408 을 8 MHz 보드에서 → 168 MHz (유효). **같은 값을 25 MHz 보드에서** → VCO 입력 3.125 MHz (범위 밖), PLLCLK 525 MHz → -1. 크리스털만 바꾸고 `HSE_VALUE`·PLL 상수를 안 바꾼 보드가 실제로 겪는 사고다 (PLL 이 락이 안 되거나 오버클럭). M = 1 (0x07405401), N = 49 (0x07400C48), M = 0 (0으로 나누기 전에 거부) 도 -1.

</details>

### 연습 3 — 정확한 해가 없을 때: 24.576 MHz 오디오 크리스털로 168 MHz

`pll_nearest_below(L, hse, target, &cfg, &ppm)` — 목표 이하 최대 SYSCLK 과 오차 ppm (올림).

<details>
<summary>답</summary>

먼저 정확한 해가 없음을 증명: 168e6 / 24.576e6 = 875/128 이므로 N = 875·M·P/128 → 128 | M·P. VCO 입력 ≥ 1 MHz 라 M ≤ 24, P ≤ 8 → M·P ≤ 192 → M·P = 128 뿐 → (M, P) = (16, 8) 이면 N = 875 > 432 ✗. 그래서 0 개 (`pll_enumerate` 도 0).
최근접(이하): **M 15, N 205, P 2 → 167.936 MHz, −381 ppm**. 분수 비교는 교차 곱 (`num·bden > bnum·den`) — 최대 5.7e12 라 64비트로 충분. 오디오 쪽은 이 크리스털로 I2S 클럭을 정확히 만들고(PLLI2S), 코어는 약간 낮은 클럭을 받아들이는 것이 보통이다.

</details>

### 연습 4 — 클럭 계획이 UART 에 주는 영향 (COMM-B03 연결)

8 → 168 계획의 PCLK1 42 MHz, PCLK2 84 MHz 에서 115200 과 921600 의 BRR(OVER8 = 0)과 오차 ppm 을 구하라.

<details>
<summary>답</summary>

16배 오버샘플에서 `BRR = 가수<<4 | 분수 = round(16 · USARTDIV) = round(PCLK / baud)` — 한 정수로 합쳐진다 (분수 반올림이 16 이 되어 가수로 넘어가는 경우도 자동 처리; 레지스터 세부는 BM-B12).

| PCLK | baud | BRR | 실제 baud | 오차 |
|---|---|---|---|---|
| 42 MHz | 115200 | 0x16D (365) | 115 068 | −1141 ppm |
| 84 MHz | 115200 | 0x2D9 (729) | 115 226 | +228 ppm |
| 42 MHz | 921600 | 0x2E (46) | 913 043 | **−9284 ppm (−0.93 %)** |
| 84 MHz | 921600 | 0x5B (91) | 923 077 | +1602 ppm |

−0.93 % 는 상대편 오차와 더해지면 8N1 허용 한계(COMM-B03)를 갉아먹는다 → 고속 UART 는 APB2 (USART1/6) 에.

</details>

### 연습 5 — 전압 범위별 최대 HCLK

`hclk_max_for(L) = min(sysclk_max, step·(ws_max + 1))` 로 2.7–3.6 / 2.4–2.7 / 2.1–2.4 / 1.8–2.1 V 의 최대 HCLK 과 그때 WS 를 구하라.

<details>
<summary>답</summary>

30 MHz/WS → 168 MHz, 5 WS. 24 MHz/WS → 168 MHz, 6 WS. 22 MHz/WS → 168 MHz, 7 WS. 20 MHz/WS → **160 MHz**, 7 WS (8 WS 는 LATENCY 필드에 없음). 배터리 직결 1.8 V 설계에서 168 MHz 를 못 쓰는 이유. (RM0090 표 기준 대표값 — 1.8–2.1 V 에서는 prefetch 를 끄라는 조건 등 세부는 RM 확인 권장. 전압 스케일링 PWR_CR.VOS 에 따른 상한은 이 모델 밖.)

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 29/29 passed
```

## 10. 핵심 정리

- `SYSCLK = HSE/M × N/P`, `PLL48 = HSE/M × N/Q`. 제약: VCO 입력 1–2 MHz (2 MHz 권장), VCO 100–432 MHz, M 2–63, N 50–432 (구형 192–), P {2,4,6,8}, Q 2–15. 칩이 바뀌면 **구조체만** 바꾼다.
- 정확성은 정수로: `hse·N == T·M·P` (uint64). M·P 를 고르면 N 이 정해진다 → 최대 248 번 루프. float `==` 는 실측 2.6 % 해를 놓친다.
- 해가 없음은 약수로 증명: 25 MHz → 168 은 25 | M → M = 25 하나. 180 MHz 는 48 ∤ 180·P → USB 불가 → 그래서 168 MHz.
- 순위: USB 48 정확 > VCO 입력 2 MHz 에 가깝게 > 낮은 VCO. 8 MHz → 168 best = M4 N168 P2 Q7 (고전값 M8 N336 도 해).
- APB 분주는 상한 이하 최소값 (168 → APB1 /4 = 42, APB2 /2 = 84). 타이머 클럭은 **분주 ≠ 1 이면 ×2** (84 / 168), 분주 1 이면 ×1.
- 플래시 WS = ceil(HCLK/step) − 1, **HCLK 기준**. 올릴 땐 LATENCY 먼저, 내릴 땐 SW 먼저.
- 레지스터: PLLCFGR 0x07402A04 (P 는 코드 P/2−1), CFGR 0x9402, ACR 0x705. AHB 에 /32 없음.

## 참고자료

- ST RM0090 *STM32F405/415, STM32F407/417, STM32F42xxx and STM32F43xxx reference manual* — RCC 장 (Clock tree 그림, RCC_PLLCFGR/RCC_CFGR 비트, 타이머 클럭 ×2 규칙), 플래시 장 (CPU 클럭과 플래시 지연 표, LATENCY 변경 순서) — <https://www.st.com> 에서 "RM0090" 검색 (개정판마다 PLLN/VCO 하한 표기가 다르니 최신판 확인)
- STM32F405xx/407xx 데이터시트 — PLL 특성 표 (VCO 입력·출력 범위, 개정판별 N 하한 차이 확인용), HSE 4–26 MHz
- STM32CubeMX "Clock Configuration" 탭 (같은 문제를 푸는 공식 툴 — 이 레슨 결과와 비교해 보라)
- 구 STM32F4 Standard Peripheral Library 의 `system_stm32f4xx.c` (`PLL_M 8`, `PLL_N 336`, `SetSysClock()`) — Cube 시대 템플릿(<https://github.com/STMicroelectronics/cmsis_device_f4>)에서는 클럭 설정이 `main.c` 의 `SystemClock_Config()` 로 옮겨졌다
- USB 2.0 Specification §7.1.11 Data Signaling Rate — full-speed 12 Mb/s ±0.25 % (절 번호 확인 권장)
- 관련 레슨: COMM-B03 (baud 오차와 클럭 허용 한계), BM-B12 (BRR 레지스터·폴링 UART), BM-B13 (SysTick reload — HCLK 기준), BM-B14 (타이머 PSC/ARR — 이 레슨의 타이머 클럭에서 출발), BM-A01 (SystemInit: 클럭 전환 순서·HSE 타임아웃), BM-M11 (저전력: PLL 끄기와 Stop 후 클럭 복원), SIV-B01 (레지스터 헤더 생성)
