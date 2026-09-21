# Verkada 기술 세션 학습 가이드 — 동시성·실시간·설계를 개념부터

> **대상 세션**: Verkada · Senior Embedded Linux Engineer, Connectivity — 2파트(problem solving + system design)
> **근거 문서**: 공고(Greenhouse 5209588007) · 리크루터 Davis 안내 메일(2026-09-15)
> **읽는 법**: 1장부터 순서대로. 각 장 끝의 자가 점검을 **먼저 머릿속으로 답한 뒤** 펼쳐 본다. 다 맞히면 "이 장 완료 표시"를 누르고 다음 장으로 간다. 틀린 게 있으면 그 절만 다시 읽는다.

이 가이드는 한 가지 질문에 답하려고 쓴 것이다. **"리크루터가 적어준 네 가지 주제를, 면접관 앞에서 설계하고 설명할 수 있을 만큼 이해하려면 무엇을 어떤 순서로 알아야 하나?"**

그래서 암기 목록이 아니라 **개념이 쌓이는 순서**로 짰다. 1장의 개념 없이는 2장이 안 읽히고, 4장(조건 변수)은 3장(뮤텍스)을 전제로 한다. 코드는 개념을 확인하는 용도로만 짧게 넣었고, 손으로 짜는 연습은 각 장 끝의 "연습 연결"이 이미 만들어 둔 문제 은행으로 안내한다.

---

## 0장. 시작하기 — 이 세션이 무엇을 보는가

### 0.1 메일이 알려준 것 — 출제 범위와 채점 기준

Davis의 메일은 두 가지를 명시했다. 하나는 **공부할 주제**, 다른 하나는 **보는 기준**이다. 이 가이드의 장 구성은 이 두 목록을 그대로 따른다.

| 메일의 주제 | 실제로 묻는 것 | 이 가이드 |
|---|---|---|
| Thread safety in multi-threaded environments | 공유 데이터를 여러 스레드가 만질 때 무엇이 깨지고 어떻게 지키나 | 1장 · 2장 |
| Synchronization mechanisms (mutexes, condition variables, atomic operations) | 세 도구 각각의 의미, 언제 무엇을 고르나 | 3장 · 4장 · 5장 |
| Real-time data handling patterns (such as double buffering) | 생산자를 멈추지 않으면서 소비자가 망가진 데이터를 보지 않게 하는 법 | 6장 |
| Design principles for modular, testable embedded software | 하드웨어 없이 테스트할 수 있게 코드를 나누는 법 | 7장 |
| (세션 2부) System design | 위 도구들을 조합해 게이트웨이 소프트웨어를 설계하고 trade-off를 설명 | 8장 · 9장 |

| 메일의 채점 기준 | 뜻 | 연습 방법 |
|---|---|---|
| how you approach design decisions and justify your trade-offs | "왜 이 도구인가"를 말로 설명 | 각 장의 "고르는 기준" 표를 소리 내어 설명 |
| ability to write clean and maintainable code | 짧은 함수, 명확한 이름, 불변식 주석 | 문제 은행에서 직접 구현 |
| how you handle edge cases and ensure robustness in concurrent systems | 빈 큐, 가득 참, 종료, 가짜 기상, 이중 close | 4장의 종료 프로토콜, 10장 체크리스트 |
| clarity of your communication and thought process | 코드 치기 전에 설계를 말하고, 치면서 내레이션 | 10장의 문장 템플릿 |

> **핵심**: 코드가 돌아가는 것은 최소 조건이다. 점수는 **"왜"를 설명하는 데서** 난다. 그래서 이 가이드의 모든 장은 "무엇"보다 "왜"에 분량을 더 쓴다.

### 0.2 공고(JD)와의 연결

공고에서 이 세션과 직접 연결되는 문구는 이것들이다.

- **"Proficient in C, C++ or Go"** — 코딩은 C로 해도 된다. Don의 가장 강한 언어로 가는 게 맞다(언어 선택은 리크루터에게 확인).
- **"working knowledge of FreeRTOS and Embedded Linux"** — 같은 동시성 개념이 RTOS 태스크와 Linux 스레드 양쪽에 나온다. 이 가이드는 둘을 나란히 보여준다.
- **"SPI, I2C, UART"** — 7장의 테스트 가능한 드라이버 예시가 I2C 센서다.
- **"development with test driven and data driven methods"** — 7장이 직접 다룬다.
- **"Own the full engineering cycle end to end"** — 8장 시스템 설계에서 요구사항부터 관측성·테스트까지 말하는 이유다.
- **Connectivity 팀 제품** — 셀룰러 게이트웨이(GC31-E), Wi-Fi 게이트웨이(GW31-E), 태양광 트레일러, 공기질 센서. 예제 시나리오는 전부 여기서 가져왔다.

### 0.3 Don의 출발점 — 이미 아는 것

Don은 이 주제를 **처음 배우는 사람이 아니다.** SSD 컨트롤러 펌웨어에서 매일 하던 일이 이름만 다를 뿐 같은 개념이다.

| Don이 이미 한 것 | 이 가이드에서의 이름 |
|---|---|
| 호스트 명령 큐 → NAND 작업 스케줄링 | producer/consumer, bounded queue (4장) |
| DMA 완료 인터럽트 후 다음 버퍼 지정 | ping-pong / double buffering (6장) |
| UART RX 인터럽트 → 링버퍼 → 파서 | SPSC lock-free ring buffer (5장) |
| 인터럽트 끄고 짧게 공유 변수 갱신 | critical section, mutex (3장) |
| 멀티코어 Cortex-R 간 공유 메모리 | memory ordering, barrier (5장) |
| 에러 리포팅/텔레메트리 체계 | 관측성, drop 카운터 (6장·8장) |
| 챔버 테스트 자동화 | 테스트 3층, HIL (7장) |

새로 익혀야 하는 것은 **POSIX API의 이름과 규칙**, 그리고 **C11 atomic의 memory order 어휘**다. 개념 자체는 이미 몸에 있다. 면접에서도 이 대응을 적극적으로 말하면 "Embedded Linux 경험이 얕다"는 약점이 "동시성의 본질을 하드웨어 수준에서 안다"는 강점으로 바뀐다.

### 0.4 각 장의 구조

모든 장은 같은 순서로 되어 있다.

1. **왜 필요한가** — 이 개념이 없으면 무엇이 깨지는지
2. **개념** — 비유와 그림으로, 그다음 정확한 정의
3. **핵심 코드** — 개념을 확인하는 최소한의 코드
4. **고르는 기준** — 비슷한 도구 사이의 trade-off (면접 점수가 나는 곳)
5. **자가 점검** — 클릭해서 답을 여는 질문
6. **연습 연결** — 이미 만든 문제 은행/노트로 가는 길

```check
Q: 메일의 네 주제 중 "실제로 코드를 짜게 될 가능성이 가장 높은" 것은 무엇이고, 그 근거는?
A: Thread safety + synchronization, 구체적으로는 bounded blocking queue(mutex + condition variable)다. 메일이 thread safety와 mutex·condition variable을 나란히 적었고, 이 셋을 한 문제로 모두 확인할 수 있는 가장 표준적인 과제가 bounded queue이기 때문이다. Don이 예전에 정리한 Verkada 연습 문제(AC42 출입 이벤트 큐)도 같은 유형이다.
Q: 채점 기준 네 가지 중 "코드가 맞다"와 무관하게 점수가 갈리는 것 두 가지는?
A: 설계 결정과 trade-off의 정당화, 그리고 커뮤니케이션의 명료함. 똑같이 맞는 코드를 써도 "왜 이렇게 했는지"를 말하지 않으면 이 두 항목에서 점수를 받지 못한다.
```

> **연습 연결**: 이 장은 읽기만 하면 된다. 전체 준비 자료의 지도는 [빈출 10문제 문서](verkada_concurrency_top10.html)의 0장, 회사·팀 맥락은 [컨텍스트 파일](verkada_sr_embedded_linux_engineer_connectivity_context.html)에 있다.

---

## 1장. 기초 모델 — 스레드와 "동시에"의 의미

### 1.1 왜 필요한가

동시성 버그가 어려운 이유는 **코드를 읽어서는 보이지 않기 때문**이다. 한 줄씩 순서대로 실행된다고 믿고 읽으면 모든 코드가 맞아 보인다. 문제는 여러 흐름이 **서로의 줄 사이에 끼어든다**는 데 있다. 그 끼어듦이 어디서 가능한지 머릿속에 그림이 있어야 버그가 보인다. 이 장은 그 그림을 만든다.

### 1.2 프로세스와 스레드

**프로세스**는 자기만의 주소 공간(메모리 맵)을 가진 실행 단위다. 프로세스 A의 포인터 `0x1000`과 프로세스 B의 `0x1000`은 서로 다른 물리 메모리다. 그래서 프로세스끼리는 기본적으로 메모리를 공유하지 않는다.

**스레드**는 한 프로세스 안에서 **주소 공간을 공유하면서** 따로 도는 실행 흐름이다. 스레드마다 따로 갖는 것은 딱 세 가지다.

- **PC(program counter)** — 지금 어느 명령을 실행 중인지
- **레지스터** — 계산 중인 값
- **스택** — 지역 변수와 함수 호출 기록

나머지 — 전역 변수, `static` 변수, `malloc`으로 잡은 힙, 열린 파일 — 는 **전부 공유**된다. 동시성의 모든 문제는 이 "공유"에서 나온다.

```
프로세스 (하나의 주소 공간)
┌──────────────────────────────────────────────┐
│  코드 · 전역 변수 · static · 힙 · 열린 fd      │  ← 모든 스레드가 공유
│                                              │
│   스레드 1        스레드 2        스레드 3      │
│  ┌────────┐     ┌────────┐     ┌────────┐    │
│  │ PC     │     │ PC     │     │ PC     │    │  ← 각자 따로
│  │ 레지스터 │     │ 레지스터 │     │ 레지스터 │    │
│  │ 스택    │     │ 스택    │     │ 스택    │    │
│  └────────┘     └────────┘     └────────┘    │
└──────────────────────────────────────────────┘
```

### 1.3 베어메탈 · RTOS · Linux — 같은 개념, 다른 이름

| | 베어메탈 (Don의 SSD 경험) | FreeRTOS (센서 MCU) | Embedded Linux (게이트웨이) |
|---|---|---|---|
| 실행 흐름 | main loop + ISR | task | thread (pthread) |
| 끼어드는 주체 | 인터럽트 | 스케줄러(선점) + ISR | 커널 스케줄러(선점) + 멀티코어 |
| 짧은 보호 | 인터럽트 disable | `taskENTER_CRITICAL`, mutex | `pthread_mutex_t` |
| 기다리기 | 플래그 폴링 / WFI | queue, semaphore, event group | condition variable |
| 순서 보장 | `__DMB()` 배리어 | 동일 | C11 atomic memory order |

여기서 중요한 차이가 하나 있다. 베어메탈의 ISR은 main을 **끼어들기만** 하고 main이 ISR을 끼어들지는 않는다(비대칭). Linux 스레드는 **서로가 서로를 언제든** 끼어들고, 멀티코어면 **진짜로 같은 순간에** 실행된다(대칭). 그래서 "인터럽트를 끄면 안전하다"는 베어메탈 직관이 Linux에서는 통하지 않는다 — 다른 코어가 같은 변수를 동시에 만지고 있을 수 있다.

### 1.4 "동시에"의 두 가지 뜻

- **동시성(concurrency)**: 여러 흐름이 **번갈아** 진행된다. 코어가 하나여도 스케줄러가 스레드를 수시로 바꿔 끼우면 생긴다.
- **병렬성(parallelism)**: 여러 흐름이 **물리적으로 같은 순간에** 실행된다. 코어가 여럿일 때만 생긴다.

면접에서 이 구분을 말할 일은 드물지만, 설계할 때는 중요하다. 싱글 코어에서 우연히 동작하던 코드가 멀티코어 게이트웨이(Cortex-A 쿼드코어 등)에서 깨지는 이유가 이것이다. **동시성 코드는 항상 "어떤 순서로든 끼어들 수 있다"고 가정하고 짠다.**

### 1.5 스레드를 만들고 기다리기 — POSIX 최소 API

```c
#include <pthread.h>

static void *worker(void *arg)        /* 스레드가 실행할 함수 */
{
    int id = *(int *)arg;
    /* ... 일 ... */
    return NULL;
}

int main(void)
{
    pthread_t t;
    int id = 1;
    pthread_create(&t, NULL, worker, &id);  /* 새 스레드 시작 */
    pthread_join(t, NULL);                  /* 끝날 때까지 기다림 */
    return 0;
}
```

- `pthread_create`는 새 스레드를 만들어 `worker(&id)`를 실행시키고 **즉시 돌아온다.** 두 흐름이 이제부터 동시에 간다.
- `pthread_join`은 그 스레드가 끝날 때까지 **기다린다.** join 없이 `main`이 끝나면 프로세스 전체가 종료되면서 스레드가 중간에 증발한다.
- 빌드할 때 `-pthread` 플래그가 필요하다.

> **주의**: 위 코드에서 `&id`를 넘기는 건 `main`이 join으로 기다리기 때문에 안전하다. 만약 루프 안에서 `pthread_create(&t[i], NULL, worker, &i)`처럼 루프 변수의 주소를 넘기면, 스레드가 읽기 전에 `i`가 바뀌어 여러 스레드가 같은 값을 본다. 공유는 이렇게 **의도하지 않은 곳에서도** 생긴다.

### 1.6 왜 어려운가 — 끼어들기 조합의 폭발

두 스레드가 각각 3단계를 실행하면, 가능한 실행 순서는 6!/(3!·3!) = **20가지**다. 단계가 10개씩이면 18만 가지가 넘는다. 테스트로 모든 순서를 확인하는 것은 불가능하고, 버그는 그중 **몇 개의 순서에서만** 나타난다. 그래서 동시성 코드는 "테스트해서 믿는" 게 아니라 **"구조적으로 틀릴 수 없게 짜고, 테스트는 그 구조를 확인하는"** 방식으로 다룬다. 2장부터가 그 구조를 만드는 방법이다.

```check
Q: 스레드끼리 공유하지 않는 세 가지는? 그리고 지역 변수는 항상 안전한가?
A: PC, 레지스터, 스택. 지역 변수는 스택에 있으므로 기본적으로 스레드 전용이다. 하지만 그 지역 변수의 **주소를 다른 스레드에 넘기면** 공유가 된다(예: pthread_create에 &i를 넘기는 경우). "지역 변수라서 안전"이 아니라 "주소가 새지 않아서 안전"이다.
Q: 베어메탈에서 "인터럽트를 끄고 공유 변수를 갱신하면 안전하다"는 방법이 Linux 멀티스레드에서 통하지 않는 이유는?
A: 인터럽트 disable은 **현재 코어**에서 끼어들기를 막을 뿐이다. 멀티코어에서는 다른 코어의 스레드가 같은 순간에 같은 변수를 만질 수 있다. 게다가 사용자 공간에서는 인터럽트를 끌 수도 없다. 그래서 모든 코어가 존중하는 뮤텍스나 atomic 연산이 필요하다.
Q: 싱글 코어에서 수천 번 테스트해도 멀쩡하던 코드가 멀티코어에서 깨지는 이유 두 가지는?
A: (1) 멀티코어에서는 진짜 병렬 실행이 일어나 싱글 코어에서는 드물던 끼어들기 순서가 흔해진다. (2) 코어마다 캐시와 쓰기 버퍼가 있어서, 한 코어가 쓴 값이 다른 코어에 **보이는 순서**가 프로그램 순서와 달라질 수 있다(5장의 memory ordering 문제).
```

> **연습 연결**: [기초 노트 00 — 동시성 기초 총정리](notes_site/basics/00_start_here.html)의 §1이 이 장을 더 자세히 다룬다.

---

## 2장. Thread safety — race, 불변식, 임계구역

### 2.1 왜 필요한가

메일의 첫 주제다. "thread safe하다"는 말은 흔하지만 정확한 정의를 말할 수 있는 사람은 적다. 이 장의 목표는 이 한 문장을 **이유와 함께** 말할 수 있게 되는 것이다.

> **Thread-safe**: 여러 스레드가 **어떤 순서로 끼어들어도** 그 코드가 약속한 불변식이 깨지지 않고, 단일 스레드에서 실행한 것과 같은 의미의 결과를 내는 것.

### 2.2 가장 작은 race — `counter++`

C 소스에서는 한 줄이지만 CPU에서는 세 단계다.

```
load   r0 <- [counter]     ; 메모리에서 레지스터로 읽기
add    r0 <- r0 + 1        ; 레지스터에서 더하기
store  [counter] <- r0     ; 레지스터를 메모리에 쓰기
```

두 스레드가 동시에 `counter++`를 하면 이런 순서가 가능하다. 처음 `counter = 5`.

```
시간 →   스레드 A                     스레드 B
  1     load  r0 <- 5
  2                                  load  r0 <- 5
  3     add   r0 <- 6
  4                                  add   r0 <- 6
  5     store counter <- 6
  6                                  store counter <- 6      ← 결과 6 (7이어야 함)
```

증가 두 번이 한 번으로 **사라졌다.** 이것을 **lost update**라고 한다. 100만 번씩 돌리면 결과가 매번 다르게, 항상 200만보다 작게 나온다.

### 2.3 두 가지 용어 — data race와 race condition

면접에서 둘을 구분하면 좋은 인상을 준다.

| | Data race | Race condition |
|---|---|---|
| 정의 | 두 스레드가 같은 메모리에 동기화 없이 접근하고, 그중 하나 이상이 쓰기 | 실행 순서(타이밍)에 따라 결과가 달라지는 **논리** 버그 |
| C 표준에서 | **정의되지 않은 동작(UB)** — 컴파일러가 무슨 짓을 해도 된다 | 동작 자체는 정의됨, 결과가 틀릴 뿐 |
| 찾는 도구 | ThreadSanitizer가 잡는다 | 도구가 못 잡는 경우가 많다 |
| 예 | 락 없는 `counter++` | 모든 접근에 락을 걸었지만 "확인"과 "행동" 사이에 락을 놓는 코드 |

두 번째가 더 위험하다. **모든 변수 접근을 락으로 감싸도 race condition은 남을 수 있다.** 다음 절이 그 이유다.

### 2.4 불변식 — 락이 진짜로 지키는 것

**불변식(invariant)**은 "데이터가 항상 만족해야 하는 조건"이다. 예를 들어 링크 통계 구조체가 있다고 하자.

```c
typedef struct {
    unsigned count;     /* 샘플 수 */
    long     sum;       /* 합계 */
    int      min, max;
} LinkStats;
/* 불변식: count == 0 이면 sum == 0,
          count > 0 이면 min <= sum/count <= max */
```

샘플 하나를 추가하려면 네 필드를 모두 바꿔야 한다. 그 **도중에는** 불변식이 잠깐 깨진다(count는 늘었는데 sum은 아직). 다른 스레드가 이 중간 상태를 보면 평균이 min보다 작은 이상한 값을 읽는다.

그래서 이렇게 이해해야 한다.

> **락은 변수를 지키는 게 아니라 불변식을 지킨다.** 불변식이 깨져 있는 구간 전체가 하나의 임계구역이어야 한다.

필드마다 따로 락을 걸거나 필드마다 atomic을 쓰면, 각 필드는 안전해도 **필드 사이의 관계**는 깨진다. 이것이 "atomic이면 다 되는 거 아니야?"에 대한 답이다.

### 2.5 임계구역(critical section)

**임계구역**은 불변식이 잠깐 깨지는 코드 구간이다. 규칙은 두 가지다.

1. **한 번에 한 스레드만** 들어갈 수 있어야 한다(상호 배제).
2. **최대한 짧아야** 한다 — 임계구역 안에 있는 동안 다른 스레드는 기다린다.

두 번째 규칙 때문에 다음은 임계구역 안에서 하지 않는다.

- 파일/소켓 I/O, 로그 출력 — 수 밀리초가 걸릴 수 있다
- 다른 모듈의 콜백 호출 — 그 콜백이 무엇을 할지 모른다(같은 락을 다시 잡으면 데드락)
- 블로킹 대기, `sleep`
- 큰 메모리 할당

**패턴**: 락 안에서는 상태만 바꾸고 필요한 값을 지역 변수로 복사한 뒤, 락을 놓고 나서 I/O를 한다.

### 2.6 Check-then-act — 락을 걸었는데도 깨지는 경우

PoE 포트 두 개가 총 60W 예산을 나눠 쓴다고 하자. 새 장치에 15W를 주려면 "남은 예산 확인 → 차감"을 해야 한다.

```c
/* 잘못된 예 — 각 함수는 안에서 락을 잡는다 */
if (budget_remaining() >= 15)     /* ① 확인 (락 잡고 읽고 놓음) */
    budget_take(15);              /* ② 행동 (락 잡고 빼고 놓음) */
```

①과 ② 사이에 다른 스레드가 끼어들어 예산을 먼저 가져가면, 확인할 때는 충분했던 예산이 행동할 때는 부족하다. 결과는 **예산 초과(over-commit)** — 실제 하드웨어라면 PoE 과부하다. 이것을 **TOCTOU(time-of-check to time-of-use)**라고 한다.

해법은 **확인과 행동을 같은 임계구역**에 넣는 것이다. API 수준에서는 "확인 함수 + 행동 함수"가 아니라 **"조건부 행동 함수" 하나**를 제공한다.

```c
bool budget_reserve(Budget *b, int watts)   /* 확인과 차감이 한 임계구역 */
{
    pthread_mutex_lock(&b->m);
    bool ok = (b->remaining >= watts);
    if (ok) b->remaining -= watts;
    pthread_mutex_unlock(&b->m);
    return ok;
}
```

> **면접 포인트**: "각 함수가 thread-safe해도 **그 함수들의 조합**은 thread-safe하지 않다." 이 문장이 thread safety의 핵심이다.

### 2.7 비슷한 말들 — thread-safe · reentrant · async-signal-safe

| 용어 | 뜻 | 예 |
|---|---|---|
| Thread-safe | 여러 스레드가 동시에 불러도 안전 (내부에서 락을 쓸 수도 있음) | `malloc` |
| Reentrant | 실행 도중에 **같은 함수가 다시 불려도** 안전 — 공유 상태를 아예 안 씀 | `strtok_r` (vs `strtok`는 아님) |
| Async-signal-safe | 시그널 핸들러 / ISR 안에서 불러도 안전 | `write`는 됨, `printf`·`malloc`은 안 됨 |

ISR이나 시그널 핸들러에서 락을 쓰는 함수(`malloc`, `printf`)를 부르면, 그 락을 이미 쥐고 있던 코드를 끼어든 상태이므로 **자기 자신과 데드락**에 빠진다. 그래서 ISR에서는 플래그를 세우거나 lock-free 링버퍼에 넣는 것만 한다(5장).

### 2.8 공유를 줄이는 것이 최고의 전략

락을 잘 거는 것보다 **락이 필요 없게 설계하는 것**이 더 좋다. 방법은 네 가지다.

| 전략 | 방법 | 예 |
|---|---|---|
| 불변(immutable) 객체 | 만든 뒤 절대 안 바꾼다. 바꿀 땐 새로 만들어 포인터만 교체 | 설정 객체 교체 (5장 끝) |
| 소유권 이전 | 데이터를 큐로 **넘기면** 보낸 쪽은 더 이상 만지지 않는다 | 이벤트 큐 (4장) |
| 스레드 한정(confinement) | 어떤 자원은 **한 스레드만** 만진다. 다른 스레드는 메시지로 요청 | 모뎀 제어 스레드 (8장) |
| 스레드 로컬 | 각자 따로 세고 마지막에 합친다 | 스레드별 통계 카운터 |

Verkada 게이트웨이 같은 시스템에서 이 네 가지를 먼저 적용하고, **남은 공유에만** 락을 거는 것이 좋은 설계다. 면접에서 "락을 어디에 걸까요?"보다 "공유를 어떻게 줄일까요?"로 시작하면 한 단계 위의 답이 된다.

### 2.9 어떻게 찾나 — 도구와 방법

- **ThreadSanitizer(TSan)**: `-fsanitize=thread`로 빌드해 실행하면 data race를 스택 트레이스와 함께 알려준다. 동시성 코드를 쓰면 CI에 넣는 게 기본이다.
- **불변식 assert**: 임계구역 끝에서 불변식을 검사한다. 디버그 빌드에서만 켜도 버그를 일찍 잡는다.
- **스트레스 테스트**: 작은 버퍼, 많은 스레드, 많은 반복으로 드문 순서를 자주 만든다. 결과는 **정확한 값이 아니라 불변식**으로 검사한다(예: 만든 개수 = 소비 + 버림 + 남은 것).
- **로그**: 스레드 ID와 시퀀스 번호를 같이 찍는다.

```check
Q: data race와 race condition의 차이를 한 문장씩. 어느 쪽이 도구로 잡기 어려운가?
A: data race는 동기화 없는 동시 접근(하나 이상 쓰기)으로 C 표준상 UB이며 TSan이 잡는다. race condition은 타이밍에 따라 결과가 달라지는 논리 버그로, 모든 접근이 락으로 보호돼도 생길 수 있어서 도구로 잡기 어렵다.
Q: LinkStats의 네 필드를 각각 atomic으로 만들면 thread-safe한가?
A: 아니다. 각 필드의 읽기·쓰기는 원자적이 되지만, 필드 사이의 관계(불변식)는 여전히 깨진다. 샘플 추가 도중에 다른 스레드가 count는 새 값, sum은 옛 값으로 읽을 수 있다. 여러 필드에 걸친 불변식은 하나의 임계구역(뮤텍스)으로 지켜야 한다.
Q: budget_remaining()과 budget_take()가 둘 다 내부에서 뮤텍스를 쓴다. 이 둘을 연달아 부르는 코드는 왜 여전히 틀렸나? 고친 API는 어떤 모양이어야 하나?
A: 두 호출 사이에 락이 풀리므로 그 틈에 다른 스레드가 예산을 가져갈 수 있다(TOCTOU). 확인과 행동을 한 임계구역에서 하는 조건부 API, 예를 들어 bool budget_reserve(Budget *b, int watts)를 제공해야 한다.
Q: 임계구역 안에서 로그를 출력하면 안 되는 이유 두 가지는?
A: (1) 로그 출력은 I/O라서 느리고 가변적이다 — 그동안 다른 스레드가 전부 기다려 처리량과 지연이 나빠진다. (2) 로깅 모듈이 내부에서 다른 락을 잡으면 락 순서 문제로 데드락이 생길 수 있다. 필요한 값을 지역 변수로 복사하고 락을 놓은 뒤 출력한다.
Q: 락을 거는 것보다 먼저 고려할 네 가지 전략은?
A: 불변 객체(바꿀 땐 새로 만들어 교체), 소유권 이전(큐로 넘기면 보낸 쪽은 안 만짐), 스레드 한정(한 스레드만 자원을 소유), 스레드 로컬(각자 모았다가 합침).
```

> **연습 연결**: 문제 은행 [01 스레드 & 뮤텍스 기초](verkada_prep/index.html) (Q1 race 재현, Q3 불변식, Q4 임계구역 최소화, Q6 TOCTOU). 터미널에서 `cd verkada_prep && make prob N=01_threads_mutex`. 복습용 요약은 [복습 노트 01](notes_site/review/01_threads_mutex.html).

---

## 3장. 동기화 도구 ① 뮤텍스 (mutex)

### 3.1 왜 필요한가

2장에서 "임계구역에는 한 번에 한 스레드만"이라고 했다. 그걸 강제하는 가장 기본적인 도구가 **뮤텍스(mutual exclusion lock)**다. 베어메탈의 "인터럽트 끄기"에 해당하지만, 모든 코어와 모든 스레드가 존중한다는 점이 다르다.

### 3.2 개념 — 열쇠가 하나뿐인 방

화장실 열쇠가 하나뿐인 카페를 생각하면 된다. 열쇠를 가진 사람만 들어갈 수 있고, 나머지는 열쇠가 돌아올 때까지 **줄을 서서 잔다(블록된다).** 열쇠를 가져간 사람이 반드시 돌려놔야 한다.

```c
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;   /* 정적 초기화 */
static long counter;

void inc(void)
{
    pthread_mutex_lock(&m);      /* 열쇠 획득 — 다른 스레드가 쥐고 있으면 여기서 잠든다 */
    counter++;                   /* 임계구역: 이제 한 번에 한 스레드만 */
    pthread_mutex_unlock(&m);    /* 열쇠 반납 — 기다리던 스레드 하나가 깨어난다 */
}
```

규칙은 세 가지다.

1. **잠근 스레드가 푼다.** 다른 스레드가 대신 풀면 안 된다(뮤텍스에는 "주인"이 있다).
2. **모든 경로에서 푼다.** 중간에 `return`하는 에러 경로가 있으면 거기서도 풀어야 한다. C에서는 `goto out` 패턴으로 출구를 하나로 모은다.
3. **같은 스레드가 두 번 잠그지 않는다.** 기본 뮤텍스는 자기 자신을 기다리며 영원히 멈춘다.

뮤텍스는 락/언락 외에 한 가지 일을 더 한다. **unlock 이전에 쓴 모든 메모리가, 같은 뮤텍스를 lock한 다른 스레드에게 보이도록 보장한다.** 그래서 뮤텍스로 보호된 데이터는 5장의 memory order를 따로 신경 쓰지 않아도 된다. 뮤텍스가 그 일을 대신 해 준다.

### 3.3 락의 크기(granularity) — trade-off의 첫 번째 예

| | 큰 락 (coarse-grained) | 작은 락 여러 개 (fine-grained) |
|---|---|---|
| 방식 | 모듈 전체에 뮤텍스 하나 | 자료구조·필드 그룹마다 따로 |
| 장점 | 단순, 데드락 걱정 없음, 불변식 지키기 쉬움 | 병렬성 높음, 경합 적음 |
| 단점 | 경합이 심하면 느림 | 여러 락을 함께 잡을 때 데드락 위험, 불변식이 락 경계를 넘으면 깨짐 |
| 언제 | **기본값.** 측정해서 병목일 때만 쪼갠다 | 프로파일링으로 경합이 확인된 핫스팟 |

> **면접 문장**: "I'd start with one lock per data structure and only split it if profiling shows contention — splitting locks trades simplicity and deadlock-safety for throughput, and I want evidence before paying that cost."

### 3.4 데드락 — 락을 두 개 이상 쓰는 순간 생기는 문제

스레드 A가 락1을 쥐고 락2를 기다리고, 스레드 B가 락2를 쥐고 락1을 기다리면 둘 다 영원히 멈춘다. 데드락은 **네 조건이 모두** 성립할 때만 생긴다(Coffman 조건).

| 조건 | 뜻 | 깨는 법 |
|---|---|---|
| 상호 배제 | 자원을 한 번에 하나만 쓴다 | (뮤텍스의 본질이라 못 깸) |
| 점유 대기 | 하나를 쥔 채 다른 걸 기다린다 | 필요한 락을 한 번에 다 잡거나, 못 잡으면 다 놓기 |
| 비선점 | 남이 쥔 락을 뺏을 수 없다 | trylock 실패 시 자기 락을 스스로 놓기 |
| **순환 대기** | A→B→A 식으로 기다림이 원을 이룬다 | **전역 락 순서**를 정한다 ← 실무 해법 |

**실무 해법은 락 순서다.** "락은 항상 ID(또는 주소)가 작은 것부터 잡는다"는 규칙 하나면 원이 생길 수 없다.

```c
/* 어느 방향으로 불러도 항상 같은 순서로 잡는다 */
Port *first  = (a->id < b->id) ? a : b;
Port *second = (a->id < b->id) ? b : a;
pthread_mutex_lock(&first->m);
pthread_mutex_lock(&second->m);
/* ... 두 포트의 예산을 옮긴다 ... */
pthread_mutex_unlock(&second->m);
pthread_mutex_unlock(&first->m);
```

순서를 정할 수 없을 때(외부 콜백 등)는 **trylock + 백오프**를 쓴다. 두 번째 락을 `pthread_mutex_trylock`으로 시도하고, 실패하면 **첫 번째 락도 놓고** 잠깐 쉰 뒤 처음부터 다시 한다. 쉬는 시간에 무작위성을 넣지 않으면 두 스레드가 똑같이 양보만 하다 진전이 없는 **라이브락(livelock)**이 된다.

### 3.5 우선순위 역전 — 임베디드 면접의 단골

RTOS에서 우선순위가 다른 세 태스크가 있다고 하자.

```
L(낮음)이 뮤텍스를 쥔다
H(높음)가 깨어나 같은 뮤텍스를 기다린다   → H는 L이 끝나길 기다림
M(중간)이 깨어난다                        → M이 L보다 높으니 L을 선점
                                          → L이 못 돌아서 뮤텍스를 못 놓음
                                          → 결과: M이 H를 사실상 막고 있다
```

H는 자기와 아무 관계 없는 M 때문에 무한정 기다린다. 1997년 **Mars Pathfinder**가 이 문제로 반복 리셋됐다. 해법은 **우선순위 상속(priority inheritance)**이다. H가 기다리는 동안 L의 우선순위를 H만큼 올려서, M이 L을 선점하지 못하게 한다.

- **FreeRTOS**: `xSemaphoreCreateMutex()`로 만든 뮤텍스는 우선순위 상속을 지원한다. `xSemaphoreCreateBinary()`로 만든 바이너리 세마포어는 **지원하지 않는다.** 상호 배제에 바이너리 세마포어를 쓰면 안 되는 이유다.
- **Linux**: `pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT)`.
- **근본 해법**: 공유 자원을 **한 태스크만 소유**하게 하고 나머지는 메시지 큐로 요청한다(2.8의 스레드 한정). 락이 없으니 역전도 없다.

### 3.6 뮤텍스의 친척들 — 언제 무엇을

| 도구 | 특징 | 쓸 때 | 피할 때 |
|---|---|---|---|
| mutex | 기다리는 동안 잠든다 | **기본값** | — |
| rwlock | 읽기는 여럿 동시, 쓰기는 혼자 | 읽기가 압도적이고 임계구역이 긴 경우 | 임계구역이 짧으면 오히려 느림(관리 비용), writer 기아 주의 |
| spinlock | 기다리는 동안 돌며 확인(안 잠듦) | 커널/ISR 근처, 임계구역이 매우 짧고 멀티코어일 때 | 사용자 공간 일반 코드, 싱글 코어(쥔 스레드가 못 돌아서 계속 돈다) |
| recursive mutex | 같은 스레드가 여러 번 잠글 수 있음 | 거의 없음 | 설계 냄새 — "내가 지금 락을 쥐고 있나?"를 모르는 코드라는 뜻. 대신 `_locked` 접미사 내부 함수로 분리 |
| binary semaphore | 주인 없는 0/1 신호 | ISR → 태스크 **신호 전달** | 상호 배제(주인 개념·우선순위 상속이 없음) |

```check
Q: 뮤텍스가 "한 번에 한 스레드"를 보장하는 것 외에 하는 일 하나는? 그게 왜 중요한가?
A: unlock 이전에 쓴 메모리가 같은 뮤텍스를 lock한 스레드에게 보이도록 보장한다(메모리 가시성과 순서). 덕분에 뮤텍스로 보호하는 데이터는 atomic이나 배리어를 따로 쓸 필요가 없다.
Q: 데드락의 네 조건과, 그중 실무에서 깨는 조건과 방법은?
A: 상호 배제, 점유 대기, 비선점, 순환 대기. 실무에서는 순환 대기를 깬다 — 모든 코드가 락을 전역으로 정한 같은 순서(ID나 주소 순)로 잡게 한다.
Q: trylock + 백오프에서 두 가지 필수 요소는?
A: (1) 두 번째 락을 못 잡으면 첫 번째 락도 반드시 놓는다(쥔 채 재시도하면 여전히 데드락). (2) 재시도 전 대기에 무작위성을 넣는다(없으면 두 스레드가 같은 박자로 양보만 하는 라이브락).
Q: 우선순위 역전을 설명하고, FreeRTOS에서 상호 배제에 바이너리 세마포어를 쓰면 안 되는 이유를 말하라.
A: 낮은 우선순위 태스크가 락을 쥔 상태에서 중간 우선순위 태스크가 그것을 선점하면, 락을 기다리는 높은 우선순위 태스크가 무관한 중간 태스크 때문에 무한정 기다린다. FreeRTOS의 mutex는 우선순위 상속으로 이를 막지만 바이너리 세마포어는 주인 개념이 없어 상속을 지원하지 않는다.
Q: recursive mutex가 필요해 보이는 상황은 어떻게 고치나?
A: 공개 함수는 락을 잡고, 실제 일은 락이 이미 잡혀 있다고 가정하는 내부 함수(이름에 _locked 접미사)에 맡긴다. 공개 함수끼리 서로 부르지 않고 내부 함수를 부르게 하면 중첩 잠금이 사라진다.
```

> **연습 연결**: 문제 은행 [01 스레드 & 뮤텍스](verkada_prep/index.html)의 Q5(중첩 락), Q9(rwlock), [기초 노트 09 — 락 순서](notes_site/basics/09_lock_order.html). 직접 실행: `cd concurrency_practice && make sol N=09` (반대 방향 5만 번씩 이전해도 데드락 없이 총합 보존).

---

## 4장. 동기화 도구 ② 조건 변수 (condition variable)

### 4.1 왜 필요한가

뮤텍스는 "동시에 들어오지 마"만 말할 수 있다. 하지만 동시성 코드의 절반은 **"어떤 조건이 될 때까지 기다려"**다. 큐가 비었으면 뭔가 들어올 때까지, 가득 찼으면 자리가 날 때까지 기다려야 한다.

뮤텍스만으로 기다리면 이렇게 된다.

```c
/* 나쁜 예: busy-wait */
for (;;) {
    pthread_mutex_lock(&q->m);
    if (q->count > 0) break;          /* 들어왔으면 탈출 (락 쥔 채로) */
    pthread_mutex_unlock(&q->m);      /* 아니면 놓고 다시 확인 */
}
```

CPU를 100% 태우고(태양광 트레일러의 배터리!), 락을 계속 잡았다 놓아서 생산자까지 방해한다. 필요한 것은 **"조건이 바뀌면 깨워 줘, 그때까지 잘게"**다. 그게 조건 변수다.

### 4.2 개념 — 대기실과 호출 벨

조건 변수는 **조건에 딸린 대기실**이다. 기다리는 스레드는 대기실에서 잠들고, 상태를 바꾼 스레드가 벨을 눌러 깨운다. 조건 변수 자체는 아무 상태도 없다 — 진짜 상태(`count`)는 뮤텍스가 지키는 변수에 있고, 조건 변수는 **기다리는 스레드 목록**일 뿐이다.

그래서 조건 변수는 **항상 뮤텍스와 한 쌍**으로 쓴다.

### 4.3 wait의 비밀 — "놓고 자기"가 하나의 동작이다

```c
pthread_mutex_lock(&q->m);
while (q->count == 0)                       /* 조건 확인은 락 안에서 */
    pthread_cond_wait(&q->not_empty, &q->m);
/* 여기 오면 락을 쥔 채로 count > 0 이 보장된다 */
```

`pthread_cond_wait`는 세 가지를 한다.

1. 뮤텍스를 **놓고**, 동시에 대기실에서 **잠든다** — 이 두 개가 **원자적**이다.
2. 누군가 벨을 누르면 깨어난다.
3. 돌아오기 전에 뮤텍스를 **다시 잡는다.**

1번이 원자적이라는 게 핵심이다. 만약 "놓고" "잠드는" 사이에 틈이 있으면, 그 틈에 생산자가 데이터를 넣고 벨을 누를 수 있다. 소비자는 아직 대기실에 없으니 벨 소리를 못 듣고, 그 뒤에 잠들어서 **영원히 안 깨어난다.** 이것을 **lost wakeup**이라고 하고, 조건 변수가 뮤텍스를 인자로 받는 이유가 바로 이것이다.

### 4.4 왜 `if`가 아니라 `while`인가 — 면접 1순위 질문

깨어났다고 해서 조건이 참이라는 보장이 없다. 이유는 세 가지다.

| 이유 | 무슨 일이 생기나 |
|---|---|
| **가짜 기상(spurious wakeup)** | POSIX는 아무도 벨을 안 눌렀는데 깨어나는 것을 허용한다(구현 효율 때문) |
| **가로채기(stolen wakeup)** | 벨 소리에 깨어나 락을 다시 잡는 사이, 다른 소비자가 먼저 들어와 데이터를 가져갔다 |
| **broadcast** | 여럿을 한꺼번에 깨웠지만 데이터는 하나뿐이다 |

셋 다 "깨어났는데 조건은 거짓"인 상황이다. `if`로 쓰면 빈 큐에서 pop해서 쓰레기 값을 읽는다. `while`로 쓰면 다시 확인하고 다시 잔다.

> **면접 문장**: "I always wait in a while loop that re-checks the predicate — spurious wakeups are allowed by POSIX, and even a real wakeup doesn't guarantee the condition still holds by the time I re-acquire the lock."

### 4.5 signal과 broadcast

| | `pthread_cond_signal` | `pthread_cond_broadcast` |
|---|---|---|
| 깨우는 수 | 대기 중인 스레드 **최소 하나** | 대기 중인 스레드 **전부** |
| 쓸 때 | 조건을 만족시킬 수 있는 게 하나뿐일 때 (아이템 1개 push → 소비자 1명) | 모두가 상태를 다시 봐야 할 때 — **종료**, 설정 변경, 조건이 대기자마다 다를 때 |
| 잘못 쓰면 | 종료 시 signal만 쓰면 나머지 대기자는 영원히 잔다 | 매번 broadcast하면 전부 깨어났다가 대부분 다시 잔다(thundering herd, 성능 낭비) |

signal을 unlock **전**에 부르든 **후**에 부르든 둘 다 정확하다. 단, **상태 변경은 반드시 락 안에서** 해야 한다. unlock 후에 signal하면 깨어난 스레드가 바로 락을 잡을 수 있어 약간 효율적이다.

### 4.6 전체 예제 — bounded blocking queue

메일이 사실상 예고한 문제다. 이 코드를 **백지에서 45분 안에** 쓸 수 있어야 한다. 구조는 뮤텍스 하나와 조건 변수 둘이다. 조건 변수가 둘인 이유는 "비어서 기다리는 소비자"와 "가득 차서 기다리는 생산자"가 서로 다른 조건을 기다리기 때문이다.

```c
typedef struct {
    AccessEvent    *buf;
    size_t          cap, head, tail, count;
    bool            closed;
    pthread_mutex_t m;
    pthread_cond_t  not_full, not_empty;
} BQueue;

bool bq_push(BQueue *q, AccessEvent ev)
{
    pthread_mutex_lock(&q->m);
    /* while: spurious wakeup + 여러 생산자가 동시에 깨는 경우 방어 */
    while (q->count == q->cap && !q->closed)
        pthread_cond_wait(&q->not_full, &q->m);

    if (q->closed) {                      /* close 이후 push는 거부 */
        pthread_mutex_unlock(&q->m);
        return false;
    }
    q->buf[q->tail] = ev;
    q->tail = (q->tail + 1) % q->cap;
    q->count++;
    pthread_mutex_unlock(&q->m);          /* unlock 먼저 → 깨어난 소비자가 곧장 lock 획득 */
    pthread_cond_signal(&q->not_empty);
    return true;
}

bool bq_pop(BQueue *q, AccessEvent *out)
{
    pthread_mutex_lock(&q->m);
    while (q->count == 0 && !q->closed)
        pthread_cond_wait(&q->not_empty, &q->m);

    if (q->count == 0) {                  /* closed && empty → 종료 신호 */
        pthread_mutex_unlock(&q->m);
        return false;
    }
    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    pthread_mutex_unlock(&q->m);
    pthread_cond_signal(&q->not_full);
    return true;
}

void bq_close(BQueue *q)
{
    pthread_mutex_lock(&q->m);
    q->closed = true;
    pthread_mutex_unlock(&q->m);
    /* 대기 중인 모든 스레드를 깨워야 한다 → signal이 아니라 broadcast */
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
}
```

한 줄씩 왜 있는지 확인하자.

- `count` 필드: `head == tail`이 빈 것인지 가득 찬 것인지 모호한 문제를 없앤다.
- `while (... && !q->closed)`: 기다리는 조건에 **종료도 포함**한다. 안 그러면 종료 신호를 받아도 다시 잠든다.
- push에서 `closed` 확인: 종료된 큐에 넣으면 영원히 아무도 안 꺼낸다 → 거부하고 호출자에게 알린다.
- pop의 `count == 0` 확인: 종료됐어도 **남은 항목은 먼저 다 꺼내 준다**(drain). 비었을 때만 false.
- close의 두 broadcast: 소비자와 생산자 **양쪽 대기실 모두** 깨운다.

### 4.7 종료 프로토콜 — edge case의 대부분이 여기 있다

동시성 코드의 버그는 정상 동작보다 **멈출 때** 훨씬 많이 난다. 순서를 외워 두자.

1. **종료 플래그를 락 안에서** 세운다 (`closed = true`).
2. **모든 조건 변수에 broadcast** — 잠든 스레드 전부를 깨운다.
3. 깨어난 스레드는 while 조건에서 종료를 보고 **스스로 빠져나온다.**
4. 만든 쪽이 **모든 스레드를 join**한다.
5. join이 끝난 **후에만** 자원을 해제한다(destroy, free). 먼저 해제하면 아직 도는 스레드가 해제된 메모리를 만진다(use-after-free).

이중 close는 **멱등(idempotent)**하게 — 두 번 불러도 한 번 부른 것과 같게 — 만든다.

### 4.8 기다리되 무한정은 아니게 — timed wait

"최대 500ms만 기다려"가 필요하면 `pthread_cond_timedwait`를 쓴다. 함정이 하나 있다. 이 함수의 타임아웃은 기본적으로 **벽시계(CLOCK_REALTIME)** 기준이다. NTP가 시각을 앞뒤로 조정하면 대기 시간이 늘어나거나 줄어든다.

- **Linux**: `pthread_condattr_setclock(&attr, CLOCK_MONOTONIC)`으로 단조 시계를 쓰게 한다.
- **macOS**: 그 API가 없어서 상대 시간 버전 `pthread_cond_timedwait_relative_np`를 쓴다.
- 어느 쪽이든 **깨어날 때마다 남은 시간을 다시 계산**한다. 가짜 기상이 타임아웃을 연장하면 안 된다.

### 4.9 세마포어와 비교

**카운팅 세마포어**는 "남은 개수"를 가진 도구다. `wait`는 개수를 하나 줄이고(0이면 잠듦), `post`는 하나 늘린다. "동시에 업로드는 최대 3개"처럼 **개수 제한**에 딱 맞는다. 조건 변수는 더 일반적이어서 **어떤 조건이든** 기다릴 수 있다. 사실 세마포어는 뮤텍스 + 조건 변수 + 카운터로 직접 만들 수 있다(문제 은행 Q20).

C++이라면 `std::condition_variable::wait(lock, [&]{ return count > 0; })`처럼 predicate를 넘기면 while 루프가 내장된다. 면접에서 C++로 쓴다면 이 형태를 쓰고 "while과 같다"고 설명하면 된다.

```check
Q: pthread_cond_wait가 인자로 뮤텍스를 받는 이유는? 그 뮤텍스로 무엇을 하나?
A: "뮤텍스 놓기"와 "잠들기"를 하나의 원자적 동작으로 만들기 위해서다. 둘 사이에 틈이 있으면 그 틈에 상태가 바뀌고 벨이 울려도 아직 잠들지 않은 스레드가 못 듣는다(lost wakeup). 깨어나면 반환 전에 다시 잡는다.
Q: while 대신 if로 쓰면 깨지는 상황 세 가지는?
A: 가짜 기상(아무도 안 깨웠는데 깸), 가로채기(깨어나 락을 다시 잡는 사이 다른 스레드가 조건을 소비), broadcast로 여럿이 깨었지만 조건을 만족하는 건 하나뿐인 경우.
Q: bounded queue에서 조건 변수를 두 개 쓰는 이유는? 하나로 하면 어떻게 되나?
A: 소비자는 "비지 않음", 생산자는 "가득 차지 않음"이라는 서로 다른 조건을 기다린다. 하나로 합치면 signal이 엉뚱한 쪽(예: 생산자가 생산자)을 깨울 수 있어 진전이 멈추거나, 그걸 피하려고 매번 broadcast해야 해서 비효율적이다.
Q: 종료 시 signal이 아니라 broadcast여야 하는 이유는? 종료 후 pop은 무엇을 반환해야 하나?
A: 종료는 잠든 **모든** 스레드가 알아야 하는 상태 변화다. signal은 하나만 깨우므로 나머지는 영원히 잠든다. 종료 후 pop은 남은 항목이 있으면 계속 돌려주고(drain), 비었을 때 false를 반환해 소비자 루프를 끝낸다.
Q: 종료 프로토콜 5단계를 순서대로.
A: (1) 락 안에서 종료 플래그 세팅 (2) 모든 조건 변수 broadcast (3) 대기자가 while 조건에서 종료를 보고 스스로 탈출 (4) 모든 스레드 join (5) join 후에만 자원 해제.
Q: pthread_cond_timedwait의 기본 시계 함정은? Linux에서의 해법은?
A: 기본이 CLOCK_REALTIME이라 NTP 시각 조정에 따라 대기가 늘거나 준다. Linux에서는 pthread_condattr_setclock으로 CLOCK_MONOTONIC을 지정한다. 그리고 깨어날 때마다 남은 시간을 다시 계산한다.
```

> **연습 연결**: ★ 가장 중요한 연습. 문제 은행 [02 조건 변수 & 블로킹 큐](verkada_prep/index.html) — Q11~Q20 전부(`make prob N=02_condvar_queues`). 한 문제를 처음부터 끝까지 짜 보려면 `cd concurrency_practice && make run N=01`. 해설은 [기초 노트 01](notes_site/basics/01_bounded_queue.html), 종료 edge case는 [기초 노트 06 — 워커 풀](notes_site/basics/06_thread_pool.html).

---

## 5장. 동기화 도구 ③ Atomic과 메모리 순서

### 5.1 왜 필요한가

뮤텍스는 강력하지만 두 가지 한계가 있다. **잠들 수 있어서 ISR에서 못 쓰고**, 아주 작은 데이터(카운터 하나, 플래그 하나)에는 **비용이 과하다.** 이럴 때 쓰는 것이 atomic 연산이다. 그런데 atomic을 제대로 쓰려면 "메모리 순서"라는 새로운 개념을 알아야 한다. 이 장이 이 가이드에서 가장 추상적이지만, Don의 멀티코어 Cortex-R 경험(배리어)과 직접 연결된다.

### 5.2 공유 데이터의 세 가지 문제

| 문제 | 뜻 | 예 |
|---|---|---|
| **원자성(atomicity)** | 연산이 중간에 끊기지 않는가 | `counter++`가 load/add/store로 쪼개져 끼어들기 당함 (2장) |
| **가시성(visibility)** | 한 스레드가 쓴 값이 다른 스레드에 보이는가 | 컴파일러가 변수를 레지스터에 캐시해서 루프가 영원히 옛 값을 봄 |
| **순서(ordering)** | 쓴 순서대로 다른 스레드에 보이는가 | "데이터 쓰고 → 플래그 세움"인데 다른 코어에는 플래그가 먼저 보임 |

뮤텍스는 이 셋을 모두 해결한다. atomic은 **원자성은 항상** 해결하고, **가시성과 순서는 memory order 인자로 선택**하게 한다.

### 5.3 volatile은 왜 답이 아닌가

`volatile`은 컴파일러에게 "이 변수를 읽고 쓸 때마다 **실제로 메모리에 접근해라**, 레지스터에 캐시하거나 생략하지 마라"고 말한다. 그 용도는 **메모리 매핑된 하드웨어 레지스터(MMIO)**와 시그널 핸들러 플래그(`volatile sig_atomic_t`)다.

스레드 동기화에 부족한 이유는 세 가지다.

1. **원자성 없음** — `volatile int x; x++`도 여전히 load/add/store 세 단계다.
2. **CPU 재배치를 막지 않음** — 컴파일러만 막을 뿐, CPU가 쓰기 순서를 바꾸는 것(다른 코어에 보이는 순서)은 그대로다.
3. **다른 일반 변수와의 순서를 보장하지 않음** — volatile 변수끼리만 컴파일러 순서가 유지된다.

> **면접 문장**: "volatile only stops the compiler from caching or eliding accesses — it gives no atomicity and no inter-thread ordering. For thread synchronization I use C11 atomics or a mutex; volatile is for memory-mapped registers."

### 5.4 C11 atomic 기본

```c
#include <stdatomic.h>

_Atomic unsigned long drops;          /* atomic 변수 선언 */

atomic_fetch_add(&drops, 1);          /* 원자적 증가 — 끼어들기 불가 */
unsigned long n = atomic_load(&drops);
atomic_store(&drops, 0);
```

인자가 없는 버전은 가장 강한 순서(`memory_order_seq_cst`)를 쓴다. 필요에 따라 `_explicit` 버전으로 순서를 약하게 해서 성능을 얻는다.

### 5.5 메모리 순서 — "데이터 먼저, 깃발 나중"

가장 흔한 패턴은 **메시지 전달**이다. 스레드 A가 데이터를 준비하고 "준비됨" 깃발을 세운다. 스레드 B는 깃발을 보고 데이터를 읽는다.

```
스레드 A (생산자)                        스레드 B (소비자)
data = 42;                ①             if (ready) {           ③
ready = 1;                ②                 use(data);         ④
                                        }
```

사람은 ①이 ②보다 먼저 일어났으니 B가 ③에서 ready=1을 보면 ④에서 42를 볼 거라고 생각한다. 하지만 컴파일러나 CPU가 ①과 ②의 순서를 바꾸면 B는 **ready=1인데 data는 옛 값**을 본다. 해법이 release/acquire 짝이다.

```c
/* 생산자 */
data = 42;
atomic_store_explicit(&ready, 1, memory_order_release);   /* 위의 쓰기가 이것보다 먼저 보인다 */

/* 소비자 */
if (atomic_load_explicit(&ready, memory_order_acquire))   /* 이걸 본 뒤의 읽기는 */
    use(data);                                            /* 생산자의 release 이전 쓰기를 본다 */
```

- **release store**: "이 저장 **이전**의 모든 읽기·쓰기가, 이 저장보다 **뒤로** 밀려나지 않는다." — 짐을 다 싣고 나서 출발 깃발을 올린다.
- **acquire load**: "이 읽기 **이후**의 모든 읽기·쓰기가, 이 읽기보다 **앞으로** 당겨지지 않는다." — 깃발을 확인한 뒤에 짐을 내린다.
- 둘이 **같은 변수에서 짝**을 이루면, release 이전의 모든 쓰기가 acquire 이후에 보인다.

| memory order | 보장 | 쓸 때 |
|---|---|---|
| `relaxed` | 원자성만. 순서 보장 없음 | 다른 데이터와 무관한 **통계 카운터** (drop 수, 재연결 수) |
| `release` (store) | 이전 접근이 뒤로 못 감 | 데이터를 publish하는 쪽 |
| `acquire` (load) | 이후 접근이 앞으로 못 감 | publish된 데이터를 받는 쪽 |
| `acq_rel` | 둘 다 (read-modify-write) | CAS나 fetch_add로 publish와 수신을 동시에 할 때 |
| `seq_cst` | 모든 스레드가 같은 전체 순서를 봄 | **기본값.** 헷갈리면 이걸 쓴다. 약간 느리다 |

> **Don의 경험과 연결**: ARM에서 `release`는 대략 "저장 전에 `DMB`", `acquire`는 "읽은 뒤에 `DMB`"다. Cortex-R 멀티코어에서 공유 메모리 디스크립터를 쓰고 배리어를 넣던 것과 같은 일을, C11이 이식 가능한 이름으로 부르는 것뿐이다.

### 5.6 대표 예제 — ISR ↔ 태스크 SPSC 링버퍼

UART RX 인터럽트(생산자 하나)가 바이트를 넣고 파서 태스크(소비자 하나)가 꺼낸다. ISR에서는 뮤텍스를 못 쓴다. 생산자와 소비자가 **각각 하나씩**이면 락 없이 안전하게 만들 수 있다.

```c
#define RING_CAP 64u                       /* 2의 거듭제곱 */

typedef struct {
    uint8_t          buf[RING_CAP];
    _Atomic unsigned head;                 /* 소비자만 store */
    _Atomic unsigned tail;                 /* 생산자만 store */
} Ring;

bool ring_push(Ring *r, uint8_t b)         /* ISR 전용 */
{
    unsigned t = atomic_load_explicit(&r->tail, memory_order_relaxed); /* 내가 쓴 값 */
    unsigned next = (t + 1u) & (RING_CAP - 1u);
    /* 소비자가 얼마나 비웠는지 본다 → acquire */
    if (next == atomic_load_explicit(&r->head, memory_order_acquire))
        return false;                      /* full (한 칸은 항상 비워둔다) */

    r->buf[t] = b;                         /* 데이터 먼저 */
    /* release: 위의 데이터 write가 tail publish 이전에 보이도록 보장 */
    atomic_store_explicit(&r->tail, next, memory_order_release);
    return true;
}

bool ring_pop(Ring *r, uint8_t *out)       /* 태스크 전용 */
{
    unsigned h = atomic_load_explicit(&r->head, memory_order_relaxed);
    /* acquire: tail을 읽은 뒤 buf를 읽어야 생산자의 데이터가 보인다 */
    if (h == atomic_load_explicit(&r->tail, memory_order_acquire))
        return false;                      /* empty */

    *out = r->buf[h];
    atomic_store_explicit(&r->head, (h + 1u) & (RING_CAP - 1u), memory_order_release);
    return true;
}
```

왜 락 없이 되는가:

- **각 인덱스는 쓰는 쪽이 한 명뿐이다.** tail은 생산자만, head는 소비자만 쓴다. 두 스레드가 같은 변수에 쓰는 경쟁이 없다.
- 자기가 쓰는 인덱스는 relaxed로 읽어도 된다(자기가 마지막으로 쓴 값이므로).
- 상대 인덱스는 acquire로 읽고, 자기 인덱스는 release로 publish한다 — 5.5의 "데이터 먼저, 깃발 나중"이 양방향으로 적용된다.
- 한 칸을 비워 두면 `head == tail`(빈 것)과 가득 찬 상태가 구분된다.
- 가득 차면 ISR은 **기다리지 않고 버린다**(실제로는 drop 카운터를 올린다). ISR이 기다리면 시스템 전체가 멈춘다.

### 5.7 CAS — 조건부 원자적 갱신

`compare_exchange`(CAS)는 "값이 내가 예상한 것과 같으면 새 값으로 바꾸고, 다르면 실패하고 현재 값을 알려준다"를 원자적으로 한다. 여러 스레드가 경쟁하는 갱신을 락 없이 할 때 쓴다.

```c
/* 최대 RSSI를 락 없이 갱신 */
void update_max(_Atomic int *max, int v)
{
    int cur = atomic_load(max);
    while (v > cur &&
           !atomic_compare_exchange_weak(max, &cur, v)) {
        /* 실패하면 cur 에 현재 값이 들어온다 → 다시 비교 */
    }
}
```

- `_weak`는 가끔 이유 없이 실패할 수 있지만(루프 안에서 쓰면 문제없음) 더 빠르다.
- **ABA 문제**: 값이 A→B→A로 바뀌면 CAS는 변화가 없었다고 착각한다. 포인터를 CAS하는 lock-free 스택 등에서 문제가 된다. 해법은 포인터에 버전 카운터를 붙이는 것(tagged pointer).

### 5.8 언제 atomic, 언제 mutex — 고르는 기준

| 상황 | 선택 | 이유 |
|---|---|---|
| 독립된 카운터·플래그 하나 | atomic (relaxed 또는 seq_cst) | 한 단어짜리, 불변식이 다른 변수와 엮이지 않음 |
| ISR ↔ 태스크 데이터 전달 | SPSC 링버퍼 (atomic) | ISR은 잠들 수 없음 |
| 여러 필드에 걸친 불변식 | **mutex** | atomic 여러 개로는 필드 간 관계를 못 지킴 (2.4) |
| 조건을 기다려야 함 | mutex + condvar | atomic에는 "잠들어 기다리기"가 없음 |
| 읽기가 압도적, 쓰기 드묾, 측정된 병목 | seqlock / RCU / refcount 교체 | 읽기 쪽 비용을 거의 0으로 |
| 잘 모르겠다 | **mutex** | 틀리기 어렵고, 대부분 충분히 빠르다 |

> **면접 문장**: "I default to a mutex because it's hard to get wrong. I reach for atomics when the shared state is a single word, when I'm in an ISR, or when profiling shows the lock is a bottleneck — and I say which memory order I'm using and why."

측정 근거 없이 lock-free를 쓰는 것은 감점 요인이다. 정확성을 증명하기 어렵고, 유지보수하는 다음 사람이 이해하기 어렵다.

### 5.9 심화 — 읽기 중에 설정을 바꾸는 법

클라우드에서 새 설정이 내려와 교체해야 하는데, 워커들이 계속 읽고 있다. 패턴은 이렇다. 설정을 **불변 객체**로 만들고, 포인터만 atomic하게 교체하고, 읽는 쪽이 **참조 카운트**를 올려서 쓰는 동안 해제되지 않게 한다.

여기에 **숨은 함정**이 하나 있다. "포인터 읽기"와 "참조 카운트 증가"는 각각은 원자적이지만 **둘을 합친 것은 원자적이지 않다.** 그 사이에 publisher가 포인터를 바꾸고 옛 객체의 마지막 참조를 해제하면, 읽는 쪽은 이미 해제된 메모리의 카운트를 올린다(use-after-free). 이 틈을 닫는 방법은 아주 짧은 락, hazard pointer, RCU 셋뿐이고, 임베디드에서는 **짧은 락이 대부분 정답**이다. 이 함정을 스스로 발견해서 말하면 시니어 수준의 답이 된다.

```check
Q: 공유 데이터의 세 가지 문제(원자성·가시성·순서)를 각각 한 줄로. volatile은 이 중 무엇을 해결하나?
A: 원자성은 연산이 중간에 끊기지 않는가, 가시성은 쓴 값이 다른 스레드에 보이는가, 순서는 쓴 순서대로 보이는가. volatile은 컴파일러 수준의 가시성(매번 실제 메모리 접근)만 해결한다. 원자성과 CPU 수준 순서는 해결하지 못한다.
Q: "데이터를 쓰고 ready 플래그를 세운다" 패턴에서 생산자와 소비자에 각각 어떤 memory order를 쓰고, 무엇이 보장되나?
A: 생산자는 플래그를 release로 store, 소비자는 플래그를 acquire로 load. 소비자가 플래그=1을 보면, 생산자가 그 release store 이전에 한 모든 쓰기(데이터)가 소비자의 acquire 이후 읽기에 보인다.
Q: SPSC 링버퍼가 락 없이 안전한 핵심 이유는? MPSC(생산자 여럿)로 바꾸면 왜 그대로는 안 되나?
A: 각 인덱스에 쓰는 스레드가 하나뿐이라(tail은 생산자, head는 소비자) 같은 변수에 대한 쓰기 경쟁이 없다. 생산자가 여럿이면 여러 스레드가 tail을 동시에 갱신하려 하므로 CAS로 슬롯을 예약하고 데이터 기록 완료를 별도로 표시해야 해서 복잡해진다. 실무에선 생산자별 큐를 두는 편이 낫다.
Q: relaxed를 써도 되는 대표적인 경우는? 링버퍼 인덱스 publish에 relaxed를 쓰면 무엇이 깨지나?
A: 다른 데이터와 순서 관계가 없는 통계 카운터. 인덱스 publish에 relaxed를 쓰면 소비자가 "인덱스는 늘었는데 버퍼 데이터는 아직 안 보이는" 상태를 볼 수 있어 쓰레기 값을 읽는다.
Q: 여러 필드로 된 통계 구조체를 atomic으로 보호하려는 동료에게 뭐라고 하겠나?
A: 필드 간 불변식이 있으면 atomic 여러 개로는 지킬 수 없다. 하나의 뮤텍스로 갱신 전체를 감싸는 게 맞다. 읽기가 매우 잦고 측정된 병목이라면 seqlock이나 스냅샷 교체를 고려하되, 근거가 먼저다.
Q: 참조 카운트 기반 설정 교체에서 "포인터 load 후 refcount 증가"가 위험한 이유와 해법은?
A: 두 동작 사이에 publisher가 포인터를 바꾸고 옛 객체의 마지막 참조를 놓아 free할 수 있다. 그러면 이미 해제된 메모리의 카운트를 증가시킨다(use-after-free). 두 동작을 아주 짧은 락으로 묶거나, hazard pointer 또는 RCU/epoch 방식으로 해제를 늦춘다. 임베디드에서는 짧은 락이 대개 최선이다.
```

> **연습 연결**: 문제 은행 [03 Atomic & Lock-free](verkada_prep/index.html) (Q21~Q30, `make prob N=03_atomics_lockfree`). 해설은 [기초 노트 02 — SPSC 링버퍼](notes_site/basics/02_spsc_ring.html)와 [기초 노트 08 — 설정 교체](notes_site/basics/08_config_swap_refcount.html). 레이스를 눈으로 보려면 `make tsan N=03_atomics_lockfree`.

---

## 6장. 실시간 데이터 처리 패턴

### 6.1 왜 필요한가

메일은 이 주제에 **예시까지** 붙였다 — "such as double buffering". 센서, 카메라, 모뎀처럼 **생산 속도가 외부에서 정해지는** 데이터를 다루는 코드는 일반적인 큐와 다른 제약을 받는다. 이 장은 그 제약이 무엇이고, 제약에 따라 어떤 패턴을 고르는지를 다룬다.

### 6.2 "실시간"의 정확한 뜻

실시간은 **빠르다**는 뜻이 아니다. **정해진 시간 안에 반드시 끝난다(예측 가능하다)**는 뜻이다.

| 용어 | 뜻 | 예 |
|---|---|---|
| deadline | 이 시각까지 끝나야 한다 | 다음 DMA 완료 전까지 버퍼를 비워야 한다 |
| jitter | 주기의 흔들림 | 100ms 주기인데 실제로는 97~108ms |
| hard real-time | 한 번이라도 놓치면 실패 | 모터 제어, 에어백 |
| soft real-time | 가끔 놓쳐도 품질만 떨어짐 | 영상 스트리밍, 센서 텔레메트리 |

Verkada 게이트웨이와 센서는 대부분 **soft real-time**이다. 그래서 "절대 놓치지 않기"보다 **"놓치면 최신 것으로 대체하고, 놓친 사실을 기록한다"**가 기본 전략이 된다.

### 6.3 생산자를 막으면 안 되는 이유

일반적인 bounded queue(4장)는 가득 차면 생산자를 **재운다.** 그런데 생산자가 다음 중 하나라면 재울 수 없다.

- **ISR** — 인터럽트 핸들러는 잠들 수 없다.
- **DMA 완료 콜백** — 다음 전송을 늦게 걸면 하드웨어가 데이터를 덮어쓰거나 버린다.
- **고정 주기 샘플러** — 멈추면 그 시각의 샘플이 영원히 사라진다.

이때 선택지는 두 가지뿐이다. **공간을 충분히 두거나(버퍼 크기)**, **무엇을 버릴지 정하거나(정책)**. 이 장의 모든 패턴은 이 둘의 조합이다.

### 6.4 첫 질문 — 데이터가 무엇을 의미하나

패턴을 고르기 전에 **"소비자에게 모든 항목이 필요한가, 최신 값만 필요한가"**를 먼저 묻는다. 면접에서도 이 질문을 먼저 던지면 좋다.

| 데이터의 성격 | 예 | 맞는 패턴 |
|---|---|---|
| **스트림** — 모든 항목이 의미가 있다 | UART 바이트, 출입 이벤트, 로그 | 큐 / 링버퍼 (4장, 5장) |
| **상태** — 최신 값만 의미가 있다 | 신호 세기, 온도, 링크 상태 | 최신값 슬롯 / seqlock |
| **큰 프레임** — 최신 한 장이면 충분 | 카메라 프레임, 센서 배열 | 더블/트리플 버퍼 |

### 6.5 더블 버퍼링 — 핵심 아이디어

버퍼 두 개를 번갈아 쓴다. 하나를 **생산자가 채우는 동안** 다른 하나를 **소비자가 읽는다.** 채우기가 끝나면 역할을 바꾼다. 데이터를 복사하지 않고 **역할(인덱스)만** 바꾸므로 교체가 즉시 끝난다.

가장 중요한 한 문장은 이것이다.

> **락으로 데이터를 보호하지 말고, 버퍼의 소유권을 보호한다.**

프레임 512바이트를 락 안에서 복사하면 그동안 생산자가 멈춘다. 대신 "이 버퍼는 지금 누구 것인가"라는 **인덱스 몇 개만** 락으로 보호하고, 실제 데이터 쓰기·읽기는 락 밖에서 한다. 소유권이 명확하면 두 스레드가 같은 버퍼를 동시에 만질 일이 없으므로 안전하다.

각 버퍼는 항상 세 상태 중 하나다.

```
            write_begin              write_commit            read_acquire
  [FREE] ──────────────▶ [WRITER] ──────────────▶ [READY] ──────────────▶ [READER]
    ▲                   생산자가 채움            publish됨,              소비자가 읽음
    │                                           누구도 안 만짐                │
    └──────────────────────────────── read_release ◀──────────────────────────┘

  버퍼 2개에서: reader가 하나, READY가 하나를 차지하면 writer는 쓸 곳이 없다
             → READY(아직 안 읽힌 프레임)를 버리고 그 버퍼에 쓴다  = drop
             → reader가 쥔 버퍼는 절대 건드리지 않는다              = tearing 방지
```

### 6.6 더블 버퍼 코드

```c
typedef struct {
    Frame           buf[NBUF];
    int             write_idx;      /* writer 소유 버퍼, 없으면 -1 */
    int             ready_idx;      /* publish된 최신 버퍼, 없으면 -1 */
    int             reader_idx;     /* reader 대여 중 버퍼, 없으면 -1 */
    unsigned long   dropped;        /* 소비되지 못하고 버려진 프레임 수 */
    pthread_mutex_t m;
} DoubleBuf;

Frame *db_write_begin(DoubleBuf *db)
{
    pthread_mutex_lock(&db->m);
    int idx = -1;
    for (int i = 0; i < NBUF; i++) {              /* 아무도 소유하지 않은 버퍼 */
        if (i != db->ready_idx && i != db->reader_idx) { idx = i; break; }
    }
    if (idx < 0) {
        /* reader가 하나, READY가 하나를 차지 → READY를 희생시킨다.
         * (절대 reader 버퍼는 건드리지 않는다: 그게 tearing의 원인) */
        idx = db->ready_idx;
        db->ready_idx = -1;
        db->dropped++;
    }
    db->write_idx = idx;
    pthread_mutex_unlock(&db->m);
    return &db->buf[idx];                          /* 데이터 write는 락 밖에서 */
}

void db_write_commit(DoubleBuf *db)
{
    pthread_mutex_lock(&db->m);
    if (db->ready_idx >= 0)
        db->dropped++;                             /* 소비 전에 더 새 프레임이 나옴 */
    db->ready_idx  = db->write_idx;                /* publish */
    db->write_idx  = -1;
    pthread_mutex_unlock(&db->m);
}

Frame *db_read_acquire(DoubleBuf *db)
{
    Frame *f = NULL;
    pthread_mutex_lock(&db->m);
    if (db->reader_idx < 0 && db->ready_idx >= 0) {
        db->reader_idx = db->ready_idx;            /* READY를 reader 소유로 이전 */
        db->ready_idx  = -1;
        f = &db->buf[db->reader_idx];
    }
    pthread_mutex_unlock(&db->m);
    return f;
}
```

(`db_read_release`는 `reader_idx = -1`로 되돌리기만 한다.)

확인할 것:

- 락 안에서는 **인덱스만** 바꾼다. 프레임 채우기(`memset`, DMA)는 `write_begin`이 반환한 **뒤, 락 밖에서** 한다.
- `write_begin`은 **절대 기다리지 않는다.** 빈 버퍼가 없으면 READY를 희생한다 — 생산자를 막지 않는다는 6.3의 요구를 지킨다.
- 버린 프레임은 `dropped`로 센다. 테스트에서 "읽은 수 + 버린 수 + 남은 수 = 만든 수"를 검사할 수 있다.
- 락 unlock/lock 짝이 3.2의 가시성을 보장하므로, 락 밖에서 쓴 데이터가 소비자에게 제대로 보인다.

### 6.7 더블 vs 트리플 버퍼 — trade-off의 대표 예

`NBUF`를 3으로 바꾸면 코드 수정 없이 **트리플 버퍼**가 된다. 세 번째 버퍼 덕에 "reader 하나 + READY 하나"인 상황에서도 writer가 쓸 빈 버퍼가 항상 있다.

| | 더블 (2개) | 트리플 (3개) |
|---|---|---|
| 메모리 | 프레임 × 2 | 프레임 × 3 (1.5배) |
| writer가 빈 버퍼를 못 찾는 경우 | reader가 느리면 발생 → READY를 희생 | 없음 |
| reader가 받는 프레임 | 최신이 아닐 수 있음(읽는 동안 두 장이 지나감) | 항상 가장 최근에 완성된 프레임 |
| 언제 | 메모리가 빠듯한 MCU, 작은 프레임 | 프레임 크기 대비 메모리 여유, 지연 최소화 중요 |

> **면접 문장**: "Double buffering costs two frames of memory, but if the consumer is slow the producer has to drop the pending frame. Triple buffering spends one more frame so the producer never has to wait or drop for lack of a free buffer — on a 1080p pipeline that extra frame is real BOM cost, so the product constraint decides."

### 6.8 작은 상태값 — seqlock

신호 세기(RSRP/RSRQ), SIM 슬롯, 업타임처럼 **작은 구조체 하나**를 한 writer가 가끔 쓰고 여러 reader가 자주 읽는 경우, 버퍼 교체 대신 **seqlock**을 쓸 수 있다.

- writer: 시퀀스 번호를 **홀수**로(쓰는 중) → 데이터 갱신 → **짝수**로(끝).
- reader: 시퀀스 읽기(홀수면 재시도) → 데이터 복사 → 시퀀스 다시 읽기 → 같으면 성공, 다르면 재시도.
- writer는 **절대 기다리지 않는다.** 대신 reader가 재시도한다.

주의할 점이 둘 있다. (1) 엄밀히는 C 표준상 data race라서 ThreadSanitizer가 경고한다 — 실무에서는 필드를 relaxed atomic으로 두거나 커널처럼 `READ_ONCE`/`WRITE_ONCE`를 쓴다. (2) 재시도 루프를 `do { ... if (홀수) continue; ... } while (before != after);`로 쓰면 `continue`가 **조건식으로 점프**해서 초기화 안 된 변수를 비교하는 버그가 된다. `for(;;)` + 명시적 `return`으로 쓴다.

그리고 이것도 말할 수 있어야 한다. **"그냥 mutex로 충분하지 않은가?"** — 대부분 충분하다. seqlock은 읽기 빈도가 매우 높고 writer 지연이 실제 문제라는 측정 근거가 있을 때만 쓴다.

### 6.9 백프레셔와 drop 정책

스트림 데이터(모든 항목이 의미 있음)인데 소비자가 못 따라가면? LTE 링크가 끊겨 업로더가 멈춘 동안에도 센서는 계속 샘플을 만든다. 메모리는 유한하므로 **무엇을 할지 정책으로 정한다.**

| 정책 | 동작 | 맞는 데이터 |
|---|---|---|
| BLOCK | 생산자를 재운다 | 절대 잃으면 안 되는 이벤트(감사, 과금). **단, 실시간 경로에서는 금지** |
| DROP_NEWEST | 새로 온 것을 버린다 | 앞부분 이력이 중요한 로그 |
| DROP_OLDEST | 가장 오래된 것을 버린다 | 최신 상태가 중요한 텔레메트리·알람 |

정책은 **데이터의 의미가** 정한다. 그리고 어떤 정책이든 **버린 개수를 반드시 카운트해서 같이 보고한다.** "가끔 데이터가 비어요"라는 필드 이슈를 추적할 수 있는 유일한 단서다.

> **면접 문장**: "A dropped sample is acceptable; a silently dropped sample is a bug. Whatever the policy, I export a dropped counter alongside the data."

### 6.10 속도가 다른 생산자와 소비자 — 레이트 디커플링

센서가 400Hz로 샘플을 만들고 업로더는 100Hz로 보낸다면, 4개 중 무엇을 보낼지 정해야 한다.

| 방식 | 동작 | 맞는 데이터 |
|---|---|---|
| 최신값 채택 | 마지막 샘플만 보낸다 | 상태(현재 온도) |
| 구간 평균 | 4개를 평균해 보낸다 | 노이즈가 있는 측정값(공기질, 전력) |
| 최대/최소 유지 | 구간의 극값을 보낸다 | 피크가 중요한 값(전류 스파이크) |

**배치 처리**도 같은 맥락이다. LTE는 요청 하나당 오버헤드(라디오 깨우기, 헤더, 왕복 지연)가 크므로 16~64개를 모아서 한 번에 보내는 게 전력과 데이터 요금 모두에 유리하다. 대신 지연이 늘어난다 — 이것도 trade-off로 말한다.

### 6.11 주기 실행 — drift와 즉시 취소

"100ms마다 샘플링"을 `sleep(100ms)` 루프로 짜면 두 가지 문제가 생긴다.

1. **drift** — 작업 시간이 매 주기 누적돼서 주기가 점점 밀린다(100ms + 작업 3ms = 103ms씩).
2. **취소 지연** — 종료 신호를 받아도 최대 100ms 늦게 반응한다.

해법은 **절대 시각(deadline) 기준**으로, **취소 가능한 대기**를 쓰는 것이다.

```c
int64_t next = now_mono_ns() + period_ns;
for (;;) {
    if (periodic_wait_until(&p, next)) break;   /* stop이면 즉시 true */
    do_sample();
    next += period_ns;                          /* '지금 + 주기'가 아니라 '이전 deadline + 주기' */
    if (next < now_mono_ns()) {                 /* 주기를 놓쳤다 */
        late_ticks++;
        next = now_mono_ns() + period_ns;       /* 몰아서 따라잡지 않는다 */
    }
}
```

- 대기는 조건 변수의 timed wait(4.8)로 한다. 종료 시 broadcast하면 즉시 깨어난다.
- 시계는 **CLOCK_MONOTONIC** — 벽시계는 NTP가 바꿀 수 있다.
- 주기를 놓쳤을 때 밀린 만큼 **몰아서 실행하지 않는다.** 센서 샘플링에서 몰아 실행은 버스트를 만들어 상황을 악화시킨다. 놓친 횟수는 카운터로 남긴다.

### 6.12 DMA와 캐시 — 하드웨어가 끼면 생기는 것

Don에게는 익숙하지만 면접에서 말하면 강한 포인트다. DMA는 CPU 캐시를 거치지 않고 메모리에 직접 쓴다. 그래서:

- DMA가 쓴 버퍼를 CPU가 읽기 전에 해당 캐시 라인을 **invalidate**해야 옛 값을 안 읽는다.
- CPU가 쓴 버퍼를 DMA가 보내기 전에 **clean(flush)**해야 캐시에만 있는 값이 전송된다.
- 버퍼는 캐시 라인 크기로 정렬한다(옆 데이터와 라인을 공유하면 invalidate가 그것까지 날린다).
- Linux에서는 `dma_alloc_coherent`(캐시 일관성 메모리) 또는 streaming DMA API(`dma_map_single` + sync)가 이 일을 한다.
- 완료 ISR에서는 **인덱스만 바꾸고**(더블 버퍼의 commit), 파싱·복사는 태스크에서 한다.

### 6.13 고르는 기준 — 한 장 요약

| 상황 | 패턴 |
|---|---|
| ISR → 태스크, 바이트 스트림 | SPSC 링버퍼 (락 없음) |
| 여러 스레드 → 한 스레드, 모든 이벤트 필요 | bounded queue (mutex + condvar) |
| 모든 항목이 필요하지만 링크가 막힐 수 있음 | bounded queue + drop 정책 + 카운터 + (필요 시) 플래시 spill |
| 큰 프레임, 최신 한 장이면 충분 | 더블 버퍼 (메모리 빠듯) / 트리플 버퍼 (지연 최소) |
| 작은 상태, 읽기 매우 잦음 | 뮤텍스 보호 스냅샷 → 측정 후 seqlock |
| 속도가 다른 생산·소비 | 최신값 / 구간 평균 / 극값 + 배치 |
| 고정 주기 작업 | 절대 deadline + monotonic 시계 + 취소 가능한 대기 |

```check
Q: 패턴을 고르기 전에 가장 먼저 던질 질문은? 그 답에 따라 어떻게 갈리나?
A: "소비자에게 모든 항목이 필요한가, 최신 값만 필요한가?" 모든 항목이면 큐나 링버퍼(+ 넘칠 때의 정책), 최신 값이면 최신값 슬롯·seqlock·더블 버퍼로 간다.
Q: 더블 버퍼에서 "데이터가 아니라 소유권을 보호한다"는 말의 뜻과 장점은?
A: 락은 "이 버퍼가 지금 writer·READY·reader 중 누구 것인가"를 나타내는 인덱스 몇 개만 보호하고, 실제 프레임 쓰기·읽기는 락 밖에서 한다. 소유권이 명확해서 같은 버퍼를 동시에 만질 일이 없고, 임계구역이 매우 짧아 생산자가 거의 막히지 않는다.
Q: 버퍼 2개로 더블 버퍼링할 때 writer가 쓸 버퍼가 없는 상황은 언제 생기고, 어떻게 처리하나? 트리플 버퍼는 이걸 어떻게 해결하나?
A: reader가 한 버퍼를 읽는 중이고 다른 버퍼가 READY(아직 안 읽힘)일 때. writer는 기다리지 않고 READY를 희생해 거기에 쓰며 drop을 센다. reader 버퍼는 절대 건드리지 않는다. 트리플 버퍼는 세 번째 버퍼가 있어 이 상황에서도 빈 버퍼가 항상 남는다.
Q: LTE가 끊겨 텔레메트리 큐가 찼다. 신호 세기 텔레메트리와 과금 이벤트에 각각 어떤 정책을 쓰겠나?
A: 신호 세기는 최신 상태가 중요하므로 DROP_OLDEST. 과금 이벤트는 잃으면 안 되므로 BLOCK(단, 실시간 경로가 아닌 곳에서) 또는 플래시에 영구 저장 후 재전송. 어느 쪽이든 버린 수를 카운트해 보고한다.
Q: sleep 루프로 주기 실행을 할 때의 두 문제와 해법은?
A: 작업 시간 누적에 따른 drift, 종료 반응 지연. 이전 deadline에 주기를 더하는 절대 시각 스케줄과, 조건 변수 timed wait처럼 종료 시 즉시 깨울 수 있는 대기를 쓴다. 시계는 monotonic, 놓친 주기는 몰아서 실행하지 않고 카운트한다.
Q: DMA로 받은 버퍼를 CPU가 읽을 때와, CPU가 채운 버퍼를 DMA로 보낼 때 각각 필요한 캐시 작업은?
A: DMA가 쓴 걸 읽기 전에는 해당 라인을 invalidate(옛 캐시 값 버리기), CPU가 쓴 걸 보내기 전에는 clean/flush(캐시 내용을 메모리로 내리기). 버퍼는 캐시 라인 정렬.
```

> **연습 연결**: ★ 메일 명시 주제. 문제 은행 [04 실시간 버퍼링](verkada_prep/index.html) (Q31~Q38, `make prob N=04_realtime_buffers`). 한 문제를 통째로 짜 보려면 `cd concurrency_practice && make run N=03` (더블 버퍼) — tearing을 바이트 단위로 검사하는 테스트가 들어 있다. 해설은 [기초 노트 03](notes_site/basics/03_double_buffer.html), [04 seqlock](notes_site/basics/04_seqlock_latest.html), [05 drop 정책](notes_site/basics/05_drop_policy_queue.html), [07 주기 타이머](notes_site/basics/07_periodic_timer.html).

---

## 7장. 모듈화·테스트 가능한 임베디드 소프트웨어

### 7.1 왜 필요한가

메일의 네 번째 주제이고, 세션 2부(시스템 설계)에서 거의 반드시 나온다 — **"이걸 하드웨어 없이 어떻게 테스트하나요?"** JD도 "test driven and data driven methods"를 명시했다. 이 질문에 막힘없이 답하려면, 테스트 기법보다 먼저 **테스트할 수 있게 코드를 나누는 법**을 알아야 한다.

### 7.2 임베디드 테스트가 어려운 세 가지 이유

| 어려움 | 예 | 그래서 필요한 것 |
|---|---|---|
| 하드웨어 의존 | 드라이버가 I2C 레지스터를 직접 읽는다 | 하드웨어 접근을 **인터페이스 뒤로** 숨긴다 |
| 시간 의존 | "1초 안에 응답 없으면 타임아웃" — 테스트도 1초 기다려야 함 | **시계를 주입**해서 테스트가 시간을 조종한다 |
| 비결정성 | 스레드 순서, 센서 노이즈, 링크 끊김 | 동시성을 가장자리로 밀어내고, 핵심 로직은 **순수 함수**로 |

### 7.3 계층 나누기

```
┌──────────────────────────────────────────┐
│ 애플리케이션 / 서비스                        │  ← 정책: 언제 측정? 실패하면? 무엇을 보고?
├──────────────────────────────────────────┤
│ 드라이버 (센서, 모뎀)                        │  ← 프로토콜: 명령 순서, 재시도, CRC, 상태머신
├──────────────────────────────────────────┤
│ HAL 인터페이스  i2c_read / i2c_write / now_ms │  ← 경계: 여기서 위는 하드웨어를 모른다
├──────────────────────────────────────────┤
│ 실제 구현: Linux i2c-dev · MCU 레지스터 · fake │  ← 제품에선 진짜, 테스트에선 가짜
└──────────────────────────────────────────┘
```

각 층은 **바로 아래 층의 인터페이스만** 안다. 그러면 HAL 아래를 가짜로 바꿔 끼우는 것만으로 위의 모든 층을 PC에서 테스트할 수 있다.

### 7.4 C에서 의존성 주입 — 함수 포인터 구조체

C++의 가상 함수가 없는 C에서는 **함수 포인터를 모은 구조체**가 인터페이스 역할을 한다.

```c
typedef enum { IO_OK = 0, IO_NACK = -1, IO_BUS_ERR = -2 } IoStatus;

typedef struct {
    IoStatus (*read)(void *ctx, uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
    IoStatus (*write)(void *ctx, uint8_t addr, uint8_t reg, uint8_t val);
    void     *ctx;
} I2cOps;

typedef struct {
    uint32_t (*now_ms)(void *ctx);
    void     *ctx;
} ClockOps;

void sensor_init(Sensor *s, I2cOps i2c, ClockOps clk, uint8_t addr, uint32_t timeout_ms);
```

- 드라이버는 `i2c.read(...)`만 부른다. 그게 Linux `/dev/i2c-1`인지, MCU 레지스터인지, 테스트용 가짜인지 모른다.
- `ctx`는 구현마다 필요한 상태(파일 디스크립터, 레지스터 베이스 주소, 가짜의 시나리오)를 담는 자리다.
- **시계도 똑같이 주입한다.** 이게 이 설계의 가장 강력한 부분이다(7.6).

C에서 의존성을 바꿔 끼우는 방법은 세 가지이고, 각각 trade-off가 있다.

| 방법 | 방식 | 장점 | 단점 |
|---|---|---|---|
| 함수 포인터 구조체 | 위 코드 | 실행 중 교체 가능, 여러 인스턴스, 가장 유연 | 간접 호출 비용(작음), 코드 조금 늘어남 |
| 링크 타임 치환 | 같은 함수 이름을 제품용 .c / 테스트용 .c로 따로 만들어 링크 | 코드 변경 없음, 비용 0 | 한 바이너리에 한 구현만, 빌드 설정이 복잡 |
| 컴파일 타임(매크로·헤더) | `#ifdef TEST`로 구현 선택 | 비용 0 | 조건부 코드가 늘어 읽기 어려움 |

추상화 비용을 물으면: 함수 포인터 간접 호출은 몇 사이클이고 I2C 트랜잭션은 수백 마이크로초다 — **측정 가능한 차이가 없다.** 비용이 실제 문제인 핫 루프(예: 샘플당 호출되는 DSP)에서만 컴파일 타임 방식으로 바꾼다.

### 7.5 정책과 I/O를 떼어내기

테스트하기 좋은 코드는 **판단하는 코드**와 **실행하는 코드**가 분리돼 있다.

- **순수 함수로 뽑을 것**: CRC 계산, 재시도 여부(`should_retry(err, attempt)`), 백오프 간격(`next_delay(attempt)`), 설정 검증, 프로토콜 파싱. 입력만 넣으면 출력이 나오므로 테스트가 한 줄씩이다.
- **상태머신으로 만들 것**: 측정 시작 → 준비 대기 → 데이터 읽기 → 검증 → 완료/에러. 상태와 전이를 명시하면 "이 상태에서 이 이벤트가 오면?"을 빠짐없이 테스트할 수 있다. 전이를 **테이블**로 두면 잘못된 전이도 한눈에 보인다.
- **에러는 값으로 반환**: 드라이버는 스스로 죽거나(assert, 무한 재시도) 로그만 찍고 삼키지 않는다. 에러 코드를 돌려주고 **정책은 호출자가** 정한다. 그래야 에러 경로를 테스트할 수 있다.
- **실패하면 출력을 건드리지 않는다**: CRC가 틀렸으면 `*out`을 바꾸지 않는다. 오염된 값이 새어 나가면 안 된다.

### 7.6 시간을 조종하는 테스트

시계를 주입했기 때문에, "1초 타임아웃" 테스트가 **실제로는 0초** 걸린다.

```c
typedef struct { uint32_t ms; } FakeClock;           /* 시간을 테스트가 조종한다 */
static uint32_t fake_now(void *ctx) { return ((FakeClock *)ctx)->ms; }

/* case 3: 타임아웃 — 시계만 앞으로 돌리면 즉시 검증된다(실제 대기 0초) */
Sensor s; FakeI2c f = { .not_ready_times = 10000 }; FakeClock c = { 0 };
setup(&s, &f, &c);
assert(sensor_start_measure(&s) == SENS_OK);
int16_t ppm = 0;
assert(sensor_poll(&s, &ppm) == SENS_EAGAIN);
c.ms = 1000;                                   /* 타임아웃 경계 */
assert(sensor_poll(&s, &ppm) == SENS_ETIMEDOUT);
assert(s.state == SENS_ERROR);
assert(sensor_poll(&s, &ppm) == SENS_ESTATE);  /* 에러 상태 고착 확인 */
```

가짜 I2C(`FakeI2c`)는 "처음 N번은 준비 안 됨", "M번 NACK", "CRC 틀리게"를 설정할 수 있다. 그래서 **실제 하드웨어로는 재현하기 어려운 경로**(3번 연속 NACK 후 복구, 정확히 경계에서의 타임아웃)를 확정적으로 테스트한다. 같은 원리로 **난수, 로깅, 파일 시스템**도 주입 대상이다.

### 7.7 동시성 코드는 어떻게 테스트하나

로직을 순수 함수와 상태머신으로 빼내면 대부분은 싱글 스레드 테스트로 충분하다. 남은 **동시성 자체**(큐, 버퍼)는 이렇게 검증한다.

1. **불변식 기반 스트레스 테스트** — 스레드 여러 개, 작은 용량, 많은 반복. 정확한 값 대신 불변식을 검사한다: "생산 = 소비 + 버림 + 남음", "생산자별 순서 보존", "tearing 0".
2. **ThreadSanitizer를 CI에** — data race를 자동으로 잡는다.
3. **결정적 스케줄 주입** — 테스트 훅으로 특정 지점에 `yield`나 대기를 넣어 문제의 순서를 강제로 재현한다.
4. **장시간 soak 테스트** — 몇 시간 돌려 드문 순서와 누수를 잡는다.

### 7.8 테스트 3층 — "하드웨어 없이 어떻게?"에 대한 완성된 답

| 층 | 무엇을 | 어떻게 | 언제 |
|---|---|---|---|
| **단위(unit)** | 파싱, CRC, 상태 전이, 재시도·타임아웃 정책 | 가짜 HAL + 가짜 시계. 하드웨어 0개, 밀리초 단위 | 매 커밋 (CI) |
| **통합(integration)** | 실제 I2C 버스 + 실제 센서, 실제 모뎀 | HIL(hardware-in-the-loop) 랙에 보드 몇 장 | 매일 밤 |
| **시스템(system)** | 온도(-40~50°C), 전원 사이클, 링크 단절, 장시간 | 챔버, 프로그래머블 전원, 네트워크 결함 주입 | 릴리스 전 |

> **Don의 연결점**: 세 번째 층은 Don이 SK hynix에서 만든 **챔버 테스트 자동화**(온도 제어 API, UART 기반 테스트 시퀀스, 다수 클라이언트 상태 모니터링)와 정확히 같은 일이다. 면접에서 이 경험을 여기에 붙이면 추상적인 답이 구체적인 경험담이 된다.

**Test-driven / data-driven (JD 문구)**의 임베디드식 해석:

- **test-driven**: 드라이버를 짜기 전에 가짜 HAL로 "NACK 3번이면 이렇게 동작해야 한다"는 테스트를 먼저 쓴다. 하드웨어가 도착하기 전에 로직이 완성된다 — NPI 일정에 직접 도움이 된다.
- **data-driven**: fleet 텔레메트리(drop 수, 재연결 수, 타임아웃 수)를 보고 다음 수정의 우선순위를 정한다. 200만 대 규모에서는 "재현 안 되는 버그"를 통계로 찾는다.

### 7.9 깨끗하고 유지보수 가능한 코드 — 메일의 채점 기준

라이브 코딩에서 보이는 "깨끗함"은 거창한 게 아니다.

- 함수 하나는 한 가지 일. `push`에 로깅·통계·재시도를 섞지 않는다.
- 이름이 불변식을 말한다: `not_full`, `not_empty`, `ready_idx`, `reader_idx`.
- 불변식과 소유권을 **주석 한 줄로** 적는다: "count는 m으로 보호", "pop이 반환한 항목은 호출자 소유".
- 에러 경로의 출구를 하나로(`goto out`), 락은 모든 경로에서 풀린다.
- 전역 변수 대신 구조체로 상태를 묶어 **인스턴스를 여러 개** 만들 수 있게 한다(테스트가 쉬워진다).
- `const`를 정확히 붙인다: 읽기만 하는 인자는 `const T *`.

```check
Q: 임베디드 코드가 테스트하기 어려운 세 가지 이유와 각각의 설계 대응은?
A: 하드웨어 의존 → HAL 인터페이스 뒤로 숨기고 주입. 시간 의존 → 시계를 주입해 테스트가 조종. 비결정성 → 동시성을 가장자리로 밀고 핵심 로직은 순수 함수와 상태머신으로.
Q: C에서 의존성을 바꿔 끼우는 세 방법과 각각의 단점은?
A: 함수 포인터 구조체(간접 호출 비용 약간, 코드 증가), 링크 타임 치환(한 바이너리에 한 구현, 빌드 복잡), 컴파일 타임 매크로(조건부 코드로 가독성 저하).
Q: "함수 포인터가 성능을 해치지 않나?"라는 질문에 어떻게 답하나?
A: 간접 호출은 몇 사이클이고 I2C 트랜잭션은 수백 마이크로초라 측정 가능한 차이가 없다. 호출 빈도가 매우 높은 핫 루프에서만 측정 후 링크 타임이나 컴파일 타임 방식으로 바꾼다. 측정 없이 최적화하지 않는다.
Q: 시계를 주입하면 얻는 것 두 가지는?
A: 타임아웃·주기 테스트가 실제 대기 없이 즉시 끝난다. 그리고 "정확히 경계에서" 같은 타이밍을 확정적으로 재현할 수 있다.
Q: CRC 검증에 실패했을 때 드라이버가 지켜야 할 두 가지는?
A: 출력 파라미터(*out)를 절대 바꾸지 않는다(오염된 값 누출 방지). 에러 코드를 값으로 반환하고, 재측정 여부 같은 정책은 호출자에게 맡긴다(상태는 재측정 가능한 상태로).
Q: "하드웨어 없이 이걸 어떻게 테스트하나요?"에 대한 3층 답을 말해 보라.
A: 단위: 가짜 HAL과 가짜 시계로 파싱·CRC·상태 전이·재시도·타임아웃을 매 커밋 CI에서. 통합: 실제 버스와 센서를 붙인 HIL 랙에서 매일 밤. 시스템: 챔버·전원 사이클·링크 단절 주입으로 릴리스 전. 동시성 코드는 여기에 불변식 스트레스 테스트와 TSan을 더한다.
```

> **연습 연결**: ★ 메일 명시 주제. 문제 은행 [07 모듈화·테스트 설계](verkada_prep/index.html) — Q59~Q68이 하나의 I2C 공기질 센서 드라이버를 단계별로 키운다(`make prob N=07_modular_design`). 통짜 버전은 `cd concurrency_practice && make run N=10`, 해설은 [기초 노트 10](notes_site/basics/10_testable_driver.html).

---

## 8장. 시스템 설계 파트 — 도구를 조합하는 법

### 8.1 왜 필요한가

세션의 두 번째 절반이다. 1~7장의 도구를 알아도 **"게이트웨이 소프트웨어를 설계해 보세요"**라는 열린 질문 앞에서는 어디서 시작할지 막히기 쉽다. 설계 면접은 정답을 맞히는 시험이 아니라 **생각하는 순서와 trade-off 판단**을 보는 자리다. 그래서 순서를 몸에 익히는 게 가장 중요하다.

### 8.2 설계 답변의 7단계 프레임

어떤 문제가 나와도 이 순서로 말한다.

| 단계 | 할 일 | 예시 질문/문장 |
|---|---|---|
| 1. 요구사항·제약 | 3분 동안 질문만 한다 | "샘플 주기와 크기는? 링크가 얼마나 오래 끊길 수 있나요? 데이터 손실이 허용되나요? RAM/플래시 예산은?" |
| 2. 데이터 흐름 | 박스와 화살표를 먼저 그린다 | 센서 → 샘플러 → 버퍼 → 업로더 → 클라우드 |
| 3. 인터페이스 | 박스 사이의 API를 정한다 | `push(sample)`, `pop_batch(out, max)` |
| 4. 동시성 모델 | 스레드 몇 개, 무엇을 공유, 무엇으로 보호 | "공유 상태를 큐 하나로 좁히겠습니다" |
| 5. 실패와 복구 | 무엇이 실패할 수 있고, 그때 어떻게 되나 | 링크 단절, 전원 손실, 센서 무응답, 시계 틀림 |
| 6. 관측 가능성 | 무엇을 세고 무엇을 로그로 남기나 | sent / dropped / retries / queue_depth / last_error |
| 7. 테스트 + trade-off 요약 | 어떻게 검증하고, 무엇을 포기했나 | "메모리를 더 써서 손실을 줄였고, 지연은 늘었습니다" |

> **핵심 습관**: 1단계를 건너뛰지 않는다. 요구사항을 묻지 않고 바로 설계하면 "가정을 확인하지 않는 사람"으로 보인다. 모든 질문에 면접관이 "알아서 정하세요"라고 하면, **가정을 소리 내어 선언**하고 진행한다.

### 8.3 게이트웨이 소프트웨어의 뼈대 — 스레드인가, 이벤트 루프인가

Linux 게이트웨이 데몬은 대개 두 방식 중 하나, 또는 섞어서 쓴다.

| | 스레드 여럿 | 이벤트 루프 (poll/epoll) |
|---|---|---|
| 구조 | 작업마다 스레드, 큐로 연결 | 한 스레드가 여러 fd(소켓, 모뎀 tty, 타이머)를 기다렸다 처리 |
| 장점 | 블로킹 코드를 그대로 쓸 수 있음, 멀티코어 활용 | 락이 거의 필요 없음, 결정적, 자원 적게 씀 |
| 단점 | 락·종료·데드락을 신경 써야 함 | 한 핸들러가 오래 걸리면 전체가 멈춤 |
| 맞는 곳 | CPU 작업(압축, 암호화), 블로킹 라이브러리 | I/O 위주(네트워크, 시리얼, 타이머) |

현실적인 답은 **혼합**이다. I/O는 이벤트 루프 하나로 처리하고, 무거운 계산만 워커 스레드에 넘기며, 둘 사이는 큐 하나로 연결한다. 공유 상태가 큐 하나로 줄어드니 동시성 버그가 들어갈 자리가 적다.

그리고 모뎀·라우팅 테이블처럼 **직렬로 다뤄야 하는 자원은 한 스레드만 소유**하게 한다(2.8, 3.5). 다른 부분은 그 스레드에 메시지로 요청한다.

### 8.4 설계 예 1 — 센서 데이터를 클라우드까지 (가장 유력)

> "공기질 센서가 1초마다 샘플을 만든다. 이걸 LTE로 클라우드에 올리는 게이트웨이 소프트웨어를 설계하라."

**1) 요구사항 질문**: 샘플 크기? 손실 허용? 링크 단절 최대 시간? 지연 요구(실시간 알람인가, 통계인가)? RAM·플래시 예산? 셀룰러 데이터 요금 제약?

**2) 데이터 흐름**

```
 [I2C 센서] ──▶ (샘플러 스레드, 1Hz 절대 deadline)
                     │ push (drop-oldest, 카운터)
                     ▼
               [bounded queue, RAM]
                     │ pop_batch(최대 32개 또는 30초)
                     ▼
              (업로더 스레드) ──▶ HTTPS/MQTT ──▶ 클라우드
                     │ 실패 시
                     ▼
               [플래시 spill 파일]  ← 링크 복구 시 먼저 재전송
```

**3~4) 동시성 모델**: 스레드 두 개(샘플러, 업로더). 공유는 큐 하나(mutex + condvar). 샘플러는 절대 블록되지 않도록 drop-oldest. 업로더는 배치로 꺼내 락 밖에서 전송.

**5) 실패와 복구**

| 실패 | 대응 |
|---|---|
| 링크 단절 (분~시간) | 지수 백오프 + 지터로 재연결. RAM 큐가 차면 플래시로 spill. 플래시도 상한 — 오래된 것부터 버리고 카운트 |
| 플래시 수명 | 매 샘플마다 쓰지 않고 배치로 append. 쓰기 양 상한 (Don의 SSD wear 지식) |
| 전원 손실 | spill 파일은 "임시 파일에 쓰고 fsync 후 rename"으로 원자적으로. 재부팅 후 반쯤 쓴 파일이 남지 않게 |
| 센서 NACK/무응답 | 드라이버가 재시도 후 에러 반환(7장). 서비스는 "센서 오류" 상태를 텔레메트리로 보고 |
| 시계가 틀림 (부팅 직후 NTP 전) | 샘플에 monotonic 타임스탬프 + 부팅 ID를 붙이고, 시각 동기화 후 서버가 보정 |
| 중복 전송 | 샘플마다 시퀀스 번호. 서버에서 중복 제거(at-least-once) |

**6) 관측성**: `samples`, `dropped_ram`, `dropped_flash`, `upload_ok`, `upload_fail`, `retries`, `queue_depth`, `spill_bytes`, `last_error`. 이것도 같이 업로드한다.

**7) 테스트 + trade-off**: 샘플러·업로더 로직은 가짜 센서·가짜 네트워크·가짜 시계로 단위 테스트. 링크 단절은 네트워크 결함 주입. Trade-off: "배치가 크면 전력·요금이 줄지만 지연이 늘고, 전원 손실 시 잃는 양이 커진다. 30초 배치 + 즉시 전송되는 알람 경로로 절충하겠다."

### 8.5 설계 예 2 — WAN failover (Ethernet ↔ LTE, 듀얼 SIM)

> "게이트웨이가 살아 있는 인터넷 경로로 자동 전환하게 설계하라."

- **구조**: 인터페이스마다 **모니터**(링크 상태, health check) → 이벤트 큐 → **단일 의사결정 스레드**(상태머신) → 라우팅 변경. 라우팅 테이블을 이 스레드만 만지니 락이 필요 없다.
- **health check는 ping이 아니라 실제 서비스**: 캐리어가 ICMP를 막거나 walled garden(요금 미납 시 포털로 리다이렉트)일 수 있다. Command 클라우드 엔드포인트로의 HTTPS 요청이 성공해야 "살아 있음"으로 본다.
- **히스테리시스**: 한 번 실패로 전환하지 않는다. "3회 연속 실패 → 강등, 5회 연속 성공 → 복귀" 식으로 플래핑(왔다 갔다)을 막는다.
- **듀얼 SIM**: 등록 실패 / 데이터 불통 / 품질 저하를 구분하고, SIM 전환에는 쿨다운(모뎀 재등록에 수십 초)을 둔다.
- **관측성**: 전환 횟수, 각 경로 체류 시간, 마지막 전환 이유.
- **테스트**: 상태머신을 순수하게 만들어 "이 이벤트 순서면 이 상태"를 표로 테스트. 가짜 시계로 히스테리시스 타이밍 검증.

### 8.6 설계 예 3 — fleet OTA (200만 대)

> "펌웨어 업데이트를 안전하게 배포하라."

- **A/B 파티션**: 비활성 슬롯에 새 이미지를 쓰고, 다 쓴 뒤 부트 설정만 원자적으로 바꾼다. 쓰는 도중 전원이 나가도 현재 슬롯은 멀쩡하다.
- **서명 검증**: 이미지 서명을 부트로더와 업데이터 양쪽에서 확인(secure boot 체인).
- **자동 롤백**: 새 슬롯으로 부팅 → 부트 카운터 증가 → 헬스 체크(클라우드 연결, 핵심 서비스 기동) 통과 시에만 "커밋". 통과 못 하면 watchdog 리셋 후 카운터 초과로 이전 슬롯 부팅.
- **단계적 배포**: 1% → 10% → 100%, 각 단계에서 크래시율·오프라인 비율을 보고 자동 중단.
- **셀룰러 제약**: delta 업데이트로 전송량 절감, 배포 시간대 분산.
- Verkada의 과거 보안 사고 이력을 생각하면 **서명·보안을 먼저 말하는 것**이 좋은 인상을 준다.

### 8.7 trade-off 어휘집 — 설계 답변에서 계속 쓸 문장들

| 축 | 한쪽 | 다른 쪽 |
|---|---|---|
| 손실 vs 지연 | 블록해서 손실 0 | 버려서 지연 0 |
| 메모리 vs 지연 | 버퍼를 늘려 여유 | 버퍼를 줄여 신선도 |
| 전력·요금 vs 지연 | 크게 배치 | 즉시 전송 |
| 단순성 vs 처리량 | 큰 락 하나 | 쪼갠 락, lock-free |
| 일관성 vs 가용성 | 확인될 때까지 대기 | 로컬에서 먼저 처리 후 동기화 |
| 유연성 vs 비용 | 런타임 주입(함수 포인터) | 컴파일 타임 선택 |

> **면접 문장 틀**: "I'm choosing **X** over **Y** because **[요구사항]**. The cost is **[포기한 것]**, which I'd mitigate with **[보완책]**. If the requirement were **[다른 조건]**, I'd switch to **Y**."

```check
Q: 설계 답변 7단계를 순서대로 말하라.
A: 요구사항·제약 확인 → 데이터 흐름 → 인터페이스 → 동시성 모델 → 실패와 복구 → 관측 가능성 → 테스트와 trade-off 요약.
Q: 센서→클라우드 설계에서 샘플러와 업로더 사이 공유 상태를 무엇으로 두고, 샘플러 쪽 정책은 무엇인가? 이유는?
A: bounded queue 하나(mutex + condvar). 샘플러는 절대 블록되면 안 되므로(고정 주기) 큐가 차면 drop-oldest로 가장 오래된 샘플을 버리고 카운트한다. 업로더는 배치로 꺼내 락 밖에서 전송한다.
Q: WAN failover에서 라우팅 테이블 변경에 락이 필요 없게 만드는 구조는?
A: 모니터들이 이벤트를 큐로 보내고, 단일 의사결정 스레드(상태머신)만 라우팅을 바꾼다. 자원을 한 스레드가 소유하므로 공유가 없다.
Q: health check를 ping으로 하면 안 되는 이유는?
A: 캐리어가 ICMP를 막거나, 요금 문제 등으로 walled garden에 걸리면 ping은 되거나 안 되거나 실제 서비스 가용성과 무관하다. 실제 클라우드 엔드포인트로의 HTTPS 성공 여부로 판단해야 한다.
Q: OTA에서 "새 펌웨어가 부팅은 되는데 클라우드에 못 붙는" 경우 어떻게 복구되나?
A: 새 슬롯은 헬스 체크(클라우드 연결 포함)를 통과해야만 커밋된다. 통과 못 하면 watchdog 리셋, 부트 카운터가 한도를 넘으면 부트로더가 이전 슬롯으로 되돌린다.
Q: 설계에서 "알아서 정하세요"라는 답을 들으면 어떻게 하나?
A: 합리적인 가정을 소리 내어 선언하고 진행한다. 예: "그럼 샘플은 64바이트, 링크는 최대 6시간 끊길 수 있고, 통계 데이터라 일부 손실은 허용된다고 가정하겠습니다."
```

> **연습 연결**: [빈출 10문제 문서](verkada_concurrency_top10.html)의 3장(시스템 설계 3제)을 화이트보드나 종이에 각 15분씩 그려 본다. 소리 내어 7단계를 말하면서.

---

## 9장. JD 보강 — Embedded Linux·네트워킹·셀룰러 최소 개념

### 9.1 왜 필요한가

메일의 네 주제에는 없지만, 공고 제목이 **Embedded Linux Engineer — Connectivity**다. 설계 파트나 후속 라운드에서 이 단어들이 나오면 **대화가 가능한 수준**이면 된다. 깊이 파는 장이 아니라, 각 개념을 한 문단과 면접 문장 하나로 정리한다.

### 9.2 Embedded Linux

| 개념 | 한 줄 설명 | 면접에서 쓸 문장 |
|---|---|---|
| 부트 체인 | ROM → SPL → U-Boot → 커널 → rootfs → init(systemd) | "I've brought up bare-metal boot from ROM through DRAM init, so I know what U-Boot and the kernel are abstracting." |
| device tree | 보드에 어떤 하드웨어가 어디 붙었는지 커널에 알려주는 데이터 | 드라이버 코드를 바꾸지 않고 보드 차이를 표현한다 |
| kernel vs user space | 드라이버는 커널, 데몬은 사용자 공간. `/dev`, `/sys`, `ioctl`로 연결 | I2C는 커널 드라이버를 쓰거나 `/dev/i2c-N`으로 사용자 공간에서 직접 |
| systemd | 서비스 기동·재시작·의존성 관리 | 데몬이 죽으면 자동 재시작, `WatchdogSec`로 멈춤 감지 |
| watchdog | 일정 시간 안에 "살아 있음"을 못 보내면 리셋 | GW31-E의 "오프라인 30분이면 자가 재부팅"이 이 계열 |
| poll / epoll | 여러 fd를 한 스레드에서 기다리기 | 8.3의 이벤트 루프. epoll은 fd가 많을 때 효율적 |
| timerfd / signalfd | 타이머·시그널을 fd로 받아 이벤트 루프에 통합 | 시그널 핸들러에서 할 일을 루프 안으로 옮긴다 |
| 원자적 파일 교체 | 임시 파일 → fsync → rename | 전원이 나가도 설정 파일이 반쯤 써진 상태로 남지 않는다 |

### 9.3 네트워킹

| 개념 | 한 줄 설명 | 게이트웨이에서의 의미 |
|---|---|---|
| TCP vs UDP | 순서·재전송 보장 vs 가볍고 손실 허용 | 텔레메트리는 TCP(HTTPS/MQTT), 영상 실시간은 UDP 계열 |
| NAT / CGNAT | 사설 주소를 공인 주소로 변환. 셀룰러는 캐리어 수준 NAT | 외부에서 게이트웨이로 **들어올 수 없다** → 기기가 클라우드로 나가는 연결을 유지 |
| keepalive | 유휴 연결에 주기적으로 신호 | CGNAT가 유휴 매핑을 수 분 만에 지운다 → keepalive로 연결 유지 |
| MTU / PMTUD | 한 번에 보낼 수 있는 패킷 크기, 경로 MTU 탐지 | LTE는 MTU가 작을 수 있어 큰 패킷이 조용히 사라짐 → "Ethernet에선 되는데 LTE에서만 끊김"의 단골 원인 |
| 라우팅 metric / policy routing | 여러 경로 중 우선순위, 조건별 경로 선택 | 8.5 failover의 실행 수단 (`ip route`, `ip rule`) |
| DHCP (LAN 쪽) | 연결된 장치에 주소 배포 | 게이트웨이가 PoE로 붙은 카메라에 주소를 준다 |

### 9.4 셀룰러

| 개념 | 한 줄 설명 |
|---|---|
| AT 명령 | 모뎀과 텍스트로 대화. `AT+CSQ`(신호), `AT+CREG?`(등록 상태), `AT+COPS?`(사업자), `AT+CGDCONT`(APN) |
| QMI / MBIM | 더 구조화된 모뎀 제어 프로토콜. Linux에서는 ModemManager·`qmicli`·`mmcli` |
| RSSI / RSRP / RSRQ / SINR | 신호 세기 지표. RSRP는 기준 신호 세기, RSRQ는 품질, SINR은 잡음 대비 신호 |
| APN | 캐리어 데이터망에 접속하는 설정 |
| 듀얼 SIM failover | 등록 실패 / 데이터 불통 / 품질 저하 기준으로 다른 SIM으로 전환 (GC31-E) |

> **면접 전략**: 이 영역은 Don의 약점이다. 모르는 걸 아는 척하지 말고 **"하드웨어 수준에서는 RF 프론트엔드 통합을 해 봤고, 호스트 쪽 모뎀 제어 스택은 지금 공부하고 있다 — 예를 들어 CGNAT 때문에 게이트웨이가 아웃바운드 연결을 유지해야 한다는 것"**처럼, 아는 것을 정확히 말하고 배우는 중인 것을 구체적으로 말한다.

```check
Q: 게이트웨이가 LTE 뒤에 있을 때 클라우드가 게이트웨이에 직접 접속할 수 없는 이유와, 그래서 택하는 구조는?
A: 셀룰러는 캐리어 수준 NAT(CGNAT) 뒤에 있어 공인 주소로 들어오는 연결을 받을 수 없다. 그래서 게이트웨이가 클라우드로 나가는 연결(outbound)을 열어 두고 keepalive로 유지하며, 명령은 그 연결로 받는다.
Q: "Ethernet에서는 멀쩡한데 LTE에서만 영상이 끊긴다"의 흔한 원인 하나는?
A: MTU 차이. LTE 경로의 MTU가 작아 큰 패킷이 단편화되거나 ICMP가 막혀 PMTUD가 실패하면 패킷이 조용히 사라진다. 그 외 업링크 대역폭 부족, CGNAT 유휴 타임아웃도 후보다.
Q: 설정 파일을 전원 손실에 안전하게 저장하는 방법은?
A: 임시 파일에 전부 쓰고 fsync한 뒤 rename으로 교체한다. rename은 원자적이라 옛 파일 또는 새 파일 둘 중 하나만 존재하고, 반쯤 쓴 파일은 남지 않는다.
```

> **연습 연결**: 문제 은행 [05 임베디드 Linux I/O](verkada_prep/index.html)(poll 루프, 부분 읽기, self-pipe, 원자적 저장)와 [06 네트워크·프로토콜 파싱](verkada_prep/index.html)(IPv4/CIDR, 체크섬, AT 응답 파서). 복습 노트 [05](notes_site/review/05_linux_io.html) · [06](notes_site/review/06_net_parsing.html).

---

## 10장. 인터뷰 당일 — 생각을 말로 보여주는 법

### 10.1 왜 필요한가

메일의 채점 기준 네 개 중 두 개(trade-off 정당화, 커뮤니케이션)는 **말하는 방식**으로 결정된다. 아는 것을 보여주는 기술은 따로 연습해야 한다.

### 10.2 코딩 파트 45분 배분

| 시간 | 할 일 | 말할 것 |
|---|---|---|
| 0–3분 | 요구사항 확인 | "How many producers and consumers? Bounded — and if full, block or drop? How does shutdown work?" |
| 3–6분 | API 먼저 | "Let me define the interface first: init, push, pop, close." |
| 6–8분 | 동기화 전략 선언 | "One mutex protects the state; two condition variables, not_full and not_empty." |
| 8–30분 | 구현하며 내레이션 | "I'm using while here because of spurious wakeups." |
| 30–38분 | edge case 스스로 점검 | 아래 체크리스트를 소리 내어 |
| 38–45분 | 테스트 전략 + 확장 | "I'd test with an invariant-based stress test and run it under ThreadSanitizer." |

### 10.3 채점 기준별로 할 행동

| 기준 | 행동 |
|---|---|
| 설계 결정과 trade-off | 선택할 때마다 "X를 골랐다, 이유는 Y, 포기한 것은 Z" |
| 깨끗한 코드 | 짧은 함수, 불변식 주석 한 줄, 에러 경로 출구 하나 |
| edge case와 견고성 | 면접관이 묻기 **전에** 체크리스트를 먼저 꺼낸다 |
| 커뮤니케이션 | 코드 치기 전 30초 설계 요약, 치면서 내레이션, 막히면 생각을 소리 내어 |

### 10.4 edge case 체크리스트 — 동시성 코드를 다 쓴 뒤 소리 내어

- 빈 상태에서 pop / 가득 찬 상태에서 push
- 가짜 기상 — 모든 wait가 while 안에 있나
- 종료: 잠든 스레드 전원 기상(broadcast), 종료 후 push 거부, 남은 항목 drain
- 이중 close / 이중 shutdown — 멱등한가
- join 전에 자원을 해제하지 않았나
- 락을 쥔 채 I/O·콜백·블로킹을 하지 않나
- 모든 에러 경로에서 락이 풀리나
- 버리는 데이터는 카운트되나
- 용량 0, NULL 인자 같은 입력 검증

### 10.5 그대로 쓸 수 있는 문장들

- "Before I code, let me restate the requirements and my assumptions."
- "I'll start with the simplest correct design — one mutex — and optimize only if we have a measured bottleneck."
- "I protect ownership of the buffers, not the data itself, so the critical section only swaps indices."
- "A dropped sample is fine; a silently dropped sample is a bug — so I export a counter."
- "volatile won't help here; it gives no atomicity or ordering. I'll use a release store and an acquire load."
- "The trade-off is memory versus latency. Given the constraint you mentioned, I'd pick…"
- "In my SSD firmware work, the host command queue feeding NAND operations was exactly this producer-consumer pattern."

### 10.6 막혔을 때

1. 침묵하지 않는다. **지금 무엇을 생각하는지** 말한다: "I'm deciding whether the close flag needs to be part of the wait predicate…"
2. 더 단순한 버전부터 만든다: "Let me first make it correct with a single lock, then we can discuss lock-free."
3. 모르는 API는 추측하지 않는다: "I don't remember the exact signature — the idea is X, and I'd check the man page."
4. 힌트는 감사히 받고 바로 반영한다. 힌트를 받는 태도도 평가 대상이다.

### 10.7 언어 선택

- **C**: Don의 가장 강한 언어. pthread API를 정확히 쓰면 된다.
- **C++**: `std::mutex`, `std::unique_lock`, `std::condition_variable::wait(lock, pred)`, RAII로 락 해제 보장. 코드가 짧아지고 예외 안전성을 말할 수 있다.
- 어느 쪽이든 **리크루터에게 미리 확인**한다.

```check
Q: 코드를 치기 전 첫 3분 동안 해야 할 질문 세 가지는?
A: 생산자·소비자 수, 가득 찼을 때 블록인지 drop인지(용량 포함), 종료 방식(남은 항목 처리 여부). 그 외 항목 소유권(복사 vs 포인터)도 좋다.
Q: 동시성 코드를 다 쓴 뒤 스스로 점검할 edge case 다섯 가지 이상.
A: 빈 pop, 가득 찬 push, 가짜 기상(while), 종료 시 broadcast와 push 거부·drain, 이중 close 멱등성, join 전 해제 금지, 락 안의 I/O 금지, 에러 경로의 unlock, drop 카운트, 입력 검증.
Q: trade-off를 말할 때 쓰는 문장 틀은?
A: "I'm choosing X over Y because [요구사항]. The cost is [포기한 것], mitigated by [보완책]. If [조건이 달라지면], I'd switch to Y."
```

---

## 11장. 학습 계획과 최종 점검

### 11.1 이 가이드와 연습 자료를 함께 쓰는 순서

| 순서 | 읽기 (이 가이드) | 손으로 (연습) |
|---|---|---|
| 1 | 0~2장 | `verkada_prep` → `make prob N=01_threads_mutex` |
| 2 | 3~4장 | ★ `make prob N=02_condvar_queues` · `concurrency_practice` → `make run N=01` |
| 3 | 5장 | `make prob N=03_atomics_lockfree` · `make run N=02` |
| 4 | 6장 | ★ `make prob N=04_realtime_buffers` · `make run N=03` |
| 5 | 7장 | ★ `make prob N=07_modular_design` · `make run N=10` |
| 6 | 8장 | 설계 3제를 종이에 각 15분 |
| 7 | 9장 | `make prob N=05_linux_io` · `make prob N=06_net_parsing` (시간 있으면) |
| 8 | 10장 | 빈 파일에 bounded queue를 45분 타이머로 처음부터 |

각 연습 후에는 `make tsan N=...`으로 레이스가 없는지까지 확인한다.

### 11.2 남은 날짜별 압축 플랜

| 남은 기간 | 계획 |
|---|---|
| **3일** | D-3: 1~4장 + 02 세트. D-2: 5~7장 + 04·07 세트. D-1: 8·10장 + bounded queue 백지 작성 2회 |
| **5일** | 위 3일 플랜에 03 세트(atomic), 9장과 05·06 세트, 설계 3제 화이트보드 연습 추가 |
| **당일 아침** | 10장 전체 다시 읽기 · 4.7 종료 프로토콜 · 6.7 double vs triple 표 · bounded queue 한 번 타이핑 |

### 11.3 최종 셀프 체크

- [ ] thread-safe의 정의를 "불변식"이라는 단어를 써서 말할 수 있다
- [ ] data race와 race condition의 차이를 예시와 함께 말할 수 있다
- [ ] check-then-act(TOCTOU)를 PoE 예산 예시로 설명하고 API로 고칠 수 있다
- [ ] 데드락 4조건과 락 순서 해법, 우선순위 역전과 상속을 설명할 수 있다
- [ ] cond_wait가 뮤텍스를 받는 이유(lost wakeup)를 설명할 수 있다
- [ ] while인 이유 세 가지, signal vs broadcast를 설명할 수 있다
- [ ] bounded blocking queue를 백지에서 45분 안에 쓸 수 있다
- [ ] 종료 프로토콜 5단계를 순서대로 말할 수 있다
- [ ] volatile이 스레드 동기화에 부족한 이유를 세 가지로 말할 수 있다
- [ ] release/acquire를 "데이터 먼저, 깃발 나중"으로 설명할 수 있다
- [ ] SPSC 링버퍼가 락 없이 안전한 이유를 설명할 수 있다
- [ ] atomic과 mutex를 고르는 기준을 표 없이 말할 수 있다
- [ ] 더블 버퍼의 소유권 3상태를 그림으로 그리고, double vs triple trade-off를 말할 수 있다
- [ ] 데이터 성격(스트림/상태/프레임)에 따라 패턴을 고를 수 있다
- [ ] drop 정책 세 가지와 각각 맞는 데이터를 말하고, 카운터를 왜 두는지 말할 수 있다
- [ ] 주기 실행의 drift와 취소 문제, monotonic 시계를 설명할 수 있다
- [ ] 함수 포인터 구조체로 HAL과 시계를 주입하는 코드를 쓸 수 있다
- [ ] "하드웨어 없이 어떻게 테스트하나"에 3층으로 답할 수 있다
- [ ] 시스템 설계 7단계로 센서→클라우드 파이프라인을 15분 안에 설명할 수 있다
- [ ] CGNAT와 keepalive, MTU 문제를 한 문장씩 설명할 수 있다
- [ ] 리크루터에게 인터뷰 날짜와 코딩 언어를 확인했다

> 이 체크리스트가 다 채워지면, 메일의 네 주제와 네 채점 기준을 모두 한 번 이상 **말로** 연습한 상태다. 마지막으로 [빈출 10문제 문서](verkada_concurrency_top10.html)를 훑으며 follow-up 질문에 답해 보면 준비가 끝난다.
