---
id: TOOL-M09
time: 90분
updated: 2026-11-08
---
# TOOL-M09 ThreadSanitizer로 데이터 레이스 찾기

## 학습 목표

- 버그 3종(공유 카운터 레이스 · 락 없이 읽는 플래그 · 락 순서 역전)을 pthread 로 직접 만들고, `-fsanitize=thread` 리포트를 **한 줄씩** 읽어 "어느 변수 · 어느 두 스레드 · 어느 코드 줄 · 어떤 락" 인지 30초 안에 말할 수 있다.
- 리포트의 `Write/Read … by thread Tn`, `Previous write`, `(mutexes: write M0)`, `Location is global / heap block`, `Mutex M0 created at`, `Thread Tn created by`, `As if synchronized via sleep`, `Cycle in lock order graph` 가 각각 무슨 뜻인지 설명한다.
- 고치는 도구를 버그 성격에 맞게 고른다: 여러 값이 같이 움직이면 **뮤텍스**, 단독 카운터는 **atomic RMW**(`fetch_add`, 읽고-비우기는 `exchange`), 플래그 게시는 **release/acquire**, 두 락은 **고정된 순서(rank)**.
- `TSAN_OPTIONS`(halt_on_error, abort_on_error/exitcode, second_deadlock_stack, detect_deadlocks, history_size, suppressions, log_path), 비용(이 맥에서 실측 약 10배 CPU · 4배 메모리), **TSan 이 못 보는 것**(실행 안 된 경로, 계측 안 된 코드, 같은 8바이트 칸의 두 번째 레이스, Apple 런타임의 교착 검출)을 실측으로 안다.

## 1. 문제

"TSan 리포트 하나 보여 줄 테니 무슨 버그인지 읽어 보세요" — 펌웨어/시스템 면접에서 동시성 기본기(OS-B04 ~ OS-B06)를 확인한 다음 단계로 자주 나오는 질문이다. 펌웨어는 타깃(Cortex-M)에서 TSan 을 돌릴 수 없지만, **드라이버·프로토콜·큐 로직을 호스트에서 pthread 로 돌리는 단위 테스트**에는 그대로 쓴다. 스레드 하나를 ISR 이나 FreeRTOS 태스크 하나로 보면 된다 — ISR ↔ main 공유 변수(C-M14), 태스크 간 뮤텍스(RTOS-B10), 코어 간 atomic(C-A03)에서 생기는 바로 그 버그들이다.

> TSan 이 말하는 "data race" 는 C11 의 정의 그대로다 — 서로 다른 스레드의 **충돌하는 두 접근**(같은 메모리 위치, 최소 하나는 쓰기) 중 **하나라도 원자 연산이 아니고**, 어느 쪽도 다른 쪽보다 **먼저 일어나지(happens before) 않으면** 데이터 레이스이고 동작은 정의되지 않는다. — C11 5.1.2.4p4, p25 요지

그래서 TSan 은 "값이 틀렸다" 를 보는 게 아니라 **happens-before 가 없는 충돌 접근**을 본다. 결과가 우연히 맞아도 잡고(Part A 의 HALF 실험), 결과가 틀려도 모든 접근이 atomic 이면 못 잡는다(C-A03 의 load+store lost update).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">TSan = 컴파일 시 계측 + 실행 시 shadow 메모리 + happens-before(벡터 시계)</text>
  <rect x="10" y="40" width="220" height="110" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor" font-weight="bold">① 컴파일 (-fsanitize=thread)</text>
  <text x="20" y="84" fill="currentColor" font-family="monospace" font-size="12">s-&gt;samples++;</text>
  <text x="20" y="104" fill="currentColor" font-family="monospace" font-size="12">→ ldr ; add</text>
  <text x="20" y="122" fill="currentColor" font-family="monospace" font-size="12">  bl ___tsan_write8</text>
  <text x="20" y="140" fill="currentColor" font-family="monospace" font-size="12">  str</text>
  <rect x="260" y="40" width="220" height="110" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="270" y="62" fill="currentColor" font-weight="bold">② shadow 메모리</text>
  <text x="270" y="84" fill="currentColor" font-size="12">앱 메모리 8 바이트 칸마다</text>
  <text x="270" y="102" fill="currentColor" font-size="12">최근 접근 몇 개를 기록:</text>
  <text x="270" y="120" fill="currentColor" font-size="12">(스레드, 시각 epoch, 크기,</text>
  <text x="270" y="138" fill="currentColor" font-size="12"> 읽기/쓰기, atomic 여부)</text>
  <rect x="510" y="40" width="220" height="110" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="520" y="62" fill="currentColor" font-weight="bold">③ 동기화 이벤트</text>
  <text x="520" y="84" fill="currentColor" font-size="12">lock/unlock, create/join,</text>
  <text x="520" y="102" fill="currentColor" font-size="12">cond wait/signal,</text>
  <text x="520" y="120" fill="currentColor" font-size="12">atomic release → acquire</text>
  <text x="520" y="138" fill="currentColor" font-size="12">= 시계를 넘겨주는 간선</text>
  <path d="M230 95 L258 95" stroke="currentColor" stroke-width="2"/>
  <path d="M480 95 L508 95" stroke="currentColor" stroke-width="2"/>
  <rect x="10" y="175" width="720" height="80" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="198" fill="currentColor" font-weight="bold">④ 접근할 때마다 검사</text>
  <text x="20" y="220" fill="currentColor" font-size="12">같은 칸의 이전 접근이 "다른 스레드 · 둘 중 하나는 쓰기 · 둘 다 atomic 은 아님" 이고,</text>
  <text x="20" y="240" fill="currentColor" font-size="12">내 시계가 그 접근 시각을 아직 모르면(happens-before 없음) → WARNING: data race</text>
  <text x="10" y="282" fill="currentColor">실행된 접근만 기록한다 → 안 돈 경로, 계측 안 된 코드는 못 본다 (5. 풀이 마지막 절).</text>
  <text x="10" y="304" fill="currentColor">타이밍과 무관: 두 접근이 1초 떨어져 있어도 동기화가 없으면 잡는다 (실제로 겹칠 필요 없음).</text>
</svg>
```

| 버그 | 데모 (리포트 관찰) | 고친 버전 (체크) | 펌웨어에서 같은 모양 |
|---|---|---|---|
| 1. 공유 카운터 레이스 | `demo_counter.c` | Part A `stats_record` / `hits_inc` / `hits_take` | ISR 과 main 이 같이 `rx_count++`, 태스크 둘이 같은 통계 |
| 2. 락 없이 읽는 플래그 | `demo_flag.c`, `demo_flag_m4.c` | Part B `mb_post` / `mb_wait` | `while (!data_ready);` 로 ISR 기다리기 |
| 3. 락 순서 역전 | `demo_lock_order.c` (Linux 에서 보고) | Part B `lock_pair`, Part C `hub_reconfigure` | 태스크 A: SPI→I2C 버스 락, 태스크 B: I2C→SPI |

### Part A — 공유 카운터: 리포트 한 장을 줄 단위로 읽기

- `demo_counter.c` 를 TSan 으로 돌려 리포트의 모든 줄에 이름을 붙인다. `-DHEAP`(힙 위치), `-DHALF`(한쪽만 락을 잡은 "반쯤 고친" 코드 → `mutexes:` 와 `Mutex M0 created at`)도 읽는다.
- `stats_record / stats_snapshot` — `samples` 와 `bytes` 두 필드를 뮤텍스로 묶는다. 불변식 `bytes == samples * 64` 는 읽는 도중에도 깨지면 안 된다.
- `hits_inc` — relaxed `fetch_add`. `hits_take` — "읽고 0 으로" 비우는 동안 들어온 증가를 하나도 잃지 않게. 체크: 4 스레드 × 20만 + 읽는 스레드 1개 → 값 정확, 스냅샷 찢어짐 0, take 합계 == 800000.

### Part B — 플래그 메일박스와 락 순서

- `demo_flag.c`: 평범한 `bool` 로 busy-wait. TSan 리포트, `-O2` 에서 실제로 무슨 일이 나는지(asm), `volatile` 로 "고친 척" 한 버전이 여전히 레이스인 이유.
- `mb_post / mb_wait(timeout_ms)` — 보정값(calib) 을 쓰고 `atomic_bool` 을 release 로 세움 / acquire 로 기다림. 기다림은 **반드시 타임아웃** (펌웨어 워치독 감각).
- `demo_lock_order.c`: 두 스레드를 **차례로** 돌려 실제 교착이 없는데도 TSan 이 `lock-order-inversion (potential deadlock)` 을 내는 것을 본다 (이 맥의 TSan 은 이 검출을 안 한다 → Linux VM 에서 실측).
- `lock_pair / unlock_pair` — rank 가 작은 락부터. `a == b` 면 한 번만. 반대 방향 이동 10만 번씩 동시에 → 교착 없음, 합계 보존, rank 위반 0.

### Part C — 텔레메트리 허브: 레거시 코드의 세 버그를 한 번에

센서 샘플러 태스크 3개가 채널별 통계를 올리고, 현재 보정 설정으로 값을 계산해 로그 링에 쓴다. main 은 그 사이에 설정을 150번 바꾸고, heartbeat 태스크는 정지 플래그만 보고 돈다. 레거시 코드는 이랬다:

```c
static unsigned long per_ch[NCH];            /* 버그 1: 평범한 카운터를 여러 태스크가 ++ */
static bool stop;                            /* 버그 2: 평범한 bool 정지 플래그 */
void hub_sample(...)      { per_ch[ch]++; lock(cfg); v = raw*gain+off; lock(log); append; unlock(log); unlock(cfg); }
void hub_reconfigure(...) { lock(log); lock(cfg); version++; ...; append(CFG); unlock(cfg); unlock(log); }  /* 버그 3: log → cfg */
```

- `hub_sample`, `hub_reconfigure`, `hub_request_stop`, `hub_should_stop` 을 고친다. 설정 락(rank 1) → 로그 락(rank 2) 순서를 지킨다.
- 체크: 채널 합계 == 60000, 로그 길이 == 60000 + 150, 로그를 처음부터 다시 계산했을 때 **버전 역행 0 · 값 불일치 0**, rank 위반 0, heartbeat 는 정지 플래그를 보고 끝남.

## 2. Problem (English)

> **Find and fix data races with ThreadSanitizer.**
>
> You are given three buggy pthread programs: (1) two threads increment a shared `unsigned long` without a lock, (2) a consumer busy-waits on a plain `bool ready` that a producer sets after writing a payload, (3) `move(a, b)` locks `a` then `b` while another path calls `move(b, a)`. Build each with `-g -O1 -fsanitize=thread`, explain every line of the report, then implement the fixed APIs:
>
> - `void stats_record(stats_t *)`, `void stats_snapshot(stats_t *, uint64_t *samples, uint64_t *bytes)` — the pair must stay consistent (`bytes == 64 * samples`) for concurrent readers.
> - `void hits_inc(atomic_ullong *)`, `uint64_t hits_take(atomic_ullong *)` — take returns the accumulated count and resets it to 0 without losing concurrent increments.
> - `void mb_post(mailbox_t *, const calib_t *)`, `bool mb_wait(mailbox_t *, calib_t *out, unsigned timeout_ms)`.
> - `void lock_pair(rlock_t *a, rlock_t *b)`, `void unlock_pair(rlock_t *a, rlock_t *b)` — deadlock-free for any argument order; `a == b` must not self-deadlock.
> - A telemetry hub (`hub_sample`, `hub_reconfigure`, `hub_request_stop`, `hub_should_stop`) whose log never goes back in config version.
>
> The fixed program must report **zero** TSan warnings and pass ASan/UBSan. Follow-ups: definition of a data race; why TSan warns about a deadlock that never happened; TSan cost and blind spots; finding the same bugs on a Cortex-M target.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력):

```text
FAIL starter.c:264  sn == 3u && by == 3u * SAMPLE_BYTES
FAIL starter.c:267  atomic_load(&h1) == 2u
FAIL starter.c:268  hits_take(&h1) == 2u && atomic_load(&h1) == 0u
FAIL starter.c:280  s == (uint64_t)NT * A_ITERS && b == s * SAMPLE_BYTES
FAIL starter.c:282  ro.taken == (uint64_t)NT * A_ITERS
  [A] reader snapshots during run: 11
FAIL starter.c:293  mb_wait(&mb0, &got, 20u) && got.gain == 7 && got.offset == -3 && got.crc == calib_crc(&got)
FAIL starter.c:307  ok == B_ROUNDS
FAIL starter.c:314  rl_held(&lo) && rl_held(&hi)
FAIL starter.c:319  rl_held(&lo) && atomic_load(&g_relock) == 0u
SKIP 2 stress checks (Part B lock order): unit checks failed
FAIL starter.c:337  g_hub.log_n == 1u && g_hub.log[0].version == 0u && g_hub.log[0].kind_ch == 2u && g_hub.log[0].value == 10
FAIL starter.c:339  g_hub.cfg.version == 1u && g_hub.log_n == 2u && g_hub.log[1].kind_ch == (uint32_t)REC_CFG << 8
FAIL starter.c:341  g_hub.log_n == 3u && g_hub.log[2].version == 1u && g_hub.log[2].value == 7
FAIL starter.c:342  atomic_load(&g_hub.per_ch[1]) == 1u && atomic_load(&g_hub.per_ch[2]) == 1u
FAIL starter.c:346  hub_should_stop(&g_hub)
SKIP 5 stress checks (Part C): unit checks failed
9/30 passed
```

하네스 설계:

- **rank 검사기 `rlock_t`** (TODO 아님): `rl_lock` 은 이미 쥔 락 중 rank 가 같거나 높은 것이 있으면 `g_order_violations` 를 올린다 — 펌웨어의 lock level 규칙, 아주 작은 lockdep (그래프 기반은 OS-M11). 이미 쥔 락을 다시 잠그려 하면 잠그지 않고 `g_relock` 만 센다 → 틀린 코드가 **멈추지 않고 FAIL**. 첫 체크가 검사기 자체를 시험한다(일부러 2 → 1).
- 교착 위험이 있는 스트레스(Part B 락, Part C)는 그 Part 의 단위 테스트가 다 통과해야 돈다. 모든 대기(`mb_wait`, heartbeat)에 타임아웃. 체크는 매 실행 항상 성립하는 것만(합계, 불변식, 로그 재계산), 타이밍 값은 출력만.

관찰용 데모 (하네스와 별개, 전부 이 디렉터리에 있음):

```bash
cc -std=c11 -g -O1 -fsanitize=thread -pthread [-DHEAP|-DHALF|-DFIX] demo_counter.c -o dc_t && ./dc_t
cc -std=c11 -g -O1 -fsanitize=thread -pthread [-DVOL [-DNEAR]|-DFIX] demo_flag.c -o df_t && ./df_t
cc -std=c11 -O2 -pthread demo_flag.c -o df && ./df                     # TSan 없이 -O2
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_flag_m4.c -o -
cc -std=c11 -O2 -pthread -DCONCURRENT [-DFIX] demo_lock_order.c -o dl_c && ./dl_c   # 실제 교착 (2초 타임아웃)
# Linux (Lima VM elinux): clang -std=c11 -g -O1 -fsanitize=thread -pthread [-DFIX] demo_lock_order.c && ./a.out
cc -std=c11 -g -O1 -fsanitize=thread -pthread [-DONE_LOOP] demo_options.c -o do_t && TSAN_OPTIONS=... ./do_t
cc -std=c11 -g -O1 -fsanitize=thread -pthread [-DNOINST] demo_limits.c -o dlim_t && ./dlim_t [fault]
cc -std=c11 -O1 -g [-fsanitize=thread] -pthread demo_cost.c -o dcost && /usr/bin/time -l ./dcost
```

## 4. 힌트

<details>
<summary>힌트 1 — 리포트는 "두 접근 + 그 주소 + 두 스레드의 출생지" 다섯 덩어리</summary>

맨 위 `Write/Read of size N at ADDR by thread Tx` 가 **방금** 일어난 접근(그 순간 TSan 이 검사하던 쪽), `Previous …` 가 shadow 에 남아 있던 **이전** 접근이다. 각각 아래 스택의 `#0` 이 실제 메모리 접근 줄. 그 다음 `Location is …` 가 무엇의 주소인지, `Thread Tx … created by … at` 이 그 스레드를 만든 곳. 두 `#0` 줄을 나란히 놓고 "둘 사이에 무슨 동기화가 있어야 했나" 를 물으면 수정 방법이 나온다.

</details>

<details>
<summary>힌트 2 — 두 필드는 락, 한 필드는 atomic, "읽고 비우기" 는 RMW 하나</summary>

`samples` 와 `bytes` 를 각각 atomic 으로 만들면 증가는 안 잃지만, 읽는 쪽이 "samples 는 새 값, bytes 는 옛 값" 을 볼 수 있다 → 불변식이 깨진다. 같이 움직이는 값은 한 락 아래. 단독 카운터의 `load` 후 `store(0)` 은 둘 다 atomic 이어도 그 사이 증가를 잃는다(TSan 은 이것을 **못** 잡는다). `atomic_exchange(c, 0)` 하나로.

</details>

<details>
<summary>힌트 3 — 플래그: 데이터 먼저, 플래그 나중, 기다림엔 끝이 있어야</summary>

`mb_post`: 데이터를 다 쓴 **뒤** `atomic_store_explicit(…, release)`. `mb_wait`: `atomic_load_explicit(…, acquire)` 가 true 를 본 **뒤** 데이터를 읽는다. 루프마다 `now_ms()` 를 보고 데드라인이 지나면 false, 안에서는 `sched_yield()` (MCU 라면 `__WFE()` 나 RTOS 대기). C-A03 Part B 의 1칸 채널과 같은 원리다.

</details>

<details>
<summary>힌트 4 — 락 두 개: 전역 순서 하나를 정하고 모두가 따른다</summary>

`lock_pair(a, b)` 는 인자 순서를 믿지 말고 rank 비교로 작은 쪽을 먼저 잠근다. `a == b` 인 호출(같은 풀끼리 이동)은 기본 뮤텍스를 두 번 잠그는 것이 되므로(POSIX: PTHREAD_MUTEX_DEFAULT 재잠금은 UB) 한 번만. Part C 의 `hub_reconfigure` 는 `lock_pair(&cfg_lk, &log_lk)` 한 줄로 해결된다 — 레거시는 로그를 먼저 잡았다.

</details>

## 5. 풀이

### 0. 계측된 코드는 어떻게 생겼나

`demo_counter.c` 의 `stats_bump` 를 `-O1 -fsanitize=thread -S` 로 본 실제 arm64 출력 (지시어 생략):

```text
_stats_bump:
        bl   ___tsan_func_entry        ; 스택 추적용: 함수 진입 기록
        adrp x19, _g_stats_obj.0@PAGE
        add  x19, x19, _g_stats_obj.0@PAGEOFF
        ldr  x8, [x19]                 ; 읽기 — 별도 검사 호출 없음
        add  x20, x8, #1
        mov  x0, x19
        bl   ___tsan_write8            ; 쓰기 직전 검사 (8 바이트)
        str  x20, [x19]
        bl   ___tsan_func_exit
```

- 읽기 `ldr` 에는 `___tsan_read8` 이 없다. 같은 기본 블록에서 같은 주소에 쓰기가 뒤따르면 컴파일러가 읽기 검사를 생략한다(쓰기 검사가 더 강함). 그래서 이 버그의 리포트는 `Write … / Previous write …` 로 나온다.
- `-DFIX` (atomic) 빌드에서는 같은 자리가 `bl ___tsan_atomic64_fetch_add` 한 번이 된다 — atomic 은 런타임이 "원자 접근" 으로 기록하므로 서로 레이스가 아니다.
- `__tsan_func_entry/exit` 가 리포트의 호출 스택(`#1`, `#2`)을, `-g` 가 줄 번호를 만든다. 최적화는 `-O1` 이 무난(Clang 문서 권장).

### Part A — 리포트 한 장을 줄 단위로 읽기

`cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_counter.c -o dc_t && ./dc_t` (실제 출력, 머신 Apple M6):

```text
==================
WARNING: ThreadSanitizer: data race (pid=41428)
  Write of size 8 at 0x000104e10000 by thread T2:
    #0 stats_bump demo_counter.c:47 (TOOL-M09_dc_t:arm64+0x1000007e0)
    #1 record_sample demo_counter.c:52 (TOOL-M09_dc_t:arm64+0x1000007a4)
    #2 sensor_task demo_counter.c:57 (TOOL-M09_dc_t:arm64+0x100000764)

  Previous write of size 8 at 0x000104e10000 by thread T1:
    #0 stats_bump demo_counter.c:47 (TOOL-M09_dc_t:arm64+0x1000007e0)
    #1 record_sample demo_counter.c:52 (TOOL-M09_dc_t:arm64+0x1000007a4)
    #2 sensor_task demo_counter.c:57 (TOOL-M09_dc_t:arm64+0x100000764)

  Location is global 'g_stats_obj.0' at 0x000104e10000 (TOOL-M09_dc_t+0x100008000)

  Thread T2 (tid=6029602, running) created by main thread at:
    #0 pthread_create <null> (libclang_rt.tsan_osx_dynamic.dylib:arm64e+0x335b8)
    #1 main demo_counter.c:87 (TOOL-M09_dc_t:arm64+0x10000069c)

  Thread T1 (tid=6029601, running) created by main thread at:
    … (T2 와 같은 두 줄: pthread_create ← main demo_counter.c:87)

SUMMARY: ThreadSanitizer: data race demo_counter.c:47 in stats_bump
==================
samples=1021382 expect=2000000 lost=978618
ThreadSanitizer: reported 1 warnings
exit=134
```

| 줄 | 뜻 | 이 리포트에서 |
|---|---|---|
| `WARNING: ThreadSanitizer: data race (pid=…)` | 보고 종류. 다른 종류: `thread leak`, `lock-order-inversion (potential deadlock)`, `signal-unsafe call inside of a signal`, `heap-use-after-free` 등 | 데이터 레이스 |
| `Write of size 8 at ADDR by thread T2:` | **현재 접근**: 쓰기/읽기, 바이트 수, 주소, 스레드 번호. `T0` = main, 이후 생성 순서대로 T1, T2… | T2 가 8 B 를 씀 (`unsigned long`) |
| 그 아래 `#0 … #2` | 현재 접근의 호출 스택. **`#0` 의 파일:줄이 실제 메모리 접근** | `stats_bump` 47행 `s->samples++` |
| `Previous write … by thread T1:` | shadow 에 남아 있던 **이전 충돌 접근**과 그 스택(런타임이 기록해 둔 이력에서 복원) | T1 의 같은 줄 |
| `Location is global 'g_stats_obj.0' at …` | 주소의 정체. 전역이면 심볼 이름, 힙이면 할당 스택, 스택 변수면 그 스레드 | `.0` 접미사 = 최적화기가 구조체를 필드별 전역으로 쪼갬(GlobalOpt). `-O0` 빌드에선 `'g_stats_obj'` 로 나왔다 |
| `Thread T2 (tid=…, running) created by main thread at:` | 각 스레드의 출생지. `running`/`finished` 는 보고 순간 상태 | 둘 다 `main` 87행 루프에서 생성 |
| `exit=134` | 이 맥 런타임은 `abort_on_error=true` 가 기본 → 보고가 있으면 끝날 때 `abort()` → 셸에서 128+6 | Linux 기본은 exit 66 (아래) |

읽는 순서: **SUMMARY → #0 두 개 → Location → 생성 위치**. 이 리포트의 해석 한 문장: "`sensor_task` 를 도는 두 스레드(main 87행에서 생성)가 전역 `g_stats_obj.samples` 를 `stats_bump` 47행에서 동기화 없이 쓴다."

**변형 1 — 힙 (`-DHEAP`)**: 위치 줄만 바뀌어 할당 스택까지 준다: `Location is heap block of size 16 at 0x00010b700720 allocated by main thread:` → `#0 calloc <null> (libclang_rt.tsan_osx_dynamic.dylib…)`, `#1 main demo_counter.c:55`.

**변형 2 — 반만 고친 코드 (`-DHALF`)**: 샘플러는 락을 잡고 `++`, 텔레메트리 덤프 `stats_read_and_clear` 는 락을 깜빡했다. 실제 출력:

```text
WARNING: ThreadSanitizer: data race (pid=…)
  Write of size 8 at 0x0001048a0040 by thread T2 (mutexes: write M0):
    #0 stats_bump demo_counter.c:44 (TOOL-M09_dc_hf:arm64+0x1000008e4)
    … (#1 record_sample :52, #2 sensor_task :57)

  Previous write of size 8 at 0x0001048a0040 by thread T1:
    #0 stats_read_and_clear demo_counter.c:65 (TOOL-M09_dc_hf:arm64+0x10000085c)
    #1 telemetry_task demo_counter.c:70 (TOOL-M09_dc_hf:arm64+0x1000007b8)

  Location is global 'g_stats_obj.0' at 0x0001048a0040 (TOOL-M09_dc_hf+0x100008040)

  Mutex M0 (0x0001048a0000) created at:
    #0 pthread_mutex_init <null> (libclang_rt.tsan_osx_dynamic.dylib:arm64e+0x35070)
    #1 main demo_counter.c:81 (TOOL-M09_dc_hf:arm64+0x100000688)
  …
samples=2000000 expect=2000000 lost=0
```

- `(mutexes: write M0)` — 현재 접근 시점에 T2 가 **쥐고 있던 락** 목록(`write` = 배타 잠금, rwlock 읽기 잠금이면 `read`). Previous 쪽에는 그 줄이 **없다** → "한쪽만 락을 잡았다" 가 리포트에 그대로 보인다.
- `Mutex M0 (주소) created at:` — 그 락이 어디서 `pthread_mutex_init` 됐는지. 락이 수십 개인 드라이버에서 "어느 락 얘기냐" 를 바로 알 수 있다. (`PTHREAD_MUTEX_INITIALIZER` 로 정적 초기화한 락은 생성 스택이 없다 — 그래서 데모는 일부러 `pthread_mutex_init` 을 썼다.)
- **결과는 정확했다(lost=0)** 그런데도 잡았다. 텔레메트리가 1000번만 돌아 실제 충돌이 없었을 뿐, happens-before 가 없다는 사실은 그대로다. "테스트 통과 = 레이스 없음" 이 아니다.

**TSan 없이 `-O2`** (6회): `lost=982226`, `1000000`, `322039`, `888338`, `879394`, `773510` — 매번 다르다. `-DFIX`(relaxed `fetch_add`)는 `lost=0`, TSan 보고 없음.

**고치기 (solution Part A, 코드는 6. 정답 코드의 diff):** `stats_record/stats_snapshot` 은 같은 뮤텍스 안에서 두 필드를 쓰고 읽는다. `hits_inc` 는 relaxed `fetch_add`, `hits_take` 는 relaxed `atomic_exchange(c, 0)`.

| 선택 | 이유 |
|---|---|
| 스냅샷도 락 안에서 | 쓰는 쪽만 잠그고 읽는 쪽이 안 잠그면 `-DHALF` 와 같은 리포트. 두 값을 일관된 쌍으로 읽으려면 읽기도 같은 락 |
| 카운터는 relaxed | 이 값으로 다른 데이터를 "넘기지" 않는다. RMW 는 수정 순서의 마지막 값을 읽으므로 증가를 잃지 않는다 (C-A03 Part A) |
| take 는 `exchange` | `v = load(c); store(c, 0);` 은 둘 다 atomic 이라 **TSan 이 조용하지만** 그 사이 증가를 잃는다. 읽기+쓰기를 RMW 하나로 |
| 읽는 스레드의 종료 조건 | `g_writers_left == 0` 을 **먼저** 읽고 그 다음 take — writer 의 `fetch_sub` 이전 증가가 모두 보장된 상태에서 마지막 take 를 한다 (테스트의 `ro.taken == 800000`) |

### Part B — 락 없이 읽는 플래그

`demo_flag.c` 기본 빌드 (TSan, `-O1`) 실제 출력:

```text
WARNING: ThreadSanitizer: data race (pid=41755)
  Write of size 1 at 0x00010224c000 by thread T1:
    #0 calib_task demo_flag.c:49 (TOOL-M09_df_t:arm64+0x1000007f4)

  Previous read of size 1 at 0x00010224c000 by main thread:
    #0 wait_ready demo_flag.c (TOOL-M09_df_t:arm64+0x1000006c0)
    #1 main demo_flag.c:67 (TOOL-M09_df_t:arm64+0x100000728)

  As if synchronized via sleep:
    #0 usleep <null> (libclang_rt.tsan_osx_dynamic.dylib:arm64e+0x31718)
    #1 calib_task demo_flag.c:44 (TOOL-M09_df_t:arm64+0x1000007cc)

  Location is global 'g_ready' at 0x00010224c000 (TOOL-M09_df_t+0x100008000)
  …
SUMMARY: ThreadSanitizer: data race demo_flag.c:49 in calib_task
==================
TIMEOUT: wait_ready() never returned (flag hoisted out of the loop?)
==================
WARNING: ThreadSanitizer: thread leak (pid=41755)
…
exit=3
```

- `by main thread` — main 은 `T0` 대신 이렇게 표시된다. `wait_ready demo_flag.c` 에 **줄 번호가 없다** — 읽기가 루프 밖으로 끌어올려져(hoist) 원래 줄과 대응이 끊겼다. 줄 번호가 없거나 이상하면 "최적화가 접근을 옮겼다" 는 신호다.
- `As if synchronized via sleep:` — TSan 의 친절한 덧붙임: "T1 이 `usleep` 한 덕에 실제로는 순서가 맞았을 것 같지만 **sleep 은 동기화가 아니다**." 테스트를 sleep 으로 맞춘 코드에 단골로 붙는다.
- 그리고 프로그램은 **끝나지 않았다**(2초 알람으로 종료, 그래서 `thread leak` 도 덤으로 보고). `-O1 -fsanitize=thread -S` 로 본 `_wait_ready` 는 `bl ___tsan_read1` → `ldrb` 를 **한 번** 한 뒤 false 면 `LBB0_2: b LBB0_2` (제자리 무한 루프) 였다.

`-O2` (TSan 없음) 는 한술 더 떴다: `_wait_ready:` 가 **`ret` 한 줄**이 되었고 실행 결과는 3번 모두 `payload=0` (기다리지 않고 바로 지나가서 쓰기 전 값을 읽음). C11 6.8.5p6 은 "I/O·volatile·동기화·atomic 이 없는 반복문(조건식이 상수가 아닌)은 끝난다고 가정해도 된다" 고 하고, 다른 스레드가 바꾸는 평범한 변수를 읽는 것 자체가 데이터 레이스(UB)다. 컴파일러는 둘 다 합법적으로 이용했다.

Cortex-M4 로도 확인 (`demo_flag_m4.c`, freestanding, 실제 출력 요약):

```text
wait_plain:                               wait_atomic:  (acquire load)
    ldrb  r0, [r0]     ; 한 번 읽고       .LBB2_1: ldrb r1, [r0]
    cbz   r0, .LBB1_2                              dmb  sy        ; acquire 배리어
    ldr   r0, [g_sample] ; bx lr                   cbnz r1, .LBB2_5   (×4 펼침)
.LBB1_2: b .LBB1_2     ; ← 펌웨어가 "멈춘" 그 모습      cmp r1,#1 ; bne .LBB2_1
```

(arm64 호스트의 `wait_atomic` 은 `ldaprb w9, [x8]` 루프.) 펌웨어에서 "디버그 빌드(-O0)에선 되는데 릴리스(-O2)에서 멈춘다" 의 정체가 이것이다 (C-B09, C-M14).

**`volatile` 로 "고친 척" (`-DVOL`)** — 루프는 끝나고 `payload=42` 도 나오지만 TSan 은 두 개를 보고한다:

```text
WARNING: ThreadSanitizer: data race      Write of size 1 … by thread T1:  #0 calib_task demo_flag.c:49
                                         Previous read of size 1 … by main thread:  #0 wait_ready demo_flag.c:38
  Location is global 'g_ready' …
WARNING: ThreadSanitizer: data race      Read of size 1 at 0x000102c50040 by main thread:  #0 main demo_flag.c:68
                                         Previous write of size 1 … by thread T1:  #0 calib_task demo_flag.c:45
  Location is global 'g_payload' …
ThreadSanitizer: reported 2 warnings
```

`volatile` 은 "매번 메모리에서 읽어라" 일 뿐 atomic 도 순서도 아니다(C11 5.1.2.4 의 데이터 레이스 정의에서 volatile 은 예외가 아니다). 두 번째 보고가 핵심: **payload 읽기가 payload 쓰기와 동기화되지 않았다** — ARM 처럼 순서가 약한 기계에서는 플래그를 보고도 옛 payload 를 읽을 수 있다. (`g_payload` 가 int 인데 `size 1` 인 것은 최적화기가 0/42 두 값만 갖는 static 변수를 1 바이트로 줄였기 때문. `-O0` 빌드에서는 `Read of size 4` 였다.)

**고치기:** `mb_post` 는 데이터를 다 쓴 뒤 `atomic_store_explicit(&mb->ready, true, memory_order_release)`, `mb_wait` 는 `atomic_load_explicit(…, acquire)` 루프(매 바퀴 데드라인 확인 + `sched_yield()`)가 true 를 본 **뒤** 데이터를 복사한다.

release 저장 → 그 값을 읽은 acquire 로드가 synchronizes-with 를 만들고, 그 앞의 `data` 쓰기가 뒤의 `data` 읽기보다 happens-before 가 된다. TSan 은 atomic 의 release/acquire 를 동기화 간선으로 이해하므로 보고가 사라진다 (`-DFIX`: `payload=42`, exit 0). 오래 기다릴 거라면 스핀 대신 잠드는 쪽(연습 3, OS-B06 조건 변수).

### Part B — 락 순서 역전 (잠재적 교착)

`demo_lock_order.c` 는 `move_credit(from, to)` 가 **인자 순서대로** 두 락을 잡는다. T1 = rx→tx, T2 = tx→rx 이지만 **T1 을 join 한 뒤 T2 를 만든다** → 실제 교착은 불가능하다.

이 맥 (Apple clang 21 TSan 런타임) 실측: `TSAN_OPTIONS=help=1` 에 `detect_deadlocks … (Current Value: false)` 이고, `detect_deadlocks=1` 로 켜도 이 프로그램과 더 단순한 A→B / B→A 프로그램 모두 **보고가 없었다**(exit 0). 그래서 같은 소스를 Lima VM `elinux` (Ubuntu aarch64, clang 21.1.8 / gcc 15.2) 에서 돌렸다. 기본 설정 실제 출력 (BuildId 생략):

```text
WARNING: ThreadSanitizer: lock-order-inversion (potential deadlock) (pid=1999)
  Cycle in lock order graph: M0 (0xaaaaac023560) => M1 (0xaaaaac0235a0) => M0

  Mutex M1 acquired here while holding mutex M0 in thread T1:
    #0 pthread_mutex_lock <null> (dl_t+0x70018)
    #1 move_credit /tmp/tool_m09/demo_lock_order.c:48:5 (dl_t+0xff750)
    #2 rx_to_tx /tmp/tool_m09/demo_lock_order.c:60:74 (dl_t+0xff6d4)

    Hint: use TSAN_OPTIONS=second_deadlock_stack=1 to get more informative warning message

  Mutex M0 acquired here while holding mutex M1 in thread T2:
    #0 pthread_mutex_lock <null> (dl_t+0x70018)
    #1 move_credit /tmp/tool_m09/demo_lock_order.c:48:5 (dl_t+0xff750)
    #2 tx_to_rx /tmp/tool_m09/demo_lock_order.c:61:74 (dl_t+0xff70c)

  Thread T1 (tid=2001, finished) created by main thread at:  … main demo_lock_order.c:77
  Thread T2 (tid=2002, running) created by main thread at:   … main demo_lock_order.c:81

SUMMARY: ThreadSanitizer: lock-order-inversion (potential deadlock) (/tmp/tool_m09/dl_t+0x70018) in pthread_mutex_lock
rx=1000 tx=1000 sum=2000
ThreadSanitizer: reported 1 warnings
exit=66
```

```svg
<svg viewBox="0 0 740 236" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">락 순서 그래프: "B 를 잡을 때 A 를 쥐고 있었다" = 간선 A → B. 사이클이면 경고</text>
  <circle cx="110" cy="110" r="38" class="f-acc" stroke="currentColor"/>
  <text x="88" y="106" fill="currentColor" font-family="monospace">M0</text>
  <text x="80" y="124" fill="currentColor" font-size="12">g_rx.lock</text>
  <circle cx="330" cy="110" r="38" class="f-acc" stroke="currentColor"/>
  <text x="308" y="106" fill="currentColor" font-family="monospace">M1</text>
  <text x="300" y="124" fill="currentColor" font-size="12">g_tx.lock</text>
  <path d="M146 96 Q220 50 294 96" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M284 88 L294 96 L281 99" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="160" y="58" fill="currentColor" font-size="12">T1: 48행 (M0 쥐고 M1)</text>
  <path d="M294 124 Q220 170 146 124" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M156 132 L146 124 L159 121" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="160" y="178" fill="currentColor" font-size="12">T2: 48행 (M1 쥐고 M0)</text>
  <rect x="420" y="50" width="310" height="120" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="432" y="74" fill="currentColor" font-weight="bold">고친 뒤 (rank / id 순서)</text>
  <text x="432" y="98" fill="currentColor" font-size="12">rx→tx: M0 → M1</text>
  <text x="432" y="118" fill="currentColor" font-size="12">tx→rx: M0 → M1 (작은 id 먼저)</text>
  <text x="432" y="138" fill="currentColor" font-size="12">간선은 M0 → M1 하나뿐 → 사이클 없음</text>
  <text x="432" y="158" fill="currentColor" font-size="12">= 순환 대기 조건이 깨짐 (OS-M10)</text>
  <text x="10" y="222" fill="currentColor">실제로 동시에 돌았는지는 상관없다 — 그래프는 실행 이력을 누적한다 (같은 스레드 하나여도: Part C 실측).</text>
</svg>
```

| 줄 | 뜻 |
|---|---|
| `Cycle in lock order graph: M0 (addr) => M1 (addr) => M0` | 락 이름 M0/M1 은 이 보고 안에서만 쓰는 번호. 주소로 어떤 락인지 맞춘다 (`g_rx`, `g_tx` 의 `lock` 필드) |
| `Mutex M1 acquired here while holding mutex M0 in thread T1:` | 간선 M0 → M1 이 생긴 곳: **두 번째** 락을 잡은 스택 (48행) |
| `Hint: … second_deadlock_stack=1` | 기본은 간선마다 스택 하나. 켜면 "먼저 잡은 락" 의 스택도 |
| `Thread T1 (…, finished)`, exit 66 | T1 은 이미 끝났다 — 동시에 돈 적이 없는데도 보고. 66 = Linux 기본 `exitcode` (맥은 abort 라 134) |

`TSAN_OPTIONS=second_deadlock_stack=1` 이면 각 간선 아래에 이것이 추가된다 (실제 출력):

```text
  Mutex M0 previously acquired by the same thread here:
    #0 pthread_mutex_lock <null> (dl_t+0x70018)
    #1 move_credit /tmp/tool_m09/demo_lock_order.c:47:5 (dl_t+0xff748)
    #2 rx_to_tx /tmp/tool_m09/demo_lock_order.c:60:74 (dl_t+0xff6d4)
```

gcc 15 (`libtsan.so.2`)도 같은 형식의 보고를 냈다. 그리고 맥에서 **진짜로 동시에** 돌리면 (`-DCONCURRENT`, 100만 번씩, TSan 없음) 3번 모두 `TIMEOUT: deadlock (each thread holds one lock and waits for the other)` 로 끝났다. `-DCONCURRENT -DFIX` 는 3번 모두 `rx=1000 tx=1000 sum=2000`. TSan(Linux)은 이 교착을 **순차 실행에서 미리** 알려 준 셈이다.

**고치기 — `lock_pair`:** `a == b` 면 `rl_lock(a)` 한 번으로 끝, 아니면 rank 를 비교해 작은 쪽을 먼저 `rl_lock`.

같은 등급의 락(풀 배열의 원소들)처럼 rank 를 따로 줄 수 없으면 **주소 순서**로 정한다(Linux 스케줄러의 `double_rq_lock` 이 같은 아이디어). 해제 순서는 교착과 무관하지만 관례상 역순으로 푼다.

### Part C — 텔레메트리 허브

레거시 `hub_reconfigure` 만 되돌려 넣은 `legacy.c` (solution.c 에서 176행 한 줄만 `log → cfg` 로 교체)를 Linux TSan 으로 돌린 실제 출력:

```text
WARNING: ThreadSanitizer: lock-order-inversion (potential deadlock) (pid=2374)
  Cycle in lock order graph: M0 (0xaaaaaabf6c08) => M1 (0xaaaaaabf6c50) => M0

  Mutex M1 acquired here while holding mutex M0 in main thread:
    #0 pthread_mutex_lock <null> (legacy_t+0x6ff58)
    #1 rl_lock /tmp/tool_m09/legacy.c:49:5 (legacy_t+0x102010)
    #2 hub_sample /tmp/tool_m09/legacy.c:169:5 (legacy_t+0x102010)
    #3 main /tmp/tool_m09/legacy.c:362:5 (legacy_t+0x10087c)

  Mutex M0 acquired here while holding mutex M1 in main thread:
    #0 pthread_mutex_lock <null> (legacy_t+0x6ff58)
    #1 rl_lock /tmp/tool_m09/legacy.c:49:5 (legacy_t+0x102430)
    #2 hub_reconfigure /tmp/tool_m09/legacy.c:176:26 (legacy_t+0x102430)
    #3 main /tmp/tool_m09/legacy.c:364:5 (legacy_t+0x100950)
  …
FAIL legacy.c:369  atomic_load(&g_order_violations) == 0u && atomic_load(&g_relock) == 0u
SKIP 5 stress checks (Part C): unit checks failed
24/30 passed
exit=66
```

두 간선이 **모두 main thread** 에서 나왔다 — Part C **단일 스레드 단위 테스트**(362행 `hub_sample`, 364행 `hub_reconfigure`)만으로 잡혔다. 래퍼 함수 `rl_lock` 이 `#1` 에 끼어 있으니 `#2` 가 실제 호출자다. 하네스의 rank 검사기도 같은 버그를 FAIL 로 잡고 스트레스를 건너뛰었다(그래서 멈추지 않았다) — 맥처럼 TSan 교착 검출이 없는 환경에서는 이런 rank 검사가 대안이다.

왜 두 락을 **중첩**해야 하나? 요구사항 "로그 안에서 버전이 뒤로 가지 않는다" 때문이다. `hub_sample` 이 설정을 복사하고 cfg 락을 **푼 뒤** 로그 락을 잡으면, 그 틈에 `hub_reconfigure` 가 CFG 마커를 먼저 써서 "버전 5 마커 뒤에 버전 4 샘플" 이 생긴다. 하네스는 로그 전체를 처음부터 다시 계산(`ver`, `gain`, `off` 를 따라가며)해 이걸 검사한다.

세 버그의 수정 (6. 정답 코드 diff 참고):

| 레거시 | 고친 코드 |
|---|---|
| `per_ch[ch]++` (평범한 변수) | `atomic_fetch_add_explicit(&h->per_ch[ch], 1u, memory_order_relaxed)` |
| `bool stop` | `hub_request_stop` = release store, `hub_should_stop` = acquire load |
| `hub_reconfigure`: log → cfg | `lock_pair(&h->cfg_lk, &h->log_lk)` → 항상 cfg(rank 1) → log(rank 2), `hub_sample` 과 같은 순서 |

per_ch 통계를 cfg 락 **밖**에서 relaxed 로 올리는 이유: 이 값은 다른 데이터를 넘기지 않는 순수 통계라 락 구간을 늘릴 필요가 없다(락은 짧게, OS-B05). 고친 solution 은 Linux TSan(clang)에서도 `30/30 passed`, 보고 없음.

### TSAN_OPTIONS · suppressions — `demo_options.c`

`TSAN_OPTIONS` 는 `:` 로 이어 쓴다 (`TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1 ./a.out`). 이 맥에서 `TSAN_OPTIONS=help=1 ./a.out` 으로 본 기본값과 실험 결과:

| 옵션 | 기본값(이 맥) | 효과 / 실측 |
|---|---|---|
| `halt_on_error=1` | false | 첫 보고 직후 종료. 실측: CRC 레이스 1개만 보고, exit 134 |
| `abort_on_error` / `exitcode` | true / 66 | 맥은 abort → **134**. `abort_on_error=0` 이면 실측 exit **66** (Linux 와 같음) |
| `second_deadlock_stack=1` | false | 교착 보고에 "먼저 잡은 락" 스택 추가 (위 Linux 실측) |
| `detect_deadlocks` | **false** | 락 순서 검출. 이 맥에선 1 로 켜도 보고 없음(실측). Linux 런타임은 기본으로 보고 |
| `history_size` | 0 (0..7) | 스레드별 접근 이력 크기. `Previous …` 스택이 `[failed to restore the stack]` 으로 나오면 늘린다. 실측: T1 이 첫 쓰기 뒤 1억 번 접근한 다음 T2 가 써도 기본값에서 Previous 스택이 복원됐다 — 이 런타임에선 재현 못 함 |
| `suppressions=file` | 없음 | 아래 |
| `log_path=/tmp/tsan_log` | stderr | 실측: `/tmp/…_tsanlog.30468` 파일에 WARNING 2개 |

**suppressions** — 고칠 수 없는 코드(벤더 SDK)의 알려진 레이스만 숨긴다. 이 디렉터리의 `tsan.supp`:

```text
# TOOL-M09: 벤더 SDK 레이스는 업스트림에 보고했고 우리 쪽 감시에서만 숨긴다
race:legacy_crc_update
```

`demo_options.c` 기본 빌드는 1단계(CRC 스레드 2개, 벤더 레이스) → join → 2단계(RX 스레드 2개, 우리 `rx_count++` 레이스). 실제 출력 요약:

```text
./do_t                                                     → 보고 2개 (25행 legacy_crc_update, 36행 on_rx_byte), exit=134
TSAN_OPTIONS=suppressions=tsan.supp:print_suppressions=1   → SUMMARY: … demo_options.c:36 in on_rx_byte
                                                              rx_count=104389 (expect 200000)
                                                              ThreadSanitizer: reported 1 warnings
                                                              ThreadSanitizer: Matched 2 suppressions (pid=42741):
                                                              2 race:legacy_crc_update
                                                              exit=134
```

규칙 종류: `race:`(함수·파일·전역 이름 패턴), `deadlock:`, `mutex:`, `thread:`, `signal:`, `called_from_lib:`(라이브러리 단위). 대안으로 **컴파일 시** 계측을 빼는 `__attribute__((no_sanitize("thread")))` / `-fsanitize-ignorelist=` 가 있지만, 그러면 그 코드의 레이스는 아예 안 보인다(아래 `demo_limits.c`). suppression 은 "보고는 되지만 숨김" 이라 `print_suppressions` 로 몇 번 맞았는지 남는다 — 리뷰에서 추적 가능해서 더 낫다.

**주의 — 같은 루프 안이면 suppression 이 우리 버그까지 덮었다.** `-DONE_LOOP` (한 스레드 루프에서 벤더 CRC 갱신과 `rx_count++` 를 같이) 실측: 3번 모두 보고는 **CRC 하나뿐**, `rx_count=200000`. 여기에 `tsan.supp` 를 걸면 **보고 0개, exit 0** — 우리 레이스가 그대로 남았는데 CI 는 초록. 순서를 바꿔 `rx_count++` 를 먼저 두면 이번엔 rx_count 만 보고됐다(스크래치 실험). 같은 두 스레드 사이의 두 번째 레이스가 이 런타임에서 보고되지 않은 정확한 내부 원인은 확인하지 못했다. 확실한 교훈: **보고 하나를 고치거나 숨겼으면 다시 돌려라. "보고 1개 = 버그 1개" 가 아니다.**

### 비용 — `demo_cost.c`

4 스레드가 각 8 MB 버퍼를 8번 채우고 필터링, 1024 샘플마다 뮤텍스로 공유 히스토그램에 합침 (레이스 없음). Apple M6 (12코어: `hw.perflevel0` Super 2 · `perflevel1` Performance 4 · `perflevel2` Efficiency 6), `Apple clang version 21.0.0 (clang-2100.0.123.102)`, `/usr/bin/time -l` 3회씩:

| 빌드 | 프로그램 측정 wall | user CPU | instructions retired | max RSS |
|---|---|---|---|---|
| `-O1 -g` | 72.4 – 78.7 ms | 0.22 s | 1.84 – 1.88 G | 35.5 MB |
| `-O1 -g -fsanitize=thread` | 743 – 903 ms | 2.12 – 2.36 s | 32.2 – 32.5 G | 145 – 149 MB |
| 배율 | **약 10–12×** | **약 10×** | 약 17× | **약 4.1×** |

Clang 문서의 "보통 5–15배 느림, 메모리 5–10배" 와 같은 범위(메모리는 이 작업에선 하한보다 조금 낮게 나왔다). 실무 결론: TSan 빌드는 따로 두고 **동시성 스트레스 테스트 전용** CI 잡으로 돌린다. ASan 과 함께 걸 수 없다(`-fsanitize=address,thread` 는 컴파일 에러) → 빌드 2벌.

### TSan 이 못 보는 것

`demo_limits.c` 실측:

```text
./dlim_t            → ok=400000 crc_err=0                (보고 없음, exit 0)
./dlim_t fault      → WARNING: data race … Location is global 'g_crc_errors' …
                      SUMMARY: … demo_limits.c:40 in rx_task
                      ok=399600 crc_err=250              (400 이어야 함, exit 134)
./dlim_n (-DNOINST) → ok=400000 crc_err=0 dma_done=399954 (expect 400000)   (보고 없음, exit 0)
                      … dma_done=399999, 399956 (3회 모두 보고 없음)
```

| 못 보는 것 | 실측 / 근거 | 대책 |
|---|---|---|
| **실행되지 않은 경로** | CRC 에러 경로의 레이스는 `fault` 를 줘야만 보고 | 에러 주입, 커버리지와 함께 보기, 결함 주입 테스트 (SIV-M09) |
| **계측 안 된 코드** | `no_sanitize("thread")` 함수의 `g_dma_done++` 는 증가를 잃는데도 침묵. TSan 없이 빌드한 라이브러리·어셈블리도 같다 | 의존성도 TSan 으로 빌드, 어셈블리 경계는 리뷰 |
| **같은 8 바이트 칸의 두 번째 레이스** | `demo_flag -DVOL -DNEAR`: `g_payload`(0x…8000) 와 `g_ready`(0x…8004) 가 같은 8 B 칸 → `g_ready` 보고 1개뿐, payload 레이스는 침묵. 기본(64 B 떨어뜨림)은 2개 | 고치고 다시 돌린다. 런타임은 보고한 칸의 shadow 를 지워 같은 곳을 반복 보고하지 않는다(tsan_rtl_access.cpp 의 `DoReportRace` 주석 "This prevents trapping on this address in future") |
| **atomic 끼리의 논리 오류** | load 후 store 로 비우기, check-then-act — 모두 atomic 이면 데이터 레이스가 아님 (C-A03 `demo_lost_update.c`) | RMW 하나로 설계 + 결과 검사 (Part A `hits_take`) |
| **잠재적 교착 (이 맥)** | `detect_deadlocks=1` 로도 보고 없음 | Linux 에서 TSan, 또는 rank 검사기 / lockdep 류 (OS-M11) |
| **실제로 걸린 교착** | TSan 은 멈춘 프로그램을 진단하지 않는다 | 디버거 `thread backtrace all` (TOOL-M07), 워치독 |
| **ISR · DMA · 다른 코어의 하드웨어** | TSan 은 pthread 와 POSIX 시그널만 안다 | 호스트에선 ISR 을 스레드로 모델링, 타깃에선 리뷰 + `-Wthread-safety` 정적 분석 + RTOS 트레이스 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution        # 30/30 passed
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_t && ./so_t                 # 30/30, 보고 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_a && ./so_a   # 깨끗
```

## 7. 자주 하는 실수

**1. `volatile` 을 붙이고 "고쳤다" 고 한다.** 루프가 끝나게 될 뿐 데이터 레이스는 그대로다(`-DVOL` 실측: 보고 2개). volatile 은 원자성도, 다른 변수와의 순서도 주지 않는다 (C-B09). → 플래그는 `atomic_bool` + release/acquire, 또는 락.

**2. 쓰는 쪽만 잠근다.** `-DHALF` 리포트처럼 `(mutexes: write M0)` 가 한쪽에만 붙는다. 읽기도, 리셋도, 디버그 덤프도 같은 락. 규칙: "이 변수는 이 락이 지킨다" 를 변수 옆 주석에 쓰고(`/* guarded by stats_lock */`), clang 이면 `-Wthread-safety` 의 `guarded_by` 로 컴파일러가 검사하게 한다.

**3. 두 필드를 각각 atomic 으로 바꾼다.** 각 증가는 안 잃지만 쌍이 찢어진다(`samples` 새 값 + `bytes` 옛 값). TSan 은 조용하다. → 같이 움직이는 값은 한 락 아래 (Part A 체크 `!g_torn`).

```c
atomic_fetch_add(&s->samples, 1); atomic_fetch_add(&s->bytes, 64);   /* 틀림: 읽는 쪽이 중간 상태를 봄 */
```

**4. "읽고 0 으로" 를 load + store 로.** `v = atomic_load(c); atomic_store(c, 0);` — 그 사이 증가가 사라지는데 TSan 은 못 잡는다. → `atomic_exchange(c, 0)`.

**5. 교착 경고를 "실제로 안 멈췄으니 오탐" 이라고 suppress 한다.** `lock-order-inversion` 은 "타이밍만 맞으면 멈춘다" 는 뜻이다(`-DCONCURRENT` 실측: 3번 모두 교착). → 전역 락 순서를 정하고 문서화. 순서를 정할 수 없으면 trylock back-off (연습 2).

**6. TSan 결과가 깨끗하니 레이스가 없다고 결론낸다.** 실행된 경로·계측된 코드·보고된 첫 레이스까지만 본 것이다. 이 맥에서는 교착 검출이 아예 꺼져 있다. → 스트레스 테스트를 여러 번, 에러 경로까지, Linux CI 에서도. 빌드는 `-g -O1` (`-g` 없으면 줄 번호가 없고, `-O3` 는 인라인으로 프레임이 사라진다).


## 8. 면접 Q&A

<details>
<summary>Q1. TSan 이 말하는 data race 의 정의는? race condition 과 같은 말인가요?</summary>

C11 5.1.2.4p25: 서로 다른 스레드의 충돌하는 두 접근(같은 위치, 최소 하나는 쓰기) 중 하나 이상이 비원자이고 어느 쪽도 다른 쪽보다 happens-before 가 아니면 데이터 레이스 = UB. **race condition** 은 더 넓은 말로, 실행 순서에 따라 결과가 달라지는 논리 버그다. atomic load 후 atomic store 로 비우기는 race condition 이지만 데이터 레이스는 아니라서 TSan 이 못 잡는다. 반대로 결과가 우연히 맞아도 동기화가 없으면 데이터 레이스다(`-DHALF`: lost=0 인데 보고).

</details>

<details>
<summary>Q2. 이 리포트를 읽어 보세요 (demo_counter 리포트를 보여 주며). 어떻게 고치죠?</summary>

SUMMARY 로 위치(`demo_counter.c:47 stats_bump`), `#0` 두 개로 같은 줄을 T1·T2 가 쓴다는 것, Location 으로 전역 `g_stats_obj`(`.0` 은 최적화기가 쪼갠 첫 필드), 생성 스택으로 둘 다 main 의 루프에서 만든 샘플러임을 말한다. `Write`/`Previous write` 둘 다 쓰기인 건 `++` 의 읽기 검사가 생략됐기 때문. 수정: 단독 통계면 `atomic_fetch_add_explicit(…, relaxed)`, 다른 필드와 같이 움직이면 뮤텍스. 이후 TSan 으로 다시 돌려 보고 0 을 확인.

</details>

<details>
<summary>Q3. 교착이 일어나지 않았는데 TSan 은 왜 lock-order-inversion 을 보고하나요? 오탐인가요?</summary>

TSan 은 "B 를 잠글 때 A 를 쥐고 있었다" 를 간선 A→B 로 누적하는 락 순서 그래프를 유지하고, 사이클이 생기면 보고한다. 그 두 경로가 실제로 겹치지 않았어도 다른 타이밍에선 각자 하나씩 쥐고 서로를 기다릴 수 있으므로 **잠재적** 교착이고 오탐이 아니다(같은 코드를 동시에 돌리니 3/3 교착). 진짜 오탐에 가까운 경우는 상위 락이 이미 두 경로를 직렬화하는 경우(gate lock) 정도인데, 그래도 순서를 맞추는 게 싸다. Linux 커널의 lockdep 이 같은 원리다.

</details>

<details>
<summary>Q4. 맥에서 TSan 을 돌렸는데 lock-order 경고가 안 나옵니다. 왜죠?</summary>

이 맥의 Apple clang 21 TSan 런타임은 `detect_deadlocks` 기본값이 false 이고, 1 로 켜도 가장 단순한 A→B/B→A 프로그램조차 보고하지 않았다(실측). Linux(Lima VM, clang 21 / gcc 15)에서는 기본으로 보고된다. 그래서 "TSan 이 조용하다" 를 믿기 전에, 일부러 넣은 버그를 그 플랫폼의 도구가 실제로 잡는지부터 확인한다(이 레슨의 데모들이 그 역할).

</details>

<details>
<summary>Q5. TSan 은 얼마나 비싸고, 무엇을 못 잡나요?</summary>

이 맥 실측으로 CPU 약 10배, 메모리 약 4배(문서상 5–15배, 5–10배). ASan 과 동시에 못 켠다. 못 잡는 것: 실행 안 된 경로, 계측 안 된 코드(no_sanitize, TSan 없이 빌드한 라이브러리, 어셈블리), atomic 끼리의 논리 오류, 이미 보고한 8 바이트 칸의 다음 레이스, 실제로 걸린 교착 자체, 그리고 ISR/DMA 같은 하드웨어 동시성. 타이밍과는 무관하게 happens-before 로 판단하므로 "겹쳐야만 잡히는" 건 아니지만, 그 접근이 **실행**은 되어야 한다.

</details>

<details>
<summary>Q6. 플래그 하나 기다리는 건데 꼭 atomic 이어야 하나요? 우리 MCU 는 단일 코어인데요.</summary>

언어 차원에서 평범한 변수를 다른 스레드(또는 ISR)가 바꾸는 걸 기다리는 루프는 데이터 레이스 = UB 이고, 컴파일러는 실제로 루프를 지우거나(`-O2`: `ret`) 한 번만 읽고 무한 루프로 만든다(Cortex-M4: `ldrb` 후 `b .`). 단일 코어 ISR 이면 CPU 재배치는 문제가 아니므로 `volatile sig_atomic_t` 또는 relaxed atomic + 컴파일러 배리어로 충분한 경우가 많지만(C-M14, C-A02), 플래그가 **다른 데이터의 게시**라면 release/acquire 가 정석이다. Cortex-M4 에서 acquire 로드는 `ldrb` + `dmb sy` 로 나왔다 — 단일 코어에선 비용이 작다.

</details>

<details>
<summary>Q7. 타깃(Cortex-M, RTOS)에서는 TSan 이 없는데 같은 버그를 어떻게 찾나요?</summary>

(1) 드라이버·큐·프로토콜 로직을 HAL 뒤로 분리해 호스트에서 pthread 로 ISR/태스크를 모델링하고 TSan + 스트레스로 돌린다(이 레슨의 방식). (2) 컴파일 시 검사: clang `-Wthread-safety` 의 capability 어노테이션(`guarded_by`, `acquires`)으로 "이 변수는 이 락 아래" 를 강제. (3) 런타임 검사: 이 레슨의 rank 검사기 같은 lock level assert, RTOS 의 mutex 소유자 검사, 우선순위 상속 설정(RTOS-M04). (4) Linux 커널이면 lockdep(락 순서)과 KCSAN(데이터 레이스). (5) 트레이스(SystemView, Tracealyzer)로 실제 인터리빙 확인.

</details>

## 9. 추가 연습

### 연습 1 — 지연 초기화 레이스와 `pthread_once`

CRC-16 테이블을 처음 쓸 때 만든다. `if (!g_table_ready) { init(); g_table_ready = true; }` 를 여러 스레드가 동시에 부르면 무엇이 깨지는가? 8 스레드가 동시에 `crc16("123456789")` 를 불러도 초기화는 **정확히 한 번**, 결과는 모두 `0x29B1` 이 되게 고쳐라.

<details>
<summary>답 보기</summary>

순진한 버전은 (a) `g_table_ready` 와 테이블 쓰기·읽기가 모두 데이터 레이스, (b) 두 스레드가 동시에 init 하거나, 다른 스레드가 반쯤 찬 테이블을 읽을 수 있다. `pthread_once(&once, crc_table_init)` 는 처음 부른 스레드만 init 을 실행하고 나머지는 끝날 때까지 기다리며, init 완료가 이후 호출보다 happens-before 다(POSIX 메모리 동기화). practice.c: `g_init_calls == 1`, 8개 결과 모두 `0x29B1`. C11 에도 `call_once` 가 있지만 macOS 의 `<threads.h>` 지원이 없어서 pthread 를 썼다. 직접 짜는 DCLP 는 OS-M08 의 주제.

</details>

### 연습 2 — 순서를 정할 수 없을 때: trylock back-off

두 계좌의 락 순서를 미리 정할 수 없다고 하자(예: 콜백 안에서 두 번째 락이 정해짐). 첫 락은 잡고, 둘째는 `pthread_mutex_trylock` — 실패하면 첫 락을 **놓고** 다시 시도. a→b, b→a 를 5만 번씩 동시에 돌려도 교착 없이 합계 1000 을 지켜라.

<details>
<summary>답 보기</summary>

교착의 4조건 중 "점유 대기(hold and wait)" 를 깬다 — 둘째를 못 얻으면 쥔 것을 내려놓는다. 대가는 재시도(실측 back-off 횟수 3 ~ 23628, 실행마다 크게 다름)와 이론상 livelock 가능성(둘이 계속 양보) — `sched_yield` 나 랜덤 지연으로 완화한다. Linux VM 의 TSan 은 이 버전에 lock-order-inversion 을 보고하지 않았다(실측, 7/7 통과) — trylock 은 막히지 않으므로 교착 간선이 되지 않는다. 정해진 순서가 가능하면 그쪽이 더 단순하고 빠르다 (OS-M10).

</details>

### 연습 3 — busy-wait 대신 잠드는 정지 이벤트

heartbeat 가 `hub_should_stop()` 을 돌며 CPU 를 태운다. `event_set / event_wait(timeout_ms)` 를 mutex + cond 로 만들어 4개 태스크를 broadcast 한 번으로 깨워라. 아무도 set 하지 않으면 타임아웃으로 false.

<details>
<summary>답 보기</summary>

`set` 플래그는 락 안에서 바꾸고 `pthread_cond_broadcast`. 기다리는 쪽은 `while (!e->set && rc != ETIMEDOUT) rc = pthread_cond_timedwait(...)` — POSIX 는 가짜 깨어남(spurious wakeup)을 허용하므로 `while` 로 재검사한다 (OS-B06). macOS 엔 `pthread_condattr_setclock` 이 없어 절대 시각을 `CLOCK_REALTIME` 으로 계산했다(벽시계가 바뀌면 영향 — Linux 면 `CLOCK_MONOTONIC` 으로 설정). RTOS 라면 `xEventGroupWaitBits` / 태스크 알림(RTOS-M01)이 같은 역할. practice.c: 30 ms 타임아웃 → false, broadcast 후 4개 모두 깨어남.

</details>

### 연습 4 — join 도 동기화다

8 스레드가 각자 `g_slots[i].sum` 에 결과를 쓰고 main 이 join 뒤에 읽는다. atomic 도 락도 없는데 TSan 이 조용한 이유를 설명하고, 결과가 단일 스레드 계산과 같음을 확인하라.

<details>
<summary>답 보기</summary>

(1) 각 스레드는 **자기 칸에만** 쓴다 — 충돌하는 접근이 없다. (2) main 의 읽기는 `pthread_join` 이후 — 스레드 종료가 join 반환보다 happens-before 다(POSIX 4.12 메모리 동기화, TSan 도 create/join 을 동기화 간선으로 본다). 칸을 64 B 로 패딩한 건 정확성이 아니라 false sharing 성능 때문(ARCH-M08). 만약 main 이 join **전에** 진행률을 보려고 읽으면 레이스 → 그때는 relaxed atomic.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice              # 7/7 passed
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o practice_t && ./practice_t          # 7/7, 보고 없음
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pa && ./pa   # 깨끗
```

## 10. 핵심 정리

- 데이터 레이스 = 다른 스레드의 충돌 접근 + 하나 이상 비원자 + happens-before 없음 (C11 5.1.2.4p25). TSan 은 값이 아니라 **동기화 유무**를 본다 — 결과가 맞아도 잡고, atomic 끼리의 논리 오류는 못 잡는다.
- 리포트 읽는 순서: SUMMARY → 두 `#0` (현재 / Previous) → `Location`(global / heap + 할당 스택) → `(mutexes: …)` 와 `Mutex Mx created at` → `Thread Tn created at`. `As if synchronized via sleep` = sleep 은 동기화가 아니다.
- 고치는 도구: 같이 움직이는 값 → 뮤텍스(읽기도), 단독 카운터 → relaxed `fetch_add`, 읽고-비우기 → `exchange`, 플래그 게시 → release/acquire + 타임아웃, 두 락 → 전역 순서(rank/주소), `a == b` 는 한 번만.
- 평범한 bool busy-wait 는 UB 이고 실제로 `-O2` 에서 `ret`(루프 삭제), Cortex-M4 에서 `b .`(무한 루프)로 컴파일됐다. `volatile` 은 루프만 살릴 뿐 레이스는 그대로.
- lock-order-inversion 은 교착이 안 일어나도 그래프 사이클로 미리 보고한다. 이 맥 TSan 은 검출 꺼짐(켜도 무보고) → Linux 에서 확인, 또는 rank 검사기.
- 옵션: `halt_on_error`, `abort_on_error=0`(맥 134 → 66), `second_deadlock_stack=1`, `suppressions=`(`race:fn`), `log_path`, `history_size`. 비용 실측 약 10× CPU, 4× 메모리, ASan 과 별도 빌드.
- 못 보는 것: 안 돈 경로, 계측 안 된 코드, 같은 8 B 칸·같은 루프의 다음 레이스, ISR/DMA. **하나 고치면 다시 돌린다.**

## 참고자료

- C11 (N1570): 5.1.2.4 Multi-threaded executions and data races (p4 conflict, p25 data race), 6.8.5p6 반복문 종료 가정, 7.17 Atomics — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- POSIX.1-2017: XBD 4.12 Memory Synchronization, `pthread_mutex_lock` (DEFAULT 타입 재잠금 UB), `pthread_cond_timedwait` (spurious wakeup), `pthread_once` — <https://pubs.opengroup.org/onlinepubs/9699919799/>
- Clang ThreadSanitizer 문서 (비용, `-O1` 권장, `no_sanitize("thread")`, ignorelist): <https://clang.llvm.org/docs/ThreadSanitizer.html>
- google/sanitizers wiki — ThreadSanitizerFlags, ThreadSanitizerSuppressions, ThreadSanitizerDeadlockDetector, ThreadSanitizerAlgorithm: <https://github.com/google/sanitizers/wiki>
- LLVM compiler-rt `lib/tsan/rtl/` (tsan_flags.inc, tsan_rtl_access.cpp `DoReportRace`): <https://github.com/llvm/llvm-project/tree/main/compiler-rt/lib/tsan/rtl>
- Clang Thread Safety Analysis (`-Wthread-safety`): <https://clang.llvm.org/docs/ThreadSafetyAnalysis.html>
- Linux kernel: lockdep (`Documentation/locking/lockdep-design.rst`), KCSAN (`Documentation/dev-tools/kcsan.rst`) — <https://docs.kernel.org/>
- OSTEP 28장 Locks, 32장 Common Concurrency Problems (교착·락 순서) — <https://pages.cs.wisc.edu/~remzi/OSTEP/>
- 관련 레슨: OS-B04 (counter++ 레이스·asm), OS-B05 (뮤텍스·경합 비용), OS-B06 (조건 변수), C-B09 (volatile), C-M14 (ISR 공유 변수), C-A02 (배리어), C-A03 (memory_order, TSan 이 못 잡는 lost update), OS-M08 (DCLP), OS-M10 (교착 4조건), OS-M11 (mini lockdep), RTOS-B10 · RTOS-M04 (RTOS 뮤텍스·우선순위 상속), TOOL-M07 (디버거), TOOL-B11 · TOOL-B12 (ASan · UBSan), ARCH-M08 (false sharing), SIV-M09 (동시성 스트레스)
