---
id: LPERF-M07
time: 90분
updated: 2026-11-08
---
# LPERF-M07 락 경합 프로파일링: futex 대기와 holder 찾기

## 학습 목표

- `sys_enter_futex` / `sys_exit_futex` 를 묶어 FUTEX_WAIT 대기 시간을 **락 주소(uaddr)·호출 스택·holder** 별로 집계하는 bpftrace 스크립트를 짜고 그 출력을 읽는다.
- `perf lock contention` 이 무엇을 보고 무엇을 **못 보는지**(이 VM 에서 `-b` 는 빌드에 없음, 트레이스포인트 모드는 커널 락만) 실측으로 안다.
- 대기자의 스택은 "피해자"이고 진짜 원인은 **holder** 라는 것을, `pthread_mutex_t.__owner` 와 `FUTEX_WAKE` 스택으로 숫자를 내어 보인다.
- 락 분할(striping)과 seqlock 읽기 경로로 고치고, 첫 번째 병목을 치우면 **두 번째 병목(stats_lock)이 드러나는** 과정을 같은 도구로 재측정한다.
- 정확성 CHECK(맵 내용, 찢어진 읽기 0)와 측정(p50/p99/대기 시간)을 분리하고, VM 에서는 비율만 믿는다.

## 1. 문제

펌웨어·검증 일을 하다 보면 "CPU 는 40 % 인데 왜 처리량이 안 오르지?"를 만난다 — 리눅스 게이트웨이의 로거·텔레메트리 서버, 멀티스레드 검증 프레임워크, SoC bring-up 테스트 데몬 모두 같다. 스레드가 **일하는 대신 락을 기다리고** 있다면 flamegraph 에는 거의 안 잡힌다(off-CPU 니까). 면접에서는 "lock contention 을 어떻게 찾고 어떻게 고치죠?"가 그대로 나온다.

이 레슨의 대상은 사용자 공간 뮤텍스다. `pthread_mutex_lock` 의 경합 경로는 결국 `futex(FUTEX_WAIT_PRIVATE)` 시스템콜로 잠든다 (구조는 OS-A01). 그래서 **futex 를 재면 락이 보인다**. 다만 도구마다 보는 층이 다르다 — 이 차이를 모르고 `perf lock` 만 돌리면 엉뚱한 곳을 고친다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">경합 한 번이 지나가는 층과, 각 도구가 보는 곳</text>
  <rect x="20" y="40" width="340" height="50" class="f-acc" stroke="currentColor"/>
  <text x="32" y="60" fill="currentColor">사용자 공간: pthread_mutex_lock(&amp;map_lock)</text>
  <text x="32" y="80" fill="currentColor" font-size="12">락 단어 CAS 실패 → 2 로 표시 → 시스템콜로</text>
  <path d="M190 90 L190 110" stroke="currentColor" marker-end="url(#a)"/>
  <rect x="20" y="110" width="340" height="50" class="f-warn" stroke="currentColor"/>
  <text x="32" y="130" fill="currentColor">syscall 경계: sys_enter_futex(uaddr, FUTEX_WAIT)</text>
  <text x="32" y="150" fill="currentColor" font-size="12">… 잠든다 …  sys_exit_futex(0 또는 -EAGAIN)</text>
  <path d="M190 160 L190 180" stroke="currentColor" marker-end="url(#a)"/>
  <rect x="20" y="180" width="340" height="50" class="f-mut" stroke="currentColor"/>
  <text x="32" y="200" fill="currentColor">커널 내부: futex 해시 버킷 spinlock</text>
  <text x="32" y="220" fill="currentColor" font-size="12">futex_q_lock / futex_wake 가 잠깐 잡는 진짜 커널 락</text>
  <g fill="currentColor" font-size="12">
    <text x="400" y="62">futex_wait.bt (이 레슨의 bpftrace)</text>
    <text x="400" y="80">→ uaddr = 사용자 락 주소, ustack, __owner</text>
    <text x="400" y="132">perf trace -s</text>
    <text x="400" y="150">→ 스레드별 futex 호출 수·총 시간 (락 구분 없음)</text>
    <text x="400" y="202">perf lock contention (-b 든 record 든)</text>
    <text x="400" y="220">→ 이 층만: 커널 락 (사용자 뮤텍스 주소 없음)</text>
  </g>
  <path d="M396 66 L364 66" stroke="currentColor"/><path d="M396 138 L364 138" stroke="currentColor"/><path d="M396 208 L364 208" stroke="currentColor"/>
  <text x="20" y="268" fill="currentColor" font-weight="bold">실측 (이 VM, 같은 서버 global 모드)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="290">bpftrace:  map_lock 8672 ms, stats_lock 1513 ms  (6 s 동안 사용자 락 대기)</text>
    <text x="20" y="310">perf lock: futex_wake 48.78 ms, futex_q_lock 22.90 ms  (3 s 동안 커널 spinlock)</text>
  </g>
  <defs><marker id="a" markerWidth="8" markerHeight="8" refX="6" refY="3" orient="auto"><path d="M0 0 L6 3 L0 6 z" fill="currentColor"/></marker></defs>
</svg>
```

**환경 (이 레슨의 모든 리눅스 숫자)**: Lima VM `elinux` (vz, aarch64, 6 vCPU) · `uname -r` 7.0.0-38-generic · gcc 15.2.0 · clang 21.1.8 · glibc 2.43 · perf 7.0.14 · bpftrace 0.25.0. 호스트 Apple M6. **VM load average 4.5–8.4** (다른 세션이 VM 을 같이 쓰는 중) — 그리고 vCPU 는 호스트에게 선점될 수 있다. 절대값이 아니라 **비율**만 믿을 것.

### Part A — 측정 결과를 읽는 코드

서버(`demo_server.c`)가 시작하며 찍는 `LOCK`/`TID` 표와 bpftrace 출력(`futex_wait.bt`)을 합쳐, "어느 락이 뜨겁고 누가 쥐고 있었나"를 코드로 계산한다. 고정 문자열로 검사하므로 결정적이다.

- `lat_bucket` / `lat_percentile` — 옥타브당 8 칸 지연 히스토그램, 분위수 (p99 는 permille 990).
- `bt_parse_line` — bpftrace 맵 한 줄 `@holder_ns[281472890175488, 2231]: 913455` 파싱 (막대·스택 줄은 거부).
- `reg_lookup` — 주소 → 락 등록부 항목 (배열이면 몇 번째 칸인지).
- `rank_locks` — `wait_ns` 를 락별로 합산해 내림차순. `top_holder` — 어느 역할의 스레드가 쥐고 있었나.

### Part B — 개선 1: 락 분할

전역 mutex 하나를 스트라이프 64 개로 쪼갠 `smap_*` 을 짠다.

- `stripe_of` — 해시의 **위쪽** 비트로 스트라이프 선택 (슬롯 번호가 아래쪽 비트를 쓰므로).
- `smap_put`, `smap_get_locked` — 스트라이프 하나만 잠금.
- `smap_scan` — 스트라이프를 하나씩 돌며 합산 (둘을 겹쳐 쥐지 않는다).
- `smap_put2` — 두 키를 원자적으로 쓰기: 같은 스트라이프면 한 번만, 다르면 번호가 작은 쪽부터 (OS-M10 순환 대기 제거).

### Part C — 개선 2: seqlock 읽기 경로

스트라이프마다 seq 를 두고 읽기는 잠그지 않는다 (LLAT-M02 의 패턴을 맵에 적용).

- `seq_write_begin` / `seq_write_end` — 쓰기 구간 표시.
- `smap_get_seq` — seq 검증으로 재시도, `SEQ_MAX_RETRY` 번 실패하면 락으로 떨어진다.
- 강제 스케줄 CHECK: 읽는 도중 쓰기가 끼어들어도 찢어진 값을 돌려주지 않는다. 측정은 `demo_server.c` 로 전후 비교.

## 2. Problem (English)

> You own a multi-threaded C server whose session map is guarded by one global `pthread_mutex_t`. Under load, throughput
> flattens and p99 latency explodes, yet CPU profiles look unremarkable. Find the hot lock and its holder, then fix it.
>
> **Tasks.** (1) Write a bpftrace script that times `FUTEX_WAIT` between `sys_enter_futex` and `sys_exit_futex` and
> aggregates wait time by `uaddr`, user stack and lock owner. Compare with `perf lock contention -b` — state what each tool
> can and cannot see, including what you do if `-b` is unavailable. (2) Implement a lock-striped map
> `smap_put/smap_get_locked/smap_scan`, and an atomic two-key write `smap_put2` that cannot deadlock.
> (3) Add a seqlock read path `bool smap_get_seq(smap_t *m, uint64_t key, val_t *out, unsigned *retries)` that takes no lock on
> the fast path, never returns a torn value, and has a bounded retry count before falling back to the lock.
> (4) Report before/after p50, p99 and total futex wait.
>
> **Constraints.** Hash table slots are `_Atomic uint64_t` (so racing lock-free reads are defined behaviour).
> `stripe_of` must not reuse the bits that pick the home slot. Correctness checks must not depend on timing.
>
> **Example.** `smap_get_seq` while a writer updates `key` after the reader copied word 1 of 4: it must retry and return
> the new value, with `*retries == 1` (never old words 0–1 + new words 2–3). Follow-ups: why is `perf lock contention` not
> enough for pthread mutexes? Why was a reader-writer lock *slower* than the mutex here? What does a striped design give up?

## 3. 시작 코드

스텁을 채우면 된다. 하네스는 고정 데이터와 "매 실행 항상 성립하는 것"(맵 내용, 개수, 찢어진 읽기 0)만 검사한다. 잘못된 답이 멈추지 않도록 `alarm(60)` watchdog 이 있다.

<!-- code: starter.c -->

```bash
# 리눅스(VM) 전용: 첫 줄이 /* ENV: linux */ 이다. Mac 홈은 VM 에서 읽기 전용이므로 복사해서 쓴다.
limactl shell elinux -- bash -lc 'mkdir -p /tmp/LPERF-M07_work && cp <lesson>/* /tmp/LPERF-M07_work/ && cd /tmp/LPERF-M07_work && \
  gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st'
```

처음 실행 결과 (실제 출력, exit 1, 51 FAIL):

```text
PASS  gmap_init(&gm, 8)
…
== A1/A2 지연 히스토그램 ==
PASS  lat_bucket(0) == 0
FAIL  lat_bucket(7) == 7  (line 289)
…
30/81 passed
```

실측용 파일 (리눅스 전용, 맵 이름은 solution.c 함수를 `#include` 로 재사용):

| 파일 | 용도 |
|---|---|
| `demo_server.c` | 전역/rwlock/striped/seq/fixed 다섯 모드의 서버. 시작 시 `LOCK`·`TID` 출력 |
| `futex_wait.bt` | futex 대기 집계 스크립트 |
| `run_trace.sh` | 서버 + bpftrace 묶음 실행 |
| `demo_analyze.c` | bpftrace 출력 + 등록부 → 순위표 (Part A 함수 사용) |

## 4. 힌트

<details><summary>힌트 1 — 어디서 시간을 재나</summary>

`sys_enter_futex` 에서 `tid` 별로 시작 시각과 uaddr 를 저장, `sys_exit_futex` 에서 차이를 재면 된다. `args.op & 127` 이 0 이면 FUTEX_WAIT 이다(128 은 `FUTEX_PRIVATE_FLAG`). 반환값 0 은 진짜로 잠들었다 깨어난 것, `-11` 은 잠들기 전에 값이 바뀐 것이다. 둘을 나눠야 "잠든 횟수"가 맞다.

</details>

<details><summary>힌트 2 — holder 는 어떻게 아나</summary>

대기자의 스택은 "누가 기다렸나"만 말한다. glibc 일반 뮤텍스는 `__owner` 에 잠근 스레드의 tid 를 적는다. 대기 시작 순간에 그 필드를 읽으면(`demo_server` 가 offset 을 출력한다: 8) 표본이 된다. 짧게 쥐는 락은 잠드는 사이 벌써 풀려서 0 이 많다. `FUTEX_WAKE` 를 부른 쪽의 스택은 "막 놓은 쪽"의 코드 위치다.

</details>

<details><summary>힌트 3 — stripe_of 와 home_slot 이 같은 비트를 쓰면</summary>

스트라이프 0 에 들어간 키는 모두 해시 하위 비트가 같아서, 스트라이프 내부 슬롯도 일부에만 몰린다. 충돌이 늘어 탐사가 길어지고, `distinct > 900` 테스트가 잡는다. 위쪽 비트(`>> 56`)를 쓴다.

</details>

<details><summary>힌트 4 — 왜 슬롯이 전부 atomic 인가</summary>

seqlock reader 는 writer 와 **동시에** 슬롯을 읽는다. 보통 변수면 C11 의 data race = UB 다. relaxed atomic 워드면 찢어져도 UB 가 아니고, seq 비교로 찢어진 값을 버린다. 왜 fence 가 그 자리에 있는지는 LLAT-M02 를 복습한다.

</details>

<details><summary>힌트 5 — smap_put2 의 자기 교착</summary>

두 키가 같은 스트라이프면 mutex 를 두 번 잠그게 된다. `lo == hi` 분기로 한 번만 잠근다. 번호가 다르면 작은 쪽을 먼저 — `smap_put2(1,2)` 와 `smap_put2(2,1)` 이 서로를 영원히 기다리는 것을 막는다.

</details>

## 5. 풀이

### Part A — 먼저 재기: 두 도구를 나란히

**도구 1: `perf lock contention -b`.** 시킨 대로 먼저 시도한다. 이 VM 의 perf 는 BPF 스켈레톤 없이 빌드되어 있다.

```text
$ sudo perf lock contention -b -a -- sleep 0.1
 Error: switch `b' is not available because no BUILD_BPF_SKEL=1
 Usage: perf lock contention [<options>]

    -b, --use-bpf         use BPF program to collect lock contention stats
                          (not built-in because no BUILD_BPF_SKEL=1)
```

`perf version --build-options` 에도 `bpf_skeletons: [ OFF ]` 로 나온다. `-b` 없는 `perf lock contention -a -- sleep 1` 은 실시간 측정이 아니라 현재 디렉터리의 `perf.data` 를 읽어 보고만 한다(파일이 있을 때 측정 없이 그 내용이 그대로 나왔다). 그래서 쓸 수 있는 경로는 **`perf lock record` → `perf lock contention -i perf.data`** 다. (주의: 이 VM 에서 `perf lock record … | tail` 처럼 stdout 이 파이프면 perf.data 가 터미널로 흘러나왔다 → 파일로 돌려라. `perf lock contention` 도 stdin 이 파이프면 `incompatible file format` 으로 실패했다.)

```text
$ sudo perf lock record -a -- sleep 3 > lr.log 2>&1    # (server global 5 실행 중)
$ sudo perf lock contention -i perf.data -E 5
 contended   total wait     max wait     avg wait         type   caller

     24299     48.78 ms      5.46 ms      2.01 us     spinlock   futex_wake+0x104
      9770     22.90 ms      5.46 ms      2.34 us     spinlock   futex_q_lock+0x38
         4      7.14 ms      7.11 ms      1.79 ms     spinlock   raw_spin_rq_lock_nested+0x38
        83    715.37 us     57.50 us      8.62 us     spinlock   free_pcppages_bulk+0x44
        36    595.79 us    125.62 us     16.55 us     spinlock   raw_spin_rq_lock_nested+0x38
$ sudo perf lock contention -i perf.data -l -E 3
 contended   total wait     max wait     avg wait            address   symbol

     24715     36.09 ms    670.00 us      1.46 us   ffff0000cb1da144    (spinlock)
      9350     35.59 ms      5.46 ms      3.81 us   ffff0000cb1da684    (spinlock)
         6      7.13 ms      7.11 ms      1.19 ms   ffff00032e409288    (spinlock)
```

중요한 관찰: **여기에는 우리 뮤텍스가 없다.** `lock:contention_begin/end` 는 커널 락 경로의 트레이스포인트이고(이 VM 의 `perf script` 스택: `queued_spin_lock_slowpath ← _raw_spin_lock ← futex_wake`), 찍힌 것은 커널 `ffff0000…` 주소다. 위 두 spinlock 은 사용자 뮤텍스 두 개가 걸린 **futex 해시 버킷의 spinlock** 으로 보인다 (주소를 버킷에 직접 대응시키진 않았다, 추정). 합계도 70 ms 대로, 사용자 수준 대기와 자릿수가 다르다. fixed 모드로 같은 측정을 하면 futex 항목이 통째로 사라진다:

```text
$ sudo perf lock contention -i perf.data -E 4        # (server fixed 5 실행 중)
 contended   total wait     max wait     avg wait         type   caller

         8     30.04 us      5.08 us      3.76 us     spinlock   rcu_report_qs_rdp+0x40
         3     21.25 us     12.75 us      7.08 us     spinlock   raw_spin_rq_lock_nested+0x38
         3     19.54 us      9.71 us      6.51 us        mutex   pipe_double_lock+0x68
         1     15.17 us     15.17 us     15.17 us     spinlock   raw_spin_rq_lock_nested+0x38
```

**도구 2: `futex_wait.bt`.** 사용자 락 주소를 직접 본다.

<!-- code: futex_wait.bt -->

이 서버의 뮤텍스는 `FUTEX_WAIT` 만 쓴다. `pthread_cond_wait`·`pthread_mutex_timedlock` 쪽은 `FUTEX_WAIT_BITSET`(cmd 9) 계열이므로 (OS-A01 참고, 이 레슨 미측정) 그런 코드를 볼 때는 조건을 `$cmd == 0 || $cmd == 9` 로 넓힌다. `ustack(6)` 는 6 프레임까지만 잡는다: 더 깊은 호출 경로가 필요하면 늘린다.

```bash
gcc -std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -pthread demo_server.c -o server
gcc -std=c11 -O2 -Wall -Wextra -Werror -pthread demo_analyze.c -o analyze
./run_trace.sh global 5 6 > tr_global.txt && ./analyze tr_global.txt
```

서버가 찍는 표 + bpftrace 맵 (주소는 실행마다 다르다. 실제 출력에서 발췌, "…" 로 생략):

```text
pid 38397 mode global workers 5 secs 9
mutex __owner offset 8
LOCK map_lock 206939744633096 48 1
LOCK stats_lock 206939744633232 48 1
LOCK log_lock 206939744637320 48 1
TID worker 38399
…
TID flusher 38404
…
@by_stack[
        __lll_lock_wait+84
        pthread_mutex_lock@@GLIBC_2.17+248
        map_get+96
        worker+236
        start_thread+1000
        thread_start+12
]: 7883164644
@holder_ns[206939744633096, 38404]: 5308205835
@holder_ns[206939744633096, 0]: 1670387465
@holder_ns[206939744633096, 38402]: 412344584
…
@not_slept[206939744633096, -11]: 1306918
@not_slept[206939744633232, -11]: 655317
@wait_cnt[206939744633096]: 27540
@wait_ns[206939744633096]: 8672011227
@wait_us[206939744633096]:
…
[4, 8)              4530 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[8, 16)             3149 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                |
[16, 32)            2599 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                       |
[32, 64)            3018 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                  |
[64, 128)           3501 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@            |
[128, 256)          3793 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@         |
[256, 512)          2731 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                     |
…
```

이것을 `analyze` 가 합친다 (Part A 함수의 실제 출력):

```text
rows=23  total futex-wait = 10185.8 ms  in 34015 sleeps
lock                wait_ms    share   top holder
map_lock             8672.0     85.1% flusher (61.2 %)
stats_lock           1512.6     14.8% (owner=0) (59.7 %)
log_lock                1.1      0.0% worker (100.0 %)
```

읽는 법:

| 질문 | 어디서 | 이번 답 |
|---|---|---|
| 얼마나 기다렸나 | `@wait_ns` 합 | 6 초에 총 10.2 초 (5 worker = 30 스레드-초 → 대략 1/3 대기) |
| 어떤 락인가 | uaddr → `LOCK` 표 | `map_lock` 85.1 % |
| 어디서 기다렸나 | `@by_stack` | `map_get` 7.9 초, `stats_add` 1.5 초, `gmap_put` 0.75 초 |
| 얼마나 자주 "못 잠들었나" | `@not_slept` | map_lock 에서 EAGAIN 1,306,918 번 vs 진짜 잠 27,540 번 (FUTEX_WAIT 호출의 대부분은 곧바로 돌아온다) |
| **누가 쥐고 있었나** | `@holder_ns` | `flusher` 61.2 % |

마지막 줄이 이 레슨의 핵심이다. 스택만 보면 "map_get 이 범인"처럼 보인다. 하지만 map_get 은 **기다린 쪽**이다. 스택은 피해자의 위치를 말하고, 원인은 그 시간 동안 쥐고 있던 쪽이다.

```svg
<svg viewBox="0 0 740 216" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">스택은 피해자, holder 는 원인: flusher 한 번의 순회가 worker 들을 줄 세운다</text>
  <text x="10" y="48" fill="currentColor" font-size="12">flusher</text>
  <rect x="80" y="36" width="280" height="20" class="f-warn" stroke="currentColor"/>
  <text x="90" y="51" fill="currentColor" font-size="12">gmap_scan: 맵 전체를 쥔 채 순회 (45–160 µs)</text>
  <g font-size="12" fill="currentColor">
    <text x="10" y="80">worker 1</text><rect x="100" y="68" width="260" height="20" class="f-mut" stroke="currentColor"/><text x="110" y="83">map_get → FUTEX_WAIT (잠듦)</text>
    <rect x="360" y="68" width="30" height="20" class="f-ok" stroke="currentColor"/>
    <text x="10" y="112">worker 2</text><rect x="130" y="100" width="260" height="20" class="f-mut" stroke="currentColor"/><text x="140" y="115">gmap_put → FUTEX_WAIT</text>
    <rect x="390" y="100" width="24" height="20" class="f-ok" stroke="currentColor"/>
    <text x="10" y="144">worker 3</text><rect x="180" y="132" width="234" height="20" class="f-mut" stroke="currentColor"/><text x="190" y="147">map_get → FUTEX_WAIT</text>
    <rect x="414" y="132" width="24" height="20" class="f-ok" stroke="currentColor"/>
    <text x="10" y="178">@by_stack 은 회색 칸을 map_get/gmap_put 으로 합산한다.</text>
    <text x="10" y="198">@holder_ns 는 같은 회색 칸을 "flusher 가 쥔 동안" 으로 합산한다.</text>
  </g>
</svg>
```

**holder 표본의 한계.** `__owner` 는 대기자가 **잠드는 순간** 읽은 것이다 (이 VM 에서 잠근 뒤 `__owner == gettid()`, 푼 뒤 0 확인. glibc 내부 필드이며 이식 가능한 인터페이스가 아니다). 0 이 나오는 경우가 많은 stats_lock(카운터 세 개만 올리고 바로 놓는 락) 처럼 **짧게 쥐는 락은 표본이 맞지 않는다** — 잠들기까지 몇 µs 사이에 벌써 풀린다. 오래 쥘수록 잘 잡히는 이 편향은 우리가 원하는 방향이다: 범인을 찾을 때는 오래 쥐는 쪽이 궁금하니까. `@waker` (FUTEX_WAKE 를 부른 쪽의 스택) 는 코드 위치를 보여 준다:

```text
@waker[
        __lll_lock_wake+40
        __pthread_mutex_unlock_usercnt+148
        flusher+116
        start_thread+1000
        thread_start+12
, flusher]: 1998
@waker[
        __lll_lock_wake+40
        __pthread_mutex_unlock_usercnt+148
        map_get+168
        worker+236
        start_thread+1000
, worker]: 2408118
```

map_get 쪽 wake 만 240 만 번인데 진짜 잠든 스레드는 수만 번이다: 단어 값이 2 로 남아 있으면 unlock 이 빈 `FUTEX_WAKE` 를 부른다 (OS-A01 의 헛 wake).

**도구 3: `perf trace -s`.** 같은 시간대에 스레드별 시스템콜 요약을 보는 교차 검증이다. worker 하나가 2 초에 futex 87,969 번(그중 EAGAIN 등 에러 25,637), 840 ms — 대략 2 초의 40 % 를 futex 안에서 보냈다:

```text
 worker (40015), 175933 events, 17.9%
   syscall            calls  errors  total       min       avg       max       stddev
                                     (msec)    (msec)    (msec)    (msec)        (%)
   --------------- --------  ------ -------- --------- --------- ---------     ------
   futex              87969  25637   840.410     0.000     0.010    31.707      4.60%
```

세 도구 정리:

| | futex_wait.bt | perf lock contention | perf trace -s |
|---|---|---|---|
| 사용자 락 주소 | **예** (uaddr) | 아니요 (커널 락만) | 아니요 |
| 코드 위치 | ustack | 커널 caller | 없음 (스레드 단위) |
| holder 추정 | `__owner` / FUTEX_WAKE | `-o` 옵션은 있으나 커널 락 대상 (안 돌림) | 없음 |
| 이 VM 에서 | 동작 | `-b` 불가 → `record` + `-i` | 동작 |
| 무엇을 답하나 | "어느 mutex 가, 누구 때문에" | "커널 안 락 경합" (futex 구현 비용, 런큐 등) | "시스템콜에 얼마나" |

### Part B — 고치기: 먼저 rwlock 은 왜 안 되나

흔한 첫 시도는 `pthread_rwlock_t` 다. 읽기 90 % 니까 될 것 같다. 실측(§5 끝 표)은 **전역 mutex 보다 나쁘다**: 3152 → 1487 kreq/s, p99.9 는 98 µs → 360 µs. 읽기 구간이 짧으면 reader 도 공유 카운터를 원자적으로 고치느라 캐시 라인이 튕긴다. 게다가 flusher 의 순회가 read 잠금을 오래 쥐니 writer 와 대기열이 더 엉킨다. (rwlock 내부 큐 동작까지 분석하진 않았다.) **"읽기 위주니까 rwlock" 은 측정 없이는 믿지 말 것.**

스트라이프:

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">전역 mutex → 스트라이프 64 개 + seq (flusher 는 칸 하나씩)</text>
  <rect x="20" y="36" width="210" height="60" class="f-warn" stroke="currentColor"/>
  <text x="30" y="58" fill="currentColor">gmap: lock 1 개</text>
  <text x="30" y="80" fill="currentColor" font-size="12">scan 동안 모두 대기</text>
  <path d="M238 66 L290 66" stroke="currentColor" marker-end="url(#b)"/>
  <g stroke="currentColor">
    <rect x="300" y="36" width="60" height="28" class="f-ok"/><rect x="364" y="36" width="60" height="28" class="f-ok"/>
    <rect x="428" y="36" width="60" height="28" class="f-mut"/><rect x="492" y="36" width="60" height="28" class="f-ok"/>
    <rect x="556" y="36" width="60" height="28" class="f-ok"/><rect x="620" y="36" width="60" height="28" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="310" y="54">s[0]</text><text x="374" y="54">s[1]</text><text x="438" y="54">s[2]</text><text x="502" y="54">s[3]</text><text x="566" y="54">…</text><text x="630" y="54">s[63]</text>
    <text x="300" y="82">회색 칸 = flusher 가 쥔 칸. 나머지 칸은 영향 없음.</text>
  </g>
  <text x="20" y="124" fill="currentColor" font-weight="bold">해시 비트의 역할 분담</text>
  <rect x="20" y="138" width="130" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="150" y="138" width="430" height="28" class="f-mut" stroke="currentColor"/>
  <rect x="580" y="138" width="130" height="28" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="30" y="157">63..56</text><text x="340" y="157">(key * phi 의 비트들)</text><text x="590" y="157">home_slot: 하위</text>
    <text x="20" y="188">위쪽 비트로 스트라이프를 고르고, 스트라이프 안에서 슬롯은 아래쪽 비트로 고른다.</text>
    <text x="20" y="208">같은 비트를 쓰면 스트라이프 0 의 키는 모두 슬롯 0 근처로 몰린다 → 탐사 길어짐 → hold 길어짐.</text>
    <text x="20" y="234" font-weight="bold">reader: seq 만 읽는다 (쓰지 않는다) → 캐시 라인이 reader 끼리 튕기지 않는다</text>
  </g>
  <defs><marker id="b" markerWidth="8" markerHeight="8" refX="6" refY="3" orient="auto"><path d="M0 0 L6 3 L0 6 z" fill="currentColor"/></marker></defs>
</svg>
```

스트라이프가 지켜야 할 것:

- 한 번에 스트라이프 하나 (`smap_scan` 은 앞 스트라이프를 푼 뒤 다음을 잡는다). 대가: scan 의 합계는 한순간의 스냅샷이 아니다. 전역 일관성이 필요한 작업은 설계를 바꿔야 한다.
- 스트라이프 구조체를 128 B 로 정렬 (ARCH-M08: M6 는 128 B 단위). 안 그러면 옆 스트라이프의 mutex 와 캐시 라인을 공유해 false sharing 이 된다.
- 다중 키 연산은 번호 오름차순 잠금 (`smap_put2`).

**재측정 — 그런데 거의 안 빨라졌다.** 스트라이프 후 throughput 은 3152 → 3935 kreq/s 뿐이다. 왜? 같은 스크립트를 다시 돌린다 (이것이 이 레슨의 진짜 교훈이다):

```text
rows=526  total futex-wait = 7091.9 ms  in 18724 sleeps
lock                wait_ms    share   top holder
stats_lock           3618.9     51.0% (owner=0) (61.6 %)
stripe               3472.6     48.9% flusher (78.7 %)
log_lock                0.4      0.0% worker (100.0 %)
```

**맵 락을 치웠더니 stats_lock 이 1 위로 올라왔다.** 모든 요청이 전역 stats mutex 를 거치므로 (한 요청당 한 번) 스트라이프를 아무리 쪼개도 직렬 구간이 그대로 남는다 (Amdahl). 프로파일러 없이 "맵을 고쳤으니 끝" 이라고 했으면 놓쳤을 것이다. 대기 시간은 스레드 수 × 시간에 묶이므로, 한 곳을 치우면 다른 곳이 그 대기 몫을 이어받는다.

### Part C — 읽기 경로와 stats 고치기

`seq` 모드(스트라이프 + seqlock 읽기, stats 는 그대로)는 3935 → 3927 kreq/s: **전혀 안 는다**. stats_lock 이 그대로이기 때문이다. 프로파일이 지목하지 않은 곳을 고쳐 봐야 소용없다는 실증이다. `fixed` 모드는 stats 를 스레드 지역 카운터로 바꾼다(lock 없이, 합계는 끝에 모은다).

`smap_get_seq` 의 구조는 LLAT-M02 와 같다. writer 는 stripe 락을 쥐고 `seq_write_begin`/`end` 로 감싸므로 stripe 당 writer 는 하나다(seqlock 의 전제). reader 는 seq(acquire) → 슬롯 복사(relaxed) → acquire fence → seq(relaxed) 재확인. 재시도 상한을 넘으면 락으로 내려간다:

| 경로 | 동작 | 실측 (fixed, 54,977,016 요청) |
|---|---|---|
| 빠른 경로 | 락 없음, 공유 메모리에 쓰기 없음 | 대부분 |
| 재시도 | seq 불일치, 다시 읽기 | seq_retries 1,947,925 (요청 대비 약 3.5 %) |
| 락 fallback | 64 번 실패 시 mutex | 2,089 번 (약 0.004 %) |

(GCC 의 TSan 은 `atomic_thread_fence` 를 지원하지 않는다는 `-Wtsan` 경고를 낸다. 슬롯이 전부 atomic 이라 TSan 이 race 를 보고할 데이터는 없다 — fence 자체의 정당성은 TSan 이 아니라 LLAT-M02 의 논증과 강제 스케줄 CHECK 로 확인한다. VM 의 gcc TSan 도 이번엔 실행되었고 `81/81 passed`, clang TSan 도 깨끗했다.)

재시도가 작은 이유: writer 구간은 슬롯 4 워드 쓰기만큼 짧다. vCPU 가 쓰기 중간에 호스트에게 선점되면 reader 가 64 번을 다 태우고 fallback 하는데, 이것이 위 fallback 수치에 섞인 원인으로 추정된다(분리 측정 안 함).

**전후 비교 — 측정 (처리량·지연은 trace 없이, 4 초 × 5 회 중앙값)**

load average: 측정 전 4.5, 후 7.8.

| mode | kreq/s (최소–최대) | p50 | p99 (최소–최대) | p99.9 |
|---|---|---|---|---|
| global | 3152 (3119–3418) | 255 ns | 2303 (1151–12287) | 98303 |
| rwlock | 1487 (1347–2127) | 223 ns | 24575 (1151–61439) | 360447 |
| striped | 3935 (2172–4118) | 223 ns | 2047 (895–9215) | 65535 |
| seq | 3927 (2158–4120) | 223 ns | 6143 (767–11263) | 40959 |
| **fixed** | **8343** (2961–10486) | **43 ns** | **223** (223–319) | **511** |

히스토그램 칸의 상한이므로 12.5 % 오차가 있다. p99 는 VM 부하에 따라 들쭉날쭉하다(global p99 가 1151~12287 ns!). p99.9 와 처리량 중앙값이 비교에 더 안정적이다.

**대기 시간 — bpftrace 6 초 trace** (서버 5 worker, 위 표와 다른 실행):

| mode | futex wait 합계 | 잠든 횟수 | 1 위 락 |
|---|---|---|---|
| global | 10185.8 ms | 34,015 | map_lock 85.1 %, holder flusher 61.2 % |
| striped | 7091.9 ms | 18,724 | stats_lock 51.0 %, stripe 48.9 % |
| fixed | 1682.6 ms | 930 | stripe 99.2 %, holder flusher 90.0 % |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold">futex 대기 (ms, 6 s trace, 실측) — 1 위 락이 바뀌는 것을 보라</text>
  <g fill="currentColor">
    <text x="10" y="50">global</text>
    <text x="10" y="120">striped</text>
    <text x="10" y="190">fixed</text>
  </g>
  <rect x="90" y="40" width="400" height="16" class="f-warn" stroke="currentColor"/>
  <rect x="90" y="58" width="70" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="90" y="110" width="167" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="90" y="128" width="160" height="16" class="f-mut" stroke="currentColor"/>
  <rect x="90" y="180" width="77" height="16" class="f-mut" stroke="currentColor"/>
  <rect x="90" y="198" width="1" height="16" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="498" y="53">map_lock 8672 (flusher 61 %)</text>
    <text x="168" y="71">stats_lock 1513</text>
    <text x="264" y="123">stats_lock 3619 ← 맵을 고치자 1 위</text>
    <text x="258" y="141">stripe 3473 (holder flusher 79 %)</text>
    <text x="174" y="193">stripe 1670 (쓰기만 flusher 를 기다림)</text>
    <text x="100" y="211">log_lock 13</text>
    <text x="10" y="238">fixed 는 잠든 횟수도 34,015 → 930. 남은 대기는 worker 쓰기가 flusher 의 스트라이프를 기다리는 시간.</text>
  </g>
</svg>
```

요약: 처리량 약 2.6 배, p50 약 1/6, p99.9 약 1/190 (98 µs → 0.5 µs), 잠든 횟수 약 1/37. 남은 대기는 flusher 가 쓰기를 막는 시간이므로, 다음 단계는 flusher 도 seq 로 읽거나 스트라이프 사이에 양보(yield)하게 만드는 것이다 (이 레슨에서는 하지 않음).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

검증 (VM, 실제 실행):

```text
$ gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so    → 81/81 passed (6 회 연속)
$ clang -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so  → 81/81 passed
$ gcc/clang -fsanitize=address,undefined -g -O1 solution.c                     → 81/81 passed, 보고 없음
$ clang -fsanitize=thread -g -O1 solution.c                                    → 81/81 passed, 보고 없음
$ gcc   -fsanitize=thread -g -O1 solution.c                                    → 81/81 passed, 보고 없음 (-Wtsan 경고만)
```

## 7. 자주 하는 실수

1. **`perf lock contention` 결과만 보고 뮤텍스를 고친다.** 이 도구는 커널 락을 본다 (실측: `futex_wake` / `futex_q_lock`). 사용자 mutex 경합은 futex 의 "그림자"로만 나타난다. → FUTEX_WAIT 를 uaddr 별로 재거나, 자기 코드에 계측(연습 1)을 넣는다.
2. **대기자의 스택을 범인으로 지목한다.** `map_get` 은 기다린 쪽이다. 고쳐야 할 것은 긴 hold 다 — 여기서는 전체 순회. → `@holder_ns` 와 `@waker` 를 함께 본다.
3. **ustack 심볼이 숫자로 나온다.** bpftrace 는 출력 시점에 심볼을 푼다. 프로세스가 죽은 뒤라면 못 푼다. → trace 보다 서버를 오래 살린다(`run_trace.sh` 가 +3 초). 인라인된 함수는 스택에 안 남는다 → `noinline`, `-fno-omit-frame-pointer`.
   ```c
   __attribute__((noinline)) static bool map_get(uint64_t k, val_t *o, worker_t *w);  /* 스택에 이름이 남는다 */
   ```
4. **`@start[tid]` 로 재다가 짧은 값이 섞인다.** WAIT 의 exit 이벤트를 놓치면(실제로 이 VM 에서 `__lll_lock_wake` 스택이 대기 시간표에 작은 값으로 몇 줄 섞여 나왔다) `FUTEX_WAKE` 의 exit 이 낡은 시작 시각으로 계산된다. → enter 에서 WAIT 이 아니면 `@start[tid] = 0` 으로 지운다. (exit 을 놓친 정확한 원인은 분석 안 함, BPF 재진입 스킵으로 추정.)
5. **rwlock 이면 읽기는 공짜라고 생각한다.** 읽기 잠금은 공유 카운터 쓰기다. 짧은 읽기에서 전역 mutex 보다 나빴다 (1487 vs 3152 kreq/s). → 측정.
6. **스트라이프만 하고 재측정을 안 한다.** 첫 병목을 지우면 두 번째가 그 대기를 이어받는다. stats_lock 이 그랬다. → 고칠 때마다 같은 스크립트를 다시 돌린다.
7. **stripe_of 와 slot 에 같은 해시 비트.** 스트라이프 내 슬롯이 일부에 몰려 hold 가 길어진다. (B1 의 `distinct > 900` 체크가 잡는다.)
8. **seqlock reader 가 보통 포인터를 따라간다.** 복사하는 동안 그 포인터가 해제되면 use-after-free 다. 이 레슨은 슬롯이 고정 배열이고 워드가 atomic 이라 안전하다. 연결 리스트 맵이면 RCU/EBR(OS-A03)이 필요하다.

## 8. 면접 Q&A

<details><summary>Q1. perf lock contention 이 있는데 왜 futex 를 직접 재나요?</summary>

**답.** `perf lock contention` 은 `lock:contention_begin/end` 를 쓰는데, 이건 커널 락 경로에 있다. 사용자 뮤텍스 경합은 "futex 시스템콜의 커널 내부 비용"(futex 해시 버킷 spinlock 등)으로만 보인다. 이 VM 실측에서 perf 는 합계 70 ms 대 `futex_wake`/`futex_q_lock` 만 보였고, bpftrace 는 사용자 락 대기 8.7 초를 `map_lock` 주소로 잡았다.
**꼬리: 그러면 perf lock 은 쓸모없나요?** 아니다. 커널 락 자체가 문제일 때(mmap_lock, rq lock, 파일시스템) 답을 준다. 층이 다를 뿐이다. 이 VM 에서는 `-b` 가 빌드 옵션(`BUILD_BPF_SKEL`)으로 빠져 있어서 `perf lock record` + `-i` 로 돌렸다.
**꼬리: 사용자 락에 다른 방법은?** uprobe 로 `pthread_mutex_lock` 진입/반환을 재거나(오버헤드 큼, 이 레슨에서 미측정), 코드 안에 측정 래퍼를 넣는다(연습 1). off-CPU 프로파일은 "어디서 잠들었나" 를 주고 락 주소는 안 준다.

</details>

<details><summary>Q2. FUTEX_WAIT 대기 시간은 곧 락을 쥔 시간인가요?</summary>

**답.** 아니다. 대기 시간 = 앞사람들의 hold + 깨어난 뒤 CPU 를 받기까지의 **런큐 대기**(+ VM 이면 호스트 선점). 그리고 sleep 하기 직전에 lock 값이 바뀌면 `EAGAIN` 으로 즉시 돌아온다. 실측에서 map_lock 은 EAGAIN 이 130만 번, 실제 잠든 것은 2.7만 번이었다.
**꼬리: 왜 EAGAIN 이 그렇게 많죠?** 락이 자주 바뀌어서 `futex(WAIT, 2)` 의 커널 내 값 비교가 실패한다. 재시도 비용은 시스템콜 왕복. hist 로 보면 잠든 쪽만 나오니 `@not_slept` 를 같이 봐야 "syscall 폭주" 를 알아챈다.
**꼬리: 스핀을 추가하면?** `PTHREAD_MUTEX_ADAPTIVE_NP` 는 짧은 시간 스핀해서 일부 futex 를 줄일 수 있으나 hold 가 길면 소용없다 (이 레슨 미측정). 근본은 경합을 줄이는 것.

</details>

<details><summary>Q3. 락을 쥔 스레드를 어떻게 찾나요?</summary>

**답.** 세 가지다. ① glibc 일반 뮤텍스의 `__data.__owner` 를 대기 순간 읽는다(이 레슨, 잠드는 순간의 표본). ② `FUTEX_WAKE` 호출자의 스택: 막 unlock 한 쪽의 코드 위치. ③ 애플리케이션 계측: 잠금 시각·owner 를 자기 래퍼에 기록(연습 1, 임베디드에선 `xSemaphoreGetMutexHolder` + DWT 사이클 카운터).
**꼬리: PTHREAD_PRIO_INHERIT 뮤텍스면?** 락 단어가 owner TID 자체가 되어(`FUTEX_LOCK_PI`) 단어만 읽어도 owner 를 안다 (OS-A01 데모에서 확인).
**꼬리: 표본의 약점은?** 0 이나 낡은 TID 가 나올 수 있고, 짧게 쥐는 락은 잠드는 사이 다 풀려 버린다 (stats_lock 의 owner=0 비중 약 60 %). 그래도 오래 쥐는 쪽을 찾는 목적에는 잘 맞는다.

</details>

<details><summary>Q4. 락 분할의 함정은?</summary>

**답.** (1) 전역 불변식이 쪼개진다: scan 합계는 한순간 스냅샷이 아니다. (2) 키 쏠림: 뜨거운 키 하나면 그 스트라이프는 여전히 전역 락이다 (연습 2 로 판정). (3) 다중 키 연산은 번호 순서로 잠가야 교착이 없고, 같은 스트라이프면 자기 교착을 피해 한 번만 잠근다. (4) false sharing: 스트라이프 구조체 사이를 128 B 로 띄워야 한다 (ARCH-M08).
**꼬리: 몇 개로 쪼개나요?** 스레드 수보다 넉넉히(스레드의 수 배), 그리고 2 의 거듭제곱. 많을수록 scan 비용과 메모리가 늘어난다.

</details>

<details><summary>Q5. 왜 rwlock 이 mutex 보다 느렸나요?</summary>

**답.** 임계 구역이 짧으면 reader 도 공유 상태(reader 수)를 원자적으로 고쳐야 한다. 캐시 라인이 reader 끼리 튕기고, 스케줄링 비용이 더해져 임계 구역이 긴 경우에만 rwlock 이 이긴다 (이 레슨 실측 1487 vs 3152 kreq/s, p99.9 360 µs vs 98 µs — glibc rwlock 내부까지 분석은 안 했다). 또 긴 reader(flusher)는 writer 를 굶겨 tail 을 키운다.
**꼬리: 그럼 언제 rwlock?** 읽기 구간이 µs 이상으로 길고, 쓰기가 드물 때. 짧은 읽기엔 seqlock/RCU 가 맞다: reader 가 공유 메모리에 쓰지 않는다.

</details>

<details><summary>Q6. seqlock 과 RCU 중 무엇을 고르나요?</summary>

**답.** seqlock: 고정 크기 POD 를 복사해 읽을 때, reader 는 재시도, writer 는 막지 않음. RCU: 포인터로 더 큰 구조(리스트, 설정 테이블)를 읽을 때, reader 는 멈추지 않지만 writer 는 복사-후-교체 + 유예 기간(grace period)이 필요하다. 이 레슨의 슬롯은 고정 배열·고정 크기라 seqlock 이 맞다. 연습 4 의 설정 테이블은 RCU 스타일이 맞다 (OS-A03).
**꼬리: seqlock 을 일반 포인터 구조에 쓰면?** reader 가 읽는 중 해제될 수 있어 위험하다 (포인터를 따라가는 시점에 UAF).
**꼬리: writer 가 계속 쓰면?** reader 가 굶는다. 그래서 재시도 상한 + 락 fallback 을 뒀다.

</details>

<details><summary>Q7. 이 측정, 믿어도 되나요?</summary>

**답.** 비율만. VM 의 vCPU 는 호스트 스케줄러에게 선점될 수 있고(p99 범위가 1151~12287 ns 까지 벌어졌다), load average 도 4.5–8.4 로 다른 작업이 섞여 있었다. 추적기 자체도 영향을 준다: 위 trace 실행에서 처리량이 낮게 나왔다(global 2417 kreq/s vs 무추적 중앙값 3152). 그래서 ① 같은 조건에서 전후를 번갈아 재고, ② trace 숫자는 "어디가 뜨겁나" 용, 처리량/지연은 trace 없는 실행으로 낸다. Closed-loop(응답 후 다음 요청) 측정은 coordinated omission 으로 긴 지연을 덜 세므로, 절대 p99 가 필요하면 open-loop 로 측정해야 한다 (이 레슨 미측정).

</details>

<details><summary>Q8. 펌웨어(RTOS)에서 같은 문제를 어떻게 다루죠?</summary>

**답.** bpftrace 가 없으니 계측을 코드에 넣는다: 뮤텍스 래퍼에서 trylock 실패 횟수·대기·보유 시간을 DWT CYCCNT 로 재고 (연습 1), `xSemaphoreGetMutexHolder()` 로 holder 를 본다 (FreeRTOS, `INCLUDE_xSemaphoreGetMutexHolder` 설정 필요 — 이 레슨에서 실행 확인은 안 함). 근본 해결도 같다: 임계 구역 줄이기, ISR 은 lock-free 큐로, 전역 stats 는 per-CPU/태스크 카운터. priority inversion 은 RTOS-M03/M04.

</details>

## 9. 추가 연습

1. **traced_lock**: `pthread_mutex_trylock` 실패로 경합을 세고, 시계를 주입해 대기·보유 시간을 잰다. 힌트: 통계는 락을 쥔 뒤에 고친다.

   <details><summary>답</summary>

   `t0 = clock(); c = trylock != 0; if (c) lock; t1 = clock(); acquires++; if (c) contended++; wait += t1-t0; lock_t0 = t1;` / unlock 전에 `hold = clock() - lock_t0`. 시계를 주입하니 테스트가 결정적이다. 통계 필드는 락을 쥔 동안만 고치므로 추가 동기화가 필요 없다.

   </details>

2. **stripe_skew**: 스트라이프별 대기 횟수에서 max/mean 을 permille 로 계산, 뜨거운 스트라이프를 찾는다. 실측 `@wait_cnt` 고정 데이터로 검사한다. 2 배 미만이면 해시 분포는 괜찮은 것이다.

   <details><summary>답</summary>

   `mx * n * 1000 / sum`, sum 이 0 이면 0. 이번 실측 10 칸 샘플은 1523 ‰ (평균의 약 1.5 배) 로 쏠림이 없었다. 값이 크게 나오면 키 분포(핫 키)를 의심한다 — 분할로 안 풀리는 경우다.

   </details>

3. **smap_get2**: 두 키를 "같은 순간의 쌍"으로 읽기. 두 스트라이프의 seq 를 모두 검증하고, 실패가 쌓이면 번호 순으로 두 락을 잡는다.

   <details><summary>답</summary>

   `s1, s2` 를 acquire 로 읽고, 둘 다 짝수인지 확인 → 두 값을 복사 → acquire fence → 둘 다 그대로인지 재확인. 한쪽만 검증하면 쓰기가 끼어들어 "한쪽은 새 값, 한쪽은 옛 값"이 나온다 — 강제 스케줄 CHECK 와 stress 의 `x.w[0] == y.w[0]` 가 잡는다.

   </details>

4. **RCU 스타일 설정 테이블**: 작은 테이블을 복사-후-교체로 갱신하고, QSBR 유예 기간이 지난 옛 버전만 해제한다 (OS-A03 복습).

   <details><summary>답</summary>

   reader: `cur` acquire 로드 후 그 스냅샷만 사용, 끝나면 `seen[id] = gen`. writer: `exchange(cur, new)`, `old->retired_at = ++gen` 후 retired 목록에 넣는다. reclaim: 모든 reader 의 `seen ≥ retired_at` 이면 free. 이 레슨의 맵은 슬롯 복사라 seqlock 이 맞았지만, 포인터로 큰 구조를 바꿀 때는 이쪽이다 (전역 설정, 라우팅 테이블).

   </details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
# VM 안 /tmp/LPERF-M07_work 에서 (practice.c 는 solution.c 를 include 한다)
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr   # 29/29 passed
```

```text
== 연습 1: traced_lock ==
PASS  t.acquires == 1 && t.contended == 0
…
== 연습 4: RCU 스타일 설정 테이블 ==
…
PASS  atomic_load(&r2.cur)->version == 3000
29/29 passed
```

## 10. 핵심 정리

- 사용자 락 경합은 `futex(FUTEX_WAIT)` 로 재자: enter/exit 를 tid 로 묶고 uaddr·ustack·`__owner` 를 키로 집계한다.
- `perf lock contention` 은 커널 락 도구다. 사용자 뮤텍스는 futex 해시 버킷의 그림자로만 보인다. `-b` 는 BPF 스켈레톤 빌드가 필요하다(이 VM 에는 없음, `record` + `-i` 로 대체).
- 대기자의 스택은 피해자, holder 가 원인이다. `__owner` 는 잠드는 순간의 표본이라 긴 hold 를 찾는 데 적합하고 짧은 락에선 0 이 많다.
- 대기 시간은 hold 시간이 아니다: 런큐 대기 포함, EAGAIN 은 `@not_slept` 로 따로 센다.
- 고치면 다시 잰다: 맵을 스트라이프하자 stats_lock 이 1 위가 되었다. seq 읽기만 넣어도 소용없었다(프로파일이 지목한 곳을 고쳐야 한다). 둘 다 없앤 fixed 에서 처리량 약 2.6 배, p99.9 98 µs → 0.5 µs (ratio only).
- rwlock 은 짧은 읽기에서 전역 mutex 보다 느렸다(측정해서 안 것).
- 스트라이프 대가: 전역 스냅샷 불가, 다중 키는 번호 순, 핫 키 쏠림, false sharing 정렬.
- seqlock 은 POD 복사, RCU 는 포인터 구조. 재시도 상한과 fallback 으로 reader 굶주림을 막는다.

## 참고자료

- `man 2 futex` — FUTEX_WAIT / FUTEX_WAKE, `FUTEX_PRIVATE_FLAG`, 반환값 (OS-A01 복습)
- Drepper, *Futexes Are Tricky* — glibc 뮤텍스의 0/1/2 상태 (OS-A01)
- `perf-lock` 문서: kernel `tools/perf/Documentation/perf-lock.txt` (이 VM 에는 man 페이지 없음 — 옵션은 `perf lock contention -h` 출력으로 확인)
- bpftrace reference: `ustack`, `hist`, tracepoint `args` — https://bpftrace.org/
- OSTEP 29장 (락 기반 동시 자료구조: 분할 카운터, 스트라이프 해시)
- 연관 레슨: OS-A01 (futex), LLAT-M02 (seqlock fence), OS-A03 (RCU/EBR), ARCH-M08 + MEM-M04 (false sharing), OS-M10 (락 순서), LPERF-A03 (cache line 경합)
