---
id: OS-A01
time: 110분
updated: 2026-11-08
---
# OS-A01 futex 기반 뮤텍스 구현 (Drepper 방식)

## 학습 목표

- `syscall(SYS_futex, …)` 로 `FUTEX_WAIT_PRIVATE` / `FUTEX_WAKE_PRIVATE` 를 직접 부르고, 반환값 `0 / -EAGAIN / -EINTR / -ETIMEDOUT` 을 각각 어떻게 처리해야 하는지 말할 수 있다.
- Drepper "Futexes Are Tricky" 의 **0/1/2 뮤텍스(mutex2)** 를 짜고, 각 CAS 가 왜 그 값인지(특히 "다시 잡을 때 1 이 아니라 2") 상태 기계로 설명한다.
- 순진한 0/1 버전의 **lost wakeup** 을 강제 스케줄로 매번 재현하고, "FUTEX_WAIT 가 커널 안에서 값을 원자적으로 비교한다" 가 그것을 어떻게 없애는지 보인다.
- 경합 없는 lock/unlock 의 syscall 수(0 vs 1)와 glibc `pthread_mutex_t` 대비 성능을 **실측 숫자**로 비교하고, glibc 가 실제로 같은 0/1/2 규칙을 쓰는 증거(objdump·strace·단어 값)를 보인다.
- RTOS 뮤텍스(FreeRTOS 는 매번 크리티컬 섹션), Zephyr, PREEMPT_RT / `FUTEX_LOCK_PI` 와 연결한다.

## 1. 문제

"pthread_mutex_lock 은 경합이 없으면 커널에 들어가나요?" — 리눅스 시스템/임베디드 리눅스 면접의 단골 질문이고, 답은 **"아니오, 원자 연산 하나로 끝납니다. 경합일 때만 futex syscall 로 잠듭니다"** 다. 그 한 문장 뒤에는 lost wakeup, 헛 syscall, 시그널(EINTR), 타임아웃 처리 같은 함정이 줄줄이 있어서 Drepper 가 논문 제목을 "Futexes Are **Tricky**" 로 붙였다. 펌웨어 쪽에서 보면 정반대 설계가 흔하다: FreeRTOS `xSemaphoreTake()` 는 경합이 없어도 **매번** 커널 코드에 들어가 크리티컬 섹션(인터럽트 마스크)을 연다. "빠른 길은 사용자 공간, 느린 길만 커널" 이라는 아이디어가 MMU·특권 분리가 있는 시스템에서 왜 큰 이득인지가 이 레슨의 핵심이다. 4층(락을 직접 만들기)의 두 번째 칸이다. OS-M07 의 스핀락은 기다리는 동안 CPU 를 태웠다. 여기서는 기다리는 스레드를 **재운다**. 원자 연산 자체(C11 `stdatomic`, CAS, acquire/release)는 C-A03, LSE vs LL/SC 비용은 ARM-M04 · LLAT-B06 에서 했으니 다시 하지 않는다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">futex = 사용자 공간 int 하나 + 커널 대기 큐 (주소로 찾는다)</text>
  <rect x="20" y="40" width="330" height="110" class="f-ok" stroke="currentColor"/>
  <text x="30" y="60" fill="currentColor" font-weight="bold">사용자 공간 (빠른 길)</text>
  <text x="30" y="82" fill="currentColor" font-family="monospace" font-size="12">lock:   cas(v, 0→1) 성공 → 끝</text>
  <text x="30" y="102" fill="currentColor" font-family="monospace" font-size="12">unlock: fetch_sub(v) 이전값 == 1 → 끝</text>
  <text x="30" y="126" fill="currentColor">경합 없음 = 원자 연산 1 개, syscall 0 개</text>
  <text x="30" y="144" fill="currentColor">(이 VM: lock+unlock 한 쌍 2.7 ns)</text>
  <rect x="390" y="40" width="330" height="110" class="f-warn" stroke="currentColor"/>
  <text x="400" y="60" fill="currentColor" font-weight="bold">커널 (느린 길, 경합일 때만)</text>
  <text x="400" y="82" fill="currentColor" font-family="monospace" font-size="12">FUTEX_WAIT(&amp;v, 2): v==2 면 잠듦</text>
  <text x="400" y="102" fill="currentColor" font-family="monospace" font-size="12">FUTEX_WAKE(&amp;v, 1): 하나 깨움</text>
  <text x="400" y="126" fill="currentColor">syscall 1 번 ≈ 150 ns (빈 큐 wake)</text>
  <text x="400" y="144" fill="currentColor">+ 잠들기/깨우기 = 문맥 교환 (µs)</text>
  <path d="M350 95 L390 95" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="140" y="180" width="460" height="40" class="f-mut" stroke="currentColor"/>
  <text x="155" y="205" fill="currentColor" font-family="monospace" font-size="12">int v : 0 = 풀림 · 1 = 잠김(대기자 없음) · 2 = 잠김(대기자 있을 수 있음)</text>
  <path d="M185 150 L240 180" stroke="currentColor"/><path d="M555 150 L500 180" stroke="currentColor"/>
  <text x="20" y="250" fill="currentColor">커널은 v 의 뜻을 모른다: "주소" 와 "이 값이면 재워 줘" 뿐.</text>
  <text x="20" y="272" fill="currentColor">상태 2 덕분에 unlock 은 대기자가 있을 때만 syscall (mutex2).</text>
  <text x="20" y="292" fill="currentColor">비교 + 잠들기를 커널이 한 번에 → lost wakeup 이 없다.</text>
</svg>
```

**환경 (이 레슨의 모든 리눅스 숫자)**: Lima VM `elinux` (vz, aarch64, 6 vCPU, 12 GiB) · Ubuntu 26.04.1 LTS · kernel 7.0.0-38-generic (`PREEMPT_DYNAMIC`, `CONFIG_FUTEX=y`, `CONFIG_FUTEX_PI=y`) · glibc 2.43 · gcc 15.2.0 · clang 21.1.8 · strace 6.19 · perf 7.0.14. 호스트: Apple M6 (`sysctl -n machdep.cpu.brand_string hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu hw.perflevel2.physicalcpu` → `Apple M6 / 2 / 4 / 6`), `cc --version` → `Apple clang version 21.0.0 (clang-2100.0.123.102)`. **VM vCPU 는 물리 코어가 아니다** — 호스트에서 다른 세션 6개가 동시에 벤치마크를 돌렸다(맥 load average 3.6–5.4). 절대 ns 보다 **비율**을 믿을 것.

참고 문헌 버전: Ulrich Drepper, *Futexes Are Tricky*, **Version 1.6, November 5, 2011** (akkadia.org PDF 를 내려받아 확인; §4 Mutex Take 1, §5 Take 2 = `mutex2`, §6 Take 3 = `mutex3`). 아래 코드의 `mutex2` 는 그 §5 코드를 C11 atomics 로 옮긴 것이다.

### Part A — futex 래퍼 + Drepper mutex2

- `futex_wait(addr, expected, rel)` / `futex_wake(addr, n)` — `syscall(SYS_futex, …, FUTEX_WAIT_PRIVATE / FUTEX_WAKE_PRIVATE, …)`. 반환은 `0` 또는 `-errno`. 모든 호출을 전역 카운터(`wait, wake, woken, eagain, eintr, timedout`)에 센다 — **syscall 수를 정확히 CHECK** 하기 위해서다.
- `mutex2_lock` / `mutex2_unlock` — 0/1/2. 경합 없으면 syscall 0. 잠들기 직전에 `HOOK(HK_BEFORE_WAIT)` 를 부른다 (강제 스케줄용, 벤치에서는 `NO_HOOKS` 로 컴파일에서 빠진다).
- 테스트: 값이 다르면 `-EAGAIN`, 같으면 시간 초과, 빈 큐 wake = 0명, 실제로 잠든 스레드(→ `/proc/self/task/<tid>/syscall` 로 확인)를 wake = 1명, 시그널 = `-EINTR`. mutex2 상태값 0→1→0, 대기자가 잠들면 2, 8 스레드 스트레스(임계 구역 동시 진입 0, 합계 정확).

### Part B — 순진한 0/1 버전 두 개를 깨뜨린다

- `naiveq` (주어진 코드): 상태 `v` 는 0/1, 잠드는 자리는 따로 둔 단어 `q`(항상 0). "v 를 보고 → q 에서 잔다" 가 분리돼 있다.
- `naive` (주어진 코드): 상태 단어 `v` 에서 `FUTEX_WAIT(&v, 1)`. 정확하지만 unlock 마다 `FUTEX_WAKE`.
- **TODO** `forced_schedule(ops, out)` — ① main 이 잡고 ② 대기자 W 가 실패해서 "잠들기 직전" 훅에서 멈추고 ③ 그 창에서 main 이 unlock+wake ④ W 를 놓아준다 ⑤ join. 결과 syscall 차이를 돌려준다.
- **TODO** `count_uncontended(ops, n)` — 경합 없는 n 쌍의 syscall 수.
- 판정: `naiveq` 는 watchdog(300 ms) 타임아웃 1번 = lost wakeup. `naive`·`mutex2` 는 `-EAGAIN` 1번, 타임아웃 0. 1000 쌍: `naive` wake 1000번, `mutex2` 0번.

### Part C — IMU 데몬의 I2C 버스 락: timedlock

i.MX8 급 리눅스 보드의 센서 데몬. 1 kHz 제어 스레드와 eMMC 로거 스레드가 I2C 버스 하나를 나눠 쓴다. 제어 스레드는 버스를 800 µs 안에 못 잡으면 **직전 샘플로 마감을 지킨다**.

- **TODO** `mutex2_timedlock(m, timeout_ns)` — 0 또는 `ETIMEDOUT`. 마감 시각을 정해 두고 잠들 때마다 남은 시간을 다시 계산한다. `EINTR` 은 실패가 아니다 → 남은 시간으로 다시 잔다.
- `ctrl_tick(imu, budget)` (주어진 코드) — timedlock 성공이면 새 값, 실패면 stale 카운트.
- 테스트: 자기가 쥔 락에 timedlock → `ETIMEDOUT` 후 상태 2 가 남고 다음 unlock 이 **헛 wake 1번**(무해), 예산 0 이면 syscall 없이 포기, 잠든 대기자에게 `SIGUSR1` → `EINTR` 을 세고 다시 잠들었다가 결국 획득.

## 2. Problem (English)

> Implement a mutex on Linux futexes that never enters the kernel when uncontended.
>
> ```c
> int  futex_wait(atomic_int *addr, int expected, const struct timespec *rel); // 0, -EAGAIN, -EINTR, -ETIMEDOUT
> int  futex_wake(atomic_int *addr, int n);                                    // #woken or -errno
> void mutex2_lock(lock_t *m);     // states: 0 unlocked, 1 locked, 2 locked + maybe waiters
> void mutex2_unlock(lock_t *m);
> int  mutex2_timedlock(lock_t *m, long timeout_ns);                           // 0 or ETIMEDOUT
> int  forced_schedule(const lock_ops_t *ops, sched_out_t *out);               // deterministic interleaving
> fsnap_t count_uncontended(const lock_ops_t *ops, int n);
> ```
>
> Constraints: use the raw `syscall(SYS_futex, ...)` with the `_PRIVATE` ops; the lock word is a 32-bit `int`;
> unlock may only issue `FUTEX_WAKE` when a waiter may exist; `EINTR` and spurious wake-ups must not make lock
> return without owning the mutex; timedlock must not extend its deadline after a signal.
> Then show that a naive 0/1 lock that checks the state in user space and sleeps on a separate word loses a
> wake-up under a forced schedule, and that a 0/1 lock waiting on the state word issues one `FUTEX_WAKE` per unlock.
> Example: 1000 uncontended lock/unlock pairs → `mutex2`: 0 syscalls; `naive`: 1000 `FUTEX_WAKE`.
> Follow-ups: why must the re-acquire after sleeping write 2, not 1? What does the kernel do to make
> "compare and sleep" atomic? Why `FUTEX_PRIVATE_FLAG`? How does glibc's `pthread_mutex_lock` compare?

## 3. 시작 코드

<!-- code: starter.c -->

**리눅스 전용** (`/* ENV: linux */` — 맥 검증기는 건너뛴다). 맥 홈은 VM 에 **읽기 전용**으로 마운트되므로 VM 의 `/tmp/OS-A01_work/` 로 복사해서 빌드한다.

```bash
limactl list                              # elinux 가 Running 인지 (Stopped 면 limactl start elinux)
limactl shell elinux                      # 이후 VM 안
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/OS/OS-A01
mkdir -p /tmp/OS-A01_work && cp $L/*.c /tmp/OS-A01_work/ && cd /tmp/OS-A01_work
gcc   -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st
clang -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st
```

`syscall()` · `gettid()` · `pthread_kill` 은 `-std=c11` 에서 `#define _GNU_SOURCE` 를 **include 앞에** 둬야 보인다. 시작 코드 실행 (끝부분, 실제 출력, 7.2 s 걸림 — 모든 대기에 상한이 있어 멈추지 않는다):

```text
== Part B: 강제 스케줄 — 잠들기 직전에 unlock 이 끼어들면 ==
FAIL  forced_schedule(&OPS_NAIVEQ, &so) == 0 && so.acquired  (line 354)
  naiveq: wait 0 wake 0 woken 0 eagain 0 timedout 0
...
FAIL  ctrl_tick(&imu, 800000L) == 1 && imu.last == 200 && imu.fresh == 2 && imu.stale == 1  (line 412)
7/34 passed
exit=1
```
(PASS 7개는 "syscall 0" 같은 테스트가 빈 스텁에 우연히 맞은 것.)

## 4. 힌트

<details>
<summary>힌트 1 — 래퍼: syscall() 은 -1 과 errno 를 돌려준다</summary>

`syscall(SYS_futex, (int *)addr, FUTEX_WAIT_PRIVATE, expected, rel, NULL, 0)` 은 libc 함수라서 실패하면 `-1` 을 돌려주고 `errno` 를 세팅한다. `errno` 는 다음 libc 호출이 덮어쓸 수 있으니 바로 지역 변수에 담고 `-e` 로 돌려준다. `FUTEX_WAKE` 의 성공 반환값은 "깨운 스레드 수" 다. `atomic_int *` → `int *` 캐스트는 크기가 같다는 `_Static_assert` 로 지킨다 (커널은 4바이트 정렬된 u32 를 읽는다).

</details>

<details>
<summary>힌트 2 — mutex2_lock 의 세 CAS</summary>

① `cas(0→1)`: 빠른 길. ② 실패해서 본 값 `c` 가 2 면 바로 잔다. 1 이면 `cas(1→2)` 로 "대기자 있음" 을 표시하는데, 이 CAS 가 **0 을 보면**(그새 풀림) 자지 말고 바로 ③ 으로. ③ `cas(0→2)` 로 다시 잡기 시도 — 0 이 아니면 루프. `cas` 는 성공·실패와 상관없이 **이전 값**을 돌려주게 만들면 Drepper 코드와 1:1 로 맞는다.

</details>

<details>
<summary>힌트 3 — unlock: atomic_dec 의 이전 값</summary>

`atomic_fetch_sub(&v, 1, release)` 의 이전 값이 1 이면 대기자 없음 → 끝. 2 였으면 지금 v 는 1 인데, 그대로 두면 아무도 못 잡는 "잠김" 으로 보이니 0 으로 내리고 `futex_wake(&v, 1)`. 그 사이 다른 스레드가 `cas(1→2)` 를 했어도 상관없다 — 그 스레드는 `FUTEX_WAIT(&v, 2)` 에서 v == 0 을 보고 `-EAGAIN` 으로 돌아온다.

</details>

<details>
<summary>힌트 4 — forced_schedule: 무엇을 기다리고 무엇을 풀어 주나</summary>

`gate_hook` 은 무장된 스레드가 처음으로 "잠들기 직전" 에 도착하면 `g_gate_at = 1` 을 세우고 `g_gate_go` 를 (최대 2 s) 기다린다. main 은 `wait_flag(&g_gate_at, 2000)` → unlock → `g_gate_go = 1` → join. 훅은 스레드가 없을 때만 설정·해제한다(`pthread_create`/`join` 이 순서를 만든다).

</details>

<details>
<summary>힌트 5 — timedlock: 상대 시간을 매번 다시</summary>

`FUTEX_WAIT` 의 timeout 은 **상대 시간**(CLOCK_MONOTONIC 기준)이다. EINTR 로 깨어나 처음의 30 ms 로 다시 자면 마감이 늘어진다. 마감 시각 `deadline = now + timeout` 을 한 번 정하고 잘 때마다 `left = deadline - now` 를 계산, `left <= 0` 이면 `ETIMEDOUT`. 포기할 때 v 를 2 → 1 로 되돌리려 하지 말 것 (다른 대기자가 있을 수 있다).

</details>

## 5. 풀이

### 0. futex 계약 — 이 레슨에 필요한 만큼

| 호출 | 하는 일 | 반환 |
|---|---|---|
| `FUTEX_WAIT(addr, val, rel)` | 해시 버킷 락을 잡고 `*addr` 를 읽어 `== val` 이면 대기 큐에 넣고 잠든다 | `0` 깨워짐(가짜 깨어남 포함) · `EAGAIN` 값이 달랐음 · `EINTR` 시그널 · `ETIMEDOUT` |
| `FUTEX_WAKE(addr, n)` | 같은 키로 잠든 스레드를 최대 n 명 깨운다 | 깨운 수 (0 일 수 있다) |
| `_PRIVATE` 플래그 (`FUTEX_PRIVATE_FLAG` = 128) | 키를 (mm, 가상주소) 로 만든다 → 공유 메모리 페이지/inode 조회 생략 | 같은 프로세스 스레드끼리만 |

Drepper 원문(§2)도 같은 말을 한다: "Before the thread is suspended the value of the futex variable is checked. If it does not have the same value as the val1 parameter the system call immediately returns with the error EWOULDBLOCK." (리눅스에서 `EWOULDBLOCK == EAGAIN`).

**왜 이 비교가 lost wakeup 을 없애나.** 대기자 쪽의 "값 읽기 + 큐에 넣기" 와 깨우는 쪽의 "큐에서 꺼내기" 가 **같은 해시 버킷 락** 아래에서 일어난다. 그래서 unlock 쪽이 `v = 0` 을 쓴 뒤 `FUTEX_WAKE` 를 하면, 대기자는 둘 중 하나다: (a) 이미 큐에 들어가 있었다 → wake 가 찾아서 깨운다. (b) 아직 안 들어갔다 → 커널 안에서 `v` 를 읽을 때 0 을 본다 → `EAGAIN`. "봤더니 1 이었는데 그 직후 풀려서 깨움을 놓치고 영원히 잠" 이 생길 틈이 없다. (커널 `kernel/futex/waitwake.c` 의 "Futex Ordering Guarantees" 주석이 이 순서와 `smp_mb()` 위치를 설명한다 — 문서 기준, 이 VM 커널 소스는 열어 보지 않았다. 이 VM 에서 잠든 스레드의 `/proc/<tid>/wchan` 은 `futex_do_wait` 였다.)

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">강제 스케줄: 대기자 W 가 "잠들기 직전" 에 멈춘 사이 main 이 unlock + wake</text>
  <text x="10" y="45" fill="currentColor" font-weight="bold">naiveq (검사는 v, 잠은 q)</text>
  <text x="10" y="70" fill="currentColor">main</text><text x="10" y="100" fill="currentColor">W</text>
  <rect x="60" y="58" width="90" height="20" class="f-mut" stroke="currentColor"/><text x="66" y="72" fill="currentColor">lock (v=1)</text>
  <rect x="80" y="88" width="120" height="20" class="f-mut" stroke="currentColor"/><text x="84" y="102" fill="currentColor">xchg(v)=1 실패</text>
  <rect x="210" y="88" width="70" height="20" class="f-warn" stroke="currentColor"/><text x="216" y="102" fill="currentColor">훅: 멈춤</text>
  <rect x="290" y="58" width="190" height="20" class="f-mut" stroke="currentColor"/><text x="296" y="72" fill="currentColor">v=0; WAKE(q) → 0 명</text>
  <rect x="490" y="88" width="230" height="20" class="f-warn" stroke="currentColor"/><text x="496" y="102" fill="currentColor">WAIT(q, 0): q==0 → 잠듦 … 깨울 사람 없음</text>
  <text x="490" y="126" fill="currentColor">→ 300 ms watchdog ETIMEDOUT = lost wakeup (timedout 1)</text>
  <text x="10" y="175" fill="currentColor" font-weight="bold">mutex2 (검사도 잠도 v)</text>
  <text x="10" y="200" fill="currentColor">main</text><text x="10" y="230" fill="currentColor">W</text>
  <rect x="60" y="188" width="90" height="20" class="f-mut" stroke="currentColor"/><text x="66" y="202" fill="currentColor">lock (v=1)</text>
  <rect x="80" y="218" width="120" height="20" class="f-mut" stroke="currentColor"/><text x="84" y="232" fill="currentColor">cas(1→2) v=2</text>
  <rect x="210" y="218" width="70" height="20" class="f-warn" stroke="currentColor"/><text x="216" y="232" fill="currentColor">훅: 멈춤</text>
  <rect x="290" y="188" width="190" height="20" class="f-mut" stroke="currentColor"/><text x="296" y="202" fill="currentColor">dec→1≠1: v=0; WAKE(v) → 0 명</text>
  <rect x="490" y="218" width="230" height="20" class="f-ok" stroke="currentColor"/><text x="496" y="232" fill="currentColor">WAIT(v, 2): 커널이 v==0 을 봄 → EAGAIN</text>
  <text x="490" y="256" fill="currentColor">→ cas(0→2) 성공, 즉시 획득 (eagain 1, timedout 0)</text>
  <text x="10" y="295" fill="currentColor">두 wake 모두 빈 큐를 깨웠다. 차이는 "이 값이면 재워 줘" 의 단어가</text>
  <text x="10" y="315" fill="currentColor">결정의 근거였던 v 냐, 상관없는 q 냐다.</text>
</svg>
```

### Part A — 래퍼: 반환값별 처리

```c
static int futex_wait(atomic_int *addr, int expected, const struct timespec *rel) {
    CNT(wait);
    long r = syscall(SYS_futex, (int *)addr, FUTEX_WAIT_PRIVATE, expected, rel, NULL, 0);
    if (r == 0) return 0;
    int e = errno;                       /* 바로 저장: 다음 libc 호출이 덮어쓸 수 있다 */
    ...
    return -e;
}
```

락 코드는 `futex_wait` 의 반환값을 **보지 않는다**. 어떤 이유로 돌아왔든 할 일은 같다: "다시 잡아 보고, 안 되면 다시 잔다". 이것이 EAGAIN·EINTR·가짜 깨어남을 모두 처리하는 가장 견고한 방법이다 (glibc 도 같다 — 아래 objdump). 반환값이 의미를 갖는 곳은 timedlock 의 `ETIMEDOUT` 뿐이고, 그것도 "남은 시간 ≤ 0" 검사로 대신한다.

시그널이 정확히 어떻게 들어오는지는 `demo_semantics.c` 로 확인했다 (실제 출력):

```text
== 1. FUTEX_WAIT 와 시그널 ==
  SA_RESTART=0 timeout=없음 : 시그널 50 ms 뒤 아직 futex 안? 아니오 | 이후 wake 가 깨운 수 0 | 스레드 반환 -1 (Interrupted system call)
  SA_RESTART=1 timeout=없음 : 시그널 50 ms 뒤 아직 futex 안? 예 | 이후 wake 가 깨운 수 1 | 스레드 반환 0 (깨워짐)
  SA_RESTART=0 timeout=2s  : 시그널 50 ms 뒤 아직 futex 안? 아니오 | 이후 wake 가 깨운 수 0 | 스레드 반환 -1 (Interrupted system call)
  SA_RESTART=1 timeout=2s  : 시그널 50 ms 뒤 아직 futex 안? 아니오 | 이후 wake 가 깨운 수 0 | 스레드 반환 -1 (Interrupted system call)
```

| | timeout 없음 | 상대 timeout 있음 |
|---|---|---|
| 핸들러, `SA_RESTART` 없음 | `EINTR` | `EINTR` |
| 핸들러, `SA_RESTART` 있음 | 커널이 **다시 시작** (사용자는 모름) | 그래도 `EINTR` |

넷째 줄이 흥미롭다: `SA_RESTART` 를 줘도 timeout 이 있으면 `EINTR` 이 나왔다. 커널이 timeout 있는 대기를 `-ERESTART_RESTARTBLOCK` 방식으로 재시작하는데, 핸들러가 실행되면 이것은 `EINTR` 로 바뀐다고 알려져 있다 (원인 설명은 2차 지식, 동작 자체는 실측). 결론: **SA_RESTART 에 기대지 말고 루프로 처리한다.** 라이브러리 코드는 호출자가 어떤 sigaction 을 썼는지 모른다.

### Part A — mutex2 상태 기계

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">mutex2 의 상태와 전이 (누가 어떤 원자 연산으로)</text>
  <circle cx="110" cy="140" r="48" class="f-ok" stroke="currentColor"/>
  <text x="96" y="137" fill="currentColor" font-size="20" font-weight="bold">0</text><text x="88" y="158" fill="currentColor">풀림</text>
  <circle cx="370" cy="140" r="48" class="f-acc" stroke="currentColor"/>
  <text x="363" y="137" fill="currentColor" font-size="20" font-weight="bold">1</text><text x="335" y="158" fill="currentColor">잠김·대기자 없음</text>
  <circle cx="630" cy="140" r="48" class="f-warn" stroke="currentColor"/>
  <text x="623" y="137" fill="currentColor" font-size="20" font-weight="bold">2</text><text x="590" y="158" fill="currentColor">잠김·대기자 가능</text>
  <path d="M155 120 Q240 70 325 120" fill="none" stroke="currentColor"/><path d="M318 112 L325 120 L314 121" fill="none" stroke="currentColor"/>
  <text x="170" y="78" fill="currentColor">lock: cas(0→1)  빠른 길</text>
  <path d="M325 162 Q240 210 155 162" fill="none" stroke="currentColor"/><path d="M162 170 L155 162 L166 160" fill="none" stroke="currentColor"/>
  <text x="165" y="212" fill="currentColor">unlock: dec 이전값 1 → syscall 없음</text>
  <path d="M415 120 Q500 70 585 120" fill="none" stroke="currentColor"/><path d="M578 112 L585 120 L574 121" fill="none" stroke="currentColor"/>
  <text x="420" y="78" fill="currentColor">대기자: cas(1→2), WAIT(&amp;v,2)</text>
  <path d="M600 182 Q370 300 140 186" fill="none" stroke="currentColor"/><path d="M147 194 L140 186 L151 185" fill="none" stroke="currentColor"/>
  <text x="250" y="262" fill="currentColor">unlock: dec 이전값 2 → v=0 → WAKE(&amp;v,1)</text>
  <path d="M155 145 Q390 30 588 128" fill="none" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="400" y="40" fill="currentColor">깨어난 쪽: cas(0→2) — 1 이 아니라 2</text>
  <text x="10" y="292" fill="currentColor">2 = "대기자가 있을지도 모른다". 과대 추정 = 헛 wake 1 번, 과소 추정 = 영원히 잠.</text>
</svg>
```

| 줄 | 코드 | 왜 |
|---|---|---|
| 1 | `c = cas(v, 0, 1); if (c == 0) return;` | 경합 없는 경우가 압도적으로 많다 → 원자 연산 1개로 끝 |
| 2 | `if (c == 2 \|\| cas(v, 1, 2) != 0)` | 이미 2 면 CAS 를 아낀다(`\|\|` 단락 평가). 1 이면 "나 기다린다" 표시. CAS 가 0 을 봤으면 그새 풀렸으니 **자지 않는다** |
| 3 | `futex_wait(&v, 2, …)` | 기대값은 **무조건 2**. v 가 그 사이 0 이 됐으면 커널이 EAGAIN |
| 4 | `while ((c = cas(v, 0, 2)) != 0)` | 다시 잡을 때 2: 나 말고 다른 대기자가 자고 있을 수 있다 (Drepper: "because we do not know any better") |
| 5 | `if (fetch_sub(v, 1) != 1) { v = 0; wake(1); }` | 이전 값 1 = 대기자 없음 → syscall 생략. 2 였으면 하나만 깨운다 (다 깨워도 하나만 잡는다 = thundering herd) |

4번 줄을 1 로 바꾸면 무슨 일이 나는지가 연습 3 이다: 대기자 2 명이 자고 있을 때 하나만 깨어나 v=1 로 잡고, 그가 unlock 할 때 이전 값 1 → wake 생략 → 남은 대기자가 영원히 잔다. practice.c 실측: `mutex2_bad: wake 1 woken 1 timedout 1` vs `mutex2: wake 3 woken 2 timedout 0`.

**메모리 순서.** 락을 얻는 CAS·xchg 는 acquire, 푸는 `fetch_sub`/`store` 는 release (C-A03 의 스핀락과 같다). 상태 단어 하나에 대한 RMW 들은 하나의 수정 순서(modification order)를 가지므로 서로 다른 변수 둘이 얽히는 store-buffering 패턴(seq_cst 가 필요한 경우)은 없다. 커널 쪽 비교·큐잉의 순서는 커널이 책임진다.

**실제로 나온 명령** (VM, `gcc -O2 -S` / `clang -O2 -S` / `-march=armv8.1-a`, 같은 mutex2 를 따로 뽑은 asm_m2.c):

| 컴파일 | lock 의 CAS | unlock 의 dec | unlock 의 store 0 |
|---|---|---|---|
| gcc 15.2 기본 | `bl __aarch64_cas4_acq` | `bl __aarch64_ldadd4_rel` | `stlr wzr, [x1]` |
| clang 21 기본 | `bl __aarch64_cas4_acq` | `bl __aarch64_ldadd4_rel` | `stlr wzr, [x19]` |
| gcc `-march=armv8.1-a` | `casa w1, w0, [x19]` | `ldaddl w2, w2, [x0]` | `stlr wzr, [x0]` |
| clang `-march=armv8.1-a` | `casa w8, w9, [x0]` | `ldaddl w8, w8, [x0]` | `stlr wzr, [x0]` |

Ubuntu 의 aarch64 gcc/clang 기본값은 **outline atomics** — `__aarch64_cas4_acq` 같은 도우미를 부르고, 도우미가 런타임에 CPU 가 LSE(`atomics` 플래그, 이 VM `/proc/cpuinfo` 에 있음)를 지원하면 `casa`, 아니면 `ldaxr/stxr` 루프를 탄다. 아래 glibc objdump 에서 그 도우미의 실제 몸통이 보인다. (Apple clang 의 arm64-apple-macos 기본값은 ARM-M04 참고 — 그쪽은 다르다.)

### Part B — ① lost wakeup: 강제 스케줄로 매번

solution 실행의 해당 부분 (실제 출력):

```text
PASS  forced_schedule(&OPS_NAIVEQ, &so) == 0 && so.acquired
  naiveq: wait 1 wake 2 woken 0 eagain 0 timedout 1
PASS  so.d.timedout == 1 && so.d.woken == 0
PASS  forced_schedule(&OPS_NAIVE, &so) == 0 && so.acquired
  naive : wait 1 wake 2 woken 0 eagain 1 timedout 0
PASS  so.d.eagain == 1 && so.d.timedout == 0
PASS  forced_schedule(&OPS_MUTEX2, &so) == 0 && so.acquired
  mutex2: wait 1 wake 2 woken 0 eagain 1 timedout 0
PASS  so.d.eagain == 1 && so.d.timedout == 0 && so.v_after == 0
```

`naiveq` 는 watchdog 이 없으면 **영원히** 잔다 (테스트는 300 ms 로 상한을 두고 "타임아웃 = 잃어버린 깨움" 으로 센다). 강제 스케줄은 진짜 스레드와 진짜 커널 futex 를 쓰되, 순서만 훅으로 고정한 것이다 — "운 나쁘면 생긴다" 를 "매번 생긴다" 로 바꾼다. 테스트가 "W 가 정말 커널 안에서 자고 있다" 를 확인할 때는 `/proc/self/task/<tid>/syscall` 을 읽는다: 자는 스레드는 `98 0xbe498bbc0018 0x80 0x0 …` (aarch64 `SYS_futex` = 98, 주소, op 0x80 = `FUTEX_WAIT_PRIVATE`), 실행 중이면 `running` 이 나온다 (같은 프로세스 안에서는 읽힌다. 다른 프로세스의 것은 이 VM 에서 `Operation not permitted`).

**강제하지 않으면 실제로 나나?** `./bench lost` (watchdog 5 ms, VM load 1.07 → 1.34, 실제 출력):

```text
  naiveq  T=2  500 ms: ops  1611962  futex_wait    1008  timedout(=잃어버린 wakeup)    0  eagain      0
  naiveq  T=4  500 ms: ops  1719153  futex_wait   60694  timedout(=잃어버린 wakeup)    0  eagain      0
  naiveq  T=8  500 ms: ops  1860144  futex_wait  180737  timedout(=잃어버린 wakeup)    0  eagain      0
  naiveq  T=2  5000 라운드 (2147 ms): futex_wait    353  timedout(=잃어버린 wakeup)  353  eagain     0
  naiveq  T=4  5000 라운드 (6978 ms): futex_wait  10028  timedout(=잃어버린 wakeup) 1459  eagain     0
  naive   T=2  5000 라운드 (2 ms): futex_wait   1086  timedout(=잃어버린 wakeup)    0  eagain  1086
  naive   T=4  5000 라운드 (11 ms): futex_wait  13269  timedout(=잃어버린 wakeup)    0  eagain 12036
```

이 숫자가 이 버그가 무서운 이유를 그대로 보여 준다.

- **계속 두드리는 부하**(위 세 줄)에서는 18만 번 잠들어도 거의 0 이다 (한 번 돌렸을 때 T=4 에서 1 번 나온 적은 있다). 잃어버린 대기자는 **다른 스레드의 다음 unlock 이 대신 깨워 준다** — 느려질 뿐 멈추지 않는다. 부하 테스트로는 못 잡는다.
- **라운드 방식**(매 라운드 각 스레드가 lock/unlock 한 번 → 배리어)에서는 매 라운드 "마지막 unlock" 이 생긴다. 그러면 T=2 에서 **잠든 353 번이 전부** 잃어버린 깨움이었다: 임계 구역이 짧아서 주인이 대기자의 syscall 진입(≈150 ns)보다 먼저 unlock 해 버리기 때문이다. 같은 일을 `naive` 는 1086 번 모두 `EAGAIN` 으로 넘겼다 (2 ms vs 2147 ms).

### Part B — ② 헛 FUTEX_WAKE: 경합이 없어도 매번 syscall

```text
  naive : wait 0 wake 1000
PASS  u.wake == 1000 && u.wait == 0 && u.woken == 0
  mutex2: wait 0 wake 0
PASS  u.wake == 0 && u.wait == 0
```

래퍼 카운터만 믿지 않고 커널 쪽에서도 셌다. `strace -f -c -e trace=futex ./bench one <lock> 1 100000` (실제 출력, 표 부분):

```text
naive  : 100.00    0.850892           8    100001           futex
mutex2 :   0.00    0.000000           0         1           futex
pthread: 100.00    0.000406         406         1           futex
```

(남는 1 번은 스레드 join.) 경합 4 스레드에서 `sudo perf stat -e syscalls:sys_enter_futex ./bench one <lock> 4 250000` — perf 수와 래퍼 수가 스레드 생성/조인 분 +2 만큼 정확히 맞는다:

| 락 (4 스레드 × 250000, tight) | ns/op | 래퍼가 센 wait+wake | perf `sys_enter_futex` |
|---|---|---|---|
| naive | 233.9 | 167608 + 1000000 = 1167608 | 1167610 |
| mutex2 | 27.8 | 47854 + 54400 = 102254 | 102256 |
| mutex3 | 27.5 | 73005 + 82732 = 155737 | 155739 |
| pthread (glibc 기본) | 34.2 | (래퍼 밖) | 144995 |
| pthread_adaptive | 37.4 | (래퍼 밖) | 120055 |

syscall 한 번의 값 (`./bench syscall`, 100만 번 평균, 3 회 동일, VM load 0.02):

```text
  rep 2: getppid 134 ns | FUTEX_WAKE_PRIVATE(빈 큐) 146 ns | FUTEX_WAKE shared(빈 큐) 160 ns | FUTEX_WAIT_PRIVATE 즉시 EAGAIN 157 ns
```

빈 큐를 깨우는 데도 getppid 보다 비싸다 (해시·버킷 락). shared 는 private 보다 ~10% 더 든다 — 키를 만들려고 페이지를 조사하기 때문이다 (사적 익명 메모리라 이 정도; 파일 매핑이면 더 들 수 있다, 미측정).

### Part B — 벤치마크: 우리 락 vs glibc pthread_mutex_t

**경합 없음** (`./bench uncontended`, 스레드 1, 500만 쌍, best of 3, VM load 0.10–2.6):

```text
  naive             152.27 ns/쌍 (best of 3)  syscalls/쌍 1.000
  mutex2              2.68 ns/쌍 (best of 3)  syscalls/쌍 0.000
  mutex3              2.63 ns/쌍 (best of 3)  syscalls/쌍 0.000
  spin+futex          2.59 ns/쌍 (best of 3)  syscalls/쌍 0.000
  pthread             3.05 ns/쌍 (best of 3)  syscalls/쌍 (래퍼 밖 → perf stat 로)
  pthread_adaptive    3.29 ns/쌍 (best of 3)  syscalls/쌍 (래퍼 밖 → perf stat 로)
```

`naive` → `mutex2` = **약 57배**. Drepper 가 논문에서 "4 프로세서 기계의 실제 애플리케이션이 이 문제를 고쳐 8–10배 빨라졌다" 고 쓴 것이 이 차이다. glibc 가 우리보다 0.4 ns 느린 것은 주인 TID·`__nusers` 기록과 뮤텍스 종류 분기 때문이다 (아래 objdump).

**경합** (`./bench contended 100`: 같은 락 하나, 100 ms 고정, 3 번 돌린 **중앙값** Mops/s, 괄호는 최소–최대. 돌리는 동안 VM load 1.23 → 2.71, 맥 4.9):

| tight (임계 구역 안·밖 일 없음) | T=1 | T=2 | T=4 | T=6 | T=8 | T=12 |
|---|---|---|---|---|---|---|
| naive | 6.55 | 6.88 | 6.54 | 6.16 | 6.22 | 6.20 |
| mutex2 | 370.1 | 9.83 (7.3–11.6) | 30.3 | 28.2 | 21.6 | 13.5 |
| mutex3 | 372.5 | 12.9 (12.0–31.6) | 32.1 | 31.2 | 24.2 | 15.6 |
| spin+futex (연습 2) | 364.4 | 8.6 | 29.3 | 29.3 | 23.3 | 14.3 |
| pthread | 270.4 | 16.2 | 39.9 | 38.2 | 26.2 | 11.8 |
| pthread_adaptive | 263.8 | 20.4 | 24.5 | 24.6 | 21.6 | 15.1 |

| work (안 ~50, 밖 ~400 회 산술) | T=1 | T=2 | T=4 | T=6 | T=8 | T=12 |
|---|---|---|---|---|---|---|
| naive | 1.82 | 3.27 | 5.55 | 5.25 | 5.39 | 5.78 |
| mutex2 | 3.17 | 5.91 | 5.36 | 5.28 | 5.31 | 5.85 |
| mutex3 | 3.17 | 5.89 | 5.55 | 5.41 | 5.49 | 5.91 |
| spin+futex | 3.17 | 5.89 | 6.38 | 5.95 | 6.08 | 6.29 |
| pthread | 3.15 | 5.80 | 4.98 | 4.92 | 4.96 | 4.89 |
| pthread_adaptive | 3.13 | 5.95 | **8.86** | 7.60 | 7.59 | 7.48 |

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">tight 경합 처리량 (Mops/s, 3 회 중앙값, VM 6 vCPU) — T=2..12</text>
  <path d="M80 300 L700 300 M80 40 L80 300" stroke="currentColor"/>
  <g stroke="currentColor" stroke-dasharray="2 4" opacity="0.4">
    <path d="M80 242.2 L700 242.2"/><path d="M80 184.4 L700 184.4"/><path d="M80 126.7 L700 126.7"/><path d="M80 68.9 L700 68.9"/>
  </g>
  <g fill="currentColor">
    <text x="58" y="304">0</text><text x="52" y="246">10</text><text x="52" y="188">20</text><text x="52" y="131">30</text><text x="52" y="73">40</text>
    <text x="74" y="318">2</text><text x="194" y="318">4</text><text x="314" y="318">6</text><text x="434" y="318">8</text><text x="670" y="318">12 스레드</text>
  </g>
  <polyline points="80,260.3 200,262.2 320,264.4 440,264.1 680,264.2" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="2 3"/>
  <polyline points="80,243.2 200,124.9 320,137.2 440,175.3 680,221.8" fill="none" stroke="currentColor" stroke-width="2.5"/>
  <polyline points="80,206.5 200,69.4 320,79.0 440,148.8 680,231.6" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="8 4"/>
  <polyline points="80,182.2 200,158.7 320,158.0 440,175.3 680,212.8" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="12 3 2 3"/>
  <g stroke="currentColor">
    <circle cx="200" cy="124.9" r="4" class="f-acc"/><circle cx="320" cy="137.2" r="4" class="f-acc"/><circle cx="440" cy="175.3" r="4" class="f-acc"/>
    <rect x="196" y="65.4" width="8" height="8" class="f-ok"/><rect x="316" y="75" width="8" height="8" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="520" y="170">mutex2 (굵은 실선)</text><text x="225" y="62">pthread (긴 점선)</text>
    <text x="470" y="200">pthread_adaptive (일점쇄선)</text><text x="470" y="282">naive: 6–7 로 평평 (매 unlock syscall)</text>
  </g>
  <text x="10" y="340" fill="currentColor">T=1 은 축 밖: mutex2 370 · pthread 270 · naive 6.55 Mops/s.</text>
  <text x="10" y="356" fill="currentColor">T=2 최악 = 자고 깨우는 핑퐁 (wake/op 0.33). T=12 는 6 vCPU 초과.</text>
</svg>
```

읽는 법 (비율만 믿을 것):

- **mutex2 가 이기는 곳은 "경합 없음 ~ 가벼운 경합"** 이다: T=1 tight 56배, work T=1 1.74배, T=2 1.8배. 그 아래 숫자를 보면 이유가 명확하다 — 경합이 없을 때 naive 의 비용은 전부 헛 wake 다.
- **경합이 포화되면(work T≥4) 다 비슷해진다** (5–6 Mops/s). 이때 mutex2 도 unlock 의 37–57 % 가 진짜 wake 를 한다(wake/op 0.37–0.57). naive 의 헛 wake 는 락을 **놓은 뒤** 하는 일이라 임계 구역을 늘리지 않는다. 병목은 "깨어난 스레드가 CPU 에 올라오는 시간" 이다.
- **잠깐 도는 게 이 구간을 이긴다**: glibc `PTHREAD_MUTEX_ADAPTIVE_NP` 가 work T=4 에서 8.86 (일반 pthread 4.98 의 1.8배). 연습 2 의 100회 스핀도 6.4 로 조금 낫다. 반대로 tight 에서는 adaptive 가 더 나쁘다 (T=4 24.5 vs 39.9) — 스핀이 같은 캐시 라인을 두드려 주인의 unlock 을 늦춘다 (OS-M07 의 TTAS 이야기와 같다).
- **공정성**: 모든 futex 락은 FIFO 가 아니다. 깨어난 스레드가 잡기 전에 막 도착한 스레드가 새치기(barging)할 수 있다. 이 측정에서 스레드별 획득 수 min/mean 은 0.77–1.0 범위였다(심한 굶주림은 없었음). 처리량을 위해 공정성을 포기하는 설계다 — 엄격한 순서가 필요하면 티켓 락(OS-M07)이나 PI 뮤텍스.

### glibc 는 실제로 무엇을 하나 (objdump · strace · 단어 값)

글리브씨 소스를 외우는 대신 이 VM 의 `libc.so.6` (glibc 2.43) 을 직접 열었다. `nm -D` 로 보이는 심볼은 `__lll_lock_wait_private@@GLIBC_PRIVATE`, `pthread_mutex_lock@@GLIBC_2.17`, `pthread_mutex_unlock@@GLIBC_2.17`. `objdump -d --no-show-raw-insn libc.so.6 --start-address=0x88b40 …` (실제 출력, 발췌):

```text
0000000000088b40 <__lll_lock_wait_private@@GLIBC_PRIVATE>:
   88b54:	ldr	w0, [x0]
   88b58:	cmp	w0, #0x2
   88b5c:	b.eq	88b70            // 이미 2 → 바로 잔다
   88b60:	mov	x1, x19
   88b64:	mov	w0, #0x2
   88b68:	bl	14e8b0           // xchg(futex, 2)  (아래 도우미)
   88b6c:	cbz	w0, 88bb0        // 이전 값 0 → 획득, 반환
   88b74:	mov	x1, #0x80        // FUTEX_WAIT_PRIVATE
   88b7c:	mov	x2, #0x2         // 기대값 2
   88b84:	mov	x8, #0x62        // SYS_futex = 98
   88b88:	svc	#0x0
   88b90:	b.ls	88b60            // 성공 → 다시 xchg
   88b94:	cmn	w0, #0xb          // -EAGAIN → 다시
   88b9c:	cmn	w0, #0x4          // -EINTR  → 다시
   88bac:	bl	80ac0 <__libc_fatal@@GLIBC_PRIVATE>   // 그 밖의 오류는 치명적
000000000014e8b0:                 // outline atomics 도우미 (심볼 없음)
  14e8b4:	ldrb	w16, [x16, #3642]   // LSE 지원 플래그
  14e8bc:	swpa	w0, w0, [x1]        // 있으면 SWPA 한 방 (없으면 14e8c8 의 LDAXR/STXR 루프)
```

이것은 정확히 **Drepper §6 mutex3 의 느린 길** (`if (c != 2) c = xchg(v, 2); while (c != 0) { futex_wait(v, 2); c = xchg(v, 2); }`) 이고, EAGAIN·EINTR 을 "다시 시도" 로 처리하는 것까지 우리 래퍼 정책과 같다. `pthread_mutex_lock` 의 빠른 길(0x8f6e0~)은 종류 비트를 검사한 뒤 `bl 14e730` → 그 도우미 몸통은 `casa w0, w1, [x2]` (없으면 `ldaxr/stxr`) 로 0→1 CAS, 성공하면 `__owner` 에 TID 를 쓰고 `__nusers` 를 올린다. 같은 함수에 단일 스레드 프로세스면 원자 연산 없이 `str` 로 1 을 쓰는 분기도 보인다(0x8f778–0x8f790, 바이트 플래그 검사 — `__libc_single_threaded` 류로 추정). unlock 경로(0x91300~)는 `__owner` 를 지우고 `bl 14e9d0` → `swpl w0, w0, [x1]` 로 **xchg(0) 의 이전 값이 1 보다 크면** `__lll_lock_wake` 를 부른다 — 우리 연습 1 의 `mutex3_unlock` 과 같은 모양이다. (함수 이름 없이 주소로 본 것이라 "어느 C 함수가 어디" 는 추정. 명령 자체는 실측.)

strace 로 본 경합 중 glibc (`strace -f -e trace=futex ./bench one pthread 4 200000`, 실제 출력 일부):

```text
[pid  5483] futex(0xbcbfa3ae0058, FUTEX_WAIT_PRIVATE, 2, NULL <unfinished ...>
[pid  5482] futex(0xbcbfa3ae0058, FUTEX_WAKE_PRIVATE, 1 <unfinished ...>
```

그리고 `demo_semantics.c` 가 `pthread_mutex_t.__data.__lock` 을 직접 읽은 값 (실제 출력):

```text
== 4. glibc pthread_mutex_t 의 futex 단어 ==
  초기 __lock=0 | lock 후 1 (owner=5777, 내 tid=5777) | 대기자가 잠든 뒤 2 | 모두 끝난 뒤 0
```

세 가지 증거가 같은 말을 한다: **glibc 기본 뮤텍스 = Drepper 의 0/1/2 + xchg 느린 길.** (glibc 내부 매크로 이름 `lll_lock` / `__lll_lock_wait` / `lll_unlock` 은 소스 기준 지식 — 이 VM 에서 소스는 안 열었다.)

### Part C — timedlock 과 EINTR

```c
long deadline = now_ns() + timeout_ns;
do {
    if (c == 2 || cas(&m->v, 1, 2) != 0) {
        long left = deadline - now_ns();
        if (left <= 0) return ETIMEDOUT;      /* v 는 2 로 남는다 */
        struct timespec rel = { left / 1000000000L, left % 1000000000L };
        futex_wait(&m->v, 2, &rel);           /* 0/EAGAIN/EINTR/ETIMEDOUT 모두 → 다시 시도 */
    }
} while ((c = cas(&m->v, 0, 2)) != 0);
```

세 가지 결정:

1. **마감 시각 하나, 상대 시간은 매번 재계산.** 시그널이 1 ms 마다 오는 데몬(타이머 시그널, 프로파일러)에서 처음 30 ms 를 매번 다시 넘기면 영원히 안 끝날 수 있다. 커널의 `FUTEX_WAIT_BITSET` 은 **절대 시각**을 받으므로 재계산 자체가 필요 없다 — glibc 의 `pthread_mutex_timedlock` 이 그쪽을 쓴다 (위 strace 의 스레드 join 에서 `FUTEX_WAIT_BITSET|FUTEX_CLOCK_REALTIME` 이 보인다. timedlock 경로 자체는 이 레슨에서 strace 안 함).
2. **포기해도 2 를 되돌리지 않는다.** 내가 2 로 만들었는지, 다른 대기자가 이미 2 로 만들었는지 알 수 없다. 1 로 되돌리면 그 대기자의 깨움을 잃는다(연습 3 과 같은 버그). 비용은 "다음 unlock 의 헛 wake 1 번" — 테스트가 그것을 정확히 확인한다 (`d.wake == 1 && d.woken == 0`).
3. **예산 0 = trylock + 표시.** 자지는 않지만 `cas(1→2)` 는 했으므로 역시 헛 wake 1 번이 생긴다. 진짜 trylock 은 연습 2 처럼 `cas(0→1)` 하나만 한다.

EINTR 테스트는 "대기자가 커널 안에서 자는 것 확인 → `pthread_kill(SIGUSR1)` → eintr 카운터 증가 확인 → **다시** 커널에서 자는 것 확인 → unlock → 0 반환" 순서로 결정적이다. 펌웨어식으로 말하면 `ctrl_tick` 은 "버스가 바쁘면 이전 샘플로 PID 루프를 돌린다" 이고, stale 카운터가 그 정책의 계측값이다.

### macOS 대응물 — 비교만 (`demo_mac_ulock.c`, 맥에서 실행)

macOS 14.4+ 의 공개 API `os_sync_wait_on_address(addr, value, size, flags)` / `os_sync_wake_by_address_any` (헤더 `MacOSX26.4.sdk/usr/include/os/os_sync_wait_on_address.h` 존재 확인, `__API_AVAILABLE(macos(14.4), …)`). 리눅스와의 차이 하나: 헤더 주석상 값이 이미 다르면 **성공(≥0)으로 즉시 반환**한다 (리눅스는 `EAGAIN`). EINTR 은 "interrupted / spurious wake up" 으로 문서화돼 있다. lldb 로 확인한 것 (실제 출력 발췌):

```text
(lldb) dis -n os_unfair_lock_lock
libsystem_platform.dylib`os_unfair_lock_lock:
    0x18c533c44 <+8>:  ldr    w3, [x9, #0x18]
    0x18c533c48 <+12>: casa   w8, w3, [x0]
(lldb) b __ulock_wait2
(lldb) expr -i false -- (int)os_sync_wait_on_address((void*)&g_m2, 5, 4, 0)
error: Expression execution hit a breakpoint: breakpoint 2.1.
->  0x1875d89f4 <+0>:  mov    x16, #0x220               ; =544
```

- `os_unfair_lock` 의 빠른 길 = `casa` 0 → **스레드 고유값**(TLS 에서 읽은 값; 이름 공간상 mach 스레드 포트로 추정). 리눅스 PI futex 처럼 주인을 단어에 적는다 → 커널이 주인을 알고 우선순위를 올려 줄 수 있다(문서 기준).
- `os_sync_wait_on_address` 는 `__ulock_wait2` (BSD syscall 544) 로 간다. 리눅스 futex 의 맥 쪽 짝이다.

결과 (맥, load 3.6–4.4, 실제 출력 발췌):

```text
경합 없음 m2 (os_sync_wait)  2.18 ns/쌍 (best of 3)
경합 없음 os_unfair_lock     2.21 ns/쌍 (best of 3)
경합 없음 pthread_mutex      3.48 ns/쌍 (best of 3)
경합 m2 (os_sync_wait)  T=4   48.67 Mops/s  wait/op 0.044 wake/op 0.057  min/mean 0.88 max/mean 1.11
경합 os_unfair_lock     T=4   42.88 Mops/s  min/mean 0.95 max/mean 1.07
경합 pthread_mutex      T=2  133.78 Mops/s  min/mean 0.96 max/mean 1.04
경합 pthread_mutex      T=4   61.21 Mops/s  min/mean 0.99 max/mean 1.01
```

같은 0/1/2 코드가 맥에서도 그대로 동작하고, 경합 없음은 `os_unfair_lock` 과 같다. 맥 `pthread_mutex` 가 T=2 에서 유난히 빠른 이유는 확인하지 않았다 (내부 스핀 정책 추정, 미검증). 맥 숫자와 VM 숫자는 서로 비교하지 말 것 (하나는 물리 코어 + QoS 스케줄러, 하나는 vCPU).

### 펌웨어 연결 — RTOS 뮤텍스는 왜 "항상 커널" 인가

| 시스템 | 경합 없는 take | 경합 시 |
|---|---|---|
| Linux futex 뮤텍스 (이 레슨) | 사용자 공간 CAS 1 개 | `FUTEX_WAIT` syscall, 스케줄러 |
| FreeRTOS V11.3.2 `xSemaphoreTake` (`~/.esn_scratch/FreeRTOS-Kernel/queue.c:1659` `xQueueSemaphoreTake`) | **매번** `taskENTER_CRITICAL()` (1688행) → 카운트 검사 → `taskEXIT_CRITICAL` | 태스크를 이벤트 리스트에 넣고 문맥 교환 |
| FreeRTOS-MPU 포트 | API 호출이 SVC 로 특권 진입 (문서 기준) | 〃 |
| Zephyr `k_mutex_lock` | 커널 함수, 스핀락(IRQ lock) — 사용자 모드 스레드면 syscall | 우선순위 상속 포함 (문서 기준) |
| Zephyr `sys_sem` + `k_futex` (`CONFIG_USERSPACE`) | 사용자 공간 원자 연산 | `k_futex_wait` (문서 기준, 미검증) |

MMU·특권 분리가 없는 Cortex-M 에서 FreeRTOS "커널 진입" 은 함수 호출 + BASEPRI/PRIMASK 마스킹 수십 사이클이라 리눅스 syscall(≈150 ns + 캐시 오염)만큼 비싸지 않다. 그래서 RTOS 는 "항상 커널" 로도 충분하고, 대신 소유권·우선순위 상속을 커널이 쉽게 관리한다 (RTOS-B10, RTOS-M04). ISR 과 공유하는 데이터는 뮤텍스가 아니라 IRQ 마스킹이다 (C-M14).

**PI futex.** 리눅스에서 우선순위 역전(RTOS-M03)을 막으려면 `PTHREAD_PRIO_INHERIT` → glibc 가 `FUTEX_LOCK_PI` 를 쓴다. 단어의 의미가 커널 정의로 바뀐다: **0 = 풀림, 값 = 주인 TID, 대기자가 있으면 `| FUTEX_WAITERS (0x80000000)`**. `demo_semantics.c` 실측:

```text
== 5. PTHREAD_PRIO_INHERIT 뮤텍스 ==
  내 tid=5777 | lock 후 단어 0x00001691 | 대기자가 잠든 뒤 0x80001691 (FUTEX_WAITERS=0x80000000) | 끝 0x00000000
```

0x1691 = 5777 = 내 TID. strace 로는 대기자의 `futex(…, FUTEX_LOCK_PI_PRIVATE, NULL …)` 과 `FUTEX_UNLOCK_PI_PRIVATE` 가 보였고, **main 의 경합 없는 lock 은 syscall 이 없었다** — PI 뮤텍스도 빠른 길은 사용자 공간 CAS(0 → TID)다. (strace 첫 줄의 `FUTEX_UNLOCK_PI_PRIVATE = -1 EPERM` 은 `pthread_mutex_init` 이 PI 지원 여부를 떠보는 호출로 추정.) 커널은 대기 중에 주인 태스크의 우선순위를 `rt_mutex` 로 올린다. PREEMPT_RT 커널은 커널 내부 spinlock·mutex 대부분도 `rt_mutex` 로 바꾼다 (이 VM 은 `PREEMPT_DYNAMIC`, RT 아님 — 개념만).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

검증 (VM `/tmp/OS-A01_work/`, 실제 결과): gcc·clang strict 빌드 34/34 (gcc 로 5 회 반복 모두 34/34, 0.42 s — 그중 300 ms 는 naiveq 의 의도된 watchdog) · gcc/clang `-fsanitize=address,undefined` 34/34 오류 0 · clang `-fsanitize=thread` 34/34 WARNING 0 · gcc `-fsanitize=thread` 도 34/34 WARNING 0 (조건 변수를 안 쓰는 이 코드에서는 gcc TSan 도 정상).

TSan 은 raw `syscall(SYS_futex)` 를 동기화로 이해하지 못하지만 상관없다: happens-before 는 락 단어의 acquire CAS / release store 가 만들고, TSan 은 그 원자 연산을 본다. 락으로만 보호되는 평범한 `g_shared++` 가 깨끗하다는 것이 그 증거다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 상태 검사와 잠드는 단어를 분리 (`naiveq`) | 검사와 잠들기 사이의 unlock 을 커널이 알 방법이 없다 → lost wakeup | `futex_wait(&m->v, <내가 본 값>)` — 결정의 근거가 된 단어·값을 그대로 |
| 깨어난 뒤 `cas(0→1)` 로 재획득 | 남은 대기자가 있는데 "대기자 없음" 상태 → 다음 unlock 이 wake 생략 (연습 3: timedout 1) | `cas(0→2)` |
| unlock 에서 무조건 `FUTEX_WAKE` | 정확하지만 경합 없는 unlock 마다 ≈150 ns syscall (실측 57배 느림) | 이전 값이 1 이면 생략 |
| `FUTEX_WAKE(&v, INT_MAX)` 로 뮤텍스 unlock | 한 명만 잡을 수 있는데 모두 깨움 → thundering herd | 뮤텍스는 1, 래치/배리어/브로드캐스트만 INT_MAX (연습 4) |
| `futex_wait` 가 `-EINTR`/`-EAGAIN` 이면 lock 실패로 반환 | 락을 안 가진 채 임계 구역 진입 | 반환값과 무관하게 루프에서 다시 CAS |
| timedlock 에서 EINTR 후 처음 timeout 으로 다시 대기 | 시그널이 잦으면 마감이 무한히 늘어남 | 마감 시각을 고정하고 남은 시간 재계산 (또는 `FUTEX_WAIT_BITSET` 절대 시각) |
| 프로세스 공유 메모리(`MAP_SHARED`)의 futex 에 `_PRIVATE` 사용 | 키가 (mm, 주소) 라 다른 프로세스와 안 맞음 → 실측 `PRIVATE/PRIVATE: 깨운 수 0, 자식 ETIMEDOUT` | 공유 메모리면 `FUTEX_WAIT`/`FUTEX_WAKE` (flag 없음), pthread 는 `PTHREAD_PROCESS_SHARED` |
| `syscall()` 반환 후 `printf` 하고 나서 `errno` 읽기 | `errno` 덮어써짐 | 바로 `int e = errno;` |

같은 프로세스 안에서도 PRIVATE 로 잔 스레드를 shared `FUTEX_WAKE` 로 깨우면 0 명이었다 (`demo_semantics.c` 2번, 이어서 PRIVATE wake 는 1 명) — 키가 다르다.

## 8. 면접 Q&A

<details>
<summary>Q1. futex 뮤텍스가 빠른 이유를 한 문장으로? 꼬리: 그럼 경합이 심하면 스핀락보다 느린가?</summary>

경합이 없으면 lock 도 unlock 도 사용자 공간 원자 연산 하나로 끝나고(syscall 0 — 이 VM 실측 2.7 ns/쌍, 매번 wake 하는 버전은 152 ns), 커널은 정말로 기다려야 할 때만 개입한다. 꼬리: 임계 구역이 아주 짧고 코어가 남으면 잠들고 깨우는 비용(µs 단위 문맥 교환)이 아깝다 — 그래서 glibc adaptive 뮤텍스처럼 "잠깐 돌고 나서 잔다". 실측 work T=4 에서 adaptive 8.86 vs 일반 4.98 Mops/s. 반대로 코어보다 스레드가 많으면 순수 스핀락은 선점된 주인을 기다리며 CPU 를 태운다 (OS-M07).

</details>

<details>
<summary>Q2. FUTEX_WAIT 가 값을 비교하는 것이 왜 핵심인가? 꼬리: 비교 없이 "그냥 재워 줘" 였다면?</summary>

대기자가 "잠겨 있네" 를 본 순간과 실제로 잠드는 순간 사이에 unlock+wake 가 끼면, wake 는 빈 큐를 깨우고 대기자는 그 뒤에 잠들어 영원히 깨지 않는다(lost wakeup). 커널은 해시 버킷 락 아래에서 값을 다시 읽고 큐에 넣으며, wake 도 같은 락을 잡으므로 "이미 큐에 있다 → 깨워짐" 또는 "값이 바뀌었다 → EAGAIN" 둘 중 하나만 가능하다. 비교 없는 park 였다면 Solaris `park()/unpark()` 의 `setpark()` 같은 별도 장치가 필요하다 (OSTEP 28장). 이 레슨의 `naiveq` 가 정확히 그 상태이고, 강제 스케줄에서 매번 timedout 1.

</details>

<details>
<summary>Q3. mutex2 에서 깨어난 스레드가 왜 1 이 아니라 2 로 잡나? 꼬리: 그 대가는?</summary>

대기자가 둘 이상일 때 하나만 깨운다. 깨어난 스레드는 다른 대기자가 남아 있는지 모른다. 1 로 잡으면 unlock 이 "대기자 없음" 으로 보고 wake 를 생략 → 남은 대기자는 영원히 잔다 (연습 3 실측: `timedout 1`). 2 로 잡으면 안전하다. 대가: 실제로 남은 대기자가 없을 때 헛 `FUTEX_WAKE` 1번 (강제 스케줄의 mutex2: `wake 2 woken 0` — 두 번째 wake 가 그것). Drepper: "a possible unnecessary call to futex wake in unlock".

</details>

<details>
<summary>Q4. FUTEX_PRIVATE_FLAG 는 무엇이고 언제 쓰면 안 되나?</summary>

키를 (mm, 가상 주소) 로 만들라는 약속이다. 공유 futex 는 같은 물리 페이지를 공유하는 다른 프로세스와도 만나야 하므로 커널이 페이지(익명이면 mm+주소+표시, 파일 매핑이면 inode+offset)를 조사해야 해서 더 비싸다 (실측 빈 큐 wake 146 vs 160 ns). 프로세스 간 공유 메모리의 futex 에 쓰면 안 된다 — 실측 `PRIVATE/PRIVATE` 에서 부모의 wake 가 자식을 0 명 깨우고 자식은 ETIMEDOUT. 대기·깨움 양쪽의 플래그가 같아야 하는 것도 실측(섞으면 0 명). glibc 는 `PTHREAD_PROCESS_PRIVATE`(기본)일 때 `_PRIVATE` 를 쓴다 (strace: `FUTEX_WAIT_PRIVATE, 2`).

</details>

<details>
<summary>Q5. EINTR 와 EAGAIN 을 어떻게 처리하나? 꼬리: SA_RESTART 를 쓰면 되지 않나?</summary>

둘 다 "다시 CAS 해 보고 안 되면 다시 잔다" 로 처리한다 — lock 루프는 futex_wait 반환값을 보지 않는 것이 가장 견고하다. glibc `__lll_lock_wait_private` 도 objdump 상 `-EAGAIN(-11)` 과 `-EINTR(-4)` 를 "다시" 로, 그 밖의 오류는 `__libc_fatal` 로 보낸다. SA_RESTART 는 신뢰할 수 없다: 실측에서 timeout 없는 대기는 커널이 재시작했지만 **timeout 있는 대기는 SA_RESTART 여도 EINTR** 이었다. 라이브러리는 호출자의 sigaction 을 모른다. timedlock 은 마감을 고정하고 남은 시간을 재계산.

</details>

<details>
<summary>Q6. glibc pthread_mutex_lock 은 이것과 같은가? 어떻게 확인했나?</summary>

기본(NORMAL) 뮤텍스는 같은 0/1/2 규칙이다. 세 가지로 확인: ① `pthread_mutex_t.__data.__lock` 값이 0 → lock 후 1 → 대기자가 잠든 뒤 2 → 0. ② strace 에서 `futex(addr, FUTEX_WAIT_PRIVATE, 2, NULL)`. ③ objdump: 빠른 길은 `casa`(outline 도우미) 0→1, 느린 길 `__lll_lock_wait_private` 은 `swpa` 로 xchg(2) 후 `svc` futex(0x80, 2) — Drepper mutex3 — unlock 은 `swpl` 로 xchg(0), 이전 값 > 1 이면 wake. 덧붙여 TID 를 `__owner` 에 기록한다 (ERRORCHECK/RECURSIVE 종류·디버깅용). ADAPTIVE 는 잠들기 전 스핀, PI 는 `FUTEX_LOCK_PI` 로 완전히 다른 경로.

</details>

<details>
<summary>Q7. FreeRTOS 의 뮤텍스는 왜 이런 빠른 길이 없나? 펌웨어에서 futex 같은 설계가 의미 있는 곳은?</summary>

FreeRTOS V11.3.2 `xQueueSemaphoreTake` 는 경합 여부와 상관없이 `taskENTER_CRITICAL()` 로 시작한다(queue.c 1688행 확인). MPU 없는 Cortex-M 에서 "커널 진입" 은 함수 호출 + BASEPRI 마스킹이라 싸고, 커널이 모든 take 를 보니 소유자 기록·우선순위 상속이 단순해진다. 특권 분리가 있어 커널 진입이 진짜 트랩(SVC)이 되는 곳 — FreeRTOS-MPU, Zephyr `CONFIG_USERSPACE`, 리눅스/QNX 같은 MMU 시스템 — 에서는 futex 식 빠른 길이 이득이다. Zephyr 는 그래서 `k_futex` 와 그 위의 `sys_sem` 을 둔다 (문서 기준).

</details>

<details>
<summary>Q8. 우선순위 역전은 futex 뮤텍스에서 어떻게 막나? 꼬리: 단어 의미는 어떻게 바뀌나?</summary>

0/1/2 futex 는 커널이 "누가 주인인지" 모르므로 상속을 할 수 없다. `PTHREAD_PRIO_INHERIT` → `FUTEX_LOCK_PI`: 단어 = 주인 TID (| `FUTEX_WAITERS` 0x80000000), 실측 `0x00001691 → 0x80001691`. 경합 없으면 여전히 사용자 공간 CAS(0→TID) — strace 에서 주인의 lock 은 syscall 이 없었다. 경합 시 커널은 `rt_mutex` 로 대기자 우선순위를 주인에게 빌려준다. PREEMPT_RT 는 커널 내부 락도 rt_mutex 로 바꾼다. RTOS 쪽 같은 개념: RTOS-M04. 맥의 `os_unfair_lock` 도 단어에 스레드 고유값을 `casa` 로 적는다 (lldb 확인) — 같은 아이디어.

</details>

## 9. 추가 연습

### 연습 1 — mutex3 (Drepper §6) + glibc 식 unlock

느린 길을 CAS 두 번 대신 `xchg(v, 2)` 한 번으로. unlock 은 `xchg(v, 0)` 의 이전 값이 1 보다 크면 wake. 경합 없는 1000 쌍 syscall 0, 강제 스케줄 EAGAIN 1·타임아웃 0, 8 스레드 스트레스, 대기자 2 명 테스트를 통과시켜라.

<details>
<summary>답</summary>

```c
static void mutex3_lock(lock_t *m) {
    int c = cas(&m->v, 0, 1);
    if (c == 0) return;
    if (c != 2) c = atomic_exchange_explicit(&m->v, 2, memory_order_acquire);
    while (c != 0) {
        futex_wait(&m->v, 2, g_watchdog);
        c = atomic_exchange_explicit(&m->v, 2, memory_order_acquire);
    }
}
static void mutex3_unlock(lock_t *m) {
    if (atomic_exchange_explicit(&m->v, 0, memory_order_release) > 1) futex_wake(&m->v, 1);
}
```

xchg 가 이전 값 0 을 돌려줬다면 "그새 풀렸고 방금 내가 2 로 잡았다" 는 뜻이다. 2 로 잡았으니 Q3 의 규칙도 자동으로 지켜진다. 측정상 mutex2 와 거의 같다 (tight T=4 32.1 vs 30.3 Mops/s) — LSE `swp` 가 있으면 xchg 가 CAS 보다 싸다는 Drepper 의 IA-32 논리가 arm64 에도 맞지만, 이 부하에서 차이는 작다.

</details>

### 연습 2 — trylock + spin-then-futex

`mutex2_trylock` 은 절대 자지 않고 `cas(0→1)` 만 (실패 시 2 로 표시하지 않는다). `spinmutex_lock` 은 최대 100 번 "읽기 → 0 이면 CAS" (TTAS) 하며 `yield` 힌트를 넣고, 2 를 보면 (이미 자는 대기자가 있으니) 새치기하지 말고 mutex3 느린 길로.

<details>
<summary>답</summary>

```c
static int mutex2_trylock(lock_t *m) { return cas(&m->v, 0, 1) == 0 ? 0 : EBUSY; }
static void spinmutex_lock(lock_t *m) {
    for (int i = 0; i < SPIN_TRIES; i++) {
        int v = atomic_load_explicit(&m->v, memory_order_relaxed);
        if (v == 0 && cas(&m->v, 0, 1) == 0) return;
        if (v == 2) break;
        cpu_relax();                     /* aarch64: yield */
    }
    mutex3_lock(m);
}
```

work 프로파일 T=4 에서 6.38 vs mutex2 5.36 Mops/s. glibc adaptive(8.86)보다는 못하다 — 스핀 횟수와 "2 를 보면 포기" 정책이 다르다 (glibc 의 스핀 횟수는 tunable `glibc.pthread.mutex_spin_count`, 문서 기준).

</details>

### 연습 3 — "다시 잡을 때 1 로" 버그를 대기자 2 명으로 재현

main 이 잡은 채 대기자 2 명을 띄우고 **둘 다 커널에서 자는 것을 확인**(`/proc/.../syscall`)한 뒤 unlock 한 번. 버그 버전은 타임아웃 1, 정답은 0 이어야 한다.

<details>
<summary>답</summary>

`mutex2_bad_lock` 은 루프 조건만 `cas(&m->v, 0, 1)` 로 바꾼 것. `two_waiters()` 가 대기자 둘을 재우고 확인한 뒤 unlock → join → syscall 차이. 실측: `mutex2_bad: wake 1 woken 1 timedout 1`, `mutex2: wake 3 woken 2 timedout 0`. 순서: main 의 wake 가 하나를 깨움 → 그가 v=1 로 잡고 풀 때 이전 값 1 → wake 생략 → 두 번째 대기자는 watchdog(300 ms)까지 잔다.

</details>

### 연습 4 — futex 래치 (count-down latch)

`latch_count_down`: `fetch_sub` 의 이전 값이 1(내가 마지막)일 때만 `futex_wake(&count, INT_MAX)`. `latch_wait`: 본 값 c 가 0 이 아니면 `futex_wait(&count, c)`. 대기자 4 명이 잠든 것을 확인하고 count_down 3 번 → 아무도 못 지나감, 4 번째 → wake syscall **1 번에 4 명**.

<details>
<summary>답</summary>

```c
static void latch_count_down(latch_t *l) {
    if (atomic_fetch_sub_explicit(&l->count, 1, memory_order_acq_rel) == 1)
        futex_wake(&l->count, INT_MAX);
}
static void latch_wait(latch_t *l) {
    int c;
    while ((c = atomic_load_explicit(&l->count, memory_order_acquire)) != 0)
        futex_wait(&l->count, c, g_watchdog);
}
```

중간 count_down 이 값을 바꿔도 이미 자는 대기자는 깨지 않는다 — futex 는 "값이 바뀌면 깨움" 이 아니라 "wake 를 받으면 깨움" 이다. 반대로 잠들기 직전에 값이 바뀌었으면 `EAGAIN` 으로 다시 읽는다. 래치는 모두가 통과해야 하므로 INT_MAX 가 맞다(뮤텍스와 반대). 대기자 수를 세어 wake 를 아끼는 최적화는 MCPP-A03 이 측정한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
# VM 안, /tmp/OS-A01_work/
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr    # 23/23 passed (5 회 반복 동일)
clang -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_t && ./pr_t   # 23/23, WARNING 0
```

ASan+UBSan(gcc, clang) 도 23/23, 오류 0. 데모 빌드 (실험 출력은 위 5. 풀이에 붙였다):

```bash
gcc -std=c11 -Wall -Wextra -O2 -pthread demo_bench.c -o bench && ./bench contended 100   # syscall|uncontended|lost|one 도
sudo perf stat -e syscalls:sys_enter_futex ./bench one mutex2 4 250000; strace -f -c -e trace=futex ./bench one naive 1 100000
gcc -std=c11 -Wall -Wextra -O2 -pthread demo_semantics.c -o sem && ./sem
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_mac_ulock.c -o /tmp/OS-A01_work/mac_ulock   # 맥
```

<!-- code: demo_semantics.c -->

<!-- code: demo_bench.c -->

<!-- code: demo_mac_ulock.c -->

## 10. 핵심 정리

- futex = 사용자 공간 int + 커널 대기 큐. `FUTEX_WAIT(addr, val)` 은 **커널이 값을 비교하고 잠든다** → 검사와 잠들기 사이의 unlock 을 놓치지 않는다 (lost wakeup 방지의 전부).
- Drepper mutex2: 0 풀림 / 1 잠김 / 2 잠김+대기자 가능. 경합 없으면 lock·unlock 각각 원자 연산 1 개, syscall 0 (실측 2.7 ns vs 매번 wake 152 ns).
- 대기자는 `WAIT(&v, 2)`, 깨어나서는 `cas(0→2)` — **1 로 잡으면 다른 대기자를 영원히 재운다.** 대가는 가끔 헛 wake 1 번.
- lock 루프는 futex_wait 반환값(EAGAIN·EINTR·가짜 깨어남)을 보지 않고 다시 CAS. SA_RESTART 에 기대지 말 것 (timeout 있으면 SA_RESTART 여도 EINTR 실측).
- `_PRIVATE` = (mm, 주소) 키 → 싸지만 프로세스 간 공유 불가. 대기·깨움 플래그가 다르면 서로 못 찾는다.
- glibc 기본 pthread_mutex = 0/1/2 + xchg 느린 길(mutex3) — `__lock` 값·strace·objdump 로 확인. adaptive 는 스핀, PI 는 `FUTEX_LOCK_PI` (단어 = TID | 0x80000000).
- lost wakeup 은 부하 테스트에서 거의 안 보인다 (다음 unlock 이 구해 줌). "마지막 unlock" 이나 강제 스케줄에서만 드러난다 → 결정적 훅 테스트가 필요한 이유.
- RTOS 뮤텍스(FreeRTOS)는 매번 크리티컬 섹션 — 커널 진입이 싼 MCU 의 합리적 선택. 특권 분리가 있으면 futex 식 빠른 길이 이긴다.

## 참고자료

- Ulrich Drepper, *Futexes Are Tricky*, Version 1.6, 2011-11-05 — https://www.akkadia.org/drepper/futex.pdf (§2 커널 인터페이스, §4 Take 1, §5 mutex2, §6 mutex3)
- `man 2 futex`, `man 7 signal` (SA_RESTART 와 재시작되는 syscall), `man 7 futex` — https://man7.org/linux/man-pages/man2/futex.2.html
- Hubertus Franke, Rusty Russell, Matthew Kirkwood, "Fuss, Futexes and Furwocks: Fast Userlevel Locking in Linux", OLS 2002
- Linux 커널 소스 `kernel/futex/waitwake.c` ("Futex Ordering Guarantees" 주석), `kernel/futex/pi.c`, `Documentation/locking/pi-futex.rst`, `Documentation/locking/rt-mutex.rst`
- glibc 소스 `nptl/pthread_mutex_lock.c`, `nptl/lowlevellock.c`, `sysdeps/nptl/lowlevellock.h` (이 레슨은 2.43 바이너리를 objdump 로 확인)
- OSTEP 28장 "Locks" — 28.14 park()/unpark() 와 wakeup/waiting race (https://pages.cs.wisc.edu/~remzi/OSTEP/threads-locks.pdf)
- Apple `os/os_sync_wait_on_address.h`, `os/lock.h` (MacOSX26.4.sdk)
- FreeRTOS-Kernel V11.3.2 `queue.c` `xQueueSemaphoreTake`; Zephyr 문서 "Futex" / "Mutexes" (k_futex, sys_sem, k_mutex)
- 이 저장소: C-A03 (C11 atomics), ARM-M04 (LL/SC vs LSE), LLAT-B06 (원자 연산 비용), OS-M07 (스핀락), OS-B05·OS-B06 (뮤텍스·조건 변수), MCPP-A03 (atomic wait/notify), C-M14 (ISR 공유 데이터), RTOS-B10·RTOS-M03·RTOS-M04 (RTOS 뮤텍스·우선순위 역전/상속)
