---
id: MEM-B05
time: 60분
updated: 2026-11-15
---
# MEM-B05 리프레시: tREFI, tRFC와 성능 손실

## 학습 목표

- DRAM 이 왜 리프레시(refresh)를 해야 하는지(커패시터 누설, 64 ms 리텐션, 8192 번의 REF)를 말하고, tREFI · tRFC 의 뜻을 정확히 구분한다.
- 밀도(2/4/8/16 Gb)와 온도(1x ≤ 85 °C, 2x 85–95 °C)에 따라 리프레시로 잃는 시간 비율 tRFC / tREFI 를 **정수 피코초 + 기약분수**로 계산한다 (8 Gb 1x = 7/156 = 4.487 %, 16 Gb 2x = 11/78 = 14.103 %).
- all-bank 리프레시와 per-bank / same-bank 리프레시를 뱅크 시뮬레이터에 넣어 **평균이 아니라 꼬리 지연(p99, p99.9, max)** 이 어떻게 변하는지 숫자로 보인다.
- 메모리 컨트롤러 레지스터 값을 ns → 클럭으로 바꿀 때 tRFC 는 올림, tREFI 는 내림이어야 하는 이유를 말하고, REF 연기(postpone) · 당김(pull-in) 규칙을 "빚(debt) 카운터"로 검사한다.
- 밀도가 오를수록 리프레시 비용이 커지는 이유(REF 한 번에 행 수 2배, tRFC 260 → 350 → 550 ns)와 고온 2x 전환을 펌웨어가 어떻게 다루는지 설명한다.

## 1. 문제

면접에서 "16 Gb DDR4 에서 리프레시가 대역폭을 얼마나 먹나요, 왜 점점 나빠지죠?" 는 메모리 컨트롤러 · SoC 검증 · 성능 팀의 단골 질문이고, 보드 bring-up 에서는 tRFC 를 작은 밀도 값으로 잘못 넣으면 **고온 · 고부하에서만** 데이터가 깨지는 버그가 된다.

DRAM 셀은 커패시터 하나에 전하로 비트를 저장한다. 전하는 새어 나가므로, 모든 행(row)을 리텐션 시간(retention time, DDR4 대표값 64 ms, 85 °C 초과 시 32 ms) 안에 한 번씩 읽어서 다시 써야 한다. 컨트롤러는 행 주소를 몰라도 된다 — **REF 명령 한 번**을 보내면 칩 안의 행 카운터가 다음 몇 행을 리프레시한다. 64 ms 동안 REF 를 8192 번 보내야 하므로 평균 간격이 tREFI = 64 ms / 8192 ≈ 7.8 µs 다. REF 하나가 끝날 때까지 걸리는 시간이 tRFC 이고, 그동안 (all-bank REF 라면) 그 랭크(rank)의 **모든 뱅크가 읽기 · 쓰기를 못 한다**.

Don 의 SSD 배경으로 말하면: NAND 는 전원이 꺼져도 몇 년 버티지만 리텐션 오류를 백그라운드 스캔 · 리프레시(재기록)로 막고, DRAM 은 같은 문제를 64 ms 주기로 하드웨어가 강제로 푼다. 둘 다 "백그라운드 유지보수가 포그라운드 I/O 의 꼬리 지연을 만든다"는 같은 구조다.

```svg
<svg viewBox="0 0 760 396" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">리프레시 막힘 창 (축척 그대로: 75 px = 1 µs) — all-bank 16 Gb vs DDR5식 same-bank</text>
  <text x="10" y="44" fill="currentColor" font-size="12">(a) all-bank: tREFI 7.8 µs 마다 모든 뱅크가 tRFC 550 ns 동안 막힘</text>
  <g fill="currentColor" font-size="12"><text x="10" y="73">bank 0</text><text x="10" y="98">bank 1</text><text x="10" y="123">bank 2</text><text x="10" y="148">bank 3..15</text></g>
  <g stroke="currentColor" fill="none" opacity="0.5"><line x1="100" y1="69" x2="730" y2="69"/><line x1="100" y1="94" x2="730" y2="94"/><line x1="100" y1="119" x2="730" y2="119"/><line x1="100" y1="144" x2="730" y2="144"/></g>
  <g stroke="currentColor"><rect x="100" y="60" width="41.25" height="18" class="f-warn"/><rect x="100" y="85" width="41.25" height="18" class="f-warn"/><rect x="100" y="110" width="41.25" height="18" class="f-warn"/><rect x="100" y="135" width="41.25" height="18" class="f-warn"/><rect x="685" y="60" width="41.25" height="18" class="f-warn"/><rect x="685" y="85" width="41.25" height="18" class="f-warn"/><rect x="685" y="110" width="41.25" height="18" class="f-warn"/><rect x="685" y="135" width="41.25" height="18" class="f-warn"/></g>
  <text x="146" y="57" fill="currentColor" font-size="12">tRFC 550 ns</text>
  <path d="M682 106 L682 128" stroke="currentColor" stroke-width="2"/><path d="M677 112 L682 103 L687 112 Z" class="f-acc" stroke="currentColor"/>
  <path d="M100 162 L100 168 L685 168 L685 162" fill="none" stroke="currentColor"/>
  <text x="300" y="183" fill="currentColor" font-size="12">tREFI = 7.8 µs (85 °C 초과면 3.9 µs)</text>
  <text x="10" y="203" fill="currentColor" font-size="12">▲ bank 2 요청(7.760 µs 도착)은 7.8 µs 의 REF 와 겹친다 → 8.35 µs 에 시작, 지연 620 ns</text>
  <text x="10" y="230" fill="currentColor" font-size="12">(b) same-bank (DDR5 REFsb, 모델 가정): 뱅크 b 는 슬롯 b % 4 — 975 ns 씩 엇갈려 130 ns 만 막힘</text>
  <g fill="currentColor" font-size="12"><text x="10" y="253">slot 0</text><text x="10" y="278">slot 1</text><text x="10" y="303">slot 2</text><text x="10" y="328">slot 3</text></g>
  <g stroke="currentColor" fill="none" opacity="0.5"><line x1="100" y1="249" x2="730" y2="249"/><line x1="100" y1="274" x2="730" y2="274"/><line x1="100" y1="299" x2="730" y2="299"/><line x1="100" y1="324" x2="730" y2="324"/></g>
  <g stroke="currentColor" class="f-acc"><rect x="100" y="240" width="9.75" height="18"/><rect x="392.5" y="240" width="9.75" height="18"/><rect x="685" y="240" width="9.75" height="18"/><rect x="173.1" y="265" width="9.75" height="18"/><rect x="465.6" y="265" width="9.75" height="18"/><rect x="246.3" y="290" width="9.75" height="18"/><rect x="538.8" y="290" width="9.75" height="18"/><rect x="319.4" y="315" width="9.75" height="18"/><rect x="611.9" y="315" width="9.75" height="18"/></g>
  <g stroke="currentColor"><line x1="100" y1="348" x2="730" y2="348"/><line x1="100" y1="344" x2="100" y2="352"/><line x1="175" y1="344" x2="175" y2="352"/><line x1="250" y1="344" x2="250" y2="352"/><line x1="325" y1="344" x2="325" y2="352"/><line x1="400" y1="344" x2="400" y2="352"/><line x1="475" y1="344" x2="475" y2="352"/><line x1="550" y1="344" x2="550" y2="352"/><line x1="625" y1="344" x2="625" y2="352"/><line x1="700" y1="344" x2="700" y2="352"/></g>
  <g fill="currentColor" font-size="12"><text x="96" y="366">0</text><text x="171" y="366">1</text><text x="246" y="366">2</text><text x="321" y="366">3</text><text x="396" y="366">4</text><text x="471" y="366">5</text><text x="546" y="366">6</text><text x="621" y="366">7</text><text x="696" y="366">8 µs</text></g>
  <text x="10" y="388" fill="currentColor" font-size="12">slot k = bank k, k+4, k+8, k+12 (BG 마다 같은 번호). 다른 슬롯 뱅크는 그동안 계속 요청을 받는다.</text>
</svg>
```

### Part A — 손실 비율 계산기 (밀도 × 온도)

- 시간은 전부 정수 피코초(ps). `trefi_ps_for_temp(temp)`: −40…85 °C → 7 800 000 ps, 86…95 °C → 3 900 000 ps (2x), 그 밖은 `RC_ETEMP`.
- `frac_make(num, den)` 은 기약분수, `refresh_overhead(density, temp, &f)` 는 tRFC1 / tREFI, `frac_milli_pct(f)` 는 0.001 % 단위 반올림(half-up). 예: 8 Gb, 25 °C → 7/156 → 4487 (= 4.487 %).
- 밀도 표에 없는 값(3 Gb, 12 Gb)은 `RC_EINVAL`. DDR5 16 Gb REFab 295/3900, REFsb 130/3900 도 같은 함수로 확인한다.

### Part B — all-bank vs per-bank 리프레시 시뮬레이터

- 뱅크 b 의 막힘 창은 `[off_b + k·period, off_b + k·period + dur)`. all-bank 는 슬롯 1개(모든 뱅크 off = 0), per-bank · same-bank 는 슬롯 `b % nslots` 마다 `period / nslots` 씩 엇갈린다.
- `ref_next_free(r, bank, t, busy)`: t 이후에 뱅크를 `[s, s+busy)` 동안 쓸 수 있는 가장 이른 s. REF 전에는 뱅크가 precharge 되어 있어야 하므로 **창과 겹치는 요청은 시작하지 않는다**. O(1) 산수로, 느린 "창 훑기" 기준 구현과 4만 건 대조한다.
- `sim_run`: 뱅크별 FCFS, close-page. 요청마다 뱅크 점유 tRC = 46.25 ns, 데이터 반환 tRCD + CL + 4 = 30 ns (MEM-B03 어휘). 리프레시 때문에 시작이 **직접** 밀린 요청 수를 돌려준다.
- `lat_stats`: mean / p50 / p99 / p99.9 / max (최근접 순위). 200만 요청 × 8가지 방식 × 2가지 부하 실험은 `demo_refresh.c`.

### Part C — 컨트롤러 레지스터와 REF 연기 규칙 (bring-up)

- `mc_refresh_regs(density, temp, tCK)`: 컨트롤러에 쓸 tREFI_ck(내림), tRFC_ck(올림). DDR4-3200 8 Gb → 12480 / 560, DDR4-2400(tCK 833 ps) → 9363 / 421.
- `ref_trace_check(t[], n, tREFI, horizon, 8, &rep)`: 로직 애널라이저 · 컨트롤러 트레이스로 뽑은 REF 시각 목록이 "최대 8개 연기, 최대 8개 당김" 규칙(DDR4 대표값)을 지키는지 검사하고, 최대 연기 · 당김과 첫 위반 시각을 보고한다.
- 리텐션 산수(64 ms / 8192, REF 한 번에 뱅크당 행 수)와 온도 히스테리시스는 9절 추가 연습.

## 2. Problem (English)

You are writing the refresh part of a DDR4 memory-controller model and its bring-up checks. All times are integer picoseconds.

```c
typedef int64_t ps_t;
typedef struct { uint64_t num, den; } frac_t;                    /* always reduced */
typedef struct { ps_t period, dur; int nslots; } ref_cfg;        /* nslots = 1: all-bank */
typedef struct { int nbanks; ps_t busy, lat; ref_cfg ref; } sim_cfg;
typedef struct { ps_t arr; int bank; } req_t;

ps_t     trefi_ps_for_temp(int temp_c);                    /* 7.8 us <= 85 C, 3.9 us <= 95 C, else -2 */
frac_t   frac_make(uint64_t num, uint64_t den);
int      refresh_overhead(int density_gb, int temp_c, frac_t *out);   /* tRFC1 / tREFI */
uint64_t frac_milli_pct(frac_t f);                          /* round half up, 0.001 % units */
ps_t     ref_next_free(const ref_cfg *r, int bank, ps_t t, ps_t busy);
long     sim_run(const sim_cfg *c, const req_t *rq, size_t n, ps_t *lat_out); /* returns #pushed */
int      lat_stats(const ps_t *lat, size_t n, ps_t *scratch, lat_stats_t *st);
int      mc_refresh_regs(int density_gb, int temp_c, ps_t tck_ps, mc_ref_regs *out);
int      ref_trace_check(const ps_t *t, size_t n, ps_t trefi, ps_t horizon, int max_debt, ref_report *rep);
```

Constraints: no floating point in the checked paths; no heap in the model (the caller owns buffers); a request may not overlap a refresh window of its bank; tests are deterministic and finish in under 2 s.

Examples:

```text
refresh_overhead(8 Gb, 25 C)   -> 7/156   -> 4487 m%      refresh_overhead(16 Gb, 86 C) -> 11/78 -> 14103 m%
ref_next_free(all-bank 7.8us/550ns, bank 0, t=7753751 ps, busy=46250) -> 8350000 (1 ps too late)
8 Gb all-bank, requests {0,b0},{0,b1},{400ns,b0},{400ns,b0},{7760ns,b2}
   -> latency 380, 380, 30, 76.25, 420 ns; 3 requests pushed by refresh
mc_refresh_regs(8 Gb, 25 C, tCK 833 ps) -> tREFI 9363 ck, tRFC 421 ck
REF at T, then 9 REFs starting at 10T  -> OK, worst postponed = 8 (gap 9 x tREFI)
REF at T, then 9 REFs starting at 10T+1ps -> 1 violation at 10T (postponed = 9)
```

Follow-ups: How much bandwidth does refresh cost at 16 Gb and why does it get worse every generation? Why does refresh barely move the median but blow up p99? What does a controller gain by postponing REFs, and what does it risk? Why must tREFI round down but tRFC round up?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 아홉 군데다 (Part A 4개, Part B 3개, Part C 2개). `trfc1_ps` 표, `ref_offset`, 요청 생성기 `gen_requests`, 백분위 도우미, 느린 기준 구현 `ref_next_free_slow`, 하네스는 주어진다. 스텁은 멈추지 않는 안전한 틀린 값을 돌려준다 (`ref_next_free` 는 리프레시를 무시, `sim_run` 은 "모든 요청 30 ns"). 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:186  trefi_ps_for_temp(86) == 3900000 && trefi_ps_for_temp(95) == 3900000
FAIL starter.c:188  trefi_ps_for_temp(96) == RC_ETEMP && trefi_ps_for_temp(-41) == RC_ETEMP
FAIL starter.c:189  f.num == 7 && f.den == 156
…
ref_next_free vs slow scan: 40000 cases, mismatches=3640
…
FAIL starter.c:351  ref_trace_check(uns, 3, T, 10 * T, 8, &rp) == RC_EINVAL
FAIL starter.c:352  ref_trace_check(uns, 1, 0, 10 * T, 8, &rp) == RC_EINVAL
16/79 passed
```

(exit 1. 통과한 16개는 스텁 값이 우연히 맞은 경우다 — 예: 25 °C 의 tREFI, 막힘 창 밖의 `ref_next_free`, 정시 REF 로그의 "위반 0".)

## 4. 힌트

<details><summary>힌트 1 — 단위부터 고정한다</summary>

ns 를 double 로 들고 다니면 13.75 / 0.625 같은 나눗셈에서 22.000000001 → ceil 23 사고가 난다. 모든 상수를 ps 정수로 두면 tRFC 350 ns = 350000, tREFI 7.8 µs = 7800000, tCK = 625 이고, 올림은 `(a + b − 1) / b`, 내림은 `a / b` 다. 7.8 µs × 8192 = 6.4e10 ps 는 32비트를 넘으므로 `int64_t`.

</details>

<details><summary>힌트 2 — 반올림 밀리퍼센트</summary>

num/den × 100 % × 1000 = num × 100000 / den. half-up 반올림은 분자 · 분모를 2배 해서 `(2·num·100000 + den) / (2·den)`. 1/200000 = 0.0005 % → 0.5 m% → 1 이 나와야 한다.

</details>

<details><summary>힌트 3 — ref_next_free 는 "지금 창 안인가 + 다음 창 전에 끝나는가"</summary>

off = 슬롯 오프셋. t &lt; off 면 다음 창 시작 ws = off. 아니면 k = (t − off) / period, ws = off + k·period. t 가 [ws, ws + dur) 안이면 t = ws + dur 로 옮기고, 다음 창은 ws + period. 마지막으로 t + busy ≤ 다음 창 시작이면 t, 아니면 그 창 끝(다음 창 시작 + dur). busy ≤ period − dur 이면 두 번째 후보는 반드시 들어간다 (그래서 그 조건이 깨지면 −1).

</details>

<details><summary>힌트 4 — 시뮬레이터 한 줄 요약</summary>

`ready = max(arr, bank_free[b])`, `start = ref_next_free(b, ready)`, `lat = start + 30 ns − arr`, `bank_free[b] = start + 46.25 ns`. start ≠ ready 면 "리프레시가 직접 민 요청". 같은 뱅크 뒤에 줄 선 요청은 직접 밀린 것으로 세지 않지만 지연은 똑같이 늘어난다 — 5절에서 이 차이가 크다는 것을 본다.

</details>

<details><summary>힌트 5 — REF 로그 검사는 빚 카운터</summary>

k 번째 REF 의 기한을 k·tREFI 로 둔다. due(x) = ⌊x / tREFI⌋, issued(x) = (시각 ≤ x 인 REF 수). 연기 = due − issued 는 기한 경계 x = k·tREFI 에서만 커지므로 거기서만 보면 되고, 당김 = issued − due 는 REF 직후에만 커지므로 i 번째 REF 에서 (i+1) − ⌊t_i / tREFI⌋ 를 보면 된다. 두 포인터로 O(n + K).

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. **왜:** 셀 커패시터가 새므로 64 ms(고온 32 ms) 안에 모든 행을 다시 써야 한다. 컨트롤러는 REF 8192 번만 보내고, 행 순서는 칩 안의 카운터가 관리한다.
2. **얼마나:** 손실 ≈ tRFC / tREFI. DDR4 8 Gb 4.5 %, 16 Gb 7.1 %, 고온 2x 면 두 배(14.1 %). 대역폭 25.6 GB/s 채널이면 16 Gb 2x 에서 3.6 GB/s 를 잃는다.
3. **어떻게 아픈가:** 평균보다 꼬리. 요청의 몇 % 가 tRFC 만큼 기다리므로 p50 은 그대로, p99 가 수백 ns 뛴다.
4. **완화:** per-bank / same-bank REF(다른 뱅크는 계속 일함), 연기 · 당김으로 한가할 때 몰아서, FGR, 리텐션 인지 리프레시(연구), 그리고 컨트롤러가 리프레시 중인 뱅크를 피해 스케줄링.
5. **펌웨어:** SPD/데이터시트에서 tRFC 를 읽어 올림, tREFI 는 내림, 온도 센서로 2x 전환(히스테리시스), REF 로그로 검증.

### Part A — 손실 비율: 정의와 정확한 표

리프레시 하나는 tREFI 마다 tRFC 동안 랭크를 막는다. 그래서 "막혀 있는 시간 비율"은

  손실 = tRFC / tREFI   (all-bank, 1x: tREFI = 7.8 µs, 2x: 3.9 µs)

이다. 이 레슨의 정의 (JESD79-4 / 데이터시트 대표값 — 실제 부품은 데이터시트 확인): tRFC1 = 2 Gb 160 ns, 4 Gb 260 ns, 8 Gb 350 ns, 16 Gb 550 ns. `demo_refresh` 가 찍은 실제 표:

```text
== Part A: refresh overhead tRFC/tREFI (exact) ==
dens       tRFC | 1x (7.8us)               % | 2x (3.9us)               %
 2 Gb    160 ns | 4/195              2.051 | 8/195              4.103
 4 Gb    260 ns | 1/30               3.333 | 1/15               6.667
 8 Gb    350 ns | 7/156              4.487 | 7/78               8.974
16 Gb    550 ns | 11/156             7.051 | 11/78             14.103
```

```svg
<svg viewBox="0 0 720 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">리프레시 손실 tRFC / tREFI (solution.c 로 계산한 정확한 값, 30 px = 1 %)</text>
  <g fill="currentColor" font-size="12"><text x="10" y="60">DDR4 2 Gb (160 ns)</text><text x="10" y="105">DDR4 4 Gb (260 ns)</text><text x="10" y="150">DDR4 8 Gb (350 ns)</text><text x="10" y="195">DDR4 16 Gb (550 ns)</text><text x="10" y="240">DDR5 16 Gb REFab</text><text x="10" y="255">(295 ns / 3.9 µs)</text><text x="10" y="285">DDR5 REFsb 뱅크당</text><text x="10" y="300">(130 ns / 3.9 µs)</text></g>
  <g stroke="currentColor">
    <rect x="200" y="48" width="61.5" height="12" class="f-acc"/><rect x="200" y="63" width="123.1" height="12" class="f-warn"/>
    <rect x="200" y="93" width="100" height="12" class="f-acc"/><rect x="200" y="108" width="200" height="12" class="f-warn"/>
    <rect x="200" y="138" width="134.6" height="12" class="f-acc"/><rect x="200" y="153" width="269.2" height="12" class="f-warn"/>
    <rect x="200" y="183" width="211.5" height="12" class="f-acc"/><rect x="200" y="198" width="423.1" height="12" class="f-warn"/>
    <rect x="200" y="234" width="226.9" height="12" class="f-mut"/>
    <rect x="200" y="279" width="100" height="12" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="266" y="59">2.051 % (1x)</text><text x="328" y="74">4.103 % (2x)</text>
    <text x="305" y="104">3.333 %</text><text x="405" y="119">6.667 %</text>
    <text x="340" y="149">4.487 %</text><text x="474" y="164">8.974 %</text>
    <text x="417" y="194">7.051 %</text><text x="628" y="209">14.103 %</text>
    <text x="432" y="245">7.564 %</text>
    <text x="305" y="290">3.333 % (그 뱅크만)</text>
  </g>
  <line x1="200" y1="40" x2="200" y2="300" stroke="currentColor"/>
  <rect x="470" y="262" width="12" height="10" class="f-acc" stroke="currentColor"/><text x="488" y="271" fill="currentColor" font-size="12">1x (≤ 85 °C, 7.8 µs)</text>
  <rect x="470" y="280" width="12" height="10" class="f-warn" stroke="currentColor"/><text x="488" y="289" fill="currentColor" font-size="12">2x (85–95 °C, 3.9 µs)</text>
  <text x="10" y="324" fill="currentColor" font-size="12">밀도 2배마다 tRFC 는 1.35–1.63배 → 손실 비율이 계속 오른다. 고온 2x 는 정확히 2배 (테스트로 확인).</text>
</svg>
```

**왜 정수 ps 와 분수인가.** 350 / 7800 을 double 로 계산해 출력하면 4.487179… 는 맞지만, 테스트가 "4.487" 과 비교하려면 반올림 규칙이 필요하다. 분수 7/156 은 정확하고, 비교 `b.num · a.den == 2 · a.num · b.den` 으로 "2x 는 정확히 두 배"를 오차 없이 검사할 수 있다. 0.001 % 단위 반올림은 `(2·num·100000 + den) / (2·den)` 하나로 끝난다.

**대역폭으로 바꾸면.** 손실 비율 × 피크. DDR4-3200 x64 채널 25 600 MB/s (MEM-B04) 에서 8 Gb 1x 는 25600 × 7/156 = 1148 MB/s, 16 Gb 2x 는 3610 MB/s 를 잃는다 (연습 3). 실제 손실은 이보다 작을 수도(리프레시 동안 어차피 할 일이 없으면) 클 수도(리프레시 앞뒤로 행이 닫혀 row hit 이 사라지면) 있다 — 이 비율은 "막혀 있는 시간"의 상한 모델이다.

**DDR5 는 왜 tREFI 를 3.9 µs 로 줄였나 (대표값, 확인 권장).** DDR5 는 기본 tREFI 가 3.9 µs 이고 16 Gb REFab tRFC1 ≈ 295 ns 다. 손실 295/3900 = 7.564 % 로 DDR4 16 Gb 1x(7.051 %)와 비슷하지만, 한 번 막히는 시간이 550 → 295 ns 로 짧아져 꼬리가 줄어든다. 그리고 same-bank REF(REFsb)는 뱅크 그룹마다 같은 번호의 뱅크만 tRFCsb ≈ 130 ns 동안 막는다 — 그 뱅크 입장에서 130/3900 = 3.333 %, 나머지 뱅크는 계속 일한다.

### Part B — 뱅크 모델과 ref_next_free

**모델 (이 레슨의 정의).** 16 뱅크(DDR4 x8 = 4 BG × 4), 요청마다 close-page 접근 하나: ACT → RD(auto-precharge). 데이터 반환 tRCD + CL + 4 = 22 + 22 + 4 = 48 ck = 30 ns, 뱅크가 다시 ACT 를 받을 수 있을 때까지 tRC = 74 ck = 46.25 ns (MEM-B02 의 22/22/52/74). REF 는 뱅크가 precharge 상태여야 하므로, **시작한 요청이 tRC 안에 끝나지 않으면 시작하지 않는다**. 데이터 버스 경합, tFAW/tRRD(MEM-A01), FR-FCFS 재정렬(MEM-M01), 쓰기는 무시한다. 연기 없는 고정 스케줄이다(연기는 Part C).

**ref_next_free 를 손으로 따라가기** (all-bank 16 Gb: period 7 800 000, dur 550 000, busy 46 250):

| 입력 t (ps) | 창 안? | 다음 창 시작 | t + busy ≤ 다음 창? | 결과 |
|---|---|---|---|---|
| 0 | 예 [0, 550000) → t = 550000 | 7 800 000 | 596 250 ≤ 7.8 M 예 | 550 000 |
| 7 753 750 | 아니오 | 7 800 000 | 7 800 000 ≤ 7 800 000 예 (딱 맞음) | 7 753 750 |
| 7 753 751 | 아니오 | 7 800 000 | 7 800 001 > 아니오 | 7 800 000 + 550 000 = 8 350 000 |
| 7 800 000 | 예 (다음 주기 창) → 8 350 000 | 15 600 000 | 예 | 8 350 000 |

same-bank (period 3.9 µs, dur 130 ns, 4 슬롯)이면 뱅크 1 · 5 · 9 · 13 의 창은 975 ns 에서 시작한다: `ref_next_free(bank 1, 975000) = 1105000`, `bank 2` 는 같은 시각에 그대로 975000.

**정확성.** 결과 s 는 (1) s ≥ t, (2) [s, s + busy) 가 어떤 창과도 겹치지 않고, (3) 그보다 이른 값은 없다. 후보는 "t 자신" 또는 "어떤 창의 끝" 뿐이다 — 겹치는 창이 있으면 그 창 끝 전의 시각은 모두 실패하기 때문. busy ≤ period − dur 이면 창 끝 바로 뒤 후보는 다음 창 전에 반드시 끝난다. 하네스는 창을 하나씩 훑는 느린 구현과 4만 건(4가지 설정, 무작위 bank/t/busy)을 대조해 `mismatches=0` 을 확인한다.

**sim_run 손 계산** (8 Gb all-bank, tRFC 350 ns):

| 요청 | 도착 | bank | ready | start | 지연 | 직접 밀림? |
|---|---|---|---|---|---|---|
| 0 | 0 | 0 | 0 | 350 (창 끝) | 380 | 예 |
| 1 | 0 | 1 | 0 | 350 (all-bank 라 bank 1 도) | 380 | 예 |
| 2 | 400 | 0 | 400 (bank 0 은 396.25 에 빔) | 400 | 30 | 아니오 |
| 3 | 400 | 0 | 446.25 (요청 2 뒤) | 446.25 | 76.25 | 아니오 |
| 4 | 7760 | 2 | 7760 | 8150 (7806.25 > 7800) | 420 | 예 |

(단위 ns.) 리프레시가 없으면 30, 30, 30, 76.25, 30 이다.

### Part B — 실험: 지연 분포 (demo_refresh.c 실제 출력)

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_refresh.c -o demo_refresh && ./demo_refresh   # 0.86 s user
```

시뮬레이션이라 결과는 **결정적**이다 (고정 시드, 기계 부하와 무관 — 실행 당시 load avg 17–20 이었지만 숫자에 영향 없음). 도착은 1 ns 틱마다 확률 1/gap 인 베르누이 과정(이산 포아송), 뱅크 균등. blk% = 한 뱅크가 막혀 있는 시간 비율, pushed% = 리프레시가 시작을 **직접** 민 요청, `>300ns` = 지연이 300 ns 넘은 요청 비율.

```text
== load: mean gap 10 ns, 16 banks, per-bank utilisation rho = 0.289, 2000000 requests, span 20.0 ms ==
mode                           blk% pushed% |     mean    p50     p99   p99.9     max | >100ns >300ns   >1us
no refresh                    0.000  0.000% |    39.19   30.0   113.5   162.5   378.5 |  1.98%  0.00% 0.000%
DDR4  8Gb all-bank 1x         4.487  1.888% |    54.05   30.0   390.2   463.0   650.5 |  8.02%  2.64% 0.000%
DDR4 16Gb all-bank 1x         7.051  2.005% |    72.30   30.0   590.2   663.0   850.5 | 11.54%  6.23% 0.000%
DDR4 16Gb all-bank 2x        14.103  4.009% |   105.41   30.0   618.0   681.2   875.0 | 21.13% 12.46% 0.000%
DDR4 16Gb 8-REF batch         7.051  0.257% |   263.36   30.0  4047.0  4453.0  4627.0 | 11.68%  9.45% 7.887%
DDR5 16Gb REFab               7.564  3.649% |    61.35   30.0   363.0   426.2   620.0 | 12.13%  3.27% 0.000%
DDR5 16Gb REFsb (4 slots)     3.333  2.813% |    45.33   30.0   198.0   259.8   430.2 |  6.28%  0.02% 0.000%
per-bank 130ns (16 slots)     3.333  2.806% |    45.26   30.0   197.0   258.8   442.5 |  6.26%  0.02% 0.000%

== load: mean gap 5 ns, 16 banks, per-bank utilisation rho = 0.578, 2000000 requests, span 10.0 ms ==
mode                           blk% pushed% |     mean    p50     p99   p99.9     max | >100ns >300ns   >1us
no refresh                    0.000  0.000% |    61.03   43.2   221.5   328.2   686.2 | 15.14%  0.18% 0.000%
DDR4  8Gb all-bank 1x         4.487  1.021% |    88.38   52.5   468.0   594.8  1021.2 | 24.32%  5.86% 0.000%
DDR4 16Gb all-bank 1x         7.051  1.026% |   120.55   58.2   667.8   794.0  1221.2 | 29.52% 11.95% 0.002%
DDR4 16Gb all-bank 2x        14.103  2.051% |   180.00   78.0   709.8   834.8  1248.8 | 43.91% 23.69% 0.005%
DDR4 16Gb 8-REF batch         7.051  0.129% |   441.56   57.5  4275.8  4531.2  4887.8 | 29.36% 16.26% 13.452%
DDR5 16Gb REFab               7.564  2.031% |   102.35   60.2   454.5   579.0   993.8 | 30.67%  8.18% 0.000%
DDR5 16Gb REFsb (4 slots)     3.333  1.880% |    73.23   51.0   296.2   421.8   727.5 | 22.14%  0.93% 0.000%
per-bank 130ns (16 slots)     3.333  1.883% |    73.36   51.0   297.2   420.2   853.8 | 22.19%  0.95% 0.000%
```

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">부하 rho = 0.578 에서의 지연 (demo_refresh 실제 출력, 0.45 px = 1 ns)</text>
  <text x="10" y="36" fill="currentColor" font-size="12">막대 = p99, 세로 눈금 = p99.9, 점 = p50. 오른쪽 숫자 = p99 / p99.9 (ns)</text>
  <g fill="currentColor" font-size="12"><text x="10" y="72">no refresh</text><text x="10" y="102">DDR4 8Gb all-bank</text><text x="10" y="132">DDR4 16Gb all-bank</text><text x="10" y="162">DDR4 16Gb all-bank 2x</text><text x="10" y="192">16Gb 8-REF batch</text><text x="10" y="222">DDR5 16Gb REFab</text><text x="10" y="252">DDR5 REFsb (4 slots)</text><text x="10" y="282">per-bank (16 slots)</text></g>
  <g stroke="currentColor">
    <rect x="200" y="60" width="99.7" height="16" class="f-ok"/>
    <rect x="200" y="90" width="210.6" height="16" class="f-acc"/>
    <rect x="200" y="120" width="300.5" height="16" class="f-acc"/>
    <rect x="200" y="150" width="319.4" height="16" class="f-warn"/>
    <rect x="200" y="180" width="405" height="16" class="f-warn"/>
    <rect x="200" y="210" width="204.5" height="16" class="f-acc"/>
    <rect x="200" y="240" width="133.3" height="16" class="f-ok"/>
    <rect x="200" y="270" width="133.7" height="16" class="f-ok"/>
  </g>
  <g stroke="currentColor" stroke-width="2"><line x1="347.7" y1="56" x2="347.7" y2="80"/><line x1="467.7" y1="86" x2="467.7" y2="110"/><line x1="557.3" y1="116" x2="557.3" y2="140"/><line x1="575.7" y1="146" x2="575.7" y2="170"/><line x1="460.6" y1="206" x2="460.6" y2="230"/><line x1="389.8" y1="236" x2="389.8" y2="260"/><line x1="389.1" y1="266" x2="389.1" y2="290"/></g>
  <g fill="currentColor"><circle cx="219.4" cy="68" r="3"/><circle cx="223.6" cy="98" r="3"/><circle cx="226.2" cy="128" r="3"/><circle cx="235.1" cy="158" r="3"/><circle cx="225.9" cy="188" r="3"/><circle cx="227.1" cy="218" r="3"/><circle cx="223" cy="248" r="3"/><circle cx="223" cy="278" r="3"/></g>
  <g fill="currentColor" font-size="12"><text x="612" y="72">221.5 / 328.2</text><text x="612" y="102">468.0 / 594.8</text><text x="612" y="132">667.8 / 794.0</text><text x="612" y="162">709.8 / 834.8</text><text x="612" y="192">4275.8 (잘림) →</text><text x="612" y="222">454.5 / 579.0</text><text x="612" y="252">296.2 / 421.8</text><text x="612" y="282">297.2 / 420.2</text></g>
  <g stroke="currentColor"><line x1="200" y1="300" x2="605" y2="300"/><line x1="200" y1="296" x2="200" y2="304"/><line x1="290" y1="296" x2="290" y2="304"/><line x1="380" y1="296" x2="380" y2="304"/><line x1="470" y1="296" x2="470" y2="304"/><line x1="560" y1="296" x2="560" y2="304"/></g>
  <g fill="currentColor" font-size="12"><text x="196" y="318">0</text><text x="280" y="318">200</text><text x="370" y="318">400</text><text x="460" y="318">600</text><text x="550" y="318">800 ns</text></g>
</svg>
```

**읽는 법 (숫자에서 바로 나오는 것만):**

1. **p50 은 거의 그대로, 꼬리가 뛴다.** 저부하에서 p50 은 모든 방식이 30.0 ns 다. p99 는 113.5 → 390.2(8 Gb) → 590.2(16 Gb) ns. p99 가 "리프레시에 걸린 몇 %" 안에 들어가기 때문이다. 16 Gb 에서 고립된 요청이 걸리는 도착 시각 비율은 (tRFC + busy)/tREFI = 596.25/7800 = **7.64 %** (연습 1 의 전수 결과 596 249 ps / 7.8 µs 와 일치) > 1 % 이므로, p99 는 반드시 그 안에 있다.
2. **평균 증가는 tRFC 의 제곱에 비례한다.** 고립 요청이 균일하게 도착하면 추가 지연 평균은 (D²/2 + busy·D + busy²/2)/P (D = tRFC, P = tREFI). 8 Gb 10.07, 16 Gb 22.79, 16 Gb 2x 45.58, DDR5 REFab 14.93, REFsb 3.98 ns. 저부하 실측 증가분은 14.86, 33.11, 66.22, 22.16, 6.14 ns — 모두 이론의 약 1.5 배다. 차이는 **줄 서기(convoy)**: 리프레시 동안 도착한 요청들이 같은 뱅크에 쌓였다가 하나씩 46.25 ns 씩 빠진다.
3. **pushed% 는 피해 범위를 과소평가한다.** ρ = 0.578, 16 Gb 에서 리프레시가 직접 민 요청은 1.026 % 뿐인데 300 ns 넘은 요청은 11.95 % (리프레시 없음 0.18 %). 줄의 맨 앞만 "밀린 것"으로 세기 때문이다. 리프레시 영향은 "pushed" 가 아니라 **지연 분포 비교**로 재야 한다.
4. **2x 는 손실을 두 배로 만든다.** 16 Gb 고온에서 mean 120.55 → 180.00 ns, 300 ns 초과 11.95 → 23.69 %. 데이터센터에서 DIMM 온도 관리가 성능 문제인 이유다.
5. **same-bank / per-bank 는 꼬리를 자른다.** DDR5 REFab(295 ns) p99 454.5 vs REFsb(130 ns) 296.2 ns, 300 ns 초과 8.18 % vs 0.93 %. 막히는 시간이 짧고, 다른 슬롯의 뱅크는 계속 일한다. 이 모델에서 4 슬롯(REFsb)과 16 슬롯(뱅크 하나씩)의 차이는 거의 없다 — 랜덤 뱅크 트래픽에서는 "한 번에 몇 개 막히나" 보다 "얼마나 오래 막히나"(130 ns)가 지배한다.
6. **몰아서 연기(8-REF batch)는 평균 손실은 같고 꼬리는 최악.** 8개를 4.4 µs 동안 연속으로 하면 blk% 는 7.051 % 로 같지만 1 µs 넘는 요청이 13.45 %, max 4.9 µs. 연기는 "한가할 때" 해야 의미가 있다 (Part C, Q5).

### Part C — 컨트롤러 레지스터: 올림과 내림

bring-up 펌웨어(또는 BIOS/MRC, U-Boot 의 DDR init)는 SPD 나 데이터시트에서 ns 값을 읽어 컨트롤러 레지스터에 **클럭 수**로 쓴다. 두 값의 방향이 반대다.

| 값 | 스펙의 뜻 | 클럭 변환 | DDR4-3200 (625 ps) | DDR4-2400 (833 ps, 대표값) |
|---|---|---|---|---|
| tRFC (8 Gb 350 ns) | REF 후 **최소** 이만큼 기다린다 | 올림 ⌈350000/tCK⌉ | 560 | 420.17 → **421** |
| tREFI (7.8 µs) | REF 평균 간격의 **최대** | 내림 ⌊7800000/tCK⌋ | 12480 | 9363.75 → **9363** |

tREFI 를 올림하면 9364 × 833 = 7 800 212 ps > 7.8 µs — 스펙보다 0.003 % 느리게 리프레시한다. 작아 보이지만 "스펙 위반"이고, 검증 체크리스트에서 걸린다. tRFC 를 내림하면 칩이 아직 리프레시 중인데 ACT 가 들어간다 — 보통은 그냥 동작하는 것처럼 보이다가 고온 · 특정 패턴에서만 데이터가 깨진다. **16 Gb 부품에 8 Gb 값(350 ns)을 넣는 실수**가 같은 증상이다 (DIMM 교체 후 가끔 나는 ECC CE 폭증 → MEM-A06).

### Part C — REF 연기 · 당김: 빚 카운터

컨트롤러는 REF 를 정확히 tREFI 마다 보내지 않아도 된다. DDR4 (JESD79-4 대표값, 확인 권장): **최대 8개까지 연기**할 수 있고, 두 REF 사이 최대 간격은 9 × tREFI. 반대로 **최대 8개를 미리 당겨** 보낼 수 있다. 바쁠 때 미루고 한가할 때 갚는 것이다. 이 규칙을 로그로 검사하려면 간격이 아니라 **누적 빚**을 봐야 한다.

```svg
<svg viewBox="0 0 720 326" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">빚(연기된 REF 수) = due − issued — "T 에 1개, 10T 에 9개 몰아서" 로그</text>
  <line x1="80" y1="260" x2="690" y2="260" stroke="currentColor"/><line x1="80" y1="260" x2="80" y2="50" stroke="currentColor"/>
  <g fill="currentColor" font-size="12"><text x="56" y="264">0</text><text x="56" y="176">4</text><text x="56" y="88">8</text><text x="56" y="66">9</text><text x="12" y="40">빚</text></g>
  <line x1="80" y1="84" x2="690" y2="84" stroke="currentColor" stroke-dasharray="6 4"/>
  <text x="600" y="78" fill="currentColor" font-size="12">한도 8</text>
  <rect x="80" y="50" width="610" height="34" class="f-warn" opacity="0.25"/>
  <text x="440" y="66" fill="currentColor" font-size="12">위반 영역 (빚 ≥ 9)</text>
  <path d="M80 260 H180 V238 H230 V216 H280 V194 H330 V172 H380 V150 H430 V128 H480 V106 H530 V84 H580 L598 260 H690" fill="none" stroke="currentColor" stroke-width="2"/>
  <path d="M580 84 V62" stroke="currentColor" stroke-dasharray="3 3" stroke-width="2"/>
  <text x="604" y="112" fill="currentColor" font-size="12">1 ps 늦으면</text><text x="604" y="128" fill="currentColor" font-size="12">10T 에 빚 9</text>
  <g class="f-acc" stroke="currentColor"><path d="M130 268 L125 278 L135 278 Z"/><path d="M580 268 L575 278 L585 278 Z"/><path d="M630 268 L625 278 L635 278 Z"/><path d="M680 268 L675 278 L685 278 Z"/></g>
  <g fill="currentColor" font-size="12"><text x="118" y="294">REF</text><text x="560" y="294">REF ×9</text><text x="620" y="294">REF</text><text x="670" y="294">REF</text></g>
  <path d="M130 302 L130 307 L580 307 L580 302" fill="none" stroke="currentColor"/>
  <text x="300" y="319" fill="currentColor" font-size="12">REF 간격 9 × tREFI (허용 최대)</text>
  <g fill="currentColor" font-size="12"><text x="126" y="252">T</text><text x="226" y="252">3T</text><text x="326" y="252">5T</text><text x="426" y="252">7T</text><text x="526" y="252">9T</text></g>
</svg>
```

정의: k 번째 REF 의 기한 = k·tREFI, due(x) = ⌊x/tREFI⌋, issued(x) = 시각 ≤ x 인 REF 수. 연기 = due − issued ≤ 8, 당김 = issued − due ≤ 8 이 **모든 x** 에서 성립해야 한다. 연기는 기한 경계에서만 늘어나고 당김은 REF 직후에만 늘어나므로, 그 지점들만 보면 된다. 검증 결과 (solution.c 테스트):

| REF 로그 | 결과 | 최대 연기 / 당김 |
|---|---|---|
| T, 2T, …, 100T (정시) | 위반 0 | 0 / 0 |
| 0, T, …, 99T (한 tREFI 먼저) | 위반 0 | – / 1 |
| T, 그다음 10T 부터 9개 연속 | 위반 0 (간격 9T) | 8 / – |
| T, 그다음 10T + 1 ps 부터 9개 | **위반 1**, 첫 위반 10T | 9 / – |
| 시작에 8개 연속, 9T 부터 정시 | 위반 0 | 0 / 8 |
| 시작에 9개 연속 | **위반 1**, 첫 위반 = 9번째 REF | – / 9 |
| 9T, 17T, 25T, … 마다 8개씩 몰아서 | 위반 0 | 8 / – |
| 1.5T 마다 하나 (간격은 늘 1.5T) | **위반**, 첫 위반 25T | 빚이 계속 쌓임 |
| REF 없음, 20T 까지 | 위반 12 (9T…20T) | – |

마지막에서 두 번째 줄이 핵심이다: "REF 간격 ≤ 9 tREFI" 만 검사하는 체크러는 이 로그를 통과시키지만, 실제로는 25T 에 빚이 9 가 되고 결국 64 ms 안에 모든 행을 리프레시하지 못한다.

### Part C — 왜 밀도가 오를수록 비싸지나 (리텐션 산수)

REF 명령 수는 리텐션 창마다 8192 번으로 고정이다. 그러면 REF 한 번이 맡는 행 수 = (뱅크당 행 수) / 8192:

| DDR4 x8 | 뱅크당 행 | REF 한 번에 뱅크당 행 | × 16 뱅크 | tRFC1 (대표값) |
|---|---|---|---|---|
| 4 Gb | 32 K | 4 | 64 행 | 260 ns |
| 8 Gb | 64 K | 8 | 128 행 | 350 ns |
| 16 Gb | 128 K | 16 | 256 행 | 550 ns |

행 수는 두 배씩 느는데 tRFC 는 1.35 배, 1.57 배로 덜 는다 — 칩 안에서 여러 행을 동시에 활성화하기 때문이다. 하지만 동시에 켤 수 있는 행 수는 순간 전류(IDD5B, 대표값)와 전원망이 제한하므로 끝없이 병렬화할 수 없다. 그래서 밀도가 오르면 tRFC 가 오르고, 손실 tRFC/tREFI 가 오른다. 셀이 작아질수록 리텐션도 나빠지는 경향이 있어 tREFI 를 늘릴 수도 없다. DDR5 가 tREFI 를 3.9 µs 로 줄이고 same-bank REF 를 넣은 것, LPDDR 이 처음부터 per-bank REF 를 둔 것이 이 압력에 대한 답이다.

리텐션과 tREFI: 64 ms / 8192 = 7.8125 µs (스펙 표기는 7.8 µs). 컨트롤러가 tREFI_ck = 12480 (DDR4-3200) 이면 한 행이 다시 리프레시되기까지 12480 × 625 ps × 8192 = 63.90 ms ≤ 64 ms. 9 µs 로 잘못 쓰면 73.73 ms — 약한 셀이 64 ms 근처에서 새면 고온에서만 비트가 뒤집힌다 (연습 4). 고온(85–95 °C)에서는 리텐션이 32 ms 로 보장되므로 2x 가 필요하다.

### 복잡도와 실행 시간

`ref_next_free` O(1), `sim_run` O(n), `lat_stats` O(n log n)(qsort), `ref_trace_check` O(n + horizon/tREFI). solution.c 전체(무작위 대조 4만 건 + 20만 요청 × 5 방식) 실행 0.2 s 미만, demo 200만 요청 × 8 방식 × 2 부하 0.86 s user. 모델 메모리: 뱅크당 `bank_free` 8 B — 실제 컨트롤러도 뱅크당 타이머 몇 개와 랭크당 REF 빚 카운터(4비트면 충분) 정도다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
# ref_next_free vs slow scan: 40000 cases, mismatches=0
# 79/79 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan
# 79/79 passed (ASan/UBSan 경고 없음)
```

## 7. 자주 하는 실수

**1. ns 를 double 로 나누고 ceil.** `ceil(350.0 / 0.833)` 은 맞아 보여도, 13.75 / 0.625 처럼 딱 나눠떨어져야 하는 값이 부동소수 오차로 22.0000001 이 되면 23 이 된다. → 모든 시간을 정수 ps 로: `(ps + tck − 1) / tck`.

**2. tREFI 도 올림.** "타이밍은 다 올림" 이라고 외우면 틀린다. tREFI 는 최대값이다 → 내림. DDR4-2400 에서 올림하면 9364 ck = 7.8002 µs 로 스펙 초과.

```c
out->trfc_ck  = (uint32_t)((rfc + tck_ps - 1) / tck_ps);   /* 최소 → 올림 */
out->trefi_ck = (uint32_t)(refi / tck_ps);                 /* 최대 → 내림 */
```

**3. 요청이 REF 창으로 넘어 들어가게 둔다.** 시작 시각만 창 밖인지 보면 `t = 7753751` 요청이 7.8 µs 의 REF 와 겹친다. REF 는 모든 뱅크가 precharge 된 상태에서만 나갈 수 있으므로 `[s, s + tRC)` 전체가 창 밖이어야 한다. 이걸 빼먹으면 리프레시 비용을 과소평가한다.

**4. 32비트 오버플로.** `uint32_t` 로 tREFI(ps) × 8192 = 6.39e10 → 넘친다. 리텐션 계산과 누적 시간은 `int64_t`.

**5. "pushed" 를 영향받은 요청 수로 보고.** 실험에서 직접 밀린 요청은 1.0 % 인데 300 ns 넘은 요청은 12 % 다. 리프레시 뒤에 줄 선 요청까지 세려면 **리프레시 없는 실행과 같은 요청 스트림으로 지연을 비교**해야 한다 (하네스의 `g_lat1[i] >= g_lat0[i]` 비교가 그 방법).

**6. 백분위 인덱스 하나 차이.** `sorted[n * 99 / 100]` 은 n = 100 에서 sorted[99] = 최댓값을 p99 로 낸다. 최근접 순위는 `idx = ceil(p·n) − 1` → n = 100 이면 sorted[98]. 정의를 하나 정하고 테스트(1..1000 → p99 = 990)로 고정한다.

**7. 연기 규칙을 간격으로만 검사.** "두 REF 사이 ≤ 9 tREFI" 만 보면 1.5T 마다 하나씩 보내는 로그(빚이 무한히 쌓임)를 통과시킨다. 누적 빚(due − issued)을 봐야 한다.

## 8. 면접 Q&A

<details><summary>Q1. DRAM 은 왜 리프레시가 필요하고, 컨트롤러는 행 주소를 어떻게 관리하나요?</summary>

셀이 커패시터 하나 + 트랜지스터 하나라 전하가 접합 누설 등으로 샌다. 데이터시트는 모든 셀이 최소 64 ms(≤ 85 °C, 대표값) 버틴다고 보장하고, 그 안에 모든 행을 다시 활성화(ACT 하면 센스 앰프가 행 전체를 읽어 다시 쓴다)해야 한다. 컨트롤러는 64 ms 동안 REF 를 8192 번 보내기만 하고, 어느 행을 리프레시할지는 DRAM 내부 카운터가 정한다 (auto refresh). 전원 절약 상태에서는 DRAM 이 스스로 타이머로 리프레시하는 self-refresh 를 쓴다 (MEM-M08). 그래서 tREFI = 64 ms / 8192 ≈ 7.8 µs.

</details>

<details><summary>Q2. 16 Gb DDR4 에서 리프레시가 대역폭을 얼마나 먹고, 왜 세대마다 나빠지죠?</summary>

tRFC1 550 ns / tREFI 7.8 µs = 11/156 = 7.05 %, 85 °C 넘으면 14.1 %. 25.6 GB/s 채널이면 1.8 / 3.6 GB/s 다. 나빠지는 이유: REF 수(8192)는 고정이라 밀도가 2배면 REF 하나가 리프레시할 행이 2배(8 Gb 뱅크당 8행 → 16 Gb 16행). 칩 안에서 병렬로 처리하지만 순간 전류 한계 때문에 tRFC 가 260 → 350 → 550 ns 로 오른다. 셀이 작아져 리텐션 여유도 줄어 tREFI 를 늘릴 수 없다. 그래서 DDR5 는 tREFI 3.9 µs + 짧은 tRFC + same-bank REF, 연구 쪽은 리텐션을 아는 리프레시(RAIDR 등)로 간다.

</details>

<details><summary>Q3. 리프레시는 평균 지연은 조금 올리는데 p99 는 크게 올립니다. 왜죠?</summary>

요청의 대부분은 리프레시와 안 겹친다 → p50 은 그대로(실측 30.0 ns 고정). 겹치는 비율은 (tRFC + tRC)/tREFI ≈ 7.6 %(16 Gb) 이고 그 요청들은 최대 tRFC + tRC 만큼 기다린다. 7.6 % > 1 % 이므로 p99 는 그 집단 안에 떨어진다 → 113 ns → 590 ns. 평균 증가는 (D²/2 + …)/P 로 23 ns 정도. 그래서 리프레시는 "대역폭 몇 %" 로 말하면 작아 보이고, 실시간 · 저지연 시스템(오디오 DMA, 네트워크 패킷, 로봇 제어 루프)에서는 꼬리 지연 문제로 나타난다. 측정도 평균이 아니라 분포로 해야 한다.

</details>

<details><summary>Q4. all-bank 와 per-bank(same-bank) 리프레시의 차이는? 컨트롤러는 뭘 더 해야 하나요?</summary>

all-bank 는 랭크의 모든 뱅크를 tRFC 동안 막는다 — 단순하지만 그동안 그 랭크는 아무것도 못 한다. per-bank(LPDDR4/5 REFpb)나 same-bank(DDR5 REFsb, 뱅크 그룹마다 같은 번호 뱅크)는 일부 뱅크만 더 짧게(DDR5 16 Gb 대표값 130 ns) 막고 나머지는 계속 서비스한다. 실험에서 p99 454.5 → 296.2 ns, 300 ns 초과 8.2 % → 0.9 %. 대가: REF 명령 수가 늘고(커맨드 버스), 컨트롤러가 뱅크별 리프레시 빚을 따로 관리해야 하며, 리프레시 중인 뱅크로 가는 요청을 뒤로 미루고 다른 뱅크 요청을 먼저 내보내는 스케줄러(MEM-M01 FR-FCFS 확장)가 있어야 이득이 커진다. 리프레시할 뱅크를 "요청이 없는 뱅크" 로 고르는 것도 컨트롤러 몫이다.

</details>

<details><summary>Q5. REF 연기(postpone)는 언제 쓰고, 무엇이 위험한가요?</summary>

DDR4 는 최대 8개 연기, 8개 당김을 허용한다 (대표값, JESD79-4 확인). 바쁜 구간에는 미루고 큐가 비면 몰아서 갚으면 평균 손실은 같지만 요청과 덜 부딪힌다. 위험: (1) 몰아서 갚을 때 8 × tRFC = 4.4 µs(16 Gb) 동안 막힌다 — 실험의 "8-REF batch" 는 1 µs 초과 요청 13.5 %, max 4.9 µs. 바쁠 때 빚이 8 에 닿으면 어차피 강제로 갚아야 하므로 꼬리가 더 나빠질 수 있다. (2) 빚 계산이 틀리면 64 ms 리텐션을 어긴다 — 테스트 장비로만 잡히는 고온 데이터 깨짐. 그래서 빚 카운터 기반 검사(Part C)를 검증에 넣는다.

</details>

<details><summary>Q6. 85 °C 를 넘으면 무슨 일이 일어나고, 펌웨어는 뭘 하나요?</summary>

누설이 커져 보장 리텐션이 32 ms 로 줄어든다(대표값) → 리프레시 2배(tREFI 3.9 µs). 온도 소스는 DIMM 의 온도 센서(TS)나 LPDDR 의 모드 레지스터(MR4 의 리프레시 배율 필드 — 데이터시트 확인)다. 펌웨어/컨트롤러는 온도를 주기적으로 읽어 2x 로 전환하는데, 85 °C 근처에서 센서가 떨리면 모드가 계속 바뀌므로 **히스테리시스**(예: 83 °C 이상 2x, 78 °C 이하 1x, 가드 밴드는 센서 정확도로 정함)를 둔다. 95 °C 초과면 스펙 밖이므로 대역폭 쓰로틀 · 경보. 연습 5 에서 84/86 °C 로 떨리는 센서에 히스테리시스 4번 vs 단순 비교 103번 전환을 확인한다.

</details>

<details><summary>Q7. bring-up 에서 tRFC 를 잘못 넣으면 어떤 증상이 나오나요?</summary>

너무 작으면(예: 16 Gb 부품에 8 Gb 값 350 ns) REF 가 끝나기 전에 ACT 가 들어가 행이 깨질 수 있다. 증상은 결정적이지 않다 — 메모리 테스트 패턴(MEM-B08/B09)은 대부분 통과하고, 고온 · 고부하 · 특정 접근 패턴에서만 ECC CE 가 늘거나 UE 가 난다. 그래서 체크 순서는 (1) SPD/데이터시트의 tRFC1/tRFC2 값과 레지스터 덤프 대조(ck 변환 방향 포함), (2) 고온 챔버 + 스트레스 memtest, (3) EDAC CE 카운트 추이(MEM-A06, SIV-M14). 너무 크게 넣으면 동작은 안전하지만 성능 손실이 늘어난다 — 검증 리포트에 "보수적 설정" 으로 남긴다.

</details>

<details><summary>Q8. 리프레시 비용을 더 줄이는 방법에는 뭐가 있나요?</summary>

(1) FGR(DDR4 fine granularity refresh, 2x/4x 모드): 더 자주 · 더 짧게 — 한 번 막히는 시간은 줄지만 총 손실은 오히려 는다 (연습 2: 8 Gb 4.49 → 6.67 → 8.21 %, 대표값). 꼬리를 줄이는 대가다. (2) per-bank/same-bank REF + 스케줄러. (3) 연기/당김으로 유휴 시간에 몰기. (4) 리텐션 인지 리프레시 — 대부분의 행은 64 ms 보다 훨씬 오래 버티므로 약한 행만 자주(연구: RAIDR, ISCA 2012). 다만 리텐션은 온도와 시간에 따라 변하고(VRT, variable retention time) 패턴에 의존해 프로파일링이 어렵다. (5) 리프레시 슬롯은 Row Hammer 완화(TRR)에도 쓰이므로 줄이는 것과 충돌한다 (MEM-A02).

</details>

## 9. 추가 연습

### 연습 1 — 고립 요청의 최악 지연 (전수)

한가한 뱅크에 요청 하나가 [0, tREFI) 의 모든 ps 에 도착한다고 보고 `ref_next_free` 로 전수 조사하라. 리프레시 때문에 밀리는 도착 시각 수와 최악 지연, 그 도착 시각을 구하라 (all-bank 16 Gb, DDR5 same-bank 뱅크 1).

<details><summary>답 보기</summary>

실측: all-bank 16 Gb 는 밀리는 도착 596 249 ps / 7 800 000 = D + busy − 1 개, 최악 지연 626 249 ps (= D + busy − 1 + 30 ns) 이고 도착 t = 7 753 751 ps (창 시작 − busy + 1). same-bank 는 176 249 개, 최악 206 249 ps. 이유: 도착 t 가 (W − busy, W + D) 안이면 밀린다 — 열린 구간 길이 D + busy 에서 정수점 D + busy − 1 개. 최악은 "딱 1 ps 늦어서 창 끝까지 기다리는" 경우다. 비율 (D + busy)/P = 7.64 % 가 5절 "p99 가 반드시 리프레시 집단 안" 논리의 근거다.

</details>

### 연습 2 — DDR4 FGR 1x/2x/4x

FGR 모드 m(1, 2, 4)은 tREFI 를 7.8/m µs 로 줄이고 tRFC 를 tRFC1/2/4 로 쓴다. 대표값 (4 Gb 260/160/110, 8 Gb 350/260/160, 16 Gb 550/350/260 ns) 으로 손실을 구하고, 모드가 커지면 총 손실이 어떻게 되는지 확인하라.

<details><summary>답 보기</summary>

8 Gb: 7/156 = 4.487 %, 1/15 = 6.667 %, 16/195 = 8.205 %. 16 Gb 4x: 2/15 = 13.333 %. 모든 밀도에서 1x &lt; 2x &lt; 4x (테스트로 확인). tRFC 가 모드 배율만큼 줄지 않기 때문이다 (REF 하나의 고정 비용 — 명령 처리, 내부 준비 — 이 있다). FGR 은 평균 손실을 늘리고 한 번 막히는 시간을 줄여 꼬리를 줄이는 선택이다. 수치는 대표값 — 부품 데이터시트 확인.

</details>

### 연습 3 — 잃는 대역폭

`lost_mbps(peak, frac)` = ⌊peak × num / den⌋. DDR4-3200 x64 (25 600 MB/s) 의 8 Gb 1x, 16 Gb 2x, DDR5-4800 DIMM (38 400 MB/s, MEM-B04) 의 16 Gb REFab 를 계산하라.

<details><summary>답 보기</summary>

1148, 3610, 2904 MB/s. DDR5 는 tREFI 가 3.9 µs 라 비율(7.56 %)이 DDR4 16 Gb 1x(7.05 %) 보다 약간 크다. "막혀 있는 시간 × 피크" 는 상한 모델이다 — 실제 스트림 측정(MEM-B11)에서 리프레시 몫만 떼어내기는 어렵다.

</details>

### 연습 4 — 리텐션 산수와 잘못된 tREFI 레지스터

(a) 64 ms / 8192, 32 ms / 8192 를 ps 로. (b) 4/8/16 Gb x8 (뱅크당 32 K/64 K/128 K 행)에서 REF 한 번에 뱅크당 몇 행인가. (c) 레지스터 tREFI_ck 와 tCK 로 "한 행이 다시 리프레시될 때까지" = tREFI_ck × tCK × 8192 를 계산해 12480 ck 와, 9 µs 로 잘못 쓴 14400 ck 를 64 ms 와 비교하라.

<details><summary>답 보기</summary>

(a) 7 812 500 ps, 3 906 250 ps. (b) 4, 8, 16 행. (c) 12480 → 63 897 600 000 ps = 63.90 ms ≤ 64 ms OK. 14400 → 73.728 ms > 64 ms 위반. 12480 을 고온에서 그대로 두면 63.9 ms > 32 ms 도 위반이다 — 2x 전환을 빠뜨리는 버그. 모두 `int64_t` (6.4e10 은 32비트 밖).

</details>

### 연습 5 — 온도 히스테리시스

상태 1X / 2X / OVERTEMP. 1X → 2X: T ≥ 83 °C, 2X → 1X: T ≤ 78 °C, T > 95 °C → OVERTEMP (2x 유지 + 경보), OVERTEMP → 2X: T ≤ 93 °C. 60 → 98 °C 상승, 84/86 °C 100번 떨림, 86 → 57 °C 하강 시퀀스에서 (a) 모든 시점에 "T &gt; 85 인데 1X" 가 없는지, (b) 전환 횟수를 단순 비교(T > 85 이면 2x)와 비교하라.

<details><summary>답 보기</summary>

히스테리시스 4번(1X→2X, →OVERTEMP, →2X, →1X), 단순 비교 103번. 안전 조건 위반 0. 83 °C 로 미리 올리는 이유는 센서 오차(±1–2 °C, 대표값)와 샘플링 주기 동안의 온도 상승 때문이다. 내릴 때 78 °C 까지 기다리는 것은 성능 손실(2x 동안 손실 2배)보다 모드 진동 · 레지스터 쓰기 폭주가 더 나쁘기 때문이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 28/28 passed
```

## 10. 핵심 정리

- tREFI = 리텐션 / 8192 ≈ 7.8 µs (85 °C 초과 3.9 µs), tRFC = REF 하나가 랭크를 막는 시간 (DDR4 4/8/16 Gb: 260/350/550 ns, 대표값).
- 손실 = tRFC / tREFI: 8 Gb 7/156 = 4.487 %, 16 Gb 11/156 = 7.051 %, 16 Gb 고온 11/78 = 14.103 %. 25.6 GB/s 채널이면 1.1–3.6 GB/s.
- 밀도 2배 → REF 하나가 맡는 행 2배 → tRFC 증가 → 손실 증가. 그래서 DDR5 는 3.9 µs + same-bank REF, LPDDR 은 per-bank REF.
- 리프레시는 p50 이 아니라 꼬리를 친다: 겹치는 비율 (tRFC + tRC)/tREFI ≈ 7.6 % → p99 113 → 590 ns. 직접 밀린 요청(1 %)보다 줄 선 요청이 더 많다.
- same-bank/per-bank 는 짧게 · 일부만 막아 p99 를 줄인다 (454 → 296 ns). 몰아서 연기(8 × tRFC)는 평균은 같고 꼬리는 최악 (max 4.9 µs).
- ns → ck: tRFC(최소) 올림, tREFI(최대) 내림. 정수 ps 로 계산.
- DDR4 연기 · 당김 최대 8 (간격 ≤ 9 tREFI) — 검사는 간격이 아니라 누적 빚(due − issued)으로.

## 참고자료

- JEDEC JESD79-4 DDR4 SDRAM Standard — tREFI, tRFC1/2/4, REF postpone/pull-in, 고온 2x 리프레시 (유료 문서, 이 레슨의 수치는 대표값 — 부품 데이터시트로 확인): <https://www.jedec.org/>
- JEDEC JESD79-5 DDR5 SDRAM Standard — REFab / REFsb, tRFC1 · tRFCsb (대표값만 인용).
- Micron DDR4 SDRAM 데이터시트 (예: 8 Gb x8) — tRFC 표, 리프레시 명령 · 온도 범위 절. 세부 절 번호는 문서 확인.
- U. Drepper, "What Every Programmer Should Know About Memory" (2007), 2장 DRAM: <https://people.freebsd.org/~lstewart/articles/cpumemory.pdf>
- B. Jacob, S. Ng, D. Wang, *Memory Systems: Cache, DRAM, Disk* — 리프레시와 컨트롤러 스케줄링 장.
- J. Liu, B. Jaiyen, R. Veras, O. Mutlu, "RAIDR: Retention-Aware Intelligent DRAM Refresh", ISCA 2012 — 밀도에 따른 리프레시 비용 증가와 리텐션 인지 리프레시.
- J. Liu et al., "An Experimental Study of Data Retention Behavior in Modern DRAM Devices", ISCA 2013 — VRT, 패턴 의존성.
- J. Stuecheli, D. Kaseridis, H. Hunter, L. John, "Elastic Refresh: Techniques to Mitigate Refresh Penalties in High Density Memory", MICRO 2010 — 연기를 이용한 스케줄링.
- Ramulator 2.0 (리프레시 모델 포함 DRAM 시뮬레이터): <https://github.com/CMU-SAFARI/ramulator2>
- 관련 레슨: MEM-B02 (명령 · 타이밍, ns → ck), MEM-B03 (row hit/empty/conflict 지연), MEM-B04 (대역폭 계산), MEM-B08 · MEM-B09 (메모리 테스트), MEM-M01 (스케줄링), MEM-M08 (self-refresh), MEM-A01 (cycle-accurate 모델), MEM-A02 (Row Hammer · TRR), MEM-A06 (ECC RAS · CE/UE), SIV-M14 (ECC 오류 주입), ARCH-B10 (AMAT)
