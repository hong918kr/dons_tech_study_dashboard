# B2. pthread 실전 — 만들고, 넘기고, 기다리기

> **이 노트를 읽고 나면**
> - `pthread_create` / `pthread_join` / `pthread_detach`를 보지 않고 쓸 수 있고, 각각이 무엇을 회수하는지 말할 수 있다
> - 스레드에 인자를 넘기는 4가지 방법과 각각의 함정을 구별할 수 있다 (특히 `&i` 버그)
> - 이 문제 세트가 왜 거의 항상 "샘플러 스레드 1개"인지 근거를 대고 설명할 수 있다
>
> **선행**: [B1. 스레드와 스케줄러](B1_threads_and_scheduler.md)
>
> **이 개념을 쓰는 문제**: 10문제 전부의 `*_init()` / `*_deinit()`. 시작점은
> `03_event_tailer_shutdown`(샘플러 1개 + `evq_deinit()`의 join)과 `01_gps_fix_cache`(같은 모양).
> `07_badge_audit_dedupe`는 생산자 4개를 하네스가 띄우므로 인자 넘기기 4가지가 직접 걸린다.

---

## 1. 왜 이게 필요한가

연습 문제의 stub을 열면 이렇게 되어 있다.

```
int  evq_init(void)    /* TODO: start the sampler thread */
void evq_deinit(void)  /* TODO: stop it and join */
```

10문제 전부가 `pthread_create`로 시작하고 `pthread_join`으로 끝난다. 여기서 틀리면 Part 1을 손대기도 전에 하네스가 hang 하거나 크래시한다. 이 단계에서 나오는 사고는 세 종류다.

- 인자를 `&i`로 넘겨서 스레드가 엉뚱한 번호를 받는다 (§4.2에서 실제 출력을 본다)
- `join`을 안 해서 `deinit()` 직후에도 스레드가 살아 있고, 이미 free된 메모리를 만진다
- 스레드 함수가 지역 변수 주소를 반환해서 `join`으로 받은 포인터가 쓰레기다

이 노트는 그 세 가지만 다룬다. mutex는 [B3](B3_race_and_critical_section.md)에서 시작한다.

## 2. 그림으로 먼저

### create 와 join 의 타임라인

```svg
<svg viewBox="0 0 660 300" role="img" aria-label="pthread_create 와 pthread_join 의 타임라인">
  <text x="12" y="22" class="lbl">시간 →</text>

  <text x="12" y="62">main</text>
  <line class="muted" x1="60" y1="56" x2="640" y2="56"/>
  <rect class="fill-soft" x="70" y="42" width="96" height="28" rx="5"/>
  <text x="118" y="61" text-anchor="middle" class="lbl">create()</text>
  <rect class="box" x="180" y="42" width="180" height="28" rx="5"/>
  <text x="270" y="61" text-anchor="middle" class="lbl">내 일을 계속 한다</text>
  <rect class="fill-soft" x="374" y="42" width="120" height="28" rx="5"/>
  <text x="434" y="61" text-anchor="middle" class="lbl">join() 에서 대기</text>
  <rect class="box" x="508" y="42" width="120" height="28" rx="5"/>
  <text x="568" y="61" text-anchor="middle" class="lbl">이후 코드</text>
  <text x="12" y="152">worker</text>
  <line class="dash" x1="60" y1="146" x2="166" y2="146"/>
  <text x="80" y="140" class="lbl">아직 없다</text>
  <rect class="fill-soft" x="170" y="132" width="330" height="28" rx="5"/>
  <text x="335" y="151" text-anchor="middle" class="lbl">start_routine(arg) 실행 → return</text>
  <line class="dash" x1="504" y1="146" x2="640" y2="146"/>
  <text x="520" y="140" class="lbl">끝났지만 자원은 남아 있다</text>
  <line class="accent" x1="166" y1="56" x2="166" y2="132"/>
  <polygon class="accent" points="162,124 166,134 170,124"/>
  <text x="176" y="100" class="lbl">create: 새 실행 흐름이 태어난다</text>
  <line class="accent" x1="504" y1="146" x2="504" y2="70"/>
  <polygon class="accent" points="500,78 504,68 508,78"/>
  <text x="514" y="106" class="lbl">join: 종료를 기다리고 반환값·자원을 회수한다</text>

  <line class="muted" x1="12" y1="196" x2="648" y2="196"/>
  <text x="12" y="222" class="lbl">join 없음 → worker 는 끝났는데 기록이 남는다 (스레드 누수)</text>
  <text x="12" y="244" class="lbl">main 이 먼저 return → 프로세스 전체가 죽고 worker 는 중간에서 사라진다</text>
  <text x="12" y="266" class="lbl">detach → 끝나는 즉시 스스로 회수. 대신 기다릴 방법이 없다</text>
</svg>
```

### 스레드의 세 가지 상태

```svg
<svg viewBox="0 0 640 190" role="img" aria-label="joinable, detached, 종료 후 상태 전이">
  <rect class="fill-soft" x="16" y="66" width="140" height="56" rx="8"/>
  <text x="86" y="92" text-anchor="middle">joinable (기본)</text>
  <rect class="box" x="250" y="12" width="160" height="56" rx="8"/>
  <text x="330" y="38" text-anchor="middle">종료, 미회수</text>
  <text x="330" y="58" text-anchor="middle" class="lbl">자원 남음 = 누수</text>
  <rect class="box" x="250" y="120" width="160" height="56" rx="8"/>
  <text x="330" y="146" text-anchor="middle">detached</text>
  <text x="330" y="166" text-anchor="middle" class="lbl">join 불가</text>
  <rect class="fill-soft" x="480" y="66" width="144" height="56" rx="8"/>
  <text x="552" y="92" text-anchor="middle">완전히 사라짐</text>
  <line class="accent" x1="156" y1="82" x2="244" y2="48"/><polygon class="accent" points="244,44 252,48 243,53"/>
  <text x="168" y="64" class="lbl">return</text>
  <line class="muted" x1="156" y1="106" x2="244" y2="142"/><polygon class="muted" points="244,138 252,143 243,147"/>
  <text x="166" y="136" class="lbl">detach()</text>
  <line class="accent" x1="410" y1="48" x2="476" y2="82"/><polygon class="accent" points="476,78 484,83 475,87"/>
  <text x="404" y="38" class="lbl">join()</text>
  <line class="muted" x1="410" y1="146" x2="476" y2="108"/><polygon class="muted" points="476,112 484,106 477,103"/>
  <text x="418" y="166" class="lbl">return (자동 회수)</text>
</svg>
```

## 3. 개념 (용어를 하나씩)

### 3.1 `pthread_t`와 `pthread_create`

`pthread_t`는 스레드를 가리키는 **불투명(opaque) 식별자**다 — 나중에 join/detach 할 대상을 지목하기 위해 존재한다. `==`로 비교하면 안 되고 `pthread_equal()`을 쓴다. **join 한 뒤의 `pthread_t`는 무효**이고 다시 join 하면 UB다. `03_event_tailer_shutdown`의 모범답안이 `g_started` 플래그를 따로 두는 이유가 이것이다.

```c
int pthread_create(pthread_t *thread,               /* 1. 핸들을 받아올 곳 */
                   const pthread_attr_t *attr,      /* 2. 스택 크기 등. NULL = 기본 */
                   void *(*start_routine)(void *),  /* 3. 새 스레드가 실행할 함수 */
                   void *arg);                      /* 4. 그 함수에 넘길 것 하나 */
```

- 반환값은 **`errno`가 아니라 에러 번호 그 자체**다. 성공 0, 실패는 `EAGAIN` 같은 양수. `perror()`가 아니라 `strerror(rc)`로 찍는다. pthread 계열은 거의 다 이 규약이다.
- `attr`에 `NULL` = joinable + 기본 스택(Linux 8 MiB, macOS 512 KiB). 스레드를 수십 개 띄우면 `pthread_attr_setstacksize()`로 줄일 일이 생기지만, 우리는 1~2개라 `NULL`로 충분하다.
- **성공하는 순간 새 스레드가 이미 돌고 있을 수 있다.** create 가 return 하기 전에 worker가 다 끝나 있어도 정상이다. 그래서 초기화는 **create 전에** 끝내야 한다.

이게 `event_queue_solution.c`의 `evq_init()`이 큐와 카운터를 0으로 만들고 `stopping = false`를 세운 **다음에** 마지막으로 `pthread_create`를 부르는 이유다. 순서를 바꾸면 샘플러가 `stopping == true`를 보거나 초기화 중인 버퍼에 push 한다.

### 3.3 스레드 함수의 시그니처는 왜 `void *(*)(void *)`인가

```c
void *worker(void *arg);
```

**왜 이 모양인가**: pthread는 제네릭이 없는 C 라이브러리다. "아무 타입이든 하나 받고 하나 돌려주는 함수"를 C89로 표현하는 유일한 방법이 `void *`다. `void *`는 **"타입 지운 포인터 한 칸"**이고, 타입을 되살리는 책임은 전부 내 쪽에 있다. 파생 규칙 세 개:

- 인자는 **정확히 하나**다. 두 개 넘기려면 구조체로 묶는다.
- 컴파일러가 캐스팅을 검사해주지 않는다. `int *`로 넘기고 `long *`로 받으면 그냥 깨진다.
- 반환값도 `void *`다. 지역 변수 주소를 반환하면 스레드 스택이 사라진 뒤라 **dangling pointer**다.

### 3.4 인자 넘기는 4가지 방법

| 방법 | 어떻게 | 함정 | 언제 쓰나 |
| --- | --- | --- | --- |
| 1. 루프 변수 주소 `&i` | `pthread_create(..., &i)` | **거의 항상 버그.** 모든 스레드가 같은 주소를 보고, `i`는 계속 변한다 | 쓰지 않는다 |
| 2. 값을 포인터에 캐스팅 | `(void *)(intptr_t)id` | 포인터 크기를 넘는 값은 못 넣는다. 정수 → 포인터 캐스팅은 구현 정의 | 작은 정수 ID 하나 |
| 3. 배열의 슬롯 주소 | `&jobs[i]` | 배열이 **join 까지 살아 있어야** 한다. `main`의 지역 배열이면 OK | 스레드 수가 고정, 데이터가 읽기 전용 |
| 4. `malloc` 후 소유권 이전 | 만들어 넘기고 스레드가 `free` | `pthread_create` 실패 시 호출자가 free 해야 한다. 이중 free 주의 | 스레드 수가 동적, 데이터가 크다 |

2번의 `intptr_t`는 `<stdint.h>`에 있다. `(void *)(long)x`로도 되지만 `intptr_t`가 의도를 밝힌다.

### 3.5 `pthread_join` — 두 가지를 회수한다

```c
void *ret;
pthread_join(t, &ret);      /* 끝날 때까지 블로킹 + 반환값 받기 */
pthread_join(t, NULL);      /* 반환값에 관심 없으면 NULL */
```

**회수 1**: 스레드의 종료를 **기다린다**. 이게 동기화다 — join 이후 코드는 worker가 쓴 메모리를 안전하게 본다(happens-before 가 생긴다).

**회수 2**: 스레드 기록(TCB, 스택)을 해제한다. join 하지 않은 joinable 스레드는 끝나도 자원이 남는다. 이게 **스레드 누수**이고, 수천 번 반복하면 `pthread_create`가 `EAGAIN`으로 실패한다.

join 없이 `deinit()`을 끝내면 더 나쁘다. 샘플러가 아직 `g_q.buf`에 쓰고 있는데 호출자가 그 메모리를 free 하면 use-after-free다. 모범답안의 주석: *"join — and only now is it safe to say the thread is gone."*

### 3.6 `pthread_detach`, 그리고 `main`이 먼저 끝나면

`pthread_detach(t)`(또는 attr 에 `PTHREAD_CREATE_DETACHED`)를 하면 스레드가 끝나는 즉시 자원이 자동 회수된다. 대신 **join 할 수 없고, 반환값도 종료 시점도 알 수 없다.** 그래서 이 문제 세트에는 쓸 곳이 없다 — `deinit()`이 "스레드가 정말 끝났음"을 보장해야 하기 때문이다. detach는 떠나 보내고 잊어도 되는 일(로그 플러시 같은)에만 쓴다.

`main`의 `return`은 `exit()`과 같고, `exit()`은 **프로세스 전체**를 끝낸다. 다른 스레드는 어디서 실행 중이든 그 자리에서 사라진다. `atexit` 핸들러는 돌지만 worker의 정리 코드는 돌지 않는다. `pthread_exit(NULL)`을 쓰면 main 스레드만 끝나고 나머지는 계속 돈다(§4.4에서 비교한다). 다만 실무에서는 **명시적 join**을 쓴다 — 의도가 코드에 남기 때문이다.

### 3.7 스레드 개수는 어떻게 정하나

원칙은 하나다. **스레드 수는 성능 튜닝 값이 아니라 제약의 결과다.**

- 벤더 호출이 **thread-safe 하지 않다** → 그 호출을 하는 스레드는 **정확히 1개**. `evsrc.h`가 못 박아 놓았다: *"exactly one thread may ever be inside this function."* 2개로 늘리면 성능이 아니라 정합성이 깨진다.
- 벤더 호출이 **블로킹한다** → 호출자와 소비자가 같은 스레드일 수 없다. 최소 1개는 따로 필요하다.
- CPU를 태우는 일이면 코어 수만큼. 우리 문제는 I/O 대기라 해당 없다. 그리고 벤더가 직렬이면 스레드를 늘려도 처리량은 안 오른다.

그래서 답은 항상 "샘플러 1개 + 공유 상태 publish"다. 면접에서 말할 문장 (영어): *"The vendor call is not thread-safe and it blocks, so the design is forced: exactly one dedicated sampler thread owns it, and every consumer reads published state instead."*

### 3.8 `-pthread`는 왜 필요한가

`-lpthread`가 아니라 `-pthread`를 쓴다. 이 플래그는 **컴파일과 링크 양쪽에** 영향을 준다.

- 컴파일 시 `-D_REENTRANT`(glibc) 같은 매크로를 정의한다. 이게 `errno`를 스레드별로 만드는 등 헤더의 동작을 바꾼다. 링크만 고쳐서는 못 얻는다.
- 링크 시 pthread 구현을 끌어온다.

macOS에서는 pthread가 libSystem에 있어 `-pthread` 없이도 링크된다(§7에서 확인한다). 그래서 **맥에서만 테스트하면 이 실수를 못 잡는다.** glibc 2.34+ 도 libc에 합쳐졌지만 그 이전 Linux에서는 `undefined reference to 'pthread_create'`가 난다.

## 4. 코드로 보기

### 4.1 가장 작은 것

```c
#include <pthread.h>
#include <stdio.h>
#include <string.h>
static void *worker(void *arg) {
    printf("worker: %s\n", (const char *)arg);
    return NULL;
}
int main(void) {
    pthread_t t;
    int rc = pthread_create(&t, NULL, worker, "hello");
    if (rc != 0) { fprintf(stderr, "create: %s\n", strerror(rc)); return 1; }
    pthread_join(t, NULL);
    return 0;
}
```

출력은 `worker: hello` 한 줄이다. **이 줄이 없으면?** `strerror(rc)` 대신 `perror()`를 쓰면 엉뚱한 메시지가 나온다 — `pthread_create`는 `errno`를 세우지 않는다. `pthread_join`이 없으면 `main`이 먼저 끝나 `worker`의 출력이 안 보일 수 있다.

### 4.2 대표적 버그 — `&i`를 넘기기

```c
/* argbug.c */
#include <pthread.h>
#include <stdio.h>
static void *worker(void *arg) {
    int id = *(int *)arg;                          /* 언제 읽느냐에 따라 값이 달라진다 */
    printf("worker sees id=%d\n", id);
    return NULL;
}
int main(void) {
    pthread_t t[4];
    for (int i = 0; i < 4; i++)
        pthread_create(&t[i], NULL, worker, &i);   /* BUG: 넷 다 같은 주소 */
    for (int i = 0; i < 4; i++)
        pthread_join(t[i], NULL);
    return 0;
}
```

경고도 안 나고 컴파일된다. 실제 실행 결과 (출력은 `sort`로 정렬했다):

```
$ cc -std=c11 -O2 -Wall -Wextra -pthread -o argbug argbug.c
$ ./argbug | sort          $ ./argbug | sort          $ ./argbug | sort
worker sees id=1           worker sees id=1           worker sees id=1
worker sees id=2           worker sees id=4           worker sees id=2
worker sees id=4           worker sees id=4           worker sees id=3
worker sees id=4           worker sees id=4           worker sees id=4
```

읽는 법. 세 번 다 다르다. **`id=0`은 한 번도 안 나왔고 `id=4`가 나왔다** — 4는 루프를 끝낸 뒤의 값, 즉 배열 범위 밖 인덱스다. `handler[id]`로 썼다면 그 자리에서 메모리를 밟는다. 세 번째 실행은 1,2,3,4를 찍었는데 **우연히 다 달라서** 더 위험하다 — 테스트를 통과한다. 게다가 `i`는 `main`의 지역 변수라, join 없이 먼저 나가면 이미 사라진 스택을 읽는다.

### 4.3 고친 세 가지 방법

```c
/* argfix.c — 방법 2 / 3 / 4 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
struct Job { int id; double gain; };

static void *by_value(void *arg) {                 /* 2) 값을 포인터 칸에 실어 보낸다 */
    long id = (long)(intptr_t)arg;
    printf("by_value   id=%ld\n", id);
    return (void *)(intptr_t)(id * 10);            /* 반환값도 같은 트릭 */
}
static void *by_slot(void *arg) {                  /* 3) 스레드마다 다른 슬롯 = 다른 주소 */
    struct Job *j = arg;
    printf("by_slot    id=%d gain=%.1f\n", j->id, j->gain);
    return NULL;
}
static void *by_owned(void *arg) {                 /* 4) 힙 + 소유권 이전 */
    struct Job *j = arg;
    printf("by_owned   id=%d gain=%.1f\n", j->id, j->gain);
    free(j);                                       /* 해제 책임이 여기 있다 */
    return NULL;
}
int main(void) {
    pthread_t t[3];
    struct Job jobs[1] = { { 7, 1.5 } };           /* join 까지 살아 있다 */
    struct Job *owned = malloc(sizeof *owned);
    owned->id = 9; owned->gain = 2.5;
    pthread_create(&t[0], NULL, by_value, (void *)(intptr_t)3);
    pthread_create(&t[1], NULL, by_slot,  &jobs[0]);
    pthread_create(&t[2], NULL, by_owned, owned);
    void *ret = NULL;
    pthread_join(t[0], &ret);
    printf("by_value returned %ld\n", (long)(intptr_t)ret);
    pthread_join(t[1], NULL);  pthread_join(t[2], NULL);
    return 0;
}
```

실제 실행 결과 (경고 0으로 빌드된다):

```
$ cc -std=c11 -O2 -Wall -Wextra -Werror -pthread -o argfix argfix.c
$ ./argfix | sort
by_owned   id=9 gain=2.5
by_slot    id=7 gain=1.5
by_value   id=3
by_value returned 30
```

**이 줄이 없으면?** `free(j)`가 없으면 방법 4는 메모리 누수다. `jobs`를 지역 배열로 둔 것은 join 이 같은 함수 안에 있어서 괜찮다 — join 을 다른 함수로 옮기는 순간 dangling 이 된다. `(intptr_t)`를 빼면 포인터와 정수 크기가 다른 플랫폼에서 경고가 난다.

### 4.4 `main`이 먼저 끝나면

```c
static void *slow(void *a) {
    (void)a;  usleep(200000);
    printf("slow: I finished\n");    /* 여기까지 올까? */
    return NULL;
}
int main(void) {
    pthread_t t;
    pthread_create(&t, NULL, slow, NULL);
    printf("main: returning without join\n");
    return 0;                        /* = exit(0) = 프로세스 전체 종료 */
}
```

```
$ ./mainexit                          $ ./mainexit2   (return 0 → pthread_exit(NULL))
main: returning without join          main: returning without join
exit=0                                slow: I finished
                                      exit=0
```

왼쪽에는 `slow`의 출력이 **없다.** 200 ms를 기다리다가 프로세스와 함께 사라졌고, exit code는 0이라 CI도 조용하다. 오른쪽은 main 스레드만 끝나고 프로세스가 살아 있었으므로 출력이 나온다. 하지만 **`pthread_exit` 대신 join 을 쓴다** — 두 출력의 차이가 함수 한 줄에 숨어 있다는 게 바로 문제다.

## 5. 단계별로 만들어 보기

`03_event_tailer_shutdown`의 `evq_init()` / `evq_deinit()`을 세 단계로 만든다.

### v0 — 틀린 버전

```c
int evq_init(void) {
    pthread_create(&g_thread, NULL, sampler_main, NULL);
    memset(&g_q, 0, sizeof g_q);        /* 초기화가 create 뒤에 있다 */
    g_running = true;
    return 0;
}
void evq_deinit(void) { g_running = false; }   /* 플래그만 내린다 */
```

틀린 곳 네 개. `memset`이 create 뒤다(샘플러가 이미 push를 시작했을 수 있고, 초기화 중인 버퍼를 덮어쓴다). `g_running = true`도 create 뒤라 샘플러가 루프 조건을 먼저 보고 **즉시 종료**할 수 있다. 반환값을 안 보므로 create 실패 시 `g_thread`가 쓰레기인 채 join 된다. 그리고 **join 이 없다** — 게다가 샘플러는 `evsrc_read_blocking()` 안에 갇혀 플래그를 볼 기회조차 없다.

### v1 — 순서와 반환값을 고친 버전

```c
int evq_init(void) {
    if (g_started) return 0;                      /* 두 번 불려도 안전 */
    memset(&g_q, 0, sizeof g_q);                  /* 1. 상태를 먼저 완성 */
    g_q.stopping = false;
    atomic_store(&g_running, true);                /* 2. 플래그도 먼저 */
    int rc = pthread_create(&g_thread, NULL, sampler_main, NULL);
    if (rc != 0) { atomic_store(&g_running, false); return -1; }  /* 3. 되돌린다 */
    g_started = true;                              /* 4. 이제 join 대상이 있다 */
    return 0;
}
void evq_deinit(void) {
    if (!g_started) return;
    atomic_store(&g_running, false);
    pthread_join(g_thread, NULL);                  /* 기다린다 */
    g_started = false;                             /* 핸들은 이제 무효다 */
}
```

순서가 맞고 join도 있다(이 조각은 가짜 `sampler_main`을 붙여 `-Werror`로 컴파일·실행까지 확인했다). 그런데 실제 벤더를 붙이면 **hang 한다.** 샘플러가 타임아웃 없는 `evsrc_read_blocking()` 안에서 자고 있어서, 플래그를 내려도 다음 이벤트가 올 때까지 알 수 없고 조용한 사이트에서는 영영 오지 않는다.

### v2 — 실제 모범답안의 종료 순서

`event_queue_solution.c`의 `evq_deinit()`이 하는 일은 네 단계다.

```c
atomic_store_explicit(&g_running, false, memory_order_release);  /* 1. 정지 발행 */
evsrc_wake();                                                    /* 2. 벤더 탈출구 */
pthread_mutex_lock(&g_q.lock);
g_q.stopping = true;
pthread_cond_broadcast(&g_q.cv);                                 /* 3. 대기 중 소비자 해제 */
pthread_mutex_unlock(&g_q.lock);
pthread_join(g_thread, NULL);                                    /* 4. 이제서야 join */
g_started = false;
```

핵심은 **2번**이다. `evsrc_wake()`는 진행 중인 `evsrc_read_blocking()`을 `-2`로 되돌리고, **latch**라서 wake 이후에 시작된 read도 즉시 `-2`를 반환한다. 그래서 "1 µs 일찍 깨웠다" 같은 경쟁이 없다. 이 탈출구가 없는 벤더라면 wake + timed join 루프를 직접 만들어야 한다. 3번을 빼면 join 은 성공하지만 `evq_pop_timed()`에서 자던 소비자가 타임아웃 전체를 기다린다.

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| 루프 변수 주소 `&i`를 넘긴다 | 스레드가 중복된/범위 밖 ID를 받는다. 가끔은 정상 | 네 스레드가 같은 주소를 보고, `i`는 계속 변한다 | 값을 캐스팅(`(void*)(intptr_t)i`)하거나 슬롯 배열(`&jobs[i]`)을 넘긴다 |
| `pthread_create` 반환값을 무시 | 실패 후 join 에서 크래시 | 실패 시 `pthread_t`는 채워지지 않는다 | `rc != 0`이면 되돌리고 `-1` 반환. `strerror(rc)`로 찍는다 |
| join 없이 `deinit()`을 끝낸다 | free 뒤에 쓰기, 간헐적 크래시, 스레드 누수 | 플래그를 내려도 스레드는 아직 코드 안에 있다 | 반드시 `pthread_join`. 블로킹 중이면 wake 를 먼저 |
| 스레드 함수가 지역 변수 주소를 반환 | `join`으로 받은 값이 쓰레기 | 스레드 스택이 사라진다 | 작은 값은 캐스팅해 반환, 큰 것은 `malloc` 또는 호출자 버퍼 |
| create 뒤에 초기화를 마무리 | 첫 몇 개 이벤트가 사라지거나 즉시 종료 | create 성공 시점에 worker가 이미 돌 수 있다 | 초기화 → 플래그 → create 순서 |
| `-pthread` 없이 빌드 | macOS는 되고 Linux에서 링크 에러/오동작 | 매크로 정의와 링크가 함께 필요하다 | 컴파일·링크 양쪽에 `-pthread` |

## 7. 손으로 확인하기

```sh
cd 03_event_tailer_shutdown
sed -n '/^int evq_init/,/^}/p;/^void evq_deinit/,/^}/p' event_queue_solution.c
```

볼 것: init 이 "상태 초기화 → 플래그 → create" 순서인지, deinit 이 "플래그 → wake → broadcast → join" 4단계인지. §5의 v2와 대조한다. 지문과 벤더 헤더는 [`03_event_tailer_shutdown`](../03_event_tailer_shutdown/question_note)에 있다.

그다음 `-pthread`를 뺀 빌드를 해본다. `cc -std=c11 -O2 -Wall -Wextra -o np argfix.c` 는 맥에서 그냥 링크된다(직접 확인했다). 같은 명령이 glibc 구버전 Linux에서는 `undefined reference to 'pthread_create'`로 실패한다. 그래서 `main.sh`를 고쳐 쓰지 않는다.

- [ ] `argbug.c`를 5번 돌려 "우연히 정상"이 나오는 것을 봤다
- [ ] `evq_deinit()`에서 `evsrc_wake()`를 지우면 `./main.sh sol`이 hang 하는지 확인했다

## 8. 자가 점검

```check
Q: `pthread_create`가 0을 반환했다. 이 시점에 새 스레드는 어떤 상태인가?
A: 알 수 없다. 시작 전일 수도, 실행 중일 수도, 벌써 끝났을 수도 있다. 그래서 create 이후에 초기화를
   마무리하면 안 되고, 공유 상태와 실행 플래그는 create 전에 완성해야 한다. `evq_init()`이
   `pthread_create`를 마지막에 부르는 이유다.

Q: 루프에서 `pthread_create(&t[i], NULL, w, &i)`가 왜 버그인가? 증상을 구체적으로 말하라.
A: 네 스레드가 모두 `i` 하나의 주소를 받는다. `*arg`를 읽는 시점에 `i`가 이미 변해 있으므로 중복 ID나
   루프 종료값(범위 밖 인덱스)이 나온다. 실제로 0은 한 번도 안 나오고 4가 나왔다. 더 나쁜 건 우연히
   0,1,2,3이 나올 때도 있어서 테스트를 통과한다는 점이다.

Q: `pthread_join`은 정확히 무엇을 회수하는가? 두 가지를 말하라.
A: 첫째, 종료를 기다린다 — join 이후 코드는 worker가 쓴 메모리를 안전하게 본다. 둘째, 스레드 기록(TCB,
   스택)을 해제한다. 둘째를 안 하면 스레드 누수이고 반복하면 `pthread_create`가 `EAGAIN`으로 실패한다.
   `deinit()`에서 join 이 필수인 이유는 첫째다.

Q: `deinit()`에서 `g_running = false`만 하면 왜 부족한가? 이 문제 세트의 답은 무엇인가?
A: 샘플러가 타임아웃 없는 `evsrc_read_blocking()` 안에서 자고 있다. 플래그는 다음 이벤트가 올 때까지
   읽히지 않고, 조용한 사이트에서는 영영 안 온다. 답은 플래그 → `evsrc_wake()` → 대기 소비자
   broadcast → join 의 4단계다. `evsrc_wake()`는 latch 라서 깨우는 타이밍 경쟁도 없앤다.

Q: 스레드 함수의 시그니처가 `void *(*)(void *)`인 이유와, 거기서 나오는 제약 두 가지를 말하라.
A: 제네릭이 없는 C에서 "임의 타입 하나를 받고 하나를 돌려주는 함수"를 표현하는 유일한 방법이다.
   제약 1: 인자는 정확히 하나이므로 여러 개는 구조체로 묶는다. 제약 2: 타입 검사가 없어 캐스팅이 틀려도
   컴파일된다. 덧붙여 반환값이 지역 변수 주소면 스레드 스택이 사라진 뒤라 dangling pointer 다.

Q: 이 문제 세트에서 샘플러 스레드를 2개로 늘리면 어떻게 되는가?
A: 성능이 아니라 정합성이 깨진다. `evsrc.h`와 `gps.h`가 벤더 호출에 static 상태가 있어 thread-safe 하지
   않다고 명시했다. 두 스레드가 동시에 들어가면 그 상태가 망가진다. 게다가 이벤트는 직렬로 오므로
   처리량도 안 오른다. 스레드 수는 튜닝 값이 아니라 제약의 결과다.

```

## 9. 요약 카드

| 외울 것 | 내용 |
| --- | --- |
| 시그니처 | `pthread_create(&t, NULL, fn, arg)` / `fn`은 `void *(*)(void *)` |
| 에러 규약 | 에러 번호를 **반환**한다. `errno` 아님 → `strerror(rc)` |
| 순서 | 상태 초기화 → 실행 플래그 → **마지막에** `pthread_create` |
| join | 종료 대기 + 자원 회수. 둘 다 필요. 한 번만 할 수 있다 |
| detach / `main` return | detach는 기다릴 수 없다 · `main` 의 return 은 프로세스 전체 종료 |
| 인자 | `&i` 금지 · 값 캐스팅 · 슬롯 배열 · `malloc` 소유권 이전 |
| 스레드 수 | 벤더가 thread-unsafe 하면 **정확히 1개**. 튜닝 값이 아니다 |
| 빌드 · 다음 | `-pthread`는 컴파일·링크 양쪽 → [B3](B3_race_and_critical_section.md) |
