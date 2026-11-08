---
id: LLAT-M05
time: 90분
updated: 2026-11-08
---
# LLAT-M05 스레드 간 핸드오프 지연 (ping-pong)

## 학습 목표

- 두 스레드가 **캐시 라인 하나** 로 값을 주고받는 핑퐁 프로토콜(핑 = 홀수, 퐁 = 짝수 순번)을 짜고, "양쪽이 모든 순번을 정확히 한 번씩 봤다" 를 결정적으로 검사할 수 있다.
- 41.67 ns 눈금 시계로 80 ns 짜리 왕복을 재는 법 — **왕복 N 번을 한 표본으로 묶고(batching) 나누기**, 워밍업, min / p50 / p99 — 을 실제로 해 보고 숫자의 신뢰 범위를 말할 수 있다.
- 이 맥(Apple M6)에서 잰 **코어 배치별 왕복 지연** — Super/Performance 끼리 약 80 ns, E 코어끼리 79–250 ns, P↔E 약 200–400 ns, 잠드는 핸드오프 2.5 µs — 과 "QoS 는 고정이 아니라 힌트" 라는 것을 실측으로 설명한다.
- 같은 라인 vs 다른 라인: 엄격히 번갈아 쓰는 핑퐁에서는 차이가 없고, **각자 쓰는** 경우에만 패딩이 효과를 낸다는 것, 그리고 이 맥에서 실제로 잰 간섭 경계(P 코어끼리 64 B, E 코어가 끼면 128 B)를 말한다.
- 리눅스 `pthread_setaffinity_np` 실험 행렬을 설계하고, VM 에서 돌려 본 결과를 "vCPU 이지 물리 코어가 아니다" 라는 단서와 함께 해석한다. 듀얼 코어 MCU 메일박스(요청 라인 / 응답 라인)를 같은 원리로 짠다.

## 1. 문제

"코어 사이에서 메시지 하나 넘기는 데 몇 ns 걸리죠? 그걸 어떻게 재고, 그 숫자로 뭘 결정하나요?" — 저지연(HFT, 로봇 제어) 면접의 단골 질문이고, 펌웨어에서는 **듀얼 코어 MCU/SoC 의 코어 간 통신**(RP2040 core0↔core1, STM32H7 M7↔M4, i.MX8M A53↔M4 메일박스)에서 같은 질문이 나온다. 답의 핵심은 "스레드 두 개를 서로 다른 코어에서 돌리면 공유 변수 하나를 주고받는 비용은 **캐시 라인이 코어 사이를 오가는 비용** 이고, 잠들었다 깨우면 그보다 30배 이상 비싸다" 이다.

이미 아는 것(다시 가르치지 않는다): 파이프 핑퐁으로 컨텍스트 스위치 비용 재기, 시계 해상도(`CLOCK_MONOTONIC` 1 µs vs `mach_absolute_time` 41.67 ns), 코어 번호 기록, Mach affinity tag 미지원(OS-M02). 원자 연산 하나의 비용과 경합(LLAT-B06), release/acquire publish(C-A03), 스핀락의 라인 핑퐁(OS-M07), futex/wait-on-address 의 원리(OS-A01, MCPP-A03), seqlock(LLAT-M02). 여기서는 **핸드오프 한 번의 지연** 과 그것이 **코어 배치 · 라인 배치 · 기다리는 방식** 에 따라 어떻게 바뀌는지만 잰다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">왕복(RT) 1번 = 라인이 코어 사이를 두 번 이동 (seq 하나: 핑은 홀수, 퐁은 짝수를 쓴다)</text>
  <rect x="20" y="40" width="160" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="34" y="62" fill="currentColor" font-weight="bold">코어 A (핑)</text>
  <text x="34" y="82" fill="currentColor" font-family="monospace" font-size="12">stlr 2k-1 → [seq]</text>
  <text x="34" y="100" fill="currentColor" font-family="monospace" font-size="12">ldapr 돌기: == 2k ?</text>
  <rect x="560" y="40" width="160" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="574" y="62" fill="currentColor" font-weight="bold">코어 B (퐁)</text>
  <text x="574" y="82" fill="currentColor" font-family="monospace" font-size="12">ldapr 돌기: == 2k-1 ?</text>
  <text x="574" y="100" fill="currentColor" font-family="monospace" font-size="12">stlr 2k → [seq]</text>
  <rect x="300" y="50" width="140" height="50" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="314" y="72" fill="currentColor">128 B 라인 [seq]</text>
  <text x="314" y="90" fill="currentColor" font-size="12">소유권은 한 코어만</text>
  <path d="M180 60 L300 60" stroke="currentColor" fill="none"/>
  <path d="M440 60 L560 60" stroke="currentColor" fill="none"/>
  <text x="196" y="54" fill="currentColor" font-size="12">① A 가 쓰려면 독점</text>
  <text x="452" y="54" fill="currentColor" font-size="12">② B 가 읽으면 이동</text>
  <path d="M560 95 L440 95" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <path d="M300 95 L180 95" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="452" y="112" fill="currentColor" font-size="12">③ B 가 2k 를 씀</text>
  <text x="196" y="112" fill="currentColor" font-size="12">④ A 가 읽으면 이동</text>
  <rect x="20" y="140" width="700" height="80" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="34" y="162" fill="currentColor" font-weight="bold">무엇이 왕복 시간을 바꾸나 (이 레슨에서 잰 것)</text>
  <text x="34" y="182" fill="currentColor">· 코어 배치: 같은 클러스터 / 다른 클러스터 / E 코어 (macOS 는 고정 불가 → QoS 힌트 + cpu 번호 기록)</text>
  <text x="34" y="200" fill="currentColor">· 기다리는 방식: 스핀 · WFE (라인이 바뀌면 이벤트) · 잠들기 (os_sync_wait_on_address, 조건 변수)</text>
  <text x="34" y="216" fill="currentColor" font-size="12">· 라인 배치: 깃발 하나 / 깃발 둘을 8 · 64 · 128 · 256 B 떨어뜨림</text>
  <rect x="20" y="236" width="700" height="72" rx="6" fill="none" stroke="currentColor"/>
  <text x="34" y="258" fill="currentColor" font-weight="bold">시계: mach_absolute_time 1 tick = 41.67 ns (24 MHz) &gt; 왕복의 절반</text>
  <text x="34" y="278" fill="currentColor">→ 왕복 1번을 직접 재면 0 · 1 · 2 · 3 tick 으로만 보인다. 64번을 한 표본으로 묶어 재고 ÷64.</text>
  <text x="34" y="298" fill="currentColor" font-size="12">한 방향 지연은 묶을 수 없다 (연습 4: 중앙값 1 tick) — 그래서 보통 왕복/2 로 말한다.</text>
</svg>
```

체크 테스트(`starter.c` → `solution.c`)는 **프로토콜 정확성** 만 본다: 양쪽이 모든 순번을 정확히 한 번씩 봤나, 최종 값, 메일박스 응답이 그 요청의 답인가. 지연 숫자는 `demo_*.c` 의 출력이다.

### Part A — 재는 도구와 한 라인 핑퐁

- `rt_stats(ticks, n, warm, batch, numer, denom)` — 표본 하나 = `batch` 번 왕복의 tick. 앞 `warm` 개를 버리고 **왕복 1번당 피코초** 로 min / p50 / p99 / max (정의는 OS-M02 와 같다: 낮은 쪽 중앙값, nearest-rank p99). `tick × numer × 1000` 이 64비트를 넘쳐도 정확하게.
- `qos_apply(q)` — 이 스레드의 QoS 를 요청하고, 받아들여졌는지 `qos_class_self()` 로 확인.
- `spin_until_eq(p, want, deadline)` — acquire load 로 돌되 4096번마다 마감을 보고 `sched_yield`. `pp_run(side)` — 핑퐁의 한쪽(initiator: 쓰고 기다림, responder: 기다리고 씀). 2만 왕복, 양쪽 `seen[k] == 1`.

### Part B — 라인 배치와 잠드는 핸드오프

- `same_line(a, b, line)` — 두 주소가 같은 정렬 블록에 있나. 8 B / 64 B / 128 B 떨어진 구조체로 확인 (64 B 거리는 128 블록으로는 같고 64 블록으로는 다르다).
- 깃발 두 개(`a` 는 핑만, `b` 는 퐁만 쓴다, 128 B 패딩)로 같은 핑퐁.
- `blk_wait_eq` / `blk_post` — `os_sync_wait_on_address_with_timeout`(macOS 14.4+, 이 SDK 에 헤더 있음)으로 **잠드는** 대기. 값이 이미 바뀌었으면 잠들지 않는다(lost wakeup 없음), 마감이 있다.
- 관찰: `demo_pingpong.c` — 레이아웃 · QoS 조합 · 기다리는 방식별 왕복 지연, `fs` 모드로 라인 간섭 경계.

### Part C — 듀얼 코어 메일박스 RPC

코어 0 이 명령(`cmd` + `arg[4]`)을 보내고 코어 1 이 계산해 답하는 메일박스. 요청 라인과 응답 라인을 **따로** 두고(128 B), payload 는 평범한 변수, `seq` 의 release/acquire 가 순서를 보장한다. 2만 번 호출 — 답이 전부 그 요청의 답이고, 서버는 순번을 하나도 건너뛰거나 두 번 보지 않는다. 깨진 버전(relaxed seq)은 `demo_broken.c`.

## 2. Problem (English)

You are characterizing core-to-core handoff latency on a heterogeneous SoC (Apple M6: 2 "Super" + 4 Performance + 6 Efficiency cores) and turning it into a mailbox design.

**Part A.** Implement `rt_stats_t rt_stats(const uint64_t *ticks, int n, int warm, uint32_t batch, uint32_t numer, uint32_t denom)`: each sample is the tick count of `batch` round trips; drop the first `warm` samples and return min / lower-median / nearest-rank p99 / max **per round trip in picoseconds** (`ticks*numer*1000/(denom*batch)`, truncating, overflow-safe); invalid input → all zeros. Implement `bool qos_apply(qos_class_t q)`. Implement `bool spin_until_eq(_Atomic uint64_t *p, uint64_t want, uint64_t deadline_ns)` (acquire loads, check the clock and `sched_yield` every 4096 spins, false after the deadline) and `uint64_t pp_run(const pp_side_t *s)`: one side of a ping-pong where round *k* posts `pmul*k+poff` and waits for `wmul*k+woff` (initiator posts first), counting `seen[k]` on every successful wait. Two threads, one 64-bit `seq` (ping odd, pong even), 20 000 rounds: both sides must see every value exactly once and `seq` must end at 40 000.

**Part B.** `bool same_line(const void *a, const void *b, size_t line)`. The same ping-pong with two flags on separate 128-byte lines. A sleeping handoff: `bool blk_wait_eq(...)` using `os_sync_wait_on_address_with_timeout` (retry on EINTR/EFAULT/ETIMEDOUT until the deadline; never sleep if the value already changed) and `void blk_post(_Atomic uint64_t *p, uint64_t v)` (release store, then wake one).

**Part C.** A two-line mailbox: `bool mbx_call(mbx_t*, cmd, arg[4], *result, *status, deadline)` and `bool mbx_serve_one(mbx_t*, uint64_t *last, deadline)`. Payload fields are plain; ordering comes only from release/acquire on `req.seq` / `rsp.seq`.

Constraints: C11 + pthreads, macOS arm64, no timing in tests, every wait bounded. Follow-up: measure round-trip latency for P–P, E–E, P–E placements, same-line vs split flags, spin vs WFE vs futex-style sleep vs condvar, and propose the Linux `pthread_setaffinity_np` experiment matrix.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력, exit 1, 0.1초 안에 끝남 — 스텁은 기다리지 않고 틀린 값을 돌려준다):

```text
-- Part A: rt_stats / qos_apply / one-line ping-pong
FAIL starter.c:194  r.min_ps == 1953 && r.p50_ps == 3906 && r.p99_ps == 7812 && r.max_ps == 7812
FAIL starter.c:199  r.min_ps == 1000 && r.p50_ps == 50000 && r.p99_ps == 99000 && r.max_ps == 100000
...
FAIL starter.c:226  dp == R && dq == R
FAIL starter.c:227  op && oq
...
-- Part B: layout / two flags / blocking handoff
FAIL starter.c:240  same_line(&pk.a, &pk.b, LINE)
...
-- Part C: dual-core mailbox RPC
FAIL starter.c:310  calls == NCALL && s.served == NCALL
FAIL starter.c:311  ok == NCALL && st_ok == NCALL
FAIL starter.c:313  atomic_load(&m.req.seq) == NCALL && atomic_load(&m.rsp.seq) == NCALL
11/33 passed
```

관찰용 파일 (하네스와 별개, 숫자는 CHECK 하지 않는다):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_pingpong.c -o pp
./pp suite        # 배치 · 레이아웃 · QoS 조합 · 기다리는 방식 (약 1초)
./pp fs ui        # 각자 쓰는 두 카운터: 거리 8~256 B (P 코어)   ./pp fs bg  (E 코어가 끼게)
./pp map          # cpu 번호 → 코어 종류 추정 (빈 루프 속도, 1.5초)
PP_SPLIT=1 ./pp rt spin one ui bg 3 2000 64     # 한 조합만, 코어 쌍 종류별로 나눠서
cc -std=c11 -Wall -Wextra -O2 -pthread demo_broken.c -o broken && ./broken 2000000           # relaxed 메일박스
cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_broken.c -o broken_tsan && ./broken_tsan 1000
# 리눅스 (Lima VM elinux, /tmp/LLAT-M05_work 로 복사해서):
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_linux_affinity.c -o aff && ./aff
```

## 4. 힌트

<details>
<summary>힌트 1 — 왕복 80 ns 를 41.67 ns 시계로?</summary>

한 왕복마다 시계를 읽으면 결과는 0, 41.7, 83.3, 125 ns 중 하나다 (실측 `batch 1` 줄: min 0.0, p50 83.3). 64 왕복을 한 표본으로 재면 표본이 약 5 µs 가 되고 한 눈금 오차가 1% 아래로 내려간다. `rt_stats` 는 그 나눗셈을 하고, 나눗셈 전에 곱하면 넘치므로 `unsigned __int128` 로 곱한다 (2^40 tick × 125 × 1000 은 2^64 를 넘는다).
</details>

<details>
<summary>힌트 2 — 정확히 한 번을 보장하는 대기 조건</summary>

"홀수면 답한다" 처럼 **모양** 으로 기다리면 같은 값을 두 번 볼 수 있다. 라운드 k 에서 기다리는 값은 정확히 하나(`2k−1` 또는 `2k`)이고 `==` 로 비교한다. `poff = (uint64_t)-1` 은 부호 없는 정수 wrap 으로 `2k − 1` 을 만든다 — `pmul*k + poff` 한 식으로 두 레이아웃을 다 표현하려는 것.
</details>

<details>
<summary>힌트 3 — 스핀이 멈추지 않게</summary>

상대가 틀렸거나(starter) 같은 코어에 몰렸으면 영원히 돌 수 있다. 4096번마다 `now_ns() > deadline` 이면 false, 아니면 `sched_yield()` — 스레드가 코어보다 많을 때 상대에게 코어를 넘긴다 (리눅스 VM 에서 같은 vCPU 에 둘을 묶으면 순수 스핀은 왕복 6 ms 가 된다 — 5. 풀이).
</details>

<details>
<summary>힌트 4 — os_sync_wait_on_address 에 넘기는 값</summary>

"이 값이면 잔다" 의 **이 값** 은 `want` 가 아니라 방금 읽은 **현재 값 v** 다. 커널은 `*p == v` 를 원자적으로 확인하고 잔다. post 가 load 와 wait 사이에 끼면 `*p != v` 라서 바로 돌아온다. `want` 를 넘기면 "이미 원하는 값일 때 잠드는" 반대 동작이 된다. 반환 후에는 항상 다시 읽고 비교한다 (가짜 깨움, 다른 값).
</details>

<details>
<summary>힌트 5 — 메일박스의 순서</summary>

클라이언트: payload 쓰기 → `req.seq = s` (release) → `rsp.seq == s` (acquire) → 결과 읽기. 서버: `req.seq == last+1` (acquire) → payload 읽기 → 결과 쓰기 → `rsp.seq = s` (release). 다음 호출의 payload 는 응답을 받은 뒤에야 쓰므로 서버가 아직 읽는 중인 payload 를 덮지 않는다 — 단일 슬롯 메일박스는 이 "응답 = 빈 슬롯 신호" 가 핵심이다.
</details>

## 5. 풀이

### 0. 측정 환경 — 숫자를 읽기 전에

```text
$ sysctl -n machdep.cpu.brand_string hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu hw.perflevel2.physicalcpu
Apple M6
2
4
6
$ sysctl hw.cachelinesize hw.perflevel0.name hw.perflevel1.name hw.perflevel2.name
hw.cachelinesize: 128
hw.perflevel0.name: Super
hw.perflevel1.name: Performance
hw.perflevel2.name: Efficiency
$ sysctl hw.perflevel0.cpusperl2 hw.perflevel1.cpusperl2 hw.perflevel2.cpusperl2
hw.perflevel0.cpusperl2: 2
hw.perflevel1.cpusperl2: 4
hw.perflevel2.cpusperl2: 6
$ cc --version | head -1
Apple clang version 21.0.0 (clang-2100.0.123.102)
$ echo | cc -dM -E -x c - | grep DESTRUCTIVE
#define __GCC_DESTRUCTIVE_SIZE 256
```

**같은 맥에서 다른 세션 6개가 동시에 벤치마크를 돌렸다** (load average 3.7–8). 절대 ns 보다 **배수** 를 믿을 것. 각 표 옆에 `uptime` 을 적었다.

코어 번호는 읽을 수만 있다(`pthread_cpu_number_np`, OS-M02). 어느 번호가 어느 종류인지는 빈 루프 20000회의 가장 빠른 시간으로 추정했다:

```text
$ ./pp map        (load average 4.36)
USER_INTERACTIVE: cpu0 x29 best 40750ns | cpu3 x10 best 40791ns | cpu4 x12 best 40833ns | ... | cpu6 x77812 best 4083ns |
                  cpu7 x73650 best 4083ns | cpu8 x672 best 4541ns | cpu9 x1332 best 4458ns | cpu10 x551 best 4666ns | cpu11 x1115 best 4500ns |
BACKGROUND      : cpu6 x76684 best 4083ns | cpu7 x87158 best 4083ns | cpu8 x113 best 17083ns | ... | cpu10 x47 best 4458ns |
```

0–5 는 10배 느리다 → **E 코어** (OS-M02 와 같은 결론). 6–7 이 가장 빠르고(4083 ns) 개수가 `perflevel0` 과 같은 2 → **Super** 로, 8–11 (4458–4666 ns) → **Performance** 로 **추정** 했다 (Apple 문서로 확인한 번호 매핑은 아니다). 이 표의 BACKGROUND 줄에도 주목: BACKGROUND 로 요청한 바쁜 스레드가 1.5초 동안 E 코어를 한 번도 안 썼다 — QoS 는 힌트다.

### Part A — 재는 법: 배치와 정확성

`demo_pingpong.c` 는 핑이 `batch` 번 왕복을 `mach_absolute_time` 두 번 사이에 넣고(표본 하나), 앞 10% 표본을 버린다. 같은 조합(spin, seq 하나, ui/ui)을 batch 만 바꿔 쟀다:

```text
$ ./pp suite      (load average 3.72 → 4.36, ns per round trip, 3 runs)
spin  one  ui /ui  | p50    83.3 p99    166.7 min    0.0 | p50    83.3 p99    166.7 min    0.0 | p50    83.3 p99    166.7 min    0.0    ← batch 1
spin  one  ui /ui  | p50    88.5 p99    130.2 min   67.7 | p50    88.5 p99    125.0 min   72.9 | p50    88.5 p99    125.0 min   78.1    ← batch 8
spin  one  ui /ui  | p50    86.6 p99    151.7 min   73.6 | p50    85.9 p99    100.9 min   74.2 | p50    85.9 p99    110.0 min   73.6    ← batch 64
```

| batch | 표본 하나 | 보이는 값 | 읽는 법 |
|---|---|---|---|
| 1 | ≈ 2 tick | 0, 41.7, 83.3, 125, 166.7 만 | min 0 은 "0 ns 왕복" 이 아니라 시계 한계. p50 83.3 = 2 tick |
| 8 | ≈ 17 tick | 5.2 ns 단위 | min/p50 이 의미를 갖기 시작 |
| 64 | ≈ 130 tick ≈ 5.5 µs | 0.65 ns 단위 | 한 눈금 오차 &lt; 1%. **이 레슨의 모든 스핀 숫자는 batch 64** |

배치의 대가: 표본이 왕복 64개의 **평균** 이라서 꼬리(p99)가 눌린다. 진짜 꼬리 분포를 보려면 왕복마다 재되, 시계 단위가 거칠다는 것을 감수해야 한다 (CPU 사이클 카운터를 못 읽는 맥에서는 이게 한계 — Cortex-M 은 `DWT->CYCCNT` 로 사이클 단위가 가능, RTOS-A03).

`warm` 을 버리는 이유도 실측으로 보였다: 프로그램 시작 직후 첫 실험은 `E% 100/100`(두 스레드가 E 코어에서 깨어남)과 150–187 ns 로 나왔다. 그래서 `suite` 앞에 0.1초 워밍업 실행을 넣었다.

**정확성:** `pp_run` 은 라운드 k 에서 정확한 값 하나를 기다린다. 핑은 `2k−1` 을 쓰고 `2k` 를, 퐁은 `2k−1` 을 기다리고 `2k` 를 쓴다. 이 값은 한 번만 나타나므로 `seen[k]` 는 정확히 1, 끝나면 `seq == 2R`. 체크가 이걸 2만 라운드로 확인한다.

**asm** (`cc -std=c11 -O2 -S solution.c`, `pp_run` 이 인라인된 `side_thread` 의 스핀 부분, 실제 출력에서 발췌):

```text
LBB4_8:
	ldapr	x8, [x22]          ; acquire load → LDAPR (RCpc, FEAT_LRCPC)
	cmp	x8, x21
	b.ne	LBB4_12
...
	stlr	x25, [x0]          ; release store → STLR
```

Apple clang 의 기본 target-cpu 는 `apple-m1` 이다 (`cc -### -c`: `"-target-cpu" "apple-m1"`). 그래서 `memory_order_acquire` load 가 `ldapr` 로 나온다. `-mcpu=apple-a7`(ARMv8.0)로 같은 함수를 컴파일하면 `ldar` 가 나왔다. 둘 다 이 핑퐁에는 충분하다 — ldapr 이 더 약한 것은 "앞선 stlr 와 다른 주소" 순서뿐이다 (ARM-M05). `seq_cst` 로 쓴 `atomic_load` 는 `ldar` 였다.

### Part A — QoS 조합: P–P, E–E, P–E

macOS 는 코어를 고정할 수 없다 (`sched_setaffinity` 없음, affinity tag 는 `KERN_NOT_SUPPORTED` — OS-M02 에서 확인). 할 수 있는 것은 **QoS 를 요청하고, 실제로 어디서 돌았는지 기록해서 나누는 것** 이다. `pthread_set_qos_class_self_np` 를 핑과 퐁이 각자 부르고, 핑은 표본마다, 퐁은 256 왕복마다 cpu 번호를 기록한다. 대괄호 안 `SE:1773@330` = "핑 Super, 퐁 E" 인 표본 1773개, 그 p50 330 ns.

```text
$ ./pp suite      (load average 3.72 → 4.36)
spin  one  ui /ui  | p50 186.2 ... E% 44/ 78 [ EE:254@79 ES:433@191 PE:1113@186 ] | p50 86.6 E%100/ 84 [ EE:1478@86 ES:322@199 ] | p50 184.9 E% 14/ 73 [ EE:277@84 SE:447@198 PE:544@186 PP:532@77 ]
spin  one  ut /ut  | p50  87.9 ... E%100/100 [ EE:1795@88 ... ] | p50 87.2 E% 85/100 [ EE:1495@87 SE:305@197 ] | p50 87.2 E% 99/100 [ EE:1788@87 ... ]
spin  one  bg /bg  | p50  78.8 ... E%100/100 [ EE:1800@79 ] | p50 93.1 E%100/100 [ EE:1800@93 ] | p50 248.0 E%100/100 [ EE:1800@248 ]
spin  one  ui /bg  | p50 330.1 ... E%  0/100 [ SE:1773@330 PE:27@335 ] | p50 259.8 [ SE:1780@258 ... ] | p50 278.6 [ SE:1798@277 ... ]
spin  one  bg /ui  | p50 250.0 ... E%100/  0 [ ES:1792@250 ... ] | p50 260.4 [ ES:1796@260 ... ] | p50 261.7 [ ES:1800@262 ]
spin  one  ui /ut  | p50  82.0 ... E%  0/  0 [ SS:1648@82 SP:146@80 PP:6@77 ] | p50 82.0 [ SS:1692@82 SP:108@79 ] | p50 82.0 [ SS:1384@82 SP:416@79 ]
```

한 번 더 (같은 날, load average 4.45):

```text
spin  one  ui /ui  | p50  89.8 E%  0/  0 [ SS:1800@90 ] | p50 85.3 E%  9/  0 [ ES:174@188 SS:1555@85 SP:28@80 PS:43@79 ] | p50 85.3 [ SS:1800@85 ]
spin  one  bg /bg  | p50 160.8 E%100/100 [ EE:1800@161 ] | p50 156.2 [ EE:1800@156 ] | p50 170.6 [ EE:1800@171 ]
spin  one  ui /bg  | p50 253.9 E%  0/100 [ SE:1800@254 ] | p50 256.5 [ SE:1798@257 ... ] | p50 246.7 [ SE:1782@247 ... ]
spin  one  ut /ut  | p50  77.5 E%  0/  0 [ SS:4@83 SP:364@82 PS:1335@77 PP:97@77 ] | p50 81.4 [ SP:1294@81 ... ] | p50 82.0 [ SS:613@85 ... PP:139@72 ]
```

코어 쌍 종류별로 모으면 (위 두 번 + 앞선 시험 실행들, 표본 p50 범위):

| 쌍 (핑–퐁) | 왕복 p50 | 메모 |
|---|---|---|
| Super–Super | 82–90 ns | 같은 클러스터 (cpusperl2 2) |
| Super↔Performance | 77–82 ns | 다른 perflevel 인데 오히려 약간 빠름 — 원인 미확인 |
| Performance–Performance | 72–83 ns | 표본 수가 적은 run 이 많다 |
| E–E | **79–250 ns** | run 마다 크게 다름 — E 클러스터 클럭(DVFS)에 따라 (추정) |
| Super/Perf ↔ E | **206–393 ns** | 가장 비싸다. P 쪽 왕복의 약 3–4배 |
| 같은 코어 (time slicing) | 측정 불가 (맥) | 리눅스 VM 에서 6 ms — 아래 |

읽는 법:

1. **클러스터를 건너면 비싸다.** P↔E 는 라인이 두 클러스터의 L2 사이를 오가야 한다 (L2 를 공유하지 않으므로 그 밑의 공유 계층을 거친다 — Apple 이 구조를 공개하지 않아서 "어디까지 내려가는지" 는 추정). 같은 P 쪽 안에서는 80 ns 안팎.
2. **E–E 가 79 ns 인 run 도 있다.** 다른 세션이 E 코어를 바쁘게 쓰던 시점이라 E 클러스터 클럭이 올라가 있었다고 본다(추정). 같은 조합이 248 ns 일 때도 있었다. "E 코어끼리는 X ns" 라고 한 숫자로 말하면 틀린다.
3. **QoS 는 고정이 아니다.** BACKGROUND 핑퐁은 모든 run 에서 표본 100% 가 E 코어였다 — 가장 믿을 만한 힌트. 반면 USER_INTERACTIVE 핑퐁이 부하가 있을 때 표본의 44%/78% 를 E 코어에서 보낸 run 이 있었고(`E% 44/ 78`), UTILITY 는 어떤 run 은 100% E, 어떤 run 은 0% E 였다. 그리고 `map` 에서는 BACKGROUND 빈 루프가 Super 코어에만 있었다. QoS 는 "이 일은 얼마나 급한가" 를 스케줄러에 알려 줄 뿐이다.
4. **보고하는 법:** "왕복 80 ns" 가 아니라 "Super/P 끼리 77–90 ns, P↔E 200–400 ns, 코어 쌍은 기록해서 분리, load 4" 처럼 조건을 붙인다.

### Part B — 라인 배치: 같은 라인 vs 다른 라인

깃발 두 개(핑은 `a` 만, 퐁은 `b` 만 쓴다)를 8 / 64 / 128 / 256 B 떨어뜨려 같은 핑퐁:

```text
$ ./pp suite      (load average 3.72)
spin  d8   ui /ui  | p50    85.9 p99    107.4 min   73.6 | p50    85.9 p99    101.6 min   76.2 | p50    85.9 p99    139.3 min   72.3
spin  d64  ui /ui  | p50    86.6 p99    102.9 min   72.9 | p50    87.9 p99    102.9 min   80.7 | p50    86.6 p99    106.1 min   72.9
spin  d128 ui /ui  | p50    78.1 p99    100.3 min   70.3 | p50    77.5 p99     86.6 min   69.7 | p50    76.8 p99    147.8 min   71.0
spin  d256 ui /ui  | p50    78.8 p99    143.2 min   70.3 | p50    80.1 p99    119.8 min   72.3 | p50    76.8 p99    111.3 min   71.6
```

다른 실행(load 4.6)에서는 d8 82.0 / d64 82.0 / d128 79.4 / d256 82.0 이었다. **엄격히 번갈아 쓰는 핑퐁에서는 패딩이 거의 상관없다** (차이 0–10%, run 사이 흔들림과 비슷). 이유는 라인 이동 횟수다: 한 라인이든 두 라인이든 왕복마다 "핑의 쓰기가 퐁에게 보이기" 와 "퐁의 쓰기가 핑에게 보이기" 두 번의 이동이 필요하고, 한 쪽이 기다리는 동안 다른 쪽은 쓰지 않는다. 패딩이 효과를 내는 것은 **두 코어가 동시에 각자 쓰는** 경우다. 그래서 `fs` 모드 — 두 스레드가 각자 자기 카운터만 `fetch_add`(relaxed, `ldadd`) 2천만 번:

```text
$ ./pp fs ui      (load average 4.36; ns/op, 3 runs, E% = 표본 중 E 코어 비율)
fs d=8    | 12.63 ns/op E%  6/  6 | 10.44 ns/op E% 13/  8 | 10.78 ns/op E% 11/ 10
fs d=32   | 10.49 ns/op E%  4/  4 | 10.91 ns/op E%  0/  0 | 11.36 ns/op E%  0/  0
fs d=56   | 11.26 ns/op E%  0/  0 | 11.35 ns/op E%  0/  0 | 10.78 ns/op E%  9/ 10
fs d=64   |  1.42 ns/op E%  0/  0 |  1.99 ns/op E% 16/ 13 |  2.45 ns/op E% 32/ 33
fs d=96   |  1.43 ns/op E%  0/  0 |  1.42 ns/op E%  0/  0 |  1.41 ns/op E%  0/  0
fs d=120  |  1.43 ns/op E%  0/  0 |  1.43 ns/op E%  0/  0 |  1.42 ns/op E%  0/  0
fs d=128  |  1.48 ns/op E%  0/  0 |  1.55 ns/op E%  0/  0 |  1.49 ns/op E%  0/  0
fs d=256  |  1.45 ns/op E%  0/  0 |  1.42 ns/op E%  0/  0 |  1.43 ns/op E%  0/  0
$ ./pp fs bg
fs d=8    | 15.07 ns/op E% 31/ 72 | 13.28 ns/op E% 42/ 51 | 12.02 ns/op E%  1/ 32
fs d=32   | 11.75 ns/op E%  0/ 29 |  5.61 ns/op E%  2/ 12 | 17.38 ns/op E%  0/ 39
fs d=56   | 14.85 ns/op E%  0/ 31 | 17.41 ns/op E%  0/ 51 | 14.15 ns/op E% 26/ 66
fs d=64   | 19.42 ns/op E%  1/ 54 | 14.64 ns/op E% 14/ 58 | 14.41 ns/op E% 16/ 74
fs d=96   |  9.83 ns/op E% 16/ 43 | 11.37 ns/op E%  3/ 29 | 19.51 ns/op E%  0/ 46
fs d=120  | 13.50 ns/op E% 20/ 73 |  7.75 ns/op E% 62/ 75 | 16.62 ns/op E% 19/ 69
fs d=128  |  2.31 ns/op E%  0/ 42 |  2.30 ns/op E%  0/ 38 |  2.19 ns/op E%  0/ 44
fs d=256  |  2.34 ns/op E%  0/ 32 |  2.32 ns/op E%  0/ 33 |  2.55 ns/op E%  0/ 39
```

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">각자 쓰는 두 카운터의 거리 vs ns/op (실측, 3 run 중앙값, 10 px = 1 ns)</text>
  <line x1="80" y1="250" x2="730" y2="250" stroke="currentColor"/>
  <line x1="80" y1="50" x2="80" y2="250" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="50" y="254">0</text><text x="44" y="154">10</text><text x="44" y="54">20 ns</text>
  </g>
  <line x1="76" y1="150" x2="730" y2="150" stroke="currentColor" stroke-dasharray="2 4"/>
  <rect x="100" y="142.2" width="24" height="107.8" class="f-acc" stroke="currentColor"/>
  <rect x="128" y="117.2" width="24" height="132.8" class="f-warn" stroke="currentColor"/>
  <rect x="180" y="140.9" width="24" height="109.1" class="f-acc" stroke="currentColor"/>
  <rect x="208" y="132.5" width="24" height="117.5" class="f-warn" stroke="currentColor"/>
  <rect x="260" y="137.4" width="24" height="112.6" class="f-acc" stroke="currentColor"/>
  <rect x="288" y="101.5" width="24" height="148.5" class="f-warn" stroke="currentColor"/>
  <rect x="340" y="230.1" width="24" height="19.9" class="f-ok" stroke="currentColor"/>
  <rect x="368" y="103.6" width="24" height="146.4" class="f-warn" stroke="currentColor"/>
  <rect x="420" y="235.8" width="24" height="14.2" class="f-ok" stroke="currentColor"/>
  <rect x="448" y="136.3" width="24" height="113.7" class="f-warn" stroke="currentColor"/>
  <rect x="500" y="235.7" width="24" height="14.3" class="f-ok" stroke="currentColor"/>
  <rect x="528" y="115.0" width="24" height="135.0" class="f-warn" stroke="currentColor"/>
  <rect x="580" y="235.1" width="24" height="14.9" class="f-ok" stroke="currentColor"/>
  <rect x="608" y="227.0" width="24" height="23.0" class="f-ok" stroke="currentColor"/>
  <rect x="660" y="235.7" width="24" height="14.3" class="f-ok" stroke="currentColor"/>
  <rect x="688" y="226.6" width="24" height="23.4" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="110" y="268">8 B</text><text x="188" y="268">32 B</text><text x="268" y="268">56 B</text><text x="348" y="268">64 B</text>
    <text x="428" y="268">96 B</text><text x="506" y="268">120 B</text><text x="586" y="268">128 B</text><text x="666" y="268">256 B</text>
  </g>
  <rect x="90" y="284" width="14" height="12" class="f-acc" stroke="currentColor"/>
  <text x="110" y="295" fill="currentColor" font-size="12">ui (P/Super 코어끼리): 64 B 부터 깨끗</text>
  <rect x="380" y="284" width="14" height="12" class="f-warn" stroke="currentColor"/>
  <text x="400" y="295" fill="currentColor" font-size="12">bg (E 코어가 낌): 120 B 까지 느림, 128 B 부터 깨끗</text>
  <text x="90" y="314" fill="currentColor" font-size="12">초록 = 간섭 없음 (1.4–2.4 ns/op). 같은 블록 안이면 7–14배.</text>
</svg>
```

이 맥에서 본 것:

- P/Super 코어끼리: 8–56 B 는 10–12 ns/op, **64 B 부터 1.4 ns/op** — 간섭 경계가 64 B 처럼 보인다. `hw.cachelinesize` 는 128 인데.
- E 코어가 끼면(bg, 한쪽 스레드 표본의 30–75% 가 E): 64–120 B 도 8–20 ns/op, **128 B 부터 2.3 ns/op**.
- 해석(추정, Apple 미공개): P 쪽 L1 은 64 B 단위로 소유권을 추적하고, 클러스터를 건너는 일관성은 128 B 단위로 보인다. 어느 쪽이든 결론은 같다: **패딩은 128 B (`hw.cachelinesize`) 이상.** clang 이 이 타깃에 정의한 `__GCC_DESTRUCTIVE_SIZE` 는 256 으로 더 보수적이다 (C++ `std::hardware_destructive_interference_size` 가 이 값을 쓴다). Cortex-A 리눅스 보드는 보통 64 B. false sharing 자체의 이론은 ARCH-M08.

### Part B — 기다리는 방식: 스핀 · WFE · 잠들기

```text
$ ./pp suite      (load average 3.72 → 4.36; ulock/cond 은 batch 4)
wfe   one  ui /ui  | p50    79.4 p99     82.7 min   78.1 | p50    79.4 p99     91.8 min   77.5 | p50    79.4 p99     80.7 min   77.5
ulock one  ui /ui  | p50  2500.0 p99   4385.4 min 1718.8 | p50  2489.6 p99   4437.5 min 1697.9 | p50  2510.4 p99   4437.5 min 1708.3
cond  one  ui /ui  | p50  2531.2 p99   4729.2 min 1937.5 | p50  2572.9 p99   4520.8 min 1718.8 | p50  2562.5 p99   4927.1 min 1729.2
ulock one  bg /bg  | p50 10364.6 p99  22541.7 min 7156.2 | p50  9791.7 p99  18593.8 min  531.2 | p50  7416.7 p99  17395.8 min  875.0
cond  one  bg /bg  | p50  7729.2 p99  19260.4 min 7104.2 | p50  7302.1 p99  20552.1 min 3864.6 | p50  7322.9 p99  19041.7 min 3875.0
```

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">핸드오프 왕복 p50 — 실측, 로그 눈금 (x = 150 + 200·log10(ns/50))</text>
  <line x1="150" y1="34" x2="150" y2="352" stroke="currentColor"/>
  <g stroke="currentColor" stroke-dasharray="2 4">
    <line x1="210.2" y1="34" x2="210.2" y2="352"/><line x1="410.2" y1="34" x2="410.2" y2="352"/><line x1="610.2" y1="34" x2="610.2" y2="352"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="140" y="368">50</text><text x="198" y="368">100</text><text x="392" y="368">1 µs</text><text x="590" y="368">10 µs</text>
    <text x="10" y="52">spin P–P</text><text x="10" y="80">WFE P–P</text><text x="10" y="108">spin E–E</text>
    <text x="10" y="136">spin P↔E</text><text x="10" y="164">VM spin vCPU↔vCPU</text><text x="10" y="192">ulock ui</text>
    <text x="10" y="220">cond ui</text><text x="10" y="248">VM cond 같은 vCPU</text><text x="10" y="276">cond bg (E)</text>
    <text x="10" y="304">ulock bg (E)</text><text x="10" y="332">VM cond vCPU↔vCPU</text>
  </g>
  <rect x="150" y="40" width="46.1" height="16" class="f-ok" stroke="currentColor"/>
  <rect x="150" y="68" width="39.7" height="16" class="f-ok" stroke="currentColor"/>
  <rect x="150" y="96" width="87.6" height="16" class="f-ok" stroke="currentColor"/>
  <rect x="150" y="124" width="142.2" height="16" class="f-warn" stroke="currentColor"/>
  <rect x="150" y="152" width="53" height="16" class="f-mut" stroke="currentColor"/>
  <rect x="150" y="180" width="339.5" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="150" y="208" width="341.2" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="150" y="236" width="378.4" height="16" class="f-mut" stroke="currentColor"/>
  <rect x="150" y="264" width="433.2" height="16" class="f-warn" stroke="currentColor"/>
  <rect x="150" y="292" width="451.3" height="16" class="f-warn" stroke="currentColor"/>
  <rect x="150" y="320" width="491.9" height="16" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="202" y="53">≈ 85 ns</text><text x="196" y="81">≈ 79 ns</text><text x="244" y="109">79–250 ns (중앙 ≈ 137)</text>
    <text x="298" y="137">≈ 257 ns (206–393)</text><text x="209" y="165">≈ 92 ns</text><text x="496" y="193">≈ 2.5 µs</text>
    <text x="498" y="221">≈ 2.5 µs</text><text x="534" y="249">≈ 3.9 µs</text><text x="590" y="277">≈ 7.3 µs</text>
    <text x="608" y="305">≈ 9 µs</text><text x="648" y="333">≈ 14.4 µs</text>
  </g>
  <text x="10" y="390" fill="currentColor" font-size="12">초록 = 스핀(코어 2개를 100% 씀), 주황 = 잠들기, 노랑 = E 코어가 낌, 회색 = 리눅스 VM. VM 같은 vCPU 스핀 = 6 ms (축 밖).</text>
</svg>
```

| 방식 | 왕복 p50 (P 코어) | 코어 사용 | 언제 |
|---|---|---|---|
| 스핀 (`ldapr` 반복) | ≈ 80 ns | 양쪽 100% | 코어를 전용으로 줄 수 있을 때 (HFT, 전용 폴링 스레드) |
| WFE (`sevl; wfe; ldaxr`) | ≈ 79 ns | 코어는 깨어 있지만 명령을 안 돈다 | 스핀과 같은 지연, 덜 뜨겁게 (전력은 이 레슨에서 안 쟀다) |
| `os_sync_wait_on_address` | ≈ 2.5 µs | 기다리는 동안 0 | 기다림이 길거나 예측 불가 |
| 조건 변수 | ≈ 2.5 µs | 0 | 같은 대 — 비용 대부분이 "잠들고 깨우기" |

- **스핀 vs 잠들기 ≈ 30배.** 잠드는 쪽은 커널 진입 → 스레드를 대기 큐에 → 다른 쪽이 깨우기 시스템 콜 → 스케줄러가 깨어날 코어를 고름(유휴 코어면 IPI) → 복귀. OS-M02 의 파이프 핑퐁(2.8 µs)과 같은 대다. 전달 수단(파이프 / 조건 변수 / wait-on-address)보다 잠들기 자체가 비싸다.
- **ulock 의 min 531 ns** (bg run 2): 기다리는 쪽이 아직 잠들기 전에 값이 바뀐 왕복이다 — `os_sync_wait_on_address` 가 "값이 이미 다르면 잠들지 않는다" 를 지킨 결과. 그래서 실무 락/큐는 **잠깐 스핀 후 잠들기** (spin-then-park)를 쓴다 (glibc mutex adaptive, libc++ `atomic::wait` 의 스핀 단계 — MCPP-A03).
- **WFE 가 스핀만큼 빠른 이유:** `ldaxr` 가 라인을 exclusive 모니터에 걸어 두면, 다른 코어가 그 라인에 쓸 때 모니터가 풀리며 이벤트가 생겨 `wfe` 가 깬다 (Arm ARM, "Wait for Event mechanism" / exclusive monitor 절 — 문서 기준). 리눅스 arm64 의 `smp_cond_load_acquire` 가 이 방식이다(`__cmpwait`, 문서·소스 기준 미검증). 이 맥 사용자 모드에서 `wfe` 는 트랩되지 않았다. 다만 아무도 안 써도 깨어나는 간격을 재 보니 **약 1.3 µs** 였다 (`wfe_probe`: 200 ms 동안 약 15만 번, 맥과 리눅스 VM 둘 다) — 깊이 잠드는 명령이 아니다. 원인(이벤트 스트림 설정인지 구현 특성인지)은 미확인. 덕분에 연습 3 의 마감 확인이 동작한다.
- E 코어에서 잠드는 핸드오프는 7–10 µs — P 코어의 3–4배. 지연 민감 스레드를 BACKGROUND 로 두면 안 되는 이유.

asm (`cc -O2 -S practice.c`, 연습 3 의 루프, 실제 출력):

```text
	wfe
	ldaxr	x8, [x20]
	cmp	x8, #1
	b.ne	LBB0_57
```

### Part B — 리눅스: `pthread_setaffinity_np` 실험 계획

리눅스는 고정할 수 있으므로 "QoS 로 유도하고 기록해서 나누기" 대신 **행렬로 직접** 잰다:

| 칸 | 고정 | 무엇을 재나 | 실기에서의 기대 (방향만) |
|---|---|---|---|
| ① 같은 코어 | 둘 다 cpu N | 스핀이면 **타임슬라이스**, 잠들기면 같은 코어 스위치 | 스핀은 ms 단위 — 쓰면 안 되는 배치 |
| ② SMT 형제 | cpu N, N' (`thread_siblings_list`) | L1 공유 — 가장 빠른 핸드오프 | x86 에만. Arm Cortex/Apple 코어는 SMT 없음 |
| ③ 같은 클러스터 | `cluster_cpus_list` 안 | L2 공유 | Cortex-A55/A78 DSU, 또는 big 끼리 |
| ④ 다른 클러스터 | big ↔ LITTLE | 클러스터 간 일관성 | 이 맥의 P↔E 에 해당 |
| ⑤ 다른 소켓/NUMA | `numactl -H` | 인터커넥트 | 서버만 |

준비: `lscpu -e`, `/sys/devices/system/cpu/cpu*/topology/`, 거버너 `performance`(DVFS 고정), `isolcpus=`/`nohz_full=` 로 다른 작업 치우기, 두 스레드 모두 `SCHED_FIFO`(선택). 각 칸 spin 과 cond 를 3 run 씩, min/p50/p99 와 `uptime`.

Lima VM(`elinux`, aarch64 Ubuntu, 커널 7.0.0, vCPU 6개, gcc 15.2)에서 실제로 돌린 결과 — **"CPU" 는 vCPU(호스트의 스레드)이지 물리 코어가 아니다.** `lscpu -e` 가 보여 주는 토폴로지도 평평하다(클러스터 1개, 코어마다 L2 0):

```text
$ lscpu -e
CPU NODE CLUSTER CORE L1d:L1i:L2 ONLINE
  0    0       0    0 0:0:0         yes
  1    0       0    1 1:1:0         yes
  ...
$ ./aff        (VM load average 0.10; 호스트 load ≈ 4.5)
spin cpu0<->cpu1  p50      91.8  p99      108.7  min     90.5 ns/RT   (2000 x 64 RT, 13.3 ms)
spin cpu0<->cpu2  p50      91.8  p99      111.3  min     90.5 ns/RT   (2000 x 64 RT, 14.9 ms)
spin cpu0<->cpu5  p50      91.8  p99      106.8  min     90.5 ns/RT   (2000 x 64 RT, 12.0 ms)
spin cpu0<->cpu0  p50 5999776.0  p99  6006193.0  min 3029138.0 ns/RT   (20 x 1 RT, 112.6 ms)
cond cpu0<->cpu0  p50    3906.2  p99     4562.5  min   1854.2 ns/RT   (1000 x 4 RT, 14.9 ms)
cond cpu0<->cpu1  p50   14479.2  p99    17698.0  min  13968.8 ns/RT   (1000 x 4 RT, 59.6 ms)
cond cpu0<->cpu5  p50   14437.5  p99    17833.5  min  13979.2 ns/RT   (1000 x 4 RT, 59.5 ms)
```

- **다른 vCPU 끼리 스핀 92 ns** — 맥의 P–P(80 ns)와 비슷하다. vCPU 스레드가 호스트 P 코어에서 돌고 있었을 것으로 본다. 어느 vCPU 쌍이든 같다 → VM 안에서는 ③/④ 를 구분할 수 없다.
- **같은 vCPU 에 스핀 둘 = 왕복 6 ms.** 상대가 돌 차례를 기다리느라 한쪽이 타임슬라이스를 다 쓴다. 이 VM 의 `base_slice_ns` 는 2100000(2.1 ms), `CONFIG_HZ=1000` 이라 틱에 맞춰 약 3 ms 씩 두 번(min 3.03 ms) 으로 읽힌다. 스레드가 코어보다 많을 때 순수 스핀이 재앙인 이유 — 그래서 체크용 `spin_until_eq` 는 4096번마다 `sched_yield` 를 한다.
- **cond: 같은 vCPU 3.9 µs, 다른 vCPU 14.4 µs.** VM 에서는 쉬는 vCPU 를 깨우려면 호스트가 그 vCPU 스레드를 다시 돌려야 해서(가상 IPI) 크게 부풀려진다. OS-M02 의 파이프 결과(같은 vCPU 1.7 µs vs 다른 vCPU 15 µs)와 같은 모양. 실기 리눅스에서는 차이가 훨씬 작을 것이다 — **이 표를 물리 코어 결과로 인용하지 말 것.**

### Part C — 메일박스: 두 라인, release/acquire

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">메일박스: 요청 라인은 클라이언트만, 응답 라인은 서버만 쓴다</text>
  <rect x="20" y="40" width="150" height="160" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="32" y="62" fill="currentColor" font-weight="bold">코어 0 (클라이언트)</text>
  <text x="32" y="86" fill="currentColor" font-size="12">① cmd, arg[4] 쓰기</text>
  <text x="32" y="106" fill="currentColor" font-size="12">② req.seq = s (release)</text>
  <text x="32" y="126" fill="currentColor" font-size="12">⑥ rsp.seq == s ? (acquire)</text>
  <text x="32" y="146" fill="currentColor" font-size="12">⑦ result 읽기</text>
  <text x="32" y="166" fill="currentColor" font-size="12">→ 다음 요청은 ⑦ 뒤에만</text>
  <rect x="570" y="40" width="150" height="160" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="582" y="62" fill="currentColor" font-weight="bold">코어 1 (서버)</text>
  <text x="582" y="86" fill="currentColor" font-size="12">③ req.seq == last+1 ?</text>
  <text x="582" y="106" fill="currentColor" font-size="12">④ payload 읽고 계산</text>
  <text x="582" y="126" fill="currentColor" font-size="12">⑤ result 쓰고</text>
  <text x="582" y="146" fill="currentColor" font-size="12">   rsp.seq = s (release)</text>
  <rect x="250" y="44" width="240" height="60" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="262" y="66" fill="currentColor" font-family="monospace" font-size="12">req 라인 (128 B)</text>
  <text x="262" y="86" fill="currentColor" font-family="monospace" font-size="12">seq | cmd | arg[0..3]</text>
  <rect x="250" y="130" width="240" height="60" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="262" y="152" fill="currentColor" font-family="monospace" font-size="12">rsp 라인 (128 B)</text>
  <text x="262" y="172" fill="currentColor" font-family="monospace" font-size="12">seq | status | result</text>
  <path d="M170 80 L250 80 M490 80 L570 80" stroke="currentColor" fill="none"/>
  <path d="M570 150 L490 150 M250 150 L170 150" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="20" y="226" fill="currentColor" font-size="12">MCU: RP2040 은 SIO FIFO + SEV/WFE, STM32H7 은 HSEM + 공유 SRAM(M7 D-cache 관리 필요), i.MX8M 은 MU(Messaging Unit).</text>
  <text x="20" y="244" fill="currentColor" font-size="12">원리는 같다: payload 먼저, 순번/도어벨은 나중 (release), 받는 쪽은 순번 먼저 (acquire).</text>
</svg>
```

- **왜 두 라인인가:** 각 라인의 writer 가 하나다. 요청 라인은 클라이언트만, 응답 라인은 서버만 쓴다. 한 라인에 두면 서버가 응답을 쓰는 동안 클라이언트의 다음 요청 쓰기와 같은 라인을 다툰다 (지금의 엄격한 번갈아 쓰기에서는 차이가 작지만 — 위 d8 결과 — 요청을 미리 쌓는 큐로 확장하면 바로 문제가 된다, LLAT-M01 SPSC).
- **payload 가 평범한 변수여도 되는 이유:** 클라이언트의 payload 쓰기 → `req.seq` release store, 서버의 `req.seq` acquire load → payload 읽기. release/acquire 가 synchronizes-with 를 만들어 payload 쓰기가 읽기보다 happens-before 다 (C11 5.1.2.4). 다음 payload 쓰기는 `rsp.seq` acquire 뒤라서 서버의 이전 읽기와도 순서가 있다. 그래서 TSan 이 조용하다 (아래 검증).
- **깨뜨리면:** `demo_broken.c` 는 네 개의 seq 연산을 전부 relaxed 로 바꿨다.

```text
$ ./broken 2000000        (load average 8.14)
relaxed mailbox: 2000000 calls, wrong results 2000000
relaxed mailbox: 2000000 calls, wrong results 2000000
relaxed mailbox: 2000000 calls, wrong results 2000000
```

**200만 번 전부 틀렸다** — 하드웨어 재배치가 아니라 **컴파일러** 다. `cc -O2 -S demo_broken.c` 의 `main` (실제 출력 발췌):

```text
LBB0_5:                         ; 호출 루프
	eor	w13, w14, w14, lsl #13    ; arg 4개 계산 (레지스터에만)
	...
	str	x9, [x10, _g_m.0@PAGEOFF]     ; req.seq = s   (relaxed → 평범한 str)
LBB0_6:
	ldr	x0, [x11, _g_m.5@PAGEOFF]     ; rsp.seq 돌기
	cmp	x0, x9
	b.ne	LBB0_6
	...
	b.ls	LBB0_5
	str	w13, [x8, _g_m.1@PAGEOFF]     ; arg[0..3] 저장은 루프가 끝난 뒤 한 번!
	str	w15, [x8, _g_m.2@PAGEOFF]
```

relaxed atomic 은 다른 변수의 접근 순서를 묶지 않으므로, 컴파일러가 payload 저장을 루프 밖으로 빼서 **마지막에 한 번만** 썼다(전역 구조체가 `_g_m.0`… 로 쪼개진 것도 보인다). 서버는 매번 0 인 payload 를 읽는다. TSan 빌드는 바로 잡았다:

```text
$ ./broken_tsan 1000
WARNING: ThreadSanitizer: data race (pid=86200)
  Read of size 4 at 0x000104834088 by thread T1:
    #0 server demo_broken.c:29 (broken_tsan:arm64+0x100000850)
  Previous write of size 4 at 0x000104834088 by main thread:
    #0 main demo_broken.c:42 (broken_tsan:arm64+0x100000724)
  Location is global 'g_m' at 0x000104834080 (broken_tsan+0x100008088)
  …
SUMMARY: ThreadSanitizer: data race demo_broken.c:29 in server
(exit 134)
```

펌웨어에서도 똑같다: 공유 SRAM 메일박스에서 도어벨 레지스터 쓰기 앞에 payload 가 실제로 메모리에 있어야 한다 — C 에서는 release(또는 `__DMB()` + volatile 도어벨), 하드웨어에서는 Cortex-M7 D-cache 가 켜져 있으면 **clean** 까지 (STM32H7 의 M7↔M4 공유 메모리는 하드웨어 일관성이 없다 — 문서 기준, C-A02 / MEM 트랙). RP2040 은 SRAM 에 데이터 캐시가 없고 코어 간 32비트 하드웨어 FIFO(SIO)와 SEV/WFE 가 있다 (RP2040 datasheet, SIO 절 — 문서 기준).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution       # 33/33 passed (6번 실행 모두)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_asan && ./so_asan   # 33/33, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan           # 33/33, 보고 없음 (1.2초)
```

- `rt_stats`: 복사 → `qsort` → 인덱스 `0`, `(m−1)/2`, `(99m+99)/100 − 1`, `m−1`. 곱은 `unsigned __int128` (clang/gcc 확장 — 표준 C 에는 없다. 이식성이 필요하면 `t/div*K + (t%div)*K/div` 로 나눠 곱한다).
- `spin_until_eq`: `i & 4095` 마다만 시계를 읽어서 스핀 경로에는 `ldapr; cmp; b.ne` 와 카운터만 남는다(위 asm).
- `blk_wait_eq`: `os_sync_wait_on_address_with_timeout(p, v, 8, NONE, OS_CLOCK_MACH_ABSOLUTE_TIME, left)`. 헤더 주석대로 EINTR/EFAULT 는 다시 읽고 재시도, ETIMEDOUT 은 마감 확인으로 돌아간다. 한 번에 최대 50 ms 만 잔다.
- `blk_post`: **store 먼저, wake 나중.** 반대로 하면 깨어난 쪽이 아직 옛 값을 보고 다시 잠들고, 그 뒤의 store 는 아무도 깨우지 않는다 (lost wakeup). 기다리는 이가 없으면 wake 는 `ENOENT` 를 돌려줄 뿐이지만 **시스템 콜 한 번** 이다 — 대기자 수를 세서 0 이면 wake 를 건너뛰는 최적화는 MCPP-A03 / OS-A01 (futex 0/1/2 상태).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 (이 맥 실측) | 고친 것 |
|---|---|---|
| 왕복마다 시계를 읽는다 | 41.67 ns 눈금 → min 0.0, p50 83.3 같은 tick 배수만 보임 | 64 왕복을 한 표본으로, ÷64 |
| 첫 run 을 그대로 쓴다 | 시작 직후 `E% 100/100`, 150–187 ns | 0.1초 워밍업 버리기, run 여러 번 |
| "QoS 로 P 코어에 고정했다" | ui 핑퐁이 표본 44%/78% 를 E 에서 보낸 run, bg 빈 루프가 Super 에만 | 코어 번호를 기록하고 쌍 종류별로 나눠 보고 |
| 깃발 패딩 64 B 면 충분 | E 코어가 끼면 64–120 B 도 8–20 ns/op | 128 B (`hw.cachelinesize`) 이상, C++ 은 256 |
| 핑퐁 결과로 "패딩 효과 없음" 결론 | 번갈아 쓰기에서는 원래 차이가 없다 (d8 ≈ d128) | 동시에 각자 쓰는 경우(fs)로 따로 잰다 |
| 코어보다 많은 스레드에서 순수 스핀 | VM 같은 vCPU: 왕복 6 ms (×65000) | spin-then-yield / spin-then-park |
| relaxed seq + 평범한 payload | 컴파일러가 payload 저장을 루프 밖으로: 200만/200만 틀림, TSan race | seq 는 release store / acquire load |
| `os_sync_wait_on_address(p, want, ...)` | "이미 원하는 값이면 잠든다" — 반대 의미 | 방금 읽은 **현재 값 v** 를 넘긴다 |
| wake 를 store 보다 먼저 | 깨어난 쪽이 옛 값을 보고 다시 잠듦 → 영원히 | store(release) → wake |
| VM 숫자를 물리 코어 숫자로 인용 | 다른 vCPU cond 14.4 µs 는 가상 IPI 로 부풀려짐 | "vCPU" 라고 쓰고, 실기에서 다시 잰다 |

고친 코드 (잠드는 대기, 정답에서):

```c
uint64_t v = atomic_load_explicit(p, memory_order_acquire);
if (v == want) return true;
/* ... 마감 계산 ... */
os_sync_wait_on_address_with_timeout((void *)p, v, 8, OS_SYNC_WAIT_ON_ADDRESS_NONE,
                                     OS_CLOCK_MACH_ABSOLUTE_TIME, left);   /* v: "아직 v 이면 자라" */
```

## 8. 면접 Q&A

<details>
<summary>Q1. 두 코어 사이 핸드오프 지연은 얼마이고, 어떻게 쟀나요?</summary>

두 스레드가 128 B 정렬된 64비트 `seq` 하나로 핑퐁(핑 홀수, 퐁 짝수, release store / acquire load 스핀). 64 왕복을 `mach_absolute_time` 두 번 사이에 넣어 한 표본으로, 2000 표본 중 앞 10% 버리고 min/p50/p99. Apple M6 에서 Super/Performance 코어끼리 왕복 77–90 ns(한 방향 약 40 ns), E 코어끼리 79–250 ns(클럭에 따라), P↔E 200–400 ns. 다른 세션의 벤치마크와 동시에 재서 load 4 안팎 — 배수로 말한다. 꼬리: "왜 왕복으로 재나?" → 한 방향은 송신 시각과 수신 시각을 다른 코어에서 읽어야 하고, 한 번에 1 tick(42 ns) 이라 묶을 수 없다. 이 맥의 `mach_absolute_time` 은 시스템 공용 카운터라 코어 간 뺄셈은 가능하지만(연습 4: 중앙값 1 tick) 해상도가 모자라다.
</details>

<details>
<summary>Q2. 같은 캐시 라인 vs 다른 라인 — 핑퐁에서 패딩하면 빨라지나요?</summary>

엄격히 번갈아 쓰는 핑퐁에서는 거의 안 바뀐다 (실측 d8 85.9 / d64 86.6 / d128 77.5 / d256 78.8 ns, run 간 흔들림과 비슷). 왕복마다 "내 쓰기가 상대에게 보이기" 두 번의 라인 이동이 필요하고, 기다리는 쪽은 쓰지 않기 때문이다. 패딩이 결정적인 것은 두 코어가 **동시에 각자** 쓸 때다: 각자 `fetch_add` 하는 두 카운터가 같은 블록이면 10–12 ns/op, 떨어지면 1.4 ns/op (7–8배). 꼬리: "얼마나 떨어뜨리나?" → 이 맥에서 P 코어끼리는 64 B 에서 이미 깨끗했지만 E 코어가 끼면 128 B 가 필요했다. 그래서 `hw.cachelinesize`(128) 이상, C++ 은 `hardware_destructive_interference_size`(이 clang 에서 256). Cortex-A 보드는 보통 64.
</details>

<details>
<summary>Q3. macOS 에서 스레드를 특정 코어에 고정할 수 있나요? QoS 는 무엇을 해 주나요?</summary>

고정은 못 한다. `sched_setaffinity` 가 없고 Mach affinity tag 는 Apple Silicon 에서 `KERN_NOT_SUPPORTED` 다 (OS-M02 확인). `pthread_set_qos_class_self_np` 는 "얼마나 급한 일인가" 를 알리는 힌트다. 실측: BACKGROUND 핑퐁은 매번 표본 100% 가 E 코어였지만, USER_INTERACTIVE 핑퐁도 부하가 있으면 표본 44–78% 를 E 에서 보냈고, BACKGROUND 빈 루프는 Super 코어에서만 돌았다. 그래서 측정은 `pthread_cpu_number_np` 로 코어를 기록하고 쌍 종류별로 나눈다. 꼬리: "그 cpu 번호 → 코어 종류 매핑은?" → 빈 루프 속도로 추정(0–5 E, 6–7 Super, 8–11 Performance). 문서로 확인한 매핑이 아니고, 읽은 순간 이후에 옮겨질 수 있다.
</details>

<details>
<summary>Q4. 스핀 대기와 잠드는 대기, 언제 무엇을 쓰나요?</summary>

실측 P 코어: 스핀 80 ns, `os_sync_wait_on_address`/조건 변수 2.5 µs (약 30배), E 코어에서 잠들기 7–10 µs. 스핀은 코어 두 개를 100% 태우고, 스레드가 코어보다 많으면 재앙이다 (리눅스 VM 같은 vCPU 에서 왕복 6 ms). 그래서: 코어를 전용으로 줄 수 있고 µs 미만이 필요하면 스핀(HFT 폴링 스레드, `isolcpus` 와 함께), 아니면 잠들기, 대부분은 **잠깐 스핀 후 잠들기** — 상대가 곧 올 확률이 높을 때 시스템 콜을 아낀다. 꼬리: "스핀의 전력을 줄이려면?" → arm64 는 `ldaxr` + `wfe` (라인이 바뀌면 이벤트로 깨어남, 이 맥에서 지연 79 ns 로 스핀과 같음), x86 은 `pause` / `umwait`. Cortex-M 듀얼 코어(RP2040)는 `SEV`/`WFE`.
</details>

<details>
<summary>Q5. 리눅스에서는 이 실험을 어떻게 설계하나요?</summary>

`pthread_setaffinity_np` 로 쌍을 고정해 행렬로: 같은 코어 / SMT 형제(x86) / 같은 클러스터(L2 공유) / 다른 클러스터(big.LITTLE) / 다른 소켓. 토폴로지는 `lscpu -e` 와 `/sys/devices/system/cpu/cpu*/topology/`. 잡음 제거: 거버너 `performance`, `isolcpus`/`nohz_full`, IRQ affinity 치우기, `SCHED_FIFO`. 각 칸 spin 과 cond, 3 run, min/p50/p99. VM 에서 돌려 봤더니 모든 vCPU 쌍이 92 ns 로 같았다 — vCPU 는 호스트 스레드라 토폴로지가 없다. 꼬리: "같은 코어에 둘을 묶으면?" → 스핀은 상대가 돌 차례를 기다리느라 타임슬라이스 단위(이 VM 6 ms), 잠들기는 같은 코어 스위치(3.9 µs)가 다른 vCPU 깨우기(14.4 µs)보다 쌌다. 실기에서는 그 차이가 훨씬 작다.
</details>

<details>
<summary>Q6. 메일박스를 설계해 보세요. 왜 요청/응답을 다른 라인에 두고, 왜 release/acquire 인가요?</summary>

요청 라인(seq, cmd, args)은 클라이언트만, 응답 라인(seq, status, result)은 서버만 쓴다 — 라인마다 writer 하나. 클라이언트: payload → `req.seq` release → `rsp.seq` acquire 대기 → 결과. 서버: `req.seq` acquire 대기 → 계산 → 결과 → `rsp.seq` release. 응답이 곧 "슬롯이 비었다" 는 신호라서 payload 를 덮어쓰지 않는다. relaxed 로 바꾸면? 이 맥 clang -O2 는 payload 저장을 루프 밖으로 빼서 200만 호출이 전부 틀렸고 TSan 이 data race 를 보고했다 — 하드웨어가 아니라 컴파일러가 먼저 깬다. 꼬리: "MCU 에서는?" → 같은 순서 + 캐시: Cortex-M7 D-cache 가 공유 영역을 캐시하면 clean/invalidate 또는 MPU 로 non-cacheable, 그리고 도어벨(IPCC/MU/HSEM 인터럽트)을 쓰기 전 `DMB`. 큐로 늘리면 SPSC 링(LLAT-M01).
</details>

<details>
<summary>Q7. 왕복 80 ns 를 41.67 ns 시계로 재면 무엇이 틀어지나요? 배치의 단점은?</summary>

왕복 하나를 재면 값이 0·41.7·83.3·125 ns 로만 나온다 (실측 batch 1: min 0.0, p50 83.3). 64개를 묶으면 표본 5.5 µs, 해상도 0.65 ns/왕복으로 한 눈금 오차가 1% 아래. 단점: 표본이 64개의 평균이라 **꼬리가 눌린다** — 한 번 800 ns 걸린 왕복이 표본 안에서 +11 ns 로 희석된다. 꼬리를 보려면 cycle 카운터(리눅스 `perf_event`/`cntvct_el0` 직접, Cortex-M `DWT->CYCCNT`)나 히스토그램을 쓰고, 그래도 안 되면 "p99 는 배치 평균의 p99" 라고 밝힌다. 꼬리: "시계 읽기 비용은?" → `mach_absolute_time` 약 6 ns(OS-M02) — 64 왕복(5.5 µs) 대비 0.1%.
</details>

<details>
<summary>Q8. 이 숫자로 실제로 무엇을 결정하나요?</summary>

(1) 파이프라인을 몇 스레드로 나눌지: 단계마다 핸드오프가 80 ns(스핀) ~ 2.5 µs(잠들기)이면, 1 µs 짜리 작업을 스레드 둘로 나누는 건 잠들기 핸드오프에서는 손해다. (2) 코어 배치: 생산자–소비자를 같은 클러스터에 두면 P↔E 대비 3배 빠르다 — 리눅스는 affinity, 맥은 같은 QoS. (3) 배치 크기: 메시지당 2.5 µs 를 감당 못 하면 N 개씩 묶는다 (OS-M02 Part C). (4) 듀얼 코어 MCU: 코어 간 FIFO/메일박스 지연 + 인터럽트 진입이 제어 루프 주기에 들어가는지. 꼬리: "E 코어에 밀린 소비자는?" → 왕복 3–4배, 잠들기 7–10 µs. 지연 민감 스레드는 높은 QoS, 리눅스는 SCHED_FIFO + affinity.
</details>

## 9. 추가 연습

### 연습 1 — 세 스레드 토큰 링

캐시 라인 하나에 토큰을 두고 스레드 0 → 1 → 2 → 0 … 순서로 넘긴다. 스레드 i 는 라운드 r 에 토큰이 `3r + i` 가 되면 받고 +1 해서 넘긴다. 1만 라운드 후 각자 정확히 1만 번 받았고 토큰은 3만인지 확인하라.

<details>
<summary>답</summary>

```c
for (uint64_t r = 0; r < a->rounds; r++) {
    uint64_t want = r * (uint64_t)a->n + (uint64_t)a->id;
    if (!spin_until_eq(a->tok, want, a->deadline)) return NULL;
    a->got++;
    atomic_store_explicit(a->tok, want + 1, memory_order_release);
}
```

핑퐁의 일반화다. 토큰 하나당 라인 이동 1번이라 한 바퀴 = 이동 3번. 세 코어가 같은 클러스터면 한 바퀴 ≈ 왕복의 1.5배가 예상값이다 (재 보지는 않았다 — 직접 `demo_pingpong.c` 에 추가해 볼 것). 정확히 한 번 = 기다리는 값이 정확히 하나.
</details>

### 연습 2 — 조건 변수 핸드오프

`cv_wait_eq(box, want, deadline)` / `cv_post(box, v)` 를 mutex + cond 로 만들고 3000 왕복 핑퐁. 안 오는 값은 20 ms 마감에 false.

<details>
<summary>답</summary>

```c
pthread_mutex_lock(&b->mu);
while (b->val != want) {
    /* left = deadline - now, 0 이하면 실패 */
    int rc = pthread_cond_timedwait_relative_np(&b->cv, &b->mu, &rel);
    if (rc != 0 && rc != ETIMEDOUT) { ok = false; break; }
}
pthread_mutex_unlock(&b->mu);
```

`while` 은 가짜 깨움과 "다른 값" 둘 다를 막는다 (OS-B06). post 는 **락 안에서** 값을 바꾸고 signal — 락이 "확인 → 잠들기" 사이를 막아 lost wakeup 이 없다. `pthread_cond_timedwait_relative_np` 는 macOS 전용(상대 시간). POSIX 는 `pthread_cond_timedwait` + 절대 시각(기본 `CLOCK_REALTIME`). 실측 왕복 2.5 µs 로 `os_sync_wait_on_address` 와 같은 대였다.
</details>

### 연습 3 — WFE 로 기다리기

`sevl` 로 이벤트를 켜 두고, 루프마다 `wfe` → `ldaxr` → 비교. 맞으면 `clrex` 하고 true. 1024번마다 마감 확인.

<details>
<summary>답</summary>

```c
__asm__ volatile("sevl" ::: "memory");
for (uint32_t i = 1;; i++) {
    uint64_t v;
    __asm__ volatile("wfe" ::: "memory");
    __asm__ volatile("ldaxr %0, [%1]" : "=r"(v) : "r"(p) : "memory");
    if (v == want) { __asm__ volatile("clrex" ::: "memory"); return true; }
    if ((i & 1023) == 0 && now_ns() > deadline_ns) { __asm__ volatile("clrex" ::: "memory"); return false; }
}
```

`ldaxr` 가 걸어 둔 exclusive 모니터가 다른 코어의 쓰기로 풀리면 이벤트가 생겨 `wfe` 가 깬다. `sevl` 이 없으면 첫 `wfe` 가 아직 모니터를 걸지도 않은 상태에서 잠든다. 비교와 `wfe` 사이에 쓰기가 와도 이벤트 레지스터가 켜져서 다음 `wfe` 가 바로 통과 — 놓치지 않는다. 마감 확인이 동작하는 건 이 맥에서 `wfe` 가 아무도 안 써도 약 1.3 µs 마다 깨어나기 때문이다 (실측, 원인 미확인). 그런 주기적 깨움이 없는 시스템에서는 마감을 보장하려면 이벤트 스트림(리눅스 `evtstrm`, VM 의 `lscpu` Flags 에 있었다)이나 타이머가 필요하다. TSan 은 인라인 asm 의 `ldaxr` 를 못 보지만, 이 연습의 공유 접근은 store 쪽이 C11 release 라서 TSan 빌드도 보고 없이 통과했다.
</details>

### 연습 4 — 타임스탬프를 싣는 한 방향 기록

핑이 `ts = mach_absolute_time()`, `tag = k` 를 쓰고 `seq = k`(release). 퐁은 `seq == k` 를 보면 `now − ts` 와 `tag` 를 기록하고 `ack = k`. 핑은 ack 를 본 뒤에만 다음 ts 를 쓴다. 2만 번: 모든 tag 가 그 번호인지, ts 가 줄지 않는지 확인하고, 한 방향 중앙값을 출력(체크 아님).

<details>
<summary>답</summary>

```c
static bool ow_send(ow_t *o, uint64_t k, uint64_t dl) {
    o->ts = mach_absolute_time();
    o->tag = k;
    atomic_store_explicit(&o->seq, k, memory_order_release);
    return spin_until_eq(&o->ack, k, dl);
}
```

실측 출력: `(info) one-way median 1 ticks = 42 ns, p99 83 ns` (6번 실행 모두 1 tick). 왕복 80 ns 의 절반과 맞지만 **한 눈금이 측정값 전체** 라서 이 이상 정밀하게는 못 잰다 — 그래서 왕복으로 잰다. ts 는 "엄격히 증가" 가 아니라 "줄지 않음" 으로 검사한다: 왕복이 약 2 tick 이라 연속 두 메시지의 ts 가 같은 tick 일 수 있다 (처음에 `<=` 로 썼다가 실제로 실패했다). TSan 빌드에서는 중앙값이 12–13 tick(500 ns)으로 늘었다 — 계측 비용.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice                                    # 11/11 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_asan && ./pr_asan     # 11/11, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_tsan && ./pr_tsan                                       # 11/11, 보고 없음
```

## 10. 핵심 정리

- 핸드오프 = 캐시 라인 이동. 왕복 1번 = 이동 2번. 재는 법: seq 하나 핑퐁(핑 홀수 / 퐁 짝수, `==` 로 정확히 한 번), 64 왕복씩 묶어 ÷64 (41.67 ns 눈금), 워밍업 버림, min/p50/p99 + load.
- Apple M6 실측 (load ≈ 4, 배수로 볼 것): Super/P 끼리 77–90 ns, E 끼리 79–250 ns (클럭 따라), P↔E 200–400 ns, WFE ≈ 스핀, 잠들기(`os_sync_wait_on_address`·cond) 2.5 µs (E 에서 7–10 µs).
- macOS 는 고정 불가. QoS 는 힌트 — BACKGROUND → E 는 믿을 만했고, USER_INTERACTIVE 도 부하에 따라 E 로 갔다. cpu 번호를 기록해 쌍 종류별로 나눈다.
- 번갈아 쓰는 핑퐁에서 패딩은 거의 무관. 각자 쓰면 같은 블록이 7–14배 느림 — 이 맥 경계 P 64 B / E 128 B → **128 B 이상** (C++ 256).
- 코어보다 많은 스레드에서 순수 스핀은 재앙 (VM 같은 vCPU 6 ms). spin-then-yield / spin-then-park.
- 잠들기 API 에는 **방금 읽은 값** 을 넘기고(커널이 원자적으로 비교 → lost wakeup 없음), post 는 store 뒤 wake.
- 메일박스: 요청/응답 라인 분리(라인당 writer 하나), payload → seq release / seq acquire → payload. relaxed 는 컴파일러가 먼저 깬다 (200만/200만).
- 리눅스: `pthread_setaffinity_np` 행렬(같은 코어 / SMT / 클러스터 / 크로스 클러스터 / 소켓) + 거버너·isolcpus. VM 의 vCPU 숫자는 물리 코어 숫자가 아니다.

## 참고자료

- Apple `os/os_sync_wait_on_address.h` (MacOSX26.4.sdk, `__API_AVAILABLE(macos(14.4))`) — 원자적 compare-and-wait, EINTR/EFAULT 재시도 규칙, `_with_timeout`/`_with_deadline`, `os_sync_wake_by_address_any/all`
- Apple `sys/qos.h`, `pthread/qos.h` (`pthread_set_qos_class_self_np`, `qos_class_self`), `pthread_cpu_number_np` (`pthread/pthread.h` 주석: 읽은 값은 곧 낡을 수 있음); *Energy Efficiency Guide for Mac Apps — Prioritize Work with Quality of Service Classes*: <https://developer.apple.com/library/archive/documentation/Performance/Conceptual/power_efficiency_guidelines_osx/PrioritizeWorkAtTheTaskLevel.html>
- `man sysctl` — `hw.cachelinesize`, `hw.nperflevels`, `hw.perflevelN.{name,physicalcpu,cpusperl2,l2cachesize}`
- Arm Architecture Reference Manual for A-profile (DDI 0487) — WFE/SEVL 과 "Wait for Event mechanism", exclusive monitor 와 이벤트 생성, LDAPR/LDAR/STLR: <https://developer.arm.com/documentation/ddi0487/latest/>
- C11 (N1570) 5.1.2.4 Multi-threaded executions and data races (synchronizes-with, happens-before), 7.17.3 Order and consistency: <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Linux `pthread_setaffinity_np(3)`, `sched_setaffinity(2)`, `taskset(1)`, `Documentation/admin-guide/cputopology.rst` (`/sys/devices/system/cpu/cpuN/topology/`): <https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html> · <https://docs.kernel.org/admin-guide/cputopology.html>
- Linux arm64 `arch/arm64/include/asm/barrier.h` (`smp_cond_load_acquire` → `__cmpwait`), `arch/arm64/include/asm/cmpxchg.h`: <https://elixir.bootlin.com/linux/latest/source/arch/arm64/include/asm/barrier.h>
- Paul E. McKenney, *Is Parallel Programming Hard...* — 3장 Hardware and its Habits (캐시 라인 이동 비용): <https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook.html>
- RP2040 Datasheet — SIO: inter-processor FIFOs, spinlocks; SEV/WFE: <https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf>
- ST AN5617 *STM32H745/755 and STM32H747/757 lines inter-processor communications* (HSEM, 공유 메모리, 캐시 유지): <https://www.st.com/resource/en/application_note/an5617-stm32h745755-and-stm32h747757-lines-interprocessor-communications-stmicroelectronics.pdf>
- 관련 레슨: OS-M02 (파이프 핑퐁, 시계, 코어 번호, affinity tag), LLAT-B06 (원자 연산 비용·경합), OS-M07 (스핀락 라인 핑퐁), OS-A01 (futex), MCPP-A03 (atomic wait/notify), LLAT-M02 (seqlock), LLAT-M01 (SPSC), C-A03 (release/acquire publish), C-A02 (컴파일러 배리어), ARM-M04 / ARM-M05 (LDXR/STXR, LDAR/LDAPR), ARCH-M08 (false sharing), OS-B06 (조건 변수), RTOS-A03 (DWT 사이클 측정)
