---
id: BM-B14
time: 60분
updated: 2026-11-11
---
# BM-B14 하드웨어 타이머: PSC/ARR 계산과 주기 인터럽트

## 학습 목표

- `f_update = f_tim / ((PSC+1)(ARR+1))` 에서 각 `+1` 이 어디서 오는지 설명하고, 16비트 제약 안에서 **오차 최소 (PSC, ARR)** 를 정수 연산만으로 찾는 함수를 쓴다 (정확/부정확 판정, ppm 오차, 불가능 판정 포함).
- 같은 N 을 만드는 쌍이 여러 개일 때 무엇을 고를지(tie-break) 스스로 정하고 근거를 댄다 — 분해능(ARR 최대) vs 읽기 쉬운 1 µs tick(PSC=83).
- 타이머의 **preload / shadow** 구조를 mock 모델로 구현한다: PSC 는 항상 preload, ARR 은 ARPE=1 일 때만 preload, 둘 다 update event(UEV) 때 shadow 로 옮겨진다.
- "첫 주기가 이상해요" (EGR.UG 누락)와 "켜자마자 인터럽트가 한 번 와요" (UG 가 UIF 를 세움) 를 mock 에서 클럭 단위로 재현하고, 정석 초기화 순서로 고친다.
- 동작 중 주기를 바꿀 때 ARPE=0 이면 생기는 글리치(ARR < CNT → 0xFFFF 까지 돌고 감김)를 숫자로 보여 준다.

## 1. 문제

"84 MHz 타이머로 1 kHz 인터럽트를 만들어 보세요 — PSC 와 ARR 은?" 은 펌웨어 면접의 단골 워밍업이고, 보드 브링업에서는 "첫 인터럽트만 이상하게 빨리 와요" 같은 버그로 다시 만난다. SSD 컨트롤러의 타임아웃 타이머, RF 모듈의 TDMA 슬롯 타이머, 모터 제어 루프의 10 kHz tick 이 전부 이 계산 하나 위에 있다.

STM32 범용 타이머(general-purpose timer, TIM2–TIM5)의 업카운트 모드는 단순하다. 타이머 클럭 f_tim 을 **프리스케일러(prescaler, PSC)** 가 (PSC+1) 로 나누고, 그 클럭으로 **카운터(CNT)** 가 0 → ARR 까지 센 뒤 0 으로 돌아가며 **업데이트 이벤트(update event, UEV)** 를 낸다. UEV 는 상태 비트 `SR.UIF` 를 세우고, `DIER.UIE` 가 켜져 있으면 NVIC 로 인터럽트가 간다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">범용 타이머 업카운트: preload → shadow 는 UEV 때만 (STM32F4 TIMx, RM0090 기준 단순화)</text>
  <rect x="10" y="70" width="90" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="90" fill="currentColor">f_tim</text>
  <text x="20" y="107" fill="currentColor" font-family="monospace">84 MHz</text>
  <rect x="140" y="40" width="130" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="150" y="62" fill="currentColor" font-family="monospace">PSC (preload)</text>
  <rect x="140" y="100" width="130" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="150" y="122" fill="currentColor" font-family="monospace">÷(PSC_sh + 1)</text>
  <rect x="320" y="100" width="150" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="330" y="122" fill="currentColor" font-family="monospace">CNT: 0,1,…,ARR_sh</text>
  <rect x="320" y="190" width="150" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="330" y="212" fill="currentColor" font-family="monospace">ARR_sh (shadow)</text>
  <rect x="320" y="250" width="150" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="330" y="272" fill="currentColor" font-family="monospace">ARR (preload)</text>
  <rect x="520" y="100" width="90" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="535" y="122" fill="currentColor" font-weight="bold">UEV</text>
  <rect x="520" y="40" width="90" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="530" y="62" fill="currentColor" font-family="monospace">EGR.UG</text>
  <rect x="640" y="100" width="90" height="34" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="650" y="122" fill="currentColor" font-family="monospace">SR.UIF</text>
  <rect x="640" y="190" width="90" height="50" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="650" y="210" fill="currentColor" font-family="monospace">&amp; DIER.UIE</text>
  <text x="650" y="230" fill="currentColor">→ NVIC/ISR</text>
  <g stroke="currentColor" fill="none">
    <path d="M100 117 L140 117"/><path d="M270 117 L320 117"/><path d="M470 117 L520 117"/>
    <path d="M610 117 L640 117"/><path d="M685 134 L685 190"/><path d="M565 74 L565 100"/>
    <path d="M395 190 L395 134"/>
    <path d="M205 74 L205 100" stroke-dasharray="5 3"/>
    <path d="M395 250 L395 224" stroke-dasharray="5 3"/>
    <path d="M540 134 C 540 170, 300 170, 230 134" stroke-dasharray="5 3"/>
  </g>
  <text x="212" y="92" fill="currentColor" font-size="12">UEV 때 복사</text>
  <text x="402" y="244" fill="currentColor" font-size="12">ARPE=1: UEV 때 복사</text>
  <text x="402" y="183" fill="currentColor" font-size="12">ARPE=0: 쓰는 즉시</text>
  <text x="200" y="196" fill="currentColor" font-size="12">UEV → shadow 갱신, CNT=0</text>
  <text x="10" y="160" fill="currentColor" font-size="12">CNT == ARR_sh 다음 tick</text>
  <text x="10" y="176" fill="currentColor" font-size="12">→ CNT=0 + UEV</text>
  <text x="10" y="210" fill="currentColor" font-size="12">주기 N = (PSC+1)(ARR+1) 클럭</text>
  <text x="10" y="228" fill="currentColor" font-size="12">84 MHz, PSC=83, ARR=999</text>
  <text x="10" y="246" fill="currentColor" font-size="12">→ 84·1000 = 84000 클럭 = 1 ms</text>
</svg>
```

그림의 점선이 이 레슨의 절반이다. 소프트웨어가 쓰는 PSC·ARR 레지스터는 **preload** 이고, 카운터를 실제로 움직이는 것은 **shadow** 다. 둘은 UEV 때만 맞춰진다 (ARR 은 ARPE=0 이면 즉시). 그래서 "PSC 를 썼는데 첫 주기에 안 먹는다" 가 생긴다.

### Part A — 오차 최소 (PSC, ARR) 찾기

`tim_calc(f_tim_hz, upd_mHz, lim, &cfg)` — 목표 업데이트 주파수를 **밀리헤르츠(mHz, 1 Hz = 1000 mHz — MHz 와 혼동 주의)** 정수로 받는다 (0.5 Hz, 44.1 kHz 도 정확히 표현). N* = 1000·f_tim / upd_mHz 를 (PSC+1)(ARR+1) 로 가장 가깝게 만든다.

- 순위 규칙: ① 주기 오차 |N − N*| 최소 ② **ARR 최대** (카운터 분해능 — 나중에 PWM 듀티 분해능, BM-M05) ③ PSC 최소.
- 결과: PSC, ARR, N, exact 여부, 주기 오차 ppm, 실제 주파수(mHz).
- 불가능: N* < 2 (ARR=0 이면 카운터가 멈추므로 ARR ≥ 1) → `TIM_E_TOO_FAST`, N* > 65536² → `TIM_E_TOO_SLOW`.
- 검증: 고정 벡터(84 MHz → 1 kHz, 1 Hz, 50 Hz, 44.1 kHz, 13 Hz, 소수 N*), 경계(N* = 2³² 정확히), 그리고 **property test 두 개** — 무작위 300 목표에서 "모든 PSC 를 훑는" 기준과 비교, 8비트 타이머에서 (p, q) 쌍 전부를 보는 brute force 와 비교.

### Part B — mock 타이머 모델과 결함 재현

BM-B02 의 mock 코어(레지스터 표 + 훅 + 접근 로그)의 축약판 위에 TIM3 모델을 만든다.

- `tim_update_event(by_ug)` — preload → shadow, CNT = 0, 프리스케일러 카운터 = 0, UIF 세트 (UG 이면서 CR1.URS = 1 이면 안 세움).
- `tim_clock_once()` — 타이머 클럭 1개: 프리스케일러 → 카운터 → `CNT == ARR` 이면 UEV. 그 뒤 `UIF && UIE` 면 ISR 호출 (NVIC 대용, 레벨 트리거).
- 레지스터 규칙(훅, 주어짐): SR 은 rc_w0, EGR 은 쓰기 전용(읽으면 0), PSC 는 preload, ARR 은 ARPE 에 따라.
- 결함 재현: PSC=83, ARR=999 를 레지스터로 직접 설정하고 ① UG 없이 ARPE=0 ② UG 없이 ARPE=1 ③ UG 후 UIF 안 지우고 UIE ④ ISR 이 UIF 를 안 지움 — 각각 첫 ISR 시각과 ISR 횟수를 클럭 단위로 단언한다.

### Part C — 드라이버로 주기 인터럽트 쓰기

- `tim_start_periodic(base, f_tim, upd_mHz, &cfg)` — 정석 초기화 순서. 접근 로그로 순서를 검사한다.
- `TIM3_IRQHandler()` — UIF 를 rc_w0 규칙대로 지우고 콜백 호출.
- `tim_set_rate(...)` — 동작 중 주기 변경. 지금 주기는 옛 값으로 끝나고 다음 주기부터 새 PSC/ARR 이 **동시에** 적용돼야 한다.
- 검증: 1 kHz 10 주기 = 정확히 84000 클럭 간격, 50 Hz = 1 680 000 클럭, 1 kHz → 10 kHz 변경, 그리고 같은 변경을 ARPE=0 으로 했을 때의 글리치 주기.

## 2. Problem (English)

> **Periodic interrupt from a 16-bit timer.**
>
> 1. Implement `int tim_calc(uint32_t f_tim_hz, uint64_t upd_mHz, const tim_limits_t *lim, tim_cfg_t *cfg)`.
>    The update rate is `f_tim / ((PSC+1)(ARR+1))`, `PSC ≤ lim->psc_max (≤ 0xFFFF)`, `1 ≤ ARR ≤ lim->arr_max`
>    (ARR = 0 stops the counter). Target is in milli-hertz. Return the pair minimizing the absolute period
>    error; break ties by the largest ARR, then the smallest PSC. Report exact/inexact and the period error in ppm.
>    Use integer arithmetic only. Return `TIM_E_TOO_FAST` / `TIM_E_TOO_SLOW` / `TIM_E_ARG` when appropriate.
>    - `tim_calc(84000000, 1000000 /*1 kHz*/, &TIM16, &c)` → `PSC=1, ARR=41999`, exact
>    - `tim_calc(84000000, 44100000 /*44.1 kHz*/, &TIM16, &c)` → `PSC=0, ARR=1904`, +125 ppm, 44094.488 Hz
>    - `tim_calc(84000000, 10 /*0.01 Hz*/, &TIM16, &c)` → `TIM_E_TOO_SLOW` (OK on a 32-bit timer: `PSC=1, ARR=4199999999`)
> 2. Model the timer: PSC is always preloaded; ARR is preloaded only when `CR1.ARPE=1`; an update event
>    (overflow or `EGR.UG`) copies preload → shadow, clears CNT and the prescaler counter, and sets `SR.UIF`
>    (not for UG when `CR1.URS=1`). `SR` is rc_w0.
> 3. Write `tim_start_periodic()`, the ISR, and `tim_set_rate()`. Show that without `EGR.UG` the first period
>    is wrong, that `UG` creates a spurious first interrupt unless `UIF` is cleared before `UIE` is set, and that
>    changing ARR on the fly with `ARPE=0` can produce a period of almost 65536 ticks.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "4/45 passed" — TODO 6곳(A, B1, B2, C1, C2, C3)을 채워 45/45 를 만든다
./solution -v     # (정답 빌드 후) 계산 표 + PSC=1/ARR=2 클럭 트레이스
```

시작 코드의 C1·C2 는 "흔한 첫 시도" 로 채워져 있다 (UG 없음, UIF 안 지움). 모델(B1·B2)만 먼저 완성하고 돌려 보면 이 첫 시도가 어떻게 틀리는지 숫자로 나온다 — 첫 ISR 이 **42000 클럭**(반 주기)에 오고, 그 뒤 UIF 가 계속 서 있어 840 000 클럭 동안 ISR 이 **798 001 번** 불린다 (실측).

타깃 빌드: 파일 맨 위 `#define UNIT_TEST 1` 을 지우면 `REG_READ/REG_WRITE` 가 `*(volatile uint32_t *)addr` 가 되고 드라이버(Part A·C) 코드는 한 줄도 안 바뀐다 (mock·테스트는 떼어 낸다 — seam 구조는 BM-B02). 실제 칩에서는 그 전에 RCC 클럭 켜기(`RCC_APB1ENR.TIM3EN`, BM-B03)와 NVIC 활성화(BM-M01)가 필요하다.

실제로 solution.c 에서 Part A·C 와 seam 만 떼어 (`UNIT_TEST` 없이, 드라이버 함수는 non-static) `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -ffreestanding -O2 -c` 로 빌드하고 `xcrun llvm-objdump -d` 로 본 ISR (이 잘라낸 파일에서는 `g_tim3_cb` 를 아무도 설정하지 않아 콜백 호출은 컴파일러가 지웠다):

```text
00000300 <TIM3_IRQHandler>:
     300: f240 4010    	movw	r0, #0x410
     304: f2c4 0000    	movt	r0, #0x4000          ; r0 = 0x40000410 = TIM3_SR
     308: 6801         	ldr	r1, [r0]
     30a: 07c9         	lsls	r1, r1, #0x1f        ; UIF(bit0) 검사
     30c: bf1c         	itt	ne
     30e: f06f 0101    	mvnne	r1, #0x1             ; r1 = ~1 = 0xFFFFFFFE
     312: 6001         	strne	r1, [r0]             ; SR = ~UIF — 읽기 1번, 쓰기 1번
     314: 4770         	bx	lr
```

같은 오브젝트의 재배치(relocation)에는 `__aeabi_uldivmod` 3곳, `__aeabi_ldivmod` 1곳이 보인다 (text 866 B). Cortex-M4 에는 64비트 나눗셈 명령이 없어 `tim_calc` 의 uint64 나눗셈은 라이브러리 호출이고, 루프가 최악 65536 번 돈다. 그래서 이 계산은 **초기화 때 한 번** 하거나, 고정 주기라면 호스트에서 미리 계산해 상수로 넣는 것이 보통이다 (C++ 이면 `constexpr`, CPP-M13 참고).

## 4. 힌트

<details>
<summary>힌트 1 — N* 를 정수 두 개로 들고 다녀라</summary>

f_tim 과 목표가 둘 다 정수이므로 N* = F / T (F = 1000·f_tim, T = upd_mHz) 를 **분수 그대로** 쓴다. 후보 N 의 주기 오차는 |N − F/T| 이고, T 를 곱하면 |N·T − F| — 부동소수점 없이 정확하게 비교할 수 있다. f_tim ≤ 1 GHz 로 막으면 F ≤ 10¹² 이라 모든 곱이 uint64 안에 들어간다.

</details>

<details>
<summary>힌트 2 — PSC 를 고정하면 ARR 은 두 후보뿐</summary>

p = PSC+1 을 정하면 오차 |p·q·T − F| 는 q 에 대해 V 자(볼록)다. 최솟값은 q = ⌊N*/p⌋ 또는 ⌈N*/p⌉ 이고, 범위 [2, ARR_max+1] 밖이면 끝으로 잘라(clamp) 넣으면 된다. 그러면 p 를 1..65536 까지 한 번 훑는 O(65536) 검색 — 마이크로초 단위다.

</details>

<details>
<summary>힌트 3 — 조기 종료 두 가지</summary>

p 를 작은 쪽부터 보면 처음 만나는 정확해(오차 0)가 곧 ARR 이 가장 큰 정확해다 (N 고정이면 q = N/p 는 p 에 반비례). 또 ⌊N*/p⌋ < 2 가 되면 이후 p 는 N = 2p 가 N* 에서 멀어지기만 하므로 멈춘다.

</details>

<details>
<summary>힌트 4 — 모델의 한 클럭</summary>

`if (pc == psc_sh) { pc = 0; 카운터 한 칸 } else pc++;` — 카운터 한 칸은 `CNT == arr_sh` 면 UEV(CNT=0), 아니면 CNT+1. 이렇게 하면 enable(또는 UG) 직후부터 정확히 (PSC+1)(ARR+1) 번째 클럭에 UEV 가 난다. ISR 검사는 CEN 과 무관하게 매 클럭 — NVIC 는 카운터가 서 있어도 펜딩 플래그를 본다.

</details>

<details>
<summary>힌트 5 — 초기화 순서와 rc_w0</summary>

PSC/ARR 를 쓴 뒤 `EGR = UG` 로 shadow 를 채우면 UIF 도 같이 선다. UIE 를 켜기 **전에** `SR = ~UIF` (UIF 자리만 0, 나머지는 1 → rc_w0 에서 1 쓰기는 무효). `SR &= ~UIF` 는 읽기와 쓰기 사이에 하드웨어가 세운 다른 플래그를 지울 수 있다 (BM-B04).

</details>

## 5. 풀이

### Part A — 공식의 +1 두 개

| 단계 | 레지스터 값 | 실제 나눗수 | 이유 |
|---|---|---|---|
| 프리스케일러 | PSC = 0 … 65535 | PSC + 1 (1 … 65536) | PSC=0 이 "나누지 않음" 이 되도록 정의돼 있다 |
| 카운터 | ARR = 1 … 65535 | ARR + 1 (2 … 65536) | CNT 가 0, 1, …, ARR 까지 **ARR+1 개 상태** 를 돈다 |
| 합 | — | N = (PSC+1)(ARR+1) | 한 주기의 타이머 클럭 수 → f_update = f_tim / N |

84 MHz 는 STM32F407 을 168 MHz 로 돌릴 때 APB1 = 42 MHz, 그리고 "APB 프리스케일러 ≠ 1 이면 타이머 클럭은 ×2" 규칙 (BM-B10 참고, RM0090 기준 대표값) 으로 나온 TIM3 의 클럭이다. f_tim 을 PCLK1(42 MHz) 로 잘못 넣으면 모든 주기가 정확히 2배가 된다 — 브링업에서 "타이머가 반 속도로 돈다" 의 1순위 원인.

**검색.** p = PSC+1 을 고정하면 오차 e(q) = |p·q·T − F| 는 q 에 대한 볼록 함수이므로 정수 최솟값은 ⌊N*/p⌋ 나 그 +1 이다. 범위 밖이면 가까운 끝으로 자른 값이 그 범위 안 최솟값이다 (볼록 함수의 구간 최솟값은 꼭짓점을 구간에 사영한 점). 모든 p 에 대해 후보 둘을 보고 순위 규칙으로 고르면 끝:

```c
for (uint64_t p = 1u; p <= pmax; p++) {
    uint64_t q0 = F / (T * p);                       /* floor(N* / p) */
    for (k = 0..1) { q = clamp(q0 + k, 2, qmax); e = |p·q·T − F|;
                     if (e < best_e || (e == best_e && q > bq)) 갱신; }
    if (best_e == 0u) break;    /* 처음 정확해 = 가장 큰 ARR */
    if (q0 < 2u) break;         /* 이후 N = 2p 는 멀어지기만 */
}
```

정확히 비교하기 위해 정수 범위를 따져 두자. f_tim ≤ 10⁹ → F ≤ 10¹². TOO_FAST 를 먼저 거르면 T ≤ F/2 이고, 후보의 p·q·T ≤ F + p·T ≤ F·(1 + 65536/2) ≈ 3.3·10¹⁶ — uint64(1.8·10¹⁹) 안이다. TOO_SLOW 판정 `N* > Nmax ⇔ F > T·Nmax` 는 곱이 넘칠 수 있으므로 `(F − 1)/T ≥ Nmax` 로 바꿔 쓴다 (정수에서 F > a ⇔ F − 1 ≥ a ⇔ ⌊(F−1)/T⌋ ≥ Nmax).

`./solution -v` 실측 표 (TIM16 제약):

```text
f_tim[Hz]   target    ret  PSC    ARR         N           exact err_ppm  actual[mHz]
84000000    1 kHz       0  1      41999       84000       1     0        1000000
84000000    1 Hz        0  1343   62499       84000000    1     0        1000
84000000    50 Hz       0  27     59999       1680000     1     0        50000
84000000    44.1 kHz    0  0      1904        1905        0     125      44094488
84000000    13 Hz       0  502    12845       6461538     0     0        13000
84000000    7 Hz        0  191    62499       12000000    1     0        7000
84000000    0.01 Hz    -3  -
84000000    42 MHz      0  0      1           2           1     0        42000000000
84000000    50 MHz     -2  -
16000000    1 Hz        0  249    63999       16000000    1     0        1000
72000000    3 Hz        0  374    63999       24000000    1     0        3000
1000003     1 Hz        0  52     18867       1000004     0     1        1000
```

읽는 법:

- **1 kHz → PSC=1, ARR=41999.** 84000 을 만드는 16비트 쌍은 94 개 (PSC=1/ARR=41999 … PSC=41999/ARR=1, 파이썬으로 센 값). 우리 규칙은 ARR 최대를 고른다. CubeMX 예제에서 흔한 PSC=83/ARR=999 (1 µs tick) 도 정확해지만 분해능이 42배 거칠다 — 8. Q&A 의 Q3.
- **1 Hz → PSC=1343, ARR=62499.** N* = 84·10⁶ 은 65536 보다 크므로 p ≥ ⌈84·10⁶/65536⌉ = 1282. 84·10⁶ = 2⁸·3·5⁶·7 의 약수 중 1282 이상인 가장 작은 것이 1344 (= 2⁶·3·7) → q = 62500.
- **44.1 kHz** 는 N* = 1904.76… 이라 정확해가 없다. N = 1905 → 주기 +125 ppm (주파수 44 094.488 Hz). 1시간이면 44 100·3600 − 84·10⁶·3600/1905 ≈ 19 842 샘플이 모자란다 — 오디오라면 들린다. 해결은 연습 4 (ARR 디더링) 또는 PLL 을 바꿔 f_tim 을 44.1 kHz 의 배수로 (BM-B10).
- **13 Hz** 는 부정확하지만 오차가 −0.07 ppm (반올림 0). 크리스털(HSE) 자체 허용오차가 보통 수십 ppm (데이터시트 확인)이므로 의미 없는 오차다 — "정확" 보다 "오차가 클럭 원천보다 작은가" 가 실무 기준.
- **1 000 003 Hz → 1 Hz** 는 N* 가 소수(prime)라 어떤 (p, q) 로도 정확히 못 만든다. N = 1 000 004 = 53·18868 로 1 클럭(+1 ppm) 차이.
- **0.01 Hz** 는 N* = 8.4·10⁹ > 2³² 라 16비트로 불가능 → 32비트 ARR 의 TIM2/TIM5 (F4 기준) 면 PSC=1, ARR=4 199 999 999 (연습 3).

**property test 두 개.** ① 무작위 300 목표(f_tim 6종 + 무작위, N* 를 2..2³² 와 2..70000 에서 섞어 뽑음)에 대해 조기 종료가 없고 후보를 하나 더 보는 `ref_sweep()` 과 PSC/ARR/반환값이 모두 같다. ② 8비트 타이머(PSC, ARR ≤ 255)로 줄여 (p, q) 65 280 쌍 **전부** 를 보는 `ref_brute8()` 과 300 목표에서 일치. 두 번째가 진짜 독립 검증이다 — "후보 둘이면 충분하다" 는 볼록성 논증을 실험으로 확인한다.

### Part B — 모델 한 클럭씩: PSC=1, ARR=2 트레이스

`./solution -v` 의 트레이스 (UG 로 shadow 를 채우고 UIF 를 지운 뒤 CEN=1, UIF 는 출력 후 테스트가 지움):

| clk | 프리스케일러 pc | CNT | UIF | 무슨 일 |
|---|---|---|---|---|
| 1 | 1 | 0 | 0 | pc 0 → 1 (PSC_sh=1 이라 아직 카운트 안 함) |
| 2 | 0 | 1 | 0 | pc == PSC → pc=0, CNT 0 → 1 |
| 3 | 1 | 1 | 0 | |
| 4 | 0 | 2 | 0 | CNT 1 → 2 (= ARR) |
| 5 | 1 | 2 | 0 | |
| 6 | 0 | 0 | **1** | CNT == ARR 에서 한 칸 → CNT=0, **UEV**, UIF=1 |
| 7 | 1 | 0 | 0 | 다음 주기 시작 |
| 8 | 0 | 1 | 0 | |

UEV 는 6 = (1+1)(2+1) 번째 클럭. 카운터가 ARR 값에 "도달" 할 때가 아니라 ARR 에서 **다음 칸으로 넘어갈 때** 이벤트가 난다 — 그래서 상태 수가 ARR+1 이다.

모델의 규칙 (RM0090 의 업카운트 설명을 단순화한 것, URS·UDIS 세부는 RM 확인 권장):

| 레지스터 | 접근 | 모델 동작 |
|---|---|---|
| CR1 (CEN 0, URS 2, ARPE 7) | rw | 저장 |
| DIER (UIE 0) | rw | 저장 |
| SR (UIF 0) | rc_w0 | `new = old & val` — 0 을 쓴 비트만 지워짐, 1 쓰기는 무효 |
| EGR (UG 0) | w | UG=1 이면 UEV(by_ug), 저장값은 항상 0 (읽으면 0) |
| CNT | rw | 저장 (모델이 직접 증가) |
| PSC | rw, **항상 preload** | 저장만. shadow 는 UEV 때 |
| ARR | rw, ARPE 에 따라 | ARPE=0 이면 shadow 도 즉시, ARPE=1 이면 UEV 때. 리셋값은 모델에서 최대(0xFFFF) — 실제 리셋값은 RM 확인 |

### Part B — 첫 주기 결함 네 가지 (PSC=83, ARR=999, 84 MHz → 기대 84000 클럭)

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">첫 UIF/ISR 시각 (mock 실측, 단위: 타이머 클럭, 축척 아님)</text>
  <g stroke="currentColor" fill="none">
    <path d="M250 60 L730 60"/><path d="M250 125 L730 125"/><path d="M250 190 L730 190"/><path d="M250 255 L730 255"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="56">① ARPE=0, UG 없음</text><text x="10" y="74" font-size="12">PSC_sh 가 리셋값 0</text>
    <text x="10" y="121">② ARPE=1, UG 없음</text><text x="10" y="139" font-size="12">PSC_sh 0, ARR_sh 0xFFFF</text>
    <text x="10" y="186">③ UG, UIF 안 지움</text><text x="10" y="204" font-size="12">UG 가 세운 UIF 그대로</text>
    <text x="10" y="251">④ 정석</text><text x="10" y="269" font-size="12">UG → SR=~UIF → UIE → CEN</text>
  </g>
  <g class="f-warn" stroke="currentColor">
    <rect x="296" y="50" width="8" height="20"/><rect x="616" y="50" width="8" height="20"/>
    <rect x="436" y="115" width="8" height="20"/><rect x="696" y="115" width="8" height="20"/>
    <rect x="256" y="180" width="8" height="20"/>
  </g>
  <g class="f-ok" stroke="currentColor">
    <rect x="536" y="180" width="8" height="20"/><rect x="536" y="245" width="8" height="20"/><rect x="696" y="245" width="8" height="20"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="300" y="92">1000</text><text x="620" y="92">85000</text>
    <text x="440" y="157">65536</text><text x="700" y="157">149536</text>
    <text x="260" y="222">1 (가짜)</text><text x="540" y="222">84000</text>
    <text x="540" y="287">84000</text><text x="700" y="287">168000</text>
  </g>
  <text x="10" y="310" fill="currentColor" font-size="12">주황 = 틀린 시각, 초록 = 기대값. ①② 는 두 번째 주기부터 84000 간격으로 정상화된다.</text>
</svg>
```

| 경우 | 첫 ISR | 둘째 ISR | 원인 |
|---|---|---|---|
| ① ARPE=0, UG 없음 | 1000 | 85000 | ARR 은 즉시 반영(999)됐지만 PSC 는 preload → 첫 주기 PSC_sh = 0 → 1·1000 |
| ② ARPE=1, UG 없음 | 65536 | 149536 | PSC_sh = 0, ARR_sh = 리셋값 0xFFFF → 1·65536. 첫 UEV 에서야 83/999 로드 |
| ③ UG 후 UIF 안 지움 | **1** | 84000 | UG 가 shadow 를 채우면서 UIF 도 세움 → UIE 켜자마자 ISR (가짜 첫 인터럽트) |
| ④ 정석 | 84000 | 168000 | UEV 는 UG 1 + overflow 3 = 4 회 (`g_tim.uev == 4`) |
| ⑤ ISR 이 UIF 안 지움 | — | — | 84000+99 클럭 동안 ISR **100 번** (첫 UEV 이후 매 클럭 재진입) |

①이 "첫 주기가 짧다", ②가 "첫 주기가 길다" 의 정체다. 둘 다 원인은 하나: **PSC 는 언제나 preload** 다 (RM0090: 새 프리스케일러 비는 다음 UEV 부터 적용 — 대표 문장, RM 확인 권장). UG 는 "지금 당장 UEV 를 한 번 만들어 preload 를 shadow 로 밀어 넣는" 소프트웨어 버튼이다.

③의 UIF 는 RM 의 정의대로라면 URS=0 일 때 UG 도 UIF 를 세운다 (URS=1 이면 overflow/underflow 만 세운다 — 연습 2). 그래서 HAL 의 `HAL_TIM_Base_Init()` 도 UG 를 쓴 직후 UIF 를 지우는 코드가 들어 있다 (HAL 버전마다 다름, 소스 확인 권장).

### Part C — 정석 초기화와 접근 순서

```c
REG_WRITE(TIM_CR1(base), 0u);                       /* 1. 멈춘다 (CEN=0) */
REG_WRITE(TIM_PSC(base), cfg->psc);                 /* 2. preload 에 값 */
REG_WRITE(TIM_ARR(base), cfg->arr);
REG_WRITE(TIM_CR1(base), TIM_CR1_ARPE);             /* 3. 이후 ARR 변경도 버퍼링 */
REG_WRITE(TIM_EGR(base), TIM_EGR_UG);               /* 4. shadow 로드, CNT=0, UIF=1 */
REG_WRITE(TIM_SR(base), ~TIM_SR_UIF);               /* 5. 가짜 UIF 제거 (rc_w0) */
REG_WRITE(TIM_DIER(base), TIM_DIER_UIE);            /* 6. 그 다음에야 인터럽트 허용 */
REG_WRITE(TIM_CR1(base), TIM_CR1_ARPE | TIM_CR1_CEN);   /* 7. 출발 */
```

테스트는 접근 로그에서 PSC·ARR 쓰기 < UG < SR 지우기 < UIE < CEN 순서를 단언하고, 10 주기 동안 ISR 시각이 정확히 84000·k (k = 1..10), `isr_calls == 10` (폭주·가짜 없음) 임을 본다. 50 Hz 는 PSC=27, ARR=59999 → 1 680 000 클럭 간격 (3 주기, 5 040 000 클럭에 세 번째).

ISR 은 두 줄이 핵심이다:

```c
if (REG_READ(TIM_SR(TIM3_BASE)) & TIM_SR_UIF) {
    REG_WRITE(TIM_SR(TIM3_BASE), ~TIM_SR_UIF);    /* UIF 자리만 0 — 다른 플래그(CCxIF)는 1 을 써서 보존 */
    if (g_tim3_cb) g_tim3_cb();
}
```

`~TIM_SR_UIF` 는 0xFFFFFFFE 다. rc_w0 레지스터에 1 을 쓰는 것은 "건드리지 않음" 이므로 UIF 만 지워진다. 같은 일을 `SR &= ~UIF` 로 하면 읽기~쓰기 사이에 하드웨어가 세운 CC1IF 를, 읽은 값이 0 이었다는 이유로 0 으로 덮어 지운다 (BM-B04 의 RMW 함정).

### Part C — 동작 중 주기 변경: ARPE 가 하는 일

1 kHz (PSC=1, ARR=41999) 로 돌다가 3 주기 + 20000 클럭 시점(CNT = 10000)에 10 kHz (PSC=0, ARR=8399) 로 바꾼다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">CNT 톱니파: 클럭 252000 에서 주기 시작, 272000 에 변경 (mock 실측 시각, 축척 아님)</text>
  <text x="10" y="50" fill="currentColor">ARPE=1 (tim_set_rate)</text>
  <g stroke="currentColor" fill="none">
    <path d="M40 140 L730 140"/>
    <path d="M60 140 L300 75 L300 140 L340 125 L340 140 L380 125 L380 140 L420 125 L420 140"/>
    <path d="M140 60 L140 145" stroke-dasharray="4 3"/>
  </g>
  <text x="62" y="160" fill="currentColor" font-family="monospace" font-size="12">252000</text>
  <text x="118" y="56" fill="currentColor" font-size="12">write @272000 (CNT=10000)</text>
  <text x="270" y="160" fill="currentColor" font-family="monospace" font-size="12">336000</text>
  <text x="330" y="176" fill="currentColor" font-family="monospace" font-size="12">344400 …</text>
  <text x="450" y="90" fill="currentColor" font-size="12">옛 설정으로 끝까지 (84000 클럭),</text>
  <text x="450" y="108" fill="currentColor" font-size="12">다음 UEV 에서 PSC·ARR 동시 전환</text>
  <text x="450" y="126" fill="currentColor" font-size="12">→ 8400 클럭 주기</text>
  <text x="10" y="205" fill="currentColor">ARPE=0 (결함 주입)</text>
  <g stroke="currentColor" fill="none">
    <path d="M40 310 L730 310"/>
    <path d="M60 310 L140 293 L520 225 L520 310 L600 290 L600 310 L640 290 L640 310"/>
    <path d="M140 215 L140 315" stroke-dasharray="4 3"/>
    <path d="M40 263 L730 263" stroke-dasharray="2 4"/>
  </g>
  <text x="600" y="258" fill="currentColor" font-size="12" font-family="monospace">41999 높이</text>
  <text x="62" y="326" fill="currentColor" font-family="monospace" font-size="12">252000</text>
  <text x="150" y="214" fill="currentColor" font-size="12">ARR=8399 즉시 적용 → CNT(10000) &gt; ARR → 65535 까지 감</text>
  <text x="526" y="244" fill="currentColor" font-family="monospace" font-size="12">65535→0</text>
  <text x="560" y="326" fill="currentColor" font-family="monospace" font-size="12">399872 (147872 클럭 뒤)</text>
</svg>
```

| | 변경 후 첫 UEV | 그 주기 길이 | 이후 |
|---|---|---|---|
| ARPE=1 (`tim_set_rate`) | 336000 | 84000 (옛 설정 그대로 완주) | 8400 간격: 344400, 352800, 361200 |
| ARPE=0, ARR 8399 < CNT 10000 | 399872 | **147872** = 20000 + 2·(65536 − 10000 + 8400) | 8400 간격 |
| ARPE=0, 500 Hz (PSC=2, ARR=55999) | — | **112000** = 2·56000 (새 ARR × 옛 PSC, 혼합 주기) | 168000 간격 |

마지막 줄은 `/tmp` 에서 같은 모델로 따로 돌린 실측이다: 1 kHz 도 500 Hz 도 아닌 주기가 한 번 섞인다. ARPE=0 에서는 ARR 만 즉시 바뀌고 PSC 는 다음 UEV 를 기다리기 때문이다. ARPE=1 이면 둘이 **같은 UEV 에서 함께** 바뀐다 — 이것이 ARPE 의 존재 이유다. 실제 칩에서 CNT > ARR 이 되었을 때 카운터가 최대값까지 세고 0 으로 감긴 뒤 (감길 때는 UEV 없이) ARR 에서 UEV 를 낸다는 동작은 RM 의 업카운터 설명에서 나오는 결론 — 모델의 가정이니 RM 확인 권장.

`tim_set_rate()` 에 UG 를 쓰지 않는 이유: UG 는 카운터를 0 으로 되돌려 진행 중인 주기를 잘라 버린다 (그 순간 ISR 도 한 번 더). 남은 위험 하나: PSC 와 ARR 쓰기 **사이에** UEV 가 끼면 한 주기만 "새 PSC × 옛 ARR" 가 된다. 정석 회피는 주기 변경을 ISR 안(방금 UEV 가 났으니 거의 한 주기의 여유)에서 하거나, 쓰는 동안 CR1.UDIS 로 UEV 를 막는 것이다 (UDIS 동안 overflow 인터럽트도 사라지므로 trade-off — RM 확인).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 45/45 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 45/45, 경고 없음
```

실행 시간 0.11 s (O2), ASan/UBSan 0.15 s — property test 600 개와 모델 약 800 만 클럭 포함.

## 7. 자주 하는 실수

1. **f_tim 을 PCLK 로 넣는다.** APB1 = 42 MHz 인데 타이머 클럭은 84 MHz (APB 프리스케일러 ≠ 1 → ×2). 계산이 정확히 2배 어긋난다.
   → `f_tim = (apb_presc == 1) ? pclk : 2 * pclk;` (BM-B10, RM 의 클럭 트리 그림 확인).
2. **`PSC = f_tim / f_cnt` (−1 을 잊음).** PSC=84 면 84 MHz/85 = 988 235 Hz tick → 1 kHz 가 −1.2 % 틀린다.
   → `PSC = f_tim / f_cnt − 1`, `ARR = ticks − 1`.
3. **ARR = 0 으로 "가장 빠른 타이머" 를 만든다.** 카운터가 멈춘다 (RM0090 의 ARR 설명: "counter is blocked while the auto-reload value is null" — 대표 문장, 확인 권장).
   → N ≥ 2, 즉 f_update ≤ f_tim/2. `tim_calc` 는 `TIM_E_TOO_FAST`.
4. **UG 없이 시작한다.** 첫 주기가 리셋값 PSC(0)·ARR 로 돈다 (Part B ①②).
   → PSC/ARR 를 쓴 뒤 `EGR = UG`.
5. **UG 후 UIF 를 안 지우고 UIE 를 켠다.** 켜자마자 ISR 한 번 (③).
   → `SR = ~UIF` 후 UIE, 또는 UG 동안 CR1.URS = 1 (연습 2).
6. **ISR 에서 UIF 를 안 지우거나 `SR = 0` 으로 지운다.** 안 지우면 무한 재진입 (모델: 99 클럭에 100 번). `SR = 0` 은 아직 처리 안 한 CCxIF 까지 지운다.
   → `SR = ~UIF` (rc_w0 에서 1 쓰기는 무효).
7. **ARPE=0 으로 동작 중 ARR 를 줄인다.** CNT > ARR 이면 최대값까지 돌아 한 주기가 거의 65536 tick (Part C 표).
   → 동작 중 변경은 ARPE=1, 또는 ISR 안에서.
8. **곱을 uint32 로 계산한다.** `(psc+1)*(arr+1)` 은 최대 2³² — uint32 에서 0 으로 감긴다. `1000*f_tim` 도 넘친다.
   → `uint64_t` 로, 범위를 미리 증명해 두기 (풀이 Part A).

## 8. 면접 Q&A

<details>
<summary>Q1. 84 MHz 타이머로 1 kHz 인터럽트 — PSC 와 ARR 은? 다른 답도 있나?</summary>

N = 84 000 = (PSC+1)(ARR+1). 사람이 흔히 고르는 답은 PSC=83 (1 MHz = 1 µs tick), ARR=999. 분해능 최대를 원하면 PSC=1, ARR=41999. 16비트 범위에서 84000 을 만드는 쌍은 94 개 있다. 꼬리질문 "왜 PSC=0, ARR=83999 는 안 되나?" → ARR 이 16비트(≤ 65535)라서. TIM2/TIM5 같은 32비트 타이머면 된다.

</details>

<details>
<summary>Q2. 공식에 왜 +1 이 두 번 들어가나?</summary>

프리스케일러는 PSC=0 일 때 "1로 나눔" 이 되도록 정의돼 있어서 나눗수가 PSC+1 이다. 카운터는 0..ARR 을 세니 상태가 ARR+1 개이고, UEV 는 ARR 에서 0 으로 넘어갈 때 난다 (모델 트레이스에서 PSC=1, ARR=2 → 6 클럭). 레지스터 값 0 이 의미를 갖게 하려는 하드웨어 관례이고, 분주기·baud 레지스터 대부분이 같은 식이다.

</details>

<details>
<summary>Q3. PSC 를 작게(ARR 크게) 하는 것과 PSC=83 처럼 "1 µs tick" 으로 하는 것, 어느 쪽?</summary>

용도에 따라 다르다. ① 주기 인터럽트만 필요하면 둘 다 정확하고 차이가 없다. ② 같은 타이머로 PWM 이나 출력 비교(output compare)를 하면 ARR+1 이 듀티 단계 수다: 41 999 단계 vs 1 000 단계 (BM-M05). ③ CNT 를 시간 측정용으로 읽는다면 1 µs tick 이 사람이 읽기 쉽고 나눗셈이 없다. ④ 부정확한 목표는 ARR 이 클수록 선택지가 촘촘해 오차를 줄이기 쉽다. 이 레슨의 tie-break 는 ②④ 때문에 ARR 최대. 어느 쪽이든 **규칙을 명시** 하는 것이 면접 포인트다.

</details>

<details>
<summary>Q4. 설정했는데 첫 인터럽트만 시간이 틀린다. 왜? 어떻게 고치나?</summary>

PSC 는 항상 preload 레지스터라 쓴 값이 다음 UEV 에서야 shadow 로 옮겨진다. 처음 켤 때는 아직 UEV 가 없었으니 첫 주기는 리셋값 PSC(0) 로 돈다 → 첫 주기가 PSC+1 배 짧다 (모델: 84000 대신 1000 클럭). ARPE=1 이면 ARR 도 리셋값이라 65536 클럭이 된다. 고침: PSC/ARR 를 쓴 뒤 `EGR.UG = 1` 로 UEV 를 한 번 만들어 shadow 를 채운다.

</details>

<details>
<summary>Q5. 그런데 UG 를 쓰니까 켜자마자 인터럽트가 한 번 온다. 왜, 그리고 두 가지 고침은?</summary>

UG 도 업데이트 이벤트라서 URS=0 이면 UIF 를 세운다. 이 상태로 UIE 를 켜면 NVIC 가 즉시 펜딩된다 (모델: 1 클럭 뒤 ISR). 고침 ① UG 직후, UIE 켜기 전에 `SR = ~UIF`. ② UG 동안 `CR1.URS = 1` — UIF 는 overflow/underflow 에서만 선다 (연습 2). URS=1 은 slave-mode 리셋이 만드는 UEV 의 인터럽트도 막으므로, 그게 필요한 설계라면 ①을 쓴다 (RM 확인).

</details>

<details>
<summary>Q6. 상태 플래그를 왜 `SR = ~UIF` 로 지우나? `SR &= ~UIF` 는?</summary>

STM32F4 TIM SR 은 rc_w0: 0 을 쓴 비트만 지워지고 1 쓰기는 무시된다. `~UIF` 는 UIF 자리만 0 이라 정확히 그 비트만 지운다 — 한 번의 쓰기라 원자적이다. `SR &= ~UIF` 는 읽기-수정-쓰기라, 읽은 뒤 쓰기 전에 하드웨어가 CC1IF 를 세웠다면 읽은 값의 0 을 다시 써서 그 새 플래그를 지워 버린다 (흔하지만 레이스, BM-B04). 반대로 W1C(rc_w1) 레지스터라면 `REG = FLAG` (1 을 쓴 비트만 지움) 이 같은 역할이다 — 레지스터 접근 형식을 먼저 확인하는 습관.

</details>

<details>
<summary>Q7. ISR 맨 끝에서 UIF 를 지웠더니 가끔 ISR 이 한 번 더 들어온다. 왜?</summary>

Cortex-M 의 스토어는 버스 쓰기 버퍼와 APB 브리지를 거치므로 ISR 리턴(예외 복귀)이 주변장치의 플래그 클리어보다 먼저 끝날 수 있다. 그 순간 NVIC 는 인터럽트 라인이 아직 high 라고 보고 다시 펜딩한다 → 같은 UEV 로 ISR 이 두 번 (tail-chain 재진입). 고침: 플래그를 ISR **처음** 에 지우거나, 끝에서 지웠다면 같은 레지스터를 한 번 다시 읽어 쓰기가 끝나게 하거나 `__DSB()`. 여러 STM32 커뮤니티 글과 ARM 지식 문서에서 다루는 현상이다 (칩·버스 클럭 비율에 따라 다름 — 확인 권장). 이 레슨의 모델은 쓰기가 즉시 반영되므로 이 현상을 재현하지 않는다.

</details>

<details>
<summary>Q8. 0.01 Hz (100 초) 주기가 필요하다. 16비트 타이머로는 N* = 8.4·10⁹ > 2³² 인데?</summary>

선택지: ① 32비트 타이머 (F4 의 TIM2/TIM5: ARR 32비트) → PSC=1, ARR=4 199 999 999 정확 (연습 3). ② 소프트웨어 포스트스케일러: 1 Hz ISR 에서 100 번 세기 — 가장 흔하고 간단. ③ 타이머 체이닝: 한 타이머의 UEV 를 다른 타이머의 클럭으로 (master/slave, TRGO → ITR — BM-M 레벨). ④ 그 정도로 느린 주기는 저전력 RTC 웨이크업 타이머가 맞는 도구인 경우가 많다 (BM-M12). 고속 타이머를 100 초 동안 돌리는 것은 전력 낭비다.

</details>

<details>
<summary>Q9. 타이머 주기는 "정확" 하다. 그러면 실제 시간도 정확한가?</summary>

아니다. 타이머는 f_tim 클럭 수를 정확히 셀 뿐이고, f_tim 의 정확도는 클럭 원천이 정한다: HSE 크리스털은 보통 ±10–50 ppm, 내부 RC(HSI)는 공장 보정 후에도 ±1 % 수준 (데이터시트 확인). 13 Hz 설정의 −0.07 ppm 계산 오차는 크리스털 오차보다 몇 자릿수 작다. 반대로 44.1 kHz 의 +125 ppm 은 크리스털 오차보다 크므로 설계 문제다. 장시간 시간 유지는 32.768 kHz LSE + RTC, 또는 외부 기준(GPS PPS)으로 보정한다.

</details>

## 9. 추가 연습

### 연습 1 — 주기(µs)로 받는 API

`tim_calc_period_us(f_tim, period_us, lim, &cfg)` 를 만들어라. 2.5 ms, 1 µs, 100 s 는? 주파수 API 와 같은 목표(1 ms ↔ 1 kHz)에서 결과가 같아야 한다.

<details>
<summary>답 보기</summary>

N* = f_tim·period_us / 10⁶ 이므로 Part A 의 검색을 일반 비율 F/T 로 빼 두면 (`tim_calc_ratio(F, T)`) 주파수 API 는 (1000·f_tim, mHz), 주기 API 는 (f_tim·period_us, 10⁶) 을 넘기면 된다. F 가 최대 10¹⁸ 까지 커지므로 `F < 2T` 를 `F/2 < T` 로 바꿔 쓰고, `q0 = F / T / p` (⌊⌊F/T⌋/p⌋ = ⌊F/(Tp)⌋) 로 T·p 곱을 피한다. 결과 (practice.c 실측): 1 µs → PSC=0, ARR=83 / 2.5 ms → PSC=3, ARR=52499 / 1 s → PSC=1343, ARR=62499 / 100 s → 16비트 TOO_SLOW, 32비트 PSC=1, ARR=4199999999. 1 MHz 에서 1 µs 는 N*=1 → TOO_FAST.

</details>

### 연습 2 — URS=1 로 가짜 첫 인터럽트 없애기

SR 을 한 번도 쓰지 않고 초기화하되, 첫 ISR 이 정확히 한 주기 뒤에 오게 하라.

<details>
<summary>답 보기</summary>

`CR1 = URS` 를 먼저 쓰고 PSC, ARR, ARPE, UG, UIE, CEN 순서. URS=1 이면 UG 는 shadow 만 채우고 UIF 를 세우지 않는다 (모델: UG 직후 UIF=0, PSC_sh=1, ARR_sh=41999). overflow 는 여전히 UIF 를 세우므로 ISR 은 84000·k 에 정확히 3 번 (테스트가 접근 로그에 SR 쓰기가 없음을 단언). URS 는 그대로 켜 둬도 된다 — 단 나중에 UG 로 주기를 강제 재시작할 때 인터럽트가 안 온다는 점을 기억.

</details>

### 연습 3 — 32비트 타이머로 바꾸면

TIM2 (32비트 ARR) 에서 1 Hz, 0.01 Hz, 1 kHz 의 결과는? 모델의 카운터 폭(cnt_mask)만 바꿔 1 kHz 를 돌려 확인하라.

<details>
<summary>답 보기</summary>

`TIM32 = {0xFFFF, 0xFFFFFFFF}`: 1 Hz → PSC=0, ARR=83 999 999 / 0.01 Hz → PSC=1, ARR=4 199 999 999 / 1 kHz → PSC=0, ARR=83 999 (16비트에선 PSC=1 이었다). 모델을 `tim_model_init(0x40000000, 0xFFFFFFFF)` 로 만들면 ARR 쓰기가 83999 그대로 저장되고 (16비트 마스크였다면 잘렸다) ISR 은 84000·k. 실무 팁: 32비트 타이머는 F4 에서 두 개뿐이라 "자유 실행 µs 타임스탬프" 같은 용도로 아껴 두는 경우가 많다.

</details>

### 연습 4 — 44.1 kHz 를 평균적으로 정확하게: ARR 디더링

고정 ARR 은 +125 ppm 이다. PSC=0 에서 매 주기 ARR 을 1904/1905 중에서 골라 **평균 주기가 정확히 N*** 이 되게 하라. 위상 오차가 1 클럭 미만으로 유지되는지 확인하라.

<details>
<summary>답 보기</summary>

N* = 84·10⁹ / 44.1·10⁶ = 40000/21 = 1904 + 16/21 (gcd 로 약분). Bresenham 누산기: 매 주기 `acc += 16; if (acc >= 21) { acc -= 21; N = 1905; } else N = 1904;` → 21 주기마다 1905 가 16 번, 1904 가 5 번, 합 40000 클럭. ISR 이 다음 값을 ARR 에 쓰면 ARPE=1 이라 **다다음** 주기부터 적용되므로, 시작할 때 첫 값 → UG → 둘째 값을 미리 써 둔다. 실측: 21 번째 ISR 이 정확히 40000, 42 번째가 80000 클럭, 모든 k 에서 위상 오차 ∈ (−1, 0] 클럭. 오디오 샘플 클럭, NCO, 소프트웨어 PLL 이 쓰는 방법이고 지터는 1 클럭(12 ns)이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 24/24 passed
```

## 10. 핵심 정리

- f_update = f_tim / ((PSC+1)(ARR+1)). PSC 0..65535, ARR 1..65535 (ARR=0 은 카운터 정지) → N ∈ [2, 2³²]. f_tim 은 APB 프리스케일러 ≠ 1 이면 PCLK×2.
- 검색: p = PSC+1 을 훑고 q 는 ⌊N*/p⌋, ⌈N*/p⌉ 두 후보만 (볼록성), |N·T − F| 정수 비교, 순위 = 오차 → ARR 최대 → PSC 최소. 처음 만난 정확해가 ARR 최대.
- 결과는 exact 여부 + ppm 으로 보고하고 크리스털 오차(수십 ppm)와 비교한다. 정확해가 없으면 디더링이나 PLL 변경.
- PSC 는 항상 preload, ARR 은 ARPE=1 일 때 preload. shadow 는 UEV 때만 바뀐다 → 시작 때 `EGR.UG` 필수.
- UG 도 UIF 를 세운다 (URS=0) → UIE 전에 `SR = ~UIF`, 또는 URS=1.
- 정석 순서: CEN=0 → PSC, ARR → ARPE → UG → SR=~UIF → UIE → CEN. ISR 은 `SR = ~UIF` (rc_w0) 를 먼저.
- 동작 중 변경은 ARPE=1 (PSC·ARR 동시 전환). ARPE=0 이면 ARR < CNT 에서 거의 65536 tick 짜리 주기, ARR > CNT 에서 혼합 주기.

## 참고자료

- ST RM0090 *STM32F405/415, F407/417, F427/437, F429/439 Reference Manual* — General-purpose timers (TIM2–TIM5): time-base unit, prescaler 설명(다음 UEV 부터 적용), 업카운트 모드, update event, TIMx_CR1 (CEN/UDIS/URS/ARPE), TIMx_SR (UIF, rc_w0), TIMx_EGR (UG), TIMx_ARR — <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf> (이 레슨의 주소·비트·동작은 기억에 의존한 대표값 — 원문 확인 권장)
- ST AN4776 *General-purpose timer cookbook for STM32 microcontrollers* — 타임 베이스 설정, preload, 주기 변경
- STM32CubeF4 HAL `stm32f4xx_hal_tim.c` — `TIM_Base_SetConfig()` (PSC/ARR 쓰기 후 UG), `HAL_TIM_Base_Start_IT()`
- ARMv7-M Architecture Reference Manual — 예외 복귀와 메모리 배리어(DSB), Q7 의 배경 — <https://developer.arm.com/documentation/ddi0403/latest/>
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* (3rd ed.) — 인터럽트 펜딩·tail-chaining
- 관련 레슨: BM-B02 (mock 프레임워크 원본), BM-B03 (RCC 클럭 켜기), BM-B04 (rc_w0 / W1C, RMW 레이스), BM-B10 (클럭 트리, APB 타이머 ×2), BM-B13 (SysTick: 24비트 다운카운터, reload = N−1), BM-M01 (NVIC), BM-M05 (PWM), BM-M06 (입력 캡처), BM-M12 (RTC 웨이크업), COMM-B03 (같은 "분주비 반올림 + 오차" 문제를 baud 로)
