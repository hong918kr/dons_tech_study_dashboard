---
id: OS-B03
time: 65분
updated: 2026-11-11
---
# OS-B03 시스템 콜 비용 측정: 사용자↔커널 경계

## 학습 목표

- 빈 함수 호출, `getpid`, `read(fd, buf, 0)`, `clock_gettime`, `getppid`, `write(/dev/null, 1)` 의 호출당 비용을 **워밍업 + 여러 run 의 median + 루프 오버헤드 빼기** 로 재고, 컴파일러가 측정 대상을 지우지 못하게 막는다.
- 결과가 왜 세 무리(≈0.4 ns / 4~16 ns / 100 ns~2 µs)로 갈리는지 설명한다: 함수 호출, **commpage·vDSO** 에서 시계 읽기, `svc` 로 커널 진입(모드 전환) + 커널 경로 길이.
- "이 호출은 정말 커널에 들어가나?" 를 시간으로 추측하지 않고 **센다**: macOS 는 `task_info(TASK_EVENTS_INFO)` 의 `syscalls_unix` 카운터, 리눅스는 `strace -c`. 그리고 libSystem 기계어(`svc #0x80`, `mrs CNTVCT_EL0`)를 lldb 로 직접 본다.
- macOS 의 `getpid` 는 libc 가 pid 를 **캐시** 해서 시스템 콜이 아니라는 것을 카운터·기계어·시간 세 가지로 확인하고, fork 뒤 자식에서 캐시가 비워지는 것까지 본다.
- 시스템 콜 고정비를 펌웨어 예산으로 바꾼다: 타이머 눈금에 맞는 반복 수, 센서 읽기 배치 크기(CPU 예산 vs 지연 한도).

## 1. 문제

"시스템 콜 하나에 얼마나 걸리죠? `gettimeofday` 는요?" — 임베디드 리눅스·플랫폼 면접의 단골 질문이고, 실무에서는 "센서 샘플마다 `read()` 해도 되나, 모아서 읽어야 하나", "로그에 타임스탬프를 찍는 비용이 얼마인가" 를 정하는 숫자다. MCU 에도 같은 경계가 있다: FreeRTOS-MPU 에서 비특권 태스크가 커널 API 를 부르면 `SVC` 예외로 들어간다 (SEC-M08, ARM-M09). 이 레슨은 OSTEP 6장(*Limited Direct Execution*) 의 측정 과제 "0 바이트 `read` 를 반복해 시스템 콜 비용을 재라" 를 이 맥(Apple M6, macOS 27.2)과 리눅스 VM 에서 실제로 한다.

**시스템 콜(system call)** 은 "평범한 함수 호출처럼 보이지만 안에 **트랩(trap) 명령** 이 숨어 있는 호출" 이다 (OSTEP 6.2). arm64 의 트랩 명령은 `svc`(supervisor call) — CPU 가 사용자 모드(EL0)에서 커널 모드(EL1)로 올라가 커널이 미리 등록해 둔 예외 벡터로 뛰고, 커널이 일을 마치면 `eret` 로 돌아온다. 반대로 시계 읽기는 커널이 모든 프로세스에 **읽기 전용으로 매핑해 둔 페이지** (macOS 는 commpage, 리눅스는 vDSO)와 사용자 모드에서 읽을 수 있는 하드웨어 카운터(`CNTVCT_EL0`)만으로 끝나서 커널에 들어가지 않는다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">세 가지 길: 함수 호출 · 사용자 공간 시계 · 시스템 콜 (이 맥에서 lldb 로 본 기계어)</text>
  <line x1="10" y1="200" x2="730" y2="200" stroke="currentColor" stroke-dasharray="6 4"/>
  <text x="20" y="193" fill="currentColor" font-size="12">EL0 (사용자 모드)</text>
  <text x="20" y="216" fill="currentColor" font-size="12">EL1 (커널 모드)</text>
  <rect x="20" y="40" width="190" height="50" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="30" y="60" fill="currentColor">① empty_fn()</text>
  <text x="30" y="78" fill="currentColor" font-family="monospace" font-size="12">bl _empty_fn → ret</text>
  <text x="30" y="110" fill="currentColor" font-size="12">≈ 0.4 ns (1~2 사이클)</text>
  <rect x="240" y="40" width="230" height="110" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="250" y="60" fill="currentColor">② mach_absolute_time()</text>
  <text x="250" y="80" fill="currentColor" font-family="monospace" font-size="12">ldr x1,[commpage]  ; 오프셋</text>
  <text x="250" y="97" fill="currentColor" font-family="monospace" font-size="12">mrs x0, CNTVCT_EL0 ; 카운터</text>
  <text x="250" y="114" fill="currentColor" font-family="monospace" font-size="12">ldr x2,[commpage]  ; 그새 바뀜?</text>
  <text x="250" y="131" fill="currentColor" font-family="monospace" font-size="12">cmp x1,x2 / b.ne 다시</text>
  <text x="250" y="170" fill="currentColor" font-size="12">≈ 4 ns, clock_gettime 은 7~16 ns</text>
  <rect x="250" y="230" width="210" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="260" y="252" fill="currentColor" font-size="12">commpage (커널이 써 둔 페이지)</text>
  <path d="M450 150 L450 228" stroke="currentColor" fill="none" stroke-dasharray="3 3"/>
  <text x="330" y="222" fill="currentColor" font-size="12">읽기만 (트랩 없음)</text>
  <rect x="500" y="40" width="225" height="70" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="510" y="60" fill="currentColor">③ getppid()</text>
  <text x="510" y="80" fill="currentColor" font-family="monospace" font-size="12">mov x16, #39   ; 번호</text>
  <text x="510" y="97" fill="currentColor" font-family="monospace" font-size="12">svc #0x80      ; 트랩</text>
  <path d="M560 110 L560 236" stroke="currentColor" fill="none"/>
  <path d="M555 228 L560 238 L565 228" stroke="currentColor" fill="none"/>
  <rect x="490" y="240" width="240" height="78" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="500" y="258" fill="currentColor" font-size="12">예외 벡터 → 레지스터 저장</text>
  <text x="500" y="275" fill="currentColor" font-size="12">→ 번호로 디스패치 → 핸들러</text>
  <text x="500" y="292" fill="currentColor" font-size="12">→ 복원 → eret</text>
  <text x="500" y="310" fill="currentColor" font-size="12">≈ 100 ns ~ 1.5 µs (콜마다 다름)</text>
  <path d="M700 240 L700 112" stroke="currentColor" fill="none"/>
  <path d="M695 120 L700 110 L705 120" stroke="currentColor" fill="none"/>
  <text x="20" y="300" fill="currentColor" font-size="12">비용 = 모드 전환(바닥값) + 커널 안에서 걷는 경로</text>
  <text x="20" y="318" fill="currentColor" font-size="12">+ 간접 비용(캐시·분기 예측 오염, demo_pollute.c)</text>
</svg>
```

### Part A — 측정 통계 도구 (`solution.c`, 고정 입력으로 CHECK)

시간 측정 자체는 매번 값이 달라 CHECK 할 수 없으므로, **측정값을 다루는 계산** 을 정확하게 만든다.

- `ticks_to_ns(ticks, numer, denom)` — `mach_absolute_time` 의 tick 을 ns 로. Apple Silicon 은 `125/3`(24 MHz, 1 tick = 41.67 ns). `ticks × 125` 가 64 비트를 넘어도 정확히 `floor(ticks·numer/denom)`.
- `median(a, n)` (제자리 정렬, 짝수면 가운데 둘의 평균), `percentile_nr(s, n, p)` (nearest-rank).
- `net_ns_per_call(batch_ns, n, loop_ns)` — 배치 시간 / n − 빈 루프 1 회 비용, 음수는 0.
- `summarize(batch_ns, runs, n, loop_ns)` → min / median / p90 / max. 인터럽트에 얻어맞은 run 하나가 median 을 흔들지 않는 것을 확인.

### Part B — 실측과 "정말 커널에 들어갔나?" 세기

- `demo_bench.c` — 대상 12 개(리눅스 11 개)를 각각 배치(≈30 ms) × 7 run 으로 재서 호출당 ns 표를 낸다. 빈 함수는 `noinline` + `asm volatile("" ::: "memory")` 로 지워지지 않게.
- `count_syscalls(fn, n, &d)` — `task_info(TASK_EVENTS_INFO)` 의 `syscalls_unix` / `syscalls_mach` 를 호출 전후로 읽어 차이를 낸다 (읽기 함수 `sc_read` 는 하네스 제공). `classify(&d, n)` — 비율로 USER_ONLY / UNIX_TRAP / MACH_TRAP / PARTIAL.
- 확인: `getppid`, `read(0B)`, `write`, `clock_gettime(CLOCK_THREAD_CPUTIME_ID)` 는 **정확히 n 번** 진입, `getpid`·`clock_gettime(MONOTONIC/REALTIME)`·`mach_absolute_time` 은 **0 번**.
- `demo_count.c` — 같은 카운터로 대상 13 개를 10만 번씩 센 표. `demo_disasm.sh` — lldb 로 libSystem 의 `getpid`(캐시 확인 후 `svc`), `getppid`/`read`(`svc #0x80`), `mach_absolute_time`(`mrs CNTVCT_EL0`) 기계어를 본다. `demo_pollute.c` — 시스템 콜이 내 캐시를 밀어내는 간접 비용.
- 리눅스(Lima VM): 같은 `demo_bench.c` + `strace -c` 로 집계. vDSO `clock_gettime` 은 strace 에 **안 보인다**.

### Part C — 펌웨어 예산으로 바꾸기

- `iters_for_resolution(tick_ps, est_ps, err_ppm)` — 41.67 ns 눈금 타이머로 0.25 ns 짜리를 100 ppm 정확도로 재려면 배치당 몇 번?
- 8 kHz IMU 를 임베디드 리눅스에서 받는다. 읽기마다 `poll + read` 2 콜. `syscall_ns_per_sec`, `min_batch_for_budget`(CPU 0.5 % 예산), `max_batch_for_latency`(1 ms 지연 한도), `plan_batch`(둘 다 만족하는 최소 배치, 없으면 0). IIO 버퍼의 watermark 를 정하는 계산이다 (ELX-M09).

## 2. Problem (English)

> **Measure the user↔kernel boundary and prove which calls actually trap.**
>
> 1. Statistics helpers (deterministic, tested with fixed inputs):
>    `uint64_t ticks_to_ns(uint64_t ticks, uint32_t numer, uint32_t denom)` — exact `floor(ticks*numer/denom)` without 64-bit overflow (denom 0 → 0);
>    `double median(double *a, size_t n)` (sorts in place; even n → mean of the middle two; n = 0 → NAN);
>    `double percentile_nr(const double *sorted, size_t n, double p)` — nearest rank `ceil(p/100·n)` clamped to [1, n];
>    `double net_ns_per_call(double batch_ns, uint64_t n, double loop_ns)` — clamp at 0;
>    `stats_t summarize(const double *batch_ns, size_t runs, uint64_t n, double loop_ns)` → {min, median, p90, max}, NAN if runs is 0 or > 64.
> 2. `int count_syscalls(void (*fn)(void), int n, sc_count_t *d)` — the change in this task's BSD-syscall and Mach-trap counters (`task_info(TASK_EVENTS_INFO)`) across `n` calls. `call_kind_t classify(const sc_count_t *d, int n)`: unix ≥ 95 % of n → UNIX_TRAP, else mach ≥ 95 % → MACH_TRAP, else unix+mach ≤ n/100 + 2 → USER_ONLY, else PARTIAL.
>    Expected on macOS: `getppid`, `read(fd,buf,0)`, `write`, `clock_gettime(CLOCK_THREAD_CPUTIME_ID)` trap every time; `getpid`, `clock_gettime(CLOCK_MONOTONIC)`, `mach_absolute_time` never trap.
> 3. Budget helpers (integer math, round up where a minimum is asked): iterations so that one timer tick is ≤ `err_ppm` of a batch; syscall CPU ns per second for a sensor read in batches; the smallest batch meeting a CPU budget; the largest batch meeting a latency bound; the plan that meets both (0 if infeasible).
>
> Example: `ticks_to_ns(24000000, 125, 3) == 1000000000`. 8 kHz, 2 syscalls/read at 1500 ns: batch 1 costs 24,000,000 ns/s (2.4 % CPU); a 0.5 % budget needs batch 5; a 1 ms latency bound allows at most 8.
>
> Follow-ups: Why is `clock_gettime` 10× cheaper than `getppid`? Why can't you measure syscall cost with `getpid` on macOS (or old glibc)? Why does `read` of 0 bytes cost 2.5× `getppid`? What does `strace` do to the numbers? What is the *indirect* cost of a syscall?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력, 0.1 초 안에 끝난다):

```text
FAIL starter.c:184  ticks_to_ns(24000000u, 125, 3) == 1000000000u
FAIL starter.c:185  ticks_to_ns(1, 125, 3) == 41u && ticks_to_ns(3, 125, 3) == 125u
… (Part A·B·C 의 나머지 FAIL 39 줄 생략)
FAIL starter.c:261  plan_batch(8000, 2, 1500, 5000000u, 1000) == 5u
FAIL starter.c:263  plan_batch(4000000u, 2, 1500, 10000000u, 1000) == 1200u
8/51 passed
```

하네스 설계: 시간(ns)은 CHECK 하지 않는다 — 부하·코어·주파수에 따라 흔들린다. 대신 **커널 진입 횟수** 는 매번 같다 (`getppid` 2만 번 → `syscalls_unix` 정확히 +20000). 그래서 "트랩하나?" 는 CHECK 로, "얼마나 걸리나?" 는 demo 로 나눴다. 관찰용 파일:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c   -o bench   && ./bench          # 호출당 ns 표 (약 3 초)
cc -std=c11 -Wall -Wextra -Werror -O2 demo_pollute.c -o pollute && ./pollute        # 간접 비용 (약 2 초)
cc -std=c11 -Wall -Wextra -Werror -O2 demo_count.c   -o count   && ./count          # 대상별 커널 진입 횟수
cc -std=c11 -Wall -Wextra -Werror -O2 demo_paths.c   -o paths   && ./paths          # read/write 경로별 비용
sh demo_disasm.sh                                                                     # libSystem 기계어 (lldb)
```

## 4. 힌트

<details>
<summary>힌트 1 — ticks × numer 오버플로 피하기</summary>

`ticks = q·denom + r` (q = ticks / denom, r = ticks % denom) 로 쪼개면 `ticks·numer/denom = q·numer + r·numer/denom`. 첫 항은 정수이므로 floor 는 두 번째 항에만 걸린다 → 정확히 같은 결과. `r < denom ≤ 2^32`, `numer < 2^32` 이라 `r·numer` 는 넘치지 않는다.

</details>

<details>
<summary>힌트 2 — nearest-rank 와 부동소수점</summary>

`ceil(p / 100.0 * n)` 은 `0.9 * 10` 같은 곳에서 `9.000000000000002` 가 되어 rank 가 10 으로 튈 수 있다. `p * n` 을 먼저 곱하면(정수 × 정수라 정확) `/ 100` 에서 한 번만 반올림된다. 그리고 rank 를 `[1, n]` 으로 잘라야 p = 0 에서 `s[-1]` 을 읽지 않는다.

</details>

<details>
<summary>힌트 3 — summarize 는 Part A 함수 조립</summary>

입력 배열을 바꾸면 안 되니 지역 배열(최대 64)에 `net_ns_per_call` 결과를 담고, `median()` 이 그 배열을 정렬해 주므로 그 뒤에 `v[0]`, `v[runs-1]`, `percentile_nr(v, runs, 90)` 을 읽으면 된다.

</details>

<details>
<summary>힌트 4 — count_syscalls 는 "전 · n 번 · 후"</summary>

`sc_read(&a)` → `for (i < n) fn()` → `sc_read(&b)` → `b − a`. `task_info` 자체가 Mach 메시지(트랩 1 회)라 차이에 mach +1 이 섞인다 — 그래서 classify 의 USER_ONLY 조건에 `+ 2` 여유가 있다. classify 의 95 % 비교는 `unix * 100 >= n * 95` 처럼 정수로 하면 경계에서 흔들리지 않는다.

</details>

<details>
<summary>힌트 5 — "최소 batch" 는 올림 나눗셈 한 번</summary>

비용 = `K / batch` (K = hz × 콜 수 × ns). `K / b ≤ budget` 인 최소 b 는 `ceil(K / budget)`. 지연 한도는 반대로 내림: `floor(hz × µs / 1e6)`. 둘 다 32 비트 입력을 **64 비트로 바꾼 뒤** 곱한다 (`4000000 × 2 × 1500` 은 32 비트를 넘는다).

</details>

## 5. 풀이

### Part A — 측정 방법: 한 번이 아니라 "배치 ÷ N", 그리고 여러 run 의 median

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 번을 재면 눈금(41.67 ns)보다 작다 → N 번 묶어 재고 나눈다</text>
  <text x="10" y="55" fill="currentColor">t0</text>
  <rect x="40" y="40" width="40" height="24" class="f-mut" stroke="currentColor"/>
  <text x="45" y="57" fill="currentColor" font-size="12">워밍업</text>
  <rect x="90" y="40" width="80" height="24" class="f-acc" stroke="currentColor"/>
  <text x="100" y="57" fill="currentColor" font-size="12">run 1</text>
  <rect x="175" y="40" width="80" height="24" class="f-acc" stroke="currentColor"/>
  <text x="185" y="57" fill="currentColor" font-size="12">run 2</text>
  <rect x="260" y="40" width="120" height="24" class="f-warn" stroke="currentColor"/>
  <text x="270" y="57" fill="currentColor" font-size="12">run 3 (인터럽트)</text>
  <rect x="385" y="40" width="80" height="24" class="f-acc" stroke="currentColor"/>
  <text x="395" y="57" fill="currentColor" font-size="12">run 4</text>
  <text x="475" y="57" fill="currentColor">… run 7  → median</text>
  <rect x="90" y="95" width="560" height="30" class="f-acc" stroke="currentColor"/>
  <text x="100" y="115" fill="currentColor" font-family="monospace" font-size="12">for (i = 0; i &lt; N; i++) { acc += getppid(); BARRIER(); }</text>
  <path d="M90 140 L650 140" stroke="currentColor"/>
  <text x="90" y="158" fill="currentColor" font-size="12">t0 = mach_absolute_time()</text>
  <text x="480" y="158" fill="currentColor" font-size="12">t1 = mach_absolute_time()</text>
  <text x="10" y="190" fill="currentColor">ns/call = (t1 − t0)·125/3 / N − loop_ns    (loop_ns = 같은 루프에 호출만 뺀 것, ≈ 0.23 ns)</text>
  <text x="10" y="212" fill="currentColor">배치 ≈ 30 ms → 눈금 오차 ±41.67 ns 는 1.4 ppm. 시계 읽기 2 회(≈ 8 ns)도 무시할 수준</text>
  <text x="10" y="234" fill="currentColor">평균 대신 median: run 3 같은 이상치 하나에 끌려가지 않는다 (summarize 테스트의 230 ns run)</text>
</svg>
```

| 단계 | 왜 | 코드 |
|---|---|---|
| 배치 측정 | `mach_absolute_time` 눈금 41.67 ns > 측정 대상(0.4 ns). 한 번 재면 0 또는 41.67 만 나온다 | N 을 대상마다 자동 결정: 1000 회 시험 → 배치 ≈ 30 ms 가 되게 (`demo_bench.c` `measure`) |
| 워밍업 1 배치 버림 | 첫 배치는 i-cache·TLB·분기 예측이 차갑고, 코어가 E→P 로 옮겨 가는 중일 수 있다 | `(void)run_batch(t, n)` |
| 루프 오버헤드 빼기 | 루프 자체(`add/cmp/b.ne`)도 1 사이클쯤 쓴다. 빈 함수(0.4 ns)는 이걸 안 빼면 2 배로 보인다 | `net_ns_per_call`, 음수는 0 |
| median (+min, max) | 다른 프로세스·인터럽트가 끼면 run 하나가 튄다. 평균은 끌려가고 median 은 안 끌려간다 | `summarize` |
| 컴파일러 막기 | 결과를 안 쓰면 `getpid()` 루프도 지워질 수 있다. 빈 함수는 인라인되면 0 | `noinline` + `asm volatile("" ::: "memory")` + 반환값을 `volatile` 전역에 누적 |

`ticks_to_ns` 의 오버플로: `ticks × 125` 는 `2^64 / 125 ≈ 1.48e17` tick 부터 넘치는데, 24 MHz 로는 `1.48e17 / 24e6 ≈ 6.1e9` 초 = **약 195 년** 이다. 부팅 후 시간을 바로 변환하는 데는 사실상 안전하다. 문제는 같은 "곱한 뒤 나누기" 를 다른 곳에 쓸 때다 — 예: 누적 ns 를 다른 클록 주파수의 tick 으로 바꾸는 `ns × freq_hz / 1e9` 는 24 MHz 에서도 `ns > 7.7e11` (약 13 분)이면 넘친다. 몫·나머지 분해는 "곱하기 전에 나눈다" 를 정확도 손실 없이 하는 표준 기법이다 (리눅스 커널의 `mul_u64_u32_div()` 같은 헬퍼가 이 문제를 푼다). 테스트의 2^58 tick 은 일부러 순진한 곱셈이 넘치는 값을 골랐다.

`percentile_nr` 은 **실제 표본 중 하나** 를 돌려준다 (p90 of 7 run = 7 번째 = 최댓값 229). 선형 보간(numpy 기본)은 154 처럼 표본에 없는 값을 낸다 — 연습 3. 지연 SLO 를 말할 때는 "p99 = 실제 관측된 값" 인 nearest-rank 가 설명하기 쉽다.

### Part B — 실측 결과 (이 맥)

머신: `sysctl -n machdep.cpu.brand_string` = **Apple M6** (Super 2 + Performance 4 + Efficiency 6 코어, P/Super L1D 128/64 KiB), macOS 27.2, `Apple clang version 21.0.0 (clang-2100.0.123.102)`. `./bench` 실제 출력:

```text
timer: mach_absolute_time, timebase 125/3 → 1 tick = 41.67 ns
cold: 1st getppid 333 ns, 2nd 83 ns (단발 측정, 눈금 단위로 반올림됨)
7 runs (+1 warm-up) per target. loop overhead median 0.227 ns/iter (아래 값에서 뺐다)

target                          N/batch       min    median       max   /getppid
empty_fn()                     27692307      0.33      0.40      0.46      0.004
empty via fnptr                42352941      0.34      0.41      0.46      0.004
getpid()                       28799999      0.32      0.66      0.82      0.006
clock_gettime(MONO)             2068965     15.30     15.55     15.97      0.150
clock_gettime(REAL)             2891566     10.48     10.57     10.91      0.102
clock_gettime(MONO_RAW)         2627737     11.25     11.41     11.83      0.110
clock_gettime_nsec_np           3750000      7.20      7.28      7.67      0.070
mach_absolute_time              6728971      3.81      3.86      3.91      0.037
getppid()                        300877    102.29    103.79    105.43      1.000
read(fd,buf,0)                    90486    266.69    270.20    272.41      2.603
write(/dev/null,1)                21698   1508.36   1531.70   1626.08     14.758
clock_gettime(THREAD_CPU)        242505    119.61    120.68    122.20      1.163
```

다섯 번 실행한 median 의 범위: getppid 101~118 ns, read 0B 254~296 ns, write 1.40~1.94 µs, THREAD_CPU 118~150 ns, clock_gettime 10.5~15.6 ns, mach_absolute_time 3.82~4.00 ns, 빈 함수 0.24~0.41 ns. **절대값은 run 마다 10~20 % 흔들리지만 비율(무리 사이 25~250 배)은 그대로다.** 숫자는 비율로 기억한다.

```svg
<svg viewBox="0 0 740 345" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">호출당 ns (median, 로그 눈금, Apple M6 / macOS 27.2) — 세 무리</text>
  <line x1="190" y1="35" x2="190" y2="262" stroke="currentColor"/>
  <line x1="300" y1="35" x2="300" y2="262" stroke="currentColor" stroke-dasharray="2 4"/>
  <line x1="410" y1="35" x2="410" y2="262" stroke="currentColor" stroke-dasharray="2 4"/>
  <line x1="520" y1="35" x2="520" y2="262" stroke="currentColor" stroke-dasharray="2 4"/>
  <line x1="630" y1="35" x2="630" y2="262" stroke="currentColor" stroke-dasharray="2 4"/>
  <text x="178" y="280" fill="currentColor" font-size="12">0.1</text>
  <text x="294" y="280" fill="currentColor" font-size="12">1</text>
  <text x="402" y="280" fill="currentColor" font-size="12">10</text>
  <text x="508" y="280" fill="currentColor" font-size="12">100</text>
  <text x="615" y="280" fill="currentColor" font-size="12">1000 ns</text>
  <text x="10" y="54" fill="currentColor" font-size="12">empty_fn()</text>
  <rect x="190" y="42" width="66" height="16" class="f-ok" stroke="currentColor"/>
  <text x="262" y="55" fill="currentColor" font-size="12">0.40</text>
  <text x="10" y="76" fill="currentColor" font-size="12">getpid() (캐시)</text>
  <rect x="190" y="64" width="90" height="16" class="f-ok" stroke="currentColor"/>
  <text x="286" y="77" fill="currentColor" font-size="12">0.66</text>
  <text x="10" y="98" fill="currentColor" font-size="12">mach_absolute_time</text>
  <rect x="190" y="86" width="175" height="16" class="f-acc" stroke="currentColor"/>
  <text x="371" y="99" fill="currentColor" font-size="12">3.86</text>
  <text x="10" y="120" fill="currentColor" font-size="12">clock_gettime_nsec_np</text>
  <rect x="190" y="108" width="205" height="16" class="f-acc" stroke="currentColor"/>
  <text x="401" y="121" fill="currentColor" font-size="12">7.28</text>
  <text x="10" y="142" fill="currentColor" font-size="12">clock_gettime(REAL)</text>
  <rect x="190" y="130" width="223" height="16" class="f-acc" stroke="currentColor"/>
  <text x="419" y="143" fill="currentColor" font-size="12">10.6</text>
  <text x="10" y="164" fill="currentColor" font-size="12">clock_gettime(MONO)</text>
  <rect x="190" y="152" width="241" height="16" class="f-acc" stroke="currentColor"/>
  <text x="437" y="165" fill="currentColor" font-size="12">15.6</text>
  <text x="10" y="186" fill="currentColor" font-size="12">getppid()</text>
  <rect x="190" y="174" width="332" height="16" class="f-warn" stroke="currentColor"/>
  <text x="528" y="187" fill="currentColor" font-size="12">104</text>
  <text x="10" y="208" fill="currentColor" font-size="12">clock_gettime(THREAD)</text>
  <rect x="190" y="196" width="339" height="16" class="f-warn" stroke="currentColor"/>
  <text x="535" y="209" fill="currentColor" font-size="12">121</text>
  <text x="10" y="230" fill="currentColor" font-size="12">read(fd, buf, 0)</text>
  <rect x="190" y="218" width="377" height="16" class="f-warn" stroke="currentColor"/>
  <text x="573" y="231" fill="currentColor" font-size="12">270</text>
  <text x="10" y="252" fill="currentColor" font-size="12">write(/dev/null, 1)</text>
  <rect x="190" y="240" width="460" height="16" class="f-warn" stroke="currentColor"/>
  <text x="656" y="253" fill="currentColor" font-size="12">1532</text>
  <rect x="10" y="322" width="14" height="12" class="f-ok" stroke="currentColor"/>
  <text x="30" y="333" fill="currentColor" font-size="12">함수 (커널 안 감)</text>
  <rect x="170" y="322" width="14" height="12" class="f-acc" stroke="currentColor"/>
  <text x="190" y="333" fill="currentColor" font-size="12">commpage 시계 (커널 안 감)</text>
  <rect x="390" y="322" width="14" height="12" class="f-warn" stroke="currentColor"/>
  <text x="410" y="333" fill="currentColor" font-size="12">svc 트랩 (매번 커널 진입 — demo_count.c 로 셈)</text>
</svg>
```

**세 무리를 읽는 법:**

1. **≈ 0.4 ns — 함수 호출.** `bl` + `ret` 는 리턴 주소 예측기(return stack)가 맞히면 1~2 사이클이다 (`objdump` 로 본 `_empty_fn` 은 `ret` 한 줄, 루프에 `bl 0x100000e14 <_empty_fn>` 가 남아 있음을 확인). 간접 호출(함수 포인터)도 대상이 늘 같으면 예측이 맞아 거의 같다. **macOS 의 `getpid()` 가 이 무리에 있다** — 아래.
2. **4~16 ns — 커널에 안 가는 시계.** `mach_absolute_time` 은 commpage 의 오프셋 + `mrs CNTVCT_EL0` (위 그림 ②). `clock_gettime` 은 libc 가 clock id 를 보고 분기한 뒤 그 위에 단위 변환을 얹어서 2~4 배. `CLOCK_MONOTONIC` 은 기계어상 `_mach_boottime_usec` + `gettimeofday`(→ `__commpage_gettimeofday`) 조합에 `× 1000` — 그래서 가장 비싸고 **µs 단위** 다 (OS-M02 `demo_timer.c`). 측정용 시계는 `CLOCK_UPTIME_RAW` / `mach_absolute_time` 을 쓴다.
3. **100 ns ~ 1.5 µs — 진짜 시스템 콜.** `getppid` 는 커널에서 하는 일이 "부모 pid 읽기" 뿐이라 **모드 전환 왕복의 바닥값** 에 가깝다: `svc` → 예외 진입 → 레지스터 저장 → 번호(x16 = 39)로 디스패치 → 복원 → `eret`. `read(fd, buf, 0)` 은 같은 왕복에 **fd → 파일 객체 조회, 참조 카운트, vnode 경로** 가 붙어 2.6 배. `write(/dev/null)` 은 15 배다. `demo_paths.c` 로 경로만 바꿔 보면 (실제 출력, 2 바퀴째):

```text
getppid                       132.7 ns
read0 pipe                    231.2 ns
read0 /dev/null               315.4 ns
read0 regular file            297.2 ns
read1 /dev/zero               350.5 ns
pread1 regular file           484.6 ns
write0 /dev/null             2009.5 ns
write1 /dev/null             1922.4 ns
write1+read1 pipe (pair)      538.2 ns
```

(다른 작업이 돌던 때라 bench 표보다 전체가 15~30 % 높다 — 비율만 본다.) **0 바이트 write 도 2 µs** 이고, 파이프에 1 바이트 쓰고 읽는 **한 쌍** 이 0.54 µs 다. 즉 바이트 수가 아니라 /dev/null 로 가는 쓰기 경로 자체가 길다. 읽기는 파이프 < 장치·정규 파일 < `pread`(1 바이트 실제 복사 포함) 순. 왜 /dev/null 쓰기가 긴지는 커널 함수 단위로 쪼개 봐야 아는데, macOS 에서 그 도구(DTrace/dtruss)는 sudo 와 SIP 해제가 필요해서 이 레슨에서는 쓰지 않았다 — **원인 미확인**. 교훈은 하나: **"시스템 콜 비용" 은 모드 전환 바닥값 + 그 콜이 커널 안에서 걷는 길이다.**

**콜드(첫 호출):** 첫 `getppid` 333 ns vs 두 번째 83 ns (단발이라 눈금 41.67 ns 단위). 다른 실행에서 292/125, 458/167, 375/167 ns. 첫 호출은 libSystem 스텁·커널 경로의 i-cache/TLB 가 차갑다. 실시간 경로에서 "첫 호출만 느린" 지터의 원인이 된다 — 그래서 RT 스레드는 시작할 때 쓸 경로를 한 번씩 미리 밟는다(pre-fault, warm-up).

#### "정말 커널에 들어갔나?" — 세기 (solution.c Part B)

시간으로 "100 ns 니까 트랩이겠지" 는 추측이다. 맥에서 `dtruss` 는 sudo 와 SIP 해제가 필요하지만, 커널이 태스크마다 세는 카운터가 있다: `task_info(mach_task_self(), TASK_EVENTS_INFO, …)` 의 `syscalls_unix`(BSD 시스템 콜)와 `syscalls_mach`(Mach 트랩). `demo_count.c` (각 10만 번) 실제 출력:

```text
nothing                      unix +0 mach +1 csw +2
getpid                       unix +0 mach +1 csw +0
getppid                      unix +100000 mach +1 csw +26
read0                        unix +100000 mach +1 csw +70
write1 devnull               unix +100000 mach +1 csw +347
clock_gettime MONOTONIC      unix +0 mach +1 csw +2
clock_gettime REALTIME       unix +0 mach +1 csw +0
clock_gettime UPTIME_RAW     unix +0 mach +1 csw +1
clock_gettime THREAD_CPU     unix +100000 mach +1 csw +44
clock_gettime PROCESS_CPU    unix +100000 mach +1 csw +53
clock_gettime_nsec_np        unix +0 mach +1 csw +3
mach_absolute_time           unix +0 mach +1 csw +3
mach_thread_self+dealloc     unix +0 mach +200001 csw +74
```

(`unix`·`mach` 열은 실행마다 똑같고, `csw` 만 부하에 따라 바뀐다 — 다른 실행에서 getppid 줄은 `csw +163`.)

- `mach +1` 은 카운터를 읽는 `task_info` 자신(Mach 메시지)이다. `csw` 는 그동안의 컨텍스트 스위치 수 — 오래 걸린 루프일수록 많이 선점당했다.
- **`getpid` 는 10만 번에 0 번.** libSystem 의 `__getpid` 기계어 (`demo_disasm.sh` 실제 출력):

```text
libsystem_kernel.dylib`__getpid:
    adrp   x9, 448220
    add    x9, x9, #0x710            ; _current_pid
    ldr    w0, [x9]                  ; 캐시된 pid
    cmp    w0, #0x0
    b.le   <+24>                     ; 0 이하면 커널로
    ret                              ; ← 보통은 여기서 끝 (0.66 ns)
    mov    x16, #0x14                ; =20 (SYS_getpid)
    svc    #0x80
    ...
    ldxr   w10, [x9]                 ; 받은 pid 를 캐시에 (아직 0 일 때만)
    cbnz   w10, <+76>
    stxr   wzr, w0, [x9]
    ret
```

  그래서 OSTEP 이 "간단한 시스템 콜" 로 `getpid` 를 떠올리게 하더라도 **macOS 에서는 시스템 콜 비용을 재는 데 쓸 수 없다**. glibc 도 2.25 이전에는 pid 를 캐시했다 (2.25 릴리스 노트에서 캐시 제거 — 아래 리눅스 결과는 2.43 이라 매번 트랩). 확실한 트랩이 필요하면 `getppid` 를 쓴다 (기계어: `mov x16, #0x27 ; =39` / `svc #0x80`, 캐시 없음).
- **fork 하면?** 자식은 pid 가 다르므로 캐시가 비워져야 한다. 연습 2 에서 자식의 getpid 1000 번 = BSD 콜 **1 번**, 다시 1000 번 = 0 번, 값은 fork 가 돌려준 자식 pid 와 같음을 확인했다.
- `CLOCK_THREAD_CPUTIME_ID` / `CLOCK_PROCESS_CPUTIME_ID` 는 **트랩한다** — "이 스레드가 CPU 를 얼마나 썼나" 는 스케줄러(커널)만 안다. 기계어상 각각 `__thread_selfusage`(`mov x16, #0x1e2 ; =482` / `svc #0x80`)와 `getrusage` 로 간다. 프로파일링 코드에서 이걸 핫 루프에 넣으면 시스템 콜 비용을 낸다.
- `mach_thread_self` + `mach_port_deallocate` 는 mach 쪽으로 정확히 2 번씩 — Mach 트랩은 별도 카운터다 (Mach IPC 는 OS-A14).

`solution.c` 의 Part B CHECK 는 이 사실들을 2만 번씩 다시 센다. 시간과 달리 **매번 같다** (8 회 연속, ASan 빌드 포함 동일).

#### 간접 비용 — 커널이 내 캐시를 밀어낸다 (`demo_pollute.c`)

시스템 콜 동안 커널 코드·데이터가 L1 에 들어오면서 내 작업 집합(working set)이 일부 쫓겨난다. 돌아와서 다시 채우는 시간은 `getppid` 의 100 ns 에 안 잡히고 **내 코드가 느려진 것으로** 보인다. 배열을 캐시 라인(이 맥 `hw.cachelinesize` = 128 B) 간격으로 한 바퀴 읽는 일에 `getppid` 를 끼워 `indirect = both − work − sys` 를 쟀다. 실제 출력(3 회 중 하나, 단위 ns/반복):

```text
    bytes       work      sys       both   indirect
     8 KiB        8.6    106.2      116.2       +1.5
    32 KiB       33.7    102.5      141.9       +5.7
    48 KiB       52.2    104.5      160.0       +3.3
    64 KiB       69.1    104.1      177.7       +4.5
    96 KiB      121.0    105.4      218.7       -7.7
   112 KiB      137.5    101.0      249.3      +10.9
   128 KiB      160.7    101.9      312.3      +49.7
   256 KiB      971.0    110.6     1103.0      +21.4
  1024 KiB     3971.4    122.4     4125.2      +31.5
```

- 작업 집합이 작으면(≤ 64 KiB) 간접 비용은 3 회 모두 −2~+9 ns — 잡음 수준, 직접 비용의 10 % 이하.
- **L1D 경계에서 커진다: 128 KiB 는 3 회 모두 +50~+82 ns** (+82, +50, +51). 96·112 KiB 는 −8~+50 으로 실행마다 흔들렸다. 이 맥의 L1D 는 Super 코어 128 KiB, Performance 코어 64 KiB (`hw.perflevel0/1.l1dcachesize`) 라서, 겨우 들어가 있던 배열이 커널의 몇 줄에 밀려 L2 에서 다시 와야 한다. 시스템 콜 하나가 **자기 비용의 절반을 더** 청구한 셈이다.
- L2 크기 이상(256 KiB~)에서는 어차피 L2 에서 읽으므로 차이가 잡음(±30~80 ns) 속에 묻힌다.
- 한계: macOS 는 코어를 고정할 수 없어(OS-M02) 어느 코어에서 돌았는지 모른다. 그래서 경계가 96 KiB 와 128 KiB 사이에서 흔들린다. 결론은 "작업 집합이 캐시에 꽉 찬 핫 루프에서 시스템 콜은 보이는 것보다 비싸다" 까지만 말한다. (TLB·분기 예측기 오염도 같은 종류의 간접 비용이다. 이것을 줄이려고 콜을 모아서 한 번에 넘기는 설계가 io_uring 이다 — LLAT-A03.)

#### 리눅스 (Lima VM `elinux`: aarch64 Ubuntu 26.04, 커널 7.0.0-38-generic, gcc 15.2.0, glibc 2.43)

홈 마운트가 읽기 전용이라 VM `/tmp/OS-B03/` 로 복사해 `gcc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench` 로 빌드했다. 리눅스에서는 `mach_*` 대신 `syscall(SYS_clock_gettime, …)` (vDSO 를 우회한 **강제 시스템 콜**)을 같은 표에 넣는다. 실제 출력:

```text
timer: clock_gettime(CLOCK_MONOTONIC), res 1 ns
cold: 1st getppid 458 ns, 2nd 167 ns (단발 측정, 눈금 단위로 반올림됨)
7 runs (+1 warm-up) per target. loop overhead median 0.228 ns/iter (아래 값에서 뺐다)

target                          N/batch       min    median       max   /getppid
empty_fn()                     60000000      0.28      0.35      0.40      0.002
empty via fnptr                40000000      0.37      0.41      0.44      0.003
getpid()                         219579    137.29    140.49    143.57      0.933
clock_gettime(MONO)             1832061     13.56     13.89     15.28      0.092
clock_gettime(REAL)             2093071     13.73     14.16     16.02      0.094
clock_gettime(MONO_RAW)         2162162     14.11     14.63     15.22      0.097
getppid()                        182370    146.50    150.62    154.80      1.000
read(fd,buf,0)                   162234    176.64    179.01    182.75      1.189
write(/dev/null,1)               181086    173.07    187.35    197.74      1.244
clock_gettime(THREAD_CPU)        146788    199.66    212.91    215.43      1.414
syscall(SYS_clock_gettime)       167870    171.52    174.98    179.67      1.162
```

| 관찰 | macOS (호스트) | Linux (VM) |
|---|---|---|
| `getpid` | 0.66 ns — libc 캐시 | 140 ns — 매번 트랩 (glibc ≥ 2.25) |
| `getppid` (바닥값) | ~104 ns | ~151 ns |
| 같은 시계, 사용자 공간 vs 강제 시스템 콜 | — | vDSO 13.9 ns vs `syscall()` 175 ns → **12.6 배** |
| `read` 0 B / `write` 1 B `/dev/null` | 2.6× / 15× getppid | 1.2× / 1.2× getppid |
| `CLOCK_THREAD_CPUTIME_ID` | 트랩 (121 ns) | 트랩 (213 ns) — arm64 vDSO 는 CPU 시간 clock 을 처리하지 않음 |

- VM 안이지만 게스트 프로세스의 `svc` 는 게스트 커널(EL1)이 처리하고 하이퍼바이저(EL2)로 나가지 않는다 — 그래서 시스템 콜 비용 비교에는 쓸 만하다. 그래도 네이티브 리눅스 숫자는 아니다 (VM 탈출 비용은 VIRT-M02).
- `res 1 ns` 라고 나오지만 **cold 줄의 값이 41.67 의 배수다** (458 = 11 × 41.67). 이 VM 의 클록소스는 `arch_sys_counter`(`/sys/devices/system/clocksource/clocksource0/current_clocksource`), 즉 24 MHz 카운터라서 두 번 연속 읽은 차이의 최소 0 아닌 값이 41 ns 였다. `clock_getres` 가 1 ns 라고 해도 실제 카운터 눈금은 그보다 굵을 수 있다 — 직접 연속 읽기로 확인한다.
- vDSO 는 `/proc/self/maps` 의 `[vdso]` 줄(`r-xp`, 2 페이지)과 보조 벡터 `AT_SYSINFO_EHDR` 로 보인다 (실측). 커널이 이 페이지에 시계 변환 계수를 써 두고 사용자 코드가 seqcount 로 일관된 값을 읽는다 — macOS commpage 와 같은 구조 (`ldr / mrs / ldr / cmp / b.ne`).

`strace -c ./bench 100000 3` (대상마다 4 × 10만 번) 실제 출력:

```text
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 37.29    6.719960           8    800000           clock_gettime
 18.38    3.312401           8    400002           getppid
 16.34    2.944799           7    400001           read
 14.12    2.544649           6    400001           write
 13.86    2.497742           6    400000           getpid
  0.00    0.000016           5         3           close
  ...
------ ----------- ----------- --------- --------- ----------------
100.00   18.019567           7   2400038         1 total
```

- `clock_gettime` 80만 = `THREAD_CPU` 40만 + `syscall(SYS_clock_gettime)` 40만. **MONO / REAL / MONO_RAW 120만 번은 한 번도 안 세졌다** — vDSO 는 커널에 안 들어가므로 ptrace 가 볼 수 없다. 벤치 자신의 타이머(`clock_gettime(MONOTONIC)`)도 마찬가지.
- `getpid` 40만 = 매번 트랩. `getppid` 400002 와 `read`/`write` 400001 의 +1~2 는 cold 측정·`open` 직후 등 하네스 몫.
- **strace 아래 벤치 결과는 쓰레기다:** 같은 실행의 getppid median 이 90,377 ns (평소의 600 배). strace 는 시스템 콜마다 ptrace 로 멈추고 트레이서 프로세스로 두 번 전환한다. `usecs/call` 6~8 µs 도 그 오버헤드가 섞인 값이다. **strace -c 는 "몇 번" 을 세는 도구이지 "얼마나" 를 재는 도구가 아니다** — 오버헤드가 작은 대안(`perf trace -s`, `perf stat -e 'syscalls:sys_enter_*'`, bpftrace)은 LPERF-B07.

### Part C — 펌웨어 예산

**타이머 해상도 → 반복 수.** 눈금 41.667 ns 타이머로 0.25 ns 짜리 함수 호출을 100 ppm 이하 오차로 재려면 `41667 ps × 1e6 / (250 ps × 100) = 1,666,680` 번. 100 ns 짜리 시스템 콜이면 4,167 번. `demo_bench.c` 의 자동 N(배치 ≈ 30 ms)은 둘 다 넉넉히 넘는다. MCU 에서 `DWT->CYCCNT`(사이클 단위)로 잴 때는 눈금이 1 사이클이라 반복이 덜 필요하지만, 대신 인터럽트가 끼면 그 run 을 버리는 median 이 더 중요하다 (RTOS-A03).

**센서 읽기 배치.** 8 kHz IMU 를 임베디드 리눅스 사용자 공간에서 받는다고 하자. 읽기 한 번 = `poll()` + `read()` 2 콜, 콜당 1500 ns 라고 **가정** (Cortex-A53 급 코어의 값은 그 보드에서 `demo_bench.c` 로 재서 넣는다 — 이 맥의 100~300 ns 를 그대로 쓰면 안 된다).

| batch | 읽기/초 | 시스템 콜 CPU (ns/s) | CPU % | 첫 샘플 대기 |
|---|---|---|---|---|
| 1 | 8000 | 24,000,000 | 2.4 % | 0.125 ms |
| 5 | 1600 | 4,800,000 | 0.48 % | 0.625 ms |
| 8 | 1000 | 3,000,000 | 0.30 % | 1.0 ms |
| 24 | 333.3 | 1,000,000 | 0.10 % | 3.0 ms |

- 예산 0.5 % (5,000,000 ns/s) → `min_batch = ceil(24e6 / 5e6) = 5`. 지연 한도 1 ms → `max_batch = floor(8000 × 1000 / 1e6) = 8`. 5 ≤ 8 이라 `plan_batch = 5`.
- 예산 0.1 % 는 batch 24 가 필요한데 지연 한도는 8 → **불가능(0)**. 이때 선택지는 (a) 콜 수 줄이기 — `poll` 없이 블로킹 `read` 1 콜, (b) 커널 쪽에서 모으기 — IIO 버퍼의 watermark 를 5 로 두면 커널이 5 개 모일 때만 깨운다 (ELX-M09), (c) 지연 한도를 다시 협상.
- 이 계산은 직접 비용만이다. Part B 에서 본 간접 비용(캐시 오염, 깨움·컨텍스트 스위치 — OS-M02)이 더해지므로 실제 여유는 더 남겨 둔다.
- 펌웨어 대응: 베어메탈 MCU 에는 "시스템 콜" 이 없다 — 드라이버 호출은 그냥 함수 호출(0.4 ns 무리)이다. FreeRTOS-MPU 처럼 권한을 나누면 커널 API 호출이 `SVC` 예외 진입·복귀가 되고, Cortex-M3/M4 는 예외 진입만 12 사이클이다 (Arm 문서의 interrupt latency). 같은 "경계 비용" 논리가 MCU 에서도 성립한다 (SEC-M08, ARM-M09, ARCH-M10).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
```

실제 출력 (레슨 폴더와 `/tmp` 두 곳에서 8 회 연속 실행, 모두 같다. 실행 시간 0.08 초):

```text
51/51 passed
```

```bash
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_asan && ./so_asan   # 51/51, exit 0
```

ASan 빌드에서도 Part B 의 진입 횟수가 그대로였다 (ASan 이 `read`/`write` 를 가로채 버퍼를 검사하지만 시스템 콜을 더 만들지는 않는다).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `static void f(void) {}` 를 루프에서 불러 "함수 호출 0 ns" | `-O2` 가 인라인하고 빈 몸체를 지운다. 루프도 통째로 사라질 수 있다 | `__attribute__((noinline))` + 몸체에 `__asm__ volatile("" ::: "memory")`, 루프에도 BARRIER, 반환값 누적 |
| `getpid()` 로 시스템 콜 비용 측정 | macOS libSystem 은 `_current_pid` 캐시 → 0.66 ns, 커널 진입 0 회 (실측). glibc < 2.25 도 캐시 | 캐시 없는 `getppid()` 또는 `read(fd, buf, 0)`, 그리고 **세어서 확인** (`task_info` / `strace -c`) |
| 한 번 호출을 `mach_absolute_time` 두 번으로 잼 | 눈금 41.67 ns. 0.4 ns 짜리는 0 또는 41.67, 100 ns 짜리도 ±40 % | 배치 N 회 ÷ N. `iters_for_resolution` 으로 N 결정 |
| 평균 하나만 보고, 첫 run 포함 | 콜드 run·인터럽트 맞은 run 이 평균을 끌어올린다 (테스트의 230 ns run) | 워밍업 버림 + run 7 개의 median (+min/max 같이 보고) |
| 루프 오버헤드 안 빼거나, 빼서 음수 | 빈 함수 0.4 ns 에 루프 0.23 ns 가 섞이면 1.6 배로 보임. 잡음으로 음수 가능 | `net_ns_per_call`: 빼고 0 으로 자르기 |
| `ticks * numer / denom` | 큰 tick 에서 `ticks * 125` 가 64 비트 넘침 (테스트: 2^58) | `q * numer + r * numer / denom` |
| `ceil(p / 100.0 * n)` | `0.9 * 10 = 9.000000000000002` 류 오차로 rank 가 하나 밀림 | `ceil(p * n / 100.0)` + `[1, n]` 으로 자르기 |
| strace 를 켠 채로 시간 측정 | ptrace 로 콜마다 트레이서로 두 번 전환 → getppid 90 µs (600 배) | strace 는 횟수만. 시간은 따로 실행, 저오버헤드 도구는 LPERF-B07 |
| 측정에 macOS `CLOCK_MONOTONIC` 사용 | µs 단위(기계어상 usec × 1000)이고 15 ns 로 시계 중 가장 비쌈 | `CLOCK_UPTIME_RAW` / `clock_gettime_nsec_np` / `mach_absolute_time` |
| "100 ns 니까 시스템 콜 비용은 100 ns" 로 일반화 | 같은 맥에서 `read` 0 B 는 270 ns, `write(/dev/null)` 은 1.5 µs. 경로마다 다르다 | 바닥값(getppid)과 실제 쓸 콜을 **둘 다** 잰다 |

## 8. 면접 Q&A

<details>
<summary>Q1. 시스템 콜 한 번은 얼마나 걸리고, 그 시간은 어디에 쓰이나요?</summary>

이 맥(Apple M6)에서 `getppid` 가 약 100 ns, 리눅스 VM 에서 약 150 ns. 커널이 거의 아무 일도 안 하는 콜이라 **모드 전환 왕복의 바닥값** 이다: libc 스텁이 번호를 레지스터에 넣고(`mov x16, #39`) `svc` → CPU 가 EL1 로 올라가 예외 벡터로 → 사용자 레지스터 저장 → 번호로 핸들러 디스패치 → 결과 저장 → 레지스터 복원 → `eret`. 여기에 콜마다 커널 경로가 붙는다: `read` 0 B 는 fd 조회 등으로 270 ns, `/dev/null` write 는 1.5 µs. 그리고 숨은 **간접 비용**: 커널이 L1·TLB·분기 예측기를 흐려 놓아서 돌아온 뒤 내 코드가 느려진다 — L1 에 꽉 찬 작업 집합에서 콜당 +50~80 ns 를 쟀다. 꼬리: "함수 호출과 비교하면?" → 함수 호출 0.4 ns, 즉 250 배.

</details>

<details>
<summary>Q2. clock_gettime 은 왜 시스템 콜보다 10배 이상 싸죠? 그럼 아무 콜이나 그렇게 만들면 되지 않나요?</summary>

커널에 안 들어가기 때문이다. 리눅스는 vDSO, macOS 는 commpage — 커널이 모든 프로세스에 읽기 전용으로 매핑한 페이지에 "카운터 → 시각" 변환 계수를 써 두고, 사용자 코드가 하드웨어 카운터(arm64 `CNTVCT_EL0`, EL0 에서 읽기 허용)를 직접 읽어 계산한다. 계수가 갱신되는 도중에 읽으면 안 되므로 "버전 읽기 → 카운터 읽기 → 버전 다시 읽기 → 다르면 재시도" (seqlock 패턴, 실제 기계어 `ldr / mrs / ldr / cmp / b.ne`) 로 일관성을 맞춘다. 실측: vDSO 13.9 ns vs 같은 시계를 `syscall()` 로 강제 175 ns. 아무 콜이나 그렇게 못 하는 이유: 사용자 공간이 **읽기만** 하면 되는, 커널이 미리 써 둘 수 있는 전역 상태여야 한다. 파일 읽기처럼 권한 검사·상태 변경이 필요한 일은 반드시 커널이 해야 한다. 스레드 CPU 시간도 스케줄러만 알아서 macOS·리눅스(arm64) 둘 다 트랩했다. 꼬리: "strace 에 clock_gettime 이 안 보이는데요?" → vDSO 라서 ptrace 가 못 본다 (실측: MONO 120만 번 → 0 회 집계).

</details>

<details>
<summary>Q3. getpid 를 100만 번 불러서 시스템 콜 비용을 쟀더니 0.7 ns 가 나왔습니다. 왜죠?</summary>

macOS 의 libSystem 은 pid 를 `_current_pid` 에 캐시한다. 기계어상 `ldr w0, [_current_pid]` → 0 보다 크면 `ret`, 아니면 `svc`. `task_info` 카운터로 세면 10만 번에 BSD 콜 0 회. glibc 도 2.25 전에는 캐시했다 (그 뒤 제거: 리눅스 VM 에서 140 ns, strace 40만 회 집계). 캐시의 함정은 fork — 자식은 pid 가 바뀌므로 캐시를 비워야 한다. 맥에서 자식의 첫 getpid 만 1 번 트랩하고 그 뒤는 0 번임을 쟀다. (예전 glibc 는 `clone()` 을 직접 부르면 캐시가 틀린 pid 를 돌려주는 문제가 있었고, 그게 캐시 제거의 이유 중 하나였다.) 결론: 측정 대상은 **캐시 없는 콜(getppid)** 로 고르고, 시간 말고 **횟수로 검증** 한다.

</details>

<details>
<summary>Q4. 측정 방법을 설명해 보세요. 어떤 함정이 있나요?</summary>

(1) 타이머 해상도: `mach_absolute_time` 은 41.67 ns 눈금이라 한 번은 못 잰다 → N 번 배치(≈30 ms)로 묶어 나눈다. 리눅스 VM 도 `res 1 ns` 라지만 실제 눈금은 41 ns 였다. (2) 컴파일러: 결과를 안 쓰는 루프·빈 함수는 사라진다 → `noinline`, `asm volatile` 장벽, 반환값 누적, 그리고 `objdump -d` 로 `bl _empty_fn` 이 남았는지 확인. (3) 루프 오버헤드(0.23 ns)를 빈 루프로 재서 뺀다. (4) 워밍업: 첫 호출은 콜드(333 vs 83 ns). (5) 잡음: 인터럽트·다른 프로세스·코어 이동(P/E)·주파수 → run 7 개의 median 과 min/max 를 같이 보고. 평균만 보면 안 된다. (6) 관측 도구가 대상을 바꾼다: strace 아래 getppid 90 µs. (7) 결과는 절대값보다 비율로 말한다 — run 마다 10~20 % 흔들렸지만 무리 사이 비율은 같았다. 꼬리: "macOS 에서 코어 고정은?" → `sched_setaffinity` 가 없다. QoS 로 P/E 만 유도할 수 있다 (OS-M02).

</details>

<details>
<summary>Q5. read(fd, buf, 0) 은 아무것도 안 읽는데 왜 getppid 보다 2.6 배 비싸죠?</summary>

0 바이트여도 커널은 할 일을 한다: fd 번호로 프로세스의 fd 테이블에서 파일 객체를 찾고 참조를 잡고(동시에 다른 스레드가 close 할 수 있으므로), 읽기 모드인지 확인하고, 파일 종류별 경로를 거쳐 0 을 돌려주고 참조를 놓는다. POSIX 는 nbyte 가 0 이면 에러를 "감지할 수도 있다(may)" 고만 하는데, macOS 와 리눅스 VM 모두 `read(99, buf, 0)` 에 `-1 / EBADF` 를 돌려줬다 (실측) — fd 검사를 실제로 한다는 뜻이다. `getppid` 는 현재 프로세스 구조체에서 값 하나만 읽는다. 그래서 "시스템 콜 비용" 은 **모드 전환 바닥값 + 콜별 경로** 로 나눠 말해야 한다. 이 맥에서 `/dev/null` write 는 0 바이트도 2 µs 였고 파이프 write+read 쌍은 0.54 µs 였다 (`demo_paths.c`) — 경로 차이가 모드 전환보다 클 수 있다. 같은 콜이 리눅스 VM 에서는 getppid 의 1.2 배였다: OS 마다 경로 길이가 다르다. 꼬리: "원인을 어떻게 찾나?" → 리눅스는 `perf record -g` / `perf trace` / ftrace 로 커널 함수 단위 분해 (LPERF-B05, LPERF-B07). macOS 는 DTrace 가 sudo + SIP 해제를 요구해 이 레슨에서는 미확인으로 남겼다.

</details>

<details>
<summary>Q6. 고주파 센서 데이터를 사용자 공간에서 받습니다. 샘플마다 read 해도 되나요?</summary>

계산해서 정한다. 콜 비용 × 콜 수 × 읽기 빈도 = CPU 비용. 예: 8 kHz, `poll + read` 2 콜, 콜당 1.5 µs(그 보드에서 잰 값이라고 가정) → 샘플마다 읽으면 2.4 % CPU. 예산 0.5 % 면 5 개씩 묶어야 하고, 묶으면 첫 샘플이 0.625 ms 기다린다 — 지연 한도 1 ms 안이라 OK. 예산과 지연이 충돌하면 (a) 콜 수 줄이기(블로킹 read 1 콜), (b) 커널에서 모으기 — IIO 버퍼 watermark, DMA + 링 버퍼 (ELX-M09), (c) 대량 I/O 는 io_uring 처럼 제출·완료를 배치 (LLAT-A03). 그리고 sysfs 로 샘플마다 `open/read/close` 하며 문자열을 파싱하는 구현은 콜 3 개 + 파싱이라 가장 나쁘다. 꼬리: "MCU 라면?" → 베어메탈은 경계가 없어 드라이버 호출이 함수 호출이다. RTOS 에서 권한을 나누면 `SVC` 경계가 생기고 같은 계산을 한다 (SEC-M08).

</details>

<details>
<summary>Q7. Spectre/Meltdown 이후 시스템 콜이 느려졌다는 얘기는 뭔가요?</summary>

Meltdown 대응인 KPTI(커널 페이지 테이블 격리)는 x86 에서 사용자 모드일 때 커널 매핑을 페이지 테이블에서 빼 두고, 시스템 콜마다 페이지 테이블(CR3)을 바꾼다. PCID 가 없으면 TLB 가 비워져 간접 비용까지 커진다. arm64 리눅스에도 같은 생각의 옵션(커널 매핑을 사용자 모드에서 숨기는 KPTI, `CONFIG_UNMAP_KERNEL_AT_EL0`)이 있고, 취약한 코어에서만 켠다. 이 레슨의 수치에서 어떤 완화가 켜져 있는지는 확인하지 않았다 — 같은 하드웨어라도 커널 설정·마이크로코드에 따라 바닥값이 달라질 수 있다는 것이 요점이다. 그래서 숫자는 "이 머신, 이 커널" 이라는 꼬리표와 함께 말한다. 꼬리: "확인 방법은?" → 리눅스 `/sys/devices/system/cpu/vulnerabilities/*` 와 부팅 로그.

</details>

## 9. 추가 연습

### 연습 1 — strace -c 표 파서

리눅스 VM 에서 받은 `strace -c` 출력(문자열 상수로 박아 둠)에서 시스템 콜 이름 → `calls` 를 돌려주는 `long strace_calls(const char *table, const char *name)` 를 작성하라. `errors` 칸이 비어 있는 줄과 채워진 줄(`faccessat`)을 모두 처리하고, `total` 줄도 읽고, 헤더·구분선은 건너뛴다. 없으면 −1.

<details>
<summary>답 보기</summary>

한 줄씩 잘라 공백으로 토큰화하면 칸 수가 5(errors 없음) 또는 6 이다. **첫 토큰이 숫자로 시작하는 줄만** 데이터 줄 — 헤더 `% time` 과 구분선 `------` 이 걸러진다. calls 는 항상 네 번째 토큰(`tok[3]`), 이름은 마지막 토큰. 그래서 `strace_calls(t, "calls")` 는 −1 (헤더는 데이터가 아니다). 결과: `getppid` 400002, `clock_gettime` 800000 (vDSO 3 종 120만 번은 없음), `faccessat` 1, `total` 2400038. 실무에서는 `strace -c -S calls` 로 정렬하고, 비교 자동화에는 `-o file` 로 받는다. 칸 위치는 strace 버전마다 바뀔 수 있으니 헤더를 읽어 인덱스를 정하는 편이 더 튼튼하다.

</details>

### 연습 2 — fork 뒤 getpid 캐시

macOS 에서 자식 프로세스의 getpid 1000 번이 BSD 시스템 콜을 몇 번 만드는지 예측하고, 자식이 결과를 파이프로 보내 부모가 CHECK 하게 하라. 자식에 워치독(`alarm`)을 걸고 부모는 반드시 `waitpid`.

<details>
<summary>답 보기</summary>

실측: 부모 1000 번 → 0 회, 자식 첫 1000 번 → **1 회**, 자식 다음 1000 번 → 0 회, 자식이 본 pid == fork 의 반환값. fork 가 자식 쪽에서 캐시를 0 으로 비우고, 첫 getpid 가 `svc` 로 진짜 pid 를 받아 `ldxr/stxr` 로 캐시에 넣는다 (5. 풀이의 기계어). 자식은 `printf` 대신 `write` 로 구조체를 보내고 `_exit` 한다 — fork 전 `fflush(stdout)` 를 안 하면 부모 버퍼가 자식에 복제되어 두 번 찍힌다 (실제로 겪음: 첫 시도에서 "parent …" 줄이 두 번 나왔다). 프로세스 생성 자체는 OS-B01.

</details>

### 연습 3 — 선형 보간 백분위수

`percentile_lin(s, n, p)` (R type 7 / numpy 기본: `h = (n−1)p/100`, 이웃 두 값 보간)를 작성하고, Part A 의 7 run `{100,101,101,102,103,104,229}` 에서 p90 을 nearest-rank 와 비교하라.

<details>
<summary>답 보기</summary>

h = 6 × 0.9 = 5.4 → `s[5] + 0.4 × (s[6] − s[5])` = 104 + 0.4 × 125 = **154**. nearest-rank 는 `ceil(0.9 × 7) = 7` 번째 = **229**. 표본이 7 개면 "p90" 은 사실상 최댓값이고, 보간값 154 는 한 번도 관측되지 않은 숫자다. 짝수 n 의 p50 도 다르다: `{10,20,30,40}` → 보간 25, nearest-rank 20. 어느 정의를 썼는지 함께 적지 않은 p99 는 비교할 수 없다. 표본이 수천 개 이상이면 둘의 차이는 작아진다 (OS-M02 의 p99 는 20만 표본).

</details>

### 연습 4 — 시스템 콜 고정비 상각

읽기 한 번 = 고정비 300 ns + 바이트당 50 ps(= 20 GB/s 복사). "효율(복사 시간 / 전체)" 이 90 %, 99 % 가 되는 최소 읽기 크기는? `min_chunk_bytes(syscall_ps, copy_ps_per_byte, eff_pct)` 를 정수 연산으로.

<details>
<summary>답 보기</summary>

효율 `b·c / (S + b·c) ≥ e` → `b ≥ S·e / (c·(1 − e))`. 정수로 `ceil(S·eff / (c·(100 − eff)))`: 90 % → 300000·90 / (50·10) = **54,000 B**, 99 % → **594,000 B**, 50 % → 6,000 B (고정비 = 복사비). 99 % 로 가려면 90 % 의 11 배 크기가 필요하다 — 수확 체감. 그래서 `read` 버퍼는 보통 수십~수백 KiB 에서 멈춘다 (stdio 의 `BUFSIZ`, `cp` 의 버퍼). 너무 크면 이번엔 캐시를 넘쳐 복사가 느려진다 (Part B 의 L1/L2 경계와 같은 이야기). mmap 은 이 고정비를 페이지 폴트로 바꾼다 (OS-B09).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 19/19 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pa && ./pa   # 19/19, 깨끗
```

## 10. 핵심 정리

- 세 무리 (Apple M6): 함수 호출 ≈ 0.4 ns / commpage·vDSO 시계 4~16 ns / `svc` 시스템 콜 100 ns(getppid) ~ 1.5 µs(`write /dev/null`). **비율로 기억** (약 1 : 25 : 250+).
- 시스템 콜 비용 = 모드 전환 바닥값(`svc` → 벡터 → 저장 → 디스패치 → 복원 → `eret`) + 콜별 커널 경로 + 간접 비용(캐시 오염: L1 경계에서 +50~80 ns).
- 시계는 커널이 매핑한 페이지(commpage/vDSO) + `CNTVCT_EL0` 로 사용자 공간에서 읽는다. seqlock 재시도 루프. strace 에 안 보인다. CPU 시간 clock 은 트랩한다.
- **macOS `getpid` 는 캐시** (0 트랩, fork 뒤 첫 호출만 트랩). 시스템 콜 측정엔 `getppid`. 추측 말고 센다: macOS `task_info(TASK_EVENTS_INFO).syscalls_unix`, 리눅스 `strace -c`.
- 측정: 배치 ÷ N(타이머 눈금 ≫ 대상), 워밍업 버림, 루프 오버헤드 빼기, 7 run median + min/max, `noinline` + `asm volatile` 장벽, `objdump` 확인. strace 아래에서는 시간을 재지 않는다.
- 펌웨어 예산: CPU = 빈도/batch × 콜 수 × 콜 비용. 예산은 batch 의 하한(올림), 지연은 상한(내림) — 충돌하면 콜 수를 줄이거나 커널에서 모은다(IIO watermark, io_uring).

## 참고자료

- OSTEP — 6장 *Mechanism: Limited Direct Execution* (6.2 트랩·트랩 테이블, Homework (Measurement): 0 바이트 read 로 시스템 콜 비용 재기): <https://pages.cs.wisc.edu/~remzi/OSTEP/cpu-mechanisms.pdf> · 로컬 원문 `operating-system-three-easy-pieces/book-md/C06_mechanism_limited_direct_execution.md`, Don 의 노트 `notes/2026-09-30_C06_limited_direct_execution.md`
- POSIX.1-2017 — `read()` (nbyte 0 의 의미), `clock_gettime()`, `getppid()`: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/read.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/clock_getres.html>
- macOS man pages — `man 3 clock_gettime` (`CLOCK_UPTIME_RAW`, `clock_gettime_nsec_np`), `man 3 mach_absolute_time` 대신 헤더 `<mach/mach_time.h>`, `<mach/task_info.h>` (`TASK_EVENTS_INFO`, `syscalls_unix`, `syscalls_mach`)
- Apple XNU 소스 (libsyscall, commpage): <https://github.com/apple-oss-distributions/xnu>
- Linux man pages — `vdso(7)`, `syscall(2)`, `strace(1)`, `getpid(2)` (NOTES: glibc 2.25 에서 pid 캐시 제거): <https://man7.org/linux/man-pages/man7/vdso.7.html>, <https://man7.org/linux/man-pages/man2/getpid.2.html>
- Arm Architecture Reference Manual (DDI0487) — `SVC`, 예외 레벨, `CNTVCT_EL0`(Generic Timer) EL0 접근: <https://developer.arm.com/documentation/ddi0487/latest/>
- Soares & Stumm, *FlexSC: Flexible System Call Scheduling with Exception-Less System Calls*, OSDI 2010 — 시스템 콜의 간접 비용(캐시·TLB 오염) 측정
- 관련 레슨: OS-M02 (컨텍스트 스위치 비용, `demo_timer.c` 시계 해상도), OS-B01 (fork/exec), OS-B09 (mmap), OS-A09 (xv6 에 시스템 콜 추가), OS-A14 (Mach 메시지), LPERF-B03 (perf stat), LPERF-B07 (strace -c·-T vs perf trace), LLAT-A03 (io_uring), ELX-M09 (IIO 버퍼·watermark), ARCH-M10 (예외와 트랩), ARM-M09 (Cortex-M 예외 진입), ARM-M13 (AArch64 예외 레벨), SEC-M08 (MPU + SVC 게이트), VIRT-M02 (VM exit 비용), RTOS-A03 (DWT 사이클 측정)
