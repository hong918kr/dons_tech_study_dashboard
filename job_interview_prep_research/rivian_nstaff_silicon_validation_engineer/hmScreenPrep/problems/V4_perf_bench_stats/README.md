# V4 · Benchmark Harness: Latency / Throughput Stats from a 32-bit Cycle Counter

> **ID**: V4 · **Priority**: P0 · **Pattern**: wrap-safe delta + overhead calibration + nearest-rank percentile + overflow-safe integer math · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh V4` / `./run.sh V4 sol`

## 1. Problem

You are writing the micro-benchmark library the validation team uses to characterize blocks on a new SoC (CPU memcpy, DMA copies, NPU kernels). The only timer is a **free-running 32-bit cycle counter** `read_cycles()` that wraps at 2^32. Reading the counter costs a few cycles itself. The HAL is a fake at the bottom of `stub.c`: the test can start the counter near `0xFFFFFFFF`, inject extra cost on individual reads, and script the duration of each call to the operation under test.

```c
uint32_t read_cycles(void);                 /* HAL: free-running, wraps at 2^32 */
typedef void (*bench_op_fn)(void *ctx);     /* operation under test */
#define CPU_HZ      1500000000u             /* 1.5 GHz */
#define CPU_MHZ     (CPU_HZ / 1000000u)
#define BENCH_CAL_K 8

typedef struct {
    uint32_t n;
    uint32_t min, max, mean;   /* cycles, overhead removed, mean rounded down */
    uint32_t p50, p99;         /* nearest-rank percentiles */
    uint32_t outliers;         /* samples > p50 * outlier_x */
} BenchStats;

uint32_t bench_calibrate(uint32_t k);
int      bench_run(bench_op_fn op, void *ctx, uint32_t iters, uint32_t warmup,
                   uint32_t *samples, uint32_t *overhead_out);
int      bench_stats(uint32_t *samples, uint32_t n, uint32_t outlier_x, BenchStats *st);
uint64_t bench_mbps(uint64_t bytes_per_op, uint32_t cycles_per_op, uint32_t cpu_mhz);
```

- **`bench_calibrate(k)`**: the cost of an *empty* measurement (`t0 = read; t1 = read; t1 - t0`), taken as the **minimum** over `k` tries. `k == 0` → `0`.
- **`bench_run`**: (1) `overhead = bench_calibrate(BENCH_CAL_K)`, store it in `*overhead_out` if non-NULL; (2) call `op(ctx)` `warmup` times without measuring; (3) for each of `iters` iterations: `t0 = read; op(ctx); t1 = read;` and store `samples[i] = (t1 - t0) - overhead`, **wrap-safe**, clamped at 0. Return `0`, or `-EINVAL` if `op == NULL` or `samples == NULL && iters > 0`.
- **`bench_stats`**: `min`, `max`, `mean` (64-bit accumulator, floor), `p50` and `p99` by **nearest rank**: `rank = ceil(p * n / 100)`, value = `sorted[rank - 1]`. `outliers` = number of samples strictly greater than `p50 * outlier_x`. You may sort `samples` in place (it is the caller's buffer). No `malloc`. `n == 0` or NULL pointers → `-EINVAL` and `*st` is left untouched.
- **`bench_mbps`**: throughput in MB/s (1 MB = 10^6 bytes) = `bytes * cpu_mhz / cycles`, **rounded down**, with no 64-bit overflow for any input. Saturate at `UINT64_MAX`. `cycles_per_op == 0` → `0`.

### Example

```
read cost = 20 cycles, op takes exactly 1000 cycles
bench_run(op, ctx, 16, 4, smp, &ovh)  -> 0, ovh = 20, op called 20 times, smp[0..15] = 1000
bench_stats(smp, 16, 2, &st)          -> min = max = mean = p50 = p99 = 1000, outliers = 0

counter starts at 0 - 1100: iteration 2 sees t0 = 0xFFFFFF9C, t1 = 0x000000DC
                            raw = 0x140 = 320, sample = 320 - 20 = 300

100 samples: 95 x 1000, 3 x 1500, 2 x 5000 (any order), outlier_x = 2
  -> min 1000, max 5000, mean 1095, p50 1000, p99 5000, outliers 2

bench_mbps(4096, 2048, 1500)        -> 3000      (4 KiB copy in 2048 cycles at 1.5 GHz)
bench_mbps(1000, 3, 1000)           -> 333333    (333333.33 rounded down)
bench_mbps(1ull << 54, 1000000, 1500) -> 27021597764222   (bytes * mhz would overflow 64 bits)
```

### Constraints

- C11, single thread, interrupts may disturb a measurement (the fake injects this as extra read cost).
- `1 <= n <= 4096` for `bench_stats` (O(n²) insertion sort is acceptable; say what you would use for larger n).
- A real measured interval is always shorter than one counter period (2^32 cycles ≈ 2.86 s at 1.5 GHz).

### Clarifying questions to ask (first 3 minutes)

1. Is the counter 32 or 64 bit, and is it the core clock (PMU `PMCCNTR`) or a fixed-frequency timer (`CNTVCT`)? Does it keep counting under DVFS / WFI?
2. How should I remove the measurement overhead — subtract a calibrated empty measurement? Min or mean of the calibration runs?
3. Which percentile definition do you want: nearest rank or linear interpolation?
4. Can I reorder the caller's sample buffer, or must I keep the original order (e.g. for a time-series plot)?
5. Is MB 10^6 or 2^20 bytes? Round down or to nearest?
6. Is the op measured one call per sample, or should I batch N calls per sample when the op is shorter than the overhead?

## 2. Your workspace

`./run.sh V4`로 바로 테스트하세요. 테스트 6묶음(36개 CHECK)이 있고, 파일 아래쪽 fake counter / fake op는 건드리지 않습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 랩어라운드는 뺄셈 한 번으로 끝</summary>

`uint32_t raw = t1 - t0;` 이면 됩니다. unsigned 뺄셈은 mod 2^32로 정의되어 있어서 t1이 wrap 후 작은 값이 되어도 결과가 맞습니다. `if (t1 < t0) raw = (UINT32_MAX - t0) + t1 + 1;` 같은 분기는 필요 없고, 64-bit나 signed로 바꿔서 빼면 오히려 틀립니다.

</details>

<details>
<summary>Hint 2 — 오버헤드는 왜 min인가</summary>

빈 측정의 진짜 비용은 고정값이고, 노이즈(인터럽트, 캐시 미스)는 **항상 더하기만** 합니다. 그래서 K번 중 최솟값이 진짜 값에 가장 가깝습니다. 평균을 쓰면 노이즈가 섞인 값만큼 모든 샘플을 과하게 빼게 됩니다(테스트 3번).

</details>

<details>
<summary>Hint 3 — MB/s 오버플로</summary>

`bytes * mhz`를 먼저 곱하면 bytes가 2^54 근처에서 64-bit를 넘습니다. `bytes = q*cycles + r`로 나눠서 `q*mhz + (r*mhz)/cycles`를 계산하면 정확한 floor 값이 나오고, `r < cycles < 2^32`라 `r*mhz`는 절대 넘치지 않습니다. `q*mhz`는 `q > UINT64_MAX / mhz`로 미리 검사해서 포화시킵니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 분포를 보면 평균은 거짓말을 한다

벤치마크 결과는 숫자 하나가 아니라 **분포**입니다. 테스트 4번 데이터를 히스토그램으로 그리면, 평균(1095)은 실제 어떤 샘플과도 같지 않고 꼬리 2개에 끌려간 값입니다. 그래서 검증 리포트에는 min / p50 / p99 / max를 같이 적습니다.

<div class="chart" data-src="chart1.svg" data-caption="테스트 4번 분포 (n = 100). p50 = 1000, mean = 1095, p99 = rank 99 = 5000. p50 × 2 = 2000을 넘는 샘플 2개가 outlier"></div>

- **min**: 하드웨어가 낼 수 있는 최선(캐시 hit, 방해 없음). 회귀 비교에 가장 안정적입니다.
- **p50**: 전형적인 값. 평균보다 꼬리에 덜 흔들립니다.
- **p99 / max**: 실시간 시스템(카메라 프레임, 제어 루프)에서 deadline을 결정하는 값.
- **outlier 수**: 인터럽트, refresh, TLB miss 같은 "가끔 생기는 일"이 몇 번 있었는지.

nearest rank 정의: `rank = ceil(p·n/100)`. n = 4, 값 {10,20,30,40}이면 p50 = rank 2 = **20**입니다(보간 정의면 25). 정의가 답을 바꾸므로 질문 3번을 먼저 합니다.

### 4.2 wrap-safe 측정과 오버헤드

<div class="chart" data-src="chart2.svg" data-caption="테스트 2번 iteration 2: t0 = 0xFFFFFF9C, t1 = 0x000000DC. uint32 뺄셈은 320(= read 비용 20 + op 300), signed 64-bit로 빼면 −4294966976"></div>

fake에서 `read_cycles()`는 값을 먼저 잡고(latch) 그 뒤에 비용 20 cycle을 씁니다. 그래서 측정 하나는 `raw = (t0 읽기 비용) + op`이고, 빈 측정은 `raw = t0 읽기 비용`입니다. 빈 측정 값을 빼면 op 시간만 남습니다. 실제 코어에서도 같은 구조입니다: `MRS PMCCNTR`(또는 `rdtsc`) 자체와 그 앞뒤 파이프라인 효과가 항상 같이 잡힙니다.

### 4.3 손으로 따라가기 (trace)

테스트 2번 (counter 시작 = `0 − 1100` = 0xFFFFFBB4, read 비용 20, op 300):

| 단계 | t0 | t1 | raw = t1 − t0 | sample | 끝난 뒤 counter |
|---|---|---|---|---|---|
| calibrate ×8 | – | – | 20 (8번 모두) | ovh = **20** | 0xFFFFFCF4 (0 − 780) |
| iter 0 | 0xFFFFFCF4 | 0xFFFFFE34 | 320 | 300 | 0xFFFFFE48 (0 − 440) |
| iter 1 | 0xFFFFFE48 | 0xFFFFFF88 | 320 | 300 | 0xFFFFFF9C (0 − 100) |
| iter 2 | **0xFFFFFF9C** | **0x000000DC** | 0x140 = **320** | **300** | 0x000000F0 |
| iter 3 | 0x000000F0 | 0x00000230 | 320 | 300 | 0x00000244 |

iteration 하나 = t0 읽기 20 + op 300 + t1 읽기 20 = 340 cycle 진행, 그중 raw에 잡히는 것은 앞의 320입니다.

테스트 3번 (calibration 오염): 빈 측정 8쌍 중 0–6번 쌍의 t0 읽기에 +15를 주입합니다.

| 방법 | 계산 | overhead | sample (op 500) |
|---|---|---|---|
| 첫 값 | 35 | 35 | 485 ✗ |
| 평균 | (7 × 35 + 20) / 8 = 33 | 33 | 487 ✗ |
| **최솟값** | min(35 ×7, 20) | **20** | **500** ✓ |

테스트 4번 통계: 합 = 95·1000 + 3·1500 + 2·5000 = 109500 → mean = 1095. p50 rank = ceil(50) = 50 → sorted[49] = 1000. p99 rank = ceil(99) = 99 → sorted[98] = 5000. outlier 기준 1000 × 2 = 2000 → 5000 두 개.

MB/s (`q = bytes / cycles`, `r = bytes % cycles`):

| bytes | cycles | mhz | q | r | q·mhz + r·mhz/cycles | 결과 |
|---|---|---|---|---|---|---|
| 4096 | 2048 | 1500 | 2 | 0 | 3000 + 0 | **3000** |
| 1000 | 3 | 1000 | 333 | 1 | 333000 + 333 | **333333** |
| 2^54 | 1000000 | 1500 | 18014398509 | 481984 | 27021597763500 + 722 | **27021597764222** |
| 2^64 − 1 | 1 | 2 | 2^64 − 1 | 0 | q > UINT64_MAX / 2 | **UINT64_MAX** (포화) |

세 번째 줄에서 순진하게 `2^54 × 1500`을 먼저 하면 ≈ 2.7 × 10^19 > 2^64 ≈ 1.8 × 10^19라 값이 깨집니다.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `uint32_t raw = t1 - t0;` 한 줄이 wrap 처리 전부입니다. 조건은 "실제 구간 < 2^32 cycles"뿐이고, 이건 Constraints에 있습니다.
- `samples[i] = (raw > ovh) ? raw - ovh : 0;` — op가 거의 0 cycle이면 노이즈 때문에 `raw < ovh`가 될 수 있고, 그대로 빼면 unsigned underflow로 4 × 10^9 같은 값이 됩니다.
- 측정 루프 안에는 `read → op → read`와 저장 한 번만 있습니다. 통계, printf, 분기는 루프 밖에서 합니다(measurement perturbation 최소화).
- `bench_stats`는 결과를 지역 변수 `s`에 만든 다음 마지막에 `*st = s`로 한 번에 씁니다. 그래서 에러 경로에서는 `*st`가 바뀌지 않습니다.
- `pct_nearest_rank`의 `((uint64_t)p * n + 99) / 100`은 정수 ceil입니다. `rank == 0` 방어는 p = 0을 위한 것입니다.
- `(uint64_t)s.p50 * outlier_x` — p50 = 0xF0000000, x = 0xFFFFFFFF 같은 값은 32-bit에서 넘칩니다(테스트 4번 마지막).

### 4.5 복잡도

- `bench_run`: 시간 O(K + warmup + iters), 메모리 O(1) + 호출자 버퍼.
- `bench_stats`: insertion sort O(n²) (n ≤ 4096이면 최악 ~8M 비교, 리포트 단계라 허용). n이 크면 heapsort O(n log n, 추가 메모리 0) 또는 **고정 bin 히스토그램** O(n)으로 p50/p99를 근사합니다.
- `bench_mbps`: O(1), 나눗셈 2번.

---

## 5. 임베디드 관점으로 말할 것

- **카운터 선택**: Cortex-A/R의 PMU cycle counter(`PMCCNTR_EL0`)는 코어 클록을 세므로 DVFS로 주파수가 바뀌면 "cycle"은 그대로인데 시간은 바뀝니다. Generic timer(`CNTVCT_EL0`)는 고정 주파수(예: 24–100 MHz)라 벽시계 시간에 맞지만 분해능이 낮습니다. 리포트에 둘 중 무엇인지, 측정 중 주파수를 고정했는지 적습니다.
- **오버헤드 / 직렬화**: out-of-order 코어에서는 `ISB`(Arm) 또는 `lfence; rdtsc`(x86)로 카운터 읽기를 op 앞뒤로 고정해야 op가 측정 구간 밖으로 새지 않습니다. 이 문제의 calibrate는 그 비용까지 함께 빼는 구조입니다.
- **warm-up**: 첫 몇 번은 I-cache/D-cache miss, TLB miss, 분기 예측 학습, DDR page open 때문에 느립니다. warm-up을 버리거나, 반대로 **cold 성능이 목적이면 매 iteration 전에 cache flush**를 하고 그 사실을 명시합니다.
- **throughput은 정수로**: bare-metal 검증 펌웨어는 FPU를 안 켜거나 printf `%f`가 없는 경우가 많습니다. MB/s를 정수 floor로 내고, 필요하면 `×1000`한 milli-MB/s로 소수점을 보존합니다.
- **결과는 host로**: 샘플 원본 대신 `min/p50/p99/max/outliers` 한 줄(CSV)을 UART로 내보내고, 원본은 필요할 때만 덤프합니다 → 같은 형식의 결과 라인은 [V5](../V5_test_runner_coverage/index.html) 참고.

## 5.1 면접관(HW 출신 Sr Manager)이 파고들 포인트

- **"How do you know your benchmark itself is right?"** — 이론값을 아는 대상으로 먼저 검증합니다: `NOP` N개 루프(= N cycle), 알려진 DDR 대역폭, 또는 HW 성능 카운터/로직 분석기와 교차 확인. *"I validate the harness first against something with a known answer, like a fixed NOP loop or a DMA with a known bus width and clock, before I trust it on a new block."*
- **"FPGA/HAPS에서 잰 숫자와 실리콘 숫자가 다르다."** — FPGA는 클록이 10–50 MHz라 절대값은 의미가 없고, **cycle 수와 비율(op 대비 DDR latency)** 만 비교합니다. 메모리 서브시스템은 FPGA에서 다른 IP(예: 대체 DDR 컨트롤러)를 쓰는 경우가 많아 그 차이를 리포트에 명시합니다. *"On FPGA I compare cycle counts and ratios, not absolute time, and I flag any block that is modeled differently there, like the DDR controller."*
- **"achieved vs theoretical"** — 이론 대역폭(버스 폭 × 클록 × 채널)이나 이론 TOPS(MAC 수 × 2 × 클록)를 먼저 적고 utilization = achieved / theoretical로 말합니다. 낮으면 roofline으로 compute-bound인지 memory-bound인지 가릅니다. *"I always report utilization against the theoretical peak, and use a roofline to say whether we are compute- or bandwidth-bound."*
- **"PVT / 주파수 sweep과 어떻게 엮나"** — 같은 벤치를 주파수·전압·온도 조합마다 돌리고 CSV로 모아 perf/W 곡선과 shmoo 형태로 그립니다 → [V7](../V7_shmoo_margin/index.html). *"The same benchmark runs at every frequency-voltage-temperature point and the host plots perf per watt; any point where results drift beyond run-to-run variance is a margin flag."*
- **"run-to-run variance"** — 같은 보드에서 여러 번, 여러 보드에서 반복해 p50의 분산을 봅니다. 차이가 그 분산보다 작으면 "개선/회귀"라고 말하지 않습니다. *"A change only counts if it is larger than the run-to-run variance we measured on the same build."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Cycle counter vs a timer: which do you use and why? | PMU `PMCCNTR`는 코어 cycle 단위로 분해능이 높지만 DVFS·WFI에 영향을 받고, 코어마다 따로입니다. `CNTVCT`는 고정 주파수·시스템 전체 공통이라 멀티코어/DMA 구간 측정에 맞지만 분해능이 낮습니다. 짧은 커널은 PMU, 긴 전송/여러 마스터 구간은 generic timer를 씁니다. 32-bit면 오버플로 IRQ로 상위 비트를 확장하거나 64-bit 카운터를 씁니다. |
| What about frequency scaling during the run? | 측정 중 DVFS governor를 끄고 주파수를 고정한 뒤, 그 주파수를 결과에 같이 기록합니다. cycle → 시간 변환은 측정 당시 실제 주파수(PLL 레지스터에서 읽은 값)로 합니다. |
| Why warm up? When would you NOT? | 캐시/TLB/분기 예측/DDR page 상태를 안정시켜 steady-state를 보려는 것입니다. 부팅 시간, 첫 프레임 latency처럼 cold 성능이 요구사항이면 반대로 매번 cache clean+invalidate, TLB flush 후 측정합니다. |
| How would you measure DMA bandwidth vs CPU memcpy? | CPU memcpy는 `read → memcpy → read`로 됩니다. DMA는 시작 시각을 kick 직전, 끝을 완료 인터럽트/상태 비트로 잡고, 큰 전송(수 MB)으로 setup 비용을 희석합니다. 캐시 coherency(clean/invalidate) 비용을 포함할지 명시하고, 결과는 `bench_mbps`로 같은 단위로 비교합니다. |
| How do you report utilization, e.g. achieved vs theoretical TOPS? | 이론 TOPS = MAC 수 × 2 × 클록. achieved = 연산 수 / 측정 시간. utilization = achieved / 이론. roofline(산술 강도 = ops/byte 대 대역폭 한계)에 찍어서 메모리 병목인지 계산 병목인지 판단합니다. |
| How would you sweep compute / IO frequency and plot perf/W? | 주파수 테이블을 돌며 PLL 설정 → 안정화 대기 → 벤치 → 전력(PMIC/INA 센서) 측정을 반복하고 한 줄 CSV로 host에 보냅니다. host가 perf, power, perf/W를 그리고 knee point를 찾습니다. |
| Is a 2% improvement real? | run-to-run variance를 먼저 잽니다(같은 빌드 N회, p50의 표준편차나 min-max 범위). 차이가 그보다 작으면 노이즈입니다. 엄밀하게는 두 분포에 Mann-Whitney 같은 비모수 검정을 씁니다(분포가 정규가 아니므로). |
| Does the measurement perturb what you measure? | 네. 카운터 읽기, 샘플 저장(캐시 라인 오염), printf가 영향을 줍니다. 루프 안에서는 저장만 하고, op가 오버헤드보다 짧으면 N번을 묶어 한 샘플로 재고 N으로 나눕니다. 인터럽트는 측정 구간 동안 마스킹하거나, 남기고 outlier로 셉니다. |
| Why insertion sort? | n ≤ 4096이고 리포트 단계라 단순함이 우선입니다. 더 크면 heapsort(추가 메모리 0), 또는 정해진 bin의 히스토그램으로 O(n) 근사 percentile을 냅니다. 원래 순서(시계열)가 필요하면 복사본을 정렬합니다. |

## 7. 자주 하는 실수

- 64-bit나 signed로 바꿔서 `t1 - t0` → wrap 구간에서 음수/거대한 값 (테스트 2번)
- calibration에 평균이나 첫 값을 씀 → 모든 샘플이 과하게 빠짐 (테스트 3번)
- `raw - ovh`를 그냥 빼서 unsigned underflow
- 합계를 `uint32_t`로 누적 → 0xF0000000 × 4에서 넘침 (테스트 4번 마지막)
- p99를 `sorted[n * 99 / 100]`으로 계산 → rank와 index를 섞은 off-by-one (n = 100이면 index 99 = rank 100 = max가 되어 버림). `rank = ceil(p·n/100)`, `index = rank − 1`로 나눠 씁니다
- `n == 0`에서 `sum / n` → 0으로 나누기 (테스트 5번)
- `bytes * mhz / cycles`를 그대로 곱함 → 큰 값에서 64-bit 오버플로 (테스트 6번)
- warm-up 호출을 아예 안 하거나, warm-up 결과도 samples에 저장
