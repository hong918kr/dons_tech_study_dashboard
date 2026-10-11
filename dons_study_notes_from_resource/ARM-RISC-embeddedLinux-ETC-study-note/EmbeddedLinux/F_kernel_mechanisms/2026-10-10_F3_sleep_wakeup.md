# F3. 잠자기와 깨우기 — wait queue, wait_event, completion, schedule()

> **이 노트를 다 읽으면**: 커널에서 "잔다"가 `__state` 변경 + runqueue에서 빠지기 + `schedule()`이고 "깨운다"가 `try_to_wake_up()`이라는 것을 함수 이름으로 설명할 수 있다 · wait queue로 blocking `read()` + `poll()` + `O_NONBLOCK` + 시그널 처리 + 타임아웃을 갖춘 드라이버를 짤 수 있다 · lost-wakeup 경쟁이 왜 생기고 `wait_event`가 어떻게 막는지 순서도로 그릴 수 있다 · "IRQ/DMA 완료를 기다린다"를 completion으로 올바르게(재초기화·타임아웃·늦은 IRQ 포함) 구현할 수 있다
> **왜 필요한가**: 드라이버 면접의 단골 과제 "Implement a blocking read in a character driver"가 이 노트 한 장이고, 실무에서 "프로세스가 D 상태로 멈춰 kill -9도 안 먹는다", "가끔 read가 수 초씩 늦게 깬다" 같은 버그가 전부 여기서 나온다
> **Don 기준 난이도**: RTOS의 세마포어 pend/post, 이벤트 플래그, ISR에서 `GiveFromISR`로 태스크 깨우기는 이미 손에 익었다 / 새로 배울 부분은 Linux가 그것을 **조건(condition) + 대기열(wait queue) + 태스크 상태**로 쪼개 놓은 방식, 시그널이 끼어드는 규칙(`-ERESTARTSYS`), 멀티코어에서 "검사 후 잠들기" 순서가 틀리면 생기는 경쟁
> **선행 노트**: A2(프로세스 상태 S/D), A4(poll/epoll), A6(시그널), E3(커널 모듈), E6(`-ERESTARTSYS`와 시그널 재시작), F1(스케줄러), F2(실행 컨텍스트) — F4(락), F6(인터럽트), G4/G6/G7(char device, poll, misc device)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

센서 키트의 IMU 드라이버를 생각해 보자. 유저 데몬이 `read(fd, buf, 64)`를 불렀는데 FIFO에 샘플이 아직 없다. 드라이버가 할 수 있는 일은 셋 뿐이다.

1. 바로 `-EAGAIN`을 돌려준다 (`O_NONBLOCK`으로 열었을 때).
2. 데이터가 올 때까지 **CPU를 붙잡고 돈다** (busy-wait) — 커널에서는 거의 항상 틀린 답이다. 그 CPU의 다른 모든 일이 멈춘다.
3. 호출한 태스크를 **재운다**. 데이터가 오면(인터럽트 핸들러나 다른 쓰기 경로가) **깨운다**.

3번이 이 노트의 주제다. Don에게는 새로운 개념이 아니다. FreeRTOS에서 `xSemaphoreTake(s, portMAX_DELAY)`를 부르면 태스크가 Ready 리스트에서 빠져 세마포어의 대기 리스트로 옮겨지고, ISR이 `xSemaphoreGiveFromISR(s)`를 부르면 다시 Ready 리스트로 돌아온다. Linux도 구조는 똑같다. 다른 점은 Linux가 이것을 **세 부품**으로 쪼갰다는 것이다.

- **조건(condition)**: "FIFO가 비어 있지 않다" 같은 C 표현식. 세마포어 카운트 대신 드라이버가 가진 아무 상태나 쓴다.
- **wait queue**: "이 조건을 기다리는 태스크들" 목록. `wait_queue_head_t` 하나가 RTOS 세마포어의 대기 리스트에 해당한다.
- **태스크 상태 + 스케줄러**: 잠든 태스크는 `__state`가 `TASK_INTERRUPTIBLE` 등으로 바뀌고 runqueue(=Ready 리스트)에서 빠진다. 깨우는 쪽은 `try_to_wake_up()`으로 다시 runqueue에 넣는다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="320" height="260" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="44" text-anchor="middle" font-size="14" font-weight="bold">RTOS (FreeRTOS 식)</text>
  <rect x="30" y="60" width="280" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="85" text-anchor="middle" font-size="13">Ready 리스트</text>
  <rect x="30" y="180" width="280" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="205" text-anchor="middle" font-size="13">세마포어 s의 대기 리스트</text>
  <line x1="110" y1="100" x2="110" y2="178" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="104" y="135" text-anchor="end" font-size="12">Take(s, 무한)</text>
  <text x="104" y="152" text-anchor="end" font-size="12">카운트 0</text>
  <line x1="230" y1="178" x2="230" y2="102" stroke="#3f9a6b" marker-end="url(#ah1)"/>
  <text x="238" y="135" font-size="12">GiveFromISR</text>
  <text x="238" y="152" font-size="12">ISR이 옮김</text>
  <text x="170" y="250" text-anchor="middle" font-size="12">카운트 = 조건, 리스트 = 대기열</text>
  <text x="170" y="268" text-anchor="middle" font-size="12">한 객체 안에 묶여 있다</text>
  <rect x="350" y="20" width="320" height="260" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="44" text-anchor="middle" font-size="14" font-weight="bold">Linux 드라이버</text>
  <rect x="370" y="60" width="280" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="85" text-anchor="middle" font-size="13">runqueue (CPU마다 하나)</text>
  <rect x="370" y="180" width="280" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="205" text-anchor="middle" font-size="13">wait_queue_head_t rq + 상태 S</text>
  <line x1="450" y1="100" x2="450" y2="178" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="444" y="135" text-anchor="end" font-size="12">wait_event</text>
  <text x="444" y="152" text-anchor="end" font-size="12">조건 거짓</text>
  <line x1="570" y1="178" x2="570" y2="102" stroke="#3f9a6b" marker-end="url(#ah1)"/>
  <text x="578" y="135" font-size="12">wake_up(&amp;rq)</text>
  <text x="578" y="152" font-size="12">ttwu</text>
  <text x="510" y="250" text-anchor="middle" font-size="12">조건 = 드라이버 상태(아무 식)</text>
  <text x="510" y="268" text-anchor="middle" font-size="12">대기열 · 상태 · 스케줄러가 분리</text>
</svg>
```
그림 1 — RTOS 세마포어 pend/post와 Linux wait queue는 같은 동작이다. Linux는 "무엇을 기다리나(조건)"와 "누가 기다리나(wait queue)"를 분리해서, 드라이버가 가진 어떤 상태든 조건으로 쓸 수 있게 했다. 대신 **조건 검사와 잠들기 사이의 순서**를 드라이버(또는 매크로)가 지켜야 한다.

이 노트에서 하는 실습:

| 절 | 내용 | 실습 |
|---|---|---|
| §1 | "잔다"의 정체: 태스크 상태, `schedule()`, `try_to_wake_up()` | 커널 소스 확인 |
| §2 | wait queue API와 실습 드라이버 `f3_queue` | 예제 1 (빌드), 예제 2 (EAGAIN · blocking read · poll) |
| §3 | 잠든 태스크 관찰: `/proc/<pid>/stack`, `wchan`, S/D/I, `kill -9`, hung task | 예제 3, 4, 5 |
| §4 | 시그널과 `-ERESTARTSYS`, 타임아웃 | 예제 6 |
| §5 | `wait_event` 내부와 lost-wakeup 경쟁 | 예제 7 (버그를 일부러 재현하고 고치기) |
| §6 | exclusive wait와 thundering herd | 예제 8 |
| §7 | completion — "IRQ/DMA 끝날 때까지 기다리기" | 예제 9, 10 |
| §8 | ftrace로 본 sleep → wakeup → 실행 지연 | 예제 11 + 차트 |

> 실행 환경: 커널 모듈과 테스트는 Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`) 안의 공용 랩에서 **디버그 커널** `/var/lab/out/Image-dbg`(`6.18.55-dbg`: lockdep, `DEBUG_ATOMIC_SLEEP`, ftrace, hung task 감지기 포함, `CONFIG_PREEMPT=y`, `CONFIG_HZ=250`)로 QEMU `virt`(`-cpu cortex-a57 -smp 2 -m 512M`, TCG 에뮬레이션)를 부팅해 돌렸다. 모듈은 `make -C /var/lab/out/linux-build-dbg M=$PWD`로 빌드했고, 테스트 프로그램은 VM에서 `gcc -static -pthread`로 빌드해 initramfs에 넣었다. 커널 소스 인용은 `/var/lab/src/linux-6.18.55` 그대로다. **TCG 에뮬레이션이라 시간 값(특히 §8의 지연)은 실제 Cortex-A 보드보다 10배 이상 크다.** 비율과 모양만 읽자. PID·타임스탬프는 실행마다 다르다.

---

## 1. "잔다"는 것의 정체 — 상태 바꾸고, runqueue에서 빠지고, `schedule()`

### 1.1 직관 — 태스크는 "상태 필드 + 어느 리스트에 있나"다

RTOS 커널을 직접 짜 봤다면 태스크 블록(TCB)에 `state` 필드가 있고, 태스크가 Ready 리스트나 어떤 대기 리스트 중 하나에 매달려 있다는 걸 안다. Linux의 `task_struct`(F1)도 같다.

- `task_struct->__state`: 지금 상태. 0이면 `TASK_RUNNING`(실행 중이거나 실행 가능).
- runqueue: CPU마다 하나씩 있는 "실행 가능한 태스크" 집합. CFS/EEVDF는 red-black tree로 관리한다(F1).
- wait queue: 드라이버나 커널 서브시스템이 만든 "이걸 기다리는 태스크" 리스트.

"잠든다"는 다음 세 단계다.

1. 자기 상태를 `TASK_RUNNING`이 아닌 값으로 바꾼다 (`set_current_state(TASK_INTERRUPTIBLE)`).
2. 나중에 누가 찾을 수 있게 wait queue에 자기를 등록한다.
3. `schedule()`을 부른다. 스케줄러는 "상태가 RUNNING이 아니네"를 보고 이 태스크를 **runqueue에서 뺀(dequeue)** 뒤 다른 태스크로 문맥 교환한다.

"깨운다"는 반대다. 깨우는 쪽이 wait queue를 훑어 태스크를 찾고 `try_to_wake_up()`(줄여서 ttwu)을 부른다. ttwu는 상태를 `TASK_RUNNING`으로 바꾸고 태스크를 다시 runqueue에 넣는다. 그 CPU가 놀고 있으면 IPI로 깨운다. 잠들었던 태스크는 **`schedule()` 호출 바로 다음 줄에서** 실행을 이어 간다 — RTOS에서 `xSemaphoreTake()`가 리턴하는 것과 같다.

### 1.2 상태 값 — 커널 소스 그대로

`include/linux/sched.h` (6.18.55):

```c
/* Used in tsk->__state: */
#define TASK_RUNNING			0x00000000
#define TASK_INTERRUPTIBLE		0x00000001
#define TASK_UNINTERRUPTIBLE		0x00000002
...
#define TASK_WAKEKILL			0x00000100
#define TASK_WAKING			0x00000200
#define TASK_NOLOAD			0x00000400
...
#define TASK_KILLABLE			(TASK_WAKEKILL | TASK_UNINTERRUPTIBLE)
#define TASK_IDLE			(TASK_UNINTERRUPTIBLE | TASK_NOLOAD)
#define TASK_NORMAL			(TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)
```

잠자는 상태는 사실상 네 가지다. A2에서 `ps`의 S/D로 봤던 것의 커널 쪽 정체다.

| 상태 | `ps`/`status` 표시 | 깨우는 것 | loadavg 포함 | hung task 감지 대상 | 쓰는 곳 |
|---|---|---|---|---|---|
| `TASK_INTERRUPTIBLE` | S (sleeping) | `wake_up`, **모든 시그널** | 아니오 | 아니오 | 유저가 기다리는 I/O (read, poll) — 기본값 |
| `TASK_UNINTERRUPTIBLE` | D (disk sleep) | `wake_up`만. 시그널 무시 | 예 | **예** | 중간에 끊으면 안 되는 짧은 대기 (디스크 I/O, 일부 락) |
| `TASK_KILLABLE` | D (disk sleep) | `wake_up`, **치명적 시그널**(SIGKILL 등) | 예 | 아니오 | 끊기면 곤란하지만 `kill -9`는 먹어야 하는 대기 |
| `TASK_IDLE` | I (idle) | `wake_up`만 | 아니오 | 아니오 | 커널 스레드가 일감 없이 오래 쉴 때 |

`/proc/<pid>/status`의 문자열은 `fs/proc/array.c`의 표에서 온다:

```c
static const char * const task_state_array[] = {

	/* states in TASK_REPORT: */
	"R (running)",		/* 0x00 */
	"S (sleeping)",		/* 0x01 */
	"D (disk sleep)",	/* 0x02 */
	...
	/* states beyond TASK_REPORT: */
	"I (idle)",		/* 0x80 */
};
```

"disk sleep"은 역사적 이름일 뿐이다. 디스크와 상관없이 `TASK_UNINTERRUPTIBLE` 비트가 있으면 전부 D다 — `TASK_KILLABLE`도 D로 보인다. §3에서 직접 확인한다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="95" width="200" height="110" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="120" y="122" text-anchor="middle" font-size="14" font-weight="bold">TASK_RUNNING (R)</text>
  <text x="120" y="146" text-anchor="middle" font-size="12">runqueue 안에 있음</text>
  <text x="120" y="166" text-anchor="middle" font-size="12">CPU에서 실행 중이거나</text>
  <text x="120" y="186" text-anchor="middle" font-size="12">차례를 기다리는 중</text>
  <rect x="420" y="20" width="250" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="545" y="41" text-anchor="middle" font-size="13">S · TASK_INTERRUPTIBLE</text>
  <text x="545" y="61" text-anchor="middle" font-size="12">wake_up + 모든 시그널</text>
  <rect x="420" y="86" width="250" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="545" y="107" text-anchor="middle" font-size="13">D · TASK_KILLABLE</text>
  <text x="545" y="127" text-anchor="middle" font-size="12">wake_up + 치명적 시그널만</text>
  <rect x="420" y="152" width="250" height="52" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="545" y="173" text-anchor="middle" font-size="13">D · TASK_UNINTERRUPTIBLE</text>
  <text x="545" y="193" text-anchor="middle" font-size="12">wake_up만 (kill -9 무시)</text>
  <rect x="420" y="218" width="250" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="545" y="239" text-anchor="middle" font-size="13">I · TASK_IDLE</text>
  <text x="545" y="259" text-anchor="middle" font-size="12">wake_up만, load에 안 셈</text>
  <line x1="220" y1="115" x2="416" y2="115" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="318" y="95" text-anchor="middle" font-size="12">① set_current_state(S/D/I)</text>
  <text x="318" y="109" text-anchor="middle" font-size="12">+ wait queue 등록 + schedule()</text>
  <line x1="416" y1="185" x2="222" y2="185" stroke="#3f9a6b" marker-end="url(#ah2)"/>
  <text x="318" y="205" text-anchor="middle" font-size="12">② wake_up() → try_to_wake_up()</text>
  <text x="318" y="221" text-anchor="middle" font-size="12">상태 = RUNNING, runqueue로</text>
  <text x="120" y="240" text-anchor="middle" font-size="12">schedule()이 RUNNING이 아닌</text>
  <text x="120" y="258" text-anchor="middle" font-size="12">태스크를 runqueue에서 뺀다</text>
</svg>
```
그림 2 — 태스크 상태 전이. 잠드는 쪽(①)은 **자기 자신이** 상태를 바꾸고 `schedule()`을 부른다. 깨우는 쪽(②)은 다른 태스크나 IRQ가 ttwu를 부른다. 오른쪽 네 상태는 "무엇이 깨울 수 있나"만 다르다.

### 1.3 `schedule()`이 실제로 runqueue에서 빼는 곳

`kernel/sched/core.c`의 `__schedule()`에서 잠드는 태스크를 처리하는 부분:

```c
	} else if (!preempt && prev_state) {
		...
		try_to_block_task(rq, prev, &prev_state,
				  !task_is_blocked(prev));
		switch_count = &prev->nvcsw;
	}
```

그리고 `try_to_block_task()`의 앞부분:

```c
static bool try_to_block_task(struct rq *rq, struct task_struct *p,
			      unsigned long *task_state_p, bool should_block)
{
	unsigned long task_state = *task_state_p;
	int flags = DEQUEUE_NOCLOCK;

	if (signal_pending_state(task_state, p)) {
		WRITE_ONCE(p->__state, TASK_RUNNING);
		*task_state_p = TASK_RUNNING;
		return false;
	}
	...
	p->sched_contributes_to_load =
		(task_state & TASK_UNINTERRUPTIBLE) &&
		!(task_state & TASK_NOLOAD) &&
		!(task_state & TASK_FROZEN);
```

여기서 세 가지를 읽을 수 있다.

- `!preempt && prev_state`: **선점(preemption)으로 `__schedule()`에 들어온 경우에는 상태가 S여도 runqueue에서 빼지 않는다.** 그래서 `set_current_state()`와 `schedule()` 사이에 타이머 인터럽트로 선점돼도 태스크가 사라지지 않는다. 자발적으로 `schedule()`을 부를 때만 잠든다.
- `signal_pending_state()`: S 상태로 잠들려는데 이미 시그널이 와 있으면 잠들지 않고 RUNNING으로 돌린다. 시그널을 놓치지 않는 안전장치다.
- `sched_contributes_to_load`: D(`TASK_UNINTERRUPTIBLE`)는 loadavg에 들어가고, `TASK_NOLOAD`가 붙은 I는 빠진다. "CPU를 안 쓰는데 load가 높다"의 원인이 D 상태 태스크인 이유다(A2).

깨우는 쪽 `try_to_wake_up(struct task_struct *p, unsigned int state, int wake_flags)`는 먼저 `p->__state & state`를 확인한다(`ttwu_state_match`). 즉 `wake_up_interruptible()`(state = `TASK_INTERRUPTIBLE`)은 D 상태 태스크를 **깨우지 못한다**. §3.3에서 이 함정을 재현한다. 상태가 맞으면 태스크를 대상 CPU의 runqueue에 넣고, 대상 CPU가 다른 CPU면 보통 `ttwu_queue_wakelist()`로 그 CPU에 IPI를 보내 거기서 넣게 한다(§8의 trace에서 `sched_wakeup`이 하드IRQ 플래그 `h`와 함께 대상 CPU에서 찍히는 이유).

### 1.4 RTOS와 나란히

| RTOS (FreeRTOS 식) | Linux |
|---|---|
| `xSemaphoreTake(s, portMAX_DELAY)` | `wait_event_interruptible(wq, cond)` |
| `xSemaphoreTake(s, ticks)` | `wait_event_interruptible_timeout(wq, cond, jiffies)` |
| `xSemaphoreGiveFromISR(s, &woken)` + `portYIELD_FROM_ISR` | IRQ 핸들러에서 `wake_up(&wq)` (선점은 IRQ 복귀 때 커널이 알아서) |
| 이진 세마포어로 "DMA 완료" 알림 | `struct completion` + `complete()` / `wait_for_completion_timeout()` |
| `xEventGroupWaitBits(any)` | `poll()`/`epoll` + 드라이버 `.poll`의 `poll_wait()` (A4, G6) |
| `vTaskDelay()` | `msleep()` (F8) — 내부적으로 `schedule_timeout()` |
| 태스크 삭제 요청 | 시그널 — 잠든 태스크를 깨워 `-ERESTARTSYS`로 돌아오게 함 (A6, E6) |

RTOS에 없는 것은 **시그널**이다. RTOS 태스크는 대기 중에 누가 "그만 기다리고 나와"라고 할 수단이 보통 타임아웃뿐이다. Linux는 유저가 Ctrl-C를 누르면 잠든 `read()`가 깨어나 돌아와야 한다. 그래서 상태가 S/D/KILLABLE로 나뉘었다.

---

## 2. wait queue API와 실습 드라이버 `f3_queue`

### 2.1 부품 두 개: 머리(head)와 엔트리(entry)

```c
struct wait_queue_head {          /* include/linux/wait.h (요약) */
	spinlock_t		lock;
	struct list_head	head;     /* 엔트리들의 리스트 */
};

struct wait_queue_entry {
	unsigned int		flags;    /* WQ_FLAG_EXCLUSIVE 등 */
	void			*private; /* 보통 기다리는 task_struct */
	wait_queue_func_t	func;     /* 깨울 때 부를 함수 */
	struct list_head	entry;
};
```

- **head**는 드라이버가 하나 만든다. "이 장치의 읽기 대기열". 장치 구조체 안에 넣고 `init_waitqueue_head(&dev->rq)`로 초기화하거나, 전역이면 `DECLARE_WAIT_QUEUE_HEAD(rq)`로 정적 선언한다.
- **entry**는 잠드는 태스크마다 하나, 보통 **그 태스크의 커널 스택 위에** 만든다. `DEFINE_WAIT(wait)`(깨우면 리스트에서 자동 제거하는 `autoremove_wake_function` 사용)나 `DECLARE_WAITQUEUE(wait, current)`(옛 방식, `default_wake_function`, 직접 `add_wait_queue`/`remove_wait_queue`)로 만든다. `wait_event_*` 매크로를 쓰면 매크로 안에서 알아서 만든다.
- head의 `lock`은 스핀락이라서 `wake_up()`은 **IRQ 컨텍스트에서도 부를 수 있다** (F2). 반대로 `wait_event_*`는 잠들기 때문에 프로세스 컨텍스트에서만 쓴다.

### 2.2 기다리는 쪽 — `wait_event` 가족

모두 "조건이 참이 될 때까지 잔다"는 매크로다. 조건은 **매크로 안에서 여러 번 평가되는 C 표현식**이다 (함수 호출이면 여러 번 불린다 — §6에서 이 성질로 횟수를 센다).

| 매크로 | 잠자는 상태 | 반환값 | 언제 |
|---|---|---|---|
| `wait_event(wq, cond)` | D (`UNINTERRUPTIBLE`) | 없음 (void) | 아주 짧고 반드시 끝나는 대기. 유저 I/O에는 쓰지 말 것 |
| `wait_event_interruptible(wq, cond)` | S | 0 = 조건 참, `-ERESTARTSYS` = 시그널 | **유저가 부른 read/write의 기본값** |
| `wait_event_killable(wq, cond)` | D (`KILLABLE`) | 0, `-ERESTARTSYS` = 치명적 시그널 | 중간에 끊으면 상태가 꼬이지만 `kill -9`는 먹어야 할 때 |
| `wait_event_timeout(wq, cond, t)` | D | 0 = 시간 초과 + 조건 거짓, ≥1 = 남은 jiffies | 하드웨어 응답 대기 (커널 내부) |
| `wait_event_interruptible_timeout(wq, cond, t)` | S | 0 = 시간 초과, ≥1 = 남은 jiffies, `-ERESTARTSYS` | 타임아웃이 있는 유저 I/O |
| `wait_event_idle(wq, cond)` | I | 없음 | kthread의 긴 휴식 (load에 안 잡힘) |
| `wait_event_interruptible_exclusive(wq, cond)` | S + 배타적 | `_interruptible`과 같음 | 여럿이 기다리지만 한 명만 깨우면 될 때 (§6) |
| `wait_event_interruptible_lock_irq(wq, cond, lock)` | S | 같음 | 조건을 스핀락 아래서 검사해야 할 때 (잠들 때 풀고 깨면 다시 잡음) |

`_timeout` 계열 반환값은 헷갈리기 쉽다: 시간이 다 됐어도 **그 순간 조건이 참이면 1**을 돌려준다 (`include/linux/wait.h` 주석: "0 if the @condition evaluated to %false after the @timeout elapsed, ... the remaining jiffies (at least 1) if the @condition evaluated to %true"). 그래서 "0이면 타임아웃"만 검사하면 된다.

### 2.3 깨우는 쪽 — `wake_up` 가족

`include/linux/wait.h`:

```c
#define wake_up(x)			__wake_up(x, TASK_NORMAL, 1, NULL)
#define wake_up_all(x)			__wake_up(x, TASK_NORMAL, 0, NULL)
...
#define wake_up_interruptible(x)	__wake_up(x, TASK_INTERRUPTIBLE, 1, NULL)
#define wake_up_interruptible_all(x)	__wake_up(x, TASK_INTERRUPTIBLE, 0, NULL)
```

두 번째 인자는 **깨울 상태 마스크**, 세 번째는 **배타적(exclusive) 대기자를 몇 명까지 깨울지**(0 = 전부)다. 배타적이지 않은 대기자는 숫자와 상관없이 **전부** 깨운다 (§6).

| 함수 | 깨우는 상태 | 배타적 대기자 | 메모 |
|---|---|---|---|
| `wake_up(&wq)` | S, D (`TASK_NORMAL`) | 1명 | 무난한 기본값 |
| `wake_up_all(&wq)` | S, D | 전부 | 장치 제거·리셋처럼 모두 나와야 할 때 |
| `wake_up_interruptible(&wq)` | **S만** | 1명 | 대기 쪽이 전부 `_interruptible`일 때만 안전 |
| `wake_up_interruptible_all(&wq)` | S만 | 전부 | |
| `wake_up_poll(&wq, EPOLLIN)` | S, D | 1명 | poll/epoll 대기자에게 이벤트 키 전달 (G6) |

내부에서는 `__wake_up_common()`이 리스트를 훑으며 각 엔트리의 `func`을 부른다 (`kernel/sched/wait.c`):

```c
	list_for_each_entry_safe_from(curr, next, &wq_head->head, entry) {
		unsigned flags = curr->flags;
		int ret;

		ret = curr->func(curr, mode, wake_flags, key);
		if (ret < 0)
			break;
		if (ret && (flags & WQ_FLAG_EXCLUSIVE) && !--nr_exclusive)
			break;
	}
```

보통 `func`은 `autoremove_wake_function()` → `default_wake_function()` → `try_to_wake_up(curr->private, mode, wake_flags)`다. 즉 **wait queue는 "누구를 ttwu할지" 목록일 뿐**이고 실제 깨우기는 §1의 ttwu다.

### 2.4 실습 드라이버 `f3_queue` (1/4) — 상태와 모듈 파라미터

misc device(G7) 하나에 256바이트 `kfifo`를 둔다. `write()`가 넣고 `read()`가 꺼낸다. 비어 있으면 read가 잠들고, 꽉 차면 write가 잠든다 — 생산자/소비자 링버퍼다. 잠드는 방식은 모듈 파라미터 `wmode`로 바꿔 가며 실험한다 (`/sys/module/f3_queue/parameters/wmode`에 실행 중에 써 넣을 수 있다).

무엇을 확인하는 코드인가: 드라이버 상태(FIFO, 락, wait queue 두 개)와 "조건 함수". 조건을 함수로 감싸 평가 횟수를 debugfs로 센다.

```c
// SPDX-License-Identifier: GPL-2.0
/* f3_queue: blocking read/write FIFO char device - wait queue lab for note F3 */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

static int wmode;		/* how read() sleeps - see f3_wait_data() */
module_param(wmode, int, 0644);
static int tmo_ms;		/* wmode 0 only: >0 -> _timeout variant */
module_param(tmo_ms, int, 0644);
static int race_ms;		/* wmode 5/6: widen the check-then-sleep window */
module_param(race_ms, int, 0644);
static bool wake_intr;		/* writer uses wake_up_interruptible() (pitfall) */
module_param(wake_intr, bool, 0644);

static DEFINE_MUTEX(f3_lock);			/* protects f3_fifo */
static DEFINE_KFIFO(f3_fifo, char, 256);
static DECLARE_WAIT_QUEUE_HEAD(f3_rq);		/* readers wait for data */
static DECLARE_WAIT_QUEUE_HEAD(f3_wq);		/* writers wait for space */
static atomic_t f3_checks = ATOMIC_INIT(0);	/* how often the condition ran */
static struct dentry *f3_dir;

static bool f3_has_data(void)
{
	atomic_inc(&f3_checks);
	return !kfifo_is_empty(&f3_fifo);
}
```

### 2.5 `f3_queue` (2/4) — 잠드는 방법 일곱 가지

`f3_wait_data()`는 "데이터가 생길 때까지" 잔다. 0이면 데이터가 있다는 뜻이고, 음수면 호출자에게 돌려줄 에러다. 모드 5는 **일부러 넣은 버그**(§5), 모드 6은 그 버그를 고친 손으로 짠(open-coded) 루프다.

```c
/* 0 = data is there, <0 = error to hand back to the caller */
static long f3_wait_data(void)
{
	DEFINE_WAIT(wait);
	long t;

	switch (wmode) {
	case 0:
		if (tmo_ms <= 0)
			return wait_event_interruptible(f3_rq, f3_has_data());
		t = wait_event_interruptible_timeout(f3_rq, f3_has_data(),
						     msecs_to_jiffies(tmo_ms));
		return t > 0 ? 0 : (t == 0 ? -ETIMEDOUT : t);
	case 1:
		return wait_event_killable(f3_rq, f3_has_data());
	case 2:
		wait_event(f3_rq, f3_has_data());	/* TASK_UNINTERRUPTIBLE */
		return 0;
	case 3:
		wait_event_idle(f3_rq, f3_has_data());	/* TASK_IDLE */
		return 0;
	case 4:
		return wait_event_interruptible_exclusive(f3_rq, f3_has_data());
	case 5:	/* BUG: test condition, THEN get on the queue, THEN sleep */
		if (!f3_has_data()) {
			mdelay(race_ms);		/* writer's wake_up() lands here */
			prepare_to_wait(&f3_rq, &wait, TASK_INTERRUPTIBLE);
			schedule_timeout(2 * HZ);	/* real bug: schedule() = forever */
			finish_wait(&f3_rq, &wait);
		}
		return signal_pending(current) ? -ERESTARTSYS : 0;
	default: /* 6: correct open-coded loop: queue + state first, then test */
		for (;;) {
			prepare_to_wait(&f3_rq, &wait, TASK_INTERRUPTIBLE);
			mdelay(race_ms);		/* same window - now harmless */
			if (f3_has_data())
				break;
			if (signal_pending(current)) {
				finish_wait(&f3_rq, &wait);
				return -ERESTARTSYS;
			}
			schedule();
		}
		finish_wait(&f3_rq, &wait);
		return 0;
	}
}
```

### 2.6 `f3_queue` (3/4) — `read`와 `write`: 정석 패턴

무엇을 확인하는 코드인가: 면접에서 "blocking read를 짜 보라"고 할 때 쓰는 모양. 핵심 규칙은 세 개다. **락을 쥔 채로 잠들지 않는다**, **깨어나면 조건을 락 아래서 다시 검사한다**(다른 리더가 먼저 가져갔을 수 있다), **상태를 바꾼 뒤에 깨운다**.

```c
static ssize_t f3_read(struct file *f, char __user *buf, size_t n, loff_t *ppos)
{
	unsigned int copied;
	long ret;

	for (;;) {
		if (mutex_lock_interruptible(&f3_lock))
			return -ERESTARTSYS;
		if (!kfifo_is_empty(&f3_fifo))
			break;				/* keep the lock, go copy */
		mutex_unlock(&f3_lock);			/* never sleep holding it */
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = f3_wait_data();
		if (ret)
			return ret;			/* -ERESTARTSYS, -ETIMEDOUT */
	}
	ret = kfifo_to_user(&f3_fifo, buf, n, &copied);
	mutex_unlock(&f3_lock);
	if (ret)
		return ret;
	wake_up_interruptible(&f3_wq);			/* space freed */
	return copied;
}

static ssize_t f3_write(struct file *f, const char __user *buf, size_t n,
			loff_t *ppos)
{
	unsigned int copied;
	int ret;

	for (;;) {
		if (mutex_lock_interruptible(&f3_lock))
			return -ERESTARTSYS;
		if (!kfifo_is_full(&f3_fifo))
			break;
		mutex_unlock(&f3_lock);
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(f3_wq, !kfifo_is_full(&f3_fifo)))
			return -ERESTARTSYS;
	}
	ret = kfifo_from_user(&f3_fifo, buf, n, &copied);
	mutex_unlock(&f3_lock);
	if (ret)
		return ret;
	if (wake_intr)
		wake_up_interruptible(&f3_rq);		/* only TASK_INTERRUPTIBLE */
	else
		wake_up(&f3_rq);			/* TASK_NORMAL */
	return copied;
}
```

- `for (;;)` 루프가 `wait_event` 바깥에 한 번 더 있는 이유: `wait_event_interruptible()`은 **락 없이** 조건을 봤다. 깨어나서 락을 잡았을 때 다른 리더가 이미 데이터를 가져갔을 수 있다. 그래서 락 아래서 다시 본다. 이 이중 확인이 정석이다.
- `mutex_lock_interruptible()`도 잠들 수 있는 함수라 시그널에 `-ERESTARTSYS`를 돌려준다. 같은 규칙으로 처리한다.
- 깨우기는 FIFO를 바꾸고 락을 푼 **뒤에** 한다. 앞에서 깨우면 깨어난 리더가 아직 빈 FIFO를 보고 다시 잔다(헛깨움). 데이터가 사라지지는 않지만 낭비다.
- `wake_intr`가 켜지면 `wake_up_interruptible()`을 쓴다 — `wmode=1/2/3`(D·I 상태)와 섞으면 리더를 못 깨운다. §3.3에서 재현한다.

### 2.7 `f3_queue` (4/4) — `poll`과 등록

```c
static __poll_t f3_poll(struct file *f, poll_table *pt)
{
	__poll_t mask = 0;

	poll_wait(f, &f3_rq, pt);	/* register only - never sleeps here */
	poll_wait(f, &f3_wq, pt);
	if (!kfifo_is_empty(&f3_fifo))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (!kfifo_is_full(&f3_fifo))
		mask |= EPOLLOUT | EPOLLWRNORM;
	return mask;
}

static const struct file_operations f3_fops = {
	.owner	= THIS_MODULE,
	.read	= f3_read,
	.write	= f3_write,
	.poll	= f3_poll,
	.llseek	= noop_llseek,
};

static struct miscdevice f3_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "f3_queue",
	.fops	= &f3_fops,
	.mode	= 0666,
};

static int __init f3_init(void)
{
	int ret = misc_register(&f3_misc);

	if (ret)
		return ret;
	f3_dir = debugfs_create_dir("f3_queue", NULL);
	debugfs_create_atomic_t("checks", 0644, f3_dir, &f3_checks);
	pr_info("f3: /dev/f3_queue ready (wmode=%d)\n", wmode);
	return 0;
}

static void __exit f3_exit(void)
{
	debugfs_remove_recursive(f3_dir);
	misc_deregister(&f3_misc);
}
module_init(f3_init);
module_exit(f3_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("F3 wait queue lab: blocking FIFO");
```

`.poll`은 **절대 잠들지 않는다.** `poll_wait()`는 "이 wait queue에서 깨워 주면 다시 물어봐 달라"고 poll 테이블에 엔트리를 하나 걸어 둘 뿐이고, 지금 상태를 마스크로 바로 돌려준다. 실제로 잠드는 것은 VFS의 `do_poll()`/epoll 쪽이다. 그래서 드라이버의 `wake_up(&f3_rq)` 한 번이 `read()`에서 잠든 태스크와 `poll()`에서 잠든 태스크를 **둘 다** 깨운다 — 같은 wait queue에 매달려 있기 때문이다 (A4에서 본 poll의 커널 쪽, 자세히는 G6).

### 2.8 예제 1 — 빌드: 디버그 커널용 모듈

무엇을 확인하나: 4개 조각을 `f3_queue.c` 하나로 합치고 `f3_comp.c`, `f3_all.c`(§7)와 함께 디버그 커널 빌드 트리로 빌드한다. 경고가 없어야 한다.

```sh
# VM 안, /var/tmp/F3/mod 에서 (Kbuild: obj-m := f3_queue.o f3_comp.o f3_all.o)
make -C /var/lab/out/linux-build-dbg M=$PWD -j2 modules
modinfo -p f3_queue.ko; modinfo f3_queue.ko | grep vermagic
```

```text
make: Entering directory '/var/lab/build/linux-dbg'
make[1]: Entering directory '/var/tmp/F3/mod'
  CC [M]  f3_queue.o
  CC [M]  f3_comp.o
  CC [M]  f3_all.o
  MODPOST Module.symvers
  CC [M]  f3_queue.mod.o
  CC [M]  .module-common.o
  CC [M]  f3_comp.mod.o
  CC [M]  f3_all.mod.o
  LD [M]  f3_queue.ko
  LD [M]  f3_comp.ko
  LD [M]  f3_all.ko
make[1]: Leaving directory '/var/tmp/F3/mod'
make: Leaving directory '/var/lab/build/linux-dbg'
wmode: (int)
tmo_ms: (int)
race_ms: (int)
wake_intr: (bool)
vermagic:       6.18.55-dbg SMP preempt mod_unload aarch64
```

출력에서 볼 것: 경고 0개, `vermagic`이 `6.18.55-dbg`라서 `out/Image-dbg`에만 들어간다. 파라미터 네 개가 `/sys/module/f3_queue/parameters/`에 생긴다.

QEMU 부팅은 매 예제 같은 방식이다 (게스트 스크립트 `f3_run.sh`의 해당 절을 `LAB_CMD`로 실행하고 끝나면 전원이 꺼진다):

```sh
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 512M -nographic \
  -kernel /var/lab/out/Image-dbg -initrd /var/tmp/F3/f3_initrd.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/f3_run.sh~basic"
```

### 2.9 예제 2 — EAGAIN, 잠든 read를 write가 깨우기, poll

공용 헤더 (모든 테스트 프로그램이 쓴다):

```c
/* f3_common.h - helpers shared by the F3 test programs */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#define DEV "/dev/f3_queue"

static inline int open_dev(int flags)
{
	int fd = open(DEV, flags);
	if (fd < 0) { perror(DEV); exit(1); }
	return fd;
}

static inline double now(void)		/* seconds, CLOCK_MONOTONIC */
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static inline void show(const char *what, long r)
{
	if (r < 0)
		printf("%-34s -> -1 errno=%d %s\n", what, errno, strerrorname_np(errno));
	else
		printf("%-34s -> %ld\n", what, r);
}
```

무엇을 확인하나: ① `O_NONBLOCK`이면 빈 장치에서 즉시 `EAGAIN`, ② `poll()`은 데이터가 없으면 타임아웃까지 기다린다, ③ blocking `read()`가 잠들어 있다가 `write()`가 오면 바로 깬다, ④ 데이터가 있으면 `poll()`이 `POLLIN|POLLOUT`을 바로 돌려준다.

```c
/* f3_t1_basic: EAGAIN, blocking read woken by a writer, poll() readiness */
#include "f3_common.h"
#include <poll.h>

static double t0;

static void *reader(void *arg)
{
	char buf[32];
	int fd = open_dev(O_RDONLY);			/* blocking fd */
	long r = read(fd, buf, sizeof(buf));		/* sleeps here */
	printf("[reader] read returned %ld \"%.*s\" at t=%.3f s\n",
	       r, (int)(r > 0 ? r : 0), buf, now() - t0);
	close(fd);
	return arg;
}

int main(void)
{
	char buf[32];
	pthread_t th;
	int nb = open_dev(O_RDWR | O_NONBLOCK);
	struct pollfd p = { .fd = nb, .events = POLLIN };

	show("1) read(O_NONBLOCK) on empty", read(nb, buf, sizeof(buf)));
	t0 = now();
	show("2) poll(POLLIN, 200 ms) on empty", poll(&p, 1, 200));
	printf("   poll took %.3f s, revents=0x%x\n", now() - t0, p.revents);

	t0 = now();
	pthread_create(&th, NULL, reader, NULL);
	usleep(500 * 1000);				/* reader is asleep now */
	printf("[main] write at t=%.3f s\n", now() - t0);
	show("3) write(\"hello\")", write(nb, "hello", 5));
	pthread_join(th, NULL);

	show("4) write(\"abc\") (no reader)", write(nb, "abc", 3));
	show("   poll(POLLIN|POLLOUT, -1)",
	     (p.events = POLLIN | POLLOUT, poll(&p, 1, -1)));
	printf("   revents=0x%x (POLLIN=0x%x POLLOUT=0x%x)\n",
	       p.revents, POLLIN, POLLOUT);
	show("   read(O_NONBLOCK) now", read(nb, buf, sizeof(buf)));
	close(nb);
	return 0;
}
```

게스트에서:

```sh
uname -r; insmod /f3_queue.ko; ls -l --color=never /dev/f3_queue; ls --color=never /sys/module/f3_queue/parameters
./f3_t1_basic
```

```text
6.18.55-dbg
crw-rw-rw-    1 0        0          10, 258 Oct 10 22:27 /dev/f3_queue
race_ms    tmo_ms     wake_intr  wmode
1) read(O_NONBLOCK) on empty       -> -1 errno=11 EAGAIN
2) poll(POLLIN, 200 ms) on empty   -> 0
   poll took 0.211 s, revents=0x0
[main] write at t=0.527 s
3) write("hello")                  -> 5
[reader] read returned 5 "hello" at t=0.539 s
4) write("abc") (no reader)        -> 3
   poll(POLLIN|POLLOUT, -1)        -> 1
   revents=0x5 (POLLIN=0x1 POLLOUT=0x4)
   read(O_NONBLOCK) now            -> 3
```

출력에서 볼 것: 장치는 misc major 10. read는 0.5초 동안 잠들어 있다가 write 직후(약 10 ms 뒤 — TCG 에뮬레이션 기준) `"hello"`를 받았다. 리더 스레드는 그동안 CPU를 쓰지 않았다. 마지막 `revents=0x5`는 `f3_poll()`이 돌려준 `EPOLLIN | EPOLLOUT`이다 (`EPOLLRDNORM`/`EPOLLWRNORM`은 `poll()`의 `POLLIN`/`POLLOUT` 요청에는 따로 표시되지 않는다).

---

## 3. 잠든 태스크 들여다보기 — `/proc/<pid>/stack`, `wchan`, S/D/I, `kill -9`

### 3.1 예제 3 — 모드마다 상태가 어떻게 보이나

무엇을 확인하나: 같은 `cat /dev/f3_queue`를 `wmode` 0~3으로 재우고 `/proc/<pid>/status`, `/proc/<pid>/wchan`, `/proc/<pid>/stack`을 본 다음 `kill -9`를 보낸다. 안 죽으면 데이터를 써서 깨운다.

```sh
P=/sys/module/f3_queue/parameters
st() { grep State /proc/$1/status 2>/dev/null || echo "State:	(pid $1 gone)"; }
for m in 0 1 2 3; do
  echo $m > $P/wmode
  cat /dev/f3_queue & C=$!
  sleep 1
  echo "== wmode=$m pid=$C"; st $C; echo "wchan:	$(cat /proc/$C/wchan)"
  [ $m = 0 ] && { echo "--- /proc/$C/stack"; cat /proc/$C/stack; echo "--- ps"; ps -o pid,stat,comm 2>&1 | head -3; echo "stat field3: $(cut -d' ' -f3 /proc/$C/stat)"; }
  kill -9 $C; sleep 1; echo "after kill -9:"; st $C
  if grep -qE "^State:.*(D|I)" /proc/$C/status 2>/dev/null; then
    echo "x" > /dev/f3_queue; sleep 1; echo "after write:"; st $C
  fi
  wait $C 2>/dev/null
done
```

```text
== wmode=0 pid=74
State:	S (sleeping)
wchan:	f3_wait_data
--- /proc/74/stack
[<0>] f3_wait_data+0x54c/0x5a0 [f3_queue]
[<0>] f3_read+0x74/0x120 [f3_queue]
[<0>] vfs_read+0xcc/0x358
[<0>] ksys_read+0x6c/0x110
[<0>] __arm64_sys_read+0x20/0x38
[<0>] invoke_syscall.constprop.0+0x48/0xf0
[<0>] el0_svc_common.constprop.0+0x40/0xf0
[<0>] do_el0_svc+0x24/0x40
[<0>] el0_svc+0x50/0x2f8
[<0>] el0t_64_sync_handler+0xa0/0xe8
[<0>] el0t_64_sync+0x198/0x1a0
--- ps
PID   STAT COMMAND
    1 S    init
    2 SW   kthreadd
stat field3: S
after kill -9:
State:	(pid 74 gone)
== wmode=1 pid=85
State:	D (disk sleep)
wchan:	f3_wait_data
after kill -9:
State:	(pid 85 gone)
== wmode=2 pid=92
State:	D (disk sleep)
wchan:	f3_wait_data
after kill -9:
State:	D (disk sleep)
after write:
State:	(pid 92 gone)
== wmode=3 pid=101
State:	I (idle)
wchan:	f3_wait_data
after kill -9:
State:	I (idle)
after write:
State:	(pid 101 gone)
```

출력에서 볼 것:

- `wmode=0`(`wait_event_interruptible`)은 **S**, `wmode=1`(`_killable`)과 `wmode=2`(`wait_event`)는 둘 다 **D**, `wmode=3`(`wait_event_idle`)은 **I**. 커널 안에서 쓴 매크로가 `ps`에 그대로 드러난다.
- `wchan`(wait channel)은 "어느 함수에서 잠들었나"를 심볼 하나로 보여 준다. `wait_event_*`는 매크로라 `f3_wait_data` 안으로 인라인돼서 우리 함수 이름이 나온다. 실무에서 `ps -eo pid,stat,wchan:32,comm`으로 멈춘 프로세스를 볼 때 이 칸이 첫 단서다.
- `/proc/<pid>/stack`은 잠든 태스크의 **커널 스택**이다. 위에서 아래로 읽으면: `f3_wait_data` ← `f3_read` ← `vfs_read` ← `ksys_read` ← `__arm64_sys_read` ← … ← `el0t_64_sync`. E6에서 본 syscall 경로가 여기서 멈춰 있다. (BusyBox `ps -o`는 첫 세 줄만 잘랐다 — STAT 칸이 있다는 것만 보이고 우리 cat 줄은 잘렸다. 그래서 `/proc/<pid>/stat`의 3번째 필드로 다시 확인했다.)
- `kill -9`: S와 KILLABLE은 바로 죽었다(`gone`). **`wait_event`(UNINTERRUPTIBLE)과 `wait_event_idle`은 `kill -9`를 받고도 그대로** 있다가, 데이터를 써서 깨우자 그제서야 죽었다. SIGKILL은 "보류(pending)"로 쌓여 있다가 syscall이 끝나 유저로 돌아가는 길에 처리된다(E6 §2.4).

이것이 "kill -9로도 안 죽는 프로세스"의 정체다. 드라이버가 유저 I/O를 `wait_event()`(D)로 기다리는데 하드웨어가 응답을 안 하면 그 프로세스는 영원히 D로 남고, 재부팅 말고는 방법이 없다.

### 3.2 예제 4 — hung task 감지기는 누구를 잡나

무엇을 확인하나: 디버그 커널에는 `CONFIG_DETECT_HUNG_TASK=y`(기본 120초)가 있다. 게스트의 `hung_task_timeout_secs`를 3초로 줄이고 KILLABLE, UNINTERRUPTIBLE, IDLE 리더를 하나씩 재워 둔다. (게스트 안의 sysctl이라 공용 VM에는 영향이 없다.)

```sh
insmod /f3_queue.ko
echo 3 > /proc/sys/kernel/hung_task_timeout_secs
for m in 1 2 3; do echo $m > $P/wmode; cat /dev/f3_queue & sleep 1; done
ps -o pid,stat,args | grep "[c]at /dev"
sleep 10
dmesg | grep -A3 "blocked for more than" | head -12
```

```text
   74 D    cat /dev/f3_queue
   76 D    cat /dev/f3_queue
   78 I    cat /dev/f3_queue
[   17.019079] INFO: task cat:76 blocked for more than 3 seconds.
[   17.019340]       Tainted: G           O        6.18.55-dbg #1
[   17.019410] "echo 0 > /proc/sys/kernel/hung_task_timeout_secs" disables this message.
[   20.090614] INFO: task cat:76 blocked for more than 6 seconds.
[   20.090783]       Tainted: G           O        6.18.55-dbg #1
[   20.090827] "echo 0 > /proc/sys/kernel/hung_task_timeout_secs" disables this message.
[   23.162186] INFO: task cat:76 blocked for more than 9 seconds.
[   23.162261]       Tainted: G           O        6.18.55-dbg #1
[   23.162281] "echo 0 > /proc/sys/kernel/hung_task_timeout_secs" disables this message.
[   17.019079] INFO: task cat:76 blocked for more than 3 seconds.
[   17.019340]       Tainted: G           O        6.18.55-dbg #1
[   17.019410] "echo 0 > /proc/sys/kernel/hung_task_timeout_secs" disables this message.
[   17.019490] task:cat             state:D stack:0     pid:76    tgid:76    ppid:72     task_flags:0x400000 flags:0x00000008
--
[   20.090614] INFO: task cat:76 blocked for more than 6 seconds.
[   20.090783]       Tainted: G           O        6.18.55-dbg #1
[   20.090827] "echo 0 > /proc/sys/kernel/hung_task_timeout_secs" disables this message.
[   20.090853] task:cat             state:D stack:0     pid:76    tgid:76    ppid:72     task_flags:0x400000 flags:0x00000008
--
[   23.162186] INFO: task cat:76 blocked for more than 9 seconds.
[   23.162261]       Tainted: G           O        6.18.55-dbg #1
```

(콘솔로도 같은 메시지가 찍혀 앞부분과 `dmesg` 출력이 겹친다. 뒤쪽 중복 줄은 그대로 두었다.)

출력에서 볼 것: 세 리더 모두 잠들어 있지만 경고는 **pid 76(`wmode=2`, `wait_event`)만** 받았다. 74(KILLABLE)와 78(IDLE)은 조용하다. `kernel/hung_task.c`의 검사가 그렇게 짜여 있다:

```c
	if (!(state & TASK_UNINTERRUPTIBLE) ||
	    (state & (TASK_WAKEKILL | TASK_NOLOAD | TASK_FROZEN)))
		return;
```

즉 커널은 "`kill -9`도 안 먹는 D 상태로 오래 있는 것"만 문제로 본다. 양산 장비에서 `INFO: task xxx blocked for more than 120 seconds`가 찍히면 그 아래 콜 트레이스의 맨 위 함수가 범인 드라이버다.

### 3.3 예제 5 — 함정: `wake_up_interruptible()`은 D 상태를 못 깨운다

무엇을 확인하나: 리더는 `wait_event_killable()`(D)로 자는데 라이터는 `wake_up_interruptible()`을 부르면? §1.3의 `ttwu_state_match`가 상태 마스크로 걸러 낸다.

```sh
echo 1 > $P/wmode; echo Y > $P/wake_intr
cat /dev/f3_queue & C=$!
sleep 1; echo "x" > /dev/f3_queue; sleep 1; echo "after write #1:"; st $C
echo N > $P/wake_intr; echo "y" > /dev/f3_queue; sleep 1; echo "after write #2 (wake_up):"; st $C
```

```text
== pitfall: wmode=1 (killable) + wake_intr=Y
after write #1:
State:	D (disk sleep)
x
y
after write #2 (wake_up):
State:	D (disk sleep)
```

출력에서 볼 것: 첫 write 뒤에도 `cat`은 아무것도 출력하지 않았다 — FIFO에 `x`가 들어 있는데 리더가 깨지 않았다. `wake_up()`으로 바꾼 두 번째 write에서야 깨어 `x`와 `y`를 한 번에 읽어 출력했다. (두 번째 write 뒤 State가 다시 D인 것은 `cat`이 출력한 뒤 **다음 read에서 다시 잠들었기** 때문이다.) 대기 쪽 상태를 나중에 `_killable`로 바꾸면서 깨우는 쪽 `wake_up_interruptible()`을 그대로 두면 생기는 실제 버그다. 원칙: **특별한 이유가 없으면 `wake_up()`을 쓴다.**

---

## 4. 시그널과 `-ERESTARTSYS`, 그리고 타임아웃

### 4.1 규칙

`wait_event_interruptible()`이 `-ERESTARTSYS`를 돌려주면 드라이버는 **그대로 위로 돌려준다.** 이 값은 유저에게 절대 그대로 가지 않는다. syscall에서 유저로 돌아가는 길(arm64 `do_signal()`)이 시그널 핸들러의 `SA_RESTART` 여부를 보고 둘 중 하나로 바꾼다 (E6 §5.4에 소스가 있다).

- 핸들러에 `SA_RESTART`가 없으면 → 유저는 `-1`, `errno = EINTR`.
- `SA_RESTART`가 있으면 → 핸들러 실행 후 **같은 syscall을 다시** 실행한다. 유저는 시그널이 왔는지도 모르고 계속 기다린다.
- 핸들러 없는 치명적 시그널이면 → 프로세스가 죽는다.

`-ERESTARTSYS`는 "아무 일도 없었던 것처럼 다시 불러도 된다"는 약속이다. 그래서 **데이터를 일부라도 소비한 뒤에는 `-ERESTARTSYS`가 아니라 처리한 바이트 수를 돌려준다.** `f3_read()`는 잠들기 전에 FIFO를 건드리지 않으므로 지킨다.

### 4.2 예제 6 — EINTR, 재시작, KILLABLE, 타임아웃

무엇을 확인하나: 빈 장치를 읽는 중 1초에 SIGALRM(핸들러 있음), 2초에 자식이 `"late"`를 쓴다. 대기 방식과 `SA_RESTART`에 따라 결과가 어떻게 갈리는지.

```c
/* f3_t2_signal: a signal during a sleeping read - EINTR, restart, or ignored */
#include "f3_common.h"
#include <signal.h>
#include <sys/wait.h>

static volatile sig_atomic_t hits;
static void on_alarm(int s) { (void)s; hits++; }

int main(int argc, char **argv)
{
	const char *how = argc > 1 ? argv[1] : "plain";
	struct sigaction sa = { .sa_handler = on_alarm };
	char buf[32];
	int fd = open_dev(O_RDWR);

	sa.sa_flags = strcmp(how, "restart") == 0 ? SA_RESTART : 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);
	if (fork() == 0) {			/* child: data arrives at t=2 s */
		sleep(2);
		if (write(fd, "late", 4) != 4)
			perror("child write");
		_exit(0);
	}
	alarm(1);				/* signal arrives at t=1 s */
	double t0 = now();
	long r = read(fd, buf, sizeof(buf));
	printf("[%s] read returned after %.2f s, handler ran %d time(s)\n",
	       how, now() - t0, (int)hits);
	show("    read()", r);
	if (r > 0)
		printf("    got \"%.*s\"\n", (int)r, buf);
	wait(NULL);
	fcntl(fd, F_SETFL, O_NONBLOCK);		/* drain leftovers for next run */
	while (read(fd, buf, sizeof(buf)) > 0)
		;
	close(fd);
	return 0;
}
```

```sh
insmod /f3_queue.ko
echo "== wmode=0 (wait_event_interruptible)"; ./f3_t2_signal plain; ./f3_t2_signal restart
echo 1 > $P/wmode; echo "== wmode=1 (wait_event_killable)"; ./f3_t2_signal plain
echo 0 > $P/wmode; echo 500 > $P/tmo_ms
echo "== wmode=0 tmo_ms=500 (wait_event_interruptible_timeout)"; ./f3_t2_signal plain
```

```text
== wmode=0 (wait_event_interruptible)
[plain] read returned after 0.97 s, handler ran 1 time(s)
    read()                         -> -1 errno=4 EINTR
[restart] read returned after 2.11 s, handler ran 1 time(s)
    read()                         -> 4
    got "late"
== wmode=1 (wait_event_killable)
[plain] read returned after 2.27 s, handler ran 1 time(s)
    read()                         -> 4
    got "late"
== wmode=0 tmo_ms=500 (wait_event_interruptible_timeout)
[plain] read returned after 0.53 s, handler ran 0 time(s)
    read()                         -> -1 errno=110 ETIMEDOUT
```

출력에서 볼 것:

- `plain`: 1초에 시그널이 오자 `wait_event_interruptible()`이 `-ERESTARTSYS` → 유저는 `EINTR`. 
- `restart`: 핸들러는 1초에 한 번 돌았지만 read는 다시 잠들어 2초에 `"late"`를 받았다. 드라이버 코드는 똑같다 — 차이는 전부 유저 쪽 `sigaction` 플래그다.
- `wait_event_killable`: SIGALRM은 핸들러가 있으니 치명적 시그널이 아니다 → 깨지 않고 2초까지 기다렸다. 핸들러는 read가 끝나고 유저로 돌아갈 때 실행됐다(`handler ran 1`).
- 타임아웃: 0.5초에 `wait_event_interruptible_timeout()`이 0을 돌려줬고 드라이버가 `-ETIMEDOUT`으로 바꿨다. 어떤 errno를 줄지는 드라이버의 약속이다. 일반 파일 read의 관례는 아니므로, 실제 장치라면 `-EAGAIN`이나 짧은 read(0)가 더 맞을 수도 있다 — 헤더 문서에 적어 둔다. (예제 프로그램이 출력 뒤에 `wait()`과 드레인을 하므로 다음 실행에 남은 데이터가 섞이지 않는다.)

---

## 5. `wait_event` 내부와 lost-wakeup 경쟁

### 5.1 "검사하고 잔다"는 왜 위험한가

가장 순진한 blocking read는 이렇다.

```c
/* 틀린 코드 — 개념 설명용 (실행하지 않음) */
if (kfifo_is_empty(&fifo)) {          /* (a) 검사 */
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&rq, &wait);    /* (b) 등록 */
	schedule();                    /* (c) 잠 */
}
```

(a)와 (b) 사이에 다른 CPU의 라이터가 데이터를 넣고 `wake_up(&rq)`을 부르면, 그 순간 wait queue에는 아무도 없다. 깨우기는 허공으로 사라지고(**lost wakeup**), 리더는 (c)에서 **데이터가 들어 있는 FIFO를 앞에 두고** 잔다. 다음 write가 올 때까지 — 다음 write가 영영 없으면 영원히. RTOS에서 "플래그 검사 후 `taskYIELD()`" 사이에 ISR이 끼어드는 경쟁과 같은 모양이고, 펌웨어에서는 인터럽트를 끄고 검사→대기를 원자적으로 해서 막았을 것이다. 커널은 인터럽트를 끄지 않고 **순서**로 막는다.

> 올바른 순서: **① 큐에 등록하고 상태를 S로 바꾼다 → ② 조건을 검사한다 → ③ 거짓일 때만 `schedule()`.** 
> ①과 ② 사이에 깨우기가 오면, 깨우는 쪽이 큐에서 우리를 찾아 상태를 RUNNING으로 되돌려 놓는다. 그러면 ③의 `schedule()`은 잠들지 않고 바로 돌아온다(§1.3: 상태가 RUNNING이면 dequeue하지 않는다).

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">버그: 검사 → 등록 → 잠 (wmode=5)</text>
  <text x="170" y="42" text-anchor="middle" font-size="13">리더 (CPU1)</text>
  <text x="510" y="42" text-anchor="middle" font-size="13">라이터 (CPU0)</text>
  <line x1="340" y1="50" x2="340" y2="222" stroke="currentColor" stroke-dasharray="3 4"/>
  <rect x="20" y="52" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="74" text-anchor="middle" font-size="13">① 조건 검사: FIFO 비어 있음</text>
  <rect x="360" y="96" width="300" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="114" text-anchor="middle" font-size="13">② 데이터 넣고 wake_up()</text>
  <text x="510" y="132" text-anchor="middle" font-size="12">큐가 비어 있음 → 깨울 대상 없음</text>
  <rect x="20" y="150" width="300" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="170" text-anchor="middle" font-size="13">③ prepare_to_wait: 등록 + S</text>
  <rect x="20" y="188" width="300" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="170" y="210" text-anchor="middle" font-size="13">④ schedule(): 데이터가 있는데 잠듦</text>
  <line x1="20" y1="238" x2="660" y2="238" stroke="currentColor" stroke-opacity="0.3"/>
  <text x="340" y="262" text-anchor="middle" font-size="14" font-weight="bold">올바른 순서: 등록 → 검사 → 잠 (wait_event, wmode=6)</text>
  <text x="170" y="284" text-anchor="middle" font-size="13">리더 (CPU1)</text>
  <text x="510" y="284" text-anchor="middle" font-size="13">라이터 (CPU0)</text>
  <line x1="340" y1="292" x2="340" y2="460" stroke="currentColor" stroke-dasharray="3 4"/>
  <rect x="20" y="294" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="316" text-anchor="middle" font-size="13">① prepare_to_wait: 등록 + S</text>
  <rect x="360" y="338" width="300" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="356" text-anchor="middle" font-size="13">② 데이터 넣고 wake_up()</text>
  <text x="510" y="374" text-anchor="middle" font-size="12">리더를 찾음 → 상태 = RUNNING</text>
  <rect x="20" y="392" width="300" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="412" text-anchor="middle" font-size="13">③ 조건 검사: 데이터 있음 → break</text>
  <rect x="20" y="428" width="300" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="450" text-anchor="middle" font-size="13">④ finish_wait: 잠들지 않고 진행</text>
</svg>
```
그림 3 — lost-wakeup 경쟁 타임라인(위)과 올바른 순서(아래). 시간은 위에서 아래로 흐른다. 차이는 "큐 등록 + 상태 변경"이 조건 검사보다 **앞이냐 뒤냐** 하나뿐이다. 아래 경우에는 ②가 ①과 ③ 사이 어디에 끼어들어도, 혹은 ③과 `schedule()` 사이에 끼어들어도 리더는 깨어 있게 된다.

### 5.2 메모리 순서까지 — 왜 이 순서면 안전한가

멀티코어에서는 "순서대로 썼다"만으로 부족하다. CPU가 store와 load 순서를 바꿀 수 있다(F5). 두 쪽을 나란히 보면:

```
reader (CPU1)                          writer (CPU0)
  STORE  __state = INTERRUPTIBLE         STORE  fifo.in++        (data)
  (list add under wq->lock)              LOCK   wq->lock         (inside __wake_up)
  smp_mb()   <- set_current_state        LOAD   list: reader there?
  LOAD   fifo.in  (condition)            -> ttwu: STORE __state = RUNNING
```

- 리더 쪽은 `prepare_to_wait*()`가 `wq->lock` 아래서 리스트에 넣고, `set_current_state()`가 `smp_store_mb()`(store + 전체 배리어)라서 "상태 store → 조건 load" 순서가 보장된다. `include/linux/sched.h`:

```c
#define set_current_state(state_value)					\
	do {								\
		debug_normal_state_change((state_value));		\
		trace_set_current_state(state_value);			\
		smp_store_mb(current->__state, (state_value));		\
	} while (0)
```

- 라이터 쪽은 `__wake_up()`이 같은 `wq->lock`을 잡고 리스트를 본다. 락이 양쪽을 줄 세우므로 "리더가 아직 리스트에 없어서 못 봤다"면 리더의 조건 load는 반드시 그 뒤 — 즉 데이터를 본다.
- **함정**: 깨우는 쪽에서 성능 때문에 `if (waitqueue_active(&wq)) wake_up(&wq);`처럼 락 없이 리스트를 먼저 엿보면 이 줄 세우기가 깨진다. 그럴 때는 `wq_has_sleeper()`(내부에 `smp_mb()`)를 쓰거나 데이터 store와 검사 사이에 `smp_mb()`를 둔다. `wait.h`의 `waitqueue_active()` 주석이 바로 이 경고다.

`prepare_to_wait()` 본문 (`kernel/sched/wait.c`) — 주석이 정확히 이 이야기를 한다:

```c
/*
 * Note: we use "set_current_state()" _after_ the wait-queue add,
 * because we need a memory barrier there on SMP, so that any
 * wake-function that tests for the wait-queue being active
 * will be guaranteed to see waitqueue addition _or_ subsequent
 * tests in this thread will see the wakeup having taken place.
 ...
 */
void
prepare_to_wait(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry, int state)
{
	unsigned long flags;

	wq_entry->flags &= ~WQ_FLAG_EXCLUSIVE;
	spin_lock_irqsave(&wq_head->lock, flags);
	if (list_empty(&wq_entry->entry))
		__add_wait_queue(wq_head, wq_entry);
	set_current_state(state);
	spin_unlock_irqrestore(&wq_head->lock, flags);
}
```

### 5.3 `wait_event`가 펼쳐지는 모습

모든 `wait_event_*`는 결국 `___wait_event()` 하나로 펼쳐진다 (`include/linux/wait.h`, 6.18.55 그대로):

```c
#define ___wait_event(wq_head, condition, state, exclusive, ret, cmd)		\
({										\
	__label__ __out;							\
	struct wait_queue_entry __wq_entry;					\
	long __ret = ret;	/* explicit shadow */				\
										\
	init_wait_entry(&__wq_entry, exclusive ? WQ_FLAG_EXCLUSIVE : 0);	\
	for (;;) {								\
		long __int = prepare_to_wait_event(&wq_head, &__wq_entry, state);\
										\
		if (condition)							\
			break;							\
										\
		if (___wait_is_interruptible(state) && __int) {			\
			__ret = __int;						\
			goto __out;						\
		}								\
										\
		cmd;								\
										\
		if (condition)							\
			break;							\
	}									\
	finish_wait(&wq_head, &__wq_entry);					\
__out:	__ret;									\
})
```

- `cmd`가 `schedule()`(또는 `_timeout` 계열은 `schedule_timeout()`)이다.
- `prepare_to_wait_event()`는 `prepare_to_wait()`에 시그널 검사를 더한 것이다. 시그널이 와 있으면 `-ERESTARTSYS`를 돌려주고, 매크로는 `goto __out`으로 빠진다.
- `cmd` 뒤에 조건을 **한 번 더** 본다. 깨어났으면 조건이 참일 가능성이 높으니 루프 앞으로 돌아가 다시 등록하기 전에 확인한다. 거짓이면(헛깨움) 다시 등록 → 검사 → 잠.
- 엔트리 `__wq_entry`는 **스택 위**에 있다. 그래서 `finish_wait()` 없이 함수를 빠져나가면 wait queue에 죽은 스택 주소가 남는다 — 손으로 짤 때 가장 무서운 버그다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="110" y="14" width="320" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="270" y="36" text-anchor="middle" font-size="13">init_wait_entry: 스택 위 엔트리 준비</text>
  <rect x="110" y="72" width="320" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="270" y="94" text-anchor="middle" font-size="13">prepare_to_wait_event: 등록 + 상태 설정</text>
  <rect x="110" y="130" width="320" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="270" y="152" text-anchor="middle" font-size="13">condition 참?</text>
  <rect x="110" y="188" width="320" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="270" y="210" text-anchor="middle" font-size="13">시그널 대기 중? (S·KILLABLE만 검사)</text>
  <rect x="110" y="246" width="320" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="270" y="268" text-anchor="middle" font-size="13">cmd = schedule(): 여기서 잔다</text>
  <rect x="110" y="304" width="320" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="270" y="326" text-anchor="middle" font-size="13">깨어남 → condition 참?</text>
  <rect x="480" y="130" width="190" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="575" y="152" text-anchor="middle" font-size="13">finish_wait → 0</text>
  <rect x="480" y="188" width="190" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="575" y="210" text-anchor="middle" font-size="13">-ERESTARTSYS 반환</text>
  <line x1="270" y1="48" x2="270" y2="70" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="270" y1="106" x2="270" y2="128" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="270" y1="164" x2="270" y2="186" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="278" y="180" font-size="12">아니오</text>
  <line x1="270" y1="222" x2="270" y2="244" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="278" y="238" font-size="12">아니오</text>
  <line x1="270" y1="280" x2="270" y2="302" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="278" y="296" font-size="12">wake_up 또는 시그널</text>
  <line x1="430" y1="147" x2="478" y2="147" stroke="#3f9a6b" marker-end="url(#ah3)"/>
  <text x="454" y="140" text-anchor="middle" font-size="12">예</text>
  <line x1="430" y1="205" x2="478" y2="205" stroke="#d0564a" marker-end="url(#ah3)"/>
  <text x="454" y="198" text-anchor="middle" font-size="12">예</text>
  <polyline points="430,321 575,321 575,166" fill="none" stroke="#3f9a6b" marker-end="url(#ah3)"/>
  <text x="500" y="314" text-anchor="middle" font-size="12">예</text>
  <polyline points="110,321 60,321 60,89 108,89" fill="none" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="54" y="210" text-anchor="end" font-size="12">아니오</text>
  <text x="270" y="370" text-anchor="middle" font-size="12">헛깨움(다른 리더가 먼저 가져감)이면 다시 등록하고 잔다</text>
</svg>
```
그림 4 — `___wait_event()` 루프. 매번 **등록·상태 설정이 조건 검사보다 먼저**다. 깨어난 뒤에도 조건이 거짓이면(헛깨움) 다시 돈다. `wait_event`가 "조건을 여러 번 평가한다"는 말의 정확한 의미가 이 그림이다.

### 5.4 손으로 짜는 정석 — `prepare_to_wait` / `finish_wait`

매크로로 표현하기 어려운 경우(조건 검사 중에 락을 잡았다 풀어야 하거나, 깨어날 때마다 무언가를 해야 할 때)는 손으로 짠다. `f3_wait_data()`의 `case 6`이 그 모양이다:

```c
	DEFINE_WAIT(wait);

	for (;;) {
		prepare_to_wait(&f3_rq, &wait, TASK_INTERRUPTIBLE);  /* 1. 등록 + 상태 */
		if (f3_has_data())                                    /* 2. 검사 */
			break;
		if (signal_pending(current)) {                        /* 3. 시그널 */
			finish_wait(&f3_rq, &wait);
			return -ERESTARTSYS;
		}
		schedule();                                           /* 4. 잠 */
	}
	finish_wait(&f3_rq, &wait);                                   /* 5. RUNNING + 제거 */
```

`finish_wait()`는 상태를 `TASK_RUNNING`으로 돌리고, 아직 리스트에 있으면(깨움 없이 조건이 참이 된 경우) 빼낸다. **모든 출구에서** 불러야 한다.

옛 코드에서 보는 `sleep_on()`/`interruptible_sleep_on()`은 바로 이 경쟁 때문에 커널에서 제거됐다(3.15 무렵). 인터넷 예제에서 보이면 버린다.

### 5.5 예제 7 — lost wakeup을 일부러 만들고 고치기

무엇을 확인하나: `wmode=5`(버그)는 "검사 → `mdelay(race_ms)` → 등록 → 잠"이다. `mdelay`로 경쟁 창을 200 ms로 벌려 두고, 라이터가 50 ms에 쓰게 해서 **매번** 창 안에 떨어지게 만든다. 실제 버그는 `schedule()`로 영원히 자지만, 데모는 `schedule_timeout(2 * HZ)`라서 2초 뒤에 깨어 다시 검사한다. `wmode=6`(수정)은 같은 200 ms `mdelay`를 등록 뒤로 옮겼다.

```c
/* f3_t5_race: write lands inside the reader's check->sleep window */
#include "f3_common.h"

static double t0;

static void *reader(void *arg)
{
	char buf[8];
	int fd = open_dev(O_RDONLY);
	long r = read(fd, buf, sizeof(buf));
	printf("  reader got %ld byte(s) at t=%.3f s\n", r, now() - t0);
	close(fd);
	return arg;
}

int main(int argc, char **argv)
{
	int trials = argc > 1 ? atoi(argv[1]) : 3;
	int fd = open_dev(O_WRONLY);

	for (int i = 0; i < trials; i++) {
		pthread_t th;

		t0 = now();
		pthread_create(&th, NULL, reader, NULL);
		usleep(50 * 1000);		/* reader is inside mdelay(race_ms) */
		if (write(fd, "x", 1) != 1) { perror("write"); exit(1); }
		printf("trial %d: write at t=%.3f s\n", i, now() - t0);
		pthread_join(th, NULL);
	}
	close(fd);
	return 0;
}
```

```sh
insmod /f3_queue.ko; echo 200 > $P/race_ms
echo 5 > $P/wmode; echo "== wmode=5 BUGGY (check, then prepare_to_wait)"; ./f3_t5_race 3
echo 6 > $P/wmode; echo "== wmode=6 FIXED (prepare_to_wait, then check)"; ./f3_t5_race 3
```

```text
== wmode=5 BUGGY (check, then prepare_to_wait)
trial 0: write at t=0.063 s
  reader got 1 byte(s) at t=2.257 s
trial 1: write at t=0.053 s
  reader got 1 byte(s) at t=2.339 s
trial 2: write at t=0.053 s
  reader got 1 byte(s) at t=2.265 s
== wmode=6 FIXED (prepare_to_wait, then check)
trial 0: write at t=0.055 s
  reader got 1 byte(s) at t=0.218 s
trial 1: write at t=0.052 s
  reader got 1 byte(s) at t=0.216 s
trial 2: write at t=0.053 s
  reader got 1 byte(s) at t=0.216 s
```

출력에서 볼 것: 버그 버전은 데이터가 0.05초에 들어왔는데 리더가 **2.2초**(= `mdelay` 200 ms + 타임아웃 2 s)에야 받았다. 깨우기가 사라졌고 타임아웃 덕분에 살아났을 뿐이다. 수정 버전은 똑같이 200 ms를 바쁘게 기다리는데도 0.22초에 받았다 — 라이터의 `wake_up()`이 큐에서 리더를 찾아 상태를 RUNNING으로 돌려놨고, 이어서 조건 검사가 데이터를 봤다. `-smp 2`라서 라이터가 다른 CPU에서 진짜로 동시에 돈다.

실제 장비에서는 창이 수 ns~수 µs라서 백만 번에 한 번 터진다. "가끔 센서 read가 다음 샘플까지 늦게 깬다"는 버그 리포트가 오면 이 경쟁부터 의심한다. 디버그 방법이 바로 이것이다: 의심 구간에 `mdelay()`를 넣어 창을 벌리면 재현율이 100%가 된다.

(참고: `mdelay()`는 TASK_INTERRUPTIBLE 상태에서 돌아도 잠들지 않는다 — 바쁜 대기이고, 그 사이 선점돼도 §1.3의 `!preempt` 조건 때문에 runqueue에서 빠지지 않는다.)

---

## 6. exclusive wait와 thundering herd

### 6.1 무엇이 문제인가

리더 8명이 같은 wait queue에서 자고 있는데 1바이트가 들어왔다. `wake_up()`은 배타적이지 않은 대기자를 **전부** 깨운다. 8명이 깨어나 CPU를 다투고, 1명만 데이터를 얻고, 7명은 조건을 다시 보고 다시 잔다. 이것이 **thundering herd**(우르르 몰려옴)다. 서버의 `accept()`에서 유명해진 문제고, 드라이버에서는 "여러 스레드가 같은 장치를 읽는" 구조에서 나온다.

해결은 **배타적(exclusive) 대기**다. `wait_event_interruptible_exclusive()`는 엔트리에 `WQ_FLAG_EXCLUSIVE`를 붙이고 리스트 **꼬리**에 넣는다(`prepare_to_wait_exclusive()` → `__add_wait_queue_entry_tail`). `__wake_up_common()`은 §2.3의 루프에서 배타적 대기자를 `nr_exclusive`(= `wake_up()`이면 1)명 깨우면 멈춘다. 비배타적 대기자(예: `poll()` 대기자)는 머리 쪽에 있어서 여전히 전부 깨어난다.

### 6.2 예제 8 — 조건 평가 횟수로 센 thundering herd

무엇을 확인하나: 리더 스레드 8개가 1바이트씩 읽으려고 잠든다. 1바이트를 쓰고 조건 함수 `f3_has_data()`가 몇 번 불렸는지 debugfs(`/sys/kernel/debug/f3_queue/checks`)로 센다.

```c
/* f3_t3_herd: 8 sleeping readers, 1 byte written - how many woke up? */
#include "f3_common.h"

#define N 8
#define CHECKS "/sys/kernel/debug/f3_queue/checks"

static int checks(void)
{
	char s[32] = "";
	int fd = open(CHECKS, O_RDWR);
	if (fd < 0) { perror(CHECKS); exit(1); }
	if (read(fd, s, sizeof(s) - 1) < 0) { perror("read checks"); exit(1); }
	close(fd);
	return atoi(s);
}

static void *reader(void *arg)
{
	char c;
	int fd = open_dev(O_RDONLY);
	if (read(fd, &c, 1) != 1)
		perror("reader read");
	close(fd);
	return arg;
}

int main(void)
{
	pthread_t th[N];
	int fd = open_dev(O_WRONLY), i, before, after;

	for (i = 0; i < N; i++)
		pthread_create(&th[i], NULL, reader, NULL);
	usleep(300 * 1000);			/* all 8 are asleep on f3_rq */
	before = checks();
	if (write(fd, "x", 1) != 1) { perror("write"); exit(1); }
	usleep(300 * 1000);			/* let the wakeups settle */
	after = checks();
	printf("1 byte for %d sleepers: condition evaluated %d times\n",
	       N, after - before);
	for (i = 1; i < N; i++)			/* release the others */
		if (write(fd, "y", 1) != 1) { perror("write"); exit(1); }
	for (i = 0; i < N; i++)
		pthread_join(th[i], NULL);
	close(fd);
	return 0;
}
```

```sh
insmod /f3_queue.ko
echo "== wmode=0 wait_event_interruptible"; ./f3_t3_herd
echo 4 > $P/wmode; echo "== wmode=4 wait_event_interruptible_exclusive"; ./f3_t3_herd
```

```text
== wmode=0 wait_event_interruptible
1 byte for 8 sleepers: condition evaluated 15 times
== wmode=4 wait_event_interruptible_exclusive
1 byte for 8 sleepers: condition evaluated 1 times
```

출력에서 볼 것: 비배타적일 때 **15번** = 승자 1명이 깨어나 1번 + 패자 7명이 각자 "깨어나서 1번(`cmd` 뒤 검사) + 다시 등록하고 1번(루프 앞 검사)" = 1 + 7 × 2. 그림 4의 루프를 숫자로 확인한 셈이다. (같은 테스트를 다른 부팅에서 돌렸을 때는 16번도 한 번 나왔다. 깨어나는 타이밍에 따라 한두 번 더 평가될 수 있다 — 그 한 번의 정확한 원인은 추적하지 않았다. 요점은 "대기자 수에 비례"한다는 것이다.) 배타적일 때는 **1번** — 한 명만 깨어났다.

언제 쓰나: 대기자 누구나 같은 일을 처리할 수 있고 한 번의 이벤트를 한 명만 소비하면 될 때. 반대로 "장치가 제거됐다", "모드가 바뀌었다"처럼 모두가 알아야 하는 이벤트는 `wake_up_all()`로 배타적 대기자까지 다 깨운다. 함정: 배타적 대기자가 깨어났는데 시그널 때문에 그냥 나가 버리면 이벤트를 아무도 처리하지 않는다. 그래서 `prepare_to_wait_event()`는 "배타적 대기자가 이미 깨움을 받았으면 시그널로 실패하지 않는다"는 처리를 한다(§5.3 앞부분 소스 주석 "Exclusive waiter must not fail if it was selected by wakeup").

---

## 7. completion — "IRQ/DMA가 끝날 때까지 기다린다"

### 7.1 왜 따로 있나

"DMA를 시작하고 완료 IRQ를 기다린다"를 wait queue로 짜면 `done` 플래그 + wait queue + 배리어 + 재초기화를 직접 관리해야 한다. 커널은 이 패턴을 묶어 `struct completion`으로 준다 (`include/linux/completion.h`):

```c
struct completion {
	unsigned int done;
	struct swait_queue_head wait;
};
```

`done`은 **카운트**다. `complete()`가 1 올리고 `wait_for_completion()`이 1 내린다. 그래서 IRQ가 기다리는 쪽보다 **먼저** 와도 사라지지 않는다 — 이진 세마포어와 같고, lost wakeup이 구조적으로 없다. 대기열은 가벼운 simple wait queue(`swait`)다.

| API | 하는 일 |
|---|---|
| `DECLARE_COMPLETION(x)` / `init_completion(&x)` | 정적/동적 초기화 (`done = 0`) |
| `reinit_completion(&x)` | `done = 0`으로 되돌림. **다음 요청을 시작하기 전에** |
| `wait_for_completion(&x)` | D 상태로 무한 대기 (유저 I/O에는 피함) |
| `wait_for_completion_interruptible(&x)` | S, 시그널이면 `-ERESTARTSYS` |
| `wait_for_completion_killable(&x)` | KILLABLE |
| `wait_for_completion_timeout(&x, t)` | D, 0 = 타임아웃, 아니면 남은 jiffies |
| `wait_for_completion_interruptible_timeout(&x, t)` | S + 타임아웃 |
| `complete(&x)` | `done++`, 대기자 1명 깨움. **IRQ 컨텍스트에서 OK** |
| `complete_all(&x)` | `done = UINT_MAX`, 모두 깨움. 이후 재사용하려면 `reinit_completion` 필수 |
| `try_wait_for_completion(&x)` / `completion_done(&x)` | 잠들지 않고 확인 |

대기 쪽 핵심 (`kernel/sched/completion.c`) — `wait_event`와 같은 "등록 → 상태 → 검사 → 잠" 순서를 락 아래서 한다:

```c
	if (!x->done) {
		DECLARE_SWAITQUEUE(wait);

		do {
			if (signal_pending_state(state, current)) {
				timeout = -ERESTARTSYS;
				break;
			}
			__prepare_to_swait(&x->wait, &wait);
			__set_current_state(state);
			raw_spin_unlock_irq(&x->wait.lock);
			timeout = action(timeout);
			raw_spin_lock_irq(&x->wait.lock);
		} while (!x->done && timeout);
		__finish_swait(&x->wait, &wait);
		if (!x->done)
			return timeout;
	}
	if (x->done != UINT_MAX)
		x->done--;
	return timeout ?: 1;
```

**completion vs wait queue — 언제 무엇을:**

| 상황 | 고를 것 | 이유 |
|---|---|---|
| "이 한 번의 일이 끝났다" (DMA 완료, 펌웨어 응답, kthread 종료) | completion | 조건 변수가 따로 없음, 순서 실수 불가 |
| "어떤 상태가 참이 될 때까지" (FIFO 비어 있지 않음, 링크 업) | wait queue + `wait_event_*` | 조건이 드라이버 상태에 있음, 여러 번 참/거짓 반복 |
| poll/epoll도 지원해야 함 | wait queue | `poll_wait()`는 `wait_queue_head_t`를 받는다 |
| 여러 대기자를 한 번에 풀기 (초기화 완료 신호) | `complete_all()` | 한 번 열리면 계속 열림 |
| 어떤 변수가 특정 값이 될 때까지, wait queue를 두기 싫음 | `wait_var_event()` | 전역 해시 wait queue 공유 |

### 7.2 예제 9 — 타이머를 "가짜 IRQ"로 쓰는 DMA 완료 대기

무엇을 확인하나: `timer_list`(F8)가 정해진 시간 뒤에 softirq 컨텍스트에서 `complete()`를 부른다 = "하드웨어가 IRQ를 올렸다". 전송 1은 20 ms에 IRQ(정상), 전송 2는 하드웨어가 150 ms 걸리는데 100 ms만 기다림(타임아웃), 100 ms 쉬고 전송 3(정상)을 한다. `bug_noreinit=1`이면 `reinit_completion()`과 타임아웃 뒤 타이머 취소를 빼먹는다.

```c
// SPDX-License-Identifier: GPL-2.0
/* f3_comp: "kick DMA, wait for done IRQ" with a completion; timer = fake IRQ */
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/timer.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>
#include <linux/delay.h>

static bool bug_noreinit;	/* forget reinit_completion() + timer cancel */
module_param(bug_noreinit, bool, 0444);

static DECLARE_COMPLETION(f3_done);
static struct timer_list f3_irq;
static ktime_t f3_t0, f3_tirq;

static void f3_fake_irq(struct timer_list *t)	/* softirq ctx: must not sleep */
{
	f3_tirq = ktime_get();
	complete(&f3_done);			/* never sleeps - OK in IRQ */
}

/* lat_ms = when the "hardware" raises its IRQ, tmo_ms = how long we wait */
static int f3_xfer(int id, int lat_ms, int tmo_ms)
{
	unsigned long left;

	if (!bug_noreinit)
		reinit_completion(&f3_done);	/* BEFORE kicking the hardware */
	f3_t0 = ktime_get();
	mod_timer(&f3_irq, jiffies + msecs_to_jiffies(lat_ms));	/* "kick DMA" */
	left = wait_for_completion_timeout(&f3_done, msecs_to_jiffies(tmo_ms));
	if (!left) {
		if (!bug_noreinit)
			timer_delete_sync(&f3_irq);	/* stop the late IRQ */
		pr_info("f3c: xfer%d TIMEOUT after %lld us\n", id,
			ktime_us_delta(ktime_get(), f3_t0));
		return -ETIMEDOUT;
	}
	pr_info("f3c: xfer%d done: irq at %lld us, waiter ran at %lld us, %lu jiffies left\n",
		id, ktime_us_delta(f3_tirq, f3_t0),
		ktime_us_delta(ktime_get(), f3_t0), left);
	return 0;
}

static int __init f3c_init(void)
{
	timer_setup(&f3_irq, f3_fake_irq, 0);
	f3_xfer(1, 20, 100);		/* normal: IRQ after 20 ms */
	f3_xfer(2, 150, 100);		/* hardware too slow -> timeout */
	msleep(100);			/* (the late IRQ would land here) */
	f3_xfer(3, 50, 100);		/* normal again? */
	timer_delete_sync(&f3_irq);
	return 0;
}

static void __exit f3c_exit(void) { }
module_init(f3c_init);
module_exit(f3c_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("F3 completion lab: wait for fake IRQ");
```

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="170" y="22" text-anchor="middle" font-size="13" font-weight="bold">드라이버 (프로세스 컨텍스트)</text>
  <text x="510" y="22" text-anchor="middle" font-size="13" font-weight="bold">하드웨어 + IRQ 핸들러</text>
  <line x1="340" y1="30" x2="340" y2="350" stroke="currentColor" stroke-dasharray="3 4"/>
  <rect x="20" y="36" width="300" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="57" text-anchor="middle" font-size="13">① reinit_completion(&amp;done)</text>
  <rect x="20" y="80" width="300" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="101" text-anchor="middle" font-size="13">② DMA 시작 (doorbell 레지스터)</text>
  <line x1="320" y1="96" x2="358" y2="96" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="360" y="80" width="300" height="32" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="101" text-anchor="middle" font-size="13">전송 진행 중</text>
  <rect x="20" y="124" width="300" height="32" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="170" y="145" text-anchor="middle" font-size="13">③ wait_for_completion_timeout(): 잠</text>
  <rect x="360" y="168" width="300" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="187" text-anchor="middle" font-size="13">④ 완료 IRQ → 핸들러</text>
  <text x="510" y="204" text-anchor="middle" font-size="12">complete(&amp;done): done++, 깨움</text>
  <line x1="358" y1="200" x2="322" y2="236" stroke="#3f9a6b" marker-end="url(#ah4)"/>
  <rect x="20" y="224" width="300" height="32" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="245" text-anchor="middle" font-size="13">⑤ 깨어남: 상태 레지스터 확인</text>
  <rect x="20" y="276" width="300" height="60" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-dasharray="5 3"/>
  <text x="170" y="296" text-anchor="middle" font-size="12">타임아웃(0 반환)이면:</text>
  <text x="170" y="313" text-anchor="middle" font-size="12">하드웨어 정지 + IRQ 막기 + 동기화</text>
  <text x="170" y="330" text-anchor="middle" font-size="12">안 하면 늦은 IRQ가 다음 전송을 속인다</text>
  <rect x="360" y="276" width="300" height="60" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="296" text-anchor="middle" font-size="12">IRQ 핸들러는 잠들 수 없다</text>
  <text x="510" y="313" text-anchor="middle" font-size="12">complete()는 잠들지 않으니 OK</text>
  <text x="510" y="330" text-anchor="middle" font-size="12">wait_for_completion()은 금지</text>
</svg>
```
그림 5 — completion으로 "IRQ/DMA 완료 대기"하기. 순서가 중요하다: ① 재초기화는 ② 하드웨어를 깨우기 **전에**(뒤에 하면 아주 빠른 IRQ의 `complete()`를 지워 버린다). 타임아웃 경로에서는 늦게 올 IRQ를 반드시 막는다.

게스트에서 (정상 → 버그 순서로 두 번 로드):

```sh
insmod /f3_comp.ko; rmmod f3_comp
echo "== bug_noreinit=1"; insmod /f3_comp.ko bug_noreinit=1; rmmod f3_comp
insmod /f3_all.ko
dmesg | grep -E "f3[ca]:"
```

```text
== bug_noreinit=1
[   16.263415] f3c: xfer1 done: irq at 23324 us, waiter ran at 24470 us, 19 jiffies left
[   16.367249] f3c: xfer2 TIMEOUT after 103659 us
[   16.531275] f3c: xfer3 done: irq at 54130 us, waiter ran at 59768 us, 11 jiffies left
[   16.895094] f3c: xfer1 done: irq at 22143 us, waiter ran at 22806 us, 19 jiffies left
[   16.999767] f3c: xfer2 TIMEOUT after 104519 us
[   17.103768] f3c: xfer3 done: irq at -53526 us, waiter ran at 78 us, 25 jiffies left
```

(`== bug_noreinit=1` 줄은 셸 echo라 바로 찍혔고, 커널 로그는 마지막 `dmesg`에서 한꺼번에 나왔다. 앞 세 줄이 정상 로드, 다음 세 줄이 `bug_noreinit=1` 로드다. `f3a:` 줄은 예제 10.)

출력에서 볼 것:

- 정상: 전송 1은 IRQ가 23 ms에 왔고 대기자는 1 ms 남짓 뒤에 실행됐다. 전송 2는 100 ms에 타임아웃. 전송 3은 정상(54 ms에 IRQ). `jiffies left`는 `HZ=250`이라 4 ms 단위다 (100 ms = 25 jiffies).
- 버그: 전송 3이 **78 µs 만에 "완료"** 됐고 IRQ 시각이 **-53 ms**다. 전송 2의 늦은 IRQ(150 ms에 발사)가 `done`을 1로 만들어 놓았고, 재초기화를 안 했으니 전송 3의 `wait_for_completion_timeout()`이 바로 통과했다. 실제 장치라면 아직 DMA가 진행 중인 버퍼를 드라이버가 읽어 간다 — **데이터 오염**이다. 게다가 전송 3의 진짜 IRQ가 또 `done`을 남겨서 다음 전송도 속는다.

SSD 펌웨어에서 "명령 타임아웃 후 늦게 도착한 completion을 다음 명령의 completion으로 착각"하는 버그를 태그(command ID)로 막았던 것과 똑같은 문제다. 리눅스 드라이버에서도 같은 해법을 쓴다: 타임아웃이면 하드웨어를 정지/리셋하고(`timer_delete_sync()`는 그 흉내), IRQ 핸들러가 끝났음을 `synchronize_irq()`로 확인한 뒤, 다음 요청 전에 `reinit_completion()`.

### 7.3 예제 10 — `complete_all()`과 `wait_var_event()`

무엇을 확인하나: kthread 3개가 같은 completion에서 잠들고 `complete_all()` 한 번에 모두 풀린다. 메인은 "살아 있는 워커 수" 변수가 0이 될 때까지 `wait_var_event()`로 기다린다. `wait_var_event()`는 **변수 주소를 해시해서** 커널 전역 wait queue 테이블(`bit_wait_table`, 256개)의 한 칸을 빌려 쓴다(`kernel/sched/wait_bit.c`의 `__var_waitqueue()`). 구조체에 `wait_queue_head_t`를 따로 두지 않아도 된다.

```c
// SPDX-License-Identifier: GPL-2.0
/* f3_all: complete_all() releases every waiter; wait_var_event() joins them */
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/wait_bit.h>
#include <linux/atomic.h>
#include <linux/delay.h>

static DECLARE_COMPLETION(f3_go);
static atomic_t f3_alive = ATOMIC_INIT(0);

static int f3_worker(void *arg)
{
	long id = (long)arg;

	wait_for_completion(&f3_go);			/* all park here */
	pr_info("f3a: worker%ld released on cpu%d\n", id, raw_smp_processor_id());
	if (atomic_dec_and_test(&f3_alive))
		wake_up_var(&f3_alive);			/* last one out */
	return 0;
}

static int __init f3a_init(void)
{
	long i;

	for (i = 0; i < 3; i++) {
		atomic_inc(&f3_alive);
		if (IS_ERR(kthread_run(f3_worker, (void *)i, "f3_worker%ld", i)))
			atomic_dec(&f3_alive);
	}
	msleep(200);					/* let them block */
	pr_info("f3a: complete_all() now, done was %u\n", f3_go.done);
	complete_all(&f3_go);
	wait_var_event(&f3_alive, atomic_read(&f3_alive) == 0);
	pr_info("f3a: all workers finished, done=%u (UINT_MAX = stays open)\n",
		f3_go.done);
	return 0;
}

static void __exit f3a_exit(void) { }
module_init(f3a_init);
module_exit(f3a_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("F3 lab: complete_all + wait_var_event");
```

```text
[   17.739427] f3a: complete_all() now, done was 0
[   17.740486] f3a: worker1 released on cpu0
[   17.743609] f3a: worker0 released on cpu1
[   17.743945] f3a: worker2 released on cpu1
[   17.746009] f3a: all workers finished, done=4294967295 (UINT_MAX = stays open)
```

출력에서 볼 것: 세 워커가 두 CPU에서 거의 동시에(3 ms 안에) 풀렸다. 끝난 뒤 `done`이 `4294967295`(`UINT_MAX`)다 — `complete_all()` 이후 completion은 "영구히 열린" 상태라 새로 오는 대기자도 그냥 통과한다. 재사용하려면 모든 대기자가 빠져나간 것을 확인한 뒤 `reinit_completion()`을 해야 한다. `wake_up_var()` 앞의 `atomic_dec_and_test()`는 값을 돌려주는 atomic이라 완전한 배리어를 포함한다. 일반 store 뒤에 `wake_up_var()`를 부를 때는 그 사이에 `smp_mb()`가 필요하다(§5.2와 같은 이유).

(주의: 이 데모의 kthread는 함수에서 그냥 return한다. 모듈 init이 끝난 뒤 곧바로 rmmod하면 이론상 워커가 아직 모듈 코드의 마지막 몇 명령을 실행 중일 수 있다. 실제 드라이버에서는 `kthread_stop()`이나 `kthread_complete_and_exit()`로 수명을 맞춘다 — F7.)

---

## 8. ftrace로 보는 잠과 깨움 — `sched_switch` / `sched_waking` / `sched_wakeup`

### 8.1 예제 11 — 라이터가 리더를 깨우는 순간과 지연

무엇을 확인하나: 라이터(cpu0)가 3 ms마다 현재 시각을 8바이트로 써 넣고, 리더(`f3_rd`)는 그것을 읽어 "write 직전 → read 리턴" 지연을 잰다. 리더를 cpu1(다른 CPU)에 두는 경우와 cpu0(같은 CPU)에 두는 경우를 비교하고, 동시에 ftrace 스케줄러 이벤트를 `f3_rd`로 필터링해 기록한다.

```c
/* f3_t4_lat: write() -> sleeping reader returns from read(): wakeup latency */
#include "f3_common.h"
#include <sched.h>
#include <sys/prctl.h>

#define N 200
static double lat_us[N];
static int rd_cpu;

static void pin(int cpu)
{
	cpu_set_t s;
	CPU_ZERO(&s);
	CPU_SET(cpu, &s);
	if (sched_setaffinity(0, sizeof(s), &s)) { perror("affinity"); exit(1); }
}

static void *reader(void *arg)
{
	int fd = open_dev(O_RDONLY);
	double t_w;

	pin(rd_cpu);
	prctl(PR_SET_NAME, "f3_rd");			/* ftrace filter key */
	for (int i = 0; i < N; i++) {
		if (read(fd, &t_w, sizeof(t_w)) != sizeof(t_w)) { perror("read"); exit(1); }
		lat_us[i] = (now() - t_w) * 1e6;
	}
	close(fd);
	return arg;
}

static int cmp(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}
```

```c
int main(int argc, char **argv)
{
	pthread_t th;
	int fd = open_dev(O_WRONLY);

	rd_cpu = argc > 1 ? atoi(argv[1]) : 1;
	pin(0);						/* writer on cpu0 */
	pthread_create(&th, NULL, reader, NULL);
	usleep(100 * 1000);
	for (int i = 0; i < N; i++) {
		double t = now();
		if (write(fd, &t, sizeof(t)) != sizeof(t)) { perror("write"); exit(1); }
		usleep(3000);				/* reader goes back to sleep */
	}
	pthread_join(th, NULL);
	printf("RAW");
	for (int i = 0; i < N; i++)
		printf(" %.0f", lat_us[i]);
	qsort(lat_us, N, sizeof(double), cmp);
	printf("\nwriter cpu0 -> reader cpu%d, %d samples: min %.0f  p50 %.0f  p90 %.0f  p99 %.0f  max %.0f us\n",
	       rd_cpu, N, lat_us[0], lat_us[N / 2], lat_us[N * 9 / 10],
	       lat_us[N * 99 / 100], lat_us[N - 1]);
	close(fd);
	return 0;
}
```

게스트 스크립트:

```sh
mount -t tracefs none /sys/kernel/tracing; T=/sys/kernel/tracing
echo 'comm == "f3_rd"' > $T/events/sched/sched_waking/filter
echo 'comm == "f3_rd"' > $T/events/sched/sched_wakeup/filter
echo 'prev_comm == "f3_rd" || next_comm == "f3_rd"' > $T/events/sched/sched_switch/filter
for cpu in 1 0; do
  echo > $T/trace
  for e in sched_waking sched_wakeup sched_switch; do echo 1 > $T/events/sched/$e/enable; done
  ./f3_t4_lat $cpu
  for e in sched_waking sched_wakeup sched_switch; do echo 0 > $T/events/sched/$e/enable; done
  echo "@@TRACE_BEGIN cpu$cpu"; grep -v "^#" $T/trace; echo "@@TRACE_END"
done
```

유저 쪽 결과 (RAW 200개 숫자 줄은 생략):

```text
writer cpu0 -> reader cpu1, 200 samples: min 507  p50 1285  p90 4883  p99 11718  max 16242 us
writer cpu0 -> reader cpu0, 200 samples: min 311  p50 818  p90 2806  p99 7590  max 46270 us
```

trace 일부 — 리더가 cpu1에 있을 때 (앞 네 줄, 한 번의 잠→깨움→실행):

```text
           f3_rd-76      [001] d..2.    14.243635: sched_switch: prev_comm=f3_rd prev_pid=76 prev_prio=120 prev_state=S ==> next_comm=swapper/1 next_pid=0 next_prio=120
       f3_t4_lat-75      [000] d..3.    14.334498: sched_waking: comm=f3_rd pid=76 prio=120 target_cpu=001
          <idle>-0       [001] dNh2.    14.334830: sched_wakeup: comm=f3_rd pid=76 prio=120 target_cpu=001
          <idle>-0       [001] d..2.    14.335018: sched_switch: prev_comm=swapper/1 prev_pid=0 prev_prio=120 prev_state=R ==> next_comm=f3_rd next_pid=76 next_prio=120
```

리더가 cpu0(라이터와 같은 CPU)에 있을 때:

```text
           f3_rd-79      [000] d..2.    16.707731: sched_switch: prev_comm=f3_rd prev_pid=79 prev_prio=120 prev_state=S ==> next_comm=swapper/0 next_pid=0 next_prio=120
       f3_t4_lat-78      [000] d..3.    16.807539: sched_waking: comm=f3_rd pid=79 prio=120 target_cpu=000
       f3_t4_lat-78      [000] d..4.    16.807948: sched_wakeup: comm=f3_rd pid=79 prio=120 target_cpu=000
       f3_t4_lat-78      [000] d..2.    16.808171: sched_switch: prev_comm=f3_t4_lat prev_pid=78 prev_prio=120 prev_state=S ==> next_comm=f3_rd next_pid=79 next_prio=120
           f3_rd-79      [000] d..2.    16.808578: sched_switch: prev_comm=f3_rd prev_pid=79 prev_prio=120 prev_state=S ==> next_comm=swapper/0 next_pid=0 next_prio=120
       f3_t4_lat-78      [000] d..3.    16.811976: sched_waking: comm=f3_rd pid=79 prio=120 target_cpu=000
       f3_t4_lat-78      [000] dN.4.    16.812040: sched_wakeup: comm=f3_rd pid=79 prio=120 target_cpu=000
```

출력에서 볼 것:

- `prev_state=S`: 리더가 `read()`에서 `wait_event_interruptible()`로 잠들며 CPU를 놓는 순간. 그 CPU는 `swapper/1`(idle)로 넘어간다.
- `sched_waking`은 **깨우는 쪽 문맥**에서 찍힌다 — 태스크 칸이 `f3_t4_lat-75`(라이터)이고 CPU가 `[000]`이다. 라이터의 `write()` → `wake_up()` → `try_to_wake_up()`이 이 지점이다.
- 다른 CPU 경우 `sched_wakeup`은 **대상 CPU `[001]`에서 `<idle>`이 하드IRQ 플래그 `h`와 함께** 찍혔다. ttwu가 리더를 cpu1의 wake list에 넣고 IPI를 보냈고, cpu1이 IPI 핸들러에서 runqueue에 넣은 것이다(§1.3). 플래그 `N`은 need-resched — 곧 문맥 교환하라는 표시다.
- 같은 CPU 경우 두 번째 묶음에서 라이터가 `prev_state=R+`로 쫓겨났다. 깨어난 리더가 **wakeup preemption**으로 라이터를 선점한 것이다 (`+`는 선점됨 표시). 첫 묶음은 라이터가 `usleep`으로 스스로 잠들며(`S`) 리더에게 넘겼다.

trace 전체(cpu1 189회, cpu0 197회)에서 `sched_waking` → 리더가 CPU를 받은 `sched_switch`까지의 시간을 Python으로 계산했다 (VM 쪽에서 로그를 파싱. `sched_waking` 뒤에 리더의 switch-in이 이어지는 짝만 셌더니 cpu1 189회, cpu0 197회였다):

```text
cross user 200 min 507 p50 1285 p90 4883 p99 11718 max 16242
same user 200 min 311 p50 818 p90 2806 p99 7590 max 46270
cpu1 waking->switch-in 189 min 181 p50 737 p90 2170 p99 9697 max 15876
cpu1 waking->wakeup 189 p50 489 p90 1493
cpu0 waking->switch-in 197 min 98 p50 345 p90 1169 p99 4145 max 6861
cpu0 waking->wakeup 197 p50 94 p90 233
```

```svg
<svg viewBox="0 0 640 330" xmlns="http://www.w3.org/2000/svg">
  <line x1="70" y1="260.0" x2="620" y2="260.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="264.0" text-anchor="end" font-size="12">0%</text>
  <line x1="70" y1="222.0" x2="620" y2="222.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="226.0" text-anchor="end" font-size="12">10%</text>
  <line x1="70" y1="184.0" x2="620" y2="184.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="188.0" text-anchor="end" font-size="12">20%</text>
  <line x1="70" y1="146.0" x2="620" y2="146.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="150.0" text-anchor="end" font-size="12">30%</text>
  <line x1="70" y1="108.0" x2="620" y2="108.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="112.0" text-anchor="end" font-size="12">40%</text>
  <line x1="70" y1="70.0" x2="620" y2="70.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="62" y="74.0" text-anchor="end" font-size="12">50%</text>
  <line x1="70" y1="260" x2="620" y2="260" stroke="currentColor"/>
  <line x1="70" y1="70" x2="70" y2="260" stroke="currentColor"/>
  <rect x="82.3" y="153.9" width="26" height="106.1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="95.3" y="149.9" text-anchor="middle" font-size="12">28</text>
  <rect x="110.3" y="247.9" width="26" height="12.1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="123.3" y="243.9" text-anchor="middle" font-size="12">3</text>
  <text x="109.3" y="278" text-anchor="middle" font-size="12">&lt;0.25</text>
  <rect x="160.9" y="78.7" width="26" height="181.3" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="173.9" y="74.7" text-anchor="middle" font-size="12">48</text>
  <rect x="188.9" y="171.5" width="26" height="88.5" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="201.9" y="167.5" text-anchor="middle" font-size="12">23</text>
  <text x="187.9" y="278" text-anchor="middle" font-size="12">0.25–0.5</text>
  <rect x="239.4" y="215.6" width="26" height="44.4" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="252.4" y="211.6" text-anchor="middle" font-size="12">12</text>
  <rect x="267.4" y="97.1" width="26" height="162.9" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="280.4" y="93.1" text-anchor="middle" font-size="12">43</text>
  <text x="266.4" y="278" text-anchor="middle" font-size="12">0.5–1</text>
  <rect x="318.0" y="236.9" width="26" height="23.1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="331.0" y="232.9" text-anchor="middle" font-size="12">6</text>
  <rect x="346.0" y="183.6" width="26" height="76.4" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="359.0" y="179.6" text-anchor="middle" font-size="12">20</text>
  <text x="345.0" y="278" text-anchor="middle" font-size="12">1–2</text>
  <rect x="396.6" y="238.8" width="26" height="21.2" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="409.6" y="234.8" text-anchor="middle" font-size="12">6</text>
  <rect x="424.6" y="229.8" width="26" height="30.2" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="437.6" y="225.8" text-anchor="middle" font-size="12">8</text>
  <text x="423.6" y="278" text-anchor="middle" font-size="12">2–4</text>
  <rect x="475.1" y="256.1" width="26" height="3.9" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="488.1" y="252.1" text-anchor="middle" font-size="12">1</text>
  <rect x="503.1" y="254.0" width="26" height="6.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="516.1" y="250.0" text-anchor="middle" font-size="12">2</text>
  <text x="502.1" y="278" text-anchor="middle" font-size="12">4–8</text>
  <rect x="553.7" y="260.0" width="26" height="0.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="566.7" y="256.0" text-anchor="middle" font-size="12">0</text>
  <rect x="581.7" y="256.0" width="26" height="4.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="594.7" y="252.0" text-anchor="middle" font-size="12">1</text>
  <text x="580.7" y="278" text-anchor="middle" font-size="12">&gt;8</text>
  <text x="345" y="304" text-anchor="middle" font-size="13">sched_waking → 리더가 CPU에 올라탄 sched_switch까지 (ms)</text>
  <text x="22" y="165" text-anchor="middle" font-size="12" transform="rotate(-90 22 165)">샘플 비율</text>
  <rect x="90" y="14" width="14" height="12" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="110" y="25" font-size="12">같은 CPU: 리더·라이터 모두 cpu0 (p50 0.35 ms, n=197)</text>
  <rect x="90" y="36" width="14" height="12" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="110" y="47" font-size="12">다른 CPU: 라이터 cpu0 → 리더 cpu1 (p50 0.74 ms, n=189)</text>
</svg>
```
그림 6 — 깨우기 → 실행 지연 분포 (ftrace 기준, QEMU TCG `-smp 2`, 직접 측정). 같은 CPU에서는 깨우는 순간 바로 선점해 넘겨주므로 중앙값 0.35 ms, 다른 CPU에서는 IPI → idle 탈출 → 문맥 교환을 거쳐 0.74 ms. **절대값은 TCG 에뮬레이션 때문에 크다** — 실제 Cortex-A 보드라면 같은 경로가 수십 µs 수준이다(측정하지 않은 일반적인 값, 기억에 의존). 비교해서 볼 것은 "다른 CPU가 대략 두 배, 꼬리가 더 길다"는 모양이다.

실무에서 이 측정이 쓰이는 곳: 센서 샘플이 늦게 온다는 지터 문제(contents.md의 "가끔 센서 샘플이 늦게 온다" → F6–F8, M5–M6, N1–N4)에서 "IRQ → `wake_up` → 데몬 `read()` 리턴" 중 어느 구간이 긴지 쪼개 보는 첫 단계가 바로 이 세 이벤트다. 더 정확한 측정은 `wakeup` 트레이서와 `cyclictest`(M5, N1)로 한다.

---

## 9. 커널 안에서는 무슨 일이 일어나나 — 한 장 요약

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="165" y="20" text-anchor="middle" font-size="13" font-weight="bold">리더: read() 가 잠드는 길</text>
  <text x="515" y="20" text-anchor="middle" font-size="13" font-weight="bold">라이터/IRQ: 깨우는 길</text>
  <rect x="20" y="32" width="290" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="165" y="51" text-anchor="middle" font-size="12">svc #0 → ksys_read → vfs_read</text>
  <rect x="20" y="72" width="290" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="165" y="91" text-anchor="middle" font-size="12">f_op-&gt;read = f3_read: 비었음, 락 해제</text>
  <rect x="20" y="112" width="290" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="165" y="131" text-anchor="middle" font-size="12">wait_event_interruptible(f3_rq, ...)</text>
  <rect x="20" y="152" width="290" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="165" y="171" text-anchor="middle" font-size="12">prepare_to_wait_event: 등록 + S</text>
  <rect x="20" y="192" width="290" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="165" y="211" text-anchor="middle" font-size="12">schedule() → __schedule()</text>
  <rect x="20" y="232" width="290" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="165" y="251" text-anchor="middle" font-size="12">try_to_block_task: runqueue에서 뺌</text>
  <rect x="20" y="300" width="290" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="165" y="319" text-anchor="middle" font-size="12">schedule() 다음 줄: 조건 재검사</text>
  <rect x="20" y="340" width="290" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="165" y="359" text-anchor="middle" font-size="12">finish_wait → kfifo_to_user → 리턴</text>
  <rect x="370" y="32" width="290" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="515" y="51" text-anchor="middle" font-size="12">write() → f3_write 또는 IRQ 핸들러</text>
  <rect x="370" y="72" width="290" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="515" y="91" text-anchor="middle" font-size="12">데이터 넣기 (락 아래) → 락 해제</text>
  <rect x="370" y="112" width="290" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="515" y="131" text-anchor="middle" font-size="12">wake_up(&amp;f3_rq) → __wake_up_common</text>
  <rect x="370" y="152" width="290" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="515" y="171" text-anchor="middle" font-size="12">autoremove_wake_function</text>
  <rect x="370" y="192" width="290" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="515" y="211" text-anchor="middle" font-size="12">try_to_wake_up: 상태 확인 → RUNNING</text>
  <rect x="370" y="232" width="290" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="515" y="251" text-anchor="middle" font-size="12">다른 CPU면 IPI → 그쪽 runqueue에 넣음</text>
  <line x1="165" y1="60" x2="165" y2="70" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="165" y1="100" x2="165" y2="110" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="165" y1="140" x2="165" y2="150" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="165" y1="180" x2="165" y2="190" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="165" y1="220" x2="165" y2="230" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="165" y1="328" x2="165" y2="338" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="515" y1="60" x2="515" y2="70" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="515" y1="100" x2="515" y2="110" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="515" y1="140" x2="515" y2="150" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="515" y1="180" x2="515" y2="190" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="515" y1="220" x2="515" y2="230" stroke="currentColor" marker-end="url(#ah5)"/>
  <polyline points="515,260 515,284 165,284 165,298" fill="none" stroke="#3f9a6b" stroke-width="2" marker-end="url(#ah5)"/>
  <text x="340" y="278" text-anchor="middle" font-size="12">스케줄러가 리더를 다시 고름</text>
  <line x1="310" y1="125" x2="368" y2="125" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="339" y="118" text-anchor="middle" font-size="12">같은 rq</text>
</svg>
```
그림 7 — 잠드는 길(왼쪽)과 깨우는 길(오른쪽)이 `f3_rq` 하나에서 만난다. 왼쪽은 예제 3의 `/proc/<pid>/stack`, 오른쪽 아래는 예제 11의 `sched_waking`/`sched_wakeup` trace에서 실제로 본 경로다.

드라이버를 쓸 때 다시 만나는 지점:

- **G4/G7 char·misc device**: `.read`/`.write`의 blocking + `O_NONBLOCK` 처리 = §2.6 그대로.
- **G6 poll/epoll**: `.poll`의 `poll_wait()`이 같은 wait queue에 걸린다. 깨우는 쪽은 `wake_up_poll()`/`wake_up_interruptible_poll()`로 이벤트 마스크를 키로 넘기면 epoll이 관심 없는 이벤트로 헛깨지 않는다.
- **F6 인터럽트 / F7 지연 작업**: IRQ 핸들러(또는 threaded IRQ)가 FIFO에 넣고 `wake_up()`, 혹은 DMA 완료에 `complete()`. 핸들러에서는 절대 `wait_event`/`wait_for_completion`을 부르지 않는다(F2).
- **F4 락**: mutex, semaphore도 내부적으로 "대기 리스트 + 상태 + `schedule()`"이다. "락을 쥔 채로 잠들지 마라"는 스핀락 기준이고, mutex를 쥐고 잠드는 건 허용되지만 같은 mutex를 잡아야 하는 깨우는 쪽과 교착이 생긴다 (§2.6에서 락을 놓고 잔 이유).
- **H 버스 드라이버**: I2C/SPI 컨트롤러 드라이버의 전송 함수가 거의 다 `reinit_completion` → 하드웨어 시작 → `wait_for_completion_timeout` 모양이다 (예: `drivers/i2c/busses/`의 많은 드라이버).
- **F8 시간**: `msleep()` = `schedule_timeout_uninterruptible()`. 결국 이 노트의 "상태 + schedule"이다.

---

## 10. Embedded 실무에서 다시 보기

### 10.1 IMU 드라이버의 FIFO watermark 인터럽트

예를 들어 센서 키트의 SPI IMU가 FIFO에 샘플 32개가 쌓이면 IRQ를 올린다. threaded IRQ 핸들러(F6)가 SPI로 FIFO를 읽어 커널 링버퍼에 넣고 `wake_up_interruptible_poll(&imu->rq, EPOLLIN)`을 부른다. 유저 데몬은 epoll로 IMU, GNSS UART, CAN 소켓을 함께 기다린다. 이 구조에서 나오는 버그 셋:

- 핸들러가 링버퍼 인덱스를 갱신하기 **전에** 깨운다 → 데몬이 깨서 빈 버퍼를 보고 다시 잠 → 다음 IRQ까지(32 샘플 = 수십 ms) 지연.
- 깨우기 전에 `if (waitqueue_active())`로 최적화했다 → 배리어 누락으로 아주 가끔 lost wakeup (§5.2).
- 데몬 종료(SIGTERM) 시 read가 `wait_event()`(D)라서 systemd가 90초 뒤 SIGKILL을 보내도 안 죽는다 → 장비 재부팅 시간이 늘어난다. `_interruptible`로 고친다.

### 10.2 카메라/DMA: "프레임 완료" completion과 타임아웃

V4L2 드라이버(K장 계열)나 사내 DMA 엔진 드라이버에서 "DMA 시작 → `wait_for_completion_timeout(…, msecs_to_jiffies(100))`"을 쓴다. 타임아웃 경로에서 하드웨어를 멈추지 않으면 예제 9의 "-53 ms IRQ"가 실제로 일어난다. 리뷰 체크리스트: 타임아웃 뒤 ① DMA 엔진 정지, ② `synchronize_irq()`, ③ 다음 요청 전 `reinit_completion()`.

### 10.3 펌웨어 로딩·리셋을 기다리는 사용자 명령

MCU 보조칩에 펌웨어를 올리는 ioctl은 수 초가 걸린다. 사용자가 Ctrl-C를 눌렀다고 중간에 빠지면 칩이 반쯤 프로그래밍된 상태가 된다. 이럴 때 `wait_event_killable()`/`wait_for_completion_killable()`을 쓴다: 일반 시그널은 무시하고 끝까지 가되, 시스템 종료나 OOM killer의 SIGKILL은 받아들인다. 예제 6에서 KILLABLE이 SIGALRM을 무시하고 끝까지 기다린 것이 이 동작이다.

### 10.4 "멈춘 장비" 현장 진단 순서

```sh
ps -eo pid,stat,wchan:32,comm | awk '$2 ~ /D/'     # D 상태와 잠든 함수
cat /proc/<pid>/stack                               # 커널 스택 (root)
echo w > /proc/sysrq-trigger; dmesg | tail -50     # 모든 D 태스크의 스택 덤프
```

(위 세 줄은 일반 배포판 기준 명령으로, 이 노트의 BusyBox 게스트에서는 실행하지 않았다. `/proc/<pid>/stack`과 `/proc/<pid>/wchan`은 예제 3에서 실행했다.) `wchan`이 내 드라이버 함수라면 그 wait queue를 깨우는 경로가 왜 안 불렸는지(IRQ가 안 왔나, 깨우는 쪽이 `_interruptible`인가, lost wakeup인가)를 본다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| "검사 → 등록 → 잠" 순서로 손으로 짬 | 가끔 read가 다음 이벤트까지 늦게 깸, 드물게 영원히 | lost wakeup (§5, 예제 7) | `wait_event_*` 사용, 손으로 짤 땐 `prepare_to_wait` 먼저 |
| 유저 I/O를 `wait_event()`(D)로 대기 | `kill -9` 안 먹음, hung task 경고, loadavg 상승 | `TASK_UNINTERRUPTIBLE` (예제 3·4) | `wait_event_interruptible` 또는 `_killable` |
| 대기는 `_killable`, 깨우기는 `wake_up_interruptible()` | 데이터가 있는데 리더가 안 깸 | ttwu 상태 마스크 불일치 (예제 5) | `wake_up()` 사용 |
| `-ERESTARTSYS` 무시하고 루프 재시도 | Ctrl-C 안 먹고 S 상태로 영원히 대기 | 시그널 확인을 버림 | 반환값을 그대로 위로 return |
| 데이터 일부 복사 후 `-ERESTARTSYS` 반환 | 재시작된 read가 데이터 일부를 잃음 | 재시작 약속 위반 | 이미 처리한 바이트 수 반환 |
| 스핀락(또는 mutex)을 쥔 채 `wait_event` | 교착, `BUG: sleeping function called from invalid context` | 원자 컨텍스트에서 잠 (F2), 깨우는 쪽이 락을 못 잡음 | 락 풀고 대기, 깨면 락 잡고 재검사 |
| 깨어난 뒤 조건을 락 아래서 재검사 안 함 | 빈 FIFO에서 읽기, 언더런 | 다른 리더가 먼저 가져감 | §2.6의 `for (;;)` 이중 확인 |
| `reinit_completion()` 누락 / 타임아웃 뒤 IRQ 방치 | 다음 전송이 즉시 "완료", 데이터 오염 | 늦은 `complete()`가 `done`에 남음 (예제 9) | 시작 전 `reinit`, 타임아웃 시 HW 정지 + `synchronize_irq` |
| IRQ 핸들러에서 `wait_for_completion` | `scheduling while atomic` 패닉 | 하드IRQ는 잠들 수 없음 | 핸들러는 `complete()`만, 대기는 프로세스 컨텍스트 |
| 대기자 여럿인데 `wake_up_all` 남용 / 비배타 대기 | CPU 스파이크, 지연 증가 | thundering herd (예제 8) | `_exclusive` 대기 + `wake_up()` |

---

## 12. 면접에서 이렇게 말한다

**Q.** Implement a blocking read in a character driver. Walk me through it.

**A.** 장치 구조체에 링버퍼, 그걸 지키는 mutex, 그리고 `wait_queue_head_t`를 둔다. `read`에서는 mutex를 잡고 버퍼가 비었는지 본다. 비었으면 락을 풀고, `O_NONBLOCK`이면 `-EAGAIN`, 아니면 `wait_event_interruptible(dev->rq, !kfifo_is_empty(...))`로 잔다. 반환값이 0이 아니면 `-ERESTARTSYS`를 그대로 돌려준다. 깨어나면 다른 리더가 먼저 가져갔을 수 있으니 루프를 돌아 락 아래서 다시 확인하고, 데이터가 있으면 `copy_to_user`(나 `kfifo_to_user`)로 복사하고 바이트 수를 돌려준다. `write`나 IRQ 핸들러는 버퍼에 넣고 락을 푼 뒤 `wake_up(&dev->rq)`를 부른다. 같은 wait queue로 `.poll`에서 `poll_wait`을 걸면 poll/epoll도 지원된다.

> I keep a ring buffer, a mutex protecting it, and a wait_queue_head_t in the device struct. In read, I take the mutex and check whether the buffer is empty. If it is, I drop the lock — never sleep holding it — and if the file is O_NONBLOCK I return -EAGAIN; otherwise I call wait_event_interruptible on the queue with "buffer not empty" as the condition. If that returns nonzero I propagate -ERESTARTSYS so the signal code can turn it into EINTR or restart the call. When I wake up I loop back and re-check under the lock, because another reader may have taken the data, then copy_to_user and return the byte count. The producer — write or the IRQ handler — fills the buffer, releases the lock, and calls wake_up. The .poll method calls poll_wait on the same queue and returns EPOLLIN when there's data, so select, poll and epoll work too.

**꼬리 질문**: "Why not just loop with msleep(1) until data arrives?" → 지연이 최소 1 jiffy(HZ=250이면 4 ms)로 늘고, 데이터가 없어도 계속 깨어나 CPU와 전력을 쓰며, poll과 연결할 방법이 없다.

**Q.** What is the lost-wakeup problem and how does wait_event avoid it?

**A.** 조건을 검사하고 잠들기 사이에 깨우기가 오면, 그 순간 대기열에 아무도 없어서 깨우기가 사라지고 태스크는 조건이 이미 참인데 잠든다. `wait_event`는 순서를 바꾼다: 먼저 대기열에 등록하고 상태를 `TASK_INTERRUPTIBLE`로 바꾼 뒤(`set_current_state`가 배리어 포함) 조건을 검사하고, 거짓일 때만 `schedule()`한다. 등록 후에 깨우기가 오면 ttwu가 상태를 RUNNING으로 돌려놓으니 `schedule()`이 바로 돌아온다. 저는 이 경쟁을 `mdelay`로 창을 벌려 QEMU에서 매번 재현했는데, 버그 버전은 데이터가 50 ms에 왔는데도 2초 타임아웃까지 잤고, 순서만 바꾸니 바로 받았다.

> If you check the condition and then go to sleep, a wakeup that lands in between finds nobody on the queue and is lost, so you sleep with the condition already true. wait_event orders it the other way: it adds itself to the queue and sets the task state first — set_current_state has a full barrier — then tests the condition, and only calls schedule if it's still false. If the wakeup comes after the enqueue, try_to_wake_up flips the state back to running and schedule returns immediately. The waker takes the same wait-queue spinlock, so both sides are ordered. I reproduced it by widening the window with mdelay: the buggy version slept until its two-second timeout even though data arrived at 50 ms, and the fixed ordering returned right away.

**Q.** What's the difference between TASK_INTERRUPTIBLE, TASK_UNINTERRUPTIBLE and TASK_KILLABLE? When do you use each?

**A.** 셋 다 runqueue에서 빠진 잠이고, 무엇이 깨울 수 있느냐만 다르다. INTERRUPTIBLE은 아무 시그널이나 깨우고 `ps`에 S로 보인다 — 유저가 기다리는 I/O의 기본값이다. UNINTERRUPTIBLE은 명시적 wake_up만 깨우고 D로 보이며, loadavg에 잡히고 hung task 감지 대상이다 — 아주 짧고 반드시 끝나는 대기에만 쓴다. KILLABLE은 D로 보이지만 SIGKILL 같은 치명적 시그널은 받는다 — 중간에 끊으면 곤란한 긴 작업에 쓴다. 실제로 hung task 타임아웃을 3초로 줄여 보니 UNINTERRUPTIBLE만 경고를 받았고 KILLABLE과 IDLE은 제외됐다.

> All three take the task off the runqueue; they differ in what can wake it. Interruptible wakes on any signal and shows as S — that's the default for anything a user is waiting on. Uninterruptible ignores signals, shows as D, counts toward the load average and is what the hung-task detector watches, so it's only for short waits that are guaranteed to finish. Killable also shows as D but fatal signals like SIGKILL still wake it, which is right for long operations you don't want a stray signal to abort, like firmware download. When I dropped hung_task_timeout_secs to three seconds, only the uninterruptible sleeper was reported; killable and TASK_IDLE are excluded by design.

**꼬리 질문**: "A process is stuck in D state and kill -9 does nothing. How do you debug it?" → `cat /proc/<pid>/stack`과 `wchan`으로 잠든 함수를 찾고, `echo w > /proc/sysrq-trigger`로 모든 D 태스크 스택을 본다. 그 wait queue를 깨워야 할 IRQ나 완료 경로가 왜 안 왔는지 추적한다.

**Q.** When would you use a completion instead of a wait queue?

**A.** "이 한 번의 일이 끝났다"를 알릴 때다 — DMA 완료, 펌웨어 응답, 스레드 종료. completion은 내부에 카운트(`done`)가 있어서 `complete()`가 대기보다 먼저 와도 사라지지 않고, 조건 변수를 따로 둘 필요가 없어 순서 실수를 할 수 없다. 반대로 "FIFO가 비어 있지 않다"처럼 상태가 참·거짓을 오가거나 poll을 지원해야 하면 wait queue다. completion을 쓸 때는 하드웨어를 시작하기 전에 `reinit_completion`, 타임아웃이면 하드웨어를 멈추고 IRQ를 동기화해야 한다 — 안 그러면 늦은 IRQ가 다음 요청을 완료로 속인다. 제 실험에서 그 경우 다음 전송이 78 µs 만에 "완료"됐다.

> A completion is for "this one event happened" — DMA done, a firmware reply, a thread exiting. It carries a counter, so a complete that arrives before the wait isn't lost, and there's no separate condition to get the ordering wrong. A wait queue is for "wait until some state is true", like a FIFO being non-empty, where the condition flips back and forth and where I also need poll support. With completions the classic bug is reuse: reinit before kicking the hardware, and on timeout stop the hardware and synchronize the IRQ, otherwise a late interrupt from the timed-out request completes the next one. I reproduced that — the next transfer "completed" in 78 microseconds with a stale IRQ.

**Q.** What does -ERESTARTSYS mean, and what should a driver do with it?

**A.** "시그널 때문에 잠에서 깼고, 아무 일도 안 했으니 다시 불러도 된다"는 커널 내부 코드다. 드라이버는 그대로 위로 돌려준다. syscall 복귀 경로가 핸들러의 `SA_RESTART`를 보고 유저에게 `EINTR`을 주거나 syscall을 자동 재시작한다. 유저에게 그대로 새면 안 되고, 데이터를 일부 처리한 뒤라면 `-ERESTARTSYS` 대신 처리한 바이트 수를 돌려줘야 한다. 같은 드라이버 코드로 `SA_RESTART` 없이 1초에 EINTR, 있으면 계속 기다려 2초에 데이터를 받는 것을 확인했다.

> It's a kernel-internal code meaning "a signal interrupted the sleep and nothing has been done, so it's safe to retry." The driver just returns it. On the way back to user space the signal code checks the handler's SA_RESTART flag and either converts it to EINTR or rewinds the PC so the syscall runs again. It must never leak to user space, and if I already consumed part of the data I return the partial count instead, because a restart would lose those bytes.

**Q.** What is the thundering herd problem in wait queues and how do you avoid it?

**A.** 여러 태스크가 같은 wait queue에서 자는데 이벤트 하나로 전부 깨어나고 한 명만 일을 얻는 것이다. 나머지는 깨어나 조건을 보고 다시 잔다 — CPU 낭비와 지연. `wait_event_interruptible_exclusive`로 배타적 대기를 하면 엔트리가 꼬리에 `WQ_FLAG_EXCLUSIVE`로 붙고, `wake_up()`은 배타적 대기자를 한 명만 깨우고 멈춘다. 리더 8개로 재 보니 비배타는 조건이 15번 평가됐고(1 + 7×2), 배타는 1번이었다.

> It's when many tasks sleep on the same queue, one event wakes all of them, one wins and the rest re-check and go back to sleep — wasted CPU and latency. Exclusive waits fix it: wait_event_interruptible_exclusive queues the entry at the tail with WQ_FLAG_EXCLUSIVE, and wake_up stops after waking one exclusive waiter. Non-exclusive waiters like poll still all get woken. With eight readers I counted condition evaluations: fifteen for one byte without exclusive, exactly one with it.

**Q.** Can you call wake_up() or complete() from an interrupt handler? What about wait_event()?

**A.** `wake_up()`과 `complete()`는 된다 — 내부에서 스핀락(`irqsave`)만 잡고 잠들지 않는다. `wait_event_*`와 `wait_for_completion*`은 안 된다. 잠드는 함수이고 하드IRQ, softirq, 스핀락을 쥔 구간 같은 원자 컨텍스트에서는 잘 수 없다. 디버그 커널의 `DEBUG_ATOMIC_SLEEP`이 이걸 잡아 `sleeping function called from invalid context`를 찍는다. 그래서 패턴은 항상 "IRQ가 깨우고, 프로세스 컨텍스트가 기다린다"이다.

> Yes for wake_up and complete — they only take a spinlock with irqsave and never sleep, so hardirq and softirq context are fine; my demo's timer callback calls complete. No for wait_event and wait_for_completion — those sleep, and you can't sleep in atomic context, whether that's an IRQ handler, a softirq, or while holding a spinlock. CONFIG_DEBUG_ATOMIC_SLEEP catches it with "sleeping function called from invalid context". So the pattern is always: the interrupt wakes, process context waits.

---

## 13. 직접 해보기

1. **개념**: `set_current_state(TASK_INTERRUPTIBLE)`과 `schedule()` 사이에 타이머 인터럽트로 선점되면 태스크는 runqueue에서 빠지는가? 이유를 소스 한 줄로 대라.
   정답/힌트: 빠지지 않는다. `__schedule()`의 `else if (!preempt && prev_state)` — 선점 경로에서는 `try_to_block_task()`를 부르지 않는다.

2. **코드**: `f3_queue`의 write 쪽도 FIFO가 꽉 차면 잔다. 리더 없이 300바이트를 blocking write 하는 프로그램을 짜고, 다른 스레드가 1초 뒤 100바이트를 읽으면 write가 깨서 나머지를 쓰는지 확인하라. 첫 write의 반환값은 얼마인가?
   정답/힌트: `kfifo_from_user`는 남은 공간(256)만큼만 복사하므로 첫 write는 256을 돌려주는 짧은 쓰기다. 두 번째 write가 잠들었다가 깬다.

3. **코드**: `wmode=6`의 손으로 짠 루프에서 `finish_wait()`를 시그널 경로에서 빼먹으면 어떤 일이 생길지 예측하고, 왜 위험한지 설명하라 (실제로 돌리지는 말 것).
   정답/힌트: 스택 위의 `wait` 엔트리가 `f3_rq`에 남는다. 함수가 리턴해 스택이 재사용된 뒤 다음 `wake_up()`이 쓰레기 포인터의 `func`을 부른다 → Oops 또는 메모리 오염.

4. **/proc 관찰**: 예제 3을 `wmode=4`(exclusive)로 돌려 State와 `wchan`을 보라. 같은 S인가?
   정답/힌트: S다. exclusive 여부는 상태가 아니라 wait queue 엔트리의 플래그라 `/proc`에는 드러나지 않는다.

5. **ftrace 관찰**: 예제 11에 `sched_switch` 대신 `wakeup` 트레이서(`echo wakeup > /sys/kernel/tracing/current_tracer`)를 써서 최대 wakeup 지연(`tracing_max_latency`)을 구하라. 디버그 커널에 `CONFIG_SCHED_TRACER`가 있다.
   정답/힌트: `wakeup` 트레이서는 가장 높은 우선순위 태스크를 따라가므로 리더를 `chrt -f 50`처럼 RT로 올려야 잘 잡힌다(BusyBox에 `chrt`가 있는지 먼저 확인).

6. **코드**: `f3_comp.c`의 타임아웃 경로를 실제 하드웨어 드라이버처럼 바꿔 보라: 타이머 대신 `request_irq`를 쓴다고 가정하고 "HW 정지 → `synchronize_irq()` → 에러 반환" 순서를 의사코드로 쓰라.
   정답/힌트: `writel(STOP, base + CTRL); synchronize_irq(irq); return -ETIMEDOUT;` 그리고 다음 요청 시작 직전 `reinit_completion()`.

7. **개념**: `complete()`가 `wait_for_completion()`보다 먼저 불리면? 같은 상황에서 `wake_up()`이 `wait_event()`보다 먼저 불리면?
   정답/힌트: completion은 `done`이 1로 남아 대기가 즉시 통과한다. wait queue는 깨우기 자체는 사라지지만, `wait_event`가 처음에 조건을 검사하므로 조건(데이터)이 남아 있으면 역시 잠들지 않는다 — 조건이 상태로 남는 한 안전하다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| wait queue | 대기열 | `wait_queue_head_t` + 엔트리 리스트. "이 조건을 기다리는 태스크" 목록 |
| wait queue entry | 대기열 엔트리 | 잠드는 태스크마다 스택 위에 하나. `private` = task, `func` = 깨울 함수 |
| `wait_event_*` | 조건 대기 매크로 | 등록 → 상태 → 검사 → `schedule()` 루프를 대신 짜 줌 |
| `TASK_INTERRUPTIBLE` | S 상태 | 시그널로도 깨는 잠. 유저 I/O 기본 |
| `TASK_UNINTERRUPTIBLE` | D 상태 | 시그널 무시. `kill -9` 안 먹음, hung task 대상 |
| `TASK_KILLABLE` | D 상태(죽일 수 있음) | 치명적 시그널만 받는 잠 |
| `TASK_IDLE` | I 상태 | D와 같지만 loadavg·hung task에서 제외 |
| `schedule()` | 스케줄러 호출 | 상태가 RUNNING이 아니면 runqueue에서 빼고 문맥 교환 |
| `try_to_wake_up()` (ttwu) | 깨우기 | 상태 마스크 확인 → RUNNING → runqueue에 넣음 (필요하면 IPI) |
| lost wakeup | 깨우기 유실 | 검사와 잠 사이에 온 깨우기를 놓쳐 조건이 참인데 자는 버그 |
| thundering herd | 우르르 깨움 | 이벤트 하나에 대기자 전원이 깨어나는 낭비 |
| exclusive wait | 배타적 대기 | `WQ_FLAG_EXCLUSIVE`. `wake_up()`이 한 명만 깨움 |
| `-ERESTARTSYS` | 재시작 가능 에러 | 시그널로 깼음. 유저에겐 EINTR 또는 자동 재시작 |
| completion | 완료 객체 | `done` 카운트 + 대기열. 한 번의 사건 알림 |
| `reinit_completion()` | 재초기화 | `done = 0`. 다음 요청 시작 전에 |
| `complete_all()` | 전원 해제 | `done = UINT_MAX`. 영구히 열림 |
| `wait_var_event()` | 변수 대기 | 변수 주소 해시로 전역 wait queue를 빌려 씀 |
| `wchan` | wait channel | 잠든 태스크가 멈춘 커널 함수 이름 (`/proc/<pid>/wchan`) |
| hung task detector | 정지 태스크 감지기 | D(비-killable) 상태로 너무 오래 있는 태스크를 경고 |
| `sched_waking` / `sched_wakeup` | 스케줄러 tracepoint | 깨우기 시작(깨우는 쪽) / runqueue 진입(대상 CPU) |

---

## 15. 요약 & 체크리스트

커널에서 "잔다"는 자기 `__state`를 RUNNING이 아닌 값으로 바꾸고, 누가 찾을 수 있게 wait queue에 등록하고, `schedule()`로 runqueue에서 빠지는 것이다. "깨운다"는 wait queue를 훑어 `try_to_wake_up()`으로 상태를 RUNNING으로 돌리고 runqueue에 넣는 것이다. 잠의 종류(S, D, KILLABLE, I)는 무엇이 깨울 수 있는지만 다르고, 그 선택이 `kill -9`, loadavg, hung task 경고로 그대로 드러난다. `wait_event_*`의 가치는 **"등록 → 상태 → 검사 → 잠"** 순서를 지켜 lost wakeup을 막는 데 있다. 손으로 짤 때도 같은 순서와 모든 출구의 `finish_wait()`을 지킨다. 드라이버의 blocking read는 "락 아래 검사 → 락 풀고 `wait_event_interruptible` → `-ERESTARTSYS`는 그대로 반환 → 깨면 락 아래 재검사 → 복사"이고, 같은 wait queue를 `.poll`에 걸면 poll/epoll이 따라온다. "한 번의 일이 끝났다"는 completion으로 알리되, 재초기화와 타임아웃 뒤 늦은 IRQ 처리를 잊지 않는다.

- [ ] "잔다"를 `__state`, wait queue, `schedule()`, runqueue dequeue로 설명할 수 있다
- [ ] S / D / KILLABLE / I 상태가 무엇에 깨는지, `ps`·loadavg·hung task에서 어떻게 보이는지 말할 수 있다
- [ ] blocking `read()` + `O_NONBLOCK` + `poll()` + 시그널 + 타임아웃을 갖춘 드라이버를 짤 수 있다
- [ ] `wait_event_interruptible`의 반환값과 `-ERESTARTSYS` 처리 규칙(부분 처리 시 바이트 수)을 설명할 수 있다
- [ ] lost-wakeup 경쟁을 타임라인으로 그리고, `prepare_to_wait`/`finish_wait` 정석 루프를 손으로 쓸 수 있다
- [ ] `set_current_state()`의 배리어와 `waitqueue_active()` 함정을 설명할 수 있다
- [ ] `wake_up` / `wake_up_interruptible` / `wake_up_all`의 차이와 상태 마스크 함정을 말할 수 있다
- [ ] exclusive wait로 thundering herd를 줄이는 원리를 `__wake_up_common()` 루프로 설명할 수 있다
- [ ] completion으로 "IRQ/DMA 완료 대기"를 재초기화·타임아웃·늦은 IRQ까지 올바르게 짤 수 있다
- [ ] completion과 wait queue 중 무엇을 쓸지 상황별로 고를 수 있다
- [ ] `/proc/<pid>/stack`, `wchan`, ftrace `sched_waking`/`sched_wakeup`/`sched_switch`로 잠과 깨움을 관찰할 수 있다

## 참고 자료

- Linux 커널 소스 (랩 6.18.55): `include/linux/wait.h`, `kernel/sched/wait.c`, `include/linux/completion.h`, `kernel/sched/completion.c`, `kernel/sched/wait_bit.c`, `kernel/sched/core.c`(`try_to_wake_up`, `__schedule`), `kernel/hung_task.c`, `fs/proc/array.c` — 온라인: [elixir.bootlin.com/linux/v6.18/source](https://elixir.bootlin.com/linux/v6.18/source)
- docs.kernel.org — [Completions - "wait for completion" barrier APIs](https://docs.kernel.org/scheduler/completion.html)
- docs.kernel.org — [Kernel API: Wait queues and Wake events](https://docs.kernel.org/driver-api/basics.html)
- Jonathan Corbet, Alessandro Rubini, Greg Kroah-Hartman, "Linux Device Drivers, 3rd ed." 6장 "Advanced Char Driver Operations" (Blocking I/O, poll) — [lwn.net/Kernel/LDD3](https://lwn.net/Kernel/LDD3/) (API 일부는 옛 버전: `sleep_on`, `interruptible_sleep_on`은 이후 제거됨)
- Michael Kerrisk, "The Linux Programming Interface" 21.5 (시그널에 의한 시스템 호출 중단과 `SA_RESTART`)
- man7.org — [signal(7)](https://man7.org/linux/man-pages/man7/signal.7.html) "Interruption of system calls and library functions by signal handlers"
- docs.kernel.org — [Event Tracing](https://docs.kernel.org/trace/events.html) (이벤트 필터 문법)
- Bootlin, "Linux kernel and driver development training" 슬라이드 — Sleeping, wait queues 절 ([bootlin.com/training/kernel](https://bootlin.com/training/kernel/))
