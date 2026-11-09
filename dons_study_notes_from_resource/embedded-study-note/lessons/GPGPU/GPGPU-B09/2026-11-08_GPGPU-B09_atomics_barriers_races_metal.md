---
id: GPGPU-B09
time: 65분
updated: 2026-11-08
---
# GPGPU-B09 Atomic과 Barrier: 레이스와 동기화

## 학습 목표

- GPU 에서 `c[0]++` 가 **2^20 번 중 1 번만 남는** 이유를 SIMD lockstep(32 lane 이 한 명령으로 load → 그다음 store)과 device 메모리의 가시성 범위로 설명하고, `atomic_fetch_add_explicit(..., memory_order_relaxed)` 로 정확히 고친다.
- `threadgroup_barrier(mem_flags::mem_threadgroup)` 가 하는 두 가지(실행 맞추기 + threadgroup 메모리 쓰기 보이기)를 말하고, barrier 를 빼면 **다른 simdgroup 의 칸을 읽을 때만** 틀린다는 것을 실측(T=32 → 0, T≥64 → 약 50%)으로 보여 준다. "돌려 보니 맞더라" 가 증명이 아닌 이유도 같이.
- Metal 메모리 모델 단어 — relaxed 만 되는 device atomic(MSL 4.0), `atomic_thread_fence` + `thread_scope`, `coherent(device)`, `simdgroup_barrier` vs `threadgroup_barrier`, 커널 안 grid barrier 없음(→ dispatch 나누기) — 를 CUDA 용어(`__syncthreads`, `__syncwarp`, `atomicAdd`, `__threadfence`)와 짝지어 말한다.
- atomic 경합(주소 1~1024개)에 따른 처리량을 재고, **"같은 SIMD 명령 안에서 같은 주소를 친 lane 수"** 가 비용을 정한다는 것을 숫자(1 주소 20 → 8+ 주소 85 Gop/s)로 읽는다.
- `MTL_SHADER_VALIDATION=1` 이 잡는 것(범위 밖 접근)과 못 잡는 것(레이스)을 실제로 돌려 보고 안다.

## 1. 문제

GPU 커널 면접(NVIDIA·Apple·AI 칩)에서 reduction·histogram·scan 문제는 결국 "어디에 barrier, 어디에 atomic?" 질문으로 끝나고, 가속기 펌웨어·실리콘 검증에서는 그 atomic 유닛과 barrier 하드웨어가 **정확히 세는지**를 directed test 로 확인한다. CPU 에서 본 `counter++` 레이스(OS-B04), ISR↔main 공유 변수(C-M14)와 같은 문제가 수천 개 스레드 규모로 나타난다 — 그래서 잘못되면 훨씬 극적으로 틀린다.

이미 아는 것(다시 가르치지 않는다): CPU 의 lost update 와 mutex/atomic 수정(OS-B04, OS-B05), C11 atomic·release/acquire(C-A03), atomic 비용과 경합(LLAT-B06), 캐시 라인 경합(OS-M07, ARCH-M08 은 병렬 작성 중). 여기서는 **GPU 의 실행 단위와 메모리 범위** 위에서 같은 질문을 다시 푼다. 히스토그램 privatization 은 GPGPU-M03 몫이다.

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">범위(scope)마다 공유하는 메모리와 맞추는 도구가 다르다 (Apple M6 GPU 실측값 포함)</text>
<text x="20" y="44" fill="currentColor" font-weight="bold">범위</text><text x="250" y="44" fill="currentColor" font-weight="bold">같이 보는 메모리</text><text x="460" y="44" fill="currentColor" font-weight="bold">맞추는 도구</text>
<rect x="10" y="54" width="220" height="40" rx="5" class="f-mut" stroke="currentColor"/><text x="20" y="72" fill="currentColor">thread (1)</text><text x="20" y="88" fill="currentColor" font-size="12">자기 레지스터</text>
<text x="250" y="80" fill="currentColor">thread 메모리</text><text x="460" y="80" fill="currentColor">필요 없음 (프로그램 순서)</text>
<rect x="10" y="102" width="220" height="40" rx="5" class="f-ok" stroke="currentColor"/><text x="20" y="120" fill="currentColor">simdgroup (32 lane)</text><text x="20" y="136" fill="currentColor" font-size="12">threadExecutionWidth = 32</text>
<text x="250" y="128" fill="currentColor">simd_shuffle / simd_sum</text><text x="460" y="128" fill="currentColor" font-family="monospace" font-size="12">simdgroup_barrier(...)</text>
<rect x="10" y="150" width="220" height="40" rx="5" class="f-acc" stroke="currentColor"/><text x="20" y="168" fill="currentColor">threadgroup (≤ 1024)</text><text x="20" y="184" fill="currentColor" font-size="12">GPU 코어 하나에서 실행</text>
<text x="250" y="168" fill="currentColor">threadgroup 메모리</text><text x="250" y="184" fill="currentColor" font-size="12">최대 32 KB (32768)</text><text x="460" y="176" fill="currentColor" font-family="monospace" font-size="12">threadgroup_barrier(mem_threadgroup)</text>
<rect x="10" y="198" width="220" height="40" rx="5" class="f-warn" stroke="currentColor"/><text x="20" y="216" fill="currentColor">grid (dispatch 하나)</text><text x="20" y="232" fill="currentColor" font-size="12">GPU 코어 12개 전부</text>
<text x="250" y="216" fill="currentColor">device 메모리 (MTLBuffer)</text><text x="250" y="232" fill="currentColor" font-size="12">기본: threadgroup 범위만</text><text x="460" y="216" fill="currentColor">atomic + fence, grid barrier 없음</text><text x="460" y="232" fill="currentColor">→ dispatch 를 나눈다</text>
<rect x="10" y="246" width="220" height="40" rx="5" class="f-mut" stroke="currentColor"/><text x="20" y="264" fill="currentColor">CPU ↔ GPU</text><text x="20" y="280" fill="currentColor" font-size="12">통합 메모리, shared storage</text>
<text x="250" y="272" fill="currentColor">같은 MTLBuffer</text><text x="460" y="272" fill="currentColor" font-family="monospace" font-size="12">waitUntilCompleted / handler</text>
<path d="M236 74 L244 74 M236 122 L244 122 M236 170 L244 170 M236 218 L244 218 M236 266 L244 266" stroke="currentColor"/>
<text x="10" y="310" fill="currentColor" font-size="12">아래로 갈수록 범위가 넓고 맞추는 비용이 크다.</text><text x="10" y="328" fill="currentColor" font-size="12">좁은 범위의 도구로 넓은 범위를 맞출 수 없다 (예: simdgroup_barrier 로 threadgroup 을).</text>
</svg>
```

Metal 과 CUDA 용어를 한 번 짝지어 두면 면접에서 섞여 나와도 헷갈리지 않는다.

| Metal (MSL) | CUDA | 이 레슨에서 |
|---|---|---|
| grid / threadgroup / simdgroup(32) | grid / block / warp(32) | `dispatchThreadgroups:threadsPerThreadgroup:` |
| `threadgroup` 메모리 (`[[threadgroup(0)]]`) | `__shared__` | Part B 의 `tg[]` |
| `threadgroup_barrier(mem_flags::mem_threadgroup)` | `__syncthreads()` | Part B |
| `simdgroup_barrier(...)` | `__syncwarp()` | demo B (부족한 예) |
| `atomic_fetch_add_explicit(p, v, memory_order_relaxed)` | `atomicAdd(p, v)` (relaxed) | Part A, C |
| `atomic_thread_fence(mem_device, seq_cst, thread_scope_device)` | `__threadfence()` | 연습 5 |
| 커널 안 grid barrier 없음 → dispatch 분리 | cooperative launch `grid.sync()` 또는 커널 분리 | 연습 2 |

### Part A — 전역 카운터: plain vs atomic

- `count_plain` 은 이미 있다: 스레드마다 `c[0]++`. 결과를 **찍기만** 한다(CHECK 아님 — 레이스 결과는 비결정적).
- `count_atomic` 을 채운다: `gid < n` 인 스레드가 `atomic_fetch_add_explicit(c, 1u, memory_order_relaxed)`. n = 2^20 과 1000 에서 정확히 n.
- 관찰: `demo_race.m` A·C — plain / `volatile` / `coherent(device)` / atomic 을 n = 2^8..2^20 에서, 그리고 plain 을 10 dispatch 로 나눠서.

### Part B — threadgroup 메모리 + barrier

- `tg_reverse`: 그룹(T 스레드)마다 `tg[lid] = in[gid]` → **barrier** → `out[gid] = tg[m-1-lid]` (그룹 안에서 순서 뒤집기, m = 그 그룹의 유효 원소 수). T ≥ 64 이면 읽는 칸이 **다른 simdgroup** 의 것이다.
- 마지막 그룹은 덜 찰 수 있다(n = 1000, T = 256 → 232 개). 범위 밖 스레드도 barrier 까지는 와야 한다.
- 호스트에 CPU 골든 모델 `ref_reverse` 를 채운다. CHECK = GPU 결과가 골든 모델과 전부 같음 (T = 32, 64, 256, 1024).
- 관찰: `demo_race.m` B — barrier 없음 / `simdgroup_barrier` / `threadgroup_barrier(mem_none)` / 정답, 일부 simdgroup 을 일부러 늦춘 경우까지 틀린 원소 수.

### Part C — atomic 경합 sweep

- `spread_add`: n 번의 atomic add 를 주소 `gid % naddr` 에. 호스트의 `expect_at(n, naddr, a)` (주소 a 의 기대값)를 채운다. CHECK = naddr = 1, 2, 4, …, 1024 전부와 나누어떨어지지 않는 n 에서 주소마다 정확히 기대값, 합 = n.
- 관찰: `demo_contention.m` — 2^22 add, 주소 배치 3가지(interleaved 4 B 간격 / interleaved 128 B 간격 / blocked) × naddr → Gop/s.

## 2. Problem (English)

You are writing Metal compute kernels (MSL 4.0, Apple GPU, SIMD width 32) and must make them race-free.

**Part A.** `kernel void count_atomic(device atomic_uint *c, constant uint &n, uint gid)` — every thread with `gid < n` increments `*c` exactly once. After a dispatch of `ceil(n/256)` threadgroups of 256, `*c == n`. A given `count_plain` kernel doing `c[0]++` is run for observation only.

**Part B.** `kernel void tg_reverse(device const uint *in, device uint *out, constant uint &n, threadgroup uint *tg, ...)` — threadgroup `g` with `T` threads covers `base = g*T`, `m = min(T, n - base)` elements; produce `out[base + l] = in[base + m - 1 - l]` for `l < m`, using threadgroup memory (`T*4` bytes set by the host). Implement the CPU golden model `void ref_reverse(const uint32_t *in, uint32_t *out, uint32_t n, uint32_t T)`. Must be exact for T in {32, 64, 256, 1024}, including a partial last threadgroup. Example: `in = 10..17, T = 4` → `13 12 11 10 17 16 15 14`; with `n = 6` → `13 12 11 10 15 14`.

**Part C.** `kernel void spread_add(device atomic_uint *bins, constant uint &n, constant uint &naddr, uint gid)` adds 1 to `bins[gid % naddr]`. Implement `uint32_t expect_at(uint32_t n, uint32_t naddr, uint32_t a)`. Example: `expect_at(10, 4, ·) = 3, 3, 2, 2`. Verify for `n = 2^22`, `naddr = 1..1024` (powers of two) and `n = 2^22 + 7, naddr = 3`.

Constraints: all checks on the CPU side, exact counts only, no timing in checks; the starter must fail deterministically (not by luck of a race). Follow-ups: why is `c[0]++` almost always 1? Why does a missing barrier "work" for T = 32? What limits atomic throughput at 1 address vs 1024?

## 3. 시작 코드

**레슨 폴더에서 빌드하고 실행한다.** 호스트는 `__FILE__`(= `starter.m`)의 확장자를 `.metal` 로 바꾼 파일을 현재 폴더에서 읽어 `-[MTLDevice newLibraryWithSource:options:error:]` 로 실행 중에 컴파일한다 — 이 맥(Command Line Tools 만 있음)에는 `metal` 오프라인 셰이더 컴파일러가 없기 때문이다. 기본 `MTLCompileOptions.languageVersion` 은 `0x40000` = MSL 4.0 (실측).

<!-- code: starter.m -->

<!-- code: starter.metal -->

```bash
cd lessons/GPGPU/GPGPU-B09
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
mkdir -p /tmp/GPGPU-B09_work
clang -fobjc-arc -O2 -Wall -Wextra -Werror starter.m -framework Metal -framework Foundation -o /tmp/GPGPU-B09_work/starter
/tmp/GPGPU-B09_work/starter          # 0/13 passed, exit 1
```

starter 의 실패는 레이스 운에 기대지 않는다: TODO 커널은 아무것도 쓰지 않아서 카운터 = 0, `out[]` = 0xFFFFFFFF 그대로, 호스트 TODO 는 0 을 돌려준다. 실제 출력(앞부분):

```text
device: Apple M6
INFO  plain c[0]++ : 1 / 1048576  (lost 1048575 — 관찰값, CHECK 아님)
FAIL  count_on_gpu("count_atomic", N) == N  (line 142)
FAIL  count_on_gpu("count_atomic", 1000) == 1000  (line 143)
FAIL  o8[0] == 13 && o8[3] == 10 && o8[4] == 17 && o8[7] == 14  (line 148)
...
FAIL  spread_ok(M + 7, 3)  (line 166)
0/13 passed
```

## 4. 힌트

<details>
<summary>힌트 1 — Part A 는 한 줄</summary>

MSL 의 atomic 함수는 C11 처럼 `_explicit` 꼴이고, device atomic 에 MSL 4.0 은 `memory_order_relaxed` 만 받는다. 버퍼 타입이 이미 `device atomic_uint *` 이니 `atomic_fetch_add_explicit(c, 1u, memory_order_relaxed)`. `gid < n` 검사는 잊지 말 것 (마지막 그룹의 남는 스레드).
</details>

<details>
<summary>힌트 2 — Part B: barrier 를 어디에?</summary>

모든 스레드가 `tg[lid]` 를 쓴 **다음**, 그리고 누구든 남의 칸을 읽기 **전**. barrier 는 그룹의 모든 스레드가 만나야 하므로 `if (lid < m)` 안에 넣으면 안 된다 — barrier 는 if 밖, 읽기/쓰기만 if 안.
</details>

<details>
<summary>힌트 3 — Part B 골든 모델</summary>

`for (base = 0; base < n; base += T) { m = min(T, n - base); for (l < m) out[base+l] = in[base+m-1-l]; }`. 손으로 푼 예(10..17, T=4, n=8 과 n=6)를 먼저 통과시키면 GPU 비교가 쉬워진다.
</details>

<details>
<summary>힌트 4 — Part C 기대값</summary>

i = 0..n-1 이 주소 i % naddr 에 하나씩 → 모든 주소가 ⌊n/naddr⌋ 를 받고, 앞쪽 n % naddr 개 주소가 하나씩 더 받는다.
</details>

<details>
<summary>힌트 5 — "barrier 없이도 맞던데요?"</summary>

T = 32 면 그룹 = simdgroup 하나다. 32 lane 이 같은 store 명령을 함께 끝낸 뒤 load 를 하니 이 GPU 에서는 맞게 나온다. T = 64 로 바꿔 보라 (demo_race B).
</details>

## 5. 풀이

### Part A — 왜 `c[0]++` 는 2^20 번 해도 1 인가

`demo_race` A·C 실측 (Apple M6 GPU 12코어, macOS 27.2, `uptime` load 14.25 — 다른 세션이 CPU 를 쓰는 중이지만 숫자는 실행 간 거의 같았다):

| 커널 | n = 256 | 2^10 | 2^14 | 2^17 | 2^20 |
|---|---|---|---|---|---|
| plain `c[0]++` | 1 ×5 | 1 ×5 | 1 ×5 | 1 ×5 | 1 ×5 |
| `volatile device uint*` | 1 | 1 | 2 | 9–10 | 67–169 |
| `coherent(device) device uint*` | 1 | 1 | 1–2 | 10 | 61–69 |
| `atomic_fetch_add_explicit` relaxed | 256 | 1024 | 16384 | 131072 | **1048576** |
| plain, **10 dispatch** 로 나눠 (n = 1 / 32 / 2^20 각각) | | | | | **10** |

읽는 법:

1. **simdgroup 안**: 32 lane 이 `load c[0]` 를 한 명령으로 함께 하고(모두 0), 그다음 `store 1` 을 함께 한다. 32 개 증가가 한 번에 1 개로 줄어든다. CPU 의 OS-B04 는 두 스레드가 드물게 겹쳤지만, GPU 는 **겹치는 게 기본** 이다.
2. **threadgroup·grid 사이**: n = 256 (그룹 하나, simdgroup 8 개)도 1 이고 2^20 (그룹 4096 개, GPU 코어 12 개에 여러 차례 나눠 실행)도 1 이다. 늦게 시작한 그룹조차 앞 그룹이 쓴 값을 못 봤다는 뜻이다. MSL 스펙 4.8 Memory Coherency: *"By default, memory in the device address space has threadgroup coherence"* — 한 dispatch 안에서 다른 threadgroup 의 일반 store 가 보일 의무가 없다. (정확히 어느 캐시에 값이 머무는지는 확인 안 함 — 가설.) `volatile` / `coherent(device)` 를 붙이면 61–169 로 조금 늘 뿐, 여전히 99.99% 가 사라진다: 가시성을 넓혀도 **load–add–store 가 원자적이 되지는 않는다.**
3. **dispatch 경계**: 같은 encoder 에 plain 커널을 10 번 넣으면 2^20 스레드라도 정확히 10 — 앞 dispatch 의 쓰기는 다음 dispatch 에 보인다. 기본 encoder 는 `MTLDispatchTypeSerial` 이라 dispatch 를 차례로 실행한다(Metal 문서 기준. 실측은 10 으로 일치). 이게 "커널 안 grid barrier 대신 dispatch 를 나눈다" 의 근거다(연습 2).
4. **atomic**: RMW 하나가 L2/메모리 쪽에서 원자적으로 처리돼 정확히 n. relaxed 로 충분한 이유 — 카운터는 "다른 메모리를 이 값과 함께 보이게" 할 필요가 없고, 원자성 + 수정 순서 일관성(modification order)만 필요하다 (MSL 6.16.1.1: *"they guarantee atomicity and modification order consistency... updating counters"*). 커널이 끝나고 `waitUntilCompleted` 가 돌아오면 CPU 가 그 값을 읽는다.

relaxed 말고는 안 되는가? 이 맥의 런타임 컴파일러(GPUCompiler 32023)로 직접 컴파일해 본 결과:

```text
[fetch_add acquire ver=0x0] FAIL   error: use of undeclared identifier 'memory_order_acquire'; did you mean 'memory_order_relaxed'?
[fetch_add seq_cst ver=0x0] FAIL   error: no matching function for call to 'atomic_fetch_add_explicit'
[fence seq_cst device ver=0x0] OK  atomic_thread_fence(mem_flags::mem_device, memory_order_seq_cst, thread_scope_device)
[lang 3.1 fence ver=0x30001] FAIL  error: use of undeclared identifier 'memory_order_seq_cst' / 'thread_scope_device'
[41 fetch_add acq+flags ver=0x40001] OK   atomic_fetch_add_explicit(c, 1u, memory_order_acq_rel, mem_flags::mem_device)
```

즉 MSL 4.0(기본)에서 device atomic 의 order 는 relaxed 만, 순서가 필요하면 `atomic_thread_fence`(MSL 3.2+) 를 쓴다. 스펙 4.1(2026-06-04 판) 1.3 이 "memory order (including acquire and release) to barriers and atomics" 를 추가했고, SDK 26.4 헤더에는 `MTLLanguageVersion4_1` 상수가 없지만 원시 값 `(4<<16)+1` 을 넣으면 이 OS 컴파일러가 받아들였다 — 그때도 relaxed 가 아니면 `mem_flags` 인자가 필요했다("'order' argument must be 'metal::memory_order_relaxed' if no 'mem_flags' argument is provided"). 4.1 의 의미는 실행해서 검증하지 않았다.

### Part B — barrier 가 막는 것

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">T = 64: simdgroup 0 (lane 0–31) 은 tg[63..32] 를, simdgroup 1 은 tg[31..0] 을 읽는다</text>
<text x="10" y="48" fill="currentColor" font-weight="bold">barrier 없음</text>
<text x="10" y="76" fill="currentColor">sg 0</text><rect x="60" y="60" width="140" height="26" class="f-ok" stroke="currentColor"/><text x="68" y="78" fill="currentColor">tg[0..31] 쓰기</text>
<rect x="210" y="60" width="170" height="26" class="f-warn" stroke="currentColor"/><text x="218" y="78" fill="currentColor">tg[63..32] 읽기 ✗</text>
<text x="10" y="114" fill="currentColor">sg 1</text><rect x="120" y="98" width="230" height="26" class="f-mut" stroke="currentColor"/><text x="128" y="116" fill="currentColor">(늦게 시작 / 다른 일)</text>
<rect x="360" y="98" width="150" height="26" class="f-ok" stroke="currentColor"/><text x="368" y="116" fill="currentColor">tg[32..63] 쓰기</text>
<rect x="520" y="98" width="150" height="26" class="f-ok" stroke="currentColor"/><text x="528" y="116" fill="currentColor">tg[31..0] 읽기 ✓</text>
<path d="M300 86 L380 98" stroke="currentColor" stroke-dasharray="4 3" fill="none"/><text x="390" y="80" fill="currentColor" font-size="12">아직 안 쓴 칸 = 이전 그룹의 값</text>
<text x="10" y="164" fill="currentColor" font-weight="bold">threadgroup_barrier(mem_flags::mem_threadgroup)</text>
<text x="10" y="192" fill="currentColor">sg 0</text><rect x="60" y="176" width="140" height="26" class="f-ok" stroke="currentColor"/><text x="68" y="194" fill="currentColor">tg[0..31] 쓰기</text>
<rect x="200" y="176" width="310" height="26" class="f-mut" stroke="currentColor"/><text x="210" y="194" fill="currentColor">barrier 에서 대기</text>
<rect x="530" y="176" width="170" height="26" class="f-ok" stroke="currentColor"/><text x="538" y="194" fill="currentColor">tg[63..32] 읽기 ✓</text>
<text x="10" y="230" fill="currentColor">sg 1</text><rect x="120" y="214" width="230" height="26" class="f-mut" stroke="currentColor"/><text x="128" y="232" fill="currentColor">(늦게 시작 / 다른 일)</text>
<rect x="360" y="214" width="150" height="26" class="f-ok" stroke="currentColor"/><text x="368" y="232" fill="currentColor">tg[32..63] 쓰기</text>
<rect x="530" y="214" width="170" height="26" class="f-ok" stroke="currentColor"/><text x="538" y="232" fill="currentColor">tg[31..0] 읽기 ✓</text>
<path d="M520 168 L520 248" stroke="currentColor" stroke-width="3"/><text x="440" y="264" fill="currentColor" font-size="12">전원 도착 + 앞의 tg 쓰기가 보임</text>
<text x="10" y="290" fill="currentColor" font-size="12">MSL 5.2.3.6: SIMD-group 들은 barrier 가 없으면 서로 독립적으로 진행한다 (independent forward progress).</text>
</svg>
```

`demo_race` B 실측 (n = 2^20, 틀린 원소 수, 5 회, load 14.25):

| 변형 | T = 32 | T = 64 | T = 256 | T = 1024 |
|---|---|---|---|---|
| barrier 없음 | 0 ×5 | 476640–509760 | 504480–518624 | 514560–518656 |
| `simdgroup_barrier(mem_threadgroup)` 만 | 0 ×5 | 228800–257984 | 421216–466208 | 476064–497504 |
| `threadgroup_barrier(mem_none)` | 0 | 0 | 0 | 0 |
| `threadgroup_barrier(mem_threadgroup)` (정답) | 0 | 0 | 0 | 0 |
| 홀수 simdgroup 지연(skew 2000) + barrier 없음 | 0 | 524288 ×5 | 524256–524288 | 524224–524288 |
| 같은 지연 + 정답 barrier | 0 | 0 | 0 | 0 |

- **T = 32 는 barrier 없이 0**: 그룹이 simdgroup 하나라 32 lane 의 store 명령이 끝난 뒤 load 가 나온다. 이건 *이 GPU 의 실행 방식* 이지 스펙의 약속이 아니다 — 같은 simdgroup 안의 threadgroup 메모리 교환이라도 최소 `simdgroup_barrier` 를 두는 게 정석이다. CUDA 는 Volta 부터 warp 안 스레드도 독립 스케줄(independent thread scheduling)이라 이런 "warp-synchronous" 코드가 실제로 깨진다고 문서화했다(CUDA Programming Guide, 확인 필요한 절 번호는 참고자료).
- **T ≥ 64 는 약 절반이 틀린다**, 늦추면 **정확히 절반(524288)**: 늦은 simdgroup 의 칸을 읽은 쪽이 전부 틀린다. 틀린 값은 같은 GPU 코어에서 앞서 돈 threadgroup 의 찌꺼기다(입력을 실행마다 다른 salt 로 만들어 우연히 맞을 수 없게 했다).
- **`simdgroup_barrier` 는 부족**: 자기 simdgroup 끼리만 맞춘다. 숫자가 줄어든 건 타이밍이 바뀐 것일 뿐 0 이 아니다.
- **`mem_none` 이 0 인 건 운**: 스펙 Table 6.13 은 `mem_none` 을 "execution barrier only, doesn't apply a memory fence" 로 정의한다. 이 GPU 에선 전원 도착만으로 쓰기가 보였지만, 다른 GPU·컴파일러에서 보장되지 않는다. 쓰는 메모리에 맞는 플래그를 준다.
- **"돌려 보니 맞더라" 가 증명이 아닌 이유**: T = 32 처럼 하드웨어가 우연히 순서를 지켜 주는 구간이 있고, 스케줄이 바뀌면(다른 칩, 다른 occupancy, 셰이더 검증을 켜서 코드가 바뀜) 갑자기 틀린다. 정답 커널은 **skew 로 일부러 순서를 뒤섞어도** 0 이어야 한다 — 이게 이 demo 의 진짜 테스트다.

부분 그룹과 barrier 위치: n = 1000, T = 256 이면 마지막 그룹에 유효 스레드 232 개 + 남는 24 개. 남는 스레드가 `if (gid >= n) return;` 으로 먼저 나가면 스펙 6.10.1 의 "모든 스레드가 barrier 를 만나야 한다" 를 어긴다. (스펙은 "On Apple silicon, a thread that has ended no longer participates or blocks remaining threads at a barrier" 라고 덧붙인다 — Apple GPU 에서는 멈추지 않지만 이식성 없는 코드다.) 그래서 정답은 모든 스레드가 `tg[lid]` 를 쓰고(범위 밖은 0), barrier 를 지나고, **쓰기만** `if (lid < m)` 안에서 한다. `if (l < 128) threadgroup_barrier(...)` 처럼 조건문 안 barrier 는 런타임 컴파일러가 **오류 없이 컴파일했다**(실측) — 컴파일러가 막아 주지 않는다. 실행은 GPU 를 멈출 수 있어 돌리지 않았다.

### Part C — atomic 경합: 무엇이 처리량을 정하나

`demo_contention` 실측 (2^22 add/dispatch, T = 256, dispatch 16 번을 한 command buffer 에 넣고 GPU 시간 ÷ 16, 7 회 중앙값, `uptime` load 10.25; 세 번 다시 돌려도 소수 둘째 자리까지 거의 같았다 — 그래도 **배수** 로 읽을 것):

| naddr | interleaved, 4 B 간격 | interleaved, 128 B 간격 | blocked, 4 B 간격 |
|---|---|---|---|
| 1 | 0.208 ms · 20.13 Gop/s | 20.13 | 20.13 |
| 2 | 39.24 | 39.24 | 20.13 |
| 4 | 74.72 | 40.50 | 20.13 |
| 8 | 84.59 | 79.23 | 20.13 |
| 16 | 84.64 | 51.10 | 20.13 |
| 32 | 84.60 | 78.20 | 20.13 |
| 64 | 84.60 | 68.11 | 20.13 |
| 256 | 84.57 | 61.43 | 20.21 |
| 1024 | 84.59 | 26.73 | 21.66 |

바닥선: `nop`(스레드만 띄움) 0.027 ms = 154 G threads/s, `store_only`(자기 칸에 쓰기, 16 MB) 0.091 ms = 46 G/s, `simd_sum` 으로 모은 뒤 simdgroup 당 atomic 1 번(주소 1개) 0.044 ms = 96 G adds/s(논리).

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">atomic add 처리량 vs 주소 수 (Apple M6 GPU, 2^22 add, 실측 중앙값)</text>
<path d="M80 50 L80 290 L690 290" stroke="currentColor" fill="none"/>
<path d="M76 242 L690 242 M76 194 L690 194 M76 146 L690 146 M76 98 L690 98 M76 50 L690 50" stroke="currentColor" stroke-dasharray="1 5" fill="none"/>
<text x="42" y="294" fill="currentColor">0</text><text x="42" y="246" fill="currentColor">20</text><text x="42" y="198" fill="currentColor">40</text><text x="42" y="150" fill="currentColor">60</text><text x="42" y="102" fill="currentColor">80</text><text x="36" y="54" fill="currentColor">100</text>
<text x="10" y="170" fill="currentColor" transform="rotate(-90 20 170)">Gop/s</text>
<text x="76" y="308" fill="currentColor">1</text><text x="136" y="308" fill="currentColor">2</text><text x="196" y="308" fill="currentColor">4</text><text x="256" y="308" fill="currentColor">8</text><text x="312" y="308" fill="currentColor">16</text><text x="372" y="308" fill="currentColor">32</text><text x="432" y="308" fill="currentColor">64</text><text x="488" y="308" fill="currentColor">128</text><text x="548" y="308" fill="currentColor">256</text><text x="608" y="308" fill="currentColor">512</text><text x="664" y="308" fill="currentColor">1024</text>
<text x="330" y="326" fill="currentColor">naddr (주소 수, 로그 눈금)</text>
<path d="M80 59.8 L690 59.8" stroke="currentColor" stroke-dasharray="2 4" fill="none"/><text x="470" y="72" fill="currentColor">simd_sum 집계, 주소 1개: 96 (논리)</text>
<path d="M80 179 L690 179" stroke="currentColor" stroke-dasharray="2 4" fill="none"/><text x="560" y="174" fill="currentColor">store_only 46</text>
<polyline points="80,241.7 140,195.8 200,110.7 260,87.0 320,86.9 380,87.0 440,87.0 500,86.6 560,87.0 620,86.9 680,87.0" stroke="currentColor" stroke-width="2.5" fill="none"/>
<polyline points="80,241.7 140,195.8 200,192.8 260,99.8 320,167.4 380,102.3 440,126.5 500,121.5 560,142.6 620,184.2 680,225.8" stroke="currentColor" stroke-width="2" stroke-dasharray="7 4" fill="none"/>
<polyline points="80,241.7 140,241.7 200,241.7 260,241.7 320,241.7 380,241.7 440,241.7 500,241.6 560,241.5 620,240.9 680,238.0" stroke="currentColor" stroke-width="2" stroke-dasharray="2 3" fill="none"/>
<circle cx="200" cy="110.7" r="4" class="f-ok" stroke="currentColor"/><circle cx="680" cy="87.0" r="4" class="f-ok" stroke="currentColor"/><circle cx="680" cy="225.8" r="4" class="f-warn" stroke="currentColor"/><circle cx="80" cy="241.7" r="4" class="f-acc" stroke="currentColor"/>
<rect x="96" y="112" width="250" height="62" rx="4" class="f-mut" stroke="currentColor"/>
<path d="M104 126 L134 126" stroke="currentColor" stroke-width="2.5"/><text x="140" y="130" fill="currentColor">interleaved, 4 B 간격</text>
<path d="M104 144 L134 144" stroke="currentColor" stroke-width="2" stroke-dasharray="7 4"/><text x="140" y="148" fill="currentColor">interleaved, 128 B 간격</text>
<path d="M104 162 L134 162" stroke="currentColor" stroke-width="2" stroke-dasharray="2 3"/><text x="140" y="166" fill="currentColor">blocked (simdgroup 전체가 한 주소)</text>
<text x="10" y="350" fill="currentColor">주소 1개 → 20.1, 8개 이상 → 84.6 (×4.2). blocked 는 주소를 1024 개로 늘려도 20 근처.</text>
</svg>
```

해석 (측정에서 끌어낸 모델 — 하드웨어 내부는 공개 자료로 확인 안 함):

- **비용 = SIMD 명령 하나 안에서 같은 주소를 친 lane 수.** interleaved 에서 naddr = k 면 한 simdgroup 의 32 lane 이 k 개 주소로 갈려 주소당 32/k lane. 시간이 32 → 16 → 8 lane 에 비례해 0.208 → 0.107 → 0.056 ms 로 정확히 반씩 줄고, 4 lane 이하(naddr ≥ 8)에서는 다른 바닥(≈0.050 ms)에 닿는다. 같은 주소를 친 lane 들은 줄을 서고(직렬화), 다른 주소는 함께 처리된다.
- **blocked 는 주소를 늘려도 안 빨라진다**: 주소 = gid / (n/naddr) 라 한 simdgroup 의 32 lane 이 항상 같은 주소다. 서로 다른 simdgroup 이 다른 주소를 쳐도 20 Gop/s 그대로 → 병목은 "simdgroup 사이의 경합" 이 아니라 "한 명령 안의 같은 주소 lane 직렬화" 다. 히스토그램의 편향 입력(GPGPU-M03)이 느린 것도 이 그림이다.
- **128 B 간격이 오히려 느리다 (1024 주소: 26.7 vs 84.6)**: CPU 직관(false sharing → 띄워라, ARCH-M08)과 반대다. GPU 의 device atomic 은 코어 간에 캐시 라인을 주고받는 게 아니라 공유 캐시 쪽에서 처리되는 것으로 보이고(가설), 4 B 간격이면 한 명령의 32 주소가 128 B 한 줄에 들어가 요청이 적다. 128 B 간격이면 32 lane 이 32 개 줄을 따로 요청한다. 간격 128 B 에서 naddr 4 / 16 이 들쭉날쭉한 원인은 확인하지 못했다(캐시 뱅크·슬라이스 매핑 추정).
- **simd 집계**: `simd_sum` 으로 32 개를 레지스터에서 먼저 더하고 lane 하나만 atomic → 주소 1 개에서도 96 Gop/s(논리) = plain 1 주소의 4.8배. 연습 4 에서 정확성을 CHECK 한다(atomic 횟수 130854 → 32307).

### Metal 메모리 모델 단어 정리

| 단어 | 뜻 (MSL 4.1 스펙 절) | 이 맥에서 |
|---|---|---|
| `memory_order_relaxed` | 원자성 + 수정 순서만, 다른 메모리 순서 없음 (6.16.1.1) | MSL 4.0 의 device/threadgroup atomic 은 이것만 컴파일됨 |
| `atomic_thread_fence(flags, order, scope)` | 비원자·relaxed 접근의 순서를 scope 안에서 맞춤 (6.16.3, MSL 3.2+, Apple silicon) | `seq_cst` + `thread_scope_device` 컴파일·동작 확인 (연습 5) |
| `thread_scope_{thread,simdgroup,threadgroup,device}` | 이 연산이 동기화할 수 있는 스레드 집합 (6.16.2) | |
| `coherent(device)` | device 메모리를 device 범위로 보이게 (기본은 threadgroup 범위) (4.8) | 연습 5 에서 빼도 200/200 맞음 — 보장은 아님 |
| `mem_flags::mem_threadgroup` / `mem_device` / `mem_none` | barrier 가 순서를 맞출 메모리 (Table 6.13) | `mem_none` 은 실행만 맞춤 |
| `threadgroup_barrier` / `simdgroup_barrier` | 그룹 / simdgroup 전원 도착 + 메모리 순서 (6.10.1) | 조건문 안 barrier 도 컴파일은 됨 |
| grid barrier | 없음. dispatch 를 나누거나(연습 2) 마지막 그룹 패턴(연습 5) | 같은 encoder 의 다음 dispatch 는 앞의 쓰기를 봄 (demo C) |

### `MTL_SHADER_VALIDATION=1` — 무엇을 잡나 (실행 결과)

- `MTL_SHADER_VALIDATION=1` 만 주면 "Metal GPU Validation Enabled" 한 줄만 찍히고, threadgroup 메모리 밖에 쓰는 `rev_oob` 도 **아무 보고 없이** status = Completed 였다. `MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1` 을 같이 줘야 stderr 로 나왔다 (`MTL_DEBUG_LAYER=1` 은 API 검증이고 이 보고와 별개였다):

```text
$ MTL_SHADER_VALIDATION=1 MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1 /tmp/GPGPU-B09_work/demo_race oob
2026-11-08 16:25:26.844 demo_race[18134:6909398] Metal GPU Validation Enabled
=================================================================
Invalid Metal usage: Invalid store in 'threadgroup' address space at offset 1028, executing compute function: "rev_oob"
...
GPU stack trace:
	* frame #0: rev_oob() - /program_source:68:17
```

- 같은 설정으로 `demo_race` 전체(barrier 없는 커널, plain 카운터 포함)를 돌리면 **보고 0 건** — 레이스·barrier 누락은 잡지 않는다. starter/solution/practice 도 보고 0 건.
- 켜면 코드가 계측돼 타이밍이 바뀐다: plain 카운터가 1 → 326–537 로 달라졌다. 검증 도구는 "범위 밖 접근, 잘못된 버퍼" 용이고, 동기화 정확성은 **결정적 테스트 + 일부러 순서를 흔드는 스트레스(skew)** 로 본다.

### 펌웨어·실리콘 검증 연결

- 가속기(GPU/NPU) atomic 유닛 검증은 바로 Part C 의 CHECK 모양이다: 주소 수·배치(같은 줄 / 다른 줄 / 같은 lane 묶음)를 바꿔 가며 **정확한 합**을 골든 모델과 비교하고, 처리량은 별도로 기록한다. 같은 주소 직렬화 경로(blocked)와 병렬 경로(interleaved)는 하드웨어에서 다른 로직이라 둘 다 테스트한다.
- barrier 하드웨어는 demo B 의 skew 처럼 **일부러 늦는 그룹**을 만들어야 검증된다. 정상 스케줄에서는 버그가 숨는다(T = 32 처럼).
- CPU↔가속기 공유 버퍼: Apple 통합 메모리의 shared storage 는 커널 완료(`waitUntilCompleted`) 뒤 CPU 가 바로 읽는다. 일반 SoC 에서 non-coherent 가속기/DMA 라면 완료 인터럽트 뒤 캐시 invalidate 가 필요하다 — ARCH-M12 (병렬 작성 중).

## 6. 정답 코드

<!-- code: solution.m diff=starter.m -->

<!-- code: solution.metal diff=starter.metal -->

```bash
clang -fobjc-arc -O2 -Wall -Wextra -Werror solution.m -framework Metal -framework Foundation -o /tmp/GPGPU-B09_work/solution
/tmp/GPGPU-B09_work/solution         # 13/13 passed, exit 0 (6 회 연속 실행 모두 같음)
```

```text
device: Apple M6
INFO  plain c[0]++ : 1 / 1048576  (lost 1048575 — 관찰값, CHECK 아님)
PASS  count_on_gpu("count_atomic", N) == N
...
PASS  tg_reverse_mismatches(1000, 256) == 0
PASS  tg_reverse_mismatches(77, 64) == 0
...
PASS  spread_ok(M + 7, 3)
13/13 passed
```

## 7. 자주 하는 실수

1. **`c[0]++` 에 `volatile` 이나 `coherent(device)` 를 붙여 고치려 함** → 가시성만 바뀌고 load–add–store 는 여전히 셋으로 나뉜다. 실측 2^20 중 61–169 만 남음. 고침: `atomic_fetch_add_explicit(c, 1u, memory_order_relaxed)` (버퍼 타입 `device atomic_uint *`).
2. **barrier 를 `if` 안에 둠** — `if (lid < m) { threadgroup_barrier(...); out[gid] = ...; }` → 일부 스레드만 barrier 를 만난다(스펙 위반, GPU 에 따라 멈춤). 컴파일러는 오류를 내지 않았다. 고침: barrier 는 if 밖, 쓰기만 if 안.
3. **범위 밖 스레드가 barrier 전에 `return`** → 2 와 같은 문제. 고침: 범위 밖 스레드도 `tg[lid] = 0` 을 쓰고 barrier 를 지나간다.
4. **`simdgroup_barrier` 로 threadgroup 을 맞추려 함** → T = 64 에서 22–25% 틀림(실측). 고침: 그룹 안 다른 simdgroup 의 칸을 읽으면 `threadgroup_barrier(mem_flags::mem_threadgroup)`.
5. **T = 32 로만 테스트하고 "barrier 필요 없음" 결론** → 그룹 = simdgroup 하나라 우연히 맞는다. 고침: T 를 64/256/1024 로, 그리고 일부 simdgroup 을 늦추는 스트레스로 테스트.
6. **커널 안에서 grid 전체를 기다리려고 atomic 스핀** (`while (atomic_load(done) < ngroups) {}`) → 모든 threadgroup 이 동시에 상주한다는 보장이 없어 아직 시작 못 한 그룹을 기다리며 영원히 돈다. 고침: dispatch 를 나누거나(연습 2), 기다리지 않는 "마지막 그룹이 마무리" 패턴(연습 5).
7. **atomic 칸 예약 결과를 순서까지 비교** (스트림 압축) → 순서는 실행마다 다르다. 고침: 개수 + 정렬 후 비교(연습 3), 순서가 필요하면 scan 기반 압축.
8. **경합을 줄이려고 카운터를 128 B 씩 띄움** (CPU 습관) → GPU 에서는 1024 주소에서 오히려 3.2배 느림(26.7 vs 84.6). 고침: 먼저 같은 주소 lane 을 줄인다(주소 분산, `simd_sum` 집계, threadgroup privatization — GPGPU-M03), 간격은 재 보고 정한다.

## 8. 면접 Q&A

<details>
<summary>Q1. GPU 에서 모든 스레드가 c[0]++ 를 하면 결과가 왜 거의 1 인가요? CPU 와 뭐가 다르죠?</summary>

simdgroup(warp) 32 lane 이 load 명령을 함께 실행해 모두 같은 옛 값을 읽고, store 를 함께 해서 32 개 증가가 1 개가 된다. 게다가 한 dispatch 안에서 device 메모리의 일반 store 는 다른 threadgroup 에 보일 의무가 없다(MSL 4.8: 기본 threadgroup coherence) — 실측 2^20 스레드에서 5 회 모두 1. CPU 의 OS-B04 레이스는 두 스레드가 드물게 겹칠 때만 잃지만, GPU 는 겹치는 게 기본이다. 꼬리: "volatile 이면?" → 61–169. 원자성이 없으니 고쳐지지 않는다. "그럼 dispatch 를 10 번 나누면?" → 10. dispatch 경계에서는 앞의 쓰기가 보인다.
</details>

<details>
<summary>Q2. threadgroup_barrier 는 정확히 무엇을 보장하나요? simdgroup_barrier 와 mem_flags 는?</summary>

두 가지: (1) 실행 — 그룹의 모든 스레드가 도착할 때까지 아무도 지나가지 못함, (2) 메모리 — `mem_flags` 로 지정한 주소 공간(`mem_threadgroup`, `mem_device`, `mem_texture`)의 앞선 쓰기가 barrier 뒤의 읽기에 보이도록 순서를 맞춤. `mem_none` 은 실행만. `simdgroup_barrier` 는 같은 일을 simdgroup 32 lane 범위에서만 한다 — 그룹 안 다른 simdgroup 과는 아무것도 안 맞춘다(실측 T=64 에서 22–25% 틀림). CUDA 대응은 `__syncthreads()` / `__syncwarp()`. 꼬리: "barrier 를 조건문 안에?" → 모든 스레드가 같은 조건이어야 한다. 스레드마다 다르면 위반이고 멈출 수 있다. 컴파일러는 막지 않았다(실측).
</details>

<details>
<summary>Q3. barrier 를 빼도 테스트가 통과했어요. 그럼 필요 없는 거 아닌가요?</summary>

아니다. T = 32(그룹 = simdgroup 하나)에서는 lockstep 으로 우연히 맞는다(실측 0). T = 64 이상에서 다른 simdgroup 의 칸을 읽으면 약 50%, 일부 simdgroup 을 늦추면 정확히 절반이 틀렸다. "돌려 보니 맞다" 는 그 칩·그 스케줄에서의 관찰일 뿐 — CUDA 는 Volta 이후 warp 안 스레드도 독립 스케줄이라 warp-synchronous 코드가 깨졌다. 검증은 결정적 CHECK(골든 모델 비교) + 순서를 일부러 흔드는 스트레스로 한다. 꼬리: "도구로 못 잡나?" → `MTL_SHADER_VALIDATION` 은 범위 밖 접근만 보고했고 레이스는 0 건(실측). CUDA 는 `compute-sanitizer --tool racecheck` 가 shared memory 레이스를 본다(이 맥에서 실행 안 함).
</details>

<details>
<summary>Q4. 커널 안에서 모든 threadgroup 을 동기화(grid barrier)하려면?</summary>

Metal 커널에는 grid barrier 가 없다. 방법: (1) dispatch 를 나눈다 — 같은 encoder 의 다음 dispatch 는 앞 dispatch 가 끝나고 쓰기가 보인 뒤 시작(연습 2, demo C 의 10 dispatch = 10). (2) 기다리지 않는 패턴 — 각 그룹이 부분합을 쓰고 device fence 후 ticket atomic 을 올려, 마지막 ticket 을 받은 그룹이 마무리(연습 5, CUDA 샘플의 threadFenceReduction 과 같은 구조). 스핀으로 기다리면 아직 상주 못 한 그룹을 기다려 멈출 수 있다. CUDA 는 cooperative launch 로 상주를 보장할 때만 `grid.sync()` 를 허용한다. 꼬리: "연습 5 에서 fence 를 빼면?" → 실측 200 회 중 191 회 틀린 합(N=2^20, T=256). `coherent(device)` 만 뺐을 땐 0/200 이었지만 스펙상 보장이 아니니 둔다.
</details>

<details>
<summary>Q5. Metal atomic 은 왜 relaxed 만 되나요? 그럼 생산자-소비자 패턴은 어떻게?</summary>

MSL 4.0 까지 device/threadgroup atomic 함수의 order 는 relaxed 만(6.16.4.5, 이 맥에서 acquire/seq_cst 는 컴파일 오류). GPU 에서 흔한 용도 — 카운터, 칸 예약, 히스토그램 — 는 원자성만 필요하다. 순서가 필요하면 MSL 3.2+ 의 `atomic_thread_fence(mem_flags::mem_device, memory_order_seq_cst, thread_scope_device)` 를 relaxed atomic 앞뒤에 두고(release 쪽: 데이터 쓰기 → fence → flag atomic, acquire 쪽: flag atomic → fence → 데이터 읽기), 다른 threadgroup 이 읽을 버퍼는 `coherent(device)`. 이건 C11 의 fence-fence 동기화(MCPP-M13)와 같은 모양이다. MSL 4.1 은 atomic·barrier 에 acquire/release 를 추가했다(스펙 1.3) — 이 맥에서 원시 버전 값으로 컴파일만 확인했다. 꼬리: "CUDA 는?" → `atomicAdd` 도 relaxed 이고 순서는 `__threadfence()` 나 `cuda::atomic_ref` 의 memory_order 로.
</details>

<details>
<summary>Q6. atomic 경합이 처리량을 얼마나 떨어뜨리고, 어떻게 줄이나요?</summary>

실측(2^22 add): 주소 1 개 20 Gop/s, 8 개 이상 85 Gop/s(×4.2). 시간이 "한 SIMD 명령 안에서 같은 주소를 친 lane 수"(32→16→8)에 비례했고, simdgroup 전체가 한 주소를 치는 blocked 배치는 주소 1024 개에서도 20 그대로였다. 줄이는 법: (1) 주소를 나눠 같은 주소 lane 을 줄인다, (2) `simd_sum` 으로 레지스터에서 먼저 모아 simdgroup 당 atomic 1 번(실측 96 Gop/s 논리, 횟수 130854 → 32307), (3) threadgroup 지역 카운터/히스토그램 후 병합(privatization — GPGPU-M03), (4) 결과를 스레드별 칸에 쓰고 reduction. 꼬리: "CPU 처럼 128 B 띄우면?" → 1024 주소에서 3.2배 느려졌다. GPU atomic 은 라인 핑퐁이 아니라 공유 캐시 쪽 처리로 보인다(가설).
</details>

<details>
<summary>Q7. 가속기 실리콘 검증에서 atomic·barrier 를 어떻게 테스트하겠어요?</summary>

(1) 정확성: 주소 수(1..N), 배치(같은 lane 묶음·같은 캐시 줄·다른 줄·뱅크 충돌), 연산 종류(add/min/max/xchg/CAS)를 바꿔 가며 최종값을 골든 모델과 정확히 비교 — Part C 의 CHECK 와 같은 모양. CAS 는 "칸 예약" 처럼 결과 집합이 유일한 테스트로(연습 3). (2) barrier: 일부 simdgroup/웨이브를 일부러 늦추는 skew, 그룹 크기 경계(T=32/64/최대), 부분 그룹, 반복 루프 안 barrier(연습 1). (3) 메모리 순서: 마지막 그룹 패턴처럼 fence 가 빠지면 깨지는 테스트를 넣고 fence 를 뺀 mutant 가 실제로 FAIL 하는지 확인(연습 5 실측 191/200). (4) 처리량은 별도 리포트(경합 곡선)로 회귀 감시. 꼬리: "통과율 100% 면 끝?" → 관찰되지 않은 재배치는 금지의 증거가 아니다. 모델(스펙) 기준 테스트와 mutant 로 테스트 자체의 감도를 확인한다.
</details>

## 9. 추가 연습

`practice.metal` 커널 + `practice.m` 호스트. 모두 결정적 CHECK.

### 연습 1 — threadgroup 트리 합 (루프 안 barrier)

그룹마다 `tg[]` 를 반씩 접어(s = T/2, T/4, …, 1) `tg[0]` 에 합을 만든다. 마지막 그룹이 덜 차도(n = 1000, T = 64) 맞아야 한다.

<details>
<summary>답</summary>

```c
static void tree_sum(threadgroup uint *tg, uint lid, uint T) {
    for (uint s = T / 2; s > 0; s >>= 1) {
        if (lid < s) tg[lid] += tg[lid + s];
        threadgroup_barrier(mem_flags::mem_threadgroup);   // if 밖: 매 반복 전원이 만난다
    }
}
```

반복 횟수는 모든 스레드에서 같다(T 는 그룹 공통) → 루프 안 barrier 가 균일하다. 범위 밖 스레드는 0 을 넣어 합에 영향이 없다. 각 반복 끝 barrier 가 없으면 다음 반복이 아직 안 더해진 칸을 읽는다.
</details>

### 연습 2 — dispatch 두 번으로 전체 합 (grid barrier 대신)

연습 1 의 부분합 → 두 번째 커널(threadgroup 하나, 1024 스레드)이 합친다. 둘을 **한 compute encoder** 에 넣고 CPU 는 마지막에만 읽는다.

<details>
<summary>답</summary>

```c
enc(e, "tg_reduce", @[in, part], k1, 1, G, T, T * 4u);
enc(e, "sum_partials", @[part, res], k2, 1, 1, 1024, 1024 * 4u);
```

기본 serial dispatch encoder 에서는 두 번째 dispatch 가 첫 번째의 쓰기를 본다(demo C 의 10 dispatch = 10 과 같은 근거). `MTLDispatchTypeConcurrent` encoder 라면 `memoryBarrierWithScope:` 가 필요하다(문서 기준, 이 레슨에서 실행 안 함). `sum_partials` 는 grid-stride 루프 `for (i = lid; i < ngroups; i += T)` 로 부분합 수가 1024 보다 많아도 된다.
</details>

### 연습 3 — atomic 으로 출력 칸 예약 (스트림 압축)

`in[i] % 3 == 0` 인 원소만 `out[]` 에 모은다. `slot = atomic_fetch_add(cnt, 1)` 의 옛 값이 내 칸 번호.

<details>
<summary>답</summary>

```c
if (gid < n && in[gid] % 3u == 0u) {
    uint slot = atomic_fetch_add_explicit(cnt, 1u, memory_order_relaxed);
    out[slot] = in[gid];
}
```

옛 값이 스레드마다 달라 칸이 겹치지 않는다(fetch_add 의 수정 순서가 하나라서). 순서는 실행마다 다르니 호스트는 개수 비교 + 정렬 후 `memcmp`. 입력 값을 서로 다르게 만들어 정렬 비교가 정확하다.
</details>

### 연습 4 — `simd_sum` 집계 카운터 (+ lane 배치 확인)

`(in[i] & 7) == 0` 인 원소 수를 센다. simdgroup 안에서 `simd_sum` 으로 합친 뒤 `simd_is_first()` lane 만, 합이 0 이 아닐 때 atomic 1 번. 별도 카운터로 atomic 횟수를 세어 CPU 의 "참이 하나라도 있는 32 개 묶음 수" 와 비교한다. 이 비교는 simdgroup = 연속한 32 스레드라는 가정이 필요하니 `lane_map` 커널로 먼저 확인한다.

<details>
<summary>답</summary>

```c
uint s = simd_sum(mine);
if (simd_is_first() && s != 0u) {
    atomic_fetch_add_explicit(&cnt[0], s, memory_order_relaxed);
    atomic_fetch_add_explicit(&cnt[1], 1u, memory_order_relaxed);
}
```

실측: 개수 130854, atomic 32307 번 (스레드마다 하면 130854 번 → 4.05배 적음). `lane_map`: `thread_position_in_threadgroup == simdgroup_index * 32 + thread_index_in_simdgroup` 이 T = 256 / 1024 에서 성립했다(이 GPU 실측. 스펙 4.4.1 은 매핑이 dispatch 동안 불변이라고만 한다).
</details>

### 연습 5 — 한 dispatch 단일 패스 합 (마지막 그룹이 마무리)

그룹마다 부분합을 `partial[g]` 에 쓰고, lane 0 이 device fence → `ticket` fetch_add. 옛 값이 `ngroups - 1` 이면 그 그룹이 마지막 → acquire fence 후 전원이 `partial[]` 을 합친다. 5 회 실행 모두 CPU 합과 같아야 한다.

<details>
<summary>답</summary>

```c
if (lid == 0) {
    partial[g] = tg[0];
    atomic_thread_fence(mem_flags::mem_device, memory_order_seq_cst, thread_scope_device);   // release
    uint t = atomic_fetch_add_explicit(ticket, 1u, memory_order_relaxed);
    is_last = (t == ngroups - 1u) ? 1u : 0u;
    if (is_last) atomic_thread_fence(mem_flags::mem_device, memory_order_seq_cst, thread_scope_device); // acquire
}
threadgroup_barrier(mem_flags::mem_threadgroup | mem_flags::mem_device);
if (is_last) { /* grid-stride 로 partial[] 합 → tree_sum → result[0] */ }
```

`is_last` 는 threadgroup 변수라 barrier 뒤 그룹 전원이 같은 값을 본다 → `if (is_last)` 안의 barrier 도 균일하다. 실측 (각 200 회): 정답 0/200 틀림, `coherent(device)` 만 뺌 0/200, **fence 두 개 + barrier 의 `mem_device` 를 뺌 191/200 틀림** (N = 2^20, T = 256), 126/200 (n = 77777, T = 1024). GPU 에서도 순서는 저절로 지켜지지 않는다 — C11 fence-fence 동기화(MCPP-M13)와 같은 구조다. barrier 가 lane 0 의 device-범위 fence 효과를 그룹의 다른 스레드에게 넘겨 주는지(범위가 섞인 happens-before)는 스펙 문장으로 확인하지 못해, barrier 에 `mem_device` 를 같이 줬다.
</details>

### 한 번에 검증

<!-- code: practice.m -->

<!-- code: practice.metal -->

```bash
clang -fobjc-arc -O2 -Wall -Wextra -Werror practice.m -framework Metal -framework Foundation -o /tmp/GPGPU-B09_work/practice
/tmp/GPGPU-B09_work/practice                                    # 12/12 passed, exit 0
clang -fobjc-arc -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror \
      practice.m -framework Metal -framework Foundation -o /tmp/GPGPU-B09_work/practice_san
/tmp/GPGPU-B09_work/practice_san                                # 12/12, 보고 없음
MTL_SHADER_VALIDATION=1 MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1 /tmp/GPGPU-B09_work/practice   # 12/12, Invalid 0 건
```

새니타이저는 호스트(Objective-C) 코드만 계측한다 — GPU 커널은 대상이 아니다. 그래서 크기를 줄일 필요가 없었다(전체 크기로 1 초 안). 더 줄이고 싶으면 `B09_SMALL=1` (n = 2^16).

데모 (CHECK 없음, 레슨 폴더에서):

<!-- code: demo_race.metal -->

<!-- code: demo_contention.metal -->

```bash
clang -fobjc-arc -O2 -Wall -Wextra -Werror demo_race.m -framework Metal -framework Foundation -o /tmp/GPGPU-B09_work/demo_race
/tmp/GPGPU-B09_work/demo_race          # A 카운터 4종, B barrier 변형, C 10 dispatch (1 초 안)
/tmp/GPGPU-B09_work/demo_race oob      # 셰이더 검증 시험용 (위의 환경 변수와 함께)
clang -fobjc-arc -O2 -Wall -Wextra -Werror demo_contention.m -framework Metal -framework Foundation -o /tmp/GPGPU-B09_work/demo_contention
/tmp/GPGPU-B09_work/demo_contention    # 경합 sweep (1 초 안)
```

## 10. 핵심 정리

- GPU 의 plain `c[0]++` 는 2^20 번 해도 **1** (SIMD lockstep + 한 dispatch 안 device store 는 다른 threadgroup 에 보일 의무 없음). `volatile`/`coherent(device)` 로도 61–169. 고침은 `atomic_fetch_add_explicit(..., memory_order_relaxed)` 하나.
- MSL 4.0 device atomic = relaxed 만(원자성 + 수정 순서). 순서는 `atomic_thread_fence(mem_device, seq_cst, thread_scope_device)` + `coherent(device)`. MSL 4.1 이 acquire/release 추가.
- `threadgroup_barrier(mem_threadgroup)` = 전원 도착 + threadgroup 메모리 쓰기 보이기. 모든 스레드가 만나야 하니 **if 밖**, 범위 밖 스레드도 return 하지 않는다.
- barrier 를 빼면 T = 32 는 0, T ≥ 64 는 약 50% 틀림(늦추면 정확히 절반). `simdgroup_barrier` 는 그룹을 못 맞춘다. "통과했다" ≠ 맞다 — skew 스트레스로 테스트.
- 커널 안 grid barrier 없음 → dispatch 를 나누거나(다음 dispatch 는 앞 쓰기를 본다) 마지막 그룹 패턴(fence 빼면 191/200 틀림).
- atomic 비용 = 한 SIMD 명령 안 **같은 주소 lane 수**: 1 주소 20 → 8+ 주소 85 Gop/s, blocked 는 20 고정, `simd_sum` 집계 96. CPU 처럼 128 B 띄우면 1024 주소에서 3.2배 느림.
- `MTL_SHADER_VALIDATION=1` (+ `_REPORT_TO_STDERR=1`) 은 범위 밖 접근을 잡고 레이스는 못 잡는다.

## 참고자료

- Apple, *Metal Shading Language Specification* Version 4.1 (2026-06-04, `developer.apple.com/metal/Metal-Shading-Language-Specification.pdf` 에서 받아 확인): 1.3 New in Metal 4.1, 4.4.1 SIMD-Groups and Quad-Groups, 4.8 Memory Coherency (`coherent(device)`, 기본 threadgroup coherence), 5.2.3.6 Kernel Function Input Attributes (independent forward progress, `threads_per_threadgroup`), 6.10.1 Threadgroup and SIMD-Group Synchronization Functions (Table 6.12, Table 6.13 mem_flags), 6.10.2 SIMD-Group Functions (`simd_sum`, `simd_is_first`), 6.16.1 Memory Order, 6.16.2 Thread Scope, 6.16.3 Fence Functions, 6.16.4.5 Atomic Fetch and Modify Functions
- Apple Metal 문서 — `MTLDevice newLibraryWithSource:options:error:`, `MTLComputeCommandEncoder setThreadgroupMemoryLength:atIndex:`, `MTLCommandBuffer GPUStartTime/GPUEndTime`, `MTLDispatchType`, `memoryBarrierWithScope:`: <https://developer.apple.com/documentation/metal>
- Apple, "Validating your app's Metal shader usage" (셰이더 검증, 환경 변수 `MTL_SHADER_VALIDATION`): <https://developer.apple.com/documentation/xcode/validating-your-apps-metal-shader-usage> (URL·문서 내용 확인 필요 — 이 레슨은 실행 결과만 근거로 씀). `MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1` 이 필요하다는 것은 이 맥에서 실행으로 확인
- NVIDIA, *CUDA C++ Programming Guide* — Synchronization Functions (`__syncthreads`, `__syncwarp`), Memory Fence Functions (`__threadfence`, 마지막 블록 합 예제), Atomic Functions (relaxed), Independent Thread Scheduling (Volta 이후 warp-synchronous 코드 주의), Cooperative Groups grid sync: <https://docs.nvidia.com/cuda/cuda-c-programming-guide/> (절 번호는 판마다 달라 확인 필요)
- NVIDIA Compute Sanitizer — racecheck tool: <https://docs.nvidia.com/compute-sanitizer/ComputeSanitizer/index.html> (이 맥에서 실행 안 함)
- Hwu, Kirk, El Hajj, *Programming Massively Parallel Processors* 4th ed. — barrier 동기화·atomic·histogram privatization 장
- 관련 레슨: OS-B04 (CPU counter 레이스), OS-B05 (mutex/atomic 수정), C-M14 (ISR 공유 데이터), C-A03 (C11 atomic), MCPP-M13 (fence-fence 동기화), LLAT-B06 (CPU atomic 비용·경합), OS-M07 (스핀락과 일관성 트래픽), ARCH-M08 (false sharing, 병렬 작성), ARCH-M12 (non-coherent DMA 캐시 유지보수, 병렬 작성), GPGPU-M03 (histogram privatization, 병렬 작성), GPGPU-B01 (SIMT 시뮬레이터), GPGPU-B04 (divergence)
