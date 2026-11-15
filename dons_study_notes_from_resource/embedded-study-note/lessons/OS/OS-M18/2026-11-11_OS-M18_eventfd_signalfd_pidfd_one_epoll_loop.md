---
id: OS-M18
time: 90분
updated: 2026-11-11
---
# OS-M18 알림용 fd IPC: eventfd·signalfd·pidfd를 epoll 하나로

## 학습 목표

- eventfd 의 **카운터 의미**(write = 더하기, read = 읽고 0 으로, `EFD_SEMAPHORE` = 1 씩, 최대값 `0xfffffffffffffffe`, `EFD_NONBLOCK` 의 `EAGAIN`)를 테스트로 확인하고, 작업 스레드 → 이벤트 루프 완료 알림을 "큐에 push → eventfd 에 +1" 순서로 짠다.
- signalfd 가 시그널을 **훔치지 않는다**는 것 — 모든 스레드에서 먼저 막아야(pthread_sigmask) pending 에 남고 그래야 읽힌다 — 를 설명하고, 막기 전에 만든 스레드 하나 때문에 SIGTERM 에 프로세스가 죽는 버그를 재현한다.
- pidfd_open + `waitid(P_PIDFD)` + `pidfd_send_signal` 로 자식을 **pid 재사용 경쟁 없이** 감시·회수·종료하고, 자식 5 명이 동시에 죽을 때 SIGCHLD 는 합쳐지지만 pidfd 는 정확히 5 개임을 결정적으로 보인다.
- 세 fd 를 epoll 하나에 꽂은 데몬 뼈대에 SIGTERM → 작업 drain → 워커 join → 자식 TERM → 유예 후 KILL → fd·마스크 원상복구까지의 **깔끔한 종료**를 구현한다.
- OS-B07 의 self-pipe 와 코드량·깨움 지연을 실측으로 비교하고, 어느 쪽을 언제 쓰는지 말할 수 있다.

## 1. 문제

임베디드 리눅스 데몬(센서 수집기, 펌웨어 업데이트 에이전트, 게이트웨이 프로세스 감시자)은 거의 다 **"epoll 하나 + 워커 몇 개 + 자식 몇 개 + SIGTERM 으로 정지"** 모양이고, 면접에서는 "다른 스레드가 epoll 루프를 어떻게 깨우나요?", "멀티스레드 프로그램에서 시그널은 어떻게 받나요?", "자식 여러 개가 동시에 죽으면?" 으로 바로 파고든다. 리눅스는 이 셋에 각각 **데이터 없이 '일어났다' 만 전달하는 fd** 를 준다: eventfd(스레드·커널 → 루프), signalfd(시그널 → 루프), pidfd(자식 종료 → 루프). OS-B07 에서 시그널을 self-pipe 로 fd 로 바꾸는 트릭을 손으로 했다면, 여기서는 커널이 그 일을 대신하는 정식 도구를 쓴다. epoll 자체(LT/ET, ONESHOT)는 OS-M13, kqueue 로 같은 데몬을 짜는 맥 쪽 종합은 OS-A11 이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">epoll 하나 = 알림 fd 세 종류 (data.u64 = 종류 &lt;&lt; 32 | 번호)</text>
  <rect x="20" y="40" width="190" height="70" class="f-ok" stroke="currentColor"/>
  <text x="30" y="60" fill="currentColor">워커 스레드 ×4</text>
  <text x="30" y="80" fill="currentColor" font-family="monospace" font-size="12">push(완료큐); write(+1)</text>
  <text x="30" y="98" fill="currentColor" font-size="12">완료 3 번 → 카운터 3</text>
  <rect x="20" y="130" width="190" height="70" class="f-warn" stroke="currentColor"/>
  <text x="30" y="150" fill="currentColor">커널: SIGTERM / SIGCHLD</text>
  <text x="30" y="170" fill="currentColor" font-size="12">모든 스레드가 막음 → pending</text>
  <text x="30" y="188" fill="currentColor" font-size="12">표준 시그널은 종류당 1 개</text>
  <rect x="20" y="220" width="190" height="70" class="f-mut" stroke="currentColor"/>
  <text x="30" y="240" fill="currentColor">자식 프로세스 ×N</text>
  <text x="30" y="260" fill="currentColor" font-size="12">자식마다 pidfd 하나</text>
  <text x="30" y="278" fill="currentColor" font-size="12">종료 → 그 fd 만 EPOLLIN</text>
  <rect x="250" y="40" width="120" height="250" fill="none" stroke="currentColor"/>
  <text x="262" y="75" fill="currentColor" font-family="monospace" font-size="12">eventfd</text>
  <text x="262" y="165" fill="currentColor" font-family="monospace" font-size="12">signalfd</text>
  <text x="262" y="255" fill="currentColor" font-family="monospace" font-size="12">pidfd[0..N)</text>
  <path d="M210 75 L250 75 M210 165 L250 165 M210 255 L250 255" stroke="currentColor"/>
  <rect x="410" y="120" width="120" height="90" class="f-acc" stroke="currentColor"/>
  <text x="430" y="160" fill="currentColor" font-weight="bold">epoll_wait</text>
  <text x="425" y="182" fill="currentColor" font-size="12">메인 스레드 1 개</text>
  <path d="M370 75 L410 140 M370 165 L410 165 M370 255 L410 190" stroke="currentColor"/>
  <rect x="570" y="40" width="160" height="250" fill="none" stroke="currentColor"/>
  <text x="580" y="70" fill="currentColor" font-size="12">TAG_EVENT:</text>
  <text x="580" y="88" fill="currentColor" font-size="12">read → 합, 완료큐 비우기</text>
  <text x="580" y="140" fill="currentColor" font-size="12">TAG_SIGNAL:</text>
  <text x="580" y="158" fill="currentColor" font-size="12">siginfo 읽기 → 종료 시작</text>
  <text x="580" y="210" fill="currentColor" font-size="12">TAG_CHILD(i):</text>
  <text x="580" y="228" fill="currentColor" font-size="12">waitid(P_PIDFD) → close</text>
  <path d="M530 165 L570 165" stroke="currentColor"/>
  <text x="10" y="318" fill="currentColor">셋 다 "일어났다" 만 준다 — 데이터(결과·종료 코드)는 큐·waitid 로 따로.</text>
</svg>
```

**환경 (이 레슨의 모든 리눅스 출력)**: Lima VM `elinux` (vz, aarch64, 6 vCPU) · Ubuntu 26.04.1 LTS · kernel `7.0.0-38-generic` · glibc 2.43 (`ldd --version`) · `gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0` · `Ubuntu clang version 21.1.8` · strace 6.19 · man-pages 6.17. 호스트 Apple M6, 측정 중 맥 load average 15–17 (다른 세션들이 동시에 빌드·벤치). VM vCPU 는 물리 코어가 아니므로 지연 숫자는 **비율**만 믿는다.

### Part A — eventfd: 카운터 의미 + 작업 완료 알림

- `ev_post(efd, n)` / `ev_take(efd, &v)` — 정확히 8 바이트 write/read, `0` 또는 `-errno`, `EINTR` 은 다시.
- 테스트: 3 + 4 를 쓰고 한 번 읽으면 7, 다시 읽으면 `-EAGAIN`. `UINT64_MAX` 쓰기는 `-EINVAL`, `UINT64_MAX-1` 까지는 되고 거기서 +1 은 `-EAGAIN`. 4 바이트 버퍼 read 는 `EINVAL`. `EFD_SEMAPHORE` 초기값 3 → 1,1,1 → `EAGAIN`, 2 를 더해도 한 번에 1. epoll LT: 0 이면 조용, >0 이면 `EPOLLIN`, 읽으면 다시 조용.
- `pool_complete` (워커 쪽: 완료 큐에 push → +1) / `pool_collect` (메인: epoll → 카운터 합산 → 큐 비우기, **카운터 합 == 작업 수** 가 될 때까지). 워커 4, 작업 800: 결과는 정확히 한 번씩, 카운터 합 800.

### Part B — signalfd 와 pidfd

- `sigfd_open(sigs, n, &old)` — 막고(pthread_sigmask) 그 집합의 signalfd(`SFD_NONBLOCK|SFD_CLOEXEC`). `sigfd_drain` — `EAGAIN` 까지 `struct signalfd_siginfo` 를 읽어 레코드 수.
- `watch_child(epfd, pid, tag)` — `pidfd_open` + epoll 등록. `pidfd_reap(pidfd, &code, &sig)` — `waitid(P_PIDFD, …, WEXITED)`.
- 테스트: SIGUSR1 두 번 → 레코드 1 개(합쳐짐, `ssi_pid` = 나), SIGRTMIN `sigqueue` 세 번 → 레코드 3 개(값 1,2,3 순서). 자식 프로세스 안에서 "막고 → 스레드" 는 signalfd 로 받아 exit 0, "스레드 → 막고" 는 **SIGTERM 에 죽는다**. 자식 5 명이 같은 순간 종료 → pidfd 준비 정확히 5, 각자의 종료 코드 10+i 회수, SIGCHLD 레코드는 5 보다 적다.

### Part C — 데몬 뼈대와 깔끔한 종료

- `daemon_init` — ① SIGTERM·SIGINT·SIGCHLD 막기 + signalfd ② eventfd ③ epoll 등록 ④ **그 다음에** 워커 4 개.
- `daemon_run` — 루프 하나로 `TAG_EVENT / TAG_SIGNAL / TAG_CHILD` 분배. SIGTERM 이면 `d_begin_shutdown`: 새 작업 거부, 워커 깨우기, 살아 있는 자식에 `pidfd_send_signal(SIGTERM)`, 300 ms 유예 뒤 남은 자식은 SIGKILL. 작업이 다 끝나면 워커 join, 자식이 다 회수되면 반환.
- 테스트: 자식 4 (TERM 에 죽음 / TERM 무시 / exit 7 / exit 9), 작업 300 을 넣고 자기에게 SIGTERM → 300 개 모두 완료·합계 일치, 이미 끝난 자식은 종료 코드 그대로, TERM 무시 자식은 SIGKILL, 종료 후 fd 수·시그널 마스크가 처음과 같다.

## 2. Problem (English)

> Build a Linux event-loop skeleton where a single `epoll` instance multiplexes three notification fds.
>
> ```c
> int  ev_post(int efd, uint64_t n);                    // eventfd add; 0 or -errno (-EINVAL for UINT64_MAX, -EAGAIN on overflow)
> int  ev_take(int efd, uint64_t *out);                 // eventfd read; sum (or 1 with EFD_SEMAPHORE); -EAGAIN if zero
> int  sigfd_open(const int *sigs, int n, sigset_t *oldmask);   // block, then signalfd(SFD_NONBLOCK|SFD_CLOEXEC)
> int  sigfd_drain(int sfd, sigrec_t *out, int max);    // read signalfd_siginfo records until EAGAIN, return count
> int  watch_child(int epfd, pid_t pid, uint64_t tag);  // pidfd_open + EPOLL_CTL_ADD
> int  pidfd_reap(int pidfd, int *code, int *sig);      // waitid(P_PIDFD)
> int  daemon_init(daemon_t *d, int nworkers);
> int  daemon_run(daemon_t *d, int grace_ms, int max_ms);
> ```
>
> Worker threads report completion by pushing a result and then incrementing an eventfd. On SIGTERM the daemon
> stops accepting jobs, finishes queued jobs, joins workers, sends SIGTERM to children through their pidfds, escalates
> to SIGKILL after `grace_ms`, reaps everything and restores the fd table and signal mask.
> Example: two writes of 3 and 4 followed by one read return 7. Five children exiting together produce exactly five
> readable pidfds but fewer than five SIGCHLD records.
> Follow-ups: Why must the signals be blocked in *every* thread? Why push before post? Why is `pidfd_send_signal`
> safer than `kill(pid)`? When would you still use a self-pipe? What does a forked child inherit from the daemon?

## 3. 시작 코드

<!-- code: starter.c -->

**리눅스 전용** (`/* ENV: linux */`, 둘째 줄 `/* BUILD: -pthread */` — 맥 검증기는 건너뛴다). 맥 홈은 VM 에 읽기 전용으로 마운트되므로 VM 의 `/tmp/OS-M18/` 로 복사해서 빌드한다.

```bash
limactl list                              # elinux 가 Running 인지
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/OS/OS-M18
limactl shell elinux -- bash -lc "mkdir -p /tmp/OS-M18 && cp $L/* /tmp/OS-M18/ && cd /tmp/OS-M18 && \
  gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st"
```

`eventfd`·`signalfd`·`pidfd_open`·`P_PIDFD`·`sigqueue` 는 `-std=c11` 에서 `#define _GNU_SOURCE` 를 include 앞에 둬야 보인다. 시작 코드 실행 결과(실제, 끝부분 — 0.01 s 에 끝난다, 대기마다 상한이 있어 멈추지 않는다):

```text
== Part B: 마스크는 스레드마다 — 막기 전에 만든 스레드가 SIGTERM 을 받는다 ==
FAIL  st_ok >= 0 && WIFEXITED(st_ok) && WEXITSTATUS(st_ok) == 0  (line 484)
FAIL  st_bug >= 0 && WIFSIGNALED(st_bug) && WTERMSIG(st_bug) == SIGTERM  (line 485)
  info: block→thread: exit | thread→block: exit 6
...
FAIL  d->alive == 0 && d->workers_joined  (line 575)
  info: run 0 ms, eventfd 깨어남 0 번 (작업 300), SIGCHLD 레코드 0 개 (자식 4)
PASS  count_open_fds() == fds0 && !blocked_now(SIGTERM)
5/39 passed
```

(PASS 5 개는 "카운터 0 이면 epoll 조용" 처럼 빈 스텁에 우연히 맞는 것.) 하네스는 **SIGTERM 이 실제로 막혀 있을 때만** 자기에게 SIGTERM 을 보낸다 — 그렇지 않으면 시작 코드가 기본 동작으로 죽어 버리기 때문이다. 마스크를 되돌리기 전에는 쌓인 시그널을 `sigtimedwait(0)` 으로 먹어 치운다(`mask_restore`).

## 4. 힌트

<details>
<summary>힌트 1 — eventfd 는 8 바이트 정수 하나짜리 "파일"</summary>

`write(efd, &n, 8)` 은 커널 카운터에 n 을 더하고, `read(efd, &v, 8)` 은 카운터를 v 로 돌려주며 0 으로 만든다(`EFD_SEMAPHORE` 면 1 을 돌려주고 1 을 뺀다). 8 바이트가 아니면 `EINVAL`. 0 인데 읽으면 blocking 은 잠들고 `EFD_NONBLOCK` 은 `EAGAIN`. 넘칠 쓰기도 마찬가지. `write`/`read` 는 실패하면 `-1` + `errno` 이니 `-errno` 로 바꿔 돌려준다.

</details>

<details>
<summary>힌트 2 — pool_collect 의 종료 조건은 "카운터 합"</summary>

메인이 카운터를 읽은 직후, 큐에는 **아직 post 하지 않은** 결과가 들어 있을 수 있다(워커가 push 와 post 사이). 그 결과도 같이 꺼내면 다음 read 의 카운터는 "이미 꺼낸" 결과의 몫이다. 그래서 개별 바퀴의 `k` 와 꺼낸 개수는 다를 수 있지만 **총합은 맞는다**. 루프는 `counter_sum < njobs` 동안 돈다 — 마지막 post 가 끝났다는 것은 모든 push 가 끝났다는 뜻이다.

</details>

<details>
<summary>힌트 3 — sigfd_open 은 순서가 정답의 전부</summary>

`sigemptyset` → `sigaddset` 반복 → `pthread_sigmask(SIG_BLOCK, &set, oldmask)` (실패하면 errno 가 아니라 **반환값**이 오류 번호) → `signalfd(-1, &set, SFD_NONBLOCK|SFD_CLOEXEC)`. signalfd 를 열어도 막지 않으면 시그널은 원래대로 배달된다(기본 동작 = 종료 또는 무시). 그리고 이 함수는 **스레드를 만들기 전에** 불려야 한다 — 마스크는 스레드별이고 생성 시 복사된다.

</details>

<details>
<summary>힌트 4 — pidfd 세 함수</summary>

`pidfd_open(pid, 0)` (glibc 2.36+ 래퍼, 없으면 `syscall(SYS_pidfd_open, pid, 0)`), 자식이 끝나면 그 fd 가 `EPOLLIN`. 회수는 `waitid(P_PIDFD, (id_t)pidfd, &si, WEXITED)` → `si.si_code == CLD_EXITED` 면 `si_status` 가 종료 코드, `CLD_KILLED`/`CLD_DUMPED` 면 시그널 번호. 종료 요청은 `pidfd_send_signal(pidfd, SIGTERM, NULL, 0)`.

</details>

<details>
<summary>힌트 5 — daemon_run 은 "종료 단계 점검 → 타임아웃 계산 → epoll_wait → 분배"</summary>

종료 중이면 매 바퀴 먼저: 작업이 다 끝났으면(`completed == submitted`) 워커 join(한 번), join 됐고 자식이 0 이면 반환, 유예가 지났으면 남은 자식에 SIGKILL(한 번). 그 다음 epoll_wait 타임아웃 = min(테스트 상한까지, SIGKILL 마감까지). `EINTR` 은 continue. 이벤트는 `tag_of(data.u64)` 로 `switch`.

</details>

## 5. 풀이

### 0. 세 fd 한눈에

| | eventfd | signalfd | pidfd |
|---|---|---|---|
| 만드는 법 | `eventfd(init, EFD_*)` | 막고 → `signalfd(-1, &set, SFD_*)` | `pidfd_open(pid, 0)` / `clone3(CLONE_PIDFD)` |
| read 가 주는 것 | u64 카운터 (합, 또는 1) | `struct signalfd_siginfo` 128 B × k | read 안 함 → `waitid(P_PIDFD)` |
| EPOLLIN 조건 | 카운터 > 0 | 막힌 집합의 시그널이 pending | 프로세스 종료 (회수 전까지 유지) |
| 합쳐짐 | 카운터라 합쳐짐 (개수는 합에 보존) | 표준 시그널은 종류당 1 개 | 자식마다 fd → 안 합쳐짐 |
| 리눅스 버전 (man-pages 6.17) | 2.6.22, `EFD_SEMAPHORE` 2.6.30 | 2.6.22 | 5.3 (`P_PIDFD` 5.4, `pidfd_send_signal` 5.1) |

glibc 래퍼: 이 VM 의 `nm -D libc.so.6` 에 `pidfd_open@@GLIBC_2.36`, `pidfd_send_signal@@GLIBC_2.36`, `pidfd_getpid@@GLIBC_2.39`. 재미있게도 설치된 man-pages 6.17 의 `pidfd_open(2)` 는 아직 "glibc provides no wrapper ... use syscall(2)" 라고 적혀 있다 — **헤더(`<sys/pidfd.h>`)와 심볼 버전이 더 정확하다.** 오래된 glibc 에서는 `syscall(SYS_pidfd_open, pid, 0)` 로 부른다.

### Part A — eventfd 카운터 의미 (실측 = CHECK 결과)

| 동작 | 결과 | 의미 |
|---|---|---|
| post 3, post 4, take | 7 | 두 번 쓰고 한 번 읽으면 합 |
| take (카운터 0, NONBLOCK) | `-EAGAIN` | blocking 이면 잠든다 |
| post `0xffff…ffff` | `-EINVAL` | 쓸 수 없는 값 (man: 최대 `0xfffffffffffffffe`) |
| post `0xffff…fffe` 후 post 1 | 0, 그 다음 `-EAGAIN` | 넘칠 쓰기 = blocking 이면 잠듦 |
| `read` 4 바이트 버퍼 | `EINVAL` | 버퍼는 8 바이트 이상 |
| SEMAPHORE 초기 3: take ×4 | 1, 1, 1, `-EAGAIN` | 토큰 하나씩 |
| SEMAPHORE post 2, take | 1 | 더해도 한 번에 1 |

```svg
<svg viewBox="0 0 740 265" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">push → post 순서와 카운터 합쳐짐 (워커 W1·W2, 메인 M)</text>
  <path d="M60 60 L720 60 M60 120 L720 120 M60 190 L720 190" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="64" fill="currentColor">W1</text><text x="10" y="124" fill="currentColor">W2</text><text x="10" y="194" fill="currentColor">M</text>
  <rect x="80" y="48" width="70" height="24" class="f-ok" stroke="currentColor"/><text x="88" y="65" fill="currentColor" font-size="12">push r1</text>
  <rect x="160" y="48" width="60" height="24" class="f-acc" stroke="currentColor"/><text x="170" y="65" fill="currentColor" font-size="12">+1 →1</text>
  <rect x="190" y="108" width="70" height="24" class="f-ok" stroke="currentColor"/><text x="198" y="125" fill="currentColor" font-size="12">push r2</text>
  <rect x="270" y="108" width="60" height="24" class="f-acc" stroke="currentColor"/><text x="280" y="125" fill="currentColor" font-size="12">+1 →2</text>
  <rect x="350" y="48" width="70" height="24" class="f-ok" stroke="currentColor"/><text x="358" y="65" fill="currentColor" font-size="12">push r3</text>
  <rect x="380" y="178" width="140" height="24" class="f-warn" stroke="currentColor"/><text x="388" y="195" fill="currentColor" font-size="12">read → 2, 큐 3 개</text>
  <rect x="440" y="48" width="60" height="24" class="f-acc" stroke="currentColor"/><text x="450" y="65" fill="currentColor" font-size="12">+1 →1</text>
  <rect x="560" y="178" width="150" height="24" class="f-warn" stroke="currentColor"/><text x="568" y="195" fill="currentColor" font-size="12">read → 1, 큐 0 개</text>
  <text x="60" y="230" fill="currentColor" font-size="12">바퀴별로는 2 vs 3, 1 vs 0 으로 어긋나도 합은 3 = 3.</text>
  <text x="60" y="250" fill="currentColor" font-size="12">거꾸로(post → push)면 메인이 깨어나 빈 큐를 보고 결과를 놓칠 수 있다.</text>
</svg>
```

워커 4 · 작업 800 실측(solution 9 회 + TSan 1 회): **epoll 깨어남 35, 46, 103, 67, 118, 61, 111, 132, 102 번 (TSan 22 번)** — post 800 번이 카운터 덕분에 그만큼 합쳐졌다. 매번 `counter_sum == 800`, 결과는 정확히 한 번씩. 같은 일을 pipe 에 1 바이트씩 쓰면 바이트 수로 개수는 보존되지만 64 KiB 버퍼가 차면 쓰는 쪽이 막히고(또는 `EAGAIN` 처리), 매 바이트가 메모리를 쓴다. eventfd 는 8 바이트 카운터 하나라 **넘칠 일이 사실상 없고(2^64−2) fd 하나로 양방향 상태가 끝난다.**

push → post 를 거꾸로 하면? 메인이 `+1` 을 보고 깨어나 큐를 비우는 사이 결과가 아직 없다 → 그 바퀴는 0 개, 카운터는 소비됨 → 루프 종료 조건을 "카운터 합" 으로 두면 마지막 결과를 영영 못 꺼내고 끝날 수 있다. 조건 변수의 "상태를 바꾼 뒤 signal" (OS-B06) 과 같은 규칙이다.

### Part B — signalfd: 막아야 읽힌다

signalfd 는 시그널을 가로채지 않는다. 커널은 평소처럼 시그널을 배달하려 하고, **막힌 시그널만** pending 에 남는다. signalfd 는 그 pending 을 읽어 가는 창구일 뿐이다(`signalfd(2)`: "the set of signals ... should be blocked using sigprocmask(2), to prevent the signals being handled according to their default dispositions"). `demo_unblocked.c` 실제 출력:

```text
signalfd 는 시그널을 "훔치지" 않는다 — 막혀서 pending 에 남은 것만 읽는다
  1) SIGUSR1, 안 막음 → killed by User defined signal 1
  1) SIGUSR1, 막음  → exit 0
  2) SIGCHLD, 안 막음 → exit 1 (signalfd 조용 — 시그널이 버려졌다)
  2) SIGCHLD, 막음  → exit 0 (signalfd 에 레코드 있음)
  3) 스레드 먼저 → 메인만 막음, pthread_create 직후 바로 kill: 200 번 중 SIGTERM 에 죽음 5, signalfd 로 받음 195, 기타 0
  3) 스레드 먼저 → 메인만 막음, 스레드가 돌기 시작한 뒤 kill: 200 번 중 SIGTERM 에 죽음 200, signalfd 로 받음 0, 기타 0
```

- **1)** SIGUSR1 의 기본 동작은 종료 → signalfd 를 읽을 틈도 없다.
- **2)** SIGCHLD 의 기본 동작은 "무시" → 막지 않으면 생성 시점에 버려져 signalfd 는 영원히 조용하다. 조용히 망가지는 쪽이라 더 위험하다.
- **3)** 마스크는 **스레드별**이다. 프로세스로 보낸 시그널(`kill`)은 그 시그널을 막지 않은 아무 스레드에게 간다. 메인만 막았으면 워커가 받고, SIGTERM 의 기본 동작은 프로세스 전체 종료다.

3) 의 첫 줄이 이 레슨에서 가장 중요한 관찰이다: **버그가 200 번 중 195 번 숨었다.** 처음 짠 테스트도 "pthread_create 직후 kill" 이었고 버그 쪽이 `exit 0` 으로 통과해 버렸다. 원인은 glibc 의 스레드 생성 절차다 (`strace -f`, 실제 출력):

```text
rt_sigprocmask(SIG_BLOCK, ~[], [], 8)   = 0          ← 만드는 쪽: 잠시 전부 막고
clone3({flags=CLONE_VM|...|CLONE_THREAD|...}) = 291896
[pid 291895] rt_sigprocmask(SIG_SETMASK, [], NULL, 8) = 0   ← 만드는 쪽 복구
[pid 291896] rt_sigprocmask(SIG_SETMASK, [], NULL, 8) = 0   ← 새 스레드가 start 루틴 직전에 복구
```

새 스레드는 커널 입장에서 **전부 막힌 상태로 태어나** 처음 실행될 때 마스크를 복구한다. 그 사이에 온 SIGTERM 은 받을 스레드가 없어 공유 pending 에 남고, 메인의 signalfd 가 읽어 버린다. 그래서 하네스(`thread_case`)는 스레드가 "시작했다" 를 pipe 로 알린 뒤에 kill 한다 → 200/200 재현. 교훈: **"막고 → 스레드" 순서는 테스트로 잡기 어렵다. 코드 리뷰 규칙으로 지켜라** (main 첫 줄 근처에서 막기, 라이브러리가 몰래 스레드를 만들기 전에).

표준 vs 실시간 (CHECK 실측): SIGUSR1 을 두 번 보내면 레코드 1 개(`ssi_code = SI_USER`, `ssi_pid` = 보낸 프로세스), SIGRTMIN 을 `sigqueue` 로 세 번(값 1,2,3) 보내면 레코드 3 개가 순서대로(`SI_QUEUE`, `ssi_int` 1,2,3). 표준 시그널의 pending 은 **비트**, 실시간 시그널은 **큐**다 (`signal(7)`).

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">자식 5 명이 같은 순간 종료 — 부모가 받는 알림</text>
  <g stroke="currentColor">
    <rect x="20" y="45" width="60" height="26" class="f-mut"/><rect x="20" y="80" width="60" height="26" class="f-mut"/>
    <rect x="20" y="115" width="60" height="26" class="f-mut"/><rect x="20" y="150" width="60" height="26" class="f-mut"/>
    <rect x="20" y="185" width="60" height="26" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12"><text x="28" y="63">exit 10</text><text x="28" y="98">exit 11</text><text x="28" y="133">exit 12</text><text x="28" y="168">exit 13</text><text x="28" y="203">exit 14</text></g>
  <path d="M80 58 L250 110 M80 93 L250 113 M80 128 L250 116 M80 163 L250 119 M80 198 L250 122" stroke="currentColor"/>
  <rect x="250" y="95" width="150" height="40" class="f-warn" stroke="currentColor"/>
  <text x="258" y="113" fill="currentColor" font-size="12">SIGCHLD pending 비트</text>
  <text x="258" y="129" fill="currentColor" font-size="12">1 개 (나머지 4 번 버림)</text>
  <text x="410" y="78" fill="currentColor">signalfd 레코드 1 개 (ssi_pid = 첫 자식)</text>
  <text x="410" y="96" fill="currentColor" font-size="12">→ waitpid(-1, WNOHANG) 를 없을 때까지</text>
  <path d="M80 58 L560 168 M80 93 L560 178 M80 128 L560 188 M80 163 L560 198 M80 198 L560 208" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="560" y="160" width="170" height="56" class="f-ok" stroke="currentColor"/>
  <text x="568" y="180" fill="currentColor" font-size="12">pidfd[0..4] 각각 EPOLLIN</text>
  <text x="568" y="198" fill="currentColor" font-size="12">waitid(P_PIDFD) 로 그 자식만</text>
  <text x="10" y="245" fill="currentColor">실측(8 회): SIGCHLD 레코드 1 개 · pidfd 준비 5 개 · 종료 코드 10..14 모두 회수</text>
</svg>
```

결정적으로 만든 방법: 자식 5 명이 같은 pipe 의 EOF 를 기다리고(각자 쓰기 끝을 닫는다 — 안 닫으면 EOF 가 영영 안 온다, OS-B02), 부모가 쓰기 끝을 닫는 순간 같이 종료. 부모는 **pidfd 5 개가 모두 readable 해질 때까지** signalfd 를 읽지 않는다. pidfd 는 회수 전까지 readable 이 유지되므로(LT) 이 대기는 확실히 끝나고, 그동안 SIGCHLD 는 막혀서 pending 비트 하나에 쌓인다. 그래서 `nsig` 는 1 (CHECK 는 안전하게 `1 ≤ nsig < 5`). 실제 출력:

```text
  info: SIGCHLD 레코드 1 개 (첫 ssi_pid=260504) vs pidfd 준비 5 개, pidfd 회수 5 개
```

### Part B — pidfd 가 푸는 문제

1. **합쳐짐**: 위 그림. SIGCHLD 로는 "누군가 죽었다" 만 안다.
2. **남의 자식을 가로채기**: `waitpid(-1)` 루프는 라이브러리가 `posix_spawn`/`system` 으로 만든 자식까지 회수해 버린다. pidfd 는 내가 연 자식만 본다.
3. **pid 재사용**: `kill(pid, SIGTERM)` 사이에 그 자식이 죽고 회수되고 pid 가 재사용되면 **엉뚱한 프로세스**를 죽인다. pidfd 는 프로세스 자체를 가리키므로 회수 뒤에는 `ESRCH` (연습 3 실측: `pidfd_send_signal → -1 ESRCH`, `pidfd_getpid → -1`). 이 VM 의 `pid_max` 는 4194304 라 재사용이 드물 뿐 0 은 아니다.
4. `fork` → `pidfd_open` 사이 창은 **내 자식에게는 안전**하다: 내가 회수하기 전까지 그 pid 는 좀비로 남아 재사용되지 않는다. 단 SIGCHLD 가 `SIG_IGN` 이거나 `SA_NOCLDWAIT` 면 자동 회수되어 창이 생긴다 → 그럴 때는 `clone3(CLONE_PIDFD)` 나 `pidfd_spawn` (glibc 2.39, 이 VM 심볼 확인) 으로 생성과 동시에 pidfd 를 받는다.

### Part C — 데몬: 종료 순서가 설계다

| 단계 | 하는 일 | 왜 이 순서인가 |
|---|---|---|
| init ① | SIGTERM·SIGINT·SIGCHLD 막기 + signalfd | 이후 만드는 모든 스레드가 막힌 마스크를 물려받는다 |
| init ②③ | eventfd + epoll 등록 (`data.u64` 태그) | fd 번호 대신 태그로 분배 → fd 재사용 혼동 없음 |
| init ④ | 워커 생성 | 반드시 ① 뒤 |
| spawn | fork → 자식: (무시 설정) → 마스크 복구 → 일 / 부모: pidfd 등록 | 막힌 마스크는 fork·exec 로 상속된다 (연습 4) |
| SIGTERM | stopping=1, 워커 broadcast, 자식에 `pidfd_send_signal(TERM)`, 마감 = now + 300 ms | 새 작업 거부, 큐에 있던 작업은 끝낸다 |
| 작업 끝 | `completed == submitted` → 워커 join | 워커는 "stopping + 큐 빔" 에서 퇴근 |
| 마감 경과 | 남은 자식에 SIGKILL | systemd `TimeoutStopSec` → SIGKILL 과 같은 사다리 (ELX-B13) |
| 자식 0 | 반환 → `daemon_close` 가 fd 닫고 쌓인 시그널 먹고 마스크 복구 | fd 수·마스크가 시작 때와 같아야 한다 (CHECK) |

자식 쪽의 미묘한 점 두 가지. TERM 무시 자식은 **아직 막혀 있을 때** `SIG_IGN` 을 설정하고 그 다음 마스크를 푼다 — 반대면 그 사이에 SIGTERM 이 와서 죽을 수 있다(무시로 바꾸면 pending 이던 것도 버려진다, POSIX `sigaction`). 그리고 멀티스레드 부모에서 fork 한 자식은 exec 전까지 async-signal-safe 함수만 써야 한다 — 여기서 쓰는 `signal`·`sigprocmask`·`alarm`·`read`·`close`·`pause`·`_exit` 는 모두 `signal-safety(7)` 목록에 있다 (`pthread_sigmask` 도 POSIX.1-2008 TC1 부터 목록에 있지만 자식은 어차피 단일 스레드라 `sigprocmask` 로 충분).

solution 실제 출력 (Part C 부분):

```text
== Part C: 데몬 — epoll 하나에 eventfd + signalfd + pidfd, SIGTERM 으로 깔끔한 종료 ==
PASS  rc == 0 && d->nw == 4 && blocked_now(SIGTERM) && blocked_now(SIGCHLD)
PASS  ok && d->nch == 4 && d->alive == 4
PASS  rc == 0
PASS  d->submitted == 300 && d->completed == 300 && d->result_sum == expect
PASS  d->sigterm_seen == 1 && d->stopping && daemon_submit(d, 999) == -EAGAIN
PASS  d->ch[2].code == 7 && d->ch[3].code == 9
PASS  d->ch[0].sig == SIGTERM && !d->ch[0].killed_hard
PASS  d->ch[1].sig == SIGKILL && d->ch[1].killed_hard
PASS  d->alive == 0 && d->workers_joined
  info: run 307 ms, eventfd 깨어남 1 번 (작업 300), SIGCHLD 레코드 3 개 (자식 4)
PASS  count_open_fds() == fds0 && !blocked_now(SIGTERM)
39/39 passed
```

run 307 ms = 유예 300 ms + 나머지. SIGCHLD 레코드 3 개 / 자식 4 — 여기서도 합쳐졌다(데몬은 세기만 하고 회수는 pidfd 로). 진짜 프로세스로 밖에서 `kill -TERM` 을 보낸 `demo_daemon.c` (solution.c 를 `#include`):

```text
$ ./daemon & sleep 0.5; kill -TERM $!; wait $!; echo "exit=$?"
[daemon 291771] workers=4 children=291777,291778,291779 jobs=1000 — SIGTERM 을 기다림
[daemon] run=0 after 803 ms: completed 1000/1000, sigterm=1, eventfd wakeups=175, SIGCHLD records=3
[daemon] child 0 pid 291777: code=-1 sig=15 (Terminated)
[daemon] child 1 pid 291778: code=-1 sig=15 (Terminated)
[daemon] child 2 pid 291779: code=-1 sig=9 (Killed) ← 유예 후 SIGKILL
exit=0
```

### self-pipe(OS-B07) vs signalfd/pidfd — 코드량과 깨움 지연

**코드량**: `demo_bench.c` 안에 같은 기능(시그널 집합 → epoll 에 넣을 fd → 루프에서 drain)을 두 방식으로 짜고 `/* LOC:이름:begin/end */` 표시 구간의 빈 줄·주석 줄을 뺀 줄 수를 셌다:

```bash
for r in selfpipe signalfd pidfd sigchld; do printf "%-9s " $r; awk -v r=$r '$0 ~ "LOC:"r":begin"{on=1;next} $0 ~ "LOC:"r":end"{on=0} on && !/^[[:space:]]*$/ && !/^[[:space:]]*\/\*.*\*\/[[:space:]]*$/ {n++} END{print n}' demo_bench.c; done
```

```text
selfpipe  27
signalfd  15
pidfd     14
sigchld   5
```

| 방식 | 줄 수 | 빠진 것 / 덤 |
|---|---|---|
| self-pipe (pipe2 + sigaction ×n + errno 보존 핸들러 + drain) | 27 | 바이트 = 시그널 번호뿐. 보낸 pid·uid·`si_status` 없음. 핸들러 = async-signal-safe 규칙 |
| signalfd (막기 + signalfd + siginfo drain) | 15 | `ssi_pid/ssi_uid/ssi_status/ssi_int` 까지. 핸들러 없음 → `printf` 자유 |
| pidfd (open + epoll + waitid + DEL/close) | 14 | 자식당 fd 관리가 늘지만 합쳐짐·가로채기·pid 재사용 문제가 없음 |
| SIGCHLD 회수 루프 `while (waitpid(-1, WNOHANG) > 0)` | 5 | 짧지만 남의 자식까지 회수 |

**깨움 지연** (`demo_bench.c`, 실측 3 회 — 보낸 순간 t0 → epoll_wait 반환 t1, 핑퐁이라 한 번에 하나, µs):

| 경로 | min | p50 | p99 |
|---|---|---|---|
| 스레드 → eventfd write | 0.5–0.6 | 14.7–15.7 | 60–176 |
| 스레드 → pipe write (참고) | 0.6–0.8 | 13.9–15.1 | 39–288 |
| 스레드 → `tgkill(SIGUSR1)` → signalfd | 1.3–1.4 | 15.5–16.6 | 41–226 |
| 스레드 → `tgkill(SIGUSR1)` → 핸들러 → self-pipe | 4.7–5.3 | 19.1–19.8 | 45–312 |
| 자식 `_exit` → pidfd | 13.5–21.2 | 43.9–50.5 | 106–857 |
| 자식 `_exit` → SIGCHLD → signalfd | 19.6–22.9 | 44.0–51.6 | 127–1078 |
| 자식 `_exit` → SIGCHLD → 핸들러 → self-pipe | 25.0–26.8 | 50.0–55.0 | 153–459 |

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">스레드 → 메인 깨움 지연 p50 (µs, 3 회 중 가운데 값) — 막대 1 µs = 25 px</text>
  <g stroke="currentColor">
    <rect x="200" y="40" width="370" height="26" class="f-ok"/>
    <rect x="200" y="80" width="375" height="26" class="f-mut"/>
    <rect x="200" y="120" width="412" height="26" class="f-acc"/>
    <rect x="200" y="160" width="485" height="26" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="58">eventfd</text><text x="10" y="98">pipe (참고)</text>
    <text x="10" y="138">signalfd</text><text x="10" y="178">self-pipe 핸들러</text>
    <text x="210" y="58">14.8</text><text x="210" y="98">15.0</text><text x="210" y="138">16.5</text><text x="210" y="178">19.4</text>
  </g>
  <text x="10" y="214" fill="currentColor" font-size="12">공통 ~14 µs = VM 에서 잠든 스레드를 깨우는 비용. 메커니즘 차이는 min 에서 보인다: 0.5 / 0.7 / 1.3 / 5 µs.</text>
</svg>
```

읽는 법:

- **p50 의 대부분(~14 µs)은 "잠든 스레드를 깨워 CPU 에 올리는" 비용**이고 네 방식이 공유한다. 메커니즘 자체의 차이는 min 에 드러난다: eventfd 0.5 µs, signalfd 1.3 µs(시그널 생성 + siginfo 꺼내기), self-pipe 약 5 µs.
- self-pipe 가 비싼 이유: 핸들러 프레임을 사용자 스택에 쌓고 → 핸들러가 `write` syscall → `rt_sigreturn` → `epoll_wait` 가 `EINTR` 로 돌아오고(`epoll_wait` 은 `SA_RESTART` 여도 재시작되지 않는다, `signal(7)`) → 다시 `epoll_wait`. 실측 `epoll_wait EINTR 19977–19984 / 20000` — 나머지 십여 번은 메인이 아직 epoll_wait 에 들어가기 전에 시그널이 와서 EINTR 없이 처리된 경우다.
- 자식 종료 경로는 `exit` 정리 비용이 지배적이라 pidfd ≈ signalfd, self-pipe 가 +5 µs 정도. p99 는 호스트 부하(load 15–17)에 휘둘려 1 ms 까지 튄다 — 비교에 쓰지 말 것.
- 결론: **지연은 몇 µs 차이일 뿐이고, 진짜 이득은 코드량·정보량·정확성**(합쳐짐, 가로채기, pid 재사용, 핸들러 제약 없음)이다. self-pipe 가 여전히 맞는 곳: 이식성(macOS/BSD 는 kqueue `EVFILT_SIGNAL`·`EVFILT_PROC` 이 대응물 — OS-A11), 그리고 막을 수 없는 동기 시그널(SIGSEGV 등)은 애초에 signalfd 대상이 아니다.

### 펌웨어 연결

- **ISR → 태스크 알림** 과 같은 모양이다: eventfd 카운터 = FreeRTOS 카운팅 세마포어/태스크 알림 값(`ulTaskNotifyTake(pdTRUE)` 가 값 전체를 받고 0 으로 = eventfd read, `pdFALSE` 면 1 씩 = `EFD_SEMAPHORE`) (RTOS-B08, RTOS-M01). "결과는 큐로, 알림은 카운터로" 도 RTOS 의 큐 + 알림 패턴이다.
- **커널 ↔ 사용자 공간**: eventfd 는 KVM `ioeventfd`/`irqfd`(VIRT-A04), VFIO 인터럽트 전달(VIRT-M11)에 쓰인다 — 사용자 공간 드라이버가 인터럽트를 epoll 로 받는 길. UIO 의 `read(/dev/uioN)` 도 "인터럽트 횟수" 카운터를 돌려주는 같은 발상이다(UIO 문서 기준, 이 VM 에서 실험하지는 않았다).
- **프로세스 감시자**: 게이트웨이 보드에서 모뎀 매니저·센서 데몬을 자식으로 띄워 감시하는 슈퍼바이저는 pidfd 로 짜면 pid 재사용·합쳐짐 걱정이 없다. systemd 도 자식 감시에 pidfd 를 쓰는 방향으로 옮겨 왔다(버전 세부는 확인하지 않음).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

빌드·검증 (VM, 실제 결과): `gcc … solution.c` 39/39 (8 회 반복 동일, 0.3 s), `clang` 39/39, gcc·clang `-fsanitize=address,undefined` 39/39 오류 0, `clang -fsanitize=thread` 39/39 · WARNING 0 (3 회). `/tmp` 밖의 다른 cwd 에서 실행해도 같다 (파일을 만들지 않는다).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| signalfd 만 열고 안 막음 | 시그널은 기본 동작대로 배달 — SIGTERM 은 종료, SIGCHLD 는 버려짐 (demo 1·2) | `pthread_sigmask(SIG_BLOCK, &set, NULL)` 후 `signalfd(-1, &set, …)` |
| 스레드를 만든 뒤 메인만 막음 | 마스크는 스레드별 — 안 막은 스레드가 SIGTERM 을 받아 프로세스 종료 (Part B). 생성 직후엔 숨는다 (195/200) | main 첫 줄 근처에서 막고 그 뒤에 모든 스레드 생성 |
| 막힌 마스크로 fork/exec | 자식(그리고 exec 한 프로그램)도 SIGTERM 을 막은 채 → `systemctl stop` 이 안 먹음 (연습 4: exit 42) | 자식에서 exec 전에 `sigprocmask(SIG_SETMASK, &orig, NULL)` (`posix_spawnattr_setsigmask` 도 가능) |
| SIGCHLD 레코드 1 개 = 자식 1 명 | 표준 시그널은 합쳐진다 (5 명 → 1 개) | `while (waitpid(-1, &st, WNOHANG) > 0)` 또는 자식마다 pidfd |
| eventfd 에 post 먼저, push 나중 | 메인이 깨어나 빈 큐를 보고 카운터만 소비 → 결과 유실/지연 | 큐에 넣고 unlock 한 뒤 `ev_post(efd, 1)` |
| eventfd 를 4 바이트로 read/write | `EINVAL` (CHECK 실측) | 항상 `uint64_t` 8 바이트 |
| blocking eventfd 를 epoll 에 넣고 "혹시 몰라" 두 번 read | 두 번째 read 가 카운터 0 에서 영원히 잠듦 | `EFD_NONBLOCK` + `EAGAIN` 은 정상 |
| 회수한 뒤 `kill(pid, …)` | pid 가 재사용되면 남을 죽인다 | `pidfd_send_signal` — 회수 뒤엔 `ESRCH` |
| self-pipe/epoll 루프에서 `EINTR` 을 오류로 처리 | `epoll_wait` 은 핸들러가 돌면 `SA_RESTART` 여도 `EINTR` | `if (errno == EINTR) continue;` |
| 마스크를 되돌리기 전에 pending 을 안 비움 | 막아 둔 SIGTERM 이 풀리는 순간 기본 동작으로 죽음 | `sigtimedwait(&set, NULL, &zero)` 로 먹고 복구 (`mask_restore`) |

## 8. 면접 Q&A

<details>
<summary>Q1. 다른 스레드가 epoll_wait 에서 자고 있는 이벤트 루프를 깨우려면?</summary>

eventfd 하나를 epoll 에 등록해 두고 다른 스레드가 `write(efd, &one, 8)`. 카운터라서 여러 번 써도 한 번 깨어나 합을 읽으면 되고, 버퍼가 찰 걱정이 없다(최대 2^64−2). 데이터는 뮤텍스 큐로 넘기고 eventfd 는 "봐라" 만 — 순서는 push → post. pipe 로도 되지만 fd 2 개, 버퍼가 차면 쓰는 쪽이 막히거나 EAGAIN 처리가 필요하다. 꼬리: "카운터 합과 꺼낸 개수가 다르면?" → 정상. push 와 post 사이에 메인이 끼면 바퀴별로 어긋나지만 총합은 맞는다. 종료 조건은 카운터 합으로.

</details>

<details>
<summary>Q2. EFD_SEMAPHORE 는 언제 쓰나요?</summary>

read 가 값 전체가 아니라 1 을 돌려주고 1 을 빼므로 카운팅 세마포어가 된다. 여러 소비자 스레드가 같은 eventfd 를 blocking read 하면 토큰 하나씩 나눠 가진다 (연습 1: 토큰 3, 스레드 8 → 동시 실행 최대 3 실측). 일반 모드에서 여러 소비자가 읽으면 한 명이 합 전체를 가져가 버린다. 단 epoll 과 같이 쓰면 "readable" 이 카운터 > 0 이라 여러 스레드가 동시에 깨어날 수 있다(thundering herd) — 그 경우 `EFD_NONBLOCK` 으로 읽고 EAGAIN 이면 다시 잔다.

</details>

<details>
<summary>Q3. 멀티스레드 프로그램에서 시그널을 안전하게 받는 방법은?</summary>

① 스레드를 만들기 전에 main 이 `pthread_sigmask(SIG_BLOCK, {TERM, INT, HUP, CHLD})` → 모든 스레드가 물려받는다. ② 받는 곳을 하나로: signalfd 를 이벤트 루프에 넣거나, 전용 스레드가 `sigwaitinfo`/`sigwait`. 핸들러가 없으니 async-signal-safe 제약도 없다. 왜 ① 이 필수인가: 프로세스로 보낸 시그널은 막지 않은 **아무** 스레드에게 간다 — SIGTERM 이면 그 스레드에서 기본 동작(프로세스 종료). 꼬리: "테스트로 잡을 수 있나?" → 어렵다. glibc 는 새 스레드를 전부 막힌 마스크로 시작하므로 생성 직후 kill 하면 200 번 중 195 번 통과했다. 규칙으로 지킨다. SIGSEGV 같은 동기 시그널은 일으킨 스레드로 가므로 막으면 안 된다.

</details>

<details>
<summary>Q4. signalfd 가 시그널 핸들러보다 나은 점과 못한 점은?</summary>

나은 점: 일반 코드 문맥에서 처리(락·malloc·printf 자유), 루프 하나로 통합, `ssi_pid`/`ssi_uid`/`ssi_status`/`ssi_int` 등 siginfo 전체, EINTR 난리 없음(시그널이 막혀 있으니 syscall 이 안 끊긴다), 코드 27 → 15 줄. 못한 점: 리눅스 전용. 막아야 하므로 fork/exec 한 자식에 마스크가 새는 문제(연습 4). 표준 시그널 합쳐짐은 똑같다. 동기 시그널(SIGSEGV/SIGBUS/SIGFPE)은 대상이 아니다. 꼬리: "fork 하면 signalfd 는?" → fd 는 상속되고, 자식이 읽으면 **자식의** pending 을 읽는다 (`signalfd(2)` fork semantics).

</details>

<details>
<summary>Q5. 자식 5 명이 동시에 죽었는데 SIGCHLD 가 한 번만 왔다. 왜, 그리고 어떻게?</summary>

SIGCHLD 는 표준 시그널이라 pending 이 비트 하나 — 이미 pending 이면 새 것은 버려진다. 그래서 SIGCHLD 는 "적어도 한 명 상태가 바뀜" 일 뿐이고, 처리는 `while ((pid = waitpid(-1, &st, WNOHANG)) > 0)` 로 "더 없을 때까지" (연습 5: 레코드 1 → 5 명 회수). 더 나은 길은 자식마다 pidfd: fd 마다 따로 EPOLLIN, `waitid(P_PIDFD)` 로 그 자식만 (실측: 레코드 1 vs pidfd 5). 꼬리: "실시간 시그널로 바꾸면?" → SIGCHLD 의 번호는 못 바꾼다. 실시간 시그널은 큐라서 3 번 보내면 3 개 오지만(Part B 실측), 큐 길이 한도(`RLIMIT_SIGPENDING`)가 있다.

</details>

<details>
<summary>Q6. pidfd 를 쓰면 무엇이 안전해지나요? fork 와 pidfd_open 사이의 틈은?</summary>

pid 는 숫자라 회수 뒤 재사용될 수 있다 → `kill(pid)` 가 남을 죽일 수 있다. pidfd 는 프로세스 객체를 가리키므로 회수 뒤에는 `ESRCH` (연습 3 실측). 또 `waitid(P_PIDFD)` 는 내 자식 하나만 회수해 라이브러리 자식을 가로채지 않는다. 틈: 내 자식은 **내가 회수하기 전까지 좀비로 남아 pid 가 재사용되지 않으므로** fork → pidfd_open 은 안전하다. 단 SIGCHLD 를 `SIG_IGN`/`SA_NOCLDWAIT` 로 두면 자동 회수돼 틈이 생긴다 → `clone3(CLONE_PIDFD)` 나 `pidfd_spawn` (glibc 2.39). 남의 프로세스에 대한 pidfd_open 은 열기 전에 pid 가 바뀌었을 수 있으니 열고 나서 `/proc` 정보 등으로 확인해야 한다.

</details>

<details>
<summary>Q7. SIGTERM 을 받은 데몬의 "깔끔한 종료" 순서를 말해 보세요.</summary>

① 새 요청 거부(stopping 플래그, 리스닝 소켓 닫기) ② 진행 중·큐에 있는 작업 drain(워커는 "stopping + 큐 빔" 에서 퇴근) → join ③ 자식에게 TERM (pidfd_send_signal) → 유예 → KILL ④ 모두 회수(좀비 0) ⑤ 버퍼 flush·상태 저장 ⑥ fd 닫기, exit 0. systemd 는 `TimeoutStopSec`(기본 90 s) 뒤 SIGKILL 하므로 유예는 그보다 짧게. watchdog(`WatchdogSec`, `sd_notify("WATCHDOG=1")`)을 쓰면 종료 중에도 핑을 계속하거나 `EXTEND_TIMEOUT_USEC` 로 늘린다 (ELX-B13). 꼬리: "종료 중 또 SIGTERM 이 오면?" → 이미 stopping 이면 무시(멱등)하거나, 두 번째 SIGINT 는 즉시 종료로 승격하는 CLI 관례도 있다.

</details>

<details>
<summary>Q8. epoll 에 fd 대신 data.u64 태그를 넣는 이유는?</summary>

fd 번호는 close 후 재사용된다. 같은 epoll_wait 묶음 안에서 앞 이벤트를 처리하다 fd 를 닫고 새 fd 가 같은 번호로 열리면 뒤 이벤트가 엉뚱한 객체로 간다. `(종류 << 32) | 인덱스` 태그면 객체 테이블로 바로 가고, 객체에 `alive` 플래그를 둬서 "이미 처리됨" 을 걸러낼 수 있다(`d_on_child` 의 `!alive` 검사). 또 닫기 전에 `EPOLL_CTL_DEL` — epoll 은 "열린 파일 설명(open file description)" 기준으로 등록을 들고 있어서, fd 를 dup 했거나 fork 로 자식이 같은 파일을 들고 있으면 close 만으로는 등록이 안 빠진다 (`epoll(7)` Q&A).

</details>

## 9. 추가 연습

### 연습 1 — EFD_SEMAPHORE 토큰 풀

eventfd 하나(`EFD_SEMAPHORE`, 초기값 3)로 "동시에 최대 3 개만 일한다" 를 만든다. 스레드 8 × 200 회: blocking read 로 토큰 1 개, 일, write 1 로 반납. 동시 실행 수의 최대값이 3 을 넘지 않고, 끝나면 토큰이 정확히 3 개 남는다.

<details>
<summary>답</summary>

`tok_worker`: `read(efd, &one, 8)` (EINTR 이면 다시) → `active` 원자 증가, `max_active` 를 CAS 로 갱신 → 일 → 감소 → `write(efd, &(uint64_t){1}, 8)`. 끝난 뒤 `O_NONBLOCK` 으로 바꿔 EAGAIN 까지 읽으면 3 개. 실측 `max_active=3`. RTOS 의 카운팅 세마포어(DMA 채널 3 개 공유)와 같다.

</details>

### 연습 2 — timerfd 도 같은 루프에

`timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)` 에 2 ms 주기를 걸고 30 ms 동안 읽지 않은 뒤, eventfd 와 함께 epoll 한 번에 둘 다 나오는지, timerfd read 가 **만료 횟수**(카운터)인지 확인한다.

<details>
<summary>답</summary>

timerfd 의 read 도 u64 = 지난 read 이후 만료 횟수 — eventfd 와 같은 "합쳐지는 카운터" 다. 실측 30 ms 뒤 16 번 (CHECK 는 하한 10 만). 읽은 직후 다시 읽으면 EAGAIN. 주기 작업을 `sleep` 대신 timerfd 로 하면 루프가 하나로 유지되고, 늦어도 몇 번 놓쳤는지 안다 — 1 kHz 제어 루프의 오버런 감지와 같은 쓰임.

</details>

### 연습 3 — pidfd_send_signal 과 ESRCH

자식을 만들고 pidfd 로 SIGKILL → poll 로 readable → 회수 전 `pidfd_send_signal(sig 0)` 은 0(좀비도 존재), `waitid(P_PIDFD)` 로 `CLD_KILLED/SIGKILL` 확인 → 회수 뒤 `pidfd_send_signal` 은 `ESRCH`, 두 번째 `waitid` 는 `ECHILD`.

<details>
<summary>답</summary>

실측 7/7 PASS, 그리고 `회수 뒤 pidfd_getpid = -1 (errno No such process)`. 숫자 pid 였다면 회수 뒤 같은 번호가 다른 프로세스에 배정됐을 때 `kill` 이 그 프로세스로 갔을 것이다. pidfd 는 "그 프로세스가 끝났다" 를 기억한다.

</details>

### 연습 4 — 막힌 마스크는 exec 를 건너간다

SIGTERM 을 막은 부모가 `/bin/sh -c 'kill -TERM $$; exit 42'` 를 fork+exec 한다. 마스크를 안 되돌린 자식과 되돌린 자식의 종료 상태를 비교한다.

<details>
<summary>답</summary>

실측 `안 되돌림 → exit 42 | 되돌림 → signal 15`. 시그널 마스크와 pending 은 exec 를 지나도 남는다 (`signalfd(2)` execve semantics, POSIX `exec`). signalfd 데몬이 띄운 자식이 `systemctl stop` 에 반응하지 않는 실제 버그의 원인이다. exec 전 `sigprocmask(SIG_SETMASK, &orig, NULL)` 또는 `posix_spawnattr_setsigmask` + `POSIX_SPAWN_SETSIGMASK`.

</details>

### 연습 5 — pidfd 없이: SIGCHLD + waitpid 루프

자식 5 명이 동시에 종료하게 하고(`waitid(P_PID, pid, WEXITED|WNOHANG|WNOWAIT)` 로 회수하지 않고 엿보며 5 명 모두 좀비가 될 때까지 기다림), signalfd 를 비운 뒤 `waitpid(-1, WNOHANG)` 를 "없을 때까지" 돌린다.

<details>
<summary>답</summary>

실측 `SIGCHLD 레코드 1 개, waitpid 루프 회수 5 명`, 종료 코드 합 20+…+24, 이후 `waitpid → ECHILD`. 레코드 수만큼 waitpid 를 부르면 4 명이 좀비로 남는다. pidfd 를 못 쓰는 커널(< 5.3)·이식 코드의 정답이 이 루프다(OS-B07 과 같은 결론).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
# VM 안, /tmp/OS-M18/
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr      # 18/18 passed (5 회 반복 동일, 0.05 s)
clang -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_t && ./pr_t  # 18/18, WARNING 0
```

gcc ASan+UBSan 도 18/18, 오류 0. 데모 (위 5. 풀이에 실제 출력):

```bash
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_unblocked.c -o unblocked && ./unblocked
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o bench && ./bench          # N=20000 (인자로 변경)
gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_daemon.c -o daemon && { ./daemon & sleep 0.5; kill -TERM $!; wait $!; echo "exit=$?"; }
```

<!-- code: demo_unblocked.c -->

<!-- code: demo_bench.c -->

<!-- code: demo_daemon.c -->

## 10. 핵심 정리

- eventfd = 커널 u64 카운터. write 는 더하기, read 는 합을 받고 0 (`EFD_SEMAPHORE` 는 1 씩). 0 이면 `EAGAIN`/잠듦, 최대 `0xfffffffffffffffe`, 8 바이트가 아니면 `EINVAL`.
- 스레드 → 루프 알림: **결과는 큐로, 알림은 eventfd 로, push 다음 post.** 카운터 합이 진실 — 800 post 가 35–132 번 깨어남으로 합쳐졌다 (실행마다 다름).
- signalfd 는 시그널을 훔치지 않는다. **스레드를 만들기 전에 모든 시그널을 막아야** pending 에 남아 읽힌다. 안 막으면 TERM 은 죽고, CHLD 는 버려진다.
- 그 버그는 테스트로 잘 안 잡힌다: glibc 는 새 스레드를 전부 막힌 마스크로 시작 → 생성 직후 kill 은 195/200 통과. 규칙으로 지킨다.
- 표준 시그널은 합쳐진다(5 → 1), 실시간 시그널은 쌓인다(3 → 3). SIGCHLD 는 "누군가" 일 뿐 → waitpid 루프, 또는 **자식마다 pidfd** (5 → 5).
- pidfd: `pidfd_open` → EPOLLIN → `waitid(P_PIDFD)`, 종료 요청은 `pidfd_send_signal` (회수 뒤 `ESRCH` → pid 재사용 안전).
- 막힌 마스크는 fork·exec 로 상속된다 → 자식은 exec 전에 원래 마스크로.
- self-pipe 대비: 코드 27 → 15 줄, 깨움 지연 min 5 → 1.3 µs (p50 은 공통 ~14 µs 깨움 비용이 지배). 진짜 이득은 정보량과 정확성.

## 참고자료

- `eventfd(2)`, `signalfd(2)`, `pidfd_open(2)`, `pidfd_send_signal(2)`, `wait(2)`(`waitid`, `P_PIDFD`), `epoll_wait(2)`, `epoll(7)`, `signal(7)`(표준 vs 실시간, 재시작되지 않는 인터페이스), `timerfd_create(2)` — https://man7.org/linux/man-pages/ (이 레슨의 버전 정보는 VM 의 man-pages 6.17 에서 확인)
- glibc 2.43 `<sys/pidfd.h>`, `<sys/eventfd.h>`, `<sys/signalfd.h>` 와 `nm -D libc.so.6` 심볼 버전 (VM 에서 확인)
- POSIX.1-2024 `pthread_sigmask`, `sigaction`(SIG_IGN 설정 시 pending 버림), 2.4.3 Signal Actions(async-signal-safe 목록), `exec`(마스크 상속) — https://pubs.opengroup.org/onlinepubs/9799919799/
- OSTEP 5장 "Process API" (fork/wait), 33장 "Event-based Concurrency" (select/poll 이벤트 루프, 블로킹 문제) — https://pages.cs.wisc.edu/~remzi/OSTEP/
- Christian Brauner, pidfd 관련 LWN 기사 "Toward race-free process signaling"(2018) 등 — https://lwn.net/ (개념 배경, 세부 버전은 man-pages 기준)
- D. J. Bernstein, "The self-pipe trick" — https://cr.yp.to/docs/selfpipe.html
- 관련 레슨: OS-B07 (시그널·self-pipe), OS-B01 (fork/wait, 좀비), OS-B02 (pipe EOF·fd 상속), OS-B06 (상태 변경 후 signal), OS-B14 (IPC 비교 하네스), OS-M13 (epoll LT/ET), OS-M15 (POSIX mq, mq_notify), OS-A11 (kqueue 데몬, SIGTERM/SIGHUP), OS-A01 (futex), ELX-B13 (systemd 정지·watchdog), RTOS-B08 (ISR → 세마포어), RTOS-M01 (태스크 알림), C-M14 (ISR 공유 데이터), VIRT-A04 (ioeventfd·irqfd), VIRT-M11 (VFIO)
