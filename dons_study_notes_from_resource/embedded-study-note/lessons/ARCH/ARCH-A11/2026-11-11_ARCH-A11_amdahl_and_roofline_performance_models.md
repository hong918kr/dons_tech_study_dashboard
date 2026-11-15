---
id: ARCH-A11
time: 120분
updated: 2026-11-11
---
# ARCH-A11 성능 모델: Amdahl과 Roofline

## 학습 목표

- Amdahl `S(p) = 1/(f + (1-f)/p)` 와 그 역함수인 **Karp–Flatt** `e = (1/S − 1/p)/(1 − 1/p)` 를 손으로 쓰고, 측정한 속도 향상에서 "실험적 직렬 비율" 을 뽑아 **e 가 p 와 함께 커지면 직렬 코드가 아니라 오버헤드·불균형** 이라고 진단할 수 있다.
- Apple M6 의 2 Super + 4 Performance + 6 Efficiency 코어에서 직접 잰 숫자로 "모든 코어가 같다" 는 Amdahl 가정이 깨지는 모습(정적 분할은 가장 느린 코어가 결정)을 보여 주고, 동적 분할로 고칠 수 있다.
- NEON `fmla.4s` 피크를 **누산기 수 스윕** 으로 재서 FMA 지연(latency) × 파이프 수 = 필요한 독립 체인 수를 계산하고, FLOP 을 정확히 센다 (1 FMA = 2 FLOP × 4 lane).
- STREAM 식 copy/scale/add/triad 로 DRAM 대역폭(1 스레드, 다중 스레드)과 캐시 계층별 대역폭을 재서 **루프라인** `attainable = min(peak, AI × BW)` 을 실측 숫자로 그린다.
- vadd · matmul(naive/tiled) · conv1d(naive/blocked) 의 연산 강도(AI)를 compulsory 트래픽 가정으로 계산하고, 측정 GFLOP/s 를 루프라인 위에 찍어 메모리/연산 바운드를 판정하고 **지붕까지의 간격(gap)** 을 벡터화·의존 체인·load/store 로 설명한다.

## 1. 문제

"이 커널은 메모리 바운드인가, 연산 바운드인가?", "코어를 12개 썼는데 왜 5배밖에 안 빨라졌나?" — 성능 엔지니어링·AI 가속기·실리콘 검증 면접의 첫 질문이고, 칩 팀에서는 **새 칩이 스펙 시트의 FLOP/s 와 GB/s 를 실제로 내는지**, 커널이 그 지붕의 몇 % 인지로 버그와 튜닝 여지를 가른다. 답의 도구는 두 개다: 병렬화의 상한을 주는 **Amdahl 의 법칙**, 그리고 한 코어(또는 칩)의 상한을 연산 강도로 주는 **루프라인(roofline)**.

이미 아는 것(다시 가르치지 않는다): AMAT 와 캐시 계층(ARCH-B10), 캐시 친화 루프 순서와 타일링·2의 거듭제곱 충돌·`-Rpass` 함정(ARCH-M07 — 이 레슨의 matmul 은 그 fp32 판), false sharing 과 이 맥의 측정 방식·코어 번호 추정 매핑(ARCH-M08), 메모리 순서(ARCH-A05). 다음 단계: MEM-A08 (계층 루프라인 본격), AIPERF-B04/B06 (AI 커널 루프라인), AIPERF-A08 (DVFS 와 성능/전력).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a11a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">두 모델 한눈에 — 왼쪽: Amdahl (코어 수 → 속도 향상), 오른쪽: 루프라인 (연산 강도 → 성능 상한)</text>
  <line x1="50" y1="290" x2="330" y2="290" stroke="currentColor" marker-end="url(#a11a)"/>
  <line x1="50" y1="290" x2="50" y2="40" stroke="currentColor" marker-end="url(#a11a)"/>
  <text x="250" y="310" fill="currentColor" font-size="12">코어 수 p</text>
  <text x="56" y="48" fill="currentColor" font-size="12">S(p)</text>
  <line x1="50" y1="290" x2="300" y2="60" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="250" y="80" fill="currentColor" font-size="12">이상 S = p</text>
  <path d="M50 290 Q120 170 200 150 T320 138" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="200" y="138" fill="currentColor" font-size="12">f = 0.05</text>
  <line x1="50" y1="120" x2="320" y2="120" stroke="currentColor" stroke-dasharray="2 4"/>
  <text x="56" y="114" fill="currentColor" font-size="12">한계 1/f = 20</text>
  <path d="M50 290 Q120 230 200 225 T320 222" fill="none" stroke="currentColor" stroke-width="2" class="f-warn"/>
  <text x="170" y="245" fill="currentColor" font-size="12">이기종 + 정적 분할</text>
  <line x1="410" y1="290" x2="720" y2="290" stroke="currentColor" marker-end="url(#a11a)"/>
  <line x1="410" y1="290" x2="410" y2="40" stroke="currentColor" marker-end="url(#a11a)"/>
  <text x="500" y="310" fill="currentColor" font-size="12">연산 강도 AI (FLOP/byte, log)</text>
  <text x="416" y="48" fill="currentColor" font-size="12">GFLOP/s (log)</text>
  <line x1="420" y1="270" x2="560" y2="100" stroke="currentColor" stroke-width="2"/>
  <line x1="560" y1="100" x2="710" y2="100" stroke="currentColor" stroke-width="2"/>
  <text x="430" y="200" fill="currentColor" font-size="12" transform="rotate(-50 430 200)">AI × BW (메모리 지붕)</text>
  <text x="600" y="92" fill="currentColor" font-size="12">peak (연산 지붕)</text>
  <line x1="560" y1="100" x2="560" y2="290" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="520" y="285" fill="currentColor" font-size="12">릿지 = peak/BW</text>
  <circle cx="470" cy="250" r="6" class="f-warn" stroke="currentColor"/>
  <text x="480" y="254" fill="currentColor" font-size="12">메모리 바운드</text>
  <circle cx="650" cy="160" r="6" class="f-ok" stroke="currentColor"/>
  <text x="540" y="180" fill="currentColor" font-size="12">연산 바운드 (지붕 아래 = gap)</text>
  <line x1="650" y1="154" x2="650" y2="106" stroke="currentColor" marker-end="url(#a11a)"/>
</svg>
```

체크 테스트(`starter.c` → `solution.c`)는 **결정적인 것만** 본다: 모델 수식, 분할 코드가 일을 빠짐없이 한 번씩 하는지, 커널 결과, 바이트·FLOP 세기. **시간은 `demo_amdahl.c`, `demo_roofline.c` 에서만** 잰다 (두 데모는 `solution.c` 를 `main` 이름만 바꿔 포함 — 커널 코드가 하나).

### Part A — Amdahl · Karp–Flatt · 이기종 코어

- `amdahl_speedup(f, p)`, `karp_flatt(S, p)` (p ≤ 1 이면 NAN) — 체크: Karp–Flatt 가 Amdahl 의 정확한 역함수 (f 8개 × p 2..64).
- `hetero_speedup(f, speed[], p, split)` — 코어마다 속도가 다를 때. 정적 분할(같은 양) = 가장 느린 코어가 끝나야 끝, 동적 분할 = 속도의 합. 체크: 같은 속도면 Amdahl 과 같다, M6 모양(1, 1, 0.8×4, 0.4×6 — 예시 값)에서 **E 코어 6개를 정적 분할로 더하면 이득 0**.
- `static_range` (균형 분할), 동적 분할 루프 (`atomic_fetch_add` 로 chunk 씩) — 체크: 1..12 스레드 × 두 방식 모두 결과 합이 같고 단위를 빠짐없이 한 번씩.
- 관찰 (`demo_amdahl.c`): f ∈ {0, 0.05, 0.2} × p ∈ {1..12} 측정 속도 향상 vs Amdahl, 실험적 e, 스레드별 시간 차이.

### Part B — 루프라인 재료: 피크 FLOP/s 와 STREAM 대역폭

- `roofline`, `ridge_point`, `fma_flops` (1 FMA = 2 FLOP/lane), `stream_bytes` (STREAM 규칙 + RFO 옵션), `stream_flops`, `stream_kernel` 의 add/triad.
- 피크 커널 `peak_fma_N` (주어짐): N 개 독립 `float32x4_t` 누산기 — 체크: 스칼라 `fmaf` 기준과 **비트 단위로 같다** (FLOP 을 세려면 먼저 그 일을 했어야).
- 관찰 (`demo_roofline.c`): 누산기 1..24 스윕, 1..12 스레드 피크, STREAM 1..12 스레드, triad 작업 집합 48 KB → 384 MB.

### Part C — 커널을 지붕 위에 찍기

- `mm_tiled` (fp32, ARCH-M07 구조), `conv1d_blocked` (출력 16개 레지스터 블로킹), `cost_vadd/cost_matmul/cost_conv1d` (compulsory, RFO 옵션), `bound_of`.
- 체크: 정수 입력에서 naive 와 결과가 정확히 같음 (T 가 n 의 약수가 아닐 때, n < 16 일 때 포함), 실수 입력 conv 는 허용 오차, AI 값 (vadd 1/12, matmul n/6, conv K/4), 판정.
- 관찰: 1 스레드 · 12 스레드 GFLOP/s → 루프라인 svg (실측 숫자), `-ffast-math` 빌드와 Accelerate `cblas_sgemm` 천장(라벨 붙임).

## 2. Problem (English)

You are asked to bound the performance of a few kernels on an Apple M6 laptop (2 Super + 4 Performance + 6 Efficiency cores, NEON 128-bit) and to explain every gap between the bound and the measurement.

**Part A — Amdahl.** Implement `double amdahl_speedup(double f, int p)`, `double karp_flatt(double S, int p)` (NAN for `p <= 1` or `S <= 0`), and `double hetero_speedup(double f, const double *speed, int p, split_t split)` where the serial part runs on core 0 and the parallel part is split either statically (equal shares → the slowest core finishes last) or dynamically (ideal work sharing → sum of speeds). Implement the balanced `static_range(n, p, t, &lo, &hi)` and the dynamic chunk loop of a pthreads kernel whose serial fraction is a parameter. The kernel must give the same result for 1..12 threads and both splits. Then measure speedup for f ∈ {0, 0.05, 0.2}, report the Karp–Flatt serial fraction, and show where "identical cores" breaks.

**Part B — Roofline ceilings.** Implement `roofline(peak, bw, ai) = min(peak, ai·bw)`, `ridge_point`, `fma_flops(n_fma, lanes)` (1 FMA = 2 FLOP per lane), `stream_bytes(kernel, n, elem, rfo)` with STREAM counting rules (copy/scale 2 words, add/triad 3 words, +1 if counting write-allocate), `stream_flops`, and the add/triad bodies. Measure single-thread and all-thread peak fp32 FLOP/s with enough independent accumulators to hide FMA latency (show the `fmla` in the asm), and STREAM bandwidth on arrays much larger than the 8–20 MB L2 (≥ 256 MB total).

**Part C — Kernels on the roofline.** Implement a tiled fp32 matmul and a register-blocked conv1d (16 outputs per block). Implement `cost_vadd`, `cost_matmul`, `cost_conv1d` (compulsory traffic: inputs read once, output written once; optional RFO) and `bound_of(ai, peak, bw)`. Measure vadd, naive/tiled matmul, naive/blocked conv1d (K = 3 and 64), place them on the measured roofline, classify each as memory- or compute-bound and explain the distance to the roof.

Examples: `amdahl_speedup(0.05, 12) = 7.742`; `karp_flatt(amdahl_speedup(f, p), p) == f`; with speeds {1,1,0.8×4,0.4×6} and f = 0, static split gives 4.8 and dynamic 7.6; `stream_bytes(TRIAD, 1000, 8, false) = 24000`; AI of vadd = 1/12, of n×n matmul = n/6, of conv1d ≈ K/4 FLOP/B.

Follow-ups: Why does adding efficiency cores make a statically split job slower? Your Karp–Flatt fraction grows with p — what does that tell you? How many accumulators do you need to reach peak, and why? Why is STREAM triad bandwidth not the DRAM spec number? Why can a kernel with AI 170 FLOP/B still be 50× below the compute roof?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/ARCH/ARCH-A11
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
mkdir -p /tmp/esn_ARCH-A11
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o /tmp/esn_ARCH-A11/st && /tmp/esn_ARCH-A11/st; echo "exit $?"
```

실제 출력 (발췌):

```text
-- Part A: Amdahl, Karp-Flatt, heterogeneous cores, pthreads kernel --
FAIL  amdahl_speedup(0.0, 12) == 12.0  (line 319)
FAIL  amdahl_speedup(1.0, 12) == 1.0  (line 320)
...
      f=0, M6-shaped speeds: static 0.000  dynamic 0.000  (Amdahl identical cores: 12)
...
FAIL  all  (line 387)
FAIL  cover  (line 388)
-- Part B: roofline, FMA FLOP counting, STREAM byte rules --
FAIL  roofline(100.0, 50.0, 0.5) == 25.0  (line 395)
...
-- Part C: kernels (fp32) + arithmetic intensity + bound verdict --
FAIL  mm_check(1, 1)  (line 443)
...
FAIL  bound_of(2.0, 100, 50) == BOUND_COMPUTE  (line 475)
15/75 passed
exit 1
```

시작 코드에서도 통과하는 15개는 두 종류다: 주어진 코드에 대한 체크 (피크 커널 = `fmaf` 기준 3개, `vadd`, 전부 직렬인 `par_kernel` 경로, 잘못된 p) 와, 스텁이 돌려주는 0 이 우연히 맞는 비교 (`hetero == amdahl` 이 0 == 0, `bound_of` 의 MEMORY 쪽 등). 스텁은 무한 루프나 UB 없이 0 을 돌려준다 (`static_range` 스텁은 빈 구간).

## 4. 힌트

<details><summary>힌트 1 — Karp–Flatt 는 Amdahl 을 f 에 대해 푼 것</summary>

`1/S = f + (1-f)/p` 를 f 로 풀면 `f = (1/S − 1/p) / (1 − 1/p)`. 그래서 Amdahl 을 그대로 따르는 기계에서는 모든 p 에서 같은 값이 나온다. 측정 S 를 넣었는데 p 가 클수록 e 가 커지면, 그 증가분은 "코드의 직렬 부분" 이 아니라 p 에 따라 커지는 비용(스레드 생성, 동기화, 부하 불균형, 공유 자원 경쟁)이다.
</details>

<details><summary>힌트 2 — 이기종 정적 분할은 "가장 느린 코어 × p"</summary>

일을 p 등분하면 각자 `(1-f)/p` 를 하고, 시간은 `(1-f)/p / speed_i`. 끝나는 시각은 그중 최대 = 가장 느린 코어. f = 0 이면 속도 향상 = `p × min(speed)`. Super 2 + P 4 (예: 1.0, 0.8) 에서 6 스레드면 4.8, E 하나(0.4)를 넣어 7 스레드면 2.8 로 **떨어진다**. 동적 분할은 빨리 끝난 코어가 더 가져가므로 `Σ speed`.
</details>

<details><summary>힌트 3 — 균형 분할과 동적 chunk</summary>

`q = n / p, r = n % p` 이면 앞의 r 조각이 q+1 개, 나머지가 q 개. t 번째 조각 시작 = `t·q + min(t, r)`. 동적 분할은 `b = atomic_fetch_add(&next, chunk)` 가 n 이상이면 끝, 아니면 `[b, min(b + chunk, n))`. 순서는 상관없다 — 결과가 합(mod 2^64)이라 교환·결합 법칙이 성립한다 (부동소수 합이었다면 스레드 수마다 결과가 달라졌을 것).
</details>

<details><summary>힌트 4 — FLOP 과 바이트를 세는 규칙</summary>

`fmla.4s` 하나 = lane 4 × (곱 1 + 덧셈 1) = 8 FLOP. STREAM 은 원소당 copy 16 B, triad 24 B (double) 로 센다 — 쓰기 전에 그 라인을 읽어 오는 RFO(read for ownership) 는 세지 않는 것이 STREAM 규칙이다. AI 계산의 "compulsory" 는 각 입력을 DRAM 에서 한 번 읽고 출력을 한 번 쓴다는 가정 → 실제 트래픽의 **하한**, 그래서 AI 의 **상한**.
</details>

<details><summary>힌트 5 — conv1d 를 벡터화하려면 출력 방향으로</summary>

naive 의 안쪽 k 루프는 합(reduction)이다. IEEE 순서를 지키면 덧셈을 재배치할 수 없어 lane 4 개를 동시에 더할 수 없다. 출력 16 개를 묶어 `acc[j] += w[k] * x[i+j+k]` (j 가 안쪽) 로 쓰면 j 방향은 서로 독립 → `fmla.4s` 4 개. 출력마다 k 순서는 그대로라 결과도 (fused 여부를 빼면) 같다.
</details>

## 5. 풀이

측정 환경 (모든 표 공통): Apple M6 (Super 2 + Performance 4 + Efficiency 6, `hw.perflevel0/1/2.l2cachesize` 20 MB / 20 MB / 8 MB, `hw.cachelinesize` 128), `Apple clang version 21.0.0 (clang-2100.0.123.102)`, `-O2` (따로 적은 곳만 `-ffast-math`). QoS USER_INTERACTIVE 요청 — 힌트일 뿐 코어 고정이 아니다. **다른 세션들이 같은 기계에서 빌드·벤치를 돌리고 있었다: 측정 동안 1분 load average 10.5–13.2** (측정 직전 한때 140 까지 올라간 것을 보고 내려오기를 기다렸다). 그래서 1 스레드 숫자는 비교적 믿을 만하지만, **6 스레드 이상 숫자는 다른 일과 코어를 나눠 쓴 하한값** 이다. 5회 중앙값 (괄호는 min–max).

```bash
cd lessons/ARCH/ARCH-A11 && export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -O2 -pthread demo_amdahl.c   -o /tmp/esn_ARCH-A11/amd     # ./amd  |  ./amd percpu 12
cc -std=c11 -O2 -pthread demo_roofline.c -o /tmp/esn_ARCH-A11/roof    # ./roof peak | percpu 12 | peakmt [dyn] | stream 0 [dyn] | hier | kernels | kernelsmt 6 dyn | qos
cc -std=c11 -O2 -ffast-math -pthread demo_roofline.c -o /tmp/esn_ARCH-A11/roof_fm          # ./roof_fm kernels (NaN 관련 경고 2 개는 -ffast-math 때문, 무시)
cc -std=c11 -O2 -pthread -DUSE_ACCELERATE -DACCELERATE_NEW_LAPACK demo_roofline.c -framework Accelerate -o /tmp/esn_ARCH-A11/roof_blas   # ./roof_blas blas
uptime                                                                # 측정 전후 load average 확인 (데모도 시작/끝에 찍는다)
```

### Part A — Amdahl 이 가정하는 것, 그리고 이 맥이 깨는 것

수식 두 줄:

| 식 | 뜻 |
|---|---|
| `S(p) = 1 / (f + (1-f)/p)` | 1 코어 시간 1 중 f 는 직렬, 나머지는 p 코어가 **완벽히 같은 속도로** 나눔. `p → ∞` 이면 `1/f` |
| `e = (1/S − 1/p) / (1 − 1/p)` | Karp–Flatt: 측정 S 를 넣어 f 를 거꾸로 푼 것. Amdahl 이 맞으면 모든 p 에서 같은 값 |

Amdahl 의 숨은 가정은 셋이다: (1) 모든 코어가 같은 속도, (2) T1 을 잰 코어도 그 속도, (3) 병렬화 비용(생성·동기화·경쟁)이 0. 이 맥에서 셋 다 깨지는 것을 숫자로 본다.

**코어 종류별 속도 (가정 1, 2)** — `demo_roofline percpu`, `demo_amdahl percpu`: 스레드 12개가 짧은 조각 (roofline: add 체인 약 2 ms, amdahl: `unit_work` 1000 개 약 0.2 ms) 을 반복하며 조각 앞뒤의 `pthread_cpu_number_np` 가 같은 조각만 모았다 (이동한 조각은 버림 — roofline 12 스레드에서 3600 조각 중 1274 개가 2 ms 안에 코어를 옮겼다). load avg 10.5–11.2.

| 코어 번호 | 의존 add 체인 (≈ 클럭, G/s) | `peak_fma_16` GFLOP/s | `peak_fma_24` | 이 레슨의 정수 커널 `unit_work` (단위/µs) |
|---|---|---|---|---|
| 0–5 (E) | 2.91 | 70.5 | 70.5 | **5.08** |
| 6–7 (Super) | 4.47 | 127.0 | **142.1** | 4.42 |
| 8–11 (P) | 4.47 | **107.3** | 87.1 | 4.00 |
| 1 스레드만 돌 때 6–11 | 4.09–4.22 (p90 4.35–4.48) | 94–121 | 78–135 | 3.66–4.05 |

네 가지가 보인다.

1. **ARCH-M08 의 추정 매핑이 맞다**: 0–5 번만 2.9 GHz 대 → E. 6–7 (Super) 과 8–11 (P) 은 클럭이 같지만 FMA 처리량이 다르다 (Super 는 누산기 24 개에서 `fmla.4s` 3.97 개/사이클, P 는 16 개에서 3.0 개/사이클이고 24 개면 오히려 떨어진다 — 이유는 Apple 미공개, 레지스터 리네이밍/스케줄러 차이로 추정만).
2. **"빠른 코어" 는 커널마다 다르다**: 시프트 붙은 `eor` 3 개짜리 의존 체인인 `unit_work` 에서는 클럭이 가장 낮은 **E 코어가 가장 빠르다** (5.08 vs 4.00). 역산하면 E 는 반복당 2.9e3/5.08/200 ≈ 2.85 사이클 (`eor` 하나 ≈ 1 사이클), P 는 4.47e3/4.00/200 ≈ 5.6 사이클 (≈ 2 사이클) — 명령 지연이 코어 설계마다 다르다는 해석 (Apple 미공개, 측정에서 역산한 추정). FMA 에서는 반대로 E 가 가장 느리다.
3. **QoS 로 코어 종류를 고를 수 없었다**: `roof qos` 에서 1 스레드를 QoS BACKGROUND 로 띄워도 add 4.06 / 3.98 G/s, 코어 번호 7–11 — 이 부하 상황에서는 E 로 가지 않았다. "E 코어 성능" 은 위처럼 코어 번호로 걸러야 보인다.
4. **혼자 돌 때 클럭이 더 낮다**: 1 스레드면 6–11 번 add 속도 중앙값이 4.09–4.22, 12 스레드면 4.47. 그래서 T1 (3.73 단위/µs, 804 ms) 자체가 "가장 빠른 코어의 최고 클럭" 이 아니다.

**측정 속도 향상** (`demo_amdahl`, W = 3 000 000 단위, chunk 1024, 5회 중앙값, load avg 11.2–12.7):

| p | Amdahl f=0.05 | f=0.05 정적 S / e | f=0.05 동적 S / e | Amdahl f=0.2 | f=0.2 동적 S / e | f=0 정적 S | f=0 동적 S / e |
|---|---|---|---|---|---|---|---|
| 2 | 1.90 | 1.98 / 0.011 | 1.99 / 0.004 | 1.67 | 1.69 / 0.181 | 2.07 | 2.08 / −0.040 |
| 4 | 3.48 | 3.76 / 0.021 | 3.84 / 0.013 | 2.50 | 2.62 / 0.176 | 4.25 | 4.57 / −0.042 |
| 6 | 4.80 | 5.14 / 0.033 | 5.45 / 0.020 | 3.00 | 3.19 / 0.176 | 6.79 | 7.12 / −0.032 |
| 8 | 5.93 | 6.02 / 0.047 | 6.06 / 0.046 | 3.33 | 3.55 / 0.179 | 8.57 | 9.67 / −0.025 |
| 10 | 6.90 | 6.38 / 0.063 | 6.52 / 0.059 | 3.57 | 3.40 / 0.216 | 10.31 | 11.12 / −0.011 |
| 12 | 7.74 | 6.68 / 0.072 | 6.93 / 0.067 | 3.75 | 3.42 / 0.228 | 11.24 | 12.46 / −0.003 |

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">실측 속도 향상 (동적 분할, 5회 중앙값, load avg 11–12) vs Amdahl — demo_amdahl.c</text>
  <line x1="60" y1="300" x2="560" y2="300" stroke="currentColor"/>
  <line x1="60" y1="300" x2="60" y2="40" stroke="currentColor"/>
  <text x="98" y="316" fill="currentColor">1</text>
  <text x="139" y="316" fill="currentColor">2</text>
  <text x="223" y="316" fill="currentColor">4</text>
  <text x="306" y="316" fill="currentColor">6</text>
  <text x="389" y="316" fill="currentColor">8</text>
  <text x="473" y="316" fill="currentColor">10</text>
  <text x="556" y="316" fill="currentColor">12</text>
  <text x="38" y="304" fill="currentColor">0</text>
  <line x1="60" y1="300" x2="560" y2="300" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="38" y="252" fill="currentColor">3</text>
  <line x1="60" y1="248" x2="560" y2="248" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="38" y="200" fill="currentColor">6</text>
  <line x1="60" y1="196" x2="560" y2="196" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="38" y="148" fill="currentColor">9</text>
  <line x1="60" y1="144" x2="560" y2="144" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="38" y="96" fill="currentColor">12</text>
  <line x1="60" y1="92" x2="560" y2="92" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="38" y="44" fill="currentColor">15</text>
  <line x1="60" y1="40" x2="560" y2="40" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="280" y="332" fill="currentColor">스레드 수 p</text>
  <text x="64" y="34" fill="currentColor">S = T1/Tp</text>
  <polyline points="101.7,282.7 112.1,278.3 122.5,274.0 132.9,269.7 143.3,265.3 153.8,261.0 164.2,256.7 174.6,252.3 185.0,248.0 195.4,243.7 205.8,239.3 216.2,235.0 226.7,230.7 237.1,226.3 247.5,222.0 257.9,217.7 268.3,213.3 278.8,209.0 289.2,204.7 299.6,200.3 310.0,196.0 320.4,191.7 330.8,187.3 341.2,183.0 351.7,178.7 362.1,174.3 372.5,170.0 382.9,165.7 393.3,161.3 403.8,157.0 414.2,152.7 424.6,148.3 435.0,144.0 445.4,139.7 455.8,135.3 466.2,131.0 476.7,126.7 487.1,122.3 497.5,118.0 507.9,113.7 518.3,109.3 528.8,105.0 539.2,100.7 549.6,96.3 560.0,92.0" fill="none" stroke="currentColor" stroke-dasharray="5 3"/>
  <circle cx="101.7" cy="282.7" r="4.5" class="f-mut" stroke="currentColor"/>
  <circle cx="143.3" cy="263.9" r="4.5" class="f-mut" stroke="currentColor"/>
  <circle cx="185.0" cy="243.3" r="4.5" class="f-mut" stroke="currentColor"/>
  <circle cx="226.7" cy="220.8" r="4.5" class="f-mut" stroke="currentColor"/>
  <circle cx="310.0" cy="176.6" r="4.5" class="f-mut" stroke="currentColor"/>
  <circle cx="393.3" cy="132.4" r="4.5" class="f-mut" stroke="currentColor"/>
  <circle cx="476.7" cy="107.3" r="4.5" class="f-mut" stroke="currentColor"/>
  <circle cx="560.0" cy="84.0" r="4.5" class="f-mut" stroke="currentColor"/>
  <polyline points="101.7,282.7 112.1,278.6 122.5,274.6 132.9,270.8 143.3,267.0 153.8,263.3 164.2,259.7 174.6,256.2 185.0,252.7 195.4,249.4 205.8,246.1 216.2,242.9 226.7,239.7 237.1,236.6 247.5,233.6 257.9,230.7 268.3,227.8 278.8,224.9 289.2,222.2 299.6,219.5 310.0,216.8 320.4,214.2 330.8,211.6 341.2,209.1 351.7,206.7 362.1,204.3 372.5,201.9 382.9,199.6 393.3,197.3 403.8,195.0 414.2,192.8 424.6,190.7 435.0,188.6 445.4,186.5 455.8,184.4 466.2,182.4 476.7,180.5 487.1,178.5 497.5,176.6 507.9,174.7 518.3,172.9 528.8,171.1 539.2,169.3 549.6,167.5 560.0,165.8" fill="none" stroke="currentColor" stroke-dasharray="5 3"/>
  <circle cx="101.7" cy="282.7" r="4.5" class="f-acc" stroke="currentColor"/>
  <circle cx="143.3" cy="265.5" r="4.5" class="f-acc" stroke="currentColor"/>
  <circle cx="185.0" cy="249.2" r="4.5" class="f-acc" stroke="currentColor"/>
  <circle cx="226.7" cy="233.4" r="4.5" class="f-acc" stroke="currentColor"/>
  <circle cx="310.0" cy="205.5" r="4.5" class="f-acc" stroke="currentColor"/>
  <circle cx="393.3" cy="195.0" r="4.5" class="f-acc" stroke="currentColor"/>
  <circle cx="476.7" cy="187.0" r="4.5" class="f-acc" stroke="currentColor"/>
  <circle cx="560.0" cy="179.9" r="4.5" class="f-acc" stroke="currentColor"/>
  <polyline points="101.7,282.7 112.1,279.4 122.5,276.4 132.9,273.6 143.3,271.1 153.8,268.8 164.2,266.7 174.6,264.7 185.0,262.9 195.4,261.1 205.8,259.6 216.2,258.1 226.7,256.7 237.1,255.4 247.5,254.1 257.9,253.0 268.3,251.9 278.8,250.8 289.2,249.8 299.6,248.9 310.0,248.0 320.4,247.2 330.8,246.3 341.2,245.6 351.7,244.8 362.1,244.1 372.5,243.5 382.9,242.8 393.3,242.2 403.8,241.6 414.2,241.1 424.6,240.5 435.0,240.0 445.4,239.5 455.8,239.0 466.2,238.5 476.7,238.1 487.1,237.7 497.5,237.2 507.9,236.8 518.3,236.4 528.8,236.1 539.2,235.7 549.6,235.3 560.0,235.0" fill="none" stroke="currentColor" stroke-dasharray="5 3"/>
  <circle cx="101.7" cy="282.8" r="4.5" class="f-warn" stroke="currentColor"/>
  <circle cx="143.3" cy="270.7" r="4.5" class="f-warn" stroke="currentColor"/>
  <circle cx="185.0" cy="260.7" r="4.5" class="f-warn" stroke="currentColor"/>
  <circle cx="226.7" cy="254.6" r="4.5" class="f-warn" stroke="currentColor"/>
  <circle cx="310.0" cy="244.7" r="4.5" class="f-warn" stroke="currentColor"/>
  <circle cx="393.3" cy="238.5" r="4.5" class="f-warn" stroke="currentColor"/>
  <circle cx="476.7" cy="241.1" r="4.5" class="f-warn" stroke="currentColor"/>
  <circle cx="560.0" cy="240.7" r="4.5" class="f-warn" stroke="currentColor"/>
  <rect x="555.0" y="37.9" width="10" height="10" class="f-ok" stroke="currentColor"/>
  <text x="390" y="47" fill="currentColor">이기종 모델 예측 14.8 →</text>
  <text x="568" y="88" fill="currentColor">12.46</text>
  <text x="568" y="164" fill="currentColor">7.74 (Amdahl)</text>
  <text x="568" y="192" fill="currentColor">6.93</text>
  <text x="568" y="233" fill="currentColor">3.75 (Amdahl)</text>
  <text x="568" y="253" fill="currentColor">3.42</text>
  <circle cx="600" cy="60" r="4.5" class="f-mut" stroke="currentColor"/><text x="610" y="64" fill="currentColor">f = 0</text>
  <circle cx="600" cy="80" r="4.5" class="f-acc" stroke="currentColor"/><text x="610" y="84" fill="currentColor">f = 0.05</text>
  <circle cx="600" cy="100" r="4.5" class="f-warn" stroke="currentColor"/><text x="610" y="104" fill="currentColor">f = 0.2</text>
  <line x1="592" y1="120" x2="606" y2="120" stroke="currentColor" stroke-dasharray="5 3"/><text x="610" y="124" fill="currentColor">Amdahl 예측</text>
  <rect x="595" y="135" width="10" height="10" class="f-ok" stroke="currentColor"/><text x="610" y="144" fill="currentColor">코어별 실측 속도로</text>
  <text x="610" y="160" fill="currentColor">만든 hetero 예측</text>
</svg>
```

읽는 법:

- **p ≤ 8 에서는 Amdahl 보다 빠르다 (e < f, f=0 이면 e < 0 = 초선형)**. 직렬 코드가 줄어든 게 아니라 **가정 2 가 깨졌다**: T1 은 혼자 도는 느린 클럭(4.09–4.22)에서 쟀고, 여러 스레드일 때는 4.47 + E 코어가 이 커널에서 더 빠르다.
- 코어별 실측 속도를 `hetero_speedup` 에 넣으면 (속도 = 단위/µs ÷ T1 의 3.73, 동적 분할, f = 0): Σ = (2×4.42 + 4×4.00 + 6×5.08)/3.73 = **14.8** — 그림의 사각형. 실측 12.46 은 그보다 16% 낮다. 정적 분할 예측(가장 느린 P 코어 × 12 = 12.9)보다도 낮은 11.24.
- **p 가 커질수록 e 가 커지고 p ≥ 10 에서 f 를 넘는다** (f=0.05: 0.004 → 0.067, f=0.2: 0.176 → 0.228). Karp–Flatt 의 진단 그대로 "p 와 함께 커지는 비용" 이고, 이번 경우 가장 큰 몫은 **다른 세션의 부하** 다 — load average 11–12 는 평균 11–12 개의 다른 실행 가능 스레드가 12 코어를 같이 쓰고 있다는 뜻이다. 우리 스레드 12 개 중 일부는 남의 일과 시간을 나눴다. 스레드 생성(12 개, 수십 µs)과 마지막 chunk 꼬리는 120 ms 실행에서 작은 몫이다.
- **정적 vs 동적**: 동적이 거의 항상 낫다 (f=0, p=12: 11.24 vs 12.46). 정적 분할의 스레드별 시간 max/min 은 1.25 (f=0, p=12). 생각보다 작은 이유: macOS 가 스레드를 2 ms 안에도 코어 사이로 옮겨서 (위 1274/3600) 한 스레드가 E 에만 묶이지 않는다 — **OS 이동이 정적 분할의 불균형을 일부 평균내 준다**. 대신 그 평균은 보장이 아니다.

**`hetero_speedup` 이 말하는 극단** (체크 테스트, 예시 속도 1, 1, 0.8×4, 0.4×6 — "E 가 느린" 일반적인 경우): f = 0 이면 정적 4.8, 동적 7.6. 정적으로 6 스레드(빠른 코어만) 4.8 → 7 스레드 2.8 → 12 스레드 4.8 — **E 코어 6 개를 정적으로 더하면 이득 0, 하나만 더하면 손해**. 이 맥의 정수 커널처럼 E 가 빠른 경우도 있으니 "E 를 빼라" 가 아니라 **"일을 속도에 맞게 나눠라 (동적/가중 분할)"** 가 일반 규칙이다.

**Gustafson** (연습 1): Amdahl 은 "같은 문제를 더 빨리", Gustafson 은 "같은 시간에 더 큰 문제". 병렬 실행에서 잰 직렬 비율 s 로 확장 속도 향상 `p − s(p−1)`. s = 0.05, p = 12 → 11.45 (Amdahl 7.74). 둘은 모순이 아니라 **f 를 어느 실행에서 쟀냐** 의 차이다: 확장 문제의 1 코어 기준 직렬 비율 `f = s / (s + (1−s)p)` = 0.0044 를 Amdahl 에 넣으면 정확히 11.45 가 나온다 (체크로 확인). 펌웨어 예: 센서 수가 늘면 처리량이 같이 느는 파이프라인은 Gustafson 쪽, 한 프레임 지연(latency)이 목표면 Amdahl 쪽.

### Part B — 지붕 재기

#### 피크 FLOP/s: 누산기 수 = 지연 × 파이프 수

`peak_fma_N` 의 실제 기계어 (`cc -std=c11 -O2 -pthread -S -o - demo_roofline.c`, N = 16, 루프 부분):

```text
LBB8_2:                                 ; =>This Inner Loop Header: Depth=1
	fmla.4s	v2, v1, v0[0]
	fmla.4s	v25, v1, v0[0]
	fmla.4s	v24, v1, v0[0]
	...                                   ; (같은 꼴 16 개, 누산기 레지스터가 모두 다름)
	fmla.4s	v3, v1, v0[0]
	subs	x0, x0, #1
	b.ne	LBB8_2
```

`fmla.4s` 하나 = fp32 lane 4 × (곱 + 덧셈) = **8 FLOP**. 반복당 16 × 8 = 128 FLOP. 누산기는 레지스터 16 개(v2–v7, v16–v25)로 서로 독립이고, 각 누산기는 반복마다 자기 자신에 의존하는 체인이다.

`demo_roofline peak` (1 스레드, 4×10^8 `fmla` 를 누산기 수로 나눔, load avg 10.5–10.7, 같은 실행에서 의존 add 4.02 G/s):

| 누산기 | 1 | 2 | 4 | 8 | 12 | 16 | 24 |
|---|---|---|---|---|---|---|---|
| GFLOP/s 중앙값 | 8.16 | 16.22 | 32.01 | 64.11 | 92.72 | 114.73 (90.3–134.4) | 75.74 (68.1–97.2) |
| fmla/사이클 (4.02 GHz 가정) | 0.25 | 0.50 | 0.99 | 1.99 | 2.88 | 3.57 | 2.35 |

- 1 개일 때 0.25 개/사이클 → **FMA 지연 ≈ 4 사이클** (추정: 1 스레드 클럭 4.0–4.2 GHz 사이). 12 개까지 정확히 누산기 수에 비례 = 지연에 묶인 구간.
- 16 개에서 꺾이고 24 개에서 오히려 떨어진 것은 위 코어별 표가 설명한다: 실행 직후 코어 번호가 16 개 실행은 6 8 6 8 6 (주로 Super), 24 개 실행은 11 7 9 11 9 (주로 P) 였고, **P 는 16 에서 최고 (107), 24 에서 87**, Super 는 24 에서 142. 16 개의 min–max 90–134 가 넓은 것도 코어 종류가 섞여서다. 그래서 1 스레드 피크는 하나의 숫자가 아니다 — 지붕 그림에는 **Super 142 (누산기 24)** 를 1 스레드 지붕으로, **P 107** 을 점선으로 그렸다.
- 필요한 독립 체인 수 = 지연 × 처리량 = 4 × 4 = 16 (Super 는 지연이 조금 더 크거나 루프 오버헤드가 있어 24 에서야 4 개/사이클에 닿는다 — 추정).

전 코어 피크: `peakmt` (p 스레드 × 25 조각, 조각 = 1.28×10^8 FLOP, load avg 11.0–12.2):

| p | 1 | 2 | 4 | 6 | 8 | 10 | 12 |
|---|---|---|---|---|---|---|---|
| 정적 GFLOP/s | 107.4 | 180.6 | 316.9 | 428.6 | 473.2 | 399.5 | 513.6 (356.0–545.1) |
| 동적 GFLOP/s | 100.3 | 197.0 | 356.2 | 524.5 | 612.7 | **703.5** | 618.0 (269.0–697.5) |

코어별 깨끗한 조각의 합 = 2 × 142.1 + 4 × 107.3 + 6 × 70.5 = **1136 GFLOP/s** 가 이 칩 NEON fp32 의 전 코어 천장(추정)이고, 벽시계로 잰 최고는 동적 10 스레드 703.5 — 62%. 나머지는 다른 세션이 가져간 코어 시간이다 (12 스레드 동적의 조각 수가 스레드마다 17–39 로 고르지 않다). **이 레슨의 전 코어 숫자는 하한** 이라는 뜻이다.

#### 메모리 대역폭: STREAM 식 4 커널

STREAM 규칙 = 원소당 copy/scale 16 B, add/triad 24 B (double). `-Rpass=loop-vectorize` 로 보면 scale/add/triad 는 `vectorization width: 2, interleaved count: 4` (double 2 개 × 4 묶음), copy 루프는 컴파일러가 `memcpy` 호출로 바꿨다 (`bl _memcpy`) — copy 가 다른 셋보다 빠른 이유의 일부다.

`demo_roofline stream` / `stream 0 dyn` (배열 3 × 512 MB = 1.5 GB ≫ L2, GB/s = STREAM 바이트 / 시간, load avg 11.4–13.2):

| p | 정적 copy | 정적 triad | 동적 copy | 동적 triad |
|---|---|---|---|---|
| 1 | 106.5 | 102.1 (100.1–107.7) | 106.3 | 109.9 (102.3–110.7) |
| 2 | 127.8 | 104.2 | 127.1 | 105.8 |
| 4 | 125.6 | 79.5 (51.9–111.2) | 118.3 | **121.0** (119.5–123.6) |
| 8 | 122.2 | 89.1 | **144.5** | **122.6** |
| 12 | 101.8 | 62.5 (47.0–94.9) | 121.9 | 72.1 |

- **한 코어가 이미 DRAM 대역폭의 대부분을 쓴다**: 1 스레드 triad 102–110 GB/s, 전체 최고 triad 121–123, copy 144. 지붕으로는 **1 스레드 105 GB/s, 전체 125 GB/s** 를 쓴다 (copy 144 는 `memcpy` 경로라 따로 표시만). Apple 은 이 칩의 메모리 대역폭 사양을 내가 확인한 자료로는 밝히지 않았으므로 "사양 대비 몇 %" 는 말하지 않는다.
- p = 12 에서 떨어지는 것은 다른 세션과의 코어 경쟁이다 — 동적 분할에서 스레드 몫이 1.2–15.9% 로 갈렸다 (일부 스레드는 측정 구간 동안 거의 실행되지 못했다).
- **RFO 는 숫자와 맞지 않는다**: write-allocate 라면 triad 의 실제 트래픽은 원소당 32 B 이고 실제 대역폭은 표의 4/3 배여야 한다. 그런데 아래 conv1d (K=3, 출력 1 개당 x 4 B 읽기 + y 4 B 쓰기) 6 스레드가 97.2 GFLOP/s = 출력 16.2 G/s × 8 B = 130 GB/s 로 triad 125 GB/s 와 맞는다. RFO 가 있었다면 12 B × 16.2 = 194 GB/s 가 필요했다. **이 칩은 순차 쓰기에서 RFO 를 피하거나 숨긴다고 보는 것이 측정과 맞다** (추정: Arm 코어들의 write-streaming / 라인 전체 쓰기 감지 같은 기법 — Apple 미공개). ARCH-M06 의 "memset 은 RFO 로 2 배" 는 이 맥에서는 그대로 성립하지 않을 수 있다.

**캐시 계층별 1 스레드 triad** (`demo_roofline hier`, 작업 집합 = 세 배열 합, 측정 1 번에 약 1 GB 이상 반복, load avg 10.5):

| 작업 집합 | 48 KB | 1.5 MB | 6 MB | 24 MB | 384 MB |
|---|---|---|---|---|---|
| GB/s (중앙값) | **178.4** (130.2–265.2) | 122.0 | 119.2 | 122.7 | 108.1 |
| GFLOP/s (= GB/s ÷ 12) | 14.9 | 10.2 | 9.9 | 10.2 | 9.0 |

L1 에 들어갈 때만 확 빨라지고, L2(수 MB)와 DRAM 의 차이는 1 스레드에서 10–15% 뿐이다. **1 스레드 triad 는 DRAM 이 아니라 코어 쪽(동시에 처리할 수 있는 미스 수, load/store 처리량)에 묶여 있다** 는 뜻이다 (Little 의 법칙: 대역폭 = 동시 미스 수 × 128 B / 지연). 그래서 "DRAM 지붕" 도 스레드 수의 함수다 — 1 스레드 105, 전체 125.

### Part C — 커널을 지붕 위에 찍기

**연산 강도 (compulsory, fp32)**: 입력은 DRAM 에서 한 번, 출력은 한 번 쓴다고 가정 → 실제 트래픽의 하한 → AI 의 상한.

| 커널 | FLOP | 바이트 (compulsory) | AI (FLOP/B) | +RFO |
|---|---|---|---|---|
| vadd `c = a + b`, n | n | 12n | **1/12 = 0.083** | 1/16 |
| matmul n×n | 2n³ | 12n² | **n/6** (n = 1024 → 170.7) | n/8 |
| conv1d n 출력, K 탭 | 2Kn | 4(2n + 2K − 1) | **≈ K/4** (K=3 → 0.75, K=64 → 16) | ≈ K/6 |

**1 스레드 지붕**: peak 142 (Super, 누산기 24), DRAM 105 GB/s → **릿지 1.35 FLOP/B**. 전 코어: 1136 / 125 → 릿지 9.1.

**측정** (`demo_roofline kernels`, 1 스레드, load avg 10.6–11.0; `kernelsmt 6 dyn`, load avg 13.2):

| 커널 | AI | 측정 GFLOP/s | 지붕 (1 스레드) | 판정 | 지붕 대비 |
|---|---|---|---|---|---|
| vadd n = 2^25 | 0.083 | **9.23** (9.01–9.26) | 8.75 (메모리) | 메모리 바운드 | 105% (= 110.8 GB/s) |
| conv1d naive K=3 | 0.75 | 5.67 | 78.8 (메모리) | 메모리 바운드 | 7% |
| conv1d blocked K=3 | 0.75 | **53.11** (15.25–61.58) | 78.8 | 메모리 바운드 | 67% |
| conv1d naive K=64 | 16 | 9.52 | 142 (연산) | 연산 바운드 | 6.7% |
| conv1d naive K=64 `-ffast-math` | 16 | 32.77 | 142 | 연산 바운드 | 23% |
| conv1d blocked K=64 | 16 | **47.10** (44.47–48.80) | 142 | 연산 바운드 | 33% |
| matmul naive n=1024 | 170.7 | 1.58 | 142 | 연산 바운드 | 1.1% |
| matmul tiled T = 32 / 64 / 128 / 256 | 170.7 | 13.87 / 17.19 / 20.42 / **20.85** | 142 | 연산 바운드 | 15% |
| 6 스레드 동적: vadd | 0.083 | 10.38 | 10.4 (전체 125 GB/s) | 메모리 | ≈ 100% |
| 6 스레드 동적: conv1d blocked K=3 | 0.75 | 97.18 | 93.8 | 메모리 | ≈ 104% |
| 6 스레드 동적: conv1d blocked K=64 | 16 | 254.55 | 6 코어 몫 | 연산 | — |
| Accelerate `cblas_sgemm` n=1024 / 2048 | 170.7 | **1890.0** / 1743.6 | (NEON 1136 위) | 다른 지붕 | — |

다중 스레드는 6 스레드 동적을 썼다. 4 스레드 동적은 vadd 10.01, conv K=3 95.24, K=64 149.09 로 같은 그림이고, **12 스레드는 표에서 뺐다**: vadd 2.34 (정적) / 3.90 (동적) GFLOP/s 로 1 스레드보다 느렸고, 동적 분할 몫이 0.0% 인 스레드가 있었다 — 측정 구간(수십 ms) 동안 다른 세션 때문에 거의 실행되지 못한 스레드가 있었다는 뜻이다 (load avg 11–12).

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Apple M6 루프라인 (실측 지붕 + 실측 커널, fp32, log–log) — demo_roofline.c</text>
  <line x1="70" y1="360" x2="600" y2="360" stroke="currentColor"/><line x1="70" y1="360" x2="70" y2="50" stroke="currentColor"/>
  <text x="103" y="376" fill="currentColor">0.1</text><line x1="111" y1="360" x2="111" y2="50" stroke="currentColor" stroke-opacity="0.12"/>
  <text x="239" y="376" fill="currentColor">1</text><line x1="247" y1="360" x2="247" y2="50" stroke="currentColor" stroke-opacity="0.12"/>
  <text x="374" y="376" fill="currentColor">10</text><line x1="382" y1="360" x2="382" y2="50" stroke="currentColor" stroke-opacity="0.12"/>
  <text x="510" y="376" fill="currentColor">100</text><line x1="518" y1="360" x2="518" y2="50" stroke="currentColor" stroke-opacity="0.12"/>
  <text x="34" y="364" fill="currentColor">1</text><line x1="70" y1="360" x2="600" y2="360" stroke="currentColor" stroke-opacity="0.12"/>
  <text x="34" y="275" fill="currentColor">10</text><line x1="70" y1="271" x2="600" y2="271" stroke="currentColor" stroke-opacity="0.12"/>
  <text x="34" y="186" fill="currentColor">100</text><line x1="70" y1="182" x2="600" y2="182" stroke="currentColor" stroke-opacity="0.12"/>
  <text x="34" y="97" fill="currentColor">1000</text><line x1="70" y1="93" x2="600" y2="93" stroke="currentColor" stroke-opacity="0.12"/>
  <text x="240" y="392" fill="currentColor">연산 강도 AI (FLOP/byte, compulsory)</text>
  <text x="14" y="40" fill="currentColor">GFLOP/s</text>
  <polyline points="70.0,295.8 264.5,168.1 600.0,168.1" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="482" y="162" fill="currentColor">1 스레드 142 (Super)</text>
  <polyline points="70.0,295.8 247.8,179.1 600.0,179.1" fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="3 3"/>
  <text x="482" y="193" fill="currentColor">P 코어 107</text>
  <polyline points="70.0,289.0 376.8,87.6 600.0,87.6" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3"/>
  <text x="393" y="104" fill="currentColor">전 코어 합 1136 (코어별 실측의 합)</text>
  <line x1="70.0" y1="275.4" x2="233.3" y2="168.1" stroke="currentColor" stroke-dasharray="1 3"/>
  <text x="122" y="231" fill="currentColor" transform="rotate(-33 122 231)">L1 triad 178 GB/s</text>
  <text x="90" y="305" fill="currentColor" transform="rotate(-33 90 305)">DRAM 1 스레드 105</text>
  <text x="296" y="133" fill="currentColor" transform="rotate(-33 296 133)">DRAM 전체 125</text>
  <circle cx="100.1" cy="273.9" r="5" class="f-warn" stroke="currentColor"/>
  <circle cx="229.7" cy="292.8" r="5" class="f-mut" stroke="currentColor"/>
  <circle cx="229.7" cy="206.2" r="5" class="f-acc" stroke="currentColor"/>
  <circle cx="410.2" cy="272.8" r="5" class="f-mut" stroke="currentColor"/>
  <circle cx="410.2" cy="224.9" r="5" class="f-mut" stroke="currentColor"/>
  <circle cx="410.2" cy="210.8" r="5" class="f-acc" stroke="currentColor"/>
  <circle cx="549.8" cy="342.3" r="5" class="f-mut" stroke="currentColor"/>
  <circle cx="549.8" cy="242.4" r="5" class="f-acc" stroke="currentColor"/>
  <text x="108" y="266" fill="currentColor">vadd</text>
  <text x="238" y="297" fill="currentColor">c3 naive</text>
  <text x="238" y="200" fill="currentColor">c3 blk</text>
  <text x="418" y="277" fill="currentColor">c64 naive</text>
  <text x="260" y="229" fill="currentColor">c64 naive -ffast-math</text>
  <text x="418" y="207" fill="currentColor">c64 blk</text>
  <text x="480" y="346" fill="currentColor">mm naive</text>
  <text x="440" y="246" fill="currentColor">mm tiled T=256</text>
  <rect x="95.1" y="264.4" width="10" height="10" class="f-ok" stroke="currentColor"/>
  <rect x="224.7" y="177.8" width="10" height="10" class="f-ok" stroke="currentColor"/>
  <rect x="405.2" y="140.5" width="10" height="10" class="f-ok" stroke="currentColor"/>
  <text x="108" y="255" fill="currentColor">(6 스레드)</text>
  <text x="238" y="179" fill="currentColor">c3 6T</text>
  <text x="418" y="150" fill="currentColor">c64 6T</text>
  <polygon points="549.8,60.9 542.8,72.9 556.8,72.9" class="f-warn" stroke="currentColor"/>
  <text x="365" y="72" fill="currentColor">Accelerate sgemm 1890 →</text>
  <circle cx="612" cy="70" r="5" class="f-acc" stroke="currentColor"/><text x="622" y="74" fill="currentColor">1 스레드, 최적화</text>
  <circle cx="612" cy="92" r="5" class="f-mut" stroke="currentColor"/><text x="622" y="96" fill="currentColor">1 스레드, naive</text>
  <rect x="607" y="109" width="10" height="10" class="f-ok" stroke="currentColor"/><text x="622" y="118" fill="currentColor">6 스레드 동적</text>
  <polygon points="612,129 606,140 618,140" class="f-warn" stroke="currentColor"/><text x="622" y="140" fill="currentColor">행렬 유닛 천장</text>
  <text x="606" y="166" fill="currentColor">릿지: 1T 1.35</text>
  <text x="606" y="184" fill="currentColor">전체 9.1 FLOP/B</text>
</svg>
```

**gap 설명 — 지붕까지 왜 못 가나:**

- **vadd, conv K=3 (메모리 바운드)**: 1 스레드 vadd 는 지붕 위 (110.8 GB/s ≈ 1 스레드 triad 의 측정 범위 102–110 안 — 지붕 숫자 자체의 오차). 6 스레드에서 vadd 10.38, conv K=3 97.2 로 **둘 다 전체 메모리 지붕에 붙었다**. 메모리 바운드 커널은 FLOP 을 줄이는 최적화가 소용없고, 바이트를 줄이거나(데이터 타입을 fp16/int8 로, 연산 융합 = 중간 결과를 DRAM 에 쓰지 않기) 스레드를 늘려 동시 미스를 늘리는 것만 듣는다. 1 스레드 conv K=3 blocked 가 67% 에 그친 것은 K=3 이면 블록 하나가 `fmla` 12 개뿐이라 루프 진입·저장·주소 계산 명령이 비중이 커서다 (명령 수 바운드, 추정).
- **conv1d naive → blocked (연산 바운드, K=64)**: naive 의 `-O2` 기계어는 "vectorized loop" remark 가 뜨지만 실제로는 **순서 있는 합(strict in-order reduction)** 이다 — `fmul.4s` 4 개로 곱을 벡터로 한 뒤, `fadd s0, s0, s1` **16 개를 차례로** 더한다 (IEEE 순서를 지키느라). 덧셈 체인 하나가 지연에 묶여 9.5 GFLOP/s.

  ```text
  ; conv1d_naive (K >= 16 경로, demo_roofline.c -O2 -S 발췌)
  	fmul.4s	v1, v1, v5
  	fmul.4s	v2, v2, v6
  	...
  	fadd	s0, s0, s1        ; 16 개의 스칼라 fadd 가 s0 하나로 직렬
  	fadd	s0, s0, s18
  	fadd	s0, s0, s17
  	...
  ```

  `-ffast-math` 는 재결합을 허용해 `fmla.4s` 4 누산기로 바꾼다 → 32.8 (3.4 배), 대신 결과가 바뀐다. **blocked** 는 순서를 지키면서 출력 방향으로 벡터화 → 47.1:

  ```text
  ; conv1d_blocked 안쪽 k 루프 (solution.c -O2 -S)
  	ldp	q6, q4, [x13, #-32]     ; x[i+k .. i+k+7]
  	ld1r.4s	{ v5 }, [x16], #4    ; w[k] 를 4 lane 에 복사
  	fmla.4s	v0, v6, v5
  	ldr	q6, [x13]
  	fmla.4s	v1, v4, v5
  	fmla.4s	v2, v6, v5
  	ldr	q4, [x13, #16]
  	fmla.4s	v3, v4, v5
  	add	x13, x13, #4
  	subs	x14, x14, #1
  	b.ne	...
  ```

  (이 j 루프는 `-Rpass=loop-vectorize` 에 나오지 않는다 — 16 회 고정 루프를 완전히 펼친 뒤 SLP 벡터라이저가 묶었다. remark 가 없다고 스칼라라고 단정하면 안 된다.) 누산기가 **4 개(v0–v3)** 뿐이라 Part B 의 표로는 4 체인 × 8 FLOP / 4 사이클 ≈ 1 `fmla`/사이클 ≈ 36 GFLOP/s 가 상한인데, 측정은 47 이다. 출력 블록끼리는 서로 독립이라 **비순차 실행(OoO)이 다음 블록의 체인을 앞당겨 겹친 것** 으로 해석한다 (블록 하나 = 루프 몸체 11 명령 × 64 ≈ 700 명령 — Apple 코어의 재정렬 창(ROB) 크기는 미공개, 추정). 남은 gap 은 누산기를 16 개로 (출력 64 개 묶음) 늘리는 것 — 힌트 5 의 다음 단계.
- **matmul naive 1.1%, tiled 15%**: compulsory AI 170 은 "각 원소를 DRAM 에서 한 번만" 이라는 이상이다. naive 는 B 를 열로 걸어 매번 새 라인 (ARCH-M07), 덧셈 순서 때문에 스칼라 `fmadd` 하나씩 — 지연 약 4 사이클짜리 체인 1 개 → 클럭 4.1–4.5 GHz 면 2.1–2.2 GFLOP/s 가 상한이고, B 열 걷기의 캐시 미스가 더해져 측정 1.58. tiled 의 안쪽 루프는 NEON 이지만 `fmla.4s` 하나마다 B 16 B 읽기 + C 16 B 읽기 + C 16 B 쓰기:

  ```text
  ; mm_tiled 안쪽 j 루프 (solution.c -O2 -S)
  	ldp	q1, q2, [x2, #-32]     ; B[k][j..]
  	ldp	q3, q4, [x2], #64
  	ldp	q5, q6, [x1, #-32]     ; C[i][j..]
  	ldp	q7, q16, [x1]
  	fmla.4s	v5, v1, v0[0]       ; C += r * B   (4 개)
  	...
  	stp	q5, q6, [x1, #-32]     ; C 다시 저장
  	stp	q7, q16, [x1], #64
  ```

  **L1 수준의 AI = 8 FLOP / 48 B = 0.167**. 같은 "load 2 + store 1" 패턴인 triad 의 L1 속도 178 GB/s 를 곱하면 **L1 지붕 ≈ 30 GFLOP/s** — tiled 20.85 는 이 지붕의 70% 다. 즉 tiled matmul 은 DRAM 기준으론 연산 바운드지만 **실제로는 L1 load/store 바운드** (계층 루프라인, 연습 3). 다음 단계는 레지스터 블로킹 (C 조각을 레지스터에 두고 k 루프 동안 load/store 없앰 — ARCH-M07 Q8) 이다.
- **Accelerate `cblas_sgemm` 1890 GFLOP/s** (n = 1024) 는 NEON 전 코어 천장 1136 보다 위다. NEON 이 아닌 **행렬 유닛** (이 칩 `sysctl hw.optional.arm.FEAT_SME/SME2 = 1` — SME 를 쓰는지, Apple 의 다른 행렬 엔진을 쓰는지는 Apple 이 밝히지 않는다) 을 쓰는 다른 지붕이다. 루프라인은 "그 실행 유닛의 지붕" 이라 유닛이 바뀌면 지붕도 바뀐다. 라벨 붙인 참고값으로만 쓴다.

**펌웨어/실리콘 쪽 결론**: (1) 메모리 바운드 커널(vadd, 작은 K 의 conv, 배치 1 FC — 연습 4)은 코어를 늘리거나 NPU 로 옮겨도 대역폭 지붕을 못 넘는다 — DMA 로 겹치기, 데이터 타입 줄이기, 연산 융합이 답. (2) 연산 바운드라고 판정돼도 지붕의 1–33% 인 경우가 흔하고, 그 gap 은 의존 체인(누산기 수), 벡터화 여부(remark 가 아니라 asm 으로), L1 load/store 로 설명된다. (3) 새 칩 bring-up 에서는 이 레슨의 `peak`·`stream`·`percpu` 가 그대로 **"지붕이 사양대로 나오나"** 검증 테스트가 된다 — 코어 종류별로, 부하 없는 상태에서.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o /tmp/esn_ARCH-A11/so && /tmp/esn_ARCH-A11/so
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/esn_ARCH-A11/san && /tmp/esn_ARCH-A11/san
cc -std=c11 -g -O1 -fsanitize=thread solution.c -o /tmp/esn_ARCH-A11/tsan && /tmp/esn_ARCH-A11/tsan
```

```text
-- Part A: Amdahl, Karp-Flatt, heterogeneous cores, pthreads kernel --
PASS  amdahl_speedup(0.0, 12) == 12.0
...
PASS  inv
...
      f=0, M6-shaped speeds: static 4.800  dynamic 7.600  (Amdahl identical cores: 12)
PASS  near(st, 12 * 0.4, 1e-12)
PASS  near(dy, 2 + 3.2 + 2.4, 1e-12)
...
PASS  near(hetero_speedup(0.0, m6, 12, SPLIT_STATIC), hetero_speedup(0.0, m6, 6, SPLIT_STATIC), 1e-12)
...
PASS  all
PASS  cover
...
-- Part B: roofline, FMA FLOP counting, STREAM byte rules --
PASS  roofline(100.0, 50.0, 0.5) == 25.0
...
PASS  peak_fma_24(777, a, b) == peak_ref(24, 777, a, b)
-- Part C: kernels (fp32) + arithmetic intensity + bound verdict --
...
PASS  conv_check(4099, 64, false)
...
      AI: vadd 0.0833  matmul(1024) 170.7  conv1d K=3 0.7500  K=64 16.000  FLOP/B
...
PASS  bound_of(2.0, 100, 50) == BOUND_COMPUTE
75/75 passed
```

ASan+UBSan, TSan 빌드도 경고 없이 `75/75 passed`. `-pthread` 없이 (`cc -std=c11 -Wall -Wextra -Werror -O2 solution.c`) 빌드해도 macOS 에서는 pthread 가 libSystem 에 있어 같은 결과다. 체크는 시간을 보지 않으므로 부하와 무관하게 매번 같다.

구현 포인트:

| 포인트 | 이유 |
|---|---|
| `karp_flatt` 의 `!(S > 0)` | S 가 0 또는 NaN 이면 NAN — `S <= 0` 은 NaN 을 통과시킨다 |
| `hetero_speedup` 직렬 부분 = `f / speed[0]` | 직렬 코드가 느린 코어에 걸리면 Amdahl 한계 1/f 도 같이 나빠진다 |
| 동적 분할의 `memory_order_relaxed` | 카운터는 "누가 어느 조각" 만 정하면 된다. 결과는 `pthread_join` 이 happens-before 로 넘겨 준다 |
| 결과 = 정수 합 mod 2^64 | 분할 방식·스레드 수와 무관하게 비트 단위로 같다 → 체크 가능. 부동소수 합이었다면 스레드 수마다 반올림이 달라진다 |
| 정수 입력 (−4…4) 커널 테스트 | 덧셈 순서·fused 여부가 달라도 `==` 비교 가능. 실수 입력 conv 는 naive 의 벡터 경로가 `fmul`+`fadd` (곱 따로 반올림), blocked 는 `fmla` (한 번 반올림) 라 4099 개 중 2959 개가 비트가 다르고 최대 차이 1.9e-6 (K=64, 따로 확인) → 허용 오차 1e-5·K |
| 피크 커널의 `volatile` a, b | 상수면 컴파일러가 `fma(x, 1, b)` 를 `fadd` 로 바꾸거나 계산을 접을 수 있다 → FLOP 을 셀 수 없다 |

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 것 |
|---|---|---|
| 누산기 1 개로 "피크" 측정 | FMA 지연(≈ 4 사이클)에 묶여 8.2 GFLOP/s — 처리량 피크의 1/13 | 독립 누산기 ≥ 지연 × 파이프 수 (이 맥 P 16, Super 24), asm 으로 확인 |
| FMA 를 1 FLOP 으로, 또는 lane 수를 빼고 셈 | 4 배·2 배 틀린 GFLOP/s | `fmla.4s` = 4 lane × 2 = 8 FLOP |
| "1 스레드 피크" 를 하나의 숫자로 | Super 142 / P 107 / E 70.5 — 어느 코어에서 돌았냐에 따라 다르다 | 코어 번호를 같이 찍고, 코어 종류별로 따로 잰다 |
| 정적 등분 + 이기종 코어 | 가장 느린 코어가 끝나야 끝 (예시 모델: E 하나 추가로 4.8 → 2.8) | 동적/가중 분할 |
| T1 을 아무 코어에서나 | 혼자 돌 때 클럭이 낮아 (4.09–4.22 vs 4.47) 초선형·음수 Karp–Flatt | T1 을 어느 코어·몇 GHz 에서 쟀는지 적는다. 가능하면 코어 종류별 속도로 hetero 모델 |
| STREAM 조각을 반복해 시간 늘리기 | 조각(수 MB)이 L2 에 남아 DRAM 이 아니라 L2 를 잰다 — 처음 짠 데모가 이렇게 400 GB/s 를 냈다 | 배열을 키우고 (1.5 GB) 한 번씩 |
| 결과를 안 읽는 벤치 | 컴파일러가 죽은 저장을 지워 시간 0 (처음 짠 `qos` 모드의 triad 가 `inf GB/s`) | 결과를 `volatile` 싱크로 읽기 |
| `-Rpass=loop-vectorize` remark = 벡터화 성공 | conv1d naive 는 remark 가 뜨지만 순서 있는 합 (`fadd s` 16 개 직렬). blocked 의 j 루프는 remark 가 없지만 SLP 로 `fmla.4s` | `-S` 로 실제 루프 확인 |
| compulsory AI 를 "실제 AI" 로 | naive matmul AI 170 이라도 실제 L1/L2 트래픽은 훨씬 크다 | 계층별 AI (L1 0.167 등) 와 계층 지붕 |
| 공유 기계에서 12 스레드 숫자를 사양처럼 | load avg 11 이면 남의 스레드가 코어 절반을 쓴다 | `uptime` 을 같이 적고, 다중 스레드는 하한이라고 말한다 |

코드로 보면 가장 흔한 두 개:

```c
/* 틀림: 누산기 1 개 — 지연 바운드. 그리고 a, b 가 상수라 컴파일러가 접을 수 있다 */
float32x4_t acc = vdupq_n_f32(0);
for (long i = 0; i < iters; i++) acc = vfmaq_f32(acc, vdupq_n_f32(1.0f), vdupq_n_f32(2.0f));

/* 고침: 독립 누산기 N 개 (DEF_PEAK), a/b 는 volatile 에서 읽고, 결과는 싱크로 */
static volatile float g_a = 0.999999f, g_b = 1e-7f;
g_sink = peak_fma_16(iters, g_a, g_b);       /* FLOP = iters × 16 × 4 × 2 */
```

```c
/* 틀림: 조각마다 reps 번 반복 → 조각이 캐시에 남는다 */
while (range_next(&it, &lo, &hi)) for (int r = 0; r < 8; r++) stream_kernel(k, a, b, c, 3.0, lo, hi);
/* 고침: 캐시보다 훨씬 큰 배열을 한 번만 (reps 1, 3 × 512 MB) */
while (range_next(&it, &lo, &hi)) stream_kernel(k, a, b, c, 3.0, lo, hi);
```

## 8. 면접 Q&A

<details><summary>Q1. 코어 12 개로 5% 직렬인 코드를 돌리면 몇 배 빨라지나요? 측정했더니 6.9 배였다면?</summary>

Amdahl: 1/(0.05 + 0.95/12) = 7.74 배, 한계 1/f = 20 배. 6.9 배를 Karp–Flatt 에 넣으면 e = (1/6.93 − 1/12)/(1 − 1/12) = 0.067 — f 보다 크다. 이 맥 측정이 정확히 이랬다 (f = 0.05, p = 12, 동적 6.93). e 가 p 와 함께 커지면 (0.004 → 0.067) 직렬 코드가 아니라 **p 에 비례하는 비용**: 동기화, 부하 불균형, 공유 자원(메모리 대역폭, 다른 프로세스와의 코어 경쟁). 이번에는 load average 11–12 의 다른 세션이 가장 큰 원인이었다.

꼬리: "e 가 p 와 무관하게 일정하면?" → 진짜 직렬 부분이다. 그 코드를 줄이는 것(알고리즘 변경, 직렬 구간을 다른 일과 겹치기) 외엔 코어를 늘려도 1/e 를 못 넘는다.
</details>

<details><summary>Q2. big.LITTLE / P+E 코어 칩에서 Amdahl 은 어떻게 바뀌나요?</summary>

"모든 코어 같은 속도" 가정이 깨진다. 정적 등분이면 가장 느린 코어가 끝날 때까지 → `p × min(speed)`. 예시 모델(빠른 6 개 1.0/0.8, E 6 개 0.4)에서 6 스레드 4.8 → 7 스레드 2.8 → 12 스레드 4.8: **E 코어를 더해서 느려진다**. 동적 분할이면 `Σ speed`. 직렬 부분이 어느 코어에 걸리느냐도 한계 1/f 를 바꾼다. 그리고 이 맥에서 본 함정: **코어 속도는 커널마다 다르다** — FMA 에서는 E 가 70.5 vs Super 142 GFLOP/s 로 느리지만, 시프트 `eor` 체인 정수 커널에서는 E 가 5.08 vs P 4.00 단위/µs 로 더 빨랐다.

꼬리: "OS 는 이걸 어떻게 처리하나?" → 스케줄러가 스레드를 코어 사이로 계속 옮긴다 (이번 측정: 2 ms 조각의 35% 가 코어를 바꿨다). 그래서 정적 분할의 불균형이 어느 정도 평균되지만 보장은 아니다. 펌웨어(RTOS, AMP) 라면 태스크를 코어에 고정하므로 이 평균도 없다 — 분할을 직접 속도에 맞춰야 한다.
</details>

<details><summary>Q3. 피크 FLOP/s 를 어떻게 재나요? 누산기는 몇 개 필요하죠?</summary>

독립 FMA 체인을 `지연 × 파이프 수` 이상 두고, 메모리 접근 없이 레지스터만 돌린다. 이 맥: 누산기 1 개 8.16 GFLOP/s (= 0.25 `fmla`/사이클 → 지연 ≈ 4 사이클), 12 개까지 정확히 비례, P 코어는 16 개에서 107 (3.0/사이클), Super 는 24 개에서 142 (3.97/사이클). 반드시 `-S` 로 `fmla.4s` 가 N 개인지, 컴파일러가 계산을 접지 않았는지 확인하고, FLOP 은 lane 4 × 2.

꼬리: "그 숫자가 사양 피크와 다르면?" → (1) 클럭 확인 (의존 add 체인 = 1 사이클/명령 가정으로 4.47 GHz 를 역산했다), (2) 코어 종류 확인, (3) 다른 부하, (4) 그래도 낮으면 실리콘/펌웨어 (DVFS 테이블, 파이프 하나 꺼짐) 를 의심 — 이게 bring-up 의 "peak 테스트" 다.
</details>

<details><summary>Q4. STREAM triad 결과가 DRAM 사양 대역폭보다 낮은 이유는?</summary>

(1) STREAM 은 RFO 를 세지 않는다 — write-allocate 면 실제 트래픽은 triad 에서 4/3 배 (단, 이 맥에서는 conv1d 측정이 RFO 없음 쪽과 맞았다). (2) 1 코어의 동시 미스 수가 대역폭을 제한한다 (Little: BW = 동시 미스 × 라인 / 지연) — 이 맥 1 스레드 105 GB/s, 전체 125 GB/s. (3) 읽기/쓰기 전환, 리프레시, 뱅크 충돌로 DRAM 효율은 사양의 수십 % 대가 보통 (일반론). (4) 다른 마스터(GPU, 디스플레이, NPU) 와 공유.

꼬리: "SoC 검증이라면?" → 코어 수·패턴(읽기만/쓰기만/혼합)·스트라이드별 대역폭 표를 만들고, 인터커넥트 QoS·DRAM 컨트롤러 설정을 바꿔 가며 같은 표를 다시 잰다 (ARCH-A10 아비터, SIM-M05).
</details>

<details><summary>Q5. 이 커널이 메모리 바운드인지 어떻게 판단하죠?</summary>

AI = FLOP / DRAM 바이트를 계산해 릿지 포인트 (peak/BW) 와 비교한다. 이 맥 1 스레드 142/105 = 1.35, 전 코어 1136/125 = 9.1 FLOP/B. vadd 1/12, conv K=3 0.75 → 메모리 바운드. conv K=64 16, matmul 170 → 연산 바운드. **릿지가 스레드 수에 따라 움직인다** (1.35 → 9.1) — 1 스레드에서 연산 바운드인 커널이 전 코어에서는 메모리 바운드가 될 수 있다 (AI 1.35–9.1 구간). 그리고 실측으로 확인: 메모리 바운드라면 코어를 늘려도 GB/s 가 지붕에서 멈춘다 (vadd 6 스레드 10.38 GFLOP/s = 125 GB/s).

꼬리: "AI 를 어떻게 올리나?" → 재사용 (타일링, 연산 융합), 데이터 타입 축소 (fp32 → fp16/int8 이면 바이트가 1/2–1/4), 배치 키우기 (연습 4: FC 층 배치 1 → 32 로 AI 2 → 63).
</details>

<details><summary>Q6. AI 가 170 인 matmul 이 왜 피크의 15% 밖에 못 내나요?</summary>

compulsory AI 는 DRAM 기준의 이상값이다. 루프라인은 계층마다 다시 그려야 한다: tiled 안쪽 루프는 `fmla.4s` 1 개당 B·C 읽기 32 B + C 쓰기 16 B → L1 AI 0.167, 같은 패턴 triad 의 L1 속도 178 GB/s → L1 지붕 30 GFLOP/s, 측정 20.85 (70%). 즉 L1 load/store 바운드. naive 는 덧셈 순서 때문에 스칼라 `fmadd` 의존 체인 하나 (지연 바운드, 1.58). 해결은 레지스터 블로킹 (C 의 작은 판을 레지스터에 두고 A·B 를 브로드캐스트 — BLAS 마이크로 커널).

꼬리: "Accelerate 는 1890 GFLOP/s 인데?" → NEON 전 코어 천장(1136)보다 위 — 다른 실행 유닛(행렬 엔진/SME 계열, Apple 미공개) 의 지붕이다. 루프라인의 peak 는 "어느 유닛으로 계산하나" 에 따라 바뀐다.
</details>

<details><summary>Q7. -O3 나 -ffast-math 를 켜면 빨라지나요? 위험은?</summary>

`-ffast-math` 는 부동소수 재결합을 허용해 합(reduction)을 여러 누산기로 쪼개 벡터화한다: conv1d naive K=64 9.52 → 32.77 GFLOP/s. 대신 결과가 바뀌고 (덧셈 순서), NaN/Inf 가정도 깨진다 (이 데모 빌드에서 `NAN` 사용에 경고가 2 개 떴다). 같은 이득을 순서를 지키며 얻는 방법이 출력 방향 블로킹 (47.10, 결과는 fused 여부 외엔 같다). 펌웨어·검증 코드에서는 결정적 결과가 필요하므로 블로킹 쪽이 낫다. `-O3` 는 이 레슨에서 쓰지 않았다 (모든 측정은 `-O2`, 비교용 `-O2 -ffast-math` 만).

꼬리: "왜 컴파일러는 순서를 못 바꾸나?" → IEEE 754 덧셈은 결합 법칙이 성립하지 않는다. `(a+b)+c ≠ a+(b+c)` 일 수 있으니 C 의 의미(왼쪽부터)를 지켜야 한다.
</details>

<details><summary>Q8. 공유된 기계(또는 CI 러너)에서 벤치마크 숫자를 어떻게 믿을 수 있게 만드나요?</summary>

(1) `uptime`/load average 를 숫자와 같이 기록, (2) 중앙값 + min–max, (3) 1 스레드 결과를 기준으로 쓰고 다중 스레드는 하한이라고 말하기, (4) 동적 분할로 남의 부하에 덜 민감하게, (5) **짧은 조각 + 코어 번호 필터** (이 레슨의 `percpu`: 2 ms 조각 중 앞뒤 코어 번호가 같은 것만) — 경쟁·이동이 섞인 조각을 버리면 코어 종류별 숫자가 몇 % 안에서 재현됐다 (두 번의 실행에서 E 70.5 / 70.5, Super 142.2 / 142.1, P 107.3–107.4 / 104.8–107.3 GFLOP/s). (6) 측정 중 예상 밖 숫자는 버그를 의심 — 이 레슨에서 400 GB/s (캐시 재사용 버그), `inf GB/s` (죽은 코드 제거) 를 그렇게 잡았다.

꼬리: "macOS 에서 코어를 고정할 수 있나?" → 공개 API 로는 못 한다. QoS 는 힌트이고, 이번 측정에서는 BACKGROUND 도 P 쪽에서 돌았다. Linux 라면 `taskset`/`sched_setaffinity`, `isolcpus`, 주파수 고정(`cpupower`) 으로 훨씬 깨끗하게 잰다.
</details>

## 9. 추가 연습

**연습 1 — Gustafson.** f = 0.05 인 일을 12 코어로: Amdahl 과 Gustafson 속도 향상은? 같은 실행을 두 관점으로 보면 어떻게 이어지나?

<details><summary>답</summary>

Amdahl 7.742, Gustafson `p − s(p−1)` = 11.45. Gustafson 의 s 는 **병렬 실행에서 잰** 직렬 비율이다. 문제를 p 배 키운 일을 1 코어로 하면 `s + (1−s)p` 이고, 그 기준의 직렬 비율 `f = s/(s + (1−s)p)` = 0.0044 를 Amdahl 에 넣으면 정확히 11.45 — 모든 s (0…1, 0.1 간격), p (1…64) 에서 같은지 체크했다. 면접에서 "Amdahl 은 비관적, Gustafson 은 낙관적" 이라고 하면 반쪽 답이다: 질문이 지연(같은 일)인지 처리량(더 큰 일)인지가 고르는 기준.
</details>

**연습 2 — 오버헤드가 있는 Amdahl.** `T(p) = f + (1−f)/p + c(p−1)` (스레드마다 동기화 비용 c). f = 0.05, c = 0.002 일 때 최적 p 와 그때 속도 향상은? Karp–Flatt e 는 p 에 따라 어떻게 변하나?

<details><summary>답</summary>

미분하면 `p* ≈ √((1−f)/c)` = 21.8 → 정수 탐색 결과 **p = 22**, S = 7.397. p = 64 면 5.240 으로 오히려 느리다. e 는 p = 2 0.054, p = 12 0.074, p = 32 0.114 로 단조 증가 — 순수 Amdahl 이면 모든 p 에서 0.05 (체크). 본문 측정의 e 증가(0.004 → 0.067)가 이런 꼴이다. 펌웨어 예: 코어마다 잠금을 잡는 공유 큐, 코어 수에 비례하는 캐시 일관성 트래픽 (ARCH-A04).
</details>

**연습 3 — 계층 루프라인.** peak 100, L1 400 / L2 150 / DRAM 60 GB/s (연습용 대표값). 타일 matmul 의 AI 가 L1 0.2, L2 4, DRAM 170 이면 몇 GFLOP/s 이고 어느 지붕이 묶나? vadd (모든 계층 1/12) 와 L2 에 들어가는 vadd (DRAM 트래픽 ≈ 0) 는?

<details><summary>답</summary>

`attainable = min(peak, min_i AI_i × BW_i)`. 타일 matmul: L1 0.2 × 400 = **80** (L1 이 묶는다 — DRAM AI 가 170 인데도). vadd: DRAM 60/12 = **5**. L2 상주 vadd: L2 150/12 = **12.5**. 레지스터 블로킹으로 AI 가 8 / 40 / 200 이 되면 모든 계층이 100 이상 → **연산 지붕 100**. 본문 tiled matmul 이 바로 첫 경우 (L1 지붕 30, 측정 20.85).
</details>

**연습 4 — 엣지 NPU (펌웨어/AI 가속기).** 4 TOPS INT8, LPDDR 8.5 GB/s (둘 다 연습용 대표값) 인 NPU 에서 (a) 3×3 conv, 56×56, 64 → 64 채널, (b) FC 4096×4096 배치 1, (c) 같은 FC 배치 32 — AI, 판정, 시간 하한은?

<details><summary>답</summary>

릿지 = 4e12 / 8.5e9 = **470.6 op/B**. (a) MAC = 56·56·64·64·9 = 115.6 M → 231.2 Mop, 바이트 = 입력 200 704 + 출력 200 704 + 가중치 36 864 = 438 272 → AI **527.6** > 릿지 → 연산 바운드 (간신히), 하한 57.8 µs. (b) 가중치 16.8 MB 를 한 번 읽어 33.6 Mop → AI **2.00** → 메모리 바운드, 하한 1975 µs (연산만이면 8.4 µs — 235 배). (c) 배치 32 → AI 63.0, 여전히 메모리 바운드, 2005 µs (배치 1 과 거의 같은 시간에 32 배 일). 그래서 LLM 디코드(배치 1)는 대역폭이 성능이고, NPU 사양의 TOPS 는 conv 같은 층에서만 의미가 있다. 펌웨어가 할 일: 가중치를 SRAM 에 상주시키기(타일), int4 가중치, 배치/요청 묶기, DMA 이중 버퍼로 겹치기.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/esn_ARCH-A11/pr && /tmp/esn_ARCH-A11/pr
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o /tmp/esn_ARCH-A11/sanp && /tmp/esn_ARCH-A11/sanp
```

```text
-- 연습 1: Gustafson vs Amdahl --
      s=f=0.05, p=12: Amdahl 7.742  Gustafson 11.450
PASS  near(gustafson_speedup(0.05, 12), 11.45, 1e-12)
PASS  gustafson_speedup(0.0, 12) == 12.0 && gustafson_speedup(1.0, 12) == 1.0
PASS  same
PASS  amdahl_f_of_scaled(0.05, 12) < 0.005
-- 연습 2: overhead model, best p, Karp-Flatt growth --
      f=0.05 c=0.002: best p=22 (sqrt((1-f)/c)=21.79) S=7.397; S(12)=6.615 S(64)=5.240
PASS  abs(bp - (int)lround(sqrt((1 - f) / c))) <= 1
PASS  speedup_ovh(f, c, 64) < speedup_ovh(f, c, bp)
PASS  best_p(f, 0.0, 64) == 64
PASS  grows
PASS  flat
      e(KF) with overhead: p=2 0.0540  p=12 0.0740  p=32 0.1140
-- 연습 3: hierarchical roofline --
      tiled-like: attainable 80.0, binding level 0
PASS  near(r1, 80.0, 1e-12) && w == 0
PASS  near(r2, 5.0, 1e-12) && w == 2
PASS  near(r3, 12.5, 1e-12) && w == 1
PASS  roof_hier(100, 3, ai_reg, bw, &w) == 100.0 && w == -1
-- 연습 4: edge NPU, 4 TOPS INT8 / 8.5 GB/s (representative) --
      ridge 470.6 op/B | conv3x3 56x56x64->64: 231.2 Mop, 438272 B, AI 527.6, t_lb 57.8 us
      FC 4096x4096 B=1: AI 2.00, t_lb 1975 us (compute alone 8.4 us) | B=32: AI 63.0, t_lb 2005 us
PASS  near(ridge, 470.588, 1e-5)
PASS  cv.ops == 2.0 * 56 * 56 * 64 * 64 * 9 && cv.bytes == 438272.0
PASS  cv.ops / cv.bytes > ridge
PASS  f1.ops / f1.bytes < 2.0 && f1.ops / f1.bytes > 1.99
PASS  f32.ops / f32.bytes < ridge
PASS  t_lower_bound(f1, peak, bw) > 200 * f1.ops / peak
PASS  near(t_lower_bound(f32, peak, bw) / t_lower_bound(f1, peak, bw), (16777216.0 + 32 * 8192) / (16777216.0 + 8192), 1e-12)
20/20 passed
```

관찰용 파일 (CHECK 없음, `solution.c` 를 `main` 이름만 바꿔 포함):

<!-- code: demo_amdahl.c -->

<!-- code: demo_roofline.c -->

## 10. 핵심 정리

- Amdahl `S = 1/(f + (1−f)/p)`, 한계 1/f. Karp–Flatt `e = (1/S − 1/p)/(1 − 1/p)` 가 p 와 함께 커지면 직렬 코드가 아니라 오버헤드·불균형·경쟁 (이 맥 f = 0.05: e 0.004 → 0.067, S(12) 6.93 vs 예측 7.74).
- 이기종 코어: 정적 등분 = `p × min(speed)`, 동적 = `Σ speed`. 코어 속도는 **커널마다** 다르다 (M6: FMA 는 Super 142 > P 107 > E 70.5 GFLOP/s, 시프트-eor 정수 체인은 E 5.08 > Super 4.42 > P 4.00 단위/µs). T1 을 잰 코어·클럭도 적는다.
- 피크: 독립 누산기 ≥ 지연 × 파이프 (FMA 지연 ≈ 4 사이클, P 16 개 / Super 24 개). `fmla.4s` = 8 FLOP. asm 으로 확인.
- 대역폭: 1 스레드 triad ≈ 105 GB/s, 전체 ≈ 125 GB/s (1.5 GB 배열, load avg 11–13). 1 스레드도 DRAM 의 대부분 — 코어 쪽 동시 미스가 한계. STREAM 은 RFO 를 안 세지만 이 맥 conv1d 실측은 RFO 없음과 맞았다.
- 루프라인 `min(peak, AI × BW)`, 릿지 1 스레드 1.35 / 전체 9.1 FLOP/B. vadd·conv K=3 은 메모리 바운드 (6 스레드에서 지붕에 붙음), conv K=64·matmul 은 연산 바운드인데 지붕의 1–33%.
- gap 은 의존 체인 (naive 합, 누산기 4 개), 벡터화 실체 (remark 아닌 asm: 순서 있는 합의 `fadd s` 16 개), L1 load/store (tiled matmul 의 L1 AI 0.167 → L1 지붕 30) 로 설명. 다음은 레지스터 블로킹.
- 공유 기계: load average 기록, 중앙값, 1 스레드 기준, 다중 스레드는 하한, 짧은 조각 + 코어 번호 필터. 이상한 숫자(400 GB/s, inf)는 먼저 벤치 버그를 의심.

## 참고자료

- Gene M. Amdahl, "Validity of the single processor approach to achieving large scale computing capabilities", AFIPS 1967.
- John L. Gustafson, "Reevaluating Amdahl's Law", CACM 31(5), 1988.
- Alan H. Karp, Horace P. Flatt, "Measuring Parallel Processor Performance", CACM 33(5), 1990 — 실험적 직렬 비율.
- Mark D. Hill, Michael R. Marty, "Amdahl's Law in the Multicore Era", IEEE Computer 2008 — 대칭/비대칭(이기종) 멀티코어 Amdahl.
- Samuel Williams, Andrew Waterman, David Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures", CACM 52(4), 2009.
- John D. McCalpin, "Memory Bandwidth and Machine Balance in Current High Performance Computers", IEEE TCCA Newsletter 1995 — STREAM 과 바이트 세는 규칙 (www.cs.virginia.edu/stream).
- Aleksandar Ilic, Frederico Pratas, Leonel Sousa, "Cache-aware Roofline model", IEEE CAL 2014 — 계층별 지붕.
- John L. Hennessy, David A. Patterson, *Computer Architecture: A Quantitative Approach* 6판 — 1장 Amdahl, 4장 루프라인.
- Arm, *Arm Architecture Reference Manual for A-profile* — `FMLA (vector)` 의미 (곱-누산, 한 번 반올림). Apple 코어의 지연·파이프 수는 공개 문서가 없어 이 레슨의 숫자는 모두 측정 역산.
- LLVM 문서 — Auto-Vectorization in LLVM (loop vectorizer, SLP vectorizer, `-Rpass=loop-vectorize`), 순서 있는(strict) FP reduction.
- Apple, `pthread_set_qos_class_self_np` / `pthread_attr_set_qos_class_np` (QoS 는 스케줄링 힌트), Accelerate `cblas_sgemm`.
- 관련 레슨: ARCH-B10 (AMAT), ARCH-M06 (쓰기 정책·RFO), ARCH-M07 (matmul 캐시, 레지스터 블로킹 이야기), ARCH-M08 (측정 방식·코어 번호 매핑), ARCH-A04 (일관성 트래픽), ARCH-A10 (공유 자원 중재). 다음: MEM-A08 (계층 루프라인), AIPERF-B04/B06 (AI 커널 루프라인), AIPERF-A08 (DVFS).
