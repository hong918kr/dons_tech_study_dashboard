# L3 · RFC 2544 Throughput Search for an Ethernet/IO Path

> **ID**: L3 · **Priority**: P0 · **Pattern**: line-rate math + binary search with confirmation + trial budget + loss-curve stop rule · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh L3` / `./run.sh L3 sol`

## 0. 문제 한국어로 먼저 보기

> 영어 원문(§1)을 읽기 전에 **무슨 상황이고 뭘 만들어야 하는지**만 빠르게 잡는 요약입니다. 풀이 힌트는 없습니다.

**한 줄 요약**: Ethernet/IO 경로의 **benchmark tool**을 짭니다. 선로가 낼 수 있는 최대 frame rate(**line rate**)를 계산하고, RFC 2544 방식으로 **frame을 하나도 잃지 않는 최대 속도(throughput)**를 찾고, 부하별 **frame loss 곡선**을 그립니다.

**상황**
- traffic generator가 DUT(device under test)에 정해진 속도로 frame을 **일정 시간(trial)** 동안 보내고, 몇 개가 돌아왔는지 셉니다. `받은 수 == 보낸 수`일 때만 **zero loss**입니다.
- 코드는 DUT를 **trial callback** `trial(ctx, rate_fps, frame_len, n_frames) → frames_received` 하나로만 봅니다. DUT는 `stub.c` 아래쪽 **mock**입니다.
  - mock은 초당 처리 가능한 frame 수(`pps_max`, per-packet 처리 한계)가 있어서, 그보다 빨리 보내면 일부를 버립니다. 그래서 **작은 frame은 loss**가 나고 **큰 frame은 line rate**까지 갑니다.
  - 특정 trial 하나에 **일시적 이벤트**를 넣을 수 있습니다: `FLAKY_BOOST`(그 trial만 용량을 넘어도 안 잃음) / `FLAKY_DROP`(그 trial만 frame 1개를 잃음, 예: bit error).
- 선로 위의 frame 하나는 frame 자체(`frame_len`, FCS 포함, 64..9216 B) 말고도 **20 B**를 더 씁니다: preamble 7 + SFD 1 + IFG(inter-frame gap) 12.
- 속도 단위: **`pct_x100`** = line rate의 0.01 % 단위 정수(0..10000, 10000 = 100 %).
  - `rate_fps = line_fps * pct_x100 / 10000` (내림)
  - trial당 frame 수 `n = rate_fps * trial_ms / 1000` (내림, 최소 1)

**구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `line_rate_fps(link_bps, frame_len)` | line rate에서 초당 최대 frame 수 | frame당 선로 비용 = `frame_len + 20` 바이트. **내림**. `link_bps == 0`이나 `frame_len`이 64..9216 밖이면 `0` |
| `rfc2544_throughput(fn, ctx, link_bps, frame_len, cfg, out)` | RFC 2544 §26.1 throughput | ① 100 %에서 trial. zero loss면 후보 = 100 %. ② 아니면 이진 탐색: `lo = 0`(pass), `hi = 10000`(fail), `hi − lo > res_x100`인 동안 `mid = lo + (hi − lo) / 2`로 trial, zero loss면 `lo = mid`, 아니면 `hi = mid`. 후보 = `lo`. ③ **확인 trial**: 후보에서 한 번 더. zero loss면 끝. loss면 `hi = 후보`, `lo = 0`으로 놓고 ②부터 다시. 후보가 0이면(아무것도 통과 못 함) 확인 없이 끝. trial은 전부 합쳐 **최대 `max_trials`번**, 넘으면 `-ETIMEDOUT`. 결과: `rate_fps`, `pct_x100`, `trials`(확인 포함) |
| `rfc2544_loss_curve(fn, ctx, link_bps, frame_len, trial_ms, pts, cap)` | RFC 2544 §26.3 frame loss rate | 100 %, 90 %, …, 10 %로 10 %씩 내려가며 trial. 단계마다 `LossPoint`(`pct_x100`, `rate_fps`, `sent`, `lost`, `loss_x100 = lost * 10000 / sent` 내림) 기록. **연속 두 단계 zero loss**면 멈춤(10 %까지 가면 거기서 끝). 점 개수 반환. `pts`가 모자라면 `-ENOSPC` |

잘못된 인자(NULL callback/포인터, 잘못된 `frame_len`·`link_bps`, `res_x100`이 0이거나 10000 초과, `max_trials == 0`, `trial_ms == 0`, `cap < 1`)는 `-EINVAL`이고 trial을 하나도 돌리지 않습니다.

**예시를 말로 풀면**
- **line rate**: 10 GbE, 64 B frame 하나는 선로에서 64 + 20 = 84 B = 672 bit → `10^10 / 672` = **14,880,952 fps**. 1518 B면 1538 B = 12,304 bit → **812,743 fps**.
- **line rate를 그대로 내는 DUT**: 100 %에서 통과, 확인 trial도 통과 → **100 %, trial 2번**.
- **9.1 Mfps에서 막히는 경로, 64 B, 해상도 0.1 %**: 100 % 실패 → 50 % 통과 → 75 % 실패 → … → 61.12 % 통과, 61.22 % 실패로 간격이 0.1 %가 되면 멈춤 → 61.12 %에서 확인 통과 → **61.12 % = 9,095,237 fps, trial 12번**. 같은 경로에 1518 B를 보내면 100 %.
- **3번째 trial(75 %)이 `FLAKY_BOOST`**: 75 %가 거짓 통과해서 탐색이 75 % 근처로 갑니다. 확인 trial에서 75 %가 loss → 0..75 %에서 다시 탐색 → **61.14 %, trial 23번**. 예산이 20번이면 `-ETIMEDOUT`.
- **loss 곡선, 같은 9.1 Mfps 경로 64 B**: 100 % → 38.84 %, 90 % → 32.05 %, 80 % → 23.55 %, 70 % → 12.63 %, 60 % → 0, 50 % → 0 → 두 번 연속 0이라 **점 6개**.

**지켜야 할 조건**
- C11, 정수 연산만(float 금지), `malloc` 금지. 결과는 호출자가 준 구조체/배열에.
- 모든 계산은 `uint64_t`로 overflow 없이 (`link_bps`는 최대 수백 Gb/s, trial은 60 s 이상일 수 있음).
- mock DUT는 결정적입니다. 같은 입력이면 같은 결과. trial 번호는 DUT가 직접 셉니다.

## 1. Problem

You are writing the **throughput benchmark** for an Ethernet / IO path on a new SoC. A traffic generator offers frames to the device under test (DUT) at a chosen rate for a fixed trial time and counts how many come back. Implement the line-rate math, the **RFC 2544 §26.1 throughput search** (highest rate with **zero frame loss**), and the **RFC 2544 §26.3 frame-loss-rate curve**.

Your code sees the DUT only through a trial callback. The DUT is a **mock** at the bottom of `stub.c`: it forwards at most `pps_max` frames/s (per-packet processing limit), so small frames lose and large frames reach line rate. It can inject one transient event at a chosen trial index: `FLAKY_BOOST` (that trial loses nothing even above capacity) or `FLAKY_DROP` (that trial loses exactly one frame, e.g. a bit error).

```c
typedef uint64_t (*trial_fn)(void *ctx, uint64_t rate_fps, uint32_t frame_len, uint64_t n_frames);
                       /* returns frames received; zero loss <=> received == n_frames */

typedef struct { uint32_t res_x100, max_trials, trial_ms; } TputCfg;
typedef struct { uint64_t rate_fps; uint32_t pct_x100; uint32_t trials; } TputResult;
typedef struct { uint32_t pct_x100; uint64_t rate_fps, sent, lost; uint32_t loss_x100; } LossPoint;

uint64_t line_rate_fps(uint64_t link_bps, uint32_t frame_len);
int rfc2544_throughput(trial_fn fn, void *ctx, uint64_t link_bps, uint32_t frame_len,
                       const TputCfg *cfg, TputResult *out);               /* 0, -EINVAL, -ETIMEDOUT */
int rfc2544_loss_curve(trial_fn fn, void *ctx, uint64_t link_bps, uint32_t frame_len,
                       uint32_t trial_ms, LossPoint *pts, int cap);        /* #points, -ENOSPC, -EINVAL */
```

Units: rates are searched in **`pct_x100`** = 0.01 % of line rate (0..10000).
`rate_fps = line_fps * pct_x100 / 10000` (floor); frames per trial `n = rate_fps * trial_ms / 1000` (floor, at least 1).

Requirements:

- **`line_rate_fps`**: `frame_len` includes the FCS, valid 64..9216. Each frame also costs **20 bytes on the wire** (preamble 7 + SFD 1 + IFG 12). Result = `link_bps / ((frame_len + 20) * 8)`, **floor**. Bad args → `0`.
- **`rfc2544_throughput`**:
  1. Trial at 100 %. Zero loss → candidate = 100 %.
  2. Otherwise binary search with `lo = 0` (pass), `hi = 10000` (fail): while `hi − lo > res_x100`, `mid = lo + (hi − lo) / 2`, run a trial, zero loss → `lo = mid`, else `hi = mid`. Candidate = `lo`.
  3. **Confirm**: one more trial at the candidate. Zero loss → done. Loss → `hi = candidate`, `lo = 0`, back to step 2. A candidate of 0 (nothing passed) is returned without a confirmation.
  - At most `max_trials` trials in total; when the budget runs out → `-ETIMEDOUT` with `out->trials` set and the rate fields 0.
  - On success fill `rate_fps`, `pct_x100`, `trials` (including confirmations).
- **`rfc2544_loss_curve`**: trials at 100 %, 90 %, …, 10 % of line rate. One `LossPoint` per step, `lost = sent − received` (0 if received ≥ sent), `loss_x100 = lost * 10000 / sent` (floor). Stop after **two consecutive steps with zero lost frames**, or after the 10 % step. Return the number of points; `-ENOSPC` if `cap` is too small.
- Bad arguments → `-EINVAL` without running any trial.

### Example

```
line_rate_fps(10 GbE,   64) = 10^10 / (84 * 8)   = 14,880,952 fps   (14,880,952.38 floored)
line_rate_fps(10 GbE, 1518) = 10^10 / (1538 * 8) =    812,743 fps

cfg = { res_x100 = 10 (0.1 %), max_trials = 32, trial_ms = 60000 }
DUT at line rate, 64 B          -> 100.00 %, 14,880,952 fps, trials 2   (100 % + confirm)
DUT capped at 9.1 Mfps, 64 B    ->  61.12 %,  9,095,237 fps, trials 12
same DUT, 1518 B                -> 100.00 %, trials 2
same DUT, FLAKY_BOOST at trial 3 (75 % falsely passes)
                                ->  61.14 %,  9,098,214 fps, trials 23
                                    with max_trials = 20 -> -ETIMEDOUT, trials 20

loss curve, 9.1 Mfps DUT, 64 B:
  100 % 38.84 %  |  90 % 32.05 %  |  80 % 23.55 %  |  70 % 12.63 %  |  60 % 0  |  50 % 0  -> 6 points
```

### Constraints

- C11, integer math only (no float), no malloc. Everything in `uint64_t` without overflow (`link_bps` up to hundreds of Gb/s, trials of 60 s or more).
- The mock DUT is deterministic and counts its own trials (1-based).

### Clarifying questions to ask (first 3 minutes)

1. Is `frame_len` with or without the FCS? Does "line rate" include preamble and IFG? (RFC 2544 uses frame sizes with FCS; the wire adds 20 B.)
2. What resolution do you want for throughput — absolute fps or % of line rate — and is there a time budget for the whole run?
3. Is "zero loss" strictly zero frames, or is there an acceptance threshold? Do duplicated / extra frames count as a failure?
4. Trial duration: the RFC says at least 60 s. Can I use shorter trials to search and a long trial only to confirm?
5. Are both ports on one clock, or can the tester and DUT differ by up to ±100 ppm?
6. Is the path back-pressured (PCIe/AXI) or does it drop on overflow (Ethernet, camera)?

## 2. Your workspace

`./run.sh L3`로 바로 테스트하세요. 테스트 9묶음(CHECK 54개)이 있고, 파일 아래쪽 mock DUT는 건드리지 않습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 탐색 변수는 fps가 아니라 pct_x100</summary>

탐색은 0..10000 정수 범위에서 하고, trial 직전에만 `rate = line * pct / 10000`으로 바꿉니다. 해상도가 frame 크기와 상관없이 같은 의미(0.1 % = 10)가 되고, 결과를 % of line rate로 바로 보고할 수 있습니다. `rate`가 `line`보다 커지는 일도 없습니다(내림).

</details>

<details>
<summary>Hint 2 — 탐색 + 확인을 한 루프로</summary>

`run_trial()` 헬퍼 하나에 "예산 검사 → `trials++` → rate/n 계산 → `fn() == n`"을 몰아넣으면 예산 초과 처리가 한 군데로 모입니다. 바깥 루프는 `for (;;) { 필요하면 이진 탐색; 후보 확인; 통과면 break; 아니면 hi = 후보, lo = 0 }` 모양입니다. 100 %가 바로 통과한 경우는 "탐색은 건너뛰고 확인만" 하는 같은 루프입니다.

</details>

<details>
<summary>Hint 3 — "zero loss"는 반올림한 %가 아니라 frame 개수로</summary>

4억 개 중 1개를 잃으면 `loss_x100`은 0으로 내림됩니다. 그래도 그 단계는 zero loss가 **아닙니다**. 연속 카운터(`zero_run`)는 `lost == 0`으로만 올리고, 하나라도 잃으면 0으로 되돌립니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — line rate는 선로 위 비트로 계산

Ethernet에서 frame 하나가 차지하는 시간은 frame 바이트만이 아닙니다. 앞에 preamble 7 B + SFD 1 B가 붙고, 뒤에 최소 IFG 12 B가 비어 있어야 합니다. 그래서 "frame 슬롯" 하나 = `frame_len + 20` 바이트입니다.

<div class="chart" data-src="chart3.svg" data-caption="64 B frame 하나는 선로에서 84 B(672 bit)를 씁니다. 10 Gb/s에서 슬롯 하나가 67.2 ns이고, 1초에 14,880,952개(내림)가 최대입니다. 64 B에서는 선로의 23.8 %가 오버헤드라, 20 B를 빼고 계산하면 성능을 31 % 부풀리게 됩니다"></div>

| link | 64 B | 512 B | 1518 B | 9216 B |
|---|---|---|---|---|
| 1 GbE | 1,488,095 | 234,962 | 81,274 | – |
| 10 GbE | 14,880,952 | 2,349,624 | 812,743 | 135,339 |
| 25 GbE | 37,202,380 | 5,874,060 | 2,031,859 | – |

(전부 내림. 업계에서 흔히 쓰는 "812,744 pps"는 반올림 값이고, [V10](../V10_eth_frame_fcs/index.html) 표에도 그렇게 적혀 있습니다. 여기서는 **line rate를 절대 넘지 않도록 내림**을 씁니다.)

### 4.2 이진 탐색과 확인 trial

throughput은 "frame을 하나도 안 잃는 최대 rate"입니다. 통과/실패가 rate에 대해 단조(어느 rate 아래는 다 통과, 위는 다 실패)라고 보고 이진 탐색합니다. 0.1 % 해상도면 100 % trial 1번 + 탐색 10번(10000 / 2^10 ≈ 9.8 ≤ 10) + 확인 1번 = **12번**입니다.

<div class="chart" data-src="chart1.svg" data-caption="9.1 Mfps에서 막히는 경로(64 B)의 탐색. 파란 막대는 각 trial 뒤의 [lo, hi], 초록은 zero loss, 빨강은 loss. 실제 용량 61.15 %(주황 점선) 근처로 간격이 반씩 줄고, 11번째 trial 뒤 hi − lo = 0.10 %가 되어 멈춘 다음 12번째에 61.12 %를 확인합니다"></div>

**확인 trial이 필요한 이유**: 단조 가정은 실제로 깨집니다. 다른 bus master가 잠깐 쉬었거나, 열·클럭 상태가 좋았던 trial 하나가 용량보다 높은 rate를 **거짓 통과**시키면, 그 뒤 탐색은 전부 그 위에서만 일어나고 결과가 높게 나옵니다. 마지막에 후보를 다시 돌려 보면 이걸 잡습니다. 반대로 **거짓 실패**(bit error로 1개 잃음)는 확인으로 못 잡습니다. 결과가 낮게 나올 뿐 재현은 되기 때문입니다(테스트 6번).

### 4.3 손으로 따라가기 (trace)

**9.1 Mfps 경로, 64 B, 10 GbE, res 10, trial 60 s** (테스트 3번). 모든 실패 trial에서 DUT는 60 s × 9.1 Mfps = 546,000,000개만 돌려줍니다.

| trial | pct_x100 | rate (fps) | sent | received | 판정 | lo / hi 뒤 |
|---|---|---|---|---|---|---|
| 1 | 10000 | 14,880,952 | 892,857,120 | 546,000,000 | loss | 0 / 10000 |
| 2 | 5000 | 7,440,476 | 446,428,560 | 446,428,560 | pass | 5000 / 10000 |
| 3 | 7500 | 11,160,714 | 669,642,840 | 546,000,000 | loss | 5000 / 7500 |
| 4 | 6250 | 9,300,595 | 558,035,700 | 546,000,000 | loss | 5000 / 6250 |
| 5 | 5625 | 8,370,535 | 502,232,100 | 502,232,100 | pass | 5625 / 6250 |
| 6 | 5937 | 8,834,821 | 530,089,260 | 530,089,260 | pass | 5937 / 6250 |
| 7 | 6093 | 9,066,964 | 544,017,840 | 544,017,840 | pass | 6093 / 6250 |
| 8 | 6171 | 9,183,035 | 550,982,100 | 546,000,000 | loss | 6093 / 6171 |
| 9 | 6132 | 9,124,999 | 547,499,940 | 546,000,000 | loss | 6093 / 6132 |
| 10 | 6112 | 9,095,237 | 545,714,220 | 545,714,220 | pass | 6112 / 6132 |
| 11 | 6122 | 9,110,118 | 546,607,080 | 546,000,000 | loss | 6112 / 6122 → 간격 10, 탐색 끝 |
| 12 | 6112 | 9,095,237 | 545,714,220 | 545,714,220 | pass (확인) | **결과 61.12 %** |

실제 한계는 `9,100,000 / 14,880,952` = 61.152 %라서, pct로는 6115까지 통과합니다. 결과 6112는 해상도(10) 안에 있습니다.

**`FLAKY_BOOST` at trial 3** (테스트 5번):

| 구간 | trial | 일어난 일 |
|---|---|---|
| 첫 탐색 | 1–2 | 100 % loss, 50 % pass (위와 같음) |
| | 3 | 75 % **거짓 pass** → lo = 7500 |
| | 4–11 | 87.50, 81.25, 78.12, 76.56, 75.78, 75.39, 75.19, 75.09 % 전부 loss → 간격 9, 후보 7500 |
| 확인 | 12 | 75 % loss → **거짓 통과 발견**, hi = 7500, lo = 0 |
| 재탐색 | 13–22 | 37.50 P, 56.25 P, 65.62 F, 60.93 P, 63.27 F, 62.10 F, 61.51 F, 61.22 F, 61.07 P, 61.14 P → 간격 8 |
| 확인 | 23 | 61.14 % pass → **61.14 %, 9,098,214 fps, trials 23** |

확인이 없었다면 75 %(11,160,714 fps)를 보고했을 것이고, 실제보다 23 % 높은 숫자입니다. 재탐색은 경로가 달라서 61.12가 아니라 61.14에서 끝났지만 둘 다 진짜 한계(61.15) 아래, 해상도 안입니다. `max_trials = 20`이면 재탐색 도중 21번째 trial을 못 돌려서 `-ETIMEDOUT`, `trials = 20`.

**다른 결과들** (테스트 2, 4, 6, 7번):

| 경우 | 결과 |
|---|---|
| line rate DUT, 64 B / 1518 B | 100 %, 14,880,952 / 812,743 fps, trials 2 |
| 3.0 Mfps 경로, 64 / 128 / 256 B | 20.11 % / 35.44 % / 66.20 % (실제 20.16 / 35.52 / 66.24 %) |
| 3.0 Mfps 경로, 512 / 1024 / 1280 / 1518 B | 전부 100 % (line rate가 3.0 Mfps보다 낮음) |
| `FLAKY_DROP` at trial 2 (50 %에서 1개 잃음) | hi = 5000이 돼 버림 → **49.90 %, 7,425,595 fps, trials 12** (확인은 통과, 못 잡음) |
| 아무것도 못 보내는 경로 (`pps_max = 0`) | 0 %, trials 11 (확인 없음) |

<div class="chart" data-src="chart2.svg" data-caption="3.0 Mfps에서 막히는 경로의 frame 크기별 zero-loss throughput. 작은 frame은 per-packet 비용에 걸려 line rate의 일부만 나오고, 512 B부터는 line rate(2.35 Mfps)가 경로 한계보다 낮아 100 %가 나옵니다. RFC 2544 결과표가 항상 frame 크기별로 나오는 이유입니다"></div>

**loss 곡선** (테스트 8번, 9.1 Mfps 경로, 64 B):

| 단계 | rate (fps) | sent | lost | loss_x100 | zero_run |
|---|---|---|---|---|---|
| 100 % | 14,880,952 | 892,857,120 | 346,857,120 | 3884 (38.84 %) | 0 |
| 90 % | 13,392,856 | 803,571,360 | 257,571,360 | 3205 | 0 |
| 80 % | 11,904,761 | 714,285,660 | 168,285,660 | 2355 | 0 |
| 70 % | 10,416,666 | 624,999,960 | 78,999,960 | 1263 | 0 |
| 60 % | 8,928,571 | 535,714,260 | 0 | 0 | 1 |
| 50 % | 7,440,476 | 446,428,560 | 0 | 0 | 2 → 멈춤, **6점** |

- 50 % 단계에 `FLAKY_DROP`(6번째 trial)이면 lost = 1, `loss_x100` = 0(내림)이지만 zero_run이 0으로 리셋 → 40 %, 30 %까지 가서 **8점**.
- line rate DUT(1518 B): 100 %, 90 % 둘 다 0 → **2점**.
- 1 Mfps 경로(64 B의 10 %도 1,488,095 fps라 넘음): 10 %까지 전부 loss → **10점**.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `line_rate_fps`의 곱셈은 `(uint64_t)(frame_len + 20) * 8`로 64-bit에서 합니다. 400 GbE도 `4e11`이라 `uint64_t`로 충분합니다.
- `rate_at()`은 내림이라 `pct = 10000`이면 정확히 `line`이고 절대 넘지 않습니다. tester가 line rate를 넘겨 보내라고 하면 결과가 의미 없습니다(테스트 3번 `max_rate`).
- `run_trial()`이 **예산 검사 → `trials++` → `fn() == n`**을 한 곳에서 합니다. `received > sent`(중복 frame)도 실패로 봅니다. `==` 하나로 둘 다 잡힙니다.
- `bool search`: 100 %가 통과한 경우는 탐색을 건너뛰고 같은 확인 경로를 탑니다. 100 %의 확인이 실패하면 `hi = 10000, lo = 0`에서 탐색이 시작되므로 별도 코드가 필요 없습니다.
- `mid = lo + (hi - lo) / 2`: 여기서는 값이 작아 overflow는 없지만, 습관적으로 쓰는 형태입니다.
- loss 곡선의 `zero_run`은 `lost == 0`으로만 판단합니다. `loss_x100`은 보고용이고 판정용이 아닙니다.

### 4.5 복잡도

- throughput: trial 수 = 1 + ⌈log2(10000 / res)⌉ + 1 (확인 실패가 없을 때). res 10 → 12번. 시간 = trial 수 × trial 길이: 60 s trial이면 **12분 / frame 크기**, RFC 2544 기본 7개 크기면 약 **84분**.
- loss 곡선: 최대 10 trial. 메모리는 둘 다 O(1) + 호출자 배열.

---

## 5. 임베디드 관점으로 말할 것

- **IFG/preamble을 빼먹으면 숫자가 틀린다**: 64 B에서 20 B를 빼면 19.5 Mfps가 나와 31 % 부풀려집니다. 반대로 L2 bit rate로 보고하면 64 B line rate가 7.62 Gb/s밖에 안 돼 보여서 "DUT가 느리다"는 오해가 생깁니다. 보고서에는 **fps와 % of line rate를 같이** 적습니다.
- **trial 길이 = 통계**: RFC 2544는 trial을 **60 s 이상**으로 권합니다. 짧은 trial은 FIFO가 overflow하기 전에 끝나서 거짓 통과가 납니다. 예: 9.1 Mfps 경로에 한계보다 0.1 % 높은 rate를 주면 초과분이 초당 9,100개라, 1,000-frame buffer가 약 0.11 s를 버팁니다 → 0.1 s trial은 통과로 나옵니다. 한계에 가까울수록 오래 버티니, 바로 그 구간에서 짧은 trial이 틀립니다. 실무에서는 짧은 trial(예: 2–10 s)로 탐색하고 마지막 확인만 60 s로 돌리는 방식이 흔합니다. 또 trial이 끝난 뒤 **약 2 s 기다렸다가** 수신 카운터를 읽어야 in-flight frame을 loss로 잘못 세지 않습니다.
- **latency (§26.2)**: throughput rate에서 120 s 스트림을 보내고, 60 s쯤에 **timestamp를 박은 frame** 하나를 넣어 TX/RX 시각 차를 잽니다. 최소 20번 반복해 평균. store-and-forward 장비는 마지막 비트 입력 → 첫 비트 출력(LIFO), cut-through는 첫 비트 → 첫 비트(FIFO)로 정의가 다릅니다(RFC 1242). 반드시 **throughput 이하 rate**에서 재야 합니다. 그 위에선 queue가 무한히 쌓여 latency가 trial 길이에 비례할 뿐입니다. 분포 통계(min/p50/p99)는 [V4](../V4_perf_bench_stats/index.html), timestamp 정합은 [V12](../V12_frame_timestamp_check/index.html).
- **back-to-back (§26.4)**: 최소 IFG로 burst를 보내고, loss 없이 통과하는 **가장 긴 burst 길이**를 찾습니다(burst 2 s 이상, 50번 반복). throughput이 "지속 처리 능력"이라면 back-to-back은 **buffer 깊이**를 잽니다. 이것도 같은 이진 탐색 코드에 "rate" 대신 "burst 길이"를 넣으면 됩니다.
- **SoC 내부 경로에 그대로 적용**: JD의 "throughput, latency, resource utilization, IO frequency" 벤치마크가 바로 이 방법입니다.
  - **drop하는 경로**(MIPI CSI-2 카메라 입력, Ethernet MAC RX, ADC 스트림): 상대에게 back-pressure를 못 겁니다. "zero-loss throughput" = **FIFO overflow 카운터가 0으로 유지되는 최대 지속 rate**. 카메라 frame rate × 해상도를 올려 가며 같은 탐색을 합니다 → [V9](../V9_csi2_packet_parse/index.html).
  - **back-pressure가 있는 경로**(AXI, PCIe, DMA → DDR): loss가 안 나고 대신 상류가 멈춥니다. 그래서 기준을 "offered rate 대비 achieved rate가 99.9 % 이상" 또는 "latency p99가 한계 이하"로 바꿔 같은 탐색을 합니다. DMA 엔진은 [V3](../V3_dma_desc_ring/index.html), PCIe 링크 쪽은 [L2](../L2_pcie_cap_walk/index.html), 생산자/소비자 ring overflow는 [L5](../L5_spsc_sensor_ring/index.html).
  - **NPU IO**: 입력 tensor DMA rate를 올려 가며 NPU가 frame 마감 안에 끝내는 최대 rate를 찾습니다. 결과는 "frames/s at zero deadline miss".
- **traffic generator 정확도**: SW로 만든 generator는 타이머 jitter 때문에 평균 rate는 맞아도 **순간 burst**가 생겨 거짓 loss가 납니다. 그래서 tester는 FPGA/HW에서 IFG를 bit 단위로 맞춥니다. SoC 검증에서도 CPU 루프보다 **HW traffic generator**(AXI traffic generator IP, DMA 엔진의 rate limiter)를 씁니다. 또 Ethernet은 ±100 ppm 클럭 차이가 허용되므로, tester 클럭이 DUT보다 빠르면 100 %에서 완벽한 DUT도 조금씩 잃습니다.
- **BER과 frame loss는 다르다**: bit error 하나 → FCS 불일치 → frame drop → loss로 셈. 10 GbE 64 B line rate 60 s면 선로 비트가 6 × 10^11개, 그중 frame 비트가 약 4.6 × 10^11개라서, BER 1e-12 링크면 trial당 **오류 frame 기대값이 약 0.46개**입니다. 용량 문제가 아니라 링크 문제로 loss가 날 수 있으니, throughput 전에 PRBS로 링크를 먼저 검증합니다 → [V6](../V6_prbs_crc_checker/index.html). `FLAKY_DROP` 테스트가 이 상황입니다.

## 5.1 면접관 Iris Lee(SW·시스템 출신 Principal)가 파고들 포인트

- **"Why binary search, and why the confirmation trial?"** → 통과/실패가 rate에 대해 단조라고 가정하면 이진 탐색이 trial 수를 log로 줄입니다(0.1 %에 12번). 하지만 실제 DUT는 비결정적이라 거짓 통과 하나가 결과 전체를 끌어올립니다. 확인 trial이 그걸 잡고, 거짓 실패는 못 잡으니 전체 테스트를 반복해 분산도 봅니다.
  *"Binary search assumes monotonic pass/fail, which real systems violate; the confirmation trial catches a false pass, and repeating the whole run catches a false fail."*
- **"Your trial is 60 seconds. How long does a full RFC 2544 run take, and how do you shorten it?"** → frame 크기 7개 × 12 trial × 60 s ≈ 84분, latency·back-to-back까지 하면 몇 시간입니다. 짧은 trial로 탐색 → 60 s로 확인, 이전 결과 근처에서 탐색 시작(warm start), 여러 포트 병렬. 단, 확인 trial은 절대 줄이지 않습니다.
  *"Search with short trials, confirm with the full 60 seconds, and start the next frame size near the last answer; the confirmation length is the one thing I never cut."*
- **"You lost 1 frame in 446 million. Is that a throughput problem?"** → 아닐 가능성이 큽니다. 한 개 단발 loss는 용량 한계(수백만 개씩 잃음)와 모양이 다르고, BER 1e-12면 기대되는 수준입니다. FCS 에러 카운터를 같이 읽어서 "drop 원인 = CRC 에러"인지 "drop 원인 = FIFO overflow"인지 나눕니다. 카운터를 원인별로 나눠 놓는 게 테스트 설계의 핵심입니다.
  *"A single lost frame looks like a bit error, not a capacity limit, so I read the FCS-error and FIFO-overflow counters separately before I call it a throughput failure."*
- **"How does this apply to the SoC, not an Ethernet switch?"** → 같은 framework에서 trial callback만 바꿉니다. 카메라/MAC처럼 drop하는 경로는 overflow 카운터, AXI/PCIe처럼 back-pressure가 있는 경로는 achieved/offered 비율이나 latency p99를 판정 기준으로 씁니다. generator도 CPU가 아니라 HW(DMA rate limiter, AXI traffic generator)로 해야 burst 없이 정확합니다.
  *"The search and reporting stay the same; only the trial changes — for a camera path the pass criterion is zero FIFO overflow, for an AXI or PCIe path it's achieved over offered rate and a p99 latency bound."*
- **"How do you trust the traffic generator?"** → generator를 먼저 **loopback으로 자가 측정**합니다(DUT 없이 100 %에서 zero loss, IFG가 12 B로 정확한지). 그리고 rate 계산은 내림이라 line rate를 넘을 수 없게 했습니다. 클럭 ppm 차이도 확인합니다.
  *"I calibrate the generator against itself in loopback first — it must hit 100 percent with zero loss and exact 12-byte gaps — and the rate math rounds down so it can never exceed line rate."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Why search in % of line rate instead of fps? | 해상도가 frame 크기와 무관하게 같은 의미가 되고(0.1 %), 결과를 RFC 2544 보고 형식(% of line + fps)으로 바로 낼 수 있습니다. 정수 0..10000이라 float가 필요 없고, 내림으로 line rate를 넘을 수 없습니다. |
| The DUT passes 100 % but loses frames at 90 %. Possible? | 가능합니다. 비단조 동작의 예: 특정 rate에서만 interrupt coalescing 타이머와 맞물려 FIFO가 넘치거나, rate limiter/shaper의 토큰 버킷 경계. 이진 탐색은 이걸 못 봅니다. 그래서 §26.3 loss 곡선(10 % 단위 전 구간)을 같이 돌려 단조성을 확인합니다. |
| What if the tester clock is 100 ppm faster than the DUT's egress? | 같은 속도 포트끼리 forwarding하면 DUT는 받는 것보다 100 ppm 느리게 내보냅니다. 100 % 60 s 64 B면 약 892,857,120 × 1e-4 ≈ 89,286개가 쌓여 버퍼를 넘치면 loss. 결과가 99.99 %처럼 나오는 이유이고, tester가 DUT 포트 클럭을 측정해 보정하거나 같은 기준 클럭을 씁니다. |
| How would you measure latency per §26.2 with this code? | throughput 결과 rate에서 trial을 돌리면서 frame 하나에 TX timestamp를 넣고 RX에서 차이를 잽니다. 20번 이상 반복해 평균(우리는 min/p50/p99도). 반드시 throughput 이하 rate에서 — 그 위에선 queue가 계속 쌓여서 latency가 "측정"이 아니라 "trial 길이"가 됩니다. |
| How do you implement back-to-back (§26.4)? | 같은 탐색에서 변수만 바꿉니다: rate는 100 %(최소 IFG) 고정, burst 길이(frame 수)를 이진 탐색해 loss 없는 최대 burst를 찾습니다. 결과 × frame 크기 ≈ DUT의 유효 buffer 깊이. 50번 반복해 평균. |
| How do you tell a capacity limit from a link problem? | loss 모양과 카운터. 용량 한계는 rate에 비례해 대량으로 잃고(위 표처럼 수억 개), 수신 MAC의 overflow/no-buffer 카운터가 올라갑니다. 링크 문제는 rate와 무관하게 소량이고 FCS/CRC 에러 카운터가 올라갑니다. 링크는 PRBS로 먼저 검증 → [V6](../V6_prbs_crc_checker/index.html). |
| The trial budget runs out. What do you report? | 실패(-ETIMEDOUT)로 보고하고 trial 로그를 남깁니다. 마지막 "확인되지 않은" 후보를 결과처럼 내면 안 됩니다. 예산 초과 자체가 DUT가 불안정하다는 신호(거짓 통과/실패 반복)라서 그 자체로 버그 리포트감입니다. |
| Mapping to a DMA → DDR path on the SoC? | DMA는 back-pressure가 있어서 loss가 안 납니다. offered rate를 rate limiter로 올려 가며 achieved bandwidth와 latency p99를 기록하고, "achieved ≥ 99.9 % of offered"를 통과 기준으로 같은 탐색을 합니다. 동시에 다른 master(CPU, NPU)의 트래픽을 걸어 interconnect contention 아래서의 값도 냅니다 → [V3](../V3_dma_desc_ring/index.html), [V4](../V4_perf_bench_stats/index.html). |

## 7. 자주 하는 실수

- frame당 20 B(preamble + SFD + IFG)를 빼먹음 → 64 B line rate가 19.5 Mfps로 나와서 DUT가 "61 %"가 아니라 "47 %"처럼 보임 (테스트 1번)
- `frame_len`에 FCS를 또 더함 → 64 B를 68 B로 계산
- 이진 탐색 결과를 확인 없이 보고 → 거짓 통과 하나로 75 % 보고 (테스트 5번)
- 탐색을 fps로 하면서 `hi = line + 1` 같은 경계 → line rate 초과 trial
- trial 예산이 없음 → 불안정한 DUT에서 무한 루프. 상한과 `-ETIMEDOUT`이 필요 (테스트 5번 `max_trials = 20`)
- zero loss를 `loss_x100 == 0`으로 판정 → 1개 잃은 단계도 통과로 셈 (테스트 8번)
- `received > sent`(중복 frame)를 통과로 봄 → `>=` 대신 `==`
- `rate * trial_ms`나 `n * pps_max`를 32-bit로 계산 → 25 GbE 60 s에서 overflow
- trial 끝나자마자 카운터를 읽음 → 아직 오는 중인 frame을 loss로 셈 (실장비에서 2 s 대기)
