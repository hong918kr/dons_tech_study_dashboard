---
id: BM-M10
time: 80분
updated: 2026-11-11
---
# BM-M10 워치독 하드웨어: IWDG/WWDG 타임아웃·윈도우 계산

## 학습 목표

- IWDG 타임아웃 `4·2^PR·(RLR+1)/f_LSI` 를 손으로 계산하고, LSI 오차(F4 데이터시트 17–47 kHz)를 넣었을 때 **"정상 동작에선 안 터지고, 멈추면 제때 터지는"** PR/RLR 이 존재하려면 요구 창의 비가 47/17 = 2.76 이상이어야 함을 설명할 수 있다.
- WWDG 카운터(T, 0x7F → 0x3F)와 윈도 값 W 로 **가장 이른 / 가장 늦은 합법 refresh 시각**을 프리스케일러 위상과 무관하게 보장되는 값으로 계산하고, EWI(early wakeup interrupt)가 정상 동작 중 헛울리지 않게 고를 수 있다.
- KR 키 시퀀스(0xCCCC 시작, 0x5555 잠금 해제, 0xAAAA 재적재), PVU/RVU busy, "한 번 켜면 못 끈다", DBGMCU freeze 를 mock 모델로 재현하고 드라이버 초기화 순서를 이유와 함께 말할 수 있다.
- 부팅 때 RCC_CSR 로 리셋 원인을 읽고 RMVF 로 지우는 이유(안 지우면 두 번의 리셋이 섞인다)를 테스트로 보인다.
- 10 ms 제어 루프에 WWDG(빠른 검출, 너무 이른 kick 검출) + IWDG(클럭 고장 대비)를 같이 거는 설계를 숫자로 방어한다.

## 1. 문제

워치독(watchdog)은 "펌웨어가 멈추면 칩을 리셋하는" 마지막 안전장치다. 면접에서는 거의 반드시 **"IWDG 타임아웃이 몇 ms 죠? LSI 가 ±50 % 틀리면요?"**, **"윈도 워치독을 너무 일찍 킥하면 왜 리셋이죠?"**, **"디버깅하다 브레이크포인트에서 보드가 리셋돼요"**, **"리셋 뒤 원인은 어떻게 알죠?"** 가 나온다. 공식은 한 줄이지만, 실제로는 RC 발진기 오차·프리스케일러 위상·레지스터 쓰기 보호·플래그 지우기 같은 디테일에서 사고가 난다. SSD 컨트롤러에서 FTL 이 GC 루프에 갇혔을 때 호스트 타임아웃보다 먼저 컨트롤러를 살려 내는 것도, 드론 모터 제어 루프가 멈췄을 때 출력을 끄는 것도 이 계산 위에 서 있다.

STM32F4 에는 워치독이 두 개 있다 (주소·비트는 RM0090 기준 대표값 — RM 확인 권장, 테스트의 정답은 이 레슨 mock 의 정의).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">IWDG (독립, LSI) 와 WWDG (윈도, PCLK1) — 이 레슨 mock 의 구조</text>
  <rect x="10" y="34" width="110" height="46" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="18" y="54" fill="currentColor">LSI RC</text>
  <text x="18" y="70" fill="currentColor">17–47 kHz</text>
  <rect x="150" y="34" width="120" height="46" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="158" y="54" fill="currentColor">÷ 4·2^PR</text>
  <text x="158" y="70" fill="currentColor">PR 0..6 (PVU)</text>
  <rect x="300" y="34" width="170" height="46" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="308" y="54" fill="currentColor">12비트 다운카운터</text>
  <text x="308" y="70" fill="currentColor">0 에서 다음 tick → 리셋</text>
  <rect x="500" y="34" width="230" height="46" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="508" y="54" fill="currentColor">IWDG 리셋 → RCC_CSR.IWDGRSTF</text>
  <text x="508" y="70" fill="currentColor">LSI 도메인: 메인 클럭이 죽어도 돈다</text>
  <path d="M120 57 L150 57 M270 57 L300 57 M470 57 L500 57" stroke="currentColor"/>
  <text x="300" y="100" fill="currentColor">KR=0xAAAA → 카운터 ← RLR (RVU) / 0xCCCC 시작 / 0x5555 PR·RLR 쓰기 허용</text>
  <rect x="10" y="130" width="110" height="46" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="18" y="150" fill="currentColor">PCLK1</text>
  <text x="18" y="166" fill="currentColor">42 MHz</text>
  <rect x="150" y="130" width="120" height="46" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="158" y="150" fill="currentColor">÷ 4096·2^WDGTB</text>
  <text x="158" y="166" fill="currentColor">tick 97.5 µs·2^TB</text>
  <rect x="300" y="130" width="170" height="46" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="308" y="150" fill="currentColor">7비트 카운터 T[6:0]</text>
  <text x="308" y="166" fill="currentColor">꺼져 있어도 free-running</text>
  <rect x="500" y="122" width="230" height="30" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="508" y="142" fill="currentColor">0x40 도달 → EWIF (+EWI 인터럽트)</text>
  <rect x="500" y="158" width="230" height="30" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="508" y="178" fill="currentColor">0x40→0x3F (T6=0) → 리셋 WWDGRSTF</text>
  <path d="M120 153 L150 153 M270 153 L300 153 M470 145 L500 137 M470 160 L500 173" stroke="currentColor"/>
  <rect x="300" y="200" width="430" height="30" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="308" y="220" fill="currentColor">CR 쓰기(refresh) 때 카운터 &gt; W → "너무 이른 킥" 리셋</text>
  <rect x="10" y="250" width="720" height="68" rx="6" fill="none" stroke="currentColor" stroke-dasharray="5 3"/>
  <text x="20" y="270" fill="currentColor">DBGMCU_APB1_FZ (0xE0042008): DBG_WWDG_STOP bit11 / DBG_IWDG_STOP bit12 — 코어가 halt 된 동안 카운터 정지</text>
  <text x="20" y="288" fill="currentColor">RCC_CSR (0x40023874): LPWR31 WWDG30 IWDG29 SFT28 POR27 PIN26 BOR25 | RMVF24 (1 쓰면 전부 지움)</text>
  <text x="20" y="306" fill="currentColor">리셋 플래그는 시스템 리셋에선 남고 전원 리셋에서만 지워진다 → 부팅 코드가 읽고 지운다</text>
</svg>
```

이 레슨은 맥에서 돈다. 진짜 칩 대신 **µs 단위로 도는 결정적(deterministic) 시뮬레이터**를 쓴다: LSI 와 PCLK1 을 분수 누산기로 쪼개 IWDG/WWDG 카운터를 움직이고, 레지스터 접근은 `REG_READ/REG_WRITE` 이음매(BM-B02 mock 축약판)로 모델에 들어가며, WWDG EWI 는 NVIC-lite(`sim_step()` 이 라인이 서 있고 enable 이면 ISR 을 동기 호출)로 들어온다. 리셋이 나면 모델은 레지스터를 초기화하고 RCC_CSR 에 원인을 남기며, `.noinit` 을 흉내 낸 구조체는 살아남는다.

### Part A — 공식과 고르기 (순수 함수)

- `uint64_t iwdg_timeout_us(pr, rlr, lsi_hz)` — 내림. RM0090 표(32 kHz: /4 → 0.125–512 ms … /256 → 8–32768 ms) 재현.
- `int iwdg_pick(min_ms, max_ms, lsi_min_hz, lsi_max_hz, iwdg_cfg_t *out)` — LSI 가 범위 어디에 있든 **타임아웃 ∈ [min_ms, max_ms]** 인 (PR, RLR). PR 작은 것 → RLR 작은 것. 불가능 −1, 인자 오류 −2. 전수 탐색(7 × 4096)과 3000 개 고정 시드 케이스로 비교.
- `int wwdg_window_us(pclk1, wdgtb, T, W, wwdg_win_t *out)` — refresh 직후부터 잰 `earliest_us`(올림), `quiet_us`(EWI 전, 내림), `latest_us`(리셋 전, 내림), `reset_us`(늦어도 리셋, 올림).

### Part B — mock 워치독과 고장 주입

- `iwdg_init(pr, rlr)` — 0xCCCC → 0x5555 → PR → RLR → SR(PVU|RVU)==0 유한 대기 → 0xAAAA. 접근 로그로 순서 검증.
- 주입: 굶기기(starvation) 리셋 시각, 0x5555 없는 PR/RLR 쓰기 무시, PVU busy 중 재기록 무시, RVU busy 중 kick 은 옛 RLR, LSI 꺼진 채 "설정 먼저" → busy 영원히, 한 번 켠 IWDG 는 어떤 키로도 안 멈춤, WWDG 너무 이른 refresh, 창 안 refresh 200 ms 생존, 굶기면 EWI 1 회 후 리셋, **임의 위상 256 케이스**에서 earliest/latest 경계 보장, `CR |= WDGA` RMW 즉시 리셋, EWIF 찌꺼기, DBGMCU freeze, 리셋 원인 + RMVF.
- `reset_cause_decode(csr)` / `reset_cause_read_and_clear(&raw)`.

### Part C — 10 ms 제어 루프

- `int wwdg_pick(pclk1, min_us, max_us, bool ewi, wwdg_cfg_t *out)` — 닫힌 식으로 풀고 전수 탐색(4 × 64 × 64)과 1000 케이스 비교.
- `WWDG_IRQHandler()` — EWIF 지우기, "유언(last words)" 을 `.noinit` 에 남기고 출력 안전 상태. kick 금지.
- 부팅(`fw_boot`) → 루프 10 ms ± 1 ms 지터 → 고장 주입 4 종(정상 / 멈춤 / 폭주 / PCLK1 정지) → 다음 부팅의 원인 보고.

## 2. Problem (English)

> **Independent and window watchdog configuration on an STM32F4-class MCU.**
>
> 1. `iwdg_timeout_us(pr, rlr, lsi_hz)` = ⌊4·2^PR·(RLR+1)·10⁶ / f_LSI⌋. `iwdg_pick(min_ms, max_ms, lsi_min, lsi_max, &cfg)`: choose PR ∈ 0..6, RLR ∈ 0..4095 such that the timeout is ≥ `min_ms` at the fastest LSI and ≤ `max_ms` at the slowest LSI; prefer the smallest PR, then the smallest RLR. Return −1 if impossible.
> 2. `wwdg_window_us(pclk1, wdgtb, T, W, &win)`: the counter ticks every 4096·2^WDGTB PCLK1 cycles from T down; a refresh while counter > W resets; the 0x40→0x3F transition resets; EWIF is set at 0x40. The prescaler phase at refresh is unknown. Return the guaranteed earliest legal refresh, the latest refresh that avoids EWI, the latest legal refresh and the latest reset time.
> 3. Bring-up driver on a register mock: `iwdg_init`, `wwdg_init`, `reset_cause_read_and_clear` (RCC_CSR + RMVF), and an EWI ISR that records last words in `.noinit` RAM.
> 4. `wwdg_pick(pclk1, min_us, max_us, ewi, &cfg)` for a 10 ms ± 1 ms control loop at PCLK1 = 42 MHz.
>
> Example: `iwdg_pick(100, 2000, 17000, 47000)` → PR=0, RLR=1174, 100.000–276.471 ms; `iwdg_pick(100, 250, 17000, 47000)` → −1 (250/100 < 47/17). `wwdg_window_us(42 MHz, 0, 0x7F, 0x5F)` → earliest 3121 µs, latest 6144 µs, reset ≤ 6242 µs. `wwdg_pick(42 MHz, 9000, 11000, ewi=true)` → WDGTB=1, T=0x7A, W=0x4C.
>
> Follow-ups: why can't you stop the IWDG? What happens at a breakpoint? Why must the reset flags be cleared? Why is kicking from a timer ISR a bad idea?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "31/97 passed", exit 1 (mock 자체 동작을 보는 테스트만 통과) — TODO 8곳을 채워 97/97
```

TODO 는 8 곳: A1 `iwdg_timeout_us`, A2 `iwdg_pick`, A3 `wwdg_window_us`, B1 `iwdg_init`, B2 `reset_cause_decode` + `reset_cause_read_and_clear`, C1 `wwdg_pick`, C2 `WWDG_IRQHandler`. mock·하네스·`wwdg_init`·`iwdg_kick`·`wwdg_kick` 은 주어진다. 스텁 상태에서도 모든 시뮬레이션 루프는 µs 상한이 있어 멈추지 않는다.

관찰용 파일:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_tables.c -o demo_tables && ./demo_tables               # IWDG/WWDG 표, "고를 수 있나" 지도
cc -std=c11 -Wall -Wextra -Werror -O2 demo_wwdg_phase.c -o demo_wwdg_phase && ./demo_wwdg_phase   # 위상 4096 가지 refresh 결과
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -ffreestanding -DTARGET_BUILD -c solution.c -o wd.o
xcrun llvm-objdump -d --no-show-raw-insn wd.o                                                    # 같은 드라이버의 진짜 Thumb-2
```

## 4. 힌트

<details>
<summary>힌트 1 — A2: 두 부등식을 정수로</summary>

ticks = 4·2^PR·(RLR+1). "빠른 LSI 에서도 ≥ min_ms" 는 ticks / lsi_max ≥ min_ms / 1000, 즉 **ticks·1000 ≥ min_ms·lsi_max**. "느린 LSI 에서도 ≤ max_ms" 는 **ticks·1000 ≤ max_ms·lsi_min**. 둘 다 `uint64_t` 로. PR 하나를 고정하면 RLR+1 의 최소값은 올림 나눗셈 한 번으로 나온다.

</details>

<details>
<summary>힌트 2 — A2: 왜 PR 을 키워도 소용없는 경우가 있나</summary>

PR 을 고정했을 때 가능한 ticks 는 step = 4·2^PR 의 배수. min 을 넘는 가장 작은 배수가 max 를 넘으면 그 PR 은 실패. 더 큰 PR 은 step 이 더 굵으니 "min 을 넘는 가장 작은 배수" 가 더 작아질 수 없다 — 그래도 RLR 이 4096 을 넘어 건너뛴 PR 이 있을 수 있으니 루프는 끝까지 돌린다.

</details>

<details>
<summary>힌트 3 — A3: 위상을 모를 때의 보장값</summary>

refresh 순간 프리스케일러 누산기 φ ∈ [0, 1 tick). k 번째 감소는 (k−φ')·tick 근처 어딘가 — 정확히는 ((k−1)·tick, k·tick] 안. 그러니 "T−W 번 감소가 **반드시** 끝난" 시각은 (T−W)·tick 이고(올림), "T−0x3F 번째 감소가 **아직** 안 온" 시각은 (T−0x40)·tick 까지(내림)다. EWI 를 피하려면 0x40 이 되는 T−0x40 번째 감소 전 = (T−0x41)·tick 까지.

</details>

<details>
<summary>힌트 4 — B1: 왜 시작(0xCCCC)이 먼저인가</summary>

PR/RLR 값은 LSI 도메인으로 넘어가야 반영되고, 그동안 PVU/RVU 가 1 이다. LSI 가 꺼져 있으면 영원히 1. 0xCCCC 는 IWDG 를 시작하면서 **LSI 를 강제로 켠다** (RM0090: "forced ON and cannot be disabled"). 대기 루프는 반드시 유한하게.

</details>

<details>
<summary>힌트 5 — C1: 닫힌 식</summary>

latest(또는 quiet) ≥ max_us ⇔ (T − base) ≥ ⌈max_us·pclk / (cyc·10⁶)⌉ (base = 0x40, EWI 면 0x41). earliest ≤ min_us ⇔ (T − W) ≤ ⌊min_us·pclk / (cyc·10⁶)⌋. WDGTB 를 0 부터 올리며 T 가 0x7F 안에 들어오는 첫 경우를 잡고, W = max(0x40, T − k).

</details>

## 5. 풀이

### 0. 두 워치독 한눈에

| | IWDG (independent) | WWDG (window) |
|---|---|---|
| 클럭 | LSI RC (F4 데이터시트 17/32/47 kHz min/typ/max — 확인 권장) | PCLK1 / 4096 / 2^WDGTB |
| 카운터 | 12비트 다운, RLR(0..0xFFF)에서 시작 | 7비트 T[6:0], 0x7F..0x40 이 유효 구간 |
| 리셋 조건 | 0 에 도달 (모델: (RLR+1) prescaled tick 뒤 — RM 표와 일치) | 0x40 → 0x3F, **또는** 카운터 > W 일 때 refresh, **또는** T6=0 을 쓰기 |
| kick | KR ← 0xAAAA | CR ← WDGA \| T (T6=1 필수) |
| 끄기 | 불가 (리셋만) — 하드웨어 옵션(WDG_SW=0)이면 전원 켜자마자 시작 | 불가 (WDGA 는 set-only) |
| 조기 경보 | 없음 | EWI: 0x40 에서 인터럽트, 리셋까지 정확히 1 tick |
| 잡는 고장 | 모든 것 + **메인 클럭 고장** | 멈춤 + **너무 빠른/폭주 루프** (시간 창 위반) |
| 정밀도 | 나쁨 (RC, ±50 % 급) | 좋음 (크리스털 기반 PCLK1) |

### Part A — IWDG: 공식은 쉽고, 오차가 어렵다

RM0090 표를 공식이 그대로 재현하는지부터 본다 (`demo_tables` 실제 출력, 32 kHz 열이 RM 표와 같다):

```text
== IWDG timeout (ms) = 4*2^PR*(RLR+1)/f_LSI ==
PR  div  | RLR=0: 17k     32k     47k   | RLR=0xFFF: 17k        32k        47k
0  /4    |   0.235   0.125   0.085  |      963.8      512.0      348.6
1  /8    |   0.471   0.250   0.170  |     1927.5     1024.0      697.2
2  /16   |   0.941   0.500   0.340  |     3855.1     2048.0     1394.4
3  /32   |   1.882   1.000   0.681  |     7710.1     4096.0     2788.8
4  /64   |   3.765   2.000   1.362  |    15420.2     8192.0     5577.5
5  /128  |   7.529   4.000   2.723  |    30840.5    16384.0    11155.1
6  /256  |  15.059   8.000   5.447  |    61680.9    32768.0    22310.1
```

"PR=6, RLR=0xFFF 면 32.8 초" 라고 외우면 틀린다. 같은 설정이 **22.3 s ~ 61.7 s** 다. 그래서 요구사항은 하나의 숫자가 아니라 두 개다:

- **min_ms** — 정상 동작에서 kick 간격의 최악값(+여유). 이보다 일찍 터지면 멀쩡한 시스템을 리셋한다 → 가장 **빠른** LSI 로 검사.
- **max_ms** — 멈춘 뒤 이 시간 안에 반드시 리셋돼야 한다 (안전 요구) → 가장 **느린** LSI 로 검사.

ticks/lsi_max ≥ min, ticks/lsi_min ≤ max 를 나누면 **max/min ≥ lsi_max/lsi_min = 47/17 = 2.765** 가 필요조건이다. 즉 "100 ms 안에 kick 하고, 멈추면 250 ms 안에 리셋" 은 보정하지 않은 LSI 로는 **원리적으로 불가능** — 테스트의 `iwdg_pick(100, 250, …) == -1`. 아래 지도(실제 출력)에서 17–47 kHz 줄은 비 2.8 부터 열리고, 측정값 ±10 % 로 보정한 줄(연습 1, 비 1.222)은 1.3 부터 열린다. min 1 ms 줄만 늦게 열리는 건(17–47 kHz 에선 3.0, 보정 후엔 2.0) 해상도 때문이다 — PR=0 한 칸이 4 LSI 사이클 ≈ 0.085–0.235 ms 라 1 ms 근처에선 칸이 굵다.

```text
== pickable? min_ms x (max_ms/min_ms), LSI 17-47 kHz (ratio 2.765) vs calibrated 28.8-35.2 kHz (1.222) ==
min_ms  |  x1.2            x1.3            x1.5            x2.0            x2.5            x2.8            x3.0
-- LSI 17000..47000 Hz --
     1  |  --               --               --               --               --               --               PR0 RLR=11
    10  |  --               --               --               --               --               PR0 RLR=117      PR0 RLR=117
   100  |  --               --               --               --               --               PR0 RLR=1174     PR0 RLR=1174
  1000  |  --               --               --               --               --               PR2 RLR=2937     PR2 RLR=2937
 10000  |  --               --               --               --               --               PR5 RLR=3671     PR5 RLR=3671
-- LSI 28800..35200 Hz --
     1  |  --               --               --               PR0 RLR=8        PR0 RLR=8        PR0 RLR=8        PR0 RLR=8
    10  |  --               PR0 RLR=87       PR0 RLR=87       PR0 RLR=87       PR0 RLR=87       PR0 RLR=87       PR0 RLR=87
   100  |  --               PR0 RLR=879      PR0 RLR=879      PR0 RLR=879      PR0 RLR=879      PR0 RLR=879      PR0 RLR=879
  1000  |  --               PR2 RLR=2199     PR2 RLR=2199     PR2 RLR=2199     PR2 RLR=2199     PR2 RLR=2199     PR2 RLR=2199
 10000  |  --               PR5 RLR=2749     PR5 RLR=2749     PR5 RLR=2749     PR5 RLR=2749     PR5 RLR=2749     PR5 RLR=2749
```

예제 `iwdg_pick(100, 2000, 17000, 47000)` 을 손으로: PR=0 → step 4, RLR+1 ≥ ⌈100·47000 / 4000⌉ = 1175 → RLR = 1174, ticks = 4700. 47 kHz 에서 100.000 ms, 32 kHz 에서 146.9 ms, 17 kHz 에서 276.471 ms(올림) ≤ 2000 ✓.

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">PR=0, RLR=1174 (ticks 4700) — 같은 설정, LSI 에 따라 타임아웃이 움직인다 (2.3 px/ms)</text>
  <line x1="40" y1="120" x2="720" y2="120" stroke="currentColor"/>
  <text x="34" y="138" fill="currentColor">0</text>
  <line x1="270" y1="114" x2="270" y2="126" stroke="currentColor"/>
  <text x="250" y="138" fill="currentColor">100 ms</text>
  <line x1="500" y1="114" x2="500" y2="126" stroke="currentColor"/>
  <text x="480" y="138" fill="currentColor">200 ms</text>
  <rect x="40" y="70" width="230" height="36" class="f-ok" stroke="currentColor"/>
  <text x="48" y="92" fill="currentColor">kick 은 여기 안에 (≤ min_ms)</text>
  <rect x="270" y="70" width="406" height="36" class="f-warn" stroke="currentColor"/>
  <text x="290" y="92" fill="currentColor">리셋 시각: 47 kHz 100.0 ms … 17 kHz 276.5 ms</text>
  <line x1="270" y1="40" x2="270" y2="70" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="200" y="36" fill="currentColor">47 kHz: 100.0</text>
  <line x1="378" y1="48" x2="378" y2="70" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="340" y="58" fill="currentColor">32 kHz: 146.9</text>
  <line x1="676" y1="40" x2="676" y2="70" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="600" y="36" fill="currentColor">17 kHz: 276.5</text>
  <text x="40" y="164" fill="currentColor">필요조건: max_ms / min_ms ≥ lsi_max / lsi_min = 47/17 = 2.765 → "100 ms 이내 kick, 250 ms 이내 리셋" 은 불가능</text>
  <text x="40" y="184" fill="currentColor">해법: LSI 를 재서(TIM 입력 캡처) 범위를 좁히거나, 정밀한 검출은 WWDG 에 맡기고 IWDG 는 넉넉한 백스톱으로</text>
</svg>
```

모델은 kick(0xAAAA) 순간 프리스케일러를 0 으로 되돌리지만 LSI 자체의 위상(다음 LSI 에지까지 남은 시간)은 모른다. 그래서 47 kHz 에서 실제 모델 리셋은 **99 997 µs** 로 공식보다 LSI 한 주기(21 µs) 안쪽에서 일찍 왔다 (`./solution -v` 첫 줄). 실칩도 KR 쓰기가 LSI 도메인으로 동기화되는 만큼의 불확실성이 있으니(RM 은 위상을 말하지 않음) min_ms 에는 항상 여유를 둔다.

### Part A — WWDG: "보장" 창

WWDG 는 정밀하지만 위상 문제가 있다. RM 은 refresh 가 프리스케일러(÷4096)를 리셋하는지 말하지 않는다 — 이 모델은 **리셋하지 않는다(free-running)** 고 보수적으로 잡는다. refresh 순간 위상 φ 를 모르니, k 번째 감소는 ((k−1)·tick, k·tick] 어딘가에서 온다. 여기서 네 개의 보장값이 나온다 (tick = 4096·2^WDGTB / PCLK1):

| 값 | 식 | 반올림 | T=0x7F, W=0x5F, WDGTB=0, 42 MHz |
|---|---|---|---|
| earliest — 이후엔 항상 카운터 ≤ W | (T−W)·tick | 올림 | 32·97.524 = 3120.76 → **3121 µs** |
| quiet — 그때까진 EWI 없음 | (T−0x41)·tick | 내림 | 62·97.524 = 6046.48 → 6046 µs |
| latest — 그때까진 아직 리셋 전 | (T−0x40)·tick | 내림 | 63·97.524 = **6144 µs** (딱 떨어짐) |
| reset — 늦어도 리셋 | (T−0x3F)·tick | 올림 | 64·97.524 = 6241.52 → **6242 µs** |

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">WWDG 카운터 (T=0x7F, W=0x5F, WDGTB=0, PCLK1=42 MHz, tick 97.52 µs)</text>
  <line x1="60" y1="200" x2="720" y2="200" stroke="currentColor"/>
  <line x1="60" y1="40" x2="60" y2="200" stroke="currentColor"/>
  <text x="18" y="54" fill="currentColor">0x7F</text>
  <text x="18" y="118" fill="currentColor">0x5F W</text>
  <text x="18" y="178" fill="currentColor">0x40</text>
  <text x="18" y="196" fill="currentColor">0x3F</text>
  <line x1="60" y1="114" x2="720" y2="114" stroke="currentColor" stroke-dasharray="5 3"/>
  <line x1="60" y1="50" x2="660" y2="178" stroke="currentColor"/>
  <line x1="660" y1="178" x2="680" y2="192" stroke="currentColor"/>
  <rect x="60" y="208" width="300" height="22" class="f-warn" stroke="currentColor"/>
  <text x="70" y="223" fill="currentColor">refresh = 리셋 (카운터 &gt; W)</text>
  <rect x="360" y="208" width="300" height="22" class="f-ok" stroke="currentColor"/>
  <text x="370" y="223" fill="currentColor">합법 창 3121 … 6144 µs</text>
  <rect x="660" y="208" width="60" height="22" class="f-warn" stroke="currentColor"/>
  <text x="664" y="223" fill="currentColor">리셋</text>
  <line x1="360" y1="40" x2="360" y2="200" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="300" y="36" fill="currentColor">earliest 3121</text>
  <line x1="660" y1="40" x2="660" y2="200" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="560" y="36" fill="currentColor">latest 6144 · reset ≤ 6242</text>
  <circle cx="650" cy="176" r="5" class="f-acc" stroke="currentColor"/>
  <text x="520" y="160" fill="currentColor">0x40: EWIF/EWI</text>
  <text x="60" y="250" fill="currentColor">경계 근처 1 tick 은 위상에 따라 결과가 갈린다 → 보장값은 그 tick 을 버린다</text>
</svg>
```

정말 그런지 위상 4096 가지를 다 세 본 `demo_wwdg_phase` 실제 출력:

```text
WDGTB=0 T=0x7F W=0x5F PCLK1=42000000 Hz, tick=97.524 us
t(us)    EARLY     OK       RESET
  3000    100.0%     0.0%     0.0%
  3023    100.0%     0.0%     0.0%
  3024     99.2%     0.8%     0.0%
  3050     72.6%    27.4%     0.0%
  3100     21.3%    78.7%     0.0%
  3120      0.8%    99.2%     0.0%
  3121      0.0%   100.0%     0.0%
  3200      0.0%   100.0%     0.0%
  6000      0.0%   100.0%     0.0%
  6046      0.0%   100.0%     0.0%
  6047      0.0%   100.0%     0.0%
  6100      0.0%   100.0%     0.0%
  6144      0.0%   100.0%     0.0%
  6145      0.0%    99.0%     1.0%
  6200      0.0%    42.6%    57.4%
  6241      0.0%     0.5%    99.5%
  6242      0.0%     0.0%   100.0%
  6300      0.0%     0.0%   100.0%
```

3024–3120 µs 와 6145–6241 µs 가 "운에 맡기는" 구간이고, 3121 / 6144 / 6242 가 정확히 표의 보장값이다. 하네스의 B11 테스트도 임의 WDGTB/T/W/위상 256 케이스에서 같은 성질(earliest·latest 에선 항상 OK, earliest−1 tick 전엔 항상 리셋, 방치하면 (latest, reset_us] 안에서 리셋)을 확인한다.

참고로 RM0090 Table 109 는 PCLK1 = **30 MHz** 기준(136.53 µs … 69.91 ms)이고, 42 MHz 값(97.52 µs … 49.93 ms)은 같은 공식으로 계산한 것이다 — 연습 2 가 둘 다 정수 ns 로 재현한다.

### Part B — mock 모델이 지키는 규칙

| 레지스터 | 모델 동작 (RM0090 기준 — 확인 권장) |
|---|---|
| IWDG_KR | 0x5555 = PR/RLR 쓰기 허용. **그 밖의 모든 값 = 다시 잠금**. 0xAAAA = 카운터 ← (반영된) RLR. 0xCCCC = 시작 + LSI 강제 ON. 멈추는 키는 없다. 읽으면 0 |
| IWDG_PR / RLR | 잠겨 있거나 PVU/RVU=1 이면 쓰기 무시(카운트). 쓰면 5 LSI 사이클 동안 busy, 끝나야 카운터 쪽에 반영 (RM: "up to 5 RC 40 kHz cycles") |
| IWDG_SR | PVU bit0 / RVU bit1, read-only |
| WWDG_CR | WDGA set-only. refresh 때 (WDGA=1 && 카운터 > W) → 리셋. 쓴 뒤 WDGA=1 && T6=0 → 즉시 리셋 |
| WWDG_CFR | W[6:0], WDGTB[8:7], EWI bit9 set-only |
| WWDG_SR | EWIF bit0, rc_w0. 카운터가 0x40 이 되면 EWI 꺼져 있어도 선다 (RM 문장 그대로) |
| RCC_CSR | 리셋 시 원인 비트 + PINRSTF 를 OR, LSION 은 0 으로. RMVF=1 쓰기 → 비트 31..25 전부 0. LSIRDY = LSI 동작 중 |
| DBGMCU_APB1_FZ | halt 중 bit12 → IWDG 정지, bit11 → WWDG 정지 |

`iwdg_init(0, 99)` 의 버스 접근 순서 (접근 로그를 테스트가 검사):

| # | 접근 | 모델 상태 변화 | 이유 |
|---|---|---|---|
| 1 | W KR=0xCCCC | started=1, LSI 강제 ON, cnt=0xFFF | PVU/RVU 는 LSI 가 돌아야 끝난다 |
| 2 | W KR=0x5555 | unlocked=1 | 쓰기 보호 해제 |
| 3 | W PR=0 | PVU=1 (5 LSI cyc) | |
| 4 | W RLR=99 | RVU=1 | |
| 5.. | R SR (10 µs 간격, ≤ 100 회) | 156 µs 뒤 SR=0 | 안 기다리고 kick 하면 옛 RLR(0xFFF)로 적재 — B4 테스트 |
| 끝 | W KR=0xAAAA | cnt=99, 잠금 | 새 값으로 첫 주기 시작 |

같은 소스를 Cortex-M4 로 컴파일하면 (`clang --target=thumbv7em-none-eabi -O2 -DTARGET_BUILD`, 실제 출력 발췌) kick 은 store 한 번이다. 0xAAAA 는 `movw` 하나로 들어간다:

```text
00000460 <iwdg_kick>:
     460:      	movw	r0, #0x3000
     464:      	movt	r0, #0x4000
     468:      	movw	r1, #0xaaaa
     46c:      	str	r1, [r0]
     46e:      	bx	lr
```

`iwdg_init` 본문도 `str r0,[r4]`(KR) → `str r2,[r4,#0x4]`(PR) → `str r1,[r4,#0x8]`(RLR) → `ldr r0,[r4,#0xc]; lsls r0,r0,#0x1e`(SR 의 아래 두 비트 검사) 루프 → `movw r0,#0xaaaa; str` 순서 그대로 나온다.

고장 주입 결과 요약 (모두 `solution.c` 의 CHECK 로 단언):

| 주입 | 기대 (모델) |
|---|---|
| IWDG PR=0 RLR=99, kick 안 함 | 12 500 ± 32 µs 에 리셋, CSR = IWDGRSTF\|PINRSTF |
| 0x5555 없이 PR=3, RLR=10 | 무시 2 회, 기본값 → 512 ms 에 리셋 |
| PVU busy 중 PR 재기록 / RVU busy 중 kick | 무시 / cnt=0xFFF (옛 값). busy 끝난 뒤 kick → cnt=99 |
| LSI 꺼진 채 "설정 → 대기 → 시작" | SR 이 1 ms 동안 안 풀림 → −1 |
| 시작 후 KR=0x0000/0x5555/0x1234, LSION=0 | LSIRDY 그대로 1, 그래도 리셋 |
| WWDG refresh 1 ms 만에 (카운터 ≈ 0x75 > W=0x5F) | 즉시 WWDGRSTF |
| 7 ms 방치(카운터 0x38) 후 `CR \|= WDGA` | 즉시 리셋 (T6=0). `wwdg_init` 은 `WDGA\|T` 한 번 → OK |
| 2 s halt + FZ 두 비트 | 리셋 없음. IWDG FZ 를 빼고 halt → 12.5 ms 안에 IWDG 리셋 |

### Part B — 리셋 원인

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">RCC_CSR [31:24] — 원인별로 서는 비트 (이 모델; PINRSTF 동반은 RM 확인 권장)</text>
  <g stroke="currentColor">
    <rect x="150" y="30" width="70" height="26" class="f-mut"/><rect x="220" y="30" width="70" height="26" class="f-mut"/>
    <rect x="290" y="30" width="70" height="26" class="f-mut"/><rect x="360" y="30" width="70" height="26" class="f-mut"/>
    <rect x="430" y="30" width="70" height="26" class="f-mut"/><rect x="500" y="30" width="70" height="26" class="f-mut"/>
    <rect x="570" y="30" width="70" height="26" class="f-mut"/><rect x="640" y="30" width="70" height="26" class="f-acc"/>
  </g>
  <text x="160" y="48" fill="currentColor">31 LPWR</text><text x="226" y="48" fill="currentColor">30 WWDG</text>
  <text x="298" y="48" fill="currentColor">29 IWDG</text><text x="370" y="48" fill="currentColor">28 SFT</text>
  <text x="440" y="48" fill="currentColor">27 POR</text><text x="510" y="48" fill="currentColor">26 PIN</text>
  <text x="580" y="48" fill="currentColor">25 BOR</text><text x="648" y="48" fill="currentColor">24 RMVF</text>
  <text x="10" y="80" fill="currentColor">전원 투입 0x0E000000</text>
  <rect x="430" y="68" width="70" height="16" class="f-ok"/><rect x="500" y="68" width="70" height="16" class="f-ok"/><rect x="570" y="68" width="70" height="16" class="f-ok"/>
  <text x="10" y="104" fill="currentColor">IWDG 리셋 0x24000000</text>
  <rect x="290" y="92" width="70" height="16" class="f-warn"/><rect x="500" y="92" width="70" height="16" class="f-ok"/>
  <text x="10" y="128" fill="currentColor">WWDG 리셋 0x44000000</text>
  <rect x="220" y="116" width="70" height="16" class="f-warn"/><rect x="500" y="116" width="70" height="16" class="f-ok"/>
  <text x="10" y="152" fill="currentColor">안 지우고 둘 다 0x64000000</text>
  <rect x="220" y="140" width="70" height="16" class="f-warn"/><rect x="290" y="140" width="70" height="16" class="f-warn"/><rect x="500" y="140" width="70" height="16" class="f-ok"/>
  <text x="10" y="186" fill="currentColor">해석 규칙: 주 플래그 {LPWR, WWDG, IWDG, SFT, POR} 가 2개 이상 → MULTIPLE(지난번에 안 지움), 1개 → 그것,</text>
  <text x="10" y="204" fill="currentColor">없으면 BOR → PIN → NONE. POR 은 BOR 도 세우므로 POR 을 먼저 본다. 읽은 뒤 RMVF 로 지운다.</text>
</svg>
```

핵심은 **플래그가 끈적(sticky)하다**는 것. 시스템 리셋으로는 안 지워지고(RM0090: "reset flags are reset only by a power reset") RMVF 를 써야 지워진다. 부팅 코드가 매번 읽고 지우지 않으면 "IWDG 로 한 번, 다음 날 WWDG 로 한 번" 이 같은 부팅에서 0x64000000 으로 보인다 — 테스트가 이 경우를 `RC_MULTIPLE` 로 잡는다. 또 내부 리셋은 NRST 핀으로도 출력되므로(RM0090: "The system reset signal provided to the device is output on the NRST pin") PINRSTF 가 함께 서는 것이 흔한 관찰이다 — 이 모델은 그렇게 정의했고, 그래서 PIN 은 **맨 마지막**에 본다. CSR 의 LSION 은 RTC 용으로 켜 두었을 수 있으니 `csr | RMVF` 로 RMW 한다 (`REG_WRITE(RCC_CSR, RMVF)` 만 쓰면 LSI 가 꺼진다 — 마지막 CHECK).

### Part C — 10 ms 제어 루프의 창 고르기

루프는 10 ms ± 1 ms 로 kick 한다 → min = 9000 µs, max = 11 000 µs, PCLK1 = 42 MHz, EWI 사용.

1. WDGTB=0: tick 97.52 µs, quiet ≥ 11 000 이려면 T−0x41 ≥ ⌈11000·42/4096⌉ = 113 > 62 → 불가.
2. WDGTB=1: tick 195.05 µs, T−0x41 ≥ ⌈11000·42/8192⌉ = ⌈56.40⌉ = 57 → **T = 0x7A**.
3. T−W ≤ ⌊9000·42/8192⌋ = ⌊46.14⌋ = 46 → **W = 0x7A − 46 = 0x4C**.
4. 창: earliest 8973 µs, quiet 11 117 µs, latest 11 312 µs, 늦어도 리셋 11 508 µs.

EWI 없이(`ewi=false`) 고르면 T=0x79, W=0x4B, latest 11 117 µs 인데, 그 설정에 EWI 를 켜면 kick 간격이 11 ms 근처일 때 카운터가 0x40 을 찍고 EWI 가 **헛울린다**. 하네스 C1b 가 실제로 3 초 동안 3 번 울리는 것(리셋은 0)을 보여 준다 — 이 레슨을 쓰다가 처음 버전의 정답 코드가 정확히 이 버그를 가지고 있었다. EWI 핸들러가 "출력 끔" 을 하므로 헛울림 = 모터가 이유 없이 멈춘다.

IWDG 는 백스톱: `iwdg_pick(30, 100, 17000, 47000)` → PR=0, RLR=352 (ticks 1412), 30.04–83.06 ms. 정상 루프보다 3 배 이상 길어 절대 안 터지고, PCLK1 이 죽어 WWDG 가 멈춰도 LSI 로 돈다.

`./solution -v` 실제 출력 (부팅 로그 = bring-up 엔지니어가 UART 로 보는 줄):

```text
  iwdg_pick(100,300) PR=0 RLR=1174  model reset: 99997 us @47k, 276465 us @17k
  ewi=false window + EWI on: 3 false EWIs in 3 s (300 kicks), resets=0
  [boot 1] t=0 us cause=POR csr=0x0E000000
  normal: 299 kicks in 3 s, gap 9012..10994 us, resets=0, EWI=0
  hang  : WWDG reset 11421 us after last kick (bound 11508 us)
  [boot 2] t=4517011 us cause=WWDG csr=0x44000000 | EWI last words: counter=0x40 stage=2
  runaway: early-refresh reset 100 us after last good kick
  [boot 3] t=5523311 us cause=WWDG csr=0x44000000
  clock fail: IWDG reset 44115 us after last kick (bound 83059 us)
  [boot 4] t=6070157 us cause=IWDG csr=0x24000000
97/97 passed
```

읽는 법: 정상 3 초 동안 kick 간격 9.0–11.0 ms, EWI 0 회. 제어 계산(stage 2)에서 멈추면 마지막 kick 후 11.4 ms 에 WWDG 리셋, 다음 부팅이 EWI 가 남긴 유언 "카운터 0x40, stage 2" 를 보고한다 — 어디서 멈췄는지가 리셋을 넘어 살아남았다. 폭주(kick 을 100 µs 마다)는 첫 이른 kick 에서 바로 리셋 — IWDG 만 있었다면 영원히 못 잡는 고장이다. PCLK1 이 멈추면 WWDG 도 같이 멈추지만 IWDG 가 44.1 ms(32 kHz 모델) 에 잡는다. `.noinit` 매직이 깨진 전원 투입 RAM(0xA5 로 채움)은 초기화된다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 97/97 passed (약 0.06 s)
./solution -v                                                                     # Part C 부팅 로그
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_san && ./sol_san   # 97/97, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 타임아웃을 LSI 32 kHz 하나로 계산 | 실제 17–47 kHz → 같은 설정이 0.68–1.88 배 | `iwdg_pick(min, max, lsi_min, lsi_max)` 로 양 끝 검사, 비 ≥ 2.76 확인 |
| PR/RLR 을 0x5555 없이 씀 | 쓰기 보호로 조용히 무시 → 기본 512 ms | KR=0x5555 직후 PR, RLR |
| PR/RLR 쓰고 바로 0xAAAA | RVU busy 중이라 옛 RLR 로 적재 (첫 주기 엉뚱) | SR==0 을 **유한** 대기 후 0xAAAA |
| 시작 전에 설정하고 무한 대기 (`while (IWDG->SR);`) | LSI 가 꺼져 있으면 PVU 가 안 풀림 → 부팅 멈춤 | 0xCCCC 먼저(LSI 강제 ON) 또는 LSION 켜고, 대기에 상한 |
| "테스트 끝났으니 IWDG 끄자" | 끄는 방법이 없다 (리셋만) | 시작 전에 결정. 펌웨어 업데이트/긴 Flash 지우기 중에도 kick 하도록 설계 |
| `WWDG->CR \|= WDGA;` | free-running 카운터가 0x3F 이하면 T6=0 → 즉시 리셋 | `WWDG->CR = WDGA \| T;` (T6=1) |
| refresh 를 `if (cnt <= W) kick();` 로 "안전하게" | 너무 이른 kick 이라는 **증상**을 숨긴다 → 폭주 검출 무력화 | 무조건 정해진 지점에서 kick, 창은 설계로 맞춘다 |
| EWI 를 켜면서 창은 latest 로 계산 | 정상 kick 이 0x40 을 찍어 EWI 헛울림 | `quiet_us = (T−0x41)·tick` 로 고르기 (`ewi=true`) |
| EWI ISR 에서 EWIF 를 안 지움 / 1 을 써서 지우려 함 | rc_w0: 0 을 써야 지워진다. 안 지우면 다시 진입 | `WWDG->SR = 0;` |
| EWI ISR 에서 kick | 멈춘 메인을 ISR 이 살려 줌 → 워치독 무의미 | 기록 + 안전 상태만, kick 금지 |
| 리셋 원인을 안 지움 | 다음 부팅에서 이전 원인과 섞임 | 부팅 때 읽고 `RCC->CSR \|= RCC_CSR_RMVF;` |
| PINRSTF 를 먼저 검사 | 내부 리셋도 NRST 를 끌어 PINRSTF 동반 → 전부 "핀 리셋" 으로 오판 | 구체적 원인 먼저, PIN 은 마지막 |
| 디버깅 중 브레이크포인트마다 리셋 | halt 중에도 카운터가 돈다 | 디버그 빌드 부팅 때 `DBGMCU->APB1FZ \|= DBG_IWDG_STOP \| DBG_WWDG_STOP` |
| 타이머 ISR 에서 kick | 메인 루프/태스크가 죽어도 ISR 은 돈다 | 메인 루프 끝 또는 태스크 체크인 후 kick (TEST-B09) |

## 8. 면접 Q&A

<details>
<summary>Q1. IWDG 와 WWDG 는 뭐가 다르고, 둘 다 쓰는 이유는?</summary>

IWDG 는 독립 RC(LSI)로 도는 단순 타임아웃 — 메인 클럭(HSE/PLL)이 죽어도, 코어가 어떤 상태여도 돈다. 대신 RC 라 부정확하다. WWDG 는 PCLK1 기반이라 정확하고, **창(window)** 이 있어 "너무 늦게" 뿐 아니라 "너무 일찍" kick 해도 리셋한다 → 루프가 일을 건너뛰고 kick 만 반복하는 폭주, 엉뚱한 곳으로 점프해 kick 코드를 연달아 실행하는 경우를 잡는다. 그리고 EWI 로 리셋 직전 1 tick 동안 기록할 기회를 준다. 하지만 PCLK1 이 멈추면 WWDG 도 멈춘다. 그래서 안전 설계에선 WWDG(정밀, 빠른 검출) + IWDG(독립 클럭 백스톱) 조합이 흔하다 — Part C 의 "clock fail" 시나리오가 정확히 그 이유.

</details>

<details>
<summary>Q2. IWDG 타임아웃을 계산해 보세요. LSI 가 17–47 kHz 면 설계가 어떻게 바뀌나요?</summary>

T = 4·2^PR·(RLR+1)/f_LSI. PR=0, RLR=0xFFF 면 32 kHz 에서 512 ms 지만 47 kHz 348.6 ms, 17 kHz 963.8 ms. 요구사항은 "정상 kick 간격의 최악값(min)" 과 "멈춘 뒤 리셋 데드라인(max)" 두 개로 쓰고, min 은 가장 빠른 LSI, max 는 가장 느린 LSI 로 검사한다. 그러면 max/min ≥ 47/17 ≈ 2.76 이어야만 해가 존재한다. 더 빡빡하면 ① LSI 를 TIM 입력 캡처로 재서(F4: TIM5 CH4 내부 연결, RM 확인 권장) 범위를 ±10 % 정도로 좁히거나(연습 1: 비 1.22 로 완화), ② 정밀 검출은 WWDG 에 맡긴다. 꼬리: "RM 표의 32 kHz 는?" → 대표값일 뿐, RM 각주가 데이터시트를 보라고 한다.

</details>

<details>
<summary>Q3. 윈도 워치독을 너무 일찍 킥하면 왜 리셋인가요? 창은 어떻게 정하죠?</summary>

refresh 시 카운터 > W 면 리셋 — "정상 주기보다 빨리 kick 이 왔다 = 루프가 일을 안 하고 있다" 는 신호로 본다. 창은 tick = 4096·2^WDGTB/PCLK1 로, refresh 후 earliest = (T−W)·tick, latest = (T−0x40)·tick 인데, refresh 가 ÷4096 프리스케일러를 리셋한다는 보장이 없으니 경계마다 1 tick 의 불확실성이 있다. 보장값은 earliest 올림, latest 내림으로 잡고(위상 4096 가지 데모가 3121/6144 를 확인), 정상 kick 간격 [min, max] 가 그 안에 들어오게 고른다. 10 ms ± 1 ms 루프, 42 MHz → WDGTB=1, T=0x7A, W=0x4C (EWI 사용 시). 꼬리: "W 를 0x7F 로 두면?" → 창이 없는 일반 워치독이 된다.

</details>

<details>
<summary>Q4. 디버거로 브레이크포인트에 걸렸더니 보드가 리셋됩니다. 왜, 어떻게 고치죠?</summary>

코어가 halt 돼도 워치독 카운터는 계속 돈다 — kick 하는 코드가 안 도니 타임아웃. DBGMCU_APB1_FZ 의 DBG_IWDG_STOP(bit12)/DBG_WWDG_STOP(bit11)을 켜면 halt 중에 카운터가 멈춘다 (B14 테스트: 2 초 halt 에도 리셋 없음). 보통 디버그 빌드 부팅 코드나 디버거 스크립트가 켠다. 주의: 이 설정은 디버그 접근이 있을 때만 의미가 있고, 양산 빌드에서 굳이 켤 이유가 없다. 또 IWDG 는 "못 끈다" 가 원칙이라 freeze 는 끄는 게 아니라 halt 동안만 세우는 것이다. 꼬리: "Stop 모드에서는?" → F4 IWDG 는 Stop/Standby 에서도 돈다 (신형 L4 등은 옵션 바이트 IWDG_STOP/IWDG_STDBY 로 세울 수 있음 — 확인 권장) → RTC 웨이크업으로 깨어 kick (연습 5).

</details>

<details>
<summary>Q5. 리셋 후 원인은 어떻게 알아내나요? 무엇을 조심하죠?</summary>

부팅 초기(가능하면 main 첫 줄, `.data/.bss` 초기화와 무관한 레지스터 읽기라 일찍 해도 됨)에 RCC_CSR 를 읽어 저장하고 RMVF 로 지운다. 조심: ① 플래그는 시스템 리셋에서 안 지워진다 → 안 지우면 다음 리셋과 섞인다 (테스트: 0x64000000 → MULTIPLE). ② 내부 리셋도 NRST 로 출력돼 PINRSTF 가 같이 서는 것이 흔하다 → PIN 은 마지막에. ③ POR 은 BORRSTF 도 세운다 → POR 을 BOR 보다 먼저. ④ 지울 때 RMW 로 LSION 보존. 그리고 원인 + 카운터를 `.noinit`/백업 레지스터에 누적해 "IWDG 리셋이 하루 몇 번" 같은 통계를 텔레메트리로 보낸다 (BM-A09 크래시 덤프, BM-M13 fault 기록).

</details>

<details>
<summary>Q6. IWDG 초기화 순서를 말해 보세요. 왜 SR 을 기다리나요?</summary>

HAL(STM32CubeF4, 버전 확인 권장)과 이 레슨의 순서: KR=0xCCCC(시작) → KR=0x5555(쓰기 허용) → PR → RLR → SR 의 PVU/RVU 가 0 이 될 때까지 **타임아웃 있는** 대기 → KR=0xAAAA. 시작이 먼저인 이유는 0xCCCC 가 LSI 를 강제로 켜기 때문이다 — PR/RLR 은 LSI 도메인으로 넘어가야 반영되므로 LSI 가 꺼져 있으면 PVU 가 영원히 1 (B5 테스트). SR 을 기다리는 이유: busy 중의 PR/RLR 재기록은 반영이 안 되고(RM: "can be updated only when PVU bit is reset"), busy 중의 reload 는 옛 RLR 을 쓴다 (B4). 시작 직후 설정 중에는 기본값(512 ms @32 kHz)으로 돌고 있으니 설정은 빨리 끝내야 한다.

</details>

<details>
<summary>Q7. EWI 에서는 무엇을 하나요? 거기서 kick 해도 되나요?</summary>

EWI 는 카운터 0x40 에서 들어오고, 리셋은 다음 tick(0x3F) — 남은 시간은 정확히 1 tick 에서 진입 지연을 뺀 것: 168/42 MHz 에서 WDGTB=0 이면 약 16 372 사이클, WDGTB=3 이면 131 060 사이클(연습 4). 할 일: EWIF 지우기(rc_w0), 출력을 안전 상태로(모터 PWM 끄기, 히터 끄기), 짧은 진단 기록을 `.noinit`/백업 SRAM 에. Flash 섹터 지우기는 수십~수백 ms 급이라(데이터시트 확인 권장) 불가. kick 은 **하지 않는다** — 멈춘 메인 루프를 ISR 이 살려 두면 워치독이 의미 없어진다. "EWI 에서 한 번 살려 주고 카운트" 같은 설계를 본다면, 그건 회복 정책이지 감시가 아니라는 점을 짚는다.

</details>

<details>
<summary>Q8. 타이머 ISR 에서 주기적으로 kick 하면 안 되나요? RTOS 에서는?</summary>

안 된다. 타이머 ISR 은 메인 루프나 태스크가 데드락/무한 루프에 빠져도 계속 돌기 때문에 워치독이 아무것도 감시하지 않게 된다 (HardFault 처럼 인터럽트까지 죽는 경우만 잡는다). 올바른 구조: kick 은 "진짜 일을 끝낸 지점" 에서. RTOS 면 각 태스크가 주기마다 체크인 비트를 세우고, 감시 태스크(또는 최저 우선순위 idle 훅)가 **모든** 비트가 섰을 때만 kick 하고 비트를 지운다 — TEST-B09 에서 설계한다. 꼬리: "폭주는?" → 체크인만으로는 "너무 자주" 를 못 잡으니 WWDG 창과 조합.

</details>

<details>
<summary>Q9. 펌웨어 업데이트처럼 오래 걸리는 작업 중에는요?</summary>

IWDG 는 못 끄니(하드웨어 워치독 옵션이면 부팅부터 켜져 있음) 긴 작업을 쪼개서 사이사이 kick 한다. F4 의 128 KB 섹터 지우기는 1 초 이상 걸릴 수 있어(데이터시트 확인 권장) 그 동안 Flash 에서 코드를 못 읽으면 kick 도 못 한다 → ① 지우기 전 kick 하고 최대 지우기 시간 < IWDG min 이 되게 PR 을 크게 잡거나, ② 업데이트 모드 진입 시 리셋 → 부트로더가 더 긴 IWDG 설정으로 시작(시작 전에는 PR/RLR 을 바꿀 수 있으므로), ③ 듀얼 뱅크면 다른 뱅크에서 실행. 핵심은 "워치독 설정은 부팅 단계별로 다를 수 있다" 와 "끌 수는 없다".

</details>

## 9. 추가 연습

### 연습 1 — LSI 보정으로 불가능을 가능하게

`iwdg_pick(100, 130, 17000, 47000)` 은 불가능하다. TIM5(84 MHz, 32비트) 입력 캡처로 LSI 를 IC 프리스케일러 /8 로 쟀더니 두 캡처값 차이가 21 024 였다. f_LSI 와 ±10 % 범위, 그 범위로 다시 고른 PR/RLR 은? 캡처 사이에 카운터가 0xFFFFFFFF 를 넘어가면?

<details>
<summary>답 보기</summary>

f = 84e6·8/21024 = 31 963.47 → 31 963 Hz (반올림). ±10 % → [28 766, 35 160] Hz (아래는 내림, 위는 올림 — 보수적으로). 비 = 35160/28766 = 1.22 < 1.3 이므로 이제 가능: PR=0, RLR=878, 100.000–122.228 ms. 넘침은 `uint32_t delta = c2 − c1` 의 모듈러 뺄셈이 알아서 처리한다 (0xFFFFA000 → +21000 테스트). ±10 % 는 온도·전압 드리프트를 위한 이 연습의 가정 — 실제 여유는 데이터시트의 LSI 드리프트와 재보정 주기로 정한다. 보정은 부팅 때 한 번, 온도가 크게 변하면 다시.

</details>

### 연습 2 — RM 표를 정수로 재현

RM0090 Table 109 (PCLK1 = 30 MHz) 의 WWDG 최소/최대 타임아웃(136.53 µs / 8.74 ms … 1092.27 µs / 69.91 ms)을 정수 ns 로 계산해 맞춰 보고, 42 MHz 로 다시 계산하라.

<details>
<summary>답 보기</summary>

tick = 4096·2^WDGTB·10⁹ / PCLK1 [ns], 최대 = 64·tick (T=0x7F → T[5:0]+1 = 64). 실제 출력:

```text
ex2: WDGTB=0  30 MHz: tick=136533 ns max=8738133 ns | 42 MHz: tick=97523 ns max=6241523 ns
ex2: WDGTB=1  30 MHz: tick=273066 ns max=17476266 ns | 42 MHz: tick=195047 ns max=12483047 ns
ex2: WDGTB=2  30 MHz: tick=546133 ns max=34952533 ns | 42 MHz: tick=390095 ns max=24966095 ns
ex2: WDGTB=3  30 MHz: tick=1092266 ns max=69905066 ns | 42 MHz: tick=780190 ns max=49932190 ns
```

30 MHz 값을 0.01 단위로 반올림하면 RM 표와 같다. 주의: RM 표의 "최대" 는 방치했을 때 리셋까지(= 이 레슨의 reset 쪽) 이고, 합법 refresh 의 보장 latest 는 그보다 1 tick 짧다 (42 MHz WDGTB=0: 6144 µs vs 6241.5 µs).

</details>

### 연습 3 — 리셋 원인 해석 전수 검사

본문의 `reset_cause_decode` 를 다른 방식(주 플래그 비트마스크 + "비트 2개 이상" 검사 `x & (x−1)`)으로 다시 짜고, 비트 31..25 의 128 조합 전부에서 같은지 확인하라. 0x2E000000 은?

<details>
<summary>답 보기</summary>

`primary = csr & (LPW|WWD|IWD|SFT|POR); if (primary & (primary−1)) return MULTIPLE;` 후 switch. 128/128 일치. 0x2E000000 = IWDG + POR + PIN + BOR → 주 플래그 2 개 → MULTIPLE: 전원 투입 후 부팅 코드가 플래그를 안 지운 채 IWDG 리셋이 난 것. 정말 IWDG 인지 알 수 없다는 게 핵심 — 그래서 매 부팅 지운다.

</details>

### 연습 4 — EWI 시간 예산

HCLK 168 MHz, PCLK1 42 MHz, 예외 진입 12 사이클. EWI 에서 리셋까지 CPU 가 쓸 수 있는 사이클을 WDGTB=0, 3 에 대해 구하라. 16 MHz 단일 클럭(HCLK=PCLK1)에서는?

<details>
<summary>답 보기</summary>

1 tick = 4096·2^WDGTB PCLK1 사이클 → ×HCLK/PCLK1 = ×4. WDGTB=0: 16 384 − 12 = **16 372**, WDGTB=3: 131 072 − 12 = **131 060**, 16 MHz: 4096 − 12 = **4084**. 4 K 사이클이면 출력 끄기 + 수십 바이트 RAM 기록은 넉넉하지만 UART 로 로그를 내보내기엔 부족(115200 baud 1 바이트 ≈ 87 µs ≈ 1389 사이클 @16 MHz → 3 바이트 정도). 12 사이클은 Cortex-M3/M4 의 0-wait-state 기준 — Flash wait state 가 있으면 더 길다.

</details>

### 연습 5 — Stop 모드에서 IWDG 먹이기

F4 IWDG 는 Stop 에서도 돈다. `iwdg_pick(1000, 4000, 17000, 47000)` 설정에서, 깨어나 kick 하기까지 5 ms, 여유 50 ms 일 때 RTC 웨이크업 주기 상한은? RTC 가 LSE 일 때와 IWDG 와 같은 LSI 일 때를 비교하라.

<details>
<summary>답 보기</summary>

설정: PR=2, RLR=2937, ticks = 47 008, t_short = 1 000 170 µs (47 kHz). (a) LSE(정확): 주기 ≤ 1 000 170 − 5000 − 50 000 = **945 170 µs**. (b) LSI: RTC 와 IWDG 가 같이 빨라지고 느려지므로 주기를 LSI 사이클 N 으로 세면 비율이 고정된다. 남는 오차는 절대 시간인 깨는 지연뿐 → N ≤ 47 008 − ⌈5 ms·47 kHz⌉ = 47 008 − 235 = **46 773 LSI 사이클**. LSI 하나로 둘 다 돌리면 LSI 오차가 상쇄되어 오히려 설계가 쉬워진다 — 단, LSI 가 멈추는 고장은 둘을 동시에 죽인다(IWDG 는 LSI 강제 ON 이라 실제로는 드묾). Stop 진입·복귀는 BM-M11, RTC 웨이크업은 BM-M12.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 25/25 passed
```

## 10. 핵심 정리

- IWDG: `T = 4·2^PR·(RLR+1)/f_LSI`, PR 0..6, RLR 12비트. LSI 17–47 kHz → 같은 설정이 0.68–1.88 배. min 은 빠른 LSI, max 는 느린 LSI 로 검사 → **max/min ≥ 2.76** 이 아니면 불가능 (보정하면 완화).
- IWDG 초기화: 0xCCCC(시작 + LSI 강제 ON) → 0x5555 → PR, RLR → PVU/RVU 유한 대기 → 0xAAAA. 다른 KR 값은 다시 잠금. **한 번 켜면 못 끈다.**
- WWDG: tick = 4096·2^WDGTB/PCLK1, 7비트 카운터 T. 리셋 = 0x40→0x3F, 카운터 > W 에서 refresh, T6=0 쓰기. EWI = 0x40, 리셋까지 1 tick. `CR = WDGA | T` 한 번에.
- 위상을 모르면 창은 1 tick 씩 줄어든다: earliest = ⌈(T−W)·tick⌉, latest = ⌊(T−0x40)·tick⌋, EWI 를 쓰면 quiet = ⌊(T−0x41)·tick⌋. 10 ms ± 1 ms @42 MHz → WDGTB=1, T=0x7A, W=0x4C.
- EWI ISR: EWIF 0 쓰기(rc_w0), 출력 안전 상태, `.noinit` 에 유언. kick 금지.
- 리셋 원인: 부팅 때 RCC_CSR 읽고 RMVF 로 지운다(sticky). 구체 원인 먼저, POR 다음 BOR, PIN 은 마지막.
- 디버깅: DBGMCU_APB1_FZ 로 halt 중 정지. 타이머 ISR 에서 kick 금지 — 태스크 체크인(TEST-B09). WWDG(정밀·폭주 검출) + IWDG(클럭 고장 백스톱).

## 참고자료

- ST RM0090 Reference manual (STM32F405/415, 407/417, 427/437, 429/439) — 21장 IWDG (KR 키, PVU/RVU "up to 5 RC 40 kHz cycles", 32 kHz 타임아웃 표, 하드웨어 워치독 옵션), 22장 WWDG (타임아웃 식, Table 109 @30 MHz, free-running 카운터, T6 소프트웨어 리셋, EWIF "also set if the interrupt is not enabled"), 7장 RCC (Watchdog clock: LSI forced ON, RCC_CSR 0x74, reset value 0x0E000000, BORRSTF on POR/PDR) — 확인한 웹 사본: <https://docs.dirba.io/rm0090-stm32f405-415-407-417-427-437-429-439/22-window-watchdog-wwdg/>, <https://docs.dirba.io/rm0090-stm32f405-415-407-417-427-437-429-439/21-independent-watchdog-iwdg/>, <https://docs.dirba.io/rm0090-stm32f405-415-407-417-427-437-429-439/07-reset-and-clock-control-for-stm32f405xx-07xx-and-stm32f415xx/>, 원본 <https://www.st.com/resource/en/reference_manual/rm0090.pdf>
- STM32F405xx/407xx 데이터시트 — LSI oscillator characteristics (f_LSI 17/32/47 kHz min/typ/max, 이 맥에서 원문 미확인 — 확인 권장)
- RM0090 DBG 장 — DBGMCU_APB1_FZ (DBG_WWDG_STOP / DBG_IWDG_STOP, 비트 위치 확인 권장)
- STM32CubeF4 HAL `stm32f4xx_hal_iwdg.c` / `stm32f4xx_hal_wwdg.c` — `HAL_IWDG_Init` (start → write access → PR/RLR → SR 대기 → reload), `HAL_WWDG_Init`, `__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)`, `__HAL_RCC_CLEAR_RESET_FLAGS()` — <https://github.com/STMicroelectronics/stm32f4xx-hal-driver>
- Jack Ganssle, "Great Watchdog Timers For Embedded Systems" — 워치독 설계 원칙(타이머 ISR 에서 kick 금지, 창 워치독) — <http://www.ganssle.com/watchdogs.htm>
- ARMv7-M ARM B1.5 (예외 진입), Joseph Yiu *The Definitive Guide to ARM Cortex-M3/M4* — 12 사이클 인터럽트 지연
- 관련 레슨: BM-B02 (mock 프레임워크), BM-B03 (RCC 클럭 게이팅 — WWDG 는 APB1ENR 클럭이 필요, 이 mock 은 생략), BM-B04 (rc_w0 / W1C), BM-B13 (SysTick, 유한 대기), BM-B14 (PSC/ARR 공식), BM-M11 (Stop/Standby), 다음 단계: BM-M12 (RTC 웨이크업), BM-M13 (fault → 안전 상태 → 리셋), BM-A09 (.noinit 크래시 덤프), TEST-B09 (태스크 체크인 워치독), ARM-M12 (HardFault 해석)
