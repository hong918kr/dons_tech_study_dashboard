# A8. 유저스페이스 동기화 — race를 눈으로 보고, mutex·condvar·atomic·futex를 바닥까지 내려가 본다

> **이 노트를 다 읽으면**: lost update를 어셈블리 수준에서 설명할 수 있다 · mutex/condvar/semaphore/rwlock/atomic 중 상황에 맞는 것을 고를 수 있다 · arm64 weak ordering과 acquire/release를 litmus 테스트로 증명할 수 있다 · pthread mutex가 futex 위에 어떻게 서 있고 priority inversion을 PI로 어떻게 막는지 strace로 보여 줄 수 있다
> **왜 필요한가**: "mutex와 spinlock 차이는?", "condvar를 왜 while로 감싸나?", "futex가 뭔가?", "priority inversion 설명해 봐" — 커널 락 질문의 앞단에서 거의 반드시 나온다. 실무에서는 센서 스레드 간 데이터 공유와 RT 스레드 설계가 전부 이 위에 있다.
> **Don 기준 난이도**: RTOS mutex·semaphore·PI·링버퍼·LDREX/STREX는 이미 안다 / glibc·futex·C11 memory model·ThreadSanitizer 같은 Linux 쪽 구현과 도구가 새로 배울 부분이다
> **선행 노트**: A2 (프로세스와 스레드), A5 (공유 메모리), A7 (IPC)

---

## 0. 큰 그림 — 이게 왜 필요한가

센서 키트를 떠올려 보자. i.MX8 위에서 IMU 스레드가 1 kHz로 SPI를 읽고, 카메라 스레드가 30 fps로 V4L2 버퍼를 받고, 퓨전 스레드가 둘을 합쳐 자세를 추정하고, 로깅 스레드가 디스크에 쓴다. 이 스레드들은 **같은 주소 공간**을 공유한다(A2). 그래서 "최신 IMU 샘플", "보정 테이블", "프레임 큐" 같은 공유 데이터를 **동시에 만지는 순간**이 생기고, 그 순간을 질서 있게 만드는 도구가 이 노트의 주제다.

bare-metal에서 Don이 하던 일과 1:1로 대응시켜 보면 출발점이 쉬워진다.

| bare-metal / RTOS에서 하던 것 | Linux 유저스페이스에서 대응하는 것 | 차이 |
|---|---|---|
| `__disable_irq()`로 임계 구역 보호 | `pthread_mutex_lock()` | 유저는 인터럽트를 끌 권한이 없다. 다른 코어도 동시에 돈다 |
| RTOS mutex (PI 옵션) | `pthread_mutex` + `PTHREAD_PRIO_INHERIT` | 기본값은 PI 꺼짐 |
| RTOS counting semaphore | POSIX `sem_t` | named로 만들면 프로세스 간에도 쓴다 |
| RTOS event flags / 태스크 notify | condition variable (`pthread_cond_t`) | "조건"은 내가 변수로 들고 있어야 한다 |
| Cortex-M `LDREX`/`STREX`, `DMB` | C11 `<stdatomic.h>`, `memory_order_*` | 컴파일러가 `ldxr/stxr`, `ldadd`, `ldar/stlr`, `dmb`를 대신 고른다 |
| ISR ↔ 태스크 lock-free 링버퍼 | SPSC ring + acquire/release | 코어가 여러 개라 순서 보장이 명시적이어야 한다 |

이 모든 유저스페이스 도구가 결국 어디로 내려가는지 한 장으로 보면 다음과 같다.

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="15" width="640" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="42" font-size="14" text-anchor="middle">유저 스레드: IMU 스레드 · 카메라 스레드 · 퓨전 스레드 · 로깅 스레드</text>
  <rect x="20" y="85" width="120" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="80" y="112" font-size="12" text-anchor="middle">pthread_mutex</text>
  <rect x="150" y="85" width="120" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="210" y="112" font-size="12" text-anchor="middle">pthread_cond</text>
  <rect x="280" y="85" width="120" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="112" font-size="12" text-anchor="middle">sem_t</text>
  <rect x="410" y="85" width="120" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="470" y="112" font-size="12" text-anchor="middle">pthread_rwlock</text>
  <rect x="540" y="85" width="120" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="600" y="112" font-size="12" text-anchor="middle">stdatomic.h</text>
  <rect x="20" y="155" width="510" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="275" y="177" font-size="13" text-anchor="middle">glibc NPTL: 경합이 없으면 atomic 명령 하나로 끝 (fast path)</text>
  <text x="275" y="196" font-size="13" text-anchor="middle">경합이 있을 때만 futex syscall (slow path)</text>
  <rect x="540" y="155" width="120" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="600" y="177" font-size="13" text-anchor="middle">ldadd, ldxr</text>
  <text x="600" y="196" font-size="13" text-anchor="middle">ldar, stlr</text>
  <line x1="80" y1="129" x2="80" y2="153" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="210" y1="129" x2="210" y2="153" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="129" x2="340" y2="153" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="470" y1="129" x2="470" y2="153" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="600" y1="129" x2="600" y2="153" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="10" y1="235" x2="670" y2="235" stroke="currentColor" stroke-dasharray="6 4"/>
  <text x="20" y="228" font-size="12">user (EL0)</text>
  <text x="20" y="252" font-size="12">kernel (EL1)</text>
  <line x1="275" y1="205" x2="275" y2="268" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="285" y="228" font-size="12">futex() = SVC</text>
  <rect x="20" y="270" width="510" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="275" y="292" font-size="13" text-anchor="middle">futex: 주소 → 해시 버킷 → 대기 큐 → schedule()</text>
  <text x="275" y="311" font-size="13" text-anchor="middle">PI futex는 커널 rt_mutex로 우선순위 상속</text>
  <rect x="540" y="270" width="120" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="600" y="292" font-size="13" text-anchor="middle">atomic은</text>
  <text x="600" y="311" font-size="13" text-anchor="middle">커널 개입 없음</text>
  <text x="340" y="352" font-size="13" text-anchor="middle">커널 안의 락(spinlock_t, mutex, RCU)은 F4에서 — 이 노트는 유저 쪽 바닥까지</text>
</svg>
```
그림 1 — 유저스페이스 동기화 도구의 층 구조. 경합이 없으면 커널까지 내려가지 않는다는 것이 핵심이다.

이 노트의 모든 예제는 Lima VM에서 실행했다. 환경은 다음과 같다(4 vCPU, Apple Silicon 호스트 위의 arm64 VM).

```sh
limactl shell elinux -- bash -lc 'uname -srm; gcc --version | head -1; ldd --version | head -1; nproc'
```
```text
Linux 7.0.0-34-generic aarch64
gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
ldd (Ubuntu GLIBC 2.43-2ubuntu2.4) 2.43
4
```

빌드는 모두 이런 모양이다(스레드를 쓰므로 `-pthread`).

```sh
limactl shell elinux -- bash -lc 'mkdir -p /tmp/A8 && cd /tmp/A8 && gcc -std=gnu11 -Wall -Wextra -O2 -pthread /Users/donh/.elinux_scratch/A8/ex1_race.c -o ex1 && ./ex1'
```

---

## 1. Race condition을 눈으로 보기

### 1.1 직관

**race condition**은 "결과가 스레드 실행 순서(타이밍)에 따라 달라지는 상태"다. C 한 줄 `counter++`는 CPU 입장에서 한 동작이 아니라 **메모리에서 읽기 → 레지스터에서 더하기 → 메모리에 쓰기** 세 동작이다. 두 코어가 이 세 단계를 엇갈려 실행하면 한쪽의 증가가 사라진다. 이걸 **lost update**라고 부른다.

Don이 SSD 펌웨어에서 ISR과 메인 루프가 같은 카운터를 건드릴 때 `__disable_irq()`로 감쌌던 바로 그 문제다. 차이는 Linux에서는 **진짜로 4개 코어가 동시에** 돈다는 점이다. 인터럽트를 꺼도(유저는 끌 수도 없지만) 다른 코어는 막을 수 없다.

### 1.2 예제 1 — lost update 재현

4개 스레드가 보호 없이 공유 카운터를 100만 번씩 증가시킨다.

```c
// ex1_race.c — 4개 스레드가 보호 없이 counter++ 를 N번씩 한다
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define NTHREADS 4
#define ITERS    1000000

static volatile long counter;   /* volatile이어도 race는 그대로다 */

static void *worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < ITERS; i++)
        counter++;              /* ldr → add → str 3단계 */
    return NULL;
}

int main(void)
{
    pthread_t t[NTHREADS];
    for (int run = 0; run < 5; run++) {
        counter = 0;
        for (int i = 0; i < NTHREADS; i++)
            if (pthread_create(&t[i], NULL, worker, NULL) != 0) {
                perror("pthread_create");
                exit(1);
            }
        for (int i = 0; i < NTHREADS; i++)
            pthread_join(t[i], NULL);
        printf("run %d: counter = %8ld (expected %d, lost %ld)\n",
               run, counter, NTHREADS * ITERS,
               (long)NTHREADS * ITERS - counter);
    }
    return 0;
}
```
```text
run 0: counter =  1017953 (expected 4000000, lost 2982047)
run 1: counter =  1048462 (expected 4000000, lost 2951538)
run 2: counter =  1203178 (expected 4000000, lost 2796822)
run 3: counter =  1213917 (expected 4000000, lost 2786083)
run 4: counter =  1129303 (expected 4000000, lost 2870697)
```
출력에서 볼 것: 400만이 나와야 하는데 **약 100만~120만**만 남았다. 증가의 70% 이상이 사라졌고, 실행할 때마다 값이 다르다(값은 실행마다 다르다).

### 1.3 어셈블리로 확인 — `gcc -S`

`counter++`가 정말 세 명령인지 컴파일러 출력으로 본다.

```sh
limactl shell elinux -- bash -lc 'gcc -std=gnu11 -O2 -S -o - /Users/donh/.elinux_scratch/A8/ex1_race.c | sed -n "/^worker:/,/ret/p"'
```
```text
worker:
.LFB40:
	.cfi_startproc
	mov	w1, 16960
	adrp	x2, .LANCHOR0
	movk	w1, 0xf, lsl 16
	.p2align 5,,15
.L2:
	ldr	x0, [x2, #:lo12:.LANCHOR0]
	subs	w1, w1, #1
	add	x0, x0, 1
	str	x0, [x2, #:lo12:.LANCHOR0]
	bne	.L2
	mov	x0, 0
	ret
```
출력에서 볼 것: 루프 `.L2` 안에 `ldr`(읽기) → `add`(더하기) → `str`(쓰기)가 따로 있다. `16960 + 0xf<<16 = 1000000`이 반복 횟수다. `ldr`과 `str` 사이에 다른 코어가 끼어들면 그 코어의 증가는 덮어써진다.

```svg
<svg viewBox="0 0 640 340" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="200" y="28" font-size="14" text-anchor="middle">스레드 A (CPU0)</text>
  <text x="420" y="28" font-size="14" text-anchor="middle">스레드 B (CPU1)</text>
  <text x="575" y="28" font-size="14" text-anchor="middle">메모리 counter</text>
  <line x1="40" y1="45" x2="40" y2="275" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="30" y="290" font-size="12">시간</text>
  <rect x="110" y="50" width="180" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="200" y="70" font-size="13" text-anchor="middle">ldr x0 ← counter (5)</text>
  <rect x="330" y="88" width="180" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="420" y="108" font-size="13" text-anchor="middle">ldr x0 ← counter (5)</text>
  <rect x="110" y="126" width="180" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="200" y="146" font-size="13" text-anchor="middle">add x0 = 6</text>
  <rect x="330" y="164" width="180" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="420" y="184" font-size="13" text-anchor="middle">add x0 = 6</text>
  <rect x="110" y="202" width="180" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="200" y="222" font-size="13" text-anchor="middle">str counter ← 6</text>
  <rect x="330" y="240" width="180" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="420" y="260" font-size="13" text-anchor="middle">str counter ← 6</text>
  <text x="575" y="70" font-size="13" text-anchor="middle">5</text>
  <text x="575" y="108" font-size="13" text-anchor="middle">5</text>
  <text x="575" y="146" font-size="13" text-anchor="middle">5</text>
  <text x="575" y="184" font-size="13" text-anchor="middle">5</text>
  <text x="575" y="222" font-size="13" text-anchor="middle">6</text>
  <text x="575" y="260" font-size="13" text-anchor="middle">6 (7이어야 함)</text>
  <line x1="290" y1="217" x2="545" y2="217" stroke="#4a7bd0" stroke-dasharray="3 3" marker-end="url(#ah2)"/>
  <line x1="510" y1="255" x2="530" y2="255" stroke="#d0564a" marker-end="url(#ah2)"/>
  <rect x="110" y="296" width="400" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="310" y="318" font-size="13" text-anchor="middle">증가는 두 번 했는데 결과는 +1 → lost update</text>
</svg>
```
그림 2 — race interleaving 타임라인. B가 A의 `str` 전에 옛 값 5를 읽었기 때문에 A의 증가가 B의 `str`에 덮어써진다.

### 1.4 함정 — `volatile`도, "결과가 맞음"도 증거가 아니다

`volatile`은 "매번 메모리에서 읽고 쓰라"는 컴파일러 지시일 뿐이다. 세 단계가 **원자적(atomic, 쪼갤 수 없게)**이 되지는 않는다. 위 결과가 그 증거다. 레지스터 MMIO 접근에 쓰는 `volatile`과 동기화는 전혀 다른 개념이다.

반대로 `volatile`을 빼면 더 무서운 일이 생긴다. 같은 코드에서 `volatile`만 지운 `ex1b`를 돌려 보자.

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && gcc -std=gnu11 -Wall -Wextra -O2 -pthread /Users/donh/.elinux_scratch/A8/ex1b_novol.c -o ex1b && ./ex1b && gcc -std=gnu11 -O2 -S -o - /Users/donh/.elinux_scratch/A8/ex1b_novol.c | sed -n "/^worker:/,/ret/p"'
```
```text
run 0: counter =  4000000 (expected 4000000, lost 0)
run 1: counter =  4000000 (expected 4000000, lost 0)
run 2: counter =  4000000 (expected 4000000, lost 0)
run 3: counter =  4000000 (expected 4000000, lost 0)
run 4: counter =  4000000 (expected 4000000, lost 0)
worker:
.LFB40:
	.cfi_startproc
	adrp	x1, .LANCHOR0
	ldr	x0, [x1, #:lo12:.LANCHOR0]
	add	x0, x0, 999424
	add	x0, x0, 576
	str	x0, [x1, #:lo12:.LANCHOR0]
	mov	x0, 0
	ret
```
출력에서 볼 것: 결과가 "맞게" 나온다. 하지만 어셈블리를 보면 루프가 사라지고 `ldr; add 1000000; str` 한 번으로 합쳐졌다. 스레드가 짧아서 우연히 안 겹쳤을 뿐, **race는 그대로 있다**. C11에서 동기화 없는 동시 접근(data race)은 **undefined behavior**이므로 컴파일러는 이렇게 바꿔도 된다. 테스트가 통과했다고 안전한 게 아니다 — 그래서 10절의 ThreadSanitizer 같은 도구가 필요하다.

---

## 2. Mutex — 한 번에 한 스레드만

### 2.1 정의

**mutex**(mutual exclusion)는 "열쇠가 하나뿐인 방"이다. `pthread_mutex_lock()`으로 열쇠를 얻은 스레드만 임계 구역(critical section, 공유 데이터를 만지는 코드 구간)에 들어가고, 나머지는 **잠들어서**(CPU를 쓰지 않고) 기다린다. 이 "잠들어서"가 spinlock과의 결정적 차이다(6절).

RTOS mutex와 같은 점: 소유자(owner) 개념이 있다 — 잠근 스레드만 풀 수 있다(타입에 따라 검사 여부가 다르다). 다른 점: 기본 타입은 **priority inheritance가 꺼져 있고**, 소유자 검사도 하지 않는다.

```c
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;   /* 정적 초기화 */
pthread_mutex_lock(&m);
/* 임계 구역: 공유 데이터 접근 */
pthread_mutex_unlock(&m);
```

mutex는 단지 상호 배제만 하는 게 아니라 **메모리 순서도 보장**한다. lock은 acquire, unlock은 release 의미를 가진다(7절). 그래서 unlock 전에 쓴 값은 다음에 lock한 스레드에게 반드시 보인다. 별도의 `DMB`를 넣을 필요가 없다.

### 2.2 mutex 타입 — normal / errorcheck / recursive

`pthread_mutexattr_settype()`으로 타입을 고른다. 세 타입이 "같은 스레드가 두 번 lock" "안 잡은 mutex를 unlock"에 어떻게 반응하는지 직접 시험한다.

```c
// ex2_types.c — mutex 타입별로 "같은 스레드가 두 번 lock"과 "남의 unlock"을 시험
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void try_type(int type, const char *name)
{
    pthread_mutexattr_t a;
    pthread_mutex_t m;
    pthread_mutexattr_init(&a);
    if (pthread_mutexattr_settype(&a, type) != 0) {
        perror("settype");
        exit(1);
    }
    pthread_mutex_init(&m, &a);

    pthread_mutex_lock(&m);
    int r = pthread_mutex_trylock(&m);      /* 이미 내가 잡고 있는 상태 */
    printf("%-10s 2nd trylock -> %s\n", name, r ? strerror(r) : "OK (count+1)");
    if (r == 0)
        pthread_mutex_unlock(&m);
    if (type == PTHREAD_MUTEX_ERRORCHECK) {
        r = pthread_mutex_lock(&m);          /* lock은 trylock과 달리 막힐 수 있다 */
        printf("%-10s 2nd lock    -> %s\n", name, r ? strerror(r) : "OK");
    }
    pthread_mutex_unlock(&m);
    r = pthread_mutex_unlock(&m);            /* 이미 풀린 mutex를 또 unlock */
    printf("%-10s extra unlock-> %s\n", name, r ? strerror(r) : "OK (undefined!)");
    pthread_mutex_destroy(&m);
    pthread_mutexattr_destroy(&a);
}

int main(void)
{
    try_type(PTHREAD_MUTEX_NORMAL,     "NORMAL");
    try_type(PTHREAD_MUTEX_ERRORCHECK, "ERRORCHECK");
    try_type(PTHREAD_MUTEX_RECURSIVE,  "RECURSIVE");
    return 0;
}
```
```text
NORMAL     2nd trylock -> Device or resource busy
NORMAL     extra unlock-> OK (undefined!)
ERRORCHECK 2nd trylock -> Device or resource busy
ERRORCHECK 2nd lock    -> Resource deadlock avoided
ERRORCHECK extra unlock-> Operation not permitted
RECURSIVE  2nd trylock -> OK (count+1)
RECURSIVE  extra unlock-> Operation not permitted
```
출력에서 볼 것: NORMAL은 잘못된 unlock에도 "OK"를 돌려준다 — 에러를 알려 주지 않을 뿐 동작은 정의되지 않은 것이다. ERRORCHECK는 자기 자신 deadlock을 `EDEADLK`("Resource deadlock avoided")로, 남의/풀린 mutex unlock을 `EPERM`으로 잡아 준다. RECURSIVE는 같은 스레드가 여러 번 잠그면 카운트만 올린다.

| 타입 | 같은 스레드가 두 번 lock | 소유자 아닌 unlock | 언제 쓰나 |
|---|---|---|---|
| `PTHREAD_MUTEX_NORMAL` (기본) | 영원히 멈춤(self-deadlock) | 정의되지 않음 | 성능 우선, 코드가 단순할 때 |
| `PTHREAD_MUTEX_ERRORCHECK` | `EDEADLK` 반환 | `EPERM` 반환 | **개발·디버그 빌드에 강력 추천** |
| `PTHREAD_MUTEX_RECURSIVE` | 카운트 증가 | `EPERM` 반환 | 레거시 콜백 구조. 새 코드에선 설계 냄새 |
| `PTHREAD_MUTEX_DEFAULT` | 구현 정의 (glibc는 NORMAL) | 정의되지 않음 | 이식성 코드에서 가정 금지 |

### 2.3 Robust mutex — 프로세스가 lock을 쥔 채 죽으면?

멀티 프로세스 설계(A7)에서 공유 메모리에 mutex를 두면 `PTHREAD_PROCESS_SHARED` 속성이 필요하다. 그런데 한 프로세스가 lock을 쥔 채 segfault로 죽으면? 일반 mutex라면 **다른 프로세스는 영원히 막힌다**. 센서 데몬 하나가 죽었다고 시스템 전체가 멈추면 안 된다. 그래서 **robust mutex**가 있다. 주인이 죽으면 다음 lock이 `EOWNERDEAD`를 돌려주고, 받은 쪽이 데이터를 복구한 뒤 `pthread_mutex_consistent()`로 "정상화했다"고 표시한다.

```c
// ex3_robust.c — 공유 메모리의 process-shared robust mutex: 주인이 죽으면?
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

struct shared { pthread_mutex_t m; int value; };

int main(void)
{
    struct shared *s = mmap(NULL, sizeof *s, PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (s == MAP_FAILED) { perror("mmap"); exit(1); }

    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_setpshared(&a, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&a, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(&s->m, &a);

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid == 0) {                       /* 자식: lock 잡고 값 쓰다가 죽는다 */
        pthread_mutex_lock(&s->m);
        s->value = 42;
        printf("child %d: locked, value=42, now crashing\n", getpid());
        fflush(stdout);
        abort();                          /* unlock 없이 사망 */
    }
    waitpid(pid, NULL, 0);

    int r = pthread_mutex_lock(&s->m);    /* 일반 mutex였다면 영원히 막힌다 */
    printf("parent: lock -> %s\n", r ? strerror(r) : "OK");
    if (r == EOWNERDEAD) {
        printf("parent: previous owner died, value=%d -> repair & mark consistent\n",
               s->value);
        s->value = 0;                     /* 데이터 복구는 응용의 몫 */
        pthread_mutex_consistent(&s->m);
    }
    pthread_mutex_unlock(&s->m);
    r = pthread_mutex_lock(&s->m);
    printf("parent: lock again -> %s\n", r ? strerror(r) : "OK");
    pthread_mutex_unlock(&s->m);
    return 0;
}
```
```text
child 32580: locked, value=42, now crashing
parent: lock -> Owner died
parent: previous owner died, value=42 -> repair & mark consistent
parent: lock again -> OK
```
출력에서 볼 것: 부모의 lock이 막히지 않고 `EOWNERDEAD`("Owner died")로 돌아왔다. 자식이 반쯤 써 놓은 `value=42`를 보고 복구할 기회를 얻는다. `pthread_mutex_consistent()` 없이 unlock하면 그 mutex는 영구히 `ENOTRECOVERABLE` 상태가 된다.

커널은 이걸 어떻게 아나? strace로 보면 모든 스레드가 시작할 때 `set_robust_list`를 부른다.

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && strace -f -e trace=set_robust_list,futex ./ex3 2>&1 | head -12'
```
```text
set_robust_list(0xf44e42807040, 24)     = 0
strace: Process 202634 attached
[pid 202634] set_robust_list(0xf44e42807040, 24) = 0
child 202634: locked, value=42, now crashing
[pid 202634] --- SIGABRT {si_signo=SIGABRT, si_code=SI_TKILL, si_pid=202634, si_uid=501} ---
[pid 202634] +++ killed by SIGABRT +++
--- SIGCHLD {si_signo=SIGCHLD, si_code=CLD_KILLED, si_pid=202634, si_uid=501, si_status=SIGABRT, si_utime=0, si_stime=0} ---
parent: lock -> Owner died
parent: previous owner died, value=42 -> repair & mark consistent
parent: lock again -> OK
+++ exited with 0 +++
```
출력에서 볼 것: glibc는 스레드마다 "내가 지금 쥐고 있는 robust mutex 목록"의 head 주소를 `set_robust_list`로 커널에 등록해 둔다. 스레드가 죽으면 커널이 그 목록을 훑어서 lock word에 `FUTEX_OWNER_DIED` 비트를 세우고 대기자를 깨운다. 그래서 부모의 lock은 futex syscall조차 없이 바로 `EOWNERDEAD`를 받았다(PID는 실행마다 다르다).

---

## 3. Condition variable — "조건이 될 때까지 잠들기"

### 3.1 직관

mutex는 "들어가도 되나?"만 답한다. 하지만 생산자-소비자 큐에서 소비자가 원하는 것은 "**큐가 비어 있지 않을 때까지** 기다리기"다. mutex만으로 하면 lock → 확인 → unlock → 다시 lock … 하는 busy-polling이 된다. **condition variable(condvar)**은 "조건이 바뀌었을 수 있으니 다시 확인해 봐"라는 **알림 채널**이다. RTOS event flag와 비슷하지만 중요한 차이가 있다. condvar는 **상태를 저장하지 않는다**. 아무도 기다리지 않을 때 보낸 signal은 그냥 사라진다. 그래서 "조건(predicate)" 자체는 반드시 공유 변수(`count` 같은)로 들고 있어야 하고, 그 변수는 mutex로 보호한다.

핵심 함수 `pthread_cond_wait(&cv, &m)`는 세 일을 **원자적으로** 한다. (1) mutex를 풀고 (2) 잠들고 (3) 깨어나면 mutex를 다시 잡은 뒤 반환한다. (1)과 (2) 사이에 틈이 있으면 그 틈에 온 signal을 놓쳐서 영원히 잔다(lost wakeup). 이 원자성이 condvar의 존재 이유다.

### 3.2 규칙: predicate는 항상 `while`로 다시 확인한다

깨어났다고 조건이 참이라는 보장은 없다. 이유는 두 가지다.

- **spurious wakeup**: POSIX는 아무도 signal하지 않았는데 wait가 반환되는 것을 허용한다. 구현 내부 사정(futex 경합 등) 때문이다.
- **stolen wakeup**: 생산자가 signal을 보내 소비자 A를 깨웠는데, A가 mutex를 다시 잡기 전에 소비자 B가 먼저 mutex를 잡고 아이템을 가져가 버린다. A가 깨어났을 때 큐는 다시 비어 있다. 실무에서 훨씬 자주 일어나는 쪽이 이것이다.

그래서 `if (count == 0) wait;`가 아니라 `while (count == 0) wait;`이다.

### 3.3 예제 4 — bounded queue (생산자 2, 소비자 2, 용량 4)

```c
// ex4_bq.c — condvar로 만든 bounded queue (생산자 2, 소비자 2, 용량 4)
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define CAP 4
#define PER_PRODUCER 100000
static int buf[CAP], head, tail, count;
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;
static pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;
static long full_waits, empty_waits;      /* m으로 보호 */

static void put(int v)
{
    pthread_mutex_lock(&m);
    while (count == CAP) {                 /* if가 아니라 while! */
        full_waits++;
        pthread_cond_wait(&not_full, &m);  /* 원자적으로 unlock+sleep, 깨면 재lock */
    }
    buf[tail] = v; tail = (tail + 1) % CAP; count++;
    pthread_cond_signal(&not_empty);
    pthread_mutex_unlock(&m);
}

static int get(void)
{
    pthread_mutex_lock(&m);
    while (count == 0) {
        empty_waits++;
        pthread_cond_wait(&not_empty, &m);
    }
    int v = buf[head]; head = (head + 1) % CAP; count--;
    pthread_cond_signal(&not_full);
    pthread_mutex_unlock(&m);
    return v;
}

static void *producer(void *arg)
{
    (void)arg;
    for (int i = 1; i <= PER_PRODUCER; i++) put(i);
    put(-1);                               /* 종료 표시(poison pill) */
    return NULL;
}

static void *consumer(void *arg)
{
    long sum = 0;
    for (int v; (v = get()) != -1; ) sum += v;
    *(long *)arg = sum;
    return NULL;
}

int main(void)
{
    pthread_t p[2], c[2];
    long sums[2];
    for (int i = 0; i < 2; i++) {
        if (pthread_create(&p[i], NULL, producer, NULL) ||
            pthread_create(&c[i], NULL, consumer, &sums[i])) {
            perror("pthread_create"); exit(1);
        }
    }
    for (int i = 0; i < 2; i++) { pthread_join(p[i], NULL); pthread_join(c[i], NULL); }
    long expect = 2L * PER_PRODUCER * (PER_PRODUCER + 1) / 2;
    printf("sum = %ld + %ld = %ld (expected %ld)\n",
           sums[0], sums[1], sums[0] + sums[1], expect);
    printf("waits: full=%ld empty=%ld\n", full_waits, empty_waits);
    return 0;
}
```
```text
sum = 4960064727 + 5040035273 = 10000100000 (expected 10000100000)
waits: full=62889 empty=63407
sum = 5071943111 + 4928156889 = 10000100000 (expected 10000100000)
waits: full=63481 empty=63698
```
출력에서 볼 것: 두 번 실행 모두 합계가 정확하다. 소비자 둘이 나눠 가진 비율은 매번 다르다. 용량 4짜리 큐라서 6만 번 넘게 "가득 참/비어 있음" 대기가 일어났다.

### 3.4 `while`을 `if`로 바꾸면 — 실제로 깨진다

같은 소스에서 두 `while`만 `if`로 바꾼 버전이다.

```sh
cd ~/.elinux_scratch/A8 && sed -e 's/while (count == 0) {/if (count == 0) {/' -e 's/while (count == CAP) {/if (count == CAP) {/' ex4_bq.c > ex4b_if.c
limactl shell elinux -- bash -lc 'cd /tmp/A8 && gcc -std=gnu11 -Wall -Wextra -O2 -pthread /Users/donh/.elinux_scratch/A8/ex4b_if.c -o ex4b && for i in 1 2 3; do timeout 5 ./ex4b; done'
```
```text
sum = 5828403673 + 5407020881 = 11235424554 (expected 10000100000)
waits: full=179 empty=177
sum = 6233117034 + 5488518898 = 11721635932 (expected 10000100000)
waits: full=51 empty=50
sum = 6193421485 + 4638777509 = 10832198994 (expected 10000100000)
waits: full=40 empty=39
```
출력에서 볼 것: 합계가 **틀렸다**(10~17% 더 큼). stolen wakeup으로 깬 소비자가 빈 큐에서 옛 슬롯 값을 한 번 더 읽고, 생산자는 가득 찬 큐에 덮어쓰면서 `count`가 범위를 벗어났다. 크래시도 경고도 없이 조용히 틀린다는 점이 무섭다.

### 3.5 condvar 함정 정리

- predicate를 바꾸는 쓰기는 **반드시 같은 mutex를 잡고** 한다. 안 그러면 "확인했는데 비어 있음 → (그 사이 생산자가 넣고 signal) → 잠듦"의 lost wakeup이 생긴다.
- `signal`은 최소 한 명을 깨우고, `broadcast`는 모두 깨운다. 조건이 여러 종류인데 condvar 하나를 공유한다면(위 예제를 condvar 하나로 짰다면) `broadcast`를 써야 엉뚱한 쪽만 깨우는 사고가 없다.
- 타임아웃은 `pthread_cond_timedwait()`인데 기본 시계가 `CLOCK_REALTIME`이다. NTP·GPS가 시간을 점프시키면 타임아웃이 엉뚱해진다. 임베디드에선 `pthread_condattr_setclock(&ca, CLOCK_MONOTONIC)`을 습관화한다.
- condvar는 async-signal-safe가 아니다. 시그널 핸들러에서 `pthread_cond_signal`을 부르지 말 것(A6). 핸들러에선 `sem_post`가 허용된다.

---

## 4. Semaphore — 개수를 세는 열쇠

### 4.1 정의

**semaphore**는 정수 카운터 + 대기 큐다. `sem_wait()`(P 연산)은 값이 0보다 크면 1 줄이고 지나가고, 0이면 잠든다. `sem_post()`(V 연산)은 1 늘리고 잠든 스레드가 있으면 깨운다. Don에게는 RTOS counting semaphore 그대로다.

mutex와의 차이는 두 가지다. (1) **소유자가 없다** — 아무 스레드나 post할 수 있다. 그래서 "스레드 A가 준비되면 스레드 B에게 알리기" 같은 신호 용도로 쓴다. (2) 값이 1보다 클 수 있어서 "자원 N개 중 하나 빌리기"를 표현한다. 반대로 소유자가 없으니 **priority inheritance를 할 수 없다** — 누구의 우선순위를 올려야 할지 모르기 때문이다. 상호 배제가 목적이면 semaphore가 아니라 mutex를 쓴다.

POSIX semaphore는 두 종류다.

| 종류 | 만들기 | 어디에 사나 | 용도 |
|---|---|---|---|
| unnamed | `sem_init(&s, pshared, value)` | 내 변수(스레드 간) 또는 공유 메모리 안(`pshared=1`) | 같은 프로세스 스레드끼리 |
| named | `sem_open("/name", O_CREAT, mode, value)` | `/dev/shm/sem.name` 파일(tmpfs) | 관계없는 프로세스끼리 이름으로 |

### 4.2 예제 5 — 슬롯 2개 + 프로세스 간 신호

unnamed semaphore로 "동시에 최대 2명만 I2C 버스 사용"을 표현하고, named semaphore로 자식 프로세스가 "초기화 끝났다"를 부모에게 알린다.

```c
// ex5_sem.c — unnamed sem(스레드 간 슬롯 2개) + named sem(프로세스 간 신호)
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static sem_t slots;                 /* unnamed: 같은 프로세스 스레드끼리 */
static atomic_int inside, max_inside;

static void *user(void *arg)
{
    long id = (long)arg;
    sem_wait(&slots);               /* P: 0이면 잠든다 */
    int now = atomic_fetch_add(&inside, 1) + 1;
    int old = atomic_load(&max_inside);
    while (now > old && !atomic_compare_exchange_weak(&max_inside, &old, now)) ;
    printf("thread %ld in  (inside=%d)\n", id, now);
    usleep(100000);
    atomic_fetch_sub(&inside, 1);
    sem_post(&slots);               /* V */
    return NULL;
}

int main(void)
{
    pthread_t t[5];
    setvbuf(stdout, NULL, _IOLBF, 0);   /* fork 전 버퍼 중복 방지 */
    if (sem_init(&slots, 0, 2) == -1) { perror("sem_init"); exit(1); }
    for (long i = 0; i < 5; i++) pthread_create(&t[i], NULL, user, (void *)i);
    for (int i = 0; i < 5; i++) pthread_join(t[i], NULL);
    printf("max concurrently inside = %d\n", atomic_load(&max_inside));

    sem_t *ready = sem_open("/a8_ready", O_CREAT, 0600, 0);   /* named */
    if (ready == SEM_FAILED) { perror("sem_open"); exit(1); }
    if (fork() == 0) {
        usleep(200000);
        printf("child : init done, sem_post\n");
        sem_post(ready);
        _exit(0);
    }
    if (system("ls -l /dev/shm/ | grep a8_ready") != 0) puts("not found");
    sem_wait(ready);
    printf("parent: got the signal\n");
    wait(NULL);
    sem_close(ready);
    sem_unlink("/a8_ready");
    return 0;
}
```
```text
thread 0 in  (inside=1)
thread 1 in  (inside=2)
thread 3 in  (inside=1)
thread 2 in  (inside=2)
thread 4 in  (inside=2)
max concurrently inside = 2
-rw------- 1 donh donh 32 Oct  5 22:42 sem.a8_ready
child : init done, sem_post
parent: got the signal
```
출력에서 볼 것: 5개 스레드가 있어도 동시에 안에 있는 수는 최대 2다. named semaphore는 `/dev/shm/sem.a8_ready`라는 32바이트 파일로 실제로 존재한다 — glibc가 이 파일을 `mmap`해서 그 안의 정수 하나를 futex로 쓰는 구조다. `sem_unlink`를 안 하면 프로세스가 죽어도 파일이 남는다(재부팅 전까지).

> 첫 실행에서는 `setvbuf` 없이 돌렸더니 자식의 `printf`가 출력되지 않았다. 파이프로 연결된 stdout은 full buffering이라 자식이 `_exit()`하며 버퍼를 버렸기 때문이다(A2의 fork + stdio 함정). 그래서 줄 버퍼링으로 바꿨다.

---

## 5. Read-write lock — 읽기는 여럿, 쓰기는 혼자

### 5.1 정의

센서 보정 테이블처럼 **읽기가 압도적으로 많고 쓰기는 드문** 데이터에 mutex를 쓰면 독자끼리도 줄을 서야 한다. **rwlock**은 읽기 잠금(`rdlock`)은 여럿이 동시에, 쓰기 잠금(`wrlock`)은 혼자만 잡게 한다.

### 5.2 예제 6 — mutex vs rwlock 경과 시간

```c
// ex6_rwlock.c — 읽기 위주 설정 테이블: mutex vs rwlock 경과 시간 비교
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define READERS 4
#define ROUNDS  20
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t rw = PTHREAD_RWLOCK_INITIALIZER;
static int config[16];              /* 예: 센서 보정 테이블 */
static int use_rw;

static void *reader(void *arg)
{
    long sum = 0;
    for (int r = 0; r < ROUNDS; r++) {
        if (use_rw) pthread_rwlock_rdlock(&rw); else pthread_mutex_lock(&mtx);
        for (int i = 0; i < 16; i++) sum += config[i];
        usleep(5000);               /* 읽는 데 5 ms 걸린다고 치자 */
        if (use_rw) pthread_rwlock_unlock(&rw); else pthread_mutex_unlock(&mtx);
    }
    *(long *)arg = sum;
    return NULL;
}

static double run(int rw_mode)
{
    pthread_t t[READERS];
    long sums[READERS];
    struct timespec a, b;
    use_rw = rw_mode;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < READERS; i++)
        if (pthread_create(&t[i], NULL, reader, &sums[i])) { perror("create"); exit(1); }
    for (int i = 0; i < READERS; i++) pthread_join(t[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &b);
    return (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
}

int main(void)
{
    for (int i = 0; i < 16; i++) config[i] = i;
    printf("mutex : %6.1f ms\n", run(0));
    printf("rwlock: %6.1f ms\n", run(1));
    printf("(ideal serial = %d ms, ideal parallel = %d ms)\n",
           READERS * ROUNDS * 5, ROUNDS * 5);
    return 0;
}
```
```text
mutex :  474.6 ms
rwlock:  113.0 ms
(ideal serial = 400 ms, ideal parallel = 100 ms)
```
출력에서 볼 것: mutex는 독자 4명이 줄을 서서 직렬(400 ms 근처), rwlock은 동시에 읽어서 병렬(100 ms 근처)에 가깝다. `usleep`의 잠듦·깨움 오버헤드만큼 이상값보다 조금 길다.

### 5.3 rwlock 함정

- **writer starvation**: glibc 기본 rwlock은 독자 우선이다. 독자가 끊임없이 오면 작성자는 영원히 못 들어갈 수 있다. glibc 확장 `pthread_rwlockattr_setkind_np(&a, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP)`로 작성자 우선으로 바꿀 수 있다.
- **임계 구역이 짧으면 오히려 느리다**: rwlock은 독자 수를 세는 atomic 연산 때문에 그 카운터 캐시 라인에서 코어끼리 경합한다. 임계 구역이 수십 ns면 mutex와 별 차이가 없거나 더 나쁘다. 위 예제는 일부러 5 ms를 넣었다.
- rdlock을 잡은 채 wrlock으로 "업그레이드"하는 API는 없다. 시도하면 자기 자신과 deadlock이다.
- 진짜로 읽기가 많고 쓰기가 드물며 짧다면, 커널은 RCU를 쓴다(F4). 유저스페이스에도 liburcu가 있다.

---

## 6. 유저스페이스 spinlock — 그리고 왜 대개 나쁜 생각인가

### 6.1 정의

**spinlock**은 잠들지 않고 `while (!try_lock()) ;`처럼 **CPU를 돌리며** 기다리는 락이다. glibc의 `pthread_spin_lock()`은 커널을 전혀 부르지 않는 순수 유저스페이스 busy-wait다. bare-metal 멀티코어나 커널(F4)에서는 기본 도구지만, 유저스페이스에서는 함정이 있다.

커널 spinlock이 안전한 이유는 `spin_lock()`이 **preemption을 끄기** 때문이다. 락을 쥔 코드는 스케줄러에 밀려나지 않고 짧은 일을 끝낸 뒤 바로 푼다. 그런데 유저 스레드는 preemption을 끌 수 없다. 락을 쥔 스레드가 타임 슬라이스를 다 써서 밀려나면, 나머지 스레드들은 **락 주인이 다시 스케줄될 때까지 CPU를 태우며** 돈다. 그 CPU는 정작 주인이 써야 할 CPU일 수도 있다.

### 6.2 예제 7 — spin vs mutex: wall time과 CPU time

```c
// ex7_spin.c — pthread_spin vs pthread_mutex: 경과 시간과 CPU 시간 비교
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#define MAXTH 16
#define ITERS 20000
static pthread_spinlock_t sl;
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
static int use_spin;
static volatile unsigned long shared;

static void *worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < ITERS; i++) {
        if (use_spin) pthread_spin_lock(&sl); else pthread_mutex_lock(&mx);
        for (int k = 0; k < 2000; k++) shared++;   /* 수 µs짜리 임계 구역 */
        if (use_spin) pthread_spin_unlock(&sl); else pthread_mutex_unlock(&mx);
    }
    return NULL;
}

static double tv(struct timeval t) { return t.tv_sec + t.tv_usec / 1e6; }

int main(int argc, char **argv)
{
    use_spin = argc > 1 && strcmp(argv[1], "spin") == 0;
    int nth = argc > 2 ? atoi(argv[2]) : 4;
    if (nth < 1 || nth > MAXTH) { fprintf(stderr, "1..16\n"); exit(1); }
    pthread_spin_init(&sl, PTHREAD_PROCESS_PRIVATE);
    struct timespec a, b;
    struct rusage ru;
    pthread_t t[MAXTH];
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < nth; i++)
        if (pthread_create(&t[i], NULL, worker, NULL)) { perror("create"); exit(1); }
    for (int i = 0; i < nth; i++) pthread_join(t[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &b);
    getrusage(RUSAGE_SELF, &ru);
    printf("%-5s th=%d wall=%6.2f s  cpu(user+sys)=%6.2f s  invol-ctxsw=%ld\n",
           use_spin ? "spin" : "mutex", nth,
           (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9,
           tv(ru.ru_utime) + tv(ru.ru_stime), ru.ru_nivcsw);
    return 0;
}
```
```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && for n in 4 8; do ./ex7 mutex $n; ./ex7 spin $n; done; echo "== taskset -c 0"; taskset -c 0 ./ex7 mutex 4; taskset -c 0 ./ex7 spin 4'
```
```text
mutex th=4 wall=  0.11 s  cpu(user+sys)=  0.13 s  invol-ctxsw=856
spin  th=4 wall=  0.36 s  cpu(user+sys)=  0.80 s  invol-ctxsw=3726
mutex th=8 wall=  0.15 s  cpu(user+sys)=  0.16 s  invol-ctxsw=774
spin  th=8 wall=  1.14 s  cpu(user+sys)=  3.45 s  invol-ctxsw=3640
== taskset -c 0
mutex th=4 wall=  0.07 s  cpu(user+sys)=  0.07 s  invol-ctxsw=49
spin  th=4 wall=  0.17 s  cpu(user+sys)=  0.16 s  invol-ctxsw=86
```
출력에서 볼 것: 4코어에 4스레드일 때 spin은 wall이 3배, **CPU 시간은 6배**다. 코어 수보다 스레드가 많은 8스레드에서는 wall 7.6배, CPU 21배로 폭발한다 — 주인이 밀려난 동안 나머지가 타임 슬라이스를 통째로 태우기 때문이다(`invol-ctxsw`, 즉 강제로 밀려난 횟수도 4배 이상). 한 코어(`taskset -c 0`)에서도 spin이 2배 이상 느리다. 같은 일을 하는데 배터리·발열 예산을 날리는 셈이다.

### 6.3 그럼 언제 쓰나

- 스레드를 **코어에 하나씩 고정**(`pthread_setaffinity_np`, `isolcpus`)하고, 그 코어에 다른 스레드가 없으며, 임계 구역이 수십 ns로 아주 짧을 때. 예: DPDK 같은 고속 패킷 처리, 전용 코어의 RT 루프.
- 그 외에는 mutex를 쓴다. glibc mutex는 경합이 없으면 이미 atomic 한 번으로 끝나므로(8절) spinlock이 얻는 이득이 거의 없다. 짧게 돌다가 잠들고 싶으면 glibc 확장 `PTHREAD_MUTEX_ADAPTIVE_NP` 타입이 있다.
- SCHED_FIFO 스레드가 같은 코어의 낮은 우선순위 주인을 기다리며 spin하면 **영원히** 돈다(주인이 절대 스케줄되지 않는다). RT 시스템에서 유저 spinlock이 특히 위험한 이유다.

---

## 7. C11 atomics와 memory ordering — arm64는 순서를 지켜 주지 않는다

### 7.1 atomic 연산으로 lost update 고치기

`<stdatomic.h>`의 `atomic_fetch_add()`는 읽기-더하기-쓰기를 **쪼갤 수 없는 한 연산**으로 만든다. ex1과 같은 경쟁을 atomic으로 고친다.

```c
// ex8_atomic.c — ex1과 같은 경쟁을 C11 atomic으로 고친다
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define NTHREADS 4
#define ITERS    1000000
static atomic_long counter;

static void *worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < ITERS; i++)
        atomic_fetch_add_explicit(&counter, 1, memory_order_relaxed);
    return NULL;
}

int main(void)
{
    pthread_t t[NTHREADS];
    for (int i = 0; i < NTHREADS; i++)
        if (pthread_create(&t[i], NULL, worker, NULL)) { perror("create"); exit(1); }
    for (int i = 0; i < NTHREADS; i++)
        pthread_join(t[i], NULL);
    printf("counter = %ld (expected %d)\n", atomic_load(&counter), NTHREADS * ITERS);
    return 0;
}
```
```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && gcc -std=gnu11 -Wall -Wextra -O2 -pthread /Users/donh/.elinux_scratch/A8/ex8_atomic.c -o ex8 && ./ex8 && objdump -d --no-show-raw-insn ex8 | sed -n "/<worker>:/,/ret/p"; objdump -d --no-show-raw-insn ex8 | grep -A12 "<__aarch64_ldadd8_relax>:"'
```
```text
counter = 4000000 (expected 4000000)
0000000000000b80 <worker>:
 b80:	stp	x29, x30, [sp, #-32]!
 b84:	mov	x29, sp
 b88:	stp	x19, x20, [sp, #16]
 b8c:	mov	w19, #0x4240                	// #16960
 b90:	adrp	x20, 20000 <__data_start>
 b94:	add	x20, x20, #0x18
 b98:	movk	w19, #0xf, lsl #16
 b9c:	nop
 ba0:	mov	x1, x20
 ba4:	mov	x0, #0x1                   	// #1
 ba8:	bl	bd0 <__aarch64_ldadd8_relax>
 bac:	subs	w19, w19, #0x1
 bb0:	b.ne	ba0 <worker+0x20>  // b.any
 bb4:	ldp	x19, x20, [sp, #16]
 bb8:	mov	x0, #0x0                   	// #0
 bbc:	ldp	x29, x30, [sp], #32
 bc0:	ret
0000000000000bd0 <__aarch64_ldadd8_relax>:
 bd0:	adrp	x16, 20000 <__data_start>
 bd4:	ldrb	w16, [x16, #32]
 bd8:	cbz	w16, be4 <__aarch64_ldadd8_relax+0x14>
 bdc:	ldadd	x0, x0, [x1]
 be0:	ret
 be4:	mov	x16, x0
 be8:	ldxr	x0, [x1]
 bec:	add	x17, x0, x16
 bf0:	stxr	w15, x17, [x1]
 bf4:	cbnz	w15, be8 <__aarch64_ldadd8_relax+0x14>
 bf8:	ret
```
출력에서 볼 것: 이제 400만이 정확히 나온다. 어셈블리가 흥미롭다. gcc는 기본으로 **outline atomics**를 써서 `__aarch64_ldadd8_relax`라는 헬퍼를 부른다. 헬퍼는 시작할 때 CPU가 ARMv8.1 LSE(Large System Extensions) atomics를 지원하는지 확인한 플래그(`ldrb w16`)를 보고,

- 지원하면 `ldadd` **한 명령**으로 메모리에서 직접 더한다(이 VM의 CPU는 `/proc/cpuinfo` Features에 `atomics`가 있으니 이 경로).
- 아니면 `ldxr`/`stxr` 루프를 돈다. Cortex-M의 `LDREX`/`STREX`와 같은 **exclusive monitor** 방식이다 — 읽은 뒤 누가 그 주소를 건드렸으면 `stxr`가 실패(`w15 != 0`)하고 처음부터 다시 한다.

타깃 SoC가 확실히 LSE를 가지면 `-march=armv8.1-a` 이상(또는 `-mcpu=cortex-a55` 등)으로 빌드해서 헬퍼 호출 없이 인라인 `ldadd`를 쓰게 할 수 있다.

```sh
limactl shell elinux -- bash -lc 'gcc -std=gnu11 -O2 -march=armv8.1-a -S -o - /Users/donh/.elinux_scratch/A8/ex8_atomic.c | sed -n "/^worker:/,/ret/p" | grep -v "^\s*\."'
```
```text
worker:
	mov	w0, 16960
	adrp	x1, .LANCHOR0
	add	x1, x1, :lo12:.LANCHOR0
	movk	w0, 0xf, lsl 16
	mov	x2, 1
	ldadd	x2, x3, [x1]
	subs	w0, w0, #1
	bne	.L2
	mov	x0, 0
	ret
```
출력에서 볼 것: 루프 안이 `ldadd` 하나다. (i.MX8M의 Cortex-A53은 ARMv8.0이라 LSE가 **없다** — 그런 보드에서 `-march=armv8.1-a`로 빌드하면 illegal instruction으로 죽는다. 그래서 배포판 기본은 outline atomics다.)

### 7.2 memory ordering — 왜 필요한가

atomic이 "쪼개지지 않음"만 해결하면 끝일까? 아니다. 두 번째 문제는 **순서**다. 현대 CPU와 컴파일러는 성능을 위해 메모리 접근 순서를 바꾼다.

- 컴파일러: 서로 다른 주소에 대한 load/store를 재배치한다(1.4절에서 루프가 통째로 합쳐진 것처럼).
- CPU: arm64는 **weakly ordered** 아키텍처다. store buffer, out-of-order 실행, 캐시 coherence 지연 때문에 **다른 코어가 보기에** 내 store 두 개의 순서가 뒤집힐 수 있고, 내 load 두 개도 순서가 뒤집혀 실행될 수 있다. x86은 TSO(Total Store Order)라 store-store, load-load 순서를 하드웨어가 지켜 주지만 arm64는 아니다.

Don은 이미 DMA 디스크립터를 쓸 때 "데이터 쓰고 → `DMB`/`DSB` → owner 비트 넘기기"를 했다. 그게 정확히 이 문제다. 다른 관찰자(DMA 엔진)가 owner 비트를 데이터보다 먼저 보면 안 되니까. C11은 이걸 `memory_order`로 표현하고 컴파일러가 알맞은 명령을 고른다.

| memory_order | 보장 | arm64 load | arm64 store | 쓰는 곳 |
|---|---|---|---|---|
| `relaxed` | 그 변수 하나의 원자성만. 다른 접근과 순서 보장 없음 | `ldr` | `str` | 통계 카운터 |
| `acquire` (load) | 이 load **뒤의** 접근이 앞으로 못 올라온다 | `ldar` (`ldapr`) | — | "준비됐나?" 확인 |
| `release` (store) | 이 store **앞의** 접근이 뒤로 못 내려간다 | — | `stlr` | "준비됐다" 공개 |
| `acq_rel` | RMW에 둘 다 | `ldaxr`/`stlxr` 또는 `ldaddal` 등 | 같음 | lock 구현 |
| `seq_cst` (기본) | acquire/release + 모든 스레드가 하나의 전체 순서에 동의 | `ldar` | `stlr` | 잘 모르겠으면 이것 |

표의 명령이 실제로 그런지 확인한다. 함수마다 순서만 바꾼 작은 파일을 컴파일만 한다.

```c
// ex17_orders.c — memory_order별로 arm64 명령이 어떻게 바뀌나 (컴파일만)
#include <stdatomic.h>
atomic_int x;
int  ld_relaxed(void) { return atomic_load_explicit(&x, memory_order_relaxed); }
int  ld_acquire(void) { return atomic_load_explicit(&x, memory_order_acquire); }
int  ld_seq_cst(void) { return atomic_load(&x); }
void st_relaxed(int v) { atomic_store_explicit(&x, v, memory_order_relaxed); }
void st_release(int v) { atomic_store_explicit(&x, v, memory_order_release); }
void st_seq_cst(int v) { atomic_store(&x, v); }
void fence_rel(void)   { atomic_thread_fence(memory_order_release); }
void fence_sc(void)    { atomic_thread_fence(memory_order_seq_cst); }
```
```sh
limactl shell elinux -- bash -lc 'for M in "" "-march=armv8.3-a+rcpc"; do echo "### gcc -O2 $M"; gcc -std=gnu11 -Wall -Wextra -O2 $M -c /Users/donh/.elinux_scratch/A8/ex17_orders.c -o /tmp/A8/ex17.o && objdump -d --no-show-raw-insn /tmp/A8/ex17.o | grep -E "^[0-9a-f]+ <|ldr|ldar|ldapr|str|stlr|dmb" ; done'
```
```text
### gcc -O2 
0000000000000000 <ld_relaxed>:
   8:	ldr	w0, [x0]
0000000000000010 <ld_acquire>:
  18:	ldar	w0, [x0]
0000000000000020 <ld_seq_cst>:
  28:	ldar	w0, [x0]
0000000000000030 <st_relaxed>:
  38:	str	w0, [x1]
0000000000000040 <st_release>:
  48:	stlr	w0, [x1]
0000000000000050 <st_seq_cst>:
  58:	stlr	w0, [x1]
0000000000000060 <fence_rel>:
  60:	dmb	ish
0000000000000068 <fence_sc>:
  68:	dmb	ish
### gcc -O2 -march=armv8.3-a+rcpc
0000000000000000 <ld_relaxed>:
   8:	ldr	w0, [x0]
0000000000000010 <ld_acquire>:
  18:	ldapr	w0, [x0]
0000000000000020 <ld_seq_cst>:
  28:	ldar	w0, [x0]
0000000000000030 <st_relaxed>:
  38:	str	w0, [x1]
0000000000000040 <st_release>:
  48:	stlr	w0, [x1]
0000000000000050 <st_seq_cst>:
  58:	stlr	w0, [x1]
0000000000000060 <fence_rel>:
  60:	dmb	ish
0000000000000068 <fence_sc>:
  68:	dmb	ish
```
출력에서 볼 것: relaxed는 평범한 `ldr`/`str`이다 — atomic이라고 다 비싼 게 아니다. acquire/seq_cst load는 `ldar`, release/seq_cst store는 `stlr`. RCpc 확장이 있는 CPU(ARMv8.3+)를 지정하면 acquire load가 더 약한(그래서 더 빠를 수 있는) `ldapr`로 바뀐다. 독립 fence는 `dmb ish`(inner shareable 도메인 전체 barrier)다. arm64는 `ldar`/`stlr`라는 "순서가 붙은 load/store" 명령이 있어서 x86처럼 무거운 full fence 없이 acquire/release를 표현할 수 있다.

### 7.3 예제 9 — message-passing litmus: 순서가 정말 뒤집히나?

**litmus test**는 메모리 모델의 특정 동작이 일어나는지 보는 작은 동시성 실험이다. 가장 유명한 것이 **message passing(MP)**이다.

- writer: `data = 1;` 그다음 `flag = 1;`
- reader: `flag`를 읽어서 1이면 `data`를 읽는다.
- 질문: reader가 `flag == 1`인데 `data == 0`을 볼 수 있나?

x86에선 불가능하다. arm64에선 둘 다 relaxed면 **가능하다**. 실제로 보이는지 VM에서 실험한다. writer와 reader가 큰 배열을 반대 방향으로 훑어서 매 슬롯마다 "막 쓰는 중"인 순간에 마주치게 만드는 방식이다.

```c
// ex9_mp.c — message-passing litmus: writer는 data→flag 순서로 쓰고,
//            reader는 flag→data 순서로 읽는다. "flag=1인데 data=0"이 보이나?
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define N    (1 << 20)     /* 슬롯 수 */
#define STR  16            /* 슬롯 간격 64 B = 캐시 라인 하나씩 */
#define REPS 20
#ifdef USE_ACQREL
#define ST_ORD memory_order_release
#define LD_ORD memory_order_acquire
#else
#define ST_ORD memory_order_relaxed
#define LD_ORD memory_order_relaxed
#endif
static atomic_int *data, *flag, start;

static void *writer(void *arg)
{
    (void)arg;
    while (!atomic_load(&start)) ;
    for (int i = 0; i < N; i++) {
        atomic_store_explicit(&data[i * STR], 1, memory_order_relaxed);
        atomic_store_explicit(&flag[i * STR], 1, ST_ORD);    /* "준비됐다" */
    }
    return NULL;
}

int main(void)
{
    long seen = 0, weird = 0;
    for (int rep = 0; rep < REPS; rep++) {
        data = calloc((size_t)N * STR, sizeof(int));
        flag = calloc((size_t)N * STR, sizeof(int));
        if (!data || !flag) { perror("calloc"); exit(1); }
        pthread_t w;
        atomic_store(&start, 0);
        if (pthread_create(&w, NULL, writer, NULL)) { perror("create"); exit(1); }
        atomic_store(&start, 1);
        for (int pass = 0; pass < 4; pass++)              /* 뒤에서부터 훑어서 */
            for (int i = N - 1; i >= 0; i--) {             /* writer와 마주친다 */
                int f = atomic_load_explicit(&flag[i * STR], LD_ORD);
                int d = atomic_load_explicit(&data[i * STR], memory_order_relaxed);
                if (f) { seen++; if (!d) weird++; }
            }
        pthread_join(w, NULL);
        free(data); free(flag);
    }
    printf("flag==1 observed %ld times, of which data==0: %ld\n", seen, weird);
    return 0;
}
```
```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && gcc -std=gnu11 -Wall -Wextra -O2 -pthread /Users/donh/.elinux_scratch/A8/ex9_mp.c -o ex9_relaxed && gcc -std=gnu11 -Wall -Wextra -O2 -pthread -DUSE_ACQREL /Users/donh/.elinux_scratch/A8/ex9_mp.c -o ex9_acqrel && echo "== relaxed"; for i in 1 2 3 4 5; do ./ex9_relaxed; done; echo "== release/acquire"; for i in 1 2 3 4 5; do ./ex9_acqrel; done'
```
```text
== relaxed
flag==1 observed 58446506 times, of which data==0: 7
flag==1 observed 45164671 times, of which data==0: 4
flag==1 observed 57369716 times, of which data==0: 3
flag==1 observed 66391084 times, of which data==0: 6
flag==1 observed 64973841 times, of which data==0: 3
== release/acquire
flag==1 observed 49299218 times, of which data==0: 0
flag==1 observed 52166584 times, of which data==0: 0
flag==1 observed 52648518 times, of which data==0: 0
flag==1 observed 64261521 times, of which data==0: 0
flag==1 observed 59575445 times, of which data==0: 0
```
출력에서 볼 것: relaxed에서는 약 5천만 번 중 **3~7번**, "flag는 1인데 data는 0"인 불가능해 보이는 상태가 실제로 관찰됐다. release/acquire로 바꾸면 0번이다. 횟수는 실행마다 다르고 아주 드물다 — 바로 이 "드묾" 때문에 이런 버그는 테스트를 통과하고 현장에서 한 달에 한 번 터진다.

이게 컴파일러 탓이 아니라 **하드웨어 재배치**인지 확인하려면 디스어셈블리를 본다.

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && for b in ex9_relaxed ex9_acqrel; do echo "=== $b writer"; objdump -d --no-show-raw-insn $b | sed -n "/<writer>:/,/ret/p"; done'
```
```text
=== ex9_relaxed writer
0000000000000c00 <writer>:
 c00:	adrp	x2, 20000 <__data_start>
 c04:	add	x1, x2, #0x18
 c08:	ldar	w0, [x1]
 c0c:	cbz	w0, c08 <writer+0x8>
 c10:	add	x2, x2, #0x18
 c14:	mov	x0, #0x0                   	// #0
 c18:	mov	x3, #0x4000000             	// #67108864
 c1c:	nop
 c20:	ldr	x1, [x2, #8]
 c24:	mov	w4, #0x1                   	// #1
 c28:	add	x1, x1, x0
 c2c:	str	w4, [x1]
 c30:	ldr	x1, [x2, #16]
 c34:	add	x1, x1, x0
 c38:	str	w4, [x1]
 c3c:	add	x0, x0, #0x40
 c40:	cmp	x0, x3
 c44:	b.ne	c20 <writer+0x20>  // b.any
 c48:	mov	x0, #0x0                   	// #0
 c4c:	ret
=== ex9_acqrel writer
0000000000000c00 <writer>:
 c00:	adrp	x2, 20000 <__data_start>
 c04:	add	x1, x2, #0x18
 c08:	ldar	w0, [x1]
 c0c:	cbz	w0, c08 <writer+0x8>
 c10:	add	x2, x2, #0x18
 c14:	mov	x0, #0x0                   	// #0
 c18:	mov	x3, #0x4000000             	// #67108864
 c1c:	nop
 c20:	ldr	x1, [x2, #8]
 c24:	mov	w4, #0x1                   	// #1
 c28:	add	x1, x1, x0
 c2c:	str	w4, [x1]
 c30:	ldr	x1, [x2, #16]
 c34:	add	x1, x1, x0
 c38:	stlr	w4, [x1]
 c3c:	add	x0, x0, #0x40
 c40:	cmp	x0, x3
 c44:	b.ne	c20 <writer+0x20>  // b.any
 c48:	mov	x0, #0x0                   	// #0
 c4c:	ret
```
reader 쪽 load 두 개(같은 방식으로 `main`에서 뽑음):

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && for b in ex9_relaxed ex9_acqrel; do echo "=== $b reader"; objdump -d --no-show-raw-insn $b | sed -n "/ 9b0:/,/ 9c8:/p"; done'
```
```text
=== ex9_relaxed reader
 9b0:	ldr	x1, [x19, #16]
 9b4:	add	x1, x1, x0
 9b8:	ldr	w2, [x1]
 9bc:	ldr	x1, [x19, #8]
 9c0:	add	x1, x1, x0
 9c4:	ldr	w1, [x1]
 9c8:	cbz	w2, 9d8 <main+0xd8>
=== ex9_acqrel reader
 9b0:	ldr	x1, [x19, #16]
 9b4:	add	x1, x1, x0
 9b8:	ldar	w2, [x1]
 9bc:	ldr	x1, [x19, #8]
 9c0:	add	x1, x1, x0
 9c4:	ldr	w1, [x1]
 9c8:	cbz	w2, 9d8 <main+0xd8>
```
출력에서 볼 것: 두 바이너리 모두 **프로그램 순서가 소스와 같다** — writer는 `data`(`[x2,#8]`가 가리키는 배열) store 후 `flag`(`[x2,#16]`) store, reader는 `flag` load(`9b8`) 후 `data` load(`9c4`). 즉 컴파일러는 순서를 바꾸지 않았다. 그런데도 relaxed에서 `data==0`이 보였으니 **CPU가 재배치한 것**이다. 차이는 딱 두 명령이다: writer의 두 번째 store가 `str` → `stlr`(release), reader의 첫 load가 `ldr` → `ldar`(acquire). 이 두 명령이 그림 3의 화살표를 만든다.

```svg
<svg viewBox="0 0 660 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="160" y="25" font-size="14" text-anchor="middle">writer 스레드 (CPU0)</text>
  <text x="500" y="25" font-size="14" text-anchor="middle">reader 스레드 (CPU1)</text>
  <rect x="30" y="45" width="260" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="70" font-size="13" text-anchor="middle">data = 1   (str, 평범한 store)</text>
  <line x1="160" y1="85" x2="160" y2="120" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="20" y1="127" x2="300" y2="127" stroke="#3f9a6b" stroke-dasharray="5 3"/>
  <text x="160" y="145" font-size="12" text-anchor="middle">release 장벽: 위의 접근은 아래로 못 내려감</text>
  <rect x="30" y="155" width="260" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="160" y="180" font-size="13" text-anchor="middle">flag = 1   (stlr = release)</text>
  <rect x="370" y="155" width="260" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="500" y="180" font-size="13" text-anchor="middle">flag 읽기 → 1   (ldar = acquire)</text>
  <line x1="290" y1="175" x2="368" y2="175" stroke="#3f9a6b" stroke-width="2" marker-end="url(#ah3)"/>
  <text x="330" y="215" font-size="12" text-anchor="middle">synchronizes-with</text>
  <line x1="360" y1="232" x2="640" y2="232" stroke="#3f9a6b" stroke-dasharray="5 3"/>
  <text x="405" y="252" font-size="12">acquire 장벽: 아래 접근은 위로 못 올라감</text>
  <line x1="390" y1="195" x2="390" y2="260" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="370" y="262" width="260" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="500" y="287" font-size="13" text-anchor="middle">data 읽기 → 반드시 1   (ldr)</text>
  <rect x="30" y="315" width="600" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="330" y="337" font-size="13" text-anchor="middle">둘 다 relaxed(str/ldr)였다면: 약 5천만 번 중 3~7번 data = 0 관찰 (ex9)</text>
</svg>
```
그림 3 — acquire/release 순서. release store 이전의 모든 쓰기는, 그 값을 acquire load로 읽은 스레드에게 반드시 보인다. 짝(pair)이 맞아야 효과가 있다 — 한쪽만 걸면 보장 없음.

### 7.4 예제 10 — lock-free SPSC ring buffer

Don이 ISR ↔ 메인 루프 사이에 쓰던 링버퍼를 멀티코어에서 안전하게 만든 것이 **SPSC(single-producer single-consumer) ring**이다. 생산자 하나, 소비자 하나라는 조건이면 락 없이 acquire/release 두 쌍만으로 정확하다.

```
 buf[SIZE]:  [ . . . X X X X X . . . ]
                     ^         ^
                   head      tail
   consumer owns head (reads buf[head], then head++ with release)
   producer owns tail (writes buf[tail], then tail++ with release)
   each side reads the OTHER index with acquire
```

규칙은 하나다: **데이터를 쓰고 나서** 인덱스를 release로 공개하고, 상대는 인덱스를 acquire로 읽은 **다음에** 데이터를 읽는다. 그림 3의 MP 패턴 그대로다. 그리고 head와 tail을 서로 다른 캐시 라인에 둔다(`_Alignas(64)`) — 같은 라인에 있으면 두 코어가 라인을 서로 뺏는 **false sharing**으로 느려진다.

```c
// ex10_spsc.c — lock-free SPSC ring buffer (생산자 1, 소비자 1), acquire/release
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SIZE 1024                       /* 2의 거듭제곱 → % 대신 & */
#define ITEMS 20000000UL
struct ring {
    _Alignas(64) atomic_size_t head;    /* 소비자만 쓴다 */
    _Alignas(64) atomic_size_t tail;    /* 생산자만 쓴다 */
    _Alignas(64) uint64_t buf[SIZE];
} r;

static int push(uint64_t v)
{
    size_t t = atomic_load_explicit(&r.tail, memory_order_relaxed);   /* 내 것 */
    size_t h = atomic_load_explicit(&r.head, memory_order_acquire);   /* 남의 것 */
    if (t - h == SIZE) return 0;                                      /* full */
    r.buf[t & (SIZE - 1)] = v;                                        /* 1) 데이터 */
    atomic_store_explicit(&r.tail, t + 1, memory_order_release);      /* 2) 공개 */
    return 1;
}

static int pop(uint64_t *v)
{
    size_t h = atomic_load_explicit(&r.head, memory_order_relaxed);
    size_t t = atomic_load_explicit(&r.tail, memory_order_acquire);   /* 공개 확인 */
    if (h == t) return 0;                                             /* empty */
    *v = r.buf[h & (SIZE - 1)];                                       /* 데이터 읽기 */
    atomic_store_explicit(&r.head, h + 1, memory_order_release);      /* 슬롯 반납 */
    return 1;
}

static void *producer(void *arg)
{
    (void)arg;
    for (uint64_t i = 0; i < ITEMS; i++)
        while (!push(i)) ;                 /* full이면 재시도 */
    return NULL;
}

int main(void)
{
    pthread_t p; struct timespec a, b; uint64_t v, expect = 0, errors = 0;
    clock_gettime(CLOCK_MONOTONIC, &a);
    if (pthread_create(&p, NULL, producer, NULL)) { perror("create"); exit(1); }
    while (expect < ITEMS) {
        if (!pop(&v)) continue;
        if (v != expect++) errors++;
    }
    pthread_join(p, NULL);
    clock_gettime(CLOCK_MONOTONIC, &b);
    double s = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
    printf("%lu items, %lu out-of-order, %.2f s, %.1f M items/s\n",
           ITEMS, errors, s, ITEMS / s / 1e6);
    return 0;
}
```
```text
20000000 items, 0 out-of-order, 0.64 s, 31.0 M items/s
```
출력에서 볼 것: 2천만 개가 순서 오류 0으로 전달됐고 초당 약 3천만 개다. 같은 일을 ex4처럼 mutex+condvar 큐로 하면 아이템마다 lock/unlock과 signal이 붙는다. 센서 드라이버 스레드 → 처리 스레드 같은 1:1 파이프라인은 이 구조가 표준이다.

SPSC 함정:

- 이름 그대로 **생산자 1, 소비자 1**일 때만 맞다. 생산자가 둘이 되면 `tail` 갱신이 race가 된다(MPSC/MPMC는 CAS가 필요하고 훨씬 어렵다).
- 인덱스를 `size_t`로 계속 증가시키고 `& (SIZE-1)`로 접는다. 그래서 `t - h`가 자연스럽게 개수가 되고, "full과 empty 구분용 빈 칸 하나"가 필요 없다(unsigned wraparound가 정의된 동작이라 가능).
- 소비자가 비어 있을 때 `continue`로 도는 건 busy-wait다. 저지연이 목표면 전용 코어에서 그대로 두고, 아니면 비었을 때 eventfd(A4)나 futex로 잠드는 장치를 추가한다.

---

## 8. futex — pthread mutex의 정체

### 8.1 직관: 경합이 없으면 커널을 부르지 않는다

syscall은 비싸다(11절에서 재면 약 130 ns). 매번 lock/unlock마다 커널에 들어간다면 mutex는 쓸 수 없을 만큼 느릴 것이다. **futex**(fast userspace mutex)는 이렇게 나눈다.

- **fast path (유저)**: lock word(유저 메모리의 정수 하나)를 atomic CAS로 0 → 1. 성공하면 끝. syscall 0회.
- **slow path (커널)**: CAS가 실패(누가 쥐고 있음)하면 그때만 `futex(addr, FUTEX_WAIT, val)` syscall로 "이 주소의 값이 아직 val이면 나를 재워 줘"라고 커널에 부탁한다. unlock할 때 대기자가 있었으면 `futex(addr, FUTEX_WAKE, 1)`로 한 명을 깨운다.

커널은 lock word의 의미를 모른다. 커널이 아는 것은 "이 **주소**에서 기다리는 스레드 큐"뿐이다. RTOS에서 커널 객체(mutex control block)를 만들어 핸들로 쓰던 것과 정반대로, 상태는 유저 메모리에 있고 커널은 대기실만 빌려준다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="170" y="22" font-size="14" text-anchor="middle">lock</text>
  <text x="510" y="22" font-size="14" text-anchor="middle">unlock</text>
  <rect x="30" y="35" width="280" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="58" font-size="13" text-anchor="middle">CAS(lockword, 0 → 1)</text>
  <line x1="170" y1="71" x2="170" y2="96" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="180" y="88" font-size="12">실패 (누가 쥐고 있음)</text>
  <rect x="30" y="98" width="280" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="121" font-size="13" text-anchor="middle">lockword = 2 (대기자 있음 표시)</text>
  <line x1="170" y1="134" x2="170" y2="268" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="180" y="200" font-size="12">futex(WAIT, addr, 2)</text>
  <rect x="370" y="35" width="280" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="58" font-size="13" text-anchor="middle">old = fetch_sub(lockword, 1)</text>
  <line x1="510" y1="71" x2="510" y2="96" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="370" y="98" width="280" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="121" font-size="13" text-anchor="middle">old == 1 → 끝 (syscall 0회)</text>
  <line x1="510" y1="134" x2="510" y2="159" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="370" y="161" width="280" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="184" font-size="13" text-anchor="middle">old == 2 → lockword = 0</text>
  <line x1="510" y1="197" x2="510" y2="268" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="520" y="235" font-size="12">futex(WAKE, addr, 1)</text>
  <text x="40" y="165" font-size="12" text-anchor="start">CAS 성공이면</text>
  <text x="40" y="181" font-size="12" text-anchor="start">바로 진입 (fast path)</text>
  <line x1="10" y1="250" x2="670" y2="250" stroke="currentColor" stroke-dasharray="6 4"/>
  <text x="20" y="244" font-size="12">user</text>
  <text x="20" y="264" font-size="12">kernel</text>
  <rect x="30" y="270" width="280" height="58" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="170" y="292" font-size="13" text-anchor="middle">값이 아직 2인가? 다르면 EAGAIN</text>
  <text x="170" y="312" font-size="13" text-anchor="middle">같으면 대기 큐에 넣고 잠든다</text>
  <rect x="370" y="270" width="280" height="58" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="292" font-size="13" text-anchor="middle">같은 주소의 대기 큐에서</text>
  <text x="510" y="312" font-size="13" text-anchor="middle">한 명을 깨운다</text>
  <line x1="368" y1="310" x2="312" y2="310" stroke="#d0564a" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
  <rect x="30" y="348" width="620" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="371" font-size="13" text-anchor="middle">깨어난 스레드는 다시 lockword를 2로 바꾸며 재시도 (ex12의 my_lock 루프)</text>
</svg>
```
그림 4 — futex 기반 mutex의 fast/slow path. 점선 위는 유저 공간에서 atomic 명령만으로 끝나고, 경합이 있을 때만 점선 아래(커널)로 내려간다. "값이 아직 2인가" 재확인이 lost wakeup을 막는다.

### 8.2 예제 11 — strace로 fast path 확인

같은 lock/unlock 루프를 스레드 1개와 4개로 돌리면서 futex syscall 수를 센다.

```c
// ex11_futex.c — pthread_mutex는 언제 syscall을 부르나? (strace로 관찰)
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define ITERS 1000000
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static long counter;

static void *worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < ITERS; i++) {
        pthread_mutex_lock(&m);
        counter++;
        pthread_mutex_unlock(&m);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 1;   /* 스레드 수 */
    pthread_t t[8];
    if (n < 1 || n > 8) { fprintf(stderr, "1..8\n"); exit(1); }
    for (int i = 0; i < n; i++)
        if (pthread_create(&t[i], NULL, worker, NULL)) { perror("create"); exit(1); }
    for (int i = 0; i < n; i++) pthread_join(t[i], NULL);
    printf("threads=%d lock/unlock pairs=%d counter=%ld\n", n, n * ITERS, counter);
    return 0;
}
```
```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && gcc -std=gnu11 -Wall -Wextra -O2 -pthread /Users/donh/.elinux_scratch/A8/ex11_futex.c -o ex11 && for n in 1 4; do echo "=== $n thread(s)"; strace -f -c -e trace=futex ./ex11 $n 2>&1; done'
```
```text
=== 1 thread(s)
strace: Process 201591 attached
threads=1 lock/unlock pairs=1000000 counter=1000000
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
  0.00    0.000000           0         1           futex
------ ----------- ----------- --------- --------- ----------------
100.00    0.000000           0         1           total
=== 4 thread(s)
strace: Process 201595 attached
strace: Process 201596 attached
strace: Process 201597 attached
strace: Process 201598 attached
threads=4 lock/unlock pairs=4000000 counter=4000000
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
100.00    0.059385          19      3058      1185 futex
------ ----------- ----------- --------- --------- ----------------
100.00    0.059385          19      3058      1185 total
```
출력에서 볼 것: 스레드 1개일 때 lock/unlock **100만 쌍에 futex는 단 1회**다 — 그 1회는 mutex가 아니라 `pthread_join`이 스레드 종료를 기다리는 데 쓴 것이다. 즉 경합 없는 mutex는 syscall 0회다. 4개 스레드가 경합하면 3058회가 생겼다. 400만 쌍에 비하면 여전히 0.08%뿐이지만, 이 일부가 비용 대부분을 차지한다.

개별 호출 몇 줄을 보면 이렇다(`-o`로 파일에 받고 완결된 줄만 골라냄, 나머지 생략).

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && strace -f -o tr.txt -e trace=futex ./ex11 4 >/dev/null; grep -v unfinished tr.txt | grep -v resumed | head -6'
```
```text
201634 futex(0xaf582b590018, FUTEX_WAKE_PRIVATE, 1) = 0
201634 futex(0xaf582b590018, FUTEX_WAKE_PRIVATE, 1) = 0
201634 futex(0xaf582b590018, FUTEX_WAIT_PRIVATE, 2, NULL) = -1 EAGAIN (Resource temporarily unavailable)
201634 futex(0xaf582b590018, FUTEX_WAIT_PRIVATE, 2, NULL) = -1 EAGAIN (Resource temporarily unavailable)
201634 futex(0xaf582b590018, FUTEX_WAKE_PRIVATE, 1) = 0
201633 futex(0xaf582b590018, FUTEX_WAIT_PRIVATE, 2, NULL) = -1 EAGAIN (Resource temporarily unavailable)
```
출력에서 볼 것: 모든 호출이 **같은 주소**(`0x...018`, 전역 mutex `m`의 lock word)다. `FUTEX_WAIT_PRIVATE, 2`는 "값이 2(잠김+대기자)면 재워 줘"다. `EAGAIN`은 커널에 들어간 사이 이미 값이 바뀌어서(주인이 풀었음) 잠들지 않고 바로 돌아왔다는 뜻이고 정상이다. `_PRIVATE`는 "이 futex는 내 프로세스 안에서만 쓴다"는 힌트로, 커널이 공유 메모리 매핑을 찾는 비싼 키 계산을 건너뛴다(12절).

### 8.3 예제 12 — futex로 mutex를 직접 만들기

glibc 내부(`nptl/lowlevellock.c`)가 하는 일을 50줄로 재현한다. Ulrich Drepper의 논문 "Futexes Are Tricky"에 나오는 3-상태 mutex다.

```c
// ex12_myfutex.c — futex로 직접 만든 mutex (Drepper "Futexes Are Tricky"의 3-상태 버전)
#include <linux/futex.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

/* 0 = 풀림, 1 = 잠김(대기자 없음), 2 = 잠김(대기자 있을 수 있음) */
static atomic_int lockword;
static atomic_long n_wait, n_wake;
static long counter;

static void futex(atomic_int *a, int op, int v) { syscall(SYS_futex, a, op, v, NULL, NULL, 0); }

static void my_lock(atomic_int *l)
{
    int c = 0;
    if (atomic_compare_exchange_strong(l, &c, 1))
        return;                                   /* fast path: syscall 없음 */
    if (c != 2) c = atomic_exchange(l, 2);        /* "대기자 있음" 표시 */
    while (c != 0) {
        atomic_fetch_add(&n_wait, 1);
        futex(l, FUTEX_WAIT_PRIVATE, 2);          /* 값이 아직 2면 잠든다 */
        c = atomic_exchange(l, 2);
    }
}

static void my_unlock(atomic_int *l)
{
    if (atomic_fetch_sub(l, 1) != 1) {            /* 2였다 = 대기자 있을 수 있음 */
        atomic_store(l, 0);
        atomic_fetch_add(&n_wake, 1);
        futex(l, FUTEX_WAKE_PRIVATE, 1);          /* 한 명 깨운다 */
    }
}

static void *worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < 1000000; i++) {
        my_lock(&lockword); counter++; my_unlock(&lockword);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 1;
    pthread_t t[8];
    if (n < 1 || n > 8) { fprintf(stderr, "1..8\n"); exit(1); }
    for (int i = 0; i < n; i++)
        if (pthread_create(&t[i], NULL, worker, NULL)) { perror("create"); exit(1); }
    for (int i = 0; i < n; i++) pthread_join(t[i], NULL);
    printf("threads=%d counter=%ld futex_wait=%ld futex_wake=%ld\n",
           n, counter, atomic_load(&n_wait), atomic_load(&n_wake));
    return 0;
}
```
```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && gcc -std=gnu11 -Wall -Wextra -O2 -pthread /Users/donh/.elinux_scratch/A8/ex12_myfutex.c -o ex12 && ./ex12 1 && ./ex12 2 && ./ex12 4 && strace -f -c -e trace=futex ./ex12 1 2>&1 | tail -3'
```
```text
threads=1 counter=1000000 futex_wait=0 futex_wake=0
threads=2 counter=2000000 futex_wait=380306 futex_wake=475926
threads=4 counter=4000000 futex_wait=785558 futex_wake=934934
  0.00    0.000000           0         1           futex
------ ----------- ----------- --------- --------- ----------------
100.00    0.000000           0         1           total
```
출력에서 볼 것: 카운터는 모두 정확하다 — 50줄짜리 futex mutex가 실제로 동작한다. 스레드 1개면 futex 0회(strace의 1회는 역시 `pthread_join`). 경합하면 syscall이 수십만 번 생긴다. 같은 4스레드 조건에서 glibc mutex(ex11)는 3058회였으니, 우리 버전은 경합 시 수백 배 많이 커널에 들어간다. 정확한 원인을 여기서 단정하진 않겠지만, "올바른 futex 락"과 "잘 튜닝된 futex 락"은 다르다는 점이 요점이다 — 프로덕션에선 직접 만들지 말고 pthread를 쓴다.

왜 상태가 3개인가? 2-상태(0/1)로 만들면 unlock이 "대기자가 있는지" 몰라서 **매번** `FUTEX_WAKE`를 불러야 한다. 상태 2가 "누군가 잠들었을 수 있다"를 기록해서, 경합이 없던 unlock(1 → 0)은 syscall을 건너뛴다.

### 8.4 다른 primitive도 전부 futex다

- `pthread_cond_wait` / `signal`: condvar 내부 카운터(futex word) 주소에 `FUTEX_WAIT` / `FUTEX_WAKE`.
- `sem_wait` / `sem_post`: sem_t 안의 값 주소에 futex. named semaphore는 `/dev/shm` 파일을 `mmap`한 주소라서 non-private(공유) futex가 된다.
- `pthread_join`: 종료할 스레드의 TID 필드 주소. 커널이 스레드 종료 시 그 값을 0으로 쓰고 WAKE한다(`CLONE_CHILD_CLEARTID`).
- `pthread_rwlock`, `pthread_barrier`, `pthread_once`도 마찬가지다. 유저스페이스 동기화의 커널 쪽 대기실은 사실상 futex 하나다.

---

## 9. Priority inversion — 그리고 PTHREAD_PRIO_INHERIT

### 9.1 Mars Pathfinder 이야기

1997년 화성에 착륙한 Mars Pathfinder는 며칠 뒤부터 반복적으로 **시스템 전체 리셋**을 일으켰다. OS는 VxWorks였다. 원인은 이랬다.

- 낮은 우선순위의 기상 데이터 태스크가 공유 "정보 버스"를 보호하는 mutex를 잡았다.
- 높은 우선순위의 버스 관리 태스크가 그 mutex를 기다리며 막혔다.
- 그 사이 **중간 우선순위**의 통신 태스크가 오래 실행되면서 낮은 우선순위 태스크를 계속 밀어냈다. mutex 주인이 못 돌았으니 높은 우선순위 태스크도 계속 막혔다.
- 다음 주기가 시작될 때 버스 관리 태스크가 끝나지 않은 것을 감시 로직이 발견하고 시스템을 리셋했다.

JPL은 지상의 복제 시스템에서 이를 재현했고, 해당 mutex의 **priority inheritance 옵션을 켜는** 수정을 화성으로 업로드해 해결했다. 이것이 **unbounded priority inversion**이다. "높은 우선순위가 낮은 우선순위를 기다리는 것" 자체(bounded)는 락을 쓰는 한 피할 수 없지만, 그 대기 시간이 **락과 무관한 중간 우선순위 태스크의 실행 시간만큼 무한정 늘어나는 것**이 문제다.

### 9.2 Priority inheritance(PI)

해법은 Don이 RTOS에서 이미 아는 그것이다. 높은 우선순위 H가 mutex를 기다리기 시작하면, 그 mutex의 주인 L의 우선순위를 **일시적으로 H만큼 올린다**. 그러면 중간 M이 L을 밀어낼 수 없고, L은 빨리 끝내고 unlock하며 원래 우선순위로 돌아간다. 다른 해법인 **priority ceiling**(`PTHREAD_PRIO_PROTECT`)은 mutex에 "이 락을 쓰는 가장 높은 우선순위"를 미리 지정해 두고 lock하는 순간 그만큼 올린다.

Linux에서는 mutex 속성으로 켠다.

```c
pthread_mutexattr_setprotocol(&ma, PTHREAD_PRIO_INHERIT);   /* 기본값은 PTHREAD_PRIO_NONE */
```

### 9.3 예제 13 — 직접 재현: 219 ms vs 19 ms

CPU 0 하나에 SCHED_FIFO 스레드 셋을 올린다. L(prio 10)이 락을 잡고 20 ms 일하는 사이 H(prio 30)가 락을 요청하고, M(prio 20)이 락과 무관하게 200 ms 동안 CPU를 독식한다. SCHED_FIFO 설정에는 root(정확히는 `CAP_SYS_NICE`)가 필요해서 `sudo`로 실행한다.

```c
// ex13_pi.c — priority inversion 재현 (CPU 0 하나, SCHED_FIFO), PI 유무 비교. sudo 필요
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t m;
static volatile int l_has_lock;
static double now_ms(clockid_t c)
{ struct timespec t; clock_gettime(c, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static void burn_cpu(double ms)                    /* 내 CPU 시간으로 ms만큼 일한다 */
{ double s = now_ms(CLOCK_THREAD_CPUTIME_ID); while (now_ms(CLOCK_THREAD_CPUTIME_ID) - s < ms) ; }

static void *low(void *a)  { (void)a; pthread_mutex_lock(&m); l_has_lock = 1;
                             burn_cpu(20); pthread_mutex_unlock(&m); return NULL; }
static void *mid(void *a)  { (void)a; burn_cpu(200); return NULL; }   /* 락과 무관한 CPU 독식 */
static void *high(void *a) { double t0 = now_ms(CLOCK_MONOTONIC);
                             pthread_mutex_lock(&m);
                             *(double *)a = now_ms(CLOCK_MONOTONIC) - t0;
                             pthread_mutex_unlock(&m); return NULL; }

static pthread_t spawn(void *(*fn)(void *), int prio, void *arg)
{
    pthread_attr_t at; struct sched_param sp = { .sched_priority = prio }; pthread_t t;
    pthread_attr_init(&at);
    pthread_attr_setinheritsched(&at, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&at, SCHED_FIFO);
    pthread_attr_setschedparam(&at, &sp);
    int r = pthread_create(&t, &at, fn, arg);
    if (r) { fprintf(stderr, "pthread_create: %s (sudo?)\n", strerror(r)); exit(1); }
    return t;
}

int main(int argc, char **argv)
{
    int pi = argc > 1 && strcmp(argv[1], "pi") == 0;
    cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(0, &cs);
    struct sched_param sp = { .sched_priority = 40 };       /* main이 제일 높다 */
    if (sched_setaffinity(0, sizeof cs, &cs) || sched_setscheduler(0, SCHED_FIFO, &sp)) {
        perror("sched (run with sudo)"); exit(1);
    }
    pthread_mutexattr_t ma; pthread_mutexattr_init(&ma);
    if (pi) pthread_mutexattr_setprotocol(&ma, PTHREAD_PRIO_INHERIT);
    pthread_mutex_init(&m, &ma);

    double h_wait = 0;
    pthread_t L = spawn(low, 10, NULL);
    while (!l_has_lock) usleep(1000);                        /* L이 락을 잡을 때까지 */
    pthread_t H = spawn(high, 30, &h_wait);                  /* H: 락 대기로 잠듦 */
    pthread_t M = spawn(mid, 20, NULL);                      /* M: L보다 높아서 L을 밀어냄 */
    pthread_join(H, NULL); pthread_join(M, NULL); pthread_join(L, NULL);
    printf("%-12s high-priority thread waited %6.1f ms for the lock\n",
           pi ? "PRIO_INHERIT" : "PRIO_NONE", h_wait);
    return 0;
}
```
```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && gcc -std=gnu11 -Wall -Wextra -O2 -pthread /Users/donh/.elinux_scratch/A8/ex13_pi.c -o ex13 && ./ex13; for i in 1 2 3; do sudo ./ex13; sudo ./ex13 pi; done'
```
```text
sched (run with sudo): Operation not permitted
PRIO_NONE    high-priority thread waited  219.1 ms for the lock
PRIO_INHERIT high-priority thread waited   19.0 ms for the lock
PRIO_NONE    high-priority thread waited  219.0 ms for the lock
PRIO_INHERIT high-priority thread waited   19.0 ms for the lock
PRIO_NONE    high-priority thread waited  219.1 ms for the lock
PRIO_INHERIT high-priority thread waited   19.0 ms for the lock
```
출력에서 볼 것: 첫 줄은 sudo 없이 실행해 SCHED_FIFO 설정이 거부된 것이다. PI 없이는 H가 **219 ms**(M의 200 ms + L의 남은 일 약 19 ms) 기다렸고, PI를 켜면 **19 ms**(L의 남은 일만) 기다렸다. 세 번 모두 0.1 ms 안에서 재현된다. M은 락을 건드리지도 않았는데 H를 10배 넘게 늦췄다 — 이것이 Pathfinder의 버그다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="14">PI 없음 (PRIO_NONE): H가 219 ms 대기</text>
  <text x="20" y="62" font-size="13">H (prio 30)</text>
  <text x="20" y="97" font-size="13">M (prio 20)</text>
  <text x="20" y="132" font-size="13">L (prio 10)</text>
  <rect x="110" y="45" width="438" height="24" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
  <text x="329" y="62" font-size="12" text-anchor="middle">락 대기로 잠듦 (blocked)</text>
  <rect x="548" y="45" width="20" height="24" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <rect x="110" y="80" width="400" height="24" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
  <text x="310" y="97" font-size="12" text-anchor="middle">락과 무관한 일 200 ms — L을 밀어냄</text>
  <rect x="110" y="115" width="400" height="24" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-dasharray="4 3"/>
  <text x="310" y="132" font-size="12" text-anchor="middle">락을 쥔 채 실행 못 함 (preempted)</text>
  <rect x="510" y="115" width="38" height="24" fill="#4a7bd0" fill-opacity="0.45" stroke="#4a7bd0"/>
  <line x1="110" y1="150" x2="610" y2="150" stroke="currentColor"/>
  <text x="110" y="166" font-size="12" text-anchor="middle">0</text>
  <text x="210" y="166" font-size="12" text-anchor="middle">50</text>
  <text x="310" y="166" font-size="12" text-anchor="middle">100</text>
  <text x="410" y="166" font-size="12" text-anchor="middle">150</text>
  <text x="510" y="166" font-size="12" text-anchor="middle">200</text>
  <text x="625" y="166" font-size="12" text-anchor="middle">ms</text>
  <text x="20" y="212" font-size="14">PI 있음 (PRIO_INHERIT): H가 19 ms 대기</text>
  <text x="20" y="252" font-size="13">H (prio 30)</text>
  <text x="20" y="287" font-size="13">M (prio 20)</text>
  <text x="20" y="322" font-size="13">L (prio 10)</text>
  <rect x="110" y="235" width="38" height="24" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
  <rect x="148" y="235" width="20" height="24" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="178" y="252" font-size="12">H 실행 → 끝</text>
  <rect x="168" y="270" width="400" height="24" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
  <text x="368" y="287" font-size="12" text-anchor="middle">M은 H·L이 끝난 뒤에 200 ms 실행</text>
  <rect x="110" y="305" width="38" height="24" fill="#4a7bd0" fill-opacity="0.45" stroke="#4a7bd0"/>
  <text x="158" y="322" font-size="12">L이 prio 30을 상속받아 M보다 먼저 실행 → unlock</text>
  <line x1="110" y1="340" x2="610" y2="340" stroke="currentColor"/>
  <text x="110" y="356" font-size="12" text-anchor="middle">0</text>
  <text x="210" y="356" font-size="12" text-anchor="middle">50</text>
  <text x="310" y="356" font-size="12" text-anchor="middle">100</text>
  <text x="410" y="356" font-size="12" text-anchor="middle">150</text>
  <text x="510" y="356" font-size="12" text-anchor="middle">200</text>
  <text x="625" y="356" font-size="12" text-anchor="middle">ms</text>
  <text x="340" y="388" font-size="12" text-anchor="middle">t=0: H가 lock 요청. 빨강 = H 실행, 파랑 = L이 락 쥐고 실행, 주황 = M 실행 (ex13 실측 기반)</text>
</svg>
```
그림 5 — priority inversion 타임라인 (1 ms = 2 px). 위: PI가 없으면 H의 대기 시간이 M의 실행 시간만큼 늘어난다. 아래: PI가 L을 H 우선순위로 올려서 대기가 L의 남은 임계 구역(19 ms)으로 제한된다.

PI mutex는 내부적으로 일반 futex와 다른 연산을 쓴다.

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && sudo strace -f -e trace=futex ./ex13 pi 2>&1 | grep -E "LOCK_PI|UNLOCK_PI|waited" | head -5'
```
```text
futex(0xfffffdb52e84, FUTEX_UNLOCK_PI_PRIVATE) = -1 EPERM (Operation not permitted)
[pid 201918] futex(0xb7df613d0018, FUTEX_LOCK_PI_PRIVATE, NULLstrace: Process 201919 attached
[pid 201917] futex(0xb7df613d0018, FUTEX_UNLOCK_PI_PRIVATE <unfinished ...>
[pid 201918] futex(0xb7df613d0018, FUTEX_UNLOCK_PI_PRIVATE) = 0
PRIO_INHERIT high-priority thread waited  100.2 ms for the lock
```
출력에서 볼 것: `FUTEX_LOCK_PI` / `FUTEX_UNLOCK_PI`가 쓰였다. 첫 줄의 `EPERM`은 mutex와 무관한 스택 주소다. `pthread_mutex_init`이 PI 속성을 받으면 커널이 PI futex를 지원하는지 스택 변수로 한 번 떠보는 호출로 보인다(glibc `nptl/pthread_mutex_init.c`). PI futex의 lock word에는 "잠김" 대신 **주인의 TID**가 들어간다 — 커널이 누구의 우선순위를 올릴지 알아야 하기 때문이다. 그래서 경합 시 커널이 그 TID의 태스크를 찾아 rt_mutex로 우선순위를 상속시킨다(12절). (strace는 ptrace로 매 syscall마다 멈추기 때문에 이 실행의 대기 시간 100 ms는 의미 없다. 시간 측정은 strace 없이 한다.)

### 9.4 RTOS · PREEMPT_RT와의 연결

- RTOS: FreeRTOS의 mutex(`xSemaphoreCreateMutex`)는 기본으로 priority inheritance가 들어 있다. Linux pthread는 **기본이 꺼져 있다**는 것이 가장 큰 차이다. RT 스레드가 공유하는 mutex는 반드시 PI로 만든다.
- semaphore에는 주인이 없어서 PI가 불가능하다. RT 경로에서 semaphore를 상호 배제용으로 쓰면 Pathfinder 버그를 다시 만든다.
- 커널 쪽: PI futex의 대기는 커널 내부의 `rt_mutex`로 구현된다. **PREEMPT_RT**(6.12부터 mainline) 커널에서는 드라이버가 쓰는 `spinlock_t`와 `mutex`까지 rt_mutex 기반의 잠들 수 있는 PI 락으로 바뀐다 — 커널 안에서도 priority inversion을 막기 위해서다. 자세한 건 N 모듈(실시간)에서 다룬다.
- PI는 **체인**도 따라간다(H가 기다리는 L이 또 다른 락을 기다리면 그 주인까지 올린다). 그래도 락을 짧게 쥐는 설계가 먼저다. PI는 안전망이지 성능 도구가 아니다.

---

## 10. Deadlock과 도구 — helgrind, ThreadSanitizer

### 10.1 ABBA deadlock

**deadlock**은 둘 이상의 스레드가 서로가 쥔 락을 기다리며 영원히 멈춘 상태다. 가장 흔한 형태가 **ABBA**다. 스레드 1은 A → B 순서로, 스레드 2는 B → A 순서로 잠근다. 둘이 동시에 첫 락을 잡으면 끝이다.

```c
// ex14_abba.c — ABBA 락 순서: 같이 돌면 deadlock, 따로 돌면 "운 좋게" 통과
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pthread_mutex_t A = PTHREAD_MUTEX_INITIALIZER;   /* 예: I2C 버스 락 */
static pthread_mutex_t B = PTHREAD_MUTEX_INITIALIZER;   /* 예: 센서 상태 락 */
static int sleep_us;

static void *t1(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&A); usleep(sleep_us);
    pthread_mutex_lock(&B);
    puts("t1 got A then B");
    pthread_mutex_unlock(&B); pthread_mutex_unlock(&A);
    return NULL;
}

static void *t2(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&B); usleep(sleep_us);
    pthread_mutex_lock(&A);                                  /* 순서가 반대! */
    puts("t2 got B then A");
    pthread_mutex_unlock(&A); pthread_mutex_unlock(&B);
    return NULL;
}

int main(int argc, char **argv)
{
    int together = argc > 1 && strcmp(argv[1], "together") == 0;
    pthread_t x, y;
    setvbuf(stdout, NULL, _IONBF, 0);
    sleep_us = together ? 100000 : 0;
    if (pthread_create(&x, NULL, t1, NULL)) { perror("create"); exit(1); }
    if (!together) pthread_join(x, NULL);                  /* 순차 실행 */
    if (pthread_create(&y, NULL, t2, NULL)) { perror("create"); exit(1); }
    if (together) pthread_join(x, NULL);
    pthread_join(y, NULL);
    puts("done");
    return 0;
}
```
```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && gcc -std=gnu11 -Wall -Wextra -O2 -pthread /Users/donh/.elinux_scratch/A8/ex14_abba.c -o ex14 && ./ex14; echo "exit=$?"; timeout 3 ./ex14 together; echo "exit=$? (124 = timeout: deadlock)"'
```
```text
t1 got A then B
t2 got B then A
done
exit=0
exit=124 (124 = timeout: deadlock)
```
출력에서 볼 것: 순차로 돌면 멀쩡하다. 동시에 돌면 아무것도 출력하지 못하고 3초 뒤 `timeout`에 죽는다(124). 코드는 같은데 타이밍만 다르다.

### 10.2 멈춘 프로세스 들여다보기 — `/proc`과 gdb

현장에서 "장비가 멈췄다"는 연락을 받으면 이렇게 본다.

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && (./ex14 together > /dev/null &) ; sleep 1; P=$(pgrep -n ex14); for t in /proc/$P/task/*; do echo "$(basename $t) state=$(grep State $t/status | cut -f2) wchan=$(cat $t/wchan)"; done; T=$(ls /proc/$P/task | tail -1); sudo cat /proc/$P/task/$T/stack; echo ---; sudo gdb -p $P -batch -ex "thread apply all bt" 2>/dev/null | grep -E "^Thread|^#[0-9]" | head -20; kill -9 $P'
```
```text
201983 state=S (sleeping) wchan=futex_do_wait
201985 state=S (sleeping) wchan=futex_do_wait
201986 state=S (sleeping) wchan=futex_do_wait
[<0>] futex_do_wait+0x50/0xd0
[<0>] __futex_wait+0xa4/0x140
[<0>] futex_wait+0x90/0x140
[<0>] do_futex+0x1b0/0x270
[<0>] __arm64_sys_futex+0x108/0x210
[<0>] invoke_syscall.constprop.0+0x60/0xf0
[<0>] el0_svc_common.constprop.0+0x114/0x140
[<0>] do_el0_svc+0x28/0x58
[<0>] el0_svc+0x40/0x1e0
[<0>] el0t_64_sync_handler+0xc0/0x110
[<0>] el0t_64_sync+0x1b8/0x1c0
---
Thread 3 (Thread 0xf874d192f160 (LWP 201985) "ex14"):
#0  futex_wait (futex_word=0xca0d948f0018 <B>, expected=2, private=0) at ../sysdeps/nptl/futex-internal.h:126
#1  __GI___lll_lock_wait (futex=futex@entry=0xca0d948f0018 <B>, private=private@entry=0) at ./nptl/lowlevellock.c:49
#2  0x0000f874d19bf7d8 [PAC] in lll_mutex_lock_optimized (mutex=0xca0d948f0018 <B>) at ./nptl/pthread_mutex_lock.c:48
#3  ___pthread_mutex_lock (mutex=0xca0d948f0018 <B>) at ./nptl/pthread_mutex_lock.c:87
#4  0x0000ca0d948d0d8c [PAC] in t1 ()
#5  0x0000f874d19bc248 in start_thread (arg=0xf874d192f160) at ./nptl/pthread_create.c:454
#6  0x0000f874d1a26b9c [PAC] in thread_start () at ../sysdeps/unix/sysv/linux/aarch64/clone3.S:75
Thread 2 (Thread 0xf874d111f160 (LWP 201986) "ex14"):
#0  futex_wait (futex_word=0xca0d948f0050 <A>, expected=2, private=0) at ../sysdeps/nptl/futex-internal.h:126
#1  __GI___lll_lock_wait (futex=futex@entry=0xca0d948f0050 <A>, private=private@entry=0) at ./nptl/lowlevellock.c:49
#2  0x0000f874d19bf7d8 [PAC] in lll_mutex_lock_optimized (mutex=0xca0d948f0050 <A>) at ./nptl/pthread_mutex_lock.c:48
#3  ___pthread_mutex_lock (mutex=0xca0d948f0050 <A>) at ./nptl/pthread_mutex_lock.c:87
#4  0x0000ca0d948d0d2c [PAC] in t2 ()
#5  0x0000f874d19bc248 in start_thread (arg=0xf874d111f160) at ./nptl/pthread_create.c:454
#6  0x0000f874d1a26b9c [PAC] in thread_start () at ../sysdeps/unix/sysv/linux/aarch64/clone3.S:75
Thread 1 (Thread 0xf874d1b3af60 (LWP 201983) "ex14"):
#0  __syscall_cancel_arch () at ../sysdeps/unix/sysv/linux/aarch64/syscall_cancel.S:50
#1  0x0000f874d19b8440 in __internal_syscall_cancel (a1=<optimized out>, a2=<optimized out>, a3=<optimized out>, a4=<optimized out>, a5=a5@entry=0, a6=a6@entry=4294967295, nr=nr@entry=98) at ./nptl/cancellation.c:49
#2  0x0000f874d19b88b4 [PAC] in __futex_abstimed_wait_common64 (private=<optimized out>, futex_word=<optimized out>, expected=<optimized out>, op=<optimized out>, abstime=<optimized out>, cancel=true) at ./nptl/futex-internal.c:57
```
출력에서 볼 것: 세 스레드 모두 `S (sleeping)`이고 커널에서 `futex_do_wait`에 잠들어 있다(CPU를 안 쓰는 deadlock이라 `top`에선 조용해 보인다). 커널 스택은 `el0t_64_sync`(EL0에서 SVC 예외 진입) → `__arm64_sys_futex` → `futex_wait` → `futex_do_wait` 순서로, A1에서 본 syscall 경로 그대로다. gdb는 결정적인 정보를 준다: t1은 **B**를, t2는 **A**를 기다린다. 서로가 쥔 것을 기다리는 순환이 보인다. 메인 스레드는 `pthread_join`(syscall 98 = futex) 중이다. (주소·TID는 실행마다 다르다. 위 gdb 출력은 `head -20`으로 잘랐다.)

### 10.3 ThreadSanitizer — 일어나지 않은 deadlock까지 잡는다

멈춘 뒤 원인을 찾는 것보다, 멈추기 전에 잡는 게 낫다. **ThreadSanitizer(TSan)**는 컴파일러가 모든 메모리 접근과 락 연산에 감시 코드를 심어서 (1) data race와 (2) **락 순서 위반**을 실행 중에 찾는다. 핵심은, deadlock이 **실제로 일어나지 않은** 순차 실행에서도 "A→B를 본 적 있는데 이번엔 B→A"라는 순서 그래프의 사이클을 보고 경고한다는 점이다.

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && gcc -std=gnu11 -Wall -Wextra -O1 -g -fsanitize=thread -pthread /Users/donh/.elinux_scratch/A8/ex14_abba.c -o ex14_tsan && ./ex14_tsan 2>&1 | head -40'
```
```text
t1 got A then B
==================
WARNING: ThreadSanitizer: lock-order-inversion (potential deadlock) (pid=202049)
  Cycle in lock order graph: M0 (0xaaaaaaac0050) => M1 (0xaaaaaaac0018) => M0

  Mutex M1 acquired here while holding mutex M0 in thread T1:
    #0 pthread_mutex_lock ../../../../src/libsanitizer/tsan/tsan_interceptors_posix.cpp:1424 (libtsan.so.2+0x5dc5c) (BuildId: 92f14145646a32a217effb45501763763addc1bd)
    #1 t1 /Users/donh/.elinux_scratch/A8/ex14_abba.c:16 (ex14_tsan+0xe1c) (BuildId: 7a2759c9c5138680f195614372a022b7ad061bab)

    Hint: use TSAN_OPTIONS=second_deadlock_stack=1 to get more informative warning message

  Mutex M0 acquired here while holding mutex M1 in thread T2:
    #0 pthread_mutex_lock ../../../../src/libsanitizer/tsan/tsan_interceptors_posix.cpp:1424 (libtsan.so.2+0x5dc5c) (BuildId: 92f14145646a32a217effb45501763763addc1bd)
    #1 t2 /Users/donh/.elinux_scratch/A8/ex14_abba.c:26 (ex14_tsan+0xda8) (BuildId: 7a2759c9c5138680f195614372a022b7ad061bab)

  Thread T1 (tid=202052, finished) created by main thread at:
    #0 pthread_create ../../../../src/libsanitizer/tsan/tsan_interceptors_posix.cpp:1078 (libtsan.so.2+0x5e83c) (BuildId: 92f14145646a32a217effb45501763763addc1bd)
    #1 main /Users/donh/.elinux_scratch/A8/ex14_abba.c:38 (ex14_tsan+0xfb4) (BuildId: 7a2759c9c5138680f195614372a022b7ad061bab)

  Thread T2 (tid=202053, running) created by main thread at:
    #0 pthread_create ../../../../src/libsanitizer/tsan/tsan_interceptors_posix.cpp:1078 (libtsan.so.2+0x5e83c) (BuildId: 92f14145646a32a217effb45501763763addc1bd)
    #1 main /Users/donh/.elinux_scratch/A8/ex14_abba.c:40 (ex14_tsan+0xfe4) (BuildId: 7a2759c9c5138680f195614372a022b7ad061bab)

SUMMARY: ThreadSanitizer: lock-order-inversion (potential deadlock) /Users/donh/.elinux_scratch/A8/ex14_abba.c:16 in t1
==================
t2 got B then A
done
ThreadSanitizer: reported 1 warnings
```
출력에서 볼 것: 프로그램은 "done"까지 정상 종료했는데도 TSan은 **lock-order-inversion**을 보고했다. 줄 번호(16행: t1이 A를 쥔 채 B, 26행: t2가 B를 쥔 채 A)까지 정확하다.

같은 도구로 1절의 race도 잡힌다.

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && gcc -std=gnu11 -Wall -Wextra -O1 -g -fsanitize=thread -pthread /Users/donh/.elinux_scratch/A8/ex1_race.c -o ex1_tsan && ./ex1_tsan 2>&1 | head -16'
```
```text
==================
WARNING: ThreadSanitizer: data race (pid=202962)
  Read of size 8 at 0xaaaaaaac0018 by thread T2:
    #0 worker /Users/donh/.elinux_scratch/A8/ex1_race.c:15 (ex1_tsan+0xb98) (BuildId: 9dbc2d8792acbb9797e1164aa229dd4be06f53e1)

  Previous write of size 8 at 0xaaaaaaac0018 by thread T1:
    #0 worker /Users/donh/.elinux_scratch/A8/ex1_race.c:15 (ex1_tsan+0xba8) (BuildId: 9dbc2d8792acbb9797e1164aa229dd4be06f53e1)

  Location is global 'counter' of size 8 at 0xaaaaaaac0018 (ex1_tsan+0x20018)

  Thread T2 (tid=202966, running) created by main thread at:
    #0 pthread_create ../../../../src/libsanitizer/tsan/tsan_interceptors_posix.cpp:1078 (libtsan.so.2+0x5e83c) (BuildId: 92f14145646a32a217effb45501763763addc1bd)
    #1 main /Users/donh/.elinux_scratch/A8/ex1_race.c:25 (ex1_tsan+0xc58) (BuildId: 9dbc2d8792acbb9797e1164aa229dd4be06f53e1)

  Thread T1 (tid=202965, running) created by main thread at:
    #0 pthread_create ../../../../src/libsanitizer/tsan/tsan_interceptors_posix.cpp:1078 (libtsan.so.2+0x5e83c) (BuildId: 92f14145646a32a217effb45501763763addc1bd)
```
출력에서 볼 것: 전역 `counter`(15행 `counter++`)에서 T2의 읽기와 T1의 쓰기가 동기화 없이 겹친다고 정확히 지목한다. `volatile`이 붙어 있어도 race로 잡는다 — 1.4절의 결론과 같다. TSan은 실행 속도가 수 배~수십 배 느려지고 메모리를 많이 쓰므로 CI나 개발 빌드에서 돌린다(그리고 x86-64·aarch64 같은 64비트 타깃만 지원한다).

### 10.4 helgrind — 재컴파일 없이

Valgrind의 **helgrind**는 바이너리를 가상 CPU에서 돌리며 같은 종류의 오류를 찾는다. 재컴파일이 필요 없어서 서드파티 바이너리에도 쓸 수 있지만 훨씬 느리다. (VM에 `sudo apt-get install valgrind`로 설치했다.)

```sh
limactl shell elinux -- bash -lc 'cd /tmp/A8 && gcc -std=gnu11 -O0 -g -pthread /Users/donh/.elinux_scratch/A8/ex14_abba.c -o ex14_g && valgrind --tool=helgrind ./ex14_g 2>&1 | grep -v "^==[0-9]*== *$" | sed -n "1,60p"'
```
```text
==202111== Helgrind, a thread error detector
==202111== Copyright (C) 2007-2024, and GNU GPL'd, by OpenWorks LLP et al.
==202111== Using Valgrind-3.26.0 and LibVEX; rerun with -h for copyright info
==202111== Command: ./ex14_g
t1 got A then B
==202111== ---Thread-Announcement------------------------------------------
...
==202111== Thread #3: lock order "0x4020018 before 0x4020048" violated
==202111== Observed (incorrect) order is: acquisition of lock at 0x4020048
==202111==    at 0x48B0AD8: mutex_lock_WRK (hg_intercepts.c:1003)
==202111==    by 0x4000BE3: t2 (ex14_abba.c:25)
...
==202111==  followed by a later acquisition of lock at 0x4020018
==202111==    at 0x48B0AD8: mutex_lock_WRK (hg_intercepts.c:1003)
==202111==    by 0x4000BFF: t2 (ex14_abba.c:26)
...
==202111== Required order was established by acquisition of lock at 0x4020018
==202111==    at 0x48B0AD8: mutex_lock_WRK (hg_intercepts.c:1003)
==202111==    by 0x4000B7F: t1 (ex14_abba.c:15)
...
==202111==  Address 0x4020018 is 0 bytes inside data symbol "A"
...
==202111==  Address 0x4020048 is 0 bytes inside data symbol "B"
t2 got B then A
done
...
==202111== ERROR SUMMARY: 1 errors from 1 contexts (suppressed: 2 from 2)
```
출력에서 볼 것: (스택 프레임 일부를 `...`로 생략했다.) helgrind도 "A before B 순서가 정해졌는데 t2가 B 다음 A를 잡았다"고 보고하고, 주소를 심볼 `A`, `B`로 풀어 준다.

### 10.5 deadlock을 설계로 막는 규칙

- **전역 락 순서**: 모든 락에 순위를 매기고 항상 낮은 순위부터 잡는다(예: "버스 락 → 장치 락 → 상태 락"). 커널은 lockdep(M8)이 이 규칙을 런타임에 검증한다.
- 락을 쥔 채 **콜백·외부 함수 호출 금지** — 그 안에서 무슨 락을 잡을지 모른다.
- 락을 쥔 채 **블로킹 I/O 금지**(I2C 전송, 파일 쓰기, `sleep`). 데이터만 복사해 두고 락 밖에서 I/O한다.
- 여러 락을 한꺼번에 잡아야 하면 `pthread_mutex_trylock`으로 시도하고 실패하면 **전부 풀고** 다시 시도하는 back-off 패턴.
- 가능하면 락을 아예 하나로 합치거나, 메시지 전달(큐) 구조로 공유 자체를 줄인다.

---

## 11. 측정: 동기화는 얼마나 비싼가

### 11.1 예제 15 — mutex vs atomic, 스레드 수별

총 800만 번 증가를 스레드 1/2/4/8개로 나눠 하고, 증가 1회당 경과 시간(ns, 처리량의 역수)을 잰다. 각 설정을 5번 돌려 최솟값을 쓴다.

```c
// ex15_bench.c — 공유 카운터 증가 1회 비용: mutex vs atomic, 스레드 수 1/2/4/8
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TOTAL 8000000L                      /* 스레드 수와 관계없이 총 증가 횟수 고정 */
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static long plain;
static atomic_long at;
static long per_thread;

static void *w_mutex(void *a)
{ (void)a; for (long i = 0; i < per_thread; i++) { pthread_mutex_lock(&m); plain++; pthread_mutex_unlock(&m); } return NULL; }
static void *w_atomic(void *a)
{ (void)a; for (long i = 0; i < per_thread; i++) atomic_fetch_add_explicit(&at, 1, memory_order_relaxed); return NULL; }

static double run(void *(*fn)(void *), int n)
{
    pthread_t t[8];
    struct timespec a, b;
    per_thread = TOTAL / n;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < n; i++)
        if (pthread_create(&t[i], NULL, fn, NULL)) { perror("create"); exit(1); }
    for (int i = 0; i < n; i++) pthread_join(t[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &b);
    return ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / TOTAL;  /* ns/op */
}

int main(void)
{
    int ns[] = { 1, 2, 4, 8 };
    printf("threads  mutex(ns/op)  atomic(ns/op)\n");
    for (int k = 0; k < 4; k++) {
        double bm = 1e9, ba = 1e9;          /* 5회 중 최솟값 */
        for (int r = 0; r < 5; r++) {
            double x = run(w_mutex, ns[k]), y = run(w_atomic, ns[k]);
            if (x < bm) bm = x;
            if (y < ba) ba = y;
        }
        printf("%7d  %12.1f  %13.1f\n", ns[k], bm, ba);
    }
    printf("check: plain=%ld atomic=%ld (5 runs x 4 configs x %ld)\n",
           plain, atomic_load(&at), TOTAL);
    return 0;
}
```
```text
threads  mutex(ns/op)  atomic(ns/op)
      1           7.0            2.0
      2          36.9            4.3
      4          34.0            8.5
      8          59.1            8.7
check: plain=160000000 atomic=160000000 (5 runs x 4 configs x 8000000)
```
비교 기준으로 "아무 일도 안 하는 syscall" 1회 비용도 잰다.

```c
// ex16_syscost.c — 비교 기준: "아무것도 안 하는" syscall 1회 비용
#include <stdio.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    struct timespec a, b;
    const long N = 2000000;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (long i = 0; i < N; i++)
        syscall(SYS_getppid);               /* glibc 캐시를 피하려고 syscall() 직접 */
    clock_gettime(CLOCK_MONOTONIC, &b);
    printf("getppid syscall: %.1f ns/call\n",
           ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / N);
    return 0;
}
```
```text
getppid syscall: 127.2 ns/call
getppid syscall: 130.5 ns/call
```

```svg
<svg viewBox="0 0 640 340" xmlns="http://www.w3.org/2000/svg">
  <text x="70" y="18" font-size="13">ns / 증가 1회 (총 800만 회 ÷ 경과 시간, 5회 중 최솟값)</text>
  <rect x="70" y="30" width="14" height="14" fill="#4a7bd0" fill-opacity="0.85"/>
  <text x="90" y="42" font-size="12">pthread_mutex</text>
  <rect x="220" y="30" width="14" height="14" fill="#e08a3c" fill-opacity="0.85"/>
  <text x="240" y="42" font-size="12">atomic_fetch_add (relaxed)</text>
  <line x1="70" y1="270.0" x2="610" y2="270.0" stroke="currentColor" stroke-opacity="0.5"/>
  <text x="62" y="274.0" font-size="12" text-anchor="end">0</text>
  <line x1="70" y1="237.1" x2="610" y2="237.1" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="241.1" font-size="12" text-anchor="end">10</text>
  <line x1="70" y1="204.3" x2="610" y2="204.3" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="208.3" font-size="12" text-anchor="end">20</text>
  <line x1="70" y1="171.4" x2="610" y2="171.4" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="175.4" font-size="12" text-anchor="end">30</text>
  <line x1="70" y1="138.6" x2="610" y2="138.6" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="142.6" font-size="12" text-anchor="end">40</text>
  <line x1="70" y1="105.7" x2="610" y2="105.7" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="109.7" font-size="12" text-anchor="end">50</text>
  <line x1="70" y1="72.9" x2="610" y2="72.9" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="76.9" font-size="12" text-anchor="end">60</text>
  <line x1="70" y1="60" x2="70" y2="270" stroke="currentColor" stroke-opacity="0.5"/>
  <rect x="90.5" y="247.0" width="44" height="23.0" fill="#4a7bd0" fill-opacity="0.85"/>
  <text x="112.5" y="242.0" font-size="12" text-anchor="middle">7.0</text>
  <rect x="140.5" y="263.4" width="44" height="6.6" fill="#e08a3c" fill-opacity="0.85"/>
  <text x="162.5" y="258.4" font-size="12" text-anchor="middle">2.0</text>
  <text x="137.5" y="288" font-size="13" text-anchor="middle">1</text>
  <rect x="225.5" y="148.8" width="44" height="121.2" fill="#4a7bd0" fill-opacity="0.85"/>
  <text x="247.5" y="143.8" font-size="12" text-anchor="middle">36.9</text>
  <rect x="275.5" y="255.9" width="44" height="14.1" fill="#e08a3c" fill-opacity="0.85"/>
  <text x="297.5" y="250.9" font-size="12" text-anchor="middle">4.3</text>
  <text x="272.5" y="288" font-size="13" text-anchor="middle">2</text>
  <rect x="360.5" y="158.3" width="44" height="111.7" fill="#4a7bd0" fill-opacity="0.85"/>
  <text x="382.5" y="153.3" font-size="12" text-anchor="middle">34.0</text>
  <rect x="410.5" y="242.1" width="44" height="27.9" fill="#e08a3c" fill-opacity="0.85"/>
  <text x="432.5" y="237.1" font-size="12" text-anchor="middle">8.5</text>
  <text x="407.5" y="288" font-size="13" text-anchor="middle">4</text>
  <rect x="495.5" y="75.8" width="44" height="194.2" fill="#4a7bd0" fill-opacity="0.85"/>
  <text x="517.5" y="70.8" font-size="12" text-anchor="middle">59.1</text>
  <rect x="545.5" y="241.4" width="44" height="28.6" fill="#e08a3c" fill-opacity="0.85"/>
  <text x="567.5" y="236.4" font-size="12" text-anchor="middle">8.7</text>
  <text x="542.5" y="288" font-size="13" text-anchor="middle">8</text>
  <text x="340" y="312" font-size="13" text-anchor="middle">스레드 수 (VM 4 vCPU, 8은 oversubscribed)</text>
  <text x="340" y="332" font-size="12" text-anchor="middle">참고: 빈 syscall(getppid) 1회 ≈ 127~131 ns (ex16) — 막대 축 밖</text>
</svg>
```
그림 6 — 공유 카운터 증가 1회 비용 실측(ex15). 1스레드는 경합 없음, 2개 이상은 같은 캐시 라인을 두고 경합한다.

### 11.2 해석

- **경합 없는 mutex는 7 ns다.** lock 한 번 + unlock 한 번이 atomic 몇 개로 끝난다. 빈 syscall(약 130 ns)의 1/18이다. "mutex는 무거우니 피해야 한다"는 직관은 경합이 없을 때는 틀렸다 — 8절의 fast path 덕분이다.
- **경합하면 5~8배 비싸진다.** 2스레드부터 36 ns로 뛴다. 비용의 정체는 (1) lock word가 든 캐시 라인이 코어 사이를 오가는 coherence 트래픽과 (2) 일부 futex syscall과 잠들기·깨우기(context switch)다. 8스레드(코어보다 많음)에서는 59 ns로 더 나빠진다.
- **atomic도 공짜가 아니다.** 1스레드 2 ns에서 4스레드 8.5 ns로 4배가 됐다. 잠들지는 않지만 같은 캐시 라인을 두고 코어들이 줄을 서기 때문이다. 하지만 경합 시 mutex보다 4~7배 싸고, 코어 수 이상으로 스레드를 늘려도 크게 나빠지지 않는다(잠들고 깨는 비용이 없어서).
- 교훈: 가장 좋은 동기화는 **공유를 줄이는 것**이다. 스레드별 카운터를 따로 두고(각자 다른 캐시 라인) 읽을 때만 합치면 경합 비용 자체가 사라진다. 커널의 per-CPU 변수가 이 아이디어다(F 모듈).

> 측정은 Apple Silicon 위의 VM에서 했으므로 절대값은 실제 i.MX8·AM62 보드와 다르다(Cortex-A53은 LSE가 없어 atomic이 `ldxr/stxr` 루프다). 비율과 경향을 보는 용도다.

---

## 12. 커널 안에서는 무슨 일이 일어나나

### 12.1 futex syscall의 커널 경로

10.2절의 커널 스택(`el0t_64_sync` → `el0_svc` → `__arm64_sys_futex` → `do_futex` → `futex_wait` → `futex_do_wait`)을 따라 내려가면, 커널 소스 `kernel/futex/` 아래에서 이런 일이 일어난다(함수 이름은 VM의 7.0 기준이다. 6.x 초반 커널에서는 `futex_do_wait` 자리가 `futex_wait_queue`라는 이름이었다).

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="15" width="310" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="175" y="38" font-size="13" text-anchor="middle">futex(uaddr, FUTEX_WAIT_PRIVATE, 2)</text>
  <rect x="350" y="15" width="310" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="505" y="38" font-size="13" text-anchor="middle">futex(uaddr, FUTEX_WAKE_PRIVATE, 1)</text>
  <line x1="10" y1="66" x2="670" y2="66" stroke="currentColor" stroke-dasharray="6 4"/>
  <text x="664" y="80" font-size="12" text-anchor="end">kernel</text>
  <text x="664" y="61" font-size="12" text-anchor="end">user</text>
  <line x1="175" y1="51" x2="175" y2="88" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="505" y1="51" x2="505" y2="88" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="20" y="90" width="640" height="52" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="112" font-size="13" text-anchor="middle">get_futex_key() → futex_hash(): 같은 주소 = 같은 키 = 같은 해시 버킷</text>
  <text x="340" y="131" font-size="13" text-anchor="middle">private 키 = (mm, 가상 주소) · shared 키 = (공유 매핑의 inode/페이지, offset)</text>
  <line x1="340" y1="142" x2="340" y2="164" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="170" y="166" width="340" height="52" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="188" font-size="13" text-anchor="middle">struct futex_hash_bucket</text>
  <text x="340" y="207" font-size="13" text-anchor="middle">spinlock + 대기자 리스트 (futex_q들)</text>
  <line x1="200" y1="218" x2="175" y2="246" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="480" y1="218" x2="505" y2="246" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="20" y="248" width="310" height="52" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="175" y="270" font-size="13" text-anchor="middle">futex_wait_setup(): 버킷 잠금</text>
  <text x="175" y="289" font-size="13" text-anchor="middle">*uaddr 재확인 — 다르면 -EAGAIN</text>
  <line x1="175" y1="300" x2="175" y2="322" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="20" y="324" width="310" height="52" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="175" y="346" font-size="13" text-anchor="middle">futex_q 등록 → futex_do_wait()</text>
  <text x="175" y="365" font-size="13" text-anchor="middle">TASK_INTERRUPTIBLE → schedule()</text>
  <rect x="350" y="248" width="310" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="505" y="270" font-size="13" text-anchor="middle">futex_wake(): 버킷에서 같은 키의</text>
  <text x="505" y="289" font-size="13" text-anchor="middle">futex_q를 찾아 최대 n개 깨움</text>
  <line x1="350" y1="290" x2="332" y2="345" stroke="#3f9a6b" stroke-dasharray="4 3" marker-end="url(#ah7)"/>
  <rect x="20" y="390" width="640" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="412" font-size="13" text-anchor="middle">LOCK_PI → pi_state + rt_mutex · 종료 시 exit_robust_list() → OWNER_DIED</text>
</svg>
```
그림 7 — futex의 커널 내부. 커널은 lock word의 의미를 모르고 "주소 → 대기 큐"만 관리한다. 버킷 spinlock을 잡은 상태에서 값을 재확인하기 때문에 "확인 후 잠들기 전에 깨움을 놓치는" lost wakeup이 생기지 않는다.

정리하면:

- **키**: `get_futex_key()`가 유저 주소를 키로 바꾼다. private futex는 (프로세스 `mm`, 가상 주소)로 끝나서 싸다. 공유 futex(프로세스 간 robust mutex, named semaphore)는 같은 물리 페이지를 다른 가상 주소로 매핑한 프로세스들이 같은 키를 얻도록 공유 매핑의 inode/페이지와 offset을 쓴다 — 그래서 `_PRIVATE`가 더 빠르다.
- **버킷**: `futex_hash()`로 고른 `struct futex_hash_bucket`은 spinlock 하나와 대기자(`struct futex_q`) 리스트다. 대기자는 `plist`(우선순위 정렬 리스트)에 들어가서, 깨울 때 높은 우선순위 RT 스레드가 먼저 깬다.
- **재확인**: `futex_wait_setup()`은 버킷 spinlock을 잡은 뒤 `*uaddr`가 아직 기대값인지 다시 읽는다. 이것이 condvar의 "unlock + sleep 원자성"을 커널이 제공하는 방법이다.
- **PI**: `FUTEX_LOCK_PI`는 lock word의 TID로 주인 `task_struct`를 찾아 `pi_state`를 만들고, 커널 `rt_mutex`에 대기시켜 주인의 우선순위를 올린다(`kernel/futex/pi.c`, `kernel/locking/rtmutex.c`).
- **robust**: 스레드가 죽으면 `exit_robust_list()`가 `set_robust_list`로 등록된 목록을 훑어 `FUTEX_OWNER_DIED` 비트를 세우고 대기자를 깨운다(2.3절).

### 12.2 유저 primitive ↔ 커널 primitive

드라이버를 쓰기 시작하면 같은 개념을 커널 API로 다시 만난다. 깊은 설명은 F2–F5에서 하고, 여기서는 대응표만 잡아 둔다.

| 유저스페이스 | 커널 안 | 결정적 차이 | 노트 |
|---|---|---|---|
| `pthread_mutex` | `struct mutex` (`mutex_lock`) | 잠들 수 있는 문맥(process context)에서만. IRQ 핸들러에서 금지. 주인이 실행 중이면 잠깐 spin(optimistic spinning) | F4 |
| `pthread_spin` | `spinlock_t` (`spin_lock`, `spin_lock_irqsave`) | **preemption을 끈다**, `_irqsave`는 로컬 IRQ까지 끈다. 그래서 커널에선 정상적인 도구. 쥔 채 잠들면 안 됨 | F2, F4 |
| `sem_t` | `struct semaphore` (`down`/`up`) | 새 코드에선 거의 안 씀. 신호 용도는 `completion` | F3, F4 |
| `pthread_cond` + while 루프 | `wait_queue_head_t` + `wait_event_interruptible(wq, cond)` | **매크로 자체가 cond를 루프로 재확인**한다 — 3.2절 규칙이 API에 내장 | F3 |
| `pthread_rwlock` | `rwlock_t`, `struct rw_semaphore`, **RCU** | 읽기 위주 데이터는 RCU(독자가 락 없이 읽고 작성자가 grace period를 기다림) | F4 |
| C11 atomics | `atomic_t` (`atomic_inc`, `atomic_cmpxchg`) | 커널은 C11 atomics 대신 자체 API와 메모리 모델(LKMM)을 쓴다 | F4, F5 |
| `memory_order_acquire/release` | `smp_load_acquire()`, `smp_store_release()`, `smp_mb()`, `READ_ONCE`/`WRITE_ONCE` | 장치(MMIO·DMA)와의 순서는 `wmb()`, `dma_wmb()` 같은 별도 배리어 | F5, H1, H9 |
| `PTHREAD_PRIO_INHERIT` | `rt_mutex`, PREEMPT_RT의 `spinlock_t` | RT 커널에선 대부분의 커널 락이 rt_mutex 기반 sleeping lock | N1 |
| ThreadSanitizer, helgrind | lockdep, KCSAN | lockdep은 커널 락 순서를 런타임 검증 (10.5절 규칙의 커널판) | M8 |

### 12.3 드라이버를 쓸 때 다시 나오는 곳

- **blocking `read()` 구현**(G4, F3): 센서 드라이버의 `.read`는 "데이터가 없으면 `wait_event_interruptible(dev->wq, dev->ready)`로 잠들고, IRQ 핸들러가 데이터를 채운 뒤 `wake_up_interruptible(&dev->wq)`"로 짠다. 정확히 ex4의 소비자/생산자 구조이고, 이 대기 큐가 futex 버킷의 대기 리스트와 같은 종류의 물건이다.
- **IRQ 핸들러와 process context가 공유하는 데이터**(F2, F6): 핸들러는 잠들 수 없으니 mutex를 못 쓴다. `spin_lock_irqsave`를 쓴다 — Don이 bare-metal에서 `__disable_irq()`로 하던 것의 SMP 버전이다.
- **링버퍼**: 커널의 `kfifo`는 단일 생산자·단일 소비자일 때 락 없이 쓰도록 설계돼 있다(ex10과 같은 아이디어). 드라이버의 RX 버퍼에 자주 쓴다.
- **DMA 디스크립터**(H9): "버퍼 채우기 → `dma_wmb()` → owner 비트 넘기기"는 그림 3의 release 패턴을 장치 상대로 하는 것이다.
- **유저 API를 설계할 때**: 드라이버가 노출하는 공유 메모리(`mmap`, A5)를 유저가 여러 스레드로 쓸 거라면, 헤더에 "이 인덱스는 release로 쓰고 acquire로 읽어라" 같은 프로토콜을 명시해야 한다. V4L2·io_uring의 링 인터페이스가 이렇게 설계돼 있다.

---

## 13. Embedded 실무에서 다시 보기

### 13.1 IMU 1 kHz → 퓨전 스레드: SPSC + 최신값 스냅샷

센서 키트에서 IMU 스레드(SCHED_FIFO 80)가 1 kHz로 SPI를 읽어 퓨전 스레드로 넘긴다. 모든 샘플이 필요하면 ex10 SPSC 링으로 넘긴다 — RT 스레드가 락 때문에 잠들 일이 없다. "최신 값 하나만" 필요한 상태 공유(예: 최신 자세)라면 링도 과하다. 두 버퍼를 번갈아 쓰고 인덱스만 release로 공개하는 방식이나, 커널의 seqlock과 같은 시퀀스 카운터 방식을 쓴다. 공통 원칙은 **RT 스레드는 non-RT 스레드가 쥘 수 있는 락을 기다리지 않는다**는 것이다.

### 13.2 RT 스레드와 로깅 스레드가 mutex를 공유할 때

어쩔 수 없이 공유한다면 반드시 PI mutex로 만든다(9절). 실제 장비에서 자주 보는 사고는 이렇다: 1 kHz 제어 루프가 가끔 수십 ms씩 늦는데 원인은 로깅 스레드가 쥔 mutex였고, 그 사이 중간 우선순위의 네트워크 스레드가 CPU를 쓰고 있었다. `cyclictest`류 측정(N3)에서 긴 꼬리 지연이 보이면 의심할 1순위다.

```c
pthread_mutexattr_t ma;
pthread_mutexattr_init(&ma);
pthread_mutexattr_setprotocol(&ma, PTHREAD_PRIO_INHERIT);   /* RT 공유 락은 항상 */
pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_ERRORCHECK);  /* 디버그 빌드에서 */
pthread_mutex_init(&shared_lock, &ma);
```

### 13.3 여러 데몬이 공유 메모리로 카메라 메타데이터를 나눌 때

카메라 데몬과 인식 데몬이 공유 메모리(A5, A7)로 프레임 메타데이터를 주고받는다면 mutex는 `PTHREAD_PROCESS_SHARED` + `PTHREAD_MUTEX_ROBUST`로 만들고, `EOWNERDEAD`를 받으면 복구하는 코드를 반드시 짠다(ex3). systemd가 죽은 데몬을 재시작해도, robust가 아니면 살아남은 쪽이 영원히 막혀서 장비 전체가 멈춘다.

### 13.4 현장에서 "멈췄다"는 보고를 받았을 때

1. `top -H -p <pid>`로 스레드별 CPU를 본다. 0%면 deadlock(잠듦) 쪽, 100%면 livelock이나 spin 쪽이다.
2. `cat /proc/<pid>/task/*/wchan`으로 커널 어디서 자는지 본다. `futex_do_wait`가 많으면 유저 락 대기다(10.2절).
3. `gdb -p <pid> -batch -ex "thread apply all bt"`로 누가 어떤 락을 기다리는지 본다. 원격 보드라면 gdbserver(M9), 재현이 어려우면 `gcore`로 core dump를 떠서 가져온다.
4. 재발 방지는 TSan을 CI에 넣고 락 순서 규칙을 문서화한다.

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 공유 카운터에 `volatile`만 붙임 | 값이 가끔 모자람(ex1) | `volatile`은 원자성을 주지 않음 | `atomic_fetch_add` 또는 mutex |
| condvar를 `if`로 기다림 | 가끔 빈 큐에서 꺼내거나 데이터 손상(ex4b) | spurious·stolen wakeup | 항상 `while (!pred) pthread_cond_wait()` |
| predicate를 mutex 밖에서 변경 | 가끔 영원히 잠듦 | 확인과 잠들기 사이에 signal 유실 | predicate 변경도 같은 mutex 안에서 |
| relaxed로 "준비됐다" 플래그 전달 | arm64에서만, 아주 드물게 옛 데이터 읽음(ex9) | weak ordering으로 store/load 재배치 | 플래그는 release store + acquire load |
| 유저스페이스 spinlock 남용 | CPU 사용률 폭증, 코어보다 스레드 많으면 급격히 느려짐(ex7) | 주인이 preempt되면 대기자가 타임 슬라이스 소진 | mutex 사용, spin은 코어 고정·초단기일 때만 |
| RT 스레드 공유 mutex에 PI 미설정 | 제어 루프가 가끔 수십~수백 ms 지연(ex13) | unbounded priority inversion | `PTHREAD_PRIO_INHERIT` |
| 락 순서가 경로마다 다름 (ABBA) | 드물게 프로세스 전체가 CPU 0%로 멈춤(ex14) | 순환 대기 | 전역 락 순서 규칙, TSan/helgrind로 검증 |
| 공유 메모리 mutex가 robust 아님 | 한 데몬 크래시 후 다른 데몬이 영구 정지 | 주인 사망을 알 방법 없음 | `PTHREAD_MUTEX_ROBUST` + `EOWNERDEAD` 처리 |
| head/tail을 같은 캐시 라인에 둠 | SPSC 링이 기대보다 수 배 느림 | false sharing | `_Alignas(64)`로 분리 |
| `pthread_cond_timedwait` 기본 시계 사용 | 시간 동기화 순간 타임아웃이 너무 길거나 즉시 만료 | `CLOCK_REALTIME`이 점프 | `pthread_condattr_setclock(CLOCK_MONOTONIC)` |

---

## 15. 면접에서 이렇게 말한다

**Q.** What is a race condition? Show me one at the instruction level.

**A.** 결과가 스레드 실행 순서에 따라 달라지는 상태다. `counter++`는 arm64에서 `ldr`/`add`/`str` 세 명령이라, 두 코어가 둘 다 옛 값을 읽고 각자 +1해서 쓰면 증가 하나가 사라진다. 실제로 4스레드 × 100만 번에서 70% 이상이 사라지는 걸 재현했다. `volatile`은 해결책이 아니고, atomic RMW나 mutex가 필요하다.

> A race condition is when the result depends on the timing of threads. `counter++` compiles to three instructions on arm64 — ldr, add, str — so if two cores both load the old value, one increment is lost. I reproduced it: four threads doing a million increments each lost over 70 percent. `volatile` doesn't fix it; you need an atomic read-modify-write like `ldadd`, or a mutex.

**꼬리 질문**: "그럼 테스트에서 결과가 맞으면 안전한가?" → 아니다. `-O2`에서 컴파일러가 루프를 합쳐 우연히 맞게 나온 사례를 보여 주고, data race는 C11에서 UB이므로 TSan으로 검증한다고 답한다.

**Q.** Mutex vs spinlock — when would you use each?

**A.** mutex는 못 잡으면 잠들고, spinlock은 CPU를 돌리며 기다린다. 커널에선 spinlock이 preemption을 끄기 때문에 짧은 임계 구역이나 IRQ 문맥에서 표준이다. 유저스페이스에선 preemption을 끌 수 없어서, 주인이 밀려나면 대기자가 타임 슬라이스를 태운다. 측정해 보니 4코어에 8스레드에서 spin이 mutex보다 wall 7배, CPU 21배 더 썼다. 그래서 유저에선 mutex가 기본이고 spin은 코어 고정된 초단기 구간에만 쓴다.

> A mutex sleeps when contended; a spinlock burns CPU while waiting. In the kernel, spin_lock disables preemption, so it's the right tool for short sections and interrupt context. In user space you can't disable preemption — if the holder gets preempted, every waiter spins out its whole time slice. I measured 8 threads on 4 cores: the spinlock version took 7x the wall time and 21x the CPU time of a mutex. So in user space I default to a mutex, and an uncontended glibc mutex is only about 7 nanoseconds anyway.

**꼬리 질문**: "커널 IRQ 핸들러와 공유하는 데이터는?" → `spin_lock_irqsave`, 핸들러는 잠들 수 없으니 mutex 불가(F2, F4).

**Q.** What is a futex and why is it fast?

**A.** fast userspace mutex. 락 상태는 유저 메모리의 정수 하나이고, 경합이 없으면 atomic CAS 하나로 끝나서 syscall이 없다. 경합할 때만 `futex(WAIT)`로 커널에 "이 주소 값이 아직 이거면 재워 달라"고 하고, unlock 때 대기자가 있었으면 `futex(WAKE)`를 부른다. strace로 보면 경합 없는 100만 번 lock/unlock에 futex가 0회다. 커널은 주소를 키로 해시 버킷의 대기 큐만 관리한다.

> A futex is a fast userspace mutex. The lock state is just an integer in user memory; the uncontended path is a single atomic compare-and-swap with no system call. Only on contention does the thread call futex WAIT, which atomically checks that the word still has the expected value and sleeps; unlock calls futex WAKE only if waiters were recorded. Under strace, a million uncontended lock/unlock pairs made zero futex calls. The kernel just hashes the address to a bucket with a wait queue.

**꼬리 질문**: "WAIT에 기대값을 왜 넘기나?" → 확인과 잠들기 사이에 unlock이 끼면 영원히 잔다. 커널이 버킷 락을 잡고 재확인해서 lost wakeup을 막는다(그림 7).

**Q.** Why must you wait on a condition variable in a while loop?

**A.** 깨어났다고 조건이 참이라는 보장이 없어서다. POSIX가 spurious wakeup을 허용하고, 더 흔하게는 다른 소비자가 먼저 mutex를 잡고 아이템을 가져가는 stolen wakeup이 있다. `if`로 바꾼 bounded queue를 돌려 보면 합계가 10% 넘게 틀린다. 커널의 `wait_event` 매크로도 내부에서 조건을 루프로 재확인한다.

> Because returning from wait doesn't mean the predicate is true. POSIX allows spurious wakeups, and more commonly another consumer grabs the mutex first and takes the item — a stolen wakeup. When I changed `while` to `if` in a bounded queue with two consumers, the checksum came out more than 10 percent wrong. The kernel's `wait_event` macro re-checks the condition in a loop for the same reason.

**Q.** Explain priority inversion and how Linux handles it.

**A.** 높은 우선순위 H가 낮은 L이 쥔 락을 기다리는데, 중간 M이 L을 밀어내서 H가 M의 실행 시간만큼 무한정 늦어지는 현상이다. Mars Pathfinder가 이걸로 반복 리셋됐고 PI를 켜서 고쳤다. Linux는 `PTHREAD_PRIO_INHERIT` mutex를 쓰면 `FUTEX_LOCK_PI`로 커널 rt_mutex가 L의 우선순위를 H만큼 올린다. 기본값은 꺼져 있다. 실측에서 H 대기가 219 ms에서 19 ms로 줄었다.

> Priority inversion is when a high-priority task waits on a lock held by a low-priority task, and a medium-priority task preempts the low one, so the high task's wait becomes unbounded. That's what reset Mars Pathfinder; they fixed it by enabling priority inheritance. On Linux you set `PTHREAD_PRIO_INHERIT` on the mutex; contention then goes through `FUTEX_LOCK_PI` and the kernel's rt_mutex boosts the owner. It's off by default. In my SCHED_FIFO test the high thread's wait dropped from 219 ms to 19 ms.

**꼬리 질문**: "semaphore로는 왜 안 되나?" → 주인이 없어서 누구를 올릴지 모른다. "커널 안에서는?" → PREEMPT_RT에서 spinlock_t·mutex가 rt_mutex 기반이 된다(N1).

**Q.** What do acquire and release mean, and why does it matter on ARM?

**A.** release store는 그 앞의 모든 읽기·쓰기가 store보다 먼저 보이게 하고, acquire load는 그 뒤의 접근이 load보다 앞당겨지지 않게 한다. 짝을 이루면 release 전에 쓴 데이터가 acquire로 플래그를 본 스레드에게 반드시 보인다. arm64는 weakly ordered라 relaxed로 하면 실제로 "flag=1인데 data=0"이 5천만 번 중 몇 번 관찰된다. arm64에선 `stlr`/`ldar` 명령 하나씩으로 구현되고 x86처럼 full fence가 필요 없다.

> A release store guarantees all earlier memory operations become visible before the store; an acquire load prevents later operations from moving before it. Paired, anything written before the release is visible to a thread that sees the flag via acquire. arm64 is weakly ordered — with relaxed atomics I actually observed flag=1 but data=0 a few times in 50 million trials. On arm64 they map to `stlr` and `ldar`, single instructions, no full barrier needed.

**Q.** How would you debug a deadlock on a deployed device?

**A.** 먼저 `top -H`로 CPU가 0%인지 본다(잠든 deadlock). `/proc/<pid>/task/*/wchan`이 `futex_do_wait`면 유저 락 대기다. `gdb -p`로 `thread apply all bt`를 떠서 각 스레드가 어떤 mutex를 기다리는지 보고 순환을 찾는다. 원인은 대개 ABBA 락 순서다. 재발 방지로 전역 락 순서를 정하고 CI에 TSan을 넣는다 — TSan은 실제 deadlock이 안 나도 순서 역전을 잡는다.

> I'd check `top -H` first — zero CPU suggests a deadlock rather than a livelock. If the threads' `wchan` shows `futex_do_wait`, they're blocked on user-space locks. Then `gdb -p` with `thread apply all bt` shows which mutex each thread is waiting on, and you look for the cycle — usually an ABBA lock order. To prevent it, define a global lock ordering and run ThreadSanitizer in CI, which reports lock-order inversions even when the deadlock didn't actually happen.

---

## 16. 직접 해보기

1. **개념**: ex1에서 스레드 수를 1, 2, 8로 바꾸면 lost update 비율은 어떻게 될까? 예측하고 돌려 본다. 정답/힌트: 1이면 0. 늘수록 늘지만 8(코어 초과)에서는 스레드가 번갈아 돌아 겹침이 오히려 줄 수 있다. 측정으로 확인할 것.
2. **코드**: ex4의 condvar 두 개를 하나로 합치고 `pthread_cond_signal`을 그대로 쓰면 무슨 일이 생기나? `broadcast`로 바꾸면? 정답/힌트: 생산자가 생산자를 깨우는 식의 잘못된 깨움으로 모두 잠드는 hang이 생길 수 있다. `timeout 5`로 감싸서 실험하고, `broadcast`로 고친다.
3. **코드**: ex12 mutex를 2-상태(0/1)로 바꾸고 unlock마다 무조건 `FUTEX_WAKE`를 부르게 한 뒤, 1스레드에서 `strace -c -e futex`로 syscall 수를 비교한다. 정답/힌트: 1스레드에서도 unlock마다 syscall이 생겨 100만 회 가까이 나온다. 상태 2의 존재 이유다.
4. **관찰**: ex9를 `-DUSE_ACQREL` 없이 빌드하되 writer의 두 store 사이에만 `atomic_thread_fence(memory_order_release)`를, reader의 두 load 사이에 `atomic_thread_fence(memory_order_acquire)`를 넣는다. 결과와 디스어셈블리(`dmb`)를 확인한다. 정답/힌트: 이상 관찰 0회가 되고, `dmb ishst`/`dmb ishld` 또는 `dmb ish`가 보일 것이다. fence와 ldar/stlr의 차이를 비교해 볼 것.
5. **관찰**: ex13을 PI로 돌리는 동안 다른 터미널에서 `ps -eLo tid,cls,rtprio,pri,comm`을 반복해서 L의 우선순위가 30으로 올라가는 순간을 잡아 본다. 정답/힌트: L의 `rtprio`가 잠시 30으로 보인다. 20 ms 창이므로 `burn_cpu(20)`을 2000으로 늘려서 관찰한다.
6. **코드**: ex15에 스레드별 카운터(각자 64바이트 정렬된 슬롯) 방식을 추가해서 8스레드에서의 ns/op를 mutex, atomic과 비교한다. 정답/힌트: 공유가 없으니 스레드 수에 따라 오히려 빨라진다. per-CPU 변수(F4)의 원리다.
7. **관찰**: `strace -f -e trace=futex`로 ex5(semaphore)와 ex4(condvar)를 추적해서, 어떤 주소에서 WAIT/WAKE가 일어나는지 본다. named semaphore 쪽 futex에 `_PRIVATE`가 붙는지 확인한다. 정답/힌트: 프로세스 간 공유인 named semaphore 쪽은 `_PRIVATE`가 붙지 않은 futex 연산이 보여야 한다. 실제로 어떤 op가 쓰이는지 직접 확인할 것.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| race condition | 경쟁 상태 | 결과가 실행 타이밍에 따라 달라지는 상태 |
| data race | 데이터 경쟁 | 동기화 없이 같은 메모리에 동시 접근(하나 이상이 쓰기). C11에서 UB |
| lost update | 갱신 유실 | read-modify-write가 겹쳐 한쪽 쓰기가 사라짐 |
| critical section | 임계 구역 | 한 번에 한 스레드만 실행해야 하는 코드 구간 |
| mutex | 상호 배제 락 | 소유자가 있는 잠들 수 있는 락 |
| robust mutex | 견고한 mutex | 주인이 죽으면 다음 lock이 `EOWNERDEAD`를 받는 mutex |
| condition variable | 조건 변수 | 조건이 바뀌었다는 알림 채널. 상태는 저장하지 않음 |
| spurious wakeup | 가짜 깨움 | signal 없이 wait가 반환되는 것. POSIX가 허용 |
| semaphore | 세마포어 | 카운터 + 대기 큐. 소유자 없음 |
| rwlock | 읽기-쓰기 락 | 독자는 여럿, 작성자는 혼자 |
| spinlock | 스핀락 | 잠들지 않고 돌며 기다리는 락 |
| futex | fast userspace mutex | 유저 메모리 정수 + 커널 주소별 대기 큐 |
| fast path / slow path | 빠른 경로 / 느린 경로 | 경합 없으면 유저 atomic만, 경합 시 syscall |
| memory ordering | 메모리 순서 | 다른 코어가 관찰하는 load/store 순서의 보장 수준 |
| acquire / release | 획득 / 해제 의미 | 짝을 이뤄 release 전 쓰기를 acquire 후에 보이게 함 |
| weakly ordered | 약한 순서 | 하드웨어가 메모리 접근 순서를 바꿔 보일 수 있는 아키텍처(arm64) |
| LSE | Large System Extensions | ARMv8.1의 단일 명령 atomic(`ldadd`, `cas`, `swp`) |
| litmus test | 리트머스 테스트 | 메모리 모델 동작을 확인하는 작은 동시성 실험 |
| false sharing | 거짓 공유 | 서로 다른 변수가 한 캐시 라인에 있어 코어끼리 라인을 뺏는 현상 |
| SPSC ring | 단일 생산자·소비자 링 | acquire/release만으로 락 없이 동작하는 링버퍼 |
| priority inversion | 우선순위 역전 | 높은 우선순위가 낮은 우선순위를 기다리는데 중간이 끼어드는 현상 |
| priority inheritance | 우선순위 상속 | 락 주인의 우선순위를 대기자만큼 일시 상승 |
| rt_mutex | RT mutex | PI를 구현하는 커널 락. PI futex와 PREEMPT_RT의 기반 |
| deadlock | 교착 상태 | 서로가 쥔 자원을 기다리며 영원히 멈춤 |
| ThreadSanitizer | TSan | 컴파일러 계측으로 data race·락 순서 역전을 찾는 도구 |
| helgrind | 헬그린드 | Valgrind 기반 스레드 오류 검출기 |

---

## 18. 요약 & 체크리스트

공유 데이터를 동시에 만지면 `ldr`/`add`/`str` 사이에 다른 코어가 끼어들어 갱신이 사라진다. 이를 막는 도구는 용도별로 다르다: 상호 배제는 mutex, 조건 대기는 condvar(항상 `while`), 개수·신호는 semaphore, 읽기 위주는 rwlock, 단일 변수는 atomic이다. arm64는 weakly ordered라 스레드 간 "준비됐다" 신호는 release/acquire(`stlr`/`ldar`) 짝으로 보내야 하고, 그 패턴이 lock-free SPSC 링의 전부다. pthread의 모든 대기는 futex 위에 서 있다 — 경합이 없으면 atomic 하나(약 7 ns)로 끝나고 경합 때만 커널의 주소별 대기 큐로 내려간다. 유저스페이스 spinlock은 preemption을 끌 수 없어서 대개 손해이고, RT 스레드가 공유하는 mutex는 PI를 켜지 않으면 Pathfinder처럼 수백 ms 지연을 만든다. deadlock은 락 순서 규칙으로 막고 TSan·helgrind로 검증한다. 이 개념들은 커널에서 `mutex`·`spinlock_t`·`wait_event`·`smp_store_release`·`rt_mutex`로 다시 등장한다(F2–F5, N1).

- [ ] `counter++`가 왜 race인지 arm64 명령 세 개로 설명할 수 있다
- [ ] `volatile`이 동기화가 아닌 이유와, 테스트 통과가 증거가 아닌 이유를 설명할 수 있다
- [ ] mutex 타입(normal/errorcheck/recursive)과 robust mutex의 `EOWNERDEAD` 처리를 코드로 짤 수 있다
- [ ] condvar를 `while`로 기다려야 하는 두 이유(spurious, stolen)를 말하고 bounded queue를 짤 수 있다
- [ ] semaphore와 mutex의 차이(소유자, PI 가능 여부)를 설명할 수 있다
- [ ] 유저스페이스 spinlock이 대개 나쁜 이유를 커널 spinlock과 비교해 설명할 수 있다
- [ ] relaxed/acquire/release/seq_cst가 arm64에서 어떤 명령이 되는지 말할 수 있다
- [ ] message-passing litmus를 설명하고 acquire/release로 고칠 수 있다
- [ ] lock-free SPSC 링버퍼를 acquire/release로 짤 수 있다
- [ ] futex의 fast/slow path와 3-상태 lock word를 그림으로 설명하고 strace로 보여 줄 수 있다
- [ ] priority inversion을 Pathfinder로 설명하고 `PTHREAD_PRIO_INHERIT`로 고칠 수 있다
- [ ] 멈춘 프로세스를 `/proc/*/wchan`과 `gdb thread apply all bt`로 진단하고 TSan으로 예방할 수 있다

---

## 참고 자료

- man 페이지: [futex(2)](https://man7.org/linux/man-pages/man2/futex.2.html), [futex(7)](https://man7.org/linux/man-pages/man7/futex.7.html), [pthreads(7)](https://man7.org/linux/man-pages/man7/pthreads.7.html), [sem_overview(7)](https://man7.org/linux/man-pages/man7/sem_overview.7.html), [pthread_mutexattr_setrobust(3)](https://man7.org/linux/man-pages/man3/pthread_mutexattr_setrobust.3.html), [pthread_spin_init(3)](https://man7.org/linux/man-pages/man3/pthread_spin_init.3.html), [set_robust_list(2)](https://man7.org/linux/man-pages/man2/set_robust_list.2.html)
- Michael Kerrisk, "The Linux Programming Interface" — 30장 Threads: Thread Synchronization, 53장 POSIX Semaphores
- OSTEP — 28장 Locks, 30장 Condition Variables, 31장 Semaphores, 32장 Common Concurrency Problems
- Ulrich Drepper, ["Futexes Are Tricky"](https://www.akkadia.org/drepper/futex.pdf) — ex12의 3-상태 mutex 출처
- 커널 문서: [Lock types and their rules](https://docs.kernel.org/locking/locktypes.html), [Robust futexes](https://docs.kernel.org/locking/robust-futexes.html), [PI-futex](https://docs.kernel.org/locking/pi-futex.html), [RT-mutex subsystem with PI support](https://docs.kernel.org/locking/rt-mutex.html), [memory-barriers.txt](https://www.kernel.org/doc/Documentation/memory-barriers.txt)
- 커널 소스(elixir.bootlin.com): `kernel/futex/waitwake.c`(futex_wait_setup, futex_wake), `kernel/futex/core.c`(get_futex_key, exit_robust_list), `kernel/futex/pi.c`, `kernel/locking/rtmutex.c`
- glibc 소스: `nptl/pthread_mutex_lock.c`, `nptl/lowlevellock.c`(10.2절 gdb 백트레이스에 나온 파일들)
- Mike Jones, ["What really happened on Mars?"](https://www.cs.cmu.edu/afs/cs/user/raj/www/mars.html) — Pathfinder priority inversion, Glenn Reeves(JPL)의 설명 포함
- [ThreadSanitizer C/C++ manual](https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual), [Helgrind manual](https://valgrind.org/docs/manual/hg-manual.html)
