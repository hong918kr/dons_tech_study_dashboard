---
id: OS-M08
time: 90분
updated: 2026-11-08
---
# OS-M08 C11 atomics 동기화 패턴: 메시지 패싱·DCLP

## 학습 목표

- **메시지 패싱(message passing, MP)** 을 relaxed 플래그로 짜면 Apple M6 에서 실제로 깨진다는 것을 스트레스 테스트로 재현하고, 결과를 **STALE(통째로 옛것) / TORN(섞임)** 으로 나눠 센다. release 한쪽, acquire 한쪽만 붙여도 여전히 깨진다는 것을 숫자로 보인다.
- **TSan 이 보는 것 = C11 모델 위반**, 하드웨어가 실제로 재배치했는지와는 별개라는 것을 안다 (TSan 아래에서는 재배치 0 건인데 경고는 나온다. 반대로 payload 를 relaxed atomic 으로 바꾸면 재배치는 여전한데 TSan 은 침묵한다).
- **double-checked locking(DCLP)** 지연 초기화를 일반 포인터 / relaxed 포인터 / acquire+release / `pthread_once` 로 짜 보고, 앞의 둘이 실제로 덜 만든 객체를 넘기는 것을 재현한 뒤 고친다. C11 `call_once`(macOS 에는 없음)와 C++ magic static 이 같은 문제를 어떻게 푸는지 말할 수 있다.
- 락을 쓸 수 없는 곳(ISR, 다른 코어, 스케줄러 시작 전)을 위한 **드라이버 1회 초기화 상태 기계** `UNINIT → BUSY → READY` 를 CAS 로 만들고, `pthread_once` 로는 안 되는 **프로브 실패 후 재시도** 를 지원한다.

## 1. 문제

"플래그 하나로 다른 코어에 데이터를 넘기는 코드, 그리고 싱글턴 지연 초기화 — 여기 버그가 있나요?" 는 펌웨어·시스템 면접에서 memory_order 지식을 **실제 코드 패턴** 에 쓸 수 있는지 보는 단골 질문이다. Cortex-A + Cortex-M 이 공유 SRAM 으로 메시지를 주고받는 SoC(STM32MP1, i.MX 8 계열), 듀얼 코어 MCU(STM32H7, nRF5340, RP2040), SMP RTOS 의 드라이버 지연 초기화가 전부 이 두 패턴이다.

C-A03 은 1칸 채널의 release/acquire 를 가르쳤고, **relaxed 로 바꿔도 60만 개 메시지 동안 한 번도 깨지지 않았다** 는 것까지 보였다 (C-A03 5. 풀이 Part B). 이 레슨은 거기서 시작한다. 정말 하드웨어는 안 깨뜨리나? 깨뜨리게 하려면 어떤 모양이어야 하나? 그리고 "가끔만 깨지는" 같은 버그가 지연 초기화(DCLP)에서는 어떻게 생기나?

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">메시지 패싱(MP): relaxed 면 "flag 는 새것, payload 는 옛것" 이 허용된다</text>
  <text x="10" y="70" fill="currentColor">writer</text>
  <text x="10" y="86" fill="currentColor">(Core1)</text>
  <text x="10" y="190" fill="currentColor">reader</text>
  <text x="10" y="206" fill="currentColor">(Core0)</text>
  <rect x="80" y="55" width="200" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="88" y="77" fill="currentColor" font-family="monospace">pl = {v, f(v), g(v), h(v)}</text>
  <rect x="300" y="55" width="150" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="310" y="77" fill="currentColor" font-family="monospace">flag = v (str)</text>
  <rect x="300" y="175" width="160" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="308" y="197" fill="currentColor" font-family="monospace">flag == v (ldr)</text>
  <rect x="480" y="175" width="160" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="490" y="197" fill="currentColor" font-family="monospace">out = pl (ldr)</text>
  <path d="M375 89 L380 175" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M374 165 L380 175 L386 165" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="390" y="135" fill="currentColor">새 값을 봤다</text>
  <path d="M180 89 C 300 140, 470 120, 555 175" stroke="currentColor" fill="none" stroke-dasharray="5 4"/>
  <text x="560" y="130" fill="currentColor">pl 쓰기는 아직</text>
  <text x="560" y="146" fill="currentColor">안 보일 수 있다</text>
  <g fill="currentColor">
    <text x="10" y="240">relaxed: 사이에 happens-before 없음 → store→store, load→load 둘 다 CPU 가 바꿔도 된다.</text>
    <text x="10" y="262">release(stlr) + acquire(ldapr): flag 에서 값 v 를 읽으면 pl 쓰기가 반드시 먼저 보인다 (synchronizes-with).</text>
    <text x="10" y="284">이 레슨의 실측: relaxed 0.01~1.2 %, 한쪽만 release 또는 acquire 0.0002~0.0013 %, 양쪽 0 % (5. 풀이).</text>
  </g>
</svg>
```

| 패턴 | 깨진 버전 | 고친 버전 | 펌웨어 예 |
|---|---|---|---|
| 메시지 패싱 | payload 쓰고 `flag` relaxed store / relaxed load | release store / acquire load (또는 fence 쌍) | 코어 간 메일박스, DMA 완료 후 프레임 넘기기, 센서 샘플 링 |
| DCLP 지연 초기화 | 일반 포인터, 또는 relaxed atomic 포인터 | 빠른 경로 acquire load + 락 안 release store, 또는 `pthread_once` | CRC 표, 보정 계수 표, 로거·드라이버 싱글턴 |
| 1회 초기화 상태 기계 | `bool inited` 를 검사 후 대입 (검사-후-행동 레이스) | CAS `UNINIT→BUSY` + release `READY` / 실패 시 `UNINIT` | ISR 에서도 부르는 센서 드라이버 `open()`, 부팅 중 늦게 뜨는 주변장치 |

### Part A — 메시지 패싱: 깨짐을 세고, 고친다

- `mp_classify(p, v)` — 받은 payload 판정: 라운드 `v` 로 온전하면 `MP_OK`, 다른 라운드로 온전하면 `MP_STALE`, 필드가 섞였으면 `MP_TORN`. payload 는 `seq` 와 그로부터 계산되는 `a, b, c` 4 필드이고, `v = 0` 이면 전부 0 (아직 아무도 안 쓴 칸).
- `mp_post(s, v)` / `mp_take(s, v, out)` — payload 를 다 쓴 뒤 `flag = v` 게시, `flag == v` 일 때만 복사 (기다리지 않음).
- 스트레스: writer 1 + reader 1, 1024 칸 × 300 라운드 = 307 200 메시지 → 전부 `MP_OK`.
- `demo_mp_stress.c` 로 relaxed / 한쪽만 / 양쪽 · payload 배치 3 가지 · 일반 필드 vs relaxed atomic 필드를 **실측** 하고, TSan 이 무엇을 잡는지 비교한다.

### Part B — DCLP 지연 초기화

- `lazy_get(z)` — 빠른 경로는 락 없이 load 한 번, 처음 한 번만 뮤텍스 + `table_build`. 6 스레드가 3000 라운드 동안 매 라운드 "새 부팅" 상태에서 동시에 불러도 **초기화 정확히 1 번, 모두 같은 완성된 표**.
- `once_get()` — 같은 일을 `pthread_once` 로. (macOS 에는 `<threads.h>` 가 없어 C11 `call_once` 를 못 쓴다 — 3. 시작 코드 참고.)
- `demo_dclp.c` 로 락 없는 naive / 일반 포인터 DCLP / relaxed 포인터 DCLP 가 실제로 깨지는 횟수와 TSan 보고를 본다.

### Part C — 드라이버 1회 초기화 상태 기계 (락 없음, 재시도 가능)

- `drv_try_open(d)` — 절대 기다리지 않는다 (ISR 에서 호출 가능). `READY → DRV_OK`, `BUSY → DRV_EBUSY`, `UNINIT` 이면 CAS 로 `BUSY` 를 차지한 **한 명만** `drv_probe()` 를 부르고, 성공하면 `READY` 게시, 실패하면 `UNINIT` 로 되돌리고 `DRV_EPROBE`.
- `drv_open(d, max_spins)` — 스레드 문맥용: `EBUSY` 동안만 최대 `max_spins` 번 쉬며 재시도.
- 스트레스: 6 스레드(그중 하나는 "ISR" 처럼 `drv_try_open` 만), 2000 라운드, 라운드마다 프로브가 처음 0~3 번 실패 → **프로브 횟수 = 실패 수 + 1 정확히**, 동시 프로브 0 번, `DRV_OK` 를 받은 모두가 완성된 필드를 본다.

## 2. Problem (English)

> **Turn memory-order theory into three production patterns.** Threads model cores; plain data may only be read after a happens-before edge your atomics create.
>
> 1. *Message passing.* `slot_t { payload_t pl; atomic_uint flag; }` with a 4-field payload. Implement `void mp_post(slot_t*, uint32_t v)`, `bool mp_take(slot_t*, uint32_t v, payload_t *out)` (non-blocking), and `mp_result_t mp_classify(const payload_t*, uint32_t v)` returning OK / STALE (self-consistent but from another round) / TORN (fields from different rounds). 307 200 messages must all classify as OK. Then measure how often the relaxed version fails on real hardware.
> 2. *Lazy initialisation.* `const table_t *lazy_get(lazy_t*)` using double-checked locking: lock-free fast path, mutex on first use only, exactly one `table_build` per "boot" even with 6 racing threads. Also `once_get()` with `pthread_once`. Explain why a plain or relaxed pointer is broken even though it "works".
> 3. *Driver one-shot init without a lock.* State machine `UNINIT → BUSY → READY` (failure: `BUSY → UNINIT`). `drv_status_t drv_try_open(drv_t*)` must never wait (callable from an ISR); `drv_open(drv_t*, unsigned max_spins)` waits only while BUSY. A failed probe must be retryable; probes must never overlap.
>
> Follow-ups: Why does TSan report the relaxed version even in runs where nothing broke? Why is `memory_order_relaxed` on the *inner* load of DCLP fine? What do C11 `call_once` and C++ function-local statics give you, and why can't they express "probe failed, try again later" from an ISR? What changes on a single-core Cortex-M?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력, 0.09 초 만에 끝남 — 멈추지 않는다):

```text
FAIL starter.c:261  mp_classify(&p, 7u) == MP_OK
FAIL starter.c:262  mp_classify(&p, 8u) == MP_STALE
FAIL starter.c:263  mp_classify(&z, 8u) == MP_STALE
FAIL starter.c:271  mp_take(&g_slots[0], 5u, &q) && mp_classify(&q, 5u) == MP_OK
FAIL starter.c:273  atomic_load(&g_slots[0].flag) == 5u
SKIP 2 stress checks (Part A): unit checks failed
FAIL starter.c:303  table_ok(t1) && t1 == t2 && t2 == t3 && t1 == &lz.obj
FAIL starter.c:304  atomic_load(&g_table_inits) == 1
FAIL starter.c:308  table_ok(t1) && t1 == t2 && atomic_load(&g_table_inits) == 1
SKIP 3 stress checks (Part B): unit checks failed
FAIL starter.c:327  drv_try_open(&d1) == DRV_OK && drv_fields_ok(&d1)
FAIL starter.c:328  drv_try_open(&d1) == DRV_OK && atomic_load(&d1.probe_calls) == 1
FAIL starter.c:331  drv_open(&d1, 10u) == DRV_EPROBE
FAIL starter.c:332  drv_try_open(&d1) == DRV_OK && atomic_load(&d1.probe_calls) == 3 && drv_fields_ok(&d1)
FAIL starter.c:335  drv_try_open(&d1) == DRV_EBUSY && atomic_load(&d1.probe_calls) == 0
SKIP 3 stress checks (Part C): unit checks failed
7/28 passed
```

하네스는 C-A03 과 같은 구조다: Part 마다 단일 스레드 단위 테스트가 다 통과해야 스트레스가 돈다. 스트레스의 CHECK 는 **올바른 구현이면 매번 성립하는 것** (재배치 0 건, init 1 번, 프로브 = 실패 + 1) 만 본다. 대기 루프에는 전부 상한이 있다.

C11 `call_once` 를 쓰려고 하면 이 맥에서는 이렇게 된다 (실제 출력):

```text
$ cc -std=c11 th.c        # #include <threads.h>
th.c:1:10: fatal error: 'threads.h' file not found
$ ./nt                    # #ifdef __STDC_NO_THREADS__ 검사
__STDC_NO_THREADS__ defined
```

C11 은 `<threads.h>` 를 선택 기능으로 두고, 없으면 `__STDC_NO_THREADS__` 를 정의하라고 한다 (6.10.8.3). Apple libc 는 그 선택을 했다. 그래서 이 레슨은 POSIX `pthread_once` 를 쓴다 (glibc 2.28+ 에는 `<threads.h>` 가 있다 — 문서 기준, 이 레슨에선 미검증).

관찰용 파일 (하네스와 별개):

```bash
cc -std=c11 -O2 -pthread [-DLAYOUT=0|1|2] [-DPLAIN] [-DPUB=memory_order_release] [-DACQ=memory_order_acquire] \
   demo_mp_stress.c -o mp && ./mp                                     # MP 실측 (기본 = 전부 relaxed)
cc -std=c11 -g -O1 -fsanitize=thread -pthread -DLAYOUT=1 -DPLAIN -DROUNDS=20u demo_mp_stress.c -o mp_t && ./mp_t
cc -std=c11 -O2 -pthread -DV=0|1|2|3|4 demo_dclp.c -o dclp && ./dclp   # naive / plain / relaxed / acqrel / once
cc -std=c11 -g -O1 -fsanitize=thread -pthread -DV=1 -DROUNDS=200 demo_dclp.c -o dclp_t && ./dclp_t
cc -std=c11 -O2 -S demo_asm.c -o -                                                          # arm64
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S demo_asm.c -o -  # Cortex-M4
c++ -std=c++20 -O2 -S demo_magic_static.cpp -o -                                            # C++ magic static
```

## 4. 힌트

<details>
<summary>힌트 1 — mp_classify: seq 하나만 믿고 나머지를 다시 계산한다</summary>

`payload_make(p->seq)` 를 만들어 `a, b, c` 와 비교하면 "이 payload 는 한 라운드의 온전한 사본인가" 를 알 수 있다. 온전하면 그다음에 `seq == v` 인지로 OK / STALE 을 가른다. `v = 0` 의 payload 가 전부 0 이 되게 만든 이유가 여기 있다: 아직 안 쓴 칸도 "온전한 옛것(STALE)" 으로 분류된다.
</details>

<details>
<summary>힌트 2 — mp_post / mp_take: 순서는 "데이터 → 플래그", "플래그 → 데이터"</summary>

게시 쪽은 `flag` 의 store 가 release, 받는 쪽은 `flag` 의 load 가 acquire. payload 자체는 일반 대입으로 둔다 — release/acquire 쌍이 생기면 일반 데이터도 함께 보호된다. 둘 중 하나만 바꾸면? 5. 풀이의 표를 보기 전에 예상해 보자.
</details>

<details>
<summary>힌트 3 — DCLP: 바깥 load 와 안쪽 load 는 순서가 다르다</summary>

락 없이 읽는 **바깥** load 는 acquire 여야 한다 (락이 없으니 그것만이 init 스레드의 쓰기와 연결해 준다). 락을 잡은 뒤의 **안쪽** load 는 relaxed 로 충분하다 — 포인터를 게시한 스레드는 같은 락 안에서 썼고, unlock → lock 이 이미 happens-before 를 만든다. 게시 store 는 release.
</details>

<details>
<summary>힌트 4 — 상태 기계: CAS 실패 시 expected 에 들어오는 값을 쓴다</summary>

`atomic_compare_exchange_strong_explicit(&state, &s, BUSY, acquire, acquire)` 가 실패하면 `s` 에 **현재 값** 이 들어온다. 그 사이 다른 누군가 `READY` 로 만들었으면 OK, 아직 `BUSY` 면 EBUSY. 실패 순서도 acquire 인 이유: `READY` 를 봤다면 곧바로 필드를 읽을 것이기 때문이다. 프로브 실패 시 `UNINIT` 로 되돌리는 store 도 release — 다음 프로버가 반쯤 쓴 레지스터 그림자를 이어받는다.
</details>

## 5. 풀이

### 0. 필요한 규칙 두 줄 (자세한 건 C-A03 · MCPP-M13)

- **데이터 레이스** = 다른 스레드의 충돌하는 두 접근 중 하나라도 비원자이고, 어느 쪽도 다른 쪽보다 happens-before 가 아님 → UB (C11 5.1.2.4p25).
- **release store** A 와, A 가 쓴 값을 읽는 **acquire load** B 는 synchronizes-with → A 이전의 모든 쓰기가 B 이후의 읽기보다 happens-before (5.1.2.4p11, 7.17.3). 펜스 쌍도 같은 관계를 만든다 (7.17.4, 연습 1).

이 레슨은 이 두 줄을 **실제 하드웨어에서 깨지는 모습** 과 **패턴별 정답 위치** 로 바꾸는 연습이다.

### Part A — 실측: relaxed 메시지 패싱은 Apple M6 에서 깨지나?

`demo_mp_stress.c` 는 C-A03 의 1칸 채널과 모양이 다르다. 칸이 4096 개이고, reader 는 writer 를 **바로 뒤에서 쫓아간다**: writer 가 칸 i 에 쓰는 순간 reader 는 칸 i 의 flag 를 돌고 있다. 그래서 "flag 는 보였는데 payload 는 아직" 인 창이 매번 생긴다. C-A03 의 채널은 소비자가 FULL 을 볼 때쯤이면 slot 쓰기가 이미 오래전에 끝나 있는 모양이라 창이 거의 없었다.

<!-- code: demo_mp_stress.c -->

기계: Apple M6 (`sysctl hw.nperflevels` = 3: Super 2 + Performance 4 + Efficiency 6 = 12 코어), Apple clang version 21.0.0 (clang-2100.0.123.102). 각 빌드 3 회, 8 192 000 메시지씩 (실제 출력 요약):

| 빌드 (flag 순서 / payload) | 배치 | stale | torn | 깨진 비율 |
|---|---|---|---|---|
| relaxed / relaxed atomic 필드 | `LAYOUT=0` 필드마다 다른 라인 | 914 · 1032 · 991 (1회차 1074) | 66 664 ~ 101 273 | 0.82 ~ 1.25 % |
| relaxed / relaxed atomic 필드 | `LAYOUT=1` payload 한 라인, flag 다른 라인 | 574 · 1464 · 610 | 223 · 459 · 183 | 0.0097 ~ 0.0235 % |
| relaxed / relaxed atomic 필드 | `LAYOUT=2` payload 와 flag 같은 라인 | 934 · 910 · 912 | 3647 · 3870 · 3677 | 0.056 ~ 0.058 % |
| relaxed / **일반 필드** (`-DPLAIN`) | `LAYOUT=1` | 22 665 · 18 377 · 32 532 | 958 · 957 · 1343 | 0.24 ~ 0.41 % |
| **release** store / relaxed load | `LAYOUT=1` | 24 · 23 · 17 | 9 · 8 · 15 | 0.0004 % |
| relaxed store / **acquire** load | `LAYOUT=1` | 9 · 15 · 13 | 5 · 13 · 12 | 0.0002 ~ 0.0003 % |
| release / acquire | `LAYOUT=1` | 0 · 0 · 0 | 0 · 0 · 0 | **0 %** |
| release / acquire, 일반 필드 | `LAYOUT=1` | 0 · 0 · 0 | 0 · 0 · 0 | **0 %** |
| release / acquire | `LAYOUT=0` | 0 · 0 · 0 | 0 · 0 · 0 | **0 %** |

(첫 줄 원문 예: `LAYOUT=0 relaxed-payload slot=320B rounds=8192000 ok=8089653 stale=1074 torn=101273  (bad 1.2494%)`)

읽는 법:

1. **하드웨어 재배치가 실제로 보였다.** 이 패턴에서는 relaxed MP 가 1 만 번에 1 번 ~ 100 번에 1 번 깨진다. "Apple Silicon 은 사실상 안 깨진다" 는 C-A03 의 관찰은 **그 패턴에서** 의 관찰이었다. 리트머스 결과는 모양(타이밍·캐시 라인 배치)에 크게 좌우된다.
2. **양쪽 다 필요하다.** release 만(writer 의 store→store 순서만 보장) 붙이면 reader 의 load→load 재배치가 남고, acquire 만 붙이면 writer 쪽 재배치가 남는다. 둘 다 0 이 아니다 — 즉 Apple M6 은 **양쪽 재배치를 모두 실제로 한다.** ARMv8 은 둘 다 허용한다 (Arm ARM DDI 0487, B2.3 메모리 모델 — 절 번호는 판본마다 다름).
3. **같은 캐시 라인이어도 안전하지 않다** (`LAYOUT=2`: 0.06 %). "flag 와 데이터를 한 라인에 두면 같이 보인다" 는 흔한 오해다. 읽는 쪽 load 끼리의 순서는 주소가 같은 라인이어도 보장되지 않는다 (원인이 어느 쪽 재배치인지는 이 실험으로 구분하지 못했다 — 추정).
4. **일반 필드 버전의 모양이 다르다.** `-DPLAIN` 에서 컴파일러는 payload 4 필드를 16 B 한 번에 썼다 (`stur q0`) 그리고 `ldp` 두 번으로 읽었다 (아래 asm). 그래서 torn 보다 stale 이 많다. 그렇다고 16 B `str q0` 가 다른 코어에게 **한 번에** 보인다는 보장은 C 에도, 이 데모에도 없다 — 이번 실행에서 torn 이 적었을 뿐이다.

컴파일러가 만든 명령 (`cc -O2 -S demo_mp_stress.c -DLAYOUT=1`, 실제 출력에서 발췌):

```text
-DPLAIN (일반 필드)                       기본 (relaxed atomic 필드)
writer:  stur  q0, [x13, #-64]   ; pl     writer:  str w14, [x13]       ; seq
         str   w15, [x13], #128  ; flag            str w14, [x13, #4]   ; a
reader:  ldur  w15, [x0, #64]    ; flag            str w14, [x13, #8]   ; b
         b.ne  LBB0_8                              str w14, [x13, #12]  ; c
         ldp   w15, w16, [x0]    ; pl              str w14, [x13, #64]  ; flag
         ldp   w17, w0, [x0, #8]        reader:  ldur w15, [x0, #64] ; flag → ldr ×4 (pl)
```

relaxed 는 그냥 `str`/`ldr` 이다. 순서를 지키는 명령이 하나도 없으니 CPU 가 바꾸는 게 맞다. 컴파일러가 순서를 바꾸지 않았다는 것도 확인된다 — **재배치는 하드웨어가 했다.**

**TSan 은 무엇을 보나** (`-DLAYOUT=1 -DPLAIN -DROUNDS=20u`, `-g -O1 -fsanitize=thread`, 실제 출력 일부):

```text
WARNING: ThreadSanitizer: data race (pid=68854)
  Read of size 4 at 0x00010371c000 by main thread:
    #0 main demo_mp_stress.c:105 (mp_tp:arm64+0x10000075c)
  Previous write of size 4 at 0x00010371c000 by thread T1:
    #0 writer demo_mp_stress.c:82 (mp_tp:arm64+0x100000928)
  Location is heap block of size 524288 at 0x00010371c000 allocated by main thread:
SUMMARY: ThreadSanitizer: data race demo_mp_stress.c:105 in main
... (a 필드 0x...c008 에 두 번째 보고)
LAYOUT=1 plain slot=128B rounds=81920 ok=81920 stale=0 torn=0  (bad 0.0000%)
ThreadSanitizer: reported 2 warnings          ← 종료 코드 134
```

| 빌드 (TSan) | 실행 결과 | TSan |
|---|---|---|
| 일반 필드 + relaxed flag | **ok=81920, 깨짐 0** | data race 2 건 (105 행 읽기 vs 82 행 쓰기) |
| relaxed atomic 필드 + relaxed flag | ok=81920, 깨짐 0 | **보고 없음**, exit 0 |
| 일반 필드 + release/acquire | ok=81920 | 보고 없음, exit 0 |

- 첫 줄: TSan 아래에서는 실행이 느려져 **재배치가 한 번도 안 일어났는데** 경고가 나왔다. TSan 은 "재배치를 봤다" 가 아니라 "이 두 접근 사이에 happens-before 가 **없다**" 를 본다 (vector clock). 그래서 하드웨어가 협조하지 않아도 잡는다.
- 둘째 줄이 함정이다: payload 를 relaxed atomic 으로 바꾸면 **데이터 레이스는 없어진다** (모두 원자 접근이므로 UB 아님). 그런데 위 표에서 보듯 실제로는 여전히 0.01 % 깨진다. relaxed atomic 은 "찢어진 단일 값이 없음" 만 보장하고 **필드 사이의 순서** 는 보장하지 않는다. TSan 은 UB 만 보므로 침묵한다 — 순서 버그는 리뷰와 스트레스 테스트의 몫이다.

### Part A — 정답 코드와 분류기

```c
static void mp_post(slot_t *s, uint32_t v) {
    s->pl = payload_make(v);                                   /* 1) 데이터 (일반 쓰기) */
    atomic_store_explicit(&s->flag, v, memory_order_release);  /* 2) 게시 */
}
static bool mp_take(slot_t *s, uint32_t v, payload_t *out) {
    if (atomic_load_explicit(&s->flag, memory_order_acquire) != v) return false;
    *out = s->pl;
    return true;
}
```

<!-- code: demo_asm.c -->

`demo_asm.c` 에서 같은 함수가 된 명령 (실제 출력):

| 함수 | arm64 (Apple M6) | Cortex-M4 (thumbv7em) |
|---|---|---|
| `mp_post` | `str q0, [x0]` → `stlr w2, [x8]` | `str` ×4 (c, b, a, seq 순 — 역순!) → `dmb sy` → `str r2, [r0, #64]` |
| `mp_post_relaxed` | `str q0, [x0]` → `str w2, [x0, #64]` | `str` ×4 → `str r2, [r0, #64]` (배리어 없음) |
| `mp_take` | `ldapr w8, [x8]` → `ldr q0, [x0]` | `ldr r3, [r0, #64]` → `dmb sy` → `ldr` ×4 |

- arm64 acquire load 가 `ldar` 가 아니라 `ldapr`(RCpc, FEAT_LRCPC — 이 맥 `hw.optional.arm.FEAT_LRCPC: 1`) 인 이유는 C-A03 SB 리트머스 참고. MP 에는 `ldapr` 로 충분하다.
- Cortex-M4 에서 컴파일러가 payload 4 개를 **거꾸로** 썼다. 일반 쓰기끼리는 컴파일러가 마음대로 순서를 바꾼다는 실물 증거다. release 는 "이 묶음이 전부 flag 보다 먼저" 만 요구하므로 묶음 안 순서는 상관없다.
- 단일 코어 Cortex-M4 (ISR ↔ main) 라면 CPU 가 자기 자신에게 순서를 바꿔 보이는 일은 없으므로 `dmb` 는 사실상 컴파일러 배리어 역할만 한다 (C-M14). 듀얼 코어(M7+M4)나 DMA 마스터가 끼면 `dmb` 가 진짜 일을 한다 — 캐시 유지보수까지 필요한 경우는 C-A02 Part C.

분류기 `mp_classify` 는 `seq` 로 다시 계산한 `a, b, c` 가 다르면 TORN, 같으면 `seq == v` 로 OK/STALE. 스트레스 결과 (실제 출력): `Part A stress: 307200 messages  ok=307200 stale=0 torn=0`. **같은 하네스에 relaxed 를 넣으면** (정답의 두 순서만 relaxed 로 바꾼 사본, 3 회) `stale=14`, `stale=27`, `stale=6` 으로 **세 번 다 FAIL** 했다 — 이 기계에서는 이 스트레스가 relaxed 버그를 잡는다. 그래도 그 CHECK 를 "relaxed 를 막는 테스트" 로 믿으면 안 된다: 다른 기계·부하에서는 0 이 나올 수 있다 (C-A03 이 그랬다). 체크 테스트는 **정답이 항상 통과** 하는 것만 보장한다.

### Part B — DCLP: 왜 "가끔" 덜 만든 객체를 받나

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">일반 포인터 DCLP: 빠른 경로는 락을 안 잡으므로 아무 순서도 받지 못한다</text>
  <text x="10" y="64" fill="currentColor">스레드 A</text>
  <text x="10" y="80" fill="currentColor">(락 안)</text>
  <text x="10" y="184" fill="currentColor">스레드 B</text>
  <text x="10" y="200" fill="currentColor">(빠른 경로)</text>
  <rect x="90" y="50" width="70" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="100" y="72" fill="currentColor" font-family="monospace">lock</text>
  <rect x="170" y="50" width="200" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="178" y="72" fill="currentColor" font-family="monospace">tab[], sum, magic 쓰기</text>
  <rect x="380" y="50" width="140" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="390" y="72" fill="currentColor" font-family="monospace">g_p = &amp;obj</text>
  <rect x="530" y="50" width="80" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="540" y="72" fill="currentColor" font-family="monospace">unlock</text>
  <rect x="400" y="170" width="140" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="408" y="192" fill="currentColor" font-family="monospace">g_p != NULL ✓</text>
  <rect x="550" y="170" width="180" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="558" y="192" fill="currentColor" font-family="monospace">p-&gt;magic == 0 ✗</text>
  <path d="M450 84 L465 170" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M458 161 L465 170 L469 159" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M270 84 C 380 130, 560 120, 630 170" stroke="currentColor" fill="none" stroke-dasharray="5 4"/>
  <text x="560" y="130" fill="currentColor">아직 안 보임</text>
  <g fill="currentColor">
    <text x="10" y="236">B 는 락을 안 잡았다 → A 의 unlock 과 관계없음 → A 의 쓰기 순서를 볼 권리가 없다 (레이스, UB).</text>
    <text x="10" y="258">고침: g_p 를 _Atomic 으로, A 는 release store(stlr), B 의 빠른 경로는 acquire load(ldapr).</text>
    <text x="10" y="280">relaxed 포인터는 포인터 레이스만 없앨 뿐, tab[]/magic 은 여전히 레이스 — 실측 bad 351~651 건.</text>
  </g>
</svg>
```

<!-- code: demo_dclp.c -->

`demo_dclp.c`: 라운드마다 객체와 포인터를 0/NULL 로 되돌리고 ("새 부팅"), 6 워커를 동시에 풀어 0~300 번 빈 루프만큼 도착을 흩뜨린 뒤 `get()` 을 부르고 받은 표를 검사(magic + 합계)한다. 각 3 회 (실제 출력):

| `-DV=` | 구현 | 라운드 | init ≠ 1 인 라운드 | 덜 만든 표를 받은 횟수 | 시간 |
|---|---|---|---|---|---|
| 0 | naive (락 없음) | 200 000 | 122 619 · 123 388 · 121 872 | 8928 · 8464 · 8640 | 0.24~0.33 s |
| 1 | DCLP, 일반 포인터 | 200 000 | 0 · 0 · 0 | **357 · 351 · 514** | 4.8~5.3 s |
| 2 | DCLP, relaxed `_Atomic` 포인터 | 200 000 | 0 · 0 · 0 | **361 · 651 · 378** | 4.6~4.9 s |
| 3 | DCLP, acquire / release | 200 000 | 0 · 0 · 0 | 0 · 0 · 0 | 5.3~11 s |
| 4 | `pthread_once` | 1024 | 0 · 0 · 0 | 0 · 0 · 0 | ≤ 0.09 s |

(V=4 는 라운드마다 정적 `pthread_once_t` 하나가 필요해 1024 라운드만.) 같은 V=1, V=2 를 처음에 1024 라운드 / 도착 흩뜨림 0~4000 으로 돌렸을 때는 **0 건** 이었다. 흩뜨림을 0~300 으로 줄여 도착을 init 완료 직후에 몰고 20 만 라운드로 늘리자 나타났다. 창이 그만큼 좁다 — "우리 제품에서 한 번도 문제없었다" 가 증거가 안 되는 이유다.

- **V=0** 은 순서 문제가 아니라 **검사-후-행동(check-then-act) 레이스**: 두 스레드가 같이 `!g_p` 를 보고 둘 다 init 한다 (60 % 라운드). 펌웨어라면 I2C 초기화 시퀀스가 두 번 나가거나 DMA 채널이 두 번 할당된다.
- **V=1** 의 컴파일 결과 (`-O2 -S`): 컴파일러는 `table_init` 을 인라인하고 표를 상수로 접어 `stur q1, [x28, #4]` … `str w8, [x28]`(magic) → `str x28, [x25, _g_p@PAGEOFF]` 순으로, 즉 **소스 순서대로** 썼다. 그래도 깨졌다 → 하드웨어 재배치. 반대로 컴파일러가 `g_p` 쓰기를 앞당겼어도 C 규칙상 할 말이 없다 (일반 쓰기는 마음대로 옮긴다).
- **V=2** 가 이 레슨의 핵심 함정: `_Atomic` 을 붙였으니 고쳤다고 생각하기 쉽다. relaxed 는 포인터 값 자체만 원자적으로 만들 뿐, 포인터가 가리키는 **내용** 과의 순서를 만들지 않는다.

TSan (V=1, `-DROUNDS=200`, 실제 출력 일부):

```text
WARNING: ThreadSanitizer: data race (pid=75944)
  Write of size 8 at 0x000100908168 by thread T2 (mutexes: write M0):
    #0 worker demo_dclp.c:129 (dt1:arm64+0x100000a1c)
  Previous read of size 8 at 0x000100908168 by thread T5:
    #0 worker demo_dclp.c:129 (dt1:arm64+0x100000990)
  Location is global 'g_p' at 0x000100908168 (dt1+0x100008168)
...
  Read of size 4 at 0x000100908040 by thread T1:
    #0 worker demo_dclp.c:129 (dt1:arm64+0x100000a4c)
  Previous write of size 4 at 0x000100908040 by thread T5 (mutexes: write M0):
  Location is global 'storage' at 0x000100908040 (dt1+0x100008040)
...
V=1 dclp_plain   rounds=200 threads=6  rounds_with_init!=1: 0  bad_reads: 0
ThreadSanitizer: reported 4 warnings          ← exit 134
```

V=2 는 `g_p` 보고가 사라지고 `'storage'` 보고만 3 건 (`reported 3 warnings`, exit 134) — 위 "포인터는 OK, 내용은 레이스" 와 정확히 같다. V=3, V=4 는 보고 없음, exit 0. 129 행은 `get()` 이 인라인된 호출 위치 `if (!table_ok(get()))` 다. 여기서도 TSan 실행은 `bad_reads: 0` — 재배치는 없었지만 규칙 위반은 잡았다. "Write … (mutexes: write M0)" 와 "Previous read" 에 뮤텍스가 **없는** 것이 DCLP 레이스의 지문이다: 한쪽만 락을 잡았다.

**고친 DCLP** 와 각 순서의 이유:

```c
table_t *p = atomic_load_explicit(&z->p, memory_order_acquire);  /* ① 빠른 경로 */
if (p) return p;
pthread_mutex_lock(&z->mu);
p = atomic_load_explicit(&z->p, memory_order_relaxed);           /* ② 락 안 */
if (!p) { table_build(&z->obj); p = &z->obj;
          atomic_store_explicit(&z->p, p, memory_order_release); } /* ③ 게시 */
pthread_mutex_unlock(&z->mu);
```

| 위치 | 순서 | 이유 |
|---|---|---|
| ① 빠른 경로 load | acquire | 락이 없으니 ③ 과 synchronizes-with 를 만드는 유일한 길. arm64 `ldapr x28, [x20]` (V=3 asm) |
| ② 락 안 load | relaxed | 값을 쓴 쪽(③)도 같은 락 안. unlock → lock 이 이미 happens-before |
| ③ 게시 store | release | `table_build` 의 모든 쓰기 → 포인터. arm64 `stlr x24, [x20]` |
| 뮤텍스 | — | "init 은 한 명만" (V=0 의 중복 init 방지). 순서는 ①③ 이 담당 |

빠른 경로 비용은 load 하나 (`ldapr` 는 `ldr` 과 거의 같은 비용 — 측정은 LLAT-B06). 이게 DCLP 를 쓰는 이유다: 매 호출 뮤텍스를 잡는 단순한 버전도 **맞지만**, 핫 패스(ISR 직후 처리, 패킷마다 부르는 CRC)에서 매번 락은 비싸다.

**`pthread_once` · C11 `call_once` · C++ magic static**

| 방법 | 보장 | 실패한 init | 기다림 | 이 맥 |
|---|---|---|---|---|
| `pthread_once(&o, f)` | 처음 호출자만 `f()`, 나머지는 `f` 가 끝날 때까지 대기. 반환 시 `f` 완료 (POSIX) | `f` 는 `void` — 오류를 돌려줄 방법이 없다 (연습 3) | 블로킹 → ISR 불가 | 있음. `-O2` 에서 `b _pthread_once` 로 **매번 함수 호출** (인라인 빠른 경로 없음) |
| C11 `call_once(&flag, f)` (7.26.2.1) | 같은 의미, `once_flag` / `ONCE_FLAG_INIT` | 같음 | 블로킹 | **없음** (`__STDC_NO_THREADS__`) |
| C++11 함수 안 `static T t;` ([stmt.dcl]/4) | 동시에 들어오면 init 완료까지 대기. 생성자가 예외로 빠지면 **다음 호출에서 다시 시도** | 예외로 재시도 가능 | 블로킹 | 있음 (아래 asm) |

<!-- code: demo_magic_static.cpp -->

`demo_magic_static.cpp` 를 `c++ -std=c++20 -O2 -S` 한 결과 (실제 출력 발췌):

```text
__Z5tablev:
	adrp	x8, __MergedGlobals@PAGE
	add	x8, x8, __MergedGlobals@PAGEOFF
	ldaprb	w8, [x8]            ; guard 바이트 acquire load = 우리가 손으로 쓴 ① 과 같은 빠른 경로
	tbz	w8, #0, LBB2_2       ; 아직이면 cold 경로로
	...                          ; 이미 됐으면 &t 반환
__Z5tablev.cold.1:
	bl	___cxa_guard_acquire  ; 락 + 재검사 (② 에 해당)
	...                          ; 생성자 (인라인)
	b	___cxa_guard_release  ; 게시 (③ 에 해당) + 대기자 깨우기
```

컴파일러가 만드는 것이 정확히 DCLP 다: acquire 빠른 경로 + 런타임 라이브러리의 락. C++ 에서 직접 DCLP 를 쓸 일은 거의 없고 함수 안 static 이 정답인 이유다 (C++ 쪽 자세한 건 CPP-A04 · MCPP-M13). 단, `-fno-threadsafe-statics` 로 빌드하면 이 보호가 빠진다 — 일부 임베디드 C++ 빌드가 코드 크기 때문에 끈다 (문서 기준; 그 빌드는 이 레슨에서 확인하지 않았다).

### Part C — 락이 없는 곳의 1회 초기화: 상태 기계

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">드라이버 1회 초기화: 한 명만 BUSY 를 차지하고, 결과를 release 로 넘긴다</text>
  <rect x="40" y="90" width="130" height="50" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="70" y="120" fill="currentColor" font-family="monospace">UNINIT</text>
  <rect x="300" y="90" width="130" height="50" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="340" y="120" fill="currentColor" font-family="monospace">BUSY</text>
  <rect x="560" y="90" width="130" height="50" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="595" y="120" fill="currentColor" font-family="monospace">READY</text>
  <path d="M170 105 L300 105" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M290 99 L300 105 L290 111" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="178" y="80" fill="currentColor">CAS 성공 (acquire)</text>
  <text x="178" y="96" fill="currentColor">→ 이 호출자만 프로브</text>
  <path d="M430 115 L560 115" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M550 109 L560 115 L550 121" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="438" y="100" fill="currentColor">프로브 OK</text>
  <text x="438" y="138" fill="currentColor">store (release)</text>
  <path d="M365 140 C 330 200, 140 200, 105 140" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M101 151 L105 140 L113 149" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="150" y="196" fill="currentColor">프로브 실패: store UNINIT (release) → 다음 호출자가 재시도</text>
  <g fill="currentColor">
    <text x="10" y="232">BUSY 를 본 호출자: drv_try_open → 즉시 DRV_EBUSY (ISR 안전) / drv_open → max_spins 까지 쉬며 재시도.</text>
    <text x="10" y="252">READY 를 본 호출자: acquire load 였으므로 chip_id · calib[] 를 바로 읽어도 된다.</text>
    <text x="10" y="272">pthread_once 와 다른 점: 기다리지 않을 수 있고, 실패한 init 을 다시 시도할 수 있다.</text>
  </g>
</svg>
```

왜 이런 게 필요한가 — 펌웨어 현실:

- **ISR 에서 첫 호출**: 센서 데이터 준비 인터럽트가 드라이버 `open` 보다 먼저 올 수 있다. ISR 은 뮤텍스를 잡을 수 없다 (FreeRTOS 뮤텍스는 ISR 에서 못 쓴다 — RTOS 트랙). `pthread_once` / `call_once` / magic static 은 전부 **블로킹** 이다.
- **프로브 실패가 정상**: 전원 레일이 아직 안정되지 않은 센서, 핫플러그 장치. 실패하면 나중에 다시 해야 한다. `pthread_once` 는 `init_routine` 이 `void` 라 실패를 표현할 수 없고, 한 번 불리면 다시 부르지 않는다 (연습 3 에서 실측: 하드웨어가 살아나도 영원히 `-5`).
- **멀티 코어 부팅**: 스케줄러 시작 전 두 코어가 같은 주변장치를 건드리는 순간. 락 대신 원자 상태 하나로 "한 명만" 을 보장한다.

```c
static drv_status_t drv_try_open(drv_t *d) {
    unsigned s = atomic_load_explicit(&d->state, memory_order_acquire);
    if (s == DRV_READY) return DRV_OK;
    if (s == DRV_BUSY) return DRV_EBUSY;
    if (!atomic_compare_exchange_strong_explicit(&d->state, &s, DRV_BUSY,
                                                 memory_order_acquire, memory_order_acquire))
        return s == DRV_READY ? DRV_OK : DRV_EBUSY;
    if (drv_probe(d) == 0) { atomic_store_explicit(&d->state, DRV_READY, memory_order_release); return DRV_OK; }
    atomic_store_explicit(&d->state, DRV_UNINIT, memory_order_release);
    return DRV_EPROBE;
}
```

| 연산 | 순서 | 빼면 |
|---|---|---|
| 첫 load | acquire | `READY` 를 보고 곧장 `chip_id` 를 읽는데 프로브 쓰기가 안 보일 수 있다 |
| CAS 성공 | acquire | 지난 실패자가 남긴 필드 쓰기(release `UNINIT`)와 연결이 끊긴다 → 다음 프로버의 쓰기와 레이스 |
| CAS 실패 | acquire | 실패 순간 `READY` 를 봤을 때 (`s == READY`) 필드를 읽을 권리가 없어진다 |
| `READY` store | release | 프로브 결과 게시 — Part A 와 같은 MP |
| `UNINIT` store | release | 반쯤 쓴 필드를 다음 프로버에게 넘김 (반납 방향, C-A03 의 recv 쪽 release 와 같은 역할) |

`strong` 을 쓴 이유: 실패하면 바로 `EBUSY` 를 돌려주는 1회 시도라서, `weak` 의 가짜 실패(spurious failure)가 "아무도 BUSY 가 아닌데 EBUSY" 라는 거짓 응답이 된다. 루프 안에서 다시 시도하는 CAS 라면 `weak` 가 맞다 (C-A03 Q5).

스트레스 결과 (실제 출력): `Part C stress: 2000 rounds x 6 threads  bad_probe_count=0 overlaps=0 bad=0 isr_ebusy=12643` — "ISR" 스레드는 1 만 번 넘게 `EBUSY` 를 받고 **한 번도 기다리지 않았다.** `isr_ebusy` 값은 실행마다 다르다 (TSan 실행에서는 4142) — 그래서 CHECK 하지 않고 출력만 한다.

Cortex-M4 에서 CAS 는 이렇게 된다 (`demo_asm.c` 의 `drv_claim`, 실제 출력):

```text
.LBB4_1:  ldrex  r0, [r1]        ; 현재 state
          cbnz   r0, .LBB4_4     ; UNINIT 아니면 실패 경로
          movs   r0, #1
          strex  r2, r0, [r1]    ; BUSY 쓰기 시도
          cmp    r2, #0
          bne    .LBB4_1         ; 모니터를 잃었으면 (ISR 이 끼어들었으면) 다시
          dmb    sy              ; acquire
          bx     lr
.LBB4_4:  movs   r0, #0
          clrex                  ; 쓰지 않을 독점 예약 해제
          dmb    sy
```

arm64 는 `casa w8, w9, [x0]` 한 줄 (LSE, acquire). LDREX/STREX 사이에 예외가 나면 모니터가 지워져 STREX 가 실패하고 재시도한다 — 그 실물은 ARM-M11, 명령 자체는 ARM-M04. Cortex-M0/M0+ (ARMv6-M) 는 LDREX/STREX 가 없어서 이 CAS 를 PRIMASK 임계 구역으로 만들어야 한다 (CPP-A04).

### 정리: 패턴별 "어디에 무엇을"

| 패턴 | 게시하는 쪽 | 받는 쪽 | 함정 |
|---|---|---|---|
| 메시지 패싱 | 데이터 → flag **release** | flag **acquire** → 데이터 | 한쪽만 붙이면 여전히 깨진다 (실측). payload 를 relaxed atomic 으로 바꿔도 순서는 안 생긴다 |
| DCLP | 락 안: 만들고 → 포인터 **release** | 빠른 경로 포인터 **acquire** | relaxed 포인터 = 포인터만 안전, 내용은 레이스 |
| `pthread_once` / magic static | 라이브러리가 대신 | 라이브러리가 대신 | 블로킹, (C) 실패 재시도 불가 |
| 1회 초기화 상태 기계 | CAS 로 차지, 결과 **release** | 상태 **acquire** | 1회 시도 CAS 에 `weak` 를 쓰면 거짓 EBUSY |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_san && ./so_san
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_t && ./so_t
```

실제 출력 (`-O2`, 0.18 초):

```text
Part A stress: 307200 messages  ok=307200 stale=0 torn=0
Part B stress (DCLP): 3000 rounds x 6 threads  rounds_with_init!=1=0 bad_reads=0
Part B stress (pthread_once): 63 rounds x 6 threads  rounds_with_init!=1=0 bad_reads=0
Part C stress: 2000 rounds x 6 threads  bad_probe_count=0 overlaps=0 bad=0 isr_ebusy=12643
28/28 passed
```

5 회 연속 `28/28 passed`. ASan+UBSan `28/28 passed` (exit 0), TSan 보고 없음 (exit 0).

## 7. 자주 하는 실수

1. **"flag 를 `_Atomic` 으로 했으니 됐다"** — 기본 `atomic_store`/`atomic_load` 는 seq_cst 라 맞지만, "성능 때문에" `_explicit(..., relaxed)` 로 바꾸는 순간 깨진다. relaxed 는 그 변수 하나의 원자성만 준다. → 게시 store 는 release, 확인 load 는 acquire.
2. **한쪽만 고치기** — "writer 에 release 를 붙였으니 reader 는 relaxed 로" (또는 반대). 실측 0.0002~0.0013 % 로 여전히 깨졌다. synchronizes-with 는 **쌍** 이다.
3. **같은 캐시 라인이면 안전하다는 믿음** — `LAYOUT=2` 0.06 %. 아키텍처 규칙에 캐시 라인은 나오지 않는다.
4. **DCLP 에서 포인터만 relaxed atomic** — 포인터는 찢어지지 않지만 `p->tab[]` 은 레이스 (V=2 실측 361~651 건, TSan `'storage'` 3 건). → 빠른 경로 acquire, 게시 release.
5. **DCLP 의 안쪽 검사를 빼기** — 락을 잡은 뒤 다시 확인하지 않으면 바깥에서 같이 NULL 을 본 두 스레드가 차례로 init 한다 (V=0 과 같은 중복 init). → 락 안에서 한 번 더 load.
6. **`bool inited` 플래그 + 나중에 대입** — `if (!inited) { init(); inited = true; }` 는 순서 문제 이전에 검사-후-행동 레이스다. 원자 RMW(CAS, `atomic_flag_test_and_set`) 로 "차지" 해야 한 명만 들어간다 → Part C.
7. **ISR 에서 `pthread_once` / 뮤텍스 DCLP 를 호출** — 다른 코어(또는 선점된 태스크)가 init 중이면 ISR 이 블록된다. 단일 코어에서 init 중인 main 을 ISR 이 선점하면 **영원히** 기다린다 (main 은 ISR 이 끝나야 다시 돈다). → 기다리지 않는 `try_open` 이 `EBUSY` 를 돌려주고 ISR 은 그 샘플을 버리거나 나중에 처리.
8. **TSan 이 조용하니 순서도 맞다** — relaxed atomic payload 는 TSan 보고 0 건인데 실측 0.01 % 깨졌다. 반대로 fence 쌍은 맞는데 TSan 이 거짓 경고를 낸다 (연습 1). TSan 은 UB 검사기지 순서 검증기가 아니다.

## 8. 면접 Q&A

<details>
<summary>Q1. 이 메일박스 코드는 x86 에서 몇 년 동안 문제없었는데, ARM 보드로 옮기니 가끔 깨진 값을 읽습니다. 왜죠?</summary>

x86(TSO)은 store→store, load→load 순서를 하드웨어가 지켜 준다. 그래서 relaxed(= 일반 `mov`) 로도 MP 가 우연히 맞는다 (컴파일러가 순서를 안 바꾸는 한). ARMv8·ARMv7-A·RISC-V(RVWMO) 는 두 재배치를 다 허용하고, Apple M6 에서는 이 레슨의 패턴으로 relaxed 가 0.01~1 % 깨졌다. 코드는 처음부터 C11 규칙 위반(데이터 레이스)이었고 x86 이 숨겨 줬을 뿐이다. 고침: 게시 store release, 확인 load acquire. 꼬리: "x86 에서는 release/acquire 가 공짜냐" — 대부분 일반 `mov` 로 내려가 명령 비용은 거의 없고, 컴파일러 재배치만 막는다 (x86 의 seq_cst store 는 `xchg`/`mfence` — 문서 기준, 이 레슨에서 x86 asm 은 확인하지 않음).
</details>

<details>
<summary>Q2. 스트레스 테스트에서 relaxed 버전도 한 번도 안 깨졌습니다. 그럼 relaxed 써도 되나요?</summary>

안 된다. (1) C11 에서는 데이터 레이스 = UB 라 다음 컴파일러 버전이 store 를 앞당겨도 할 말이 없다. (2) 관찰 빈도는 패턴·타이밍에 극단적으로 좌우된다: C-A03 의 1칸 채널은 60만 개 동안 0, 이 레슨의 "바로 뒤에서 쫓는" 링은 1 % — 같은 기계, 같은 relaxed. DCLP 는 1024 라운드에서 0 건, 20 만 라운드 + 도착 시각 몰기에서야 수백 건. (3) 다른 코어(M7 + 캐시, 다른 SoC)에서는 창이 넓어질 수 있다. 검증은 **모델 기준** 으로: 리뷰에서 happens-before 경로를 그려 보고, TSan 으로 위반을 찾고, 스트레스는 보조로만.
</details>

<details>
<summary>Q3. TSan 은 실제로 아무것도 안 깨진 실행에서 왜 경고를 내죠? 반대로 못 잡는 경우는?</summary>

TSan 은 접근마다 vector clock 을 기록하고 "두 접근 사이에 happens-before 가 있나" 를 본다. 하드웨어가 실제로 재배치했는지는 보지 않는다 — 그래서 느려진 실행(이 레슨: 깨짐 0) 에서도 MP·DCLP 레이스를 잡았다. 못 잡는 것: (1) 모두 원자 접근인 순서 버그 — payload 를 relaxed atomic 으로 만들면 UB 가 아니므로 침묵 (실측 0.01 % 깨짐), (2) 실행되지 않은 경로, (3) 계측되지 않은 코드 (손 asm, 미계측 라이브러리). 거꾸로 **거짓 양성** 도 있다: 단독 `atomic_thread_fence` 는 동기화로 모델링하지 않아 맞는 fence 코드에 경고가 난다 (연습 1 에서 실제 확인).
</details>

<details>
<summary>Q4. DCLP 가 왜 "깨진 패턴" 으로 유명했고, 지금은 왜 괜찮다고 하나요?</summary>

C++03/C99 에는 스레드와 메모리 모델이 없어서, 일반 포인터 DCLP 는 컴파일러·CPU 가 "포인터 쓰기를 객체 생성보다 먼저 보이게" 하는 것을 막을 수단이 없었다 (Meyers & Alexandrescu 2004). `volatile` 도 답이 아니었다 (컴파일러 재배치 일부만 막고 CPU 순서·다른 일반 쓰기와의 순서는 못 막는다, C-A02). C11/C++11 이 원자 타입과 acquire/release 를 주면서 "빠른 경로 acquire load + 게시 release store" 로 **표준 안에서** 올바르게 쓸 수 있게 됐다. 그래도 직접 짜기보다 `pthread_once`/`call_once`/C++ 함수 안 static 이 우선이고, 컴파일러가 만드는 magic static 이 정확히 그 DCLP 다 (`ldaprb` + `__cxa_guard_acquire`).
</details>

<details>
<summary>Q5. DCLP 의 락 안쪽 load 는 왜 relaxed 로 충분하죠? 바깥 load 를 relaxed 로 하면?</summary>

안쪽: 포인터를 게시한 스레드는 같은 뮤텍스 안에서 썼고, 그 unlock 이 지금 내 lock 과 synchronizes-with 다 (POSIX 뮤텍스 = acquire/release). 그래서 안쪽 load 가 non-NULL 을 보면 객체 내용도 이미 보인다. 바깥: 락을 안 잡았으니 그런 연결이 하나도 없다 — relaxed 로 하면 V=2 처럼 포인터는 봤는데 내용이 덜 보일 수 있다 (실측 361~651 건/20 만 라운드). 게시 store 는 release 여야 바깥 acquire 와 짝이 된다. 꼬리: "acquire 대신 consume 은?" — `memory_order_consume` 은 컴파일러들이 사실상 acquire 로 승격해 구현한다 (C++ 쪽은 C++26 에서 deprecated 로 정리됨 — 문서 기준, 미검증).
</details>

<details>
<summary>Q6. 드라이버 init 을 ISR 에서도 부를 수 있어야 합니다. pthread_once 나 뮤텍스 DCLP 를 쓰면 안 되는 이유와 대안은?</summary>

둘 다 init 중이면 **기다린다**. ISR 은 기다릴 수 없다 — 특히 단일 코어에서 init 중인 main 을 ISR 이 선점하면 main 이 다시 돌 수 없어 교착이다. 대안은 Part C 의 상태 기계: CAS 로 `UNINIT→BUSY` 를 차지한 한 명만 프로브, 나머지(ISR)는 즉시 `EBUSY` 를 받고 그 샘플을 버리거나 플래그만 남기고 나간다. `READY` 는 release 로 게시, 확인은 acquire. 추가 이점: 프로브 실패 시 `UNINIT` 로 되돌려 재시도 가능 — `pthread_once` 는 `void` init 이라 실패를 표현할 수 없다 (연습 3). 꼬리: 단일 코어 Cortex-M 이면 CAS 대신 PRIMASK 로 짧게 막아도 된다 (C-M14). 다만 프로브(I2C 수 ms) 전체를 인터럽트 금지로 감싸면 안 된다 — 상태 전이만 막는다.
</details>

<details>
<summary>Q7. 이 상태 기계에서 CAS 를 weak 로 바꾸면 무엇이 달라지나요?</summary>

`compare_exchange_weak` 는 값이 기대값과 같아도 가짜로 실패할 수 있다 (LL/SC 아키텍처에서 STREX 가 예약을 잃는 경우 — C11 7.17.7.4). 루프 안 CAS 라면 다시 돌면 그만이지만, 여기 `drv_try_open` 은 1회 시도라서 가짜 실패가 "아무도 BUSY 가 아닌데 EBUSY" 라는 거짓 응답이 된다. ISR 은 그 샘플을 버릴 테니 데이터 손실. 그래서 1회 시도 CAS 는 `strong`. 꼬리: Cortex-M4 `strong` CAS 가 내부에 `bne .LBB4_1` 재시도 루프를 가진 걸 실제 asm 에서 볼 수 있다 — strong 은 "가짜 실패를 라이브러리가 대신 재시도" 일 뿐이다.
</details>

<details>
<summary>Q8. 메일박스 flag 를 acquire 로 읽으면 매번 비싸지 않나요? 폴링 루프를 더 싸게 할 수는?</summary>

arm64 에서 acquire load 는 `ldapr` 한 명령이라 `ldr` 과 큰 차이가 없다 (정량은 LLAT-B06). Cortex-M 에서는 `ldr` + `dmb sy` 라 루프마다 배리어가 돈다. 그 경우 **relaxed 로 폴링하다가 값을 본 뒤 한 번만 `atomic_thread_fence(memory_order_acquire)`** 를 넣는다 (연습 1). 7.17.4 의 fence 규칙: writer 의 release fence(또는 release store)와 reader 의 acquire fence 사이에, 같은 원자 객체에 대해 "fence 뒤 store / fence 앞 load 가 그 값을 읽음" 이 있으면 synchronizes-with. arm64 에서 실제로 acquire fence 는 `dmb ishld`, release fence 는 `dmb ish` 로 나왔다. 규칙의 형식적 정리는 MCPP-M13.
</details>

## 9. 추가 연습

### 연습 1 — fence 버전 메시지 패싱

`mp_post_fence` 는 payload 쓰기 → `atomic_thread_fence(release)` → relaxed store, `mp_take_fence` 는 relaxed load 로 폴링 → 맞는 값이면 `atomic_thread_fence(acquire)` → payload 읽기. 512 칸 × 200 라운드가 전부 온전해야 한다.

<details>
<summary>답 보기</summary>

7.17.4p2~4 의 fence–fence 동기화: release fence F1 이 store X 보다 앞서고(sequenced-before), acquire fence F2 가 load Y 보다 뒤에 있으며, Y 가 X 가 쓴 값을 읽으면 F1 synchronizes-with F2. 이점은 **폴링 중엔 배리어가 없고** 성공했을 때만 한 번. arm64 에서 `practice.c -O2 -S` 결과 release fence = `dmb ish`, acquire fence = `dmb ishld` (실제 출력). 주의: TSan 은 단독 fence 를 동기화로 보지 않는다. `-DFORCE_FENCE -fsanitize=thread` 로 fence 버전을 TSan 에 돌리면 맞는 코드인데도 다음이 나온다 (실제 출력 일부):

```text
WARNING: ThreadSanitizer: data race (pid=75538)
  Read of size 4 at 0x000104d00080 by main thread:
    #0 main practice.c:171 (pr_tf:arm64+0x1000007d4)
  Previous write of size 8 at 0x000104d00080 by thread T1:
    #0 w1 practice.c:81 (pr_tf:arm64+0x1000010b4)
SUMMARY: ThreadSanitizer: data race practice.c:171 in main
...
9/9 passed
ThreadSanitizer: reported 2 warnings
```

그래서 `practice.c` 는 TSan 빌드에서만 같은 의미의 release store / acquire load 로 바꿔 검증한다 (`__has_feature(thread_sanitizer)`). GCC 는 이 경우 `-Wtsan` 경고를 낸다 (문서 기준, 이 맥엔 GCC 없음).
</details>

### 연습 2 — CAS 경주 지연 초기화 (락 없음)

`lazy_cas_get(pp)`: acquire load 로 있으면 반환. 없으면 각자 객체를 **만들고**, `CAS(NULL → mine)` 에 이긴 한 명만 게시, 진 쪽은 자기 것을 `free` 하고 승자 것을 반환. 6 스레드 × 2000 라운드: 라운드마다 `만든 수 − 버린 수 == 1`, 모두 같은 완성된 객체.

<details>
<summary>답 보기</summary>

CAS 성공 순서 release (내 객체 내용 → 포인터), 실패 순서 acquire (실패 시 `expected` 에 들어온 **승자** 포인터의 내용을 읽기 때문). 장점: 락이 없고 기다리지 않는다 (ISR 에서도 가능 — 단 ISR 에서 malloc 은 안 되니 미리 할당한 풀에서). 단점: init 이 여러 번 **실행될 수 있다** — 부작용 없는(멱등) init 에만 쓴다. 하드웨어를 건드리는 프로브에는 안 된다 → 그건 Part C. Linux 커널의 `cmpxchg` 기반 지연 할당이 이 모양이다 (문서 기준).
</details>

### 연습 3 — pthread_once 로 실패할 수 있는 초기화를 감싸면

`init3` 은 프로브 결과를 `g_err3` 에 저장한다. 첫 프로브만 실패(-5)하도록 만들고 `cfg_once_get` 을 세 번 부른다.

<details>
<summary>답 보기</summary>

세 번 모두 `-5`, 프로브는 단 1 번 (`g_probe_calls3 == 1`) — 실제 실행 결과. `pthread_once` 는 `init_routine` 이 "끝났다" 만 알고 "성공했다" 는 모른다. 한 번 끝난 once 는 되돌릴 수 없다 (once 객체를 다시 `PTHREAD_ONCE_INIT` 으로 덮어쓰는 건 POSIX 가 정의하지 않는다). C11 `call_once` 도 같다. 그래서 "오류 시 재시도" 가 요구 사항이면 Part C 상태 기계를 쓴다. C++ 함수 안 static 은 생성자가 **예외** 로 빠지면 다음 호출에서 재시도하지만, `-fno-exceptions` 펌웨어 빌드에서는 그 길도 없다.
</details>

### 연습 4 — stop 플래그는 relaxed, done+결과는 release/acquire

워커는 `g_stop4` 를 relaxed 로 보며 돌다가 멈추면 `g_result4`(일반 변수)에 횟수를 쓰고 `g_done4` 를 release 로 세운다. main 은 진행 카운터가 10 만을 넘으면 stop 을 relaxed 로 세우고, done 을 acquire 로 기다린 뒤 결과를 읽는다. `결과 == 진행 카운터 최종값` 이어야 한다.

<details>
<summary>답 보기</summary>

stop 플래그는 **데이터를 함께 넘기지 않는다** — "언젠가 멈춰" 라는 뜻뿐이고, 늦게 보여도 몇 번 더 돌 뿐이다. C11 은 원자 store 가 "합리적인 시간 안에" 보이게 하라고 권고한다 (7.17.3p16 — should, 보장 아님). 반면 done 은 `g_result4` 를 넘기므로 MP 그대로 release/acquire. `res == prog` 는 매번 성립한다: 워커의 마지막 `fetch_add` 가 done release 보다 앞서고, main 의 load 는 done acquire 뒤라서 쓰기-읽기 일관성(write-read coherence)에 의해 마지막 값 이상을 본다. "relaxed 로 충분한 곳" 을 고를 수 있는 것이 이 레슨의 다른 절반이다: 통계 카운터(C-A03), stop/요청 플래그, 진단용 워터마크.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_t && ./pr_t                  # 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread -DFORCE_FENCE practice.c -o pr_tf && ./pr_tf  # fence 거짓 양성 2 건
```

실제 출력: `9/9 passed` (3 회, ASan+UBSan 빌드 포함, TSan 빌드 보고 없음).

## 10. 핵심 정리

- MP 는 **쌍**: 데이터 → release store, acquire load → 데이터. Apple M6 실측 — relaxed 0.01~1.2 %, 한쪽만 0.0002~0.0013 %, 양쪽 0 %.
- 리트머스 빈도는 모양이 정한다. "안 깨졌다" (C-A03 1칸 채널 60만 개 0 건) ≠ "맞다". 같은 캐시 라인도 보호가 아니다.
- TSan = **모델 위반 검사**. 재배치 0 건인 실행에서도 잡고, 모두 원자인 순서 버그는 못 잡고, 단독 fence 는 거짓 양성.
- DCLP: 빠른 경로 **acquire**, 락 안 재검사 **relaxed**, 게시 **release**. relaxed 포인터는 포인터만 고친다 (실측 수백 건/20 만 라운드).
- `pthread_once` / C11 `call_once`(macOS 없음, `__STDC_NO_THREADS__`) / C++ magic static(`ldaprb` + `__cxa_guard_*`) = 컴파일러·라이브러리가 짜 주는 DCLP. 전부 블로킹, (C 둘은) 실패 재시도 불가.
- ISR·멀티 코어 부팅의 1회 초기화 = CAS `UNINIT→BUSY` + release `READY` / 실패 시 release `UNINIT`. 1회 시도 CAS 는 `strong`.
- relaxed 가 맞는 곳도 있다: 데이터를 함께 넘기지 않는 stop 플래그, 통계 카운터.

## 참고자료

- C11 (N1570): 5.1.2.4 Multi-threaded executions and data races (p11 synchronizes-with, p25 데이터 레이스), 6.10.8.3 `__STDC_NO_THREADS__`, 7.17.3 Order and consistency, 7.17.4 Fences, 7.17.7.4 compare_exchange (weak 의 가짜 실패), 7.26.2.1 `call_once` — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- C++ [stmt.dcl]/4 (함수 안 static 의 동시 초기화, 예외 시 재시도), [atomics.fences] — <https://eel.is/c++draft/stmt.dcl>
- POSIX.1-2017 `pthread_once`, XBD 4.12 Memory Synchronization — <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_once.html>
- Scott Meyers, Andrei Alexandrescu, "C++ and the Perils of Double-Checked Locking" (DDJ, 2004) — <https://www.aristeia.com/Papers/DDJ_Jul_Aug_2004_revised.pdf>
- Jeff Preshing, "Double-Checked Locking is Fixed In C++11" — <https://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11/>
- Hans-J. Boehm, "Threads Cannot Be Implemented as a Library" (PLDI 2005)
- Clang ThreadSanitizer 문서: <https://clang.llvm.org/docs/ThreadSanitizer.html>
- Arm Architecture Reference Manual for A-profile (DDI 0487) — B2.3 메모리 모델, LDAPR(FEAT_LRCPC), CASA(LSE): <https://developer.arm.com/documentation/ddi0487/latest/>
- ARMv7-M Architecture Reference Manual (DDI 0403) — A3.4 배타적 접근(LDREX/STREX/CLREX), A3.7 메모리 배리어: <https://developer.arm.com/documentation/ddi0403/latest/>
- Linux kernel `Documentation/memory-barriers.txt` (MP 패턴, `smp_store_release`/`smp_load_acquire`): <https://www.kernel.org/doc/Documentation/memory-barriers.txt>
- 관련 레슨: C-A03 (memory_order 기초, 1칸 채널, SB 리트머스), C-A02 (컴파일러 vs CPU 재배치, DMA 디스크립터), C-M14 (ISR ↔ main, 단일 코어), C-B09 (volatile), TOOL-M09 (TSan 읽기, `pthread_once` 첫 소개), OS-B04/OS-B05 (카운터 레이스·뮤텍스), CPP-A04 (Cortex-M lock-free · PRIMASK), MCPP-M13 (fence·release sequence 규칙), ARM-M04 (LDXR/STXR·LSE), ARM-M05 (MP 리트머스 asm, DMB/DSB/ISB), ARM-M11 (Cortex-M 모니터와 예외), LLAT-B06 (acquire/release 비용), ARCH-A05 (하드웨어 리트머스), RTOS 트랙 (ISR 에서 쓸 수 있는 API)
