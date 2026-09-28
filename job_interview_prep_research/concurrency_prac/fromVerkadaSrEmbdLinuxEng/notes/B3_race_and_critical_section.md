# B3. 경쟁 상태와 임계구역 — 무엇을 지키는가

> **이 노트를 읽고 나면**
> - `counter++`가 왜 안전하지 않은지 기계어 단위 타임라인으로 그릴 수 있고, 실제로 깨지는 것을 재현할 수 있다
> - data race와 race condition을 구별하고, "락은 변수가 아니라 **불변식**을 지킨다"를 예로 설명할 수 있다
> - check-then-act(TOCTOU)를 찾아내 조건부 행동 API로 고칠 수 있고, 공유를 줄이는 4가지 전략을 댈 수 있다
>
> **선행**: [B1. 스레드와 스케줄러](B1_threads_and_scheduler.md) · [B2. pthread 실전](B2_pthread_api.md)
>
> **이 개념을 쓰는 문제**: 10문제 전부의 Part 1. 불변식은 `01_gps_fix_cache`(GpsFix의 lat/lon/hdop/timestamp가 한 fix여야 한다)와 `02_modem_rssi_window`(값과 시각이 한 쌍이어야 한다)가 직접 묻는다. check-then-act는 `03_event_tailer_shutdown`의 `evq_try_pop`과 `04_temp_single_flight`의 in-flight 판단이 함정이다.

## 1. 왜 이게 필요한가

[`01_gps_fix_cache`](../01_gps_fix_cache/question_note)의 지문에 이런 문장이 있다.
```
"Coherent" is the whole point: *out must be a single fix as the module emitted it.
Handing back last second's lat with this second's lon is the bug that makes a
geofence alert fire in the wrong county.
```

여기서 지켜야 하는 것은 `lat`이라는 **변수**가 아니다. "`lat`과 `lon`과 `hdop`과 `timestamp`가 **같은 하나의 fix에서 왔다**"는 **관계**다. 변수 하나하나를 원자적으로 만들어도 이 관계는 지켜지지 않는다. 그 관계에 이름을 붙이는 것이 이 노트의 주제고, 그 이름이 **불변식(invariant)**이다. 동시에 초보자는 "그래서 얼마나 자주 깨지는데?"에 감이 없다. 그래서 이 노트는 깨지는 것을 **직접 돌려서 숫자로 본다** — 2000만 번 읽어 860만 번 찢어진 출력을 §4.5에서 인용한다.

## 2. 그림으로 먼저

### `counter++` 하나가 사라지는 타임라인 (ASCII)
```
시간 →    스레드 A                     스레드 B                    메모리의 counter
  1      load  r0 <- counter (5)                                        5
  2                                   load  r0 <- counter (5)           5
  3      add   r0 = 6                                                   5
  4                                   add   r0 = 6                      5
  5      store counter <- 6                                             6
  6                                   store counter <- 6                6   ← 증가 하나가 사라졌다
```

두 번 증가했는데 5 → 6이다. A와 B의 `r0`은 **각자의 레지스터**(B1 §2의 "전용" 영역)라서 서로 안 보인다. 싱글코어에서도 2번과 3번 사이에 컨텍스트 스위치가 나면 똑같이 깨진다.

### 같은 것을 그림으로 — 그리고 임계구역이 무엇을 바꾸는가
```svg
<svg viewBox="0 0 660 320" role="img" aria-label="락 없는 증가와 임계구역으로 묶은 증가 비교">
  <text x="12" y="20" class="lbl">(1) 락 없음 — 두 read-modify-write 가 겹친다</text>
  <text x="14" y="48" class="lbl">A</text><line class="muted" x1="34" y1="42" x2="640" y2="42"/>
  <rect class="box" x="60" y="28" width="66" height="28" rx="4"/><text x="93" y="47" text-anchor="middle" class="lbl">load 5</text>
  <rect class="box" x="300" y="28" width="66" height="28" rx="4"/><text x="333" y="47" text-anchor="middle" class="lbl">add</text>
  <rect class="fill-soft" x="430" y="28" width="80" height="28" rx="4"/><text x="470" y="47" text-anchor="middle" class="lbl">store 6</text>
  <text x="14" y="96" class="lbl">B</text><line class="muted" x1="34" y1="90" x2="640" y2="90"/>
  <rect class="box" x="170" y="76" width="66" height="28" rx="4"/><text x="203" y="95" text-anchor="middle" class="lbl">load 5</text>
  <rect class="box" x="380" y="76" width="66" height="28" rx="4"/><text x="413" y="95" text-anchor="middle" class="lbl">add</text>
  <rect class="fill-soft" x="540" y="76" width="80" height="28" rx="4"/><text x="580" y="95" text-anchor="middle" class="lbl">store 6</text>
  <line class="dash" x1="126" y1="42" x2="203" y2="76"/>
  <text x="210" y="124" class="lbl">B 가 A 의 중간 상태를 읽었다 → 증가 하나가 사라진다 (결과 6, 기대 7)</text>
  <line class="accent" x1="12" y1="148" x2="648" y2="148"/>
  <text x="12" y="176" class="lbl">(2) 임계구역으로 묶음 — 겹칠 수 있는 타임라인이 사라진다</text>
  <text x="14" y="208" class="lbl">A</text><line class="muted" x1="34" y1="202" x2="640" y2="202"/>
  <rect class="fill-soft" x="60" y="186" width="230" height="30" rx="5"/><text x="175" y="206" text-anchor="middle" class="lbl">lock · load add store · unlock</text>
  <text x="14" y="260" class="lbl">B</text><line class="muted" x1="34" y1="254" x2="640" y2="254"/>
  <rect class="box" x="60" y="238" width="230" height="30" rx="5"/><text x="175" y="258" text-anchor="middle" class="lbl">lock() 에서 대기</text>
  <rect class="fill-soft" x="300" y="238" width="230" height="30" rx="5"/><text x="415" y="258" text-anchor="middle" class="lbl">lock · load add store · unlock</text>
  <line class="dash" x1="290" y1="202" x2="300" y2="238"/>
  <text x="60" y="296" class="lbl">한 번에 하나만 들어간다 = 상호 배제(mutual exclusion). 결과는 항상 7</text>
</svg>
```

## 3. 개념 (용어를 하나씩)

### 3.1 data race — 언어 규격이 금지하는 것

**정의**: 두 스레드가 **같은 메모리 위치**를 **동기화 없이 동시에** 접근하고, 그중 **하나 이상이 쓰기**인 상태. C11 규격(5.1.2.4)은 이것을 **undefined behavior**로 못 박았다.

**왜 UB인가**: 컴파일러는 "data race 없음"을 전제로 최적화한다. 전제가 깨지면 최적화 결과가 보장하는 게 없어진다. 값이 틀리는 게 아니라 **프로그램이 의미를 잃는다.** §4.1에서 컴파일러가 100만 번의 루프를 하나의 `add`로 접는 것을 본다. **주의**: `volatile`은 data race를 고치지 않는다. "이 접근을 생략하거나 합치지 마라"일 뿐이고 원자성도 순서 보장도 주지 않는다. 베어메탈에서 `volatile`로 충분했던 것은 인터럽트를 끌 수 있었고 접근이 워드 단위였기 때문이다. 여기서는 둘 다 성립하지 않는다.

### 3.2 race condition — 논리가 타이밍에 의존하는 것

**정의**: 실행 타이밍(스레드가 끼어드는 순서)에 따라 **결과가 달라지는 논리 버그**. data race는 **메모리 접근에 대한 규격 위반**이고 race condition은 **프로그램 의미에 대한 설계 결함**이다. 둘은 서로를 함의하지 않는다.

| | data race | race condition |
| --- | --- | --- |
| 정의 | 동기화 없는 동시 접근, 하나 이상 쓰기 | 타이밍에 따라 결과가 달라지는 논리 버그 |
| 결과 | undefined behavior | "틀린 답"이지만 정의된 동작 |
| 예 | 락 없는 `counter++` (§4.1) | check-then-act (§4.4). 모든 접근이 락 안인데 결과가 틀리다 |
| 도구로 잡히나 | `-fsanitize=thread`가 잡는다 | 잡히지 않는다. 사람이 불변식으로 논증해야 한다 |

**data race 없이 race condition만**: §4.4의 `toctou.c`. 모든 함수가 mutex를 제대로 쓰므로 TSan은 아무것도 보고하지 않는데, 빈 큐에서 pop 하는 일이 199번 일어난다. **race condition 없이 data race만**: 통계 카운터를 락 없이 올리는 것. "대략 맞으면 된다"가 요구사항이면 논리 버그는 아니지만 여전히 UB이므로 `_Atomic`(relaxed)으로 써야 한다.

### 3.3 불변식 (invariant) — 락이 실제로 지키는 것

**정의**: 프로그램이 **항상 참이라고 약속하는 문장**. 임계구역 안에서는 잠깐 깨질 수 있지만, 임계구역을 나가는 순간에는 반드시 참이어야 한다. 이 개념이 필요한 이유는 "무엇을 락으로 감싸야 하나?"에 답할 수 있는 유일한 방법이기 때문이다. 변수 단위로 생각하면 "각 변수를 원자적으로 만들자"는 틀린 결론이 나온다. `01_gps_fix_cache`의 불변식은 이렇게 쓴다.
```
어떤 reader 가 어느 순간에 보는 (lat, lon, hdop, timestamp) 네 값은
모듈이 한 번에 내보낸 하나의 fix 에서 나온 것이어야 한다.
```

네 필드를 각각 `_Atomic`으로 만들어도 이 문장은 **지켜지지 않는다.** 필드 단위 원자성은 "각 필드가 찢어진 비트를 보이지 않음"만 보장하고, 네 값이 **같은 세대**라는 것은 보장하지 않는다. 그래서 답은 "네 필드를 한 임계구역에서 함께 쓰고 함께 읽는다"(mutex) 또는 "세대에 번호를 붙인다"(seqlock)다.

| 문제 | 불변식 |
| --- | --- |
| `01_gps_fix_cache` | lat/lon/hdop/timestamp는 한 fix에서 온다 |
| `02_modem_rssi_window` | RSSI 값과 그 값을 읽은 시각은 한 쌍이다 (staleness 판단이 여기 걸린다) |
| `03_event_tailer_shutdown` | `received == popped + dropped + depth` (`evq_stats`가 한 락 안에서 다섯 필드를 다 복사하는 이유) |

### 3.4 임계구역 (critical section)

**정의**: 한 번에 **최대 한 스레드만** 실행할 수 있어야 하는 코드 구간. 그 안에서 불변식이 잠깐 깨진다. 두 규칙이 있다.

- **규칙 1 — 한 번에 하나.** 같은 데이터를 만지는 **모든** 경로가 같은 락을 잡아야 한다. 하나라도 빠지면 상호 배제가 아니다. 읽기만 하는 경로도 포함이다.
- **규칙 2 — 최대한 짧게.** 임계구역이 길면 다른 스레드가 그만큼 기다린다. 우리 문제에서는 이게 곧 "getter가 블로킹하지 않는다"는 요구사항이다. 모범답안의 임계구역은 전부 구조체 복사 한 번이나 경계 있는 루프 하나다.

**안에서 하면 안 되는 것 3가지.**
- **블로킹 호출** — 벤더 호출, 파일/소켓 I/O, `sleep`. `evsrc_read_blocking()`을 락 안에서 부르면 몇 분간 모든 소비자가 멈춘다. 그래서 `03_event_tailer_shutdown/event_queue_solution.c`의 샘플러는 벤더에서 **받아온 다음에** 락을 잡는다.
- **`malloc`/`free`와 로깅** — 둘 다 내부에 자기 락이 있어 락 순서 문제(deadlock)를 만들고 지연이 예측 불가다. 할당은 임계구역 **밖에서** 하고 포인터만 안에서 교체한다.
- **콜백/사용자 코드 호출** — 그 코드가 어떤 락을 잡을지 알 수 없다. 내 락을 잡은 채 남의 락을 잡는 순간 락 순서가 전역 문제가 된다. 콜백은 락을 놓은 뒤에 부른다.

### 3.5 락은 코드가 아니라 데이터에 붙는다

자주 하는 착각: "이 함수를 락으로 감쌌으니 안전하다." 락은 **데이터**를 지킨다. 같은 데이터를 만지는 다른 함수가 락을 안 잡으면 그 데이터는 보호되지 않는다. 그래서 설계할 때는 락마다 이런 문장을 적는다 — `event_queue_solution.c`의 주석을 옮긴 것이다.
```
g_q.lock   은 g_q 의 buf/head/tail/stopping/received/dropped/popped/errors 를 지킨다.
g_cnt.lock 은 g_cnt 의 n/cur/primed/unknown 을 지킨다.
서로 다른 상태이므로 락도 따로 둔다 — 카운팅 질의가 큐를 비우는 소비자 뒤에 줄 서지 않게.
```
"이 락이 무엇을 지키는지" 한 줄로 못 쓰면 설계가 덜 된 것이다.

### 3.6 check-then-act / TOCTOU

**정의**: 조건을 **확인(check)**하고 그 결과에 따라 **행동(act)**하는데, 확인과 행동 사이에 락이 풀려 있는 패턴. 보안 쪽에서는 TOCTOU(Time-Of-Check to Time-Of-Use)라고 부른다. 확인의 결과는 **확인한 순간의 사실**이고, 락을 놓으면 그 사실은 즉시 과거가 된다.
```c
if (q_size() > 0)   /* CHECK — 락을 잡았다가 놓는다 */
    q_pop();        /* ACT   — 이미 남이 가져갔을 수 있다 */
```

두 함수 각각은 완벽하게 thread-safe 하다. 그런데 프로그램은 틀렸다. **thread-safe 한 함수를 모아도 thread-safe 한 프로그램이 되지 않는다.**

**고치는 법 — 조건부 행동 API.** 확인과 행동을 한 임계구역에 넣고 하나의 함수로 만든다. 반환값으로 "했는지 못 했는지"를 알린다.
```c
int q_try_pop(int *out) {        /* check 와 act 가 같은 락 안에 있다 */
    pthread_mutex_lock(&m);
    int ok = (depth > 0);
    if (ok) { depth--; *out = buf[tail++]; }
    pthread_mutex_unlock(&m);
    return ok ? 0 : -1;          /* 못 했으면 -1. 호출자가 미리 물어볼 필요가 없다 */
}
```

이것이 `event_queue.h`의 `evq_try_pop()`이 `int`를 반환하는 이유다. `evq_size()` 같은 함수를 먼저 부르게 만드는 API는 사용자를 TOCTOU로 유도한다. `04_temp_single_flight`의 "이미 in-flight 인가?"도 같은 함정이다 — 확인하고 나서 요청을 보내면 중복 요청이 나간다. 확인과 등록이 한 임계구역에 있어야 한다.

### 3.7 thread-safe / reentrant / async-signal-safe

| | 뜻 | 어떻게 달성하나 | 예 |
| --- | --- | --- | --- |
| thread-safe | 여러 스레드가 동시에 불러도 정의된 대로 동작한다 | 내부 락 또는 공유 상태 없음 | `evq_try_pop()`, `malloc` |
| reentrant | 실행 중에 **다시 자기 자신이 불려도** 안전하다 (재귀·시그널·콜백) | 공유/static 상태를 아예 안 쓴다 | `strtok_r`, 순수 함수 |
| async-signal-safe | **시그널 핸들러 안에서** 불러도 안전하다 | 락을 안 쓰고, POSIX가 허용한 함수만 | `write`, `sig_atomic_t` 대입 |

reentrant 이면 대체로 thread-safe 하다(공유 상태가 없으니까). 역은 성립하지 않는다. 실무에서 가장 위험한 조합은 **thread-safe 하지만 async-signal-safe 하지 않은** 경우다. `malloc`은 내부 락으로 thread-safe 하지만, 락을 잡은 스레드에서 시그널이 떠서 핸들러가 다시 `malloc`을 부르면 **자기 자신을 기다리는 deadlock**이 된다. 같은 이유로 핸들러에서 `printf`도 금지다. 우리 문제 세트가 요구하는 것은 **thread-safe**다. 다만 `gps_wait_for_fix()`처럼 *"NOT thread-safe ... exactly one thread may ever call it"*인 벤더 함수가 왜 그런지(내부 static 상태) 알아야 설계가 나온다.

### 3.8 공유를 줄이는 4가지 전략

락은 마지막 수단이다. 공유가 없으면 race도 없다.

| 전략 | 어떻게 | 대가 | 이 폴더의 예 |
| --- | --- | --- | --- |
| 불변 객체 (immutable) | 만든 뒤 절대 안 바꾼다. 바꾸려면 새로 만들어 포인터를 교체 | 메모리, "누가 아직 보나" 문제 | `06_config_publish_rollback` (refcount) |
| 소유권 이전 (ownership transfer) | 한 번에 **한 스레드만** 그 데이터의 주인. 넘길 때만 동기화 | 넘기는 지점을 설계해야 한다 | **10문제 전부** |
| 스레드 한정 (confinement) | 특정 데이터는 특정 스레드만 만진다 | 그 스레드가 병목이 될 수 있다 | 벤더의 static 상태 → 샘플러 1개에 가둔다 |
| 스레드 로컬 (TLS) | `_Thread_local`로 스레드마다 사본 | 합산이 필요하면 따로 모아야 한다 | 하네스의 per-reader 통계 |

**이 문제 세트의 골격은 전부 "소유권 이전"이다.** 샘플러가 벤더에서 받은 값을 **자기 것으로** 들고 있다가 publish 지점(큐 push, seqlock 쓰기, 포인터 교체) **한 곳에서만** 소비자에게 넘긴다. 그래서 동기화가 필요한 코드가 전체에서 몇 줄뿐이다. 면접에서 말할 문장 (영어): *"I don't share the vendor's data — I transfer ownership of it. The sampler owns each sample until it publishes, and publishing is the only place that needs synchronization."*

## 4. 코드로 보기

### 4.1 200만이 안 나오는 프로그램 — 실제로 돌린 결과
```c
/* race.c */
#include <pthread.h>
#include <stdio.h>
#define N 1000000
static long counter = 0;          /* 공유, 보호 없음 */
static void *bump(void *arg) {
    (void)arg;
    for (int i = 0; i < N; i++)
        counter++;                /* load -> add -> store */
    return NULL;
}
int main(void) {
    pthread_t a, b;
    pthread_create(&a, NULL, bump, NULL);
    pthread_create(&b, NULL, bump, NULL);
    pthread_join(a, NULL);  pthread_join(b, NULL);
    printf("expected %d, got %ld, lost %ld\n", 2*N, counter, (long)(2*N) - counter);
    return 0;
}
```

문제 세트와 같은 플래그(`-O2`)로 먼저 돌리고, 이어서 어셈블리를 본다.
```
$ cc -std=c11 -O2 -Wall -Wextra -pthread -o race2 race.c && ./race2 ; ./race2 ; ./race2
expected 2000000, got 2000000, lost 0
expected 2000000, got 2000000, lost 0
expected 2000000, got 2000000, lost 0
$ cc -std=c11 -O2 -S -o - race.c | sed -n '/^_bump:/,/ret/p'
_bump:
	adrp	x8, _counter@PAGE
	ldr	x9, [x8, _counter@PAGEOFF]
	add	x9, x9, #244, lsl #12      ; =999424
	add	x9, x9, #576
	str	x9, [x8, _counter@PAGEOFF]
	ret
```

**정답이 나왔는데 이게 더 나쁜 결과다.** 루프가 없다 — 컴파일러가 100만 번의 증가를 **load 한 번 + add 한 번 + store 한 번**으로 접었다. data race가 UB이므로 컴파일러는 "다른 스레드가 중간 상태를 본다"는 가능성을 고려할 의무가 없다. 2000000이 나온 것은 한 스레드가 끝난 뒤에 다른 스레드가 시작했기 때문이고, 겹치면 1000000이 나온다. **UB는 "틀린 값"이 아니라 "아무 값"이다.** `-O0`으로 최적화를 끄거나 `counter`를 `volatile long`으로만 바꿔 `-O2`로 돌리면 원래 보여주려던 모습이 나온다.
```
$ cc -std=c11 -O0 -Wall -Wextra -pthread -o race0 race.c && ./race0 ; ./race0 ; ./race0
expected 2000000, got 1063047, lost 936953
expected 2000000, got 1034424, lost 965576
expected 2000000, got 1006139, lost 993861
$ ./racev ; ./racev ; ./racev          # volatile long, -O2
expected 2000000, got 1007185, lost 992815
expected 2000000, got 1000164, lost 999836
expected 2000000, got 1007365, lost 992635
```

**200만 중 100만 가까이 사라졌다.** 두 스레드가 거의 항상 서로의 중간 상태를 읽고 있다는 뜻이다. 그리고 **`volatile`은 손실을 하나도 줄이지 못했다** — 컴파일러의 최적화만 막았을 뿐이다.

### 4.2 도구로 잡기 — ThreadSanitizer
```
$ cc -std=c11 -O1 -g -Wall -Wextra -pthread -fsanitize=thread -o race_tsan race_v.c && ./race_tsan
WARNING: ThreadSanitizer: data race (pid=8763)
  Write of size 8 at 0x000102b88000 by thread T2:
    #0 bump race_v.c:11
  Previous write of size 8 at 0x000102b88000 by thread T1:
    #0 bump race_v.c:11
  Location is global 'counter' at 0x000102b88000
SUMMARY: ThreadSanitizer: data race race_v.c:11 in bump
expected 2000000, got 1024494, lost 975506
```

줄 번호까지 찍어준다. SPEC.md가 모범답안을 `-fsanitize=thread`로도 돌려보라고 하는 이유다. **단, 이건 data race만 잡는다** — §4.4는 조용히 통과한다.

### 4.3 고치기
```c
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static void *bump(void *arg) {
    (void)arg;
    for (int i = 0; i < N; i++) {
        pthread_mutex_lock(&m);      /* 임계구역 시작 */
        counter++;
        pthread_mutex_unlock(&m);    /* 끝 */
    }
    return NULL;
}
```
```
$ ./racefix ; ./racefix ; ./racefix
expected 2000000, got 2000000, lost 0
```
세 번 모두 같은 줄이 나왔고, 이번에는 그럴 이유가 있다. **이 줄이 없으면?** `unlock`이 없으면 두 번째 반복에서 자기 락을 기다리며 영원히 멈춘다(self-deadlock). `lock`을 루프 **밖으로** 빼면 결과는 맞지만 상호 배제가 아니라 순차 실행이 된다 — 규칙 2 위반이다. 카운터 하나뿐이라면 `_Atomic long` + `atomic_fetch_add`가 훨씬 빠르다. 락이 진짜 필요한 이유는 **여러 필드를 묶을 때**다(§4.5).

### 4.4 data race 없이 틀리기 — check-then-act

모든 함수가 mutex를 제대로 쓴다. 그런데 결과가 틀리다.
```c
static int q_size(void) {             /* thread-safe */
    pthread_mutex_lock(&m); int d = depth; pthread_mutex_unlock(&m); return d;
}
static void q_pop(void) {             /* thread-safe */
    pthread_mutex_lock(&m);
    depth--;
    if (depth < 0) overpops++;        /* 있어서는 안 되는 일을 센다 */
    pthread_mutex_unlock(&m);
}
static void *consumer(void *arg) {
    (void)arg;
    for (long i = 0; i < 2000000L; i++)
        if (q_size() > 0)             /* CHECK  ... 여기서 선점되면? */
            q_pop();                  /* ACT    이미 남이 가져갔다 */
    return NULL;
}
```

생산자 1개 + 소비자 2개로 돌린 실제 결과:
```
$ cc -std=c11 -O2 -Wall -Wextra -pthread -o toctou toctou.c && ./toctou ; ./toctou ; ./toctou
final depth = 0, pops on an empty queue = 199
final depth = 0, pops on an empty queue = 328
final depth = 0, pops on an empty queue = 196
```

빈 큐에서 pop 하는 일이 200번 안팎 일어난다. 실제 링버퍼였다면 `tail`이 `head`를 넘어가 **없는 데이터를 읽는다.** 그리고 `-fsanitize=thread`는 이걸 잡지 못한다 — 모든 접근이 락 안이므로 data race가 없다. 고치는 법은 §3.6의 `q_try_pop()` 하나로 합치는 것이다.

### 4.5 불변식이 깨지는 것 보기 — 존재한 적 없는 좌표

`GpsFix`를 두 필드로 줄인 최소 재현이다. writer는 항상 **서울 또는 뉴욕** 중 하나만 publish 한다.
```c
struct GpsFix { double lat, lon; };
static volatile struct GpsFix g_pub = { 37.5, 127.0 };
static void *writer(void *arg) {
    (void)arg;
    while (!atomic_load(&stop)) {
        g_pub.lat = 37.5; g_pub.lon = 127.0;   /* 서울 */
        g_pub.lat = 40.7; g_pub.lon = -74.0;   /* 뉴욕 */
    }
    return NULL;
}
static void *reader(void *arg) {
    long *torn = arg, n = 0;
    for (long i = 0; i < 20000000L; i++) {
        double lat = g_pub.lat;                /* 두 번의 별개 load, 그 사이에 */
        double lon = g_pub.lon;                /* writer 가 끼어든다 */
        int seoul = (lat == 37.5 && lon == 127.0);
        int ny    = (lat == 40.7 && lon == -74.0);
        if (!seoul && !ny) n++;                /* 존재한 적 없는 좌표 */
    }
    *torn = n; return NULL;
}
```
```
$ cc -std=c11 -O2 -Wall -Wextra -pthread -o torn torn.c && ./torn ; ./torn ; ./torn
20,000,000 reads -> 8689809 torn (never-existed) positions
20,000,000 reads -> 8634973 torn (never-existed) positions
20,000,000 reads -> 8658434 torn (never-existed) positions
```

**2000만 번 중 860만 번이 찢어졌다. 43%다.** "가끔 일어나는 희귀한 일"이라는 감각이 왜 틀렸는지 보여준다. 나오는 좌표는 `(37.5, -74.0)` — 서울의 위도와 뉴욕의 경도. 지문이 말한 *"a geofence alert in the wrong county"*가 바로 이것이다. 중요한 점: **각 `double` load/store 자체는 찢어지지 않았다.** 8바이트 정렬 접근은 이 하드웨어에서 쪼개지지 않는다. 깨진 것은 **두 필드의 관계**, 즉 불변식이다. 그래서 필드마다 `_Atomic`을 붙이는 해법은 이 버그를 못 고친다.

## 5. 단계별로 만들어 보기

`gps_get_last_fix()`를 세 단계로 만든다. 불변식은 "네 필드가 한 fix에서 온다".

### v0 — 필드마다 따로
```c
static volatile double g_lat, g_lon;
static volatile float  g_hdop;
static volatile uint64_t g_ts;
int gps_get_last_fix(struct GpsFix *out) {
    out->lat = g_lat; out->lon = g_lon;      /* 사이에 writer 가 끼어든다 */
    out->hdop = g_hdop; out->timestamp = g_ts;
    return 0;
}
```

틀린 이유: §4.5가 그대로 재현한 것이다. `volatile`은 원자성을 주지 않고 네 번의 load 사이에 writer가 끼면 세대가 섞인다. 각각을 `_Atomic`으로 바꿔도 마찬가지다 — 필드 원자성은 불변식이 아니다. 덧붙여 "아직 fix가 없음"을 구별할 방법도 없다(항상 0 반환).

### v1 — mutex 스냅샷 (기본값이자 정답)
```c
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct GpsFix g_fix;
static bool g_have;

int gps_get_last_fix(struct GpsFix *out) {
    pthread_mutex_lock(&g_lock);
    bool ok = g_have;
    if (ok) *out = g_fix;            /* 구조체 통째로 복사 — 한 세대만 본다 */
    pthread_mutex_unlock(&g_lock);
    return ok ? 0 : -1;              /* check-then-act 를 안 만드는 API */
}
```

- 임계구역이 **구조체 복사 한 번**이다. 40바이트 복사는 나노초 단위이므로 규칙 2를 지킨다.
- `g_have`를 같은 락 안에서 읽는다. 따로 읽으면 "있다고 확인했는데 복사할 때는 초기화 전"이 가능하다.
- 반환값으로 없음을 알린다 — 호출자가 `gps_has_fix()`를 먼저 부르는 TOCTOU 패턴을 만들지 않는다.

`gps_cache_solution.c`의 주석이 이 버전을 이렇게 평가한다: *"A MUTEX SNAPSHOT IS THE RIGHT DEFAULT. lock / memcpy / unlock is ~10 lines, obviously correct ... If I had 20 minutes on a whiteboard that is what I would ship."* 면접에서도 먼저 이걸 말하는 것이 옳다.

### v2 — seqlock (비대칭을 근거로 댈 수 있을 때만)

writer 1개가 10 Hz로 쓰고 reader 여러 개가 훨씬 빠르게 폴링한다. mutex면 **reader가 선점된 동안 writer가 멈춘다** — RT 커널에서는 priority inversion이다. seqlock은 버전 카운터로 writer를 wait-free로 만든다: 쓰기 전 `seq++`(홀수), 쓰기, 후 `seq++`(짝수). reader는 앞뒤 `seq`가 같고 짝수일 때만 값을 채택하고, 아니면 다시 읽는다. 대가도 반드시 함께 말한다. reader가 writer 폭주에 굶을 수 있고, **모든 필드가 atomic이어야** 하며(아니면 reader가 하도록 허용된 찢어진 읽기가 UB이고 TSan이 잡는다), 페이로드가 작고 부작용이 없어야 한다. `01_gps_fix_cache`가 seqlock을 고른 근거는 성능 측정이 아니라 **read/write 비대칭**이다.

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| `volatile`로 공유 상태를 보호한다 | 값이 여전히 틀리고 TSan이 경고한다 | `volatile`은 최적화만 막는다. 원자성·순서 없음 | `_Atomic` 또는 mutex. 여러 필드면 mutex |
| 필드마다 `_Atomic`을 붙인다 | 각 값은 정상인데 조합이 존재한 적 없는 상태 | 필드 원자성 ≠ 불변식. 세대가 섞인다 | 네 필드를 한 임계구역에서 함께 읽고 쓴다 |
| 읽기 경로에서 락을 생략한다 | 드물게 찢어진 값, 재현 불가 | 상호 배제는 **모든** 경로가 참여해야 성립한다 | 같은 데이터를 만지는 모든 경로가 같은 락을 잡는다 |
| `size()` 확인 후 `pop()` | 빈 큐에서 pop, 인덱스가 넘어감. TSan은 조용 | check와 act 사이에 락이 풀린다 (TOCTOU) | 조건부 행동 API 하나로 합친다 (`try_pop`) |
| 임계구역 안에서 벤더 호출/IO | getter 최악 지연이 수십 ms, 하네스 FAIL | 블로킹이 락 보유 시간이 된다 | 벤더에서 받아온 **뒤에** 락을 잡는다 |
| 테스트가 통과하면 됐다고 본다 | 몇 주 뒤 현장에서 터진다 | 43%짜리 버그도 0.01%짜리 버그도 똑같이 통과한다 | 불변식을 적고 타임라인으로 반박한다 + TSan |

## 7. 손으로 확인하기

먼저 §4의 프로그램들을 직접 돌린다. `-O2`/`-O0`와 `volatile` 유무 네 조합을 다 봐야 §4.1의 교훈이 남는다.
```sh
cc -std=c11 -O0 -Wall -Wextra -pthread -o race0 race.c && ./race0
cc -std=c11 -O2 -g -Wall -Wextra -pthread -fsanitize=thread -o rt race.c && ./rt
```

그다음 모범답안에서 임계구역의 크기를 눈으로 확인한다.
```sh
cd 03_event_tailer_shutdown
sed -n '/^void evq_stats/,/^}/p' event_queue_solution.c
```

볼 것: 다섯 필드를 **한 락 안에서** 복사한다. 주석이 이유를 적어 놨다 — *"a caller checking received == popped + dropped + depth must see a consistent snapshot."* 그게 불변식이다. 그다음 일부러 깨뜨려 본다. `01_gps_fix_cache/gps_cache.c`(stub)에 §5의 v0을 넣고 `./main.sh`를 돌리면 하네스의 "찢어진 값" 검사가 무엇을 잡는지 볼 수 있다.

- [ ] `race.c`를 `-O2`와 `-O0`으로 각각 돌려 결과가 다른 것을 봤다
- [ ] `toctou.c`를 `-fsanitize=thread`로 돌려 **경고가 없는데도 틀린** 것을 확인했다

## 8. 자가 점검
```check
Q: `counter++`를 두 스레드가 1,000,000번씩 했는데 2,000,000이 나왔다. 안전하다고 결론해도 되는가?
A: 안 된다. `-O2`에서 실제로 2000000이 나왔지만 어셈블리를 보면 컴파일러가 100만 번의 증가를
   load/add/store 한 벌로 접어 놓았다. 두 스레드가 겹치지 않아 우연히 맞은 것이고, 겹치면 1000000이
   나온다. data race는 UB이므로 결과는 "틀린 값"이 아니라 "아무 값"이다. `-O0`에서는 100만 가까이 사라졌다.

Q: data race와 race condition의 차이를 예와 함께 말하라.
A: data race는 동기화 없이 같은 위치에 동시 접근하고 하나 이상이 쓰기인 상태로, C 표준상 UB다(예: 락
   없는 `counter++`). race condition은 타이밍에 따라 결과가 달라지는 논리 버그다(예: `size()` 확인 후
   `pop()` — 모든 접근이 락 안이라 data race는 없지만 빈 큐에서 pop 한다). TSan은 전자만 잡는다.

Q: `volatile`을 붙이면 공유 변수가 안전해지는가? 베어메탈에서는 왜 충분했는가?
A: 안전해지지 않는다. `volatile`은 컴파일러가 접근을 생략·합치지 못하게만 하고 원자성도 순서 보장도
   주지 않는다. 실제로 `volatile long counter`는 `-O2`에서 200만 중 99만을 잃었다. 베어메탈에서 통했던
   것은 인터럽트를 끌 수 있었고 접근이 워드 단위였기 때문이다. 유저 공간 멀티스레드엔 둘 다 없다.

Q: `struct GpsFix`의 네 필드를 각각 `_Atomic`으로 만들면 문제가 해결되는가?
A: 안 된다. 지켜야 하는 것은 각 필드의 원자성이 아니라 "네 값이 같은 fix에서 왔다"는 불변식이다. 필드마다
   원자적이어도 네 번의 load 사이에 writer가 끼면 세대가 섞인다. 실측으로 2000만 번 중 860만 번(43%)이
   서울 위도 + 뉴욕 경도 같은 존재한 적 없는 좌표였다. 답은 mutex 스냅샷이나 seqlock이다.

Q: 임계구역의 두 규칙과, 그 안에서 하면 안 되는 것 3가지를 말하라.
A: 규칙 1은 "한 번에 하나" — 같은 데이터를 만지는 모든 경로(읽기 포함)가 같은 락을 잡아야 상호 배제가
   성립한다. 규칙 2는 "최대한 짧게" — 그게 곧 getter의 최악 지연이다. 안에서 금지인 것은 블로킹
   호출(벤더 호출·IO·sleep), `malloc`/로깅(내부 락 → 락 순서 문제), 콜백 호출(어떤 락을 잡을지 모른다).

Q: check-then-act를 어떻게 고치는가? 이 문제 세트의 API가 그 해법을 담고 있는 곳은 어디인가?
A: 확인과 행동을 한 임계구역에 넣어 하나의 함수로 만들고, 못 했으면 반환값으로 알린다. `evq_try_pop()`이
   그 형태다 — 큐가 비었으면 `-1`을 반환하므로 호출자가 `size()`를 먼저 물어볼 필요가 없다.
   `04_temp_single_flight`의 in-flight 판단도 확인과 등록이 한 임계구역에 있어야 한다.

Q: 이 문제 세트가 공유를 줄이는 4가지 전략 중 어느 것을 쓰는가? 한 문장으로 설명하라.
A: 소유권 이전(ownership transfer)이다. 샘플러가 벤더에서 받은 샘플의 유일한 주인이고, publish 지점(큐
   push, seqlock 쓰기, 포인터 교체) 한 곳에서만 소비자에게 넘긴다. 동기화가 필요한 코드가 몇 줄로 줄어든다.
   여기에 스레드 한정(벤더의 static 상태를 샘플러 1개에 가둠)이 함께 쓰인다.
```

## 9. 요약 카드

| 외울 것 | 내용 |
| --- | --- |
| data race | 동기화 없는 동시 접근 + 하나 이상 쓰기 = **UB**. TSan이 잡는다 |
| race condition | 타이밍에 따라 결과가 달라지는 **논리** 버그. 도구가 못 잡는다 |
| `volatile` | 최적화만 막는다. 원자성·순서 없음 → 동기화 도구가 아니다 |
| 불변식 | 락이 지키는 것은 변수가 아니라 **여러 값 사이의 관계** |
| 임계구역 | 한 번에 하나(모든 경로 포함) · 최대한 짧게 · 안에서 블로킹 호출/`malloc`/로깅/콜백 금지 |
| TOCTOU | check와 act를 한 락 안으로. `try_*` 형태 API로 만든다 |
| 공유 줄이기 | 불변 객체 · **소유권 이전**(이 문제 세트) · 스레드 한정 · TLS |
| 실측 | 락 없는 증가는 절반을 잃고, 찢어진 좌표는 43% 나온다 |
| 이전 | [B1. 스레드와 스케줄러](B1_threads_and_scheduler.md) · [B2. pthread 실전](B2_pthread_api.md) |
