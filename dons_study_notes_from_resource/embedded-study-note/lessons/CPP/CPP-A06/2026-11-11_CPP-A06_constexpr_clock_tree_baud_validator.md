---
id: CPP-A06
time: 110분
updated: 2026-11-11
---
# CPP-A06 컴파일타임 클럭 트리·보레이트 검증기

## 학습 목표

- HSE·목표 SYSCLK 로 PLL M/N/P/Q 를 찾는 **constexpr 전수 탐색**을 부동소수점 없이 쓰고, `consteval` 로 "반드시 빌드 중에" 돌게 만든다. 실패하면 에러 메시지에 **어떤 HSE·SYSCLK 가 문제인지**가 찍히게 한다.
- USART BRR 을 OVER16 / OVER8 두 방식으로 인코딩하고 (mantissa/fraction, 반올림 올림수, OVER8 의 bit3), 오차가 2 % 를 넘으면 빌드를 멈추는 게이트를 `static_assert` 판과 `concept` 판으로 만들고 차이를 말한다.
- "정수 ppm" 을 고른 이유를 설명한다: 컴파일 타임과 런타임 결과가 같아야 하고, 2 % 경계는 반올림한 ppm 으로는 구별되지 않는다 (실측 테스트).
- constexpr 평가 한도를 숫자로 안다: clang 기본 `-fconstexpr-steps` = 1 048 576 (실측), GCC `-fconstexpr-ops-limit` = 33 554 432 (에러 메시지). 가지치기 없는 탐색이 **해가 없을 때만** 한도에 걸리는 함정.
- Cortex-M4 오브젝트에서 컴파일 타임 경로가 **상수 store 만** 남기는 것과, 런타임 폴백(HSE 를 실행 중에 알 때)의 실제 크기·호출 심볼을 실측으로 비교한다.

## 1. 문제

"보드 HSE 를 8 MHz 에서 25 MHz 로 바꿨더니 UART 가 깨진 글자를 냅니다. 빌드는 아무 경고 없이 됐고요." — 클럭 설정은 펌웨어 버그 중 **빌드 시점에 잡을 수 있는데 안 잡는** 대표 사례다. 면접에서는 "PLL 값을 손으로 계산하나요, 툴(CubeMX)이 주나요? 그 값이 맞는지는 누가 보장하죠?" 로 시작해서 constexpr/consteval, 정수 산술, 코드 크기로 꼬리를 문다.

역할 분담: PLL 수식·플래시 wait state 의 유도는 **BM-B10**, BRR 공식과 폴링 UART 는 **BM-B12** 가 C 로 다룬다. 이 레슨은 그 계산을 **C++ 컴파일러에게 시키고, 틀리면 빌드를 멈추는** 쪽이다. `static_assert` 메시지 기법 일반은 CPP-M14, constexpr 표를 `.rodata` 로 굽는 것은 CPP-M13, 레지스터 타입은 CPP-M01 이 맡는다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">STM32F4 급 클럭 트리와 제약 (RM0090 대표값) — 이 레슨이 static_assert 로 지키는 것</text>
  <rect x="10" y="50" width="90" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="70" fill="currentColor">HSE</text><text x="22" y="86" fill="currentColor" font-size="12">4–26 MHz</text>
  <rect x="125" y="50" width="70" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="137" y="70" fill="currentColor">/M</text><text x="135" y="86" fill="currentColor" font-size="12">2..63</text>
  <rect x="220" y="50" width="100" height="44" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="230" y="70" fill="currentColor" font-size="12">VCO 입력</text><text x="230" y="86" fill="currentColor" font-size="12">1–2 MHz</text>
  <rect x="345" y="50" width="70" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="357" y="70" fill="currentColor">×N</text><text x="353" y="86" fill="currentColor" font-size="12">50..432</text>
  <rect x="440" y="50" width="110" height="44" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="450" y="70" fill="currentColor" font-size="12">VCO 출력</text><text x="450" y="86" fill="currentColor" font-size="12">100–432 MHz</text>
  <g stroke="currentColor" fill="none">
    <path d="M100 72 L125 72"/><path d="M195 72 L220 72"/><path d="M320 72 L345 72"/><path d="M415 72 L440 72"/>
    <path d="M550 66 L580 66 L580 48 L600 48"/><path d="M550 80 L580 80 L580 120 L600 120"/>
    <path d="M690 48 L700 48 L700 170 L640 170"/>
  </g>
  <rect x="600" y="30" width="90" height="36" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="608" y="53" fill="currentColor" font-size="12">/P ∈ {2,4,6,8}</text>
  <rect x="600" y="102" width="130" height="36" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="608" y="125" fill="currentColor" font-size="12">/Q 2..15 → 48 MHz</text>
  <rect x="470" y="152" width="170" height="36" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="480" y="175" fill="currentColor" font-size="12">SYSCLK = HCLK ≤ 168 MHz</text>
  <g stroke="currentColor" fill="none"><path d="M470 170 L420 170 L420 215"/><path d="M420 215 L300 215 L300 230"/><path d="M420 215 L560 215 L560 230"/></g>
  <rect x="200" y="230" width="200" height="36" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="210" y="253" fill="currentColor" font-size="12">APB1 /1..16 ≤ 42 MHz → USART2</text>
  <rect x="460" y="230" width="200" height="36" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="470" y="253" fill="currentColor" font-size="12">APB2 /1..16 ≤ 84 MHz → USART1</text>
  <text x="10" y="128" fill="currentColor" font-size="12">정확성 조건 (정수):</text>
  <text x="10" y="146" fill="currentColor" font-size="12" font-family="monospace">HSE·N == SYSCLK·M·P</text>
  <text x="10" y="164" fill="currentColor" font-size="12" font-family="monospace">USB: VCO % 48 MHz == 0</text>
  <text x="10" y="182" fill="currentColor" font-size="12" font-family="monospace">BRR div = round(PCLK/baud)</text>
  <text x="10" y="200" fill="currentColor" font-size="12" font-family="monospace">|PCLK − div·baud|·50 ≤ div·baud</text>
  <text x="10" y="290" fill="currentColor" font-size="12">노란 칸 = 범위 제약, 파란 칸 = 고르는 값, 초록 칸 = 결과 (여기서도 한도 검사)</text>
</svg>
```

숫자 한계(HSE 4–26 MHz, VCO 입력 1–2 MHz, VCO 출력 100–432 MHz, N 50..432, APB1 ≤ 42 MHz, APB2 ≤ 84 MHz, SYSCLK ≤ 168 MHz)는 STM32F405/407 레퍼런스 매뉴얼 RM0090 의 **대표값**으로 쓴다. 이 레슨에서 RM 원문과 한 줄씩 대조하지는 않았고, RM 판(revision)에 따라 VCO 출력 하한·N 하한이 다르게 적힌 적이 있다 — 실제 칩에서는 쓰는 칩의 RM/데이터시트로 `f4::` 상수만 바꾸면 된다.

### Part A — PLL 전수 탐색 + consteval

- `constexpr PllResult pll_search(const PllReq&)` — M 오름차순 → N 오름차순 → P 오름차순으로 돌며 **처음** 맞는 해. 모든 비교는 64비트 정수 곱셈으로 (나눗셈·부동소수점 없음). `need_usb48` 이면 Q 로 정확히 48 MHz.
- `consteval PllCfg pll_or_fail(PllReq)` — 해가 없으면 이름이 곧 에러 메시지인 constexpr 아닌 함수를 불러 **빌드 실패**.
- `encode_pllcfgr(PllCfg)` — RCC_PLLCFGR 비트 배치 (대표값).

### Part B — BRR 게이트

- `uart_brr(pclk, baud, Over, max_ppm)` — OVER16/OVER8 인코딩, TooFast/TooSlow/ErrorTooHigh, 부호 있는 정수 ppm (표시용). 게이트는 **정확한 유리수 비교**.
- `UartBrr<Pclk, Baud, Ov>::value` (static_assert + 증인 템플릿) 와 `concept BaudOk<...>` 두 게이트. 어느 쪽 에러가 읽기 좋은지 실측.

### Part C — 보드 계획, 초기화 순서, 런타임 폴백

- `plan_from(PllResult, BoardClock)` → HCLK/PCLK1/PCLK2, RCC_CFGR, FLASH_ACR. `Clock<BoardClock{...}>` 이 버스 한도를 `static_assert`.
- `apply_plan(plan, regs)` — 쓰기 순서: HSE 켜기 → **wait state 먼저** → PLLCFGR → PLLON → SW=PLL. mock `Soc` 가 순서를 기록한다.
- `pll_solve_fast(PllReq)` — HSE 를 부팅 후에야 아는 보드(스트랩·OTP·CSS 로 HSI 전환)용 런타임 탐색. N 루프 없이 gcd 로. **컴파일 타임 기준표와 232 경우 + 무작위 2000 경우 일치**해야 한다.

## 2. Problem (English)

> **Compile-time clock-tree and baud-rate validator (STM32F4-class).**
>
> 1. `constexpr PllResult pll_search(const PllReq& r)` — given HSE and target SYSCLK, search PLL M∈[2,63], N∈[50,432], P∈{2,4,6,8} in that lexicographic order and return the first exact solution with 1 MHz ≤ HSE/M ≤ 2 MHz, 100 MHz ≤ VCO ≤ 432 MHz, SYSCLK ≤ 168 MHz. If `need_usb48`, VCO/Q must be exactly 48 MHz with Q∈[2,15]; otherwise Q = max(2, ⌈VCO/48 MHz⌉). Integer arithmetic only. Provide `consteval PllCfg pll_or_fail(PllReq)` that fails the build with a message naming the problem, and `encode_pllcfgr`.
> 2. `constexpr BrrResult uart_brr(uint32_t pclk, uint32_t baud, Over ov, uint32_t max_ppm = 20000)` for oversampling by 16 and 8 (mantissa 12 bits, fraction 4 or 3 bits). Report the signed error in ppm (rounded) and reject when |error| > max_ppm, deciding the boundary exactly. Build a `UartBrr<Pclk, Baud, Ov>` gate that fails with `static_assert` and a concept `BaudOk`.
> 3. From a `BoardClock` NTTP derive bus clocks and register values, assert bus limits, and initialise mock RCC/FLASH/USART registers in a safe order. Add a runtime fallback for an HSE known only at run time that agrees with the constexpr search on every case of a test table.
>
> Constraints: `-std=c++20 -fno-exceptions -fno-rtti`, no heap, no floating point. Show that the compile-time path emits only constant stores on Cortex-M4.
>
> Follow-ups: Why `consteval` and not just `constexpr`? What happens when the search exceeds the constexpr step limit? Why not `double`? Does OVER8 reduce baud error? What does the runtime fallback cost in flash, and which libgcc helpers does it pull in?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
```

처음 실행 결과 (실제 출력, PASS 줄과 중간 FAIL 일부 생략):

```text
FAIL starter.cpp:285  cfg_is(kF407, 4, 168, 2, 7) && kF407.cfg.vco_hz == 336 * MHz && kF407.cfg.q_hz == 48 * MHz
FAIL starter.cpp:287  cfg_is(pll_search({25 * MHz, 168 * MHz, true}), 25, 336, 2, 7)
...
FAIL starter.cpp:302  brr_is(uart_brr(84 * MHz, 115200, Over::x16), BaudErr::None, 729, 0x2D9, 229)
...
FAIL starter.cpp:324  g_ok && !g_hi && !g_fast16 && g_fast8
FAIL starter.cpp:325  (UartBrr<84 * MHz, 115200>::value == 0x2D9 && UartBrr<42 * MHz, 921600>::err_ppm == -9284)
FAIL starter.cpp:329  Board::plan.hclk == 168 * MHz && Board::plan.pclk1 == 42 * MHz && Board::plan.pclk2 == 84 * MHz
...
FAIL starter.cpp:348  board_init_runtime(hse_strap, kBoard, Bus::Apb2, 115200, Over::x16, rt) == InitStatus::Ok
  HSE table: 0/232 agree, 232 solvable
FAIL starter.cpp:365  agree == static_cast<int>(kNRef) && solved == 161
  random: 0/2000 agree, 2000 solvable
FAIL starter.cpp:378  agree2 == 2000 && solved2 == 640
5/53 passed
```

하네스 설계 메모:

- 스텁은 **게이트를 통과하는 가짜 값** 을 돌려준다 (`pll_search` → `{None, {}}`, `uart_brr` → `{None, 0, 0, 0}`). 실패 값을 돌려주면 하네스 안의 `UartBrr<...>` / `Clock<...>` 의 `static_assert` 가 빌드를 멈춰서 FAIL 목록조차 볼 수 없다 (CPP-A04 와 같은 이유).
- `kF407`, `kRef[]`, `BaudOk<...>` 결과는 **컴파일러가 계산한 상수** 다. `CHECK` 는 그 상수를 런타임에 보고만 한다 — 그래서 starter 도 빌드된다.
- 기대값은 손계산과 **독립 파이썬 구현**(분수 `Fraction` 으로 같은 규칙)으로 만들었다. 232 경우 중 해가 있는 161, 무작위 2000 경우 중 640 이라는 숫자도 파이썬이 센 것이다.

관찰용 파일 (하네스와 별개, 결과는 5. 풀이에 실제 출력으로):

```bash
c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=N demo_errors.cpp   # N=1..7 실패 설정의 실제 에러
bash steps.sh limit|count|time      # demo_steps.cpp: constexpr step 한도·사용량·컴파일 시간 (count 는 맥에서 약 12분)
bash target_check.sh                # demo_target.cpp: Cortex-M4 asm/size/libcall, -O0 비교, VM gcc — 검사 통과 시 exit 0
```

`demo_*.cpp` 는 `#define CPP_A06_NO_HARNESS` 후 `solution.cpp` 를 include 한다 — 테스트한 그 코드를 그대로 타깃으로 컴파일하기 위해서다 (하네스와 `main` 은 `#ifndef` 로 빠진다).

## 4. 힌트

<details>
<summary>힌트 1 — 나눗셈을 곱셈으로 옮기면 정확하다</summary>

`HSE/M` 은 25 MHz / 13 처럼 정수가 아닐 수 있다. 범위 검사 `1 MHz ≤ HSE/M` 은 `HSE ≥ 1 MHz × M` 로, 정확성 `HSE/M×N/P == SYSCLK` 은 `HSE×N == SYSCLK×M×P` 로 쓴다. 25e6 × 432 는 32비트를 넘으니 `uint64_t`.

</details>

<details>
<summary>힌트 2 — 탐색 순서가 곧 "어떤 해를 고르나" 의 사양</summary>

M 이 작을수록 VCO 입력이 높다 (2 MHz 쪽, 지터에 유리). 같은 M 에서 정확한 해는 N = SYSCLK·M·P/HSE 라서 N 오름차순 = P 오름차순 = 낮은 VCO (전력). 처음 맞는 것을 돌려주면 그 선호가 자동으로 들어간다. 런타임 폴백도 **같은 순서** 를 지켜야 일치 테스트가 통과한다.

</details>

<details>
<summary>힌트 3 — BRR: 두 모드 모두 div = round(PCLK/baud)</summary>

OVER16: USARTDIV = PCLK/(16·baud), BRR 은 16·USARTDIV 를 반올림한 값을 12.4 고정소수점으로. OVER8: USARTDIV = PCLK/(8·baud), 8·USARTDIV 를 12.3 으로. 둘 다 "반올림한 PCLK/baud" 라는 같은 정수 `div` 다. 다른 것은 **비트 배치와 허용 범위** 뿐 — OVER8 은 `(div>>3)<<4 | (div&7)`.

</details>

<details>
<summary>힌트 4 — consteval 에서 "실패" 를 만드는 법 (-fno-exceptions)</summary>

`throw` 는 못 쓴다. 상수 평가가 constexpr 아닌 함수 호출에 닿으면 그 식은 상수식이 아니고, 즉시 호출(immediate invocation)은 반드시 상수식이어야 하므로 컴파일 에러가 된다 ([expr.const]). 에러에 그 함수 **이름** 이 찍히니 이름을 문장처럼 짓는다.

</details>

<details>
<summary>힌트 5 — 런타임 폴백: gcd 로 N 을 바로 구한다</summary>

VCO = SYSCLK·P 는 M 과 무관하다. g = gcd(VCO, HSE), a = VCO/g, b = HSE/g 이면 HSE·N == VCO·M ⇔ b·N == a·M ⇔ (gcd(a,b)=1 이므로) b | M, 그리고 N = a·(M/b). 모두 32비트.

</details>

## 5. 풀이

### Part A — 탐색: 제약을 루프 범위·곱셈 비교로

```cpp
for (std::uint32_t m = M_MIN; m <= M_MAX; ++m) {
    if (hse < u64{VCO_IN_MIN} * m || hse > u64{VCO_IN_MAX} * m) continue;          // M 단계에서 가지치기
    for (std::uint32_t n = N_MIN; n <= N_MAX; ++n) {
        const u64 vco_x_m = hse * n;                                                // = VCO × M
        if (vco_x_m < u64{VCO_MIN} * m || vco_x_m > u64{VCO_MAX} * m) continue;     // N 단계에서 가지치기
        for (std::uint32_t p = 2; p <= 8; p += 2) {
            if (vco_x_m != u64{r.sysclk_hz} * m * p) continue;                       // 정확히
            const std::uint32_t vco = r.sysclk_hz * p;                               // 위 등식 덕분에 정수
            std::uint32_t q = (vco + USB_HZ - 1) / USB_HZ;  if (q < Q_MIN) q = Q_MIN;
            if (r.need_usb48 && (vco % USB_HZ != 0 || q > Q_MAX)) continue;
            return {PllErr::None, {m, n, p, q, vco, r.sysclk_hz, vco / q}};
        }
    }
}
```

| 요청 (HSE, SYSCLK, USB) | 결과 M/N/P/Q | 테스트가 지키는 것 |
|---|---|---|
| 8 MHz, 168, USB | 4/168/2/7 (VCO 336, Q 48 MHz) | 기본 F407 설정. CubeMX 흔한 값은 8/336/2/7 (VCO 입력 1 MHz) — 이 레슨의 순서는 M 이 작은 쪽(2 MHz)을 먼저 |
| 25 MHz, 168, USB | 25/336/2/7 | M=13..24 는 VCO 입력이 정수가 아니고 N 도 정수가 안 나와 전부 지나침 |
| 16 MHz, 84, USB | 8/168/**4**/7 | P=2 면 VCO 168 → 48 로 안 나뉨 → P=4 |
| 8 MHz, 100, USB / 비USB | 없음 / 4/100/2/5 (Q 40 MHz) | USB 가 해를 없앤다 |
| 12.288 MHz (오디오), 168 | 없음 | 168·M·2/12.288 = 27.34375·M 이 정수가 되려면 M=32 → VCO 입력 0.384 MHz |
| 26 MHz / 4 MHz, 168, USB | 13/168/2/7 / 2/168/2/7 | VCO 입력 2 MHz 경계(포함), M_MIN 경계 |
| 3 MHz, 180 MHz, SYSCLK 0 | HseRange / SysclkRange | 범위 오류는 "해 없음" 과 구별 |

**왜 64비트 정수 등식인가.** 부동소수점으로 `hse/m*n/p == target` 을 쓰면 25/13 같은 값의 반올림 때문에 같은 설정이 맞기도 틀리기도 한다. 등식을 곱셈으로 바꾸면 "정확히 168 000 000 Hz" 가 판정 가능해진다. 그리고 컴파일 타임 경로와 런타임 경로가 **같은 비트** 를 내야 한다 (Part B 에서 다시).

**consteval 로 강제하기.** `constexpr` 함수는 "상수 평가에 **쓸 수 있다**" 는 뜻이지 "빌드 중에 **돈다**" 는 뜻이 아니다 ([dcl.constexpr]). `consteval` 은 즉시 함수(immediate function)를 만들고, 모든 호출이 상수식이어야 한다 ([expr.const] immediate invocation). 실패 경로는 이렇게:

```cpp
inline void pll_error_no_exact_solution_for_this_hse_and_sysclk() {}   // constexpr 아님 — 이름이 메시지
consteval PllCfg pll_or_fail(PllReq r) {
    const PllResult res = pll_search(r);
    if (res.err == PllErr::NoExactSolution) pll_error_no_exact_solution_for_this_hse_and_sysclk();
    if (res.err == PllErr::HseRange || res.err == PllErr::SysclkRange) pll_error_hse_or_sysclk_out_of_range();
    return res.cfg;
}
```

`demo_errors.cpp` 실제 출력 (Apple clang 21, 소스 인용 줄 일부 생략):

```text
# SHOW_ERROR=3: pll_or_fail({12'288'000, 168 MHz, true})
demo_errors.cpp:18:18: error: constexpr variable 'e3' must be initialized by a constant expression
./solution.cpp:80:45: note: non-constexpr function 'pll_error_no_exact_solution_for_this_hse_and_sysclk' cannot be used in a constant expression
demo_errors.cpp:18:23: note: in call to 'pll_or_fail({12288000, 168000000, true})'
demo_errors.cpp:18:23: error: call to consteval function 'pll_or_fail' is not a constant expression
# SHOW_ERROR=4: 런타임 값으로 호출
demo_errors.cpp:20:39: error: call to consteval function 'pll_or_fail' is not a constant expression
demo_errors.cpp:20:52: note: function parameter 'hse' with unknown value cannot be used in a constant expression
# arm-none-eabi-g++ 14.2.1, SHOW_ERROR=3
demo_errors.cpp:18:34:   in 'constexpr' expansion of 'pll_or_fail(PllReq{12288000, 168000000, true})'
solution.cpp:80:96: error: call to non-'constexpr' function 'void pll_error_no_exact_solution_for_this_hse_and_sysclk()'
```

규칙 = 함수 이름, 값 = `in call to 'pll_or_fail({12288000, 168000000, true})'`. 두 컴파일러 모두 **인자 값까지** 찍는다. 같은 질문에 다른 답 (C++26 이면 메시지 문자열 조립, P2741) 은 CPP-M14 가 다룬다.

### Part A — constexpr 평가 한도: 언제, 어떻게 터지나

면접에서 처음 떠올리기 쉬운 탐색은 M×N×P×Q 를 모두 만들고 마지막에 조건을 보는 모양이다 (`demo_steps.cpp` 의 `pll_search_naive`). 62 × 383 × 4 × 14 = 1 329 664 조합. clang 기본 한도로 컴파일하면:

```text
# -DVARIANT=1 (naive), HSE 12.288 MHz — 해가 없는 경우
demo_steps.cpp:45:21: error: constexpr variable 'kR' must be initialized by a constant expression
demo_steps.cpp:30:21: note: constexpr evaluation hit maximum step limit; possible infinite loop?
demo_steps.cpp:45:26: note: in call to 'pll_search_naive({12288000U, 168000000U, 1 != 0})'
```

**기본 한도가 몇인가** — `bash steps.sh limit` 실제 출력 (`spin(n)` = n 번 도는 빈 덧셈 루프):

```text
default: spin(1048571) passes, spin(1048572) fails
spin(1048571) needs -fconstexpr-steps >= 1048576
spin(1048572) fails with -fconstexpr-steps=1048576 too
```

기본값으로 갈리는 경계와 `-fconstexpr-steps=1048576` 으로 갈리는 경계가 같다 → **clang 기본 = 1 048 576 (2^20)**. 루프 한 바퀴가 대략 1 step 이다. 표준 Annex B [implimits] 의 권장 최소량에 "core constant expression 안에서 평가되는 full-expression 수 [1 048 576]" 이 있고 숫자가 같다 (clang 의 step 이 정확히 그 단위인지는 미확인).

**실제 사용량** — `bash steps.sh count` (통과하는 최소 `-fconstexpr-steps` 를 이분 탐색, 실제 출력):

| HSE, SYSCLK, USB | `pll_search` (이 레슨) | `pll_solve_fast` | naive |
|---|---|---|---|
| 8 MHz, 168, USB | 2 035 | 70 | 355 425 |
| 25 MHz, 168, USB | 61 102 | 346 | **3 656 289** |
| 26 MHz, 168, USB | 2 062 | 114 | **1 741 137** |
| 12.288 MHz, 168, USB (해 없음) | 29 412 | 213 | **9 546 021** |
| 12.288 MHz, 168, 비USB (해 없음) | 29 412 | 213 | **9 546 021** |

읽는 법:

- naive 는 **8 MHz 같은 흔한 보드에서는 통과** 한다 (M=4 에서 일찍 찾음). 25 MHz 보드로 바꾸는 날, 또는 해가 없는 설정을 넣는 날 빌드가 "step limit; possible infinite loop?" 로 깨진다. 진짜 원인(해 없음)이 아닌 엉뚱한 메시지다 — 가장 나쁜 실패 모양.
- 가지치기(M 단계에서 VCO 입력, N 단계에서 VCO 출력)만으로 최악 61 k step, 한도의 6 %. 한도를 올리는 것(`-fconstexpr-steps=...`)은 빌드 플래그를 모든 개발자·CI 에 퍼뜨리는 일이라 **마지막 수단**.
- 25 MHz 가 26 MHz 보다 30배 비싼 이유: 25 MHz 는 해가 M=25 (VCO 입력 1 MHz) 에서야 나와 M=13..24 의 N 루프를 다 돌고, 26 MHz 는 M=13 (2 MHz) 에서 바로 나온다.
- GCC 는 이름도 단위도 다르다. VM `arm-none-eabi-g++ 14.2.1` 에서 naive(12.288 MHz): `error: 'constexpr' evaluation operation count exceeds limit of 33554432 (use '-fconstexpr-ops-limit=' to increase the limit)`. GCC 에는 루프당 반복 한도 `-fconstexpr-loop-limit` (문서상 기본 262144) 도 따로 있는데, 이 탐색의 루프는 최대 383 회라 해당 없다.

**컴파일 시간** — `bash steps.sh time` (`-fsyntax-only`, 3회 최소, 이 맥):

```text
spin(1) (baseline, solution.cpp 헤더 포함): 0.158 s
pll_search 12.288MHz:                     0.171 s
pll_solve_fast 12.288MHz:                 0.164 s
naive 12.288MHz, -fconstexpr-steps=2^31-1: 5.94 s
```

가지치기한 탐색은 기준선 대비 +13 ms. naive 는 한도를 풀어 주면 통과하지만 탐색 하나에 약 6 초 (GCC 는 `-fconstexpr-ops-limit=1000000000` 으로 2.52 s). 하네스의 232 경우 기준표 + 나머지 전부를 포함한 `solution.cpp` 전체 `-O2` 빌드는 약 1.6 s 였다. 기준표를 **원소마다 따로** 상수 평가하는 이유도 이것이다 (`template <size_t I> inline constexpr PllResult kRefOne = pll_search(ref_req(I));`) — step 한도는 상수식 하나마다 적용되므로 232 개를 한 람다에서 채우면 한도 합계가 한 평가에 몰린다.

### Part B — BRR: 한 정수 div, 두 가지 인코딩

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">USART_BRR — div = round(PCLK/baud) 를 어떻게 싣나 (RM0090 대표값)</text>
  <text x="10" y="58" fill="currentColor">OVER16</text>
  <rect x="90" y="40" width="480" height="30" class="f-acc" stroke="currentColor"/>
  <rect x="570" y="40" width="160" height="30" class="f-ok" stroke="currentColor"/>
  <text x="200" y="60" fill="currentColor" font-family="monospace" font-size="12">DIV_Mantissa[15:4] = div &gt;&gt; 4</text>
  <text x="578" y="60" fill="currentColor" font-family="monospace" font-size="12">Fraction[3:0]=div&amp;15</text>
  <text x="10" y="118" fill="currentColor">OVER8</text>
  <rect x="90" y="100" width="480" height="30" class="f-acc" stroke="currentColor"/>
  <rect x="570" y="100" width="40" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="610" y="100" width="120" height="30" class="f-ok" stroke="currentColor"/>
  <text x="200" y="120" fill="currentColor" font-family="monospace" font-size="12">DIV_Mantissa[15:4] = div &gt;&gt; 3</text>
  <text x="580" y="120" fill="currentColor" font-family="monospace" font-size="12">0</text>
  <text x="616" y="120" fill="currentColor" font-family="monospace" font-size="12">[2:0]=div&amp;7</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="160">실제 보레이트 = PCLK / div (두 모드 같음) → 오차도 같다. OVER8 이 바꾸는 것은 범위뿐:</text>
    <text x="10" y="180">div 허용: OVER16 16..65535 (최대 PCLK/16), OVER8 8..32767 (최대 PCLK/8, 대신 최저 속도가 2배)</text>
    <text x="10" y="200">예: 84 MHz, 219000 baud → div 383.56 → 384 = 0x180 (mantissa 24, fraction 0 — 반올림이 올림수를 만든다)</text>
    <text x="10" y="220">     42 MHz, 2 Mbaud → div 21 → OVER16 0x15, OVER8 0x25 (같은 div, 다른 레지스터 값)</text>
    <text x="10" y="240">bit3 (경고색 칸) 은 OVER8 에서 0 으로 둔다 — div 를 그대로 쓰면 31 = 0x1F 의 bit3 이 켜진다 (정답 0x37)</text>
  </g>
</svg>
```

유도: OVER16 에서 USARTDIV = PCLK/(16·baud) 이고 BRR 은 USARTDIV 를 1/16 단위로 반올림한 값이다 → 16·USARTDIV = PCLK/baud 를 반올림 = `div`. OVER8 은 USARTDIV = PCLK/(8·baud) 를 1/8 단위로 → 역시 PCLK/baud 를 반올림 = 같은 `div`. 레지스터 값만 다르다:

```cpp
const u64 div = (u64{pclk} + baud / 2) / baud;                       // 반올림 (0.5 는 올림)
if (div < div_min(ov)) TooFast;  if (div > div_max(ov)) TooSlow;
r.brr = ((div >> fbits) << 4) | (div & ((1u << fbits) - 1u));        // fbits = 4 (x16) / 3 (x8)
```

RM 의 계산법 "mantissa = floor(USARTDIV), fraction = round(frac × 16), 16 이 되면 mantissa 에 올림" 과 정확히 같은 값이다 (정수 부분은 반올림에 영향이 없으므로 round(16m + 16f) = 16m + round(16f)). 손으로 나눠 쓰면 올림수 버그가 생긴다: mantissa 23, fraction 16 을 `(23 << 4) | 16` 으로 합치면 `0x170 | 0x10 = 0x170` (정답 `0x180`). mantissa 가 짝수면 우연히 맞아서 **홀수 mantissa 테스트 벡터가 필요하다** (`84 MHz, 219000` 이 그것).

| 테스트 | div | BRR | 오차 ppm | 판정 |
|---|---|---|---|---|
| 84 MHz, 115200, x16 | 729 | 0x2D9 | +229 | OK |
| 42 MHz, 115200, x16 | 365 | 0x16D | −1142 | OK |
| 16 MHz(HSI), 921600, x16 / x8 | 17 | 0x11 / 0x21 | +21242 | **ErrorTooHigh** (둘 다) |
| 42 MHz, 3 Mbaud, x16 / x8 | 14 | — / 0x16 | 0 | **TooFast** / OK |
| 84 MHz, 1200, x16 · 84 MHz, 2400, x8 | 70000 · 35000 | — | — | **TooSlow** |
| 20 400 000, 1 Mbaud | 20 | 0x14 | +20000 | OK (정확히 2 %) |
| 20 400 001, 1 Mbaud | 20 | 0x14 | +20000 | **ErrorTooHigh** (2.000005 %) |
| 19 600 000 / 19 599 999, 1 Mbaud | 20 | 0x14 | −20000 / −20000 | OK / **ErrorTooHigh** |

**정수 ppm, 그리고 왜 게이트는 ppm 이 아닌가.** 마지막 두 줄이 핵심이다. 반올림한 ppm 은 둘 다 20000 인데 하나는 2 % 이하, 하나는 초과다. 그래서 판정은 유리수 그대로 `|PCLK − div·baud| · 1e6 > max_ppm · div·baud` (64비트 곱셈만) 로 하고, ppm 은 사람에게 보여 주는 값으로만 쓴다. 돌연변이 검증에서 게이트를 "반올림 ppm > 20000" 으로 바꾼 판은 정확히 이 두 CHECK 에서 FAIL 했다.

**왜 부동소수점이 아닌가.** C++20 은 상수식에서 `double` 연산을 허용한다. 그래도 정수를 고른 이유:

| 이유 | 근거 |
|---|---|
| 컴파일 타임과 런타임이 같은 답을 내야 한다 (런타임 폴백 일치 테스트) | [expr.const] 의 note: 표준은 부동소수점 정확도를 제한하지 않으므로, 번역 중 평가가 실행 중 평가와 같은 결과인지는 **unspecified** |
| 경계(정확히 2 %, 정확히 168 MHz)를 판정해야 한다 | 위 표의 20 400 000 / 20 400 001 |
| 런타임 폴백이 M0/M0+ 나 FPU 끈 빌드에서도 돈다 | soft-float 이면 `__aeabi_ddiv` 등 라이브러리 호출. 아래 실측처럼 정수도 64비트 나눗셈은 libgcc 헬퍼를 부른다 — 그래서 게이트는 나눗셈 없이 곱셈만 |

**OVER8 은 오차를 줄이지 않는다.** 위 유도대로 실제 보레이트는 두 모드 모두 `PCLK/div` 다. 16 MHz 에서 921600 은 OVER8 로 바꿔도 +2.12 % 그대로 (`CHECK` 로 고정). OVER8 은 최대 보레이트를 PCLK/8 로 올릴 뿐이고, 대신 비트당 샘플이 적어 수신 허용 오차가 줄어든다 (RM 의 receiver tolerance 표 — 구체적 값은 이 레슨에서 미확인). ST RM 의 OVER8 오차 표와 한 줄씩 대조하지는 않았다.

#### 게이트 두 개: static_assert vs concept

```cpp
template <bool Bad, std::int32_t ErrPpm, std::uint32_t Div>
struct baud_error_over_2pct {                                   // 증인 템플릿: 숫자를 인자로 실어 에러에 찍는다
    static_assert(!Bad, "UartBrr: |baud error| > 2% - see ErrPpm (rounded ppm) and Div in the witness template args");
    static constexpr bool ok = true;
};
template <std::uint32_t Pclk, std::uint32_t Baud, Over Ov = Over::x16>
struct UartBrr {
    static constexpr BrrResult r = uart_brr(Pclk, Baud, Ov);
    static_assert(Baud != 0, "UartBrr: baud must not be 0");
    static_assert(baud_divisor_out_of_range<r.err == BaudErr::TooFast || r.err == BaudErr::TooSlow, r.div, div_min(Ov), div_max(Ov)>::ok);
    static_assert(baud_error_over_2pct<r.err == BaudErr::ErrorTooHigh, r.err_ppm, r.div>::ok);
    static constexpr std::uint16_t value = r.brr;
};
template <std::uint32_t Pclk, std::uint32_t Baud, Over Ov = Over::x16>
concept BaudOk = uart_brr(Pclk, Baud, Ov).ok();
```

실제 에러 (clang 21):

```text
# SHOW_ERROR=1: UartBrr<16 MHz, 921600>
./solution.cpp:139:19: error: static assertion failed due to requirement '!true': UartBrr: |baud error| > 2% - see ErrPpm (rounded ppm) and Div in the witness template args
./solution.cpp:148:19: note: in instantiation of template class 'baud_error_over_2pct<true, 21242, 17>' requested here
demo_errors.cpp:14:21: note: in instantiation of template class 'UartBrr<16000000, 921600>' requested here
# SHOW_ERROR=2: UartBrr<42 MHz, 3 Mbaud> (x16)
./solution.cpp:134:19: error: static assertion failed due to requirement '!true': UartBrr: Div = round(PCLK/baud) must be in [DivMin, DivMax] - too fast: Over::x8 or faster bus
./solution.cpp:146:19: note: in instantiation of template class 'baud_divisor_out_of_range<true, 14, 16, 65535>' requested here
demo_errors.cpp:16:21: note: in instantiation of template class 'UartBrr<42000000, 3000000>' requested here
# SHOW_ERROR=5: 같은 조건을 requires 절 (BaudOk) 로
demo_errors.cpp:24:21: error: constraints not satisfied for variable template 'brr_v' [with P = 16000000, B = 921600, O = Over::x16]
demo_errors.cpp:22:74: note: because 'BaudOk<16000000U, 921600U, (Over)'\x00'>' evaluated to false
./solution.cpp:154:18: note: because 'uart_brr(16000000U, 921600U, (Over)'\x00').ok()' evaluated to false
```

GCC 14.2.1 도 증인 인자를 보여 준다 (`In instantiation of 'struct baud_error_over_2pct<true, 21242, 17>': required from 'struct UartBrr<16000000, 921600>'`). 처음 판은 조건을 `r.err != BaudErr::ErrorTooHigh` 로 썼는데 clang 이 `expression evaluates to '4 != 4'` (enum 숫자) 를 찍어 쓸모가 없었다 — 증인 템플릿으로 바꾼 이유다. 또 하나 관찰: 한 클래스 템플릿 인스턴스 안에서 `static_assert` 가 여러 개 실패해도 clang 은 **첫 번째만** 보고했다 (실험 파일에서 4개 중 1개). 그래서 순서를 "0 → 범위 → 오차" 로 둔다.

| | `UartBrr` (static_assert) | `BaudOk` (concept / requires) |
|---|---|---|
| 메시지 | 내가 쓴 규칙 + 증인 인자 (ppm, div) | 컴파일러 기본: 어떤 식이 false 였나. enum 이 `(Over)'\x00'` 로 찍힘 |
| "물어보기" | 불가 — 클래스 본문 `static_assert` 는 SFINAE 가 아니라 hard error (CPP-B12) | `if constexpr (BaudOk<...>)`, 오버로드 선택, 하네스의 `constexpr bool g_hi = BaudOk<16 MHz, 921600>` |
| 쓰는 곳 | 드라이버 초기화 지점 (실패 = 빌드 중지) | 정책 선택 (예: OVER16 이 안 되면 OVER8 로 자동 전환), 테스트 |

### Part C — 보드 계획 → 레지스터, 그리고 진짜 0 인가

`BoardClock` 은 C++20 의 구조적 클래스 타입(structural type)이라 템플릿 인자로 그대로 넘긴다 ([temp.param]). 에러에도 그대로 찍힌다:

```text
# SHOW_ERROR=6: Clock<BoardClock{8 MHz, 168 MHz, true, 1, 2, 2}> — APB1 을 /2 로
./solution.cpp:206:19: error: static assertion failed due to requirement 'plan.pclk1 <= f4::APB1_MAX': Clock: APB1 > 42 MHz - raise apb1_div
demo_errors.cpp:26:21: note: in instantiation of template class 'Clock<BoardClock{8000000, 168000000, true, 1, 2, 2}>' requested here
./solution.cpp:206:30: note: expression evaluates to '84000000 <= 42000000'
# SHOW_ERROR=7: HSE 를 12.288 MHz 로 잘못 적은 보드 — Clock 안의 consteval 이 실패, 뒤따르는 static_assert 도 연쇄 에러
./solution.cpp:205:32: error: constexpr variable 'plan' must be initialized by a constant expression
./solution.cpp:205:73: note: in call to 'pll_or_fail({12288000, 168000000, true})'
./solution.cpp:206:19: error: static assertion expression is not an integral constant expression
```

여기서는 조건이 정수 비교라 clang 의 `expression evaluates to '84000000 <= 42000000'` 이 그대로 쓸모 있다 — 증인이 필요 없는 경우.

**초기화 순서** (`apply_plan`, mock `Soc` 가 기록): ① `RCC_CR |= HSEON`, HSERDY 대기 ② `FLASH_ACR = 0x705` (LATENCY 5 + 프리페치/캐시) ③ `RCC_PLLCFGR = 0x07402A04` ④ `RCC_CR |= PLLON`, PLLRDY 대기 ⑤ `RCC_CFGR = 0x9402` (HPRE /1, PPRE1 /4, PPRE2 /2, SW=PLL). 왜 ②가 ⑤보다 먼저인가: 168 MHz 로 바꾼 순간 플래시가 아직 0 wait state 면 다음 명령 인출이 틀린다 — 클럭을 **올릴 때는 wait state 먼저, 내릴 때는 클럭 먼저** (유도는 BM-B10). 테스트는 로그 순서 `CR, ACR, PLLCFGR, CR, CFGR, BRR, CR1` 를 고정하고, 돌연변이(ACR 을 맨 뒤로)가 그 CHECK 에서 잡혔다.

```svg
<svg viewBox="0 0 740 220" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 코드, 두 경로 — 무엇이 플래시에 남나 (thumbv7em, -O2, 실측 바이트)</text>
  <rect x="10" y="40" width="200" height="60" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor" font-family="monospace" font-size="12">BoardClock{8MHz,...}</text>
  <text x="20" y="82" fill="currentColor" font-size="12">템플릿 인자 (빌드 시 앎)</text>
  <rect x="250" y="40" width="220" height="60" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="260" y="62" fill="currentColor" font-size="12">Clock&lt;B&gt;::plan, UartBrr&lt;...&gt;</text>
  <text x="260" y="82" fill="currentColor" font-size="12">탐색·인코딩 = 컴파일러 안에서</text>
  <rect x="510" y="40" width="220" height="60" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="520" y="62" fill="currentColor" font-size="12">clock_init_ct: 122 B (clang)</text>
  <text x="520" y="82" fill="currentColor" font-size="12">상수 store + RDY 폴링뿐</text>
  <rect x="10" y="130" width="200" height="60" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="152" fill="currentColor" font-family="monospace" font-size="12">hse_hz (스트랩/OTP)</text>
  <text x="20" y="172" fill="currentColor" font-size="12">부팅 후에 앎</text>
  <rect x="250" y="130" width="220" height="60" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="260" y="152" fill="currentColor" font-size="12">pll_solve_fast + plan_from</text>
  <text x="260" y="172" fill="currentColor" font-size="12">+ uart_brr — 실행 중 계산</text>
  <rect x="510" y="130" width="220" height="60" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="520" y="152" fill="currentColor" font-size="12">clang 1700 B / gcc 780 B</text>
  <text x="520" y="172" fill="currentColor" font-size="12">gcc: __aeabi_uldivmod, memset</text>
  <g stroke="currentColor" fill="none"><path d="M210 70 L250 70"/><path d="M470 70 L510 70"/><path d="M210 160 L250 160"/><path d="M470 160 L510 160"/></g>
  <text x="10" y="212" fill="currentColor" font-size="12">일치 보장: 232 경우 표 + 무작위 2000 경우에서 두 경로의 PllResult 가 비트 단위로 같다 (하네스)</text>
</svg>
```

**"런타임 비용 0" 실측** — `bash target_check.sh asm` (clang 21, `--target=thumbv7em-none-eabihf -mcpu=cortex-m4 -O2`, `demo_target.cpp` 의 `Mmio` 버스로 같은 `board_init<kBoard, Bus::Apb2, 115200>`):

```text
00000000 <clock_init_ct>:
       0:      	movw	r0, #0x3800
       4:      	movt	r0, #0x4002          @ RCC 0x40023800
       8:      	ldr	r1, [r0]
       a:      	orr	r1, r1, #0x10000      @ HSEON
       e:      	str	r1, [r0]
      10:      	ldr	r1, [r0]              @ HSERDY 폴링 (clang 이 4번 펼침, 10..28)
      ...
      2a:      	movw	r1, #0x705
      2e:      	str.w	r1, [r0, #0x400]      @ FLASH_ACR (0x40023C00)
      32:      	movw	r1, #0x2a04
      36:      	movt	r1, #0x740
      3a:      	str	r1, [r0, #0x4]          @ RCC_PLLCFGR = 0x07402A04
      3c:      	ldr	r1, [r0]
      3e:      	orr	r1, r1, #0x1000000    @ PLLON
      42:      	str	r1, [r0]
      ...                                    @ PLLRDY 폴링 (44..5c)
      5e:      	movw	r1, #0x9402
      62:      	str	r1, [r0, #0x8]          @ RCC_CFGR
      64:      	movw	r0, #0x1008
      68:      	movt	r0, #0x4001
      6c:      	movw	r1, #0x2d9
      70:      	str	r1, [r0]                @ USART1_BRR
      72:      	movw	r1, #0x200c
      76:      	str	r1, [r0, #0x4]          @ USART1_CR1 = UE|TE|RE
      78:      	bx	lr
```

(`...` 은 폴링 루프 명령, 주석은 내가 단 것.) 계산 흔적은 없다: `0x705`, `0x07402A04`, `0x9402`, `0x2D9`, `0x200C` 가 즉시값으로 들어가 있고 `bl` 이 없다. 남은 `ldr/lsls/bmi` 는 하드웨어 RDY 를 기다리는 **진짜 일** 이다. 스크립트는 "`clock_init_ct` 에 `bl` 없음, 오브젝트에 `pll_search` 심볼 없음" 을 검사하고 exit 0. VM 의 `arm-none-eabi-g++ 14.2.1 -O2` 도 같은 상수(`movw r1, #1797 @ 0x705`, 리터럴 풀 `.word 0x07402a04`, `movw r4, #37890 @ 0x9402`, `movw r0, #729`)에 `bl` 없이 100 바이트였다 (폴링은 펼치지 않음).

**런타임 폴백의 값** — 같은 오브젝트 `-ffunction-sections` 의 `llvm-nm -C --size-sort` / VM `arm-none-eabi-nm` (실제 출력에서):

| 함수 | clang 21 | arm-none-eabi-g++ 14.2.1 |
|---|---|---|
| `clock_init_ct` (컴파일 타임 경로) | 0x7a = 122 B | 0x64 = 100 B |
| `clock_init_rt` (런타임 경로 본체) | 0x130 = 304 B | 0x16c = 364 B |
| `pll_solve_fast` | 0x3fa = 1018 B | 0x1a0 = 416 B |
| `plan_from` | 0x17a = 378 B | (인라인됨) |
| `.rodata` | 24 B (`kBoard`, 참조로 넘겨서) | — |
| 미정의 심볼 | 없음 | `__aeabi_uldivmod`, `memset` |

- 컴파일 타임 경로 대비 런타임 경로는 clang 에서 약 14배 (122 B vs 1700 B). gcd 루프와 4개 P 에 대한 펼침이 크기 대부분이다.
- clang 은 `uart_brr` 의 64비트 `(pclk + baud/2) / baud` 에서 baud 가 상수(115200)라 곱셈으로 바꿨고 헬퍼가 필요 없었다. GCC 는 같은 나눗셈에 `__aeabi_uldivmod` 를 불렀다. Cortex-M4 의 `UDIV` 는 32비트뿐이다.
- `bash target_check.sh ppm` (clang): baud 가 런타임 값이면 BRR 만 꺼내도 `U __aeabi_uldivmod` (`baud_brr_rt` 64 B), ppm 까지 꺼내면 128 B, baud 가 상수여도 **ppm 을 쓰면** 다시 `__aeabi_uldivmod` (118 B). `clock_init_rt` 는 ppm 을 안 쓰니 그 나눗셈이 통째로 지워졌다. 런타임 경로에서 ppm 은 로그용으로만, 게이트는 곱셈만 — 이 설계가 숫자로 맞았다.
- 개선 여지 (연습 아님, 판단 문제): `div` 를 32비트 `q = pclk / baud; r = pclk % baud; div = q + (r >= baud - baud/2)` 로 쓰면 M4 의 `UDIV` 한 개로 끝나고 GCC 의 헬퍼도 빠질 것이다 (이 레슨에서 측정하진 않음).

**constexpr 함수 ≠ 컴파일 타임 실행** — `bash target_check.sh o0` (`-O0 -DNOT_FORCED`, 함수별 `R_ARM_THM_CALL` 대상, 실제 출력):

```text
### clang -O0 -DNOT_FORCED
o0_const_local     encode_pllcfgr(PllCfg const&)
o0_plain_local     encode_pllcfgr(PllCfg const&)
o0_expr            pll_search(PllReq const&) encode_pllcfgr(PllCfg const&)
o0_consteval_arg   encode_pllcfgr(PllCfg const&)
o0_constexpr_var
### arm-none-eabi-g++ -O0 -DNOT_FORCED
(위와 같은 5줄)
```

| 형태 | -O0 에서 런타임에 남은 것 (clang = gcc) |
|---|---|
| `const PllResult r = pll_search(kReq);` / 비-const 지역 변수 | 탐색은 사라짐 (clang 은 결과를 `.L__const.o0_const_local.r` 32 B 로 굽고 복사), `encode_pllcfgr` 호출은 남음 |
| `encode_pllcfgr(pll_search(kReq).cfg)` (식 안) | **`pll_search` 가 런타임에 실행된다** |
| `encode_pllcfgr(pll_or_fail(kReq))` (consteval 인자) | 탐색은 확실히 빌드 중 (PllCfg 를 상수로 스택에 씀), `encode_pllcfgr` 는 런타임 호출 |
| `constexpr std::uint32_t v = encode_pllcfgr(pll_or_fail(kReq));` | 아무 호출 없음 |

예상과 달랐던 점: 두 컴파일러 모두 -O0 에서도 **지역 변수 초기화** 는 상수로 접었다. 표준이 요구하는 것은 아니다 (상수 초기화가 보장되는 것은 정적 저장 기간 변수, [basic.start.static]). 반면 같은 호출을 식 안에 쓰면 탐색이 그대로 런타임에 돈다. 결론: "컴파일 타임에 끝났다" 를 보장하는 것은 **constexpr 변수** 와 **consteval** 뿐이고, consteval 도 **그 함수 하나만** 강제한다 (`encode_pllcfgr` 가 -O0 에서 호출로 남았다). 디버그 빌드 플래시 예산이 빡빡하면 결과를 constexpr 변수(또는 `Clock<B>::plan` 같은 static constexpr 멤버)에 받는다.

**일치 테스트.** `kRef[232]` 는 원소마다 컴파일 타임에 `pll_search` 로 구한 값, 런타임은 `volatile` 로 감춘 HSE 로 `pll_solve_fast` 를 부른다. 232/232 일치, 해 있는 경우 161. 무작위 2000 쌍(HSE 3..27.5 MHz 0.5 MHz 단위, SYSCLK 24..173 MHz, USB 무작위, xorshift32 고정 시드)은 같은 constexpr 함수를 **런타임에** 돌린 것과 비교해 2000/2000, 해 640 — 범위 밖(HseRange/SysclkRange)과 해 없음까지 같은 답이어야 통과한다. 돌연변이 `m % b[i]` 검사 제거는 두 일치 CHECK 에서 잡혔다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution     # 53/53 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o sol_asan && ./sol_asan   # 53/53, 보고 없음
bash target_check.sh    # target_check: OK (exit 0)
```

돌연변이 검증 (solution.cpp 사본 6개, 레슨 디렉터리 밖): OVER8 재인코딩 생략 → 4 FAIL, 게이트를 반올림 ppm 으로 → 2 FAIL (2 % 경계), FLASH_ACR 을 SW 뒤로 → 2 FAIL (순서), VCO 입력 2 MHz 를 배타적으로 → 12 FAIL, 빠른 탐색에서 `m % b` 생략 → 2 FAIL (일치), USB 의 `VCO % 48 MHz` 검사 생략 → 4 FAIL. 전부 잡혔다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `double` 로 `hse / m * n / p == target` | 25/13 같은 값의 반올림, 컴파일 타임과 런타임 결과가 같다는 보장 없음 ([expr.const] note) | `u64{hse} * n == u64{sys} * m * p` |
| `uint32_t` 로 `hse * n` | 25 MHz × 432 = 1.08e10 → 넘침, 엉뚱한 해 | `std::uint64_t` |
| M×N×P×Q 를 모두 만든 뒤 조건 검사 | 흔한 8 MHz 에선 통과, 해 없는 설정에서 `step limit; possible infinite loop?` — 원인과 다른 에러 | 제약을 루프 단계마다 `continue` 로 (M 에서 VCO 입력, N 에서 VCO 출력) |
| `constexpr` 함수면 빌드 중에 돈다고 믿음 | 식 안의 호출은 -O0 에서 런타임에 돌았다 (실측) | `constexpr` 변수에 받거나 `consteval` |
| consteval 안에서 실패를 `throw` 로 | `-fno-exceptions` 빌드에서 쓸 수 없다 | 이름이 메시지인 constexpr 아닌 함수 호출 |
| BRR = `(mant << 4) \| round(frac*16)` | fraction 이 16 이 되면 올림이 사라짐 (홀수 mantissa 에서만 드러남) | `div = round(PCLK/baud)`, `brr = div` (x16) |
| OVER8 에서 BRR = div | bit3 이 켜질 수 있다 (`31` → `0x1F`, 정답 `0x37`) | `(div>>3)<<4 \| (div&7)` |
| 2 % 판정을 반올림 ppm 으로 | 2.000005 % 가 20000 ppm 으로 통과 | `diff·1e6 > max_ppm·div·baud` |
| "OVER8 로 바꾸면 오차가 준다" | 실제 보레이트는 두 모드 모두 PCLK/div | PCLK 를 바꾸거나 (버스·분주) 보레이트를 바꾼다 |
| 클럭을 올린 뒤 FLASH wait state 설정 | 전환 직후 인출 오류 | wait state 먼저, 그다음 SW=PLL |
| `static_assert(r.err != BaudErr::ErrorTooHigh)` 만 | 에러가 `'4 != 4'` — 어떤 값인지 모름 | 증인 템플릿 `baud_error_over_2pct<Bad, ErrPpm, Div>` |

## 8. 면접 Q&A

<details>
<summary>Q1. constexpr 로 충분하지 않나요? 왜 consteval 을 썼죠?</summary>

`constexpr` 함수는 상수 평가에 **쓸 수 있는** 함수일 뿐이다. 호출 문맥이 상수식을 요구하지 않으면 런타임에 돌아도 된다 — 실측으로 `encode_pllcfgr(pll_search(kReq).cfg)` 는 clang·GCC 모두 -O0 에서 `pll_search` 를 런타임에 호출했다. 클럭 설정이 그렇게 되면 ① 탐색 코드가 플래시에 들어가고 ② 해가 없을 때 빌드가 아니라 **부팅** 에서 실패한다. `consteval` 은 모든 호출이 상수식이어야 해서 ②를 빌드 에러로 바꾼다. 꼬리: "consteval 이면 다 0 바이트?" → 아니다. consteval 함수의 결과를 다른 constexpr 함수(`encode_pllcfgr`)에 넘기면 그 호출은 -O0 에서 남았다 (실측). 확실한 것은 결과를 `constexpr` 변수/`static constexpr` 멤버로 받는 것.

</details>

<details>
<summary>Q2. 컴파일 타임 탐색이 너무 오래 걸리거나 한도에 걸리면?</summary>

clang 기본 `-fconstexpr-steps` 는 1 048 576 이다 (경계 실측: `spin(1048571)` 통과, `spin(1048572)` 실패, `=1048576` 과 같은 경계). GCC 는 `-fconstexpr-ops-limit` 33 554 432 (에러 메시지). 가지치기 없는 M×N×P×Q 탐색은 8 MHz 에서는 355 k step 으로 통과하지만 25 MHz 3.66 M, 해 없음 9.55 M 으로 실패한다. 대응 순서: ① 제약을 루프 범위로 (이 레슨: 최악 61 k) ② 알고리즘 바꾸기 (gcd 로 N 직접 계산: 최악 346) ③ 상수식 하나에 몰지 않기 (표를 원소별 `inline constexpr` 로) ④ 마지막으로 한도 플래그 — 모든 빌드 환경에 퍼뜨려야 하므로 비추천. 컴파일 시간: 가지치기판 +13 ms, naive 를 한도 풀고 돌리면 5.9 s (clang).

</details>

<details>
<summary>Q3. 왜 부동소수점이 아니라 정수 ppm 이죠?</summary>

세 가지. ① 같은 함수를 런타임 폴백에서도 부르는데, 표준은 번역 중 부동소수점 결과가 실행 중과 같다고 보장하지 않는다 ([expr.const] note: unspecified). 일치 테스트가 의미를 가지려면 비트가 같아야 한다. ② 경계 판정: 20 400 000 Hz / 1 Mbaud 는 정확히 2 %, 20 400 001 은 2.000005 % — 정수 곱셈 비교는 둘을 가르고, 반올림한 ppm(둘 다 20000)은 못 가른다. ③ 타깃 비용: FPU 없는 M0 나 soft-float 빌드에서 런타임 폴백이 `__aeabi_d*` 를 끌어온다. 단, 정수도 공짜가 아니다 — M4 에서 64비트 나눗셈은 `__aeabi_uldivmod` (GCC `clock_init_rt`, clang 의 런타임 baud 경로, 실측). 그래서 게이트는 곱셈만 쓰고 ppm 나눗셈은 표시용으로만 둔다.

</details>

<details>
<summary>Q4. OVER8 로 바꾸면 보레이트 오차가 줄어드나요?</summary>

아니다. OVER16 은 16·USARTDIV, OVER8 은 8·USARTDIV 를 반올림하는데 둘 다 PCLK/baud 를 정수로 반올림하는 것과 같다 → 같은 div, 같은 실제 보레이트 PCLK/div. 16 MHz 에서 921600 은 두 모드 모두 +2.12 % (테스트). OVER8 이 주는 것은 최대 보레이트 2배 (div 하한 16 → 8: 42 MHz 에서 3 Mbaud 가 OVER16 은 TooFast, OVER8 은 오차 0) 이고, 잃는 것은 최저 보레이트(div 상한 65535 → 32767)와 수신 허용 오차다 (비트당 샘플이 적다). 오차를 줄이려면 PCLK 를 바꾼다 — 예: APB2(84 MHz)에 있는 USART 로 옮기거나, 보레이트에 맞는 HSE (14.7456 MHz 류) 를 쓴다.

</details>

<details>
<summary>Q5. 2 % 라는 기준은 어디서 왔죠? 송수신 양쪽을 어떻게 생각해야 하나요?</summary>

UART 수신기는 시작 비트 에지에 맞춘 뒤 약 10 비트 동안 자기 클럭으로 샘플한다. 프레임 끝에서 누적 오차가 반 비트보다 작아야 하므로 이론상 양쪽 오차의 **합** 이 대략 5 % 미만이어야 하고, 노이즈·샘플링 방식으로 더 줄어든다 (ST RM 의 receiver tolerance 표 — 이 레슨에서 숫자는 미확인). 그래서 "한쪽 2 % 이하" 는 상대도 비슷하다는 가정의 흔한 사내 규칙이다. 상대가 USB-UART 브리지처럼 정확하면 2.5 % 까지 허용하기도 한다 (`uart_brr(..., 25'000)` 테스트). 꼬리: "부호가 왜 필요?" → 양쪽이 같은 방향으로 틀리면 상쇄되고 반대면 더해진다. 그래서 ppm 을 부호 있게 보고한다.

</details>

<details>
<summary>Q6. HSE 를 실행 중에야 아는 보드는요? 컴파일 타임 경로는 버리나요?</summary>

두 경로를 둔다. 보드 상수가 확정이면 `Clock<BoardClock{...}>` (빌드 검증, 122 B 의 상수 store). 보드 ID 스트랩·OTP·CSS(크리스털 고장 → HSI 전환)처럼 런타임에 정해지면 `board_init_runtime` 이 실패를 값(`InitStatus`)으로 돌려주고 **실패 시 레지스터를 건드리지 않는다** (테스트: `bad.nlog == 0`). 핵심은 두 경로가 같은 답을 낸다는 증거다: 컴파일 타임 기준표 232 경우와 무작위 2000 경우에서 `PllResult` 가 비트 단위로 같다. 비용: clang 에서 런타임 경로 약 1.7 KB, GCC 0.78 KB + `__aeabi_uldivmod`/`memset`. 지원 HSE 가 몇 개뿐이면 런타임 탐색 대신 **컴파일 타임에 만든 표** (`kRef` 같은 `std::array`) 에서 찾는 것이 더 작고 검증도 끝나 있다 (표 크기 × 32 B vs 탐색 코드 — 어느 쪽이 작은지는 지원 개수에 달림).

</details>

<details>
<summary>Q7. 실패 메시지에 "어떤 값이 틀렸는지" 를 어떻게 넣었죠? C++20 static_assert 메시지는 문자열 리터럴뿐인데요.</summary>

세 경로 (CPP-M14 가 일반론을 맡음). ① consteval: 실패 경로에서 부르는 함수 **이름** 이 규칙, 컴파일러가 붙이는 `in call to 'pll_or_fail({12288000, 168000000, true})'` 가 값. ② 증인 템플릿: 숫자를 템플릿 인자로 실어 `baud_error_over_2pct<true, 21242, 17>` 처럼 찍히게 — enum 조건 `'4 != 4'` 보다 훨씬 낫다. ③ 정수 비교 조건은 clang 이 `expression evaluates to '84000000 <= 42000000'` 로 양변을 보여 준다. 그리고 관찰 하나: clang 은 한 클래스 인스턴스 안에서 실패한 `static_assert` 중 첫 번째만 보고했다 — 가장 기본적인 검사부터 순서대로 둔다.

</details>

<details>
<summary>Q8. 이 검증기를 다른 칩(F7, G4, H7)이나 다른 컴파일러로 옮기면 무엇이 깨지나요?</summary>

칩: 한계값(`f4::`), PLLCFGR 비트 배치(H7 은 PLL 이 여러 개고 분수 N 이 있다 — 분수 N 이면 "정확한 해" 정의가 바뀐다), P 허용값, USART 커널 클럭 소스(G4/H7 은 PCLK 가 아닌 별도 클럭 선택), 그리고 G4/L4 의 LPUART 는 공식이 다르다 (256 배). 그래서 `f4::` 를 칩별 trait 구조체로 빼서 템플릿 인자로 받는 것이 다음 단계다. 컴파일러: 한도 플래그 이름과 단위가 다르다 (clang steps vs GCC ops/loop-limit) — 탐색이 한도의 6 % 이하라 둘 다 통과했다 (VM GCC 14.2.1 에서 `-fsyntax-only` 0.04 s). 구조적 클래스 NTTP (`Clock<BoardClock{...}>`) 는 C++20 이 필요하고, 오래된 툴체인(C++17)이면 필드별 정수 NTTP 로 풀어야 한다.

</details>

## 9. 추가 연습

### 연습 1 — 정확한 해가 없을 때: 허용 오차 안의 최근접 PLL

12.288 MHz 오디오 크리스털로 "168 MHz 근처" 를 원한다. `pll_search_tol(hse, sys, max_ppm)` 은 168 MHz 를 넘지 않는 모든 (M,N,P) 중 |SYSCLK 오차| 최소(동점이면 먼저 찾은 것)를 돌려주고, 그 오차가 `max_ppm` 이하면 `ok`.

<details>
<summary>답 보기</summary>

오차 |HSE·N/(M·P) − SYS| = |HSE·N − SYS·M·P| / (M·P) 를 **분수 그대로** (`num`, `den`) 보관하고 교차 곱 `num·best_den < best_num·den` 으로 비교한다 — 나눗셈 없이 정확. 결과 (독립 파이썬으로 확인): 12.288 MHz → M=9, N=246, P=2 = 167.936 MHz, −381 ppm (1000 ppm 정책 통과, 300 ppm 정책 거부). 14.7456 MHz → 14/319/2, −33 ppm. 24.576 MHz → 100 MHz 는 18/293/4, +107 ppm. 정확한 해가 있으면 (8 MHz) 같은 4/168/2, 0 ppm. 실무 판단: 168 MHz 를 버리고 오디오 쪽 PLL(I2S PLL)에 정확한 클럭을 주는 것이 보통 더 낫다 — SYSCLK 가 381 ppm 틀리는 것은 대부분 상관없지만 USB 48 MHz 는 안 된다.

</details>

### 연습 2 — 반올림 div 가 "보레이트" 오차를 최소화하지 않는 경우

`round(PCLK/baud)` 는 비트 **시간** 오차를 최소화한다. 보레이트(rate) 오차 |PCLK/V − baud| 를 최소화하는 `rate_optimal_div` 를 쓰고 둘이 다른 예를 찾아라.

<details>
<summary>답 보기</summary>

x = PCLK/baud, V₁ = ⌊x⌋, V₂ = V₁+1 일 때 rate 오차는 b(x−V₁)/V₁ 와 b(V₂−x)/V₂ — 가중치가 1/V 라서 두 오차가 같아지는 점은 산술 중점이 아니라 조화 평균 2V₁V₂/(V₁+V₂) 다. V₁=8 이면 8.4706. x 가 (8.4706, 8.5) 에 있으면 반올림은 8, rate 최적은 9. 예: 42 MHz, 4 952 830 baud (x = 8.48) → V=8 은 +6.0 %, V=9 는 −5.78 %. 비교는 정수로 `(pclk − lo·b)·hi ≤ (hi·b − pclk)·lo`. 42 MHz 에서 9600..5 Mbaud 를 977 간격으로 훑으면 84 경우가 달랐다 — 전부 div 가 작은(고속) 쪽. 어느 쪽이 옳은가: 수신기가 누적하는 것은 비트 시간 오차이므로 RM 의 반올림(시간 기준)이 합리적이고, ST 의 오차 표는 rate 로 표시한다. 차이는 div 가 작을 때만 의미 있다.

</details>

### 연습 3 — 해를 모두 세고, 컴파일 타임 배열로

`count_pll(hse, sys, usb, MNP* out = nullptr)` 와 `all_pll<Hse, Sys, Usb>()` (결과 `std::array<MNP, count>`) 를 써라. BM-B10 의 "모두 찾기" 를 C++ 로.

<details>
<summary>답 보기</summary>

같은 함수를 두 번: 한 번은 개수만 세서 배열 크기(템플릿 인자 = 상수식)로, 한 번은 `a.data()` 에 채운다. C++20 은 상수 평가 안에서 `std::array` 를 지역 변수로 만들고 포인터로 써도 된다. 8 MHz/168/USB 는 5개: (4,168,2) (5,210,2) (6,252,2) (7,294,2) (8,336,2) — 전부 VCO 336. 25 MHz 는 1개, 12.288 MHz 는 0개. 이 배열은 Q6 의 "지원 HSE 표" 를 만드는 재료다.

</details>

### 연습 4 — APB 타이머 클럭 ×2 와 1 µs 틱 프리스케일러

`timer_clock(hclk, apb_div)` 와 `Tick1MHz<TimClk>::psc` (TimClk 가 MHz 의 정수배가 아니면 static_assert). 168 MHz, APB1 /4 의 TIM2 는?

<details>
<summary>답 보기</summary>

RM0090 대표값 (TIMPRE = 0): APB 분주가 1 이면 TIMxCLK = PCLK, 아니면 2 × PCLK. APB1 /4 → PCLK1 42 MHz → TIM2 는 **84 MHz** → PSC = 83. APB2 /2 → 168 MHz → PSC = 167. 흔한 버그는 TIM2 클럭을 42 MHz 로 계산해 모든 타이밍이 2배 빨라지는 것 — "PCLK1 = 42" 만 외운 결과. 이 규칙도 `Clock<B>` 의 계산에 넣어 두면 한 번에 검증된다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 13/13 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer practice.cpp -o pr_asan && ./pr_asan   # 13/13, 보고 없음
```

## 10. 핵심 정리

- 클럭 설정은 빌드에서 잡을 수 있다: 보드 상수 → `consteval` PLL 탐색 → `static_assert` 버스 한도 → BRR 게이트. 틀리면 에러에 값(`pll_or_fail({12288000, ...})`, `baud_error_over_2pct<true, 21242, 17>`)이 찍힌다.
- 정확성은 정수 곱셈 등식으로: `HSE·N == SYSCLK·M·P`, `|PCLK − div·baud|·1e6 ≤ max_ppm·div·baud`. 반올림한 ppm 은 표시용 (2 % 경계를 못 가름).
- BRR: 두 모드 모두 div = round(PCLK/baud). OVER16 은 `div`, OVER8 은 `(div>>3)<<4 | (div&7)`. OVER8 은 오차가 아니라 최대 속도를 바꾼다.
- `constexpr` ≠ 빌드 중 실행. 식 안 호출은 -O0 에서 런타임에 돌았다. 보장은 constexpr 변수와 consteval (그 함수 하나만).
- 한도: clang 1 048 576 step (실측), GCC 33 554 432 ops. 가지치기 없는 탐색은 해가 없을 때 "possible infinite loop?" 로 깨진다. 제약을 루프 범위로.
- Cortex-M4 실측: 컴파일 타임 경로 122 B, 상수 store + RDY 폴링뿐. 런타임 폴백 clang 1.7 KB / GCC 0.78 KB + `__aeabi_uldivmod`. 두 경로는 232 + 2000 경우 비트 단위로 일치.
- 초기화 순서: wait state 를 먼저 올리고 SW=PLL.

## 참고자료

- C++ 표준 (N4861 C++20 초안): [dcl.constexpr] (constexpr / consteval 함수), [expr.const] (상수식, immediate invocation, 부동소수점 결과에 관한 note), [temp.param] (구조적 클래스 타입 NTTP), [basic.start.static] (상수 초기화), [temp.res] / [dcl.pre] (static_assert), Annex B [implimits] (core constant expression 권장 한도) — <https://isocpp.org/files/papers/N4861.pdf>
- cppreference — consteval specifier: <https://en.cppreference.com/w/cpp/language/consteval>
- Clang 사용자 매뉴얼 — `-fconstexpr-steps`, `-fconstexpr-depth`: <https://clang.llvm.org/docs/UsersManual.html>
- GCC C++ Dialect Options — `-fconstexpr-ops-limit`, `-fconstexpr-loop-limit`: <https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Dialect-Options.html>
- STMicroelectronics RM0090 (STM32F405/415/407/417/427/437/429/439 Reference Manual) — RCC (PLLCFGR, CFGR), FLASH_ACR, USART 보레이트 생성 절. 이 레슨의 숫자는 대표값으로 사용, 원문 대조 미확인: <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf>
- P2741R3 (C++26 user-generated static_assert messages): <https://wg21.link/P2741>
- 관련 레슨: BM-B10 (PLL·wait state 유도, C), BM-B12 (BRR·폴링 UART, C), CPP-M14 (static_assert 메시지 기법), CPP-M13 (constexpr 표·.rodata·step 한도 첫 관찰), CPP-M01 (Reg·버스 정책), CPP-B12 (본문 static_assert 는 SFINAE 아님), CPP-A03 (`__aeabi_*` 런타임 의존성), CPP-A04 (cross-target 스크립트 패턴)
