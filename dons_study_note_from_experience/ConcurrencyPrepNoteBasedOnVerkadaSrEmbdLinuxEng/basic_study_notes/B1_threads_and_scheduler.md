# B1. 스레드와 스케줄러 — "동시에"가 정확히 무슨 뜻인가

> **이 노트를 읽고 나면**
> - 스레드 두 개가 무엇을 공유하고 무엇을 따로 갖는지 그림으로 그릴 수 있다
> - "스케줄러가 아무 때나 끼어든다"는 말을 타임라인으로 옮길 수 있고, 가능한 타임라인 개수를 직접 세어볼 수 있다
> - 싱글코어에서도 race가 나는 이유를 설명할 수 있고, 그래서 "테스트로 증명 못 한다"는 결론까지 말할 수 있다
>
> **선행**: 없음 (이 폴더의 첫 노트다)
>
> **이 개념을 쓰는 문제**: 10문제 전부. 특히 `01_gps_fix_cache`의 Part 1(샘플러 1개가 쓰고 여러 reader가 읽는다)과
> `03_event_tailer_shutdown`의 Part 1(샘플러가 벤더 호출 안에 갇혀 있는데 다른 스레드가 종료시켜야 한다)은
> 이 노트의 내용만으로 "왜 어려운지"가 설명된다.

---

## 1. 왜 이게 필요한가

[`01_gps_fix_cache`](../01_gps_fix_cache/question_note)의 지문은 이렇게 시작한다. 벤더 함수 `gps_wait_for_fix()`는 **최대 100 ms 블로킹**이고 **thread-safe 하지 않다**. 그런데 우리가 만들 `gps_get_last_fix()`는 **절대 블로킹하면 안 되고 몇 개의 스레드가 동시에 불러도 안전해야** 한다. 여기서 처음 막히는 곳은 mutex 문법이 아니라 그 전 단계다.

- 스레드를 하나 더 띄우면 그 스레드는 내 전역 변수를 **같이** 보는가? (본다)
- 내가 `g_pub.lat = ...; g_pub.lon = ...;` 두 줄을 연달아 쓰면 그 사이에 다른 스레드가 **끼어들 수 있는가**? (있다)
- 그 "끼어듦"은 얼마나 자주 일어나는가? 내가 테스트를 100번 돌려서 안 나오면 없는 것인가? (아니다)

베어메탈 펌웨어에서는 이 감각이 다르게 잡혀 있다. 거기서 "끼어드는 것"은 **ISR 하나**였고, 언제 끼어드는지 알고 있었고, `__disable_irq()`로 막을 수도 있었다. 스레드는 그 세 가지가 전부 다르다. 이 노트는 그 차이만 다룬다 — 락은 아직 안 나온다.

## 2. 그림으로 먼저

### 프로세스 하나, 스레드 둘 — 무엇이 공유되는가

```svg
<svg viewBox="0 0 640 330" role="img" aria-label="프로세스 안의 스레드 두 개, 공유 영역과 전용 영역">
  <rect class="box" x="8" y="8" width="624" height="314" rx="10"/>
  <text x="20" y="30" class="lbl">프로세스 1개 (= 주소 공간 1개, fork 하지 않았다)</text>

  <rect class="fill-soft" x="24" y="44" width="360" height="160" rx="8"/>
  <text x="204" y="64" text-anchor="middle">공유 — 주소 공간 / 커널 자원</text>
  <text x="40" y="90" class="lbl">코드(.text) · 전역/정적 변수(.data .bss)</text>
  <text x="40" y="112" class="lbl">힙 (malloc 한 것 전부)</text>
  <text x="40" y="134" class="lbl">파일 디스크립터 표 · 소켓 · mmap</text>
  <text x="40" y="156" class="lbl">시그널 핸들러 · cwd · uid</text>
  <text x="40" y="182" class="lbl">→ 포인터 하나만 넘겨도 상대가 그대로 읽는다</text>

  <rect class="box" x="404" y="44" width="208" height="74" rx="8"/>
  <text x="508" y="64" text-anchor="middle">스레드 A 전용</text>
  <text x="418" y="86" class="lbl">스택 · 레지스터 · PC · SP</text>
  <text x="418" y="106" class="lbl">errno · TLS · 시그널 마스크</text>

  <rect class="box" x="404" y="130" width="208" height="74" rx="8"/>
  <text x="508" y="150" text-anchor="middle">스레드 B 전용</text>
  <text x="418" y="172" class="lbl">스택 · 레지스터 · PC · SP</text>
  <text x="418" y="192" class="lbl">errno · TLS · 시그널 마스크</text>

  <line class="accent" x1="384" y1="80" x2="400" y2="80"/>
  <polygon class="accent" points="400,76 408,80 400,84"/>
  <line class="accent" x1="384" y1="166" x2="400" y2="166"/>
  <polygon class="accent" points="400,162 408,166 400,170"/>

  <rect class="box" x="24" y="224" width="588" height="80" rx="8"/>
  <text x="318" y="246" text-anchor="middle">커널 스케줄러</text>
  <text x="40" y="270" class="lbl">A와 B를 어느 코어에, 얼마나, 어떤 순서로 올릴지 정한다</text>
  <text x="40" y="292" class="lbl">내 코드에는 아무런 힌트도 없다 — 이게 핵심이다</text>
</svg>
```

핵심 한 줄: **전역 변수와 힙은 기본적으로 공유, 스택과 레지스터는 기본적으로 전용.** 그래서 스레드 사이 "통신"은 별도 API가 필요 없다 — 이미 같은 메모리를 본다. 그게 편리함이자 모든 버그의 원인이다.

### 선점 — 스케줄러가 끼어드는 자리

```svg
<svg viewBox="0 0 660 260" role="img" aria-label="싱글코어에서 두 스레드가 타임 슬라이스를 나눠 쓰는 모습">
  <text x="12" y="22" class="lbl">코어 1개 · 시간 →</text>

  <rect class="fill-soft" x="12"  y="36" width="110" height="34" rx="5"/>
  <text x="67" y="58" text-anchor="middle">A 실행</text>
  <rect class="box" x="126" y="36" width="26" height="34" rx="4"/>
  <text x="139" y="58" text-anchor="middle" class="lbl">CS</text>
  <rect class="fill-soft" x="156" y="36" width="150" height="34" rx="5"/>
  <text x="231" y="58" text-anchor="middle">B 실행</text>
  <rect class="box" x="310" y="36" width="26" height="34" rx="4"/>
  <text x="323" y="58" text-anchor="middle" class="lbl">CS</text>
  <rect class="fill-soft" x="340" y="36" width="90" height="34" rx="5"/>
  <text x="385" y="58" text-anchor="middle">A 실행</text>
  <rect class="box" x="434" y="36" width="26" height="34" rx="4"/>
  <text x="447" y="58" text-anchor="middle" class="lbl">CS</text>
  <rect class="fill-soft" x="464" y="36" width="180" height="34" rx="5"/>
  <text x="554" y="58" text-anchor="middle">B 실행</text>

  <text x="12" y="96" class="lbl">CS = 컨텍스트 스위치 (레지스터 저장/복원 + 캐시·TLB 손실)</text>

  <line class="dash" x1="126" y1="70" x2="126" y2="130"/>
  <line class="dash" x1="310" y1="70" x2="310" y2="130"/>
  <line class="dash" x1="434" y1="70" x2="434" y2="130"/>
  <rect class="box" x="126" y="138" width="184" height="30" rx="5"/>
  <text x="218" y="158" text-anchor="middle" class="lbl">A 는 여기서 멈춘 줄을 모른다</text>

  <line class="accent" x1="12" y1="196" x2="644" y2="196"/>
  <text x="12" y="220" class="lbl">끼어드는 자리는 "함수 사이"가 아니라 "기계어 명령 사이" — C 한 줄 안에서도 끊긴다</text>
</svg>
```

## 3. 개념 (용어를 하나씩)

### 3.1 스레드 (thread)

**정의**: 같은 주소 공간을 공유하면서 독립적으로 스케줄되는 실행 흐름.

**왜 존재하는가**: 블로킹하는 일과 블로킹하면 안 되는 일을 분리하려고. `evsrc_read_blocking()`은 몇 분씩 멈출 수 있는데 대시보드는 즉시 답을 받아야 한다. 해법은 하나뿐이다 — **멈추는 쪽을 전용 스레드에 격리한다.**

**예시**: `03_event_tailer_shutdown/event_queue_solution.c`의 파일 상단 주석이 이 모양을 그려 놓았다. 샘플러 스레드 1개가 벤더 호출의 유일한 주인이고, 소비자들은 절대 벤더를 건드리지 않는다.

### 3.2 선점 (preemption)

**정의**: 스레드가 자발적으로 양보하지 않아도 커널이 강제로 CPU를 뺏는 것.

**왜 존재하는가**: 그렇지 않으면 무한 루프 하나가 시스템을 멈춘다. Linux의 기본 스케줄러는 선점형이다.

**언제 뺏기는가** (내 코드에서 알 수 없다):
- 타임 슬라이스를 다 썼을 때
- 더 높은 우선순위 스레드가 깨어났을 때
- 하드웨어 인터럽트가 들어와 커널에 진입했다가 돌아올 때
- 내가 syscall을 불렀을 때(`printf`, `read`, `malloc`이 brk를 부를 때 등)

**타임 슬라이스**: 한 번에 얼마나 실행하게 해줄지. Linux CFS에서 보통 1~10 ms 수준이다. 하지만 **다 쓰지 않아도** 위의 다른 이유로 뺏긴다. "1 ms 안쪽 코드는 안전하다"는 추론은 틀렸다.

### 3.3 컨텍스트 스위치 비용

**정의**: 스레드를 바꿔 올릴 때 드는 비용 — 레지스터 저장/복원(직접 비용) + 캐시와 분기 예측기가 식는 것(간접 비용).

간접 비용이 보통 더 크다. 그래서 "스레드를 많이 만들면 빨라진다"가 아니고, 이 문제 세트에서 **샘플러 스레드가 거의 항상 딱 1개**인 이유 중 하나다. 이 맥에서 실제로 재본 숫자 (§4의 `bench.c`):

| 항목 | 측정값 |
| --- | --- |
| `pthread_create` + `pthread_join` 한 쌍 | 약 14 µs |
| `sched_yield()` 한 번 | 약 0.11 µs |
| 두 스레드가 플래그를 한 번 주고받기 | 약 0.15 µs |

읽는 법: 스레드 생성은 **마이크로초 단위**인데 `gps_wait_for_fix()`는 **100 ms = 100,000 µs** 블로킹이다. 7000배 차이. "블로킹을 스레드로 감싸는" 거래는 압도적으로 이득이다. 반대로 이벤트 하나당 스레드를 새로 만드는 설계는 비용이 일의 크기를 넘는다.

### 3.4 동시성 (concurrency) vs 병렬성 (parallelism)

| | 동시성 | 병렬성 |
| --- | --- | --- |
| 정의 | 여러 작업이 **겹치는 기간 동안 진행 중**인 구조 | 여러 작업이 **같은 순간에 실제로 실행**되는 것 |
| 필요한 것 | 코어 1개로도 성립 | 코어 2개 이상 필요 |
| 무엇에 관한 문제인가 | 프로그램 **구조**(설계) | **하드웨어 자원** |
| 우리 문제 세트 | 이것이 주제다 | 있으면 버그가 더 빨리 드러날 뿐 |

면접에서 말할 문장 (영어): *"Concurrency is about dealing with many things at once; parallelism is about doing many things at once. This problem is a concurrency problem — it would still be one on a single core."*

### 3.5 싱글코어에서도 race가 나는 이유

병렬 실행이 없으면 안전할 것 같지만 아니다. 이유는 **원자성(atomicity)**이 없기 때문이다.

C 한 줄은 기계어 여러 개다. `counter++`는 보통 세 개다.

```
load   r0 <- [counter]
add    r0 <- r0 + 1
store  [counter] <- r0
```

싱글코어에서도 스케줄러는 이 **세 개 사이에서** 스레드를 바꿀 수 있다. 바꾸면 A의 `r0`은 A의 전용 레지스터에 저장되고, B가 같은 `counter`를 읽고 쓴 다음, A가 돌아와 **낡은 r0**으로 store한다. B의 증가가 사라진다.

같은 이유로 **구조체 대입 한 줄**도 안전하지 않다. `struct GpsFix`는 40바이트다(`01_gps_fix_cache/gps.h`의 주석이 그렇게 적어 놓았다). 40바이트 store는 여러 명령으로 쪼개지고, 그 사이에 reader가 끼면 **lat는 이번 fix, lon은 지난 fix**인 좌표가 나온다. 존재한 적 없는 위치다. B3에서 실제로 재현한다.

멀티코어는 여기에 진짜 동시 실행과 **메모리 재배열**(코어마다 store 순서가 다르게 보임)을 더한다. 하지만 race의 뿌리는 멀티코어가 아니라 원자성 부재다.

### 3.6 인터리빙 조합이 폭발한다

스레드 A가 단계 3개(a1 a2 a3), B가 단계 3개(b1 b2 b3)를 실행한다. 각 스레드 **안에서의 순서는 고정**이고, 두 스레드 사이의 순서만 스케줄러가 정한다. 가능한 전체 타임라인은 몇 가지인가? 전체 6칸 중에서 A의 자리 3개를 고르면 남은 3칸은 자동으로 B다. 그래서

```
개수 = C(6,3) = 6! / (3! * 3!) = 720 / (6 * 6) = 20
```

20가지다. 일반식은 `(m+n)! / (m! * n!)`. 이게 얼마나 빨리 커지는지:

| 스레드 × 단계 | 타임라인 수 |
| --- | --- |
| 2 × 3 | 20 |
| 2 × 5 | 252 |
| 2 × 10 | 184,756 |
| 3 × 3 | 1,680 |
| 4 × 10 | 약 4.7 × 10^21 |

여기서 "단계"는 C 한 줄이 아니라 **기계어 명령**이고 실제 함수 하나에는 수십 개가 있다. 현실의 경우의 수는 위 표보다 훨씬 크다.

**그래서 테스트로는 증명할 수 없다.** 다음 절에서 이걸 직접 확인한다.

## 4. 코드로 보기

### 4.1 인터리빙을 눈으로 보기

```c
/* interleave.c — 두 스레드가 3단계씩. 실제로 일어난 순서를 찍는다. */
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static char log_[16];
static int  n = 0;

static void step(char who) {
    pthread_mutex_lock(&m);
    log_[n++] = who;          /* 기록만 한다. 순서는 스케줄러가 정한다 */
    pthread_mutex_unlock(&m);
}
static void *worker(void *arg) {
    char who = *(char *)arg;
    for (int i = 0; i < 3; i++) step(who);
    return NULL;
}
int main(void) {
    pthread_t a, b; char A = 'A', B = 'B';
    memset(log_, 0, sizeof log_);
    pthread_create(&a, NULL, worker, &A);
    pthread_create(&b, NULL, worker, &B);
    pthread_join(a, NULL); pthread_join(b, NULL);
    printf("%s\n", log_);
    return 0;
}
```

`mutex`가 왜 여기 있나? **기록 자체가 깨지는 것을 막으려고**다. 이 프로그램은 race가 아니라 **순서**를 보여주는 것이므로 기록은 정확해야 한다. `n++`를 락 없이 하면 글자가 겹쳐 써지고 실험이 무의미해진다.

**이 줄이 없으면?** `pthread_join`이 없으면 `main`이 먼저 끝나 아무것도 안 찍힐 수 있다(B2에서 다룬다).

40번 돌린 실제 결과:

```
$ cc -std=c11 -O2 -Wall -Wextra -pthread -o interleave interleave.c
$ for i in $(seq 40); do ./interleave; done | sort | uniq -c | sort -rn
  32 AAABBB
   8 BBBAAA
```

**20가지 중 2가지만 나왔다.** 스레드 하나가 너무 짧아서 시작하자마자 끝나 버린다. "내 코드는 100번 돌려도 잘 되는데요"라고 말하는 상황이 정확히 이것이다.

### 4.2 숨어 있던 나머지 타임라인을 꺼내기

`step()` 뒤에 `sched_yield()` 한 줄을 넣는다. "지금 나 말고 돌릴 게 있으면 돌려라"라고 스케줄러에게
요청하는 함수다 (`#include <sched.h>`).

```c
static void *worker(void *arg) {
    char who = *(char *)arg;
    for (int i = 0; i < 3; i++) { step(who); sched_yield(); }
    return NULL;
}
```

200번 돌린 실제 결과:

```
$ for i in $(seq 200); do ./interleave_y; done | sort | uniq -c | sort -rn
 116 AAABBB
  16 BABABA
  15 BABBAA
  11 ABABAB
  10 BBABAA
  10 AABABB
   7 ABAABB
   6 ABBBAA
   4 BBBAAA
   4 ABABBA
   1 ABBABA
```

**같은 코드, 20가지 중 11가지가 나왔다.** 로직은 한 줄도 안 고쳤다 — 타이밍만 흔들었다. `ABBABA`는 200번 중 1번이다. 1/200짜리 타임라인이 프로덕션에서는 초당 수천 번의 기회를 얻는다.

### 4.3 비용 재보기

```c
/* bench.c 의 핵심 부분 */
uint64_t t0 = now_us();
for (int i = 0; i < 10000; i++) {
    pthread_create(&t, NULL, nop, NULL);
    pthread_join(t, NULL);              /* 만들고 바로 기다린다 = 순수 비용 */
}
uint64_t t1 = now_us();
printf("pthread_create + join : %.2f us each\n", (t1 - t0) / 10000.0);
```

`pthread_join`이 없으면? 회수되지 않은 스레드 자원이 쌓여 측정값이 "생성 비용"이 아니라 "생성 + 과부하"가 된다. 10000개가 동시에 살려고 해서 `pthread_create`가 실패할 수도 있다.

실제 출력 (Apple M2, macOS):

```
$ cc -std=c11 -O2 -Wall -Wextra -pthread -o bench bench.c && ./bench
pthread_create + join : 14.09 us each
sched_yield           : 0.108 us each
thread ping-pong      : 0.145 us per handoff
$ ./bench
pthread_create + join : 14.86 us each
sched_yield           : 0.106 us each
thread ping-pong      : 0.150 us per handoff
```

## 5. 단계별로 만들어 보기

여기서 "만드는 것"은 코드가 아니라 **머릿속 모델**이다. 틀린 모델 → 덜 틀린 모델 → 쓸 수 있는 모델.

### v0 — 틀린 모델: "함수 사이에서만 바뀐다"

```
"내 함수가 실행되는 동안에는 내가 CPU를 갖고 있고, return 하면 다른 스레드로 넘어간다."
```

왜 틀렸나: 선점은 **기계어 명령 경계**에서 일어난다. `counter++` 중간, 구조체 대입 중간, `if` 조건 평가와
본문 사이 — 전부 끼어들 수 있다(§3.5의 3명령 분해가 반례다). 이 모델을 갖고 있으면 "짧은 함수는 락이
필요 없다"는 결론에 도달하고, 그게 `01_gps_fix_cache`의 찢어진 좌표 버그가 된다.

### v1 — 덜 틀린 모델: "명령 사이에서 바뀔 수 있다"

```
"어느 두 기계어 명령 사이에서든 넘어갈 수 있다. 여러 명령이 한 단위여야 하면 내가 직접 묶어야 한다."
```

이건 맞다. 하지만 **"드물게 일어난다"**는 착각을 남기고, 그래서 "테스트로 확인하고 넘어가자"가 된다.
§4.1이 그 함정이다 — 40번 돌려 20가지 중 2가지만 봤다.

### v2 — 쓸 수 있는 모델: "가능한 모든 타임라인이 언젠가 일어난다"

```
"스케줄러는 가능한 타임라인 중 아무거나 고를 수 있다.
 내 코드가 옳다는 것은 '20가지 전부에서 옳다'는 뜻이고, 테스트는 그중 몇 개를 봤다는 뜻일 뿐이다.
 그러니 옳음은 실행이 아니라 구조로 보장해야 한다."
```

"구조로 보장"의 구체적인 뜻은 이 폴더의 모범답안들이 전부 같은 형태로 보여준다.

- **공유를 없앤다**: 벤더 호출을 스레드 1개에 가둔다 → "두 스레드가 동시에 벤더를 부르는 타임라인"이 아예 존재하지 않는다
- **묶는다**: 여러 필드를 한 임계구역 안에서 쓰고 읽는다 → 찢어진 상태가 관찰 가능한 타임라인이 사라진다
- **불변식을 적는다**: "reader는 항상 하나의 fix를 본다"를 먼저 쓰고, 그 문장이 깨지는 타임라인이 있는지 따진다

면접에서 말할 문장 (영어):
*"I can't test my way to correctness here — the schedule space is too big.
So I state the invariant first, then argue no interleaving can break it."*

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| 전역 변수를 두 스레드가 그냥 같이 쓴다 | 값이 가끔 틀리고, 재현이 안 된다 | 스레드는 `.data`/`.bss`/힙을 공유한다. 기본이 공유다 | 공유를 의도적으로 설계한다. 안 나눠도 되면 스레드 로컬로 두고, 나눠야 하면 락으로 묶는다 |
| "한 줄이니까 원자적"이라고 가정 | 카운터가 덜 세지고, 좌표가 섞인다 | C 한 줄 = 기계어 여러 개. 40바이트 구조체 대입은 store 여러 번 | 원자성이 필요하면 `_Atomic` 또는 락. 크기가 워드를 넘으면 락이 기본 |
| `sleep`/`usleep`으로 순서를 맞춘다 | 개발 장비에서는 되고 타깃/부하에서 깨진다 | 지연은 보장이 아니다. 스케줄러는 약속한 적이 없다 | 순서가 필요하면 순서를 강제하는 도구(join, mutex, condvar)를 쓴다 |
| 스레드를 일 단위로 계속 만든다 | CPU는 바쁜데 처리량이 안 오른다 | 생성·소멸이 약 14 µs이고 컨텍스트 스위치마다 캐시가 식는다 | 전담 스레드 소수 + 큐. 이 문제 세트가 "샘플러 1개"인 이유 |
| 테스트가 통과했으니 맞다고 결론 | 몇 주 뒤 현장에서 한 번 터진다 | 40번 실행으로 20가지 중 2가지만 봤다(§4.1) | 불변식을 글로 쓰고 타임라인으로 반박해 본다. 보조로 `-fsanitize=thread` |
| 싱글코어/저사양이라 race가 없다고 믿는다 | 필드 단위로 섞인 값이 나온다 | race의 원인은 병렬 실행이 아니라 원자성 부재다 | 코어 수와 무관하게 임계구역을 설계한다 |
| ISR처럼 "잠깐 끄면 된다"고 생각한다 | 방법을 못 찾고, 찾은 방법이 위험하다 | 유저 공간에 `__disable_irq()`는 없다. 다른 스레드를 멈출 수단이 없다 | 인터럽트를 막는 대신 **데이터**를 잠근다(mutex). 대칭적 상호 배제가 유저 공간의 도구다 |

## 7. 손으로 확인하기

`03_event_tailer_shutdown`에서 "스레드가 실제로 따로 돈다"를 확인한다.

```sh
cd 03_event_tailer_shutdown
./main.sh sol
```

볼 것: 하네스가 reader 여러 개를 띄워 2초간 돌리는데도 getter의 **최악 지연이 마이크로초 단위**로 찍힌다. 벤더가 몇십 ms 블로킹하는데도 그렇다. 블로킹이 샘플러 스레드 한 곳에 격리되어 있기 때문이다. 이어서 `sed -n '1,40p' event_queue_solution.c`로 파일 상단 주석의 그림(샘플러 1개 → 큐/카운터 2곳)을 확인한다. [`03_event_tailer_shutdown` 지문](../03_event_tailer_shutdown/question_note)과 [모범답안](../03_event_tailer_shutdown/event_queue_solution.c)이 원본이다.

이 노트의 실험을 직접 해보려면 §4의 세 프로그램을 만들어 돌린다.

```sh
cc -std=c11 -O2 -Wall -Wextra -pthread -o /tmp/il interleave.c
for i in $(seq 200); do /tmp/il; done | sort | uniq -c | sort -rn
```

- [ ] `sched_yield()` 없는 버전과 있는 버전의 결과 종류 수를 비교했다
- [ ] 20가지 타임라인을 종이에 다 적어 봤다 (AAABBB부터 BBBAAA까지)
- [ ] `bench.c`의 세 숫자를 내 장비에서 재봤다

## 8. 자가 점검

```check
Q: 스레드 A가 `malloc`으로 받은 포인터를 스레드 B에게 전역 변수로 건네주면, B는 그 메모리를 읽을 수 있는가?
A: 읽을 수 있다. 힙은 프로세스 전체가 공유하는 하나의 주소 공간에 있고, 스레드는 주소 공간을 공유한다.
   그래서 스레드 간 데이터 전달에 별도 IPC가 필요 없다. 다만 "언제부터 안전하게 읽어도 되는지"는
   여전히 동기화 문제로 남는다 — 공유되는 것과 순서가 보장되는 것은 다른 얘기다.

Q: 코어가 1개인 보드에서는 race condition이 생기지 않는다 — 맞는가?
A: 틀렸다. race의 원인은 병렬 실행이 아니라 원자성 부재다. `counter++`는 load/add/store 세 명령이고,
   싱글코어에서도 스케줄러가 그 사이에서 스레드를 바꿀 수 있다. 바뀌면 낡은 레지스터 값으로 store 해서
   증가가 사라진다. 멀티코어는 빈도를 높이고 메모리 재배열을 추가할 뿐이다.

Q: 스레드 2개가 각각 4단계를 실행할 때 가능한 타임라인은 몇 가지인가? 식과 함께 답하라.
A: C(8,4) = 8!/(4!4!) = 40320/576 = 70가지다. 전체 8칸 중 A가 쓸 4칸을 고르면 나머지는 B로 정해지기
   때문이다. 여기서 "단계"가 C 한 줄이 아니라 기계어 명령이라는 점을 감안하면 실제 경우의 수는 훨씬 크다.

Q: 테스트를 1000번 돌려 전부 통과했다. 이 코드에 race가 없다고 말할 수 있는가?
A: 말할 수 없다. §4.1에서 40번 실행으로 가능한 20가지 중 2가지만 관찰했다. 실행 횟수는 표본이고
   타임라인 공간은 조합적으로 크다. 테스트는 버그의 존재를 보여줄 수 있지만 부재는 보여주지 못한다.
   부재는 불변식과 임계구역 설계로 논증하고, `-fsanitize=thread`로 보강한다.

Q: 베어메탈에서 쓰던 "ISR 진입 전에 인터럽트를 막는다"가 pthread 환경에서 왜 안 통하는가?
A: 유저 공간에는 다른 스레드를 멈출 수단이 없다. ISR 모델은 비대칭·단방향이라 main 쪽에서만 막으면
   충분했지만, 스레드는 대칭·양방향이고 멀티코어에서는 진짜로 동시에 돈다. 그래서 "실행을 막는" 대신
   "데이터 접근을 직렬화"해야 한다 — 그것이 mutex다.

Q: `struct GpsFix`(40바이트)를 `_Atomic`으로 선언하면 락 없이 publish 할 수 있는가?
A: 문법적으로는 되지만 하드웨어에 40바이트 원자 store가 없어서 컴파일러가 내부적으로 락을 깐다.
   `atomic_is_lock_free()`로 확인할 수 있다. 결과적으로 숨은 락이 생기므로, 차라리 mutex를 명시적으로
   쓰거나 seqlock 같은 버전 기법을 쓰는 것이 낫다. 이 판단은 `01_gps_fix_cache` Part 1의 핵심이다.
```

## 9. 요약 카드

| 외울 것 | 내용 |
| --- | --- |
| 공유 | 코드 · 전역/정적 · 힙 · fd 표 · 시그널 핸들러 |
| 전용 | 스택 · 레지스터 · PC/SP · `errno` · TLS |
| 선점 | 기계어 명령 경계에서 언제든. 함수 경계가 아니다 |
| 타임라인 수 | 2스레드 × n단계 = `C(2n,n)`. 3단계면 20, 10단계면 184,756 |
| 싱글코어 race | 생긴다. 원인은 병렬성이 아니라 원자성 부재 |
| 비용 | `create+join` 약 14 µs · 스위치 간접비용이 더 크다 · 벤더 블로킹은 100,000 µs |
| 결론 | 테스트는 표본이다. 옳음은 불변식과 구조로 논증한다 |
| 다음 | [B2. pthread 실전](B2_pthread_api.md) → [B3. 경쟁 상태와 임계구역](B3_race_and_critical_section.md) |

**ISR/main 모델 ↔ 스레드 모델 대응표** (베어메탈 경험을 옮겨 오는 표)

| 축 | 베어메탈 ISR / main | pthread 스레드 |
| --- | --- | --- |
| 누가 누구를 끊는가 | ISR이 main을 끊는다. 역방향은 없다 (**비대칭·단방향**) | 아무 스레드가 아무 스레드를 끊는다 (**대칭·양방향**) |
| 정말 동시에 돌 수 있나 | 아니다. 코어 1개를 번갈아 쓴다 | 멀티코어에서는 **진짜 동시**에 돈다 |
| 끼어드는 시점 | 인터럽트 소스를 알고 있다 | 알 수 없다. 스케줄러의 자유 |
| 막는 방법 | `__disable_irq()` / 우선순위 마스크 | 없다. **데이터를 잠근다**(mutex) |
| 공유 상태 관례 | `volatile` + 인터럽트 잠깐 끄기 | `volatile`은 무효. `_Atomic` 또는 mutex |
| 대기하는 방법 | 플래그 폴링, WFI | `pthread_join`, condvar, timed wait |
| 스택 | main 하나 + ISR이 같은 스택을 쓸 수도 | 스레드마다 별도 스택(기본 512 KiB~8 MiB) |
| 종료 | 리셋 | `pthread_join`으로 회수 (`03_event_tailer_shutdown`의 주제) |
