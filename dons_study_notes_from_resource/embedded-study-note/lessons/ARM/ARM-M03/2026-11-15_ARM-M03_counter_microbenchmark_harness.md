---
id: ARM-M03
time: 80분
updated: 2026-11-15
---
# ARM-M03 시스템 카운터 기반 마이크로벤치 하네스

## 학습 목표

- `isb; mrs x0, cntvct_el0` 로 **직렬화한 카운터 읽기**를 asm 으로 쓰고, ISB 가 왜 필요한지 (카운터 읽기도 순서를 바꿔 실행될 수 있다) 실제 측정으로 설명한다.
- `CNTFRQ_EL0` 가 말하는 주파수와 **실제 해상도**가 다를 수 있음을 이 M6 에서 직접 확인한다 (1 GHz 라고 읽히지만 눈금은 41/42 tick = 24 MHz).
- **지연(latency)** 은 한 사슬의 의존 명령으로, **처리량(throughput)** 은 독립 사슬 여러 개로 잰다는 원리를 `.S` 커널로 구현하고, ADD/MUL/MADD/SDIV/UDIV/LDR/FADD/FMUL/FMADD 표를 만든다.
- 하네스 수학 (넘침 없는 중앙값, 오버헤드 클램프, 정확한 tick→ns, ps/op, 사이클 환산) 과 배관 (시계 주입, 결과 싱크) 을 **결정적인 테스트**로 검증한다.
- 측정이 거짓말하는 4가지 (해상도, ISB 누락, 죽은 코드 제거, 값/주소 예측) 를 실제로 재현하고, Cortex-M 의 `DWT->CYCCNT`/SysTick 측정으로 옮겨 말한다.

## 1. 문제

면접에서 "이 명령 지연 몇 사이클이야?" 다음 질문은 거의 항상 **"그거 어떻게 쟀어? 그 숫자 믿을 수 있어?"** 다. 실리콘 검증·bring-up 에서도 똑같다: ISR 이 예산(budget) 안에 들어오는지, 새 보드의 클럭이 설정대로 도는지, 컴파일러를 바꾸고 나서 느려졌는지 — 전부 **카운터 두 번 읽고 빼기**로 판단하는데, 그 사이에서 숫자가 틀어지는 길이 많다.

ARM 에는 모든 코어가 공유하는 **Generic Timer 시스템 카운터**가 있고 EL0(유저)에서 `CNTVCT_EL0` 로 읽힌다 (macOS·Linux 둘 다 허용). 코어 사이클 카운터 `PMCCNTR_EL0` 는 EL0 에서 막혀 있다 — 이 Mac 에서 읽으면 실제로 **SIGILL** (exit 132) 로 죽는다. 그래서 "사이클" 은 직접 못 재고, 시간을 재서 **환산**해야 한다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">한 샘플 = isb; mrs t0 → 커널(명령 32만 개) → isb; mrs t1</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="40" height="30" class="f-acc"/>
    <rect x="60" y="40" width="60" height="30" class="f-ok"/>
    <rect x="120" y="40" width="460" height="30" class="f-mut"/>
    <rect x="580" y="40" width="40" height="30" class="f-acc"/>
    <rect x="620" y="40" width="60" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="40" y="60">isb</text><text x="90" y="60">mrs t0</text>
    <text x="350" y="60" font-family="monospace">.rept 16 / add x1,x1,#1 / .endr / subs / b.ne  × 20000</text>
    <text x="600" y="60">isb</text><text x="650" y="60">mrs t1</text>
  </g>
  <line x1="20" y1="110" x2="720" y2="110" stroke="currentColor"/>
  <g stroke="currentColor">
    <line x1="40" y1="102" x2="40" y2="118"/><line x1="121" y1="102" x2="121" y2="118"/>
    <line x1="204" y1="102" x2="204" y2="118"/><line x1="287" y1="102" x2="287" y2="118"/>
    <line x1="370" y1="102" x2="370" y2="118"/><line x1="452" y1="102" x2="452" y2="118"/>
    <line x1="535" y1="102" x2="535" y2="118"/><line x1="618" y1="102" x2="618" y2="118"/>
    <line x1="701" y1="102" x2="701" y2="118"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">
    <text x="80" y="134">+41</text><text x="162" y="134">+42</text><text x="245" y="134">+42</text>
    <text x="328" y="134">+41</text><text x="411" y="134">+42</text><text x="493" y="134">+42</text>
    <text x="576" y="134">+41</text><text x="660" y="134">+42</text>
  </g>
  <text x="20" y="160" fill="currentColor">CNTVCT_EL0 눈금: CNTFRQ_EL0 = 1 000 000 000 (1 tick = 1 ns 라고 "주장") 이지만 값은 41 또는 42 씩만 뛴다</text>
  <text x="20" y="180" fill="currentColor">→ 평균 125/3 = 41.67 tick = 진짜 해상도 41.67 ns (24 MHz). Lima 리눅스 게스트는 24 MHz, +1 씩으로 읽는다.</text>
  <rect x="20" y="198" width="700" height="88" class="f-mut" stroke="currentColor"/>
  <text x="30" y="220" fill="currentColor">양자화 오차: t1−t0 는 참값 ± 1 눈금 (41.67 ns). 구간이 짧으면 오차가 전부다.</text>
  <text x="30" y="242" fill="currentColor" font-family="monospace">32만 add ≈ 80 us → 41.67 ns / 80 us ≈ 0.05 %   (16개 add ≈ 4 ns → 측정 불가)</text>
  <text x="30" y="264" fill="currentColor">ISB 없음: mrs 가 사슬보다 먼저 실행될 수 있다 (실측: 256 sdiv 구간이 약 2눈금 짧음)</text>
  <text x="30" y="280" fill="currentColor" font-size="12">사이클 = (ps/op) ÷ (바로 옆에서 잰 add 사슬의 ps/op) — add 지연 1 사이클 가정 (Part C)</text>
</svg>
```

| 층 | 파일 | 하는 일 |
|---|---|---|
| 카운터 | `.S` `cnt_read`, `cnt_freq` | `isb; mrs cntvct_el0` / `mrs cntfrq_el0` |
| 커널 | `.S` `lat_*`, `tput_*`, `k_empty` | 반복당 측정 명령 **정확히 16개**, 반환값 = 사슬의 최종 값 |
| 배관 | `.c` `bench_measure` | 시계(함수 포인터) 두 번 읽기 사이에 커널 1번, 결과는 `g_sink` 에 XOR |
| 수학 | `.c` `u64_median`, `ticks_to_ns`, `per_op_ps`, `mcycles`, `bench_summarize`, `chains_needed` | 넘침 없는 정수 연산, 클램프, 반올림 |

### Part A — 카운터 읽기와 측정 커널

`starter.S` 의 TODO: `cnt_read` (ISB 후 `CNTVCT_EL0`), `cnt_freq` (`CNTFRQ_EL0`), `lat_add` (add 16개 의존 사슬), `tput_add` (16개 독립 사슬에 add 1개씩), `lat_ldr` (`ldr x1, [x1]` 포인터 추적). 나머지 커널 (MUL/MADD/SDIV/UDIV/FP/tput_ldr) 은 같은 틀로 이미 써 있으니 읽고 따라 한다. 테스트는 시간을 판정하지 않는다: 커널이 **정말로 16×iters 개를 실행했는지**를 반환값으로 C 참조 모델과 비교한다 (3칸 포인터 고리, FP 는 비트 단위 일치).

### Part B — 하네스 수학·배관, 그리고 측정 함정

`starter.c` 의 TODO 8개. 경계값이 핵심이다: `(a+b)/2` 넘침, `ticks*1e9` 넘침, 오버헤드가 샘플보다 클 때 unsigned 뺄셈, 카운터 wrap. **심어 둔 버그 3개** (`median2_naive`, `ticks_to_ns_naive`, `sub_naive`) 가 실제로 틀린 값을 낸다는 것도 테스트가 확인한다. 배관은 **가짜 시계**로 검사한다 (읽기 순서·횟수, 커널 인자, 싱크). 그리고 `demo_*.c` 로 진짜 함정 4개를 재현한다: 해상도, ISB 누락, 죽은 코드 제거, 포인터 추적이 0.33 사이클로 나오는 값 예측.

### Part C — 실측 표와 해석

`demo_bench.c` 로 M6 의 Super/Performance/Efficiency 코어별 지연·처리량 표를 만들고, `chains_needed(L, T) = ⌈L/T⌉` (Little 의 법칙) 로 "사슬 16개면 충분한가" 를 판단한다. 마지막으로 같은 측정을 Cortex-M (`DWT->CYCCNT`, SysTick) 에서는 어떻게 하는지 옮긴다.

## 2. Problem (English)

> **Build a counter-based microbenchmark harness on AArch64.**
>
> 1. In assembly: `uint64_t cnt_read(void)` — a counter read that cannot be reordered with earlier instructions; `uint64_t cnt_freq(void)`. Kernels with the signature `uint64_t k(uint64_t iters, uint64_t x, uint64_t y)` that execute exactly 16 measured instructions per iteration and return the final chain value: `lat_add` (one dependent chain of `add #1`), `tput_add` (16 independent chains, returns their sum), `lat_ldr` (pointer chase `p = *p`). `iters == 0` must return the initial value. Leaf functions, AAPCS64, do not touch x18 or d8–d15.
> 2. In C, with exact integer math and no `__int128`: `u64_median` (sorts in place; even n → floor of the mean of the two middles, overflow-free), `u64_sub_clamp`, `ticks_to_ns(ticks, freq)` = ⌊ticks·10⁹/freq⌋ saturating (freq < 2³⁴), `per_op_ps(ns, nops)` = ⌊ns·1000/nops⌋, `mcycles(ps_op, ps_cycle)` = round(1000·ps_op/ps_cycle).
> 3. `bench_measure(clk, k, iters, x, y, out, reps)` — the clock is injected so the plumbing can be tested with a fake clock; results are XOR-ed into a volatile sink. `bench_summarize` — subtract overhead (clamped), min/median/max, count outliers > 2×median without overflow.
> 4. `chains_needed(L, T)` = ⌈L/T⌉ independent chains to saturate a unit with latency L and reciprocal throughput T.
>
> Examples: `ticks_to_ns(1, 24000000) == 41`, `ticks_to_ns(1<<40, 24000000) == 45812984490666`, `ticks_to_ns(UINT64_MAX, 1) == UINT64_MAX`, `u64_median({UINT64_MAX, UINT64_MAX-2}) == UINT64_MAX-1`, `mcycles(2, 3) == 667`, `chains_needed(3000, 250) == 12`.
>
> Follow-ups: Why the ISB? What is the counter's real resolution and how many instructions per sample do you need? How do you convert to cycles without a cycle counter? Why does your pointer chase report less than one cycle? How would you do this on a Cortex-M4? How does x86 `rdtsc` differ?

## 3. 시작 코드

C 하네스 (TODO 8개 + 테스트 전체):

<!-- code: starter.c -->

asm (TODO 5개, 스텁은 `mov x0, #0` / `mov x0, x1` 로 틀린 값을 돌려주지만 메모리·스택은 안 건드린다):

<!-- code: starter.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c starter.S -o /tmp/ARM-M03_starter && /tmp/ARM-M03_starter
```

처음 실행 결과 (실제 출력 일부, exit 1):

```text
INFO  cntfrq_el0 = 0 Hz
FAIL  starter.c:148  f >= 1000000u
FAIL  starter.c:154  t1 > t0
FAIL  starter.c:159  lat_add(it, x, y) == x + n
FAIL  starter.c:160  tput_add(it, x, y) == 16u * (x + it)
FAIL  starter.c:175  lat_ldr(it, (uint64_t)&g_slot[0], 0) == (uint64_t)&g_slot[n % 3]
FAIL  starter.c:202  u64_median(big, 2) == UINT64_MAX - 1
FAIL  starter.c:216  ticks_to_ns(1ull << 40, 24000000u) == 45812984490666u
FAIL  starter.c:250  out[0] == 30 && out[1] == 29
FAIL  starter.c:253  g_fake_pos == 8 && g_kcalls == 4
FAIL  starter.c:283  chains_needed(3000, 250) == 12
...
57/101 passed
```

이미 써 있는 커널 (MUL, SDIV, FP …) 의 체크와 심어 둔 버그 체크는 처음부터 PASS 다. `iters == 0` 경우도 스텁이 `x` 를 돌려주므로 우연히 통과한다.

## 4. 힌트

<details>
<summary>힌트 1 — cnt_read 는 명령 두 개</summary>

`isb` 로 앞 명령이 전부 끝날 때까지 다음 명령을 시작하지 않게 하고 `mrs x0, cntvct_el0`. `cnt_freq` 는 `mrs x0, cntfrq_el0` 하나 — 이 레지스터는 펌웨어가 부팅 때 써 넣은 "주장" 일 뿐 하드웨어가 재는 값이 아니다.

</details>

<details>
<summary>힌트 2 — 커널 틀</summary>

`cbz x0, 2f` → `1:` 본문 (`.rept 16` … `.endr`) → `subs x0, x0, #1` / `b.ne 1b` → `2: mov x0, x1` / `ret`. `tput_add` 는 `.irp r, ICHAINS` 로 16개 레지스터 (x1, x3–x17) 를 돌며 `mov \r, x1` (초기화), `add \r, \r, #1` (본문), `add x0, x0, \r` (합). x18 은 Apple 이 예약, x19 이상은 callee-saved 라 안 쓴다.

</details>

<details>
<summary>힌트 3 — 넘침 없는 tick → ns</summary>

`ticks = q·freq + r` 로 쪼개면 `ticks·1e9/freq = q·1e9 + r·1e9/freq`. 앞 항은 정확하고 (넘침만 검사 → 포화), 뒤 항은 `r < freq < 2³⁴` 이라 `r·1e9 < 2⁶⁴`. `per_op_ps` 도 같은 방법 (`ns = q·nops + r`).

</details>

<details>
<summary>힌트 4 — 중앙값과 이상치 판정의 넘침</summary>

짝수 n 의 평균은 `lo + (hi − lo)/2` (정렬돼 있으니 `hi ≥ lo`). "x > 2·med" 는 `x > med && x − med > med` — `2*med` 도, `x − med` (x < med 일 때) 도 넘칠 수 있다.

</details>

<details>
<summary>힌트 5 — 배관과 Little 의 법칙</summary>

`out[i] = t1 − t0` 는 unsigned 뺄셈이라 카운터 wrap 에도 맞다 (카운터 폭이 64비트일 때). 커널 결과는 `g_sink ^= r` (volatile) — 싱크가 없으면 C 로 쓴 커널은 통째로 사라질 수 있다 (5절). `chains_needed` 는 정수 올림 나눗셈 `(L + T − 1) / T`.

</details>

## 5. 풀이

### Part A — 카운터 읽기: 왜 ISB 인가

Arm ARM 의 Generic Timer 장은 `CNTVCT_EL0` (그리고 `CNTPCT_EL0`) 읽기가 **같은 코어의 다른 명령에 대해 추측적으로, 순서를 바꿔** 일어날 수 있고, 순서가 필요하면 ISB 를 쓰라고 한다 (DDI 0487 D12 장 기준, 확인 권장). 비순차(out-of-order) 코어에서 `mrs` 는 입력 레지스터가 없으니 "준비된" 명령이다 → 디스패치되자마자 실행될 수 있다. 측정 구간 끝의 `mrs` 가 사슬이 끝나기 전에 실행되면 구간이 짧게 잡힌다. `isb` 는 앞 명령이 모두 끝나고 파이프라인을 비운 뒤 다음 명령을 가져오므로 그 순서를 강제한다 (ISB 의미 자체는 ARM-M05). Linux arm64 의 `__arch_counter_get_cntvct()` 도 `isb` 후 `mrs` 이고, FEAT_ECV 가 있는 CPU 에서는 ISB 없이도 순서가 지켜지는 **self-synchronized** 판 `CNTVCTSS_EL0` 를 쓰도록 되어 있다 (커널 버전마다 다름, 확인 권장). 이 M6 은 `hw.optional.arm.FEAT_ECV: 1` 이고 `mrs x9, S3_3_C14_C0_6` (= CNTVCTSS_EL0) 가 실제로 실행된다.

`solution.S` 를 링크한 실제 기계어 (`/usr/bin/objdump -d /tmp/ARM-M03_solution`):

```text
00000001000024d0 <_cnt_read>:
1000024d0: d5033fdf     isb
1000024d4: d53be040     mrs  x0, CNTVCT_EL0
1000024d8: d65f03c0     ret
0000000100002520 <_lat_add>:
100002520: b4000260     cbz  x0, 0x10000256c <_lat_add+0x4c>
100002524: 91000421     add  x1, x1, #0x1
...                     (같은 줄 16개)
100002564: f1000400     subs x0, x0, #0x1
100002568: 54fffde1     b.ne 0x100002524 <_lat_add+0x4>
10000256c: aa0103e0     mov  x0, x1
```

`.rept 16` 가 정말 add 16개로 펼쳐졌다 (0x524–0x560, 4 B × 16). 루프 꼬리 `subs/b.ne` 2개는 사슬과 **독립**이라 지연 커널에서는 거의 공짜로 겹친다 — `k_empty` (꼬리만) 가 명령 16개 기준 0.06 사이클/명령으로 나온다 (Part C 표).

### Part A — 지연 vs 처리량 커널

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">lat_mul: 사슬 1개 (지연)</text>
  <text x="380" y="22" fill="currentColor" font-weight="bold">tput_mul: 사슬 16개, 곱셈기 2개라면</text>
  <g stroke="currentColor">
    <rect x="20" y="44" width="60" height="24" class="f-acc"/><rect x="80" y="44" width="60" height="24" class="f-acc"/>
    <rect x="140" y="44" width="60" height="24" class="f-acc"/><rect x="200" y="44" width="60" height="24" class="f-acc"/>
    <rect x="260" y="44" width="60" height="24" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">
    <text x="50" y="61">mul#1</text><text x="110" y="61">mul#2</text><text x="170" y="61">mul#3</text><text x="230" y="61">mul#4</text><text x="290" y="61">mul#5</text>
  </g>
  <text x="20" y="90" fill="currentColor">다음 mul 은 앞 결과(x1)를 기다린다 → 3 사이클마다 1개</text>
  <text x="20" y="108" fill="currentColor" font-family="monospace">실측 lat_mul = 3.00 사이클 (S/P/E 모두)</text>
  <line x1="20" y1="124" x2="320" y2="124" stroke="currentColor"/>
  <g fill="currentColor" font-size="12"><text x="20" y="140">0</text><text x="76" y="140">3</text><text x="136" y="140">6</text><text x="196" y="140">9</text><text x="252" y="140">12 사이클</text></g>
  <g stroke="currentColor">
    <rect x="380" y="44" width="60" height="20" class="f-ok"/><rect x="410" y="68" width="60" height="20" class="f-ok"/>
    <rect x="440" y="44" width="60" height="20" class="f-ok"/><rect x="470" y="68" width="60" height="20" class="f-ok"/>
    <rect x="500" y="44" width="60" height="20" class="f-ok"/><rect x="530" y="68" width="60" height="20" class="f-ok"/>
    <rect x="560" y="44" width="60" height="20" class="f-ok"/><rect x="590" y="68" width="60" height="20" class="f-ok"/>
    <rect x="620" y="44" width="60" height="20" class="f-ok"/><rect x="650" y="68" width="60" height="20" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="11" text-anchor="middle" font-family="monospace">
    <text x="410" y="58">x1</text><text x="440" y="82">x3</text><text x="470" y="58">x4</text><text x="500" y="82">x5</text>
    <text x="530" y="58">x6</text><text x="560" y="82">x7</text><text x="590" y="58">x8</text><text x="620" y="82">x9</text>
    <text x="650" y="58">x10</text><text x="680" y="82">x11</text>
  </g>
  <text x="380" y="108" fill="currentColor">유닛 2개가 매 사이클 새 mul 을 받는다 (파이프라인)</text>
  <text x="380" y="126" fill="currentColor" font-family="monospace">실측 tput_mul = 0.50 사이클/개 (P, E)</text>
  <rect x="20" y="160" width="700" height="108" class="f-mut" stroke="currentColor"/>
  <text x="30" y="182" fill="currentColor">필요한 독립 사슬 수 = ⌈L / T⌉ (Little 의 법칙: 동시에 실행 중인 개수 = 지연 × 처리량)</text>
  <text x="30" y="204" fill="currentColor" font-family="monospace">MUL  : 3 / 0.5  = 6   ≤ 16 ✓      FADD : 2.5 / 0.33 ≈ 8 ≤ 16 ✓</text>
  <text x="30" y="226" fill="currentColor" font-family="monospace">SDIV : 7 / 2.0  = 4   ≤ 16 ✓      FADD 사슬 4개: L/4 ≈ 0.62 → 실측 0.61 (연습 5)</text>
  <text x="30" y="248" fill="currentColor">사슬이 모자라면 "처리량" 커널이 실제로는 L/k 를 잰다 (유닛 수가 아니라 사슬 수)</text>
</svg>
```

- 레지스터 선택: 정수 사슬 `x1, x3–x17` (16개, 전부 caller-saved; x16/x17 은 IP0/IP1 이지만 호출이 없는 leaf 라 안전), FP 사슬 `d16–d31` (d8–d15 의 하위 64비트는 callee-saved 라 피했다 — ARM-M01 영역).
- `tput_sdiv` 는 처음에 사슬 결과를 **XOR** 로 합쳤는데, 16개 사슬이 같은 값에서 시작하니 XOR 은 항상 0 → 테스트가 아무것도 검사하지 못했다. 합(`add`)으로 바꿨다 (작성 중 실제로 고친 것).
- 테스트의 포인터 고리는 일부러 **3칸** (a→b→c→a). 16·iters 번 따라가면 `16·iters mod 3` 칸 앞이므로 "명령 수가 정확히 맞는지" 까지 검사된다. 자기 자신을 가리키는 칸 (`*p == p`) 으로는 몇 번을 돌아도 같은 값이라 검사가 안 된다 — 그리고 Part B 에서 보듯 측정도 틀린다.
- FP 참조 모델은 같은 순서·같은 연산이라 **비트 단위로 같아야** 한다 (`fmadd` ↔ C 의 `fma()`, 한 번 반올림). `-ffp-contract` 가 끼어들 곱셈+덧셈 표현식은 C 모델에 없다.

### Part B — 하네스 수학: 어디서 넘치나

| 함수 | 순진한 식 | 넘치는 지점 | 고친 식 |
|---|---|---|---|
| 중앙값 (짝수 n) | `(lo + hi) / 2` | `lo + hi ≥ 2⁶⁴` | `lo + (hi − lo)/2` |
| `ticks_to_ns` | `t * 1e9 / f` | `t > 1.8·10¹⁰` — 1 GHz 카운터면 **18.4초** 분량 | `q·1e9 + r·1e9/f`, 포화 |
| `per_op_ps` | `ns * 1000 / n` | `ns > 1.8·10¹⁶` | `q·1000 + r·1000/n` |
| 오버헤드 보정 | `raw − ovh` | `raw < ovh` → 2⁶⁴ 근처 | `raw > ovh ? raw − ovh : 0` |
| 이상치 `x > 2·med` | `x − med > med` | `x < med` 면 `x − med` 가 wrap | `x > med && x − med > med` |

심어 둔 버그가 실제로 내는 값 (테스트가 "틀렸다" 를 확인한다):

- `median2_naive(UINT64_MAX, UINT64_MAX−2)` = `0x7FFFFFFFFFFFFFFE` (정답 `UINT64_MAX−1`).
- `ticks_to_ns_naive(2⁴⁰, 24 MHz)` = 464,738,642,797 ns (7.7분) — 정답은 45,812,984,490,666 ns (12.7시간).
- `sub_naive(3, 5)` = `UINT64_MAX − 1`.

**이 레슨을 쓰다가 실제로 낸 버그.** 처음 `bench_summarize` 의 이상치 판정을 `scratch[i] - st->med > st->med` 로만 썼다. 중앙값보다 **작은** 샘플에서 뺄셈이 wrap 해서 거대한 값이 되고 이상치로 세어졌다. 테스트가 잡은 실제 출력:

```text
FAIL  solution.c:285  st.min == 0 && st.med == 5 && st.max == 300 && st.outliers == 2
100/101 passed
```

`{30, 0, 5, 300, 1}` 에서 0 과 1 이 "이상치" 로 들어가 4가 나왔다. `scratch[i] > st->med &&` 를 앞에 붙여 고쳤다.

**배관 검사 (가짜 시계).** 시계를 함수 포인터로 받으면 시간 없이 배관을 검사할 수 있다. 시계가 `100, 130, 200, 229, 2⁶⁴−5, 10, 500, 500` 을 차례로 돌려주게 하고 4번 반복 → `out = {30, 29, 15, 0}` (wrap 구간도 unsigned 뺄셈이면 15), 시계 읽기 정확히 8번, 커널은 1·3·5·7번째 읽기 **사이**에서 호출, 인자 그대로, 싱크 = 반환값 4개의 XOR. 진짜 시계는 같은 함수에 `cnt_read` 를 넣을 뿐이다.

### Part B — 측정 함정 ① 해상도: CNTFRQ 가 거짓말한다

`demo_bench res` 실제 출력 (`bench_m6.txt`):

```text
cntfrq_el0 = 1000000000 Hz (1 tick = 1.000 ns 라고 '주장')
1e6 연속 읽기: 같은 값 730240, 변화 269759 (41: 89640, 42: 179399, 그 외: 720)
평균 눈금 = 44.789 tick, 읽기 3.71 번마다 값이 바뀜, isb+mrs 1회 ≈ 12.08 ns
같은 구간: cntvct 8975749 tick, mach_absolute_time 215420 tick, 비 41.6663 (125/3 = 41.6667)
cntvct % 125 표본: 83(x3%125=124) 0(x3%125=0) 41(x3%125=123) 83(x3%125=124) 0(x3%125=0) ...
```

- 값이 바뀔 때 41 : 42 가 정확히 1 : 2 → 평균 41.667 = 125/3. 카운터 값을 125 로 나눈 나머지는 {0, 41, 83} 뿐 → **CNTVCT = ⌊(24 MHz 카운트) × 125/3⌋** 처럼 보인다. 즉 하드웨어 눈금은 24 MHz 이고 1 GHz 단위로 **환산되어** 보인다.
- Armv8.6 부터 시스템 카운터 주파수가 1 GHz 로 정해졌고, FEAT_ECV 에 카운터 스케일링 기능이 있다고 알고 있다 (Arm ARM, 확인 권장). macOS 가 정확히 어떤 방법으로 이렇게 보이게 하는지는 확인하지 않았다. 같은 Mac 의 **Lima 리눅스 게스트에서는** `cntfrq = 24000000`, 값이 +1 씩 뛴다 (실측: 1e6 읽기 중 +1 이 256,644번, 읽기 3.86번마다 변화) — 단위만 다르고 해상도는 같다.
- Arm ARM 은 카운터가 낮은 주파수에서 더 큰 폭으로 증가하는 구현을 허용한다고 읽었다 (평균 속도만 맞으면 됨 — 확인 권장). **교훈: `CNTFRQ_EL0` 는 단위이지 해상도가 아니다. 해상도는 연속 읽기로 잰다.**
- 결과: 1 눈금 = 41.67 ns ≈ 160 사이클 (4 GHz). 오차 0.1% 를 원하면 구간 ≥ 41.7 µs → add 기준 약 16.7만 개 (연습 1: `iters_for_error(41667, 250, 1000) = 10417`). 그래서 샘플당 32만 명령 (iters = 20000) 을 쓴다.

### Part B — 측정 함정 ② ISB 를 빼면 구간이 짧아진다

함수 호출 경계 없이 asm 한 함수 안에서 `mrs … 256개 sdiv 사슬 … mrs` 를 재고 ISB 유무만 바꿨다 (`demo_isb.S`, `bench isb`, 각 2001회, 실제 출력):

```text
256 sdiv 사슬: 긴 측정 기준 1367 ps/sdiv → 기대 ≈ 349 ns
isb; mrs ... isb; mrs          min  334  med  459  max    792 tick (2001회)
mrs ... mrs (ISB 없음)       min  291  med  375  max    709 tick (2001회)
isb; mrs ... (dep load) mrs    min  250  med  375  max    709 tick (2001회)
mrs cntvctss ... cntvctss      min  333  med  417  max   2792 tick (2001회)
```

- "기대 349 ns" 는 긴 측정의 **min** 이라 E 코어 (sdiv 4 사이클) 값이다. P/S 코어에서 sdiv 는 7 사이클 × 256 / 3.9 GHz ≈ **459 ns** — ISB 판 중앙값 459 와 맞는다.
- ISB 를 빼면 중앙값이 375 (−84 ns = 2눈금, 약 18% 짧음). 끝의 `mrs` 가 사슬이 끝나기 전에 실행됐다는 뜻이다 (ISB 자체 비용은 오히려 ISB 판을 늘리는 쪽이라 이 차이를 설명하지 못한다).
- `dep load` 판: `mrs` 앞에 사슬 결과에 의존하는 load 를 둬도 **소용없다** — `mrs` 는 그 load 에 의존하지 않으니까. 데이터 의존성으로 카운터 읽기를 묶을 수는 없다.
- `CNTVCTSS_EL0` 판은 417 로 중간이다. 1눈금 차이라서 이 데이터로 "self-synchronized 가 ISB 와 같다/다르다" 를 결론 내리지 않는다 (정직한 한계).
- 다른 실행에서는 차이가 1–3눈금 사이에서 흔들렸다 (부하 13–45). 방향은 늘 "ISB 없음이 더 짧거나 같음" 이었다.

### Part B — 측정 함정 ③ 잰 루프가 -O2 에서 사라진다

`demo_sink.c` 의 실제 `clang -O2 -S` 출력 (`.p2align` 등 지시어 생략):

```text
_c_loop_unused:                 ; x += 3 루프, 결과 안 씀
	ret                             ← 루프 통째로 삭제
_c_loop_closed_form:            ; 같은 루프, 결과 반환
	add	x0, x0, x0, lsl #1          ← 3n 닫힌 식. 사슬이 없다
_c_loop_barrier:                ; 매 반복 asm volatile("" : "+r"(x))
LBB2_1:
	add	x8, x8, #3
	; InlineAsm Start
	; InlineAsm End
	subs	x0, x0, #1
	b.ne	LBB2_1                      ← 루프가 산다
_asm_kernel_unused:             ; lat_add(1000,0,0) 결과 버림
	b	_lat_add                        ← 호출은 남는다 (불투명한 .S 함수, 꼬리 호출)
```

- C 로 쓴 커널은 결과를 안 쓰면 사라지고, 써도 닫힌 식이 될 수 있다. 빈 asm 장벽 `"+r"(x)` 는 "x 를 바꿀 수도 있다" 고 컴파일러를 속여 매 반복을 남긴다 (Google Benchmark 의 `DoNotOptimize` 와 같은 아이디어; 제약조건 문법은 ARM-M02).
- 이 레슨의 커널은 `.S` 라서 컴파일러가 안을 볼 수 없다 → 호출이 지워지지 않는다. 그래도 `g_sink ^= r` 로 결과를 모으는 이유: LTO 나 나중에 C 커널로 바꿀 때 하네스가 안전하고, 테스트 (`g_sink == …`) 로 "결과가 실제로 흘렀다" 를 검사할 수 있다.
- 실제 `bench_measure` 기계어 (objdump): `blr x25` (시계) → `blr x24` (커널) → `blr x25` (시계) → `sub x8, x0, x26` → `str x8, [x20], #8` → `ldr/eor/str` (싱크). 순서가 소스 그대로다 — 간접 호출은 컴파일러가 재배치할 수 없다.

### Part B — 측정 함정 ④ 포인터 추적이 1 사이클도 안 걸린다?

브리프대로 "자기 자신을 가리키는 칸" (`*p == p`) 으로 `ldr x1, [x1]` 을 쟀더니 **0.33–0.50 사이클**이 나왔다. L1 load-to-use 지연이 1 사이클 미만일 수는 없다. 고리 크기를 바꿔 본 실제 결과 (`bench ldr`, Perf 열 위주):

```text
ring                             ps/op cyc(all) | Super          Perf           Eff
  1 자기 자신 (*p == p)         91     0.34 |   0.33 (n=30)     -             3.00 (n= 9)
  2 2칸: PC마다 주소·값 고정     136     0.50 |   0.33 (n= 5)    0.50 (n=29)    3.00 (n= 3)
  3 3칸: 16 mod 3 = 1             700     3.01 |    -             3.00 (n=36)    3.00 (n= 1)
 15 15칸                          782     3.00 |    -             3.00 (n=40)     -
 16 16칸: PC마다 주소·값 고정     843     2.13 |   0.43 (n= 1)    0.70 (n=18)    2.93 (n= 9)
 17 17칸                          807     3.01 |    -             3.01 (n=41)     -
 64 64줄 무작위 (4 KB)        1001     3.00 |   3.00 (n=20)    3.06 (n= 1)    3.01 (n=10)
512 512줄 무작위 (32 KB)       781     3.00 |    -             3.01 (n=31)    3.08 (n= 4)
```

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">ldr x1,[x1] 사슬의 "지연" — 고리 크기별 (S/P 코어, 사이클)</text>
  <line x1="60" y1="230" x2="720" y2="230" stroke="currentColor"/>
  <line x1="60" y1="50" x2="60" y2="230" stroke="currentColor"/>
  <line x1="60" y1="76" x2="720" y2="76" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="64" y="70" fill="currentColor" font-size="12">3 사이클 = 진짜 L1 hit 지연</text>
  <g fill="currentColor" font-size="12" text-anchor="end"><text x="54" y="234">0</text><text x="54" y="80">3</text><text x="54" y="157">1.5</text></g>
  <g stroke="currentColor">
    <rect x="80" y="213" width="56" height="17" class="f-warn"/>
    <rect x="160" y="204" width="56" height="26" class="f-warn"/>
    <rect x="240" y="76" width="56" height="154" class="f-ok"/>
    <rect x="320" y="76" width="56" height="154" class="f-ok"/>
    <rect x="400" y="194" width="56" height="36" class="f-warn"/>
    <rect x="480" y="76" width="56" height="154" class="f-ok"/>
    <rect x="560" y="76" width="56" height="154" class="f-ok"/>
    <rect x="640" y="76" width="56" height="154" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="108" y="208">0.33</text><text x="188" y="199">0.50</text><text x="268" y="98">3.00</text><text x="348" y="98">3.00</text>
    <text x="428" y="189">0.70</text><text x="508" y="98">3.01</text><text x="588" y="98">3.00</text><text x="668" y="98">3.01</text>
    <text x="108" y="248">1 (자기)</text><text x="188" y="248">2</text><text x="268" y="248">3</text><text x="348" y="248">15</text>
    <text x="428" y="248">16</text><text x="508" y="248">17</text><text x="588" y="248">64 무작위</text><text x="668" y="248">512 무작위</text>
  </g>
  <text x="20" y="272" fill="currentColor">경고색 막대: 고리 크기가 16의 약수 → ldr 마다(PC마다) 주소·값이 매 반복 같음 → 사슬이 끊김</text>
  <text x="20" y="288" fill="currentColor" font-size="12">E 코어는 모두 3.0. 값 예측인지 주소 예측인지는 이 실험으로 구분 못 함 (가설)</text>
</svg>
```

- 깨지는 경우는 고리 크기가 **16 의 약수** (1, 2, 16) 일 때뿐이다 — 반복당 ldr 16개라서, 각 ldr 명령(PC) 이 매 반복 **같은 주소에서 같은 값**을 읽는다. 3·15·17칸이나 무작위 고리는 PC마다 값이 바뀌므로 진짜 지연 3 사이클이 나온다.
- 해석 (가설, hedge): 최근 Apple 코어에는 load 값/주소 예측기가 있다는 공개 연구 (SLAP: M2 이후 load address prediction, FLOP: M3 이후 load value prediction — Kim·Genkin 외, 2025) 가 있다. 예측이 맞으면 다음 ldr 이 앞 ldr 을 기다리지 않아 "사슬" 이 아니게 된다. E 코어에서는 1칸도 3.00 이라 그 코어에는 이 효과가 안 보였다. 정확한 메커니즘은 확인하지 않았다.
- 교훈: **포인터 추적은 무작위 순열로, 칸마다 다른 캐시 라인에** 만든다 (`make_ring(n, 1)`). 하네스 테스트에 3칸 고리를 쓴 것도 이것 때문이다. 이 M6 의 L1 hit load-to-use = **3 사이클** (S/P/E 모두, 4 KB·32 KB 무작위), `[x2, x1, lsl #3]` 인덱스 주소면 **4 사이클** (연습 4).

### Part C — 실측 표 (Apple M6, 사이클 = 바로 옆 add 사슬 대비)

방법: 각 커널마다 (lat_add 샘플, 커널 샘플) **쌍**을 41번 잰다. 쌍마다 `mcycles(커널 ps/op, add ps/op)` 를 구하고, 쌍 앞뒤에서 `pthread_cpu_number_np` 로 본 코어 번호가 같으면 그 코어 종류 칸에 넣어 중앙값을 낸다 (이 M6: 0–5 E, 6–7 Super, 8–11 Perf — ARCH-M08 측정; QoS BACKGROUND 로 돌리면 0–5 만 나와서 E 매핑을 다시 확인했다). macOS 는 코어 고정이 안 되므로 이것이 최선이다. 부하는 load average 12.7–45 (병렬 writer 10명) 였다.

| 명령 | 지연 S / P / E | 처리량 역수 S / P / E | 비고 |
|---|---|---|---|
| `add x, x, #1` | 1 / 1 / 1 (기준) | 0.14 / 0.26 / 0.26 | S ≈ 7개/사이클 |
| `mul x` | 3 / 3 / 3 | 0.33–0.38 / 0.50 / 0.52 | |
| `madd` (Rn 경유 사슬) | 4 / 4 / 3 | — | |
| `madd` (Ra 누산기 사슬) | 1 / 1 / 1 | — | 누산기 늦은 전달 |
| `sdiv`/`udiv` (2⁶²/1) | 7 / 7 / 4 | 2.0 / 2.0 / 4.0 | E 는 파이프라인 아님 |
| `ldr` L1 hit (무작위 고리) | 3 / 3 / 3 | 0.33 / 0.50 / 0.50 | `*p==p` 면 0.33/0.50/3.0 |
| `ldr [x2, x1, lsl #3]` | 4 / 4 / 4 | — | 연습 4 |
| `fadd d` | 2.08 / 2.5 / 2.5 | 0.24–0.25 / 0.33 / 0.33 | |
| `fmul d` | 3.0 / 3.0 / 3.5 | 0.25 / 0.33 / 0.33 | |
| `fmadd d` | 3.08 / 3.6 / 3.5 | 0.25 / 0.33 / 0.33 | |

클럭 추정 (add 1 사이클 가정): Super 3717–3846, Perf 3937–3952, Eff 2631–2941 MHz (실행마다 다름). 원본 출력은 `bench_m6.txt` (최종 실행 전체 + 앞선 실행 2개). 최종 실행의 표 일부:

```text
kernel                           ps/op cyc(all) | Super          Perf           Eff
lat_add      add x1,x1,#1          269     1.00 |   1.00 (n=38)     -              -
tput_add     (16 chains)            58     0.23 |   0.14 (n=19)    0.26 (n=21)     -
lat_mul      mul x1,x1,x2          804     3.00 |   3.00 (n= 1)    3.00 (n=38)     -
lat_madd_acc via Ra                273     1.01 |   1.01 (n=37)     -             1.00 (n= 1)
lat_sdiv     2^62/1               1879     7.00 |   7.00 (n=13)    7.01 (n=22)     -
tput_sdiv    (16 chains)           521     2.00 |    -             2.00 (n=33)    4.06 (n= 5)
lat_fadd     fadd d0,d0,d1         673     2.50 |    -             2.50 (n=39)    2.50 (n= 1)
k_empty      (loop tail only)       14     0.06 |    -             0.06 (n=41)     -
```

해석과 정직한 한계:

- **SDIV/UDIV 지연은 피연산자에 따라 변하지 않았다.** 몫 0, 3비트, 17·32·33·49·63·64비트, 음수, x/0 모두 P/S 7.0, E 4.0 (`bench div`). 제수가 1 로 고정이라 "제수 비트 수" 의존성은 재지 않았다. Cortex-M3/M4 의 `SDIV` 는 2–12 사이클로 값에 따라 달라진다고 TRM 에 있다 (확인 권장) — 상수 시간이 필요한 암호 코드에서 차이가 난다.
- **반정수 사이클** (FADD 2.5, FMADD 3.6 on P) 은 해석하지 못했다. 값 (0, 비정규수, 1.0 등) 을 바꿔도 같았다 (`bench fp`). FP 와 정수 파이프의 클럭 관계나 전달 경로 차이일 수 있으나 추측이다. 사이클을 "add 대비 비" 로 정의했으므로 숫자 자체는 재현된다.
- Super 의 처리량 칸 일부 (MUL 0.33–0.38, LDR 0.33) 는 표본 수가 작다 (n = 4–8). 공개된 Apple 코어 수치와 비교하지 않았다 (인용할 만한 M6 자료가 없다).
- 처리량 커널은 사슬 16개라 `chains_needed` 기준 모두 충분하다 (MUL 6, FADD ≈ 8, SDIV 4). 사슬을 4개로 줄인 `tput_fadd4` 는 0.61 (= 2.5/4) 로 지연에 묶인다 (연습 5).

### Part C — Cortex-M 으로 옮기면

| | Apple M6 (EL0) | Cortex-M3/M4/M7 |
|---|---|---|
| 카운터 | `CNTVCT_EL0`, 실제 24 MHz | `DWT->CYCCNT` (0xE0001004), **코어 클럭 그대로** 32비트 |
| 켜기 | 이미 켜짐 (`CNTKCTL_EL1.EL0VCTEN`) | `CoreDebug->DEMCR |= TRCENA` (bit 24), `DWT->CTRL |= CYCCNTENA` (bit 0); M7 은 `DWT->LAR = 0xC5ACCE55` 가 필요할 수 있음 |
| 사이클 | 환산 (add 대비) | 직접 — 단 플래시 wait state, 캐시, 인터럽트가 섞인다 |
| 직렬화 | `isb` | in-order 라 대개 불필요; 필요하면 `__DSB(); __ISB()` |
| 없는 코어 | — | Cortex-M0/M0+ (ARMv6-M) 는 CYCCNT 없음 → SysTick (24비트 down-counter) |
| wrap | 64비트, 사실상 없음 | CYCCNT 32비트 @ 168 MHz ≈ 25.6초 → `(uint32_t)(t1 − t0)` |

(DWT 레지스터 주소·비트는 ARMv7-M ARM / CMSIS 기준, 확인 권장.) 연습 2 가 `systick_elapsed` 와 `cyccnt_elapsed` 를 구현한다. ISR 예산 검사라면 "최댓값" 이 중요하다 — 중앙값은 평소, 최대·p99 는 최악을 말한다 (연습 3).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

<!-- code: solution.S diff=starter.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c solution.S -o /tmp/ARM-M03_solution && /tmp/ARM-M03_solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c solution.S -o /tmp/ARM-M03_san && /tmp/ARM-M03_san
```

실제 출력 끝부분 (전부 PASS, exit 0, 0.03 s; ASan/UBSan 빌드도 `101/101 passed`, 보고 없음):

```text
PASS  chains_needed(3100, 1000) == 4 && chains_needed(5, 0) == 0
PASS  ps_add == 250 && ps_mul == 750 && mcycles(ps_mul, ps_add) == 3000
PASS  ps24 == 250
101/101 passed
```

**Linux/ELF 에서도 같은 파일.** `.S` 맨 위의 `FUNC`/`ENDF` 매크로가 Mach-O 의 `_` 접두사와 ELF 의 `.type`/`.size` 를 흡수한다. Lima VM (aarch64 Ubuntu) 실제 실행:

```text
gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
INFO  cntfrq_el0 = 24000000 Hz
101/101 passed
21/21 passed
Ubuntu clang version 21.1.8 (6ubuntu1)
INFO  cntfrq_el0 = 24000000 Hz
101/101 passed
21/21 passed
    98: 0000000000002da0    12 FUNC    GLOBAL DEFAULT   12 cnt_read
   151: 0000000000002df0    84 FUNC    GLOBAL DEFAULT   12 lat_add
```

게스트의 `CNTFRQ_EL0` 는 24 MHz — 같은 하드웨어 카운터를 macOS 는 1 GHz 단위로, 리눅스 게스트는 원래 단위로 본다. 하네스는 `cnt_freq()` 로 단위를 받으니 양쪽에서 같은 ns 가 나온다. `cnt_read` 의 ELF `.size` = 12 B = 명령 3개.

실측 데모 빌드 (`demo_bench.c` 는 `BENCH_NO_MAIN` 으로 `solution.c` 를 그대로 `#include` — 테스트를 통과한 함수가 측정한다):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c solution.S demo_isb.S practice.S -o /tmp/ARM-M03_bench
/tmp/ARM-M03_bench res | table | table bg | ldr | div | fp | isb | prac      # 하나씩
clang -std=c11 -O2 -S -fno-asynchronous-unwind-tables -o - demo_sink.c        # 함정 ③
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `mrs x0, cntvct_el0` 만 | 비순차 실행: 구간 끝 읽기가 사슬보다 먼저 실행될 수 있다 (실측 −2눈금) | `isb` + `mrs` (또는 FEAT_ECV 의 `CNTVCTSS_EL0`) |
| `1e9 / CNTFRQ` 를 해상도로 씀 | M6: 1 ns 라고 하지만 눈금은 41.67 ns | 연속 읽기로 눈금을 잰다, 구간 ≥ 4만 ns |
| 짧은 구간 (명령 수십 개) 을 한 번 잼 | 1 눈금 = 160 사이클. 0 또는 41 만 나온다 | 반복 10⁵–10⁶ 개 + 반복 측정 |
| 평균 사용 | 문맥 전환 한 번이 평균을 망친다 (max 가 157124 tick 인 샘플이 실제로 있었다) | 중앙값·최솟값, 이상치 개수 보고 |
| `*p == p` 로 L1 지연 측정 | M6 S/P 코어에서 0.33–0.5 사이클 (예측으로 사슬이 끊김) | 무작위 순열 고리, 칸마다 다른 라인 |
| 처리량 커널에 사슬 2–4개 | `L/k` 를 재게 된다 (`tput_fadd4` = 0.61) | `⌈L/T⌉` 이상 (여기선 16) |
| C 커널 결과를 버림 | `-O2` 가 루프를 지운다 (`ret` 하나) | 결과 싱크 + 필요하면 `asm volatile("" : "+r"(x))` |
| 다른 시각에 잰 GHz 로 사이클 환산 | 코어가 S/P/E 를 오가며 클럭이 2.6–3.9 GHz 로 바뀐다 | 같은 쌍 안에서 add 와의 **비**로 |
| `(lo + hi) / 2`, `t * 1e9 / f` | 64비트 넘침 (5절 표) | 차이·몫/나머지로 쪼개기 |
| `x − med > med` 로 이상치 | `x < med` 면 wrap → 작은 샘플이 이상치로 (작성 중 실제 버그) | `x > med &&` 먼저 |
| 사슬 결과를 XOR 로 합침 | 같은 값 짝수 개 → 항상 0, 테스트가 무력 | 합 또는 사슬마다 다른 초기값 |
| `PMCCNTR_EL0` 를 유저 공간에서 읽기 | EL0 접근이 꺼져 있으면 Undefined → 이 Mac 에서 SIGILL (exit 132) | 시스템 카운터 + 환산, 또는 OS 의 perf/kperf 인터페이스 |

## 8. 면접 Q&A

<details>
<summary>Q1. ARM 에서 "사이클" 을 재려면 무엇을 읽나요? 유저 공간에서 PMCCNTR 를 읽으면?</summary>

코어 사이클은 PMU 의 `PMCCNTR_EL0` 인데 EL0 접근은 `PMUSERENR_EL0.EN` 등을 커널이 켜 줘야 한다. macOS 는 안 켜 준다 — 이 Mac 에서 `mrs x0, pmccntr_el0` 는 SIGILL (exit 132). 리눅스는 `perf_event_open` (커널이 PMU 를 읽어 줌) 이 정석이다. 그래서 유저 공간 마이크로벤치는 보통 시스템 카운터 `CNTVCT_EL0` 로 시간을 재고, 지연이 1 사이클로 알려진 의존 `add` 사슬로 클럭을 추정해 환산한다. 꼬리질문 "그 가정이 틀리면?" → 모든 사이클 값이 같은 비율로 틀리지만 명령 사이의 **비** 는 맞다; 그래서 표를 "add 대비" 로 정의했다.

</details>

<details>
<summary>Q2. 카운터 읽기 앞에 ISB 는 왜 넣나요? DSB 나 DMB 로는 안 되나요?</summary>

`mrs cntvct_el0` 는 입력이 없는 명령이라 비순차 코어가 앞 명령이 끝나기 전에 실행할 수 있다 (Arm ARM 이 이 읽기의 추측·순서 바뀜을 명시하고 ISB 를 요구한다, 확인 권장). DMB 는 메모리 접근끼리의 순서만, DSB 는 메모리 접근 완료를 기다리지만 이후 명령 실행을 막는다고 보장하지 않는다; ISB 는 앞 명령을 모두 끝내고 파이프라인을 비운 뒤 다시 가져오므로 "앞 명령 완료 → 카운터 읽기" 를 보장한다. 데이터 의존성 트릭도 안 된다 (실측: dep load 판 = ISB 없음 판). FEAT_ECV (Armv8.6) 의 `CNTVCTSS_EL0` 는 ISB 없이 순서가 지켜지는 판이다. 꼬리: "구간 끝에도 ISB?" → 끝 읽기 뒤의 명령이 앞당겨 실행되는 건 측정에 영향이 적지만, 끝 읽기 **앞** 의 명령이 다 끝나야 하므로 끝 읽기 앞에도 ISB 가 필요하다.

</details>

<details>
<summary>Q3. 지연과 처리량은 어떻게 따로 재나요? 사슬은 몇 개가 필요하죠?</summary>

지연: 각 명령이 바로 앞 명령의 결과를 입력으로 받는 사슬 하나 (`mul x1, x1, x2` × N) → 시간/N. 처리량: 서로 독립인 사슬 k 개를 번갈아 → 시간/N 이 처리량의 역수. Little 의 법칙으로 동시에 날아다녀야 하는 명령 수 = 지연 × 처리량 → `k ≥ ⌈L/T⌉`. M6 MUL: 3 / 0.5 = 6, FADD: 2.5 / 0.33 ≈ 8. 모자라면 `L/k` 를 잰다 (fadd 사슬 4개 → 0.61 실측). 꼬리: "루프 오버헤드는?" → `subs/b.ne` 는 사슬과 독립이라 지연 커널에서는 겹치고, 처리량 커널에서는 디스패치 슬롯을 먹으므로 반복당 명령을 16개로 키워 비중을 줄였다 (`k_empty` = 0.06 사이클/명령).

</details>

<details>
<summary>Q4. x86 의 rdtsc 와 비교하면요?</summary>

TSC 는 (invariant TSC 라면) 코어 클럭과 무관한 **고정 주파수**로 증가한다 — ARM 시스템 카운터와 같은 "시간" 이지 사이클이 아니다 (다만 주파수가 보통 GHz 급이라 해상도는 훨씬 좋다). 순서: `rdtsc` 는 직렬화되지 않아 `lfence; rdtsc` (앞 명령 완료 대기) 로 시작하고, 끝은 `rdtscp; lfence` (rdtscp 는 앞 명령 완료를 기다리지만 뒤 명령이 앞당겨지는 것은 막지 않음) 를 쓰는 패턴이 Intel 백서에 있다 (Paoloni 2010; `cpuid` 직렬화도 쓰임, 확인 권장). ARM 의 `isb; mrs` 가 `lfence; rdtsc` 의 짝이다. 사이클이 필요하면 x86 도 PMU (`perf`) 로 간다.

</details>

<details>
<summary>Q5. 샘플을 여러 번 재서 왜 평균이 아니라 중앙값/최솟값을 쓰나요?</summary>

잡음이 대칭이 아니다: 인터럽트·문맥 전환·코어 이동은 시간을 **늘리기만** 한다 (이번 실측에서 ISB 판 max 53708 tick, 정상 459). 평균은 그런 샘플 하나에 끌려간다. 최솟값은 "방해받지 않은 실행" 에 가깝지만 운 좋은 양자화(−1 눈금)에도 끌린다; 중앙값은 둘 다에 강하다. 여기선 쌍별 비의 중앙값을 주 지표로, min/min 비를 보조로, `outliers` (2×중앙값 초과) 개수를 품질 지표로 냈다. 꼬리: "꼬리 지연이 중요한 ISR 은?" → 최대·p99 (연습 3) 를 따로 보고, 측정 자체는 인터럽트를 막은 상태와 실제 상태를 둘 다 잰다.

</details>

<details>
<summary>Q6. 포인터 추적 벤치마크가 0.33 사이클이 나왔습니다. 무엇이 잘못됐나요?</summary>

load-to-use 가 1 사이클 미만일 수는 없으니 사슬이 끊긴 것이다. 이 M6 S/P 코어에서 각 ldr 명령이 매 반복 같은 주소에서 같은 값을 읽을 때 (고리 크기 1, 2, 16) 만 그랬고, 3·15·17칸이나 무작위 순열은 3.00 이었다. 공개 연구 (SLAP/FLOP, 2025) 가 Apple M2/M3 이후 코어의 load 주소/값 예측기를 보고했으므로 그것으로 추정한다 (확인 안 됨, E 코어는 영향 없음). 대책: 칸마다 다른 캐시 라인에 무작위 순환 순열, 크기는 목표 캐시 레벨에 맞춰 (L1: 4–32 KB). 하드웨어 프리페처·TLB 도 같은 이유로 무작위가 필요하다.

</details>

<details>
<summary>Q7. Cortex-M4 에서 함수 하나가 몇 사이클인지 재 보세요.</summary>

`CoreDebug->DEMCR |= (1u << 24)` (TRCENA) → `DWT->CYCCNT = 0` → `DWT->CTRL |= 1` (CYCCNTENA) → `t0 = DWT->CYCCNT; f(); t1 = DWT->CYCCNT; cyc = t1 − t0 − 오버헤드`. 오버헤드는 빈 구간으로 보정 (보통 몇 사이클). 주의: 플래시 wait state·ART/캐시 때문에 첫 실행과 반복이 다르고, 인터럽트를 막고 재야 순수 값이 나오며, 32비트라 168 MHz 에서 약 25.6초마다 wrap (unsigned 뺄셈이면 한 번은 OK). M0/M0+ 는 CYCCNT 가 없어 SysTick (24비트 down) 을 쓰고 경과 = `start − end` (주기를 넘으면 reload+1 보정, 연습 2). 주소·비트는 ARMv7-M ARM / CMSIS 기준 (확인 권장).

</details>

<details>
<summary>Q8. -O2 에서 측정 루프가 사라지지 않게 하려면?</summary>

실측: 결과를 안 쓰는 C 루프는 `ret` 하나가 되고, 결과를 돌려줘도 `add x0, x0, x0, lsl #1` (3n) 닫힌 식이 된다. 방법: (1) 커널을 별도 `.S` 로 — 컴파일러가 안을 못 본다, (2) 결과를 volatile 싱크에 저장, (3) 루프 안에 `asm volatile("" : "+r"(x))` 같은 최적화 장벽, (4) 입력도 컴파일 타임 상수가 아니게. 그리고 **objdump 로 확인**한다 — 이 레슨의 `lat_add` 는 add 16개 + `subs/b.ne` 가 실제 기계어에 있다.

</details>

<details>
<summary>Q9. 나눗셈 지연이 값에 따라 달라지나요? 왜 그게 중요하죠?</summary>

이 M6 에서는 아니었다: 몫 0비트부터 64비트, 음수, 0 나누기까지 P/S 7 사이클, E 4 사이클로 일정 (제수 1 고정 조건). 처리량은 P 에서 2 사이클마다 1개 (파이프라인), E 에서 4 (= 지연, 비파이프라인). Cortex-M3/M4 의 SDIV/UDIV 는 2–12 사이클로 피연산자 의존이라고 TRM 에 있다 (확인 권장). 중요한 이유: ① ISR 최악 실행시간 (WCET) 분석, ② 암호 코드의 상수 시간 요구 — 값에 따라 시간이 바뀌면 타이밍 사이드 채널이 된다.

</details>

## 9. 추가 연습

### 연습 1 — 해상도에서 필요한 반복 수

`uint64_t iters_for_error(uint64_t tick_ps, uint64_t op_ps, uint64_t err_ppm)` — 1 눈금 양자화 오차가 `err_ppm` 이하가 되는 최소 iters (반복당 명령 16개).

<details>
<summary>답 보기</summary>

필요한 구간 = `⌈tick_ps · 10⁶ / err_ppm⌉` ps, 명령 수 = `⌈구간 / op_ps⌉`, iters = `⌈명령 / 16⌉`. 24 MHz 눈금 (41667 ps), add 250 ps, 0.1% → 41.667 µs → 166,668 명령 → **10417**. 1% 면 1042. 진짜 1 ns 해상도였다면 250 — 41.67배 차이가 "CNTFRQ 를 믿으면 안 되는" 비용이다.

</details>

### 연습 2 — Cortex-M 카운터의 경과 시간

`uint32_t systick_elapsed(uint32_t start, uint32_t end, uint32_t reload)` (24비트 down-counter, 주기 reload+1), `uint32_t cyccnt_elapsed(uint32_t t0, uint32_t t1)`.

<details>
<summary>답 보기</summary>

SysTick 은 **내려가므로** `start ≥ end ? start − end : start + (reload + 1) − end` — 한 주기 안에서만 유효. 예: `(5, 0xFFFFF0, 0xFFFFFF)` → 0 을 지나 다시 reload 로 → 21. CYCCNT 는 올라가는 32비트라 `t1 − t0` (uint32_t) 가 wrap 을 한 번까지 자동 처리: `(0xFFFFFFF0, 0x10)` → 0x20. 두 번 wrap 하는 구간은 어떤 식으로도 구분 못 한다 → 측정 구간을 주기보다 짧게.

</details>

### 연습 3 — 꼬리 지연: nearest-rank 백분위

`uint64_t u64_percentile(uint64_t *a, size_t n, unsigned p)` — 정렬 후 `rank = ⌈p·n/100⌉` (1-based), p = 0 → 최솟값.

<details>
<summary>답 보기</summary>

1..10 에서 p50 = 5, p90 = 9, p100 = 10. `{100,200,300}` 의 p34 = ⌈1.02⌉ = 2번째 = 200. ISR 예산은 중앙값이 아니라 p99/최대로 판정한다. 보간 백분위 (선형) 도 있지만 정수 tick 데이터에서는 실제로 관측된 값을 돌려주는 nearest-rank 가 설명하기 쉽다.

</details>

### 연습 4 — 인덱스 추적 load (`practice.S`)

`uint64_t lat_ldr_idx(uint64_t iters, uint64_t i0, const uint64_t *tab)` — `i ← tab[i]` 를 `ldr x1, [x2, x1, lsl #3]` 로 16번/반복.

<details>
<summary>답 보기</summary>

본문은 한 줄 `.rept 16` / `ldr x1, [x2, x1, lsl #3]`. 테스트는 64칸 무작위 순환 순열에서 16·iters 칸 따라간 결과를 C 와 비교하고, iters = 4 (64스텝) 면 제자리. 실측 (`bench prac`): 포인터 `ldr x1,[x1]` 3.00 사이클, 인덱스형 **4.00–4.02** (S/P/E 모두) — 주소에 shift+add 가 끼면 load-to-use 가 1 사이클 늘었다. 연결 리스트 대신 인덱스 배열을 쓰는 펌웨어 자료구조 (풀 할당자) 에서 의미가 있다.

</details>

### 연습 5 — 사슬이 모자란 처리량 커널 (`practice.S`)

`double tput_fadd4(uint64_t iters, double x, double y)` — 사슬 4개 (d16–d19) × fadd 4개. 그리고 `expected_mcyc(L, T, k) = max(T, ⌈L/k⌉)`.

<details>
<summary>답 보기</summary>

`.rept 4` 안에 `fadd d16..d19` 네 줄, 끝에서 `((d16+d17)+d18)+d19`. FADD L ≈ 2.5, T ≈ 0.33 → 필요 사슬 8–10 > 4 → 예상 `2.5/4 = 0.625`. 실측: 사슬 4개 **0.58–0.61**, 사슬 16개 0.24–0.33, 사슬 1개 2.5 (`bench prac`). "처리량" 이라고 이름 붙인 커널이 실제로는 지연을 재고 있을 수 있다는 것 — 표를 만들기 전에 `chains_needed` 로 먼저 검산한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

<!-- code: practice.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S -o /tmp/ARM-M03_practice && /tmp/ARM-M03_practice   # 21/21 passed
```

ASan/UBSan 빌드 (`-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined`) 도 `21/21 passed`, Lima VM 의 gcc 15.2 / clang 21 빌드도 `21/21 passed`.

## 10. 핵심 정리

- 카운터 읽기 = `isb; mrs x0, cntvct_el0`. ISB 없으면 구간이 짧게 잡힌다 (실측 −2눈금); 데이터 의존성으로는 못 묶는다. FEAT_ECV 면 `CNTVCTSS_EL0`.
- `CNTFRQ_EL0` 는 단위지 해상도가 아니다: M6 은 1 GHz 라고 읽히지만 41/42 씩 → 41.67 ns (24 MHz). 해상도는 연속 읽기로 잰다. 리눅스 게스트는 24 MHz, +1.
- 1 눈금 ≈ 160 사이클 → 샘플당 10⁵–10⁶ 명령, 반복 측정, 중앙값/최솟값 + 이상치 개수.
- 지연 = 의존 사슬 1개, 처리량 = 독립 사슬 `⌈L/T⌉` 개 이상. 사이클 = 바로 옆 add 사슬과의 비 (코어·클럭이 바뀌어도 유지).
- M6 (S/P/E): ADD 1, MUL 3, MADD 4/4/3 (누산기 경유 1), SDIV 7/7/4, L1 LDR 3 (인덱스 4), FADD 2.1/2.5/2.5, FMUL 3/3/3.5.
- 측정이 거짓말하는 길: 해상도, ISB 누락, 죽은 코드 제거 (`ret` 하나), 값/주소 예측 (`*p==p` → 0.33 사이클).
- 하네스 수학은 넘침과의 싸움: `lo+(hi−lo)/2`, `q·1e9 + r·1e9/f`, 클램프, `x > med && x−med > med`. 시계는 주입해서 테스트한다.
- Cortex-M: `DWT->CYCCNT` (TRCENA + CYCCNTENA, 32비트 wrap), M0 는 SysTick 24비트 down.

## 참고자료

- [Arm Architecture Reference Manual for A-profile (DDI 0487)](https://developer.arm.com/documentation/ddi0487/latest) — D12 "The Generic Timer in AArch64 state" (CNTVCT/CNTFRQ, 순서·ISB, Armv8.6 1 GHz, FEAT_ECV CNTVCTSS), C6 ISB/SDIV/MADD
- [Arm — CNTVCTSS_EL0 (system register)](https://developer.arm.com/documentation/ddi0601/latest/AArch64-Registers/CNTVCTSS-EL0--Counter-timer-Self-Synchronized-Virtual-Count-Register)
- Linux `arch/arm64/include/asm/arch_timer.h` — `__arch_counter_get_cntvct()`, `arch_counter_enforce_ordering()`
- [ARMv7-M Architecture Reference Manual (DDI 0403)](https://developer.arm.com/documentation/ddi0403/latest) — DWT CYCCNT, DEMCR.TRCENA, SysTick
- Cortex-M3/M4 Technical Reference Manual — 명령 사이클 표 (SDIV/UDIV 2–12)
- G. Paoloni, *How to Benchmark Code Execution Times on Intel IA-32 and IA-64 Instruction Set Architectures*, Intel, 2010
- J. Kim, D. Genkin 외, *SLAP: Data Speculation Attacks via Load Address Prediction on Apple Silicon* / *FLOP: Breaking the Apple M3 CPU via False Load Output Predictions*, 2025
- [Apple — Writing ARM64 code for Apple platforms](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms) (x18 예약)
- 관련 레슨: ARM-B07 (24 MHz tick → ns Q32.32, CNTFRQ 1 GHz 첫 관찰), ARM-M05 (ISB 의미), ARCH-M03 (add 사슬로 클럭 추정), ARCH-M08 (코어 번호 매핑), ARM-B10 (memcpy 벤치), 다음 단계 ARM-M02 (인라인 asm 제약조건), ARM-M06 (NEON 처리량)
