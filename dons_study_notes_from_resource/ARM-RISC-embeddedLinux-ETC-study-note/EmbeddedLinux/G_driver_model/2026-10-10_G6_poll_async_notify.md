# G6. poll/select와 비동기 통지 — .poll, poll_wait, EPOLL 이벤트, fasync/SIGIO, eventfd

> **이 노트를 다 읽으면**: 드라이버의 `.poll`을 "등록(`poll_wait`) + 상태 보고(마스크)" 두 단계 계약으로 구현하고 select/poll/epoll이 모두 그 한 함수로 들어오는 경로를 그릴 수 있다 · 장치 상태를 `EPOLLIN`/`EPOLLOUT`/`EPOLLPRI`/`EPOLLHUP` 마스크로 설계하고, edge-triggered epoll·thundering herd·장치 제거까지 버티는 깨우기 규칙을 지킬 수 있다 · fasync/SIGIO, 커널발 eventfd, `sysfs_notify` 세 가지 대안 통지 경로를 언제 쓰는지 설명할 수 있다 · ftrace로 `.poll` 호출과 `ep_poll_callback` 깨우기를 직접 확인할 수 있다
> **왜 필요한가**: 드라이버 면접 단골 "Implement poll for your character driver"와 그 꼬리 질문(ET epoll이면? 장치가 빠지면? 여러 프로세스가 기다리면?)이 이 노트이고, 실무에서는 "센서 데몬이 가끔 샘플을 늦게 받는다", "장치를 뽑았더니 데몬이 영원히 잔다" 같은 버그가 여기서 나온다
> **Don 기준 난이도**: RTOS 이벤트 그룹에 비트 등록하고 ISR에서 비트 세우기, level/edge 인터럽트 차이는 이미 안다 / 새로 배울 부분은 Linux가 "기다리는 장소(wait queue)"와 "상태 질의(.poll)"를 분리한 방식, 깨우기에 실어 보내는 key, 그리고 장치 수명(kref)과 wait queue 수명을 묶는 법
> **선행 노트**: A4(select/poll/epoll 유저 쪽), A6(시그널), F3(wait queue와 기본 `.poll`), F2(실행 컨텍스트), F6(인터럽트) — G3(platform driver), G4(char device), G7(misc device), G8(sysfs)과 함께 읽으면 좋다

---

## 0. 큰 그림 — 이게 왜 필요한가

센서 키트의 메인 데몬을 떠올리자. 스레드 하나가 epoll로 IMU, GNSS UART, CAN 소켓, 타이머를 한꺼번에 기다린다 (A4 §8). 이 루프가 IMU 드라이버에게 묻는 질문은 딱 두 가지다.

1. "지금 읽을 게 있나? 쓸 자리는 있나? 특별한 일(모션 이벤트, FIFO overflow)이 있었나?"
2. "지금은 없다면, 생기는 순간 나를 깨워 줄 수 있나?"

드라이버는 이 두 질문에 `file_operations.poll` 하나로 답한다. F3에서 이미 짧은 `.poll`을 짰다. 이번 노트는 그 함수가 지켜야 하는 **계약 전체**를 다룬다. 무슨 비트를 언제 켜는지, 어느 wait queue를 어떤 key로 깨우는지, edge-triggered epoll 사용자에게 무엇을 보장해야 하는지, 장치가 사라질 때 잠든 사람들을 어떻게 내보내는지.

그리고 "fd를 기다린다" 말고도 드라이버가 유저에게 알리는 길이 세 개 더 있다. 시그널(fasync/SIGIO), 유저가 넘겨 준 eventfd를 커널이 울리기, sysfs 속성 파일을 poll 가능하게 만드는 `sysfs_notify`. 이 노트의 실습 드라이버 `g6_sensorkit`(가상의 IMU)은 네 가지를 모두 구현한다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="120" width="130" height="70" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="75" y="148" text-anchor="middle" font-size="13">IMU FIFO</text>
  <text x="75" y="168" text-anchor="middle" font-size="12">watermark IRQ</text>
  <line x1="140" y1="155" x2="176" y2="155" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="180" y="30" width="210" height="270" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="285" y="54" text-anchor="middle" font-size="14" font-weight="bold">g6_sensorkit 드라이버</text>
  <rect x="192" y="72" width="186" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="285" y="97" text-anchor="middle" font-size="12">wake_up_poll(rq, key)</text>
  <rect x="192" y="127" width="186" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="285" y="152" text-anchor="middle" font-size="12">kill_fasync(SIGIO)</text>
  <rect x="192" y="182" width="186" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="285" y="207" text-anchor="middle" font-size="12">eventfd_signal(ctx)</text>
  <rect x="192" y="237" width="186" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="285" y="262" text-anchor="middle" font-size="12">sysfs_notify_dirent(kn)</text>
  <line x1="378" y1="92" x2="426" y2="92" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="378" y1="147" x2="426" y2="147" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="378" y1="202" x2="426" y2="202" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="378" y1="257" x2="426" y2="257" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="430" y="72" width="240" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="550" y="97" text-anchor="middle" font-size="12">select / poll / epoll_wait 깨움</text>
  <rect x="430" y="127" width="240" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="550" y="152" text-anchor="middle" font-size="12">시그널 핸들러가 끼어듦</text>
  <rect x="430" y="182" width="240" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="550" y="207" text-anchor="middle" font-size="12">유저의 eventfd 카운터 +1</text>
  <rect x="430" y="237" width="240" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="550" y="262" text-anchor="middle" font-size="12">sysfs 속성 파일 poll이 깨어남</text>
  <text x="550" y="40" text-anchor="middle" font-size="13" font-weight="bold">유저스페이스가 보는 것</text>
</svg>
```
그림 1 — 한 번의 "새 샘플/이벤트"를 드라이버가 유저에게 알리는 네 갈래. 위에서부터 가장 일반적인 순서다. 1번(`.poll` + wait queue)이 기본이고 나머지는 특수한 요구가 있을 때 덧붙인다.

bare-metal 비유로 보면 `.poll`은 RTOS의 `xEventGroupWaitBits()`를 **두 조각으로 쪼갠 것**이다. RTOS에서는 "비트를 검사하고, 없으면 그 이벤트 그룹의 대기 리스트에 들어가 잔다"가 한 함수 안에서 끝난다. Linux 드라이버의 `.poll`은 앞 절반, 즉 "대기 리스트 주소를 알려 주고(`poll_wait`), 지금 비트를 돌려준다(마스크)"만 한다. 잠드는 뒤 절반은 VFS(select/poll)나 epoll이 맡는다. 이렇게 쪼갰기 때문에 fd 1000개를 한 번에 기다릴 수 있다 — 드라이버 1000개가 각자 잠들 수는 없으니까.

이 노트의 실습:

| 절 | 내용 | 실습 |
|---|---|---|
| §1 | `.poll`의 두 단계 계약, `__poll_t`, 호출 경로 | 소스 확인 |
| §2 | 실습 드라이버 `g6_sensorkit` 전체 코드 | 예제 1 (빌드) |
| §3 | 이벤트 마스크 설계 — IN/OUT/PRI/HUP | 예제 2 (select·poll·epoll로 같은 상태 보기) |
| §4 | edge-triggered epoll이 드라이버에 요구하는 것 | 예제 3 (ET 멈춤 버그 재현과 수정) |
| §5 | wait queue 여러 개와 wake key | 예제 4 (헛깨움 횟수 세기) |
| §6 | `EPOLLEXCLUSIVE`와 thundering herd | 예제 5 |
| §7 | fasync / SIGIO | 예제 6 (시그널 합쳐짐) |
| §8 | 커널이 울리는 eventfd | 예제 7 |
| §9 | `sysfs_notify` — 속성 파일 poll | 예제 8 |
| §10 | 장치 제거 중의 poll — `EPOLLHUP`과 수명 | 예제 9 (unbind로 hot-unplug) |
| §11 | ftrace로 경로 확인 | 예제 10, 11 |
| §12 | 깨우기 → 유저 도착 지연: poll vs epoll vs SIGIO vs eventfd | 예제 12 + 차트 |

> 실행 환경: 테스트 프로그램은 Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`, gcc 15)에서 `-static -pthread`로 빌드했고, 커널 모듈과 테스트는 VM 안 공용 랩의 QEMU `virt` 게스트(`-cpu cortex-a57 -smp 2 -m 512M`, **TCG 에뮬레이션**)에서 돌렸다. 커널은 주로 디버그 커널 `/var/lab/out/Image-dbg`(`6.18.55-dbg`: ftrace, lockdep, `CONFIG_PREEMPT=y`, `CONFIG_HZ=250`)이고, §12 지연 측정만 lockdep 부담이 없는 일반 랩 커널 `out/Image`(`6.18.55`)로 다시 돌렸다. 커널 소스 인용은 `/var/lab/src/linux-6.18.55` 그대로다. 타임스탬프·PID·minor 번호는 실행마다 다르고, **시간 값은 TCG라 실제 Cortex-A 보드보다 몇 배 크다** — 비교와 모양만 읽자.

---

## 1. 드라이버 쪽 계약 — `.poll`은 "등록 + 보고" 두 단계

### 1.1 직관 — 접수창구 비유

`.poll`을 은행 창구 직원이라고 하자. 손님(select/poll/epoll)이 와서 "제 일 처리됐어요?"라고 묻는다. 직원은 두 가지를 한다.

- **연락처를 받아 둔다** — "아직이면 이 번호로 연락드릴게요." 이것이 `poll_wait(file, &wq, pt)`다. 손님의 "연락처"(poll table이 만든 wait queue 엔트리)를 장치의 wait queue에 걸어 둔다.
- **지금 상태를 바로 말해 준다** — "입금은 됐고, 출금은 아직이에요." 이것이 리턴값 마스크다.

직원은 **손님을 창구 앞에 붙잡아 두지 않는다**. 기다릴지 말지는 손님(VFS/epoll)이 정한다. 이게 핵심 규칙 "**`.poll`은 절대 잠들지 않는다**"의 의미다. `poll_wait()`라는 이름 때문에 "여기서 기다린다"고 착각하기 쉬운데, 실제로는 "기다릴 장소를 등록한다"는 뜻이다.

### 1.2 시그니처와 `__poll_t`

```c
/* include/linux/fs.h, struct file_operations */
__poll_t (*poll) (struct file *, struct poll_table_struct *);

/* include/uapi/linux/types.h */
typedef unsigned __bitwise __poll_t;
```

`__poll_t`는 그냥 `unsigned`지만 `__bitwise` 표시가 붙어 있어서, `sparse`(커널의 정적 분석기, `make C=1`)가 "`EPOLLIN` 계열이 아닌 정수와 섞어 쓰는 실수"를 잡아 준다. 그래서 드라이버는 유저 API의 `POLLIN`이 아니라 커널 쪽 `EPOLLIN`, `EPOLLRDNORM` 같은 `EPOLL*` 상수를 써야 한다 (옛 코드의 `unsigned int mask = POLLIN | POLLRDNORM;`은 4.16 무렵 일괄 변환됐다). 비트 값은 대부분의 아키텍처에서 `POLLIN`과 같다.

`poll_wait()`의 실제 코드는 놀랄 만큼 짧다.

```c
/* include/linux/poll.h (6.18.55) */
static inline void poll_wait(struct file * filp, wait_queue_head_t * wait_address, poll_table *p)
{
	if (p && p->_qproc) {
		p->_qproc(filp, wait_address, p);
		/*
		 * This memory barrier is paired in the wq_has_sleeper().
		 * See the comment above prepare_to_wait(), we need to
		 * ensure that subsequent tests in this thread can't be
		 * reordered with __add_wait_queue() in _qproc() paths.
		 */
		smp_mb();
	}
}
```

`_qproc`가 누구냐에 따라 "등록"의 의미가 바뀐다. select/poll이면 `__pollwait`(엔트리를 만들어 `add_wait_queue`), epoll이면 `ep_ptable_queue_proc`(콜백 `ep_poll_callback`을 단 엔트리), 그리고 **이미 등록을 마친 두 번째 이후 호출에는 `NULL`** 이라 아무 일도 안 한다. 그 뒤의 `smp_mb()`는 F3 §5에서 본 lost-wakeup 방지 순서 — "먼저 대기열에 등록하고, 그다음 상태를 검사한다" — 를 멀티코어에서 보장하는 장벽이다.

### 1.3 두 단계 계약과 그 뒤에 일어나는 일

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="40" y="10" width="140" height="32" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="110" y="31" text-anchor="middle" font-size="13">유저 태스크</text>
  <rect x="270" y="10" width="140" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="31" text-anchor="middle" font-size="13">VFS do_poll()</text>
  <rect x="490" y="10" width="160" height="32" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="31" text-anchor="middle" font-size="13">드라이버 g6_poll</text>
  <line x1="110" y1="42" x2="110" y2="340" stroke="#888" stroke-dasharray="4 4"/>
  <line x1="340" y1="42" x2="340" y2="340" stroke="#888" stroke-dasharray="4 4"/>
  <line x1="570" y1="42" x2="570" y2="340" stroke="#888" stroke-dasharray="4 4"/>
  <line x1="110" y1="70" x2="336" y2="70" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="225" y="63" text-anchor="middle" font-size="12">poll(fd, POLLIN, -1)</text>
  <line x1="340" y1="100" x2="566" y2="100" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="455" y="93" text-anchor="middle" font-size="12">1차 호출 (_qproc=__pollwait)</text>
  <rect x="470" y="110" width="200" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="128" text-anchor="middle" font-size="12">① poll_wait: rq에 엔트리 등록</text>
  <line x1="566" y1="155" x2="344" y2="155" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="455" y="148" text-anchor="middle" font-size="12">② mask = 0 (아직 없음)</text>
  <rect x="250" y="168" width="180" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="186" text-anchor="middle" font-size="12">schedule() — 여기서 잔다</text>
  <rect x="470" y="208" width="200" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="570" y="226" text-anchor="middle" font-size="12">IRQ: push + wake_up(rq)</text>
  <line x1="566" y1="250" x2="344" y2="250" stroke="#d0564a" marker-end="url(#ah2)"/>
  <text x="455" y="243" text-anchor="middle" font-size="12">엔트리 콜백 pollwake → 깨움</text>
  <line x1="340" y1="280" x2="566" y2="280" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="455" y="273" text-anchor="middle" font-size="12">2차 호출 (_qproc=NULL)</text>
  <line x1="566" y1="305" x2="344" y2="305" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="455" y="298" text-anchor="middle" font-size="12">mask = EPOLLIN | EPOLLRDNORM</text>
  <line x1="336" y1="330" x2="114" y2="330" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="225" y="323" text-anchor="middle" font-size="12">revents = POLLIN</text>
</svg>
```
그림 2 — `.poll`의 두 단계 계약. 드라이버는 ① 등록과 ② 보고만 하고 바로 돌아온다. 잠드는 곳은 VFS의 `schedule()`이다. 깨어난 VFS는 드라이버를 **다시 호출해서** 진짜 상태를 묻는다 — 그래서 `wake_up`은 "뭔가 바뀌었을 수 있다"는 힌트일 뿐이고, 판정은 항상 `.poll`의 리턴값이 한다.

이 그림에서 나오는 규칙 네 가지:

1. **잠들지 말 것.** `.poll` 안에서 `mutex_lock`처럼 잠깐 잘 수 있는 락은 괜찮지만(process context다), I/O 완료를 기다리거나 `msleep`하면 안 된다. 다른 fd 999개의 응답까지 늦어진다.
2. **모든 관련 wait queue를 등록할 것.** 리턴 마스크에 영향을 주는 조건이 바뀔 때 깨워지는 queue는 전부 `poll_wait`에 넘긴다. 하나라도 빠지면 그 조건 변화로는 아무도 깨어나지 않는다.
3. **등록을 먼저, 검사를 나중에.** `poll_wait` 다음에 상태를 읽는다. 순서를 바꾸면 "검사 → (그 사이 데이터 도착·wake_up, 아직 등록 전이라 아무도 안 깸) → 등록 → 잠" 경쟁이 생긴다 (F3 §5의 lost wakeup과 같은 모양).
4. **상태가 바뀌는 모든 곳에서 깨울 것.** 데이터가 들어올 때, 자리가 날 때, 이벤트가 생길 때, 장치가 사라질 때. 깨우기를 빠뜨린 조건은 poll 사용자에게 "영원히 안 오는 이벤트"가 된다.

### 1.4 select · poll · epoll은 모두 같은 함수로 들어온다

유저 API 세 개는 커널 안에서 전부 `vfs_poll()` 한 줄로 모인다.

```c
/* include/linux/poll.h */
static inline __poll_t vfs_poll(struct file *file, struct poll_table_struct *pt)
{
	if (unlikely(!file->f_op->poll))
		return DEFAULT_POLLMASK;
	return file->f_op->poll(file, pt);
}
```

`.poll`이 없는 파일은 `DEFAULT_POLLMASK`(= `EPOLLIN | EPOLLOUT | EPOLLRDNORM | EPOLLWRNORM`, "항상 준비됨")로 취급된다 — 일반 파일을 poll하면 즉시 리턴하는 이유다 (A4 §4.7). 드라이버가 `.poll`을 빼먹으면 데몬의 이벤트 루프가 100% CPU로 도는 증상이 이것이다.

| 유저 API | 커널 경로 | 첫 `.poll` 호출의 `_qproc` | 다시 물을 때 |
|---|---|---|---|
| `select()`/`pselect6` | `do_select()` → `vfs_poll()` | `__pollwait` | 깨어날 때마다 모든 fd를 다시 `.poll` (`_qproc = NULL`) |
| `poll()`/`ppoll` | `do_sys_poll()` → `do_poll()` → `do_pollfd()` → `vfs_poll()` | `__pollwait` | 위와 같음 |
| `epoll_ctl(ADD)` | `do_epoll_ctl()` → `ep_insert()` → `ep_item_poll()` → `vfs_poll()` | `ep_ptable_queue_proc` | 한 번만 등록, 엔트리는 `EPOLL_CTL_DEL`/close까지 남는다 |
| `epoll_wait()` | `do_epoll_wait()` → `ep_send_events()` → `ep_item_poll()` | `NULL` | ready list에 올라온 fd만 다시 `.poll` |

그래서 드라이버 입장에서 **select/poll/epoll을 따로 지원하는 코드는 없다.** `.poll` 하나를 계약대로 짜면 셋이 다 된다. 차이는 "누가 언제 몇 번 부르느냐"뿐이고, 그 차이가 A4 §9의 확장성 차이(select/poll은 매번 N개 전부, epoll은 준비된 것만)로 나타난다. 이 경로는 §11에서 ftrace로 직접 확인한다.

### 1.5 `poll_requested_events()` — 무엇을 물어보는지 알 수 있다

poll table에는 요청된 이벤트 집합(`_key`)도 실려 온다. `poll_requested_events(pt)`로 꺼낼 수 있다. 예를 들어 `POLLOUT`만 묻는데 읽기 쪽 상태 계산이 비싸다면(하드웨어 레지스터를 읽어야 한다든지) 생략할 수 있다. 다만 등록(`poll_wait`)은 생략하지 말자 — 같은 epoll 엔트리가 나중에 `EPOLL_CTL_MOD`로 관심 이벤트를 바꿀 수 있다. 실무 드라이버 대부분은 이 최적화를 쓰지 않는다. 상태 검사는 보통 변수 몇 개 읽기라서 싸다.

---

## 2. 실습 드라이버 `g6_sensorkit`

### 2.1 설계 — 가상의 IMU 하나에 통지 경로 네 개

`g6_sensorkit`은 G3(platform driver)·G7(misc device) 뼈대 위에 이 노트의 주제만 얹은 가상 IMU 드라이버다. 실제 하드웨어 대신 두 가지 "가짜 인터럽트"로 샘플을 만든다.

- **hrtimer**: `period_us`에 주기를 쓰면 그 주기로 샘플 하나씩 넣는다. hrtimer 콜백은 hardirq 문맥에서 돌기 때문에 진짜 FIFO watermark IRQ와 같은 조건이다 (F2, F6).
- **sysfs `inject`**: `echo "N ax" > inject`로 샘플 N개를 즉시 넣는다. 테스트를 결정적으로 만들기 위한 것이다. `|ax|`가 `motion_thresh`(기본 1000)를 넘으면 "모션 이벤트"가 된다.

장치가 유저에게 보여 주는 상태는 네 가지이고, 각각 다른 poll 비트에 대응한다 (§3).

| 상태 | 저장소 | poll 비트 | 깨우는 queue |
|---|---|---|---|
| 읽을 샘플이 있다 | `rx` kfifo (16바이트 × 64) | `EPOLLIN`, `EPOLLRDNORM` | `rq` |
| 명령을 쓸 자리가 있다 | `cmd` kfifo (32바이트) | `EPOLLOUT`, `EPOLLWRNORM` | `wq` |
| 모션 이벤트·overflow가 있었다 | `events` 비트 | `EPOLLPRI` | `eq` |
| 장치가 제거됐다 | `dead` | `EPOLLHUP`, `EPOLLERR` | 전부 |

모듈 파라미터 두 개로 "틀린 드라이버"를 일부러 만들 수 있다. `wmode=0`이면 FIFO가 비어 있다가 찰 때만 깨운다(§4의 ET 버그). `qmode=0`이면 queue 하나에 key 없이 깨운다, `qmode=1`이면 queue 하나에 key를 붙여 깨운다, `qmode=2`(기본)면 queue 세 개를 쓴다(§5).

공용 헤더(드라이버와 테스트 프로그램이 같이 쓴다):

```c
/* g6_uapi.h - shared by the g6_sensorkit driver and its test programs */
#ifndef G6_UAPI_H
#define G6_UAPI_H
#include <linux/types.h>
#include <linux/ioctl.h>

struct g6_sample {		/* 16 bytes, one IMU FIFO entry */
	__u64 ts_ns;		/* CLOCK_MONOTONIC when the driver queued it */
	__s16 ax, ay, az;	/* mg */
	__u16 seq;
};

#define G6_EV_MOTION	0x1	/* |ax| exceeded motion_thresh */
#define G6_EV_OVERFLOW	0x2	/* rx FIFO full, oldest sample dropped */

#define G6_IOC_GET_EVENTS	_IOR('g', 1, __u32)	/* read and clear G6_EV_* */
#define G6_IOC_SET_EVENTFD	_IOW('g', 2, __s32)	/* eventfd to signal, -1 = none */

#endif
```

### 2.2 `g6_sensorkit.c` (1/10) — 상태 구조체

무엇을 보나: 장치 하나가 가진 것 전부. wait queue가 세 개(`rq`, `wq`, `eq`)이고, 구조체 수명을 `kref`로 관리한다 — 장치가 제거돼도 열린 파일이 남아 있으면 wait queue가 살아 있어야 하기 때문이다(§10).

```c
// SPDX-License-Identifier: GPL-2.0
/* g6_sensorkit: fictional IMU - .poll / fasync / eventfd / sysfs_notify lab */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/kfifo.h>
#include <linux/hrtimer.h>
#include <linux/eventfd.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include "g6_uapi.h"

static int qmode = 2;	/* 0: one queue, no key  1: one queue, keyed  2: rq/wq/eq, keyed */
module_param(qmode, int, 0444);
static int wmode = 1;	/* 0: wake only on empty->non-empty (ET bug)  1: every sample */
module_param(wmode, int, 0644);

struct g6_dev {
	struct kref ref;		/* probe + one per open file */
	struct miscdevice misc;
	spinlock_t lock;		/* rx, cmd, events, dead, efd */
	DECLARE_KFIFO(rx, struct g6_sample, 64);
	DECLARE_KFIFO(cmd, u8, 32);
	u32 events;			/* pending G6_EV_* -> EPOLLPRI */
	bool dead;			/* removed -> EPOLLHUP | EPOLLERR */
	wait_queue_head_t rq, wq, eq;	/* readable / writable / exceptional */
	struct fasync_struct *fasync;	/* SIGIO subscribers */
	struct eventfd_ctx *efd;	/* optional eventfd to signal */
	struct hrtimer timer;		/* fake "FIFO watermark IRQ" */
	ktime_t period;
	u16 seq;
	int thresh;
	unsigned int motion_count;
	atomic_t poll_calls;
	struct kernfs_node *ev_kn;	/* sysfs motion_events, for notify */
};
```

### 2.3 (2/10) — 깨우기 도우미와 생산자 `g6_push`

무엇을 보나: 샘플 하나가 들어올 때 드라이버가 하는 일 전부. 락 안에서 상태를 바꾸고, 락 밖에서 네 갈래 통지(wake_up, kill_fasync, eventfd, sysfs)를 보낸다. `wmode`가 0이면 "비어 있다가 찬 순간"에만 알린다.

```c
static wait_queue_head_t *g6_q(struct g6_dev *d, wait_queue_head_t *q)
{
	return qmode == 2 ? q : &d->rq;	/* qmode 0/1: everybody on rq */
}

static void g6_wake(struct g6_dev *d, wait_queue_head_t *q, __poll_t key)
{
	if (qmode == 0)
		wake_up_interruptible(&d->rq);		/* key = NULL */
	else
		wake_up_interruptible_poll(g6_q(d, q), key);
}

/* producer: hrtimer callback (hardirq) or sysfs "inject" (process ctx) */
static void g6_push(struct g6_dev *d, s16 ax)
{
	struct g6_sample s = { .ax = ax, .az = 1000 };
	bool was_empty, ev = false, motion = false;
	unsigned long fl;

	spin_lock_irqsave(&d->lock, fl);
	if (d->dead) {
		spin_unlock_irqrestore(&d->lock, fl);
		return;
	}
	s.seq = d->seq++;
	s.ts_ns = ktime_get_ns();
	was_empty = kfifo_is_empty(&d->rx);
	if (kfifo_is_full(&d->rx)) {
		kfifo_skip(&d->rx);			/* drop oldest */
		d->events |= G6_EV_OVERFLOW;
		ev = true;
	}
	kfifo_put(&d->rx, s);
	if (abs(ax) > d->thresh) {
		d->events |= G6_EV_MOTION;
		d->motion_count++;
		ev = motion = true;
	}
	if (d->efd && (wmode || was_empty))
		eventfd_signal(d->efd);			/* 6.8+: no count arg */
	spin_unlock_irqrestore(&d->lock, fl);

	if (wmode || was_empty) {
		g6_wake(d, &d->rq, EPOLLIN | EPOLLRDNORM);
		kill_fasync(&d->fasync, SIGIO, POLL_IN);
	}
	if (ev) {
		g6_wake(d, &d->eq, EPOLLPRI);
		kill_fasync(&d->fasync, SIGIO, POLL_PRI);
	}
	if (motion && d->ev_kn)
		sysfs_notify_dirent(d->ev_kn);		/* kernfs_notify: any context */
}
```

`g6_push`는 hardirq(hrtimer)에서도 불리므로 `spin_lock_irqsave`를 쓴다. 여기서 부르는 통지 함수 네 개는 모두 **잠들지 않는다** — `wake_up_*`는 wait queue의 spinlock만 잡고, `kill_fasync`는 RCU + rwlock, `eventfd_signal`은 eventfd의 spinlock, `sysfs_notify_dirent`(= `kernfs_notify`)는 "Maybe be called from any context"라고 소스 주석에 적혀 있다. 반대로 `sysfs_notify()`는 이름으로 속성을 찾느라 `kernfs_rwsem`을 잡을 수 있어 **IRQ에서 부르면 안 된다** — 그래서 probe에서 `sysfs_get_dirent()`로 노드를 미리 찾아 둔다(§9).

### 2.4 (3/10) — 가짜 IRQ와 `.poll`

```c
static enum hrtimer_restart g6_tick(struct hrtimer *t)
{
	struct g6_dev *d = container_of(t, struct g6_dev, timer);

	g6_push(d, 10);
	hrtimer_forward_now(t, d->period);
	return HRTIMER_RESTART;
}

static __poll_t g6_poll(struct file *f, poll_table *pt)
{
	struct g6_dev *d = f->private_data;
	__poll_t mask = 0;
	unsigned long fl;

	atomic_inc(&d->poll_calls);
	poll_wait(f, &d->rq, pt);		/* phase 1: register, never sleep */
	if (qmode == 2) {
		poll_wait(f, &d->wq, pt);
		poll_wait(f, &d->eq, pt);
	}
	spin_lock_irqsave(&d->lock, fl);	/* phase 2: report state now */
	if (d->dead) {
		mask = EPOLLHUP | EPOLLERR;
	} else {
		if (!kfifo_is_empty(&d->rx))
			mask |= EPOLLIN | EPOLLRDNORM;
		if (!kfifo_is_full(&d->cmd))
			mask |= EPOLLOUT | EPOLLWRNORM;
		if (d->events)
			mask |= EPOLLPRI;
	}
	spin_unlock_irqrestore(&d->lock, fl);
	return mask;
}
```

§1.3의 계약 그대로다. ① 관련 queue를 전부 `poll_wait`로 등록하고, ② 락 아래서 상태를 마스크로 만든다. `qmode=2`일 때 queue 세 개를 다 등록하는 것에 주목하자 — 한 번의 `poll()` 호출이 queue 세 개에 엔트리를 하나씩 건다. `poll_calls` 카운터는 §5에서 "헛깨움" 횟수를 세는 용도다.

### 2.5 (4/10) — blocking `read`와 `write`

```c
static ssize_t g6_read(struct file *f, char __user *buf, size_t n, loff_t *pos)
{
	struct g6_dev *d = f->private_data;
	struct g6_sample s;
	size_t done = 0;

	if (n < sizeof(s))
		return -EINVAL;
	for (;;) {
		spin_lock_irq(&d->lock);
		if (d->dead) {
			spin_unlock_irq(&d->lock);
			return -ENODEV;
		}
		while (done + sizeof(s) <= n && kfifo_get(&d->rx, &s)) {
			spin_unlock_irq(&d->lock);
			if (copy_to_user(buf + done, &s, sizeof(s)))
				return done ? done : -EFAULT;
			done += sizeof(s);
			spin_lock_irq(&d->lock);
		}
		spin_unlock_irq(&d->lock);
		if (done)
			return done;
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(d->rq, !kfifo_is_empty(&d->rx) ||
					     READ_ONCE(d->dead)))
			return -ERESTARTSYS;
	}
}

static ssize_t g6_write(struct file *f, const char __user *buf, size_t n,
			loff_t *pos)
{
	struct g6_dev *d = f->private_data;
	u8 tmp[32];
	unsigned int put;

	n = min(n, sizeof(tmp));
	if (copy_from_user(tmp, buf, n))
		return -EFAULT;
	for (;;) {
		spin_lock_irq(&d->lock);
		put = d->dead ? 0 : kfifo_in(&d->cmd, tmp, n);
		spin_unlock_irq(&d->lock);
		if (READ_ONCE(d->dead))
			return -ENODEV;
		if (put)
			return put;
		if (f->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(*g6_q(d, &d->wq),
				!kfifo_is_full(&d->cmd) || READ_ONCE(d->dead)))
			return -ERESTARTSYS;
	}
}
```

F3에서 짠 정석 패턴(락 아래 검사 → 비었으면 `O_NONBLOCK`이면 `-EAGAIN`, 아니면 `wait_event_interruptible` → 깨면 다시 검사)에 `dead` 검사만 더했다. `wait_event`의 조건에 `dead`가 들어 있어야 장치 제거 때 잠든 `read()`가 빠져나온다. 같은 `rq`에 `read()` 대기자와 `poll()` 대기자가 함께 매달린다는 점이 중요하다 — 드라이버는 둘을 구별하지 않고 한 번 깨운다.

### 2.6 (5/10) — `.fasync`와 ioctl (이벤트 확인, eventfd 등록)

```c
static int g6_fasync(int fd, struct file *f, int on)
{
	struct g6_dev *d = f->private_data;

	return fasync_helper(fd, f, on, &d->fasync);	/* add/remove entry */
}

static long g6_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct g6_dev *d = f->private_data;
	struct eventfd_ctx *new = NULL, *old;
	int efd;
	u32 ev;

	switch (cmd) {
	case G6_IOC_GET_EVENTS:
		spin_lock_irq(&d->lock);
		ev = d->events;
		d->events = 0;			/* ack: EPOLLPRI goes away */
		spin_unlock_irq(&d->lock);
		return put_user(ev, (u32 __user *)arg);
	case G6_IOC_SET_EVENTFD:
		if (get_user(efd, (int __user *)arg))
			return -EFAULT;
		if (efd >= 0) {
			new = eventfd_ctx_fdget(efd);	/* takes a ctx reference */
			if (IS_ERR(new))
				return PTR_ERR(new);
		}
		spin_lock_irq(&d->lock);
		old = d->efd;
		d->efd = new;
		spin_unlock_irq(&d->lock);
		if (old)
			eventfd_ctx_put(old);
		return 0;
	}
	return -ENOTTY;
}
```

`EPOLLPRI`는 "읽기"로 사라지는 상태가 아니다. 유저가 이벤트를 **확인(ack)** 해야 꺼진다 — 여기서는 `G6_IOC_GET_EVENTS`가 읽고 지운다. 확인 방법이 없으면 `EPOLLPRI`가 영원히 켜져 있어서 level-triggered 사용자는 무한 루프에 빠진다. `G6_IOC_SET_EVENTFD`는 §8에서 쓴다. `eventfd_ctx_fdget()`이 eventfd 컨텍스트에 참조를 잡기 때문에 유저가 eventfd fd를 닫아도 드라이버 쪽 포인터는 안전하다.

### 2.7 (6/10) — open/release와 `file_operations`

```c
static void g6_free(struct kref *k)
{
	struct g6_dev *d = container_of(k, struct g6_dev, ref);

	if (d->efd)
		eventfd_ctx_put(d->efd);
	pr_info("g6: struct freed (last reference)\n");
	kfree(d);
}

static int g6_open(struct inode *ino, struct file *f)
{
	struct g6_dev *d = container_of(f->private_data, struct g6_dev, misc);

	kref_get(&d->ref);		/* wait queues must outlive this file */
	f->private_data = d;
	return stream_open(ino, f);
}

static int g6_release(struct inode *ino, struct file *f)
{
	struct g6_dev *d = f->private_data;

	kref_put(&d->ref, g6_free);	/* fasync entry already gone (__fput) */
	return 0;
}

static const struct file_operations g6_fops = {
	.owner		= THIS_MODULE,
	.open		= g6_open,
	.release	= g6_release,
	.read		= g6_read,
	.write		= g6_write,
	.poll		= g6_poll,
	.fasync		= g6_fasync,
	.unlocked_ioctl	= g6_ioctl,
};
```

`release`에서 `g6_fasync(-1, f, 0)`를 부르지 않는다. 옛 책(LDD3)의 예제는 직접 불렀지만, 지금 커널은 `__fput()`이 `FASYNC` 플래그가 켜진 파일에 대해 `f_op->fasync(-1, file, 0)`을 대신 불러 준다 (`fs/file_table.c`). 직접 불러도 해롭지는 않다.

### 2.8 (7/10, 8/10) — sysfs 속성 (테스트 손잡이)

```c
static ssize_t inject_store(struct device *dev, struct device_attribute *a,
			    const char *buf, size_t len)
{
	struct g6_dev *d = dev_get_drvdata(dev);
	int n, ax = 10;

	if (sscanf(buf, "%d %d", &n, &ax) < 1 || n < 0 || n > 10000)
		return -EINVAL;
	while (n--)
		g6_push(d, ax);
	return len;
}
static DEVICE_ATTR_WO(inject);

static ssize_t cmd_drain_store(struct device *dev, struct device_attribute *a,
			       const char *buf, size_t len)
{
	struct g6_dev *d = dev_get_drvdata(dev);
	unsigned int n;

	if (kstrtouint(buf, 0, &n))
		return -EINVAL;
	spin_lock_irq(&d->lock);
	kfifo_skip_count(&d->cmd, min(n, kfifo_len(&d->cmd)));	/* IMU ate n */
	spin_unlock_irq(&d->lock);
	g6_wake(d, &d->wq, EPOLLOUT | EPOLLWRNORM);
	kill_fasync(&d->fasync, SIGIO, POLL_OUT);
	return len;
}
static DEVICE_ATTR_WO(cmd_drain);

static ssize_t motion_events_show(struct device *dev,
				  struct device_attribute *a, char *buf)
{
	struct g6_dev *d = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", READ_ONCE(d->motion_count));
}
static DEVICE_ATTR_RO(motion_events);

static ssize_t poll_calls_show(struct device *dev, struct device_attribute *a,
			       char *buf)
{
	struct g6_dev *d = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", atomic_xchg(&d->poll_calls, 0));
}
static DEVICE_ATTR_RO(poll_calls);
```

```c
static ssize_t period_us_store(struct device *dev, struct device_attribute *a,
			       const char *buf, size_t len)
{
	struct g6_dev *d = dev_get_drvdata(dev);
	unsigned int us;

	if (kstrtouint(buf, 0, &us))
		return -EINVAL;
	if (READ_ONCE(d->dead))
		return -ENODEV;
	hrtimer_cancel(&d->timer);
	if (us) {
		d->period = us_to_ktime(us);
		hrtimer_start(&d->timer, d->period, HRTIMER_MODE_REL);
	}
	return len;
}
static DEVICE_ATTR_WO(period_us);

static ssize_t motion_thresh_store(struct device *dev,
				   struct device_attribute *a,
				   const char *buf, size_t len)
{
	struct g6_dev *d = dev_get_drvdata(dev);

	return kstrtoint(buf, 0, &d->thresh) ? -EINVAL : len;
}
static DEVICE_ATTR_WO(motion_thresh);

static struct attribute *g6_attrs[] = {
	&dev_attr_inject.attr, &dev_attr_cmd_drain.attr,
	&dev_attr_motion_events.attr, &dev_attr_poll_calls.attr,
	&dev_attr_period_us.attr, &dev_attr_motion_thresh.attr, NULL,
};
static const struct attribute_group g6_group = { .attrs = g6_attrs };

static void g6_put_action(void *data)	/* devm: runs after the sysfs group is gone */
{
	struct g6_dev *d = data;

	sysfs_put(d->ev_kn);
	kref_put(&d->ref, g6_free);
}
```

`g6_put_action`은 `devm_add_action_or_reset()`으로 **sysfs 그룹보다 먼저** 등록한다. devres는 등록 역순으로 풀리므로 unbind 때 "sysfs 그룹 제거 → (그 뒤) 구조체 참조 해제" 순서가 된다. 순서가 반대면 `inject`에 쓰는 도중 구조체가 해제되는 use-after-free가 가능하다.

### 2.9 (9/10) — probe

```c
static int g6_probe(struct platform_device *pdev)
{
	struct g6_dev *d = kzalloc(sizeof(*d), GFP_KERNEL);
	int ret;

	if (!d)
		return -ENOMEM;
	kref_init(&d->ref);
	spin_lock_init(&d->lock);
	INIT_KFIFO(d->rx);
	INIT_KFIFO(d->cmd);
	init_waitqueue_head(&d->rq);
	init_waitqueue_head(&d->wq);
	init_waitqueue_head(&d->eq);
	d->thresh = 1000;
	hrtimer_setup(&d->timer, g6_tick, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	platform_set_drvdata(pdev, d);
	ret = devm_add_action_or_reset(&pdev->dev, g6_put_action, d);
	if (ret)
		return ret;
	ret = devm_device_add_group(&pdev->dev, &g6_group);
	if (ret)
		return ret;
	d->ev_kn = sysfs_get_dirent(pdev->dev.kobj.sd, "motion_events");
	d->misc.minor = MISC_DYNAMIC_MINOR;
	d->misc.name = "g6_sensorkit";
	d->misc.fops = &g6_fops;
	d->misc.mode = 0666;
	d->misc.parent = &pdev->dev;
	ret = misc_register(&d->misc);
	if (ret)
		return ret;
	dev_info(&pdev->dev, "/dev/g6_sensorkit ready (qmode=%d wmode=%d)\n",
		 qmode, wmode);
	return 0;
}
```

### 2.10 (10/10) — remove("뽑힘")와 모듈 등록

```c
static void g6_remove(struct platform_device *pdev)	/* unbind = "unplug" */
{
	struct g6_dev *d = platform_get_drvdata(pdev);

	hrtimer_cancel(&d->timer);
	misc_deregister(&d->misc);		/* no new open() from here */
	spin_lock_irq(&d->lock);
	d->dead = true;
	spin_unlock_irq(&d->lock);
	/* nr_exclusive = 0: wake EVERY waiter, EPOLLEXCLUSIVE ones too */
	__wake_up(&d->rq, TASK_INTERRUPTIBLE, 0, poll_to_key(EPOLLHUP | EPOLLERR));
	__wake_up(&d->wq, TASK_INTERRUPTIBLE, 0, poll_to_key(EPOLLHUP | EPOLLERR));
	__wake_up(&d->eq, TASK_INTERRUPTIBLE, 0, poll_to_key(EPOLLHUP | EPOLLERR));
	kill_fasync(&d->fasync, SIGIO, POLL_HUP);
	dev_info(&pdev->dev, "removed, open files keep the struct alive\n");
}

static struct platform_driver g6_driver = {
	.probe	= g6_probe,
	.remove	= g6_remove,
	.driver	= { .name = "g6_sensorkit" },
};

static struct platform_device *g6_pdev;

static int __init g6_init(void)
{
	int ret = platform_driver_register(&g6_driver);

	if (ret)
		return ret;
	g6_pdev = platform_device_register_simple("g6_sensorkit", -1, NULL, 0);
	if (IS_ERR(g6_pdev)) {
		platform_driver_unregister(&g6_driver);
		return PTR_ERR(g6_pdev);
	}
	return 0;
}

static void __exit g6_exit(void)
{
	platform_device_unregister(g6_pdev);
	platform_driver_unregister(&g6_driver);
}
module_init(g6_init);
module_exit(g6_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("G6 lab: poll/epoll/fasync/eventfd for a fake IMU");
```

`remove`는 §10에서 자세히 본다. 요점은 **`dead`를 세우고, 모든 queue의 모든 대기자를 `EPOLLHUP | EPOLLERR` key로 깨우고, SIGIO 구독자에게 `POLL_HUP`을 보내고, 구조체는 마지막 파일이 닫힐 때까지 남겨 두는 것**이다. `wake_up_interruptible_poll()` 매크로는 `nr_exclusive = 1`이라 `EPOLLEXCLUSIVE` 대기자를 하나만 깨우므로, 여기서는 `__wake_up(…, 0, key)`로 전원을 깨운다 (§6).

### 2.11 예제 1 — 빌드와 게스트 부팅

무엇을 확인하나: 디버그 커널 빌드 트리로 경고 없이 빌드되는지, 파라미터 두 개가 보이는지.

```sh
# VM 안, /var/tmp/G6/mod 에서 (Kbuild: obj-m := g6_sensorkit.o)
make -C /var/lab/out/linux-build-dbg M=$PWD -j2 modules
modinfo -p g6_sensorkit.ko; modinfo g6_sensorkit.ko | grep vermagic
```

```text
make: Entering directory '/var/lab/build/linux-dbg'
make[1]: Entering directory '/var/tmp/G6/mod'
  CC [M]  g6_sensorkit.o
  MODPOST Module.symvers
  CC [M]  g6_sensorkit.mod.o
  CC [M]  .module-common.o
  LD [M]  g6_sensorkit.ko
make[1]: Leaving directory '/var/tmp/G6/mod'
make: Leaving directory '/var/lab/build/linux-dbg'
qmode: (int)
wmode: (int)
vermagic:       6.18.55-dbg SMP preempt mod_unload aarch64
```

출력에서 볼 것: 경고 0개, `vermagic`이 `6.18.55-dbg`라서 `out/Image-dbg`에서만 로드된다.

테스트 프로그램은 VM에서 정적 빌드하고(`gcc -std=gnu11 -Wall -Wextra -O2 -static -pthread g6_xxx.c`, 전부 경고 0개), 랩 rootfs를 `/var/tmp/G6/rootfs`로 복사해 `.ko`·바이너리·게스트 스크립트 `g6_run.sh`를 넣고 `cpio`로 다시 묶는다. 매 예제는 같은 방식으로 부팅한다 (`g6_run.sh`의 해당 절을 `LAB_CMD`로 실행하고 끝나면 전원이 꺼진다):

```sh
timeout 180 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 512M -nographic \
  -kernel /var/lab/out/Image-dbg -initrd /var/tmp/G6/g6_initrd.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/g6_run.sh~basic"
```

테스트 프로그램 공용 헤더 (`now_ns()`는 드라이버의 `ktime_get_ns()`와 같은 `CLOCK_MONOTONIC`이라 §12에서 둘을 바로 뺄 수 있다):

```c
/* g6_common.h - helpers for the g6_sensorkit test programs */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include "../mod/g6_uapi.h"

#define DEV "/dev/g6_sensorkit"
#define SYS "/sys/devices/platform/g6_sensorkit/"
#define DIE(m) do { perror(m); exit(1); } while (0)
#define msleep(ms) usleep((ms) * 1000)

static inline uint64_t now_ns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);	/* same clock as ktime_get_ns() */
	return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}

static inline void sysw(const char *attr, const char *val)
{
	char p[128];
	int fd = (snprintf(p, sizeof(p), SYS "%s", attr), open(p, O_WRONLY));

	if (fd < 0 || write(fd, val, strlen(val)) < 0)
		DIE(p);
	close(fd);
}

static inline long sysr(const char *attr)
{
	char p[128], b[32] = "";
	int fd = (snprintf(p, sizeof(p), SYS "%s", attr), open(p, O_RDONLY));

	if (fd < 0 || read(fd, b, sizeof(b) - 1) < 0)
		DIE(p);
	close(fd);
	return atol(b);
}

static inline int drain(int fd)		/* read until EAGAIN, return #samples */
{
	struct g6_sample s[8];
	ssize_t r;
	int n = 0;

	while ((r = read(fd, s, sizeof(s))) > 0)
		n += r / sizeof(s[0]);
	if (r < 0 && errno != EAGAIN)
		DIE("read");
	return n;
}
```

---

## 3. 이벤트 마스크 설계 — 장치 상태를 비트로 번역하기

### 3.1 비트 사전

`.poll`이 돌려주는 비트는 "이 fd에 지금 무엇을 해도 막히지 않는가"를 말한다. 드라이버가 자주 쓰는 것만 추리면 이렇다 (값은 `include/uapi/linux/eventpoll.h`, arm64 기준).

| 비트 | 값 | 뜻 (드라이버 입장) | 센서 키트 예 |
|---|---|---|---|
| `EPOLLIN` | 0x001 | `read()`가 지금 막히지 않는다 | IMU FIFO에 샘플 있음 |
| `EPOLLRDNORM` | 0x040 | "일반 데이터" 읽기 가능 — 문자 장치는 `EPOLLIN`과 항상 같이 켠다 | 위와 같음 |
| `EPOLLPRI` | 0x002 | 예외·긴급 조건. 일반 데이터와 별도로 처리해야 하는 일 | 모션 임계값 초과, FIFO overflow, 센서 fault |
| `EPOLLOUT` | 0x004 | `write()`가 지금 막히지 않는다 | 명령 FIFO에 자리 있음 |
| `EPOLLWRNORM` | 0x100 | 일반 데이터 쓰기 가능 — `EPOLLOUT`과 같이 켠다 | 위와 같음 |
| `EPOLLERR` | 0x008 | 에러 조건. 유저가 요청하지 않아도 항상 보고된다 | 장치 제거, 복구 불가 하드웨어 에러 |
| `EPOLLHUP` | 0x010 | 상대가 끊겼다(hang up). 요청 없이도 항상 보고된다 | 장치 제거, USB 센서 뽑힘 |
| `EPOLLRDHUP` | 0x2000 | 스트림 소켓의 상대가 쓰기 쪽을 닫았다 | 문자 장치는 보통 안 쓴다 (TCP용) |
| `EPOLLNVAL` | 0x020 | fd가 열려 있지 않다 — VFS가 붙인다, 드라이버는 안 쓴다 | — |

`RDNORM`/`WRNORM`은 System V 시절 "일반 대역 vs 우선 대역(`RDBAND`/`WRBAND`)" 구분의 흔적이다. Linux의 문자 장치 드라이버는 관례적으로 `EPOLLIN | EPOLLRDNORM`, `EPOLLOUT | EPOLLWRNORM`을 짝으로 켠다. 유저가 `poll()`에 `POLLRDNORM`만 요청해도 동작하게 하려는 것이다.

### 3.2 select는 비트를 세 묶음으로 접는다

select는 비트 대신 fd 집합 세 개(`readfds`, `writefds`, `exceptfds`)를 쓴다. 커널은 `.poll`의 마스크를 다음 매크로로 접는다.

```c
/* fs/select.c (6.18.55) */
#define POLLIN_SET (EPOLLRDNORM | EPOLLRDBAND | EPOLLIN | EPOLLHUP | EPOLLERR |\
			EPOLLNVAL)
#define POLLOUT_SET (EPOLLWRBAND | EPOLLWRNORM | EPOLLOUT | EPOLLERR |\
			 EPOLLNVAL)
#define POLLEX_SET (EPOLLPRI | EPOLLNVAL)
```

그래서 `EPOLLPRI`는 select의 **`exceptfds`** 로 나오고, `EPOLLHUP`/`EPOLLERR`는 **읽기 집합에도 쓰기 집합에도** 켜진다. "select의 exceptfds는 소켓 OOB 데이터용"이라고만 알고 있으면 드라이버의 긴급 이벤트를 놓친다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="110" y="24" text-anchor="middle" font-size="13" font-weight="bold">드라이버 상태</text>
  <text x="350" y="24" text-anchor="middle" font-size="13" font-weight="bold">g6_poll이 켜는 비트</text>
  <text x="580" y="24" text-anchor="middle" font-size="13" font-weight="bold">select에서</text>
  <rect x="10" y="40" width="200" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="67" text-anchor="middle" font-size="12">rx FIFO에 샘플 있음</text>
  <rect x="250" y="40" width="200" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="350" y="67" text-anchor="middle" font-size="12">EPOLLIN | EPOLLRDNORM</text>
  <rect x="490" y="40" width="180" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="580" y="67" text-anchor="middle" font-size="12">readfds</text>
  <rect x="10" y="100" width="200" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="110" y="127" text-anchor="middle" font-size="12">cmd FIFO에 자리 있음</text>
  <rect x="250" y="100" width="200" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="350" y="127" text-anchor="middle" font-size="12">EPOLLOUT | EPOLLWRNORM</text>
  <rect x="490" y="100" width="180" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="580" y="127" text-anchor="middle" font-size="12">writefds</text>
  <rect x="10" y="160" width="200" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="110" y="187" text-anchor="middle" font-size="12">모션 이벤트 / overflow</text>
  <rect x="250" y="160" width="200" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="350" y="187" text-anchor="middle" font-size="12">EPOLLPRI</text>
  <rect x="490" y="160" width="180" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="580" y="187" text-anchor="middle" font-size="12">exceptfds</text>
  <rect x="10" y="220" width="200" height="60" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="110" y="246" text-anchor="middle" font-size="12">장치 제거됨 (dead)</text>
  <text x="110" y="266" text-anchor="middle" font-size="12">다른 비트는 전부 끈다</text>
  <rect x="250" y="220" width="200" height="60" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="350" y="246" text-anchor="middle" font-size="12">EPOLLHUP | EPOLLERR</text>
  <text x="350" y="266" text-anchor="middle" font-size="12">요청 안 해도 보고됨</text>
  <rect x="490" y="220" width="180" height="60" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="580" y="246" text-anchor="middle" font-size="12">readfds + writefds</text>
  <text x="580" y="266" text-anchor="middle" font-size="12">둘 다 켜진다</text>
  <line x1="210" y1="62" x2="246" y2="62" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="450" y1="62" x2="486" y2="62" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="210" y1="122" x2="246" y2="122" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="450" y1="122" x2="486" y2="122" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="210" y1="182" x2="246" y2="182" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="450" y1="182" x2="486" y2="182" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="210" y1="250" x2="246" y2="250" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="450" y1="250" x2="486" y2="250" stroke="currentColor" marker-end="url(#ah3)"/>
</svg>
```
그림 3 — `g6_sensorkit`의 이벤트 마스크 지도. 왼쪽 상태 하나가 가운데 비트 하나(또는 짝)로 번역되고, select에서는 오른쪽 집합으로 접힌다. 이 표를 드라이버 설계 문서에 먼저 써 두면 `.poll` 구현이 기계적으로 나온다.

### 3.3 예제 2 — 같은 상태를 select · poll · epoll로 보기

무엇을 확인하나: 장치를 다섯 가지 상태로 만들면서, 같은 순간 select(r/w/x 집합), poll(`revents`), epoll(`events`)이 무엇을 보고하는지 나란히 찍는다. 모든 대기는 timeout 0이라 "지금 상태"만 묻는다.

```c
/* g6_mux: the same driver state seen through select, poll and epoll */
#include "g6_common.h"
#include <poll.h>
#include <sys/select.h>
#include <sys/epoll.h>

static void show(int fd, const char *label)
{
	fd_set r, w, x;
	struct timeval tv = { 0, 0 };
	struct pollfd p = { .fd = fd, .events = POLLIN | POLLOUT | POLLPRI };
	struct epoll_event ev = { .events = EPOLLIN | EPOLLOUT | EPOLLPRI }, out;
	int ep = epoll_create1(0), n;

	FD_ZERO(&r); FD_ZERO(&w); FD_ZERO(&x);
	FD_SET(fd, &r); FD_SET(fd, &w); FD_SET(fd, &x);
	if (select(fd + 1, &r, &w, &x, &tv) < 0 || poll(&p, 1, 0) < 0)
		DIE("select/poll");
	if (ep < 0 || epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) < 0)
		DIE("epoll");
	n = epoll_wait(ep, &out, 1, 0);
	printf("%-24s select r%d w%d x%d   poll 0x%03x   epoll 0x%03x\n", label,
	       FD_ISSET(fd, &r), FD_ISSET(fd, &w), FD_ISSET(fd, &x),
	       p.revents, n > 0 ? out.events : 0);
	close(ep);
}

int main(void)
{
	int fd = open(DEV, O_RDWR | O_NONBLOCK), n = 0;
	uint32_t ev;

	if (fd < 0)
		DIE(DEV);
	show(fd, "empty");
	sysw("inject", "3");
	show(fd, "3 samples queued");
	printf("  drained %d samples\n", drain(fd));
	sysw("inject", "1 3000");			/* |ax| > 1000 -> motion */
	show(fd, "motion event pending");
	if (ioctl(fd, G6_IOC_GET_EVENTS, &ev) < 0)
		DIE("ioctl");
	printf("  GET_EVENTS -> 0x%x, drained %d\n", ev, drain(fd));
	show(fd, "event acked, drained");
	while (write(fd, "c", 1) == 1)
		n++;
	printf("  wrote %d cmd bytes, then errno=%s\n", n, strerrorname_np(errno));
	show(fd, "cmd FIFO full");
	sysw("cmd_drain", "8");
	show(fd, "IMU consumed 8 bytes");
	return 0;
}
```

```text
[    4.050094] g6_sensorkit g6_sensorkit: /dev/g6_sensorkit ready (qmode=2 wmode=1)
crw-rw-rw-    1 0        0          10, 258 Oct 11 01:59 /dev/g6_sensorkit
cmd_drain
driver
driver_override
inject
misc
modalias
motion_events
motion_thresh
period_us
poll_calls
power
subsystem
uevent
lrwxrwxrwx    1 0        0                0 Oct 11 01:59 /sys/devices/platform/g6_sensorkit/driver -> ../../../bus/platform/drivers/g6_sensorkit
empty                    select r0 w1 x0   poll 0x004   epoll 0x004
3 samples queued         select r1 w1 x0   poll 0x005   epoll 0x005
  drained 3 samples
motion event pending     select r1 w1 x1   poll 0x007   epoll 0x007
  GET_EVENTS -> 0x1, drained 1
event acked, drained     select r0 w1 x0   poll 0x004   epoll 0x004
  wrote 32 cmd bytes, then errno=EAGAIN
cmd FIFO full            select r0 w0 x0   poll 0x000   epoll 0x000
IMU consumed 8 bytes     select r0 w1 x0   poll 0x004   epoll 0x004
```

출력에서 볼 것:

- `empty`: `0x004` = `EPOLLOUT`만. 명령 FIFO에 자리가 있으니 쓸 수 있다. `EPOLLWRNORM`(0x100)이 안 보이는 이유는 poll/epoll 모두 **요청한 비트만** 돌려주기 때문이다 (`do_pollfd()`가 `mask & filter`, epoll은 `epi->event.events`로 거른다). 요청에 `POLLWRNORM`을 넣으면 보인다.
- `3 samples queued`: `0x005` = IN + OUT. select의 `r1 w1`.
- `motion event pending`: `0x007` = IN + PRI + OUT. select는 `x1`(exceptfds)로 보여 준다. 모션 샘플 자체도 FIFO에 들어가므로 IN도 켜져 있다.
- `GET_EVENTS -> 0x1` 이후 PRI가 꺼졌다 — ack가 PRI를 끄는 유일한 방법이다.
- `cmd FIFO full`: 32바이트를 쓰고 33번째에서 `EAGAIN`. 이때 마스크가 `0x000` — OUT이 꺼졌다. `cmd_drain`으로 8바이트를 소비하면 다시 `0x004`.

### 3.4 흔한 마스크 실수

- **`EPOLLIN`만 켜고 `EPOLLRDNORM`을 안 켠다** — 유저가 `POLLRDNORM`으로 기다리면 영원히 안 깬다. 반대로 `EPOLLRDNORM`만 켜면 대부분의 유저 코드(`POLLIN`)가 못 받는다.
- **쓰기를 지원하지 않는데 `EPOLLOUT`을 켠다**(또는 반대로 쓰기를 지원하는데 끄지 않는다) — "쓸 수 있다"고 해 놓고 `write()`가 막히면 이벤트 루프 전체가 멈춘다.
- **`EPOLLPRI`를 끌 방법이 없다** — LT 사용자는 같은 이벤트로 무한히 깨어난다. 이벤트는 ack(ioctl, 읽기, sysfs 쓰기 중 하나)로 꺼지게 설계한다.
- **제거된 장치에서 `EPOLLIN`을 계속 켠다** — 유저는 `read()`를 시도하고 `-ENODEV`를 받고 다시 poll한다. `EPOLLHUP | EPOLLERR`을 돌려주면 유저 라이브러리 대부분이 fd를 닫는다.

---

## 4. edge-triggered epoll이 드라이버에 요구하는 것

### 4.1 문제 — "비어 있다가 찰 때만 깨우면 되지 않나?"

드라이버를 처음 짜면 이렇게 최적화하고 싶어진다. "FIFO가 이미 비어 있지 않으면, 기다리는 사람은 이미 깨어 있을 테니 또 깨울 필요 없다." poll/select에서는 맞는 말이다. 깨어난 쪽이 `.poll`을 다시 불러 상태를 보기 때문이다. LT epoll에서도 맞다. LT는 이벤트를 돌려준 epitem을 ready list에 **다시 넣어 두므로** 다음 `epoll_wait`이 `.poll`로 다시 확인한다 (A4 §10.3).

ET epoll에서는 틀리다. ET는 epitem을 ready list에 다시 넣지 않는다. **다음 `ep_poll_callback`, 즉 드라이버의 다음 `wake_up`이 와야만** 다시 올라간다. 그러니 드라이버가 "비어 있지 않은 FIFO에 샘플이 추가될 때" 깨우지 않으면, 덜 읽은 ET 사용자는 **새 데이터가 계속 들어오는데도 영원히 깨지 않는다.**

Linux 커널의 pipe가 실제로 이 문제를 겪었다. 5.x 시절 pipe 코드를 "비었을 때만 깨우기"로 최적화했다가, ET epoll을 쓰는 실제 프로그램들이 멈추는 회귀가 보고돼 되돌렸다. 지금 `pipe_write()`의 주석이 그 흔적이다.

```c
/* fs/pipe.c (6.18.55), pipe_write() 끝부분 */
	/*
	 * Epoll nonsensically wants a wakeup whether the pipe
	 * was already empty or not.
	 */
	if (was_empty || pipe->poll_usage)
		wake_up_interruptible_sync_poll(&pipe->rd_wait, EPOLLIN | EPOLLRDNORM);
	kill_fasync(&pipe->fasync_readers, SIGIO, POLL_IN);
```

`poll_usage`는 누군가 이 pipe를 한 번이라도 poll했으면 켜지는 플래그다. 즉 "poll 사용자가 있으면 무조건 매번 깨운다". 커널 개발자는 "nonsensically"라고 투덜대지만 결론은 분명하다. **드라이버의 규칙: 새 데이터가 들어올 때마다 깨운다. 상태 전이(빈→찬)에서만 깨우지 않는다.** 그리고 `kill_fasync`도 매번 보낸다(이것도 같은 시기에 고쳐졌다).

### 4.2 예제 3 — ET 멈춤 버그 재현과 수정

무엇을 확인하나: 매 라운드 샘플 2개를 넣고(가짜 IRQ), `epoll_wait`(100 ms timeout) 후 샘플을 읽는다. 드라이버 `wmode`(0 = 빈→찬 때만 wake, 1 = 매번), epoll 모드(LT/ET), 읽는 방식(한 개만 / `EAGAIN`까지 전부)을 바꿔 가며 다섯 조합을 돌린다.

```c
/* g6_et: edge-triggered epoll vs a driver that wakes only on empty->non-empty */
#include "g6_common.h"
#include <sys/epoll.h>

int main(int argc, char **argv)
{
	int et = argc > 1 && atoi(argv[1]);	/* 1: EPOLLET */
	int full = argc > 2 && atoi(argv[2]);	/* 1: drain until EAGAIN */
	int fd = open(DEV, O_RDONLY | O_NONBLOCK), ep = epoll_create1(0);
	struct epoll_event ev = { .events = EPOLLIN | (et ? EPOLLET : 0) }, out;
	struct g6_sample s;
	int queued = 0, got, i, n;
	FILE *f = fopen("/sys/module/g6_sensorkit/parameters/wmode", "r");

	if (fd < 0 || ep < 0 || !f)
		DIE("setup");
	printf("wmode=%c %s, reader=%s\n", fgetc(f), et ? "EPOLLET" : "LT",
	       full ? "drain until EAGAIN" : "one sample per wakeup");
	drain(fd);				/* start from an empty FIFO */
	if (epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) < 0)
		DIE("epoll_ctl");
	for (i = 0; i < 6; i++) {
		sysw("inject", "2");			/* IRQ: 2 new samples */
		queued += 2;
		n = epoll_wait(ep, &out, 1, 100);
		got = 0;
		if (n > 0)
			got = full ? drain(fd) : (read(fd, &s, sizeof(s)) > 0);
		queued -= got;
		printf("  round %d: epoll_wait=%d read=%d left in FIFO=%d\n",
		       i, n, got, queued);
	}
	return 0;
}
```

게스트 스크립트에서 다섯 조합을 이 순서로 돌렸다:

```sh
insmod /g6_sensorkit.ko wmode=0
/g6_et 0 0; /g6_et 1 0
echo 1 > /sys/module/g6_sensorkit/parameters/wmode
/g6_et 1 0; /g6_et 1 1
echo 0 > /sys/module/g6_sensorkit/parameters/wmode
/g6_et 1 1
```

```text
[    3.573427] g6_sensorkit g6_sensorkit: /dev/g6_sensorkit ready (qmode=2 wmode=0)
wmode=0 LT, reader=one sample per wakeup
  round 0: epoll_wait=1 read=1 left in FIFO=1
  round 1: epoll_wait=1 read=1 left in FIFO=2
  round 2: epoll_wait=1 read=1 left in FIFO=3
  round 3: epoll_wait=1 read=1 left in FIFO=4
  round 4: epoll_wait=1 read=1 left in FIFO=5
  round 5: epoll_wait=1 read=1 left in FIFO=6
wmode=0 EPOLLET, reader=one sample per wakeup
  round 0: epoll_wait=1 read=1 left in FIFO=1
  round 1: epoll_wait=0 read=0 left in FIFO=3
  round 2: epoll_wait=0 read=0 left in FIFO=5
  round 3: epoll_wait=0 read=0 left in FIFO=7
  round 4: epoll_wait=0 read=0 left in FIFO=9
  round 5: epoll_wait=0 read=0 left in FIFO=11
wmode=1 EPOLLET, reader=one sample per wakeup
  round 0: epoll_wait=1 read=1 left in FIFO=1
  round 1: epoll_wait=1 read=1 left in FIFO=2
  round 2: epoll_wait=1 read=1 left in FIFO=3
  round 3: epoll_wait=1 read=1 left in FIFO=4
  round 4: epoll_wait=1 read=1 left in FIFO=5
  round 5: epoll_wait=1 read=1 left in FIFO=6
wmode=1 EPOLLET, reader=drain until EAGAIN
  round 0: epoll_wait=1 read=2 left in FIFO=0
  round 1: epoll_wait=1 read=2 left in FIFO=0
  round 2: epoll_wait=1 read=2 left in FIFO=0
  round 3: epoll_wait=1 read=2 left in FIFO=0
  round 4: epoll_wait=1 read=2 left in FIFO=0
  round 5: epoll_wait=1 read=2 left in FIFO=0
wmode=0 EPOLLET, reader=drain until EAGAIN
  round 0: epoll_wait=1 read=2 left in FIFO=0
  round 1: epoll_wait=1 read=2 left in FIFO=0
  round 2: epoll_wait=1 read=2 left in FIFO=0
  round 3: epoll_wait=1 read=2 left in FIFO=0
  round 4: epoll_wait=1 read=2 left in FIFO=0
  round 5: epoll_wait=1 read=2 left in FIFO=0
```

출력에서 볼 것:

1. **LT + 틀린 드라이버(wmode=0)**: 매 라운드 깨어난다. 드라이버가 안 깨워도 LT가 epitem을 ready list에 남겨 두기 때문이다. 다만 한 개씩만 읽어서 backlog가 1씩 쌓인다 — 이건 유저 버그다.
2. **ET + 틀린 드라이버 + 한 개씩 읽기**: 라운드 0에서 한 번 깨고 **그 뒤로 영원히 timeout**(`epoll_wait=0`). FIFO에는 샘플이 3, 5, 7 … 11개 쌓여 가는데 아무도 모른다. 이것이 "센서 데몬이 가끔 멈춘다"의 정체다. 실제 장비에서는 FIFO가 가득 차 overflow가 날 때까지 조용하다.
3. **ET + 고친 드라이버(wmode=1) + 한 개씩 읽기**: 매번 깨지만 backlog가 쌓인다(A4 §4.4의 유저 쪽 ET 버그). 드라이버는 할 일을 다 했다.
4. **ET + 고친 드라이버 + 전부 읽기**: 정답. 매 라운드 2개를 읽고 FIFO가 비워진다.
5. **ET + 틀린 드라이버 + 전부 읽기**: 이것도 동작한다. 유저가 매번 `EAGAIN`까지 비우면 다음 push는 항상 "빈→찬" 전이가 되기 때문이다. 하지만 드라이버가 이런 유저 습관에 기대면 안 된다 — 사용자가 하나라도 덜 읽으면 2번이 된다.

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
  <text x="10" y="70" font-size="13" font-weight="bold">wmode=0</text>
  <text x="10" y="90" font-size="12">빈→찬 때만 wake</text>
  <text x="10" y="200" font-size="13" font-weight="bold">wmode=1</text>
  <text x="10" y="220" font-size="12">샘플마다 wake</text>
  <line x1="120" y1="130" x2="670" y2="130" stroke="currentColor"/>
  <line x1="120" y1="250" x2="670" y2="250" stroke="currentColor"/>
  <circle cx="150" cy="38" r="6" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <rect x="140" y="124" width="20" height="6" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="150" y="119" text-anchor="middle" font-size="12">1</text>
  <circle cx="235" cy="38" r="6" fill="none" stroke="#d0564a" stroke-dasharray="2 2"/>
  <rect x="225" y="112" width="20" height="18" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="235" y="107" text-anchor="middle" font-size="12">3</text>
  <circle cx="320" cy="38" r="6" fill="none" stroke="#d0564a" stroke-dasharray="2 2"/>
  <rect x="310" y="100" width="20" height="30" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="320" y="95" text-anchor="middle" font-size="12">5</text>
  <circle cx="405" cy="38" r="6" fill="none" stroke="#d0564a" stroke-dasharray="2 2"/>
  <rect x="395" y="88" width="20" height="42" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="405" y="83" text-anchor="middle" font-size="12">7</text>
  <circle cx="490" cy="38" r="6" fill="none" stroke="#d0564a" stroke-dasharray="2 2"/>
  <rect x="480" y="76" width="20" height="54" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="490" y="71" text-anchor="middle" font-size="12">9</text>
  <circle cx="575" cy="38" r="6" fill="none" stroke="#d0564a" stroke-dasharray="2 2"/>
  <rect x="565" y="64" width="20" height="66" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="575" y="59" text-anchor="middle" font-size="12">11</text>
  <text x="640" y="42" text-anchor="middle" font-size="12">멈춤</text>
  <circle cx="150" cy="168" r="6" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <rect x="140" y="244" width="20" height="6" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="150" y="239" text-anchor="middle" font-size="12">1</text>
  <circle cx="235" cy="168" r="6" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <rect x="225" y="238" width="20" height="12" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="235" y="233" text-anchor="middle" font-size="12">2</text>
  <circle cx="320" cy="168" r="6" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <rect x="310" y="232" width="20" height="18" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="320" y="227" text-anchor="middle" font-size="12">3</text>
  <circle cx="405" cy="168" r="6" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <rect x="395" y="226" width="20" height="24" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="405" y="221" text-anchor="middle" font-size="12">4</text>
  <circle cx="490" cy="168" r="6" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <rect x="480" y="220" width="20" height="30" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="490" y="215" text-anchor="middle" font-size="12">5</text>
  <circle cx="575" cy="168" r="6" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <rect x="565" y="214" width="20" height="36" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="575" y="209" text-anchor="middle" font-size="12">6</text>
  <text x="640" y="172" text-anchor="middle" font-size="12">지연</text>
  <text x="360" y="274" text-anchor="middle" font-size="12">라운드 0~5 (매 라운드 샘플 2개 inject, 1개 read) · 초록 원 = 깨어남 · 막대 = FIFO에 남은 샘플</text>
</svg>
```
그림 4 — 예제 3의 2번(위)과 3번(아래) 조합을 그린 것. 둘 다 유저는 ET에서 한 개씩만 읽는다(유저 버그). 드라이버가 전이 때만 깨우면(위) 사용자는 라운드 1부터 다시는 안 깨고 FIFO만 차오른다. 드라이버가 매번 깨우면(아래) 최소한 깨어나기는 하므로, 유저 버그는 "지연"으로 드러나 고칠 수 있다.

### 4.3 정리 — ET 사용자를 위한 드라이버 체크리스트

- 새 데이터가 들어올 때마다 `wake_up_*_poll(&rq, EPOLLIN | EPOLLRDNORM)`을 부른다. 빈→찬 전이 최적화는 하지 않는다. `wake_up`은 대기자가 없으면 거의 공짜다(`waitqueue_active` 수준의 검사와 spinlock 한 번).
- 쓰기 쪽도 같다. 자리가 생길 때마다 `EPOLLOUT`으로 깨운다.
- 이벤트(`EPOLLPRI`)도 새 이벤트가 생길 때마다 깨운다. 이미 pending인 비트를 다시 세우는 경우라도.
- `kill_fasync`도 매번 보낸다.
- 유저 문서(또는 README)에 "ET로 쓰면 `EAGAIN`까지 읽을 것"을 적는다. 드라이버가 매번 깨워도 덜 읽은 데이터는 다음 샘플까지 지연된다.

---

## 5. wait queue 여러 개와 wake key — "관심 없는 사람은 깨우지 않기"

### 5.1 헛깨움(spurious wakeup)이란

F3의 `f3_queue`는 wait queue 두 개(`rq`, `wq`)를 썼다. 장치 상태가 세 종류(읽기, 쓰기, 이벤트)라면 queue를 몇 개 둬야 할까? 극단적으로는 하나만 둬도 동작은 한다. 무엇이 바뀌든 그 queue를 깨우면, 깨어난 쪽이 `.poll`을 다시 불러 확인하니까. 대가는 **헛깨움**이다. "모션 이벤트만 기다리는" 스레드가 샘플이 200 Hz로 들어올 때마다 깨어나 `.poll`을 부르고, 아무것도 없는 걸 확인하고 다시 잔다. 센서 키트의 저전력 모드에서 이건 그대로 전력 낭비다.

해결은 두 겹이다.

1. **wake key**: `wake_up_interruptible_poll(&q, EPOLLIN)`처럼 "무엇이 바뀌었나"를 key로 실어 보낸다. 대기 엔트리의 콜백이 key를 보고 관심 없으면 무시한다.
2. **queue 분리**: 아예 상태 종류별로 queue를 나눠서, 관심 없는 대기자는 그 queue에 없게 한다.

key를 거르는 코드는 엔트리 콜백 쪽에 있다. poll/select의 엔트리:

```c
/* fs/select.c (6.18.55) */
static int pollwake(wait_queue_entry_t *wait, unsigned mode, int sync, void *key)
{
	struct poll_table_entry *entry;

	entry = container_of(wait, struct poll_table_entry, wait);
	if (key && !(key_to_poll(key) & entry->key))
		return 0;
	return __pollwake(wait, mode, sync, key);
}
```

`entry->key`는 `__pollwait`이 등록할 때 저장한 "이 fd에 요청한 비트 + `EPOLLERR | EPOLLHUP`"이다. epoll의 엔트리도 똑같이 거른다 (`ep_poll_callback`의 `if (pollflags && !(pollflags & epi->event.events)) goto out_unlock;`). 둘 다 **key가 `NULL`이면 거르지 않는다** — 그래서 key 없는 `wake_up_interruptible()`은 "모두 깨워서 다시 물어보게" 하는 것과 같다.

### 5.2 예제 4 — 헛깨움 횟수 세기

무엇을 확인하나: 한 스레드는 `POLLPRI`(모션 이벤트)만 기다리고, 메인 스레드는 데이터 샘플 100개를 2 ms 간격으로 넣은 뒤 모션 이벤트 1개를 넣는다. 드라이버의 `.poll` 호출 횟수(`poll_calls`, 읽으면 0으로 리셋)를 `qmode` 0/1/2에서 poll과 epoll로 잰다.

```c
/* g6_keyed: a poller that wants only POLLPRI - how often is it woken for data? */
#include "g6_common.h"
#include <poll.h>
#include <pthread.h>
#include <sys/epoll.h>

static int fd, use_epoll;

static void *waiter(void *arg)
{
	struct pollfd p = { .fd = fd, .events = POLLPRI };
	struct epoll_event ev = { .events = EPOLLPRI }, out;
	int ep, n;

	(void)arg;
	if (!use_epoll) {
		n = poll(&p, 1, 5000);
		printf("  poll   -> %d revents=0x%x\n", n, p.revents);
		return NULL;
	}
	ep = epoll_create1(0);
	if (ep < 0 || epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) < 0)
		DIE("epoll");
	n = epoll_wait(ep, &out, 1, 5000);
	printf("  epoll  -> %d events=0x%x\n", n, n > 0 ? out.events : 0);
	return NULL;
}

int main(int argc, char **argv)
{
	FILE *f = fopen("/sys/module/g6_sensorkit/parameters/qmode", "r");
	pthread_t t;
	uint32_t ev;
	int i;

	use_epoll = argc > 1 && atoi(argv[1]);
	fd = open(DEV, O_RDONLY | O_NONBLOCK);
	if (fd < 0 || !f || ioctl(fd, G6_IOC_GET_EVENTS, &ev) < 0)
		DIE("open");				/* ack old events */
	sysr("poll_calls");				/* read = reset counter */
	pthread_create(&t, NULL, waiter, NULL);
	msleep(200);
	for (i = 0; i < 100; i++) {			/* 100 data IRQs ... */
		sysw("inject", "1");
		msleep(2);
		drain(fd);
	}
	sysw("inject", "1 3000");			/* ... then one motion event */
	pthread_join(t, NULL);
	printf("qmode=%c %s: .poll called %ld times for 100 data + 1 event\n",
	       fgetc(f), use_epoll ? "epoll" : "poll ", sysr("poll_calls"));
	return 0;
}
```

```sh
for q in 0 1 2; do insmod /g6_sensorkit.ko qmode=$q; /g6_keyed 0; /g6_keyed 1; rmmod g6_sensorkit; done
```

```text
[    3.561811] g6_sensorkit g6_sensorkit: /dev/g6_sensorkit ready (qmode=0 wmode=1)
  poll   -> 1 revents=0x2
qmode=0 poll : .poll called 102 times for 100 data + 1 event
  epoll  -> 1 events=0x2
qmode=0 epoll: .poll called 102 times for 100 data + 1 event
[    5.048845] g6_sensorkit g6_sensorkit: removed, open files keep the struct alive
[    5.049763] g6: struct freed (last reference)
[    5.194632] g6_sensorkit g6_sensorkit: /dev/g6_sensorkit ready (qmode=1 wmode=1)
  poll   -> 1 revents=0x2
qmode=1 poll : .poll called 2 times for 100 data + 1 event
  epoll  -> 1 events=0x2
qmode=1 epoll: .poll called 2 times for 100 data + 1 event
[    6.724150] g6_sensorkit g6_sensorkit: removed, open files keep the struct alive
[    6.724686] g6: struct freed (last reference)
[    6.871517] g6_sensorkit g6_sensorkit: /dev/g6_sensorkit ready (qmode=2 wmode=1)
  poll   -> 1 revents=0x2
qmode=2 poll : .poll called 2 times for 100 data + 1 event
  epoll  -> 1 events=0x2
qmode=2 epoll: .poll called 2 times for 100 data + 1 event
[    8.588078] g6_sensorkit g6_sensorkit: removed, open files keep the struct alive
[    8.589209] g6: struct freed (last reference)
```

출력에서 볼 것:

- **qmode=0 (queue 하나, key 없음)**: `.poll`이 **102번**. 등록 때 1번, 데이터 100번마다 헛깨움 1번씩, 마지막 이벤트 1번. poll도 epoll도 같다 — key가 `NULL`이라 `pollwake`도 `ep_poll_callback`도 거르지 못한다.
- **qmode=1 (queue 하나, key 있음)**: **2번**(등록 + 진짜 이벤트). 같은 queue에 매달려 있어도 콜백이 key(`EPOLLIN`)와 관심(`EPOLLPRI`)을 비교해 그냥 돌아간다. 깨우는 쪽 비용은 남는다 — `__wake_up_common`이 엔트리를 하나씩 훑으며 콜백을 부르기는 한다.
- **qmode=2 (queue 셋, key 있음)**: 역시 2번. 이벤트만 기다리는 대기자의 엔트리가 `rq`에도 걸려 있지만(§2.4 — `.poll`이 queue 셋을 다 등록) key로 걸러지고, blocking `read()`/`write()` 대기자는 애초에 자기 queue에만 있다.

교훈: **`wake_up_interruptible()` 대신 `wake_up_interruptible_poll(&q, 이벤트)`를 쓰는 것만으로** 헛깨움 대부분이 사라진다. queue 분리는 blocking read/write 대기자까지 고려할 때 추가로 이득이 있다. 실무 드라이버는 보통 "읽기 queue 하나 + 쓰기 queue 하나"에 key를 붙이는 정도로 충분하다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="13">IRQ → g6_push → wake_up_interruptible_poll(&amp;rq, EPOLLIN | EPOLLRDNORM)</text>
  <rect x="10" y="40" width="110" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="65" y="62" text-anchor="middle" font-size="13">rq</text>
  <text x="65" y="80" text-anchor="middle" font-size="12">읽기 가능</text>
  <rect x="150" y="40" width="150" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="225" y="62" text-anchor="middle" font-size="12">read() 대기자</text>
  <text x="225" y="80" text-anchor="middle" font-size="12">autoremove_wake</text>
  <rect x="320" y="40" width="150" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="395" y="62" text-anchor="middle" font-size="12">poll() 대기자</text>
  <text x="395" y="80" text-anchor="middle" font-size="12">pollwake + key</text>
  <rect x="490" y="40" width="180" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="580" y="62" text-anchor="middle" font-size="12">epoll A 엔트리</text>
  <text x="580" y="80" text-anchor="middle" font-size="12">ep_poll_callback</text>
  <line x1="120" y1="65" x2="146" y2="65" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="300" y1="65" x2="316" y2="65" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="470" y1="65" x2="486" y2="65" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="10" y="120" width="110" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="65" y="142" text-anchor="middle" font-size="13">wq</text>
  <text x="65" y="160" text-anchor="middle" font-size="12">쓰기 가능</text>
  <rect x="150" y="120" width="150" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="225" y="142" text-anchor="middle" font-size="12">write() 대기자</text>
  <text x="225" y="160" text-anchor="middle" font-size="12">깨우지 않음</text>
  <line x1="120" y1="145" x2="146" y2="145" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="10" y="200" width="110" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="65" y="222" text-anchor="middle" font-size="13">eq</text>
  <text x="65" y="240" text-anchor="middle" font-size="12">이벤트</text>
  <rect x="320" y="200" width="150" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="395" y="222" text-anchor="middle" font-size="12">poll() 대기자</text>
  <text x="395" y="240" text-anchor="middle" font-size="12">같은 poll 호출</text>
  <rect x="490" y="200" width="180" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="580" y="222" text-anchor="middle" font-size="12">epoll A 엔트리</text>
  <text x="580" y="240" text-anchor="middle" font-size="12">같은 epitem</text>
  <line x1="120" y1="225" x2="316" y2="225" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="470" y1="225" x2="486" y2="225" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="440" y="280" width="230" height="64" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="555" y="302" text-anchor="middle" font-size="12">eventpoll A</text>
  <text x="555" y="320" text-anchor="middle" font-size="12">rdllist에 epitem 추가</text>
  <text x="555" y="337" text-anchor="middle" font-size="12">ep->wq의 epoll_wait 깨움</text>
  <line x1="580" y1="90" x2="580" y2="200" stroke="#e08a3c" stroke-dasharray="4 3"/>
  <line x1="555" y1="250" x2="555" y2="276" stroke="#e08a3c" marker-end="url(#ah5)"/>
  <text x="10" y="290" font-size="12">rq만 깨운다. rq의 엔트리는 모두 콜백이 불린다:</text>
  <text x="10" y="310" font-size="12">read 대기자는 깨고, poll/epoll은 key를 자기 관심과</text>
  <text x="10" y="330" font-size="12">비교해 맞으면 깨고 아니면 그냥 돌아간다.</text>
</svg>
```
그림 5 — 드라이버 wait queue와 그 위에 매달린 엔트리들. `poll()` 한 번이나 `epoll_ctl(ADD)` 한 번이 `.poll`의 `poll_wait` 세 번을 통해 queue 세 곳에 엔트리를 하나씩 건다. 드라이버는 **누가 매달려 있는지 모른다** — queue를 key와 함께 깨우기만 하면, 각 엔트리의 콜백(`autoremove_wake_function`, `pollwake`, `ep_poll_callback`)이 알아서 처리한다.

---

## 6. `EPOLLEXCLUSIVE`와 thundering herd

### 6.1 무엇이 문제인가

F3 §6에서 blocking `read()` 대기자의 thundering herd는 `wait_event_interruptible_exclusive()`로 풀었다. 그런데 poll/epoll 대기자는 기본적으로 **비배타적**이라 전원이 깬다. 여러 워커 스레드가 **각자 자기 epoll 인스턴스**에 같은 장치 fd를 등록해 두면(예: 센서 프레임을 워커 풀이 나눠 처리), 샘플 하나에 워커 전원이 깨어나고 한 명만 일한다.

Linux 4.5부터 `EPOLL_CTL_ADD`에 `EPOLLEXCLUSIVE`를 주면 `ep_ptable_queue_proc()`이 엔트리를 `add_wait_queue_exclusive()`로(= `WQ_FLAG_EXCLUSIVE`, 리스트 꼬리) 건다. 드라이버가 `wake_up_*`(`nr_exclusive = 1`)을 부르면 `__wake_up_common()`이 배타적 엔트리 하나를 깨운 뒤 멈춘다.

```c
/* kernel/sched/wait.c (6.18.55) */
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

드라이버가 할 일은 거의 없다. 다만 **key를 실어 보내는 편이 안전하다.** `ep_poll_callback`은 먼저 key와 관심 이벤트를 비교해 안 맞으면 0을 리턴한다(카운트를 안 줄이니 다음 배타적 엔트리로 넘어간다). key가 `NULL`이면 이 거름이 없어서, 쓰기 자리가 났다는 깨우기 한 번이 "읽기만 기다리는" 배타적 대기자 한 명에게 소비되고 정작 쓰기를 기다리던 대기자는 계속 잘 수 있다.

### 6.2 예제 5 — 네 스레드 중 몇이 깨나

무엇을 확인하나: 스레드 4개가 각자 epoll 인스턴스로 같은 fd를 기다리고, 샘플 1개를 넣는다.

```c
/* g6_excl: 4 threads, each with its own epoll on the same fd - who wakes? */
#include "g6_common.h"
#include <pthread.h>
#include <sys/epoll.h>

#define N 4
static int fd, excl;

static void *worker(void *arg)
{
	long id = (long)arg;
	struct epoll_event ev = { .events = EPOLLIN | (excl ? EPOLLEXCLUSIVE : 0) };
	struct epoll_event out;
	int ep = epoll_create1(0), n;

	if (ep < 0 || epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) < 0)
		DIE("epoll");
	n = epoll_wait(ep, &out, 1, 1000);
	printf("  thread %ld: %s (events=0x%x)\n", id,
	       n > 0 ? "WOKE" : "timeout", n > 0 ? out.events : 0);
	return NULL;
}

int main(int argc, char **argv)
{
	pthread_t t[N];
	long i;

	excl = argc > 1 && atoi(argv[1]);
	fd = open(DEV, O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		DIE(DEV);
	drain(fd);					/* start non-readable */
	printf("%s:\n", excl ? "EPOLLEXCLUSIVE" : "plain EPOLLIN");
	for (i = 0; i < N; i++)
		pthread_create(&t[i], NULL, worker, (void *)i);
	msleep(300);					/* all 4 asleep */
	sysw("inject", "1");				/* one sample */
	for (i = 0; i < N; i++)
		pthread_join(t[i], NULL);
	return 0;
}
```

```text
[    3.600722] g6_sensorkit g6_sensorkit: /dev/g6_sensorkit ready (qmode=2 wmode=1)
plain EPOLLIN:
  thread 0: WOKE (events=0x1)
  thread 1: WOKE (events=0x1)
  thread 3: WOKE (events=0x1)
  thread 2: WOKE (events=0x1)
EPOLLEXCLUSIVE:
  thread 0: WOKE (events=0x1)
  thread 2: timeout (events=0x0)
  thread 1: timeout (events=0x0)
  thread 3: timeout (events=0x0)
```

출력에서 볼 것: plain은 4명 전원, `EPOLLEXCLUSIVE`는 1명만 깼다. 나머지 셋은 1초 timeout. 이 실험의 첫 시도에서 나는 `EPOLLEXCLUSIVE`인데도 4명이 다 깨는 결과를 봤다. 원인은 앞 실행(plain)이 FIFO에 남긴 샘플이었다 — `epoll_ctl(ADD)` 순간 `.poll`이 이미 `EPOLLIN`을 돌려주니 4명 모두 대기 없이 바로 리턴한 것이다. 그래서 지금 코드는 시작할 때 `drain()`한다. **exclusive는 "깨우기"를 줄일 뿐, 이미 준비된 상태는 모두에게 보인다.**

### 6.3 드라이버가 조심할 점

- 배타적 대기자가 있으면 "깨어난 한 명"이 일을 다 처리하지 못할 때 나머지는 계속 잔다. 그래서 LT로 쓰는 게 보통이다(남은 데이터가 있으면 다음 `epoll_wait`에서 다시 보고).
- **장치 제거처럼 모두가 알아야 하는 사건은 `nr_exclusive = 0`으로 깨운다.** `g6_remove`가 `__wake_up(&d->rq, TASK_INTERRUPTIBLE, 0, poll_to_key(EPOLLHUP | EPOLLERR))`를 쓰는 이유다. `ep_poll_callback`의 배타적 분기에서 key가 `HUP | ERR`이면 `EPOLLINOUT_BITS`와 겹치는 비트가 없어(`case 0`) "깨웠다"로 계산되므로, `nr_exclusive = 1`이면 한 명만 HUP을 받고 나머지는 계속 잔다.
- `wake_up_all()`/`wake_up_interruptible_all()`도 `nr_exclusive = 0`이지만 key가 `NULL`이다. HUP을 key로 실어 보내려면 `__wake_up()`을 직접 쓴다 (`EXPORT_SYMBOL`된 함수다).

---

## 7. fasync와 SIGIO — "시그널로 알려 줘"

### 7.1 동작

poll 계열은 유저가 "물어보러 오는" 방식이다. fasync는 반대로 커널이 **시그널을 쏴서** 알린다. bare-metal로 치면 "이벤트 루프에서 플래그를 보는 대신, 아무 데서나 끼어드는 소프트웨어 인터럽트"다.

유저 쪽 3단계:

1. `fcntl(fd, F_SETOWN, getpid())` — 시그널을 받을 프로세스(또는 `F_SETOWN_EX`로 스레드)를 정한다.
2. (선택) `fcntl(fd, F_SETSIG, SIGRTMIN)` — 기본 `SIGIO` 대신 다른 시그널을 쓰고, `siginfo`에 fd와 이유를 싣는다.
3. `fcntl(fd, F_SETFL, flags | O_ASYNC)` — 이 순간 커널이 드라이버의 `.fasync(fd, file, 1)`을 부른다.

드라이버 쪽 2개:

- `.fasync`에서 `fasync_helper(fd, file, on, &dev->fasync)` — `fasync_struct` 리스트에 이 파일을 넣거나 뺀다.
- 상태가 바뀔 때 `kill_fasync(&dev->fasync, SIGIO, POLL_IN)` — 리스트의 각 파일 주인에게 `send_sigio()`. 두 번째 인자는 "기본 시그널", 세 번째는 이유(`POLL_IN`, `POLL_OUT`, `POLL_PRI`, `POLL_HUP`, `POLL_ERR`, `POLL_MSG`)다.

`send_sigio_to_task()`의 핵심 부분:

```c
/* fs/fcntl.c (6.18.55), 요약 없이 발췌 */
	switch (signum) {
		default: {
			kernel_siginfo_t si;
			...
			si.si_signo = signum;
			si.si_errno = 0;
		        si.si_code  = reason;
			...
				si.si_band = mangle_poll(band_table[reason - POLL_IN]);
			si.si_fd    = fd;
			if (!do_send_sig_info(signum, &si, p, type))
				break;
		}
			fallthrough;	/* fall back on the old plain SIGIO signal */
		case 0:
			do_send_sig_info(SIGIO, SEND_SIG_PRIV, p, type);
	}
```

(가운데 몇 줄은 `...`로 생략했다.) `F_SETSIG`을 안 했으면(`signum == 0`) **정보 없는 plain SIGIO**(`si_code = SI_KERNEL`)가 간다. `F_SETSIG`을 했으면 `si_code = POLL_IN` 같은 이유, `si_fd`, `si_band`(= 그 이유에 해당하는 poll 비트)가 실린다. 그리고 실시간 시그널 큐가 넘치면(`RLIMIT_SIGPENDING`) **plain SIGIO로 떨어진다**(fallthrough). 이때 유저는 "뭔가 놓쳤다"고 보고 모든 fd를 다시 훑어야 한다.

### 7.2 예제 6 — 시그널은 합쳐진다

무엇을 확인하나: 샘플 50개를 연속으로 넣고 모션 이벤트 1개를 넣는 동안(드라이버는 `kill_fasync`를 51 + 1번 부른다) 메인 루프가 시그널을 막고 바쁜 척한다. 그 뒤 풀었을 때 핸들러가 몇 번 도는지, plain SIGIO와 `F_SETSIG SIGRTMIN`으로 비교한다.

```c
/* g6_sigio: fasync - SIGIO (coalesced) vs F_SETSIG real-time signal (queued) */
#include "g6_common.h"
#include <signal.h>

static volatile sig_atomic_t hits, in_cnt, pri_cnt, hup_cnt;

static void on_sig(int sig, siginfo_t *si, void *uc)
{
	(void)sig; (void)uc;
	hits++;
	if (si->si_code == POLL_IN)
		in_cnt++;
	else if (si->si_code == POLL_PRI)
		pri_cnt++;
	else if (si->si_code == POLL_HUP)
		hup_cnt++;
}

int main(int argc, char **argv)
{
	int rt = argc > 1 && atoi(argv[1]);
	int sig = rt ? SIGRTMIN : SIGIO;
	struct sigaction sa = { .sa_sigaction = on_sig, .sa_flags = SA_SIGINFO };
	sigset_t blk, old;
	int fd = open(DEV, O_RDONLY | O_NONBLOCK);

	if (fd < 0)
		DIE(DEV);
	sigemptyset(&sa.sa_mask);
	if (sigaction(sig, &sa, NULL) < 0)
		DIE("sigaction");
	if (fcntl(fd, F_SETOWN, getpid()) < 0)		/* who gets the signal */
		DIE("F_SETOWN");
	if (rt && fcntl(fd, F_SETSIG, sig) < 0)		/* RT signal + siginfo */
		DIE("F_SETSIG");
	if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_ASYNC) < 0)  /* -> .fasync */
		DIE("O_ASYNC");
	sigemptyset(&blk);
	sigaddset(&blk, sig);
	sigprocmask(SIG_BLOCK, &blk, &old);		/* a busy main loop ... */
	sysw("inject", "50");				/* 50 samples in a burst */
	sysw("inject", "1 3000");			/* + 1 motion event */
	sigprocmask(SIG_SETMASK, &old, NULL);		/* ... unblocks later */
	printf("%s: 51 kill_fasync(POLL_IN) + 1 (POLL_PRI) -> handler ran %d times"
	       " (POLL_IN %d, POLL_PRI %d)\n", rt ? "F_SETSIG SIGRTMIN" : "plain SIGIO",
	       hits, in_cnt, pri_cnt);
	printf("  samples actually in FIFO: %d\n", drain(fd));
	return 0;
}
```

```text
[    3.583658] g6_sensorkit g6_sensorkit: /dev/g6_sensorkit ready (qmode=2 wmode=1)
plain SIGIO: 51 kill_fasync(POLL_IN) + 1 (POLL_PRI) -> handler ran 1 times (POLL_IN 0, POLL_PRI 0)
  samples actually in FIFO: 51
F_SETSIG SIGRTMIN: 51 kill_fasync(POLL_IN) + 1 (POLL_PRI) -> handler ran 52 times (POLL_IN 51, POLL_PRI 1)
  samples actually in FIFO: 51
```

출력에서 볼 것:

- **plain SIGIO: 핸들러 1번, `si_code`는 `POLL_IN`도 `POLL_PRI`도 아니다.** 표준 시그널은 pending 비트 하나라서 52번이 1번으로 합쳐졌고, 이유 정보도 없다(`SI_KERNEL`). 유저는 "어떤 fd에, 무슨 일이, 몇 번"을 하나도 모른 채 전부 다시 확인해야 한다.
- **`F_SETSIG SIGRTMIN`: 52번, `POLL_IN` 51 + `POLL_PRI` 1.** 실시간 시그널은 큐에 쌓이므로 하나도 안 빠졌다. 대신 샘플 하나마다 시그널 하나 — 200 Hz IMU면 초당 200번 핸들러가 끼어들고, 큐 한도를 넘으면 위의 fallthrough로 plain SIGIO가 된다.
- 어느 쪽이든 FIFO에는 51개가 그대로 있다. 시그널은 "알림"일 뿐 데이터 전달 수단이 아니다.

### 7.3 왜 시그널은 나쁜 API인가

| 문제 | 설명 |
|---|---|
| 합쳐짐 | 표준 시그널은 횟수를 잃는다(예제 6). 실시간 시그널은 큐 한도가 있다. |
| 정보 부족 | plain SIGIO는 어떤 fd인지 모른다. fd가 여럿이면 전부 다시 poll해야 한다. |
| 아무 데서나 끼어듦 | 핸들러 안에서는 async-signal-safe 함수만 쓸 수 있다 (`printf`, `malloc`, mutex 금지). 결국 플래그만 세우고 메인 루프가 처리 — 그럴 거면 처음부터 poll이 낫다. |
| `EINTR` | 시그널이 올 때마다 blocking syscall이 `EINTR`로 깨질 수 있다(`SA_RESTART`가 없으면). 라이브러리 코드 전체가 영향을 받는다 (A6). |
| 프로세스 단위 | `F_SETOWN`은 프로세스 단위다. 멀티스레드에서는 어느 스레드가 받을지 정하려면 `F_SETOWN_EX`와 시그널 마스크를 맞춰야 한다. |
| 라이브러리 충돌 | 시그널 번호·핸들러는 프로세스 전역 자원이다. 두 라이브러리가 SIGIO를 쓰면 싸운다. |

그래서 새 유저 코드는 fasync 대신 epoll(또는 아래 eventfd, signalfd)을 쓴다. 그런데도 **드라이버는 `.fasync`를 구현해 두는 것이 관례**다. 오래된 유저 프로그램과 tty/input 같은 기존 서브시스템이 쓰고, 구현 비용이 함수 하나와 `kill_fasync` 몇 줄뿐이기 때문이다. 그리고 §4와 같은 이유로 **상태 전이 때만이 아니라 매번** `kill_fasync`를 부른다.

---

## 8. 커널이 울리는 eventfd — "이 fd로 알려 줘"

### 8.1 아이디어

eventfd는 A4 §5에서 본 "64비트 카운터 + wait queue를 가진 fd"다. 보통은 유저 스레드끼리 쓰지만, **커널도 울릴 수 있다.** 유저가 eventfd를 하나 만들어 ioctl로 드라이버에 넘기면, 드라이버는 이벤트마다 `eventfd_signal(ctx)`로 카운터를 올린다. 유저는 그 eventfd를 자기 epoll 루프에 넣고 기다린다.

시그널의 문제가 전부 사라진다. 끼어들지 않고(fd라서 이벤트 루프에서 처리), 횟수를 잃지 않고(카운터 누적), 장치 fd와 **분리된 fd**라서 다른 프로세스에 넘길 수도 있다 (`SCM_RIGHTS` 또는 fork).

이 패턴의 대표가 **VFIO와 KVM**이다. VFIO는 유저스페이스 드라이버(DPDK, QEMU의 장치 패스스루)에게 장치 인터럽트를 eventfd로 전달한다(`VFIO_DEVICE_SET_IRQS`에 eventfd를 넘긴다). KVM의 `irqfd`는 반대 방향으로, eventfd를 울리면 게스트에 인터럽트를 주입한다. 둘 다 커널 안에서 `eventfd_ctx_fdget()` → `eventfd_signal()`을 쓴다.

```c
/* include/linux/eventfd.h (6.18.55) */
struct eventfd_ctx *eventfd_ctx_fdget(int fd);
void eventfd_ctx_put(struct eventfd_ctx *ctx);
void eventfd_signal_mask(struct eventfd_ctx *ctx, __poll_t mask);

static inline void eventfd_signal(struct eventfd_ctx *ctx)
{
	eventfd_signal_mask(ctx, 0);
}
```

**버전 주의**: 6.7까지는 `eventfd_signal(ctx, n)`(더할 값 n, 리턴값 있음)이었고, 6.8부터 인자가 `ctx` 하나로 바뀌어 항상 1을 더한다. 옛 드라이버 코드나 블로그의 `eventfd_signal(ctx, 1)`은 6.8 이상에서 컴파일되지 않는다. 구현은 짧다.

```c
/* fs/eventfd.c (6.18.55) */
void eventfd_signal_mask(struct eventfd_ctx *ctx, __poll_t mask)
{
	unsigned long flags;

	/*
	 * Deadlock or stack overflow issues can happen if we recurse here
	 * through waitqueue wakeup handlers. If the caller users potentially
	 * nested waitqueues with custom wakeup handlers, then it should
	 * check eventfd_signal_allowed() before calling this function. If
	 * it returns false, the eventfd_signal() call should be deferred to a
	 * safe context.
	 */
	if (WARN_ON_ONCE(current->in_eventfd))
		return;

	spin_lock_irqsave(&ctx->wqh.lock, flags);
	current->in_eventfd = 1;
	if (ctx->count < ULLONG_MAX)
		ctx->count++;
	if (waitqueue_active(&ctx->wqh))
		wake_up_locked_poll(&ctx->wqh, EPOLLIN | mask);
	current->in_eventfd = 0;
	spin_unlock_irqrestore(&ctx->wqh.lock, flags);
}
EXPORT_SYMBOL_GPL(eventfd_signal_mask);
```

볼 것: spinlock + `irqsave`라 hardirq에서 불러도 된다. `EXPORT_SYMBOL_GPL`이라 GPL 모듈만 쓸 수 있다. 그리고 재귀 방지 — eventfd를 깨운 콜백이 다시 eventfd를 울리면(`in_eventfd`) 경고하고 무시한다. eventfd의 wait queue에 매달린 것도 결국 `pollwake`/`ep_poll_callback` 엔트리라서, 유저 쪽에서 보면 "eventfd도 그냥 `.poll`을 가진 파일"이다.

### 8.2 예제 7 — eventfd 경로

무엇을 확인하나: eventfd를 드라이버에 등록하고 **장치 fd가 아니라 eventfd만** epoll로 기다린다. 샘플 1개, 5개 묶음, 1개를 넣었을 때 카운터 값을 본다. 그다음 유저가 eventfd를 먼저 닫아도 드라이버가 안전한지, 잘못된 fd를 넘기면 어떻게 되는지 본다.

```c
/* g6_efd: driver signals an eventfd the user handed it (VFIO/KVM style) */
#include "g6_common.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>

int main(void)
{
	int fd = open(DEV, O_RDONLY | O_NONBLOCK);
	int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	int ep = epoll_create1(0), n, i;
	struct epoll_event ev = { .events = EPOLLIN }, out;
	uint64_t cnt;

	if (fd < 0 || efd < 0 || ep < 0)
		DIE("setup");
	if (ioctl(fd, G6_IOC_SET_EVENTFD, &efd) < 0)	/* eventfd_ctx_fdget() */
		DIE("SET_EVENTFD");
	ev.data.fd = efd;
	if (epoll_ctl(ep, EPOLL_CTL_ADD, efd, &ev) < 0)	/* watch efd, NOT the dev */
		DIE("epoll_ctl");
	for (i = 0; i < 3; i++) {
		sysw("inject", i == 1 ? "5" : "1");	/* 1, then a burst of 5, then 1 */
		n = epoll_wait(ep, &out, 1, 500);
		if (n < 1 || read(efd, &cnt, sizeof(cnt)) != sizeof(cnt))
			DIE("eventfd");
		printf("  round %d: eventfd counter=%llu, samples read from dev=%d\n",
		       i, (unsigned long long)cnt, drain(fd));
	}
	close(efd);				/* driver still holds its ctx ref */
	sysw("inject", "1");			/* eventfd_signal on a closed fd: OK */
	printf("  after close(efd): inject OK, dev has %d sample\n", drain(fd));
	n = -1;
	if (ioctl(fd, G6_IOC_SET_EVENTFD, &n) < 0)	/* eventfd_ctx_put() */
		DIE("unset");
	n = 99;
	printf("  SET_EVENTFD(99) -> %d (%s)\n", ioctl(fd, G6_IOC_SET_EVENTFD, &n),
	       strerrorname_np(errno));
	return 0;
}
```

```text
[    3.472619] g6_sensorkit g6_sensorkit: /dev/g6_sensorkit ready (qmode=2 wmode=1)
  round 0: eventfd counter=1, samples read from dev=1
  round 1: eventfd counter=5, samples read from dev=5
  round 2: eventfd counter=1, samples read from dev=1
  after close(efd): inject OK, dev has 1 sample
  SET_EVENTFD(99) -> -1 (EBADF)
```

출력에서 볼 것:

- 묶음 5개는 **카운터 5**로 왔다. 깨어남은 한 번이지만 횟수는 보존된다 — 시그널과 다른 점이다. `read(efd)`가 카운터를 0으로 되돌린다(`EFD_SEMAPHORE`가 아닐 때).
- `close(efd)` 뒤에도 `inject`가 문제없다. 드라이버가 `eventfd_ctx_fdget()`으로 **컨텍스트 참조**를 잡고 있기 때문이다(fd 번호가 아니라 객체를 잡는다). 참조는 `SET_EVENTFD(-1)`이나 장치 구조체 해제 때 `eventfd_ctx_put()`으로 놓는다.
- eventfd가 아닌 fd(99는 열려 있지 않다)를 넘기면 `eventfd_ctx_fdget()`이 `-EBADF`를 돌려준다. 열려 있지만 eventfd가 아닌 fd면 `-EINVAL`이다.

### 8.3 언제 eventfd를 고르나

- 이벤트를 **다른 프로세스나 다른 서브시스템**에 넘겨야 할 때 (VFIO → QEMU, 센서 이벤트 → 다른 데몬).
- 장치 fd는 blocking으로 쓰는 스레드가 따로 있고, 이벤트 루프는 "알림"만 받고 싶을 때.
- 여러 장치의 이벤트를 하나의 카운터로 모으고 싶을 때 (같은 eventfd를 여러 드라이버에 등록).

장치 fd 자체를 epoll에 넣을 수 있는 평범한 경우라면 `.poll`만으로 충분하다. eventfd 경로는 ioctl 설계(G5)와 참조 관리가 추가되는 대가가 있다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" font-size="13" font-weight="bold">fasync / SIGIO 경로</text>
  <rect x="10" y="34" width="150" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="85" y="55" text-anchor="middle" font-size="12">kill_fasync()</text>
  <text x="85" y="73" text-anchor="middle" font-size="12">POLL_IN, POLL_PRI</text>
  <rect x="180" y="34" width="150" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="255" y="55" text-anchor="middle" font-size="12">send_sigio()</text>
  <text x="255" y="73" text-anchor="middle" font-size="12">시그널 대기열</text>
  <rect x="350" y="34" width="150" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="425" y="55" text-anchor="middle" font-size="12">SIGIO 핸들러</text>
  <text x="425" y="73" text-anchor="middle" font-size="12">아무 곳에나 끼어듦</text>
  <rect x="520" y="34" width="150" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="595" y="55" text-anchor="middle" font-size="12">합쳐짐 · EINTR</text>
  <text x="595" y="73" text-anchor="middle" font-size="12">핸들러 제약</text>
  <line x1="160" y1="59" x2="176" y2="59" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="330" y1="59" x2="346" y2="59" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="500" y1="59" x2="516" y2="59" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="10" y="112" font-size="12">plain SIGIO: 횟수·fd·이유 정보 없음 / F_SETSIG RT 시그널: 정보는 있지만 큐 한도</text>
  <line x1="10" y1="135" x2="670" y2="135" stroke="#888" stroke-dasharray="4 4"/>
  <text x="10" y="162" font-size="13" font-weight="bold">eventfd 경로</text>
  <rect x="10" y="174" width="150" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="85" y="195" text-anchor="middle" font-size="12">eventfd_signal()</text>
  <text x="85" y="213" text-anchor="middle" font-size="12">6.8+: 인자 ctx 하나</text>
  <rect x="180" y="174" width="150" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="255" y="195" text-anchor="middle" font-size="12">count++</text>
  <text x="255" y="213" text-anchor="middle" font-size="12">wake_up_locked_poll</text>
  <rect x="350" y="174" width="150" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="425" y="195" text-anchor="middle" font-size="12">epoll 루프</text>
  <text x="425" y="213" text-anchor="middle" font-size="12">fd 하나 더 감시</text>
  <rect x="520" y="174" width="150" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="595" y="195" text-anchor="middle" font-size="12">read(efd)</text>
  <text x="595" y="213" text-anchor="middle" font-size="12">누적 횟수 받음</text>
  <line x1="160" y1="199" x2="176" y2="199" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="330" y1="199" x2="346" y2="199" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="500" y1="199" x2="516" y2="199" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="10" y="252" font-size="12">끼어들지 않음 · 횟수 보존 · 장치 fd와 분리돼 다른 프로세스에 넘길 수 있음</text>
  <text x="10" y="274" font-size="12">대가: ioctl로 등록하는 설계, ctx 참조 관리 (VFIO_DEVICE_SET_IRQS, KVM irqfd가 이 모양)</text>
</svg>
```
그림 6 — 같은 "새 샘플" 사건을 시그널로 알리는 길(위)과 유저가 넘겨 준 eventfd로 알리는 길(아래). eventfd 쪽 마지막 단계는 결국 그림 5의 `ep_poll_callback`이다 — eventfd도 wait queue를 가진 평범한 파일이기 때문이다.

---

## 9. `sysfs_notify` — 속성 파일을 poll하기

### 9.1 언제 쓰나

설정·상태 값은 보통 sysfs 속성 파일로 노출한다(G8). 센서 키트라면 `motion_events`(누적 모션 횟수), `calibration_state`, `temperature_alarm` 같은 것. 이 값이 바뀌는 순간 유저가 알고 싶다면? 데이터 스트림용 문자 장치를 따로 만들 필요 없이, 속성 파일 자체를 poll 가능하게 만들 수 있다.

드라이버는 값이 바뀔 때 `sysfs_notify(&dev->kobj, NULL, "motion_events")`를 부른다. 그러면 그 파일을 poll하던 프로세스가 **`POLLPRI | POLLERR`** 로 깨어난다 (`POLLIN`이 아니다 — 속성 파일은 언제나 읽을 수 있으니 IN은 항상 켜져 있다). A4 §3.3에서 본 sysfs GPIO `value` 파일이 바로 이 메커니즘이다.

커널 쪽 구현은 kernfs에 있다:

```c
/* fs/kernfs/file.c (6.18.55) */
__poll_t kernfs_generic_poll(struct kernfs_open_file *of, poll_table *wait)
{
	struct kernfs_open_node *on = of_on(of);

	poll_wait(of->file, &on->poll, wait);

	if (of->event != atomic_read(&on->event))
		return DEFAULT_POLLMASK|EPOLLERR|EPOLLPRI;

	return DEFAULT_POLLMASK;
}
```

`kernfs_notify()`가 `on->event` 카운터를 올리고 `on->poll`을 깨운다. 열린 파일 각각은 "마지막으로 읽었을 때의 event 값"(`of->event`)을 기억하고, 둘이 다르면 `PRI | ERR`를 켠다. **파일을 다시 읽어야 `of->event`가 갱신된다** — 그래서 유저는 깨어날 때마다 offset 0부터 다시 읽어야 하고, 처음 poll하기 전에도 한 번 읽어 둬야 한다.

**문맥 주의**: `sysfs_notify()`는 디렉터리·이름으로 kernfs 노드를 찾느라 `kernfs_find_and_get()`(내부에서 rwsem)을 부를 수 있어 **IRQ·spinlock 안에서 부르면 안 된다**. IRQ 경로에서 알려야 하면 probe에서 `sysfs_get_dirent()`로 노드를 찾아 두고 `sysfs_notify_dirent(kn)`(= `kernfs_notify(kn)`, "Maybe be called from any context")를 부른다. `g6_push`가 hrtimer(hardirq)에서도 불리기 때문에 이 방식을 썼다. 노드 참조는 `sysfs_put()`으로 놓는다.

### 9.2 예제 8 — 속성 파일 poll

무엇을 확인하나: `motion_events` 파일을 열고 한 번 읽은 뒤 `poll(POLLPRI | POLLERR)`로 기다린다. 다른 스레드가 200 ms에 일반 샘플(알림 없음), 400 ms에 모션 1번, 700 ms에 모션 2번을 연달아 넣는다.

```c
/* g6_sysfs: poll() a sysfs attribute that the driver sysfs_notify()s */
#include "g6_common.h"
#include <poll.h>
#include <pthread.h>

static uint64_t t0;

static void *irq_sim(void *a)
{
	(void)a;
	msleep(200); sysw("inject", "1 10");	/* plain sample: no notify */
	msleep(200); sysw("inject", "1 3000");	/* motion #1 */
	msleep(300); sysw("inject", "2 3000");	/* motion #2 + #3 back to back */
	return NULL;
}

int main(void)
{
	int fd = open(SYS "motion_events", O_RDONLY), i, n;
	struct pollfd p = { .events = POLLPRI | POLLERR };
	pthread_t t;
	char b[16];

	if (fd < 0)
		DIE("open");
	n = pread(fd, b, sizeof(b) - 1, 0);	/* read once BEFORE polling */
	b[n > 0 ? n : 0] = 0;
	printf("initial motion_events=%s", b);
	p.fd = fd;
	t0 = now_ns();
	pthread_create(&t, NULL, irq_sim, NULL);
	for (i = 0; i < 3; i++) {
		n = poll(&p, 1, 1000);
		printf("  +%4llu ms poll=%d revents=0x%x", (unsigned long long)
		       ((now_ns() - t0) / 1000000), n, p.revents);
		n = pread(fd, b, sizeof(b) - 1, 0);	/* re-read from offset 0 */
		b[n > 0 ? n : 0] = 0;
		printf("  value=%s", b[0] ? b : "-\n");
	}
	pthread_join(t, NULL);
	return 0;
}
```

```text
[    3.520007] g6_sensorkit g6_sensorkit: /dev/g6_sensorkit ready (qmode=2 wmode=1)
initial motion_events=0
  + 448 ms poll=1 revents=0xa  value=1
  + 761 ms poll=1 revents=0xa  value=3
  +1781 ms poll=0 revents=0x0  value=3
```

출력에서 볼 것:

- `revents=0xa` = `POLLPRI(0x2) | POLLERR(0x8)`. 일반 샘플(200 ms)로는 안 깼고, 첫 모션(약 450 ms — 200+200 ms에 스레드 시작·sysfs 쓰기 지연이 더해졌다)에서 깼다.
- 두 번째 깨어남(약 760 ms)의 값이 **3**이다. 모션 2번이 연달아 왔는데 깨어남은 한 번 — sysfs 알림은 "바뀌었다"는 표시일 뿐 이벤트 큐가 아니다. 그래서 속성 값 자체가 **누적 카운터나 현재 상태**여야 한다 ("마지막 이벤트 종류" 같은 값이면 중간 것을 잃는다).
- 세 번째 poll은 새 알림이 없어 1초 timeout(`poll=0`).

### 9.3 sysfs 알림을 쓸 때의 설계 규칙

- 고빈도 데이터(샘플)에는 쓰지 않는다. 깨어날 때마다 `open`된 파일을 `pread`로 텍스트 파싱해야 하고, 알림이 합쳐진다. 샘플은 문자 장치(+ `.poll`)나 IIO buffer로.
- 드물게 바뀌는 상태(알람, 캘리브레이션 완료, 링크 up/down, 배터리 상태)에 쓴다. 커널의 예: md RAID의 `array_state` 등(`sysfs_notify_dirent_safe()`), 레거시 sysfs GPIO의 `value`.
- 문서(`Documentation/ABI/`)에 "이 속성은 poll 가능하다(`sysfs_notify`)"를 적는다. 유저는 코드만 봐서는 모른다.

---

## 10. 장치가 사라질 때 — `EPOLLHUP`과 수명

### 10.1 무엇이 위험한가

센서 키트의 IMU가 USB로 붙어 있거나, 드라이버를 unbind/rmmod 하거나, DT overlay로 장치가 빠지는 상황(D6, G9)을 생각하자. 그 순간 데몬은 그 장치 fd를 epoll에 넣고 자고 있을 수도 있고, `read()`에서 자고 있을 수도 있다. 드라이버가 할 일:

1. **잠든 사람 전부를 깨운다.** 깨우지 않으면 데몬은 영원히 잔다(장치가 없으니 데이터도 영원히 안 온다).
2. **깨어난 사람에게 "끝났다"고 말한다.** `.poll`은 `EPOLLHUP | EPOLLERR`, `read`/`write`/`ioctl`은 `-ENODEV`. 입력 서브시스템의 `evdev_poll()`이 정확히 이렇게 한다 — `evdev->exist`가 거짓이면(장치가 사라졌으면) `EPOLLHUP | EPOLLERR`를 돌려준다.
3. **wait queue를 해제하지 않는다 — 마지막 파일이 닫힐 때까지.** epoll 엔트리와 poll 엔트리는 드라이버의 `wait_queue_head_t` **안에** 매달려 있다. 장치 구조체를 `remove()`에서 `kfree()`하면, 아직 열려 있는 파일의 epoll 엔트리가 해제된 메모리를 가리킨다. 나중에 `epoll_ctl(DEL)`이나 close가 그 리스트를 건드리는 순간 use-after-free다.

3번의 표준 해법은 **장치 구조체를 refcount(kref)로 관리**하는 것이다. probe가 1, `open`마다 +1, `release`마다 -1, remove가 -1. 0이 되면 해제. 이 노트의 `g6_dev`가 그렇게 돼 있다. epoll 엔트리는 파일에 묶여 있어서(`epitem`은 파일이 닫힐 때 `eventpoll_release()`로 정리된다) "파일이 다 닫힐 때까지 wait queue가 산다"면 안전하다.

드물게 wait queue의 수명이 **파일이 아닌 다른 것**(태스크 등)에 묶여 있으면 `wake_up_pollfree()`를 써야 한다 — `POLLFREE` key로 깨워서 epoll/aio가 엔트리를 스스로 떼게 하고, 해제는 RCU 유예 뒤에 한다. signalfd와 binder가 쓰는 특수한 경우이고, 보통의 장치 드라이버는 kref로 충분하다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <line x1="20" y1="110" x2="660" y2="110" stroke="currentColor" marker-end="url(#ah7)"/>
  <text x="660" y="130" text-anchor="end" font-size="12">시간</text>
  <polyline points="40,90 140,90 140,60 420,60 420,90 560,90 560,110" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="90" y="82" text-anchor="middle" font-size="12">ref=1</text>
  <text x="280" y="52" text-anchor="middle" font-size="12">ref=2</text>
  <text x="490" y="82" text-anchor="middle" font-size="12">ref=1</text>
  <circle cx="40" cy="110" r="5" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <circle cx="140" cy="110" r="5" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <circle cx="250" cy="110" r="5" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <circle cx="350" cy="110" r="5" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <circle cx="420" cy="110" r="5" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <circle cx="560" cy="110" r="5" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <rect x="10" y="150" width="90" height="46" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="55" y="170" text-anchor="middle" font-size="12">probe</text>
  <text x="55" y="187" text-anchor="middle" font-size="12">kref_init</text>
  <rect x="105" y="150" width="90" height="46" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="150" y="170" text-anchor="middle" font-size="12">open</text>
  <text x="150" y="187" text-anchor="middle" font-size="12">kref_get</text>
  <rect x="200" y="150" width="100" height="46" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="250" y="170" text-anchor="middle" font-size="12">epoll_wait</text>
  <text x="250" y="187" text-anchor="middle" font-size="12">rq에 엔트리</text>
  <rect x="305" y="150" width="105" height="70" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="357" y="170" text-anchor="middle" font-size="12">remove()</text>
  <text x="357" y="187" text-anchor="middle" font-size="12">dead = true</text>
  <text x="357" y="204" text-anchor="middle" font-size="12">전원 HUP 깨움</text>
  <rect x="415" y="150" width="105" height="70" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="467" y="170" text-anchor="middle" font-size="12">devm 정리</text>
  <text x="467" y="187" text-anchor="middle" font-size="12">sysfs 제거</text>
  <text x="467" y="204" text-anchor="middle" font-size="12">kref_put</text>
  <rect x="525" y="150" width="145" height="70" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="597" y="170" text-anchor="middle" font-size="12">close → release</text>
  <text x="597" y="187" text-anchor="middle" font-size="12">kref_put → 0</text>
  <text x="597" y="204" text-anchor="middle" font-size="12">g6_free: kfree</text>
  <text x="340" y="250" text-anchor="middle" font-size="12">remove와 kfree 사이: 파일은 열려 있고 .poll은 EPOLLHUP | EPOLLERR, read/write는 -ENODEV</text>
</svg>
```
그림 7 — 장치 구조체의 수명. `remove()`가 불려도 구조체(그리고 그 안의 wait queue)는 마지막 `release`까지 살아 있다. 그 사이 열린 파일로 들어오는 모든 호출은 `dead`를 보고 "끝났다"를 돌려준다. `misc_deregister()`는 remove에서 바로 하므로 새 `open()`은 들어오지 못한다(`/dev` 노드도 사라진다).

### 10.2 예제 9 — 잠든 read · poll · epoll 아래에서 unbind

무엇을 확인하나: 같은 fd로 세 스레드가 각각 blocking `read()`, `poll(POLLIN)`, `epoll_wait`(`EPOLLEXCLUSIVE`!)로 잔다. 300 ms 뒤 `/sys/bus/platform/drivers/g6_sensorkit/unbind`에 장치 이름을 써서 "뽑는다". 그다음 셸에서 fd를 쥔 채로 rmmod를 시도한다.

```c
/* g6_hup: unplug (unbind) the device while read, poll and epoll are asleep */
#include "g6_common.h"
#include <poll.h>
#include <pthread.h>
#include <sys/epoll.h>
static int fd;

static void *t_read(void *a)
{
	struct g6_sample s;
	ssize_t r = read(fd, &s, sizeof(s));		/* blocking read */

	(void)a;
	printf("  read   -> %zd errno=%s\n", r, strerrorname_np(errno));
	return NULL;
}

static void *t_poll(void *a)
{
	struct pollfd p = { .fd = fd, .events = POLLIN };	/* HUP/ERR implicit */
	int n = poll(&p, 1, 3000);

	(void)a;
	printf("  poll   -> %d revents=0x%x\n", n, p.revents);
	return NULL;
}

static void *t_epoll(void *a)
{
	struct epoll_event ev = { .events = EPOLLIN | EPOLLEXCLUSIVE }, out;
	int ep = epoll_create1(0), n;

	(void)a;
	if (ep < 0 || epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) < 0)
		DIE("epoll");
	n = epoll_wait(ep, &out, 1, 3000);
	printf("  epoll  -> %d events=0x%x\n", n, n > 0 ? out.events : 0);
	return NULL;
}

int main(void)
{
	pthread_t t[3];
	int i;

	if ((fd = open(DEV, O_RDWR)) < 0)		/* blocking fd */
		DIE(DEV);
	pthread_create(&t[0], NULL, t_read, NULL);
	pthread_create(&t[1], NULL, t_poll, NULL);
	pthread_create(&t[2], NULL, t_epoll, NULL);
	msleep(300);
	printf("unbind at +300 ms\n");
	i = open("/sys/bus/platform/drivers/g6_sensorkit/unbind", O_WRONLY);
	if (i < 0 || write(i, "g6_sensorkit", 12) < 0)
		DIE("unbind");
	for (i = 0; i < 3; i++)
		pthread_join(t[i], NULL);
	printf("  write  -> %zd errno=%s\n", write(fd, "x", 1), strerrorname_np(errno));
	return 0;
}
```

게스트 스크립트의 `hup` 절:

```sh
insmod /g6_sensorkit.ko; /g6_hup; ls /dev/g6_sensorkit; ls -l /sys/devices/platform/g6_sensorkit/driver
echo "--- rebind, hold an fd, unbind, rmmod"
echo g6_sensorkit > /sys/bus/platform/drivers/g6_sensorkit/bind
exec 3< /dev/g6_sensorkit
echo g6_sensorkit > /sys/bus/platform/drivers/g6_sensorkit/unbind
cat /sys/module/g6_sensorkit/refcnt; rmmod g6_sensorkit
exec 3<&-
cat /sys/module/g6_sensorkit/refcnt; rmmod g6_sensorkit && echo "rmmod OK"
```

```text
[    3.525994] g6_sensorkit g6_sensorkit: /dev/g6_sensorkit ready (qmode=2 wmode=1)
unbind at +300 ms
  poll   -> 1 revents=0x18
  read   -> -1 errno=ENODEV
[    3.906425] g6_sensorkit g6_sensorkit: removed, open files keep the struct alive
  epoll  -> 1 events=0x18
  write  -> -1 errno=ENODEV
[    3.923142] g6: struct freed (last reference)
ls: /dev/g6_sensorkit: No such file or directory
ls: /sys/devices/platform/g6_sensorkit/driver: No such file or directory
--- rebind, hold an fd, unbind, rmmod
[    3.979781] g6_sensorkit g6_sensorkit: /dev/g6_sensorkit ready (qmode=2 wmode=1)
[    3.983708] g6_sensorkit g6_sensorkit: removed, open files keep the struct alive
1
rmmod: remove 'g6_sensorkit': Resource temporarily unavailable
[    4.017347] g6: struct freed (last reference)
0
rmmod OK
```

(콘솔 커널 메시지와 프로그램 출력이 섞여 찍힌다. 끝의 `dmesg | grep g6` 반복 줄은 생략했다.)

출력에서 볼 것:

- 세 대기자가 unbind 직후 모두 깨어났다. `read`는 `-1 ENODEV`, poll은 `revents=0x18`, epoll은 `events=0x18` = `EPOLLERR(0x08) | EPOLLHUP(0x10)`. 유저는 `POLLIN`만 요청했지만 HUP/ERR는 요청 없이 항상 보고된다. epoll 쪽이 `EPOLLEXCLUSIVE`인데도 깬 것은 remove가 `nr_exclusive = 0`으로 깨웠기 때문이다.
- `removed, open files keep the struct alive` 다음, **프로그램이 끝나 fd가 닫힌 뒤에야** `struct freed (last reference)`가 찍혔다. 그 사이 `write`도 `-ENODEV`로 안전하게 실패했다.
- unbind 뒤 `/dev/g6_sensorkit`과 `driver` 링크가 사라졌다.
- 두 번째 실험: fd 3을 쥔 채 unbind하면 `refcnt`가 1(열린 파일의 `fops->owner` 참조)이고 rmmod가 거부된다(커널 `try_stop_module()`이 `-EWOULDBLOCK` = `EAGAIN`을 돌려준다). **unbind는 되지만 모듈 언로드는 안 된다** — 코드(`.text`)는 파일이 남아 있는 동안 필요하기 때문이다. fd를 닫자 그 순간 구조체가 해제되고 refcnt 0, rmmod 성공.

### 10.3 체크리스트 — "장치 제거 중 poll"

- [ ] `dead`(또는 `exist`) 플래그를 락 아래 세운다.
- [ ] 모든 wait queue를 HUP/ERR key로, `nr_exclusive = 0`으로 깨운다.
- [ ] `kill_fasync(…, SIGIO, POLL_HUP)`을 보낸다.
- [ ] `.poll`은 dead면 `EPOLLHUP | EPOLLERR`만 돌려준다.
- [ ] `read`/`write`/`ioctl`은 dead면 `-ENODEV`. blocking 대기의 조건식에 dead가 들어 있다.
- [ ] IRQ·타이머·workqueue를 멈춘 뒤 dead를 세운다 (그 뒤에 push가 오지 않게).
- [ ] 구조체는 kref로 관리하고 마지막 `release`에서 해제한다.

---

## 11. ftrace로 경로 확인하기

§1.4의 표와 그림 5를 실제 커널에서 확인한다. 디버그 커널의 ftrace를 쓴다 (`tracefs`는 자동 마운트되지 않으므로 직접 마운트).

### 11.1 예제 10 — 누가 `g6_poll`을 부르고, `wake_up`은 누구를 부르나

무엇을 확인하나: `function` tracer에 `func_stack_trace` 옵션을 켜고 `g6_poll`, `ep_poll_callback`, `pollwake` 세 함수만 걸러, 불릴 때마다 호출 스택을 찍는다. 테스트 프로그램은 epoll 대기 한 번과 poll 대기 한 번을 하고, 두 번 다 hrtimer("가짜 IRQ", 30 ms)가 깨운다. `trace_marker`에 구간 표시를 쓴다.

```c
/* g6_trace: one epoll and one poll wait, woken by the hrtimer "IRQ", for ftrace */
#include "g6_common.h"
#include <poll.h>
#include <sys/epoll.h>

static int mk;

static void mark(const char *m)
{
	if (write(mk, m, strlen(m)) < 0)
		DIE("trace_marker");
}

int main(void)
{
	int fd = open(DEV, O_RDONLY | O_NONBLOCK), ep = epoll_create1(0);
	struct epoll_event ev = { .events = EPOLLIN }, out;
	struct pollfd p = { .events = POLLIN };

	mk = open("/sys/kernel/tracing/trace_marker", O_WRONLY);
	if (fd < 0 || ep < 0 || mk < 0)
		DIE("setup");
	mark("== epoll_ctl(ADD)");
	if (epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) < 0)
		DIE("epoll_ctl");
	mark("== epoll_wait, timer armed");
	sysw("period_us", "30000");
	if (epoll_wait(ep, &out, 1, 1000) != 1)
		DIE("epoll_wait");
	sysw("period_us", "0");
	drain(fd);
	close(ep);
	mark("== poll, timer armed");
	p.fd = fd;
	sysw("period_us", "30000");
	if (poll(&p, 1, 1000) != 1)
		DIE("poll");
	sysw("period_us", "0");
	mark("== done");
	return 0;
}
```

```sh
# 게스트에서 (g6_run.sh의 trace 절 앞부분)
mount -t tracefs none /sys/kernel/tracing; T=/sys/kernel/tracing
echo "g6_poll ep_poll_callback pollwake" > $T/set_ftrace_filter
echo function > $T/current_tracer; echo 1 > $T/options/func_stack_trace
echo 1 > $T/tracing_on; /g6_trace; echo 0 > $T/tracing_on
cat $T/trace
```

```text
# tracer: function
#
# entries-in-buffer/entries-written: 16/16   #P:2
#
#                                _-----=> irqs-off/BH-disabled
#                               / _----=> need-resched
#                              | / _---=> hardirq/softirq
#                              || / _--=> preempt-depth
#                              ||| / _-=> migrate-disable
#                              |||| /     delay
#           TASK-PID     CPU#  |||||  TIMESTAMP  FUNCTION
#              | |         |   |||||     |         |
        g6_trace-75      [000] ...1.     4.216918: tracing_mark_write: == epoll_ctl(ADD)
        g6_trace-75      [000] .....     4.218421: g6_poll <-ep_item_poll
        g6_trace-75      [000] .....     4.218559: <stack trace>
 => g6_poll
 => ep_item_poll
 => do_epoll_ctl
 => __arm64_sys_epoll_ctl
 => invoke_syscall.constprop.0
 => el0_svc_common.constprop.0
        g6_trace-75      [000] ...1.     4.219233: tracing_mark_write: == epoll_wait, timer armed
          <idle>-0       [000] d.h2.     4.261679: ep_poll_callback <-__wake_up_common
          <idle>-0       [000] d.h2.     4.261696: <stack trace>
 => ep_poll_callback
 => __wake_up_common
 => __wake_up
 => g6_push
 => g6_tick
 => __hrtimer_run_queues
 => hrtimer_interrupt
 => arch_timer_handler_virt
 => handle_percpu_devid_irq
 => handle_irq_desc
 => generic_handle_domain_irq
 => gic_handle_irq
 => call_on_irq_stack
 => do_interrupt_handler
 => el1_interrupt
 => el1h_64_irq_handler
 => el1h_64_irq
 => default_idle_call
 => do_idle
 => cpu_startup_entry
 => rest_init
 => start_kernel
 => __primary_switched
        g6_trace-75      [000] .....     4.262636: g6_poll <-ep_item_poll
        g6_trace-75      [000] .....     4.262640: <stack trace>
 => g6_poll
 => ep_item_poll
 => ep_send_events
 => do_epoll_wait
 => do_compat_epoll_pwait.part.0
 => __arm64_sys_epoll_pwait
 => invoke_syscall.constprop.0
 => el0_svc_common.constprop.0
        g6_trace-75      [000] ...1.     4.264538: tracing_mark_write: == poll, timer armed
        g6_trace-75      [000] .....     4.265443: g6_poll <-do_sys_poll
        g6_trace-75      [000] .....     4.265448: <stack trace>
 => g6_poll
 => do_sys_poll
 => __arm64_sys_ppoll
 => invoke_syscall.constprop.0
 => el0_svc_common.constprop.0
          <idle>-0       [000] d.h2.     4.295577: pollwake <-__wake_up_common
          <idle>-0       [000] d.h2.     4.295586: <stack trace>
 => pollwake
 => __wake_up_common
 => __wake_up
 => g6_push
 => g6_tick
 => __hrtimer_run_queues
 => hrtimer_interrupt
 => arch_timer_handler_virt
 => handle_percpu_devid_irq
 => handle_irq_desc
 => generic_handle_domain_irq
 => gic_handle_irq
 => call_on_irq_stack
 => do_interrupt_handler
 => el1_interrupt
 => el1h_64_irq_handler
 => el1h_64_irq
 => default_idle_call
 => do_idle
 => cpu_startup_entry
 => rest_init
 => start_kernel
 => __primary_switched
        g6_trace-75      [000] .....     4.295879: g6_poll <-do_sys_poll
        g6_trace-75      [000] .....     4.295883: <stack trace>
 => g6_poll
 => do_sys_poll
 => __arm64_sys_ppoll
 => invoke_syscall.constprop.0
 => el0_svc_common.constprop.0
        g6_trace-75      [000] ...1.     4.296955: tracing_mark_write: == done
```

(헤더는 그대로 두고, syscall 스택마다 `invoke_syscall.constprop.0` 아래에 붙는 공통 꼬리 다섯 줄 `el0_svc_common` → `do_el0_svc` → `el0_svc` → `el0t_64_sync_handler` → `el0t_64_sync`만 지웠다.)

출력에서 볼 것 — §1.4 표의 네 줄이 전부 보인다:

1. `epoll_ctl(ADD)`: `__arm64_sys_epoll_ctl` → `do_epoll_ctl` → `ep_item_poll` → **`g6_poll`**. (`ep_insert`와 `vfs_poll`은 인라인돼 스택에 안 보인다.)
2. 타이머가 터지면 **`<idle>-0` 태스크, `d.h2.` 플래그**(irqs-off, **h**ardirq) 문맥에서 `g6_tick` → `g6_push` → `__wake_up` → `__wake_up_common` → **`ep_poll_callback`**. 그 아래로 `hrtimer_interrupt` → `gic_handle_irq` → … → `default_idle_call`이 보인다. CPU가 idle(WFI)에 있다가 타이머 IRQ로 깨어난 것이다. 드라이버의 wake_up이 IRQ 문맥에서 epoll 콜백까지 곧장 이어진다는 것, 그리고 콜백이 잠들면 안 되는 이유(F2)가 이 스택 한 장에 있다.
3. 깨어난 `epoll_wait`: `do_epoll_wait` → `ep_send_events` → `ep_item_poll` → **`g6_poll`** (다시 묻기).
4. `poll()`: `__arm64_sys_ppoll` → `do_sys_poll` → `g6_poll`, 타이머가 **`pollwake`** 를 부르고, 깨어난 `do_sys_poll`이 `g6_poll`을 한 번 더 부른다. 그림 2 그대로다.

### 11.2 예제 11 — `g6_poll` 안에서 무슨 일이: `function_graph`

무엇을 확인하나: `set_graph_function=g6_poll`, `max_graph_depth=3`으로 `g6_poll` 내부 호출만 그린다. 같은 `g6_trace`를 다시 돌린다. 디버그 커널은 spinlock마다 lockdep 내부 호출(`unwind_find_stack…`, `preempt_count_*`, `do_raw_spin_*`)이 수십 줄씩 찍혀서 `set_graph_notrace`로 뺐다. `funcgraph-irqs=0`은 그래프 도중 끼어든 hardirq 처리를 숨긴다.

```sh
echo 0 > $T/options/func_stack_trace; echo > $T/trace
echo > $T/set_ftrace_filter; echo g6_poll > $T/set_graph_function
printf "unwind_find_stack.constprop.0\npreempt_count_add\npreempt_count_sub\ndo_raw_spin_trylock\ndo_raw_spin_unlock\n" > $T/set_graph_notrace
echo 3 > $T/max_graph_depth; echo function_graph > $T/current_tracer
echo 0 > $T/options/funcgraph-irqs; echo 1 > $T/tracing_on
/g6_trace; echo 0 > $T/tracing_on; cat $T/trace
```

```text
# tracer: function_graph
#
# CPU  DURATION                  FUNCTION CALLS
# |     |   |                     |   |   |   |
 0)               |  /* == epoll_ctl(ADD) */
 0)               |  g6_poll [g6_sensorkit]() {
 0)               |    ep_ptable_queue_proc() {
 0)   9.632 us    |      kmem_cache_alloc_noprof();
 0) + 34.880 us   |      add_wait_queue();
 0) ! 155.664 us  |    }
 0)               |    ep_ptable_queue_proc() {
 0)   8.080 us    |      kmem_cache_alloc_noprof();
 0) + 26.800 us   |      add_wait_queue();
 0) + 40.336 us   |    }
 0)               |    ep_ptable_queue_proc() {
 0)   7.712 us    |      kmem_cache_alloc_noprof();
 0) + 25.536 us   |      add_wait_queue();
 0) + 38.256 us   |    }
 0) + 12.416 us   |    _raw_spin_lock_irqsave();
 0) + 76.912 us   |    _raw_spin_unlock_irqrestore();
 0) ! 428.912 us  |  }
 0)               |  /* == epoll_wait, timer armed */
 0)               |  g6_poll [g6_sensorkit]() {
 0) + 13.504 us   |    _raw_spin_lock_irqsave();
 0) + 12.960 us   |    _raw_spin_unlock_irqrestore();
 0) + 39.968 us   |  }
 0)               |  /* == poll, timer armed */
 0)               |  g6_poll [g6_sensorkit]() {
 0)               |    __pollwait() {
 0) ! 375.536 us  |      add_wait_queue();
 0) ! 393.088 us  |    }
 0)               |    __pollwait() {
 0) + 27.456 us   |      add_wait_queue();
 0) + 31.792 us   |    }
 0)               |    __pollwait() {
 0) + 24.208 us   |      add_wait_queue();
 0) + 27.536 us   |    }
 0) + 12.080 us   |    _raw_spin_lock_irqsave();
 0) + 12.160 us   |    _raw_spin_unlock_irqrestore();
 0) ! 497.008 us  |  }
 0)               |  g6_poll [g6_sensorkit]() {
 0) + 16.208 us   |    _raw_spin_lock_irqsave();
 0)               |    _raw_spin_unlock_irqrestore() {
 0)   4.784 us    |      irq_enter_rcu();
 0) + 99.744 us   |      do_softirq_own_stack();
 0)   1.504 us    |      idle_cpu();
 0)   1.408 us    |      raw_irqentry_exit_cond_resched();
 0) ! 487.840 us  |    }
 0) ! 516.592 us  |  }
 0)               |  /* == done */
```

(줄인 곳 없이 그대로다. 시간 값은 TCG + lockdep이라 매우 크다.)

출력에서 볼 것:

- `epoll_ctl(ADD)` 때 `g6_poll` 안에서 **`ep_ptable_queue_proc`이 세 번** — `poll_wait`을 `rq`, `wq`, `eq`에 한 번씩 불렀기 때문이다. 각 호출이 `eppoll_entry`를 `kmem_cache_alloc`하고 `add_wait_queue`로 건다. epoll 등록이 "fd당 메모리 할당"을 하는 이유다.
- `poll()`의 첫 호출에서는 **`__pollwait`이 세 번**, 각각 `add_wait_queue`.
- `epoll_wait`이 깨어나 다시 부른 `g6_poll`과 `poll()`의 두 번째 `g6_poll`에는 **등록이 없다** — `_qproc == NULL`이라 `poll_wait`이 아무것도 안 한다. 락 잡고 상태 읽고 끝. "다시 물을 때는 등록 비용이 없다"가 눈으로 확인된다.
- 마지막 `_raw_spin_unlock_irqrestore` 안의 `irq_enter_rcu`, `do_softirq_own_stack`은 `spin_lock_irqsave`가 막아 둔 인터럽트가 irqrestore 순간 처리된 흔적이다. `.poll`이 IRQ를 끄고 있는 구간은 짧게 유지해야 하는 이유이기도 하다.

---

## 12. 깨우기에서 유저 도착까지 — poll vs epoll vs SIGIO vs eventfd

### 12.1 예제 12 — 지연 측정

무엇을 확인하나: hrtimer가 5 ms마다(200 Hz) 샘플을 넣는다. 드라이버는 샘플에 `ktime_get_ns()` 타임스탬프를 찍고 곧바로 통지한다. 유저는 깨어난 직후 `CLOCK_MONOTONIC`을 읽고, 그 깨어남에서 가장 오래된 샘플의 타임스탬프를 뺀다 — "드라이버가 알린 순간부터 유저 코드가 돌기 시작한 순간까지". 300개씩 모아 분위수를 낸다. SIGIO는 핸들러 안에서 시각을 기록하고(`clock_gettime`은 async-signal-safe), 메인은 `sigsuspend`로 기다린다. eventfd는 장치 fd가 아니라 eventfd를 epoll로 기다린다.

```c
/* g6_lat: driver timestamp -> userspace latency for poll/epoll/SIGIO/eventfd */
#include "g6_common.h"
#include <poll.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#define N 300
static volatile uint64_t t_sig;
static void on_sigio(int s) { (void)s; t_sig = now_ns(); }
static int cmp(const void *a, const void *b)
{ return (*(const uint64_t *)a > *(const uint64_t *)b) - (*(const uint64_t *)a < *(const uint64_t *)b); }

int main(int argc, char **argv)
{
	int m = argc > 1 ? atoi(argv[1]) : 0, fd = open(DEV, O_RDONLY | O_NONBLOCK), i = 0, w = fd;
	int ep = epoll_create1(0), efd = eventfd(0, EFD_NONBLOCK);
	const char *name[] = { "poll", "epoll", "SIGIO", "eventfd" };
	struct epoll_event ev = { .events = EPOLLIN }, out;
	struct pollfd p = { .fd = fd, .events = POLLIN };
	static uint64_t lat[N];
	struct g6_sample s;
	sigset_t blk, old;
	uint64_t t, c;

	if (fd < 0 || ep < 0 || efd < 0)
		DIE("setup");
	if (m == 3 && ioctl(fd, G6_IOC_SET_EVENTFD, &efd) < 0)
		DIE("SET_EVENTFD");
	if (m == 3)
		w = efd;
	if (epoll_ctl(ep, EPOLL_CTL_ADD, w, &ev) < 0)
		DIE("epoll_ctl");
	if (m == 2) {
		signal(SIGIO, on_sigio);
		sigemptyset(&blk); sigaddset(&blk, SIGIO);
		sigprocmask(SIG_BLOCK, &blk, &old);	/* open only in sigsuspend */
		fcntl(fd, F_SETOWN, getpid());
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_ASYNC);
	}
	drain(fd);
	sysw("period_us", "5000");			/* 200 Hz fake IRQ */
	while (i < N) {
		if (m == 0 && poll(&p, 1, -1) < 0) DIE("poll");
		if ((m == 1 || m == 3) && epoll_wait(ep, &out, 1, -1) < 0) DIE("epoll");
		if (m == 2) sigsuspend(&old);		/* handler sets t_sig */
		t = m == 2 ? t_sig : now_ns();
		if (m == 3 && read(efd, &c, sizeof(c)) < 0) DIE("efd");
		while (read(fd, &s, sizeof(s)) == sizeof(s))
			if (i < N && t > s.ts_ns)		/* oldest sample per wake */
				lat[i++] = t - s.ts_ns, t = 0;
	}
	sysw("period_us", "0");
	qsort(lat, N, sizeof(lat[0]), cmp);
	printf("%-8s n=%d  min %6.1f  p50 %6.1f  p90 %6.1f  p99 %7.1f us\n", name[m], N,
	       lat[0] / 1e3, lat[N / 2] / 1e3, lat[N * 9 / 10] / 1e3, lat[N * 99 / 100] / 1e3);
	return 0;
}
```

일반 랩 커널(`out/Image`, lockdep 없음)에서 같은 소스를 `linux-build`로 다시 빌드한 모듈로, 4가지 방식을 번갈아 3번씩 도는 부팅을 3번 했다 (`-smp 2`, TCG). 세 부팅의 로그를 모아 본 것:

```sh
grep -H "n=300" lat_rel.log lat_rel1.log lat_rel2.log
```

```text
lat_rel.log:poll     n=300  min   39.3  p50  363.3  p90  464.1  p99   733.6 us
lat_rel.log:epoll    n=300  min  131.8  p50  610.1  p90  798.7  p99  1132.3 us
lat_rel.log:SIGIO    n=300  min  144.8  p50  575.9  p90  679.9  p99  1069.3 us
lat_rel.log:eventfd  n=300  min  111.6  p50  442.6  p90  640.3  p99  1241.9 us
lat_rel.log:poll     n=300  min   81.9  p50  489.1  p90  721.9  p99  1092.6 us
lat_rel.log:epoll    n=300  min   70.5  p50  378.4  p90  818.7  p99  1153.2 us
lat_rel.log:SIGIO    n=300  min  163.6  p50  561.3  p90  968.2  p99  1281.5 us
lat_rel.log:eventfd  n=300  min  109.7  p50  414.5  p90  703.6  p99  1293.4 us
lat_rel.log:poll     n=300  min  121.6  p50  403.3  p90  502.2  p99  1343.1 us
lat_rel.log:epoll    n=300  min  113.0  p50  404.4  p90  563.3  p99   993.2 us
lat_rel.log:SIGIO    n=300  min  133.8  p50  577.4  p90  735.4  p99  1210.0 us
lat_rel.log:eventfd  n=300  min   83.0  p50  541.8  p90  789.6  p99  1180.5 us
lat_rel1.log:poll     n=300  min   36.0  p50  359.2  p90  475.4  p99   928.2 us
lat_rel1.log:epoll    n=300  min   84.7  p50  374.6  p90  431.3  p99   588.8 us
lat_rel1.log:SIGIO    n=300  min   84.5  p50  528.3  p90  839.4  p99  1173.3 us
lat_rel1.log:eventfd  n=300  min  120.4  p50  412.3  p90  498.3  p99   978.1 us
lat_rel1.log:poll     n=300  min  136.7  p50  431.2  p90  516.5  p99  1211.6 us
lat_rel1.log:epoll    n=300  min  108.2  p50  390.9  p90  542.8  p99   875.6 us
lat_rel1.log:SIGIO    n=300  min  145.1  p50  511.3  p90  642.1  p99  1112.4 us
lat_rel1.log:eventfd  n=300  min   74.3  p50  378.0  p90  537.6  p99   770.3 us
lat_rel1.log:poll     n=300  min   79.7  p50  401.9  p90  510.4  p99   874.8 us
lat_rel1.log:epoll    n=300  min  141.6  p50  439.3  p90  816.8  p99  1713.3 us
lat_rel1.log:SIGIO    n=300  min  201.9  p50  650.7  p90  894.4  p99  1469.8 us
lat_rel1.log:eventfd  n=300  min   61.7  p50  406.3  p90  672.1  p99  1166.3 us
lat_rel2.log:poll     n=300  min   49.8  p50  329.0  p90  434.4  p99  1042.9 us
lat_rel2.log:epoll    n=300  min   79.3  p50  490.6  p90  710.4  p99  1003.2 us
lat_rel2.log:SIGIO    n=300  min   72.8  p50  364.0  p90  671.8  p99  1069.0 us
lat_rel2.log:eventfd  n=300  min   52.1  p50  346.1  p90  535.5  p99   921.3 us
lat_rel2.log:poll     n=300  min  102.9  p50  423.6  p90  796.1  p99  1153.7 us
lat_rel2.log:epoll    n=300  min  107.8  p50  514.1  p90  871.4  p99  1276.4 us
lat_rel2.log:SIGIO    n=300  min  188.7  p50  525.3  p90  712.8  p99  1060.7 us
lat_rel2.log:eventfd  n=300  min  111.4  p50  381.7  p90  491.8  p99   647.3 us
lat_rel2.log:poll     n=300  min  124.9  p50  385.1  p90  462.5  p99  1152.7 us
lat_rel2.log:epoll    n=300  min  117.6  p50  560.3  p90  843.8  p99  1194.1 us
lat_rel2.log:SIGIO    n=300  min  162.5  p50  624.6  p90  934.8  p99  1477.4 us
lat_rel2.log:eventfd  n=300  min  129.6  p50  586.7  p90  723.7  p99  1192.5 us
```

참고로 디버그 커널(`Image-dbg`, lockdep·ftrace 포함)에서 같은 방식으로 3번씩 돌린 결과:

```text
poll     n=300  min  136.2  p50  828.3  p90 1435.6  p99  2712.4 us
epoll    n=300  min  234.7  p50  942.2  p90 1330.5  p99  2670.7 us
SIGIO    n=300  min  400.3  p50 1297.0  p90 1783.0  p99  2709.9 us
eventfd  n=300  min  230.7  p50  959.1  p90 1369.8  p99  2537.3 us
poll     n=300  min  237.5  p50  897.1  p90 1436.9  p99  2977.3 us
epoll    n=300  min  250.1  p50  924.4  p90 1460.2  p99  2108.6 us
SIGIO    n=300  min  533.0  p50 1274.9  p90 1823.1  p99  3194.4 us
eventfd  n=300  min  234.4  p50  895.6  p90 1379.2  p99  2503.4 us
poll     n=300  min  236.7  p50 1053.1  p90 1701.8  p99  2784.0 us
epoll    n=300  min  304.7  p50  902.9  p90 1502.8  p99  2268.3 us
SIGIO    n=300  min  344.4  p50 1244.4  p90 1815.6  p99  2478.9 us
eventfd  n=300  min  245.5  p50  813.8  p90 1317.4  p99  2756.4 us
```

```svg
<svg viewBox="0 0 640 330" xmlns="http://www.w3.org/2000/svg">
  <line x1="70" y1="270.0" x2="620" y2="270.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="274.0" text-anchor="end" font-size="12">0</text>
  <line x1="70" y1="231.7" x2="620" y2="231.7" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="235.7" text-anchor="end" font-size="12">150</text>
  <line x1="70" y1="193.3" x2="620" y2="193.3" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="197.3" text-anchor="end" font-size="12">300</text>
  <line x1="70" y1="155.0" x2="620" y2="155.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="159.0" text-anchor="end" font-size="12">450</text>
  <line x1="70" y1="116.7" x2="620" y2="116.7" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="120.7" text-anchor="end" font-size="12">600</text>
  <line x1="70" y1="78.3" x2="620" y2="78.3" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="82.3" text-anchor="end" font-size="12">750</text>
  <line x1="70" y1="40.0" x2="620" y2="40.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="62" y="44.0" text-anchor="end" font-size="12">900</text>
  <line x1="70" y1="40" x2="70" y2="270" stroke="currentColor"/>
  <line x1="70" y1="270" x2="620" y2="270" stroke="currentColor"/>
  <text x="16" y="155" font-size="12" transform="rotate(-90 16 155)" text-anchor="middle">지연 (µs)</text>
  <rect x="81.8" y="249.1" width="34" height="20.9" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="98.8" y="244.1" text-anchor="middle" font-size="12">82</text>
  <rect x="121.8" y="167.3" width="34" height="102.7" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="138.8" y="162.3" text-anchor="middle" font-size="12">402</text>
  <rect x="161.8" y="141.7" width="34" height="128.3" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="178.8" y="136.7" text-anchor="middle" font-size="12">502</text>
  <text x="138.8" y="290" text-anchor="middle" font-size="13">poll</text>
  <rect x="219.2" y="242.3" width="34" height="27.7" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="236.2" y="237.3" text-anchor="middle" font-size="12">108</text>
  <rect x="259.2" y="157.7" width="34" height="112.3" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="276.2" y="152.7" text-anchor="middle" font-size="12">439</text>
  <rect x="299.2" y="65.9" width="34" height="204.1" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="316.2" y="60.9" text-anchor="middle" font-size="12">799</text>
  <text x="276.2" y="290" text-anchor="middle" font-size="13">epoll</text>
  <rect x="356.8" y="232.9" width="34" height="37.1" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="373.8" y="227.9" text-anchor="middle" font-size="12">145</text>
  <rect x="396.8" y="126.6" width="34" height="143.4" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="413.8" y="121.6" text-anchor="middle" font-size="12">561</text>
  <rect x="436.8" y="82.1" width="34" height="187.9" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="453.8" y="77.1" text-anchor="middle" font-size="12">735</text>
  <text x="413.8" y="290" text-anchor="middle" font-size="13">SIGIO</text>
  <rect x="494.2" y="242.0" width="34" height="28.0" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="511.2" y="237.0" text-anchor="middle" font-size="12">110</text>
  <rect x="534.2" y="164.6" width="34" height="105.4" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="551.2" y="159.6" text-anchor="middle" font-size="12">412</text>
  <rect x="574.2" y="106.4" width="34" height="163.6" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="591.2" y="101.4" text-anchor="middle" font-size="12">640</text>
  <text x="551.2" y="290" text-anchor="middle" font-size="13">eventfd</text>
  <rect x="380" y="12" width="14" height="14" fill="#888" fill-opacity="0.6" stroke="#888"/>
  <text x="400" y="24" font-size="12">min</text>
  <rect x="460" y="12" width="14" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="480" y="24" font-size="12">p50</text>
  <rect x="540" y="12" width="14" height="14" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="560" y="24" font-size="12">p90</text>
  <text x="70" y="24" font-size="12">TCG, Image 6.18.55, 9회 실행 중앙값</text>
  <text x="345" y="320" text-anchor="middle" font-size="12">드라이버 ktime_get_ns() → 유저가 깨어난 시각, 200 Hz hrtimer</text>
</svg>
```
그림 8 — 예제 12의 결과(일반 랩 커널, 부팅 3번 × 3회 = 실행 9번의 각 분위수 중앙값). poll·epoll·eventfd는 잡음 범위 안에서 같고, SIGIO만 최솟값·중앙값이 뚜렷이 크다. p90 이상은 TCG 잡음이 지배한다.

출력에서 볼 것:

- **poll, epoll, eventfd는 구별되지 않는다.** 9번 실행의 p50 중앙값이 각각 약 400, 440, 410 µs이고, 한 실행 안에서도 순서가 매번 바뀐다. fd 하나를 기다릴 때 세 방식의 깨우기 경로는 길이가 거의 같다 — wait queue 엔트리 콜백 하나(`pollwake` 또는 `ep_poll_callback`) + 태스크 깨우기 + 스케줄. eventfd는 콜백이 한 단계 더(드라이버 → eventfd의 `wqh` → `ep_poll_callback`) 있지만 측정 잡음보다 훨씬 작다.
- **SIGIO가 가장 느린 쪽이다.** p50 중앙값 약 560 µs, 최솟값 중앙값 약 145 µs로 나머지(80~110 µs)보다 크다. 9번 중 7번은 SIGIO의 p50이 네 방식 중 가장 컸다. 시그널 전달은 `send_sigio` → 시그널 큐에 넣기 → 대상 태스크 깨우기 → 유저로 돌아갈 때 시그널 프레임을 만들어 핸들러로 점프하는 과정이 추가된다.
- **절대값은 믿지 말자.** 5 ms 주기에 p50이 수백 µs라는 것 자체가 TCG(명령어 번역 에뮬레이션) 비용이다. 같은 프로그램을 (공용 헤더를 다듬기 전에) 호스트가 한가할 때 돌린 실행에서는 p50이 poll/epoll/eventfd 90~130 µs, SIGIO 150~180 µs였다 — **같은 코드가 호스트 부하에 따라 4배 달라졌다.** 디버그 커널에서는 lockdep 때문에 다시 2배쯤 크다. p99는 전부 1 ms 안팎으로 흔들린다. 실제 보드에서 다시 재야 하고(Cortex-A53/A72급이면 이 경로는 보통 수십 µs 이하라고 알려져 있다 — 이 문장은 측정이 아니라 일반적인 추정이다), 비교는 같은 부팅 안의 상대값으로만 읽는다.
- 결론은 "속도"가 아니라 **API 성질**로 고르라는 것이다. fd 하나면 poll/epoll/eventfd 아무거나 비슷하고, fd가 많으면 epoll(A4 §9), 다른 프로세스에 넘기려면 eventfd, 시그널은 피한다.

---

## 13. 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="150" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="35" text-anchor="middle" font-size="12">select / pselect6</text>
  <rect x="180" y="10" width="150" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="255" y="35" text-anchor="middle" font-size="12">poll / ppoll</text>
  <rect x="350" y="10" width="150" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="425" y="35" text-anchor="middle" font-size="12">epoll_ctl(ADD)</text>
  <rect x="520" y="10" width="150" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="595" y="35" text-anchor="middle" font-size="12">epoll_wait</text>
  <rect x="10" y="80" width="150" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="85" y="105" text-anchor="middle" font-size="12">do_select</text>
  <rect x="180" y="80" width="150" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="255" y="105" text-anchor="middle" font-size="12">do_poll → do_pollfd</text>
  <rect x="350" y="80" width="150" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="425" y="105" text-anchor="middle" font-size="12">ep_insert</text>
  <rect x="520" y="80" width="150" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="595" y="105" text-anchor="middle" font-size="12">ep_send_events</text>
  <line x1="85" y1="50" x2="85" y2="76" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="255" y1="50" x2="255" y2="76" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="425" y1="50" x2="425" y2="76" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="595" y1="50" x2="595" y2="76" stroke="currentColor" marker-end="url(#ah9)"/>
  <rect x="180" y="150" width="320" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="177" text-anchor="middle" font-size="13">vfs_poll(file, pt) → f_op->poll = g6_poll</text>
  <line x1="85" y1="120" x2="200" y2="148" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="255" y1="120" x2="290" y2="148" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="425" y1="120" x2="390" y2="148" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="595" y1="120" x2="480" y2="148" stroke="currentColor" marker-end="url(#ah9)"/>
  <text x="560" y="140" font-size="12">ep_item_poll</text>
  <rect x="180" y="220" width="320" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="245" text-anchor="middle" font-size="13">poll_wait(f, &amp;rq, pt) → pt->_qproc</text>
  <line x1="340" y1="194" x2="340" y2="216" stroke="currentColor" marker-end="url(#ah9)"/>
  <rect x="10" y="290" width="200" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="310" text-anchor="middle" font-size="12">__pollwait</text>
  <text x="110" y="327" text-anchor="middle" font-size="12">엔트리 콜백 = pollwake</text>
  <rect x="240" y="290" width="200" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="310" text-anchor="middle" font-size="12">ep_ptable_queue_proc</text>
  <text x="340" y="327" text-anchor="middle" font-size="12">콜백 = ep_poll_callback</text>
  <rect x="470" y="290" width="200" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="570" y="310" text-anchor="middle" font-size="12">NULL</text>
  <text x="570" y="327" text-anchor="middle" font-size="12">다시 묻기: 등록 안 함</text>
  <line x1="260" y1="260" x2="120" y2="286" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="340" y1="260" x2="340" y2="286" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="420" y1="260" x2="560" y2="286" stroke="currentColor" marker-end="url(#ah9)"/>
  <text x="340" y="365" text-anchor="middle" font-size="12">깨울 때: wake_up_*_poll(&amp;rq, key) → __wake_up_common → 엔트리마다 콜백(key로 거름)</text>
  <text x="340" y="387" text-anchor="middle" font-size="12">별도 경로: kill_fasync → send_sigio · eventfd_signal → eventfd wqh · kernfs_notify → on->poll</text>
</svg>
```
그림 9 — select·poll·epoll이 드라이버의 `.poll` 하나로 모이는 경로와, `poll_wait`이 호출자에 따라 다른 엔트리를 거는 모습. 드라이버 코드는 맨 가운데 두 상자(`g6_poll`, `poll_wait`)와 맨 아래 "깨울 때" 한 줄이 전부다.

자료구조로 정리하면:

| 구조체 / 함수 | 어디 있나 | 역할 |
|---|---|---|
| `struct file_operations.poll` | 드라이버 | 등록 + 상태 마스크 |
| `poll_table` (`_qproc`, `_key`) | 호출자 스택 | "어떻게 등록할지"와 "무엇을 묻는지" |
| `struct poll_wqueues` / `poll_table_entry` | select/poll 호출 동안 스택·임시 페이지 | 엔트리 + `pollwake` 콜백, syscall 끝나면 전부 해제 (`poll_freewait`) |
| `struct epitem` / `eppoll_entry` | epoll 인스턴스 (slab) | fd 하나당 epitem, `poll_wait` 한 번당 eppoll_entry. `EPOLL_CTL_DEL`/close까지 유지 |
| `wait_queue_head_t` | 드라이버 구조체 | 엔트리들이 매달리는 곳. **드라이버 구조체 수명 ≥ 열린 파일 수명** 이어야 한다 |
| `struct fasync_struct` | 드라이버가 가진 리스트 | `fasync_helper`로 추가·삭제, `kill_fasync`가 순회 |
| `struct eventfd_ctx` | 유저가 만든 eventfd | `eventfd_ctx_fdget`으로 참조, `eventfd_signal`로 `count++` + wake |
| `kernfs_open_node.poll` / `.event` | sysfs 속성 파일 | `kernfs_notify`가 event++ 후 wake, 읽으면 동기화 |

드라이버를 쓸 때 다시 만나는 지점:

- **F3**: `.poll`의 wait queue와 blocking `read`의 wait queue는 같은 것이다. lost-wakeup 순서 규칙도 같다.
- **F6/F7**: IRQ 핸들러(또는 threaded IRQ)에서 `wake_up_interruptible_poll`을 부른다. 깨우기 자체는 hardirq에서 안전하지만, 데이터를 하드웨어에서 읽어 오는 일(I2C/SPI 전송)은 threaded IRQ로 미룬다.
- **G4/G7**: `file_operations`에 `.poll`과 `.fasync`를 넣는 것이 문자 장치의 "이벤트 지원"이다.
- **G8**: sysfs 속성의 poll 지원은 `sysfs_notify` 한 줄 + ABI 문서.
- **G9**: hot-unplug 때의 HUP 처리와 kref 수명 관리.
- **H(서브시스템)**: input(`evdev_poll`), IIO buffer, V4L2(`vb2_poll` — 버퍼 큐 상태를 `EPOLLIN`/`EPOLLPRI`(이벤트)로 보고), tty, 소켓은 모두 이 노트의 계약을 서브시스템이 대신 구현해 준 것이다. 서브시스템 드라이버를 쓰면 `.poll`을 직접 짤 일이 줄지만, "언제 깨우나"는 여전히 드라이버 몫이다(예: IIO는 `iio_push_to_buffers()`가 깨운다).

---

## 14. Embedded 실무에서 다시 보기

### 14.1 IMU: FIFO watermark + 모션 인터럽트를 두 비트로

예를 들어 센서 키트의 SPI IMU(예: Bosch BMI088, TDK ICM-42688 계열)는 보통 INT 핀이 둘이다. INT1은 FIFO watermark(샘플 N개 쌓임), INT2는 any-motion·tap 같은 이벤트. 드라이버 설계는 이 노트 그대로 나온다. INT1 threaded IRQ가 SPI로 FIFO를 읽어 링버퍼에 넣고 `wake_up_interruptible_poll(&rq, EPOLLIN | EPOLLRDNORM)`. INT2는 이벤트 비트를 세우고 `wake_up_interruptible_poll(&eq, EPOLLPRI)`. 유저 데몬은 epoll 하나로 `EPOLLIN`(데이터 처리)과 `EPOLLPRI`(이벤트 ioctl로 읽고 ack)를 구분해 처리한다. 다만 실제 제품에서는 이런 IMU를 직접 문자 장치로 만들기보다 **IIO 서브시스템**(buffer + event 인터페이스)에 올리는 경우가 많다 — IIO가 바로 이 구조(데이터 버퍼 fd, 이벤트 fd)를 표준화해 놓았다.

### 14.2 FPGA/코프로세서 "작업 완료" 통지: eventfd

센서 융합을 FPGA나 DSP가 하고, 드라이버는 작업 완료 IRQ를 받는다고 하자. 처리 결과를 쓰는 프로세스가 둘(기록 데몬, 제어 루프)이면 eventfd 두 개를 드라이버에 등록시키고 IRQ에서 둘 다 `eventfd_signal`하는 설계가 깔끔하다. 장치 fd를 두 프로세스가 공유하며 서로의 데이터를 가져가는 경쟁이 없다. VFIO로 FPGA를 유저스페이스 드라이버로 다루는 경우는 이미 이 모양이다.

### 14.3 장치가 빠지는 장비: USB 카메라·USB IMU

드론의 USB 카메라가 진동으로 순간 끊겼다가 다시 붙으면, 커널 쪽에서는 `disconnect` → (새) `probe`가 일어난다. 데몬이 옛 fd를 epoll에 넣고 자고 있었다면, 드라이버(또는 V4L2/USB 서브시스템)가 `EPOLLHUP | EPOLLERR`로 깨워 줘야 데몬이 fd를 닫고 udev 이벤트를 기다려 새 장치를 연다(G9). HUP 처리가 없는 드라이버에서는 "카메라 재연결 후 영상이 안 나오는데 데몬은 살아 있다"는 증상이 된다.

### 14.4 현장 진단 순서 — "데몬이 이벤트를 못 받는다"

1. `cat /proc/<pid>/wchan`, `cat /proc/<pid>/stack` — `do_epoll_wait`/`do_sys_poll`에서 자고 있나 (F3 §3).
2. `cat /proc/<pid>/fdinfo/<epfd>` — epoll에 등록된 fd와 관심 이벤트(`events:` 마스크) 확인. ET(`0x80000000`대 비트)인지 본다.
3. 장치 상태를 직접 확인 — 드라이버 debugfs/sysfs에 FIFO 수위가 있으면 본다. "데이터는 쌓이는데 안 깬다" = ET + 전이 때만 깨우는 드라이버(§4) 또는 깨우기 누락.
4. ftrace로 `ep_poll_callback`/`pollwake`가 불리는지 본다(§11). 안 불리면 드라이버가 안 깨우는 것, 불리는데 안 깨면 key가 관심 이벤트와 안 맞는 것(§5).

---

## 15. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `.poll`에서 데이터를 기다리며 잠듦 | 다른 fd까지 응답이 늦어짐, poll timeout 무시 | `poll_wait`을 "기다리기"로 오해 | 등록만 하고 즉시 마스크 리턴 |
| 상태를 검사한 뒤 `poll_wait` | 가끔 이벤트 하나를 놓치고 다음 이벤트까지 지연 | 검사와 등록 사이의 wake_up이 허공으로 | `poll_wait` 먼저, 검사는 그 뒤 |
| 빈→찬 전이 때만 wake | ET epoll 사용자가 영원히 멈춤 (예제 3) | ET는 새 콜백이 와야 다시 보고 | 새 데이터마다 wake, `kill_fasync`도 매번 |
| `wake_up_interruptible()`(key 없음) 남용 | 이벤트만 기다리는 스레드가 데이터마다 깸, 전력 낭비 (예제 4) | 콜백이 거를 정보가 없음 | `wake_up_interruptible_poll(&q, 비트)` |
| `.poll`에 일부 queue만 등록 | 특정 조건(쓰기 가능, 이벤트) 변화로는 안 깸 | 등록 안 된 queue를 깨움 | 마스크에 영향 주는 queue 전부 `poll_wait` |
| `EPOLLPRI`를 끌 방법 없음 | LT 사용자가 100% CPU로 루프 | 조건이 계속 참 | ack 경로(ioctl/read/sysfs) 설계 |
| remove에서 깨우지 않음 | 장치 제거 후 데몬이 영원히 잠 | 아무도 wake 안 함 | dead 세우고 HUP key로 전원(`nr_exclusive=0`) 깨움 |
| remove에서 구조체 `kfree` | 열린 파일이 남은 상태에서 oops/KASAN UAF | wait queue에 epoll 엔트리가 매달려 있음 | kref, 마지막 release에서 해제 |
| IRQ에서 `sysfs_notify()` | "sleeping function called from invalid context" 경고 | 이름 검색이 rwsem을 잡음 | probe에서 `sysfs_get_dirent`, IRQ에서 `sysfs_notify_dirent` |
| 6.8+에서 `eventfd_signal(ctx, 1)` | 컴파일 에러 "too many arguments" | API가 `eventfd_signal(ctx)`로 바뀜 | 인자 제거, 버전 분기면 `LINUX_VERSION_CODE` |

---

## 16. 면접에서 이렇게 말한다

**Q.** Implement poll for your character driver. Walk me through it.

**A.** `file_operations`에 `.poll`을 넣는다. 함수는 두 단계다. 먼저 마스크에 영향을 주는 wait queue 전부에 `poll_wait()`를 부른다 — 이건 잠드는 게 아니라 호출자의 엔트리를 등록하는 것이다. 그다음 락 아래서 상태를 보고 마스크를 만든다: 데이터가 있으면 `EPOLLIN | EPOLLRDNORM`, 쓸 자리가 있으면 `EPOLLOUT | EPOLLWRNORM`, 예외 이벤트면 `EPOLLPRI`, 장치가 사라졌으면 `EPOLLHUP | EPOLLERR`. 생산자 쪽(IRQ, write)은 상태를 바꾼 뒤 `wake_up_interruptible_poll(&q, 해당 비트)`를 부른다. 같은 queue를 blocking `read`도 쓰므로 select, poll, epoll, blocking I/O가 한 번에 지원된다.

> I add a poll method to file_operations, and it has two phases. First it calls poll_wait on every wait queue that can change the result — that doesn't sleep, it just registers the caller's entry on my queue. Then, under the lock, it computes the current mask: EPOLLIN and EPOLLRDNORM if the FIFO has data, EPOLLOUT and EPOLLWRNORM if there's room to write, EPOLLPRI for an exceptional event like a threshold interrupt or an overflow, and EPOLLHUP plus EPOLLERR once the device is gone. The producer side — the IRQ handler or write — updates the state and calls wake_up_interruptible_poll with the matching bits as the key. The blocking read sleeps on the same queue, so select, poll, epoll and blocking reads all work from that one function.

**꼬리 질문**: "Why must poll_wait come before checking the state?" → 반대 순서면 검사와 등록 사이에 온 `wake_up`이 아무도 깨우지 못해 이벤트 하나를 놓친다. `poll_wait` 뒤의 `smp_mb()`가 등록과 검사의 순서를 멀티코어에서 보장한다.

**Q.** Does poll_wait sleep? Where does the process actually sleep?

**A.** 아니다. `poll_wait`은 poll table의 `_qproc`를 불러 엔트리를 wait queue에 거는 것뿐이다 — select/poll이면 `__pollwait`, epoll이면 `ep_ptable_queue_proc`, 재확인 호출이면 `NULL`이라 아무것도 안 한다. 잠드는 곳은 VFS의 `do_select`/`do_poll`(`poll_schedule_timeout`)이나 epoll의 `ep_poll`이다. 깨어나면 드라이버의 `.poll`을 다시 불러 실제 상태를 확인한다.

> No. poll_wait just invokes the poll table's queueing callback, which adds an entry to my wait queue — __pollwait for select and poll, ep_ptable_queue_proc for epoll, and on re-polls the callback is NULL so it does nothing. The sleeping happens in the VFS — do_select or do_poll — or in ep_poll for epoll. After a wakeup they call my poll method again to read the real state; the wakeup itself is only a hint.

**Q.** What does edge-triggered epoll require from a driver?

**A.** 새 데이터가 들어올 때마다 깨워야 한다. 빈→찬 전이 때만 깨우면, 덜 읽은 ET 사용자는 epitem이 ready list에 다시 올라갈 계기가 없어서 데이터가 쌓여도 영원히 안 깬다. 제가 실험 드라이버로 재현했을 때 FIFO가 11개까지 차는 동안 `epoll_wait`이 계속 timeout이었다. 커널 pipe도 같은 이유로 "poll 사용자가 있으면 매번 깨운다"로 돌아갔다. `kill_fasync`도 마찬가지로 매번 보낸다.

> It has to wake on every new piece of data, not only on the empty-to-non-empty transition. Edge-triggered epoll only re-queues the item when the wait-queue callback fires, so if a user didn't drain completely and the driver skips the wakeup because the FIFO was already non-empty, that user never wakes again. I reproduced it: the FIFO kept filling and epoll_wait just timed out every round. The pipe code went through the same regression and now wakes on every write when there are pollers. The same applies to kill_fasync.

**Q.** How do you avoid waking threads that don't care about the event?

**A.** 깨울 때 key를 실어 보낸다 — `wake_up_interruptible_poll(&q, EPOLLIN)`. `pollwake`와 `ep_poll_callback`이 key를 등록 때의 관심 이벤트와 비교해 안 맞으면 그냥 돌아간다. 필요하면 읽기·쓰기·이벤트 queue를 분리한다. 제 측정에서 이벤트만 기다리는 poller가 key 없는 깨우기로는 데이터 100개에 `.poll`이 102번 불렸고, key를 붙이자 2번이 됐다. 여러 프로세스가 같은 fd를 각자 epoll로 기다리면 `EPOLLEXCLUSIVE`로 하나만 깨운다.

> Pass a key when waking — wake_up_interruptible_poll with the event bits. The poll and epoll wait-queue callbacks compare the key with what each waiter asked for and skip the ones that don't match. Separate queues for read, write and events help further. In my test an event-only poller re-polled 102 times for 100 data samples with unkeyed wakeups, and twice with keyed ones. For many epoll instances on the same fd there's EPOLLEXCLUSIVE, which wakes only one of them.

**Q.** What happens to processes polling your device when it's unplugged?

**A.** remove에서 IRQ를 멈추고 `dead`를 세운 뒤, 모든 queue를 `EPOLLHUP | EPOLLERR` key와 `nr_exclusive = 0`으로 깨우고(배타적 대기자까지 전원) `kill_fasync(POLL_HUP)`을 보낸다. 이후 `.poll`은 HUP|ERR, `read`/`write`는 `-ENODEV`. 그리고 중요한 것: 장치 구조체를 remove에서 해제하지 않는다. epoll 엔트리가 내 wait queue 안에 매달려 있으므로, kref로 마지막 `release`까지 살려 둔다.

> In remove I stop the interrupt source, set a dead flag under the lock, wake every queue with EPOLLHUP and EPOLLERR as the key and nr_exclusive zero so even exclusive waiters get it, and send kill_fasync with POLL_HUP. From then on poll returns HUP and ERR, and read and write return ENODEV. The key point is lifetime: epoll entries live inside my wait queue heads, so I can't free the device struct in remove. It's reference-counted with a kref — probe holds one, each open holds one — and it's freed on the last release.

**Q.** fasync/SIGIO vs eventfd — when would you use each?

**A.** 새 설계에서 SIGIO는 피한다. 표준 시그널은 합쳐져서 횟수와 fd 정보를 잃고(제 실험에서 52번 알림이 핸들러 1번), 핸들러 제약과 EINTR 문제가 있다. 그래도 레거시 호환 때문에 `.fasync`는 구현해 둔다. eventfd는 유저가 만든 eventfd를 ioctl로 넘기면 드라이버가 `eventfd_ctx_fdget`으로 잡고 `eventfd_signal`로 카운터를 올리는 방식이다. 횟수가 보존되고, 이벤트 루프에 자연스럽게 들어가고, 다른 프로세스에 넘길 수 있다. VFIO와 KVM irqfd가 이 모양이다. 6.8부터 `eventfd_signal`은 인자가 ctx 하나다.

> I'd avoid SIGIO in new designs: standard signals coalesce, so you lose the count and which fd fired — in my test 52 notifications turned into one handler call — plus handler restrictions and EINTR everywhere. I still implement fasync for compatibility. For a modern notification path, the user creates an eventfd and passes it via ioctl; the driver takes a context reference with eventfd_ctx_fdget and calls eventfd_signal from the interrupt path. The count is preserved, it fits into any epoll loop, and the fd can be handed to another process. That's how VFIO delivers interrupts and how KVM irqfd works. Note that since 6.8 eventfd_signal takes only the context.

**Q.** How can user space wait for a change in a sysfs attribute?

**A.** 드라이버가 값이 바뀔 때 `sysfs_notify()`를 부르면, 그 속성 파일을 poll하는 쪽이 `POLLPRI | POLLERR`로 깬다. 유저는 처음에 한 번 읽고, 깨어날 때마다 offset 0부터 다시 읽어야 한다. 알림은 합쳐지므로 속성은 누적 카운터나 현재 상태여야 한다. IRQ 문맥에서는 `sysfs_notify()`가 잠들 수 있어 안 되고, 미리 찾아 둔 노드로 `sysfs_notify_dirent()`를 부른다.

> The driver calls sysfs_notify when the value changes, and a process polling that attribute file wakes with POLLPRI and POLLERR. User space must read the file once before polling and re-read from offset zero after every wakeup. Notifications coalesce, so the attribute should be a counter or a current state, not a last-event value. sysfs_notify can sleep, so from interrupt context I look up the node once at probe with sysfs_get_dirent and call sysfs_notify_dirent instead.

**Q.** select, poll and epoll — what does the driver have to do differently for each?

**A.** 아무것도 다르지 않다. 셋 다 `vfs_poll()`을 통해 같은 `.poll`을 부른다. 차이는 호출 패턴이다. select/poll은 매 호출마다 모든 fd의 `.poll`을 부르고 엔트리를 걸었다가 syscall이 끝나면 떼고, epoll은 ADD 때 한 번 등록해 두고 깨어난 fd만 다시 `.poll`한다. 그래서 드라이버의 `.poll`은 싸야 하고(자주 불린다), 등록과 상태 검사 외에 부작용이 없어야 한다.

> Nothing. All three reach the same poll method through vfs_poll. The difference is the calling pattern: select and poll call every fd's poll method on every syscall and tear the entries down at the end, while epoll registers once at EPOLL_CTL_ADD and only re-polls the fds that were woken. So the driver's poll has to be cheap and side-effect free — register, read the state, return.

---

## 17. 직접 해보기

1. **개념**: 드라이버가 `.poll`을 구현하지 않으면 select/poll/epoll은 그 fd를 어떻게 보는가? 데몬에서 어떤 증상이 나오는가?
   정답/힌트: `vfs_poll()`이 `DEFAULT_POLLMASK`(IN|OUT|RDNORM|WRNORM)를 돌려주므로 항상 준비됨으로 보인다. 이벤트 루프가 계속 깨어 `read()`에서 막히거나(blocking fd) `EAGAIN`으로 100% CPU를 쓴다.
2. **개념**: 예제 4에서 `qmode=1`인데 blocking `read()` 대기자와 `POLLPRI` poller가 같은 `rq`에 있다. 모션 이벤트가 오면 `read()` 대기자는 깨는가? 깬다면 무엇을 하나?
   정답/힌트: `qmode=1`에서는 이벤트도 `rq`를 깨운다. `read`의 엔트리(`autoremove_wake_function`)는 key를 보지 않으므로 깨고, `wait_event` 조건(FIFO 비지 않음)을 다시 검사한다. 모션 샘플 자체가 FIFO에 들어가므로 이 경우엔 읽을 것이 있다.
3. **코드**: `g6_poll`에 `poll_requested_events(pt)`를 써서 `EPOLLOUT`이 요청되지 않았으면 `cmd` FIFO 검사를 생략해 보라. 예제 2의 출력이 바뀌는가? 왜 등록(`poll_wait(&wq)`)은 생략하면 안 되나?
   정답/힌트: 출력은 요청한 비트만 보이므로 같다. 등록을 빼면 같은 epoll 엔트리가 `EPOLL_CTL_MOD`로 `EPOLLOUT`을 추가했을 때 `wq` 깨우기를 받을 엔트리가 없다(MOD는 재등록하지 않는다).
4. **코드**: `g6_remove`의 `__wake_up(…, 0, …)`을 `wake_up_interruptible_poll(&d->rq, EPOLLHUP | EPOLLERR)`로 바꾸고, 예제 5처럼 스레드 4개가 `EPOLLEXCLUSIVE`로 기다리는 중에 unbind하라. 몇 명이 HUP을 받는가?
   정답/힌트: 한 명. 나머지는 timeout까지 잔다(timeout이 없으면 영원히). 그래서 제거 경로는 `nr_exclusive = 0`.
5. **코드**: `G6_IOC_SET_EVENTFD`를 확장해 eventfd를 두 개(데이터용, 이벤트용) 등록할 수 있게 하고, 이벤트용은 `G6_EV_*`가 생길 때만 울리게 하라. 유저는 두 eventfd를 epoll에 넣어 구분한다.
   정답/힌트: `struct eventfd_ctx *efd_data, *efd_ev;` 두 포인터, ioctl 번호 두 개(`_IOW('g', 3, __s32)`), 해제는 `eventfd_ctx_put` 두 번.
6. **관찰**: 예제 3의 ET 멈춤 상태에서 `/proc/<pid>/fdinfo/<epfd>`를 읽어 `events:` 값에 ET 비트가 있는지, 그리고 ftrace로 `ep_poll_callback`이 더 이상 불리지 않는다는 것을 확인하라.
   정답/힌트: `events: 80000019` 형태 — EPOLLET(`1u << 31`) + EPOLLIN(0x1) + EPOLLERR(0x8) + EPOLLHUP(0x10). ERR/HUP은 `epoll_ctl`이 ADD/MOD 때 자동으로 더한다(`epds->events |= EPOLLERR | EPOLLHUP`). `wmode=0`이면 FIFO가 비어 있지 않은 동안 `g6_push`가 `g6_wake`를 건너뛰므로 `ep_poll_callback` 줄이 안 찍힌다.
7. **관찰**: 예제 12를 `-smp 1`로 다시 돌려 보라. 무엇이 달라지고 왜 그런가?
   정답/힌트: 직접 해 보고 기록할 것(이 노트에서는 실행하지 않았다). 예상: hrtimer IRQ와 깨어날 태스크가 같은 CPU라 선점 경로(`need_resched` → IRQ 리턴 시 스케줄)로 바로 전환되므로 최솟값은 비슷하거나 줄고, 다른 일이 겹치면 꼬리가 늘 수 있다.

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| `.poll` | `file_operations`의 poll 메서드 | 등록(`poll_wait`) + 현재 상태 마스크 리턴, 절대 잠들지 않음 |
| `poll_wait()` | 등록 함수 | `pt->_qproc`로 호출자의 엔트리를 wait queue에 건다. 이름과 달리 기다리지 않는다 |
| `poll_table` | poll 테이블 | `_qproc`(등록 방법)와 `_key`(요청 이벤트)를 담은 작은 구조체 |
| `__poll_t` | poll 마스크 타입 | `__bitwise unsigned`, sparse가 `EPOLL*` 아닌 값 섞기를 잡는다 |
| `vfs_poll()` | VFS 공통 진입점 | select·poll·epoll이 모두 이걸로 `.poll`을 부른다 |
| `EPOLLPRI` | 우선·예외 이벤트 | 일반 데이터와 따로 처리할 사건. select에서는 exceptfds |
| `EPOLLHUP` / `EPOLLERR` | 끊김 / 에러 | 요청하지 않아도 항상 보고. 장치 제거 표시 |
| wake key | 깨우기 key | `wake_up_*_poll(q, 비트)`로 실어 보내는 "무엇이 바뀌었나" |
| `pollwake` | select/poll 엔트리 콜백 | key가 관심과 맞으면 잠든 태스크를 깨운다 |
| `ep_poll_callback` | epoll 엔트리 콜백 | epitem을 ready list에 넣고 `epoll_wait`을 깨운다 |
| level / edge triggered | LT / ET | LT는 준비된 동안 계속 보고, ET는 새 콜백(새 wake_up)이 올 때만 보고 |
| `EPOLLEXCLUSIVE` | 배타적 epoll 등록 | 같은 fd를 여러 epoll이 기다릴 때 하나만 깨움 (4.5+) |
| thundering herd | 우르르 깨어남 | 이벤트 하나에 대기자 전원이 깨고 한 명만 일함 |
| fasync | 비동기 시그널 통지 | `O_ASYNC` + `F_SETOWN`, 드라이버는 `fasync_helper` + `kill_fasync` |
| `F_SETSIG` | SIGIO 대체 시그널 | 실시간 시그널 + `siginfo`(fd, 이유, band) |
| `eventfd_signal()` | 커널발 eventfd 알림 | ctx의 카운터 +1, 대기자 깨움. 6.8+는 인자 ctx 하나 |
| `eventfd_ctx_fdget()` | eventfd 참조 잡기 | fd 번호로 ctx 객체 참조, `eventfd_ctx_put`으로 놓음 |
| `sysfs_notify()` | sysfs 속성 변경 알림 | 속성 파일 poller를 `POLLPRI`, `POLLERR`로 깨움 |
| `wake_up_pollfree()` | wait queue 소멸 알림 | 파일보다 먼저 사라지는 wait queue용, `POLLFREE` key |
| kref | 참조 카운터 | 장치 구조체를 마지막 사용자까지 살려 두는 표준 도구 |

---

## 19. 요약 & 체크리스트

드라이버의 `.poll`은 "기다리기"가 아니라 **"기다릴 장소를 알려 주고(`poll_wait`), 지금 상태를 비트로 말하기"** 두 단계이고, select·poll·epoll은 모두 `vfs_poll()`로 이 한 함수에 들어온다. 잠드는 쪽은 VFS와 epoll이고, 드라이버의 일은 상태가 바뀌는 모든 순간에 **올바른 queue를 올바른 key로 깨우는 것**이다. 새 데이터마다 깨워야 edge-triggered 사용자가 멈추지 않고, key를 실어야 관심 없는 대기자가 헛깨지 않으며, 장치가 사라질 때는 `EPOLLHUP | EPOLLERR`로 전원(`nr_exclusive = 0`)을 깨우되 wait queue를 품은 구조체는 kref로 마지막 `release`까지 살려 둔다. 시그널 통지(fasync/SIGIO)는 합쳐지고 끼어드는 약한 API라 호환용으로만 두고, 다른 프로세스·서브시스템에 알릴 때는 커널발 eventfd(`eventfd_ctx_fdget` + `eventfd_signal`, 6.8+ 인자 하나), 드물게 바뀌는 속성에는 `sysfs_notify`(IRQ에서는 `sysfs_notify_dirent`)를 쓴다.

- [ ] `.poll`의 두 단계(등록 → 검사)와 "절대 잠들지 않는다"를 그림으로 설명할 수 있다
- [ ] select/poll/epoll이 각각 어떤 경로로 `vfs_poll()`에 오고, `_qproc`가 무엇인지 말할 수 있다
- [ ] 장치 상태를 `EPOLLIN|RDNORM`, `EPOLLOUT|WRNORM`, `EPOLLPRI`, `EPOLLHUP|ERR`로 설계하고 select의 r/w/x 집합에 어떻게 접히는지 설명할 수 있다
- [ ] `EPOLLPRI` 이벤트에 ack 경로가 필요한 이유를 설명할 수 있다
- [ ] ET epoll 사용자를 위해 "새 데이터마다 깨우기"가 필요한 이유를 pipe 사례와 함께 설명할 수 있다
- [ ] `wake_up_interruptible_poll(&q, key)`가 `pollwake`/`ep_poll_callback`에서 어떻게 걸러지는지 말할 수 있다
- [ ] `EPOLLEXCLUSIVE`의 동작과, 제거 경로에서 `nr_exclusive = 0`이 필요한 이유를 설명할 수 있다
- [ ] `.fasync` + `fasync_helper` + `kill_fasync`를 구현하고, 시그널 통지의 약점 다섯 가지를 말할 수 있다
- [ ] eventfd를 ioctl로 받아 `eventfd_signal`로 알리는 드라이버 코드를 짤 수 있다 (6.8 API 변화 포함)
- [ ] `sysfs_notify`와 `sysfs_notify_dirent`의 문맥 차이, 유저 쪽 "먼저 읽고 깨면 다시 읽기" 규칙을 설명할 수 있다
- [ ] hot-unplug 때 잠든 read/poll/epoll을 모두 깨우고 kref로 구조체 수명을 관리하는 remove를 짤 수 있다
- [ ] ftrace(`func_stack_trace`, `function_graph`)로 `.poll` 호출자와 깨우기 경로를 확인할 수 있다

## 참고 자료

- man7.org: [poll(2)](https://man7.org/linux/man-pages/man2/poll.2.html), [select(2)](https://man7.org/linux/man-pages/man2/select.2.html), [epoll(7)](https://man7.org/linux/man-pages/man7/epoll.7.html), [epoll_ctl(2)](https://man7.org/linux/man-pages/man2/epoll_ctl.2.html) (`EPOLLEXCLUSIVE`, `EPOLLET`), [fcntl(2)](https://man7.org/linux/man-pages/man2/fcntl.2.html) (`F_SETOWN`, `F_SETSIG`, `O_ASYNC`), [eventfd(2)](https://man7.org/linux/man-pages/man2/eventfd.2.html)
- Michael Kerrisk, "The Linux Programming Interface", 63장 "Alternative I/O Models" (signal-driven I/O, epoll, ET 주의점)
- Jonathan Corbet, Alessandro Rubini, Greg Kroah-Hartman, "Linux Device Drivers, 3rd ed." 6장 "Advanced Char Driver Operations" — poll and select, Asynchronous Notification — [lwn.net/Kernel/LDD3](https://lwn.net/Kernel/LDD3/) (마스크 타입이 `unsigned int`/`POLLIN`인 옛 API, `release`에서 fasync 해제 등 지금과 다른 부분이 있다)
- 커널 문서: [docs.kernel.org — sysfs](https://docs.kernel.org/filesystems/sysfs.html), [docs.kernel.org — VFIO](https://docs.kernel.org/driver-api/vfio.html)
- 커널 소스 (인용은 랩의 6.18.55, 온라인으로는 [elixir.bootlin.com](https://elixir.bootlin.com/linux/latest/source)에서 같은 파일을 볼 수 있다 — 버전에 따라 줄 번호가 다르다): `include/linux/poll.h` (`poll_wait`, `vfs_poll`), `fs/select.c` (`__pollwait`, `pollwake`, `do_pollfd`, `POLLIN_SET`), `fs/eventpoll.c` (`ep_ptable_queue_proc`, `ep_poll_callback`, `ep_send_events`), `kernel/sched/wait.c` (`__wake_up_common`), `include/linux/wait.h` (`wake_up_interruptible_poll`, `wake_up_pollfree`), `fs/fcntl.c` (`fasync_helper`, `kill_fasync`, `send_sigio_to_task`), `fs/eventfd.c`, `fs/kernfs/file.c` (`kernfs_notify`, `kernfs_generic_poll`), `fs/pipe.c` (`pipe_write`의 epoll 주석), `drivers/input/evdev.c` (`evdev_poll`)
- 이 노트와 이어지는 노트: A4(유저 쪽 select/poll/epoll, ET 함정, 확장성), A6(시그널), F3(wait queue, 기본 `.poll`), F6/F7(IRQ와 지연 작업), G4/G7(문자 장치, misc), G8(sysfs), G9(핫플러그)
