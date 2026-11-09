---
id: MCPP-A03
time: 110분
updated: 2026-11-08
---
# MCPP-A03 atomic wait/notify로 동기화 프리미티브 구현

## 학습 목표

- C++20 `std::atomic<T>::wait(old)` 의 계약 — "값이 `old` 와 **같은 동안만** 잔다, 비교와 잠들기는 notify 에 대해 원자적이다, 그냥 돌아올 수도 있다(가짜 깨어남)" — 을 말하고, 그 계약 하나로 깨움 분실(lost wakeup)이 사라지는 이유와 **그래도 깨움 분실을 만드는 세 가지 실수**를 코드로 보일 수 있다.
- `std::atomic::wait/notify_one/notify_all` 만으로 카운팅 세마포어, 래치, 3 상태(0/1/2) 뮤텍스를 짜고, 강제 스케줄(진짜 wait 직전 훅)로 "검사 ↔ 잠들기" 창을 매번 재현해 결정적으로 검증한다.
- "대기자 수를 세어 notify 를 건너뛴다" 의 정확한 조건(SB 모양 → seq_cst 네 개)과 실제 이득을 **이 맥에서 잰 숫자**로 말할 수 있다: libc++ 가 이미 대기자를 세므로 단일 스레드 이득은 0.2~0.9 ns 뿐이고, 대기자 카운트를 어느 캐시 줄에 두느냐가 경합 처리량을 2~3 배 바꾼다.
- 이 맥 libc++ 가 wait/notify 아래에서 실제로 하는 일 — 64 번 폴링 → 4 µs 까지 스핀 → `__ulock_wait(UL_COMPARE_AND_WAIT64)`, 32 비트 변수는 전역 표에서 자고 `notify_one` 이 **전부 깨운다** — 을 역어셈과 lldb 로 확인한 그대로 설명할 수 있다.
- MCU 로 옮길 때: `wait` 는 OS 가 있어야 의미가 있다(스레드 없는 libc++ 은 스핀 + notify 빈 함수). RTOS 세마포어·태스크 알림과 어떻게 대응하는지 말할 수 있다.

## 1. 문제

"세마포어를 atomic 하나로 짜 보세요. 잠들어 있는 스레드는 어떻게 깨우죠? 깨움을 놓치지 않는다는 걸 어떻게 보장하나요?" — 동시성 면접에서 스핀락(OS-M07) 다음 질문이다. 펌웨어에서도 같은 질문이 "ISR 이 준 세마포어를 태스크가 놓치지 않는 이유" 로 나온다(RTOS-B08/B09). C++20 은 이걸 위해 **값 기반 대기**(`atomic::wait / notify`)를 표준에 넣었다. Linux futex(OS-A01), macOS `__ulock_wait` / `os_sync_wait_on_address` 를 이식성 있게 감싼 것이다.

핵심은 한 문장이다: **"이 값이 아직 `old` 면 재워라" 를 커널이 원자적으로 확인한다.** 사용자 코드가 "값을 봤다(0) → 잘까?" 하는 사이에 다른 스레드가 값을 바꾸고 notify 해도, 커널의 비교가 실패하므로 잠들지 않는다. 조건 변수에서 뮤텍스가 하던 일을 비교 연산 하나가 한다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">wait(old) 의 계약: 검사와 잠들기 사이의 창을 커널 비교가 닫는다</text>
  <text x="14" y="44" fill="currentColor" font-weight="bold">대기자 (acquire)</text>
  <text x="14" y="200" fill="currentColor" font-weight="bold">해제자 (release)</text>
  <line x1="120" y1="60" x2="720" y2="60" stroke="currentColor"/>
  <line x1="120" y1="216" x2="720" y2="216" stroke="currentColor"/>
  <rect x="130" y="70" width="130" height="28" rx="3" class="f-mut" stroke="currentColor"/>
  <text x="138" y="88" fill="currentColor" font-family="monospace">v = count.load() → 0</text>
  <rect x="270" y="70" width="150" height="28" rx="3" class="f-warn" stroke="currentColor"/>
  <text x="278" y="88" fill="currentColor">창: "0 봤음, 아직 안 잠" </text>
  <rect x="430" y="70" width="280" height="28" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="438" y="88" fill="currentColor" font-family="monospace">wait(v): 커널이 *addr == v ? 잠듦 : 반환</text>
  <rect x="290" y="228" width="200" height="28" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="298" y="246" fill="currentColor" font-family="monospace">count = 1; notify_one()</text>
  <path d="M390 228 L390 102" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="396" y="168" fill="currentColor">창 안에 끼어든 release</text>
  <path d="M490 236 L560 102" stroke="currentColor" stroke-width="2"/>
  <path d="M550 106 L560 102 L560 113" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="530" y="160" fill="currentColor">커널 비교: 1 ≠ 0 → 바로 반환</text>
  <text x="130" y="128" fill="currentColor">규칙 ① old 는 "검사에 쓴 그 값" 이어야 한다 (다시 읽은 값이면 → 영원히 잠듦, Part B)</text>
  <text x="130" y="146" fill="currentColor">규칙 ② 돌아왔다고 조건이 참인 것은 아니다 (가짜 깨어남, 표 충돌) → 반드시 루프에서 재검사</text>
  <text x="130" y="282" fill="currentColor">규칙 ③ notify 를 건너뛰려면 "값 바꾸기 → 대기자 수 읽기" 순서 + 양쪽 seq_cst (SB 모양)</text>
</svg>
```

표준 문구 (C++20 N4861 [atomics.types.operations] `wait`, [atomics.wait]): `wait(old, order)` 는 ① `load(order)` 로 읽어 `old` 와 **값 표현(value representation)** 을 비교하고, 다르면 반환 ② 같으면 notify 로 풀리거나 **가짜로(spuriously)** 풀릴 때까지 막힌다 ③ ① 부터 반복. 그리고 "M 에 대한 wait 가 X 의 결과를 보고 막혔고, M 의 수정 순서에서 X 뒤에 Y 가 있고, Y 가 notify 호출보다 happens-before 이면, 그 wait 는 그 notify 로 풀릴 자격이 있다" ([atomics.wait]/4) — 이게 "깨움 분실 없음" 의 표준식 표현이다.

### Part A — 세 프리미티브 (`std::atomic<int64_t>` + wait/notify 만)

- **Semaphore**: `count_` (남은 허가) + `waiters_` (잠들려고 등록한 수). `try_acquire` = CAS 감소, `acquire` = [등록 → `wait(count_, 0)` → 해제] 반복, `release(n)` = 증가 후 **대기자가 있을 때만** notify (n==1 → one, n&gt;1 → all).
- **Latch**: `count_down` = `fetch_sub(release)`, 0 이 되는 순간에만 `notify_all`. `wait` = `load(acquire)` 루프, **읽은 값 그대로** wait.
- **Mutex**: 0 = 풀림, 1 = 잠김, 2 = 잠김 + 대기자 있을 수 있음. CAS 0→1 빠른 경로, 아니면 `exchange(2)` 가 0 을 줄 때까지 `wait(2)`, `unlock` 은 2 였을 때만 `notify_one`. 이 세 상태 설계가 왜 필요한지(1/2 를 안 나누면 매 unlock 이 notify)와 그 미묘한 버그들은 OS-A01 (Drepper "Futexes Are Tricky") 몫 — 여기선 10 줄로 쓰고 측정한다.
- 스트레스: 8 스레드 × 2만 회 풀(허가 3) 동시 사용 ≤ 3, 8 × 5만 뮤텍스 카운터 정확, 래치 200 라운드 데이터 게시.

### Part B — 계약 검증: 강제 스케줄과 notify 호출 수

- 하네스의 `plat_wait` 은 진짜 `a.wait()` **직전에** 훅을 한 번 부른다. 훅이 `release` / `count_down` / `unlock` 을 실행하면 "검사는 끝났고 아직 안 잠든" 창에 상대가 끼어든 상황이 **단일 스레드에서 매번 똑같이** 생긴다. 올바른 구현이면 진짜 wait 는 값이 바뀐 걸 보고 즉시 돌아온다.
- `plat_notify_*` 는 호출 수를 센다 → "대기자 없으면 notify 0 번", "래치 notify_all 정확히 1 번", "뮤텍스 경합 없으면 0 번, 경합 뒤엔 보수적 1 번 더" 를 정확한 숫자로 CHECK.
- 진짜 2 스레드: 대기자가 커널에서 확실히 잠든 뒤(20 ms) release → 깨어나야 한다.

### Part C — 센서 파이프라인

드라이버 3 개(IMU/기압/지자기)가 설정 구조체(plain)를 쓰고 `init_done` 래치를 내린다. "ISR" 생산자는 `free_slots`(16) / `filled`(0) 두 세마포어로 16 칸 링에 샘플 10만 개 + 독약 3 개를 넣고, 작업자 3 개가 `init_done.wait()` 후 설정을 검사하고 `bus` 뮤텍스 안에서 링 tail 과 통계를 갱신한다. 모두 한 번씩, 체크섬 일치, 임계 구역 동시 진입 최대 1.

## 2. Problem (English)

> **Build synchronization primitives on C++20 `std::atomic<T>::wait / notify_one / notify_all` only.**
>
> 1. `Semaphore(int64_t initial)` with `try_acquire()`, `acquire()`, `release(n = 1)`. `release` must not call notify when nobody is (about to be) waiting; prove the "skip notify" is safe.
> 2. `Latch(n)` with `count_down(n = 1)`, `try_wait()`, `wait()`, `arrive_and_wait()`. Exactly one `notify_all`, when the count reaches zero. Writes before any `count_down` must be visible after `wait` returns.
> 3. `Mutex` with `lock()`, `try_lock()`, `unlock()` using the three-state protocol (0 unlocked, 1 locked, 2 locked with possible waiters). Uncontended `lock/unlock` must make zero wait/notify calls.
> 4. All calls go through `plat_wait / plat_notify_one / plat_notify_all` (provided). A test hook runs right before the real `wait`; your primitives must survive "the other side acts between your check and your sleep".
> 5. A pipeline: 3 driver-init threads → latch; an ISR-like producer → 16-slot ring with two semaphores → 3 workers holding a bus mutex. 100 000 samples, each consumed exactly once.
>
> Constraints: no `std::mutex`, `std::condition_variable`, `std::counting_semaphore` in your primitives. Spurious wakeups are allowed by the standard.
>
> Follow-ups: Why can't a waiter miss a notify? What makes a waiter count race? What does libc++ on macOS do under `wait` — spin, sleep, which syscall? Does `notify_one` wake one? Why does `std::counting_semaphore::release` use `notify_all`? How fast is your semaphore vs `std::counting_semaphore`, your mutex vs `std::mutex`, and why? What would `atomic::wait` mean on a bare-metal Cortex-M?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread starter.cpp -o starter && ./starter
```

처음 실행 결과 (실제 출력 앞부분과 끝, exit 1, 0.09 초 — 감시 스레드가 있어 멈추지 않는다):

```text
FAIL starter.cpp:198  s.try_acquire() && s.try_acquire()
FAIL starter.cpp:199  !s.try_acquire() && s.value() == 0
FAIL starter.cpp:201  s.value() == 3
FAIL starter.cpp:203  s.value() == 0 && s.waiters() == 0
…
SKIP 4 stress checks (Part A): unit checks failed
FAIL starter.cpp:279  g_before_wait.load() == nullptr
…
SKIP 1 stress checks (Part B): unit checks failed
SKIP 4 stress checks (Part C): unit checks failed
6/34 passed
```

TODO 는 A1–A9 (세 클래스의 메서드 아홉 개), C1 `isr_producer`, C2 `worker`. 하네스(플랫폼 래퍼, 훅, 감시 스레드, CHECK)는 고치지 않는다. 각 Part 의 스레드 스트레스는 그 Part 의 단위 체크가 다 통과해야 돈다 — 틀린 프리미티브로 스레드를 돌려 멈추는 일을 막는다. 그래도 멈추면 60 초 감시 스레드가 `FAIL watchdog` 을 찍고 exit 1.

## 4. 힌트

<details>
<summary>힌트 1 — 세마포어 acquire: 무엇을 old 로 넘기나?</summary>

잠드는 조건은 "허가가 0" 이다. 그러니 `wait(count_, 0)`. `try_acquire` 가 실패한 뒤 `count_` 를 다시 읽어 그 값으로 기다리면 안 된다 — 그 사이 누군가 1 로 만들었으면 "1 인 동안 잔다" 가 되어 영원히 잔다. 돌아오면 이유를 묻지 말고 `try_acquire` 부터 다시.

</details>

<details>
<summary>힌트 2 — notify 를 건너뛰는 조건</summary>

대기자: `waiters++` → (wait 안의) `count` 읽기. 해제자: `count += n` → `waiters` 읽기. "서로 다른 변수에 쓰고 상대 것을 읽는" 모양 = store buffering (C-A03 SB 리트머스, ARM-M05). 넷 다 seq_cst 면 "해제자는 waiters==0 을 봤는데 대기자는 count==0 을 보고 잠든" 결과가 금지된다. 순서도 중요: 해제자가 waiters 를 **먼저** 읽으면 순서와 무관하게 틀린다(demo ③).

</details>

<details>
<summary>힌트 3 — 래치: 메모리 순서</summary>

`fetch_sub(release)` 들은 전부 RMW 라 서로 release sequence 로 이어진다(MCPP-M13). 그래서 0 을 읽은 `load(acquire)` 하나가 **모든** count_down 앞의 쓰기와 동기화된다. notify_all 은 `old == n` (이번 감소로 0 이 됨) 일 때만.

</details>

<details>
<summary>힌트 4 — 3 상태 뮤텍스</summary>

```text
lock:   c = 0; CAS(c → 1) 성공이면 끝.
        c != 2 이면 c = exchange(2)          // "나 기다린다" 표시
        while (c != 0) { wait(2); c = exchange(2); }
unlock: if (exchange(0) == 2) notify_one();
```

깨어난 쪽이 1 이 아니라 2 로 잡는 이유: 다른 대기자가 남았는지 모른다. 그 대가로 마지막 unlock 이 notify 를 한 번 더 한다 (B6 이 그 숫자를 CHECK).

</details>

<details>
<summary>힌트 5 — 파이프라인 종료와 소유권</summary>

생산자: `n + kWorkers` 번 [free_slots.acquire → 쓰기 → head++ → filled.release], 마지막 kWorkers 개는 `make_sample(-1)`. 작업자: filled.acquire → bus.lock 안에서 tail 칸 꺼내기·통계 → unlock → free_slots.release → 독약이면 끝. 링 칸의 쓰기/읽기 순서는 세마포어의 release/acquire 가, tail 과 통계는 bus 가 보호한다.

</details>

## 5. 풀이

### 0. 이 맥에서 `wait` / `notify` 아래에 실제로 있는 것

추측하지 말고 확인했다. 기계와 도구:

```text
$ sysctl -n machdep.cpu.brand_string hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu hw.perflevel2.physicalcpu
Apple M6
2
4
6
$ cc --version | head -1
Apple clang version 21.0.0 (clang-2100.0.123.102)
$ sysctl hw.cachelinesize
hw.cachelinesize: 128
```

**① 헤더** (`MacOSX26.4.sdk/usr/include/c++/v1/__atomic/atomic_sync.h`, `__thread/poll_with_backoff.h`, `__atomic/contention_t.h`):

- `__libcpp_thread_poll_with_backoff`: 먼저 `high_resolution_clock::now()` 로 시작 시각을 잡고, 값을 **64 번** 바로 폴링(`__libcpp_polling_count = 64`), 그 뒤엔 매번 시계를 읽어 경과 시간을 backoff 함수에 넘긴다.
- `__atomic_wait_backoff_impl`: 경과가 `chrono::microseconds(4)` 이하면 아무것도 안 하고 다시 폴링(= **약 4 µs 바쁜 스핀**), 넘으면 `__libcpp_atomic_monitor` → 값 재확인 → `__libcpp_atomic_wait` (진짜 잠).
- `__cxx_contention_t` 는 Linux 에선 `int32_t`, 그 밖(이 맥)에선 `int64_t`. 대기 변수가 **정확히** `__cxx_atomic_impl<int64_t>` 면 그 변수 자체에서 자고, 아니면 주소를 해시한 **전역 표 칸**의 64 비트 카운터에서 잔다 ("value sequence laundering").
- 스레드가 없는 빌드(`!_LIBCPP_HAS_THREADS`): wait = `__spinning_backoff_policy` (그냥 계속 폴링), `__atomic_notify_one/all` = **빈 함수**.

**② 바이너리** (`demo_platform_calls.cpp`, uint32 하나·int64 하나를 진짜로 재웠다 깨움):

<!-- code: demo_platform_calls.cpp -->

```text
$ nm -m dpc | grep -E 'atomic_(wait|notify|monitor)' | c++filt      (요약)
(undefined) external std::__1::__libcpp_atomic_wait(std::__1::__cxx_atomic_impl<long long, ...> const volatile*, long long) (from libc++)
(undefined) external std::__1::__libcpp_atomic_wait(void const volatile*, long long) (from libc++)
(undefined) external std::__1::__cxx_atomic_notify_one(std::__1::__cxx_atomic_impl<long long, ...> const volatile*) (from libc++)
(undefined) external std::__1::__cxx_atomic_notify_one(void const volatile*) (from libc++)
(undefined) external std::__1::__libcpp_atomic_monitor(void const volatile*) (from libc++)
```

인라인된 폴링 루프(`xcrun llvm-objdump -d`, 핵심 줄만): 64 회 카운터와 4 µs 비교가 그대로 보인다. `yield`/`wfe` 같은 쉼 명령은 **없다**.

```text
10000088c:  bl  <stub>              ; std::__1::chrono::steady_clock::now()   (otool -Iv 로 스텁 이름 확인)
1000008ac:  ldapr w25, [x8]         ; 값 읽기 (memory_order 에 따라 ldapr / ldar / ldr 분기)
1000008dc:  cmp  w24, #0x3f         ; 64 번 폴링
1000008e4:  bl   <stub>             ; steady_clock::now()
1000008f8:  cmp  x8, #0xfa1         ; 경과 < 4001 ns 이면 다시 폴링
100000908:  bl   <stub>             ; __libcpp_atomic_monitor(void const volatile*)
100000954:  bl   <stub>             ; __libcpp_atomic_wait(void const volatile*, long long)
```

libc++.1.dylib 안쪽은 lldb 로 역어셈했다 (`lldb -b -o 'b main' -o run -o "disassemble -n '...'" ./probe`). 주소는 부팅마다 다르다. 요점만:

```text
std::__1::__libcpp_atomic_wait(void const volatile*, long long):          ; 32 비트 등 "다른 폭" 경로
    bl     std::__1::__hash_memory(void const*, unsigned long)
    and    x8, x0, #0xff                    ; 표 256 칸
    add    x9, x9, #0x280                   ; std::__1::__contention_table_global
    addpt  x20, x9, x8, lsl #6              ; 칸 = 64 B
    ldadd  x8, x8, [x20]                    ; 대기자 수 +1
    dmb    ish
    mov    w0, #0x5                         ; 연산 5
    bl     <stub>                           ; → __ulock_wait (아래 lldb 로 확인)
    ldaddl x8, x8, [x20]                    ; 대기자 수 -1
std::__1::__atomic_notify_global_table(void const*):                       ; notify_one/all(void const volatile*) 둘 다 여기로
    ldaddal x9, x9, [x1]                    ; 칸의 platform_state +1 (값 세탁)
    ldar   x8, [x8]                         ; 대기자 수
    cbz    x8, <ret>                        ; 0 이면 시스템 호출 없이 끝
    mov    w0, #0x105                       ; 5 | 0x100  ← notify_one 이어도 "전부 깨움"
    b      <stub>                           ; → __ulock_wake
std::__1::__cxx_atomic_notify_one(__cxx_atomic_impl<long long> const volatile*):   ; int64 경로
    ldar   x8, [x8]                         ; __contention_table_native 칸의 대기자 수
    cbz    x8, <ret>
    mov    w0, #0x5                         ; 하나만
    bl     <stub>                           ; → __ulock_wake
```

lldb 브레이크포인트(`br set -n __ulock_wait -n __ulock_wake -n os_sync_wait_on_address ...`)에 실제로 걸린 것:

```text
&a32=0x100008000  &a64=0x100008008
frame #1: libc++.1.dylib`std::__1::__libcpp_atomic_wait(void const volatile*, long long) + 88
  x0 = 0x0000000000000005   x1 = 0x00000001f25374c8  libc++.1.dylib`std::__1::__contention_table_global + 8776
frame #1: libc++.1.dylib`std::__1::__cxx_atomic_notify_one(void const volatile*) + 16
  x0 = 0x0000000000000105   x1 = 0x00000001f25374c8  libc++.1.dylib`std::__1::__contention_table_global + 8776
frame #1: libc++.1.dylib`std::__1::__libcpp_atomic_wait(std::__1::__cxx_atomic_impl<long long, ...> const volatile*, long long) + 96
  x0 = 0x0000000000000005   x1 = 0x0000000100008008  dpc`a64
frame #1: libc++.1.dylib`std::__1::__cxx_atomic_notify_one(std::__1::__cxx_atomic_impl<long long, ...> const volatile*) + 80
  x0 = 0x0000000000000005   x1 = 0x0000000100008008  dpc`a64
```

`os_sync_wait_on_address` 는 한 번도 안 걸렸다 — 이 libc++ 는 비공개 `__ulock_wait/__ulock_wake` 를 직접 쓴다. (공개 API `os_sync_wait_on_address` 는 SDK 헤더 `os/os_sync_wait_on_address.h` 에 `__API_AVAILABLE(macos(14.4), ...)` 로 있다 — OS-A01 이 macOS 대응으로 다룬다.) 연산 번호 5 = `UL_COMPARE_AND_WAIT64`, `0x100` = `ULF_WAKE_ALL` 은 xnu `bsd/sys/ulock.h` 기준 (그 헤더는 SDK 에 없다 — 번호는 위에서 본 값, 이름은 xnu 소스 기준).

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">이 맥 libc++ 의 a.wait(old) / a.notify_one() (실측 경로)</text>
  <rect x="10" y="34" width="150" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="18" y="52" fill="currentColor">① 폴링 64 번</text>
  <text x="18" y="67" fill="currentColor" font-family="monospace">ldapr / ldar / ldr</text>
  <rect x="180" y="34" width="160" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="188" y="52" fill="currentColor">② 4 µs 까지 스핀</text>
  <text x="188" y="67" fill="currentColor">매번 steady_clock::now()</text>
  <rect x="360" y="34" width="370" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="368" y="52" fill="currentColor">③ 대기자 +1 (ldadd), dmb ish, 커널에서 잠</text>
  <text x="368" y="67" fill="currentColor" font-family="monospace">__ulock_wait(5, addr, old)</text>
  <path d="M160 54 L178 54" stroke="currentColor" stroke-width="2"/>
  <path d="M340 54 L358 54" stroke="currentColor" stroke-width="2"/>
  <text x="10" y="104" fill="currentColor" font-weight="bold">어디서 자나: T 의 폭에 따라</text>
  <rect x="10" y="114" width="350" height="70" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="18" y="132" fill="currentColor" font-weight="bold">std::atomic&lt;int64_t&gt; (long long)</text>
  <text x="18" y="150" fill="currentColor">변수 자체 주소에서 잔다 (native 표 = 대기자 수만)</text>
  <text x="18" y="168" fill="currentColor" font-family="monospace">notify_one → __ulock_wake(5): 하나</text>
  <rect x="380" y="114" width="350" height="70" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="388" y="132" fill="currentColor" font-weight="bold">uint32_t, long(ptrdiff_t), 그 밖</text>
  <text x="388" y="150" fill="currentColor">해시 → 전역 표 256 칸 × 64 B 의 카운터에서 잔다</text>
  <text x="388" y="168" fill="currentColor" font-family="monospace">notify_one → __ulock_wake(0x105): 전부</text>
  <text x="10" y="212" fill="currentColor">notify 공통: 칸의 대기자 수가 0 이면 시스템 호출 없이 반환 (ldar + cbz) — libc++ 가 이미 "notify 생략" 을 한다</text>
  <text x="10" y="232" fill="currentColor">전역 표 경로는 여기에 ldaddal 하나 더 (값 세탁). 다른 변수가 같은 칸에 걸리면 서로 깨운다 = 가짜 깨어남</text>
  <text x="10" y="252" fill="currentColor">std::counting_semaphore / std::latch 의 카운터는 ptrdiff_t = long → 전역 표 경로 (nm 으로 확인)</text>
  <text x="10" y="272" fill="currentColor">스레드 없는 libc++ 빌드: wait = 끝없는 폴링, notify = 빈 함수 (헤더 확인) → MCU 에선 OS 훅이 필요</text>
</svg>
```

폭의 차이를 실제로 본 것 (`demo_notify_width.cpp`: 대기자 4 개가 값 0 에서 잠든 뒤 값을 1 로 바꾸고 `notify_one` 한 번, 30 ms 뒤 몇 개가 돌아왔나):

<!-- code: demo_notify_width.cpp -->

```text
trial 0: notify_one 뒤 돌아온 대기자 (4 개 중)  uint32_t=4  int64_t=1
trial 1: notify_one 뒤 돌아온 대기자 (4 개 중)  uint32_t=4  int64_t=1
trial 2: notify_one 뒤 돌아온 대기자 (4 개 중)  uint32_t=4  int64_t=1
trial 3: notify_one 뒤 돌아온 대기자 (4 개 중)  uint32_t=4  int64_t=1
trial 4: notify_one 뒤 돌아온 대기자 (4 개 중)  uint32_t=4  int64_t=1
```

표준은 notify_one 이 "적어도 하나" 를 풀면 되므로 둘 다 합법이다. 하지만 3 상태 뮤텍스를 `uint32_t` 로 짜면 이 맥에서 unlock 마다 대기자 **전원**이 깨어나 다시 싸운다(thundering herd). 그래서 이 레슨의 모든 상태 변수는 `int64_t` 다. 이식 규칙: **플랫폼의 기본 대기 폭에 맞춰라** — Linux futex 는 32 비트(OS-A01), 이 맥 libc++ 는 64 비트.

### Part A — Semaphore

```cpp
bool try_acquire() {
    i64 c = count_.load(relaxed);
    while (c > 0)
        if (count_.compare_exchange_weak(c, c - 1, acquire, relaxed)) return true;   // 실패하면 c 가 새 값으로
    return false;
}
void acquire() {
    while (!try_acquire()) {
        waiters_.fetch_add(1, seq_cst);        // ① 등록
        plat_wait(count_, i64{0}, seq_cst);    // ② 0 인 동안만 잔다
        waiters_.fetch_sub(1, seq_cst);
    }
}
void release(i64 n) {
    count_.fetch_add(n, seq_cst);              // ③ 값 먼저
    if (waiters_.load(seq_cst) == 0) return;   // ④ 그다음 대기자 확인
    n == 1 ? plat_notify_one(count_) : plat_notify_all(count_);
}
```

**왜 notify 생략이 안전한가 (SB 논증).** seq_cst 연산 네 개(① W_w, ② 의 비교 load R_c, ③ W_c, ④ R_w)는 하나의 전체 순서 S 에 놓인다.

| S 에서의 경우 | 결과 |
|---|---|
| ① 이 ④ 보다 앞 | ④ 는 등록을 본다 (≥1) → notify 한다 |
| ④ 가 ① 보다 앞 | ③ sb ④ &lt;S ① sb ② 이므로 ③ 이 ② 보다 앞 → ② 는 증가된 count 를 본다 → 잠들지 않는다 |

어느 쪽이든 "해제자는 대기자 0 을 봤고, 대기자는 count 0 을 보고 잠들었다" 는 불가능하다. 하나라도 relaxed/acquire/release 로 내리면 이 표가 무너진다 (C-A03 연습 4 Peterson, ARM-M05 SB 리트머스와 같은 모양). 그럼 실제로 깨지나? `demo_lost_wakeup.cpp` ④ 에서 relaxed 판을 20 만 라운드 핑퐁으로 돌렸더니 **0 회** — 이유는 아래 Part B 의 "relaxed 는 왜 안 깨졌나".

**왜 notify_one 이면 충분한가.** 대기자는 count==0 일 때만 잔다. release(1) 마다 notify_one 하나 → 증가 1 당 대기자 하나가 깨어나 시도한다. 깨어난 쪽이 끼어든 다른 스레드에게 허가를 빼앗기면 다시 등록하고 잔다 — 허가는 누군가 가져갔으니 손실이 아니다. 반대로 libc++ 의 `__atomic_semaphore_base::release` 는 "이전 값이 0 일 때만" notify 하는데, 그래서 **notify_all** 을 쓴다. 같은 조건에 notify_one 을 쓰면 깨진다(실수 표, demo ②: 10/10 회 대기자 하나가 영원히 잠듦).

**libc++ 가 이미 대기자를 센다면, 내 waiters_ 는 왜?** notify 를 부르기 전에 건너뛰므로 라이브러리 호출·주소 해시·(전역 표면) `ldaddal` 이 사라진다. 그 이득을 쟀다 (B1, 아래 표). 정직한 결론: **단일 스레드에선 0.2~0.9 ns, 작다.**

### Part A — Latch

```cpp
void count_down(i64 n) { if (count_.fetch_sub(n, release) == n) plat_notify_all(count_); }
void wait() const {
    for (i64 c = count_.load(acquire); c != 0; c = count_.load(acquire))
        plat_wait(count_, c, acquire);   // 읽은 c 그대로
}
```

래치는 notify 가 평생 한 번이라 대기자 카운트를 둘 이유가 없다(std::latch 도 같다). 메모리 순서: 각 `fetch_sub(release)` 는 release sequence 의 머리이고 뒤의 RMW 들이 그 sequence 를 잇는다 → 0 을 읽은 acquire load 는 **모든** count_down 과 synchronizes-with (MCPP-M13 Part A 의 작업 보드와 같은 규칙). 스트레스: 8 스레드 × 200 라운드, 다른 스레드 칸이 하나도 안 틀림. TSan 도 조용했다 (아래 6 절).

### Part A — Mutex (3 상태, 10 줄)

| 상태 | 뜻 | 들어오는 길 |
|---|---|---|
| 0 | 풀림 | unlock 의 `exchange(0)` |
| 1 | 잠김, 대기자 없음 | 빠른 경로 CAS 0→1 |
| 2 | 잠김, 대기자 있을 수 있음 | 경합 시 `exchange(2)`, 깨어난 쪽도 2 |

`lock` 의 빠른 경로는 CAS 한 번, `unlock` 은 `exchange` 한 번 — wait/notify 0 회 (B5 가 1000 번으로 CHECK). 경합이 생기면 상태가 2 로 "끈적하게" 남아, 마지막 대기자가 나간 뒤에도 unlock 이 notify 한 번을 더 한다 (B6: `notify_one == 2`). Drepper 가 mutex2/mutex3 로 다듬은 부분, 그리고 왜 1/2 를 구분하지 않으면 안 되는지는 **OS-A01** 이 futex 로 유도한다.

### Part B — 강제 스케줄로 창을 재현

진짜 스레드로 "검사 직후, 잠들기 직전" 에 상대를 끼워 넣는 건 운에 맡겨야 한다. 그래서 하네스가 그 지점에 훅을 둔다:

```cpp
template <class T> static void plat_wait(const std::atomic<T> &a, T old, std::memory_order mo) {
    g_st.waits.fetch_add(1, relaxed);
    if (void (*h)() = g_before_wait.exchange(nullptr)) h();   // 상대가 여기서 끼어든다 (한 번)
    a.wait(old, mo);
}
```

| 테스트 | 훅이 하는 일 | 올바른 구현의 결과 (CHECK) |
|---|---|---|
| B1 세마포어 | `release(1)` | wait(0) 즉시 반환, 허가 획득, waits 1 · notify_one 1 |
| B2 세마포어 | (훅 없음) release/acquire 1000 쌍 | notify 0, waits 0 |
| B3 세마포어 | `release(3)` | notify_all 1, 남은 허가 2 |
| B4 래치 | 마지막 `count_down` | count_down 2 번까지 notify 0, 그 뒤 notify_all 1, 다시 wait 해도 waits 그대로 |
| B5 뮤텍스 | (훅 없음) lock/unlock 1000 | wait/notify 0, 상태 0 |
| B6 뮤텍스 | 이미 잠근 상태에서 `lock` → 훅이 `unlock` | 1→2, 훅: 2→0 + notify, wait(2) 즉시 반환, 0→2 획득. 상태 2, notify_one 1 → unlock 후 0, notify_one 2 |
| B7 2 스레드 | 대기자 등록 + 20 ms 뒤 release | 깨어난다 (분실이면 60 초 감시가 잡는다) |

B6 은 단일 스레드에서 같은 뮤텍스를 두 번 잠그는 이상한 모양이지만, 이 뮤텍스는 주인을 확인하지 않으므로 "exchange(2) 와 wait(2) 사이에 unlock" 이라는 정확히 그 창을 결정적으로 만든다.

**깨움 분실을 일부러 만들기** — `demo_lost_wakeup.cpp` (멈춘 스레드는 구출 플래그 + notify_all 로 풀어서 센다):

<!-- code: demo_lost_wakeup.cpp -->

```text
$ uptime
15:23  up 6 days, 15:45, 10 users, load averages: 4.54 5.15 4.28
$ ./dlw
① wait(v)  (검사한 값)     : wait 반환까지       0.6 us  → 정상
① wait(c.load())  (다시 읽음) : wait 반환까지  201514.0 us  → STUCK, 감시 쪽이 200 ms 뒤 구출
② 0→1 일 때만 notify_one        : 10/10 회 대기자 하나가 영원히 잠듦
② 0→1 일 때만 notify_all (libc++) :  0/10 회 대기자 하나가 영원히 잠듦
② 대기자 카운트 + notify_one (정답) :  0/10 회 대기자 하나가 영원히 잠듦
③ waiters 를 count 증가 전에 읽음 :  5/5 회 대기자가 영원히 잠듦
④ 대기자 카운트 seq_cst (정답) : 200000 라운드 중 깨움 분실 0 회  (30 ms)
④ 대기자 카운트 relaxed (SB 위험) : 200000 라운드 중 깨움 분실 0 회  (46 ms)
```

- ① 강제 스케줄이라 결정적이다: 검사(1 을 봄) → 훅에서 마지막 count_down(0) → `wait(c.load())` = `wait(0)` = "0 인 동안 잔다" → 영원히.
- ② 대기자 W1·W2 가 커널에서 자는 중 release 두 번: 첫 번째(0→1)는 notify_one 으로 W1 하나를 깨우고, 두 번째(1→2)는 "0 이 아니었으니" 건너뛴다. W1 이 하나 가져가면 count 1 이 남는데 W2 는 계속 잔다 (int64 경로라 notify_one 이 진짜 하나). libc++ 가 같은 조건에 notify_all 을 쓰는 이유다.
- ③ "읽고 → (대기자가 등록·잠듦) → 증가" 순서. seq_cst 여도 소용없다 — 순서 자체가 틀렸다.
- ④ **relaxed 는 왜 안 깨졌나 (정직한 보고).** 20 만 라운드가 30~46 ms, 라운드당 약 150~230 ns — 대기자가 거의 항상 ② 4 µs 스핀 단계에서 값을 보고 돌아왔다는 뜻이다. 진짜로 잠드는 길은 `ldadd` + `dmb ish` 뒤 커널 비교라, 해제자의 relaxed 읽기가 "옛 대기자 수" 를 4 µs 넘게 볼 수는 없다. 즉 이 맥 libc++ 의 스핀 + 배리어 + 커널 비교가 SB 창을 **가리고 있다**. 표준상 relaxed 판은 여전히 틀렸고(위 SB 표), 스핀이 없는 구현(직접 `__ulock_wait`/futex 를 부르는 코드, 다른 표준 라이브러리, 실시간 커널)에선 다르게 나올 수 있다 — 관찰 0 ≠ 보장.

### Part C — 파이프라인

```text
"ISR" 생산자                      16 칸 링                       작업자 ×3
free_slots.acquire() ─┐                                   ┌─ init_done.wait() → cfg crc 검사
ring[head%16] = s     │  filled (0..16) ─────────────────▶│  filled.acquire()
head++                │                                   │  bus.lock(): ring[tail%16], tail++, 통계
filled.release() ─────┘◀───────────────── free_slots (16) │  bus.unlock(); free_slots.release()
(n 개 + 독약 3 개)                                         └─ 독약이면 끝
```

| 공유 데이터 | 누가 순서를 보장하나 |
|---|---|
| `cfg[3]` (plain) | `init_done`: count_down(release) → wait(acquire) |
| 링 칸 쓰기 → 읽기 | `filled.release` (seq_cst RMW) → `filled.acquire` 의 CAS(acquire) |
| 링 칸 읽기 → 다음 쓰기 | `free_slots.release` → 생산자 `free_slots.acquire` |
| `tail`, 통계 | `bus` (CAS/exchange acquire ↔ exchange release) |

결과: `Part C stress: samples=100000 ring=16 workers=3`, 합 = N(N-1)/2, 체크섬 실패 0, 설정 오류 0, 임계 구역 동시 진입 최대 1, 끝난 뒤 free_slots 16 · filled 0 · bus 0.

펌웨어 대응 (RTOS-B09 연습 4 의 UART 링과 같은 모양): FreeRTOS 라면 `xSemaphoreGiveFromISR(filled, &woken)` + `portYIELD_FROM_ISR(woken)`, 작업자는 `xSemaphoreTake(filled, portMAX_DELAY)`, 버스는 우선순위 상속이 있는 `xSemaphoreCreateMutex()` (RTOS-B10, RTOS-M04). 드라이버 초기화 래치는 이벤트 그룹 `xEventGroupWaitBits(..., IMU|BARO|MAG, pdTRUE /*clear*/, pdTRUE /*all*/, ...)` (RTOS-B13, 이 레슨 연습 3). 차이점 하나: FreeRTOS 세마포어 take 는 허가가 있어도 매번 커널 임계 구역(`taskENTER_CRITICAL`)을 거친다 — "경합 없으면 커널 0 회" 같은 빠른 경로가 없다. MCU 엔 사용자/커널 경계가 없으니 그 비용이 수십 사이클 수준이라 괜찮은 것이다. 더 가벼운 길은 태스크 알림(RTOS-M01, 연습 4).

### 측정 — 직접 만든 것 vs std (`demo_bench.cpp`)

<!-- code: demo_bench.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread demo_bench.cpp -o dbench && ./dbench   # 한 번 ≈ 3 초
```

여섯 개 작성 세션이 같은 맥에서 동시에 벤치마크를 돌리던 중이다. 아래는 load average 3.3~3.6 일 때 연속 3 회 (`uptime`: `15:56 ... load averages: 3.34 2.62 2.79` → `3.64 2.69 2.82`). **절대 ns 보다 배수를 믿을 것.** 시계는 `clock_gettime_nsec_np(CLOCK_UPTIME_RAW)`, `mach timebase 125/3 (1 tick = 41.67 ns)` — B2 의 중앙값이 125/167/208 처럼 41.67 의 배수로 뭉치는 이유다 (그래서 전체 시간 / 횟수 평균도 같이 찍었다).

**B1 — 대기자 없는 release+acquire 한 쌍 (단일 스레드, ns)**

| 구현 | run 1 | run 2 | run 3 |
|---|---|---|---|
| Semaphore (대기자 없으면 notify 생략) | 3.35 | 3.46 | 3.98 |
| 같은 알고리즘, 매번 notify_one, int64 | 3.59 | 3.83 | 4.15 |
| 매번 notify_one, uint32 (전역 표 `ldaddal`) | 4.22 | 4.34 | 4.89 |
| `std::counting_semaphore<>` | 16.70 | 17.08 | 20.31 |
| `std::binary_semaphore` | 16.73 | 17.75 | 19.68 |
| `std::counting_semaphore` release + **try_acquire** | 3.81 | 3.39 | 3.80 |
| Mutex lock+unlock | 2.14 | 2.54 | 2.51 |
| `std::mutex` lock+unlock | 3.98 | 3.48 | 3.91 |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">B1 실측 (run 1, ns, 짧을수록 좋음) — std 세마포어의 13 ns 는 acquire 의 시계 읽기</text>
  <g>
    <text x="10" y="44" fill="currentColor">Semaphore (생략)</text><rect x="250" y="32" width="67" height="16" class="f-ok" stroke="currentColor"/><text x="322" y="44" fill="currentColor">3.35</text>
    <text x="10" y="68" fill="currentColor">매번 notify, int64</text><rect x="250" y="56" width="72" height="16" class="f-mut" stroke="currentColor"/><text x="327" y="68" fill="currentColor">3.59</text>
    <text x="10" y="92" fill="currentColor">매번 notify, uint32</text><rect x="250" y="80" width="84" height="16" class="f-mut" stroke="currentColor"/><text x="339" y="92" fill="currentColor">4.22</text>
    <text x="10" y="116" fill="currentColor">std::counting_semaphore</text><rect x="250" y="104" width="334" height="16" class="f-warn" stroke="currentColor"/><text x="589" y="116" fill="currentColor">16.70</text>
    <text x="10" y="140" fill="currentColor">std::binary_semaphore</text><rect x="250" y="128" width="335" height="16" class="f-warn" stroke="currentColor"/><text x="590" y="140" fill="currentColor">16.73</text>
    <text x="10" y="164" fill="currentColor">std::counting (try_acquire)</text><rect x="250" y="152" width="76" height="16" class="f-mut" stroke="currentColor"/><text x="331" y="164" fill="currentColor">3.81</text>
    <text x="10" y="188" fill="currentColor">Mutex lock+unlock</text><rect x="250" y="176" width="43" height="16" class="f-ok" stroke="currentColor"/><text x="298" y="188" fill="currentColor">2.14</text>
    <text x="10" y="212" fill="currentColor">std::mutex lock+unlock</text><rect x="250" y="200" width="80" height="16" class="f-mut" stroke="currentColor"/><text x="335" y="212" fill="currentColor">3.98</text>
  </g>
  <line x1="250" y1="26" x2="250" y2="222" stroke="currentColor"/>
  <text x="250" y="240" fill="currentColor">0</text><text x="440" y="240" fill="currentColor">10 ns</text><text x="640" y="240" fill="currentColor">20 ns</text>
</svg>
```

읽는 법:

- **notify 생략의 이득은 작다**: int64 0.2~0.4 ns, uint32 0.8~0.9 ns. libc++ 가 이미 `ldar + cbz` 로 시스템 호출을 건너뛰기 때문 (0 절). 이득은 "함수 호출 + 해시 + (uint32 면) 공유 표에 `ldaddal`" 만큼이다.
- **std 세마포어가 4~5 배 느린 이유는 notify 가 아니라 acquire**: `try_acquire` 로 바꾸면 3.4~3.8 ns 로 내 것과 같다. `acquire` 는 허가가 있어도 `__libcpp_thread_poll_with_backoff` 에 들어가고, 그 함수는 첫 폴링 **전에** `steady_clock::now()` 를 부른다 (0 절 역어셈 `10000088c`). 내 `acquire` 는 `try_acquire` 가 성공하면 대기 함수에 아예 안 들어간다.
- 뮤텍스 빠른 경로: 내 것 2.1~2.5 ns (CAS + exchange, 인라인), `std::mutex` 3.5~4.0 ns (`pthread_mutex_lock/unlock` 호출).

**B1' — 스레드 4 개가 세마포어 하나를 공유, release → acquire (안 잠듦, Mops/s, 클수록 좋음)**

| 구현 | run 1 | run 2 | run 3 |
|---|---|---|---|
| Semaphore (count·waiters 같은 줄) | 15.3 | 17.2 | 16.3 |
| Semaphore (waiters 를 다른 128 B 줄로) | 39.6 | 48.5 | 42.1 |
| 매번 notify, int64 (waiters 안 읽음) | 37.1 | 33.9 | 33.4 |
| 매번 notify, uint32 | 15.8 | 16.7 | 16.0 |
| `std::counting_semaphore` | 21.0 | 20.5 | 20.4 |

처음 봤을 땐 거꾸로였다: "notify 생략" 판이 "매번 notify" 판보다 2 배 느리다. 원인은 **캐시 줄**: 생략 판은 release 마다 `waiters_` 를 seq_cst 로 읽는데, `waiters_` 가 4 스레드가 RMW 로 두들기는 `count_` 와 **같은 128 B 줄**에 있다 → 읽기마다 줄을 다시 가져온다. `alignas(128)` 로 떼어 놓으면 `waiters_` 는 아무도 안 쓰는 읽기 전용 줄이 되어 공유 상태로 각 코어에 남는다 → 2.5~3 배 빨라져 가장 빠른 판이 됐다. uint32 매번-notify 판이 느린 건 모든 스레드가 같은 전역 표 칸에 `ldaddal` 을 하기 때문으로 본다(추정). 이 레슨의 정답 코드는 단순하게 같은 줄에 두었다 — 핑퐁(B2)에선 대기자가 `waiters_` 를 매번 RMW 하므로 떼어 놓으면 오히려 줄 두 개가 오간다 (B2 에서 둘의 차이는 노이즈 안). **레이아웃은 작업 부하로 정한다.**

**B2 — 핑퐁 왕복 (두 스레드, 20000 회, ns)**

| 구현 | run 1 med / p99 / mean | run 2 | run 3 |
|---|---|---|---|
| Semaphore | 167 / 583 / 231 | 250 / 7458 / 382 | 167 / 292 / 256 |
| Semaphore (waiters 다른 줄) | 167 / 459 / 256 | 250 / 6000 / 367 | 208 / 3625 / 333 |
| `std::counting_semaphore` | 125 / 375 / 180 | 208 / 6250 / 328 | 167 / 2958 / 280 |
| `std::binary_semaphore` | 167 / 250 / 190 | 208 / 4416 / 317 | 166 / 5833 / 292 |

상대가 수백 ns 안에 응답하므로 거의 전부 ② 스핀 단계에서 끝난다 (커널 진입 없음). 이때 내 세마포어가 대체로 느리다 — 평균으로 run 1·2 에서 16~28 % (run 3 은 오히려 9 % 빠름: 노이즈 범위) — acquire 가 막힐 때마다 `waiters_` RMW 두 번(+1, −1)이 같은 줄을 한 번 더 왕복시키고, 해제자는 등록을 보고 notify 를 부른다. std 는 대기자를 세지 않는다(대신 매번 notify_all). **대기자 카운트는 "거의 안 막히는" 부하에선 이득, "매번 막히는" 핑퐁에선 비용** 이다.

**B3 — 잠든 대기자 깨우기: 신호 쪽이 100 µs 쉰 뒤 t0 → 대기자 반환 t1 (600 회, µs)**

| 구현 | run 1 med / p99 | run 2 | run 3 |
|---|---|---|---|
| Semaphore | 5.2 / 10.4 | 4.4 / 46.4 | 6.1 / 66.4 |
| `std::counting_semaphore` | 5.3 / 25.8 | 5.9 / 56.1 | 6.1 / 63.6 |
| Mutex (unlock → 대기 lock) | 5.0 / 19.3 | 6.0 / 64.1 | 5.9 / 42.6 |
| `std::mutex` | 6.5 / 62.7 | 6.6 / 65.3 | 6.4 / 65.2 |
| Latch (대기자 4, 대기자별) | 5.8 / 19.6 | 15.3 / 70.3 | 14.7 / 66.7 |
| `std::latch` (대기자 4) | 11.9 / 67.5 | 14.8 / 69.0 | 11.6 / 50.7 |

커널 경로(`__ulock_wake` → 스케줄러 → 대기 스레드가 코어를 받음)에 들어가면 **구현 차이는 사라진다**: 세마포어·뮤텍스는 내 것이든 std 든 중앙값 4~7 µs, 꼬리는 수십 µs. B2 의 0.2 µs 와 30 배 차이 — 그래서 libc++ 가 잠들기 전에 4 µs 를 스핀하는 것이다. 래치는 대기자 4 개를 차례로 깨우므로 뒤쪽 대기자가 늦다. 측정 함정 하나: 처음엔 신호 쪽이 `yield` 로 돌며 "깨어났나" 를 기다렸는데, 그 판은 8 회 중 3 회에서 **여러 구현의 중앙값이 69 µs 근처로** 뭉쳤다 (예: `Semaphore med 68958 p99 72209`). 신호 쪽도 `atomic::wait` 로 잠들게 바꾼 7 회에선 그 모드가 한 번도 안 나왔지만 ~70 µs 꼬리는 남았다. 원인은 확인하지 못했다 (스케줄러가 깨운 스레드를 yield 로 도는 코어에 올린다는 추정). `./dbench spin` 으로 옛 방식을 다시 돌릴 수 있다.

**B4 — 경합 뮤텍스 처리량 (스레드당 20 만 회 lock / counter++ / unlock, Mops/s)**

| T | Mutex (run 1 / 2 / 3) | 넘겨줌 비율 | notify / unlock | `std::mutex` (run 1 / 2 / 3) | 넘겨줌 비율 |
|---|---|---|---|---|---|
| 4 | 21.6 / 18.4 / 16.2 | 28~37 % | 0.35~0.84 | 59.0 / 64.8 / 52.2 | 0.8~1.2 % |
| 8 | 20.1 / 21.7 / 25.7 | 12~18 % | 0.31~0.48 | 42.5 / 41.0 / 39.5 | 1.7~1.9 % |

"넘겨줌" = 직전 주인과 다른 스레드가 잡은 비율 (임계 구역 안에서 plain 변수로 셈). 내 뮤텍스가 2~3 배 느리다. 숫자가 말해 주는 것: `std::mutex` 는 99 % 이상 **방금 푼 스레드가 다시 잡는다** — 락과 카운터의 캐시 줄이 한 코어에 머문다. 내 것은 상태가 2 로 끈적하게 남아 unlock 의 31~84 % 가 notify 를 부르고, 4 µs 스핀 중이던 대기자가 값 변화를 보고 `exchange(2)` 로 뛰어들어 넘겨줌이 30 % 안팎 생긴다 — 넘겨줄 때마다 줄이 코어 사이를 오간다. macOS `pthread_mutex` 내부가 정확히 어떤 정책인지는 libpthread 소스로 확인하지 않았다 (미검증). 교훈은 일반적이다: **처리량 벤치마크는 공정성과 맞바꾼 숫자일 수 있다** (OS-M07 의 TAS vs ticket 공정성 측정과 같은 이야기).

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread solution.cpp -o solution && ./solution     # 34/34 passed (7 회 반복 모두)
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.cpp -o sol_asan && ./sol_asan   # 34/34, 보고 0
c++ -std=c++20 -g -O1 -fsanitize=thread -pthread solution.cpp -o sol_tsan && ./sol_tsan       # 34/34, 보고 0 (1.2 초)
```

실제 출력:

```text
Part A stress: pool max_in_use=3  mutex counter=400000  latch rounds=200
Part C stress: samples=100000 ring=16 workers=3
34/34 passed
```

일반 빌드 0.19 초. TSan 이 조용하다는 것의 의미: 뮤텍스 카운터·래치 칸·파이프라인 설정/링/통계 같은 plain 데이터가 전부 내 프리미티브의 acquire/release 로 happens-before 가 생겼다는 TSan 판단이다 (libc++ dylib 안쪽은 계측되지 않지만, 순서를 만드는 연산은 헤더에서 인라인된 우리 쪽 atomic 이다). 반대로 순서를 relaxed 로 내리면 TSan 이 바로 잡는다 — `demo_tsan_broken.cpp`:

<!-- code: demo_tsan_broken.cpp -->

```text
$ c++ -std=c++20 -g -O1 -fsanitize=thread -pthread demo_tsan_broken.cpp -o dtb && ./dtb
==================
WARNING: ThreadSanitizer: data race (pid=88370)
  Write of size 8 at 0x00016cf06b18 by thread T2:
    #0 void* std::__1::__thread_proxy[abi:nqe210106]<... main::$_0>>(void*) thread.h:168 (dtb:arm64+0x100000d70)

  Previous write of size 8 at 0x00016cf06b18 by thread T1:
    #0 void* std::__1::__thread_proxy[abi:nqe210106]<... main::$_0>>(void*) thread.h:168 (dtb:arm64+0x100000d70)

  Location is stack of main thread.
  …
SUMMARY: ThreadSanitizer: data race thread.h:168 in void* std::__1::__thread_proxy[...]
==================
counter = 40000 (기대 40000)
ThreadSanitizer: reported 1 warnings
```

(exit 134.) 카운터는 40000 으로 맞았다 — 상태 전이는 원자적이니 "동시에 두 명" 은 없었다. 그래도 relaxed lock 은 앞 주인의 `counter++` 를 happens-before 로 넘겨받지 못하므로 C++ 정의상 data race 이고 UB 다 ([intro.races]). 맞은 값은 우연이다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `if (c == 0) a.wait(a.load());` | 검사와 다른 값을 old 로 → 그 사이 바뀌었으면 "새 값인 동안" 잠든다 (demo ①, 200 ms 구출) | `i64 c = a.load(); if (c == 0) a.wait(c);` |
| `wait` 가 돌아오면 조건이 참이라고 가정 | 가짜 깨어남 허용 + 전역 표 칸 충돌 + 다른 스레드가 먼저 가져감 | `while (!try_acquire()) { ...wait... }` |
| 세마포어 `if (old == 0) notify_one();` | 잠든 대기자 2 + release 2 번 → 두 번째가 건너뜀 → 하나 영원히 (demo ② 10/10) | 대기자 카운트 + 매 release notify_one, 또는 libc++ 처럼 notify_all |
| release 가 `waiters` 를 먼저 읽고 count 증가 | 읽은 뒤 등록·잠든 대기자를 놓침 (demo ③ 5/5) | `count.fetch_add` → `waiters.load` |
| waiters / count 를 relaxed 로 | SB: 둘 다 옛 값을 볼 수 있다 (이 맥에선 0/20만 관찰 — 보장 아님) | 네 연산 모두 seq_cst |
| 3 상태 뮤텍스를 `uint32_t` 로 (이 맥) | 전역 표 경로 → `notify_one` 이 전원을 깨움 (demo_notify_width 4/4) | 플랫폼 대기 폭: 이 맥 libc++ `int64_t`, Linux futex 32 비트 |
| 깨어난 뒤 `exchange(1)` 로 잡음 | 다른 대기자가 남았는데 상태 1 → 다음 unlock 이 notify 안 함 → 분실 | `exchange(2)` (보수적 notify 1 번 감수) |
| 뮤텍스 lock/unlock 을 relaxed | 상호 배제는 되지만 보호 데이터에 hb 없음 → data race (TSan 보고) | lock = acquire, unlock = release |
| 래치 `count_down` 마다 notify_all | 마지막 전엔 깰 이유가 없다 — 대기자가 다시 잠들 뿐 | `if (old == n) notify_all();` |
| "대기자 카운트 = 무조건 이득" | 같은 줄이면 경합 처리량이 반토막(B1'), 핑퐁에선 RMW 2 번 비용(B2) | 부하를 재고 레이아웃 결정 |
| `atomic::wait` 에 타임아웃이 있다고 생각 | C++20 엔 없다. `try_acquire_for` 는 폴링 + 백오프 | 연습 1 |

## 8. 면접 Q&A

<details>
<summary>Q1. atomic::wait 를 쓰면 왜 깨움 분실이 없나요? 조건 변수와 비교해서 설명해 보세요.</summary>

조건 변수는 "조건 검사 → 잠들기" 사이를 뮤텍스로 닫는다 (`pthread_cond_wait` 가 뮤텍스를 풀며 원자적으로 잠듦, OS-B06). `wait(old)` 는 그 창을 **값 비교**로 닫는다: 커널(이 맥 `__ulock_wait(UL_COMPARE_AND_WAIT64, addr, old)`, Linux `FUTEX_WAIT`)이 "`*addr == old` 면 잠들기" 를 wake 와 원자적으로 한다. 그래서 검사 직후 다른 스레드가 값을 바꾸고 notify 해도, 내 wait 는 비교에서 떨어져 즉시 돌아온다 (Part B 의 B1/B4/B6 이 이 창을 강제로 만들어 확인). 표준 근거는 [atomics.wait]/4 의 "막힌 뒤의 수정 Y 가 notify 보다 happens-before 면 풀릴 자격이 있다". 꼬리: "그럼 깨움 분실은 영영 없나?" → old 를 잘못 넘기거나(다시 읽은 값), notify 를 잘못 생략하면 직접 만들 수 있다 (demo ①②③).

</details>

<details>
<summary>Q2. 대기자 수를 세서 notify 를 건너뛰는 게 왜 위험하고, 어떻게 안전하게 하나요?</summary>

대기자: `waiters++` → count 읽기(wait 안). 해제자: `count++` → waiters 읽기. 서로 다른 변수에 쓰고 상대 것을 읽는 store buffering 모양이라, acquire/release 로는 "해제자 waiters==0, 대기자 count==0" 이 둘 다 허용된다 → 해제자는 건너뛰고 대기자는 잠든다. 넷 다 seq_cst 면 전체 순서 S 에서 어느 한쪽이 상대 쓰기를 반드시 본다 (5 절 표). 순서도 지켜야 한다: 해제자가 waiters 를 먼저 읽으면 seq_cst 여도 깨진다 (demo ③ 5/5). 꼬리: "실제로 relaxed 가 깨지는 걸 봤나?" → 이 맥에선 20 만 라운드 0 회. libc++ 의 4 µs 스핀과 `dmb ish` + 커널 비교가 창을 가리기 때문. 관찰 0 은 보장이 아니다.

</details>

<details>
<summary>Q3. libc++ 는 macOS 에서 wait/notify 를 어떻게 구현하나요? notify_one 은 정말 하나만 깨우나요?</summary>

역어셈과 lldb 로 본 것: 값 64 번 폴링 → 시계를 읽으며 4 µs 까지 스핀 → 대기자 수 `ldadd` + `dmb ish` → `__ulock_wait(5, addr, old, 0)`. 깨우기는 대기자 수를 `ldar` 로 보고 0 이면 시스템 호출 없이 반환, 아니면 `__ulock_wake`. 대기 변수가 정확히 `int64_t`(=`__cxx_contention_t`) 면 그 주소에서 자고 notify_one 은 연산 5 (하나만). 그 밖의 폭(uint32, long/ptrdiff_t)은 주소 해시로 전역 표 256 칸 × 64 B 의 카운터에서 자고, notify_one 도 `0x105` = `ULF_WAKE_ALL` → **전부** 깨운다 (demo_notify_width: 4/4 vs 1/4). 표준은 "적어도 하나" 라 둘 다 합법. `os_sync_wait_on_address` 는 이 libc++ 에서 불리지 않았다. 꼬리: "std::counting_semaphore 는?" → 카운터가 `ptrdiff_t` 라 전역 표 경로이고 release 는 0→양수일 때 notify_all.

</details>

<details>
<summary>Q4. 직접 만든 세마포어가 std::counting_semaphore 보다 빠르던데, 왜죠? 항상 그런가요?</summary>

단일 스레드 release+acquire 3.4 ns vs 16.7 ns. notify 가 이유가 아니다 — std 쪽을 `try_acquire` 로 바꾸면 3.4~3.8 ns 로 같아진다. libc++ `acquire` 는 허가가 있어도 `__libcpp_thread_poll_with_backoff` 에 들어가고 그 함수가 첫 폴링 전에 `steady_clock::now()` 를 부른다 (역어셈 확인). 내 것은 `try_acquire` 성공이면 대기 함수에 안 들어간다. 항상 그렇진 않다: 핑퐁(매번 막힘)에선 내 것이 평균 16~28 % 느린 실행이 많았다 (3 회 중 2 회) (waiters RMW 두 번이 줄을 한 번 더 왕복). 커널까지 잠드는 경우(B3)엔 둘 다 중앙값 4~7 µs 로 같다 — 시스템 호출과 스케줄러가 지배한다. 꼬리: "그럼 std 를 쓰지 말라는 건가?" → 아니다. 측정된 핫패스가 아니면 std 를 쓴다. 바꾼다면 "빠른 경로에 시계 읽기 0" 같은 구체적 숫자가 이유여야 한다.

</details>

<details>
<summary>Q5. 3 상태 뮤텍스에서 상태 1 과 2 를 왜 나누나요? 그리고 당신 뮤텍스가 std::mutex 보다 경합 시 2~3 배 느린 이유는?</summary>

1/2 를 안 나누면 unlock 이 "기다리는 사람이 있나" 를 모르니 매번 notify 해야 한다. 나누면 경합이 없을 때 lock = CAS 한 번, unlock = exchange 한 번, wait/notify 0 회 (B5). 깨어난 쪽은 남은 대기자를 모르니 2 로 잡고, 그래서 마지막에 notify 가 한 번 더 나간다 (B6 `notify_one == 2`). 자세한 유도와 Drepper 의 mutex2→mutex3 개선은 OS-A01. 경합 처리량은 측정으로 답한다: std::mutex 는 99 % 이상 방금 푼 스레드가 다시 잡아(넘겨줌 0.8~1.9 %) 캐시 줄이 한 코어에 머문다. 내 것은 상태 2 가 끈적해 unlock 의 31~84 % 가 notify 를 부르고, 스핀 중 대기자가 뛰어들어 넘겨줌이 12~37 % — 줄이 코어 사이를 오간다. 꼬리: "그럼 std::mutex 가 더 좋은가?" → 처리량은 그렇지만 그건 불공정과 맞바꾼 숫자다. 실시간 시스템은 처리량보다 최악 대기 시간·우선순위 상속(RTOS-M04, PI-futex)을 본다.

</details>

<details>
<summary>Q6. 래치의 메모리 순서를 설명해 보세요. count_down 이 relaxed 면 안 되나요?</summary>

count_down = `fetch_sub(release)`, wait = `load(acquire)`. 여러 스레드의 fetch_sub 는 모두 RMW 라, 각 RMW 가 머리인 release sequence 에 뒤의 RMW 들이 이어진다 ([intro.races] release sequence, MCPP-M13). 그래서 0 을 읽은 acquire load 하나가 **모든** count_down 과 synchronizes-with → 각 스레드가 count_down 전에 쓴 plain 데이터가 보인다 (Part A 래치 200 라운드, TSan 조용). relaxed count_down 이면 이 sw 가 없어 data race. 마지막 스레드가 `arrive_and_wait` 로 자기 값(0)을 읽는 경우도 같다 — 그 값은 앞 RMW 들의 release sequence 안에 있다. 꼬리: "acq_rel 이 필요한가?" → count_down 쪽이 다른 스레드 데이터를 읽을 일이 없으면 release 로 충분. std::latch 구현도 release/acquire 다 (헤더 확인).

</details>

<details>
<summary>Q7. atomic::wait 에 타임아웃이 없는데 try_acquire_for 는 어떻게 구현되나요?</summary>

C++20 의 `atomic::wait` 엔 타임아웃 판이 없다. libc++ `__atomic_semaphore_base::try_acquire_for` 는 `__libcpp_thread_poll_with_backoff(try_acquire, __libcpp_timed_backoff_policy(), rel_time)` — 즉 폴링 + 점점 길게 자는 백오프다 (헤더 확인). 내 연습 1 도 같은 방식: 64 번 즉시, 64 번 yield, 그 뒤 50 µs → 1 ms 까지 두 배씩, 남은 시간 한도 안에서. 대가: 깨어나는 반응이 백오프 간격만큼 늦고, 오래 기다리면 주기적으로 깨어나 전력을 쓴다. 타임아웃 있는 진짜 잠을 원하면 플랫폼 API (`__ulock_wait` 의 timeout 인자, `os_sync_wait_on_address_with_timeout`, futex 의 timespec) 를 직접 써야 한다. 타임아웃 판 atomic wait 를 표준에 넣으려는 제안이 있다는 것만 알고 있다 (상태 미확인).

</details>

<details>
<summary>Q8. 이걸 Cortex-M 펌웨어로 가져가면 atomic::wait 는 무엇이 되나요?</summary>

`wait` 는 "스케줄러에게 나를 재워 달라" 이므로 OS 가 있어야 의미가 있다. 스레드 없는 libc++ 빌드에선 wait = 끝없는 폴링, notify = 빈 함수다 (헤더의 `!_LIBCPP_HAS_THREADS` 판 확인) — 베어메탈에선 그냥 스핀이고, 단일 코어에서 ISR 이 값을 바꿔 주지 않으면 영원히 돈다. 전원 측면에선 스핀 대신 `WFE`/`WFI` 로 자야 한다. RTOS 가 있으면 같은 설계를 RTOS 원시 기능으로 옮긴다: 세마포어 = `xSemaphoreTake/Give(FromISR)`, 단일 대기자 Event = 태스크 알림 `ulTaskNotifyTake/vTaskNotifyGiveFromISR` (RTOS-M01, 연습 4), 래치·EventGroup = `xEventGroupWaitBits` (RTOS-B13, 연습 3). C++20 freestanding 에서 wait/notify 가 어떤 지위인지는 표준 판마다 달라 확인하지 못했다 — 실무에선 툴체인 문서(arm-none-eabi libstdc++/libc++ 설정)를 본다. 꼬리: "FreeRTOS take 에 빠른 경로가 있나?" → 없다, 허가가 있어도 매번 커널 임계 구역. MCU 에선 사용자/커널 경계 비용이 없어서 괜찮다.

</details>

## 9. 추가 연습

### 연습 1 — `try_acquire_for`: 타임아웃은 폴링으로

`sem_try_acquire_for(Semaphore&, nanoseconds)` 를 만들어라. 64 번 즉시 재시도 → 64 번 yield → 남은 시간 안에서 50 µs 부터 두 배씩 최대 1 ms 잠. 시간 전에 포기하지 않고, 다른 스레드가 5 ms 뒤 release 하면 성공해야 한다.

<details>
<summary>답 보기</summary>

`practice.cpp` 의 `sem_try_acquire_for`. 검사: 허가 0 에서 20 ms → false 이고 경과 ≥ 20 ms, 허가 1 에서 0 ns → true, 5 ms 뒤 release 하는 스레드가 있으면 5 s 한도로 true. libc++ `try_acquire_for` 와 같은 구조 (폴링 + timed backoff). 반응 지연 = 마지막 잠의 길이(최대 1 ms) — 그래서 이 방식은 "가끔 쓰는 타임아웃" 용이다.

</details>

### 연습 2 — 재사용 Barrier: 세대 번호를 도착 전에 찍어라

`Barrier(n).arrive_and_wait()` — n 번째 도착자가 세대를 넘기고 notify_all, 나머지는 세대가 바뀔 때까지 잔다. 여러 번 재사용. 강제 스케줄: 내가 잠들기 직전 상대가 도착하는 경우에도 돌아와야 한다.

<details>
<summary>답 보기</summary>

```cpp
i64 g = gen_.load(acquire);                          // ① 도착 "전에" 찍는다
if (arrived_.fetch_add(1, acq_rel) + 1 == n_) {
    arrived_.store(0, relaxed); gen_.fetch_add(1, release); plat_notify_all(gen_); return;
}
for (i64 cur = gen_.load(acquire); cur == g; cur = gen_.load(acquire)) plat_wait(gen_, g, acquire);
```

도착 뒤에 g 를 읽으면 마지막 도착자가 이미 세대를 넘긴 뒤의 값을 찍어 "다음 세대가 될 때까지" 잔다 = 깨움 분실 (데모 ① 과 같은 실수, eventcount 패턴의 핵심). `arrived_` 를 0 으로 되돌리는 relaxed store 는 뒤의 `gen_` release 가 게시하므로 다음 세대 도착자는 0 을 본다. 검사: 훅 테스트(세대 1, waits 1, notify_all 1), 6 스레드 × 300 단계 × 배리어 두 번 — 다른 칸 오류 0, 세대 600.

</details>

### 연습 3 — EventGroup: wait-all / wait-any + clear-on-exit

`set(bits)` 와 `wait(mask, all, clear)` 를 만들어라 (FreeRTOS `xEventGroupSetBits` / `xEventGroupWaitBits`). 대기자가 없으면 notify 0. clear 면 조건을 만족한 비트를 원자적으로 지우고 지우기 **전** 값을 돌려준다.

<details>
<summary>답 보기</summary>

조건 검사에 쓴 `v` 로 `wait(bits_, v)`. 조건이 대기자마다 달라서 `set` 은 notify_all (하나만 깨우면 조건이 안 맞는 대기자가 깰 수 있다). clear 는 `compare_exchange_weak(v, v & ~mask, acq_rel, acquire)` — 실패하면 v 가 갱신되니 조건부터 다시. 대기자 카운트는 세마포어와 같은 SB 규칙 (seq_cst 넷). 검사: 훅으로 wait-all(0b11) 중 bit1 이 켜짐 → 0b11 반환, 비트 0, waits 1 · notify_all 1. 200 라운드 × 세터 3 스레드: 데이터 게시 + 지우기 오류 0. FreeRTOS 이벤트 그룹은 `configUSE_16_BIT_TICKS` 에 따라 쓸 수 있는 비트가 8 또는 24 개다 (FreeRTOS 문서 기준).

</details>

### 연습 4 — Event: 단일 대기자 자동 리셋 (태스크 알림 모양)

상태 0(빈) / 1(게시) / 2(대기 중) 하나로 `post()` 와 `wait()` 를 만들어라. 게시가 여러 번이어도 1 (이진), 이미 게시돼 있으면 잠들지 않고, post 는 상태가 2 일 때만 notify_one. 대기자는 하나뿐이라고 가정.

<details>
<summary>답 보기</summary>

`post`: `if (s_.exchange(1, release) == 2) notify_one`. `wait`: 1 이면 CAS 1→0 으로 소비, 0 이면 CAS 0→2 로 "잔다" 표시 후 `wait(s_, 2)`, 돌아오면 다시 읽기. 3 상태 뮤텍스와 같은 아이디어를 신호용으로 쓴 것 — 대기자 카운트가 상태 안에 들어 있다. FreeRTOS `vTaskNotifyGiveFromISR` / `ulTaskNotifyTake(pdTRUE, ...)` 가 이진 세마포어보다 가벼운 이유(별도 객체·대기 리스트 없음, 대기자 = 그 태스크 하나)와 같은 구조다 (RTOS-M01). 검사: 3 번 게시 → 1, notify 0 / 이미 게시 → waits 0 / 훅(대기 표시 뒤 post) → notify_one 1 / 2 스레드 핑퐁 2 만 회, plain 카운터 순서 오류 0.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread practice.cpp -o practice && ./practice        # 18/18 passed
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.cpp -o pr_asan && ./pr_asan   # 18/18, 보고 0
c++ -std=c++20 -g -O1 -fsanitize=thread -pthread practice.cpp -o pr_tsan && ./pr_tsan          # 18/18, 보고 0
```

## 10. 핵심 정리

- `wait(old)` = "값이 old 인 동안만 잔다", 비교는 notify 와 원자적(커널 compare-and-wait), 가짜 깨어남 허용 → **검사에 쓴 그 값을 old 로, 항상 루프**.
- 깨움 분실은 여전히 만들 수 있다: 다시 읽은 old, "0→1 일 때만 notify_one", 대기자 수를 값 변경 **전에** 읽기. 대기자 카운트로 notify 를 생략하려면 SB 모양 → seq_cst 넷.
- 세마포어 = CAS 감소 + 0 에서 wait. 래치 = release fetch_sub + 0 이 될 때 notify_all 한 번 + acquire load. 뮤텍스 = 0/1/2, 빠른 경로 wait/notify 0 회, 깨어난 쪽은 2 (자세한 건 OS-A01).
- 이 맥 libc++: 64 번 폴링 → 4 µs 스핀 → `__ulock_wait(5)`. `int64_t` 는 변수 자체에서, 그 밖의 폭은 전역 표(256 × 64 B)에서 자고 notify_one 이 전부 깨운다. notify 는 대기자 0 이면 시스템 호출 없음.
- 실측: notify 생략 이득 0.2~0.9 ns (작다), std 세마포어 acquire 의 13 ns 는 시계 읽기, waiters 를 같은 줄에 두면 경합 처리량 절반, 커널 깨우기는 구현 무관 중앙값 4~7 µs, std::mutex 는 넘겨줌 1 % 로 2~3 배 빠름(공정성과 맞바꿈).
- MCU: wait 는 OS 가 있어야 의미, 스레드 없는 libc++ 는 스핀 + 빈 notify. RTOS 에선 세마포어 / 태스크 알림 / 이벤트 그룹으로 같은 설계를 옮긴다.

## 참고자료

- C++20 working draft N4861 — [atomics.types.operations] (`wait`, `notify_one`, `notify_all` 의 단계), [atomics.wait] (¶4 "eligible to be unblocked"): <https://timsong-cpp.github.io/cppwp/n4861/atomics.wait> · [intro.races] (release sequence, data race): <https://timsong-cpp.github.io/cppwp/n4861/intro.races>
- P1135R6 *The C++20 Synchronization Library* (atomic wait/notify, counting_semaphore, latch, barrier): <https://wg21.link/p1135r6>
- cppreference — `std::atomic<T>::wait`, `std::counting_semaphore`, `std::latch`, `std::barrier`: <https://en.cppreference.com/w/cpp/atomic/atomic/wait>, <https://en.cppreference.com/w/cpp/thread/counting_semaphore>, <https://en.cppreference.com/w/cpp/thread/latch>
- libc++ 소스 (이 맥 SDK 헤더로 확인한 부분): `MacOSX26.4.sdk/usr/include/c++/v1/__atomic/atomic_sync.h`, `__thread/poll_with_backoff.h`, `__atomic/contention_t.h`, `semaphore`, `latch` · dylib 쪽 구현 `libcxx/src/atomic.cpp` (LLVM 저장소, `__ulock_wait`/`UL_COMPARE_AND_WAIT64`/`ULF_WAKE_ALL`, 대기자 수 카운터): <https://github.com/llvm/llvm-project/blob/main/libcxx/src/atomic.cpp>
- xnu `bsd/sys/ulock.h` (연산·플래그 번호): <https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/ulock.h> · 공개 API `os/os_sync_wait_on_address.h` (macOS 14.4+, SDK 헤더)
- Ulrich Drepper, *Futexes Are Tricky* (3 상태 뮤텍스의 유래 — 이 레슨은 OS-A01 에 미룬다): <https://www.akkadia.org/drepper/futex.pdf>
- Dmitry Vyukov, eventcount (세대 번호를 검사 전에 찍는 패턴) — 1024cores.net 동기화 글 모음
- FreeRTOS 문서 — 세마포어, 태스크 알림("RTOS Task Notifications"), 이벤트 그룹: <https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/03-Direct-to-task-notifications/01-Task-notifications>
- Anthony Williams, *C++ Concurrency in Action*, 2nd ed. (C++20 동기화 라이브러리 부록)
- 관련 레슨: C-A03 (stdatomic, SB/Peterson), MCPP-M13 (release sequence, TSan 한계), ARM-M04 (LDXR/STXR vs LSE — 위 역어셈의 `ldadd`/`ldaddal`/`ldar`), LLAT-B06 (원자 연산 비용, 같은 줄 경합), OS-B05 (뮤텍스 비용), OS-B06 (조건 변수, 가짜 깨어남), OS-M07 (스핀락·공정성), OS-A01 (futex 뮤텍스 Drepper), LLAT-M05 (코어 간 핸드오프 지연), RTOS-B08/B09/B10 (이진·카운팅 세마포어, 뮤텍스), RTOS-M01 (태스크 알림), RTOS-M04 (우선순위 상속), RTOS-B13 (이벤트 그룹)
