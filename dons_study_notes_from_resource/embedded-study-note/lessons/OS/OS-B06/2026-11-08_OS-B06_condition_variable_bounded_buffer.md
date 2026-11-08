---
id: OS-B06
time: 70분
updated: 2026-11-08
---
# OS-B06 조건 변수: bounded buffer 생산자-소비자

## 학습 목표

- `pthread_mutex` 1개 + `pthread_cond` 2개(`empty`/`fill`)로 **bounded buffer** 를 구현하고, 생산자·소비자가 여럿이어도 모든 항목이 정확히 한 번, 생산자별 순서대로 전달됨을 테스트로 보인다.
- 조건 변수 3규칙을 **각각 깨뜨려 보고** 설명할 수 있다: ① 검사·대기·알림은 mutex 를 잡고 ② 대기는 `if` 가 아니라 `while` 로 감싸고 ③ "누구를 깨우는가" 를 생각해 CV 를 나누거나 broadcast 한다.
- OSTEP 30장의 깨진 두 버전(그림 30.6 `if`+CV 1개, 그림 30.8 `while`+CV 1개)을 **강제 스케줄** 로 매번 똑같이 재현하고, 강제하지 않은 실제 실행에서도 어떻게 깨지는지 실측으로 확인한다.
- `signal` 과 `broadcast` 를 고르는 기준(종료 알림, 여러 칸이 비었을 때, covering condition)을 말하고 헛깨움(futile wakeup) 비용을 숫자로 본다.
- 같은 문제가 RTOS 큐(FreeRTOS `xQueueReceive` 의 타임아웃 재검사 루프, ISR 의 `FromISR` 비블로킹 전송)에서 어떤 모양인지 연결한다.

## 1. 문제

"생산자-소비자 큐를 조건 변수로 짜 보세요. 왜 `while` 이죠? `signal` 대신 `broadcast` 는 언제 쓰죠?" — OS·펌웨어 면접에서 mutex 다음에 거의 반드시 나오는 질문이다. 펌웨어에서도 같은 구조가 매일 나온다: 센서 샘플링 태스크 → 로거 태스크, UART RX → 프로토콜 파서, 네트워크 RX → 처리 태스크. RTOS 의 메시지 큐(RTOS-B11)는 이 레슨의 bounded buffer 를 커널 안에 넣은 것이고, ISR 은 기다릴 수 없으니 `xQueueSendFromISR` 같은 비블로킹 전송만 쓴다 (C-M14, BM-M04).

OS-B04 에서 레이스를 봤고 OS-B05 에서 mutex 로 막았다. 그런데 mutex 만으로는 **"조건이 될 때까지 기다리기"** 를 못 한다. 버퍼가 비었을 때 소비자가 할 수 있는 건 (a) 락을 풀고 다시 잡기를 반복하는 바쁜 대기(busy-wait) — CPU 낭비, 또는 (b) **잠들었다가 누가 상태를 바꾸면 깨어나기** 다. (b) 가 조건 변수(condition variable, CV)다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">bounded buffer = 링(cap 칸) + mutex 1개 + 조건 변수 2개</text>
  <rect x="20" y="60" width="130" height="40" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="32" y="85" fill="currentColor">생산자 P1..Pn</text>
  <rect x="590" y="60" width="130" height="40" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="602" y="85" fill="currentColor">소비자 C1..Cm</text>
  <g stroke="currentColor">
    <rect x="240" y="60" width="50" height="40" class="f-acc"/>
    <rect x="290" y="60" width="50" height="40" class="f-acc"/>
    <rect x="340" y="60" width="50" height="40" class="f-acc"/>
    <rect x="390" y="60" width="50" height="40" class="f-mut"/>
    <rect x="440" y="60" width="50" height="40" class="f-mut"/>
  </g>
  <text x="250" y="52" fill="currentColor" font-family="monospace">head → (count=3, cap=5)</text>
  <path d="M150 80 L238 80" stroke="currentColor" stroke-width="2"/>
  <path d="M228 74 L238 80 L228 86" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M490 80 L588 80" stroke="currentColor" stroke-width="2"/>
  <path d="M578 74 L588 80 L578 86" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="20" y="150" width="300" height="56" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="30" y="172" fill="currentColor" font-family="monospace">empty: "빈 칸이 생겼다"</text>
  <text x="30" y="194" fill="currentColor">꽉 차면 생산자가 여기서 잔다</text>
  <rect x="420" y="150" width="300" height="56" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="430" y="172" fill="currentColor" font-family="monospace">fill: "항목이 생겼다"</text>
  <text x="430" y="194" fill="currentColor">비면 소비자가 여기서 잔다</text>
  <text x="20" y="236" fill="currentColor">put: lock → while(count==cap) wait(empty) → 넣기 → signal(fill) → unlock</text>
  <text x="20" y="258" fill="currentColor">get: lock → while(count==0)  wait(fill)  → 꺼내기 → signal(empty) → unlock</text>
  <text x="20" y="284" fill="currentColor">소비자는 생산자만, 생산자는 소비자만 깨운다 — 그래서 CV 가 두 개다 (OSTEP 그림 30.10)</text>
</svg>
```

`pthread_cond_wait(&cv, &m)` 이 하는 일은 세 가지다 (POSIX):

1. 호출할 때 **m 을 잡고 있어야** 한다.
2. **m 을 풀고 잠드는 것을 원자적으로** 한다 — 다른 스레드가 m 을 잡은 뒤 signal 하면 그 signal 은 반드시 이 대기자에게 보인다. (락 없이 검사하면 이 보장이 사라진다 → lost wakeup, `demo_lost_wakeup.c`)
3. 깨어나면 **m 을 다시 잡은 다음에** 돌아온다. 그런데 깨어난 시점과 락을 다시 잡은 시점 사이에 다른 스레드가 끼어들 수 있고(Mesa 의미론), 아무도 signal 하지 않았는데 돌아올 수도 있다(spurious wakeup). 그래서 돌아온 뒤 조건을 **다시** 검사해야 한다.

(RTOS 쪽 대응표는 5. 풀이 끝.)

### Part A — bounded buffer (mutex + CV 2개 + while)

- `bool bb_put(bb_t *b, int v)` — 꽉 찼으면 `empty` 에서 기다렸다 넣고 `fill` 에 알림. 닫힌 버퍼면 false.
- `bool bb_get(bb_t *b, int *out)` — 비었으면 `fill` 에서 기다렸다 꺼내고 `empty` 에 알림. 닫혔고 비었으면 false (닫혀도 남은 항목은 준다).
- 기다릴 때는 하네스가 준 `wait_on(b, cv)` 를 부른다 — 안에서 `pthread_cond_wait` 를 그대로 부르고, 테스트용으로 "지금 몇 명이 자고 있나" 를 센다.
- 테스트: 단일 스레드 FIFO·wrap-around, 꽉 찼을 때 생산자가 실제로 잠드는지, 비었을 때 소비자가 잠드는지, 스트레스 4 구성 (P×C×cap = 1×1×1, 4×4×4, 8×2×8, 2×8×2) 에서 **모든 항목 정확히 한 번 + 생산자별 순서 유지**.

### Part B — OSTEP 30장의 깨진 버전 재현, 그리고 종료

하네스에 OSTEP 그대로의 깨진 버전이 들어 있다:

| 버전 | 대기 | CV | OSTEP |
|---|---|---|---|
| v1 | `if` | 1개, signal | 그림 30.6 |
| v2 | `while` | 1개, signal | 그림 30.8 |
| v2b | `while` | 1개, **broadcast** | 30.3 covering condition 방식 |
| mine | `while` | 2개 (`empty`/`fill`) | 그림 30.10 — 내가 Part A 에서 쓴 것 |

세 시나리오를 **강제 스케줄(forced schedule)** 로 돌린다. 강제라는 뜻: 하네스가 "누가 언제 락을 잡고 누가 깨어나는지" 를 정해 준다. 그 선택은 모두 POSIX 가 허용하는 것이라, 여기서 깨지는 코드는 실제 스케줄러에서도 언젠가 깨진다.

- **S1 가로채기 (그림 30.7)**: C1 이 빈 버퍼에서 잠듦 → P 가 넣고 signal → C1 이 락을 다시 잡기 **전에** C2 가 먼저 가져감 → C1 실행.
- **S2 가짜 깨움**: C1 이 잠든 사이 상태 변화 없이 깨움 (spurious wakeup 과 같은 상황).
- **S3 엉뚱한 깨움 (그림 30.9)**: 용량 1, C1·C2 가 먼저 잠들고 P 가 1, 2 를 넣는다. C1 이 1 을 꺼낸 뒤의 signal 이 P 대신 C2 에게 간다.

그리고 내 코드에 `bb_close()` 를 추가한다: 닫으면 잠든 생산자·소비자가 **전부** 깨어나 false 로 돌아와야 한다.

### Part C — 센서 샘플 → 로거 (배치 꺼내기)

센서 샘플링 스레드 2개가 샘플을 넣고, 로거 스레드 1개가 **한 번에 최대 16개** 를 꺼내 "플래시 페이지" 로 쓴다 (플래시는 페이지 단위로 써야 효율적이다).

- `int bb_get_batch(bb_t *b, int *out, int max)` — 1개 이상 생길 때까지 기다린 뒤 최대 max 개를 꺼낸다. 닫혔고 비었으면 0.
- 함정: 한 번에 **여러 칸** 이 비었는데 `signal` 한 번이면 생산자 **한 명만** 깨어난다. 테스트: 꽉 찬 용량 4 버퍼에 생산자 3명이 잠든 상태에서 배치로 4개를 꺼내면 3명 모두 들어와야 한다.
- 스트레스: 2 × 30000 샘플이 정확히 한 번, 센서별 순서대로 기록.

## 2. Problem (English)

> **Bounded buffer with POSIX condition variables.**
>
> Implement a fixed-capacity FIFO shared by many producer and many consumer threads using one `pthread_mutex_t` and two `pthread_cond_t` (`empty`, `fill`):
>
> ```c
> bool bb_put(bb_t *b, int v);               // blocks while full; false if closed
> bool bb_get(bb_t *b, int *out);            // blocks while empty; false if closed and drained
> void bb_close(bb_t *b);                    // wake every waiter; later puts fail, gets drain
> int  bb_get_batch(bb_t *b, int *out, int max); // wait for >= 1 item, take up to max; 0 if closed+empty
> ```
>
> Requirements: every item delivered exactly once; items from one producer are seen by any consumer in production order; no busy-waiting; no thread sleeps forever while it could make progress.
>
> Then explain, with a concrete interleaving, why the textbook versions with (1) `if` instead of `while` and (2) a single condition variable for both producers and consumers fail with 2+ consumers.
>
> Follow-ups: What is a spurious wakeup and what does POSIX say about it? Why must the predicate be checked while holding the mutex? When do you need `pthread_cond_broadcast`? Is it OK to signal after unlocking? How would you add a timeout? What changes in an ISR?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력, 0.17 초에 끝남 — 멈추지 않는다):

```text
FAIL starter.c:441  bb_put(&b, 1) && bb_put(&b, 2) && bb_put(&b, 3)
FAIL starter.c:442  b.count == 3
...
FAIL starter.c:478  ok
SKIP 8 stress checks (Part A): unit checks failed
S1 steal (30.7)          v1=UNDERFLOW  v2=OK         v2b=OK         mine=WRONG
S2 spurious              v1=UNDERFLOW  v2=OK         v2b=OK         mine=WRONG
S3 wrong wakeup (30.9)   v1=UNDERFLOW  v2=ALL_ASLEEP v2b=OK         mine=WRONG
FAIL starter.c:507  r[0][3] == OUT_OK
...
FAIL starter.c:576  all_in
SKIP 2 stress checks (Part C): unit checks failed
8/37 passed
```

통과하는 8개 중 4개는 "깨진 버전이 정말 깨진다" 를 확인하는 체크다 (v1/v2/v2b 결과) — 시작 코드에서도 이미 표가 보인다. 하네스 설계:

- **기다림에는 전부 제한 시간이 있다.** `WAIT_FOR(b, 2000ms, 조건)` 은 1 ms 마다 락을 잡고 조건을 본다. 스트레스의 join 도 30 초 제한 + "구조(rescue)": 하네스가 직접 `closed=true` + 모든 CV broadcast 를 하고, 그래도 안 끝나는 스레드는 detach 한다. 그래서 틀린 구현도 멈추지 않고 FAIL 로 끝난다.
- **체크는 매번 성립하는 것만.** 정확히 한 번, 생산자별 순서, 강제 스케줄의 결과, 잠든 스레드 수. 시간은 출력만 한다.
- `bb_t` 의 `waiting`/`waits`/`underflow`/`overflow` 는 계측용이다. `ring_get` 은 빈 버퍼에서 불리면 OSTEP 처럼 assert 로 죽는 대신 `underflow++` 하고 쓰레기 값(-999)을 돌려준다.
- `scv_t` 는 깨진 버전 전용 "테스트용 조건 변수" — 5. 풀이 Part B 에서 설명한다.

관찰용 파일 (하네스와 별개):

```bash
cc -std=c11 -O2 -pthread demo_freerun.c -o dfr && ./dfr 1 2 200000 5        # 강제 없이 v1/v2/v3 실제 실행
cc -std=c11 -O2 -pthread demo_lost_wakeup.c -o dlw && ./dlw                 # 락 없이 검사 → signal 분실
cc -std=c11 -O2 -pthread -DFIX demo_lost_wakeup.c -o dlwf && ./dlwf         # 고친 버전
cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_lost_wakeup.c -o dlwt && ./dlwt   # TSan 보고
cc -std=c11 -O2 -pthread demo_wakeups.c -o dw && ./dw 8 8 1 200000 5         # signal vs broadcast 헛깨움
```

## 4. 힌트

<details>
<summary>힌트 1 — 모양은 항상 같다: lock → while(못 함) wait → 상태 변경 → 알림 → unlock</summary>

조건 변수 코드는 거의 항상 이 다섯 줄이다. "못 함" 은 put 이면 `count == cap`, get 이면 `count == 0`. 상태를 바꾼 쪽이 "이제 할 수 있게 된 쪽" 을 깨운다: put 은 소비자(`fill`), get 은 생산자(`empty`).

</details>

<details>
<summary>힌트 2 — 왜 while 인가: 깨어났다는 것은 "조건이 참이다" 가 아니다</summary>

`pthread_cond_wait` 에서 돌아왔다는 건 "한번 다시 보라" 는 뜻일 뿐이다. 그 사이 다른 소비자가 가져갔을 수도(S1), 아무도 안 깨웠을 수도(S2) 있다. POSIX 도 돌아온 것이 조건에 대해 아무것도 뜻하지 않으니 다시 평가하라고 적고 있다.

</details>

<details>
<summary>힌트 3 — close: closed 도 "조건" 의 일부다</summary>

대기 조건을 `count == 0 && !closed` 로 만든다. 루프를 빠져나온 뒤 `count == 0` 이면 닫힌 것이다. closed 를 바꾼 쪽은 **기다리는 모든 스레드** 가 진행할 수 있게 되었으므로 `signal` 이 아니라 `broadcast`, 그리고 `fill` 과 `empty` 둘 다.

</details>

<details>
<summary>힌트 4 — 배치: 몇 명이 진행 가능해졌나 세어 보라</summary>

칸이 n 개 비었으면 생산자 최대 n 명이 진행할 수 있다. signal 은 "적어도 한 명" 만 깨운다. n 번 signal 하거나 broadcast 한다 (broadcast 로 깨어난 나머지는 while 덕분에 다시 잠든다).

</details>

## 5. 풀이

### Part A — 정답의 모양

```c
static bool bb_get(bb_t *b, int *out) {          /* bb_put 은 대칭: count==cap, empty 에서 대기, fill 에 알림 */
    pthread_mutex_lock(&b->m);
    while (b->count == 0 && !b->closed)           /* ① 락 안에서 검사, ② while */
        wait_on(b, &b->fill);                     /*    = pthread_cond_wait(&b->fill, &b->m) */
    if (b->count == 0) { pthread_mutex_unlock(&b->m); return false; }   /* 닫혔고 다 비움 */
    *out = ring_get(b);
    pthread_cond_signal(&b->empty);               /* ③ 생산자만 깨운다 */
    pthread_mutex_unlock(&b->m);
    return true;
}
```

| 줄 | 이유 |
|---|---|
| `lock` 후 검사 | `count` 는 공유 상태. 락 없이 읽으면 C11 5.1.2.4 의 데이터 레이스(UB) 이고, 검사와 잠들기 사이 틈에 온 signal 을 잃는다 (아래 lost wakeup) |
| `while` | Mesa 의미론 + spurious wakeup. 돌아온 뒤 조건이 참이라는 보장이 없다 |
| `wait(cv, m)` | m 을 원자적으로 풀고 잠든다. 그래서 생산자가 m 을 잡고 넣고 signal 하는 동안 소비자는 "잠들기 직전" 상태에 머물 수 없다 |
| CV 2개 | signal 이 "진행할 수 있게 된 종류" 에게만 간다 → 엉뚱한 깨움(S3) 불가능 |
| `closed` 를 대기 조건에 | 종료도 "상태" 다. 상태로 기록하지 않은 알림은 그 순간 자는 스레드가 없으면 사라진다 (OSTEP 30.1 의 `done` 변수와 같은 이유) |

**생산자별 순서는 왜 보장되나:** 링은 FIFO 이고 모든 put/get 은 같은 락 아래서 일어나므로 전체 순서가 하나 있다. 생산자 p 의 항목은 그 순서대로 링에 들어간다. 소비자 한 명의 get 들은 서로 순서가 있으므로 링 순서대로 뽑힌다 → 소비자 한 명이 본 p 의 항목은 증가 순. (소비자끼리 사이의 순서는 보장되지 않는다 — 출력 순서가 중요하면 소비자 1명이거나 시퀀스 번호로 재정렬.)

**실제 출력 (solution):**

```text
Part A stress P=1 C=1 cap=1: items=20000 waits=39996
Part A stress P=4 C=4 cap=4: items=80000 waits=57922
Part A stress P=8 C=2 cap=8: items=80000 waits=51321
Part A stress P=2 C=8 cap=2: items=40000 waits=52569
```

`P=1 C=1 cap=1` 에서 `waits=39996 ≈ 2 × 20000`: 용량 1 이면 거의 항목마다 생산자와 소비자가 한 번씩 잠든다 — 핑퐁. 용량을 키우면 잠드는 횟수(=컨텍스트 스위치, OS-M02)가 줄어든다. 큐 깊이는 **지연 vs 처리량** 의 손잡이다.

### Part A — 락 없이 검사하면: lost wakeup (`demo_lost_wakeup.c`)

```c
if (count == 0) {          /* 락 없이 읽음 */
    sleep_ms(50);          /* 틈 (일부러 넓힘) — 생산자가 여기서 count=1 + signal */
    pthread_mutex_lock(&m);
    pthread_cond_timedwait(&fill, &m, &deadline_1s);   /* 이미 지나간 signal 을 기다린다 */
```

틈은 sleep 으로 넓힌 강제 스케줄이다 (실제로는 수 ns 짜리 틈이 아주 드물게 맞아떨어진다 — 그래서 더 위험하다). 실제 출력:

```text
$ ./dlw   (3회)
unlocked check: waited 1000 ms, rc=ETIMEDOUT (lost wakeup), count=1
unlocked check: waited 1006 ms, rc=ETIMEDOUT (lost wakeup), count=1
unlocked check: waited 1008 ms, rc=ETIMEDOUT (lost wakeup), count=1
$ ./dlwf  (-DFIX: 같은 50 ms 틈을 락 안에 둠, 3회)
check under lock: waited 51 ms, rc=0, count=1
check under lock: waited 55 ms, rc=0, count=1
check under lock: waited 51 ms, rc=0, count=1
```

`count=1` 인데 1 초를 다 기다렸다. signal 은 **기억되지 않는다** — 그 순간 잠든 스레드가 없으면 그냥 사라진다 (세마포어와 다른 점, RTOS-B08). 고친 버전은 틈이 락 안에 있어서 생산자가 끼어들 수 없고, `wait` 가 락 풀기와 잠들기를 원자적으로 하므로 signal 을 놓치지 않는다. TSan 은 하드웨어 운과 상관없이 잡는다 (`-g -O1 -fsanitize=thread`, 실제 출력):

```text
WARNING: ThreadSanitizer: data race (pid=42215)
  Write of size 1 at 0x000100e9c070 by thread T1 (mutexes: write M0):
    #0 producer demo_lost_wakeup.c:35 (OS-B06_dlwt:arm64+0x100000910)

  Previous read of size 1 at 0x000100e9c070 by main thread:
    #0 main demo_lost_wakeup.c:48 (OS-B06_dlwt:arm64+0x100000760)

  As if synchronized via sleep:
    #0 nanosleep <null> (libclang_rt.tsan_osx_dynamic.dylib:arm64e+0x31960)
    #1 producer demo_lost_wakeup.c:33 (OS-B06_dlwt:arm64+0x1000008f0)
  Location is global 'count' at 0x000100e9c070 (OS-B06_dlwt+0x100008070)
  ... (mutex 생성 위치, 스레드 생성 스택 생략)
SUMMARY: ThreadSanitizer: data race demo_lost_wakeup.c:35 in producer
unlocked check: waited 1002 ms, rc=ETIMEDOUT (lost wakeup), count=1
ThreadSanitizer: reported 1 warnings
(exit 134)
```

읽을 거리 두 개만 (리포트 정독은 TOOL-M09): ① 쓰기 쪽은 `mutexes: write M0` — 락을 잡고 썼지만 읽기 쪽이 락 없이 읽었으니 한쪽만 잡은 락은 소용없다. ② "As if synchronized via sleep" — TSan 은 sleep 으로 순서를 맞춘 것을 동기화로 인정하지 않는다고 알려 준다. `int count` 인데 "size 1" 인 것은 clang 최적화가 0/1 만 저장되는 전역을 1바이트 bool 로 줄였기 때문이다 (`-O1`).

### Part B — 깨진 버전을 매번 똑같이 깨뜨리는 법: `scv_t`

진짜 `pthread_cond` 로는 S3 을 강제할 수 없다. 대기자가 {C2, P} 둘일 때 `signal` 이 누구를 깨울지 POSIX 는 정하지 않는다("적어도 하나"). 그래서 깨진 버전(v1/v2/v2b)은 하네스가 만든 `scv_t` 를 쓴다:

```c
static void scv_wait(scv_t *s, pthread_mutex_t *m) {
    unsigned long my = s->next++;                               /* 번호표 */
    while (my >= s->woken || (s->held && my >= s->runnable))    /* 깨워졌고, (held 면) 실행 허락도 받았나 */
        pthread_cond_wait(&s->cv, m);
}
static void scv_signal(scv_t *s) { if (s->woken < s->next) s->woken++; pthread_cond_broadcast(&s->cv); }
```

- `signal` = 가장 오래 기다린 스레드부터(FIFO). POSIX 가 허용하는 여러 선택 중 하나.
- `held` = 깨어난 스레드도 하네스가 `scv_run_one()` 할 때까지 락을 다시 못 잡는다. "스케줄러가 그 스레드를 늦게 돌렸다" 와 같다. 이것도 합법적인 스케줄.
- 내 버전(mine)은 진짜 `pthread_cond` 를 쓴다. 강제할 필요가 없다는 것이 바로 요점이다: CV 가 2개면 각 CV 의 대기자가 모두 같은 종류라서 **누가 깨어나든** 진행할 수 있다.

S1 은 진짜 CV 로도 강제가 쉽다: 대기자가 C1 하나뿐이고, 하네스(main)가 락을 쥔 채 "P 의 임계 구역(넣기+signal)" 과 "C2 의 임계 구역(꺼내기)" 을 연달아 수행하면 "P 가 unlock 하자마자 C2 가 C1 보다 먼저 락을 잡은" 스케줄과 똑같다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">S1 가로채기 (OSTEP 그림 30.7) — 깨어남 ≠ 조건 참</text>
  <text x="10" y="62" fill="currentColor">C1</text>
  <text x="10" y="122" fill="currentColor">P</text>
  <text x="10" y="182" fill="currentColor">C2</text>
  <rect x="50" y="45" width="150" height="28" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="58" y="64" fill="currentColor" font-family="monospace">count==0 → wait</text>
  <rect x="210" y="45" width="230" height="28" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="222" y="64" fill="currentColor">Sleep → Ready (락 대기)</text>
  <rect x="210" y="105" width="130" height="28" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="218" y="124" fill="currentColor" font-family="monospace">put; signal</text>
  <rect x="345" y="165" width="95" height="28" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="353" y="184" fill="currentColor" font-family="monospace">get (=42)</text>
  <path d="M275 105 L275 75" stroke="currentColor" stroke-width="2"/>
  <path d="M269 85 L275 75 L281 85" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="450" y="45" width="130" height="28" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="458" y="64" fill="currentColor">락 획득, 실행</text>
  <text x="450" y="104" fill="currentColor" font-family="monospace">if   → get → 빈 버퍼! (UNDERFLOW)</text>
  <text x="450" y="124" fill="currentColor" font-family="monospace">while→ 다시 검사 → 다시 잠듦 (OK)</text>
  <text x="10" y="226" fill="currentColor">P 의 signal 은 C1 을 Ready 로 만들 뿐, 락은 경쟁이다. C2 가 먼저 잡으면 C1 이 볼 때는 다시 비어 있다.</text>
  <text x="10" y="248" fill="currentColor">거의 모든 실제 시스템이 이 Mesa 의미론이다. Hoare 의미론(깨운 즉시 대기자에게 넘김)이면 if 도 맞지만</text>
  <text x="10" y="270" fill="currentColor">spurious wakeup 까지 생각하면 POSIX 에서는 언제나 while (S2).</text>
</svg>
```

S3 을 번호표로 따라가면 (용량 1, v2 = while + CV 1개):

| 단계 | 누가 | 동작 | CV 대기 (번호표) | Ready(보류) | 버퍼 |
|---|---|---|---|---|---|
| 1 | C1 | 비었음 → wait | C1(0) | — | 0 |
| 2 | C2 | 비었음 → wait | C1(0), C2(1) | — | 0 |
| 3 | P | 1 넣고 signal → **C1** | C2(1) | C1 | 1 |
| 4 | P | 2 넣으려니 꽉 참 → wait | C2(1), P(2) | C1 | 1 |
| 5 | C1 (실행 허락) | 1 꺼내고 signal → FIFO 맨 앞 = **C2** (P 가 아니라!) | P(2) | C2 | 0 |
| 6 | C1 | 다음 get: 비었음 → wait | P(2), C1(3) | C2 | 0 |
| 7 | C2 (실행 허락) | while 재검사: 비었음 → wait | P(2), C1(3), C2(4) | — | 0 |
| 끝 | — | **세 명 모두 잠듦**, 버퍼는 비었고 P 는 넣을 2 가 있다 | | | ALL_ASLEEP |

5단계가 핵심: 버퍼를 비운 소비자는 **생산자** 를 깨워야 하는데, CV 를 하나만 쓰면 signal 이 소비자에게 갈 수 있다. while 은 여기서 아무 도움이 안 된다 — C2 는 제대로 재검사하고 제대로 다시 잠든다. 문제는 깨운 대상이다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">S3 의 5단계: 같은 signal, CV 1개 vs 2개</text>
  <rect x="20" y="45" width="330" height="120" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="32" y="68" fill="currentColor" font-weight="bold">CV 1개 (v2)</text>
  <text x="32" y="92" fill="currentColor" font-family="monospace">one: [C2(1), P(2)]</text>
  <text x="32" y="116" fill="currentColor">C1: get 후 signal(one)</text>
  <text x="32" y="140" fill="currentColor">→ C2 깨어남 → 비었음 → 다시 잠듦</text>
  <text x="32" y="158" fill="currentColor">→ P 는 영원히 잠듦</text>
  <rect x="390" y="45" width="330" height="120" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="402" y="68" fill="currentColor" font-weight="bold">CV 2개 (mine)</text>
  <text x="402" y="92" fill="currentColor" font-family="monospace">fill: [C2]   empty: [P]</text>
  <text x="402" y="116" fill="currentColor">C1: get 후 signal(empty)</text>
  <text x="402" y="140" fill="currentColor">→ 대기자는 생산자뿐 → P 가 2 를 넣고</text>
  <text x="402" y="158" fill="currentColor">  signal(fill) → C2 가 2 를 받는다</text>
  <text x="10" y="194" fill="currentColor">CV 2개면 "누가 깨어나든" 진행할 수 있다 → 깨우는 순서(구현마다 다름)에 정확성이 기대지 않는다.</text>
  <text x="10" y="216" fill="currentColor">CV 1개로 고치려면 broadcast (v2b): 모두 깨우고 각자 while 로 재검사. 맞지만 헛깨움이 많다.</text>
  <text x="10" y="238" fill="currentColor">v1(if) 은 7단계에서 C2 가 재검사 없이 빈 버퍼에서 꺼낸다 → UNDERFLOW.</text>
</svg>
```

**결과표 (solution, 실제 출력 — 매 실행 동일):**

```text
S1 steal (30.7)          v1=UNDERFLOW  v2=OK         v2b=OK         mine=OK
S2 spurious              v1=UNDERFLOW  v2=OK         v2b=OK         mine=OK
S3 wrong wakeup (30.9)   v1=UNDERFLOW  v2=ALL_ASLEEP v2b=OK         mine=OK
```

| | if 문제 (S1, S2) | 엉뚱한 깨움 (S3) | 고치는 것 |
|---|---|---|---|
| v1 if + CV1 | 깨짐 | 깨짐 (if 때문에 underflow 로 나타남) | — |
| v2 while + CV1 | 해결 | **전원 수면** | while 은 S1·S2 만 고친다 |
| v2b while + CV1 + broadcast | 해결 | 해결 (비쌈) | covering condition |
| mine while + CV2 | 해결 | 해결 | 표준 답 |

### Part B — 강제하지 않으면? (`demo_freerun.c`, 진짜 `pthread_cond`)

강제 없이 그냥 돌린 결과 (감시 스레드가 200 ms 동안 진행이 없고 전원이 잠들어 있으면 HANG 으로 기록하고 구조). 1 생산자 × 2 소비자 × 20만 개, 5회:

```text
v1 P=1 C=2 N=200000/producer:
  run 0: consumed=200000  underflow=206296 overflow=0     finished
  run 1: consumed=200000  underflow=206361 overflow=0     finished
  ... (run 2..4: underflow 188995 / 200529 / 191420, 모두 finished)
v2 P=1 C=2 N=200000/producer:
  run 0: consumed=1       underflow=0     overflow=0     HANG (all asleep) sleepers=3
  ... (run 1..4 도 똑같이 consumed=1, HANG, sleepers=3)
v3 P=1 C=2 N=200000/producer:
  run 0: consumed=200000  underflow=0     overflow=0     finished   (run 1..4 동일)
```

4 생산자 × 4 소비자 × 5만 개(3회)에서도 v2 는 매번 HANG(consumed=3, 3, 6, sleepers=8), v1 은 consumed 약 10~12만에 overflow 76277~97434, v3 는 3회 모두 200000 / 0 / 0.

관찰:

- **v2 는 이 맥에서 매번, 그것도 정확히 1개 소비 직후에** 멈췄다 — 위 S3 표 그대로다. 단계 5에서 signal 이 먼저 잠든 C2 에게 간 것으로 보아, 이 맥(macOS 26, Apple M6)의 `pthread_cond_signal` 은 관찰상 먼저 잠든 쪽을 깨운다. **POSIX 보장은 아니다** — 다른 OS 라면 수백만 번에 한 번 멈출 수도 있고, 그게 더 나쁘다(테스트를 통과하고 현장에서 멈춘다).
- **v1** 은 1×2 에서 끝까지 갔지만 underflow 가 항목 수보다 많다: 빈 버퍼에서 꺼낸 소비자도 signal 을 보내 다른 소비자를 깨우고, 그 소비자도 if 라서 또 꺼낸다 — 헛깨움의 연쇄. 4×4 에서는 생산자도 여럿이라 **overflow(덮어쓰기 = 데이터 손실) 약 9만 개**. assert 가 없는 펌웨어라면 조용히 데이터를 잃는 버그다.

### Part B — `bb_close`: 종료는 broadcast

락 안에서 `closed = true` (상태로 남긴다 — 나중에 오는 스레드도 본다) → `pthread_cond_broadcast(&fill)` + `pthread_cond_broadcast(&empty)` (잠든 소비자·생산자 전부).

`signal` 로 하면 테스트("소비자 3명이 잠든 상태에서 close → 셋 다 false")에서 한 명만 돌아오고 두 명은 2 초 제한에 걸려 FAIL 이다 (일부러 틀린 버전으로 확인함: `woke` 체크 FAIL, 하네스 구조 후 정상 종료). 규칙: **상태 변경 하나로 여러 대기자가 진행 가능해지면 broadcast.**

또 하나: 닫힌 뒤에도 `bb_get` 은 남은 항목을 준다 (`count == 0` 일 때만 false). "닫힘" 과 "비었음" 을 둘 다 봐야 종료 시 데이터를 잃지 않는다 — 로거를 끌 때 마지막 샘플이 날아가지 않게.

### Part C — 배치 꺼내기: 몇 명이 진행 가능해졌나

```c
    while (n < max && b->count > 0) out[n++] = ring_get(b);   /* 대기 부분은 bb_get 과 같다 */
    if (n > 1) pthread_cond_broadcast(&b->empty);              /* 빈 칸 n 개 → 생산자 최대 n 명 */
    else if (n == 1) pthread_cond_signal(&b->empty);
```

| 선택 | 결과 |
|---|---|
| `signal` 1번 | 빈 칸 4개인데 생산자 1명만 깨어남. 다음 get 이 올 때까지 나머지는 잠든 채 — 이 테스트에서는 영원히 (`all_in` FAIL 확인) |
| `signal` n번 | 정확하고 헛깨움이 적다. 대기자가 n 명보다 적으면 남는 signal 은 그냥 사라짐(무해) |
| `broadcast` | 정확. 대기자가 n 명보다 많으면 나머지는 while 로 다시 잠듦(헛깨움) |

실제 출력: `Part C logger: samples=60000 batches=3864 (avg 15.5 per batch)` — 로거가 느린 쪽이라 거의 항상 16개씩 꽉 채워 가져갔다. 플래시 페이지 쓰기 같은 "건당 고정비가 큰" 소비자는 배치로 고정비를 나눈다. 펌웨어에서는 DMA 로 UART 버퍼를 통째로 넘기는 것과 같은 생각이다 (DS-A02 bip buffer).

### signal vs broadcast — 헛깨움 측정 (`demo_wakeups.c`)

환경: Apple M6 (`sysctl -n hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu` → P 코어 2, E 코어 4), `Apple clang version 21.0.0 (clang-2100.0.123.102)`, `-O2`. 5회 중앙값. 헛깨움 = wait 에서 돌아왔는데 조건이 거짓이라 다시 잔 횟수.

| 구성 | 방식 | 시간 (ms) | wakeup/항목 | 헛깨움/항목 | 헛깨움 비율 |
|---|---|---|---|---|---|
| P8 C8 cap1, 20만 | A: CV2 + signal | 654.7 | 2.00 | 0.00 | 0% |
| | B: CV1 + broadcast | 1141.9 | 2.14 | 0.99 | 46% |
| | C: CV2 + broadcast | 1093.2 | 2.03 | 0.90 | 44% |
| P4 C4 cap4, 40만 | A | 1236.0 | 0.71 | 0.15 | 21% |
| | B | 942.1 | 0.83 | 0.23 | 27% |
| | C | 1138.2 | 0.83 | 0.24 | 29% |
| P1 C8 cap16, 40만 | A | 587.8 | 0.47 | 0.23 | 48% |
| | B | 479.9 | 0.48 | 0.28 | 58% |
| | C | 509.6 | 0.48 | 0.27 | 56% |

솔직한 해석:

- 용량 1 + 스레드 16개처럼 **잠든 대기자가 많은** 구성에서는 signal 이 1.7배 빠르고 헛깨움이 0 이다. broadcast 는 항목 하나마다 거의 한 번씩 헛깨움을 만든다 (thundering herd).
- 용량이 크면 대부분의 put/get 이 아예 잠들지 않아 차이가 작고, 이번 측정에서는 오히려 broadcast 쪽 시간이 짧았다. 시간은 mutex 경합과 macOS 커널(psynch)의 깨우기 비용이 지배하며 실행마다 크게 흔들린다. **"broadcast 는 항상 느리다" 는 증명되지 않았다.**
- signal 버전(A)도 헛깨움이 있다 (21%, 48%) — Mesa 가로채기(S1)가 실제로 자주 일어난다는 뜻. **while 이 이론용이 아니라는 실측 증거.**
- 결론: 정확성이 먼저, 성능은 측정해서. CV 를 나눠 깨울 대상을 정확히 하는 것이 broadcast 를 줄이는 정석이다.

### Part C — 펌웨어로 옮기면

| 호스트 (이 레슨) | FreeRTOS / Cortex-M |
|---|---|
| `bb_get` 의 while + wait | `xQueueReceive` 내부도 `for(;;)` 루프: 깨어난 뒤 다시 검사하고, 더 높은 우선순위 태스크가 먼저 가져갔으면 남은 시간으로 다시 블록 (`xTaskCheckForTimeOut`) — 연습 1 의 절대 마감 시각과 같은 생각 |
| CV 대기자 순서는 미지정 | 큐의 대기 리스트는 **우선순위 순** (같은 우선순위끼리는 먼저 온 순). 그래서 RTOS 에서는 "누가 깨어나나" 가 정해져 있다 — 그래도 우선순위 역전은 별개 문제 (RTOS-M03) |
| 생산자 스레드는 블록 가능 | ISR 은 블록 불가 → `xQueueSendFromISR` (꽉 차면 실패 반환, drop 카운트) — 연습 2. `pxHigherPriorityTaskWoken` 으로 ISR 끝에서 문맥 교환 요청 |
| `pthread_mutex` 로 링 보호 | 큐 커널 코드는 임계 구역(인터럽트 마스킹, C-M14)으로 보호. ISR ↔ 태스크 링을 직접 짠다면 SPSC lock-free (DS-A01, DS-A02) |
| `bb_close` broadcast | 태스크 종료 알림은 이벤트 그룹/태스크 알림 (RTOS-M01) 또는 큐에 "종료 메시지" |
| 배치 꺼내기 | 로거 태스크가 큐에서 여러 개를 모아 플래시 페이지/SD 블록 단위로 쓰기 |

Cortex-M 단일 코어에서 RTOS 를 쓰지 않는 베어메탈이라면 조건 변수 자체가 없다. 메인 루프가 링 버퍼를 폴링하거나 `WFI` 로 잠들고 ISR 이 깨운다 — `WFI` 직전 검사와 잠들기 사이의 틈은 위 lost wakeup 과 똑같은 문제이며, 인터럽트를 막은 채 검사하고 `WFI` 를 부르는(PRIMASK 가 걸려 있어도 대기 중 인터럽트가 WFI 를 깨운다) 패턴으로 푼다 (C-M14, BM-M04).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution
```

```text
(Part A stress 4줄, S1~S3 결과표, Part C logger 줄 — 5. 풀이에 실은 것과 같은 형식)
37/37 passed
```

8번 반복 실행 모두 `37/37 passed`. 새니타이저 (둘 다 경고 0, 종료 코드 0):

```bash
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_asan && ./so_asan
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan
```

TSan 이 깨끗하다는 것은 깨진 v1/v2 도 **데이터 레이스는 없다** 는 뜻이다 — 모든 접근이 락 안이다. 이 레슨의 버그는 전부 **로직(조건·깨우기) 버그** 라서 TSan 이 못 잡는다. 강제 스케줄 테스트가 필요한 이유다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `if (count == 0) wait(...)` | 깨어남 ≠ 조건 참. 가로채기(S1)·spurious(S2) 에서 빈 버퍼를 읽는다. 실측 v1: 4×4 에서 덮어쓰기 9만 개 | `while (count == 0) wait(...)` |
| 생산자·소비자가 CV 하나 + signal | 소비자가 소비자를 깨워 전원 수면 (S3). 이 맥에서는 매 실행 1개 만에 멈춤 | CV 2개 (`empty`/`fill`) 또는 broadcast |
| 락 없이 조건 검사 후 lock+wait | 검사와 잠들기 사이 signal 분실 + 데이터 레이스 (TSan 보고) | lock → while 검사 → wait |
| 상태 변수 없이 signal 만 (`done` 없이) | signal 은 기억되지 않는다. 먼저 signal 하고 나중에 wait 하면 영원히 잔다 (OSTEP 30.1) | 상태를 바꾸고(락 안) 그 다음 signal |
| 종료 알림을 signal 로 | 잠든 스레드 중 하나만 깨어남 | `closed = true` + broadcast (양쪽 CV) |
| 배치로 n 칸 비우고 signal 한 번 | 생산자 1명만 들어옴 | broadcast 또는 signal n 번 |
| timedwait 를 루프마다 "지금+timeout" 으로 다시 계산 | 가짜 깨움마다 마감이 늘어나 영원히 안 끝날 수 있다 | 절대 마감 시각을 처음 한 번 계산 (연습 1) |
| `ETIMEDOUT` 이면 바로 false | 시간 초과와 동시에 항목이 들어왔을 수 있다 (timedwait 는 락을 다시 잡고 돌아온다) | 루프 뒤에 조건을 한 번 더 보고 결정 |
| 락을 잡은 채 긴 작업(플래시 쓰기) | 생산자 전원이 막힌다 | 배치를 꺼내 락을 푼 뒤 쓴다 (Part C 로거) |
| macOS 에서 `sem_init` 사용 | 헤더에 `__deprecated`, 동작하지 않음 (-1/ENOSYS) | mutex+cond 로 만든 세마포어 (연습 3) 또는 `dispatch_semaphore` |

## 8. 면접 Q&A

<details>
<summary>Q1. 조건 변수 대기를 왜 while 로 감싸나요? spurious wakeup 은 실제로 일어나나요?</summary>

두 가지 이유. ① **Mesa 의미론**: signal 은 대기자를 Ready 로 만들 뿐이고 락은 다시 경쟁한다. 그 사이 다른 스레드가 조건을 거짓으로 되돌릴 수 있다(S1 — 다른 소비자가 가져감). ② **spurious wakeup**: POSIX 는 `pthread_cond_wait` 가 signal 없이 돌아올 수 있다고 명시하고, 돌아온 것이 조건에 대해 아무것도 뜻하지 않으니 다시 평가하라고 한다. 구현이 멀티코어에서 깨우기를 싸게 하려고 이 자유를 쓴다(예: signal 과 대기 진입이 경합할 때 둘 다 깨우기). 실측에서 더 중요한 건 ①이다 — 정답 코드(CV 2개 + signal)에서도 깨어난 뒤 조건이 거짓인 경우가 21~48% 나왔다(`demo_wakeups.c`). 꼬리: "if 로 충분한 경우는?" → 대기자가 하나이고 Hoare 의미론인 경우뿐인데, POSIX 에서는 spurious 때문에 그것도 안 된다. 그냥 항상 while.

</details>

<details>
<summary>Q2. CV 를 하나만 쓰면 어떤 시나리오에서 깨지나요? 고치는 방법은?</summary>

용량 1, 소비자 2, 생산자 1. C1·C2 가 잠들고, P 가 넣고 C1 을 깨운 뒤 다음 것을 넣으려다 꽉 차서 잠든다. C1 이 꺼내고 signal 하는데, 대기자 {C2, P} 중 **C2** 가 깨어나면 C2 는 빈 버퍼를 보고 다시 잠들고 C1 도 다음 get 에서 잠든다 → 셋 다 잠듦(OSTEP 그림 30.9). 이 맥에서는 강제하지 않아도 5/5 실행 모두 1개 소비 직후 멈췄다. 고침: (a) CV 2개 — 소비자는 `empty` 에만, 생산자는 `fill` 에만 signal. 각 CV 의 대기자가 모두 같은 종류라 누가 깨어나도 진행 가능. (b) CV 1개 + broadcast — 맞지만 헛깨움이 많다(8×8 cap1 에서 1.7배 느림). 꼬리: "while 이면 되지 않나?" → 아니다. 이 버그는 while 로도 남는다. 재검사는 맞게 하고 "엉뚱한 사람을 깨운" 것이 문제다.

</details>

<details>
<summary>Q3. signal 과 broadcast 는 각각 언제 쓰나요?</summary>

signal: 상태 변경 하나로 **정확히 한 명** 이 진행 가능하고, 그 CV 의 대기자가 **누구든 그 한 명이 될 수 있을 때** (항목 1개 넣음 → 소비자 아무나). broadcast: ① 여러 명이 진행 가능해졌을 때 — 종료(`closed`), 배치로 여러 칸 비움, 배리어 통과(연습 4). ② 누가 진행 가능한지 깨우는 쪽이 모를 때 — covering condition (OSTEP 30.3, 크기가 다른 메모리 요청: free(50) 은 100 을 원하는 쪽이 아니라 10 을 원하는 쪽을 깨워야 한다, 연습 5). OSTEP 의 경고: "broadcast 로 바꾸니까 된다" 면 대개 CV 설계 버그다. 꼬리: "broadcast 의 비용?" → thundering herd. 대기자 N 명이 전부 깨어나 같은 mutex 로 몰리고 N-1 명은 다시 잔다.

</details>

<details>
<summary>Q4. signal 은 mutex 를 잡고 해야 하나요, 풀고 해야 하나요?</summary>

POSIX 는 둘 다 허용한다. 다만 "예측 가능한 스케줄링이 필요하면" 잡고 하라고 한다. 핵심 조건은 **상태 변경은 반드시 락 안에서** 라는 것 — 그래야 대기자의 "검사 → 잠들기" 가 그 변경과 원자적으로 엇갈리지 않는다. 락 안에서 signal: 추론이 쉽다. 깨어난 스레드가 곧바로 락에 막힐 수 있지만 많은 구현이 이를 최적화한다(wait morphing). 락 밖에서 signal: 그 막힘이 줄어든다. 단, 락 밖으로 나간 사이 다른 스레드가 CV 나 버퍼를 파괴하는 경우(예: 종료 후 `pthread_cond_destroy`)를 조심해야 한다. 면접 답: "상태는 락 안에서, signal 은 어느 쪽이든 맞지만 저는 락 안에서 합니다, 이유는…".

</details>

<details>
<summary>Q5. wait 에 왜 mutex 를 넘기나요? 그냥 unlock 하고 wait 하면 안 되나요?</summary>

`unlock(); wait();` 으로 나누면 그 사이에 생산자가 넣고 signal 할 수 있고, 그 signal 은 아직 아무도 안 잠들었으니 사라진다 → lost wakeup. `pthread_cond_wait(cv, m)` 은 "m 풀기 + cv 에 잠들기" 를 다른 스레드가 m 을 잡는 것에 대해 원자적으로 한다. `demo_lost_wakeup.c` 에서 락 없이 검사한 버전은 `count=1` 인데 1초 타임아웃을 다 기다렸고, TSan 은 `count` 데이터 레이스를 보고했다. 같은 문제가 베어메탈의 "플래그 검사 → `WFI`" 사이에도 있다(인터럽트를 막고 검사, 대기 중인 인터럽트가 WFI 를 깨움). 꼬리: "세마포어는 이 문제가 없나?" → 세마포어는 값(카운트)을 기억하므로 post 가 먼저 와도 잃지 않는다. CV 는 기억이 없어서 반드시 상태 변수와 같이 쓴다.

</details>

<details>
<summary>Q6. 이 버그들은 ThreadSanitizer 로 잡히나요? 어떻게 테스트하죠?</summary>

거의 안 잡힌다. 깨진 v1/v2 도 모든 접근이 락 안이라 데이터 레이스가 없다 — 이 레슨의 solution(깨진 버전 포함)이 TSan 에서 경고 0 이다. TSan 이 잡는 건 락 없이 검사하는 실수(lost wakeup 데모)처럼 레이스가 낀 경우뿐이다. 로직 버그는 ① 스트레스 + 불변식 검사(정확히 한 번, 순서, underflow/overflow 카운터) ② 워치독(전원 수면 감지) ③ **강제 스케줄** — 테스트용 CV 로 깨우는 순서와 실행 시점을 고정해 OSTEP 의 나쁜 인터리빙을 결정적으로 재현 ④ 모델 검사(TLA+, CBMC, 혹은 모든 인터리빙을 도는 도구)로 잡는다. 꼬리: "강제 스케줄이 실제를 반영하나?" → 고른 선택(FIFO, 늦게 실행)이 POSIX 가 허용하는 것이면 실제 스케줄러가 언젠가 그렇게 할 수 있다. 이 맥에서는 v2 가 강제 없이도 매번 그 스케줄로 멈췄다.

</details>

<details>
<summary>Q7. 큐에 타임아웃을 넣으려면? macOS 에서 주의할 점은?</summary>

`pthread_cond_timedwait(cv, m, &abstime)` — **절대 시각** 을 받는다. 마감은 처음 한 번 계산하고 루프 안에서 재사용한다(가짜 깨움마다 다시 계산하면 끝나지 않을 수 있다). `ETIMEDOUT` 이 와도 락을 다시 잡은 상태이므로 조건을 한 번 더 보고, 그 사이 항목이 들어왔으면 받는다. 리눅스에서는 `pthread_condattr_setclock(CLOCK_MONOTONIC)` 으로 시계 변경(NTP, 사용자 설정)에 안전하게 만들지만, **macOS SDK 에는 이 함수가 없다** (헤더 확인). 대신 `pthread_cond_timedwait_relative_np` (상대 시간)가 있다. 이 레슨 연습 1 은 이식성 때문에 `CLOCK_REALTIME` 절대 시각을 썼다. FreeRTOS `xQueueReceive` 도 같은 구조: 깨어난 뒤 `xTaskCheckForTimeOut` 으로 남은 tick 을 다시 계산해 재블록한다.

</details>

## 9. 추가 연습

### 연습 1 — 타임아웃 get

`bool bb_get_timeout(bb_t *b, int *out, int ms)` — ms 안에 항목이 없으면 false. 기다리는 동안 상태 변화 없는 broadcast 를 5번 받아도 마감은 늘어나지 않아야 하고, 기다리는 중에 들어온 항목은 받아야 한다.

<details>
<summary>답 보기</summary>

마감 시각을 `clock_gettime(CLOCK_REALTIME)` + ms 로 **처음 한 번** 계산하고, `while (count == 0 && !closed && rc != ETIMEDOUT) rc = pthread_cond_timedwait(&fill, &m, &dl);` 루프 뒤에 `count` 를 다시 보고 결정한다 (시간 초과와 동시에 온 항목은 받는다). `tv_nsec` 정규화(10억 이상이면 올림)를 빼먹으면 `EINVAL` 이다. 테스트는 시간을 재지 않는다: 빈 버퍼 → false, 있으면 대기 없이 true, 가짜 깨움 5번 뒤에도 항목 없이 false, 대기 중 put → true. macOS 에 `pthread_condattr_setclock` 이 없어 REALTIME 기준이라는 점은 Q7.

</details>

### 연습 2 — ISR 생산자: 기다리지 않는 put

`bool bb_try_put(bb_t *b, int v)` — 꽉 찼으면 기다리지 않고 `drops++` 후 false. "ISR" 스레드가 10만 개를 쏟아붓고 로거가 받을 때, 받은 것 + 버린 것 = 보낸 것, 받은 순서 증가.

<details>
<summary>답 보기</summary>

```c
pthread_mutex_lock(&b->m);
if (b->count == b->cap) { b->drops++; pthread_mutex_unlock(&b->m); return false; }
ring_put(b, v); pthread_cond_signal(&b->fill); pthread_mutex_unlock(&b->m); return true;
```

실제 출력: `try_put: received=159 dropped=99841` (용량 8, 실행마다 다름 — 체크는 합계만). 생산자가 소비자보다 압도적으로 빠르면 대부분 버려진다 — 이게 ISR 의 현실이고, 그래서 **drop 카운터를 노출** 하는 것이 중요하다. 진짜 ISR 은 mutex 도 못 잡는다(블록 가능) → RTOS 는 `xQueueSendFromISR` 가 인터럽트 마스킹 임계 구역으로 큐를 보호하고, 직접 짠다면 ISR→태스크 SPSC 링 (DS-A01/DS-A02) + 태스크 알림으로 깨운다 (RTOS-M01).

</details>

### 연습 3 — 세마포어를 mutex + cond 로, 그리고 세마포어 버퍼

macOS 에서는 `sem_init` 이 deprecated 이고 동작하지 않는다. `csem_wait/csem_post` 를 만들고, 그것으로 OSTEP 31장의 버퍼(빈 칸 세마포어 `empty`, 찬 칸 세마포어 `full`, 링 보호 mutex)를 만들어 4×4 스트레스를 통과시켜라.

<details>
<summary>답 보기</summary>

`csem_wait`: lock → `while (v <= 0) wait` → `v--` → unlock. `csem_post`: lock → `v++` → signal → unlock (값이 1 늘면 한 명만 진행 가능 → signal 로 충분). 버퍼 put = `csem_wait(&empty); lock; 넣기; unlock; csem_post(&full);`. **순서 주의:** 락을 먼저 잡고 `csem_wait(&empty)` 하면 꽉 찼을 때 락을 쥔 채 잠들어 소비자가 영원히 못 들어온다 — OSTEP 31장의 교착 예. 세마포어는 값을 기억하므로 Q5 의 lost wakeup 이 없다. 체크: 4×4×1만 개 정확히 한 번, 끝나면 `empty.v == cap`, `full.v == 0`.

</details>

### 연습 4 — 재사용 배리어 (macOS 에는 `pthread_barrier_t` 가 없다)

`barrier_wait(b)` — n 명이 모두 도착하면 함께 통과, 마지막 도착자만 true. 같은 배리어를 2000 라운드 연속 써도 섞이지 않아야 한다.

<details>
<summary>답 보기</summary>

도착할 때 `my_gen = gen` 을 기억하고, 마지막 도착자는 `arrived = 0; gen++; broadcast`, 나머지는 `while (my_gen == gen) wait`. `while (arrived < n)` 으로 기다리면 안 된다: 마지막 도착자가 `arrived = 0` 으로 리셋하는 순간 깨어난 스레드가 그 조건을 다시 보면 "아직 0 < n" 이라 다시 잠든다. 그래서 **세대 번호** 를 조건으로 쓴다 — 내가 들어온 세대가 끝났는가. broadcast 인 이유: 모두가 진행 가능해진다. 체크: 4 스레드 × 2000 라운드, 매 라운드 모두가 같은 라운드 값을 보고(불일치 0), 마지막 도착자는 배리어마다 정확히 1명.

</details>

### 연습 5 — covering condition (OSTEP 30.3)

`heap_alloc(h, n)` 은 남은 바이트가 n 이상이 될 때까지 기다리고, `heap_free(h, n)` 은 돌려준다. Ta 가 100, Tb 가 10 을 원하며 잠든 상태에서 free(50) 하면 Tb 만 진행하고 Ta 는 계속 기다려야 한다.

<details>
<summary>답 보기</summary>

`heap_free` 에서 broadcast. signal 이면 (FIFO 구현에서) 먼저 잠든 Ta 가 깨어나 50 < 100 을 보고 다시 잠들고, 진행 가능한 Tb 는 잠든 채 남는다. free 하는 쪽은 누가 진행 가능한지 모르므로 모두 깨워 각자 while 로 판단하게 한다. CV 를 요청 크기별로 나누는 건 비현실적이다. 체크: free(50) 뒤 Tb 완료, Ta 대기(`waiting == 1`), 남은 바이트 40. free(60) 뒤 Ta 완료, 남은 0.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice    # 15/15 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_asan && ./pr_asan   # 15/15, 경고 0
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_tsan && ./pr_tsan       # 15/15, 보고 없음
```

## 10. 핵심 정리

- 조건 변수 = "상태가 바뀔 때까지 잠들기". 모양은 항상 **lock → while(못 함) wait → 상태 변경 → signal/broadcast → unlock**.
- `wait(cv, m)` 은 m 풀기와 잠들기를 원자적으로 하고, 깨어나면 m 을 다시 잡고 돌아온다. 검사·상태 변경을 락 밖에서 하면 lost wakeup(실측: count=1 인데 1초 대기) + 데이터 레이스.
- **while**: Mesa 의미론(깨어남 ≠ 조건 참, 실측 헛깨움 21~48%) + POSIX spurious wakeup. if 버전은 가로채기/가짜 깨움에서 빈 버퍼를 읽는다(4×4 에서 덮어쓰기 9만 개).
- **CV 2개(empty/fill)**: CV 하나 + signal 은 소비자가 소비자를 깨워 전원 수면(OSTEP 30.9). 이 맥에서는 강제 없이도 매 실행 1개 만에 멈췄다. while 로는 못 고친다.
- **broadcast**: 여러 명이 진행 가능(종료, 배치, 배리어)하거나 누가 가능한지 모를 때(covering condition). 그 외엔 signal — 대기자가 많을 때 broadcast 는 헛깨움 폭증(8×8 cap1: 1.7배 느림).
- signal 은 기억되지 않는다 → 반드시 상태 변수와 같이. 세마포어는 기억한다.
- 이 버그들은 락 안의 로직 버그라 TSan 이 못 잡는다 → 불변식 스트레스 + 워치독 + 강제 스케줄로 테스트.
- 펌웨어: RTOS 큐가 이 구조(재검사 루프 + 타임아웃 재계산, 우선순위 순 대기 리스트), ISR 은 블록 불가 → FromISR/try_put + drop 카운터.

## 참고자료

- OSTEP 30장 *Condition Variables* — 그림 30.6(CV 1개 + if), 30.7(깨진 트레이스 v1), 30.8(CV 1개 + while), 30.9(전원 수면 트레이스), 30.10(CV 2개), 30.12(최종 해법), 30.3 covering condition / 그림 30.13: <https://pages.cs.wisc.edu/~remzi/OSTEP/threads-cv.pdf> (로컬: `operating-system-three-easy-pieces/book-md/C30_condition_variables.md`)
- OSTEP 31장 *Semaphores* — 세마포어 bounded buffer와 락 순서 교착 (연습 3): <https://pages.cs.wisc.edu/~remzi/OSTEP/threads-sema.pdf>
- POSIX `pthread_cond_wait` / `pthread_cond_timedwait` — mutex 를 잡고 호출, 원자적 해제·대기, spurious wakeup 과 조건 재평가: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_cond_wait.html>
- POSIX `pthread_cond_signal` / `pthread_cond_broadcast` — "적어도 하나", 락 없이 호출 가능하지만 예측 가능한 스케줄이 필요하면 락을 잡고: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_cond_signal.html>
- C11 (N1570) 5.1.2.4 — 데이터 레이스 정의(p25): <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Lampson & Redell, *Experience with Processes and Monitors in Mesa* (CACM 1980) — Mesa 의미론, covering condition
- C.A.R. Hoare, *Monitors: An Operating System Structuring Concept* (CACM 1974)
- FreeRTOS — Queues (`xQueueReceive`, `xQueueSendFromISR`): <https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/02-Queues-mutexes-and-semaphores/01-Queues>
- Zephyr — Condition Variables (`k_condvar`): <https://docs.zephyrproject.org/latest/kernel/services/synchronization/condvar.html>
- Clang ThreadSanitizer: <https://clang.llvm.org/docs/ThreadSanitizer.html>
- 관련 레슨: OS-B04 (counter++ 레이스), OS-B05 (mutex·경합 비용), TOOL-M09 (TSan 리포트 읽기), OS-M02 (컨텍스트 스위치 비용), OS-M09 (reader-writer lock), OS-M10 (교착), C-M14 (ISR 공유 데이터·임계 구역), C-A03 (atomics·acquire/release), RTOS-B08/B09 (세마포어), RTOS-B11 (메시지 큐), RTOS-M01 (태스크 알림), RTOS-M03 (우선순위 역전), RTOS-M06 (블로킹 큐 구현), BM-M04 (ISR + 링버퍼 UART), DS-B03 (링 버퍼 Full/Empty), DS-A01 (lock-free SPSC), DS-A02 (ISR↔태스크 링, bip buffer)
