---
id: GPGPU-M03
time: 90분
updated: 2026-11-08
---
# GPGPU-M03 Histogram: Privatization과 Atomic 경합

## 학습 목표

- 256-bin 히스토그램을 **device atomic 버전(v1)** 과 **threadgroup 지역 히스토그램 + 병합 버전(v2)** 으로 짜고, 두 barrier 의 자리와 이유를 말한다.
- "privatization 은 언제 빨라지나" 를 교과서 말이 아니라 이 맥(Apple M6 GPU)에서 잰 숫자로 답한다: 고른 입력에서는 **1.05배(거의 0)**, 치우친 입력에서는 **1.47배**, 한 bin 에 몰리면 **1.48배**, 거기에 SIMD 집계를 더하면 **7.2배**.
- 병목이 "주소가 256개뿐이라서" 가 아니라 **한 SIMD 명령 안의 같은 주소 lane** 이라는 것을 `lane32` 진단 입력으로 구분한다.
- 병합 비용을 공식(threadgroup 마다 0 이 아닌 bin 수의 합)으로 예측하고 GPU 카운터로 맞춘다. 작은 입력 · 너무 많은 threadgroup · 32 KB 를 넘는 bin 수에서는 왜 안 되는지 안다.
- GPU 히스토그램을 ISP 자동 노출(AE) 통계로 바꿔 쓰고, GPU/NPU atomic 검증 테스트를 어떻게 설계할지 말한다.

## 1. 문제

256-bin 히스토그램을 GPU 로 빠르게 세라 — 전 스레드가 같은 256개 카운터를 atomic 으로 때리는 것(v1)과, threadgroup 마다 자기 카운터를 두고 마지막에 합치는 것(v2)을 구현하고 고른 입력 · 치우친 입력에서 어느 쪽이 얼마나 빠른지 재는 문제다. GPU/AI 가속기 면접의 단골이고("atomic 경합을 어떻게 줄이나요?"), 펌웨어에서는 **카메라 ISP 의 AE/AWB 통계 블록**, 실리콘 검증에서는 **GPU/NPU atomic 유닛의 스트레스 패턴** 과 곧장 이어진다.

이미 아는 것(다시 가르치지 않는다): `c[0]++` 가 깨지는 이유 · atomic 의 relaxed · `threadgroup_barrier` 의 뜻 · simdgroup lockstep · 주소 수별 atomic 처리량(GPGPU-B09), CPU 쪽 per-thread 카운터 패턴(MEM-M04 pattern b — 7층에서 병렬 작성 중), atomic 비용 숫자(LLAT-B06), 캐시 라인 경합(OS-M07, ARCH-M08). 여기서는 **"경합을 만드는 구조를 어떻게 바꾸나"** 만 다룬다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">v1: 전 GPU 가 같은 256칸을 친다 / v2: threadgroup 마다 내 256칸, 끝에 한 번만 합친다</text>
  <text x="20" y="48" fill="currentColor" font-weight="bold">v1 전역 atomic</text>
  <rect x="20" y="58" width="60" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="30" y="78" fill="currentColor" font-size="12">TG 0</text>
  <rect x="90" y="58" width="60" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="100" y="78" fill="currentColor" font-size="12">TG 1</text>
  <text x="160" y="78" fill="currentColor">…</text>
  <rect x="180" y="58" width="80" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="190" y="78" fill="currentColor" font-size="12">TG 1023</text>
  <path d="M50 88 L140 150" stroke="currentColor" fill="none"/>
  <path d="M120 88 L140 150" stroke="currentColor" fill="none"/>
  <path d="M220 88 L140 150" stroke="currentColor" fill="none"/>
  <rect x="30" y="150" width="230" height="44" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="44" y="170" fill="currentColor">device atomic_uint hist[256]</text>
  <text x="44" y="187" fill="currentColor" font-size="12">바이트마다 1번 = 16,777,216 atomic</text>
  <text x="20" y="218" fill="currentColor" font-size="12">주소 256개 · 스레드 262,144개가 공유</text>
  <text x="20" y="236" fill="currentColor" font-size="12">전부 device atomic (어디서 처리되는지 세부는 비공개)</text>
  <text x="400" y="48" fill="currentColor" font-weight="bold">v2 threadgroup 지역 + 병합</text>
  <rect x="400" y="58" width="90" height="56" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="410" y="76" fill="currentColor" font-size="12">TG 0</text>
  <text x="410" y="94" fill="currentColor" font-size="12">local[256]</text>
  <text x="410" y="108" fill="currentColor" font-size="12">1 KB</text>
  <rect x="500" y="58" width="90" height="56" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="510" y="76" fill="currentColor" font-size="12">TG 1</text>
  <text x="510" y="94" fill="currentColor" font-size="12">local[256]</text>
  <text x="600" y="90" fill="currentColor">…</text>
  <rect x="622" y="58" width="100" height="56" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="632" y="76" fill="currentColor" font-size="12">TG 1023</text>
  <text x="632" y="94" fill="currentColor" font-size="12">local[256]</text>
  <path d="M445 114 L540 164" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <path d="M545 114 L550 164" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <path d="M672 114 L560 164" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <rect x="430" y="164" width="240" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="444" y="184" fill="currentColor">device atomic_uint hist[256]</text>
  <text x="444" y="201" fill="currentColor" font-size="12">병합: bin 마다 TG 당 1번 ≤ 262,144 atomic</text>
  <text x="400" y="228" fill="currentColor" font-size="12">주소 256 × 1024 = 262,144칸 · 칸마다 경합은 그 TG 의 256 스레드끼리</text>
  <text x="400" y="246" fill="currentColor" font-size="12">메모리 쪽 atomic 은 16,777,216 → 262,144 (64분의 1)</text>
  <rect x="20" y="262" width="700" height="58" rx="6" fill="none" stroke="currentColor"/>
  <text x="34" y="283" fill="currentColor" font-weight="bold">이 레슨의 결론 (실측)</text>
  <text x="34" y="303" fill="currentColor">입력이 고르면 둘 다 메모리 대역폭에 걸려 차이가 없고, 같은 bin 이 몰릴수록 v2 와 SIMD 집계(v3)가 벌어진다.</text>
</svg>
```

체크 테스트는 **정확성** 만 본다: bin 256개가 전부 CPU 결과와 같은가, 합이 N 인가, AE 결정이 기대값인가. 처리량은 `demo_bench.m` 의 출력이다.

### Part A — CPU 기준과 전역 atomic (v1)

- `hist_cpu` — CPU 기준 히스토그램(golden). 모든 GPU 결과는 이것과 비교한다.
- 커널 `hist_v1_global` — `uint` 단위로 읽고(바이트 4개), 바이트마다 `atomic_fetch_add_explicit(&hist[b], 1u, memory_order_relaxed)`. 스레드마다 word 여러 개(grid-stride), 뒤에 남는 0~3 바이트(tail)까지 정확히.
- 입력 세 종류(결정적 xorshift32): 균일(`uniform`), Zipf s=1.2(`zipf1.2`, bin 0 이 약 25 %), 한 bin(`same`, 전부 42). N = 2^24.

### Part B — threadgroup 지역 히스토그램 (v2)

커널 `hist_v2_tg` — `threadgroup atomic_uint local[256]` 로:

1. 0 으로 초기화 (threadgroup 크기가 256 보다 작아도 커도 맞게)
2. `threadgroup_barrier(mem_flags::mem_threadgroup)`
3. 지역 누적 (device atomic 없음)
4. 다시 barrier
5. 병합: bin 마다 **device atomic 1번** (0 인 bin 은 건너뜀)

threadgroup 크기 32 / 64 / 96 / 128 / 1024, threadgroup 1개 · 4096개, 홀수 길이, 반복 실행에서도 CPU 와 정확히 같아야 한다. 틀린 버전들(barrier 없음 · 초기화 없음 · 그냥 저장 병합)은 `demo_bugs.m` 에서 관찰한다.

### Part C — SIMD 집계(v3)와 ISP 자동 노출 통계

- 커널 `hist_v3_simd` — v2 에 빠른 길 추가: simdgroup 의 활성 lane 이 **모두 같은 word 이고 그 word 의 4바이트가 같으면** lane 하나가 `local[bin] += 4 × 활성 lane 수`. `simd_broadcast_first`, `simd_all`, `simd_sum`, `simd_is_first` 를 쓰고, 일부 lane 만 활성인 마지막 반복에서도 정확해야 한다.
- `ae_stats` / `ae_step` — 8비트 Y 히스토그램에서 평균(Q8), p50, p99, 하이라이트/섀도 포화 픽셀 수를 **정수 연산만으로** 구하고, 노출을 한 단계 올릴지 내릴지 정한다. 합성 프레임 3장(어두움 / 하늘 포화 / 보통)에서 GPU 히스토그램 → AE 결정이 기대값과 같아야 한다.

## 2. Problem (English)

You must compute a 256-bin histogram of N bytes (N ≥ 2^24) on an Apple GPU with Metal compute kernels, and show when threadgroup privatization beats global atomics.

**Part A.** `void hist_cpu(const uint8_t *p, size_t n, uint32_t h[256])`: the single-threaded reference. Kernel `hist_v1_global(words, hist, P, gid, gsize, lid, tgsz)`: read 32-bit words with grid-stride addressing (`i = gid; i < P.n_words; i += gsize`), add 1 to `hist[byte]` for each of the 4 bytes with `atomic_fetch_add_explicit(..., memory_order_relaxed)`; the remaining `P.n_bytes - 4*P.n_words` tail bytes are handled by threads `gid < tail`.

**Part B.** Kernel `hist_v2_tg`: declare `threadgroup atomic_uint local[256]`; zero it (bin `b` is owned by thread `b mod tgsz`, works for any threadgroup size), barrier, accumulate locally, barrier, then merge each non-zero bin into the device histogram with one atomic add per bin per threadgroup. Must be exact for threadgroup sizes 32..1024, 1..4096 threadgroups, odd lengths, and repeated dispatches.

**Part C.** Kernel `hist_v3_simd`: v2 plus a SIMD aggregation fast path: if all active lanes of the simdgroup read the same word and all 4 bytes of it are equal, one lane adds `4 * active_lanes` to the local bin. Must stay exact when only some lanes are active. `bool ae_stats(const uint32_t h[256], ae_stats_t *st)`: total `n`, `mean_q8 = floor(256*mean)`, `p50`/`p99` = smallest bin with `cum*100 >= q*n`, `clip_hi` = pixels in bins ≥ 250, `clip_lo` = pixels in bins ≤ 5; returns false for an empty histogram. `int ae_step(const ae_stats_t*)`: −1 if more than 2 % saturated, else +1 if `p99 < 200`, else 0.

Constraints: Objective-C host + MSL kernels compiled at run time, `MTLResourceStorageModeShared`, integers only in `ae_stats`, every GPU result compared bin-for-bin with the CPU. Follow-up: measure GB/s per version per input (uniform, Zipf, all-same), GPU time from `commandBuffer.GPUStartTime/GPUEndTime`, median of several runs, with a single-thread CPU reference; explain when privatization does not help (tiny inputs, too many threadgroups, more bins than `maxThreadgroupMemoryLength` allows).

## 3. 시작 코드

호스트:

<!-- code: starter.m -->

커널:

<!-- code: starter.metal -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cd lessons/GPGPU/GPGPU-M03                                               # 반드시 이 폴더에서 실행 (.metal 을 현재 디렉터리에서 읽는다)
clang -fobjc-arc -O2 -Wall -Wextra -Werror starter.m -framework Metal -framework Foundation -o starter && ./starter
```

호스트는 `__FILE__`(= `starter.m`)의 확장자를 `.metal` 로 바꾼 파일을 읽어 `newLibraryWithSource` 로 컴파일한다(이 맥에는 오프라인 `metal` 컴파일러가 없다, GPGPU-B09 참고). 그래서 `.m` 이 곧 `.metal` 파일 이름을 정한다.

처음 실행 결과 (실제 출력, exit 1, 약 1초, PASS 줄과 중간 생략):

```text
-- Part A: CPU reference + global atomics (hist_v1_global)
FAIL starter.m:225  c['l'] == 2 && c['o'] == 2 && c['h'] == 2 && c[','] == 1 && c[' '] == 1 && hist_sum(c) == 16
FAIL starter.m:228  hist_sum(c) == N
...
  v1 uniform  MISMATCH
FAIL starter.m:239  v1_match
...
-- Part B: threadgroup privatization (hist_v2_tg)
  v2 uniform  MISMATCH
FAIL starter.m:257  v2_match
...
-- Part C: SIMD aggregation (hist_v3_simd) + ISP auto-exposure stats
  v3 uniform  MISMATCH
FAIL starter.m:290  v3_match
...
FAIL starter.m:327  all_eq
FAIL starter.m:328  all_step
2/26 passed
```

시작 코드의 커널은 병합(또는 누적) 자리가 비어 있어서 `hist` 가 늘 0 으로 남는다 → **레이스가 일어나든 말든 언제나 FAIL** 이다.

## 4. 힌트

<details>
<summary>힌트 1 — grid-stride 로 word 나누기</summary>

스레드 `gid` 가 word `gid, gid+G, gid+2G, ...` (G = 전체 스레드 수)를 맡으면 겹침도 빠짐도 없다. 스레드 수는 `tg × ntg` 라 `[[threads_per_grid]]` 로 받는다. 남은 0~3 바이트는 `gid < tail` 인 스레드가 하나씩.
</details>

<details>
<summary>힌트 2 — 지역 배열은 누가 초기화하나</summary>

스레드 `lid` 가 bin `lid, lid+tgsz, lid+2·tgsz, ...` 를 맡는다. threadgroup 이 256 스레드면 스레드마다 1칸, 64 스레드면 4칸, 1024 스레드면 앞의 256 스레드만 일한다. 병합도 같은 모양이다.
</details>

<details>
<summary>힌트 3 — barrier 는 왜 두 개인가</summary>

첫 번째: 다른 simdgroup 이 아직 0 을 쓰는 중인데 내가 더하면 내 +1 이 지워진다. 두 번째: 다른 simdgroup 이 아직 더하는 중인데 내가 읽으면 그 값이 빠진다. 같은 simdgroup 안이면 lockstep 이라 멀쩡해 보이지만, simdgroup 사이는 barrier 없이는 순서가 없다(GPGPU-B09).
</details>

<details>
<summary>힌트 4 — SIMD 집계 빠른 길</summary>

`uint first = simd_broadcast_first(w);` 로 첫 활성 lane 의 값을 전부 받고, `simd_all(w == first)` 로 모두 같은지 본다. 4바이트가 같은지는 `first == (first & 0xFF) * 0x01010101u`. 활성 lane 수는 `simd_sum(4u)` — **모든 활성 lane 이 함께** 불러야 하므로 `if (simd_is_first())` 안에 넣으면 안 된다.
</details>

<details>
<summary>힌트 5 — 백분위를 정수로</summary>

`q` 퍼센트 지점 = `cum * 100 >= q * n` 을 처음 만족하는 bin. 나누지 말고 곱해서 비교한다(n 이 2^32 를 넘을 수 있으니 `uint64_t`). `mean_q8` 는 `sum(b*h[b]) * 256 / n`.
</details>

## 5. 풀이

### Part A — 호스트 뼈대와 전역 atomic

Metal 호스트에서 한 번의 실행은 다음 흐름이다. 하네스의 `gpu_init`, `gpu_pipe`, `gpu_hist` 가 이 일을 한다.

| 단계 | API | 메모 |
|---|---|---|
| 장치 | `MTLCreateSystemDefaultDevice()` | Apple M6, 12코어 GPU |
| 소스 → 라이브러리 | `newLibraryWithSource:options:error:` | `.metal` 파일 문자열을 런타임 컴파일 |
| 파이프라인 | `newComputePipelineStateWithFunction:error:` | 여기서 threadgroup 메모리 한도 등을 검사한다(연습 1) |
| 버퍼 | `newBufferWithLength:options:MTLResourceStorageModeShared` | CPU/GPU 가 같은 메모리를 본다 |
| 인코딩 | `dispatchThreadgroups:threadsPerThreadgroup:` | 전체 스레드 = `ntg × tg` |
| 동기화 | `commit` → `waitUntilCompleted` | 끝난 뒤에야 CPU 가 결과를 읽는다 |

`relaxed` 면 충분한 이유: 카운터는 "다른 메모리를 이 값과 함께 보이게" 할 필요가 없고 원자성만 필요하다. 결과는 command buffer 가 끝난 뒤 CPU 가 읽으므로, 그 시점이 곧 동기화 지점이다(자세한 메모리 순서 얘기는 GPGPU-B09).

왜 `uint` 로 읽나: 스레드 하나가 1바이트씩 읽으면 스레드 수가 N 개 필요하다. word 로 읽고 16 word 쯤 grid-stride 로 맡기면 2^24 바이트에 스레드 2^18 개면 된다(`pick_ntg`). 4바이트의 순서(엔디언)는 히스토그램에 영향이 없다 — 순서와 무관하게 세기 때문이다.

### Part B — 왜 privatization 이 경합을 줄이나, 그리고 barrier 두 개

| | v1 전역 | v2 지역 + 병합 |
|---|---|---|
| 카운터 주소 수 | 256 (전 GPU 공유) | 256 × ntg (threadgroup 끼리 안 겹침) |
| 메모리 쪽 atomic 수 | N 번 (2^24) | Σ_threadgroup (0 아닌 bin 수) ≤ 256 × ntg |
| 추가 비용 | 없음 | threadgroup 메모리(1 KB/TG), 0 초기화, 병합, barrier 2개 |

연습 4 에서 병합 수를 실제로 세면, N = 2^22 · 256 스레드 · ntg 1024 일 때 uniform 262,144 번(16분의 1), 전부 같은 값이면 1,024 번(4096분의 1)이다(`hist_v2_count` 의 GPU 카운터 == CPU 예측). 입력이 치우칠수록 이득이 크다.

barrier 가 왜 필요한지는 그림과 아래 실측으로 본다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">threadgroup 하나의 시간표 (세로: simdgroup, 가로: 시간) — barrier 가 없는 곳에서 깨진다</text>
  <text x="10" y="58" fill="currentColor" font-size="12">SG 0</text>
  <text x="10" y="98" fill="currentColor" font-size="12">SG 1</text>
  <rect x="50" y="42" width="110" height="26" class="f-mut" stroke="currentColor"/>
  <text x="60" y="60" fill="currentColor" font-size="12">0 초기화</text>
  <rect x="90" y="82" width="110" height="26" class="f-mut" stroke="currentColor"/>
  <text x="100" y="100" fill="currentColor" font-size="12">0 초기화 (늦음)</text>
  <path d="M215 34 L215 116" stroke="currentColor" stroke-width="2"/>
  <text x="196" y="130" fill="currentColor" font-size="12">barrier 1</text>
  <rect x="230" y="42" width="190" height="26" class="f-ok" stroke="currentColor"/>
  <text x="240" y="60" fill="currentColor" font-size="12">지역 누적 (atomic)</text>
  <rect x="230" y="82" width="230" height="26" class="f-ok" stroke="currentColor"/>
  <text x="240" y="100" fill="currentColor" font-size="12">지역 누적 (atomic, 늦음)</text>
  <path d="M475 34 L475 116" stroke="currentColor" stroke-width="2"/>
  <text x="456" y="130" fill="currentColor" font-size="12">barrier 2</text>
  <rect x="490" y="42" width="120" height="26" class="f-acc" stroke="currentColor"/>
  <text x="500" y="60" fill="currentColor" font-size="12">병합</text>
  <rect x="490" y="82" width="120" height="26" class="f-acc" stroke="currentColor"/>
  <text x="500" y="100" fill="currentColor" font-size="12">병합</text>
  <rect x="20" y="148" width="340" height="120" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="32" y="168" fill="currentColor" font-weight="bold">barrier 1 을 빼면</text>
  <text x="32" y="188" fill="currentColor" font-size="12">SG 0 이 이미 +1 한 칸을, 늦은 SG 1 의 0 쓰기가</text>
  <text x="32" y="206" fill="currentColor" font-size="12">덮어쓴다 → 카운트 사라짐 (합 &lt; N)</text>
  <text x="32" y="226" fill="currentColor" font-size="12">실측 (tg 256): 20/20 실행 틀림,</text>
  <text x="32" y="244" fill="currentColor" font-size="12">합 14.2M ~ 16.5M / 16.8M</text>
  <rect x="380" y="148" width="340" height="120" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="392" y="168" fill="currentColor" font-weight="bold">barrier 2 를 빼면</text>
  <text x="392" y="188" fill="currentColor" font-size="12">SG 0 이 병합하며 읽을 때 SG 1 은 아직 세는 중</text>
  <text x="392" y="206" fill="currentColor" font-size="12">→ 그 뒤의 +1 은 영영 병합 안 됨 (합 &lt; N)</text>
  <text x="392" y="226" fill="currentColor" font-size="12">실측 (tg 256): 20/20 실행 틀림,</text>
  <text x="392" y="244" fill="currentColor" font-size="12">합 14.4M ~ 14.9M / 16.8M</text>
</svg>
```

`threadgroup_barrier` 는 그 threadgroup 의 **모든 스레드가** 도착해야 풀린다(MSL 사양). 그래서 barrier 앞에서 `return` 하면 안 되고, 스레드마다 다른 횟수로 barrier 를 만나서도 안 된다 (어겼을 때 이 칩에서 어떻게 되는지는 돌려 보지 않았다). 일이 없는 스레드도 barrier 는 지나가게 한다 — v2 의 grid-stride 루프가 비어도 barrier 는 모두 지나간다.

**threadgroup 크기와 무관하게** 맞아야 하는 부분: 초기화/병합 루프가 `b = lid; b < 256; b += tgsz` 이면 크기 32(스레드당 8칸), 1024(앞의 256 스레드만) 어디서든 256칸을 정확히 한 번씩 만진다. `if (lid < 256) local[lid] = 0` 같은 코드는 tg = 64 일 때 뒤의 192칸을 안 지운다.

### Part C — SIMD 집계와 "같은 주소 lane" 병목

v3 의 빠른 길은 한 줄 요약하면 "simdgroup 32 lane 이 같은 칸에 +1 × 32 를 하려 하면, lane 하나가 +32 를 하게 한다" 다. CUDA 쪽에서 warp-aggregated atomics, `__reduce_add_sync` / `__match_any_sync`(Volta+, 버전 확인 필요)로 부르는 기법과 같은 생각이다.

`hist_v3_simd` 의 주의점:

- `simd_sum(4u)` 는 활성 lane 모두가 불러야 합이 나온다. 그래서 `if (simd_is_first())` 앞에서 부른다.
- grid-stride 의 마지막 반복에서는 일부 lane 만 활성이다(`i >= P.n_words` 로 빠져나간 lane). MSL 사양은 SIMD 함수를 활성 lane 기준으로 정의한다 — `v3_partial_ok` 가 이것(부분 활성, tg 96·64·1024, 일부 word 만 다름)을 검사한다.
- `same` 입력에서만 이득이 크고, 조건 검사 비용은 작다 — 아래 측정에서 uniform 은 오차 범위, zipf 는 v2 와 같다.

그럼 "일부 bin 만 몰린" zipf 는? 32 lane 이 전부 같은 값일 일이 드물어서 빠른 길을 거의 못 탄다. `demo_bench.metal` 의 `hist_v3c_match` 는 "남은 lane 중 첫 lane 의 값과 같은 lane 들을 묶어 한 번에" 를 반복하는 일반 버전인데, 실측은 uniform 에서 **5.3 ms (v2 의 46배 느림)**, zipf 3.1 ms(20배 느림), `same` 에서만 0.185 ms 다. lane 마다 고유값이 많으면 반복이 lane 수만큼 돈다. `hist_v3b_subsg`(simdgroup 마다 따로 256칸, 8 KB)는 v2 와 차이가 없었다(같은 주소 lane 은 같은 simdgroup 안에서 생기니까 simdgroup 을 나눠도 안 줄어든다).

**AE 통계**: 대부분의 카메라 SoC 에서 히스토그램은 ISP 의 **하드웨어 통계 블록** 이 프레임마다 만들어 메모리나 레지스터로 넘기고, 펌웨어는 `ae_stats` 같은 요약만 계산해 노출·게인을 조절한다(칩마다 다름; CAM-M06 은 그 제어 루프, SYSD-M05 는 시스템 그림). GPU 로 세는 것은 그 블록을 소프트웨어로 대신하는 경우(하드웨어 윈도가 부족할 때, 검증용 골든)다. 두 가지 설계 포인트: (1) 정수 연산 — 프레임마다 값이 흔들리면 AE 가 떨린다, (2) 백분위 정의를 코드와 문서가 같게.

### 측정 — 언제 빨라지나 (`demo_bench.m`)

```bash
clang -fobjc-arc -O2 -Wall -Wextra -Werror demo_bench.m -framework Metal -framework Foundation -o bench
./bench main      # 약 8초
./bench ntg ; ./bench size ; ./bench state
```

측정 방식: GPU 시간 = `cb.GPUEndTime - cb.GPUStartTime`, 버전을 번갈아 돌린 11회의 중앙값, 시작 전에 GPU 를 2초 돌려 예열(아래 "두 상태" 참고), 결과가 CPU 와 다르면 `X` 표시(이번엔 한 번도 없음). CPU = 1스레드 `h[p[i]]++`, 5회 중앙값. 기계: Apple M6 (Super 2 + Performance 4 + Efficiency 6), GPU 12코어, Apple clang 21.0.0. **이 시간에 다른 세션의 벤치마크도 같이 돌고 있었다 (load average 4.3–4.5)** → 절대값보다 배수를 볼 것.

N = 2^24 (16 MiB), tg 256 × 1024 threadgroup. 셀 = ms / GB/s:

| 입력 | v0 읽기만 | v1 전역 | v2 지역 | v3 SIMD | v3b simdgroup별 | v3c match 루프 | CPU 1스레드 |
|---|---|---|---|---|---|---|---|
| uniform | 0.120 / 139.5 | 0.120 / 140.0 | 0.114 / 147.6 | 0.110 / 152.2 | 0.110 / 151.8 | 5.276 / 3.2 | 3.78 / 4.44 |
| zipf1.2 | 0.116 / 144.3 | 0.230 / **73.0** | 0.156 / **107.4** | 0.155 / 108.1 | 0.162 / 103.7 | 3.092 / 5.4 | 8.58 / 1.95 |
| same | 0.122 / 137.9 | 0.826 / **20.3** | 0.559 / **30.0** | 0.115 / **145.7** | 0.580 / 28.9 | 0.185 / 90.7 | 15.27 / 1.10 |
| lane32 | 0.135 / 124.3 | 0.123 / 136.9 | 0.122 / 137.7 | 0.119 / 140.8 | 0.114 / 147.7 | 5.609 / 3.0 | 14.68 / 1.14 |

N = 2^26 (64 MiB)에서도 같은 그림: uniform 140.5 / 149.2 / 149.3, zipf 74.0 / 110.0 / 112.8, same 20.3 / 31.1 / 149.6 GB/s (v1 / v2 / v3).

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">N = 2^24 바이트 처리량 (GB/s, 높을수록 좋음) — Apple M6 GPU, 11회 중앙값, load 4.5</text>
  <rect x="20" y="34" width="14" height="12" class="f-warn" stroke="currentColor"/>
  <text x="40" y="45" fill="currentColor">v1 전역 atomic</text>
  <rect x="185" y="34" width="14" height="12" class="f-acc" stroke="currentColor"/>
  <text x="205" y="45" fill="currentColor">v2 threadgroup 지역</text>
  <rect x="350" y="34" width="14" height="12" class="f-ok" stroke="currentColor"/>
  <text x="370" y="45" fill="currentColor">v3 = v2 + SIMD 집계</text>
  <path d="M515 40 L535 40" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3"/>
  <text x="541" y="45" fill="currentColor">v0 읽기만 (대역폭 천장)</text>
  <path d="M60 300 L720 300" stroke="currentColor" stroke-opacity="0.2" fill="none"/>
  <text x="52" y="304" fill="currentColor" text-anchor="end">0</text>
  <path d="M60 250 L720 250" stroke="currentColor" stroke-opacity="0.2" fill="none"/>
  <text x="52" y="254" fill="currentColor" text-anchor="end">40</text>
  <path d="M60 200 L720 200" stroke="currentColor" stroke-opacity="0.2" fill="none"/>
  <text x="52" y="204" fill="currentColor" text-anchor="end">80</text>
  <path d="M60 150 L720 150" stroke="currentColor" stroke-opacity="0.2" fill="none"/>
  <text x="52" y="154" fill="currentColor" text-anchor="end">120</text>
  <path d="M60 100 L720 100" stroke="currentColor" stroke-opacity="0.2" fill="none"/>
  <text x="52" y="104" fill="currentColor" text-anchor="end">160</text>
  <path d="M60 300 L720 300" stroke="currentColor" fill="none"/>
  <rect x="85" y="125" width="34" height="175" class="f-warn" stroke="currentColor"/>
  <text x="102" y="120" fill="currentColor" text-anchor="middle">140</text>
  <rect x="125" y="116" width="34" height="184" class="f-acc" stroke="currentColor"/>
  <text x="142" y="110" fill="currentColor" text-anchor="middle">148</text>
  <rect x="165" y="110" width="34" height="190" class="f-ok" stroke="currentColor"/>
  <text x="182" y="105" fill="currentColor" text-anchor="middle">152</text>
  <path d="M79 126 L205 126" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3" fill="none"/>
  <text x="142" y="318" fill="currentColor" text-anchor="middle" font-weight="bold">uniform</text>
  <text x="142" y="334" fill="currentColor" text-anchor="middle">CPU 1스레드 4.44</text>
  <rect x="250" y="209" width="34" height="91" class="f-warn" stroke="currentColor"/>
  <text x="267" y="204" fill="currentColor" text-anchor="middle">73</text>
  <rect x="290" y="166" width="34" height="134" class="f-acc" stroke="currentColor"/>
  <text x="307" y="161" fill="currentColor" text-anchor="middle">107</text>
  <rect x="330" y="165" width="34" height="135" class="f-ok" stroke="currentColor"/>
  <text x="347" y="160" fill="currentColor" text-anchor="middle">108</text>
  <path d="M244 120 L370 120" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3" fill="none"/>
  <text x="307" y="318" fill="currentColor" text-anchor="middle" font-weight="bold">zipf1.2</text>
  <text x="307" y="334" fill="currentColor" text-anchor="middle">CPU 1스레드 1.95</text>
  <rect x="415" y="275" width="34" height="25" class="f-warn" stroke="currentColor"/>
  <text x="432" y="270" fill="currentColor" text-anchor="middle">20</text>
  <rect x="455" y="262" width="34" height="38" class="f-acc" stroke="currentColor"/>
  <text x="472" y="258" fill="currentColor" text-anchor="middle">30</text>
  <rect x="495" y="118" width="34" height="182" class="f-ok" stroke="currentColor"/>
  <text x="512" y="113" fill="currentColor" text-anchor="middle">146</text>
  <path d="M409 128 L535 128" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3" fill="none"/>
  <text x="472" y="318" fill="currentColor" text-anchor="middle" font-weight="bold">same</text>
  <text x="472" y="334" fill="currentColor" text-anchor="middle">CPU 1스레드 1.10</text>
  <rect x="580" y="129" width="34" height="171" class="f-warn" stroke="currentColor"/>
  <text x="597" y="124" fill="currentColor" text-anchor="middle">137</text>
  <rect x="620" y="128" width="34" height="172" class="f-acc" stroke="currentColor"/>
  <text x="637" y="123" fill="currentColor" text-anchor="middle">138</text>
  <rect x="660" y="124" width="34" height="176" class="f-ok" stroke="currentColor"/>
  <text x="677" y="119" fill="currentColor" text-anchor="middle">141</text>
  <path d="M574 145 L700 145" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3" fill="none"/>
  <text x="637" y="318" fill="currentColor" text-anchor="middle" font-weight="bold">lane32</text>
  <text x="637" y="334" fill="currentColor" text-anchor="middle">CPU 1스레드 1.14</text>
</svg>
```

읽는 법:

1. **uniform 에서는 v1 ≈ v2 ≈ v0 천장(약 140–150 GB/s)**. 이 칩의 device atomic 은 uniform 256 bin 을 대역폭 천장까지 처리한다 — 경합이 병목이 아니므로 privatization 이 줄 게 없다. 교과서("privatization 하면 빠르다")는 이 칩의 이 입력에서는 맞지 않는다. (140–150 GB/s 가 이 칩 메모리 대역폭의 몇 %인지는 공칭값을 확인 못 했다.)
2. **zipf: v2 가 v1 의 1.47배**. bin 0 에 25 % 가 몰려서 v1 이 73 GB/s 로 떨어진다. v2 는 병합에서 빼고는 그 경합을 threadgroup 안으로 가둔다.
3. **same (최악): v1 20.3, v2 30.0(1.48배), v3 145.7(7.2배)**. v2 도 threadgroup 안에서 같은 주소를 256 lane 이 치므로 여전히 느리다 — 진짜 해법은 SIMD 집계다.
4. **lane32(진단 입력)가 핵심 단서**: word i 의 4바이트가 모두 `i & 31` 이라 한 simdgroup 의 32 lane 이 서로 다른 32개 bin 을 치고, 모든 simdgroup 이 같은 32개 bin 을 친다. bin 수가 32 뿐인데도 v1 이 136.9 GB/s (v0 천장 근처)다. 즉 비싼 것은 "bin 이 적다" 가 아니라 **"같은 SIMD 명령 안의 여러 lane 이 같은 주소를 친다"** 이다 (같은 주소 lane 의 직렬화 — GPGPU-B09 의 blocked 패턴 실측과 같은 결론). 내부에서 정확히 어떻게 처리되는지는 Apple 이 공개하지 않아 추정이다.
5. CPU 1스레드와의 비교: uniform 은 GPU 가 약 33배 빠르고, `same` 입력에서 CPU 는 1.10 GB/s 로 uniform 의 1/4 이 된다. 같은 카운터를 연달아 올리면 **이전 저장을 다음 읽기가 기다리는** 메모리 의존 체인이 생겨서다 (원인은 이 추정 이상 확인하지 않았다. 그래서 빠른 CPU 히스토그램은 카운터 표를 여러 벌 두고 번갈아 쓴다 — ARM-M07, ARM-A11, 멀티스레드면 MEM-M04 pattern b).

**두 상태 (주의)**: 이 칩에서 v1 uniform 은 0.378 ms("느린 상태") 또는 0.11 ms 대("빠른 상태")로 나뉘어 나왔다. `./bench state` (load 4.85):

```text
after 2 s idle, 8 MB CPU memset between dispatches: run# : v0 read ms / v1 global ms (uniform, N = 2^24, ntg 1024)
 0: 0.145 / 0.383    1: 0.255 / 0.378    2: 0.145 / 0.378    3: 0.117 / 0.379    4: 0.118 / 0.378
...
after 3 s of back-to-back dispatches, no CPU work between dispatches:
 0: 0.119 / 0.118    1: 0.119 / 0.125    2: 0.127 / 0.127    3: 0.133 / 0.135    4: 0.127 / 0.122
```

v0(읽기만)는 그대로인데 atomic 커널만 3배 차이가 난다. GPU 가 몇 초 바쁘게 돌면 빠른 상태가 되는 것으로 보아 GPU 클럭/전력 상태로 추정한다(확인 못 함, 다른 세션의 GPU 사용도 섞여 있다). 그래서 벤치는 시작 전에 2초 예열한다. 느린 상태일 때(`./bench size`·`./bench place` 의 일부 실행에서 관찰) v1 0.378 ms, v2 0.267 ms 로 **v1 대비 1.42배** 빨랐다 — atomic 처리량이 천장 아래로 내려오면 uniform 에서도 privatization 이 효과를 낸다.

**threadgroup 수 (`./bench ntg`, N = 2^24, load 4.2–4.3)** — 몇 개가 적당한가 (v1 / v2 ms):

| ntg | uniform | same |
|---|---|---|
| 16 | 0.258 / 0.246 | 1.351 / 1.005 |
| 64 | 0.142 / 0.112 | 0.924 / 0.715 |
| 256 | 0.133 / 0.120 | 0.959 / 0.657 |
| 1024 | 0.114 / 0.111 | 0.826 / 0.558 |
| 4096 | 0.110 / 0.105 | 0.822 / 0.574 |
| 16384 | 0.128 / **0.135** | 0.991 / 0.831 |

너무 적으면(16) 스레드가 모자라 GPU 가 다 차지 않는 것으로 보이고, 많으면(16384 = 스레드당 1 word) v2 의 병합이 약 4백만 번(계산값, 바이트 N 의 1/4)이 되어 uniform 에서 v1 보다 느려진다. 위 표의 몇몇 칸은 다른 GPU 작업의 잡음이 섞였다 — 경향만 본다.

**작은 입력 (`./bench size`, uniform, load 3.5–5.0, ms):**

| N | v0 읽기만 | v1 | v2 | v2/v1 |
|---|---|---|---|---|
| 1 KiB | 0.0023 | 0.0026 | 0.0027 | 1.06 |
| 4 KiB | 0.0052 | 0.0060 | 0.0064 | 1.07 |
| 16 KiB | 0.0090 | 0.0115 | 0.0120 | 1.04 |
| 64 KiB | 0.0092 | 0.0119 | 0.0122 | 1.02 |
| 256 KiB | 0.0093 | 0.0128 | 0.0127 | 0.99 |
| 1 MiB | 0.0048 | 0.0091 | 0.0076 | 0.84 |
| 4 MiB | 0.0260 | 0.1011 | 0.0734 | 0.73 |
| 16 MiB | 0.1445 | 0.1381 | 0.1406 | 1.02 |

수 KiB 이하에서 v2 는 언제나 조금 느리다(4–7 %): 시간 대부분이 launch 오버헤드이고 v2 는 0 초기화·병합·barrier 를 더 한다. 1–4 MiB 줄은 "느린 상태" 값이 섞인 것으로 보인다(4 MiB 의 v1 0.1011 ≈ 0.378 / 4). **수 KiB 이하에서는 privatization 을 할 이유가 없다.**

**threadgroup 메모리 한도** (실측): `maxThreadgroupMemoryLength = 32768`, `threadExecutionWidth = 32`, `maxTotalThreadsPerThreadgroup = 1024`. v2 는 1 KB 만 쓰므로 bin 이 많아질 때까지 문제없다. 8192 bin 은 꼭 32 KB 로 파이프라인 OK, 16384 bin 은 만들 때 실패했다 (연습 1):

```text
Threadgroup memory size (65536) exceeds the maximum threadgroup memory allowed (32768)
```

이때의 대안: (1) device atomic 으로 돌아가기, (2) 범위별로 나눠 여러 패스, (3) 먼저 정렬해서 같은 bin 끼리 모으기(GPGPU-M08), (4) bin 값을 줄인 뒤 2단계로 세기(거친 히스토그램 → 필요한 범위만 세밀히).

**펌웨어/검증 연결**:

- CPU 쪽 같은 아이디어 = per-thread 카운터(MEM-M04 pattern b, LLAT-B06: 8스레드 CAS 루프 24 Mops/s vs 스레드별 슬롯 6786 Mops/s). GPU 의 threadgroup 지역 메모리는 "스레드별 슬롯" 에 해당하고, 차이는 "칩 안의 빠른 지역 메모리가 공짜로 있다" 는 점이다.
- ISP 통계: 윈도(zone) 별 히스토그램은 RGBA 4채널(연습 2)처럼 칸 수가 빠르게 늘어난다. 하드웨어는 메모리 크기가 정해져 있어 "bin 수 × 윈도 수" 가 한도 — 여기의 32 KB 문제와 같은 트레이드오프다(칩마다 다름, 확인 필요).
- GPU/NPU atomic 검증(Q6): 합 불변식(합 == N)과 CPU 골든을 패턴별로 — all-same(같은 lane 충돌), lane-distinct(lane32), uniform, 부분 활성 simdgroup, 여러 threadgroup 크기. 위 측정이 보여 주듯 패턴에 따라 내부 경로가 다르므로 **한 패턴으로만 검증하면 안 된다**.

## 6. 정답 코드

호스트 (시작 코드 대비 바뀐 줄 강조):

<!-- code: solution.m diff=starter.m -->

커널:

<!-- code: solution.metal diff=starter.metal -->

```bash
clang -fobjc-arc -O2 -Wall -Wextra -Werror solution.m -framework Metal -framework Foundation -o solution && ./solution
```

실제 출력 (PASS 줄 생략, 약 1초):

```text
-- Part A: CPU reference + global atomics (hist_v1_global)
  v1 uniform  match
  v1 zipf1.2  match
  v1 same     match
-- Part B: threadgroup privatization (hist_v2_tg)
  v2 uniform  match
  v2 zipf1.2  match
  v2 same     match
-- Part C: SIMD aggregation (hist_v3_simd) + ISP auto-exposure stats
  v3 uniform  match
  v3 zipf1.2  match
  v3 same     match
  frame dark   mean 31.5 p50 31 p99 63 clip_hi 0.00% -> step +1
  frame sky    mean 176.1 p50 166 p99 255 clip_hi 40.00% -> step -1
  frame normal mean 128.7 p50 129 p99 235 clip_hi 0.37% -> step +0
26/26 passed
```

## 7. 자주 하는 실수

**틀린 v2 다섯 가지를 실제로 돌린 결과** (`demo_bugs.m`, 입력 uniform 2^24, 각 20회):

<!-- code: demo_bugs.metal -->

```bash
clang -fobjc-arc -O2 -Wall -Wextra -Werror demo_bugs.m -framework Metal -framework Foundation -o bugs && ./bugs
```

```text
uniform N = 2^24, tg 256 x ntg 1024, 20 runs each
kernel           | wrong runs | sum(|gpu-cpu|) min .. max over runs | total count min .. max (want 16777216)
ok_v2            |    0/20    |          0 .. 0          |   16777216 .. 16777216
bug_no_zero      |    0/20    |          0 .. 0          |   16777216 .. 16777216
bug_no_barrier1  |   20/20    |     245103 .. 2621269    |   14155947 .. 16532113
bug_no_barrier2  |   20/20    |    1916608 .. 2344706    |   14432510 .. 14860608
bug_plain_merge  |   20/20    |   16760832 .. 16760832   |      16384 .. 16384
bug_rmw_merge    |   20/20    |   15338518 .. 15400825   |    1376391 .. 1438698
```

| 실수 | 왜 틀렸나 (실측) | 고침 |
|---|---|---|
| 0 초기화 생략 (`bug_no_zero`) | 이 칩에서는 **20/20 맞았다**. `DIRTY=1 ./bugs` 로 threadgroup 메모리를 0xDEAD 로 채우는 커널을 먼저 돌려도 맞았다 (드라이버/하드웨어가 지우는 듯 — 확인 못 함) — 그래도 MSL 에서 threadgroup 변수는 초기값이 보장되지 않고(사양 확인 필요), 다른 GPU 의 shared memory 는 쓰레기를 담고 있다. **맞았다 ≠ 맞는 코드** | 항상 지운다 |
| 첫 barrier 생략 | 늦은 simdgroup 의 0 쓰기가 앞선 +1 을 지운다. 실측 20/20 틀림(tg 256). **tg 64 × ntg 64 에서는 0/20 틀림** — 두 simdgroup 뿐이라 우연히 안 겹친 것으로 보인다 | `threadgroup_barrier(mem_threadgroup)` |
| 둘째 barrier 생략 | 다른 simdgroup 이 세는 중에 읽어 일부를 놓친다. 실측 tg 256 에서 합이 11–14 % 부족 | barrier |
| 병합을 `hist[b] = c` 로 | threadgroup 끼리 덮어써 **마지막 하나만** 남는다: 합이 정확히 16384 = N/ntg. 레이스지만 결과는 매번 똑같이 틀린다 | `atomic_fetch_add_explicit` |
| 병합을 `hist[b] = hist[b] + c` 로 | lost update, 합이 8~9 % 만 남음 (GPGPU-B09 의 `c[0]++` 와 같다) | atomic RMW |
| 초기화/병합이 `local[lid]` 뿐 | tg < 256 이면 뒤 칸을 안 건드리고, tg > 256 이면 범위 밖이다 | `b = lid; b < 256; b += tgsz` |
| `simd_sum` 을 `if (simd_is_first())` 안에서 호출 | 활성 lane 이 하나뿐이라 합이 4 가 된다 | 분기 전에 전 lane 이 호출 |
| 반복 dispatch 전에 `hist` 안 지움 | device 버퍼는 커널이 안 지운다 (연습 3 의 2-pass 는 덮어쓰니 괜찮다) | `memset` 또는 clear 커널 |
| `waitUntilCompleted` 전에 읽기 | CPU 가 끝나기 전 값을 본다 | 완료 후 읽기 |

`MTL_SHADER_VALIDATION=1 MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1` (실행해 봄): 위 버그 5개는 **아무것도 보고되지 않았다** — 범위 밖 접근 도구지 레이스 도구가 아니다(`OOB=1 ./bugs` 의 off-by-one 커널 — `hist[256]` 쓰기 — 은 `Invalid store in 'device' or 'constant' address space at offset 1024` 로 잡혔고, 정답 코드는 0건). `REPORT_TO_STDERR` 없이는 범위 밖도 조용했다는 것은 GPGPU-B09 에 정리돼 있다. barrier 누락을 잡는 것은 도구가 아니라 **결정적 비교(골든 + 합 불변식)** 뿐이다.

## 8. 면접 Q&A

<details>
<summary>Q1. GPU 히스토그램에서 atomic 경합을 어떻게 줄입니까? 언제 효과가 없습니까?</summary>

threadgroup 지역 히스토그램을 두고 끝에 병합한다(privatization). 메모리 쪽 atomic 이 N 번에서 Σ(0 아닌 bin 수)로 줄어든다. 효과가 없는 경우: (1) 입력이 고르고 atomic 이 병목이 아닐 때 — 이 맥 실측 uniform 에서 v1 140 vs v2 148 GB/s, 둘 다 읽기 천장 근처. (2) 작은 입력 — 수 KiB 에서 v2 가 4–7 % 느림. (3) threadgroup 이 너무 많을 때 — 16384 TG 면 병합이 N/4 번. (4) bin 수가 threadgroup 메모리 한도(이 맥 32 KB = 4바이트 카운터 8192개)를 넘을 때.
꼬리: "그럼 처음부터 안 쓰면?" → 치우친 입력에서 1.47배(zipf), 한 bin 몰림에서 1.48배(+ SIMD 집계 7.2배). 입력 분포를 모르면 v2 가 안전한 기본값이다.
</details>

<details>
<summary>Q2. 왜 barrier 가 두 개 필요합니까? 하나만 빼면 어떻게 되나요? 실제로 깨집니까?</summary>

첫 barrier 는 초기화와 누적 사이(지운 뒤에 센다), 둘째는 누적과 병합 사이(다 센 뒤에 읽는다). 같은 simdgroup 안에서는 lockstep 이라 괜찮아 보이지만, simdgroup 사이는 barrier 없이 순서가 없다(MSL: 독립 진행). 실측: tg 256 에서 둘째를 빼면 20/20 틀리고 11–14 % 를 잃는다. 첫째를 빼면 20/20 틀리지만, tg 64 에서는 0/20 맞았다 — 우연이다.
꼬리: "MTL_SHADER_VALIDATION 으로 못 잡나요?" → 실행해 봤는데 barrier 누락은 보고가 0건이었다. 범위 밖 접근은 `MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1` 일 때만 보고됐다. 그래서 CPU 골든 + 합 불변식 테스트로 잡는다.
</details>

<details>
<summary>Q3. 이 패턴을 CUDA 로 옮기면?</summary>

threadgroup → `__shared__` (block 단위), `threadgroup_barrier(mem_threadgroup)` → `__syncthreads()`, simdgroup 32 → warp 32, `atomic_fetch_add_explicit` → `atomicAdd`. SIMD 집계 → `__reduce_add_sync` / `__match_any_sync`. 전역 `atomicAdd` 대 shared 지역 + 병합의 격차는 예전부터 알려져 있는데, 세대에 따라 크게 달라진다는 것이 핵심이다: shared atomic 이 네이티브 명령이 되기 전에는 비쌌고(Maxwell 이전, 블로그 기준), Apple M6 에서 잰 숫자처럼 고른 입력에서는 차이가 사라질 수 있다.
꼬리: "그럼 PMPP 책의 숫자를 인용해도 되나요?" → 세대 고정 숫자다. 칩을 바꾸면 다시 잰다.
</details>

<details>
<summary>Q4. 왜 한 bin 에 몰린 입력이 그렇게 느립니까? bin 수가 적어서입니까?</summary>

아니다. lane32 입력(bin 32개, lane 마다 서로 다름)은 v1 이 136.9 GB/s 로 빠르다. 느린 것은 한 SIMD 명령 안에서 32 lane 이 같은 주소에 atomic 을 하려 하는 경우(`same`, 20.3 GB/s)다. 같은 주소가 lane 수만큼 직렬화되는 것으로 보인다 — 내부 구현은 비공개라 측정으로 추정할 뿐이다.
해결: simdgroup 안에서 먼저 합쳐 lane 하나가 `+k` (`simd_all` + `simd_sum`), 또는 입력을 섞어 같은 값이 한 simdgroup 에 몰리지 않게 한다.
꼬리: "입력을 섞는 건 비용이 없나요?" → 메모리 접근 패턴을 망쳐 대역폭을 잃는다. 이런 건 측정해서 고른다.
</details>

<details>
<summary>Q5. bin 이 64K 개이면 어떻게 합니까?</summary>

threadgroup 메모리는 이 맥에서 32 KB(`maxThreadgroupMemoryLength`, 실측) — 8192 bin 까지. 16384 bin 은 파이프라인 생성에서 실패했다. 대안: (1) 거친 히스토그램(상위 비트) 먼저, 관심 범위만 세밀하게, (2) 값 범위를 나눠 여러 패스, (3) 정렬 기반(radix sort 의 블록 히스토그램, GPGPU-M08), (4) 그냥 device atomic — 이 칩에서는 uniform 이면 나쁘지 않다.
꼬리: "threadgroup 수를 줄이면 bin 을 늘릴 수 있나요?" → 아니다. 한도는 threadgroup 하나의 메모리다. 스레드 수를 줄여도 그대로다. 또 threadgroup 메모리를 많이 쓰면 동시에 띄울 수 있는 threadgroup 수(occupancy, GPGPU-B08)가 줄 수 있다 — Apple GPU 에서 정확한 규칙은 미확인.
</details>

<details>
<summary>Q6. 새 GPU/NPU 의 atomic 을 검증한다면 어떤 테스트를 짜겠습니까?</summary>

(1) 골든 비교: 정수 합은 결합·교환 법칙이 성립하므로 실행 순서와 무관하게 CPU 와 정확히 같아야 한다 → 결정적 CHECK. (2) 패턴별 커버리지: all-same(같은 lane 충돌), lane-distinct, uniform, 치우친 분포, 부분 활성 simdgroup, 여러 threadgroup 크기·개수. 측정에서 보듯 패턴마다 내부 경로가 다르다. (3) 불변식: 합 == N, 카운터 넘침 경계(2^32 근처), 병합 수 == 예측(연습 4). (4) 배치된 메모리 위치별(채널·뱅크 경계)로 스트레스. (5) 깨진 버전이 잡히는지(mutation): barrier 를 뺀 커널이 실제로 FAIL 하는가.
꼬리: "결과가 맞으면 끝인가요?" → 성능 회귀도 본다. 같은 입력의 GB/s 를 프로파일별로 기록한다 (단, 이 맥처럼 클럭 상태가 흔들리면 예열과 중앙값이 필수).
</details>

<details>
<summary>Q7. 왜 float 히스토그램이나 float 합을 atomic 으로 하면 위험합니까?</summary>

정수 atomic add 는 결합·교환 법칙이 정확해서 순서가 어떻든 최종 값이 같다(이 레슨 CHECK 의 근거). float 덧셈은 결합 법칙이 깨져서 실행 순서에 따라 결과가 달라진다 — 그래서 합산 재현성 문제가 생긴다 (GPGPU-A07). 히스토그램의 카운트는 정수라 안전하다.
꼬리: "uint 가 넘치면?" → 2^32 회 이상이면 wrap. 이 레슨은 N ≤ 2^26 이지만 8K 영상을 누적하면 신경 써야 한다.
</details>

<details>
<summary>Q8. ISP 에서 AE 통계를 하드웨어로 만드는 이유는 무엇이고, 펌웨어는 무엇을 합니까?</summary>

픽셀마다 CPU/GPU 를 쓰지 않으려는 것이다(칩마다 다르지만 보통 별도 블록). 펌웨어는 프레임 종료 인터럽트 뒤에 통계를 읽어 평균·백분위·포화 픽셀을 계산하고, 센서 적용 지연(2프레임 등)을 고려해 노출·게인을 정한다(CAM-M06). 검증에서는 골든 프레임(한 값만, 계단, 체커)을 넣어 합 == 픽셀 수, bin 경계, 윈도 경계를 본다 — 이 레슨의 합성 프레임 테스트가 축소판이다.
꼬리: "왜 정수인가요?" → 프레임 간 흔들림과 부동소수점 비용을 줄이고, 하드웨어 레지스터 값과 바로 비교할 수 있다.
</details>

## 9. 추가 연습

아래 넷은 `practice.m` + `practice.metal` 에 답이 있다.

**연습 1. 12비트 RAW 4096-bin 히스토그램.** 입력 `uint16_t`, 값 > 4095 는 마지막 bin 으로. 파이프라인의 `staticThreadgroupMemoryLength` 가 16 KB 이고 장치 한도 안인지 확인하고, 16384-bin(64 KB) 커널은 **파이프라인 생성이 실패** 해야 한다.

<details>
<summary>답</summary>

`threadgroup atomic_uint local[4096]` + 같은 5단계. `min(uint(v[i]), 4095u)` 로 클램프. 실측 오류: `Threadgroup memory size (65536) exceeds the maximum threadgroup memory allowed (32768)` — 라이브러리 컴파일은 통과하고 **파이프라인 만들 때** 실패한다. 그래서 `hist14_too_big` 커널을 다른 커널과 같은 .metal 에 둘 수 있다.
</details>

**연습 2. RGBA 4채널 히스토그램 (AWB 통계).** `uchar4` 픽셀, 출력 `[4][256]`. 지역 배열 4 KB. 채널별 합 == 픽셀 수.

<details>
<summary>답</summary>

`local[4*256]` 하나에 채널별 오프셋(0, 256, 512, 768)을 더해 쓴다. 초기화/병합 루프의 범위만 1024 로. A 채널이 전부 255 라 한 bin 에 몰리는 입력을 일부러 넣었다.
</details>

**연습 3. atomic 없는 2-pass.** pass 1: threadgroup 마다 지역 히스토그램을 `partial[tg*256+b]` 에 그냥 저장(device atomic 0번). pass 2: 스레드 b 가 `partial[0..ntg)[b]` 합을 `hist[b]` 에. 왜 atomic 이 없어도 되나? 왜 pass 2 가 pass 1 의 값을 보나?

<details>
<summary>답</summary>

각 칸을 쓰는 threadgroup 이 하나뿐이라 충돌이 없다. 같은 command buffer 안 **두 dispatch** 는 기본(serial) 인코더에서 앞의 쓰기가 다음에 보인다. 대가: 메모리 `ntg × 1 KB`(512 KB), 합산 시간. 얻는 것: device atomic 0번이라 구조가 단순하고 합산 순서가 고정이다(값이 정수라 결과는 어차피 같지만, float 으로 바꾸면 이 고정 순서가 재현성을 준다 — GPGPU-A07).
</details>

**연습 4. 병합 비용 모델.** 원소 i 는 `threadgroup = (i mod (tg*ntg)) / tg` 가 처리한다. CPU 로 "threadgroup 마다 0 아닌 bin 수" 합을 예측하고, 병합마다 `hist[256]` 을 증가시키는 커널의 카운터와 비교.

<details>
<summary>답</summary>

`predict_merges` 는 `(t, bin)` 처음 본 쌍을 센다. 실측(N = 2^22, tg 256): uniform ntg 1 / 64 / 1024 → 256 / 16,384 / 262,144, 모두 GPU 카운터와 일치. 전부 같은 값이면 ntg 번. v1 의 device atomic 수는 N 이므로 ratio 는 16배(uniform, ntg 1024) ~ 4,194,304배(same, ntg 1).
</details>

### 한 번에 검증

호스트:

<!-- code: practice.m -->

커널:

<!-- code: practice.metal -->

```bash
clang -fobjc-arc -O2 -Wall -Wextra -Werror practice.m -framework Metal -framework Foundation -o practice && ./practice
```

```text
-- 연습 1: 12-bit RAW, 4096 bins
  hist14_too_big: Threadgroup memory size (65536) exceeds the maximum threadgroup memory allowed (32768)
-- 연습 2: RGBA 4-channel histograms
-- 연습 3: atomic-free two-pass (partial + column reduce)
-- 연습 4: merge-cost model
  uniform  ntg    1: device atomics v1 = 4194304, v2 merge = 256 (pred 256), ratio 16384x
  uniform  ntg   64: device atomics v1 = 4194304, v2 merge = 16384 (pred 16384), ratio 256x
  uniform  ntg 1024: device atomics v1 = 4194304, v2 merge = 262144 (pred 262144), ratio 16x
  same     ntg    1: device atomics v1 = 4194304, v2 merge = 1 (pred 1), ratio 4194304x
  same     ntg   64: device atomics v1 = 4194304, v2 merge = 64 (pred 64), ratio 65536x
  same     ntg 1024: device atomics v1 = 4194304, v2 merge = 1024 (pred 1024), ratio 4096x
  16 bins  ntg    1: device atomics v1 = 4194304, v2 merge = 16 (pred 16), ratio 262144x
  16 bins  ntg   64: device atomics v1 = 4194304, v2 merge = 1024 (pred 1024), ratio 4096x
  16 bins  ntg 1024: device atomics v1 = 4194304, v2 merge = 16384 (pred 16384), ratio 256x
10/10 passed
```

## 10. 핵심 정리

- privatization = threadgroup 지역 히스토그램(0 초기화 → barrier → 누적 → barrier → bin 당 device atomic 1번 병합). 메모리 쪽 atomic 이 N → Σ(0 아닌 bin 수).
- 이 맥 실측: uniform 은 효과 없음(둘 다 대역폭 천장 약 140–150 GB/s), zipf 1.47배, 한 bin 1.48배, SIMD 집계를 더하면 7.2배.
- 병목은 bin 이 적은 것이 아니라 **한 SIMD 명령 안에서 같은 주소를 치는 lane** 이다 (lane32 는 bin 32개인데도 빠르다).
- privatization 이 안 되는 곳: 수 KiB 이하(launch·병합 오버헤드), 너무 많은 threadgroup(16384 → 병합 N/4), bin 수 > 32 KB/4.
- barrier 를 빼면 깨진다(실측 20/20), 그러나 `bug_no_zero` 처럼 "돌아서 맞는" 버그도 있다. `MTL_SHADER_VALIDATION` 은 barrier 누락을 못 잡는다 → 골든 + 합 불변식.
- GPU 시간은 클럭 상태에 흔들린다(v1 0.11 vs 0.378 ms) → 예열, 번갈아 돌리기, 중앙값, **배수만** 믿기.
- CPU 쪽 같은 생각 = per-thread 카운터 (MEM-M04 b). 펌웨어 쪽 = ISP AE/AWB 하드웨어 통계 + 펌웨어 판단.

## 참고자료

- Hwu, Kirk, El Hajj, *Programming Massively Parallel Processors*, 4th ed. — parallel histogram 장(atomic 과 privatization; 장 번호 확인 필요).
- Apple, *Metal Shading Language Specification* — atomic functions, `threadgroup_barrier` / `simdgroup_barrier`, SIMD-group functions (`simd_broadcast_first`, `simd_sum` 등), 메모리 일관성. 절 번호는 GPGPU-B09 참고.
- Apple Metal 문서 — `MTLCommandBuffer.GPUStartTime` / `GPUEndTime`, `MTLDevice.maxThreadgroupMemoryLength`, `MTLComputePipelineState.staticThreadgroupMemoryLength` / `threadExecutionWidth`.
- NVIDIA, *CUDA C++ Programming Guide* — atomic functions, `__syncthreads`, `__match_any_sync`, `__reduce_add_sync`.
- NVIDIA Developer Blog — "GPU Pro Tip: Fast Histograms Using Shared Atomics on Maxwell" (Sakharnykh, 2015), "Optimized Filtering with Warp-Aggregated Atomics" (Adinets, 2014) — 제목·연도는 기억에 의존, 확인 필요.
- 이 저장소: GPGPU-B09 (atomic · barrier 기초, 주소 수 스윕), GPGPU-M08 (radix sort), GPGPU-A07 (수치 결정성), MEM-M04 · LLAT-B06 (CPU 쪽 per-thread 카운터), CAM-M06 (AE 제어 루프), ARM-M07 · ARM-A11 (CPU 히스토그램).
