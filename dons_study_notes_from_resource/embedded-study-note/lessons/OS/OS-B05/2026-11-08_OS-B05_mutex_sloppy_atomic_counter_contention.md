---
id: OS-B05
time: 70분
updated: 2026-11-08
---
# OS-B05 뮤텍스: 레이스 해결과 경합 비용 측정

## 학습 목표

- OS-B04 의 깨지는 공유 카운터를 **전역 뮤텍스**, **atomic `fetch_add`**, **sloppy counter**(OSTEP 29장) 세 가지로 고치고, 각각이 왜 맞는지 한 문장으로 말할 수 있다.
- 스레드 1~8 에서 처리량을 직접 재고 (5회 중앙값), "스레드를 늘렸더니 **더 느려졌다**" 를 캐시 라인 이동·커널 대기·락 convoy 로 설명한다.
- **락 범위(scope)**: 검사와 갱신을 한 임계 구역에 넣지 않으면 데이터 레이스가 없어도 틀린다는 것(race condition ≠ data race)을 강제 스케줄로 재현하고, TSan 이 그걸 못 잡는 이유를 안다.
- **락 세분성(granularity)**: 락을 잘게 쪼개거나(sloppy, 스트라이핑) 덜 자주 잡는(batch) 설계와 그 대가(근사값, 락 순서 규칙)를 측정값으로 비교한다.
- 펌웨어의 같은 문제 — ISR ↔ main, RTOS 태스크, 듀얼 코어 MCU 의 코어별 통계 — 에 어떤 도구가 대응하는지 고른다.

## 1. 문제

"공유 카운터 레이스를 고쳐 보세요. 그럼 8 코어에서 8배 빨라지나요?" — 동시성 면접의 첫 꼬리질문이다. 뮤텍스를 붙이면 맞는 답은 나오지만, 측정해 보면 스레드를 늘릴수록 **느려진다**. 펌웨어에서도 똑같다: 패킷·샘플 통계를 ISR 과 main 이(C-M14), FreeRTOS 태스크 여럿이(RTOS-B10), 듀얼 코어 MCU 의 두 코어가(ESP32, RP2040, STM32H7) 같이 세면 같은 lost update 가 나고, "인터럽트를 끄고 센다" 는 단일 코어 해법은 코어가 둘이 되는 순간 통하지 않는다.

OS-B04 에서 본 것: `counter++` 는 `ldr / add / str` 세 명령이라 두 스레드의 명령이 섞이면 증가가 사라진다. 이 레슨은 그걸 **고치는 세 가지 설계** 를 만들고 비용을 잰다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 카운터, 세 가지 설계 — 공유하는 곳이 어디인가</text>
  <rect x="10" y="40" width="230" height="200" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor" font-weight="bold">① 전역 뮤텍스</text>
  <rect x="60" y="80" width="130" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="72" y="98" fill="currentColor" font-family="monospace">lock</text>
  <text x="72" y="114" fill="currentColor" font-family="monospace">value</text>
  <g fill="currentColor"><text x="28" y="168">T0</text><text x="78" y="168">T1</text><text x="128" y="168">T2</text><text x="178" y="168">T3</text></g>
  <path d="M35 155 L100 122 M85 155 L115 122 M135 155 L135 122 M185 155 L150 122" stroke="currentColor" fill="none"/>
  <text x="22" y="196" fill="currentColor">매 증가마다 같은 락 + 같은</text>
  <text x="22" y="214" fill="currentColor">캐시 라인. 경합 → 커널 대기</text>
  <rect x="255" y="40" width="230" height="200" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="267" y="62" fill="currentColor" font-weight="bold">② atomic fetch_add</text>
  <rect x="305" y="80" width="130" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="317" y="105" fill="currentColor" font-family="monospace">atomic value</text>
  <g fill="currentColor"><text x="273" y="168">T0</text><text x="323" y="168">T1</text><text x="373" y="168">T2</text><text x="423" y="168">T3</text></g>
  <path d="M280 155 L345 122 M330 155 L360 122 M380 155 L380 122 M430 155 L395 122" stroke="currentColor" fill="none"/>
  <text x="267" y="196" fill="currentColor">락은 없지만 캐시 라인 하나를</text>
  <text x="267" y="214" fill="currentColor">모든 코어가 돌려 쓴다</text>
  <rect x="500" y="40" width="230" height="200" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="512" y="62" fill="currentColor" font-weight="bold">③ sloppy / 스레드별 칸</text>
  <rect x="560" y="80" width="110" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="572" y="102" fill="currentColor" font-family="monospace">global</text>
  <g class="f-ok" stroke="currentColor"><rect x="512" y="140" width="46" height="26" rx="3"/><rect x="566" y="140" width="46" height="26" rx="3"/><rect x="620" y="140" width="46" height="26" rx="3"/><rect x="674" y="140" width="46" height="26" rx="3"/></g>
  <g fill="currentColor" font-size="12"><text x="518" y="158">L0</text><text x="572" y="158">L1</text><text x="626" y="158">L2</text><text x="680" y="158">L3</text></g>
  <path d="M535 140 L590 114 M697 140 L645 114" stroke="currentColor" stroke-dasharray="4 3" fill="none"/>
  <text x="512" y="196" fill="currentColor">평소엔 자기 칸만. S 개 모이면</text>
  <text x="512" y="214" fill="currentColor">가끔 global 로 (점선)</text>
  <text x="10" y="266" fill="currentColor">정확성: ①② 항상 정확 · ③ global 은 근사값 (오차 ≤ T·(S−1)), 다 모으면 정확</text>
  <text x="10" y="288" fill="currentColor">비용: 공유 지점에 쓰는 횟수가 적을수록 스레드 수에 따라 늘어난다 (5. 풀이 실험 1)</text>
</svg>
```

| 설계 | C 도구 | 펌웨어 대응 | 이 레슨 |
|---|---|---|---|
| 전역 락 | `pthread_mutex_t` | FreeRTOS 뮤텍스, 단일 코어에서는 `taskENTER_CRITICAL()` / `__disable_irq()` | Part A |
| 원자 RMW | `atomic_fetch_add` | Cortex-M3/M4/M7 `LDREX/STREX` 루프 (M0/M0+ 는 없음 → 임계 구역) | Part A |
| 검사 + 갱신 한 구역 | 같은 뮤텍스, 범위만 다르게 | TX 크레딧, 큐 바이트 한도 | Part B |
| 쪼갠 락 + 근사값 | 스레드별 로컬 + 전역 | Linux `percpu_counter` (근사 read / 정확 sum) | Part B |
| 단일 writer 칸 | relaxed load + store | 코어별·ISR별 통계, 텔레메트리 태스크가 합산 | Part C |

### Part A — 전역 뮤텍스 카운터와 atomic 카운터

- `mc_add(c, n)` / `mc_get(c)` — `pthread_mutex_t` 하나로 보호하는 카운터. **읽기도** 락 안에서.
- `ac_add(c, n)` / `ac_get(c)` — `atomic_long` 하나, `atomic_fetch_add_explicit` (relaxed 로 충분한 이유는 C-A03 이 담당 — 여기선 결과만 쓴다).
- 4 스레드 × 10만 번 → 둘 다 정확히 400000.
- `demo_bench.c` 실험 1: 스레드 1~8 에서 mutex / atomic / sloppy / 스레드별 칸 처리량 → 표 + 그래프.

### Part B — 락 범위와 세분성

- `mc_try_add(c, n, limit)` — `value + n <= limit` 일 때만 더하고 true. 4 스레드가 40만 번 시도해도 **정확히 250000** 에서 멈춰야 한다. `demo_scope.c` 는 `mc_get()` 으로 검사하고 `mc_add()` 로 더하는 버전이 한도를 넘는 것을 **강제 스케줄** 로 매번 재현한다.
- `sc_update(c, tid, amt)` / `sc_get(c)` / `sc_get_exact(c)` — OSTEP 29.1 sloppy counter. 스레드별 로컬(+로컬 락)에 모으다가 임계값 S 이상이면 전역으로. 락 두 개를 잡을 때는 **항상 local → global**.
- 검사: 관찰 스레드가 본 근사값은 줄지 않고, 끝난 뒤 `총합 − 근사값 ≤ T·(S−1)`, `sc_get_exact` 는 정확.
- `demo_bench.c` 실험 2(락 범위: wide / narrow / batch), 실험 3(S 에 따른 처리량 vs 오차), `demo_order.c`(락 순서를 뒤집으면 교착).

### Part C — 듀얼 코어 패킷 처리: 워커별 통계 카운터

패킷 처리 워커(코어)마다 `{pkts, bytes, drops}` 칸이 하나씩 있고, 텔레메트리 스레드가 1초마다 합계를 읽는다. 운영자는 "통계 지우기" 를 할 수 있다.

- `bump(p, n)` — **쓰는 스레드가 하나뿐인** 원자 변수 증가: RMW 없이 relaxed load + relaxed store.
- `stats_rx(s, wid, len)` / `stats_drop(s, wid)` — 자기 칸만 갱신 (락 없음).
- `stats_read(s)` / `stats_clear(s)` — 원시 합 − 기준값(base). clear 는 워커 칸을 건드리지 않고 base 만 옮긴다. reader 끼리는 뮤텍스.
- 스트레스: 4 워커 × 10만 패킷 동안 관찰 스레드가 본 값은 **필드별로 줄지 않고**, 끝나면 정확히 일치.

## 2. Problem (English)

> **Fix a shared counter three ways and measure the cost of contention.**
>
> 1. `void mc_add(mcounter_t *c, long n)`, `long mc_get(mcounter_t *c)` protected by one `pthread_mutex_t`; `void ac_add(acounter_t *c, long n)`, `long ac_get(acounter_t *c)` using C11 atomics. With 4 threads × 100 000 increments the result must be exactly 400 000.
> 2. `bool mc_try_add(mcounter_t *c, long n, long limit)` adds only if `value + n <= limit`. 4 threads × 100 000 attempts with `limit = 250000` must stop at exactly 250 000 with exactly 250 000 `true` returns. Explain why "locked get, then locked add" is wrong even though every access holds the lock.
> 3. An approximate (sloppy) counter: per-thread local counts with their own locks, flushed into a global count when a local reaches threshold `S`. `sc_get` returns the global (approximate) value; `sc_get_exact` flushes every local. Bound the error. State and obey a lock-ordering rule.
> 4. Per-worker statistics `{pkts, bytes, drops}` where each worker is the only writer of its slot; a telemetry thread sums slots; `stats_clear` resets without writing to worker slots. No locks on the worker path.
>
> Measure throughput for 1–8 threads (median of 5 runs) and explain the curves. Follow-ups: Why does the mutex version get slower with more threads? When does shrinking the critical section not help? What does ThreadSanitizer miss here? How do you do the same on a single-core Cortex-M with an ISR, and on a dual-core MCU?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력, exit 1):

```text
FAIL starter.c:281  mc_get(&m) == 3
FAIL starter.c:286  ac_get(&ac) == 17
SKIP 2 stress checks (Part A): unit checks failed
FAIL starter.c:301  mc_try_add(&m, 6, 10) && mc_get(&m) == 6
FAIL starter.c:302  !mc_try_add(&m, 5, 10) && mc_get(&m) == 6
FAIL starter.c:303  mc_try_add(&m, 4, 10) && mc_get(&m) == 10
FAIL starter.c:308  sc_get(sc) == 0 && sc->local[0].count == 4
FAIL starter.c:310  sc_get(sc) == 5 && sc->local[0].count == 0
FAIL starter.c:312  sc_get(sc) == 5
FAIL starter.c:313  sc_get_exact(sc) == 8 && sc->local[1].count == 0 && sc_get(sc) == 8
FAIL starter.c:315  sc_get(sc) == 20
SKIP 5 stress checks (Part B): unit checks failed
FAIL starter.c:348  s.pkts == 4 && s.bytes == 350 && s.drops == 1
FAIL starter.c:349  atomic_load(&st->w[0].pkts) == 3 && atomic_load(&st->w[1].bytes) == 50
FAIL starter.c:355  s.pkts == 1 && s.bytes == 60 && s.drops == 0
FAIL starter.c:356  atomic_load(&st->w[0].pkts) == 3
SKIP 2 stress checks (Part C): unit checks failed
6/29 passed
```

하네스 설계: Part 마다 단일 스레드 단위 테스트가 먼저 돌고, 그게 다 통과해야 멀티스레드 스트레스가 돈다 (시작 코드가 멈추지 않게). 스트레스 CHECK 는 "올바른 구현이면 **매 실행** 성립" 하는 것만 본다 — 최종 합계, 한도, 단조 증가. 처리량은 절대 CHECK 하지 않는다.

관찰용 파일 (하네스와 별개):

```bash
cc -std=c11 -Wall -Wextra -O2 -pthread demo_bench.c -o bench && ./bench 5          # 실험 1~4, 약 90초
cc -std=c11 -Wall -Wextra -O2 -pthread demo_scope.c -o ds && ./ds                  # 검사-후-갱신 범위 버그
cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_scope.c -o ds_t && ./ds_t       # TSan 은 침묵
cc -std=c11 -Wall -Wextra -O2 -pthread [-DWRONG_ORDER] demo_order.c -o dord && ./dord   # 락 순서 (교착이면 1초 뒤 감지·종료)
cc -std=c11 -O2 -S demo_asm.c -o -                                                 # arm64: ldadd vs ldr/add/str
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -  # Cortex-M4
```

## 4. 힌트

<details>
<summary>힌트 1 — 읽기에도 락이 필요한가?</summary>

다른 스레드가 쓰는 동안 일반 변수를 읽으면 그것도 데이터 레이스다 (C11 5.1.2.4: 충돌하는 두 접근 중 하나가 쓰기이고, 원자적이지 않고, happens-before 가 없으면). 락을 잡고 읽으면 "직전 unlock 까지의 쓰기" 를 본다는 보장도 같이 얻는다.

</details>

<details>
<summary>힌트 2 — 한도 카운터: 검사 결과는 언제 낡는가</summary>

락을 풀면 그 순간부터 검사 결과는 "옛날 이야기" 다. 검사(`value + n <= limit`)와 갱신(`value += n`)이 같은 lock/unlock 쌍 **안에** 있어야 한다. 락 하나, 임계 구역 하나.

</details>

<details>
<summary>힌트 3 — sloppy counter 의 두 락</summary>

`sc_update` 는 자기 로컬 락을 잡고 더한 뒤, S 이상이면 **로컬 락을 쥔 채** 전역 락을 잡고 옮긴다. `sc_get_exact` 도 로컬 → 전역 순서로 잡아야 한다. 반대 순서로 잡는 함수가 하나라도 있으면 두 스레드가 서로의 락을 기다릴 수 있다.

</details>

<details>
<summary>힌트 4 — 쓰는 스레드가 하나뿐이면</summary>

lost update 는 "두 writer 의 load~store 가 겹칠 때" 생긴다. writer 가 하나면 겹칠 상대가 없으니 원자적 RMW 가 필요 없다. 그래도 reader 가 동시에 읽으니 접근 자체는 `atomic_load/atomic_store` (relaxed) 로. clear 는 워커 칸에 쓰지 말고 reader 쪽에 기준값을 둔다.

</details>

## 5. 풀이

### 0. 용어 — 이 레슨에 필요한 만큼

| 용어 | 뜻 |
|---|---|
| 임계 구역 (critical section) | 한 번에 한 스레드만 실행해야 하는 코드 구간. lock ~ unlock 사이 |
| 데이터 레이스 (data race) | C11 5.1.2.4p25: 서로 다른 스레드의 충돌 접근(하나 이상 쓰기), 하나 이상 비원자, happens-before 없음 → UB. TSan 이 찾는 것 |
| 레이스 컨디션 (race condition) | 결과가 실행 순서(타이밍)에 따라 틀려지는 **논리** 버그. 데이터 레이스 없이도 생긴다 (Part B) |
| 락 범위 (scope) | 한 임계 구역에 무엇을 넣는가. 불변식(invariant)을 지키는 데 필요한 것은 전부 넣고, 나머지는 뺀다 |
| 락 세분성 (granularity) | 락 하나가 지키는 데이터의 크기. 굵으면(전역 락) 단순·경합 큼, 잘면(칸마다 락) 경합 작음·순서 규칙 필요 |
| 경합 (contention) | 락이 잡혀 있을 때 다른 스레드가 잡으려는 것. 비용은 "기다림" 뿐 아니라 캐시 라인 이동·커널 왕복 |

POSIX 규칙 (pthread_mutex_lock): 기본(default) 뮤텍스를 같은 스레드가 다시 잠그거나, 소유자가 아닌 스레드가 풀면 **정의되지 않은 동작** 이다. `PTHREAD_MUTEX_ERRORCHECK` 로 만들면 각각 `EDEADLK`, `EPERM` 을 돌려준다 (연습 3 에서 실측). 또 POSIX 는 `pthread_mutex_lock/unlock` 을 메모리를 동기화하는 함수로 규정한다 — unlock 전의 쓰기는 같은 뮤텍스를 다음에 lock 한 스레드에게 보인다.

### Part A — 전역 뮤텍스와 atomic

- 뮤텍스 판은 OS-B04 의 `ldr/add/str` 를 그대로 두고 **끼어들 수 없게** 만든다. atomic 판은 세 명령을 **한 명령**(arm64 LSE `ldadd`) 또는 실패하면 재시도하는 루프(Cortex-M4 `ldrex/strex`)로 바꾼다 — 실측 asm 은 OS-B04 `demo_asm.c` 와 Part C 아래 표.
- `mc_get` 도 락을 잡는다. `long` 이 64비트인 32비트 MCU 에서는 읽기 자체가 두 번의 32비트 접근이라 찢어진 값(tearing)을 읽을 수 있다 (C-M14). 64비트 머신에서도 C11 상 데이터 레이스 = UB 다.
- `atomic_is_lock_free(&ac.value)` 를 CHECK 한다: lock-free 가 아니면 "atomic" 이 내부 락으로 구현돼 이 비교가 무의미해진다 (C-A03 Q7).
- relaxed 로 충분한 이유: 개수만 세고 이 값으로 다른 데이터를 넘기지 않기 때문. 깊은 설명은 C-A03.

#### 실험 1 — 처리량, 스레드 1~8 (`demo_bench.c`)

측정 환경: Apple M6 (`hw.perflevel0..2.physicalcpu` = Super 2 + Performance 4 + Efficiency 6 = 12 코어, `hw.cachelinesize` = 128), `Apple clang version 21.0.0 (clang-2100.0.123.102)`, macOS 27.2, `-O2`. 스레드를 다 만든 뒤 동시에 출발시키고 전부 join 할 때까지의 벽시계 시간. 구성마다 워밍업 1회 + 5회, **중앙값**. 매 실행 끝에 최종 합계가 T×ops 인지 확인한다 (틀리면 중단). macOS 는 `sched_setaffinity` 가 없어 코어 고정을 못 한다 — `pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE)` 로 P 코어를 **선호하게** 만 했다 (힌트일 뿐). 측정 중 다른 프로세스(시스템 데몬 등)도 돌고 있었다 (load average 7~9) — 그래서 min–max 를 같이 적는다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="70" y="28" fill="currentColor" font-weight="bold">실험 1 — 순수 증가 처리량 (Mops/s, 로그 축, 5회 중앙값)</text>
<path d="M70 40 L70 270 L510 270" stroke="currentColor" fill="none"/>
<text x="66.0" y="286" fill="currentColor" font-size="12">1</text><text x="128.9" y="286" fill="currentColor" font-size="12">2</text><text x="191.7" y="286" fill="currentColor" font-size="12">3</text><text x="254.6" y="286" fill="currentColor" font-size="12">4</text><text x="317.4" y="286" fill="currentColor" font-size="12">5</text><text x="380.3" y="286" fill="currentColor" font-size="12">6</text><text x="443.1" y="286" fill="currentColor" font-size="12">7</text><text x="506.0" y="286" fill="currentColor" font-size="12">8</text>
<text x="260.0" y="302" fill="currentColor" font-size="12">스레드 수</text>
<path d="M70 270.0 L510 270.0" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="64" y="274.0" fill="currentColor" font-size="12" text-anchor="end">10</text><path d="M70 233.4 L510 233.4" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="64" y="237.4" fill="currentColor" font-size="12" text-anchor="end">30</text><path d="M70 193.3 L510 193.3" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="64" y="197.3" fill="currentColor" font-size="12" text-anchor="end">100</text><path d="M70 156.8 L510 156.8" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="64" y="160.8" fill="currentColor" font-size="12" text-anchor="end">300</text><path d="M70 116.7 L510 116.7" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="64" y="120.7" fill="currentColor" font-size="12" text-anchor="end">1000</text><path d="M70 80.1 L510 80.1" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="64" y="84.1" fill="currentColor" font-size="12" text-anchor="end">3000</text><path d="M70 40.0 L510 40.0" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="64" y="44.0" fill="currentColor" font-size="12" text-anchor="end">10000</text>
<path d="M70.0 162.7 L132.9 182.6 L195.7 192.3 L258.6 202.6 L321.4 205.7 L384.3 209.3 L447.1 214.7 L510.0 218.3" stroke="currentColor" fill="none" stroke-width="1.5"/>
<circle cx="70.0" cy="162.7" r="4" class="f-warn" stroke="currentColor"/><circle cx="132.9" cy="182.6" r="4" class="f-warn" stroke="currentColor"/><circle cx="195.7" cy="192.3" r="4" class="f-warn" stroke="currentColor"/><circle cx="258.6" cy="202.6" r="4" class="f-warn" stroke="currentColor"/><circle cx="321.4" cy="205.7" r="4" class="f-warn" stroke="currentColor"/><circle cx="384.3" cy="209.3" r="4" class="f-warn" stroke="currentColor"/><circle cx="447.1" cy="214.7" r="4" class="f-warn" stroke="currentColor"/><circle cx="510.0" cy="218.3" r="4" class="f-warn" stroke="currentColor"/>
<path d="M70.0 131.7 L132.9 177.4 L195.7 177.6 L258.6 171.6 L321.4 172.9 L384.3 174.3 L447.1 174.8 L510.0 175.4" stroke="currentColor" fill="none" stroke-width="1.5" stroke-dasharray="6 4"/>
<rect x="66.0" y="127.7" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="128.9" y="173.4" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="191.7" y="173.6" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="254.6" y="167.6" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="317.4" y="168.9" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="380.3" y="170.3" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="443.1" y="170.8" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="506.0" y="171.4" width="8" height="8" class="f-acc" stroke="currentColor"/>
<path d="M70.0 160.4 L132.9 139.6 L195.7 128.7 L258.6 120.0 L321.4 123.7 L384.3 120.4 L447.1 117.9 L510.0 115.3" stroke="currentColor" fill="none" stroke-width="1.5" stroke-dasharray="2 3"/>
<path d="M70.0 155.4 L75.0 164.4 L65.0 164.4 Z" class="f-ok" stroke="currentColor"/><path d="M132.9 134.6 L137.9 143.6 L127.9 143.6 Z" class="f-ok" stroke="currentColor"/><path d="M195.7 123.7 L200.7 132.7 L190.7 132.7 Z" class="f-ok" stroke="currentColor"/><path d="M258.6 115.0 L263.6 124.0 L253.6 124.0 Z" class="f-ok" stroke="currentColor"/><path d="M321.4 118.7 L326.4 127.7 L316.4 127.7 Z" class="f-ok" stroke="currentColor"/><path d="M384.3 115.4 L389.3 124.4 L379.3 124.4 Z" class="f-ok" stroke="currentColor"/><path d="M447.1 112.9 L452.1 121.9 L442.1 121.9 Z" class="f-ok" stroke="currentColor"/><path d="M510.0 110.3 L515.0 119.3 L505.0 119.3 Z" class="f-ok" stroke="currentColor"/>
<path d="M70.0 115.9 L132.9 94.9 L195.7 79.9 L258.6 70.6 L321.4 65.1 L384.3 61.2 L447.1 60.9 L510.0 55.5" stroke="currentColor" fill="none" stroke-width="1.5" stroke-dasharray="10 3 2 3"/>
<path d="M70.0 110.9 L75.0 115.9 L70.0 120.9 L65.0 115.9 Z" class="f-mut" stroke="currentColor"/><path d="M132.9 89.9 L137.9 94.9 L132.9 99.9 L127.9 94.9 Z" class="f-mut" stroke="currentColor"/><path d="M195.7 74.9 L200.7 79.9 L195.7 84.9 L190.7 79.9 Z" class="f-mut" stroke="currentColor"/><path d="M258.6 65.6 L263.6 70.6 L258.6 75.6 L253.6 70.6 Z" class="f-mut" stroke="currentColor"/><path d="M321.4 60.1 L326.4 65.1 L321.4 70.1 L316.4 65.1 Z" class="f-mut" stroke="currentColor"/><path d="M384.3 56.2 L389.3 61.2 L384.3 66.2 L379.3 61.2 Z" class="f-mut" stroke="currentColor"/><path d="M447.1 55.9 L452.1 60.9 L447.1 65.9 L442.1 60.9 Z" class="f-mut" stroke="currentColor"/><path d="M510.0 50.5 L515.0 55.5 L510.0 60.5 L505.0 55.5 Z" class="f-mut" stroke="currentColor"/>
<text x="14" y="160" fill="currentColor" font-size="12" transform="rotate(-90 14 160)">Mops/s (log)</text>
<path d="M545 70 L581 70" stroke="currentColor" stroke-width="1.5"/>
<circle cx="563.0" cy="70.0" r="4" class="f-warn" stroke="currentColor"/>
<text x="589" y="74" fill="currentColor">mutex (전역 락)</text>
<path d="M545 98 L581 98" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 4"/>
<rect x="559.0" y="94.0" width="8" height="8" class="f-acc" stroke="currentColor"/>
<text x="589" y="102" fill="currentColor">atomic fetch_add</text>
<path d="M545 126 L581 126" stroke="currentColor" stroke-width="1.5" stroke-dasharray="2 3"/>
<path d="M563.0 121.0 L568.0 130.0 L558.0 130.0 Z" class="f-ok" stroke="currentColor"/>
<text x="589" y="130" fill="currentColor">sloppy S=1024</text>
<path d="M545 154 L581 154" stroke="currentColor" stroke-width="1.5" stroke-dasharray="10 3 2 3"/>
<path d="M563.0 149.0 L568.0 154.0 L563.0 159.0 L558.0 154.0 Z" class="f-mut" stroke="currentColor"/>
<text x="589" y="158" fill="currentColor">per-thread 슬롯</text>
<text x="545" y="192" fill="currentColor" font-size="12">Apple M6 (2S+4P+6E), clang 21</text>
<text x="545" y="210" fill="currentColor" font-size="12">공유 1곳에 쓰는 두 줄은</text>
<text x="545" y="226" fill="currentColor" font-size="12">스레드가 늘수록 내려가고,</text>
<text x="545" y="242" fill="currentColor" font-size="12">나눠 쓰는 두 줄은 올라간다.</text>
</svg>
```

| T | mutex | atomic | sloppy S=1024 | 스레드별 칸 (Part C) | mutex CPU% (그중 sys%) |
|---|---|---|---|---|---|
| 1 | 250.8 [156–273] | 636.5 [490–719] | 268.7 | 1022.4 | 98 (2) |
| 2 | 138.0 | 161.6 | 501.6 | 1922.3 | 146 (70) |
| 3 | 103.3 | 160.5 | 696.0 | 3016.9 | 194 (128) |
| 4 | 75.7 | 192.1 | 903.4 | 3993.2 | 261 (198) |
| 5 | 68.9 | 185.0 | 809.8 | 4705.2 | 333 (262) |
| 6 | 61.9 | 177.3 | 894.3 | 5296.4 | 399 (325) |
| 7 | 52.6 | 174.7 | 964.3 | 5336.6 | 498 (420) |
| 8 | 47.2 [45–52] | 171.5 [170–176] | 1041.5 [1021–1078] | 6281.4 [5965–6627] | 624 (537) |

(단위 Mops/s = 초당 백만 증가, 5회 중앙값 [최소–최대]. atomic 의 sys% 는 8 스레드에서도 7%.)

읽는 법:

1. **스레드 1개일 때가 각 방식의 "맨몸 비용"** 이다. 경합 없는 lock+unlock+증가 ≈ 1/250.8M ≈ 4.0 ns, `ldadd` ≈ 1.6 ns, 스레드별 칸(ldr/add/str) ≈ 1.0 ns. 경합이 없으면 뮤텍스도 싸다 — 사용자 공간 원자 연산 몇 개로 끝난다.
2. **공유 지점 하나에 쓰는 두 방식(mutex, atomic)은 스레드를 늘리면 처리량이 줄어든다.** atomic 은 2 스레드에서 636 → 162 로 4배 떨어지고 그 뒤로 평평하다. 카운터가 든 캐시 라인을 쓰려면 그 코어가 라인을 독점해야 하고(일관성 프로토콜, ARCH-M08 · MEM-M04), 두 코어가 번갈아 쓰면 라인이 매번 코어 사이를 오간다. 한 시점에 한 코어만 증가시킬 수 있으니 총 처리량은 "라인 이동 속도" 로 묶인다.
3. **mutex 는 그 위에 커널 비용이 붙는다.** 8 스레드에서 CPU 624% 중 537% 가 sys(커널) 시간이다. 잡혀 있는 뮤텍스를 만난 스레드는 커널에서 잠들고, 해제하는 쪽은 커널에 깨워 달라고 한다. 증가 한 번(수 ns)에 커널 왕복(µs 단위)이 섞이는 셈이다.
4. **나눠 쓰는 두 방식은 스레드에 따라 늘어난다.** sloppy 는 8 스레드에서 1041 (1 스레드의 3.9배), 스레드별 칸은 6281 (6.1배). 8배가 안 되는 이유: 8 스레드가 Super/Performance 코어 6개를 넘어 Efficiency 코어에도 올라가고(코어 고정 불가), 메모리 대역폭·주파수도 공유하기 때문.

#### 실험 4 — macOS 뮤텍스 정책: 처리량 vs 공정성

macOS 의 `pthread_mutex_t` 에는 비표준 정책 속성이 있다 (`pthread_mutexattr_setpolicy_np`). 이 머신의 기본값을 `pthread_mutexattr_getpolicy_np` 로 읽으니 **3 = `PTHREAD_MUTEX_POLICY_FIRSTFIT_NP`** 였다.

| T | default | FAIRSHARE_NP | FIRSTFIT_NP |
|---|---|---|---|
| 1 | 228.5 | 186.1 | 180.5 |
| 2 | 104.6 | **1.9** | 108.4 |
| 4 | 56.1 | **0.4** | 68.9 |
| 8 | 45.6 | **0.6** | 45.0 |

FIRSTFIT 는 락이 풀리는 순간 **먼저 잡는 스레드가 임자** 다 — 방금 푼 스레드가 다시 바로 잡을 수 있어 처리량이 높지만, 특정 스레드가 오래 굶을 수 있다(불공정). FAIRSHARE 는 대기자에게 차례를 넘기므로 매 증가마다 "깨우기 → 상대가 실행될 때까지 기다리기" 가 끼어 100배 이상 느려진다. 공정성과 처리량은 맞바꾸는 것이다. RTOS 뮤텍스는 대개 우선순위 순으로 넘겨주는 쪽이다 (RTOS-B10, RTOS-M04).

### Part B — 락 범위: 검사와 갱신은 한 임계 구역

```c
/* 틀린 범위 — demo_scope.c */                 /* 올바른 범위 — solution.c */
long v = mc_get(c);          /* 락 1 */          pthread_mutex_lock(&c->lock);
if (v + n > limit) return false;                 bool ok = c->value + n <= limit;
mc_add(c, n);                /* 락 2 */          if (ok) c->value += n;
return true;                                     pthread_mutex_unlock(&c->lock);
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">강제 스케줄: value=8, limit=10, A 와 B 가 모두 +2 를 시도 (demo_scope.c ①)</text>
  <text x="10" y="64" fill="currentColor">A</text>
  <text x="10" y="124" fill="currentColor">B</text>
  <rect x="40" y="44" width="170" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="50" y="64" fill="currentColor" font-family="monospace">mc_get → 8 (OK)</text>
  <rect x="230" y="104" width="250" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="240" y="124" fill="currentColor" font-family="monospace">mc_get → 8 (OK), mc_add → 10</text>
  <rect x="500" y="44" width="200" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="510" y="64" fill="currentColor" font-family="monospace">mc_add → 12 (한도 초과)</text>
  <path d="M210 59 L230 119" stroke="currentColor" fill="none"/>
  <path d="M480 119 L500 59" stroke="currentColor" fill="none"/>
  <text x="215" y="92" fill="currentColor" font-size="12">틈: A 는 락을 안 쥐고 있다</text>
  <rect x="10" y="160" width="720" height="78" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="182" fill="currentColor">모든 접근이 락 안이므로 데이터 레이스는 없다 → TSan 은 아무것도 보고하지 않는다.</text>
  <text x="22" y="204" fill="currentColor">그래도 "value ≤ limit" 이라는 불변식이 깨졌다 = 레이스 컨디션. 불변식에 관여하는 읽기와 쓰기는</text>
  <text x="22" y="226" fill="currentColor">같은 임계 구역 안에 있어야 한다. 올바른 판에서는 B 가 락에 막혀 그 틈에 들어오지 못한다.</text>
</svg>
```

`demo_scope.c` 실제 출력 — ① 은 조건 변수 핸드셰이크로 **스케줄을 강제** 한 것이라 매번 같고, ② 는 강제 없이 자유롭게 돌린 것이다:

```text
[forced] broken: value=8 limit=10, A and B both try +2
    A: B finished while A was between check and add
    A -> true, B -> true, final value=12 (OVER LIMIT)
[forced] ok    : value=8 limit=10, A and B both try +2
    A: B could not finish within 200 ms (blocked on the lock)
    A -> true, B -> false, final value=10 (ok)
[free] broken run 1: final=250001 (limit 250000) successes=250001 over=1
[free] broken run 2: final=250001 (limit 250000) successes=250001 over=1
[free] broken run 3: final=250002 (limit 250000) successes=250002 over=2
[free] broken run 4: final=250002 (limit 250000) successes=250002 over=2
[free] broken run 5: final=250001 (limit 250000) successes=250001 over=1
[free] ok     run 1: final=250000 (limit 250000) successes=250000 over=0
[free] ok     run 2: final=250000 (limit 250000) successes=250000 over=0
```

자유 실행에서 넘친 양은 겨우 1~2 다. 한도 근처에서만, 그것도 스레드 수−1 이하로만 넘치기 때문이다. **이런 버그가 테스트를 통과해 출하된다** — 크레딧이 하나 더 나가 DMA 디스크립터가 덮이는 식으로. TSan 빌드(`-fsanitize=thread`)도 같은 출력을 내고 **보고 0건, exit 0** 이었다 (TSan 실행에서는 run 3 이 over=0 이었다 — 타이밍 의존이라는 증거). 데이터 레이스가 아니니 TSan 의 관할이 아니다.

#### 실험 2 — 락 범위를 줄이면 빨라지나? (wide / narrow / batch)

연산 하나 = "일"(xorshift 반복, 공유 데이터 안 건드림) + 공유 갱신(`value++`, `sink += x`). wide 는 일까지 락 안, narrow 는 갱신만 락 안, batch 는 로컬에 64 개 모았다가 락 한 번.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="60" y="28" fill="currentColor" font-weight="bold">실험 2a 짧은 일 (40단계 ≈ 55 ns), Mops/s</text>
<path d="M60 40 L60 240 L330 240" stroke="currentColor" fill="none"/>
<text x="56.0" y="256" fill="currentColor" font-size="12">1</text><text x="94.6" y="256" fill="currentColor" font-size="12">2</text><text x="133.1" y="256" fill="currentColor" font-size="12">3</text><text x="171.7" y="256" fill="currentColor" font-size="12">4</text><text x="210.3" y="256" fill="currentColor" font-size="12">5</text><text x="248.9" y="256" fill="currentColor" font-size="12">6</text><text x="287.4" y="256" fill="currentColor" font-size="12">7</text><text x="326.0" y="256" fill="currentColor" font-size="12">8</text>
<text x="165.0" y="272" fill="currentColor" font-size="12">스레드 수</text>
<path d="M60 240.0 L330 240.0" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="54" y="244.0" fill="currentColor" font-size="12" text-anchor="end">0</text><path d="M60 190.0 L330 190.0" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="54" y="194.0" fill="currentColor" font-size="12" text-anchor="end">30</text><path d="M60 140.0 L330 140.0" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="54" y="144.0" fill="currentColor" font-size="12" text-anchor="end">60</text><path d="M60 90.0 L330 90.0" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="54" y="94.0" fill="currentColor" font-size="12" text-anchor="end">90</text><path d="M60 40.0 L330 40.0" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="54" y="44.0" fill="currentColor" font-size="12" text-anchor="end">120</text>
<path d="M60.0 211.2 L98.6 221.5 L137.1 228.0 L175.7 227.7 L214.3 227.2 L252.9 227.0 L291.4 226.8 L330.0 225.5" stroke="currentColor" fill="none" stroke-width="1.5"/>
<circle cx="60.0" cy="211.2" r="4" class="f-warn" stroke="currentColor"/><circle cx="98.6" cy="221.5" r="4" class="f-warn" stroke="currentColor"/><circle cx="137.1" cy="228.0" r="4" class="f-warn" stroke="currentColor"/><circle cx="175.7" cy="227.7" r="4" class="f-warn" stroke="currentColor"/><circle cx="214.3" cy="227.2" r="4" class="f-warn" stroke="currentColor"/><circle cx="252.9" cy="227.0" r="4" class="f-warn" stroke="currentColor"/><circle cx="291.4" cy="226.8" r="4" class="f-warn" stroke="currentColor"/><circle cx="330.0" cy="225.5" r="4" class="f-warn" stroke="currentColor"/>
<path d="M60.0 211.0 L98.6 225.0 L137.1 226.7 L175.7 226.8 L214.3 226.8 L252.9 227.2 L291.4 225.2 L330.0 224.2" stroke="currentColor" fill="none" stroke-width="1.5" stroke-dasharray="6 4"/>
<rect x="56.0" y="207.0" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="94.6" y="221.0" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="133.1" y="222.7" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="171.7" y="222.8" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="210.3" y="222.8" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="248.9" y="223.2" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="287.4" y="221.2" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="326.0" y="220.2" width="8" height="8" class="f-acc" stroke="currentColor"/>
<path d="M60.0 211.3 L98.6 183.5 L137.1 154.5 L175.7 128.8 L214.3 92.8 L252.9 70.2 L291.4 51.3 L330.0 52.3" stroke="currentColor" fill="none" stroke-width="1.5" stroke-dasharray="2 3"/>
<path d="M60.0 206.3 L65.0 215.3 L55.0 215.3 Z" class="f-ok" stroke="currentColor"/><path d="M98.6 178.5 L103.6 187.5 L93.6 187.5 Z" class="f-ok" stroke="currentColor"/><path d="M137.1 149.5 L142.1 158.5 L132.1 158.5 Z" class="f-ok" stroke="currentColor"/><path d="M175.7 123.8 L180.7 132.8 L170.7 132.8 Z" class="f-ok" stroke="currentColor"/><path d="M214.3 87.8 L219.3 96.8 L209.3 96.8 Z" class="f-ok" stroke="currentColor"/><path d="M252.9 65.2 L257.9 74.2 L247.9 74.2 Z" class="f-ok" stroke="currentColor"/><path d="M291.4 46.3 L296.4 55.3 L286.4 55.3 Z" class="f-ok" stroke="currentColor"/><path d="M330.0 47.3 L335.0 56.3 L325.0 56.3 Z" class="f-ok" stroke="currentColor"/>
<text x="430" y="28" fill="currentColor" font-weight="bold">실험 2b 긴 일 (400단계 ≈ 0.6 µs), Mops/s</text>
<path d="M430 40 L430 240 L700 240" stroke="currentColor" fill="none"/>
<text x="426.0" y="256" fill="currentColor" font-size="12">1</text><text x="464.6" y="256" fill="currentColor" font-size="12">2</text><text x="503.1" y="256" fill="currentColor" font-size="12">3</text><text x="541.7" y="256" fill="currentColor" font-size="12">4</text><text x="580.3" y="256" fill="currentColor" font-size="12">5</text><text x="618.9" y="256" fill="currentColor" font-size="12">6</text><text x="657.4" y="256" fill="currentColor" font-size="12">7</text><text x="696.0" y="256" fill="currentColor" font-size="12">8</text>
<text x="535.0" y="272" fill="currentColor" font-size="12">스레드 수</text>
<path d="M430 240.0 L700 240.0" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="424" y="244.0" fill="currentColor" font-size="12" text-anchor="end">0</text><path d="M430 190.0 L700 190.0" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="424" y="194.0" fill="currentColor" font-size="12" text-anchor="end">4</text><path d="M430 140.0 L700 140.0" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="424" y="144.0" fill="currentColor" font-size="12" text-anchor="end">8</text><path d="M430 90.0 L700 90.0" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="424" y="94.0" fill="currentColor" font-size="12" text-anchor="end">12</text><path d="M430 40.0 L700 40.0" stroke="currentColor" stroke-opacity="0.15" fill="none"/><text x="424" y="44.0" fill="currentColor" font-size="12" text-anchor="end">16</text>
<path d="M430.0 220.0 L468.6 226.2 L507.1 230.0 L545.7 230.0 L584.3 230.0 L622.9 228.8 L661.4 227.5 L700.0 225.0" stroke="currentColor" fill="none" stroke-width="1.5"/>
<circle cx="430.0" cy="220.0" r="4" class="f-warn" stroke="currentColor"/><circle cx="468.6" cy="226.2" r="4" class="f-warn" stroke="currentColor"/><circle cx="507.1" cy="230.0" r="4" class="f-warn" stroke="currentColor"/><circle cx="545.7" cy="230.0" r="4" class="f-warn" stroke="currentColor"/><circle cx="584.3" cy="230.0" r="4" class="f-warn" stroke="currentColor"/><circle cx="622.9" cy="228.8" r="4" class="f-warn" stroke="currentColor"/><circle cx="661.4" cy="227.5" r="4" class="f-warn" stroke="currentColor"/><circle cx="700.0" cy="225.0" r="4" class="f-warn" stroke="currentColor"/>
<path d="M430.0 218.8 L468.6 196.2 L507.1 172.5 L545.7 153.8 L584.3 162.5 L622.9 205.0 L661.4 205.0 L700.0 206.2" stroke="currentColor" fill="none" stroke-width="1.5" stroke-dasharray="6 4"/>
<rect x="426.0" y="214.8" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="464.6" y="192.2" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="503.1" y="168.5" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="541.7" y="149.8" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="580.3" y="158.5" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="618.9" y="201.0" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="657.4" y="201.0" width="8" height="8" class="f-acc" stroke="currentColor"/><rect x="696.0" y="202.2" width="8" height="8" class="f-acc" stroke="currentColor"/>
<path d="M430.0 217.5 L468.6 195.0 L507.1 173.8 L545.7 150.0 L584.3 126.2 L622.9 103.8 L661.4 78.8 L700.0 56.2" stroke="currentColor" fill="none" stroke-width="1.5" stroke-dasharray="2 3"/>
<path d="M430.0 212.5 L435.0 221.5 L425.0 221.5 Z" class="f-ok" stroke="currentColor"/><path d="M468.6 190.0 L473.6 199.0 L463.6 199.0 Z" class="f-ok" stroke="currentColor"/><path d="M507.1 168.8 L512.1 177.8 L502.1 177.8 Z" class="f-ok" stroke="currentColor"/><path d="M545.7 145.0 L550.7 154.0 L540.7 154.0 Z" class="f-ok" stroke="currentColor"/><path d="M584.3 121.2 L589.3 130.2 L579.3 130.2 Z" class="f-ok" stroke="currentColor"/><path d="M622.9 98.8 L627.9 107.8 L617.9 107.8 Z" class="f-ok" stroke="currentColor"/><path d="M661.4 73.8 L666.4 82.8 L656.4 82.8 Z" class="f-ok" stroke="currentColor"/><path d="M700.0 51.2 L705.0 60.2 L695.0 60.2 Z" class="f-ok" stroke="currentColor"/>
<path d="M60 300 L96 300" stroke="currentColor" stroke-width="1.5"/>
<circle cx="78.0" cy="300.0" r="4" class="f-warn" stroke="currentColor"/>
<text x="104" y="304" fill="currentColor">wide (일까지 락 안)</text>
<path d="M290 300 L326 300" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 4"/>
<rect x="304.0" y="296.0" width="8" height="8" class="f-acc" stroke="currentColor"/>
<text x="334" y="304" fill="currentColor">narrow (갱신만 락 안)</text>
<path d="M520 300 L556 300" stroke="currentColor" stroke-width="1.5" stroke-dasharray="2 3"/>
<path d="M538.0 295.0 L543.0 304.0 L533.0 304.0 Z" class="f-ok" stroke="currentColor"/>
<text x="564" y="304" fill="currentColor">batch (64개 모아 1번)</text>
</svg>
```

| T | 2a wide | 2a narrow | 2a batch | 2b wide | 2b narrow (sys%) | 2b batch |
|---|---|---|---|---|---|---|
| 1 | 17.3 | 17.4 | 17.2 | 1.6 | 1.7 (1) | 1.8 |
| 2 | 11.1 | 9.0 | 33.9 | 1.1 | 3.5 (2) | 3.6 |
| 4 | 7.4 | 7.9 | 66.7 | 0.8 | 6.9 (9) | 7.2 |
| 5 | 7.7 | 7.9 | 88.3 | 0.8 | 6.2 (78) | 9.1 |
| 6 | 7.8 | 7.7 | 101.9 | 0.9 | 2.8 (240) | 10.9 |
| 8 | 8.7 | 9.5 | 112.6 | 1.2 | 2.7 (391) | 14.7 |

(Mops/s, 5회 중앙값. 2a = 일 40단계 ≈ 55 ns, 2b = 400단계 ≈ 0.6 µs.)

- **wide 는 일 자체를 직렬화한다.** 2b 에서 스레드를 8개로 늘려도 1 스레드(1.6)보다 느리다(0.8~1.2). 공유 데이터를 안 건드리는 일을 락 안에 두는 건 순수한 낭비다.
- **narrow 는 "충돌이 드물 때만" 이긴다.** 2b 에서 4 스레드까지는 거의 선형(6.9)인데, 5~6 스레드에서 무너져 2.7 이 된다. 그 순간 sys% 가 9 → 240 → 391 로 뛴다. 충돌 한 번이 커널 대기·깨우기(µs)를 부르고, 깨어나는 동안 락이 사실상 더 오래 묶여 다음 충돌을 부르는 **convoy** 다. 2a 처럼 락을 50 ns 마다 잡으면 처음부터 이 상태다 — narrow 도 wide 와 같은 7~9.
- **batch 가 진짜 처방이다.** 락을 잡는 **횟수** 를 64분의 1로 줄이자 2a 112.6 (6.5배), 2b 14.7 (8 스레드 거의 선형). sloppy counter 는 batch 를 카운터 자료구조 안에 넣은 것이다.
- 정리: 범위는 **정확성** 이 정한다(불변식에 필요한 것은 전부 안에). 그 범위 안에서 성능은 "락을 몇 번, 얼마나 자주 잡나" 가 정한다.

### Part B — 세분성: sloppy counter (OSTEP 29.1)

```svg
<svg viewBox="0 0 740 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">sc_update(c, tid, amt) — 락 순서는 항상 local → global</text>
  <rect x="10" y="40" width="150" height="34" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor" font-family="monospace">lock(local[tid])</text>
  <rect x="180" y="40" width="130" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="190" y="62" fill="currentColor" font-family="monospace">count += amt</text>
  <rect x="330" y="40" width="130" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="340" y="62" fill="currentColor" font-family="monospace">count &gt;= S ?</text>
  <rect x="480" y="40" width="250" height="34" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="490" y="62" fill="currentColor" font-family="monospace">unlock(local[tid])  (대부분)</text>
  <path d="M160 57 L180 57 M310 57 L330 57 M460 57 L480 57" stroke="currentColor"/>
  <text x="470" y="35" fill="currentColor" font-size="12">아니오</text>
  <path d="M395 74 L395 110" stroke="currentColor"/>
  <text x="402" y="96" fill="currentColor" font-size="12">예 (S 번에 한 번)</text>
  <rect x="200" y="110" width="400" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="210" y="132" fill="currentColor" font-family="monospace">lock(global); global += count; unlock(global)</text>
  <path d="M400 144 L400 160" stroke="currentColor"/>
  <rect x="300" y="160" width="200" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="310" y="180" fill="currentColor" font-family="monospace">count = 0; unlock(local)</text>
  <text x="10" y="216" fill="currentColor">로컬 락은 거의 항상 비어 있다 (자기 스레드만 씀) → 경합 없는 4 ns. 전역 락은 S 번에 한 번만.</text>
  <text x="10" y="234" fill="currentColor">sc_get_exact 도 local[i] → global 순서로 잡고 옮긴다. 반대로 잡는 함수가 하나라도 있으면 교착 가능.</text>
</svg>
```

**오차 상한**: 갱신이 끝날 때마다 로컬은 S 미만이다 (S 이상이면 그 자리에서 0 으로 비움). amt ≥ 1 인 정수라면 각 로컬은 최대 S−1, 그래서 `진짜 합 − global ≤ T·(S−1)`. 테스트(T=4, S=64)는 이 상한 252 를 CHECK 한다. 실제 출력:

```text
Part B sloppy: approx=400000 exact=400000 (bound 252), observer reads=26276
```

approx 가 정확히 400000 으로 찍힌 건 관찰 스레드가 256 번에 한 번 부르는 `sc_get_exact` 가 마지막에 로컬을 다 옮겼기 때문이다 (그 시점에 따라 실행마다 다를 수 있다 — 그래서 CHECK 는 상한만 본다). 단위 테스트(S=5)가 근사값이 뒤처지는 모습을 결정적으로 보여 준다: 4 번 더해도 `sc_get == 0`, 5 번째에 5.

#### 실험 3 — S 를 바꾸면 (T=8)

| S | 1 | 2 | 4 | 16 | 64 | 256 | 1024 | 4096 |
|---|---|---|---|---|---|---|---|---|
| Mops/s (중앙값) | 28.7 | 34.2 | 40.1 | 61.2 | 159.7 | 505.5 | 1005.7 | 1189.8 |
| 최대 오차 T·(S−1) | 0 | 8 | 24 | 120 | 504 | 2040 | 8184 | 32760 |
| sys% | 531 | 529 | 506 | 470 | 347 | 183 | 43 | 8 |

S=1 은 매번 락 두 개를 잡는 "전역 락" 이라 순수 mutex(47.2)보다도 느리다. S 를 키울수록 전역 락을 덜 만나 커널 시간(sys%)이 531% → 8% 로 줄고 처리량은 40배 늘지만, 그만큼 `sc_get` 이 뒤처질 수 있다. OSTEP 도 같은 그래프로 "S 가 정확도와 성능을 맞바꾼다" 고 정리한다. 실무 판단: 대시보드·통계(1초에 한 번 보는 값)는 근사값으로 충분하고, 한도 검사(크레딧·쿼터)처럼 **정확한 값으로 결정** 하는 곳에는 쓰면 안 된다.

Linux 커널의 `percpu_counter`(lib/percpu_counter.c)가 정확히 이 구조다: CPU 별 칸 + batch 임계값, `percpu_counter_read()` 는 근사값(전역만), `percpu_counter_sum()` 은 락을 잡고 모든 CPU 칸을 더한 정확한 값.

#### 락 순서를 뒤집으면 — `demo_order.c`

`sc_get_exact` 를 "전역 락을 먼저 잡고 로컬들을 돈다" 로 쓰면 자연스러워 보이지만, `sc_update` 는 로컬 → 전역 순서다. 워커 1 + 리더 1 로 돌린 실제 결과:

```text
$ ./dord                       # 기본 빌드 = 올바른 순서 (3회)
OK: 2000000 updates finished in ~200 ms, 23653110 exact reads, global+local=2000000
OK: 2000000 updates finished in ~100 ms, 9906142 exact reads, global+local=2000000
OK: 2000000 updates finished in ~100 ms, 8016046 exact reads, global+local=2000000
$ ./dord                       # -DWRONG_ORDER 빌드 (5회 모두)
DEADLOCK: no progress for 1 s (updates=123, exact reads=0)
DEADLOCK: no progress for 1 s (updates=3, exact reads=73)
DEADLOCK: no progress for 1 s (updates=239, exact reads=0)
DEADLOCK: no progress for 1 s (updates=4, exact reads=503)
DEADLOCK: no progress for 1 s (updates=119, exact reads=0)
```

S=4 라 전역 락을 자주 잡게 해서 몇 백 번 안에 순환 대기에 빠진다. 같은 실수를 solution.c 에 넣어 보면 Part B 스트레스에서 3번 모두 멈췄다(5초 후 강제 종료). 주의: **이 맥의 TSan 은 이 교착을 보고하지 않았다** — `-fsanitize=thread` 빌드도 보고 없이 멈췄고, 락 두 개를 한 스레드가 a→b, b→a 순으로 잡는 10줄짜리 최소 예제에서도 `lock-order-inversion` 보고가 나오지 않았다 (`TSAN_OPTIONS=detect_deadlocks=1` 로도). TSan 의 락 순서 검사와 그 한계는 TOOL-M09, 교착의 4조건과 순서 규칙은 OS-M10, 런타임 락 순서 검사기는 OS-M11.

### Part C — 단일 writer 칸: 락도 RMW 도 없이

```c
static void bump(atomic_ulong *p, unsigned long n) {
    atomic_store_explicit(p, atomic_load_explicit(p, memory_order_relaxed) + n, memory_order_relaxed);
}
```

C-A03 의 `demo_lost_update.c` 는 **writer 가 여럿일 때** 이 load+store 가 증가를 잃는 것을 보였다. 여기서는 칸마다 writer 가 **하나뿐** 이라 겹칠 상대가 없다. reader(텔레메트리)는 동시에 읽지만 원자 접근끼리라 데이터 레이스가 아니고, 64비트 값이 찢어지지도 않는다. 실제로 내려가는 명령 (`demo_asm.c`, 실측):

| | arm64 (`cc -O2 -S`) | Cortex-M4 (`--target=thumbv7em-none-eabi -O2 -S`) | Cortex-M0+ (`--target=thumbv6m-none-eabi`) |
|---|---|---|---|
| `shared_inc` (fetch_add relaxed) | `ldadd x9, x8, [x8]` | `.LBB0_1: ldrex r1,[r0]` / `adds r1,#1` / `strex r2,r1,[r0]` / `cmp r2,#0` / `bne .LBB0_1` | `bl __atomic_fetch_add_4` |
| `slot_inc` (load + store relaxed) | `ldr x9,[x8]` / `add x9,x9,#1` / `str x9,[x8]` | `ldr r1,[r0]` / `adds r1,#1` / `str r1,[r0]` | `bl __atomic_load_4` / `adds` / `bl __atomic_store_4` |

단일 writer 판은 일반 `counter++` 와 **같은 명령** 이다. 다른 점은 C 언어 수준에서 "레이스가 아님" 이 보장되고 컴파일러가 접근을 없애거나 합치지 않는다는 것뿐이다. 그래서 실험 1 에서 가장 빠르다. M0+ 열은 주의: 이 clang 은 thumbv6m 에서 lock-free 원자 크기를 0 으로 보고(`-Watomic-alignment` 경고 3개) relaxed load/store 까지 라이브러리 호출로 만들었다. 그 함수는 펌웨어(또는 런타임 라이브러리)가 제공해야 하고 보통 인터럽트를 끄는 식으로 구현된다. 그래서 M0+ 펌웨어는 단일 writer 칸을 흔히 정렬된 `volatile uint32_t` 로 둔다 — 정렬된 32비트 `ldr/str` 이 한 번에 일어난다는 **아키텍처** 보장에 기대는 것이지 C11 보장은 아니다 (C-M14, C-B09). (Cortex-M4 의 `atomic_ulong` 은 32비트. 64비트 카운터를 32비트 MCU 에서 원자적으로 읽으려면 C-M14 의 재읽기 기법이나 Linux 의 `u64_stats_sync`(seqcount) 같은 장치가 필요하다 — LLAT-M02.)

나머지 설계 포인트:

- **칸 하나 = 캐시 라인 하나.** `_Alignas(128)` 로 `sizeof(wstat_t) == 128` 을 `_Static_assert` 한다. 이웃 워커의 칸이 같은 라인에 있으면 서로 다른 변수인데도 라인이 코어 사이를 오간다(false sharing). 측정은 ARCH-M08 이 담당.
- **clear 는 reader 쪽에서.** 워커 칸을 0 으로 쓰면 writer 가 둘(워커 + clear)이 되어 단일 writer 전제가 깨진다 — 워커가 막 읽은 옛값 + 1 을 써서 clear 가 사라질 수 있다. 대신 base 에 "지운 시점의 원시 합" 을 저장하고 읽을 때 뺀다. base 는 reader 끼리 공유하므로 뮤텍스로 보호하고, **원시 합 읽기와 base 빼기를 한 임계 구역** 에 넣는다 (Part B 의 범위 규칙).
- **스냅샷은 필드별로만 단조롭다.** `pkts` 와 `bytes` 를 서로 다른 순간에 읽으므로 "bytes / pkts = 평균 크기" 같은 필드 간 관계는 한 스냅샷 안에서 어긋날 수 있다. 테스트도 필드별 단조성만 CHECK 한다. 여러 필드를 한 순간으로 읽어야 하면 seqlock (LLAT-M02).
- 실제 출력: `Part C stats: pkts=392000 bytes=37429952 drops=8000, observer reads=447895` — 관찰 스레드가 45만 번 읽는 동안 감소 0회.

펌웨어 대응: 단일 코어 MCU 에서 ISR 과 main 이 같은 카운터를 증가시키면 main 쪽 증가는 `__disable_irq()` 구간이나 `LDREX/STREX` 가 필요하다 (C-M14). 그런데 "ISR 용 칸" 과 "main 용 칸" 을 따로 두고 읽는 쪽이 더하면, 각 칸의 writer 가 하나라 아무것도 끌 필요가 없다 — 이 Part 와 같은 설계다. 듀얼 코어(ESP32 의 FreeRTOS SMP, RP2040)에서는 인터럽트 끄기가 다른 코어를 막지 못하므로 이 설계의 가치가 더 커진다.

### 어느 방법을 언제

| 상황 | 선택 | 이유 (이 레슨 근거) |
|---|---|---|
| 정확한 값으로 결정한다 (한도·크레딧·ID 발급) | 뮤텍스 (검사+갱신 한 구역) 또는 atomic RMW/CAS | 근사값이면 결정이 틀린다 (Part B) |
| 숫자 하나만 센다, 경합 적음 | atomic `fetch_add` | 1 스레드 1.6 ns, 락보다 싸고 교착 없음 |
| 여러 필드를 함께 바꾼다 | 뮤텍스 | atomic 은 변수 하나만 원자적 |
| 핫 패스에서 많이, 읽기는 가끔 | 스레드(코어)별 칸 / sloppy | 실험 1: 8 스레드 6281 / 1041 vs 47 |
| 갱신이 잦고 락이 필요 | batch (로컬에 모아 가끔 한 번) | 실험 2: 2a 9.5 → 112.6 |
| ISR 에서 갱신 | atomic 또는 ISR 전용 칸. **뮤텍스 금지** (ISR 은 잠들 수 없다, RTOS-B10) | |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution        # 29/29 passed (5회 반복, 모두 통과)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o sol_asan && ./sol_asan   # 29/29, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o sol_tsan && ./sol_tsan          # 29/29, 보고 없음
```

실제 출력 (`-O2`):

```text
Part B sloppy: approx=400000 exact=400000 (bound 252), observer reads=26276
Part C stats: pkts=392000 bytes=37429952 drops=8000, observer reads=447895
29/29 passed
```

TSan 은 ASan 과 같이 쓸 수 없어서 따로 빌드한다. 관찰 스레드가 `sc_get_exact` 를 섞어 부르는 이유는 두 락을 잡는 경로를 실제로 **실행** 시키기 위해서다 — TSan 이든 교착이든 실행된 경로만 드러난다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 쓰기만 락, 읽기는 그냥 `return c->value;` | 동시 쓰기와 일반 읽기 = 데이터 레이스(UB). 32비트 MCU 의 64비트 값은 찢어진다 | 읽기도 lock/unlock (또는 atomic load) |
| `if (mc_get(c) + n <= limit) mc_add(c, n);` | 각 접근은 안전해도 검사 결과가 낡는다. TSan 침묵, 한도 1~2 초과 (demo_scope) | 검사+갱신을 한 임계 구역 (`mc_try_add`) |
| sloppy 의 exact 읽기를 global → local 로 | update 는 local → global. 순환 대기 → 5/5 교착 (demo_order). 이 맥 TSan 은 보고 없음 | 모든 경로에서 local → global |
| 공유와 무관한 일까지 락 안에 (wide) | 일 자체가 직렬화. 2b 8 스레드 1.2 vs 1 스레드 1.6 | 일은 락 밖, 공유 갱신만 안 |
| 락 범위만 줄이고 끝 (narrow) | 매 연산 락을 잡으면 충돌 → 커널 대기 → convoy (2b 6 스레드부터 2.7) | 잡는 횟수를 줄인다: batch, 스레드별 칸 |
| 스레드별 칸을 `long local[8]` 로 촘촘히 | 8 칸이 한두 캐시 라인에 → false sharing (ARCH-M08) | `_Alignas(128)` 칸 + `_Static_assert` |
| 단일 writer 칸을 clear 하려고 reader 가 0 을 씀 | writer 가 둘이 됨 → clear 유실 | reader 쪽 base 빼기 (Part C) |
| writer 가 둘인데 load+store 증가 | lost update (C-A03 demo_lost_update, TSan 도 못 잡음) | `atomic_fetch_add` |
| 정적 뮤텍스를 초기화 안 함 / destroy 후 재사용 | 미정의 동작 | `PTHREAD_MUTEX_INITIALIZER` 또는 `pthread_mutex_init`, 재사용 전 다시 init |
| 다른 스레드가 unlock, 같은 스레드가 재잠금 | 기본 뮤텍스에서 UB (POSIX) | 소유자만 unlock. 디버그 빌드는 `PTHREAD_MUTEX_ERRORCHECK` (연습 3) |
| 임계 구역 안에서 `printf`/로그/플래시 쓰기 | 느린 I/O 동안 모두가 기다린다 | 값만 복사해 락 밖에서 출력 |
| ISR 에서 뮤텍스 lock | ISR 은 잠들 수 없다. FreeRTOS 도 ISR 에서 뮤텍스 take 를 허용하지 않는다 (RTOS-B10) | atomic, ISR 전용 칸, 또는 main 쪽 임계 구역 (C-M14) |

## 8. 면접 Q&A

<details>
<summary>Q1. counter++ 레이스를 고치는 방법을 세 가지 말하고, 비용을 비교해 보세요.</summary>

① 뮤텍스로 감싼다 — 단순하고 여러 필드에도 쓸 수 있지만 경합 시 커널 대기가 붙는다. ② `atomic_fetch_add` — 명령 하나(arm64 `ldadd`, M4 `ldrex/strex` 루프), 교착 없음. ③ 공유를 없앤다 — 스레드별 칸에 세고 읽을 때 더하거나(sloppy, percpu). 이 맥 측정(1→8 스레드, Mops/s): 뮤텍스 251→47, atomic 637→172, sloppy 269→1042, 스레드별 칸 1022→6281. 꼬리: "그럼 항상 ③?" → 읽기가 비싸지고(칸을 다 돌아야) 근사값이며 메모리를 T×128 B 쓴다. 정확한 값으로 결정해야 하면 ①② 다.

</details>

<details>
<summary>Q2. 뮤텍스 카운터는 스레드를 늘리면 왜 더 느려지나요?</summary>

세 가지가 겹친다. (1) 락 변수와 카운터의 캐시 라인을 매번 다른 코어가 독점해야 해서 라인이 코어 사이를 오간다 — 락 없는 atomic 도 2 스레드에서 4배 떨어진 이유. (2) 잡혀 있는 락을 만난 스레드는 커널에서 잠들고 해제 쪽은 커널로 깨운다 — 8 스레드에서 CPU 의 86% 가 sys 시간이었다. (3) 깨어나는 데 µs 가 걸리는 동안 다른 스레드가 또 충돌해 convoy 가 생긴다. 측정 결과 1 스레드 251 → 8 스레드 47 Mops/s. 꼬리: "macOS 에서 공정한 뮤텍스로 바꾸면?" → FAIRSHARE 정책은 매번 대기자에게 넘겨서 0.4~1.9 Mops/s 로 100배 이상 느렸다. 기본값(FIRSTFIT)은 처리량을 위해 공정성을 포기한다.

</details>

<details>
<summary>Q3. race condition 과 data race 는 같은 말인가요? TSan 은 어느 쪽을 잡나요?</summary>

다르다. data race 는 C11 5.1.2.4p25 의 정의 — 서로 다른 스레드의 충돌 접근(하나 이상 쓰기), 하나 이상 비원자, happens-before 없음 → UB. race condition 은 결과가 실행 순서에 따라 틀려지는 논리 버그다. `mc_get()` 후 `mc_add()` 는 모든 접근이 락 안이라 data race 가 0 인데도 한도를 넘는다 (demo_scope: 강제 스케줄에서 12/10, 자유 실행에서 250001~250002). TSan 은 data race 만 본다 — 이 데모는 TSan 빌드에서도 보고 0건이었다. 반대로 data race 가 있지만 결과는 늘 맞아 보이는 코드도 있다 (C-A03 의 relaxed 채널). 꼬리: "그럼 race condition 은 어떻게 찾나?" → 불변식을 CHECK 하는 스트레스 테스트, 강제 스케줄(hook), 코드 리뷰에서 "검사 결과를 락 밖으로 가지고 나가는가" 를 본다.

</details>

<details>
<summary>Q4. sloppy (approximate) counter 를 설명하고, 오차 상한과 S 고르는 법을 말해 보세요.</summary>

스레드(CPU)마다 로컬 카운트와 로컬 락을 두고, 로컬이 S 이상이 되면 전역 락을 잡고 전역에 옮긴다 (OSTEP 29.1). 평소엔 경합 없는 로컬 락만 잡으므로 확장된다. 각 로컬은 갱신 직후 S 미만이므로 `진짜 − 전역 ≤ T·(S−1)`. S 는 "읽는 쪽이 감수할 오차" 와 "전역 락 빈도" 를 맞바꾼다 — 8 스레드 측정에서 S=1 은 28.7, S=64 는 160, S=1024 는 1006 Mops/s, 오차 상한은 0 / 504 / 8184. 정확한 값이 필요하면 모든 로컬을 local→global 순서로 잡고 옮긴다. 꼬리: "실제로 쓰이는 곳?" → Linux `percpu_counter` (`_read` 근사, `_sum` 정확), 네트워크 드라이버의 per-CPU 통계. 꼬리 2: "락 순서를 지키지 않으면?" → `demo_order.c` 에서 5번 모두 교착.

</details>

<details>
<summary>Q5. 임계 구역은 짧을수록 좋다고 하는데, 측정해 보면 항상 그런가요?</summary>

범위를 줄이는 건 필요조건이지 충분조건이 아니다. 공유와 무관한 일을 락 밖으로 빼면(narrow) 긴 일(0.6 µs)에서는 4 스레드까지 거의 선형으로 좋아졌다(1.7 → 6.9). 그러나 6 스레드부터 충돌이 잦아지자 convoy 로 2.7 까지 무너졌고, 짧은 일(55 ns)에서는 처음부터 wide 와 차이가 없었다(8 스레드 8.7 vs 9.5). 처리량을 정한 건 임계 구역의 길이보다 **락을 잡는 빈도** 였다 — 64 개씩 모아 한 번 잡자(batch) 112.6 / 14.7 로 올랐다. 그리고 범위를 줄일 때 불변식에 필요한 읽기까지 빼면 Q3 의 버그가 된다. 꼬리: "spinlock 으로 바꾸면 convoy 가 없어지나?" → 커널 왕복은 없어지지만 짧은 임계 구역 + 코어 수 ≤ 스레드 수 조건에서만 이득이고, 잡은 스레드가 선점되면 모두가 돈다 (OS-M07).

</details>

<details>
<summary>Q6. 텔레메트리 통계를 코어별로 나눠 셀 때 fetch_add 가 필요한가요? 통계 리셋은 어떻게 하죠?</summary>

칸마다 writer 가 하나면 필요 없다. relaxed load + relaxed store 로 충분하고, arm64 에서는 `ldr/add/str`, M4 에서는 `ldr/adds/str` 로 일반 증가와 같은 명령이 나온다(실측) — `ldadd` 나 `ldrex/strex` 루프가 없다. 원자 타입으로 두는 이유는 reader 와의 데이터 레이스를 없애고 64비트 머신에서 찢어짐을 막기 위해서다. 리셋은 reader 가 워커 칸에 쓰면 writer 가 둘이 되므로 안 되고, "지운 시점의 원시 합" 을 base 로 저장해 읽을 때 뺀다. 칸은 캐시 라인(이 맥 128 B)마다 하나. 꼬리: "32비트 MCU 에서 64비트 바이트 카운터는?" → 한 번에 읽히지 않으니 seqcount(Linux `u64_stats_sync`) 또는 재읽기 (C-M14, LLAT-M02).

</details>

<details>
<summary>Q7. 단일 코어 Cortex-M 에서 ISR 과 main 이 같은 카운터를 증가시킨다면? 뮤텍스를 쓰면 되나요?</summary>

ISR 은 잠들 수 없으므로 뮤텍스(FreeRTOS `xSemaphoreTake`)는 ISR 에서 못 쓴다 (RTOS-B10). 선택지: ① main 쪽 증가만 `__disable_irq()`/`__enable_irq()` (또는 PRIMASK 저장·복원) 로 감싼다 — ISR 은 main 에 의해 끊기지 않으므로 ISR 쪽은 그대로 둬도 된다 (C-M14). ② M3/M4/M7 은 `LDREX/STREX` 루프(`atomic_fetch_add`) — ISR 이 끼어들면 예외 진입/복귀가 exclusive monitor 를 지워 STREX 가 실패하고 재시도한다. M0/M0+ 에는 이 명령이 없어 컴파일러가 `__atomic_fetch_add_4` 같은 라이브러리 호출을 낸다(실측, Part C 표) — 그 구현은 보통 인터럽트를 끄는 것이다. ③ ISR 칸과 main 칸을 따로 두고 읽을 때 더한다 (Part C) — 끌 것도 원자 명령도 필요 없다. 꼬리: "듀얼 코어면?" → ① 은 다른 코어를 못 막는다. ②(전역 exclusive monitor 지원 메모리일 때) 또는 ③, 또는 하드웨어 세마포어(STM32H7 HSEM, RP2040 SIO 스핀락).

</details>

## 9. 추가 연습

### 연습 1 — 락 스트라이핑 히스토그램

센서 값 분포를 64 버킷 히스토그램으로 4 스레드가 동시에 센다. 락 하나로 다 감싸지 말고 **락 8 개** 를 두어 버킷 b 는 `lock[b % 8]` 이 지키게 하라. 전체 합계 `hist_total` 은 한 순간의 일관된 값이어야 한다.

<details>
<summary>답 보기</summary>

```c
static void hist_add(hist_t *h, unsigned v) {
    unsigned b = v % NBUCKET;
    pthread_mutex_t *m = &h->lock[b % NSTRIPE].m;
    pthread_mutex_lock(m); h->count[b]++; pthread_mutex_unlock(m);
}
static long hist_total(hist_t *h) {           /* 0 → 7 순서로 전부 잡고, 역순으로 푼다 */
    for (int i = 0; i < NSTRIPE; i++) pthread_mutex_lock(&h->lock[i].m);
    long t = 0;
    for (int b = 0; b < NBUCKET; b++) t += h->count[b];
    for (int i = NSTRIPE - 1; i >= 0; i--) pthread_mutex_unlock(&h->lock[i].m);
    return t;
}
```

세분성을 키운 대가가 "여러 락을 한꺼번에 잡는 경로" 다. 그 경로는 모두 같은 순서(인덱스 오름차순)로 잡아야 교착이 없다 — Java `ConcurrentHashMap`(초기 버전)의 세그먼트 락, 커널의 해시 버킷 락이 같은 구조. 락 구조체를 128 B 로 정렬해 스트라이프끼리 false sharing 도 막았다. 테스트: 버킷별 값이 단일 스레드 계산과 일치, 합계 400000, 관찰 스레드가 본 합계는 줄지 않음.

</details>

### 연습 2 — 경합 계측 뮤텍스

"이 락이 얼마나 경합하나?" 를 재는 래퍼를 써라. `cmutex_lock` 은 먼저 `pthread_mutex_trylock` 을 시도하고, 실패(EBUSY)하면 경합 횟수를 올린 뒤 `pthread_mutex_lock` 으로 기다린다.

<details>
<summary>답 보기</summary>

```c
static void cmutex_lock(cmutex_t *c) {
    if (pthread_mutex_trylock(&c->m) != 0) {
        atomic_fetch_add_explicit(&c->contended, 1, memory_order_relaxed);
        pthread_mutex_lock(&c->m);
    }
    atomic_fetch_add_explicit(&c->acquisitions, 1, memory_order_relaxed);
}
```

테스트를 결정적으로 만드는 방법: main 이 락을 쥔 상태에서 워커를 출발시키면 워커의 trylock 은 **반드시** 실패한다. main 은 `contended == 1` 이 될 때까지(최대 2초) 기다린 뒤 푼다. 카운터는 여러 스레드가 올리므로 `fetch_add`. 실무에서는 대기 시간(`clock_gettime` 차이)과 락 주소별 통계까지 모은다 — Linux 에서는 `perf lock`, futex 추적으로 같은 일을 한다 (LPERF-M07).

</details>

### 연습 3 — 오류 검사 뮤텍스

`make_errorcheck(m)` 으로 `PTHREAD_MUTEX_ERRORCHECK` 뮤텍스를 만들고, 같은 스레드의 재잠금과 소유자가 아닌 스레드의 해제가 어떤 값을 돌려주는지 확인하라.

<details>
<summary>답 보기</summary>

```c
pthread_mutexattr_t a;
pthread_mutexattr_init(&a);
pthread_mutexattr_settype(&a, PTHREAD_MUTEX_ERRORCHECK);
pthread_mutex_init(m, &a);
pthread_mutexattr_destroy(&a);
```

이 맥 실측: 두 번째 lock → `EDEADLK`, 다른 스레드의 unlock → `EPERM`, 이미 풀린 락을 다시 unlock → `EPERM`. 기본 뮤텍스에서 같은 일을 하면 POSIX 상 미정의 동작이라 조용히 넘어가거나 교착한다. 디버그 빌드에서 ERRORCHECK 를 쓰면 "소유권" 위반이 바로 드러난다 — 뮤텍스가 바이너리 세마포어와 다른 핵심이 소유권이다 (RTOS-B10: 소유권이 있어야 우선순위 상속도 가능).

</details>

### 연습 4 — sloppy counter 종료 flush

워커 스레드가 끝날 때 `sc_thread_done(c, tid)` 를 불러 남은 로컬을 전역으로 옮기게 하라. 모든 워커가 끝난 뒤에는 `sc_get_exact` 없이 `sc_get` 만으로 정확해야 한다 (스레드마다 반복 횟수가 달라 S 의 배수가 아니어도).

<details>
<summary>답 보기</summary>

```c
static void sc_thread_done(sloppy_t *c, int tid) {
    sc_local_t *l = &c->local[tid];
    pthread_mutex_lock(&l->lock);
    sc_move(c, l);              /* lock(global); global += count; unlock(global); count = 0 */
    pthread_mutex_unlock(&l->lock);
}
```

`sc_update` 의 flush 부분을 `sc_move` 로 뽑아 두 곳에서 같은 순서(local → global)로 쓴다 — 락 순서 규칙을 함수 하나에 가두는 습관. 테스트: 스레드 i 가 100000+i 번 갱신, join 뒤 `sc_get == 400006`, 모든 로컬 0. 펌웨어로 치면 "태스크 종료/슬립 진입 전에 로컬 통계 flush".

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice                         # 13/13 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_asan && ./pr_asan   # 13/13, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_tsan && ./pr_tsan                            # 13/13, 보고 없음
```

## 10. 핵심 정리

- 고치는 법은 셋: **락으로 감싸기**(뮤텍스), **한 명령으로 만들기**(atomic RMW), **공유를 없애기**(스레드별 칸·sloppy). 읽기도 보호 대상이다.
- 공유 지점 하나에 쓰는 방식은 스레드가 늘수록 느려진다: 캐시 라인 이동 + (뮤텍스는) 커널 대기. 이 맥: mutex 251→47, atomic 637→172 Mops/s (1→8 스레드). 나눠 쓰면 늘어난다: sloppy 1042, 스레드별 칸 6281.
- **범위는 정확성이 정한다**: 불변식에 관여하는 검사와 갱신은 한 임계 구역. 락 안의 get + 락 안의 add 는 race condition — 데이터 레이스가 아니라서 TSan 도 못 잡는다.
- **성능은 락을 잡는 빈도가 정한다**: narrow 만으로는 convoy 를 못 피한다. batch(64개씩) 와 sloppy(S) 가 빈도를 줄인다.
- sloppy: 오차 ≤ T·(S−1), 정확한 읽기는 모든 로컬 flush. 두 락을 잡는 모든 경로는 같은 순서(local → global) — 어기면 교착 (5/5 재현, 이 맥 TSan 은 침묵).
- 단일 writer 칸은 RMW 가 필요 없다 (relaxed load+store = ldr/add/str). 칸은 캐시 라인마다, 리셋은 reader 쪽 base.
- 펌웨어: ISR 에서는 뮤텍스 금지 → atomic(M3+ `LDREX/STREX`), 인터럽트 마스킹(main 쪽), 또는 ISR/코어별 칸. 듀얼 코어에서는 인터럽트 끄기가 소용없다.

## 참고자료

- OSTEP (Arpaci-Dusseau), 28장 Locks, **29장 Lock-based Concurrent Data Structures** (29.1 Concurrent Counters — approximate counter와 S 에 따른 성능 그래프): <https://pages.cs.wisc.edu/~remzi/OSTEP/threads-locks-usage.pdf>
- POSIX.1-2017 `pthread_mutex_lock` / `pthread_mutex_trylock` / `pthread_mutex_unlock` (mutex type 별 재잠금·비소유자 해제 동작, EDEADLK/EPERM/EBUSY): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_mutex_lock.html>
- POSIX.1-2017 XBD General Concepts — Memory Synchronization (뮤텍스 함수가 메모리를 동기화): <https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap04.html>
- C11 (N1570) 5.1.2.4 Multi-threaded executions and data races (p25 데이터 레이스 정의), 7.17 Atomics: <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Apple `pthread.h` — `pthread_mutexattr_setpolicy_np`, `PTHREAD_MUTEX_POLICY_FAIRSHARE_NP` / `FIRSTFIT_NP` (SDK 헤더 `usr/include/pthread/pthread.h`), `pthread/qos.h` — `pthread_set_qos_class_self_np`
- Linux kernel `lib/percpu_counter.c`, `include/linux/percpu_counter.h` (근사 read / 정확 sum), `include/linux/u64_stats_sync.h`: <https://elixir.bootlin.com/linux/latest/source/lib/percpu_counter.c>
- Paul E. McKenney, *Is Parallel Programming Hard, And, If So, What Can You Do About It?* — 5장 Counting (statistical counters, per-thread variables): <https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook.html>
- Clang ThreadSanitizer 문서: <https://clang.llvm.org/docs/ThreadSanitizer.html>
- 관련 레슨: OS-B04 (레이스 재현, counter++ asm), OS-B06 (조건 변수), TOOL-M09 (TSan 리포트 읽기, lock-order-inversion), OS-M02 (컨텍스트 스위치 비용), OS-M07 (TAS/TTAS/ticket 스핀락), OS-M10 · OS-M11 (교착, 락 순서 검사), C-A03 (memory_order, relaxed 카운터, demo_lost_update), C-M14 (ISR 공유 데이터, 인터럽트 마스킹), RTOS-B10 (뮤텍스 vs 세마포어, ISR 금지), RTOS-A08 (SMP RTOS 스핀락), ARCH-M08 (false sharing), MEM-M04 (일관성 트래픽), LLAT-B06 (atomic 연산 비용), LLAT-M02 (seqlock), LPERF-M07 (락 경합 프로파일링)
